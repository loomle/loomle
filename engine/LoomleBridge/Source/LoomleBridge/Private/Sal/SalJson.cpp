// Copyright 2026 Loomle contributors.

#include "SalJson.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "SalDiagnostics.h"

namespace Loomle::Sal
{
namespace
{
bool IsIdentifier(const FString& Text)
{
    auto IsAsciiAlpha = [](const TCHAR Character)
    {
        return (Character >= TEXT('A') && Character <= TEXT('Z'))
            || (Character >= TEXT('a') && Character <= TEXT('z'));
    };
    auto IsAsciiDigit = [](const TCHAR Character)
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
    return true;
}

bool IsLocalIdentifier(const FString& Text)
{
    return IsIdentifier(Text)
        && Text != TEXT("true")
        && Text != TEXT("false")
        && Text != TEXT("null");
}

bool IsStableId(const FString& Text)
{
    if (Text.IsEmpty() || Text.Contains(TEXT("."))) return false;
    for (const TCHAR Character : Text)
    {
        if (FChar::IsWhitespace(Character)) return false;
    }
    return true;
}

bool IsInlineToken(const FString& Text)
{
    if (Text.IsEmpty()) return false;
    for (const TCHAR Character : Text)
    {
        if (FChar::IsWhitespace(Character)) return false;
    }
    return true;
}

bool IsFieldPath(const FString& Text)
{
    TArray<FString> Segments;
    Text.ParseIntoArray(Segments, TEXT("."), false);
    if (Segments.IsEmpty())
    {
        return false;
    }
    for (const FString& Segment : Segments)
    {
        if (!IsIdentifier(Segment))
        {
            return false;
        }
    }
    return true;
}

TSharedPtr<FJsonObject> Invalid(const FString& Message, const TArray<FString>& Path = {})
{
    FSalDiagnosticBuilder Diagnostic = FSalDiagnostics::Error(TEXT("language.invalid_object_shape"), Message);
    if (!Path.IsEmpty())
    {
        Diagnostic.Path(Path);
    }
    return FSalDiagnostics::Result(Diagnostic.Build());
}

TSharedPtr<FJsonObject> InvalidResult(const FString& Message)
{
    return FSalDiagnostics::Result(
        FSalDiagnostics::Error(TEXT("language.invalid_result_shape"), Message).Build());
}

bool HasOnly(const TSharedPtr<FJsonObject>& Object, std::initializer_list<const TCHAR*> Allowed)
{
    if (!Object.IsValid())
    {
        return false;
    }
    TSet<FString> Fields;
    for (const TCHAR* Field : Allowed)
    {
        Fields.Add(Field);
    }
    for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : Object->Values)
    {
        if (!Fields.Contains(Pair.Key))
        {
            return false;
        }
    }
    return true;
}

bool ReadRequiredString(const TSharedPtr<FJsonObject>& Object, const TCHAR* Field, FString& Out)
{
    Out.Reset();
    return Object.IsValid() && Object->TryGetStringField(Field, Out) && !Out.IsEmpty();
}

bool ValidateExpr(const TSharedPtr<FJsonValue>& Value, FString& OutMessage);
bool ValidateRef(const TSharedPtr<FJsonObject>& Object, bool bBindingTarget, FString& OutMessage);

bool ValidateStringPath(const TSharedPtr<FJsonObject>& Object, const TCHAR* Field, FString& OutMessage)
{
    const TArray<TSharedPtr<FJsonValue>>* Path = nullptr;
    if (!Object->TryGetArrayField(Field, Path) || Path == nullptr || Path->IsEmpty())
    {
        OutMessage = FString::Printf(TEXT("%s must be a non-empty string path."), Field);
        return false;
    }
    for (const TSharedPtr<FJsonValue>& SegmentValue : *Path)
    {
        FString Segment;
        if (!SegmentValue.IsValid() || !SegmentValue->TryGetString(Segment) || !IsIdentifier(Segment))
        {
            OutMessage = FString::Printf(TEXT("%s contains an invalid path segment."), Field);
            return false;
        }
    }
    return true;
}

bool ValidateCall(const TSharedPtr<FJsonObject>& Object, FString& OutMessage)
{
    FString Callee;
    const TSharedPtr<FJsonObject>* Args = nullptr;
    if (!HasOnly(Object, {TEXT("kind"), TEXT("callee"), TEXT("args")})
        || !ReadRequiredString(Object, TEXT("callee"), Callee)
        || !IsIdentifier(Callee)
        || !Object->TryGetObjectField(TEXT("args"), Args)
        || Args == nullptr
        || !(*Args).IsValid())
    {
        OutMessage = TEXT("Call must contain only kind, a valid callee, and args.");
        return false;
    }
    for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : (*Args)->Values)
    {
        if (!IsIdentifier(Pair.Key) || !ValidateExpr(Pair.Value, OutMessage))
        {
            if (OutMessage.IsEmpty())
            {
                OutMessage = TEXT("Call contains an invalid named argument.");
            }
            return false;
        }
    }
    return true;
}

bool ValidateRef(const TSharedPtr<FJsonObject>& Object, const bool bBindingTarget, FString& OutMessage)
{
    FString Kind;
    if (!ReadRequiredString(Object, TEXT("kind"), Kind) || !IsLocalIdentifier(Kind))
    {
        OutMessage = TEXT("Reference requires a valid kind.");
        return false;
    }
    if (Object->HasField(TEXT("id")))
    {
        FString Id;
        if (bBindingTarget
            || !HasOnly(Object, {TEXT("kind"), TEXT("id")})
            || !ReadRequiredString(Object, TEXT("id"), Id)
            || !IsStableId(Id))
        {
            OutMessage = TEXT("Stable reference requires a kind and formatter-safe id without whitespace or dots.");
            return false;
        }
        return true;
    }
    if (Kind == TEXT("local"))
    {
        FString Name;
        if (!HasOnly(Object, {TEXT("kind"), TEXT("name")})
            || !ReadRequiredString(Object, TEXT("name"), Name)
            || !IsLocalIdentifier(Name))
        {
            OutMessage = TEXT("Local reference requires one valid name.");
            return false;
        }
        return true;
    }
    if (Kind == TEXT("member"))
    {
        const TSharedPtr<FJsonObject>* Owner = nullptr;
        if (!HasOnly(Object, {TEXT("kind"), TEXT("object"), TEXT("path")})
            || !Object->TryGetObjectField(TEXT("object"), Owner)
            || Owner == nullptr
            || !(*Owner).IsValid()
            || !ValidateStringPath(Object, TEXT("path"), OutMessage))
        {
            if (OutMessage.IsEmpty())
            {
                OutMessage = TEXT("Member reference requires object and a non-empty path.");
            }
            return false;
        }
        FString OwnerKind;
        (*Owner)->TryGetStringField(TEXT("kind"), OwnerKind);
        if (bBindingTarget && OwnerKind != TEXT("local"))
        {
            OutMessage = TEXT("A binding member owner must be a local reference.");
            return false;
        }
        return ValidateRef(*Owner, false, OutMessage) && OwnerKind != TEXT("member");
    }
    OutMessage = TEXT("Stable reference requires a kind and non-empty id.");
    return false;
}

bool ValidateExpr(const TSharedPtr<FJsonValue>& Value, FString& OutMessage)
{
    if (!Value.IsValid())
    {
        OutMessage = TEXT("Expression is missing.");
        return false;
    }
    if (Value->IsNull())
    {
        return true;
    }
    FString StringValue;
    double NumberValue = 0.0;
    bool BoolValue = false;
    if (Value->TryGetString(StringValue) || Value->TryGetNumber(NumberValue) || Value->TryGetBool(BoolValue))
    {
        return true;
    }
    const TArray<TSharedPtr<FJsonValue>>* Array = nullptr;
    if (Value->TryGetArray(Array) && Array != nullptr)
    {
        for (const TSharedPtr<FJsonValue>& Item : *Array)
        {
            if (!ValidateExpr(Item, OutMessage))
            {
                return false;
            }
        }
        return true;
    }
    const TSharedPtr<FJsonObject>* Object = nullptr;
    if (!Value->TryGetObject(Object) || Object == nullptr || !(*Object).IsValid())
    {
        OutMessage = TEXT("Unsupported expression value.");
        return false;
    }
    FString Kind;
    if ((*Object)->TryGetStringField(TEXT("kind"), Kind))
    {
        if ((*Object)->HasField(TEXT("id")))
        {
            return ValidateRef(*Object, false, OutMessage);
        }
        if (Kind == TEXT("call"))
        {
            return ValidateCall(*Object, OutMessage);
        }
        if (Kind == TEXT("name"))
        {
            FString Name;
            if (!HasOnly(*Object, {TEXT("kind"), TEXT("name")})
                || !ReadRequiredString(*Object, TEXT("name"), Name)
                || !IsLocalIdentifier(Name))
            {
                OutMessage = TEXT("Name expression requires one valid name.");
                return false;
            }
            return true;
        }
        if (Kind == TEXT("local") || Kind == TEXT("member"))
        {
            return ValidateRef(*Object, false, OutMessage);
        }
    }
    for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : (*Object)->Values)
    {
        if (!IsIdentifier(Pair.Key) || !ValidateExpr(Pair.Value, OutMessage))
        {
            OutMessage = TEXT("Inline object contains an invalid field or value.");
            return false;
        }
    }
    return true;
}

bool ValidateBinding(const TSharedPtr<FJsonObject>& Object, FString& OutAlias, FString& OutTargetKey, FString& OutMessage)
{
    OutAlias.Reset();
    OutTargetKey.Reset();
    const TSharedPtr<FJsonObject>* Target = nullptr;
    const TSharedPtr<FJsonValue> Value = Object->TryGetField(TEXT("value"));
    if (!HasOnly(Object, {TEXT("target"), TEXT("value")})
        || !Object->TryGetObjectField(TEXT("target"), Target)
        || Target == nullptr
        || !(*Target).IsValid()
        || !ValidateRef(*Target, true, OutMessage)
        || !ValidateExpr(Value, OutMessage))
    {
        if (OutMessage.IsEmpty())
        {
            OutMessage = TEXT("Binding requires a binding target and expression value.");
        }
        return false;
    }
    FString Kind;
    (*Target)->TryGetStringField(TEXT("kind"), Kind);
    if (Kind == TEXT("local"))
    {
        (*Target)->TryGetStringField(TEXT("name"), OutAlias);
        OutTargetKey = OutAlias;
        return true;
    }
    const TSharedPtr<FJsonObject>* Owner = nullptr;
    (*Target)->TryGetObjectField(TEXT("object"), Owner);
    FString OwnerName;
    if (Owner != nullptr && (*Owner).IsValid())
    {
        (*Owner)->TryGetStringField(TEXT("name"), OwnerName);
    }
    const TArray<TSharedPtr<FJsonValue>>* Path = nullptr;
    (*Target)->TryGetArrayField(TEXT("path"), Path);
    TArray<FString> Segments;
    if (Path != nullptr)
    {
        for (const TSharedPtr<FJsonValue>& SegmentValue : *Path)
        {
            FString Segment;
            SegmentValue->TryGetString(Segment);
            Segments.Add(Segment);
        }
    }
    OutAlias = OwnerName;
    OutTargetKey = OwnerName + TEXT(".") + FString::Join(Segments, TEXT("."));
    return true;
}

bool ValidateFieldObject(const TSharedPtr<FJsonObject>& Field, FString& OutMessage)
{
    return Field.IsValid()
        && HasOnly(Field, {TEXT("path")})
        && ValidateStringPath(Field, TEXT("path"), OutMessage);
}

bool ValidateCondition(const TSharedPtr<FJsonObject>& Condition, FString& OutMessage)
{
    FString Kind;
    if (!ReadRequiredString(Condition, TEXT("kind"), Kind))
    {
        OutMessage = TEXT("Condition requires kind.");
        return false;
    }
    if (Kind == TEXT("eq") || Kind == TEXT("ne") || Kind == TEXT("contains") || Kind == TEXT("compare"))
    {
        const TSharedPtr<FJsonObject>* Field = nullptr;
        if (!Condition->TryGetObjectField(TEXT("field"), Field)
            || Field == nullptr
            || !ValidateFieldObject(*Field, OutMessage)
            || !ValidateExpr(Condition->TryGetField(TEXT("value")), OutMessage))
        {
            return false;
        }
        if (Kind == TEXT("compare"))
        {
            FString Op;
            if (!Condition->TryGetStringField(TEXT("op"), Op)
                || !(Op == TEXT("gt") || Op == TEXT("gte") || Op == TEXT("lt") || Op == TEXT("lte"))
                || !HasOnly(Condition, {TEXT("kind"), TEXT("op"), TEXT("field"), TEXT("value")}))
            {
                OutMessage = TEXT("Compare condition requires gt, gte, lt, or lte.");
                return false;
            }
        }
        else if (!HasOnly(Condition, {TEXT("kind"), TEXT("field"), TEXT("value")}))
        {
            OutMessage = TEXT("Condition has unsupported fields.");
            return false;
        }
        return true;
    }
    if (Kind == TEXT("not"))
    {
        const TSharedPtr<FJsonObject>* Inner = nullptr;
        if (!HasOnly(Condition, {TEXT("kind"), TEXT("condition")})
            || !Condition->TryGetObjectField(TEXT("condition"), Inner)
            || Inner == nullptr)
        {
            OutMessage = TEXT("Not condition requires condition.");
            return false;
        }
        return ValidateCondition(*Inner, OutMessage);
    }
    if (Kind == TEXT("and") || Kind == TEXT("or"))
    {
        const TArray<TSharedPtr<FJsonValue>>* Conditions = nullptr;
        if (!HasOnly(Condition, {TEXT("kind"), TEXT("conditions")})
            || !Condition->TryGetArrayField(TEXT("conditions"), Conditions)
            || Conditions == nullptr
            || Conditions->Num() < 2)
        {
            OutMessage = TEXT("And/or condition requires at least two conditions.");
            return false;
        }
        for (const TSharedPtr<FJsonValue>& Item : *Conditions)
        {
            const TSharedPtr<FJsonObject>* Inner = nullptr;
            if (!Item.IsValid() || !Item->TryGetObject(Inner) || Inner == nullptr || !ValidateCondition(*Inner, OutMessage))
            {
                return false;
            }
        }
        return true;
    }
    OutMessage = TEXT("Unsupported condition kind.");
    return false;
}

bool ValidateStableRefField(const TSharedPtr<FJsonObject>& Object, const TCHAR* Field, FString& OutMessage)
{
    const TSharedPtr<FJsonObject>* Ref = nullptr;
    return Object->TryGetObjectField(Field, Ref)
        && Ref != nullptr
        && ValidateRef(*Ref, false, OutMessage)
        && (*Ref)->HasField(TEXT("id"));
}

bool ValidateOperation(const TSharedPtr<FJsonObject>& Operation, FString& OutMessage)
{
    FString Kind;
    if (!ReadRequiredString(Operation, TEXT("kind"), Kind))
    {
        OutMessage = TEXT("Query operation requires kind.");
        return false;
    }
    const TSet<FString> Collections = {
        TEXT("assets"), TEXT("variables"), TEXT("dispatchers"), TEXT("graphs"), TEXT("components"),
        TEXT("nodes"), TEXT("properties"), TEXT("functions"), TEXT("defaults"), TEXT("widgets")};
    const TSet<FString> Named = {
        TEXT("variable"), TEXT("dispatcher"), TEXT("graph"), TEXT("component"), TEXT("property"),
        TEXT("function"), TEXT("default"), TEXT("widget")};
    const TSet<FString> IdKinds = {
        TEXT("blueprint"), TEXT("variable"), TEXT("dispatcher"), TEXT("graph"), TEXT("component"),
        TEXT("node"), TEXT("pin"), TEXT("widget"), TEXT("palette")};
    if (Kind == TEXT("summary"))
    {
        return HasOnly(Operation, {TEXT("kind")});
    }
    if (Collections.Contains(Kind))
    {
        FString Text;
        if (!HasOnly(Operation, {TEXT("kind"), TEXT("text")}))
        {
            OutMessage = TEXT("Collection operation has unsupported fields.");
            return false;
        }
        return !Operation->HasField(TEXT("text")) || ReadRequiredString(Operation, TEXT("text"), Text);
    }
    if (Named.Contains(Kind) && Operation->HasField(TEXT("name")))
    {
        FString Name;
        return HasOnly(Operation, {TEXT("kind"), TEXT("name")}) && ReadRequiredString(Operation, TEXT("name"), Name);
    }
    if (IdKinds.Contains(Kind) && Operation->HasField(TEXT("id")))
    {
        FString Id;
        return HasOnly(Operation, {TEXT("kind"), TEXT("id")})
            && ReadRequiredString(Operation, TEXT("id"), Id)
            && IsInlineToken(Id);
    }
    if (Named.Contains(Kind) || IdKinds.Contains(Kind))
    {
        OutMessage = TEXT("Exact operation requires exactly one supported name or id selector.");
        return false;
    }
    if (Kind == TEXT("tree"))
    {
        if (!HasOnly(Operation, {TEXT("kind"), TEXT("root"), TEXT("depth")}))
        {
            OutMessage = TEXT("Tree operation has unsupported fields.");
            return false;
        }
        if (Operation->HasField(TEXT("root")) && !ValidateStableRefField(Operation, TEXT("root"), OutMessage))
        {
            return false;
        }
    }
    else if (Kind == TEXT("context"))
    {
        if (!HasOnly(Operation, {TEXT("kind"), TEXT("target"), TEXT("depth")})
            || !ValidateStableRefField(Operation, TEXT("target"), OutMessage))
        {
            return false;
        }
    }
    else if (Kind == TEXT("exec_flow") || Kind == TEXT("data_flow"))
    {
        FString Direction;
        if (!HasOnly(Operation, {TEXT("kind"), TEXT("direction"), TEXT("target"), TEXT("depth")})
            || !Operation->TryGetStringField(TEXT("direction"), Direction)
            || !(Direction == TEXT("from") || Direction == TEXT("to"))
            || !ValidateStableRefField(Operation, TEXT("target"), OutMessage))
        {
            OutMessage = TEXT("Flow operation requires direction and stable target.");
            return false;
        }
    }
    else if (Kind == TEXT("palette_entries"))
    {
        FString Text;
        if (!HasOnly(Operation, {TEXT("kind"), TEXT("text"), TEXT("pinContext")})
            || (Operation->HasField(TEXT("text")) && !ReadRequiredString(Operation, TEXT("text"), Text)))
        {
            OutMessage = TEXT("Palette entries operation has invalid fields.");
            return false;
        }
        const TSharedPtr<FJsonObject>* PinContext = nullptr;
        if (Operation->HasField(TEXT("pinContext")))
        {
            FString Direction;
            if (!Operation->TryGetObjectField(TEXT("pinContext"), PinContext)
                || PinContext == nullptr
                || !(*PinContext).IsValid()
                || !HasOnly(*PinContext, {TEXT("direction"), TEXT("pin")})
                || !(*PinContext)->TryGetStringField(TEXT("direction"), Direction)
                || !(Direction == TEXT("from") || Direction == TEXT("to"))
                || !ValidateStableRefField(*PinContext, TEXT("pin"), OutMessage))
            {
                OutMessage = TEXT("Palette pin context is invalid.");
                return false;
            }
        }
        return true;
    }
    else
    {
        OutMessage = TEXT("Unsupported Query operation kind.");
        return false;
    }

    double Depth = 0.0;
    if (Operation->HasField(TEXT("depth"))
        && (!Operation->TryGetNumberField(TEXT("depth"), Depth)
            || Depth < 1.0
            || Depth > static_cast<double>(MAX_int32)
            || FMath::FloorToDouble(Depth) != Depth))
    {
        OutMessage = TEXT("Depth must be a positive 32-bit integer.");
        return false;
    }
    return true;
}

bool ValidateTarget(const TSharedPtr<FJsonObject>& Target, FString& OutAlias, TSharedPtr<FJsonObject>& OutValue, FString& OutMessage)
{
    const TSharedPtr<FJsonObject>* Value = nullptr;
    if (!HasOnly(Target, {TEXT("alias"), TEXT("value")})
        || !ReadRequiredString(Target, TEXT("alias"), OutAlias)
        || !IsLocalIdentifier(OutAlias)
        || !Target->TryGetObjectField(TEXT("value"), Value)
        || Value == nullptr
        || !(*Value).IsValid())
    {
        OutMessage = TEXT("Target requires a valid alias and Call or Name value.");
        return false;
    }
    FString Kind;
    (*Value)->TryGetStringField(TEXT("kind"), Kind);
    if (Kind == TEXT("call"))
    {
        if (!ValidateCall(*Value, OutMessage))
        {
            return false;
        }
    }
    else if (Kind == TEXT("name"))
    {
        FString Name;
        if (!HasOnly(*Value, {TEXT("kind"), TEXT("name")})
            || !ReadRequiredString(*Value, TEXT("name"), Name)
            || !IsLocalIdentifier(Name))
        {
            OutMessage = TEXT("Target Name is invalid.");
            return false;
        }
    }
    else
    {
        OutMessage = TEXT("Target value must be a Call or Name.");
        return false;
    }
    OutValue = *Value;
    return true;
}

bool DecodeEnvelope(
    const TSharedPtr<FJsonObject>& Arguments,
    const FString& ExpectedKind,
    TSharedPtr<FJsonObject>& OutObject,
    TSharedPtr<FJsonObject>& OutError)
{
    const TSharedPtr<FJsonObject>* Object = nullptr;
    if (!Arguments.IsValid()
        || !HasOnly(Arguments, {TEXT("object")})
        || !Arguments->TryGetObjectField(TEXT("object"), Object)
        || Object == nullptr
        || !(*Object).IsValid())
    {
        OutError = Invalid(TEXT("SAL RPC requires one object envelope."));
        return false;
    }
    FString Kind;
    if (!(*Object)->TryGetStringField(TEXT("kind"), Kind) || Kind != ExpectedKind)
    {
        OutError = Invalid(
            FString::Printf(TEXT("Expected normalized %s object."), *ExpectedKind),
            {TEXT("object"), TEXT("kind")});
        return false;
    }
    OutObject = *Object;
    return true;
}

bool ValidateOrderAndPage(const TSharedPtr<FJsonObject>& Object, FSalQuery& OutQuery, FString& OutMessage)
{
    const TArray<TSharedPtr<FJsonValue>>* With = nullptr;
    if (Object->HasField(TEXT("with")))
    {
        if (!Object->TryGetArrayField(TEXT("with"), With) || With == nullptr || With->IsEmpty())
        {
            OutMessage = TEXT("with must contain at least one detail.");
            return false;
        }
        TSet<FString> Seen;
        for (const TSharedPtr<FJsonValue>& DetailValue : *With)
        {
            FString Detail;
            if (!DetailValue.IsValid() || !DetailValue->TryGetString(Detail) || !IsIdentifier(Detail) || Seen.Contains(Detail))
            {
                OutMessage = TEXT("with contains an invalid or duplicate detail.");
                return false;
            }
            Seen.Add(Detail);
            OutQuery.With.Add(Detail);
        }
    }
    const TArray<TSharedPtr<FJsonValue>>* Order = nullptr;
    if (Object->HasField(TEXT("orderBy")))
    {
        if (!Object->TryGetArrayField(TEXT("orderBy"), Order) || Order == nullptr || Order->IsEmpty())
        {
            OutMessage = TEXT("orderBy must contain at least one entry.");
            return false;
        }
        for (const TSharedPtr<FJsonValue>& OrderValue : *Order)
        {
            const TSharedPtr<FJsonObject>* Entry = nullptr;
            FString Key;
            FString Direction;
            if (!OrderValue.IsValid()
                || !OrderValue->TryGetObject(Entry)
                || Entry == nullptr
                || !HasOnly(*Entry, {TEXT("key"), TEXT("direction")})
                || !ReadRequiredString(*Entry, TEXT("key"), Key)
                || !IsFieldPath(Key)
                || !(*Entry)->TryGetStringField(TEXT("direction"), Direction)
                || !(Direction == TEXT("asc") || Direction == TEXT("desc")))
            {
                OutMessage = TEXT("orderBy entry is invalid.");
                return false;
            }
            OutQuery.OrderBy.Add(*Entry);
        }
    }
    const TSharedPtr<FJsonObject>* Page = nullptr;
    if (Object->HasField(TEXT("page")))
    {
        if (!Object->TryGetObjectField(TEXT("page"), Page)
            || Page == nullptr
            || !(*Page).IsValid()
            || !HasOnly(*Page, {TEXT("limit"), TEXT("after")})
            || (*Page)->Values.IsEmpty())
        {
            OutMessage = TEXT("page requires limit and/or after.");
            return false;
        }
        double Limit = 0.0;
        if ((*Page)->HasField(TEXT("limit")))
        {
            if (!(*Page)->TryGetNumberField(TEXT("limit"), Limit)
                || Limit < 1.0
                || Limit > static_cast<double>(MAX_int32)
                || FMath::FloorToDouble(Limit) != Limit)
            {
                OutMessage = TEXT("page limit must be a positive 32-bit integer.");
                return false;
            }
            OutQuery.PageLimit = static_cast<int32>(Limit);
        }
        if ((*Page)->HasField(TEXT("after")) && !ReadRequiredString(*Page, TEXT("after"), OutQuery.PageAfter))
        {
            OutMessage = TEXT("page after must be a non-empty cursor.");
            return false;
        }
    }
    return true;
}

bool ValidatePatchOperation(const TSharedPtr<FJsonObject>& Object, FString& OutMessage)
{
    FString Kind;
    if (!ReadRequiredString(Object, TEXT("kind"), Kind))
    {
        OutMessage = TEXT("Patch operation requires kind.");
        return false;
    }
    auto RefField = [&](const TCHAR* Field)
    {
        const TSharedPtr<FJsonObject>* Ref = nullptr;
        return Object->TryGetObjectField(Field, Ref) && Ref != nullptr && ValidateRef(*Ref, false, OutMessage);
    };
    if (Kind == TEXT("compile") || Kind == TEXT("save"))
    {
        return HasOnly(Object, {TEXT("kind")});
    }
    if (Kind == TEXT("remove") || Kind == TEXT("break"))
    {
        return HasOnly(Object, {TEXT("kind"), TEXT("target")}) && RefField(TEXT("target"));
    }
    if (Kind == TEXT("reset"))
    {
        const TSharedPtr<FJsonObject>* Target = nullptr;
        FString TargetKind;
        return HasOnly(Object, {TEXT("kind"), TEXT("target")})
            && Object->TryGetObjectField(TEXT("target"), Target)
            && Target != nullptr
            && ValidateRef(*Target, false, OutMessage)
            && (*Target)->TryGetStringField(TEXT("kind"), TargetKind)
            && TargetKind == TEXT("member");
    }
    if (Kind == TEXT("set"))
    {
        const TSharedPtr<FJsonObject>* Target = nullptr;
        return HasOnly(Object, {TEXT("kind"), TEXT("target"), TEXT("value")})
            && Object->TryGetObjectField(TEXT("target"), Target)
            && Target != nullptr
            && ValidateRef(*Target, false, OutMessage)
            && (*Target)->GetStringField(TEXT("kind")) == TEXT("member")
            && ValidateExpr(Object->TryGetField(TEXT("value")), OutMessage);
    }
    if (Kind == TEXT("connect") || Kind == TEXT("disconnect"))
    {
        return HasOnly(Object, {TEXT("kind"), TEXT("from"), TEXT("to")})
            && RefField(TEXT("from")) && RefField(TEXT("to"));
    }
    if (Kind == TEXT("insert"))
    {
        return HasOnly(Object, {TEXT("kind"), TEXT("from"), TEXT("input"), TEXT("output"), TEXT("to")})
            && RefField(TEXT("from")) && RefField(TEXT("input")) && RefField(TEXT("output")) && RefField(TEXT("to"));
    }
    if (Kind == TEXT("replace"))
    {
        return HasOnly(Object, {TEXT("kind"), TEXT("target"), TEXT("with")})
            && RefField(TEXT("target")) && RefField(TEXT("with"));
    }
    if (Kind == TEXT("wrap"))
    {
        const TArray<TSharedPtr<FJsonValue>>* Targets = nullptr;
        const TSharedPtr<FJsonObject>* Wrapper = nullptr;
        FString WrapperKind;
        if (!HasOnly(Object, {TEXT("kind"), TEXT("targets"), TEXT("with")})
            || !Object->TryGetArrayField(TEXT("targets"), Targets)
            || Targets == nullptr
            || Targets->IsEmpty()
            || !Object->TryGetObjectField(TEXT("with"), Wrapper)
            || Wrapper == nullptr
            || !ValidateRef(*Wrapper, false, OutMessage)
            || !(*Wrapper)->TryGetStringField(TEXT("kind"), WrapperKind)
            || WrapperKind != TEXT("local"))
        {
            return false;
        }
        for (const TSharedPtr<FJsonValue>& TargetValue : *Targets)
        {
            const TSharedPtr<FJsonObject>* Target = nullptr;
            if (!TargetValue.IsValid() || !TargetValue->TryGetObject(Target) || Target == nullptr || !ValidateRef(*Target, false, OutMessage))
            {
                return false;
            }
        }
        return true;
    }
    if (Kind == TEXT("invoke"))
    {
        FString Operation;
        const TSharedPtr<FJsonObject>* Args = nullptr;
        const TArray<TSharedPtr<FJsonValue>>* Outputs = nullptr;
        if (!HasOnly(Object, {TEXT("kind"), TEXT("target"), TEXT("operation"), TEXT("args"), TEXT("outputs")})
            || !RefField(TEXT("target"))
            || !ReadRequiredString(Object, TEXT("operation"), Operation)
            || !IsIdentifier(Operation)
            || !Object->TryGetObjectField(TEXT("args"), Args)
            || Args == nullptr
            || !Object->TryGetArrayField(TEXT("outputs"), Outputs)
            || Outputs == nullptr)
        {
            return false;
        }
        for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : (*Args)->Values)
        {
            if (!IsIdentifier(Pair.Key) || !ValidateExpr(Pair.Value, OutMessage))
            {
                return false;
            }
        }
        for (const TSharedPtr<FJsonValue>& OutputValue : *Outputs)
        {
            const TSharedPtr<FJsonObject>* Output = nullptr;
            FString Alias;
            FString Selector;
            if (!OutputValue.IsValid()
                || !OutputValue->TryGetObject(Output)
                || Output == nullptr
                || !HasOnly(*Output, {TEXT("selector"), TEXT("alias")})
                || !ReadRequiredString(*Output, TEXT("alias"), Alias)
                || !IsLocalIdentifier(Alias)
                || ((*Output)->HasField(TEXT("selector"))
                    && (!ReadRequiredString(*Output, TEXT("selector"), Selector) || !IsFieldPath(Selector))))
            {
                return false;
            }
        }
        return true;
    }
    if (Kind == TEXT("add"))
    {
        const TSharedPtr<FJsonObject>* Target = nullptr;
        if (!Object->TryGetObjectField(TEXT("target"), Target) || Target == nullptr || !ValidateRef(*Target, true, OutMessage))
        {
            return false;
        }
        int32 Placement = 0;
        for (const TCHAR* Field : {TEXT("to"), TEXT("before"), TEXT("after")})
        {
            if (Object->HasField(Field))
            {
                ++Placement;
                if (!RefField(Field))
                {
                    return false;
                }
            }
        }
        return Placement <= 1 && HasOnly(Object, {TEXT("kind"), TEXT("target"), TEXT("to"), TEXT("before"), TEXT("after")});
    }
    if (Kind == TEXT("move"))
    {
        if (!RefField(TEXT("target")))
        {
            return false;
        }
        int32 DestinationCount = 0;
        for (const TCHAR* Field : {TEXT("to"), TEXT("by"), TEXT("before"), TEXT("after")})
        {
            if (Object->HasField(Field))
            {
                ++DestinationCount;
            }
        }
        if (DestinationCount != 1 || !HasOnly(Object, {TEXT("kind"), TEXT("target"), TEXT("to"), TEXT("by"), TEXT("before"), TEXT("after")}))
        {
            return false;
        }
        for (const TCHAR* Field : {TEXT("before"), TEXT("after")})
        {
            if (Object->HasField(Field) && !RefField(Field))
            {
                return false;
            }
        }
        const TSharedPtr<FJsonValue> To = Object->TryGetField(TEXT("to"));
        const TSharedPtr<FJsonValue> By = Object->TryGetField(TEXT("by"));
        const TSharedPtr<FJsonValue> PointValue = To.IsValid() ? To : By;
        if (PointValue.IsValid())
        {
            const TArray<TSharedPtr<FJsonValue>>* Point = nullptr;
            if (PointValue->TryGetArray(Point) && Point != nullptr)
            {
                if (Point->Num() != 2)
                {
                    return false;
                }
                for (const TSharedPtr<FJsonValue>& Coordinate : *Point)
                {
                    double Number = 0.0;
                    if (!Coordinate.IsValid() || !Coordinate->TryGetNumber(Number))
                    {
                        return false;
                    }
                }
            }
            else if (To.IsValid() && !RefField(TEXT("to")))
            {
                return false;
            }
            else if (By.IsValid())
            {
                return false;
            }
        }
        return true;
    }
    OutMessage = TEXT("Unsupported Patch operation kind.");
    return false;
}

bool RefUsesDeclaredAlias(const TSharedPtr<FJsonObject>& Ref, const TSet<FString>& Aliases)
{
    FString Kind;
    if (!Ref.IsValid() || !Ref->TryGetStringField(TEXT("kind"), Kind)) return false;
    if (Ref->HasField(TEXT("id"))) return true;
    if (Kind == TEXT("local"))
    {
        FString Name;
        return Ref->TryGetStringField(TEXT("name"), Name) && Aliases.Contains(Name);
    }
    if (Kind == TEXT("member"))
    {
        const TSharedPtr<FJsonObject>* Owner = nullptr;
        return Ref->TryGetObjectField(TEXT("object"), Owner)
            && Owner != nullptr
            && RefUsesDeclaredAlias(*Owner, Aliases);
    }
    return true;
}

bool ExprUsesDeclaredAliases(const TSharedPtr<FJsonValue>& Value, const TSet<FString>& Aliases)
{
    if (!Value.IsValid() || Value->IsNull()) return true;
    FString String;
    double Number = 0.0;
    bool Boolean = false;
    if (Value->TryGetString(String) || Value->TryGetNumber(Number) || Value->TryGetBool(Boolean)) return true;
    const TArray<TSharedPtr<FJsonValue>>* Array = nullptr;
    if (Value->TryGetArray(Array) && Array != nullptr)
    {
        for (const TSharedPtr<FJsonValue>& Item : *Array)
        {
            if (!ExprUsesDeclaredAliases(Item, Aliases)) return false;
        }
        return true;
    }
    const TSharedPtr<FJsonObject>* Object = nullptr;
    if (!Value->TryGetObject(Object) || Object == nullptr || !(*Object).IsValid()) return false;
    FString Kind;
    (*Object)->TryGetStringField(TEXT("kind"), Kind);
    if ((*Object)->HasField(TEXT("id"))) return true;
    if (Kind == TEXT("local") || Kind == TEXT("member")) return RefUsesDeclaredAlias(*Object, Aliases);
    if (Kind == TEXT("name")) return true;
    if (Kind == TEXT("call"))
    {
        const TSharedPtr<FJsonObject>* Args = nullptr;
        if (!(*Object)->TryGetObjectField(TEXT("args"), Args) || Args == nullptr) return false;
        for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : (*Args)->Values)
        {
            if (!ExprUsesDeclaredAliases(Pair.Value, Aliases)) return false;
        }
        return true;
    }
    for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : (*Object)->Values)
    {
        if (!ExprUsesDeclaredAliases(Pair.Value, Aliases)) return false;
    }
    return true;
}

bool ConditionUsesDeclaredAliases(const TSharedPtr<FJsonObject>& Condition, const TSet<FString>& Aliases)
{
    FString Kind;
    if (!Condition.IsValid() || !Condition->TryGetStringField(TEXT("kind"), Kind)) return false;
    if (Kind == TEXT("not"))
    {
        const TSharedPtr<FJsonObject>* Inner = nullptr;
        return Condition->TryGetObjectField(TEXT("condition"), Inner)
            && Inner != nullptr
            && ConditionUsesDeclaredAliases(*Inner, Aliases);
    }
    if (Kind == TEXT("and") || Kind == TEXT("or"))
    {
        const TArray<TSharedPtr<FJsonValue>>* Conditions = nullptr;
        if (!Condition->TryGetArrayField(TEXT("conditions"), Conditions) || Conditions == nullptr) return false;
        for (const TSharedPtr<FJsonValue>& Value : *Conditions)
        {
            const TSharedPtr<FJsonObject>* Inner = nullptr;
            if (!Value.IsValid()
                || !Value->TryGetObject(Inner)
                || Inner == nullptr
                || !ConditionUsesDeclaredAliases(*Inner, Aliases))
            {
                return false;
            }
        }
        return true;
    }
    return ExprUsesDeclaredAliases(Condition->TryGetField(TEXT("value")), Aliases);
}

bool PatchOperationUsesDeclaredAliases(const TSharedPtr<FJsonObject>& Operation, const TSet<FString>& Aliases)
{
    FString Kind;
    if (!Operation.IsValid() || !Operation->TryGetStringField(TEXT("kind"), Kind)) return false;
    auto SafeRef = [&](const TCHAR* Field)
    {
        const TSharedPtr<FJsonObject>* Ref = nullptr;
        return Operation->TryGetObjectField(Field, Ref)
            && Ref != nullptr
            && RefUsesDeclaredAlias(*Ref, Aliases);
    };
    if (Kind == TEXT("compile") || Kind == TEXT("save")) return true;
    if (Kind == TEXT("remove") || Kind == TEXT("break") || Kind == TEXT("reset")) return SafeRef(TEXT("target"));
    if (Kind == TEXT("set"))
    {
        return SafeRef(TEXT("target")) && ExprUsesDeclaredAliases(Operation->TryGetField(TEXT("value")), Aliases);
    }
    if (Kind == TEXT("connect") || Kind == TEXT("disconnect")) return SafeRef(TEXT("from")) && SafeRef(TEXT("to"));
    if (Kind == TEXT("insert"))
    {
        return SafeRef(TEXT("from")) && SafeRef(TEXT("input")) && SafeRef(TEXT("output")) && SafeRef(TEXT("to"));
    }
    if (Kind == TEXT("replace")) return SafeRef(TEXT("target")) && SafeRef(TEXT("with"));
    if (Kind == TEXT("wrap"))
    {
        const TArray<TSharedPtr<FJsonValue>>* Targets = nullptr;
        if (!SafeRef(TEXT("with")) || !Operation->TryGetArrayField(TEXT("targets"), Targets) || Targets == nullptr) return false;
        for (const TSharedPtr<FJsonValue>& Value : *Targets)
        {
            const TSharedPtr<FJsonObject>* Ref = nullptr;
            if (!Value.IsValid() || !Value->TryGetObject(Ref) || Ref == nullptr || !RefUsesDeclaredAlias(*Ref, Aliases)) return false;
        }
        return true;
    }
    if (Kind == TEXT("add"))
    {
        if (!SafeRef(TEXT("target"))) return false;
        for (const TCHAR* Field : {TEXT("to"), TEXT("before"), TEXT("after")})
        {
            if (Operation->HasField(Field) && !SafeRef(Field)) return false;
        }
        return true;
    }
    if (Kind == TEXT("move"))
    {
        if (!SafeRef(TEXT("target"))) return false;
        for (const TCHAR* Field : {TEXT("before"), TEXT("after")})
        {
            if (Operation->HasField(Field) && !SafeRef(Field)) return false;
        }
        if (Operation->HasField(TEXT("to")))
        {
            const TArray<TSharedPtr<FJsonValue>>* Point = nullptr;
            if (!Operation->TryGetArrayField(TEXT("to"), Point) && !SafeRef(TEXT("to"))) return false;
        }
        return true;
    }
    if (Kind == TEXT("invoke"))
    {
        const TSharedPtr<FJsonObject>* Args = nullptr;
        if (!SafeRef(TEXT("target")) || !Operation->TryGetObjectField(TEXT("args"), Args) || Args == nullptr) return false;
        for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : (*Args)->Values)
        {
            if (!ExprUsesDeclaredAliases(Pair.Value, Aliases)) return false;
        }
        return true;
    }
    return false;
}

bool IsValidCommentText(const FString& Text)
{
    if (Text.IsEmpty()) return false;
    if (Text == TEXT("###")) return true;
    TArray<FString> Lines;
    Text.ParseIntoArrayLines(Lines, false);
    return !Lines.Contains(TEXT("###"));
}

bool ValidateObjectText(const TSharedPtr<FJsonObject>& Object, FString& OutMessage)
{
    const TArray<TSharedPtr<FJsonValue>>* Statements = nullptr;
    if (!HasOnly(Object, {TEXT("statements")})
        || !Object->TryGetArrayField(TEXT("statements"), Statements)
        || Statements == nullptr)
    {
        OutMessage = TEXT("ObjectText requires one statements array.");
        return false;
    }
    TSet<FString> Aliases;
    TSet<FString> Targets;
    for (const TSharedPtr<FJsonValue>& StatementValue : *Statements)
    {
        const TSharedPtr<FJsonObject>* Statement = nullptr;
        if (!StatementValue.IsValid() || !StatementValue->TryGetObject(Statement) || Statement == nullptr)
        {
            OutMessage = TEXT("ObjectText statement must be an object.");
            return false;
        }
        if ((*Statement)->HasField(TEXT("target")) && (*Statement)->HasField(TEXT("value")) && !(*Statement)->HasField(TEXT("kind")))
        {
            FString Alias;
            FString Key;
            if (!ValidateBinding(*Statement, Alias, Key, OutMessage)
                || Targets.Contains(Key)
                || ((*Statement)->GetObjectField(TEXT("target"))->GetStringField(TEXT("kind")) == TEXT("member") && !Aliases.Contains(Alias))
                || !ExprUsesDeclaredAliases((*Statement)->TryGetField(TEXT("value")), Aliases))
            {
                if (OutMessage.IsEmpty()) OutMessage = TEXT("ObjectText binding value references an undeclared local alias.");
                return false;
            }
            Targets.Add(Key);
            const FString TargetKind = (*Statement)->GetObjectField(TEXT("target"))->GetStringField(TEXT("kind"));
            if (TargetKind == TEXT("local"))
            {
                if (Aliases.Contains(Alias))
                {
                    OutMessage = TEXT("ObjectText contains duplicate local alias.");
                    return false;
                }
                Aliases.Add(Alias);
            }
            continue;
        }
        FString Kind;
        (*Statement)->TryGetStringField(TEXT("kind"), Kind);
        if (Kind == TEXT("comment"))
        {
            FString Text;
            if (!HasOnly(*Statement, {TEXT("kind"), TEXT("text")})
                || !ReadRequiredString(*Statement, TEXT("text"), Text)
                || !IsValidCommentText(Text))
            {
                return false;
            }
            continue;
        }
        if ((*Statement)->HasField(TEXT("from")) && (*Statement)->HasField(TEXT("to")) && !(*Statement)->HasField(TEXT("kind")))
        {
            const TSharedPtr<FJsonObject>* From = nullptr;
            const TSharedPtr<FJsonObject>* To = nullptr;
            if (!HasOnly(*Statement, {TEXT("from"), TEXT("to")})
                || !(*Statement)->TryGetObjectField(TEXT("from"), From)
                || !(*Statement)->TryGetObjectField(TEXT("to"), To)
                || From == nullptr || To == nullptr
                || !ValidateRef(*From, false, OutMessage)
                || !ValidateRef(*To, false, OutMessage)
                || !RefUsesDeclaredAlias(*From, Aliases)
                || !RefUsesDeclaredAlias(*To, Aliases))
            {
                if (OutMessage.IsEmpty()) OutMessage = TEXT("ObjectText Edge must follow both local endpoint bindings.");
                return false;
            }
            continue;
        }
        OutMessage = TEXT("ObjectText contains invalid statement.");
        return false;
    }
    return true;
}

bool ValidateSourceSpan(const TSharedPtr<FJsonObject>& Span)
{
    if (!Span.IsValid() || !HasOnly(Span, {TEXT("line"), TEXT("column"), TEXT("length")})) return false;
    double Line = 0.0;
    double Column = 0.0;
    double Length = 0.0;
    if (!Span->TryGetNumberField(TEXT("line"), Line)
        || !Span->TryGetNumberField(TEXT("column"), Column)
        || Line < 1.0
        || Column < 1.0
        || FMath::FloorToDouble(Line) != Line
        || FMath::FloorToDouble(Column) != Column)
    {
        return false;
    }
    return !Span->HasField(TEXT("length"))
        || (Span->TryGetNumberField(TEXT("length"), Length)
            && Length >= 0.0
            && FMath::FloorToDouble(Length) == Length);
}

bool ValidateDiagnostic(const TSharedPtr<FJsonObject>& Diagnostic)
{
    if (!HasOnly(
            Diagnostic,
            {TEXT("severity"), TEXT("code"), TEXT("message"), TEXT("path"), TEXT("span"),
             TEXT("domain"), TEXT("operation"), TEXT("ref"), TEXT("expected"), TEXT("actual"),
             TEXT("supported"), TEXT("matches"), TEXT("suggestion")}))
    {
        return false;
    }
    FString Severity;
    FString Code;
    FString Message;
    if (!Diagnostic->TryGetStringField(TEXT("severity"), Severity)
        || !(Severity == TEXT("error") || Severity == TEXT("warning") || Severity == TEXT("info"))
        || !ReadRequiredString(Diagnostic, TEXT("code"), Code)
        || !ReadRequiredString(Diagnostic, TEXT("message"), Message))
    {
        return false;
    }
    for (const TCHAR* Field : {TEXT("domain"), TEXT("operation"), TEXT("ref"), TEXT("suggestion")})
    {
        FString Value;
        if (Diagnostic->HasField(Field) && !ReadRequiredString(Diagnostic, Field, Value)) return false;
    }
    const TArray<TSharedPtr<FJsonValue>>* Path = nullptr;
    if (Diagnostic->TryGetArrayField(TEXT("path"), Path) && Path != nullptr)
    {
        for (const TSharedPtr<FJsonValue>& Segment : *Path)
        {
            FString Text;
            double Index = 0.0;
            if (!Segment.IsValid()
                || (!Segment->TryGetString(Text)
                    && (!Segment->TryGetNumber(Index) || Index < 0.0 || FMath::FloorToDouble(Index) != Index)))
            {
                return false;
            }
        }
    }
    else if (Diagnostic->HasField(TEXT("path")))
    {
        return false;
    }
    const TSharedPtr<FJsonObject>* Span = nullptr;
    if (Diagnostic->TryGetObjectField(TEXT("span"), Span) && Span != nullptr)
    {
        if (!ValidateSourceSpan(*Span)) return false;
    }
    else if (Diagnostic->HasField(TEXT("span")))
    {
        return false;
    }
    const TArray<TSharedPtr<FJsonValue>>* Matches = nullptr;
    return !Diagnostic->HasField(TEXT("matches"))
        || (Diagnostic->TryGetArrayField(TEXT("matches"), Matches) && Matches != nullptr);
}

bool ValidateResultPage(const TSharedPtr<FJsonObject>& Page)
{
    if (!Page.IsValid() || !HasOnly(Page, {TEXT("next")}) || Page->Values.IsEmpty()) return false;
    FString Next;
    return !Page->HasField(TEXT("next")) || ReadRequiredString(Page, TEXT("next"), Next);
}
}

bool FSalJson::DecodeQuery(
    const TSharedPtr<FJsonObject>& Arguments,
    FSalQuery& OutQuery,
    TSharedPtr<FJsonObject>& OutError)
{
    OutQuery = FSalQuery();
    TSharedPtr<FJsonObject> Object;
    if (!DecodeEnvelope(Arguments, TEXT("query"), Object, OutError))
    {
        return false;
    }
    if (!HasOnly(Object, {TEXT("kind"), TEXT("target"), TEXT("operation"), TEXT("where"), TEXT("with"), TEXT("orderBy"), TEXT("page")}))
    {
        OutError = Invalid(TEXT("Query contains unsupported fields."));
        return false;
    }
    FString Message;
    const TSharedPtr<FJsonObject>* Target = nullptr;
    const TSharedPtr<FJsonObject>* Operation = nullptr;
    if (!Object->TryGetObjectField(TEXT("target"), Target)
        || Target == nullptr
        || !ValidateTarget(*Target, OutQuery.Alias, OutQuery.TargetValue, Message)
        || !Object->TryGetObjectField(TEXT("operation"), Operation)
        || Operation == nullptr
        || !ValidateOperation(*Operation, Message))
    {
        OutError = Invalid(Message.IsEmpty() ? TEXT("Query target or operation is invalid.") : Message);
        return false;
    }
    OutQuery.Operation = *Operation;
    if (!ExprUsesDeclaredAliases(MakeShared<FJsonValueObject>(OutQuery.TargetValue), {}))
    {
        OutError = Invalid(TEXT("Query target contains an unresolved local reference."), {TEXT("object"), TEXT("target")});
        return false;
    }
    const TSharedPtr<FJsonObject>* Where = nullptr;
    if (Object->HasField(TEXT("where")))
    {
        if (!Object->TryGetObjectField(TEXT("where"), Where)
            || Where == nullptr
            || !(*Where).IsValid()
            || !ValidateCondition(*Where, Message))
        {
            OutError = Invalid(
                Message.IsEmpty() ? TEXT("where must be a Condition object.") : Message,
                {TEXT("object"), TEXT("where")});
            return false;
        }
        OutQuery.Where = *Where;
        if (!ConditionUsesDeclaredAliases(OutQuery.Where, {OutQuery.Alias}))
        {
            OutError = Invalid(TEXT("Query condition references an undeclared local alias."), {TEXT("object"), TEXT("where")});
            return false;
        }
    }
    if (!ValidateOrderAndPage(Object, OutQuery, Message))
    {
        OutError = Invalid(Message);
        return false;
    }
    OutQuery.Source = Object;
    return true;
}

bool FSalJson::DecodePatch(
    const TSharedPtr<FJsonObject>& Arguments,
    FSalPatch& OutPatch,
    TSharedPtr<FJsonObject>& OutError)
{
    OutPatch = FSalPatch();
    TSharedPtr<FJsonObject> Object;
    if (!DecodeEnvelope(Arguments, TEXT("patch"), Object, OutError))
    {
        return false;
    }
    if (!HasOnly(Object, {TEXT("kind"), TEXT("target"), TEXT("dryRun"), TEXT("statements")}))
    {
        OutError = Invalid(TEXT("Patch contains unsupported fields."));
        return false;
    }
    FString Message;
    const TSharedPtr<FJsonObject>* Target = nullptr;
    if (!Object->TryGetObjectField(TEXT("target"), Target)
        || Target == nullptr
        || !ValidateTarget(*Target, OutPatch.Alias, OutPatch.TargetValue, Message)
        || !OutPatch.TargetValue.IsValid()
        || OutPatch.TargetValue->GetStringField(TEXT("kind")) != TEXT("call"))
    {
        OutError = Invalid(Message.IsEmpty() ? TEXT("Patch requires a bound Call target.") : Message);
        return false;
    }
    if (!ExprUsesDeclaredAliases(MakeShared<FJsonValueObject>(OutPatch.TargetValue), {}))
    {
        OutError = Invalid(TEXT("Patch target contains an unresolved local reference."), {TEXT("object"), TEXT("target")});
        return false;
    }
    if (!Object->TryGetBoolField(TEXT("dryRun"), OutPatch.bDryRun))
    {
        OutError = Invalid(TEXT("Patch requires boolean dryRun."));
        return false;
    }
    const TArray<TSharedPtr<FJsonValue>>* Statements = nullptr;
    if (!Object->TryGetArrayField(TEXT("statements"), Statements) || Statements == nullptr || Statements->IsEmpty())
    {
        OutError = Invalid(TEXT("Patch requires at least one statement."));
        return false;
    }
    TSet<FString> Aliases = {OutPatch.Alias};
    TSet<FString> BindingTargets;
    for (int32 Index = 0; Index < Statements->Num(); ++Index)
    {
        const TSharedPtr<FJsonValue>& StatementValue = (*Statements)[Index];
        const TSharedPtr<FJsonObject>* Statement = nullptr;
        if (!StatementValue.IsValid() || !StatementValue->TryGetObject(Statement) || Statement == nullptr)
        {
            OutError = Invalid(TEXT("Patch statement must be an object."), {TEXT("object"), TEXT("statements"), FString::FromInt(Index)});
            return false;
        }
        FString Kind;
        (*Statement)->TryGetStringField(TEXT("kind"), Kind);
        if (Kind.IsEmpty())
        {
            FString Alias;
            FString Key;
            if (!ValidateBinding(*Statement, Alias, Key, Message)
                || BindingTargets.Contains(Key)
                || !ExprUsesDeclaredAliases((*Statement)->TryGetField(TEXT("value")), Aliases))
            {
                OutError = Invalid(Message.IsEmpty() ? TEXT("Invalid, duplicate, or reference-unsafe Patch binding.") : Message);
                return false;
            }
            const FString TargetKind = (*Statement)->GetObjectField(TEXT("target"))->GetStringField(TEXT("kind"));
            if (TargetKind == TEXT("local"))
            {
                if (Aliases.Contains(Alias))
                {
                    OutError = Invalid(TEXT("Patch contains duplicate local alias."));
                    return false;
                }
                Aliases.Add(Alias);
            }
            else if (!Aliases.Contains(Alias))
            {
                OutError = Invalid(TEXT("Patch member binding owner is not declared."));
                return false;
            }
            BindingTargets.Add(Key);
        }
        else if (!ValidatePatchOperation(*Statement, Message)
            || !PatchOperationUsesDeclaredAliases(*Statement, Aliases))
        {
            OutError = Invalid(Message.IsEmpty() ? TEXT("Invalid or reference-unsafe Patch operation.") : Message);
            return false;
        }
        if (Kind == TEXT("invoke"))
        {
            const TArray<TSharedPtr<FJsonValue>>* Outputs = nullptr;
            (*Statement)->TryGetArrayField(TEXT("outputs"), Outputs);
            if (Outputs != nullptr)
            {
                for (const TSharedPtr<FJsonValue>& OutputValue : *Outputs)
                {
                    const TSharedPtr<FJsonObject>* Output = nullptr;
                    FString OutputAlias;
                    if (!OutputValue.IsValid()
                        || !OutputValue->TryGetObject(Output)
                        || Output == nullptr
                        || !(*Output)->TryGetStringField(TEXT("alias"), OutputAlias)
                        || Aliases.Contains(OutputAlias))
                    {
                        OutError = Invalid(TEXT("Invoke output aliases must be unique and declared in statement order."));
                        return false;
                    }
                    Aliases.Add(OutputAlias);
                }
            }
        }
        OutPatch.Statements.Add(StatementValue);
    }
    OutPatch.Source = Object;
    return true;
}

bool FSalJson::ValidateResult(
    const TSharedPtr<FJsonObject>& Result,
    TSharedPtr<FJsonObject>& OutError)
{
    if (!Result.IsValid())
    {
        OutError = InvalidResult(TEXT("SAL executor returned no result."));
        return false;
    }
    const bool bMutation = Result->HasField(TEXT("isError"));
    if (bMutation)
    {
        if (!HasOnly(
                Result,
                {TEXT("object"), TEXT("diagnostics"), TEXT("page"), TEXT("isError"), TEXT("dryRun"),
                 TEXT("valid"), TEXT("applied"), TEXT("assetPath"), TEXT("operation"), TEXT("resolvedRefs"),
                 TEXT("planned"), TEXT("diff"), TEXT("previousRevision"), TEXT("newRevision")}))
        {
            OutError = InvalidResult(TEXT("MutationResult contains unsupported fields."));
            return false;
        }
    }
    else if (!HasOnly(Result, {TEXT("object"), TEXT("diagnostics"), TEXT("page")}))
    {
        OutError = InvalidResult(TEXT("SAL Result contains unsupported fields."));
        return false;
    }

    const TArray<TSharedPtr<FJsonValue>>* Diagnostics = nullptr;
    if (!Result->TryGetArrayField(TEXT("diagnostics"), Diagnostics) || Diagnostics == nullptr)
    {
        OutError = InvalidResult(TEXT("SAL result requires diagnostics."));
        return false;
    }
    for (const TSharedPtr<FJsonValue>& DiagnosticValue : *Diagnostics)
    {
        const TSharedPtr<FJsonObject>* Diagnostic = nullptr;
        if (!DiagnosticValue.IsValid()
            || !DiagnosticValue->TryGetObject(Diagnostic)
            || Diagnostic == nullptr
            || !ValidateDiagnostic(*Diagnostic))
        {
            OutError = InvalidResult(TEXT("SAL result contains an invalid diagnostic."));
            return false;
        }
    }
    const TSharedPtr<FJsonObject>* Object = nullptr;
    FString Message;
    if (Result->TryGetObjectField(TEXT("object"), Object) && Object != nullptr)
    {
        if (!ValidateObjectText(*Object, Message))
        {
            OutError = InvalidResult(Message.IsEmpty() ? TEXT("SAL result ObjectText is invalid.") : Message);
            return false;
        }
    }
    else if (Result->HasField(TEXT("object")))
    {
        OutError = InvalidResult(TEXT("SAL result object must be ObjectText."));
        return false;
    }
    const TSharedPtr<FJsonObject>* Page = nullptr;
    if (Result->TryGetObjectField(TEXT("page"), Page) && Page != nullptr)
    {
        if (!ValidateResultPage(*Page))
        {
            OutError = InvalidResult(TEXT("SAL result page is invalid."));
            return false;
        }
    }
    else if (Result->HasField(TEXT("page")))
    {
        OutError = InvalidResult(TEXT("SAL result page must be an object."));
        return false;
    }
    if (bMutation)
    {
        bool bIsError = false;
        bool bDryRun = false;
        bool bValid = false;
        bool bApplied = false;
        FString Operation;
        if (!Result->TryGetBoolField(TEXT("isError"), bIsError)
            || !Result->TryGetBoolField(TEXT("dryRun"), bDryRun)
            || !Result->TryGetBoolField(TEXT("valid"), bValid)
            || !Result->TryGetBoolField(TEXT("applied"), bApplied)
            || !ReadRequiredString(Result, TEXT("operation"), Operation))
        {
            OutError = InvalidResult(TEXT("MutationResult is missing required execution fields."));
            return false;
        }
        if (bDryRun && bApplied)
        {
            OutError = InvalidResult(TEXT("MutationResult cannot report applied=true for a dry run."));
            return false;
        }
        for (const TCHAR* Field : {TEXT("assetPath"), TEXT("previousRevision"), TEXT("newRevision")})
        {
            FString Value;
            if (Result->HasField(Field) && !ReadRequiredString(Result, Field, Value))
            {
                OutError = InvalidResult(FString::Printf(TEXT("MutationResult %s must be a non-empty string."), Field));
                return false;
            }
        }
    }
    return true;
}
}
