// Copyright 2026 Loomle contributors.

#pragma once

#include "CoreMinimal.h"

class FJsonObject;
struct FEdGraphSchemaAction;

namespace Loomle::Lgl
{
struct FLglObjectRequest;
struct FLglObjectResult;
struct FLglBlueprintResolvedGraph;

class FLglBlueprintGraphPaletteService
{
public:
    FLglObjectResult QueryPaletteEntries(
        const FLglObjectRequest& Request,
        const FLglBlueprintResolvedGraph& ResolvedGraph) const;

    bool ResolvePaletteAction(
        const FLglBlueprintResolvedGraph& ResolvedGraph,
        const FString& PaletteId,
        TSharedPtr<FEdGraphSchemaAction>& OutAction,
        FLglObjectResult& OutError) const;
};
}
