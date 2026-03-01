#include "LoomleMcpBridgeModule.h"

#include "Editor.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Algo/Sort.h"
#include "Components/BoxComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Containers/Ticker.h"
#include "EdGraph/EdGraph.h"
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
#include "K2Node_CallFunction.h"
#include "K2Node_DynamicCast.h"
#include "K2Node_Event.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "EdGraphSchema_K2.h"
#include "Engine/Blueprint.h"
#include "LoomleMcpPipeServer.h"
#include "Misc/App.h"
#include "Misc/DateTime.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"

DEFINE_LOG_CATEGORY_STATIC(LogLoomleMcpBridge, Log, All);

using FCondensedJsonWriter = TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>;

namespace LoomleMcpBridgeConstants
{
    static const TCHAR* PipeName = TEXT("loomle-mcp");
}

void FLoomleMcpBridgeModule::StartupModule()
{
    PipeServer = MakeUnique<FLoomleMcpPipeServer>(
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
}

void FLoomleMcpBridgeModule::ShutdownModule()
{
    UnregisterEditorStreamDelegates();

    if (PipeServer)
    {
        PipeServer->StopServer();
        PipeServer.Reset();
    }
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
    ServerInfo->SetStringField(TEXT("name"), TEXT("loomle-ue5-mcp"));
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
        Tools.Add(MakeTool(TEXT("get_context"), TEXT("Get current UE editor context."), Schema));
    }

    {
        TSharedPtr<FJsonObject> Schema = MakeShared<FJsonObject>();
        Schema->SetStringField(TEXT("type"), TEXT("object"));
        Schema->SetArrayField(TEXT("required"), TArray<TSharedPtr<FJsonValue>>{});
        Schema->SetObjectField(TEXT("properties"), MakeShared<FJsonObject>());
        Tools.Add(MakeTool(TEXT("get_selection_transform"), TEXT("Get transform and bounds for selected actors."), Schema));
    }

    {
        TSharedPtr<FJsonObject> Schema = MakeShared<FJsonObject>();
        Schema->SetStringField(TEXT("type"), TEXT("object"));
        TArray<TSharedPtr<FJsonValue>> Required;
        Required.Add(MakeShared<FJsonValueString>(TEXT("action")));
        Schema->SetArrayField(TEXT("required"), Required);

        TSharedPtr<FJsonObject> Properties = MakeShared<FJsonObject>();
        TSharedPtr<FJsonObject> ActionProperty = MakeShared<FJsonObject>();
        ActionProperty->SetStringField(TEXT("type"), TEXT("string"));
        ActionProperty->SetStringField(TEXT("description"), TEXT("editor_stream action: start, stop, or status."));
        TArray<TSharedPtr<FJsonValue>> EnumValues;
        EnumValues.Add(MakeShared<FJsonValueString>(TEXT("start")));
        EnumValues.Add(MakeShared<FJsonValueString>(TEXT("stop")));
        EnumValues.Add(MakeShared<FJsonValueString>(TEXT("status")));
        ActionProperty->SetArrayField(TEXT("enum"), EnumValues);
        Properties->SetObjectField(TEXT("action"), ActionProperty);

        Schema->SetObjectField(TEXT("properties"), Properties);
        Tools.Add(MakeTool(TEXT("editor_stream"), TEXT("Control live editor event streaming."), Schema));
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
        Tools.Add(MakeTool(TEXT("execute_python"), TEXT("Execute inline Python code inside UE Editor."), Schema));
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

    if (Name.Equals(TEXT("get_context")))
    {
        Payload = BuildGetContextToolResult();
    }
    else if (Name.Equals(TEXT("get_selection_transform")))
    {
        Payload = BuildSelectionTransformToolResult();
    }
    else if (Name.Equals(TEXT("editor_stream")))
    {
        Payload = BuildEditorStreamToolResult(Arguments);
        bIsError = Payload->GetBoolField(TEXT("isError"));
    }
    else if (Name.Equals(TEXT("execute_python")))
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

    return Result;
}

TSharedPtr<FJsonObject> FLoomleMcpBridgeModule::BuildEditorStreamToolResult(const TSharedPtr<FJsonObject>& Arguments)
{
    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

    FString Action;
    if (!Arguments->TryGetStringField(TEXT("action"), Action))
    {
        Result->SetBoolField(TEXT("isError"), true);
        Result->SetStringField(TEXT("message"), TEXT("arguments.action is required."));
        return Result;
    }

    Action = Action.ToLower();
    if (Action.Equals(TEXT("start")))
    {
        if (!bEditorStreamEnabled)
        {
            RegisterEditorStreamDelegates();
            bEditorStreamEnabled = true;
            bSelectionEventPending = false;
            PendingSelectionCount = 0;
            PendingSelectionSignature.Empty();
            LastEmittedSelectionSignature.Empty();
            LastSelectionChangeTimeSeconds = 0.0;
        }

        TSharedPtr<FJsonObject> StartedData = MakeShared<FJsonObject>();
        StartedData->SetStringField(TEXT("message"), TEXT("editor_stream started"));
        SendEditorStreamEvent(TEXT("stream_started"), StartedData);

        Result->SetBoolField(TEXT("isError"), false);
        Result->SetBoolField(TEXT("running"), true);
        Result->SetStringField(TEXT("message"), TEXT("editor_stream started."));
        return Result;
    }

    if (Action.Equals(TEXT("stop")))
    {
        if (bEditorStreamEnabled)
        {
            UnregisterEditorStreamDelegates();
            bEditorStreamEnabled = false;
            bSelectionEventPending = false;
            PendingSelectionCount = 0;
            PendingSelectionSignature.Empty();
            LastEmittedSelectionSignature.Empty();
            LastSelectionChangeTimeSeconds = 0.0;
        }

        Result->SetBoolField(TEXT("isError"), false);
        Result->SetBoolField(TEXT("running"), false);
        Result->SetStringField(TEXT("message"), TEXT("editor_stream stopped."));
        return Result;
    }

    if (Action.Equals(TEXT("status")))
    {
        Result->SetBoolField(TEXT("isError"), false);
        Result->SetBoolField(TEXT("running"), bEditorStreamEnabled);
        Result->SetStringField(TEXT("message"), bEditorStreamEnabled ? TEXT("editor_stream is running.") : TEXT("editor_stream is stopped."));
        return Result;
    }

    Result->SetBoolField(TEXT("isError"), true);
    Result->SetStringField(TEXT("message"), TEXT("arguments.action must be start, stop, or status."));
    return Result;
}

TSharedPtr<FJsonObject> FLoomleMcpBridgeModule::BuildSelectionTransformToolResult() const
{
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

void FLoomleMcpBridgeModule::SendEditorStreamEvent(const FString& EventName, const TSharedPtr<FJsonObject>& Data) const
{
    if (!bEditorStreamEnabled || !PipeServer.IsValid())
    {
        return;
    }

    TSharedPtr<FJsonObject> Params = MakeShared<FJsonObject>();
    Params->SetStringField(TEXT("event"), EventName);
    Params->SetStringField(TEXT("timestamp"), FDateTime::UtcNow().ToIso8601());
    Params->SetObjectField(TEXT("data"), Data);

    TSharedPtr<FJsonObject> Notification = MakeShared<FJsonObject>();
    Notification->SetStringField(TEXT("jsonrpc"), TEXT("2.0"));
    Notification->SetStringField(TEXT("method"), TEXT("notifications/editor_stream"));
    Notification->SetObjectField(TEXT("params"), Params);

    FString Output;
    TSharedRef<FCondensedJsonWriter> Writer = TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Output);
    FJsonSerializer::Serialize(Notification.ToSharedRef(), Writer);
    AppendEditorStreamEventLog(Output);
    PipeServer->SendServerNotification(Output);
}

void FLoomleMcpBridgeModule::AppendEditorStreamEventLog(const FString& JsonLine) const
{
    const FString StreamLogPath = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("editor_stream_events.jsonl"));
    const FString Line = JsonLine + LINE_TERMINATOR;
    FFileHelper::SaveStringToFile(Line, *StreamLogPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM, &IFileManager::Get(), FILEWRITE_Append);
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

    if (!SelectionDebounceTickerHandle.IsValid())
    {
        SelectionDebounceTickerHandle = FTSTicker::GetCoreTicker().AddTicker(
            FTickerDelegate::CreateLambda([this](float)
            {
                if (!bEditorStreamEnabled || !bSelectionEventPending)
                {
                    return true;
                }

                const double Now = FPlatformTime::Seconds();
                constexpr double DebounceSeconds = 0.1;
                if (Now - LastSelectionChangeTimeSeconds < DebounceSeconds)
                {
                    return true;
                }

                TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
                Data->SetNumberField(TEXT("count"), PendingSelectionCount);
                Data->SetStringField(TEXT("signature"), PendingSelectionSignature);
                SendEditorStreamEvent(TEXT("selection_changed"), Data);
                LastEmittedSelectionSignature = PendingSelectionSignature;
                bSelectionEventPending = false;
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
    LastSelectionChangeTimeSeconds = FPlatformTime::Seconds();
    bSelectionEventPending = true;
}

void FLoomleMcpBridgeModule::OnMapOpened(const FString& Filename, bool bAsTemplate)
{
    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetStringField(TEXT("filename"), Filename);
    Data->SetBoolField(TEXT("asTemplate"), bAsTemplate);
    SendEditorStreamEvent(TEXT("map_opened"), Data);
}

void FLoomleMcpBridgeModule::OnActorMoved(AActor* Actor)
{
    if (!Actor)
    {
        return;
    }

    const FVector Location = Actor->GetActorLocation();
    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetStringField(TEXT("name"), Actor->GetActorLabel());
    Data->SetStringField(TEXT("path"), Actor->GetPathName());

    TSharedPtr<FJsonObject> LocationObj = MakeShared<FJsonObject>();
    LocationObj->SetNumberField(TEXT("x"), Location.X);
    LocationObj->SetNumberField(TEXT("y"), Location.Y);
    LocationObj->SetNumberField(TEXT("z"), Location.Z);
    Data->SetObjectField(TEXT("location"), LocationObj);

    SendEditorStreamEvent(TEXT("actor_moved"), Data);
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
