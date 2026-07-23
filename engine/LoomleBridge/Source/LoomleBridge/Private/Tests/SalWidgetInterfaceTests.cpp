// Copyright 2026 Loomle contributors.

#if WITH_DEV_AUTOMATION_TESTS

#include "Sal/Widget/SalWidgetInterface.h"
#include "Tests/LoomleTestEditorState.h"

#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetBlueprintGeneratedClass.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Button.h"
#include "Components/CanvasPanel.h"
#include "Components/PanelSlot.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Editor.h"
#include "Engine/Blueprint.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/AutomationTest.h"
#include "Sal/SalModel.h"
#include "UObject/GarbageCollection.h"
#include "UObject/Package.h"
#include "UObject/UObjectHash.h"
#include "WidgetBlueprint.h"

namespace
{
using namespace Loomle::Sal;

FString WidgetGuidText(const FGuid& Guid)
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
            && (*Diagnostic)->TryGetStringField(TEXT("severity"), Severity)
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
    TArray<TSharedPtr<FJsonObject>> Args;
    const TSharedPtr<FJsonObject>* Object = nullptr;
    const TArray<TSharedPtr<FJsonValue>>* Statements = nullptr;
    if (!Result.IsValid()
        || !Result->TryGetObjectField(TEXT("object"), Object)
        || Object == nullptr
        || !(*Object)->TryGetArrayField(TEXT("statements"), Statements)
        || Statements == nullptr)
    {
        return Args;
    }
    for (const TSharedPtr<FJsonValue>& StatementValue : *Statements)
    {
        const TSharedPtr<FJsonObject>* Statement = nullptr;
        const TSharedPtr<FJsonObject>* Call = nullptr;
        const TSharedPtr<FJsonObject>* CallArgsObject = nullptr;
        FString ActualCallee;
        if (StatementValue.IsValid()
            && StatementValue->TryGetObject(Statement)
            && Statement != nullptr
            && (*Statement)->TryGetObjectField(TEXT("value"), Call)
            && Call != nullptr
            && (*Call)->TryGetStringField(TEXT("callee"), ActualCallee)
            && ActualCallee == Callee
            && (*Call)->TryGetObjectField(TEXT("args"), CallArgsObject)
            && CallArgsObject != nullptr)
        {
            Args.Add(*CallArgsObject);
        }
    }
    return Args;
}

TSharedPtr<FJsonObject> FindCallArgsById(
    const TSharedPtr<FJsonObject>& Result,
    const FString& Callee,
    const FString& Id)
{
    for (const TSharedPtr<FJsonObject>& Args : CallArgs(Result, Callee))
    {
        FString ActualId;
        if (Args.IsValid()
            && Args->TryGetStringField(TEXT("id"), ActualId)
            && ActualId == Id)
        {
            return Args;
        }
    }
    return nullptr;
}

TArray<FString> CallIds(
    const TSharedPtr<FJsonObject>& Result,
    const FString& Callee)
{
    TArray<FString> Ids;
    for (const TSharedPtr<FJsonObject>& Args : CallArgs(Result, Callee))
    {
        FString Id;
        if (Args.IsValid() && Args->TryGetStringField(TEXT("id"), Id))
        {
            Ids.Add(Id);
        }
    }
    return Ids;
}

bool HasCommentContaining(
    const TSharedPtr<FJsonObject>& Result,
    const FString& Text)
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
        FString Comment;
        if (StatementValue.IsValid()
            && StatementValue->TryGetObject(Statement)
            && Statement != nullptr
            && (*Statement)->TryGetStringField(TEXT("kind"), Kind)
            && Kind == TEXT("comment")
            && (*Statement)->TryGetStringField(TEXT("text"), Comment)
            && Comment.Contains(Text))
        {
            return true;
        }
    }
    return false;
}

bool HasWidgetMemberBinding(
    const TSharedPtr<FJsonObject>& Result,
    const FString& WidgetId,
    const TArray<FString>& ExpectedPath)
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
        const TSharedPtr<FJsonObject>* Target = nullptr;
        const TSharedPtr<FJsonObject>* Call = nullptr;
        const TSharedPtr<FJsonObject>* Args = nullptr;
        const TArray<TSharedPtr<FJsonValue>>* Path = nullptr;
        FString TargetKind;
        FString Callee;
        FString ActualId;
        if (!StatementValue.IsValid()
            || !StatementValue->TryGetObject(Statement)
            || Statement == nullptr
            || !(*Statement)->TryGetObjectField(TEXT("target"), Target)
            || Target == nullptr
            || !(*Target)->TryGetStringField(TEXT("kind"), TargetKind)
            || TargetKind != TEXT("member")
            || !(*Target)->TryGetArrayField(TEXT("path"), Path)
            || Path == nullptr
            || !(*Statement)->TryGetObjectField(TEXT("value"), Call)
            || Call == nullptr
            || !(*Call)->TryGetStringField(TEXT("callee"), Callee)
            || Callee != TEXT("widget")
            || !(*Call)->TryGetObjectField(TEXT("args"), Args)
            || Args == nullptr
            || !(*Args)->TryGetStringField(TEXT("id"), ActualId)
            || ActualId != WidgetId
            || Path->Num() != ExpectedPath.Num())
        {
            continue;
        }
        bool bPathMatches = true;
        for (int32 Index = 0; Index < Path->Num(); ++Index)
        {
            FString Segment;
            if (!(*Path)[Index].IsValid()
                || !(*Path)[Index]->TryGetString(Segment)
                || Segment != ExpectedPath[Index])
            {
                bPathMatches = false;
                break;
            }
        }
        if (bPathMatches)
        {
            return true;
        }
    }
    return false;
}

FSalResolvedTarget WidgetTarget(UWidgetBlueprint* Blueprint)
{
    FSalResolvedTarget Target;
    Target.Kind = ESalTargetKind::Blueprint;
    Target.Alias = TEXT("widget_blueprint");
    Target.AssetPath =
        Blueprint != nullptr ? Blueprint->GetPathName() : FString();
    Target.Id =
        Blueprint != nullptr
            ? WidgetGuidText(Blueprint->GetBlueprintGuid())
            : FString();
    Target.Object = Blueprint;
    Target.Package =
        Blueprint != nullptr ? Blueprint->GetOutermost() : nullptr;
    Target.Blueprint = Blueprint;
    Target.Class =
        Blueprint != nullptr ? Blueprint->GeneratedClass.Get() : nullptr;
    Target.Interfaces = {
        FName(TEXT("blueprint")),
        FName(TEXT("widget"))};
    return Target;
}

FSalQuery WidgetQuery(const FString& Kind)
{
    FSalQuery Query;
    Query.Alias = TEXT("widget_blueprint");
    Query.Operation = MakeShared<FJsonObject>();
    Query.Operation->SetStringField(TEXT("kind"), Kind);
    return Query;
}

FSalPatch SetWidgetVariablePatch(
    const FGuid& WidgetGuid,
    const bool Value)
{
    TSharedRef<FJsonObject> WidgetRef = MakeShared<FJsonObject>();
    WidgetRef->SetStringField(TEXT("kind"), TEXT("widget"));
    WidgetRef->SetStringField(TEXT("id"), WidgetGuidText(WidgetGuid));

    TSharedRef<FJsonObject> Member = MakeShared<FJsonObject>();
    Member->SetStringField(TEXT("kind"), TEXT("member"));
    Member->SetObjectField(TEXT("object"), WidgetRef);
    Member->SetArrayField(
        TEXT("path"),
        {MakeShared<FJsonValueString>(TEXT("bIsVariable"))});

    TSharedRef<FJsonObject> Set = MakeShared<FJsonObject>();
    Set->SetStringField(TEXT("kind"), TEXT("set"));
    Set->SetObjectField(TEXT("target"), Member);
    Set->SetBoolField(TEXT("value"), Value);

    FSalPatch Patch;
    Patch.Alias = TEXT("widget_blueprint");
    Patch.Statements = {MakeShared<FJsonValueObject>(Set)};
    return Patch;
}

bool UnloadWidgetTestPackage(
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

class FWidgetInterfaceFixture
{
public:
    FWidgetInterfaceFixture() = default;
    FWidgetInterfaceFixture(const FWidgetInterfaceFixture&) = delete;
    FWidgetInterfaceFixture& operator=(
        const FWidgetInterfaceFixture&) = delete;

    ~FWidgetInterfaceFixture()
    {
        FString Ignored;
        Cleanup(Ignored);
    }

    bool Initialize(FString& OutError)
    {
        OutError.Reset();
        const FString Token =
            FGuid::NewGuid().ToString(EGuidFormats::Digits);
        Package = CreatePackage(*FString::Printf(
            TEXT("/Game/LoomleTests/SalWidgetInterface_%s"),
            *Token));
        Blueprint = Cast<UWidgetBlueprint>(
            FKismetEditorUtilities::CreateBlueprint(
                UUserWidget::StaticClass(),
                Package,
                *FString::Printf(
                    TEXT("WBP_WidgetInterface_%s"),
                    *Token),
                BPTYPE_Normal,
                UWidgetBlueprint::StaticClass(),
                UWidgetBlueprintGeneratedClass::StaticClass(),
                NAME_None));
        if (Package == nullptr
            || Blueprint == nullptr
            || Blueprint->WidgetTree == nullptr)
        {
            OutError =
                TEXT("UE could not create the WidgetBlueprint fixture.");
            return false;
        }

        Blueprint->WidgetTree->SetFlags(RF_Transactional);
        Blueprint->WidgetTree->Modify();
        Root = Blueprint->WidgetTree->ConstructWidget<UCanvasPanel>(
            UCanvasPanel::StaticClass(),
            RootName);
        Child = Blueprint->WidgetTree->ConstructWidget<UButton>(
            UButton::StaticClass(),
            ChildName);
        if (Root == nullptr || Child == nullptr)
        {
            OutError =
                TEXT("UE could not construct the fixture Widget hierarchy.");
            return false;
        }

        Root->bIsVariable = false;
        Child->bIsVariable = false;
        Blueprint->WidgetTree->RootWidget = Root;
        Slot = Root->AddChild(Child);
        if (Slot == nullptr)
        {
            OutError =
                TEXT("UE could not attach the fixture child Widget.");
            return false;
        }
        Blueprint->OnVariableAdded(RootName);
        Blueprint->OnVariableAdded(ChildName);

        // Match UE 5.7 Widget authoring order: complete the source hierarchy
        // and persistent identities before the first compile.
        FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(
            Blueprint);
        FKismetEditorUtilities::CompileBlueprint(Blueprint);

        Root = Cast<UCanvasPanel>(
            Blueprint->WidgetTree->FindWidget(RootName));
        Child = Cast<UButton>(
            Blueprint->WidgetTree->FindWidget(ChildName));
        RootGuid =
            Blueprint->WidgetVariableNameToGuidMap.FindRef(RootName);
        ChildGuid =
            Blueprint->WidgetVariableNameToGuidMap.FindRef(ChildName);
        if (Root == nullptr
            || Child == nullptr
            || Blueprint->GeneratedClass == nullptr
            || Blueprint->Status == BS_Error
            || !RootGuid.IsValid()
            || !ChildGuid.IsValid())
        {
            OutError =
                TEXT("The authored WidgetBlueprint fixture did not compile with stable Widget identities.");
            return false;
        }
        Package->SetDirtyFlag(false);
        return true;
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
        Slot = nullptr;
        Child = nullptr;
        Root = nullptr;
        Blueprint = nullptr;
        Package = nullptr;
        return UnloadWidgetTestPackage(
            PackageToUnload,
            OutError);
    }

    UButton* FindChild() const
    {
        return Blueprint != nullptr && Blueprint->WidgetTree != nullptr
            ? Cast<UButton>(
                Blueprint->WidgetTree->FindWidget(ChildName))
            : nullptr;
    }

    static const FName RootName;
    static const FName ChildName;

    UPackage* Package = nullptr;
    UWidgetBlueprint* Blueprint = nullptr;
    UCanvasPanel* Root = nullptr;
    UButton* Child = nullptr;
    UPanelSlot* Slot = nullptr;
    FGuid RootGuid;
    FGuid ChildGuid;

private:
    bool bCleaned = false;
};

const FName FWidgetInterfaceFixture::RootName(
    TEXT("RootCanvas"));
const FName FWidgetInterfaceFixture::ChildName(
    TEXT("ActionButton"));
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FSalWidgetQueryCoverageTest,
    "Loomle.Sal.Widget.Query.SummaryTreeCollectionAndExact",
    EAutomationTestFlags::EditorContext
        | EAutomationTestFlags::EngineFilter)

bool FSalWidgetQueryCoverageTest::RunTest(
    const FString& Parameters)
{
    (void)Parameters;
    FWidgetInterfaceFixture Fixture;
    Loomle::Tests::FScopedIsolatedTransactor TestTransactor;
    if (!TestTrue(
            TEXT("Widget Query test owns an isolated UE transaction buffer"),
            TestTransactor.Initialize()))
    {
        return false;
    }

    FString FixtureError;
    if (!TestTrue(
            *FString::Printf(
                TEXT("Authored Widget Query fixture compiles: %s"),
                *FixtureError),
            Fixture.Initialize(FixtureError)))
    {
        TestTransactor.Restore();
        FString CleanupError;
        Fixture.Cleanup(CleanupError);
        return false;
    }

    {
        const FSalResolvedTarget Target =
            WidgetTarget(Fixture.Blueprint);
        const FString BlueprintId =
            WidgetGuidText(Fixture.Blueprint->GetBlueprintGuid());
        const FString RootId =
            WidgetGuidText(Fixture.RootGuid);
        const FString ChildId =
            WidgetGuidText(Fixture.ChildGuid);

        const TSharedPtr<FJsonObject> Summary =
            FSalWidgetInterface::Query(
                WidgetQuery(TEXT("summary")),
                Target);
        TestFalse(
            TEXT("Widget summary Query succeeds"),
            HasError(Summary));
        const TSharedPtr<FJsonObject> SummaryArgs =
            FindCallArgsById(
                Summary,
                TEXT("blueprint"),
                BlueprintId);
        TestNotNull(
            TEXT("Widget summary returns the exact WidgetBlueprint"),
            SummaryArgs.Get());
        if (SummaryArgs.IsValid())
        {
            const TSharedPtr<FJsonObject>* RootRef = nullptr;
            FString RootKind;
            FString ActualRootId;
            TestTrue(
                TEXT("Widget summary exposes the exact root Widget"),
                SummaryArgs->TryGetObjectField(
                    TEXT("Root"),
                    RootRef)
                    && RootRef != nullptr
                    && (*RootRef)->TryGetStringField(
                        TEXT("kind"),
                        RootKind)
                    && RootKind == TEXT("widget")
                    && (*RootRef)->TryGetStringField(
                        TEXT("id"),
                        ActualRootId)
                    && ActualRootId == RootId);
        }
        TestTrue(
            TEXT("Widget summary reports both authored Widgets"),
            HasCommentContaining(
                Summary,
                TEXT("widgets: 2")));
        TestTrue(
            TEXT("Widget summary reports both Widgets reachable"),
            HasCommentContaining(
                Summary,
                TEXT("reachable widgets: 2")));

        const TSharedPtr<FJsonObject> Tree =
            FSalWidgetInterface::Query(
                WidgetQuery(TEXT("tree")),
                Target);
        TestFalse(
            TEXT("Widget tree Query succeeds"),
            HasError(Tree));
        const TArray<FString> TreeIds =
            CallIds(Tree, TEXT("widget"));
        TestEqual(
            TEXT("Widget tree emits exactly the authored hierarchy"),
            TreeIds.Num(),
            2);
        TestTrue(
            TEXT("Widget tree preserves root-first order"),
            TreeIds.Num() == 2
                && TreeIds[0] == RootId
                && TreeIds[1] == ChildId);
        TestTrue(
            TEXT("Widget tree emits the child under its authored member path"),
            HasWidgetMemberBinding(
                Tree,
                ChildId,
                {Fixture.ChildName.ToString()}));

        const TSharedPtr<FJsonObject> Collection =
            FSalWidgetInterface::Query(
                WidgetQuery(TEXT("widgets")),
                Target);
        TestFalse(
            TEXT("Widget collection Query succeeds"),
            HasError(Collection));
        const TArray<FString> CollectionIds =
            CallIds(Collection, TEXT("widget"));
        TestEqual(
            TEXT("Widget collection returns both source Widgets"),
            CollectionIds.Num(),
            2);
        TestTrue(
            TEXT("Widget collection preserves authored tree order"),
            CollectionIds.Num() == 2
                && CollectionIds[0] == RootId
                && CollectionIds[1] == ChildId);

        FSalQuery Exact = WidgetQuery(TEXT("widget"));
        Exact.Operation->SetStringField(
            TEXT("id"),
            ChildId);
        const TSharedPtr<FJsonObject> ExactResult =
            FSalWidgetInterface::Query(Exact, Target);
        TestFalse(
            TEXT("Exact Widget Query succeeds"),
            HasError(ExactResult));
        const TSharedPtr<FJsonObject> ExactArgs =
            FindCallArgsById(
                ExactResult,
                TEXT("widget"),
                ChildId);
        TestNotNull(
            TEXT("Exact Widget Query returns the requested Widget"),
            ExactArgs.Get());
        if (ExactArgs.IsValid())
        {
            FString Type;
            TestTrue(
                TEXT("Exact Widget Query preserves the native Widget type"),
                ExactArgs->TryGetStringField(
                    TEXT("type"),
                    Type)
                    && Type
                        == UButton::StaticClass()
                               ->GetPathName());
            TestFalse(
                TEXT("Exact Widget Query omits the false structural flag"),
                ExactArgs->HasField(
                    TEXT("bIsVariable")));
        }
    }

    TestTransactor.Restore();
    FString CleanupError;
    TestTrue(
        *FString::Printf(
            TEXT("Widget Query fixture unloads cleanly: %s"),
            *CleanupError),
        Fixture.Cleanup(CleanupError));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FSalWidgetLiveMutationCoverageTest,
    "Loomle.Sal.Widget.Mutation.LiveSetReadbackUndo",
    EAutomationTestFlags::EditorContext
        | EAutomationTestFlags::EngineFilter)

bool FSalWidgetLiveMutationCoverageTest::RunTest(
    const FString& Parameters)
{
    (void)Parameters;
    if (GEditor == nullptr)
    {
        AddError(
            TEXT("Widget live mutation test requires GEditor."));
        return false;
    }

    FWidgetInterfaceFixture Fixture;
    Loomle::Tests::FScopedIsolatedTransactor TestTransactor;
    if (!TestTrue(
            TEXT("Widget mutation test owns an isolated UE transaction buffer"),
            TestTransactor.Initialize()))
    {
        return false;
    }

    FString FixtureError;
    if (!TestTrue(
            *FString::Printf(
                TEXT("Authored Widget mutation fixture compiles: %s"),
                *FixtureError),
            Fixture.Initialize(FixtureError)))
    {
        TestTransactor.Restore();
        FString CleanupError;
        Fixture.Cleanup(CleanupError);
        return false;
    }

    {
        const FSalResolvedTarget Target =
            WidgetTarget(Fixture.Blueprint);
        const FString ChildId =
            WidgetGuidText(Fixture.ChildGuid);
        UButton* NativeChild = Fixture.FindChild();
        TestNotNull(
            TEXT("Live Patch target resolves in the source WidgetTree"),
            NativeChild);
        TestFalse(
            TEXT("Live Patch target starts non-variable"),
            NativeChild != nullptr
                && NativeChild->bIsVariable);

        const TSharedPtr<FJsonObject> Applied =
            FSalWidgetInterface::Patch(
                SetWidgetVariablePatch(
                    Fixture.ChildGuid,
                    true),
                Target);
        TestTrue(
            TEXT("Widget live set validates"),
            ResultBool(Applied, TEXT("valid")));
        TestTrue(
            TEXT("Widget live set reports applied"),
            ResultBool(Applied, TEXT("applied")));
        TestFalse(
            TEXT("Widget live set is not reported as dry-run"),
            ResultBool(Applied, TEXT("dryRun"), true));

        NativeChild = Fixture.FindChild();
        TestTrue(
            TEXT("Widget live set updates native source state"),
            NativeChild != nullptr
                && NativeChild->bIsVariable);
        TestEqual(
            TEXT("Widget live set preserves the persistent Widget id"),
            Fixture.Blueprint
                ->WidgetVariableNameToGuidMap
                .FindRef(Fixture.ChildName),
            Fixture.ChildGuid);

        FSalQuery ExactApplied =
            WidgetQuery(TEXT("widget"));
        ExactApplied.Operation->SetStringField(
            TEXT("id"),
            ChildId);
        const TSharedPtr<FJsonObject> AppliedReadback =
            FSalWidgetInterface::Query(
                ExactApplied,
                Target);
        TestFalse(
            TEXT("SAL readback after Widget live set succeeds"),
            HasError(AppliedReadback));
        const TSharedPtr<FJsonObject> AppliedArgs =
            FindCallArgsById(
                AppliedReadback,
                TEXT("widget"),
                ChildId);
        bool bSalIsVariable = false;
        TestTrue(
            TEXT("SAL readback observes the native Widget mutation"),
            AppliedArgs.IsValid()
                && AppliedArgs->TryGetBoolField(
                    TEXT("bIsVariable"),
                    bSalIsVariable)
                && bSalIsVariable);

        TestTrue(
            TEXT("Widget live Patch is one undoable UE transaction"),
            GEditor->UndoTransaction(false));
        NativeChild = Fixture.FindChild();
        TestFalse(
            TEXT("Undo restores native Widget state"),
            NativeChild != nullptr
                && NativeChild->bIsVariable);
        TestEqual(
            TEXT("Undo preserves the persistent Widget id"),
            Fixture.Blueprint
                ->WidgetVariableNameToGuidMap
                .FindRef(Fixture.ChildName),
            Fixture.ChildGuid);

        FSalQuery ExactUndone =
            WidgetQuery(TEXT("widget"));
        ExactUndone.Operation->SetStringField(
            TEXT("id"),
            ChildId);
        const TSharedPtr<FJsonObject> UndoReadback =
            FSalWidgetInterface::Query(
                ExactUndone,
                Target);
        TestFalse(
            TEXT("SAL readback after Widget Undo succeeds"),
            HasError(UndoReadback));
        const TSharedPtr<FJsonObject> UndoArgs =
            FindCallArgsById(
                UndoReadback,
                TEXT("widget"),
                ChildId);
        TestTrue(
            TEXT("SAL readback observes the restored Widget state"),
            UndoArgs.IsValid()
                && !UndoArgs->HasField(
                    TEXT("bIsVariable")));
    }

    TestTransactor.Restore();
    FString CleanupError;
    TestTrue(
        *FString::Printf(
            TEXT("Widget mutation fixture unloads cleanly: %s"),
            *CleanupError),
        Fixture.Cleanup(CleanupError));
    return true;
}

#endif
