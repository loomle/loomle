// Copyright 2026 Loomle contributors.

#pragma once

#include "CoreMinimal.h"
#include "../LglDomainAdapter.h"

namespace Loomle::Lgl
{
class FLglGraphAdapter final : public ILglDomainAdapter
{
public:
    virtual FString GetDomain() const override;
    virtual FLglObjectResult Query(const FLglObjectRequest& Request) override;
    virtual FLglObjectResult Patch(const FLglObjectRequest& Request) override;
};
}
