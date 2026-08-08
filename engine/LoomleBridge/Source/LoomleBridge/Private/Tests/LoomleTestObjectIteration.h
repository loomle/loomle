// Copyright 2026 Loomle contributors.

#pragma once

#include "Misc/EngineVersionComparison.h"
#include "UObject/UObjectHash.h"

namespace Loomle::Tests
{
#if UE_VERSION_NEWER_THAN_OR_EQUAL(5, 8, 0)
inline constexpr EGetObjectsFlags IncludeNestedObjects =
    EGetObjectsFlags::IncludeNestedObjects;
#else
inline constexpr bool IncludeNestedObjects = true;
#endif
}
