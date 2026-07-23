// Copyright 2026 Loomle contributors.

#pragma once

#include "CoreMinimal.h"
#include "UObject/StrongObjectPtr.h"

class FJsonObject;
class UBlueprint;

namespace Loomle::Sal
{
struct FSalPatch;
struct FSalQuery;
struct FSalResolvedTarget;

class FSalGraphInterface
{
public:
    static TSharedPtr<FJsonObject> Query(const FSalQuery& Query, const FSalResolvedTarget& Target);
    static TSharedPtr<FJsonObject> Patch(const FSalPatch& Patch, const FSalResolvedTarget& Target);

#if WITH_DEV_AUTOMATION_TESTS
    static bool BuildSandboxTargetForTesting(
        const FSalResolvedTarget& Source,
        TStrongObjectPtr<UBlueprint>& OutSandboxOwner,
        FSalResolvedTarget& OutTarget,
        FString& OutError);
#endif
};
}
