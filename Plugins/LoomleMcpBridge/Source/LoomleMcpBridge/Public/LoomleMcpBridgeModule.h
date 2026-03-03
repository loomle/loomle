#pragma once

#include "Containers/Ticker.h"
#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

class FLoomleMcpPipeServer;
class FEditorContextProviderRegistry;
class AActor;
class FJsonObject;
class FJsonValue;
class UObject;
struct FPropertyChangedEvent;

class FLoomleMcpBridgeModule : public IModuleInterface
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
    void OnPostUndoRedo();

    FString MakeJsonResponse(const TSharedPtr<FJsonValue>& Id, const TSharedPtr<FJsonObject>& Result) const;
    FString MakeJsonError(const TSharedPtr<FJsonValue>& Id, int32 Code, const FString& Message) const;

private:
    TSharedPtr<FLoomleMcpPipeServer, ESPMode::ThreadSafe> PipeServer;
    TUniquePtr<FEditorContextProviderRegistry> ContextProviderRegistry;
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
    FTSTicker::FDelegateHandle SelectionDebounceTickerHandle;
};
