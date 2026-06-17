// Copyright 2026 Loomle contributors.

#pragma once

#include "CoreMinimal.h"

class FJsonObject;

namespace Loomle::Lgl
{
class FLglModule
{
public:
    static TSharedPtr<FJsonObject> BuildObjectQueryResult(const TSharedPtr<FJsonObject>& Arguments);

private:
    static TSharedPtr<FJsonObject> MakeInvalidRequest(const FString& Message, const FString& Suggestion = FString());
    static TSharedPtr<FJsonObject> MakeInvalidObject(const FString& Message, const FString& Suggestion = FString());
};
}
