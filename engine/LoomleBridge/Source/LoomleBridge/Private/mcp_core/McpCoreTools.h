#pragma once

#include "CoreMinimal.h"

class FJsonObject;

namespace Loomle::McpCore
{
TSharedPtr<FJsonObject> BuildInitializeResult(const TSharedPtr<FJsonObject>& Params);
TSharedPtr<FJsonObject> BuildToolsListResult();
bool IsKnownTool(const FString& Name);
}
