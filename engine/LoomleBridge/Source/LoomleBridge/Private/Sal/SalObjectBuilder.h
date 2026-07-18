// Copyright 2026 Loomle contributors.

#pragma once

#include "CoreMinimal.h"

class FJsonObject;
class FJsonValue;

namespace Loomle::Sal
{
namespace Value
{
TSharedPtr<FJsonValue> Null();
TSharedPtr<FJsonValue> Bool(bool InValue);
TSharedPtr<FJsonValue> Number(double InValue);
TSharedPtr<FJsonValue> String(const FString& InValue);
TSharedPtr<FJsonValue> Name(const FString& InName);
TSharedPtr<FJsonValue> Local(const FString& InName);
TSharedPtr<FJsonValue> Stable(const FString& InKind, const FString& InId);
TSharedPtr<FJsonValue> Member(const TSharedPtr<FJsonObject>& ObjectRef, const TArray<FString>& Path);
TSharedPtr<FJsonValue> Call(const FString& Callee, const TSharedPtr<FJsonObject>& Args);
TSharedPtr<FJsonObject> LocalObject(const FString& InName);
TSharedPtr<FJsonObject> StableObject(const FString& InKind, const FString& InId);
TSharedPtr<FJsonObject> MemberObject(const TSharedPtr<FJsonObject>& ObjectRef, const TArray<FString>& Path);
TSharedPtr<FJsonObject> CallObject(const FString& Callee, const TSharedPtr<FJsonObject>& Args);
}

class FSalObjectBuilder
{
public:
    static bool IsIdentifier(const FString& Text);
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
