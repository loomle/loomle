// Copyright 2026 Loomle contributors.

#if WITH_DEV_AUTOMATION_TESTS

#include "LoomleTestObjectIteration.h"
#include "Sal/SalModule.h"

#include "AssetRegistry/AssetData.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Components/ActorComponent.h"
#include "Components/SceneComponent.h"
#include "ComponentInstanceDataCache.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Editor.h"
#include "Editor/Transactor.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Engine/Level.h"
#include "Engine/SCS_Node.h"
#include "Engine/Selection.h"
#include "Engine/SimpleConstructionScript.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "HAL/FileManager.h"
#include "Helpers/PCGHelpers.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "LevelInstance/LevelInstanceActor.h"
#include "LevelInstance/LevelInstanceSubsystem.h"
#include "Misc/AutomationTest.h"
#include "Misc/CoreDelegates.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "PCGComponent.h"
#include "UObject/GarbageCollection.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/UObjectHash.h"
#include "UObject/UObjectIterator.h"
#include "UObject/UnrealType.h"

namespace
{
using namespace Loomle::Sal;

FString LevelGuidText(const FGuid& Guid)
{
    return Guid.ToString(EGuidFormats::DigitsWithHyphensLower);
}

TArray<TSharedPtr<FJsonValue>> LevelStringValues(
    const TArray<FString>& Strings)
{
    TArray<TSharedPtr<FJsonValue>> Values;
    Values.Reserve(Strings.Num());
    for (const FString& String : Strings)
    {
        Values.Add(MakeShared<FJsonValueString>(String));
    }
    return Values;
}

TSharedRef<FJsonObject> LevelTarget(
    const FString& Asset,
    const FString& Type = FString())
{
    TSharedRef<FJsonObject> Target = MakeShared<FJsonObject>();
    Target->SetStringField(TEXT("kind"), TEXT("target"));
    Target->SetStringField(TEXT("domain"), TEXT("level"));
    Target->SetStringField(TEXT("asset"), Asset);
    if (!Type.IsEmpty())
    {
        Target->SetStringField(TEXT("type"), Type);
    }
    return Target;
}

TSharedRef<FJsonObject> LevelTargetBinding(
    const TSharedRef<FJsonObject>& Target)
{
    TSharedRef<FJsonObject> Binding = MakeShared<FJsonObject>();
    Binding->SetStringField(TEXT("alias"), TEXT("level_scope"));
    Binding->SetObjectField(TEXT("target"), Target);
    return Binding;
}

TSharedRef<FJsonObject> LevelOperation(
    const FString& Kind,
    const FString& SearchText = FString())
{
    TSharedRef<FJsonObject> Operation = MakeShared<FJsonObject>();
    Operation->SetStringField(TEXT("kind"), Kind);
    if (!SearchText.IsEmpty())
    {
        Operation->SetStringField(TEXT("text"), SearchText);
    }
    return Operation;
}

TSharedRef<FJsonObject> LevelStableRef(const FString& ActorId)
{
    TSharedRef<FJsonObject> Ref = MakeShared<FJsonObject>();
    Ref->SetStringField(TEXT("kind"), TEXT("stable_ref"));
    Ref->SetArrayField(
        TEXT("identityPath"),
        LevelStringValues({ActorId}));
    return Ref;
}

TSharedRef<FJsonObject> LevelComponentStableRef(
    const FString& ActorId,
    const FString& Source,
    const FString& Id)
{
    TSharedRef<FJsonObject> Ref = MakeShared<FJsonObject>();
    Ref->SetStringField(TEXT("kind"), TEXT("stable_ref"));
    Ref->SetArrayField(
        TEXT("identityPath"),
        LevelStringValues({ActorId, Source, Id}));
    return Ref;
}

TSharedRef<FJsonObject> LevelExactOperation(const FString& ActorId)
{
    TSharedRef<FJsonObject> Operation = LevelOperation(TEXT("object"));
    Operation->SetObjectField(TEXT("target"), LevelStableRef(ActorId));
    return Operation;
}

TSharedRef<FJsonObject> LevelExactComponentOperation(
    const FString& ActorId,
    const FString& Source,
    const FString& Id)
{
    TSharedRef<FJsonObject> Operation = LevelOperation(TEXT("object"));
    Operation->SetObjectField(
        TEXT("target"),
        LevelComponentStableRef(ActorId, Source, Id));
    return Operation;
}

TSharedRef<FJsonObject> LevelQueryArguments(
    const TSharedRef<FJsonObject>& Target,
    const TSharedRef<FJsonObject>& Operation,
    const int32 PageLimit = 0,
    const FString& PageAfter = FString())
{
    TSharedRef<FJsonObject> Query = MakeShared<FJsonObject>();
    Query->SetStringField(TEXT("kind"), TEXT("query"));
    Query->SetObjectField(TEXT("target"), LevelTargetBinding(Target));
    Query->SetObjectField(TEXT("operation"), Operation);
    if (PageLimit > 0 || !PageAfter.IsEmpty())
    {
        TSharedRef<FJsonObject> Page = MakeShared<FJsonObject>();
        if (PageLimit > 0)
        {
            Page->SetNumberField(TEXT("limit"), PageLimit);
        }
        if (!PageAfter.IsEmpty())
        {
            Page->SetStringField(TEXT("after"), PageAfter);
        }
        Query->SetObjectField(TEXT("page"), Page);
    }

    TSharedRef<FJsonObject> Arguments = MakeShared<FJsonObject>();
    Arguments->SetObjectField(TEXT("object"), Query);
    return Arguments;
}

bool LevelHasDiagnostic(
    const TSharedPtr<FJsonObject>& Result,
    const FString& ExpectedCode)
{
    const TArray<TSharedPtr<FJsonValue>>* Diagnostics = nullptr;
    if (!Result.IsValid()
        || !Result->TryGetArrayField(TEXT("diagnostics"), Diagnostics)
        || Diagnostics == nullptr)
    {
        return false;
    }
    for (const TSharedPtr<FJsonValue>& Value : *Diagnostics)
    {
        const TSharedPtr<FJsonObject>* Diagnostic = nullptr;
        FString Code;
        if (Value.IsValid()
            && Value->TryGetObject(Diagnostic)
            && Diagnostic != nullptr
            && (*Diagnostic).IsValid()
            && (*Diagnostic)->TryGetStringField(TEXT("code"), Code)
            && Code == ExpectedCode)
        {
            return true;
        }
    }
    return false;
}

bool LevelHasError(const TSharedPtr<FJsonObject>& Result)
{
    const TArray<TSharedPtr<FJsonValue>>* Diagnostics = nullptr;
    if (!Result.IsValid()
        || !Result->TryGetArrayField(TEXT("diagnostics"), Diagnostics)
        || Diagnostics == nullptr)
    {
        return true;
    }
    for (const TSharedPtr<FJsonValue>& Value : *Diagnostics)
    {
        const TSharedPtr<FJsonObject>* Diagnostic = nullptr;
        FString Severity;
        if (Value.IsValid()
            && Value->TryGetObject(Diagnostic)
            && Diagnostic != nullptr
            && (*Diagnostic).IsValid()
            && (*Diagnostic)->TryGetStringField(TEXT("severity"), Severity)
            && Severity == TEXT("error"))
        {
            return true;
        }
    }
    return false;
}

bool LevelHasTargetContext(
    const TSharedPtr<FJsonObject>& Result,
    const FString& Expected)
{
    FString Context;
    return Result.IsValid()
        && Result->TryGetStringField(TEXT("targetContext"), Context)
        && Context == Expected;
}

bool HasCanonicalLevelTarget(
    const TSharedPtr<FJsonObject>& Result,
    const FString& ExpectedAsset)
{
    const TSharedPtr<FJsonObject>* Binding = nullptr;
    const TSharedPtr<FJsonObject>* Target = nullptr;
    FString Domain;
    FString Asset;
    FString Type;
    return Result.IsValid()
        && Result->TryGetObjectField(TEXT("target"), Binding)
        && Binding != nullptr
        && (*Binding).IsValid()
        && (*Binding)->TryGetObjectField(TEXT("target"), Target)
        && Target != nullptr
        && (*Target).IsValid()
        && (*Target)->TryGetStringField(TEXT("domain"), Domain)
        && Domain == TEXT("level")
        && (*Target)->TryGetStringField(TEXT("asset"), Asset)
        && Asset == ExpectedAsset
        && (*Target)->TryGetStringField(TEXT("type"), Type)
        && Type == UWorld::StaticClass()->GetPathName();
}

bool TryReadLevelObjectFields(
    const TSharedPtr<FJsonValue>& Value,
    const TSharedPtr<FJsonObject>*& OutFields,
    FString* OutSemanticTag = nullptr)
{
    OutFields = nullptr;
    const TSharedPtr<FJsonObject>* Object = nullptr;
    FString Kind;
    if (!Value.IsValid()
        || !Value->TryGetObject(Object)
        || Object == nullptr
        || !(*Object).IsValid()
        || !(*Object)->TryGetStringField(TEXT("kind"), Kind)
        || Kind != TEXT("object")
        || !(*Object)->TryGetObjectField(TEXT("fields"), OutFields)
        || OutFields == nullptr)
    {
        return false;
    }
    if (OutSemanticTag != nullptr)
    {
        OutSemanticTag->Reset();
        (*Object)->TryGetStringField(TEXT("semanticTag"), *OutSemanticTag);
    }
    return true;
}

TArray<TSharedPtr<FJsonObject>> LevelTaggedObjectFields(
    const TSharedPtr<FJsonObject>& Result,
    const FString& SemanticTag)
{
    TArray<TSharedPtr<FJsonObject>> Matches;
    const TSharedPtr<FJsonObject>* Object = nullptr;
    const TArray<TSharedPtr<FJsonValue>>* Statements = nullptr;
    if (!Result.IsValid()
        || !Result->TryGetObjectField(TEXT("object"), Object)
        || Object == nullptr
        || !(*Object).IsValid()
        || !(*Object)->TryGetArrayField(TEXT("statements"), Statements)
        || Statements == nullptr)
    {
        return Matches;
    }
    for (const TSharedPtr<FJsonValue>& StatementValue : *Statements)
    {
        const TSharedPtr<FJsonObject>* Statement = nullptr;
        const TSharedPtr<FJsonObject>* Fields = nullptr;
        FString ActualTag;
        if (StatementValue.IsValid()
            && StatementValue->TryGetObject(Statement)
            && Statement != nullptr
            && (*Statement).IsValid()
            && TryReadLevelObjectFields(
                (*Statement)->TryGetField(TEXT("value")),
                Fields,
                &ActualTag)
            && Fields != nullptr
            && ActualTag == SemanticTag)
        {
            Matches.Add(*Fields);
        }
    }
    return Matches;
}

TSharedPtr<FJsonObject> FindLevelFieldsById(
    const TArray<TSharedPtr<FJsonObject>>& Objects,
    const FString& ExpectedId)
{
    for (const TSharedPtr<FJsonObject>& Fields : Objects)
    {
        FString Id;
        if (Fields.IsValid()
            && Fields->TryGetStringField(TEXT("id"), Id)
            && Id == ExpectedId)
        {
            return Fields;
        }
    }
    return nullptr;
}

int32 CountLevelFieldsById(
    const TArray<TSharedPtr<FJsonObject>>& Objects,
    const FString& ExpectedId)
{
    int32 Count = 0;
    for (const TSharedPtr<FJsonObject>& Fields : Objects)
    {
        FString Id;
        if (Fields.IsValid()
            && Fields->TryGetStringField(TEXT("id"), Id)
            && Id == ExpectedId)
        {
            ++Count;
        }
    }
    return Count;
}

TSharedPtr<FJsonObject> FirstLevelAssetFields(
    const TSharedPtr<FJsonObject>& Result)
{
    const TSharedPtr<FJsonObject>* Object = nullptr;
    const TArray<TSharedPtr<FJsonValue>>* Statements = nullptr;
    if (!Result.IsValid()
        || !Result->TryGetObjectField(TEXT("object"), Object)
        || Object == nullptr
        || !(*Object).IsValid()
        || !(*Object)->TryGetArrayField(TEXT("statements"), Statements)
        || Statements == nullptr)
    {
        return nullptr;
    }

    TSharedPtr<FJsonObject> Fields = MakeShared<FJsonObject>();
    for (const TSharedPtr<FJsonValue>& StatementValue : *Statements)
    {
        const TSharedPtr<FJsonObject>* Statement = nullptr;
        const TSharedPtr<FJsonObject>* Target = nullptr;
        const TSharedPtr<FJsonObject>* Owner = nullptr;
        const TArray<TSharedPtr<FJsonValue>>* Path = nullptr;
        FString TargetKind;
        FString OwnerKind;
        FString OwnerName;
        FString FieldName;
        if (StatementValue.IsValid()
            && StatementValue->TryGetObject(Statement)
            && Statement != nullptr
            && (*Statement).IsValid()
            && (*Statement)->TryGetObjectField(TEXT("target"), Target)
            && Target != nullptr
            && (*Target).IsValid()
            && (*Target)->TryGetStringField(TEXT("kind"), TargetKind)
            && TargetKind == TEXT("member")
            && (*Target)->TryGetObjectField(TEXT("object"), Owner)
            && Owner != nullptr
            && (*Owner).IsValid()
            && (*Owner)->TryGetStringField(TEXT("kind"), OwnerKind)
            && OwnerKind == TEXT("local")
            && (*Owner)->TryGetStringField(TEXT("name"), OwnerName)
            && OwnerName == TEXT("level_scope")
            && (*Target)->TryGetArrayField(TEXT("path"), Path)
            && Path != nullptr
            && Path->Num() == 1
            && (*Path)[0].IsValid()
            && (*Path)[0]->TryGetString(FieldName)
            && !FieldName.IsEmpty())
        {
            Fields->SetField(
                FieldName,
                (*Statement)->TryGetField(TEXT("value")));
        }
    }
    return Fields->Values.IsEmpty() ? nullptr : Fields;
}

bool ReadLevelNextCursor(
    const TSharedPtr<FJsonObject>& Result,
    FString& OutCursor)
{
    OutCursor.Reset();
    const TSharedPtr<FJsonObject>* Page = nullptr;
    return Result.IsValid()
        && Result->TryGetObjectField(TEXT("page"), Page)
        && Page != nullptr
        && (*Page).IsValid()
        && (*Page)->TryGetStringField(TEXT("next"), OutCursor)
        && !OutCursor.IsEmpty();
}

bool ReadLevelLocalField(
    const TSharedPtr<FJsonObject>& Fields,
    const FString& FieldName,
    FString& OutAlias)
{
    OutAlias.Reset();
    const TSharedPtr<FJsonObject>* Ref = nullptr;
    FString Kind;
    return Fields.IsValid()
        && Fields->TryGetObjectField(FieldName, Ref)
        && Ref != nullptr
        && (*Ref).IsValid()
        && (*Ref)->TryGetStringField(TEXT("kind"), Kind)
        && Kind == TEXT("local")
        && (*Ref)->TryGetStringField(TEXT("name"), OutAlias)
        && !OutAlias.IsEmpty();
}

bool ReadLevelNameField(
    const TSharedPtr<FJsonObject>& Fields,
    const FString& FieldName,
    FString& OutName)
{
    OutName.Reset();
    const TSharedPtr<FJsonObject>* Value = nullptr;
    FString Kind;
    return Fields.IsValid()
        && Fields->TryGetObjectField(FieldName, Value)
        && Value != nullptr
        && (*Value).IsValid()
        && (*Value)->TryGetStringField(TEXT("kind"), Kind)
        && Kind == TEXT("name")
        && (*Value)->TryGetStringField(TEXT("name"), OutName)
        && !OutName.IsEmpty();
}

bool ReadLevelStableRefField(
    const TSharedPtr<FJsonObject>& Fields,
    const FString& FieldName,
    const FString& ExpectedSemanticTag,
    TArray<FString>& OutIdentityPath)
{
    OutIdentityPath.Reset();
    const TSharedPtr<FJsonObject>* Ref = nullptr;
    const TArray<TSharedPtr<FJsonValue>>* Segments = nullptr;
    FString Kind;
    FString SemanticTag;
    if (!Fields.IsValid()
        || !Fields->TryGetObjectField(FieldName, Ref)
        || Ref == nullptr
        || !(*Ref).IsValid()
        || !(*Ref)->TryGetStringField(TEXT("kind"), Kind)
        || Kind != TEXT("stable_ref")
        || !(*Ref)->TryGetStringField(
            TEXT("semanticTag"),
            SemanticTag)
        || SemanticTag != ExpectedSemanticTag
        || !(*Ref)->TryGetArrayField(
            TEXT("identityPath"),
            Segments)
        || Segments == nullptr
        || Segments->IsEmpty())
    {
        return false;
    }
    for (const TSharedPtr<FJsonValue>& SegmentValue : *Segments)
    {
        FString Segment;
        if (!SegmentValue.IsValid()
            || !SegmentValue->TryGetString(Segment)
            || Segment.IsEmpty())
        {
            OutIdentityPath.Reset();
            return false;
        }
        OutIdentityPath.Add(Segment);
    }
    return true;
}

bool ReadSingleRelatedLevelTarget(
    const TSharedPtr<FJsonObject>& Result,
    FString& OutAlias,
    FString& OutAsset)
{
    OutAlias.Reset();
    OutAsset.Reset();
    const TArray<TSharedPtr<FJsonValue>>* Related = nullptr;
    if (!Result.IsValid()
        || !Result->TryGetArrayField(TEXT("relatedTargets"), Related)
        || Related == nullptr
        || Related->Num() != 1)
    {
        return false;
    }
    const TSharedPtr<FJsonObject>* Binding = nullptr;
    const TSharedPtr<FJsonObject>* Target = nullptr;
    FString Kind;
    FString Domain;
    FString Type;
    return (*Related)[0].IsValid()
        && (*Related)[0]->TryGetObject(Binding)
        && Binding != nullptr
        && (*Binding).IsValid()
        && (*Binding)->TryGetStringField(TEXT("alias"), OutAlias)
        && !OutAlias.IsEmpty()
        && (*Binding)->TryGetObjectField(TEXT("target"), Target)
        && Target != nullptr
        && (*Target).IsValid()
        && (*Target)->TryGetStringField(TEXT("kind"), Kind)
        && Kind == TEXT("target")
        && (*Target)->TryGetStringField(TEXT("domain"), Domain)
        && Domain == TEXT("level")
        && (*Target)->TryGetStringField(TEXT("asset"), OutAsset)
        && !OutAsset.IsEmpty()
        && (*Target)->TryGetStringField(TEXT("type"), Type)
        && Type == UWorld::StaticClass()->GetPathName();
}

bool ReadRelatedLevelTargets(
    const TSharedPtr<FJsonObject>& Result,
    TMap<FString, FString>& OutAliasesByAsset)
{
    OutAliasesByAsset.Reset();
    const TArray<TSharedPtr<FJsonValue>>* Related = nullptr;
    if (!Result.IsValid()
        || !Result->TryGetArrayField(TEXT("relatedTargets"), Related)
        || Related == nullptr
        || Related->IsEmpty())
    {
        return false;
    }
    for (const TSharedPtr<FJsonValue>& Value : *Related)
    {
        const TSharedPtr<FJsonObject>* Binding = nullptr;
        const TSharedPtr<FJsonObject>* Target = nullptr;
        FString Alias;
        FString Asset;
        FString Kind;
        FString Domain;
        FString Type;
        if (!Value.IsValid()
            || !Value->TryGetObject(Binding)
            || Binding == nullptr
            || !(*Binding).IsValid()
            || !(*Binding)->TryGetStringField(TEXT("alias"), Alias)
            || Alias.IsEmpty()
            || !(*Binding)->TryGetObjectField(TEXT("target"), Target)
            || Target == nullptr
            || !(*Target).IsValid()
            || !(*Target)->TryGetStringField(TEXT("kind"), Kind)
            || Kind != TEXT("target")
            || !(*Target)->TryGetStringField(TEXT("domain"), Domain)
            || Domain != TEXT("level")
            || !(*Target)->TryGetStringField(TEXT("asset"), Asset)
            || Asset.IsEmpty()
            || !(*Target)->TryGetStringField(TEXT("type"), Type)
            || Type != UWorld::StaticClass()->GetPathName()
            || OutAliasesByAsset.Contains(Asset)
            || OutAliasesByAsset.FindKey(Alias) != nullptr)
        {
            OutAliasesByAsset.Reset();
            return false;
        }
        OutAliasesByAsset.Add(Asset, Alias);
    }
    return true;
}

bool HasSingleLevelHandoff(
    const TSharedPtr<FJsonObject>& Result,
    const FString& ExpectedPurpose,
    const FString& ExpectedAlias)
{
    const TArray<TSharedPtr<FJsonValue>>* Handoffs = nullptr;
    if (!Result.IsValid()
        || !Result->TryGetArrayField(TEXT("handoffs"), Handoffs)
        || Handoffs == nullptr
        || Handoffs->Num() != 1)
    {
        return false;
    }
    const TSharedPtr<FJsonObject>* Handoff = nullptr;
    const TSharedPtr<FJsonObject>* Target = nullptr;
    FString Kind;
    FString Purpose;
    FString RefKind;
    FString Alias;
    return (*Handoffs)[0].IsValid()
        && (*Handoffs)[0]->TryGetObject(Handoff)
        && Handoff != nullptr
        && (*Handoff).IsValid()
        && (*Handoff)->TryGetStringField(TEXT("kind"), Kind)
        && Kind == TEXT("target_handoff")
        && (*Handoff)->TryGetStringField(TEXT("purpose"), Purpose)
        && Purpose == ExpectedPurpose
        && (*Handoff)->TryGetObjectField(TEXT("target"), Target)
        && Target != nullptr
        && (*Target).IsValid()
        && (*Target)->TryGetStringField(TEXT("kind"), RefKind)
        && RefKind == TEXT("local")
        && (*Target)->TryGetStringField(TEXT("name"), Alias)
        && Alias == ExpectedAlias;
}

bool ReadLevelHandoffAliases(
    const TSharedPtr<FJsonObject>& Result,
    const FString& ExpectedPurpose,
    TSet<FString>& OutAliases)
{
    OutAliases.Reset();
    const TArray<TSharedPtr<FJsonValue>>* Handoffs = nullptr;
    if (!Result.IsValid()
        || !Result->TryGetArrayField(TEXT("handoffs"), Handoffs)
        || Handoffs == nullptr
        || Handoffs->IsEmpty())
    {
        return false;
    }
    for (const TSharedPtr<FJsonValue>& Value : *Handoffs)
    {
        const TSharedPtr<FJsonObject>* Handoff = nullptr;
        const TSharedPtr<FJsonObject>* Target = nullptr;
        FString Kind;
        FString Purpose;
        FString RefKind;
        FString Alias;
        if (!Value.IsValid()
            || !Value->TryGetObject(Handoff)
            || Handoff == nullptr
            || !(*Handoff).IsValid()
            || !(*Handoff)->TryGetStringField(TEXT("kind"), Kind)
            || Kind != TEXT("target_handoff")
            || !(*Handoff)->TryGetStringField(TEXT("purpose"), Purpose)
            || Purpose != ExpectedPurpose
            || !(*Handoff)->TryGetObjectField(TEXT("target"), Target)
            || Target == nullptr
            || !(*Target).IsValid()
            || !(*Target)->TryGetStringField(TEXT("kind"), RefKind)
            || RefKind != TEXT("local")
            || !(*Target)->TryGetStringField(TEXT("name"), Alias)
            || Alias.IsEmpty()
            || OutAliases.Contains(Alias))
        {
            OutAliases.Reset();
            return false;
        }
        OutAliases.Add(Alias);
    }
    return true;
}

bool HasNoLevelRelatedContext(
    const TSharedPtr<FJsonObject>& Result)
{
    return Result.IsValid()
        && !Result->HasField(TEXT("relatedTargets"))
        && !Result->HasField(TEXT("handoffs"));
}

void PrepareLevelPackageForCollection(UPackage* Package)
{
    if (Package == nullptr)
    {
        return;
    }
    Package->SetDirtyFlag(false);
    Package->ClearFlags(RF_Public | RF_Standalone);
    ForEachObjectWithPackage(
        Package,
        [](UObject* Object)
        {
            Object->ClearFlags(RF_Public | RF_Standalone);
            return true;
        },
        Loomle::Tests::IncludeNestedObjects);
}

bool SetFixtureGuidProperty(
    AActor* Actor,
    const FName PropertyName,
    const FGuid& Guid)
{
    FStructProperty* GuidProperty =
        FindFProperty<FStructProperty>(
            AActor::StaticClass(),
            PropertyName);
    if (Actor == nullptr
        || GuidProperty == nullptr
        || GuidProperty->Struct != TBaseStructure<FGuid>::Get())
    {
        return false;
    }
    *GuidProperty->ContainerPtrToValuePtr<FGuid>(Actor) = Guid;
    return true;
}

bool SetFixtureActorGuid(AActor* Actor, const FGuid& Guid)
{
    return SetFixtureGuidProperty(
            Actor,
            TEXT("ActorGuid"),
            Guid)
        && Actor->GetActorGuid() == Guid;
}

struct FLevelMapRecord
{
    FString PackageName;
    FString ObjectPath;
    FString Filename;
    UWorld* World = nullptr;
    bool bRegisteredInMemory = false;
};

class FScopedLevelQueryFixture
{
public:
    ~FScopedLevelQueryFixture()
    {
        FString Ignored;
        Cleanup(Ignored);
    }

    FScopedLevelQueryFixture(const FScopedLevelQueryFixture&) = delete;
    FScopedLevelQueryFixture& operator=(const FScopedLevelQueryFixture&) = delete;
    FScopedLevelQueryFixture() = default;

    bool Build(FString& OutError)
    {
        Token = FGuid::NewGuid().ToString(EGuidFormats::Digits);
        RootPackagePath = FString::Printf(
            TEXT("/Game/LoomleTests/Level/%s"),
            *Token);

        if (!CreateMap(TEXT("L_10_Loaded"), Loaded, OutError))
        {
            return false;
        }
        Alpha = SpawnActor(
            Loaded.World,
            TEXT("Actor_Alpha"),
            TEXT("Loomle Level Alpha"),
            FTransform(FVector(100.0, 20.0, 5.0)),
            OutError);
        Beta = SpawnActor(
            Loaded.World,
            TEXT("Actor_Beta"),
            TEXT("Loomle Level Beta"),
            FTransform(FVector(-25.0, 50.0, 10.0)),
            OutError);
        Gamma = SpawnActor(
            Loaded.World,
            TEXT("Actor_Gamma"),
            TEXT("Loomle Level Gamma"),
            FTransform(FVector(0.0, 0.0, 125.0)),
            OutError);
        InstanceProjection = SpawnActor(
            Loaded.World,
            TEXT("Actor_InstanceProjection"),
            TEXT("Loomle Instance Projection"),
            FTransform::Identity,
            OutError);
        ExternalContent = SpawnActor(
            Loaded.World,
            TEXT("Actor_ExternalContent"),
            TEXT("Loomle External Content"),
            FTransform::Identity,
            OutError);
        PcgGenerated = SpawnActor(
            Loaded.World,
            TEXT("Actor_PCGGenerated"),
            TEXT("Loomle PCG Generated"),
            FTransform::Identity,
            OutError);
        if (Alpha == nullptr
            || Beta == nullptr
            || Gamma == nullptr
            || InstanceProjection == nullptr
            || ExternalContent == nullptr
            || PcgGenerated == nullptr)
        {
            return false;
        }
        const FGuid InstanceGuid = FGuid::NewGuid();
        const FGuid ContentBundleGuid = FGuid::NewGuid();
        if (!SetFixtureGuidProperty(
            InstanceProjection,
            TEXT("ActorInstanceGuid"),
            InstanceGuid)
            || InstanceProjection->GetActorInstanceGuid() != InstanceGuid
            || !SetFixtureGuidProperty(
            ExternalContent,
            TEXT("ContentBundleGuid"),
            ContentBundleGuid)
            || !ExternalContent->HasExternalContent())
        {
            OutError = TEXT("The Level fixture could not author projection and external-content evidence.");
            return false;
        }
        PcgGenerated->Tags.AddUnique(
            PCGHelpers::DefaultPCGActorTag);
        AlphaId = Alpha->GetActorGuid();
        BetaId = Beta->GetActorGuid();
        GammaId = Gamma->GetActorGuid();
        InstanceProjectionId = InstanceProjection->GetActorGuid();
        ExternalContentId = ExternalContent->GetActorGuid();
        PcgGeneratedId = PcgGenerated->GetActorGuid();
        if (!AlphaId.IsValid()
            || !BetaId.IsValid()
            || !GammaId.IsValid()
            || !InstanceProjectionId.IsValid()
            || !ExternalContentId.IsValid()
            || !PcgGeneratedId.IsValid())
        {
            OutError = TEXT("UE did not assign persistent ActorGuids to the loaded fixture Actors.");
            return false;
        }
        if (!SaveMap(Loaded, OutError))
        {
            return false;
        }

        if (!CreateMap(TEXT("L_20_Unloaded"), Unloaded, OutError)
            || !SaveMap(Unloaded, OutError)
            || !UnloadMap(Unloaded, OutError))
        {
            return false;
        }

        if (!CreateMap(TEXT("L_30_Corrupt"), Corrupt, OutError))
        {
            return false;
        }
        CorruptStable = SpawnActor(
            Corrupt.World,
            TEXT("Actor_Stable"),
            TEXT("Loomle Stable Evidence"),
            FTransform::Identity,
            OutError);
        if (CorruptStable == nullptr || !SaveMap(Corrupt, OutError))
        {
            return false;
        }
        InvalidGuidActor = SpawnActor(
            Corrupt.World,
            TEXT("Actor_InvalidGuid"),
            TEXT("Loomle Invalid Guid"),
            FTransform::Identity,
            OutError);
        DuplicateFirst = SpawnActor(
            Corrupt.World,
            TEXT("Actor_DuplicateA"),
            TEXT("Loomle Duplicate A"),
            FTransform::Identity,
            OutError);
        DuplicateSecond = SpawnActor(
            Corrupt.World,
            TEXT("Actor_DuplicateB"),
            TEXT("Loomle Duplicate B"),
            FTransform::Identity,
            OutError);
        DuplicateId = DuplicateFirst != nullptr
            ? DuplicateFirst->GetActorGuid()
            : FGuid();
        if (InvalidGuidActor == nullptr
            || DuplicateFirst == nullptr
            || DuplicateSecond == nullptr
            || !DuplicateId.IsValid()
            || !SetFixtureActorGuid(InvalidGuidActor, FGuid())
            || !SetFixtureActorGuid(DuplicateSecond, DuplicateId))
        {
            OutError = TEXT("The corrupt Level fixture could not author invalid and duplicate ActorGuid evidence.");
            return false;
        }
        Corrupt.World->GetOutermost()->SetDirtyFlag(false);
        return ValidateOnDiskEvidence(Loaded, false, OutError)
            && ValidateOnDiskEvidence(Unloaded, true, OutError)
            && ValidateOnDiskEvidence(Corrupt, false, OutError);
    }

    bool Cleanup(FString& OutError)
    {
        OutError.Reset();
        if (bCleaned)
        {
            return true;
        }
        bCleaned = true;

        if (bEditorContextChanged && GEditor != nullptr)
        {
            GEditor->GetEditorWorldContext().SetCurrentWorld(
                OriginalEditorWorld);
            bEditorContextChanged = false;
        }

        IAssetRegistry& Registry =
            FModuleManager::LoadModuleChecked<FAssetRegistryModule>(
                TEXT("AssetRegistry"))
                .Get();
        for (FLevelMapRecord* Record : {&Loaded, &Unloaded, &Corrupt})
        {
            if (Record->World != nullptr)
            {
                if (Record->bRegisteredInMemory)
                {
                    FAssetRegistryModule::AssetDeleted(Record->World);
                    Record->bRegisteredInMemory = false;
                }
                UPackage* Package = Record->World->GetOutermost();
                Record->World->DestroyWorld(false);
                PrepareLevelPackageForCollection(Package);
                Record->World = nullptr;
            }
            else if (!Record->PackageName.IsEmpty())
            {
                if (UPackage* Package = FindPackage(
                        nullptr,
                        *Record->PackageName))
                {
                    PrepareLevelPackageForCollection(Package);
                }
            }
        }

        Alpha = nullptr;
        Beta = nullptr;
        Gamma = nullptr;
        InstanceProjection = nullptr;
        ExternalContent = nullptr;
        PcgGenerated = nullptr;
        CorruptStable = nullptr;
        InvalidGuidActor = nullptr;
        DuplicateFirst = nullptr;
        DuplicateSecond = nullptr;
        CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);

        TArray<FString> Filenames;
        for (const FLevelMapRecord* Record : {&Loaded, &Unloaded, &Corrupt})
        {
            if (!Record->Filename.IsEmpty())
            {
                IFileManager::Get().Delete(
                    *Record->Filename,
                    false,
                    true,
                    true);
                Filenames.Add(Record->Filename);
            }
            if (!Record->PackageName.IsEmpty()
                && FindPackage(nullptr, *Record->PackageName) != nullptr
                && OutError.IsEmpty())
            {
                OutError = TEXT("Level fixture package remained loaded during cleanup: ")
                    + Record->PackageName;
            }
        }
        if (!Filenames.IsEmpty())
        {
            Registry.ScanModifiedAssetFiles(Filenames);
        }
        return OutError.IsEmpty();
    }

    bool IsUnloadedMapStillUnloaded() const
    {
        return FindPackage(nullptr, *Unloaded.PackageName) == nullptr
            && FindObject<UWorld>(nullptr, *Unloaded.ObjectPath) == nullptr;
    }

    bool Activate(
        const FLevelMapRecord& Record,
        FString& OutError)
    {
        if (GEditor == nullptr || Record.World == nullptr)
        {
            OutError = TEXT("Cannot activate a null Level fixture in the Editor World context.");
            return false;
        }
        FWorldContext& Context = GEditor->GetEditorWorldContext();
        if (!bEditorContextChanged)
        {
            OriginalEditorWorld = Context.World();
            bEditorContextChanged = true;
        }
        Context.SetCurrentWorld(Record.World);
        if (Context.World() != Record.World)
        {
            OutError = TEXT("UE failed to bind the saved Level fixture to the Editor World context.");
            return false;
        }
        return true;
    }

    FLevelMapRecord Loaded;
    FLevelMapRecord Unloaded;
    FLevelMapRecord Corrupt;
    AActor* Alpha = nullptr;
    AActor* Beta = nullptr;
    AActor* Gamma = nullptr;
    AActor* InstanceProjection = nullptr;
    AActor* ExternalContent = nullptr;
    AActor* PcgGenerated = nullptr;
    AActor* CorruptStable = nullptr;
    AActor* InvalidGuidActor = nullptr;
    AActor* DuplicateFirst = nullptr;
    AActor* DuplicateSecond = nullptr;
    FGuid AlphaId;
    FGuid BetaId;
    FGuid GammaId;
    FGuid InstanceProjectionId;
    FGuid ExternalContentId;
    FGuid PcgGeneratedId;
    FGuid DuplicateId;

private:
    static UWorld::InitializationValues FixtureInitializationValues()
    {
        return UWorld::InitializationValues()
            .RequiresHitProxies(false)
            .ShouldSimulatePhysics(false)
            .EnableTraceCollision(false)
            .CreateNavigation(false)
            .CreateAISystem(false)
            .AllowAudioPlayback(false)
            .CreatePhysicsScene(false);
    }

    bool CreateMap(
        const FString& AssetName,
        FLevelMapRecord& OutRecord,
        FString& OutError)
    {
        OutRecord.PackageName = RootPackagePath + TEXT("/") + AssetName;
        OutRecord.ObjectPath = OutRecord.PackageName
            + TEXT(".")
            + AssetName;
        OutRecord.Filename = FPackageName::LongPackageNameToFilename(
            OutRecord.PackageName,
            FPackageName::GetMapPackageExtension());
        IFileManager::Get().MakeDirectory(
            *FPaths::GetPath(OutRecord.Filename),
            true);

        UPackage* Package = CreatePackage(*OutRecord.PackageName);
        const UWorld::InitializationValues InitValues =
            FixtureInitializationValues();
        OutRecord.World = UWorld::CreateWorld(
            EWorldType::Editor,
            false,
            FName(*AssetName),
            Package,
            false,
            ERHIFeatureLevel::Num,
            &InitValues);
        if (OutRecord.World == nullptr
            || OutRecord.World->PersistentLevel == nullptr
            || OutRecord.World->GetPathName() != OutRecord.ObjectPath)
        {
            OutError = TEXT("UE failed to create an exact Editor UWorld fixture at ")
                + OutRecord.ObjectPath;
            return false;
        }
        OutRecord.World->SetFlags(
            RF_Public | RF_Standalone | RF_Transactional);
        FAssetRegistryModule::AssetCreated(OutRecord.World);
        OutRecord.bRegisteredInMemory = true;
        OutRecord.World->GetOutermost()->SetDirtyFlag(false);
        return true;
    }

    static AActor* SpawnActor(
        UWorld* World,
        const FString& Name,
        const FString& Label,
        const FTransform& Transform,
        FString& OutError)
    {
        FActorSpawnParameters Params;
        Params.Name = FName(*Name);
        Params.OverrideLevel = World != nullptr
            ? World->PersistentLevel
            : nullptr;
        Params.ObjectFlags = RF_Transactional;
        AActor* Actor = World != nullptr
            ? World->SpawnActor<AActor>(
                AActor::StaticClass(),
                Transform,
                Params)
            : nullptr;
        if (Actor == nullptr)
        {
            OutError = TEXT("UE failed to spawn Level fixture Actor ") + Name;
            return nullptr;
        }
        Actor->SetActorLabel(Label, false);
        return Actor;
    }

    static bool SaveMap(
        FLevelMapRecord& Record,
        FString& OutError)
    {
        if (Record.World == nullptr)
        {
            OutError = TEXT("Cannot save a null Level fixture World.");
            return false;
        }
        UPackage* Package = Record.World->GetOutermost();
        Package->SetDirtyFlag(true);
        Package->FullyLoad();
        FSavePackageArgs SaveArgs;
        SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
        SaveArgs.Error = GLog;
        if (!UPackage::SavePackage(
                Package,
                Record.World,
                *Record.Filename,
                SaveArgs))
        {
            OutError = TEXT("UE failed to save Level fixture ")
                + Record.ObjectPath;
            return false;
        }
        Package->SetDirtyFlag(false);
        FModuleManager::LoadModuleChecked<FAssetRegistryModule>(
            TEXT("AssetRegistry"))
            .Get()
            .ScanModifiedAssetFiles({Record.Filename});
        return true;
    }

    static bool UnloadMap(
        FLevelMapRecord& Record,
        FString& OutError)
    {
        if (Record.World == nullptr)
        {
            OutError = TEXT("Cannot unload a null Level fixture World.");
            return false;
        }
        UPackage* Package = Record.World->GetOutermost();
        if (Record.bRegisteredInMemory)
        {
            FAssetRegistryModule::AssetDeleted(Record.World);
            Record.bRegisteredInMemory = false;
        }
        Record.World->DestroyWorld(false);
        PrepareLevelPackageForCollection(Package);
        Record.World = nullptr;
        CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);

        FModuleManager::LoadModuleChecked<FAssetRegistryModule>(
            TEXT("AssetRegistry"))
            .Get()
            .ScanModifiedAssetFiles({Record.Filename});
        if (FindPackage(nullptr, *Record.PackageName) != nullptr
            || FindObject<UWorld>(nullptr, *Record.ObjectPath) != nullptr)
        {
            OutError = TEXT("Saved Level fixture remained loaded: ")
                + Record.ObjectPath;
            return false;
        }
        return true;
    }

    static bool ValidateOnDiskEvidence(
        const FLevelMapRecord& Record,
        const bool bExpectUnloaded,
        FString& OutError)
    {
        IAssetRegistry& Registry =
            FModuleManager::LoadModuleChecked<FAssetRegistryModule>(
                TEXT("AssetRegistry"))
                .Get();
        const FAssetData Data = Registry.GetAssetByObjectPath(
            FSoftObjectPath(Record.ObjectPath),
            true);
        if (!Data.IsValid()
            || Data.AssetClassPath != UWorld::StaticClass()->GetClassPathName())
        {
            OutError = TEXT("Asset Registry lacks on-disk UWorld evidence for ")
                + Record.ObjectPath;
            return false;
        }
        if (bExpectUnloaded
            && (Data.FastGetAsset(false) != nullptr
                || FindPackage(nullptr, *Record.PackageName) != nullptr))
        {
            OutError = TEXT("On-disk Level fixture unexpectedly remained loaded: ")
                + Record.ObjectPath;
            return false;
        }
        return true;
    }

    FString Token;
    FString RootPackagePath;
    bool bCleaned = false;
    UWorld* OriginalEditorWorld = nullptr;
    bool bEditorContextChanged = false;
};

class FScopedLevelComponentQueryFixture
{
public:
    FScopedLevelComponentQueryFixture() = default;
    FScopedLevelComponentQueryFixture(
        const FScopedLevelComponentQueryFixture&) = delete;
    FScopedLevelComponentQueryFixture& operator=(
        const FScopedLevelComponentQueryFixture&) = delete;

    ~FScopedLevelComponentQueryFixture()
    {
        FString Ignored;
        Cleanup(Ignored);
    }

    bool Build(
        FScopedLevelQueryFixture& LevelFixture,
        FString& OutError)
    {
        OutError.Reset();
        UWorld* World = LevelFixture.Loaded.World;
        if (World == nullptr || World->PersistentLevel == nullptr)
        {
            OutError = TEXT(
                "Component fixture requires the loaded Level fixture World.");
            return false;
        }

        const FString Token =
            FGuid::NewGuid().ToString(EGuidFormats::Digits);
        BlueprintPackageName = FString::Printf(
            TEXT("/Game/LoomleTests/LevelComponent/%s/BP_LevelComponent"),
            *Token);
        BlueprintPackage = CreatePackage(*BlueprintPackageName);
        Blueprint = BlueprintPackage != nullptr
            ? FKismetEditorUtilities::CreateBlueprint(
                AStaticMeshActor::StaticClass(),
                BlueprintPackage,
                FName(TEXT("BP_LevelComponent")),
                BPTYPE_Normal,
                UBlueprint::StaticClass(),
                UBlueprintGeneratedClass::StaticClass(),
                NAME_None)
            : nullptr;
        if (Blueprint == nullptr
            || Blueprint->SimpleConstructionScript == nullptr)
        {
            OutError = TEXT(
                "UE failed to create the Component fixture Blueprint and SCS.");
            return false;
        }
        BlueprintRoot.Reset(Blueprint);
        FAssetRegistryModule::AssetCreated(Blueprint);
        bBlueprintRegistered = true;

        SCSNode = Blueprint->SimpleConstructionScript->CreateNode(
            USceneComponent::StaticClass(),
            FName(TEXT("LoomleSCSComponent")));
        SCSCollisionProbeNode =
            Blueprint->SimpleConstructionScript->CreateNode(
                USceneComponent::StaticClass(),
                FName(TEXT("LoomleSCSCollisionProbe")));
        if (SCSNode == nullptr
            || !SCSNode->VariableGuid.IsValid()
            || SCSCollisionProbeNode == nullptr
            || !SCSCollisionProbeNode->VariableGuid.IsValid()
            || SCSCollisionProbeNode->VariableGuid == SCSNode->VariableGuid)
        {
            OutError = TEXT(
                "UE failed to create the Component fixture SCS declaration.");
            return false;
        }
        Blueprint->SimpleConstructionScript->AddNode(SCSNode);
        Blueprint->SimpleConstructionScript->AddNode(
            SCSCollisionProbeNode);
        FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
        FKismetEditorUtilities::CompileBlueprint(Blueprint);
        GeneratedClass = Cast<UBlueprintGeneratedClass>(
            Blueprint->GeneratedClass);
        if (Blueprint->Status == BS_Error || GeneratedClass == nullptr)
        {
            OutError = TEXT(
                "The Component fixture Blueprint failed to compile.");
            return false;
        }

        FActorSpawnParameters Params;
        Params.Name = FName(TEXT("Actor_LevelComponents"));
        Params.OverrideLevel = World->PersistentLevel;
        Params.ObjectFlags = RF_Transactional;
        Actor = World->SpawnActor<AActor>(
            GeneratedClass,
            FTransform::Identity,
            Params);
        if (Actor == nullptr)
        {
            OutError = TEXT(
                "UE failed to spawn the Component fixture Blueprint Actor.");
            return false;
        }
        Actor->SetActorLabel(TEXT("Loomle Component Owner"), false);
        ActorId = Actor->GetActorGuid();
        NativeComponent = CastChecked<AStaticMeshActor>(Actor)
            ->GetStaticMeshComponent();

        TArray<UActorComponent*> SpawnedComponents;
        Actor->GetComponents(SpawnedComponents, false);
        for (UActorComponent* Component : SpawnedComponents)
        {
            if (Component != nullptr
                && Component->CreationMethod
                    == EComponentCreationMethod::SimpleConstructionScript
                && Component->GetFName() == SCSNode->GetVariableName())
            {
                SCSComponent = Component;
            }
        }
        const FObjectPropertyBase* SCSProperty =
            FindFProperty<FObjectPropertyBase>(
                GeneratedClass,
                SCSNode->GetVariableName());

        InstanceComponent = NewObject<USceneComponent>(
            Actor,
            USceneComponent::StaticClass(),
            FName(TEXT("LoomleInstanceComponent")),
            RF_Transactional);
        if (InstanceComponent != nullptr)
        {
            Actor->AddInstanceComponent(InstanceComponent);
            InstanceComponent->OnComponentCreated();
            InstanceComponent->RegisterComponent();
        }

        PCGComponent = NewObject<UPCGComponent>(
            Actor,
            UPCGComponent::StaticClass(),
            FName(TEXT("LoomlePCGComponent")),
            RF_Transactional);
        if (PCGComponent != nullptr)
        {
            Actor->AddInstanceComponent(PCGComponent);
            PCGComponent->OnComponentCreated();
        }

        const TArray<TPair<FName, FName>> GeneratedPCGSpecs = {
            {
                FName(TEXT("LoomlePCGGeneratedComponent")),
                PCGHelpers::DefaultPCGTag
            },
            {
                FName(TEXT("LoomlePCGDebugComponent")),
                PCGHelpers::DefaultPCGDebugTag
            },
            {
                FName(TEXT("LoomlePCGCleanupComponent")),
                PCGHelpers::MarkedForCleanupPCGTag
            }
        };
        for (const TPair<FName, FName>& Spec : GeneratedPCGSpecs)
        {
            USceneComponent* Generated = NewObject<USceneComponent>(
                Actor,
                USceneComponent::StaticClass(),
                Spec.Key,
                RF_Transactional);
            if (Generated != nullptr)
            {
                Actor->AddInstanceComponent(Generated);
                Generated->ComponentTags.Add(Spec.Value);
                GeneratedPCGComponents.Add(Generated);
            }
        }

        UCSComponent = Actor->AddComponentByClass(
            USceneComponent::StaticClass(),
            true,
            FTransform::Identity,
            false);

        if (!ActorId.IsValid()
            || NativeComponent == nullptr
            || NativeComponent->CreationMethod
                != EComponentCreationMethod::Native
            || SCSComponent == nullptr
            || InstanceComponent == nullptr
            || !InstanceComponent->IsRegistered()
            || PCGComponent == nullptr
            || PCGComponent->IsRegistered()
            || PCGComponent->GetConstOriginalComponent() != PCGComponent
            || GeneratedPCGComponents.Num() != GeneratedPCGSpecs.Num()
            || UCSComponent == nullptr
            || UCSComponent->CreationMethod
                != EComponentCreationMethod::UserConstructionScript)
        {
            OutError = TEXT(
                "The Component fixture did not produce exact Native, SCS, "
                "Instance, PCG, and UCS source evidence.");
            return false;
        }

        NativeId = NativeComponent->GetFName().ToString();
        InstanceId = InstanceComponent->GetFName().ToString();
        PCGId = PCGComponent->GetFName().ToString();
        SCSDeclaringClass = GeneratedClass->GetPathName();
        SCSId = SCSDeclaringClass
            + TEXT("#")
            + LevelGuidText(SCSNode->VariableGuid);
        if (NativeId.IsEmpty()
            || InstanceId.IsEmpty()
            || PCGId.IsEmpty()
            || SCSDeclaringClass.IsEmpty()
            || SCSId.IsEmpty()
            || SCSProperty == nullptr
            || SCSProperty->GetObjectPropertyValue_InContainer(Actor)
                != SCSComponent)
        {
            OutError = TEXT(
                "The Component fixture could not prove its stable source locators.");
            return false;
        }

        World->GetOutermost()->SetDirtyFlag(false);
        BlueprintPackage->SetDirtyFlag(false);
        return true;
    }

    bool Cleanup(FString& OutError)
    {
        OutError.Reset();
        if (bCleaned)
        {
            return true;
        }
        bCleaned = true;

        UBlueprint* RootedBlueprint = BlueprintRoot.Get();
        if (bBlueprintRegistered && RootedBlueprint != nullptr)
        {
            FAssetRegistryModule::AssetDeleted(RootedBlueprint);
            bBlueprintRegistered = false;
        }
        UPackage* Package = !BlueprintPackageName.IsEmpty()
            ? FindPackage(nullptr, *BlueprintPackageName)
            : nullptr;
        PrepareLevelPackageForCollection(Package);

        Actor = nullptr;
        NativeComponent = nullptr;
        SCSComponent = nullptr;
        InstanceComponent = nullptr;
        PCGComponent = nullptr;
        GeneratedPCGComponents.Reset();
        UCSComponent = nullptr;
        SCSNode = nullptr;
        SCSCollisionProbeNode = nullptr;
        GeneratedClass = nullptr;
        Blueprint = nullptr;
        BlueprintPackage = nullptr;
        BlueprintRoot.Reset();
        CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);
        if (!BlueprintPackageName.IsEmpty()
            && FindPackage(nullptr, *BlueprintPackageName) != nullptr)
        {
            OutError = TEXT(
                "Component fixture Blueprint package remained loaded during cleanup: ")
                + BlueprintPackageName;
        }
        return OutError.IsEmpty();
    }

    AActor* Actor = nullptr;
    UActorComponent* NativeComponent = nullptr;
    UActorComponent* SCSComponent = nullptr;
    USceneComponent* InstanceComponent = nullptr;
    UPCGComponent* PCGComponent = nullptr;
    TArray<USceneComponent*> GeneratedPCGComponents;
    UActorComponent* UCSComponent = nullptr;
    FGuid ActorId;
    FString NativeId;
    FString SCSId;
    FString SCSDeclaringClass;
    FString InstanceId;
    FString PCGId;

    UPackage* GetBlueprintPackage() const
    {
        return BlueprintPackage;
    }

    bool BeginSCSGuidCollision(FGuid& OutOriginalGuid)
    {
        OutOriginalGuid.Invalidate();
        if (SCSNode == nullptr
            || SCSCollisionProbeNode == nullptr
            || !SCSNode->VariableGuid.IsValid()
            || !SCSCollisionProbeNode->VariableGuid.IsValid())
        {
            return false;
        }
        OutOriginalGuid = SCSCollisionProbeNode->VariableGuid;
        SCSCollisionProbeNode->VariableGuid = SCSNode->VariableGuid;
        return true;
    }

    void EndSCSGuidCollision(const FGuid& OriginalGuid)
    {
        if (SCSCollisionProbeNode != nullptr && OriginalGuid.IsValid())
        {
            SCSCollisionProbeNode->VariableGuid = OriginalGuid;
        }
    }

private:
    FString BlueprintPackageName;
    UPackage* BlueprintPackage = nullptr;
    UBlueprint* Blueprint = nullptr;
    UBlueprintGeneratedClass* GeneratedClass = nullptr;
    USCS_Node* SCSNode = nullptr;
    USCS_Node* SCSCollisionProbeNode = nullptr;
    TStrongObjectPtr<UBlueprint> BlueprintRoot;
    bool bBlueprintRegistered = false;
    bool bCleaned = false;
};

struct FLevelInstanceLoadState
{
    TWeakObjectPtr<ALevelInstance> Actor;
    FString SourcePath;
    FGuid ActorInstanceGuid;
    bool bLoaded = false;
    bool bLoading = false;
};

class FScopedLevelInstanceQueryFixture
{
public:
    FScopedLevelInstanceQueryFixture() = default;
    FScopedLevelInstanceQueryFixture(
        const FScopedLevelInstanceQueryFixture&) = delete;
    FScopedLevelInstanceQueryFixture& operator=(
        const FScopedLevelInstanceQueryFixture&) = delete;

    ~FScopedLevelInstanceQueryFixture()
    {
        FString Ignored;
        Cleanup(Ignored);
    }

    bool Build(FString& OutError)
    {
        Token = FGuid::NewGuid().ToString(EGuidFormats::Digits);
        RootPackagePath = FString::Printf(
            TEXT("/Game/LoomleTests/LevelInstance/%s"),
            *Token);

        if (!CreateMap(TEXT("L_10_SourceA"), SourceA, OutError)
            || !SaveMap(SourceA, OutError)
            || !UnloadMap(SourceA, OutError)
            || !CreateMap(TEXT("L_20_SourceB"), SourceB, OutError)
            || !SaveMap(SourceB, OutError)
            || !UnloadMap(SourceB, OutError)
            || !CreateMap(
                TEXT("L_30_Containing"),
                Containing,
                OutError))
        {
            return false;
        }

        Ordinary = SpawnActor(
            Containing.World,
            TEXT("Actor_Ordinary"),
            TEXT("Loomle Ordinary Actor"),
            OutError);
        SharedOne = SpawnLevelInstance(
            Containing.World,
            TEXT("LI_SharedA"),
            TEXT("Loomle Resolved Instance Shared Source A"),
            OutError);
        SharedTwo = SpawnLevelInstance(
            Containing.World,
            TEXT("LI_SharedB"),
            TEXT("Loomle Resolved Instance Shared Source B"),
            OutError);
        OtherSource = SpawnLevelInstance(
            Containing.World,
            TEXT("LI_OtherSource"),
            TEXT("Loomle Resolved Instance Other Source"),
            OutError);
        MissingSource = SpawnLevelInstance(
            Containing.World,
            TEXT("LI_MissingSource"),
            TEXT("Loomle Missing Source Instance"),
            OutError);
        if (Ordinary == nullptr
            || SharedOne == nullptr
            || SharedTwo == nullptr
            || OtherSource == nullptr
            || MissingSource == nullptr
            || !SetSource(SharedOne, SourceA.ObjectPath, OutError)
            || !SetSource(SharedTwo, SourceA.ObjectPath, OutError)
            || !SetSource(OtherSource, SourceB.ObjectPath, OutError))
        {
            return false;
        }

        OrdinaryId = Ordinary->GetActorGuid();
        SharedOneId = SharedOne->GetActorGuid();
        SharedTwoId = SharedTwo->GetActorGuid();
        OtherSourceId = OtherSource->GetActorGuid();
        MissingSourceId = MissingSource->GetActorGuid();
        if (!OrdinaryId.IsValid()
            || !SharedOneId.IsValid()
            || !SharedTwoId.IsValid()
            || !OtherSourceId.IsValid()
            || !MissingSourceId.IsValid())
        {
            OutError = TEXT(
                "UE did not assign persistent ActorGuids to the Level Instance fixture Actors.");
            return false;
        }

        if (!SaveMap(Containing, OutError)
            || !ValidateOnDiskEvidence(SourceA, true, OutError)
            || !ValidateOnDiskEvidence(SourceB, true, OutError)
            || !ValidateOnDiskEvidence(Containing, false, OutError)
            || !SourcesRemainUnloaded())
        {
            if (OutError.IsEmpty())
            {
                OutError = TEXT(
                    "Creating the containing Level loaded a Level Instance source map.");
            }
            return false;
        }
        return true;
    }

    bool Activate(FString& OutError)
    {
        if (GEditor == nullptr || Containing.World == nullptr)
        {
            OutError = TEXT(
                "Cannot activate a null containing Level Instance fixture World.");
            return false;
        }
        FWorldContext& Context = GEditor->GetEditorWorldContext();
        if (!bEditorContextChanged)
        {
            OriginalEditorWorld = Context.World();
            bEditorContextChanged = true;
        }
        Context.SetCurrentWorld(Containing.World);
        if (Context.World() != Containing.World)
        {
            OutError = TEXT(
                "UE failed to bind the containing Level Instance fixture to the Editor World context.");
            return false;
        }
        return true;
    }

    bool CaptureNoLoadState(FString& OutError)
    {
        OutError.Reset();
        if (Containing.World == nullptr
            || !SourcesRemainUnloaded()
            || HasLoadedSourceInstancePackage())
        {
            OutError = TEXT(
                "The Level Instance no-load fixture already contained loaded source or temporary instance packages.");
            return false;
        }
        LevelInstanceSubsystem =
            Containing.World->GetSubsystem<ULevelInstanceSubsystem>();
        if (LevelInstanceSubsystem == nullptr)
        {
            OutError = TEXT(
                "The containing World has no Level Instance subsystem.");
            return false;
        }
        LevelsBefore = Containing.World->GetLevels();
        LoadStates.Reset();
        for (ALevelInstance* Actor : {
                 SharedOne,
                 SharedTwo,
                 OtherSource,
                 MissingSource})
        {
            if (Actor == nullptr)
            {
                OutError = TEXT(
                    "The Level Instance read invariant received a null placement Actor.");
                return false;
            }
            FLevelInstanceLoadState& State =
                LoadStates.AddDefaulted_GetRef();
            State.Actor = Actor;
            State.SourcePath =
                Actor->GetWorldAsset().ToSoftObjectPath().ToString();
            State.ActorInstanceGuid = Actor->GetActorInstanceGuid();
            State.bLoaded = LevelInstanceSubsystem->IsLoaded(Actor);
            State.bLoading = LevelInstanceSubsystem->IsLoading(Actor);
            if (State.bLoaded
                || State.bLoading
                || State.ActorInstanceGuid != Actor->GetActorGuid())
            {
                OutError = TEXT(
                    "A Level Instance fixture placement was already loaded, loading, or projected before Query.");
                return false;
            }
        }
        bNoLoadStateCaptured = true;
        return true;
    }

    bool VerifyNoLoadState(FAutomationTestBase& Test) const
    {
        bool bOk = Test.TestTrue(
            TEXT("Level Instance Query keeps both source maps unloaded"),
            SourcesRemainUnloaded());
        bOk &= Test.TestFalse(
            TEXT("Level Instance Query creates no temporary source instance package"),
            HasLoadedSourceInstancePackage());
        bOk &= Test.TestTrue(
            TEXT("Level Instance no-load state was captured"),
            bNoLoadStateCaptured);
        if (!bNoLoadStateCaptured || Containing.World == nullptr)
        {
            return false;
        }
        bOk &= Test.TestEqual(
            TEXT("Level Instance Query preserves the World's Level list"),
            Containing.World->GetLevels(),
            LevelsBefore);
        bOk &= Test.TestEqual(
            TEXT("Level Instance Query preserves the native subsystem"),
            Containing.World->GetSubsystem<ULevelInstanceSubsystem>(),
            LevelInstanceSubsystem);
        for (const FLevelInstanceLoadState& State : LoadStates)
        {
            ALevelInstance* Actor = State.Actor.Get();
            bOk &= Test.TestNotNull(
                TEXT("Level Instance Query preserves each placement Actor"),
                Actor);
            if (Actor == nullptr || LevelInstanceSubsystem == nullptr)
            {
                continue;
            }
            bOk &= Test.TestEqual(
                TEXT("Level Instance Query preserves the saved source path"),
                Actor->GetWorldAsset().ToSoftObjectPath().ToString(),
                State.SourcePath);
            bOk &= Test.TestEqual(
                TEXT("Level Instance Query does not project ActorInstanceGuid"),
                Actor->GetActorInstanceGuid(),
                State.ActorInstanceGuid);
            bOk &= Test.TestEqual(
                TEXT("Level Instance Query does not load a placement"),
                LevelInstanceSubsystem->IsLoaded(Actor),
                State.bLoaded);
            bOk &= Test.TestEqual(
                TEXT("Level Instance Query does not queue a placement load"),
                LevelInstanceSubsystem->IsLoading(Actor),
                State.bLoading);
            bOk &= Test.TestNull(
                TEXT("Level Instance source SoftObjectPtr remains unresolved"),
                Actor->GetWorldAsset().Get());
        }
        return bOk;
    }

    bool SetSharedOneSource(
        const bool bUseSourceB,
        FString& OutError)
    {
        return SetSource(
            SharedOne,
            bUseSourceB ? SourceB.ObjectPath : SourceA.ObjectPath,
            OutError);
    }

    bool SourcesRemainUnloaded() const
    {
        return IsMapUnloaded(SourceA) && IsMapUnloaded(SourceB);
    }

    bool Cleanup(FString& OutError)
    {
        OutError.Reset();
        if (bCleaned)
        {
            return true;
        }
        bCleaned = true;

        if (bEditorContextChanged && GEditor != nullptr)
        {
            GEditor->GetEditorWorldContext().SetCurrentWorld(
                OriginalEditorWorld);
            bEditorContextChanged = false;
        }

        IAssetRegistry& Registry =
            FModuleManager::LoadModuleChecked<FAssetRegistryModule>(
                TEXT("AssetRegistry"))
                .Get();
        for (FLevelMapRecord* Record : {
                 &SourceA,
                 &SourceB,
                 &Containing})
        {
            UWorld* World = Record->World;
            if (World == nullptr && !Record->ObjectPath.IsEmpty())
            {
                World = FindObject<UWorld>(
                    nullptr,
                    *Record->ObjectPath);
            }
            if (World != nullptr)
            {
                if (Record->bRegisteredInMemory)
                {
                    FAssetRegistryModule::AssetDeleted(World);
                    Record->bRegisteredInMemory = false;
                }
                UPackage* Package = World->GetOutermost();
                World->DestroyWorld(false);
                PrepareLevelPackageForCollection(Package);
                Record->World = nullptr;
            }
            else if (!Record->PackageName.IsEmpty())
            {
                if (UPackage* Package = FindPackage(
                        nullptr,
                        *Record->PackageName))
                {
                    PrepareLevelPackageForCollection(Package);
                }
            }
        }

        Ordinary = nullptr;
        SharedOne = nullptr;
        SharedTwo = nullptr;
        OtherSource = nullptr;
        MissingSource = nullptr;
        LevelInstanceSubsystem = nullptr;
        LoadStates.Reset();
        LevelsBefore.Reset();
        CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);

        TArray<FString> Filenames;
        for (const FLevelMapRecord* Record : {
                 &SourceA,
                 &SourceB,
                 &Containing})
        {
            if (!Record->Filename.IsEmpty())
            {
                IFileManager::Get().Delete(
                    *Record->Filename,
                    false,
                    true,
                    true);
                Filenames.Add(Record->Filename);
            }
            if (!Record->PackageName.IsEmpty()
                && FindPackage(nullptr, *Record->PackageName) != nullptr
                && OutError.IsEmpty())
            {
                OutError = TEXT(
                    "Level Instance fixture package remained loaded during cleanup: ")
                    + Record->PackageName;
            }
        }
        if (!Filenames.IsEmpty())
        {
            Registry.ScanModifiedAssetFiles(Filenames);
        }
        return OutError.IsEmpty();
    }

    FLevelMapRecord SourceA;
    FLevelMapRecord SourceB;
    FLevelMapRecord Containing;
    AActor* Ordinary = nullptr;
    ALevelInstance* SharedOne = nullptr;
    ALevelInstance* SharedTwo = nullptr;
    ALevelInstance* OtherSource = nullptr;
    ALevelInstance* MissingSource = nullptr;
    FGuid OrdinaryId;
    FGuid SharedOneId;
    FGuid SharedTwoId;
    FGuid OtherSourceId;
    FGuid MissingSourceId;

private:
    static UWorld::InitializationValues FixtureInitializationValues()
    {
        return UWorld::InitializationValues()
            .RequiresHitProxies(false)
            .ShouldSimulatePhysics(false)
            .EnableTraceCollision(false)
            .CreateNavigation(false)
            .CreateAISystem(false)
            .AllowAudioPlayback(false)
            .CreatePhysicsScene(false);
    }

    bool CreateMap(
        const FString& AssetName,
        FLevelMapRecord& OutRecord,
        FString& OutError)
    {
        OutRecord.PackageName = RootPackagePath + TEXT("/") + AssetName;
        OutRecord.ObjectPath = OutRecord.PackageName
            + TEXT(".")
            + AssetName;
        OutRecord.Filename = FPackageName::LongPackageNameToFilename(
            OutRecord.PackageName,
            FPackageName::GetMapPackageExtension());
        IFileManager::Get().MakeDirectory(
            *FPaths::GetPath(OutRecord.Filename),
            true);

        UPackage* Package = CreatePackage(*OutRecord.PackageName);
        const UWorld::InitializationValues InitValues =
            FixtureInitializationValues();
        OutRecord.World = UWorld::CreateWorld(
            EWorldType::Editor,
            false,
            FName(*AssetName),
            Package,
            false,
            ERHIFeatureLevel::Num,
            &InitValues);
        if (OutRecord.World == nullptr
            || OutRecord.World->PersistentLevel == nullptr
            || OutRecord.World->GetPathName() != OutRecord.ObjectPath)
        {
            OutError = TEXT(
                "UE failed to create an exact Level Instance fixture World at ")
                + OutRecord.ObjectPath;
            return false;
        }
        OutRecord.World->SetFlags(
            RF_Public | RF_Standalone | RF_Transactional);
        FAssetRegistryModule::AssetCreated(OutRecord.World);
        OutRecord.bRegisteredInMemory = true;
        OutRecord.World->GetOutermost()->SetDirtyFlag(false);
        return true;
    }

    static AActor* SpawnActor(
        UWorld* World,
        const FString& Name,
        const FString& Label,
        FString& OutError)
    {
        FActorSpawnParameters Params;
        Params.Name = FName(*Name);
        Params.OverrideLevel = World != nullptr
            ? World->PersistentLevel
            : nullptr;
        Params.ObjectFlags = RF_Transactional;
        AActor* Actor = World != nullptr
            ? World->SpawnActor<AActor>(
                AActor::StaticClass(),
                FTransform::Identity,
                Params)
            : nullptr;
        if (Actor == nullptr)
        {
            OutError = TEXT(
                "UE failed to spawn Level Instance fixture Actor ")
                + Name;
            return nullptr;
        }
        Actor->SetActorLabel(Label, false);
        return Actor;
    }

    static ALevelInstance* SpawnLevelInstance(
        UWorld* World,
        const FString& Name,
        const FString& Label,
        FString& OutError)
    {
        FActorSpawnParameters Params;
        Params.Name = FName(*Name);
        Params.OverrideLevel = World != nullptr
            ? World->PersistentLevel
            : nullptr;
        Params.ObjectFlags = RF_Transactional;
        ALevelInstance* Actor = World != nullptr
            ? World->SpawnActor<ALevelInstance>(
                ALevelInstance::StaticClass(),
                FTransform::Identity,
                Params)
            : nullptr;
        if (Actor == nullptr)
        {
            OutError = TEXT(
                "UE failed to spawn Level Instance placement ")
                + Name;
            return nullptr;
        }
        Actor->SetActorLabel(Label, false);
        return Actor;
    }

    static bool SetSource(
        ALevelInstance* Actor,
        const FString& SourceObjectPath,
        FString& OutError)
    {
        const TSoftObjectPtr<UWorld> Source{
            FSoftObjectPath(SourceObjectPath)};
        if (Actor == nullptr
            || SourceObjectPath.IsEmpty()
            || !Actor->SetWorldAsset(Source)
            || Actor->GetWorldAsset().ToSoftObjectPath().ToString()
                != SourceObjectPath
            || Actor->GetWorldAsset().Get() != nullptr)
        {
            OutError = TEXT(
                "UE failed to retain an unloaded saved source on a Level Instance placement: ")
                + SourceObjectPath;
            return false;
        }
        return true;
    }

    static bool SaveMap(
        FLevelMapRecord& Record,
        FString& OutError)
    {
        if (Record.World == nullptr)
        {
            OutError = TEXT(
                "Cannot save a null Level Instance fixture World.");
            return false;
        }
        UPackage* Package = Record.World->GetOutermost();
        Package->SetDirtyFlag(true);
        Package->FullyLoad();
        FSavePackageArgs SaveArgs;
        SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
        SaveArgs.Error = GLog;
        if (!UPackage::SavePackage(
                Package,
                Record.World,
                *Record.Filename,
                SaveArgs))
        {
            OutError = TEXT(
                "UE failed to save Level Instance fixture ")
                + Record.ObjectPath;
            return false;
        }
        Package->SetDirtyFlag(false);
        FModuleManager::LoadModuleChecked<FAssetRegistryModule>(
            TEXT("AssetRegistry"))
            .Get()
            .ScanModifiedAssetFiles({Record.Filename});
        return true;
    }

    static bool UnloadMap(
        FLevelMapRecord& Record,
        FString& OutError)
    {
        if (Record.World == nullptr)
        {
            OutError = TEXT(
                "Cannot unload a null Level Instance fixture World.");
            return false;
        }
        UPackage* Package = Record.World->GetOutermost();
        if (Record.bRegisteredInMemory)
        {
            FAssetRegistryModule::AssetDeleted(Record.World);
            Record.bRegisteredInMemory = false;
        }
        Record.World->DestroyWorld(false);
        PrepareLevelPackageForCollection(Package);
        Record.World = nullptr;
        CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);
        FModuleManager::LoadModuleChecked<FAssetRegistryModule>(
            TEXT("AssetRegistry"))
            .Get()
            .ScanModifiedAssetFiles({Record.Filename});
        if (!IsMapUnloaded(Record))
        {
            OutError = TEXT(
                "Saved Level Instance source fixture remained loaded: ")
                + Record.ObjectPath;
            return false;
        }
        return true;
    }

    static bool IsMapUnloaded(const FLevelMapRecord& Record)
    {
        return !Record.PackageName.IsEmpty()
            && !Record.ObjectPath.IsEmpty()
            && FindPackage(nullptr, *Record.PackageName) == nullptr
            && FindObject<UWorld>(nullptr, *Record.ObjectPath) == nullptr;
    }

    static bool ValidateOnDiskEvidence(
        const FLevelMapRecord& Record,
        const bool bExpectUnloaded,
        FString& OutError)
    {
        IAssetRegistry& Registry =
            FModuleManager::LoadModuleChecked<FAssetRegistryModule>(
                TEXT("AssetRegistry"))
                .Get();
        const FAssetData Data = Registry.GetAssetByObjectPath(
            FSoftObjectPath(Record.ObjectPath),
            true);
        if (!Data.IsValid()
            || Data.AssetClassPath
                != UWorld::StaticClass()->GetClassPathName())
        {
            OutError = TEXT(
                "Asset Registry lacks on-disk UWorld evidence for Level Instance fixture ")
                + Record.ObjectPath;
            return false;
        }
        if (bExpectUnloaded
            && (Data.FastGetAsset(false) != nullptr
                || !IsMapUnloaded(Record)))
        {
            OutError = TEXT(
                "On-disk Level Instance source fixture unexpectedly remained loaded: ")
                + Record.ObjectPath;
            return false;
        }
        return true;
    }

    bool HasLoadedSourceInstancePackage() const
    {
        TArray<FString> Prefixes;
        for (const FLevelMapRecord* Record : {&SourceA, &SourceB})
        {
            Prefixes.Add(
                TEXT("/Temp")
                + FPackageName::GetLongPackagePath(Record->PackageName)
                + TEXT("/")
                + FPackageName::GetShortName(Record->PackageName)
                + TEXT("_LevelInstance_"));
        }
        for (TObjectIterator<UPackage> It; It; ++It)
        {
            const FString PackageName = It->GetName();
            for (const FString& Prefix : Prefixes)
            {
                if (PackageName.StartsWith(Prefix))
                {
                    return true;
                }
            }
        }
        return false;
    }

    FString Token;
    FString RootPackagePath;
    bool bCleaned = false;
    UWorld* OriginalEditorWorld = nullptr;
    bool bEditorContextChanged = false;
    bool bNoLoadStateCaptured = false;
    ULevelInstanceSubsystem* LevelInstanceSubsystem = nullptr;
    TArray<ULevel*> LevelsBefore;
    TArray<FLevelInstanceLoadState> LoadStates;
};

struct FFixtureActorState
{
    TWeakObjectPtr<AActor> Actor;
    FGuid Guid;
    FString Label;
    FTransform Transform;
};

class FLevelReadInvariant
{
public:
    explicit FLevelReadInvariant(UWorld* InWorld)
        : World(InWorld)
        , GWorldBefore(GWorld.GetReference())
        , EditorWorldBefore(
            GEditor != nullptr
                ? GEditor->GetEditorWorldContext().World()
                : nullptr)
        , CurrentLevelBefore(
            InWorld != nullptr ? InWorld->GetCurrentLevel() : nullptr)
        , PackageDirtyBefore(
            InWorld != nullptr && InWorld->GetOutermost()->IsDirty())
        , SelectedActorCountBefore(
            GEditor != nullptr ? GEditor->GetSelectedActorCount() : -1)
        , SelectedComponentCountBefore(
            GEditor != nullptr ? GEditor->GetSelectedComponentCount() : -1)
        , UndoCountBefore(
            GEditor != nullptr && GEditor->Trans != nullptr
                ? GEditor->Trans->GetUndoCount()
                : -1)
        , QueueLengthBefore(
            GEditor != nullptr && GEditor->Trans != nullptr
                ? GEditor->Trans->GetQueueLength()
                : -1)
    {
        if (World != nullptr && World->PersistentLevel != nullptr)
        {
            for (AActor* Actor : World->PersistentLevel->Actors)
            {
                if (Actor == nullptr)
                {
                    continue;
                }
                FFixtureActorState& State = ActorStates.AddDefaulted_GetRef();
                State.Actor = Actor;
                State.Guid = Actor->GetActorGuid();
                State.Label = Actor->GetActorLabel();
                State.Transform = Actor->GetActorTransform();
            }
        }
        TransactionHandle = FCoreUObjectDelegates::OnObjectTransacted.AddLambda(
            [this](UObject*, const FTransactionObjectEvent&)
            {
                ++TransactionEventCount;
            });
    }

    ~FLevelReadInvariant()
    {
        FCoreUObjectDelegates::OnObjectTransacted.Remove(TransactionHandle);
    }

    bool Verify(FAutomationTestBase& Test) const
    {
        bool bOk = true;
        bOk &= Test.TestEqual(
            TEXT("Level Query preserves GWorld"),
            GWorld.GetReference(),
            GWorldBefore);
        if (GEditor != nullptr)
        {
            bOk &= Test.TestEqual(
                TEXT("Level Query preserves the Editor World context"),
                GEditor->GetEditorWorldContext().World(),
                EditorWorldBefore);
            bOk &= Test.TestEqual(
                TEXT("Level Query preserves Actor selection"),
                GEditor->GetSelectedActorCount(),
                SelectedActorCountBefore);
            bOk &= Test.TestEqual(
                TEXT("Level Query preserves Component selection"),
                GEditor->GetSelectedComponentCount(),
                SelectedComponentCountBefore);
            if (GEditor->Trans != nullptr)
            {
                bOk &= Test.TestEqual(
                    TEXT("Level Query creates no Undo entry"),
                    GEditor->Trans->GetUndoCount(),
                    UndoCountBefore);
                bOk &= Test.TestEqual(
                    TEXT("Level Query preserves the transaction queue"),
                    GEditor->Trans->GetQueueLength(),
                    QueueLengthBefore);
            }
        }
        bOk &= Test.TestEqual(
            TEXT("Level Query emits no UObject transaction"),
            TransactionEventCount,
            0);
        if (World != nullptr)
        {
            bOk &= Test.TestEqual(
                TEXT("Level Query preserves the World's current Level"),
                World->GetCurrentLevel(),
                CurrentLevelBefore);
            bOk &= Test.TestEqual(
                TEXT("Level Query preserves the map package dirty state"),
                World->GetOutermost()->IsDirty(),
                PackageDirtyBefore);
        }
        for (const FFixtureActorState& State : ActorStates)
        {
            AActor* Actor = State.Actor.Get();
            bOk &= Test.TestNotNull(
                TEXT("Level Query preserves each source Actor"),
                Actor);
            if (Actor != nullptr)
            {
                bOk &= Test.TestEqual(
                    TEXT("Level Query preserves ActorGuid"),
                    Actor->GetActorGuid(),
                    State.Guid);
                bOk &= Test.TestEqual(
                    TEXT("Level Query preserves ActorLabel"),
                    Actor->GetActorLabel(),
                    State.Label);
                bOk &= Test.TestTrue(
                    TEXT("Level Query preserves Actor transform"),
                    Actor->GetActorTransform().Equals(State.Transform));
            }
        }
        return bOk;
    }

private:
    UWorld* World = nullptr;
    UWorld* GWorldBefore = nullptr;
    UWorld* EditorWorldBefore = nullptr;
    ULevel* CurrentLevelBefore = nullptr;
    bool PackageDirtyBefore = false;
    int32 SelectedActorCountBefore = -1;
    int32 SelectedComponentCountBefore = -1;
    int32 UndoCountBefore = -1;
    int32 QueueLengthBefore = -1;
    TArray<FFixtureActorState> ActorStates;
    FDelegateHandle TransactionHandle;
    int32 TransactionEventCount = 0;
};

struct FFixtureComponentState
{
    TWeakObjectPtr<UActorComponent> Component;
    FName Name;
    TWeakObjectPtr<UObject> Outer;
    TWeakObjectPtr<AActor> Owner;
    EComponentCreationMethod CreationMethod =
        EComponentCreationMethod::Native;
    bool bRegistered = false;
    bool bHasBeenCreated = false;
};

class FLevelComponentReadInvariant
{
public:
    FLevelComponentReadInvariant(
        AActor* InActor,
        UPackage* InBlueprintPackage)
        : Actor(InActor)
        , BlueprintPackage(InBlueprintPackage)
        , ActorPackageDirtyBefore(
            InActor != nullptr
                && InActor->GetOutermost()->IsDirty())
        , BlueprintPackageDirtyBefore(
            InBlueprintPackage != nullptr
                && InBlueprintPackage->IsDirty())
    {
        if (Actor == nullptr)
        {
            return;
        }
        TArray<UActorComponent*> Components;
        Actor->GetComponents(Components, false);
        for (UActorComponent* Component : Components)
        {
            if (Component == nullptr)
            {
                continue;
            }
            FFixtureComponentState& State =
                ComponentStates.AddDefaulted_GetRef();
            State.Component = Component;
            State.Name = Component->GetFName();
            State.Outer = Component->GetOuter();
            State.Owner = Component->GetOwner();
            State.CreationMethod = Component->CreationMethod;
            State.bRegistered = Component->IsRegistered();
            State.bHasBeenCreated = Component->HasBeenCreated();
        }
        for (UActorComponent* Component : Actor->GetInstanceComponents())
        {
            InstanceComponentsBefore.Add(Component);
        }
        for (UActorComponent* Component : Actor->BlueprintCreatedComponents)
        {
            BlueprintCreatedComponentsBefore.Add(Component);
        }
    }

    bool Verify(FAutomationTestBase& Test) const
    {
        bool bOk = true;
        bOk &= Test.TestNotNull(
            TEXT("Component Query preserves its owner Actor"),
            Actor);
        if (Actor == nullptr)
        {
            return false;
        }

        TArray<UActorComponent*> ComponentsAfter;
        Actor->GetComponents(ComponentsAfter, false);
        bOk &= Test.TestEqual(
            TEXT("Component Query preserves the exact owned Component count"),
            ComponentsAfter.Num(),
            ComponentStates.Num());
        for (const FFixtureComponentState& State : ComponentStates)
        {
            UActorComponent* Component = State.Component.Get();
            bOk &= Test.TestNotNull(
                TEXT("Component Query preserves each Component incarnation"),
                Component);
            if (Component == nullptr)
            {
                continue;
            }
            bOk &= Test.TestTrue(
                TEXT("Component Query preserves owned Component membership"),
                ComponentsAfter.Contains(Component));
            bOk &= Test.TestEqual(
                TEXT("Component Query preserves Component FName"),
                Component->GetFName(),
                State.Name);
            bOk &= Test.TestEqual(
                TEXT("Component Query preserves Component Outer"),
                Component->GetOuter(),
                State.Outer.Get());
            bOk &= Test.TestEqual(
                TEXT("Component Query preserves Component owner"),
                Component->GetOwner(),
                State.Owner.Get());
            bOk &= Test.TestTrue(
                TEXT("Component Query preserves CreationMethod"),
                Component->CreationMethod == State.CreationMethod);
            bOk &= Test.TestEqual(
                TEXT("Component Query preserves registration state"),
                Component->IsRegistered(),
                State.bRegistered);
            bOk &= Test.TestEqual(
                TEXT("Component Query preserves creation lifecycle state"),
                Component->HasBeenCreated(),
                State.bHasBeenCreated);
        }

        const TArray<UActorComponent*>& InstanceComponentsAfter =
            Actor->GetInstanceComponents();
        bOk &= Test.TestEqual(
            TEXT("Component Query preserves InstanceComponents count"),
            InstanceComponentsAfter.Num(),
            InstanceComponentsBefore.Num());
        for (int32 Index = 0;
             Index < FMath::Min(
                 InstanceComponentsAfter.Num(),
                 InstanceComponentsBefore.Num());
             ++Index)
        {
            bOk &= Test.TestEqual(
                TEXT("Component Query preserves InstanceComponents order"),
                InstanceComponentsAfter[Index],
                InstanceComponentsBefore[Index].Get());
        }

        bOk &= Test.TestEqual(
            TEXT("Component Query preserves BlueprintCreatedComponents count"),
            Actor->BlueprintCreatedComponents.Num(),
            BlueprintCreatedComponentsBefore.Num());
        for (int32 Index = 0;
             Index < FMath::Min(
                 Actor->BlueprintCreatedComponents.Num(),
                 BlueprintCreatedComponentsBefore.Num());
             ++Index)
        {
            bOk &= Test.TestEqual(
                TEXT("Component Query preserves Blueprint-created Component order"),
                Actor->BlueprintCreatedComponents[Index].Get(),
                BlueprintCreatedComponentsBefore[Index].Get());
        }

        bOk &= Test.TestEqual(
            TEXT("Component Query preserves the map package dirty state"),
            Actor->GetOutermost()->IsDirty(),
            ActorPackageDirtyBefore);
        if (BlueprintPackage != nullptr)
        {
            bOk &= Test.TestEqual(
                TEXT("Component Query preserves the declaring Blueprint package dirty state"),
                BlueprintPackage->IsDirty(),
                BlueprintPackageDirtyBefore);
        }
        return bOk;
    }

private:
    AActor* Actor = nullptr;
    UPackage* BlueprintPackage = nullptr;
    bool ActorPackageDirtyBefore = false;
    bool BlueprintPackageDirtyBefore = false;
    TArray<FFixtureComponentState> ComponentStates;
    TArray<TWeakObjectPtr<UActorComponent>> InstanceComponentsBefore;
    TArray<TWeakObjectPtr<UActorComponent>>
        BlueprintCreatedComponentsBefore;
};

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FSalLevelNoLoadTargetTest,
    "Loomle.Sal.Level.Query.NoLoadCanonicalTarget",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSalLevelNoLoadTargetTest::RunTest(const FString& Parameters)
{
    FScopedLevelQueryFixture Fixture;
    FString Error;
    if (!TestTrue(TEXT("Level no-load fixture builds"), Fixture.Build(Error)))
    {
        AddError(Error);
        return false;
    }
    TestTrue(
        TEXT("No-load fixture starts with no UPackage or UWorld"),
        Fixture.IsUnloadedMapStillUnloaded());

    UWorld* SourceWorld = Fixture.Loaded.World;
    ULevel* SourceLevel = SourceWorld != nullptr
        ? SourceWorld->PersistentLevel
        : nullptr;
    UWorld* ActiveEditorWorld = GEditor != nullptr
        ? GEditor->GetEditorWorldContext().World()
        : nullptr;
    if (!TestNotNull(TEXT("Inactive source fixture has a PersistentLevel"), SourceLevel)
        || !TestNotNull(TEXT("Streamed source fixture has an active Editor World"), ActiveEditorWorld))
    {
        return false;
    }
    const EWorldType::Type OriginalSourceType = SourceWorld->WorldType;
    UWorld* OriginalOwningWorld = SourceLevel->OwningWorld;
    SourceWorld->WorldType = EWorldType::Inactive;
    SourceLevel->OwningWorld = SourceWorld;

    const TSharedRef<FJsonObject> InactiveTarget =
        LevelTarget(Fixture.Loaded.ObjectPath);
    const TSharedPtr<FJsonObject> InactiveTargetResult =
        FSalModule::BuildQueryResult(
            LevelQueryArguments(
                InactiveTarget,
                LevelOperation(TEXT("target"))));
    const TSharedPtr<FJsonObject> InactiveTargetFields =
        FirstLevelAssetFields(InactiveTargetResult);
    bool bInactiveLoaded = true;
    TestTrue(
        TEXT("A saved inactive Editor World canonicalizes without becoming Level content authority"),
        !LevelHasError(InactiveTargetResult)
            && InactiveTargetFields.IsValid()
            && InactiveTargetFields->TryGetBoolField(
                TEXT("loaded"),
                bInactiveLoaded)
            && !bInactiveLoaded);
    const TSharedPtr<FJsonObject> InactiveSummary =
        FSalModule::BuildQueryResult(
            LevelQueryArguments(
                InactiveTarget,
                LevelOperation(TEXT("summary"))));
    TestTrue(
        TEXT("An inactive Editor World cannot serve Level content Query"),
        LevelHasDiagnostic(
            InactiveSummary,
            TEXT("capability.level_not_loaded")));

    SourceLevel->OwningWorld = ActiveEditorWorld;
    const TSharedPtr<FJsonObject> StreamedSummary =
        FSalModule::BuildQueryResult(
            LevelQueryArguments(
                InactiveTarget,
                LevelOperation(TEXT("summary"))));
    const bool bStreamedShapePreserved =
        SourceWorld->WorldType == EWorldType::Inactive
        && SourceLevel->OwningWorld == ActiveEditorWorld;
    SourceLevel->OwningWorld = OriginalOwningWorld;
    SourceWorld->WorldType = OriginalSourceType;
    TestTrue(
        TEXT("An inactive exact source World is readable when its PersistentLevel belongs to the active Editor World"),
        !LevelHasError(StreamedSummary)
            && bStreamedShapePreserved
            && FirstLevelAssetFields(StreamedSummary).IsValid());

    const TSharedRef<FJsonObject> Target =
        LevelTarget(Fixture.Unloaded.ObjectPath);
    const TSharedPtr<FJsonObject> TargetResult =
        FSalModule::BuildQueryResult(
            LevelQueryArguments(Target, LevelOperation(TEXT("target"))));
    TestTrue(
        TEXT("Level target canonicalizes from on-disk Asset Registry evidence"),
        !LevelHasError(TargetResult)
            && LevelHasTargetContext(TargetResult, TEXT("exact_target"))
            && HasCanonicalLevelTarget(
                TargetResult,
                Fixture.Unloaded.ObjectPath));
    const TSharedPtr<FJsonObject> TargetFields =
        FirstLevelAssetFields(TargetResult);
    bool bLoaded = true;
    FString Path;
    TestTrue(
        TEXT("No-load target truthfully reports the canonical unloaded map"),
        TargetFields.IsValid()
            && TargetFields->TryGetStringField(TEXT("path"), Path)
            && Path == Fixture.Unloaded.ObjectPath
            && TargetFields->TryGetBoolField(TEXT("loaded"), bLoaded)
            && !bLoaded);
    TestTrue(
        TEXT("Canonical target Query does not load the map"),
        Fixture.IsUnloadedMapStillUnloaded());

    for (const FString& Operation : {TEXT("summary"), TEXT("actors")})
    {
        const TSharedPtr<FJsonObject> Result =
            FSalModule::BuildQueryResult(
                LevelQueryArguments(Target, LevelOperation(Operation)));
        TestTrue(
            *FString::Printf(
                TEXT("Unloaded Level %s fails with exact loading-state context"),
                *Operation),
            LevelHasDiagnostic(
                Result,
                TEXT("capability.level_not_loaded"))
                && LevelHasTargetContext(Result, TEXT("exact_target"))
                && HasCanonicalLevelTarget(
                    Result,
                    Fixture.Unloaded.ObjectPath));
        TestTrue(
            *FString::Printf(
                TEXT("Unloaded Level %s does not load the map"),
                *Operation),
            Fixture.IsUnloadedMapStillUnloaded());
    }

    const TSharedPtr<FJsonObject> Exact =
        FSalModule::BuildQueryResult(
            LevelQueryArguments(
                Target,
                LevelExactOperation(LevelGuidText(FGuid::NewGuid()))));
    TestTrue(
        TEXT("StableRef resolution on an unloaded exact target reports level_not_loaded"),
        LevelHasDiagnostic(
            Exact,
            TEXT("capability.level_not_loaded"))
            && LevelHasTargetContext(Exact, TEXT("exact_target"))
            && Fixture.IsUnloadedMapStillUnloaded());

    if (!Fixture.Cleanup(Error))
    {
        AddError(Error);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FSalLevelLoadedActorQueryTest,
    "Loomle.Sal.Level.Query.LoadedActorsSummaryStableRefSearchAndPage",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSalLevelLoadedActorQueryTest::RunTest(const FString& Parameters)
{
    FScopedLevelQueryFixture Fixture;
    FString Error;
    if (!TestTrue(TEXT("Loaded Level fixture builds"), Fixture.Build(Error)))
    {
        AddError(Error);
        return false;
    }
    const TSharedRef<FJsonObject> Target =
        LevelTarget(
            Fixture.Loaded.ObjectPath,
            UWorld::StaticClass()->GetPathName());
    if (!TestTrue(
            TEXT("Loaded Level fixture binds to the Editor World context"),
            Fixture.Activate(Fixture.Loaded, Error)))
    {
        AddError(Error);
        return false;
    }
    FLevelReadInvariant Invariant(Fixture.Loaded.World);

    const TSharedPtr<FJsonObject> TargetResult =
        FSalModule::BuildQueryResult(
            LevelQueryArguments(Target, LevelOperation(TEXT("target"))));
    const TSharedPtr<FJsonObject> TargetFields =
        FirstLevelAssetFields(TargetResult);
    bool bLoaded = false;
    FString WorldType;
    TestTrue(
        TEXT("Loaded Level target reports the exact Editor source World"),
        !LevelHasError(TargetResult)
            && HasCanonicalLevelTarget(
                TargetResult,
                Fixture.Loaded.ObjectPath)
            && TargetFields.IsValid()
            && TargetFields->TryGetBoolField(TEXT("loaded"), bLoaded)
            && bLoaded
            && TargetFields->TryGetStringField(TEXT("worldType"), WorldType)
            && WorldType == TEXT("Editor"));

    const TSharedPtr<FJsonObject> Summary =
        FSalModule::BuildQueryResult(
            LevelQueryArguments(Target, LevelOperation(TEXT("summary"))));
    const TSharedPtr<FJsonObject> SummaryFields =
        FirstLevelAssetFields(Summary);
    double ActorCount = 0.0;
    double LoadedActorCount = 0.0;
    double UnloadedDescriptorCount = -1.0;
    double StableActorCount = 0.0;
    bool bIdentityComplete = false;
    TestTrue(
        TEXT("Level summary reports a complete non-WP loaded Actor identity set"),
        !LevelHasError(Summary)
            && SummaryFields.IsValid()
            && SummaryFields->TryGetNumberField(TEXT("actorCount"), ActorCount)
            && ActorCount >= 3.0
            && SummaryFields->TryGetNumberField(
                TEXT("loadedActorCount"),
                LoadedActorCount)
            && LoadedActorCount == ActorCount
            && SummaryFields->TryGetNumberField(
                TEXT("unloadedDescriptorCount"),
                UnloadedDescriptorCount)
            && UnloadedDescriptorCount == 0.0
            && SummaryFields->TryGetNumberField(
                TEXT("stableActorCount"),
                StableActorCount)
            && StableActorCount == ActorCount
            && SummaryFields->TryGetBoolField(
                TEXT("identityComplete"),
                bIdentityComplete)
            && bIdentityComplete);

    const TSharedPtr<FJsonObject> ActorsResult =
        FSalModule::BuildQueryResult(
            LevelQueryArguments(Target, LevelOperation(TEXT("actors"))));
    const TArray<TSharedPtr<FJsonObject>> Actors =
        LevelTaggedObjectFields(ActorsResult, TEXT("actor"));
    const FString AlphaId = LevelGuidText(Fixture.AlphaId);
    const FString BetaId = LevelGuidText(Fixture.BetaId);
    const FString GammaId = LevelGuidText(Fixture.GammaId);
    TestTrue(
        TEXT("Loaded Level actors returns every authored fixture Actor"),
        !LevelHasError(ActorsResult)
            && FindLevelFieldsById(Actors, AlphaId).IsValid()
            && FindLevelFieldsById(Actors, BetaId).IsValid()
            && FindLevelFieldsById(Actors, GammaId).IsValid());
    TestTrue(
        TEXT("Level actors excludes instance projections, external-content Actors, and PCG-generated output"),
        !FindLevelFieldsById(
            Actors,
            LevelGuidText(Fixture.InstanceProjectionId)).IsValid()
            && !FindLevelFieldsById(
                Actors,
                LevelGuidText(Fixture.ExternalContentId)).IsValid()
            && !FindLevelFieldsById(
                Actors,
                LevelGuidText(Fixture.PcgGeneratedId)).IsValid());

    const TSharedPtr<FJsonObject> ExactResult =
        FSalModule::BuildQueryResult(
            LevelQueryArguments(Target, LevelExactOperation(BetaId)));
    const TSharedPtr<FJsonObject> ExactFields =
        FindLevelFieldsById(
            LevelTaggedObjectFields(ExactResult, TEXT("actor")),
            BetaId);
    FString ExactLabel;
    bool bExactLoaded = false;
    bool bStableRefAvailable = false;
    TestTrue(
        TEXT("ActorGuid StableRef resolves the exact loaded Actor"),
        !LevelHasError(ExactResult)
            && ExactFields.IsValid()
            && ExactFields->TryGetStringField(
                TEXT("ActorLabel"),
                ExactLabel)
            && ExactLabel == TEXT("Loomle Level Beta")
            && ExactFields->TryGetBoolField(
                TEXT("loaded"),
                bExactLoaded)
            && bExactLoaded
            && ExactFields->TryGetBoolField(
                TEXT("stableRefAvailable"),
                bStableRefAvailable)
            && bStableRefAvailable);

    for (const FString& NonCanonicalId : {
             BetaId.ToUpper(),
             Fixture.BetaId.ToString(EGuidFormats::Digits)})
    {
        const TSharedPtr<FJsonObject> NonCanonicalExact =
            FSalModule::BuildQueryResult(
                LevelQueryArguments(
                    Target,
                    LevelExactOperation(NonCanonicalId)));
        TestTrue(
            TEXT("Actor StableRef rejects non-canonical ActorGuid text"),
            LevelHasDiagnostic(
                NonCanonicalExact,
                TEXT("validation.invalid_reference")));
    }

    const TSharedPtr<FJsonObject> SearchByLabel =
        FSalModule::BuildQueryResult(
            LevelQueryArguments(
                Target,
                LevelOperation(
                    TEXT("actors"),
                    TEXT("Loomle Level Alpha"))));
    const TArray<TSharedPtr<FJsonObject>> SearchActors =
        LevelTaggedObjectFields(SearchByLabel, TEXT("actor"));
    TestTrue(
        TEXT("Actor search matches the current ActorLabel without using it as identity"),
        !LevelHasError(SearchByLabel)
            && SearchActors.Num() == 1
            && FindLevelFieldsById(SearchActors, AlphaId).IsValid());

    TArray<FString> FullOrder;
    for (const TSharedPtr<FJsonObject>& Fields : Actors)
    {
        FString Id;
        if (Fields.IsValid() && Fields->TryGetStringField(TEXT("id"), Id))
        {
            FullOrder.Add(Id);
        }
    }
    TArray<FString> PagedOrder;
    FString Cursor;
    for (int32 PageIndex = 0; PageIndex < 64; ++PageIndex)
    {
        const TSharedPtr<FJsonObject> Page =
            FSalModule::BuildQueryResult(
                LevelQueryArguments(
                    Target,
                    LevelOperation(TEXT("actors")),
                    1,
                    Cursor));
        if (LevelHasError(Page))
        {
            AddError(TEXT("A deterministic Level Actor continuation page failed."));
            break;
        }
        const TArray<TSharedPtr<FJsonObject>> PageActors =
            LevelTaggedObjectFields(Page, TEXT("actor"));
        if (PageActors.Num() != 1)
        {
            AddError(TEXT("A non-final Level Actor page did not contain exactly one Actor."));
            break;
        }
        FString Id;
        if (!PageActors[0]->TryGetStringField(TEXT("id"), Id))
        {
            AddError(TEXT("A Level Actor page omitted ActorGuid identity."));
            break;
        }
        PagedOrder.Add(Id);
        FString Next;
        if (!ReadLevelNextCursor(Page, Next))
        {
            break;
        }
        Cursor = Next;
    }
    TestEqual(
        TEXT("Cursor pagination reproduces the deterministic full Actor order"),
        PagedOrder,
        FullOrder);

    const TSharedPtr<FJsonObject> FirstSearchPage =
        FSalModule::BuildQueryResult(
            LevelQueryArguments(
                Target,
                LevelOperation(TEXT("actors")),
                1));
    FString SearchBoundCursor;
    if (ReadLevelNextCursor(FirstSearchPage, SearchBoundCursor))
    {
        const TSharedPtr<FJsonObject> ReusedForOtherSearch =
            FSalModule::BuildQueryResult(
                LevelQueryArguments(
                    Target,
                    LevelOperation(TEXT("actors"), TEXT("Alpha")),
                    1,
                    SearchBoundCursor));
        TestTrue(
            TEXT("Actor cursor is bound to its search and snapshot"),
            LevelHasDiagnostic(
                ReusedForOtherSearch,
                TEXT("validation.invalid_cursor")));

        const FString OriginalLabel = Fixture.Alpha->GetActorLabel();
        const bool bOriginalDirty =
            Fixture.Loaded.World->GetOutermost()->IsDirty();
        Fixture.Alpha->SetActorLabel(
            TEXT("Loomle Level Alpha Changed"),
            false);
        const TSharedPtr<FJsonObject> ReusedAfterSnapshotChange =
            FSalModule::BuildQueryResult(
                LevelQueryArguments(
                    Target,
                    LevelOperation(TEXT("actors")),
                    1,
                    SearchBoundCursor));
        TestTrue(
            TEXT("Actor cursor is invalid after any emitted snapshot field changes"),
            LevelHasDiagnostic(
                ReusedAfterSnapshotChange,
                TEXT("validation.invalid_cursor")));
        Fixture.Alpha->SetActorLabel(OriginalLabel, false);
        Fixture.Loaded.World->GetOutermost()->SetDirtyFlag(
            bOriginalDirty);
    }

    const FString OriginalGammaLabel = Fixture.Gamma->GetActorLabel(false);
    const bool bDirtyBeforeLabelRead =
        Fixture.Loaded.World->GetOutermost()->IsDirty();
    Fixture.Gamma->ClearActorLabel();
    int32 ActorLabelChangedEvents = 0;
    const FDelegateHandle LabelChangedHandle =
        FCoreDelegates::OnActorLabelChanged.AddLambda(
            [&ActorLabelChangedEvents](AActor*)
            {
                ++ActorLabelChangedEvents;
            });
    const TSharedPtr<FJsonObject> UnlabeledExact =
        FSalModule::BuildQueryResult(
            LevelQueryArguments(Target, LevelExactOperation(GammaId)));
    const TSharedPtr<FJsonObject> UnlabeledFields =
        FindLevelFieldsById(
            LevelTaggedObjectFields(UnlabeledExact, TEXT("actor")),
            GammaId);
    const bool bLabelRemainedEmpty =
        Fixture.Gamma->GetActorLabel(false).IsEmpty();
    FCoreDelegates::OnActorLabelChanged.Remove(LabelChangedHandle);
    Fixture.Gamma->SetActorLabel(OriginalGammaLabel, false);
    Fixture.Loaded.World->GetOutermost()->SetDirtyFlag(
        bDirtyBeforeLabelRead);
    TestTrue(
        TEXT("Exact Actor Query observes an absent ActorLabel without creating one"),
        !LevelHasError(UnlabeledExact)
            && UnlabeledFields.IsValid()
            && !UnlabeledFields->HasField(TEXT("ActorLabel"))
            && bLabelRemainedEmpty
            && ActorLabelChangedEvents == 0);

    TestTrue(
        TEXT("All loaded Level reads preserve Editor and authored state"),
        Invariant.Verify(*this));

    Fixture.Loaded.World->GetOutermost()->SetDirtyFlag(true);
    const TSharedPtr<FJsonObject> DirtySummary =
        FSalModule::BuildQueryResult(
            LevelQueryArguments(Target, LevelOperation(TEXT("summary"))));
    TestTrue(
        TEXT("Read Query preserves a pre-existing dirty map package"),
        !LevelHasError(DirtySummary)
            && Fixture.Loaded.World->GetOutermost()->IsDirty());
    Fixture.Loaded.World->GetOutermost()->SetDirtyFlag(false);

    if (!Fixture.Cleanup(Error))
    {
        AddError(Error);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FSalLevelActorGuidCorruptionTest,
    "Loomle.Sal.Level.Query.InvalidAndDuplicateActorGuid",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSalLevelActorGuidCorruptionTest::RunTest(const FString& Parameters)
{
    FScopedLevelQueryFixture Fixture;
    FString Error;
    if (!TestTrue(TEXT("Corrupt Level fixture builds"), Fixture.Build(Error)))
    {
        AddError(Error);
        return false;
    }
    const TSharedRef<FJsonObject> Target =
        LevelTarget(
            Fixture.Corrupt.ObjectPath,
            UWorld::StaticClass()->GetPathName());
    if (!TestTrue(
            TEXT("Corrupt Level fixture binds to the Editor World context"),
            Fixture.Activate(Fixture.Corrupt, Error)))
    {
        AddError(Error);
        return false;
    }
    FLevelReadInvariant Invariant(Fixture.Corrupt.World);

    const TSharedPtr<FJsonObject> Summary =
        FSalModule::BuildQueryResult(
            LevelQueryArguments(Target, LevelOperation(TEXT("summary"))));
    const TSharedPtr<FJsonObject> SummaryFields =
        FirstLevelAssetFields(Summary);
    double InvalidCount = 0.0;
    double DuplicateCount = 0.0;
    TestTrue(
        TEXT("Summary exposes bounded invalid and duplicate ActorGuid corruption evidence"),
        !LevelHasError(Summary)
            && LevelHasDiagnostic(
                Summary,
                TEXT("resolution.identity_conflict"))
            && SummaryFields.IsValid()
            && SummaryFields->TryGetNumberField(
                TEXT("invalidActorGuidCount"),
                InvalidCount)
            && InvalidCount == 1.0
            && SummaryFields->TryGetNumberField(
                TEXT("duplicateActorGuidCount"),
                DuplicateCount)
            && DuplicateCount == 1.0);

    const TSharedPtr<FJsonObject> ActorsResult =
        FSalModule::BuildQueryResult(
            LevelQueryArguments(Target, LevelOperation(TEXT("actors"))));
    const TArray<TSharedPtr<FJsonObject>> Actors =
        LevelTaggedObjectFields(ActorsResult, TEXT("actor"));
    const FString InvalidId = LevelGuidText(FGuid());
    const FString DuplicateId = LevelGuidText(Fixture.DuplicateId);
    const TSharedPtr<FJsonObject> InvalidFields =
        FindLevelFieldsById(Actors, InvalidId);
    bool bIdentityValid = true;
    bool bStableRefAvailable = true;
    TestTrue(
        TEXT("Invalid ActorGuid remains readable but receives no StableRef"),
        !LevelHasError(ActorsResult)
            && InvalidFields.IsValid()
            && InvalidFields->TryGetBoolField(
                TEXT("identityValid"),
                bIdentityValid)
            && !bIdentityValid
            && InvalidFields->TryGetBoolField(
                TEXT("stableRefAvailable"),
                bStableRefAvailable)
            && !bStableRefAvailable);
    TestEqual(
        TEXT("Duplicate ActorGuid publishes both corruption records"),
        CountLevelFieldsById(Actors, DuplicateId),
        2);
    for (const TSharedPtr<FJsonObject>& Fields : Actors)
    {
        FString Id;
        if (!Fields.IsValid()
            || !Fields->TryGetStringField(TEXT("id"), Id)
            || Id != DuplicateId)
        {
            continue;
        }
        bool bUnique = true;
        bool bAvailable = true;
        TestTrue(
            TEXT("Each duplicate record denies unique StableRef identity"),
            Fields->TryGetBoolField(TEXT("identityUnique"), bUnique)
                && !bUnique
                && Fields->TryGetBoolField(
                    TEXT("stableRefAvailable"),
                    bAvailable)
                && !bAvailable);
    }

    const TSharedPtr<FJsonObject> DuplicateExact =
        FSalModule::BuildQueryResult(
            LevelQueryArguments(
                Target,
                LevelExactOperation(DuplicateId)));
    TestTrue(
        TEXT("Duplicate ActorGuid StableRef resolution fails closed"),
        LevelHasDiagnostic(
            DuplicateExact,
            TEXT("resolution.identity_conflict")));

    const TSharedPtr<FJsonObject> InvalidExact =
        FSalModule::BuildQueryResult(
            LevelQueryArguments(
                Target,
                LevelExactOperation(InvalidId)));
    TestTrue(
        TEXT("Invalid ActorGuid text cannot become a StableRef"),
        LevelHasDiagnostic(
            InvalidExact,
            TEXT("validation.invalid_reference")));
    TestTrue(
        TEXT("Corruption reads preserve the corrupt source evidence exactly"),
        Invariant.Verify(*this));

    if (!Fixture.Cleanup(Error))
    {
        AddError(Error);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FSalLevelInstanceSourceOwnershipTest,
    "Loomle.Sal.Level.Query.LevelInstanceSourceOwnershipNoLoadAndDedup",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSalLevelInstanceSourceOwnershipTest::RunTest(
    const FString& Parameters)
{
    FScopedLevelInstanceQueryFixture Fixture;
    FString Error;
    if (!TestTrue(
            TEXT("Level Instance source-ownership fixture builds"),
            Fixture.Build(Error)))
    {
        AddError(Error);
        return false;
    }
    if (!TestTrue(
            TEXT("Containing Level Instance fixture becomes the active Editor World"),
            Fixture.Activate(Error)))
    {
        AddError(Error);
        return false;
    }
    if (!TestTrue(
            TEXT("Level Instance fixture captures an unloaded-source baseline"),
            Fixture.CaptureNoLoadState(Error)))
    {
        AddError(Error);
        return false;
    }
    FLevelReadInvariant Invariant(Fixture.Containing.World);

    const TSharedRef<FJsonObject> Target = LevelTarget(
        Fixture.Containing.ObjectPath,
        UWorld::StaticClass()->GetPathName());
    const FString SharedOneId = LevelGuidText(Fixture.SharedOneId);
    const FString SharedTwoId = LevelGuidText(Fixture.SharedTwoId);
    const FString OtherSourceId = LevelGuidText(Fixture.OtherSourceId);
    const FString OrdinaryId = LevelGuidText(Fixture.OrdinaryId);
    const FString MissingSourceId = LevelGuidText(
        Fixture.MissingSourceId);

    const TSharedPtr<FJsonObject> TargetOnlyResult =
        FSalModule::BuildQueryResult(
            LevelQueryArguments(
                Target,
                LevelOperation(TEXT("target"))));
    const TSharedPtr<FJsonObject> SummaryResult =
        FSalModule::BuildQueryResult(
            LevelQueryArguments(
                Target,
                LevelOperation(TEXT("summary"))));
    TestTrue(
        TEXT("Level target and summary reads never enumerate placement source Targets"),
        !LevelHasError(TargetOnlyResult)
            && !LevelHasError(SummaryResult)
            && HasNoLevelRelatedContext(TargetOnlyResult)
            && HasNoLevelRelatedContext(SummaryResult));

    const TSharedPtr<FJsonObject> SharedResult =
        FSalModule::BuildQueryResult(
            LevelQueryArguments(
                Target,
                LevelOperation(
                    TEXT("actors"),
                    TEXT("Shared Source"))));
    const TArray<TSharedPtr<FJsonObject>> SharedActors =
        LevelTaggedObjectFields(SharedResult, TEXT("actor"));
    const TSharedPtr<FJsonObject> SharedOneFields =
        FindLevelFieldsById(SharedActors, SharedOneId);
    const TSharedPtr<FJsonObject> SharedTwoFields =
        FindLevelFieldsById(SharedActors, SharedTwoId);
    FString SharedRelatedAlias;
    FString SharedRelatedAsset;
    FString SharedOneSourceAlias;
    FString SharedTwoSourceAlias;
    bool bSharedOneLevelInstance = false;
    bool bSharedTwoLevelInstance = false;
    TestTrue(
        TEXT("Two placements of one source remain two containing-Level Actors"),
        !LevelHasError(SharedResult)
            && LevelHasTargetContext(
                SharedResult,
                TEXT("exact_target"))
            && HasCanonicalLevelTarget(
                SharedResult,
                Fixture.Containing.ObjectPath)
            && SharedActors.Num() == 2
            && SharedOneFields.IsValid()
            && SharedTwoFields.IsValid()
            && SharedOneFields->TryGetBoolField(
                TEXT("levelInstance"),
                bSharedOneLevelInstance)
            && bSharedOneLevelInstance
            && SharedTwoFields->TryGetBoolField(
                TEXT("levelInstance"),
                bSharedTwoLevelInstance)
            && bSharedTwoLevelInstance);
    TestTrue(
        TEXT("Same-source placements share one canonical related Level and handoff"),
        ReadSingleRelatedLevelTarget(
            SharedResult,
            SharedRelatedAlias,
            SharedRelatedAsset)
            && SharedRelatedAsset == Fixture.SourceA.ObjectPath
            && HasSingleLevelHandoff(
                SharedResult,
                TEXT("inspect_source_level"),
                SharedRelatedAlias)
            && ReadLevelLocalField(
                SharedOneFields,
                TEXT("sourceLevel"),
                SharedOneSourceAlias)
            && ReadLevelLocalField(
                SharedTwoFields,
                TEXT("sourceLevel"),
                SharedTwoSourceAlias)
            && SharedOneSourceAlias == SharedRelatedAlias
            && SharedTwoSourceAlias == SharedRelatedAlias);
    TestTrue(
        TEXT("Level Instance source availability is represented only by sourceLevel presence"),
        SharedOneFields.IsValid()
            && SharedTwoFields.IsValid()
            && !SharedOneFields->HasField(
                TEXT("sourceLevelAvailable"))
            && !SharedTwoFields->HasField(
                TEXT("sourceLevelAvailable")));

    const TSharedPtr<FJsonObject> AllResolvedResult =
        FSalModule::BuildQueryResult(
            LevelQueryArguments(
                Target,
                LevelOperation(
                    TEXT("actors"),
                    TEXT("Loomle Resolved Instance"))));
    const TArray<TSharedPtr<FJsonObject>> AllResolvedActors =
        LevelTaggedObjectFields(AllResolvedResult, TEXT("actor"));
    const TSharedPtr<FJsonObject> AllSharedOneFields =
        FindLevelFieldsById(AllResolvedActors, SharedOneId);
    const TSharedPtr<FJsonObject> AllSharedTwoFields =
        FindLevelFieldsById(AllResolvedActors, SharedTwoId);
    const TSharedPtr<FJsonObject> AllOtherSourceFields =
        FindLevelFieldsById(AllResolvedActors, OtherSourceId);
    TMap<FString, FString> FullAliasesByAsset;
    TSet<FString> FullHandoffAliases;
    const bool bFullTargetsValid = ReadRelatedLevelTargets(
        AllResolvedResult,
        FullAliasesByAsset);
    const bool bFullHandoffsValid = ReadLevelHandoffAliases(
        AllResolvedResult,
        TEXT("inspect_source_level"),
        FullHandoffAliases);
    const FString SourceAAlias = FullAliasesByAsset.FindRef(
        Fixture.SourceA.ObjectPath);
    const FString SourceBAlias = FullAliasesByAsset.FindRef(
        Fixture.SourceB.ObjectPath);
    FString AllSharedOneSourceAlias;
    FString AllSharedTwoSourceAlias;
    FString AllOtherSourceAlias;
    TestTrue(
        TEXT("Full Actor collection deduplicates each distinct source and maps every placement to it"),
        !LevelHasError(AllResolvedResult)
            && AllResolvedActors.Num() == 3
            && AllSharedOneFields.IsValid()
            && AllSharedTwoFields.IsValid()
            && AllOtherSourceFields.IsValid()
            && bFullTargetsValid
            && FullAliasesByAsset.Num() == 2
            && !SourceAAlias.IsEmpty()
            && !SourceBAlias.IsEmpty()
            && SourceAAlias != SourceBAlias
            && bFullHandoffsValid
            && FullHandoffAliases.Num() == 2
            && FullHandoffAliases.Contains(SourceAAlias)
            && FullHandoffAliases.Contains(SourceBAlias)
            && ReadLevelLocalField(
                AllSharedOneFields,
                TEXT("sourceLevel"),
                AllSharedOneSourceAlias)
            && ReadLevelLocalField(
                AllSharedTwoFields,
                TEXT("sourceLevel"),
                AllSharedTwoSourceAlias)
            && ReadLevelLocalField(
                AllOtherSourceFields,
                TEXT("sourceLevel"),
                AllOtherSourceAlias)
            && AllSharedOneSourceAlias == SourceAAlias
            && AllSharedTwoSourceAlias == SourceAAlias
            && AllOtherSourceAlias == SourceBAlias);

    const TSharedPtr<FJsonObject> ExactInstance =
        FSalModule::BuildQueryResult(
            LevelQueryArguments(
                Target,
                LevelExactOperation(SharedOneId)));
    const TSharedPtr<FJsonObject> ExactInstanceFields =
        FindLevelFieldsById(
            LevelTaggedObjectFields(ExactInstance, TEXT("actor")),
            SharedOneId);
    FString ExactRelatedAlias;
    FString ExactRelatedAsset;
    FString ExactSourceAlias;
    TestTrue(
        TEXT("Exact Level Instance Query keeps the containing Level as its main exact Target"),
        !LevelHasError(ExactInstance)
            && LevelHasTargetContext(
                ExactInstance,
                TEXT("exact_target"))
            && HasCanonicalLevelTarget(
                ExactInstance,
                Fixture.Containing.ObjectPath)
            && ExactInstanceFields.IsValid()
            && ReadSingleRelatedLevelTarget(
                ExactInstance,
                ExactRelatedAlias,
                ExactRelatedAsset)
            && ExactRelatedAsset == Fixture.SourceA.ObjectPath
            && HasSingleLevelHandoff(
                ExactInstance,
                TEXT("inspect_source_level"),
                ExactRelatedAlias)
            && ReadLevelLocalField(
                ExactInstanceFields,
                TEXT("sourceLevel"),
                ExactSourceAlias)
            && ExactSourceAlias == ExactRelatedAlias);

    const TSharedPtr<FJsonObject> ExactOrdinary =
        FSalModule::BuildQueryResult(
            LevelQueryArguments(
                Target,
                LevelExactOperation(OrdinaryId)));
    const TSharedPtr<FJsonObject> OrdinaryFields =
        FindLevelFieldsById(
            LevelTaggedObjectFields(ExactOrdinary, TEXT("actor")),
            OrdinaryId);
    TestTrue(
        TEXT("An ordinary Actor exposes no Level Instance source context"),
        !LevelHasError(ExactOrdinary)
            && LevelHasTargetContext(
                ExactOrdinary,
                TEXT("exact_target"))
            && HasCanonicalLevelTarget(
                ExactOrdinary,
                Fixture.Containing.ObjectPath)
            && OrdinaryFields.IsValid()
            && !OrdinaryFields->HasField(TEXT("levelInstance"))
            && !OrdinaryFields->HasField(TEXT("sourceLevel"))
            && !OrdinaryFields->HasField(
                TEXT("sourceLevelAvailable"))
            && HasNoLevelRelatedContext(ExactOrdinary));

    const TSharedPtr<FJsonObject> ExactMissingSource =
        FSalModule::BuildQueryResult(
            LevelQueryArguments(
                Target,
                LevelExactOperation(MissingSourceId)));
    const TSharedPtr<FJsonObject> MissingSourceFields =
        FindLevelFieldsById(
            LevelTaggedObjectFields(
                ExactMissingSource,
                TEXT("actor")),
            MissingSourceId);
    bool bMissingLevelInstance = false;
    TestTrue(
        TEXT("A placement with no saved source remains readable without inventing a Target"),
        !LevelHasError(ExactMissingSource)
            && LevelHasTargetContext(
                ExactMissingSource,
                TEXT("exact_target"))
            && HasCanonicalLevelTarget(
                ExactMissingSource,
                Fixture.Containing.ObjectPath)
            && LevelHasDiagnostic(
                ExactMissingSource,
                TEXT("resolution.level_instance_source_unavailable"))
            && MissingSourceFields.IsValid()
            && MissingSourceFields->TryGetBoolField(
                TEXT("levelInstance"),
                bMissingLevelInstance)
            && bMissingLevelInstance
            && !MissingSourceFields->HasField(TEXT("sourceLevel"))
            && !MissingSourceFields->HasField(
                TEXT("sourceLevelAvailable"))
            && HasNoLevelRelatedContext(ExactMissingSource));

    const TSharedPtr<FJsonObject> FirstResolvedPage =
        FSalModule::BuildQueryResult(
            LevelQueryArguments(
                Target,
                LevelOperation(
                    TEXT("actors"),
                    TEXT("Loomle Resolved Instance")),
                1));
    const TArray<TSharedPtr<FJsonObject>> PageActors =
        LevelTaggedObjectFields(FirstResolvedPage, TEXT("actor"));
    FString Cursor;
    FString PageActorId;
    FString PageSourceAlias;
    FString PageRelatedAlias;
    FString PageRelatedAsset;
    bool bPageShapeValid = !LevelHasError(FirstResolvedPage)
        && PageActors.Num() == 1
        && PageActors[0].IsValid()
        && PageActors[0]->TryGetStringField(
            TEXT("id"),
            PageActorId)
        && ReadLevelNextCursor(FirstResolvedPage, Cursor)
        && ReadLevelLocalField(
            PageActors[0],
            TEXT("sourceLevel"),
            PageSourceAlias)
        && ReadSingleRelatedLevelTarget(
            FirstResolvedPage,
            PageRelatedAlias,
            PageRelatedAsset)
        && HasSingleLevelHandoff(
            FirstResolvedPage,
            TEXT("inspect_source_level"),
            PageRelatedAlias)
        && PageSourceAlias == PageRelatedAlias;
    FString ExpectedPageSource;
    if (PageActorId == SharedOneId || PageActorId == SharedTwoId)
    {
        ExpectedPageSource = Fixture.SourceA.ObjectPath;
    }
    else if (PageActorId == OtherSourceId)
    {
        ExpectedPageSource = Fixture.SourceB.ObjectPath;
    }
    TestTrue(
        TEXT("Actor pagination retains related source context only for the emitted page"),
        bPageShapeValid
            && !ExpectedPageSource.IsEmpty()
            && PageRelatedAsset == ExpectedPageSource);

    FString SourceChangeError;
    if (TestTrue(
            TEXT("Fixture can change one soft source without loading either map"),
            Fixture.SetSharedOneSource(true, SourceChangeError)
                && Fixture.SourcesRemainUnloaded()))
    {
        const TSharedPtr<FJsonObject> StaleContinuation =
            FSalModule::BuildQueryResult(
                LevelQueryArguments(
                    Target,
                    LevelOperation(
                        TEXT("actors"),
                        TEXT("Loomle Resolved Instance")),
                    1,
                    Cursor));
        TestTrue(
            TEXT("A source Target change invalidates an existing Actor cursor"),
            LevelHasDiagnostic(
                StaleContinuation,
                TEXT("validation.invalid_cursor")));
    }
    else if (!SourceChangeError.IsEmpty())
    {
        AddError(SourceChangeError);
    }
    SourceChangeError.Reset();
    if (!Fixture.SetSharedOneSource(false, SourceChangeError))
    {
        AddError(SourceChangeError);
    }

    TestTrue(
        TEXT("All Level Instance ownership reads preserve Editor and authored state"),
        Invariant.Verify(*this));
    TestTrue(
        TEXT("All Level Instance ownership reads preserve no-load state"),
        Fixture.VerifyNoLoadState(*this));

    if (!Fixture.Cleanup(Error))
    {
        AddError(Error);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FSalLevelComponentIdentityQueryTest,
    "Loomle.Sal.Level.Query.ComponentIdentity",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSalLevelComponentIdentityQueryTest::RunTest(
    const FString& Parameters)
{
    // Declaration order is intentional: the Level fixture must destroy its
    // Actor World before the Component fixture releases the Blueprint Class.
    FScopedLevelComponentQueryFixture ComponentFixture;
    FScopedLevelQueryFixture LevelFixture;
    FString Error;
    if (!TestTrue(
            TEXT("Level fixture for Component Query builds"),
            LevelFixture.Build(Error)))
    {
        AddError(Error);
        return false;
    }
    if (!TestTrue(
            TEXT("Component Query Level becomes the active Editor World"),
            LevelFixture.Activate(LevelFixture.Loaded, Error)))
    {
        AddError(Error);
        return false;
    }
    if (!TestTrue(
            TEXT("Native, SCS, Instance, PCG, and UCS fixture builds"),
            ComponentFixture.Build(LevelFixture, Error)))
    {
        AddError(Error);
        return false;
    }

    FLevelReadInvariant LevelInvariant(LevelFixture.Loaded.World);
    FLevelComponentReadInvariant ComponentInvariant(
        ComponentFixture.Actor,
        ComponentFixture.GetBlueprintPackage());
    const TSharedRef<FJsonObject> Target = LevelTarget(
        LevelFixture.Loaded.ObjectPath,
        UWorld::StaticClass()->GetPathName());
    const FString ActorId = LevelGuidText(ComponentFixture.ActorId);

    struct FExpectedComponent
    {
        UActorComponent* Component = nullptr;
        FString Source;
        FString Id;
        FString CreationMethod;
        FString DeclaringClass;
        bool bPCG = false;
    };
    const TArray<FExpectedComponent> Expected = {
        {
            ComponentFixture.NativeComponent,
            TEXT("native"),
            ComponentFixture.NativeId,
            TEXT("Native"),
            FString(),
            false
        },
        {
            ComponentFixture.SCSComponent,
            TEXT("scs"),
            ComponentFixture.SCSId,
            TEXT("SimpleConstructionScript"),
            ComponentFixture.SCSDeclaringClass,
            false
        },
        {
            ComponentFixture.InstanceComponent,
            TEXT("instance"),
            ComponentFixture.InstanceId,
            TEXT("Instance"),
            FString(),
            false
        },
        {
            ComponentFixture.PCGComponent,
            TEXT("instance"),
            ComponentFixture.PCGId,
            TEXT("Instance"),
            FString(),
            true
        }
    };

    const TSharedPtr<FJsonObject> ComponentsResult =
        FSalModule::BuildQueryResult(
            LevelQueryArguments(
                Target,
                LevelOperation(TEXT("components"))));
    const TArray<TSharedPtr<FJsonObject>> Components =
        LevelTaggedObjectFields(ComponentsResult, TEXT("component"));
    TestTrue(
        TEXT("Component collection remains on the canonical exact Level Target"),
        !LevelHasError(ComponentsResult)
            && LevelHasTargetContext(
                ComponentsResult,
                TEXT("exact_target"))
            && HasCanonicalLevelTarget(
                ComponentsResult,
                LevelFixture.Loaded.ObjectPath));
    TestTrue(
        TEXT("Component collection emits every explicitly expected persistent slot"),
        Components.Num() >= Expected.Num());

    TSharedPtr<FJsonObject> PCGFields;
    for (const FExpectedComponent& Entry : Expected)
    {
        const TSharedPtr<FJsonObject> Fields =
            FindLevelFieldsById(Components, Entry.Id);
        FString Name;
        FString Type;
        FString Source;
        FString CreationMethod;
        FString DeclaringClass;
        bool bRegistered = false;
        bool bStableRefAvailable = false;
        TArray<FString> ActorRefPath;
        TArray<FString> ComponentRefPath;
        const TArray<FString> ExpectedActorRefPath = {ActorId};
        const TArray<FString> ExpectedComponentRefPath = {
            ActorId,
            Entry.Source,
            Entry.Id
        };
        const bool bHasDeclaringClass = Fields.IsValid()
            && Fields->TryGetStringField(
                TEXT("declaringClass"),
                DeclaringClass);
        TestTrue(
            *FString::Printf(
                TEXT("%s Component exposes the frozen 1C-A identity fields"),
                *Entry.Source),
            Fields.IsValid()
                && Fields->TryGetStringField(TEXT("id"), Name)
                && Name == Entry.Id
                && Fields->TryGetStringField(TEXT("name"), Name)
                && Name == Entry.Component->GetFName().ToString()
                && Fields->TryGetStringField(TEXT("type"), Type)
                && Type == Entry.Component->GetClass()->GetPathName()
                && ReadLevelNameField(
                    Fields,
                    TEXT("source"),
                    Source)
                && Source == Entry.Source
                && ReadLevelNameField(
                    Fields,
                    TEXT("CreationMethod"),
                    CreationMethod)
                && CreationMethod == Entry.CreationMethod
                && Fields->TryGetBoolField(
                    TEXT("registered"),
                    bRegistered)
                && bRegistered == Entry.Component->IsRegistered()
                && Fields->TryGetBoolField(
                    TEXT("stableRefAvailable"),
                    bStableRefAvailable)
                && bStableRefAvailable
                && ReadLevelStableRefField(
                    Fields,
                    TEXT("actor"),
                    TEXT("actor"),
                    ActorRefPath)
                && ActorRefPath == ExpectedActorRefPath
                && ReadLevelStableRefField(
                    Fields,
                    TEXT("ref"),
                    TEXT("component"),
                    ComponentRefPath)
                && ComponentRefPath == ExpectedComponentRefPath
                && (Entry.DeclaringClass.IsEmpty()
                    ? !bHasDeclaringClass
                    : bHasDeclaringClass
                        && DeclaringClass == Entry.DeclaringClass));
        if (Entry.bPCG)
        {
            PCGFields = Fields;
        }
        TestTrue(
            TEXT("1C-A Component exposes no specialized Target LocalRef"),
            Fields.IsValid()
                && !Fields->HasField(TEXT("pcgComponent")));
    }

    const FString UCSName =
        ComponentFixture.UCSComponent->GetFName().ToString();
    bool bUCSReturned = false;
    for (const TSharedPtr<FJsonObject>& Fields : Components)
    {
        FString Name;
        if (Fields.IsValid()
            && Fields->TryGetStringField(TEXT("name"), Name)
            && Name == UCSName)
        {
            bUCSReturned = true;
        }
    }
    TestFalse(
        TEXT("UserConstructionScript Component receives no persistent Level identity"),
        bUCSReturned);

    for (const USceneComponent* Generated :
         ComponentFixture.GeneratedPCGComponents)
    {
        bool bGeneratedReturned = false;
        for (const TSharedPtr<FJsonObject>& Fields : Components)
        {
            FString Name;
            if (Fields.IsValid()
                && Fields->TryGetStringField(TEXT("name"), Name)
                && Generated != nullptr
                && Name == Generated->GetFName().ToString())
            {
                bGeneratedReturned = true;
            }
        }
        TestFalse(
            TEXT("PCG generated, debug, and cleanup Components receive no persistent Level identity"),
            bGeneratedReturned);
    }

    TestTrue(
        TEXT("Authored PCG Component remains generic without 1C-B related context"),
        PCGFields.IsValid()
            && !PCGFields->HasField(TEXT("pcgComponent"))
            && HasNoLevelRelatedContext(ComponentsResult));

    const TSharedPtr<FJsonObject> SummaryResult =
        FSalModule::BuildQueryResult(
            LevelQueryArguments(
                Target,
                LevelOperation(TEXT("summary"))));
    const TSharedPtr<FJsonObject> SummaryFields =
        FirstLevelAssetFields(SummaryResult);
    double ComponentCount = 0.0;
    double NativeCount = 0.0;
    double SCSCount = 0.0;
    double InstanceCount = 0.0;
    double PCGCount = 0.0;
    bool bComponentIdentityComplete = false;
    TestTrue(
        TEXT("Level summary reports the closed Component identity counts"),
        !LevelHasError(SummaryResult)
            && SummaryFields.IsValid()
            && SummaryFields->TryGetNumberField(
                TEXT("componentCount"),
                ComponentCount)
            && SummaryFields->TryGetNumberField(
                TEXT("nativeComponentCount"),
                NativeCount)
            && SummaryFields->TryGetNumberField(
                TEXT("scsComponentCount"),
                SCSCount)
            && SummaryFields->TryGetNumberField(
                TEXT("instanceComponentCount"),
                InstanceCount)
            && SummaryFields->TryGetNumberField(
                TEXT("pcgComponentCount"),
                PCGCount)
            && SummaryFields->TryGetBoolField(
                TEXT("componentIdentityComplete"),
                bComponentIdentityComplete)
            && bComponentIdentityComplete
            && ComponentCount == NativeCount + SCSCount + InstanceCount
            && NativeCount >= 1.0
            && SCSCount >= 1.0
            && InstanceCount >= 2.0
            && PCGCount >= 1.0);

    auto ComponentRefKey = [](const TSharedPtr<FJsonObject>& Fields)
    {
        TArray<FString> Identity;
        return ReadLevelStableRefField(
                   Fields,
                   TEXT("ref"),
                   TEXT("component"),
                   Identity)
            ? FString::Join(Identity, TEXT("\x1f"))
            : FString();
    };
    TArray<FString> FullComponentOrder;
    for (const TSharedPtr<FJsonObject>& Fields : Components)
    {
        FullComponentOrder.Add(ComponentRefKey(Fields));
    }
    TestFalse(
        TEXT("Every Component collection item contributes one complete cursor identity"),
        FullComponentOrder.Contains(FString()));

    TArray<FString> PagedComponentOrder;
    FString ComponentCursor;
    FString FirstComponentCursor;
    for (int32 PageIndex = 0; PageIndex < 512; ++PageIndex)
    {
        const TSharedPtr<FJsonObject> PageResult =
            FSalModule::BuildQueryResult(
                LevelQueryArguments(
                    Target,
                    LevelOperation(TEXT("components")),
                    1,
                    ComponentCursor));
        if (LevelHasError(PageResult))
        {
            AddError(TEXT("A deterministic Level Component continuation page failed."));
            break;
        }
        const TArray<TSharedPtr<FJsonObject>> PageComponents =
            LevelTaggedObjectFields(PageResult, TEXT("component"));
        if (PageComponents.Num() != 1)
        {
            AddError(TEXT("A non-final Level Component page did not contain exactly one Component."));
            break;
        }
        const FString Key = ComponentRefKey(PageComponents[0]);
        if (Key.IsEmpty())
        {
            AddError(TEXT("A Level Component page omitted its structured StableRef."));
            break;
        }
        PagedComponentOrder.Add(Key);
        FString Next;
        if (!ReadLevelNextCursor(PageResult, Next))
        {
            break;
        }
        if (FirstComponentCursor.IsEmpty())
        {
            FirstComponentCursor = Next;
        }
        ComponentCursor = Next;
    }
    TestEqual(
        TEXT("Component cursor pagination reproduces deterministic collection order"),
        PagedComponentOrder,
        FullComponentOrder);

    if (!FirstComponentCursor.IsEmpty())
    {
        const TSharedPtr<FJsonObject> ReusedForOtherSearch =
            FSalModule::BuildQueryResult(
                LevelQueryArguments(
                    Target,
                    LevelOperation(
                        TEXT("components"),
                        TEXT("LoomleSCSComponent")),
                    1,
                    FirstComponentCursor));
        TestTrue(
            TEXT("Component cursor is bound to its search"),
            LevelHasDiagnostic(
                ReusedForOtherSearch,
                TEXT("validation.invalid_cursor")));

        const bool bMapDirtyBeforeRegistrationChange =
            LevelFixture.Loaded.World->GetOutermost()->IsDirty();
        ComponentFixture.InstanceComponent->UnregisterComponent();
        const TSharedPtr<FJsonObject> ReusedAfterSnapshotChange =
            FSalModule::BuildQueryResult(
                LevelQueryArguments(
                    Target,
                    LevelOperation(TEXT("components")),
                    1,
                    FirstComponentCursor));
        ComponentFixture.InstanceComponent->RegisterComponent();
        LevelFixture.Loaded.World->GetOutermost()->SetDirtyFlag(
            bMapDirtyBeforeRegistrationChange);
        TestTrue(
            TEXT("Component cursor is invalid after an emitted registration field changes"),
            LevelHasDiagnostic(
                ReusedAfterSnapshotChange,
                TEXT("validation.invalid_cursor")));
    }

    for (const FExpectedComponent& Entry : Expected)
    {
        const TSharedPtr<FJsonObject> ExactResult =
            FSalModule::BuildQueryResult(
                LevelQueryArguments(
                    Target,
                    LevelExactComponentOperation(
                        ActorId,
                        Entry.Source,
                        Entry.Id)));
        const TArray<TSharedPtr<FJsonObject>> ExactComponents =
            LevelTaggedObjectFields(ExactResult, TEXT("component"));
        TestTrue(
            *FString::Printf(
                TEXT("Structured exact Component ref resolves %s/%s"),
                *Entry.Source,
                *Entry.Id),
            !LevelHasError(ExactResult)
                && LevelHasTargetContext(
                    ExactResult,
                    TEXT("exact_target"))
                && ExactComponents.Num() == 1
                && FindLevelFieldsById(
                    ExactComponents,
                    Entry.Id).IsValid());
        const TSharedPtr<FJsonObject> ExactFields =
            FindLevelFieldsById(ExactComponents, Entry.Id);
        TestTrue(
            TEXT("Exact 1C-A Component has no 1C-B related Target context"),
            ExactFields.IsValid()
                && !ExactFields->HasField(TEXT("pcgComponent"))
                && HasNoLevelRelatedContext(ExactResult));
    }

    for (const USceneComponent* Generated :
         ComponentFixture.GeneratedPCGComponents)
    {
        if (Generated == nullptr)
        {
            continue;
        }
        const TSharedPtr<FJsonObject> GeneratedExact =
            FSalModule::BuildQueryResult(
                LevelQueryArguments(
                    Target,
                    LevelExactComponentOperation(
                        ActorId,
                        TEXT("instance"),
                        Generated->GetFName().ToString())));
        TestTrue(
            TEXT("PCG projection tags fail closed in exact Component resolution"),
            LevelHasDiagnostic(
                GeneratedExact,
                TEXT("resolution.object_not_found")));
    }

    FGuid OriginalProbeGuid;
    if (TestTrue(
            TEXT("SCS collision probe can create bounded duplicate declaration evidence"),
            ComponentFixture.BeginSCSGuidCollision(OriginalProbeGuid)))
    {
        const TSharedPtr<FJsonObject> AmbiguousSCS =
            FSalModule::BuildQueryResult(
                LevelQueryArguments(
                    Target,
                    LevelOperation(TEXT("components"))));
        ComponentFixture.EndSCSGuidCollision(OriginalProbeGuid);
        TestTrue(
            TEXT("Duplicate declaring-Class SCS VariableGuid fails Component identity closed"),
            LevelHasDiagnostic(
                AmbiguousSCS,
                TEXT("validation.reference_scan_incomplete")));
    }

    TestTrue(
        TEXT("All Component reads preserve Level and Editor state"),
        LevelInvariant.Verify(*this));
    TestTrue(
        TEXT("All Component reads preserve Component lifecycle and packages"),
        ComponentInvariant.Verify(*this));

    Error.Reset();
    if (!LevelFixture.Cleanup(Error))
    {
        AddError(Error);
    }
    Error.Reset();
    if (!ComponentFixture.Cleanup(Error))
    {
        AddError(Error);
    }
    return true;
}

}

#endif
