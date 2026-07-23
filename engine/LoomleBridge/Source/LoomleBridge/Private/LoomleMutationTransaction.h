// Copyright 2026 Loomle contributors.

#pragma once

#include "CoreGlobals.h"
#include "Misc/ITransaction.h"
#include "ScopedTransaction.h"

namespace LoomleMutation
{
/**
 * Owns one top-level editor transaction and remembers the exact native
 * transaction record created for it.
 *
 * UE supports applying an active transaction before Finalize to restore its
 * captured objects, then canceling it. This is the only rollback path here:
 * finalizing and calling Undo(false) would discard the user's pre-existing
 * redo stack, and can undo the preceding user transaction when the failed
 * transaction was transient.
 */
class FScopedAtomicTransaction
{
public:
    explicit FScopedAtomicTransaction(const FText& Description)
        : Transaction(Description)
        , NativeTransaction(
            Transaction.IsOutstanding()
                ? GUndo
                : nullptr)
    {
    }

    bool IsOutstanding() const
    {
        return Transaction.IsOutstanding()
            && NativeTransaction != nullptr
            && GUndo == NativeTransaction;
    }

    void Cancel()
    {
        Transaction.Cancel();
    }

    /**
     * Restores every object captured by this active transaction and removes
     * the transaction without consuming the redo stack saved by UE Begin().
     *
     * If ownership of GUndo changed unexpectedly, leave the transaction
     * outstanding. Its destructor will finalize a recoverable user undo
     * record instead of canceling an unknown transaction.
     */
    bool RevertAndCancel()
    {
        if (!IsOutstanding())
        {
            return false;
        }

        NativeTransaction->Apply();
        Transaction.Cancel();
        return true;
    }

private:
    FScopedTransaction Transaction;
    ITransaction* NativeTransaction = nullptr;
};
}
