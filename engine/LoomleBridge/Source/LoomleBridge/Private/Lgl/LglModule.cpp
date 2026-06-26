// Copyright 2026 Loomle contributors.

#include "LglModule.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "LglDiagnostics.h"
#include "LglResult.h"

namespace Loomle::Lgl
{
TSharedPtr<FJsonObject> FLglModule::MakeInvalidRequest(const FString& Message, const FString& Suggestion)
{
    return FLglResult::Error(FLglDiagnostics::Make(TEXT("error"), TEXT("invalid_request"), Message, Suggestion));
}

TSharedPtr<FJsonObject> FLglModule::MakeInvalidObject(const FString& Message, const FString& Suggestion)
{
    return FLglResult::Error(FLglDiagnostics::Make(TEXT("error"), TEXT("invalid_object"), Message, Suggestion));
}

TSharedPtr<FJsonObject> FLglModule::BuildQueryResult(const TSharedPtr<FJsonObject>& Arguments)
{
    if (!Arguments.IsValid())
    {
        return MakeInvalidRequest(TEXT("lgl.query requires an object request envelope."));
    }

    const TSharedPtr<FJsonObject>* ObjectPtr = nullptr;
    if (!Arguments->TryGetObjectField(TEXT("object"), ObjectPtr) || ObjectPtr == nullptr || !(*ObjectPtr).IsValid())
    {
        return MakeInvalidRequest(
            TEXT("lgl.query requires an object field containing a normalized LGL query object."),
            TEXT("Send { \"object\": { \"kind\": \"query\", \"target\": ... } }."));
    }

    const TSharedPtr<FJsonObject> Object = *ObjectPtr;
    FString Kind;
    if (!Object->TryGetStringField(TEXT("kind"), Kind) || Kind.IsEmpty())
    {
        return MakeInvalidObject(TEXT("LGL object is missing required string field kind."));
    }
    if (!Kind.Equals(TEXT("query")))
    {
        return MakeInvalidObject(
            FString::Printf(TEXT("lgl.query expects object.kind = query, got %s."), *Kind),
            TEXT("Use lgl.patch for patch objects once that RPC exists."));
    }

    const TSharedPtr<FJsonObject>* TargetPtr = nullptr;
    if (!Object->TryGetObjectField(TEXT("target"), TargetPtr) || TargetPtr == nullptr || !(*TargetPtr).IsValid())
    {
        return MakeInvalidObject(TEXT("LGL query object is missing required target object."));
    }

    const TSharedPtr<FJsonObject> Target = *TargetPtr;
    FString Domain;
    if (!Target->TryGetStringField(TEXT("domain"), Domain) || Domain.IsEmpty())
    {
        return MakeInvalidObject(TEXT("LGL query target is missing required string field domain."));
    }

    FString Asset;
    if (!Target->TryGetStringField(TEXT("asset"), Asset) || Asset.IsEmpty())
    {
        return MakeInvalidObject(TEXT("LGL query target is missing required string field asset."));
    }

    const TSharedPtr<FJsonObject>* GraphPtr = nullptr;
    if (!Target->TryGetObjectField(TEXT("graph"), GraphPtr) || GraphPtr == nullptr || !(*GraphPtr).IsValid())
    {
        return MakeInvalidObject(TEXT("LGL query target is missing required graph reference object."));
    }

    if (!Domain.Equals(TEXT("blueprint")))
    {
        return FLglResult::Error(FLglDiagnostics::Make(
            TEXT("error"),
            TEXT("unsupported_domain"),
            FString::Printf(TEXT("lgl.query only supports target.domain = blueprint in the first spike, got %s."), *Domain),
            TEXT("Use Target.domain = \"blueprint\" for the current LGL bridge query spike.")));
    }

    const TSharedPtr<FJsonValue> FindValue = Object->TryGetField(TEXT("find"));
    if (FindValue.IsValid() && !FindValue->IsNull())
    {
        const TSharedPtr<FJsonObject>* FindPtr = nullptr;
        if (!FindValue->TryGetObject(FindPtr) || FindPtr == nullptr || !(*FindPtr).IsValid())
        {
            return MakeInvalidObject(TEXT("LGL query find field must be an object when present."));
        }

        FString FindKind;
        if (!(*FindPtr)->TryGetStringField(TEXT("kind"), FindKind) || FindKind.IsEmpty())
        {
            return MakeInvalidObject(TEXT("LGL query find object is missing required string field kind."));
        }

        if (!FindKind.Equals(TEXT("nodes")))
        {
            return FLglResult::Error(FLglDiagnostics::Make(
                TEXT("error"),
                TEXT("unsupported_query"),
                FString::Printf(TEXT("lgl.query does not support find kind %s in the first stub."), *FindKind),
                TEXT("Use an empty query or find nodes in the first LGL bridge query spike.")));
        }
    }

    return FLglResult::Error(FLglDiagnostics::Make(
        TEXT("info"),
        TEXT("not_implemented"),
        TEXT("lgl.query reached the LGL-native bridge stub; Blueprint readback is not implemented yet."),
        TEXT("Next implementation step is Blueprint asset and graph resolution in Private/Lgl/Blueprint.")));
}
}
