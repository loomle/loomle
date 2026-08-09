// Copyright 2026 Loomle contributors.

#pragma once

#include "CoreMinimal.h"

class FJsonObject;
class FJsonValue;

namespace Loomle::Sal
{
namespace Value
{
LOOMLEBRIDGE_API TSharedPtr<FJsonValue> Null();
LOOMLEBRIDGE_API TSharedPtr<FJsonValue> Bool(bool InValue);
LOOMLEBRIDGE_API TSharedPtr<FJsonValue> Number(double InValue);
LOOMLEBRIDGE_API TSharedPtr<FJsonValue> String(const FString& InValue);
LOOMLEBRIDGE_API TSharedPtr<FJsonValue> Name(const FString& InName);
LOOMLEBRIDGE_API TSharedPtr<FJsonValue> NameOrString(const FString& InName);
LOOMLEBRIDGE_API TSharedPtr<FJsonValue> Local(const FString& InName);
LOOMLEBRIDGE_API TSharedPtr<FJsonValue> Stable(const FString& InKind, const FString& InId);
LOOMLEBRIDGE_API TSharedPtr<FJsonValue> Member(const TSharedPtr<FJsonObject>& ObjectRef, const TArray<FString>& Path);
LOOMLEBRIDGE_API TSharedPtr<FJsonValue> Call(const FString& Callee, const TSharedPtr<FJsonObject>& Args);
LOOMLEBRIDGE_API bool IsExplicitExpression(const TSharedPtr<FJsonValue>& InValue);
LOOMLEBRIDGE_API TSharedPtr<FJsonObject> LocalObject(const FString& InName);
LOOMLEBRIDGE_API TSharedPtr<FJsonObject> StableObject(const FString& InKind, const FString& InId);
LOOMLEBRIDGE_API TSharedPtr<FJsonObject> MemberObject(const TSharedPtr<FJsonObject>& ObjectRef, const TArray<FString>& Path);
LOOMLEBRIDGE_API TSharedPtr<FJsonObject> CallObject(const FString& Callee, const TSharedPtr<FJsonObject>& Args);
}

class LOOMLEBRIDGE_API FSalObjectBuilder
{
public:
    static bool IsIdentifier(const FString& Text);
    static bool IsLocalIdentifier(const FString& Text);
    static FString SanitizeIdentifier(const FString& Text, const FString& Fallback = TEXT("item"));

    FString UniqueAlias(const FString& Preferred);

    void AddLocalBinding(const FString& Alias, const TSharedPtr<FJsonValue>& Value);
    void AddMemberBinding(const FString& OwnerAlias, const TArray<FString>& Path, const TSharedPtr<FJsonValue>& Value);
    void AddEdge(const TSharedPtr<FJsonObject>& From, const TSharedPtr<FJsonObject>& To);
    void AddComment(const FString& Text);

    TSharedPtr<FJsonObject> BuildObject() const;
    TSharedPtr<FJsonObject> BuildResult(const TArray<TSharedPtr<FJsonObject>>& Diagnostics = {}) const;

private:
    TArray<TSharedPtr<FJsonValue>> Statements;
    TSet<FString> Aliases;
    TSet<FString> BindingTargets;
};
}
