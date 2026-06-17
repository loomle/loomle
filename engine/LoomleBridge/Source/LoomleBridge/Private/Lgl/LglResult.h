// Copyright 2026 Loomle contributors.

#pragma once

#include "CoreMinimal.h"

class FJsonObject;

namespace Loomle::Lgl
{
class FLglResult
{
public:
    static TSharedPtr<FJsonObject> Error(const TSharedPtr<FJsonObject>& Diagnostic);
};
}
