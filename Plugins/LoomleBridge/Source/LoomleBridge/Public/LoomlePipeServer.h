#pragma once

#include "CoreMinimal.h"
#include "HAL/CriticalSection.h"
#include "HAL/Runnable.h"

class FRunnableThread;

class FLoomlePipeServer : public FRunnable
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
    bool WriteMessage(const FString& Message);
    FString GetSocketPath() const;

private:
    FString PipeName;
    FRequestHandler RequestHandler;

    FRunnableThread* Thread = nullptr;
    FThreadSafeBool bStopRequested = false;

#if PLATFORM_WINDOWS
    void* PipeHandle = nullptr;
#else
    int32 ServerFd = -1;
    int32 ClientFd = -1;
#endif
    FCriticalSection WriteMutex;
};
