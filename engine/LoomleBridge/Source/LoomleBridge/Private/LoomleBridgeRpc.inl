// RPC entrypoints and tool dispatch for Loomle Bridge.
FString FLoomleBridgeModule::HandleRequest(int32 ConnectionSerial, const FString& RequestLine)
{
    TSharedPtr<FJsonObject> RequestObject;
    const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(RequestLine);

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

    if (Method.Equals(TEXT("ping")))
    {
        return MakeJsonResponse(IdValue, MakeShared<FJsonObject>());
    }

    if (Method.Equals(TEXT("rpc.health")))
    {
        return MakeJsonResponse(IdValue, BuildRpcHealthResult());
    }

    if (Method.Equals(TEXT("rpc.capabilities")))
    {
        return MakeJsonResponse(IdValue, BuildRpcCapabilitiesResult());
    }

    if (Method.Equals(TEXT("rpc.invoke")))
    {
        bool bHasError = false;
        int32 ErrorCode = 1000;
        FString ErrorMessage = TEXT("INVALID_ARGUMENT");
        TSharedPtr<FJsonObject> ErrorData;
        TSharedPtr<FJsonObject> Result = BuildRpcInvokeResult(Params, bHasError, ErrorCode, ErrorMessage, ErrorData);
        if (bHasError)
        {
            return MakeJsonErrorEx(IdValue, ErrorCode, ErrorMessage, ErrorData);
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
        TEXT("context"), TEXT("jobs"), TEXT("profiling"), TEXT("editor.open"), TEXT("editor.focus"), TEXT("editor.screenshot"), TEXT("execute"),
        TEXT("blueprint.asset.edit"), TEXT("blueprint.member.edit"),
        TEXT("blueprint.list"), TEXT("blueprint.query"), TEXT("blueprint.mutate"), TEXT("blueprint.verify"), TEXT("blueprint.describe"),
        TEXT("material.list"), TEXT("material.query"), TEXT("material.mutate"), TEXT("material.verify"), TEXT("material.describe"),
        TEXT("pcg.list"), TEXT("pcg.query"), TEXT("pcg.mutate"), TEXT("pcg.verify"), TEXT("pcg.describe"),
        TEXT("diagnostic.tail"), TEXT("log.tail"),
        TEXT("widget.query"), TEXT("widget.mutate"), TEXT("widget.verify"), TEXT("widget.describe")
    }));

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

        TSharedPtr<FJsonObject> DiagContext = MakeShared<FJsonObject>();
        DiagContext->SetStringField(TEXT("tool"), ToolName);
        if (!DomainCode.IsEmpty())
        {
            DiagContext->SetStringField(TEXT("domainCode"), DomainCode);
        }
        AppendDiagnosticEvent(TEXT("error"), TEXT("runtime"), ToolName, Message, DiagContext);

        bOutHasError = true;
        OutErrorCode = MapToolErrorCode(DomainCode);
        OutErrorMessage = Message;
        OutErrorData = MakeShared<FJsonObject>();
        const bool bRetryable = DomainCode.Equals(TEXT("STAT_UNIT_WARMUP_REQUIRED"))
            || DomainCode.Equals(TEXT("STATS_GROUP_WARMUP_REQUIRED"));
        OutErrorData->SetBoolField(TEXT("retryable"), bRetryable);

        FString Detail;
        if (!Payload->TryGetStringField(TEXT("detail"), Detail))
        {
            FString PayloadText;
            const TSharedRef<FCondensedJsonWriter> Writer = TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&PayloadText);
            FJsonSerializer::Serialize(Payload.ToSharedRef(), Writer);
            Detail = PayloadText;
        }
        OutErrorData->SetStringField(TEXT("detail"), Detail);
        const TSharedPtr<FJsonObject>* TimeoutContext = nullptr;
        if (Payload->TryGetObjectField(TEXT("timeoutContext"), TimeoutContext) && TimeoutContext && (*TimeoutContext).IsValid())
        {
            OutErrorData->SetObjectField(TEXT("timeoutContext"), CloneJsonObject(*TimeoutContext));
        }
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

    if (!IsInGameThread()
        && !Name.Equals(LoomleBridgeConstants::BlueprintQueryToolName)
        && !Name.Equals(LoomleBridgeConstants::MaterialQueryToolName)
        && !Name.Equals(LoomleBridgeConstants::PcgQueryToolName)
        && !Name.Equals(LoomleBridgeConstants::JobsToolName))
    {
        struct FDispatchToolResult
        {
            TSharedPtr<FJsonObject> Payload;
            bool bIsError = false;
        };

        TPromise<FDispatchToolResult> PayloadPromise;
        TFuture<FDispatchToolResult> PayloadFuture = PayloadPromise.GetFuture();
        AsyncTask(ENamedThreads::GameThread, [this, Name, Arguments, Promise = MoveTemp(PayloadPromise)]() mutable
        {
            FDispatchToolResult DispatchResult;
            DispatchResult.Payload = DispatchTool(Name, Arguments, DispatchResult.bIsError);
            Promise.SetValue(MoveTemp(DispatchResult));
        });

        static constexpr uint32 GameThreadTimeoutMs = 30000;
        if (PayloadFuture.WaitFor(FTimespan::FromMilliseconds(GameThreadTimeoutMs)))
        {
            FDispatchToolResult DispatchResult = PayloadFuture.Get();
            bOutIsError = DispatchResult.bIsError;
            return DispatchResult.Payload;
        }

        bOutIsError = true;
        const TSharedPtr<FJsonObject> TimeoutContext = BuildGameThreadTimeoutContext(TEXT("tool"), Name, static_cast<int32>(GameThreadTimeoutMs));
        AppendDiagnosticEvent(TEXT("error"), TEXT("runtime"), Name, TEXT("Tool execution timed out on the game thread."), TimeoutContext);
        Payload = MakeShared<FJsonObject>();
        Payload->SetStringField(TEXT("code"), TEXT("EXECUTION_TIMEOUT"));
        Payload->SetStringField(TEXT("message"), TEXT("EXECUTION_TIMEOUT"));
        Payload->SetObjectField(TEXT("timeoutContext"), TimeoutContext);
        return Payload;
    }

    if (Name.Equals(TEXT("context")))
    {
        Payload = BuildGetContextToolResult(Arguments);
    }
    else if (Name.Equals(LoomleBridgeConstants::EditorOpenToolName))
    {
        Payload = BuildEditorOpenToolResult(Arguments);
    }
    else if (Name.Equals(LoomleBridgeConstants::EditorFocusToolName))
    {
        Payload = BuildEditorFocusToolResult(Arguments);
    }
    else if (Name.Equals(LoomleBridgeConstants::EditorScreenshotToolName))
    {
        Payload = BuildEditorScreenshotToolResult(Arguments);
    }
    else if (Name.Equals(TEXT("blueprint.list")))
    {
        Payload = BuildBlueprintListToolResult(Arguments);
    }
    else if (Name.Equals(TEXT("blueprint.asset.edit")))
    {
        Payload = BuildBlueprintAssetEditToolResult(Arguments);
    }
    else if (Name.Equals(TEXT("blueprint.member.edit")))
    {
        Payload = BuildBlueprintMemberEditToolResult(Arguments);
    }
    else if (Name.Equals(TEXT("blueprint.query")))
    {
        Payload = BuildBlueprintQueryToolResult(Arguments);
    }
    else if (Name.Equals(TEXT("blueprint.mutate")))
    {
        Payload = BuildBlueprintMutateToolResult(Arguments);
    }
    else if (Name.Equals(TEXT("blueprint.verify")))
    {
        Payload = BuildBlueprintVerifyToolResult(Arguments);
    }
    else if (Name.Equals(TEXT("blueprint.describe")))
    {
        Payload = BuildBlueprintDescribeToolResult(Arguments);
    }
    else if (Name.Equals(TEXT("material.list")))
    {
        Payload = BuildMaterialListToolResult(Arguments);
    }
    else if (Name.Equals(TEXT("material.query")))
    {
        Payload = BuildMaterialQueryToolResult(Arguments);
    }
    else if (Name.Equals(TEXT("material.mutate")))
    {
        Payload = BuildMaterialMutateToolResult(Arguments);
    }
    else if (Name.Equals(TEXT("material.verify")))
    {
        Payload = BuildMaterialVerifyToolResult(Arguments);
    }
    else if (Name.Equals(TEXT("material.describe")))
    {
        Payload = BuildMaterialDescribeToolResult(Arguments);
    }
    else if (Name.Equals(TEXT("pcg.list")))
    {
        Payload = BuildPcgListToolResult(Arguments);
    }
    else if (Name.Equals(TEXT("pcg.query")))
    {
        Payload = BuildPcgQueryToolResult(Arguments);
    }
    else if (Name.Equals(TEXT("pcg.mutate")))
    {
        Payload = BuildPcgMutateToolResult(Arguments);
    }
    else if (Name.Equals(TEXT("pcg.verify")))
    {
        Payload = BuildPcgVerifyToolResult(Arguments);
    }
    else if (Name.Equals(TEXT("pcg.describe")))
    {
        Payload = BuildPcgDescribeToolResult(Arguments);
    }
    else if (Name.Equals(LoomleBridgeConstants::ExecuteToolName))
    {
        Payload = BuildExecutePythonToolResult(Arguments);
    }
    else if (Name.Equals(LoomleBridgeConstants::JobsToolName))
    {
        Payload = BuildJobsToolResult(Arguments);
    }
    else if (Name.Equals(LoomleBridgeConstants::ProfilingToolName))
    {
        Payload = BuildProfilingToolResult(Arguments);
    }
    else if (Name.Equals(LoomleBridgeConstants::DiagnosticTailToolName))
    {
        Payload = BuildDiagnosticTailToolResult(Arguments);
    }
    else if (Name.Equals(LoomleBridgeConstants::LogTailToolName))
    {
        Payload = BuildLogTailToolResult(Arguments);
    }
    else if (Name.Equals(TEXT("widget.query")))
    {
        Payload = BuildWidgetQueryToolResult(Arguments);
    }
    else if (Name.Equals(TEXT("widget.mutate")))
    {
        Payload = BuildWidgetMutateToolResult(Arguments);
    }
    else if (Name.Equals(TEXT("widget.verify")))
    {
        Payload = BuildWidgetVerifyToolResult(Arguments);
    }
    else if (Name.Equals(TEXT("widget.describe")))
    {
        Payload = BuildWidgetDescribeToolResult(Arguments);
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
    if (DomainCode.Equals(TEXT("ASSET_NOT_FOUND")))
    {
        return 1004;
    }
    if (DomainCode.Equals(TEXT("OBJECT_NOT_FOUND")))
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
    if (DomainCode.Equals(TEXT("TARGET_NOT_FOUND")))
    {
        return 1006;
    }
    if (DomainCode.Equals(TEXT("PIN_NOT_FOUND")))
    {
        return 1007;
    }
    if (DomainCode.Equals(TEXT("WORLD_NOT_FOUND")))
    {
        return 1015;
    }
    if (DomainCode.Equals(TEXT("GAME_VIEWPORT_UNAVAILABLE")))
    {
        return 1016;
    }
    if (DomainCode.Equals(TEXT("STAT_UNIT_DATA_UNAVAILABLE")))
    {
        return 1017;
    }
    if (DomainCode.Equals(TEXT("PROFILING_ACTION_UNSUPPORTED")))
    {
        return 1018;
    }
    if (DomainCode.Equals(TEXT("STAT_UNIT_WARMUP_REQUIRED")))
    {
        return 1019;
    }
    if (DomainCode.Equals(TEXT("STATS_GROUP_UNAVAILABLE")))
    {
        return 1020;
    }
    if (DomainCode.Equals(TEXT("STATS_GROUP_WARMUP_REQUIRED")))
    {
        return 1021;
    }
    if (DomainCode.Equals(TEXT("TICKS_DATA_UNAVAILABLE")))
    {
        return 1022;
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
    if (DomainCode.Equals(TEXT("JOB_NOT_FOUND")))
    {
        return 1012;
    }
    if (DomainCode.Equals(TEXT("JOB_RESULT_EXPIRED")))
    {
        return 1013;
    }
    if (DomainCode.Equals(TEXT("JOB_ACTION_UNSUPPORTED")))
    {
        return 1014;
    }
    if (DomainCode.Equals(TEXT("JOB_MODE_UNSUPPORTED")))
    {
        return 1015;
    }
    if (DomainCode.Equals(TEXT("INVALID_EXECUTION_ENVELOPE")))
    {
        return 1000;
    }
    if (DomainCode.Equals(TEXT("IDEMPOTENCY_KEY_REQUIRED")))
    {
        return 1000;
    }
    if (DomainCode.Equals(TEXT("JOB_RUNTIME_UNAVAILABLE")))
    {
        return 1011;
    }
    if (DomainCode.Equals(TEXT("WIDGET_TREE_UNAVAILABLE")))
    {
        return 1023;
    }
    if (DomainCode.Equals(TEXT("WIDGET_PARENT_NOT_PANEL")))
    {
        return 1024;
    }
    if (DomainCode.Equals(TEXT("WIDGET_CLASS_NOT_FOUND")))
    {
        return 1025;
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
