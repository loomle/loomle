// Copyright 2026 Loomle contributors.

#pragma once

#include "CoreMinimal.h"
#include "SalModel.h"

class FJsonObject;

namespace Loomle::Sal
{
class LOOMLEBRIDGE_API FSalJson
{
public:
    /**
     * Validates one standalone canonical normalized Target value.
     *
     * This is the private JSON counterpart of canonical SAL Target Text. It
     * intentionally accepts no binding alias or Query/Patch envelope.
     */
    static bool ValidateCanonicalTarget(
        const TSharedPtr<FJsonObject>& Target,
        FString& OutMessage);

    static bool DecodeQuery(
        const TSharedPtr<FJsonObject>& Arguments,
        FSalQuery& OutQuery,
        TSharedPtr<FJsonObject>& OutError);

    static bool DecodePatch(
        const TSharedPtr<FJsonObject>& Arguments,
        FSalPatch& OutPatch,
        TSharedPtr<FJsonObject>& OutError);

    static bool ValidateResult(
        const TSharedPtr<FJsonObject>& Result,
        TSharedPtr<FJsonObject>& OutError);
};
}
