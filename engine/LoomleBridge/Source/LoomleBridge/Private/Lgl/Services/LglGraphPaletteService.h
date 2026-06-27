// Copyright 2026 Loomle contributors.

#pragma once

#include "CoreMinimal.h"

class FJsonObject;
struct FEdGraphSchemaAction;

namespace Loomle::Lgl
{
struct FLglObjectRequest;
struct FLglObjectResult;
struct FLglResolvedGraph;

class FLglGraphPaletteService
{
public:
    FLglObjectResult QueryPaletteEntries(
        const FLglObjectRequest& Request,
        const FLglResolvedGraph& ResolvedGraph) const;

    bool ResolvePaletteAction(
        const FLglResolvedGraph& ResolvedGraph,
        const FString& PaletteId,
        TSharedPtr<FEdGraphSchemaAction>& OutAction,
        FLglObjectResult& OutError) const;
};
}
