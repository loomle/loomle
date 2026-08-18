// Copyright 2026 Loomle contributors.

#pragma once

#include "CoreMinimal.h"
#include "../SalModel.h"

class FJsonObject;

namespace Loomle::Sal
{
class LOOMLEBRIDGE_API FSalPCGInterface
{
public:
    static TSharedPtr<FJsonObject> Query(
        const FSalQuery& Query,
        const FSalResolvedTarget& Target);

    /**
     * Authored PCG Graph mutation (Slice 2). Supports Palette-backed Node
     * creation in this increment; settings set/reset, move, connection,
     * removal, and save land in later increments and fail closed here.
     */
    static TSharedPtr<FJsonObject> Patch(
        const FSalPatch& Patch,
        const FSalResolvedTarget& Target);

    /**
     * Resolve a PCG StableRef in the bound Graph and rewrite Ref to the exact
     * internal query subject. Pin identity remains segmented so native labels
     * containing '/', '.', spaces, or other punctuation are never fused.
     */
    static bool LowerStableReference(
        const FSalResolvedTarget& Target,
        const TArray<FString>& IdentityPath,
        const TSharedPtr<FJsonObject>& Ref,
        FString& OutCode,
        FString& OutMessage);
};
}
