// Copyright 2026 Loomle contributors.

#pragma once

#include "CoreMinimal.h"

class FJsonObject;

namespace Loomle::Lgl
{
class FLglDiagnostics
{
public:
    static TSharedPtr<FJsonObject> Make(
        const FString& Severity,
        const FString& Code,
        const FString& Message,
        const FString& Suggestion = FString());
};
}
