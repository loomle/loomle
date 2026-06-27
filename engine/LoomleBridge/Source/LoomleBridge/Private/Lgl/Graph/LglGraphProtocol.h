// Copyright 2026 Loomle contributors.

#pragma once

#include "CoreMinimal.h"

class FJsonObject;
class FJsonValue;

namespace Loomle::Lgl
{
struct FLglObjectRequest;

struct FLglGraphPinRef
{
    FString Node;
    FString Pin;
};

struct FLglGraphEdge
{
    FLglGraphPinRef From;
    FLglGraphPinRef To;
};

TArray<TSharedPtr<FJsonValue>> ReadGraphPatchOps(const FLglObjectRequest& Request);
FString ReadGraphPatchOpKind(const TSharedPtr<FJsonValue>& OpValue);
TSharedPtr<FJsonValue> MakeGraphPatchDiagnostic(
    const FString& Severity,
    const FString& Code,
    const FString& Message,
    int32 Index,
    const FString& Operation);
bool ReadGraphPinRef(
    const TSharedPtr<FJsonObject>& Object,
    FLglGraphPinRef& OutPin,
    FString& OutErrorCode,
    FString& OutErrorMessage);
bool ReadGraphEdge(
    const TSharedPtr<FJsonObject>& EdgeObject,
    FLglGraphEdge& OutEdge,
    FString& OutErrorCode,
    FString& OutErrorMessage);
TSharedPtr<FJsonObject> MakeGraphEdgeObject(const FLglGraphEdge& Edge);
TSharedPtr<FJsonObject> BuildGraphResolvedRefs(
    const FString& AssetPath,
    const FString& GraphName,
    const FString& GraphId);
void SetGraphResolvedNode(
    const TSharedPtr<FJsonObject>& ResolvedRefs,
    const FString& BindingName,
    const FString& NodeIdValue);
}
