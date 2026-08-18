// Copyright 2026 Loomle contributors.

#pragma once

#include "CoreMinimal.h"
#include "../SalModel.h"

class FJsonObject;

namespace Loomle::Sal
{
class LOOMLEBRIDGE_API FSalPCGComponentInterface
{
public:
    static TSharedPtr<FJsonObject> Query(
        const FSalQuery& Query,
        const FSalResolvedTarget& Target);

    static bool LowerStableReference(
        const FSalResolvedTarget& Target,
        const TArray<FString>& IdentityPath,
        const TSharedPtr<FJsonObject>& Ref,
        FString& OutCode,
        FString& OutMessage);

    /**
     * Authored pcg_component Patch (edit-guard increment). Admission requires
     * one exact original authored Component and the async edit guard: the
     * Component must be idle (no generation, cleanup, or refresh task) before
     * and after the transaction, or the Patch fails closed with
     * capability.pcg_async_unproven. Certified operations are set/reset of the
     * exact scalar Seed on the Component itself; every other statement fails
     * closed. The Component is never saved here; a terminal save returns the
     * owning level handoff.
     */
    static TSharedPtr<FJsonObject> Patch(
        const FSalPatch& Patch,
        const FSalResolvedTarget& Target);
};
}
