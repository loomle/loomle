// Copyright 2026 Loomle contributors.

#pragma once

#include "Containers/Ticker.h"
#include "CoreMinimal.h"
#include "HAL/CriticalSection.h"
#include "HAL/ThreadSafeCounter.h"
#include "Input/Reply.h"
#include "Modules/ModuleManager.h"

#include <atomic>

class FLoomlePipeServer;
enum class ELoomlePipeListenerState : uint8;
class FJsonObject;
class FJsonValue;
class FSlateStyleSet;

enum class ELoomleBridgeLifecycle : uint8
{
    Starting,
    Ready,
    Draining,
    Offline,
    Failed,
};

namespace Loomle::Runtime
{
class FRequestCancellationState;
class FRequestCancellationRegistry;
}

/**
 * Loomle's UE process host.
 *
 * The module owns only local transport, runtime discovery, Fab Client setup,
 * Editor Context, and dispatch into the three current Bridge interfaces.
 * UE object semantics live under Private/Sal rather than in this host class.
 */
class FLoomleBridgeModule final : public IModuleInterface
{
public:
    FLoomleBridgeModule();
    virtual ~FLoomleBridgeModule() override;
    virtual void StartupModule() override;
    virtual void ShutdownModule() override;

private:
#if WITH_DEV_AUTOMATION_TESTS
    friend struct FLoomleBridgeRpcTestAccess;
#endif

    FString HandleRequest(int32 ConnectionSerial, const FString& RequestLine);
    TSharedRef<Loomle::Runtime::FRequestCancellationState, ESPMode::ThreadSafe> RegisterRequestCancellation(
        int32 ConnectionSerial,
        const FString& RequestIdKey);
    void UnregisterRequestCancellation(
        int32 ConnectionSerial,
        const FString& RequestIdKey,
        const TSharedRef<Loomle::Runtime::FRequestCancellationState, ESPMode::ThreadSafe>& ExpectedState);
    bool CancelRequest(const FString& RequestIdKey);
    void CancelRequestsForConnection(int32 ConnectionSerial);

    TSharedPtr<FJsonObject> BuildRpcHealthResult() const;
    TSharedPtr<FJsonObject> BuildRpcCapabilitiesResult() const;
    TSharedPtr<FJsonObject> BuildRpcInvokeResult(
        const TSharedPtr<FJsonObject>& Params,
        bool& bOutHasError,
        int32& OutErrorCode,
        FString& OutErrorMessage,
        TSharedPtr<FJsonObject>& OutErrorData);
    TSharedPtr<FJsonObject> DispatchTool(
        const FString& Name,
        const TSharedPtr<FJsonObject>& Arguments,
        bool& bOutIsError);
    FString MakeJsonResponse(
        const TSharedPtr<FJsonValue>& Id,
        const TSharedPtr<FJsonObject>& Result) const;
    FString MakeJsonErrorEx(
        const TSharedPtr<FJsonValue>& Id,
        int32 Code,
        const FString& Message,
        const TSharedPtr<FJsonObject>& ErrorData) const;
    FString MakeJsonError(
        const TSharedPtr<FJsonValue>& Id,
        int32 Code,
        const FString& Message) const;

    bool TickHealthSnapshot(float DeltaTime);
    void UpdateHealthSnapshot();
    FString GetBridgeLifecycleName() const;
    static ELoomleBridgeLifecycle ResolveBridgeLifecycle(
        ELoomleBridgeLifecycle CurrentLifecycle,
        ELoomlePipeListenerState ListenerState);
    static FString NormalizeProjectRoot(FString RawProjectRoot);
    static FString MakeProjectIdForNormalizedRoot(FString NormalizedProjectRoot, bool bFoldCase);
    static bool RemoveLegacyProjectRegistration(
        const FString& ProjectsDirectory,
        const FString& CanonicalProjectRoot,
        const FString& CanonicalProjectId,
        bool bFoldCase);
    void InitializeRuntimeIdentity();
    void RegisterStatusBarWidget();
    void UnregisterStatusBarWidget();
    void RegisterStatusBarMenus();
    void RegisterLoomleSlateStyle();
    void UnregisterLoomleSlateStyle();
    TSharedRef<class SWidget> CreateStatusBarButtonContent();
    TSharedRef<class SWidget> CreateSetupStatusPanel();
    FText GetToolbarStatusTooltip() const;
    FSlateColor GetToolbarStatusColor() const;
    FString GetToolbarStatusKey() const;
    FString GetRuntimeEndpointDisplayString() const;
    FText GetSetupPanelNextActionText() const;
    FString GetSetupPanelSetupPrompt() const;
    FReply CopySetupPrompt();
    void RecordClientActivity(const FString& Method, const FString& ToolName);
    bool GetClientActivitySummary(FString& OutDetail) const;

    void WriteProjectRegistration(const FString& InProjectRoot, const FString& InProjectId);
    void WriteRuntimeRegistration();
    void RemoveRuntimeRegistration();
    void BeginBridgeShutdown();
    void HandleShutdownPostPackagesSaved();
    void HandleEditorPreExit();
    void HandleEnginePreExit();
    void HandlePreExit();
    void StopBridgeRuntime(bool bWaitForWorkers);

private:
    TSharedPtr<FLoomlePipeServer, ESPMode::ThreadSafe> PipeServer;
    TSharedPtr<FSlateStyleSet> LoomleSlateStyle;
    FDelegateHandle StatusBarStartupHandle;
    FDelegateHandle ShutdownPostPackagesSavedHandle;
    FDelegateHandle EditorPreExitHandle;
    FDelegateHandle EnginePreExitHandle;
    FDelegateHandle PreExitHandle;
    FTSTicker::FDelegateHandle HealthSnapshotTickerHandle;
    FString RuntimeId;
    FString ProjectId;
    FString ProjectRoot;
    FString RuntimeEndpoint;
    FString RuntimeStartedAt;
    FString RuntimeRegistrationPath;

    mutable FCriticalSection ClientActivityMutex;
    bool bHasClientActivity = false;
    FDateTime LastClientActivityAt;
    FString LastClientMethod;
    FString LastClientTool;
    uint64 ClientActivityCount = 0;

    TUniquePtr<Loomle::Runtime::FRequestCancellationRegistry> RequestCancellationRegistry;
    FThreadSafeCounter ActiveGameThreadDispatchCount;
    std::atomic<uint8> BridgeLifecycleState { static_cast<uint8>(ELoomleBridgeLifecycle::Offline) };
    std::atomic<uint64> GameThreadProgressSequence { 0 };
    std::atomic<uint64> LastGameThreadProgressCycles { 0 };
    TAtomic<bool> bBridgeRunningSnapshot { false };
    TAtomic<bool> bIsPIESnapshot { false };
    TAtomic<bool> bIsShuttingDown { false };
};
