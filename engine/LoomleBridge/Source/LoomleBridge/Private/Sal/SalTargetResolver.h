// Copyright 2026 Loomle contributors.

#pragma once

#include "CoreMinimal.h"
#include "SalModel.h"

class FJsonObject;

namespace Loomle::Sal
{
class FSalTargetResolver
{
public:
    bool Resolve(
        const FString& Alias,
        const TSharedPtr<FJsonObject>& TargetValue,
        bool bForPatch,
        FSalResolvedTarget& OutTarget,
        TSharedPtr<FJsonObject>& OutError) const;

private:
    bool ResolveValue(
        const FString& Alias,
        const TSharedPtr<FJsonObject>& Value,
        bool bForPatch,
        FSalResolvedTarget& OutTarget,
        TSharedPtr<FJsonObject>& OutError) const;
};
}
