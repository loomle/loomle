// RPC entrypoints and tool dispatch for Loomle Bridge.
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
        TEXT("context"), TEXT("jobs"), TEXT("profiling"), TEXT("editor.open"), TEXT("editor.focus"), TEXT("editor.screenshot"), TEXT("graph.verify"), TEXT("execute"),
        TEXT("graph.list"), TEXT("graph.resolve"), TEXT("graph.query"),
        TEXT("graph.mutate"),
        TEXT("diag.tail")
    }));
    Result->SetArrayField(TEXT("graphTypes"), MakeStringArray({TEXT("blueprint"), TEXT("material"), TEXT("pcg")}));

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
        AppendDiagEvent(TEXT("error"), TEXT("runtime"), ToolName, Message, DiagContext);

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

    if (!IsInGameThread() && !Name.Equals(LoomleBridgeConstants::GraphQueryToolName) && !Name.Equals(LoomleBridgeConstants::JobsToolName))
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
        AppendDiagEvent(TEXT("error"), TEXT("runtime"), Name, TEXT("Tool execution timed out on the game thread."), TimeoutContext);
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
    else if (Name.Equals(LoomleBridgeConstants::GraphVerifyToolName))
    {
        Payload = BuildGraphVerifyToolResult(Arguments);
    }
    else if (Name.Equals(LoomleBridgeConstants::GraphListToolName))
    {
        Payload = BuildGraphListToolResult(Arguments);
    }
    else if (Name.Equals(LoomleBridgeConstants::GraphResolveToolName))
    {
        Payload = BuildGraphResolveToolResult(Arguments);
    }
    else if (Name.Equals(LoomleBridgeConstants::GraphQueryToolName))
    {
        Payload = BuildGraphQueryToolResult(Arguments);
    }
    else if (Name.Equals(LoomleBridgeConstants::GraphMutateToolName))
    {
        Payload = BuildGraphMutateToolResult(Arguments);
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
    else if (Name.Equals(LoomleBridgeConstants::DiagTailToolName))
    {
        Payload = BuildDiagTailToolResult(Arguments);
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
