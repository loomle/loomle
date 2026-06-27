// Copyright 2026 Loomle contributors.

#include "LglBlueprintAdapter.h"

#include "LglBlueprintGraphService.h"

namespace Loomle::Lgl
{
FString FLglBlueprintAdapter::GetDomain() const
{
    return TEXT("blueprint");
}

FLglObjectResult FLglBlueprintAdapter::Query(const FLglObjectRequest& Request)
{
    FLglBlueprintGraphService GraphService;
    return GraphService.Query(Request);
}

FLglObjectResult FLglBlueprintAdapter::Patch(const FLglObjectRequest& Request)
{
    FLglBlueprintGraphService GraphService;
    return GraphService.Patch(Request);
}
}
