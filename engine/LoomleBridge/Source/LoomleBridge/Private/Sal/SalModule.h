// Copyright 2026 Loomle contributors.

#pragma once

#include "CoreMinimal.h"

class FJsonObject;
class FJsonValue;

namespace Loomle::Sal
{
/** Normalized SAL RPC entry point. Text parsing remains in the SDK. */
class FSalModule
{
public:
    static TSharedPtr<FJsonObject> BuildQueryResult(const TSharedPtr<FJsonObject>& Arguments);
    static TSharedPtr<FJsonObject> BuildPatchResult(const TSharedPtr<FJsonObject>& Arguments);
#if WITH_DEV_AUTOMATION_TESTS
    static bool NormalizeOutputExpressionForTesting(
        const TSharedPtr<FJsonValue>& Value);
#endif
};
}
