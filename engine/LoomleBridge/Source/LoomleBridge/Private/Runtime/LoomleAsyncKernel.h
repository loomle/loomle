// Copyright 2026 Loomle contributors.

#pragma once

#include "CoreMinimal.h"

class FJsonObject;

namespace Loomle::Runtime
{
/**
 * Frontend-neutral asynchronous execution kernel extracted from the Python
 * fallback lifecycle. It owns namespaced opaque execution-id allocation, the
 * outer status machine (`pending -> running -> succeeded | failed | lost`),
 * exact continuation formatting, thread-safe poll, bounded terminal retention
 * and expiry, no-replay, and lost-on-shutdown. Frontends (Python, typed PCG)
 * register a profile that supplies only their own error codes, poll tool,
 * continuation shape, and lost payload so the kernel never embeds frontend
 * strings.
 */
class LOOMLEBRIDGE_API FLoomleAsyncKernel
{
public:
    enum class EState : uint8
    {
        Pending,
        Running,
        Terminal
    };

    struct FProfile
    {
        FString Namespace; // "python" | "pcg"
        FString BusyCode;
        FString BusyMessage;
        FString NotFoundCode;
        FString NotFoundMessage;
        FString ExpiredCode;
        FString ExpiredMessage;
        FString LostCode;
        FString LostMessage;
        int32 PollAfterMs = 1000;

        // Builds the poll arguments object for this frontend, given the
        // execution id (for example `{operation: "poll", executionId: id}`).
        TFunction<TSharedPtr<FJsonObject>(const FString& ExecutionId)>
            BuildPollArguments;
    };

    struct FRecord
    {
        FString Id;
        FString Namespace;
        EState State = EState::Pending;
        bool bExposed = false;
        double StartedAtSeconds = 0.0;
        double TerminalAtSeconds = 0.0;
        FString TerminalJson;
    };
    using FRecordPtr = TSharedPtr<FRecord, ESPMode::ThreadSafe>;

    void RegisterProfile(const FProfile& Profile);
    void Startup();
    void Shutdown();

    /**
     * Allocate a record and make it the active execution for its Namespace.
     * On busy or shutdown failure, OutError is a dispatch error and
     * OutBusyExecutionId carries the active exposed id when available.
     */
    FRecordPtr Allocate(
        const FString& Namespace,
        FString& OutError,
        FString& OutBusyExecutionId);

    void Begin(const FRecordPtr& Record);
    void Complete(const FRecordPtr& Record, const FString& TerminalJson);
    void Remove(const FRecordPtr& Record);
    bool IsActive(const FRecordPtr& Record) const;
    FString ActiveId(const FString& Namespace) const;

    /**
     * Terminal/active snapshot used by frontend initial responses and polls.
     * Running snapshots expose the frontend continuation; terminal snapshots
     * replay the retained terminal JSON with no replay of execution.
     */
    TSharedPtr<FJsonObject> Snapshot(
        const FRecordPtr& Record,
        bool bIncludeExecutionId,
        bool bExposeIfRunning);

    TSharedPtr<FJsonObject> Poll(
        const FString& Namespace,
        const FString& ExecutionId,
        FString& OutError);

    /** Expire retained terminals beyond the retention window; call from the
     *  frontend tick. */
    void Tick();

    FRecordPtr FindRecord(const FString& ExecutionId) const;

private:
    const FProfile* ProfileLocked(const FString& Namespace) const;
    void CleanupExpiredLocked(double NowSeconds);
    TSharedPtr<FJsonObject> RunningSnapshotLocked(
        const FRecordPtr& Record) const;

    mutable FCriticalSection Mutex;
    TMap<FString, FProfile> Profiles;
    TMap<FString, FRecordPtr> Records;
    TMap<FString, double> ExpiredIds;
    TMap<FString, FString> ActiveIds; // namespace -> active record id
    bool bShuttingDown = false;
    static constexpr double TerminalRetentionSeconds = 30.0 * 60.0;
};
}
