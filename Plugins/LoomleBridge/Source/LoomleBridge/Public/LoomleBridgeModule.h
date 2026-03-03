#pragma once

#include "Containers/Ticker.h"
#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

class FLoomlePipeServer;
class AActor;
class FJsonObject;
class FJsonValue;
class UObject;
struct FPropertyChangedEvent;
class FTransactionObjectEvent;

class FLoomleBridgeModule : public IModuleInterface
{
public:
    virtual void StartupModule() override;
    virtual void ShutdownModule() override;

private:
    FString HandleRequest(const FString& RequestLine);

    TSharedPtr<FJsonObject> BuildInitializeResult(const TSharedPtr<FJsonObject>& Params) const;
    TSharedPtr<FJsonObject> BuildToolsListResult() const;
    TSharedPtr<FJsonObject> BuildToolCallResult(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> BuildLoomleToolResult() const;
    TSharedPtr<FJsonObject> BuildGraphToolResult(const TSharedPtr<FJsonObject>& Arguments) const;
    TSharedPtr<FJsonObject> BuildGraphQueryToolResult(const TSharedPtr<FJsonObject>& Arguments) const;
    TSharedPtr<FJsonObject> BuildGraphMutateToolResult(const TSharedPtr<FJsonObject>& Arguments);
    TSharedPtr<FJsonObject> BuildGraphWatchToolResult(const TSharedPtr<FJsonObject>& Arguments) const;
    TSharedPtr<FJsonObject> BuildEventPullResult(const TSharedPtr<FJsonObject>& Arguments, const FString& ScopeFilter, const FString& AssetPathFilter, bool bFilterLifecycle) const;

    TSharedPtr<FJsonObject> BuildLiveToolResult(const TSharedPtr<FJsonObject>& Arguments) const;
    TSharedPtr<FJsonObject> BuildGetContextToolResult(const TSharedPtr<FJsonObject>& Arguments) const;
    TSharedPtr<FJsonObject> BuildSelectionTransformToolResult() const;
    TSharedPtr<FJsonObject> BuildExecutePythonToolResult(const TSharedPtr<FJsonObject>& Arguments) const;
    void SendEditorStreamEvent(const FString& EventName, const TSharedPtr<FJsonObject>& Data);
    void AppendEditorStreamEventLog(const FString& JsonLine);
    bool ParseLiveEventLine(const FString& JsonLine, int64& OutSeq, TSharedPtr<FJsonObject>& OutEventObject) const;
    void RegisterEditorStreamDelegates();
    void UnregisterEditorStreamDelegates();
    void EmitGraphSelectionIfChanged();
    void EmitDirtyBlueprintGraphChanges();
    void MarkBlueprintGraphDirty(const FString& BlueprintAssetPath, const FString& Reason);
    void EnsureBlueprintGraphBaseline(const FString& BlueprintAssetPath);
    bool CaptureBlueprintGraphSnapshot(const FString& BlueprintAssetPath, FString& OutSignature, int32& OutNodeCount, int32& OutEdgeCount, TArray<FString>& OutNodeTokens, TArray<FString>& OutEdgeTokens) const;
    static bool TryResolveBlueprintAssetPath(UObject* Object, FString& OutBlueprintAssetPath);
    void OnSelectionChanged(UObject* NewSelection);
    void OnMapOpened(const FString& Filename, bool bAsTemplate);
    void OnActorMoved(AActor* Actor);
    void OnActorAdded(AActor* Actor);
    void OnActorDeleted(AActor* Actor);
    void OnActorAttached(AActor* Actor, const AActor* ParentActor);
    void OnActorDetached(AActor* Actor, const AActor* ParentActor);
    void OnBeginPIE(bool bIsSimulating);
    void OnEndPIE(bool bIsSimulating);
    void OnPausePIE(bool bIsSimulating);
    void OnResumePIE(bool bIsSimulating);
    void OnObjectPropertyChanged(UObject* Object, FPropertyChangedEvent& PropertyChangedEvent);
    void OnObjectTransacted(UObject* Object, const FTransactionObjectEvent& TransactionEvent);
    void OnPostUndoRedo();

    FString MakeJsonResponse(const TSharedPtr<FJsonValue>& Id, const TSharedPtr<FJsonObject>& Result) const;
    FString MakeJsonError(const TSharedPtr<FJsonValue>& Id, int32 Code, const FString& Message) const;

private:
    TSharedPtr<FLoomlePipeServer, ESPMode::ThreadSafe> PipeServer;
    bool bEditorStreamEnabled = false;
    int64 NextLiveSequence = 1;
    int32 LiveLogAppendSinceTrim = 0;
    TArray<FString> LiveEventBuffer;
    FCriticalSection LiveLogMutex;
    bool bSelectionEventPending = false;
    int32 PendingSelectionCount = 0;
    FString PendingSelectionSignature;
    TArray<FString> PendingSelectionPaths;
    FString LastEmittedSelectionSignature;
    FString LastEmittedGraphSelectionSignature;
    double LastSelectionChangeTimeSeconds = 0.0;
    TMap<FString, FTransform> LastActorTransformByPath;
    TMap<FString, FTransform> LastSceneComponentRelativeTransformByPath;
    TMap<FString, FString> PendingDirtyBlueprintGraphReasons;
    TMap<FString, FString> LastBlueprintGraphSignatureByAssetPath;
    TMap<FString, int32> LastBlueprintGraphNodeCountByAssetPath;
    TMap<FString, int32> LastBlueprintGraphEdgeCountByAssetPath;
    TMap<FString, TArray<FString>> LastBlueprintGraphNodeTokensByAssetPath;
    TMap<FString, TArray<FString>> LastBlueprintGraphEdgeTokensByAssetPath;
    bool bGraphMutateInProgress = false;
    FDelegateHandle SelectionChangedHandle;
    FDelegateHandle MapOpenedHandle;
    FDelegateHandle ActorMovedHandle;
    FDelegateHandle ActorAddedHandle;
    FDelegateHandle ActorDeletedHandle;
    FDelegateHandle ActorAttachedHandle;
    FDelegateHandle ActorDetachedHandle;
    FDelegateHandle BeginPieHandle;
    FDelegateHandle EndPieHandle;
    FDelegateHandle PausePieHandle;
    FDelegateHandle ResumePieHandle;
    FDelegateHandle PostUndoRedoHandle;
    FDelegateHandle ObjectPropertyChangedHandle;
    FDelegateHandle ObjectTransactedHandle;
    FTSTicker::FDelegateHandle SelectionDebounceTickerHandle;
};
