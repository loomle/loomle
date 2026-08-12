// Copyright 2026 Loomle contributors.

#pragma once

#include "CoreMinimal.h"

class AActor;
class FJsonObject;
class UActorComponent;

namespace Loomle::Sal
{
struct FSalLevelComponentEntry
{
    FGuid ActorGuid;
    TWeakObjectPtr<AActor> Actor;
    TWeakObjectPtr<UActorComponent> Component;
    FString Source;
    FString Id;
    FString Name;
    FString Type;
    FString CreationMethod;
    FString DeclaringClass;
    bool bRegistered = false;
};

struct FSalLevelComponentSnapshot
{
    TArray<FSalLevelComponentEntry> Entries;
    TArray<TSharedPtr<FJsonObject>> Diagnostics;
    bool bIdentityComplete = true;
    bool bDiagnosticsTruncated = false;

    TArray<TSharedPtr<FJsonObject>> FinalDiagnostics(
        const FString& Operation) const;
};

/**
 * Builds the source-qualified identity set for already-loaded, exact
 * Level-owned Actors. The implementation is read-only and uses only bounded
 * traversal over already-loaded Component, CDO, and SCS structures.
 */
bool BuildLevelComponentSnapshot(
    const TArray<AActor*>& Actors,
    const FString& Operation,
    FSalLevelComponentSnapshot& Out,
    FString& OutReason);
}
