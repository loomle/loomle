// Copyright 2026 Loomle contributors.

#if WITH_DEV_AUTOMATION_TESTS

#include "LoomleTestObjectIteration.h"
#include "Sal/PCG/SalPCGInterface.h"
#include "Sal/SalModule.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "Editor.h"
#include "Editor/Transactor.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Curves/CurveFloat.h"
#include "HAL/FileManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/CoreDelegates.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "PCGGraph.h"
#include "PCGInputOutputSettings.h"
#include "PCGNode.h"
#include "PCGPin.h"
#include "PCGSettings.h"
#include "UObject/GarbageCollection.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/UObjectHash.h"

namespace
{
using namespace Loomle::Sal;

TArray<TSharedPtr<FJsonValue>> StringValues(
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


bool PcgMutationHasField(
    const TSharedPtr<FJsonObject>& Result,
    const FString& Field,
    bool& OutValue)
{
    OutValue = false;
    return Result.IsValid()
        && Result->TryGetBoolField(Field, OutValue);
}

TArray<TSharedPtr<FJsonValue>> PCGStringValues(
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

TSharedRef<FJsonObject> PcgTarget(
    const FString& Asset,
    const FString& Type = FString())
{
    TSharedRef<FJsonObject> Target = MakeShared<FJsonObject>();
    Target->SetStringField(TEXT("kind"), TEXT("target"));
    Target->SetStringField(TEXT("domain"), TEXT("pcg"));
    Target->SetStringField(TEXT("asset"), Asset);
    if (!Type.IsEmpty())
    {
        Target->SetStringField(TEXT("type"), Type);
    }
    return Target;
}

TSharedRef<FJsonObject> TargetBinding(
    const TSharedRef<FJsonObject>& Target)
{
    TSharedRef<FJsonObject> Binding = MakeShared<FJsonObject>();
    Binding->SetStringField(TEXT("alias"), TEXT("pcg_graph"));
    Binding->SetObjectField(TEXT("target"), Target);
    return Binding;
}

TSharedRef<FJsonObject> Operation(const FString& Kind)
{
    TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetStringField(TEXT("kind"), Kind);
    return Result;
}

TSharedRef<FJsonObject> StableRef(
    const TArray<FString>& IdentityPath)
{
    TSharedRef<FJsonObject> Ref = MakeShared<FJsonObject>();
    Ref->SetStringField(TEXT("kind"), TEXT("stable_ref"));
    Ref->SetArrayField(
        TEXT("identityPath"),
        StringValues(IdentityPath));
    return Ref;
}

TSharedRef<FJsonObject> ExactOperation(
    const TArray<FString>& IdentityPath)
{
    TSharedRef<FJsonObject> Result = Operation(TEXT("object"));
    Result->SetObjectField(TEXT("target"), StableRef(IdentityPath));
    return Result;
}

TSharedRef<FJsonObject> QueryArguments(
    const TSharedRef<FJsonObject>& Target,
    const TSharedRef<FJsonObject>& QueryOperation,
    const TArray<FString>& With = {},
    const int32 PageLimit = 0,
    const FString& PageAfter = FString())
{
    TSharedRef<FJsonObject> Query = MakeShared<FJsonObject>();
    Query->SetStringField(TEXT("kind"), TEXT("query"));
    Query->SetObjectField(TEXT("target"), TargetBinding(Target));
    Query->SetObjectField(TEXT("operation"), QueryOperation);
    if (!With.IsEmpty())
    {
        Query->SetArrayField(TEXT("with"), StringValues(With));
    }
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

TSharedRef<FJsonObject> PatchArguments(
    const TSharedRef<FJsonObject>& Target)
{
    TSharedRef<FJsonObject> Save = MakeShared<FJsonObject>();
    Save->SetStringField(TEXT("kind"), TEXT("save"));

    TSharedRef<FJsonObject> Patch = MakeShared<FJsonObject>();
    Patch->SetStringField(TEXT("kind"), TEXT("patch"));
    Patch->SetObjectField(TEXT("target"), TargetBinding(Target));
    Patch->SetBoolField(TEXT("dryRun"), true);
    Patch->SetArrayField(
        TEXT("statements"),
        {MakeShared<FJsonValueObject>(Save)});

    TSharedRef<FJsonObject> Arguments = MakeShared<FJsonObject>();
    Arguments->SetObjectField(TEXT("object"), Patch);
    return Arguments;
}

bool HasDiagnostic(
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

bool HasError(const TSharedPtr<FJsonObject>& Result)
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
            && (*Diagnostic)->TryGetStringField(
                TEXT("severity"),
                Severity)
            && Severity == TEXT("error"))
        {
            return true;
        }
    }
    return false;
}

bool HasTargetContext(
    const TSharedPtr<FJsonObject>& Result,
    const FString& Expected)
{
    FString Context;
    return Result.IsValid()
        && Result->TryGetStringField(TEXT("targetContext"), Context)
        && Context == Expected;
}

bool HasCanonicalPcgTarget(
    const TSharedPtr<FJsonObject>& Result,
    const FString& ExpectedAsset,
    const FString& ExpectedType)
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
        && Domain == TEXT("pcg")
        && (*Target)->TryGetStringField(TEXT("asset"), Asset)
        && Asset == ExpectedAsset
        && (*Target)->TryGetStringField(TEXT("type"), Type)
        && Type == ExpectedType;
}

bool TryReadObjectFields(
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

TArray<TSharedPtr<FJsonObject>> TaggedObjectFields(
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
            && TryReadObjectFields(
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

TSharedPtr<FJsonObject> FindFieldsById(
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

bool ReadAtom(
    const TSharedPtr<FJsonObject>& Fields,
    const TCHAR* Field,
    FString& OutValue)
{
    OutValue.Reset();
    if (!Fields.IsValid())
    {
        return false;
    }
    if (Fields->TryGetStringField(Field, OutValue))
    {
        return true;
    }
    const TSharedPtr<FJsonObject>* Name = nullptr;
    FString Kind;
    return Fields->TryGetObjectField(Field, Name)
        && Name != nullptr
        && (*Name).IsValid()
        && (*Name)->TryGetStringField(TEXT("kind"), Kind)
        && Kind == TEXT("name")
        && (*Name)->TryGetStringField(TEXT("name"), OutValue);
}

TSharedPtr<FJsonObject> FindPinFields(
    const TSharedPtr<FJsonObject>& Result,
    const FString& Id,
    const FString& Direction)
{
    for (const TSharedPtr<FJsonObject>& Fields :
         TaggedObjectFields(Result, TEXT("pin")))
    {
        FString ActualId;
        FString ActualDirection;
        if (Fields.IsValid()
            && Fields->TryGetStringField(TEXT("id"), ActualId)
            && ActualId == Id
            && ReadAtom(Fields, TEXT("direction"), ActualDirection)
            && ActualDirection == Direction)
        {
            return Fields;
        }
    }
    return nullptr;
}

TSharedPtr<FJsonObject> CollectTargetMemberFields(
    const TSharedPtr<FJsonObject>& Result,
    const FString& ExpectedAlias)
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
            && OwnerName == ExpectedAlias
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

bool HasCommentContaining(
    const TSharedPtr<FJsonObject>& Result,
    const FString& Fragment)
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
        return false;
    }
    for (const TSharedPtr<FJsonValue>& StatementValue : *Statements)
    {
        const TSharedPtr<FJsonObject>* Statement = nullptr;
        FString Kind;
        FString Text;
        if (StatementValue.IsValid()
            && StatementValue->TryGetObject(Statement)
            && Statement != nullptr
            && (*Statement).IsValid()
            && (*Statement)->TryGetStringField(TEXT("kind"), Kind)
            && Kind == TEXT("comment")
            && (*Statement)->TryGetStringField(TEXT("text"), Text)
            && Text.Contains(Fragment))
        {
            return true;
        }
    }
    return false;
}

int32 EdgeCount(const TSharedPtr<FJsonObject>& Result)
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
        return 0;
    }
    int32 Count = 0;
    for (const TSharedPtr<FJsonValue>& StatementValue : *Statements)
    {
        const TSharedPtr<FJsonObject>* Statement = nullptr;
        if (StatementValue.IsValid()
            && StatementValue->TryGetObject(Statement)
            && Statement != nullptr
            && (*Statement).IsValid()
            && (*Statement)->HasField(TEXT("from"))
            && (*Statement)->HasField(TEXT("to")))
        {
            ++Count;
        }
    }
    return Count;
}

bool HasEdgeBetweenNodeDirections(
    const TSharedPtr<FJsonObject>& Result,
    const FString& ExpectedFromNode,
    const FString& ExpectedToNode)
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
        return false;
    }

    TMap<FString, FString> NodeIdsByAlias;
    for (const TSharedPtr<FJsonValue>& StatementValue : *Statements)
    {
        const TSharedPtr<FJsonObject>* Statement = nullptr;
        const TSharedPtr<FJsonObject>* Target = nullptr;
        const TSharedPtr<FJsonObject>* Fields = nullptr;
        FString TargetKind;
        FString Alias;
        FString Tag;
        FString Id;
        if (StatementValue.IsValid()
            && StatementValue->TryGetObject(Statement)
            && Statement != nullptr
            && (*Statement).IsValid()
            && (*Statement)->TryGetObjectField(TEXT("target"), Target)
            && Target != nullptr
            && (*Target).IsValid()
            && (*Target)->TryGetStringField(TEXT("kind"), TargetKind)
            && TargetKind == TEXT("local")
            && (*Target)->TryGetStringField(TEXT("name"), Alias)
            && TryReadObjectFields(
                (*Statement)->TryGetField(TEXT("value")),
                Fields,
                &Tag)
            && Tag == TEXT("node")
            && Fields != nullptr
            && (*Fields)->TryGetStringField(TEXT("id"), Id))
        {
            NodeIdsByAlias.Add(Alias, Id);
        }
    }

    for (const TSharedPtr<FJsonValue>& StatementValue : *Statements)
    {
        const TSharedPtr<FJsonObject>* Statement = nullptr;
        const TSharedPtr<FJsonObject>* From = nullptr;
        const TSharedPtr<FJsonObject>* To = nullptr;
        const TSharedPtr<FJsonObject>* FromOwner = nullptr;
        const TSharedPtr<FJsonObject>* ToOwner = nullptr;
        const TArray<TSharedPtr<FJsonValue>>* FromPath = nullptr;
        const TArray<TSharedPtr<FJsonValue>>* ToPath = nullptr;
        FString FromKind;
        FString ToKind;
        FString FromOwnerKind;
        FString ToOwnerKind;
        FString FromAlias;
        FString ToAlias;
        FString FromDirection;
        FString ToDirection;
        if (StatementValue.IsValid()
            && StatementValue->TryGetObject(Statement)
            && Statement != nullptr
            && (*Statement).IsValid()
            && (*Statement)->TryGetObjectField(TEXT("from"), From)
            && (*Statement)->TryGetObjectField(TEXT("to"), To)
            && From != nullptr
            && To != nullptr
            && (*From)->TryGetStringField(TEXT("kind"), FromKind)
            && (*To)->TryGetStringField(TEXT("kind"), ToKind)
            && FromKind == TEXT("member")
            && ToKind == TEXT("member")
            && (*From)->TryGetObjectField(TEXT("object"), FromOwner)
            && (*To)->TryGetObjectField(TEXT("object"), ToOwner)
            && FromOwner != nullptr
            && ToOwner != nullptr
            && (*FromOwner)->TryGetStringField(TEXT("kind"), FromOwnerKind)
            && (*ToOwner)->TryGetStringField(TEXT("kind"), ToOwnerKind)
            && FromOwnerKind == TEXT("local")
            && ToOwnerKind == TEXT("local")
            && (*FromOwner)->TryGetStringField(TEXT("name"), FromAlias)
            && (*ToOwner)->TryGetStringField(TEXT("name"), ToAlias)
            && (*From)->TryGetArrayField(TEXT("path"), FromPath)
            && (*To)->TryGetArrayField(TEXT("path"), ToPath)
            && FromPath != nullptr
            && ToPath != nullptr
            && FromPath->Num() == 2
            && ToPath->Num() == 2
            && (*FromPath)[0]->TryGetString(FromDirection)
            && (*ToPath)[0]->TryGetString(ToDirection)
            && FromDirection == TEXT("out")
            && ToDirection == TEXT("in")
            && NodeIdsByAlias.FindRef(FromAlias) == ExpectedFromNode
            && NodeIdsByAlias.FindRef(ToAlias) == ExpectedToNode)
        {
            return true;
        }
    }
    return false;
}

bool ReadNextCursor(
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

bool ReadPoint(
    const TSharedPtr<FJsonObject>& Fields,
    const TCHAR* Field,
    int32& OutX,
    int32& OutY)
{
    const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
    double X = 0.0;
    double Y = 0.0;
    if (!Fields.IsValid()
        || !Fields->TryGetArrayField(Field, Values)
        || Values == nullptr
        || Values->Num() != 2
        || !(*Values)[0].IsValid()
        || !(*Values)[1].IsValid()
        || !(*Values)[0]->TryGetNumber(X)
        || !(*Values)[1]->TryGetNumber(Y))
    {
        return false;
    }
    OutX = static_cast<int32>(X);
    OutY = static_cast<int32>(Y);
    return true;
}

bool HasStableIdentityField(
    const TSharedPtr<FJsonObject>& Fields,
    const TCHAR* Field,
    const FString& ExpectedIdentity)
{
    const TSharedPtr<FJsonObject>* Ref = nullptr;
    const TArray<TSharedPtr<FJsonValue>>* IdentityPath = nullptr;
    FString Kind;
    FString ActualIdentity;
    return Fields.IsValid()
        && Fields->TryGetObjectField(Field, Ref)
        && Ref != nullptr
        && (*Ref).IsValid()
        && (*Ref)->TryGetStringField(TEXT("kind"), Kind)
        && Kind == TEXT("stable_ref")
        && (*Ref)->TryGetArrayField(TEXT("identityPath"), IdentityPath)
        && IdentityPath != nullptr
        && IdentityPath->Num() == 1
        && (*IdentityPath)[0].IsValid()
        && (*IdentityPath)[0]->TryGetString(ActualIdentity)
        && ActualIdentity == ExpectedIdentity;
}

bool TryReadNestedObjectFields(
    const TSharedPtr<FJsonObject>& Fields,
    const TCHAR* Field,
    const TSharedPtr<FJsonObject>*& OutFields)
{
    OutFields = nullptr;
    if (!Fields.IsValid())
    {
        return false;
    }
    const TSharedPtr<FJsonValue> Value = Fields->TryGetField(Field);
    if (TryReadObjectFields(Value, OutFields))
    {
        return true;
    }
    return Fields->TryGetObjectField(Field, OutFields)
        && OutFields != nullptr;
}

bool HasPcgTypeIdentifier(
    const TSharedPtr<FJsonObject>& Fields,
    const TCHAR* Field,
    const FPCGDataTypeIdentifier& Expected)
{
    const TSharedPtr<FJsonObject>* Identifier = nullptr;
    const TArray<TSharedPtr<FJsonValue>>* Ids = nullptr;
    double CustomSubtype = 0.0;
    if (!TryReadNestedObjectFields(Fields, Field, Identifier)
        || Identifier == nullptr
        || !(*Identifier)->TryGetArrayField(TEXT("ids"), Ids)
        || Ids == nullptr
        || Ids->Num() != Expected.GetIds().Num()
        || !(*Identifier)->TryGetNumberField(
            TEXT("customSubtype"),
            CustomSubtype)
        || CustomSubtype != Expected.CustomSubtype)
    {
        return false;
    }

    for (int32 Index = 0; Index < Ids->Num(); ++Index)
    {
        const TSharedPtr<FJsonObject>* TypeId = nullptr;
        FString StructPath;
        const bool bHasTypeId =
            TryReadObjectFields((*Ids)[Index], TypeId)
            || ((*Ids)[Index].IsValid()
                && (*Ids)[Index]->TryGetObject(TypeId)
                && TypeId != nullptr);
        const UScriptStruct* ExpectedStruct =
            Expected.GetIds()[Index].GetStruct();
        if (!bHasTypeId
            || TypeId == nullptr
            || ExpectedStruct == nullptr
            || !(*TypeId)->TryGetStringField(TEXT("struct"), StructPath)
            || StructPath != ExpectedStruct->GetPathName())
        {
            return false;
        }
    }
    return true;
}

void PreparePcgQueryPackageForCollection(UPackage* Package)
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

class FScopedPersistentPcgQueryFixture
{
public:
    ~FScopedPersistentPcgQueryFixture()
    {
        FString Ignored;
        Cleanup(Ignored);
    }

    FScopedPersistentPcgQueryFixture(
        const FScopedPersistentPcgQueryFixture&) = delete;
    FScopedPersistentPcgQueryFixture& operator=(
        const FScopedPersistentPcgQueryFixture&) = delete;

    FScopedPersistentPcgQueryFixture() = default;

    bool CreateAndSave(FString& OutError)
    {
        OutError.Reset();
        const FString Token =
            FGuid::NewGuid().ToString(EGuidFormats::Digits);
        const FString RootPackagePath = FString::Printf(
            TEXT("/Game/LoomleTests/PCGQueryPersistence/%s"),
            *Token);
        PackageName = RootPackagePath + TEXT("/PCG_QueryPersistence");
        ObjectPath = PackageName + TEXT(".PCG_QueryPersistence");
        Filename = FPackageName::LongPackageNameToFilename(
            PackageName,
            FPackageName::GetAssetPackageExtension());
        IFileManager::Get().MakeDirectory(
            *FPaths::GetPath(Filename),
            true);

        Package = CreatePackage(*PackageName);
        Graph = Package != nullptr
            ? NewObject<UPCGGraph>(
                Package,
                FName(TEXT("PCG_QueryPersistence")),
                RF_Public | RF_Standalone | RF_Transactional)
            : nullptr;
        if (Graph == nullptr)
        {
            OutError = TEXT(
                "UE failed to create the persistent PCG Query fixture.");
            return false;
        }

        InputNode = Graph->GetInputNode();
        OutputNode = Graph->GetOutputNode();
        if (!ConfigurePinsAndEdge())
        {
            OutError = TEXT(
                "UE failed to configure persistent PCG Node and Pin identity.");
            return false;
        }
        InputNode->SetNodePosition(-384, 96);
        OutputNode->SetNodePosition(448, 96);
        UPCGTrivialSettings* AuthoredSettings = nullptr;
        AuthoredNode = Graph->AddNodeOfType<UPCGTrivialSettings>(
            AuthoredSettings);
        if (AuthoredNode == nullptr || AuthoredSettings == nullptr)
        {
            OutError = TEXT(
                "UE failed to create a Graph-owned authored PCG Node.");
            return false;
        }
        AuthoredNode->SetNodePosition(32, -192);
        AuthoredNodeId = AuthoredNode->GetFName().ToString();

        FModuleManager::LoadModuleChecked<FAssetRegistryModule>(
            TEXT("AssetRegistry"));
        FAssetRegistryModule::AssetCreated(Graph);

        Package->SetDirtyFlag(true);
        Package->FullyLoad();
        FSavePackageArgs SaveArgs;
        SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
        SaveArgs.Error = GLog;
        if (!UPackage::SavePackage(
                Package,
                Graph,
                *Filename,
                SaveArgs))
        {
            OutError = TEXT(
                "UE failed to save the persistent PCG Query fixture.");
            return false;
        }
        Package->SetDirtyFlag(false);
        FModuleManager::LoadModuleChecked<FAssetRegistryModule>(
            TEXT("AssetRegistry"))
            .Get()
            .ScanModifiedAssetFiles({Filename});
        if (!IFileManager::Get().FileExists(*Filename))
        {
            OutError = TEXT(
                "The persistent PCG Query fixture has no on-disk package.");
            return false;
        }
        return true;
    }

    bool Unload(FString& OutError)
    {
        OutError.Reset();
        if (Graph == nullptr || Package == nullptr)
        {
            OutError = TEXT(
                "Cannot unload an incomplete persistent PCG Query fixture.");
            return false;
        }

        FAssetRegistryModule::AssetDeleted(Graph);
        PreparePcgQueryPackageForCollection(Package);
        Graph = nullptr;
        InputNode = nullptr;
        OutputNode = nullptr;
        AuthoredNode = nullptr;
        Package = nullptr;
        CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);
        FModuleManager::LoadModuleChecked<FAssetRegistryModule>(
            TEXT("AssetRegistry"))
            .Get()
            .ScanModifiedAssetFiles({Filename});
        if (FindPackage(nullptr, *PackageName) != nullptr
            || FindObject<UObject>(nullptr, *ObjectPath) != nullptr)
        {
            OutError = TEXT(
                "The saved PCG Query fixture remained loaded after GC.");
            return false;
        }
        return true;
    }

    bool Reload(FString& OutError)
    {
        OutError.Reset();
        Graph = LoadObject<UPCGGraph>(nullptr, *ObjectPath);
        Package = Graph != nullptr ? Graph->GetOutermost() : nullptr;
        InputNode = Graph != nullptr ? Graph->GetInputNode() : nullptr;
        OutputNode = Graph != nullptr ? Graph->GetOutputNode() : nullptr;
        AuthoredNode = nullptr;
        if (Graph != nullptr)
        {
            for (UPCGNode* Node : Graph->GetNodes())
            {
                if (Node != nullptr
                    && Node->GetFName().ToString() == AuthoredNodeId)
                {
                    AuthoredNode = Node;
                    break;
                }
            }
        }
        if (Graph == nullptr
            || Package == nullptr
            || InputNode == nullptr
            || OutputNode == nullptr
            || AuthoredNode == nullptr)
        {
            OutError = TEXT(
                "UE failed to reload the saved PCG Query fixture.");
            return false;
        }
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

        UPCGGraph* LoadedGraph = !ObjectPath.IsEmpty()
            ? FindObject<UPCGGraph>(nullptr, *ObjectPath)
            : nullptr;
        if (LoadedGraph != nullptr)
        {
            FAssetRegistryModule::AssetDeleted(LoadedGraph);
        }
        UPackage* LoadedPackage = !PackageName.IsEmpty()
            ? FindPackage(nullptr, *PackageName)
            : nullptr;
        PreparePcgQueryPackageForCollection(LoadedPackage);
        Graph = nullptr;
        InputNode = nullptr;
        OutputNode = nullptr;
        AuthoredNode = nullptr;
        Package = nullptr;
        CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);
        if (!PackageName.IsEmpty()
            && FindPackage(nullptr, *PackageName) != nullptr)
        {
            OutError = TEXT(
                "The persistent PCG Query package remained loaded during cleanup.");
        }
        if (!Filename.IsEmpty()
            && IFileManager::Get().FileExists(*Filename)
            && !IFileManager::Get().Delete(*Filename, false, true, true))
        {
            if (!OutError.IsEmpty())
            {
                OutError += TEXT(" ");
            }
            OutError += TEXT(
                "The persistent PCG Query package could not be deleted.");
        }
        if (!Filename.IsEmpty())
        {
            FModuleManager::LoadModuleChecked<FAssetRegistryModule>(
                TEXT("AssetRegistry"))
                .Get()
                .ScanModifiedAssetFiles({Filename});
            IFileManager::Get().DeleteDirectory(
                *FPaths::GetPath(Filename),
                false,
                true);
        }
        return OutError.IsEmpty();
    }

    UPackage* Package = nullptr;
    UPCGGraph* Graph = nullptr;
    UPCGNode* InputNode = nullptr;
    UPCGNode* OutputNode = nullptr;
    UPCGNode* AuthoredNode = nullptr;
    FString PackageName;
    FString ObjectPath;
    FString Filename;
    FString AuthoredNodeId;
    FName ExoticLabel = FName(TEXT("Route / Height.Value"));

private:
    bool ConfigurePinsAndEdge()
    {
        UPCGGraphInputOutputSettings* InputSettings =
            InputNode != nullptr
                ? Cast<UPCGGraphInputOutputSettings>(
                    InputNode->GetSettingsInterface())
                : nullptr;
        UPCGGraphInputOutputSettings* OutputSettings =
            OutputNode != nullptr
                ? Cast<UPCGGraphInputOutputSettings>(
                    OutputNode->GetSettingsInterface())
                : nullptr;
        if (InputSettings == nullptr || OutputSettings == nullptr)
        {
            return false;
        }

        const FPCGDataTypeIdentifier Types =
            FPCGDataTypeIdentifier{EPCGDataType::Point}
            | FPCGDataTypeIdentifier{EPCGDataType::Param};
        const FPCGPinProperties Pin(
            ExoticLabel,
            Types,
            true,
            true);
        ExoticLabel = InputSettings->AddPin(Pin).Label;
        const FName OutputLabel = OutputSettings->AddPin(
            FPCGPinProperties(
                ExoticLabel,
                Types,
                true,
                true)).Label;
        InputNode->SetSettingsInterface(InputSettings, true);
        OutputNode->SetSettingsInterface(OutputSettings, true);
        if (ExoticLabel.IsNone() || OutputLabel != ExoticLabel)
        {
            return false;
        }
        Graph->AddEdge(
            InputNode,
            ExoticLabel,
            OutputNode,
            ExoticLabel);
        return InputNode->GetInputPin(ExoticLabel) != nullptr
            && InputNode->GetOutputPin(ExoticLabel) != nullptr
            && OutputNode->GetInputPin(ExoticLabel) != nullptr;
    }

    bool bCleaned = false;
};

class FPcgQueryReadInvariant
{
public:
    FPcgQueryReadInvariant(UPCGGraph* InGraph, UPackage* InPackage)
        : Graph(InGraph)
        , Package(InPackage)
        , PackageDirtyBefore(InPackage != nullptr && InPackage->IsDirty())
        , UndoCountBefore(
            GEditor != nullptr && GEditor->Trans != nullptr
                ? GEditor->Trans->GetUndoCount()
                : -1)
        , QueueLengthBefore(
            GEditor != nullptr && GEditor->Trans != nullptr
                ? GEditor->Trans->GetQueueLength()
                : -1)
    {
        ObjectModifiedHandle =
            FCoreUObjectDelegates::OnObjectModified.AddRaw(
                this,
                &FPcgQueryReadInvariant::OnObjectModified);
        ObjectTransactedHandle =
            FCoreUObjectDelegates::OnObjectTransacted.AddRaw(
                this,
                &FPcgQueryReadInvariant::OnObjectTransacted);
        AssetLoadedHandle =
            FCoreUObjectDelegates::OnAssetLoaded.AddRaw(
                this,
                &FPcgQueryReadInvariant::OnAssetLoaded);
#if WITH_EDITOR
        if (Graph != nullptr)
        {
            GraphChangedHandle = Graph->OnGraphChangedDelegate.AddLambda(
                [this](UPCGGraphInterface*, EPCGChangeType)
                {
                    ++GraphChangedCount;
                });
        }
#endif
    }

    ~FPcgQueryReadInvariant()
    {
#if WITH_EDITOR
        if (Graph != nullptr)
        {
            Graph->OnGraphChangedDelegate.Remove(GraphChangedHandle);
        }
#endif
        FCoreUObjectDelegates::OnAssetLoaded.Remove(AssetLoadedHandle);
        FCoreUObjectDelegates::OnObjectTransacted.Remove(
            ObjectTransactedHandle);
        FCoreUObjectDelegates::OnObjectModified.Remove(
            ObjectModifiedHandle);
    }

    bool Verify(FAutomationTestBase& Test) const
    {
        bool bOk = true;
        bOk &= Test.TestEqual(
            TEXT("Post-reload PCG Queries modify no Graph-owned UObject"),
            RelevantObjectModifiedCount,
            0);
        bOk &= Test.TestEqual(
            TEXT("Post-reload PCG Queries emit no UObject transaction"),
            ObjectTransactedCount,
            0);
        bOk &= Test.TestEqual(
            TEXT("Post-reload PCG Queries trigger no additional asset load"),
            LoadedAssetCount,
            0);
#if WITH_EDITOR
        bOk &= Test.TestEqual(
            TEXT("Post-reload PCG Queries broadcast no Graph change"),
            GraphChangedCount,
            0);
#endif
        if (Package != nullptr)
        {
            bOk &= Test.TestEqual(
                TEXT("Post-reload PCG Queries preserve package dirty state"),
                Package->IsDirty(),
                PackageDirtyBefore);
        }
        if (GEditor != nullptr && GEditor->Trans != nullptr)
        {
            bOk &= Test.TestEqual(
                TEXT("Post-reload PCG Queries create no Undo entry"),
                GEditor->Trans->GetUndoCount(),
                UndoCountBefore);
            bOk &= Test.TestEqual(
                TEXT("Post-reload PCG Queries preserve the transaction queue"),
                GEditor->Trans->GetQueueLength(),
                QueueLengthBefore);
        }
        return bOk;
    }

private:
    bool IsRelevantObject(const UObject* Object) const
    {
        return Object != nullptr
            && Package != nullptr
            && Object->GetOutermost() == Package;
    }

    void OnObjectModified(UObject* Object)
    {
        if (IsRelevantObject(Object))
        {
            ++RelevantObjectModifiedCount;
        }
    }

    void OnObjectTransacted(
        UObject*,
        const FTransactionObjectEvent&)
    {
        ++ObjectTransactedCount;
    }

    void OnAssetLoaded(UObject*)
    {
        ++LoadedAssetCount;
    }

    UPCGGraph* Graph = nullptr;
    UPackage* Package = nullptr;
    bool PackageDirtyBefore = false;
    int32 UndoCountBefore = -1;
    int32 QueueLengthBefore = -1;
    int32 RelevantObjectModifiedCount = 0;
    int32 ObjectTransactedCount = 0;
    int32 LoadedAssetCount = 0;
    FDelegateHandle ObjectModifiedHandle;
    FDelegateHandle ObjectTransactedHandle;
    FDelegateHandle AssetLoadedHandle;
#if WITH_EDITOR
    int32 GraphChangedCount = 0;
    FDelegateHandle GraphChangedHandle;
#endif
};

class FScopedPcgQueryFixture
{
public:
    FScopedPcgQueryFixture()
    {
        const FString Token =
            FGuid::NewGuid().ToString(EGuidFormats::Digits);
        GraphPackage = CreatePackage(
            *FString::Printf(
                TEXT("/Game/LoomleTests/SalPCGQuery_%s"),
                *Token));
        const FName GraphName(
            *FString::Printf(TEXT("PCG_Query_%s"), *Token));
        Graph = NewObject<UPCGGraph>(
            GraphPackage,
            GraphName,
            RF_Public | RF_Standalone | RF_Transactional);
        if (Graph != nullptr)
        {
            InputNode = Graph->GetInputNode();
            OutputNode = Graph->GetOutputNode();
            ConfigurePinsAndEdge();
            if (InputNode != nullptr)
            {
                InputNode->SetNodePosition(-320, 48);
            }
            if (OutputNode != nullptr)
            {
                OutputNode->SetNodePosition(512, 48);
            }
        }

        ExternalSettingsPackage = CreatePackage(
            *FString::Printf(
                TEXT("/Game/LoomleTests/SalPCGExternalSettings_%s"),
                *Token));
        const FName ExternalSettingsName(
            *FString::Printf(TEXT("PS_External_%s"), *Token));
        ExternalSettings = NewObject<UPCGTrivialSettings>(
            ExternalSettingsPackage,
            ExternalSettingsName,
            RF_Public | RF_Standalone | RF_Transactional);
        if (Graph != nullptr && ExternalSettings != nullptr)
        {
            ExternalNode = Graph->AddNodeInstance(ExternalSettings);
        }

        OtherPackage = CreatePackage(
            *FString::Printf(
                TEXT("/Game/LoomleTests/SalPCGNonGraph_%s"),
                *Token));
        const FName OtherAssetName(
            *FString::Printf(TEXT("DA_NotPCG_%s"), *Token));
        OtherAsset = NewObject<UCurveFloat>(
            OtherPackage,
            OtherAssetName,
            RF_Public | RF_Standalone | RF_Transactional);

        FModuleManager::LoadModuleChecked<FAssetRegistryModule>(
            TEXT("AssetRegistry"));
        if (Graph != nullptr)
        {
            FAssetRegistryModule::AssetCreated(Graph);
            bGraphRegistered = true;
        }
        if (OtherAsset != nullptr)
        {
            FAssetRegistryModule::AssetCreated(OtherAsset);
            bOtherRegistered = true;
        }
        if (ExternalSettings != nullptr)
        {
            FAssetRegistryModule::AssetCreated(ExternalSettings);
            bExternalSettingsRegistered = true;
        }
        ClearDirtyFlags();
    }

    ~FScopedPcgQueryFixture()
    {
        if (ExternalNode != nullptr)
        {
            if (UPCGSettingsInstance* Instance =
                    Cast<UPCGSettingsInstance>(
                        ExternalNode->GetSettingsInterface()))
            {
                Instance->SetSettings(nullptr);
            }
        }
        if (bGraphRegistered && Graph != nullptr)
        {
            FAssetRegistryModule::AssetDeleted(Graph);
        }
        if (bOtherRegistered && OtherAsset != nullptr)
        {
            FAssetRegistryModule::AssetDeleted(OtherAsset);
        }
        if (bExternalSettingsRegistered && ExternalSettings != nullptr)
        {
            FAssetRegistryModule::AssetDeleted(ExternalSettings);
        }
        PreparePcgQueryPackageForCollection(GraphPackage);
        PreparePcgQueryPackageForCollection(OtherPackage);
        PreparePcgQueryPackageForCollection(ExternalSettingsPackage);

        Graph = nullptr;
        InputNode = nullptr;
        OutputNode = nullptr;
        ExternalSettings = nullptr;
        ExternalNode = nullptr;
        OtherAsset = nullptr;
        GraphPackage = nullptr;
        ExternalSettingsPackage = nullptr;
        OtherPackage = nullptr;
        CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);

        TArray<FString> ModifiedFiles;
        for (const FString& Filename : {
                 GraphFilename,
                 ExternalSettingsFilename,
                 OtherFilename})
        {
            if (Filename.IsEmpty())
            {
                continue;
            }
            if (IFileManager::Get().FileExists(*Filename))
            {
                IFileManager::Get().Delete(*Filename, false, true, true);
            }
            ModifiedFiles.Add(Filename);
        }
        if (!ModifiedFiles.IsEmpty())
        {
            FModuleManager::LoadModuleChecked<FAssetRegistryModule>(
                TEXT("AssetRegistry"))
                .Get()
                .ScanModifiedAssetFiles(ModifiedFiles);
        }
    }

    FScopedPcgQueryFixture(const FScopedPcgQueryFixture&) = delete;
    FScopedPcgQueryFixture& operator=(const FScopedPcgQueryFixture&) = delete;

    bool SavePublicAssets(FString& OutError)
    {
        OutError.Reset();
        if (Graph == nullptr
            || GraphPackage == nullptr
            || ExternalSettings == nullptr
            || ExternalSettingsPackage == nullptr
            || OtherAsset == nullptr
            || OtherPackage == nullptr)
        {
            OutError = TEXT(
                "Cannot save an incomplete PCG public-Target fixture.");
            return false;
        }

        GraphFilename = PackageFilename(GraphPackage);
        ExternalSettingsFilename = PackageFilename(
            ExternalSettingsPackage);
        OtherFilename = PackageFilename(OtherPackage);
        if (!SaveAsset(
                ExternalSettingsPackage,
                ExternalSettings,
                ExternalSettingsFilename,
                OutError)
            || !SaveAsset(
                OtherPackage,
                OtherAsset,
                OtherFilename,
                OutError)
            || !SaveAsset(
                GraphPackage,
                Graph,
                GraphFilename,
                OutError))
        {
            return false;
        }

        const TArray<FString> Filenames = {
            ExternalSettingsFilename,
            OtherFilename,
            GraphFilename};
        FAssetRegistryModule& AssetRegistryModule =
            FModuleManager::LoadModuleChecked<FAssetRegistryModule>(
                TEXT("AssetRegistry"));
        AssetRegistryModule.Get().ScanModifiedAssetFiles(Filenames);

        const FAssetData GraphData =
            AssetRegistryModule.Get().GetAssetByObjectPath(
                FSoftObjectPath(Graph->GetPathName()),
                true);
        const FAssetData OtherData =
            AssetRegistryModule.Get().GetAssetByObjectPath(
                FSoftObjectPath(OtherAsset->GetPathName()),
                true);
        if (!GraphData.IsValid()
            || !GraphData.IsTopLevelAsset()
            || !OtherData.IsValid()
            || !OtherData.IsTopLevelAsset())
        {
            OutError = TEXT(
                "Asset Registry did not expose disk-only evidence for the saved PCG public-Target fixtures.");
            return false;
        }
        ClearDirtyFlags();
        return true;
    }

    FSalResolvedTarget ResolvedTarget() const
    {
        FSalResolvedTarget Target;
        Target.Kind = ESalTargetKind::Asset;
        Target.Domain = ESalDomain::Pcg;
        Target.Alias = TEXT("pcg_graph");
        Target.AssetPath =
            Graph != nullptr ? Graph->GetPathName() : FString();
        Target.Object = Graph;
        Target.Package = GraphPackage;
        Target.Interfaces = {FName(TEXT("pcg"))};
        return Target;
    }

    void ClearDirtyFlags() const
    {
        if (GraphPackage != nullptr)
        {
            GraphPackage->SetDirtyFlag(false);
        }
        if (OtherPackage != nullptr)
        {
            OtherPackage->SetDirtyFlag(false);
        }
        if (ExternalSettingsPackage != nullptr)
        {
            ExternalSettingsPackage->SetDirtyFlag(false);
        }
    }

    bool IsGraphPackageDirty() const
    {
        return GraphPackage != nullptr && GraphPackage->IsDirty();
    }

    bool IsExternalSettingsPackageDirty() const
    {
        return ExternalSettingsPackage != nullptr
            && ExternalSettingsPackage->IsDirty();
    }

    UPackage* GraphPackage = nullptr;
    UPCGGraph* Graph = nullptr;
    UPCGNode* InputNode = nullptr;
    UPCGNode* OutputNode = nullptr;
    UPackage* ExternalSettingsPackage = nullptr;
    UPCGTrivialSettings* ExternalSettings = nullptr;
    UPCGNode* ExternalNode = nullptr;
    UPackage* OtherPackage = nullptr;
    UCurveFloat* OtherAsset = nullptr;
    FName ExoticLabel = FName(TEXT("Route / Height"));
    FPCGDataTypeIdentifier ExoticTypes;

private:
    static FString PackageFilename(const UPackage* Package)
    {
        return Package != nullptr
            ? FPackageName::LongPackageNameToFilename(
                Package->GetName(),
                FPackageName::GetAssetPackageExtension())
            : FString();
    }

    static bool SaveAsset(
        UPackage* Package,
        UObject* Asset,
        const FString& Filename,
        FString& OutError)
    {
        if (Package == nullptr || Asset == nullptr || Filename.IsEmpty())
        {
            OutError = TEXT("Cannot save an incomplete PCG fixture asset.");
            return false;
        }
        IFileManager::Get().MakeDirectory(
            *FPaths::GetPath(Filename),
            true);
        Package->SetDirtyFlag(true);
        Package->FullyLoad();
        FSavePackageArgs SaveArgs;
        SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
        SaveArgs.Error = GLog;
        if (!UPackage::SavePackage(
                Package,
                Asset,
                *Filename,
                SaveArgs))
        {
            Package->SetDirtyFlag(false);
            OutError = TEXT("UE failed to save PCG fixture asset ")
                + Asset->GetPathName();
            return false;
        }
        Package->SetDirtyFlag(false);
        if (!IFileManager::Get().FileExists(*Filename))
        {
            OutError = TEXT("Saved PCG fixture has no on-disk package: ")
                + Asset->GetPathName();
            return false;
        }
        return true;
    }

    void ConfigurePinsAndEdge()
    {
        UPCGGraphInputOutputSettings* InputSettings =
            InputNode != nullptr
                ? Cast<UPCGGraphInputOutputSettings>(
                    InputNode->GetSettingsInterface())
                : nullptr;
        UPCGGraphInputOutputSettings* OutputSettings =
            OutputNode != nullptr
                ? Cast<UPCGGraphInputOutputSettings>(
                    OutputNode->GetSettingsInterface())
                : nullptr;
        if (InputSettings == nullptr || OutputSettings == nullptr)
        {
            return;
        }

        ExoticTypes = FPCGDataTypeIdentifier{EPCGDataType::Point}
            | FPCGDataTypeIdentifier{EPCGDataType::Param};
        ExoticTypes.CustomSubtype = 17;
        const FPCGPinProperties ExoticPin(
            ExoticLabel,
            ExoticTypes,
            true,
            true);
        ExoticLabel = InputSettings->AddPin(ExoticPin).Label;
        const FName OutputLabel = OutputSettings->AddPin(
            FPCGPinProperties(
                ExoticLabel,
                ExoticTypes,
                true,
                true)).Label;
        InputNode->SetSettingsInterface(InputSettings, true);
        OutputNode->SetSettingsInterface(OutputSettings, true);
        if (OutputLabel == ExoticLabel)
        {
            Graph->AddEdge(
                InputNode,
                ExoticLabel,
                OutputNode,
                ExoticLabel);
        }
    }

    bool bGraphRegistered = false;
    bool bOtherRegistered = false;
    bool bExternalSettingsRegistered = false;
    FString GraphFilename;
    FString ExternalSettingsFilename;
    FString OtherFilename;
};

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FSalPcgTargetResolutionTest,
    "Loomle.Sal.PCG.Query.TargetResolutionAndPatchBoundary",
    EAutomationTestFlags::EditorContext
        | EAutomationTestFlags::EngineFilter)

bool FSalPcgTargetResolutionTest::RunTest(
    const FString& Parameters)
{
    FScopedPcgQueryFixture Fixture;
    TestNotNull(TEXT("In-memory PCG Graph fixture is created"), Fixture.Graph);
    TestNotNull(TEXT("Non-PCG asset fixture is created"), Fixture.OtherAsset);
    if (Fixture.Graph == nullptr || Fixture.OtherAsset == nullptr)
    {
        return false;
    }

    const FString GraphPath = Fixture.Graph->GetPathName();
    const FString GraphType = Fixture.Graph->GetClass()->GetPathName();
    FAssetRegistryModule& AssetRegistryModule =
        FModuleManager::LoadModuleChecked<FAssetRegistryModule>(
            TEXT("AssetRegistry"));
    const FAssetData UnsavedGraphData =
        AssetRegistryModule.Get().GetAssetByObjectPath(
            FSoftObjectPath(GraphPath),
            true);
    TestFalse(
        TEXT("AssetCreated-only PCG Graph has no disk-only Asset Registry evidence"),
        UnsavedGraphData.IsValid()
            && UnsavedGraphData.IsTopLevelAsset());

    int32 AssetLoadCount = 0;
    UPCGGraph* const OriginalGraph = Fixture.Graph;
    const FDelegateHandle AssetLoadedHandle =
        FCoreUObjectDelegates::OnAssetLoaded.AddLambda(
            [&AssetLoadCount](UObject*)
            {
                ++AssetLoadCount;
            });
    const TSharedPtr<FJsonObject> UnsavedResult =
        FSalModule::BuildQueryResult(
            QueryArguments(
                PcgTarget(GraphPath),
                Operation(TEXT("target"))));
    FCoreUObjectDelegates::OnAssetLoaded.Remove(AssetLoadedHandle);
    TestTrue(
        TEXT("AssetCreated-only PCG Graph is rejected without saved identity"),
        HasDiagnostic(
            UnsavedResult,
            TEXT("resolution.target_not_found")));
    TestTrue(
        TEXT("Unsaved PCG Target rejection remains unresolved"),
        HasTargetContext(UnsavedResult, TEXT("unresolved_target")));
    TestEqual(
        TEXT("Unsaved PCG Target resolution triggers no asset load"),
        AssetLoadCount,
        0);
    TestTrue(
        TEXT("Unsaved PCG Target resolution preserves the exact loaded fixture"),
        Fixture.Graph == OriginalGraph);
    TestFalse(
        TEXT("Unsaved PCG Target resolution leaves its package clean"),
        Fixture.IsGraphPackageDirty());

    FString SaveError;
    const bool bSaved = Fixture.SavePublicAssets(SaveError);
    TestTrue(
        *FString::Printf(
            TEXT("Public PCG Target fixtures are saved and disk-indexed: %s"),
            *SaveError),
        bSaved);
    if (!bSaved)
    {
        return false;
    }
    const FAssetData SavedGraphData =
        AssetRegistryModule.Get().GetAssetByObjectPath(
            FSoftObjectPath(GraphPath),
            true);
    TestTrue(
        TEXT("Public PCG Target has exact disk-only UPCGGraph evidence"),
        SavedGraphData.IsValid()
            && SavedGraphData.IsTopLevelAsset()
            && SavedGraphData.AssetClassPath.ToString() == GraphType);

    const TSharedPtr<FJsonObject> TargetResult =
        FSalModule::BuildQueryResult(
            QueryArguments(
                PcgTarget(GraphPath),
                Operation(TEXT("target")),
                {TEXT("schema")}));
    TestFalse(TEXT("Canonical PCG Target resolves"), HasError(TargetResult));
    TestTrue(
        TEXT("Resolved PCG Target has exact target context"),
        HasTargetContext(TargetResult, TEXT("exact_target")));
    TestTrue(
        TEXT("Resolved PCG Target is canonicalized with native Class"),
        HasCanonicalPcgTarget(TargetResult, GraphPath, GraphType));

    const TSharedPtr<FJsonObject> WrongType =
        FSalModule::BuildQueryResult(
            QueryArguments(
                PcgTarget(
                    GraphPath,
                    UCurveFloat::StaticClass()->GetPathName()),
                Operation(TEXT("target"))));
    TestTrue(
        TEXT("PCG Target rejects a mismatched native Class assertion"),
        HasDiagnostic(WrongType, TEXT("validation.invalid_target")));
    TestTrue(
        TEXT("Type mismatch fails before an exact Target is established"),
        HasTargetContext(WrongType, TEXT("unresolved_target")));

    const FString OtherPath = Fixture.OtherAsset->GetPathName();
    const FAssetData SavedNonPcgData =
        AssetRegistryModule.Get().GetAssetByObjectPath(
            FSoftObjectPath(OtherPath),
            true);
    TestTrue(
        TEXT("Non-PCG rejection fixture has exact disk-only asset evidence"),
        SavedNonPcgData.IsValid()
            && SavedNonPcgData.IsTopLevelAsset()
            && SavedNonPcgData.AssetClassPath
                == UCurveFloat::StaticClass()->GetClassPathName());
    const TSharedPtr<FJsonObject> NonPcg =
        FSalModule::BuildQueryResult(
            QueryArguments(
                PcgTarget(OtherPath),
                Operation(TEXT("target"))));
    TestTrue(
        TEXT("PCG Target rejects a non-PCG asset"),
        HasDiagnostic(
            NonPcg,
            TEXT("capability.interface_unavailable")));
    TestTrue(
        TEXT("Non-PCG rejection remains unresolved"),
        HasTargetContext(NonPcg, TEXT("unresolved_target")));

    const TSharedPtr<FJsonObject> PatchResult =
        FSalModule::BuildPatchResult(
            PatchArguments(PcgTarget(GraphPath, GraphType)));
    TestTrue(
        TEXT("PCG Patch is admitted for adapter resolution after the "
            "authored-mutation capability bump"),
        !HasDiagnostic(
            PatchResult,
            TEXT("language.invalid_object_shape")));
    TestFalse(
        TEXT("Admitted PCG Patch resolution does not dirty the Graph package"),
        Fixture.IsGraphPackageDirty());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FSalPcgStableIdentityTest,
    "Loomle.Sal.PCG.Query.StructuredNodeAndPinIdentity",
    EAutomationTestFlags::EditorContext
        | EAutomationTestFlags::EngineFilter)

bool FSalPcgStableIdentityTest::RunTest(
    const FString& Parameters)
{
    FScopedPcgQueryFixture Fixture;
    TestNotNull(TEXT("PCG identity fixture is created"), Fixture.Graph);
    TestNotNull(TEXT("PCG input Node exists"), Fixture.InputNode);
    if (Fixture.Graph == nullptr || Fixture.InputNode == nullptr)
    {
        return false;
    }

    const FSalResolvedTarget Target = Fixture.ResolvedTarget();
    const FString NodeId = Fixture.InputNode->GetFName().ToString();
    const FString Label = Fixture.ExoticLabel.ToString();
    FString Code;
    FString Message;

    TSharedPtr<FJsonObject> NodeRef =
        StableRef({NodeId});
    TestTrue(
        TEXT("One-segment PCG identity resolves to a native Node"),
        FSalPCGInterface::LowerStableReference(
            Target,
            {NodeId},
            NodeRef,
            Code,
            Message));
    FString Kind;
    FString CanonicalNodeId;
    TestTrue(
        TEXT("Node StableRef lowers to the structured Node operation"),
        NodeRef->TryGetStringField(TEXT("kind"), Kind)
            && Kind == TEXT("node")
            && NodeRef->TryGetStringField(TEXT("id"), CanonicalNodeId)
            && CanonicalNodeId == NodeId);

    for (const FString& Direction : {FString(TEXT("in")), FString(TEXT("out"))})
    {
        TSharedPtr<FJsonObject> PinRef =
            StableRef({NodeId, Direction, Label});
        Code.Reset();
        Message.Reset();
        TestTrue(
            *FString::Printf(
                TEXT("Slash-bearing %s Pin identity resolves losslessly"),
                *Direction),
            FSalPCGInterface::LowerStableReference(
                Target,
                {NodeId, Direction, Label},
                PinRef,
                Code,
                Message));
        FString PinKind;
        FString PinNode;
        FString PinDirection;
        FString PinLabel;
        TestTrue(
            *FString::Printf(
                TEXT("%s Pin lowers without fusing identity segments"),
                *Direction),
            PinRef->TryGetStringField(TEXT("kind"), PinKind)
                && PinKind == TEXT("pin")
                && PinRef->TryGetStringField(TEXT("node"), PinNode)
                && PinNode == NodeId
                && PinRef->TryGetStringField(
                    TEXT("direction"),
                    PinDirection)
                && PinDirection == Direction
                && PinRef->TryGetStringField(TEXT("label"), PinLabel)
                && PinLabel == Label);
    }

    TSharedPtr<FJsonObject> InvalidDirection =
        StableRef({NodeId, TEXT("input"), Label});
    Code.Reset();
    Message.Reset();
    TestFalse(
        TEXT("PCG Pin identity rejects non-canonical direction text"),
        FSalPCGInterface::LowerStableReference(
            Target,
            {NodeId, TEXT("input"), Label},
            InvalidDirection,
            Code,
            Message));
    TestEqual(
        TEXT("Invalid PCG Pin direction has a domain-owned diagnostic"),
        Code,
        FString(TEXT("validation.invalid_reference")));
    TestFalse(
        TEXT("Stable identity resolution remains read-only"),
        Fixture.IsGraphPackageDirty());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FSalPcgReadOnlyQueryTest,
    "Loomle.Sal.PCG.Query.TargetSummaryNodesAndExactPins",
    EAutomationTestFlags::EditorContext
        | EAutomationTestFlags::EngineFilter)

bool FSalPcgReadOnlyQueryTest::RunTest(
    const FString& Parameters)
{
    FScopedPcgQueryFixture Fixture;
    TestNotNull(TEXT("PCG Query fixture is created"), Fixture.Graph);
    TestNotNull(TEXT("PCG input Node exists"), Fixture.InputNode);
    TestNotNull(TEXT("PCG output Node exists"), Fixture.OutputNode);
    if (Fixture.Graph == nullptr
        || Fixture.InputNode == nullptr
        || Fixture.OutputNode == nullptr)
    {
        return false;
    }

    FString SaveError;
    const bool bSaved = Fixture.SavePublicAssets(SaveError);
    TestTrue(
        *FString::Printf(
            TEXT("PCG public Query fixture is saved and disk-indexed: %s"),
            *SaveError),
        bSaved);
    if (!bSaved)
    {
        return false;
    }

    const FString GraphPath = Fixture.Graph->GetPathName();
    const FString InputNodeId =
        Fixture.InputNode->GetFName().ToString();
    const FString OutputNodeId =
        Fixture.OutputNode->GetFName().ToString();
    const FString Label = Fixture.ExoticLabel.ToString();
    const TSharedRef<FJsonObject> Target = PcgTarget(GraphPath);

    const TSharedPtr<FJsonObject> TargetResult =
        FSalModule::BuildQueryResult(
            QueryArguments(
                Target,
                Operation(TEXT("target")),
                {TEXT("schema")}));
    TestFalse(TEXT("PCG target Query succeeds"), HasError(TargetResult));
    const TSharedPtr<FJsonObject> TargetFields =
        CollectTargetMemberFields(TargetResult, TEXT("pcg_graph"));
    TestTrue(
        TEXT("PCG target returns its native asset record"),
        TargetFields.IsValid());
    if (TargetFields.IsValid())
    {
        FString Type;
        FString Name;
        TestTrue(
            TEXT("PCG target reports native path, Class, and name"),
            TargetFields->TryGetStringField(TEXT("type"), Type)
                && Type == Fixture.Graph->GetClass()->GetPathName()
                && TargetFields->TryGetStringField(TEXT("name"), Name)
                && Name == Fixture.Graph->GetName());
        TestTrue(
            TEXT("PCG target returns its exact default Input Node reference"),
            HasStableIdentityField(
                TargetFields,
                TEXT("DefaultInputNode"),
                InputNodeId));
        TestTrue(
            TEXT("PCG target returns its exact default Output Node reference"),
            HasStableIdentityField(
                TargetFields,
                TEXT("DefaultOutputNode"),
                OutputNodeId));
    }
    TestTrue(
        TEXT("PCG target with schema returns adjacent read-only guidance"),
        HasCommentContaining(
            TargetResult,
            TEXT("pcg target schema (read-only)")));

    const TSharedPtr<FJsonObject> SummaryResult =
        FSalModule::BuildQueryResult(
            QueryArguments(Target, Operation(TEXT("summary"))));
    TestFalse(TEXT("PCG summary Query succeeds"), HasError(SummaryResult));
    const TSharedPtr<FJsonObject> SummaryFields =
        CollectTargetMemberFields(SummaryResult, TEXT("pcg_graph"));
    TestTrue(
        TEXT("PCG summary returns the Graph asset record"),
        SummaryFields.IsValid());
    if (SummaryFields.IsValid())
    {
        TArray<UPCGNode*> NativeNodes = Fixture.Graph->GetNodes();
        NativeNodes.Insert(Fixture.InputNode, 0);
        NativeNodes.Add(Fixture.OutputNode);
        int32 ExpectedInputPins = 0;
        int32 ExpectedOutputPins = 0;
        for (const UPCGNode* NativeNode : NativeNodes)
        {
            ExpectedInputPins += NativeNode->GetInputPins().Num();
            ExpectedOutputPins += NativeNode->GetOutputPins().Num();
        }
        double NodeCount = 0.0;
        double AuthoredNodeCount = 0.0;
        double InputPinCount = 0.0;
        double OutputPinCount = 0.0;
        double NativeEdgeCount = 0.0;
        TestTrue(
            TEXT("PCG summary reports Nodes, Pins, and Edges"),
            SummaryFields->TryGetNumberField(
                TEXT("nodeCount"),
                NodeCount)
                && NodeCount == NativeNodes.Num()
                && SummaryFields->TryGetNumberField(
                    TEXT("authoredNodeCount"),
                    AuthoredNodeCount)
                && AuthoredNodeCount == Fixture.Graph->GetNodes().Num()
                && SummaryFields->TryGetNumberField(
                    TEXT("inputPinCount"),
                    InputPinCount)
                && InputPinCount == ExpectedInputPins
                && SummaryFields->TryGetNumberField(
                    TEXT("outputPinCount"),
                    OutputPinCount)
                && OutputPinCount == ExpectedOutputPins
                && SummaryFields->TryGetNumberField(
                    TEXT("edgeCount"),
                    NativeEdgeCount)
                && NativeEdgeCount == 1.0);
    }

    const TSharedPtr<FJsonObject> NodesResult =
        FSalModule::BuildQueryResult(
            QueryArguments(
                Target,
                Operation(TEXT("nodes")),
                {TEXT("layout")}));
    TestFalse(TEXT("PCG Nodes Query succeeds"), HasError(NodesResult));
    const TArray<TSharedPtr<FJsonObject>> Nodes =
        TaggedObjectFields(NodesResult, TEXT("node"));
    TestTrue(
        TEXT("PCG Nodes includes the default Input Node"),
        FindFieldsById(Nodes, InputNodeId).IsValid());
    TestTrue(
        TEXT("PCG Nodes includes the default Output Node"),
        FindFieldsById(Nodes, OutputNodeId).IsValid());
    TestTrue(
        TEXT("PCG Nodes includes the external-Settings instance Node"),
        Fixture.ExternalNode != nullptr
            && FindFieldsById(
                Nodes,
                Fixture.ExternalNode->GetFName().ToString()).IsValid());
    const TSharedPtr<FJsonObject> InputNodeFields =
        FindFieldsById(Nodes, InputNodeId);
    if (InputNodeFields.IsValid())
    {
        int32 X = 0;
        int32 Y = 0;
        TestTrue(
            TEXT("PCG Nodes with layout returns persisted integer position"),
            ReadPoint(InputNodeFields, TEXT("at"), X, Y)
                && X == -320
                && Y == 48);
    }

    const TSharedPtr<FJsonObject> FirstPage =
        FSalModule::BuildQueryResult(
            QueryArguments(
                Target,
                Operation(TEXT("nodes")),
                {},
                1));
    FString NextCursor;
    TestTrue(
        TEXT("PCG Nodes emits a bounded deterministic first page"),
        !HasError(FirstPage)
            && TaggedObjectFields(FirstPage, TEXT("node")).Num() == 1
            && FindFieldsById(
                TaggedObjectFields(FirstPage, TEXT("node")),
                InputNodeId).IsValid()
            && ReadNextCursor(FirstPage, NextCursor));
    const TSharedPtr<FJsonObject> SecondPage =
        FSalModule::BuildQueryResult(
            QueryArguments(
                Target,
                Operation(TEXT("nodes")),
                {},
                1,
                NextCursor));
    TestTrue(
        TEXT("PCG Nodes cursor resumes in native deterministic order"),
        !HasError(SecondPage)
            && Fixture.ExternalNode != nullptr
            && FindFieldsById(
                TaggedObjectFields(SecondPage, TEXT("node")),
                Fixture.ExternalNode->GetFName().ToString()).IsValid());

    TSharedRef<FJsonObject> SearchNodes = Operation(TEXT("nodes"));
    SearchNodes->SetStringField(
        TEXT("text"),
        UPCGTrivialSettings::StaticClass()->GetPathName());
    const TSharedPtr<FJsonObject> SearchResult =
        FSalModule::BuildQueryResult(
            QueryArguments(Target, SearchNodes));
    TestTrue(
        TEXT("PCG Nodes search includes the effective external Settings Class"),
        !HasError(SearchResult)
            && Fixture.ExternalNode != nullptr
            && TaggedObjectFields(SearchResult, TEXT("node")).Num() == 1
            && FindFieldsById(
                TaggedObjectFields(SearchResult, TEXT("node")),
                Fixture.ExternalNode->GetFName().ToString()).IsValid());

    const TSharedPtr<FJsonObject> ExactNode =
        FSalModule::BuildQueryResult(
            QueryArguments(
                Target,
                ExactOperation({InputNodeId}),
                {TEXT("schema"), TEXT("layout")}));
    TestFalse(TEXT("Exact PCG Node Query succeeds"), HasError(ExactNode));
    const TSharedPtr<FJsonObject> ExactNodeFields =
        FindFieldsById(
            TaggedObjectFields(ExactNode, TEXT("node")),
            InputNodeId);
    TestTrue(
        TEXT("Exact PCG Node returns native identity"),
        ExactNodeFields.IsValid());
    if (ExactNodeFields.IsValid())
    {
        FString Type;
        int32 X = 0;
        int32 Y = 0;
        TestTrue(
            TEXT("Exact PCG Node distinguishes Node Class from Settings"),
            ExactNodeFields->TryGetStringField(TEXT("type"), Type)
                && Type == UPCGNode::StaticClass()->GetPathName());
        TestTrue(
            TEXT("Exact PCG Node returns persisted layout"),
            ReadPoint(ExactNodeFields, TEXT("at"), X, Y)
                && X == -320
                && Y == 48);
        const TSharedPtr<FJsonObject>* SettingsFields = nullptr;
        FString Ownership;
        FString SettingsType;
        FString EffectiveType;
        TestTrue(
            TEXT("Exact PCG Node preserves its Settings interface boundary"),
            TryReadNestedObjectFields(
                ExactNodeFields,
                TEXT("SettingsInterface"),
                SettingsFields)
                && SettingsFields != nullptr
                && ReadAtom(*SettingsFields, TEXT("ownership"), Ownership)
                && Ownership == TEXT("owned")
                && (*SettingsFields)->TryGetStringField(
                    TEXT("type"),
                    SettingsType)
                && !SettingsType.IsEmpty()
                && (*SettingsFields)->TryGetStringField(
                    TEXT("effectiveType"),
                    EffectiveType)
                && !EffectiveType.IsEmpty());
    }
    TestTrue(
        TEXT("Exact Node distinguishes same-label input and output Pins"),
        FindPinFields(ExactNode, Label, TEXT("in")).IsValid()
            && FindPinFields(ExactNode, Label, TEXT("out")).IsValid());
    TestTrue(
        TEXT("Exact Node emits its incident authored Edge"),
        EdgeCount(ExactNode) == 1
            && HasEdgeBetweenNodeDirections(
                ExactNode,
                InputNodeId,
                OutputNodeId));
    TestTrue(
        TEXT("Exact Node with schema emits adjacent read-only guidance"),
        HasCommentContaining(
            ExactNode,
            TEXT("pcg node schema (read-only)")));

    const FString ExternalNodeId = Fixture.ExternalNode != nullptr
        ? Fixture.ExternalNode->GetFName().ToString()
        : FString();
    const TSharedPtr<FJsonObject> ExactExternalNode =
        FSalModule::BuildQueryResult(
            QueryArguments(
                Target,
                ExactOperation({ExternalNodeId}),
                {TEXT("schema")}));
    const TSharedPtr<FJsonObject> ExternalNodeFields =
        FindFieldsById(
            TaggedObjectFields(ExactExternalNode, TEXT("node")),
            ExternalNodeId);
    const TSharedPtr<FJsonObject>* ExternalInterfaceFields = nullptr;
    FString OverallOwnership;
    FString InterfaceOwnership;
    FString EffectiveOwnership;
    FString InterfaceType;
    FString EffectiveType;
    FString ExternalPath;
    bool bIsInstance = false;
    TestTrue(
        TEXT("Exact external Settings Node preserves wrapper/effective ownership crossing"),
        Fixture.ExternalNode != nullptr
            && Fixture.ExternalSettings != nullptr
            && !HasError(ExactExternalNode)
            && TryReadNestedObjectFields(
                ExternalNodeFields,
                TEXT("SettingsInterface"),
                ExternalInterfaceFields)
            && ExternalInterfaceFields != nullptr
            && ReadAtom(
                *ExternalInterfaceFields,
                TEXT("ownership"),
                OverallOwnership)
            && OverallOwnership == TEXT("external")
            && ReadAtom(
                *ExternalInterfaceFields,
                TEXT("interfaceOwnership"),
                InterfaceOwnership)
            && InterfaceOwnership == TEXT("owned")
            && ReadAtom(
                *ExternalInterfaceFields,
                TEXT("effectiveOwnership"),
                EffectiveOwnership)
            && EffectiveOwnership == TEXT("external")
            && (*ExternalInterfaceFields)->TryGetBoolField(
                TEXT("isInstance"),
                bIsInstance)
            && bIsInstance
            && (*ExternalInterfaceFields)->TryGetStringField(
                TEXT("type"),
                InterfaceType)
            && InterfaceType
                == UPCGSettingsInstance::StaticClass()->GetPathName()
            && (*ExternalInterfaceFields)->TryGetStringField(
                TEXT("effectiveType"),
                EffectiveType)
            && EffectiveType
                == UPCGTrivialSettings::StaticClass()->GetPathName()
            && (*ExternalInterfaceFields)->TryGetStringField(
                TEXT("Settings"),
                ExternalPath)
            && ExternalPath == Fixture.ExternalSettings->GetPathName()
            && !ExactExternalNode->HasField(TEXT("relatedTargets")));

    for (const FString& Direction : {FString(TEXT("in")), FString(TEXT("out"))})
    {
        const double ExpectedConnectionCount =
            Direction == TEXT("out") ? 1.0 : 0.0;
        const UPCGPin* NativePin = Direction == TEXT("out")
            ? Fixture.InputNode->GetOutputPin(Fixture.ExoticLabel)
            : Fixture.InputNode->GetInputPin(Fixture.ExoticLabel);
        const TSharedPtr<FJsonObject> ExactPin =
            FSalModule::BuildQueryResult(
                QueryArguments(
                    Target,
                    ExactOperation({InputNodeId, Direction, Label}),
                    {TEXT("schema"), TEXT("layout")}));
        TestFalse(
            *FString::Printf(
                TEXT("Exact %s Pin Query succeeds"),
                *Direction),
            HasError(ExactPin));
        const TSharedPtr<FJsonObject> Pin =
            FindPinFields(ExactPin, Label, Direction);
        TestTrue(
            *FString::Printf(
                TEXT("Exact %s Pin preserves slash-bearing Label and direction"),
                *Direction),
            Pin.IsValid());
        if (Pin.IsValid())
        {
            double ConnectionCount = 0.0;
            TestTrue(
                *FString::Printf(
                    TEXT("Exact %s Pin returns native type unions and topology"),
                    *Direction),
                NativePin != nullptr
                    && Fixture.ExoticTypes.GetIds().Num() > 1
                    && Fixture.ExoticTypes.CustomSubtype == 17
                    && HasPcgTypeIdentifier(
                        Pin,
                        TEXT("allowedTypes"),
                        NativePin->Properties.AllowedTypes)
                    && HasPcgTypeIdentifier(
                        Pin,
                        TEXT("currentTypes"),
                        NativePin->GetCurrentTypesID())
                    && Pin->HasField(TEXT("typeDisplay"))
                    && Pin->TryGetNumberField(
                        TEXT("connectionCount"),
                        ConnectionCount)
                    && ConnectionCount == ExpectedConnectionCount);
        }
        const TSharedPtr<FJsonObject> PinOwner =
            FindFieldsById(
                TaggedObjectFields(ExactPin, TEXT("node")),
                InputNodeId);
        int32 OwnerX = 0;
        int32 OwnerY = 0;
        TestTrue(
            *FString::Printf(
                TEXT("Exact %s Pin with layout returns compact owner position"),
                *Direction),
            ReadPoint(PinOwner, TEXT("at"), OwnerX, OwnerY)
                && OwnerX == -320
                && OwnerY == 48);
        TestEqual(
            *FString::Printf(
                TEXT("Exact %s Pin emits only its own incident Edges"),
                *Direction),
            EdgeCount(ExactPin),
            Direction == TEXT("out") ? 1 : 0);
        if (Direction == TEXT("out"))
        {
            TestTrue(
                TEXT("Exact output Pin Edge preserves exact native endpoints"),
                HasEdgeBetweenNodeDirections(
                    ExactPin,
                    InputNodeId,
                    OutputNodeId));
        }
        TestTrue(
            *FString::Printf(
                TEXT("Exact %s Pin with schema emits adjacent guidance"),
                *Direction),
            HasCommentContaining(
                ExactPin,
                TEXT("pcg pin schema (read-only)")));
    }

    TestFalse(
        TEXT("All PCG read-only Queries leave the Graph package clean"),
        Fixture.IsGraphPackageDirty());
    TestFalse(
        TEXT("All PCG read-only Queries leave the external Settings package clean"),
        Fixture.IsExternalSettingsPackageDirty());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FSalPcgSavedIdentityRoundTripTest,
    "Loomle.Sal.PCG.Query.SavedIdentityRoundTripAndReadInvariants",
    EAutomationTestFlags::EditorContext
        | EAutomationTestFlags::EngineFilter)

bool FSalPcgSavedIdentityRoundTripTest::RunTest(
    const FString& Parameters)
{
    if (GEditor == nullptr
        || GEditor->IsTransactionActive()
        || GEditor->IsPlaySessionInProgress())
    {
        AddError(TEXT(
            "PCG Query persistence coverage requires an idle Editor outside PIE and transactions."));
        return false;
    }

    FScopedPersistentPcgQueryFixture Fixture;
    FString Error;
    const bool bCreated = Fixture.CreateAndSave(Error);
    TestTrue(
        *FString::Printf(
            TEXT("Persistent PCG Query fixture is saved: %s"),
            *Error),
        bCreated);
    if (!bCreated
        || Fixture.Graph == nullptr
        || Fixture.InputNode == nullptr
        || Fixture.OutputNode == nullptr
        || Fixture.AuthoredNode == nullptr)
    {
        return false;
    }

    const FString GraphPath = Fixture.Graph->GetPathName();
    const FString GraphType = Fixture.Graph->GetClass()->GetPathName();
    const FString InputNodeId =
        Fixture.InputNode->GetFName().ToString();
    const FString OutputNodeId =
        Fixture.OutputNode->GetFName().ToString();
    const FString AuthoredNodeId =
        Fixture.AuthoredNode->GetFName().ToString();
    const FString Label = Fixture.ExoticLabel.ToString();
    TestTrue(
        TEXT("Saved PCG fixture is a top-level Graph asset"),
        Fixture.Graph->IsAsset()
            && Fixture.Graph->GetTypedOuter<UPCGGraph>() == nullptr
            && GraphPath == Fixture.ObjectPath);
    TestTrue(
        TEXT("Saved PCG fixture has structured slash-bearing Pin identities"),
        Label.Contains(TEXT(" "))
            && Label.Contains(TEXT("."))
            && Label.Contains(TEXT("/"))
            && Fixture.InputNode->GetInputPin(Fixture.ExoticLabel) != nullptr
            && Fixture.InputNode->GetOutputPin(Fixture.ExoticLabel) != nullptr
            && Fixture.OutputNode->GetInputPin(Fixture.ExoticLabel) != nullptr);
    TestFalse(
        TEXT("Saved PCG fixture package begins clean"),
        Fixture.Package->IsDirty());

    Error.Reset();
    const bool bUnloaded = Fixture.Unload(Error);
    TestTrue(
        *FString::Printf(
            TEXT("Saved PCG Query fixture unloads completely: %s"),
            *Error),
        bUnloaded);
    if (!bUnloaded)
    {
        return false;
    }

    Error.Reset();
    const bool bReloaded = Fixture.Reload(Error);
    TestTrue(
        *FString::Printf(
            TEXT("Saved PCG Query fixture reloads from disk: %s"),
            *Error),
        bReloaded);
    if (!bReloaded)
    {
        return false;
    }

    TestEqual(
        TEXT("Package reload preserves the canonical PCG Target path"),
        Fixture.Graph->GetPathName(),
        GraphPath);
    TestEqual(
        TEXT("Package reload preserves the default Input Node FName"),
        Fixture.InputNode->GetFName().ToString(),
        InputNodeId);
    TestEqual(
        TEXT("Package reload preserves the default Output Node FName"),
        Fixture.OutputNode->GetFName().ToString(),
        OutputNodeId);
    TestEqual(
        TEXT("Package reload preserves the authored Node FName"),
        Fixture.AuthoredNode->GetFName().ToString(),
        AuthoredNodeId);
    TestTrue(
        TEXT("Package reload preserves same-label input and output Pin identity"),
        Fixture.InputNode->GetInputPin(Fixture.ExoticLabel) != nullptr
            && Fixture.InputNode->GetOutputPin(Fixture.ExoticLabel) != nullptr
            && Fixture.OutputNode->GetInputPin(Fixture.ExoticLabel) != nullptr);
    TestFalse(
        TEXT("Reloaded PCG package is clean before Query readback"),
        Fixture.Package->IsDirty());

    {
        FPcgQueryReadInvariant ReadInvariant(
            Fixture.Graph,
            Fixture.Package);
        const TSharedRef<FJsonObject> SavedTarget =
            PcgTarget(GraphPath, GraphType);

        const TSharedPtr<FJsonObject> TargetResult =
            FSalModule::BuildQueryResult(
                QueryArguments(
                    SavedTarget,
                    Operation(TEXT("target"))));
        TestTrue(
            TEXT("Canonical PCG Target round-trips after package reload"),
            !HasError(TargetResult)
                && HasCanonicalPcgTarget(
                    TargetResult,
                    GraphPath,
                    GraphType));
        const TSharedPtr<FJsonObject> TargetFields =
            CollectTargetMemberFields(TargetResult, TEXT("pcg_graph"));
        TestTrue(
            TEXT("Reloaded Target preserves default Node StableRefs"),
            HasStableIdentityField(
                TargetFields,
                TEXT("DefaultInputNode"),
                InputNodeId)
                && HasStableIdentityField(
                    TargetFields,
                    TEXT("DefaultOutputNode"),
                    OutputNodeId));

        const TSharedPtr<FJsonObject> NodesResult =
            FSalModule::BuildQueryResult(
                QueryArguments(
                    SavedTarget,
                    Operation(TEXT("nodes")),
                    {TEXT("layout")}));
        const TArray<TSharedPtr<FJsonObject>> Nodes =
            TaggedObjectFields(NodesResult, TEXT("node"));
        TestTrue(
            TEXT("Reloaded Nodes Query preserves every canonical Node FName"),
            !HasError(NodesResult)
                && FindFieldsById(Nodes, InputNodeId).IsValid()
                && FindFieldsById(Nodes, OutputNodeId).IsValid()
                && FindFieldsById(Nodes, AuthoredNodeId).IsValid());

        const TSharedPtr<FJsonObject> ExactAuthoredNode =
            FSalModule::BuildQueryResult(
                QueryArguments(
                    SavedTarget,
                    ExactOperation({AuthoredNodeId}),
                    {TEXT("layout")}));
        const TSharedPtr<FJsonObject> ExactAuthoredNodeFields =
            FindFieldsById(
                TaggedObjectFields(
                    ExactAuthoredNode,
                    TEXT("node")),
                AuthoredNodeId);
        int32 AuthoredX = 0;
        int32 AuthoredY = 0;
        TestTrue(
            TEXT("Saved authored Node StableRef and FName round-trip after reload"),
            !HasError(ExactAuthoredNode)
                && ExactAuthoredNodeFields.IsValid()
                && ReadPoint(
                    ExactAuthoredNodeFields,
                    TEXT("at"),
                    AuthoredX,
                    AuthoredY)
                && AuthoredX == 32
                && AuthoredY == -192);

        const TSharedPtr<FJsonObject> ExactInputNode =
            FSalModule::BuildQueryResult(
                QueryArguments(
                    SavedTarget,
                    ExactOperation({InputNodeId}),
                    {TEXT("layout")}));
        const TSharedPtr<FJsonObject> ExactNodeFields =
            FindFieldsById(
                TaggedObjectFields(ExactInputNode, TEXT("node")),
                InputNodeId);
        int32 X = 0;
        int32 Y = 0;
        TestTrue(
            TEXT("Saved Node StableRef and authored layout round-trip after reload"),
            !HasError(ExactInputNode)
                && ExactNodeFields.IsValid()
                && ReadPoint(ExactNodeFields, TEXT("at"), X, Y)
                && X == -384
                && Y == 96);
        TestTrue(
            TEXT("Reloaded exact Node preserves both structured Pin directions"),
            FindPinFields(
                ExactInputNode,
                Label,
                TEXT("in")).IsValid()
                && FindPinFields(
                    ExactInputNode,
                    Label,
                    TEXT("out")).IsValid());
        TestTrue(
            TEXT("Reloaded exact Node preserves the authored Edge endpoints"),
            EdgeCount(ExactInputNode) == 1
                && HasEdgeBetweenNodeDirections(
                    ExactInputNode,
                    InputNodeId,
                    OutputNodeId));

        for (const FString& Direction :
             {FString(TEXT("in")), FString(TEXT("out"))})
        {
            const TSharedPtr<FJsonObject> ExactPin =
                FSalModule::BuildQueryResult(
                    QueryArguments(
                        SavedTarget,
                        ExactOperation(
                            {InputNodeId, Direction, Label})));
            TestTrue(
                *FString::Printf(
                    TEXT("Saved %s Pin StableRef preserves Node, direction, and slash-bearing Label after reload"),
                    *Direction),
                !HasError(ExactPin)
                    && FindPinFields(
                        ExactPin,
                        Label,
                        Direction).IsValid()
                    && EdgeCount(ExactPin)
                        == (Direction == TEXT("out") ? 1 : 0));
        }

        ReadInvariant.Verify(*this);
    }

    Error.Reset();
    const bool bCleaned = Fixture.Cleanup(Error);
    TestTrue(
        *FString::Printf(
            TEXT("Persistent PCG Query fixture is removed: %s"),
            *Error),
        bCleaned);
    return true;
}

// ============================================================================
// PCG authored mutation (Slice 2-A) tests
// ============================================================================

TSharedRef<FJsonObject> PcgPatchArguments(
    const TSharedRef<FJsonObject>& Target,
    const TArray<TSharedPtr<FJsonValue>>& Statements,
    const bool bDryRun = false)
{
    TSharedRef<FJsonObject> Binding = MakeShared<FJsonObject>();
    Binding->SetStringField(TEXT("alias"), TEXT("pcg_scope"));
    Binding->SetObjectField(TEXT("target"), Target);
    TSharedRef<FJsonObject> Patch = MakeShared<FJsonObject>();
    Patch->SetStringField(TEXT("kind"), TEXT("patch"));
    Patch->SetObjectField(TEXT("target"), Binding);
    Patch->SetBoolField(TEXT("dryRun"), bDryRun);
    Patch->SetArrayField(TEXT("statements"), Statements);
    TSharedRef<FJsonObject> Arguments = MakeShared<FJsonObject>();
    Arguments->SetObjectField(TEXT("object"), Patch);
    return Arguments;
}

TArray<TSharedPtr<FJsonValue>> PcgAddNodeStatements(
    const FString& Alias,
    const FString& PaletteId)
{
    TSharedRef<FJsonObject> BindingTarget = MakeShared<FJsonObject>();
    BindingTarget->SetStringField(TEXT("kind"), TEXT("local"));
    BindingTarget->SetStringField(TEXT("name"), Alias);
    TSharedRef<FJsonObject> Value = MakeShared<FJsonObject>();
    Value->SetStringField(TEXT("kind"), TEXT("object"));
    TSharedRef<FJsonObject> Fields = MakeShared<FJsonObject>();
    Fields->SetStringField(TEXT("palette"), PaletteId);
    Value->SetObjectField(TEXT("fields"), Fields);
    TSharedRef<FJsonObject> BindingStatement = MakeShared<FJsonObject>();
    BindingStatement->SetObjectField(TEXT("target"), BindingTarget);
    BindingStatement->SetObjectField(TEXT("value"), Value);

    TSharedRef<FJsonObject> Statement = MakeShared<FJsonObject>();
    Statement->SetStringField(TEXT("kind"), TEXT("add"));
    TSharedRef<FJsonObject> Target = MakeShared<FJsonObject>();
    Target->SetStringField(TEXT("kind"), TEXT("local"));
    Target->SetStringField(TEXT("name"), Alias);
    Statement->SetObjectField(TEXT("target"), Target);
    return {
        MakeShared<FJsonValueObject>(BindingStatement),
        MakeShared<FJsonValueObject>(Statement),
    };
}

TArray<FString> CollectPCGNodePaletteIds(
    const TSharedPtr<FJsonObject>& Result)
{
    TArray<FString> Out;
    const TSharedPtr<FJsonObject>* Object = nullptr;
    const TArray<TSharedPtr<FJsonValue>>* Statements = nullptr;
    if (!Result.IsValid()
        || !Result->TryGetObjectField(TEXT("object"), Object)
        || Object == nullptr
        || !(*Object).IsValid()
        || !(*Object)->TryGetArrayField(TEXT("statements"), Statements)
        || Statements == nullptr)
    {
        return Out;
    }
    for (const TSharedPtr<FJsonValue>& StatementValue : *Statements)
    {
        const TSharedPtr<FJsonObject>* Statement = nullptr;
        const TSharedPtr<FJsonObject>* Value = nullptr;
        const TSharedPtr<FJsonObject>* Args = nullptr;
        FString ValueKind;
        if (StatementValue.IsValid()
            && StatementValue->TryGetObject(Statement)
            && Statement != nullptr
            && (*Statement).IsValid()
            && (*Statement)->TryGetObjectField(TEXT("value"), Value)
            && Value != nullptr
            && (*Value).IsValid()
            && (*Value)->TryGetStringField(TEXT("kind"), ValueKind)
            && ValueKind == TEXT("object")
            && (*Value)->TryGetObjectField(TEXT("fields"), Args)
            && Args != nullptr)
        {
            FString PaletteId;
            if ((*Args)->TryGetStringField(TEXT("palette"), PaletteId)
                && !PaletteId.IsEmpty())
            {
                Out.Add(PaletteId);
            }
        }
    }
    return Out;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FSalPcgPatchNodeCreationTest,
    "Loomle.Sal.PCG.Patch.NodeCreation",
    EAutomationTestFlags::EditorContext
        | EAutomationTestFlags::EngineFilter)

bool FSalPcgPatchNodeCreationTest::RunTest(
    const FString& Parameters)
{
    FScopedPersistentPcgQueryFixture Fixture;
    FString Error;
    if (!TestTrue(TEXT("PCG Patch fixture creates and saves"), Fixture.CreateAndSave(Error)))
    {
        AddError(Error);
        return false;
    }
    const FString GraphPath = Fixture.ObjectPath;
    const FString GraphType = Fixture.Graph->GetClass()->GetPathName();
    const int32 NodeCountBefore = Fixture.Graph->GetNodes().Num();
    const TSharedRef<FJsonObject> Target =
        PcgTarget(GraphPath, GraphType);

    // Discover one node Palette entry.
    const TSharedPtr<FJsonObject> PaletteResult =
        FSalModule::BuildQueryResult(
            QueryArguments(Target, Operation(TEXT("palette_entries"))));
    const TArray<FString> PaletteIds =
        CollectPCGNodePaletteIds(PaletteResult);
    if (!TestTrue(
            TEXT("PCG node Palette exposes at least one entry"),
            !PaletteIds.IsEmpty()))
    {
        return false;
    }

    // Dry run plans without creating.
    const TSharedPtr<FJsonObject> DryRun =
        FSalModule::BuildPatchResult(
            PcgPatchArguments(
                Target,
                PcgAddNodeStatements(TEXT("sample"), PaletteIds[0]),
                true));
    bool bValid = false;
    bool bApplied = false;
    if (!(PcgMutationHasField(DryRun, TEXT("valid"), bValid) && bValid))
    {
        FString Dump;
        FJsonSerializer::Serialize(
            DryRun.ToSharedRef(),
            TJsonWriterFactory<>::Create(&Dump));
        AddError(FString::Printf(
            TEXT("PCG node add dry-run result: %s"),
            *Dump));
    }
    TestTrue(
        TEXT("PCG node add dry-run validates"),
        PcgMutationHasField(DryRun, TEXT("valid"), bValid) && bValid);
    TestTrue(
        TEXT("PCG node add dry-run does not apply"),
        PcgMutationHasField(DryRun, TEXT("applied"), bApplied) && !bApplied);
    TestEqual(
        TEXT("PCG node add dry-run creates nothing"),
        Fixture.Graph->GetNodes().Num(),
        NodeCountBefore);

    // Live add creates the Node.
    const TSharedPtr<FJsonObject> AddResult =
        FSalModule::BuildPatchResult(
            PcgPatchArguments(
                Target,
                PcgAddNodeStatements(TEXT("sample"), PaletteIds[0])));
    TestTrue(
        TEXT("PCG node add applies"),
        PcgMutationHasField(AddResult, TEXT("applied"), bApplied) && bApplied);
    TestTrue(
        TEXT("PCG node add is valid"),
        PcgMutationHasField(AddResult, TEXT("valid"), bValid) && bValid);
    TestEqual(
        TEXT("PCG node add created one Node"),
        Fixture.Graph->GetNodes().Num(),
        NodeCountBefore + 1);

    // Unsupported statements fail closed.
    TSharedRef<FJsonObject> SetStatement = MakeShared<FJsonObject>();
    SetStatement->SetStringField(TEXT("kind"), TEXT("set"));
    TSharedRef<FJsonObject> SetTarget = MakeShared<FJsonObject>();
    SetTarget->SetStringField(TEXT("kind"), TEXT("member"));
    TSharedRef<FJsonObject> SetOwner = MakeShared<FJsonObject>();
    SetOwner->SetStringField(TEXT("kind"), TEXT("stable_ref"));
    SetOwner->SetArrayField(
        TEXT("identityPath"),
        PCGStringValues({TEXT("SomeNode")}));
    SetTarget->SetObjectField(TEXT("object"), SetOwner);
    SetTarget->SetArrayField(
        TEXT("path"),
        PCGStringValues({TEXT("SomeField")}));
    SetStatement->SetObjectField(TEXT("target"), SetTarget);
    SetStatement->SetStringField(TEXT("value"), TEXT("1"));
    const TSharedPtr<FJsonObject> SetResult =
        FSalModule::BuildPatchResult(
            PcgPatchArguments(
                Target,
                {MakeShared<FJsonValueObject>(SetStatement)}));
    TestTrue(
        TEXT("PCG Patch rejects unsupported statements"),
        PcgMutationHasField(SetResult, TEXT("valid"), bValid) && !bValid);

    if (GEditor != nullptr && GEditor->Trans != nullptr)
    {
        GEditor->Trans->Reset(FText::FromString(TEXT("SAL test cleanup")));
    }
    if (Fixture.Graph->GetOutermost() != nullptr)
    {
        Fixture.Graph->GetOutermost()->SetDirtyFlag(false);
    }
    if (!Fixture.Cleanup(Error))
    {
        AddError(Error);
    }
    return true;
}


TSharedRef<FJsonObject> PcgConnectStatement(
    const FString& FromNode,
    const FString& FromLabel,
    const FString& ToNode,
    const FString& ToLabel,
    const FString& Kind = TEXT("connect"))
{
    TSharedRef<FJsonObject> Statement = MakeShared<FJsonObject>();
    Statement->SetStringField(TEXT("kind"), Kind);
    TSharedRef<FJsonObject> From = MakeShared<FJsonObject>();
    From->SetStringField(TEXT("kind"), TEXT("stable_ref"));
    From->SetArrayField(
        TEXT("identityPath"),
        PCGStringValues({FromNode, TEXT("out"), FromLabel}));
    TSharedRef<FJsonObject> To = MakeShared<FJsonObject>();
    To->SetStringField(TEXT("kind"), TEXT("stable_ref"));
    To->SetArrayField(
        TEXT("identityPath"),
        PCGStringValues({ToNode, TEXT("in"), ToLabel}));
    Statement->SetObjectField(TEXT("from"), From);
    Statement->SetObjectField(TEXT("to"), To);
    return Statement;
}

TSharedRef<FJsonObject> PcgRemoveNodeStatement(const FString& NodeId)
{
    TSharedRef<FJsonObject> Statement = MakeShared<FJsonObject>();
    Statement->SetStringField(TEXT("kind"), TEXT("remove"));
    TSharedRef<FJsonObject> Target = MakeShared<FJsonObject>();
    Target->SetStringField(TEXT("kind"), TEXT("stable_ref"));
    Target->SetArrayField(
        TEXT("identityPath"),
        PCGStringValues({NodeId}));
    Statement->SetObjectField(TEXT("target"), Target);
    return Statement;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FSalPcgPatchEdgeAndRemoveTest,
    "Loomle.Sal.PCG.Patch.EdgeAndRemove",
    EAutomationTestFlags::EditorContext
        | EAutomationTestFlags::EngineFilter)

bool FSalPcgPatchEdgeAndRemoveTest::RunTest(
    const FString& Parameters)
{
    FScopedPersistentPcgQueryFixture Fixture;
    FString Error;
    if (!TestTrue(TEXT("PCG edge fixture creates and saves"), Fixture.CreateAndSave(Error)))
    {
        AddError(Error);
        return false;
    }
    const FString GraphPath = Fixture.ObjectPath;
    const FString GraphType = Fixture.Graph->GetClass()->GetPathName();
    const TSharedRef<FJsonObject> Target =
        PcgTarget(GraphPath, GraphType);
    const int32 NodeCountBefore = Fixture.Graph->GetNodes().Num();
    UPCGNode* const AuthoredNode = Fixture.AuthoredNode;
    if (!TestNotNull(TEXT("PCG edge fixture authored Node"), AuthoredNode))
    {
        return false;
    }
    if (AuthoredNode->GetOutputPins().IsEmpty())
    {
        AddError(TEXT("Authored Node exposes no output Pin."));
        return false;
    }
    const FString AuthoredOutLabel =
        AuthoredNode->GetOutputPins()[0]->Properties.Label.ToString();

    // Add one Node via the Palette.
    const TSharedPtr<FJsonObject> PaletteResult =
        FSalModule::BuildQueryResult(
            QueryArguments(Target, Operation(TEXT("palette_entries"))));
    const TArray<FString> PaletteIds =
        CollectPCGNodePaletteIds(PaletteResult);
    if (!TestTrue(TEXT("PCG edge Palette exposes entries"), !PaletteIds.IsEmpty()))
    {
        return false;
    }
    const TSharedPtr<FJsonObject> AddResult =
        FSalModule::BuildPatchResult(
            PcgPatchArguments(
                Target,
                PcgAddNodeStatements(TEXT("sample"), PaletteIds[0])));
    bool bValid = false;
    bool bApplied = false;
    if (!(PcgMutationHasField(AddResult, TEXT("applied"), bApplied) && bApplied))
    {
        AddError(TEXT("PCG edge fixture add failed to apply."));
        return false;
    }
    TestEqual(
        TEXT("PCG edge fixture added one Node"),
        Fixture.Graph->GetNodes().Num(),
        NodeCountBefore + 1);
    UPCGNode* NewNode = nullptr;
    for (UPCGNode* Node : Fixture.Graph->GetNodes())
    {
        if (Node != nullptr && Node != Fixture.Graph->GetInputNode()
            && Node != Fixture.Graph->GetOutputNode()
            && Node != AuthoredNode)
        {
            NewNode = Node;
        }
    }
    if (!TestNotNull(TEXT("PCG edge fixture found the created Node"), NewNode))
    {
        return false;
    }
    if (NewNode->GetInputPins().IsEmpty())
    {
        AddError(TEXT("Created Node exposes no input Pin."));
        return false;
    }
    const FString NewNodeId = NewNode->GetFName().ToString();
    const FString NewInLabel =
        NewNode->GetInputPins()[0]->Properties.Label.ToString();
    const int32 EdgeCountBefore = NewNode->GetInputPin(
        FName(*NewInLabel))->Edges.Num();

    // Connect the Authored output to the new input.
    const TSharedPtr<FJsonObject> ConnectResult =
        FSalModule::BuildPatchResult(
            PcgPatchArguments(
                Target,
                {MakeShared<FJsonValueObject>(PcgConnectStatement(
                    AuthoredNode->GetFName().ToString(),
                    AuthoredOutLabel,
                    NewNodeId,
                    NewInLabel))}));
    TestTrue(
        TEXT("PCG connect applies"),
        PcgMutationHasField(ConnectResult, TEXT("applied"), bApplied) && bApplied);
    TestEqual(
        TEXT("PCG connect added one edge"),
        NewNode->GetInputPin(FName(*NewInLabel))->Edges.Num(),
        EdgeCountBefore + 1);

    // Disconnect the same pair.
    const TSharedPtr<FJsonObject> DisconnectResult =
        FSalModule::BuildPatchResult(
            PcgPatchArguments(
                Target,
                {MakeShared<FJsonValueObject>(PcgConnectStatement(
                    AuthoredNode->GetFName().ToString(),
                    AuthoredOutLabel,
                    NewNodeId,
                    NewInLabel,
                    TEXT("disconnect")))}));
    TestTrue(
        TEXT("PCG disconnect applies"),
        PcgMutationHasField(DisconnectResult, TEXT("applied"), bApplied) && bApplied);
    TestEqual(
        TEXT("PCG disconnect removed one edge"),
        NewNode->GetInputPin(FName(*NewInLabel))->Edges.Num(),
        EdgeCountBefore);

    // Remove the created Node.
    const TSharedPtr<FJsonObject> RemoveResult =
        FSalModule::BuildPatchResult(
            PcgPatchArguments(
                Target,
                {MakeShared<FJsonValueObject>(PcgRemoveNodeStatement(NewNodeId))}));
    TestTrue(
        TEXT("PCG remove applies"),
        PcgMutationHasField(RemoveResult, TEXT("applied"), bApplied) && bApplied);
    TestEqual(
        TEXT("PCG remove deleted the Node"),
        Fixture.Graph->GetNodes().Num(),
        NodeCountBefore);

    if (GEditor != nullptr && GEditor->Trans != nullptr)
    {
        GEditor->Trans->Reset(FText::FromString(TEXT("SAL test cleanup")));
    }
    if (Fixture.Graph->GetOutermost() != nullptr)
    {
        Fixture.Graph->GetOutermost()->SetDirtyFlag(false);
    }
    if (!Fixture.Cleanup(Error))
    {
        AddError(Error);
    }
    return true;
}

}

#endif
