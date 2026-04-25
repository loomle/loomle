// Runtime tool handlers (context / selection / execute / diagnostics / logs).
namespace
{
constexpr int32 DefaultTailLimit = 200;
constexpr int32 MaxTailLimit = 1000;

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

int32 LogVerbosityRank(const FString& Verbosity)
{
    const FString Normalized = Verbosity.ToLower();
    if (Normalized.Equals(TEXT("fatal")))
    {
        return 5;
    }
    if (Normalized.Equals(TEXT("error")))
    {
        return 4;
    }
    if (Normalized.Equals(TEXT("warning")) || Normalized.Equals(TEXT("warn")))
    {
        return 3;
    }
    if (Normalized.Equals(TEXT("display")))
    {
        return 2;
    }
    if (Normalized.Equals(TEXT("log")) || Normalized.Equals(TEXT("info")))
    {
        return 1;
    }
    if (Normalized.Equals(TEXT("verbose")))
    {
        return 0;
    }
    if (Normalized.Equals(TEXT("veryverbose")))
    {
        return -1;
    }
    return 0;
}

bool LogEventMatchesFilters(const TSharedPtr<FJsonObject>& Event, const TSharedPtr<FJsonObject>& Filters)
{
    if (!Event.IsValid() || !Filters.IsValid())
    {
        return true;
    }

    FString MinVerbosity;
    if (Filters->TryGetStringField(TEXT("minVerbosity"), MinVerbosity) && !MinVerbosity.IsEmpty())
    {
        FString EventVerbosity;
        if (!Event->TryGetStringField(TEXT("verbosity"), EventVerbosity)
            || LogVerbosityRank(EventVerbosity) < LogVerbosityRank(MinVerbosity))
        {
            return false;
        }
    }

    FString Category;
    if (Filters->TryGetStringField(TEXT("category"), Category) && !Category.IsEmpty())
    {
        FString EventCategory;
        if (!Event->TryGetStringField(TEXT("category"), EventCategory)
            || !EventCategory.Equals(Category, ESearchCase::IgnoreCase))
        {
            return false;
        }
    }

    const TArray<TSharedPtr<FJsonValue>>* Categories = nullptr;
    if (Filters->TryGetArrayField(TEXT("categories"), Categories) && Categories != nullptr && Categories->Num() > 0)
    {
        FString EventCategory;
        if (!Event->TryGetStringField(TEXT("category"), EventCategory))
        {
            return false;
        }

        bool bMatchedCategory = false;
        for (const TSharedPtr<FJsonValue>& CategoryValue : *Categories)
        {
            FString Candidate;
            if (CategoryValue.IsValid() && CategoryValue->TryGetString(Candidate)
                && EventCategory.Equals(Candidate, ESearchCase::IgnoreCase))
            {
                bMatchedCategory = true;
                break;
            }
        }
        if (!bMatchedCategory)
        {
            return false;
        }
    }

    FString Source;
    if (Filters->TryGetStringField(TEXT("source"), Source) && !Source.IsEmpty())
    {
        FString EventSource;
        if (!Event->TryGetStringField(TEXT("source"), EventSource)
            || !EventSource.Equals(Source, ESearchCase::IgnoreCase))
        {
            return false;
        }
    }

    FString Contains;
    if (Filters->TryGetStringField(TEXT("contains"), Contains) && !Contains.IsEmpty())
    {
        FString Message;
        if (!Event->TryGetStringField(TEXT("message"), Message)
            || !Message.Contains(Contains, ESearchCase::IgnoreCase))
        {
            return false;
        }
    }

    return true;
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

FString LogLevelFromVerbosity(const ELogVerbosity::Type Verbosity)
{
    const ELogVerbosity::Type MaskedVerbosity = static_cast<ELogVerbosity::Type>(Verbosity & ELogVerbosity::VerbosityMask);
    if (MaskedVerbosity == ELogVerbosity::Fatal)
    {
        return TEXT("fatal");
    }
    if (MaskedVerbosity == ELogVerbosity::Error)
    {
        return TEXT("error");
    }
    if (MaskedVerbosity == ELogVerbosity::Warning)
    {
        return TEXT("warning");
    }
    if (MaskedVerbosity == ELogVerbosity::Display)
    {
        return TEXT("display");
    }
    if (MaskedVerbosity == ELogVerbosity::Verbose)
    {
        return TEXT("verbose");
    }
    if (MaskedVerbosity == ELogVerbosity::VeryVerbose)
    {
        return TEXT("veryverbose");
    }
    return TEXT("log");
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

TSharedPtr<FJsonObject> MakeRecentDiagnosticEventDiagnostic(const TSharedPtr<FJsonObject>& Event)
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
    Diagnostic->SetStringField(TEXT("code"), TEXT("PCG_RECENT_DIAGNOSTIC_EVENT"));
    Diagnostic->SetStringField(TEXT("severity"), Severity.IsEmpty() ? TEXT("error") : Severity.ToLower());
    Diagnostic->SetStringField(TEXT("message"), Message);
    Diagnostic->SetStringField(TEXT("sourceKind"), TEXT("diagnosticWindow"));
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

void FLoomleBridgeModule::InitializeDiagnosticStore()
{
    FScopeLock Lock(&DiagnosticStoreMutex);
    if (bDiagnosticStoreInitialized)
    {
        return;
    }

    DiagnosticStoreDirPath = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Loomle"), TEXT("diagnostics"));
    DiagnosticStoreFilePath = FPaths::Combine(DiagnosticStoreDirPath, TEXT("diagnostics.jsonl"));
    IFileManager::Get().MakeDirectory(*DiagnosticStoreDirPath, true);

    NextDiagnosticSeq = 1;
    TArray<FString> Lines;
    if (FPaths::FileExists(DiagnosticStoreFilePath) && FFileHelper::LoadFileToStringArray(Lines, *DiagnosticStoreFilePath))
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

            NextDiagnosticSeq = ParsedSeq + 1;
            break;
        }
    }

    bDiagnosticStoreInitialized = true;
}

void FLoomleBridgeModule::InitializeLogStore()
{
    FScopeLock Lock(&LogStoreMutex);
    if (bLogStoreInitialized)
    {
        return;
    }

    LogStoreDirPath = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Loomle"), TEXT("logs"));
    LogStoreFilePath = FPaths::Combine(LogStoreDirPath, TEXT("logs.jsonl"));
    IFileManager::Get().MakeDirectory(*LogStoreDirPath, true);

    NextLogSeq = 1;
    TArray<FString> Lines;
    if (FPaths::FileExists(LogStoreFilePath) && FFileHelper::LoadFileToStringArray(Lines, *LogStoreFilePath))
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

            NextLogSeq = ParsedSeq + 1;
            break;
        }
    }

    bLogStoreInitialized = true;
}

void FLoomleBridgeModule::AppendDiagnosticEvent(
    const FString& Severity,
    const FString& Category,
    const FString& Source,
    const FString& Message,
    const TSharedPtr<FJsonObject>& Context)
{
    if (!bDiagnosticStoreInitialized)
    {
        InitializeDiagnosticStore();
    }

    FScopeLock Lock(&DiagnosticStoreMutex);
    if (DiagnosticStoreFilePath.IsEmpty())
    {
        return;
    }

    const uint64 Seq = NextDiagnosticSeq;
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
            *DiagnosticStoreFilePath,
            FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM,
            &IFileManager::Get(),
            FILEWRITE_Append))
    {
        ++NextDiagnosticSeq;
        return;
    }

    // Avoid recursive logging loops when persistence itself fails.
}

void FLoomleBridgeModule::AppendLogEvent(
    const FString& Verbosity,
    const FString& Category,
    const FString& Source,
    const FString& Message,
    const TSharedPtr<FJsonObject>& Context)
{
    if (!bLogStoreInitialized)
    {
        InitializeLogStore();
    }

    FScopeLock Lock(&LogStoreMutex);
    if (LogStoreFilePath.IsEmpty())
    {
        return;
    }

    const uint64 Seq = NextLogSeq;
    TSharedPtr<FJsonObject> Event = MakeShared<FJsonObject>();
    Event->SetNumberField(TEXT("seq"), static_cast<double>(Seq));
    Event->SetStringField(TEXT("ts"), FDateTime::UtcNow().ToIso8601());
    Event->SetStringField(TEXT("verbosity"), Verbosity.ToLower());
    Event->SetStringField(TEXT("category"), Category);
    Event->SetStringField(TEXT("source"), Source);
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
            *LogStoreFilePath,
            FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM,
            &IFileManager::Get(),
            FILEWRITE_Append))
    {
        ++NextLogSeq;
    }
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

    const ELogVerbosity::Type VerbosityMask = static_cast<ELogVerbosity::Type>(Verbosity & ELogVerbosity::VerbosityMask);
    const FString CapturedLogLevel = LogLevelFromVerbosity(VerbosityMask);
    if (VerbosityMask == ELogVerbosity::Fatal
        || VerbosityMask == ELogVerbosity::Error
        || VerbosityMask == ELogVerbosity::Warning)
    {
        AppendJobLogLine(TEXT(""), CapturedLogLevel, Message);
    }

    TSharedPtr<FJsonObject> Context = MakeShared<FJsonObject>();
    AppendLogEvent(CapturedLogLevel, CategoryName, TEXT("unreal_output_log"), Message, Context);
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
        AppendDiagnosticEvent(
            TEXT("error"),
            TEXT("compile"),
            TEXT("blueprint"),
            FString::Printf(TEXT("Blueprint compile failed: %s"), *Blueprint->GetName()),
            Context);
    }

    BlueprintCompileErrorAssets = MoveTemp(CurrentErrorAssets);
}

TSharedPtr<FJsonObject> FLoomleBridgeModule::BuildDiagnosticTailToolResult(const TSharedPtr<FJsonObject>& Arguments)
{
    if (!bDiagnosticStoreInitialized)
    {
        InitializeDiagnosticStore();
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

    int32 Limit = DefaultTailLimit;
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
        Limit = FMath::Clamp(static_cast<int32>(LimitNumber), 1, MaxTailLimit);
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

    FScopeLock Lock(&DiagnosticStoreMutex);
    HighWatermark = NextDiagnosticSeq > 0 ? (NextDiagnosticSeq - 1) : 0;

    TArray<FString> Lines;
    if (FPaths::FileExists(DiagnosticStoreFilePath))
    {
        FFileHelper::LoadFileToStringArray(Lines, *DiagnosticStoreFilePath);
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
    const uint64 NextFromSeq = bHasMore ? NextSeq : HighWatermark;
    Result->SetBoolField(TEXT("isError"), false);
    Result->SetArrayField(TEXT("items"), Items);
    Result->SetNumberField(TEXT("fromSeq"), static_cast<double>(FromSeq));
    Result->SetNumberField(TEXT("nextSeq"), static_cast<double>(NextSeq));
    Result->SetNumberField(TEXT("nextFromSeq"), static_cast<double>(NextFromSeq));
    Result->SetBoolField(TEXT("hasMore"), bHasMore);
    Result->SetNumberField(TEXT("latestSeq"), static_cast<double>(HighWatermark));
    Result->SetNumberField(TEXT("highWatermark"), static_cast<double>(HighWatermark));
    return Result;
}

TSharedPtr<FJsonObject> FLoomleBridgeModule::BuildLogTailToolResult(const TSharedPtr<FJsonObject>& Arguments)
{
    if (!bLogStoreInitialized)
    {
        InitializeLogStore();
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

    int32 Limit = DefaultTailLimit;
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
        Limit = FMath::Clamp(static_cast<int32>(LimitNumber), 1, MaxTailLimit);
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

    FScopeLock Lock(&LogStoreMutex);
    HighWatermark = NextLogSeq > 0 ? (NextLogSeq - 1) : 0;

    TArray<FString> Lines;
    if (FPaths::FileExists(LogStoreFilePath))
    {
        FFileHelper::LoadFileToStringArray(Lines, *LogStoreFilePath);
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

        if (!LogEventMatchesFilters(Event, Filters))
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
    const uint64 NextFromSeq = bHasMore ? NextSeq : HighWatermark;
    Result->SetBoolField(TEXT("isError"), false);
    Result->SetArrayField(TEXT("items"), Items);
    Result->SetNumberField(TEXT("fromSeq"), static_cast<double>(FromSeq));
    Result->SetNumberField(TEXT("nextSeq"), static_cast<double>(NextSeq));
    Result->SetNumberField(TEXT("nextFromSeq"), static_cast<double>(NextFromSeq));
    Result->SetBoolField(TEXT("hasMore"), bHasMore);
    Result->SetNumberField(TEXT("latestSeq"), static_cast<double>(HighWatermark));
    Result->SetNumberField(TEXT("highWatermark"), static_cast<double>(HighWatermark));
    return Result;
}

namespace
{
FString LoomleWorldTypeToString(const EWorldType::Type WorldType)
{
    switch (WorldType)
    {
    case EWorldType::None:
        return TEXT("none");
    case EWorldType::Game:
        return TEXT("game");
    case EWorldType::Editor:
        return TEXT("editor");
    case EWorldType::PIE:
        return TEXT("pie");
    case EWorldType::EditorPreview:
        return TEXT("editor_preview");
    case EWorldType::GamePreview:
        return TEXT("game_preview");
    case EWorldType::GameRPC:
        return TEXT("game_rpc");
    case EWorldType::Inactive:
        return TEXT("inactive");
    default:
        return TEXT("unknown");
    }
}

TSharedPtr<FJsonObject> BuildExecuteRuntimeContext()
{
    TSharedPtr<FJsonObject> Runtime = MakeShared<FJsonObject>();
    const bool bIsPIE = GEditor != nullptr && GEditor->IsPlayingSessionInEditor();
    Runtime->SetBoolField(TEXT("isPIE"), bIsPIE);

    UWorld* EditorWorld = GEditor != nullptr ? GEditor->GetEditorWorldContext().World() : nullptr;
    UWorld* PieWorld = GEditor != nullptr ? GEditor->PlayWorld : nullptr;
    UWorld* ActiveWorld = bIsPIE && PieWorld != nullptr ? PieWorld : EditorWorld;

    Runtime->SetStringField(TEXT("editorWorld"), EditorWorld ? EditorWorld->GetName() : TEXT(""));
    Runtime->SetStringField(TEXT("pieWorld"), PieWorld ? PieWorld->GetName() : TEXT(""));
    Runtime->SetStringField(TEXT("activeWorld"), ActiveWorld ? ActiveWorld->GetName() : TEXT(""));
    Runtime->SetStringField(TEXT("activeWorldType"), ActiveWorld ? LoomleWorldTypeToString(ActiveWorld->WorldType) : TEXT("none"));
    Runtime->SetStringField(TEXT("sessionMode"), bIsPIE ? TEXT("pie") : TEXT("editor"));
    return Runtime;
}

UWorld* ResolveProfilingWorld(const TSharedPtr<FJsonObject>& Arguments)
{
    FString RequestedWorld = TEXT("active");
    if (Arguments.IsValid())
    {
        Arguments->TryGetStringField(TEXT("world"), RequestedWorld);
    }
    RequestedWorld = RequestedWorld.TrimStartAndEnd().ToLower();

    UWorld* EditorWorld = GEditor != nullptr ? GEditor->GetEditorWorldContext().World() : nullptr;
    UWorld* PieWorld = GEditor != nullptr ? GEditor->PlayWorld : nullptr;

    if (RequestedWorld.IsEmpty() || RequestedWorld.Equals(TEXT("active")))
    {
        if (PieWorld != nullptr)
        {
            return PieWorld;
        }
        return EditorWorld;
    }

    if (RequestedWorld.Equals(TEXT("pie")))
    {
        return PieWorld;
    }

    if (RequestedWorld.Equals(TEXT("editor")))
    {
        return EditorWorld;
    }

    return nullptr;
}

TSharedPtr<FJsonObject> BuildProfilingRuntimeContext(UWorld* World, UGameViewportClient* GameViewport)
{
    TSharedPtr<FJsonObject> Runtime = BuildExecuteRuntimeContext();
    Runtime->SetStringField(TEXT("worldName"), World ? World->GetName() : TEXT(""));
    Runtime->SetStringField(TEXT("worldType"), World ? LoomleWorldTypeToString(World->WorldType) : TEXT("none"));
    Runtime->SetStringField(TEXT("viewportKind"), GameViewport != nullptr ? TEXT("game") : TEXT("none"));
    return Runtime;
}

TSharedPtr<FJsonObject> MakeProfilingError(const FString& Code, const FString& Message)
{
    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetBoolField(TEXT("isError"), true);
    Result->SetStringField(TEXT("code"), Code);
    Result->SetStringField(TEXT("message"), Message);
    return Result;
}

FString LoomleMemoryPressureStatusToString(const FGenericPlatformMemoryStats::EMemoryPressureStatus Status)
{
    switch (Status)
    {
    case FGenericPlatformMemoryStats::EMemoryPressureStatus::Nominal:
        return TEXT("nominal");
    case FGenericPlatformMemoryStats::EMemoryPressureStatus::Warning:
        return TEXT("warning");
    case FGenericPlatformMemoryStats::EMemoryPressureStatus::Critical:
        return TEXT("critical");
    case FGenericPlatformMemoryStats::EMemoryPressureStatus::Unknown:
    default:
        return TEXT("unknown");
    }
}

FString LoomleMemoryAllocatorToString(const FGenericPlatformMemory::EMemoryAllocatorToUse Allocator)
{
    switch (Allocator)
    {
    case FGenericPlatformMemory::EMemoryAllocatorToUse::Ansi:
        return TEXT("ansi");
    case FGenericPlatformMemory::EMemoryAllocatorToUse::Stomp:
        return TEXT("stomp");
    case FGenericPlatformMemory::EMemoryAllocatorToUse::TBB:
        return TEXT("tbb");
    case FGenericPlatformMemory::EMemoryAllocatorToUse::Jemalloc:
        return TEXT("jemalloc");
    case FGenericPlatformMemory::EMemoryAllocatorToUse::Binned:
        return TEXT("binned");
    case FGenericPlatformMemory::EMemoryAllocatorToUse::Binned2:
        return TEXT("binned2");
    case FGenericPlatformMemory::EMemoryAllocatorToUse::Binned3:
        return TEXT("binned3");
    case FGenericPlatformMemory::EMemoryAllocatorToUse::Platform:
        return TEXT("platform");
    case FGenericPlatformMemory::EMemoryAllocatorToUse::Mimalloc:
        return TEXT("mimalloc");
    case FGenericPlatformMemory::EMemoryAllocatorToUse::Libpas:
        return TEXT("libpas");
    default:
        return TEXT("unknown");
    }
}

TArray<TSharedPtr<FJsonValue>> BuildProfilingGenericMemoryStatRows(const FGenericMemoryStats& Stats)
{
    struct FRow
    {
        FString Name;
        SIZE_T Value = 0;
    };

    TArray<FRow> SortedRows;
    for (const auto& Pair : Stats)
    {
        FRow& Row = SortedRows.AddDefaulted_GetRef();
        Row.Name = FString(Pair.Key);
        Row.Value = Pair.Value;
    }

    SortedRows.Sort([](const FRow& A, const FRow& B)
    {
        return A.Name < B.Name;
    });

    TArray<TSharedPtr<FJsonValue>> Rows;
    Rows.Reserve(SortedRows.Num());
    for (const FRow& Row : SortedRows)
    {
        TSharedPtr<FJsonObject> RowObject = MakeShared<FJsonObject>();
        RowObject->SetStringField(TEXT("name"), Row.Name);
        RowObject->SetNumberField(TEXT("value"), static_cast<double>(Row.Value));
        Rows.Add(MakeShared<FJsonValueObject>(RowObject));
    }
    return Rows;
}

TArray<TSharedPtr<FJsonValue>> BuildProfilingPlatformSpecificMemoryRows(const TArray<FGenericPlatformMemoryStats::FPlatformSpecificStat>& Stats)
{
    struct FRow
    {
        FString Name;
        uint64 Value = 0;
    };

    TArray<FRow> SortedRows;
    SortedRows.Reserve(Stats.Num());
    for (const FGenericPlatformMemoryStats::FPlatformSpecificStat& Stat : Stats)
    {
        FRow& Row = SortedRows.AddDefaulted_GetRef();
        Row.Name = Stat.Name != nullptr ? FString(Stat.Name) : TEXT("");
        Row.Value = Stat.Value;
    }

    SortedRows.Sort([](const FRow& A, const FRow& B)
    {
        return A.Name < B.Name;
    });

    TArray<TSharedPtr<FJsonValue>> Rows;
    Rows.Reserve(SortedRows.Num());
    for (const FRow& Row : SortedRows)
    {
        TSharedPtr<FJsonObject> RowObject = MakeShared<FJsonObject>();
        RowObject->SetStringField(TEXT("name"), Row.Name);
        RowObject->SetNumberField(TEXT("value"), static_cast<double>(Row.Value));
        Rows.Add(MakeShared<FJsonValueObject>(RowObject));
    }
    return Rows;
}

bool EnsureProfilingUnitStatsEnabled(UWorld* World, UGameViewportClient* GameViewport)
{
    if (World == nullptr || GameViewport == nullptr || GEngine == nullptr)
    {
        return false;
    }

    if (GameViewport->IsStatEnabled(TEXT("Unit")))
    {
        return false;
    }

    GEngine->SetEngineStat(World, GameViewport, TEXT("Unit"), true);
    return true;
}

bool ProfilingUnitDataNeedsWarmup(const FStatUnitData* StatUnitData, const int32 GpuIndex)
{
    if (StatUnitData == nullptr)
    {
        return true;
    }

    return FMath::IsNearlyZero(StatUnitData->FrameTime)
        && FMath::IsNearlyZero(StatUnitData->RawFrameTime)
        && FMath::IsNearlyZero(StatUnitData->GameThreadTime)
        && FMath::IsNearlyZero(StatUnitData->RawGameThreadTime)
        && FMath::IsNearlyZero(StatUnitData->RenderThreadTime)
        && FMath::IsNearlyZero(StatUnitData->RawRenderThreadTime)
        && FMath::IsNearlyZero(StatUnitData->GPUFrameTime[GpuIndex])
        && FMath::IsNearlyZero(StatUnitData->RawGPUFrameTime[GpuIndex]);
}

FString NormalizeProfilingStatGroupName(const FString& RequestedGroup)
{
    FString Normalized = RequestedGroup.TrimStartAndEnd();
    if (Normalized.IsEmpty())
    {
        return TEXT("game");
    }

    if (Normalized.StartsWith(TEXT("statgroup_"), ESearchCase::IgnoreCase))
    {
        Normalized.RightChopInline(10, EAllowShrinking::No);
    }

    return Normalized.ToLower();
}

bool EnsureProfilingStatsGroupEnabled(UWorld* World, UGameViewportClient* GameViewport, const FString& RequestedGroup)
{
#if STATS
    if (World == nullptr || GEngine == nullptr || GameViewport == nullptr)
    {
        return false;
    }

    const FString NormalizedGroup = NormalizeProfilingStatGroupName(RequestedGroup);
    if (NormalizedGroup.Equals(TEXT("game")) && GameViewport->IsStatEnabled(TEXT("Game")))
    {
        return false;
    }

    const FString Command = FString::Printf(TEXT("stat %s -nodisplay"), *NormalizedGroup);
    return GEngine->Exec(World, *Command, *GLog);
#else
    return false;
#endif
}

bool EnsureProfilingGpuStatsEnabled(UWorld* World, UGameViewportClient* GameViewport)
{
#if STATS
    if (World == nullptr || GEngine == nullptr || GameViewport == nullptr)
    {
        return false;
    }

    static bool bRequestedGpuStats = false;
    if (bRequestedGpuStats)
    {
        return false;
    }

    const bool bExecuted = GEngine->Exec(World, TEXT("stat gpu -nodisplay"), *GLog);
    if (bExecuted)
    {
        bRequestedGpuStats = true;
    }
    return bExecuted;
#else
    return false;
#endif
}

const FActiveStatGroupInfo* FindProfilingActiveStatGroup(
    const FGameThreadStatsData* StatsData,
    const FString& RequestedGroup,
    FString& OutResolvedGroupName,
    FString& OutGroupDescription)
{
#if STATS
    OutResolvedGroupName.Reset();
    OutGroupDescription.Reset();

    if (StatsData == nullptr)
    {
        return nullptr;
    }

    const FString NormalizedGroup = NormalizeProfilingStatGroupName(RequestedGroup);
    for (int32 Index = 0; Index < StatsData->GroupNames.Num() && Index < StatsData->ActiveStatGroups.Num(); ++Index)
    {
        const FString FullGroupName = StatsData->GroupNames[Index].ToString();
        FString CandidateName = FullGroupName;
        if (CandidateName.StartsWith(TEXT("STATGROUP_"), ESearchCase::IgnoreCase))
        {
            CandidateName.RightChopInline(10, EAllowShrinking::No);
        }

        if (CandidateName.Equals(NormalizedGroup, ESearchCase::IgnoreCase))
        {
            OutResolvedGroupName = FullGroupName;
            if (StatsData->GroupDescriptions.IsValidIndex(Index))
            {
                OutGroupDescription = StatsData->GroupDescriptions[Index];
            }
            return &StatsData->ActiveStatGroups[Index];
        }
    }
#endif

    return nullptr;
}

TSharedPtr<FJsonObject> BuildProfilingCycleColumns()
{
    TSharedPtr<FJsonObject> Table = MakeShared<FJsonObject>();
    TArray<TSharedPtr<FJsonValue>> Columns;

    auto AddColumn = [&](const TCHAR* Key, const TCHAR* Label, const TCHAR* Unit = nullptr)
    {
        TSharedPtr<FJsonObject> Column = MakeShared<FJsonObject>();
        Column->SetStringField(TEXT("key"), Key);
        Column->SetStringField(TEXT("label"), Label);
        if (Unit != nullptr && FCString::Strlen(Unit) > 0)
        {
            Column->SetStringField(TEXT("unit"), Unit);
        }
        Columns.Add(MakeShared<FJsonValueObject>(Column));
    };

    AddColumn(TEXT("name"), TEXT("Name"));
    AddColumn(TEXT("callCountAverage"), TEXT("Calls"));
    AddColumn(TEXT("inclusiveAverageMs"), TEXT("Inclusive Avg"), TEXT("ms"));
    AddColumn(TEXT("inclusiveMaxMs"), TEXT("Inclusive Max"), TEXT("ms"));
    AddColumn(TEXT("exclusiveAverageMs"), TEXT("Exclusive Avg"), TEXT("ms"));
    AddColumn(TEXT("exclusiveMaxMs"), TEXT("Exclusive Max"), TEXT("ms"));

    Table->SetArrayField(TEXT("columns"), Columns);
    return Table;
}

TSharedPtr<FJsonObject> BuildProfilingStatValueFields(const FComplexStatMessage& StatMessage)
{
    TSharedPtr<FJsonObject> Values = MakeShared<FJsonObject>();
    const bool bIsCycle = StatMessage.NameAndInfo.GetFlag(EStatMetaFlags::IsCycle);
    const bool bIsPackedCycle = bIsCycle && StatMessage.NameAndInfo.GetFlag(EStatMetaFlags::IsPackedCCAndDuration);
    const EStatDataType::Type DataType = StatMessage.NameAndInfo.GetField<EStatDataType>();

    auto SetDurationFields = [&](TSharedPtr<FJsonObject>& Target, const TCHAR* Prefix, const EComplexStatField::Type Sum, const EComplexStatField::Type Ave, const EComplexStatField::Type Max, const EComplexStatField::Type Min)
    {
        Target->SetNumberField(FString::Printf(TEXT("%sSumMs"), Prefix), FPlatformTime::ToMilliseconds64(StatMessage.GetValue_Duration(Sum)));
        Target->SetNumberField(FString::Printf(TEXT("%sAverageMs"), Prefix), FPlatformTime::ToMilliseconds64(StatMessage.GetValue_Duration(Ave)));
        Target->SetNumberField(FString::Printf(TEXT("%sMaxMs"), Prefix), FPlatformTime::ToMilliseconds64(StatMessage.GetValue_Duration(Max)));
        Target->SetNumberField(FString::Printf(TEXT("%sMinMs"), Prefix), FPlatformTime::ToMilliseconds64(StatMessage.GetValue_Duration(Min)));
    };

    if (bIsCycle)
    {
        SetDurationFields(Values, TEXT("inclusive"), EComplexStatField::IncSum, EComplexStatField::IncAve, EComplexStatField::IncMax, EComplexStatField::IncMin);
        SetDurationFields(Values, TEXT("exclusive"), EComplexStatField::ExcSum, EComplexStatField::ExcAve, EComplexStatField::ExcMax, EComplexStatField::ExcMin);

        if (bIsPackedCycle)
        {
            Values->SetNumberField(TEXT("callCountSum"), static_cast<double>(StatMessage.GetValue_CallCount(EComplexStatField::IncSum)));
            Values->SetNumberField(TEXT("callCountAverage"), static_cast<double>(StatMessage.GetValue_CallCount(EComplexStatField::IncAve)));
            Values->SetNumberField(TEXT("callCountMax"), static_cast<double>(StatMessage.GetValue_CallCount(EComplexStatField::IncMax)));
            Values->SetNumberField(TEXT("callCountMin"), static_cast<double>(StatMessage.GetValue_CallCount(EComplexStatField::IncMin)));
        }
        return Values;
    }

    auto SetScalarField = [&](const TCHAR* Prefix, const EComplexStatField::Type Index)
    {
        const FString FieldName = FString::Printf(TEXT("%sValue"), Prefix);
        if (DataType == EStatDataType::ST_int64)
        {
            Values->SetNumberField(FieldName, static_cast<double>(StatMessage.GetValue_int64(Index)));
        }
        else if (DataType == EStatDataType::ST_double)
        {
            Values->SetNumberField(FieldName, StatMessage.GetValue_double(Index));
        }
    };

    SetScalarField(TEXT("inclusiveAverage"), EComplexStatField::IncAve);
    SetScalarField(TEXT("inclusiveMax"), EComplexStatField::IncMax);
    SetScalarField(TEXT("inclusiveMin"), EComplexStatField::IncMin);
    return Values;
}

TSharedPtr<FJsonObject> BuildProfilingComplexStatRow(const FComplexStatMessage& StatMessage, const int32 Indentation)
{
    TSharedPtr<FJsonObject> Row = MakeShared<FJsonObject>();
    const EStatDataType::Type DataType = StatMessage.NameAndInfo.GetField<EStatDataType>();
    const bool bIsCycle = StatMessage.NameAndInfo.GetFlag(EStatMetaFlags::IsCycle);
    const bool bIsPackedCycle = bIsCycle && StatMessage.NameAndInfo.GetFlag(EStatMetaFlags::IsPackedCCAndDuration);

    Row->SetStringField(TEXT("name"), StatMessage.GetShortName().ToString());
    Row->SetStringField(TEXT("rawName"), StatMessage.NameAndInfo.GetRawName().ToString());
    Row->SetStringField(TEXT("description"), StatMessage.GetDescription());
    Row->SetBoolField(TEXT("isCycle"), bIsCycle);
    Row->SetBoolField(TEXT("isMemory"), StatMessage.NameAndInfo.GetFlag(EStatMetaFlags::IsMemory));
    Row->SetBoolField(TEXT("isGpu"), StatMessage.NameAndInfo.GetFlag(EStatMetaFlags::IsGPU));
    Row->SetBoolField(TEXT("isPackedCallCountAndDuration"), bIsPackedCycle);
    Row->SetStringField(
        TEXT("dataType"),
        DataType == EStatDataType::ST_int64 ? TEXT("int64") : (DataType == EStatDataType::ST_double ? TEXT("double") : TEXT("other")));

    if (Indentation >= 0)
    {
        Row->SetNumberField(TEXT("indentation"), static_cast<double>(Indentation));
    }

    TSharedPtr<FJsonObject> Values = BuildProfilingStatValueFields(StatMessage);
    Row->SetObjectField(TEXT("values"), Values);

    const TSharedPtr<FJsonObject>* ValuesObject = nullptr;
    if (Row->TryGetObjectField(TEXT("values"), ValuesObject) && ValuesObject && (*ValuesObject).IsValid())
    {
        double CallCountAverage = 0.0;
        if ((*ValuesObject)->TryGetNumberField(TEXT("callCountAverage"), CallCountAverage))
        {
            Row->SetNumberField(TEXT("callCountAverage"), CallCountAverage);
        }

        double InclusiveAverageMs = 0.0;
        if ((*ValuesObject)->TryGetNumberField(TEXT("inclusiveAverageMs"), InclusiveAverageMs))
        {
            Row->SetNumberField(TEXT("inclusiveAverageMs"), InclusiveAverageMs);
        }

        double InclusiveMaxMs = 0.0;
        if ((*ValuesObject)->TryGetNumberField(TEXT("inclusiveMaxMs"), InclusiveMaxMs))
        {
            Row->SetNumberField(TEXT("inclusiveMaxMs"), InclusiveMaxMs);
        }

        double ExclusiveAverageMs = 0.0;
        if ((*ValuesObject)->TryGetNumberField(TEXT("exclusiveAverageMs"), ExclusiveAverageMs))
        {
            Row->SetNumberField(TEXT("exclusiveAverageMs"), ExclusiveAverageMs);
        }

        double ExclusiveMaxMs = 0.0;
        if ((*ValuesObject)->TryGetNumberField(TEXT("exclusiveMaxMs"), ExclusiveMaxMs))
        {
            Row->SetNumberField(TEXT("exclusiveMaxMs"), ExclusiveMaxMs);
        }
    }

    return Row;
}

TArray<TSharedPtr<FJsonValue>> BuildProfilingComplexStatRows(
    const TArray<FComplexStatMessage>& SourceRows,
    const TArray<int32>* Indentation,
    const int32 MaxDepth)
{
    TArray<TSharedPtr<FJsonValue>> Rows;
    Rows.Reserve(SourceRows.Num());
    for (int32 Index = 0; Index < SourceRows.Num(); ++Index)
    {
        const int32 CurrentIndent = (Indentation != nullptr && Indentation->IsValidIndex(Index)) ? (*Indentation)[Index] : -1;
        if (MaxDepth >= 0 && CurrentIndent > MaxDepth)
        {
            continue;
        }

        Rows.Add(MakeShared<FJsonValueObject>(BuildProfilingComplexStatRow(SourceRows[Index], CurrentIndent)));
    }
    return Rows;
}

TSharedPtr<FJsonObject> BuildProfilingGpuColumns()
{
    TSharedPtr<FJsonObject> Table = MakeShared<FJsonObject>();
    TArray<TSharedPtr<FJsonValue>> Columns;

    auto AddColumn = [&](const TCHAR* Key, const TCHAR* Label, const TCHAR* Unit = nullptr)
    {
        TSharedPtr<FJsonObject> Column = MakeShared<FJsonObject>();
        Column->SetStringField(TEXT("key"), Key);
        Column->SetStringField(TEXT("label"), Label);
        if (Unit != nullptr && FCString::Strlen(Unit) > 0)
        {
            Column->SetStringField(TEXT("unit"), Unit);
        }
        Columns.Add(MakeShared<FJsonValueObject>(Column));
    };

    AddColumn(TEXT("name"), TEXT("Name"));
    AddColumn(TEXT("busyAverageMs"), TEXT("Busy Avg"), TEXT("ms"));
    AddColumn(TEXT("busyMaxMs"), TEXT("Busy Max"), TEXT("ms"));
    AddColumn(TEXT("busyMinMs"), TEXT("Busy Min"), TEXT("ms"));
    AddColumn(TEXT("waitAverageMs"), TEXT("Wait Avg"), TEXT("ms"));
    AddColumn(TEXT("waitMaxMs"), TEXT("Wait Max"), TEXT("ms"));
    AddColumn(TEXT("waitMinMs"), TEXT("Wait Min"), TEXT("ms"));
    AddColumn(TEXT("idleAverageMs"), TEXT("Idle Avg"), TEXT("ms"));
    AddColumn(TEXT("idleMaxMs"), TEXT("Idle Max"), TEXT("ms"));
    AddColumn(TEXT("idleMinMs"), TEXT("Idle Min"), TEXT("ms"));

    Table->SetArrayField(TEXT("columns"), Columns);
    return Table;
}

#if RHI_NEW_GPU_PROFILER
struct FLoomleProfilingGpuRowState
{
    const FComplexStatMessage* Busy = nullptr;
    const FComplexStatMessage* Wait = nullptr;
    const FComplexStatMessage* Idle = nullptr;
};
#endif

void PopulateProfilingGpuTimingFields(
    const TSharedPtr<FJsonObject>& Row,
    const FComplexStatMessage* Message,
    const TCHAR* Prefix)
{
    if (!Row.IsValid() || Message == nullptr || Prefix == nullptr)
    {
        return;
    }

    auto GetFieldValue = [&](const EComplexStatField::Type Field) -> double
    {
        const EStatDataType::Type DataType = Message->NameAndInfo.GetField<EStatDataType>();
        if (DataType == EStatDataType::ST_int64)
        {
            return static_cast<double>(Message->GetValue_int64(Field));
        }
        if (DataType == EStatDataType::ST_double)
        {
            return Message->GetValue_double(Field);
        }
        return 0.0;
    };

    Row->SetNumberField(FString::Printf(TEXT("%sAverageMs"), Prefix), GetFieldValue(EComplexStatField::IncAve));
    Row->SetNumberField(FString::Printf(TEXT("%sMaxMs"), Prefix), GetFieldValue(EComplexStatField::IncMax));
    Row->SetNumberField(FString::Printf(TEXT("%sMinMs"), Prefix), GetFieldValue(EComplexStatField::IncMin));
}

#if RHI_NEW_GPU_PROFILER
TArray<TSharedPtr<FJsonValue>> BuildProfilingGpuRows(const TArray<FComplexStatMessage>& SourceRows)
{
    TMap<FName, FLoomleProfilingGpuRowState> GroupedRows;
    for (const FComplexStatMessage& Message : SourceRows)
    {
        FName ShortName = Message.GetShortName();
        const int32 TypeOrdinal = ShortName.GetNumber();
        ShortName.SetNumber(0);

        using EGpuStatType = UE::RHI::GPUProfiler::FGPUStat::EType;

        FLoomleProfilingGpuRowState& RowState = GroupedRows.FindOrAdd(ShortName);
        switch (static_cast<EGpuStatType>(TypeOrdinal))
        {
        case EGpuStatType::Busy:
            RowState.Busy = &Message;
            break;
        case EGpuStatType::Wait:
            RowState.Wait = &Message;
            break;
        case EGpuStatType::Idle:
            RowState.Idle = &Message;
            break;
        default:
            break;
        }
    }

    TArray<FName> SortedNames;
    GroupedRows.GetKeys(SortedNames);
    SortedNames.Sort(FNameLexicalLess());

    TArray<TSharedPtr<FJsonValue>> Rows;
    Rows.Reserve(SortedNames.Num());
    for (const FName& Name : SortedNames)
    {
        const FLoomleProfilingGpuRowState* RowState = GroupedRows.Find(Name);
        if (RowState == nullptr)
        {
            continue;
        }

        const FComplexStatMessage* LabelStat =
            RowState->Busy ? RowState->Busy :
            (RowState->Wait ? RowState->Wait : RowState->Idle);
        if (LabelStat == nullptr)
        {
            continue;
        }

        TSharedPtr<FJsonObject> Row = MakeShared<FJsonObject>();
        Row->SetStringField(TEXT("name"), LabelStat->GetShortName().GetPlainNameString());
        Row->SetStringField(TEXT("rawName"), LabelStat->NameAndInfo.GetRawName().ToString());
        Row->SetStringField(TEXT("description"), LabelStat->GetDescription());

        PopulateProfilingGpuTimingFields(Row, RowState->Busy, TEXT("busy"));
        PopulateProfilingGpuTimingFields(Row, RowState->Wait, TEXT("wait"));
        PopulateProfilingGpuTimingFields(Row, RowState->Idle, TEXT("idle"));

        Rows.Add(MakeShared<FJsonValueObject>(Row));
    }

    return Rows;
}
#endif

class FProfilingOutputDevice final : public FOutputDevice
{
public:
    virtual void Serialize(const TCHAR* V, ELogVerbosity::Type Verbosity, const class FName& Category) override
    {
        (void)Verbosity;
        (void)Category;
        Lines.Add(V ? FString(V) : FString());
    }

    TArray<FString> Lines;
};

TSharedPtr<FJsonObject> BuildProfilingTicksColumns()
{
    TSharedPtr<FJsonObject> Table = MakeShared<FJsonObject>();
    TArray<TSharedPtr<FJsonValue>> Columns;

    auto AddColumn = [&](const TCHAR* Key, const TCHAR* Label)
    {
        TSharedPtr<FJsonObject> Column = MakeShared<FJsonObject>();
        Column->SetStringField(TEXT("key"), Key);
        Column->SetStringField(TEXT("label"), Label);
        Columns.Add(MakeShared<FJsonValueObject>(Column));
    };

    AddColumn(TEXT("diagnosticMessage"), TEXT("Diagnostic"));
    AddColumn(TEXT("state"), TEXT("State"));
    AddColumn(TEXT("actualStartTickGroup"), TEXT("Actual Start Tick Group"));
    AddColumn(TEXT("prerequisiteCount"), TEXT("Prerequisites"));
    Table->SetArrayField(TEXT("columns"), Columns);
    return Table;
}

TSharedPtr<FJsonObject> ParseProfilingTickRow(const FString& Line)
{
    const FString PrereqToken = TEXT(", Prerequesities: ");
    const FString GroupToken = TEXT(", ActualStartTickGroup: ");

    int32 PrereqIndex = INDEX_NONE;
    if (!Line.FindLastChar(TEXT(':'), PrereqIndex) || !Line.Contains(PrereqToken))
    {
        return nullptr;
    }

    PrereqIndex = Line.Find(PrereqToken, ESearchCase::CaseSensitive, ESearchDir::FromEnd);
    const int32 GroupIndex = Line.Find(GroupToken, ESearchCase::CaseSensitive, ESearchDir::FromEnd);
    if (PrereqIndex == INDEX_NONE || GroupIndex == INDEX_NONE || GroupIndex >= PrereqIndex)
    {
        return nullptr;
    }

    const FString PrereqText = Line.Mid(PrereqIndex + PrereqToken.Len()).TrimStartAndEnd();
    const FString GroupName = Line.Mid(GroupIndex + GroupToken.Len(), PrereqIndex - (GroupIndex + GroupToken.Len())).TrimStartAndEnd();
    const FString Prefix = Line.Left(GroupIndex);

    FString DiagnosticMessage;
    FString State;
    FString CoolingDownPrefix = TEXT(", Cooling Down for ");
    int32 StateSplitIndex = Prefix.Find(TEXT(", Disabled"), ESearchCase::CaseSensitive, ESearchDir::FromEnd);
    if (StateSplitIndex != INDEX_NONE)
    {
        DiagnosticMessage = Prefix.Left(StateSplitIndex).TrimStartAndEnd();
        State = TEXT("Disabled");
    }
    else
    {
        StateSplitIndex = Prefix.Find(TEXT(", Enabled"), ESearchCase::CaseSensitive, ESearchDir::FromEnd);
        if (StateSplitIndex != INDEX_NONE)
        {
            DiagnosticMessage = Prefix.Left(StateSplitIndex).TrimStartAndEnd();
            State = TEXT("Enabled");
        }
        else
        {
            StateSplitIndex = Prefix.Find(CoolingDownPrefix, ESearchCase::CaseSensitive, ESearchDir::FromEnd);
            if (StateSplitIndex != INDEX_NONE)
            {
                DiagnosticMessage = Prefix.Left(StateSplitIndex).TrimStartAndEnd();
                State = Prefix.Mid(StateSplitIndex + 2).TrimStartAndEnd();
            }
        }
    }

    if (DiagnosticMessage.IsEmpty() || State.IsEmpty())
    {
        return nullptr;
    }

    TSharedPtr<FJsonObject> Row = MakeShared<FJsonObject>();
    Row->SetStringField(TEXT("diagnosticMessage"), DiagnosticMessage);
    Row->SetStringField(TEXT("state"), State);
    Row->SetStringField(TEXT("actualStartTickGroup"), GroupName);
    Row->SetNumberField(TEXT("prerequisiteCount"), static_cast<double>(FCString::Atoi(*PrereqText)));

    if (State.StartsWith(TEXT("Cooling Down for "), ESearchCase::CaseSensitive))
    {
        const FString SecondsText = State.RightChop(FCString::Strlen(TEXT("Cooling Down for "))).Replace(TEXT(" seconds"), TEXT(""));
        Row->SetNumberField(TEXT("remainingCooldownSeconds"), FCString::Atof(*SecondsText));
    }

    return Row;
}

TSharedPtr<FJsonObject> ParseProfilingTickPrerequisite(const FString& Line)
{
    const FString Trimmed = Line.TrimStartAndEnd();
    if (Trimmed.IsEmpty())
    {
        return nullptr;
    }

    TSharedPtr<FJsonObject> Row = MakeShared<FJsonObject>();
    if (Trimmed.Equals(TEXT("Invalid Prerequisite")))
    {
        Row->SetBoolField(TEXT("isValid"), false);
        Row->SetStringField(TEXT("message"), Trimmed);
        return Row;
    }

    int32 SeparatorIndex = INDEX_NONE;
    if (!Trimmed.FindChar(TEXT(','), SeparatorIndex))
    {
        Row->SetBoolField(TEXT("isValid"), true);
        Row->SetStringField(TEXT("message"), Trimmed);
        return Row;
    }

    Row->SetBoolField(TEXT("isValid"), true);
    Row->SetStringField(TEXT("object"), Trimmed.Left(SeparatorIndex).TrimStartAndEnd());
    Row->SetStringField(TEXT("diagnosticMessage"), Trimmed.Mid(SeparatorIndex + 1).TrimStartAndEnd());
    return Row;
}

void AddJsonNumberArrayField(
    const TSharedPtr<FJsonObject>& Target,
    const TCHAR* FieldName,
    const TArray<float>& Values)
{
    if (!Target.IsValid())
    {
        return;
    }

    TArray<TSharedPtr<FJsonValue>> ArrayValues;
    ArrayValues.Reserve(Values.Num());
    for (const float Value : Values)
    {
        ArrayValues.Add(MakeShared<FJsonValueNumber>(static_cast<double>(Value)));
    }
    Target->SetArrayField(FieldName, ArrayValues);
}

FString LoomleJobIso8601OrEmpty(const FDateTime& Value)
{
    return Value.GetTicks() > 0 ? Value.ToIso8601() : TEXT("");
}

TSharedPtr<FJsonObject> CloneJobBusinessArguments(const TSharedPtr<FJsonObject>& Arguments)
{
    TSharedPtr<FJsonObject> BusinessArguments = CloneJsonObject(Arguments);
    if (!BusinessArguments.IsValid())
    {
        BusinessArguments = MakeShared<FJsonObject>();
    }

    BusinessArguments->RemoveField(TEXT("execution"));
    BusinessArguments->RemoveField(TEXT("timeoutMs"));
    return BusinessArguments;
}

FString NormalizeJobFingerprint(const FString& ToolName, const TSharedPtr<FJsonObject>& BusinessArguments)
{
    return ToolName + TEXT("|") + SerializeJsonObjectCondensed(BusinessArguments);
}

int32 ReadOptionalPositiveIntField(const TSharedPtr<FJsonObject>& Object, const TCHAR* FieldName, const int32 DefaultValue)
{
    if (!Object.IsValid())
    {
        return DefaultValue;
    }

    double ValueNumber = 0.0;
    if (!Object->TryGetNumberField(FieldName, ValueNumber))
    {
        return DefaultValue;
    }

    if (ValueNumber < 1.0)
    {
        return DefaultValue;
    }

    return static_cast<int32>(ValueNumber);
}
}

void FLoomleBridgeModule::AppendJobLogLine(const FString& JobId, const FString& Level, const FString& Message)
{
    if (Message.IsEmpty())
    {
        return;
    }

    FScopeLock Lock(&JobRegistryMutex);
    const FString ResolvedJobId = JobId.IsEmpty() ? ActiveJobId : JobId;
    if (ResolvedJobId.IsEmpty())
    {
        return;
    }

    FToolJobEntry* JobEntry = JobRegistry.Find(ResolvedJobId);
    if (JobEntry == nullptr)
    {
        return;
    }

    FJobLogEntry LogEntry;
    LogEntry.Time = FDateTime::UtcNow().ToIso8601();
    LogEntry.Level = Level;
    LogEntry.Message = Message;
    JobEntry->Logs.Add(MoveTemp(LogEntry));
    JobEntry->HeartbeatAt = FDateTime::UtcNow();
}

void FLoomleBridgeModule::StartNextJobIfNeeded()
{
    FString JobIdToRun;
    {
        FScopeLock Lock(&JobRegistryMutex);
        if (bJobRunnerActive || JobQueue.IsEmpty())
        {
            return;
        }

        JobIdToRun = JobQueue[0];
        JobQueue.RemoveAt(0);

        FToolJobEntry* JobEntry = JobRegistry.Find(JobIdToRun);
        if (JobEntry == nullptr)
        {
            return;
        }

        bJobRunnerActive = true;
        ActiveJobId = JobIdToRun;
        JobEntry->Status = TEXT("running");
        JobEntry->StartedAt = FDateTime::UtcNow();
        JobEntry->HeartbeatAt = JobEntry->StartedAt;
    }

    Async(EAsyncExecution::ThreadPool, [this, JobIdToRun]()
    {
        RunQueuedJob(JobIdToRun);
    });
}

void FLoomleBridgeModule::RunQueuedJob(const FString& JobId)
{
    FString ToolName;
    TSharedPtr<FJsonObject> BusinessArguments;
    {
        FScopeLock Lock(&JobRegistryMutex);
        if (const FToolJobEntry* JobEntry = JobRegistry.Find(JobId))
        {
            ToolName = JobEntry->ToolName;
            BusinessArguments = CloneJsonObject(JobEntry->BusinessArguments);
        }
    }

    TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
    bool bIsError = false;

    if (ToolName.Equals(LoomleBridgeConstants::ExecuteToolName))
    {
        struct FAsyncExecuteResult
        {
            TSharedPtr<FJsonObject> Payload;
            bool bIsError = false;
        };

        TPromise<FAsyncExecuteResult> Promise;
        TFuture<FAsyncExecuteResult> Future = Promise.GetFuture();
        AsyncTask(ENamedThreads::GameThread, [this, BusinessArguments, Promise = MoveTemp(Promise)]() mutable
        {
            FAsyncExecuteResult ExecuteResult;
            ExecuteResult.Payload = BuildExecutePythonToolResult(BusinessArguments);
            if (ExecuteResult.Payload.IsValid())
            {
                ExecuteResult.Payload->TryGetBoolField(TEXT("isError"), ExecuteResult.bIsError);
            }
            Promise.SetValue(MoveTemp(ExecuteResult));
        });

        const FAsyncExecuteResult ExecuteResult = Future.Get();
        Payload = ExecuteResult.Payload;
        bIsError = ExecuteResult.bIsError;
    }
    else
    {
        bIsError = true;
        Payload->SetBoolField(TEXT("isError"), true);
        Payload->SetStringField(TEXT("code"), TEXT("JOB_MODE_UNSUPPORTED"));
        Payload->SetStringField(TEXT("message"), TEXT("Job mode is not supported for this tool."));
    }

    if (Payload.IsValid())
    {
        const TArray<TSharedPtr<FJsonValue>>* PayloadLogs = nullptr;
        if (Payload->TryGetArrayField(TEXT("logs"), PayloadLogs) && PayloadLogs != nullptr)
        {
            for (const TSharedPtr<FJsonValue>& LogValue : *PayloadLogs)
            {
                const TSharedPtr<FJsonObject>* LogObject = nullptr;
                if (!LogValue.IsValid() || !LogValue->TryGetObject(LogObject) || LogObject == nullptr || !(*LogObject).IsValid())
                {
                    continue;
                }

                FString PythonLogType;
                FString PythonLogOutput;
                (*LogObject)->TryGetStringField(TEXT("type"), PythonLogType);
                (*LogObject)->TryGetStringField(TEXT("output"), PythonLogOutput);
                const FString PythonLogLevel = PythonLogType.Contains(TEXT("Error"), ESearchCase::IgnoreCase)
                    ? TEXT("error")
                    : (PythonLogType.Contains(TEXT("Warning"), ESearchCase::IgnoreCase) ? TEXT("warning") : TEXT("info"));
                AppendJobLogLine(JobId, PythonLogLevel, PythonLogOutput);
            }
        }
    }

    {
        FScopeLock Lock(&JobRegistryMutex);
        if (FToolJobEntry* JobEntry = JobRegistry.Find(JobId))
        {
            JobEntry->HeartbeatAt = FDateTime::UtcNow();
            JobEntry->FinishedAt = JobEntry->HeartbeatAt;
            JobEntry->FinalPayload = CloneJsonObject(Payload);
            JobEntry->bResultAvailable = !bIsError;
            JobEntry->Status = bIsError ? TEXT("failed") : TEXT("succeeded");
            if (bIsError)
            {
                JobEntry->ErrorPayload = CloneJsonObject(Payload);
            }
        }

        if (ActiveJobId == JobId)
        {
            ActiveJobId.Empty();
        }
        bJobRunnerActive = false;
    }

    StartNextJobIfNeeded();
}

TSharedPtr<FJsonObject> FLoomleBridgeModule::BuildJobsToolResult(const TSharedPtr<FJsonObject>& Arguments)
{
    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

    FString Action;
    if (!Arguments.IsValid() || !Arguments->TryGetStringField(TEXT("action"), Action) || Action.IsEmpty())
    {
        Result->SetBoolField(TEXT("isError"), true);
        Result->SetStringField(TEXT("code"), TEXT("INVALID_ARGUMENT"));
        Result->SetStringField(TEXT("message"), TEXT("arguments.action is required."));
        return Result;
    }

    Action = Action.ToLower();
    const auto BuildJobNotFound = [&]() -> TSharedPtr<FJsonObject>
    {
        TSharedPtr<FJsonObject> Error = MakeShared<FJsonObject>();
        Error->SetBoolField(TEXT("isError"), true);
        Error->SetStringField(TEXT("code"), TEXT("JOB_NOT_FOUND"));
        Error->SetStringField(TEXT("message"), TEXT("The requested jobId was not found."));
        return Error;
    };

    const auto IsResultExpired = [](const FToolJobEntry& Entry) -> bool
    {
        return Entry.FinishedAt.GetTicks() > 0
            && Entry.ResultTtlMs > 0
            && FDateTime::UtcNow() > (Entry.FinishedAt + FTimespan::FromMilliseconds(Entry.ResultTtlMs));
    };

    if (Action.Equals(TEXT("status")) || Action.Equals(TEXT("result")) || Action.Equals(TEXT("logs")))
    {
        FString JobId;
        if (!Arguments->TryGetStringField(TEXT("jobId"), JobId) || JobId.IsEmpty())
        {
            Result->SetBoolField(TEXT("isError"), true);
            Result->SetStringField(TEXT("code"), TEXT("INVALID_ARGUMENT"));
            Result->SetStringField(TEXT("message"), TEXT("jobId is required."));
            return Result;
        }

        FScopeLock Lock(&JobRegistryMutex);
        const FToolJobEntry* Entry = JobRegistry.Find(JobId);
        if (Entry == nullptr)
        {
            return BuildJobNotFound();
        }

        if (Action.Equals(TEXT("status")))
        {
            Result->SetBoolField(TEXT("isError"), false);
            Result->SetStringField(TEXT("jobId"), Entry->JobId);
            Result->SetStringField(TEXT("tool"), Entry->ToolName);
            Result->SetStringField(TEXT("status"), Entry->Status);
            Result->SetStringField(TEXT("acceptedAt"), LoomleJobIso8601OrEmpty(Entry->AcceptedAt));
            if (Entry->StartedAt.GetTicks() > 0)
            {
                Result->SetStringField(TEXT("startedAt"), LoomleJobIso8601OrEmpty(Entry->StartedAt));
            }
            if (Entry->FinishedAt.GetTicks() > 0)
            {
                Result->SetStringField(TEXT("finishedAt"), LoomleJobIso8601OrEmpty(Entry->FinishedAt));
            }
            if (Entry->HeartbeatAt.GetTicks() > 0)
            {
                Result->SetStringField(TEXT("heartbeatAt"), LoomleJobIso8601OrEmpty(Entry->HeartbeatAt));
            }
            Result->SetBoolField(TEXT("resultAvailable"), Entry->bResultAvailable && !IsResultExpired(*Entry));
            if (!Entry->Logs.IsEmpty())
            {
                Result->SetStringField(TEXT("logCursor"), FString::FromInt(Entry->Logs.Num()));
            }
            if (!Entry->Label.IsEmpty())
            {
                Result->SetStringField(TEXT("message"), Entry->Label);
            }
            return Result;
        }

        if (IsResultExpired(*Entry))
        {
            Result->SetBoolField(TEXT("isError"), true);
            Result->SetStringField(TEXT("code"), TEXT("JOB_RESULT_EXPIRED"));
            Result->SetStringField(TEXT("message"), TEXT("The requested job result is no longer retained."));
            return Result;
        }

        if (Action.Equals(TEXT("result")))
        {
            Result->SetBoolField(TEXT("isError"), false);
            Result->SetStringField(TEXT("jobId"), Entry->JobId);
            Result->SetStringField(TEXT("tool"), Entry->ToolName);
            Result->SetStringField(TEXT("status"), Entry->Status);
            Result->SetBoolField(TEXT("resultAvailable"), Entry->bResultAvailable);
            if (Entry->Status.Equals(TEXT("queued")) || Entry->Status.Equals(TEXT("running")))
            {
                return Result;
            }

            if (Entry->FinalPayload.IsValid())
            {
                Result->SetObjectField(TEXT("result"), CloneJsonObject(Entry->FinalPayload));
                FString Stdout;
                Entry->FinalPayload->TryGetStringField(TEXT("result"), Stdout);
                if (!Stdout.IsEmpty())
                {
                    Result->SetStringField(TEXT("stdout"), Stdout);
                }
            }

            if (Entry->ErrorPayload.IsValid())
            {
                TSharedPtr<FJsonObject> ErrorObject = MakeShared<FJsonObject>();
                FString ErrorCode;
                FString ErrorMessage;
                Entry->ErrorPayload->TryGetStringField(TEXT("code"), ErrorCode);
                Entry->ErrorPayload->TryGetStringField(TEXT("message"), ErrorMessage);
                ErrorObject->SetStringField(TEXT("code"), ErrorCode.IsEmpty() ? TEXT("INTERNAL_ERROR") : ErrorCode);
                ErrorObject->SetStringField(TEXT("message"), ErrorMessage.IsEmpty() ? TEXT("Job execution failed.") : ErrorMessage);
                Result->SetObjectField(TEXT("error"), ErrorObject);
            }
            return Result;
        }

        const int32 Limit = FMath::Clamp(ReadOptionalPositiveIntField(Arguments, TEXT("limit"), 200), 1, 1000);
        int32 StartIndex = 0;
        FString Cursor;
        if (Arguments->TryGetStringField(TEXT("cursor"), Cursor) && !Cursor.IsEmpty())
        {
            StartIndex = FMath::Max(0, FCString::Atoi(*Cursor));
        }

        Result->SetBoolField(TEXT("isError"), false);
        Result->SetStringField(TEXT("jobId"), Entry->JobId);
        TArray<TSharedPtr<FJsonValue>> Entries;
        const int32 EndIndex = FMath::Min(StartIndex + Limit, Entry->Logs.Num());
        for (int32 Index = StartIndex; Index < EndIndex; ++Index)
        {
            const FJobLogEntry& LogEntry = Entry->Logs[Index];
            TSharedPtr<FJsonObject> LogObject = MakeShared<FJsonObject>();
            LogObject->SetStringField(TEXT("time"), LogEntry.Time);
            LogObject->SetStringField(TEXT("level"), LogEntry.Level);
            LogObject->SetStringField(TEXT("message"), LogEntry.Message);
            Entries.Add(MakeShared<FJsonValueObject>(LogObject));
        }
        Result->SetArrayField(TEXT("entries"), Entries);
        Result->SetStringField(TEXT("nextCursor"), FString::FromInt(EndIndex));
        Result->SetBoolField(TEXT("hasMore"), EndIndex < Entry->Logs.Num());
        return Result;
    }

    if (Action.Equals(TEXT("list")))
    {
        FString StatusFilter;
        Arguments->TryGetStringField(TEXT("status"), StatusFilter);
        FString ToolFilter;
        Arguments->TryGetStringField(TEXT("tool"), ToolFilter);
        const int32 Limit = FMath::Clamp(ReadOptionalPositiveIntField(Arguments, TEXT("limit"), 100), 1, 1000);

        Result->SetBoolField(TEXT("isError"), false);
        TArray<TSharedPtr<FJsonValue>> Jobs;
        FScopeLock Lock(&JobRegistryMutex);
        for (const TPair<FString, FToolJobEntry>& Pair : JobRegistry)
        {
            const FToolJobEntry& Entry = Pair.Value;
            if (!StatusFilter.IsEmpty() && !Entry.Status.Equals(StatusFilter, ESearchCase::IgnoreCase))
            {
                continue;
            }
            if (!ToolFilter.IsEmpty() && !Entry.ToolName.Equals(ToolFilter, ESearchCase::IgnoreCase))
            {
                continue;
            }

            TSharedPtr<FJsonObject> JobObject = MakeShared<FJsonObject>();
            JobObject->SetStringField(TEXT("jobId"), Entry.JobId);
            JobObject->SetStringField(TEXT("tool"), Entry.ToolName);
            JobObject->SetStringField(TEXT("status"), Entry.Status);
            JobObject->SetStringField(TEXT("acceptedAt"), LoomleJobIso8601OrEmpty(Entry.AcceptedAt));
            if (!Entry.Label.IsEmpty())
            {
                JobObject->SetStringField(TEXT("label"), Entry.Label);
            }
            Jobs.Add(MakeShared<FJsonValueObject>(JobObject));
            if (Jobs.Num() >= Limit)
            {
                break;
            }
        }
        Result->SetArrayField(TEXT("jobs"), Jobs);
        return Result;
    }

    Result->SetBoolField(TEXT("isError"), true);
    Result->SetStringField(TEXT("code"), TEXT("JOB_ACTION_UNSUPPORTED"));
    Result->SetStringField(TEXT("message"), TEXT("The requested jobs action is not supported."));
    return Result;
}

TSharedPtr<FJsonObject> FLoomleBridgeModule::BuildProfilingToolResult(const TSharedPtr<FJsonObject>& Arguments)
{
    FString Action;
    if (!Arguments.IsValid() || !Arguments->TryGetStringField(TEXT("action"), Action) || Action.IsEmpty())
    {
        return MakeProfilingError(TEXT("INVALID_ARGUMENT"), TEXT("arguments.action is required."));
    }

    Action = Action.TrimStartAndEnd().ToLower();
    if (!Action.Equals(TEXT("unit")) && !Action.Equals(TEXT("game")) && !Action.Equals(TEXT("gpu")) && !Action.Equals(TEXT("ticks")) && !Action.Equals(TEXT("memory")))
    {
        return MakeProfilingError(
            TEXT("PROFILING_ACTION_UNSUPPORTED"),
            FString::Printf(TEXT("profiling.action '%s' is not implemented yet."), *Action));
    }

    UWorld* World = ResolveProfilingWorld(Arguments);
    if (World == nullptr)
    {
        return MakeProfilingError(TEXT("WORLD_NOT_FOUND"), TEXT("The requested profiling world could not be resolved."));
    }

    UGameViewportClient* GameViewport = World->GetGameViewport();

    int32 GpuIndex = 0;
    if (Arguments.IsValid())
    {
        GpuIndex = ReadOptionalPositiveIntField(Arguments, TEXT("gpuIndex"), 0);
    }
    GpuIndex = FMath::Clamp(GpuIndex, 0, MAX_NUM_GPUS - 1);

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetBoolField(TEXT("isError"), false);
    Result->SetObjectField(TEXT("runtime"), BuildProfilingRuntimeContext(World, GameViewport));

    TSharedPtr<FJsonObject> Source = MakeShared<FJsonObject>();
    if (Action.Equals(TEXT("unit")))
    {
        if (GameViewport == nullptr)
        {
            return MakeProfilingError(TEXT("GAME_VIEWPORT_UNAVAILABLE"), TEXT("The requested world does not expose a game viewport."));
        }

        const FStatUnitData* StatUnitData = GameViewport->GetStatUnitData();
        if (StatUnitData == nullptr)
        {
            return MakeProfilingError(TEXT("STAT_UNIT_DATA_UNAVAILABLE"), TEXT("Official stat unit data is not available for the requested world."));
        }

        const bool bIncludeRaw = !Arguments.IsValid() || !Arguments->HasField(TEXT("includeRaw"))
            ? true
            : Arguments->GetBoolField(TEXT("includeRaw"));
        const bool bIncludeGpuUtilization = Arguments.IsValid() && Arguments->HasField(TEXT("includeGpuUtilization"))
            ? Arguments->GetBoolField(TEXT("includeGpuUtilization"))
            : true;
        const bool bIncludeHistory = Arguments.IsValid() && Arguments->HasField(TEXT("includeHistory"))
            ? Arguments->GetBoolField(TEXT("includeHistory"))
            : false;

        const bool bUnitStatsEnabledThisCall = EnsureProfilingUnitStatsEnabled(World, GameViewport);
        if (ProfilingUnitDataNeedsWarmup(StatUnitData, GpuIndex))
        {
            const FString WarmupMessage = bUnitStatsEnabledThisCall
                ? TEXT("Official stat unit metrics were just enabled and need a rendered frame before valid data is available.")
                : TEXT("Official stat unit metrics are still warming up. Retry after one or more rendered frames.");
            return MakeProfilingError(TEXT("STAT_UNIT_WARMUP_REQUIRED"), WarmupMessage);
        }

        Source->SetStringField(TEXT("officialCommand"), TEXT("stat unit"));
        Source->SetStringField(TEXT("backend"), TEXT("FStatUnitData"));
        Source->SetNumberField(TEXT("gpuIndex"), static_cast<double>(GpuIndex));
        Result->SetObjectField(TEXT("source"), Source);

        auto BuildUnitBlock = [&](const bool bRaw) -> TSharedPtr<FJsonObject>
        {
            TSharedPtr<FJsonObject> Block = MakeShared<FJsonObject>();
            const float FrameTime = bRaw ? StatUnitData->RawFrameTime : StatUnitData->FrameTime;
            const float GameThreadTime = bRaw ? StatUnitData->RawGameThreadTime : StatUnitData->GameThreadTime;
            const float GameThreadCriticalPath = bRaw ? StatUnitData->RawGameThreadTimeCriticalPath : StatUnitData->GameThreadTimeCriticalPath;
            const float RenderThreadTime = bRaw ? StatUnitData->RawRenderThreadTime : StatUnitData->RenderThreadTime;
            const float RenderThreadCriticalPath = bRaw ? StatUnitData->RawRenderThreadTimeCriticalPath : StatUnitData->RenderThreadTimeCriticalPath;
            const float GpuFrameTime = bRaw ? StatUnitData->RawGPUFrameTime[GpuIndex] : StatUnitData->GPUFrameTime[GpuIndex];
            const float RhiThreadTime = bRaw ? StatUnitData->RawRHITTime : StatUnitData->RHITTime;
            const float InputLatencyTime = bRaw ? StatUnitData->RawInputLatencyTime : StatUnitData->InputLatencyTime;

            Block->SetNumberField(TEXT("frameTimeMs"), static_cast<double>(FrameTime));
            Block->SetNumberField(TEXT("gameThreadTimeMs"), static_cast<double>(GameThreadTime));
            Block->SetNumberField(TEXT("gameThreadCriticalPathMs"), static_cast<double>(GameThreadCriticalPath));
            Block->SetNumberField(TEXT("renderThreadTimeMs"), static_cast<double>(RenderThreadTime));
            Block->SetNumberField(TEXT("renderThreadCriticalPathMs"), static_cast<double>(RenderThreadCriticalPath));
            Block->SetNumberField(TEXT("gpuFrameTimeMs"), static_cast<double>(GpuFrameTime));
            Block->SetNumberField(TEXT("rhiThreadTimeMs"), static_cast<double>(RhiThreadTime));
            Block->SetNumberField(TEXT("inputLatencyTimeMs"), static_cast<double>(InputLatencyTime));

            if (bIncludeGpuUtilization)
            {
                const float GpuClockFraction = bRaw ? StatUnitData->RawGPUClockFraction[GpuIndex] : StatUnitData->GPUClockFraction[GpuIndex];
                const float GpuUsageFraction = bRaw ? StatUnitData->RawGPUUsageFraction[GpuIndex] : StatUnitData->GPUUsageFraction[GpuIndex];
                const uint64 GpuMemoryUsage = bRaw ? StatUnitData->RawGPUMemoryUsage[GpuIndex] : StatUnitData->GPUMemoryUsage[GpuIndex];
                const float GpuExternalUsageFraction = bRaw ? StatUnitData->RawGPUExternalUsageFraction[GpuIndex] : StatUnitData->GPUExternalUsageFraction[GpuIndex];
                const uint64 GpuExternalMemoryUsage = bRaw ? StatUnitData->RawGPUExternalMemoryUsage[GpuIndex] : StatUnitData->GPUExternalMemoryUsage[GpuIndex];

                Block->SetNumberField(TEXT("gpuClockFraction"), static_cast<double>(GpuClockFraction));
                Block->SetNumberField(TEXT("gpuUsageFraction"), static_cast<double>(GpuUsageFraction));
                Block->SetNumberField(TEXT("gpuMemoryBytes"), static_cast<double>(GpuMemoryUsage));
                Block->SetNumberField(TEXT("gpuExternalUsageFraction"), static_cast<double>(GpuExternalUsageFraction));
                Block->SetNumberField(TEXT("gpuExternalMemoryBytes"), static_cast<double>(GpuExternalMemoryUsage));
            }

            return Block;
        };

        TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
        Data->SetObjectField(TEXT("average"), BuildUnitBlock(false));
        if (bIncludeRaw)
        {
            Data->SetObjectField(TEXT("raw"), BuildUnitBlock(true));
        }

#if !UE_BUILD_SHIPPING
        if (bIncludeHistory)
        {
            TSharedPtr<FJsonObject> History = MakeShared<FJsonObject>();
            History->SetNumberField(TEXT("numberOfSamples"), static_cast<double>(FStatUnitData::NumberOfSamples));
            History->SetNumberField(TEXT("currentIndex"), static_cast<double>(StatUnitData->CurrentIndex));
            AddJsonNumberArrayField(History, TEXT("frameTimeMs"), StatUnitData->FrameTimes);
            AddJsonNumberArrayField(History, TEXT("gameThreadTimeMs"), StatUnitData->GameThreadTimes);
            AddJsonNumberArrayField(History, TEXT("renderThreadTimeMs"), StatUnitData->RenderThreadTimes);
            AddJsonNumberArrayField(History, TEXT("gpuFrameTimeMs"), StatUnitData->GPUFrameTimes[GpuIndex]);
            AddJsonNumberArrayField(History, TEXT("rhiThreadTimeMs"), StatUnitData->RHITTimes);
            AddJsonNumberArrayField(History, TEXT("inputLatencyTimeMs"), StatUnitData->InputLatencyTimes);
            Data->SetObjectField(TEXT("history"), History);
        }
#endif

        Result->SetObjectField(TEXT("data"), Data);
        return Result;
    }

    if (Action.Equals(TEXT("ticks")))
    {
        FString Mode = Arguments.IsValid() && Arguments->HasField(TEXT("mode"))
            ? Arguments->GetStringField(TEXT("mode"))
            : TEXT("all");
        Mode = Mode.TrimStartAndEnd().ToLower();
        if (!Mode.Equals(TEXT("all")) && !Mode.Equals(TEXT("grouped")) && !Mode.Equals(TEXT("enabled")) && !Mode.Equals(TEXT("disabled")))
        {
            return MakeProfilingError(TEXT("INVALID_ARGUMENT"), TEXT("profiling.mode must be one of: all, grouped, enabled, disabled."));
        }

        Source->SetStringField(
            TEXT("officialCommand"),
            Mode.Equals(TEXT("all")) ? TEXT("dumpticks") : FString::Printf(TEXT("dumpticks %s"), *Mode));
        Source->SetStringField(TEXT("backend"), TEXT("FTickTaskManagerInterface"));
        Result->SetObjectField(TEXT("source"), Source);

        TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
        Data->SetStringField(TEXT("mode"), Mode);

#if STATS
        if (Mode.Equals(TEXT("grouped")))
        {
            TSortedMap<FName, int32, FDefaultAllocator, FNameFastLess> TickContextToCountMap;
            int32 EnabledCount = 0;
            FTickTaskManagerInterface::Get().GetEnabledTickFunctionCounts(World, TickContextToCountMap, EnabledCount, true);

            struct FTickContextCountRow
            {
                FName Context;
                int32 Count = 0;
            };

            TArray<FTickContextCountRow> SortedRows;
            SortedRows.Reserve(TickContextToCountMap.Num());
            for (auto It = TickContextToCountMap.CreateConstIterator(); It; ++It)
            {
                FTickContextCountRow& Row = SortedRows.AddDefaulted_GetRef();
                Row.Context = It->Key;
                Row.Count = It->Value;
            }
            SortedRows.Sort([](const FTickContextCountRow& A, const FTickContextCountRow& B)
            {
                return A.Count == B.Count ? A.Context.LexicalLess(B.Context) : A.Count > B.Count;
            });

            TSharedPtr<FJsonObject> Table = MakeShared<FJsonObject>();
            TArray<TSharedPtr<FJsonValue>> Columns;
            {
                TSharedPtr<FJsonObject> ContextColumn = MakeShared<FJsonObject>();
                ContextColumn->SetStringField(TEXT("key"), TEXT("context"));
                ContextColumn->SetStringField(TEXT("label"), TEXT("Context"));
                Columns.Add(MakeShared<FJsonValueObject>(ContextColumn));
                TSharedPtr<FJsonObject> CountColumn = MakeShared<FJsonObject>();
                CountColumn->SetStringField(TEXT("key"), TEXT("count"));
                CountColumn->SetStringField(TEXT("label"), TEXT("Count"));
                Columns.Add(MakeShared<FJsonValueObject>(CountColumn));
            }
            Table->SetArrayField(TEXT("columns"), Columns);

            TArray<TSharedPtr<FJsonValue>> Rows;
            Rows.Reserve(SortedRows.Num());
            for (const FTickContextCountRow& Row : SortedRows)
            {
                TSharedPtr<FJsonObject> RowObject = MakeShared<FJsonObject>();
                RowObject->SetStringField(TEXT("context"), Row.Context.ToString());
                RowObject->SetNumberField(TEXT("count"), static_cast<double>(Row.Count));
                Rows.Add(MakeShared<FJsonValueObject>(RowObject));
            }
            Table->SetArrayField(TEXT("rows"), Rows);
            Data->SetObjectField(TEXT("grouped"), Table);
            Data->SetNumberField(TEXT("enabledCount"), static_cast<double>(EnabledCount));
            Result->SetObjectField(TEXT("data"), Data);
            return Result;
        }

        FProfilingOutputDevice Collector;
        const bool bEnabled = !Mode.Equals(TEXT("disabled"));
        const bool bDisabled = !Mode.Equals(TEXT("enabled"));
        FTickTaskManagerInterface::Get().DumpAllTickFunctions(Collector, World, bEnabled, bDisabled, false);

        TSharedPtr<FJsonObject> Table = BuildProfilingTicksColumns();
        TArray<TSharedPtr<FJsonValue>> Rows;
        int32 EnabledCount = 0;
        int32 DisabledCount = 0;
        int32 TotalCount = 0;
        TSharedPtr<FJsonObject> CurrentRow;

        for (const FString& Line : Collector.Lines)
        {
            const FString Trimmed = Line.TrimStartAndEnd();
            if (Trimmed.IsEmpty() || Trimmed.StartsWith(TEXT("============================")))
            {
                continue;
            }

            int32 SummaryTotal = 0;
            int32 SummaryEnabled = 0;
            int32 SummaryDisabled = 0;
            if (Trimmed.StartsWith(TEXT("Total registered tick functions: "))
                && FParse::Value(*Trimmed, TEXT("Total registered tick functions: "), SummaryTotal)
                && FParse::Value(*Trimmed, TEXT("enabled: "), SummaryEnabled)
                && FParse::Value(*Trimmed, TEXT("disabled: "), SummaryDisabled))
            {
                TotalCount = SummaryTotal;
                EnabledCount = SummaryEnabled;
                DisabledCount = SummaryDisabled;
                continue;
            }

            if (Line.StartsWith(TEXT("    ")))
            {
                if (CurrentRow.IsValid())
                {
                    TArray<TSharedPtr<FJsonValue>> Prerequisites;
                    const TArray<TSharedPtr<FJsonValue>>* Existing = nullptr;
                    if (CurrentRow->TryGetArrayField(TEXT("prerequisites"), Existing) && Existing != nullptr)
                    {
                        Prerequisites = *Existing;
                    }
                    if (TSharedPtr<FJsonObject> ParsedPrerequisite = ParseProfilingTickPrerequisite(Line))
                    {
                        Prerequisites.Add(MakeShared<FJsonValueObject>(ParsedPrerequisite));
                    }
                    CurrentRow->SetArrayField(TEXT("prerequisites"), Prerequisites);
                }
                continue;
            }

            CurrentRow = ParseProfilingTickRow(Trimmed);
            if (CurrentRow.IsValid())
            {
                Rows.Add(MakeShared<FJsonValueObject>(CurrentRow));
            }
        }

        Table->SetArrayField(TEXT("rows"), Rows);
        Data->SetObjectField(TEXT("table"), Table);
        Data->SetNumberField(TEXT("enabledCount"), static_cast<double>(EnabledCount));
        Data->SetNumberField(TEXT("disabledCount"), static_cast<double>(DisabledCount));
        Data->SetNumberField(TEXT("totalCount"), static_cast<double>(TotalCount));
        Result->SetObjectField(TEXT("data"), Data);
        return Result;
#else
        return MakeProfilingError(TEXT("TICKS_DATA_UNAVAILABLE"), TEXT("Tick profiling data is not available in this build."));
#endif
    }

    if (Action.Equals(TEXT("memory")))
    {
        FString Kind = Arguments.IsValid() && Arguments->HasField(TEXT("kind"))
            ? Arguments->GetStringField(TEXT("kind"))
            : TEXT("summary");
        Kind = Kind.TrimStartAndEnd().ToLower();
        if (Kind.IsEmpty())
        {
            Kind = TEXT("summary");
        }
        if (!Kind.Equals(TEXT("summary")))
        {
            return MakeProfilingError(TEXT("INVALID_ARGUMENT"), TEXT("profiling.kind for action=memory must currently be 'summary'."));
        }

        Source->SetStringField(TEXT("officialCommand"), TEXT("memreport"));
        Source->SetStringField(TEXT("backend"), TEXT("FPlatformMemory::GetStats + FMalloc::GetAllocatorStats + RHIGetTextureMemoryStats"));
        Result->SetObjectField(TEXT("source"), Source);

        const FPlatformMemoryStats PlatformStats = FPlatformMemory::GetStats();

        TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
        Data->SetStringField(TEXT("kind"), TEXT("summary"));

        TSharedPtr<FJsonObject> Platform = MakeShared<FJsonObject>();
        Platform->SetStringField(TEXT("memoryPressureStatus"), LoomleMemoryPressureStatusToString(PlatformStats.GetMemoryPressureStatus()));
        Platform->SetStringField(TEXT("memorySizeBucket"), LexToString(FPlatformMemory::GetMemorySizeBucket()));
        Platform->SetStringField(TEXT("allocator"), LoomleMemoryAllocatorToString(FPlatformMemory::AllocatorToUse));
        Platform->SetNumberField(TEXT("totalPhysicalBytes"), static_cast<double>(PlatformStats.TotalPhysical));
        Platform->SetNumberField(TEXT("availablePhysicalBytes"), static_cast<double>(PlatformStats.AvailablePhysical));
        Platform->SetNumberField(TEXT("usedPhysicalBytes"), static_cast<double>(PlatformStats.UsedPhysical));
        Platform->SetNumberField(TEXT("peakUsedPhysicalBytes"), static_cast<double>(PlatformStats.PeakUsedPhysical));
        Platform->SetNumberField(TEXT("totalVirtualBytes"), static_cast<double>(PlatformStats.TotalVirtual));
        Platform->SetNumberField(TEXT("availableVirtualBytes"), static_cast<double>(PlatformStats.AvailableVirtual));
        Platform->SetNumberField(TEXT("usedVirtualBytes"), static_cast<double>(PlatformStats.UsedVirtual));
        Platform->SetNumberField(TEXT("peakUsedVirtualBytes"), static_cast<double>(PlatformStats.PeakUsedVirtual));
        Platform->SetNumberField(TEXT("pageSizeBytes"), static_cast<double>(PlatformStats.PageSize));
        Platform->SetNumberField(TEXT("osAllocationGranularityBytes"), static_cast<double>(PlatformStats.OsAllocationGranularity));
        Platform->SetNumberField(TEXT("binnedPageSizeBytes"), static_cast<double>(PlatformStats.BinnedPageSize));
        Platform->SetNumberField(TEXT("binnedAllocationGranularityBytes"), static_cast<double>(PlatformStats.BinnedAllocationGranularity));
        Platform->SetArrayField(TEXT("platformSpecificStats"), BuildProfilingPlatformSpecificMemoryRows(PlatformStats.GetPlatformSpecificStats()));
        Data->SetObjectField(TEXT("platform"), Platform);

        TSharedPtr<FJsonObject> Allocator = MakeShared<FJsonObject>();
        if (UE::Private::GMalloc != nullptr)
        {
            FGenericMemoryStats AllocatorStats;
            UE::Private::GMalloc->GetAllocatorStats(AllocatorStats);
            Allocator->SetArrayField(TEXT("rows"), BuildProfilingGenericMemoryStatRows(AllocatorStats));
        }
        else
        {
            Allocator->SetArrayField(TEXT("rows"), TArray<TSharedPtr<FJsonValue>>{});
        }
        Data->SetObjectField(TEXT("allocator"), Allocator);

        TSharedPtr<FJsonObject> TextureMemory = MakeShared<FJsonObject>();
        const bool bTextureMemoryAvailable = GDynamicRHI != nullptr;
        TextureMemory->SetBoolField(TEXT("available"), bTextureMemoryAvailable);
        if (bTextureMemoryAvailable)
        {
            FTextureMemoryStats TextureMemoryStats;
            RHIGetTextureMemoryStats(TextureMemoryStats);
            TextureMemory->SetBoolField(TEXT("hardwareStatsValid"), TextureMemoryStats.AreHardwareStatsValid());
            TextureMemory->SetBoolField(TEXT("usingLimitedPoolSize"), TextureMemoryStats.IsUsingLimitedPoolSize());
            TextureMemory->SetNumberField(TEXT("dedicatedVideoMemoryBytes"), static_cast<double>(TextureMemoryStats.DedicatedVideoMemory));
            TextureMemory->SetNumberField(TEXT("dedicatedSystemMemoryBytes"), static_cast<double>(TextureMemoryStats.DedicatedSystemMemory));
            TextureMemory->SetNumberField(TEXT("sharedSystemMemoryBytes"), static_cast<double>(TextureMemoryStats.SharedSystemMemory));
            TextureMemory->SetNumberField(TEXT("totalGraphicsMemoryBytes"), static_cast<double>(TextureMemoryStats.TotalGraphicsMemory));
            TextureMemory->SetNumberField(TEXT("totalDeviceWorkingMemoryBytes"), static_cast<double>(TextureMemoryStats.GetTotalDeviceWorkingMemory()));
            TextureMemory->SetNumberField(TEXT("streamingMemorySizeBytes"), static_cast<double>(TextureMemoryStats.StreamingMemorySize));
            TextureMemory->SetNumberField(TEXT("nonStreamingMemorySizeBytes"), static_cast<double>(TextureMemoryStats.NonStreamingMemorySize));
            TextureMemory->SetNumberField(TEXT("largestContiguousAllocationBytes"), static_cast<double>(TextureMemoryStats.LargestContiguousAllocation));
            TextureMemory->SetNumberField(TEXT("texturePoolSizeBytes"), static_cast<double>(TextureMemoryStats.TexturePoolSize));
            TextureMemory->SetNumberField(TEXT("availableStreamingMemoryBytes"), static_cast<double>(TextureMemoryStats.ComputeAvailableMemorySize()));
        }
        Data->SetObjectField(TEXT("textureMemory"), TextureMemory);

        Result->SetObjectField(TEXT("data"), Data);
        return Result;
    }

#if STATS
    if (GameViewport == nullptr)
    {
        return MakeProfilingError(TEXT("GAME_VIEWPORT_UNAVAILABLE"), TEXT("The requested world does not expose a game viewport."));
    }

    if (Action.Equals(TEXT("gpu")))
    {
#if !RHI_NEW_GPU_PROFILER
        return MakeProfilingError(TEXT("GPU_PROFILER_UNAVAILABLE"), TEXT("Official stat gpu data is not available in this build."));
#else
        const bool bGpuStatsEnabledThisCall = EnsureProfilingGpuStatsEnabled(World, GameViewport);
        FGameThreadStatsData* StatsData = FLatestGameThreadStatsData::Get().Latest;
        if (StatsData == nullptr)
        {
            const FString WarmupMessage = bGpuStatsEnabledThisCall
                ? TEXT("Official stat gpu metrics were just enabled and need one or more frames before aggregated data is available.")
                : TEXT("Official stat gpu metrics are not available yet. Retry after one or more rendered frames.");
            return MakeProfilingError(TEXT("STATS_GROUP_WARMUP_REQUIRED"), WarmupMessage);
        }

        TArray<TSharedPtr<FJsonValue>> GpuGroups;
        for (int32 Index = 0; Index < StatsData->ActiveStatGroups.Num() && Index < StatsData->GroupNames.Num(); ++Index)
        {
            const FActiveStatGroupInfo& GroupInfo = StatsData->ActiveStatGroups[Index];
            if (GroupInfo.GpuStatsAggregate.IsEmpty())
            {
                continue;
            }

            TSharedPtr<FJsonObject> GroupObject = MakeShared<FJsonObject>();
            GroupObject->SetStringField(TEXT("groupName"), StatsData->GroupNames[Index].ToString());
            if (StatsData->GroupDescriptions.IsValidIndex(Index))
            {
                GroupObject->SetStringField(TEXT("groupDescription"), StatsData->GroupDescriptions[Index]);
            }

            TSharedPtr<FJsonObject> Table = BuildProfilingGpuColumns();
            Table->SetArrayField(TEXT("rows"), BuildProfilingGpuRows(GroupInfo.GpuStatsAggregate));
            GroupObject->SetObjectField(TEXT("table"), Table);
            GpuGroups.Add(MakeShared<FJsonValueObject>(GroupObject));
        }

        if (GpuGroups.IsEmpty())
        {
            const FString WarmupMessage = bGpuStatsEnabledThisCall
                ? TEXT("Official stat gpu metrics were just enabled and need one or more frames before aggregated data is available.")
                : TEXT("Official stat gpu metrics are not available yet. Retry after one or more rendered frames.");
            return MakeProfilingError(TEXT("STATS_GROUP_WARMUP_REQUIRED"), WarmupMessage);
        }

        Source->SetStringField(TEXT("officialCommand"), TEXT("stat gpu"));
        Source->SetStringField(TEXT("backend"), TEXT("FLatestGameThreadStatsData.ActiveStatGroups[].GpuStatsAggregate"));
        Source->SetStringField(TEXT("action"), Action);
        Result->SetObjectField(TEXT("source"), Source);

        TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
        Data->SetStringField(TEXT("family"), Action);
        Data->SetArrayField(TEXT("groups"), GpuGroups);
        Result->SetObjectField(TEXT("data"), Data);
        return Result;
#endif
    }

    const FString RequestedGroup = Action.Equals(TEXT("gpu"))
        ? TEXT("gpu")
        : (Arguments.IsValid() && Arguments->HasField(TEXT("group"))
            ? Arguments->GetStringField(TEXT("group"))
            : TEXT("game"));
    const bool bIncludeThreadBreakdown = Arguments.IsValid() && Arguments->HasField(TEXT("includeThreadBreakdown"))
        ? Arguments->GetBoolField(TEXT("includeThreadBreakdown"))
        : false;
    FString DisplayMode = Arguments.IsValid() && Arguments->HasField(TEXT("displayMode"))
        ? Arguments->GetStringField(TEXT("displayMode"))
        : TEXT("both");
    DisplayMode = DisplayMode.TrimStartAndEnd().ToLower();
    if (!DisplayMode.Equals(TEXT("hierarchical")) && !DisplayMode.Equals(TEXT("flat")) && !DisplayMode.Equals(TEXT("both")))
    {
        DisplayMode = TEXT("both");
    }
    const int32 MaxDepth = Arguments.IsValid() && Arguments->HasField(TEXT("maxDepth"))
        ? FMath::Max(-1, ReadOptionalPositiveIntField(Arguments, TEXT("maxDepth"), -1))
        : -1;

    const bool bGroupEnabledThisCall = EnsureProfilingStatsGroupEnabled(World, GameViewport, RequestedGroup);
    FGameThreadStatsData* StatsData = FLatestGameThreadStatsData::Get().Latest;
    FString ResolvedGroupName;
    FString GroupDescription;
    const FActiveStatGroupInfo* GroupInfo = FindProfilingActiveStatGroup(StatsData, RequestedGroup, ResolvedGroupName, GroupDescription);
    if (StatsData == nullptr || GroupInfo == nullptr)
    {
        const FString WarmupMessage = bGroupEnabledThisCall
            ? FString::Printf(TEXT("Official stat group '%s' was just enabled and needs one or more frames before aggregated data is available."), *NormalizeProfilingStatGroupName(RequestedGroup))
            : FString::Printf(TEXT("Official stat group '%s' is not available yet. Retry after one or more rendered frames."), *NormalizeProfilingStatGroupName(RequestedGroup));
        return MakeProfilingError(TEXT("STATS_GROUP_WARMUP_REQUIRED"), WarmupMessage);
    }

    Source->SetStringField(
        TEXT("officialCommand"),
        Action.Equals(TEXT("gpu"))
            ? TEXT("stat gpu")
            : FString::Printf(TEXT("stat %s"), *NormalizeProfilingStatGroupName(RequestedGroup)));
    Source->SetStringField(TEXT("backend"), TEXT("FLatestGameThreadStatsData"));
    Source->SetStringField(TEXT("groupName"), ResolvedGroupName);
    Source->SetStringField(TEXT("action"), Action);
    Result->SetObjectField(TEXT("source"), Source);

    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetStringField(TEXT("groupName"), ResolvedGroupName);
    Data->SetStringField(TEXT("requestedGroup"), NormalizeProfilingStatGroupName(RequestedGroup));
    Data->SetStringField(TEXT("family"), Action);
    Data->SetStringField(TEXT("groupDescription"), GroupDescription);
    Data->SetStringField(TEXT("displayMode"), DisplayMode);

    if (!GroupInfo->ThreadBudgetMap.IsEmpty())
    {
        TSharedPtr<FJsonObject> ThreadBudgets = MakeShared<FJsonObject>();
        for (const TPair<FName, float>& Pair : GroupInfo->ThreadBudgetMap)
        {
            ThreadBudgets->SetNumberField(Pair.Key.ToString(), static_cast<double>(Pair.Value));
        }
        Data->SetObjectField(TEXT("threadBudgetsMs"), ThreadBudgets);
    }

    if (DisplayMode.Equals(TEXT("both")) || DisplayMode.Equals(TEXT("hierarchical")))
    {
        TSharedPtr<FJsonObject> Hierarchical = BuildProfilingCycleColumns();
        Hierarchical->SetArrayField(TEXT("rows"), BuildProfilingComplexStatRows(GroupInfo->HierAggregate, &GroupInfo->Indentation, MaxDepth));
        Data->SetObjectField(TEXT("hierarchical"), Hierarchical);
    }

    if (DisplayMode.Equals(TEXT("both")) || DisplayMode.Equals(TEXT("flat")))
    {
        TSharedPtr<FJsonObject> Flat = BuildProfilingCycleColumns();
        Flat->SetArrayField(TEXT("rows"), BuildProfilingComplexStatRows(GroupInfo->FlatAggregate, nullptr, -1));
        Data->SetObjectField(TEXT("flat"), Flat);

        if (bIncludeThreadBreakdown)
        {
            TArray<TSharedPtr<FJsonValue>> ThreadGroups;
            for (const TPair<FName, TArray<FComplexStatMessage>>& Pair : GroupInfo->FlatAggregateThreadBreakdown)
            {
                TSharedPtr<FJsonObject> ThreadGroup = BuildProfilingCycleColumns();
                ThreadGroup->SetStringField(TEXT("threadName"), Pair.Key.ToString());
                ThreadGroup->SetArrayField(TEXT("rows"), BuildProfilingComplexStatRows(Pair.Value, nullptr, -1));
                ThreadGroups.Add(MakeShared<FJsonValueObject>(ThreadGroup));
            }
            Data->SetArrayField(TEXT("threadBreakdown"), ThreadGroups);
        }
    }

    Result->SetObjectField(TEXT("data"), Data);
    return Result;
#else
    return MakeProfilingError(TEXT("STATS_GROUP_UNAVAILABLE"), TEXT("Stats aggregation is not available in this build."));
#endif
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

    const TSharedPtr<FJsonObject>* ExecutionPtr = nullptr;
    if (Arguments.IsValid() && Arguments->TryGetObjectField(TEXT("execution"), ExecutionPtr) && ExecutionPtr && (*ExecutionPtr).IsValid())
    {
        FString ExecutionMode;
        (*ExecutionPtr)->TryGetStringField(TEXT("mode"), ExecutionMode);
        if (ExecutionMode.Equals(TEXT("job"), ESearchCase::IgnoreCase))
        {
            FString IdempotencyKey;
            (*ExecutionPtr)->TryGetStringField(TEXT("idempotencyKey"), IdempotencyKey);
            IdempotencyKey = IdempotencyKey.TrimStartAndEnd();
            if (IdempotencyKey.IsEmpty())
            {
                Result->SetBoolField(TEXT("isError"), true);
                Result->SetStringField(TEXT("code"), TEXT("IDEMPOTENCY_KEY_REQUIRED"));
                Result->SetStringField(TEXT("message"), TEXT("execution.idempotencyKey is required when execution.mode is 'job'."));
                return Result;
            }

            const TSharedPtr<FJsonObject> BusinessArguments = CloneJobBusinessArguments(Arguments);
            const FString RequestFingerprint = NormalizeJobFingerprint(LoomleBridgeConstants::ExecuteToolName, BusinessArguments);
            FString ExistingJobId;
            {
                FScopeLock Lock(&JobRegistryMutex);
                for (const TPair<FString, FToolJobEntry>& Pair : JobRegistry)
                {
                    const FToolJobEntry& ExistingEntry = Pair.Value;
                    if (!ExistingEntry.ToolName.Equals(LoomleBridgeConstants::ExecuteToolName)
                        || !ExistingEntry.IdempotencyKey.Equals(IdempotencyKey))
                    {
                        continue;
                    }

                    if (!ExistingEntry.RequestFingerprint.Equals(RequestFingerprint))
                    {
                        Result->SetBoolField(TEXT("isError"), true);
                        Result->SetStringField(TEXT("code"), TEXT("INVALID_EXECUTION_ENVELOPE"));
                        Result->SetStringField(TEXT("message"), TEXT("execution.idempotencyKey is already associated with a different execute payload."));
                        return Result;
                    }

                    ExistingJobId = ExistingEntry.JobId;
                    break;
                }

                if (ExistingJobId.IsEmpty())
                {
                    FToolJobEntry NewEntry;
                    NewEntry.JobId = FString::Printf(TEXT("job_%llu"), static_cast<unsigned long long>(NextJobId++));
                    NewEntry.ToolName = LoomleBridgeConstants::ExecuteToolName;
                    NewEntry.Status = TEXT("queued");
                    NewEntry.RequestFingerprint = RequestFingerprint;
                    NewEntry.IdempotencyKey = IdempotencyKey;
                    NewEntry.BusinessArguments = BusinessArguments;
                    NewEntry.AcceptedAt = FDateTime::UtcNow();
                    NewEntry.PollAfterMs = FMath::Clamp(ReadOptionalPositiveIntField(*ExecutionPtr, TEXT("waitMs"), 1000), 1, 60000);
                    NewEntry.ResultTtlMs = FMath::Clamp(ReadOptionalPositiveIntField(*ExecutionPtr, TEXT("resultTtlMs"), 3600000), 1, 86400000);
                    (*ExecutionPtr)->TryGetStringField(TEXT("label"), NewEntry.Label);
                    ExistingJobId = NewEntry.JobId;
                    JobRegistry.Add(NewEntry.JobId, MoveTemp(NewEntry));
                    JobQueue.Add(ExistingJobId);
                }
            }

            StartNextJobIfNeeded();

            FScopeLock Lock(&JobRegistryMutex);
            const FToolJobEntry* AcceptedEntry = JobRegistry.Find(ExistingJobId);
            if (AcceptedEntry == nullptr)
            {
                Result->SetBoolField(TEXT("isError"), true);
                Result->SetStringField(TEXT("code"), TEXT("JOB_RUNTIME_UNAVAILABLE"));
                Result->SetStringField(TEXT("message"), TEXT("The shared jobs runtime could not accept the request."));
                return Result;
            }

            TSharedPtr<FJsonObject> JobObject = MakeShared<FJsonObject>();
            JobObject->SetStringField(TEXT("jobId"), AcceptedEntry->JobId);
            JobObject->SetStringField(TEXT("status"), AcceptedEntry->Status);
            JobObject->SetStringField(TEXT("acceptedAt"), LoomleJobIso8601OrEmpty(AcceptedEntry->AcceptedAt));
            JobObject->SetStringField(TEXT("idempotencyKey"), AcceptedEntry->IdempotencyKey);
            JobObject->SetNumberField(TEXT("pollAfterMs"), AcceptedEntry->PollAfterMs);
            Result->SetBoolField(TEXT("isError"), false);
            Result->SetObjectField(TEXT("job"), JobObject);
            return Result;
        }
    }

    FString Code;
    if (!Arguments->TryGetStringField(TEXT("code"), Code))
    {
        Result->SetBoolField(TEXT("isError"), true);
        Result->SetStringField(TEXT("code"), TEXT("INVALID_ARGUMENT"));
        Result->SetStringField(TEXT("message"), TEXT("arguments.code is required."));
        AppendDiagnosticEvent(TEXT("error"), TEXT("python"), TEXT("execute"), TEXT("arguments.code is required."));
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
        AppendDiagnosticEvent(TEXT("error"), TEXT("python"), TEXT("execute"), TEXT("arguments.mode must be 'exec' or 'eval'."));
        return Result;
    }

    IPythonScriptPlugin* PythonScriptPlugin = IPythonScriptPlugin::Get();
    if (PythonScriptPlugin == nullptr)
    {
        Result->SetBoolField(TEXT("isError"), true);
        Result->SetStringField(TEXT("code"), TEXT("INTERNAL_ERROR"));
        Result->SetStringField(TEXT("message"), TEXT("PythonScriptPlugin module is not loaded."));
        AppendDiagnosticEvent(TEXT("error"), TEXT("python"), TEXT("execute"), TEXT("PythonScriptPlugin module is not loaded."));
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
            AppendDiagnosticEvent(TEXT("error"), TEXT("python"), TEXT("execute"), TEXT("Python runtime is not initialized."));
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
    Result->SetObjectField(TEXT("runtime"), BuildExecuteRuntimeContext());

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
        AppendDiagnosticEvent(TEXT("error"), TEXT("python"), TEXT("execute"), TEXT("Python execute failed."), Context);
    }

    return Result;
}
