// Copyright 2026 Loomle contributors.

#include "LglSchemaValidator.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "LglDiagnostics.h"
#include "LglResult.h"

namespace Loomle::Lgl
{
namespace
{
FLglObjectResult InvalidObject(const FString& Message, const FString& Suggestion = FString())
{
    return FLglResult::FromDiagnostic(FLglDiagnostics::Make(
        TEXT("error"),
        TEXT("language.invalid_object_shape"),
        Message,
        Suggestion));
}

bool RequireStringField(
    const TSharedPtr<FJsonObject>& Object,
    const TCHAR* Field,
    const FString& Message,
    FLglObjectResult& OutError)
{
    FString Value;
    if (!Object.IsValid() || !Object->TryGetStringField(Field, Value) || Value.IsEmpty())
    {
        OutError = InvalidObject(Message);
        return false;
    }
    return true;
}

bool ValidateGraphRef(const TSharedPtr<FJsonObject>& Graph, FLglObjectResult& OutError)
{
    FString Kind;
    if (!Graph.IsValid() || !Graph->TryGetStringField(TEXT("kind"), Kind) || Kind.IsEmpty())
    {
        OutError = InvalidObject(TEXT("LGL graph reference is missing required string field kind."));
        return false;
    }

    if (Kind.Equals(TEXT("name")))
    {
        return RequireStringField(Graph, TEXT("name"), TEXT("LGL graph name reference is missing required string field name."), OutError);
    }
    if (Kind.Equals(TEXT("id")))
    {
        return RequireStringField(Graph, TEXT("id"), TEXT("LGL graph id reference is missing required string field id."), OutError);
    }

    OutError = InvalidObject(
        FString::Printf(TEXT("Unsupported LGL graph reference kind %s."), *Kind),
        TEXT("Use { \"kind\": \"name\", \"name\": ... } or { \"kind\": \"id\", \"id\": ... }."));
    return false;
}

bool ValidateFind(const TSharedPtr<FJsonObject>& Object, FLglObjectResult& OutError)
{
    const TSharedPtr<FJsonValue> FindValue = Object->TryGetField(TEXT("find"));
    if (!FindValue.IsValid() || FindValue->IsNull())
    {
        return true;
    }

    const TSharedPtr<FJsonObject>* FindPtr = nullptr;
    if (!FindValue->TryGetObject(FindPtr) || FindPtr == nullptr || !(*FindPtr).IsValid())
    {
        OutError = InvalidObject(TEXT("LGL query find field must be an object when present."));
        return false;
    }

    return RequireStringField(*FindPtr, TEXT("kind"), TEXT("LGL query find object is missing required string field kind."), OutError);
}

bool ValidateDomainFind(
    const FString& Domain,
    const TSharedPtr<FJsonObject>& Object,
    FLglObjectResult& OutError)
{
    const TSharedPtr<FJsonObject>* FindPtr = nullptr;
    if (!Object->TryGetObjectField(TEXT("find"), FindPtr) || FindPtr == nullptr || !(*FindPtr).IsValid())
    {
        return true;
    }

    FString FindKind;
    (*FindPtr)->TryGetStringField(TEXT("kind"), FindKind);
    if (Domain.Equals(TEXT("asset")) && !FindKind.Equals(TEXT("assets")))
    {
        OutError = InvalidObject(
            FString::Printf(TEXT("Asset queries do not support find kind %s."), *FindKind),
            TEXT("Use find.kind = \"assets\" for asset queries."));
        return false;
    }
    return true;
}

bool ValidateStringArrayField(
    const TSharedPtr<FJsonObject>& Object,
    const TCHAR* Field,
    const FString& Message,
    FLglObjectResult& OutError)
{
    const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
    if (!Object->TryGetArrayField(Field, Values))
    {
        return true;
    }
    for (const TSharedPtr<FJsonValue>& Value : *Values)
    {
        FString Item;
        if (!Value.IsValid() || !Value->TryGetString(Item) || Item.IsEmpty())
        {
            OutError = InvalidObject(Message);
            return false;
        }
    }
    return true;
}

bool ValidateFieldPath(
    const TSharedPtr<FJsonObject>& Condition,
    const FString& ConditionKind,
    FLglObjectResult& OutError)
{
    const TSharedPtr<FJsonObject>* Field = nullptr;
    if (!Condition->TryGetObjectField(TEXT("field"), Field) || Field == nullptr || !(*Field).IsValid())
    {
        OutError = InvalidObject(FString::Printf(TEXT("LGL %s condition is missing field object."), *ConditionKind));
        return false;
    }

    const TArray<TSharedPtr<FJsonValue>>* Path = nullptr;
    if (!(*Field)->TryGetArrayField(TEXT("path"), Path) || Path == nullptr || Path->Num() == 0)
    {
        OutError = InvalidObject(FString::Printf(TEXT("LGL %s condition field.path must be a non-empty array."), *ConditionKind));
        return false;
    }

    for (const TSharedPtr<FJsonValue>& Segment : *Path)
    {
        FString SegmentText;
        if (!Segment.IsValid() || !Segment->TryGetString(SegmentText) || SegmentText.IsEmpty())
        {
            OutError = InvalidObject(FString::Printf(TEXT("LGL %s condition field.path must contain only non-empty strings."), *ConditionKind));
            return false;
        }
    }
    return true;
}

bool ValidateWhereCondition(const TSharedPtr<FJsonObject>& Condition, FLglObjectResult& OutError)
{
    if (!Condition.IsValid())
    {
        OutError = InvalidObject(TEXT("LGL where condition must be an object."));
        return false;
    }

    FString Kind;
    if (!Condition->TryGetStringField(TEXT("kind"), Kind) || Kind.IsEmpty())
    {
        OutError = InvalidObject(TEXT("LGL where condition is missing required string field kind."));
        return false;
    }

    if (Kind == TEXT("eq") || Kind == TEXT("ne") || Kind == TEXT("contains") || Kind == TEXT("compare"))
    {
        if (!ValidateFieldPath(Condition, Kind, OutError))
        {
            return false;
        }
        if (!Condition->HasField(TEXT("value")))
        {
            OutError = InvalidObject(FString::Printf(TEXT("LGL %s condition is missing value."), *Kind));
            return false;
        }
        if (Kind == TEXT("compare"))
        {
            FString Op;
            if (!Condition->TryGetStringField(TEXT("op"), Op)
                || !(Op == TEXT("gt") || Op == TEXT("gte") || Op == TEXT("lt") || Op == TEXT("lte")))
            {
                OutError = InvalidObject(TEXT("LGL compare condition op must be gt, gte, lt, or lte."));
                return false;
            }
        }
        return true;
    }

    if (Kind == TEXT("not"))
    {
        const TSharedPtr<FJsonObject>* Inner = nullptr;
        if (!Condition->TryGetObjectField(TEXT("condition"), Inner) || Inner == nullptr || !(*Inner).IsValid())
        {
            OutError = InvalidObject(TEXT("LGL not condition is missing condition object."));
            return false;
        }
        return ValidateWhereCondition(*Inner, OutError);
    }

    if (Kind == TEXT("and") || Kind == TEXT("or"))
    {
        const TArray<TSharedPtr<FJsonValue>>* Conditions = nullptr;
        if (!Condition->TryGetArrayField(TEXT("conditions"), Conditions) || Conditions == nullptr || Conditions->Num() == 0)
        {
            OutError = InvalidObject(FString::Printf(TEXT("LGL %s condition requires a non-empty conditions array."), *Kind));
            return false;
        }

        for (const TSharedPtr<FJsonValue>& Item : *Conditions)
        {
            const TSharedPtr<FJsonObject>* ItemObject = nullptr;
            if (!Item.IsValid() || !Item->TryGetObject(ItemObject) || ItemObject == nullptr || !(*ItemObject).IsValid())
            {
                OutError = InvalidObject(TEXT("LGL compound where condition contains a non-object item."));
                return false;
            }
            if (!ValidateWhereCondition(*ItemObject, OutError))
            {
                return false;
            }
        }
        return true;
    }

    OutError = InvalidObject(
        FString::Printf(TEXT("Unsupported LGL where condition kind %s."), *Kind),
        TEXT("Use eq, ne, contains, compare, not, and, or."));
    return false;
}

bool ValidateWhere(const TSharedPtr<FJsonObject>& Object, FLglObjectResult& OutError)
{
    const TSharedPtr<FJsonValue> WhereValue = Object->TryGetField(TEXT("where"));
    if (!WhereValue.IsValid() || WhereValue->IsNull())
    {
        return true;
    }

    const TSharedPtr<FJsonObject>* Where = nullptr;
    if (!WhereValue->TryGetObject(Where) || Where == nullptr || !(*Where).IsValid())
    {
        OutError = InvalidObject(TEXT("LGL query where field must be a condition object."));
        return false;
    }

    return ValidateWhereCondition(*Where, OutError);
}

bool ValidateOrderBy(const TSharedPtr<FJsonObject>& Object, FLglObjectResult& OutError)
{
    const TArray<TSharedPtr<FJsonValue>>* Orders = nullptr;
    if (!Object->TryGetArrayField(TEXT("orderBy"), Orders))
    {
        return true;
    }

    for (const TSharedPtr<FJsonValue>& OrderValue : *Orders)
    {
        const TSharedPtr<FJsonObject>* Order = nullptr;
        if (!OrderValue.IsValid() || !OrderValue->TryGetObject(Order) || Order == nullptr || !(*Order).IsValid())
        {
            OutError = InvalidObject(TEXT("LGL query orderBy field must contain only order objects."));
            return false;
        }

        FString Key;
        if (!(*Order)->TryGetStringField(TEXT("key"), Key) || Key.IsEmpty())
        {
            OutError = InvalidObject(TEXT("LGL query orderBy item is missing required string field key."));
            return false;
        }

        FString Direction;
        if (!(*Order)->TryGetStringField(TEXT("direction"), Direction)
            || !(Direction == TEXT("asc") || Direction == TEXT("desc")))
        {
            OutError = InvalidObject(TEXT("LGL query orderBy item direction must be asc or desc."));
            return false;
        }
    }
    return true;
}

bool ValidatePage(const TSharedPtr<FJsonObject>& Object, FLglObjectResult& OutError)
{
    const TSharedPtr<FJsonValue> PageValue = Object->TryGetField(TEXT("page"));
    if (!PageValue.IsValid() || PageValue->IsNull())
    {
        return true;
    }

    const TSharedPtr<FJsonObject>* Page = nullptr;
    if (!PageValue->TryGetObject(Page) || Page == nullptr || !(*Page).IsValid())
    {
        OutError = InvalidObject(TEXT("LGL query page field must be an object."));
        return false;
    }

    if ((*Page)->HasField(TEXT("limit")))
    {
        double Limit = 0.0;
        if (!(*Page)->TryGetNumberField(TEXT("limit"), Limit) || Limit <= 0.0)
        {
            OutError = InvalidObject(TEXT("LGL query page.limit must be a positive number."));
            return false;
        }
    }

    if ((*Page)->HasField(TEXT("after")))
    {
        FString After;
        if (!(*Page)->TryGetStringField(TEXT("after"), After) || After.IsEmpty())
        {
            OutError = InvalidObject(TEXT("LGL query page.after must be a non-empty string."));
            return false;
        }
    }

    return true;
}
}

bool FLglSchemaValidator::ValidateRequest(
    const FLglObjectRequest& Request,
    const FString& ExpectedKind,
    FLglObjectResult& OutError)
{
    if (!Request.Object.IsValid())
    {
        OutError = InvalidObject(TEXT("LGL request is missing object."));
        return false;
    }

    FString Kind;
    if (!Request.Object->TryGetStringField(TEXT("kind"), Kind) || Kind.IsEmpty())
    {
        OutError = InvalidObject(TEXT("LGL object is missing required string field kind."));
        return false;
    }
    if (!Kind.Equals(ExpectedKind))
    {
        OutError = InvalidObject(
            FString::Printf(TEXT("%s expects object.kind = %s, got %s."), *Request.Method, *ExpectedKind, *Kind),
            ExpectedKind.Equals(TEXT("query"))
                ? TEXT("Use lgl.patch for patch objects once that RPC exists.")
                : TEXT("Use lgl.query for query objects."));
        return false;
    }

    const TSharedPtr<FJsonObject>* TargetPtr = nullptr;
    if (!Request.Object->TryGetObjectField(TEXT("target"), TargetPtr) || TargetPtr == nullptr || !(*TargetPtr).IsValid())
    {
        OutError = InvalidObject(TEXT("LGL query object is missing required target object."));
        return false;
    }

    FString Domain;
    if (!(*TargetPtr)->TryGetStringField(TEXT("domain"), Domain) || Domain.IsEmpty())
    {
        OutError = InvalidObject(TEXT("LGL query target is missing required string field domain."));
        return false;
    }

    FString Asset;
    if ((*TargetPtr)->TryGetStringField(TEXT("asset"), Asset) && Asset.IsEmpty())
    {
        OutError = InvalidObject(TEXT("LGL query target asset field must not be empty when present."));
        return false;
    }

    const TSharedPtr<FJsonValue> GraphValue = (*TargetPtr)->TryGetField(TEXT("graph"));
    if (GraphValue.IsValid() && !GraphValue->IsNull())
    {
        const TSharedPtr<FJsonObject>* GraphPtr = nullptr;
        if (!GraphValue->TryGetObject(GraphPtr) || GraphPtr == nullptr || !(*GraphPtr).IsValid())
        {
            OutError = InvalidObject(TEXT("LGL query target graph field must be an object when present."));
            return false;
        }
        if (!ValidateGraphRef(*GraphPtr, OutError))
        {
            return false;
        }
    }

    if (!ValidateFind(Request.Object, OutError))
    {
        return false;
    }

    if (!ValidateDomainFind(Domain, Request.Object, OutError))
    {
        return false;
    }

    if (!ValidateStringArrayField(Request.Object, TEXT("with"), TEXT("LGL query with field must contain only non-empty strings."), OutError))
    {
        return false;
    }

    if (!ValidateWhere(Request.Object, OutError))
    {
        return false;
    }

    if (!ValidateOrderBy(Request.Object, OutError))
    {
        return false;
    }

    if (!ValidatePage(Request.Object, OutError))
    {
        return false;
    }

    return true;
}

bool FLglSchemaValidator::ValidateResult(
    const FLglObjectResult& Result,
    FLglObjectResult& OutError)
{
    for (const TSharedPtr<FJsonObject>& Diagnostic : Result.Diagnostics)
    {
        if (!Diagnostic.IsValid())
        {
            OutError = FLglResult::FromDiagnostic(FLglDiagnostics::Make(
                TEXT("error"),
                TEXT("language.invalid_result_shape"),
                TEXT("LGL result contains an invalid diagnostic object.")));
            return false;
        }
    }
    return true;
}
}
