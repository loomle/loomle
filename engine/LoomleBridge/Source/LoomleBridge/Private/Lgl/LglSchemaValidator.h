// Copyright 2026 Loomle contributors.

#pragma once

#include "CoreMinimal.h"
#include "LglObjectModel.h"

namespace Loomle::Lgl
{
class FLglSchemaValidator
{
public:
    static bool ValidateRequest(
        const FLglObjectRequest& Request,
        const FString& ExpectedKind,
        FLglObjectResult& OutError);

    static bool ValidateResult(
        const FLglObjectResult& Result,
        FLglObjectResult& OutError);
};
}
