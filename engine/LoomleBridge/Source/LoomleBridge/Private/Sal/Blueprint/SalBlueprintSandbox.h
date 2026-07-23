// Copyright 2026 Loomle contributors.

#pragma once

#include "CoreMinimal.h"
#include "UObject/StrongObjectPtr.h"

class UBlueprint;

namespace Loomle::Sal
{
/**
 * Mirrors the native preconditions reached by
 * FBlueprintEditorUtils::MarkBlueprintAsModified.
 */
bool ValidateBlueprintModificationClassState(
    UBlueprint* Blueprint,
    FString& OutMessage);

/**
 * Builds an isolated, transient Blueprint through UE's native duplication
 * path. The returned strong pointer owns the complete sandbox lifetime.
 */
TStrongObjectPtr<UBlueprint> MakeBlueprintSandbox(
    UBlueprint* Source,
    FString& OutMessage);
}
