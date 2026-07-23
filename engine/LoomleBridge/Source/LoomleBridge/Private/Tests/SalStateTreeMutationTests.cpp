// Copyright 2026 Loomle contributors.

#if WITH_DEV_AUTOMATION_TESTS

#include "Sal/SalModel.h"
#include "Sal/StateTree/SalStateTreeInterface.h"
#include "SalStateTreeTestSchema.h"

#include "Conditions/StateTreeCommonConditions.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Editor.h"
#include "Misc/AutomationTest.h"
#include "Serialization/JsonSerializer.h"
#include "StateTree.h"
#include "StateTreeEditingSubsystem.h"
#include "StateTreeEditorData.h"
#include "StateTreeEditorNode.h"
#include "StateTreeEditorPropertyBindings.h"
#include "StateTreeState.h"
#include "StructUtils/PropertyBag.h"
#include "UObject/Package.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/UObjectIterator.h"

namespace
{
using namespace Loomle::Sal;

struct FSalMutationFixture
{
    UStateTree* Asset = nullptr;
    UStateTreeEditorData* Data = nullptr;
    USalStateTreeTestSchema* Schema = nullptr;
    UStateTreeState* Root = nullptr;
    UStateTreeState* First = nullptr;
    UStateTreeState* Second = nullptr;
};

FString SalMutationGuid(const FGuid& Guid)
{
    return Guid.ToString(EGuidFormats::DigitsWithHyphensLower);
}

FString SalMutationParameterId(const FGuid& Container, const FGuid& Property)
{
    return SalMutationGuid(Container) + TEXT("/") + SalMutationGuid(Property);
}

FSalMutationFixture MakeSalMutationFixture()
{
    FSalMutationFixture Result;
    UPackage* Package = CreatePackage(*FString::Printf(
        TEXT("/LoomleTests/StateTreeMutation_%s"),
        *FGuid::NewGuid().ToString(EGuidFormats::Digits)));
    Result.Asset = NewObject<UStateTree>(Package, NAME_None, RF_Transient | RF_Transactional);
    Result.Data = NewObject<UStateTreeEditorData>(
        Result.Asset,
        NAME_None,
        RF_Transient | RF_Transactional);
    Result.Asset->EditorData = Result.Data;
    Result.Schema = NewObject<USalStateTreeTestSchema>(Result.Data);
    Result.Schema->bAllowBindingTask = true;
    Result.Data->Schema = Result.Schema;
    Result.Root = &Result.Data->AddSubTree(FName(TEXT("Root")));
    Result.Root->ID = FGuid(0xA1000001, 0xA1000002, 0xA1000003, 0xA1000004);
    Result.First = &Result.Root->AddChildState(FName(TEXT("First")));
    Result.First->ID = FGuid(0xA2000001, 0xA2000002, 0xA2000003, 0xA2000004);
    Result.Second = &Result.Root->AddChildState(FName(TEXT("Second")));
    Result.Second->ID = FGuid(0xA3000001, 0xA3000002, 0xA3000003, 0xA3000004);
    return Result;
}

void FinalizeSalMutationFixture(const FSalMutationFixture& Fixture)
{
    UStateTreeEditingSubsystem::ValidateStateTree(Fixture.Asset);
    Fixture.Asset->GetOutermost()->SetDirtyFlag(false);
}

FSalResolvedTarget SalMutationTarget(const FSalMutationFixture& Fixture)
{
    FSalResolvedTarget Target;
    Target.Kind = ESalTargetKind::Asset;
    Target.Alias = TEXT("tree");
    Target.AssetPath = Fixture.Asset->GetPathName();
    Target.Object = Fixture.Asset;
    Target.Package = Fixture.Asset->GetOutermost();
    Target.Interfaces = {FName(TEXT("asset")), FName(TEXT("state_tree"))};
    return Target;
}

TSharedRef<FJsonObject> SalMutationStable(const FString& Kind, const FString& Id)
{
    TSharedRef<FJsonObject> Ref = MakeShared<FJsonObject>();
    Ref->SetStringField(TEXT("kind"), Kind);
    Ref->SetStringField(TEXT("id"), Id);
    return Ref;
}

TSharedRef<FJsonObject> SalMutationLocal(const FString& Name)
{
    TSharedRef<FJsonObject> Ref = MakeShared<FJsonObject>();
    Ref->SetStringField(TEXT("kind"), TEXT("local"));
    Ref->SetStringField(TEXT("name"), Name);
    return Ref;
}

TSharedRef<FJsonObject> SalMutationMember(
    const TSharedRef<FJsonObject>& Owner,
    const std::initializer_list<const TCHAR*> Path)
{
    TSharedRef<FJsonObject> Ref = MakeShared<FJsonObject>();
    Ref->SetStringField(TEXT("kind"), TEXT("member"));
    Ref->SetObjectField(TEXT("object"), Owner);
    TArray<TSharedPtr<FJsonValue>> Segments;
    for (const TCHAR* Segment : Path)
    {
        Segments.Add(MakeShared<FJsonValueString>(Segment));
    }
    Ref->SetArrayField(TEXT("path"), MoveTemp(Segments));
    return Ref;
}

TSharedRef<FJsonObject> SalMutationName(const FString& Name)
{
    TSharedRef<FJsonObject> Value = MakeShared<FJsonObject>();
    Value->SetStringField(TEXT("kind"), TEXT("name"));
    Value->SetStringField(TEXT("name"), Name);
    return Value;
}

TSharedRef<FJsonObject> SalMutationKind(const FString& Kind)
{
    TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetStringField(TEXT("kind"), Kind);
    return Result;
}

TSharedRef<FJsonObject> SalMutationCall(
    const FString& Callee,
    const TSharedRef<FJsonObject>& Args)
{
    TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetStringField(TEXT("kind"), TEXT("call"));
    Result->SetStringField(TEXT("callee"), Callee);
    Result->SetObjectField(TEXT("args"), Args);
    return Result;
}

FSalPatch SalMutationPatch(
    TArray<TSharedPtr<FJsonValue>> Statements,
    const bool bDryRun = false)
{
    FSalPatch Patch;
    Patch.Alias = TEXT("tree");
    Patch.bDryRun = bDryRun;
    Patch.Statements = MoveTemp(Statements);
    return Patch;
}

bool SalMutationBool(
    const TSharedPtr<FJsonObject>& Result,
    const FString& Field,
    const bool Default = false)
{
    bool Value = Default;
    return Result.IsValid() && Result->TryGetBoolField(Field, Value) ? Value : Default;
}

bool SalMutationHasCode(
    const TSharedPtr<FJsonObject>& Result,
    const FString& Expected)
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
            && Code == Expected)
        {
            return true;
        }
    }
    return false;
}

FString SalMutationDiagnosticSummary(const TSharedPtr<FJsonObject>& Result)
{
    const TArray<TSharedPtr<FJsonValue>>* Diagnostics = nullptr;
    if (!Result.IsValid()
        || !Result->TryGetArrayField(TEXT("diagnostics"), Diagnostics)
        || Diagnostics == nullptr)
    {
        return TEXT("<no diagnostics>");
    }
    TArray<FString> Summaries;
    for (const TSharedPtr<FJsonValue>& Value : *Diagnostics)
    {
        const TSharedPtr<FJsonObject>* Diagnostic = nullptr;
        if (!Value.IsValid()
            || !Value->TryGetObject(Diagnostic)
            || Diagnostic == nullptr)
        {
            continue;
        }
        FString Code;
        FString Message;
        (*Diagnostic)->TryGetStringField(TEXT("code"), Code);
        (*Diagnostic)->TryGetStringField(TEXT("message"), Message);
        Summaries.Add(Code + TEXT(": ") + Message);
    }
    return Summaries.IsEmpty()
        ? TEXT("<empty diagnostics>")
        : FString::Join(Summaries, TEXT(" | "));
}

bool SalMutationHasPlannedEffect(
    const TSharedPtr<FJsonObject>& Result,
    const FString& Expected)
{
    const TSharedPtr<FJsonObject>* Planned = nullptr;
    const TArray<TSharedPtr<FJsonValue>>* Effects = nullptr;
    if (!Result.IsValid()
        || !Result->TryGetObjectField(TEXT("planned"), Planned)
        || Planned == nullptr
        || !(*Planned)->TryGetArrayField(TEXT("effects"), Effects)
        || Effects == nullptr)
    {
        return false;
    }
    for (const TSharedPtr<FJsonValue>& Effect : *Effects)
    {
        FString Text;
        if (Effect.IsValid() && Effect->TryGetString(Text) && Text.Contains(Expected))
        {
            return true;
        }
    }
    return false;
}

bool SalMutationHasPlannedEffectContainingBoth(
    const TSharedPtr<FJsonObject>& Result,
    const FString& First,
    const FString& Second)
{
    const TSharedPtr<FJsonObject>* Planned = nullptr;
    const TArray<TSharedPtr<FJsonValue>>* Effects = nullptr;
    if (!Result.IsValid()
        || !Result->TryGetObjectField(TEXT("planned"), Planned)
        || Planned == nullptr
        || !(*Planned)->TryGetArrayField(TEXT("effects"), Effects)
        || Effects == nullptr)
    {
        return false;
    }
    for (const TSharedPtr<FJsonValue>& Effect : *Effects)
    {
        FString Text;
        if (Effect.IsValid()
            && Effect->TryGetString(Text)
            && Text.Contains(First)
            && Text.Contains(Second))
        {
            return true;
        }
    }
    return false;
}

int32 SalMutationPlannedOperationCount(const TSharedPtr<FJsonObject>& Result)
{
    const TSharedPtr<FJsonObject>* Planned = nullptr;
    const TArray<TSharedPtr<FJsonValue>>* Operations = nullptr;
    return Result.IsValid()
        && Result->TryGetObjectField(TEXT("planned"), Planned)
        && Planned != nullptr
        && (*Planned)->TryGetArrayField(TEXT("operations"), Operations)
        && Operations != nullptr
        ? Operations->Num()
        : INDEX_NONE;
}

FString SalMutationPlannedJson(const TSharedPtr<FJsonObject>& Result)
{
    const TSharedPtr<FJsonObject>* Planned = nullptr;
    if (!Result.IsValid()
        || !Result->TryGetObjectField(TEXT("planned"), Planned)
        || Planned == nullptr
        || !(*Planned).IsValid())
    {
        return FString();
    }
    FString Serialized;
    const TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
        TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Serialized);
    return FJsonSerializer::Serialize((*Planned).ToSharedRef(), Writer)
        ? Serialized
        : FString();
}

FStateTreeEditorNode& AddSalMutationTask(
    UStateTreeState& State,
    const FGuid& Id,
    const FName Name)
{
    FStateTreeEditorNode& Node = State.Tasks.AddDefaulted_GetRef();
    Node.ID = Id;
    Node.Node.InitializeAs<FSalStateTreeBindingTask>();
    Node.Node.GetMutable<FSalStateTreeBindingTask>().Name = Name;
    Node.Instance.InitializeAs<FSalStateTreeBindingTaskInstanceData>();
    return Node;
}

FStateTreeEditorNode& AddSalMutationCascadeTask(
    UStateTreeState& State,
    const FGuid& Id)
{
    FStateTreeEditorNode& Node = State.Tasks.AddDefaulted_GetRef();
    Node.ID = Id;
    Node.Node.InitializeAs<FSalStateTreePostEditCascadeTask>();
    Node.Instance.InitializeAs<FSalStateTreePostEditCascadeTaskInstanceData>();
    return Node;
}

FPropertyBagPropertyDesc SalMutationIntParameter(const FName Name, const FGuid& Id)
{
    FPropertyBagPropertyDesc Desc(Name, EPropertyBagPropertyType::Int32);
    Desc.ID = Id;
    Desc.PropertyFlags = static_cast<uint64>(CPF_Edit | CPF_BlueprintVisible);
    return Desc;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FSalStateTreeMutationTransactionTest,
    "Loomle.Sal.StateTree.Mutation.TransactionAndDryRun",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSalStateTreeMutationTransactionTest::RunTest(const FString& Parameters)
{
    if (GEditor == nullptr)
    {
        AddError(TEXT("StateTree mutation tests require GEditor."));
        return false;
    }
    const FSalMutationFixture Fixture = MakeSalMutationFixture();
    FinalizeSalMutationFixture(Fixture);
    const FSalResolvedTarget Target = SalMutationTarget(Fixture);

    const auto RenameStatement = [&]()
    {
        TSharedRef<FJsonObject> Set = SalMutationKind(TEXT("set"));
        Set->SetObjectField(
            TEXT("target"),
            SalMutationMember(
                SalMutationStable(TEXT("state"), SalMutationGuid(Fixture.First->ID)),
                {TEXT("Name")}));
        Set->SetObjectField(TEXT("value"), SalMutationName(TEXT("Renamed")));
        return MakeShared<FJsonValueObject>(Set);
    };

    const TSharedPtr<FJsonObject> DryRun = FSalStateTreeInterface::Patch(
        SalMutationPatch({RenameStatement()}, true),
        Target);
    TestTrue(TEXT("Dry run validates"), SalMutationBool(DryRun, TEXT("valid")));
    TestFalse(TEXT("Dry run never applies"), SalMutationBool(DryRun, TEXT("applied")));
    TestEqual(TEXT("Dry run preserves live State name"), Fixture.First->Name, FName(TEXT("First")));

    const TSharedPtr<FJsonObject> Applied = FSalStateTreeInterface::Patch(
        SalMutationPatch({RenameStatement()}),
        Target);
    TestTrue(TEXT("Live rename validates"), SalMutationBool(Applied, TEXT("valid")));
    TestTrue(TEXT("Live rename applies"), SalMutationBool(Applied, TEXT("applied")));
    TestEqual(TEXT("Live rename changes exact State"), Fixture.First->Name, FName(TEXT("Renamed")));
    TestTrue(TEXT("One Patch is one undo step"), GEditor->UndoTransaction(false));
    TestEqual(TEXT("Undo restores State name"), Fixture.First->Name, FName(TEXT("First")));

    TSharedRef<FJsonObject> MissingRemove = SalMutationKind(TEXT("remove"));
    MissingRemove->SetObjectField(
        TEXT("target"),
        SalMutationStable(TEXT("state"), SalMutationGuid(FGuid::NewGuid())));
    const TSharedPtr<FJsonObject> Invalid = FSalStateTreeInterface::Patch(
        SalMutationPatch({RenameStatement(), MakeShared<FJsonValueObject>(MissingRemove)}),
        Target);
    TestFalse(TEXT("Invalid ordered Patch fails preflight"), SalMutationBool(Invalid, TEXT("valid"), true));
    TestFalse(TEXT("Invalid ordered Patch never applies"), SalMutationBool(Invalid, TEXT("applied"), true));
    TestEqual(TEXT("Failed preflight leaves live State unchanged"), Fixture.First->Name, FName(TEXT("First")));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FSalStateTreeMutationLifecycleTest,
    "Loomle.Sal.StateTree.Mutation.Lifecycle",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSalStateTreeMutationLifecycleTest::RunTest(const FString& Parameters)
{
    if (GEditor == nullptr)
    {
        AddError(TEXT("StateTree mutation tests require GEditor."));
        return false;
    }
    {
        const FSalMutationFixture Fixture = MakeSalMutationFixture();
        FinalizeSalMutationFixture(Fixture);
        TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
        Args->SetStringField(TEXT("palette"), TEXT("state_tree.parameter"));
        Args->SetObjectField(TEXT("Name"), SalMutationName(TEXT("CreatedCount")));
        Args->SetStringField(TEXT("type"), TEXT("IntProperty"));
        TSharedRef<FJsonObject> Definition = MakeShared<FJsonObject>();
        Definition->SetObjectField(TEXT("target"), SalMutationLocal(TEXT("created")));
        Definition->SetObjectField(TEXT("value"), SalMutationCall(TEXT("parameter"), Args));
        TSharedRef<FJsonObject> Add = SalMutationKind(TEXT("add"));
        Add->SetObjectField(TEXT("target"), SalMutationLocal(TEXT("created")));
        Add->SetObjectField(
            TEXT("to"),
            SalMutationMember(SalMutationLocal(TEXT("tree")), {TEXT("RootParameters")}));
        const TSharedPtr<FJsonObject> Result = FSalStateTreeInterface::Patch(
            SalMutationPatch({
                MakeShared<FJsonValueObject>(Definition),
                MakeShared<FJsonValueObject>(Add),
            }),
            SalMutationTarget(Fixture));
        TestTrue(TEXT("Palette Parameter add validates"), SalMutationBool(Result, TEXT("valid")));
        TestTrue(TEXT("Palette Parameter add applies"), SalMutationBool(Result, TEXT("applied")));
        TestNotNull(
            TEXT("Palette Parameter materializes native descriptor"),
            Fixture.Data->GetRootParametersPropertyBag().FindPropertyDescByName(FName(TEXT("CreatedCount"))));
        TestTrue(TEXT("Parameter add is undoable"), GEditor->UndoTransaction(false));
    }
    {
        const FSalMutationFixture Fixture = MakeSalMutationFixture();
        FinalizeSalMutationFixture(Fixture);
        TSharedRef<FJsonObject> Move = SalMutationKind(TEXT("move"));
        Move->SetObjectField(
            TEXT("target"),
            SalMutationStable(TEXT("state"), SalMutationGuid(Fixture.Second->ID)));
        Move->SetObjectField(
            TEXT("before"),
            SalMutationStable(TEXT("state"), SalMutationGuid(Fixture.First->ID)));
        const TSharedPtr<FJsonObject> Result = FSalStateTreeInterface::Patch(
            SalMutationPatch({MakeShared<FJsonValueObject>(Move)}),
            SalMutationTarget(Fixture));
        TestTrue(TEXT("State move validates"), SalMutationBool(Result, TEXT("valid")));
        TestEqual(TEXT("State move changes native order"), Fixture.Root->Children[0].Get(), Fixture.Second);
        TestTrue(TEXT("State move is undoable"), GEditor->UndoTransaction(false));
        TestEqual(TEXT("Undo restores State order"), Fixture.Root->Children[0].Get(), Fixture.First);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FSalStateTreeMutationStateConstructorCapabilityTest,
    "Loomle.Sal.StateTree.Mutation.StateConstructorCapability",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSalStateTreeMutationStateConstructorCapabilityTest::RunTest(const FString& Parameters)
{
    if (GEditor == nullptr)
    {
        AddError(TEXT("StateTree mutation tests require GEditor."));
        return false;
    }
    const auto AddState = [](
        const FSalMutationFixture& Fixture,
        const FString& Palette,
        const FString& Type,
        const FGuid* LinkedSubtree = nullptr,
        const FString* SelectionBehavior = nullptr)
    {
        TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
        Args->SetStringField(TEXT("palette"), Palette);
        Args->SetStringField(TEXT("Type"), Type);
        if (SelectionBehavior != nullptr)
        {
            Args->SetStringField(TEXT("SelectionBehavior"), *SelectionBehavior);
        }
        if (LinkedSubtree != nullptr)
        {
            Args->SetObjectField(
                TEXT("LinkedSubtree"),
                SalMutationStable(TEXT("state"), SalMutationGuid(*LinkedSubtree)));
        }
        TSharedRef<FJsonObject> Definition = MakeShared<FJsonObject>();
        Definition->SetObjectField(TEXT("target"), SalMutationLocal(TEXT("created")));
        Definition->SetObjectField(TEXT("value"), SalMutationCall(TEXT("state"), Args));
        TSharedRef<FJsonObject> Add = SalMutationKind(TEXT("add"));
        Add->SetObjectField(TEXT("target"), SalMutationLocal(TEXT("created")));
        Add->SetObjectField(
            TEXT("to"),
            SalMutationMember(SalMutationLocal(TEXT("tree")), {TEXT("SubTrees")}));
        return FSalStateTreeInterface::Patch(
            SalMutationPatch({
                MakeShared<FJsonValueObject>(Definition),
                MakeShared<FJsonValueObject>(Add),
            }),
            SalMutationTarget(Fixture));
    };

    {
        const FSalMutationFixture Fixture = MakeSalMutationFixture();
        FinalizeSalMutationFixture(Fixture);
        const TSharedPtr<FJsonObject> Added = AddState(
            Fixture,
            TEXT("state_tree.state"),
            TEXT("Subtree"));
        TestTrue(TEXT("Ordinary State Palette seed can create another allowed native Type"), SalMutationBool(Added, TEXT("valid")));
        TestEqual(TEXT("Ordinary State constructor adds one root State"), Fixture.Data->SubTrees.Num(), 2);
        TestTrue(
            TEXT("Ordinary State constructor preserves requested Subtree Type"),
            Fixture.Data->SubTrees.Last() != nullptr
                && Fixture.Data->SubTrees.Last()->Type == EStateTreeStateType::Subtree);
        TestEqual(TEXT("State constructor remains one logical add"), SalMutationPlannedOperationCount(Added), 1);
    }
    {
        const FSalMutationFixture Fixture = MakeSalMutationFixture();
        Fixture.Second->Type = EStateTreeStateType::Subtree;
        FinalizeSalMutationFixture(Fixture);
        const TSharedPtr<FJsonObject> Rejected = AddState(
            Fixture,
            TEXT("state_tree.state"),
            TEXT("Linked"),
            &Fixture.Second->ID);
        TestFalse(TEXT("Ordinary State Palette entry cannot synthesize Linked"), SalMutationBool(Rejected, TEXT("valid"), true));
        TestTrue(
            TEXT("Ordinary Linked request uses creation diagnostic"),
            SalMutationHasCode(Rejected, TEXT("validation.creation_invalid")));

        const TSharedPtr<FJsonObject> RejectedLinkedAsset = AddState(
            Fixture,
            TEXT("state_tree.state"),
            TEXT("LinkedAsset"));
        TestFalse(
            TEXT("Ordinary State Palette entry cannot synthesize LinkedAsset"),
            SalMutationBool(RejectedLinkedAsset, TEXT("valid"), true));
        TestTrue(
            TEXT("Ordinary LinkedAsset request uses creation diagnostic"),
            SalMutationHasCode(
                RejectedLinkedAsset,
                TEXT("validation.creation_invalid")));
    }
    {
        const FSalMutationFixture Fixture = MakeSalMutationFixture();
        Fixture.Second->Type = EStateTreeStateType::Subtree;
        FinalizeSalMutationFixture(Fixture);
        const FString Palette = TEXT("state_tree.state.linked.")
            + SalMutationGuid(Fixture.Second->ID);
        const TSharedPtr<FJsonObject> WrongType = AddState(
            Fixture,
            Palette,
            TEXT("Subtree"),
            &Fixture.Second->ID);
        TestFalse(TEXT("Linked State Palette entry locks native Type"), SalMutationBool(WrongType, TEXT("valid"), true));

        const TSharedPtr<FJsonObject> WrongTarget = AddState(
            Fixture,
            Palette,
            TEXT("Linked"),
            &Fixture.First->ID);
        TestFalse(TEXT("Linked State Palette entry locks exact LinkedSubtree"), SalMutationBool(WrongTarget, TEXT("valid"), true));

        const TSharedPtr<FJsonObject> MissingTarget = AddState(
            Fixture,
            Palette,
            TEXT("Linked"));
        TestFalse(TEXT("Linked State Palette entry requires LinkedSubtree"), SalMutationBool(MissingTarget, TEXT("valid"), true));
    }
    {
        const FSalMutationFixture Fixture = MakeSalMutationFixture();
        FinalizeSalMutationFixture(Fixture);
        const TSharedPtr<FJsonObject> WrongType = AddState(
            Fixture,
            TEXT("state_tree.state.linked_asset"),
            TEXT("State"));
        TestFalse(
            TEXT("LinkedAsset State Palette entry locks native Type"),
            SalMutationBool(WrongType, TEXT("valid"), true));
        TestTrue(
            TEXT("LinkedAsset wrong Type uses creation diagnostic"),
            SalMutationHasCode(WrongType, TEXT("validation.creation_invalid")));
    }
    {
        const FSalMutationFixture Fixture = MakeSalMutationFixture();
        FinalizeSalMutationFixture(Fixture);
        const TSharedPtr<FJsonObject> Added = AddState(
            Fixture,
            TEXT("state_tree.state.linked_asset"),
            TEXT("LinkedAsset"));
        TestTrue(
            TEXT("LinkedAsset State constructor succeeds without inventing an asset or SelectionBehavior"),
            SalMutationBool(Added, TEXT("valid")));
        TestEqual(
            TEXT("LinkedAsset constructor adds one root State"),
            Fixture.Data->SubTrees.Num(),
            2);
        TestTrue(
            TEXT("LinkedAsset constructor preserves its fixed native Type and UE selection default"),
            Fixture.Data->SubTrees.Last() != nullptr
                && Fixture.Data->SubTrees.Last()->Type == EStateTreeStateType::LinkedAsset
                && Fixture.Data->SubTrees.Last()->SelectionBehavior
                    == EStateTreeStateSelectionBehavior::TryEnterState);
        TestEqual(
            TEXT("LinkedAsset constructor remains one logical add"),
            SalMutationPlannedOperationCount(Added),
            1);
    }
    {
        const FSalMutationFixture Fixture = MakeSalMutationFixture();
        FinalizeSalMutationFixture(Fixture);
        const FString SelectionBehavior = TEXT("TrySelectChildrenInOrder");
        const TSharedPtr<FJsonObject> Rejected = AddState(
            Fixture,
            TEXT("state_tree.state.linked_asset"),
            TEXT("LinkedAsset"),
            nullptr,
            &SelectionBehavior);
        TestFalse(
            TEXT("LinkedAsset State constructor rejects a caller-supplied SelectionBehavior"),
            SalMutationBool(Rejected, TEXT("valid"), true));
        TestTrue(
            TEXT("LinkedAsset SelectionBehavior override uses creation diagnostic"),
            SalMutationHasCode(Rejected, TEXT("validation.creation_invalid")));
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FSalStateTreeMutationBindingTest,
    "Loomle.Sal.StateTree.Mutation.Binding",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSalStateTreeMutationBindingTest::RunTest(const FString& Parameters)
{
    if (GEditor == nullptr)
    {
        AddError(TEXT("StateTree mutation tests require GEditor."));
        return false;
    }
    const FSalMutationFixture Fixture = MakeSalMutationFixture();
    const FGuid ParameterId(0xB1000001, 0xB1000002, 0xB1000003, 0xB1000004);
    FInstancedPropertyBag& Bag = const_cast<FInstancedPropertyBag&>(
        Fixture.Data->GetRootParametersPropertyBag());
    Bag.AddProperties({SalMutationIntParameter(TEXT("Count"), ParameterId)});
    const FGuid NodeId(0xB2000001, 0xB2000002, 0xB2000003, 0xB2000004);
    AddSalMutationTask(*Fixture.Root, NodeId, TEXT("Consumer"));
    FinalizeSalMutationFixture(Fixture);
    const FSalResolvedTarget Target = SalMutationTarget(Fixture);
    const TSharedRef<FJsonObject> Source = SalMutationStable(
        TEXT("parameter"),
        SalMutationParameterId(Fixture.Data->GetRootParametersGuid(), ParameterId));
    const TSharedRef<FJsonObject> Consumer = SalMutationMember(
        SalMutationStable(TEXT("node"), SalMutationGuid(NodeId)),
        {TEXT("Instance"), TEXT("InputValue")});

    TSharedRef<FJsonObject> Bind = SalMutationKind(TEXT("bind"));
    Bind->SetObjectField(TEXT("from"), Source);
    Bind->SetObjectField(TEXT("to"), Consumer);
    const TSharedPtr<FJsonObject> Bound = FSalStateTreeInterface::Patch(
        SalMutationPatch({MakeShared<FJsonValueObject>(Bind)}),
        Target);
    TestTrue(TEXT("Ordinary Parameter Binding validates"), SalMutationBool(Bound, TEXT("valid")));
    TestEqual(
        TEXT("Ordinary Parameter Binding authors one native edge"),
        Fixture.Data->EditorBindings.GetBindings().Num(),
        1);

    TSharedRef<FJsonObject> Unbind = SalMutationKind(TEXT("unbind"));
    Unbind->SetObjectField(TEXT("from"), Source);
    Unbind->SetObjectField(TEXT("to"), Consumer);
    const TSharedPtr<FJsonObject> Unbound = FSalStateTreeInterface::Patch(
        SalMutationPatch({MakeShared<FJsonValueObject>(Unbind)}),
        Target);
    TestTrue(TEXT("Exact unbind validates"), SalMutationBool(Unbound, TEXT("valid")));
    TestEqual(
        TEXT("Exact unbind removes authored edge"),
        Fixture.Data->EditorBindings.GetBindings().Num(),
        0);

    TSharedRef<FJsonObject> InvalidOutput = SalMutationKind(TEXT("bind"));
    InvalidOutput->SetObjectField(
        TEXT("from"),
        SalMutationMember(
            SalMutationStable(TEXT("node"), SalMutationGuid(NodeId)),
            {TEXT("Instance"), TEXT("OutputValue")}));
    InvalidOutput->SetObjectField(TEXT("to"), Consumer);
    const TSharedPtr<FJsonObject> Rejected = FSalStateTreeInterface::Patch(
        SalMutationPatch({MakeShared<FJsonValueObject>(InvalidOutput)}),
        Target);
    TestFalse(TEXT("Output-to-Node Binding is rejected"), SalMutationBool(Rejected, TEXT("valid"), true));
    TestTrue(
        TEXT("Output restriction uses registered validation diagnostic"),
        SalMutationHasCode(Rejected, TEXT("validation.operation_arguments_invalid")));
    TestTrue(TEXT("Undo unbind restores authored Binding"), GEditor->UndoTransaction(false));
    TestTrue(TEXT("Undo bind removes authored Binding"), GEditor->UndoTransaction(false));
    TestEqual(TEXT("Binding test leaves no authored edge"), Fixture.Data->EditorBindings.GetBindings().Num(), 0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FSalStateTreeMutationBindingCallbackCascadeTest,
    "Loomle.Sal.StateTree.Mutation.BindingCallbackCascade",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSalStateTreeMutationBindingCallbackCascadeTest::RunTest(
    const FString& Parameters)
{
    if (GEditor == nullptr)
    {
        AddError(TEXT("StateTree mutation tests require GEditor."));
        return false;
    }
    const FSalMutationFixture Fixture = MakeSalMutationFixture();
    Fixture.Schema->bAllowCommonEnumCondition = true;
    const FGuid ParameterId(0xB3000001, 0xB3000002, 0xB3000003, 0xB3000004);
    FPropertyBagPropertyDesc EnumDesc(
        FName(TEXT("StateType")),
        EPropertyBagPropertyType::Enum,
        StaticEnum<EStateTreeStateType>());
    EnumDesc.ID = ParameterId;
    EnumDesc.PropertyFlags = static_cast<uint64>(CPF_Edit | CPF_BlueprintVisible);
    FInstancedPropertyBag& Bag = const_cast<FInstancedPropertyBag&>(
        Fixture.Data->GetRootParametersPropertyBag());
    Bag.AddProperties({EnumDesc});

    const FGuid ConditionId(0xB4000001, 0xB4000002, 0xB4000003, 0xB4000004);
    FStateTreeEditorNode& Condition =
        Fixture.First->EnterConditions.AddDefaulted_GetRef();
    Condition.ID = ConditionId;
    Condition.Node.InitializeAs<FSalStateTreeOptionalEnumCondition>();
    Condition.Instance.InitializeAs<FSalStateTreeOptionalEnumConditionInstanceData>();
    FinalizeSalMutationFixture(Fixture);

    FSalStateTreeOptionalEnumConditionInstanceData& Instance =
        Condition.Instance.GetMutable<FSalStateTreeOptionalEnumConditionInstanceData>();
    TestNull(
        TEXT("Enum Condition starts without a selected native enum"),
        Instance.Left.Enum.Get());

    const TSharedRef<FJsonObject> Source = SalMutationStable(
        TEXT("parameter"),
        SalMutationParameterId(
            Fixture.Data->GetRootParametersGuid(),
            ParameterId));
    const TSharedRef<FJsonObject> TargetMember = SalMutationMember(
        SalMutationStable(TEXT("node"), SalMutationGuid(ConditionId)),
        {TEXT("Instance"), TEXT("Left")});
    const auto MakeRelationship = [&](const FString& Kind)
    {
        TSharedRef<FJsonObject> Operation = SalMutationKind(Kind);
        Operation->SetObjectField(TEXT("from"), Source);
        Operation->SetObjectField(TEXT("to"), TargetMember);
        return Operation;
    };

    const TSharedPtr<FJsonObject> DryBind = FSalStateTreeInterface::Patch(
        SalMutationPatch(
            {MakeShared<FJsonValueObject>(MakeRelationship(TEXT("bind")))},
            true),
        SalMutationTarget(Fixture));
    TestTrue(
        TEXT("Dry-run Enum Condition Binding validates"),
        SalMutationBool(DryBind, TEXT("valid")));
    TestEqual(
        TEXT("Dry-run Binding does not author a live edge"),
        Fixture.Data->EditorBindings.GetBindings().Num(),
        0);
    TestNull(
        TEXT("Dry-run Binding does not mutate the live Condition Instance"),
        Instance.Left.Enum.Get());
    TestTrue(
        TEXT("Dry-run plan reports the native Binding callback cascade"),
        SalMutationHasPlannedEffect(
            DryBind,
            TEXT("native Binding callback updated node@")
                + SalMutationGuid(ConditionId)));
    TestFalse(
        TEXT("Dry-run callback diff excludes every explicit Binding effect"),
        SalMutationHasPlannedEffectContainingBoth(
            DryBind,
            TEXT("native Binding callback"),
            TEXT("EditorBindings")));

    const TSharedPtr<FJsonObject> Bound = FSalStateTreeInterface::Patch(
        SalMutationPatch(
            {MakeShared<FJsonValueObject>(MakeRelationship(TEXT("bind")))}),
        SalMutationTarget(Fixture));

    TestTrue(
        TEXT("Enum Condition Binding validates"),
        SalMutationBool(Bound, TEXT("valid")));
    TestEqual(
        TEXT("Native OnBindingChanged synchronizes the Condition Instance enum"),
        Instance.Left.Enum.Get(),
        StaticEnum<EStateTreeStateType>());
    TestTrue(
        TEXT("Plan reports the native Binding callback authored cascade"),
        SalMutationHasPlannedEffect(
            Bound,
            TEXT("native Binding callback updated node@")
                + SalMutationGuid(ConditionId)));
    TestFalse(
        TEXT("Callback diff excludes every explicit Binding effect"),
        SalMutationHasPlannedEffectContainingBoth(
            Bound,
            TEXT("native Binding callback"),
            TEXT("EditorBindings")));
    TestEqual(
        TEXT("Binding plus native callback remains one logical operation"),
        SalMutationPlannedOperationCount(Bound),
        1);

    const TSharedPtr<FJsonObject> DryUnbind = FSalStateTreeInterface::Patch(
        SalMutationPatch(
            {MakeShared<FJsonValueObject>(MakeRelationship(TEXT("unbind")))},
            true),
        SalMutationTarget(Fixture));
    TestTrue(
        TEXT("Dry-run Enum Condition unbind validates"),
        SalMutationBool(DryUnbind, TEXT("valid")));
    TestEqual(
        TEXT("Dry-run unbind preserves the live edge"),
        Fixture.Data->EditorBindings.GetBindings().Num(),
        1);
    TestEqual(
        TEXT("Dry-run unbind preserves the live Condition enum"),
        Instance.Left.Enum.Get(),
        StaticEnum<EStateTreeStateType>());
    TestTrue(
        TEXT("Dry-run unbind plans the native enum reset"),
        SalMutationHasPlannedEffect(
            DryUnbind,
            TEXT("native Binding callback updated node@")
                + SalMutationGuid(ConditionId)));

    const TSharedPtr<FJsonObject> Unbound = FSalStateTreeInterface::Patch(
        SalMutationPatch(
            {MakeShared<FJsonValueObject>(MakeRelationship(TEXT("unbind")))}),
        SalMutationTarget(Fixture));
    TestTrue(
        TEXT("Enum Condition unbind validates"),
        SalMutationBool(Unbound, TEXT("valid")));
    TestEqual(
        TEXT("Enum Condition unbind removes the authored edge"),
        Fixture.Data->EditorBindings.GetBindings().Num(),
        0);
    TestNull(
        TEXT("Native unbind callback clears the dependent Condition enum"),
        Instance.Left.Enum.Get());
    TestTrue(
        TEXT("Unbind plan reports the native enum reset"),
        SalMutationHasPlannedEffect(
            Unbound,
            TEXT("native Binding callback updated node@")
                + SalMutationGuid(ConditionId)));
    TestFalse(
        TEXT("Unbind callback diff excludes every explicit Binding effect"),
        SalMutationHasPlannedEffectContainingBoth(
            Unbound,
            TEXT("native Binding callback"),
            TEXT("EditorBindings")));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FSalStateTreeMutationStateSemanticCascadeTest,
    "Loomle.Sal.StateTree.Mutation.StateSemanticCascade",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSalStateTreeMutationStateSemanticCascadeTest::RunTest(const FString& Parameters)
{
    if (GEditor == nullptr)
    {
        AddError(TEXT("StateTree mutation tests require GEditor."));
        return false;
    }
    {
        const FSalMutationFixture Fixture = MakeSalMutationFixture();
        const FGuid RemovedTaskId(0xC1000001, 0xC1000002, 0xC1000003, 0xC1000004);
        const FGuid ConsumerId(0xC2000001, 0xC2000002, 0xC2000003, 0xC2000004);
        AddSalMutationTask(*Fixture.First, RemovedTaskId, TEXT("Removed"));
        AddSalMutationTask(*Fixture.Root, ConsumerId, TEXT("Consumer"));
        FinalizeSalMutationFixture(Fixture);
        Fixture.Data->EditorBindings.AddBinding(
            FPropertyBindingPath(RemovedTaskId, FName(TEXT("OutputValue"))),
            FPropertyBindingPath(ConsumerId, FName(TEXT("InputValue"))));

        TSharedRef<FJsonObject> SetType = SalMutationKind(TEXT("set"));
        SetType->SetObjectField(
            TEXT("target"),
            SalMutationMember(
                SalMutationStable(TEXT("state"), SalMutationGuid(Fixture.First->ID)),
                {TEXT("Type")}));
        SetType->SetStringField(TEXT("value"), TEXT("Linked"));
        const TSharedPtr<FJsonObject> Result = FSalStateTreeInterface::Patch(
            SalMutationPatch({MakeShared<FJsonValueObject>(SetType)}),
            SalMutationTarget(Fixture));
        TestTrue(TEXT("State Type semantic set validates"), SalMutationBool(Result, TEXT("valid")));
        TestEqual(TEXT("Native Type callback removes Tasks"), Fixture.First->Tasks.Num(), 0);
        TestEqual(TEXT("Native Type cascade removes invalid Binding"), Fixture.Data->EditorBindings.GetBindings().Num(), 0);
        TestTrue(
            TEXT("Plan names removed Task"),
            SalMutationHasPlannedEffect(Result, TEXT("node@") + SalMutationGuid(RemovedTaskId)));
        TestTrue(
            TEXT("Plan names removed Binding"),
            SalMutationHasPlannedEffect(Result, TEXT("removed native Binding source")));
    }
    {
        const FSalMutationFixture Fixture = MakeSalMutationFixture();
        Fixture.Second->Type = EStateTreeStateType::Subtree;
        Fixture.First->Type = EStateTreeStateType::Linked;
        Fixture.First->SetLinkedState(Fixture.Second->GetLinkToState());
        FStateTreeTransition& Transition = Fixture.Root->Transitions.AddDefaulted_GetRef();
        Transition.ID = FGuid(0xC3000001, 0xC3000002, 0xC3000003, 0xC3000004);
        Transition.State = Fixture.Second->GetLinkToState();
        FinalizeSalMutationFixture(Fixture);

        TSharedRef<FJsonObject> Rename = SalMutationKind(TEXT("set"));
        Rename->SetObjectField(
            TEXT("target"),
            SalMutationMember(
                SalMutationStable(TEXT("state"), SalMutationGuid(Fixture.Second->ID)),
                {TEXT("Name")}));
        Rename->SetObjectField(TEXT("value"), SalMutationName(TEXT("RenamedTarget")));
        const TSharedPtr<FJsonObject> Renamed = FSalStateTreeInterface::Patch(
            SalMutationPatch({MakeShared<FJsonValueObject>(Rename)}),
            SalMutationTarget(Fixture));
        TestTrue(TEXT("State rename validates"), SalMutationBool(Renamed, TEXT("valid")));
        TestEqual(TEXT("LinkedSubtree name follows rename"), Fixture.First->LinkedSubtree.Name, Fixture.Second->Name);
        TestEqual(TEXT("Transition State name follows rename"), Transition.State.Name, Fixture.Second->Name);

        TSharedRef<FJsonObject> ResetName = SalMutationKind(TEXT("reset"));
        ResetName->SetObjectField(
            TEXT("target"),
            SalMutationMember(
                SalMutationStable(TEXT("state"), SalMutationGuid(Fixture.Second->ID)),
                {TEXT("Name")}));
        const TSharedPtr<FJsonObject> Reset = FSalStateTreeInterface::Patch(
            SalMutationPatch({MakeShared<FJsonValueObject>(ResetName)}),
            SalMutationTarget(Fixture));
        TestTrue(TEXT("State Name reset validates"), SalMutationBool(Reset, TEXT("valid")));
        TestEqual(TEXT("LinkedSubtree name follows reset"), Fixture.First->LinkedSubtree.Name, Fixture.Second->Name);
        TestEqual(TEXT("Transition State name follows reset"), Transition.State.Name, Fixture.Second->Name);
        TestTrue(
            TEXT("Reset plan records link-name synchronization"),
            SalMutationHasPlannedEffect(Reset, TEXT(".LinkedSubtree.Name")));

        TSharedRef<FJsonObject> ResetLinked = SalMutationKind(TEXT("reset"));
        ResetLinked->SetObjectField(
            TEXT("target"),
            SalMutationMember(
                SalMutationStable(TEXT("state"), SalMutationGuid(Fixture.First->ID)),
                {TEXT("LinkedSubtree")}));
        const TSharedPtr<FJsonObject> LinkedReset = FSalStateTreeInterface::Patch(
            SalMutationPatch({MakeShared<FJsonValueObject>(ResetLinked)}),
            SalMutationTarget(Fixture));
        TestTrue(TEXT("LinkedSubtree reset validates"), SalMutationBool(LinkedReset, TEXT("valid")));
        TestFalse(TEXT("LinkedSubtree reset clears native link id"), Fixture.First->LinkedSubtree.ID.IsValid());

        TSharedRef<FJsonObject> NestedLinked = SalMutationKind(TEXT("set"));
        NestedLinked->SetObjectField(
            TEXT("target"),
            SalMutationMember(
                SalMutationStable(TEXT("state"), SalMutationGuid(Fixture.First->ID)),
                {TEXT("LinkedSubtree"), TEXT("Name")}));
        NestedLinked->SetObjectField(TEXT("value"), SalMutationName(TEXT("Bypass")));
        const TSharedPtr<FJsonObject> RejectedLinked = FSalStateTreeInterface::Patch(
            SalMutationPatch({MakeShared<FJsonValueObject>(NestedLinked)}),
            SalMutationTarget(Fixture));
        TestFalse(TEXT("Nested LinkedSubtree set is rejected"), SalMutationBool(RejectedLinked, TEXT("valid"), true));

        TSharedRef<FJsonObject> NestedTransition = SalMutationKind(TEXT("set"));
        NestedTransition->SetObjectField(
            TEXT("target"),
            SalMutationMember(
                SalMutationStable(TEXT("transition"), SalMutationGuid(Transition.ID)),
                {TEXT("State"), TEXT("Name")}));
        NestedTransition->SetObjectField(TEXT("value"), SalMutationName(TEXT("Bypass")));
        const TSharedPtr<FJsonObject> RejectedTransition = FSalStateTreeInterface::Patch(
            SalMutationPatch({MakeShared<FJsonValueObject>(NestedTransition)}),
            SalMutationTarget(Fixture));
        TestFalse(TEXT("Nested Transition.State set is rejected"), SalMutationBool(RejectedTransition, TEXT("valid"), true));
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FSalStateTreeMutationFixedParameterResetTest,
    "Loomle.Sal.StateTree.Mutation.FixedParameterReset",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSalStateTreeMutationFixedParameterResetTest::RunTest(const FString& Parameters)
{
    if (GEditor == nullptr)
    {
        AddError(TEXT("StateTree mutation tests require GEditor."));
        return false;
    }
    const FSalMutationFixture Fixture = MakeSalMutationFixture();
    const FGuid SourceId(0xC4000001, 0xC4000002, 0xC4000003, 0xC4000004);
    const FGuid LinkedId(0xC5000001, 0xC5000002, 0xC5000003, 0xC5000004);
    FInstancedPropertyBag& RootBag = const_cast<FInstancedPropertyBag&>(
        Fixture.Data->GetRootParametersPropertyBag());
    RootBag.AddProperties({SalMutationIntParameter(TEXT("SourceValue"), SourceId)});
    Fixture.Second->Type = EStateTreeStateType::Subtree;
    Fixture.Second->Parameters.Parameters.AddProperties({
        SalMutationIntParameter(TEXT("LinkedValue"), LinkedId)});
    Fixture.First->Type = EStateTreeStateType::Linked;
    Fixture.First->SetLinkedState(Fixture.Second->GetLinkToState());
    FinalizeSalMutationFixture(Fixture);
    Fixture.First->SetParametersPropertyOverridden(LinkedId, true);
    Fixture.Data->EditorBindings.AddBinding(
        FPropertyBindingPath(Fixture.Data->GetRootParametersGuid(), FName(TEXT("SourceValue"))),
        FPropertyBindingPath(Fixture.First->Parameters.ID, FName(TEXT("LinkedValue"))));

    TSharedRef<FJsonObject> Reset = SalMutationKind(TEXT("reset"));
    Reset->SetObjectField(
        TEXT("target"),
        SalMutationMember(
            SalMutationStable(
                TEXT("parameter"),
                SalMutationParameterId(Fixture.First->Parameters.ID, LinkedId)),
            {TEXT("Value")}));
    const TSharedPtr<FJsonObject> Result = FSalStateTreeInterface::Patch(
        SalMutationPatch({MakeShared<FJsonValueObject>(Reset)}),
        SalMutationTarget(Fixture));
    TestTrue(TEXT("Fixed-layout Parameter reset validates"), SalMutationBool(Result, TEXT("valid")));
    TestFalse(
        TEXT("Fixed-layout Parameter reset clears override"),
        Fixture.First->Parameters.PropertyOverrides.Contains(LinkedId));
    TestEqual(
        TEXT("UE reset removes exact override Binding"),
        Fixture.Data->EditorBindings.GetBindings().Num(),
        0);
    TestTrue(
        TEXT("Plan records UE-owned Binding deletion"),
        SalMutationHasPlannedEffect(Result, TEXT("removed native Binding source parameter@")));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FSalStateTreeMutationPostEditCascadeTest,
    "Loomle.Sal.StateTree.Mutation.PostEditCascade",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSalStateTreeMutationPostEditCascadeTest::RunTest(const FString& Parameters)
{
    if (GEditor == nullptr)
    {
        AddError(TEXT("StateTree mutation tests require GEditor."));
        return false;
    }
    const FSalMutationFixture Fixture = MakeSalMutationFixture();
    const FGuid NodeId(0xC6000001, 0xC6000002, 0xC6000003, 0xC6000004);
    FStateTreeEditorNode& Node = AddSalMutationCascadeTask(*Fixture.First, NodeId);
    FinalizeSalMutationFixture(Fixture);

    TSharedRef<FJsonObject> Set = SalMutationKind(TEXT("set"));
    Set->SetObjectField(
        TEXT("target"),
        SalMutationMember(
            SalMutationStable(TEXT("node"), SalMutationGuid(NodeId)),
            {TEXT("Instance"), TEXT("TriggerValue")}));
    Set->SetNumberField(TEXT("value"), 7);
    const TSharedPtr<FJsonObject> Result = FSalStateTreeInterface::Patch(
        SalMutationPatch({MakeShared<FJsonValueObject>(Set)}),
        SalMutationTarget(Fixture));
    TestTrue(TEXT("Node PostEdit set validates"), SalMutationBool(Result, TEXT("valid")));
    TestEqual(
        TEXT("Native Node callback synchronizes sibling authored field"),
        Node.Instance.Get<FSalStateTreePostEditCascadeTaskInstanceData>().SynchronizedValue,
        14);
    TestTrue(
        TEXT("Plan reports native Instance cascade"),
        SalMutationHasPlannedEffect(Result, TEXT("native PostEdit cascade updated node@") + SalMutationGuid(NodeId) + TEXT(".Instance")));
    TestEqual(
        TEXT("Direct set remains one logical operation"),
        SalMutationPlannedOperationCount(Result),
        1);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FSalStateTreeMutationTransitionNativeAddTest,
    "Loomle.Sal.StateTree.Mutation.TransitionNativeAdd",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSalStateTreeMutationTransitionNativeAddTest::RunTest(const FString& Parameters)
{
    if (GEditor == nullptr || GEditor->IsPlaySessionInProgress())
    {
        AddError(TEXT("Transition add test requires an available Editor outside PIE."));
        return false;
    }
    const FSalMutationFixture Fixture = MakeSalMutationFixture();
    FinalizeSalMutationFixture(Fixture);
    TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
    Args->SetStringField(TEXT("palette"), TEXT("state_tree.transition"));
    Args->SetStringField(TEXT("Priority"), TEXT("High"));
    TSharedRef<FJsonObject> Definition = MakeShared<FJsonObject>();
    Definition->SetObjectField(TEXT("target"), SalMutationLocal(TEXT("created")));
    Definition->SetObjectField(TEXT("value"), SalMutationCall(TEXT("transition"), Args));
    TSharedRef<FJsonObject> Add = SalMutationKind(TEXT("add"));
    Add->SetObjectField(TEXT("target"), SalMutationLocal(TEXT("created")));
    Add->SetObjectField(
        TEXT("to"),
        SalMutationMember(
            SalMutationStable(TEXT("state"), SalMutationGuid(Fixture.First->ID)),
            {TEXT("Transitions")}));
    const TSharedPtr<FJsonObject> Added = FSalStateTreeInterface::Patch(
        SalMutationPatch({
            MakeShared<FJsonValueObject>(Definition),
            MakeShared<FJsonValueObject>(Add),
        }),
        SalMutationTarget(Fixture));
    if (!SalMutationBool(Added, TEXT("valid")))
    {
        AddInfo(TEXT("Transition add diagnostic: ")
            + SalMutationDiagnosticSummary(Added));
    }
    TestTrue(TEXT("Transition add validates"), SalMutationBool(Added, TEXT("valid")));
    TestEqual(TEXT("Transition constructor is one logical add"), SalMutationPlannedOperationCount(Added), 1);
    TestEqual(TEXT("Transition is created"), Fixture.First->Transitions.Num(), 1);
    if (Fixture.First->Transitions.IsEmpty())
    {
        return false;
    }
    FStateTreeTransition& Transition = Fixture.First->Transitions[0];
    TestTrue(
        TEXT("Native ArrayAdd defaults Trigger"),
        Transition.Trigger == EStateTreeTransitionTrigger::OnStateCompleted);
    TestEqual(TEXT("Native ArrayAdd defaults target to root"), Transition.State.ID, Fixture.Root->ID);

    const TSharedPtr<FJsonObject> Compiled = FSalStateTreeInterface::Patch(
        SalMutationPatch({MakeShared<FJsonValueObject>(SalMutationKind(TEXT("compile")))}),
        SalMutationTarget(Fixture));
    TestTrue(TEXT("Default Transition compiles without repair"), SalMutationBool(Compiled, TEXT("valid")));

    Transition.bDelayTransition = true;
    TSharedRef<FJsonObject> SetDuration = SalMutationKind(TEXT("set"));
    SetDuration->SetObjectField(
        TEXT("target"),
        SalMutationMember(
            SalMutationStable(TEXT("transition"), SalMutationGuid(Transition.ID)),
            {TEXT("DelayDuration")}));
    SetDuration->SetNumberField(TEXT("value"), 2.0);
    const TSharedPtr<FJsonObject> Cascaded = FSalStateTreeInterface::Patch(
        SalMutationPatch({MakeShared<FJsonValueObject>(SetDuration)}),
        SalMutationTarget(Fixture));
    TestTrue(TEXT("Malformed delayed completion Transition set validates"), SalMutationBool(Cascaded, TEXT("valid")));
    TestFalse(TEXT("Native Transition callback clears delay"), Transition.bDelayTransition);
    TestTrue(
        TEXT("Plan reports delay cascade"),
        SalMutationHasPlannedEffect(Cascaded, TEXT(".bDelayTransition")));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FSalStateTreeMutationCompileCascadeTest,
    "Loomle.Sal.StateTree.Mutation.CompileCascade",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSalStateTreeMutationCompileCascadeTest::RunTest(const FString& Parameters)
{
    if (GEditor == nullptr || GEditor->IsPlaySessionInProgress())
    {
        AddError(TEXT("Compile cascade test requires an available Editor outside PIE."));
        return false;
    }
    const FSalMutationFixture Fixture = MakeSalMutationFixture();
    FinalizeSalMutationFixture(Fixture);
    const FGuid SourceParameterId(0xC6000001, 0xC6000002, 0xC6000003, 0xC6000004);
    const FGuid RemovedId(0xC7000001, 0xC7000002, 0xC7000003, 0xC7000004);
    const FGuid SingleId(0xC8000001, 0xC8000002, 0xC8000003, 0xC8000004);
    FInstancedPropertyBag& Bag = const_cast<FInstancedPropertyBag&>(
        Fixture.Data->GetRootParametersPropertyBag());
    Bag.AddProperties({
        SalMutationIntParameter(TEXT("CompileSource"), SourceParameterId)
    });
    AddSalMutationTask(*Fixture.First, RemovedId, TEXT("SchemaRejected"));
    Fixture.Second->SingleTask.ID = SingleId;
    Fixture.Second->SingleTask.Node.InitializeAs<FSalStateTreeBindingTask>();
    Fixture.Second->SingleTask.Node.GetMutable<FSalStateTreeBindingTask>().Name = FName(TEXT("WrongName"));
    Fixture.Second->SingleTask.Instance.InitializeAs<FSalStateTreeBindingTaskInstanceData>();
    Fixture.Schema->bAllowMultipleTasks = false;
    Fixture.Data->EditorBindings.AddBinding(
        FPropertyBindingPath(
            Fixture.Data->GetRootParametersGuid(),
            FName(TEXT("CompileSource"))),
        FPropertyBindingPath(RemovedId, FName(TEXT("InputValue"))));

    const TSharedPtr<FJsonObject> DryRun = FSalStateTreeInterface::Patch(
        SalMutationPatch({MakeShared<FJsonValueObject>(SalMutationKind(TEXT("compile")))}, true),
        SalMutationTarget(Fixture));
    if (!SalMutationBool(DryRun, TEXT("valid")))
    {
        AddInfo(TEXT("Compile cascade dry-run diagnostic: ")
            + SalMutationDiagnosticSummary(DryRun));
    }
    TestTrue(TEXT("Compile cascade dry run validates"), SalMutationBool(DryRun, TEXT("valid")));
    TestEqual(TEXT("Dry run preserves schema-rejected Task"), Fixture.First->Tasks.Num(), 1);
    TestTrue(
        TEXT("Compile plan enumerates removed Task"),
        SalMutationHasPlannedEffect(DryRun, TEXT("compile validation removed node@") + SalMutationGuid(RemovedId)));
    TestTrue(
        TEXT("Compile plan enumerates SingleTask update"),
        SalMutationHasPlannedEffect(DryRun, TEXT("compile validation updated node@") + SalMutationGuid(SingleId)));
    TestTrue(
        TEXT("Compile plan enumerates invalid Binding"),
        SalMutationHasPlannedEffectContainingBoth(
            DryRun,
            TEXT("removed native Binding"),
            TEXT("target node@") + SalMutationGuid(RemovedId)));

    const TSharedPtr<FJsonObject> Applied = FSalStateTreeInterface::Patch(
        SalMutationPatch({MakeShared<FJsonValueObject>(SalMutationKind(TEXT("compile")))}),
        SalMutationTarget(Fixture));
    if (!SalMutationBool(Applied, TEXT("valid")))
    {
        AddInfo(TEXT("Compile cascade live diagnostic: ")
            + SalMutationDiagnosticSummary(Applied));
    }
    TestTrue(TEXT("Compile cascade live apply validates"), SalMutationBool(Applied, TEXT("valid")));
    TestFalse(TEXT("Compile dry-run exposes a non-empty plan"), SalMutationPlannedJson(DryRun).IsEmpty());
    TestEqual(
        TEXT("Compile cascade live plan matches dry run"),
        SalMutationPlannedJson(Applied),
        SalMutationPlannedJson(DryRun));
    TestEqual(TEXT("Live compile removes schema-rejected Task"), Fixture.First->Tasks.Num(), 0);
    TestEqual(TEXT("Live compile removes invalid Binding"), Fixture.Data->EditorBindings.GetBindings().Num(), 0);
    TestEqual(
        TEXT("Live compile synchronizes SingleTask Name"),
        Fixture.Second->SingleTask.Node.Get<FSalStateTreeBindingTask>().Name,
        Fixture.Second->Name);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FSalStateTreeMutationCompileDryRunTest,
    "Loomle.Sal.StateTree.Mutation.CompileDryRun",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSalStateTreeMutationCompileDryRunTest::RunTest(const FString& Parameters)
{
    if (GEditor == nullptr || GEditor->IsPlaySessionInProgress())
    {
        AddError(TEXT("Compile dry-run test requires an available Editor outside PIE."));
        return false;
    }
    const FSalMutationFixture Fixture = MakeSalMutationFixture();
    FinalizeSalMutationFixture(Fixture);
    Fixture.Asset->SetFlags(RF_Public | RF_Standalone);
    TSet<UStateTree*> ExistingTransientTrees;
    for (TObjectIterator<UStateTree> It; It; ++It)
    {
        ExistingTransientTrees.Add(*It);
    }
    const uint32 BeforeHash = Fixture.Asset->LastCompiledEditorDataHash;
    const TSharedPtr<FJsonObject> Result = FSalStateTreeInterface::Patch(
        SalMutationPatch({MakeShared<FJsonValueObject>(SalMutationKind(TEXT("compile")))}, true),
        SalMutationTarget(Fixture));
    TestTrue(TEXT("Compile dry run executes against transient copy"), SalMutationBool(Result, TEXT("valid")));
    TestFalse(TEXT("Compile dry run does not apply"), SalMutationBool(Result, TEXT("applied")));
    TestEqual(TEXT("Compile dry run preserves live compiled hash"), Fixture.Asset->LastCompiledEditorDataHash, BeforeHash);
    TestTrue(
        TEXT("Compile dry run preserves live asset flags"),
        Fixture.Asset->HasAllFlags(RF_Public | RF_Standalone));

    int32 NewTransientCopies = 0;
    const auto VerifyTransientFlags = [this](UObject* Object)
    {
        TestFalse(
            TEXT("Preflight object is not public, standalone, or pending PostLoad"),
            Object != nullptr
                && Object->HasAnyFlags(
                    RF_Public
                    | RF_Standalone
                    | RF_NeedPostLoad
                    | RF_NeedPostLoadSubobjects));
    };
    for (TObjectIterator<UStateTree> It; It; ++It)
    {
        UStateTree* Candidate = *It;
        if (ExistingTransientTrees.Contains(Candidate)
            || Candidate->GetOuter() != GetTransientPackage()
            || !Candidate->GetName().StartsWith(
                Fixture.Asset->GetName() + TEXT("_SALDryRun")))
        {
            continue;
        }
        ++NewTransientCopies;
        VerifyTransientFlags(Candidate);
        ForEachObjectWithOuter(
            Candidate,
            [&VerifyTransientFlags](UObject* Object)
            {
                VerifyTransientFlags(Object);
            },
            true);
    }
    TestTrue(
        TEXT("Compile dry run produced an inspectable transient copy"),
        NewTransientCopies > 0);
    Fixture.Asset->ClearFlags(RF_Public | RF_Standalone);
    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
