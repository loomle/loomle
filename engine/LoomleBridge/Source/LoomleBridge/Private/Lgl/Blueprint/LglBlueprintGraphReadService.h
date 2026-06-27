// Copyright 2026 Loomle contributors.

#pragma once

#include "CoreMinimal.h"
#include "../LglObjectModel.h"

namespace Loomle::Lgl
{
struct FLglBlueprintResolvedGraph;

class FLglBlueprintGraphReadService
{
public:
    FLglObjectResult Query(const FLglObjectRequest& Request, const FLglBlueprintResolvedGraph& ResolvedGraph) const;
};
}
