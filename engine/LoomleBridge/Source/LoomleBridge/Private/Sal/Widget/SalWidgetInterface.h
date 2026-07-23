// Copyright 2026 Loomle contributors.

#pragma once

#include "CoreMinimal.h"
#include "../SalModel.h"
#include "UObject/StrongObjectPtr.h"

class FJsonObject;
class UBlueprint;
class UWidgetBlueprint;

namespace Loomle::Sal
{
class FSalWidgetInterface
{
public:
    static TSharedPtr<FJsonObject> Query(const FSalQuery& Query, const FSalResolvedTarget& Target);
    static TSharedPtr<FJsonObject> Patch(const FSalPatch& Patch, const FSalResolvedTarget& Target);

#if WITH_DEV_AUTOMATION_TESTS
    static UWidgetBlueprint* DuplicateForPreflightForTesting(
        UWidgetBlueprint* Source,
        TStrongObjectPtr<UBlueprint>& OutSandboxOwner,
        FString& OutError);
#endif
};
}
