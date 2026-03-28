#include "mcp_core/McpCoreTransportHost.h"

#include "LoomleBridgeModule.h"

FMcpCoreTransportHost::FMcpCoreTransportHost(FLoomleBridgeModule& InModule)
    : Module(InModule)
{
}

FString FMcpCoreTransportHost::HandleConnectionMessage(int32 ConnectionSerial, const FString& RequestLine)
{
    return Module.HandleRequest(ConnectionSerial, RequestLine);
}

void FMcpCoreTransportHost::HandleConnectionClosed(int32 ConnectionSerial)
{
    Module.ForgetMcpSessionState(ConnectionSerial);
}
