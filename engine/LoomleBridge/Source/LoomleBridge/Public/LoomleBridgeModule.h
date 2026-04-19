#pragma once

#include "Containers/Ticker.h"
#include "CoreMinimal.h"
#include "HAL/CriticalSection.h"
#include "Modules/ModuleManager.h"

class FLoomlePipeServer;
class FMcpCoreTransportHost;
class FJsonObject;
class FJsonValue;
class FOutputDevice;

class FLoomleBridgeModule : public IModuleInterface
{
public:
    virtual void StartupModule() override;
    virtual void ShutdownModule() override;

private:
    friend class FMcpCoreTransportHost;

    FString HandleRequest(int32 ConnectionSerial, const FString& RequestLine);
    void ForgetMcpSessionState(int32 ConnectionSerial);
    bool TickHealthSnapshot(float DeltaTime);
    void UpdateHealthSnapshot();
    void RegisterToolbarStatusWidget();
    void UnregisterToolbarStatusWidget();
    void RegisterToolbarMenus();
    FText GetToolbarStatusLabel() const;
    FText GetToolbarStatusTooltip() const;
    FSlateColor GetToolbarStatusColor() const;
    FString GetToolbarStatusKey() const;
    FString GetRuntimeEndpointDisplayString() const;

    TSharedPtr<FJsonObject> BuildMcpInitializeResult(const TSharedPtr<FJsonObject>& Params) const;
    TSharedPtr<FJsonObject> BuildMcpToolsListResult() const;
    TSharedPtr<FJsonObject> BuildMcpCallToolResult(
        const TSharedPtr<FJsonObject>& Params,
        bool& bOutHasJsonRpcError,
        int32& OutErrorCode,
        FString& OutErrorMessage,
        TSharedPtr<FJsonObject>& OutErrorData);
    TSharedPtr<FJsonObject> BuildLoomleToolResult() const;
    TSharedPtr<FJsonObject> BuildGraphDescriptorToolResult(const TSharedPtr<FJsonObject>& Arguments) const;

    TSharedPtr<FJsonObject> BuildRpcHealthResult() const;
    TSharedPtr<FJsonObject> BuildRpcCapabilitiesResult() const;
    TSharedPtr<FJsonObject> BuildRpcInvokeResult(const TSharedPtr<FJsonObject>& Params, bool& bOutHasError, int32& OutErrorCode, FString& OutErrorMessage, TSharedPtr<FJsonObject>& OutErrorData);

    TSharedPtr<FJsonObject> BuildGraphListToolResult(const TSharedPtr<FJsonObject>& Arguments) const;
    TSharedPtr<FJsonObject> BuildGraphResolveToolResult(const TSharedPtr<FJsonObject>& Arguments) const;
    TSharedPtr<FJsonObject> BuildGraphQueryToolResult(const TSharedPtr<FJsonObject>& Arguments) const;
    TSharedPtr<FJsonObject> BuildGraphQueryBaseResult(const TSharedPtr<FJsonObject>& Arguments) const;
    TSharedPtr<FJsonObject> BuildShapedGraphQueryResult(const TSharedPtr<FJsonObject>& BaseResult, const TSharedPtr<FJsonObject>& Arguments) const;
    TSharedPtr<FJsonObject> BuildGraphMutateToolResult(const TSharedPtr<FJsonObject>& Arguments);
    TSharedPtr<FJsonObject> BuildGetContextToolResult(const TSharedPtr<FJsonObject>& Arguments) const;
    TSharedPtr<FJsonObject> BuildSelectionTransformToolResult() const;
    TSharedPtr<FJsonObject> BuildEditorOpenToolResult(const TSharedPtr<FJsonObject>& Arguments);
    TSharedPtr<FJsonObject> BuildEditorFocusToolResult(const TSharedPtr<FJsonObject>& Arguments);
    TSharedPtr<FJsonObject> BuildEditorScreenshotToolResult(const TSharedPtr<FJsonObject>& Arguments);
    TSharedPtr<FJsonObject> BuildGraphVerifyToolResult(const TSharedPtr<FJsonObject>& Arguments);
    TSharedPtr<FJsonObject> BuildExecutePythonToolResult(const TSharedPtr<FJsonObject>& Arguments);
    TSharedPtr<FJsonObject> BuildJobsToolResult(const TSharedPtr<FJsonObject>& Arguments);
    TSharedPtr<FJsonObject> BuildProfilingToolResult(const TSharedPtr<FJsonObject>& Arguments);
    TSharedPtr<FJsonObject> BuildDiagTailToolResult(const TSharedPtr<FJsonObject>& Arguments);
    TSharedPtr<FJsonObject> BuildWidgetQueryToolResult(const TSharedPtr<FJsonObject>& Arguments) const;
    TSharedPtr<FJsonObject> BuildWidgetMutateToolResult(const TSharedPtr<FJsonObject>& Arguments);
    TSharedPtr<FJsonObject> BuildWidgetVerifyToolResult(const TSharedPtr<FJsonObject>& Arguments) const;
    TSharedPtr<FJsonObject> BuildWidgetDescribeToolResult(const TSharedPtr<FJsonObject>& Arguments) const;
    TSharedPtr<FJsonObject> DispatchTool(const FString& Name, const TSharedPtr<FJsonObject>& Arguments, bool& bOutIsError);
    int32 MapToolErrorCode(const FString& DomainCode) const;
    void InitializeDiagStore();
    void HandleLogLine(const FString& Message, ELogVerbosity::Type Verbosity, const FName& Category);
    void HandleBlueprintCompiled();
    void AppendDiagEvent(
        const FString& Severity,
        const FString& Category,
        const FString& Source,
        const FString& Message,
        const TSharedPtr<FJsonObject>& Context = nullptr);

    FString MakeJsonResponse(const TSharedPtr<FJsonValue>& Id, const TSharedPtr<FJsonObject>& Result) const;
    FString MakeJsonErrorEx(const TSharedPtr<FJsonValue>& Id, int32 Code, const FString& Message, const TSharedPtr<FJsonObject>& ErrorData) const;
    FString MakeJsonError(const TSharedPtr<FJsonValue>& Id, int32 Code, const FString& Message) const;

private:
    struct FMcpSessionState
    {
        bool bInitializeCompleted = false;
        bool bClientInitialized = false;
    };

    struct FPendingGraphLayoutState
    {
        FString GraphType;
        FString AssetPath;
        FString GraphName;
        TSet<FString> TouchedNodeIds;
        double UpdatedAtSeconds = 0.0;
    };

    struct FMutateIdempotencyEntry
    {
        FString RequestFingerprint;
        TSharedPtr<FJsonObject> Result;
        double CreatedAtSeconds = 0.0;
    };

    FString MakePendingGraphLayoutKey(const FString& GraphType, const FString& AssetPath, const FString& GraphName) const;
    void RecordPendingGraphLayoutNodes(const FString& GraphType, const FString& AssetPath, const FString& GraphName, const TArray<FString>& NodeIds);
    bool ResolvePendingGraphLayoutNodes(const FString& GraphType, const FString& AssetPath, const FString& GraphName, TArray<FString>& OutNodeIds, bool bConsume);
    bool ApplyBlueprintLayout(
        const FString& AssetPath,
        const FString& GraphName,
        const FString& Scope,
        const TArray<FString>& RequestedNodeIds,
        TArray<FString>& OutMovedNodeIds,
        FString& OutError);
    bool ApplyMaterialLayout(
        const FString& AssetPath,
        const FString& GraphName,
        const FString& Scope,
        const TArray<FString>& RequestedNodeIds,
        TArray<FString>& OutMovedNodeIds,
        FString& OutError);
    bool ApplyPcgLayout(
        const FString& AssetPath,
        const FString& GraphName,
        const FString& Scope,
        const TArray<FString>& RequestedNodeIds,
        TArray<FString>& OutMovedNodeIds,
        FString& OutError);
    void StartNextJobIfNeeded();
    void RunQueuedJob(const FString& JobId);
    void AppendJobLogLine(const FString& JobId, const FString& Level, const FString& Message);

private:
    TSharedPtr<FLoomlePipeServer, ESPMode::ThreadSafe> PipeServer;
    TUniquePtr<FMcpCoreTransportHost> McpTransportHost;
    FDelegateHandle ToolbarStartupHandle;
    TMap<int32, FMcpSessionState> McpSessionStates;
    FCriticalSection McpSessionStatesMutex;
    TMap<FString, FPendingGraphLayoutState> PendingGraphLayoutStates;
    FCriticalSection PendingGraphLayoutStatesMutex;
    TMap<FString, FMutateIdempotencyEntry> MutateIdempotencyRegistry;
    FCriticalSection MutateIdempotencyRegistryMutex;

    struct FJobLogEntry
    {
        FString Time;
        FString Level;
        FString Message;
    };

    struct FToolJobEntry
    {
        FString JobId;
        FString ToolName;
        FString Status;
        FString RequestFingerprint;
        FString IdempotencyKey;
        FString Label;
        TSharedPtr<FJsonObject> BusinessArguments;
        TSharedPtr<FJsonObject> FinalPayload;
        TSharedPtr<FJsonObject> ErrorPayload;
        TArray<FJobLogEntry> Logs;
        FDateTime AcceptedAt;
        FDateTime StartedAt;
        FDateTime FinishedAt;
        FDateTime HeartbeatAt;
        int32 PollAfterMs = 1000;
        int32 ResultTtlMs = 3600000;
        bool bResultAvailable = false;
    };

    TMap<FString, FToolJobEntry> JobRegistry;
    TArray<FString> JobQueue;
    FString ActiveJobId;
    uint64 NextJobId = 1;
    FCriticalSection JobRegistryMutex;
    bool bJobRunnerActive = false;

    FCriticalSection DiagStoreMutex;
    FString DiagStoreDirPath;
    FString DiagStoreFilePath;
    uint64 NextDiagSeq = 1;
    bool bDiagStoreInitialized = false;
    FOutputDevice* DiagLogOutputDevice = nullptr;
    FDelegateHandle BlueprintCompiledHandle;
    TSet<FString> BlueprintCompileErrorAssets;
    TAtomic<bool> bGraphMutateInProgress { false };
    TAtomic<bool> bBridgeRunningSnapshot { false };
    TAtomic<bool> bPythonReadySnapshot { false };
    TAtomic<bool> bIsPIESnapshot { false };
    FTSTicker::FDelegateHandle HealthSnapshotTickerHandle;
};
