// Runtime tool handlers (context / selection / execute / diag).
namespace
{
constexpr int32 DefaultDiagTailLimit = 200;
constexpr int32 MaxDiagTailLimit = 1000;

bool TryReadJsonUInt64Field(const TSharedPtr<FJsonObject>& Object, const TCHAR* FieldName, uint64& Out)
{
    Out = 0;
    if (!Object.IsValid())
    {
        return false;
    }

    double Number = 0.0;
    if (Object->TryGetNumberField(FieldName, Number))
    {
        if (Number < 0.0)
        {
            return false;
        }
        Out = static_cast<uint64>(Number);
        return true;
    }

    FString Text;
    if (Object->TryGetStringField(FieldName, Text))
    {
        return LexTryParseString<uint64>(Out, *Text);
    }

    return false;
}

bool DiagEventMatchesFilters(const TSharedPtr<FJsonObject>& Event, const TSharedPtr<FJsonObject>& Filters)
{
    if (!Event.IsValid() || !Filters.IsValid())
    {
        return true;
    }

    auto MatchStringField = [&](const TCHAR* FilterField, const TCHAR* EventField) -> bool
    {
        FString FilterValue;
        if (!Filters->TryGetStringField(FilterField, FilterValue) || FilterValue.IsEmpty())
        {
            return true;
        }

        FString EventValue;
        if (!Event->TryGetStringField(EventField, EventValue))
        {
            return false;
        }
        return EventValue.Equals(FilterValue, ESearchCase::IgnoreCase);
    };

    if (!MatchStringField(TEXT("severity"), TEXT("severity")))
    {
        return false;
    }
    if (!MatchStringField(TEXT("category"), TEXT("category")))
    {
        return false;
    }
    if (!MatchStringField(TEXT("source"), TEXT("source")))
    {
        return false;
    }

    FString AssetPathPrefix;
    if (!Filters->TryGetStringField(TEXT("assetPathPrefix"), AssetPathPrefix) || AssetPathPrefix.IsEmpty())
    {
        return true;
    }

    FString AssetPath;
    if (!Event->TryGetStringField(TEXT("assetPath"), AssetPath))
    {
        const TSharedPtr<FJsonObject>* ContextObject = nullptr;
        if (Event->TryGetObjectField(TEXT("context"), ContextObject) && ContextObject && (*ContextObject).IsValid())
        {
            (*ContextObject)->TryGetStringField(TEXT("assetPath"), AssetPath);
        }
    }

    if (AssetPath.IsEmpty())
    {
        return false;
    }

    return AssetPath.StartsWith(AssetPathPrefix);
}

FString DetermineVerifyStatusFromDiagnostics(const TArray<TSharedPtr<FJsonValue>>& Diagnostics, const bool bTreatMissingDiagnosticsAsOk = true)
{
    bool bHasWarning = false;
    for (const TSharedPtr<FJsonValue>& DiagnosticValue : Diagnostics)
    {
        const TSharedPtr<FJsonObject>* Diagnostic = nullptr;
        if (!DiagnosticValue.IsValid() || !DiagnosticValue->TryGetObject(Diagnostic) || Diagnostic == nullptr || !(*Diagnostic).IsValid())
        {
            continue;
        }

        FString Severity;
        if ((*Diagnostic)->TryGetStringField(TEXT("severity"), Severity))
        {
            Severity = Severity.ToLower();
            if (Severity.Equals(TEXT("error")))
            {
                return TEXT("error");
            }
            if (Severity.Equals(TEXT("warning")))
            {
                bHasWarning = true;
            }
        }
    }

    if (bHasWarning)
    {
        return TEXT("warn");
    }

    return bTreatMissingDiagnosticsAsOk ? TEXT("ok") : TEXT("error");
}

TArray<TSharedPtr<FJsonValue>> CloneJsonArrayField(const TSharedPtr<FJsonObject>& Object, const TCHAR* FieldName)
{
    const TArray<TSharedPtr<FJsonValue>>* Field = nullptr;
    if (Object.IsValid() && Object->TryGetArrayField(FieldName, Field) && Field != nullptr)
    {
        return *Field;
    }
    return {};
}

void CopyOptionalStringField(const TSharedPtr<FJsonObject>& Source, const TSharedPtr<FJsonObject>& Dest, const TCHAR* FieldName)
{
    FString Value;
    if (Source.IsValid() && Dest.IsValid() && Source->TryGetStringField(FieldName, Value))
    {
        Dest->SetStringField(FieldName, Value);
    }
}

void CopyOptionalObjectField(const TSharedPtr<FJsonObject>& Source, const TSharedPtr<FJsonObject>& Dest, const TCHAR* FieldName)
{
    const TSharedPtr<FJsonObject>* Value = nullptr;
    if (Source.IsValid() && Dest.IsValid() && Source->TryGetObjectField(FieldName, Value) && Value != nullptr && (*Value).IsValid())
    {
        Dest->SetObjectField(FieldName, CloneJsonObject(*Value));
    }
}

FString DiagnosticSeverityFromVerbosity(const ELogVerbosity::Type Verbosity)
{
    const ELogVerbosity::Type MaskedVerbosity = static_cast<ELogVerbosity::Type>(Verbosity & ELogVerbosity::VerbosityMask);
    if (MaskedVerbosity == ELogVerbosity::Warning)
    {
        return TEXT("warning");
    }
    if (MaskedVerbosity == ELogVerbosity::Error || MaskedVerbosity == ELogVerbosity::Fatal)
    {
        return TEXT("error");
    }
    return TEXT("info");
}

FString BuildDiagnosticIdentityKey(const TSharedPtr<FJsonObject>& Diagnostic)
{
    if (!Diagnostic.IsValid())
    {
        return TEXT("");
    }

    FString Code;
    FString Message;
    FString NodeId;
    FString SourceKind;
    double Seq = 0.0;
    Diagnostic->TryGetStringField(TEXT("code"), Code);
    Diagnostic->TryGetStringField(TEXT("message"), Message);
    Diagnostic->TryGetStringField(TEXT("nodeId"), NodeId);
    Diagnostic->TryGetStringField(TEXT("sourceKind"), SourceKind);
    Diagnostic->TryGetNumberField(TEXT("seq"), Seq);
    return FString::Printf(TEXT("%s|%s|%s|%s|%.0f"), *Code, *Message, *NodeId, *SourceKind, Seq);
}

void AppendUniqueDiagnostic(
    TArray<TSharedPtr<FJsonValue>>& Diagnostics,
    TSet<FString>& SeenDiagnosticKeys,
    const TSharedPtr<FJsonObject>& Diagnostic)
{
    if (!Diagnostic.IsValid())
    {
        return;
    }

    const FString DiagnosticKey = BuildDiagnosticIdentityKey(Diagnostic);
    if (!DiagnosticKey.IsEmpty() && SeenDiagnosticKeys.Contains(DiagnosticKey))
    {
        return;
    }

    if (!DiagnosticKey.IsEmpty())
    {
        SeenDiagnosticKeys.Add(DiagnosticKey);
    }

    Diagnostics.Add(MakeShared<FJsonValueObject>(Diagnostic));
}

TSet<FString> BuildDiagnosticIdentitySet(const TArray<TSharedPtr<FJsonValue>>& Diagnostics)
{
    TSet<FString> SeenDiagnosticKeys;
    for (const TSharedPtr<FJsonValue>& DiagnosticValue : Diagnostics)
    {
        const TSharedPtr<FJsonObject>* Diagnostic = nullptr;
        if (!DiagnosticValue.IsValid() || !DiagnosticValue->TryGetObject(Diagnostic) || Diagnostic == nullptr || !(*Diagnostic).IsValid())
        {
            continue;
        }

        const FString DiagnosticKey = BuildDiagnosticIdentityKey(*Diagnostic);
        if (!DiagnosticKey.IsEmpty())
        {
            SeenDiagnosticKeys.Add(DiagnosticKey);
        }
    }

    return SeenDiagnosticKeys;
}

TSharedPtr<FJsonObject> MakeRecentDiagEventDiagnostic(const TSharedPtr<FJsonObject>& Event)
{
    if (!Event.IsValid())
    {
        return nullptr;
    }

    FString Message;
    if (!Event->TryGetStringField(TEXT("message"), Message) || Message.IsEmpty())
    {
        return nullptr;
    }

    FString Severity = TEXT("error");
    Event->TryGetStringField(TEXT("severity"), Severity);

    FString Source = TEXT("log");
    Event->TryGetStringField(TEXT("source"), Source);

    FString Timestamp;
    Event->TryGetStringField(TEXT("ts"), Timestamp);

    TSharedPtr<FJsonObject> Diagnostic = MakeShared<FJsonObject>();
    Diagnostic->SetStringField(TEXT("code"), TEXT("PCG_RECENT_LOG_EVENT"));
    Diagnostic->SetStringField(TEXT("severity"), Severity.IsEmpty() ? TEXT("error") : Severity.ToLower());
    Diagnostic->SetStringField(TEXT("message"), Message);
    Diagnostic->SetStringField(TEXT("sourceKind"), TEXT("diagWindow"));
    Diagnostic->SetStringField(TEXT("source"), Source);
    if (!Timestamp.IsEmpty())
    {
        Diagnostic->SetStringField(TEXT("ts"), Timestamp);
    }

    double Seq = 0.0;
    if (Event->TryGetNumberField(TEXT("seq"), Seq))
    {
        Diagnostic->SetNumberField(TEXT("seq"), Seq);
    }

    const TSharedPtr<FJsonObject>* Context = nullptr;
    if (Event->TryGetObjectField(TEXT("context"), Context) && Context != nullptr && (*Context).IsValid())
    {
        FString CategoryName;
        if ((*Context)->TryGetStringField(TEXT("categoryName"), CategoryName) && !CategoryName.IsEmpty())
        {
            Diagnostic->SetStringField(TEXT("logCategory"), CategoryName);
        }
    }

    FString AssetPath;
    if (Event->TryGetStringField(TEXT("assetPath"), AssetPath) && !AssetPath.IsEmpty())
    {
        Diagnostic->SetStringField(TEXT("assetPath"), AssetPath);
    }

    return Diagnostic;
}

TSet<FString> DiagnosticCodeSet(const TArray<TSharedPtr<FJsonValue>>& Diagnostics)
{
    TSet<FString> Codes;
    for (const TSharedPtr<FJsonValue>& DiagnosticValue : Diagnostics)
    {
        const TSharedPtr<FJsonObject>* Diagnostic = nullptr;
        if (!DiagnosticValue.IsValid() || !DiagnosticValue->TryGetObject(Diagnostic) || Diagnostic == nullptr || !(*Diagnostic).IsValid())
        {
            continue;
        }

        FString Code;
        if ((*Diagnostic)->TryGetStringField(TEXT("code"), Code) && !Code.IsEmpty())
        {
            Codes.Add(Code);
        }
    }
    return Codes;
}

void AddVerifyCheck(
    TArray<TSharedPtr<FJsonValue>>& Checks,
    const FString& Name,
    const bool bPass,
    const TSharedPtr<FJsonValue>& Expected,
    const TSharedPtr<FJsonValue>& Actual)
{
    TSharedPtr<FJsonObject> Check = MakeShared<FJsonObject>();
    Check->SetStringField(TEXT("name"), Name);
    Check->SetBoolField(TEXT("pass"), bPass);
    if (Expected.IsValid())
    {
        Check->SetField(TEXT("expected"), Expected);
    }
    if (Actual.IsValid())
    {
        Check->SetField(TEXT("actual"), Actual);
    }
    Checks.Add(MakeShared<FJsonValueObject>(Check));
}

bool ReadComparisonInt64(const TSharedPtr<FJsonObject>& Object, const TCHAR* FieldName, int64& OutValue)
{
    if (!Object.IsValid())
    {
        return false;
    }

    double Number = 0.0;
    if (!Object->TryGetNumberField(FieldName, Number))
    {
        return false;
    }

    OutValue = static_cast<int64>(Number);
    return true;
}

bool EvaluateNumericExpectation(const TSharedPtr<FJsonObject>& Expectation, const int64 ActualValue)
{
    if (!Expectation.IsValid())
    {
        return true;
    }

    int64 Target = 0;
    if (ReadComparisonInt64(Expectation, TEXT("eq"), Target) && ActualValue != Target)
    {
        return false;
    }
    if (ReadComparisonInt64(Expectation, TEXT("gt"), Target) && !(ActualValue > Target))
    {
        return false;
    }
    if (ReadComparisonInt64(Expectation, TEXT("gte"), Target) && !(ActualValue >= Target))
    {
        return false;
    }
    if (ReadComparisonInt64(Expectation, TEXT("lt"), Target) && !(ActualValue < Target))
    {
        return false;
    }
    if (ReadComparisonInt64(Expectation, TEXT("lte"), Target) && !(ActualValue <= Target))
    {
        return false;
    }

    return true;
}
}

void FLoomleBridgeModule::InitializeDiagStore()
{
    FScopeLock Lock(&DiagStoreMutex);
    if (bDiagStoreInitialized)
    {
        return;
    }

    DiagStoreDirPath = FPaths::Combine(FPaths::ProjectDir(), TEXT("Loomle"), TEXT("runtime"), TEXT("diag"));
    DiagStoreFilePath = FPaths::Combine(DiagStoreDirPath, TEXT("diag.jsonl"));
    IFileManager::Get().MakeDirectory(*DiagStoreDirPath, true);

    NextDiagSeq = 1;
    TArray<FString> Lines;
    if (FPaths::FileExists(DiagStoreFilePath) && FFileHelper::LoadFileToStringArray(Lines, *DiagStoreFilePath))
    {
        for (int32 Index = Lines.Num() - 1; Index >= 0; --Index)
        {
            const FString& Line = Lines[Index];
            if (Line.TrimStartAndEnd().IsEmpty())
            {
                continue;
            }

            TSharedPtr<FJsonObject> Event;
            const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Line);
            if (!FJsonSerializer::Deserialize(Reader, Event) || !Event.IsValid())
            {
                continue;
            }

            uint64 ParsedSeq = 0;
            if (!TryReadJsonUInt64Field(Event, TEXT("seq"), ParsedSeq))
            {
                continue;
            }

            NextDiagSeq = ParsedSeq + 1;
            break;
        }
    }

    bDiagStoreInitialized = true;
}

void FLoomleBridgeModule::AppendDiagEvent(
    const FString& Severity,
    const FString& Category,
    const FString& Source,
    const FString& Message,
    const TSharedPtr<FJsonObject>& Context)
{
    if (!bDiagStoreInitialized)
    {
        InitializeDiagStore();
    }

    FScopeLock Lock(&DiagStoreMutex);
    if (DiagStoreFilePath.IsEmpty())
    {
        return;
    }

    const uint64 Seq = NextDiagSeq;
    TSharedPtr<FJsonObject> Event = MakeShared<FJsonObject>();
    Event->SetNumberField(TEXT("seq"), static_cast<double>(Seq));
    Event->SetStringField(TEXT("ts"), FDateTime::UtcNow().ToIso8601());
    Event->SetStringField(TEXT("severity"), Severity.ToLower());
    Event->SetStringField(TEXT("category"), Category.ToLower());
    Event->SetStringField(TEXT("source"), Source.ToLower());
    Event->SetStringField(TEXT("message"), Message);
    if (Context.IsValid())
    {
        Event->SetObjectField(TEXT("context"), Context);
        FString AssetPath;
        if (Context->TryGetStringField(TEXT("assetPath"), AssetPath) && !AssetPath.IsEmpty())
        {
            Event->SetStringField(TEXT("assetPath"), AssetPath);
        }
    }

    FString JsonLine;
    const TSharedRef<FCondensedJsonWriter> Writer = TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&JsonLine);
    if (!FJsonSerializer::Serialize(Event.ToSharedRef(), Writer))
    {
        return;
    }

    JsonLine.AppendChar(TEXT('\n'));
    if (FFileHelper::SaveStringToFile(
            JsonLine,
            *DiagStoreFilePath,
            FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM,
            &IFileManager::Get(),
            FILEWRITE_Append))
    {
        ++NextDiagSeq;
        return;
    }

    // Avoid recursive logging loops when persistence itself fails.
}

void FLoomleBridgeModule::HandleLogLine(const FString& Message, ELogVerbosity::Type Verbosity, const FName& Category)
{
    if (Message.IsEmpty())
    {
        return;
    }

    const FString CategoryName = Category.ToString();
    if (CategoryName.Equals(TEXT("LogLoomleBridge"), ESearchCase::IgnoreCase))
    {
        return;
    }

    const FString Severity = (Verbosity == ELogVerbosity::Warning) ? TEXT("warning") : TEXT("error");
    TSharedPtr<FJsonObject> Context = MakeShared<FJsonObject>();
    Context->SetStringField(TEXT("categoryName"), CategoryName);
    AppendDiagEvent(Severity, TEXT("runtime"), TEXT("log"), Message, Context);
}

void FLoomleBridgeModule::HandleBlueprintCompiled()
{
    TSet<FString> CurrentErrorAssets;
    for (TObjectIterator<UBlueprint> It; It; ++It)
    {
        UBlueprint* Blueprint = *It;
        if (Blueprint == nullptr || Blueprint->HasAnyFlags(RF_Transient) || Blueprint->Status != BS_Error)
        {
            continue;
        }

        const FString AssetPath = Blueprint->GetPathName();
        if (AssetPath.IsEmpty())
        {
            continue;
        }

        CurrentErrorAssets.Add(AssetPath);
        if (BlueprintCompileErrorAssets.Contains(AssetPath))
        {
            continue;
        }

        TSharedPtr<FJsonObject> Context = MakeShared<FJsonObject>();
        Context->SetStringField(TEXT("assetPath"), AssetPath);
        Context->SetStringField(TEXT("assetName"), Blueprint->GetName());
        Context->SetStringField(TEXT("status"), TEXT("error"));
        AppendDiagEvent(
            TEXT("error"),
            TEXT("compile"),
            TEXT("blueprint"),
            FString::Printf(TEXT("Blueprint compile failed: %s"), *Blueprint->GetName()),
            Context);
    }

    BlueprintCompileErrorAssets = MoveTemp(CurrentErrorAssets);
}

TSharedPtr<FJsonObject> FLoomleBridgeModule::BuildGraphVerifyToolResult(const TSharedPtr<FJsonObject>& Arguments)
{
    if (!bDiagStoreInitialized)
    {
        InitializeDiagStore();
    }

    uint64 DiagSeqBeforeVerify = 0;
    {
        FScopeLock Lock(&DiagStoreMutex);
        DiagSeqBeforeVerify = NextDiagSeq > 0 ? (NextDiagSeq - 1) : 0;
    }

    auto ReadRecentVerifyDiagEvents = [&](const uint64 FromSeq, const FString& AssetPath) -> TArray<TSharedPtr<FJsonValue>>
    {
        TArray<TSharedPtr<FJsonValue>> Items;

        FScopeLock Lock(&DiagStoreMutex);
        TArray<FString> Lines;
        if (DiagStoreFilePath.IsEmpty() || !FPaths::FileExists(DiagStoreFilePath))
        {
            return Items;
        }

        FFileHelper::LoadFileToStringArray(Lines, *DiagStoreFilePath);
        for (const FString& Line : Lines)
        {
            if (Line.TrimStartAndEnd().IsEmpty())
            {
                continue;
            }

            TSharedPtr<FJsonObject> Event;
            const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Line);
            if (!FJsonSerializer::Deserialize(Reader, Event) || !Event.IsValid())
            {
                continue;
            }

            uint64 Seq = 0;
            if (!TryReadJsonUInt64Field(Event, TEXT("seq"), Seq) || Seq <= FromSeq)
            {
                continue;
            }

            FString EventSource;
            Event->TryGetStringField(TEXT("source"), EventSource);

            FString EventAssetPath;
            Event->TryGetStringField(TEXT("assetPath"), EventAssetPath);

            FString CategoryName;
            const TSharedPtr<FJsonObject>* Context = nullptr;
            if (Event->TryGetObjectField(TEXT("context"), Context) && Context != nullptr && (*Context).IsValid())
            {
                if (EventAssetPath.IsEmpty())
                {
                    (*Context)->TryGetStringField(TEXT("assetPath"), EventAssetPath);
                }
                (*Context)->TryGetStringField(TEXT("categoryName"), CategoryName);
            }

            const bool bAssetMatched = !AssetPath.IsEmpty()
                && !EventAssetPath.IsEmpty()
                && EventAssetPath.StartsWith(AssetPath);
            const bool bIsVerifyEvent = EventSource.Equals(TEXT("graph.verify"), ESearchCase::IgnoreCase);
            const bool bIsPcgLogEvent = CategoryName.Equals(TEXT("LogPCG"), ESearchCase::IgnoreCase)
                || CategoryName.Equals(TEXT("LogPCGEditor"), ESearchCase::IgnoreCase);

            if (!bAssetMatched && !bIsVerifyEvent && !bIsPcgLogEvent)
            {
                continue;
            }

            Items.Add(MakeShared<FJsonValueObject>(Event));
            if (Items.Num() >= 64)
            {
                break;
            }
        }

        return Items;
    };

    const TSharedPtr<FJsonObject> QueryResult = BuildGraphQueryToolResult(Arguments);
    bool bQueryError = false;
    QueryResult->TryGetBoolField(TEXT("isError"), bQueryError);
    if (bQueryError)
    {
        return QueryResult;
    }

    const TSharedPtr<FJsonObject> MutateArgs = CloneJsonObject(Arguments);
    TArray<TSharedPtr<FJsonValue>> Ops;
    TSharedPtr<FJsonObject> CompileOp = MakeShared<FJsonObject>();
    CompileOp->SetStringField(TEXT("op"), TEXT("compile"));
    Ops.Add(MakeShared<FJsonValueObject>(CompileOp));
    MutateArgs->SetArrayField(TEXT("ops"), Ops);

    const TSharedPtr<FJsonObject> MutateResult = BuildGraphMutateToolResult(MutateArgs);
    bool bMutateError = false;
    MutateResult->TryGetBoolField(TEXT("isError"), bMutateError);

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetBoolField(TEXT("isError"), false);
    CopyOptionalStringField(MutateResult, Result, TEXT("graphType"));
    if (!Result->HasField(TEXT("graphType")))
    {
        CopyOptionalStringField(QueryResult, Result, TEXT("graphType"));
    }
    CopyOptionalStringField(MutateResult, Result, TEXT("assetPath"));
    if (!Result->HasField(TEXT("assetPath")))
    {
        CopyOptionalStringField(QueryResult, Result, TEXT("assetPath"));
    }
    CopyOptionalStringField(MutateResult, Result, TEXT("graphName"));
    if (!Result->HasField(TEXT("graphName")))
    {
        CopyOptionalStringField(QueryResult, Result, TEXT("graphName"));
    }
    CopyOptionalObjectField(MutateResult, Result, TEXT("graphRef"));
    if (!Result->HasField(TEXT("graphRef")))
    {
        CopyOptionalObjectField(QueryResult, Result, TEXT("graphRef"));
    }
    CopyOptionalStringField(MutateResult, Result, TEXT("previousRevision"));
    CopyOptionalStringField(MutateResult, Result, TEXT("newRevision"));

    FString GraphType;
    Result->TryGetStringField(TEXT("graphType"), GraphType);
    FString AssetPath;
    Result->TryGetStringField(TEXT("assetPath"), AssetPath);
    FString GraphName;
    Result->TryGetStringField(TEXT("graphName"), GraphName);

    const TArray<TSharedPtr<FJsonValue>> QueryDiagnostics = CloneJsonArrayField(QueryResult, TEXT("diagnostics"));
    TArray<TSharedPtr<FJsonValue>> CompileDiagnostics = CloneJsonArrayField(MutateResult, TEXT("diagnostics"));
    TArray<TSharedPtr<FJsonValue>> Diagnostics = QueryDiagnostics;
    Diagnostics.Append(CompileDiagnostics);
    TSet<FString> SeenDiagnosticKeys = BuildDiagnosticIdentitySet(Diagnostics);
    TSet<FString> SeenCompileDiagnosticKeys = BuildDiagnosticIdentitySet(CompileDiagnostics);

    TSharedPtr<FJsonObject> QueryReport = MakeShared<FJsonObject>();
    CopyOptionalStringField(QueryResult, QueryReport, TEXT("revision"));
    const TSharedPtr<FJsonObject>* Meta = nullptr;
    if (QueryResult->TryGetObjectField(TEXT("meta"), Meta) && Meta != nullptr && (*Meta).IsValid())
    {
        QueryReport->SetObjectField(TEXT("queryMeta"), CloneJsonObject(*Meta));
    }
    QueryReport->SetArrayField(TEXT("diagnostics"), QueryDiagnostics);
    Result->SetObjectField(TEXT("queryReport"), QueryReport);

    const TArray<TSharedPtr<FJsonValue>> OpResults = CloneJsonArrayField(MutateResult, TEXT("opResults"));
    bool bCompileOpReportedOk = false;
    bool bHasCompileOpResult = false;
    bool bCompilationChanged = false;
    const TSharedPtr<FJsonObject>* FirstOpResult = nullptr;
    if (OpResults.Num() > 0)
    {
        if (OpResults[0].IsValid() && OpResults[0]->TryGetObject(FirstOpResult) && FirstOpResult != nullptr && (*FirstOpResult).IsValid())
        {
            bHasCompileOpResult = true;
            (*FirstOpResult)->TryGetBoolField(TEXT("ok"), bCompileOpReportedOk);
            (*FirstOpResult)->TryGetBoolField(TEXT("changed"), bCompilationChanged);
        }
    }
    const bool bCompileSucceeded = !bMutateError && (!bHasCompileOpResult || bCompileOpReportedOk);

    FString MutateCode;
    FString MutateMessage;
    MutateResult->TryGetStringField(TEXT("code"), MutateCode);
    MutateResult->TryGetStringField(TEXT("message"), MutateMessage);

    int32 PcgVisualLogDiagnosticCount = 0;
    if (GraphType.Equals(TEXT("pcg"), ESearchCase::IgnoreCase))
    {
        if (UPCGGraph* PcgGraph = LoadPcgGraphByAssetPath(AssetPath))
        {
            if (IPCGEditorModule* PCGEditorModule = IPCGEditorModule::Get())
            {
                const FPCGNodeVisualLogs& NodeVisualLogs = PCGEditorModule->GetNodeVisualLogs();
                for (const UPCGNode* Node : PcgGraph->GetNodes())
                {
                    if (Node == nullptr)
                    {
                        continue;
                    }

                    FPCGPerNodeVisualLogs NodeLogs;
                    TArray<const IPCGGraphExecutionSource*> NodeLogSources;
                    NodeVisualLogs.GetLogsAndSources(Node, NodeLogs, NodeLogSources);
                    for (int32 LogIndex = 0; LogIndex < NodeLogs.Num(); ++LogIndex)
                    {
                        const FPCGNodeLogEntry& NodeLog = NodeLogs[LogIndex];
                        TSharedPtr<FJsonObject> Diagnostic = MakeShared<FJsonObject>();
                        Diagnostic->SetStringField(TEXT("code"), TEXT("PCG_NODE_VISUAL_LOG"));
                        Diagnostic->SetStringField(TEXT("severity"), DiagnosticSeverityFromVerbosity(NodeLog.Verbosity));
                        Diagnostic->SetStringField(TEXT("message"), NodeLog.Message.ToString());
                        Diagnostic->SetStringField(TEXT("sourceKind"), TEXT("nodeVisualLog"));
                        Diagnostic->SetStringField(TEXT("nodeId"), Node->GetPathName());
                        Diagnostic->SetStringField(TEXT("nodeTitle"), Node->NodeTitle.IsNone() ? Node->GetName() : Node->NodeTitle.ToString());
                        Diagnostic->SetStringField(
                            TEXT("nodeClassPath"),
                            Node->GetSettings() && Node->GetSettings()->GetClass()
                                ? Node->GetSettings()->GetClass()->GetPathName()
                                : TEXT(""));
                        if (NodeLogSources.IsValidIndex(LogIndex) && NodeLogSources[LogIndex] != nullptr)
                        {
                            const FString ExecutionSource = NodeLogSources[LogIndex]->GetExecutionState().GetDebugName();
                            if (!ExecutionSource.IsEmpty())
                            {
                                Diagnostic->SetStringField(TEXT("executionSource"), ExecutionSource);
                            }
                        }
                        if (!AssetPath.IsEmpty())
                        {
                            Diagnostic->SetStringField(TEXT("assetPath"), AssetPath);
                        }
                        if (!GraphName.IsEmpty())
                        {
                            Diagnostic->SetStringField(TEXT("graphName"), GraphName);
                        }

                        ++PcgVisualLogDiagnosticCount;
                        AppendUniqueDiagnostic(CompileDiagnostics, SeenCompileDiagnosticKeys, Diagnostic);
                        AppendUniqueDiagnostic(Diagnostics, SeenDiagnosticKeys, Diagnostic);
                    }
                }
            }
        }
    }

    if (bMutateError)
    {
        TSharedPtr<FJsonObject> CompileFailureDiagnostic = MakeShared<FJsonObject>();
        CompileFailureDiagnostic->SetStringField(
            TEXT("code"),
            GraphType.Equals(TEXT("pcg"), ESearchCase::IgnoreCase)
                ? (MutateCode.IsEmpty() ? TEXT("PCG_COMPILE_FAILED") : MutateCode)
                : (MutateCode.IsEmpty() ? TEXT("COMPILE_FAILED") : MutateCode));
        CompileFailureDiagnostic->SetStringField(TEXT("severity"), TEXT("error"));
        CompileFailureDiagnostic->SetStringField(
            TEXT("message"),
            MutateMessage.IsEmpty()
                ? (GraphType.Equals(TEXT("pcg"), ESearchCase::IgnoreCase)
                    ? TEXT("PCG graph compile failed.")
                    : TEXT("Graph compile failed."))
                : MutateMessage);
        CompileFailureDiagnostic->SetStringField(TEXT("sourceKind"), TEXT("compile"));
        if (!AssetPath.IsEmpty())
        {
            CompileFailureDiagnostic->SetStringField(TEXT("assetPath"), AssetPath);
        }
        if (!GraphName.IsEmpty())
        {
            CompileFailureDiagnostic->SetStringField(TEXT("graphName"), GraphName);
        }
        AppendUniqueDiagnostic(CompileDiagnostics, SeenCompileDiagnosticKeys, CompileFailureDiagnostic);
        AppendUniqueDiagnostic(Diagnostics, SeenDiagnosticKeys, CompileFailureDiagnostic);
    }

    const TArray<TSharedPtr<FJsonValue>> RecentDiagEvents = ReadRecentVerifyDiagEvents(DiagSeqBeforeVerify, AssetPath);
    for (const TSharedPtr<FJsonValue>& EventValue : RecentDiagEvents)
    {
        const TSharedPtr<FJsonObject>* Event = nullptr;
        if (!EventValue.IsValid() || !EventValue->TryGetObject(Event) || Event == nullptr || !(*Event).IsValid())
        {
            continue;
        }

        const TSharedPtr<FJsonObject> Diagnostic = MakeRecentDiagEventDiagnostic(*Event);
        AppendUniqueDiagnostic(CompileDiagnostics, SeenCompileDiagnosticKeys, Diagnostic);
        AppendUniqueDiagnostic(Diagnostics, SeenDiagnosticKeys, Diagnostic);
    }

    Result->SetArrayField(TEXT("diagnostics"), Diagnostics);

    Result->SetStringField(TEXT("status"), bCompileSucceeded ? DetermineVerifyStatusFromDiagnostics(Diagnostics) : TEXT("error"));
    if (bCompileSucceeded)
    {
        Result->SetStringField(
            TEXT("summary"),
            Diagnostics.Num() > 0
                ? TEXT("Graph verification completed with compile-backed confirmation and surfaced diagnostics.")
                : TEXT("Graph verification succeeded with compile-backed confirmation."));
    }
    else
    {
        if (PcgVisualLogDiagnosticCount > 0)
        {
            Result->SetStringField(TEXT("summary"), TEXT("Graph verification failed during compile-backed confirmation and surfaced node-level PCG editor diagnostics."));
        }
        else if (RecentDiagEvents.Num() > 0)
        {
            Result->SetStringField(TEXT("summary"), TEXT("Graph verification failed during compile-backed confirmation and captured recent Unreal diagnostic events."));
        }
        else if (Diagnostics.Num() > 0)
        {
            Result->SetStringField(TEXT("summary"), TEXT("Graph verification failed during compile-backed confirmation but did surface follow-up diagnostics."));
        }
        else
        {
            Result->SetStringField(TEXT("summary"), TEXT("Graph verification failed during compile-backed confirmation and Unreal did not expose deeper diagnostics."));
        }
    }

    TSharedPtr<FJsonObject> CompileReport = MakeShared<FJsonObject>();
    CompileReport->SetBoolField(TEXT("compiled"), bCompileSucceeded);
    CompileReport->SetBoolField(TEXT("compilationChanged"), bCompilationChanged);
    bool bApplied = false;
    bool bPartialApplied = false;
    MutateResult->TryGetBoolField(TEXT("applied"), bApplied);
    MutateResult->TryGetBoolField(TEXT("partialApplied"), bPartialApplied);
    CompileReport->SetBoolField(TEXT("applied"), bApplied);
    CompileReport->SetBoolField(TEXT("partialApplied"), bPartialApplied);
    CompileReport->SetArrayField(TEXT("opResults"), OpResults);
    CompileReport->SetArrayField(TEXT("diagnostics"), CompileDiagnostics);
    CompileReport->SetArrayField(TEXT("recentEvents"), RecentDiagEvents);
    CompileReport->SetNumberField(TEXT("pcgVisualLogDiagnosticCount"), PcgVisualLogDiagnosticCount);
    if (!MutateCode.IsEmpty())
    {
        CompileReport->SetStringField(TEXT("code"), MutateCode);
    }
    if (!MutateMessage.IsEmpty())
    {
        CompileReport->SetStringField(TEXT("message"), MutateMessage);
    }
    if (FirstOpResult != nullptr && (*FirstOpResult).IsValid())
    {
        FString FirstOpErrorCode;
        FString FirstOpErrorMessage;
        if ((*FirstOpResult)->TryGetStringField(TEXT("errorCode"), FirstOpErrorCode) && !FirstOpErrorCode.IsEmpty())
        {
            CompileReport->SetStringField(TEXT("opErrorCode"), FirstOpErrorCode);
        }
        if ((*FirstOpResult)->TryGetStringField(TEXT("errorMessage"), FirstOpErrorMessage) && !FirstOpErrorMessage.IsEmpty())
        {
            CompileReport->SetStringField(TEXT("opErrorMessage"), FirstOpErrorMessage);
        }
    }
    Result->SetObjectField(TEXT("compileReport"), CompileReport);

    if (bMutateError)
    {
        const FString FallbackCompileMessage = GraphType.Equals(TEXT("pcg"), ESearchCase::IgnoreCase)
            ? TEXT("PCG graph compile failed.")
            : TEXT("Graph compile failed.");
        Result->SetStringField(
            TEXT("code"),
            GraphType.Equals(TEXT("pcg"), ESearchCase::IgnoreCase)
                ? (MutateCode.IsEmpty() ? TEXT("PCG_COMPILE_FAILED") : MutateCode)
                : (MutateCode.IsEmpty() ? TEXT("COMPILE_FAILED") : MutateCode));
        Result->SetStringField(
            TEXT("message"),
            MutateMessage.IsEmpty() ? FallbackCompileMessage : MutateMessage);
    }

    return Result;
}

TSharedPtr<FJsonObject> FLoomleBridgeModule::BuildDiagTailToolResult(const TSharedPtr<FJsonObject>& Arguments)
{
    if (!bDiagStoreInitialized)
    {
        InitializeDiagStore();
    }

    uint64 FromSeq = 0;
    if (Arguments.IsValid() && Arguments->HasField(TEXT("fromSeq")))
    {
        if (!TryReadJsonUInt64Field(Arguments, TEXT("fromSeq"), FromSeq))
        {
            TSharedPtr<FJsonObject> Error = MakeShared<FJsonObject>();
            Error->SetBoolField(TEXT("isError"), true);
            Error->SetStringField(TEXT("code"), TEXT("INVALID_ARGUMENT"));
            Error->SetStringField(TEXT("message"), TEXT("fromSeq must be a non-negative integer."));
            return Error;
        }
    }

    int32 Limit = DefaultDiagTailLimit;
    double LimitNumber = 0.0;
    if (Arguments.IsValid() && Arguments->TryGetNumberField(TEXT("limit"), LimitNumber))
    {
        if (LimitNumber < 1.0)
        {
            TSharedPtr<FJsonObject> Error = MakeShared<FJsonObject>();
            Error->SetBoolField(TEXT("isError"), true);
            Error->SetStringField(TEXT("code"), TEXT("INVALID_ARGUMENT"));
            Error->SetStringField(TEXT("message"), TEXT("limit must be >= 1."));
            return Error;
        }
        Limit = FMath::Clamp(static_cast<int32>(LimitNumber), 1, MaxDiagTailLimit);
    }

    TSharedPtr<FJsonObject> Filters;
    const TSharedPtr<FJsonObject>* FiltersPtr = nullptr;
    if (Arguments.IsValid() && Arguments->TryGetObjectField(TEXT("filters"), FiltersPtr) && FiltersPtr && (*FiltersPtr).IsValid())
    {
        Filters = *FiltersPtr;
    }

    TArray<TSharedPtr<FJsonValue>> Items;
    uint64 NextSeq = FromSeq;
    bool bHasMore = false;
    uint64 HighWatermark = 0;

    FScopeLock Lock(&DiagStoreMutex);
    HighWatermark = NextDiagSeq > 0 ? (NextDiagSeq - 1) : 0;

    TArray<FString> Lines;
    if (FPaths::FileExists(DiagStoreFilePath))
    {
        FFileHelper::LoadFileToStringArray(Lines, *DiagStoreFilePath);
    }

    for (const FString& Line : Lines)
    {
        if (Line.TrimStartAndEnd().IsEmpty())
        {
            continue;
        }

        TSharedPtr<FJsonObject> Event;
        const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Line);
        if (!FJsonSerializer::Deserialize(Reader, Event) || !Event.IsValid())
        {
            continue;
        }

        uint64 Seq = 0;
        if (!TryReadJsonUInt64Field(Event, TEXT("seq"), Seq) || Seq <= FromSeq)
        {
            continue;
        }

        if (!DiagEventMatchesFilters(Event, Filters))
        {
            continue;
        }

        if (Items.Num() >= Limit)
        {
            bHasMore = true;
            break;
        }

        Items.Add(MakeShared<FJsonValueObject>(Event));
        NextSeq = Seq;
    }

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetBoolField(TEXT("isError"), false);
    Result->SetArrayField(TEXT("items"), Items);
    Result->SetNumberField(TEXT("nextSeq"), static_cast<double>(NextSeq));
    Result->SetBoolField(TEXT("hasMore"), bHasMore);
    Result->SetNumberField(TEXT("highWatermark"), static_cast<double>(HighWatermark));
    return Result;
}

TSharedPtr<FJsonObject> FLoomleBridgeModule::BuildGetContextToolResult(const TSharedPtr<FJsonObject>& Arguments) const
{
    (void)Arguments;

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetBoolField(TEXT("isError"), false);
    Result->SetStringField(TEXT("timestamp"), FDateTime::UtcNow().ToIso8601());

    TSharedPtr<FJsonObject> Runtime = MakeShared<FJsonObject>();
    Runtime->SetStringField(TEXT("projectName"), FApp::GetProjectName());
    Runtime->SetStringField(TEXT("projectFilePath"), FPaths::GetProjectFilePath());
    Runtime->SetStringField(TEXT("engineVersion"), FEngineVersion::Current().ToString(EVersionComponent::Patch));
    UWorld* EditorWorld = nullptr;
    if (GEditor)
    {
        EditorWorld = GEditor->GetEditorWorldContext().World();
        Runtime->SetBoolField(TEXT("isPIE"), GEditor->IsPlayingSessionInEditor());
    }
    else
    {
        Runtime->SetBoolField(TEXT("isPIE"), false);
    }
    Runtime->SetStringField(TEXT("editorWorld"), EditorWorld ? EditorWorld->GetName() : TEXT(""));
    Result->SetObjectField(TEXT("runtime"), Runtime);
    Result->SetObjectField(TEXT("activeWindow"), BuildActiveWindowJson());

    TSharedPtr<FJsonObject> Context = MakeShared<FJsonObject>();
    Context->SetStringField(TEXT("source"), TEXT("unified"));
    TSharedPtr<FJsonObject> BlueprintContext;
    if (BuildBlueprintContextSnapshot(BlueprintContext) && BlueprintContext.IsValid())
    {
        Context = BlueprintContext;
        Context->SetStringField(TEXT("source"), TEXT("unified"));
    }
    else
    {
        TSharedPtr<FJsonObject> MaterialContext;
        if (BuildMaterialContextSnapshot(MaterialContext) && MaterialContext.IsValid())
        {
            Context = MaterialContext;
            Context->SetStringField(TEXT("source"), TEXT("unified"));
        }
        else
        {
            TSharedPtr<FJsonObject> PcgContext;
            if (BuildPcgContextSnapshot(PcgContext) && PcgContext.IsValid())
            {
                Context = PcgContext;
                Context->SetStringField(TEXT("source"), TEXT("unified"));
            }
        }
    }
    Result->SetObjectField(TEXT("context"), Context);

    TSharedPtr<FJsonObject> Selection = BuildSelectionTransformToolResult();
    Result->SetObjectField(TEXT("selection"), Selection);

    return Result;
}

TSharedPtr<FJsonObject> FLoomleBridgeModule::BuildSelectionTransformToolResult() const
{
    TSharedPtr<FJsonObject> BlueprintSelection;
    if (BuildBlueprintSelectionSnapshot(BlueprintSelection) && BlueprintSelection.IsValid())
    {
        BlueprintSelection->SetStringField(TEXT("source"), TEXT("unified"));
        return BlueprintSelection;
    }

    TSharedPtr<FJsonObject> MaterialSelection;
    if (BuildMaterialSelectionSnapshot(MaterialSelection) && MaterialSelection.IsValid())
    {
        MaterialSelection->SetStringField(TEXT("source"), TEXT("unified"));
        return MaterialSelection;
    }

    TSharedPtr<FJsonObject> PcgSelection;
    if (BuildPcgSelectionSnapshot(PcgSelection) && PcgSelection.IsValid())
    {
        PcgSelection->SetStringField(TEXT("source"), TEXT("unified"));
        return PcgSelection;
    }

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetBoolField(TEXT("isError"), false);

    TArray<TSharedPtr<FJsonValue>> Items;
    TArray<TSharedPtr<FJsonValue>> ResolvedGraphRefs;
    TSet<FString> SelectionSeenGraphRefs;
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
                Item->SetStringField(TEXT("id"), Actor->GetPathName());
                Item->SetStringField(TEXT("name"), Actor->GetActorLabel());
                Item->SetStringField(TEXT("class"), Actor->GetClass() ? Actor->GetClass()->GetPathName() : TEXT(""));
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

                TArray<TSharedPtr<FJsonValue>> ItemResolvedGraphRefs;
                TSet<FString> ItemSeenGraphRefs;
                AppendActorResolvedGraphRefs(Actor, ItemResolvedGraphRefs, ItemSeenGraphRefs);
                SetResolvedGraphRefsFieldIfAny(Item, ItemResolvedGraphRefs);
                CopyResolvedGraphRefEntries(ItemResolvedGraphRefs, ResolvedGraphRefs, SelectionSeenGraphRefs);

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
    SetResolvedGraphRefsFieldIfAny(Result, ResolvedGraphRefs);

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

TSharedPtr<FJsonObject> FLoomleBridgeModule::BuildEditorOpenToolResult(const TSharedPtr<FJsonObject>& Arguments)
{
    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

    FString AssetPath;
    if (!Arguments.IsValid() || !Arguments->TryGetStringField(TEXT("assetPath"), AssetPath) || AssetPath.TrimStartAndEnd().IsEmpty())
    {
        Result->SetBoolField(TEXT("isError"), true);
        Result->SetStringField(TEXT("code"), TEXT("INVALID_ARGUMENT"));
        Result->SetStringField(TEXT("message"), TEXT("arguments.assetPath is required."));
        return Result;
    }

    if (!GEditor)
    {
        Result->SetBoolField(TEXT("isError"), true);
        Result->SetStringField(TEXT("code"), TEXT("EDITOR_NOT_AVAILABLE"));
        Result->SetStringField(TEXT("message"), TEXT("Editor is not available."));
        return Result;
    }

    UAssetEditorSubsystem* AssetEditorSubsystem = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>();
    if (AssetEditorSubsystem == nullptr)
    {
        Result->SetBoolField(TEXT("isError"), true);
        Result->SetStringField(TEXT("code"), TEXT("EDITOR_NOT_AVAILABLE"));
        Result->SetStringField(TEXT("message"), TEXT("AssetEditorSubsystem is not available."));
        return Result;
    }

    UObject* Asset = LoadObjectByAssetPath(AssetPath);
    if (Asset == nullptr)
    {
        Result->SetBoolField(TEXT("isError"), true);
        Result->SetStringField(TEXT("code"), TEXT("ASSET_NOT_FOUND"));
        Result->SetStringField(TEXT("message"), TEXT("Asset not found."));
        Result->SetStringField(TEXT("assetPath"), AssetPath);
        return Result;
    }

    FText OpenError;
    if (!AssetEditorSubsystem->CanOpenEditorForAsset(Asset, EAssetTypeActivationOpenedMethod::Edit, &OpenError))
    {
        Result->SetBoolField(TEXT("isError"), true);
        Result->SetStringField(TEXT("code"), TEXT("INVALID_ARGUMENT"));
        Result->SetStringField(TEXT("message"), OpenError.IsEmpty() ? TEXT("Asset cannot be opened in an editor.") : OpenError.ToString());
        Result->SetStringField(TEXT("assetPath"), AssetPath);
        return Result;
    }

    const bool bAlreadyOpen = AssetEditorSubsystem->FindEditorForAsset(Asset, false) != nullptr;
    const bool bOpened = AssetEditorSubsystem->OpenEditorForAsset(Asset);
    const bool bWindowFocused = AssetEditorSubsystem->FindEditorForAsset(Asset, true) != nullptr;
    const TSharedPtr<FJsonObject> ActiveWindow = BuildActiveWindowJson();

    if (!bOpened && !bAlreadyOpen)
    {
        Result->SetBoolField(TEXT("isError"), true);
        Result->SetStringField(TEXT("code"), TEXT("INTERNAL_ERROR"));
        Result->SetStringField(TEXT("message"), TEXT("Failed to open asset editor."));
        Result->SetStringField(TEXT("assetPath"), AssetPath);
        return Result;
    }

    Result->SetBoolField(TEXT("isError"), false);
    Result->SetBoolField(TEXT("ok"), true);
    Result->SetStringField(TEXT("assetPath"), AssetPath);
    Result->SetStringField(TEXT("assetName"), Asset->GetName());
    Result->SetStringField(TEXT("assetClassPath"), Asset->GetClass() ? Asset->GetClass()->GetPathName() : TEXT(""));
    Result->SetBoolField(TEXT("alreadyOpen"), bAlreadyOpen);
    Result->SetBoolField(TEXT("windowFocused"), bWindowFocused);
    Result->SetStringField(TEXT("windowTitle"), ActiveWindow.IsValid() ? ActiveWindow->GetStringField(TEXT("title")) : TEXT(""));
    return Result;
}

TSharedPtr<FJsonObject> FLoomleBridgeModule::BuildEditorFocusToolResult(const TSharedPtr<FJsonObject>& Arguments)
{
    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

    FString AssetPath;
    FString Panel;
    if (!Arguments.IsValid()
        || !Arguments->TryGetStringField(TEXT("assetPath"), AssetPath)
        || AssetPath.TrimStartAndEnd().IsEmpty()
        || !Arguments->TryGetStringField(TEXT("panel"), Panel)
        || Panel.TrimStartAndEnd().IsEmpty())
    {
        Result->SetBoolField(TEXT("isError"), true);
        Result->SetStringField(TEXT("code"), TEXT("INVALID_ARGUMENT"));
        Result->SetStringField(TEXT("message"), TEXT("arguments.assetPath and arguments.panel are required."));
        return Result;
    }

    if (!GEditor)
    {
        Result->SetBoolField(TEXT("isError"), true);
        Result->SetStringField(TEXT("code"), TEXT("EDITOR_NOT_AVAILABLE"));
        Result->SetStringField(TEXT("message"), TEXT("Editor is not available."));
        return Result;
    }

    UAssetEditorSubsystem* AssetEditorSubsystem = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>();
    if (AssetEditorSubsystem == nullptr)
    {
        Result->SetBoolField(TEXT("isError"), true);
        Result->SetStringField(TEXT("code"), TEXT("EDITOR_NOT_AVAILABLE"));
        Result->SetStringField(TEXT("message"), TEXT("AssetEditorSubsystem is not available."));
        return Result;
    }

    UObject* Asset = LoadObjectByAssetPath(AssetPath);
    if (Asset == nullptr)
    {
        Result->SetBoolField(TEXT("isError"), true);
        Result->SetStringField(TEXT("code"), TEXT("ASSET_NOT_FOUND"));
        Result->SetStringField(TEXT("message"), TEXT("Asset not found."));
        Result->SetStringField(TEXT("assetPath"), AssetPath);
        return Result;
    }

    FText OpenError;
    if (!AssetEditorSubsystem->CanOpenEditorForAsset(Asset, EAssetTypeActivationOpenedMethod::Edit, &OpenError))
    {
        Result->SetBoolField(TEXT("isError"), true);
        Result->SetStringField(TEXT("code"), TEXT("INVALID_ARGUMENT"));
        Result->SetStringField(TEXT("message"), OpenError.IsEmpty() ? TEXT("Asset cannot be opened in an editor.") : OpenError.ToString());
        Result->SetStringField(TEXT("assetPath"), AssetPath);
        return Result;
    }

    const bool bAlreadyOpen = AssetEditorSubsystem->FindEditorForAsset(Asset, false) != nullptr;
    if (!bAlreadyOpen && !AssetEditorSubsystem->OpenEditorForAsset(Asset))
    {
        Result->SetBoolField(TEXT("isError"), true);
        Result->SetStringField(TEXT("code"), TEXT("INTERNAL_ERROR"));
        Result->SetStringField(TEXT("message"), TEXT("Failed to open asset editor."));
        Result->SetStringField(TEXT("assetPath"), AssetPath);
        return Result;
    }

    IAssetEditorInstance* EditorInstance = AssetEditorSubsystem->FindEditorForAsset(Asset, true);
    if (EditorInstance == nullptr)
    {
        Result->SetBoolField(TEXT("isError"), true);
        Result->SetStringField(TEXT("code"), TEXT("EDITOR_NOT_AVAILABLE"));
        Result->SetStringField(TEXT("message"), TEXT("Asset editor is not available."));
        Result->SetStringField(TEXT("assetPath"), AssetPath);
        return Result;
    }

    Panel = NormalizeEditorPanel(Panel);
    FString EditorType;
    bool bPanelFocused = false;

    if (UBlueprint* Blueprint = Cast<UBlueprint>(Asset))
    {
        EditorType = TEXT("blueprint");
        FBlueprintEditor* BlueprintEditor = static_cast<FBlueprintEditor*>(EditorInstance);

        if (Panel.Equals(TEXT("graph")))
        {
            if (UEdGraph* EventGraph = FBlueprintEditorUtils::FindEventGraph(Blueprint))
            {
                bPanelFocused = BlueprintEditor->OpenGraphAndBringToFront(EventGraph, true).IsValid();
            }
        }
        else if (Panel.Equals(TEXT("constructionscript")))
        {
            if (UEdGraph* ConstructionGraph = FBlueprintEditorUtils::FindUserConstructionScript(Blueprint))
            {
                bPanelFocused = BlueprintEditor->OpenGraphAndBringToFront(ConstructionGraph, true).IsValid();
            }
        }
        else if (Panel.Equals(TEXT("viewport")))
        {
            EditorInstance->InvokeTab(FTabId(FBlueprintEditorTabs::SCSViewportID));
            bPanelFocused = true;
        }
        else if (Panel.Equals(TEXT("details")))
        {
            EditorInstance->InvokeTab(FTabId(FBlueprintEditorTabs::DetailsID));
            bPanelFocused = true;
        }
        else if (Panel.Equals(TEXT("myblueprint")))
        {
            EditorInstance->InvokeTab(FTabId(FBlueprintEditorTabs::MyBlueprintID));
            bPanelFocused = true;
        }
    }
    else if (IsMaterialLikeAsset(Asset))
    {
        EditorType = TEXT("material");

        if (Panel.Equals(TEXT("graph")))
        {
            EditorInstance->InvokeTab(FTabId(MaterialEditorGraphTabId));
            bPanelFocused = true;
        }
        else if (Panel.Equals(TEXT("details")))
        {
            EditorInstance->InvokeTab(FTabId(MaterialEditorPropertiesTabId));
            bPanelFocused = true;
        }
        else if (Panel.Equals(TEXT("palette")))
        {
            EditorInstance->InvokeTab(FTabId(MaterialEditorPaletteTabId));
            bPanelFocused = true;
        }
        else if (Panel.Equals(TEXT("find")))
        {
            EditorInstance->InvokeTab(FTabId(MaterialEditorFindTabId));
            bPanelFocused = true;
        }
        else if (Panel.Equals(TEXT("preview")))
        {
            EditorInstance->InvokeTab(FTabId(MaterialEditorPreviewTabId));
            bPanelFocused = true;
        }
    }
    else if (ResolvePcgGraphFromAsset(Asset) != nullptr)
    {
        EditorType = TEXT("pcg");
        FPCGEditor* PCGEditor = static_cast<FPCGEditor*>(EditorInstance);

        if (Panel.Equals(TEXT("graph")))
        {
            PCGEditor->BringFocusToPanel(EPCGEditorPanel::GraphEditor);
            bPanelFocused = true;
        }
        else if (Panel.Equals(TEXT("details")))
        {
            PCGEditor->BringFocusToPanel(EPCGEditorPanel::PropertyDetails1);
            bPanelFocused = true;
        }
        else if (Panel.Equals(TEXT("palette")))
        {
            PCGEditor->BringFocusToPanel(EPCGEditorPanel::NodePalette);
            bPanelFocused = true;
        }
        else if (Panel.Equals(TEXT("find")))
        {
            PCGEditor->BringFocusToPanel(EPCGEditorPanel::Find);
            bPanelFocused = true;
        }
        else if (Panel.Equals(TEXT("log")))
        {
            PCGEditor->BringFocusToPanel(EPCGEditorPanel::Log);
            bPanelFocused = true;
        }
        else if (Panel.Equals(TEXT("profiling")))
        {
            PCGEditor->BringFocusToPanel(EPCGEditorPanel::Profiling);
            bPanelFocused = true;
        }
        else if (Panel.Equals(TEXT("viewport")))
        {
            PCGEditor->BringFocusToPanel(EPCGEditorPanel::Viewport1);
            bPanelFocused = true;
        }
    }

    if (EditorType.IsEmpty())
    {
        Result->SetBoolField(TEXT("isError"), true);
        Result->SetStringField(TEXT("code"), TEXT("PANEL_UNSUPPORTED"));
        Result->SetStringField(TEXT("message"), TEXT("This asset editor is not supported by editor.focus."));
        Result->SetStringField(TEXT("assetPath"), AssetPath);
        Result->SetStringField(TEXT("panel"), Panel);
        return Result;
    }

    if (!bPanelFocused)
    {
        Result->SetBoolField(TEXT("isError"), true);
        Result->SetStringField(TEXT("code"), TEXT("PANEL_UNSUPPORTED"));
        Result->SetStringField(TEXT("message"), TEXT("The requested panel is not supported for this editor."));
        Result->SetStringField(TEXT("assetPath"), AssetPath);
        Result->SetStringField(TEXT("editorType"), EditorType);
        Result->SetStringField(TEXT("panel"), Panel);
        return Result;
    }

    EditorInstance->FocusWindow(Asset);
    const TSharedPtr<FJsonObject> ActiveWindow = BuildActiveWindowJson();

    Result->SetBoolField(TEXT("isError"), false);
    Result->SetBoolField(TEXT("ok"), true);
    Result->SetStringField(TEXT("assetPath"), AssetPath);
    Result->SetStringField(TEXT("assetName"), Asset->GetName());
    Result->SetStringField(TEXT("assetClassPath"), Asset->GetClass() ? Asset->GetClass()->GetPathName() : TEXT(""));
    Result->SetStringField(TEXT("editorType"), EditorType);
    Result->SetStringField(TEXT("panel"), Panel);
    Result->SetBoolField(TEXT("alreadyOpen"), bAlreadyOpen);
    Result->SetBoolField(TEXT("panelFocused"), true);
    Result->SetStringField(TEXT("windowTitle"), ActiveWindow.IsValid() ? ActiveWindow->GetStringField(TEXT("title")) : TEXT(""));
    return Result;
}

TSharedPtr<FJsonObject> FLoomleBridgeModule::BuildEditorScreenshotToolResult(const TSharedPtr<FJsonObject>& Arguments)
{
    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

    FString Target = TEXT("activeWindow");
    FString RequestedPath;
    if (Arguments.IsValid())
    {
        Arguments->TryGetStringField(TEXT("target"), Target);
        Arguments->TryGetStringField(TEXT("path"), RequestedPath);
    }

    Target = Target.TrimStartAndEnd();
    if (Target.IsEmpty())
    {
        Target = TEXT("activeWindow");
    }

    if (!Target.Equals(TEXT("activeWindow"), ESearchCase::IgnoreCase))
    {
        Result->SetBoolField(TEXT("isError"), true);
        Result->SetStringField(TEXT("code"), TEXT("INVALID_ARGUMENT"));
        Result->SetStringField(TEXT("message"), TEXT("arguments.target must be 'activeWindow'."));
        return Result;
    }

    TSharedPtr<SWindow> ActiveWindow = ResolveCaptureTopLevelWindow();

    const FString OutputPath = ResolveScreenshotOutputPath(RequestedPath);
    const FString OutputDir = FPaths::GetPath(OutputPath);
    if (!OutputDir.IsEmpty())
    {
        IFileManager::Get().MakeDirectory(*OutputDir, true);
    }

    TArray<FColor> ColorData;
    FIntVector ImageSize(0, 0, 0);

#if PLATFORM_WINDOWS
    if (UObject* ActiveMaterialAsset = FindEditedMaterialAsset())
    {
        RefreshMaterialEditorVisuals(ActiveMaterialAsset);
    }
    if (UObject* ActivePcgAsset = FindEditedPcgAsset())
    {
        RefreshPcgEditorVisuals(ActivePcgAsset);
    }
    if (ActiveWindow.IsValid())
    {
        RefreshSlateWindowForCapture(ActiveWindow.ToSharedRef());
    }

    const HWND CaptureWindowHandle = ResolveCaptureWindowHandle(ActiveWindow);
    if (CaptureWindowHandle == nullptr)
    {
        Result->SetBoolField(TEXT("isError"), true);
        Result->SetStringField(TEXT("code"), TEXT("WINDOW_NOT_FOUND"));
        Result->SetStringField(TEXT("message"), TEXT("No native editor window was found for screenshot capture."));
        return Result;
    }

    ::RedrawWindow(CaptureWindowHandle, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW | RDW_ALLCHILDREN);
    ::UpdateWindow(CaptureWindowHandle);

    FString CaptureError;
    if (!CaptureNativeWindowToColorData(CaptureWindowHandle, ColorData, ImageSize, CaptureError))
    {
        Result->SetBoolField(TEXT("isError"), true);
        Result->SetStringField(TEXT("code"), TEXT("CAPTURE_FAILED"));
        Result->SetStringField(TEXT("message"), CaptureError.IsEmpty()
            ? TEXT("Failed to capture the active editor window on Windows.")
            : CaptureError);
        return Result;
    }
#else
    if (!ActiveWindow.IsValid())
    {
        Result->SetBoolField(TEXT("isError"), true);
        Result->SetStringField(TEXT("code"), TEXT("WINDOW_NOT_FOUND"));
        Result->SetStringField(TEXT("message"), TEXT("No active top-level editor window was found."));
        return Result;
    }

    if (UObject* ActiveMaterialAsset = FindEditedMaterialAsset())
    {
        RefreshMaterialEditorVisuals(ActiveMaterialAsset);
    }
    if (UObject* ActivePcgAsset = FindEditedPcgAsset())
    {
        RefreshPcgEditorVisuals(ActivePcgAsset);
    }

    RefreshSlateWindowForCapture(ActiveWindow.ToSharedRef());
    if (!FSlateApplication::Get().TakeScreenshot(ActiveWindow.ToSharedRef(), ColorData, ImageSize))
    {
        Result->SetBoolField(TEXT("isError"), true);
        Result->SetStringField(TEXT("code"), TEXT("CAPTURE_FAILED"));
        Result->SetStringField(TEXT("message"), TEXT("Failed to capture the active editor window."));
        return Result;
    }
#endif

    if (ImageSize.X <= 0 || ImageSize.Y <= 0 || ColorData.IsEmpty())
    {
        Result->SetBoolField(TEXT("isError"), true);
        Result->SetStringField(TEXT("code"), TEXT("CAPTURE_FAILED"));
        Result->SetStringField(TEXT("message"), TEXT("Screenshot capture returned no pixels."));
        return Result;
    }

    TArray64<uint8> CompressedPng;
    FImageUtils::PNGCompressImageArray(
        ImageSize.X,
        ImageSize.Y,
        TArrayView64<const FColor>(ColorData.GetData(), ColorData.Num()),
        CompressedPng);

    if (CompressedPng.IsEmpty() || !FFileHelper::SaveArrayToFile(CompressedPng, *OutputPath))
    {
        Result->SetBoolField(TEXT("isError"), true);
        Result->SetStringField(TEXT("code"), TEXT("INTERNAL_ERROR"));
        Result->SetStringField(TEXT("message"), TEXT("Failed to write screenshot PNG."));
        Result->SetStringField(TEXT("path"), OutputPath);
        return Result;
    }

    Result->SetBoolField(TEXT("isError"), false);
    Result->SetBoolField(TEXT("ok"), true);
    Result->SetStringField(TEXT("target"), TEXT("activeWindow"));
    Result->SetStringField(TEXT("path"), OutputPath);
    Result->SetStringField(TEXT("windowTitle"), ActiveWindow.IsValid() ? ActiveWindow->GetTitle().ToString() : TEXT(""));
    Result->SetNumberField(TEXT("width"), ImageSize.X);
    Result->SetNumberField(TEXT("height"), ImageSize.Y);
    return Result;
}

TSharedPtr<FJsonObject> FLoomleBridgeModule::BuildExecutePythonToolResult(const TSharedPtr<FJsonObject>& Arguments)
{
    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

    FString Code;
    if (!Arguments->TryGetStringField(TEXT("code"), Code))
    {
        Result->SetBoolField(TEXT("isError"), true);
        Result->SetStringField(TEXT("code"), TEXT("INVALID_ARGUMENT"));
        Result->SetStringField(TEXT("message"), TEXT("arguments.code is required."));
        AppendDiagEvent(TEXT("error"), TEXT("python"), TEXT("execute"), TEXT("arguments.code is required."));
        return Result;
    }

    FString Mode = TEXT("exec");
    Arguments->TryGetStringField(TEXT("mode"), Mode);
    Mode = Mode.ToLower();
    if (!Mode.Equals(TEXT("exec")) && !Mode.Equals(TEXT("eval")))
    {
        Result->SetBoolField(TEXT("isError"), true);
        Result->SetStringField(TEXT("code"), TEXT("INVALID_ARGUMENT"));
        Result->SetStringField(TEXT("message"), TEXT("arguments.mode must be 'exec' or 'eval'."));
        AppendDiagEvent(TEXT("error"), TEXT("python"), TEXT("execute"), TEXT("arguments.mode must be 'exec' or 'eval'."));
        return Result;
    }

    IPythonScriptPlugin* PythonScriptPlugin = IPythonScriptPlugin::Get();
    if (PythonScriptPlugin == nullptr)
    {
        Result->SetBoolField(TEXT("isError"), true);
        Result->SetStringField(TEXT("code"), TEXT("INTERNAL_ERROR"));
        Result->SetStringField(TEXT("message"), TEXT("PythonScriptPlugin module is not loaded."));
        AppendDiagEvent(TEXT("error"), TEXT("python"), TEXT("execute"), TEXT("PythonScriptPlugin module is not loaded."));
        return Result;
    }

    if (!PythonScriptPlugin->IsPythonInitialized())
    {
        PythonScriptPlugin->ForceEnablePythonAtRuntime();
        if (!PythonScriptPlugin->IsPythonInitialized())
        {
            Result->SetBoolField(TEXT("isError"), true);
            Result->SetStringField(TEXT("code"), TEXT("INTERNAL_ERROR"));
            Result->SetStringField(TEXT("message"), TEXT("Python runtime is not initialized."));
            AppendDiagEvent(TEXT("error"), TEXT("python"), TEXT("execute"), TEXT("Python runtime is not initialized."));
            return Result;
        }
    }

    static constexpr int32 ExecuteTimeoutSeconds = 30;

    FString WrappedCode;
    if (Mode.Equals(TEXT("exec")))
    {
        WrappedCode += TEXT("import signal, platform\n");
        WrappedCode += FString::Printf(TEXT("_LOOMLE_TIMEOUT = %d\n"), ExecuteTimeoutSeconds);
        WrappedCode += TEXT("def _loomle_timeout_handler(signum, frame):\n");
        WrappedCode += TEXT("    raise TimeoutError(f'execute exceeded {_LOOMLE_TIMEOUT}s timeout')\n");
        WrappedCode += TEXT("if platform.system() != 'Windows' and hasattr(signal, 'SIGALRM'):\n");
        WrappedCode += TEXT("    signal.signal(signal.SIGALRM, _loomle_timeout_handler)\n");
        WrappedCode += TEXT("    signal.alarm(_LOOMLE_TIMEOUT)\n");
        WrappedCode += TEXT("try:\n");
        TArray<FString> CodeLines;
        Code.ParseIntoArrayLines(CodeLines, false);
        for (const FString& Line : CodeLines)
        {
            WrappedCode += TEXT("    ") + Line + TEXT("\n");
        }
        WrappedCode += TEXT("finally:\n");
        WrappedCode += TEXT("    if platform.system() != 'Windows' and hasattr(signal, 'SIGALRM'):\n");
        WrappedCode += TEXT("        signal.alarm(0)\n");
    }

    FPythonCommandEx PythonCommand;
    PythonCommand.ExecutionMode = Mode.Equals(TEXT("eval"))
        ? EPythonCommandExecutionMode::EvaluateStatement
        : EPythonCommandExecutionMode::ExecuteFile;
    PythonCommand.Command = Mode.Equals(TEXT("exec")) ? WrappedCode : Code;

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

    if (!bSuccess)
    {
        TSharedPtr<FJsonObject> Context = MakeShared<FJsonObject>();
        Context->SetStringField(TEXT("mode"), Mode);
        Context->SetNumberField(TEXT("logCount"), static_cast<double>(Logs.Num()));
        AppendDiagEvent(TEXT("error"), TEXT("python"), TEXT("execute"), TEXT("Python execute failed."), Context);
    }

    return Result;
}
