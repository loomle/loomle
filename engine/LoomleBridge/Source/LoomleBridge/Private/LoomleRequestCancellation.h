// Copyright 2026 Loomle contributors.

#pragma once

#include "CoreMinimal.h"

namespace Loomle::Runtime
{

/** Thread-safe cancellation state shared by every execution hop of one RPC request. */
class FRequestCancellationState : public TSharedFromThis<FRequestCancellationState, ESPMode::ThreadSafe>
{
public:
    void Cancel();
    bool IsCancellationRequested() const;

private:
    TAtomic<bool> bCancellationRequested { false };
};

/**
 * Thread-safe registry for request cancellation tokens and connection-close
 * races. Tombstones ensure a control message or disconnect that overtakes its
 * invoke worker is still observed when that worker registers.
 */
class FRequestCancellationRegistry
{
public:
    TSharedRef<FRequestCancellationState, ESPMode::ThreadSafe> Register(
        int32 ConnectionSerial,
        const FString& CancellationKey,
        double NowSeconds = -1.0);
    void Unregister(
        int32 ConnectionSerial,
        const FString& CancellationKey,
        const TSharedRef<FRequestCancellationState, ESPMode::ThreadSafe>& ExpectedState);
    bool Cancel(const FString& CancellationKey, double NowSeconds = -1.0);
    void CloseConnection(int32 ConnectionSerial, double NowSeconds = -1.0);

#if WITH_DEV_AUTOMATION_TESTS
    int32 PendingTokenCountForTesting() const;
#endif

private:
    struct FEntry
    {
        int32 ConnectionSerial = 0;
        TSharedPtr<FRequestCancellationState, ESPMode::ThreadSafe> State;
    };

    void Prune(double NowSeconds);

    TMap<FString, FEntry> Active;
    TMap<FString, double> PendingTokens;
    TMap<int32, double> ClosedConnections;
    mutable FCriticalSection Mutex;
};

/** Makes a request's cancellation state visible to synchronous provider code on this thread. */
class FScopedRequestCancellation
{
public:
    explicit FScopedRequestCancellation(const TSharedRef<FRequestCancellationState, ESPMode::ThreadSafe>& InState);
    ~FScopedRequestCancellation();

    FScopedRequestCancellation(const FScopedRequestCancellation&) = delete;
    FScopedRequestCancellation& operator=(const FScopedRequestCancellation&) = delete;

private:
    TSharedRef<FRequestCancellationState, ESPMode::ThreadSafe> State;
    FRequestCancellationState* PreviousState = nullptr;
};

/** Returns the active request state, if the caller is executing inside an RPC request scope. */
TSharedPtr<FRequestCancellationState, ESPMode::ThreadSafe> GetRequestCancellationState();

/** Cheap provider-facing cancellation check. */
bool IsRequestCancellationRequested();

}
