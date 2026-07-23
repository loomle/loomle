// Copyright 2026 Loomle contributors.

#pragma once

#include "CoreMinimal.h"
#include "HAL/CriticalSection.h"
#include "HAL/Runnable.h"
#include "HAL/ThreadSafeCounter.h"

#include <atomic>

class FRunnableThread;

enum class ELoomlePipeListenerState : uint8
{
    Starting,
    Listening,
    Failed,
    Stopping,
    Stopped,
};

class FLoomlePipeServer : public FRunnable, public TSharedFromThis<FLoomlePipeServer, ESPMode::ThreadSafe>
{
public:
    using FRequestHandler = TFunction<FString(int32, const FString&)>;
    using FConnectionClosedHandler = TFunction<void(int32)>;

    FLoomlePipeServer(const FString& InPipeName, FRequestHandler InHandler, FConnectionClosedHandler InConnectionClosedHandler = nullptr);
    virtual ~FLoomlePipeServer();

    bool Start();
    void StopServer();
    int32 GetActiveConnectionCount() const;
    ELoomlePipeListenerState GetListenerState() const;
    FString GetListenerStateName() const;

    virtual uint32 Run() override;
    virtual void Stop() override;

private:
    struct FConnectionState;

    bool TryBeginInFlight();
    void EndInFlight();
    void RegisterConnection(int32 ConnectionSerial, void* NativeHandle);
    void UnregisterConnection(int32 ConnectionSerial);
    void CloseConnectionState(const TSharedPtr<FConnectionState, ESPMode::ThreadSafe>& State);
    void CloseAllConnections();
    bool DispatchControlRequest(const FString& RequestLine, int32 ConnectionSerial);
    void DispatchRequest(FString RequestLine, int32 ConnectionSerial);
    bool WriteMessageForConnection(const FString& Message, int32 ExpectedConnectionSerial);
    void HandleWindowsClient(void* NativeHandle, int32 ConnectionSerial);
    void HandleUnixClient(int32 LocalClientFd, int32 ConnectionSerial);
    FString GetSocketPath() const;
    void SetListenerState(ELoomlePipeListenerState NewState);

private:
    struct FConnectionState
    {
#if PLATFORM_WINDOWS
        void* NativeHandle = nullptr;
#else
        int32 NativeHandle = -1;
#endif
        TAtomic<bool> bClosed { false };
        FCriticalSection WriteMutex;
    };

    FString PipeName;
    FRequestHandler RequestHandler;
    FConnectionClosedHandler ConnectionClosedHandler;

    FRunnableThread* Thread = nullptr;
    FThreadSafeBool bStopRequested = false;
    FThreadSafeCounter InFlightRequestCount;
    FThreadSafeCounter NextConnectionSerial;
    FThreadSafeCounter ActiveWorkerCount;
    std::atomic<uint8> ListenerState { static_cast<uint8>(ELoomlePipeListenerState::Stopped) };
    static constexpr int32 MaxInFlightRequests = 128;

#if PLATFORM_WINDOWS
    void* PendingPipeHandle = nullptr;
#endif
    int32 ServerFd = -1;
    TMap<int32, TSharedPtr<FConnectionState, ESPMode::ThreadSafe>> ActiveConnections;
    mutable FCriticalSection ConnectionsMutex;
};
