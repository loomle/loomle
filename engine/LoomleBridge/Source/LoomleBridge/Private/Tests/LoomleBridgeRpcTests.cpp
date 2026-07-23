// Copyright 2026 Loomle contributors.

#if WITH_DEV_AUTOMATION_TESTS

#include "LoomleBridgeModule.h"

#include "Async/Async.h"
#include "Async/TaskGraphInterfaces.h"
#include "Dom/JsonObject.h"
#include "Generated/LoomleProtocolVersion.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTime.h"
#include "LoomleGameThreadAdmission.h"
#include "LoomlePipeServer.h"
#include "LoomleRequestCancellation.h"
#include "HAL/FileManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Guid.h"
#include "Misc/Paths.h"
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
            "\"target\":{\"alias\":\"actorClass\",\"value\":{\"kind\":\"call\","
            "\"callee\":\"class\",\"args\":{\"path\":\"/Script/Engine.Actor\"}}},"
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
