// Copyright 2026 Loomle contributors.

#pragma once

#include "CoreMinimal.h"

class FJsonObject;

namespace Loomle::Sal
{
/** Normalized SAL RPC entry point. Text parsing remains in the SDK. */
class FSalModule
{
public:
    static TSharedPtr<FJsonObject> BuildQueryResult(const TSharedPtr<FJsonObject>& Arguments);
    static TSharedPtr<FJsonObject> BuildPatchResult(const TSharedPtr<FJsonObject>& Arguments);
};
}
