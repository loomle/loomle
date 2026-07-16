// Copyright 2026 Loomle contributors.

#pragma once

#include "CoreMinimal.h"
#include "SalModel.h"

class FJsonObject;

namespace Loomle::Sal
{
class FSalJson
{
public:
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
