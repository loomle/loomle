// Copyright 2026 Loomle contributors.

#pragma once

#include "CoreMinimal.h"
#include "Sal/SalModel.h"
#include "UObject/StrongObjectPtr.h"

class FJsonObject;

namespace Loomle::Sal
{
/** UE-backed implementation of the SAL blueprint interface. */
class LOOMLEBRIDGE_API FSalBlueprintInterface
{
public:
    static TSharedPtr<FJsonObject> Query(
        const FSalQuery& Query,
        const FSalResolvedTarget& Target);

    static TSharedPtr<FJsonObject> Patch(
        const FSalPatch& Patch,
        const FSalResolvedTarget& Target);

    static bool LowerStableReference(
        const FSalResolvedTarget& Target,
        const TArray<FString>& IdentityPath,
        FString& OutLegacyKind,
        FString& OutLegacyId,
        FString& OutCode,
        FString& OutMessage);

    /** Map one current Blueprint Palette identity to its private executor kind. */
    static bool ResolveCreationKind(
        const FString& Palette,
        FString& OutLegacyKind);

#if WITH_DEV_AUTOMATION_TESTS
    static TStrongObjectPtr<UBlueprint> MakeTransientPlanForTesting(
        UBlueprint* Source,
        FString& OutMessage);

    static bool ValidateModificationClassStateForTesting(
        UBlueprint* Blueprint,
        FString& OutMessage);
#endif
};
}
