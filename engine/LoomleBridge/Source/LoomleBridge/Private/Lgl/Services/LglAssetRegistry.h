// Copyright 2026 Loomle contributors.

#pragma once

#include "CoreMinimal.h"

class FJsonObject;

namespace Loomle::Lgl
{
class FLglAssetRegistry
{
public:
    bool IsAvailable() const;
    void SearchAssets(
        const FString& Text,
        int32 Limit,
        const TSharedPtr<FJsonObject>& Where,
        TArray<TSharedPtr<FJsonObject>>& OutAssets) const;
};
}
