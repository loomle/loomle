// Copyright 2026 Loomle contributors.

#include "LglAssetAdapter.h"

#include "../LglDiagnostics.h"
#include "../LglResult.h"
#include "../Services/LglAssetRegistry.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

namespace Loomle::Lgl
{
namespace
{
constexpr int32 DefaultLimit = 50;

FLglObjectResult UnsupportedQueryFeature(const FString& Feature, const FString& Suggestion)
{
    return FLglResult::FromDiagnostic(FLglDiagnostics::Make(
        TEXT("error"),
        TEXT("unsupported_query_feature"),
        FString::Printf(TEXT("The asset adapter does not support %s yet."), *Feature),
        Suggestion));
}

bool HasField(const TSharedPtr<FJsonObject>& Object, const FString& Field)
{
    return Object.IsValid() && Object->HasField(Field);
}

bool IsSupportedWhereField(const FString& Field)
{
    return Field == TEXT("root")
        || Field == TEXT("type")
        || Field == TEXT("class")
        || Field == TEXT("name")
        || Field == TEXT("path");
}

bool ReadSingleFieldPath(
    const TSharedPtr<FJsonObject>& Condition,
    FString& OutField,
    FLglObjectResult& OutError)
{
    const TSharedPtr<FJsonObject>* Field = nullptr;
    if (!Condition->TryGetObjectField(TEXT("field"), Field) || Field == nullptr || !(*Field).IsValid())
    {
        OutError = FLglResult::FromDiagnostic(FLglDiagnostics::Make(
            TEXT("error"),
            TEXT("invalid_where"),
            TEXT("Asset where condition is missing field.path.")));
        return false;
    }

    const TArray<TSharedPtr<FJsonValue>>* Path = nullptr;
    if (!(*Field)->TryGetArrayField(TEXT("path"), Path) || Path == nullptr || Path->Num() != 1)
    {
        OutError = FLglResult::FromDiagnostic(FLglDiagnostics::Make(
            TEXT("error"),
            TEXT("unsupported_where_field"),
            TEXT("Asset where currently supports only single-segment fields."),
            TEXT("Use root, type, class, name, or path.")));
        return false;
    }

    if (!(*Path)[0].IsValid() || !(*Path)[0]->TryGetString(OutField) || OutField.IsEmpty())
    {
        OutError = FLglResult::FromDiagnostic(FLglDiagnostics::Make(
            TEXT("error"),
            TEXT("invalid_where"),
            TEXT("Asset where field.path must contain a non-empty string.")));
        return false;
    }

    if (!IsSupportedWhereField(OutField))
    {
        OutError = FLglResult::FromDiagnostic(FLglDiagnostics::Make(
            TEXT("error"),
            TEXT("unsupported_where_field"),
            FString::Printf(TEXT("Asset where does not support field %s yet."), *OutField),
            TEXT("Use root, type, class, name, or path.")));
        return false;
    }

    return true;
}

bool ValidateWhereCondition(const TSharedPtr<FJsonObject>& Condition, FLglObjectResult& OutError)
{
    if (!Condition.IsValid())
    {
        OutError = FLglResult::FromDiagnostic(FLglDiagnostics::Make(
            TEXT("error"),
            TEXT("invalid_where"),
            TEXT("Asset where condition must be an object.")));
        return false;
    }

    FString Kind;
    if (!Condition->TryGetStringField(TEXT("kind"), Kind) || Kind.IsEmpty())
    {
        OutError = FLglResult::FromDiagnostic(FLglDiagnostics::Make(
            TEXT("error"),
            TEXT("invalid_where"),
            TEXT("Asset where condition is missing kind.")));
        return false;
    }

    if (Kind == TEXT("eq") || Kind == TEXT("ne") || Kind == TEXT("contains"))
    {
        FString Field;
        if (!ReadSingleFieldPath(Condition, Field, OutError))
        {
            return false;
        }
        if (!Condition->HasField(TEXT("value")))
        {
            OutError = FLglResult::FromDiagnostic(FLglDiagnostics::Make(
                TEXT("error"),
                TEXT("invalid_where"),
                TEXT("Asset where comparison is missing value.")));
            return false;
        }
        return true;
    }

    if (Kind == TEXT("not"))
    {
        const TSharedPtr<FJsonObject>* Inner = nullptr;
        if (!Condition->TryGetObjectField(TEXT("condition"), Inner) || Inner == nullptr)
        {
            OutError = FLglResult::FromDiagnostic(FLglDiagnostics::Make(
                TEXT("error"),
                TEXT("invalid_where"),
                TEXT("Asset not condition is missing condition.")));
            return false;
        }
        return ValidateWhereCondition(*Inner, OutError);
    }

    if (Kind == TEXT("and") || Kind == TEXT("or"))
    {
        const TArray<TSharedPtr<FJsonValue>>* Conditions = nullptr;
        if (!Condition->TryGetArrayField(TEXT("conditions"), Conditions) || Conditions == nullptr || Conditions->Num() == 0)
        {
            OutError = FLglResult::FromDiagnostic(FLglDiagnostics::Make(
                TEXT("error"),
                TEXT("invalid_where"),
                FString::Printf(TEXT("Asset %s condition requires a non-empty conditions array."), *Kind)));
            return false;
        }

        for (const TSharedPtr<FJsonValue>& Item : *Conditions)
        {
            const TSharedPtr<FJsonObject>* ItemObject = nullptr;
            if (!Item.IsValid() || !Item->TryGetObject(ItemObject) || ItemObject == nullptr)
            {
                OutError = FLglResult::FromDiagnostic(FLglDiagnostics::Make(
                    TEXT("error"),
                    TEXT("invalid_where"),
                    TEXT("Asset compound where condition contains a non-object item.")));
                return false;
            }
            if (!ValidateWhereCondition(*ItemObject, OutError))
            {
                return false;
            }
        }
        return true;
    }

    if (Kind == TEXT("compare"))
    {
        OutError = UnsupportedQueryFeature(
            TEXT("where compare"),
            TEXT("Use =, !=, or ~= with root, type, class, name, or path for now."));
        return false;
    }

    OutError = FLglResult::FromDiagnostic(FLglDiagnostics::Make(
        TEXT("error"),
        TEXT("unsupported_where_condition"),
        FString::Printf(TEXT("Asset where does not support condition kind %s."), *Kind),
        TEXT("Use eq, ne, contains, not, and, or.")));
    return false;
}

bool ReadWhere(const TSharedPtr<FJsonObject>& Object, TSharedPtr<FJsonObject>& OutWhere, FLglObjectResult& OutError)
{
    OutWhere.Reset();

    const TSharedPtr<FJsonValue> WhereValue = Object->TryGetField(TEXT("where"));
    if (!WhereValue.IsValid() || WhereValue->IsNull())
    {
        return true;
    }

    const TSharedPtr<FJsonObject>* Where = nullptr;
    if (!WhereValue->TryGetObject(Where) || Where == nullptr || !(*Where).IsValid())
    {
        OutError = FLglResult::FromDiagnostic(FLglDiagnostics::Make(
            TEXT("error"),
            TEXT("invalid_where"),
            TEXT("Asset where must be a condition object.")));
        return false;
    }

    if (!ValidateWhereCondition(*Where, OutError))
    {
        return false;
    }

    OutWhere = *Where;
    return true;
}

bool ReadFindText(const TSharedPtr<FJsonObject>& Object, FString& OutText, FLglObjectResult& OutError)
{
    OutText.Reset();

    const TSharedPtr<FJsonObject>* Find = nullptr;
    if (!Object->TryGetObjectField(TEXT("find"), Find) || Find == nullptr || !(*Find).IsValid())
    {
        return true;
    }

    FString Kind;
    if (!(*Find)->TryGetStringField(TEXT("kind"), Kind) || Kind != TEXT("assets"))
    {
        OutError = FLglResult::FromDiagnostic(FLglDiagnostics::Make(
            TEXT("error"),
            TEXT("unsupported_query"),
            TEXT("The asset adapter only supports find assets in this milestone."),
            TEXT("Use query asset with find.kind = \"assets\".")));
        return false;
    }

    (*Find)->TryGetStringField(TEXT("text"), OutText);
    return true;
}

bool ReadPageLimit(const TSharedPtr<FJsonObject>& Object, int32& OutLimit, FLglObjectResult& OutError)
{
    OutLimit = DefaultLimit;

    const TSharedPtr<FJsonObject>* Page = nullptr;
    if (!Object->TryGetObjectField(TEXT("page"), Page) || Page == nullptr || !(*Page).IsValid())
    {
        return true;
    }

    if ((*Page)->HasField(TEXT("after")))
    {
        OutError = UnsupportedQueryFeature(
            TEXT("page.after"),
            TEXT("Use page.limit only for now; cursor-based pagination is the next asset-query step."));
        return false;
    }

    double Limit = 0.0;
    if ((*Page)->TryGetNumberField(TEXT("limit"), Limit))
    {
        OutLimit = static_cast<int32>(Limit);
    }

    return true;
}

TArray<TSharedPtr<FJsonValue>> EncodeObjectArray(const TArray<TSharedPtr<FJsonObject>>& Objects)
{
    TArray<TSharedPtr<FJsonValue>> Values;
    for (const TSharedPtr<FJsonObject>& Object : Objects)
    {
        if (Object.IsValid())
        {
            Values.Add(MakeShared<FJsonValueObject>(Object));
        }
    }
    return Values;
}
}

FLglAssetAdapter::FLglAssetAdapter(TSharedRef<FLglAssetRegistry> InAssetRegistry)
    : AssetRegistry(InAssetRegistry)
{
}

FString FLglAssetAdapter::GetDomain() const
{
    return TEXT("asset");
}

FLglObjectResult FLglAssetAdapter::Query(const FLglObjectRequest& Request)
{
    if (!Request.Object.IsValid())
    {
        return FLglResult::FromDiagnostic(FLglDiagnostics::Make(
            TEXT("error"),
            TEXT("invalid_request"),
            TEXT("Asset query requires a normalized LGL object.")));
    }

    if (!AssetRegistry->IsAvailable())
    {
        return FLglResult::FromDiagnostic(FLglDiagnostics::Make(
            TEXT("error"),
            TEXT("asset_registry_unavailable"),
            TEXT("UE Asset Registry is not available.")));
    }

    if (HasField(Request.Object, TEXT("orderBy")))
    {
        return UnsupportedQueryFeature(
            TEXT("orderBy"),
            TEXT("Asset results currently use deterministic path order."));
    }
    if (HasField(Request.Object, TEXT("with")))
    {
        return UnsupportedQueryFeature(
            TEXT("with"),
            TEXT("registryTags expansion will be added after the base asset result is stable."));
    }

    FString Text;
    int32 Limit = DefaultLimit;
    TSharedPtr<FJsonObject> Where;
    FLglObjectResult Error;
    if (!ReadFindText(Request.Object, Text, Error)
        || !ReadWhere(Request.Object, Where, Error)
        || !ReadPageLimit(Request.Object, Limit, Error))
    {
        return Error;
    }

    TArray<TSharedPtr<FJsonObject>> Assets;
    AssetRegistry->SearchAssets(Text, Limit, Where, Assets);

    FLglObjectResult Result;
    Result.Object = MakeShared<FJsonObject>();
    Result.Object->SetStringField(TEXT("kind"), TEXT("asset_result"));
    Result.Object->SetArrayField(TEXT("assets"), EncodeObjectArray(Assets));
    return Result;
}

FLglObjectResult FLglAssetAdapter::Patch(const FLglObjectRequest& Request)
{
    return FLglResult::FromDiagnostic(FLglDiagnostics::Make(
        TEXT("error"),
        TEXT("unsupported_operation"),
        TEXT("The asset domain has no LGL patch operation.")));
}
}
