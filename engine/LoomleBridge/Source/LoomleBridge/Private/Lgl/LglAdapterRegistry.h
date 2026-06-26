// Copyright 2026 Loomle contributors.

#pragma once

#include "CoreMinimal.h"

namespace Loomle::Lgl
{
class ILglDomainAdapter;

class FLglAdapterRegistry
{
public:
    void Register(TSharedRef<ILglDomainAdapter> Adapter);
    ILglDomainAdapter* Find(const FString& Domain) const;

private:
    TMap<FString, TSharedRef<ILglDomainAdapter>> AdaptersByDomain;
};
}
