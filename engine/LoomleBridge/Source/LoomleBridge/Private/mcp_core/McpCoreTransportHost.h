#pragma once

#include "CoreMinimal.h"

class FLoomleBridgeModule;

class FMcpCoreTransportHost
{
public:
    explicit FMcpCoreTransportHost(FLoomleBridgeModule& InModule);

    FString HandleConnectionMessage(int32 ConnectionSerial, const FString& RequestLine);
    void HandleConnectionClosed(int32 ConnectionSerial);

private:
    FLoomleBridgeModule& Module;
};
