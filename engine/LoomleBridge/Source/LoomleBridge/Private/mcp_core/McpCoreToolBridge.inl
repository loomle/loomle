TSharedPtr<FJsonObject> FLoomleBridgeModule::BuildLoomleToolResult() const
{
    const TSharedPtr<FJsonObject> Health = BuildRpcHealthResult();
    const bool bIsPIE = bIsPIESnapshot.Load();
    FString Status;
    Health->TryGetStringField(TEXT("status"), Status);

    FString EditorBusyReason;
    Health->TryGetStringField(TEXT("editorBusyReason"), EditorBusyReason);

    TSharedPtr<FJsonObject> Runtime = MakeShared<FJsonObject>();
    Runtime->SetBoolField(TEXT("rpcConnected"), true);
    Runtime->SetBoolField(TEXT("listenerReady"), true);
    Runtime->SetBoolField(TEXT("isPIE"), bIsPIE);
    Runtime->SetStringField(TEXT("editorBusyReason"), EditorBusyReason);
    Runtime->SetObjectField(TEXT("capabilities"), BuildRuntimeCapabilitiesObject(bIsPIE));
    Runtime->SetObjectField(TEXT("rpcHealth"), CloneJsonObject(Health));

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetStringField(TEXT("status"), Status);
    Result->SetStringField(TEXT("domainCode"), TEXT(""));
    Result->SetStringField(TEXT("message"), TEXT(""));
    Result->SetObjectField(TEXT("runtime"), Runtime);
    return Result;
}

TSharedPtr<FJsonObject> FLoomleBridgeModule::BuildGraphDescriptorToolResult(const TSharedPtr<FJsonObject>& Arguments) const
{
    FString GraphType = TEXT("blueprint");
    Arguments->TryGetStringField(TEXT("graphType"), GraphType);

    const TSharedPtr<FJsonObject> Health = BuildRpcHealthResult();
    const bool bIsPIE = bIsPIESnapshot.Load();
    FString Status;
    Health->TryGetStringField(TEXT("status"), Status);

    TSharedPtr<FJsonObject> Runtime = MakeShared<FJsonObject>();
    Runtime->SetBoolField(TEXT("isPIE"), bIsPIE);
    {
        FString EditorBusyReason;
        Health->TryGetStringField(TEXT("editorBusyReason"), EditorBusyReason);
        Runtime->SetStringField(TEXT("editorBusyReason"), EditorBusyReason);
    }
    Runtime->SetObjectField(TEXT("capabilities"), BuildRuntimeCapabilitiesObject(bIsPIE));
    Runtime->SetObjectField(TEXT("rpcHealth"), CloneJsonObject(Health));

    TSharedPtr<FJsonObject> Limits = MakeShared<FJsonObject>();
    Limits->SetNumberField(TEXT("defaultLimit"), 200);
    Limits->SetNumberField(TEXT("maxLimit"), 1000);
    Limits->SetNumberField(TEXT("maxOpsPerMutate"), 200);

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetStringField(TEXT("status"), bIsPIE ? TEXT("blocked") : Status);
    Result->SetStringField(TEXT("graphType"), GraphType);
    Result->SetStringField(TEXT("version"), TEXT("1.0"));
    Result->SetStringField(TEXT("domainCode"), bIsPIE ? TEXT("EDITOR_BUSY") : TEXT(""));
    Result->SetStringField(TEXT("message"), bIsPIE ? TEXT("Unreal Editor is currently in Play In Editor (PIE).") : TEXT(""));
    Result->SetArrayField(TEXT("ops"), MakeJsonStringValueArray(GraphMutateOpsForType(GraphType)));
    Result->SetObjectField(TEXT("limits"), Limits);
    Result->SetObjectField(TEXT("layoutCapabilities"), BuildGraphLayoutCapabilitiesObject(GraphType));
    Result->SetObjectField(TEXT("runtime"), Runtime);
    return Result;
}
