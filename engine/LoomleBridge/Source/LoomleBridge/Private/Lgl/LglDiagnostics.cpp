// Copyright 2026 Loomle contributors.

#include "LglDiagnostics.h"

#include "Dom/JsonObject.h"

namespace Loomle::Lgl
{
TSharedPtr<FJsonObject> FLglDiagnostics::Make(
    const FString& Severity,
    const FString& Code,
    const FString& Message,
    const FString& Suggestion)
{
    TSharedPtr<FJsonObject> Diagnostic = MakeShared<FJsonObject>();
    Diagnostic->SetStringField(TEXT("severity"), Severity);
    Diagnostic->SetStringField(TEXT("code"), Code);
    Diagnostic->SetStringField(TEXT("message"), Message);
    if (!Suggestion.IsEmpty())
    {
        Diagnostic->SetStringField(TEXT("suggestion"), Suggestion);
    }
    return Diagnostic;
}
}
