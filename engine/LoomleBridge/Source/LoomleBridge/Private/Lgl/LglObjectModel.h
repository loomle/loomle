// Copyright 2026 Loomle contributors.

#pragma once

#include "CoreMinimal.h"

class FJsonObject;

namespace Loomle::Lgl
{
struct FLglObjectRequest
{
    FString Method;
    TSharedPtr<FJsonObject> Object;
};

struct FLglObjectResult
{
    TSharedPtr<FJsonObject> Object;
    TArray<TSharedPtr<FJsonObject>> Diagnostics;
    TSharedPtr<FJsonObject> Page;
};
}
