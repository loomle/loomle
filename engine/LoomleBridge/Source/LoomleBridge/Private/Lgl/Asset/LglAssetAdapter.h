// Copyright 2026 Loomle contributors.

#pragma once

#include "CoreMinimal.h"
#include "../LglDomainAdapter.h"

namespace Loomle::Lgl
{
class FLglAssetRegistry;

class FLglAssetAdapter final : public ILglDomainAdapter
{
public:
    explicit FLglAssetAdapter(TSharedRef<FLglAssetRegistry> InAssetRegistry);

    virtual FString GetDomain() const override;
    virtual FLglObjectResult Query(const FLglObjectRequest& Request) override;
    virtual FLglObjectResult Patch(const FLglObjectRequest& Request) override;

private:
    TSharedRef<FLglAssetRegistry> AssetRegistry;
};
}
