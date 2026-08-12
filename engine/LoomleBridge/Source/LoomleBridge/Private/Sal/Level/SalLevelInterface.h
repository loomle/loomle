// Copyright 2026 Loomle contributors.

#pragma once

#include "CoreMinimal.h"
#include "../SalModel.h"

class FJsonObject;
class UActorComponent;

namespace Loomle::Sal
{
class LOOMLEBRIDGE_API FSalLevelInterface
{
public:
    static TSharedPtr<FJsonObject> Query(
        const FSalQuery& Query,
        const FSalResolvedTarget& Target);

    /** Resolve a Level-scoped Actor or source-qualified Component StableRef. */
    static bool LowerStableReference(
        const FSalResolvedTarget& Target,
        const TArray<FString>& IdentityPath,
        const TSharedPtr<FJsonObject>& Ref,
        FString& OutCode,
        FString& OutMessage);

    /**
     * Resolve one already-loaded, source-qualified persistent Component using
     * the same bounded Level Actor and Component identity audit as Query.
     * This helper never loads or pins an unloaded Actor.
     */
    static bool ResolveExactComponent(
        const FSalResolvedTarget& Target,
        const FString& ActorId,
        const FString& Source,
        const FString& Id,
        UActorComponent*& OutComponent,
        FString& OutCanonicalActorId,
        FString& OutCanonicalSource,
        FString& OutCanonicalId,
        FString& OutName,
        FString& OutType,
        FString& OutCreationMethod,
        FString& OutDeclaringClass,
        FString& OutCode,
        FString& OutMessage);
};
}
