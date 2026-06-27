// Copyright 2026 Loomle contributors.

#pragma once

#include "CoreMinimal.h"
#include "../LglObjectModel.h"

namespace Loomle::Lgl
{
class FLglBlueprintGraphService
{
public:
    FLglObjectResult Query(const FLglObjectRequest& Request);
    FLglObjectResult Patch(const FLglObjectRequest& Request);
};
}
