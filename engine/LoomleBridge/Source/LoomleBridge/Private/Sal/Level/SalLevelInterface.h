// Copyright 2026 Loomle contributors.

#pragma once

#include "CoreMinimal.h"
#include "../SalModel.h"

class FJsonObject;

namespace Loomle::Sal
{
class LOOMLEBRIDGE_API FSalLevelInterface
{
public:
    static TSharedPtr<FJsonObject> Query(
        const FSalQuery& Query,
        const FSalResolvedTarget& Target);

    /**
     * Resolve one Level-scoped ActorGuid StableRef to the private exact Actor
     * operation consumed by Query. The identity audit covers loaded
     * source-Level Actors and the root World Partition Actor descriptor
     * container without loading or pinning an Actor.
     */
    static bool LowerStableReference(
        const FSalResolvedTarget& Target,
        const TArray<FString>& IdentityPath,
        FString& OutKind,
        FString& OutId,
        FString& OutCode,
        FString& OutMessage);
};
}
