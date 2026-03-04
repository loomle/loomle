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

void FLoomlePipeServer::Stop()
{
    bStopRequested = true;
    ActiveConnectionSerial.Set(0);

#if PLATFORM_WINDOWS
    if (PipeHandle != nullptr && PipeHandle != INVALID_HANDLE_VALUE)
    {
        HANDLE LocalHandle = static_cast<HANDLE>(PipeHandle);
        CancelIoEx(LocalHandle, nullptr);
        DisconnectNamedPipe(LocalHandle);
    }
#else
    if (ClientFd >= 0)
    {
        shutdown(ClientFd, SHUT_RDWR);
        close(ClientFd);
        ClientFd = -1;
    }

    if (ServerFd >= 0)
    {
        shutdown(ServerFd, SHUT_RDWR);
        close(ServerFd);
        ServerFd = -1;
    }
#endif
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
            1,
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

        PipeHandle = LocalPipe;
        const BOOL bConnected = ConnectNamedPipe(LocalPipe, nullptr) || (GetLastError() == ERROR_PIPE_CONNECTED);
        if (!bConnected)
        {
            UE_LOG(LogLoomlePipe, Warning, TEXT("ConnectNamedPipe failed, error=%lu"), GetLastError());
            CloseHandle(LocalPipe);
            PipeHandle = nullptr;
            continue;
        }
        const int32 ConnectionSerial = NextConnectionSerial.Increment();
        ActiveConnectionSerial.Set(ConnectionSerial);

        UE_LOG(LogLoomlePipe, Display, TEXT("Loomle client connected on %s"), *FullPipePath);

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

                TSharedPtr<FLoomlePipeServer, ESPMode::ThreadSafe> SharedSelf = AsShared();
                FRequestHandler HandlerSnapshot = RequestHandler;
                AsyncTask(ENamedThreads::GameThread, [SharedSelf, HandlerSnapshot, RequestLine, ConnectionSerial]()
                {
                    ON_SCOPE_EXIT
                    {
                        SharedSelf->EndInFlight();
                    };

                    const FString Response = HandlerSnapshot(RequestLine);
                    if (!Response.IsEmpty() && !SharedSelf->WriteMessageForConnection(Response, ConnectionSerial))
                    {
                        UE_LOG(LogLoomlePipe, Verbose, TEXT("Dropping response for stale or disconnected pipe client"));
                        return;
                    }
                });
            }
        }

        if (ActiveConnectionSerial.GetValue() == ConnectionSerial)
        {
            ActiveConnectionSerial.Set(0);
        }
        FlushFileBuffers(LocalPipe);
        DisconnectNamedPipe(LocalPipe);
        CloseHandle(LocalPipe);
        PipeHandle = nullptr;

        UE_LOG(LogLoomlePipe, Display, TEXT("Loomle client disconnected"));
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

    if (listen(LocalServerFd, 8) != 0)
    {
        UE_LOG(LogLoomlePipe, Error, TEXT("Failed to listen on unix socket %s"), *SocketPath);
        close(LocalServerFd);
        ServerFd = -1;
        FPlatformProcess::Sleep(1.0f);
        return 0;
    }

    while (!bStopRequested)
    {
        const int32 LocalClientFd = accept(LocalServerFd, nullptr, nullptr);
        if (LocalClientFd < 0)
        {
            if (!bStopRequested)
            {
                UE_LOG(LogLoomlePipe, Warning, TEXT("Failed to accept unix socket client on %s"), *SocketPath);
            }

            close(LocalServerFd);
            ServerFd = -1;
            continue;
        }

        ClientFd = LocalClientFd;
        const int32 ConnectionSerial = NextConnectionSerial.Increment();
        ActiveConnectionSerial.Set(ConnectionSerial);
        UE_LOG(LogLoomlePipe, Display, TEXT("Loomle client connected on unix socket %s"), *SocketPath);

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

                TSharedPtr<FLoomlePipeServer, ESPMode::ThreadSafe> SharedSelf = AsShared();
                FRequestHandler HandlerSnapshot = RequestHandler;
                AsyncTask(ENamedThreads::GameThread, [SharedSelf, HandlerSnapshot, RequestLine, ConnectionSerial]()
                {
                    ON_SCOPE_EXIT
                    {
                        SharedSelf->EndInFlight();
                    };

                    const FString Response = HandlerSnapshot(RequestLine);
                    if (!Response.IsEmpty() && !SharedSelf->WriteMessageForConnection(Response, ConnectionSerial))
                    {
                        UE_LOG(LogLoomlePipe, Verbose, TEXT("Dropping response for stale or disconnected socket client"));
                        return;
                    }
                });
            }
        }

        if (ActiveConnectionSerial.GetValue() == ConnectionSerial)
        {
            ActiveConnectionSerial.Set(0);
        }
        close(LocalClientFd);
        ClientFd = -1;
        UE_LOG(LogLoomlePipe, Display, TEXT("Loomle client disconnected"));
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
    return WriteMessageForConnection(Message, ActiveConnectionSerial.GetValue());
}

bool FLoomlePipeServer::WriteMessageForConnection(const FString& Message, int32 ExpectedConnectionSerial)
{
    FScopeLock ScopeLock(&WriteMutex);
    if (ExpectedConnectionSerial == 0 || ActiveConnectionSerial.GetValue() != ExpectedConnectionSerial)
    {
        return false;
    }

#if PLATFORM_WINDOWS
    if (PipeHandle == nullptr || PipeHandle == INVALID_HANDLE_VALUE)
    {
        return false;
    }

    const FString MessageWithNewline = Message + TEXT("\n");
    FTCHARToUTF8 Utf8(*MessageWithNewline);
    DWORD BytesWritten = 0;
    const BOOL bOk = WriteFile(
        static_cast<HANDLE>(PipeHandle),
        Utf8.Get(),
        static_cast<DWORD>(Utf8.Length()),
        &BytesWritten,
        nullptr);

    return bOk && BytesWritten == static_cast<DWORD>(Utf8.Length());
#else
    if (ClientFd < 0)
    {
        return false;
    }

    const FString MessageWithNewline = Message + TEXT("\n");
    FTCHARToUTF8 Utf8(*MessageWithNewline);
    const ssize_t BytesWritten = write(ClientFd, Utf8.Get(), static_cast<size_t>(Utf8.Length()));
    return BytesWritten == Utf8.Length();
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
