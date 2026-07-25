// Copyright 2026 Loomle contributors.

#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

namespace Loomle::Tests::Sal
{
inline bool TryReadObjectFields(
    const TSharedPtr<FJsonObject>& Expression,
    const TSharedPtr<FJsonObject>*& OutFields)
{
    OutFields = nullptr;
    FString Kind;
    return Expression.IsValid()
        && Expression->TryGetStringField(TEXT("kind"), Kind)
        && Kind == TEXT("object")
        && Expression->TryGetObjectField(
            TEXT("fields"),
            OutFields)
        && OutFields != nullptr;
}

inline bool IsReservedSemanticTag(const FString& Tag)
{
    static const TSet<FString> Reserved = {
        TEXT("true"),
        TEXT("false"),
        TEXT("null"),
        TEXT("target"),
        TEXT("domain"),
        TEXT("tree"),
        TEXT("context"),
        TEXT("palette"),
        TEXT("object"),
        TEXT("asset"),
        TEXT("blueprint"),
        TEXT("class"),
        TEXT("graph"),
        TEXT("state_tree"),
        TEXT("widget")};
    return Reserved.Contains(Tag);
}

inline bool TryReadObjectExpr(
    const TSharedPtr<FJsonObject>& Expression,
    const FString& ExpectedSemanticTag,
    const TSharedPtr<FJsonObject>*& OutFields)
{
    OutFields = nullptr;
    FString ActualSemanticTag;
    if (!TryReadObjectFields(Expression, OutFields))
    {
        return false;
    }
    if (Expression->TryGetStringField(
            TEXT("semanticTag"),
            ActualSemanticTag))
    {
        return ActualSemanticTag == ExpectedSemanticTag;
    }
    return IsReservedSemanticTag(ExpectedSemanticTag);
}

inline bool TryReadObjectExprTag(
    const TSharedPtr<FJsonObject>& Expression,
    FString& OutSemanticTag)
{
    OutSemanticTag.Reset();
    const TSharedPtr<FJsonObject>* Fields = nullptr;
    FString Kind;
    return Expression.IsValid()
        && Expression->TryGetStringField(TEXT("kind"), Kind)
        && Kind == TEXT("object")
        && Expression->TryGetObjectField(TEXT("fields"), Fields)
        && Fields != nullptr
        && Expression->TryGetStringField(
            TEXT("semanticTag"),
            OutSemanticTag);
}
}
