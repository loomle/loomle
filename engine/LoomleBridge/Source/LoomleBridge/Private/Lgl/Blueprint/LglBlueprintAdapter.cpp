// Copyright 2026 Loomle contributors.

#include "LglBlueprintAdapter.h"

#include "Dom/JsonObject.h"
#include "../LglDiagnostics.h"
#include "../LglResult.h"

namespace Loomle::Lgl
{
FString FLglBlueprintAdapter::GetDomain() const
{
    return TEXT("blueprint");
}

FLglObjectResult FLglBlueprintAdapter::Query(const FLglObjectRequest& Request)
{
    const TSharedPtr<FJsonObject> Target = Request.Object->GetObjectField(TEXT("target"));

    FString Asset;
    if (!Target->TryGetStringField(TEXT("asset"), Asset) || Asset.IsEmpty())
    {
        return FLglResult::FromDiagnostic(FLglDiagnostics::Make(
            TEXT("error"),
            TEXT("invalid_object"),
            TEXT("LGL query target is missing required string field asset.")));
    }

    const TSharedPtr<FJsonObject>* GraphPtr = nullptr;
    if (!Target->TryGetObjectField(TEXT("graph"), GraphPtr) || GraphPtr == nullptr || !(*GraphPtr).IsValid())
    {
        return FLglResult::FromDiagnostic(FLglDiagnostics::Make(
            TEXT("error"),
            TEXT("invalid_object"),
            TEXT("LGL query target is missing required graph reference object.")));
    }

    const TSharedPtr<FJsonObject>* FindPtr = nullptr;
    if (Request.Object->TryGetObjectField(TEXT("find"), FindPtr) && FindPtr != nullptr && (*FindPtr).IsValid())
    {
        FString FindKind;
        (*FindPtr)->TryGetStringField(TEXT("kind"), FindKind);
        if (!FindKind.Equals(TEXT("nodes")))
        {
            return FLglResult::FromDiagnostic(FLglDiagnostics::Make(
                TEXT("error"),
                TEXT("unsupported_query"),
                FString::Printf(TEXT("lgl.query does not support find kind %s in the first stub."), *FindKind),
                TEXT("Use an empty query or find nodes in the first LGL bridge query spike.")));
        }
    }

    return FLglResult::FromDiagnostic(FLglDiagnostics::Make(
        TEXT("info"),
        TEXT("not_implemented"),
        TEXT("lgl.query reached the LGL-native bridge stub; Blueprint readback is not implemented yet."),
        TEXT("Next implementation step is Blueprint asset and graph resolution in Private/Lgl/Blueprint.")));
}

FLglObjectResult FLglBlueprintAdapter::Patch(const FLglObjectRequest& Request)
{
    return FLglResult::FromDiagnostic(FLglDiagnostics::Make(
        TEXT("error"),
        TEXT("not_implemented"),
        TEXT("lgl.patch is not implemented for Blueprint yet.")));
}
}
