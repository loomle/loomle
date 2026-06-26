// Copyright 2026 Loomle contributors.

#include "LglJsonCodec.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "LglDiagnostics.h"
#include "LglResult.h"

namespace Loomle::Lgl
{
bool FLglJsonCodec::DecodeObjectRequest(
    const FString& Method,
    const TSharedPtr<FJsonObject>& Arguments,
    FLglObjectRequest& OutRequest,
    FLglObjectResult& OutError)
{
    if (!Arguments.IsValid())
    {
        OutError = FLglResult::FromDiagnostic(FLglDiagnostics::Make(
            TEXT("error"),
            TEXT("language.invalid_object_shape"),
            FString::Printf(TEXT("%s requires an object request envelope."), *Method)));
        return false;
    }

    const TSharedPtr<FJsonObject>* ObjectPtr = nullptr;
    if (!Arguments->TryGetObjectField(TEXT("object"), ObjectPtr) || ObjectPtr == nullptr || !(*ObjectPtr).IsValid())
    {
        OutError = FLglResult::FromDiagnostic(FLglDiagnostics::Make(
            TEXT("error"),
            TEXT("language.invalid_object_shape"),
            FString::Printf(TEXT("%s requires an object field containing a normalized LGL object."), *Method),
            TEXT("Send { \"object\": { \"kind\": \"query\", \"target\": ... } }.")));
        return false;
    }

    OutRequest.Method = Method;
    OutRequest.Object = *ObjectPtr;
    return true;
}

TSharedPtr<FJsonObject> FLglJsonCodec::EncodeObjectResult(const FLglObjectResult& Result)
{
    TSharedPtr<FJsonObject> Encoded = MakeShared<FJsonObject>();
    if (Result.Object.IsValid())
    {
        Encoded->SetObjectField(TEXT("object"), Result.Object);
    }

    TArray<TSharedPtr<FJsonValue>> Diagnostics;
    for (const TSharedPtr<FJsonObject>& Diagnostic : Result.Diagnostics)
    {
        if (Diagnostic.IsValid())
        {
            Diagnostics.Add(MakeShared<FJsonValueObject>(Diagnostic));
        }
    }
    Encoded->SetArrayField(TEXT("diagnostics"), Diagnostics);

    if (Result.Page.IsValid())
    {
        Encoded->SetObjectField(TEXT("page"), Result.Page);
    }

    return Encoded;
}
}
