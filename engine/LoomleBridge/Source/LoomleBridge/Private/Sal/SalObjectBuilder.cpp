// Copyright 2026 Loomle contributors.

#include "SalObjectBuilder.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Misc/ScopeLock.h"

namespace Loomle::Sal
{
namespace
{
bool IsReservedLocalIdentifier(const FString& Text)
{
    static const TSet<FString> Reserved = {
        TEXT("true"), TEXT("false"), TEXT("null"),
        TEXT("target"), TEXT("domain"), TEXT("tree"),
        TEXT("context"), TEXT("palette"), TEXT("object"), TEXT("asset"),
        TEXT("blueprint"), TEXT("class"), TEXT("graph"),
        TEXT("state_tree"), TEXT("widget")
    };
    return Reserved.Contains(Text);
}

TSharedPtr<FJsonObject> MakeKindObject(const FString& Kind)
{
    TSharedPtr<FJsonObject> Object = MakeShared<FJsonObject>();
    Object->SetStringField(TEXT("kind"), Kind);
    return Object;
}

FString StableSemanticTag(const FString& Kind)
{
    if (Kind == TEXT("local_variable")
        || Kind == TEXT("owner_variable")
        || Kind == TEXT("owner_local_variable"))
    {
        return TEXT("variable");
    }
    if (Kind == TEXT("owner_dispatcher"))
    {
        return TEXT("dispatcher");
    }
    if (Kind == TEXT("owner_component"))
    {
        return TEXT("component");
    }
    return Kind;
}

bool CanEmitSemanticTag(const FString& Tag)
{
    return FSalObjectBuilder::IsLocalIdentifier(Tag);
}

TSet<const FJsonValue*>& ExplicitExpressionValues()
{
    static TSet<const FJsonValue*>* Values =
        new TSet<const FJsonValue*>();
    return *Values;
}

FCriticalSection& ExplicitExpressionValuesMutex()
{
    static FCriticalSection* Mutex = new FCriticalSection();
    return *Mutex;
}

void RegisterExplicitExpression(const FJsonValue* Value)
{
    FScopeLock Lock(&ExplicitExpressionValuesMutex());
    ExplicitExpressionValues().Add(Value);
}

void UnregisterExplicitExpression(const FJsonValue* Value)
{
    FScopeLock Lock(&ExplicitExpressionValuesMutex());
    ExplicitExpressionValues().Remove(Value);
}

class FExplicitSalExpressionValue final : public FJsonValueObject
{
public:
    explicit FExplicitSalExpressionValue(
        const TSharedPtr<FJsonObject>& InObject)
        : FJsonValueObject(InObject)
    {
        RegisterExplicitExpression(this);
    }

    virtual ~FExplicitSalExpressionValue() override
    {
        UnregisterExplicitExpression(this);
    }
};

TSharedPtr<FJsonValue> MakeExplicitExpressionValue(
    const TSharedPtr<FJsonObject>& Object)
{
    return MakeShared<FExplicitSalExpressionValue>(Object);
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
    return MakeExplicitExpressionValue(LocalObject(InName));
}

TSharedPtr<FJsonObject> StableObject(const FString& InKind, const FString& InId)
{
    TSharedPtr<FJsonObject> Object = MakeKindObject(TEXT("stable_ref"));
    TArray<FString> IdentityPath;
    InId.ParseIntoArray(IdentityPath, TEXT("/"), false);
    if (IdentityPath.IsEmpty() && !InId.IsEmpty())
    {
        IdentityPath.Add(InId);
    }
    TArray<TSharedPtr<FJsonValue>> Segments;
    Segments.Reserve(IdentityPath.Num());
    for (const FString& Segment : IdentityPath)
    {
        Segments.Add(String(Segment));
    }
    Object->SetArrayField(TEXT("identityPath"), Segments);
    const FString Tag = StableSemanticTag(InKind);
    if (CanEmitSemanticTag(Tag))
    {
        Object->SetStringField(TEXT("semanticTag"), Tag);
    }
    return Object;
}

TSharedPtr<FJsonValue> Stable(const FString& InKind, const FString& InId)
{
    return MakeExplicitExpressionValue(StableObject(InKind, InId));
}

TSharedPtr<FJsonValue> Name(const FString& InName)
{
    TSharedPtr<FJsonObject> Object = MakeKindObject(TEXT("name"));
    Object->SetStringField(TEXT("name"), InName);
    return MakeExplicitExpressionValue(Object);
}

TSharedPtr<FJsonValue> NameOrString(const FString& InName)
{
    return FSalObjectBuilder::IsLocalIdentifier(InName)
        ? Name(InName)
        : String(InName);
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
    return MakeExplicitExpressionValue(MemberObject(ObjectRef, Path));
}

TSharedPtr<FJsonObject> CallObject(const FString& Callee, const TSharedPtr<FJsonObject>& Args)
{
    TSharedPtr<FJsonObject> Object = MakeKindObject(TEXT("object"));
    Object->SetObjectField(
        TEXT("fields"),
        Args.IsValid() ? Args : MakeShared<FJsonObject>());
    if (CanEmitSemanticTag(Callee))
    {
        Object->SetStringField(TEXT("semanticTag"), Callee);
    }
    return Object;
}

TSharedPtr<FJsonValue> Call(const FString& Callee, const TSharedPtr<FJsonObject>& Args)
{
    return MakeExplicitExpressionValue(CallObject(Callee, Args));
}

bool IsExplicitExpression(const TSharedPtr<FJsonValue>& InValue)
{
    if (!InValue.IsValid())
    {
        return false;
    }
    FScopeLock Lock(&ExplicitExpressionValuesMutex());
    return ExplicitExpressionValues().Contains(InValue.Get());
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

bool FSalObjectBuilder::IsLocalIdentifier(const FString& Text)
{
    return IsIdentifier(Text) && !IsReservedLocalIdentifier(Text);
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
    if (IsReservedLocalIdentifier(Alias))
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
