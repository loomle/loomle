// Copyright 2026 Loomle contributors.

#include "LglGraphResolver.h"

#include "Dom/JsonObject.h"
#include "EdGraph/EdGraph.h"
#include "Engine/Blueprint.h"
#include "../LglDiagnostics.h"
#include "../LglResult.h"
#include "Misc/PackageName.h"

namespace Loomle::Lgl
{
namespace
{
FString NormalizeAssetPath(const FString& InAssetPath)
{
    FString AssetPath = InAssetPath;
    FString Left;
    FString Right;
    if (AssetPath.Split(TEXT("."), &Left, &Right, ESearchCase::CaseSensitive, ESearchDir::FromEnd))
    {
        AssetPath = Left;
    }
    return AssetPath;
}

FString ObjectPathForAssetPath(const FString& AssetPath)
{
    const FString AssetName = FPackageName::GetLongPackageAssetName(AssetPath);
    return FString::Printf(TEXT("%s.%s"), *AssetPath, *AssetName);
}

FString GraphId(const UEdGraph* Graph)
{
    return Graph != nullptr ? Graph->GraphGuid.ToString(EGuidFormats::DigitsWithHyphensLower) : FString();
}

FLglObjectResult ResolutionError(
    const FString& Code,
    const FString& Message,
    const TArray<FString>& Path,
    const FString& Actual,
    const FString& Suggestion)
{
    return FLglResult::FromDiagnostic(
        FLglDiagnostics::Error(Code, Message)
            .Domain(TEXT("graph"))
            .Path(Path)
            .Actual(Actual)
            .Suggestion(Suggestion)
            .Build());
}

bool ReadGraphRef(
    const TSharedPtr<FJsonObject>& Target,
    FString& OutKind,
    FString& OutValue)
{
    OutKind.Reset();
    OutValue.Reset();

    const TSharedPtr<FJsonObject>* GraphRef = nullptr;
    if (!Target.IsValid() || !Target->TryGetObjectField(TEXT("graph"), GraphRef) || GraphRef == nullptr || !(*GraphRef).IsValid())
    {
        return false;
    }

    (*GraphRef)->TryGetStringField(TEXT("kind"), OutKind);
    if (OutKind == TEXT("name"))
    {
        (*GraphRef)->TryGetStringField(TEXT("name"), OutValue);
        return true;
    }
    if (OutKind == TEXT("id"))
    {
        (*GraphRef)->TryGetStringField(TEXT("id"), OutValue);
        return true;
    }
    return false;
}

UEdGraph* FindBlueprintGraphByName(UBlueprint* Blueprint, const FString& GraphName)
{
    if (Blueprint == nullptr)
    {
        return nullptr;
    }

    TArray<UEdGraph*> Graphs;
    Blueprint->GetAllGraphs(Graphs);
    for (UEdGraph* Graph : Graphs)
    {
        if (Graph != nullptr && Graph->GetName().Equals(GraphName, ESearchCase::IgnoreCase))
        {
            return Graph;
        }
    }
    return nullptr;
}

UEdGraph* FindBlueprintGraphById(UBlueprint* Blueprint, const FString& Id)
{
    if (Blueprint == nullptr)
    {
        return nullptr;
    }

    TArray<UEdGraph*> Graphs;
    Blueprint->GetAllGraphs(Graphs);
    for (UEdGraph* Graph : Graphs)
    {
        if (Graph != nullptr && GraphId(Graph).Equals(Id, ESearchCase::IgnoreCase))
        {
            return Graph;
        }
    }
    return nullptr;
}
}

bool FLglGraphResolver::Resolve(
    const TSharedPtr<FJsonObject>& Target,
    FLglResolvedGraph& OutGraph,
    FLglObjectResult& OutError) const
{
    OutGraph = FLglResolvedGraph();

    FString RawAssetPath;
    Target->TryGetStringField(TEXT("asset"), RawAssetPath);
    const FString AssetPath = NormalizeAssetPath(RawAssetPath);
    if (!FPackageName::IsValidLongPackageName(AssetPath))
    {
        OutError = ResolutionError(
            TEXT("resolution.asset_not_found"),
            FString::Printf(TEXT("Graph target asset path is not a valid long package name: %s."), *RawAssetPath),
            {TEXT("target"), TEXT("asset")},
            RawAssetPath,
            TEXT("Use a UE long package path such as /Game/Blueprints/BP_Player."));
        return false;
    }

    UObject* Asset = LoadObject<UObject>(nullptr, *ObjectPathForAssetPath(AssetPath));
    if (Asset == nullptr)
    {
        OutError = ResolutionError(
            TEXT("resolution.asset_not_found"),
            FString::Printf(TEXT("Graph target asset was not found: %s."), *AssetPath),
            {TEXT("target"), TEXT("asset")},
            AssetPath,
            TEXT("Use query asset to find the asset path before querying its graph."));
        return false;
    }

    UBlueprint* Blueprint = Cast<UBlueprint>(Asset);
    if (Blueprint == nullptr)
    {
        OutError = ResolutionError(
            TEXT("resolution.unsupported_graph_owner"),
            FString::Printf(TEXT("Graph resolver currently supports Blueprint assets only; got %s."), *Asset->GetClass()->GetPathName()),
            {TEXT("target"), TEXT("asset")},
            AssetPath,
            TEXT("Use a Blueprint asset for now; Material and PCG graph owners will be added through this resolver."));
        return false;
    }

    FString GraphRefKind;
    FString GraphRefValue;
    ReadGraphRef(Target, GraphRefKind, GraphRefValue);

    UEdGraph* Graph = nullptr;
    if (GraphRefKind == TEXT("name"))
    {
        Graph = FindBlueprintGraphByName(Blueprint, GraphRefValue);
    }
    else if (GraphRefKind == TEXT("id"))
    {
        Graph = FindBlueprintGraphById(Blueprint, GraphRefValue);
    }

    if (Graph == nullptr)
    {
        OutError = ResolutionError(
            TEXT("resolution.graph_not_found"),
            FString::Printf(TEXT("Graph %s was not found in %s."), *GraphRefValue, *AssetPath),
            {TEXT("target"), TEXT("graph")},
            GraphRefValue,
            TEXT("Use a graph name or id returned by graph list/readback before querying graph contents."));
        return false;
    }

    OutGraph.AssetPath = AssetPath;
    OutGraph.Blueprint = Blueprint;
    OutGraph.Graph = Graph;
    OutGraph.GraphName = Graph->GetName();
    OutGraph.GraphId = GraphId(Graph);
    return true;
}
}
