// Copyright 2026 Loomle contributors.

#pragma once

#include "CoreMinimal.h"

class FJsonObject;
class FJsonValue;

namespace Loomle::Sal
{
class FSalDiagnosticBuilder
{
public:
    FSalDiagnosticBuilder(const FString& Severity, const FString& Code, const FString& Message);

    FSalDiagnosticBuilder& Path(std::initializer_list<FString> Segments);
    FSalDiagnosticBuilder& Path(const TArray<FString>& Segments);
    FSalDiagnosticBuilder& Interface(const FString& Value);
    FSalDiagnosticBuilder& Operation(const FString& Value);
    FSalDiagnosticBuilder& Ref(const FString& Value);
    FSalDiagnosticBuilder& Expected(const TSharedPtr<FJsonValue>& Value);
    FSalDiagnosticBuilder& Actual(const TSharedPtr<FJsonValue>& Value);
    FSalDiagnosticBuilder& Actual(const FString& Value);
    FSalDiagnosticBuilder& Supported(const TArray<FString>& Values);
    FSalDiagnosticBuilder& Matches(const TArray<FString>& Values);
    FSalDiagnosticBuilder& Suggestion(const FString& Value);
    TSharedPtr<FJsonObject> Build() const;

private:
    TSharedPtr<FJsonObject> Diagnostic;
};

class FSalDiagnostics
{
public:
    static FSalDiagnosticBuilder Error(const FString& Code, const FString& Message);
    static FSalDiagnosticBuilder Warning(const FString& Code, const FString& Message);
    static FSalDiagnosticBuilder Info(const FString& Code, const FString& Message);

    static TSharedPtr<FJsonObject> Result(const TSharedPtr<FJsonObject>& Diagnostic);
};
}
