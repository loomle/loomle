// Copyright 2026 Loomle contributors.

#if WITH_DEV_AUTOMATION_TESTS

#include "LoomleRequestCancellation.h"

#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FLoomleRequestCancellationScopeTest,
    "Loomle.Runtime.RequestCancellation.Scope",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLoomleRequestCancellationScopeTest::RunTest(const FString& Parameters)
{
    using namespace Loomle::Runtime;

    TestFalse(TEXT("No request scope is not cancelled"), IsRequestCancellationRequested());

    const TSharedRef<FRequestCancellationState, ESPMode::ThreadSafe> OuterState =
        MakeShared<FRequestCancellationState, ESPMode::ThreadSafe>();
    {
        FScopedRequestCancellation OuterScope(OuterState);
        TestFalse(TEXT("Fresh request state is not cancelled"), IsRequestCancellationRequested());
        TestTrue(
            TEXT("Current state is discoverable"),
            GetRequestCancellationState().Get() == &OuterState.Get());

        const TSharedRef<FRequestCancellationState, ESPMode::ThreadSafe> InnerState =
            MakeShared<FRequestCancellationState, ESPMode::ThreadSafe>();
        InnerState->Cancel();
        {
            FScopedRequestCancellation InnerScope(InnerState);
            TestTrue(TEXT("Nested request exposes its own state"), IsRequestCancellationRequested());
        }

        TestFalse(TEXT("Leaving a nested scope restores the outer state"), IsRequestCancellationRequested());
        OuterState->Cancel();
        TestTrue(TEXT("Cancellation is visible inside the active scope"), IsRequestCancellationRequested());
    }

    TestFalse(TEXT("Leaving the request scope clears the thread-local state"), IsRequestCancellationRequested());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FLoomleRequestCancellationRegistryTest,
    "Loomle.Runtime.RequestCancellation.RegistryOrdering",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLoomleRequestCancellationRegistryTest::RunTest(const FString& Parameters)
{
    using namespace Loomle::Runtime;

    FRequestCancellationRegistry Registry;

    Registry.Cancel(TEXT("t:before-register"), 0.0);
    const TSharedRef<FRequestCancellationState, ESPMode::ThreadSafe> CancelledBeforeRegister =
        Registry.Register(1, TEXT("t:before-register"), 1.0);
    TestTrue(
        TEXT("A cancel that overtakes registration is retained"),
        CancelledBeforeRegister->IsCancellationRequested());

    Registry.CloseConnection(2, 2.0);
    const TSharedRef<FRequestCancellationState, ESPMode::ThreadSafe> RegisteredAfterDisconnect =
        Registry.Register(2, TEXT("t:after-disconnect"), 3.0);
    TestTrue(
        TEXT("A disconnect that overtakes registration is retained"),
        RegisteredAfterDisconnect->IsCancellationRequested());

    const TSharedRef<FRequestCancellationState, ESPMode::ThreadSafe> FirstDuplicate =
        Registry.Register(3, TEXT("t:duplicate"), 4.0);
    const TSharedRef<FRequestCancellationState, ESPMode::ThreadSafe> SecondDuplicate =
        Registry.Register(4, TEXT("t:duplicate"), 5.0);
    TestTrue(TEXT("A duplicate token cancels its earlier owner"), FirstDuplicate->IsCancellationRequested());
    TestFalse(TEXT("The replacement token starts healthy"), SecondDuplicate->IsCancellationRequested());

    const TSharedRef<FRequestCancellationState, ESPMode::ThreadSafe> Independent =
        Registry.Register(5, TEXT("t:independent"), 5.0);
    Registry.Cancel(TEXT("t:duplicate"), 6.0);
    TestFalse(TEXT("Different tokens remain isolated"), Independent->IsCancellationRequested());

    Registry.Cancel(TEXT("t:expired"), 0.0);
    const TSharedRef<FRequestCancellationState, ESPMode::ThreadSafe> Expired =
        Registry.Register(6, TEXT("t:expired"), 301.0);
    TestFalse(TEXT("Expired tombstones do not cancel a later request"), Expired->IsCancellationRequested());

    for (int32 Index = 0; Index < 5000; ++Index)
    {
        Registry.Cancel(FString::Printf(TEXT("t:bounded-%d"), Index), 400.0);
    }
    TestTrue(
        TEXT("Pending cancellation tombstones remain bounded"),
        Registry.PendingTokenCountForTesting() <= 4096);
    return true;
}

#endif
