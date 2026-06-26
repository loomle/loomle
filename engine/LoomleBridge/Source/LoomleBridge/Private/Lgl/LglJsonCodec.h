// Copyright 2026 Loomle contributors.

#pragma once

#include "CoreMinimal.h"
#include "LglObjectModel.h"

class FJsonObject;

namespace Loomle::Lgl
{
class FLglJsonCodec
{
public:
    static bool DecodeObjectRequest(
        const FString& Method,
        const TSharedPtr<FJsonObject>& Arguments,
        FLglObjectRequest& OutRequest,
        FLglObjectResult& OutError);

    static TSharedPtr<FJsonObject> EncodeObjectResult(const FLglObjectResult& Result);
};
}
