// Copyright 2026 Loomle contributors.

#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

namespace LoomleMutation
{
    /**
     * Builds the shared normalized SAL mutation envelope. Interface code
     * supplies its ordered object text, diagnostics, plan, resolved references,
     * and diff while the required execution fields stay centralized.
     */
    static TSharedPtr<FJsonObject> BuildMutationResult(
        const TSharedPtr<FJsonObject>& Object,
        const TArray<TSharedPtr<FJsonValue>>& Diagnostics,
        const bool bDryRun,
        const bool bValid,
        const bool bApplied,
        const FString& AssetPath,
        const FString& Operation,
        const TSharedPtr<FJsonObject>& Planned = nullptr,
        const TSharedPtr<FJsonObject>& ResolvedRefs = nullptr,
        const TSharedPtr<FJsonObject>& Diff = nullptr)
    {
        TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
        if (Object.IsValid())
        {
            Result->SetObjectField(TEXT("object"), Object);
        }
        Result->SetArrayField(TEXT("diagnostics"), Diagnostics);
        Result->SetBoolField(TEXT("isError"), !bValid);
        Result->SetBoolField(TEXT("dryRun"), bDryRun);
        Result->SetBoolField(TEXT("valid"), bValid);
        Result->SetBoolField(TEXT("applied"), bApplied);
        Result->SetStringField(TEXT("operation"), Operation);
        if (!AssetPath.IsEmpty())
        {
            Result->SetStringField(TEXT("assetPath"), AssetPath);
        }
        if (Planned.IsValid())
        {
            Result->SetObjectField(TEXT("planned"), Planned);
        }
        if (ResolvedRefs.IsValid())
        {
            Result->SetObjectField(TEXT("resolvedRefs"), ResolvedRefs);
        }
        if (Diff.IsValid())
        {
            Result->SetObjectField(TEXT("diff"), Diff);
        }
        return Result;
    }
}
