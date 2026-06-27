// Copyright 2026 Loomle contributors.

#include "LglBlueprintGraphService.h"

#include "Dom/JsonObject.h"
#include "../LglDiagnostics.h"
#include "../LglResult.h"
#include "LglBlueprintGraphPatchService.h"
#include "LglBlueprintGraphReadService.h"
#include "LglBlueprintGraphResolver.h"

namespace Loomle::Lgl
{
namespace
{
FLglObjectResult InvalidTarget(const FString& Message, const FString& Suggestion)
{
    return FLglResult::FromDiagnostic(
        FLglDiagnostics::Error(TEXT("language.invalid_object_shape"), Message)
            .Domain(TEXT("graph"))
            .Suggestion(Suggestion)
            .Build());
}

bool ValidateGraphTarget(const FLglObjectRequest& Request, FLglObjectResult& OutError)
{
    const TSharedPtr<FJsonObject>* Target = nullptr;
    if (!Request.Object.IsValid()
        || !Request.Object->TryGetObjectField(TEXT("target"), Target)
        || Target == nullptr
        || !(*Target).IsValid())
    {
        OutError = InvalidTarget(
            TEXT("Graph request is missing target object."),
            TEXT("Use target.domain = \"blueprint\" with target.asset and target.graph."));
        return false;
    }

    FString Asset;
    if (!(*Target)->TryGetStringField(TEXT("asset"), Asset) || Asset.IsEmpty())
    {
        OutError = InvalidTarget(
            TEXT("Graph target is missing required string field asset."),
            TEXT("Use target.asset to identify the Blueprint graph-owning asset."));
        return false;
    }

    const TSharedPtr<FJsonObject>* Graph = nullptr;
    if (!(*Target)->TryGetObjectField(TEXT("graph"), Graph) || Graph == nullptr || !(*Graph).IsValid())
    {
        OutError = InvalidTarget(
            TEXT("Graph target is missing required graph reference object."),
            TEXT("Use target.graph = { \"kind\": \"name\", \"name\": \"EventGraph\" } or an id reference."));
        return false;
    }

    return true;
}

bool ResolveGraph(const FLglObjectRequest& Request, FLglBlueprintResolvedGraph& OutGraph, FLglObjectResult& OutError)
{
    if (!ValidateGraphTarget(Request, OutError))
    {
        return false;
    }

    FLglBlueprintGraphResolver Resolver;
    return Resolver.Resolve(Request.Object->GetObjectField(TEXT("target")), OutGraph, OutError);
}
}

FLglObjectResult FLglBlueprintGraphService::Query(const FLglObjectRequest& Request)
{
    FLglObjectResult Error;
    FLglBlueprintResolvedGraph ResolvedGraph;
    if (!ResolveGraph(Request, ResolvedGraph, Error))
    {
        return Error;
    }

    FLglBlueprintGraphReadService ReadService;
    return ReadService.Query(Request, ResolvedGraph);
}

FLglObjectResult FLglBlueprintGraphService::Patch(const FLglObjectRequest& Request)
{
    FLglObjectResult Error;
    FLglBlueprintResolvedGraph ResolvedGraph;
    if (!ResolveGraph(Request, ResolvedGraph, Error))
    {
        return Error;
    }

    FLglBlueprintGraphPatchService PatchService;
    return PatchService.Patch(Request, ResolvedGraph);
}
}
