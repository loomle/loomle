// Copyright 2026 Loomle contributors.

#include "LglGraphAdapter.h"

#include "Dom/JsonObject.h"
#include "../LglCapabilityValidator.h"
#include "../LglDiagnostics.h"
#include "../LglResult.h"

namespace Loomle::Lgl
{
namespace
{
FLglQueryCapabilities GraphQueryCapabilities()
{
    FLglQueryCapabilities Capabilities;
    Capabilities.Domain = TEXT("graph");
    Capabilities.bValidateFindKinds = true;
    Capabilities.FindKinds = {TEXT("nodes"), TEXT("path"), TEXT("palette_entry")};
    Capabilities.bValidateWhereFields = true;
    Capabilities.WhereFields = {
        TEXT("type"),
        TEXT("name"),
        TEXT("id"),
        TEXT("comment"),
        TEXT("component"),
        TEXT("contextSensitive")
    };
    Capabilities.bValidateDetails = true;
    Capabilities.Details = {TEXT("pins"), TEXT("defaults")};
    Capabilities.bValidateOrderKeys = true;
    Capabilities.OrderKeys = {TEXT("name"), TEXT("type"), TEXT("id")};
    Capabilities.bSupportsPageAfter = false;
    Capabilities.bSupportsCompare = false;
    return Capabilities;
}

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
            TEXT("Graph query is missing target object."),
            TEXT("Use target.domain = \"graph\" with target.asset and target.graph."));
        return false;
    }

    FString Asset;
    if (!(*Target)->TryGetStringField(TEXT("asset"), Asset) || Asset.IsEmpty())
    {
        OutError = InvalidTarget(
            TEXT("Graph query target is missing required string field asset."),
            TEXT("Use target.asset to identify the Blueprint, Material, PCG, or other graph-owning asset."));
        return false;
    }

    const TSharedPtr<FJsonObject>* Graph = nullptr;
    if (!(*Target)->TryGetObjectField(TEXT("graph"), Graph) || Graph == nullptr || !(*Graph).IsValid())
    {
        OutError = InvalidTarget(
            TEXT("Graph query target is missing required graph reference object."),
            TEXT("Use target.graph = { \"kind\": \"name\", \"name\": \"EventGraph\" } or an id reference."));
        return false;
    }

    return true;
}

FLglObjectResult NotImplemented()
{
    return FLglResult::FromDiagnostic(
        FLglDiagnostics::Info(
            TEXT("capability.not_implemented"),
            TEXT("lgl.query reached the graph bridge adapter; graph readback is not implemented yet."))
            .Domain(TEXT("graph"))
            .Suggestion(TEXT("Next implementation step is graph asset resolution and compact graph readback."))
            .Build());
}
}

FString FLglGraphAdapter::GetDomain() const
{
    return TEXT("graph");
}

FLglObjectResult FLglGraphAdapter::Query(const FLglObjectRequest& Request)
{
    FLglObjectResult Error;
    if (!ValidateGraphTarget(Request, Error))
    {
        return Error;
    }

    if (!FLglCapabilityValidator::ValidateQuery(Request, GraphQueryCapabilities(), Error))
    {
        return Error;
    }

    return NotImplemented();
}

FLglObjectResult FLglGraphAdapter::Patch(const FLglObjectRequest& Request)
{
    return FLglResult::FromDiagnostic(
        FLglDiagnostics::Error(
            TEXT("capability.not_implemented"),
            TEXT("lgl.patch is not implemented for graph yet."))
            .Domain(TEXT("graph"))
            .Build());
}
}
