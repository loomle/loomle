// Copyright 2026 Loomle contributors.

#include "LglModule.h"

#include "Asset/LglAssetAdapter.h"
#include "Blueprint/LglBlueprintAdapter.h"
#include "Dom/JsonObject.h"
#include "Graph/LglGraphAdapter.h"
#include "LglAdapterRegistry.h"
#include "LglDiagnostics.h"
#include "LglDomainAdapter.h"
#include "LglJsonCodec.h"
#include "LglObjectModel.h"
#include "LglResult.h"
#include "LglSchemaValidator.h"
#include "Services/LglAssetRegistry.h"

namespace Loomle::Lgl
{
namespace
{
FLglAdapterRegistry BuildRegistry()
{
    TSharedRef<FLglAssetRegistry> AssetRegistry = MakeShared<FLglAssetRegistry>();
    FLglAdapterRegistry Registry;
    Registry.Register(MakeShared<FLglAssetAdapter>(AssetRegistry));
    Registry.Register(MakeShared<FLglBlueprintAdapter>());
    Registry.Register(MakeShared<FLglGraphAdapter>());
    return Registry;
}

FLglObjectResult UnsupportedDomain(const FString& Method, const FString& Domain)
{
    return FLglResult::FromDiagnostic(
        FLglDiagnostics::Error(
            TEXT("capability.unsupported_domain"),
            FString::Printf(TEXT("%s does not support target.domain = %s."), *Method, *Domain))
            .Path({TEXT("target"), TEXT("domain")})
            .Actual(Domain)
            .Supported({TEXT("asset"), TEXT("blueprint"), TEXT("graph")})
            .Suggestion(TEXT("Use target.domain = \"asset\", \"blueprint\", or \"graph\" for the current LGL bridge query milestone."))
            .Build());
}
}

TSharedPtr<FJsonObject> FLglModule::BuildQueryResult(const TSharedPtr<FJsonObject>& Arguments)
{
    constexpr const TCHAR* Method = TEXT("lgl.query");
    FLglObjectRequest Request;
    FLglObjectResult Error;
    if (!FLglJsonCodec::DecodeObjectRequest(Method, Arguments, Request, Error))
    {
        return FLglJsonCodec::EncodeObjectResult(Error);
    }

    if (!FLglSchemaValidator::ValidateRequest(Request, TEXT("query"), Error))
    {
        return FLglJsonCodec::EncodeObjectResult(Error);
    }

    const TSharedPtr<FJsonObject> Target = Request.Object->GetObjectField(TEXT("target"));
    FString Domain;
    Target->TryGetStringField(TEXT("domain"), Domain);

    FLglAdapterRegistry Registry = BuildRegistry();
    ILglDomainAdapter* Adapter = Registry.Find(Domain);
    FLglObjectResult Result = Adapter ? Adapter->Query(Request) : UnsupportedDomain(Method, Domain);
    if (!FLglSchemaValidator::ValidateResult(Result, Error))
    {
        return FLglJsonCodec::EncodeObjectResult(Error);
    }
    return FLglJsonCodec::EncodeObjectResult(Result);
}

TSharedPtr<FJsonObject> FLglModule::BuildPatchResult(const TSharedPtr<FJsonObject>& Arguments)
{
    constexpr const TCHAR* Method = TEXT("lgl.patch");
    FLglObjectRequest Request;
    FLglObjectResult Error;
    if (!FLglJsonCodec::DecodeObjectRequest(Method, Arguments, Request, Error))
    {
        return FLglJsonCodec::EncodeObjectResult(Error);
    }

    if (!FLglSchemaValidator::ValidateRequest(Request, TEXT("patch"), Error))
    {
        return FLglJsonCodec::EncodeObjectResult(Error);
    }

    const TSharedPtr<FJsonObject> Target = Request.Object->GetObjectField(TEXT("target"));
    FString Domain;
    Target->TryGetStringField(TEXT("domain"), Domain);

    FLglAdapterRegistry Registry = BuildRegistry();
    ILglDomainAdapter* Adapter = Registry.Find(Domain);
    FLglObjectResult Result = Adapter ? Adapter->Patch(Request) : UnsupportedDomain(Method, Domain);
    if (!FLglSchemaValidator::ValidateResult(Result, Error))
    {
        return FLglJsonCodec::EncodeObjectResult(Error);
    }
    return FLglJsonCodec::EncodeObjectResult(Result);
}
}
