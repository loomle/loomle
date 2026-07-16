// Copyright 2026 Loomle contributors.

#pragma once

#include "CoreMinimal.h"

class FJsonObject;
class FJsonValue;
class UBlueprint;
class UClass;
class UEdGraph;
class UObject;
class UPackage;

namespace Loomle::Sal
{
struct FSalQuery
{
    FString Alias;
    TSharedPtr<FJsonObject> TargetValue;
    TSharedPtr<FJsonObject> Operation;
    TSharedPtr<FJsonObject> Where;
    TArray<FString> With;
    TArray<TSharedPtr<FJsonObject>> OrderBy;
    int32 PageLimit = 0;
    FString PageAfter;
    TSharedPtr<FJsonObject> Source;
};

struct FSalPatch
{
    FString Alias;
    TSharedPtr<FJsonObject> TargetValue;
    bool bDryRun = false;
    TArray<TSharedPtr<FJsonValue>> Statements;
    TSharedPtr<FJsonObject> Source;
};

enum class ESalTargetKind : uint8
{
    AssetRoot,
    Asset,
    Blueprint,
    Class,
    Graph
};

struct FSalResolvedTarget
{
    ESalTargetKind Kind = ESalTargetKind::AssetRoot;
    FString Alias;
    FString AssetPath;
    FString Id;
    FString Name;
    TSharedPtr<FJsonObject> Locator;
    UObject* Object = nullptr;
    UPackage* Package = nullptr;
    UBlueprint* Blueprint = nullptr;
    UClass* Class = nullptr;
    UEdGraph* Graph = nullptr;
    TArray<FName> Interfaces;

    bool HasInterface(const FName Interface) const
    {
        return Interfaces.Contains(Interface);
    }
};

struct FSalPage
{
    int32 Offset = 0;
    int32 Limit = 50;
};
}
