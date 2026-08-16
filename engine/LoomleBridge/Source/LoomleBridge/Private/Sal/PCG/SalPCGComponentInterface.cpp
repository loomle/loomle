// Copyright 2026 Loomle contributors.

#include "SalPCGComponentInterface.h"

#include "../SalDiagnostics.h"
#include "../SalObjectBuilder.h"
#include "../SalResultTargets.h"
#include "AssetRegistry/AssetData.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Components/ActorComponent.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "GameFramework/Actor.h"
#include "Misc/EngineVersionComparison.h"
#include "Misc/SecureHash.h"
#include "Misc/PackageName.h"
#include "Modules/ModuleManager.h"
#include "PCGComponent.h"
#include "PCGGraph.h"
#include "StructUtils/PropertyBag.h"
#include "UObject/EnumProperty.h"
#include "UObject/Package.h"
#include "UObject/UnrealType.h"

namespace Loomle::Sal
{
namespace
{
constexpr int32 MaxGraphInterfaceDepth = 32;
constexpr int32 MaxGraphBindingStringChars = 64 * 1024;
constexpr int32 MaxParameterCount = 4096;
constexpr int32 MaxParameterEvidenceChars = 64 * 1024;
constexpr int32 MaxParameterValueChars = 8 * 1024;
constexpr int32 DefaultCollectionLimit = 50;
constexpr int32 MaxCollectionLimit = 200;
constexpr EObjectFlags IncompleteLoadFlags =
    RF_NeedLoad
    | RF_NeedPostLoad
    | RF_NeedPostLoadSubobjects
    | RF_WillBeLoaded;

struct FGraphBindingSnapshot
{
    UPCGGraphInstance* Owned = nullptr;
    UPCGGraphInterface* Direct = nullptr;
    UPCGGraph* TopGraph = nullptr;
    TArray<UPCGGraphInstance*> Instances;
    FString Kind = TEXT("none");
    bool bComplete = true;
    TSharedPtr<FJsonObject> PcgTarget;
    TArray<TSharedPtr<FJsonObject>> Diagnostics;
};

struct FParameterEntry
{
    FGuid Id;
    FString IdText;
    FString Name;
    FString ValueType;
    TArray<FString> ContainerTypes;
    FString ValueTypeObject;
    bool bHasValueTypeObject = false;
    bool bOverridden = false;
    bool bValueAvailable = false;
    FString EffectiveSource;
    TSharedPtr<FJsonValue> LocalValue;
    TSharedPtr<FJsonValue> EffectiveValue;
    FString LocalFingerprint;
    FString EffectiveFingerprint;
};

struct FParameterSnapshot
{
    bool bComplete = true;
    bool bResultTooLarge = false;
    FString Message;
    TArray<FString> BindingEvidence;
    TArray<FParameterEntry> Entries;
};

TSharedPtr<FJsonObject> LevelTargetValue(
    const FSalResolvedTarget& Target);

bool HasExactClauses(const FSalQuery& Query);

TSharedPtr<FJsonObject> QueryError(
    const FString& Code,
    const FString& Message,
    const FString& Operation,
    const FString& Ref = FString(),
    const TArray<FString>& Supported = {})
{
    FSalDiagnosticBuilder Diagnostic = FSalDiagnostics::Error(Code, Message)
        .Interface(TEXT("pcg_component"))
        .Operation(Operation);
    if (!Ref.IsEmpty())
    {
        Diagnostic.Ref(Ref);
    }
    if (!Supported.IsEmpty())
    {
        Diagnostic.Supported(Supported);
    }
    return FSalDiagnostics::Result(Diagnostic.Build());
}

TSharedPtr<FJsonObject> Warning(
    const FString& Message,
    const FString& Operation,
    const FString& Ref = FString())
{
    FSalDiagnosticBuilder Diagnostic = FSalDiagnostics::Warning(
            TEXT("validation.reference_scan_incomplete"),
            Message)
        .Interface(TEXT("pcg_component"))
        .Operation(Operation);
    if (!Ref.IsEmpty())
    {
        Diagnostic.Ref(Ref);
    }
    return Diagnostic.Build();
}

UPCGComponent* ResolvedComponent(const FSalResolvedTarget& Target)
{
    UPCGComponent* Component = Target.Domain == ESalDomain::PcgComponent
        ? Cast<UPCGComponent>(Target.Object)
        : nullptr;
    return IsValid(Component)
            && !Component->HasAnyFlags(IncompleteLoadFlags)
            && !Component->IsLocalComponent()
            && Component->GetConstOriginalComponent() == Component
        ? Component
        : nullptr;
}

FString CanonicalField(
    const FSalResolvedTarget& Target,
    const TCHAR* Name)
{
    FString Value;
    if (Target.CanonicalTarget.IsValid())
    {
        Target.CanonicalTarget->TryGetStringField(Name, Value);
    }
    return Value;
}

FString CreationMethodText(const UActorComponent* Component)
{
    if (Component == nullptr)
    {
        return FString();
    }
    switch (Component->CreationMethod)
    {
    case EComponentCreationMethod::Native:
        return TEXT("Native");
    case EComponentCreationMethod::SimpleConstructionScript:
        return TEXT("SimpleConstructionScript");
    case EComponentCreationMethod::Instance:
        return TEXT("Instance");
    case EComponentCreationMethod::UserConstructionScript:
    default:
        return TEXT("UserConstructionScript");
    }
}

FString DeclaringClassFromSCSId(const FString& Source, const FString& Id)
{
    int32 Separator = INDEX_NONE;
    return Source == TEXT("scs")
            && Id.FindLastChar(TEXT('#'), Separator)
            && Separator > 0
        ? Id.Left(Separator)
        : FString();
}

bool IsLoadedGraphObject(const UObject* Object)
{
    return IsValid(Object)
        && !Object->HasAnyFlags(
            IncompleteLoadFlags
            | RF_NewerVersionExists);
}

bool ConsumeGraphObjectText(
    const UObject* Object,
    int32& InOutChars)
{
    if (Object == nullptr)
    {
        return true;
    }
    const FString Path = Object->GetPathName();
    const FString Type = Object->GetClass()->GetPathName();
    if (Path.Len() > MaxGraphBindingStringChars
        || Type.Len() > MaxGraphBindingStringChars
        || InOutChars > MaxGraphBindingStringChars - Path.Len()
        || InOutChars + Path.Len()
            > MaxGraphBindingStringChars - Type.Len())
    {
        return false;
    }
    InOutChars += Path.Len() + Type.Len();
    return true;
}

TSharedPtr<FJsonObject> CanonicalPcgTarget(UPCGGraph* Graph)
{
    if (!IsLoadedGraphObject(Graph)
        || !Graph->IsAsset()
        || Graph->GetTypedOuter<UPCGGraph>() != nullptr)
    {
        return nullptr;
    }
    UPackage* Package = Graph->GetOutermost();
    if (Package == nullptr
        || Graph->GetOuter() != Package
        || Package == GetTransientPackage()
        || Package->HasAnyFlags(RF_Transient)
        || Package->HasAnyPackageFlags(PKG_PlayInEditor)
        || !FPackageName::IsValidLongPackageName(
            Package->GetName())
        || FPackageName::IsTempPackage(Package->GetName()))
    {
        return nullptr;
    }

    const IAssetRegistry& Registry =
        FModuleManager::LoadModuleChecked<FAssetRegistryModule>(
            TEXT("AssetRegistry"))
            .Get();
    const FAssetData Data = Registry.GetAssetByObjectPath(
        FSoftObjectPath(Graph->GetPathName()),
        true);
    const FString ActualType = Graph->GetClass()->GetPathName();
    if (!Data.IsValid()
        || !Data.IsTopLevelAsset()
        || Data.PackageName != Package->GetFName()
        || Data.AssetName != Graph->GetFName()
        || !Data.GetSoftObjectPath().GetSubPathString().IsEmpty()
        || Data.AssetClassPath.ToString() != ActualType
        || Data.GetSoftObjectPath().ToString() != Graph->GetPathName())
    {
        return nullptr;
    }

    TSharedPtr<FJsonObject> Target = MakeShared<FJsonObject>();
    Target->SetStringField(TEXT("kind"), TEXT("target"));
    Target->SetStringField(TEXT("domain"), TEXT("pcg"));
    Target->SetStringField(
        TEXT("asset"),
        Data.GetSoftObjectPath().ToString());
    Target->SetStringField(TEXT("type"), ActualType);
    return Target;
}

FGraphBindingSnapshot ReadGraphBinding(
    UPCGComponent* Component,
    const FString& Operation)
{
    FGraphBindingSnapshot Out;
    UPCGGraphInstance* Owned = Component != nullptr
        ? Component->GetGraphInstance()
        : nullptr;
    if (!IsLoadedGraphObject(Owned)
        || Owned->GetOuter() != Component)
    {
        Out.bComplete = false;
        Out.Diagnostics.Add(Warning(
            TEXT("The Component-owned GraphInstance is absent, incomplete, or does not belong directly to the exact Component."),
            Operation));
        return Out;
    }
    Out.Owned = Owned;
    Out.Instances.Add(Owned);

    if (!Owned->Graph.IsResolved())
    {
        Out.bComplete = false;
        Out.Diagnostics.Add(Warning(
            TEXT("The direct PCG GraphInterface is not already resolved; Query refused to resolve or load its object handle."),
            Operation,
            Component->GetPathName()));
        return Out;
    }

    UPCGGraphInterface* Direct = Owned->Graph.Get();
    if (Direct == nullptr)
    {
        return Out;
    }
    int32 TextChars = 0;
    if (!IsLoadedGraphObject(Direct)
        || !ConsumeGraphObjectText(Direct, TextChars))
    {
        Out.bComplete = false;
        Out.Kind = TEXT("none");
        Out.Diagnostics.Add(Warning(
            TEXT("The direct PCG GraphInterface is incomplete, superseded, or exceeds the bounded string budget; Query did not inspect it."),
            Operation,
            Component->GetPathName()));
        return Out;
    }
    Out.Direct = Direct;
    Out.Kind = Out.Direct->IsA<UPCGGraph>()
        ? TEXT("graph")
        : Out.Direct->IsA<UPCGGraphInstance>()
            ? TEXT("graph_instance")
            : TEXT("none");
    if (Out.Kind == TEXT("none"))
    {
        Out.Direct = nullptr;
        Out.bComplete = false;
        Out.Diagnostics.Add(Warning(
            TEXT("The direct PCG GraphInterface has an unsupported native subtype; Query did not reinterpret it as a Graph or GraphInstance."),
            Operation,
            Component->GetPathName()));
        return Out;
    }

    TSet<const UPCGGraphInterface*> Seen;
    UPCGGraphInterface* Current = Out.Direct;
    for (int32 Depth = 0; Current != nullptr; ++Depth)
    {
        if (Depth >= MaxGraphInterfaceDepth
            || Seen.Contains(Current)
            || !IsLoadedGraphObject(Current)
            || (Current != Out.Direct
                && !ConsumeGraphObjectText(Current, TextChars)))
        {
            Out.bComplete = false;
            break;
        }
        Seen.Add(Current);
        if (UPCGGraph* Graph = Cast<UPCGGraph>(Current))
        {
            Out.TopGraph = Graph;
            break;
        }
        UPCGGraphInstance* Instance = Cast<UPCGGraphInstance>(Current);
        if (Instance == nullptr)
        {
            Out.bComplete = false;
            break;
        }
        Out.Instances.Add(Instance);
        if (!Instance->Graph.IsResolved())
        {
            Out.bComplete = false;
            break;
        }
        UPCGGraphInterface* Next = Instance->Graph.Get();
        if (Next == nullptr)
        {
            Current = nullptr;
            break;
        }
        Current = Next;
    }
    if (!Out.bComplete)
    {
        Out.Diagnostics.Add(Warning(
            TEXT("The loaded PCG GraphInterface chain is cyclic, incomplete, unsupported, unresolved, or exceeds its bounded depth/string budget; Query did not follow it further."),
            Operation,
            Component->GetPathName()));
        return Out;
    }
    Out.PcgTarget = CanonicalPcgTarget(Out.TopGraph);
    return Out;
}

FString GuidText(const FGuid& Guid)
{
    return Guid.ToString(EGuidFormats::DigitsWithHyphensLower);
}

bool ParseCanonicalParameterGuid(
    const FString& Text,
    FGuid& OutGuid)
{
    OutGuid.Invalidate();
    return FGuid::ParseExact(
            Text,
            EGuidFormats::DigitsWithHyphens,
            OutGuid)
        && OutGuid.IsValid()
        && Text == GuidText(OutGuid);
}

bool ConsumeParameterText(const FString& Text, int32& InOutChars)
{
    if (Text.Len() > MaxParameterEvidenceChars
        || InOutChars > MaxParameterEvidenceChars - Text.Len())
    {
        return false;
    }
    InOutChars += Text.Len();
    return true;
}

FString ValueTypeText(const EPropertyBagPropertyType Type)
{
    switch (Type)
    {
    case EPropertyBagPropertyType::Bool: return TEXT("bool");
    case EPropertyBagPropertyType::Byte: return TEXT("byte");
    case EPropertyBagPropertyType::Int32: return TEXT("int32");
    case EPropertyBagPropertyType::Int64: return TEXT("int64");
    case EPropertyBagPropertyType::Float: return TEXT("float");
    case EPropertyBagPropertyType::Double: return TEXT("double");
    case EPropertyBagPropertyType::Name: return TEXT("name");
    case EPropertyBagPropertyType::String: return TEXT("string");
    case EPropertyBagPropertyType::Text: return TEXT("text");
    case EPropertyBagPropertyType::Enum: return TEXT("enum");
    case EPropertyBagPropertyType::Struct: return TEXT("struct");
    case EPropertyBagPropertyType::Object: return TEXT("object");
    case EPropertyBagPropertyType::SoftObject: return TEXT("soft_object");
    case EPropertyBagPropertyType::Class: return TEXT("class");
    case EPropertyBagPropertyType::SoftClass: return TEXT("soft_class");
#if UE_VERSION_NEWER_THAN_OR_EQUAL(5, 8, 0)
    case EPropertyBagPropertyType::Int8: return TEXT("int8");
    case EPropertyBagPropertyType::Int16: return TEXT("int16");
    case EPropertyBagPropertyType::UInt16: return TEXT("uint16");
#endif
    case EPropertyBagPropertyType::UInt32: return TEXT("uint32");
    case EPropertyBagPropertyType::UInt64: return TEXT("uint64");
    case EPropertyBagPropertyType::None: return TEXT("none");
    default: return FString();
    }
}

FString ContainerTypeText(const EPropertyBagContainerType Type)
{
    switch (Type)
    {
    case EPropertyBagContainerType::Array: return TEXT("array");
    case EPropertyBagContainerType::Set: return TEXT("set");
#if UE_VERSION_NEWER_THAN_OR_EQUAL(5, 8, 0)
    // A Map also has an independent key type and key type object. The frozen
    // public Parameter type shape has nowhere to represent those facts, so a
    // Map descriptor remains fail-closed instead of projecting a partial type.
    case EPropertyBagContainerType::Map: return FString();
#endif
    default: return FString();
    }
}

bool IsValidTypeObjectForDescriptor(
    const EPropertyBagPropertyType Type,
    const UObject* TypeObject)
{
    switch (Type)
    {
    case EPropertyBagPropertyType::Enum:
        return IsLoadedGraphObject(Cast<UEnum>(TypeObject));
    case EPropertyBagPropertyType::Struct:
        return IsLoadedGraphObject(Cast<UScriptStruct>(TypeObject));
    case EPropertyBagPropertyType::Object:
    case EPropertyBagPropertyType::SoftObject:
    case EPropertyBagPropertyType::Class:
    case EPropertyBagPropertyType::SoftClass:
        return IsLoadedGraphObject(Cast<UClass>(TypeObject));
    default:
        return TypeObject == nullptr;
    }
}

bool SameResolvedTypeObject(
    const TObjectPtr<const UObject>& Left,
    const TObjectPtr<const UObject>& Right)
{
    if (!Left.IsResolved() || !Right.IsResolved())
    {
        return false;
    }
    const UObject* LeftObject = Left.Get();
    const UObject* RightObject = Right.Get();
    return LeftObject == RightObject
        && (LeftObject == nullptr || IsLoadedGraphObject(LeftObject));
}

bool SameNativeDescriptorShape(
    const FPropertyBagPropertyDesc& Left,
    const FPropertyBagPropertyDesc& Right)
{
    if (Left.ValueType != Right.ValueType
        || Left.ContainerTypes != Right.ContainerTypes
        || !SameResolvedTypeObject(
            Left.ValueTypeObject,
            Right.ValueTypeObject))
    {
        return false;
    }
#if UE_VERSION_NEWER_THAN_OR_EQUAL(5, 8, 0)
    return Left.KeyType == Right.KeyType
        && SameResolvedTypeObject(
            Left.KeyTypeObject,
            Right.KeyTypeObject);
#else
    return true;
#endif
}

bool IsValidCachedProperty(
    const UPropertyBag* Struct,
    const FPropertyBagPropertyDesc& Desc)
{
    if (!IsLoadedGraphObject(Struct)
        || Desc.CachedProperty == nullptr
        || Desc.CachedProperty->GetOwnerStruct() != Struct
        || Desc.CachedProperty->GetFName() != Desc.Name
        || Struct->FindPropertyByName(Desc.Name) != Desc.CachedProperty)
    {
        return false;
    }
    const FPropertyBagPropertyDesc NativeShape(
        Desc.Name,
        Desc.CachedProperty);
    return SameNativeDescriptorShape(Desc, NativeShape);
}

bool ReadTypeObjectPath(
    const FPropertyBagPropertyDesc& Desc,
    FString& OutPath,
    bool& bOutPresent,
    int32& InOutChars)
{
    OutPath.Reset();
    bOutPresent = false;
    if (!Desc.ValueTypeObject.IsResolved())
    {
        return false;
    }
    const UObject* TypeObject = Desc.ValueTypeObject.Get();
    if (!IsValidTypeObjectForDescriptor(Desc.ValueType, TypeObject))
    {
        return false;
    }
    if (TypeObject == nullptr)
    {
        return true;
    }
    OutPath = TypeObject->GetPathName();
    const FString NativeType = TypeObject->GetClass()->GetPathName();
    if (OutPath.IsEmpty()
        || !ConsumeParameterText(OutPath, InOutChars)
        || !ConsumeParameterText(NativeType, InOutChars))
    {
        return false;
    }
    bOutPresent = true;
    return true;
}

struct FBagIndex
{
    const FInstancedPropertyBag* Bag = nullptr;
    TMap<FGuid, const FPropertyBagPropertyDesc*> ById;
    TMap<FGuid, FString> TypeObjectPaths;
    TSet<FGuid> TypeObjectIds;
};

bool BuildBagIndex(
    const FInstancedPropertyBag* Bag,
    int32& InOutChars,
    FBagIndex& Out)
{
    Out = FBagIndex();
    Out.Bag = Bag;
    if (Bag == nullptr)
    {
        return false;
    }
    const int32 Num = Bag->GetNumPropertiesInBag();
    if (Num < 0 || Num > MaxParameterCount)
    {
        return false;
    }
    const UPropertyBag* Struct = Bag->GetPropertyBagStruct();
    if (Num == 0)
    {
        return Bag->IsValid()
            && IsLoadedGraphObject(Struct)
            && Struct->GetPropertyDescs().Num() == 0;
    }
    if (!Bag->IsValid() || !IsLoadedGraphObject(Struct))
    {
        return false;
    }
    const TConstArrayView<FPropertyBagPropertyDesc> Descs =
        Struct->GetPropertyDescs();
    if (Descs.Num() != Num || Descs.Num() > MaxParameterCount)
    {
        return false;
    }
    TSet<FName> Names;
    for (const FPropertyBagPropertyDesc& Desc : Descs)
    {
        const FString Type = ValueTypeText(Desc.ValueType);
        if (!Desc.ID.IsValid()
            || Desc.Name.IsNone()
            || Type.IsEmpty()
            || !Bag->OwnsPropertyDesc(Desc)
            || !IsValidCachedProperty(Struct, Desc)
            || Names.Contains(Desc.Name)
            || Out.ById.Contains(Desc.ID)
            || !ConsumeParameterText(GuidText(Desc.ID), InOutChars)
            || !ConsumeParameterText(Desc.Name.ToString(), InOutChars)
            || !ConsumeParameterText(Type, InOutChars))
        {
            return false;
        }
        for (const EPropertyBagContainerType Container : Desc.ContainerTypes)
        {
            const FString ContainerText = ContainerTypeText(Container);
            if (ContainerText.IsEmpty()
                || !ConsumeParameterText(ContainerText, InOutChars))
            {
                return false;
            }
        }
        FString TypeObjectPath;
        bool bHasTypeObject = false;
        if (!ReadTypeObjectPath(
                Desc,
                TypeObjectPath,
                bHasTypeObject,
                InOutChars))
        {
            return false;
        }
        Names.Add(Desc.Name);
        Out.ById.Add(Desc.ID, &Desc);
        if (bHasTypeObject)
        {
            Out.TypeObjectIds.Add(Desc.ID);
            Out.TypeObjectPaths.Add(Desc.ID, MoveTemp(TypeObjectPath));
        }
    }
    return true;
}

class FParameterFingerprintBuilder
{
public:
    void Add(const FString& Value)
    {
        const FString Length = LexToString(Value.Len());
        Hash.UpdateWithString(*Length, Length.Len());
        Hash.UpdateWithString(TEXT(":"), 1);
        Hash.UpdateWithString(*Value, Value.Len());
        Hash.UpdateWithString(TEXT(";"), 1);
    }

    FString Finalize()
    {
        Hash.Final();
        uint8 Digest[FSHA1::DigestSize];
        Hash.GetHash(Digest);
        return BytesToHex(Digest, UE_ARRAY_COUNT(Digest)).ToLower();
    }

private:
    FSHA1 Hash;
};

FString ParameterCursorFingerprint(
    const FSalQuery& Query,
    const FSalResolvedTarget& Target,
    const FParameterSnapshot& Snapshot,
    const int32 Limit)
{
    FString SearchText;
    Query.Operation->TryGetStringField(TEXT("text"), SearchText);
    FParameterFingerprintBuilder Fingerprint;
    Fingerprint.Add(TEXT("pcg_component_parameter1"));
    Fingerprint.Add(Target.AssetPath);
    Fingerprint.Add(CanonicalField(Target, TEXT("actorId")));
    Fingerprint.Add(CanonicalField(Target, TEXT("source")));
    Fingerprint.Add(CanonicalField(Target, TEXT("id")));
    Fingerprint.Add(CanonicalField(Target, TEXT("type")));
    Fingerprint.Add(SearchText);
    Fingerprint.Add(LexToString(Limit));
    Fingerprint.Add(Snapshot.bComplete ? TEXT("complete") : TEXT("incomplete"));
    Fingerprint.Add(LexToString(Snapshot.BindingEvidence.Num()));
    for (const FString& Evidence : Snapshot.BindingEvidence)
    {
        Fingerprint.Add(Evidence);
    }
    for (const FParameterEntry& Entry : Snapshot.Entries)
    {
        Fingerprint.Add(Entry.IdText);
        Fingerprint.Add(Entry.Name);
        Fingerprint.Add(Entry.ValueType);
        Fingerprint.Add(LexToString(Entry.ContainerTypes.Num()));
        for (const FString& Container : Entry.ContainerTypes)
        {
            Fingerprint.Add(Container);
        }
        Fingerprint.Add(
            Entry.bHasValueTypeObject
                ? Entry.ValueTypeObject
                : TEXT("<no_type_object>"));
        Fingerprint.Add(
            Entry.bValueAvailable ? TEXT("available") : TEXT("unsupported"));
        Fingerprint.Add(Entry.bOverridden ? TEXT("overridden") : TEXT("inherited"));
        Fingerprint.Add(Entry.EffectiveSource);
        Fingerprint.Add(
            Entry.bOverridden && Entry.bValueAvailable
                ? Entry.LocalFingerprint
                : TEXT("<no_local_value>"));
        Fingerprint.Add(
            Entry.bValueAvailable
                ? Entry.EffectiveFingerprint
                : TEXT("<unsupported_value>"));
    }
    return Fingerprint.Finalize();
}

bool ParseNonNegativeInt32(const FString& Text, int32& Out)
{
    int64 Parsed = 0;
    if (!LexTryParseString(Parsed, *Text)
        || Parsed < 0
        || Parsed > MAX_int32)
    {
        return false;
    }
    Out = static_cast<int32>(Parsed);
    return true;
}

bool DecodeParameterPage(
    const FSalQuery& Query,
    const FSalResolvedTarget& Target,
    const FParameterSnapshot& Snapshot,
    FSalPage& OutPage,
    FString& OutFingerprint)
{
    OutPage.Offset = 0;
    OutPage.Limit = FMath::Clamp(
        Query.PageLimit > 0 ? Query.PageLimit : DefaultCollectionLimit,
        1,
        MaxCollectionLimit);
    OutFingerprint = ParameterCursorFingerprint(
        Query,
        Target,
        Snapshot,
        OutPage.Limit);
    if (Query.PageAfter.IsEmpty())
    {
        return true;
    }
    TArray<FString> Parts;
    Query.PageAfter.ParseIntoArray(Parts, TEXT(":"), false);
    return Parts.Num() == 3
        && Parts[0] == TEXT("pcg_component_parameter1")
        && Parts[1].Equals(OutFingerprint, ESearchCase::IgnoreCase)
        && ParseNonNegativeInt32(Parts[2], OutPage.Offset);
}

void SetParameterPage(
    const TSharedPtr<FJsonObject>& Result,
    const FString& Fingerprint,
    const int32 NextOffset,
    const bool bHasNext)
{
    if (!Result.IsValid() || !bHasNext)
    {
        return;
    }
    TSharedPtr<FJsonObject> Page = MakeShared<FJsonObject>();
    Page->SetStringField(
        TEXT("next"),
        TEXT("pcg_component_parameter1:")
            + Fingerprint
            + TEXT(":")
            + LexToString(NextOffset));
    Result->SetObjectField(TEXT("page"), Page);
}

bool ParameterMatchesText(
    const FParameterEntry& Entry,
    const FString& SearchText)
{
    if (SearchText.IsEmpty())
    {
        return true;
    }
    if (Entry.IdText.Contains(SearchText, ESearchCase::IgnoreCase)
        || Entry.Name.Contains(SearchText, ESearchCase::IgnoreCase)
        || Entry.ValueType.Contains(SearchText, ESearchCase::IgnoreCase)
        || Entry.ValueTypeObject.Contains(SearchText, ESearchCase::IgnoreCase)
        || Entry.EffectiveSource.Contains(SearchText, ESearchCase::IgnoreCase))
    {
        return true;
    }
    for (const FString& Container : Entry.ContainerTypes)
    {
        if (Container.Contains(SearchText, ESearchCase::IgnoreCase))
        {
            return true;
        }
    }
    return false;
}

FString UniqueParameterMember(
    const FParameterEntry& Entry,
    TSet<FString>& Used)
{
    const FString Base = FSalObjectBuilder::SanitizeIdentifier(
        Entry.Name,
        TEXT("parameter"));
    FString Candidate = Base;
    for (int32 Suffix = 2; Used.Contains(Candidate); ++Suffix)
    {
        Candidate = Base + TEXT("_") + LexToString(Suffix);
    }
    Used.Add(Candidate);
    return Candidate;
}

bool SameDescriptorShape(
    const FPropertyBagPropertyDesc& Expected,
    const FPropertyBagPropertyDesc& Actual)
{
    if (Expected.ID != Actual.ID
        || Expected.Name != Actual.Name
        || !SameNativeDescriptorShape(Expected, Actual))
    {
        return false;
    }
    return true;
}

bool IsCertifiedScalar(const FPropertyBagPropertyDesc& Desc)
{
    if (!Desc.ContainerTypes.IsEmpty())
    {
        return false;
    }
    switch (Desc.ValueType)
    {
    case EPropertyBagPropertyType::Bool:
    case EPropertyBagPropertyType::Byte:
    case EPropertyBagPropertyType::Int32:
    case EPropertyBagPropertyType::UInt32:
    case EPropertyBagPropertyType::Int64:
    case EPropertyBagPropertyType::UInt64:
    case EPropertyBagPropertyType::Float:
    case EPropertyBagPropertyType::Double:
    case EPropertyBagPropertyType::Name:
    case EPropertyBagPropertyType::String:
    case EPropertyBagPropertyType::Enum:
        return true;
    default:
        return false;
    }
}

FString FloatingText(const double Value)
{
    if (FMath::IsNaN(Value))
    {
        return TEXT("nan");
    }
    if (!FMath::IsFinite(Value))
    {
        return Value < 0.0 ? TEXT("-infinity") : TEXT("+infinity");
    }
    return LexToString(Value);
}

template <typename T>
FString FloatingFingerprint(const T Value, const TCHAR* Prefix)
{
    return FString(Prefix)
        + BytesToHex(
            reinterpret_cast<const uint8*>(&Value),
            sizeof(Value));
}

bool ConsumeValueString(
    const FString& Value,
    int32& InOutChars,
    bool& bOutTooLarge)
{
    if (Value.Len() > MaxParameterValueChars
        || InOutChars > MaxParameterEvidenceChars - Value.Len())
    {
        bOutTooLarge = true;
        return false;
    }
    InOutChars += Value.Len();
    return true;
}

bool ReadBoundedStoredString(
    const FInstancedPropertyBag& Bag,
    const FPropertyBagPropertyDesc& Desc,
    int32& InOutChars,
    bool& bOutTooLarge,
    FString& OutValue)
{
    OutValue.Reset();
    const FStrProperty* StringProperty =
        CastField<FStrProperty>(Desc.CachedProperty);
    const FConstStructView Storage = Bag.GetValue();
    if (StringProperty == nullptr
        || Storage.GetScriptStruct() != StringProperty->GetOwnerStruct()
        || Storage.GetMemory() == nullptr)
    {
        return false;
    }
    const FString* StoredValue =
        StringProperty->ContainerPtrToValuePtr<FString>(Storage.GetMemory());
    if (StoredValue == nullptr
        || !ConsumeValueString(
            *StoredValue,
            InOutChars,
            bOutTooLarge))
    {
        return false;
    }
    OutValue = *StoredValue;
    return true;
}

bool ReadScalarValue(
    const FInstancedPropertyBag& Bag,
    const FPropertyBagPropertyDesc& Desc,
    int32& InOutValueChars,
    bool& bOutTooLarge,
    TSharedPtr<FJsonValue>& OutValue,
    FString& OutFingerprint)
{
    OutValue.Reset();
    OutFingerprint.Reset();
    switch (Desc.ValueType)
    {
    case EPropertyBagPropertyType::Bool:
    {
        const auto Value = Bag.GetValueBool(Desc);
        if (!Value.IsValid()) return false;
        OutValue = Loomle::Sal::Value::Bool(Value.GetValue());
        OutFingerprint = Value.GetValue() ? TEXT("true") : TEXT("false");
        return true;
    }
    case EPropertyBagPropertyType::Byte:
    {
        const auto Value = Bag.GetValueByte(Desc);
        if (!Value.IsValid()) return false;
        OutValue = Loomle::Sal::Value::Number(Value.GetValue());
        OutFingerprint = LexToString(Value.GetValue());
        return true;
    }
    case EPropertyBagPropertyType::Int32:
    {
        const auto Value = Bag.GetValueInt32(Desc);
        if (!Value.IsValid()) return false;
        OutValue = Loomle::Sal::Value::Number(Value.GetValue());
        OutFingerprint = LexToString(Value.GetValue());
        return true;
    }
    case EPropertyBagPropertyType::UInt32:
    {
        const auto Value = Bag.GetValueUInt32(Desc);
        if (!Value.IsValid()) return false;
        OutValue = Loomle::Sal::Value::Number(Value.GetValue());
        OutFingerprint = LexToString(Value.GetValue());
        return true;
    }
    case EPropertyBagPropertyType::Int64:
    {
        const auto Value = Bag.GetValueInt64(Desc);
        if (!Value.IsValid()) return false;
        OutFingerprint = LexToString(Value.GetValue());
        OutValue = Loomle::Sal::Value::String(OutFingerprint);
        return true;
    }
    case EPropertyBagPropertyType::UInt64:
    {
        const auto Value = Bag.GetValueUInt64(Desc);
        if (!Value.IsValid()) return false;
        OutFingerprint = LexToString(Value.GetValue());
        OutValue = Loomle::Sal::Value::String(OutFingerprint);
        return true;
    }
    case EPropertyBagPropertyType::Float:
    {
        const auto Value = Bag.GetValueFloat(Desc);
        if (!Value.IsValid()) return false;
        const float NativeNumber = Value.GetValue();
        const double Number = NativeNumber;
        OutFingerprint = FloatingFingerprint(NativeNumber, TEXT("float:"));
        OutValue = FMath::IsFinite(Number)
            ? Loomle::Sal::Value::Number(Number)
            : Loomle::Sal::Value::String(FloatingText(Number));
        return true;
    }
    case EPropertyBagPropertyType::Double:
    {
        const auto Value = Bag.GetValueDouble(Desc);
        if (!Value.IsValid()) return false;
        const double Number = Value.GetValue();
        OutFingerprint = FloatingFingerprint(Number, TEXT("double:"));
        OutValue = FMath::IsFinite(Number)
            ? Loomle::Sal::Value::Number(Number)
            : Loomle::Sal::Value::String(FloatingText(Number));
        return true;
    }
    case EPropertyBagPropertyType::Name:
    {
        const auto Value = Bag.GetValueName(Desc);
        if (!Value.IsValid()) return false;
        OutFingerprint = Value.GetValue().ToString();
        if (!ConsumeValueString(
                OutFingerprint,
                InOutValueChars,
                bOutTooLarge)) return false;
        OutValue = Loomle::Sal::Value::String(OutFingerprint);
        return true;
    }
    case EPropertyBagPropertyType::String:
    {
        if (!ReadBoundedStoredString(
                Bag,
                Desc,
                InOutValueChars,
                bOutTooLarge,
                OutFingerprint)) return false;
        OutValue = Loomle::Sal::Value::String(OutFingerprint);
        return true;
    }
    case EPropertyBagPropertyType::Enum:
    {
        if (!Desc.ValueTypeObject.IsResolved()) return false;
        const UEnum* Enum = Cast<UEnum>(Desc.ValueTypeObject.Get());
        if (!IsLoadedGraphObject(Enum)) return false;
#if UE_VERSION_NEWER_THAN_OR_EQUAL(5, 8, 0)
        const auto Value = Bag.GetValueEnumInt64(Desc, Enum);
        if (!Value.IsValid()) return false;
        const int64 Number = Value.GetValue();
#else
        const auto Value = Bag.GetValueEnum(Desc, Enum);
        if (!Value.IsValid()) return false;
        const int64 Number = static_cast<int64>(Value.GetValue());
#endif
        const FString Name = Enum->IsValidEnumValue(Number)
            ? Enum->GetNameStringByValue(Number)
            : FString();
        if (!ConsumeValueString(Name, InOutValueChars, bOutTooLarge))
        {
            return false;
        }
        TSharedPtr<FJsonObject> Fields = MakeShared<FJsonObject>();
        Fields->SetStringField(TEXT("type"), Enum->GetPathName());
        Fields->SetField(
            TEXT("name"),
            Name.IsEmpty()
                ? Loomle::Sal::Value::Null()
                : Loomle::Sal::Value::String(Name));
        Fields->SetStringField(TEXT("value"), LexToString(Number));
        OutValue = Loomle::Sal::Value::Call(TEXT("object"), Fields);
        OutFingerprint = Enum->GetPathName()
            + TEXT("|") + Name + TEXT("|") + LexToString(Number);
        return true;
    }
    default:
        return false;
    }
}

FParameterSnapshot BuildParameterSnapshot(
    UPCGComponent* Component,
    const FString& Operation)
{
    FParameterSnapshot Out;
    const FGraphBindingSnapshot Binding = ReadGraphBinding(Component, Operation);
    if (!Binding.bComplete)
    {
        Out.bComplete = false;
        Out.Message = TEXT("The PCG GraphInterface chain is incomplete; Parameter identity and effective values are fail-closed.");
        return Out;
    }
    if (Binding.Direct == nullptr)
    {
        return Out;
    }
    if (Binding.TopGraph == nullptr || Binding.Instances.IsEmpty())
    {
        Out.bComplete = false;
        Out.Message = TEXT("The bound PCG GraphInterface chain has no terminal top Graph; Parameter declarations are unavailable.");
        return Out;
    }

    int32 EvidenceChars = 0;
    const auto AddBindingObjectEvidence =
        [&Out, &EvidenceChars](const UObject* Object)
        {
            if (!IsLoadedGraphObject(Object))
            {
                return false;
            }
            const FString Path = Object->GetPathName();
            const FString Type = Object->GetClass()->GetPathName();
            if (!ConsumeParameterText(Path, EvidenceChars)
                || !ConsumeParameterText(Type, EvidenceChars))
            {
                return false;
            }
            Out.BindingEvidence.Add(Path);
            Out.BindingEvidence.Add(Type);
            return true;
        };
    if (!ConsumeParameterText(Binding.Kind, EvidenceChars))
    {
        Out.bComplete = false;
        Out.Message = TEXT("The PCG GraphInterface binding kind exceeds the shared Parameter evidence budget.");
        return Out;
    }
    Out.BindingEvidence.Add(Binding.Kind);
    for (UPCGGraphInstance* Instance : Binding.Instances)
    {
        if (!AddBindingObjectEvidence(Instance))
        {
            Out.bComplete = false;
            Out.Message = TEXT("The PCG GraphInterface chain identity is invalid, incomplete, or exceeds the shared Parameter evidence budget.");
            return Out;
        }
    }
    if (!AddBindingObjectEvidence(Binding.TopGraph))
    {
        Out.bComplete = false;
        Out.Message = TEXT("The terminal PCG Graph identity is invalid, incomplete, or exceeds the shared Parameter evidence budget.");
        return Out;
    }

    FBagIndex GraphIndex;
    if (!BuildBagIndex(
            Binding.TopGraph->GetUserParametersStruct(),
            EvidenceChars,
            GraphIndex))
    {
        Out.bComplete = false;
        Out.Message = TEXT("The top Graph Parameter descriptor environment is invalid, ambiguous, incomplete, or over budget.");
        return Out;
    }
    TArray<FBagIndex> InstanceIndexes;
    InstanceIndexes.Reserve(Binding.Instances.Num());
    for (UPCGGraphInstance* Instance : Binding.Instances)
    {
        FBagIndex Index;
        if (!IsLoadedGraphObject(Instance)
            || !BuildBagIndex(
                Instance->GetUserParametersStruct(),
                EvidenceChars,
                Index)
            || Index.ById.Num() != GraphIndex.ById.Num())
        {
            Out.bComplete = false;
            Out.Message = TEXT("A GraphInstance Parameter bag is invalid, ambiguous, incomplete, mismatched, or over budget.");
            return Out;
        }
        for (const TPair<FGuid, const FPropertyBagPropertyDesc*>& Pair :
             GraphIndex.ById)
        {
            const FPropertyBagPropertyDesc* const* Found =
                Index.ById.Find(Pair.Key);
            if (Found == nullptr
                || *Found == nullptr
                || !SameDescriptorShape(*Pair.Value, **Found))
            {
                Out.bComplete = false;
                Out.Message = TEXT("A GraphInstance Parameter descriptor does not exactly align with its top Graph descriptor Guid and native shape.");
                return Out;
            }
        }
        for (const FGuid& OverrideId :
             Instance->ParametersOverrides.PropertiesIDsOverridden)
        {
            if (!Index.ById.Contains(OverrideId))
            {
                Out.bComplete = false;
                Out.Message = TEXT("A GraphInstance contains an override bit for no aligned Parameter descriptor.");
                return Out;
            }
        }
        InstanceIndexes.Add(MoveTemp(Index));
    }
    if (GraphIndex.ById.IsEmpty())
    {
        return Out;
    }

    TArray<const FPropertyBagPropertyDesc*> Ordered;
    GraphIndex.ById.GenerateValueArray(Ordered);
    Ordered.Sort([](
        const FPropertyBagPropertyDesc& Left,
        const FPropertyBagPropertyDesc& Right)
    {
        return GuidText(Left.ID) < GuidText(Right.ID);
    });

    int32 ValueChars = 0;
    Out.Entries.Reserve(Ordered.Num());
    for (const FPropertyBagPropertyDesc* TopDesc : Ordered)
    {
        if (TopDesc == nullptr)
        {
            Out.bComplete = false;
            Out.Message = TEXT("The top Graph Parameter descriptor index contains an invalid entry.");
            return Out;
        }
        FParameterEntry Entry;
        Entry.Id = TopDesc->ID;
        Entry.IdText = GuidText(TopDesc->ID);
        Entry.Name = TopDesc->Name.ToString();
        Entry.ValueType = ValueTypeText(TopDesc->ValueType);
        for (const EPropertyBagContainerType Container : TopDesc->ContainerTypes)
        {
            Entry.ContainerTypes.Add(ContainerTypeText(Container));
        }
        Entry.bHasValueTypeObject =
            GraphIndex.TypeObjectIds.Contains(TopDesc->ID);
        Entry.ValueTypeObject = Entry.bHasValueTypeObject
            ? GraphIndex.TypeObjectPaths.FindRef(TopDesc->ID)
            : FString();

        const FPropertyBagPropertyDesc* OwnedDesc =
            InstanceIndexes[0].ById.FindRef(TopDesc->ID);
        Entry.bOverridden = Binding.Instances[0]->IsPropertyOverridden(
            OwnedDesc != nullptr ? OwnedDesc->CachedProperty : nullptr);
        const FInstancedPropertyBag* EffectiveBag = GraphIndex.Bag;
        const FPropertyBagPropertyDesc* EffectiveDesc = TopDesc;
        Entry.EffectiveSource = TEXT("graph_default");
        if (Entry.bOverridden)
        {
            EffectiveBag = InstanceIndexes[0].Bag;
            EffectiveDesc = OwnedDesc;
            Entry.EffectiveSource = TEXT("component_override");
        }
        else
        {
            for (int32 Index = 1; Index < InstanceIndexes.Num(); ++Index)
            {
                const FPropertyBagPropertyDesc* ParentDesc =
                    InstanceIndexes[Index].ById.FindRef(TopDesc->ID);
                if (Binding.Instances[Index]->IsPropertyOverridden(
                        ParentDesc != nullptr
                            ? ParentDesc->CachedProperty
                            : nullptr))
                {
                    EffectiveBag = InstanceIndexes[Index].Bag;
                    EffectiveDesc = ParentDesc;
                    Entry.EffectiveSource = TEXT("parent_instance");
                    break;
                }
            }
        }

        Entry.bValueAvailable = IsCertifiedScalar(*TopDesc);
        if (Entry.bValueAvailable)
        {
            bool bTooLarge = false;
            if (EffectiveBag == nullptr
                || EffectiveDesc == nullptr
                || !ReadScalarValue(
                    *EffectiveBag,
                    *EffectiveDesc,
                    ValueChars,
                    bTooLarge,
                    Entry.EffectiveValue,
                    Entry.EffectiveFingerprint))
            {
                Out.bComplete = false;
                Out.bResultTooLarge = bTooLarge;
                Out.Message = bTooLarge
                    ? TEXT("The complete certified Parameter string-value evidence exceeds its hard size limit; no value was truncated.")
                    : TEXT("A certified scalar Parameter value could not be read losslessly from its aligned Property Bag descriptor.");
                return Out;
            }
            if (Entry.bOverridden)
            {
                Entry.LocalValue = Entry.EffectiveValue;
                Entry.LocalFingerprint = Entry.EffectiveFingerprint;
            }
        }
        else
        {
            Entry.EffectiveValue = Loomle::Sal::Value::Null();
        }
        Out.Entries.Add(MoveTemp(Entry));
    }
    return Out;
}

TSharedPtr<FJsonObject> ComponentFields(
    const FSalResolvedTarget& Target,
    const UPCGComponent* Component)
{
    const FString ActorId = CanonicalField(Target, TEXT("actorId"));
    const FString Source = CanonicalField(Target, TEXT("source"));
    const FString Id = CanonicalField(Target, TEXT("id"));
    TSharedPtr<FJsonObject> Fields = MakeShared<FJsonObject>();
    Fields->SetStringField(TEXT("asset"), Target.AssetPath);
    Fields->SetStringField(TEXT("actorId"), ActorId);
    Fields->SetField(TEXT("source"), Value::Name(Source));
    Fields->SetStringField(TEXT("id"), Id);
    Fields->SetStringField(
        TEXT("name"),
        Component != nullptr ? Component->GetFName().ToString() : FString());
    Fields->SetStringField(
        TEXT("type"),
        CanonicalField(Target, TEXT("type")));
    Fields->SetField(
        TEXT("CreationMethod"),
        Value::Name(CreationMethodText(Component)));
    const FString DeclaringClass = DeclaringClassFromSCSId(Source, Id);
    if (!DeclaringClass.IsEmpty())
    {
        Fields->SetStringField(TEXT("declaringClass"), DeclaringClass);
    }
    Fields->SetBoolField(TEXT("loaded"), true);
    return Fields;
}

TSharedPtr<FJsonValue> ParameterTypeValue(const FParameterEntry& Entry)
{
    TSharedPtr<FJsonObject> Fields = MakeShared<FJsonObject>();
    Fields->SetField(
        TEXT("valueType"),
        Value::NameOrString(Entry.ValueType));
    TArray<TSharedPtr<FJsonValue>> Containers;
    Containers.Reserve(Entry.ContainerTypes.Num());
    for (const FString& Container : Entry.ContainerTypes)
    {
        Containers.Add(Value::NameOrString(Container));
    }
    Fields->SetArrayField(TEXT("containerTypes"), Containers);
    Fields->SetField(
        TEXT("valueTypeObject"),
        Entry.bHasValueTypeObject
            ? Value::String(Entry.ValueTypeObject)
            : Value::Null());
    return Value::Call(TEXT("object"), Fields);
}

TSharedPtr<FJsonValue> ParameterValue(const FParameterEntry& Entry)
{
    TSharedPtr<FJsonObject> Fields = MakeShared<FJsonObject>();
    Fields->SetStringField(TEXT("id"), Entry.IdText);
    Fields->SetStringField(TEXT("name"), Entry.Name);
    Fields->SetField(TEXT("type"), ParameterTypeValue(Entry));
    Fields->SetField(
        TEXT("valueStatus"),
        Value::Name(Entry.bValueAvailable
            ? TEXT("available")
            : TEXT("unsupported")));
    Fields->SetBoolField(TEXT("overridden"), Entry.bOverridden);
    if (Entry.bOverridden
        && Entry.bValueAvailable
        && Entry.LocalValue.IsValid())
    {
        Fields->SetField(TEXT("localValue"), Entry.LocalValue);
    }
    Fields->SetField(
        TEXT("effectiveValue"),
        Entry.EffectiveValue.IsValid()
            ? Entry.EffectiveValue
            : Value::Null());
    Fields->SetField(
        TEXT("effectiveSource"),
        Value::Name(Entry.EffectiveSource));
    Fields->SetBoolField(TEXT("stableRefAvailable"), true);
    Fields->SetField(
        TEXT("ref"),
        Value::Stable(
            TEXT("parameter"),
            TArray<FString>{Entry.IdText}));
    return Value::Call(TEXT("parameter"), Fields);
}

TSharedPtr<FJsonObject> ParameterSnapshotError(
    const FParameterSnapshot& Snapshot,
    const FString& Operation)
{
    return QueryError(
        Snapshot.bResultTooLarge
            ? TEXT("validation.result_too_large")
            : TEXT("validation.reference_scan_incomplete"),
        Snapshot.Message.IsEmpty()
            ? TEXT("The PCG Parameter snapshot is incomplete.")
            : Snapshot.Message,
        Operation);
}

TSharedPtr<FJsonObject> BuildParameterResult(
    const FSalQuery& Query,
    const FSalResolvedTarget& Target,
    UPCGComponent* Component,
    const TArray<const FParameterEntry*>& Entries,
    const FString& Comment = FString())
{
    FSalObjectBuilder Builder;
    const FString ComponentAlias = Builder.UniqueAlias(
        Query.Alias.IsEmpty()
            ? (Target.Name.IsEmpty()
                ? TEXT("pcg_component")
                : Target.Name)
            : Query.Alias);
    Builder.AddLocalBinding(
        ComponentAlias,
        Value::Call(TEXT("component"), ComponentFields(Target, Component)));
    TSet<FString> UsedMembers;
    for (const FParameterEntry* Entry : Entries)
    {
        if (Entry == nullptr)
        {
            continue;
        }
        Builder.AddMemberBinding(
            ComponentAlias,
            {TEXT("Parameters"), UniqueParameterMember(*Entry, UsedMembers)},
            ParameterValue(*Entry));
    }
    if (!Comment.IsEmpty())
    {
        Builder.AddComment(Comment);
    }
    TSharedPtr<FJsonObject> Result = Builder.BuildResult();
    ResultTargets::AddHandoff(
        Result,
        LevelTargetValue(Target),
        TEXT("owning_level"),
        TEXT("inspect_level"));
    return Result;
}

TSharedPtr<FJsonObject> QueryParameters(
    const FSalQuery& Query,
    const FSalResolvedTarget& Target,
    UPCGComponent* Component)
{
    if (!Query.With.IsEmpty()
        || Query.Where.IsValid()
        || !Query.OrderBy.IsEmpty())
    {
        return QueryError(
            TEXT("capability.clause_unavailable"),
            TEXT("pcg_component parameters accepts only optional text search and cursor page clauses."),
            TEXT("parameters"));
    }
    const FParameterSnapshot Snapshot = BuildParameterSnapshot(
        Component,
        TEXT("parameters"));
    if (!Snapshot.bComplete)
    {
        return ParameterSnapshotError(Snapshot, TEXT("parameters"));
    }
    FSalPage Page;
    FString Fingerprint;
    if (!DecodeParameterPage(
            Query,
            Target,
            Snapshot,
            Page,
            Fingerprint))
    {
        return QueryError(
            TEXT("validation.invalid_cursor"),
            TEXT("PCG Component Parameter cursor does not belong to this exact Target, Parameter snapshot, search, or page limit. Re-run the first page."),
            TEXT("parameters"),
            Query.PageAfter);
    }
    FString SearchText;
    Query.Operation->TryGetStringField(TEXT("text"), SearchText);
    TArray<const FParameterEntry*> Matches;
    Matches.Reserve(Snapshot.Entries.Num());
    for (const FParameterEntry& Entry : Snapshot.Entries)
    {
        if (ParameterMatchesText(Entry, SearchText))
        {
            Matches.Add(&Entry);
        }
    }
    if (Page.Offset > Matches.Num())
    {
        return QueryError(
            TEXT("validation.invalid_cursor"),
            TEXT("PCG Component Parameter cursor offset is outside the current result set. Re-run the first page."),
            TEXT("parameters"),
            Query.PageAfter);
    }
    const int32 End = static_cast<int32>(FMath::Min<int64>(
        Matches.Num(),
        static_cast<int64>(Page.Offset) + Page.Limit));
    TArray<const FParameterEntry*> PageEntries;
    PageEntries.Reserve(End - Page.Offset);
    for (int32 Index = Page.Offset; Index < End; ++Index)
    {
        PageEntries.Add(Matches[Index]);
    }
    TSharedPtr<FJsonObject> Result = BuildParameterResult(
        Query,
        Target,
        Component,
        PageEntries,
        Matches.IsEmpty() ? TEXT("no matches") : FString());
    SetParameterPage(
        Result,
        Fingerprint,
        End,
        End < Matches.Num());
    return Result;
}

TSharedPtr<FJsonObject> QueryExactParameter(
    const FSalQuery& Query,
    const FSalResolvedTarget& Target,
    UPCGComponent* Component)
{
    FString Id;
    FGuid ParsedId;
    if (!Query.Operation->TryGetStringField(TEXT("id"), Id)
        || !ParseCanonicalParameterGuid(Id, ParsedId))
    {
        return QueryError(
            TEXT("validation.invalid_reference"),
            TEXT("Exact PCG Component Parameter identity requires one canonical descriptor Guid."),
            TEXT("parameter"));
    }
    if (HasExactClauses(Query))
    {
        return QueryError(
            TEXT("capability.clause_unavailable"),
            TEXT("Exact PCG Component Parameter read accepts only optional with schema."),
            TEXT("parameter"));
    }
    for (const FString& Detail : Query.With)
    {
        if (Detail != TEXT("schema"))
        {
            return QueryError(
                TEXT("capability.detail_unavailable"),
                TEXT("Exact PCG Component Parameter read supports only with schema."),
                TEXT("parameter"),
                Detail);
        }
    }
    const FParameterSnapshot Snapshot = BuildParameterSnapshot(
        Component,
        TEXT("parameter"));
    if (!Snapshot.bComplete)
    {
        return ParameterSnapshotError(Snapshot, TEXT("parameter"));
    }
    const FParameterEntry* Match = Snapshot.Entries.FindByPredicate(
        [&ParsedId](const FParameterEntry& Entry)
        {
            return Entry.Id == ParsedId;
        });
    if (Match == nullptr)
    {
        return QueryError(
            TEXT("resolution.object_not_found"),
            TEXT("The canonical Parameter descriptor Guid is no longer present in the bound top Graph."),
            TEXT("parameter"),
            Id);
    }
    TSharedPtr<FJsonObject> Result = BuildParameterResult(
        Query,
        Target,
        Component,
        {Match},
        Query.With.Contains(TEXT("schema"))
            ? TEXT("pcg_component Parameter schema (read-only): descriptor Guid identity, certified scalar readback, and no Patch operations")
            : FString());
    return Result;
}

TSharedPtr<FJsonValue> DirectGraphInterfaceValue(
    const UPCGGraphInterface* Interface,
    const FString& Kind)
{
    if (Interface == nullptr)
    {
        return Value::Null();
    }
    TSharedPtr<FJsonObject> Fields = MakeShared<FJsonObject>();
    Fields->SetStringField(TEXT("path"), Interface->GetPathName());
    Fields->SetStringField(
        TEXT("type"),
        Interface->GetClass()->GetPathName());
    // These tokens include the reserved SAL word "graph", so their wire
    // representation is deliberately a string rather than a NameExpr.
    Fields->SetStringField(TEXT("kind"), Kind);
    // graph is a reserved structural word, so this encodes one anonymous
    // ObjectExpr rather than inventing a public semantic tag.
    return Value::Call(TEXT("graph"), Fields);
}

TSharedPtr<FJsonValue> TopGraphValue(const UPCGGraph* Graph)
{
    if (Graph == nullptr)
    {
        return Value::Null();
    }
    TSharedPtr<FJsonObject> Fields = MakeShared<FJsonObject>();
    Fields->SetStringField(TEXT("path"), Graph->GetPathName());
    Fields->SetStringField(
        TEXT("type"),
        Graph->GetClass()->GetPathName());
    return Value::Call(TEXT("graph"), Fields);
}

TSharedPtr<FJsonObject> LevelTargetValue(
    const FSalResolvedTarget& Target)
{
    if (Target.AssetPath.IsEmpty())
    {
        return nullptr;
    }
    TSharedPtr<FJsonObject> LevelTarget = MakeShared<FJsonObject>();
    LevelTarget->SetStringField(TEXT("kind"), TEXT("target"));
    LevelTarget->SetStringField(TEXT("domain"), TEXT("level"));
    LevelTarget->SetStringField(TEXT("asset"), Target.AssetPath);
    LevelTarget->SetStringField(
        TEXT("type"),
        TEXT("/Script/Engine.World"));
    return LevelTarget;
}

TSharedPtr<FJsonObject> BuildComponentResult(
    const FSalQuery& Query,
    const FSalResolvedTarget& Target,
    UPCGComponent* Component,
    const bool bSummary,
    const bool bSchema)
{
    FSalObjectBuilder Builder;
    const FString Alias = Builder.UniqueAlias(
        Query.Alias.IsEmpty()
            ? (Target.Name.IsEmpty() ? TEXT("pcg_component") : Target.Name)
            : Query.Alias);
    TSharedPtr<FJsonObject> Fields = ComponentFields(Target, Component);
    FGraphBindingSnapshot GraphBinding;
    if (bSummary)
    {
        GraphBinding = ReadGraphBinding(Component, TEXT("summary"));
        Fields->SetStringField(
            TEXT("graphBindingKind"),
            GraphBinding.Kind);
        Fields->SetBoolField(
            TEXT("graphBindingComplete"),
            GraphBinding.bComplete);
        Fields->SetField(
            TEXT("graphInterface"),
            DirectGraphInterfaceValue(
                GraphBinding.Direct,
                GraphBinding.Direct != nullptr
                        && GraphBinding.Direct->IsA<UPCGGraphInstance>()
                    ? TEXT("graph_instance")
                    : TEXT("graph")));
        Fields->SetField(
            TEXT("graph"),
            TopGraphValue(GraphBinding.TopGraph));
    }

    Builder.AddLocalBinding(
        Alias,
        Value::Call(TEXT("component"), Fields));
    if (bSchema)
    {
        Builder.AddComment(
            TEXT("pcg_component target schema (read-only)\n")
            TEXT("fields: asset, actorId, source, id, name, type, CreationMethod, declaringClass, loaded\n")
            TEXT("operations: target, summary, parameters [text], exact @parameter-guid\n")
            TEXT("Parameter values are read-only; execution state, generated resources, inspection, schema mutation, and Patch are not active in this slice"));
    }

    TSharedPtr<FJsonObject> Result = Builder.BuildResult(
        GraphBinding.Diagnostics);
    const FString LevelAlias = ResultTargets::AddHandoff(
        Result,
        LevelTargetValue(Target),
        TEXT("owning_level"),
        TEXT("inspect_level"));
    (void)LevelAlias;
    if (bSummary && GraphBinding.PcgTarget.IsValid())
    {
        const FString GraphAlias = ResultTargets::AddHandoff(
            Result,
            GraphBinding.PcgTarget,
            TEXT("graph"),
            TEXT("inspect_graph"));
        (void)GraphAlias;
    }
    return Result;
}

bool HasExactClauses(const FSalQuery& Query)
{
    return Query.Where.IsValid()
        || !Query.OrderBy.IsEmpty()
        || Query.PageLimit > 0
        || !Query.PageAfter.IsEmpty();
}
}

TSharedPtr<FJsonObject> FSalPCGComponentInterface::Query(
    const FSalQuery& Query,
    const FSalResolvedTarget& Target)
{
    UPCGComponent* Component = ResolvedComponent(Target);
    if (Component == nullptr)
    {
        return QueryError(
            TEXT("capability.interface_unavailable"),
            TEXT("pcg_component Query requires one exact loaded original authored UPCGComponent Target."),
            TEXT("query"));
    }
    FString Operation;
    if (!Query.Operation.IsValid()
        || !Query.Operation->TryGetStringField(TEXT("kind"), Operation))
    {
        return QueryError(
            TEXT("capability.operation_unavailable"),
            TEXT("pcg_component Query has no supported primary operation."),
            TEXT("query"),
            FString(),
            {TEXT("target"), TEXT("summary"), TEXT("parameters"), TEXT("parameter")});
    }
    if (Operation == TEXT("target"))
    {
        if (HasExactClauses(Query))
        {
            return QueryError(
                TEXT("capability.clause_unavailable"),
                TEXT("Exact pcg_component target read accepts only optional with schema."),
                Operation);
        }
        for (const FString& Detail : Query.With)
        {
            if (Detail != TEXT("schema"))
            {
                return QueryError(
                    TEXT("capability.detail_unavailable"),
                    TEXT("Exact pcg_component target read supports only with schema."),
                    Operation,
                    Detail);
            }
        }
        return BuildComponentResult(
            Query,
            Target,
            Component,
            false,
            Query.With.Contains(TEXT("schema")));
    }
    if (Operation == TEXT("summary"))
    {
        if (!Query.With.IsEmpty() || HasExactClauses(Query))
        {
            return QueryError(
                TEXT("capability.clause_unavailable"),
                TEXT("pcg_component summary accepts no Query clauses."),
                Operation);
        }
        return BuildComponentResult(
            Query,
            Target,
            Component,
            true,
            false);
    }
    if (Operation == TEXT("parameters"))
    {
        return QueryParameters(Query, Target, Component);
    }
    if (Operation == TEXT("parameter"))
    {
        return QueryExactParameter(Query, Target, Component);
    }
    return QueryError(
        TEXT("capability.operation_unavailable"),
        FString::Printf(
            TEXT("pcg_component Query operation is not active in this read-only slice: %s."),
            *Operation),
        Operation,
        FString(),
        {TEXT("target"), TEXT("summary"), TEXT("parameters"), TEXT("parameter")});
}

bool FSalPCGComponentInterface::LowerStableReference(
    const FSalResolvedTarget& Target,
    const TArray<FString>& IdentityPath,
    const TSharedPtr<FJsonObject>& Ref,
    FString& OutCode,
    FString& OutMessage)
{
    OutCode = TEXT("resolution.object_not_found");
    OutMessage = TEXT("The Parameter descriptor Guid is not present in the bound PCG Component identity environment.");
    UPCGComponent* Component = ResolvedComponent(Target);
    if (Component == nullptr || !Ref.IsValid())
    {
        OutCode = TEXT("capability.interface_unavailable");
        OutMessage = TEXT("PCG Component StableRef resolution requires one exact loaded original authored UPCGComponent Target.");
        return false;
    }
    if (IdentityPath.Num() != 1 || IdentityPath[0].IsEmpty())
    {
        OutCode = TEXT("validation.invalid_reference");
        OutMessage = TEXT("PCG Component StableRef identity must be exactly one canonical non-zero Parameter descriptor Guid.");
        return false;
    }
    FGuid Parsed;
    if (!ParseCanonicalParameterGuid(IdentityPath[0], Parsed))
    {
        OutCode = TEXT("validation.invalid_reference");
        OutMessage = TEXT("PCG Component Parameter StableRef must use the canonical lower-case hyphenated non-zero descriptor Guid.");
        return false;
    }
    const FParameterSnapshot Snapshot = BuildParameterSnapshot(
        Component,
        TEXT("parameter"));
    if (!Snapshot.bComplete)
    {
        OutCode = Snapshot.bResultTooLarge
            ? TEXT("validation.result_too_large")
            : TEXT("validation.reference_scan_incomplete");
        OutMessage = Snapshot.Message.IsEmpty()
            ? TEXT("The PCG Component Parameter identity environment is incomplete.")
            : Snapshot.Message;
        return false;
    }
    const bool bFound = Snapshot.Entries.ContainsByPredicate(
        [&Parsed](const FParameterEntry& Entry)
        {
            return Entry.Id == Parsed;
        });
    if (!bFound)
    {
        return false;
    }
    Ref->Values.Reset();
    Ref->SetStringField(TEXT("kind"), TEXT("parameter"));
    Ref->SetStringField(TEXT("id"), GuidText(Parsed));
    return true;
}
}
