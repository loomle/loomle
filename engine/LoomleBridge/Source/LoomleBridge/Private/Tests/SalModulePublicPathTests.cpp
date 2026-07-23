// Copyright 2026 Loomle contributors.

#if WITH_DEV_AUTOMATION_TESTS

#include "Sal/SalJson.h"
#include "Sal/SalModule.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetBlueprintGeneratedClass.h"
#include "Blueprint/WidgetTree.h"
#include "Components/CanvasPanel.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraphSchema_K2.h"
#include "Editor.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "GameFramework/Actor.h"
#include "K2Node_CustomEvent.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/AutomationTest.h"
#include "StateTree.h"
#include "StateTreeEditorData.h"
#include "StateTreeState.h"
#include "UObject/GarbageCollection.h"
#include "UObject/Package.h"
#include "UObject/UObjectHash.h"
#include "WidgetBlueprint.h"

namespace
{
using namespace Loomle::Sal;

TSharedRef<FJsonObject> PublicPathCall(
    const FString& Callee,
    const TSharedRef<FJsonObject>& Args)
{
    TSharedRef<FJsonObject> Value = MakeShared<FJsonObject>();
    Value->SetStringField(TEXT("kind"), TEXT("call"));
    Value->SetStringField(TEXT("callee"), Callee);
    Value->SetObjectField(TEXT("args"), Args);
    return Value;
}

TSharedRef<FJsonObject> PublicPathClassCall(const FString& Path)
{
    TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
    Args->SetStringField(TEXT("path"), Path);
    return PublicPathCall(TEXT("class"), Args);
}

TSharedRef<FJsonObject> PublicPathTarget(
    const FString& Alias,
    const TSharedRef<FJsonObject>& Value)
{
    TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetStringField(TEXT("alias"), Alias);
    Result->SetObjectField(TEXT("value"), Value);
    return Result;
}

TSharedRef<FJsonObject> PublicPathQueryArguments(
    const FString& Alias,
    const TSharedRef<FJsonObject>& Value,
    const FString& OperationKind)
{
    TSharedRef<FJsonObject> Operation = MakeShared<FJsonObject>();
    Operation->SetStringField(TEXT("kind"), OperationKind);

    TSharedRef<FJsonObject> Query = MakeShared<FJsonObject>();
    Query->SetStringField(TEXT("kind"), TEXT("query"));
    Query->SetObjectField(TEXT("target"), PublicPathTarget(Alias, Value));
    Query->SetObjectField(TEXT("operation"), Operation);

    TSharedRef<FJsonObject> Arguments = MakeShared<FJsonObject>();
    Arguments->SetObjectField(TEXT("object"), Query);
    return Arguments;
}

TSharedRef<FJsonObject> PublicPathQueryArguments(
    const FString& Alias,
    const TSharedRef<FJsonObject>& Value,
    const TSharedRef<FJsonObject>& Operation)
{
    TSharedRef<FJsonObject> Query = MakeShared<FJsonObject>();
    Query->SetStringField(TEXT("kind"), TEXT("query"));
    Query->SetObjectField(TEXT("target"), PublicPathTarget(Alias, Value));
    Query->SetObjectField(TEXT("operation"), Operation);

    TSharedRef<FJsonObject> Arguments = MakeShared<FJsonObject>();
    Arguments->SetObjectField(TEXT("object"), Query);
    return Arguments;
}

TSharedRef<FJsonObject> PublicPathOperation(const FString& Kind)
{
    TSharedRef<FJsonObject> Operation = MakeShared<FJsonObject>();
    Operation->SetStringField(TEXT("kind"), Kind);
    return Operation;
}

TSharedRef<FJsonObject> PublicPathAssetRoot()
{
    TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
    Root->SetStringField(TEXT("kind"), TEXT("name"));
    Root->SetStringField(TEXT("name"), TEXT("asset"));
    return Root;
}

TSharedRef<FJsonObject> PublicPathAssetCall(
    const FString& Path,
    const FString& Type = FString())
{
    TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
    Args->SetStringField(TEXT("path"), Path);
    if (!Type.IsEmpty())
    {
        Args->SetStringField(TEXT("type"), Type);
    }
    return PublicPathCall(TEXT("asset"), Args);
}

TSharedRef<FJsonObject> PublicPathBlueprintCall(
    const FString& Path,
    const FGuid& Id)
{
    TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
    Args->SetStringField(TEXT("asset"), Path);
    Args->SetStringField(
        TEXT("id"),
        Id.ToString(EGuidFormats::DigitsWithHyphensLower));
    return PublicPathCall(TEXT("blueprint"), Args);
}

TSharedRef<FJsonObject> PublicPathGraphCall(
    const FString& BlueprintPath,
    const FGuid& BlueprintId,
    const UEdGraph* Graph)
{
    TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
    Args->SetObjectField(
        TEXT("asset"),
        PublicPathBlueprintCall(BlueprintPath, BlueprintId));
    Args->SetStringField(
        TEXT("id"),
        Graph != nullptr
            ? Graph->GraphGuid.ToString(
                EGuidFormats::DigitsWithHyphensLower)
            : FString());
    return PublicPathCall(TEXT("graph"), Args);
}

TSharedRef<FJsonObject> PublicPathPatchArguments(
    const FString& Alias,
    const TSharedRef<FJsonObject>& Value,
    const TArray<TSharedPtr<FJsonValue>>& Statements)
{
    TSharedRef<FJsonObject> Patch = MakeShared<FJsonObject>();
    Patch->SetStringField(TEXT("kind"), TEXT("patch"));
    Patch->SetObjectField(TEXT("target"), PublicPathTarget(Alias, Value));
    Patch->SetBoolField(TEXT("dryRun"), true);
    Patch->SetArrayField(TEXT("statements"), Statements);

    TSharedRef<FJsonObject> Arguments = MakeShared<FJsonObject>();
    Arguments->SetObjectField(TEXT("object"), Patch);
    return Arguments;
}

TSharedPtr<FJsonValue> PublicPathStatement(
    const TSharedRef<FJsonObject>& Statement)
{
    return MakeShared<FJsonValueObject>(Statement);
}

TSharedRef<FJsonObject> PublicPathSaveStatement()
{
    TSharedRef<FJsonObject> Save = MakeShared<FJsonObject>();
    Save->SetStringField(TEXT("kind"), TEXT("save"));
    return Save;
}

TSharedRef<FJsonObject> PublicPathSetMemberStatement(
    const TSharedRef<FJsonObject>& Owner,
    const FString& Property,
    const TSharedPtr<FJsonValue>& Value)
{
    TSharedRef<FJsonObject> Member = MakeShared<FJsonObject>();
    Member->SetStringField(TEXT("kind"), TEXT("member"));
    Member->SetObjectField(TEXT("object"), Owner);
    Member->SetArrayField(
        TEXT("path"),
        {MakeShared<FJsonValueString>(Property)});

    TSharedRef<FJsonObject> Set = MakeShared<FJsonObject>();
    Set->SetStringField(TEXT("kind"), TEXT("set"));
    Set->SetObjectField(TEXT("target"), Member);
    Set->SetField(TEXT("value"), Value);
    return Set;
}

TSharedRef<FJsonObject> PublicPathStableReference(
    const FString& Kind,
    const FGuid& Id)
{
    TSharedRef<FJsonObject> Reference = MakeShared<FJsonObject>();
    Reference->SetStringField(TEXT("kind"), Kind);
    Reference->SetStringField(
        TEXT("id"),
        Id.ToString(EGuidFormats::DigitsWithHyphensLower));
    return Reference;
}

TSharedRef<FJsonObject> PublicPathLocalReference(const FString& Alias)
{
    TSharedRef<FJsonObject> Reference = MakeShared<FJsonObject>();
    Reference->SetStringField(TEXT("kind"), TEXT("local"));
    Reference->SetStringField(TEXT("name"), Alias);
    return Reference;
}

TSharedRef<FJsonObject> PublicPathMemberReference(
    const FString& Alias,
    const FString& Property)
{
    TSharedRef<FJsonObject> Reference = MakeShared<FJsonObject>();
    Reference->SetStringField(TEXT("kind"), TEXT("member"));
    Reference->SetObjectField(
        TEXT("object"),
        PublicPathLocalReference(Alias));
    Reference->SetArrayField(
        TEXT("path"),
        {MakeShared<FJsonValueString>(Property)});
    return Reference;
}

TSharedRef<FJsonObject> PublicPathClassDefaultDryRunArguments(
    const FString& ClassPath,
    const FString& DesiredValue)
{
    static const FString Alias = TEXT("actorClass");

    TSharedRef<FJsonObject> Set = MakeShared<FJsonObject>();
    Set->SetStringField(TEXT("kind"), TEXT("set"));
    Set->SetObjectField(
        TEXT("target"),
        PublicPathMemberReference(Alias, TEXT("InitialLifeSpan")));
    Set->SetStringField(TEXT("value"), DesiredValue);

    TSharedRef<FJsonObject> Patch = MakeShared<FJsonObject>();
    Patch->SetStringField(TEXT("kind"), TEXT("patch"));
    Patch->SetObjectField(
        TEXT("target"),
        PublicPathTarget(Alias, PublicPathClassCall(ClassPath)));
    Patch->SetBoolField(TEXT("dryRun"), true);
    Patch->SetArrayField(
        TEXT("statements"),
        {MakeShared<FJsonValueObject>(Set)});

    TSharedRef<FJsonObject> Arguments = MakeShared<FJsonObject>();
    Arguments->SetObjectField(TEXT("object"), Patch);
    return Arguments;
}

bool PublicPathResultBool(
    const TSharedPtr<FJsonObject>& Result,
    const TCHAR* Field,
    const bool Default = false)
{
    bool Value = Default;
    return Result.IsValid() && Result->TryGetBoolField(Field, Value)
        ? Value
        : Default;
}

bool PublicPathHasDiagnosticCode(
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
    for (const TSharedPtr<FJsonValue>& DiagnosticValue : *Diagnostics)
    {
        const TSharedPtr<FJsonObject>* Diagnostic = nullptr;
        FString Code;
        if (DiagnosticValue.IsValid()
            && DiagnosticValue->TryGetObject(Diagnostic)
            && Diagnostic != nullptr
            && (*Diagnostic)->TryGetStringField(TEXT("code"), Code)
            && Code == ExpectedCode)
        {
            return true;
        }
    }
    return false;
}

bool PublicPathHasError(
    const TSharedPtr<FJsonObject>& Result)
{
    const TArray<TSharedPtr<FJsonValue>>* Diagnostics = nullptr;
    if (!Result.IsValid()
        || !Result->TryGetArrayField(TEXT("diagnostics"), Diagnostics)
        || Diagnostics == nullptr)
    {
        return true;
    }
    for (const TSharedPtr<FJsonValue>& DiagnosticValue : *Diagnostics)
    {
        const TSharedPtr<FJsonObject>* Diagnostic = nullptr;
        FString Severity;
        if (DiagnosticValue.IsValid()
            && DiagnosticValue->TryGetObject(Diagnostic)
            && Diagnostic != nullptr
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

bool PublicPathHasComment(
    const TSharedPtr<FJsonObject>& Result,
    const FString& Expected)
{
    const TSharedPtr<FJsonObject>* Object = nullptr;
    const TArray<TSharedPtr<FJsonValue>>* Statements = nullptr;
    if (!Result.IsValid()
        || !Result->TryGetObjectField(TEXT("object"), Object)
        || Object == nullptr
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
            && (*Statement)->TryGetStringField(TEXT("kind"), Kind)
            && Kind == TEXT("comment")
            && (*Statement)->TryGetStringField(TEXT("text"), Text)
            && Text.Contains(Expected))
        {
            return true;
        }
    }
    return false;
}

bool PublicPathHasCallPath(
    const TSharedPtr<FJsonObject>& Result,
    const FString& Callee,
    const FString& ExpectedPath)
{
    const TSharedPtr<FJsonObject>* Object = nullptr;
    const TArray<TSharedPtr<FJsonValue>>* Statements = nullptr;
    if (!Result.IsValid()
        || !Result->TryGetObjectField(TEXT("object"), Object)
        || Object == nullptr
        || !(*Object)->TryGetArrayField(TEXT("statements"), Statements)
        || Statements == nullptr)
    {
        return false;
    }
    for (const TSharedPtr<FJsonValue>& StatementValue : *Statements)
    {
        const TSharedPtr<FJsonObject>* Statement = nullptr;
        const TSharedPtr<FJsonObject>* Value = nullptr;
        const TSharedPtr<FJsonObject>* Args = nullptr;
        FString ActualCallee;
        FString ActualPath;
        if (StatementValue.IsValid()
            && StatementValue->TryGetObject(Statement)
            && Statement != nullptr
            && (*Statement)->TryGetObjectField(TEXT("value"), Value)
            && Value != nullptr
            && (*Value)->TryGetStringField(TEXT("callee"), ActualCallee)
            && ActualCallee == Callee
            && (*Value)->TryGetObjectField(TEXT("args"), Args)
            && Args != nullptr
            && (*Args)->TryGetStringField(TEXT("path"), ActualPath)
            && ActualPath == ExpectedPath)
        {
            return true;
        }
    }
    return false;
}

bool PublicPathHasCallId(
    const TSharedPtr<FJsonObject>& Result,
    const FString& Callee,
    const FString& ExpectedId)
{
    const TSharedPtr<FJsonObject>* Object = nullptr;
    const TArray<TSharedPtr<FJsonValue>>* Statements = nullptr;
    if (!Result.IsValid()
        || !Result->TryGetObjectField(TEXT("object"), Object)
        || Object == nullptr
        || !(*Object)->TryGetArrayField(TEXT("statements"), Statements)
        || Statements == nullptr)
    {
        return false;
    }
    for (const TSharedPtr<FJsonValue>& StatementValue : *Statements)
    {
        const TSharedPtr<FJsonObject>* Statement = nullptr;
        const TSharedPtr<FJsonObject>* Value = nullptr;
        const TSharedPtr<FJsonObject>* Args = nullptr;
        FString ActualCallee;
        FString ActualId;
        if (StatementValue.IsValid()
            && StatementValue->TryGetObject(Statement)
            && Statement != nullptr
            && (*Statement)->TryGetObjectField(TEXT("value"), Value)
            && Value != nullptr
            && (*Value)->TryGetStringField(
                TEXT("callee"),
                ActualCallee)
            && ActualCallee == Callee
            && (*Value)->TryGetObjectField(TEXT("args"), Args)
            && Args != nullptr
            && (*Args)->TryGetStringField(TEXT("id"), ActualId)
            && ActualId == ExpectedId)
        {
            return true;
        }
    }
    return false;
}

bool IsValidPublicPathOutgoingResult(
    const TSharedPtr<FJsonObject>& Result,
    FString& OutError)
{
    TSharedPtr<FJsonObject> ValidationError;
    if (FSalJson::ValidateResult(Result, ValidationError))
    {
        OutError.Reset();
        return true;
    }

    OutError = TEXT("Result failed SAL outgoing validation.");
    const TArray<TSharedPtr<FJsonValue>>* Diagnostics = nullptr;
    if (ValidationError.IsValid()
        && ValidationError->TryGetArrayField(TEXT("diagnostics"), Diagnostics)
        && Diagnostics != nullptr
        && !Diagnostics->IsEmpty())
    {
        const TSharedPtr<FJsonObject>* Diagnostic = nullptr;
        FString Message;
        if ((*Diagnostics)[0].IsValid()
            && (*Diagnostics)[0]->TryGetObject(Diagnostic)
            && Diagnostic != nullptr
            && (*Diagnostic)->TryGetStringField(TEXT("message"), Message))
        {
            OutError += TEXT(" ") + Message;
        }
    }
    return false;
}

bool UnloadPublicPathFixturePackage(
    UPackage* Package,
    FString& OutError)
{
    OutError.Reset();
    if (Package == nullptr)
    {
        return true;
    }

    const FString PackageName = Package->GetName();
    Package->SetDirtyFlag(false);
    Package->ClearFlags(RF_Public | RF_Standalone);
    ForEachObjectWithPackage(
        Package,
        [](UObject* Object)
        {
            Object->ClearFlags(RF_Public | RF_Standalone);
            return true;
        },
        true);

    CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);
    if (FindPackage(nullptr, *PackageName) != nullptr)
    {
        OutError = TEXT("Fixture package remained loaded: ") + PackageName;
        return false;
    }
    return true;
}

class FPublicPathClassFixture
{
public:
    FPublicPathClassFixture()
    {
        const FString Token =
            FGuid::NewGuid().ToString(EGuidFormats::Digits);
        const FString PackageName =
            TEXT("/Game/LoomleTests/SalModulePublicPath_") + Token;
        const FString BlueprintName =
            TEXT("BP_SalModulePublicPath_") + Token;

        Package = CreatePackage(*PackageName);
        Blueprint = FKismetEditorUtilities::CreateBlueprint(
            AActor::StaticClass(),
            Package,
            FName(*BlueprintName),
            BPTYPE_Normal,
            UBlueprint::StaticClass(),
            UBlueprintGeneratedClass::StaticClass(),
            NAME_None);
        if (Blueprint != nullptr)
        {
            const FName VariableName(TEXT("PublicPathValue"));
            const FEdGraphPinType VariableType(
                UEdGraphSchema_K2::PC_Int,
                NAME_None,
                nullptr,
                EPinContainerType::None,
                false,
                FEdGraphTerminalType());
            FBlueprintEditorUtils::AddMemberVariable(
                Blueprint,
                VariableName,
                VariableType);
            VariableId =
                FBlueprintEditorUtils::FindMemberVariableGuidByName(
                    Blueprint,
                    VariableName);
            Graph =
                FBlueprintEditorUtils::FindEventGraph(Blueprint);
            if (Graph != nullptr)
            {
                FGraphNodeCreator<UK2Node_CustomEvent> NodeCreator(
                    *Graph);
                Node = NodeCreator.CreateNode(false);
                Node->CustomFunctionName =
                    TEXT("PublicPathRoutingEvent");
                Node->NodePosX = 100;
                Node->NodePosY = 200;
                NodeCreator.Finalize();
            }
            FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(
                Blueprint);
            FKismetEditorUtilities::CompileBlueprint(Blueprint);
            FAssetRegistryModule::AssetCreated(Blueprint);
            bRegistered = true;
        }
        Class = Blueprint != nullptr
            ? Cast<UBlueprintGeneratedClass>(Blueprint->GeneratedClass)
            : nullptr;
        ActorCDO = Class != nullptr
            ? Cast<AActor>(Class->GetDefaultObject())
            : nullptr;
        if (Package != nullptr)
        {
            Package->SetDirtyFlag(false);
        }
    }

    ~FPublicPathClassFixture()
    {
        FString Ignored;
        Cleanup(Ignored);
    }

    FPublicPathClassFixture(const FPublicPathClassFixture&) = delete;
    FPublicPathClassFixture& operator=(const FPublicPathClassFixture&) = delete;

    bool Cleanup(FString& OutError)
    {
        if (bCleaned)
        {
            OutError.Reset();
            return true;
        }
        bCleaned = true;

        UPackage* PackageToUnload = Package;
        if (Blueprint != nullptr && bRegistered)
        {
            FAssetRegistryModule::AssetDeleted(Blueprint);
            bRegistered = false;
        }
        if (Blueprint != nullptr)
        {
            Blueprint->ClearFlags(RF_Public | RF_Standalone);
        }
        ActorCDO = nullptr;
        Node = nullptr;
        Graph = nullptr;
        Class = nullptr;
        Blueprint = nullptr;
        Package = nullptr;
        return UnloadPublicPathFixturePackage(PackageToUnload, OutError);
    }

    UPackage* Package = nullptr;
    UBlueprint* Blueprint = nullptr;
    UBlueprintGeneratedClass* Class = nullptr;
    AActor* ActorCDO = nullptr;
    UEdGraph* Graph = nullptr;
    UK2Node_CustomEvent* Node = nullptr;
    FGuid VariableId;

private:
    bool bRegistered = false;
    bool bCleaned = false;
};

class FPublicPathWidgetFixture
{
public:
    FPublicPathWidgetFixture()
    {
        const FString Token =
            FGuid::NewGuid().ToString(EGuidFormats::Digits);
        Package = CreatePackage(*FString::Printf(
            TEXT("/Game/LoomleTests/PublicPathWidget_%s"),
            *Token));
        Blueprint = Cast<UWidgetBlueprint>(
            FKismetEditorUtilities::CreateBlueprint(
                UUserWidget::StaticClass(),
                Package,
                *FString::Printf(
                    TEXT("WBP_PublicPathWidget_%s"),
                    *Token),
                BPTYPE_Normal,
                UWidgetBlueprint::StaticClass(),
                UWidgetBlueprintGeneratedClass::StaticClass(),
                NAME_None));
        if (Blueprint != nullptr
            && Blueprint->WidgetTree != nullptr)
        {
            Root = Blueprint->WidgetTree
                ->ConstructWidget<UCanvasPanel>(
                    UCanvasPanel::StaticClass(),
                    RootName);
            if (Root != nullptr)
            {
                Root->bIsVariable = false;
                Blueprint->WidgetTree->RootWidget = Root;
                Blueprint->OnVariableAdded(RootName);
                FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(
                    Blueprint);
                FKismetEditorUtilities::CompileBlueprint(Blueprint);
                Root = Cast<UCanvasPanel>(
                    Blueprint->WidgetTree->FindWidget(RootName));
                RootId =
                    Blueprint->WidgetVariableNameToGuidMap.FindRef(
                        RootName);
            }
        }
        if (Package != nullptr)
        {
            Package->SetDirtyFlag(false);
        }
    }

    ~FPublicPathWidgetFixture()
    {
        FString Ignored;
        Cleanup(Ignored);
    }

    bool Cleanup(FString& OutError)
    {
        if (bCleaned)
        {
            OutError.Reset();
            return true;
        }
        bCleaned = true;
        UPackage* PackageToUnload = Package;
        if (Blueprint != nullptr)
        {
            Blueprint->ClearFlags(RF_Public | RF_Standalone);
        }
        Root = nullptr;
        Blueprint = nullptr;
        Package = nullptr;
        return UnloadPublicPathFixturePackage(
            PackageToUnload,
            OutError);
    }

    bool IsValid() const
    {
        return Blueprint != nullptr
            && Root != nullptr
            && RootId.IsValid()
            && Blueprint->GeneratedClass != nullptr
            && Blueprint->Status != BS_Error;
    }

    static const FName RootName;
    UPackage* Package = nullptr;
    UWidgetBlueprint* Blueprint = nullptr;
    UCanvasPanel* Root = nullptr;
    FGuid RootId;

private:
    bool bCleaned = false;
};

const FName FPublicPathWidgetFixture::RootName(
    TEXT("PublicPathRoot"));

class FPublicPathStateTreeFixture
{
public:
    FPublicPathStateTreeFixture()
    {
        const FString Token =
            FGuid::NewGuid().ToString(EGuidFormats::Digits);
        Package = CreatePackage(*FString::Printf(
            TEXT("/Game/LoomleTests/PublicPathState_%s"),
            *Token));
        Asset = NewObject<UStateTree>(
            Package,
            FName(TEXT("ST_PublicPath")),
            RF_Public | RF_Standalone | RF_Transactional);
        if (Asset != nullptr)
        {
            EditorData = NewObject<UStateTreeEditorData>(
                Asset,
                NAME_None,
                RF_Transactional);
            Asset->EditorData = EditorData;
            if (EditorData != nullptr)
            {
                Root = &EditorData->AddSubTree(
                    FName(TEXT("Root")));
                Root->ID = FGuid::NewGuid();
            }
        }
        if (Package != nullptr)
        {
            Package->SetDirtyFlag(false);
        }
    }

    ~FPublicPathStateTreeFixture()
    {
        FString Ignored;
        Cleanup(Ignored);
    }

    bool Cleanup(FString& OutError)
    {
        if (bCleaned)
        {
            OutError.Reset();
            return true;
        }
        bCleaned = true;
        UPackage* PackageToUnload = Package;
        Root = nullptr;
        EditorData = nullptr;
        if (Asset != nullptr)
        {
            Asset->ClearFlags(RF_Public | RF_Standalone);
        }
        Asset = nullptr;
        Package = nullptr;
        return UnloadPublicPathFixturePackage(
            PackageToUnload,
            OutError);
    }

    bool IsValid() const
    {
        return Asset != nullptr
            && EditorData != nullptr
            && Root != nullptr
            && Root->ID.IsValid();
    }

    UPackage* Package = nullptr;
    UStateTree* Asset = nullptr;
    UStateTreeEditorData* EditorData = nullptr;
    UStateTreeState* Root = nullptr;

private:
    bool bCleaned = false;
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FSalModuleQueryPublicPathTest,
    "Loomle.Sal.PublicPath.Query.DecodeResolveDispatchAndValidate",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSalModuleQueryPublicPathTest::RunTest(const FString& Parameters)
{
    UClass* Class = AActor::StaticClass();
    const TSharedPtr<FJsonObject> Result =
        FSalModule::BuildQueryResult(
            PublicPathQueryArguments(
                TEXT("actorClass"),
                PublicPathClassCall(Class->GetPathName()),
                TEXT("summary")));

    FString ValidationError;
    const bool bOutgoingValid =
        IsValidPublicPathOutgoingResult(Result, ValidationError);
    TestTrue(
        *FString::Printf(
            TEXT("Public Query result satisfies the outgoing SAL contract: %s"),
            *ValidationError),
        bOutgoingValid);
    TestFalse(
        TEXT("Successful Query does not return a MutationResult"),
        Result.IsValid() && Result->HasField(TEXT("isError")));
    TestTrue(
        TEXT("Decoded Query resolves and dispatches to the Class interface"),
        PublicPathHasCallPath(
            Result,
            TEXT("class"),
            Class->GetPathName()));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FSalModulePatchDryRunPublicPathTest,
    "Loomle.Sal.PublicPath.Patch.DryRunDecodeResolveDispatchAndValidate",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSalModulePatchDryRunPublicPathTest::RunTest(const FString& Parameters)
{
    FPublicPathClassFixture Fixture;
    TestNotNull(
        TEXT("Public Patch fixture creates a Blueprint Generated Class"),
        Fixture.Class);
    TestNotNull(
        TEXT("Public Patch fixture creates an Actor CDO"),
        Fixture.ActorCDO);
    if (Fixture.Class == nullptr || Fixture.ActorCDO == nullptr)
    {
        return false;
    }

    const float Before = Fixture.ActorCDO->InitialLifeSpan;
    const float Desired = Before + 23.5f;
    const TSharedPtr<FJsonObject> Result =
        FSalModule::BuildPatchResult(
            PublicPathClassDefaultDryRunArguments(
                Fixture.Class->GetPathName(),
                FString::SanitizeFloat(Desired)));

    FString ValidationError;
    const bool bOutgoingValid =
        IsValidPublicPathOutgoingResult(Result, ValidationError);
    TestTrue(
        *FString::Printf(
            TEXT("Public Patch result satisfies the outgoing SAL contract: %s"),
            *ValidationError),
        bOutgoingValid);
    TestFalse(
        TEXT("Successful public Patch is not an error"),
        PublicPathResultBool(Result, TEXT("isError"), true));
    TestTrue(
        TEXT("Public Patch validates after decode, resolution, and dispatch"),
        PublicPathResultBool(Result, TEXT("valid")));
    TestTrue(
        TEXT("Public Patch preserves dryRun=true"),
        PublicPathResultBool(Result, TEXT("dryRun")));
    TestFalse(
        TEXT("Public dry-run Patch reports applied=false"),
        PublicPathResultBool(Result, TEXT("applied"), true));
    TestTrue(
        TEXT("Public dry-run Patch preserves the live generated CDO"),
        FMath::IsNearlyEqual(Fixture.ActorCDO->InitialLifeSpan, Before));
    TestFalse(
        TEXT("Public dry-run Patch preserves Package dirty state"),
        Fixture.Package->IsDirty());

    FString CleanupError;
    const bool bCleaned = Fixture.Cleanup(CleanupError);
    TestTrue(
        *FString::Printf(
            TEXT("Public Patch fixture unloads cleanly: %s"),
            *CleanupError),
        bCleaned);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FSalModuleInvalidLocatorPublicPathTest,
    "Loomle.Sal.PublicPath.Query.InvalidLocator",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSalModuleInvalidLocatorPublicPathTest::RunTest(const FString& Parameters)
{
    const TSharedPtr<FJsonObject> Result =
        FSalModule::BuildQueryResult(
            PublicPathQueryArguments(
                TEXT("invalidClass"),
                PublicPathClassCall(FString()),
                TEXT("summary")));

    FString ValidationError;
    const bool bOutgoingValid =
        IsValidPublicPathOutgoingResult(Result, ValidationError);
    TestTrue(
        *FString::Printf(
            TEXT("Resolver failure still satisfies the outgoing SAL contract: %s"),
            *ValidationError),
        bOutgoingValid);
    TestTrue(
        TEXT("Decoded invalid locator reaches TargetResolver"),
        PublicPathHasDiagnosticCode(
            Result,
            TEXT("validation.invalid_target_locator")));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FSalModuleMalformedQueryPublicPathTest,
    "Loomle.Sal.PublicPath.Query.MalformedNormalizedObject",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSalModuleMalformedQueryPublicPathTest::RunTest(const FString& Parameters)
{
    TSharedRef<FJsonObject> MalformedQuery = MakeShared<FJsonObject>();
    MalformedQuery->SetStringField(TEXT("kind"), TEXT("query"));
    MalformedQuery->SetStringField(TEXT("unsupported"), TEXT("field"));
    TSharedRef<FJsonObject> Arguments = MakeShared<FJsonObject>();
    Arguments->SetObjectField(TEXT("object"), MalformedQuery);

    const TSharedPtr<FJsonObject> Result =
        FSalModule::BuildQueryResult(Arguments);
    FString ValidationError;
    const bool bOutgoingValid =
        IsValidPublicPathOutgoingResult(Result, ValidationError);
    TestTrue(
        *FString::Printf(
            TEXT("Decode failure still satisfies the outgoing SAL contract: %s"),
            *ValidationError),
        bOutgoingValid);
    TestTrue(
        TEXT("Malformed normalized Query is rejected by the SAL decoder"),
        PublicPathHasDiagnosticCode(
            Result,
            TEXT("language.invalid_object_shape")));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FSalModuleQueryRoutingMatrixTest,
    "Loomle.Sal.PublicPath.Query.NormalizedRoutingMatrix",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSalModuleQueryRoutingMatrixTest::RunTest(
    const FString& Parameters)
{
    FPublicPathClassFixture BlueprintFixture;
    FPublicPathWidgetFixture WidgetFixture;
    FPublicPathStateTreeFixture StateTreeFixture;
    TestNotNull(
        TEXT("Routing fixture creates an Actor Blueprint"),
        BlueprintFixture.Blueprint);
    TestNotNull(
        TEXT("Routing fixture creates an Event Graph"),
        BlueprintFixture.Graph);
    TestTrue(
        TEXT("Routing fixture creates a stable variable declaration"),
        BlueprintFixture.VariableId.IsValid());
    TestTrue(
        TEXT("Routing fixture creates a valid WidgetBlueprint"),
        WidgetFixture.IsValid());
    TestTrue(
        TEXT("Routing fixture creates a valid StateTree"),
        StateTreeFixture.IsValid());
    if (BlueprintFixture.Blueprint == nullptr
        || BlueprintFixture.Graph == nullptr
        || !BlueprintFixture.VariableId.IsValid()
        || !WidgetFixture.IsValid()
        || !StateTreeFixture.IsValid())
    {
        return false;
    }

    TSharedRef<FJsonObject> AssetCollectionOperation =
        PublicPathOperation(TEXT("assets"));
    AssetCollectionOperation->SetStringField(
        TEXT("text"),
        BlueprintFixture.Blueprint->GetName());
    const TSharedPtr<FJsonObject> AssetRoot =
        FSalModule::BuildQueryResult(
            PublicPathQueryArguments(
                TEXT("asset"),
                PublicPathAssetRoot(),
                AssetCollectionOperation));
    TestFalse(
        TEXT("Normalized Asset root Query dispatches successfully"),
        PublicPathHasError(AssetRoot));
    TestTrue(
        TEXT("Asset root Query returns the exact fixture identity"),
        PublicPathHasCallPath(
            AssetRoot,
            TEXT("asset"),
            BlueprintFixture.Blueprint->GetPathName()));

    const TSharedPtr<FJsonObject> ExactAsset =
        FSalModule::BuildQueryResult(
            PublicPathQueryArguments(
                TEXT("asset"),
                PublicPathAssetCall(
                    BlueprintFixture.Blueprint->GetPathName()),
                TEXT("assets")));
    TestTrue(
        TEXT("Exact Asset reaches the Asset interface and rejects collection-only Query"),
        PublicPathHasDiagnosticCode(
            ExactAsset,
            TEXT("capability.unsupported_query")));

    const TSharedPtr<FJsonObject> Blueprint =
        FSalModule::BuildQueryResult(
            PublicPathQueryArguments(
                TEXT("blueprint"),
                PublicPathBlueprintCall(
                    BlueprintFixture.Blueprint->GetPathName(),
                    BlueprintFixture.Blueprint->GetBlueprintGuid()),
                TEXT("summary")));
    TestFalse(
        TEXT("Normalized Blueprint Query dispatches successfully"),
        PublicPathHasError(Blueprint));
    TestTrue(
        TEXT("Blueprint Query returns its exact Blueprint locator"),
        PublicPathHasCallId(
            Blueprint,
            TEXT("blueprint"),
            BlueprintFixture.Blueprint->GetBlueprintGuid().ToString(
                EGuidFormats::DigitsWithHyphensLower)));

    const TSharedPtr<FJsonObject> Graph =
        FSalModule::BuildQueryResult(
            PublicPathQueryArguments(
                TEXT("graph"),
                PublicPathGraphCall(
                    BlueprintFixture.Blueprint->GetPathName(),
                    BlueprintFixture.Blueprint->GetBlueprintGuid(),
                    BlueprintFixture.Graph),
                TEXT("summary")));
    TestFalse(
        TEXT("Normalized Graph Query dispatches successfully"),
        PublicPathHasError(Graph));
    TestTrue(
        TEXT("Graph Query returns the exact Graph id"),
        PublicPathHasCallId(
            Graph,
            TEXT("graph"),
            BlueprintFixture.Graph->GraphGuid.ToString(
                EGuidFormats::DigitsWithHyphensLower)));

    const TSharedPtr<FJsonObject> Widget =
        FSalModule::BuildQueryResult(
            PublicPathQueryArguments(
                TEXT("widget_blueprint"),
                PublicPathBlueprintCall(
                    WidgetFixture.Blueprint->GetPathName(),
                    WidgetFixture.Blueprint->GetBlueprintGuid()),
                TEXT("summary")));
    TestFalse(
        TEXT("Composed WidgetBlueprint summary routes to Widget"),
        PublicPathHasError(Widget));
    TestTrue(
        TEXT("Composed Widget Query reports authored Widget counts"),
        PublicPathHasComment(Widget, TEXT("widgets: 1")));

    const TSharedPtr<FJsonObject> StateTree =
        FSalModule::BuildQueryResult(
            PublicPathQueryArguments(
                TEXT("tree"),
                PublicPathAssetCall(
                    StateTreeFixture.Asset->GetPathName(),
                    StateTreeFixture.Asset->GetClass()->GetPathName()),
                TEXT("summary")));
    TestFalse(
        TEXT("Composed StateTree Asset summary routes to StateTree"),
        PublicPathHasError(StateTree));
    TestTrue(
        TEXT("StateTree Query reports authored State counts"),
        PublicPathHasComment(
            StateTree,
            TEXT("states: 1")));

    TSharedRef<FJsonObject> References =
        PublicPathOperation(TEXT("references"));
    References->SetObjectField(
        TEXT("target"),
        PublicPathStableReference(
            TEXT("variable"),
            BlueprintFixture.VariableId));
    const TSharedPtr<FJsonObject> ReferenceResult =
        FSalModule::BuildQueryResult(
            PublicPathQueryArguments(
                TEXT("blueprint"),
                PublicPathBlueprintCall(
                    BlueprintFixture.Blueprint->GetPathName(),
                    BlueprintFixture.Blueprint->GetBlueprintGuid()),
                References));
    TestFalse(
        TEXT("references bypasses the owner interface and routes factually"),
        PublicPathHasError(ReferenceResult));

    const TArray<TSharedPtr<FJsonObject>> Results = {
        AssetRoot,
        ExactAsset,
        Blueprint,
        Graph,
        Widget,
        StateTree,
        ReferenceResult};
    for (int32 Index = 0; Index < Results.Num(); ++Index)
    {
        FString ValidationError;
        TestTrue(
            *FString::Printf(
                TEXT("Routing result %d satisfies the outgoing SAL contract: %s"),
                Index,
                *ValidationError),
            IsValidPublicPathOutgoingResult(
                Results[Index],
                ValidationError));
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FSalModulePatchRoutingMatrixTest,
    "Loomle.Sal.PublicPath.Patch.NormalizedDryRunRoutingMatrix",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSalModulePatchRoutingMatrixTest::RunTest(
    const FString& Parameters)
{
    if (GEditor == nullptr
        || GEditor->IsPlaySessionInProgress())
    {
        AddError(
            TEXT("Public Patch routing requires an idle Editor outside PIE."));
        return false;
    }

    FPublicPathClassFixture BlueprintFixture;
    FPublicPathWidgetFixture WidgetFixture;
    FPublicPathStateTreeFixture StateTreeFixture;
    if (BlueprintFixture.Blueprint == nullptr
        || BlueprintFixture.Graph == nullptr
        || BlueprintFixture.Node == nullptr
        || !BlueprintFixture.Node->NodeGuid.IsValid()
        || !WidgetFixture.IsValid()
        || !StateTreeFixture.IsValid())
    {
        AddError(TEXT("Public Patch routing fixtures are incomplete."));
        return false;
    }

    const FString DescriptionBefore =
        BlueprintFixture.Blueprint->BlueprintDescription;
    const FIntPoint NodeBefore(
        BlueprintFixture.Node->NodePosX,
        BlueprintFixture.Node->NodePosY);
    const bool bWidgetVariableBefore =
        WidgetFixture.Root->bIsVariable;

    const TSharedPtr<FJsonObject> Asset =
        FSalModule::BuildPatchResult(
            PublicPathPatchArguments(
                TEXT("asset"),
                PublicPathAssetCall(
                    BlueprintFixture.Blueprint->GetPathName()),
                {PublicPathStatement(
                    PublicPathSaveStatement())}));

    const TSharedPtr<FJsonObject> Blueprint =
        FSalModule::BuildPatchResult(
            PublicPathPatchArguments(
                TEXT("blueprint"),
                PublicPathBlueprintCall(
                    BlueprintFixture.Blueprint->GetPathName(),
                    BlueprintFixture.Blueprint->GetBlueprintGuid()),
                {PublicPathStatement(
                    PublicPathSetMemberStatement(
                        PublicPathLocalReference(TEXT("blueprint")),
                        TEXT("BlueprintDescription"),
                        MakeShared<FJsonValueString>(
                            TEXT("dry-run description"))))}));

    TSharedRef<FJsonObject> Move = MakeShared<FJsonObject>();
    Move->SetStringField(TEXT("kind"), TEXT("move"));
    Move->SetObjectField(
        TEXT("target"),
        PublicPathStableReference(
            TEXT("node"),
            BlueprintFixture.Node->NodeGuid));
    Move->SetArrayField(
        TEXT("by"),
        {
            MakeShared<FJsonValueNumber>(17),
            MakeShared<FJsonValueNumber>(23)
        });
    const TSharedPtr<FJsonObject> Graph =
        FSalModule::BuildPatchResult(
            PublicPathPatchArguments(
                TEXT("graph"),
                PublicPathGraphCall(
                    BlueprintFixture.Blueprint->GetPathName(),
                    BlueprintFixture.Blueprint->GetBlueprintGuid(),
                    BlueprintFixture.Graph),
                {PublicPathStatement(Move)}));

    const TSharedPtr<FJsonObject> Widget =
        FSalModule::BuildPatchResult(
            PublicPathPatchArguments(
                TEXT("widget_blueprint"),
                PublicPathBlueprintCall(
                    WidgetFixture.Blueprint->GetPathName(),
                    WidgetFixture.Blueprint->GetBlueprintGuid()),
                {PublicPathStatement(
                    PublicPathSetMemberStatement(
                        PublicPathStableReference(
                            TEXT("widget"),
                            WidgetFixture.RootId),
                        TEXT("bIsVariable"),
                        MakeShared<FJsonValueBoolean>(
                            !bWidgetVariableBefore)))}));

    const TSharedPtr<FJsonObject> StateTree =
        FSalModule::BuildPatchResult(
            PublicPathPatchArguments(
                TEXT("tree"),
                PublicPathAssetCall(
                    StateTreeFixture.Asset->GetPathName(),
                    StateTreeFixture.Asset->GetClass()->GetPathName()),
                {PublicPathStatement(
                    PublicPathSaveStatement())}));

    const TArray<TPair<FString, TSharedPtr<FJsonObject>>> Results = {
        {TEXT("Asset"), Asset},
        {TEXT("Blueprint"), Blueprint},
        {TEXT("Graph"), Graph},
        {TEXT("Widget"), Widget},
        {TEXT("StateTree"), StateTree}};
    for (const TPair<FString, TSharedPtr<FJsonObject>>& Entry : Results)
    {
        FString ValidationError;
        TestTrue(
            *FString::Printf(
                TEXT("%s Patch result satisfies outgoing SAL: %s"),
                *Entry.Key,
                *ValidationError),
            IsValidPublicPathOutgoingResult(
                Entry.Value,
                ValidationError));
        TestFalse(
            *FString::Printf(
                TEXT("%s normalized Patch reaches its interface"),
                *Entry.Key),
            PublicPathResultBool(
                Entry.Value,
                TEXT("isError"),
                true));
        TestTrue(
            *FString::Printf(
                TEXT("%s normalized Patch validates"),
                *Entry.Key),
            PublicPathResultBool(
                Entry.Value,
                TEXT("valid")));
        TestTrue(
            *FString::Printf(
                TEXT("%s normalized Patch remains a dry-run"),
                *Entry.Key),
            PublicPathResultBool(
                Entry.Value,
                TEXT("dryRun")));
        TestFalse(
            *FString::Printf(
                TEXT("%s normalized Patch does not apply"),
                *Entry.Key),
            PublicPathResultBool(
                Entry.Value,
                TEXT("applied"),
                true));
    }

    TestEqual(
        TEXT("Blueprint dry-run preserves its authored field"),
        BlueprintFixture.Blueprint->BlueprintDescription,
        DescriptionBefore);
    TestTrue(
        TEXT("Graph dry-run preserves native node layout"),
        BlueprintFixture.Node->NodePosX == NodeBefore.X
            && BlueprintFixture.Node->NodePosY == NodeBefore.Y);
    TestEqual(
        TEXT("Widget dry-run preserves native Widget state"),
        WidgetFixture.Root->bIsVariable,
        bWidgetVariableBefore);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FSalModuleQueryResultSizeGateTest,
    "Loomle.Sal.PublicPath.Query.FinalResultSizeGate",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSalModuleQueryResultSizeGateTest::RunTest(
    const FString& Parameters)
{
    FPublicPathClassFixture Fixture;
    TestNotNull(
        TEXT("Result-size fixture creates a generated Class"),
        Fixture.Class);
    if (Fixture.Class == nullptr)
    {
        return false;
    }

    const FName MetadataKey(TEXT("LoomleOversizedResult"));
    Fixture.Class->SetMetaData(
        MetadataKey,
        *FString::ChrN(140 * 1024, TEXT('x')));
    const TSharedPtr<FJsonObject> Result =
        FSalModule::BuildQueryResult(
            PublicPathQueryArguments(
                TEXT("actorClass"),
                PublicPathClassCall(
                    Fixture.Class->GetPathName()),
                TEXT("summary")));
    Fixture.Class->RemoveMetaData(MetadataKey);

    FString ValidationError;
    TestTrue(
        *FString::Printf(
            TEXT("Oversized replacement result remains contract-valid: %s"),
            *ValidationError),
        IsValidPublicPathOutgoingResult(
            Result,
            ValidationError));
    TestTrue(
        TEXT("Final public Query rejects output beyond 128 KiB"),
        PublicPathHasDiagnosticCode(
            Result,
            TEXT("validation.result_too_large")));
    TestFalse(
        TEXT("Oversized public Query returns no partial object"),
        Result.IsValid() && Result->HasField(TEXT("object")));
    return true;
}

#endif
