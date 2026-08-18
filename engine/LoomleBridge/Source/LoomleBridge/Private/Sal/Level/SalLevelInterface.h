// Copyright 2026 Loomle contributors.

#pragma once

#include "CoreMinimal.h"
#include "../SalModel.h"

class FJsonObject;
class AActor;
class UActorComponent;
class ULevel;
class UWorld;

namespace Loomle::Sal
{
class LOOMLEBRIDGE_API FSalLevelInterface
{
public:
    static TSharedPtr<FJsonObject> Query(
        const FSalQuery& Query,
        const FSalResolvedTarget& Target);

    /**
     * Authored level mutation (Slice 3). Supports exact-schema `set` and
     * `reset` on loaded persisted Actor and Component scalar fields inside
     * one top-level transaction with dry-run planning, readback, and
     * rollback. Lifecycle, transform invoke, attachment, and save land in
     * later increments and fail closed here.
     */
    static TSharedPtr<FJsonObject> Patch(
        const FSalPatch& Patch,
        const FSalResolvedTarget& Target);

    /**
     * Map one opaque level Palette id to its creation kind ("actor" or
     * "component") for request lowering. The prefix is structural; the
     * adapter revalidates the full id against the exact destination.
     */
    static bool ResolveCreationKind(
        const FString& PaletteId,
        FString& OutKind);

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

    /**
     * Canonicalize one Level Editor source Level through the ordinary Level
     * Target resolver. This path reads Asset Registry and already-loaded
     * authored state only; it never loads or switches a map.
     */
    static bool ResolveEditorContextTarget(
        UWorld* EditorWorld,
        const ULevel* SourceLevel,
        FSalResolvedTarget& OutTarget,
        FString& OutCode,
        FString& OutMessage,
        FString& OutSuggestion);

    /**
     * Project one selected source Actor only after the same complete root
     * Actor/ActorDesc identity audit used by Level Query succeeds.
     */
    static bool ResolveEditorContextActor(
        const FSalResolvedTarget& Target,
        const AActor* Actor,
        FString& OutActorId,
        FString& OutCode,
        FString& OutMessage);
};
}
