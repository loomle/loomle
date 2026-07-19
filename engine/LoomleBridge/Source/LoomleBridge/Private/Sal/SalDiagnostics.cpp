// Copyright 2026 Loomle contributors.

#include "SalDiagnostics.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

namespace Loomle::Sal
{
FSalDiagnosticBuilder::FSalDiagnosticBuilder(
    const FString& Severity,
    const FString& Code,
    const FString& Message)
    : Diagnostic(MakeShared<FJsonObject>())
{
    Diagnostic->SetStringField(TEXT("severity"), Severity);
    Diagnostic->SetStringField(TEXT("code"), Code);
    Diagnostic->SetStringField(TEXT("message"), Message);
}

FSalDiagnosticBuilder& FSalDiagnosticBuilder::Path(std::initializer_list<FString> Segments)
{
    TArray<FString> Values;
    Values.Reserve(static_cast<int32>(Segments.size()));
    for (const FString& Segment : Segments)
    {
        Values.Add(Segment);
    }
    return Path(Values);
}

FSalDiagnosticBuilder& FSalDiagnosticBuilder::Path(const TArray<FString>& Segments)
{
    TArray<TSharedPtr<FJsonValue>> Values;
    Values.Reserve(Segments.Num());
    for (const FString& Segment : Segments)
    {
        Values.Add(MakeShared<FJsonValueString>(Segment));
    }
    Diagnostic->SetArrayField(TEXT("path"), Values);
    return *this;
}

FSalDiagnosticBuilder& FSalDiagnosticBuilder::Interface(const FString& Value)
{
    Diagnostic->SetStringField(TEXT("domain"), Value);
    return *this;
}

FSalDiagnosticBuilder& FSalDiagnosticBuilder::Operation(const FString& Value)
{
    Diagnostic->SetStringField(TEXT("operation"), Value);
    return *this;
}

FSalDiagnosticBuilder& FSalDiagnosticBuilder::Ref(const FString& Value)
{
    Diagnostic->SetStringField(TEXT("ref"), Value);
    return *this;
}

FSalDiagnosticBuilder& FSalDiagnosticBuilder::Expected(const TSharedPtr<FJsonValue>& Value)
{
    if (Value.IsValid())
    {
        Diagnostic->SetField(TEXT("expected"), Value);
    }
    return *this;
}

FSalDiagnosticBuilder& FSalDiagnosticBuilder::Actual(const TSharedPtr<FJsonValue>& Value)
{
    if (Value.IsValid())
    {
        Diagnostic->SetField(TEXT("actual"), Value);
    }
    return *this;
}

FSalDiagnosticBuilder& FSalDiagnosticBuilder::Actual(const FString& Value)
{
    Diagnostic->SetStringField(TEXT("actual"), Value);
    return *this;
}

FSalDiagnosticBuilder& FSalDiagnosticBuilder::Supported(const TArray<FString>& Values)
{
    TArray<TSharedPtr<FJsonValue>> Encoded;
    Encoded.Reserve(Values.Num());
    for (const FString& Value : Values)
    {
        Encoded.Add(MakeShared<FJsonValueString>(Value));
    }
    Diagnostic->SetArrayField(TEXT("supported"), Encoded);
    return *this;
}

FSalDiagnosticBuilder& FSalDiagnosticBuilder::Matches(const TArray<FString>& Values)
{
    TArray<TSharedPtr<FJsonValue>> Encoded;
    Encoded.Reserve(Values.Num());
    for (const FString& Value : Values)
    {
        Encoded.Add(MakeShared<FJsonValueString>(Value));
    }
    Diagnostic->SetArrayField(TEXT("matches"), Encoded);
    return *this;
}

FSalDiagnosticBuilder& FSalDiagnosticBuilder::Suggestion(const FString& Value)
{
    Diagnostic->SetStringField(TEXT("suggestion"), Value);
    return *this;
}

TSharedPtr<FJsonObject> FSalDiagnosticBuilder::Build() const
{
    return Diagnostic;
}

FSalDiagnosticBuilder FSalDiagnostics::Error(const FString& Code, const FString& Message)
{
    return FSalDiagnosticBuilder(TEXT("error"), Code, Message);
}

FSalDiagnosticBuilder FSalDiagnostics::Warning(const FString& Code, const FString& Message)
{
    return FSalDiagnosticBuilder(TEXT("warning"), Code, Message);
}

FSalDiagnosticBuilder FSalDiagnostics::Info(const FString& Code, const FString& Message)
{
    return FSalDiagnosticBuilder(TEXT("info"), Code, Message);
}

TSharedPtr<FJsonObject> FSalDiagnostics::Result(const TSharedPtr<FJsonObject>& Diagnostic)
{
    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    TArray<TSharedPtr<FJsonValue>> Diagnostics;
    if (Diagnostic.IsValid())
    {
        Diagnostics.Add(MakeShared<FJsonValueObject>(Diagnostic));
    }
    Result->SetArrayField(TEXT("diagnostics"), Diagnostics);
    return Result;
}
}
