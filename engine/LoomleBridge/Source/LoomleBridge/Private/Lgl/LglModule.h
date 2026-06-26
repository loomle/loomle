// Copyright 2026 Loomle contributors.

#pragma once

#include "CoreMinimal.h"

class FJsonObject;

namespace Loomle::Lgl
{
class FLglModule
{
public:
    static TSharedPtr<FJsonObject> BuildQueryResult(const TSharedPtr<FJsonObject>& Arguments);
    static TSharedPtr<FJsonObject> BuildPatchResult(const TSharedPtr<FJsonObject>& Arguments);
};
}
