#include "LoomlePipeServer.h"

#include "Async/Async.h"
#include "Dom/JsonObject.h"
#include "HAL/PlatformProcess.h"
#include "HAL/RunnableThread.h"
#include "Logging/LogMacros.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

#if PLATFORM_WINDOWS
#include "Windows/AllowWindowsPlatformTypes.h"
#include <Windows.h>
#include "Windows/HideWindowsPlatformTypes.h"
#else
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#endif

DEFINE_LOG_CATEGORY_STATIC(LogLoomlePipe, Log, All);

namespace
{
constexpr int32 UnixSocketListenBacklog = SOMAXCONN;

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
    const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Output);
    FJsonSerializer::Serialize(ResponseObject.ToSharedRef(), Writer);
    return Output;
}

}

FLoomlePipeServer::FLoomlePipeServer(const FString& InPipeName, FRequestHandler InHandler)
    : PipeName(InPipeName)
    , RequestHandler(MoveTemp(InHandler))
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
    Thread = FRunnableThread::Create(this, TEXT("LoomlePipeServerThread"), 0, TPri_Normal);
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
}

bool FLoomlePipeServer::SendServerNotification(const FString& JsonMessage)
{
    return WriteMessage(JsonMessage);
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

void FLoomlePipeServer::RegisterConnection(int32 ConnectionSerial, void* NativeHandle)
{
    FScopeLock ScopeLock(&WriteMutex);
    ActiveConnectionSerial.Set(ConnectionSerial);
#if PLATFORM_WINDOWS
    ActivePipeHandles.Add(ConnectionSerial, NativeHandle);
#else
    ActiveClientFds.Add(ConnectionSerial, static_cast<int32>(reinterpret_cast<UPTRINT>(NativeHandle)));
#endif
}

void FLoomlePipeServer::UnregisterConnection(int32 ConnectionSerial)
{
    FScopeLock ScopeLock(&WriteMutex);
#if PLATFORM_WINDOWS
    ActivePipeHandles.Remove(ConnectionSerial);
#else
    ActiveClientFds.Remove(ConnectionSerial);
#endif
    if (ActiveConnectionSerial.GetValue() == ConnectionSerial)
    {
        ActiveConnectionSerial.Set(0);
    }
}

void FLoomlePipeServer::CloseAllConnections()
{
    FScopeLock ScopeLock(&WriteMutex);
#if PLATFORM_WINDOWS
    for (TPair<int32, void*>& Pair : ActivePipeHandles)
    {
        HANDLE LocalHandle = static_cast<HANDLE>(Pair.Value);
        if (LocalHandle != nullptr && LocalHandle != INVALID_HANDLE_VALUE)
        {
            CancelIoEx(LocalHandle, nullptr);
            DisconnectNamedPipe(LocalHandle);
        }
    }
#else
    for (const TPair<int32, int32>& Pair : ActiveClientFds)
    {
        const int32 LocalClientFd = Pair.Value;
        if (LocalClientFd >= 0)
        {
            shutdown(LocalClientFd, SHUT_RDWR);
        }
    }
#endif
}

void FLoomlePipeServer::Stop()
{
    bStopRequested = true;
    ActiveConnectionSerial.Set(0);

#if PLATFORM_WINDOWS
    {
        FScopeLock ScopeLock(&WriteMutex);
        if (PendingPipeHandle != nullptr && PendingPipeHandle != INVALID_HANDLE_VALUE)
        {
            HANDLE LocalHandle = static_cast<HANDLE>(PendingPipeHandle);
            CancelIoEx(LocalHandle, nullptr);
            DisconnectNamedPipe(LocalHandle);
        }
    }
#else
    if (ServerFd >= 0)
    {
        shutdown(ServerFd, SHUT_RDWR);
        close(ServerFd);
        ServerFd = -1;
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
            PIPE_ACCESS_DUPLEX,
            PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
            PIPE_UNLIMITED_INSTANCES,
            64 * 1024,
            64 * 1024,
            0,
            nullptr);

        if (LocalPipe == INVALID_HANDLE_VALUE)
        {
            UE_LOG(LogLoomlePipe, Error, TEXT("Failed to create named pipe %s, error=%lu"), *FullPipePath, GetLastError());
            FPlatformProcess::Sleep(1.0f);
            continue;
        }

        {
            FScopeLock ScopeLock(&WriteMutex);
            PendingPipeHandle = LocalPipe;
        }

        const BOOL bConnected = ConnectNamedPipe(LocalPipe, nullptr) || (GetLastError() == ERROR_PIPE_CONNECTED);
        {
            FScopeLock ScopeLock(&WriteMutex);
            if (PendingPipeHandle == LocalPipe)
            {
                PendingPipeHandle = nullptr;
            }
        }
        if (!bConnected)
        {
            UE_LOG(LogLoomlePipe, Warning, TEXT("ConnectNamedPipe failed, error=%lu"), GetLastError());
            CloseHandle(LocalPipe);
            continue;
        }
        const int32 ConnectionSerial = NextConnectionSerial.Increment();
        RegisterConnection(ConnectionSerial, LocalPipe);
        ActiveWorkerCount.Increment();
        UE_LOG(LogLoomlePipe, Display, TEXT("Loomle client connected on %s"), *FullPipePath);
        Async(EAsyncExecution::Thread, [this, LocalPipe, ConnectionSerial]()
        {
            HandleWindowsClient(LocalPipe, ConnectionSerial);
        });
    }
#else
    const FString SocketPath = GetSocketPath();
    const FTCHARToUTF8 PathUtf8(*SocketPath);

    const int32 LocalServerFd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (LocalServerFd < 0)
    {
        UE_LOG(LogLoomlePipe, Error, TEXT("Failed to create unix socket at %s"), *SocketPath);
        FPlatformProcess::Sleep(1.0f);
        return 0;
    }

    ServerFd = LocalServerFd;
    unlink(PathUtf8.Get());

    sockaddr_un Address;
    FMemory::Memzero(Address);
    Address.sun_family = AF_UNIX;
    FCStringAnsi::Strncpy(Address.sun_path, PathUtf8.Get(), UE_ARRAY_COUNT(Address.sun_path));

    if (bind(LocalServerFd, reinterpret_cast<const sockaddr*>(&Address), sizeof(Address)) != 0)
    {
        UE_LOG(LogLoomlePipe, Error, TEXT("Failed to bind unix socket %s"), *SocketPath);
        close(LocalServerFd);
        ServerFd = -1;
        FPlatformProcess::Sleep(1.0f);
        return 0;
    }

    if (listen(LocalServerFd, UnixSocketListenBacklog) != 0)
    {
        UE_LOG(LogLoomlePipe, Error, TEXT("Failed to listen on unix socket %s"), *SocketPath);
        close(LocalServerFd);
        ServerFd = -1;
        FPlatformProcess::Sleep(1.0f);
        return 0;
    }
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

        const int32 ConnectionSerial = NextConnectionSerial.Increment();
        RegisterConnection(ConnectionSerial, reinterpret_cast<void*>(static_cast<UPTRINT>(LocalClientFd)));
        ActiveWorkerCount.Increment();
        UE_LOG(LogLoomlePipe, Display, TEXT("Loomle client connected on unix socket %s"), *SocketPath);
        Async(EAsyncExecution::Thread, [this, LocalClientFd, ConnectionSerial]()
        {
            HandleUnixClient(LocalClientFd, ConnectionSerial);
        });
    }

    if (LocalServerFd >= 0)
    {
        close(LocalServerFd);
    }
    ServerFd = -1;
    unlink(PathUtf8.Get());
#endif

    return 0;
}

bool FLoomlePipeServer::WriteMessage(const FString& Message)
{
    FScopeLock ScopeLock(&WriteMutex);
    const int32 Serial = ActiveConnectionSerial.GetValue();
    if (Serial == 0)
    {
        return false;
    }

#if PLATFORM_WINDOWS
    void** HandlePtr = ActivePipeHandles.Find(Serial);
    if (HandlePtr == nullptr || *HandlePtr == nullptr || *HandlePtr == INVALID_HANDLE_VALUE)
    {
        return false;
    }

    const FString MessageWithNewline = Message + TEXT("\n");
    FTCHARToUTF8 Utf8(*MessageWithNewline);
    DWORD BytesWritten = 0;
    const BOOL bOk = WriteFile(
        static_cast<HANDLE>(*HandlePtr),
        Utf8.Get(),
        static_cast<DWORD>(Utf8.Length()),
        &BytesWritten,
        nullptr);

    return bOk && BytesWritten == static_cast<DWORD>(Utf8.Length());
#else
    int32* ClientFdPtr = ActiveClientFds.Find(Serial);
    if (ClientFdPtr == nullptr || *ClientFdPtr < 0)
    {
        return false;
    }

    const FString MessageWithNewline = Message + TEXT("\n");
    FTCHARToUTF8 Utf8(*MessageWithNewline);
    const ssize_t BytesWritten = write(*ClientFdPtr, Utf8.Get(), static_cast<size_t>(Utf8.Length()));
    return BytesWritten == Utf8.Length();
#endif
}

bool FLoomlePipeServer::WriteMessageForConnection(const FString& Message, int32 ExpectedConnectionSerial)
{
    FScopeLock ScopeLock(&WriteMutex);
    if (ExpectedConnectionSerial == 0)
    {
        return false;
    }

#if PLATFORM_WINDOWS
    void** HandlePtr = ActivePipeHandles.Find(ExpectedConnectionSerial);
    if (HandlePtr == nullptr || *HandlePtr == nullptr || *HandlePtr == INVALID_HANDLE_VALUE)
    {
        return false;
    }

    const FString MessageWithNewline = Message + TEXT("\n");
    FTCHARToUTF8 Utf8(*MessageWithNewline);
    DWORD BytesWritten = 0;
    const BOOL bOk = WriteFile(
        static_cast<HANDLE>(*HandlePtr),
        Utf8.Get(),
        static_cast<DWORD>(Utf8.Length()),
        &BytesWritten,
        nullptr);

    return bOk && BytesWritten == static_cast<DWORD>(Utf8.Length());
#else
    int32* ClientFdPtr = ActiveClientFds.Find(ExpectedConnectionSerial);
    if (ClientFdPtr == nullptr || *ClientFdPtr < 0)
    {
        return false;
    }

    const FString MessageWithNewline = Message + TEXT("\n");
    FTCHARToUTF8 Utf8(*MessageWithNewline);
    const ssize_t BytesWritten = write(*ClientFdPtr, Utf8.Get(), static_cast<size_t>(Utf8.Length()));
    return BytesWritten == Utf8.Length();
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
    TArray<uint8> PendingBytes;
    PendingBytes.Reserve(64 * 1024);

    while (!bStopRequested)
    {
        uint8 Buffer[4096];
        DWORD BytesRead = 0;
        const BOOL bReadOk = ReadFile(LocalPipe, Buffer, sizeof(Buffer), &BytesRead, nullptr);
        if (!bReadOk || BytesRead == 0)
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

            if (!TryBeginInFlight())
            {
                if (!WriteMessageForConnection(MakeBusyErrorResponse(RequestLine), ConnectionSerial))
                {
                    UE_LOG(LogLoomlePipe, Warning, TEXT("Failed writing busy response to client pipe"));
                    break;
                }
                continue;
            }

            FRequestHandler HandlerSnapshot = RequestHandler;
            const FString Response = HandlerSnapshot(RequestLine);
            EndInFlight();
            if (!Response.IsEmpty() && !WriteMessageForConnection(Response, ConnectionSerial))
            {
                UE_LOG(LogLoomlePipe, Display, TEXT("Dropping response for stale or disconnected pipe client"));
                break;
            }
        }
    }

    UnregisterConnection(ConnectionSerial);
    FlushFileBuffers(LocalPipe);
    DisconnectNamedPipe(LocalPipe);
    CloseHandle(LocalPipe);
    UE_LOG(LogLoomlePipe, Display, TEXT("Loomle client disconnected"));
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

            if (!TryBeginInFlight())
            {
                if (!WriteMessageForConnection(MakeBusyErrorResponse(RequestLine), ConnectionSerial))
                {
                    UE_LOG(LogLoomlePipe, Warning, TEXT("Failed writing busy response to unix socket client"));
                    break;
                }
                continue;
            }

            FRequestHandler HandlerSnapshot = RequestHandler;
            const FString Response = HandlerSnapshot(RequestLine);
            EndInFlight();
            if (!Response.IsEmpty() && !WriteMessageForConnection(Response, ConnectionSerial))
            {
                UE_LOG(LogLoomlePipe, Display, TEXT("Dropping response for stale or disconnected socket client"));
                break;
            }
        }
    }

    UnregisterConnection(ConnectionSerial);
    close(LocalClientFd);
    UE_LOG(LogLoomlePipe, Display, TEXT("Loomle client disconnected"));
#endif
}

FString FLoomlePipeServer::GetSocketPath() const
{
#if PLATFORM_WINDOWS
    return FString();
#else
    return FPaths::Combine(FPaths::ProjectIntermediateDir(), TEXT("loomle.sock"));
#endif
}
