// Copyright 2026 Loomle contributors.

#include "LglResult.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "LglObjectModel.h"

namespace Loomle::Lgl
{
FLglObjectResult FLglResult::FromDiagnostic(const TSharedPtr<FJsonObject>& Diagnostic)
{
    FLglObjectResult Result;
    if (Diagnostic.IsValid())
    {
        Result.Diagnostics.Add(Diagnostic);
    }
    return Result;
}

TSharedPtr<FJsonObject> FLglResult::Error(const TSharedPtr<FJsonObject>& Diagnostic)
{
    const FLglObjectResult Result = FromDiagnostic(Diagnostic);
    TArray<TSharedPtr<FJsonValue>> Diagnostics;
    for (const TSharedPtr<FJsonObject>& Item : Result.Diagnostics)
    {
        if (Item.IsValid())
        {
            Diagnostics.Add(MakeShared<FJsonValueObject>(Item));
        }
    }

    TSharedPtr<FJsonObject> Encoded = MakeShared<FJsonObject>();
    Encoded->SetArrayField(TEXT("diagnostics"), Diagnostics);
    return Encoded;
}
}
