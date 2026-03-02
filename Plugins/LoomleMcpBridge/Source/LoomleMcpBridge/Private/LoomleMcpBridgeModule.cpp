#include "LoomleMcpBridgeModule.h"

#include "Async/Async.h"
#include "Editor.h"
#include "EditorContextProviderRegistry.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Algo/Sort.h"
#include "Components/ActorComponent.h"
#include "Components/BoxComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Containers/Ticker.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "Engine/Engine.h"
#include "Engine/Selection.h"
#include "Engine/SCS_Node.h"
#include "Engine/SimpleConstructionScript.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Character.h"
#include "HAL/PlatformTime.h"
#include "IPythonScriptPlugin.h"
#include "Json.h"
#include "HAL/FileManager.h"
#include "K2Node_CallFunction.h"
#include "K2Node_DynamicCast.h"
#include "K2Node_Event.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "EdGraphSchema_K2.h"
#include "Engine/Blueprint.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpression.h"
#include "ContextProviders/BlueprintContextProvider.h"
#include "ContextProviders/MaterialContextProvider.h"
#include "LoomleMcpPipeServer.h"
#include "Misc/App.h"
#include "Misc/DateTime.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/UnrealType.h"

DEFINE_LOG_CATEGORY_STATIC(LogLoomleMcpBridge, Log, All);

using FCondensedJsonWriter = TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>;

namespace LoomleMcpBridgeConstants
{
    static const TCHAR* PipeName = TEXT("loomle-mcp");
    static const TCHAR* LiveToolName = TEXT("live");
    static const TCHAR* ExecuteToolName = TEXT("execute");
    static const TCHAR* LiveNotificationMethod = TEXT("notifications/live");
    static const TCHAR* LiveLogFileName = TEXT("live_events.jsonl");
    constexpr int32 MaxLiveLogRecords = 100;
    constexpr int32 MaxLiveMemoryRecords = 300;
    constexpr int32 LiveLogTrimInterval = 25;
}

namespace
{
TSharedPtr<FJsonObject> ToVectorJson(const FVector& Value)
{
    TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
    Obj->SetNumberField(TEXT("x"), Value.X);
    Obj->SetNumberField(TEXT("y"), Value.Y);
    Obj->SetNumberField(TEXT("z"), Value.Z);
    return Obj;
}

TSharedPtr<FJsonObject> ToRotatorJson(const FRotator& Value)
{
    TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
    Obj->SetNumberField(TEXT("pitch"), Value.Pitch);
    Obj->SetNumberField(TEXT("yaw"), Value.Yaw);
    Obj->SetNumberField(TEXT("roll"), Value.Roll);
    return Obj;
}

TSharedPtr<FJsonObject> ToTransformJson(const FTransform& Value)
{
    TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
    Obj->SetObjectField(TEXT("location"), ToVectorJson(Value.GetLocation()));
    Obj->SetObjectField(TEXT("rotation"), ToRotatorJson(Value.Rotator()));
    Obj->SetObjectField(TEXT("scale"), ToVectorJson(Value.GetScale3D()));
    return Obj;
}

bool IsLiveLifecycleEvent(const FString& EventName)
{
    return EventName.Equals(TEXT("live_started")) || EventName.Equals(TEXT("live_stopping"));
}

bool IsLikelySystemActorIdentity(const FString& ActorName, const FString& ActorPath, const FString& ClassPath)
{
    if (ClassPath.Equals(TEXT("/Script/Engine.DefaultPhysicsVolume"))
        || ClassPath.Equals(TEXT("/Script/GameplayDebugger.GameplayDebuggerPlayerManager"))
        || ClassPath.Equals(TEXT("/Script/NavigationSystem.AbstractNavData")))
    {
        return true;
    }

    return ActorName.Contains(TEXT("DefaultPhysicsVolume"))
        || ActorName.Contains(TEXT("GameplayDebugger"))
        || ActorName.Contains(TEXT("ChaosDebugDraw"))
        || ActorName.Contains(TEXT("AbstractNavData"))
        || ActorPath.Contains(TEXT("DefaultPhysicsVolume"))
        || ActorPath.Contains(TEXT("GameplayDebugger"))
        || ActorPath.Contains(TEXT("ChaosDebugDraw"))
        || ActorPath.Contains(TEXT("AbstractNavData"));
}

FString DeriveEventOrigin(const FString& EventName, const TSharedPtr<FJsonObject>& Data)
{
    if (IsLiveLifecycleEvent(EventName))
    {
        return TEXT("system");
    }

    if (EventName.Equals(TEXT("selection_changed"))
        || EventName.Equals(TEXT("graph_selection_changed"))
        || EventName.Equals(TEXT("actor_moved"))
        || EventName.Equals(TEXT("actor_deleted"))
        || EventName.Equals(TEXT("actor_attached"))
        || EventName.Equals(TEXT("actor_detached"))
        || EventName.Equals(TEXT("pie_started"))
        || EventName.Equals(TEXT("pie_stopped"))
        || EventName.Equals(TEXT("pie_paused"))
        || EventName.Equals(TEXT("pie_resumed"))
        || EventName.Equals(TEXT("undo_redo")))
    {
        return TEXT("user");
    }

    if (EventName.Equals(TEXT("actor_added")) && Data.IsValid())
    {
        FString Name;
        FString Path;
        FString ClassPath;
        Data->TryGetStringField(TEXT("name"), Name);
        Data->TryGetStringField(TEXT("path"), Path);
        Data->TryGetStringField(TEXT("class"), ClassPath);
        return IsLikelySystemActorIdentity(Name, Path, ClassPath) ? TEXT("system") : TEXT("user");
    }

    if (EventName.Equals(TEXT("object_property_changed")) && Data.IsValid())
    {
        FString ObjectClass;
        FString Property;
        FString ActorName;
        FString ActorPath;
        Data->TryGetStringField(TEXT("objectClass"), ObjectClass);
        Data->TryGetStringField(TEXT("property"), Property);
        Data->TryGetStringField(TEXT("actorName"), ActorName);
        Data->TryGetStringField(TEXT("actorPath"), ActorPath);

        if (ObjectClass.Equals(TEXT("/Script/NavigationSystem.AbstractNavData"))
            || Property.Equals(TEXT("ActorLabel"))
            || IsLikelySystemActorIdentity(ActorName, ActorPath, ObjectClass))
        {
            return TEXT("system");
        }
        return TEXT("user");
    }

    return TEXT("unknown");
}

bool ShouldReturnInLivePull(const TSharedPtr<FJsonObject>& EventObject)
{
    if (!EventObject.IsValid())
    {
        return false;
    }

    const TSharedPtr<FJsonObject>* Params = nullptr;
    if (!EventObject->TryGetObjectField(TEXT("params"), Params) || Params == nullptr || !(*Params).IsValid())
    {
        return true;
    }

    FString EventName;
    if (!(*Params)->TryGetStringField(TEXT("event"), EventName))
    {
        return true;
    }

    return !IsLiveLifecycleEvent(EventName);
}

}

void FLoomleMcpBridgeModule::StartupModule()
{
    ContextProviderRegistry = MakeUnique<FEditorContextProviderRegistry>();
    ContextProviderRegistry->RegisterProvider(MakeShared<FBlueprintContextProvider>());
    ContextProviderRegistry->RegisterProvider(MakeShared<FMaterialContextProvider>());

    PipeServer = MakeShared<FLoomleMcpPipeServer, ESPMode::ThreadSafe>(
        LoomleMcpBridgeConstants::PipeName,
        [this](const FString& RequestLine)
        {
            return HandleRequest(RequestLine);
        });

    if (!PipeServer->Start())
    {
        UE_LOG(LogLoomleMcpBridge, Error, TEXT("Failed to start MCP pipe server."));
        PipeServer.Reset();
        return;
    }

#if PLATFORM_WINDOWS
    UE_LOG(LogLoomleMcpBridge, Display, TEXT("Loomle MCP bridge started on named pipe \\\\.\\pipe\\%s"), LoomleMcpBridgeConstants::PipeName);
#else
    UE_LOG(LogLoomleMcpBridge, Display, TEXT("Loomle MCP bridge started on unix socket %s"), *FPaths::Combine(FPaths::ProjectIntermediateDir(), TEXT("loomle-mcp.sock")));
#endif

    RegisterEditorStreamDelegates();
    bEditorStreamEnabled = true;
    NextLiveSequence = 1;
    LiveLogAppendSinceTrim = 0;
    LiveEventBuffer.Empty();
    bSelectionEventPending = false;
    PendingSelectionCount = 0;
    PendingSelectionSignature.Empty();
    PendingSelectionPaths.Empty();
    LastEmittedSelectionSignature.Empty();
    LastEmittedGraphSelectionSignature.Empty();
    LastSelectionChangeTimeSeconds = 0.0;
    LastActorTransformByPath.Empty();
    LastSceneComponentRelativeTransformByPath.Empty();

    TSharedPtr<FJsonObject> StartedData = MakeShared<FJsonObject>();
    StartedData->SetStringField(TEXT("reason"), TEXT("mcp_started"));
    StartedData->SetStringField(TEXT("message"), TEXT("live started"));
    SendEditorStreamEvent(TEXT("live_started"), StartedData);
}

void FLoomleMcpBridgeModule::ShutdownModule()
{
    if (bEditorStreamEnabled)
    {
        TSharedPtr<FJsonObject> StoppedData = MakeShared<FJsonObject>();
        StoppedData->SetStringField(TEXT("reason"), TEXT("mcp_stopping"));
        StoppedData->SetStringField(TEXT("message"), TEXT("live stopping"));
        SendEditorStreamEvent(TEXT("live_stopping"), StoppedData);
    }

    UnregisterEditorStreamDelegates();
    bEditorStreamEnabled = false;
    LiveLogAppendSinceTrim = 0;

    if (PipeServer.IsValid())
    {
        PipeServer->StopServer();
        PipeServer.Reset();
    }

    if (ContextProviderRegistry)
    {
        ContextProviderRegistry->ClearProviders();
    }
    ContextProviderRegistry.Reset();
}

FString FLoomleMcpBridgeModule::HandleRequest(const FString& RequestLine)
{
    TSharedPtr<FJsonObject> RequestObject;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(RequestLine);

    if (!FJsonSerializer::Deserialize(Reader, RequestObject) || !RequestObject.IsValid())
    {
        return MakeJsonError(MakeShared<FJsonValueNull>(), -32700, TEXT("Parse error"));
    }

    TSharedPtr<FJsonValue> IdValue = RequestObject->TryGetField(TEXT("id"));
    if (!IdValue.IsValid())
    {
        IdValue = MakeShared<FJsonValueNull>();
    }

    FString Method;
    if (!RequestObject->TryGetStringField(TEXT("method"), Method))
    {
        return MakeJsonError(IdValue, -32600, TEXT("Invalid Request: method is required"));
    }

    const TSharedPtr<FJsonObject>* ParamsPtr = nullptr;
    RequestObject->TryGetObjectField(TEXT("params"), ParamsPtr);
    const TSharedPtr<FJsonObject> Params = ParamsPtr ? *ParamsPtr : MakeShared<FJsonObject>();

    if (Method.Equals(TEXT("initialize")))
    {
        return MakeJsonResponse(IdValue, BuildInitializeResult(Params));
    }

    if (Method.Equals(TEXT("notifications/initialized")))
    {
        return FString();
    }

    if (Method.Equals(TEXT("tools/list")))
    {
        return MakeJsonResponse(IdValue, BuildToolsListResult());
    }

    if (Method.Equals(TEXT("tools/call")))
    {
        return MakeJsonResponse(IdValue, BuildToolCallResult(Params));
    }

    return MakeJsonError(IdValue, -32601, FString::Printf(TEXT("Method not found: %s"), *Method));
}

TSharedPtr<FJsonObject> FLoomleMcpBridgeModule::BuildInitializeResult(const TSharedPtr<FJsonObject>&) const
{
    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetStringField(TEXT("protocolVersion"), TEXT("2025-06-18"));

    TSharedPtr<FJsonObject> ServerInfo = MakeShared<FJsonObject>();
    ServerInfo->SetStringField(TEXT("name"), TEXT("Loomle Bridge"));
    ServerInfo->SetStringField(TEXT("version"), TEXT("0.1.0"));
    Result->SetObjectField(TEXT("serverInfo"), ServerInfo);

    TSharedPtr<FJsonObject> Capabilities = MakeShared<FJsonObject>();
    Capabilities->SetObjectField(TEXT("tools"), MakeShared<FJsonObject>());
    Result->SetObjectField(TEXT("capabilities"), Capabilities);

    return Result;
}

TSharedPtr<FJsonObject> FLoomleMcpBridgeModule::BuildToolsListResult() const
{
    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    TArray<TSharedPtr<FJsonValue>> Tools;

    auto MakeTool = [](const FString& Name, const FString& Description, const TSharedPtr<FJsonObject>& InputSchema)
    {
        TSharedPtr<FJsonObject> Tool = MakeShared<FJsonObject>();
        Tool->SetStringField(TEXT("name"), Name);
        Tool->SetStringField(TEXT("description"), Description);
        Tool->SetObjectField(TEXT("inputSchema"), InputSchema);
        return MakeShared<FJsonValueObject>(Tool);
    };

    {
        TSharedPtr<FJsonObject> Schema = MakeShared<FJsonObject>();
        Schema->SetStringField(TEXT("type"), TEXT("object"));
        Schema->SetArrayField(TEXT("required"), TArray<TSharedPtr<FJsonValue>>{});
        Schema->SetObjectField(TEXT("properties"), MakeShared<FJsonObject>());
        Tools.Add(MakeTool(TEXT("context"), TEXT("Get current UE editor context."), Schema));
    }

    {
        TSharedPtr<FJsonObject> Schema = MakeShared<FJsonObject>();
        Schema->SetStringField(TEXT("type"), TEXT("object"));
        Schema->SetArrayField(TEXT("required"), TArray<TSharedPtr<FJsonValue>>{});
        Schema->SetObjectField(TEXT("properties"), MakeShared<FJsonObject>());
        Tools.Add(MakeTool(TEXT("selection"), TEXT("Get current editor selection (graph nodes when available, otherwise selected actors)."), Schema));
    }

    {
        TSharedPtr<FJsonObject> Schema = MakeShared<FJsonObject>();
        Schema->SetStringField(TEXT("type"), TEXT("object"));
        Schema->SetArrayField(TEXT("required"), TArray<TSharedPtr<FJsonValue>>{});
        Schema->SetObjectField(TEXT("properties"), MakeShared<FJsonObject>());
        Tools.Add(MakeTool(TEXT("loomle"), TEXT("Inspect Loomle Bridge health and callable capabilities."), Schema));
    }

    {
        TSharedPtr<FJsonObject> Schema = MakeShared<FJsonObject>();
        Schema->SetStringField(TEXT("type"), TEXT("object"));
        Schema->SetArrayField(TEXT("required"), TArray<TSharedPtr<FJsonValue>>{});

        TSharedPtr<FJsonObject> Properties = MakeShared<FJsonObject>();
        {
            TSharedPtr<FJsonObject> CursorProperty = MakeShared<FJsonObject>();
            CursorProperty->SetStringField(TEXT("type"), TEXT("number"));
            CursorProperty->SetStringField(TEXT("description"), TEXT("Last consumed live sequence. Return events after this cursor."));
            Properties->SetObjectField(TEXT("cursor"), CursorProperty);

            TSharedPtr<FJsonObject> LimitProperty = MakeShared<FJsonObject>();
            LimitProperty->SetStringField(TEXT("type"), TEXT("number"));
            LimitProperty->SetStringField(TEXT("description"), TEXT("Maximum events to return, default 20, max 100."));
            Properties->SetObjectField(TEXT("limit"), LimitProperty);
        }

        Schema->SetObjectField(TEXT("properties"), Properties);
        Tools.Add(MakeTool(LoomleMcpBridgeConstants::LiveToolName, TEXT("Pull live editor events incrementally (auto-managed with bridge lifecycle)."), Schema));
    }

    {
        TSharedPtr<FJsonObject> Schema = MakeShared<FJsonObject>();
        Schema->SetStringField(TEXT("type"), TEXT("object"));
        TArray<TSharedPtr<FJsonValue>> Required;
        Required.Add(MakeShared<FJsonValueString>(TEXT("code")));
        Schema->SetArrayField(TEXT("required"), Required);

        TSharedPtr<FJsonObject> Properties = MakeShared<FJsonObject>();
        {
            TSharedPtr<FJsonObject> CodeProperty = MakeShared<FJsonObject>();
            CodeProperty->SetStringField(TEXT("type"), TEXT("string"));
            CodeProperty->SetStringField(TEXT("description"), TEXT("Inline Python code executed by UE PythonScriptPlugin."));
            Properties->SetObjectField(TEXT("code"), CodeProperty);

            TSharedPtr<FJsonObject> ModeProperty = MakeShared<FJsonObject>();
            ModeProperty->SetStringField(TEXT("type"), TEXT("string"));
            ModeProperty->SetStringField(TEXT("description"), TEXT("Execution mode: exec (default) or eval."));
            TArray<TSharedPtr<FJsonValue>> EnumValues;
            EnumValues.Add(MakeShared<FJsonValueString>(TEXT("exec")));
            EnumValues.Add(MakeShared<FJsonValueString>(TEXT("eval")));
            ModeProperty->SetArrayField(TEXT("enum"), EnumValues);
            Properties->SetObjectField(TEXT("mode"), ModeProperty);
        }

        Schema->SetObjectField(TEXT("properties"), Properties);
        Tools.Add(MakeTool(LoomleMcpBridgeConstants::ExecuteToolName, TEXT("Execute inline Python code inside UE Editor."), Schema));
    }

    Result->SetArrayField(TEXT("tools"), Tools);
    return Result;
}

TSharedPtr<FJsonObject> FLoomleMcpBridgeModule::BuildToolCallResult(const TSharedPtr<FJsonObject>& Params)
{
    FString Name;
    if (!Params->TryGetStringField(TEXT("name"), Name))
    {
        TSharedPtr<FJsonObject> Error = MakeShared<FJsonObject>();
        Error->SetBoolField(TEXT("isError"), true);
        Error->SetStringField(TEXT("message"), TEXT("Missing tool name."));
        return Error;
    }

    const TSharedPtr<FJsonObject>* ArgumentsPtr = nullptr;
    Params->TryGetObjectField(TEXT("arguments"), ArgumentsPtr);
    TSharedPtr<FJsonObject> Arguments = ArgumentsPtr ? *ArgumentsPtr : MakeShared<FJsonObject>();

    TSharedPtr<FJsonObject> Payload;
    bool bIsError = false;

    if (Name.Equals(TEXT("context")))
    {
        Payload = BuildGetContextToolResult();
    }
    else if (Name.Equals(TEXT("selection")))
    {
        Payload = BuildSelectionTransformToolResult();
    }
    else if (Name.Equals(TEXT("loomle")))
    {
        Payload = BuildLoomleToolResult();
        bIsError = Payload->GetBoolField(TEXT("isError"));
    }
    else if (Name.Equals(LoomleMcpBridgeConstants::LiveToolName))
    {
        Payload = BuildLiveToolResult(Arguments);
        bIsError = Payload->GetBoolField(TEXT("isError"));
    }
    else if (Name.Equals(LoomleMcpBridgeConstants::ExecuteToolName))
    {
        Payload = BuildExecutePythonToolResult(Arguments);
        bIsError = Payload->GetBoolField(TEXT("isError"));
    }
    else
    {
        bIsError = true;
        Payload = MakeShared<FJsonObject>();
        Payload->SetStringField(TEXT("message"), FString::Printf(TEXT("Unknown tool: %s"), *Name));
    }

    FString PayloadText;
    TSharedRef<FCondensedJsonWriter> Writer = TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&PayloadText);
    FJsonSerializer::Serialize(Payload.ToSharedRef(), Writer);

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    TArray<TSharedPtr<FJsonValue>> Content;

    TSharedPtr<FJsonObject> TextContent = MakeShared<FJsonObject>();
    TextContent->SetStringField(TEXT("type"), TEXT("text"));
    TextContent->SetStringField(TEXT("text"), PayloadText);
    Content.Add(MakeShared<FJsonValueObject>(TextContent));

    Result->SetArrayField(TEXT("content"), Content);
    if (bIsError)
    {
        Result->SetBoolField(TEXT("isError"), true);
    }

    return Result;
}

TSharedPtr<FJsonObject> FLoomleMcpBridgeModule::BuildGetContextToolResult() const
{
    if (ContextProviderRegistry)
    {
        TSharedPtr<FJsonObject> ProviderContext;
        FName ProviderId = NAME_None;
        if (ContextProviderRegistry->BuildActiveContextSnapshot(ProviderContext, ProviderId) && ProviderContext.IsValid())
        {
            ProviderContext->SetStringField(TEXT("source"), TEXT("provider"));
            ProviderContext->SetStringField(TEXT("providerId"), ProviderId.ToString());
            return ProviderContext;
        }
    }

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

    Result->SetBoolField(TEXT("isError"), false);
    Result->SetStringField(TEXT("projectName"), FApp::GetProjectName());
    Result->SetStringField(TEXT("projectFilePath"), FPaths::GetProjectFilePath());

    UWorld* EditorWorld = nullptr;
    if (GEditor)
    {
        EditorWorld = GEditor->GetEditorWorldContext().World();
        Result->SetBoolField(TEXT("isPIE"), GEditor->IsPlayingSessionInEditor());
    }
    else
    {
        Result->SetBoolField(TEXT("isPIE"), false);
    }

    Result->SetStringField(TEXT("editorWorld"), EditorWorld ? EditorWorld->GetName() : TEXT(""));
    Result->SetStringField(TEXT("source"), TEXT("legacy"));

    return Result;
}

TSharedPtr<FJsonObject> FLoomleMcpBridgeModule::BuildLoomleToolResult() const
{
    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetBoolField(TEXT("isError"), false);

    const bool bBridgeRunning = PipeServer.IsValid();
    const bool bLiveRunning = bEditorStreamEnabled;

    IPythonScriptPlugin* PythonScriptPlugin = IPythonScriptPlugin::Get();
    const bool bPythonModuleLoaded = PythonScriptPlugin != nullptr;
    const bool bPythonReady = bPythonModuleLoaded && PythonScriptPlugin->IsPythonInitialized();

    TSharedPtr<FJsonObject> Status = MakeShared<FJsonObject>();
    Status->SetStringField(TEXT("serverName"), TEXT("Loomle Bridge"));
    Status->SetStringField(TEXT("serverVersion"), TEXT("0.1.0"));
    Status->SetStringField(TEXT("protocolVersion"), TEXT("2025-06-18"));
    Status->SetBoolField(TEXT("bridgeRunning"), bBridgeRunning);
    Status->SetBoolField(TEXT("liveRunning"), bLiveRunning);
    Status->SetBoolField(TEXT("pythonModuleLoaded"), bPythonModuleLoaded);
    Status->SetBoolField(TEXT("pythonReady"), bPythonReady);
#if PLATFORM_WINDOWS
    Status->SetStringField(TEXT("transport"), TEXT("\\\\.\\pipe\\loomle-mcp"));
#else
    Status->SetStringField(TEXT("transport"), FPaths::Combine(FPaths::ProjectIntermediateDir(), TEXT("loomle-mcp.sock")));
#endif
    Result->SetObjectField(TEXT("status"), Status);

    auto MakeCapability = [](const FString& Name, const FString& Summary, bool bReady, const FString& Reason)
    {
        TSharedPtr<FJsonObject> Capability = MakeShared<FJsonObject>();
        Capability->SetStringField(TEXT("name"), Name);
        Capability->SetStringField(TEXT("summary"), Summary);
        Capability->SetBoolField(TEXT("ready"), bReady);
        if (!Reason.IsEmpty())
        {
            Capability->SetStringField(TEXT("reason"), Reason);
        }
        return MakeShared<FJsonValueObject>(Capability);
    };

    TArray<TSharedPtr<FJsonValue>> Capabilities;
    Capabilities.Add(MakeCapability(TEXT("loomle"), TEXT("Bridge health and capabilities."), bBridgeRunning, bBridgeRunning ? TEXT("") : TEXT("Bridge server is not running.")));
    Capabilities.Add(MakeCapability(TEXT("context"), TEXT("Current editor context."), bBridgeRunning, bBridgeRunning ? TEXT("") : TEXT("Bridge server is not running.")));
    Capabilities.Add(MakeCapability(TEXT("selection"), TEXT("Current editor selection with graph-node support."), bBridgeRunning, bBridgeRunning ? TEXT("") : TEXT("Bridge server is not running.")));
    Capabilities.Add(MakeCapability(TEXT("live"), TEXT("Incremental live event feed by cursor."), bBridgeRunning && bLiveRunning, bLiveRunning ? TEXT("") : TEXT("Live stream is not running. Restart Unreal Editor.")));
    Capabilities.Add(MakeCapability(TEXT("execute"), TEXT("Execute Python in editor."), bPythonReady, bPythonReady ? TEXT("") : TEXT("Python runtime is not initialized yet.")));
    Result->SetArrayField(TEXT("capabilities"), Capabilities);

    Result->SetStringField(
        TEXT("message"),
        bBridgeRunning ? TEXT("Loomle Bridge is running.") : TEXT("Loomle Bridge is not running. Restart Unreal Editor."));
    return Result;
}

TSharedPtr<FJsonObject> FLoomleMcpBridgeModule::BuildLiveToolResult(const TSharedPtr<FJsonObject>& Arguments) const
{
    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetBoolField(TEXT("isError"), false);
    Result->SetBoolField(TEXT("running"), bEditorStreamEnabled);

    if (!bEditorStreamEnabled)
    {
        Result->SetBoolField(TEXT("isError"), true);
        Result->SetStringField(TEXT("message"), TEXT("Live stream is not running. Please restart Unreal Editor to recover Loomle Bridge."));
        return Result;
    }

    int64 Cursor = 0;
    double CursorNumber = 0.0;
    if (Arguments->TryGetNumberField(TEXT("cursor"), CursorNumber))
    {
        Cursor = static_cast<int64>(CursorNumber);
    }
    if (Cursor < 0)
    {
        Result->SetBoolField(TEXT("isError"), true);
        Result->SetStringField(TEXT("message"), TEXT("arguments.cursor must be >= 0."));
        return Result;
    }

    int32 Limit = 20;
    double LimitNumber = 0.0;
    if (Arguments->TryGetNumberField(TEXT("limit"), LimitNumber))
    {
        Limit = FMath::Clamp(static_cast<int32>(LimitNumber), 1, LoomleMcpBridgeConstants::MaxLiveLogRecords);
    }

    TArray<TSharedPtr<FJsonValue>> Events;
    int64 NextCursor = Cursor;
    bool bDropped = false;
    bool bServedFromFile = false;

    int64 EarliestBufferSeq = NextLiveSequence;
    int64 LatestBufferSeq = Cursor;
    if (LiveEventBuffer.Num() > 0)
    {
        int64 Seq = 0;
        TSharedPtr<FJsonObject> Parsed;
        if (ParseLiveEventLine(LiveEventBuffer[0], Seq, Parsed))
        {
            EarliestBufferSeq = Seq;
        }
        if (ParseLiveEventLine(LiveEventBuffer.Last(), Seq, Parsed))
        {
            LatestBufferSeq = Seq;
        }
    }

    for (const FString& Line : LiveEventBuffer)
    {
        if (Events.Num() >= Limit)
        {
            break;
        }

        int64 Seq = 0;
        TSharedPtr<FJsonObject> EventObject;
        if (!ParseLiveEventLine(Line, Seq, EventObject) || !EventObject.IsValid())
        {
            continue;
        }
        if (Seq <= Cursor)
        {
            continue;
        }

        NextCursor = FMath::Max(NextCursor, Seq);
        if (!ShouldReturnInLivePull(EventObject))
        {
            continue;
        }

        Events.Add(MakeShared<FJsonValueObject>(EventObject));
    }

    if (Events.Num() == 0 && Cursor < EarliestBufferSeq - 1)
    {
        const FString StreamLogPath = FPaths::Combine(FPaths::ProjectDir(), TEXT("Loomle"), TEXT("runtime"), LoomleMcpBridgeConstants::LiveLogFileName);
        TArray<FString> LogLines;
        FFileHelper::LoadFileToStringArray(LogLines, *StreamLogPath);
        bServedFromFile = true;

        int64 EarliestFileSeq = MAX_int64;
        int64 LatestFileSeq = 0;
        for (const FString& Line : LogLines)
        {
            int64 Seq = 0;
            TSharedPtr<FJsonObject> EventObject;
            if (!ParseLiveEventLine(Line, Seq, EventObject) || !EventObject.IsValid())
            {
                continue;
            }
            EarliestFileSeq = FMath::Min(EarliestFileSeq, Seq);
            LatestFileSeq = FMath::Max(LatestFileSeq, Seq);

            if (Seq <= Cursor || Events.Num() >= Limit)
            {
                continue;
            }

            NextCursor = FMath::Max(NextCursor, Seq);
            if (!ShouldReturnInLivePull(EventObject))
            {
                continue;
            }

            Events.Add(MakeShared<FJsonValueObject>(EventObject));
        }

        if (EarliestFileSeq != MAX_int64 && Cursor < EarliestFileSeq - 1)
        {
            bDropped = true;
        }
        if (LatestFileSeq > 0)
        {
            NextCursor = FMath::Max(NextCursor, LatestFileSeq);
        }
    }

    Result->SetNumberField(TEXT("cursor"), static_cast<double>(Cursor));
    Result->SetNumberField(TEXT("nextCursor"), static_cast<double>(NextCursor));
    Result->SetBoolField(TEXT("dropped"), bDropped);
    Result->SetStringField(TEXT("source"), bServedFromFile ? TEXT("file") : TEXT("memory"));
    Result->SetArrayField(TEXT("events"), Events);
    Result->SetNumberField(TEXT("count"), Events.Num());

    if (Events.Num() == 0 && Cursor < LatestBufferSeq)
    {
        Result->SetStringField(TEXT("message"), TEXT("No new events."));
    }

    return Result;
}

TSharedPtr<FJsonObject> FLoomleMcpBridgeModule::BuildSelectionTransformToolResult() const
{
    if (ContextProviderRegistry)
    {
        TSharedPtr<FJsonObject> ProviderSelection;
        FName ProviderId = NAME_None;
        if (ContextProviderRegistry->BuildActiveSelectionSnapshot(ProviderSelection, ProviderId) && ProviderSelection.IsValid())
        {
            ProviderSelection->SetStringField(TEXT("source"), TEXT("provider"));
            ProviderSelection->SetStringField(TEXT("providerId"), ProviderId.ToString());
            return ProviderSelection;
        }
    }

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetBoolField(TEXT("isError"), false);

    TArray<TSharedPtr<FJsonValue>> Items;
    FBox AggregateBox(EForceInit::ForceInit);
    TSharedPtr<FJsonObject> ActiveItem;

    if (GEditor)
    {
        USelection* Selection = GEditor->GetSelectedActors();
        if (Selection)
        {
            for (FSelectionIterator It(*Selection); It; ++It)
            {
                AActor* Actor = Cast<AActor>(*It);
                if (!Actor)
                {
                    continue;
                }

                const FVector Location = Actor->GetActorLocation();
                const FRotator Rotation = Actor->GetActorRotation();
                const FVector Scale = Actor->GetActorScale3D();

                FVector BoundsOrigin(ForceInitToZero);
                FVector BoundsExtent(ForceInitToZero);
                Actor->GetActorBounds(true, BoundsOrigin, BoundsExtent);
                AggregateBox += (BoundsOrigin - BoundsExtent);
                AggregateBox += (BoundsOrigin + BoundsExtent);

                TSharedPtr<FJsonObject> Item = MakeShared<FJsonObject>();
                Item->SetStringField(TEXT("name"), Actor->GetActorLabel());
                Item->SetStringField(TEXT("path"), Actor->GetPathName());

                TSharedPtr<FJsonObject> LocationObj = MakeShared<FJsonObject>();
                LocationObj->SetNumberField(TEXT("x"), Location.X);
                LocationObj->SetNumberField(TEXT("y"), Location.Y);
                LocationObj->SetNumberField(TEXT("z"), Location.Z);
                Item->SetObjectField(TEXT("location"), LocationObj);

                TSharedPtr<FJsonObject> RotationObj = MakeShared<FJsonObject>();
                RotationObj->SetNumberField(TEXT("pitch"), Rotation.Pitch);
                RotationObj->SetNumberField(TEXT("yaw"), Rotation.Yaw);
                RotationObj->SetNumberField(TEXT("roll"), Rotation.Roll);
                Item->SetObjectField(TEXT("rotation"), RotationObj);

                TSharedPtr<FJsonObject> ScaleObj = MakeShared<FJsonObject>();
                ScaleObj->SetNumberField(TEXT("x"), Scale.X);
                ScaleObj->SetNumberField(TEXT("y"), Scale.Y);
                ScaleObj->SetNumberField(TEXT("z"), Scale.Z);
                Item->SetObjectField(TEXT("scale"), ScaleObj);

                TSharedPtr<FJsonObject> BoundsObj = MakeShared<FJsonObject>();
                TSharedPtr<FJsonObject> OriginObj = MakeShared<FJsonObject>();
                OriginObj->SetNumberField(TEXT("x"), BoundsOrigin.X);
                OriginObj->SetNumberField(TEXT("y"), BoundsOrigin.Y);
                OriginObj->SetNumberField(TEXT("z"), BoundsOrigin.Z);
                BoundsObj->SetObjectField(TEXT("origin"), OriginObj);

                TSharedPtr<FJsonObject> ExtentObj = MakeShared<FJsonObject>();
                ExtentObj->SetNumberField(TEXT("x"), BoundsExtent.X);
                ExtentObj->SetNumberField(TEXT("y"), BoundsExtent.Y);
                ExtentObj->SetNumberField(TEXT("z"), BoundsExtent.Z);
                BoundsObj->SetObjectField(TEXT("boxExtent"), ExtentObj);

                BoundsObj->SetNumberField(TEXT("sphereRadius"), BoundsExtent.Size());
                Item->SetObjectField(TEXT("bounds"), BoundsObj);

                if (!ActiveItem.IsValid())
                {
                    ActiveItem = Item;
                }

                Items.Add(MakeShared<FJsonValueObject>(Item));
            }
        }
    }

    Result->SetArrayField(TEXT("items"), Items);
    Result->SetNumberField(TEXT("count"), Items.Num());

    if (Items.Num() > 0)
    {
        TSharedPtr<FJsonObject> Aggregate = MakeShared<FJsonObject>();
        const FVector Center = AggregateBox.GetCenter();
        const FVector Extent = AggregateBox.GetExtent();

        TSharedPtr<FJsonObject> CenterObj = MakeShared<FJsonObject>();
        CenterObj->SetNumberField(TEXT("x"), Center.X);
        CenterObj->SetNumberField(TEXT("y"), Center.Y);
        CenterObj->SetNumberField(TEXT("z"), Center.Z);
        Aggregate->SetObjectField(TEXT("center"), CenterObj);

        TSharedPtr<FJsonObject> ExtentObj = MakeShared<FJsonObject>();
        ExtentObj->SetNumberField(TEXT("x"), Extent.X);
        ExtentObj->SetNumberField(TEXT("y"), Extent.Y);
        ExtentObj->SetNumberField(TEXT("z"), Extent.Z);
        Aggregate->SetObjectField(TEXT("boxExtent"), ExtentObj);

        Result->SetObjectField(TEXT("aggregate"), Aggregate);
    }

    if (ActiveItem.IsValid())
    {
        Result->SetObjectField(TEXT("active"), ActiveItem);
    }

    return Result;
}

void FLoomleMcpBridgeModule::SendEditorStreamEvent(const FString& EventName, const TSharedPtr<FJsonObject>& Data)
{
    if (!bEditorStreamEnabled || !PipeServer.IsValid())
    {
        return;
    }

    TSharedPtr<FJsonObject> Params = MakeShared<FJsonObject>();
    const int64 Sequence = NextLiveSequence++;
    Params->SetStringField(TEXT("event"), EventName);
    Params->SetNumberField(TEXT("seq"), static_cast<double>(Sequence));
    Params->SetStringField(TEXT("timestamp"), FDateTime::UtcNow().ToIso8601());
    Params->SetStringField(TEXT("origin"), DeriveEventOrigin(EventName, Data));
    Params->SetObjectField(TEXT("data"), Data);

    TSharedPtr<FJsonObject> Notification = MakeShared<FJsonObject>();
    Notification->SetStringField(TEXT("jsonrpc"), TEXT("2.0"));
    Notification->SetStringField(TEXT("method"), LoomleMcpBridgeConstants::LiveNotificationMethod);
    Notification->SetObjectField(TEXT("params"), Params);

    FString Output;
    TSharedRef<FCondensedJsonWriter> Writer = TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Output);
    FJsonSerializer::Serialize(Notification.ToSharedRef(), Writer);
    LiveEventBuffer.Add(Output);
    if (LiveEventBuffer.Num() > LoomleMcpBridgeConstants::MaxLiveMemoryRecords)
    {
        LiveEventBuffer.RemoveAt(0, LiveEventBuffer.Num() - LoomleMcpBridgeConstants::MaxLiveMemoryRecords, EAllowShrinking::No);
    }
    AppendEditorStreamEventLog(Output);

    const TSharedPtr<FLoomleMcpPipeServer, ESPMode::ThreadSafe> PipeServerSnapshot = PipeServer;
    Async(EAsyncExecution::ThreadPool, [PipeServerSnapshot, Output]()
    {
        if (!PipeServerSnapshot.IsValid())
        {
            return;
        }

        PipeServerSnapshot->SendServerNotification(Output);
    });
}

void FLoomleMcpBridgeModule::AppendEditorStreamEventLog(const FString& JsonLine)
{
    FScopeLock ScopeLock(&LiveLogMutex);

    const FString RuntimeDir = FPaths::Combine(FPaths::ProjectDir(), TEXT("Loomle"), TEXT("runtime"));
    IFileManager::Get().MakeDirectory(*RuntimeDir, true);
    const FString StreamLogPath = FPaths::Combine(RuntimeDir, LoomleMcpBridgeConstants::LiveLogFileName);

    const FString JsonLineWithNewline = JsonLine + TEXT("\n");
    FFileHelper::SaveStringToFile(
        JsonLineWithNewline,
        *StreamLogPath,
        FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM,
        &IFileManager::Get(),
        FILEWRITE_Append | FILEWRITE_AllowRead);

    ++LiveLogAppendSinceTrim;
    if (LiveLogAppendSinceTrim < LoomleMcpBridgeConstants::LiveLogTrimInterval)
    {
        return;
    }
    LiveLogAppendSinceTrim = 0;

    TArray<FString> Lines;
    if (!FFileHelper::LoadFileToStringArray(Lines, *StreamLogPath))
    {
        return;
    }

    if (Lines.Num() <= LoomleMcpBridgeConstants::MaxLiveLogRecords)
    {
        return;
    }

    Lines.RemoveAt(0, Lines.Num() - LoomleMcpBridgeConstants::MaxLiveLogRecords, EAllowShrinking::No);
    FFileHelper::SaveStringArrayToFile(
        Lines,
        *StreamLogPath,
        FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
}

bool FLoomleMcpBridgeModule::ParseLiveEventLine(const FString& JsonLine, int64& OutSeq, TSharedPtr<FJsonObject>& OutEventObject) const
{
    OutSeq = 0;
    OutEventObject.Reset();

    TSharedPtr<FJsonObject> EventObject;
    const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonLine);
    if (!FJsonSerializer::Deserialize(Reader, EventObject) || !EventObject.IsValid())
    {
        return false;
    }

    const TSharedPtr<FJsonObject>* Params = nullptr;
    if (!EventObject->TryGetObjectField(TEXT("params"), Params) || Params == nullptr || !(*Params).IsValid())
    {
        return false;
    }

    double SeqNumber = 0.0;
    if (!(*Params)->TryGetNumberField(TEXT("seq"), SeqNumber))
    {
        return false;
    }

    OutSeq = static_cast<int64>(SeqNumber);
    OutEventObject = EventObject;
    return true;
}

void FLoomleMcpBridgeModule::RegisterEditorStreamDelegates()
{
    if (!SelectionChangedHandle.IsValid())
    {
        SelectionChangedHandle = USelection::SelectionChangedEvent.AddRaw(this, &FLoomleMcpBridgeModule::OnSelectionChanged);
    }

    if (!MapOpenedHandle.IsValid())
    {
        MapOpenedHandle = FEditorDelegates::OnMapOpened.AddRaw(this, &FLoomleMcpBridgeModule::OnMapOpened);
    }

    if (GEngine && !ActorMovedHandle.IsValid())
    {
        ActorMovedHandle = GEngine->OnActorMoved().AddRaw(this, &FLoomleMcpBridgeModule::OnActorMoved);
    }

    if (GEngine && !ActorAddedHandle.IsValid())
    {
        ActorAddedHandle = GEngine->OnLevelActorAdded().AddRaw(this, &FLoomleMcpBridgeModule::OnActorAdded);
    }

    if (GEngine && !ActorDeletedHandle.IsValid())
    {
        ActorDeletedHandle = GEngine->OnLevelActorDeleted().AddRaw(this, &FLoomleMcpBridgeModule::OnActorDeleted);
    }

    if (GEngine && !ActorAttachedHandle.IsValid())
    {
        ActorAttachedHandle = GEngine->OnLevelActorAttached().AddRaw(this, &FLoomleMcpBridgeModule::OnActorAttached);
    }

    if (GEngine && !ActorDetachedHandle.IsValid())
    {
        ActorDetachedHandle = GEngine->OnLevelActorDetached().AddRaw(this, &FLoomleMcpBridgeModule::OnActorDetached);
    }

    if (!BeginPieHandle.IsValid())
    {
        BeginPieHandle = FEditorDelegates::BeginPIE.AddRaw(this, &FLoomleMcpBridgeModule::OnBeginPIE);
    }

    if (!EndPieHandle.IsValid())
    {
        EndPieHandle = FEditorDelegates::EndPIE.AddRaw(this, &FLoomleMcpBridgeModule::OnEndPIE);
    }

    if (!PausePieHandle.IsValid())
    {
        PausePieHandle = FEditorDelegates::PausePIE.AddRaw(this, &FLoomleMcpBridgeModule::OnPausePIE);
    }

    if (!ResumePieHandle.IsValid())
    {
        ResumePieHandle = FEditorDelegates::ResumePIE.AddRaw(this, &FLoomleMcpBridgeModule::OnResumePIE);
    }

    if (!PostUndoRedoHandle.IsValid())
    {
        PostUndoRedoHandle = FEditorDelegates::PostUndoRedo.AddRaw(this, &FLoomleMcpBridgeModule::OnPostUndoRedo);
    }

    if (!ObjectPropertyChangedHandle.IsValid())
    {
        ObjectPropertyChangedHandle = FCoreUObjectDelegates::OnObjectPropertyChanged.AddRaw(this, &FLoomleMcpBridgeModule::OnObjectPropertyChanged);
    }

    if (!SelectionDebounceTickerHandle.IsValid())
    {
        SelectionDebounceTickerHandle = FTSTicker::GetCoreTicker().AddTicker(
            FTickerDelegate::CreateLambda([this](float)
            {
                if (!bEditorStreamEnabled)
                {
                    return true;
                }

                if (bSelectionEventPending)
                {
                    const double Now = FPlatformTime::Seconds();
                    constexpr double DebounceSeconds = 0.1;
                    if (Now - LastSelectionChangeTimeSeconds >= DebounceSeconds)
                    {
                        TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
                        Data->SetNumberField(TEXT("count"), PendingSelectionCount);
                        Data->SetStringField(TEXT("signature"), PendingSelectionSignature);
                        TArray<TSharedPtr<FJsonValue>> PathValues;
                        PathValues.Reserve(PendingSelectionPaths.Num());
                        for (const FString& Path : PendingSelectionPaths)
                        {
                            PathValues.Add(MakeShared<FJsonValueString>(Path));
                        }
                        Data->SetArrayField(TEXT("paths"), PathValues);
                        SendEditorStreamEvent(TEXT("selection_changed"), Data);
                        LastEmittedSelectionSignature = PendingSelectionSignature;
                        bSelectionEventPending = false;
                    }
                }

                EmitGraphSelectionIfChanged();
                return true;
            }),
            0.02f);
    }
}

void FLoomleMcpBridgeModule::UnregisterEditorStreamDelegates()
{
    if (SelectionChangedHandle.IsValid())
    {
        USelection::SelectionChangedEvent.Remove(SelectionChangedHandle);
        SelectionChangedHandle.Reset();
    }

    if (MapOpenedHandle.IsValid())
    {
        FEditorDelegates::OnMapOpened.Remove(MapOpenedHandle);
        MapOpenedHandle.Reset();
    }

    if (GEngine && ActorMovedHandle.IsValid())
    {
        GEngine->OnActorMoved().Remove(ActorMovedHandle);
        ActorMovedHandle.Reset();
    }

    if (GEngine && ActorAddedHandle.IsValid())
    {
        GEngine->OnLevelActorAdded().Remove(ActorAddedHandle);
        ActorAddedHandle.Reset();
    }

    if (GEngine && ActorDeletedHandle.IsValid())
    {
        GEngine->OnLevelActorDeleted().Remove(ActorDeletedHandle);
        ActorDeletedHandle.Reset();
    }

    if (GEngine && ActorAttachedHandle.IsValid())
    {
        GEngine->OnLevelActorAttached().Remove(ActorAttachedHandle);
        ActorAttachedHandle.Reset();
    }

    if (GEngine && ActorDetachedHandle.IsValid())
    {
        GEngine->OnLevelActorDetached().Remove(ActorDetachedHandle);
        ActorDetachedHandle.Reset();
    }

    if (BeginPieHandle.IsValid())
    {
        FEditorDelegates::BeginPIE.Remove(BeginPieHandle);
        BeginPieHandle.Reset();
    }

    if (EndPieHandle.IsValid())
    {
        FEditorDelegates::EndPIE.Remove(EndPieHandle);
        EndPieHandle.Reset();
    }

    if (PausePieHandle.IsValid())
    {
        FEditorDelegates::PausePIE.Remove(PausePieHandle);
        PausePieHandle.Reset();
    }

    if (ResumePieHandle.IsValid())
    {
        FEditorDelegates::ResumePIE.Remove(ResumePieHandle);
        ResumePieHandle.Reset();
    }

    if (PostUndoRedoHandle.IsValid())
    {
        FEditorDelegates::PostUndoRedo.Remove(PostUndoRedoHandle);
        PostUndoRedoHandle.Reset();
    }

    if (ObjectPropertyChangedHandle.IsValid())
    {
        FCoreUObjectDelegates::OnObjectPropertyChanged.Remove(ObjectPropertyChangedHandle);
        ObjectPropertyChangedHandle.Reset();
    }

    if (SelectionDebounceTickerHandle.IsValid())
    {
        FTSTicker::GetCoreTicker().RemoveTicker(SelectionDebounceTickerHandle);
        SelectionDebounceTickerHandle.Reset();
    }
}

void FLoomleMcpBridgeModule::OnSelectionChanged(UObject*)
{
    int32 Count = 0;
    TArray<FString> Paths;
    if (GEditor)
    {
        USelection* Selection = GEditor->GetSelectedActors();
        if (Selection)
        {
            Count = Selection->Num();
            Paths.Reserve(Count);
            for (FSelectionIterator It(*Selection); It; ++It)
            {
                if (AActor* Actor = Cast<AActor>(*It))
                {
                    Paths.Add(Actor->GetPathName());
                }
            }
        }
    }

    Algo::Sort(Paths);
    const FString Signature = FString::Join(Paths, TEXT("|"));
    if (Signature == LastEmittedSelectionSignature)
    {
        return;
    }

    PendingSelectionCount = Count;
    PendingSelectionSignature = Signature;
    PendingSelectionPaths = MoveTemp(Paths);
    LastSelectionChangeTimeSeconds = FPlatformTime::Seconds();
    bSelectionEventPending = true;
}

void FLoomleMcpBridgeModule::EmitGraphSelectionIfChanged()
{
    TSharedPtr<FJsonObject> GraphData;
    FString GraphSignature;
    bool bHasGraphSelection = false;

    if (ContextProviderRegistry)
    {
        TSharedPtr<FJsonObject> ProviderSelection;
        FName ProviderId = NAME_None;
        if (ContextProviderRegistry->BuildActiveSelectionSnapshot(ProviderSelection, ProviderId) && ProviderSelection.IsValid())
        {
            FString SelectionKind;
            ProviderSelection->TryGetStringField(TEXT("selectionKind"), SelectionKind);
            if (SelectionKind.Equals(TEXT("graph_node")))
            {
                const TArray<TSharedPtr<FJsonValue>>* ItemsPtr = nullptr;
                TArray<FString> SignatureParts;
                if (ProviderSelection->TryGetArrayField(TEXT("items"), ItemsPtr) && ItemsPtr)
                {
                    SignatureParts.Reserve((*ItemsPtr).Num());
                    for (const TSharedPtr<FJsonValue>& ItemValue : *ItemsPtr)
                    {
                        const TSharedPtr<FJsonObject>* ItemObj = nullptr;
                        if (!ItemValue.IsValid() || !ItemValue->TryGetObject(ItemObj) || !ItemObj || !(*ItemObj).IsValid())
                        {
                            continue;
                        }

                        FString SignatureToken;
                        if (!(*ItemObj)->TryGetStringField(TEXT("id"), SignatureToken) || SignatureToken.IsEmpty())
                        {
                            (*ItemObj)->TryGetStringField(TEXT("path"), SignatureToken);
                        }
                        if (!SignatureToken.IsEmpty())
                        {
                            SignatureParts.Add(SignatureToken);
                        }
                    }
                }

                Algo::Sort(SignatureParts);
                GraphSignature = FString::Join(SignatureParts, TEXT("|"));
                ProviderSelection->SetStringField(TEXT("signature"), GraphSignature);
                ProviderSelection->SetStringField(TEXT("providerId"), ProviderId.ToString());
                GraphData = ProviderSelection;
                bHasGraphSelection = true;
            }
        }
    }

    if (bHasGraphSelection)
    {
        if (GraphSignature == LastEmittedGraphSelectionSignature)
        {
            return;
        }

        SendEditorStreamEvent(TEXT("graph_selection_changed"), GraphData);
        LastEmittedGraphSelectionSignature = GraphSignature;
    }
    else if (!LastEmittedGraphSelectionSignature.IsEmpty())
    {
        TSharedPtr<FJsonObject> ClearedGraphData = MakeShared<FJsonObject>();
        ClearedGraphData->SetStringField(TEXT("selectionKind"), TEXT("graph_node"));
        ClearedGraphData->SetStringField(TEXT("editorType"), TEXT("none"));
        ClearedGraphData->SetStringField(TEXT("assetPath"), TEXT(""));
        ClearedGraphData->SetArrayField(TEXT("assetPaths"), TArray<TSharedPtr<FJsonValue>>{});
        ClearedGraphData->SetArrayField(TEXT("items"), TArray<TSharedPtr<FJsonValue>>{});
        ClearedGraphData->SetNumberField(TEXT("count"), 0);
        ClearedGraphData->SetStringField(TEXT("signature"), TEXT(""));
        SendEditorStreamEvent(TEXT("graph_selection_changed"), ClearedGraphData);
        LastEmittedGraphSelectionSignature.Empty();
    }
}

void FLoomleMcpBridgeModule::OnMapOpened(const FString& Filename, bool bAsTemplate)
{
    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetStringField(TEXT("filename"), Filename);
    Data->SetStringField(TEXT("mapName"), FPaths::GetBaseFilename(Filename));
    Data->SetBoolField(TEXT("asTemplate"), bAsTemplate);
    SendEditorStreamEvent(TEXT("map_opened"), Data);
}

void FLoomleMcpBridgeModule::OnActorMoved(AActor* Actor)
{
    if (!Actor)
    {
        return;
    }

    const FString ActorPath = Actor->GetPathName();
    const FTransform CurrentTransform(Actor->GetActorRotation(), Actor->GetActorLocation(), Actor->GetActorScale3D());
    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetStringField(TEXT("name"), Actor->GetActorLabel());
    Data->SetStringField(TEXT("path"), ActorPath);
    Data->SetObjectField(TEXT("currentTransform"), ToTransformJson(CurrentTransform));

    if (const FTransform* PreviousTransform = LastActorTransformByPath.Find(ActorPath))
    {
        Data->SetObjectField(TEXT("previousTransform"), ToTransformJson(*PreviousTransform));

        TSharedPtr<FJsonObject> Delta = MakeShared<FJsonObject>();
        Delta->SetObjectField(TEXT("location"), ToVectorJson(CurrentTransform.GetLocation() - PreviousTransform->GetLocation()));
        Delta->SetObjectField(TEXT("rotation"), ToRotatorJson((CurrentTransform.Rotator() - PreviousTransform->Rotator()).GetNormalized()));
        Delta->SetObjectField(TEXT("scale"), ToVectorJson(CurrentTransform.GetScale3D() - PreviousTransform->GetScale3D()));
        Data->SetObjectField(TEXT("delta"), Delta);
    }

    LastActorTransformByPath.Add(ActorPath, CurrentTransform);

    SendEditorStreamEvent(TEXT("actor_moved"), Data);
}

void FLoomleMcpBridgeModule::OnActorAdded(AActor* Actor)
{
    if (!Actor)
    {
        return;
    }

    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetStringField(TEXT("name"), Actor->GetActorLabel());
    Data->SetStringField(TEXT("path"), Actor->GetPathName());
    Data->SetStringField(TEXT("class"), Actor->GetClass() ? Actor->GetClass()->GetPathName() : TEXT(""));
    Data->SetObjectField(TEXT("initialTransform"), ToTransformJson(FTransform(Actor->GetActorRotation(), Actor->GetActorLocation(), Actor->GetActorScale3D())));
    LastActorTransformByPath.Add(Actor->GetPathName(), FTransform(Actor->GetActorRotation(), Actor->GetActorLocation(), Actor->GetActorScale3D()));
    TArray<USceneComponent*> SceneComponents;
    Actor->GetComponents<USceneComponent>(SceneComponents);
    for (USceneComponent* SceneComponent : SceneComponents)
    {
        if (SceneComponent)
        {
            LastSceneComponentRelativeTransformByPath.Add(
                SceneComponent->GetPathName(),
                FTransform(SceneComponent->GetRelativeRotation(), SceneComponent->GetRelativeLocation(), SceneComponent->GetRelativeScale3D()));
        }
    }
    SendEditorStreamEvent(TEXT("actor_added"), Data);
}

void FLoomleMcpBridgeModule::OnActorDeleted(AActor* Actor)
{
    if (!Actor)
    {
        return;
    }

    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetStringField(TEXT("name"), Actor->GetActorLabel());
    Data->SetStringField(TEXT("path"), Actor->GetPathName());
    if (const FTransform* PreviousTransform = LastActorTransformByPath.Find(Actor->GetPathName()))
    {
        Data->SetObjectField(TEXT("lastKnownTransform"), ToTransformJson(*PreviousTransform));
    }
    const FString ActorPath = Actor->GetPathName();
    LastActorTransformByPath.Remove(ActorPath);
    for (auto It = LastSceneComponentRelativeTransformByPath.CreateIterator(); It; ++It)
    {
        if (It.Key().StartsWith(ActorPath + TEXT(".")))
        {
            It.RemoveCurrent();
        }
    }
    SendEditorStreamEvent(TEXT("actor_deleted"), Data);
}

void FLoomleMcpBridgeModule::OnActorAttached(AActor* Actor, const AActor* ParentActor)
{
    if (!Actor)
    {
        return;
    }

    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetStringField(TEXT("name"), Actor->GetActorLabel());
    Data->SetStringField(TEXT("path"), Actor->GetPathName());
    Data->SetStringField(TEXT("parentPath"), ParentActor ? ParentActor->GetPathName() : TEXT(""));
    SendEditorStreamEvent(TEXT("actor_attached"), Data);
}

void FLoomleMcpBridgeModule::OnActorDetached(AActor* Actor, const AActor* ParentActor)
{
    if (!Actor)
    {
        return;
    }

    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetStringField(TEXT("name"), Actor->GetActorLabel());
    Data->SetStringField(TEXT("path"), Actor->GetPathName());
    Data->SetStringField(TEXT("formerParentPath"), ParentActor ? ParentActor->GetPathName() : TEXT(""));
    SendEditorStreamEvent(TEXT("actor_detached"), Data);
}

void FLoomleMcpBridgeModule::OnBeginPIE(bool bIsSimulating)
{
    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetBoolField(TEXT("isSimulating"), bIsSimulating);
    SendEditorStreamEvent(TEXT("pie_started"), Data);
}

void FLoomleMcpBridgeModule::OnEndPIE(bool bIsSimulating)
{
    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetBoolField(TEXT("isSimulating"), bIsSimulating);
    SendEditorStreamEvent(TEXT("pie_stopped"), Data);
}

void FLoomleMcpBridgeModule::OnPausePIE(bool bIsSimulating)
{
    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetBoolField(TEXT("isSimulating"), bIsSimulating);
    SendEditorStreamEvent(TEXT("pie_paused"), Data);
}

void FLoomleMcpBridgeModule::OnResumePIE(bool bIsSimulating)
{
    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetBoolField(TEXT("isSimulating"), bIsSimulating);
    SendEditorStreamEvent(TEXT("pie_resumed"), Data);
}

void FLoomleMcpBridgeModule::OnObjectPropertyChanged(UObject* Object, FPropertyChangedEvent& PropertyChangedEvent)
{
    if (!Object)
    {
        return;
    }

    AActor* Actor = Cast<AActor>(Object);
    if (!Actor)
    {
        if (const UActorComponent* Component = Cast<UActorComponent>(Object))
        {
            Actor = Component->GetOwner();
        }
    }

    if (!Actor)
    {
        return;
    }

    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetStringField(TEXT("actorName"), Actor->GetActorLabel());
    Data->SetStringField(TEXT("actorPath"), Actor->GetPathName());
    Data->SetStringField(TEXT("objectPath"), Object->GetPathName());
    Data->SetStringField(TEXT("objectClass"), Object->GetClass() ? Object->GetClass()->GetPathName() : TEXT(""));
    Data->SetStringField(TEXT("property"), PropertyChangedEvent.GetPropertyName().ToString());
    Data->SetStringField(TEXT("memberProperty"), PropertyChangedEvent.GetMemberPropertyName().ToString());
    Data->SetNumberField(TEXT("changeType"), static_cast<int32>(PropertyChangedEvent.ChangeType));

    // Only export value from class-level member property. Nested struct leaf properties
    // (for example RelativeScale3D.Z) may not be directly addressable from UObject container.
    if (FProperty* SafeValueProperty = PropertyChangedEvent.MemberProperty)
    {
        bool bCanExportInContainer = false;
        if (const UObject* OwnerObject = SafeValueProperty->GetOwnerUObject())
        {
            if (const UClass* OwnerClass = Cast<UClass>(OwnerObject))
            {
                bCanExportInContainer = Object->IsA(OwnerClass);
            }
            else
            {
                bCanExportInContainer = true;
            }
        }

        if (bCanExportInContainer)
        {
            FString NewValue;
            SafeValueProperty->ExportText_InContainer(0, NewValue, Object, Object, Object, PPF_None);
            if (!NewValue.IsEmpty())
            {
                Data->SetStringField(TEXT("newValue"), NewValue);
            }
        }
    }

    const FString ActorPath = Actor->GetPathName();
    const FTransform CurrentActorTransform(Actor->GetActorRotation(), Actor->GetActorLocation(), Actor->GetActorScale3D());
    Data->SetObjectField(TEXT("actorCurrentTransform"), ToTransformJson(CurrentActorTransform));
    if (const FTransform* PreviousActorTransform = LastActorTransformByPath.Find(ActorPath))
    {
        Data->SetObjectField(TEXT("actorPreviousTransform"), ToTransformJson(*PreviousActorTransform));
    }
    LastActorTransformByPath.Add(ActorPath, CurrentActorTransform);

    if (USceneComponent* SceneComponent = Cast<USceneComponent>(Object))
    {
        const FString ComponentPath = SceneComponent->GetPathName();
        const FTransform CurrentRelative(
            SceneComponent->GetRelativeRotation(),
            SceneComponent->GetRelativeLocation(),
            SceneComponent->GetRelativeScale3D());
        Data->SetObjectField(TEXT("componentCurrentRelativeTransform"), ToTransformJson(CurrentRelative));

        if (const FTransform* PreviousRelative = LastSceneComponentRelativeTransformByPath.Find(ComponentPath))
        {
            Data->SetObjectField(TEXT("componentPreviousRelativeTransform"), ToTransformJson(*PreviousRelative));

            TSharedPtr<FJsonObject> Delta = MakeShared<FJsonObject>();
            Delta->SetObjectField(TEXT("location"), ToVectorJson(CurrentRelative.GetLocation() - PreviousRelative->GetLocation()));
            Delta->SetObjectField(TEXT("rotation"), ToRotatorJson((CurrentRelative.Rotator() - PreviousRelative->Rotator()).GetNormalized()));
            Delta->SetObjectField(TEXT("scale"), ToVectorJson(CurrentRelative.GetScale3D() - PreviousRelative->GetScale3D()));
            Data->SetObjectField(TEXT("componentDelta"), Delta);
        }

        LastSceneComponentRelativeTransformByPath.Add(ComponentPath, CurrentRelative);
    }

    SendEditorStreamEvent(TEXT("object_property_changed"), Data);
}

void FLoomleMcpBridgeModule::OnPostUndoRedo()
{
    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    SendEditorStreamEvent(TEXT("undo_redo"), Data);
}

TSharedPtr<FJsonObject> FLoomleMcpBridgeModule::BuildExecutePythonToolResult(const TSharedPtr<FJsonObject>& Arguments) const
{
    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

    FString Code;
    if (!Arguments->TryGetStringField(TEXT("code"), Code))
    {
        Result->SetBoolField(TEXT("isError"), true);
        Result->SetStringField(TEXT("message"), TEXT("arguments.code is required."));
        return Result;
    }

    FString Mode = TEXT("exec");
    Arguments->TryGetStringField(TEXT("mode"), Mode);
    Mode = Mode.ToLower();
    if (!Mode.Equals(TEXT("exec")) && !Mode.Equals(TEXT("eval")))
    {
        Result->SetBoolField(TEXT("isError"), true);
        Result->SetStringField(TEXT("message"), TEXT("arguments.mode must be 'exec' or 'eval'."));
        return Result;
    }

    IPythonScriptPlugin* PythonScriptPlugin = IPythonScriptPlugin::Get();
    if (PythonScriptPlugin == nullptr)
    {
        Result->SetBoolField(TEXT("isError"), true);
        Result->SetStringField(TEXT("message"), TEXT("PythonScriptPlugin module is not loaded."));
        return Result;
    }

    if (!PythonScriptPlugin->IsPythonInitialized())
    {
        PythonScriptPlugin->ForceEnablePythonAtRuntime();
        if (!PythonScriptPlugin->IsPythonInitialized())
        {
            Result->SetBoolField(TEXT("isError"), true);
            Result->SetStringField(TEXT("message"), TEXT("Python runtime is not initialized."));
            return Result;
        }
    }

    FPythonCommandEx PythonCommand;
    PythonCommand.ExecutionMode = Mode.Equals(TEXT("eval"))
        ? EPythonCommandExecutionMode::EvaluateStatement
        : EPythonCommandExecutionMode::ExecuteFile;
    PythonCommand.Command = Code;

    const bool bSuccess = PythonScriptPlugin->ExecPythonCommandEx(PythonCommand);
    Result->SetBoolField(TEXT("isError"), !bSuccess);
    Result->SetBoolField(TEXT("success"), bSuccess);
    Result->SetStringField(TEXT("mode"), Mode);
    Result->SetStringField(TEXT("result"), PythonCommand.CommandResult);

    TArray<TSharedPtr<FJsonValue>> Logs;
    for (const FPythonLogOutputEntry& Entry : PythonCommand.LogOutput)
    {
        TSharedPtr<FJsonObject> LogEntry = MakeShared<FJsonObject>();
        LogEntry->SetStringField(TEXT("type"), LexToString(Entry.Type));
        LogEntry->SetStringField(TEXT("output"), Entry.Output);
        Logs.Add(MakeShared<FJsonValueObject>(LogEntry));
    }
    Result->SetArrayField(TEXT("logs"), Logs);

    return Result;
}

FString FLoomleMcpBridgeModule::MakeJsonResponse(const TSharedPtr<FJsonValue>& Id, const TSharedPtr<FJsonObject>& Result) const
{
    TSharedPtr<FJsonObject> Response = MakeShared<FJsonObject>();
    Response->SetStringField(TEXT("jsonrpc"), TEXT("2.0"));
    Response->SetField(TEXT("id"), Id);
    Response->SetObjectField(TEXT("result"), Result);

    FString Output;
    TSharedRef<FCondensedJsonWriter> Writer = TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Output);
    FJsonSerializer::Serialize(Response.ToSharedRef(), Writer);
    return Output;
}

FString FLoomleMcpBridgeModule::MakeJsonError(const TSharedPtr<FJsonValue>& Id, int32 Code, const FString& Message) const
{
    TSharedPtr<FJsonObject> Error = MakeShared<FJsonObject>();
    Error->SetNumberField(TEXT("code"), Code);
    Error->SetStringField(TEXT("message"), Message);

    TSharedPtr<FJsonObject> Response = MakeShared<FJsonObject>();
    Response->SetStringField(TEXT("jsonrpc"), TEXT("2.0"));
    Response->SetField(TEXT("id"), Id);
    Response->SetObjectField(TEXT("error"), Error);

    FString Output;
    TSharedRef<FCondensedJsonWriter> Writer = TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Output);
    FJsonSerializer::Serialize(Response.ToSharedRef(), Writer);
    return Output;
}

IMPLEMENT_MODULE(FLoomleMcpBridgeModule, LoomleMcpBridge)
