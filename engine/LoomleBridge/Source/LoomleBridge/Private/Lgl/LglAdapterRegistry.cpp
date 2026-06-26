// Copyright 2026 Loomle contributors.

#include "LglAdapterRegistry.h"

#include "LglDomainAdapter.h"

namespace Loomle::Lgl
{
void FLglAdapterRegistry::Register(TSharedRef<ILglDomainAdapter> Adapter)
{
    AdaptersByDomain.Add(Adapter->GetDomain(), Adapter);
}

ILglDomainAdapter* FLglAdapterRegistry::Find(const FString& Domain) const
{
    const TSharedRef<ILglDomainAdapter>* Adapter = AdaptersByDomain.Find(Domain);
    return Adapter ? &Adapter->Get() : nullptr;
}
}
