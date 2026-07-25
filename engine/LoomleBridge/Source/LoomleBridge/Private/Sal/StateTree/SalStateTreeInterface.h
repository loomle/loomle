// Copyright 2026 Loomle contributors.

#pragma once

#include "CoreMinimal.h"
#include "../SalModel.h"

class FJsonObject;

namespace Loomle::Sal
{
class FSalStateTreeInterface
{
public:
    static TSharedPtr<FJsonObject> Query(const FSalQuery& Query, const FSalResolvedTarget& Target);
    static TSharedPtr<FJsonObject> Patch(const FSalPatch& Patch, const FSalResolvedTarget& Target);
    static bool LowerStableReference(
        const FSalResolvedTarget& Target,
        const TArray<FString>& IdentityPath,
        FString& OutLegacyKind,
        FString& OutLegacyId,
        FString& OutCode,
        FString& OutMessage);
    /** Map one StateTree Palette identity to its private executor kind. */
    static bool ResolveCreationKind(
        const FString& Palette,
        FString& OutLegacyKind);
};
}
