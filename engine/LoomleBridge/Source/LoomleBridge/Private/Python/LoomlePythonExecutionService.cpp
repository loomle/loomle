// Copyright 2026 Loomle contributors.

#include "Python/LoomlePythonExecutionService.h"

#include "../Sal/SalProjectionService.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Editor.h"
#include "Async/TaskGraphInterfaces.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformTime.h"
#include "IPythonScriptPlugin.h"
#include "Misc/FileHelper.h"
#include "Misc/Guid.h"
#include "Misc/Paths.h"
#include "Misc/ScopeLock.h"
#include "Modules/ModuleManager.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "PythonScriptTypes.h"
#include "LoomleGameThreadAdmission.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

namespace Loomle::Python
{
namespace
{
constexpr int32 MaxScriptCharacters = 262144;
constexpr int64 MaxResultUtf8Bytes = 1024 * 1024;
constexpr int64 MaxRunnerDocumentUtf8Bytes = 16 * 1024 * 1024;
constexpr int32 MaxLogEntries = 1000;
constexpr int64 MaxLogUtf8Bytes = 256 * 1024;
constexpr double TerminalRetentionSeconds = 30.0 * 60.0;
constexpr int32 PollAfterMs = 1000;

using FCondensedJsonWriter = TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>;

enum class EExecutionState : uint8
{
    Prepared,
    Running,
    Terminal,
};

TSharedPtr<FJsonObject> MakeDispatchError(const FString& Code, const FString& Message)
{
    TSharedPtr<FJsonObject> Error = MakeShared<FJsonObject>();
    Error->SetBoolField(TEXT("isError"), true);
    Error->SetStringField(TEXT("code"), Code);
    Error->SetStringField(TEXT("message"), Message);
    return Error;
}

FString SerializeObject(const TSharedPtr<FJsonObject>& Object)
{
    FString Output;
    const TSharedRef<FCondensedJsonWriter> Writer =
        TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Output);
    if (!Object.IsValid() || !FJsonSerializer::Serialize(Object.ToSharedRef(), Writer))
    {
        return FString();
    }
    return Output;
}

TSharedPtr<FJsonObject> ParseObject(const FString& Text)
{
    TSharedPtr<FJsonObject> Object;
    const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Text);
    return FJsonSerializer::Deserialize(Reader, Object) && Object.IsValid()
        ? Object
        : nullptr;
}

FString JsonStringLiteral(const FString& Value)
{
    TSharedPtr<FJsonObject> Wrapper = MakeShared<FJsonObject>();
    Wrapper->SetStringField(TEXT("value"), Value);
    const FString ObjectJson = SerializeObject(Wrapper);
    const FString Prefix = TEXT("{\"value\":");
    if (!ObjectJson.StartsWith(Prefix) || !ObjectJson.EndsWith(TEXT("}")))
    {
        return TEXT("\"\"");
    }
    return ObjectJson.Mid(Prefix.Len(), ObjectJson.Len() - Prefix.Len() - 1);
}

int64 Utf8Bytes(const FString& Value)
{
    const FTCHARToUTF8 Utf8(*Value);
    return Utf8.Length();
}

FString TruncateUtf8HeadTail(const FString& Value, int64 MaxBytes, bool& bOutTruncated)
{
    if (Utf8Bytes(Value) <= MaxBytes)
    {
        return Value;
    }

    bOutTruncated = true;
    int32 HeadChars = Value.Len() / 2;
    int32 TailChars = Value.Len() - HeadChars;
    const FString Marker = TEXT("\n... Loomle log output truncated ...\n");
    while (HeadChars > 0 || TailChars > 0)
    {
        const FString Candidate = Value.Left(HeadChars) + Marker + Value.Right(TailChars);
        if (Utf8Bytes(Candidate) <= MaxBytes)
        {
            return Candidate;
        }
        if (HeadChars >= TailChars && HeadChars > 0)
        {
            HeadChars = FMath::Max(0, HeadChars - FMath::Max(1, HeadChars / 8));
        }
        else if (TailChars > 0)
        {
            TailChars = FMath::Max(0, TailChars - FMath::Max(1, TailChars / 8));
        }
    }
    return Marker.Left(static_cast<int32>(FMath::Min<int64>(Marker.Len(), MaxBytes)));
}

FString LogTypeName(EPythonLogOutputType Type)
{
    switch (Type)
    {
    case EPythonLogOutputType::Info:
        return TEXT("info");
    case EPythonLogOutputType::Warning:
        return TEXT("warning");
    case EPythonLogOutputType::Error:
        return TEXT("error");
    default:
        return TEXT("error");
    }
}

TArray<TSharedPtr<FJsonValue>> BuildLogs(
    const TArray<FPythonLogOutputEntry>& NativeLogs,
    bool& bOutTruncated)
{
    bOutTruncated = NativeLogs.Num() > MaxLogEntries;
    TArray<int32> Indices;
    if (NativeLogs.Num() <= MaxLogEntries)
    {
        for (int32 Index = 0; Index < NativeLogs.Num(); ++Index)
        {
            Indices.Add(Index);
        }
    }
    else
    {
        const int32 HeadCount = MaxLogEntries / 2;
        for (int32 Index = 0; Index < HeadCount; ++Index)
        {
            Indices.Add(Index);
        }
        for (int32 Index = NativeLogs.Num() - (MaxLogEntries - HeadCount); Index < NativeLogs.Num(); ++Index)
        {
            Indices.Add(Index);
        }
    }

    int64 RemainingBytes = MaxLogUtf8Bytes;
    TArray<TSharedPtr<FJsonValue>> Logs;
    Logs.Reserve(Indices.Num());
    for (int32 Position = 0; Position < Indices.Num(); ++Position)
    {
        if (RemainingBytes <= 0)
        {
            bOutTruncated = true;
            break;
        }
        const FPythonLogOutputEntry& Native = NativeLogs[Indices[Position]];
        const int32 RemainingEntries = Indices.Num() - Position;
        const int64 EntryBudget = FMath::Max<int64>(1, RemainingBytes / FMath::Max(1, RemainingEntries));
        bool bEntryTruncated = false;
        const FString Output = TruncateUtf8HeadTail(Native.Output, EntryBudget, bEntryTruncated);
        bOutTruncated |= bEntryTruncated;
        RemainingBytes = FMath::Max<int64>(0, RemainingBytes - Utf8Bytes(Output));

        TSharedPtr<FJsonObject> Log = MakeShared<FJsonObject>();
        Log->SetStringField(TEXT("type"), LogTypeName(Native.Type));
        Log->SetStringField(TEXT("output"), Output);
        Logs.Add(MakeShared<FJsonValueObject>(Log));
    }
    return Logs;
}

TSharedPtr<FJsonObject> MakeExecutionError(
    const FString& Code,
    const FString& Phase,
    const FString& Message,
    const FString& Traceback = FString())
{
    TSharedPtr<FJsonObject> Error = MakeShared<FJsonObject>();
    Error->SetStringField(TEXT("code"), Code);
    Error->SetStringField(TEXT("phase"), Phase);
    Error->SetStringField(TEXT("message"), Message.IsEmpty() ? Code : Message);
    if (!Traceback.IsEmpty())
    {
        Error->SetStringField(TEXT("traceback"), Traceback);
    }
    Error->SetBoolField(
        TEXT("retryable"),
        Code == TEXT("runtime.python_initializing"));
    return Error;
}

TSharedPtr<FJsonObject> MakeTerminalFailure(
    const FString& Code,
    const FString& Phase,
    const FString& Message,
    bool bStateMayHaveChanged,
    int64 DurationMs,
    const TArray<FPythonLogOutputEntry>* NativeLogs = nullptr,
    const FString& Traceback = FString())
{
    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetStringField(TEXT("status"), TEXT("failed"));
    Result->SetBoolField(TEXT("stateMayHaveChanged"), bStateMayHaveChanged);
    Result->SetObjectField(TEXT("error"), MakeExecutionError(Code, Phase, Message, Traceback));
    if (NativeLogs != nullptr)
    {
        bool bLogsTruncated = false;
        Result->SetArrayField(TEXT("logs"), BuildLogs(*NativeLogs, bLogsTruncated));
        Result->SetBoolField(TEXT("logsTruncated"), bLogsTruncated);
        Result->SetNumberField(TEXT("durationMs"), static_cast<double>(FMath::Max<int64>(0, DurationMs)));
    }
    return Result;
}

FString BuildRunnerSource(const FString& SourcePath, const FString& ResultPath)
{
    return FString::Printf(
        TEXT(
            "import inspect as _loomle_inspect\n"
            "import json as _loomle_json\n"
            "import math as _loomle_math\n"
            "import traceback as _loomle_traceback\n"
            "_LOOMLE_SOURCE = %s\n"
            "_LOOMLE_RESULT = %s\n"
            "_LOOMLE_SAFE_INTEGER = 9007199254740991\n"
            "class _LoomleResultError(Exception):\n"
            "    pass\n"
            "_LOOMLE_SAL_OBJECT_KEY = '__loomle_sal_object__'\n"
            "class _LoomleSal:\n"
            "    def object(self, value):\n"
            "        get_path = getattr(value, 'get_path_name', None)\n"
            "        if not callable(get_path):\n"
            "            raise _LoomleResultError('sal.object() requires a Unreal Engine object')\n"
            "        return {_LOOMLE_SAL_OBJECT_KEY: get_path()}\n"
            "_loomle_sal = _LoomleSal()\n"
            "def _loomle_validate(value, seen, path):\n"
            "    value_type = type(value)\n"
            "    if value is None or value_type is bool or value_type is str:\n"
            "        return\n"
            "    if value_type is int:\n"
            "        if value < -_LOOMLE_SAFE_INTEGER or value > _LOOMLE_SAFE_INTEGER:\n"
            "            raise _LoomleResultError(f'{path} contains an integer outside the JSON safe range')\n"
            "        return\n"
            "    if value_type is float:\n"
            "        if not _loomle_math.isfinite(value):\n"
            "            raise _LoomleResultError(f'{path} contains a non-finite number')\n"
            "        return\n"
            "    value_id = id(value)\n"
            "    if value_type is list:\n"
            "        if value_id in seen:\n"
            "            raise _LoomleResultError(f'{path} contains a cycle')\n"
            "        seen.add(value_id)\n"
            "        try:\n"
            "            for index, item in enumerate(value):\n"
            "                _loomle_validate(item, seen, f'{path}[{index}]')\n"
            "        finally:\n"
            "            seen.remove(value_id)\n"
            "        return\n"
            "    if value_type is dict:\n"
            "        if _LOOMLE_SAL_OBJECT_KEY in value:\n"
            "            raise _LoomleResultError(f'{path} uses the reserved sal.object() marker key')\n"
            "        if value_id in seen:\n"
            "            raise _LoomleResultError(f'{path} contains a cycle')\n"
            "        seen.add(value_id)\n"
            "        try:\n"
            "            for key, item in value.items():\n"
            "                if type(key) is not str:\n"
            "                    raise _LoomleResultError(f'{path} contains a non-string key')\n"
            "                _loomle_validate(item, seen, f'{path}.{key}')\n"
            "        finally:\n"
            "            seen.remove(value_id)\n"
            "        return\n"
            "    raise _LoomleResultError(f'{path} contains unsupported {value_type.__name__}')\n"
            "def _loomle_write(document):\n"
            "    with open(_LOOMLE_RESULT, 'w', encoding='utf-8', newline='\\n') as output:\n"
            "        _loomle_json.dump(document, output, ensure_ascii=False, allow_nan=False, separators=(',', ':'))\n"
            "try:\n"
            "    _loomle_namespace = {'__name__': '__loomle_execution__', '__file__': _LOOMLE_SOURCE, 'sal': _loomle_sal}\n"
            "    with open(_LOOMLE_SOURCE, 'r', encoding='utf-8') as source_file:\n"
            "        _loomle_code = compile(source_file.read(), _LOOMLE_SOURCE, 'exec')\n"
            "    exec(_loomle_code, _loomle_namespace, _loomle_namespace)\n"
            "    _loomle_entry = _loomle_namespace.get('run')\n"
            "    if not callable(_loomle_entry):\n"
            "        raise RuntimeError('Python script must define one callable run() entry point')\n"
            "    if _loomle_inspect.iscoroutinefunction(_loomle_entry) or _loomle_inspect.isgeneratorfunction(_loomle_entry):\n"
            "        raise RuntimeError('run() must be synchronous and must not be a generator')\n"
            "    if len(_loomle_inspect.signature(_loomle_entry).parameters) != 0:\n"
            "        raise RuntimeError('run() must accept no arguments')\n"
            "    _loomle_value = _loomle_entry()\n"
            "    if type(_loomle_value) is not dict:\n"
            "        raise _LoomleResultError('run() must return a dict')\n"
            "    _loomle_validate(_loomle_value, set(), 'result')\n"
            "    _loomle_write({'ok': True, 'result': _loomle_value})\n"
            "except BaseException as _loomle_exception:\n"
            "    _loomle_is_result_error = isinstance(_loomle_exception, _LoomleResultError)\n"
            "    _loomle_write({\n"
            "        'ok': False,\n"
            "        'code': 'runtime.python_invalid_result' if _loomle_is_result_error else 'runtime.python_execution_failed',\n"
            "        'phase': 'result' if _loomle_is_result_error else 'execution',\n"
            "        'message': str(_loomle_exception) or type(_loomle_exception).__name__,\n"
            "        'traceback': _loomle_traceback.format_exc(),\n"
            "    })\n"),
        *JsonStringLiteral(SourcePath),
        *JsonStringLiteral(ResultPath));
}

void DeleteExecutionFiles(const FString& SourcePath, const FString& RunnerPath, const FString& ResultPath)
{
    IFileManager::Get().Delete(*SourcePath, false, true);
    IFileManager::Get().Delete(*RunnerPath, false, true);
    IFileManager::Get().Delete(*ResultPath, false, true);
}
} // namespace

struct FPythonExecutionService::FExecution
{
    FString Id;
    FString SourcePath;
    FString RunnerPath;
    FString ResultPath;
    EExecutionState State = EExecutionState::Prepared;
    bool bExposed = false;
    double StartedAtSeconds = 0.0;
    double TerminalAtSeconds = 0.0;
    FString TerminalJson;
};

void FPythonExecutionService::Startup(TFunction<void()> InGameThreadProgress)
{
    check(IsInGameThread());
    {
        FScopeLock Lock(&Mutex);
        bShuttingDown = false;
        ExpiredExecutionIds.Reset();
        GameThreadProgress = MoveTemp(InGameThreadProgress);
    }
    if (IPythonScriptPlugin* Python = FModuleManager::LoadModulePtr<IPythonScriptPlugin>(TEXT("PythonScriptPlugin")))
    {
        Python->ForceEnablePythonAtRuntime();
    }
    ExecutionTickerHandle = FTSTicker::GetCoreTicker().AddTicker(
        TEXT("LoomlePythonExecution"),
        0.0f,
        [this](float DeltaTime)
        {
            return TickExecution(DeltaTime);
        });
}

void FPythonExecutionService::Shutdown()
{
    check(IsInGameThread());
    if (ExecutionTickerHandle.IsValid())
    {
        FTSTicker::RemoveTicker(ExecutionTickerHandle);
        ExecutionTickerHandle.Reset();
    }

    TSharedPtr<Loomle::Runtime::FGameThreadAdmission, ESPMode::ThreadSafe> Admission;
    FScopeLock Lock(&Mutex);
    bShuttingDown = true;
    Admission = MoveTemp(PendingAdmission);
    PendingExecution.Reset();
    const double Now = FPlatformTime::Seconds();
    for (const TPair<FString, FExecutionPtr>& Pair : Executions)
    {
        const FExecutionPtr& Execution = Pair.Value;
        if (Execution->State == EExecutionState::Terminal)
        {
            continue;
        }
        TSharedPtr<FJsonObject> Lost = MakeShared<FJsonObject>();
        Lost->SetStringField(TEXT("status"), TEXT("lost"));
        Lost->SetBoolField(TEXT("stateMayHaveChanged"), Execution->State == EExecutionState::Running);
        Lost->SetObjectField(
            TEXT("error"),
            MakeExecutionError(
                TEXT("runtime.python_execution_lost"),
                TEXT("runtime"),
                TEXT("The Editor runtime that owned this Python execution is shutting down.")));
        Execution->TerminalJson = SerializeObject(Lost);
        Execution->TerminalAtSeconds = Now;
        Execution->State = EExecutionState::Terminal;
        DeleteExecutionFiles(Execution->SourcePath, Execution->RunnerPath, Execution->ResultPath);
    }
    ActiveExecutionId.Reset();
    GameThreadProgress = TFunction<void()>();
    Lock.Unlock();
    if (Admission.IsValid())
    {
        Admission->TryCancel();
    }
}

FPythonExecutionService::FExecutionPtr FPythonExecutionService::PrepareRun(
    const TSharedPtr<FJsonObject>& Arguments,
    TSharedPtr<FJsonObject>& OutError)
{
    OutError.Reset();
    FString Script;
    if (!Arguments.IsValid()
        || Arguments->Values.Num() != 1
        || !Arguments->TryGetStringField(TEXT("script"), Script)
        || Script.TrimStartAndEnd().IsEmpty()
        || Script.Len() > MaxScriptCharacters)
    {
        OutError = MakeDispatchError(
            TEXT("tool.invalid_arguments"),
            TEXT("python.run requires only one non-empty script within the size limit."));
        return nullptr;
    }

    FExecutionPtr Execution;
    {
        FScopeLock Lock(&Mutex);
        CleanupExpiredLocked(FPlatformTime::Seconds());
        if (bShuttingDown)
        {
            OutError = MakeDispatchError(
                TEXT("runtime.editor_shutting_down"),
                TEXT("Unreal Editor is shutting down."));
            return nullptr;
        }
        if (!ActiveExecutionId.IsEmpty())
        {
            OutError = MakeDispatchError(
                TEXT("runtime.python_busy"),
                TEXT("Another Python fallback execution is already active."));
            const FExecutionPtr* Active = Executions.Find(ActiveExecutionId);
            if (Active != nullptr && (*Active)->bExposed)
            {
                OutError->SetStringField(TEXT("executionId"), ActiveExecutionId);
            }
            return nullptr;
        }

        Execution = MakeShared<FExecution, ESPMode::ThreadSafe>();
        Execution->Id = TEXT("py_") + FGuid::NewGuid().ToString(EGuidFormats::Digits).ToLower();
        const FString Directory = FPaths::ConvertRelativePathToFull(
            FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Loomle"), TEXT("Python")));
        Execution->SourcePath = FPaths::Combine(Directory, Execution->Id + TEXT(".source.py"));
        Execution->RunnerPath = FPaths::Combine(Directory, Execution->Id + TEXT(".runner.py"));
        Execution->ResultPath = FPaths::Combine(Directory, Execution->Id + TEXT(".result.json"));
        Executions.Add(Execution->Id, Execution);
        ActiveExecutionId = Execution->Id;
    }

    const FString Directory = FPaths::GetPath(Execution->SourcePath);
    const FString RunnerSource = BuildRunnerSource(Execution->SourcePath, Execution->ResultPath);
    if (!IFileManager::Get().MakeDirectory(*Directory, true)
        || !FFileHelper::SaveStringToFile(
            Script,
            *Execution->SourcePath,
            FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM)
        || !FFileHelper::SaveStringToFile(
            RunnerSource,
            *Execution->RunnerPath,
            FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
    {
        DeleteExecutionFiles(Execution->SourcePath, Execution->RunnerPath, Execution->ResultPath);
        FScopeLock Lock(&Mutex);
        RemoveExecutionLocked(Execution);
        OutError = MakeDispatchError(
            TEXT("runtime.python_source_staging_failed"),
            TEXT("Loomle could not stage the Python source inside the project Saved directory."));
        return nullptr;
    }

    bool bInvalidatedDuringStaging = false;
    {
        FScopeLock Lock(&Mutex);
        bInvalidatedDuringStaging = bShuttingDown
            || Execution->State != EExecutionState::Prepared;
        if (bInvalidatedDuringStaging)
        {
            RemoveExecutionLocked(Execution);
        }
    }
    if (bInvalidatedDuringStaging)
    {
        DeleteExecutionFiles(Execution->SourcePath, Execution->RunnerPath, Execution->ResultPath);
        OutError = MakeDispatchError(
            TEXT("runtime.editor_shutting_down"),
            TEXT("Unreal Editor began shutting down while Python source was staged."));
        return nullptr;
    }

    return Execution;
}

bool FPythonExecutionService::EnqueueForExecution(
    const FExecutionPtr& Execution,
    const TSharedRef<Loomle::Runtime::FGameThreadAdmission, ESPMode::ThreadSafe>& Admission)
{
    FScopeLock Lock(&Mutex);
    if (bShuttingDown
        || !Execution.IsValid()
        || Execution->State != EExecutionState::Prepared
        || PendingExecution.IsValid())
    {
        return false;
    }
    PendingExecution = Execution;
    PendingAdmission = Admission;
    return true;
}

bool FPythonExecutionService::TickExecution(float DeltaTime)
{
    (void)DeltaTime;
    check(IsInGameThread());

    // Core Ticker normally runs outside TaskGraph named-thread processing. If
    // another caller recursively ticks it from a Game Thread task, leave the
    // execution pending so the worker can cancel admission instead of entering
    // Python from the unsafe call stack.
    if (FTaskGraphInterface::Get().IsThreadProcessingTasks(ENamedThreads::GameThread))
    {
        return true;
    }

    FExecutionPtr Execution;
    TSharedPtr<Loomle::Runtime::FGameThreadAdmission, ESPMode::ThreadSafe> Admission;
    TFunction<void()> Progress;
    {
        FScopeLock Lock(&Mutex);
        if (bShuttingDown || !PendingExecution.IsValid())
        {
            return true;
        }
        Execution = MoveTemp(PendingExecution);
        Admission = MoveTemp(PendingAdmission);
        Progress = GameThreadProgress;
    }

    if (!Admission.IsValid() || !Admission->TryStart())
    {
        AbandonBeforeStart(Execution);
    }
    else
    {
        Execute(Execution);
    }
    if (Progress)
    {
        Progress();
    }
    return true;
}

void FPythonExecutionService::AbandonBeforeStart(const FExecutionPtr& Execution)
{
    if (!Execution.IsValid())
    {
        return;
    }
    {
        FScopeLock Lock(&Mutex);
        const FExecutionPtr* Found = Executions.Find(Execution->Id);
        if (Execution->State != EExecutionState::Prepared
            || Found == nullptr
            || *Found != Execution)
        {
            return;
        }
        if (PendingExecution == Execution)
        {
            PendingExecution.Reset();
            PendingAdmission.Reset();
        }
        RemoveExecutionLocked(Execution);
    }
    DeleteExecutionFiles(Execution->SourcePath, Execution->RunnerPath, Execution->ResultPath);
}

void FPythonExecutionService::Execute(const FExecutionPtr& Execution)
{
    check(IsInGameThread());
    if (!Execution.IsValid())
    {
        return;
    }

    {
        FScopeLock Lock(&Mutex);
        if (Execution->State != EExecutionState::Prepared)
        {
            return;
        }
        Execution->State = EExecutionState::Running;
        Execution->StartedAtSeconds = FPlatformTime::Seconds();
    }

    const double StartSeconds = FPlatformTime::Seconds();
    TSharedPtr<FJsonObject> Terminal;
    TArray<FPythonLogOutputEntry> NativeLogs;

    IPythonScriptPlugin* Python = IPythonScriptPlugin::Get();
    if (Python == nullptr || !Python->IsPythonConfigured() || !Python->IsPythonAvailable())
    {
        Terminal = MakeTerminalFailure(
            TEXT("runtime.python_unavailable"),
            TEXT("validation"),
            TEXT("Unreal Editor Python is not available in this runtime."),
            false,
            0);
    }
    else if (!Python->IsPythonInitialized())
    {
        Terminal = MakeTerminalFailure(
            TEXT("runtime.python_initializing"),
            TEXT("validation"),
            TEXT("Unreal Editor Python has not finished initializing."),
            false,
            0);
    }
    else
    {
        FPythonCommandEx Command;
        Command.Flags = EPythonCommandFlags::Unattended;
        Command.ExecutionMode = EPythonCommandExecutionMode::ExecuteFile;
        Command.FileExecutionScope = EPythonFileExecutionScope::Private;
        Command.Command = FString::Printf(TEXT("\"%s\""), *Execution->RunnerPath.Replace(TEXT("\""), TEXT("\\\"")));
        const bool bNativeSuccess = Python->ExecPythonCommandEx(Command);
        NativeLogs = MoveTemp(Command.LogOutput);
        const int64 DurationMs = FMath::RoundToInt64(
            (FPlatformTime::Seconds() - StartSeconds) * 1000.0);

        FString ResultText;
        const int64 ResultFileSize = IFileManager::Get().FileSize(*Execution->ResultPath);
        if (ResultFileSize > MaxRunnerDocumentUtf8Bytes)
        {
            Terminal = MakeTerminalFailure(
                TEXT("runtime.python_result_too_large"),
                TEXT("result"),
                TEXT("The Python runner result document exceeded its internal size bound."),
                true,
                DurationMs,
                &NativeLogs,
                Command.CommandResult);
        }
        else if (ResultFileSize < 0
            || !FFileHelper::LoadFileToString(ResultText, *Execution->ResultPath))
        {
            Terminal = MakeTerminalFailure(
                TEXT("runtime.python_execution_failed"),
                TEXT("execution"),
                bNativeSuccess
                    ? TEXT("The Python runner did not produce a result document.")
                    : TEXT("Python execution failed before producing a result document."),
                true,
                DurationMs,
                &NativeLogs,
                Command.CommandResult);
        }
        else
        {
            TSharedPtr<FJsonObject> RunnerResult = ParseObject(ResultText);
            bool bRunnerOk = false;
            if (!RunnerResult.IsValid() || !RunnerResult->TryGetBoolField(TEXT("ok"), bRunnerOk))
            {
                Terminal = MakeTerminalFailure(
                    TEXT("runtime.python_execution_failed"),
                    TEXT("result"),
                    TEXT("The Python runner returned an invalid result document."),
                    true,
                    DurationMs,
                    &NativeLogs,
                    Command.CommandResult);
            }
            else if (bRunnerOk && bNativeSuccess)
            {
                const TSharedPtr<FJsonObject>* ResultObject = nullptr;
                if (!RunnerResult->TryGetObjectField(TEXT("result"), ResultObject)
                    || ResultObject == nullptr
                    || !(*ResultObject).IsValid())
                {
                    Terminal = MakeTerminalFailure(
                        TEXT("runtime.python_invalid_result"),
                        TEXT("result"),
                        TEXT("run() did not return a JSON object."),
                        true,
                        DurationMs,
                        &NativeLogs);
                }
                else
                {
                    const FString StructuredJson = SerializeObject(*ResultObject);
                    if (StructuredJson.IsEmpty()
                        || Utf8Bytes(StructuredJson) > MaxResultUtf8Bytes)
                    {
                        Terminal = MakeTerminalFailure(
                            TEXT("runtime.python_result_too_large"),
                            TEXT("result"),
                            TEXT("run() returned more than 1 MiB of structured JSON."),
                            true,
                            DurationMs,
                            &NativeLogs);
                    }
                    else
                    {
                        Terminal = MakeShared<FJsonObject>();
                        Terminal->SetStringField(TEXT("status"), TEXT("succeeded"));
                        Terminal->SetBoolField(TEXT("stateMayHaveChanged"), true);
                        Terminal->SetObjectField(TEXT("result"), *ResultObject);
                        // Read-only Python sal.object() projection: markers in
                        // the result are replaced in place with canonical views
                        // and the projection annex is reported.
                        Loomle::Sal::FSalProjectionService::ProjectResult(
                            Terminal);
                        bool bLogsTruncated = false;
                        Terminal->SetArrayField(TEXT("logs"), BuildLogs(NativeLogs, bLogsTruncated));
                        Terminal->SetBoolField(TEXT("logsTruncated"), bLogsTruncated);
                        Terminal->SetNumberField(TEXT("durationMs"), static_cast<double>(DurationMs));
                    }
                }
            }
            else
            {
                FString Code = TEXT("runtime.python_execution_failed");
                FString Phase = TEXT("execution");
                FString Message = TEXT("Python execution failed.");
                FString Traceback = Command.CommandResult;
                RunnerResult->TryGetStringField(TEXT("code"), Code);
                RunnerResult->TryGetStringField(TEXT("phase"), Phase);
                RunnerResult->TryGetStringField(TEXT("message"), Message);
                RunnerResult->TryGetStringField(TEXT("traceback"), Traceback);
                Terminal = MakeTerminalFailure(
                    Code,
                    Phase,
                    Message,
                    true,
                    DurationMs,
                    &NativeLogs,
                    Traceback);
            }
        }
    }

    DeleteExecutionFiles(Execution->SourcePath, Execution->RunnerPath, Execution->ResultPath);
    const FString TerminalJson = SerializeObject(Terminal);
    {
        FScopeLock Lock(&Mutex);
        Execution->TerminalJson = TerminalJson.IsEmpty()
            ? SerializeObject(MakeTerminalFailure(
                TEXT("runtime.python_execution_failed"),
                TEXT("result"),
                TEXT("Loomle could not serialize the Python execution result."),
                true,
                0))
            : TerminalJson;
        Execution->TerminalAtSeconds = FPlatformTime::Seconds();
        Execution->State = EExecutionState::Terminal;
        if (ActiveExecutionId == Execution->Id)
        {
            ActiveExecutionId.Reset();
        }
    }
}

bool FPythonExecutionService::IsTerminal(const FExecutionPtr& Execution) const
{
    FScopeLock Lock(&Mutex);
    return !Execution.IsValid() || Execution->State == EExecutionState::Terminal;
}

TSharedPtr<FJsonObject> FPythonExecutionService::BuildInitialResponse(const FExecutionPtr& Execution)
{
    FScopeLock Lock(&Mutex);
    TSharedPtr<FJsonObject> Response = BuildSnapshotLocked(Execution, false, true);
    if (Execution.IsValid()
        && Execution->State == EExecutionState::Terminal
        && !Execution->bExposed)
    {
        RemoveExecutionLocked(Execution);
    }
    return Response;
}

TSharedPtr<FJsonObject> FPythonExecutionService::Poll(
    const TSharedPtr<FJsonObject>& Arguments,
    TSharedPtr<FJsonObject>& OutError)
{
    OutError.Reset();
    FString ExecutionId;
    if (!Arguments.IsValid()
        || Arguments->Values.Num() != 1
        || !Arguments->TryGetStringField(TEXT("executionId"), ExecutionId)
        || ExecutionId.IsEmpty()
        || ExecutionId.Len() > 256)
    {
        OutError = MakeDispatchError(
            TEXT("tool.invalid_arguments"),
            TEXT("python.poll requires only one non-empty executionId."));
        return nullptr;
    }

    FScopeLock Lock(&Mutex);
    CleanupExpiredLocked(FPlatformTime::Seconds());
    const FExecutionPtr* Found = Executions.Find(ExecutionId);
    if (Found == nullptr || !Found->IsValid() || !(*Found)->bExposed)
    {
        if (ExpiredExecutionIds.Contains(ExecutionId))
        {
            OutError = MakeDispatchError(
                TEXT("runtime.python_execution_expired"),
                TEXT("The retained Python execution result has expired."));
            return nullptr;
        }
        OutError = MakeDispatchError(
            TEXT("runtime.python_execution_not_found"),
            TEXT("The Python execution id is not available in this Editor runtime."));
        return nullptr;
    }
    return BuildSnapshotLocked(*Found, true, false);
}

void FPythonExecutionService::RemoveExecutionLocked(const FExecutionPtr& Execution)
{
    if (!Execution.IsValid())
    {
        return;
    }
    Executions.Remove(Execution->Id);
    if (ActiveExecutionId == Execution->Id)
    {
        ActiveExecutionId.Reset();
    }
}

void FPythonExecutionService::CleanupExpiredLocked(double NowSeconds)
{
    TArray<FString> OldTombstones;
    for (const TPair<FString, double>& Pair : ExpiredExecutionIds)
    {
        if (NowSeconds - Pair.Value > TerminalRetentionSeconds)
        {
            OldTombstones.Add(Pair.Key);
        }
    }
    for (const FString& Id : OldTombstones)
    {
        ExpiredExecutionIds.Remove(Id);
    }

    TArray<FString> Expired;
    for (const TPair<FString, FExecutionPtr>& Pair : Executions)
    {
        const FExecutionPtr& Execution = Pair.Value;
        if (Execution->State == EExecutionState::Terminal
            && Execution->bExposed
            && Execution->TerminalAtSeconds > 0.0
            && NowSeconds - Execution->TerminalAtSeconds > TerminalRetentionSeconds)
        {
            Expired.Add(Pair.Key);
        }
    }
    for (const FString& Id : Expired)
    {
        Executions.Remove(Id);
        ExpiredExecutionIds.Add(Id, NowSeconds);
    }
}

TSharedPtr<FJsonObject> FPythonExecutionService::BuildSnapshotLocked(
    const FExecutionPtr& Execution,
    bool bIncludeExecutionId,
    bool bExposeIfRunning)
{
    if (!Execution.IsValid())
    {
        return MakeTerminalFailure(
            TEXT("runtime.python_execution_lost"),
            TEXT("runtime"),
            TEXT("The Python execution record is unavailable."),
            true,
            0);
    }
    if (Execution->State == EExecutionState::Terminal)
    {
        TSharedPtr<FJsonObject> Terminal = ParseObject(Execution->TerminalJson);
        if (!Terminal.IsValid())
        {
            Terminal = MakeTerminalFailure(
                TEXT("runtime.python_execution_failed"),
                TEXT("result"),
                TEXT("The retained Python result is invalid."),
                true,
                0);
        }
        if (bIncludeExecutionId)
        {
            Terminal->SetStringField(TEXT("executionId"), Execution->Id);
        }
        return Terminal;
    }

    if (bExposeIfRunning)
    {
        Execution->bExposed = true;
    }
    TSharedPtr<FJsonObject> Running = MakeShared<FJsonObject>();
    Running->SetStringField(TEXT("status"), TEXT("running"));
    Running->SetStringField(TEXT("executionId"), Execution->Id);
    Running->SetBoolField(TEXT("stateMayHaveChanged"), Execution->State == EExecutionState::Running);
    const double Start = Execution->StartedAtSeconds > 0.0
        ? Execution->StartedAtSeconds
        : FPlatformTime::Seconds();
    Running->SetNumberField(
        TEXT("elapsedMs"),
        static_cast<double>(FMath::Max<int64>(
            0,
            FMath::RoundToInt64((FPlatformTime::Seconds() - Start) * 1000.0))));

    TSharedPtr<FJsonObject> Continuation = MakeShared<FJsonObject>();
    Continuation->SetStringField(TEXT("tool"), TEXT("python"));
    TSharedPtr<FJsonObject> ContinuationArguments = MakeShared<FJsonObject>();
    ContinuationArguments->SetStringField(TEXT("operation"), TEXT("poll"));
    ContinuationArguments->SetStringField(TEXT("executionId"), Execution->Id);
    Continuation->SetObjectField(TEXT("arguments"), ContinuationArguments);
    Continuation->SetNumberField(TEXT("pollAfterMs"), PollAfterMs);
    Running->SetObjectField(TEXT("continuation"), Continuation);
    return Running;
}

} // namespace Loomle::Python
