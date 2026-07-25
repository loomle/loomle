// Copyright 2026 Loomle contributors.

#pragma once

#include "CoreMinimal.h"

class FJsonObject;

namespace Loomle::Sal::ResultTargets
{
TSharedPtr<FJsonObject> Blueprint(
    const FString& AssetPath,
    const FString& BlueprintId);

TSharedPtr<FJsonObject> Graph(
    const FString& AssetPath,
    const FString& BlueprintId,
    const FString& GraphId);

/**
 * Retains one independent canonical Target and an explicit handoff.
 * Returns the related Target alias, or an empty string when Target duplicates
 * the active main Target or the supplied shape is invalid.
 */
FString AddHandoff(
    const TSharedPtr<FJsonObject>& Result,
    const TSharedPtr<FJsonObject>& Target,
    const FString& PreferredAlias,
    const FString& Purpose);
}
