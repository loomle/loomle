// Copyright 2026 Loomle contributors.

#pragma once

#include "CoreMinimal.h"
#include "../LglObjectModel.h"

class UBlueprint;
class UEdGraph;
class FJsonObject;

namespace Loomle::Lgl
{
struct FLglResolvedGraph
{
    FString AssetPath;
    UBlueprint* Blueprint = nullptr;
    UEdGraph* Graph = nullptr;
    FString GraphName;
    FString GraphId;
};

class FLglGraphResolver
{
public:
    bool Resolve(
        const TSharedPtr<FJsonObject>& Target,
        FLglResolvedGraph& OutGraph,
        FLglObjectResult& OutError) const;
};
}
