// Copyright 2026 Loomle contributors.

#if WITH_DEV_AUTOMATION_TESTS

#include "Sal/PCG/SalPCGInterface.h"
#include "Sal/SalModule.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Curves/CurveFloat.h"
#include "Misc/AutomationTest.h"
#include "Modules/ModuleManager.h"
#include "PCGGraph.h"
#include "PCGInputOutputSettings.h"
#include "PCGNode.h"
#include "PCGPin.h"
#include "PCGSettings.h"
#include "UObject/Package.h"
#include "UObject/UObjectGlobals.h"

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
        if (Graph != nullptr)
        {
            Graph->ClearFlags(RF_Public | RF_Standalone);
        }
        if (OtherAsset != nullptr)
        {
            OtherAsset->ClearFlags(RF_Public | RF_Standalone);
        }
        if (ExternalSettings != nullptr)
        {
            ExternalSettings->ClearFlags(RF_Public | RF_Standalone);
        }
        ClearDirtyFlags();
    }

    FScopedPcgQueryFixture(const FScopedPcgQueryFixture&) = delete;
    FScopedPcgQueryFixture& operator=(const FScopedPcgQueryFixture&) = delete;

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

    const TSharedPtr<FJsonObject> NonPcg =
        FSalModule::BuildQueryResult(
            QueryArguments(
                PcgTarget(Fixture.OtherAsset->GetPathName()),
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
        TEXT("PCG Patch remains unavailable in the read-only slice"),
        HasDiagnostic(
            PatchResult,
            TEXT("capability.operation_unavailable")));
    TestTrue(
        TEXT("PCG Patch rejection occurs after Target resolution"),
        HasTargetContext(PatchResult, TEXT("exact_target")));
    TestFalse(
        TEXT("Target resolution and rejected Patch do not dirty the Graph package"),
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
}

#endif
