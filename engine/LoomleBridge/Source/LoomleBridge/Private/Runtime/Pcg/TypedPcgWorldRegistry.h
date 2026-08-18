// Copyright 2026 Loomle contributors.

#pragma once

#include "CoreMinimal.h"

class FJsonObject;
class UPCGComponent;
class UWorld;

namespace Loomle::Runtime
{
/**
 * Normalized typed-PCG World selector. `worldKind` distinguishes the Editor
 * World from in-process PIE; SIE is a PIE World context with
 * `playMode: simulate`. Preview, inactive, transition, Standalone, and
 * New Process Worlds are unavailable.
 */
struct LOOMLEBRIDGE_API FTypedPcgWorldSelector
{
    FString WorldKind; // "editor" | "pie"
    FString PlayMode;  // "play" | "simulate" (pie only)
    int32 PieInstance = -1;

    bool Parse(const TSharedPtr<FJsonObject>& Selector, FString& OutError);
    bool IsValid() const;
    FString NormalizeText() const;
};

/**
 * One issued source-bound World ticket. It is an opaque short-lived freshness
 * precondition bound to the normalized selector, private World incarnation
 * epoch, persistent source Target, and live Component incarnation. It cannot
 * select or control a generic World and never survives World/Component
 * replacement or epoch invalidation.
 */
struct LOOMLEBRIDGE_API FTypedPcgTicket
{
    FString Id;            // opaque "pcg_<guid>"
    FString SelectorText;  // normalized selector token
    FString WorldPath;     // resolved world path at issue
    uint64 Epoch = 0;
    FString SourceTarget;  // canonical pcg_component target text
    FString ComponentPath; // live component path at issue
    TWeakObjectPtr<UWorld> World;
    double IssuedAtSeconds = 0.0;
};
using FTypedPcgTicketPtr = TSharedPtr<FTypedPcgTicket, ESPMode::ThreadSafe>;

/**
 * Bridge-private PCG World epoch registry. Keyed by native World identity and
 * an incarnation epoch, it proves exact live Worlds from normalized
 * selectors, maps a canonical `pcg_component` source to one live Component
 * incarnation, and issues source-bound opaque tickets with stale rejection.
 * It never publishes a Domain, Target, StableRef, handoff, or generic World
 * control surface.
 */
class LOOMLEBRIDGE_API FTypedPcgWorldRegistry
{
public:
    void Startup();
    void Shutdown();

    /** Resolve the selector to the exact live World (Editor or in-process
     *  PIE). Rejects preview, inactive, transition, Standalone, and New
     *  Process Worlds and any ambiguous multi-PIE routing. */
    static bool ResolveWorld(
        const FTypedPcgWorldSelector& Selector,
        UWorld*& OutWorld,
        FString& OutError);

    /**
     * Prove the canonical `pcg_component` source Target to one exact live
     * Component incarnation in the resolved World and issue a source-bound
     * ticket. `SourceTargetText` is the canonical target object JSON.
     */
    FTypedPcgTicketPtr Prepare(
        const FTypedPcgWorldSelector& Selector,
        const FString& SourceTargetText,
        FString& OutError);

    /**
     * Revalidate selector + ticket + source atomically and return the live
     * Component incarnation. Fails stale on expiry, epoch/World replacement,
     * selector or source mismatch, or Component reconstruction.
     */
    bool Validate(
        const FTypedPcgWorldSelector& Selector,
        const FTypedPcgTicketPtr& Ticket,
        const FString& SourceTargetText,
        UPCGComponent*& OutComponent,
        FString& OutError);

    void InvalidateAll();

    static constexpr double TicketLifetimeSeconds = 60.0;

private:
    uint64 NextEpochLocked(const FString& WorldPath);
    bool ResolveComponentForWorld(
        const FTypedPcgWorldSelector& Selector,
        UWorld* World,
        const FString& SourceTargetText,
        UPCGComponent*& OutComponent,
        FString& OutError);

    mutable FCriticalSection Mutex;
    TMap<FString, uint64> Epochs; // world path -> current epoch
    TMap<FString, FTypedPcgTicketPtr> Tickets;
    bool bInitialized = false;
};
}
