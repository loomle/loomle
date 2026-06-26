// Copyright 2026 Loomle contributors.

#pragma once

#include "CoreMinimal.h"

class FJsonObject;
class FJsonValue;

namespace Loomle::Lgl
{
class FLglDiagnosticBuilder
{
public:
    FLglDiagnosticBuilder(
        const FString& Severity,
        const FString& Code,
        const FString& Message);

    FLglDiagnosticBuilder& Suggestion(const FString& Value);
    FLglDiagnosticBuilder& Domain(const FString& Value);
    FLglDiagnosticBuilder& Operation(const FString& Value);
    FLglDiagnosticBuilder& Ref(const FString& Value);
    FLglDiagnosticBuilder& Path(std::initializer_list<FString> Segments);
    FLglDiagnosticBuilder& Path(const TArray<FString>& Segments);
    FLglDiagnosticBuilder& Actual(const FString& Value);
    FLglDiagnosticBuilder& Supported(std::initializer_list<FString> Values);
    FLglDiagnosticBuilder& Supported(const TArray<FString>& Values);

    TSharedPtr<FJsonObject> Build() const;

private:
    TSharedPtr<FJsonObject> Diagnostic;
};

class FLglDiagnostics
{
public:
    static FLglDiagnosticBuilder Error(const FString& Code, const FString& Message);
    static FLglDiagnosticBuilder Warning(const FString& Code, const FString& Message);
    static FLglDiagnosticBuilder Info(const FString& Code, const FString& Message);

    static TSharedPtr<FJsonObject> Make(
        const FString& Severity,
        const FString& Code,
        const FString& Message,
        const FString& Suggestion = FString());
};
}
