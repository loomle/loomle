// Copyright 2026 Loomle contributors.

#pragma once

#if WITH_DEV_AUTOMATION_TESTS

#include "Editor.h"
#include "Editor/TransBuffer.h"
#include "UObject/StrongObjectPtr.h"

namespace Loomle::Tests
{
/**
 * Routes fixture-authoring transactions into a disposable buffer without
 * clearing or appending to the Editor's real Undo/Redo history.
 */
class FScopedIsolatedTransactor
{
public:
    FScopedIsolatedTransactor() = default;
    FScopedIsolatedTransactor(const FScopedIsolatedTransactor&) = delete;
    FScopedIsolatedTransactor& operator=(
        const FScopedIsolatedTransactor&) = delete;

    bool Initialize()
    {
        if (GEditor == nullptr
            || GEditor->IsTransactionActive()
            || Isolated.IsValid())
        {
            return false;
        }

        Original.Reset(GEditor->Trans.Get());
        UTransBuffer* Buffer =
            NewObject<UTransBuffer>(GetTransientPackage());
        if (Buffer == nullptr)
        {
            Original.Reset();
            return false;
        }
        Buffer->Initialize(16 * 1024 * 1024);
        Isolated.Reset(Buffer);
        GEditor->Trans = Buffer;
        return true;
    }

    void Restore()
    {
        if (GEditor != nullptr
            && GEditor->Trans.Get() == Isolated.Get())
        {
            if (GEditor->IsTransactionActive())
            {
                GEditor->CancelTransaction(0);
            }
            GEditor->Trans = Original.Get();
        }
        Isolated.Reset();
        Original.Reset();
    }

    ~FScopedIsolatedTransactor()
    {
        Restore();
    }

private:
    TStrongObjectPtr<UTransactor> Original;
    TStrongObjectPtr<UTransBuffer> Isolated;
};
}

#endif
