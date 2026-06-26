// Copyright 2026 Loomle contributors.

#pragma once

#include "CoreMinimal.h"

class FJsonObject;

namespace Loomle::Lgl
{
struct FLglObjectResult;

class FLglResult
{
public:
    static FLglObjectResult FromDiagnostic(const TSharedPtr<FJsonObject>& Diagnostic);
    static TSharedPtr<FJsonObject> Error(const TSharedPtr<FJsonObject>& Diagnostic);
};
}
