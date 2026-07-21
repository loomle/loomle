// Copyright 2026 Loomle contributors.

#include "LoomleRequestCancellation.h"

#include "HAL/PlatformTime.h"

namespace Loomle::Runtime
{
namespace
{
thread_local FRequestCancellationState* GCurrentRequestCancellationState = nullptr;
constexpr double TombstoneSeconds = 5.0 * 60.0;
constexpr int32 MaxTombstones = 4096;

double EffectiveNow(const double NowSeconds)
{
    return NowSeconds >= 0.0 ? NowSeconds : FPlatformTime::Seconds();
}
}

void FRequestCancellationState::Cancel()
{
    bCancellationRequested.Store(true);
}

bool FRequestCancellationState::IsCancellationRequested() const
{
    return bCancellationRequested.Load();
}

TSharedRef<FRequestCancellationState, ESPMode::ThreadSafe> FRequestCancellationRegistry::Register(
    const int32 ConnectionSerial,
    const FString& CancellationKey,
    const double NowSeconds)
{
    const TSharedRef<FRequestCancellationState, ESPMode::ThreadSafe> State =
        MakeShared<FRequestCancellationState, ESPMode::ThreadSafe>();
    FScopeLock Lock(&Mutex);
    Prune(EffectiveNow(NowSeconds));
    if (ClosedConnections.Contains(ConnectionSerial) || PendingTokens.Remove(CancellationKey) > 0)
    {
        State->Cancel();
    }
    if (const FEntry* Existing = Active.Find(CancellationKey))
    {
        if (Existing->State.IsValid())
        {
            Existing->State->Cancel();
        }
    }
    FEntry Entry;
    Entry.ConnectionSerial = ConnectionSerial;
    Entry.State = State;
    Active.Add(CancellationKey, MoveTemp(Entry));
    return State;
}

void FRequestCancellationRegistry::Unregister(
    const int32 ConnectionSerial,
    const FString& CancellationKey,
    const TSharedRef<FRequestCancellationState, ESPMode::ThreadSafe>& ExpectedState)
{
    FScopeLock Lock(&Mutex);
    const FEntry* Existing = Active.Find(CancellationKey);
    if (Existing != nullptr
        && Existing->ConnectionSerial == ConnectionSerial
        && Existing->State.Get() == &ExpectedState.Get())
    {
        Active.Remove(CancellationKey);
    }
}

bool FRequestCancellationRegistry::Cancel(const FString& CancellationKey, const double NowSeconds)
{
    FScopeLock Lock(&Mutex);
    const double Now = EffectiveNow(NowSeconds);
    Prune(Now);
    if (const FEntry* Entry = Active.Find(CancellationKey))
    {
        if (Entry->State.IsValid())
        {
            Entry->State->Cancel();
            return true;
        }
    }
    PendingTokens.Add(CancellationKey, Now);
    Prune(Now);
    return true;
}

void FRequestCancellationRegistry::CloseConnection(const int32 ConnectionSerial, const double NowSeconds)
{
    FScopeLock Lock(&Mutex);
    const double Now = EffectiveNow(NowSeconds);
    ClosedConnections.Add(ConnectionSerial, Now);
    Prune(Now);
    for (auto It = Active.CreateIterator(); It; ++It)
    {
        if (It.Value().ConnectionSerial == ConnectionSerial)
        {
            if (It.Value().State.IsValid())
            {
                It.Value().State->Cancel();
            }
            It.RemoveCurrent();
        }
    }
}

void FRequestCancellationRegistry::Prune(const double NowSeconds)
{
    for (auto It = PendingTokens.CreateIterator(); It; ++It)
    {
        if (NowSeconds - It.Value() > TombstoneSeconds)
        {
            It.RemoveCurrent();
        }
    }
    for (auto It = ClosedConnections.CreateIterator(); It; ++It)
    {
        if (NowSeconds - It.Value() > TombstoneSeconds)
        {
            It.RemoveCurrent();
        }
    }
    while (PendingTokens.Num() > MaxTombstones)
    {
        FString OldestKey;
        double OldestTime = TNumericLimits<double>::Max();
        for (const TPair<FString, double>& Pair : PendingTokens)
        {
            if (Pair.Value < OldestTime)
            {
                OldestTime = Pair.Value;
                OldestKey = Pair.Key;
            }
        }
        PendingTokens.Remove(OldestKey);
    }
    while (ClosedConnections.Num() > MaxTombstones)
    {
        int32 OldestKey = 0;
        double OldestTime = TNumericLimits<double>::Max();
        for (const TPair<int32, double>& Pair : ClosedConnections)
        {
            if (Pair.Value < OldestTime)
            {
                OldestTime = Pair.Value;
                OldestKey = Pair.Key;
            }
        }
        ClosedConnections.Remove(OldestKey);
    }
}

#if WITH_DEV_AUTOMATION_TESTS
int32 FRequestCancellationRegistry::PendingTokenCountForTesting() const
{
    FScopeLock Lock(&Mutex);
    return PendingTokens.Num();
}
#endif

FScopedRequestCancellation::FScopedRequestCancellation(
    const TSharedRef<FRequestCancellationState, ESPMode::ThreadSafe>& InState)
    : State(InState)
    , PreviousState(GCurrentRequestCancellationState)
{
    GCurrentRequestCancellationState = &State.Get();
}

FScopedRequestCancellation::~FScopedRequestCancellation()
{
    GCurrentRequestCancellationState = PreviousState;
}

TSharedPtr<FRequestCancellationState, ESPMode::ThreadSafe> GetRequestCancellationState()
{
    if (GCurrentRequestCancellationState == nullptr)
    {
        return nullptr;
    }

    return GCurrentRequestCancellationState->AsShared();
}

bool IsRequestCancellationRequested()
{
    return GCurrentRequestCancellationState != nullptr
        && GCurrentRequestCancellationState->IsCancellationRequested();
}

}
