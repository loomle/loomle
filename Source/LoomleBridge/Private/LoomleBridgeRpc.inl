// RPC entrypoints and tool dispatch for Loomle Bridge.
FString FLoomleBridgeModule::HandleRequest(const FString& RequestLine)
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

    if (Method.Equals(TEXT("rpc.health")))
    {
        return MakeJsonResponse(IdValue, BuildRpcHealthResult());
    }

    if (Method.Equals(TEXT("rpc.capabilities")))
    {
        return MakeJsonResponse(IdValue, BuildRpcCapabilitiesResult());
    }

    if (!IsInGameThread())
    {
        TPromise<FString> ResponsePromise;
        TFuture<FString> ResponseFuture = ResponsePromise.GetFuture();
        AsyncTask(ENamedThreads::GameThread, [this, RequestLine, Promise = MoveTemp(ResponsePromise)]() mutable
        {
            Promise.SetValue(HandleRequest(RequestLine));
        });
        return ResponseFuture.Get();
    }

    if (Method.Equals(TEXT("rpc.invoke")))
    {
        bool bInvokeError = false;
        int32 InvokeErrorCode = 1011;
        FString InvokeErrorMessage = TEXT("INTERNAL_ERROR");
        TSharedPtr<FJsonObject> InvokeErrorData;
        TSharedPtr<FJsonObject> Result = BuildRpcInvokeResult(Params, bInvokeError, InvokeErrorCode, InvokeErrorMessage, InvokeErrorData);
        if (bInvokeError)
        {
            return MakeJsonErrorEx(IdValue, InvokeErrorCode, InvokeErrorMessage, InvokeErrorData);
        }
        return MakeJsonResponse(IdValue, Result);
    }

    return MakeJsonError(IdValue, -32601, FString::Printf(TEXT("Method not found: %s"), *Method));
}

TSharedPtr<FJsonObject> FLoomleBridgeModule::BuildRpcHealthResult() const
{
    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    const bool bBridgeRunning = bBridgeRunningSnapshot.Load();
    const bool bPythonReady = bPythonReadySnapshot.Load();
    const bool bIsPIE = bIsPIESnapshot.Load();

    FString Health = TEXT("error");
    if (bBridgeRunning && bPythonReady)
    {
        Health = TEXT("ok");
    }
    else if (bBridgeRunning)
    {
        Health = TEXT("degraded");
    }

    Result->SetStringField(TEXT("status"), Health);
    Result->SetStringField(TEXT("service"), TEXT("loomle-rpc-listener"));
    Result->SetStringField(TEXT("rpcVersion"), LoomleBridgeConstants::RpcVersion);
    Result->SetStringField(TEXT("timestamp"), FDateTime::UtcNow().ToIso8601());
    Result->SetBoolField(TEXT("isPIE"), bIsPIE);
    Result->SetStringField(TEXT("editorBusyReason"), bIsPIE ? TEXT("PIE_ACTIVE") : TEXT(""));
    return Result;
}

TSharedPtr<FJsonObject> FLoomleBridgeModule::BuildRpcCapabilitiesResult() const
{
    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetStringField(TEXT("rpcVersion"), LoomleBridgeConstants::RpcVersion);

    auto MakeStringArray = [](std::initializer_list<const TCHAR*> Values)
    {
        TArray<TSharedPtr<FJsonValue>> Out;
        for (const TCHAR* Value : Values)
        {
            Out.Add(MakeShared<FJsonValueString>(Value));
        }
        return Out;
    };

    Result->SetArrayField(TEXT("methods"), MakeStringArray({TEXT("rpc.health"), TEXT("rpc.capabilities"), TEXT("rpc.invoke")}));
    Result->SetArrayField(TEXT("tools"), MakeStringArray({
        TEXT("context"), TEXT("execute"),
        TEXT("graph.list"), TEXT("graph.query"), TEXT("graph.actions"), TEXT("graph.mutate")
    }));
    Result->SetArrayField(TEXT("graphTypes"), MakeStringArray({TEXT("k2"), TEXT("material"), TEXT("pcg")}));

    TSharedPtr<FJsonObject> Features = MakeShared<FJsonObject>();
    Features->SetBoolField(TEXT("revision"), true);
    Features->SetBoolField(TEXT("idempotency"), true);
    Features->SetBoolField(TEXT("dryRun"), true);
    Result->SetObjectField(TEXT("features"), Features);
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
    OutErrorMessage = TEXT("INTERNAL_ERROR");
    OutErrorData = nullptr;

    FString ToolName;
    if (!Params->TryGetStringField(TEXT("tool"), ToolName) || ToolName.IsEmpty())
    {
        bOutHasError = true;
        OutErrorCode = 1000;
        OutErrorMessage = TEXT("INVALID_ARGUMENT");
        OutErrorData = MakeShared<FJsonObject>();
        OutErrorData->SetBoolField(TEXT("retryable"), false);
        OutErrorData->SetStringField(TEXT("detail"), TEXT("field tool is required"));
        return nullptr;
    }

    TSharedPtr<FJsonObject> Args = MakeShared<FJsonObject>();
    const TSharedPtr<FJsonObject>* ArgsPtr = nullptr;
    if (Params->TryGetObjectField(TEXT("args"), ArgsPtr) && ArgsPtr && (*ArgsPtr).IsValid())
    {
        Args = *ArgsPtr;
    }

    bool bToolError = false;
    const TSharedPtr<FJsonObject> Payload = DispatchTool(ToolName, Args, bToolError);
    if (bToolError)
    {
        FString DomainCode;
        Payload->TryGetStringField(TEXT("code"), DomainCode);
        if (DomainCode.IsEmpty())
        {
            Payload->TryGetStringField(TEXT("domainCode"), DomainCode);
        }

        FString Message;
        Payload->TryGetStringField(TEXT("message"), Message);
        if (Message.IsEmpty())
        {
            Message = TEXT("INTERNAL_ERROR");
        }

        bOutHasError = true;
        OutErrorCode = MapToolErrorCode(DomainCode);
        OutErrorMessage = Message;
        OutErrorData = MakeShared<FJsonObject>();
        OutErrorData->SetBoolField(TEXT("retryable"), false);

        FString Detail;
        if (!Payload->TryGetStringField(TEXT("detail"), Detail))
        {
            FString PayloadText;
            const TSharedRef<FCondensedJsonWriter> Writer = TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&PayloadText);
            FJsonSerializer::Serialize(Payload.ToSharedRef(), Writer);
            Detail = PayloadText;
        }
        OutErrorData->SetStringField(TEXT("detail"), Detail);
        return nullptr;
    }

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetBoolField(TEXT("ok"), true);
    Result->SetObjectField(TEXT("payload"), Payload);
    Result->SetArrayField(TEXT("diagnostics"), TArray<TSharedPtr<FJsonValue>>{});
    return Result;
}

TSharedPtr<FJsonObject> FLoomleBridgeModule::DispatchTool(const FString& Name, const TSharedPtr<FJsonObject>& Arguments, bool& bOutIsError)
{
    bOutIsError = false;
    TSharedPtr<FJsonObject> Payload;

    if (Name.Equals(TEXT("context")))
    {
        Payload = BuildGetContextToolResult(Arguments);
    }
    else if (Name.Equals(LoomleBridgeConstants::GraphListToolName))
    {
        Payload = BuildGraphListToolResult(Arguments);
    }
    else if (Name.Equals(LoomleBridgeConstants::GraphQueryToolName))
    {
        Payload = BuildGraphQueryToolResult(Arguments);
    }
    else if (Name.Equals(LoomleBridgeConstants::GraphActionsToolName))
    {
        Payload = BuildGraphActionsToolResult(Arguments);
    }
    else if (Name.Equals(LoomleBridgeConstants::GraphMutateToolName))
    {
        Payload = BuildGraphMutateToolResult(Arguments);
    }
    else if (Name.Equals(LoomleBridgeConstants::ExecuteToolName))
    {
        Payload = BuildExecutePythonToolResult(Arguments);
    }
    else
    {
        bOutIsError = true;
        Payload = MakeShared<FJsonObject>();
        Payload->SetStringField(TEXT("code"), TEXT("TOOL_NOT_FOUND"));
        Payload->SetStringField(TEXT("message"), FString::Printf(TEXT("Unknown tool: %s"), *Name));
        return Payload;
    }

    bool bIsErrorField = false;
    if (Payload.IsValid() && Payload->TryGetBoolField(TEXT("isError"), bIsErrorField))
    {
        bOutIsError = bIsErrorField;
    }
    return Payload;
}

int32 FLoomleBridgeModule::MapToolErrorCode(const FString& DomainCode) const
{
    if (DomainCode.Equals(TEXT("INVALID_ARGUMENT")))
    {
        return 1000;
    }
    if (DomainCode.Equals(TEXT("TOOL_NOT_FOUND")))
    {
        return 1002;
    }
    if (DomainCode.Equals(TEXT("UNSUPPORTED_GRAPH_TYPE")))
    {
        return 1003;
    }
    if (DomainCode.Equals(TEXT("ASSET_NOT_FOUND")))
    {
        return 1004;
    }
    if (DomainCode.Equals(TEXT("GRAPH_NOT_FOUND")))
    {
        return 1005;
    }
    if (DomainCode.Equals(TEXT("NODE_NOT_FOUND")))
    {
        return 1006;
    }
    if (DomainCode.Equals(TEXT("PIN_NOT_FOUND")))
    {
        return 1007;
    }
    if (DomainCode.Equals(TEXT("REVISION_CONFLICT")))
    {
        return 1008;
    }
    if (DomainCode.Equals(TEXT("LIMIT_EXCEEDED")))
    {
        return 1009;
    }
    if (DomainCode.Equals(TEXT("EXECUTION_TIMEOUT")))
    {
        return 1010;
    }
    return 1011;
}


FString FLoomleBridgeModule::MakeJsonResponse(const TSharedPtr<FJsonValue>& Id, const TSharedPtr<FJsonObject>& Result) const
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

FString FLoomleBridgeModule::MakeJsonError(const TSharedPtr<FJsonValue>& Id, int32 Code, const FString& Message) const
{
    return MakeJsonErrorEx(Id, Code, Message, nullptr);
}

FString FLoomleBridgeModule::MakeJsonErrorEx(
    const TSharedPtr<FJsonValue>& Id,
    int32 Code,
    const FString& Message,
    const TSharedPtr<FJsonObject>& ErrorData) const
{
    TSharedPtr<FJsonObject> Error = MakeShared<FJsonObject>();
    Error->SetNumberField(TEXT("code"), Code);
    Error->SetStringField(TEXT("message"), Message);
    if (ErrorData.IsValid())
    {
        Error->SetObjectField(TEXT("data"), ErrorData);
    }

    TSharedPtr<FJsonObject> Response = MakeShared<FJsonObject>();
    Response->SetStringField(TEXT("jsonrpc"), TEXT("2.0"));
    Response->SetField(TEXT("id"), Id);
    Response->SetObjectField(TEXT("error"), Error);

    FString Output;
    TSharedRef<FCondensedJsonWriter> Writer = TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Output);
    FJsonSerializer::Serialize(Response.ToSharedRef(), Writer);
    return Output;
}
