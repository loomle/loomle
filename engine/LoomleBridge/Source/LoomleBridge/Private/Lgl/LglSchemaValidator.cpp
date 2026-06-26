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
        TEXT("invalid_object"),
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
                TEXT("invalid_result"),
                TEXT("LGL result contains an invalid diagnostic object.")));
            return false;
        }
    }
    return true;
}
}
