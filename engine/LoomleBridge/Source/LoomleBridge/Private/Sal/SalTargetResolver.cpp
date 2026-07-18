// Copyright 2026 Loomle contributors.

#include "SalTargetResolver.h"

#include "AssetRegistry/AssetData.h"
#include "Blueprint/UserWidgetBlueprint.h"
#include "Dom/JsonObject.h"
#include "EdGraph/EdGraph.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Misc/PackageName.h"
#include "SalDiagnostics.h"
#include "UObject/SoftObjectPath.h"
#include "UObject/UObjectGlobals.h"
#include "WidgetBlueprint.h"

namespace Loomle::Sal
{
namespace
{
FString GuidText(const FGuid& Guid)
{
    return Guid.ToString(EGuidFormats::DigitsWithHyphensLower);
}

FString NormalizeObjectPath(const FString& Input)
{
    FString Path = Input;
    if (Path.StartsWith(TEXT("Blueprint'")) && Path.EndsWith(TEXT("'")))
    {
        Path = Path.Mid(10, Path.Len() - 11);
    }
    if (!Path.Contains(TEXT(".")) && FPackageName::IsValidLongPackageName(Path))
    {
        const FString AssetName = FPackageName::GetLongPackageAssetName(Path);
        Path = FString::Printf(TEXT("%s.%s"), *Path, *AssetName);
    }
    return Path;
}

TSharedPtr<FJsonObject> ResolutionError(
    const FString& Message,
    const FString& Ref,
    const FString& Suggestion = FString())
{
    FSalDiagnosticBuilder Diagnostic = FSalDiagnostics::Error(TEXT("resolution.target_not_found"), Message)
        .Path({TEXT("object"), TEXT("target")})
        .Ref(Ref);
    if (!Suggestion.IsEmpty())
    {
        Diagnostic.Suggestion(Suggestion);
    }
    return FSalDiagnostics::Result(Diagnostic.Build());
}

TSharedPtr<FJsonObject> InvalidTarget(const FString& Message)
{
    return FSalDiagnostics::Result(
        FSalDiagnostics::Error(TEXT("validation.invalid_target_locator"), Message)
            .Path({TEXT("object"), TEXT("target"), TEXT("value")})
            .Build());
}

bool ReadCall(const TSharedPtr<FJsonObject>& Value, FString& OutCallee, TSharedPtr<FJsonObject>& OutArgs)
{
    FString Kind;
    const TSharedPtr<FJsonObject>* Args = nullptr;
    if (!Value.IsValid()
        || !Value->TryGetStringField(TEXT("kind"), Kind)
        || Kind != TEXT("call")
        || !Value->TryGetStringField(TEXT("callee"), OutCallee)
        || !Value->TryGetObjectField(TEXT("args"), Args)
        || Args == nullptr
        || !(*Args).IsValid())
    {
        return false;
    }
    OutArgs = *Args;
    return true;
}

bool ReadStringArg(const TSharedPtr<FJsonObject>& Args, const TCHAR* Name, FString& OutValue)
{
    OutValue.Reset();
    return Args.IsValid() && Args->TryGetStringField(Name, OutValue) && !OutValue.IsEmpty();
}

bool ReadStringOrNameArg(const TSharedPtr<FJsonObject>& Args, const TCHAR* Name, FString& OutValue)
{
    if (ReadStringArg(Args, Name, OutValue))
    {
        return true;
    }
    const TSharedPtr<FJsonObject>* Expression = nullptr;
    FString Kind;
    OutValue.Reset();
    return Args.IsValid()
        && Args->TryGetObjectField(Name, Expression)
        && Expression != nullptr
        && (*Expression).IsValid()
        && (*Expression)->TryGetStringField(TEXT("kind"), Kind)
        && Kind == TEXT("name")
        && (*Expression)->TryGetStringField(TEXT("name"), OutValue)
        && !OutValue.IsEmpty();
}

UObject* LoadExactObject(const FString& InputPath)
{
    const FString ObjectPath = NormalizeObjectPath(InputPath);
    return LoadObject<UObject>(nullptr, *ObjectPath);
}

UClass* ResolveClassPath(const FString& Path)
{
    if (UClass* Existing = FindObject<UClass>(nullptr, *Path))
    {
        return Existing;
    }
    return LoadObject<UClass>(nullptr, *Path);
}
}

bool FSalTargetResolver::Resolve(
    const FString& Alias,
    const TSharedPtr<FJsonObject>& TargetValue,
    const bool bForPatch,
    FSalResolvedTarget& OutTarget,
    TSharedPtr<FJsonObject>& OutError) const
{
    OutTarget = FSalResolvedTarget();
    OutTarget.Alias = Alias;
    OutTarget.Locator = TargetValue;
    return ResolveValue(Alias, TargetValue, bForPatch, OutTarget, OutError);
}

bool FSalTargetResolver::ResolveValue(
    const FString& Alias,
    const TSharedPtr<FJsonObject>& Value,
    const bool bForPatch,
    FSalResolvedTarget& OutTarget,
    TSharedPtr<FJsonObject>& OutError) const
{
    FString Kind;
    if (!Value.IsValid() || !Value->TryGetStringField(TEXT("kind"), Kind))
    {
        OutError = InvalidTarget(TEXT("Target value has no kind."));
        return false;
    }
    if (Kind == TEXT("name"))
    {
        FString Name;
        Value->TryGetStringField(TEXT("name"), Name);
        if (Name != TEXT("asset") || bForPatch)
        {
            OutError = InvalidTarget(TEXT("Only query asset may use an unbound collection root."));
            return false;
        }
        OutTarget.Kind = ESalTargetKind::AssetRoot;
        OutTarget.Alias = Alias;
        OutTarget.Name = Name;
        OutTarget.Interfaces = {FName(TEXT("asset"))};
        return true;
    }

    FString Callee;
    TSharedPtr<FJsonObject> Args;
    if (!ReadCall(Value, Callee, Args))
    {
        OutError = InvalidTarget(TEXT("Target value must be a normalized Call or the asset root Name."));
        return false;
    }

    if (Callee == TEXT("asset"))
    {
        FString Path;
        if (!ReadStringArg(Args, TEXT("path"), Path))
        {
            OutError = InvalidTarget(TEXT("asset target requires one non-empty path locator argument."));
            return false;
        }
        UObject* Asset = LoadExactObject(Path);
        if (Asset == nullptr)
        {
            OutError = ResolutionError(
                FString::Printf(TEXT("Asset was not found: %s."), *Path),
                Path,
                TEXT("Run query asset with assets \"<name>\" to discover the exact path."));
            return false;
        }
        OutTarget.Kind = ESalTargetKind::Asset;
        OutTarget.Alias = Alias;
        OutTarget.AssetPath = Asset->GetPathName();
        OutTarget.Object = Asset;
        OutTarget.Package = Asset->GetOutermost();
        OutTarget.Interfaces = {FName(TEXT("asset"))};
        return true;
    }

    if (Callee == TEXT("blueprint"))
    {
        FString Path;
        const TSharedPtr<FJsonObject>* AssetCall = nullptr;
        if (!ReadStringArg(Args, TEXT("asset"), Path))
        {
            if (!Args->TryGetObjectField(TEXT("asset"), AssetCall)
                || AssetCall == nullptr
                || !(*AssetCall).IsValid())
            {
                OutError = InvalidTarget(
                    TEXT("blueprint target requires an Asset Path string or nested asset(...) locator and accepts an optional id."));
                return false;
            }
            FSalResolvedTarget AssetOwner;
            if (!ResolveValue(Alias, *AssetCall, bForPatch, AssetOwner, OutError))
            {
                return false;
            }
            if (AssetOwner.Kind != ESalTargetKind::Asset)
            {
                OutError = InvalidTarget(TEXT("blueprint asset must resolve through asset(...)."));
                return false;
            }
            Path = AssetOwner.AssetPath;
        }
        FString ExpectedId;
        if (Args->HasField(TEXT("id")) && !ReadStringArg(Args, TEXT("id"), ExpectedId))
        {
            OutError = InvalidTarget(TEXT("blueprint id must be a non-empty string when present."));
            return false;
        }
        if (bForPatch && ExpectedId.IsEmpty())
        {
            OutError = InvalidTarget(TEXT("Blueprint Patch requires the BlueprintGuid returned by Query as id."));
            return false;
        }
        UBlueprint* Blueprint = Cast<UBlueprint>(LoadExactObject(Path));
        if (Blueprint == nullptr)
        {
            OutError = ResolutionError(
                FString::Printf(TEXT("Blueprint was not found: %s."), *Path),
                Path,
                TEXT("Run an Asset query before binding the Blueprint."));
            return false;
        }
        const FString ActualId = GuidText(Blueprint->GetBlueprintGuid());
        if (!ExpectedId.IsEmpty() && !ExpectedId.Equals(ActualId, ESearchCase::IgnoreCase))
        {
            OutError = ResolutionError(
                FString::Printf(TEXT("Blueprint id mismatch for %s."), *Blueprint->GetPathName()),
                ExpectedId,
                TEXT("Query the Blueprint summary again and use its current id."));
            return false;
        }
        OutTarget.Kind = ESalTargetKind::Blueprint;
        OutTarget.Alias = Alias;
        OutTarget.AssetPath = Blueprint->GetPathName();
        OutTarget.Id = ActualId;
        OutTarget.Name = Blueprint->GetName();
        OutTarget.Object = Blueprint;
        OutTarget.Package = Blueprint->GetOutermost();
        OutTarget.Blueprint = Blueprint;
        OutTarget.Interfaces = {FName(TEXT("blueprint"))};
        if (Blueprint->IsA<UWidgetBlueprint>())
        {
            OutTarget.Interfaces.Add(FName(TEXT("widget")));
        }
        return true;
    }

    if (Callee == TEXT("class"))
    {
        FString Path;
        if (!ReadStringArg(Args, TEXT("path"), Path))
        {
            OutError = InvalidTarget(TEXT("class target requires one non-empty path locator argument."));
            return false;
        }
        UClass* Class = ResolveClassPath(Path);
        if (Class == nullptr)
        {
            OutError = ResolutionError(
                FString::Printf(TEXT("Class was not found: %s."), *Path),
                Path,
                TEXT("Use an exact native or generated Class Path."));
            return false;
        }
        OutTarget.Kind = ESalTargetKind::Class;
        OutTarget.Alias = Alias;
        UBlueprint* GeneratingBlueprint = nullptr;
        if (const UBlueprintGeneratedClass* GeneratedClass = Cast<UBlueprintGeneratedClass>(Class))
        {
            GeneratingBlueprint = Cast<UBlueprint>(GeneratedClass->ClassGeneratedBy);
        }
        OutTarget.AssetPath = GeneratingBlueprint != nullptr ? GeneratingBlueprint->GetPathName() : FString();
        OutTarget.Name = Class->GetName();
        OutTarget.Object = Class;
        OutTarget.Package = GeneratingBlueprint != nullptr ? GeneratingBlueprint->GetOutermost() : Class->GetOutermost();
        OutTarget.Class = Class;
        OutTarget.Interfaces = {FName(TEXT("class"))};
        return true;
    }

    if (Callee == TEXT("graph"))
    {
        const TSharedPtr<FJsonObject>* OwnerCall = nullptr;
        if (!Args->TryGetObjectField(TEXT("asset"), OwnerCall)
            || OwnerCall == nullptr
            || !(*OwnerCall).IsValid())
        {
            OutError = InvalidTarget(TEXT("graph target requires one nested asset-backed owner and at least one of id or name."));
            return false;
        }
        FSalResolvedTarget Owner;
        if (!ResolveValue(Alias, *OwnerCall, bForPatch, Owner, OutError))
        {
            return false;
        }
        UBlueprint* Blueprint = Owner.Kind == ESalTargetKind::Blueprint ? Owner.Blueprint : nullptr;
        if (Blueprint == nullptr)
        {
            OutError = InvalidTarget(
                TEXT("graph asset must be a nested blueprint(...) locator; a bare asset(...) path does not carry Blueprint identity."));
            return false;
        }
        FString Id;
        FString Name;
        if (Args->HasField(TEXT("id")) && !ReadStringArg(Args, TEXT("id"), Id))
        {
            OutError = InvalidTarget(TEXT("graph id must be a non-empty string when present."));
            return false;
        }
        if (Args->HasField(TEXT("name")) && !ReadStringOrNameArg(Args, TEXT("name"), Name))
        {
            OutError = InvalidTarget(TEXT("graph name must be a non-empty string when present."));
            return false;
        }
        if (Id.IsEmpty() && Name.IsEmpty())
        {
            OutError = InvalidTarget(TEXT("graph target requires at least one of id or name."));
            return false;
        }
        if (bForPatch && Id.IsEmpty())
        {
            OutError = InvalidTarget(
                TEXT("graph Patch target requires its stable id; use an exact-name Query only for discovery, then reuse the returned Graph id."));
            return false;
        }
        TArray<UEdGraph*> Graphs;
        Blueprint->GetAllGraphs(Graphs);
        UEdGraph* Graph = nullptr;
        for (UEdGraph* Candidate : Graphs)
        {
            if (Candidate == nullptr)
            {
                continue;
            }
            const bool bMatches = !Id.IsEmpty()
                ? GuidText(Candidate->GraphGuid).Equals(Id, ESearchCase::IgnoreCase)
                : Candidate->GetName() == Name;
            if (bMatches)
            {
                if (Graph != nullptr)
                {
                    OutError = ResolutionError(TEXT("Graph locator is ambiguous in its Blueprint owner."), !Id.IsEmpty() ? Id : Name);
                    return false;
                }
                Graph = Candidate;
            }
        }
        if (Graph == nullptr)
        {
            OutError = ResolutionError(
                TEXT("Graph was not found in its Blueprint owner."),
                !Id.IsEmpty() ? Id : Name,
                TEXT("Query the Blueprint graphs collection and reuse the returned Graph id."));
            return false;
        }
        if (!Name.IsEmpty() && Graph->GetName() != Name)
        {
            OutError = ResolutionError(
                FString::Printf(TEXT("Graph name mismatch for id %s."), *Id),
                Name,
                TEXT("Query the Blueprint graphs collection again and reuse the current Graph binding."));
            return false;
        }
        OutTarget.Kind = ESalTargetKind::Graph;
        OutTarget.Alias = Alias;
        OutTarget.AssetPath = Blueprint->GetPathName();
        OutTarget.Id = GuidText(Graph->GraphGuid);
        OutTarget.Name = Graph->GetName();
        OutTarget.Object = Graph;
        OutTarget.Package = Blueprint->GetOutermost();
        OutTarget.Blueprint = Blueprint;
        OutTarget.Graph = Graph;
        OutTarget.Interfaces = {FName(TEXT("graph"))};
        return true;
    }

    OutError = FSalDiagnostics::Result(
        FSalDiagnostics::Error(TEXT("capability.interface_unavailable"), FString::Printf(TEXT("Unknown target constructor %s."), *Callee))
            .Actual(Callee)
            .Supported({TEXT("asset"), TEXT("blueprint"), TEXT("class"), TEXT("graph")})
            .Suggestion(TEXT("Run sal_schema({}) to inspect active target locators."))
            .Build());
    return false;
}
}
