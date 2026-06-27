// Copyright 2026 Loomle contributors.

#pragma once

#include "CoreMinimal.h"
#include "../LglObjectModel.h"

class UBlueprint;
class UEdGraph;
class FJsonObject;

namespace Loomle::Lgl
{
struct FLglBlueprintResolvedGraph
{
    FString AssetPath;
    UBlueprint* Blueprint = nullptr;
    UEdGraph* Graph = nullptr;
    FString GraphName;
    FString GraphId;
};

class FLglBlueprintGraphResolver
{
public:
    bool Resolve(
        const TSharedPtr<FJsonObject>& Target,
        FLglBlueprintResolvedGraph& OutGraph,
        FLglObjectResult& OutError) const;
};
}
