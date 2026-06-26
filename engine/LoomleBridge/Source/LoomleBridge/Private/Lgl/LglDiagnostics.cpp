// Copyright 2026 Loomle contributors.

#include "LglDiagnostics.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

namespace Loomle::Lgl
{
FLglDiagnosticBuilder::FLglDiagnosticBuilder(
    const FString& Severity,
    const FString& Code,
    const FString& Message)
    : Diagnostic(MakeShared<FJsonObject>())
{
    Diagnostic->SetStringField(TEXT("severity"), Severity);
    Diagnostic->SetStringField(TEXT("code"), Code);
    Diagnostic->SetStringField(TEXT("message"), Message);
}

FLglDiagnosticBuilder& FLglDiagnosticBuilder::Suggestion(const FString& Value)
{
    if (!Value.IsEmpty())
    {
        Diagnostic->SetStringField(TEXT("suggestion"), Value);
    }
    return *this;
}

FLglDiagnosticBuilder& FLglDiagnosticBuilder::Domain(const FString& Value)
{
    if (!Value.IsEmpty())
    {
        Diagnostic->SetStringField(TEXT("domain"), Value);
    }
    return *this;
}

FLglDiagnosticBuilder& FLglDiagnosticBuilder::Operation(const FString& Value)
{
    if (!Value.IsEmpty())
    {
        Diagnostic->SetStringField(TEXT("operation"), Value);
    }
    return *this;
}

FLglDiagnosticBuilder& FLglDiagnosticBuilder::Ref(const FString& Value)
{
    if (!Value.IsEmpty())
    {
        Diagnostic->SetStringField(TEXT("ref"), Value);
    }
    return *this;
}

FLglDiagnosticBuilder& FLglDiagnosticBuilder::Path(std::initializer_list<FString> Segments)
{
    return Path(TArray<FString>(Segments));
}

FLglDiagnosticBuilder& FLglDiagnosticBuilder::Path(const TArray<FString>& Segments)
{
    TArray<TSharedPtr<FJsonValue>> Values;
    for (const FString& Segment : Segments)
    {
        Values.Add(MakeShared<FJsonValueString>(Segment));
    }
    Diagnostic->SetArrayField(TEXT("path"), Values);
    return *this;
}

FLglDiagnosticBuilder& FLglDiagnosticBuilder::Actual(const FString& Value)
{
    Diagnostic->SetStringField(TEXT("actual"), Value);
    return *this;
}

FLglDiagnosticBuilder& FLglDiagnosticBuilder::Supported(std::initializer_list<FString> Values)
{
    return Supported(TArray<FString>(Values));
}

FLglDiagnosticBuilder& FLglDiagnosticBuilder::Supported(const TArray<FString>& Values)
{
    TArray<TSharedPtr<FJsonValue>> SupportedValues;
    for (const FString& Value : Values)
    {
        SupportedValues.Add(MakeShared<FJsonValueString>(Value));
    }
    Diagnostic->SetArrayField(TEXT("supported"), SupportedValues);
    return *this;
}

TSharedPtr<FJsonObject> FLglDiagnosticBuilder::Build() const
{
    return Diagnostic;
}

FLglDiagnosticBuilder FLglDiagnostics::Error(const FString& Code, const FString& Message)
{
    return FLglDiagnosticBuilder(TEXT("error"), Code, Message);
}

FLglDiagnosticBuilder FLglDiagnostics::Warning(const FString& Code, const FString& Message)
{
    return FLglDiagnosticBuilder(TEXT("warning"), Code, Message);
}

FLglDiagnosticBuilder FLglDiagnostics::Info(const FString& Code, const FString& Message)
{
    return FLglDiagnosticBuilder(TEXT("info"), Code, Message);
}

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
