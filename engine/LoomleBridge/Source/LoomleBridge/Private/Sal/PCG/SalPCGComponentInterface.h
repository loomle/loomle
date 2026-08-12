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
};
}
