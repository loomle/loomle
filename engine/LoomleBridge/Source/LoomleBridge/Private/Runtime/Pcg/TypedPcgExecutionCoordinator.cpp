// Copyright 2026 Loomle contributors.

#include "Runtime/Pcg/TypedPcgExecutionCoordinator.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Engine/World.h"
#include "HAL/PlatformTime.h"
#include "Misc/Guid.h"
#include "Misc/ScopeLock.h"
#include "PCGComponent.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

namespace Loomle::Runtime
{
namespace
{

TSharedPtr<FJsonObject> MakeDispatchError(
    const FString& Code,
    const FString& Message)
{
    TSharedPtr<FJsonObject> Error = MakeShared<FJsonObject>();
    Error->SetBoolField(TEXT("isError"), true);
    Error->SetStringField(TEXT("code"), Code);
    Error->SetStringField(TEXT("message"), Message.IsEmpty() ? Code : Message);
    return Error;
}

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

}

FTypedPcgExecutionCoordinator::FTypedPcgExecutionCoordinator() = default;

FTypedPcgExecutionCoordinator::~FTypedPcgExecutionCoordinator()
{
    Shutdown();
}

void FTypedPcgExecutionCoordinator::Startup()
{
    FScopeLock Lock(&Mutex_);
    if (bInitialized_)
    {
        return;
    }
    bInitialized_ = true;
    Registry_.Startup();
    Kernel_.Startup();
    Loomle::Runtime::FLoomleAsyncKernel::FProfile Profile;
    Profile.Namespace = TEXT("pcg");
    Profile.BusyCode = TEXT("runtime.pcg_busy");
    Profile.BusyMessage = TEXT(
        "Another typed-PCG execution is already active for this runtime.");
    Profile.NotFoundCode = TEXT("runtime.pcg_execution_not_found");
    Profile.NotFoundMessage = TEXT(
        "The typed-PCG execution id is not available in this Editor runtime.");
    Profile.ExpiredCode = TEXT("runtime.pcg_execution_expired");
    Profile.ExpiredMessage = TEXT(
        "The retained typed-PCG execution result has expired.");
    Profile.LostCode = TEXT("runtime.pcg_execution_lost");
    Profile.LostMessage = TEXT(
        "The Editor runtime that owned this typed-PCG execution is shutting "
        "down.");
    Profile.PollToolName = TEXT("pcg_execution");
    Profile.PollAfterMs = 1000;
    Profile.BuildPollArguments =
        [](const FString& ExecutionId)
        {
            TSharedPtr<FJsonObject> PollArguments = MakeShared<FJsonObject>();
            PollArguments->SetStringField(TEXT("operation"), TEXT("poll"));
            PollArguments->SetStringField(TEXT("executionId"), ExecutionId);
            return PollArguments;
        };
    Kernel_.RegisterProfile(Profile);
}

void FTypedPcgExecutionCoordinator::Shutdown()
{
    FScopeLock Lock(&Mutex_);
    if (!bInitialized_)
    {
        return;
    }
    bInitialized_ = false;
    for (const TPair<FString, FActiveRun>& Pair : Active_)
    {
        UPCGComponent* Component = Pair.Value.Component.Get();
        if (Component != nullptr && IsValid(Component))
        {
            Component->OnPCGGraphGeneratedDelegate.RemoveAll(this);
        }
    }
    Active_.Reset();
    Kernel_.Shutdown();
    Registry_.Shutdown();
}

void FTypedPcgExecutionCoordinator::OnGraphGenerated(
    UPCGComponent* Component)
{
    FinishRunForComponent(Component, true);
}

void FTypedPcgExecutionCoordinator::FinishRunForComponent(
    UPCGComponent* Component,
    bool bSucceeded)
{
    FActiveRun Run;
    {
        FScopeLock Lock(&Mutex_);
        if (!bInitialized_ || Component == nullptr || !IsValid(Component))
        {
            return;
        }
        const FString ComponentPath = Component->GetPathName();
        const FActiveRun* Found = nullptr;
        for (const TPair<FString, FActiveRun>& Pair : Active_)
        {
            if (Pair.Value.ComponentPath == ComponentPath)
            {
                Found = &Pair.Value;
                break;
            }
        }
        if (Found == nullptr)
        {
            return;
        }
        Run = *Found;
        Active_.Remove(Run.ExecutionId);
    }
    CompleteRun(Run, bSucceeded);
}

void FTypedPcgExecutionCoordinator::CompleteRun(
    const FActiveRun& Run,
    bool bSucceeded)
{
    TSharedPtr<FJsonObject> Terminal = MakeShared<FJsonObject>();
    Terminal->SetStringField(
        TEXT("status"),
        bSucceeded ? TEXT("succeeded") : TEXT("failed"));
    Terminal->SetBoolField(TEXT("stateMayHaveChanged"), true);
    TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetStringField(TEXT("operation"), TEXT("generate"));
    Result->SetStringField(TEXT("component"), Run.ComponentPath);
    Result->SetNumberField(
        TEXT("taskId"),
        static_cast<double>(Run.TaskId));
    Result->SetStringField(TEXT("selector"), Run.Selector.NormalizeText());
    Terminal->SetObjectField(TEXT("result"), Result);
    if (!bSucceeded)
    {
        TSharedRef<FJsonObject> Error = MakeShared<FJsonObject>();
        Error->SetStringField(TEXT("code"), TEXT("runtime.pcg_generation_failed"));
        Error->SetStringField(TEXT("phase"), TEXT("execution"));
        Error->SetStringField(
            TEXT("message"),
            TEXT("The admitted typed-PCG generate did not complete normally."));
        Error->SetBoolField(TEXT("retryable"), false);
        Terminal->SetObjectField(TEXT("error"), Error);
    }
    Kernel_.Complete(
        Kernel_.FindRecord(Run.ExecutionId),
        SerializeObject(Terminal));
}

FLoomleAsyncKernel::FRecordPtr FTypedPcgExecutionCoordinator::Start(
    const FStartOptions& Options,
    TSharedPtr<FJsonObject>& OutError)
{
    OutError.Reset();
    FScopeLock Lock(&Mutex_);
    if (!bInitialized_)
    {
        OutError = MakeDispatchError(
            TEXT("runtime.pcg_unavailable"),
            TEXT("The typed-PCG execution coordinator is not initialized."));
        return nullptr;
    }

    UPCGComponent* Component = nullptr;
    FString RegistryError;
    if (!Registry_.Validate(
            Options.Selector,
            Options.Ticket,
            Options.SourceTarget,
            Component,
            RegistryError))
    {
        OutError = MakeDispatchError(
            TEXT("validation.preflight_failed"),
            RegistryError.IsEmpty()
                ? TEXT("The typed-PCG preflight admission failed.")
                : RegistryError);
        return nullptr;
    }
    if (Component == nullptr || !IsValid(Component))
    {
        OutError = MakeDispatchError(
            TEXT("resolution.object_not_found"),
            TEXT("The typed-PCG source Component incarnation is invalid."));
        return nullptr;
    }

    FString BusyExecutionId;
    FLoomleAsyncKernel::FRecordPtr Record = Kernel_.Allocate(
        TEXT("pcg"),
        OutError,
        BusyExecutionId);
    if (!Record.IsValid())
    {
        if (!BusyExecutionId.IsEmpty())
        {
            OutError->SetStringField(TEXT("executionId"), BusyExecutionId);
        }
        return nullptr;
    }

    // Admission holds the source/Level-intersection lease implicitly through
    // the validated ticket; the top-Graph shared lease is a later increment.
    FActiveRun Run;
    Run.ExecutionId = Record->Id;
    Run.ComponentPath = Component->GetPathName();
    Run.Component = Component;
    Run.SourceTarget = Options.SourceTarget;
    Run.Selector = Options.Selector;
    Kernel_.Begin(Record);

    Component->OnPCGGraphGeneratedDelegate.RemoveAll(this);
    Component->OnPCGGraphGeneratedDelegate.AddRaw(
        this,
        &FTypedPcgExecutionCoordinator::OnGraphGenerated);
    const FPCGTaskId TaskId = Component->GenerateLocalGetTaskId(
        Options.bForce);
    Run.TaskId = static_cast<uint64>(TaskId);
    if (TaskId == InvalidPCGTaskId)
    {
        // No native task was admitted (unbound, inactive, or unavailable
        // subsystem). Complete honestly as failed and release the record.
        Active_.Remove(Record->Id);
        TSharedPtr<FJsonObject> Terminal = MakeShared<FJsonObject>();
        Terminal->SetStringField(TEXT("status"), TEXT("failed"));
        Terminal->SetBoolField(TEXT("stateMayHaveChanged"), true);
        TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
        Result->SetStringField(TEXT("operation"), TEXT("generate"));
        Result->SetStringField(TEXT("component"), Run.ComponentPath);
        Result->SetNumberField(TEXT("taskId"), 0.0);
        Result->SetStringField(TEXT("selector"), Run.Selector.NormalizeText());
        Terminal->SetObjectField(TEXT("result"), Result);
        TSharedRef<FJsonObject> Error = MakeShared<FJsonObject>();
        Error->SetStringField(TEXT("code"), TEXT("runtime.pcg_generation_failed"));
        Error->SetStringField(TEXT("phase"), TEXT("execution"));
        Error->SetStringField(
            TEXT("message"),
            TEXT("No native PCG task was admitted for the typed-PCG generate."));
        Error->SetBoolField(TEXT("retryable"), false);
        Terminal->SetObjectField(TEXT("error"), Error);
        Kernel_.Complete(Record, SerializeObject(Terminal));
        return Record;
    }
    Active_.Add(Record->Id, Run);
    return Record;
}

FTypedPcgTicketPtr FTypedPcgExecutionCoordinator::Prepare(
    const FTypedPcgWorldSelector& Selector,
    const FString& SourceTargetText,
    FString& OutError)
{
    FScopeLock Lock(&Mutex_);
    if (!bInitialized_)
    {
        OutError = TEXT(
            "The typed-PCG execution coordinator is not initialized.");
        return nullptr;
    }
    return Registry_.Prepare(Selector, SourceTargetText, OutError);
}

TSharedPtr<FJsonObject> FTypedPcgExecutionCoordinator::Poll(
    const FString& ExecutionId,
    TSharedPtr<FJsonObject>& OutError)
{
    FScopeLock Lock(&Mutex_);
    if (!bInitialized_)
    {
        OutError = MakeDispatchError(
            TEXT("runtime.pcg_unavailable"),
            TEXT("The typed-PCG execution coordinator is not initialized."));
        return nullptr;
    }
    return Kernel_.Poll(TEXT("pcg"), ExecutionId, OutError);
}

void FTypedPcgExecutionCoordinator::Tick()
{
    FScopeLock Lock(&Mutex_);
    if (!bInitialized_)
    {
        return;
    }
    // Observe terminal native state: a run whose component no longer reports
    // an active generation task and whose delegate has not fired completes as
    // failed (fast-skip / aborted generation).
    TArray<FString> Stale;
    for (const TPair<FString, FActiveRun>& Pair : Active_)
    {
        UPCGComponent* Component = Pair.Value.Component.Get();
        if (Component == nullptr
            || !IsValid(Component)
            || !Component->IsGenerating())
        {
            Stale.Add(Pair.Key);
        }
    }
    for (const FString& ExecutionId : Stale)
    {
        FActiveRun Run = Active_[ExecutionId];
        Active_.Remove(ExecutionId);
        CompleteRun(Run, true);
    }
    Kernel_.Tick();
}


}
