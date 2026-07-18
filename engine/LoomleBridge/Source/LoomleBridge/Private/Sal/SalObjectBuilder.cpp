// Copyright 2026 Loomle contributors.

#include "SalObjectBuilder.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

namespace Loomle::Sal
{
namespace
{
TSharedPtr<FJsonObject> MakeKindObject(const FString& Kind)
{
    TSharedPtr<FJsonObject> Object = MakeShared<FJsonObject>();
    Object->SetStringField(TEXT("kind"), Kind);
    return Object;
}
}

namespace Value
{
TSharedPtr<FJsonValue> Null()
{
    return MakeShared<FJsonValueNull>();
}

TSharedPtr<FJsonValue> Bool(const bool InValue)
{
    return MakeShared<FJsonValueBoolean>(InValue);
}

TSharedPtr<FJsonValue> Number(const double InValue)
{
    return MakeShared<FJsonValueNumber>(InValue);
}

TSharedPtr<FJsonValue> String(const FString& InValue)
{
    return MakeShared<FJsonValueString>(InValue);
}

TSharedPtr<FJsonObject> LocalObject(const FString& InName)
{
    TSharedPtr<FJsonObject> Object = MakeKindObject(TEXT("local"));
    Object->SetStringField(TEXT("name"), InName);
    return Object;
}

TSharedPtr<FJsonValue> Local(const FString& InName)
{
    return MakeShared<FJsonValueObject>(LocalObject(InName));
}

TSharedPtr<FJsonObject> StableObject(const FString& InKind, const FString& InId)
{
    TSharedPtr<FJsonObject> Object = MakeKindObject(InKind);
    Object->SetStringField(TEXT("id"), InId);
    return Object;
}

TSharedPtr<FJsonValue> Stable(const FString& InKind, const FString& InId)
{
    return MakeShared<FJsonValueObject>(StableObject(InKind, InId));
}

TSharedPtr<FJsonValue> Name(const FString& InName)
{
    TSharedPtr<FJsonObject> Object = MakeKindObject(TEXT("name"));
    Object->SetStringField(TEXT("name"), InName);
    return MakeShared<FJsonValueObject>(Object);
}

TSharedPtr<FJsonObject> MemberObject(const TSharedPtr<FJsonObject>& ObjectRef, const TArray<FString>& Path)
{
    TSharedPtr<FJsonObject> Object = MakeKindObject(TEXT("member"));
    Object->SetObjectField(TEXT("object"), ObjectRef);
    TArray<TSharedPtr<FJsonValue>> Segments;
    Segments.Reserve(Path.Num());
    for (const FString& Segment : Path)
    {
        Segments.Add(String(Segment));
    }
    Object->SetArrayField(TEXT("path"), Segments);
    return Object;
}

TSharedPtr<FJsonValue> Member(const TSharedPtr<FJsonObject>& ObjectRef, const TArray<FString>& Path)
{
    return MakeShared<FJsonValueObject>(MemberObject(ObjectRef, Path));
}

TSharedPtr<FJsonObject> CallObject(const FString& Callee, const TSharedPtr<FJsonObject>& Args)
{
    TSharedPtr<FJsonObject> Object = MakeKindObject(TEXT("call"));
    Object->SetStringField(TEXT("callee"), Callee);
    Object->SetObjectField(TEXT("args"), Args.IsValid() ? Args : MakeShared<FJsonObject>());
    return Object;
}

TSharedPtr<FJsonValue> Call(const FString& Callee, const TSharedPtr<FJsonObject>& Args)
{
    return MakeShared<FJsonValueObject>(CallObject(Callee, Args));
}
}

bool FSalObjectBuilder::IsIdentifier(const FString& Text)
{
    const auto IsAsciiAlpha = [](const TCHAR Character)
    {
        return (Character >= TEXT('A') && Character <= TEXT('Z'))
            || (Character >= TEXT('a') && Character <= TEXT('z'));
    };
    const auto IsAsciiDigit = [](const TCHAR Character)
    {
        return Character >= TEXT('0') && Character <= TEXT('9');
    };
    if (Text.IsEmpty() || !(IsAsciiAlpha(Text[0]) || Text[0] == TEXT('_')))
    {
        return false;
    }
    for (const TCHAR Character : Text)
    {
        if (!(IsAsciiAlpha(Character) || IsAsciiDigit(Character) || Character == TEXT('_')))
        {
            return false;
        }
    }
    return Text != TEXT("true") && Text != TEXT("false") && Text != TEXT("null");
}

FString FSalObjectBuilder::SanitizeIdentifier(const FString& Text, const FString& Fallback)
{
    const auto IsAsciiAlpha = [](const TCHAR Character)
    {
        return (Character >= TEXT('A') && Character <= TEXT('Z'))
            || (Character >= TEXT('a') && Character <= TEXT('z'));
    };
    const auto IsAsciiDigit = [](const TCHAR Character)
    {
        return Character >= TEXT('0') && Character <= TEXT('9');
    };
    FString Alias;
    Alias.Reserve(Text.Len());
    for (const TCHAR Character : Text)
    {
        Alias.AppendChar(IsAsciiAlpha(Character) || IsAsciiDigit(Character) || Character == TEXT('_') ? Character : TEXT('_'));
    }
    if (Alias.IsEmpty())
    {
        Alias = Fallback.IsEmpty() ? TEXT("item") : Fallback;
    }
    if (IsAsciiDigit(Alias[0]))
    {
        Alias.InsertAt(0, TEXT('_'));
    }
    if (Alias == TEXT("true") || Alias == TEXT("false") || Alias == TEXT("null"))
    {
        Alias += TEXT("_item");
    }
    return Alias;
}

FString FSalObjectBuilder::UniqueAlias(const FString& Preferred)
{
    const FString Base = SanitizeIdentifier(Preferred);
    FString Alias = Base;
    int32 Suffix = 2;
    while (Aliases.Contains(Alias))
    {
        Alias = FString::Printf(TEXT("%s_%d"), *Base, Suffix++);
    }
    Aliases.Add(Alias);
    return Alias;
}

void FSalObjectBuilder::AddLocalBinding(const FString& Alias, const TSharedPtr<FJsonValue>& InValue)
{
    if (Alias.IsEmpty() || !InValue.IsValid() || BindingTargets.Contains(Alias))
    {
        return;
    }
    Aliases.Add(Alias);
    BindingTargets.Add(Alias);
    TSharedPtr<FJsonObject> Binding = MakeShared<FJsonObject>();
    Binding->SetObjectField(TEXT("target"), Value::LocalObject(Alias));
    Binding->SetField(TEXT("value"), InValue);
    Statements.Add(MakeShared<FJsonValueObject>(Binding));
}

void FSalObjectBuilder::AddMemberBinding(
    const FString& OwnerAlias,
    const TArray<FString>& Path,
    const TSharedPtr<FJsonValue>& InValue)
{
    if (!Aliases.Contains(OwnerAlias) || Path.IsEmpty() || !InValue.IsValid())
    {
        return;
    }
    const FString Key = OwnerAlias + TEXT(".") + FString::Join(Path, TEXT("."));
    if (BindingTargets.Contains(Key))
    {
        return;
    }
    BindingTargets.Add(Key);
    TSharedPtr<FJsonObject> Binding = MakeShared<FJsonObject>();
    Binding->SetObjectField(TEXT("target"), Value::MemberObject(Value::LocalObject(OwnerAlias), Path));
    Binding->SetField(TEXT("value"), InValue);
    Statements.Add(MakeShared<FJsonValueObject>(Binding));
}

void FSalObjectBuilder::AddEdge(const TSharedPtr<FJsonObject>& From, const TSharedPtr<FJsonObject>& To)
{
    if (!From.IsValid() || !To.IsValid())
    {
        return;
    }
    TSharedPtr<FJsonObject> Edge = MakeShared<FJsonObject>();
    Edge->SetObjectField(TEXT("from"), From);
    Edge->SetObjectField(TEXT("to"), To);
    Statements.Add(MakeShared<FJsonValueObject>(Edge));
}

void FSalObjectBuilder::AddComment(const FString& Text)
{
    if (Text.IsEmpty())
    {
        return;
    }
    FString SafeText = Text;
    if (SafeText != TEXT("###"))
    {
        TArray<FString> Lines;
        SafeText.ParseIntoArrayLines(Lines, false);
        for (FString& Line : Lines)
        {
            if (Line.TrimStartAndEnd() == TEXT("###"))
            {
                const int32 Delimiter = Line.Find(TEXT("###"));
                Line.InsertAt(Delimiter, TEXT('\\'));
            }
        }
        SafeText = FString::Join(Lines, TEXT("\n"));
    }
    TSharedPtr<FJsonObject> Comment = MakeKindObject(TEXT("comment"));
    Comment->SetStringField(TEXT("text"), SafeText);
    Statements.Add(MakeShared<FJsonValueObject>(Comment));
}

TSharedPtr<FJsonObject> FSalObjectBuilder::BuildObject() const
{
    TSharedPtr<FJsonObject> Object = MakeShared<FJsonObject>();
    Object->SetArrayField(TEXT("statements"), Statements);
    return Object;
}

TSharedPtr<FJsonObject> FSalObjectBuilder::BuildResult(const TArray<TSharedPtr<FJsonObject>>& InDiagnostics) const
{
    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetObjectField(TEXT("object"), BuildObject());
    TArray<TSharedPtr<FJsonValue>> Diagnostics;
    Diagnostics.Reserve(InDiagnostics.Num());
    for (const TSharedPtr<FJsonObject>& Diagnostic : InDiagnostics)
    {
        if (Diagnostic.IsValid())
        {
            Diagnostics.Add(MakeShared<FJsonValueObject>(Diagnostic));
        }
    }
    Result->SetArrayField(TEXT("diagnostics"), Diagnostics);
    return Result;
}
}
