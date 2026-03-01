#pragma once

#include "Containers/Ticker.h"
#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

class FLoomleMcpPipeServer;
class AActor;
class FJsonObject;
class FJsonValue;

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

    TSharedPtr<FJsonObject> BuildEditorStreamToolResult(const TSharedPtr<FJsonObject>& Arguments);
    TSharedPtr<FJsonObject> BuildGetContextToolResult() const;
    TSharedPtr<FJsonObject> BuildSelectionTransformToolResult() const;
    TSharedPtr<FJsonObject> BuildExecutePythonToolResult(const TSharedPtr<FJsonObject>& Arguments) const;
    void SendEditorStreamEvent(const FString& EventName, const TSharedPtr<FJsonObject>& Data) const;
    void AppendEditorStreamEventLog(const FString& JsonLine) const;
    void RegisterEditorStreamDelegates();
    void UnregisterEditorStreamDelegates();
    void OnSelectionChanged(UObject* NewSelection);
    void OnMapOpened(const FString& Filename, bool bAsTemplate);
    void OnActorMoved(AActor* Actor);

    FString MakeJsonResponse(const TSharedPtr<FJsonValue>& Id, const TSharedPtr<FJsonObject>& Result) const;
    FString MakeJsonError(const TSharedPtr<FJsonValue>& Id, int32 Code, const FString& Message) const;

private:
    TUniquePtr<FLoomleMcpPipeServer> PipeServer;
    bool bEditorStreamEnabled = false;
    bool bSelectionEventPending = false;
    int32 PendingSelectionCount = 0;
    FString PendingSelectionSignature;
    FString LastEmittedSelectionSignature;
    double LastSelectionChangeTimeSeconds = 0.0;
    FDelegateHandle SelectionChangedHandle;
    FDelegateHandle MapOpenedHandle;
    FDelegateHandle ActorMovedHandle;
    FTSTicker::FDelegateHandle SelectionDebounceTickerHandle;
};
