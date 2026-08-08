// Copyright 2026 Loomle contributors.

#if WITH_DEV_AUTOMATION_TESTS

#include "Sal/Blueprint/SalBlueprintInterface.h"
#include "Sal/Graph/SalGraphInterface.h"
#include "LoomleTestObjectIteration.h"
#include "SalTestObjectModel.h"
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

bool HasDiagnosticCode(
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
            && (*Diagnostic)->TryGetStringField(TEXT("code"), Code)
            && Code == ExpectedCode)
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
        const TSharedPtr<FJsonObject>* Fields = nullptr;
        if (StatementValue.IsValid()
            && StatementValue->TryGetObject(Statement)
            && Statement != nullptr
            && (*Statement)->TryGetObjectField(TEXT("value"), Call)
            && Call != nullptr
            && Loomle::Tests::Sal::TryReadObjectExpr(
                *Call,
                Callee,
                Fields))
        {
            Calls.Add(*Fields);
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
        const TSharedPtr<FJsonObject>* Fields = nullptr;
        const TArray<TSharedPtr<FJsonValue>>* Path = nullptr;
        FString TargetKind;
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
            && Loomle::Tests::Sal::TryReadObjectExpr(
                *Call,
                Callee,
                Fields))
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

TSharedPtr<FJsonObject> ObjectField(
    const TSharedPtr<FJsonObject>& Object,
    const TCHAR* Field)
{
    const TSharedPtr<FJsonObject>* Value = nullptr;
    return Object.IsValid()
        && Object->TryGetObjectField(Field, Value)
        && Value != nullptr
        ? *Value
        : nullptr;
}

TArray<TSharedPtr<FJsonObject>> ObjectArrayField(
    const TSharedPtr<FJsonObject>& Object,
    const TCHAR* Field)
{
    TArray<TSharedPtr<FJsonObject>> Values;
    const TArray<TSharedPtr<FJsonValue>>* Items = nullptr;
    if (!Object.IsValid()
        || !Object->TryGetArrayField(Field, Items)
        || Items == nullptr)
    {
        return Values;
    }
    for (const TSharedPtr<FJsonValue>& Item : *Items)
    {
        const TSharedPtr<FJsonObject>* ItemObject = nullptr;
        if (Item.IsValid()
            && Item->TryGetObject(ItemObject)
            && ItemObject != nullptr)
        {
            Values.Add(*ItemObject);
        }
    }
    return Values;
}

bool ReadPointField(
    const TSharedPtr<FJsonObject>& Object,
    const TCHAR* Field,
    FIntPoint& OutPoint)
{
    const TArray<TSharedPtr<FJsonValue>>* Point = nullptr;
    double X = 0.0;
    double Y = 0.0;
    if (!Object.IsValid()
        || !Object->TryGetArrayField(Field, Point)
        || Point == nullptr
        || Point->Num() != 2
        || !(*Point)[0].IsValid()
        || !(*Point)[1].IsValid()
        || !(*Point)[0]->TryGetNumber(X)
        || !(*Point)[1]->TryGetNumber(Y))
    {
        return false;
    }
    OutPoint = FIntPoint(
        static_cast<int32>(X),
        static_cast<int32>(Y));
    return true;
}

bool ReadNestedAt(
    const TSharedPtr<FJsonObject>& Object,
    const TCHAR* Field,
    FIntPoint& OutPoint)
{
    return ReadPointField(
        ObjectField(Object, Field),
        TEXT("at"),
        OutPoint);
}

bool MatchesMovePlanOperation(
    const TSharedPtr<FJsonObject>& Operation,
    const int32 ExpectedIndex,
    const FString& ExpectedRef,
    const FIntPoint ExpectedTo,
    const FIntPoint ExpectedBefore,
    const FIntPoint ExpectedAfter,
    const bool bExpectedChanged)
{
    double Index = -1.0;
    FString Kind;
    FString Ref;
    bool bChanged = !bExpectedChanged;
    FIntPoint To;
    FIntPoint Before;
    FIntPoint After;
    return Operation.IsValid()
        && Operation->TryGetNumberField(TEXT("index"), Index)
        && Index == ExpectedIndex
        && Operation->TryGetStringField(TEXT("operation"), Kind)
        && Kind == TEXT("move")
        && Operation->TryGetStringField(TEXT("ref"), Ref)
        && Ref == ExpectedRef
        && ReadPointField(Operation, TEXT("to"), To)
        && To == ExpectedTo
        && ReadNestedAt(Operation, TEXT("before"), Before)
        && Before == ExpectedBefore
        && ReadNestedAt(Operation, TEXT("after"), After)
        && After == ExpectedAfter
        && Operation->TryGetBoolField(TEXT("changed"), bChanged)
        && bChanged == bExpectedChanged;
}

bool MatchesStableNodeTarget(
    const TSharedPtr<FJsonObject>& Target,
    const FString& ExpectedId)
{
    FString Kind;
    FString SemanticTag;
    const TArray<TSharedPtr<FJsonValue>>* IdentityPath = nullptr;
    FString Id;
    return Target.IsValid()
        && Target->TryGetStringField(TEXT("kind"), Kind)
        && Kind == TEXT("stable_ref")
        && Target->TryGetStringField(
            TEXT("semanticTag"),
            SemanticTag)
        && SemanticTag == TEXT("node")
        && Target->TryGetArrayField(
            TEXT("identityPath"),
            IdentityPath)
        && IdentityPath != nullptr
        && IdentityPath->Num() == 1
        && (*IdentityPath)[0].IsValid()
        && (*IdentityPath)[0]->TryGetString(Id)
        && Id == ExpectedId;
}

bool MatchesMoveDiffChange(
    const TSharedPtr<FJsonObject>& Change,
    const int32 ExpectedIndex,
    const FString& ExpectedNodeId,
    const FIntPoint ExpectedBefore,
    const FIntPoint ExpectedAfter)
{
    double Index = -1.0;
    FString Kind;
    FIntPoint Before;
    FIntPoint After;
    return Change.IsValid()
        && Change->TryGetNumberField(TEXT("index"), Index)
        && Index == ExpectedIndex
        && Change->TryGetStringField(TEXT("kind"), Kind)
        && Kind == TEXT("move")
        && MatchesStableNodeTarget(
            ObjectField(Change, TEXT("target")),
            ExpectedNodeId)
        && ReadNestedAt(Change, TEXT("before"), Before)
        && Before == ExpectedBefore
        && ReadNestedAt(Change, TEXT("after"), After)
        && After == ExpectedAfter;
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

TSharedRef<FJsonValue> MoveNodeToStatement(
    const UEdGraphNode* Node,
    const double X,
    const double Y)
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
        TEXT("to"),
        {
            MakeShared<FJsonValueNumber>(X),
            MakeShared<FJsonValueNumber>(Y)
        });
    return MakeShared<FJsonValueObject>(Move);
}

FSalPatch MoveNodeToPatch(
    const UEdGraphNode* Node,
    const FIntPoint Position)
{
    FSalPatch Patch;
    Patch.Alias = TEXT("graph");
    Patch.bDryRun = false;
    Patch.Statements = {
        MoveNodeToStatement(
            Node,
            Position.X,
            Position.Y)};
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
        Loomle::Tests::IncludeNestedObjects);
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
    TestTrue(
        TEXT("Graph exact Node schema advertises absolute move"),
        HasCommentContaining(
            ExactNodeResult,
            TEXT("move to (x, y)")));
    TestFalse(
        TEXT("Graph exact Node schema does not advertise relative move"),
        HasCommentContaining(
            ExactNodeResult,
            TEXT("move by")));

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
    FSalGraphMoveSemanticsReleaseBlockerTest,
    "Loomle.Sal.ReleaseBlocker.Graph.MovePlanDiffNoOpPrecision",
    EAutomationTestFlags::EditorContext
        | EAutomationTestFlags::EngineFilter)

bool FSalGraphMoveSemanticsReleaseBlockerTest::RunTest(
    const FString& Parameters)
{
    if (!RequireEditor(*this, TEXT("Graph move semantics test")))
    {
        return false;
    }

    Loomle::Tests::FScopedIsolatedTransactor Transactions;
    if (!TestTrue(
            TEXT("Move fixture owns an isolated transaction buffer"),
            Transactions.Initialize()))
    {
        return false;
    }
    FBlueprintGraphReleaseFixture Fixture;
    if (!TestTrue(
            TEXT("Real Blueprint move fixture is valid"),
            Fixture.IsValid()))
    {
        Transactions.Restore();
        return false;
    }

    const FSalResolvedTarget GraphScope =
        GraphTarget(Fixture.Blueprint, Fixture.Graph);
    const FString NodeId = GuidText(Fixture.Node->NodeGuid);
    const FString NodeRef = TEXT("@") + NodeId;
    const FIntPoint Original(
        Fixture.Node->NodePosX,
        Fixture.Node->NodePosY);
    const bool bPackageDirtyBefore =
        Fixture.Package->IsDirty();
    const FIntPoint First(512, 256);
    const FIntPoint Second(768, 384);

    FSalPatch OrderedMoves;
    OrderedMoves.Alias = TEXT("graph");
    OrderedMoves.bDryRun = true;
    OrderedMoves.Statements = {
        MoveNodeToStatement(Fixture.Node, First.X, First.Y),
        MoveNodeToStatement(Fixture.Node, Second.X, Second.Y)};
    const TSharedPtr<FJsonObject> OrderedResult =
        FSalGraphInterface::Patch(
            OrderedMoves,
            GraphScope);
    TestTrue(
        TEXT("Repeated absolute moves validate in dry run"),
        ResultBool(OrderedResult, TEXT("valid")));
    TestTrue(
        TEXT("Repeated absolute moves report dry-run mode"),
        ResultBool(OrderedResult, TEXT("dryRun")));
    TestFalse(
        TEXT("Repeated absolute move dry run does not apply"),
        ResultBool(OrderedResult, TEXT("applied"), true));
    TestTrue(
        TEXT("Repeated absolute move dry run preserves source layout"),
        Fixture.Node->NodePosX == Original.X
            && Fixture.Node->NodePosY == Original.Y);

    const TSharedPtr<FJsonObject> OrderedPlan =
        ObjectField(OrderedResult, TEXT("planned"));
    double PlannedChangeCount = -1.0;
    TestTrue(
        TEXT("Move plan reports both changed operations"),
        OrderedPlan.IsValid()
            && OrderedPlan->TryGetNumberField(
                TEXT("changedOperationCount"),
                PlannedChangeCount)
            && PlannedChangeCount == 2.0);
    const TArray<TSharedPtr<FJsonObject>> PlannedOperations =
        ObjectArrayField(
            OrderedPlan,
            TEXT("operations"));
    TestEqual(
        TEXT("Move plan preserves repeated statement count"),
        PlannedOperations.Num(),
        2);
    if (PlannedOperations.Num() == 2)
    {
        TestTrue(
            TEXT("First move plan records original-to-first transition"),
            MatchesMovePlanOperation(
                PlannedOperations[0],
                0,
                NodeRef,
                First,
                Original,
                First,
                true));
        TestTrue(
            TEXT("Second move plan observes the first planned position"),
            MatchesMovePlanOperation(
                PlannedOperations[1],
                1,
                NodeRef,
                Second,
                First,
                Second,
                true));
    }

    const TSharedPtr<FJsonObject> OrderedDiff =
        ObjectField(OrderedResult, TEXT("diff"));
    double DiffChangeCount = -1.0;
    FString DiffScope;
    TestTrue(
        TEXT("Move-only dry-run diff is explicitly Graph-scoped"),
        OrderedDiff.IsValid()
            && OrderedDiff->TryGetNumberField(
                TEXT("changedOperations"),
                DiffChangeCount)
            && DiffChangeCount == 2.0
            && OrderedDiff->TryGetStringField(
                TEXT("scope"),
                DiffScope)
            && DiffScope == TEXT("graph"));
    const TArray<TSharedPtr<FJsonObject>> DiffChanges =
        ObjectArrayField(
            OrderedDiff,
            TEXT("changes"));
    TestEqual(
        TEXT("Move diff preserves repeated change count"),
        DiffChanges.Num(),
        2);
    if (DiffChanges.Num() == 2)
    {
        TestTrue(
            TEXT("First move diff records original-to-first transition"),
            MatchesMoveDiffChange(
                DiffChanges[0],
                0,
                NodeId,
                Original,
                First));
        TestTrue(
            TEXT("Second move diff records first-to-second transition"),
            MatchesMoveDiffChange(
                DiffChanges[1],
                1,
                NodeId,
                First,
                Second));
    }

    const TArray<double> AcceptedCoordinates = {
        -16777216.0,
        16777216.0,
        16777218.0,
        static_cast<double>(MIN_int32)};
    for (const double X : AcceptedCoordinates)
    {
        FSalPatch Boundary;
        Boundary.Alias = TEXT("graph");
        Boundary.bDryRun = true;
        Boundary.Statements = {
            MoveNodeToStatement(Fixture.Node, X, 0.0)};
        const TSharedPtr<FJsonObject> Result =
            FSalGraphInterface::Patch(
                Boundary,
                GraphScope);
        TestTrue(
            *FString::Printf(
                TEXT("Exactly representable coordinate %s validates"),
                *FString::SanitizeFloat(X)),
            ResultBool(Result, TEXT("valid")));
        TestTrue(
            *FString::Printf(
                TEXT("Accepted coordinate %s preserves live layout"),
                *FString::SanitizeFloat(X)),
            Fixture.Node->NodePosX == Original.X
                && Fixture.Node->NodePosY == Original.Y);
    }

    const TArray<double> RejectedCoordinates = {
        -16777217.0,
        16777217.0,
        static_cast<double>(MAX_int32),
        320.5,
        2147483648.0};
    for (const double X : RejectedCoordinates)
    {
        FSalPatch Boundary;
        Boundary.Alias = TEXT("graph");
        Boundary.bDryRun = true;
        Boundary.Statements = {
            MoveNodeToStatement(Fixture.Node, X, 0.0)};
        const TSharedPtr<FJsonObject> Result =
            FSalGraphInterface::Patch(
                Boundary,
                GraphScope);
        TestFalse(
            *FString::Printf(
                TEXT("Inexact coordinate %s does not validate"),
                *FString::SanitizeFloat(X)),
            ResultBool(Result, TEXT("valid"), true));
        TestTrue(
            *FString::Printf(
                TEXT("Inexact coordinate %s reports layout validation"),
                *FString::SanitizeFloat(X)),
            HasDiagnosticCode(
                Result,
                TEXT("validation.layout_invalid")));
        TestTrue(
            *FString::Printf(
                TEXT("Rejected coordinate %s preserves live layout"),
                *FString::SanitizeFloat(X)),
            Fixture.Node->NodePosX == Original.X
                && Fixture.Node->NodePosY == Original.Y);
    }

    FSalPatch NoOp =
        MoveNodeToPatch(Fixture.Node, Original);
    const TSharedPtr<FJsonObject> NoOpResult =
        FSalGraphInterface::Patch(
            NoOp,
            GraphScope);
    TestTrue(
        TEXT("Live move to the stored position validates"),
        ResultBool(NoOpResult, TEXT("valid")));
    TestFalse(
        TEXT("Live move to the stored position is not applied"),
        ResultBool(NoOpResult, TEXT("applied"), true));
    TestFalse(
        TEXT("Live no-op result is not a dry run"),
        ResultBool(NoOpResult, TEXT("dryRun"), true));

    const TArray<TSharedPtr<FJsonObject>> NoOpOperations =
        ObjectArrayField(
            ObjectField(NoOpResult, TEXT("planned")),
            TEXT("operations"));
    TestTrue(
        TEXT("No-op plan records unchanged before and after layout"),
        NoOpOperations.Num() == 1
            && MatchesMovePlanOperation(
                NoOpOperations[0],
                0,
                NodeRef,
                Original,
                Original,
                Original,
                false));
    const TSharedPtr<FJsonObject> NoOpDiff =
        ObjectField(NoOpResult, TEXT("diff"));
    double NoOpChangeCount = -1.0;
    FString NoOpDiffScope;
    TestTrue(
        TEXT("No-op diff is empty and Graph-scoped"),
        NoOpDiff.IsValid()
            && NoOpDiff->TryGetNumberField(
                TEXT("changedOperations"),
                NoOpChangeCount)
            && NoOpChangeCount == 0.0
            && NoOpDiff->TryGetStringField(
                TEXT("scope"),
                NoOpDiffScope)
            && NoOpDiffScope == TEXT("graph")
            && ObjectArrayField(
                NoOpDiff,
                TEXT("changes")).IsEmpty());
    TestTrue(
        TEXT("No-op move preserves native source state"),
        Fixture.Node->NodePosX == Original.X
            && Fixture.Node->NodePosY == Original.Y
            && Fixture.Package->IsDirty() == bPackageDirtyBefore);
    TestFalse(
        TEXT("No-op move does not create an Undo record"),
        GEditor->UndoTransaction(false));

    const FIntPoint MismatchTarget =
        Original + FIntPoint(384, 192);
    const int32 QueueLengthBeforeMismatch =
        GEditor->Trans->GetQueueLength();
    const int32 UndoCountBeforeMismatch =
        GEditor->Trans->GetUndoCount();
    const bool bDirtyBeforeMismatch =
        Fixture.Package->IsDirty();
    FSalGraphInterface::SetMoveAppliedHookForTesting(
        [LiveNode = Fixture.Node](UEdGraphNode* AppliedNode)
        {
            if (AppliedNode == LiveNode)
            {
                AppliedNode->Modify();
                ++AppliedNode->NodePosX;
            }
        });
    const TSharedPtr<FJsonObject> MismatchResult =
        FSalGraphInterface::Patch(
            MoveNodeToPatch(
                Fixture.Node,
                MismatchTarget),
            GraphScope);
    FSalGraphInterface::SetMoveAppliedHookForTesting({});

    TestFalse(
        TEXT("Live move readback mismatch does not validate"),
        ResultBool(MismatchResult, TEXT("valid"), true));
    TestFalse(
        TEXT("Live move readback mismatch does not apply"),
        ResultBool(MismatchResult, TEXT("applied"), true));
    TestTrue(
        TEXT("Live move readback mismatch is an execution error"),
        ResultBool(MismatchResult, TEXT("isError")));
    TestTrue(
        TEXT("Live move readback mismatch reports the exact diagnostic"),
        HasDiagnosticCode(
            MismatchResult,
            TEXT("validation.layout_apply_failed")));
    const TArray<TSharedPtr<FJsonObject>> MismatchOperations =
        ObjectArrayField(
            ObjectField(MismatchResult, TEXT("planned")),
            TEXT("operations"));
    TestTrue(
        TEXT("Live move readback mismatch retains the attempted plan"),
        MismatchOperations.Num() == 1
            && MatchesMovePlanOperation(
                MismatchOperations[0],
                0,
                NodeRef,
                MismatchTarget,
                Original,
                MismatchTarget,
                true));
    TestTrue(
        TEXT("Live move readback mismatch omits partial diff"),
        MismatchResult.IsValid()
            && !MismatchResult->HasField(TEXT("diff")));
    TestTrue(
        TEXT("Live move readback mismatch omits partial object readback"),
        MismatchResult.IsValid()
            && !MismatchResult->HasField(TEXT("object")));
    TestTrue(
        TEXT("Live move readback mismatch restores native layout and dirty state"),
        Fixture.Node->NodePosX == Original.X
            && Fixture.Node->NodePosY == Original.Y
            && Fixture.Package->IsDirty()
                == bDirtyBeforeMismatch);
    TestTrue(
        TEXT("Live move readback mismatch leaves no Undo record"),
        GEditor->Trans->GetQueueLength()
                == QueueLengthBeforeMismatch
            && GEditor->Trans->GetUndoCount()
                == UndoCountBeforeMismatch);

    Transactions.Restore();
    FString CleanupError;
    const bool bCleaned = Fixture.Cleanup(CleanupError);
    TestTrue(
        *FString::Printf(
            TEXT("Move semantics fixture unloads: %s"),
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
            MoveNodeToPatch(Fixture.Node, Expected),
            GraphScope);
    TestTrue(
        TEXT("Graph live move validates"),
        ResultBool(Applied, TEXT("valid")));
    TestTrue(
        TEXT("Graph live move applies to the source Blueprint"),
        ResultBool(Applied, TEXT("applied")));
    const TArray<TSharedPtr<FJsonObject>> AppliedOperations =
        ObjectArrayField(
            ObjectField(Applied, TEXT("planned")),
            TEXT("operations"));
    TestTrue(
        TEXT("Graph live move returns its verified absolute plan"),
        AppliedOperations.Num() == 1
            && MatchesMovePlanOperation(
                AppliedOperations[0],
                0,
                TEXT("@") + NodeId,
                Expected,
                Original,
                Expected,
                true));
    const TSharedPtr<FJsonObject> AppliedDiff =
        ObjectField(Applied, TEXT("diff"));
    double AppliedChangeCount = -1.0;
    const TArray<TSharedPtr<FJsonObject>> AppliedChanges =
        ObjectArrayField(
            AppliedDiff,
            TEXT("changes"));
    TestTrue(
        TEXT("Graph live move returns its verified rich diff"),
        AppliedDiff.IsValid()
            && AppliedDiff->TryGetNumberField(
                TEXT("changedOperations"),
                AppliedChangeCount)
            && AppliedChangeCount == 1.0
            && AppliedChanges.Num() == 1
            && MatchesMoveDiffChange(
                AppliedChanges[0],
                0,
                NodeId,
                Original,
                Expected));
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
