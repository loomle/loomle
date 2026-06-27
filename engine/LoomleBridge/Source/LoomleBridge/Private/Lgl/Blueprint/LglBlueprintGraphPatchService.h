// Copyright 2026 Loomle contributors.

#pragma once

#include "CoreMinimal.h"
#include "../LglObjectModel.h"

namespace Loomle::Lgl
{
struct FLglBlueprintResolvedGraph;

class FLglBlueprintGraphPatchService
{
public:
    FLglObjectResult Patch(const FLglObjectRequest& Request, const FLglBlueprintResolvedGraph& ResolvedGraph) const;
};
}
