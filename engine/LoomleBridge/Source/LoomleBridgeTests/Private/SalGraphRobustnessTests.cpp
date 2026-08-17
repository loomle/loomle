// Copyright 2026 Loomle contributors.

#if WITH_DEV_AUTOMATION_TESTS

#include "Sal/Blueprint/SalBlueprintInterface.h"
#include "Sal/Graph/SalGraphInterface.h"
#include "SalTestObjectModel.h"
#include "LoomleTestEditorState.h"

#include "Animation/AnimBlueprint.h"
#include "Animation/AnimBlueprintGeneratedClass.h"
#include "Animation/AnimInstance.h"
#include "Animation/Skeleton.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "BlueprintActionDatabase.h"
#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetBlueprintGeneratedClass.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Button.h"
#include "Components/CanvasPanel.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraphSchema_K2.h"
#include "Editor.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Engine/SCS_Node.h"
#include "Engine/SimpleConstructionScript.h"
#include "GameFramework/Actor.h"
#include "HAL/FileManager.h"
#include "K2Node_CallFunction.h"
#include "K2Node_CallFunctionOnMember.h"
#include "K2Node_CustomEvent.h"
#include "K2Node_IfThenElse.h"
#include "K2Node_MacroInstance.h"
#include "K2Node_MakeArray.h"
#include "K2Node_GetSubsystem.h"
#include "K2Node_VariableGet.h"
#include "K2Node_VariableSet.h"
#include "Kismet/KismetMathLibrary.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Subsystems/AudioEngineSubsystem.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Subsystems/LocalPlayerSubsystem.h"
#include "Subsystems/WorldSubsystem.h"
#include "Misc/AutomationTest.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "PackageTools.h"
#include "UObject/Package.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/UObjectHash.h"
#include "WidgetBlueprint.h"

namespace
{
using namespace Loomle::Sal;

FString RobustGraphGuidText(const FGuid& Guid)
{
    return Guid.ToString(EGuidFormats::DigitsWithHyphensLower);
}

bool RobustGraphRequireIdleEditor(
    FAutomationTestBase& Test,
    const FString& Surface)
{
    if (GEditor == nullptr)
    {
        Test.AddError(Surface + TEXT(" requires GEditor."));
        return false;
    }
    if (GEditor->IsPlaySessionInProgress()
        || GEditor->IsTransactionActive())
    {
        Test.AddError(
            Surface
            + TEXT(" requires an idle Editor outside PIE and outside another transaction."));
        return false;
    }
    return true;
}

bool RobustGraphResultBool(
    const TSharedPtr<FJsonObject>& Result,
    const TCHAR* Field,
    const bool Default = false)
{
    bool Value = Default;
    return Result.IsValid()
        && Result->TryGetBoolField(Field, Value)
        ? Value
        : Default;
}

bool RobustGraphHasError(const TSharedPtr<FJsonObject>& Result)
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

bool RobustGraphHasDiagnosticCode(
    const TSharedPtr<FJsonObject>& Result,
    const FString& Code)
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
        FString Actual;
        if (Value.IsValid()
            && Value->TryGetObject(Diagnostic)
            && Diagnostic != nullptr
            && (*Diagnostic)->TryGetStringField(TEXT("code"), Actual)
            && Actual == Code)
        {
            return true;
        }
    }
    return false;
}

FString RobustGraphDiagnosticsText(
    const TSharedPtr<FJsonObject>& Result)
{
    const TArray<TSharedPtr<FJsonValue>>* Diagnostics = nullptr;
    if (!Result.IsValid())
    {
        return TEXT("result unavailable");
    }
    if (!Result->TryGetArrayField(TEXT("diagnostics"), Diagnostics)
        || Diagnostics == nullptr
        || Diagnostics->IsEmpty())
    {
        return TEXT("no diagnostics");
    }
    TArray<FString> Lines;
    for (const TSharedPtr<FJsonValue>& Value : *Diagnostics)
    {
        const TSharedPtr<FJsonObject>* Diagnostic = nullptr;
        if (!Value.IsValid()
            || !Value->TryGetObject(Diagnostic)
            || Diagnostic == nullptr)
        {
            Lines.Add(TEXT("<invalid diagnostic>"));
            continue;
        }
        FString Severity;
        FString Code;
        FString Message;
        (*Diagnostic)->TryGetStringField(TEXT("severity"), Severity);
        (*Diagnostic)->TryGetStringField(TEXT("code"), Code);
        (*Diagnostic)->TryGetStringField(TEXT("message"), Message);
        Lines.Add(FString::Printf(
            TEXT("%s %s: %s"),
            *Severity,
            *Code,
            *Message));
    }
    return FString::Join(Lines, TEXT(" | "));
}

TArray<TSharedPtr<FJsonObject>> RobustGraphCallArgs(
    const TSharedPtr<FJsonObject>& Result,
    const FString& Callee)
{
    TArray<TSharedPtr<FJsonObject>> Calls;
    const TSharedPtr<FJsonObject>* Object = nullptr;
    const TArray<TSharedPtr<FJsonValue>>* Statements = nullptr;
    if (!Result.IsValid()
        || !Result->TryGetObjectField(TEXT("object"), Object)
        || Object == nullptr
        || !(*Object)->TryGetArrayField(TEXT("statements"), Statements)
        || Statements == nullptr)
    {
        return Calls;
    }
    for (const TSharedPtr<FJsonValue>& StatementValue : *Statements)
    {
        const TSharedPtr<FJsonObject>* Statement = nullptr;
        const TSharedPtr<FJsonObject>* Call = nullptr;
        const TSharedPtr<FJsonObject>* Args = nullptr;
        if (StatementValue.IsValid()
            && StatementValue->TryGetObject(Statement)
            && Statement != nullptr
            && (*Statement)->TryGetObjectField(TEXT("value"), Call)
            && Call != nullptr
            && Loomle::Tests::Sal::TryReadObjectExpr(
                *Call,
                Callee,
                Args))
        {
            Calls.Add(*Args);
        }
    }
    return Calls;
}

bool RobustGraphContainsCallId(
    const TSharedPtr<FJsonObject>& Result,
    const FString& Callee,
    const FGuid& Id)
{
    const FString Expected = RobustGraphGuidText(Id);
    for (const TSharedPtr<FJsonObject>& Args :
         RobustGraphCallArgs(Result, Callee))
    {
        FString Actual;
        if (Args.IsValid()
            && Args->TryGetStringField(TEXT("id"), Actual)
            && Actual.Equals(Expected, ESearchCase::IgnoreCase))
        {
            return true;
        }
    }
    return false;
}

bool RobustGraphHasComment(
    const TSharedPtr<FJsonObject>& Result,
    const FString& Needle)
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
            && Text.Contains(Needle))
        {
            return true;
        }
    }
    return false;
}

int32 RobustGraphEdgeCount(const TSharedPtr<FJsonObject>& Result)
{
    const TSharedPtr<FJsonObject>* Object = nullptr;
    const TArray<TSharedPtr<FJsonValue>>* Statements = nullptr;
    if (!Result.IsValid()
        || !Result->TryGetObjectField(TEXT("object"), Object)
        || Object == nullptr
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
            && (*Statement)->HasTypedField<EJson::Object>(TEXT("from"))
            && (*Statement)->HasTypedField<EJson::Object>(TEXT("to")))
        {
            ++Count;
        }
    }
    return Count;
}

FString RobustGraphNextCursor(
    const TSharedPtr<FJsonObject>& Result)
{
    const TSharedPtr<FJsonObject>* Page = nullptr;
    FString Cursor;
    if (Result.IsValid()
        && Result->TryGetObjectField(TEXT("page"), Page)
        && Page != nullptr)
    {
        (*Page)->TryGetStringField(TEXT("next"), Cursor);
    }
    return Cursor;
}

TSharedRef<FJsonObject> RobustGraphLocal(const FString& Name)
{
    TSharedRef<FJsonObject> Ref = MakeShared<FJsonObject>();
    Ref->SetStringField(TEXT("kind"), TEXT("local"));
    Ref->SetStringField(TEXT("name"), Name);
    return Ref;
}

TSharedRef<FJsonObject> RobustGraphTyped(
    const FString& Kind,
    const FGuid& Id)
{
    TSharedRef<FJsonObject> Ref = MakeShared<FJsonObject>();
    Ref->SetStringField(TEXT("kind"), Kind);
    Ref->SetStringField(TEXT("id"), RobustGraphGuidText(Id));
    return Ref;
}

TSharedRef<FJsonObject> RobustGraphMember(
    const TSharedRef<FJsonObject>& Owner,
    const FString& Field)
{
    TSharedRef<FJsonObject> Ref = MakeShared<FJsonObject>();
    Ref->SetStringField(TEXT("kind"), TEXT("member"));
    Ref->SetObjectField(TEXT("object"), Owner);
    Ref->SetArrayField(
        TEXT("path"),
        {MakeShared<FJsonValueString>(Field)});
    return Ref;
}

TSharedRef<FJsonValue> RobustGraphBinding(
    const FString& Alias,
    const FString& Palette,
    const FString& Type = FString())
{
    TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
    Args->SetStringField(TEXT("palette"), Palette);
    if (!Type.IsEmpty())
    {
        Args->SetStringField(TEXT("type"), Type);
    }
    TSharedRef<FJsonObject> Call = MakeShared<FJsonObject>();
    Call->SetStringField(TEXT("kind"), TEXT("call"));
    Call->SetStringField(TEXT("callee"), TEXT("node"));
    Call->SetObjectField(TEXT("args"), Args);
    TSharedRef<FJsonObject> Statement = MakeShared<FJsonObject>();
    Statement->SetObjectField(
        TEXT("target"),
        RobustGraphLocal(Alias));
    Statement->SetObjectField(TEXT("value"), Call);
    return MakeShared<FJsonValueObject>(Statement);
}

TSharedRef<FJsonValue> RobustGraphUnary(
    const FString& Kind,
    const TSharedRef<FJsonObject>& Target)
{
    TSharedRef<FJsonObject> Statement = MakeShared<FJsonObject>();
    Statement->SetStringField(TEXT("kind"), Kind);
    Statement->SetObjectField(TEXT("target"), Target);
    return MakeShared<FJsonValueObject>(Statement);
}

TSharedRef<FJsonValue> RobustGraphEdgeOperation(
    const FString& Kind,
    const TSharedRef<FJsonObject>& From,
    const TSharedRef<FJsonObject>& To)
{
    TSharedRef<FJsonObject> Statement = MakeShared<FJsonObject>();
    Statement->SetStringField(TEXT("kind"), Kind);
    Statement->SetObjectField(TEXT("from"), From);
    Statement->SetObjectField(TEXT("to"), To);
    return MakeShared<FJsonValueObject>(Statement);
}

TSharedRef<FJsonValue> RobustGraphSet(
    const TSharedRef<FJsonObject>& Target,
    const TSharedPtr<FJsonValue>& Value)
{
    TSharedRef<FJsonObject> Statement = MakeShared<FJsonObject>();
    Statement->SetStringField(TEXT("kind"), TEXT("set"));
    Statement->SetObjectField(TEXT("target"), Target);
    Statement->SetField(TEXT("value"), Value);
    return MakeShared<FJsonValueObject>(Statement);
}

TSharedRef<FJsonValue> RobustGraphMoveTo(
    const TSharedRef<FJsonObject>& Target,
    const FIntPoint Position)
{
    TSharedRef<FJsonObject> Statement = MakeShared<FJsonObject>();
    Statement->SetStringField(TEXT("kind"), TEXT("move"));
    Statement->SetObjectField(TEXT("target"), Target);
    Statement->SetArrayField(
        TEXT("to"),
        {
            MakeShared<FJsonValueNumber>(Position.X),
            MakeShared<FJsonValueNumber>(Position.Y)
        });
    return MakeShared<FJsonValueObject>(Statement);
}

TSharedRef<FJsonValue> RobustGraphInsert(
    const TSharedRef<FJsonObject>& From,
    const TSharedRef<FJsonObject>& Input,
    const TSharedRef<FJsonObject>& Output,
    const TSharedRef<FJsonObject>& To)
{
    TSharedRef<FJsonObject> Statement = MakeShared<FJsonObject>();
    Statement->SetStringField(TEXT("kind"), TEXT("insert"));
    Statement->SetObjectField(TEXT("from"), From);
    Statement->SetObjectField(TEXT("input"), Input);
    Statement->SetObjectField(TEXT("output"), Output);
    Statement->SetObjectField(TEXT("to"), To);
    return MakeShared<FJsonValueObject>(Statement);
}

TSharedRef<FJsonValue> RobustGraphInvoke(
    const TSharedRef<FJsonObject>& Target,
    const FString& Operation,
    const TSharedPtr<FJsonObject>& Args = nullptr)
{
    TSharedRef<FJsonObject> Statement = MakeShared<FJsonObject>();
    Statement->SetStringField(TEXT("kind"), TEXT("invoke"));
    Statement->SetObjectField(TEXT("target"), Target);
    Statement->SetStringField(TEXT("operation"), Operation);
    if (Args.IsValid())
    {
        Statement->SetObjectField(TEXT("args"), Args);
    }
    return MakeShared<FJsonValueObject>(Statement);
}

FString RobustGraphPinTypeText(const FName Category)
{
    FEdGraphPinType Type;
    Type.PinCategory = Category;
    FString Text;
    FEdGraphPinType::StaticStruct()->ExportText(
        Text,
        &Type,
        nullptr,
        nullptr,
        PPF_None,
        nullptr);
    return Text;
}

FSalResolvedTarget RobustGraphTarget(
    UBlueprint* Blueprint,
    UEdGraph* Graph)
{
    FSalResolvedTarget Target;
    Target.Kind = ESalTargetKind::Graph;
    Target.Alias = TEXT("graph");
    Target.AssetPath =
        Blueprint != nullptr ? Blueprint->GetPathName() : FString();
    Target.Id =
        Graph != nullptr
            ? RobustGraphGuidText(Graph->GraphGuid)
            : FString();
    Target.Name = Graph != nullptr ? Graph->GetName() : FString();
    Target.Object = Graph;
    Target.Package =
        Blueprint != nullptr ? Blueprint->GetOutermost() : nullptr;
    Target.Blueprint = Blueprint;
    Target.Class =
        Blueprint != nullptr ? Blueprint->GeneratedClass.Get() : nullptr;
    Target.Graph = Graph;
    Target.Interfaces = {FName(TEXT("graph"))};
    return Target;
}

FSalResolvedTarget RobustGraphBlueprintTarget(UBlueprint* Blueprint)
{
    FSalResolvedTarget Target;
    Target.Kind = ESalTargetKind::Blueprint;
    Target.Alias = TEXT("blueprint");
    Target.AssetPath =
        Blueprint != nullptr ? Blueprint->GetPathName() : FString();
    Target.Id =
        Blueprint != nullptr
            ? RobustGraphGuidText(Blueprint->GetBlueprintGuid())
            : FString();
    Target.Object = Blueprint;
    Target.Package =
        Blueprint != nullptr ? Blueprint->GetOutermost() : nullptr;
    Target.Blueprint = Blueprint;
    Target.Class =
        Blueprint != nullptr ? Blueprint->GeneratedClass.Get() : nullptr;
    Target.Interfaces = {
        FName(TEXT("asset")),
        FName(TEXT("blueprint"))};
    return Target;
}

FSalQuery RobustGraphQuery(const FString& Kind)
{
    FSalQuery Query;
    Query.Alias = TEXT("graph");
    Query.Operation = MakeShared<FJsonObject>();
    Query.Operation->SetStringField(TEXT("kind"), Kind);
    return Query;
}

FSalQuery RobustGraphPaletteFromPin(
    const FString& Text,
    const UEdGraphPin* Pin)
{
    FSalQuery Query = RobustGraphQuery(TEXT("palette_entries"));
    Query.Operation->SetStringField(TEXT("text"), Text);
    TSharedRef<FJsonObject> PinContext = MakeShared<FJsonObject>();
    PinContext->SetStringField(TEXT("direction"), TEXT("from"));
    PinContext->SetObjectField(
        TEXT("pin"),
        RobustGraphTyped(
            TEXT("pin"),
            Pin != nullptr ? Pin->PinId : FGuid()));
    Query.Operation->SetObjectField(
        TEXT("pinContext"),
        PinContext);
    Query.PageLimit = 100;
    return Query;
}

FSalQuery RobustGraphTraversal(
    const FString& Kind,
    const FString& Direction,
    const TSharedRef<FJsonObject>& Target,
    const int32 Depth)
{
    FSalQuery Query = RobustGraphQuery(Kind);
    Query.Operation->SetObjectField(TEXT("target"), Target);
    if (!Direction.IsEmpty())
    {
        Query.Operation->SetStringField(
            TEXT("direction"),
            Direction);
    }
    Query.Operation->SetNumberField(TEXT("depth"), Depth);
    return Query;
}

UEdGraphPin* RobustGraphFindPin(
    UEdGraphNode* Node,
    const FName Name,
    const EEdGraphPinDirection Direction)
{
    return Node != nullptr ? Node->FindPin(Name, Direction) : nullptr;
}

UK2Node_CustomEvent* RobustGraphAddEvent(
    UEdGraph* Graph,
    const FName Name,
    const FIntPoint Position)
{
    if (Graph == nullptr)
    {
        return nullptr;
    }
    UK2Node_CustomEvent* Node =
        NewObject<UK2Node_CustomEvent>(
            Graph,
            NAME_None,
            RF_Transactional);
    Node->CustomFunctionName = Name;
    Node->CreateNewGuid();
    Node->NodePosX = Position.X;
    Node->NodePosY = Position.Y;
    Graph->AddNode(Node, false, false);
    Node->AllocateDefaultPins();
    return Node;
}

UK2Node_IfThenElse* RobustGraphAddBranch(
    UEdGraph* Graph,
    const FIntPoint Position)
{
    if (Graph == nullptr)
    {
        return nullptr;
    }
    UK2Node_IfThenElse* Node =
        NewObject<UK2Node_IfThenElse>(
            Graph,
            NAME_None,
            RF_Transactional);
    Node->CreateNewGuid();
    Node->NodePosX = Position.X;
    Node->NodePosY = Position.Y;
    Graph->AddNode(Node, false, false);
    Node->AllocateDefaultPins();
    return Node;
}

UK2Node_CallFunction* RobustGraphAddNot(
    UEdGraph* Graph,
    const FIntPoint Position)
{
    UFunction* Function =
        UKismetMathLibrary::StaticClass()->FindFunctionByName(
            GET_FUNCTION_NAME_CHECKED(
                UKismetMathLibrary,
                Not_PreBool));
    if (Graph == nullptr || Function == nullptr)
    {
        return nullptr;
    }
    UK2Node_CallFunction* Node =
        NewObject<UK2Node_CallFunction>(
            Graph,
            NAME_None,
            RF_Transactional);
    Node->CreateNewGuid();
    Node->NodePosX = Position.X;
    Node->NodePosY = Position.Y;
    Graph->AddNode(Node, false, false);
    Node->SetFromFunction(Function);
    Node->AllocateDefaultPins();
    return Node;
}

UK2Node_MacroInstance* RobustGraphAddForEachLoopWithBreak(
    UEdGraph* Graph,
    const FIntPoint Position)
{
    UBlueprint* StandardMacros = LoadObject<UBlueprint>(
        nullptr,
        TEXT("/Engine/EditorBlueprintResources/StandardMacros.StandardMacros"));
    UEdGraph* MacroGraph = nullptr;
    if (StandardMacros != nullptr)
    {
        for (UEdGraph* Candidate : StandardMacros->MacroGraphs)
        {
            if (Candidate != nullptr
                && Candidate->GetFName() == FName(TEXT("ForEachLoopWithBreak")))
            {
                MacroGraph = Candidate;
                break;
            }
        }
    }
    if (Graph == nullptr || MacroGraph == nullptr)
    {
        return nullptr;
    }

    UK2Node_MacroInstance* Node = NewObject<UK2Node_MacroInstance>(
        Graph,
        NAME_None,
        RF_Transactional);
    Node->SetMacroGraph(MacroGraph);
    Node->CreateNewGuid();
    Node->NodePosX = Position.X;
    Node->NodePosY = Position.Y;
    Graph->AddNode(Node, false, false);
    Node->AllocateDefaultPins();
    return Node;
}

UK2Node_MakeArray* RobustGraphAddMakeArray(
    UEdGraph* Graph,
    const FIntPoint Position)
{
    if (Graph == nullptr)
    {
        return nullptr;
    }
    UK2Node_MakeArray* Node = NewObject<UK2Node_MakeArray>(
        Graph,
        NAME_None,
        RF_Transactional);
    Node->CreateNewGuid();
    Node->NodePosX = Position.X;
    Node->NodePosY = Position.Y;
    Graph->AddNode(Node, false, false);
    Node->AllocateDefaultPins();
    return Node;
}

bool RobustGraphUnloadPackage(
    UPackage* Package,
    FString& OutError)
{
    OutError.Reset();
    if (Package == nullptr)
    {
        return true;
    }

    Package->SetDirtyFlag(false);
    TArray<UPackage*> Packages = {Package};
    UPackageTools::FUnloadPackageParams Params(Packages);
    Params.bUnloadDirtyPackages = true;
    // Fixture mutations use a disposable transactor that has already been
    // restored. Preserve the Editor's real Undo/Redo history while still
    // following UE's native Blueprint/package unload path.
    Params.bResetTransBuffer = false;
    const bool bUnloaded = UPackageTools::UnloadPackages(Params);
    if (!bUnloaded)
    {
        OutError = Params.OutErrorMessage.IsEmpty()
            ? TEXT("UE package unload did not unload the Graph fixture.")
            : Params.OutErrorMessage.ToString();
    }
    return bUnloaded;
}

class FRobustGraphFixture
{
public:
    explicit FRobustGraphFixture(const bool bPersistent = false)
        : bPersistentFixture(bPersistent)
    {
        const FString Token =
            FGuid::NewGuid().ToString(EGuidFormats::Digits);
        const FString AssetName = bPersistent
            ? TEXT("BP_GraphTopology")
            : FString::Printf(TEXT("BP_RobustGraph_%s"), *Token);
        PackageName = bPersistent
            ? FString::Printf(
                TEXT("/Game/LoomleTests/GraphPersistence/%s/%s"),
                *Token,
                *AssetName)
            : FString::Printf(
                TEXT("/Game/LoomleTests/RobustGraph_%s"),
                *Token);
        ObjectPath = PackageName + TEXT(".") + AssetName;
        if (bPersistent)
        {
            Filename = FPackageName::LongPackageNameToFilename(
                PackageName,
                FPackageName::GetAssetPackageExtension());
            IFileManager::Get().MakeDirectory(
                *FPaths::GetPath(Filename),
                true);
        }

        Package = CreatePackage(*PackageName);
        Blueprint = Package != nullptr
            ? FKismetEditorUtilities::CreateBlueprint(
                AActor::StaticClass(),
                Package,
                FName(*AssetName),
                BPTYPE_Normal,
                UBlueprint::StaticClass(),
                UBlueprintGeneratedClass::StaticClass(),
                NAME_None)
            : nullptr;
        if (Blueprint == nullptr)
        {
            return;
        }
        if (bPersistent)
        {
            FAssetRegistryModule::AssetCreated(Blueprint);
            bRegistered = true;
        }

        Graph = FBlueprintEditorUtils::FindEventGraph(Blueprint);
        Entry = RobustGraphAddEvent(
            Graph,
            TEXT("RobustEntry"),
            FIntPoint(0, 0));
        LooseEntry = RobustGraphAddEvent(
            Graph,
            TEXT("RobustLooseEntry"),
            FIntPoint(0, 500));
        BranchA = RobustGraphAddBranch(
            Graph,
            FIntPoint(350, 0));
        BranchB = RobustGraphAddBranch(
            Graph,
            FIntPoint(700, -150));
        BranchC = RobustGraphAddBranch(
            Graph,
            FIntPoint(700, 200));
        NotNode = RobustGraphAddNot(
            Graph,
            FIntPoint(50, -300));

        const UEdGraphSchema_K2* Schema =
            GetDefault<UEdGraphSchema_K2>();
        if (Schema != nullptr
            && Entry != nullptr
            && BranchA != nullptr
            && BranchB != nullptr
            && BranchC != nullptr
            && NotNode != nullptr)
        {
            EntryThen = Entry->GetThenPin();
            LooseThen = LooseEntry != nullptr
                ? LooseEntry->GetThenPin()
                : nullptr;
            BranchAExec = BranchA->GetExecPin();
            BranchAThen = BranchA->GetThenPin();
            BranchAElse = BranchA->GetElsePin();
            BranchACondition = BranchA->GetConditionPin();
            BranchBExec = BranchB->GetExecPin();
            BranchBCondition = BranchB->GetConditionPin();
            BranchCExec = BranchC->GetExecPin();
            NotOutput = RobustGraphFindPin(
                NotNode,
                UEdGraphSchema_K2::PN_ReturnValue,
                EGPD_Output);
            bTopologyCreated =
                Schema->TryCreateConnection(
                    EntryThen,
                    BranchAExec)
                && Schema->TryCreateConnection(
                    BranchAThen,
                    BranchBExec)
                && Schema->TryCreateConnection(
                    BranchAElse,
                    BranchCExec)
                && Schema->TryCreateConnection(
                    NotOutput,
                    BranchACondition)
                && Schema->TryCreateConnection(
                    NotOutput,
                    BranchBCondition);
        }
        FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(
            Blueprint);
        // A persistent fixture has authored unsaved topology that the terminal
        // save operation must actually write. Transient fixtures stay clean so
        // their ordinary lifecycle assertions are isolated from package state.
        Package->SetDirtyFlag(bPersistentFixture);
    }

    ~FRobustGraphFixture()
    {
        FString Ignored;
        Cleanup(Ignored);
    }

    FRobustGraphFixture(const FRobustGraphFixture&) = delete;
    FRobustGraphFixture& operator=(const FRobustGraphFixture&) = delete;

    bool IsValid() const
    {
        return Package != nullptr
            && Blueprint != nullptr
            && Graph != nullptr
            && Entry != nullptr
            && LooseEntry != nullptr
            && BranchA != nullptr
            && BranchB != nullptr
            && BranchC != nullptr
            && NotNode != nullptr
            && EntryThen != nullptr
            && LooseThen != nullptr
            && BranchAExec != nullptr
            && BranchAThen != nullptr
            && BranchAElse != nullptr
            && BranchACondition != nullptr
            && BranchBExec != nullptr
            && BranchBCondition != nullptr
            && BranchCExec != nullptr
            && NotOutput != nullptr
            && bTopologyCreated;
    }

    bool Unload(FString& OutError)
    {
        if (!bPersistentFixture)
        {
            OutError = TEXT("Only persistent Graph fixture can unload and reload.");
            return false;
        }
        UPackage* PackageToUnload = Package;
        ClearObjectPointers();
        if (!RobustGraphUnloadPackage(PackageToUnload, OutError))
        {
            return false;
        }
        if (FindPackage(nullptr, *PackageName) != nullptr
            || FindObject<UObject>(nullptr, *ObjectPath) != nullptr)
        {
            OutError = TEXT("Persistent Graph fixture remained loaded.");
            return false;
        }
        OutError.Reset();
        return true;
    }

    bool Reload(FString& OutError)
    {
        Blueprint = LoadObject<UBlueprint>(nullptr, *ObjectPath);
        Package =
            Blueprint != nullptr ? Blueprint->GetOutermost() : nullptr;
        if (Blueprint == nullptr || Package == nullptr)
        {
            OutError = TEXT("Persistent Graph fixture did not reload.");
            return false;
        }
        OutError.Reset();
        return true;
    }

    bool Cleanup(FString& OutError)
    {
        if (bCleaned)
        {
            return OutError.IsEmpty();
        }
        bCleaned = true;
        UObject* Loaded =
            !ObjectPath.IsEmpty()
                ? FindObject<UObject>(nullptr, *ObjectPath)
                : nullptr;
        if (Loaded != nullptr && bRegistered)
        {
            FAssetRegistryModule::AssetDeleted(Loaded);
            bRegistered = false;
        }
        UPackage* LoadedPackage =
            !PackageName.IsEmpty()
                ? FindPackage(nullptr, *PackageName)
                : nullptr;
        ClearObjectPointers();
        FString UnloadError;
        if (!RobustGraphUnloadPackage(LoadedPackage, UnloadError))
        {
            OutError = UnloadError;
        }
        if (!PackageName.IsEmpty()
            && FindPackage(nullptr, *PackageName) != nullptr)
        {
            if (!OutError.IsEmpty())
            {
                OutError += TEXT(" ");
            }
            OutError +=
                TEXT("Robust Graph fixture package remained loaded during cleanup.");
        }
        if (!Filename.IsEmpty()
            && IFileManager::Get().FileExists(*Filename)
            && !IFileManager::Get().Delete(
                *Filename,
                false,
                true,
                true))
        {
            if (!OutError.IsEmpty())
            {
                OutError += TEXT(" ");
            }
            OutError +=
                TEXT("Persistent Graph fixture file could not be deleted.");
        }
        if (!Filename.IsEmpty())
        {
            IFileManager::Get().DeleteDirectory(
                *FPaths::GetPath(Filename),
                false,
                true);
        }
        return OutError.IsEmpty();
    }

    UEdGraph* FindGraphByGuid(const FGuid& Id) const
    {
        if (Blueprint == nullptr)
        {
            return nullptr;
        }
        TArray<UEdGraph*> Graphs;
        Blueprint->GetAllGraphs(Graphs);
        for (UEdGraph* Candidate : Graphs)
        {
            if (Candidate != nullptr
                && Candidate->GraphGuid == Id)
            {
                return Candidate;
            }
        }
        return nullptr;
    }

    static UEdGraphNode* FindNodeByGuid(
        UEdGraph* InGraph,
        const FGuid& Id)
    {
        if (InGraph == nullptr)
        {
            return nullptr;
        }
        for (UEdGraphNode* Candidate : InGraph->Nodes)
        {
            if (Candidate != nullptr
                && Candidate->NodeGuid == Id)
            {
                return Candidate;
            }
        }
        return nullptr;
    }

    static UEdGraphPin* FindPinByGuid(
        UEdGraph* InGraph,
        const FGuid& Id)
    {
        if (InGraph == nullptr)
        {
            return nullptr;
        }
        for (UEdGraphNode* Node : InGraph->Nodes)
        {
            if (Node == nullptr)
            {
                continue;
            }
            for (UEdGraphPin* Pin : Node->Pins)
            {
                if (Pin != nullptr && Pin->PinId == Id)
                {
                    return Pin;
                }
            }
        }
        return nullptr;
    }

    UPackage* Package = nullptr;
    UBlueprint* Blueprint = nullptr;
    UEdGraph* Graph = nullptr;
    UK2Node_CustomEvent* Entry = nullptr;
    UK2Node_CustomEvent* LooseEntry = nullptr;
    UK2Node_IfThenElse* BranchA = nullptr;
    UK2Node_IfThenElse* BranchB = nullptr;
    UK2Node_IfThenElse* BranchC = nullptr;
    UK2Node_CallFunction* NotNode = nullptr;
    UEdGraphPin* EntryThen = nullptr;
    UEdGraphPin* LooseThen = nullptr;
    UEdGraphPin* BranchAExec = nullptr;
    UEdGraphPin* BranchAThen = nullptr;
    UEdGraphPin* BranchAElse = nullptr;
    UEdGraphPin* BranchACondition = nullptr;
    UEdGraphPin* BranchBExec = nullptr;
    UEdGraphPin* BranchBCondition = nullptr;
    UEdGraphPin* BranchCExec = nullptr;
    UEdGraphPin* NotOutput = nullptr;
    FString PackageName;
    FString ObjectPath;
    FString Filename;

private:
    void ClearObjectPointers()
    {
        EntryThen = nullptr;
        LooseThen = nullptr;
        BranchAExec = nullptr;
        BranchAThen = nullptr;
        BranchAElse = nullptr;
        BranchACondition = nullptr;
        BranchBExec = nullptr;
        BranchBCondition = nullptr;
        BranchCExec = nullptr;
        NotOutput = nullptr;
        Entry = nullptr;
        LooseEntry = nullptr;
        BranchA = nullptr;
        BranchB = nullptr;
        BranchC = nullptr;
        NotNode = nullptr;
        Graph = nullptr;
        Blueprint = nullptr;
        Package = nullptr;
    }

    bool bTopologyCreated = false;
    bool bPersistentFixture = false;
    bool bRegistered = false;
    bool bCleaned = false;
};

class FVariablePaletteFixture
{
public:
    explicit FVariablePaletteFixture(const bool bAnimBlueprint)
        : bAnim(bAnimBlueprint)
    {
        const FString Token =
            FGuid::NewGuid().ToString(EGuidFormats::Digits);
        const FString AssetName = FString::Printf(
            TEXT("%s_%s"),
            bAnim ? TEXT("ABP_VariablePalette") : TEXT("BP_VariablePalette"),
            *Token);
        PackageName = FString::Printf(
            TEXT("/Game/LoomleTests/%s"),
            *AssetName);
        Package = CreatePackage(*PackageName);
        Blueprint = Package != nullptr
            ? FKismetEditorUtilities::CreateBlueprint(
                bAnim
                    ? UAnimInstance::StaticClass()
                    : AActor::StaticClass(),
                Package,
                FName(*AssetName),
                BPTYPE_Normal,
                bAnim
                    ? UAnimBlueprint::StaticClass()
                    : UBlueprint::StaticClass(),
                bAnim
                    ? UAnimBlueprintGeneratedClass::StaticClass()
                    : UBlueprintGeneratedClass::StaticClass(),
                NAME_None)
            : nullptr;
        if (Blueprint == nullptr)
        {
            return;
        }
        if (UAnimBlueprint* AnimBlueprint =
                Cast<UAnimBlueprint>(Blueprint))
        {
            AnimBlueprint->TargetSkeleton =
                NewObject<USkeleton>(
                    Package,
                    TEXT("LoomlePaletteSkeleton"),
                    RF_Transactional);
        }

        Graph = FBlueprintEditorUtils::FindEventGraph(Blueprint);
        FEdGraphPinType BoolType;
        BoolType.PinCategory = UEdGraphSchema_K2::PC_Boolean;
        bAlphaAdded = FBlueprintEditorUtils::AddMemberVariable(
            Blueprint,
            AlphaName,
            BoolType);
        bBetaAdded = FBlueprintEditorUtils::AddMemberVariable(
            Blueprint,
            BetaName,
            BoolType);
        FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(
            Blueprint);
        FBlueprintActionDatabase::Get().RefreshAssetActions(Blueprint);
        Package->SetDirtyFlag(false);
    }

    ~FVariablePaletteFixture()
    {
        FString Ignored;
        Cleanup(Ignored);
    }

    FVariablePaletteFixture(const FVariablePaletteFixture&) = delete;
    FVariablePaletteFixture& operator=(
        const FVariablePaletteFixture&) = delete;

    bool IsValid() const
    {
        return Package != nullptr
            && Blueprint != nullptr
            && Graph != nullptr
            && bAlphaAdded
            && bBetaAdded
            && VariableGuid(AlphaName).IsValid()
            && VariableGuid(BetaName).IsValid();
    }

    FGuid VariableGuid(const FName Name) const
    {
        if (Blueprint == nullptr)
        {
            return FGuid();
        }
        const int32 Index =
            FBlueprintEditorUtils::FindNewVariableIndex(
                Blueprint,
                Name);
        return Blueprint->NewVariables.IsValidIndex(Index)
            ? Blueprint->NewVariables[Index].VarGuid
            : FGuid();
    }

    bool Cleanup(FString& OutError)
    {
        if (bCleaned)
        {
            OutError.Reset();
            return true;
        }
        bCleaned = true;
        if (Blueprint != nullptr)
        {
            FBlueprintActionDatabase::Get().ClearAssetActions(
                Blueprint);
        }
        UPackage* PackageToUnload = Package;
        Blueprint = nullptr;
        Graph = nullptr;
        Package = nullptr;
        return RobustGraphUnloadPackage(
            PackageToUnload,
            OutError);
    }

    UPackage* Package = nullptr;
    UBlueprint* Blueprint = nullptr;
    UEdGraph* Graph = nullptr;
    const FName AlphaName = TEXT("LoomlePaletteAlpha");
    const FName BetaName = TEXT("LoomlePaletteBeta");

private:
    FString PackageName;
    bool bAnim = false;
    bool bAlphaAdded = false;
    bool bBetaAdded = false;
    bool bCleaned = false;
};

class FComponentBoundPaletteFixture
{
public:
    FComponentBoundPaletteFixture()
    {
        const FString Token =
            FGuid::NewGuid().ToString(EGuidFormats::Digits);
        const FString AssetName = FString::Printf(
            TEXT("BP_ComponentBoundPalette_%s"),
            *Token);
        Package = CreatePackage(*FString::Printf(
            TEXT("/Game/LoomleTests/%s"),
            *AssetName));
        Blueprint = Package != nullptr
            ? FKismetEditorUtilities::CreateBlueprint(
                AActor::StaticClass(),
                Package,
                FName(*AssetName),
                BPTYPE_Normal,
                UBlueprint::StaticClass(),
                UBlueprintGeneratedClass::StaticClass(),
                NAME_None)
            : nullptr;
        if (Blueprint == nullptr)
        {
            return;
        }

        FunctionGraph = FBlueprintEditorUtils::CreateNewGraph(
            Blueprint,
            FunctionName,
            UEdGraph::StaticClass(),
            UEdGraphSchema_K2::StaticClass());
        if (FunctionGraph != nullptr)
        {
            FBlueprintEditorUtils::AddFunctionGraph(
                Blueprint,
                FunctionGraph,
                true,
                static_cast<UClass*>(nullptr));
        }

        USimpleConstructionScript* SCS =
            Blueprint->SimpleConstructionScript;
        if (SCS != nullptr)
        {
            RootComponent = SCS->CreateNode(
                USceneComponent::StaticClass(),
                RootName);
            PreviewComponent = SCS->CreateNode(
                UStaticMeshComponent::StaticClass(),
                PreviewName);
            OtherComponent = SCS->CreateNode(
                UStaticMeshComponent::StaticClass(),
                OtherName);
            if (RootComponent != nullptr)
            {
                SCS->AddNode(RootComponent);
                if (PreviewComponent != nullptr)
                {
                    RootComponent->AddChildNode(
                        PreviewComponent);
                }
                if (OtherComponent != nullptr)
                {
                    RootComponent->AddChildNode(
                        OtherComponent);
                }
            }
        }

        FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(
            Blueprint);
        FKismetEditorUtilities::CompileBlueprint(Blueprint);
        UClass* Skeleton =
            Blueprint->SkeletonGeneratedClass.Get();
        PreviewProperty =
            Skeleton != nullptr
                ? FindFProperty<FObjectProperty>(
                    Skeleton,
                    PreviewName)
                : nullptr;
        if (FunctionGraph != nullptr
            && PreviewProperty != nullptr)
        {
            PreviewGetter = NewObject<UK2Node_VariableGet>(
                FunctionGraph,
                NAME_None,
                RF_Transactional);
            PreviewGetter->CreateNewGuid();
            PreviewGetter->SetFromProperty(
                PreviewProperty,
                true,
                Skeleton);
            FunctionGraph->AddNode(
                PreviewGetter,
                false,
                false);
            PreviewGetter->AllocateDefaultPins();
            PreviewPin = PreviewGetter->GetValuePin();

            UFunction* MakeRotatorFunction =
                UKismetMathLibrary::StaticClass()
                    ->FindFunctionByName(
                        GET_FUNCTION_NAME_CHECKED(
                            UKismetMathLibrary,
                            MakeRotator));
            if (MakeRotatorFunction != nullptr)
            {
                MakeRotator = NewObject<UK2Node_CallFunction>(
                    FunctionGraph,
                    NAME_None,
                    RF_Transactional);
                MakeRotator->CreateNewGuid();
                MakeRotator->SetFromFunction(
                    MakeRotatorFunction);
                FunctionGraph->AddNode(
                    MakeRotator,
                    false,
                    false);
                MakeRotator->AllocateDefaultPins();
                RotatorPin = MakeRotator->GetReturnValuePin();
            }
        }
        FBlueprintActionDatabase::Get().RefreshAssetActions(
            Blueprint);
        if (Package != nullptr)
        {
            Package->SetDirtyFlag(false);
        }
    }

    ~FComponentBoundPaletteFixture()
    {
        FString Ignored;
        Cleanup(Ignored);
    }

    FComponentBoundPaletteFixture(
        const FComponentBoundPaletteFixture&) = delete;
    FComponentBoundPaletteFixture& operator=(
        const FComponentBoundPaletteFixture&) = delete;

    bool IsValid() const
    {
        return Package != nullptr
            && Blueprint != nullptr
            && FunctionGraph != nullptr
            && FunctionGraph->GraphGuid.IsValid()
            && RootComponent != nullptr
            && RootComponent->VariableGuid.IsValid()
            && PreviewComponent != nullptr
            && PreviewComponent->VariableGuid.IsValid()
            && OtherComponent != nullptr
            && OtherComponent->VariableGuid.IsValid()
            && PreviewProperty != nullptr
            && PreviewGetter != nullptr
            && PreviewPin != nullptr
            && PreviewPin->Direction == EGPD_Output
            && MakeRotator != nullptr
            && RotatorPin != nullptr
            && RotatorPin->Direction == EGPD_Output;
    }

    bool Cleanup(FString& OutError)
    {
        if (bCleaned)
        {
            OutError.Reset();
            return true;
        }
        bCleaned = true;
        if (Blueprint != nullptr)
        {
            FBlueprintActionDatabase::Get().ClearAssetActions(
                Blueprint);
            Blueprint->ClearFlags(
                RF_Public | RF_Standalone);
        }
        UPackage* PackageToUnload = Package;
        RotatorPin = nullptr;
        PreviewPin = nullptr;
        MakeRotator = nullptr;
        PreviewGetter = nullptr;
        PreviewProperty = nullptr;
        OtherComponent = nullptr;
        PreviewComponent = nullptr;
        RootComponent = nullptr;
        FunctionGraph = nullptr;
        Blueprint = nullptr;
        Package = nullptr;
        return RobustGraphUnloadPackage(
            PackageToUnload,
            OutError);
    }

    UPackage* Package = nullptr;
    UBlueprint* Blueprint = nullptr;
    UEdGraph* FunctionGraph = nullptr;
    USCS_Node* RootComponent = nullptr;
    USCS_Node* PreviewComponent = nullptr;
    USCS_Node* OtherComponent = nullptr;
    FObjectProperty* PreviewProperty = nullptr;
    UK2Node_VariableGet* PreviewGetter = nullptr;
    UK2Node_CallFunction* MakeRotator = nullptr;
    UEdGraphPin* PreviewPin = nullptr;
    UEdGraphPin* RotatorPin = nullptr;
    const FName FunctionName = TEXT("UpdatePreviewRotation");
    const FName RootName = TEXT("DefaultSceneRoot");
    const FName PreviewName = TEXT("PreviewMesh");
    const FName OtherName = TEXT("OtherMesh");

private:
    bool bCleaned = false;
};

class FWidgetMemberPaletteFixture
{
public:
    FWidgetMemberPaletteFixture()
    {
        const FString Token =
            FGuid::NewGuid().ToString(EGuidFormats::Digits);
        const FString AssetName =
            TEXT("WBP_MemberPalette_") + Token;
        Package = CreatePackage(*FString::Printf(
            TEXT("/Game/LoomleTests/%s"),
            *AssetName));
        Blueprint = Package != nullptr
            ? Cast<UWidgetBlueprint>(
                FKismetEditorUtilities::CreateBlueprint(
                    UUserWidget::StaticClass(),
                    Package,
                    FName(*AssetName),
                    BPTYPE_Normal,
                    UWidgetBlueprint::StaticClass(),
                    UWidgetBlueprintGeneratedClass::StaticClass(),
                    NAME_None))
            : nullptr;
        if (Blueprint == nullptr)
        {
            return;
        }

        TargetGraph = AddFunctionGraph(TargetFunctionName);
        LoadPageGraph = AddFunctionGraph(LoadPageFunctionName);
        SetActiveGraph = AddFunctionGraph(
            SetActiveFunctionName);

        UCanvasPanel* Root =
            Blueprint->WidgetTree->ConstructWidget<UCanvasPanel>(
                UCanvasPanel::StaticClass(),
                TEXT("RootCanvas"));
        MemberWidget =
            Blueprint->WidgetTree->ConstructWidget<UButton>(
                UButton::StaticClass(),
                MemberWidgetName);
        if (Root != nullptr && MemberWidget != nullptr)
        {
            Blueprint->WidgetTree->RootWidget = Root;
            Root->AddChild(MemberWidget);
            MemberWidget->bIsVariable = true;
            Blueprint->OnVariableAdded(Root->GetFName());
            Blueprint->OnVariableAdded(MemberWidgetName);
        }

        FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(
            Blueprint);
        FKismetEditorUtilities::CompileBlueprint(Blueprint);
        FBlueprintActionDatabase::Get().RefreshAssetActions(
            Blueprint);
        Package->SetDirtyFlag(false);
    }

    ~FWidgetMemberPaletteFixture()
    {
        FString Ignored;
        Cleanup(Ignored);
    }

    FWidgetMemberPaletteFixture(
        const FWidgetMemberPaletteFixture&) = delete;
    FWidgetMemberPaletteFixture& operator=(
        const FWidgetMemberPaletteFixture&) = delete;

    bool IsValid() const
    {
        const UClass* Skeleton =
            Blueprint != nullptr
                ? Blueprint->SkeletonGeneratedClass.Get()
                : nullptr;
        const UClass* Generated =
            Blueprint != nullptr
                ? Blueprint->GeneratedClass.Get()
                : nullptr;
        return Package != nullptr
            && Blueprint != nullptr
            && TargetGraph != nullptr
            && LoadPageGraph != nullptr
            && SetActiveGraph != nullptr
            && MemberWidget != nullptr
            && Skeleton != nullptr
            && Skeleton->FindFunctionByName(
                LoadPageFunctionName) != nullptr
            && Skeleton->FindFunctionByName(
                SetActiveFunctionName) != nullptr
            && ((Generated != nullptr
                    && FindFProperty<FProperty>(
                        Generated,
                        MemberWidgetName) != nullptr)
                || FindFProperty<FProperty>(
                    Skeleton,
                    MemberWidgetName) != nullptr);
    }

    bool Cleanup(FString& OutError)
    {
        if (bCleaned)
        {
            OutError.Reset();
            return true;
        }
        bCleaned = true;
        if (Blueprint != nullptr)
        {
            FBlueprintActionDatabase::Get().ClearAssetActions(
                Blueprint);
        }
        UPackage* PackageToUnload = Package;
        MemberWidget = nullptr;
        SetActiveGraph = nullptr;
        LoadPageGraph = nullptr;
        TargetGraph = nullptr;
        Blueprint = nullptr;
        Package = nullptr;
        return RobustGraphUnloadPackage(
            PackageToUnload,
            OutError);
    }

    UPackage* Package = nullptr;
    UWidgetBlueprint* Blueprint = nullptr;
    UEdGraph* TargetGraph = nullptr;
    UEdGraph* LoadPageGraph = nullptr;
    UEdGraph* SetActiveGraph = nullptr;
    UButton* MemberWidget = nullptr;
    const FName TargetFunctionName = TEXT("ShowBottomBackpack");
    const FName LoadPageFunctionName = TEXT("LoadPage");
    const FName SetActiveFunctionName = TEXT("SetActiveNavItem");
    const FName MemberWidgetName = TEXT("BottomBackpackNavItem");

private:
    UEdGraph* AddFunctionGraph(const FName Name)
    {
        UEdGraph* Graph =
            FBlueprintEditorUtils::CreateNewGraph(
                Blueprint,
                Name,
                UEdGraph::StaticClass(),
                UEdGraphSchema_K2::StaticClass());
        if (Graph != nullptr)
        {
            FBlueprintEditorUtils::AddFunctionGraph(
                Blueprint,
                Graph,
                true,
                static_cast<UClass*>(nullptr));
        }
        return Graph;
    }

    bool bCleaned = false;
};

FString RobustGraphFindPaletteId(
    const TSharedPtr<FJsonObject>& Result,
    const FString& ExpectedType)
{
    for (const TSharedPtr<FJsonObject>& Args :
         RobustGraphCallArgs(Result, TEXT("node")))
    {
        FString Type;
        FString Palette;
        if (Args.IsValid()
            && Args->TryGetStringField(TEXT("type"), Type)
            && Type == ExpectedType
            && Args->TryGetStringField(TEXT("palette"), Palette))
        {
            return Palette;
        }
    }
    return FString();
}

TArray<FString> RobustGraphPaletteIds(
    const TSharedPtr<FJsonObject>& Result)
{
    TArray<FString> Ids;
    for (const TSharedPtr<FJsonObject>& Args :
         RobustGraphCallArgs(Result, TEXT("node")))
    {
        FString Palette;
        if (Args.IsValid()
            && Args->TryGetStringField(
                TEXT("palette"),
                Palette)
            && !Palette.IsEmpty())
        {
            Ids.Add(Palette);
        }
    }
    return Ids;
}

FString RobustGraphFindVariablePaletteId(
    const FSalResolvedTarget& Target,
    const FString& Verb,
    const FName VariableName,
    const FString& ExpectedType)
{
    FSalQuery Palette =
        RobustGraphQuery(TEXT("palette_entries"));
    Palette.Operation->SetStringField(
        TEXT("text"),
        Verb + TEXT(" ") + VariableName.ToString());
    Palette.PageLimit = 10;
    return RobustGraphFindPaletteId(
        FSalGraphInterface::Query(Palette, Target),
        ExpectedType);
}

FString RobustGraphResolvedRef(
    const TSharedPtr<FJsonObject>& Result,
    const FString& Alias)
{
    const TSharedPtr<FJsonObject>* Resolved = nullptr;
    const TSharedPtr<FJsonObject>* Ref = nullptr;
    const TArray<TSharedPtr<FJsonValue>>* IdentityPath = nullptr;
    FString Kind;
    if (!Result.IsValid()
        || !Result->TryGetObjectField(TEXT("resolvedRefs"), Resolved)
        || Resolved == nullptr
        || !(*Resolved)->TryGetObjectField(Alias, Ref)
        || Ref == nullptr
        || !(*Ref)->TryGetStringField(TEXT("kind"), Kind)
        || Kind != TEXT("stable_ref")
        || !(*Ref)->TryGetArrayField(
            TEXT("identityPath"),
            IdentityPath)
        || IdentityPath == nullptr
        || IdentityPath->IsEmpty())
    {
        return FString();
    }
    TArray<FString> Segments;
    Segments.Reserve(IdentityPath->Num());
    for (const TSharedPtr<FJsonValue>& SegmentValue :
         *IdentityPath)
    {
        FString Segment;
        if (!SegmentValue.IsValid()
            || !SegmentValue->TryGetString(Segment)
            || Segment.IsEmpty())
        {
            return FString();
        }
        Segments.Add(MoveTemp(Segment));
    }
    return FString::Join(Segments, TEXT("/"));
}

FSalPatch RobustGraphTerminalPatch()
{
    TSharedRef<FJsonObject> Compile = MakeShared<FJsonObject>();
    Compile->SetStringField(TEXT("kind"), TEXT("compile"));
    TSharedRef<FJsonObject> Save = MakeShared<FJsonObject>();
    Save->SetStringField(TEXT("kind"), TEXT("save"));
    FSalPatch Patch;
    Patch.Alias = TEXT("blueprint");
    Patch.Statements = {
        MakeShared<FJsonValueObject>(Compile),
        MakeShared<FJsonValueObject>(Save)};
    return Patch;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FSalRobustGraphPinIdentityScopeTest,
    "Loomle.Sal.Robustness.Graph.PinIdentityScope",
    EAutomationTestFlags::EditorContext
        | EAutomationTestFlags::EngineFilter)

bool FSalRobustGraphPinIdentityScopeTest::RunTest(
    const FString& Parameters)
{
    if (!RobustGraphRequireIdleEditor(
            *this,
            TEXT("Graph Pin identity scope coverage")))
    {
        return false;
    }
    Loomle::Tests::FScopedIsolatedTransactor Transactions;
    if (!TestTrue(
            TEXT("Pin identity scope isolates transaction history"),
            Transactions.Initialize()))
    {
        return false;
    }
    FRobustGraphFixture Fixture;
    if (!TestTrue(TEXT("Pin identity scope fixture is valid"), Fixture.IsValid()))
    {
        Transactions.Restore();
        return false;
    }

    UEdGraph* OtherGraph = FBlueprintEditorUtils::CreateNewGraph(
        Fixture.Blueprint,
        TEXT("PinIdentityOtherGraph"),
        UEdGraph::StaticClass(),
        UEdGraphSchema_K2::StaticClass());
    if (OtherGraph != nullptr)
    {
        FBlueprintEditorUtils::AddFunctionGraph(
            Fixture.Blueprint,
            OtherGraph,
            true,
            static_cast<UClass*>(nullptr));
    }
    UK2Node_IfThenElse* OtherBranch =
        RobustGraphAddBranch(OtherGraph, FIntPoint(0, 0));
    UEdGraphPin* OtherExec =
        OtherBranch != nullptr ? OtherBranch->GetExecPin() : nullptr;
    if (!TestNotNull(TEXT("Other Graph duplicate Pin fixture exists"), OtherExec))
    {
        Transactions.Restore();
        return false;
    }
    OtherExec->PinId = Fixture.BranchAExec->PinId;

    FSalQuery ExactPin = RobustGraphQuery(TEXT("pin"));
    ExactPin.Operation->SetStringField(
        TEXT("id"),
        RobustGraphGuidText(Fixture.BranchAExec->PinId));
    const FSalResolvedTarget Target =
        RobustGraphTarget(Fixture.Blueprint, Fixture.Graph);
    const TSharedPtr<FJsonObject> CrossGraphRead =
        FSalGraphInterface::Query(ExactPin, Target);
    TestFalse(
        TEXT("Same PinId in another Graph does not affect exact read"),
        RobustGraphHasError(CrossGraphRead));

    TStrongObjectPtr<UBlueprint> SandboxOwner;
    FSalResolvedTarget SandboxTarget;
    FString SandboxError;
    TestTrue(
        *FString::Printf(
            TEXT("Sandbox accepts PinId reuse across Graphs [%s]"),
            *SandboxError),
        FSalGraphInterface::BuildSandboxTargetForTesting(
            Target,
            SandboxOwner,
            SandboxTarget,
            SandboxError));

    Fixture.BranchBExec->PinId = Fixture.BranchAExec->PinId;
    const TSharedPtr<FJsonObject> SameGraphRead =
        FSalGraphInterface::Query(ExactPin, Target);
    TestTrue(
        TEXT("Same PinId on another Node in the bound Graph is ambiguous"),
        RobustGraphHasDiagnosticCode(
            SameGraphRead,
            TEXT("resolution.pin_ambiguous")));

    FSalQuery Context = RobustGraphTraversal(
        TEXT("context"),
        FString(),
        RobustGraphTyped(
            TEXT("pin"),
            Fixture.BranchAExec->PinId),
        1);
    const TSharedPtr<FJsonObject> AmbiguousTraversal =
        FSalGraphInterface::Query(Context, Target);
    TestTrue(
        TEXT("Traversal reports ambiguous PinId"),
        RobustGraphHasDiagnosticCode(
            AmbiguousTraversal,
            TEXT("resolution.pin_ambiguous")));

    FSalPatch AmbiguousPinPatch;
    AmbiguousPinPatch.Alias = TEXT("graph");
    AmbiguousPinPatch.bDryRun = true;
    AmbiguousPinPatch.Statements = {
        RobustGraphUnary(
            TEXT("break"),
            RobustGraphTyped(
                TEXT("pin"),
                Fixture.BranchAExec->PinId))};
    const TSharedPtr<FJsonObject> AmbiguousMutation =
        FSalGraphInterface::Patch(AmbiguousPinPatch, Target);
    TestTrue(
        TEXT("Pin mutation reports ambiguous PinId"),
        RobustGraphHasDiagnosticCode(
            AmbiguousMutation,
            TEXT("resolution.pin_ambiguous")));

    FSalPatch AmbiguousPinFieldPatch;
    AmbiguousPinFieldPatch.Alias = TEXT("graph");
    AmbiguousPinFieldPatch.bDryRun = true;
    AmbiguousPinFieldPatch.Statements = {
        RobustGraphSet(
            RobustGraphMember(
                RobustGraphTyped(
                    TEXT("pin"),
                    Fixture.BranchAExec->PinId),
                TEXT("DefaultValue")),
            MakeShared<FJsonValueString>(TEXT("unused")))};
    const TSharedPtr<FJsonObject> AmbiguousFieldMutation =
        FSalGraphInterface::Patch(AmbiguousPinFieldPatch, Target);
    TestTrue(
        TEXT("Pin field mutation reports ambiguous PinId"),
        RobustGraphHasDiagnosticCode(
            AmbiguousFieldMutation,
            TEXT("resolution.pin_ambiguous")));

    FSalPatch UnrelatedNodePatch;
    UnrelatedNodePatch.Alias = TEXT("graph");
    UnrelatedNodePatch.bDryRun = true;
    UnrelatedNodePatch.Statements = {
        RobustGraphMoveTo(
            RobustGraphTyped(
                TEXT("node"),
                Fixture.BranchC->NodeGuid),
            FIntPoint(
                Fixture.BranchC->NodePosX + 16,
                Fixture.BranchC->NodePosY))};
    const TSharedPtr<FJsonObject> UnrelatedMutation =
        FSalGraphInterface::Patch(UnrelatedNodePatch, Target);
    TestTrue(
        *FString::Printf(
            TEXT("Unrelated Node mutation survives duplicate PinIds [%s]"),
            *RobustGraphDiagnosticsText(UnrelatedMutation)),
        RobustGraphResultBool(UnrelatedMutation, TEXT("valid")));

    Transactions.Restore();
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FSalRobustGraphTraversalPaletteTest,
    "Loomle.Sal.Robustness.Graph.TraversalPalette",
    EAutomationTestFlags::EditorContext
        | EAutomationTestFlags::EngineFilter)

bool FSalRobustGraphTraversalPaletteTest::RunTest(
    const FString& Parameters)
{
    if (!RobustGraphRequireIdleEditor(
            *this,
            TEXT("Graph traversal and Palette coverage")))
    {
        return false;
    }
    FRobustGraphFixture Fixture;
    if (!TestTrue(TEXT("Graph traversal fixture is valid"), Fixture.IsValid()))
    {
        return false;
    }
    const FSalResolvedTarget Target =
        RobustGraphTarget(Fixture.Blueprint, Fixture.Graph);

    const TSharedPtr<FJsonObject> ExecDepthOne =
        FSalGraphInterface::Query(
            RobustGraphTraversal(
                TEXT("exec_flow"),
                TEXT("from"),
                RobustGraphTyped(
                    TEXT("node"),
                    Fixture.Entry->NodeGuid),
                1),
            Target);
    TestFalse(
        TEXT("Exec flow depth 1 succeeds"),
        RobustGraphHasError(ExecDepthOne));
    TestTrue(
        TEXT("Exec flow depth 1 contains entry"),
        RobustGraphContainsCallId(
            ExecDepthOne,
            TEXT("node"),
            Fixture.Entry->NodeGuid));
    TestTrue(
        TEXT("Exec flow depth 1 contains immediate Branch"),
        RobustGraphContainsCallId(
            ExecDepthOne,
            TEXT("node"),
            Fixture.BranchA->NodeGuid));
    TestFalse(
        TEXT("Exec flow depth 1 does not over-expand"),
        RobustGraphContainsCallId(
            ExecDepthOne,
            TEXT("node"),
            Fixture.BranchB->NodeGuid));
    TestEqual(
        TEXT("Exec flow depth 1 emits one complete Edge"),
        RobustGraphEdgeCount(ExecDepthOne),
        1);

    const TSharedPtr<FJsonObject> ExecDepthTwo =
        FSalGraphInterface::Query(
            RobustGraphTraversal(
                TEXT("exec_flow"),
                TEXT("from"),
                RobustGraphTyped(
                    TEXT("node"),
                    Fixture.Entry->NodeGuid),
                2),
            Target);
    TestFalse(
        TEXT("Exec flow depth 2 succeeds"),
        RobustGraphHasError(ExecDepthTwo));
    TestTrue(
        TEXT("Exec depth 2 follows Then branch"),
        RobustGraphContainsCallId(
            ExecDepthTwo,
            TEXT("node"),
            Fixture.BranchB->NodeGuid));
    TestTrue(
        TEXT("Exec depth 2 follows Else branch"),
        RobustGraphContainsCallId(
            ExecDepthTwo,
            TEXT("node"),
            Fixture.BranchC->NodeGuid));
    TestEqual(
        TEXT("Exec depth 2 emits three execution Edges"),
        RobustGraphEdgeCount(ExecDepthTwo),
        3);

    const TSharedPtr<FJsonObject> DataFlow =
        FSalGraphInterface::Query(
            RobustGraphTraversal(
                TEXT("data_flow"),
                TEXT("from"),
                RobustGraphTyped(
                    TEXT("node"),
                    Fixture.NotNode->NodeGuid),
                1),
            Target);
    TestFalse(
        TEXT("Data flow succeeds"),
        RobustGraphHasError(DataFlow));
    TestTrue(
        TEXT("Data flow reaches first Boolean consumer"),
        RobustGraphContainsCallId(
            DataFlow,
            TEXT("node"),
            Fixture.BranchA->NodeGuid));
    TestTrue(
        TEXT("Data flow reaches second Boolean consumer"),
        RobustGraphContainsCallId(
            DataFlow,
            TEXT("node"),
            Fixture.BranchB->NodeGuid));
    TestEqual(
        TEXT("Data flow emits both data Edges"),
        RobustGraphEdgeCount(DataFlow),
        2);

    const TSharedPtr<FJsonObject> Context =
        FSalGraphInterface::Query(
            RobustGraphTraversal(
                TEXT("context"),
                FString(),
                RobustGraphTyped(
                    TEXT("node"),
                    Fixture.BranchA->NodeGuid),
                1),
            Target);
    TestFalse(
        TEXT("Context traversal succeeds"),
        RobustGraphHasError(Context));
    TestTrue(
        TEXT("Context includes upstream execution owner"),
        RobustGraphContainsCallId(
            Context,
            TEXT("node"),
            Fixture.Entry->NodeGuid));
    TestTrue(
        TEXT("Context includes upstream data owner"),
        RobustGraphContainsCallId(
            Context,
            TEXT("node"),
            Fixture.NotNode->NodeGuid));

    FSalQuery Palette = RobustGraphQuery(TEXT("palette_entries"));
    Palette.Operation->SetStringField(TEXT("text"), TEXT("Branch"));
    Palette.PageLimit = 1;
    const TSharedPtr<FJsonObject> PaletteResult =
        FSalGraphInterface::Query(Palette, Target);
    TestFalse(
        TEXT("Graph Palette search succeeds"),
        RobustGraphHasError(PaletteResult));
    const FString BranchPalette = RobustGraphFindPaletteId(
        PaletteResult,
        TEXT("/Script/BlueprintGraph.K2Node_IfThenElse"));
    TestFalse(
        TEXT("Graph Palette returns exact Branch constructor"),
        BranchPalette.IsEmpty());

    FSalQuery ExactPalette = RobustGraphQuery(TEXT("palette"));
    ExactPalette.Operation->SetStringField(TEXT("id"), BranchPalette);
    ExactPalette.With.Add(TEXT("schema"));
    const TSharedPtr<FJsonObject> ExactPaletteResult =
        FSalGraphInterface::Query(ExactPalette, Target);
    TestFalse(
        TEXT("Exact Graph Palette entry with schema succeeds"),
        RobustGraphHasError(ExactPaletteResult));
    TestTrue(
        TEXT("Exact Branch Palette exposes its future Pins"),
        RobustGraphCallArgs(
            ExactPaletteResult,
            TEXT("pin")).Num() >= 4);
    TestTrue(
        TEXT("Exact Branch Palette exposes constructor schema"),
        RobustGraphHasComment(
            ExactPaletteResult,
            TEXT("schema:")));

    Palette.PageAfter = TEXT("graph1:not-this-query:1");
    TestTrue(
        TEXT("Invalid Graph Palette cursor fails closed"),
        RobustGraphHasDiagnosticCode(
            FSalGraphInterface::Query(Palette, Target),
            TEXT("validation.invalid_cursor")));
    return true;
}

bool RobustGraphRunVariablePaletteIdentityCase(
    FAutomationTestBase& Test,
    const bool bAnimBlueprint)
{
    const FString Surface =
        bAnimBlueprint ? TEXT("AnimBP") : TEXT("Blueprint");
    FVariablePaletteFixture Fixture(bAnimBlueprint);
    if (!Test.TestTrue(
            *FString::Printf(
                TEXT("%s variable Palette fixture is valid"),
                *Surface),
            Fixture.IsValid()))
    {
        return false;
    }

    const FSalResolvedTarget Target =
        RobustGraphTarget(Fixture.Blueprint, Fixture.Graph);
    const FString GetType =
        TEXT("/Script/BlueprintGraph.K2Node_VariableGet");
    const FString SetType =
        TEXT("/Script/BlueprintGraph.K2Node_VariableSet");
    const FString AlphaGet =
        RobustGraphFindVariablePaletteId(
            Target,
            TEXT("Get"),
            Fixture.AlphaName,
            GetType);
    const FString BetaGet =
        RobustGraphFindVariablePaletteId(
            Target,
            TEXT("Get"),
            Fixture.BetaName,
            GetType);
    const FString AlphaSet =
        RobustGraphFindVariablePaletteId(
            Target,
            TEXT("Set"),
            Fixture.AlphaName,
            SetType);
    const FString BetaSet =
        RobustGraphFindVariablePaletteId(
            Target,
            TEXT("Set"),
            Fixture.BetaName,
            SetType);

    const bool bDiscovered =
        !AlphaGet.IsEmpty()
        && !BetaGet.IsEmpty()
        && !AlphaSet.IsEmpty()
        && !BetaSet.IsEmpty();
    Test.TestTrue(
        *FString::Printf(
            TEXT("%s discovers both variable Getter and Setter actions"),
            *Surface),
        bDiscovered);
    Test.TestNotEqual(
        *FString::Printf(
            TEXT("%s Getter Palette identities distinguish variables"),
            *Surface),
        AlphaGet,
        BetaGet);
    Test.TestNotEqual(
        *FString::Printf(
            TEXT("%s Setter Palette identities distinguish variables"),
            *Surface),
        AlphaSet,
        BetaSet);
    if (!bDiscovered
        || AlphaGet == BetaGet
        || AlphaSet == BetaSet)
    {
        return false;
    }

    for (const FString& PaletteId :
         {AlphaGet, BetaGet, AlphaSet, BetaSet})
    {
        FSalQuery Exact =
            RobustGraphQuery(TEXT("palette"));
        Exact.Operation->SetStringField(
            TEXT("id"),
            PaletteId);
        Exact.With.Add(TEXT("schema"));
        const TSharedPtr<FJsonObject> Result =
            FSalGraphInterface::Query(Exact, Target);
        Test.TestFalse(
            *FString::Printf(
                TEXT("%s exact variable Palette identity resolves [%s]"),
                *Surface,
                *RobustGraphDiagnosticsText(Result)),
            RobustGraphHasError(Result));
    }

    Loomle::Tests::FScopedIsolatedTransactor Transactions;
    if (!Test.TestTrue(
            *FString::Printf(
                TEXT("%s variable Palette test isolates Undo history"),
                *Surface),
            Transactions.Initialize()))
    {
        return false;
    }

    FSalPatch Patch;
    Patch.Alias = TEXT("graph");
    Patch.bDryRun = true;
    Patch.Statements = {
        RobustGraphBinding(
            TEXT("AlphaSet"),
            AlphaSet,
            SetType),
        RobustGraphUnary(
            TEXT("add"),
            RobustGraphLocal(TEXT("AlphaSet"))),
        RobustGraphBinding(
            TEXT("BetaSet"),
            BetaSet,
            SetType),
        RobustGraphUnary(
            TEXT("add"),
            RobustGraphLocal(TEXT("BetaSet")))};

    const int32 OriginalNodeCount =
        Fixture.Graph->Nodes.Num();
    const TSharedPtr<FJsonObject> DryRun =
        FSalGraphInterface::Patch(Patch, Target);
    const bool bDryRunValid =
        RobustGraphResultBool(DryRun, TEXT("valid"));
    Test.TestTrue(
        *FString::Printf(
            TEXT("%s variable Setter dry run resolves through sandbox [%s]"),
            *Surface,
            *RobustGraphDiagnosticsText(DryRun)),
        bDryRunValid);
    Test.TestEqual(
        *FString::Printf(
            TEXT("%s variable Setter dry run leaves source unchanged"),
            *Surface),
        Fixture.Graph->Nodes.Num(),
        OriginalNodeCount);
    if (!bDryRunValid)
    {
        Transactions.Restore();
        return false;
    }

    Patch.bDryRun = false;
    const TSharedPtr<FJsonObject> Applied =
        FSalGraphInterface::Patch(Patch, Target);
    const bool bApplied =
        RobustGraphResultBool(Applied, TEXT("valid"))
        && RobustGraphResultBool(Applied, TEXT("applied"));
    Test.TestTrue(
        *FString::Printf(
            TEXT("%s creates both exact variable Setter Nodes [%s]"),
            *Surface,
            *RobustGraphDiagnosticsText(Applied)),
        bApplied);
    if (!bApplied)
    {
        Transactions.Restore();
        return false;
    }

    FGuid AlphaNodeGuid;
    FGuid BetaNodeGuid;
    const bool bResolved =
        FGuid::Parse(
            RobustGraphResolvedRef(Applied, TEXT("AlphaSet")),
            AlphaNodeGuid)
        && FGuid::Parse(
            RobustGraphResolvedRef(Applied, TEXT("BetaSet")),
            BetaNodeGuid);
    Test.TestTrue(
        *FString::Printf(
            TEXT("%s resolves both Setter aliases"),
            *Surface),
        bResolved);
    UK2Node_VariableSet* AlphaNode =
        bResolved
            ? Cast<UK2Node_VariableSet>(
                FRobustGraphFixture::FindNodeByGuid(
                    Fixture.Graph,
                    AlphaNodeGuid))
            : nullptr;
    UK2Node_VariableSet* BetaNode =
        bResolved
            ? Cast<UK2Node_VariableSet>(
                FRobustGraphFixture::FindNodeByGuid(
                    Fixture.Graph,
                    BetaNodeGuid))
            : nullptr;
    Test.TestNotNull(
        *FString::Printf(
            TEXT("%s Alpha Setter has the requested native type"),
            *Surface),
        AlphaNode);
    Test.TestNotNull(
        *FString::Printf(
            TEXT("%s Beta Setter has the requested native type"),
            *Surface),
        BetaNode);
    Test.TestTrue(
        *FString::Printf(
            TEXT("%s Alpha Setter preserves the requested member identity"),
            *Surface),
        AlphaNode != nullptr
            && AlphaNode->VariableReference.GetMemberGuid()
                == Fixture.VariableGuid(Fixture.AlphaName));
    Test.TestTrue(
        *FString::Printf(
            TEXT("%s Beta Setter preserves the requested member identity"),
            *Surface),
        BetaNode != nullptr
            && BetaNode->VariableReference.GetMemberGuid()
                == Fixture.VariableGuid(Fixture.BetaName));

    if (bResolved)
    {
        FSalPatch Remove;
        Remove.Alias = TEXT("graph");
        Remove.Statements = {
            RobustGraphUnary(
                TEXT("remove"),
                RobustGraphTyped(
                    TEXT("node"),
                    AlphaNodeGuid)),
            RobustGraphUnary(
                TEXT("remove"),
                RobustGraphTyped(
                    TEXT("node"),
                    BetaNodeGuid))};
        const TSharedPtr<FJsonObject> Removed =
            FSalGraphInterface::Patch(Remove, Target);
        Test.TestTrue(
            *FString::Printf(
                TEXT("%s removes both temporary Setter Nodes"),
                *Surface),
            RobustGraphResultBool(Removed, TEXT("valid"))
                && RobustGraphResultBool(
                    Removed,
                    TEXT("applied")));
    }
    Transactions.Restore();
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FSalRobustGraphWidgetMemberPaletteSandboxTest,
    "Loomle.Sal.Robustness.Graph.WidgetMemberPaletteSandbox",
    EAutomationTestFlags::EditorContext
        | EAutomationTestFlags::EngineFilter)

bool FSalRobustGraphWidgetMemberPaletteSandboxTest::RunTest(
    const FString& Parameters)
{
    if (!RobustGraphRequireIdleEditor(
            *this,
            TEXT("Widget member Palette sandbox coverage")))
    {
        return false;
    }
    FWidgetMemberPaletteFixture Fixture;
    if (!TestTrue(
            TEXT("Widget member Palette fixture is valid"),
            Fixture.IsValid()))
    {
        return false;
    }
    const FSalResolvedTarget Target =
        RobustGraphTarget(
            Fixture.Blueprint,
            Fixture.TargetGraph);
    const FString CallType =
        TEXT("/Script/BlueprintGraph.K2Node_CallFunction");
    const FString GetterType =
        TEXT("/Script/BlueprintGraph.K2Node_VariableGet");

    auto FindFunctionPalette =
        [&Target, &CallType](const FString& Text)
        {
            FSalQuery Query =
                RobustGraphQuery(TEXT("palette_entries"));
            Query.Operation->SetStringField(TEXT("text"), Text);
            Query.PageLimit = 20;
            return RobustGraphFindPaletteId(
                FSalGraphInterface::Query(Query, Target),
                CallType);
        };
    const FString LoadPagePalette =
        FindFunctionPalette(TEXT("Load Page"));
    const FString SetActivePalette =
        FindFunctionPalette(TEXT("Set Active Nav Item"));
    const FString WidgetGetterPalette =
        RobustGraphFindVariablePaletteId(
            Target,
            TEXT("Get"),
            Fixture.MemberWidgetName,
            GetterType);
    TestFalse(
        TEXT("Widget Blueprint discovers Load Page function action"),
        LoadPagePalette.IsEmpty());
    TestFalse(
        TEXT("Widget Blueprint discovers Set Active Nav Item function action"),
        SetActivePalette.IsEmpty());
    TestFalse(
        TEXT("Widget Blueprint discovers generated Widget Getter action"),
        WidgetGetterPalette.IsEmpty());
    TestNotEqual(
        TEXT("Distinct Widget Blueprint functions have distinct Palette identities"),
        LoadPagePalette,
        SetActivePalette);
    if (LoadPagePalette.IsEmpty()
        || SetActivePalette.IsEmpty()
        || WidgetGetterPalette.IsEmpty()
        || LoadPagePalette == SetActivePalette)
    {
        return false;
    }

    for (const FString& PaletteId :
         {LoadPagePalette,
          SetActivePalette,
          WidgetGetterPalette})
    {
        FSalQuery Exact =
            RobustGraphQuery(TEXT("palette"));
        Exact.Operation->SetStringField(
            TEXT("id"),
            PaletteId);
        Exact.With.Add(TEXT("schema"));
        const TSharedPtr<FJsonObject> Result =
            FSalGraphInterface::Query(Exact, Target);
        TestFalse(
            *FString::Printf(
                TEXT("Exact Widget member Palette schema resolves [%s]"),
                *RobustGraphDiagnosticsText(Result)),
            RobustGraphHasError(Result));
        TestTrue(
            TEXT("Spawnable exact Widget member Palette advertises bind and add"),
            RobustGraphHasComment(
                Result,
                TEXT("bind { palette: ... } then add or insert")));
    }

    Loomle::Tests::FScopedIsolatedTransactor Transactions;
    if (!TestTrue(
            TEXT("Widget member Palette test isolates Undo history"),
            Transactions.Initialize()))
    {
        return false;
    }
    FSalPatch Patch;
    Patch.Alias = TEXT("graph");
    Patch.bDryRun = true;
    Patch.Statements = {
        RobustGraphBinding(
            TEXT("LoadPage"),
            LoadPagePalette,
            CallType),
        RobustGraphUnary(
            TEXT("add"),
            RobustGraphLocal(TEXT("LoadPage"))),
        RobustGraphBinding(
            TEXT("SetActive"),
            SetActivePalette,
            CallType),
        RobustGraphUnary(
            TEXT("add"),
            RobustGraphLocal(TEXT("SetActive"))),
        RobustGraphBinding(
            TEXT("WidgetGetter"),
            WidgetGetterPalette,
            GetterType),
        RobustGraphUnary(
            TEXT("add"),
            RobustGraphLocal(TEXT("WidgetGetter")))};

    const int32 OriginalNodeCount =
        Fixture.TargetGraph->Nodes.Num();
    const TSharedPtr<FJsonObject> DryRun =
        FSalGraphInterface::Patch(Patch, Target);
    TestTrue(
        *FString::Printf(
            TEXT("Widget member Palette dry run resolves through sandbox [%s]"),
            *RobustGraphDiagnosticsText(DryRun)),
        RobustGraphResultBool(DryRun, TEXT("valid")));
    TestEqual(
        TEXT("Widget member Palette dry run leaves live Graph unchanged"),
        Fixture.TargetGraph->Nodes.Num(),
        OriginalNodeCount);
    if (!RobustGraphResultBool(DryRun, TEXT("valid")))
    {
        Transactions.Restore();
        return false;
    }

    Patch.bDryRun = false;
    const TSharedPtr<FJsonObject> Applied =
        FSalGraphInterface::Patch(Patch, Target);
    const bool bApplied =
        RobustGraphResultBool(Applied, TEXT("valid"))
        && RobustGraphResultBool(Applied, TEXT("applied"));
    TestTrue(
        *FString::Printf(
            TEXT("Widget member Palette live Patch applies [%s]"),
            *RobustGraphDiagnosticsText(Applied)),
        bApplied);
    if (!bApplied)
    {
        Transactions.Restore();
        return false;
    }

    auto ResolveNode =
        [&Fixture, &Applied](const FString& Alias)
            -> UEdGraphNode*
        {
            FGuid Guid;
            return FGuid::Parse(
                    RobustGraphResolvedRef(Applied, Alias),
                    Guid)
                ? FRobustGraphFixture::FindNodeByGuid(
                    Fixture.TargetGraph,
                    Guid)
                : nullptr;
        };
    UK2Node_CallFunction* LoadPageNode =
        Cast<UK2Node_CallFunction>(
            ResolveNode(TEXT("LoadPage")));
    UK2Node_CallFunction* SetActiveNode =
        Cast<UK2Node_CallFunction>(
            ResolveNode(TEXT("SetActive")));
    UK2Node_VariableGet* WidgetGetterNode =
        Cast<UK2Node_VariableGet>(
            ResolveNode(TEXT("WidgetGetter")));
    TestTrue(
        TEXT("Load Page Node retains self function identity"),
        LoadPageNode != nullptr
            && LoadPageNode->GetFunctionName()
                == Fixture.LoadPageFunctionName
            && LoadPageNode->FunctionReference.IsSelfContext());
    TestTrue(
        TEXT("Set Active Nav Item Node retains self function identity"),
        SetActiveNode != nullptr
            && SetActiveNode->GetFunctionName()
                == Fixture.SetActiveFunctionName
            && SetActiveNode->FunctionReference.IsSelfContext());
    TestTrue(
        TEXT("Widget Getter retains generated self member identity"),
        WidgetGetterNode != nullptr
            && WidgetGetterNode->VariableReference.GetMemberName()
                == Fixture.MemberWidgetName
            && WidgetGetterNode->VariableReference.IsSelfContext());
    TestEqual(
        TEXT("Live Widget member Patch adds exactly three Nodes"),
        Fixture.TargetGraph->Nodes.Num(),
        OriginalNodeCount + 3);
    TestTrue(
        TEXT("Undo removes the Widget member Nodes atomically"),
        GEditor->UndoTransaction(false));
    TestEqual(
        TEXT("Undo restores the original Widget function Graph"),
        Fixture.TargetGraph->Nodes.Num(),
        OriginalNodeCount);
    Transactions.Restore();
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FSalRobustGraphVariablePaletteIdentityTest,
    "Loomle.Sal.Robustness.Graph.VariablePaletteIdentity",
    EAutomationTestFlags::EditorContext
        | EAutomationTestFlags::EngineFilter)

bool FSalRobustGraphVariablePaletteIdentityTest::RunTest(
    const FString& Parameters)
{
    if (!RobustGraphRequireIdleEditor(
            *this,
            TEXT("Graph variable Palette identity coverage")))
    {
        return false;
    }
    const bool bBlueprint =
        RobustGraphRunVariablePaletteIdentityCase(
            *this,
            false);
    const bool bAnimBlueprint =
        RobustGraphRunVariablePaletteIdentityCase(
            *this,
            true);
    return bBlueprint && bAnimBlueprint;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FSalRobustGraphComponentBoundPaletteIdentityTest,
    "Loomle.Sal.Robustness.Graph.ComponentBoundPaletteIdentity",
    EAutomationTestFlags::EditorContext
        | EAutomationTestFlags::EngineFilter)

bool FSalRobustGraphComponentBoundPaletteIdentityTest::RunTest(
    const FString& Parameters)
{
    if (!RobustGraphRequireIdleEditor(
            *this,
            TEXT("Graph component-bound Palette identity coverage")))
    {
        return false;
    }
    FComponentBoundPaletteFixture Fixture;
    if (!TestTrue(
            TEXT("Component-bound Palette fixture is valid"),
            Fixture.IsValid()))
    {
        return false;
    }
    const FSalResolvedTarget Target =
        RobustGraphTarget(
            Fixture.Blueprint,
            Fixture.FunctionGraph);

    auto DiscoverUniquePaletteIds = [this, &Target](
        const FString& Surface,
        UEdGraphPin* Pin)
    {
        const TSharedPtr<FJsonObject> Result =
            FSalGraphInterface::Query(
                RobustGraphPaletteFromPin(
                    TEXT("Set Relative Rotation"),
                    Pin),
                Target);
        TestFalse(
            *FString::Printf(
                TEXT("%s Palette discovery succeeds [%s]"),
                *Surface,
                *RobustGraphDiagnosticsText(Result)),
            RobustGraphHasError(Result));
        const TArray<FString> Ids =
            RobustGraphPaletteIds(Result);
        TSet<FString> UniqueIds;
        for (const FString& Id : Ids)
        {
            UniqueIds.Add(Id);
        }
        TestTrue(
            *FString::Printf(
                TEXT("%s discovers unbound and component-bound actions"),
                *Surface),
            Ids.Num() >= 3);
        TestEqual(
            *FString::Printf(
                TEXT("%s gives every visible action a unique Palette identity"),
                *Surface),
            UniqueIds.Num(),
            Ids.Num());
        return !RobustGraphHasError(Result)
                && Ids.Num() >= 3
                && UniqueIds.Num() == Ids.Num()
            ? Ids
            : TArray<FString>();
    };

    const TArray<FString> ObjectContextIds =
        DiscoverUniquePaletteIds(
            TEXT("Component object Pin context"),
            Fixture.PreviewPin);
    const TArray<FString> RotatorContextIds =
        DiscoverUniquePaletteIds(
            TEXT("Rotator Pin context"),
            Fixture.RotatorPin);

    auto VerifyExactPaletteIds = [this, &Target](
        const FString& Surface,
        const TArray<FString>& PaletteIds)
    {
        for (const FString& PaletteId : PaletteIds)
        {
            FSalQuery Exact = RobustGraphQuery(TEXT("palette"));
            Exact.Operation->SetStringField(TEXT("id"), PaletteId);
            Exact.With.Add(TEXT("schema"));
            const TSharedPtr<FJsonObject> ExactResult =
                FSalGraphInterface::Query(Exact, Target);
            TestFalse(
                *FString::Printf(
                    TEXT("Every copied %s Palette id resolves uniquely [%s]"),
                    *Surface,
                    *RobustGraphDiagnosticsText(ExactResult)),
                RobustGraphHasError(ExactResult));
        }
    };
    VerifyExactPaletteIds(
        TEXT("component-Pin"),
        ObjectContextIds);
    VerifyExactPaletteIds(
        TEXT("Rotator-Pin"),
        RotatorContextIds);

    const TSharedPtr<FJsonObject> PreviewDiscovery =
        FSalGraphInterface::Query(
            RobustGraphPaletteFromPin(
                TEXT("Set Relative Rotation (PreviewMesh)"),
                Fixture.PreviewPin),
            Target);
    TestFalse(
        *FString::Printf(
            TEXT("PreviewMesh-bound action discovery succeeds [%s]"),
            *RobustGraphDiagnosticsText(PreviewDiscovery)),
        RobustGraphHasError(PreviewDiscovery));
    const TArray<FString> PreviewIds =
        RobustGraphPaletteIds(PreviewDiscovery);
    TestTrue(
        TEXT("PreviewMesh title discovers at least one action"),
        !PreviewIds.IsEmpty());
    if (ObjectContextIds.IsEmpty()
        || RotatorContextIds.IsEmpty()
        || PreviewIds.IsEmpty())
    {
        return false;
    }

    Loomle::Tests::FScopedIsolatedTransactor Transactions;
    if (!TestTrue(
            TEXT("Component-bound Palette test isolates Undo history"),
            Transactions.Initialize()))
    {
        return false;
    }
    const int32 OriginalNodeCount =
        Fixture.FunctionGraph->Nodes.Num();
    const bool bDirtyBefore = Fixture.Package->IsDirty();
    bool bDryRunsValid = true;
    for (int32 Index = 0; Index < PreviewIds.Num(); ++Index)
    {
        const FString Alias = FString::Printf(
            TEXT("PreviewRotation%d"),
            Index);
        FSalPatch DryRunPatch;
        DryRunPatch.Alias = TEXT("graph");
        DryRunPatch.bDryRun = true;
        DryRunPatch.Statements = {
            RobustGraphBinding(Alias, PreviewIds[Index]),
            RobustGraphUnary(
                TEXT("add"),
                RobustGraphLocal(Alias))};
        const TSharedPtr<FJsonObject> DryRun =
            FSalGraphInterface::Patch(
                DryRunPatch,
                Target);
        const bool bDryRunValid =
            RobustGraphResultBool(DryRun, TEXT("valid"));
        TestTrue(
            *FString::Printf(
                TEXT("Copied PreviewMesh action %d supports dry run [%s]"),
                Index,
                *RobustGraphDiagnosticsText(DryRun)),
            bDryRunValid);
        bDryRunsValid &= bDryRunValid;
    }
    TestEqual(
        TEXT("Component-bound dry runs preserve the live Graph"),
        Fixture.FunctionGraph->Nodes.Num(),
        OriginalNodeCount);
    TestEqual(
        TEXT("Component-bound dry runs preserve Package dirty state"),
        Fixture.Package->IsDirty(),
        bDirtyBefore);

    bool bCorrectBinding = false;
    for (int32 Index = 0;
         bDryRunsValid
             && !bCorrectBinding
             && Index < PreviewIds.Num();
         ++Index)
    {
        const FString Alias = FString::Printf(
            TEXT("PreviewRotation%d"),
            Index);
        FSalPatch Apply;
        Apply.Alias = TEXT("graph");
        Apply.Statements = {
            RobustGraphBinding(Alias, PreviewIds[Index]),
            RobustGraphUnary(
                TEXT("add"),
                RobustGraphLocal(Alias))};
        const TSharedPtr<FJsonObject> Applied =
            FSalGraphInterface::Patch(Apply, Target);
        const bool bApplied =
            RobustGraphResultBool(Applied, TEXT("valid"))
            && RobustGraphResultBool(
                Applied,
                TEXT("applied"));
        TestTrue(
            *FString::Printf(
                TEXT("Copied PreviewMesh action %d applies uniquely [%s]"),
                Index,
                *RobustGraphDiagnosticsText(Applied)),
            bApplied);

        FGuid CreatedGuid;
        const bool bResolved =
            bApplied
            && FGuid::Parse(
                RobustGraphResolvedRef(Applied, Alias),
                CreatedGuid);
        const UEdGraphNode* CreatedNode =
            bResolved
                ? FRobustGraphFixture::FindNodeByGuid(
                    Fixture.FunctionGraph,
                    CreatedGuid)
                : nullptr;
        FGuid BindingGetterGuid;
        if (const UK2Node_CallFunctionOnMember* CallOnMember =
                Cast<UK2Node_CallFunctionOnMember>(CreatedNode))
        {
            bCorrectBinding =
                CallOnMember->MemberVariableToCallOn.GetMemberName()
                == Fixture.PreviewName;
        }
        else if (const UK2Node_CallFunction* Call =
                     Cast<UK2Node_CallFunction>(CreatedNode))
        {
            const UEdGraphPin* SelfPin =
                Call->FindPin(UEdGraphSchema_K2::PN_Self);
            if (SelfPin != nullptr)
            {
                for (const UEdGraphPin* LinkedPin :
                     SelfPin->LinkedTo)
                {
                    const UK2Node_VariableGet* Getter =
                        LinkedPin != nullptr
                            ? Cast<UK2Node_VariableGet>(
                                LinkedPin->GetOwningNode())
                            : nullptr;
                    if (Getter != nullptr
                        && Getter != Fixture.PreviewGetter
                        && Getter->VariableReference.GetMemberName()
                            == Fixture.PreviewName)
                    {
                        bCorrectBinding = true;
                        BindingGetterGuid = Getter->NodeGuid;
                        break;
                    }
                }
            }
        }
        if (!bResolved)
        {
            continue;
        }
        FSalPatch Remove;
        Remove.Alias = TEXT("graph");
        Remove.Statements = {
            RobustGraphUnary(
                TEXT("remove"),
                RobustGraphTyped(
                    TEXT("node"),
                    CreatedGuid))};
        if (BindingGetterGuid.IsValid())
        {
            Remove.Statements.Add(
                RobustGraphUnary(
                    TEXT("remove"),
                    RobustGraphTyped(
                        TEXT("node"),
                        BindingGetterGuid)));
        }
        const TSharedPtr<FJsonObject> Removed =
            FSalGraphInterface::Patch(Remove, Target);
        TestTrue(
            TEXT("Component-bound Palette test removes its temporary Node"),
            RobustGraphResultBool(Removed, TEXT("valid"))
                && RobustGraphResultBool(
                    Removed,
                    TEXT("applied")));
    }
    TestTrue(
        TEXT("A copied PreviewMesh action preserves its native component binding"),
        bCorrectBinding);
    Transactions.Restore();
    return bDryRunsValid
        && bCorrectBinding;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FSalRobustGraphNodeLifecycleTest,
    "Loomle.Sal.Robustness.Graph.NodeLifecycle",
    EAutomationTestFlags::EditorContext
        | EAutomationTestFlags::EngineFilter)

bool FSalRobustGraphNodeLifecycleTest::RunTest(
    const FString& Parameters)
{
    if (!RobustGraphRequireIdleEditor(
            *this,
            TEXT("Graph Node lifecycle coverage")))
    {
        return false;
    }
    Loomle::Tests::FScopedIsolatedTransactor Transactions;
    if (!TestTrue(
            TEXT("Node lifecycle isolates Undo history"),
            Transactions.Initialize()))
    {
        return false;
    }
    FRobustGraphFixture Fixture;
    if (!TestTrue(TEXT("Node lifecycle fixture is valid"), Fixture.IsValid()))
    {
        Transactions.Restore();
        return false;
    }
    const FSalResolvedTarget Target =
        RobustGraphTarget(Fixture.Blueprint, Fixture.Graph);
    FSalQuery Palette = RobustGraphQuery(TEXT("palette_entries"));
    Palette.Operation->SetStringField(TEXT("text"), TEXT("Branch"));
    const FString BranchPalette = RobustGraphFindPaletteId(
        FSalGraphInterface::Query(Palette, Target),
        TEXT("/Script/BlueprintGraph.K2Node_IfThenElse"));
    if (!TestFalse(TEXT("Node lifecycle discovers Branch Palette"), BranchPalette.IsEmpty()))
    {
        Transactions.Restore();
        return false;
    }

    FSalPatch Patch;
    Patch.Alias = TEXT("graph");
    Patch.bDryRun = true;
    Patch.Statements = {
        RobustGraphBinding(
            TEXT("CreatedBranch"),
            BranchPalette,
            TEXT("/Script/BlueprintGraph.K2Node_IfThenElse")),
        RobustGraphUnary(
            TEXT("add"),
            RobustGraphLocal(TEXT("CreatedBranch"))),
        RobustGraphEdgeOperation(
            TEXT("connect"),
            RobustGraphTyped(
                TEXT("pin"),
                Fixture.LooseThen->PinId),
            RobustGraphMember(
                RobustGraphLocal(TEXT("CreatedBranch")),
                TEXT("execute"))),
        RobustGraphSet(
            RobustGraphMember(
                RobustGraphLocal(TEXT("CreatedBranch")),
                TEXT("NodeComment")),
            MakeShared<FJsonValueString>(
                TEXT("Created through robust SAL lifecycle"))),
        RobustGraphMoveTo(
            RobustGraphLocal(TEXT("CreatedBranch")),
            FIntPoint(256, 128))
    };
    const int32 OriginalNodeCount = Fixture.Graph->Nodes.Num();
    const TSharedPtr<FJsonObject> DryRun =
        FSalGraphInterface::Patch(Patch, Target);
    const bool bDryRunValid =
        RobustGraphResultBool(DryRun, TEXT("valid"));
    TestTrue(
        *FString::Printf(
            TEXT("Node add/connect/set/move dry run validates [%s]"),
            *RobustGraphDiagnosticsText(DryRun)),
        bDryRunValid);
    TestFalse(
        TEXT("Node lifecycle dry run does not apply"),
        RobustGraphResultBool(DryRun, TEXT("applied")));
    TestEqual(
        TEXT("Node lifecycle dry run leaves source count unchanged"),
        Fixture.Graph->Nodes.Num(),
        OriginalNodeCount);
    TestTrue(
        TEXT("Node lifecycle dry run leaves source Edge unchanged"),
        Fixture.LooseThen->LinkedTo.IsEmpty());
    if (!bDryRunValid)
    {
        Transactions.Restore();
        return false;
    }

    Patch.bDryRun = false;
    const TSharedPtr<FJsonObject> Applied =
        FSalGraphInterface::Patch(Patch, Target);
    const bool bApplied =
        RobustGraphResultBool(Applied, TEXT("valid"))
        && RobustGraphResultBool(Applied, TEXT("applied"));
    TestTrue(
        *FString::Printf(
            TEXT("Node add/connect/set/move applies [%s]"),
            *RobustGraphDiagnosticsText(Applied)),
        bApplied);
    if (!bApplied)
    {
        Transactions.Restore();
        return false;
    }
    TestEqual(
        TEXT("Live lifecycle adds exactly one Node"),
        Fixture.Graph->Nodes.Num(),
        OriginalNodeCount + 1);
    const FString CreatedId =
        RobustGraphResolvedRef(Applied, TEXT("CreatedBranch"));
    FGuid CreatedGuid;
    TestTrue(
        TEXT("Live lifecycle resolves creation alias to stable Node id"),
        FGuid::Parse(CreatedId, CreatedGuid));
    UEdGraphNode* Created =
        FRobustGraphFixture::FindNodeByGuid(
            Fixture.Graph,
            CreatedGuid);
    TestNotNull(TEXT("Created Node resolves natively"), Created);
    TestTrue(
        TEXT("Created Node is connected to prior source Pin"),
        Created != nullptr
            && Fixture.LooseThen->LinkedTo.Contains(
                Created->FindPin(
                    UEdGraphSchema_K2::PN_Execute,
                    EGPD_Input)));
    TestTrue(
        TEXT("Created Node preserves authored comment"),
        Created != nullptr
            && Created->NodeComment
                == TEXT("Created through robust SAL lifecycle"));
    TestTrue(
        TEXT("Created Node preserves the authored absolute position"),
        Created != nullptr
            && Created->NodePosX == 256
            && Created->NodePosY == 128);

    FSalQuery ExactNode = RobustGraphQuery(TEXT("node"));
    ExactNode.Operation->SetStringField(TEXT("id"), CreatedId);
    ExactNode.With = {TEXT("layout"), TEXT("schema")};
    const TSharedPtr<FJsonObject> Readback =
        FSalGraphInterface::Query(ExactNode, Target);
    TestFalse(
        TEXT("Created Node is queryable immediately"),
        RobustGraphHasError(Readback));
    TestTrue(
        TEXT("Created Node readback includes all Branch Pins"),
        RobustGraphCallArgs(Readback, TEXT("pin")).Num() >= 4);

    FSalPatch Remove;
    Remove.Alias = TEXT("graph");
    Remove.Statements = {
        RobustGraphUnary(
            TEXT("remove"),
            RobustGraphTyped(TEXT("node"), CreatedGuid))};
    const TSharedPtr<FJsonObject> Removed =
        FSalGraphInterface::Patch(Remove, Target);
    TestTrue(
        TEXT("Created Node removal applies"),
        RobustGraphResultBool(Removed, TEXT("valid"))
            && RobustGraphResultBool(Removed, TEXT("applied")));
    TestNull(
        TEXT("Created Node is absent after remove"),
        FRobustGraphFixture::FindNodeByGuid(
            Fixture.Graph,
            CreatedGuid));
    TestTrue(
        TEXT("Remove automatically breaks connected Edge"),
        Fixture.LooseThen->LinkedTo.IsEmpty());

    TestTrue(
        TEXT("Undo restores removed Node and Edge"),
        GEditor->UndoTransaction(false));
    TestNotNull(
        TEXT("Undo restores Node identity"),
        FRobustGraphFixture::FindNodeByGuid(
            Fixture.Graph,
            CreatedGuid));
    TestFalse(
        TEXT("Undo restores source Edge"),
        Fixture.LooseThen->LinkedTo.IsEmpty());

    TestTrue(
        TEXT("Second Undo removes the original lifecycle batch"),
        GEditor->UndoTransaction(false));
    TestNull(
        TEXT("Undo creation removes the Node"),
        FRobustGraphFixture::FindNodeByGuid(
            Fixture.Graph,
            CreatedGuid));
    TestEqual(
        TEXT("Undo creation returns Node count to baseline"),
        Fixture.Graph->Nodes.Num(),
        OriginalNodeCount);
    Transactions.Restore();
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FSalRobustGraphPinLifetimeDisconnectTest,
    "Loomle.Sal.Robustness.Graph.PinLifetimeDisconnect",
    EAutomationTestFlags::EditorContext
        | EAutomationTestFlags::EngineFilter)

bool FSalRobustGraphPinLifetimeDisconnectTest::RunTest(
    const FString& Parameters)
{
    if (!RobustGraphRequireIdleEditor(
            *this,
            TEXT("Graph Pin lifetime disconnect regression")))
    {
        return false;
    }
    Loomle::Tests::FScopedIsolatedTransactor Transactions;
    if (!TestTrue(
            TEXT("Pin lifetime regression isolates Undo history"),
            Transactions.Initialize()))
    {
        return false;
    }
    FRobustGraphFixture Fixture;
    if (!TestTrue(TEXT("Pin lifetime fixture is valid"), Fixture.IsValid()))
    {
        Transactions.Restore();
        return false;
    }

    UK2Node_MacroInstance* ForEachLoop =
        RobustGraphAddForEachLoopWithBreak(
            Fixture.Graph,
            FIntPoint(350, 500));
    UK2Node_MakeArray* ArrayValue = RobustGraphAddMakeArray(
        Fixture.Graph,
        FIntPoint(100, 700));
    UEdGraphPin* ArrayInput = ArrayValue != nullptr
        ? ArrayValue->FindPin(TEXT("[0]"), EGPD_Input)
        : nullptr;
    if (!TestNotNull(
            TEXT("Pin lifetime fixture creates a Make Array input"),
            ArrayInput))
    {
        Transactions.Restore();
        return false;
    }

    const UEdGraphSchema_K2* Schema = GetDefault<UEdGraphSchema_K2>();
    if (!TestTrue(
            TEXT("Pin lifetime fixture resolves the Make Array element type"),
            Schema != nullptr
                && Schema->TryCreateConnection(
                    Fixture.NotOutput,
                    ArrayInput)))
    {
        Transactions.Restore();
        return false;
    }

    UEdGraphPin* ArrayOutput = ArrayValue->GetOutputPin();
    UEdGraphPin* MacroArray = ForEachLoop != nullptr
        ? ForEachLoop->FindPin(TEXT("Array"), EGPD_Input)
        : nullptr;
    if (!TestNotNull(
            TEXT("Typed Make Array exposes its current output"),
            ArrayOutput)
        || !TestNotNull(
            TEXT("ForEachLoopWithBreak exposes its Array input"),
            MacroArray)
        || !TestTrue(
            TEXT("Pin lifetime fixture connects the typed macro Array"),
            Schema->TryCreateConnection(
                ArrayOutput,
                MacroArray)))
    {
        Transactions.Restore();
        return false;
    }

    UEdGraphPin* MacroExec = ForEachLoop->FindPin(
        TEXT("Exec"),
        EGPD_Input);
    if (!TestNotNull(
            TEXT("ForEachLoopWithBreak exposes its execution input"),
            MacroExec)
        || !TestTrue(
            TEXT("Pin lifetime fixture creates the macro execution Edge"),
            Schema->TryCreateConnection(
                Fixture.LooseEntry->GetThenPin(),
                MacroExec)))
    {
        Transactions.Restore();
        return false;
    }

    UEdGraphPin* SourceExec = Fixture.LooseEntry->GetThenPin();
    MacroExec = ForEachLoop->FindPin(TEXT("Exec"), EGPD_Input);
    if (!TestTrue(
            TEXT("Macro execution Edge is authoritative after native callbacks"),
            SourceExec != nullptr
                && MacroExec != nullptr
                && SourceExec->LinkedTo.Contains(MacroExec)))
    {
        Transactions.Restore();
        return false;
    }
    const FGuid SourcePinId = SourceExec->PinId;
    const FGuid MacroPinId = MacroExec->PinId;
    const int32 OriginalNodeCount = Fixture.Graph->Nodes.Num();
    Fixture.Package->SetDirtyFlag(false);
    const bool bWasDirty = Fixture.Package->IsDirty();
    const FSalResolvedTarget Target =
        RobustGraphTarget(Fixture.Blueprint, Fixture.Graph);

    auto EdgeIsConnected = [&Fixture, ForEachLoop]()
    {
        UEdGraphPin* CurrentSource = Fixture.LooseEntry->GetThenPin();
        UEdGraphPin* CurrentTarget = ForEachLoop->FindPin(
            TEXT("Exec"),
            EGPD_Input);
        return CurrentSource != nullptr
            && CurrentTarget != nullptr
            && CurrentSource->LinkedTo.Contains(CurrentTarget);
    };

    FSalPatch Disconnect;
    Disconnect.Alias = TEXT("graph");
    Disconnect.bDryRun = true;
    Disconnect.Statements = {
        RobustGraphEdgeOperation(
            TEXT("disconnect"),
            RobustGraphTyped(TEXT("pin"), SourcePinId),
            RobustGraphTyped(TEXT("pin"), MacroPinId))};
    const TSharedPtr<FJsonObject> DryRun =
        FSalGraphInterface::Patch(Disconnect, Target);
    TestTrue(
        *FString::Printf(
            TEXT("Single stable-reference disconnect dry run survives native Pin reconstruction [%s]"),
            *RobustGraphDiagnosticsText(DryRun)),
        RobustGraphResultBool(DryRun, TEXT("valid"))
            && RobustGraphResultBool(DryRun, TEXT("dryRun"))
            && !RobustGraphResultBool(DryRun, TEXT("applied")));
    TestTrue(
        TEXT("Single disconnect dry run preserves the source Edge"),
        EdgeIsConnected());
    TestEqual(
        TEXT("Single disconnect dry run preserves source Node count"),
        Fixture.Graph->Nodes.Num(),
        OriginalNodeCount);
    TestEqual(
        TEXT("Single disconnect dry run preserves source dirty state"),
        Fixture.Package->IsDirty(),
        bWasDirty);

    FSalPatch Reorder = Disconnect;
    Reorder.Statements.Add(
        RobustGraphEdgeOperation(
            TEXT("connect"),
            RobustGraphTyped(TEXT("pin"), SourcePinId),
            RobustGraphTyped(TEXT("pin"), MacroPinId)));
    const TSharedPtr<FJsonObject> Reordered =
        FSalGraphInterface::Patch(Reorder, Target);
    TestTrue(
        *FString::Printf(
            TEXT("Ordered stable-reference disconnect/reconnect re-resolves reconstructed Pins [%s]"),
            *RobustGraphDiagnosticsText(Reordered)),
        RobustGraphResultBool(Reordered, TEXT("valid"))
            && RobustGraphResultBool(Reordered, TEXT("dryRun"))
            && !RobustGraphResultBool(Reordered, TEXT("applied")));
    TestTrue(
        TEXT("Disconnect/reconnect dry run preserves the source Edge"),
        EdgeIsConnected());
    TestEqual(
        TEXT("Disconnect/reconnect dry run preserves source dirty state"),
        Fixture.Package->IsDirty(),
        bWasDirty);

    Disconnect.bDryRun = false;
    const TSharedPtr<FJsonObject> Applied =
        FSalGraphInterface::Patch(Disconnect, Target);
    TestTrue(
        *FString::Printf(
            TEXT("Live stable-reference disconnect survives native Pin reconstruction [%s]"),
            *RobustGraphDiagnosticsText(Applied)),
        RobustGraphResultBool(Applied, TEXT("valid"))
            && RobustGraphResultBool(Applied, TEXT("applied")));
    TestFalse(
        TEXT("Live disconnect removes the macro execution Edge"),
        EdgeIsConnected());
    TestTrue(
        TEXT("Undo restores the reconstructed macro execution Edge"),
        GEditor->UndoTransaction(false));
    TestTrue(
        TEXT("Macro execution Edge is present after Undo"),
        EdgeIsConnected());

    Transactions.Restore();
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FSalRobustGraphEdgeOperationsTest,
    "Loomle.Sal.Robustness.Graph.EdgeOperationsInsert",
    EAutomationTestFlags::EditorContext
        | EAutomationTestFlags::EngineFilter)

bool FSalRobustGraphEdgeOperationsTest::RunTest(
    const FString& Parameters)
{
    if (!RobustGraphRequireIdleEditor(
            *this,
            TEXT("Graph Edge operation coverage")))
    {
        return false;
    }
    Loomle::Tests::FScopedIsolatedTransactor Transactions;
    if (!TestTrue(
            TEXT("Edge operation coverage isolates Undo history"),
            Transactions.Initialize()))
    {
        return false;
    }
    FRobustGraphFixture Fixture;
    if (!TestTrue(TEXT("Edge operation fixture is valid"), Fixture.IsValid()))
    {
        Transactions.Restore();
        return false;
    }
    const FSalResolvedTarget Target =
        RobustGraphTarget(Fixture.Blueprint, Fixture.Graph);

    FSalPatch Disconnect;
    Disconnect.Alias = TEXT("graph");
    Disconnect.Statements = {
        RobustGraphEdgeOperation(
            TEXT("disconnect"),
            RobustGraphTyped(
                TEXT("pin"),
                Fixture.BranchAThen->PinId),
            RobustGraphTyped(
                TEXT("pin"),
                Fixture.BranchBExec->PinId))};
    const TSharedPtr<FJsonObject> Disconnected =
        FSalGraphInterface::Patch(Disconnect, Target);
    TestTrue(
        TEXT("Exact disconnect applies"),
        RobustGraphResultBool(Disconnected, TEXT("valid"))
            && RobustGraphResultBool(Disconnected, TEXT("applied")));
    TestFalse(
        TEXT("Exact disconnect removes only requested Edge"),
        Fixture.BranchAThen->LinkedTo.Contains(
            Fixture.BranchBExec));
    TestTrue(
        TEXT("Exact disconnect preserves sibling execution Edge"),
        Fixture.BranchAElse->LinkedTo.Contains(
            Fixture.BranchCExec));
    TestTrue(
        TEXT("Undo restores exact disconnected Edge"),
        GEditor->UndoTransaction(false));
    TestTrue(
        TEXT("Disconnected Edge is restored"),
        Fixture.BranchAThen->LinkedTo.Contains(
            Fixture.BranchBExec));

    FSalPatch Break;
    Break.Alias = TEXT("graph");
    Break.Statements = {
        RobustGraphUnary(
            TEXT("break"),
            RobustGraphTyped(
                TEXT("pin"),
                Fixture.NotOutput->PinId))};
    const TSharedPtr<FJsonObject> Broken =
        FSalGraphInterface::Patch(Break, Target);
    TestTrue(
        TEXT("Break All Pin Links applies"),
        RobustGraphResultBool(Broken, TEXT("valid"))
            && RobustGraphResultBool(Broken, TEXT("applied")));
    TestTrue(
        TEXT("Break removes every data Edge on the Pin"),
        Fixture.NotOutput->LinkedTo.IsEmpty());
    TestTrue(
        TEXT("Undo restores all broken data Edges"),
        GEditor->UndoTransaction(false));
    TestEqual(
        TEXT("Both data consumers are restored"),
        Fixture.NotOutput->LinkedTo.Num(),
        2);

    FSalQuery Palette = RobustGraphQuery(TEXT("palette_entries"));
    Palette.Operation->SetStringField(TEXT("text"), TEXT("Sequence"));
    const FString SequencePalette = RobustGraphFindPaletteId(
        FSalGraphInterface::Query(Palette, Target),
        TEXT("/Script/BlueprintGraph.K2Node_ExecutionSequence"));
    if (!TestFalse(
            TEXT("Insert discovers Sequence Palette"),
            SequencePalette.IsEmpty()))
    {
        Transactions.Restore();
        return false;
    }

    FSalPatch Insert;
    Insert.Alias = TEXT("graph");
    Insert.Statements = {
        RobustGraphBinding(
            TEXT("InsertedSequence"),
            SequencePalette,
            TEXT("/Script/BlueprintGraph.K2Node_ExecutionSequence")),
        RobustGraphInsert(
            RobustGraphTyped(
                TEXT("pin"),
                Fixture.BranchAThen->PinId),
            RobustGraphMember(
                RobustGraphLocal(TEXT("InsertedSequence")),
                TEXT("execute")),
            RobustGraphMember(
                RobustGraphLocal(TEXT("InsertedSequence")),
                TEXT("Then_0")),
            RobustGraphTyped(
                TEXT("pin"),
                Fixture.BranchBExec->PinId))
    };
    const int32 OriginalNodeCount = Fixture.Graph->Nodes.Num();
    const TSharedPtr<FJsonObject> Inserted =
        FSalGraphInterface::Patch(Insert, Target);
    TestTrue(
        TEXT("Two-sided insert applies as one native operation"),
        RobustGraphResultBool(Inserted, TEXT("valid"))
            && RobustGraphResultBool(Inserted, TEXT("applied")));
    const FString SequenceId =
        RobustGraphResolvedRef(
            Inserted,
            TEXT("InsertedSequence"));
    FGuid SequenceGuid;
    TestTrue(
        TEXT("Insert resolves the new Node identity"),
        FGuid::Parse(SequenceId, SequenceGuid));
    UEdGraphNode* Sequence =
        FRobustGraphFixture::FindNodeByGuid(
            Fixture.Graph,
            SequenceGuid);
    UEdGraphPin* SequenceExec =
        Sequence != nullptr
            ? Sequence->FindPin(
                UEdGraphSchema_K2::PN_Execute,
                EGPD_Input)
            : nullptr;
    UEdGraphPin* SequenceThen =
        Sequence != nullptr
            ? Sequence->FindPin(TEXT("Then_0"), EGPD_Output)
            : nullptr;
    TestEqual(
        TEXT("Insert adds exactly one Node"),
        Fixture.Graph->Nodes.Num(),
        OriginalNodeCount + 1);
    TestFalse(
        TEXT("Insert removes replaced direct Edge"),
        Fixture.BranchAThen->LinkedTo.Contains(
            Fixture.BranchBExec));
    TestTrue(
        TEXT("Insert connects source to new input"),
        SequenceExec != nullptr
            && Fixture.BranchAThen->LinkedTo.Contains(
                SequenceExec));
    TestTrue(
        TEXT("Insert connects new output to destination"),
        SequenceThen != nullptr
            && SequenceThen->LinkedTo.Contains(
                Fixture.BranchBExec));

    TestTrue(
        TEXT("Undo restores insert atomically"),
        GEditor->UndoTransaction(false));
    TestNull(
        TEXT("Undo removes inserted Node"),
        FRobustGraphFixture::FindNodeByGuid(
            Fixture.Graph,
            SequenceGuid));
    TestTrue(
        TEXT("Undo restores replaced direct Edge"),
        Fixture.BranchAThen->LinkedTo.Contains(
            Fixture.BranchBExec));
    Transactions.Restore();
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FSalRobustGraphDynamicPinTest,
    "Loomle.Sal.Robustness.Graph.DynamicPinLifecycle",
    EAutomationTestFlags::EditorContext
        | EAutomationTestFlags::EngineFilter)

bool FSalRobustGraphDynamicPinTest::RunTest(
    const FString& Parameters)
{
    if (!RobustGraphRequireIdleEditor(
            *this,
            TEXT("Graph dynamic Pin coverage")))
    {
        return false;
    }
    Loomle::Tests::FScopedIsolatedTransactor Transactions;
    if (!TestTrue(
            TEXT("Dynamic Pin coverage isolates Undo history"),
            Transactions.Initialize()))
    {
        return false;
    }
    FRobustGraphFixture Fixture;
    if (!TestTrue(TEXT("Dynamic Pin fixture is valid"), Fixture.IsValid()))
    {
        Transactions.Restore();
        return false;
    }
    const FSalResolvedTarget Target =
        RobustGraphTarget(Fixture.Blueprint, Fixture.Graph);
    TSharedPtr<FJsonObject> Args = MakeShared<FJsonObject>();
    Args->SetStringField(TEXT("name"), TEXT("Payload"));
    Args->SetStringField(
        TEXT("type"),
        RobustGraphPinTypeText(UEdGraphSchema_K2::PC_Int));

    FSalPatch AddParameter;
    AddParameter.Alias = TEXT("graph");
    AddParameter.bDryRun = true;
    AddParameter.Statements = {
        RobustGraphInvoke(
            RobustGraphTyped(
                TEXT("node"),
                Fixture.LooseEntry->NodeGuid),
            TEXT("AddParameter"),
            Args)};
    const int32 OriginalPinCount =
        Fixture.LooseEntry->Pins.Num();
    const TSharedPtr<FJsonObject> DryRun =
        FSalGraphInterface::Patch(AddParameter, Target);
    TestTrue(
        TEXT("Dynamic Pin dry run validates through native reconstruction"),
        RobustGraphResultBool(DryRun, TEXT("valid")));
    TestEqual(
        TEXT("Dynamic Pin dry run leaves source Pins unchanged"),
        Fixture.LooseEntry->Pins.Num(),
        OriginalPinCount);

    AddParameter.bDryRun = false;
    const TSharedPtr<FJsonObject> Applied =
        FSalGraphInterface::Patch(AddParameter, Target);
    TestTrue(
        TEXT("Custom Event AddParameter applies"),
        RobustGraphResultBool(Applied, TEXT("valid"))
            && RobustGraphResultBool(Applied, TEXT("applied")));
    UEdGraphPin* Payload =
        Fixture.LooseEntry->FindPin(TEXT("Payload"), EGPD_Output);
    TestNotNull(
        TEXT("Dynamic Pin exists on native Custom Event"),
        Payload);
    TestEqual(
        TEXT("Dynamic Pin uses requested UE native type"),
        Payload != nullptr
            ? Payload->PinType.PinCategory
            : NAME_None,
        UEdGraphSchema_K2::PC_Int);
    if (Payload == nullptr)
    {
        Transactions.Restore();
        return false;
    }
    const FGuid PayloadId = Payload->PinId;

    FSalQuery ExactPin = RobustGraphQuery(TEXT("pin"));
    ExactPin.Operation->SetStringField(
        TEXT("id"),
        RobustGraphGuidText(PayloadId));
    ExactPin.With.Add(TEXT("schema"));
    const TSharedPtr<FJsonObject> PinSchema =
        FSalGraphInterface::Query(ExactPin, Target);
    TestFalse(
        TEXT("Dynamic Pin exact schema query succeeds"),
        RobustGraphHasError(PinSchema));
    TestTrue(
        TEXT("Dynamic authored Pin advertises RemoveParameter"),
        RobustGraphHasComment(
            PinSchema,
            TEXT("RemoveParameter")));

    FSalPatch RemoveParameter;
    RemoveParameter.Alias = TEXT("graph");
    RemoveParameter.Statements = {
        RobustGraphInvoke(
            RobustGraphTyped(TEXT("pin"), PayloadId),
            TEXT("RemoveParameter"))};
    const TSharedPtr<FJsonObject> Removed =
        FSalGraphInterface::Patch(RemoveParameter, Target);
    TestTrue(
        TEXT("Dynamic Pin RemoveParameter applies"),
        RobustGraphResultBool(Removed, TEXT("valid"))
            && RobustGraphResultBool(Removed, TEXT("applied")));
    TestNull(
        TEXT("Dynamic Pin is removed natively"),
        Fixture.LooseEntry->FindPin(TEXT("Payload"), EGPD_Output));
    TestTrue(
        TEXT("Undo restores removed dynamic Pin"),
        GEditor->UndoTransaction(false));
    TestNotNull(
        TEXT("Undo restores dynamic Pin by name"),
        Fixture.LooseEntry->FindPin(TEXT("Payload"), EGPD_Output));
    TestTrue(
        TEXT("Second Undo removes originally added dynamic Pin"),
        GEditor->UndoTransaction(false));
    TestNull(
        TEXT("Undo addition returns Pin list to baseline"),
        Fixture.LooseEntry->FindPin(TEXT("Payload"), EGPD_Output));
    TestEqual(
        TEXT("Dynamic Pin count returns to baseline"),
        Fixture.LooseEntry->Pins.Num(),
        OriginalPinCount);
    Transactions.Restore();
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FSalRobustGraphTopologyPersistenceTest,
    "Loomle.Sal.Robustness.Graph.TopologySaveUnloadReload",
    EAutomationTestFlags::EditorContext
        | EAutomationTestFlags::EngineFilter)

bool FSalRobustGraphTopologyPersistenceTest::RunTest(
    const FString& Parameters)
{
    if (!RobustGraphRequireIdleEditor(
            *this,
            TEXT("Graph topology persistence coverage")))
    {
        return false;
    }
    Loomle::Tests::FScopedIsolatedTransactor Transactions;
    if (!TestTrue(
            TEXT("Topology persistence isolates transaction history"),
            Transactions.Initialize()))
    {
        return false;
    }
    FRobustGraphFixture Fixture(true);
    if (!TestTrue(TEXT("Persistent Graph fixture is valid"), Fixture.IsValid()))
    {
        Transactions.Restore();
        return false;
    }

    const FGuid BlueprintId =
        Fixture.Blueprint->GetBlueprintGuid();
    const FGuid GraphId = Fixture.Graph->GraphGuid;
    const FGuid EntryId = Fixture.Entry->NodeGuid;
    const FGuid BranchAId = Fixture.BranchA->NodeGuid;
    const FGuid EntryThenId = Fixture.EntryThen->PinId;
    const FGuid BranchExecId = Fixture.BranchAExec->PinId;
    const TSharedPtr<FJsonObject> Finalized =
        FSalBlueprintInterface::Patch(
            RobustGraphTerminalPatch(),
            RobustGraphBlueprintTarget(Fixture.Blueprint));
    TestTrue(
        TEXT("Blueprint compile/save terminal Patch validates"),
        RobustGraphResultBool(Finalized, TEXT("valid")));
    TestTrue(
        TEXT("Blueprint compile/save terminal Patch applies"),
        RobustGraphResultBool(Finalized, TEXT("applied")));
    TestTrue(
        TEXT("Topology package is written to disk"),
        IFileManager::Get().FileExists(*Fixture.Filename));
    TestFalse(
        TEXT("Topology package is clean after save"),
        Fixture.Package->IsDirty());

    Transactions.Restore();
    FString Error;
    const bool bUnloaded = Fixture.Unload(Error);
    TestTrue(
        *FString::Printf(
            TEXT("Saved topology unloads: %s"),
            *Error),
        bUnloaded);
    Error.Reset();
    const bool bReloaded = Fixture.Reload(Error);
    TestTrue(
        *FString::Printf(
            TEXT("Saved topology reloads: %s"),
            *Error),
        bReloaded);
    if (Fixture.Blueprint == nullptr)
    {
        return false;
    }
    TestEqual(
        TEXT("Reload preserves Blueprint identity"),
        Fixture.Blueprint->GetBlueprintGuid(),
        BlueprintId);
    UEdGraph* ReloadedGraph =
        Fixture.FindGraphByGuid(GraphId);
    TestNotNull(
        TEXT("Reload preserves Graph identity"),
        ReloadedGraph);
    UEdGraphNode* ReloadedEntry =
        FRobustGraphFixture::FindNodeByGuid(
            ReloadedGraph,
            EntryId);
    UEdGraphNode* ReloadedBranch =
        FRobustGraphFixture::FindNodeByGuid(
            ReloadedGraph,
            BranchAId);
    UEdGraphPin* ReloadedThen =
        FRobustGraphFixture::FindPinByGuid(
            ReloadedGraph,
            EntryThenId);
    UEdGraphPin* ReloadedExec =
        FRobustGraphFixture::FindPinByGuid(
            ReloadedGraph,
            BranchExecId);
    TestNotNull(
        TEXT("Reload preserves source Node identity"),
        ReloadedEntry);
    TestNotNull(
        TEXT("Reload preserves destination Node identity"),
        ReloadedBranch);
    TestNotNull(
        TEXT("Reload preserves source Pin identity"),
        ReloadedThen);
    TestNotNull(
        TEXT("Reload preserves destination Pin identity"),
        ReloadedExec);
    TestTrue(
        TEXT("Reload preserves exact authored Edge topology"),
        ReloadedThen != nullptr
            && ReloadedExec != nullptr
            && ReloadedThen->LinkedTo.Contains(ReloadedExec)
            && ReloadedExec->LinkedTo.Contains(ReloadedThen));

    if (ReloadedGraph != nullptr)
    {
        const TSharedPtr<FJsonObject> Readback =
            FSalGraphInterface::Query(
                RobustGraphTraversal(
                    TEXT("exec_flow"),
                    TEXT("from"),
                    RobustGraphTyped(TEXT("node"), EntryId),
                    1),
                RobustGraphTarget(
                    Fixture.Blueprint,
                    ReloadedGraph));
        TestFalse(
            TEXT("PostLoad SAL exec flow succeeds"),
            RobustGraphHasError(Readback));
        TestTrue(
            TEXT("PostLoad SAL readback contains preserved destination"),
            RobustGraphContainsCallId(
                Readback,
                TEXT("node"),
                BranchAId));
        TestEqual(
            TEXT("PostLoad SAL readback contains preserved Edge"),
            RobustGraphEdgeCount(Readback),
            1);
    }

    Error.Reset();
    const bool bCleaned = Fixture.Cleanup(Error);
    TestTrue(
        *FString::Printf(
            TEXT("Persistent topology fixture is removed: %s"),
            *Error),
        bCleaned);
    return true;
}

// ---------------------------------------------------------------------------
// Issue #195: SAL Graph dry-run crashed Unreal inside
// FMemberReference::SetGivenSelfScope (reached through
// UK2Node_CallFunction::SetFromFunction) while materializing function call
// Nodes against the isolated preflight Blueprint. Dry run must either resolve
// against a coherent sandbox context or fail closed with a structured
// diagnostic - it must never invoke UE's spawner with an inconsistent member
// owner or self scope.
// ---------------------------------------------------------------------------

class FDryRunFunctionFixture
{
public:
    FDryRunFunctionFixture()
    {
        const FString Token =
            FGuid::NewGuid().ToString(EGuidFormats::Digits);
        const FString AssetName =
            FString::Printf(TEXT("BP_DryRunFunction_%s"), *Token);
        PackageName = FString::Printf(
            TEXT("/Game/LoomleTests/RobustGraph_%s"),
            *Token);
        Package = CreatePackage(*PackageName);
        Blueprint = Package != nullptr
            ? FKismetEditorUtilities::CreateBlueprint(
                AActor::StaticClass(),
                Package,
                FName(*AssetName),
                BPTYPE_Normal,
                UBlueprint::StaticClass(),
                UBlueprintGeneratedClass::StaticClass(),
                NAME_None)
            : nullptr;
        if (Blueprint == nullptr)
        {
            return;
        }
        EventGraph =
            FBlueprintEditorUtils::FindEventGraph(Blueprint);
        FunctionGraph =
            FBlueprintEditorUtils::CreateNewGraph(
                Blueprint,
                SelfFunctionName,
                UEdGraph::StaticClass(),
                UEdGraphSchema_K2::StaticClass());
        if (FunctionGraph != nullptr)
        {
            FBlueprintEditorUtils::AddFunctionGraph(
                Blueprint,
                FunctionGraph,
                true,
                static_cast<UClass*>(nullptr));
        }
        if (EventGraph == nullptr || FunctionGraph == nullptr)
        {
            return;
        }
        FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(
            Blueprint);
        FKismetEditorUtilities::CompileBlueprint(Blueprint);
        FBlueprintActionDatabase::Get().RefreshAssetActions(
            Blueprint);
        Package->SetDirtyFlag(false);
    }

    ~FDryRunFunctionFixture()
    {
        FString Ignored;
        Cleanup(Ignored);
    }

    FDryRunFunctionFixture(
        const FDryRunFunctionFixture&) = delete;
    FDryRunFunctionFixture& operator=(
        const FDryRunFunctionFixture&) = delete;

    bool IsValid() const
    {
        const UClass* Skeleton =
            Blueprint != nullptr
                ? Blueprint->SkeletonGeneratedClass.Get()
                : nullptr;
        return Package != nullptr
            && Blueprint != nullptr
            && EventGraph != nullptr
            && FunctionGraph != nullptr
            && Skeleton != nullptr
            && Skeleton->FindFunctionByName(
                SelfFunctionName) != nullptr;
    }

    bool Cleanup(FString& OutError)
    {
        if (bCleaned)
        {
            OutError.Reset();
            return true;
        }
        bCleaned = true;
        if (Blueprint != nullptr)
        {
            FBlueprintActionDatabase::Get().ClearAssetActions(
                Blueprint);
        }
        UPackage* PackageToUnload = Package;
        FunctionGraph = nullptr;
        EventGraph = nullptr;
        Blueprint = nullptr;
        Package = nullptr;
        return RobustGraphUnloadPackage(
            PackageToUnload,
            OutError);
    }

    UPackage* Package = nullptr;
    UBlueprint* Blueprint = nullptr;
    UEdGraph* EventGraph = nullptr;
    UEdGraph* FunctionGraph = nullptr;
    const FName SelfFunctionName = TEXT("RobustDryRunSelfFunction");

private:
    FString PackageName;
    bool bCleaned = false;
};

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FSalRobustGraphDryRunFunctionCallTest,
    "Loomle.Sal.Robustness.Graph.DryRunFunctionCall",
    EAutomationTestFlags::EditorContext
        | EAutomationTestFlags::EngineFilter)

bool FSalRobustGraphDryRunFunctionCallTest::RunTest(
    const FString& Parameters)
{
    if (!RobustGraphRequireIdleEditor(
            *this,
            TEXT("function call dry run coverage")))
    {
        return false;
    }
    FDryRunFunctionFixture Fixture;
    if (!TestTrue(
            TEXT("function call dry run fixture is valid"),
            Fixture.IsValid()))
    {
        return false;
    }
    const FString CallType =
        TEXT("/Script/BlueprintGraph.K2Node_CallFunction");
    const FSalResolvedTarget EventTarget =
        RobustGraphTarget(Fixture.Blueprint, Fixture.EventGraph);
    const FSalResolvedTarget FunctionTarget =
        RobustGraphTarget(
            Fixture.Blueprint,
            Fixture.FunctionGraph);

    auto FindFunctionPalette =
        [&EventTarget, &CallType](const FString& Text)
        {
            FSalQuery Query =
                RobustGraphQuery(TEXT("palette_entries"));
            Query.Operation->SetStringField(TEXT("text"), Text);
            Query.PageLimit = 20;
            return RobustGraphFindPaletteId(
                FSalGraphInterface::Query(Query, EventTarget),
                CallType);
        };
    // UKismetSystemLibrary::PrintString is a plain BlueprintCallable static
    // function whose Palette action materializes as K2Node_CallFunction.
    // (Add_IntInt is deliberately not used: its CompactNodeTitle /
    // CommutativeAssociativeBinaryOperator meta registers a different spawner
    // type and a DisplayName of "int + int", which would not match a plain
    // function-name search.)
    const FString NativePalette =
        FindFunctionPalette(TEXT("PrintString"));
    const FString SelfPalette =
        FindFunctionPalette(
            Fixture.SelfFunctionName.ToString());
    TestFalse(
        TEXT("native static function call is discoverable"),
        NativePalette.IsEmpty());
    TestFalse(
        TEXT("Blueprint self-context function is discoverable"),
        SelfPalette.IsEmpty());
    if (NativePalette.IsEmpty() || SelfPalette.IsEmpty())
    {
        return false;
    }

    Loomle::Tests::FScopedIsolatedTransactor Transactions;
    if (!TestTrue(
            TEXT("function call dry run test isolates Undo history"),
            Transactions.Initialize()))
    {
        return false;
    }

    // Event Graph dry run: a native static function call, a Blueprint-defined
    // self-context function call, and repeated calls from the same
    // self-context palette action (Issue #195 reproduction shape).
    FSalPatch EventDryRun;
    EventDryRun.Alias = TEXT("graph");
    EventDryRun.bDryRun = true;
    EventDryRun.Statements = {
        RobustGraphBinding(
            TEXT("Native"), NativePalette, CallType),
        RobustGraphUnary(
            TEXT("add"), RobustGraphLocal(TEXT("Native"))),
        RobustGraphBinding(
            TEXT("SelfA"), SelfPalette, CallType),
        RobustGraphUnary(
            TEXT("add"), RobustGraphLocal(TEXT("SelfA"))),
        RobustGraphBinding(
            TEXT("SelfB"), SelfPalette, CallType),
        RobustGraphUnary(
            TEXT("add"), RobustGraphLocal(TEXT("SelfB"))),
        RobustGraphBinding(
            TEXT("SelfC"), SelfPalette, CallType),
        RobustGraphUnary(
            TEXT("add"), RobustGraphLocal(TEXT("SelfC")))};

    const int32 OriginalEventNodes = Fixture.EventGraph->Nodes.Num();
    const TSharedPtr<FJsonObject> EventResult =
        FSalGraphInterface::Patch(EventDryRun, EventTarget);
    const bool bEventValid =
        RobustGraphResultBool(EventResult, TEXT("valid"));
    TestTrue(
        *FString::Printf(
            TEXT("Event Graph function dry run resolves through sandbox [%s]"),
            *RobustGraphDiagnosticsText(EventResult)),
        bEventValid);
    TestEqual(
        TEXT("Event Graph function dry run leaves live Graph unchanged"),
        Fixture.EventGraph->Nodes.Num(),
        OriginalEventNodes);
    TestFalse(
        TEXT("Event Graph dry run does not fail with spawn_failed"),
        RobustGraphHasDiagnosticCode(
            EventResult,
            TEXT("validation.spawn_failed")));
    TestFalse(
        TEXT("Event Graph dry run does not reject a spawnable Palette action"),
        RobustGraphHasDiagnosticCode(
            EventResult,
            TEXT("resolution.palette_not_spawnable")));
    if (!bEventValid)
    {
        Transactions.Restore();
        return false;
    }

    // Newly-created Function Graph dry run: the same self-context function.
    FSalPatch FunctionDryRun;
    FunctionDryRun.Alias = TEXT("graph");
    FunctionDryRun.bDryRun = true;
    FunctionDryRun.Statements = {
        RobustGraphBinding(
            TEXT("Self"), SelfPalette, CallType),
        RobustGraphUnary(
            TEXT("add"), RobustGraphLocal(TEXT("Self")))};
    const int32 OriginalFunctionNodes =
        Fixture.FunctionGraph->Nodes.Num();
    const TSharedPtr<FJsonObject> FunctionResult =
        FSalGraphInterface::Patch(
            FunctionDryRun,
            FunctionTarget);
    const bool bFunctionValid =
        RobustGraphResultBool(FunctionResult, TEXT("valid"));
    TestTrue(
        *FString::Printf(
            TEXT("Function Graph self-context dry run resolves through sandbox [%s]"),
            *RobustGraphDiagnosticsText(FunctionResult)),
        bFunctionValid);
    TestEqual(
        TEXT("Function Graph dry run leaves live Graph unchanged"),
        Fixture.FunctionGraph->Nodes.Num(),
        OriginalFunctionNodes);
    if (!bFunctionValid)
    {
        Transactions.Restore();
        return false;
    }

    Transactions.Restore();
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FSalRobustGraphSandboxClassIdentityTest,
    "Loomle.Sal.Robustness.Graph.SandboxClassIdentity",
    EAutomationTestFlags::EditorContext
        | EAutomationTestFlags::EngineFilter)

bool FSalRobustGraphSandboxClassIdentityTest::RunTest(
    const FString& Parameters)
{
    if (!RobustGraphRequireIdleEditor(
            *this,
            TEXT("sandbox class identity coverage")))
    {
        return false;
    }
    FRobustGraphFixture Fixture;
    if (!TestTrue(
            TEXT("sandbox class identity fixture is valid"),
            Fixture.IsValid()))
    {
        return false;
    }
    const FSalResolvedTarget Target =
        RobustGraphTarget(Fixture.Blueprint, Fixture.Graph);

    TStrongObjectPtr<UBlueprint> SandboxOwner;
    FSalResolvedTarget SandboxTarget;
    FString SandboxError;
    if (!TestTrue(
            *FString::Printf(
                TEXT("sandbox builds for class identity: %s"),
                *SandboxError),
            FSalGraphInterface::BuildSandboxTargetForTesting(
                Target,
                SandboxOwner,
                SandboxTarget,
                SandboxError)))
    {
        return false;
    }
    UBlueprint* Sandbox = SandboxOwner.Get();
    if (!TestNotNull(
            TEXT("sandbox class identity owner"),
            Sandbox))
    {
        return false;
    }
    UBlueprintGeneratedClass* SandboxGenerated =
        Cast<UBlueprintGeneratedClass>(
            Sandbox->GeneratedClass.Get());
    UBlueprintGeneratedClass* SandboxSkeleton =
        Cast<UBlueprintGeneratedClass>(
            Sandbox->SkeletonGeneratedClass.Get());
    TestNotNull(
        TEXT("sandbox Generated Class exists"),
        SandboxGenerated);
    TestNotNull(
        TEXT("sandbox Skeleton Class exists"),
        SandboxSkeleton);
    if (SandboxGenerated == nullptr
        || SandboxSkeleton == nullptr)
    {
        return false;
    }
    TestTrue(
        TEXT("sandbox Generated Class reports the sandbox Blueprint as owner"),
        SandboxGenerated->ClassGeneratedBy == Sandbox);
    TestTrue(
        TEXT("sandbox Skeleton Class reports the sandbox Blueprint as owner"),
        SandboxSkeleton->ClassGeneratedBy == Sandbox);
    TestTrue(
        TEXT("sandbox classes remain isolated in the transient package"),
        SandboxGenerated->IsIn(GetTransientPackage())
            && SandboxSkeleton->IsIn(GetTransientPackage()));
    TestTrue(
        TEXT("sandbox classes are distinct from the live Blueprint classes"),
        SandboxGenerated != Fixture.Blueprint->GeneratedClass
            && SandboxSkeleton
                != Fixture.Blueprint->SkeletonGeneratedClass);
    return true;
}

// ---------------------------------------------------------------------------
// Issue #196: distinct K2Node_GetSubsystem creation actions shared one SAL
// palette identity because UE's FBlueprintNodeSpawner::GetSpawnerSignature
// contains only the Node class, while each subsystem action's CustomClass
// lives in its CustomizeNodeDelegate. Every subsystem-specific creation
// action must expose a stable unique palette identity and materialize through
// exact Palette schema and dry run.
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FSalRobustGraphSubsystemPaletteIdentityTest,
    "Loomle.Sal.Robustness.Graph.SubsystemPaletteIdentity",
    EAutomationTestFlags::EditorContext
        | EAutomationTestFlags::EngineFilter)

bool FSalRobustGraphSubsystemPaletteIdentityTest::RunTest(
    const FString& Parameters)
{
    if (!RobustGraphRequireIdleEditor(
            *this,
            TEXT("subsystem Palette identity coverage")))
    {
        return false;
    }
    // Collect the same Blueprint-allowable subsystem classes that
    // UK2Node_GetSubsystem::GetMenuActions registers spawners for.
    TArray<UClass*> SubsystemClasses;
    {
        TArray<UClass*> Derived;
        GetDerivedClasses(
            UGameInstanceSubsystem::StaticClass(),
            Derived);
        SubsystemClasses.Append(Derived);
        Derived.Reset();
        GetDerivedClasses(
            UWorldSubsystem::StaticClass(),
            Derived);
        SubsystemClasses.Append(Derived);
        Derived.Reset();
        GetDerivedClasses(
            ULocalPlayerSubsystem::StaticClass(),
            Derived);
        SubsystemClasses.Append(Derived);
        Derived.Reset();
        GetDerivedClasses(
            UAudioEngineSubsystem::StaticClass(),
            Derived);
        SubsystemClasses.Append(Derived);
        SubsystemClasses.RemoveAll(
            [](const UClass* Class)
            {
                return Class == nullptr
                    || !UEdGraphSchema_K2::
                        IsAllowableBlueprintVariableType(
                            Class,
                            true);
            });
        SubsystemClasses.Sort(
            [](const UClass& A, const UClass& B)
            {
                return A.GetName() < B.GetName();
            });
        if (SubsystemClasses.Num() > 4)
        {
            SubsystemClasses.SetNum(4);
        }
    }
    TestTrue(
        TEXT("Editor exposes at least two subsystem classes"),
        SubsystemClasses.Num() >= 2);
    if (SubsystemClasses.Num() < 2)
    {
        return false;
    }

    FRobustGraphFixture Fixture;
    if (!TestTrue(
            TEXT("subsystem Palette identity fixture is valid"),
            Fixture.IsValid()))
    {
        return false;
    }
    const FSalResolvedTarget Target =
        RobustGraphTarget(Fixture.Blueprint, Fixture.Graph);
    const FString GetSubsystemType =
        TEXT("/Script/BlueprintGraph.K2Node_GetSubsystem");

    TArray<FString> PaletteIds;
    TArray<FString> Labels;
    for (const UClass* SubsystemClass : SubsystemClasses)
    {
        FSalQuery Query =
            RobustGraphQuery(TEXT("palette_entries"));
        Query.Operation->SetStringField(
            TEXT("text"),
            TEXT("Get ") + SubsystemClass->GetName());
        Query.PageLimit = 20;
        const TSharedPtr<FJsonObject> Result =
            FSalGraphInterface::Query(Query, Target);
        if (RobustGraphHasError(Result))
        {
            continue;
        }
        const FString Id = RobustGraphFindPaletteId(
            Result,
            GetSubsystemType);
        if (Id.IsEmpty())
        {
            continue;
        }
        PaletteIds.Add(Id);
        Labels.Add(SubsystemClass->GetName());
    }
    TestTrue(
        TEXT("subsystem Palette discovers at least two distinct actions"),
        PaletteIds.Num() >= 2);
    if (PaletteIds.Num() < 2)
    {
        return false;
    }

    TSet<FString> UniqueIds;
    for (const FString& Id : PaletteIds)
    {
        UniqueIds.Add(Id);
    }
    TestEqual(
        TEXT("each subsystem action has a unique Palette identity"),
        UniqueIds.Num(),
        PaletteIds.Num());

    for (int32 Index = 0; Index < 2; ++Index)
    {
        FSalQuery Exact =
            RobustGraphQuery(TEXT("palette"));
        Exact.Operation->SetStringField(
            TEXT("id"),
            PaletteIds[Index]);
        Exact.With.Add(TEXT("schema"));
        const TSharedPtr<FJsonObject> ExactResult =
            FSalGraphInterface::Query(Exact, Target);
        TestFalse(
            *FString::Printf(
                TEXT("exact subsystem Palette resolves [%s]"),
                *RobustGraphDiagnosticsText(ExactResult)),
            RobustGraphHasError(ExactResult));

        const FString Alias =
            FString::Printf(TEXT("Subsystem%d"), Index);
        FSalPatch Patch;
        Patch.Alias = TEXT("graph");
        Patch.bDryRun = true;
        Patch.Statements = {
            RobustGraphBinding(
                Alias,
                PaletteIds[Index],
                GetSubsystemType),
            RobustGraphUnary(
                TEXT("add"),
                RobustGraphLocal(Alias))};
        const TSharedPtr<FJsonObject> DryRun =
            FSalGraphInterface::Patch(Patch, Target);
        TestTrue(
            *FString::Printf(
                TEXT("subsystem action %d (%s) dry run resolves through sandbox [%s]"),
                Index,
                *Labels[Index],
                *RobustGraphDiagnosticsText(DryRun)),
            RobustGraphResultBool(DryRun, TEXT("valid")));
    }
    return true;
}

#endif
