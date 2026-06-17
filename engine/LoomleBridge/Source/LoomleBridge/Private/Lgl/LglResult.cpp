// Copyright 2026 Loomle contributors.

#include "LglResult.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

namespace Loomle::Lgl
{
TSharedPtr<FJsonObject> FLglResult::Error(const TSharedPtr<FJsonObject>& Diagnostic)
{
    TArray<TSharedPtr<FJsonValue>> Diagnostics;
    if (Diagnostic.IsValid())
    {
        Diagnostics.Add(MakeShared<FJsonValueObject>(Diagnostic));
    }

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetArrayField(TEXT("diagnostics"), Diagnostics);
    return Result;
}
}
