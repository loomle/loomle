// Copyright 2026 Loomle contributors.

#if WITH_DEV_AUTOMATION_TESTS

#include "LoomleBridgeModule.h"

#include "Async/Async.h"
#include "Async/TaskGraphInterfaces.h"
#include "Dom/JsonObject.h"
#include "Editor.h"
#include "Generated/LoomleProtocolVersion.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTime.h"
#include "LoomleGameThreadAdmission.h"
#include "LoomlePipeServer.h"
#include "LoomleRequestCancellation.h"
#include "HAL/FileManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/Base64.h"
#include "Misc/FileHelper.h"
#include "Misc/Guid.h"
#include "Misc/Paths.h"
#include "Python/LoomlePythonExecutionService.h"
#include "Sal/SalJson.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

struct FLoomleBridgeRpcTestAccess
{
    static FString Handle(FLoomleBridgeModule& Module, const FString& Request)
    {
        return Module.HandleRequest(1, Request);
    }

    static int32 ActiveGameThreadDispatchCount(
        const FLoomleBridgeModule& Module)
    {
        return Module.ActiveGameThreadDispatchCount.GetValue();
    }

    static void SetShuttingDown(
        FLoomleBridgeModule& Module,
        const bool bShuttingDown)
    {
        Module.bIsShuttingDown.Store(bShuttingDown);
    }

    static void InitializeRequestCancellation(
        FLoomleBridgeModule& Module)
    {
        Module.RequestCancellationRegistry =
            MakeUnique<
                Loomle::Runtime::FRequestCancellationRegistry>();
    }

    static void InitializePythonExecutionService(
        FLoomleBridgeModule& Module)
    {
        Module.PythonExecutionService =
            MakeUnique<Loomle::Python::FPythonExecutionService>();
        Module.PythonExecutionService->Startup();
    }

    static void ShutdownPythonExecutionService(
        FLoomleBridgeModule& Module)
    {
        if (Module.PythonExecutionService)
        {
            Module.PythonExecutionService->Shutdown();
            Module.PythonExecutionService.Reset();
        }
    }

    static bool HasPendingPythonExecution(
        const FLoomleBridgeModule& Module)
    {
        if (!Module.PythonExecutionService)
        {
            return false;
        }
        FScopeLock Lock(&Module.PythonExecutionService->Mutex);
        return Module.PythonExecutionService->PendingExecution.IsValid();
    }

    static void TickPythonExecution(
        FLoomleBridgeModule& Module)
    {
        if (Module.PythonExecutionService)
        {
            Module.PythonExecutionService->TickExecution(0.0f);
        }
    }

    static void InitializeRuntimeIdentity(FLoomleBridgeModule& Module)
    {
        Module.InitializeRuntimeIdentity();
    }

    static const FString& RuntimeId(const FLoomleBridgeModule& Module)
    {
        return Module.RuntimeId;
    }

    static const FString& ProjectId(const FLoomleBridgeModule& Module)
    {
        return Module.ProjectId;
    }

    static const FString& ProjectRoot(const FLoomleBridgeModule& Module)
    {
        return Module.ProjectRoot;
    }

    static const FString& RuntimeEndpoint(const FLoomleBridgeModule& Module)
    {
        return Module.RuntimeEndpoint;
    }

    static FString MakeProjectIdForNormalizedRoot(const FString& Root, const bool bFoldCase)
    {
        return FLoomleBridgeModule::MakeProjectIdForNormalizedRoot(Root, bFoldCase);
    }

    static ELoomleBridgeLifecycle ResolveBridgeLifecycle(
        const ELoomleBridgeLifecycle CurrentLifecycle,
        const ELoomlePipeListenerState ListenerState,
        const bool bEditorInitialized = true)
    {
        return FLoomleBridgeModule::ResolveBridgeLifecycle(
            CurrentLifecycle,
            ListenerState,
            bEditorInitialized);
    }

    static bool RemoveLegacyProjectRegistration(
        const FString& Directory,
        const FString& Root,
        const FString& ProjectId,
        const bool bFoldCase)
    {
        return FLoomleBridgeModule::RemoveLegacyProjectRegistration(
            Directory,
            Root,
            ProjectId,
            bFoldCase);
    }

    static void RecordGameThreadProgress(FLoomleBridgeModule& Module)
    {
        Module.RecordGameThreadProgress();
    }

    static uint64 GameThreadProgressSequence(
        const FLoomleBridgeModule& Module)
    {
        return Module.GameThreadProgressSequence.load();
    }

    static uint64 LastGameThreadProgressCycles(
        const FLoomleBridgeModule& Module)
    {
        return Module.LastGameThreadProgressCycles.load();
    }

    static void SetBridgeLifecycle(
        FLoomleBridgeModule& Module,
        const ELoomleBridgeLifecycle Lifecycle)
    {
        Module.BridgeLifecycleState.store(static_cast<uint8>(Lifecycle));
    }

    static TSharedPtr<FJsonObject> DispatchTool(
        FLoomleBridgeModule& Module,
        const FString& Name,
        const TSharedPtr<FJsonObject>& Arguments,
        bool& bOutIsError)
    {
        return Module.DispatchTool(Name, Arguments, bOutIsError);
    }
};

namespace
{
TSharedPtr<FJsonObject> ParseResponse(FAutomationTestBase& Test, const FString& Response)
{
    TSharedPtr<FJsonObject> Object;
    const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Response);
    Test.TestTrue(
        TEXT("RPC response is valid JSON"),
        FJsonSerializer::Deserialize(Reader, Object) && Object.IsValid());
    return Object;
}

FString MakeRequest(const FString& Method, const FString& Params)
{
    return FString::Printf(
        TEXT("{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"%s\",\"params\":%s}"),
        *Method,
        *Params);
}

bool WaitForActiveGameThreadDispatch(
    const FLoomleBridgeModule& Module,
    const TFuture<FString>& Future,
    const double TimeoutSeconds = 5.0)
{
    const double Deadline = FPlatformTime::Seconds() + TimeoutSeconds;
    while (FLoomleBridgeRpcTestAccess::ActiveGameThreadDispatchCount(Module) == 0
        && !Future.IsReady()
        && FPlatformTime::Seconds() < Deadline)
    {
        FPlatformProcess::SleepNoStats(0.001f);
    }
    return FLoomleBridgeRpcTestAccess::ActiveGameThreadDispatchCount(Module) > 0;
}

bool WaitForRpcWorker(
    const TFuture<FString>& Future,
    const double TimeoutSeconds = 5.0)
{
    const double Deadline = FPlatformTime::Seconds() + TimeoutSeconds;
    while (!Future.IsReady() && FPlatformTime::Seconds() < Deadline)
    {
        FPlatformProcess::SleepNoStats(0.001f);
    }
    return Future.IsReady();
}

struct FPythonDispatchResult
{
    TSharedPtr<FJsonObject> Payload;
    bool bIsError = false;
};

bool DispatchPythonFromWorker(
    FAutomationTestBase& Test,
    FLoomleBridgeModule& Module,
    const TSharedPtr<FJsonObject>& Arguments,
    FPythonDispatchResult& OutResult)
{
    TFuture<FPythonDispatchResult> Future = Async(
        EAsyncExecution::Thread,
        [&Module, Arguments]()
        {
            FPythonDispatchResult Result;
            Result.Payload = FLoomleBridgeRpcTestAccess::DispatchTool(
                Module,
                TEXT("python.run"),
                Arguments,
                Result.bIsError);
            return Result;
        });

    const double PendingDeadline = FPlatformTime::Seconds() + 5.0;
    while (!FLoomleBridgeRpcTestAccess::HasPendingPythonExecution(Module)
        && !Future.IsReady()
        && FPlatformTime::Seconds() < PendingDeadline)
    {
        FPlatformProcess::SleepNoStats(0.001f);
    }

    const bool bPending = FLoomleBridgeRpcTestAccess::HasPendingPythonExecution(Module);
    Test.TestTrue(
        TEXT("Python execution reaches the Core Ticker pending slot"),
        bPending);
    Test.TestEqual(
        TEXT("Python execution is not submitted as a Game Thread TaskGraph task"),
        FLoomleBridgeRpcTestAccess::ActiveGameThreadDispatchCount(Module),
        0);
    Test.TestFalse(
        TEXT("The test enters Python outside Game Thread TaskGraph processing"),
        FTaskGraphInterface::Get().IsThreadProcessingTasks(ENamedThreads::GameThread));
    if (bPending)
    {
        FLoomleBridgeRpcTestAccess::TickPythonExecution(Module);
    }

    const double CompletionDeadline = FPlatformTime::Seconds() + 5.0;
    while (!Future.IsReady() && FPlatformTime::Seconds() < CompletionDeadline)
    {
        FPlatformProcess::SleepNoStats(0.001f);
    }
    if (!Future.IsReady())
    {
        Test.AddError(TEXT("Python RPC worker did not return after Core Ticker execution."));
        return false;
    }
    OutResult = Future.Get();
    return bPending;
}

void DrainGameThreadDispatches(FLoomleBridgeModule& Module)
{
    const double Deadline = FPlatformTime::Seconds() + 5.0;
    while (FLoomleBridgeRpcTestAccess::ActiveGameThreadDispatchCount(Module) > 0
        && FPlatformTime::Seconds() < Deadline)
    {
        FTaskGraphInterface::Get().ProcessThreadUntilIdle(ENamedThreads::GameThread);
        FPlatformProcess::SleepNoStats(0.001f);
    }
}

FString RpcDiagnosticCode(
    FAutomationTestBase& Test,
    const TSharedPtr<FJsonObject>& Response)
{
    const TSharedPtr<FJsonObject>* Error = nullptr;
    const TSharedPtr<FJsonObject>* Data = nullptr;
    if (!Response.IsValid()
        || !Response->TryGetObjectField(TEXT("error"), Error)
        || Error == nullptr
        || !(*Error).IsValid()
        || !(*Error)->TryGetObjectField(TEXT("data"), Data)
        || Data == nullptr
        || !(*Data).IsValid())
    {
        Test.AddError(TEXT("RPC response did not contain public error data."));
        return FString();
    }

    FString Code;
    if (!(*Data)->TryGetStringField(TEXT("code"), Code))
    {
        Test.AddError(TEXT("RPC error data did not contain a diagnostic code."));
    }
    return Code;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FLoomleBridgeRpcProtocolBoundaryTest,
    "Loomle.Runtime.Rpc.ProtocolBoundary",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLoomleBridgeRpcProtocolBoundaryTest::RunTest(const FString& Parameters)
{
    FLoomleBridgeModule Module;
    FLoomleBridgeRpcTestAccess::InitializeRequestCancellation(Module);

    for (const FString& Method : {TEXT("ping"), TEXT("rpc.health"), TEXT("rpc.capabilities")})
    {
        const TSharedPtr<FJsonObject> Response = ParseResponse(
            *this,
            FLoomleBridgeRpcTestAccess::Handle(Module, MakeRequest(Method, TEXT("{}"))));
        if (!Response.IsValid())
        {
            continue;
        }

        TestFalse(
            *FString::Printf(TEXT("%s remains open for discovery"), *Method),
            Response->HasField(TEXT("error")));
        const TSharedPtr<FJsonObject>* Result = nullptr;
        TestTrue(
            *FString::Printf(TEXT("%s returns a result"), *Method),
            Response->TryGetObjectField(TEXT("result"), Result) && Result != nullptr && (*Result).IsValid());
        if (Method != TEXT("ping") && Result != nullptr && (*Result).IsValid())
        {
            double ActualVersion = 0.0;
            TestTrue(
                *FString::Printf(TEXT("%s advertises protocolVersion"), *Method),
                (*Result)->TryGetNumberField(TEXT("protocolVersion"), ActualVersion));
            TestEqual(
                *FString::Printf(TEXT("%s advertises the generated protocolVersion"), *Method),
                ActualVersion,
                static_cast<double>(Loomle::Protocol::Version));
            if (Method == TEXT("rpc.capabilities"))
            {
                const TArray<TSharedPtr<FJsonValue>>* Tools = nullptr;
                TestTrue(
                    TEXT("RPC capabilities advertise private tools"),
                    (*Result)->TryGetArrayField(TEXT("tools"), Tools)
                        && Tools != nullptr);
                if (Tools != nullptr)
                {
                    for (const FString& Tool : {
                             FString(TEXT("editor.open")),
                             FString(TEXT("editor.close")),
                             FString(TEXT("python.run")),
                             FString(TEXT("python.poll"))})
                    {
                        TestTrue(
                            *FString::Printf(
                                TEXT("RPC capabilities advertise %s"),
                                *Tool),
                            Tools->ContainsByPredicate(
                                [&Tool](
                                    const TSharedPtr<FJsonValue>& Value)
                                {
                                    FString Text;
                                    return Value.IsValid()
                                        && Value->TryGetString(Text)
                                        && Text == Tool;
                                }));
                    }
                }
            }
        }
    }

    const TArray<FString> ProtectedMethods = {TEXT("rpc.invoke"), TEXT("rpc.cancel")};
    for (const FString& Method : ProtectedMethods)
    {
        const FString OperationParams = Method == TEXT("rpc.invoke")
            ? TEXT("\"tool\":\"sal.query\",\"args\":{}")
            : TEXT("\"cancellationToken\":\"test-token\"");
        const TArray<FString> InvalidParams = {
            FString::Printf(TEXT("{%s}"), *OperationParams),
            FString::Printf(
                TEXT("{\"protocolVersion\":%d,%s}"),
                Loomle::Protocol::Version - 1,
                *OperationParams),
            FString::Printf(
                TEXT("{\"protocolVersion\":\"%d\",%s}"),
                Loomle::Protocol::Version,
                *OperationParams),
        };
        for (const FString& Params : InvalidParams)
        {
            const TSharedPtr<FJsonObject> Response = ParseResponse(
                *this,
                FLoomleBridgeRpcTestAccess::Handle(Module, MakeRequest(Method, Params)));
            if (!Response.IsValid())
            {
                continue;
            }

            const TSharedPtr<FJsonObject>* Error = nullptr;
            TestTrue(
                *FString::Printf(TEXT("%s rejects an invalid caller protocol"), *Method),
                Response->TryGetObjectField(TEXT("error"), Error) && Error != nullptr && (*Error).IsValid());
            if (Error == nullptr || !(*Error).IsValid())
            {
                continue;
            }

            const TSharedPtr<FJsonObject>* Data = nullptr;
            TestTrue(
                TEXT("Protocol rejection contains public error data"),
                (*Error)->TryGetObjectField(TEXT("data"), Data) && Data != nullptr && (*Data).IsValid());
            if (Data == nullptr || !(*Data).IsValid())
            {
                continue;
            }

            FString Code;
            bool bRetryable = true;
            double Expected = 0.0;
            TestTrue(
                TEXT("Protocol rejection has a public code"),
                (*Data)->TryGetStringField(TEXT("code"), Code));
            TestEqual(
                TEXT("Protocol rejection uses runtime.incompatible"),
                Code,
                FString(TEXT("runtime.incompatible")));
            TestTrue(
                TEXT("Protocol rejection declares retryability"),
                (*Data)->TryGetBoolField(TEXT("retryable"), bRetryable));
            TestFalse(TEXT("Protocol rejection is not retryable"), bRetryable);
            TestTrue(
                TEXT("Protocol rejection declares the expected version"),
                (*Data)->TryGetNumberField(TEXT("expected"), Expected));
            TestEqual(
                TEXT("Protocol rejection expects the generated version"),
                Expected,
                static_cast<double>(Loomle::Protocol::Version));
        }
    }

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FLoomleBridgePythonStructuredResultTest,
    "Loomle.Runtime.Rpc.Python.StructuredResult",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLoomleBridgePythonStructuredResultTest::RunTest(const FString& Parameters)
{
    FLoomleBridgeModule Module;
    FLoomleBridgeRpcTestAccess::InitializePythonExecutionService(Module);
    FLoomleBridgeRpcTestAccess::SetBridgeLifecycle(Module, ELoomleBridgeLifecycle::Ready);

    TSharedPtr<FJsonObject> Arguments = MakeShared<FJsonObject>();
    Arguments->SetStringField(
        TEXT("script"),
        TEXT(
            "import unreal\n"
            "def run():\n"
            "    return {'loaded': unreal is not None, 'items': [1, True, None]}\n"));
    FPythonDispatchResult DispatchResult;
    DispatchPythonFromWorker(*this, Module, Arguments, DispatchResult);
    const TSharedPtr<FJsonObject>& Payload = DispatchResult.Payload;

    TestFalse(TEXT("A valid Python fallback is not a dispatch error"), DispatchResult.bIsError);
    FString Status;
    TestTrue(
        TEXT("Python fallback returns a status"),
        Payload.IsValid() && Payload->TryGetStringField(TEXT("status"), Status));
    TestEqual(TEXT("Fast Python fallback succeeds inline"), Status, FString(TEXT("succeeded")));
    TestFalse(
        TEXT("Fast Python fallback does not expose an execution id"),
        Payload.IsValid() && Payload->HasField(TEXT("executionId")));

    const TSharedPtr<FJsonObject>* Result = nullptr;
    TestTrue(
        TEXT("Python fallback returns the run() dictionary as structured JSON"),
        Payload.IsValid()
            && Payload->TryGetObjectField(TEXT("result"), Result)
            && Result != nullptr
            && (*Result).IsValid());
    if (Result != nullptr && (*Result).IsValid())
    {
        bool bLoaded = false;
        TestTrue(
            TEXT("Structured result preserves a Boolean field"),
            (*Result)->TryGetBoolField(TEXT("loaded"), bLoaded) && bLoaded);
        TestTrue(
            TEXT("Structured result preserves a nested array"),
            (*Result)->HasTypedField<EJson::Array>(TEXT("items")));
    }

    FLoomleBridgeRpcTestAccess::ShutdownPythonExecutionService(Module);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FLoomleBridgePythonSalObjectProjectionTest,
    "Loomle.Runtime.Rpc.Python.SalObjectProjection",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLoomleBridgePythonSalObjectProjectionTest::RunTest(const FString& Parameters)
{
    FLoomleBridgeModule Module;
    FLoomleBridgeRpcTestAccess::InitializePythonExecutionService(Module);
    FLoomleBridgeRpcTestAccess::SetBridgeLifecycle(Module, ELoomleBridgeLifecycle::Ready);

    TSharedPtr<FJsonObject> Arguments = MakeShared<FJsonObject>();
    Arguments->SetStringField(
        TEXT("script"),
        TEXT(
            "import unreal\n"
            "def run():\n"
            "    return {'marked': sal.object(unreal.Actor)}\n"));
    FPythonDispatchResult DispatchResult;
    DispatchPythonFromWorker(*this, Module, Arguments, DispatchResult);
    const TSharedPtr<FJsonObject>& Payload = DispatchResult.Payload;

    TestFalse(TEXT("A sal.object() script is not a dispatch error"), DispatchResult.bIsError);
    FString Status;
    const bool bSucceeded = Payload.IsValid()
        && Payload->TryGetStringField(TEXT("status"), Status)
        && Status == TEXT("succeeded");
    if (!bSucceeded)
    {
        FString Dump = TEXT("(null)");
        if (Payload.IsValid())
        {
            FJsonSerializer::Serialize(
                Payload.ToSharedRef(),
                TJsonWriterFactory<>::Create(&Dump));
        }
        AddError(FString::Printf(
            TEXT("sal.object() script payload: %s"),
            *Dump));
    }
    TestTrue(
        TEXT("The sal.object() script succeeds"),
        bSucceeded);

    const TSharedPtr<FJsonObject>* Annex = nullptr;
    TestTrue(
        TEXT("The projection annex is reported on the Python result"),
        Payload.IsValid()
            && Payload->TryGetObjectField(TEXT("projection"), Annex)
            && Annex != nullptr
            && (*Annex).IsValid());
    bool bComplete = false;
    double Marked = 0.0;
    double Projected = 0.0;
    TestTrue(
        TEXT("The projection annex is complete with one projected view"),
        Annex != nullptr
            && (*Annex).IsValid()
            && (*Annex)->TryGetBoolField(TEXT("complete"), bComplete)
            && bComplete
            && (*Annex)->TryGetNumberField(TEXT("marked"), Marked)
            && Marked == 1.0
            && (*Annex)->TryGetNumberField(TEXT("projected"), Projected)
            && Projected == 1.0);

    const TSharedPtr<FJsonObject>* Result = nullptr;
    const TSharedPtr<FJsonObject>* Record = nullptr;
    TestTrue(
        TEXT("The marker is replaced in place by a projection record"),
        Payload.IsValid()
            && Payload->TryGetObjectField(TEXT("result"), Result)
            && Result != nullptr
            && (*Result).IsValid()
            && (*Result)->TryGetObjectField(TEXT("marked"), Record)
            && Record != nullptr
            && (*Record).IsValid());
    FString RecordStatus;
    FString Relation;
    TestTrue(
        TEXT("The marked Class record is projected with relation exact"),
        Record != nullptr
            && (*Record)->TryGetStringField(TEXT("status"), RecordStatus)
            && RecordStatus == TEXT("projected")
            && (*Record)->TryGetStringField(TEXT("relation"), Relation)
            && Relation == TEXT("exact"));
    const TSharedPtr<FJsonObject>* View = nullptr;
    const TSharedPtr<FJsonObject>* Binding = nullptr;
    const TSharedPtr<FJsonObject>* Target = nullptr;
    FString Domain;
    TestTrue(
        TEXT("The Class projection carries a canonical class view"),
        Record != nullptr
            && (*Record)->TryGetObjectField(TEXT("view"), View)
            && View != nullptr
            && (*View).IsValid()
            && (*View)->TryGetObjectField(TEXT("target"), Binding)
            && Binding != nullptr
            && (*Binding).IsValid()
            && (*Binding)->TryGetObjectField(TEXT("target"), Target)
            && Target != nullptr
            && (*Target).IsValid()
            && (*Target)->TryGetStringField(TEXT("domain"), Domain)
            && Domain == TEXT("class"));

    FLoomleBridgeRpcTestAccess::ShutdownPythonExecutionService(Module);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FLoomleBridgePythonSalObjectValidationTest,
    "Loomle.Runtime.Rpc.Python.SalObjectValidation",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLoomleBridgePythonSalObjectValidationTest::RunTest(const FString& Parameters)
{
    FLoomleBridgeModule Module;
    FLoomleBridgeRpcTestAccess::InitializePythonExecutionService(Module);
    FLoomleBridgeRpcTestAccess::SetBridgeLifecycle(Module, ELoomleBridgeLifecycle::Ready);

    // A user dict that fabricates the reserved marker key is rejected.
    TSharedPtr<FJsonObject> ReservedArguments = MakeShared<FJsonObject>();
    ReservedArguments->SetStringField(
        TEXT("script"),
        TEXT(
            "def run():\n"
            "    return {'__loomle_sal_object__': '/Script/Engine.Actor'}\n"));
    FPythonDispatchResult ReservedDispatch;
    DispatchPythonFromWorker(*this, Module, ReservedArguments, ReservedDispatch);
    FString ReservedStatus;
    TestTrue(
        TEXT("The reserved marker key fails the runner validation"),
        ReservedDispatch.Payload.IsValid()
            && ReservedDispatch.Payload->TryGetStringField(
                TEXT("status"),
                ReservedStatus)
            && ReservedStatus == TEXT("failed"));
    TestTrue(
        TEXT("The reserved marker key reports runtime.python_invalid_result"),
        ReservedDispatch.Payload.IsValid()
            && ReservedDispatch.Payload->HasField(TEXT("error"))
            && !ReservedDispatch.Payload->HasField(TEXT("projection")));

    // sal.object() with a non-object is rejected.
    TSharedPtr<FJsonObject> NonObjectArguments = MakeShared<FJsonObject>();
    NonObjectArguments->SetStringField(
        TEXT("script"),
        TEXT(
            "def run():\n"
            "    return {'bad': sal.object(42)}\n"));
    FPythonDispatchResult NonObjectDispatch;
    DispatchPythonFromWorker(*this, Module, NonObjectArguments, NonObjectDispatch);
    FString NonObjectStatus;
    TestTrue(
        TEXT("sal.object() on a non-object fails the runner validation"),
        NonObjectDispatch.Payload.IsValid()
            && NonObjectDispatch.Payload->TryGetStringField(
                TEXT("status"),
                NonObjectStatus)
            && NonObjectStatus == TEXT("failed"));

    FLoomleBridgeRpcTestAccess::ShutdownPythonExecutionService(Module);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FLoomleBridgePythonPlaySessionAdmissionTest,
    "Loomle.Runtime.Rpc.Python.PlaySessionAdmission",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLoomleBridgePythonPlaySessionAdmissionTest::RunTest(const FString& Parameters)
{
    if (GEditor == nullptr || GEditor->PlayWorld != nullptr)
    {
        AddError(TEXT("Python play-session admission requires an idle Editor."));
        return false;
    }

    UWorld* const EditorWorld = GEditor->GetEditorWorldContext().World();
    if (EditorWorld == nullptr)
    {
        AddError(TEXT("Python play-session admission requires an Editor World."));
        return false;
    }

    FLoomleBridgeModule Module;
    FLoomleBridgeRpcTestAccess::InitializePythonExecutionService(Module);
    FLoomleBridgeRpcTestAccess::SetBridgeLifecycle(Module, ELoomleBridgeLifecycle::Ready);

    TSharedPtr<FJsonObject> Arguments = MakeShared<FJsonObject>();
    Arguments->SetStringField(
        TEXT("script"),
        TEXT(
            "def run():\n"
            "    return {'executed_during_play': True}\n"));

    FPythonDispatchResult DispatchResult;
    {
        TGuardValue<TObjectPtr<UWorld>> PlayWorldGuard(GEditor->PlayWorld, EditorWorld);
        DispatchPythonFromWorker(*this, Module, Arguments, DispatchResult);
    }

    FString Status;
    TestFalse(TEXT("Python during play is not a dispatch error"), DispatchResult.bIsError);
    TestTrue(
        TEXT("Python during play succeeds through the safe ticker entry"),
        DispatchResult.Payload.IsValid()
            && DispatchResult.Payload->TryGetStringField(TEXT("status"), Status)
            && Status == TEXT("succeeded"));

    const TSharedPtr<FJsonObject>* Result = nullptr;
    bool bExecutedDuringPlay = false;
    TestTrue(
        TEXT("Python during play returns its structured result"),
        DispatchResult.Payload.IsValid()
            && DispatchResult.Payload->TryGetObjectField(TEXT("result"), Result)
            && Result != nullptr
            && (*Result).IsValid()
            && (*Result)->TryGetBoolField(
                TEXT("executed_during_play"),
                bExecutedDuringPlay)
            && bExecutedDuringPlay);

    FLoomleBridgeRpcTestAccess::ShutdownPythonExecutionService(Module);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FLoomleBridgePythonTaskGraphEntryTest,
    "Loomle.Runtime.Rpc.Python.TaskGraphEntryIsDeferred",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLoomleBridgePythonTaskGraphEntryTest::RunTest(const FString& Parameters)
{
    FLoomleBridgeModule Module;
    FLoomleBridgeRpcTestAccess::InitializePythonExecutionService(Module);
    FLoomleBridgeRpcTestAccess::SetBridgeLifecycle(Module, ELoomleBridgeLifecycle::Ready);

    TSharedPtr<FJsonObject> Arguments = MakeShared<FJsonObject>();
    Arguments->SetStringField(
        TEXT("script"),
        TEXT(
            "def run():\n"
            "    return {'entry': 'safe_tick'}\n"));

    TFuture<FPythonDispatchResult> Future = Async(
        EAsyncExecution::Thread,
        [&Module, Arguments]()
        {
            FPythonDispatchResult Result;
            Result.Payload = FLoomleBridgeRpcTestAccess::DispatchTool(
                Module,
                TEXT("python.run"),
                Arguments,
                Result.bIsError);
            return Result;
        });

    const double PendingDeadline = FPlatformTime::Seconds() + 5.0;
    while (!FLoomleBridgeRpcTestAccess::HasPendingPythonExecution(Module)
        && !Future.IsReady()
        && FPlatformTime::Seconds() < PendingDeadline)
    {
        FPlatformProcess::SleepNoStats(0.001f);
    }
    TestTrue(
        TEXT("Python reaches the pending slot before the unsafe entry probe"),
        FLoomleBridgeRpcTestAccess::HasPendingPythonExecution(Module));

    bool bUnsafeTickAttempted = false;
    AsyncTask(
        ENamedThreads::GameThread,
        [&Module, &bUnsafeTickAttempted]()
        {
            bUnsafeTickAttempted = true;
            FLoomleBridgeRpcTestAccess::TickPythonExecution(Module);
        });
    FTaskGraphInterface::Get().ProcessThreadUntilIdle(ENamedThreads::GameThread);

    TestTrue(TEXT("The TaskGraph entry probe ran"), bUnsafeTickAttempted);
    TestTrue(
        TEXT("TaskGraph entry leaves Python pending for a safe Engine tick"),
        FLoomleBridgeRpcTestAccess::HasPendingPythonExecution(Module));
    TestFalse(TEXT("TaskGraph entry does not complete the Python request"), Future.IsReady());

    FLoomleBridgeRpcTestAccess::TickPythonExecution(Module);
    const double CompletionDeadline = FPlatformTime::Seconds() + 5.0;
    while (!Future.IsReady() && FPlatformTime::Seconds() < CompletionDeadline)
    {
        FPlatformProcess::SleepNoStats(0.001f);
    }
    TestTrue(TEXT("A safe tick completes the deferred Python request"), Future.IsReady());
    if (Future.IsReady())
    {
        const FPythonDispatchResult Result = Future.Get();
        FString Status;
        TestFalse(TEXT("The deferred execution is not a dispatch error"), Result.bIsError);
        TestTrue(
            TEXT("The deferred execution succeeds"),
            Result.Payload.IsValid()
                && Result.Payload->TryGetStringField(TEXT("status"), Status)
                && Status == TEXT("succeeded"));
    }

    FLoomleBridgeRpcTestAccess::ShutdownPythonExecutionService(Module);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FLoomleBridgePythonInvalidResultTest,
    "Loomle.Runtime.Rpc.Python.InvalidResult",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLoomleBridgePythonInvalidResultTest::RunTest(const FString& Parameters)
{
    FLoomleBridgeModule Module;
    FLoomleBridgeRpcTestAccess::InitializePythonExecutionService(Module);
    FLoomleBridgeRpcTestAccess::SetBridgeLifecycle(Module, ELoomleBridgeLifecycle::Ready);

    TSharedPtr<FJsonObject> Arguments = MakeShared<FJsonObject>();
    Arguments->SetStringField(
        TEXT("script"),
        TEXT(
            "import unreal\n"
            "def run():\n"
            "    return {'value': unreal.Vector()}\n"));
    FPythonDispatchResult DispatchResult;
    DispatchPythonFromWorker(*this, Module, Arguments, DispatchResult);
    const TSharedPtr<FJsonObject>& Payload = DispatchResult.Payload;

    TestFalse(TEXT("An executed Python failure is a structured result"), DispatchResult.bIsError);
    FString Status;
    TestTrue(
        TEXT("Invalid Python result returns a status"),
        Payload.IsValid() && Payload->TryGetStringField(TEXT("status"), Status));
    TestEqual(TEXT("Invalid Python result fails"), Status, FString(TEXT("failed")));
    bool bStateMayHaveChanged = false;
    TestTrue(
        TEXT("Executed Python failure reports outcome uncertainty"),
        Payload.IsValid()
            && Payload->TryGetBoolField(TEXT("stateMayHaveChanged"), bStateMayHaveChanged)
            && bStateMayHaveChanged);
    const TSharedPtr<FJsonObject>* Error = nullptr;
    FString Code;
    TestTrue(
        TEXT("Invalid Python result returns a structured error"),
        Payload.IsValid()
            && Payload->TryGetObjectField(TEXT("error"), Error)
            && Error != nullptr
            && (*Error).IsValid()
            && (*Error)->TryGetStringField(TEXT("code"), Code));
    TestEqual(
        TEXT("Invalid Python result uses its stable diagnostic"),
        Code,
        FString(TEXT("runtime.python_invalid_result")));

    FLoomleBridgeRpcTestAccess::ShutdownPythonExecutionService(Module);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FLoomleBridgePythonPartialFailureRecoveryTest,
    "Loomle.Runtime.Rpc.Python.PartialFailureRecoveryIsIdempotent",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLoomleBridgePythonPartialFailureRecoveryTest::RunTest(const FString& Parameters)
{
    FLoomleBridgeModule Module;
    FLoomleBridgeRpcTestAccess::InitializePythonExecutionService(Module);
    FLoomleBridgeRpcTestAccess::SetBridgeLifecycle(Module, ELoomleBridgeLifecycle::Ready);

    const FString Suffix = FGuid::NewGuid().ToString(EGuidFormats::Digits).ToLower();
    const FString PackagePath = TEXT("/Game/LoomleTests/PythonRecovery_") + Suffix;
    const FString AssetName = TEXT("M_PartialFailure_") + Suffix;
    const FString AssetPath = PackagePath + TEXT("/") + AssetName;

    TSharedPtr<FJsonObject> FirstArguments = MakeShared<FJsonObject>();
    FirstArguments->SetStringField(
        TEXT("script"),
        FString::Printf(
            TEXT(
                "import unreal\n"
                "def run():\n"
                "    asset_path = '%s'\n"
                "    asset = unreal.load_asset(asset_path)\n"
                "    if asset is None:\n"
                "        asset = unreal.AssetToolsHelpers.get_asset_tools().create_asset(\n"
                "            '%s', '%s', unreal.Material, unreal.MaterialFactoryNew())\n"
                "    if asset is None:\n"
                "        raise RuntimeError('fixture creation failed')\n"
                "    raise RuntimeError('intentional failure after creation')\n"),
            *AssetPath,
            *AssetName,
            *PackagePath));

    FPythonDispatchResult FirstDispatch;
    DispatchPythonFromWorker(*this, Module, FirstArguments, FirstDispatch);
    FString FirstStatus;
    bool bStateMayHaveChanged = false;
    TestFalse(TEXT("Partial Python failure is a structured execution result"), FirstDispatch.bIsError);
    TestTrue(
        TEXT("Partial Python execution fails after crossing the mutation boundary"),
        FirstDispatch.Payload.IsValid()
            && FirstDispatch.Payload->TryGetStringField(TEXT("status"), FirstStatus)
            && FirstStatus == TEXT("failed")
            && FirstDispatch.Payload->TryGetBoolField(
                TEXT("stateMayHaveChanged"),
                bStateMayHaveChanged)
            && bStateMayHaveChanged);

    TSharedPtr<FJsonObject> RecoveryArguments = MakeShared<FJsonObject>();
    RecoveryArguments->SetStringField(
        TEXT("script"),
        FString::Printf(
            TEXT(
                "import unreal\n"
                "def run():\n"
                "    asset_path = '%s'\n"
                "    asset = unreal.load_asset(asset_path)\n"
                "    created = False\n"
                "    if asset is None:\n"
                "        asset = unreal.AssetToolsHelpers.get_asset_tools().create_asset(\n"
                "            '%s', '%s', unreal.Material, unreal.MaterialFactoryNew())\n"
                "        created = asset is not None\n"
                "    return {\n"
                "        'exists': asset is not None,\n"
                "        'created': created,\n"
                "        'reused': asset is not None and not created,\n"
                "        'objectPath': asset.get_path_name() if asset else None,\n"
                "        'classPath': asset.get_class().get_path_name() if asset else None,\n"
                "    }\n"),
            *AssetPath,
            *AssetName,
            *PackagePath));

    FPythonDispatchResult RecoveryDispatch;
    DispatchPythonFromWorker(*this, Module, RecoveryArguments, RecoveryDispatch);
    FString RecoveryStatus;
    const TSharedPtr<FJsonObject>* RecoveryResult = nullptr;
    bool bExists = false;
    bool bCreated = true;
    bool bReused = false;
    TestFalse(TEXT("Idempotent recovery is not a dispatch error"), RecoveryDispatch.bIsError);
    TestTrue(
        TEXT("Recovery succeeds from the state left by the failed execution"),
        RecoveryDispatch.Payload.IsValid()
            && RecoveryDispatch.Payload->TryGetStringField(TEXT("status"), RecoveryStatus)
            && RecoveryStatus == TEXT("succeeded")
            && RecoveryDispatch.Payload->TryGetObjectField(
                TEXT("result"),
                RecoveryResult)
            && RecoveryResult != nullptr
            && (*RecoveryResult).IsValid()
            && (*RecoveryResult)->TryGetBoolField(TEXT("exists"), bExists)
            && bExists
            && (*RecoveryResult)->TryGetBoolField(TEXT("created"), bCreated)
            && !bCreated
            && (*RecoveryResult)->TryGetBoolField(TEXT("reused"), bReused)
            && bReused);

    TSharedPtr<FJsonObject> CleanupArguments = MakeShared<FJsonObject>();
    CleanupArguments->SetStringField(
        TEXT("script"),
        FString::Printf(
            TEXT(
                "import unreal\n"
                "def run():\n"
                "    asset_path = '%s'\n"
                "    deleted = (not unreal.EditorAssetLibrary.does_asset_exist(asset_path)\n"
                "        or unreal.EditorAssetLibrary.delete_asset(asset_path))\n"
                "    return {\n"
                "        'deleted': deleted,\n"
                "        'remaining': unreal.EditorAssetLibrary.does_asset_exist(asset_path),\n"
                "    }\n"),
            *AssetPath));

    FPythonDispatchResult CleanupDispatch;
    DispatchPythonFromWorker(*this, Module, CleanupArguments, CleanupDispatch);
    FString CleanupStatus;
    const TSharedPtr<FJsonObject>* CleanupResult = nullptr;
    bool bRemaining = true;
    TestTrue(
        TEXT("Partial-failure fixture asset is removed"),
        !CleanupDispatch.bIsError
            && CleanupDispatch.Payload.IsValid()
            && CleanupDispatch.Payload->TryGetStringField(TEXT("status"), CleanupStatus)
            && CleanupStatus == TEXT("succeeded")
            && CleanupDispatch.Payload->TryGetObjectField(TEXT("result"), CleanupResult)
            && CleanupResult != nullptr
            && (*CleanupResult).IsValid()
            && (*CleanupResult)->TryGetBoolField(TEXT("remaining"), bRemaining)
            && !bRemaining);

    FLoomleBridgeRpcTestAccess::ShutdownPythonExecutionService(Module);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FLoomleBridgePythonAssetImportTaskTest,
    "Loomle.Runtime.Rpc.Python.AssetImportTaskUsesSafeTickEntry",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLoomleBridgePythonAssetImportTaskTest::RunTest(const FString& Parameters)
{
    FLoomleBridgeModule Module;
    FLoomleBridgeRpcTestAccess::InitializePythonExecutionService(Module);
    FLoomleBridgeRpcTestAccess::SetBridgeLifecycle(Module, ELoomleBridgeLifecycle::Ready);

    TArray<uint8> PngBytes;
    const bool bDecoded = FBase64::Decode(
        TEXT("iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mNk+A8AAQUBAScY42YAAAAASUVORK5CYII="),
        PngBytes);
    TestTrue(TEXT("The embedded PNG fixture decodes"), bDecoded);

    const FString FixtureDirectory = FPaths::Combine(
        FPaths::ProjectSavedDir(),
        TEXT("Loomle"),
        TEXT("Tests"));
    const FString FixturePath = FPaths::Combine(
        FixtureDirectory,
        FString::Printf(
            TEXT("python_import_%s.png"),
            *FGuid::NewGuid().ToString(EGuidFormats::Digits).ToLower()));
    IFileManager::Get().MakeDirectory(*FixtureDirectory, true);
    const bool bFixtureSaved = bDecoded && FFileHelper::SaveArrayToFile(PngBytes, *FixturePath);
    TestTrue(TEXT("The PNG import fixture is staged"), bFixtureSaved);

    const FString DestinationPath = FString::Printf(
        TEXT("/Game/LoomleTests/PythonImport_%s"),
        *FGuid::NewGuid().ToString(EGuidFormats::Digits).ToLower());
    FString EscapedFixturePath = FixturePath.Replace(TEXT("\\"), TEXT("\\\\"));
    EscapedFixturePath.ReplaceInline(TEXT("\""), TEXT("\\\""));

    TSharedPtr<FJsonObject> Arguments = MakeShared<FJsonObject>();
    Arguments->SetStringField(
        TEXT("script"),
        FString::Printf(
            TEXT(
                "import unreal\n"
                "def run():\n"
                "    task = unreal.AssetImportTask()\n"
                "    task.set_editor_property('filename', \"%s\")\n"
                "    task.set_editor_property('destination_path', '%s')\n"
                "    task.set_editor_property('automated', True)\n"
                "    task.set_editor_property('replace_existing', True)\n"
                "    task.set_editor_property('save', False)\n"
                "    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])\n"
                "    objects = task.get_objects()\n"
                "    classes = [obj.get_class().get_name() for obj in objects]\n"
                "    paths = [obj.get_path_name() for obj in objects]\n"
                "    unreal.EditorAssetLibrary.delete_directory('%s')\n"
                "    return {'count': len(objects), 'classes': classes, 'paths': paths}\n"),
            *EscapedFixturePath,
            *DestinationPath,
            *DestinationPath));

    FPythonDispatchResult DispatchResult;
    const bool bDispatched = bFixtureSaved
        && DispatchPythonFromWorker(*this, Module, Arguments, DispatchResult);
    IFileManager::Get().Delete(*FixturePath, false, true);

    FString InitialStatus;
    if (DispatchResult.Payload.IsValid()
        && DispatchResult.Payload->TryGetStringField(TEXT("status"), InitialStatus)
        && InitialStatus == TEXT("running"))
    {
        FString ExecutionId;
        const bool bHasExecutionId =
            DispatchResult.Payload->TryGetStringField(TEXT("executionId"), ExecutionId)
            && !ExecutionId.IsEmpty();
        TestTrue(
            TEXT("A continued asset import exposes its exact execution id"),
            bHasExecutionId);
        if (bHasExecutionId)
        {
            TSharedPtr<FJsonObject> PollArguments = MakeShared<FJsonObject>();
            PollArguments->SetStringField(TEXT("executionId"), ExecutionId);
            DispatchResult.Payload = FLoomleBridgeRpcTestAccess::DispatchTool(
                Module,
                TEXT("python.poll"),
                PollArguments,
                DispatchResult.bIsError);
        }
    }

    TestTrue(TEXT("Asset import Python is dispatched through the safe ticker entry"), bDispatched);
    TestFalse(TEXT("Asset import is not a dispatch error"), DispatchResult.bIsError);
    FString Status;
    TestTrue(
        TEXT("Asset import returns a terminal status"),
        DispatchResult.Payload.IsValid()
            && DispatchResult.Payload->TryGetStringField(TEXT("status"), Status));
    TestEqual(TEXT("AssetImportTask completes without TaskGraph recursion"), Status, FString(TEXT("succeeded")));

    const TSharedPtr<FJsonObject>* Result = nullptr;
    double ImportedCount = 0.0;
    TestTrue(
        TEXT("AssetImportTask returns one imported object"),
        DispatchResult.Payload.IsValid()
            && DispatchResult.Payload->TryGetObjectField(TEXT("result"), Result)
            && Result != nullptr
            && (*Result).IsValid()
            && (*Result)->TryGetNumberField(TEXT("count"), ImportedCount)
            && ImportedCount == 1.0);
    if (Result != nullptr && (*Result).IsValid())
    {
        const TArray<TSharedPtr<FJsonValue>>* Classes = nullptr;
        TestTrue(
            TEXT("The imported object is a Texture2D"),
            (*Result)->TryGetArrayField(TEXT("classes"), Classes)
                && Classes != nullptr
                && Classes->Num() == 1
                && (*Classes)[0].IsValid()
                && (*Classes)[0]->AsString() == TEXT("Texture2D"));
    }

    FLoomleBridgeRpcTestAccess::ShutdownPythonExecutionService(Module);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FLoomleBridgeRpcSalQueryTest,
    "Loomle.Runtime.Rpc.SalQueryPublicPath",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLoomleBridgeRpcSalQueryTest::RunTest(const FString& Parameters)
{
    FLoomleBridgeModule Module;
    FLoomleBridgeRpcTestAccess::InitializeRequestCancellation(Module);
    FLoomleBridgeRpcTestAccess::SetBridgeLifecycle(
        Module,
        ELoomleBridgeLifecycle::Ready);

    const FString Params = FString::Printf(
        TEXT(
            "{\"protocolVersion\":%d,\"tool\":\"sal.query\",\"args\":{\"object\":{"
            "\"kind\":\"query\","
            "\"target\":{\"alias\":\"actorClass\",\"target\":{\"kind\":\"target\","
            "\"domain\":\"class\",\"path\":\"/Script/Engine.Actor\"}},"
            "\"operation\":{\"kind\":\"summary\"}}}}"),
        Loomle::Protocol::Version);
    const TSharedPtr<FJsonObject> Response = ParseResponse(
        *this,
        FLoomleBridgeRpcTestAccess::Handle(
            Module,
            MakeRequest(TEXT("rpc.invoke"), Params)));
    if (!Response.IsValid())
    {
        return false;
    }
    TestFalse(
        TEXT("Successful SAL Query is not an RPC error"),
        Response->HasField(TEXT("error")));

    const TSharedPtr<FJsonObject>* Result = nullptr;
    const TSharedPtr<FJsonObject>* Payload = nullptr;
    bool bOk = false;
    bool bIsError = false;
    TestTrue(
        TEXT("rpc.invoke returns its result envelope"),
        Response->TryGetObjectField(TEXT("result"), Result)
            && Result != nullptr
            && (*Result).IsValid());
    if (Result == nullptr || !(*Result).IsValid())
    {
        return false;
    }
    TestTrue(
        TEXT("rpc.invoke reports successful dispatch"),
        (*Result)->TryGetBoolField(TEXT("ok"), bOk) && bOk);
    TestTrue(
        TEXT("rpc.invoke returns the normalized SAL payload"),
        (*Result)->TryGetObjectField(TEXT("payload"), Payload)
            && Payload != nullptr
            && (*Payload).IsValid());
    if (Payload == nullptr || !(*Payload).IsValid())
    {
        return false;
    }
    (*Payload)->TryGetBoolField(TEXT("isError"), bIsError);
    TestFalse(
        TEXT("SAL payload does not report a query error"),
        bIsError);
    TestTrue(
        TEXT("SAL payload contains ordered object text"),
        (*Payload)->HasTypedField<EJson::Object>(TEXT("object")));
    TestTrue(
        TEXT("SAL payload contains diagnostics"),
        (*Payload)->HasTypedField<EJson::Array>(TEXT("diagnostics")));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FLoomleBridgeRpcEditorContextTest,
    "Loomle.Runtime.Rpc.EditorContextPublicPath",
    EAutomationTestFlags::EditorContext
        | EAutomationTestFlags::EngineFilter)

bool FLoomleBridgeRpcEditorContextTest::RunTest(
    const FString& Parameters)
{
    FLoomleBridgeModule Module;
    FLoomleBridgeRpcTestAccess::InitializeRequestCancellation(Module);
    FLoomleBridgeRpcTestAccess::SetBridgeLifecycle(
        Module,
        ELoomleBridgeLifecycle::Ready);

    const FString Params = FString::Printf(
        TEXT(
            "{\"protocolVersion\":%d,"
            "\"tool\":\"editor.context\","
            "\"args\":{}}"),
        Loomle::Protocol::Version);
    const TSharedPtr<FJsonObject> Response = ParseResponse(
        *this,
        FLoomleBridgeRpcTestAccess::Handle(
            Module,
            MakeRequest(TEXT("rpc.invoke"), Params)));
    if (!Response.IsValid())
    {
        return false;
    }
    TestFalse(
        TEXT("Successful Editor Context is not an RPC error"),
        Response->HasField(TEXT("error")));

    const TSharedPtr<FJsonObject>* Result = nullptr;
    const TSharedPtr<FJsonObject>* Payload = nullptr;
    bool bOk = false;
    TestTrue(
        TEXT("rpc.invoke returns its Editor Context envelope"),
        Response->TryGetObjectField(TEXT("result"), Result)
            && Result != nullptr
            && (*Result).IsValid());
    if (Result == nullptr || !(*Result).IsValid())
    {
        return false;
    }
    TestTrue(
        TEXT("rpc.invoke reports successful Editor Context dispatch"),
        (*Result)->TryGetBoolField(TEXT("ok"), bOk) && bOk);
    TestTrue(
        TEXT("rpc.invoke returns the Editor Context payload"),
        (*Result)->TryGetObjectField(TEXT("payload"), Payload)
            && Payload != nullptr
            && (*Payload).IsValid());
    if (Payload == nullptr || !(*Payload).IsValid())
    {
        return false;
    }

    FString TargetContext;
    TestTrue(
        TEXT("Public Editor Context declares Target context"),
        (*Payload)->TryGetStringField(
            TEXT("targetContext"),
            TargetContext));
    TestTrue(
        TEXT("Public Editor Context uses a supported contextual branch"),
        TargetContext == TEXT("exact_target")
            || TargetContext == TEXT("domain_root")
            || TargetContext == TEXT("unresolved_target"));

    TSharedPtr<FJsonObject> ValidationError;
    TestTrue(
        TEXT("Public Editor Context payload satisfies SAL v3 Result schema"),
        Loomle::Sal::FSalJson::ValidateResult(
            *Payload,
            ValidationError));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FLoomleBridgeRpcEditorControlResolutionTest,
    "Loomle.Runtime.Rpc.EditorControlResolutionBoundary",
    EAutomationTestFlags::EditorContext
        | EAutomationTestFlags::EngineFilter)

bool FLoomleBridgeRpcEditorControlResolutionTest::RunTest(
    const FString& Parameters)
{
    FLoomleBridgeModule Module;
    FLoomleBridgeRpcTestAccess::InitializeRequestCancellation(Module);
    FLoomleBridgeRpcTestAccess::SetBridgeLifecycle(
        Module,
        ELoomleBridgeLifecycle::Ready);

    const FString Params = FString::Printf(
        TEXT(
            "{\"protocolVersion\":%d,"
            "\"tool\":\"editor.open\","
            "\"args\":{\"target\":{"
            "\"kind\":\"target\","
            "\"domain\":\"blueprint\","
            "\"asset\":\"/Game/LoomleTests/MissingEditorTarget.MissingEditorTarget\","
            "\"id\":\"11111111-2222-3333-4444-555555555555\"}}}"),
        Loomle::Protocol::Version);
    const TSharedPtr<FJsonObject> Response = ParseResponse(
        *this,
        FLoomleBridgeRpcTestAccess::Handle(
            Module,
            MakeRequest(TEXT("rpc.invoke"), Params)));
    if (!Response.IsValid())
    {
        return false;
    }
    TestFalse(
        TEXT("A syntactically valid unresolved Editor Target is not an RPC error"),
        Response->HasField(TEXT("error")));

    const TSharedPtr<FJsonObject>* Result = nullptr;
    const TSharedPtr<FJsonObject>* Payload = nullptr;
    const TSharedPtr<FJsonObject>* Subject = nullptr;
    const TSharedPtr<FJsonObject>* Outcome = nullptr;
    TestTrue(
        TEXT("Editor control returns its RPC result envelope"),
        Response->TryGetObjectField(TEXT("result"), Result)
            && Result != nullptr
            && (*Result).IsValid());
    if (Result == nullptr || !(*Result).IsValid())
    {
        return false;
    }
    TestTrue(
        TEXT("Editor control returns its private payload"),
        (*Result)->TryGetObjectField(TEXT("payload"), Payload)
            && Payload != nullptr
            && (*Payload).IsValid());
    if (Payload == nullptr || !(*Payload).IsValid())
    {
        return false;
    }
    TestTrue(
        TEXT("Editor control keeps the SAL subject separate"),
        (*Payload)->TryGetObjectField(TEXT("subject"), Subject)
            && Subject != nullptr
            && (*Subject).IsValid());
    TestTrue(
        TEXT("Editor control keeps transient outcome metadata separate"),
        (*Payload)->TryGetObjectField(TEXT("outcome"), Outcome)
            && Outcome != nullptr
            && (*Outcome).IsValid());
    if (Subject == nullptr
        || !(*Subject).IsValid()
        || Outcome == nullptr
        || !(*Outcome).IsValid())
    {
        return false;
    }

    FString TargetContext;
    FString Operation;
    FString Status;
    TestTrue(
        TEXT("Missing content identity returns unresolved_target"),
        (*Subject)->TryGetStringField(
            TEXT("targetContext"),
            TargetContext));
    TestEqual(
        TEXT("Missing content identity uses unresolved_target"),
        TargetContext,
        FString(TEXT("unresolved_target")));
    TestTrue(
        TEXT("Editor outcome identifies open"),
        (*Outcome)->TryGetStringField(TEXT("operation"), Operation));
    TestEqual(
        TEXT("Editor outcome preserves the requested operation"),
        Operation,
        FString(TEXT("open")));
    TestTrue(
        TEXT("Unresolved Editor Target has a failed outcome"),
        (*Outcome)->TryGetStringField(TEXT("status"), Status));
    TestEqual(
        TEXT("Unresolved Editor Target reports failed"),
        Status,
        FString(TEXT("failed")));

    const TArray<TSharedPtr<FJsonValue>>* Diagnostics = nullptr;
    TestTrue(
        TEXT("Unresolved Editor Target returns diagnostics"),
        (*Subject)->TryGetArrayField(
            TEXT("diagnostics"),
            Diagnostics)
            && Diagnostics != nullptr
            && !Diagnostics->IsEmpty());
    if (Diagnostics != nullptr && !Diagnostics->IsEmpty())
    {
        const TSharedPtr<FJsonObject>* Diagnostic = nullptr;
        const TArray<TSharedPtr<FJsonValue>>* Path = nullptr;
        TestTrue(
            TEXT("Editor resolution diagnostic is an object"),
            (*Diagnostics)[0].IsValid()
                && (*Diagnostics)[0]->TryGetObject(Diagnostic)
                && Diagnostic != nullptr
                && (*Diagnostic).IsValid());
        TestTrue(
            TEXT("Editor resolution diagnostic points to public target"),
            Diagnostic != nullptr
                && (*Diagnostic)->TryGetArrayField(TEXT("path"), Path)
                && Path != nullptr
                && Path->Num() == 1
                && (*Path)[0].IsValid()
                && (*Path)[0]->AsString() == TEXT("target"));
    }

    TSharedPtr<FJsonObject> ValidationError;
    TestTrue(
        TEXT("Editor control subject satisfies the SAL Result schema"),
        Loomle::Sal::FSalJson::ValidateResult(
            *Subject,
            ValidationError));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FLoomleBridgeRpcEditorControlCanonicalInputTest,
    "Loomle.Runtime.Rpc.EditorControlCanonicalInput",
    EAutomationTestFlags::EditorContext
        | EAutomationTestFlags::EngineFilter)

bool FLoomleBridgeRpcEditorControlCanonicalInputTest::RunTest(
    const FString& Parameters)
{
    FLoomleBridgeModule Module;
    FLoomleBridgeRpcTestAccess::InitializeRequestCancellation(Module);
    FLoomleBridgeRpcTestAccess::SetBridgeLifecycle(
        Module,
        ELoomleBridgeLifecycle::Ready);

    const FString Params = FString::Printf(
        TEXT(
            "{\"protocolVersion\":%d,"
            "\"tool\":\"editor.close\","
            "\"args\":{\"target\":{"
            "\"kind\":\"target\","
            "\"domain\":\"graph\","
            "\"asset\":\"/Game/LoomleTests/Any.Any\","
            "\"id\":\"aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee\"}}}"),
        Loomle::Protocol::Version);
    const TSharedPtr<FJsonObject> Response = ParseResponse(
        *this,
        FLoomleBridgeRpcTestAccess::Handle(
            Module,
            MakeRequest(TEXT("rpc.invoke"), Params)));
    if (!Response.IsValid())
    {
        return false;
    }
    TestTrue(
        TEXT("Bridge rejects a non-canonical Graph Target at the RPC boundary"),
        Response->HasField(TEXT("error")));
    TestEqual(
        TEXT("Non-canonical private Editor Target uses tool.invalid_arguments"),
        RpcDiagnosticCode(*this, Response),
        FString(TEXT("tool.invalid_arguments")));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FLoomleBridgeRuntimeIdentityTest,
    "Loomle.Runtime.Rpc.UniqueRuntimeIdentity",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLoomleBridgeRuntimeIdentityTest::RunTest(const FString& Parameters)
{
    const FString PosixUpper = FLoomleBridgeRpcTestAccess::MakeProjectIdForNormalizedRoot(TEXT("/Projects/Game"), false);
    const FString PosixLower = FLoomleBridgeRpcTestAccess::MakeProjectIdForNormalizedRoot(TEXT("/projects/game"), false);
    TestEqual(TEXT("POSIX identity has fixed shared parity"), PosixUpper, FString(TEXT("b4b194846d3b053b")));
    TestNotEqual(TEXT("POSIX project identity preserves path case"), PosixUpper, PosixLower);

    const FString WindowsCanonical =
        FLoomleBridgeRpcTestAccess::MakeProjectIdForNormalizedRoot(TEXT("c:/projects/game"), true);
    const FString WindowsUpper =
        FLoomleBridgeRpcTestAccess::MakeProjectIdForNormalizedRoot(TEXT("C:/Projects/Game"), true);
    TestEqual(
        TEXT("Windows identity has fixed shared parity"),
        WindowsCanonical,
        FString(TEXT("991397ad6b7a080a")));
    TestEqual(TEXT("Windows project identity folds path case"), WindowsCanonical, WindowsUpper);

    TestTrue(
        TEXT("a listening runtime remains starting before Editor initialization completes"),
        FLoomleBridgeRpcTestAccess::ResolveBridgeLifecycle(
            ELoomleBridgeLifecycle::Starting,
            ELoomlePipeListenerState::Listening,
            false) == ELoomleBridgeLifecycle::Starting);
    TestTrue(
        TEXT("a listening runtime becomes ready after Editor initialization completes"),
        FLoomleBridgeRpcTestAccess::ResolveBridgeLifecycle(
            ELoomleBridgeLifecycle::Starting,
            ELoomlePipeListenerState::Listening) == ELoomleBridgeLifecycle::Ready);
    TestTrue(
        TEXT("a ready runtime whose listener fails becomes failed"),
        FLoomleBridgeRpcTestAccess::ResolveBridgeLifecycle(
            ELoomleBridgeLifecycle::Ready,
            ELoomlePipeListenerState::Failed) == ELoomleBridgeLifecycle::Failed);
    TestTrue(
        TEXT("a ready runtime whose listener stops becomes failed"),
        FLoomleBridgeRpcTestAccess::ResolveBridgeLifecycle(
            ELoomleBridgeLifecycle::Ready,
            ELoomlePipeListenerState::Stopped) == ELoomleBridgeLifecycle::Failed);
    TestTrue(
        TEXT("draining remains distinct from listener failure"),
        FLoomleBridgeRpcTestAccess::ResolveBridgeLifecycle(
            ELoomleBridgeLifecycle::Draining,
            ELoomlePipeListenerState::Stopped) == ELoomleBridgeLifecycle::Draining);

    FLoomleBridgeModule ProgressModule;
    const uint64 PreviousProgress =
        FLoomleBridgeRpcTestAccess::GameThreadProgressSequence(ProgressModule);
    FLoomleBridgeRpcTestAccess::RecordGameThreadProgress(ProgressModule);
    TestEqual(
        TEXT("completed Game Thread work advances health progress"),
        FLoomleBridgeRpcTestAccess::GameThreadProgressSequence(ProgressModule),
        PreviousProgress + 1);
    TestTrue(
        TEXT("completed Game Thread work refreshes monotonic health time"),
        FLoomleBridgeRpcTestAccess::LastGameThreadProgressCycles(ProgressModule) > 0);

    const FString MigrationRoot = TEXT("/CaseSensitive/Game");
    const FString MigrationProjectId =
        FLoomleBridgeRpcTestAccess::MakeProjectIdForNormalizedRoot(MigrationRoot, false);
    const FString LegacyProjectId =
        FLoomleBridgeRpcTestAccess::MakeProjectIdForNormalizedRoot(MigrationRoot, true);
    const FString MigrationDirectory = FPaths::Combine(
        FPaths::ProjectSavedDir(),
        TEXT("Automation"),
        TEXT("LoomleProjectIdentity"),
        FGuid::NewGuid().ToString(EGuidFormats::Digits));
    IFileManager::Get().MakeDirectory(*MigrationDirectory, true);
    const FString LegacyPath = FPaths::Combine(MigrationDirectory, LegacyProjectId + TEXT(".json"));
    const FString MatchingLegacyRecord = FString::Printf(
        TEXT("{\"projectId\":\"%s\",\"projectRoot\":\"%s\"}"),
        *LegacyProjectId,
        *MigrationRoot);
    TestTrue(
        TEXT("matching legacy project record is created"),
        FFileHelper::SaveStringToFile(MatchingLegacyRecord, *LegacyPath));
    TestTrue(
        TEXT("migration removes only the known legacy identity for the same POSIX root"),
        FLoomleBridgeRpcTestAccess::RemoveLegacyProjectRegistration(
            MigrationDirectory,
            MigrationRoot,
            MigrationProjectId,
            false));
    TestFalse(TEXT("matching legacy record was removed"), FPaths::FileExists(LegacyPath));

    const FString DistinctRootRecord = FString::Printf(
        TEXT("{\"projectId\":\"%s\",\"projectRoot\":\"/casesensitive/game\"}"),
        *LegacyProjectId);
    TestTrue(
        TEXT("distinct case-sensitive project record is created"),
        FFileHelper::SaveStringToFile(DistinctRootRecord, *LegacyPath));
    TestFalse(
        TEXT("migration preserves a distinct POSIX project differing only by case"),
        FLoomleBridgeRpcTestAccess::RemoveLegacyProjectRegistration(
            MigrationDirectory,
            MigrationRoot,
            MigrationProjectId,
            false));
    TestTrue(TEXT("distinct project record remains"), FPaths::FileExists(LegacyPath));
    IFileManager::Get().DeleteDirectory(*MigrationDirectory, false, true);

    FLoomleBridgeModule First;
    FLoomleBridgeModule Second;
    FLoomleBridgeRpcTestAccess::InitializeRuntimeIdentity(First);
    FLoomleBridgeRpcTestAccess::InitializeRuntimeIdentity(Second);

    const FString ExpectedPlatformProjectId =
        FLoomleBridgeRpcTestAccess::MakeProjectIdForNormalizedRoot(
            FLoomleBridgeRpcTestAccess::ProjectRoot(First),
#if PLATFORM_WINDOWS
            true
#else
            false
#endif
        );

    TestFalse(TEXT("runtimeId is populated"), FLoomleBridgeRpcTestAccess::RuntimeId(First).IsEmpty());
    TestEqual(
        TEXT("runtime identity uses the host platform path-case semantics"),
        FLoomleBridgeRpcTestAccess::ProjectId(First),
        ExpectedPlatformProjectId);
    TestNotEqual(
        TEXT("each Editor process identity is unique"),
        FLoomleBridgeRpcTestAccess::RuntimeId(First),
        FLoomleBridgeRpcTestAccess::RuntimeId(Second));
    TestEqual(
        TEXT("the stable project identity is shared"),
        FLoomleBridgeRpcTestAccess::ProjectId(First),
        FLoomleBridgeRpcTestAccess::ProjectId(Second));
    TestNotEqual(
        TEXT("each runtime owns a unique endpoint"),
        FLoomleBridgeRpcTestAccess::RuntimeEndpoint(First),
        FLoomleBridgeRpcTestAccess::RuntimeEndpoint(Second));

    const TSharedPtr<FJsonObject> Response = ParseResponse(
        *this,
        FLoomleBridgeRpcTestAccess::Handle(First, MakeRequest(TEXT("rpc.health"), TEXT("{}"))));
    const TSharedPtr<FJsonObject>* Result = nullptr;
    if (!Response.IsValid()
        || !Response->TryGetObjectField(TEXT("result"), Result)
        || Result == nullptr
        || !(*Result).IsValid())
    {
        AddError(TEXT("rpc.health did not return a result"));
        return false;
    }

    FString RuntimeId;
    FString ProjectId;
    FString ProjectRoot;
    FString Lifecycle;
    FString ListenerState;
    TestTrue(TEXT("health returns runtimeId"), (*Result)->TryGetStringField(TEXT("runtimeId"), RuntimeId));
    TestTrue(TEXT("health returns projectId"), (*Result)->TryGetStringField(TEXT("projectId"), ProjectId));
    TestTrue(TEXT("health returns projectRoot"), (*Result)->TryGetStringField(TEXT("projectRoot"), ProjectRoot));
    TestTrue(TEXT("health returns lifecycle"), (*Result)->TryGetStringField(TEXT("lifecycle"), Lifecycle));
    TestTrue(TEXT("health returns listenerState"), (*Result)->TryGetStringField(TEXT("listenerState"), ListenerState));
    TestEqual(TEXT("health runtimeId is exact"), RuntimeId, FLoomleBridgeRpcTestAccess::RuntimeId(First));
    TestEqual(TEXT("health projectId is exact"), ProjectId, FLoomleBridgeRpcTestAccess::ProjectId(First));
    TestEqual(TEXT("health projectRoot is normalized and exact"), ProjectRoot, FLoomleBridgeRpcTestAccess::ProjectRoot(First));
    TestEqual(TEXT("a non-started module is offline"), Lifecycle, FString(TEXT("offline")));
    TestEqual(TEXT("a non-started listener is stopped"), ListenerState, FString(TEXT("stopped")));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FLoomleBridgeCompletedDispatchProgressTest,
    "Loomle.Runtime.Rpc.CompletedDispatchRefreshesGameThreadProgress",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLoomleBridgeCompletedDispatchProgressTest::RunTest(const FString& Parameters)
{
    TestTrue(TEXT("Automation dispatch probe starts on the Game Thread"), IsInGameThread());

    FLoomleBridgeModule Module;
    FLoomleBridgeRpcTestAccess::SetBridgeLifecycle(Module, ELoomleBridgeLifecycle::Ready);
    const uint64 PreviousProgress =
        FLoomleBridgeRpcTestAccess::GameThreadProgressSequence(Module);

    TFuture<bool> DispatchFuture = Async(
        EAsyncExecution::ThreadPool,
        [&Module]()
        {
            bool bDispatchError = false;
            const TSharedPtr<FJsonObject> Payload =
                FLoomleBridgeRpcTestAccess::DispatchTool(
                    Module,
                    TEXT("loomle_test_unknown_tool"),
                    MakeShared<FJsonObject>(),
                    bDispatchError);
            FString Code;
            return bDispatchError
                && Payload.IsValid()
                && Payload->TryGetStringField(TEXT("code"), Code)
                && Code == TEXT("tool.unknown");
        });

    const double Deadline = FPlatformTime::Seconds() + 5.0;
    while (!DispatchFuture.IsReady() && FPlatformTime::Seconds() < Deadline)
    {
        FTaskGraphInterface::Get().ProcessThreadUntilIdle(ENamedThreads::GameThread);
        FPlatformProcess::SleepNoStats(0.001f);
    }

    TestTrue(TEXT("worker dispatch completed through the Game Thread"), DispatchFuture.IsReady());
    if (!DispatchFuture.IsReady())
    {
        return false;
    }

    TestTrue(TEXT("worker dispatch returned the expected tool error"), DispatchFuture.Get());
    TestEqual(
        TEXT("completed worker dispatch advances health progress"),
        FLoomleBridgeRpcTestAccess::GameThreadProgressSequence(Module),
        PreviousProgress + 1);
    TestTrue(
        TEXT("completed worker dispatch refreshes monotonic health time"),
        FLoomleBridgeRpcTestAccess::LastGameThreadProgressCycles(Module) > 0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FLoomleBridgeInFlightCancellationTest,
    "Loomle.Runtime.Rpc.InFlightCancellationPreventsGameThreadAdmission",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLoomleBridgeInFlightCancellationTest::RunTest(const FString& Parameters)
{
    TestTrue(TEXT("Automation cancellation probe starts on the Game Thread"), IsInGameThread());

    FLoomleBridgeModule Module;
    FLoomleBridgeRpcTestAccess::InitializeRequestCancellation(Module);
    FLoomleBridgeRpcTestAccess::SetBridgeLifecycle(Module, ELoomleBridgeLifecycle::Ready);

    const FString CancellationToken =
        FString::Printf(TEXT("automation-%s"), *FGuid::NewGuid().ToString(EGuidFormats::Digits));
    const FString InvokeParams = FString::Printf(
        TEXT(
            "{\"protocolVersion\":%d,\"tool\":\"sal.query\","
            "\"cancellationToken\":\"%s\",\"args\":{}}"),
        Loomle::Protocol::Version,
        *CancellationToken);
    TFuture<FString> InvokeFuture = Async(
        EAsyncExecution::Thread,
        [&Module, InvokeParams]()
        {
            return FLoomleBridgeRpcTestAccess::Handle(
                Module,
                MakeRequest(TEXT("rpc.invoke"), InvokeParams));
        });

    const bool bQueued =
        WaitForActiveGameThreadDispatch(Module, InvokeFuture);
    TestTrue(
        TEXT("SAL Query reached the in-flight Game Thread admission boundary"),
        bQueued);

    const FString CancelParams = FString::Printf(
        TEXT("{\"protocolVersion\":%d,\"cancellationToken\":\"%s\"}"),
        Loomle::Protocol::Version,
        *CancellationToken);
    const TSharedPtr<FJsonObject> CancelResponse = ParseResponse(
        *this,
        FLoomleBridgeRpcTestAccess::Handle(
            Module,
            MakeRequest(TEXT("rpc.cancel"), CancelParams)));
    const TSharedPtr<FJsonObject>* CancelResult = nullptr;
    bool bCancelled = false;
    TestTrue(
        TEXT("rpc.cancel returns a result"),
        CancelResponse.IsValid()
            && CancelResponse->TryGetObjectField(TEXT("result"), CancelResult)
            && CancelResult != nullptr
            && (*CancelResult).IsValid());
    if (CancelResult != nullptr && (*CancelResult).IsValid())
    {
        TestTrue(
            TEXT("rpc.cancel confirms cancellation"),
            (*CancelResult)->TryGetBoolField(TEXT("cancelled"), bCancelled)
                && bCancelled);
    }

    bool bWorkerCompleted = WaitForRpcWorker(InvokeFuture);
    TestTrue(
        TEXT("the cancelled worker returns without waiting for the Game Thread"),
        bWorkerCompleted);
    if (!bWorkerCompleted)
    {
        DrainGameThreadDispatches(Module);
        bWorkerCompleted = WaitForRpcWorker(InvokeFuture);
        TestTrue(
            TEXT("the cancelled worker retires after its queued task is drained"),
            bWorkerCompleted);
        if (!bWorkerCompleted)
        {
            return false;
        }
    }

    const TSharedPtr<FJsonObject> InvokeResponse =
        ParseResponse(*this, InvokeFuture.Get());
    TestEqual(
        TEXT("the public RPC reports cooperative cancellation"),
        RpcDiagnosticCode(*this, InvokeResponse),
        FString(TEXT("runtime.request_cancelled")));

    // The queued task still owns the module until it observes the cancelled
    // one-shot admission. Drain it before this stack-owned module is destroyed.
    DrainGameThreadDispatches(Module);
    TestEqual(
        TEXT("the cancelled queued task retires without executing the provider"),
        FLoomleBridgeRpcTestAccess::ActiveGameThreadDispatchCount(Module),
        0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FLoomleBridgeInFlightShutdownTest,
    "Loomle.Runtime.Rpc.InFlightShutdownPreventsGameThreadAdmission",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLoomleBridgeInFlightShutdownTest::RunTest(const FString& Parameters)
{
    TestTrue(TEXT("Automation shutdown probe starts on the Game Thread"), IsInGameThread());

    FLoomleBridgeModule Module;
    FLoomleBridgeRpcTestAccess::InitializeRequestCancellation(Module);
    FLoomleBridgeRpcTestAccess::SetBridgeLifecycle(Module, ELoomleBridgeLifecycle::Ready);

    const FString InvokeParams = FString::Printf(
        TEXT("{\"protocolVersion\":%d,\"tool\":\"sal.patch\",\"args\":{}}"),
        Loomle::Protocol::Version);
    TFuture<FString> InvokeFuture = Async(
        EAsyncExecution::Thread,
        [&Module, InvokeParams]()
        {
            return FLoomleBridgeRpcTestAccess::Handle(
                Module,
                MakeRequest(TEXT("rpc.invoke"), InvokeParams));
        });

    const bool bQueued =
        WaitForActiveGameThreadDispatch(Module, InvokeFuture);
    TestTrue(
        TEXT("SAL Patch reached the in-flight Game Thread admission boundary"),
        bQueued);

    FLoomleBridgeRpcTestAccess::SetShuttingDown(Module, true);
    bool bWorkerCompleted = WaitForRpcWorker(InvokeFuture);
    TestTrue(
        TEXT("the in-flight worker observes shutdown without Game Thread execution"),
        bWorkerCompleted);
    if (!bWorkerCompleted)
    {
        DrainGameThreadDispatches(Module);
        bWorkerCompleted = WaitForRpcWorker(InvokeFuture);
        TestTrue(
            TEXT("the shutdown worker retires after its queued task is drained"),
            bWorkerCompleted);
        if (!bWorkerCompleted)
        {
            return false;
        }
    }

    const TSharedPtr<FJsonObject> InvokeResponse =
        ParseResponse(*this, InvokeFuture.Get());
    TestEqual(
        TEXT("the public RPC reports Editor shutdown"),
        RpcDiagnosticCode(*this, InvokeResponse),
        FString(TEXT("runtime.editor_shutting_down")));

    DrainGameThreadDispatches(Module);
    TestEqual(
        TEXT("the shutdown-cancelled queued task retires without executing the provider"),
        FLoomleBridgeRpcTestAccess::ActiveGameThreadDispatchCount(Module),
        0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FLoomleBridgeGameThreadAdmissionTest,
    "Loomle.Runtime.Rpc.GameThreadAdmissionIsOneShot",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLoomleBridgeGameThreadAdmissionTest::RunTest(const FString& Parameters)
{
    Loomle::Runtime::FGameThreadAdmission Cancelled;
    TestTrue(TEXT("a waiting request can be cancelled"), Cancelled.TryCancel());
    TestFalse(TEXT("a cancelled request cannot execute later"), Cancelled.TryStart());
    TestTrue(
        TEXT("cancelled state is retained"),
        Cancelled.GetState() == Loomle::Runtime::EGameThreadAdmissionState::Cancelled);

    Loomle::Runtime::FGameThreadAdmission Started;
    TestTrue(TEXT("a waiting request can be admitted"), Started.TryStart());
    TestFalse(TEXT("an admitted request cannot be retrospectively cancelled"), Started.TryCancel());
    TestTrue(
        TEXT("started state is retained"),
        Started.GetState() == Loomle::Runtime::EGameThreadAdmissionState::Started);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FLoomleBridgeConcurrentGameThreadAdmissionTest,
    "Loomle.Runtime.Rpc.GameThreadAdmissionHasSingleConcurrentWinner",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLoomleBridgeConcurrentGameThreadAdmissionTest::RunTest(const FString& Parameters)
{
    using namespace Loomle::Runtime;

    constexpr int32 ParticipantCount = 8;
    FGameThreadAdmission Admission;
    FThreadSafeCounter ReadyCount;
    TAtomic<bool> bStartRace { false };
    TArray<TFuture<int32>> Futures;
    Futures.Reserve(ParticipantCount);

    for (int32 Index = 0; Index < ParticipantCount; ++Index)
    {
        Futures.Add(Async(
            EAsyncExecution::Thread,
            [&Admission, &ReadyCount, &bStartRace, Index]()
            {
                ReadyCount.Increment();
                while (!bStartRace.Load())
                {
                    FPlatformProcess::SleepNoStats(0.0005f);
                }

                const bool bWon = (Index % 2 == 0)
                    ? Admission.TryStart()
                    : Admission.TryCancel();
                if (!bWon)
                {
                    return 0;
                }
                return Index % 2 == 0 ? 1 : 2;
            }));
    }

    const double ReadyDeadline = FPlatformTime::Seconds() + 5.0;
    while (ReadyCount.GetValue() < ParticipantCount
        && FPlatformTime::Seconds() < ReadyDeadline)
    {
        FPlatformProcess::SleepNoStats(0.001f);
    }
    const bool bAllReady = ReadyCount.GetValue() == ParticipantCount;
    TestTrue(TEXT("all admission contenders reached the race"), bAllReady);
    bStartRace.Store(true);

    int32 WinnerCount = 0;
    int32 WinnerKind = 0;
    for (TFuture<int32>& Future : Futures)
    {
        const int32 Result = Future.Get();
        if (Result != 0)
        {
            ++WinnerCount;
            WinnerKind = Result;
        }
    }

    TestEqual(TEXT("exactly one concurrent admission transition wins"), WinnerCount, 1);
    const EGameThreadAdmissionState FinalState = Admission.GetState();
    TestTrue(
        TEXT("the retained state matches the winning transition"),
        (WinnerKind == 1 && FinalState == EGameThreadAdmissionState::Started)
            || (WinnerKind == 2 && FinalState == EGameThreadAdmissionState::Cancelled));
    return bAllReady;
}

#endif
