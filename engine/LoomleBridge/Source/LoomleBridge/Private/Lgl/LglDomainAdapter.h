// Copyright 2026 Loomle contributors.

#pragma once

#include "CoreMinimal.h"
#include "LglObjectModel.h"

namespace Loomle::Lgl
{
class ILglDomainAdapter
{
public:
    virtual ~ILglDomainAdapter() = default;

    virtual FString GetDomain() const = 0;
    virtual FLglObjectResult Query(const FLglObjectRequest& Request) = 0;
    virtual FLglObjectResult Patch(const FLglObjectRequest& Request) = 0;
};
}
