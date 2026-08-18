// Copyright 2026 Loomle contributors.

#include "Runtime/LoomleAsyncKernel.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "HAL/PlatformTime.h"
#include "Misc/Guid.h"
#include "Misc/ScopeLock.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

namespace Loomle::Runtime
{
namespace
{

FString SerializeObject(const TSharedPtr<FJsonObject>& Object)
{
    FString Output;
    const TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>>
        Writer = TJsonWriterFactory<
            TCHAR,
            TCondensedJsonPrintPolicy<TCHAR>>::Create(&Output);
    if (!Object.IsValid()
        || !FJsonSerializer::Serialize(Object.ToSharedRef(), Writer))
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

TSharedPtr<FJsonObject> MakeKernelDispatchError(
    const FString& Code,
    const FString& Message)
{
    TSharedPtr<FJsonObject> Error = MakeShared<FJsonObject>();
    Error->SetBoolField(TEXT("isError"), true);
    Error->SetStringField(TEXT("code"), Code);
    Error->SetStringField(TEXT("message"), Message.IsEmpty() ? Code : Message);
    return Error;
}

TSharedPtr<FJsonObject> MakeKernelTerminalError(
    const FString& Code,
    const FString& Phase,
    const FString& Message)
{
    TSharedPtr<FJsonObject> Error = MakeShared<FJsonObject>();
    Error->SetStringField(TEXT("code"), Code);
    Error->SetStringField(TEXT("phase"), Phase);
    Error->SetStringField(TEXT("message"), Message.IsEmpty() ? Code : Message);
    Error->SetBoolField(TEXT("retryable"), false);
    return Error;
}

TSharedPtr<FJsonObject> MakeTerminalFailure(
    const FString& Code,
    const FString& Phase,
    const FString& Message,
    const bool bStateMayHaveChanged)
{
    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetStringField(TEXT("status"), TEXT("failed"));
    Result->SetBoolField(TEXT("stateMayHaveChanged"), bStateMayHaveChanged);
    Result->SetObjectField(
        TEXT("error"),
        MakeKernelTerminalError(Code, Phase, Message));
    return Result;
}

}

void FLoomleAsyncKernel::RegisterProfile(const FProfile& Profile)
{
    FScopeLock Lock(&Mutex);
    if (Profile.Namespace.IsEmpty())
    {
        return;
    }
    Profiles.Add(Profile.Namespace, Profile);
}

void FLoomleAsyncKernel::Startup()
{
    FScopeLock Lock(&Mutex);
    bShuttingDown = false;
    ExpiredIds.Reset();
}

void FLoomleAsyncKernel::Shutdown()
{
    FScopeLock Lock(&Mutex);
    bShuttingDown = true;
    const double Now = FPlatformTime::Seconds();
    for (const TPair<FString, FRecordPtr>& Pair : Records)
    {
        const FRecordPtr& Record = Pair.Value;
        if (!Record.IsValid() || Record->State == EState::Terminal)
        {
            continue;
        }
        const FProfile* Profile = ProfileLocked(Record->Namespace);
        TSharedPtr<FJsonObject> Lost = MakeShared<FJsonObject>();
        Lost->SetStringField(TEXT("status"), TEXT("lost"));
        Lost->SetBoolField(
            TEXT("stateMayHaveChanged"),
            Record->State == EState::Running);
        Lost->SetObjectField(
            TEXT("error"),
            MakeKernelTerminalError(
                Profile != nullptr
                    ? Profile->LostCode
                    : TEXT("runtime.execution_lost"),
                TEXT("runtime"),
                Profile != nullptr
                    ? Profile->LostMessage
                    : TEXT("The Editor runtime that owned this execution is "
                        "shutting down.")));
        Record->TerminalJson = SerializeObject(Lost);
        Record->TerminalAtSeconds = Now;
        Record->State = EState::Terminal;
    }
    ActiveIds.Reset();
}

const FLoomleAsyncKernel::FProfile* FLoomleAsyncKernel::ProfileLocked(
    const FString& Namespace) const
{
    return Profiles.Find(Namespace);
}

FLoomleAsyncKernel::FRecordPtr FLoomleAsyncKernel::Allocate(
    const FString& Namespace,
    TSharedPtr<FJsonObject>& OutError,
    FString& OutBusyExecutionId)
{
    OutError.Reset();
    OutBusyExecutionId.Reset();
    FScopeLock Lock(&Mutex);
    CleanupExpiredLocked(FPlatformTime::Seconds());
    if (bShuttingDown)
    {
        OutError = MakeKernelDispatchError(
            TEXT("runtime.editor_shutting_down"),
            TEXT("Unreal Editor is shutting down."));
        return nullptr;
    }
    const FProfile* Profile = ProfileLocked(Namespace);
    if (Profile == nullptr)
    {
        OutError = MakeKernelDispatchError(
            TEXT("runtime.kernel_unavailable"),
            TEXT("The requested execution frontend is not registered."));
        return nullptr;
    }
    const FString* Active = ActiveIds.Find(Namespace);
    if (Active != nullptr && !Active->IsEmpty())
    {
        OutError = MakeKernelDispatchError(
            Profile->BusyCode,
            Profile->BusyMessage);
        const FRecordPtr* ActiveRecord = Records.Find(*Active);
        if (ActiveRecord != nullptr
            && (*ActiveRecord).IsValid()
            && (*ActiveRecord)->bExposed)
        {
            OutBusyExecutionId = *Active;
        }
        return nullptr;
    }

    FRecordPtr Record = MakeShared<FRecord, ESPMode::ThreadSafe>();
    Record->Id = Namespace + TEXT("_")
        + FGuid::NewGuid().ToString(EGuidFormats::Digits).ToLower();
    Record->Namespace = Namespace;
    Records.Add(Record->Id, Record);
    ActiveIds.Add(Namespace, Record->Id);
    return Record;
}

void FLoomleAsyncKernel::Begin(const FRecordPtr& Record)
{
    FScopeLock Lock(&Mutex);
    if (!Record.IsValid() || Record->State != EState::Pending)
    {
        return;
    }
    Record->State = EState::Running;
    Record->StartedAtSeconds = FPlatformTime::Seconds();
}

void FLoomleAsyncKernel::Complete(
    const FRecordPtr& Record,
    const FString& TerminalJson)
{
    FScopeLock Lock(&Mutex);
    if (!Record.IsValid() || Record->State == EState::Terminal)
    {
        return;
    }
    Record->TerminalJson = TerminalJson;
    Record->TerminalAtSeconds = FPlatformTime::Seconds();
    Record->State = EState::Terminal;
    // The record remains retained for poll, but the frontend slot frees so a
    // later execution may be admitted immediately.
    const FString* Active = ActiveIds.Find(Record->Namespace);
    if (Active != nullptr && *Active == Record->Id)
    {
        ActiveIds.Remove(Record->Namespace);
    }
}

void FLoomleAsyncKernel::Remove(const FRecordPtr& Record)
{
    FScopeLock Lock(&Mutex);
    if (!Record.IsValid())
    {
        return;
    }
    Records.Remove(Record->Id);
    const FString* Active = ActiveIds.Find(Record->Namespace);
    if (Active != nullptr && *Active == Record->Id)
    {
        ActiveIds.Remove(Record->Namespace);
    }
}

bool FLoomleAsyncKernel::IsActive(const FRecordPtr& Record) const
{
    FScopeLock Lock(&Mutex);
    if (!Record.IsValid())
    {
        return false;
    }
    const FString* Active = ActiveIds.Find(Record->Namespace);
    return Active != nullptr && *Active == Record->Id;
}

FString FLoomleAsyncKernel::ActiveId(const FString& Namespace) const
{
    FScopeLock Lock(&Mutex);
    const FString* Active = ActiveIds.Find(Namespace);
    return Active != nullptr ? *Active : FString();
}

FLoomleAsyncKernel::FRecordPtr FLoomleAsyncKernel::FindRecord(
    const FString& ExecutionId) const
{
    FScopeLock Lock(&Mutex);
    const FRecordPtr* Found = Records.Find(ExecutionId);
    return Found != nullptr ? *Found : FRecordPtr();
}

TSharedPtr<FJsonObject> FLoomleAsyncKernel::Snapshot(
    const FRecordPtr& Record,
    bool bIncludeExecutionId,
    bool bExposeIfRunning)
{
    FScopeLock Lock(&Mutex);
    return SnapshotLocked(Record, bIncludeExecutionId, bExposeIfRunning);
}

TSharedPtr<FJsonObject> FLoomleAsyncKernel::SnapshotLocked(
    const FRecordPtr& Record,
    bool bIncludeExecutionId,
    bool bExposeIfRunning) const
{
    if (!Record.IsValid())
    {
        return MakeTerminalFailure(
            TEXT("runtime.execution_lost"),
            TEXT("runtime"),
            TEXT("The execution record is unavailable."),
            true);
    }
    if (Record->State == EState::Terminal)
    {
        TSharedPtr<FJsonObject> Terminal = ParseObject(Record->TerminalJson);
        if (!Terminal.IsValid())
        {
            Terminal = MakeTerminalFailure(
                TEXT("runtime.execution_failed"),
                TEXT("result"),
                TEXT("The retained execution result is invalid."),
                true);
        }
        if (bIncludeExecutionId)
        {
            Terminal->SetStringField(TEXT("executionId"), Record->Id);
        }
        return Terminal;
    }

    if (bExposeIfRunning)
    {
        Record->bExposed = true;
    }
    TSharedPtr<FJsonObject> Running = MakeShared<FJsonObject>();
    Running->SetStringField(TEXT("status"), TEXT("running"));
    Running->SetStringField(TEXT("executionId"), Record->Id);
    Running->SetBoolField(
        TEXT("stateMayHaveChanged"),
        Record->State == EState::Running);
    const double Start = Record->StartedAtSeconds > 0.0
        ? Record->StartedAtSeconds
        : FPlatformTime::Seconds();
    Running->SetNumberField(
        TEXT("elapsedMs"),
        static_cast<double>(FMath::Max<int64>(
            0,
            FMath::RoundToInt64(
                (FPlatformTime::Seconds() - Start) * 1000.0))));
    const FProfile* Profile = ProfileLocked(Record->Namespace);
    if (Profile != nullptr && Profile->BuildPollArguments)
    {
        TSharedPtr<FJsonObject> PollArguments =
            Profile->BuildPollArguments(Record->Id);
        TSharedPtr<FJsonObject> Continuation = MakeShared<FJsonObject>();
        Continuation->SetStringField(
            TEXT("tool"),
            Profile->PollToolName.IsEmpty()
                ? TEXT("poll")
                : Profile->PollToolName);
        Continuation->SetObjectField(TEXT("arguments"), PollArguments);
        Continuation->SetNumberField(
            TEXT("pollAfterMs"),
            static_cast<double>(Profile->PollAfterMs));
        Running->SetObjectField(TEXT("continuation"), Continuation);
    }
    return Running;
}

TSharedPtr<FJsonObject> FLoomleAsyncKernel::Poll(
    const FString& Namespace,
    const FString& ExecutionId,
    TSharedPtr<FJsonObject>& OutError)
{
    OutError.Reset();
    FScopeLock Lock(&Mutex);
    CleanupExpiredLocked(FPlatformTime::Seconds());
    const FRecordPtr* Found = Records.Find(ExecutionId);
    const FProfile* Profile = ProfileLocked(Namespace);
    if (Found == nullptr || !Found->IsValid() || !(*Found)->bExposed)
    {
        if (ExpiredIds.Contains(ExecutionId))
        {
            OutError = MakeKernelDispatchError(
                Profile != nullptr
                    ? Profile->ExpiredCode
                    : TEXT("runtime.execution_expired"),
                Profile != nullptr
                    ? Profile->ExpiredMessage
                    : TEXT("The retained execution result has expired."));
            return nullptr;
        }
        OutError = MakeKernelDispatchError(
            Profile != nullptr
                ? Profile->NotFoundCode
                : TEXT("runtime.execution_not_found"),
            Profile != nullptr
                ? Profile->NotFoundMessage
                : TEXT("The execution id is not available in this Editor "
                    "runtime."));
        return nullptr;
    }
    return SnapshotLocked(*Found, true, false);
}

void FLoomleAsyncKernel::Tick()
{
    FScopeLock Lock(&Mutex);
    CleanupExpiredLocked(FPlatformTime::Seconds());
}

void FLoomleAsyncKernel::CleanupExpiredLocked(double NowSeconds)
{
    TArray<FString> OldTombstones;
    for (const TPair<FString, double>& Pair : ExpiredIds)
    {
        if (NowSeconds - Pair.Value > TerminalRetentionSeconds)
        {
            OldTombstones.Add(Pair.Key);
        }
    }
    for (const FString& Id : OldTombstones)
    {
        ExpiredIds.Remove(Id);
    }

    TArray<FString> Expired;
    for (const TPair<FString, FRecordPtr>& Pair : Records)
    {
        const FRecordPtr& Record = Pair.Value;
        if (Record.IsValid()
            && Record->State == EState::Terminal
            && Record->bExposed
            && Record->TerminalAtSeconds > 0.0
            && NowSeconds - Record->TerminalAtSeconds
                > TerminalRetentionSeconds)
        {
            Expired.Add(Pair.Key);
        }
    }
    for (const FString& Id : Expired)
    {
        Records.Remove(Id);
        ExpiredIds.Add(Id, NowSeconds);
    }
}
}
