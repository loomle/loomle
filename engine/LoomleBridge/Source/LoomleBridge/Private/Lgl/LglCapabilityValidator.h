// Copyright 2026 Loomle contributors.

#pragma once

#include "CoreMinimal.h"
#include "LglObjectModel.h"

namespace Loomle::Lgl
{
struct FLglQueryCapabilities
{
    FString Domain;

    bool bValidateFindKinds = false;
    TArray<FString> FindKinds;

    bool bValidateWhereFields = false;
    TArray<FString> WhereFields;

    bool bValidateDetails = false;
    TArray<FString> Details;

    bool bValidateOrderKeys = false;
    TArray<FString> OrderKeys;

    bool bSupportsPageAfter = false;
    bool bSupportsCompare = false;
};

class FLglCapabilityValidator
{
public:
    static bool ValidateQuery(
        const FLglObjectRequest& Request,
        const FLglQueryCapabilities& Capabilities,
        FLglObjectResult& OutError);
};
}
