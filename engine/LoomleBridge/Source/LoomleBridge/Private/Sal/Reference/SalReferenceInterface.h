// Copyright 2026 Loomle contributors.

#pragma once

#include "CoreMinimal.h"

class FJsonObject;

namespace Loomle::Sal
{
struct FSalQuery;
struct FSalResolvedTarget;

class FSalReferenceInterface
{
public:
    static void Startup();
    static void Shutdown();

    static TSharedPtr<FJsonObject> Query(
        const FSalQuery& Query,
        const FSalResolvedTarget& Target);
};
}
