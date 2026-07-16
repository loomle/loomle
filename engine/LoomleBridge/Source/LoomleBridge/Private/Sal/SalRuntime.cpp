// Copyright 2026 Loomle contributors.

#include "SalRuntime.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "LoomleMutationResult.h"
#include "UObject/UnrealType.h"

namespace Loomle::Sal
{
FString ExprString(const TSharedPtr<FJsonValue>& Value)
{
    if (!Value.IsValid() || Value->IsNull())
    {
        return FString();
    }
    FString Text;
    if (Value->TryGetString(Text))
    {
        return Text;
    }
    double Number = 0.0;
    if (Value->TryGetNumber(Number))
    {
        return FString::SanitizeFloat(Number);
    }
    bool bBool = false;
    if (Value->TryGetBool(bBool))
    {
        return bBool ? TEXT("true") : TEXT("false");
    }
    const TSharedPtr<FJsonObject>* Object = nullptr;
    if (Value->TryGetObject(Object) && Object != nullptr && (*Object).IsValid())
    {
        FString Kind;
        (*Object)->TryGetStringField(TEXT("kind"), Kind);
        if (Kind == TEXT("name") || Kind == TEXT("local"))
        {
            (*Object)->TryGetStringField(TEXT("name"), Text);
            return Text;
        }
        (*Object)->TryGetStringField(TEXT("id"), Text);
        return Text;
    }
    return FString();
}

FString ConditionField(const TSharedPtr<FJsonObject>& Condition)
{
    const TSharedPtr<FJsonObject>* Field = nullptr;
    const TArray<TSharedPtr<FJsonValue>>* Path = nullptr;
    if (!Condition.IsValid()
        || !Condition->TryGetObjectField(TEXT("field"), Field)
        || Field == nullptr
        || !(*Field)->TryGetArrayField(TEXT("path"), Path)
        || Path == nullptr)
    {
        return FString();
    }
    TArray<FString> Segments;
    for (const TSharedPtr<FJsonValue>& Value : *Path)
    {
        FString Segment;
        if (Value.IsValid() && Value->TryGetString(Segment))
        {
            Segments.Add(Segment);
        }
    }
    return FString::Join(Segments, TEXT("."));
}

bool HasDetail(const FSalQuery& Query, const FString& Detail)
{
    return Query.With.Contains(Detail);
}

bool ParseNonNegativeInt32(const FString& Text, int32& OutValue)
{
    OutValue = 0;
    if (Text.IsEmpty())
    {
        return false;
    }
    uint64 Parsed = 0;
    for (const TCHAR Character : Text)
    {
        if (!FChar::IsDigit(Character))
        {
            return false;
        }
        Parsed = Parsed * 10 + static_cast<uint64>(Character - TEXT('0'));
        if (Parsed > static_cast<uint64>(MAX_int32))
        {
            return false;
        }
    }
    OutValue = static_cast<int32>(Parsed);
    return true;
}

FString ExportPropertyValue(const FProperty* Property, const void* Container)
{
    if (Property == nullptr || Container == nullptr)
    {
        return FString();
    }
    FString Text;
    const void* Value = Property->ContainerPtrToValuePtr<void>(Container);
    Property->ExportTextItem_Direct(Text, Value, nullptr, nullptr, PPF_None);
    return Text;
}

bool ImportPropertyValue(FProperty* Property, void* Container, const FString& Text, FString& OutError)
{
    OutError.Reset();
    if (Property == nullptr || Container == nullptr)
    {
        OutError = TEXT("Property or value container is unavailable.");
        return false;
    }
    void* Value = Property->ContainerPtrToValuePtr<void>(Container);
    const TCHAR* End = Property->ImportText_Direct(*Text, Value, nullptr, PPF_None, GLog);
    if (End == nullptr)
    {
        OutError = FString::Printf(TEXT("UE could not import value for %s."), *Property->GetName());
        return false;
    }
    while (*End != TEXT('\0') && FChar::IsWhitespace(*End))
    {
        ++End;
    }
    if (*End != TEXT('\0'))
    {
        OutError = FString::Printf(TEXT("Value for %s contains unconsumed text."), *Property->GetName());
        return false;
    }
    return true;
}

TSharedPtr<FJsonValue> NativeValue(const FString& Text)
{
    return MakeShared<FJsonValueString>(Text);
}

TSharedPtr<FJsonObject> MakeMutationResult(
    const TSharedPtr<FJsonObject>& Object,
    const TArray<TSharedPtr<FJsonObject>>& InDiagnostics,
    const bool bDryRun,
    const bool bValid,
    const bool bApplied,
    const FString& AssetPath,
    const FString& Operation,
    const TSharedPtr<FJsonObject>& Planned,
    const TSharedPtr<FJsonObject>& ResolvedRefs,
    const TSharedPtr<FJsonObject>& Diff)
{
    TArray<TSharedPtr<FJsonValue>> Diagnostics;
    Diagnostics.Reserve(InDiagnostics.Num());
    for (const TSharedPtr<FJsonObject>& Diagnostic : InDiagnostics)
    {
        if (Diagnostic.IsValid())
        {
            Diagnostics.Add(MakeShared<FJsonValueObject>(Diagnostic));
        }
    }
    return LoomleMutation::BuildMutationResult(
        Object,
        Diagnostics,
        bDryRun,
        bValid,
        bApplied,
        AssetPath,
        Operation,
        Planned,
        ResolvedRefs,
        Diff);
}

}
