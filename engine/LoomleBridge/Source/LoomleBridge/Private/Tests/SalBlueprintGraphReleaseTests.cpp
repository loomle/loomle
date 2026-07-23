// Copyright 2026 Loomle contributors.

#if WITH_DEV_AUTOMATION_TESTS

#include "Sal/Blueprint/SalBlueprintInterface.h"
#include "Sal/Graph/SalGraphInterface.h"
#include "Tests/LoomleTestEditorState.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraphSchema_K2.h"
#include "Editor.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "GameFramework/Actor.h"
#include "K2Node_CustomEvent.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/AutomationTest.h"
#include "UObject/GarbageCollection.h"
#include "UObject/Package.h"
#include "UObject/UObjectHash.h"

namespace
{
using namespace Loomle::Sal;

FString GuidText(const FGuid& Guid)
{
    return Guid.ToString(EGuidFormats::DigitsWithHyphensLower);
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

bool ResultBool(
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

TArray<TSharedPtr<FJsonObject>> CallArgs(
    const TSharedPtr<FJsonObject>& Result,
    const FString& Callee)
{
    TArray<TSharedPtr<FJsonObject>> Calls;
    const TSharedPtr<FJsonObject>* Object = nullptr;
    const TArray<TSharedPtr<FJsonValue>>* Statements = nullptr;
    if (!Result.IsValid()
        || !Result->TryGetObjectField(TEXT("object"), Object)
        || Object == nullptr
        || !(*Object)->TryGetArrayField(
            TEXT("statements"),
            Statements)
        || Statements == nullptr)
    {
        return Calls;
    }

    for (const TSharedPtr<FJsonValue>& StatementValue : *Statements)
    {
        const TSharedPtr<FJsonObject>* Statement = nullptr;
        const TSharedPtr<FJsonObject>* Call = nullptr;
        const TSharedPtr<FJsonObject>* Args = nullptr;
        FString ActualCallee;
        if (StatementValue.IsValid()
            && StatementValue->TryGetObject(Statement)
            && Statement != nullptr
            && (*Statement)->TryGetObjectField(TEXT("value"), Call)
            && Call != nullptr
            && (*Call)->TryGetStringField(
                TEXT("callee"),
                ActualCallee)
            && ActualCallee == Callee
            && (*Call)->TryGetObjectField(TEXT("args"), Args)
            && Args != nullptr)
        {
            Calls.Add(*Args);
        }
    }
    return Calls;
}

TSharedPtr<FJsonObject> CallArgsWithId(
    const TSharedPtr<FJsonObject>& Result,
    const FString& Callee,
    const FString& Id)
{
    for (const TSharedPtr<FJsonObject>& Args :
         CallArgs(Result, Callee))
    {
        FString ActualId;
        if (Args.IsValid()
            && Args->TryGetStringField(TEXT("id"), ActualId)
            && ActualId.Equals(Id, ESearchCase::IgnoreCase))
        {
            return Args;
        }
    }
    return nullptr;
}

bool HasMemberCall(
    const TSharedPtr<FJsonObject>& Result,
    const FString& Callee,
    const FString& ExpectedMember)
{
    const TSharedPtr<FJsonObject>* Object = nullptr;
    const TArray<TSharedPtr<FJsonValue>>* Statements = nullptr;
    if (!Result.IsValid()
        || !Result->TryGetObjectField(TEXT("object"), Object)
        || Object == nullptr
        || !(*Object)->TryGetArrayField(
            TEXT("statements"),
            Statements)
        || Statements == nullptr)
    {
        return false;
    }

    for (const TSharedPtr<FJsonValue>& StatementValue : *Statements)
    {
        const TSharedPtr<FJsonObject>* Statement = nullptr;
        const TSharedPtr<FJsonObject>* Target = nullptr;
        const TSharedPtr<FJsonObject>* Call = nullptr;
        const TArray<TSharedPtr<FJsonValue>>* Path = nullptr;
        FString TargetKind;
        FString ActualCallee;
        FString Member;
        if (StatementValue.IsValid()
            && StatementValue->TryGetObject(Statement)
            && Statement != nullptr
            && (*Statement)->TryGetObjectField(
                TEXT("target"),
                Target)
            && Target != nullptr
            && (*Target)->TryGetStringField(
                TEXT("kind"),
                TargetKind)
            && TargetKind == TEXT("member")
            && (*Target)->TryGetArrayField(TEXT("path"), Path)
            && Path != nullptr
            && Path->Num() == 1
            && (*Path)[0].IsValid()
            && (*Path)[0]->TryGetString(Member)
            && Member == ExpectedMember
            && (*Statement)->TryGetObjectField(TEXT("value"), Call)
            && Call != nullptr
            && (*Call)->TryGetStringField(
                TEXT("callee"),
                ActualCallee)
            && ActualCallee == Callee)
        {
            return true;
        }
    }
    return false;
}

bool HasCommentContaining(
    const TSharedPtr<FJsonObject>& Result,
    const FString& Needle)
{
    const TSharedPtr<FJsonObject>* Object = nullptr;
    const TArray<TSharedPtr<FJsonValue>>* Statements = nullptr;
    if (!Result.IsValid()
        || !Result->TryGetObjectField(TEXT("object"), Object)
        || Object == nullptr
        || !(*Object)->TryGetArrayField(
            TEXT("statements"),
            Statements)
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

bool ReadLayout(
    const TSharedPtr<FJsonObject>& Args,
    FIntPoint& OutPoint)
{
    const TArray<TSharedPtr<FJsonValue>>* At = nullptr;
    double X = 0.0;
    double Y = 0.0;
    if (!Args.IsValid()
        || !Args->TryGetArrayField(TEXT("at"), At)
        || At == nullptr
        || At->Num() != 2
        || !(*At)[0].IsValid()
        || !(*At)[1].IsValid()
        || !(*At)[0]->TryGetNumber(X)
        || !(*At)[1]->TryGetNumber(Y))
    {
        return false;
    }
    OutPoint = FIntPoint(
        static_cast<int32>(X),
        static_cast<int32>(Y));
    return true;
}

FSalQuery Query(
    const FString& Kind,
    const FString& Alias)
{
    FSalQuery Result;
    Result.Alias = Alias;
    Result.Operation = MakeShared<FJsonObject>();
    Result.Operation->SetStringField(TEXT("kind"), Kind);
    return Result;
}

FSalResolvedTarget BlueprintTarget(UBlueprint* Blueprint)
{
    FSalResolvedTarget Target;
    Target.Kind = ESalTargetKind::Blueprint;
    Target.Alias = TEXT("blueprint");
    Target.AssetPath =
        Blueprint != nullptr ? Blueprint->GetPathName() : FString();
    Target.Object = Blueprint;
    Target.Package =
        Blueprint != nullptr ? Blueprint->GetOutermost() : nullptr;
    Target.Blueprint = Blueprint;
    Target.Class =
        Blueprint != nullptr
            ? Blueprint->GeneratedClass.Get()
            : nullptr;
    Target.Interfaces = {
        FName(TEXT("asset")),
        FName(TEXT("blueprint"))};
    return Target;
}

FSalResolvedTarget GraphTarget(
    UBlueprint* Blueprint,
    UEdGraph* Graph)
{
    FSalResolvedTarget Target;
    Target.Kind = ESalTargetKind::Graph;
    Target.Alias = TEXT("graph");
    Target.AssetPath =
        Blueprint != nullptr ? Blueprint->GetPathName() : FString();
    Target.Id =
        Graph != nullptr ? GuidText(Graph->GraphGuid) : FString();
    Target.Name =
        Graph != nullptr ? Graph->GetName() : FString();
    Target.Object = Graph;
    Target.Package =
        Blueprint != nullptr ? Blueprint->GetOutermost() : nullptr;
    Target.Blueprint = Blueprint;
    Target.Class =
        Blueprint != nullptr
            ? Blueprint->GeneratedClass.Get()
            : nullptr;
    Target.Graph = Graph;
    Target.Interfaces = {FName(TEXT("graph"))};
    return Target;
}

FSalPatch MoveNodePatch(
    const UEdGraphNode* Node,
    const FIntPoint Delta)
{
    TSharedRef<FJsonObject> NodeRef = MakeShared<FJsonObject>();
    NodeRef->SetStringField(TEXT("kind"), TEXT("node"));
    NodeRef->SetStringField(
        TEXT("id"),
        Node != nullptr ? GuidText(Node->NodeGuid) : FString());

    TSharedRef<FJsonObject> Move = MakeShared<FJsonObject>();
    Move->SetStringField(TEXT("kind"), TEXT("move"));
    Move->SetObjectField(TEXT("target"), NodeRef);
    Move->SetArrayField(
        TEXT("by"),
        {
            MakeShared<FJsonValueNumber>(Delta.X),
            MakeShared<FJsonValueNumber>(Delta.Y)
        });

    FSalPatch Patch;
    Patch.Alias = TEXT("graph");
    Patch.bDryRun = false;
    Patch.Statements = {MakeShared<FJsonValueObject>(Move)};
    return Patch;
}

bool UnloadTestPackage(
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
        OutError = TEXT("Fixture package remained loaded: ")
            + PackageName;
        return false;
    }
    return true;
}

class FBlueprintGraphReleaseFixture
{
public:
    FBlueprintGraphReleaseFixture()
    {
        const FString Token =
            FGuid::NewGuid().ToString(EGuidFormats::Digits);
        Package = CreatePackage(*FString::Printf(
            TEXT("/Game/LoomleTests/SalBlueprintGraphRelease_%s"),
            *Token));
        Blueprint = FKismetEditorUtilities::CreateBlueprint(
            AActor::StaticClass(),
            Package,
            *FString::Printf(
                TEXT("BP_BlueprintGraphRelease_%s"),
                *Token),
            BPTYPE_Normal,
            UBlueprint::StaticClass(),
            UBlueprintGeneratedClass::StaticClass(),
            NAME_None);
        if (Blueprint == nullptr)
        {
            return;
        }

        FEdGraphPinType VariableType;
        VariableType.PinCategory = UEdGraphSchema_K2::PC_Int;
        bVariableAdded =
            FBlueprintEditorUtils::AddMemberVariable(
                Blueprint,
                VariableName,
                VariableType);
        Graph = FBlueprintEditorUtils::FindEventGraph(Blueprint);
        if (Graph != nullptr)
        {
            FGraphNodeCreator<UK2Node_CustomEvent> NodeCreator(*Graph);
            Node = NodeCreator.CreateNode(false);
            Node->CustomFunctionName = TEXT("ReleaseCoverageEvent");
            Node->NodePosX = 320;
            Node->NodePosY = 160;
            NodeCreator.Finalize();
            FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(
                Blueprint);
        }
        if (Package != nullptr)
        {
            Package->SetDirtyFlag(false);
        }
    }

    ~FBlueprintGraphReleaseFixture()
    {
        FString Ignored;
        Cleanup(Ignored);
    }

    FBlueprintGraphReleaseFixture(
        const FBlueprintGraphReleaseFixture&) = delete;
    FBlueprintGraphReleaseFixture& operator=(
        const FBlueprintGraphReleaseFixture&) = delete;

    bool IsValid() const
    {
        return Package != nullptr
            && Blueprint != nullptr
            && bVariableAdded
            && Graph != nullptr
            && Node != nullptr
            && Node->NodeGuid.IsValid()
            && !Node->Pins.IsEmpty()
            && Node->Pins[0] != nullptr
            && Node->Pins[0]->PinId.IsValid();
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
        Package = nullptr;
        Blueprint = nullptr;
        Graph = nullptr;
        Node = nullptr;
        return UnloadTestPackage(PackageToUnload, OutError);
    }

    UPackage* Package = nullptr;
    UBlueprint* Blueprint = nullptr;
    UEdGraph* Graph = nullptr;
    UK2Node_CustomEvent* Node = nullptr;
    const FName VariableName = TEXT("ReleaseCoverageValue");

private:
    bool bVariableAdded = false;
    bool bCleaned = false;
};

bool RequireEditor(
    FAutomationTestBase& Test,
    const FString& Surface)
{
    if (GEditor == nullptr)
    {
        Test.AddError(Surface + TEXT(" requires GEditor."));
        return false;
    }
    if (GEditor->IsPlaySessionInProgress())
    {
        Test.AddError(
            Surface
            + TEXT(" must run outside PIE; skipping would create a false release green."));
        return false;
    }
    return true;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FSalBlueprintGraphQueryReleaseBlockerTest,
    "Loomle.Sal.ReleaseBlocker.BlueprintGraph.QuerySurface",
    EAutomationTestFlags::EditorContext
        | EAutomationTestFlags::EngineFilter)

bool FSalBlueprintGraphQueryReleaseBlockerTest::RunTest(
    const FString& Parameters)
{
    if (!RequireEditor(*this, TEXT("Blueprint/Graph Query test")))
    {
        return false;
    }

    Loomle::Tests::FScopedIsolatedTransactor Transactions;
    if (!TestTrue(
            TEXT("Query fixture owns an isolated transaction buffer"),
            Transactions.Initialize()))
    {
        return false;
    }
    FBlueprintGraphReleaseFixture Fixture;
    if (!TestTrue(
            TEXT("Real Blueprint Query fixture is valid"),
            Fixture.IsValid()))
    {
        Transactions.Restore();
        return false;
    }

    const FSalResolvedTarget BlueprintScope =
        BlueprintTarget(Fixture.Blueprint);
    const FString BlueprintId =
        GuidText(Fixture.Blueprint->GetBlueprintGuid());
    const FString GraphId = GuidText(Fixture.Graph->GraphGuid);
    const FString NodeId = GuidText(Fixture.Node->NodeGuid);
    const FString PinId = GuidText(Fixture.Node->Pins[0]->PinId);

    const TSharedPtr<FJsonObject> BlueprintSummary =
        FSalBlueprintInterface::Query(
            Query(TEXT("summary"), TEXT("blueprint")),
            BlueprintScope);
    TestFalse(
        TEXT("Blueprint summary Query succeeds"),
        HasError(BlueprintSummary));
    TestNotNull(
        TEXT("Blueprint summary returns the exact Blueprint"),
        CallArgsWithId(
            BlueprintSummary,
            TEXT("blueprint"),
            BlueprintId).Get());
    TestTrue(
        TEXT("Blueprint summary reports the authored Variable"),
        HasCommentContaining(
            BlueprintSummary,
            TEXT("variables: 1")));
    TestTrue(
        TEXT("Blueprint summary reports Graph coverage"),
        HasCommentContaining(
            BlueprintSummary,
            TEXT("graphs:")));

    const TSharedPtr<FJsonObject> Variables =
        FSalBlueprintInterface::Query(
            Query(TEXT("variables"), TEXT("blueprint")),
            BlueprintScope);
    TestFalse(
        TEXT("Blueprint variables Query succeeds"),
        HasError(Variables));
    TestTrue(
        TEXT("Blueprint variables Query preserves the member path"),
        HasMemberCall(
            Variables,
            TEXT("variable"),
            Fixture.VariableName.ToString()));
    TestEqual(
        TEXT("Blueprint variables Query returns one authored Variable"),
        CallArgs(Variables, TEXT("variable")).Num(),
        1);

    const TSharedPtr<FJsonObject> Graphs =
        FSalBlueprintInterface::Query(
            Query(TEXT("graphs"), TEXT("blueprint")),
            BlueprintScope);
    TestFalse(
        TEXT("Blueprint graphs Query succeeds"),
        HasError(Graphs));
    TestNotNull(
        TEXT("Blueprint graphs Query contains the exact Event Graph"),
        CallArgsWithId(
            Graphs,
            TEXT("graph"),
            GraphId).Get());

    FSalQuery ExactBlueprintGraph =
        Query(TEXT("graph"), TEXT("blueprint"));
    ExactBlueprintGraph.Operation->SetStringField(
        TEXT("id"),
        GraphId);
    ExactBlueprintGraph.With.Add(TEXT("schema"));
    const TSharedPtr<FJsonObject> ExactGraph =
        FSalBlueprintInterface::Query(
            ExactBlueprintGraph,
            BlueprintScope);
    TestFalse(
        TEXT("Blueprint exact Graph Query succeeds"),
        HasError(ExactGraph));
    const TSharedPtr<FJsonObject> ExactGraphArgs =
        CallArgsWithId(ExactGraph, TEXT("graph"), GraphId);
    FString ExactGraphName;
    TestTrue(
        TEXT("Blueprint exact Graph Query preserves native name"),
        ExactGraphArgs.IsValid()
            && ExactGraphArgs->TryGetStringField(
                TEXT("name"),
                ExactGraphName)
            && ExactGraphName == Fixture.Graph->GetName());
    TestTrue(
        TEXT("Blueprint exact Graph Query exposes schema"),
        HasCommentContaining(
            ExactGraph,
            TEXT("fields:\n  id: FGuid")));

    const FSalResolvedTarget GraphScope =
        GraphTarget(Fixture.Blueprint, Fixture.Graph);
    const TSharedPtr<FJsonObject> GraphSummary =
        FSalGraphInterface::Query(
            Query(TEXT("summary"), TEXT("graph")),
            GraphScope);
    TestFalse(
        TEXT("Graph summary Query succeeds"),
        HasError(GraphSummary));
    TestNotNull(
        TEXT("Graph summary Query is scoped by exact Graph id"),
        CallArgsWithId(
            GraphSummary,
            TEXT("graph"),
            GraphId).Get());
    TestTrue(
        TEXT("Graph summary reports native topology"),
        HasCommentContaining(
            GraphSummary,
            TEXT("summary:")));

    FSalQuery Nodes = Query(TEXT("nodes"), TEXT("graph"));
    Nodes.With.Add(TEXT("layout"));
    const TSharedPtr<FJsonObject> NodeCollection =
        FSalGraphInterface::Query(Nodes, GraphScope);
    TestFalse(
        TEXT("Graph nodes Query succeeds"),
        HasError(NodeCollection));
    FIntPoint CollectionLayout;
    TestTrue(
        TEXT("Graph nodes Query returns the authored Node layout"),
        ReadLayout(
            CallArgsWithId(
                NodeCollection,
                TEXT("node"),
                NodeId),
            CollectionLayout)
            && CollectionLayout
                == FIntPoint(
                    Fixture.Node->NodePosX,
                    Fixture.Node->NodePosY));

    FSalQuery ExactNode = Query(TEXT("node"), TEXT("graph"));
    ExactNode.Operation->SetStringField(TEXT("id"), NodeId);
    ExactNode.With = {TEXT("layout"), TEXT("schema")};
    const TSharedPtr<FJsonObject> ExactNodeResult =
        FSalGraphInterface::Query(ExactNode, GraphScope);
    TestFalse(
        TEXT("Graph exact Node Query succeeds"),
        HasError(ExactNodeResult));
    TestNotNull(
        TEXT("Graph exact Node Query returns the stable Node"),
        CallArgsWithId(
            ExactNodeResult,
            TEXT("node"),
            NodeId).Get());
    TestEqual(
        TEXT("Graph exact Node Query returns every native Pin"),
        CallArgs(ExactNodeResult, TEXT("pin")).Num(),
        Fixture.Node->Pins.Num());
    TestTrue(
        TEXT("Graph exact Node Query exposes schema"),
        HasCommentContaining(
            ExactNodeResult,
            TEXT("schema:")));

    FSalQuery ExactPin = Query(TEXT("pin"), TEXT("graph"));
    ExactPin.Operation->SetStringField(TEXT("id"), PinId);
    ExactPin.With = {TEXT("layout"), TEXT("schema")};
    const TSharedPtr<FJsonObject> ExactPinResult =
        FSalGraphInterface::Query(ExactPin, GraphScope);
    TestFalse(
        TEXT("Graph exact Pin Query succeeds"),
        HasError(ExactPinResult));
    TestNotNull(
        TEXT("Graph exact Pin Query returns the stable Pin"),
        CallArgsWithId(
            ExactPinResult,
            TEXT("pin"),
            PinId).Get());
    TestEqual(
        TEXT("Graph exact Pin Query remains exact"),
        CallArgs(ExactPinResult, TEXT("pin")).Num(),
        1);
    TestNotNull(
        TEXT("Graph exact Pin Query returns its owning Node"),
        CallArgsWithId(
            ExactPinResult,
            TEXT("node"),
            NodeId).Get());

    Transactions.Restore();
    FString CleanupError;
    const bool bCleaned = Fixture.Cleanup(CleanupError);
    TestTrue(
        *FString::Printf(
            TEXT("Query fixture unloads: %s"),
            *CleanupError),
        bCleaned);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FSalGraphLiveMoveReleaseBlockerTest,
    "Loomle.Sal.ReleaseBlocker.Graph.LiveMoveReadbackUndo",
    EAutomationTestFlags::EditorContext
        | EAutomationTestFlags::EngineFilter)

bool FSalGraphLiveMoveReleaseBlockerTest::RunTest(
    const FString& Parameters)
{
    if (!RequireEditor(*this, TEXT("Graph live mutation test")))
    {
        return false;
    }

    Loomle::Tests::FScopedIsolatedTransactor Transactions;
    if (!TestTrue(
            TEXT("Mutation fixture owns an isolated transaction buffer"),
            Transactions.Initialize()))
    {
        return false;
    }
    FBlueprintGraphReleaseFixture Fixture;
    if (!TestTrue(
            TEXT("Real Blueprint mutation fixture is valid"),
            Fixture.IsValid()))
    {
        Transactions.Restore();
        return false;
    }

    const FSalResolvedTarget GraphScope =
        GraphTarget(Fixture.Blueprint, Fixture.Graph);
    const FString NodeId = GuidText(Fixture.Node->NodeGuid);
    const FIntPoint Original(
        Fixture.Node->NodePosX,
        Fixture.Node->NodePosY);
    const FIntPoint Delta(256, 128);
    const FIntPoint Expected = Original + Delta;

    const TSharedPtr<FJsonObject> Applied =
        FSalGraphInterface::Patch(
            MoveNodePatch(Fixture.Node, Delta),
            GraphScope);
    TestTrue(
        TEXT("Graph live move validates"),
        ResultBool(Applied, TEXT("valid")));
    TestTrue(
        TEXT("Graph live move applies to the source Blueprint"),
        ResultBool(Applied, TEXT("applied")));
    TestEqual(
        TEXT("Native source Node X changes"),
        Fixture.Node->NodePosX,
        Expected.X);
    TestEqual(
        TEXT("Native source Node Y changes"),
        Fixture.Node->NodePosY,
        Expected.Y);

    FSalQuery ExactNode = Query(TEXT("node"), TEXT("graph"));
    ExactNode.Operation->SetStringField(TEXT("id"), NodeId);
    ExactNode.With.Add(TEXT("layout"));
    const TSharedPtr<FJsonObject> AppliedReadback =
        FSalGraphInterface::Query(ExactNode, GraphScope);
    FIntPoint AppliedLayout;
    TestTrue(
        TEXT("SAL exact Node readback observes the live move"),
        !HasError(AppliedReadback)
            && ReadLayout(
                CallArgsWithId(
                    AppliedReadback,
                    TEXT("node"),
                    NodeId),
                AppliedLayout)
            && AppliedLayout == Expected);

    const bool bUndid = GEditor->UndoTransaction(false);
    TestTrue(
        TEXT("Graph live move is one native Undo step"),
        bUndid);
    TestEqual(
        TEXT("Native Undo restores source Node X"),
        Fixture.Node->NodePosX,
        Original.X);
    TestEqual(
        TEXT("Native Undo restores source Node Y"),
        Fixture.Node->NodePosY,
        Original.Y);

    const TSharedPtr<FJsonObject> UndoReadback =
        FSalGraphInterface::Query(ExactNode, GraphScope);
    FIntPoint UndoLayout;
    TestTrue(
        TEXT("SAL exact Node readback observes native Undo"),
        !HasError(UndoReadback)
            && ReadLayout(
                CallArgsWithId(
                    UndoReadback,
                    TEXT("node"),
                    NodeId),
                UndoLayout)
            && UndoLayout == Original);

    Transactions.Restore();
    FString CleanupError;
    const bool bCleaned = Fixture.Cleanup(CleanupError);
    TestTrue(
        *FString::Printf(
            TEXT("Mutation fixture unloads: %s"),
            *CleanupError),
        bCleaned);
    return true;
}

#endif
