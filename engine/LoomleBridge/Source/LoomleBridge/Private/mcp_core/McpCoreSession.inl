namespace
{
TArray<FString> GraphMutateOpsForType(const FString& GraphType)
{
    TArray<FString> Ops{
        TEXT("addNode.byClass"),
        TEXT("connectPins"),
        TEXT("disconnectPins"),
        TEXT("breakPinLinks"),
        TEXT("setPinDefault"),
        TEXT("removeNode"),
        TEXT("moveNode"),
        TEXT("moveNodeBy"),
        TEXT("moveNodes"),
        TEXT("layoutGraph"),
        TEXT("compile"),
    };
    if (GraphType.Equals(TEXT("blueprint"), ESearchCase::IgnoreCase))
    {
        Ops.Add(TEXT("runScript"));
    }
    return Ops;
}

TSharedPtr<FJsonObject> BuildRuntimeCapabilitiesObject(bool bIsPIE)
{
    TSharedPtr<FJsonObject> Capabilities = MakeShared<FJsonObject>();
    Capabilities->SetBoolField(TEXT("executeAvailable"), true);
    Capabilities->SetBoolField(TEXT("jobsAvailable"), true);
    Capabilities->SetBoolField(TEXT("profilingAvailable"), true);
    Capabilities->SetBoolField(TEXT("graphToolsAvailable"), !bIsPIE);
    Capabilities->SetBoolField(TEXT("editorToolsAvailable"), !bIsPIE);
    return Capabilities;
}

TSharedPtr<FJsonObject> BuildGraphLayoutCapabilitiesObject(const FString& GraphType)
{
    const bool bCanMoveNode = GraphType.Equals(TEXT("blueprint"), ESearchCase::IgnoreCase)
        || GraphType.Equals(TEXT("material"), ESearchCase::IgnoreCase)
        || GraphType.Equals(TEXT("pcg"), ESearchCase::IgnoreCase);
    const FString SizeSource = GraphType.Equals(TEXT("blueprint"), ESearchCase::IgnoreCase)
        ? TEXT("partial")
        : TEXT("unsupported");

    TSharedPtr<FJsonObject> Layout = MakeShared<FJsonObject>();
    Layout->SetBoolField(TEXT("canReadPosition"), true);
    Layout->SetBoolField(TEXT("canReadSize"), false);
    Layout->SetBoolField(TEXT("canReadBounds"), false);
    Layout->SetBoolField(TEXT("canMoveNode"), bCanMoveNode);
    Layout->SetBoolField(TEXT("canBatchMove"), bCanMoveNode);
    Layout->SetBoolField(TEXT("supportsMeasuredGeometry"), false);
    Layout->SetStringField(TEXT("positionSource"), TEXT("model"));
    Layout->SetStringField(TEXT("sizeSource"), SizeSource);
    return Layout;
}

TArray<TSharedPtr<FJsonValue>> MakeJsonStringValueArray(const TArray<FString>& Values)
{
    TArray<TSharedPtr<FJsonValue>> Out;
    for (const FString& Value : Values)
    {
        Out.Add(MakeShared<FJsonValueString>(Value));
    }
    return Out;
}

FString SerializeJsonObject(const TSharedPtr<FJsonObject>& Object)
{
    FString Output;
    const TSharedRef<FCondensedJsonWriter> Writer = TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Output);
    FJsonSerializer::Serialize(Object.ToSharedRef(), Writer);
    return Output;
}
}

FString FLoomleBridgeModule::HandleRequest(int32 ConnectionSerial, const FString& RequestLine)
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

    {
        FScopeLock ScopeLock(&McpSessionStatesMutex);
        McpSessionStates.FindOrAdd(ConnectionSerial);
    }

    if (Method.Equals(TEXT("initialize")))
    {
        {
            FScopeLock ScopeLock(&McpSessionStatesMutex);
            FMcpSessionState& SessionState = McpSessionStates.FindOrAdd(ConnectionSerial);
            SessionState.bInitializeCompleted = true;
            SessionState.bClientInitialized = false;
        }
        return MakeJsonResponse(IdValue, BuildMcpInitializeResult(Params));
    }

    if (Method.Equals(TEXT("notifications/initialized")))
    {
        FScopeLock ScopeLock(&McpSessionStatesMutex);
        FMcpSessionState& SessionState = McpSessionStates.FindOrAdd(ConnectionSerial);
        SessionState.bInitializeCompleted = true;
        SessionState.bClientInitialized = true;
        return FString();
    }

    {
        FScopeLock ScopeLock(&McpSessionStatesMutex);
        const FMcpSessionState* SessionState = McpSessionStates.Find(ConnectionSerial);
        if (SessionState == nullptr || !SessionState->bInitializeCompleted || !SessionState->bClientInitialized)
        {
            return MakeJsonError(IdValue, -32002, TEXT("Server not initialized"));
        }
    }

    if (Method.Equals(TEXT("ping")))
    {
        return MakeJsonResponse(IdValue, MakeShared<FJsonObject>());
    }

    if (Method.Equals(TEXT("tools/list")))
    {
        return MakeJsonResponse(IdValue, BuildMcpToolsListResult());
    }

    if (Method.Equals(TEXT("tools/call")))
    {
        bool bCallHasJsonRpcError = false;
        int32 CallErrorCode = -32602;
        FString CallErrorMessage = TEXT("Invalid params");
        TSharedPtr<FJsonObject> CallErrorData;
        TSharedPtr<FJsonObject> Result = BuildMcpCallToolResult(Params, bCallHasJsonRpcError, CallErrorCode, CallErrorMessage, CallErrorData);
        if (bCallHasJsonRpcError)
        {
            return MakeJsonErrorEx(IdValue, CallErrorCode, CallErrorMessage, CallErrorData);
        }
        return MakeJsonResponse(IdValue, Result);
    }

    return MakeJsonError(IdValue, -32601, FString::Printf(TEXT("Method not found: %s"), *Method));
}

void FLoomleBridgeModule::ForgetMcpSessionState(int32 ConnectionSerial)
{
    FScopeLock ScopeLock(&McpSessionStatesMutex);
    McpSessionStates.Remove(ConnectionSerial);
}

TSharedPtr<FJsonObject> FLoomleBridgeModule::BuildMcpInitializeResult(const TSharedPtr<FJsonObject>& Params) const
{
    return Loomle::McpCore::BuildInitializeResult(Params);
}

TSharedPtr<FJsonObject> FLoomleBridgeModule::BuildMcpToolsListResult() const
{
    return Loomle::McpCore::BuildToolsListResult();
}

TSharedPtr<FJsonObject> FLoomleBridgeModule::BuildMcpCallToolResult(
    const TSharedPtr<FJsonObject>& Params,
    bool& bOutHasJsonRpcError,
    int32& OutErrorCode,
    FString& OutErrorMessage,
    TSharedPtr<FJsonObject>& OutErrorData)
{
    bOutHasJsonRpcError = false;
    OutErrorCode = -32602;
    OutErrorMessage = TEXT("Invalid params");
    OutErrorData = nullptr;

    FString ToolName;
    if (!Params->TryGetStringField(TEXT("name"), ToolName) || ToolName.IsEmpty())
    {
        bOutHasJsonRpcError = true;
        OutErrorData = MakeShared<FJsonObject>();
        OutErrorData->SetStringField(TEXT("detail"), TEXT("tools/call requires params.name"));
        return nullptr;
    }
    if (!Loomle::McpCore::IsKnownTool(ToolName))
    {
        bOutHasJsonRpcError = true;
        OutErrorCode = -32602;
        OutErrorMessage = TEXT("Unknown tool");
        OutErrorData = MakeShared<FJsonObject>();
        OutErrorData->SetStringField(TEXT("detail"), FString::Printf(TEXT("Unknown tool: %s"), *ToolName));
        return nullptr;
    }

    TSharedPtr<FJsonObject> Arguments = MakeShared<FJsonObject>();
    const TSharedPtr<FJsonObject>* ArgumentsPtr = nullptr;
    if (Params->TryGetObjectField(TEXT("arguments"), ArgumentsPtr) && ArgumentsPtr && (*ArgumentsPtr).IsValid())
    {
        Arguments = *ArgumentsPtr;
    }

    bool bToolError = false;
    TSharedPtr<FJsonObject> Payload;
    if (ToolName.Equals(TEXT("loomle")))
    {
        Payload = BuildLoomleToolResult();
    }
    else if (ToolName.Equals(TEXT("graph")))
    {
        Payload = BuildGraphDescriptorToolResult(Arguments);
    }
    else
    {
        Payload = DispatchTool(ToolName, Arguments, bToolError);
    }

    if (bToolError && Payload.IsValid())
    {
        FString ExistingDomainCode;
        if (!Payload->TryGetStringField(TEXT("domainCode"), ExistingDomainCode) || ExistingDomainCode.IsEmpty())
        {
            FString ExistingCode;
            if (Payload->TryGetStringField(TEXT("code"), ExistingCode) && !ExistingCode.IsEmpty())
            {
                Payload->SetStringField(TEXT("domainCode"), ExistingCode);
            }
        }

        FString ExistingDetail;
        if (!Payload->TryGetStringField(TEXT("detail"), ExistingDetail) || ExistingDetail.IsEmpty())
        {
            Payload->SetStringField(TEXT("detail"), SerializeJsonObject(Payload));
        }
    }

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    TArray<TSharedPtr<FJsonValue>> Content;
    TSharedPtr<FJsonObject> TextContent = MakeShared<FJsonObject>();
    TextContent->SetStringField(TEXT("type"), TEXT("text"));
    TextContent->SetStringField(TEXT("text"), SerializeJsonObject(Payload));
    Content.Add(MakeShared<FJsonValueObject>(TextContent));

    Result->SetArrayField(TEXT("content"), Content);
    Result->SetObjectField(TEXT("structuredContent"), Payload);
    Result->SetBoolField(TEXT("isError"), bToolError);
    return Result;
}
