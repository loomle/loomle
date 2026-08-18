// Copyright 2026 Loomle contributors.

#pragma once

#include "CoreMinimal.h"

class FJsonObject;

namespace Loomle::Sal
{
/**
 * Read-only Python `sal.object()` projection (Slice P-1). At Python
 * terminalization the Bridge walks the result JSON, replaces every reserved
 * `__loomle_sal_object__` marker in place with an independent normalized
 * projection record, and reports the projection annex. Views are produced by
 * the same Domain adapters as ordinary SAL Queries, so projection never
 * invents a new serialization and never loads, selects, dirties, or retains.
 */
class LOOMLEBRIDGE_API FSalProjectionService
{
public:
    static constexpr const TCHAR* MarkerKey = TEXT("__loomle_sal_object__");

    /**
     * Walk Result in place. Returns true when every marker resolved without a
     * projector or Bridge integrity fault; the annex `complete` flag is false
     * otherwise. The annex is omitted when the result contains no markers.
     */
    static bool ProjectResult(TSharedPtr<FJsonObject>& Result);
};
}
