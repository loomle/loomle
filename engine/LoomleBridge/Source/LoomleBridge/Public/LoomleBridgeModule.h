#pragma once

#include "Containers/Ticker.h"
#include "CoreMinimal.h"
#include "HAL/CriticalSection.h"
#include "Modules/ModuleManager.h"

class FLoomlePipeServer;
class FJsonObject;
class FJsonValue;
class FOutputDevice;
struct FEdGraphSchemaAction;

class FLoomleBridgeModule : public IModuleInterface
{
public:
    virtual void StartupModule() override;
    virtual void ShutdownModule() override;

private:
    FString HandleRequest(const FString& RequestLine);
    bool TickHealthSnapshot(float DeltaTime);
    void UpdateHealthSnapshot();

    TSharedPtr<FJsonObject> BuildRpcHealthResult() const;
    TSharedPtr<FJsonObject> BuildRpcCapabilitiesResult() const;
    TSharedPtr<FJsonObject> BuildRpcInvokeResult(const TSharedPtr<FJsonObject>& Params, bool& bOutHasError, int32& OutErrorCode, FString& OutErrorMessage, TSharedPtr<FJsonObject>& OutErrorData);

    TSharedPtr<FJsonObject> BuildGraphListToolResult(const TSharedPtr<FJsonObject>& Arguments) const;
    TSharedPtr<FJsonObject> BuildGraphResolveToolResult(const TSharedPtr<FJsonObject>& Arguments) const;
    TSharedPtr<FJsonObject> BuildGraphQueryToolResult(const TSharedPtr<FJsonObject>& Arguments) const;
    TSharedPtr<FJsonObject> BuildGraphActionsToolResult(const TSharedPtr<FJsonObject>& Arguments);
    TSharedPtr<FJsonObject> BuildGraphMutateToolResult(const TSharedPtr<FJsonObject>& Arguments);
    TSharedPtr<FJsonObject> BuildGetContextToolResult(const TSharedPtr<FJsonObject>& Arguments) const;
    TSharedPtr<FJsonObject> BuildSelectionTransformToolResult() const;
    TSharedPtr<FJsonObject> BuildExecutePythonToolResult(const TSharedPtr<FJsonObject>& Arguments);
    TSharedPtr<FJsonObject> BuildDiagTailToolResult(const TSharedPtr<FJsonObject>& Arguments);
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
    struct FGraphActionTokenEntry
    {
        FString GraphType;
        FString AssetPath;
        FString GraphName;
        FString FromNodeId;
        FString FromPinName;
        FString LegacyActionId;
        TSharedPtr<FEdGraphSchemaAction> Action;
        double CreatedAtSeconds = 0.0;
    };

    struct FPendingGraphLayoutState
    {
        FString GraphType;
        FString AssetPath;
        FString GraphName;
        TSet<FString> TouchedNodeIds;
        double UpdatedAtSeconds = 0.0;
    };

    void PruneGraphActionTokenRegistry();
    bool ResolveGraphActionToken(const FString& ActionToken, const FString& GraphType, const FString& AssetPath, const FString& GraphName, FGraphActionTokenEntry& OutEntry, FString& OutErrorCode, FString& OutErrorMessage);
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

private:
    TSharedPtr<FLoomlePipeServer, ESPMode::ThreadSafe> PipeServer;
    TMap<FString, FGraphActionTokenEntry> GraphActionTokenRegistry;
    FCriticalSection GraphActionTokenRegistryMutex;
    TMap<FString, FPendingGraphLayoutState> PendingGraphLayoutStates;
    FCriticalSection PendingGraphLayoutStatesMutex;
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
