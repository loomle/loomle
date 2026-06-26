// Copyright 2026 Loomle contributors.

#include "LglAssetAdapter.h"

#include "../LglCapabilityValidator.h"
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

FLglQueryCapabilities AssetQueryCapabilities()
{
    FLglQueryCapabilities Capabilities;
    Capabilities.Domain = TEXT("asset");
    Capabilities.bValidateFindKinds = true;
    Capabilities.FindKinds = {TEXT("assets")};
    Capabilities.bValidateWhereFields = true;
    Capabilities.WhereFields = {TEXT("root"), TEXT("type"), TEXT("class"), TEXT("name"), TEXT("path")};
    Capabilities.bValidateDetails = true;
    Capabilities.bValidateOrderKeys = true;
    Capabilities.bSupportsPageAfter = false;
    Capabilities.bSupportsCompare = false;
    return Capabilities;
}

void ReadWhere(const TSharedPtr<FJsonObject>& Object, TSharedPtr<FJsonObject>& OutWhere)
{
    OutWhere.Reset();

    const TSharedPtr<FJsonValue> WhereValue = Object->TryGetField(TEXT("where"));
    if (!WhereValue.IsValid() || WhereValue->IsNull())
    {
        return;
    }

    const TSharedPtr<FJsonObject>* Where = nullptr;
    if (WhereValue->TryGetObject(Where) && Where != nullptr && (*Where).IsValid())
    {
        OutWhere = *Where;
    }
}

void ReadFindText(const TSharedPtr<FJsonObject>& Object, FString& OutText)
{
    OutText.Reset();

    const TSharedPtr<FJsonObject>* Find = nullptr;
    if (!Object->TryGetObjectField(TEXT("find"), Find) || Find == nullptr || !(*Find).IsValid())
    {
        return;
    }

    (*Find)->TryGetStringField(TEXT("text"), OutText);
}

void ReadPageLimit(const TSharedPtr<FJsonObject>& Object, int32& OutLimit)
{
    OutLimit = DefaultLimit;

    const TSharedPtr<FJsonObject>* Page = nullptr;
    if (!Object->TryGetObjectField(TEXT("page"), Page) || Page == nullptr || !(*Page).IsValid())
    {
        return;
    }

    double Limit = 0.0;
    if ((*Page)->TryGetNumberField(TEXT("limit"), Limit))
    {
        OutLimit = static_cast<int32>(Limit);
    }
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
            TEXT("language.invalid_object_shape"),
            TEXT("Asset query requires a normalized LGL object.")));
    }

    FLglObjectResult Error;
    if (!FLglCapabilityValidator::ValidateQuery(Request, AssetQueryCapabilities(), Error))
    {
        return Error;
    }

    if (!AssetRegistry->IsAvailable())
    {
        return FLglResult::FromDiagnostic(FLglDiagnostics::Make(
            TEXT("error"),
            TEXT("resolution.asset_registry_unavailable"),
            TEXT("UE Asset Registry is not available.")));
    }

    FString Text;
    int32 Limit = DefaultLimit;
    TSharedPtr<FJsonObject> Where;
    ReadFindText(Request.Object, Text);
    ReadWhere(Request.Object, Where);
    ReadPageLimit(Request.Object, Limit);

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
        TEXT("capability.unsupported_patch_op"),
        TEXT("The asset domain has no LGL patch operation.")));
}
}
