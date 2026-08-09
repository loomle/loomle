// Copyright 2026 Loomle contributors.

#if WITH_DEV_AUTOMATION_TESTS

#include "LoomlePipeServer.h"

#include "Async/Async.h"
#include "Dom/JsonObject.h"
#include "HAL/Event.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTime.h"
#include "HAL/ThreadSafeCounter.h"
#include "Misc/AutomationTest.h"
#include "Misc/Guid.h"
#include "Misc/ScopeExit.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

#if PLATFORM_WINDOWS
#include "Windows/AllowWindowsPlatformTypes.h"
#include <Windows.h>
#include "Windows/HideWindowsPlatformTypes.h"
#else
#include <errno.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#endif

namespace
{
FString UniquePipeName()
{
    const FString Suffix = FGuid::NewGuid().ToString(EGuidFormats::Digits);
#if PLATFORM_WINDOWS
    return FString::Printf(
        TEXT("loomle-automation-%u-%s"),
        FPlatformProcess::GetCurrentProcessId(),
        *Suffix);
#else
    return FString::Printf(
        TEXT("/tmp/loomle-%u-%s.sock"),
        FPlatformProcess::GetCurrentProcessId(),
        *Suffix);
#endif
}

bool WaitUntil(TFunctionRef<bool()> Predicate, const double TimeoutSeconds)
{
    const double Deadline = FPlatformTime::Seconds() + TimeoutSeconds;
    while (FPlatformTime::Seconds() < Deadline)
    {
        if (Predicate())
        {
            return true;
        }
        FPlatformProcess::Sleep(0.01f);
    }
    return Predicate();
}

FString ResponseForRequest(const FString& RequestLine)
{
    TSharedPtr<FJsonObject> Request;
    const TSharedRef<TJsonReader<>> Reader =
        TJsonReaderFactory<>::Create(RequestLine);
    if (!FJsonSerializer::Deserialize(Reader, Request)
        || !Request.IsValid())
    {
        return FString();
    }

    TSharedPtr<FJsonObject> Response = MakeShared<FJsonObject>();
    Response->SetStringField(TEXT("jsonrpc"), TEXT("2.0"));
    const TSharedPtr<FJsonValue> Id = Request->TryGetField(TEXT("id"));
    Response->SetField(
        TEXT("id"),
        Id.IsValid() ? Id : MakeShared<FJsonValueNull>());
    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetBoolField(TEXT("ok"), true);
    Response->SetObjectField(TEXT("result"), Result);

    FString Output;
    const TSharedRef<
        TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
        TJsonWriterFactory<
            TCHAR,
            TCondensedJsonPrintPolicy<TCHAR>>::Create(&Output);
    FJsonSerializer::Serialize(Response.ToSharedRef(), Writer);
    return Output;
}

bool TryGetJsonRpcId(const FString& ResponseLine, int32& OutId)
{
    OutId = INDEX_NONE;
    TSharedPtr<FJsonObject> Response;
    const TSharedRef<TJsonReader<>> Reader =
        TJsonReaderFactory<>::Create(ResponseLine);
    if (!FJsonSerializer::Deserialize(Reader, Response)
        || !Response.IsValid())
    {
        return false;
    }
    const TSharedPtr<FJsonValue> Id = Response->TryGetField(TEXT("id"));
    return Id.IsValid() && Id->TryGetNumber(OutId);
}

class FPipeTestClient
{
public:
    ~FPipeTestClient()
    {
        Close();
    }

    bool Connect(const FString& PipeName, const double TimeoutSeconds = 2.0)
    {
        Close();
        const double Deadline = FPlatformTime::Seconds() + TimeoutSeconds;
#if PLATFORM_WINDOWS
        const FString FullName =
            FString::Printf(TEXT("\\\\.\\pipe\\%s"), *PipeName);
        while (FPlatformTime::Seconds() < Deadline)
        {
            NativeHandle = CreateFileW(
                *FullName,
                GENERIC_READ | GENERIC_WRITE,
                0,
                nullptr,
                OPEN_EXISTING,
                0,
                nullptr);
            if (NativeHandle != INVALID_HANDLE_VALUE)
            {
                return true;
            }
            WaitNamedPipeW(*FullName, 20);
            FPlatformProcess::Sleep(0.01f);
        }
        NativeHandle = nullptr;
        return false;
#else
        const FTCHARToUTF8 PathUtf8(*PipeName);
        while (FPlatformTime::Seconds() < Deadline)
        {
            NativeHandle = socket(AF_UNIX, SOCK_STREAM, 0);
            if (NativeHandle >= 0)
            {
                sockaddr_un Address;
                FMemory::Memzero(Address);
                Address.sun_family = AF_UNIX;
                FCStringAnsi::Strncpy(
                    Address.sun_path,
                    PathUtf8.Get(),
                    UE_ARRAY_COUNT(Address.sun_path));
                if (connect(
                        NativeHandle,
                        reinterpret_cast<const sockaddr*>(&Address),
                        sizeof(Address)) == 0)
                {
                    return true;
                }
                close(NativeHandle);
                NativeHandle = -1;
            }
            FPlatformProcess::Sleep(0.01f);
        }
        return false;
#endif
    }

    bool SendLine(const FString& Line)
    {
        const FString Framed = Line + TEXT("\n");
        const FTCHARToUTF8 Utf8(*Framed);
        int32 Offset = 0;
        while (Offset < Utf8.Length())
        {
#if PLATFORM_WINDOWS
            if (NativeHandle == nullptr
                || NativeHandle == INVALID_HANDLE_VALUE)
            {
                return false;
            }
            DWORD Written = 0;
            const BOOL bOk = WriteFile(
                NativeHandle,
                Utf8.Get() + Offset,
                static_cast<DWORD>(Utf8.Length() - Offset),
                &Written,
                nullptr);
            if (!bOk || Written == 0)
            {
                return false;
            }
            Offset += static_cast<int32>(Written);
#else
            if (NativeHandle < 0)
            {
                return false;
            }
            const ssize_t Written = write(
                NativeHandle,
                Utf8.Get() + Offset,
                static_cast<size_t>(Utf8.Length() - Offset));
            if (Written < 0 && errno == EINTR)
            {
                continue;
            }
            if (Written <= 0)
            {
                return false;
            }
            Offset += static_cast<int32>(Written);
#endif
        }
        return true;
    }

    bool ReadLine(FString& OutLine, const double TimeoutSeconds = 2.0)
    {
        const double Deadline = FPlatformTime::Seconds() + TimeoutSeconds;
        for (;;)
        {
            int32 NewlineIndex = INDEX_NONE;
            if (Pending.Find(static_cast<uint8>('\n'), NewlineIndex))
            {
                TArray<uint8> LineBytes;
                LineBytes.Append(Pending.GetData(), NewlineIndex);
                Pending.RemoveAt(
                    0,
                    NewlineIndex + 1,
                    EAllowShrinking::No);
                LineBytes.Add('\0');
                OutLine = UTF8_TO_TCHAR(
                    reinterpret_cast<const char*>(LineBytes.GetData()));
                return true;
            }

            const double Remaining = Deadline - FPlatformTime::Seconds();
            if (Remaining <= 0.0)
            {
                return false;
            }

            uint8 Buffer[4096];
#if PLATFORM_WINDOWS
            if (NativeHandle == nullptr
                || NativeHandle == INVALID_HANDLE_VALUE)
            {
                return false;
            }
            DWORD Available = 0;
            if (!PeekNamedPipe(
                    NativeHandle,
                    nullptr,
                    0,
                    nullptr,
                    &Available,
                    nullptr))
            {
                return false;
            }
            if (Available == 0)
            {
                FPlatformProcess::Sleep(0.01f);
                continue;
            }
            DWORD Read = 0;
            if (!ReadFile(
                    NativeHandle,
                    Buffer,
                    FMath::Min<DWORD>(Available, sizeof(Buffer)),
                    &Read,
                    nullptr)
                || Read == 0)
            {
                return false;
            }
            Pending.Append(Buffer, static_cast<int32>(Read));
#else
            if (NativeHandle < 0)
            {
                return false;
            }
            pollfd Descriptor;
            Descriptor.fd = NativeHandle;
            Descriptor.events = POLLIN;
            Descriptor.revents = 0;
            const int32 PollMilliseconds = FMath::Max(
                1,
                static_cast<int32>(Remaining * 1000.0));
            const int32 PollResult =
                poll(&Descriptor, 1, PollMilliseconds);
            if (PollResult < 0 && errno == EINTR)
            {
                continue;
            }
            if (PollResult <= 0 || !(Descriptor.revents & POLLIN))
            {
                return false;
            }
            const ssize_t Read = recv(
                NativeHandle,
                Buffer,
                sizeof(Buffer),
                0);
            if (Read <= 0)
            {
                return false;
            }
            Pending.Append(Buffer, static_cast<int32>(Read));
#endif
        }
    }

    void Close()
    {
#if PLATFORM_WINDOWS
        if (NativeHandle != nullptr
            && NativeHandle != INVALID_HANDLE_VALUE)
        {
            CancelIoEx(NativeHandle, nullptr);
            CloseHandle(NativeHandle);
        }
        NativeHandle = nullptr;
#else
        if (NativeHandle >= 0)
        {
            shutdown(NativeHandle, SHUT_RDWR);
            close(NativeHandle);
        }
        NativeHandle = -1;
#endif
        Pending.Reset();
    }

private:
#if PLATFORM_WINDOWS
    HANDLE NativeHandle = nullptr;
#else
    int32 NativeHandle = -1;
#endif
    TArray<uint8> Pending;
};

bool EndpointExists(const FString& PipeName)
{
#if PLATFORM_WINDOWS
    const FString FullName =
        FString::Printf(TEXT("\\\\.\\pipe\\%s"), *PipeName);
    return WaitNamedPipeW(*FullName, 0) != 0;
#else
    return access(TCHAR_TO_UTF8(*PipeName), F_OK) == 0;
#endif
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FLoomlePipeRealRoundTripTest,
    "Loomle.Pipe.Real.RoundTripAndControl",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLoomlePipeRealRoundTripTest::RunTest(const FString& Parameters)
{
    const FString PipeName = UniquePipeName();
    FThreadSafeCounter RequestCount;
    FThreadSafeCounter CancellationCount;
    FThreadSafeCounter ClosedCount;
    TSharedPtr<FLoomlePipeServer, ESPMode::ThreadSafe> Server =
        MakeShared<FLoomlePipeServer, ESPMode::ThreadSafe>(
            PipeName,
            [&RequestCount, &CancellationCount](
                const int32 ConnectionSerial,
                const FString& Request)
            {
                RequestCount.Increment();
                if (Request.Contains(TEXT("\"method\":\"rpc.cancel\"")))
                {
                    CancellationCount.Increment();
                }
                return ResponseForRequest(Request);
            },
            [&ClosedCount](const int32 ConnectionSerial)
            {
                ClosedCount.Increment();
            });
    FPipeTestClient Client;
    ON_SCOPE_EXIT
    {
        Client.Close();
        Server->StopServer();
    };

    TestTrue(TEXT("The real Pipe listener thread starts"), Server->Start());
    TestTrue(
        TEXT("The listener reaches its native listening state"),
        WaitUntil(
            [&Server]()
            {
                return Server->GetListenerState()
                    == ELoomlePipeListenerState::Listening;
            },
            2.0));
    const bool bConnected = Client.Connect(PipeName);
    TestTrue(TEXT("A native client connects to the endpoint"), bConnected);
    if (!bConnected)
    {
        return false;
    }
    TestTrue(
        TEXT("The server tracks the live native connection"),
        WaitUntil(
            [&Server]()
            {
                return Server->GetActiveConnectionCount() == 1;
            },
            2.0));

    TestTrue(
        TEXT("A normal JSON-RPC frame is written"),
        Client.SendLine(
            TEXT("{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"sal.query\"}")));
    FString FirstResponse;
    TestTrue(
        TEXT("A normal JSON-RPC response is read"),
        Client.ReadLine(FirstResponse));
    int32 FirstResponseId = INDEX_NONE;
    TestTrue(
        TEXT("The normal wire frame is one complete JSON response"),
        TryGetJsonRpcId(FirstResponse, FirstResponseId));
    TestEqual(
        TEXT("The normal response preserves request identity"),
        FirstResponseId,
        1);

    TestTrue(
        TEXT("A cancellation control frame is written"),
        Client.SendLine(
            TEXT("{\"jsonrpc\":\"2.0\",\"id\":2,\"method\":\"rpc.cancel\"}")));
    FString CancellationResponse;
    TestTrue(
        TEXT("The cancellation response is read synchronously"),
        Client.ReadLine(CancellationResponse));
    int32 CancellationResponseId = INDEX_NONE;
    TestTrue(
        TEXT("The cancellation wire frame is one complete JSON response"),
        TryGetJsonRpcId(
            CancellationResponse,
            CancellationResponseId));
    TestEqual(
        TEXT("The cancellation response preserves request identity"),
        CancellationResponseId,
        2);
    TestEqual(
        TEXT("The native control path invokes the cancellation handler once"),
        CancellationCount.GetValue(),
        1);
    TestEqual(
        TEXT("Both native frames reached the request handler"),
        RequestCount.GetValue(),
        2);

    Client.Close();
    TestTrue(
        TEXT("Disconnect unregisters the exact connection"),
        WaitUntil(
            [&Server]()
            {
                return Server->GetActiveConnectionCount() == 0;
            },
            2.0));
    TestTrue(
        TEXT("Disconnect notifies the owner exactly once"),
        WaitUntil(
            [&ClosedCount]()
            {
                return ClosedCount.GetValue() == 1;
            },
            2.0));

    Server->StopServer();
    TestEqual(
        TEXT("StopServer reaches the terminal state"),
        Server->GetListenerState(),
        ELoomlePipeListenerState::Stopped);
    TestFalse(
        TEXT("StopServer removes or closes the native endpoint"),
        EndpointExists(PipeName));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FLoomlePipeStaleResponseIsolationTest,
    "Loomle.Pipe.Real.StaleResponseIsolation",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLoomlePipeStaleResponseIsolationTest::RunTest(
    const FString& Parameters)
{
    const FString PipeName = UniquePipeName();
    FEvent* Entered = FPlatformProcess::GetSynchEventFromPool(true);
    FEvent* Release = FPlatformProcess::GetSynchEventFromPool(true);
    FThreadSafeCounter OldHandlerFinished;
    FThreadSafeCounter ClosedCount;
    TSharedPtr<FLoomlePipeServer, ESPMode::ThreadSafe> Server =
        MakeShared<FLoomlePipeServer, ESPMode::ThreadSafe>(
            PipeName,
            [Entered, Release, &OldHandlerFinished](
                const int32 ConnectionSerial,
                const FString& Request)
            {
                if (Request.Contains(TEXT("\"id\":101")))
                {
                    Entered->Trigger();
                    Release->Wait(5000);
                    OldHandlerFinished.Increment();
                }
                return ResponseForRequest(Request);
            },
            [&ClosedCount](const int32 ConnectionSerial)
            {
                ClosedCount.Increment();
            });
    FPipeTestClient OldClient;
    FPipeTestClient NewClient;
    ON_SCOPE_EXIT
    {
        Release->Trigger();
        OldClient.Close();
        NewClient.Close();
        Server->StopServer();
        FPlatformProcess::ReturnSynchEventToPool(Entered);
        FPlatformProcess::ReturnSynchEventToPool(Release);
    };

    TestTrue(TEXT("The Pipe listener starts"), Server->Start());
    TestTrue(
        TEXT("The Pipe listener becomes ready"),
        WaitUntil(
            [&Server]()
            {
                return Server->GetListenerState()
                    == ELoomlePipeListenerState::Listening;
            },
            2.0));
    const bool bOldConnected = OldClient.Connect(PipeName);
    TestTrue(TEXT("The first client connects"), bOldConnected);
    if (!bOldConnected)
    {
        return false;
    }
    TestTrue(
        TEXT("The first request enters its asynchronous handler"),
        OldClient.SendLine(
            TEXT("{\"jsonrpc\":\"2.0\",\"id\":101,\"method\":\"sal.query\"}"))
            && Entered->Wait(2000));

    OldClient.Close();
    TestTrue(
        TEXT("The first connection is closed before its response exists"),
        WaitUntil(
            [&ClosedCount]()
            {
                return ClosedCount.GetValue() == 1;
            },
            2.0));

    const bool bNewConnected = NewClient.Connect(PipeName);
    TestTrue(TEXT("A second client can reconnect immediately"), bNewConnected);
    if (!bNewConnected)
    {
        return false;
    }

    Release->Trigger();
    TestTrue(
        TEXT("The old request finishes after its client is gone"),
        WaitUntil(
            [&OldHandlerFinished]()
            {
                return OldHandlerFinished.GetValue() == 1;
            },
            2.0));
    TestTrue(
        TEXT("The second client can issue an independent request"),
        NewClient.SendLine(
            TEXT("{\"jsonrpc\":\"2.0\",\"id\":202,\"method\":\"sal.query\"}")));
    FString Response;
    TestTrue(
        TEXT("The second client receives one response"),
        NewClient.ReadLine(Response));
    int32 ResponseId = INDEX_NONE;
    TestTrue(
        TEXT("The replacement client's wire frame is complete JSON"),
        TryGetJsonRpcId(Response, ResponseId));
    TestEqual(
        TEXT("A late response is never delivered to a newer connection"),
        ResponseId,
        202);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FLoomlePipeStopWaitsForWorkerTest,
    "Loomle.Pipe.Real.StopWaitsForInFlightWorker",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLoomlePipeStopWaitsForWorkerTest::RunTest(
    const FString& Parameters)
{
    const FString PipeName = UniquePipeName();
    FEvent* Entered = FPlatformProcess::GetSynchEventFromPool(true);
    FEvent* Release = FPlatformProcess::GetSynchEventFromPool(true);
    TAtomic<bool> bStopReturned(false);
    FThreadSafeCounter ClosedCount;
    TSharedPtr<FLoomlePipeServer, ESPMode::ThreadSafe> Server =
        MakeShared<FLoomlePipeServer, ESPMode::ThreadSafe>(
            PipeName,
            [Entered, Release](
                const int32 ConnectionSerial,
                const FString& Request)
            {
                Entered->Trigger();
                Release->Wait(5000);
                return ResponseForRequest(Request);
            },
            [&ClosedCount](const int32 ConnectionSerial)
            {
                ClosedCount.Increment();
            });
    FPipeTestClient Client;
    ON_SCOPE_EXIT
    {
        Release->Trigger();
        Client.Close();
        Server->StopServer();
        FPlatformProcess::ReturnSynchEventToPool(Entered);
        FPlatformProcess::ReturnSynchEventToPool(Release);
    };

    TestTrue(TEXT("The Pipe listener starts"), Server->Start());
    TestTrue(
        TEXT("The Pipe listener becomes ready"),
        WaitUntil(
            [&Server]()
            {
                return Server->GetListenerState()
                    == ELoomlePipeListenerState::Listening;
            },
            2.0));
    const bool bConnected = Client.Connect(PipeName);
    TestTrue(TEXT("A client connects before shutdown"), bConnected);
    if (!bConnected)
    {
        return false;
    }
    TestTrue(
        TEXT("The in-flight request reaches the worker"),
        Client.SendLine(
            TEXT("{\"jsonrpc\":\"2.0\",\"id\":301,\"method\":\"sal.query\"}"))
            && Entered->Wait(2000));

    TFuture<void> StopFuture = Async(
        EAsyncExecution::Thread,
        [&Server, &bStopReturned]()
        {
            Server->StopServer();
            bStopReturned.Store(true);
        });
    TestTrue(
        TEXT("Shutdown closes the active native connection"),
        WaitUntil(
            [&Server]()
            {
                return Server->GetActiveConnectionCount() == 0;
            },
            2.0));
    FPlatformProcess::Sleep(0.05f);
    TestFalse(
        TEXT("StopServer does not outlive an active request worker"),
        bStopReturned.Load());

    Release->Trigger();
    TestTrue(
        TEXT("StopServer returns once the in-flight worker exits"),
        WaitUntil(
            [&bStopReturned]()
            {
                return bStopReturned.Load();
            },
            2.0));
    StopFuture.Wait();
    TestEqual(
        TEXT("Shutdown leaves no live native connections"),
        Server->GetActiveConnectionCount(),
        0);
    TestEqual(
        TEXT("Shutdown reports the closed connection exactly once"),
        ClosedCount.GetValue(),
        1);
    TestEqual(
        TEXT("Shutdown reaches the terminal listener state"),
        Server->GetListenerState(),
        ELoomlePipeListenerState::Stopped);
    TestFalse(
        TEXT("Shutdown removes or closes the native endpoint"),
        EndpointExists(PipeName));
    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
