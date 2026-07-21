// Copyright 2026 Loomle contributors.

#pragma once

#include "CoreMinimal.h"
#include "SalReferenceFacts.h"

class FJsonObject;
class IAssetRegistry;
struct FAssetData;

namespace Loomle::Sal
{
enum class EReferenceIndexScanStatus : uint8
{
    Parsed,
    Unsupported,
    Missing,
    Outdated,
    Oversized,
    Corrupt,
    Cancelled
};

struct FReferenceIndexSite
{
    FString BlueprintPath;
    FString GraphCategory;
    FString GraphDisplayName;
    FString NodeId;
    FString NodeType;
    FString NodeTitle;
    FString MatchedPath;
};

struct FReferenceIndexTarget
{
    FCanonicalReference Identity;
    FString ScopeName;
    FTopLevelAssetPath OwnerClassPath;
};

struct FReferenceIndexScanResult
{
    EReferenceIndexScanStatus Status = EReferenceIndexScanStatus::Corrupt;
    TArray<FReferenceIndexSite> Sites;
    FString Message;
};

/**
 * Reads UE Find-in-Blueprints metadata without resolving or loading an asset.
 *
 * FiB is deliberately treated as a versioned, partial fact source. The caller
 * owns coverage diagnostics and may overlay native facts from an asset that was
 * already loaded before the Query began.
 */
class FSalReferenceIndex
{
public:
    static FTopLevelAssetPath ResolveOwnerClassPath(
        IAssetRegistry& Registry,
        const FCanonicalReference& Identity);

    static FReferenceIndexScanResult ScanAsset(
        const FAssetData& Data,
        IAssetRegistry& Registry,
        const FReferenceIndexTarget& Target,
        TFunctionRef<bool()> IsCancelled);

#if WITH_DEV_AUTOMATION_TESTS
    static FReferenceIndexScanResult ScanDecodedForTesting(
        const TSharedPtr<FJsonObject>& Root,
        const TMap<int32, FText>& Lookup,
        const FString& CandidateBlueprintPath,
        const TSet<FTopLevelAssetPath>& CandidateClassLineage,
        const FReferenceIndexTarget& Target,
        bool bCandidateClassLineageComplete = true);

    static bool ValidateEncodedLayoutForTesting(
        const FString& Encoded,
        bool bVersioned,
        FString& OutError);
    static bool LookupRequiresAssetResolutionForTesting(const TMap<int32, FText>& Lookup);
#endif
};
}
