// Copyright 2026 Loomle contributors.

#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "HAL/CriticalSection.h"

class FJsonObject;
struct FLoomleBridgeRpcTestAccess;

namespace Loomle::Runtime
{
class FGameThreadAdmission;
class FLoomleAsyncKernel;
}

namespace Loomle::Python
{

class LOOMLEBRIDGE_API FPythonExecutionService final
{
    friend struct ::FLoomleBridgeRpcTestAccess;

public:
    struct FExecution;
    using FExecutionPtr = TSharedPtr<FExecution, ESPMode::ThreadSafe>;

    void Startup(TFunction<void()> InGameThreadProgress = TFunction<void()>());
    void Shutdown();

    FExecutionPtr PrepareRun(
        const TSharedPtr<FJsonObject>& Arguments,
        TSharedPtr<FJsonObject>& OutError);
    bool EnqueueForExecution(
        const FExecutionPtr& Execution,
        const TSharedRef<Loomle::Runtime::FGameThreadAdmission, ESPMode::ThreadSafe>& Admission);
    void AbandonBeforeStart(const FExecutionPtr& Execution);
    bool IsTerminal(const FExecutionPtr& Execution) const;
    TSharedPtr<FJsonObject> BuildInitialResponse(const FExecutionPtr& Execution);
    TSharedPtr<FJsonObject> Poll(
        const TSharedPtr<FJsonObject>& Arguments,
        TSharedPtr<FJsonObject>& OutError);

private:
    bool TickExecution(float DeltaTime);
    void Execute(const FExecutionPtr& Execution);
    void RemoveExecutionLocked(const FExecutionPtr& Execution);

private:
    mutable FCriticalSection Mutex;
    TMap<FString, FExecutionPtr> Executions;
    FExecutionPtr PendingExecution;
    TSharedPtr<Loomle::Runtime::FGameThreadAdmission, ESPMode::ThreadSafe> PendingAdmission;
    TSharedPtr<Loomle::Runtime::FLoomleAsyncKernel> Kernel;
    TFunction<void()> GameThreadProgress;
    FTSTicker::FDelegateHandle ExecutionTickerHandle;
    bool bShuttingDown = false;
};

} // namespace Loomle::Python
