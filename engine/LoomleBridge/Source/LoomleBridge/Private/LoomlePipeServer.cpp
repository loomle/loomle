// Copyright 2026 Loomle contributors.

#include "LoomlePipeServer.h"

#include "Async/Async.h"
#include "Dom/JsonObject.h"
#include "HAL/PlatformProcess.h"
#include "HAL/RunnableThread.h"
#include "Logging/LogMacros.h"
#include "Misc/Paths.h"
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
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#endif

DEFINE_LOG_CATEGORY_STATIC(LogLoomlePipe, Log, All);

namespace
{
#if PLATFORM_WINDOWS
bool CompleteOverlappedOperation(
    HANDLE Handle,
    OVERLAPPED& Operation,
    const BOOL bCompletedImmediately,
    const DWORD StartError,
    DWORD& OutBytesTransferred,
    DWORD& OutError)
{
    OutBytesTransferred = 0;
    OutError = ERROR_SUCCESS;
    if (!bCompletedImmediately)
    {
        if (StartError != ERROR_IO_PENDING)
        {
            OutError = StartError;
            return false;
        }
        if (WaitForSingleObject(Operation.hEvent, INFINITE) != WAIT_OBJECT_0)
        {
            OutError = GetLastError();
            return false;
        }
    }
    if (!GetOverlappedResult(
            Handle,
            &Operation,
            &OutBytesTransferred,
            false))
    {
        OutError = GetLastError();
        return false;
    }
    return true;
}
#else
constexpr int32 UnixSocketListenBacklog = SOMAXCONN;
#endif

TSharedPtr<FJsonValue> ExtractRequestId(const FString& RequestLine)
{
    TSharedPtr<FJsonObject> RequestObject;
    const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(RequestLine);
    if (!FJsonSerializer::Deserialize(Reader, RequestObject) || !RequestObject.IsValid())
    {
        return MakeShared<FJsonValueNull>();
    }

    TSharedPtr<FJsonValue> IdValue = RequestObject->TryGetField(TEXT("id"));
    if (!IdValue.IsValid())
    {
        IdValue = MakeShared<FJsonValueNull>();
    }

    return IdValue;
}

bool IsCancellationRequest(const FString& RequestLine)
{
    TSharedPtr<FJsonObject> RequestObject;
    const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(RequestLine);
    FString Method;
    return FJsonSerializer::Deserialize(Reader, RequestObject)
        && RequestObject.IsValid()
        && RequestObject->TryGetStringField(TEXT("method"), Method)
        && Method == TEXT("rpc.cancel");
}

FString MakeBusyErrorResponse(const FString& RequestLine)
{
    TSharedPtr<FJsonObject> ErrorObject = MakeShared<FJsonObject>();
    ErrorObject->SetNumberField(TEXT("code"), -32000);
    ErrorObject->SetStringField(TEXT("message"), TEXT("Server busy: too many in-flight requests"));

    TSharedPtr<FJsonObject> ResponseObject = MakeShared<FJsonObject>();
    ResponseObject->SetStringField(TEXT("jsonrpc"), TEXT("2.0"));
    ResponseObject->SetField(TEXT("id"), ExtractRequestId(RequestLine));
    ResponseObject->SetObjectField(TEXT("error"), ErrorObject);

    FString Output;
    const TSharedRef<
        TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
        TJsonWriterFactory<
            TCHAR,
            TCondensedJsonPrintPolicy<TCHAR>>::Create(&Output);
    FJsonSerializer::Serialize(ResponseObject.ToSharedRef(), Writer);
    return Output;
}

}

FLoomlePipeServer::FLoomlePipeServer(const FString& InPipeName, FRequestHandler InHandler, FConnectionClosedHandler InConnectionClosedHandler)
    : PipeName(InPipeName)
    , RequestHandler(MoveTemp(InHandler))
    , ConnectionClosedHandler(MoveTemp(InConnectionClosedHandler))
{
}

FLoomlePipeServer::~FLoomlePipeServer()
{
    StopServer();
}

bool FLoomlePipeServer::Start()
{
    if (Thread != nullptr)
    {
        return false;
    }

    bStopRequested = false;
    SetListenerState(ELoomlePipeListenerState::Starting);
    Thread = FRunnableThread::Create(this, TEXT("LoomlePipeServerThread"), 0, TPri_Normal);
    if (Thread == nullptr)
    {
        SetListenerState(ELoomlePipeListenerState::Failed);
    }
    return Thread != nullptr;
}

void FLoomlePipeServer::StopServer()
{
    Stop();

    if (Thread != nullptr)
    {
        Thread->WaitForCompletion();
        delete Thread;
        Thread = nullptr;
    }

    while (ActiveWorkerCount.GetValue() > 0)
    {
        FPlatformProcess::Sleep(0.01f);
    }
    SetListenerState(ELoomlePipeListenerState::Stopped);
}

bool FLoomlePipeServer::TryBeginInFlight()
{
    const int32 NewCount = InFlightRequestCount.Increment();
    if (NewCount > MaxInFlightRequests)
    {
        InFlightRequestCount.Decrement();
        return false;
    }

    return true;
}

void FLoomlePipeServer::EndInFlight()
{
    InFlightRequestCount.Decrement();
}

int32 FLoomlePipeServer::GetActiveConnectionCount() const
{
    FScopeLock ScopeLock(&ConnectionsMutex);
    return ActiveConnections.Num();
}

ELoomlePipeListenerState FLoomlePipeServer::GetListenerState() const
{
    return static_cast<ELoomlePipeListenerState>(ListenerState.load());
}

FString FLoomlePipeServer::GetListenerStateName() const
{
    switch (GetListenerState())
    {
    case ELoomlePipeListenerState::Starting:
        return TEXT("starting");
    case ELoomlePipeListenerState::Listening:
        return TEXT("listening");
    case ELoomlePipeListenerState::Failed:
        return TEXT("failed");
    case ELoomlePipeListenerState::Stopping:
        return TEXT("stopping");
    case ELoomlePipeListenerState::Stopped:
    default:
        return TEXT("stopped");
    }
}

void FLoomlePipeServer::SetListenerState(const ELoomlePipeListenerState NewState)
{
    ListenerState.store(static_cast<uint8>(NewState));
}

void FLoomlePipeServer::RegisterConnection(int32 ConnectionSerial, void* NativeHandle)
{
    const TSharedRef<FConnectionState, ESPMode::ThreadSafe> State =
        MakeShared<FConnectionState, ESPMode::ThreadSafe>();
#if PLATFORM_WINDOWS
    State->NativeHandle = NativeHandle;
#else
    State->NativeHandle = static_cast<int32>(reinterpret_cast<UPTRINT>(NativeHandle));
#endif
    FScopeLock ScopeLock(&ConnectionsMutex);
    ActiveConnections.Add(ConnectionSerial, State);
}

void FLoomlePipeServer::UnregisterConnection(int32 ConnectionSerial)
{
    TSharedPtr<FConnectionState, ESPMode::ThreadSafe> State;
    {
        FScopeLock ScopeLock(&ConnectionsMutex);
        State = ActiveConnections.FindRef(ConnectionSerial);
        ActiveConnections.Remove(ConnectionSerial);
    }
    CloseConnectionState(State);
}

void FLoomlePipeServer::CloseConnectionState(
    const TSharedPtr<FConnectionState, ESPMode::ThreadSafe>& State)
{
    if (!State.IsValid() || State->bClosed.Exchange(true))
    {
        return;
    }

    // Mark the state closed before touching the native handle. Writers that
    // already captured this state re-check bClosed while holding WriteMutex;
    // an in-progress blocking write is interrupted by shutdown/CancelIoEx.
#if PLATFORM_WINDOWS
    HANDLE LocalHandle = static_cast<HANDLE>(State->NativeHandle);
    if (LocalHandle != nullptr && LocalHandle != INVALID_HANDLE_VALUE)
    {
        CancelIoEx(LocalHandle, nullptr);
        DisconnectNamedPipe(LocalHandle);
        FScopeLock WriteLock(&State->WriteMutex);
        CloseHandle(LocalHandle);
        State->NativeHandle = nullptr;
    }
#else
    const int32 LocalClientFd = State->NativeHandle;
    if (LocalClientFd >= 0)
    {
        shutdown(LocalClientFd, SHUT_RDWR);
        FScopeLock WriteLock(&State->WriteMutex);
        close(LocalClientFd);
        State->NativeHandle = -1;
    }
#endif
}

void FLoomlePipeServer::CloseAllConnections()
{
    TArray<TSharedPtr<FConnectionState, ESPMode::ThreadSafe>> Connections;
    {
        FScopeLock ScopeLock(&ConnectionsMutex);
        ActiveConnections.GenerateValueArray(Connections);
    }
#if PLATFORM_WINDOWS
    for (const TSharedPtr<FConnectionState, ESPMode::ThreadSafe>& State : Connections)
    {
        CloseConnectionState(State);
    }
#else
    for (const TSharedPtr<FConnectionState, ESPMode::ThreadSafe>& State : Connections)
    {
        CloseConnectionState(State);
    }
#endif
}

bool FLoomlePipeServer::DispatchControlRequest(const FString& RequestLine, const int32 ConnectionSerial)
{
    FRequestHandler HandlerSnapshot = RequestHandler;
    const FString Response = HandlerSnapshot(ConnectionSerial, RequestLine);
    return Response.IsEmpty() || WriteMessageForConnection(Response, ConnectionSerial);
}

void FLoomlePipeServer::DispatchRequest(FString RequestLine, int32 ConnectionSerial)
{
    ActiveWorkerCount.Increment();
    Async(EAsyncExecution::Thread, [this, RequestLine = MoveTemp(RequestLine), ConnectionSerial]()
    {
        ON_SCOPE_EXIT
        {
            EndInFlight();
            ActiveWorkerCount.Decrement();
        };

        if (bStopRequested)
        {
            return;
        }

        FRequestHandler HandlerSnapshot = RequestHandler;
        const FString Response = HandlerSnapshot(ConnectionSerial, RequestLine);
        if (!Response.IsEmpty() && !WriteMessageForConnection(Response, ConnectionSerial))
        {
            UE_LOG(LogLoomlePipe, Verbose, TEXT("Dropping response for stale or disconnected client"));
        }
    });
}

void FLoomlePipeServer::Stop()
{
    const ELoomlePipeListenerState CurrentState = GetListenerState();
    if (CurrentState != ELoomlePipeListenerState::Stopped
        && CurrentState != ELoomlePipeListenerState::Failed)
    {
        SetListenerState(ELoomlePipeListenerState::Stopping);
    }
    bStopRequested = true;

#if PLATFORM_WINDOWS
    {
        FScopeLock ScopeLock(&ConnectionsMutex);
        if (PendingPipeHandle != nullptr && PendingPipeHandle != INVALID_HANDLE_VALUE)
        {
            HANDLE LocalHandle = static_cast<HANDLE>(PendingPipeHandle);
            CancelIoEx(LocalHandle, nullptr);
            DisconnectNamedPipe(LocalHandle);
        }
    }
#else
    int32 LocalServerFd = -1;
    {
        FScopeLock ScopeLock(&ConnectionsMutex);
        LocalServerFd = ServerFd;
    }
    if (LocalServerFd >= 0)
    {
        // shutdown() is not required to wake accept() on a listening AF_UNIX
        // socket on every supported POSIX platform (notably Darwin). Keep
        // ownership of the listener in Run(), then make one local connection
        // so accept() returns and observes bStopRequested without a
        // cross-thread close/double-close race.
        shutdown(LocalServerFd, SHUT_RDWR);

        const FString SocketPath = GetSocketPath();
        const FTCHARToUTF8 PathUtf8(*SocketPath);
        if (PathUtf8.Length()
            < static_cast<int32>(sizeof(((sockaddr_un*)nullptr)->sun_path)))
        {
            const int32 WakeFd = socket(AF_UNIX, SOCK_STREAM, 0);
            if (WakeFd >= 0)
            {
                sockaddr_un WakeAddress;
                FMemory::Memzero(WakeAddress);
                WakeAddress.sun_family = AF_UNIX;
                FCStringAnsi::Strncpy(
                    WakeAddress.sun_path,
                    PathUtf8.Get(),
                    UE_ARRAY_COUNT(WakeAddress.sun_path));
                connect(
                    WakeFd,
                    reinterpret_cast<const sockaddr*>(&WakeAddress),
                    sizeof(WakeAddress));
                close(WakeFd);
            }
        }
    }
#endif

    CloseAllConnections();
}

uint32 FLoomlePipeServer::Run()
{
#if PLATFORM_WINDOWS
    const FString FullPipePath = FString::Printf(TEXT("\\\\.\\pipe\\%s"), *PipeName);

    while (!bStopRequested)
    {
        HANDLE LocalPipe = CreateNamedPipeW(
            *FullPipePath,
            PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED,
            PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
            PIPE_UNLIMITED_INSTANCES,
            64 * 1024,
            64 * 1024,
            0,
            nullptr);

        if (LocalPipe == INVALID_HANDLE_VALUE)
        {
            UE_LOG(LogLoomlePipe, Error, TEXT("Failed to create named pipe %s, error=%lu"), *FullPipePath, GetLastError());
            SetListenerState(bStopRequested
                ? ELoomlePipeListenerState::Stopped
                : ELoomlePipeListenerState::Failed);
            return 0;
        }

        bool bStopBeforeConnect = false;
        {
            FScopeLock ScopeLock(&ConnectionsMutex);
            bStopBeforeConnect = bStopRequested;
            if (!bStopBeforeConnect)
            {
                PendingPipeHandle = LocalPipe;
            }
        }
        if (bStopBeforeConnect)
        {
            CloseHandle(LocalPipe);
            break;
        }

        SetListenerState(ELoomlePipeListenerState::Listening);

        HANDLE ConnectEvent = CreateEventW(nullptr, false, false, nullptr);
        bool bConnected = false;
        DWORD ConnectError = ERROR_SUCCESS;
        if (ConnectEvent != nullptr)
        {
            OVERLAPPED ConnectOperation = {};
            ConnectOperation.hEvent = ConnectEvent;
            const BOOL bConnectedImmediately =
                ConnectNamedPipe(LocalPipe, &ConnectOperation);
            ConnectError = bConnectedImmediately
                ? ERROR_SUCCESS
                : GetLastError();
            if (ConnectError == ERROR_PIPE_CONNECTED)
            {
                bConnected = true;
            }
            else
            {
                DWORD BytesTransferred = 0;
                bConnected = CompleteOverlappedOperation(
                    LocalPipe,
                    ConnectOperation,
                    bConnectedImmediately,
                    ConnectError,
                    BytesTransferred,
                    ConnectError);
            }
            CloseHandle(ConnectEvent);
        }
        else
        {
            ConnectError = GetLastError();
        }
        {
            FScopeLock ScopeLock(&ConnectionsMutex);
            if (PendingPipeHandle == LocalPipe)
            {
                PendingPipeHandle = nullptr;
            }
        }
        if (!bConnected)
        {
            if (!bStopRequested && ConnectError != ERROR_OPERATION_ABORTED)
            {
                UE_LOG(LogLoomlePipe, Warning, TEXT("ConnectNamedPipe failed, error=%lu"), ConnectError);
            }
            CloseHandle(LocalPipe);
            continue;
        }
        const int32 ConnectionSerial = NextConnectionSerial.Increment();
        RegisterConnection(ConnectionSerial, LocalPipe);
        ActiveWorkerCount.Increment();
        UE_LOG(LogLoomlePipe, Verbose, TEXT("Loomle client connected on %s"), *FullPipePath);
        Async(EAsyncExecution::Thread, [this, LocalPipe, ConnectionSerial]()
        {
            HandleWindowsClient(LocalPipe, ConnectionSerial);
        });
    }
#else
    const FString SocketPath = GetSocketPath();
    const FTCHARToUTF8 PathUtf8(*SocketPath);

    if (PathUtf8.Length() >= static_cast<int32>(sizeof(((sockaddr_un*)nullptr)->sun_path)))
    {
        UE_LOG(LogLoomlePipe, Error, TEXT("Unix socket path is too long: %s"), *SocketPath);
        SetListenerState(ELoomlePipeListenerState::Failed);
        return 0;
    }

    const int32 LocalServerFd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (LocalServerFd < 0)
    {
        UE_LOG(LogLoomlePipe, Error, TEXT("Failed to create unix socket at %s"), *SocketPath);
        SetListenerState(ELoomlePipeListenerState::Failed);
        return 0;
    }

    {
        FScopeLock ScopeLock(&ConnectionsMutex);
        ServerFd = LocalServerFd;
    }
    if (bStopRequested)
    {
        close(LocalServerFd);
        FScopeLock ScopeLock(&ConnectionsMutex);
        if (ServerFd == LocalServerFd)
        {
            ServerFd = -1;
        }
        SetListenerState(ELoomlePipeListenerState::Stopped);
        return 0;
    }
    unlink(PathUtf8.Get());

    sockaddr_un Address;
    FMemory::Memzero(Address);
    Address.sun_family = AF_UNIX;
    FCStringAnsi::Strncpy(Address.sun_path, PathUtf8.Get(), UE_ARRAY_COUNT(Address.sun_path));

    if (bind(LocalServerFd, reinterpret_cast<const sockaddr*>(&Address), sizeof(Address)) != 0)
    {
        UE_LOG(LogLoomlePipe, Error, TEXT("Failed to bind unix socket %s"), *SocketPath);
        close(LocalServerFd);
        {
            FScopeLock ScopeLock(&ConnectionsMutex);
            if (ServerFd == LocalServerFd)
            {
                ServerFd = -1;
            }
        }
        unlink(PathUtf8.Get());
        SetListenerState(ELoomlePipeListenerState::Failed);
        return 0;
    }

    if (listen(LocalServerFd, UnixSocketListenBacklog) != 0)
    {
        UE_LOG(LogLoomlePipe, Error, TEXT("Failed to listen on unix socket %s"), *SocketPath);
        close(LocalServerFd);
        {
            FScopeLock ScopeLock(&ConnectionsMutex);
            if (ServerFd == LocalServerFd)
            {
                ServerFd = -1;
            }
        }
        unlink(PathUtf8.Get());
        SetListenerState(ELoomlePipeListenerState::Failed);
        return 0;
    }
    SetListenerState(ELoomlePipeListenerState::Listening);
    UE_LOG(LogLoomlePipe, Display, TEXT("Loomle bridge listening on unix socket %s (backlog=%d)"), *SocketPath, UnixSocketListenBacklog);

    while (!bStopRequested)
    {
        const int32 LocalClientFd = accept(LocalServerFd, nullptr, nullptr);
        if (LocalClientFd < 0)
        {
            if (!bStopRequested)
            {
                UE_LOG(LogLoomlePipe, Warning, TEXT("Failed to accept unix socket client on %s"), *SocketPath);
            }
            continue;
        }
        if (bStopRequested)
        {
            close(LocalClientFd);
            break;
        }

        const int32 ConnectionSerial = NextConnectionSerial.Increment();
        RegisterConnection(ConnectionSerial, reinterpret_cast<void*>(static_cast<UPTRINT>(LocalClientFd)));
        ActiveWorkerCount.Increment();
        UE_LOG(LogLoomlePipe, Verbose, TEXT("Loomle client connected on unix socket %s"), *SocketPath);
        Async(EAsyncExecution::Thread, [this, LocalClientFd, ConnectionSerial]()
        {
            HandleUnixClient(LocalClientFd, ConnectionSerial);
        });
    }

    if (LocalServerFd >= 0)
    {
        close(LocalServerFd);
    }
    {
        FScopeLock ScopeLock(&ConnectionsMutex);
        if (ServerFd == LocalServerFd)
        {
            ServerFd = -1;
        }
    }
    unlink(PathUtf8.Get());
#endif

    SetListenerState(bStopRequested
        ? ELoomlePipeListenerState::Stopped
        : ELoomlePipeListenerState::Failed);

    return 0;
}

bool FLoomlePipeServer::WriteMessageForConnection(const FString& Message, int32 ExpectedConnectionSerial)
{
    if (ExpectedConnectionSerial == 0)
    {
        return false;
    }

    TSharedPtr<FConnectionState, ESPMode::ThreadSafe> State;
    {
        FScopeLock ScopeLock(&ConnectionsMutex);
        State = ActiveConnections.FindRef(ExpectedConnectionSerial);
    }
    if (!State.IsValid())
    {
        return false;
    }

    const FString MessageWithNewline = Message + TEXT("\n");
    FTCHARToUTF8 Utf8(*MessageWithNewline);
    FScopeLock WriteLock(&State->WriteMutex);

#if PLATFORM_WINDOWS
    HANDLE LocalHandle = static_cast<HANDLE>(State->NativeHandle);
    if (State->bClosed.Load() || LocalHandle == nullptr || LocalHandle == INVALID_HANDLE_VALUE)
    {
        return false;
    }
    HANDLE WriteEvent = CreateEventW(nullptr, false, false, nullptr);
    if (WriteEvent == nullptr)
    {
        return false;
    }
    ON_SCOPE_EXIT
    {
        CloseHandle(WriteEvent);
    };
    int32 Offset = 0;
    while (Offset < Utf8.Length())
    {
        if (State->bClosed.Load())
        {
            return false;
        }
        ResetEvent(WriteEvent);
        OVERLAPPED WriteOperation = {};
        WriteOperation.hEvent = WriteEvent;
        DWORD BytesWritten = 0;
        const BOOL bOk = WriteFile(
            LocalHandle,
            Utf8.Get() + Offset,
            static_cast<DWORD>(Utf8.Length() - Offset),
            nullptr,
            &WriteOperation);
        const DWORD StartError = bOk ? ERROR_SUCCESS : GetLastError();
        DWORD WriteError = ERROR_SUCCESS;
        if (!CompleteOverlappedOperation(
                LocalHandle,
                WriteOperation,
                bOk,
                StartError,
                BytesWritten,
                WriteError)
            || BytesWritten == 0)
        {
            return false;
        }
        Offset += static_cast<int32>(BytesWritten);
    }
    return true;
#else
    if (State->bClosed.Load() || State->NativeHandle < 0)
    {
        return false;
    }
    int32 Offset = 0;
    while (Offset < Utf8.Length())
    {
        if (State->bClosed.Load())
        {
            return false;
        }
        const ssize_t BytesWritten = write(
            State->NativeHandle,
            Utf8.Get() + Offset,
            static_cast<size_t>(Utf8.Length() - Offset));
        if (BytesWritten < 0 && errno == EINTR)
        {
            continue;
        }
        if (BytesWritten <= 0)
        {
            return false;
        }
        Offset += static_cast<int32>(BytesWritten);
    }
    return true;
#endif
}

void FLoomlePipeServer::HandleWindowsClient(void* NativeHandle, int32 ConnectionSerial)
{
#if PLATFORM_WINDOWS
    ON_SCOPE_EXIT
    {
        ActiveWorkerCount.Decrement();
    };

    HANDLE LocalPipe = static_cast<HANDLE>(NativeHandle);
    HANDLE ReadEvent = CreateEventW(nullptr, false, false, nullptr);
    if (ReadEvent == nullptr)
    {
        UnregisterConnection(ConnectionSerial);
        if (ConnectionClosedHandler)
        {
            ConnectionClosedHandler(ConnectionSerial);
        }
        return;
    }
    ON_SCOPE_EXIT
    {
        CloseHandle(ReadEvent);
    };
    TArray<uint8> PendingBytes;
    PendingBytes.Reserve(64 * 1024);

    while (!bStopRequested)
    {
        uint8 Buffer[4096];
        ResetEvent(ReadEvent);
        OVERLAPPED ReadOperation = {};
        ReadOperation.hEvent = ReadEvent;
        DWORD BytesRead = 0;
        const BOOL bReadOk = ReadFile(
            LocalPipe,
            Buffer,
            sizeof(Buffer),
            nullptr,
            &ReadOperation);
        const DWORD StartError = bReadOk ? ERROR_SUCCESS : GetLastError();
        DWORD ReadError = ERROR_SUCCESS;
        if (!CompleteOverlappedOperation(
                LocalPipe,
                ReadOperation,
                bReadOk,
                StartError,
                BytesRead,
                ReadError)
            || BytesRead == 0)
        {
            break;
        }

        PendingBytes.Append(Buffer, static_cast<int32>(BytesRead));

        int32 NewlineIndex = INDEX_NONE;
        while (PendingBytes.Find(static_cast<uint8>('\n'), NewlineIndex))
        {
            TArray<uint8> LineBytes;
            LineBytes.Append(PendingBytes.GetData(), NewlineIndex);
            PendingBytes.RemoveAt(0, NewlineIndex + 1, EAllowShrinking::No);

            LineBytes.Add('\0');
            FString RequestLine = FString(UTF8_TO_TCHAR(reinterpret_cast<const char*>(LineBytes.GetData())));
            RequestLine.TrimStartAndEndInline();
            if (RequestLine.IsEmpty())
            {
                continue;
            }

            const bool bCancellationRequest = IsCancellationRequest(RequestLine);
            if (bCancellationRequest)
            {
                if (!DispatchControlRequest(RequestLine, ConnectionSerial))
                {
                    UE_LOG(LogLoomlePipe, Verbose, TEXT("Dropping response for stale or disconnected cancellation client"));
                    break;
                }
                continue;
            }
            if (!TryBeginInFlight())
            {
                if (!WriteMessageForConnection(MakeBusyErrorResponse(RequestLine), ConnectionSerial))
                {
                    UE_LOG(LogLoomlePipe, Warning, TEXT("Failed writing busy response to client pipe"));
                    break;
                }
                continue;
            }

            DispatchRequest(MoveTemp(RequestLine), ConnectionSerial);
        }
    }

    UnregisterConnection(ConnectionSerial);
    if (ConnectionClosedHandler)
    {
        ConnectionClosedHandler(ConnectionSerial);
    }
    UE_LOG(LogLoomlePipe, Verbose, TEXT("Loomle client disconnected"));
#endif
}

void FLoomlePipeServer::HandleUnixClient(int32 LocalClientFd, int32 ConnectionSerial)
{
#if !PLATFORM_WINDOWS
    ON_SCOPE_EXIT
    {
        ActiveWorkerCount.Decrement();
    };

    TArray<uint8> PendingBytes;
    PendingBytes.Reserve(64 * 1024);

    while (!bStopRequested)
    {
        uint8 Buffer[4096];
        const ssize_t BytesRead = read(LocalClientFd, Buffer, sizeof(Buffer));
        if (BytesRead <= 0)
        {
            break;
        }

        PendingBytes.Append(Buffer, static_cast<int32>(BytesRead));

        int32 NewlineIndex = INDEX_NONE;
        while (PendingBytes.Find(static_cast<uint8>('\n'), NewlineIndex))
        {
            TArray<uint8> LineBytes;
            LineBytes.Append(PendingBytes.GetData(), NewlineIndex);
            PendingBytes.RemoveAt(0, NewlineIndex + 1, EAllowShrinking::No);

            LineBytes.Add('\0');
            FString RequestLine = FString(UTF8_TO_TCHAR(reinterpret_cast<const char*>(LineBytes.GetData())));
            RequestLine.TrimStartAndEndInline();
            if (RequestLine.IsEmpty())
            {
                continue;
            }

            const bool bCancellationRequest = IsCancellationRequest(RequestLine);
            if (bCancellationRequest)
            {
                if (!DispatchControlRequest(RequestLine, ConnectionSerial))
                {
                    UE_LOG(LogLoomlePipe, Verbose, TEXT("Dropping response for stale or disconnected cancellation client"));
                    break;
                }
                continue;
            }
            if (!TryBeginInFlight())
            {
                if (!WriteMessageForConnection(MakeBusyErrorResponse(RequestLine), ConnectionSerial))
                {
                    UE_LOG(LogLoomlePipe, Warning, TEXT("Failed writing busy response to unix socket client"));
                    break;
                }
                continue;
            }

            DispatchRequest(MoveTemp(RequestLine), ConnectionSerial);
        }
    }

    UnregisterConnection(ConnectionSerial);
    if (ConnectionClosedHandler)
    {
        ConnectionClosedHandler(ConnectionSerial);
    }
    UE_LOG(LogLoomlePipe, Verbose, TEXT("Loomle client disconnected"));
#endif
}

FString FLoomlePipeServer::GetSocketPath() const
{
#if PLATFORM_WINDOWS
    return FString();
#else
    return PipeName;
#endif
}
