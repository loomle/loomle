// Copyright 2026 Loomle contributors.

#if WITH_DEV_AUTOMATION_TESTS

#include "Sal/SalModel.h"
#include "Sal/StateTree/SalStateTreeInterface.h"
#include "Sal/StateTree/SalStateTreeSchema.h"
#include "SalStateTreeTestSchema.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "HAL/IConsoleManager.h"
#include "Misc/AutomationTest.h"
#include "PropertyBindingPath.h"
#include "StateTree.h"
#include "StateTreeDelegate.h"
#include "StateTreeEditingSubsystem.h"
#include "StateTreeEditorData.h"
#include "StateTreeEditorNode.h"
#include "StateTreeState.h"
#include "StateTreeTasksStatus.h"
#include "StructUtils/PropertyBag.h"
#include "UObject/Package.h"
#include "UObject/UObjectGlobals.h"

namespace LoomleStateTreeDiscoveryTests
{
using namespace Loomle::Sal;

struct FDiscoveryStateTree
{
    UStateTree* Asset = nullptr;
    UStateTreeEditorData* EditorData = nullptr;
    UStateTreeState* Root = nullptr;
    UStateTreeState* First = nullptr;
    UStateTreeState* Second = nullptr;
};

FString GuidText(const FGuid& Guid)
{
    return Guid.ToString(EGuidFormats::DigitsWithHyphensLower);
}

FString ParameterId(const FGuid& ContainerId, const FGuid& PropertyId)
{
    return GuidText(ContainerId) + TEXT("/") + GuidText(PropertyId);
}

UPackage* MakeDiscoveryPackage()
{
    return CreatePackage(*FString::Printf(
        TEXT("/LoomleTests/StateTreeDiscovery_%s"),
        *FGuid::NewGuid().ToString(EGuidFormats::Digits)));
}

FDiscoveryStateTree MakeDiscoveryStateTree()
{
    FDiscoveryStateTree Result;
    Result.Asset = NewObject<UStateTree>(MakeDiscoveryPackage(), NAME_None, RF_Transient);
    Result.EditorData = NewObject<UStateTreeEditorData>(Result.Asset, NAME_None, RF_Transient);
    Result.Asset->EditorData = Result.EditorData;

    Result.Root = &Result.EditorData->AddSubTree(FName(TEXT("Root")));
    Result.Root->ID = FGuid(0x71000001, 0x71000002, 0x71000003, 0x71000004);
    Result.First = &Result.Root->AddChildState(FName(TEXT("First")));
    Result.First->ID = FGuid(0x72000001, 0x72000002, 0x72000003, 0x72000004);
    Result.Second = &Result.Root->AddChildState(FName(TEXT("Second")));
    Result.Second->ID = FGuid(0x73000001, 0x73000002, 0x73000003, 0x73000004);
    return Result;
}

FSalResolvedTarget ResolvedTarget(const FDiscoveryStateTree& Tree)
{
    FSalResolvedTarget Target;
    Target.Kind = ESalTargetKind::Asset;
    Target.Alias = TEXT("tree");
    Target.AssetPath = Tree.Asset->GetPathName();
    Target.Object = Tree.Asset;
    Target.Package = Tree.Asset->GetOutermost();
    Target.Interfaces = {FName(TEXT("asset")), FName(TEXT("state_tree"))};
    return Target;
}

FSalQuery Query(const FString& Kind)
{
    FSalQuery Result;
    Result.Alias = TEXT("tree");
    Result.Operation = MakeShared<FJsonObject>();
    Result.Operation->SetStringField(TEXT("kind"), Kind);
    return Result;
}

TSharedPtr<FJsonObject> StableRef(const FString& Kind, const FString& Id)
{
    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetStringField(TEXT("kind"), Kind);
    Result->SetStringField(TEXT("id"), Id);
    return Result;
}

TSharedPtr<FJsonObject> MemberRef(
    const TSharedPtr<FJsonObject>& Owner,
    const TArray<FString>& Path)
{
    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetStringField(TEXT("kind"), TEXT("member"));
    Result->SetObjectField(TEXT("object"), Owner);
    TArray<TSharedPtr<FJsonValue>> Segments;
    for (const FString& Segment : Path)
    {
        Segments.Add(MakeShared<FJsonValueString>(Segment));
    }
    Result->SetArrayField(TEXT("path"), MoveTemp(Segments));
    return Result;
}

TSharedPtr<FJsonObject> LocalRef(const FString& Name)
{
    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetStringField(TEXT("kind"), TEXT("local"));
    Result->SetStringField(TEXT("name"), Name);
    return Result;
}

FSalQuery ReferencesTo(const FString& Kind, const FString& Id)
{
    FSalQuery Result = Query(TEXT("references"));
    Result.Operation->SetObjectField(TEXT("target"), StableRef(Kind, Id));
    return Result;
}

FSalQuery ExactObject(const FString& Kind, const FString& Id, const bool bSchema = false)
{
    FSalQuery Result = Query(Kind);
    Result.Operation->SetStringField(TEXT("id"), Id);
    if (bSchema)
    {
        Result.With.Add(TEXT("schema"));
    }
    return Result;
}

FSalQuery PaletteEntriesTo(
    const FString& StateId,
    const FString& Role,
    const FString& Text = FString())
{
    FSalQuery Result = Query(TEXT("palette_entries"));
    if (!Text.IsEmpty())
    {
        Result.Operation->SetStringField(TEXT("text"), Text);
    }
    Result.Operation->SetObjectField(
        TEXT("to"),
        MemberRef(StableRef(TEXT("state"), StateId), {Role}));
    return Result;
}

FSalQuery ExactPaletteTo(
    const FString& PaletteId,
    const FString& StateId,
    const FString& Role,
    const bool bSchema = false)
{
    FSalQuery Result = Query(TEXT("palette"));
    Result.Operation->SetStringField(TEXT("id"), PaletteId);
    Result.Operation->SetObjectField(
        TEXT("to"),
        MemberRef(StableRef(TEXT("state"), StateId), {Role}));
    if (bSchema)
    {
        Result.With.Add(TEXT("schema"));
    }
    return Result;
}


FSalQuery PaletteEntriesToTargetMember(
    const FString& TargetAlias,
    const FString& Member)
{
    FSalQuery Result = Query(TEXT("palette_entries"));
    Result.Operation->SetObjectField(
        TEXT("to"),
        MemberRef(LocalRef(TargetAlias), {Member}));
    return Result;
}

FSalQuery ExactPaletteToTargetMember(
    const FString& PaletteId,
    const FString& TargetAlias,
    const FString& Member,
    const bool bSchema = false)
{
    FSalQuery Result = Query(TEXT("palette"));
    Result.Operation->SetStringField(TEXT("id"), PaletteId);
    Result.Operation->SetObjectField(
        TEXT("to"),
        MemberRef(LocalRef(TargetAlias), {Member}));
    if (bSchema)
    {
        Result.With.Add(TEXT("schema"));
    }
    return Result;
}

FStateTreeEditorNode& AddBindingTask(
    TArray<FStateTreeEditorNode>& Nodes,
    const FGuid& Id,
    const FName Name)
{
    FStateTreeEditorNode& Node = Nodes.AddDefaulted_GetRef();
    Node.ID = Id;
    Node.Node.InitializeAs<FSalStateTreeBindingTask>();
    Node.Node.GetMutable<FSalStateTreeBindingTask>().Name = Name;
    Node.Instance.InitializeAs<FSalStateTreeBindingTaskInstanceData>();
    return Node;
}

FStateTreeEditorNode& AddContextTask(
    TArray<FStateTreeEditorNode>& Nodes,
    const FGuid& Id,
    const FName Name)
{
    FStateTreeEditorNode& Node = Nodes.AddDefaulted_GetRef();
    Node.ID = Id;
    Node.Node.InitializeAs<FSalStateTreeContextTask>();
    Node.Node.GetMutable<FSalStateTreeContextTask>().Name = Name;
    Node.Instance.InitializeAs<FSalStateTreeContextTaskInstanceData>();
    return Node;
}

FPropertyBagPropertyDesc AddRootIntParameter(
    const FDiscoveryStateTree& Tree,
    const FGuid& Id,
    const FName Name)
{
    FPropertyBagPropertyDesc Desc(Name, EPropertyBagPropertyType::Int32);
    Desc.ID = Id;
    Desc.PropertyFlags = static_cast<uint64>(CPF_Edit | CPF_BlueprintVisible);
    FInstancedPropertyBag& Bag = const_cast<FInstancedPropertyBag&>(
        Tree.EditorData->GetRootParametersPropertyBag());
    Bag.AddProperties({Desc});
    Bag.SetValueInt32(Name, 17);
    return Desc;
}

USalStateTreeTestSchema* AddTestSchema(
    const FDiscoveryStateTree& Tree,
    const FGuid& ContextId,
    const bool bAllowBindingTask = false)
{
    USalStateTreeTestSchema* Schema = NewObject<USalStateTreeTestSchema>(Tree.EditorData);
    Schema->bAllowBindingTask = bAllowBindingTask;
    Schema->ContextData.Add(FStateTreeExternalDataDesc(
        FName(TEXT("ContextObject")),
        UObject::StaticClass(),
        ContextId));
    Tree.EditorData->Schema = Schema;
    return Schema;
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
            && (*Diagnostic)->TryGetStringField(TEXT("severity"), Severity)
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

bool HasAnyDiagnosticCode(
    const TSharedPtr<FJsonObject>& Result,
    const TArray<FString>& ExpectedCodes)
{
    for (const FString& Code : ExpectedCodes)
    {
        if (HasDiagnosticCode(Result, Code))
        {
            return true;
        }
    }
    return false;
}

TArray<TSharedPtr<FJsonObject>> CallArgs(
    const TSharedPtr<FJsonObject>& Result,
    const FString& Callee)
{
    TArray<TSharedPtr<FJsonObject>> Results;
    const TSharedPtr<FJsonObject>* Object = nullptr;
    const TArray<TSharedPtr<FJsonValue>>* Statements = nullptr;
    if (!Result.IsValid()
        || !Result->TryGetObjectField(TEXT("object"), Object)
        || Object == nullptr
        || !(*Object)->TryGetArrayField(TEXT("statements"), Statements)
        || Statements == nullptr)
    {
        return Results;
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
            && (*Call)->TryGetStringField(TEXT("callee"), ActualCallee)
            && ActualCallee == Callee
            && (*Call)->TryGetObjectField(TEXT("args"), Args)
            && Args != nullptr)
        {
            Results.Add(*Args);
        }
    }
    return Results;
}

TArray<FString> CallIds(
    const TSharedPtr<FJsonObject>& Result,
    const FString& Callee)
{
    TArray<FString> Results;
    for (const TSharedPtr<FJsonObject>& Args : CallArgs(Result, Callee))
    {
        FString Id;
        if (Args.IsValid() && Args->TryGetStringField(TEXT("id"), Id))
        {
            Results.Add(Id);
        }
    }
    return Results;
}

FString FirstPaletteId(
    const TSharedPtr<FJsonObject>& Result,
    const FString& Callee)
{
    for (const TSharedPtr<FJsonObject>& Args : CallArgs(Result, Callee))
    {
        FString Id;
        if (Args.IsValid()
            && Args->TryGetStringField(TEXT("palette"), Id)
            && !Id.IsEmpty())
        {
            return Id;
        }
    }
    return FString();
}

FString NextCursor(const TSharedPtr<FJsonObject>& Result)
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

bool HasCommentContaining(
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
            && Text.Contains(Needle, ESearchCase::IgnoreCase))
        {
            return true;
        }
    }
    return false;
}

FString JoinedComments(const TSharedPtr<FJsonObject>& Result)
{
    FString Joined;
    const TSharedPtr<FJsonObject>* Object = nullptr;
    const TArray<TSharedPtr<FJsonValue>>* Statements = nullptr;
    if (!Result.IsValid()
        || !Result->TryGetObjectField(TEXT("object"), Object)
        || Object == nullptr
        || !(*Object)->TryGetArrayField(TEXT("statements"), Statements)
        || Statements == nullptr)
    {
        return Joined;
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
            && (*Statement)->TryGetStringField(TEXT("text"), Text))
        {
            if (!Joined.IsEmpty())
            {
                Joined += TEXT("\n");
            }
            Joined += Text;
        }
    }
    return Joined;
}

bool HasSchemaGuidance(const TSharedPtr<FJsonObject>& Result)
{
    return HasCommentContaining(Result, TEXT("schema"))
        || HasCommentContaining(Result, TEXT("fields:"))
        || HasCommentContaining(Result, TEXT("constraints:"));
}

bool HasCommentLineContainingAll(
    const TSharedPtr<FJsonObject>& Result,
    const TArray<FString>& Needles)
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
        if (!StatementValue.IsValid()
            || !StatementValue->TryGetObject(Statement)
            || Statement == nullptr
            || !(*Statement)->TryGetStringField(TEXT("kind"), Kind)
            || Kind != TEXT("comment")
            || !(*Statement)->TryGetStringField(TEXT("text"), Text))
        {
            continue;
        }
        TArray<FString> Lines;
        Text.ParseIntoArrayLines(Lines, false);
        for (const FString& Line : Lines)
        {
            bool bAll = true;
            for (const FString& Needle : Needles)
            {
                if (!Line.Contains(Needle, ESearchCase::IgnoreCase))
                {
                    bAll = false;
                    break;
                }
            }
            if (bAll)
            {
                return true;
            }
        }
    }
    return false;
}

FSalPatch Patch(const bool bDryRun = false)
{
    FSalPatch Result;
    Result.Alias = TEXT("tree");
    Result.bDryRun = bDryRun;
    return Result;
}

TSharedPtr<FJsonObject> Call(
    const FString& Callee,
    const TSharedPtr<FJsonObject>& Args)
{
    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetStringField(TEXT("kind"), TEXT("call"));
    Result->SetStringField(TEXT("callee"), Callee);
    Result->SetObjectField(TEXT("args"), Args);
    return Result;
}

TSharedPtr<FJsonObject> ConstructorBinding(
    const FString& Alias,
    const FString& Callee,
    const FString& PaletteId)
{
    TSharedPtr<FJsonObject> Args = MakeShared<FJsonObject>();
    Args->SetStringField(TEXT("palette"), PaletteId);
    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetObjectField(TEXT("target"), LocalRef(Alias));
    Result->SetObjectField(TEXT("value"), Call(Callee, Args));
    return Result;
}

TSharedPtr<FJsonObject> Operation(const FString& Kind)
{
    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetStringField(TEXT("kind"), Kind);
    return Result;
}

void AddStatement(FSalPatch& Patch, const TSharedPtr<FJsonObject>& Statement)
{
    Patch.Statements.Add(MakeShared<FJsonValueObject>(Statement));
}

bool ResultBool(
    const TSharedPtr<FJsonObject>& Result,
    const FString& Field,
    const bool Fallback)
{
    bool Value = Fallback;
    return Result.IsValid() && Result->TryGetBoolField(Field, Value)
        ? Value
        : Fallback;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FSalStateTreeLocalReferencesDiscoveryTest,
    "Loomle.Sal.StateTree.Discovery.LocalReferences",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSalStateTreeLocalReferencesDiscoveryTest::RunTest(const FString& Parameters)
{
    const FDiscoveryStateTree Tree = MakeDiscoveryStateTree();
    const FSalResolvedTarget Target = ResolvedTarget(Tree);

    FStateTreeTransition& RootUse = Tree.Root->AddTransition(
        EStateTreeTransitionTrigger::OnStateCompleted,
        EStateTreeTransitionType::GotoState,
        Tree.Second);
    RootUse.ID = FGuid(0x74000001, 0x74000002, 0x74000003, 0x74000004);
    FStateTreeTransition& FirstUse = Tree.First->AddTransition(
        EStateTreeTransitionTrigger::OnStateCompleted,
        EStateTreeTransitionType::GotoState,
        Tree.Second);
    FirstUse.ID = FGuid(0x75000001, 0x75000002, 0x75000003, 0x75000004);

    const FGuid SourceParameterId(0x76000001, 0x76000002, 0x76000003, 0x76000004);
    AddRootIntParameter(Tree, SourceParameterId, TEXT("SourceValue"));
    const FGuid ConsumerId(0x77000001, 0x77000002, 0x77000003, 0x77000004);
    AddBindingTask(Tree.First->Tasks, ConsumerId, TEXT("Consumer"));
    Tree.EditorData->AddPropertyBinding(
        FPropertyBindingPath(Tree.EditorData->GetRootParametersGuid(), TEXT("SourceValue")),
        FPropertyBindingPath(ConsumerId, TEXT("InputValue")));
    Tree.EditorData->AddPropertyBinding(
        FPropertyBindingPath(Tree.EditorData->GetRootParametersGuid(), TEXT("SourceValue")),
        FPropertyBindingPath(ConsumerId, TEXT("SecondaryInputValue")));

    FSalQuery FirstPageQuery = ReferencesTo(TEXT("state"), GuidText(Tree.Second->ID));
    FirstPageQuery.PageLimit = 1;
    const TSharedPtr<FJsonObject> FirstPage = FSalStateTreeInterface::Query(FirstPageQuery, Target);
    TestFalse(TEXT("Local State references succeeds"), HasError(FirstPage));
    const TArray<FString> FirstTransitionIds = CallIds(FirstPage, TEXT("transition"));
    TestEqual(TEXT("Reference page limit counts one ordinary use-site object"), FirstTransitionIds.Num(), 1);
    const FString Cursor = NextCursor(FirstPage);
    TestFalse(TEXT("More local State use sites produce a cursor"), Cursor.IsEmpty());

    FSalQuery SecondPageQuery = FirstPageQuery;
    SecondPageQuery.PageAfter = Cursor;
    const TSharedPtr<FJsonObject> SecondPage = FSalStateTreeInterface::Query(SecondPageQuery, Target);
    TestFalse(TEXT("Second local reference page succeeds"), HasError(SecondPage));
    const TArray<FString> SecondTransitionIds = CallIds(SecondPage, TEXT("transition"));
    TestEqual(TEXT("Second page contains one ordinary Transition use-site"), SecondTransitionIds.Num(), 1);
    TestTrue(TEXT("Final local reference page has no next cursor"), NextCursor(SecondPage).IsEmpty());

    TArray<FString> TransitionIds = FirstTransitionIds;
    TransitionIds.Append(SecondTransitionIds);
    TransitionIds.Sort();
    TArray<FString> ExpectedTransitionIds = {GuidText(RootUse.ID), GuidText(FirstUse.ID)};
    ExpectedTransitionIds.Sort();
    TestTrue(TEXT("Only exact State-link use sites are returned"), TransitionIds == ExpectedTransitionIds);

    FSalQuery ParameterReferences = ReferencesTo(
        TEXT("parameter"),
        ParameterId(Tree.EditorData->GetRootParametersGuid(), SourceParameterId));
    const TSharedPtr<FJsonObject> ParameterResult = FSalStateTreeInterface::Query(ParameterReferences, Target);
    TestFalse(TEXT("Local Parameter Binding references succeeds"), HasError(ParameterResult));
    TestTrue(
        TEXT("A Binding reference returns its ordinary Node use-site"),
        CallIds(ParameterResult, TEXT("node")).Contains(GuidText(ConsumerId)));
    TestEqual(
        TEXT("Several matching members on one authored object still return one use-site"),
        CallIds(ParameterResult, TEXT("node")).Num(),
        1);
    const FString MatchComments = JoinedComments(ParameterResult);
    const int32 PrimaryMatch = MatchComments.Find(TEXT("Instance.InputValue"));
    const int32 SecondaryMatch = MatchComments.Find(TEXT("Instance.SecondaryInputValue"));
    TestTrue(
        TEXT("Reference result identifies the first exact matched member path"),
        PrimaryMatch != INDEX_NONE);
    TestTrue(
        TEXT("Reference result identifies every exact matched member path on the same use-site"),
        SecondaryMatch != INDEX_NONE);
    TestTrue(
        TEXT("Matched member comments retain stable authored Binding order"),
        PrimaryMatch != INDEX_NONE && SecondaryMatch > PrimaryMatch);

    const TSharedPtr<FJsonObject> Unrelated = FSalStateTreeInterface::Query(
        ReferencesTo(TEXT("state"), GuidText(Tree.First->ID)),
        Target);
    TestFalse(TEXT("An exact declaration with no local uses is a successful empty result"), HasError(Unrelated));
    TestEqual(TEXT("Hierarchy ownership is not invented as a State reference"), CallIds(Unrelated, TEXT("transition")).Num(), 0);

    FSalQuery ProjectScope = ReferencesTo(TEXT("state"), GuidText(Tree.Second->ID));
    ProjectScope.Operation->SetStringField(TEXT("scope"), TEXT("project"));
    const TSharedPtr<FJsonObject> ProjectScopeResult = FSalStateTreeInterface::Query(ProjectScope, Target);
    TestTrue(TEXT("StateTree project reference scope fails rather than loading assets"), HasError(ProjectScopeResult));
    TestTrue(
        TEXT("Unsupported StateTree project scope is explicit"),
        HasDiagnosticCode(ProjectScopeResult, TEXT("capability.reference_unavailable")));

    FSalQuery WithSchema = ReferencesTo(TEXT("state"), GuidText(Tree.Second->ID));
    WithSchema.With.Add(TEXT("schema"));
    TestTrue(
        TEXT("References rejects with schema"),
        HasDiagnosticCode(
            FSalStateTreeInterface::Query(WithSchema, Target),
            TEXT("capability.clause_unavailable")));

    FSalQuery WithWhere = ReferencesTo(TEXT("state"), GuidText(Tree.Second->ID));
    WithWhere.Where = MakeShared<FJsonObject>();
    TestTrue(
        TEXT("References rejects where"),
        HasDiagnosticCode(
            FSalStateTreeInterface::Query(WithWhere, Target),
            TEXT("capability.clause_unavailable")));

    FSalQuery InvalidCursor = FirstPageQuery;
    InvalidCursor.PageAfter = TEXT("not-a-state-tree-reference-cursor");
    TestTrue(
        TEXT("Malformed local reference cursor fails closed"),
        HasDiagnosticCode(
            FSalStateTreeInterface::Query(InvalidCursor, Target),
            TEXT("validation.invalid_cursor")));

    FSalQuery MismatchedCursor = ReferencesTo(TEXT("state"), GuidText(Tree.First->ID));
    MismatchedCursor.PageLimit = 1;
    MismatchedCursor.PageAfter = Cursor;
    TestTrue(
        TEXT("A cursor cannot be reused for another exact reference subject"),
        HasDiagnosticCode(
            FSalStateTreeInterface::Query(MismatchedCursor, Target),
            TEXT("validation.invalid_cursor")));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FSalStateTreeExactSchemaDiscoveryTest,
    "Loomle.Sal.StateTree.Discovery.ExactSchema",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSalStateTreeExactSchemaDiscoveryTest::RunTest(const FString& Parameters)
{
    const FDiscoveryStateTree Tree = MakeDiscoveryStateTree();
    const FSalResolvedTarget Target = ResolvedTarget(Tree);
    const FGuid ContextId(0x81000001, 0x81000002, 0x81000003, 0x81000004);
    USalStateTreeTestSchema* Schema = AddTestSchema(Tree, ContextId);
    Schema->ContextData[0].Struct = FSalStateTreeBindingEventPayload::StaticStruct();

    const FGuid ParameterGuid(0x82000001, 0x82000002, 0x82000003, 0x82000004);
    AddRootIntParameter(Tree, ParameterGuid, TEXT("Threshold"));
    const FGuid NodeId(0x83000001, 0x83000002, 0x83000003, 0x83000004);
    FStateTreeEditorNode& SchemaNode = AddBindingTask(
        Tree.First->Tasks,
        NodeId,
        TEXT("Schema Task"));
    SchemaNode.ExecutionRuntimeData.InitializeAs<FSalStateTreeBindingEventPayload>();
    FStateTreeTransition& Transition = Tree.First->AddTransition(
        EStateTreeTransitionTrigger::OnStateCompleted,
        EStateTreeTransitionType::GotoState,
        Tree.Second);
    Transition.ID = FGuid(0x84000001, 0x84000002, 0x84000003, 0x84000004);

    struct FExactCase
    {
        FString Kind;
        FString Id;
        FString Callee;
    };
    const TArray<FExactCase> Cases = {
        {TEXT("state"), GuidText(Tree.First->ID), TEXT("state")},
        {TEXT("node"), GuidText(NodeId), TEXT("node")},
        {TEXT("transition"), GuidText(Transition.ID), TEXT("transition")},
        {
            TEXT("parameter"),
            ParameterId(Tree.EditorData->GetRootParametersGuid(), ParameterGuid),
            TEXT("parameter")
        },
        {TEXT("object"), GuidText(ContextId), TEXT("object")},
    };

    FSalQuery TargetSchema = Query(TEXT("target"));
    TargetSchema.With.Add(TEXT("schema"));
    const TSharedPtr<FJsonObject> TargetResult = FSalStateTreeInterface::Query(TargetSchema, Target);
    TestFalse(TEXT("Exact StateTree target accepts with schema"), HasError(TargetResult));
    TestEqual(TEXT("Target schema remains ordinary Asset Object Text"), CallArgs(TargetResult, TEXT("asset")).Num(), 1);
    TestTrue(TEXT("Target schema is adjacent guidance rather than a schema object"), HasSchemaGuidance(TargetResult));

    for (const FExactCase& Case : Cases)
    {
        const TSharedPtr<FJsonObject> Result = FSalStateTreeInterface::Query(
            ExactObject(Case.Kind, Case.Id, true),
            Target);
        TestFalse(
            *FString::Printf(TEXT("Exact %s accepts with schema"), *Case.Kind),
            HasError(Result));
        TestTrue(
            *FString::Printf(TEXT("Exact %s keeps its ordinary object"), *Case.Kind),
            CallIds(Result, Case.Callee).Contains(Case.Id));
        TestTrue(
            *FString::Printf(TEXT("Exact %s emits adjacent schema guidance"), *Case.Kind),
            HasSchemaGuidance(Result));
    }

    const TSharedPtr<FJsonObject> NodeSchema = FSalStateTreeInterface::Query(
        ExactObject(TEXT("node"), GuidText(NodeId), true),
        Target);
    TestTrue(
        TEXT("Exact Node Input member is a Binding target"),
        HasCommentLineContainingAll(
            NodeSchema,
            {TEXT("Instance.InputValue"), TEXT("binding target")}));
    TestFalse(
        TEXT("Exact Node Input member is not a Binding source"),
        HasCommentLineContainingAll(
            NodeSchema,
            {TEXT("Instance.InputValue"), TEXT("binding source")}));
    TestTrue(
        TEXT("Exact Node Output member is a Binding source"),
        HasCommentLineContainingAll(
            NodeSchema,
            {TEXT("Instance.OutputValue"), TEXT("binding source")}));
    TestFalse(
        TEXT("Exact Node Output member is not a Binding target"),
        HasCommentLineContainingAll(
            NodeSchema,
            {TEXT("Instance.OutputValue"), TEXT("binding target")}));
    TestTrue(
        TEXT("Exact Node schema exposes its authored Execution Runtime Data surface"),
        HasCommentLineContainingAll(
            NodeSchema,
            {TEXT("ExecutionRuntimeData.Value"), TEXT("read-only")}));
    TestFalse(
        TEXT("Execution Runtime Data is not advertised as a Binding source"),
        HasCommentLineContainingAll(
            NodeSchema,
            {TEXT("ExecutionRuntimeData.Value"), TEXT("binding source")}));
    TestFalse(
        TEXT("Execution Runtime Data is not advertised as a Binding target"),
        HasCommentLineContainingAll(
            NodeSchema,
            {TEXT("ExecutionRuntimeData.Value"), TEXT("binding target")}));

    const TSharedPtr<FJsonObject> ContextSchema = FSalStateTreeInterface::Query(
        ExactObject(TEXT("object"), GuidText(ContextId), true),
        Target);
    TestTrue(
        TEXT("Exact Context member is a read-only Binding source"),
        HasCommentLineContainingAll(
            ContextSchema,
            {TEXT("Value:"), TEXT("read-only"), TEXT("binding source")}));
    TestFalse(
        TEXT("Exact Context member is never a Binding target"),
        HasCommentLineContainingAll(
            ContextSchema,
            {TEXT("Value:"), TEXT("binding target")}));

    FSalQuery UnknownDetail = ExactObject(TEXT("state"), GuidText(Tree.First->ID));
    UnknownDetail.With.Add(TEXT("pins"));
    TestTrue(
        TEXT("Exact StateTree object rejects unknown detail"),
        HasDiagnosticCode(
            FSalStateTreeInterface::Query(UnknownDetail, Target),
            TEXT("capability.detail_unavailable")));

    FSalQuery ExactPage = ExactObject(TEXT("state"), GuidText(Tree.First->ID), true);
    ExactPage.PageLimit = 1;
    TestTrue(
        TEXT("Exact StateTree schema rejects pagination"),
        HasDiagnosticCode(
            FSalStateTreeInterface::Query(ExactPage, Target),
            TEXT("capability.clause_unavailable")));

    FSalQuery CollectionSchema = Query(TEXT("states"));
    CollectionSchema.With.Add(TEXT("schema"));
    TestTrue(
        TEXT("Compact State collection does not expand dynamic schema"),
        HasAnyDiagnosticCode(
            FSalStateTreeInterface::Query(CollectionSchema, Target),
            {TEXT("capability.detail_unavailable"), TEXT("capability.clause_unavailable")}));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FSalStateTreeExactSchemaBudgetDiscoveryTest,
    "Loomle.Sal.StateTree.Discovery.ExactSchemaBudget",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSalStateTreeExactSchemaBudgetDiscoveryTest::RunTest(const FString& Parameters)
{
    {
        StateTreeSchema::FExactSchemaTextBuilder TextBudget;
        FString Text;
        FString Error;
        TestTrue(
            TEXT("Exact schema text accepts its exact character budget"),
            TextBudget.Append(FString::ChrN(
                static_cast<int32>(StateTreeSchema::FExactSchemaTextBuilder::MaxCharacters),
                TEXT('x'))));
        TestTrue(
            TEXT("Exact schema text at the character boundary completes"),
            TextBudget.Finish(Text, Error));
        TestEqual(
            TEXT("Exact schema text boundary is preserved without truncation"),
            static_cast<int64>(Text.Len()),
            StateTreeSchema::FExactSchemaTextBuilder::MaxCharacters);

        StateTreeSchema::FExactSchemaTextBuilder Overflow;
        TestFalse(
            TEXT("Exact schema text rejects one character beyond its budget"),
            Overflow.Append(FString::ChrN(
                static_cast<int32>(StateTreeSchema::FExactSchemaTextBuilder::MaxCharacters + 1),
                TEXT('x'))));
        Text = TEXT("must be cleared");
        TestFalse(
            TEXT("An exceeded schema budget fails completion"),
            Overflow.Finish(Text, Error));
        TestTrue(TEXT("An exceeded schema budget exposes no partial text"), Text.IsEmpty());
    }

    FInstancedPropertyBag BudgetBag;
    TArray<FPropertyBagPropertyDesc> Fields;
    Fields.Reserve(StateTreeSchema::FExactSchemaTextBuilder::MaxFields);
    for (int32 Index = 0;
         Index < StateTreeSchema::FExactSchemaTextBuilder::MaxFields;
         ++Index)
    {
        FPropertyBagPropertyDesc Desc(
            FName(*FString::Printf(TEXT("BudgetField_%04d"), Index)),
            EPropertyBagPropertyType::Int32);
        Desc.PropertyFlags = static_cast<uint64>(CPF_Edit | CPF_BlueprintVisible);
        Fields.Add(MoveTemp(Desc));
    }
    TestTrue(
        TEXT("Property Bag fixture reaches the exact schema field boundary"),
        BudgetBag.AddProperties(Fields) == EPropertyBagAlterationResult::Success);

    const FDiscoveryStateTree Tree = MakeDiscoveryStateTree();
    const FSalResolvedTarget Target = ResolvedTarget(Tree);
    const FGuid ContextId(0x84010001, 0x84010002, 0x84010003, 0x84010004);
    USalStateTreeTestSchema* Schema = AddTestSchema(Tree, ContextId);
    Schema->ContextData[0].Struct = BudgetBag.GetPropertyBagStruct();

    const TSharedPtr<FJsonObject> AtBoundary = FSalStateTreeInterface::Query(
        ExactObject(TEXT("object"), GuidText(ContextId), true),
        Target);
    TestFalse(TEXT("Exact schema at the field boundary succeeds"), HasError(AtBoundary));
    TestTrue(
        TEXT("Exact schema at the field boundary includes its final field"),
        HasCommentContaining(AtBoundary, TEXT("BudgetField_2047")));

    FPropertyBagPropertyDesc Extra(
        FName(TEXT("BudgetField_2048")),
        EPropertyBagPropertyType::Int32);
    Extra.PropertyFlags = static_cast<uint64>(CPF_Edit | CPF_BlueprintVisible);
    TestTrue(
        TEXT("Property Bag fixture adds one over-budget field"),
        BudgetBag.AddProperties({Extra}) == EPropertyBagAlterationResult::Success);
    Schema->ContextData[0].Struct = BudgetBag.GetPropertyBagStruct();

    const TSharedPtr<FJsonObject> SchemaOverflow = FSalStateTreeInterface::Query(
        ExactObject(TEXT("object"), GuidText(ContextId), true),
        Target);
    TestTrue(TEXT("Over-budget exact object schema fails"), HasError(SchemaOverflow));
    TestTrue(
        TEXT("Over-budget exact object schema uses the registered result-size diagnostic"),
        HasDiagnosticCode(SchemaOverflow, TEXT("validation.result_too_large")));
    TestFalse(
        TEXT("Over-budget exact object schema returns no partial schema text"),
        HasSchemaGuidance(SchemaOverflow));

    const FGuid NodeId(0x84020001, 0x84020002, 0x84020003, 0x84020004);
    FStateTreeEditorNode& Node = AddBindingTask(Tree.First->Tasks, NodeId, TEXT("Budget Node"));
    Node.ExecutionRuntimeData.InitializeAs(BudgetBag.GetPropertyBagStruct());
    const TSharedPtr<FJsonObject> NodeOverflow = FSalStateTreeInterface::Query(
        ExactObject(TEXT("node"), GuidText(NodeId)),
        Target);
    TestTrue(TEXT("Over-budget exact Node native read fails"), HasError(NodeOverflow));
    TestTrue(
        TEXT("Over-budget exact Node native read uses the registered result-size diagnostic"),
        HasDiagnosticCode(NodeOverflow, TEXT("validation.result_too_large")));
    TestEqual(
        TEXT("Over-budget exact Node native read returns no partial Node object"),
        CallIds(NodeOverflow, TEXT("node")).Num(),
        0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FSalStateTreeNativeCapabilitySchemaDiscoveryTest,
    "Loomle.Sal.StateTree.Discovery.NativeCapabilitySchema",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSalStateTreeNativeCapabilitySchemaDiscoveryTest::RunTest(const FString& Parameters)
{
    const FDiscoveryStateTree Tree = MakeDiscoveryStateTree();
    const FSalResolvedTarget Target = ResolvedTarget(Tree);
    USalStateTreeTestSchema* Schema = AddTestSchema(
        Tree,
        FGuid(0x84100001, 0x84100002, 0x84100003, 0x84100004));

    FStateTreeTransition& Transition = Tree.First->AddTransition(
        EStateTreeTransitionTrigger::OnStateCompleted,
        EStateTreeTransitionType::GotoState,
        Tree.Second);
    Transition.ID = FGuid(0x84200001, 0x84200002, 0x84200003, 0x84200004);

    FSalQuery TargetSchemaQuery = Query(TEXT("target"));
    TargetSchemaQuery.With.Add(TEXT("schema"));
    const TSharedPtr<FJsonObject> WritableTarget = FSalStateTreeInterface::Query(
        TargetSchemaQuery,
        Target);
    TestTrue(
        TEXT("Schema-enabled GlobalTasksCompletion is directly writable"),
        HasCommentLineContainingAll(
            WritableTarget,
            {TEXT("GlobalTasksCompletion:"), TEXT("read/write/reset")}));
    TestFalse(
        TEXT("Target SubTrees remains structural rather than a directly writable field"),
        HasCommentLineContainingAll(
            WritableTarget,
            {TEXT("SubTrees:"), TEXT("read/write")}));
    TestFalse(
        TEXT("Target GlobalTasks remains structural rather than a directly writable field"),
        HasCommentLineContainingAll(
            WritableTarget,
            {TEXT("GlobalTasks:"), TEXT("read/write")}));

    Schema->bAllowTaskCompletionEdits = false;
    const TSharedPtr<FJsonObject> ReadOnlyTarget = FSalStateTreeInterface::Query(
        TargetSchemaQuery,
        Target);
    TestTrue(
        TEXT("Schema-disabled GlobalTasksCompletion becomes read-only"),
        HasCommentLineContainingAll(
            ReadOnlyTarget,
            {TEXT("GlobalTasksCompletion:"), TEXT("read-only")}));
    TestFalse(
        TEXT("Schema-disabled GlobalTasksCompletion is not advertised writable"),
        HasCommentLineContainingAll(
            ReadOnlyTarget,
            {TEXT("GlobalTasksCompletion:"), TEXT("read/write")}));

    const TSharedPtr<FJsonObject> RestrictedState = FSalStateTreeInterface::Query(
        ExactObject(TEXT("state"), GuidText(Tree.First->ID), true),
        Target);
    TestTrue(
        TEXT("Schema-disabled State TasksCompletion becomes read-only"),
        HasCommentLineContainingAll(
            RestrictedState,
            {TEXT("TasksCompletion:"), TEXT("read-only")}));
    TestTrue(
        TEXT("Scheduled-tick-disabled CustomTickRate becomes read-only"),
        HasCommentLineContainingAll(
            RestrictedState,
            {TEXT("CustomTickRate:"), TEXT("read-only")}));
    TestTrue(
        TEXT("Scheduled-tick-disabled toggle becomes read-only"),
        HasCommentLineContainingAll(
            RestrictedState,
            {TEXT("bHasCustomTickRate:"), TEXT("read-only")}));
    const TArray<FString> StateStructuralFields = {
        TEXT("Children:"),
        TEXT("Tasks:"),
        TEXT("Parameters:"),
        TEXT("ID:"),
        TEXT("Parent:")
    };
    for (const FString& StructuralField : StateStructuralFields)
    {
        TestFalse(
            *FString::Printf(TEXT("State structural field %s is not directly writable"), *StructuralField),
            HasCommentLineContainingAll(
                RestrictedState,
                {StructuralField, TEXT("read/write")}));
    }

    Schema->bAllowScheduledTick = true;
    Schema->bRestrictStateTypes = true;
    Schema->bRestrictStateSelection = true;
    const TSharedPtr<FJsonObject> DynamicState = FSalStateTreeInterface::Query(
        ExactObject(TEXT("state"), GuidText(Tree.First->ID), true),
        Target);
    TestTrue(
        TEXT("Scheduled-tick-enabled CustomTickRate becomes writable"),
        HasCommentLineContainingAll(
            DynamicState,
            {TEXT("CustomTickRate:"), TEXT("read/write/reset")}));
    TestTrue(
        TEXT("State schema reports the exact allowed native State type set"),
        HasCommentContaining(DynamicState, TEXT("IsStateTypeAllowed: State")));
    TestTrue(
        TEXT("State schema reports the exact allowed native selection set"),
        HasCommentContaining(
            DynamicState,
            TEXT("IsStateSelectionAllowed: TryEnterState")));

    const TSharedPtr<FJsonObject> TransitionSchema = FSalStateTreeInterface::Query(
        ExactObject(TEXT("transition"), GuidText(Transition.ID), true),
        Target);
    const TArray<FString> TransitionStructuralFields = {
        TEXT("Conditions:"),
        TEXT("ID:"),
        TEXT("DelegateListener:")
    };
    for (const FString& StructuralField : TransitionStructuralFields)
    {
        TestFalse(
            *FString::Printf(TEXT("Transition structural field %s is not directly writable"), *StructuralField),
            HasCommentLineContainingAll(
                TransitionSchema,
                {StructuralField, TEXT("read/write")}));
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FSalStateTreeParameterDispatcherSchemaDiscoveryTest,
    "Loomle.Sal.StateTree.Discovery.ParameterDispatcherSchema",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSalStateTreeParameterDispatcherSchemaDiscoveryTest::RunTest(const FString& Parameters)
{
    const FDiscoveryStateTree Tree = MakeDiscoveryStateTree();
    const FSalResolvedTarget Target = ResolvedTarget(Tree);
    AddTestSchema(
        Tree,
        FGuid(0x84300001, 0x84300002, 0x84300003, 0x84300004));

    const FGuid RootDispatcherId(0x84400001, 0x84400002, 0x84400003, 0x84400004);
    FPropertyBagPropertyDesc RootDispatcher(
        TEXT("RootDispatcher"),
        EPropertyBagPropertyType::Struct,
        FStateTreeDelegateDispatcher::StaticStruct());
    RootDispatcher.ID = RootDispatcherId;
    RootDispatcher.PropertyFlags = static_cast<uint64>(CPF_Edit | CPF_BlueprintVisible);
    FInstancedPropertyBag& RootBag = const_cast<FInstancedPropertyBag&>(
        Tree.EditorData->GetRootParametersPropertyBag());
    RootBag.AddProperties({RootDispatcher});

    const FGuid StateContainerId(0x84500001, 0x84500002, 0x84500003, 0x84500004);
    const FGuid StateDispatcherId(0x84600001, 0x84600002, 0x84600003, 0x84600004);
    Tree.First->Parameters.ID = StateContainerId;
    FPropertyBagPropertyDesc StateDispatcher(
        TEXT("StateDispatcher"),
        EPropertyBagPropertyType::Struct,
        FStateTreeDelegateDispatcher::StaticStruct());
    StateDispatcher.ID = StateDispatcherId;
    StateDispatcher.PropertyFlags = static_cast<uint64>(CPF_Edit | CPF_BlueprintVisible);
    Tree.First->Parameters.Parameters.AddProperties({StateDispatcher});

    IConsoleVariable* DispatcherCVar = IConsoleManager::Get().FindConsoleVariable(
        TEXT("StateTree.Compiler.EnableParameterDelegateDispatcherBinding"));
    const int32 PreviousValue = DispatcherCVar != nullptr ? DispatcherCVar->GetInt() : 0;
    TestNotNull(TEXT("UE 5.7 registers the root Parameter Dispatcher compiler CVar"), DispatcherCVar);
    if (DispatcherCVar != nullptr)
    {
        DispatcherCVar->Set(0, ECVF_SetByCode);
    }

    const TSharedPtr<FJsonObject> RootSchema = FSalStateTreeInterface::Query(
        ExactObject(
            TEXT("parameter"),
            ParameterId(Tree.EditorData->GetRootParametersGuid(), RootDispatcherId),
            true),
        Target);
    TestFalse(TEXT("Root Dispatcher Parameter schema succeeds"), HasError(RootSchema));
    TestFalse(
        TEXT("Disabled root Parameter Dispatcher is not advertised as a Binding source"),
        HasCommentLineContainingAll(
            RootSchema,
            {TEXT("Value:"), TEXT("binding source")}));
    TestTrue(
        TEXT("Root Dispatcher schema names the native compiler switch"),
        HasCommentContaining(
            RootSchema,
            TEXT("StateTree.Compiler.EnableParameterDelegateDispatcherBinding")));
    TestTrue(
        TEXT("Root Dispatcher schema reports the disabled source capability"),
        HasCommentLineContainingAll(
            RootSchema,
            {TEXT("root Parameter Dispatcher source:"), TEXT("unavailable")}));

    const TSharedPtr<FJsonObject> StateSchema = FSalStateTreeInterface::Query(
        ExactObject(
            TEXT("parameter"),
            ParameterId(StateContainerId, StateDispatcherId),
            true),
        Target);
    TestFalse(TEXT("State-owned Dispatcher Parameter schema succeeds"), HasError(StateSchema));
    TestTrue(
        TEXT("State-owned Parameter Dispatcher remains a Binding source"),
        HasCommentLineContainingAll(
            StateSchema,
            {TEXT("Value:"), TEXT("binding source")}));

    if (DispatcherCVar != nullptr)
    {
        DispatcherCVar->Set(PreviousValue, ECVF_SetByCode);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FSalStateTreeConditionalSchemaDiscoveryTest,
    "Loomle.Sal.StateTree.Discovery.ConditionalSchema",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSalStateTreeConditionalSchemaDiscoveryTest::RunTest(const FString& Parameters)
{
    const FDiscoveryStateTree Tree = MakeDiscoveryStateTree();
    const FSalResolvedTarget Target = ResolvedTarget(Tree);
    AddTestSchema(
        Tree,
        FGuid(0x85000001, 0x85000002, 0x85000003, 0x85000004));

    const FGuid LinkedContainerId(0x86000001, 0x86000002, 0x86000003, 0x86000004);
    const FGuid SourceContainerId(0x87000001, 0x87000002, 0x87000003, 0x87000004);
    const FGuid FixedValueId(0x88000001, 0x88000002, 0x88000003, 0x88000004);
    Tree.Second->Type = EStateTreeStateType::Subtree;
    Tree.Second->Parameters.ID = SourceContainerId;
    FPropertyBagPropertyDesc FixedValueDesc(
        TEXT("FixedValue"),
        EPropertyBagPropertyType::Int32);
    FixedValueDesc.ID = FixedValueId;
    FixedValueDesc.PropertyFlags = static_cast<uint64>(CPF_Edit | CPF_BlueprintVisible);
    Tree.Second->Parameters.Parameters.AddProperties({FixedValueDesc});
    Tree.Second->Parameters.Parameters.SetValueInt32(TEXT("FixedValue"), 23);

    Tree.First->Type = EStateTreeStateType::Linked;
    Tree.First->LinkedSubtree = Tree.Second->GetLinkToState();
    Tree.First->Parameters.ID = LinkedContainerId;
    Tree.First->Parameters.bFixedLayout = true;
    Tree.First->UpdateParametersFromLinkedSubtree();

    const TSharedPtr<FJsonObject> FixedParameter = FSalStateTreeInterface::Query(
        ExactObject(
            TEXT("parameter"),
            ParameterId(LinkedContainerId, FixedValueId),
            true),
        Target);
    TestFalse(TEXT("Fixed-layout State Parameter accepts with schema"), HasError(FixedParameter));
    TestTrue(
        TEXT("Fixed-layout Parameter Value remains writable, resettable, and bindable"),
        HasCommentLineContainingAll(
            FixedParameter,
            {TEXT("Value:"), TEXT("read/write/reset"), TEXT("binding source/target")}));
    TestTrue(
        TEXT("Fixed-layout Parameter Name is read-only"),
        HasCommentLineContainingAll(
            FixedParameter,
            {TEXT("Name:"), TEXT("read-only")}));
    TestTrue(
        TEXT("Fixed-layout Parameter native type is read-only"),
        HasCommentLineContainingAll(
            FixedParameter,
            {TEXT("type:"), TEXT("read-only")}));
    TestTrue(
        TEXT("Fixed-layout Parameter cannot be removed"),
        HasCommentLineContainingAll(
            FixedParameter,
            {TEXT("remove:"), TEXT("unavailable")}));

    Tree.First->bHasRequiredEventToEnter = true;
    Tree.First->RequiredEventToEnter.PayloadStruct = FSalStateTreeBindingEventPayload::StaticStruct();
    const TSharedPtr<FJsonObject> StateEvent = FSalStateTreeInterface::Query(
        ExactObject(TEXT("state"), GuidText(Tree.First->ID), true),
        Target);
    TestFalse(TEXT("Active State Required Event schema succeeds"), HasError(StateEvent));
    TestTrue(
        TEXT("State Event schema exposes the payload member as a conditional source"),
        HasCommentLineContainingAll(
            StateEvent,
            {TEXT("RequiredEventToEnter.Payload.Value"), TEXT("binding source")}));
    TestTrue(
        TEXT("State Event schema exposes Origin as a conditional source"),
        HasCommentLineContainingAll(
            StateEvent,
            {TEXT("RequiredEventToEnter.Origin"), TEXT("binding source")}));
    TestTrue(
        TEXT("State Event source reports its native activation condition"),
        HasCommentContaining(StateEvent, TEXT("bHasRequiredEventToEnter == true")));
    TestFalse(
        TEXT("State Event PayloadStruct remains descriptor-only"),
        HasCommentLineContainingAll(
            StateEvent,
            {TEXT("RequiredEventToEnter.PayloadStruct"), TEXT("binding")}));

    FStateTreeTransition& Transition = Tree.First->AddTransition(
        EStateTreeTransitionTrigger::OnEvent,
        EStateTreeTransitionType::GotoState,
        Tree.Second);
    Transition.ID = FGuid(0x89000001, 0x89000002, 0x89000003, 0x89000004);
    Transition.RequiredEvent.PayloadStruct = FSalStateTreeBindingEventPayload::StaticStruct();
    const TSharedPtr<FJsonObject> TransitionEvent = FSalStateTreeInterface::Query(
        ExactObject(TEXT("transition"), GuidText(Transition.ID), true),
        Target);
    TestFalse(TEXT("Active Transition Required Event schema succeeds"), HasError(TransitionEvent));
    TestTrue(
        TEXT("Transition Event schema exposes the payload member as a conditional source"),
        HasCommentLineContainingAll(
            TransitionEvent,
            {TEXT("RequiredEvent.Payload.Value"), TEXT("binding source")}));
    TestTrue(
        TEXT("Transition Event schema exposes Origin as a conditional source"),
        HasCommentLineContainingAll(
            TransitionEvent,
            {TEXT("RequiredEvent.Origin"), TEXT("binding source")}));
    TestTrue(
        TEXT("Transition Event source reports its native activation condition"),
        HasCommentContaining(TransitionEvent, TEXT("Trigger == OnEvent")));
    TestFalse(
        TEXT("Transition Event PayloadStruct remains descriptor-only"),
        HasCommentLineContainingAll(
            TransitionEvent,
            {TEXT("RequiredEvent.PayloadStruct"), TEXT("binding")}));
    TestTrue(
        TEXT("Canonical Transition DelegateListener is a native Binding target"),
        HasCommentLineContainingAll(
            TransitionEvent,
            {TEXT("DelegateListener:"), TEXT("binding target")}));
    TestFalse(
        TEXT("Ordinary Transition fields are not Binding endpoints"),
        HasCommentLineContainingAll(
            TransitionEvent,
            {TEXT("Trigger:"), TEXT("binding")}));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FSalStateTreeGlobalBindingIdentityDiscoveryTest,
    "Loomle.Sal.StateTree.Discovery.GlobalBindingIdentity",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSalStateTreeGlobalBindingIdentityDiscoveryTest::RunTest(const FString& Parameters)
{
    {
        const FDiscoveryStateTree Tree = MakeDiscoveryStateTree();
        const FSalResolvedTarget Target = ResolvedTarget(Tree);
        const FGuid CollisionId(0x8A000001, 0x8A000002, 0x8A000003, 0x8A000004);
        USalStateTreeTestSchema* Schema = AddTestSchema(Tree, CollisionId);
        Schema->ContextData[0].Struct = FSalStateTreeBindingEventPayload::StaticStruct();
        AddBindingTask(Tree.First->Tasks, CollisionId, TEXT("Context Collision Task"));

        StateTreeSchema::FResolvedMember Member;
        FString Error;
        TestTrue(
            TEXT("A colliding Node remains exactly readable outside Binding resolution"),
            StateTreeSchema::ResolveMember(
                *Tree.Asset,
                *Tree.EditorData,
                TEXT("node"),
                GuidText(CollisionId),
                {TEXT("Instance"), TEXT("InputValue")},
                Member,
                Error));
        TestTrue(TEXT("The non-Binding Node read retains its native writable field"), Member.bWritable);
        TestTrue(
            TEXT("Node Binding resolution fails closed on a cross-kind StructID collision"),
            !StateTreeSchema::ResolveMember(
                *Tree.Asset,
                *Tree.EditorData,
                TEXT("node"),
                GuidText(CollisionId),
                {TEXT("Instance"), TEXT("InputValue")},
                Member,
                Error,
                StateTreeSchema::EMemberPurpose::BindingTarget)
                && Error.Contains(TEXT("collides")));

        const TSharedPtr<FJsonObject> NodeSchema = FSalStateTreeInterface::Query(
            ExactObject(TEXT("node"), GuidText(CollisionId), true),
            Target);
        TestFalse(TEXT("A colliding Node exact read still succeeds"), HasError(NodeSchema));
        TestFalse(
            TEXT("A colliding Node schema does not advertise an unsafe Binding target"),
            HasCommentLineContainingAll(
                NodeSchema,
                {TEXT("Instance.InputValue"), TEXT("binding target")}));
        TestTrue(
            TEXT("A Context exact reference fails when its ID collides with a Node"),
            HasError(FSalStateTreeInterface::Query(
                ExactObject(TEXT("object"), GuidText(CollisionId), true),
                Target)));
        const FStateTreeExternalDataDesc* ContextDescriptor = nullptr;
        TestTrue(
            TEXT("The canonical Context helper rejects the same cross-kind collision"),
            !StateTreeSchema::ResolveCanonicalContext(
                *Tree.EditorData,
                CollisionId,
                ContextDescriptor,
                Error)
                && ContextDescriptor == nullptr
                && Error.Contains(TEXT("collides")));
    }

    {
        const FDiscoveryStateTree Tree = MakeDiscoveryStateTree();
        const FSalResolvedTarget Target = ResolvedTarget(Tree);
        AddTestSchema(Tree, FGuid(0x8B000001, 0x8B000002, 0x8B000003, 0x8B000004));
        const FGuid CollisionId(0x8C000001, 0x8C000002, 0x8C000003, 0x8C000004);
        const FGuid PropertyId(0x8D000001, 0x8D000002, 0x8D000003, 0x8D000004);
        AddBindingTask(Tree.Second->Tasks, CollisionId, TEXT("Parameter Collision Task"));
        Tree.First->Parameters.ID = CollisionId;
        FPropertyBagPropertyDesc Desc(TEXT("LocalValue"), EPropertyBagPropertyType::Int32);
        Desc.ID = PropertyId;
        Desc.PropertyFlags = static_cast<uint64>(CPF_Edit | CPF_BlueprintVisible);
        Tree.First->Parameters.Parameters.AddProperties({Desc});
        Tree.First->Parameters.Parameters.SetValueInt32(TEXT("LocalValue"), 31);

        StateTreeSchema::FResolvedMember Member;
        FString Error;
        TestTrue(
            TEXT("A colliding Parameter remains exactly readable and editable"),
            StateTreeSchema::ResolveMember(
                *Tree.Asset,
                *Tree.EditorData,
                TEXT("parameter"),
                ParameterId(CollisionId, PropertyId),
                {TEXT("Value")},
                Member,
                Error));
        TestTrue(TEXT("The non-Binding Parameter value remains writable"), Member.bWritable);
        TestTrue(
            TEXT("Parameter Binding resolution rejects a Node collision"),
            !StateTreeSchema::ResolveMember(
                *Tree.Asset,
                *Tree.EditorData,
                TEXT("parameter"),
                ParameterId(CollisionId, PropertyId),
                {TEXT("Value")},
                Member,
                Error,
                StateTreeSchema::EMemberPurpose::BindingSource)
                && Error.Contains(TEXT("collides")));
        const TSharedPtr<FJsonObject> ParameterSchema = FSalStateTreeInterface::Query(
            ExactObject(
                TEXT("parameter"),
                ParameterId(CollisionId, PropertyId),
                true),
            Target);
        TestFalse(TEXT("A colliding Parameter exact Query still succeeds"), HasError(ParameterSchema));
        TestFalse(
            TEXT("A colliding Parameter schema advertises no Binding endpoint"),
            HasCommentLineContainingAll(
                ParameterSchema,
                {TEXT("Value:"), TEXT("binding source")}));
        TestFalse(
            TEXT("A colliding Parameter schema advertises no Binding target"),
            HasCommentLineContainingAll(
                ParameterSchema,
                {TEXT("Value:"), TEXT("binding target")}));
    }

    {
        const FDiscoveryStateTree Tree = MakeDiscoveryStateTree();
        const FSalResolvedTarget Target = ResolvedTarget(Tree);
        AddTestSchema(Tree, FGuid(0x8E000001, 0x8E000002, 0x8E000003, 0x8E000004));
        const FGuid CollisionId(0x8F000001, 0x8F000002, 0x8F000003, 0x8F000004);
        AddBindingTask(Tree.Second->Tasks, CollisionId, TEXT("Transition Collision Task"));
        FStateTreeTransition& Transition = Tree.First->AddTransition(
            EStateTreeTransitionTrigger::OnDelegate,
            EStateTreeTransitionType::GotoState,
            Tree.Second);
        Transition.ID = CollisionId;

        const TSharedPtr<FJsonObject> TransitionSchema = FSalStateTreeInterface::Query(
            ExactObject(TEXT("transition"), GuidText(CollisionId), true),
            Target);
        TestFalse(TEXT("A colliding Transition exact Query still succeeds"), HasError(TransitionSchema));
        TestFalse(
            TEXT("A colliding Transition does not advertise DelegateListener as a target"),
            HasCommentLineContainingAll(
                TransitionSchema,
                {TEXT("DelegateListener:"), TEXT("binding target")}));
        StateTreeSchema::FResolvedMember Member;
        FString Error;
        TestTrue(
            TEXT("Transition Delegate Binding resolution rejects a Node collision"),
            !StateTreeSchema::ResolveMember(
                *Tree.Asset,
                *Tree.EditorData,
                TEXT("transition"),
                GuidText(CollisionId),
                {TEXT("DelegateListener")},
                Member,
                Error,
                StateTreeSchema::EMemberPurpose::BindingTarget)
                && Error.Contains(TEXT("collides")));
    }

    {
        const FDiscoveryStateTree Tree = MakeDiscoveryStateTree();
        const FSalResolvedTarget Target = ResolvedTarget(Tree);
        AddTestSchema(Tree, FGuid(0x90000001, 0x90000002, 0x90000003, 0x90000004));
        Tree.First->bHasRequiredEventToEnter = true;
        Tree.First->RequiredEventToEnter.PayloadStruct =
            FSalStateTreeBindingEventPayload::StaticStruct();
        const FGuid EventId = Tree.First->GetEventID();
        AddBindingTask(Tree.Second->Tasks, EventId, TEXT("Event Collision Task"));

        const TSharedPtr<FJsonObject> StateSchema = FSalStateTreeInterface::Query(
            ExactObject(TEXT("state"), GuidText(Tree.First->ID), true),
            Target);
        TestFalse(TEXT("The owning State exact Query remains readable"), HasError(StateSchema));
        TestFalse(
            TEXT("A colliding active State Event does not advertise Payload as a source"),
            HasCommentLineContainingAll(
                StateSchema,
                {TEXT("RequiredEventToEnter.Payload.Value"), TEXT("binding source")}));
        StateTreeSchema::FResolvedMember Member;
        FString Error;
        TestTrue(
            TEXT("State Event Binding resolution rejects a Node collision"),
            !StateTreeSchema::ResolveMember(
                *Tree.Asset,
                *Tree.EditorData,
                TEXT("state"),
                GuidText(Tree.First->ID),
                {TEXT("RequiredEventToEnter"), TEXT("Payload"), TEXT("Value")},
                Member,
                Error,
                StateTreeSchema::EMemberPurpose::BindingSource)
                && Error.Contains(TEXT("collides")));

        FStateTreeTransition& Transition = Tree.First->AddTransition(
            EStateTreeTransitionTrigger::OnEvent,
            EStateTreeTransitionType::GotoState,
            Tree.Second);
        Transition.ID = FGuid(0x90100001, 0x90100002, 0x90100003, 0x90100004);
        Transition.RequiredEvent.PayloadStruct = FSalStateTreeBindingEventPayload::StaticStruct();
        AddBindingTask(
            Tree.Second->Tasks,
            Transition.GetEventID(),
            TEXT("Transition Event Collision Task"));
        const TSharedPtr<FJsonObject> TransitionSchema = FSalStateTreeInterface::Query(
            ExactObject(TEXT("transition"), GuidText(Transition.ID), true),
            Target);
        TestFalse(TEXT("The owning Transition exact Query remains readable"), HasError(TransitionSchema));
        TestFalse(
            TEXT("A colliding active Transition Event does not advertise Payload as a source"),
            HasCommentLineContainingAll(
                TransitionSchema,
                {TEXT("RequiredEvent.Payload.Value"), TEXT("binding source")}));
        TestTrue(
            TEXT("Transition Event Binding resolution rejects a Node collision"),
            !StateTreeSchema::ResolveMember(
                *Tree.Asset,
                *Tree.EditorData,
                TEXT("transition"),
                GuidText(Transition.ID),
                {TEXT("RequiredEvent"), TEXT("Payload"), TEXT("Value")},
                Member,
                Error,
                StateTreeSchema::EMemberPurpose::BindingSource)
                && Error.Contains(TEXT("collides")));
    }

    {
        const FDiscoveryStateTree Tree = MakeDiscoveryStateTree();
        USalStateTreeTestSchema* Schema = AddTestSchema(
            Tree,
            FGuid(0x91000001, 0x91000002, 0x91000003, 0x91000004));
        const FGuid ParameterGuid(0x92000001, 0x92000002, 0x92000003, 0x92000004);
        AddRootIntParameter(Tree, ParameterGuid, TEXT("SourceValue"));
        const FGuid NodeId(0x93000001, 0x93000002, 0x93000003, 0x93000004);
        AddBindingTask(Tree.First->Tasks, NodeId, TEXT("Compatibility Target"));

        StateTreeSchema::FResolvedMember Source;
        StateTreeSchema::FResolvedMember Destination;
        FString Error;
        TestTrue(
            TEXT("Canonical source resolves before the authored collision"),
            StateTreeSchema::ResolveMember(
                *Tree.Asset,
                *Tree.EditorData,
                TEXT("parameter"),
                ParameterId(Tree.EditorData->GetRootParametersGuid(), ParameterGuid),
                {TEXT("Value")},
                Source,
                Error,
                StateTreeSchema::EMemberPurpose::BindingSource));
        TestTrue(
            TEXT("Canonical target resolves before the authored collision"),
            StateTreeSchema::ResolveMember(
                *Tree.Asset,
                *Tree.EditorData,
                TEXT("node"),
                GuidText(NodeId),
                {TEXT("Instance"), TEXT("InputValue")},
                Destination,
                Error,
                StateTreeSchema::EMemberPurpose::BindingTarget));
        Schema->ContextData.Add(FStateTreeExternalDataDesc(
            FName(TEXT("LateCollision")),
            FSalStateTreeBindingEventPayload::StaticStruct(),
            Tree.EditorData->GetRootParametersGuid()));
        TestTrue(
            TEXT("AreBindingCompatible revalidates previously resolved StructIDs"),
            !StateTreeSchema::AreBindingCompatible(
                *Tree.EditorData,
                Source,
                Destination,
                Error)
                && Error.Contains(TEXT("stale or ambiguous"))
                && Error.Contains(TEXT("collides")));
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FSalStateTreeDestinationPaletteDiscoveryTest,
    "Loomle.Sal.StateTree.Discovery.DestinationPalette",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSalStateTreeDestinationPaletteDiscoveryTest::RunTest(const FString& Parameters)
{
    const FDiscoveryStateTree Tree = MakeDiscoveryStateTree();
    const FSalResolvedTarget Target = ResolvedTarget(Tree);
    USalStateTreeTestSchema* Schema = AddTestSchema(
        Tree,
        FGuid(0x91000001, 0x91000002, 0x91000003, 0x91000004),
        true);
    Schema->bAllowPropertyFunctions = true;

    const FGuid BindingTargetId(0x91000011, 0x91000012, 0x91000013, 0x91000014);
    AddBindingTask(Tree.First->Tasks, BindingTargetId, TEXT("Property Function Target"));

    const FString DestinationId = GuidText(Tree.First->ID);
    FSalQuery Search = PaletteEntriesTo(DestinationId, TEXT("Tasks"));
    Search.PageLimit = 200;
    const TSharedPtr<FJsonObject> SearchResult = FSalStateTreeInterface::Query(Search, Target);
    TestFalse(TEXT("Destination-bound StateTree Palette search succeeds"), HasError(SearchResult));
    const FString PaletteId = FirstPaletteId(SearchResult, TEXT("node"));
    TestFalse(TEXT("Task Palette returns a copyable opaque capability id"), PaletteId.IsEmpty());

    const TSharedPtr<FJsonObject> RepeatResult = FSalStateTreeInterface::Query(Search, Target);
    TestEqual(
        TEXT("The same native candidate has a deterministic Palette id"),
        FirstPaletteId(RepeatResult, TEXT("node")),
        PaletteId);

    const TSharedPtr<FJsonObject> ExactResult = FSalStateTreeInterface::Query(
        ExactPaletteTo(PaletteId, DestinationId, TEXT("Tasks"), true),
        Target);
    TestFalse(TEXT("Exact destination-bound Palette entry accepts with schema"), HasError(ExactResult));
    TestEqual(
        TEXT("Exact Palette read repeats the copyable constructor capability"),
        FirstPaletteId(ExactResult, TEXT("node")),
        PaletteId);
    TestTrue(TEXT("Exact Palette read adds adjacent schema guidance"), HasSchemaGuidance(ExactResult));
    TestTrue(
        TEXT("Exact Node Palette schema exposes its native Node surface"),
        HasCommentContaining(ExactResult, TEXT("Node.Name")));
    TestTrue(
        TEXT("Exact Node Palette schema exposes writable and bindable Instance input"),
        HasCommentLineContainingAll(
            ExactResult,
            {TEXT("Instance.InputValue"), TEXT("read/write"), TEXT("binding target")}));
    TestTrue(
        TEXT("Exact Node Palette schema exposes its Instance output direction"),
        HasCommentLineContainingAll(
            ExactResult,
            {TEXT("Instance.OutputValue"), TEXT("binding source")}));

    const TSharedPtr<FJsonObject> PropertyFunctionDestination = MemberRef(
        StableRef(TEXT("node"), GuidText(BindingTargetId)),
        {TEXT("Instance"), TEXT("InputValue")});
    FSalQuery FunctionSearch = Query(TEXT("palette_entries"));
    FunctionSearch.Operation->SetStringField(
        TEXT("text"),
        TEXT("SalStateTreeIntPropertyFunction"));
    FunctionSearch.Operation->SetObjectField(TEXT("to"), PropertyFunctionDestination);
    const TSharedPtr<FJsonObject> FunctionSearchResult = FSalStateTreeInterface::Query(
        FunctionSearch,
        Target);
    TestFalse(
        TEXT("A native Property Function is discoverable at one compatible Binding target"),
        HasError(FunctionSearchResult));
    const FString FunctionPaletteId = FirstPaletteId(FunctionSearchResult, TEXT("node"));
    TestFalse(
        TEXT("Property Function Palette returns a copyable capability id"),
        FunctionPaletteId.IsEmpty());

    FSalQuery ExactFunction = Query(TEXT("palette"));
    ExactFunction.Operation->SetStringField(TEXT("id"), FunctionPaletteId);
    ExactFunction.Operation->SetObjectField(TEXT("to"), PropertyFunctionDestination);
    ExactFunction.With.Add(TEXT("schema"));
    const TSharedPtr<FJsonObject> ExactFunctionResult = FSalStateTreeInterface::Query(
        ExactFunction,
        Target);
    TestFalse(
        TEXT("Exact compatible Property Function Palette schema succeeds"),
        HasError(ExactFunctionResult));
    TestTrue(
        TEXT("Property Function schema reports the actual Instance output member"),
        HasCommentContaining(
            ExactFunctionResult,
            TEXT("property function output: Instance.IntResult")));

    FSalQuery IncompatibleFunctionSearch = Query(TEXT("palette_entries"));
    IncompatibleFunctionSearch.Operation->SetStringField(
        TEXT("text"),
        TEXT("SalStateTreeFloatPropertyFunction"));
    IncompatibleFunctionSearch.Operation->SetObjectField(
        TEXT("to"),
        MemberRef(
            StableRef(TEXT("node"), GuidText(BindingTargetId)),
            {TEXT("Instance"), TEXT("InputValues")}));
    const TSharedPtr<FJsonObject> IncompatibleFunctionResult =
        FSalStateTreeInterface::Query(IncompatibleFunctionSearch, Target);
    TestFalse(
        TEXT("An incompatible Property Function Palette search remains a valid empty page"),
        HasError(IncompatibleFunctionResult));
    TestEqual(
        TEXT("Property Function Palette filters a scalar float output from an int-array Binding target"),
        CallArgs(IncompatibleFunctionResult, TEXT("node")).Num(),
        0);

    Tree.Second->Type = EStateTreeStateType::Subtree;
    const TSharedPtr<FJsonObject> UnrestrictedStateSearch = FSalStateTreeInterface::Query(
        PaletteEntriesToTargetMember(TEXT("tree"), TEXT("SubTrees")),
        Target);
    TestFalse(
        TEXT("Unrestricted root State Palette discovery succeeds"),
        HasError(UnrestrictedStateSearch));
    const auto StatePaletteIdForType = [&](const FString& Type)
    {
        for (const TSharedPtr<FJsonObject>& Args : CallArgs(UnrestrictedStateSearch, TEXT("state")))
        {
            FString ActualType;
            FString Palette;
            if (Args.IsValid()
                && Args->TryGetStringField(TEXT("Type"), ActualType)
                && ActualType == Type
                && Args->TryGetStringField(TEXT("palette"), Palette))
            {
                return Palette;
            }
        }
        return FString();
    };
    const FString LinkedAssetPaletteId = StatePaletteIdForType(TEXT("LinkedAsset"));
    const FString LinkedPaletteId = StatePaletteIdForType(TEXT("Linked"));
    TestFalse(
        TEXT("State Palette exposes its fixed LinkedAsset capability"),
        LinkedAssetPaletteId.IsEmpty());
    TestFalse(
        TEXT("State Palette exposes a destination-bound fixed Linked capability"),
        LinkedPaletteId.IsEmpty());

    const TSharedPtr<FJsonObject> ExactLinkedAsset = FSalStateTreeInterface::Query(
        ExactPaletteToTargetMember(
            LinkedAssetPaletteId,
            TEXT("tree"),
            TEXT("SubTrees"),
            true),
        Target);
    TestFalse(TEXT("Exact LinkedAsset Palette schema succeeds"), HasError(ExactLinkedAsset));
    TestTrue(
        TEXT("LinkedAsset Palette schema marks native Type fixed"),
        HasCommentLineContainingAll(
            ExactLinkedAsset,
            {TEXT("Type:"), TEXT("fixed"), TEXT("LinkedAsset")}));
    TestTrue(
        TEXT("LinkedAsset Palette schema does not advertise Type writable"),
        HasCommentLineContainingAll(
            ExactLinkedAsset,
            {TEXT("Type:"), TEXT("read-only")}));
    TestTrue(
        TEXT("LinkedAsset Palette schema marks native SelectionBehavior fixed"),
        HasCommentLineContainingAll(
            ExactLinkedAsset,
            {TEXT("SelectionBehavior:"), TEXT("fixed"), TEXT("TryEnterState")}));
    TestTrue(
        TEXT("LinkedAsset Palette schema does not advertise SelectionBehavior writable"),
        HasCommentLineContainingAll(
            ExactLinkedAsset,
            {TEXT("SelectionBehavior:"), TEXT("read-only")}));
    TestTrue(
        TEXT("LinkedAsset Palette schema keeps its asset reference nullable and explicit"),
        HasCommentLineContainingAll(
            ExactLinkedAsset,
            {TEXT("LinkedAsset:"), TEXT("nullable"), TEXT("not fixed")}));

    const TSharedPtr<FJsonObject> ExactLinked = FSalStateTreeInterface::Query(
        ExactPaletteToTargetMember(
            LinkedPaletteId,
            TEXT("tree"),
            TEXT("SubTrees"),
            true),
        Target);
    TestFalse(TEXT("Exact destination-bound Linked Palette schema succeeds"), HasError(ExactLinked));
    TestTrue(
        TEXT("Linked Palette schema marks native Type fixed"),
        HasCommentLineContainingAll(
            ExactLinked,
            {TEXT("Type:"), TEXT("fixed"), TEXT("Linked")}));
    TestTrue(
        TEXT("Linked Palette schema does not advertise Type writable"),
        HasCommentLineContainingAll(
            ExactLinked,
            {TEXT("Type:"), TEXT("read-only")}));
    TestTrue(
        TEXT("Linked Palette schema preserves its exact destination-bound target"),
        HasCommentLineContainingAll(
            ExactLinked,
            {TEXT("LinkedSubtree:"), TEXT("fixed"), GuidText(Tree.Second->ID)}));

    // The copied constructor must already satisfy the active native Schema,
    // even when its allowed selection differs from UStateTreeState's CDO.
    Schema->bRestrictStateTypes = true;
    Schema->bRestrictStateSelection = true;
    const TSharedPtr<FJsonObject> StateSearch = FSalStateTreeInterface::Query(
        PaletteEntriesToTargetMember(TEXT("tree"), TEXT("SubTrees")),
        Target);
    TestFalse(TEXT("Root State Palette discovery succeeds"), HasError(StateSearch));
    const FString StatePaletteId = FirstPaletteId(StateSearch, TEXT("state"));
    TestFalse(TEXT("Root State Palette exposes one copyable capability"), StatePaletteId.IsEmpty());
    const TArray<TSharedPtr<FJsonObject>> StateConstructors = CallArgs(
        StateSearch,
        TEXT("state"));
    FString InitialStateType;
    FString InitialSelection;
    TestTrue(
        TEXT("State Palette constructor carries one Schema-approved native Type"),
        StateConstructors.Num() == 1
            && StateConstructors[0]->TryGetStringField(TEXT("Type"), InitialStateType)
            && InitialStateType == TEXT("State"));
    TestTrue(
        TEXT("State Palette constructor carries one Schema-approved native SelectionBehavior"),
        StateConstructors.Num() == 1
            && StateConstructors[0]->TryGetStringField(
                TEXT("SelectionBehavior"),
                InitialSelection)
            && InitialSelection == TEXT("TryEnterState"));
    const TSharedPtr<FJsonObject> ExactState = FSalStateTreeInterface::Query(
        ExactPaletteToTargetMember(
            StatePaletteId,
            TEXT("tree"),
            TEXT("SubTrees"),
            true),
        Target);
    TestFalse(TEXT("Exact State Palette schema succeeds"), HasError(ExactState));
    TestTrue(
        TEXT("State Palette schema exposes native Type"),
        HasCommentContaining(ExactState, TEXT("Type:")));
    TestTrue(
        TEXT("State Palette schema exposes native SelectionBehavior"),
        HasCommentContaining(ExactState, TEXT("SelectionBehavior:")));
    TestTrue(
        TEXT("State Palette schema derives allowed Types from the current native Schema"),
        HasCommentContaining(ExactState, TEXT("IsStateTypeAllowed")));
    TestTrue(
        TEXT("State Palette schema derives allowed selection from the current native Schema"),
        HasCommentContaining(ExactState, TEXT("IsStateSelectionAllowed")));
    TestFalse(
        TEXT("Ordinary State Palette allowed Types do not absorb fixed Linked capabilities"),
        HasCommentLineContainingAll(
            ExactState,
            {TEXT("IsStateTypeAllowed:"), TEXT("Linked")}));
    TestTrue(
        TEXT("Ordinary State Palette marks LinkedSubtree unavailable on this capability"),
        HasCommentLineContainingAll(
            ExactState,
            {TEXT("LinkedSubtree:"), TEXT("read-only")}));
    TestTrue(
        TEXT("Ordinary State Palette marks LinkedAsset unavailable on this capability"),
        HasCommentLineContainingAll(
            ExactState,
            {TEXT("LinkedAsset:"), TEXT("read-only")}));
    TestTrue(
        TEXT("Ordinary State Palette directs linked creation to fixed Palette entries"),
        HasCommentLineContainingAll(
            ExactState,
            {TEXT("LinkedSubtree"), TEXT("LinkedAsset"), TEXT("fixed Palette")}));

    const TSharedPtr<FJsonObject> TransitionSearch = FSalStateTreeInterface::Query(
        PaletteEntriesTo(DestinationId, TEXT("Transitions")),
        Target);
    TestFalse(TEXT("Transition Palette discovery succeeds"), HasError(TransitionSearch));
    const FString TransitionPaletteId = FirstPaletteId(TransitionSearch, TEXT("transition"));
    TestFalse(TEXT("Transition Palette exposes one copyable capability"), TransitionPaletteId.IsEmpty());
    const TSharedPtr<FJsonObject> ExactTransition = FSalStateTreeInterface::Query(
        ExactPaletteTo(
            TransitionPaletteId,
            DestinationId,
            TEXT("Transitions"),
            true),
        Target);
    TestFalse(TEXT("Exact Transition Palette schema succeeds"), HasError(ExactTransition));
    TestTrue(
        TEXT("Transition Palette schema exposes native Trigger"),
        HasCommentContaining(ExactTransition, TEXT("Trigger:")));
    TestTrue(
        TEXT("Transition Palette schema exposes native State link"),
        HasCommentContaining(ExactTransition, TEXT("State:")));
    TestTrue(
        TEXT("Transition Palette schema explains the OnEvent conditional surface"),
        HasCommentContaining(ExactTransition, TEXT("Trigger == OnEvent")));
    TestTrue(
        TEXT("Transition Palette schema explains the OnDelegate conditional surface"),
        HasCommentContaining(ExactTransition, TEXT("Trigger == OnDelegate")));

    const TSharedPtr<FJsonObject> ParameterSearch = FSalStateTreeInterface::Query(
        PaletteEntriesTo(DestinationId, TEXT("Parameters")),
        Target);
    TestFalse(TEXT("Parameter Palette discovery succeeds"), HasError(ParameterSearch));
    const TArray<TSharedPtr<FJsonObject>> ParameterConstructors =
        CallArgs(ParameterSearch, TEXT("parameter"));
    FString ParameterType;
    TestTrue(
        TEXT("Parameter Palette returns one directly consumable constructor"),
        ParameterConstructors.Num() == 1);
    TestTrue(
        TEXT("Parameter Palette constructor carries its required Name"),
        ParameterConstructors.Num() == 1
            && ParameterConstructors[0]->HasField(TEXT("Name")));
    TestTrue(
        TEXT("Parameter Palette constructor carries its required native type"),
        ParameterConstructors.Num() == 1
            && ParameterConstructors[0]->TryGetStringField(TEXT("type"), ParameterType)
            && !ParameterType.IsEmpty());

    const TSharedPtr<FJsonObject> WrongRole = FSalStateTreeInterface::Query(
        ExactPaletteTo(PaletteId, DestinationId, TEXT("EnterConditions")),
        Target);
    TestTrue(TEXT("A Task capability cannot be reused at a Condition destination"), HasError(WrongRole));
    TestTrue(
        TEXT("Schema or role rejection is distinct from a missing capability"),
        HasDiagnosticCode(WrongRole, TEXT("resolution.palette_not_spawnable")));

    FSalQuery SearchWithSchema = Search;
    SearchWithSchema.With.Add(TEXT("schema"));
    TestTrue(
        TEXT("Palette collection rejects with schema"),
        HasAnyDiagnosticCode(
            FSalStateTreeInterface::Query(SearchWithSchema, Target),
            {TEXT("capability.clause_unavailable"), TEXT("capability.detail_unavailable")}));

    FSalQuery SearchWithWhere = Search;
    SearchWithWhere.Where = MakeShared<FJsonObject>();
    TestTrue(
        TEXT("Palette collection rejects where"),
        HasDiagnosticCode(
            FSalStateTreeInterface::Query(SearchWithWhere, Target),
            TEXT("capability.clause_unavailable")));

    FSalQuery InvalidCursor = Search;
    InvalidCursor.PageAfter = TEXT("not-a-state-tree-palette-cursor");
    TestTrue(
        TEXT("Malformed StateTree Palette cursor fails closed"),
        HasDiagnosticCode(
            FSalStateTreeInterface::Query(InvalidCursor, Target),
            TEXT("validation.invalid_cursor")));

    FSalQuery ExactPage = ExactPaletteTo(PaletteId, DestinationId, TEXT("Tasks"));
    ExactPage.PageLimit = 1;
    TestTrue(
        TEXT("Exact Palette entry rejects pagination"),
        HasDiagnosticCode(
            FSalStateTreeInterface::Query(ExactPage, Target),
            TEXT("capability.clause_unavailable")));

    Tree.First->ID = FGuid(0x92000001, 0x92000002, 0x92000003, 0x92000004);
    const TSharedPtr<FJsonObject> StaleDestination = FSalStateTreeInterface::Query(
        ExactPaletteTo(PaletteId, DestinationId, TEXT("Tasks")),
        Target);
    TestTrue(TEXT("Exact Palette revalidates a stale destination"), HasError(StaleDestination));
    TestTrue(
        TEXT("Stale destination is reported as resolution or context failure"),
        HasAnyDiagnosticCode(
            StaleDestination,
            {TEXT("resolution.object_not_found"), TEXT("validation.palette_context_invalid")}));

    // UE's native Struct-ID lookup returns the first match. SAL must instead
    // reject a Context descriptor whose ID collides with an authored Node.
    Schema->ContextData.Add(FStateTreeExternalDataDesc(
        FName(TEXT("CollidingContext")),
        FSalStateTreeBindingEventPayload::StaticStruct(),
        BindingTargetId));
    StateTreeSchema::FResolvedMember CollidingContextMember;
    FString CollidingContextError;
    TestTrue(
        TEXT("A Context ID collision is detected explicitly"),
        !StateTreeSchema::ResolveMember(
            *Tree.Asset,
            *Tree.EditorData,
            TEXT("object"),
            GuidText(BindingTargetId),
            {TEXT("Value")},
            CollidingContextMember,
            CollidingContextError,
            StateTreeSchema::EMemberPurpose::BindingSource)
            && CollidingContextError.Contains(TEXT("collides")));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FSalStateTreeQueryCompletenessDiscoveryTest,
    "Loomle.Sal.StateTree.Discovery.QueryCompleteness",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSalStateTreeQueryCompletenessDiscoveryTest::RunTest(const FString& Parameters)
{
    const FDiscoveryStateTree Tree = MakeDiscoveryStateTree();
    const FSalResolvedTarget Target = ResolvedTarget(Tree);
    const FGuid ContextId(0xA1000001, 0xA1000002, 0xA1000003, 0xA1000004);
    AddTestSchema(Tree, ContextId);

    const FGuid ParameterGuid(0xA2000001, 0xA2000002, 0xA2000003, 0xA2000004);
    AddRootIntParameter(Tree, ParameterGuid, TEXT("GlobalThreshold"));
    const FGuid GlobalTaskId(0xA3000001, 0xA3000002, 0xA3000003, 0xA3000004);
    AddBindingTask(Tree.EditorData->GlobalTasks, GlobalTaskId, TEXT("Global Task"));
    Tree.EditorData->GlobalTasksCompletion = EStateTreeTaskCompletionType::All;

    const TSharedPtr<FJsonObject> TargetResult = FSalStateTreeInterface::Query(Query(TEXT("target")), Target);
    TestFalse(TEXT("Exact StateTree target read succeeds"), HasError(TargetResult));
    TestTrue(
        TEXT("Exact target includes the Root Parameter surface it owns"),
        CallIds(TargetResult, TEXT("parameter")).Contains(
            ParameterId(Tree.EditorData->GetRootParametersGuid(), ParameterGuid)));
    TestTrue(
        TEXT("Exact target includes the Global Task surface it owns"),
        CallIds(TargetResult, TEXT("node")).Contains(GuidText(GlobalTaskId)));
    const TArray<TSharedPtr<FJsonObject>> AssetArgs = CallArgs(TargetResult, TEXT("asset"));
    TestTrue(
        TEXT("Exact target exposes native GlobalTasksCompletion"),
        AssetArgs.Num() == 1 && AssetArgs[0]->HasField(TEXT("GlobalTasksCompletion")));

    const FGuid ContextTaskId(0xA4000001, 0xA4000002, 0xA4000003, 0xA4000004);
    AddContextTask(Tree.First->Tasks, ContextTaskId, TEXT("Automatic Context Consumer"));
    const TSharedPtr<FJsonObject> Summary = FSalStateTreeInterface::Query(Query(TEXT("summary")), Target);
    TestFalse(TEXT("StateTree summary with automatic Context remains readable"), HasError(Summary));
    TestTrue(
        TEXT("Summary counts derived automatic Context relationships"),
        HasCommentContaining(Summary, TEXT("automatic context relationships: 1")));

    Tree.First->Type = EStateTreeStateType::State;
    Tree.First->LinkedSubtree = Tree.Second->GetLinkToState();
    Tree.First->bHasRequiredEventToEnter = false;
    Tree.First->RequiredEventToEnter.PayloadStruct = FSalStateTreeBindingEventPayload::StaticStruct();
    const TSharedPtr<FJsonObject> DormantState = FSalStateTreeInterface::Query(
        ExactObject(TEXT("state"), GuidText(Tree.First->ID)),
        Target);
    TestFalse(TEXT("A State with dormant authored fields remains readable"), HasError(DormantState));
    const TArray<TSharedPtr<FJsonObject>> StateArgs = CallArgs(DormantState, TEXT("state"));
    TestTrue(
        TEXT("Dormant LinkedSubtree data is preserved instead of hidden"),
        StateArgs.Num() == 1 && StateArgs[0]->HasField(TEXT("LinkedSubtree")));
    TestTrue(
        TEXT("Dormant RequiredEventToEnter data is preserved instead of hidden"),
        StateArgs.Num() == 1 && StateArgs[0]->HasField(TEXT("RequiredEventToEnter")));
    TestTrue(
        TEXT("Dormant authored values are identified for the agent"),
        HasCommentContaining(DormantState, TEXT("dormant"))
            || HasCommentContaining(DormantState, TEXT("inactive")));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FSalStateTreePatchDryRunAndAtomicityTest,
    "Loomle.Sal.StateTree.Patch.DryRunAndAtomicity",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSalStateTreePatchDryRunAndAtomicityTest::RunTest(const FString& Parameters)
{
    const FDiscoveryStateTree Tree = MakeDiscoveryStateTree();
    const FSalResolvedTarget Target = ResolvedTarget(Tree);
    AddTestSchema(Tree, FGuid(0xB1000001, 0xB1000002, 0xB1000003, 0xB1000004));
    UStateTreeEditingSubsystem::ValidateStateTree(Tree.Asset);
    const FName OriginalName = Tree.First->Name;

    FSalPatch DryRun = Patch(true);
    TSharedPtr<FJsonObject> DrySet = Operation(TEXT("set"));
    DrySet->SetObjectField(
        TEXT("target"),
        MemberRef(StableRef(TEXT("state"), GuidText(Tree.First->ID)), {TEXT("Name")}));
    DrySet->SetStringField(TEXT("value"), TEXT("DryRunName"));
    AddStatement(DryRun, DrySet);
    const TSharedPtr<FJsonObject> DryResult = FSalStateTreeInterface::Patch(DryRun, Target);
    TestFalse(TEXT("StateTree dry-run preflight is valid"), HasError(DryResult));
    TestTrue(TEXT("StateTree dry-run reports dryRun"), ResultBool(DryResult, TEXT("dryRun"), false));
    TestFalse(TEXT("StateTree dry-run never reports applied"), ResultBool(DryResult, TEXT("applied"), true));
    TestEqual(TEXT("StateTree dry-run does not mutate the live State"), Tree.First->Name, OriginalName);

    FSalPatch Failing = Patch(false);
    TSharedPtr<FJsonObject> FirstSet = Operation(TEXT("set"));
    FirstSet->SetObjectField(
        TEXT("target"),
        MemberRef(StableRef(TEXT("state"), GuidText(Tree.First->ID)), {TEXT("Name")}));
    FirstSet->SetStringField(TEXT("value"), TEXT("MustRollBack"));
    AddStatement(Failing, FirstSet);
    TSharedPtr<FJsonObject> MissingRemove = Operation(TEXT("remove"));
    MissingRemove->SetObjectField(
        TEXT("target"),
        StableRef(
            TEXT("state"),
            GuidText(FGuid(0xB2000001, 0xB2000002, 0xB2000003, 0xB2000004))));
    AddStatement(Failing, MissingRemove);
    const TSharedPtr<FJsonObject> FailedResult = FSalStateTreeInterface::Patch(Failing, Target);
    TestTrue(TEXT("A later invalid operation rejects the complete StateTree Patch"), HasError(FailedResult));
    TestFalse(TEXT("A rejected StateTree Patch reports no live apply"), ResultBool(FailedResult, TEXT("applied"), true));
    TestEqual(TEXT("A rejected StateTree Patch preserves the earlier live field"), Tree.First->Name, OriginalName);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FSalStateTreePatchLifecycleTest,
    "Loomle.Sal.StateTree.Patch.Lifecycle",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSalStateTreePatchLifecycleTest::RunTest(const FString& Parameters)
{
    const FDiscoveryStateTree Tree = MakeDiscoveryStateTree();
    const FSalResolvedTarget Target = ResolvedTarget(Tree);
    AddTestSchema(Tree, FGuid(0xB3000001, 0xB3000002, 0xB3000003, 0xB3000004));
    UStateTreeEditingSubsystem::ValidateStateTree(Tree.Asset);
    const FGuid RemovedId = Tree.First->ID;
    const FGuid MovedId = Tree.Second->ID;

    FSalPatch Lifecycle = Patch(false);
    AddStatement(
        Lifecycle,
        ConstructorBinding(TEXT("added_state"), TEXT("state"), TEXT("state_tree.state")));

    TSharedPtr<FJsonObject> Add = Operation(TEXT("add"));
    Add->SetObjectField(TEXT("target"), LocalRef(TEXT("added_state")));
    Add->SetObjectField(
        TEXT("to"),
        MemberRef(StableRef(TEXT("state"), GuidText(Tree.Root->ID)), {TEXT("Children")}));
    AddStatement(Lifecycle, Add);

    TSharedPtr<FJsonObject> Set = Operation(TEXT("set"));
    Set->SetObjectField(
        TEXT("target"),
        MemberRef(LocalRef(TEXT("added_state")), {TEXT("Name")}));
    Set->SetStringField(TEXT("value"), TEXT("AddedBySAL"));
    AddStatement(Lifecycle, Set);

    TSharedPtr<FJsonObject> Move = Operation(TEXT("move"));
    Move->SetObjectField(TEXT("target"), StableRef(TEXT("state"), GuidText(MovedId)));
    Move->SetObjectField(TEXT("after"), LocalRef(TEXT("added_state")));
    AddStatement(Lifecycle, Move);

    TSharedPtr<FJsonObject> Remove = Operation(TEXT("remove"));
    Remove->SetObjectField(TEXT("target"), StableRef(TEXT("state"), GuidText(RemovedId)));
    AddStatement(Lifecycle, Remove);

    const TSharedPtr<FJsonObject> Result = FSalStateTreeInterface::Patch(Lifecycle, Target);
    TestFalse(TEXT("StateTree lifecycle Patch succeeds"), HasError(Result));
    TestTrue(TEXT("StateTree lifecycle Patch reports applied"), ResultBool(Result, TEXT("applied"), false));
    TestEqual(TEXT("StateTree lifecycle Patch leaves two children"), Tree.Root->Children.Num(), 2);
    if (Tree.Root->Children.Num() == 2)
    {
        TestEqual(TEXT("Added State occupies the authored first position"), Tree.Root->Children[0]->Name, FName(TEXT("AddedBySAL")));
        TestEqual(TEXT("Moved State follows the newly added anchor"), Tree.Root->Children[1]->ID, MovedId);
        TestTrue(TEXT("Added State keeps its exact native parent"), Tree.Root->Children[0]->Parent == Tree.Root);
    }
    TestFalse(
        TEXT("Removed State is absent from the authored collection"),
        Tree.Root->Children.ContainsByPredicate(
            [&](const TObjectPtr<UStateTreeState>& State)
            {
                return State != nullptr && State->ID == RemovedId;
            }));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FSalStateTreePatchBindingTest,
    "Loomle.Sal.StateTree.Patch.Binding",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSalStateTreePatchBindingTest::RunTest(const FString& Parameters)
{
    const FDiscoveryStateTree Tree = MakeDiscoveryStateTree();
    const FSalResolvedTarget Target = ResolvedTarget(Tree);
    AddTestSchema(
        Tree,
        FGuid(0xB4000001, 0xB4000002, 0xB4000003, 0xB4000004),
        true);
    const FGuid ParameterGuid(0xB5000001, 0xB5000002, 0xB5000003, 0xB5000004);
    AddRootIntParameter(Tree, ParameterGuid, TEXT("SourceValue"));
    const FGuid ConsumerGuid(0xB6000001, 0xB6000002, 0xB6000003, 0xB6000004);
    AddBindingTask(Tree.First->Tasks, ConsumerGuid, TEXT("Consumer"));
    UStateTreeEditingSubsystem::ValidateStateTree(Tree.Asset);

    const TSharedPtr<FJsonObject> Source = StableRef(
        TEXT("parameter"),
        ParameterId(Tree.EditorData->GetRootParametersGuid(), ParameterGuid));
    const TSharedPtr<FJsonObject> Destination = MemberRef(
        StableRef(TEXT("node"), GuidText(ConsumerGuid)),
        {TEXT("Instance"), TEXT("InputValue")});

    FSalPatch BindPatch = Patch(false);
    TSharedPtr<FJsonObject> Bind = Operation(TEXT("bind"));
    Bind->SetObjectField(TEXT("from"), Source);
    Bind->SetObjectField(TEXT("to"), Destination);
    AddStatement(BindPatch, Bind);
    const TSharedPtr<FJsonObject> BindResult = FSalStateTreeInterface::Patch(BindPatch, Target);
    TestFalse(TEXT("StateTree bind succeeds"), HasError(BindResult));
    TestTrue(TEXT("StateTree bind reports applied"), ResultBool(BindResult, TEXT("applied"), false));
    const TConstArrayView<FStateTreePropertyPathBinding> Bound = Tree.EditorData->EditorBindings.GetBindings();
    TestEqual(TEXT("StateTree bind authors exactly one native Binding"), Bound.Num(), 1);
    if (Bound.Num() == 1)
    {
        TestEqual(
            TEXT("StateTree bind preserves the source Parameter container identity"),
            Bound[0].GetSourcePath().GetStructID(),
            Tree.EditorData->GetRootParametersGuid());
        TestEqual(
            TEXT("StateTree bind preserves the target Node identity"),
            Bound[0].GetTargetPath().GetStructID(),
            ConsumerGuid);
    }

    FSalPatch UnbindPatch = Patch(false);
    TSharedPtr<FJsonObject> Unbind = Operation(TEXT("unbind"));
    Unbind->SetObjectField(TEXT("from"), Source);
    Unbind->SetObjectField(TEXT("to"), Destination);
    AddStatement(UnbindPatch, Unbind);
    const TSharedPtr<FJsonObject> UnbindResult = FSalStateTreeInterface::Patch(UnbindPatch, Target);
    TestFalse(TEXT("StateTree unbind succeeds"), HasError(UnbindResult));
    TestTrue(TEXT("StateTree unbind reports applied"), ResultBool(UnbindResult, TEXT("applied"), false));
    TestEqual(
        TEXT("StateTree unbind removes the exact authored pair"),
        Tree.EditorData->EditorBindings.GetBindings().Num(),
        0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FSalStateTreePatchCompileTerminalTest,
    "Loomle.Sal.StateTree.Patch.CompileTerminal",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSalStateTreePatchCompileTerminalTest::RunTest(const FString& Parameters)
{
    const FDiscoveryStateTree Tree = MakeDiscoveryStateTree();
    const FSalResolvedTarget Target = ResolvedTarget(Tree);
    AddTestSchema(Tree, FGuid(0xB7000001, 0xB7000002, 0xB7000003, 0xB7000004));
    UStateTreeEditingSubsystem::ValidateStateTree(Tree.Asset);

    FSalPatch Compile = Patch(false);
    AddStatement(Compile, Operation(TEXT("compile")));
    const TSharedPtr<FJsonObject> CompileResult = FSalStateTreeInterface::Patch(Compile, Target);
    TestFalse(TEXT("Independent StateTree compile executes"), HasError(CompileResult));
    TestTrue(TEXT("Independent StateTree compile reports applied"), ResultBool(CompileResult, TEXT("applied"), false));

    const uint32 CompiledHash = Tree.Asset->LastCompiledEditorDataHash;
    const bool bWasDirty = Tree.Asset->GetOutermost()->IsDirty();
    FSalPatch CompileAndSaveDryRun = Patch(true);
    AddStatement(CompileAndSaveDryRun, Operation(TEXT("compile")));
    AddStatement(CompileAndSaveDryRun, Operation(TEXT("save")));
    const TSharedPtr<FJsonObject> DryTerminalResult = FSalStateTreeInterface::Patch(
        CompileAndSaveDryRun,
        Target);
    TestFalse(TEXT("compile followed by save is a valid terminal sequence"), HasError(DryTerminalResult));
    TestTrue(TEXT("compile+save dry-run reports dryRun"), ResultBool(DryTerminalResult, TEXT("dryRun"), false));
    TestFalse(TEXT("compile+save dry-run performs no external apply"), ResultBool(DryTerminalResult, TEXT("applied"), true));
    TestEqual(TEXT("compile+save dry-run preserves live compiled hash"), Tree.Asset->LastCompiledEditorDataHash, CompiledHash);
    TestEqual(TEXT("compile+save dry-run preserves live dirty state"), Tree.Asset->GetOutermost()->IsDirty(), bWasDirty);

    const FName OriginalName = Tree.First->Name;
    FSalPatch MixedTerminal = Patch(false);
    TSharedPtr<FJsonObject> MixedSet = Operation(TEXT("set"));
    MixedSet->SetObjectField(
        TEXT("target"),
        MemberRef(StableRef(TEXT("state"), GuidText(Tree.First->ID)), {TEXT("Name")}));
    MixedSet->SetStringField(TEXT("value"), TEXT("MustNotApply"));
    AddStatement(MixedTerminal, MixedSet);
    AddStatement(MixedTerminal, Operation(TEXT("compile")));
    const TSharedPtr<FJsonObject> MixedResult = FSalStateTreeInterface::Patch(MixedTerminal, Target);
    TestTrue(TEXT("Source edits cannot share a StateTree terminal Patch"), HasError(MixedResult));
    TestTrue(
        TEXT("Mixed source and terminal Patch explains the independence rule"),
        HasDiagnosticCode(MixedResult, TEXT("validation.finalization_must_be_independent")));
    TestEqual(TEXT("Rejected mixed terminal Patch preserves authored state"), Tree.First->Name, OriginalName);

    FSalPatch ReversedTerminal = Patch(false);
    AddStatement(ReversedTerminal, Operation(TEXT("save")));
    AddStatement(ReversedTerminal, Operation(TEXT("compile")));
    const TSharedPtr<FJsonObject> ReversedResult = FSalStateTreeInterface::Patch(ReversedTerminal, Target);
    TestTrue(TEXT("save followed by compile is rejected"), HasError(ReversedResult));
    TestTrue(
        TEXT("Reversed terminal sequence uses the shared independence diagnostic"),
        HasDiagnosticCode(ReversedResult, TEXT("validation.finalization_must_be_independent")));
    return true;
}

} // namespace LoomleStateTreeDiscoveryTests

#endif
