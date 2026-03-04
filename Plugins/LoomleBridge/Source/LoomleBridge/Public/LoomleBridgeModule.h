#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

class FLoomlePipeServer;
class FJsonObject;
class FJsonValue;
struct FEdGraphSchemaAction;

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
    TSharedPtr<FJsonObject> BuildGraphListToolResult(const TSharedPtr<FJsonObject>& Arguments) const;
    TSharedPtr<FJsonObject> BuildGraphQueryToolResult(const TSharedPtr<FJsonObject>& Arguments) const;
    TSharedPtr<FJsonObject> BuildGraphAddableToolResult(const TSharedPtr<FJsonObject>& Arguments);
    TSharedPtr<FJsonObject> BuildGraphMutateToolResult(const TSharedPtr<FJsonObject>& Arguments);
    TSharedPtr<FJsonObject> BuildGetContextToolResult(const TSharedPtr<FJsonObject>& Arguments) const;
    TSharedPtr<FJsonObject> BuildSelectionTransformToolResult() const;
    TSharedPtr<FJsonObject> BuildExecutePythonToolResult(const TSharedPtr<FJsonObject>& Arguments) const;

    FString MakeJsonResponse(const TSharedPtr<FJsonValue>& Id, const TSharedPtr<FJsonObject>& Result) const;
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

    void PruneGraphActionTokenRegistry();
    bool ResolveGraphActionToken(const FString& ActionToken, const FString& GraphType, const FString& AssetPath, const FString& GraphName, FGraphActionTokenEntry& OutEntry, FString& OutErrorCode, FString& OutErrorMessage);

private:
    TSharedPtr<FLoomlePipeServer, ESPMode::ThreadSafe> PipeServer;
    TMap<FString, FGraphActionTokenEntry> GraphActionTokenRegistry;
    bool bGraphMutateInProgress = false;
};
