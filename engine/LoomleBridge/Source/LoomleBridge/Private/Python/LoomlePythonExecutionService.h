// Copyright 2026 Loomle contributors.

#pragma once

#include "CoreMinimal.h"
#include "HAL/CriticalSection.h"

class FJsonObject;

namespace Loomle::Python
{

class FPythonExecutionService final
{
public:
    struct FExecution;
    using FExecutionPtr = TSharedPtr<FExecution, ESPMode::ThreadSafe>;

    void Startup();
    void Shutdown();

    FExecutionPtr PrepareRun(
        const TSharedPtr<FJsonObject>& Arguments,
        TSharedPtr<FJsonObject>& OutError);
    void AbandonBeforeStart(const FExecutionPtr& Execution);
    void Execute(const FExecutionPtr& Execution);
    bool IsTerminal(const FExecutionPtr& Execution) const;
    TSharedPtr<FJsonObject> BuildInitialResponse(const FExecutionPtr& Execution);
    TSharedPtr<FJsonObject> Poll(
        const TSharedPtr<FJsonObject>& Arguments,
        TSharedPtr<FJsonObject>& OutError);

private:
    void RemoveExecutionLocked(const FExecutionPtr& Execution);
    void CleanupExpiredLocked(double NowSeconds);
    TSharedPtr<FJsonObject> BuildSnapshotLocked(
        const FExecutionPtr& Execution,
        bool bIncludeExecutionId,
        bool bExposeIfRunning);

private:
    mutable FCriticalSection Mutex;
    TMap<FString, FExecutionPtr> Executions;
    TMap<FString, double> ExpiredExecutionIds;
    FString ActiveExecutionId;
    bool bShuttingDown = false;
};

} // namespace Loomle::Python
