// Copyright 2026 Loomle contributors.

#if WITH_DEV_AUTOMATION_TESTS

#include "LoomleMutationTransaction.h"
#include "Tests/LoomleTestEditorState.h"

#include "Editor.h"
#include "Engine/Blueprint.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FLoomleActiveTransactionRollbackPreservesRedoTest,
    "Loomle.Mutation.Transaction.ActiveRollbackPreservesRedo",
    EAutomationTestFlags::EditorContext
        | EAutomationTestFlags::EngineFilter)

bool FLoomleActiveTransactionRollbackPreservesRedoTest::RunTest(
    const FString& Parameters)
{
    (void)Parameters;

    Loomle::Tests::FScopedIsolatedTransactor TestTransactor;
    if (!TestTrue(
            TEXT("The test owns an isolated UE transaction buffer"),
            TestTransactor.Initialize()))
    {
        return false;
    }

    UBlueprint* Subject = NewObject<UBlueprint>(
        GetTransientPackage(),
        NAME_None,
        RF_Transactional);
    if (!TestNotNull(TEXT("Transaction subject exists"), Subject))
    {
        return false;
    }

    const FString OriginalValue = TEXT("before");
    const FString RedoValue = TEXT("redo");
    Subject->BlueprintDescription = OriginalValue;
    {
        FScopedTransaction Transaction(
            FText::FromString(TEXT("Loomle transaction test seed")));
        if (!TestTrue(
                TEXT("Seed transaction opened"),
                Transaction.IsOutstanding()))
        {
            return false;
        }
        Subject->Modify();
        Subject->BlueprintDescription = RedoValue;
    }

    TestTrue(
        TEXT("Seed transaction can be undone with redo retained"),
        GEditor->UndoTransaction(true));
    TestEqual(
        TEXT("Seed undo restored the original value"),
        Subject->BlueprintDescription,
        OriginalValue);
    TestTrue(
        TEXT("Seed undo produced a redo entry"),
        GEditor->Trans->CanRedo());
    const FTransactionContext ExpectedRedo =
        GEditor->Trans->GetRedoContext();
    TestTrue(
        TEXT("Seed redo has a stable transaction identity"),
        ExpectedRedo.TransactionId.IsValid());

    {
        LoomleMutation::FScopedAtomicTransaction Transaction(
            FText::FromString(TEXT("Loomle failed atomic edit")));
        if (!TestTrue(
                TEXT("Atomic transaction opened"),
                Transaction.IsOutstanding()))
        {
            return false;
        }

        Subject->Modify();
        Subject->BlueprintDescription = TEXT("failed edit");
        TestTrue(
            TEXT("Active transaction reverted and canceled"),
            Transaction.RevertAndCancel());
    }

    TestEqual(
        TEXT("Failed edit restored its captured value"),
        Subject->BlueprintDescription,
        OriginalValue);
    TestTrue(
        TEXT("Pre-existing redo remains available"),
        GEditor->Trans->CanRedo());
    const FTransactionContext ActualRedo =
        GEditor->Trans->GetRedoContext();
    TestEqual(
        TEXT("Pre-existing redo identity was preserved"),
        ActualRedo.TransactionId,
        ExpectedRedo.TransactionId);
    TestTrue(
        TEXT("Preserved redo can still execute"),
        GEditor->RedoTransaction());
    TestEqual(
        TEXT("Preserved redo restores the seed edit"),
        Subject->BlueprintDescription,
        RedoValue);
    return true;
}

#endif
