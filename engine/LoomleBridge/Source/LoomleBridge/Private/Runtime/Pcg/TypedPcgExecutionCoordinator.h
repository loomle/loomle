// Copyright 2026 Loomle contributors.

#pragma once

#include "CoreMinimal.h"
#include "../LoomleAsyncKernel.h"
#include "TypedPcgWorldRegistry.h"

class FJsonObject;
class UPCGComponent;

namespace Loomle::Runtime
{
/**
 * Typed PCG execution frontend on the shared async kernel (Slice 4 first
 * sub-slice: Editor World generate admission, record registration, lossless
 * native task id capture, and terminal observation; no inspection,
 * cancellation, or cleanup yet).
 *
 * Every start request carries the normalized World selector, a matching
 * source-bound ticket, and the canonical `pcg_component` source Target. The
 * World registry revalidates all three atomically before admission. The
 * admitted generate runs through the shared kernel lifecycle
 * (`pending -> running -> succeeded | failed | lost`) with a typed result.
 */
class LOOMLEBRIDGE_API FTypedPcgExecutionCoordinator
{
public:
    FTypedPcgExecutionCoordinator();
    ~FTypedPcgExecutionCoordinator();

    void Startup();
    void Shutdown();

    struct FStartOptions
    {
        FTypedPcgWorldSelector Selector;
        FString SourceTarget; // canonical pcg_component target JSON
        FTypedPcgTicketPtr Ticket;
        bool bForce = true;
    };

    /**
     * Validate selector + ticket + source, admit one local generate, register
     * the kernel record before native scheduling, capture the lossless native
     * task id, and return the running kernel record. OutError is a dispatch
     * error on admission failure.
     */
    FLoomleAsyncKernel::FRecordPtr Start(
        const FStartOptions& Options,
        TSharedPtr<FJsonObject>& OutError);

    TSharedPtr<FJsonObject> Poll(
        const FString& ExecutionId,
        TSharedPtr<FJsonObject>& OutError);

    /** Prepare a source-bound World ticket through the registry. */
    FTypedPcgTicketPtr Prepare(
        const FTypedPcgWorldSelector& Selector,
        const FString& SourceTargetText,
        FString& OutError);

    /** Observe terminal native state (call from the Game Thread tick). */
    void Tick();

    FLoomleAsyncKernel& Kernel() { return Kernel_; }

private:
    struct FActiveRun
    {
        FString ExecutionId;
        FString ComponentPath;
        uint64 TaskId = 0;
        TWeakObjectPtr<UPCGComponent> Component;
        FString SourceTarget;
        FTypedPcgWorldSelector Selector;
    };

    void CompleteRun(const FActiveRun& Run, bool bSucceeded);
    void OnGraphGenerated(UPCGComponent* Component);
    void FinishRunForComponent(UPCGComponent* Component, bool bSucceeded);

    FLoomleAsyncKernel Kernel_;
    FTypedPcgWorldRegistry Registry_;
    TMap<FString, FActiveRun> Active_;
    mutable FCriticalSection Mutex_;
    bool bInitialized_ = false;
};
}
