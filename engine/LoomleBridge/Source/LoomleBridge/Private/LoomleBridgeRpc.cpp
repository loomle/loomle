// Copyright 2026 Loomle contributors.

#include "LoomleBridgeModule.h"

#include "Async/Async.h"
#include "Async/Future.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "EditorContext/EditorContextService.h"
#include "Generated/LoomleProtocolVersion.h"
#include "HAL/PlatformTime.h"
#include "LoomleGameThreadAdmission.h"
#include "LoomlePipeServer.h"
#include "LoomleRequestCancellation.h"
#include "Misc/DateTime.h"
#include "Misc/ScopeExit.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "Sal/SalModule.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

namespace
{
constexpr const TCHAR* SalQueryTool = TEXT("sal.query");
constexpr const TCHAR* SalPatchTool = TEXT("sal.patch");
constexpr const TCHAR* EditorContextTool = TEXT("editor.context");

using FCondensedJsonWriter = TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>;

bool TryMakeRpcRequestIdKey(const TSharedPtr<FJsonValue>& IdValue, FString& OutKey)
{
    OutKey.Reset();
    if (!IdValue.IsValid() || IdValue->IsNull())
    {
        return false;
    }
    if (IdValue->Type == EJson::String)
    {
        OutKey = TEXT("s:") + IdValue->AsString();
        return true;
    }
    if (IdValue->Type == EJson::Number)
    {
        OutKey = FString::Printf(TEXT("n:%.17g"), IdValue->AsNumber());
        return true;
    }
    return false;
}

TSharedPtr<FJsonObject> MakeDispatchError(const FString& Code, const FString& Message)
{
    TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
    Payload->SetBoolField(TEXT("isError"), true);
    Payload->SetStringField(TEXT("code"), Code);
    Payload->SetStringField(TEXT("message"), Message);
    return Payload;
}

TSharedPtr<FJsonObject> MakeRequestCancelledPayload()
{
    return MakeDispatchError(
        TEXT("runtime.request_cancelled"),
        TEXT("The Loomle runtime request was cancelled."));
}

int32 MapDispatchErrorCode(const FString& Code)
{
    if (Code == TEXT("tool.invalid_arguments"))
    {
        return 1000;
    }
    if (Code == TEXT("tool.unknown"))
    {
        return 1002;
    }
    if (Code == TEXT("runtime.request_timeout"))
    {
        return 1010;
    }
    if (Code == TEXT("runtime.editor_unresponsive"))
    {
        return 1012;
    }
    if (Code == TEXT("runtime.starting"))
    {
        return 1013;
    }
    if (Code == TEXT("runtime.editor_shutting_down"))
    {
        return 1014;
    }
    if (Code == TEXT("project.offline"))
    {
        return 1015;
    }
    return 1011;
}

bool IsRetryableDispatchError(const FString& Code)
{
    return Code == TEXT("runtime.request_timeout")
        || Code == TEXT("runtime.editor_unresponsive")
        || Code == TEXT("runtime.starting")
        || Code == TEXT("runtime.editor_shutting_down")
        || Code == TEXT("project.offline");
}

FString DefaultDiagnosticCodeForRpcError(const int32 RpcCode)
{
    switch (RpcCode)
    {
    case -32602:
    case 1000:
        return TEXT("tool.invalid_arguments");
    case -32601:
        return TEXT("runtime.incompatible");
    case 1002:
        return TEXT("tool.unknown");
    case 1010:
        return TEXT("runtime.request_timeout");
    default:
        return TEXT("runtime.rpc_error");
    }
}

TArray<TSharedPtr<FJsonValue>> StringValues(std::initializer_list<const TCHAR*> Values)
{
    TArray<TSharedPtr<FJsonValue>> Out;
    Out.Reserve(static_cast<int32>(Values.size()));
    for (const TCHAR* Value : Values)
    {
        Out.Add(MakeShared<FJsonValueString>(Value));
    }
    return Out;
}
}

TSharedRef<Loomle::Runtime::FRequestCancellationState, ESPMode::ThreadSafe>
FLoomleBridgeModule::RegisterRequestCancellation(int32 ConnectionSerial, const FString& RequestIdKey)
{
    check(RequestCancellationRegistry != nullptr);
    return RequestCancellationRegistry->Register(ConnectionSerial, RequestIdKey);
}

void FLoomleBridgeModule::UnregisterRequestCancellation(
    int32 ConnectionSerial,
    const FString& RequestIdKey,
    const TSharedRef<Loomle::Runtime::FRequestCancellationState, ESPMode::ThreadSafe>& ExpectedState)
{
    check(RequestCancellationRegistry != nullptr);
    RequestCancellationRegistry->Unregister(ConnectionSerial, RequestIdKey, ExpectedState);
}

bool FLoomleBridgeModule::CancelRequest(const FString& RequestIdKey)
{
    check(RequestCancellationRegistry != nullptr);
    return RequestCancellationRegistry->Cancel(RequestIdKey);
}

void FLoomleBridgeModule::CancelRequestsForConnection(int32 ConnectionSerial)
{
    if (RequestCancellationRegistry != nullptr)
    {
        RequestCancellationRegistry->CloseConnection(ConnectionSerial);
    }
}

FString FLoomleBridgeModule::HandleRequest(int32 ConnectionSerial, const FString& RequestLine)
{
    TSharedPtr<FJsonObject> Request;
    const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(RequestLine);
    if (!FJsonSerializer::Deserialize(Reader, Request) || !Request.IsValid())
    {
        return MakeJsonError(MakeShared<FJsonValueNull>(), -32700, TEXT("Parse error"));
    }

    TSharedPtr<FJsonValue> Id = Request->TryGetField(TEXT("id"));
    if (!Id.IsValid())
    {
        Id = MakeShared<FJsonValueNull>();
    }
    FString Method;
    if (!Request->TryGetStringField(TEXT("method"), Method))
    {
        return MakeJsonError(Id, -32600, TEXT("Invalid Request: method is required"));
    }
    const TSharedPtr<FJsonObject>* ParamsPtr = nullptr;
    Request->TryGetObjectField(TEXT("params"), ParamsPtr);
    const TSharedPtr<FJsonObject> Params = ParamsPtr != nullptr && (*ParamsPtr).IsValid()
        ? *ParamsPtr
        : MakeShared<FJsonObject>();

    FString ToolName;
    if (Method == TEXT("rpc.invoke"))
    {
        Params->TryGetStringField(TEXT("tool"), ToolName);
    }
    RecordClientActivity(Method, ToolName);

    if (Method == TEXT("ping"))
    {
        return MakeJsonResponse(Id, MakeShared<FJsonObject>());
    }
    if (Method == TEXT("rpc.health"))
    {
        return MakeJsonResponse(Id, BuildRpcHealthResult());
    }
    if (Method == TEXT("rpc.capabilities"))
    {
        return MakeJsonResponse(Id, BuildRpcCapabilitiesResult());
    }
    if (Method == TEXT("rpc.invoke") || Method == TEXT("rpc.cancel"))
    {
        const TSharedPtr<FJsonValue> ProtocolVersionValue =
            Params->TryGetField(TEXT("protocolVersion"));
        const bool bHasNumericProtocolVersion =
            ProtocolVersionValue.IsValid()
            && ProtocolVersionValue->Type == EJson::Number;
        const double ClientProtocolVersion =
            bHasNumericProtocolVersion
                ? ProtocolVersionValue->AsNumber()
                : 0.0;
        if (!bHasNumericProtocolVersion
            || ClientProtocolVersion != static_cast<double>(Loomle::Protocol::Version))
        {
            const FString ActualVersion = bHasNumericProtocolVersion
                ? FString::Printf(TEXT("%.17g"), ClientProtocolVersion)
                : (Params->HasField(TEXT("protocolVersion")) ? TEXT("non-numeric") : TEXT("missing"));
            TSharedPtr<FJsonObject> ErrorData = MakeShared<FJsonObject>();
            ErrorData->SetStringField(TEXT("code"), TEXT("runtime.incompatible"));
            ErrorData->SetBoolField(TEXT("retryable"), false);
            ErrorData->SetNumberField(TEXT("expected"), Loomle::Protocol::Version);
            ErrorData->SetStringField(
                TEXT("detail"),
                FString::Printf(
                    TEXT("Client protocol version %s does not match Bridge protocol version %d."),
                    *ActualVersion,
                    Loomle::Protocol::Version));
            return MakeJsonErrorEx(
                Id,
                1001,
                TEXT("Client–Bridge protocol version mismatch."),
                ErrorData);
        }
    }
    if (Method == TEXT("rpc.cancel"))
    {
        FString CancellationToken;
        if (!Params->TryGetStringField(TEXT("cancellationToken"), CancellationToken)
            || CancellationToken.IsEmpty()
            || CancellationToken.Len() > 256)
        {
            return MakeJsonError(Id, -32602, TEXT("Invalid params: cancellationToken is required"));
        }
        TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
        Result->SetBoolField(TEXT("cancelled"), CancelRequest(TEXT("t:") + CancellationToken));
        Result->SetStringField(TEXT("cancellationToken"), CancellationToken);
        return MakeJsonResponse(Id, Result);
    }
    if (Method != TEXT("rpc.invoke"))
    {
        return MakeJsonError(Id, -32601, FString::Printf(TEXT("Method not found: %s"), *Method));
    }

    FString RequestIdKey;
    const bool bHasRequestIdKey = TryMakeRpcRequestIdKey(Id, RequestIdKey);
    FString CancellationToken;
    if (Params->HasField(TEXT("cancellationToken"))
        && (!Params->TryGetStringField(TEXT("cancellationToken"), CancellationToken)
            || CancellationToken.IsEmpty()
            || CancellationToken.Len() > 256))
    {
        return MakeJsonError(Id, -32602, TEXT("Invalid params: cancellationToken must be a non-empty string"));
    }

    const FString CancellationKey = !CancellationToken.IsEmpty()
        ? TEXT("t:") + CancellationToken
        : FString::Printf(TEXT("c:%d|%s"), ConnectionSerial, *RequestIdKey);
    const bool bRegistered = bHasRequestIdKey || !CancellationToken.IsEmpty();
    const TSharedRef<Loomle::Runtime::FRequestCancellationState, ESPMode::ThreadSafe> CancellationState = bRegistered
        ? RegisterRequestCancellation(ConnectionSerial, CancellationKey)
        : MakeShared<Loomle::Runtime::FRequestCancellationState, ESPMode::ThreadSafe>();
    ON_SCOPE_EXIT
    {
        if (bRegistered)
        {
            UnregisterRequestCancellation(ConnectionSerial, CancellationKey, CancellationState);
        }
    };
    Loomle::Runtime::FScopedRequestCancellation CancellationScope(CancellationState);

    bool bHasError = false;
    int32 ErrorCode = 1011;
    FString ErrorMessage = TEXT("The Loomle runtime could not execute the request.");
    TSharedPtr<FJsonObject> ErrorData;
    const TSharedPtr<FJsonObject> Result = BuildRpcInvokeResult(
        Params,
        bHasError,
        ErrorCode,
        ErrorMessage,
        ErrorData);
    return bHasError
        ? MakeJsonErrorEx(Id, ErrorCode, ErrorMessage, ErrorData)
        : MakeJsonResponse(Id, Result);
}

TSharedPtr<FJsonObject> FLoomleBridgeModule::BuildRpcHealthResult() const
{
    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    const FString Lifecycle = GetBridgeLifecycleName();
    const FString ListenerState = PipeServer.IsValid()
        ? PipeServer->GetListenerStateName()
        : TEXT("stopped");
    const bool bReady = Lifecycle == TEXT("ready") && ListenerState == TEXT("listening");
    const uint64 LastProgressCycles = LastGameThreadProgressCycles.load();
    const double ProgressAgeMs = LastProgressCycles == 0
        ? -1.0
        : FPlatformTime::ToMilliseconds64(FPlatformTime::Cycles64() - LastProgressCycles);
    Result->SetStringField(TEXT("status"), bReady ? TEXT("ok") : TEXT("error"));
    Result->SetStringField(TEXT("service"), TEXT("loomle-rpc-listener"));
    Result->SetNumberField(TEXT("protocolVersion"), Loomle::Protocol::Version);
    Result->SetStringField(TEXT("runtimeId"), RuntimeId);
    Result->SetStringField(TEXT("projectId"), ProjectId);
    Result->SetStringField(TEXT("projectRoot"), ProjectRoot);
    Result->SetStringField(TEXT("lifecycle"), Lifecycle);
    Result->SetStringField(TEXT("listenerState"), ListenerState);
    Result->SetNumberField(
        TEXT("gameThreadProgressSequence"),
        static_cast<double>(GameThreadProgressSequence.load()));
    Result->SetNumberField(TEXT("gameThreadProgressAgeMs"), ProgressAgeMs);
    Result->SetStringField(TEXT("timestamp"), FDateTime::UtcNow().ToIso8601());
    Result->SetBoolField(TEXT("isPIE"), bIsPIESnapshot.Load());
    Result->SetStringField(TEXT("editorBusyReason"), bIsPIESnapshot.Load() ? TEXT("PIE_ACTIVE") : TEXT(""));
    return Result;
}

TSharedPtr<FJsonObject> FLoomleBridgeModule::BuildRpcCapabilitiesResult() const
{
    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetNumberField(TEXT("protocolVersion"), Loomle::Protocol::Version);
    Result->SetArrayField(
        TEXT("methods"),
        StringValues({TEXT("ping"), TEXT("rpc.health"), TEXT("rpc.capabilities"), TEXT("rpc.invoke"), TEXT("rpc.cancel")}));
    Result->SetArrayField(
        TEXT("tools"),
        StringValues({SalQueryTool, SalPatchTool, EditorContextTool}));
    return Result;
}

TSharedPtr<FJsonObject> FLoomleBridgeModule::BuildRpcInvokeResult(
    const TSharedPtr<FJsonObject>& Params,
    bool& bOutHasError,
    int32& OutErrorCode,
    FString& OutErrorMessage,
    TSharedPtr<FJsonObject>& OutErrorData)
{
    bOutHasError = false;
    OutErrorCode = 1011;
    OutErrorMessage = TEXT("The Loomle runtime could not execute the request.");
    OutErrorData.Reset();

    FString ToolName;
    if (!Params.IsValid()
        || !Params->TryGetStringField(TEXT("tool"), ToolName)
        || ToolName.IsEmpty())
    {
        bOutHasError = true;
        OutErrorCode = 1000;
        OutErrorMessage = TEXT("rpc.invoke requires a non-empty tool name.");
        OutErrorData = MakeShared<FJsonObject>();
        OutErrorData->SetStringField(TEXT("code"), TEXT("tool.invalid_arguments"));
        OutErrorData->SetBoolField(TEXT("retryable"), false);
        OutErrorData->SetStringField(TEXT("detail"), TEXT("field tool is required"));
        return nullptr;
    }

    TSharedPtr<FJsonObject> Arguments = MakeShared<FJsonObject>();
    const TSharedPtr<FJsonObject>* ArgumentsPtr = nullptr;
    if (Params->TryGetObjectField(TEXT("args"), ArgumentsPtr)
        && ArgumentsPtr != nullptr
        && (*ArgumentsPtr).IsValid())
    {
        Arguments = *ArgumentsPtr;
    }

    bool bDispatchError = false;
    const TSharedPtr<FJsonObject> Payload = DispatchTool(ToolName, Arguments, bDispatchError);
    if (bDispatchError)
    {
        FString Code = TEXT("runtime.internal_error");
        FString Message = TEXT("The Loomle runtime could not execute the request.");
        if (Payload.IsValid())
        {
            Payload->TryGetStringField(TEXT("code"), Code);
            Payload->TryGetStringField(TEXT("message"), Message);
        }
        bOutHasError = true;
        OutErrorCode = MapDispatchErrorCode(Code);
        OutErrorMessage = Message;
        OutErrorData = MakeShared<FJsonObject>();
        OutErrorData->SetStringField(TEXT("code"), Code);
        OutErrorData->SetBoolField(TEXT("retryable"), IsRetryableDispatchError(Code));
        FString Detail;
        const TSharedRef<FCondensedJsonWriter> Writer =
            TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Detail);
        if (Payload.IsValid())
        {
            FJsonSerializer::Serialize(Payload.ToSharedRef(), Writer);
        }
        OutErrorData->SetStringField(TEXT("detail"), Detail);
        return nullptr;
    }

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetBoolField(TEXT("ok"), true);
    Result->SetObjectField(TEXT("payload"), Payload.IsValid() ? Payload : MakeShared<FJsonObject>());
    return Result;
}

TSharedPtr<FJsonObject> FLoomleBridgeModule::DispatchTool(
    const FString& Name,
    const TSharedPtr<FJsonObject>& Arguments,
    bool& bOutIsError)
{
    bOutIsError = false;
    if (Loomle::Runtime::IsRequestCancellationRequested())
    {
        bOutIsError = true;
        return MakeRequestCancelledPayload();
    }
    if (bIsShuttingDown.Load())
    {
        bOutIsError = true;
        return MakeDispatchError(TEXT("runtime.editor_shutting_down"), TEXT("Unreal Editor is shutting down."));
    }
    const ELoomleBridgeLifecycle Lifecycle =
        static_cast<ELoomleBridgeLifecycle>(BridgeLifecycleState.load());
    if (Lifecycle != ELoomleBridgeLifecycle::Ready)
    {
        bOutIsError = true;
        if (Lifecycle == ELoomleBridgeLifecycle::Starting)
        {
            return MakeDispatchError(TEXT("runtime.starting"), TEXT("The Unreal Editor runtime is still starting."));
        }
        return MakeDispatchError(TEXT("project.offline"), TEXT("The Unreal Editor runtime is not available."));
    }

    if (!IsInGameThread())
    {
        struct FDispatchResult
        {
            TSharedPtr<FJsonObject> Payload;
            bool bIsError = false;
        };

        TPromise<FDispatchResult> Promise;
        TFuture<FDispatchResult> Future = Promise.GetFuture();
        const TSharedRef<Loomle::Runtime::FGameThreadAdmission, ESPMode::ThreadSafe> Admission =
            MakeShared<Loomle::Runtime::FGameThreadAdmission, ESPMode::ThreadSafe>();
        const TSharedPtr<Loomle::Runtime::FRequestCancellationState, ESPMode::ThreadSafe> CancellationState =
            Loomle::Runtime::GetRequestCancellationState();
        const bool bSupportsCooperativeCancellation = Name == SalQueryTool;
        ActiveGameThreadDispatchCount.Increment();
        AsyncTask(
            ENamedThreads::GameThread,
            [this, Name, Arguments, CancellationState, Admission, Promise = MoveTemp(Promise)]() mutable
            {
                ON_SCOPE_EXIT { ActiveGameThreadDispatchCount.Decrement(); };
                FDispatchResult DispatchResult;
                if (!Admission->TryStart())
                {
                    DispatchResult.Payload = MakeRequestCancelledPayload();
                    DispatchResult.bIsError = true;
                    RecordGameThreadProgress();
                    Promise.SetValue(MoveTemp(DispatchResult));
                    return;
                }
                if (CancellationState.IsValid())
                {
                    Loomle::Runtime::FScopedRequestCancellation Scope(CancellationState.ToSharedRef());
                    DispatchResult.Payload = DispatchTool(Name, Arguments, DispatchResult.bIsError);
                }
                else
                {
                    DispatchResult.Payload = DispatchTool(Name, Arguments, DispatchResult.bIsError);
                }
                RecordGameThreadProgress();
                Promise.SetValue(MoveTemp(DispatchResult));
            });

        constexpr uint32 AdmissionTimeoutMs = 2000;
        constexpr uint32 ExecutionTimeoutMs = 120000;
        constexpr uint32 PollMs = 25;
        uint32 AdmissionWaitedMs = 0;
        while (AdmissionWaitedMs < AdmissionTimeoutMs)
        {
            if (bSupportsCooperativeCancellation
                && CancellationState.IsValid()
                && CancellationState->IsCancellationRequested())
            {
                if (Admission->TryCancel())
                {
                    bOutIsError = true;
                    return MakeRequestCancelledPayload();
                }
            }
            if (Future.WaitFor(FTimespan::FromMilliseconds(PollMs)))
            {
                FDispatchResult Result = Future.Get();
                bOutIsError = Result.bIsError;
                return Result.Payload;
            }
            if (Admission->GetState() == Loomle::Runtime::EGameThreadAdmissionState::Started)
            {
                break;
            }
            AdmissionWaitedMs += PollMs;
            if (bIsShuttingDown.Load())
            {
                Admission->TryCancel();
                bOutIsError = true;
                return MakeDispatchError(TEXT("runtime.editor_shutting_down"), TEXT("Unreal Editor is shutting down."));
            }
        }

        if (Admission->GetState() == Loomle::Runtime::EGameThreadAdmissionState::Waiting
            && Admission->TryCancel())
        {
            bOutIsError = true;
            return MakeDispatchError(
                TEXT("runtime.editor_unresponsive"),
                TEXT("The Unreal Editor game thread did not admit the request."));
        }

        uint32 ExecutionWaitedMs = 0;
        while (ExecutionWaitedMs < ExecutionTimeoutMs)
        {
            if (bSupportsCooperativeCancellation
                && CancellationState.IsValid()
                && CancellationState->IsCancellationRequested())
            {
                bOutIsError = true;
                return MakeRequestCancelledPayload();
            }
            if (Future.WaitFor(FTimespan::FromMilliseconds(PollMs)))
            {
                FDispatchResult Result = Future.Get();
                bOutIsError = Result.bIsError;
                return Result.Payload;
            }
            ExecutionWaitedMs += PollMs;
            if (bIsShuttingDown.Load())
            {
                bOutIsError = true;
                return MakeDispatchError(TEXT("runtime.editor_shutting_down"), TEXT("Unreal Editor is shutting down."));
            }
        }

        if (CancellationState.IsValid())
        {
            CancellationState->Cancel();
        }
        bOutIsError = true;
        return MakeDispatchError(TEXT("runtime.request_timeout"), TEXT("Tool execution timed out on the game thread."));
    }

    if (Name == SalQueryTool)
    {
        return Loomle::Sal::FSalModule::BuildQueryResult(Arguments);
    }
    if (Name == SalPatchTool)
    {
        return Loomle::Sal::FSalModule::BuildPatchResult(Arguments);
    }
    if (Name == EditorContextTool)
    {
        return Loomle::EditorContext::FEditorContextService::Get().BuildResult();
    }

    bOutIsError = true;
    return MakeDispatchError(TEXT("tool.unknown"), FString::Printf(TEXT("Unknown tool: %s"), *Name));
}

FString FLoomleBridgeModule::MakeJsonResponse(
    const TSharedPtr<FJsonValue>& Id,
    const TSharedPtr<FJsonObject>& Result) const
{
    TSharedPtr<FJsonObject> Response = MakeShared<FJsonObject>();
    Response->SetStringField(TEXT("jsonrpc"), TEXT("2.0"));
    Response->SetField(TEXT("id"), Id);
    Response->SetObjectField(TEXT("result"), Result);
    FString Output;
    const TSharedRef<FCondensedJsonWriter> Writer =
        TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Output);
    FJsonSerializer::Serialize(Response.ToSharedRef(), Writer);
    return Output;
}

FString FLoomleBridgeModule::MakeJsonError(
    const TSharedPtr<FJsonValue>& Id,
    int32 Code,
    const FString& Message) const
{
    return MakeJsonErrorEx(Id, Code, Message, nullptr);
}

FString FLoomleBridgeModule::MakeJsonErrorEx(
    const TSharedPtr<FJsonValue>& Id,
    int32 Code,
    const FString& Message,
    const TSharedPtr<FJsonObject>& ErrorData) const
{
    TSharedPtr<FJsonObject> PublicErrorData = ErrorData.IsValid()
        ? ErrorData
        : MakeShared<FJsonObject>();
    if (!PublicErrorData->HasField(TEXT("code")))
    {
        PublicErrorData->SetStringField(TEXT("code"), DefaultDiagnosticCodeForRpcError(Code));
    }

    TSharedPtr<FJsonObject> Error = MakeShared<FJsonObject>();
    Error->SetNumberField(TEXT("code"), Code);
    Error->SetStringField(TEXT("message"), Message);
    Error->SetObjectField(TEXT("data"), PublicErrorData);
    TSharedPtr<FJsonObject> Response = MakeShared<FJsonObject>();
    Response->SetStringField(TEXT("jsonrpc"), TEXT("2.0"));
    Response->SetField(TEXT("id"), Id);
    Response->SetObjectField(TEXT("error"), Error);
    FString Output;
    const TSharedRef<FCondensedJsonWriter> Writer =
        TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Output);
    FJsonSerializer::Serialize(Response.ToSharedRef(), Writer);
    return Output;
}
