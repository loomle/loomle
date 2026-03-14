#pragma once

#include "CoreMinimal.h"
#include "HAL/CriticalSection.h"
#include "HAL/Runnable.h"
#include "HAL/ThreadSafeCounter.h"

class FRunnableThread;

class FLoomlePipeServer : public FRunnable, public TSharedFromThis<FLoomlePipeServer, ESPMode::ThreadSafe>
{
public:
    using FRequestHandler = TFunction<FString(const FString&)>;

    FLoomlePipeServer(const FString& InPipeName, FRequestHandler InHandler);
    virtual ~FLoomlePipeServer();

    bool Start();
    void StopServer();
    bool SendServerNotification(const FString& JsonMessage);

    virtual uint32 Run() override;
    virtual void Stop() override;

private:
    bool TryBeginInFlight();
    void EndInFlight();
    void RegisterConnection(int32 ConnectionSerial, void* NativeHandle);
    void UnregisterConnection(int32 ConnectionSerial);
    void CloseAllConnections();
    bool WriteMessage(const FString& Message);
    bool WriteMessageForConnection(const FString& Message, int32 ExpectedConnectionSerial);
    void HandleWindowsClient(void* NativeHandle, int32 ConnectionSerial);
    void HandleUnixClient(int32 LocalClientFd, int32 ConnectionSerial);
    FString GetSocketPath() const;

private:
    FString PipeName;
    FRequestHandler RequestHandler;

    FRunnableThread* Thread = nullptr;
    FThreadSafeBool bStopRequested = false;
    FThreadSafeCounter InFlightRequestCount;
    FThreadSafeCounter NextConnectionSerial;
    FThreadSafeCounter ActiveConnectionSerial;
    FThreadSafeCounter ActiveWorkerCount;
    static constexpr int32 MaxInFlightRequests = 128;

#if PLATFORM_WINDOWS
    void* PendingPipeHandle = nullptr;
    TMap<int32, void*> ActivePipeHandles;
#else
    int32 ServerFd = -1;
    TMap<int32, int32> ActiveClientFds;
#endif
    FCriticalSection WriteMutex;
};
