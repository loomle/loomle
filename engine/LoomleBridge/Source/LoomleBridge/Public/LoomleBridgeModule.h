#pragma once

#include "Containers/Ticker.h"
#include "CoreMinimal.h"
#include "HAL/CriticalSection.h"
#include "Modules/ModuleManager.h"

class FLoomlePipeServer;
class FJsonObject;
class FJsonValue;
class FOutputDevice;

class FLoomleBridgeModule : public IModuleInterface
{
public:
    virtual void StartupModule() override;
    virtual void ShutdownModule() override;

private:
    FString HandleRequest(int32 ConnectionSerial, const FString& RequestLine);
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

    TSharedPtr<FJsonObject> BuildRpcHealthResult() const;
    TSharedPtr<FJsonObject> BuildRpcCapabilitiesResult() const;
    TSharedPtr<FJsonObject> BuildRpcInvokeResult(const TSharedPtr<FJsonObject>& Params, bool& bOutHasError, int32& OutErrorCode, FString& OutErrorMessage, TSharedPtr<FJsonObject>& OutErrorData);

    // Blueprint
    TSharedPtr<FJsonObject> BuildBlueprintAssetEditToolResult(const TSharedPtr<FJsonObject>& Arguments);
    TSharedPtr<FJsonObject> BuildBlueprintMemberEditToolResult(const TSharedPtr<FJsonObject>& Arguments);
    TSharedPtr<FJsonObject> BuildBlueprintListToolResult(const TSharedPtr<FJsonObject>& Arguments) const;
    TSharedPtr<FJsonObject> BuildBlueprintQueryToolResult(const TSharedPtr<FJsonObject>& Arguments) const;
    TSharedPtr<FJsonObject> BuildBlueprintMutateToolResult(const TSharedPtr<FJsonObject>& Arguments);
    TSharedPtr<FJsonObject> BuildBlueprintVerifyToolResult(const TSharedPtr<FJsonObject>& Arguments);
    TSharedPtr<FJsonObject> BuildBlueprintDescribeToolResult(const TSharedPtr<FJsonObject>& Arguments) const;
    TSharedPtr<FJsonObject> BuildBlueprintPaletteToolResult(const TSharedPtr<FJsonObject>& Arguments) const;

    // Material
    TSharedPtr<FJsonObject> BuildMaterialListToolResult(const TSharedPtr<FJsonObject>& Arguments) const;
    TSharedPtr<FJsonObject> BuildMaterialQueryToolResult(const TSharedPtr<FJsonObject>& Arguments) const;
    TSharedPtr<FJsonObject> BuildMaterialMutateToolResult(const TSharedPtr<FJsonObject>& Arguments);
    TSharedPtr<FJsonObject> BuildMaterialVerifyToolResult(const TSharedPtr<FJsonObject>& Arguments);
    TSharedPtr<FJsonObject> BuildMaterialDescribeToolResult(const TSharedPtr<FJsonObject>& Arguments) const;

    // PCG
    TSharedPtr<FJsonObject> BuildPcgListToolResult(const TSharedPtr<FJsonObject>& Arguments) const;
    TSharedPtr<FJsonObject> BuildPcgQueryToolResult(const TSharedPtr<FJsonObject>& Arguments) const;
    TSharedPtr<FJsonObject> BuildPcgMutateToolResult(const TSharedPtr<FJsonObject>& Arguments);
    TSharedPtr<FJsonObject> BuildPcgVerifyToolResult(const TSharedPtr<FJsonObject>& Arguments);
    TSharedPtr<FJsonObject> BuildPcgDescribeToolResult(const TSharedPtr<FJsonObject>& Arguments) const;

    TSharedPtr<FJsonObject> BuildGetContextToolResult(const TSharedPtr<FJsonObject>& Arguments) const;
    TSharedPtr<FJsonObject> BuildSelectionTransformToolResult() const;
    TSharedPtr<FJsonObject> BuildEditorOpenToolResult(const TSharedPtr<FJsonObject>& Arguments);
    TSharedPtr<FJsonObject> BuildEditorFocusToolResult(const TSharedPtr<FJsonObject>& Arguments);
    TSharedPtr<FJsonObject> BuildEditorScreenshotToolResult(const TSharedPtr<FJsonObject>& Arguments);
    TSharedPtr<FJsonObject> BuildExecutePythonToolResult(const TSharedPtr<FJsonObject>& Arguments);
    TSharedPtr<FJsonObject> BuildJobsToolResult(const TSharedPtr<FJsonObject>& Arguments);
    TSharedPtr<FJsonObject> BuildProfilingToolResult(const TSharedPtr<FJsonObject>& Arguments);
    TSharedPtr<FJsonObject> BuildPlayToolResult(const TSharedPtr<FJsonObject>& Arguments);
    TSharedPtr<FJsonObject> BuildDiagnosticTailToolResult(const TSharedPtr<FJsonObject>& Arguments);
    TSharedPtr<FJsonObject> BuildLogTailToolResult(const TSharedPtr<FJsonObject>& Arguments);
    TSharedPtr<FJsonObject> BuildWidgetQueryToolResult(const TSharedPtr<FJsonObject>& Arguments) const;
    TSharedPtr<FJsonObject> BuildWidgetMutateToolResult(const TSharedPtr<FJsonObject>& Arguments);
    TSharedPtr<FJsonObject> BuildWidgetVerifyToolResult(const TSharedPtr<FJsonObject>& Arguments) const;
    TSharedPtr<FJsonObject> BuildWidgetDescribeToolResult(const TSharedPtr<FJsonObject>& Arguments) const;
    TSharedPtr<FJsonObject> DispatchTool(const FString& Name, const TSharedPtr<FJsonObject>& Arguments, bool& bOutIsError);
    int32 MapToolErrorCode(const FString& DomainCode) const;
    void InitializeDiagnosticStore();
    void InitializeLogStore();
    void HandleLogLine(const FString& Message, ELogVerbosity::Type Verbosity, const FName& Category);
    void HandleBlueprintCompiled();
    void AppendDiagnosticEvent(
        const FString& Severity,
        const FString& Category,
        const FString& Source,
        const FString& Message,
        const TSharedPtr<FJsonObject>& Context = nullptr);
    void AppendLogEvent(
        const FString& Verbosity,
        const FString& Category,
        const FString& Source,
        const FString& Message,
        const TSharedPtr<FJsonObject>& Context = nullptr);

    FString MakeJsonResponse(const TSharedPtr<FJsonValue>& Id, const TSharedPtr<FJsonObject>& Result) const;
    FString MakeJsonErrorEx(const TSharedPtr<FJsonValue>& Id, int32 Code, const FString& Message, const TSharedPtr<FJsonObject>& ErrorData) const;
    FString MakeJsonError(const TSharedPtr<FJsonValue>& Id, int32 Code, const FString& Message) const;

private:
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
    void WriteRuntimeRegistration();
    void RemoveRuntimeRegistration();

private:
    TSharedPtr<FLoomlePipeServer, ESPMode::ThreadSafe> PipeServer;
    FDelegateHandle ToolbarStartupHandle;
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

    FCriticalSection DiagnosticStoreMutex;
    FString DiagnosticStoreDirPath;
    FString DiagnosticStoreFilePath;
    uint64 NextDiagnosticSeq = 1;
    bool bDiagnosticStoreInitialized = false;
    FCriticalSection LogStoreMutex;
    FString LogStoreDirPath;
    FString LogStoreFilePath;
    uint64 NextLogSeq = 1;
    bool bLogStoreInitialized = false;
    FOutputDevice* LogCaptureOutputDevice = nullptr;
    FDelegateHandle BlueprintCompiledHandle;
    FString RuntimeRegistrationPath;
    TSet<FString> BlueprintCompileErrorAssets;
    TAtomic<bool> bGraphMutateInProgress { false };
    TAtomic<bool> bBridgeRunningSnapshot { false };
    TAtomic<bool> bPythonReadySnapshot { false };
    TAtomic<bool> bIsPIESnapshot { false };
    TMap<FString, FIntPoint> LastPlayRequestedWindowPositions;
    FIntPoint LastPlayRequestedWindowSize { 0, 0 };
    FTSTicker::FDelegateHandle HealthSnapshotTickerHandle;
};
