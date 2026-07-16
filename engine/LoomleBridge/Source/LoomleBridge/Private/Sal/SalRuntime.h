// Copyright 2026 Loomle contributors.

#pragma once

#include "CoreMinimal.h"
#include "SalModel.h"

class FJsonObject;
class FJsonValue;
class FProperty;
class UObject;

namespace Loomle::Sal
{
FString ExprString(const TSharedPtr<FJsonValue>& Value);
FString ConditionField(const TSharedPtr<FJsonObject>& Condition);
bool HasDetail(const FSalQuery& Query, const FString& Detail);
bool ParseNonNegativeInt32(const FString& Text, int32& OutValue);

FString ExportPropertyValue(const FProperty* Property, const void* Container);
bool ImportPropertyValue(FProperty* Property, void* Container, const FString& Text, FString& OutError);
TSharedPtr<FJsonValue> NativeValue(const FString& Text);

TSharedPtr<FJsonObject> MakeMutationResult(
    const TSharedPtr<FJsonObject>& Object,
    const TArray<TSharedPtr<FJsonObject>>& Diagnostics,
    bool bDryRun,
    bool bValid,
    bool bApplied,
    const FString& AssetPath,
    const FString& Operation,
    const TSharedPtr<FJsonObject>& Planned = nullptr,
    const TSharedPtr<FJsonObject>& ResolvedRefs = nullptr,
    const TSharedPtr<FJsonObject>& Diff = nullptr);

}
