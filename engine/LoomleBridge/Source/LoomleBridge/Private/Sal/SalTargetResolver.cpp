// Copyright 2026 Loomle contributors.

#include "SalTargetResolver.h"

#include "AssetRegistry/AssetData.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "BlueprintAssetHandler.h"
#include "Blueprint/UserWidgetBlueprint.h"
#include "Dom/JsonObject.h"
#include "EdGraph/EdGraph.h"
#include "Editor.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Engine/Level.h"
#include "Engine/World.h"
#include "Misc/PackageName.h"
#include "Modules/ModuleManager.h"
#include "PCGGraph.h"
#include "PCGComponent.h"
#include "Level/SalLevelInterface.h"
#include "SalDiagnostics.h"
#include "StateTree.h"
#include "UObject/SoftObjectPath.h"
#include "UObject/UObjectGlobals.h"
#include "WidgetBlueprint.h"

namespace Loomle::Sal
{
namespace
{
constexpr EObjectFlags IncompleteLoadFlags =
    RF_NeedLoad
    | RF_NeedPostLoad
    | RF_NeedPostLoadSubobjects
    | RF_WillBeLoaded;

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
        FSalDiagnostics::Error(TEXT("validation.invalid_target"), Message)
            .Path({TEXT("object"), TEXT("target"), TEXT("value")})
            .Build());
}

TSharedPtr<FJsonObject> UnavailableDomain(const FString& Domain)
{
    return FSalDiagnostics::Result(
        FSalDiagnostics::Error(
            TEXT("capability.interface_unavailable"),
            FString::Printf(
                TEXT("The %s Domain is recognized but its native adapter is not available in this Bridge build."),
                *Domain))
            .Interface(Domain)
            .Suggestion(TEXT("Use a published Domain capability or install a Bridge build that includes this adapter."))
            .Build());
}

TSharedPtr<FJsonObject> PcgComponentResolutionError(
    const FString& Code,
    const FString& Message,
    const FString& Ref)
{
    return FSalDiagnostics::Result(
        FSalDiagnostics::Error(Code, Message)
            .Interface(TEXT("pcg_component"))
            .Path({TEXT("object"), TEXT("target")})
            .Ref(Ref)
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

TSharedPtr<FJsonObject> MakeCall(
    const FString& Callee,
    const TSharedPtr<FJsonObject>& Args)
{
    TSharedPtr<FJsonObject> Call = MakeShared<FJsonObject>();
    Call->SetStringField(TEXT("kind"), TEXT("call"));
    Call->SetStringField(TEXT("callee"), Callee);
    Call->SetObjectField(TEXT("args"), Args);
    return Call;
}

TSharedPtr<FJsonObject> MakeCanonicalTarget(const FString& Domain)
{
    TSharedPtr<FJsonObject> Target = MakeShared<FJsonObject>();
    Target->SetStringField(TEXT("kind"), TEXT("target"));
    Target->SetStringField(TEXT("domain"), Domain);
    return Target;
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
    FString Kind;
    if (TargetValue.IsValid()
        && TargetValue->TryGetStringField(TEXT("kind"), Kind)
        && Kind == TEXT("target"))
    {
        return ResolveTarget(Alias, TargetValue, bForPatch, OutTarget, OutError);
    }
    OutError = InvalidTarget(
        TEXT("Bridge protocol v3 accepts only normalized target { domain: ... } values; legacy Target call shapes must be lowered before RPC."));
    return false;
}

bool FSalTargetResolver::ResolveTarget(
    const FString& Alias,
    const TSharedPtr<FJsonObject>& Target,
    const bool bForPatch,
    FSalResolvedTarget& OutTarget,
    TSharedPtr<FJsonObject>& OutError) const
{
    FString Domain;
    Target->TryGetStringField(TEXT("domain"), Domain);

    if (Domain == TEXT("asset"))
    {
        FString Path;
        if (!Target->TryGetStringField(TEXT("path"), Path))
        {
            if (bForPatch)
            {
                OutError = InvalidTarget(TEXT("Asset collection root cannot be patched."));
                return false;
            }
            OutTarget.Kind = ESalTargetKind::AssetRoot;
            OutTarget.Domain = ESalDomain::Asset;
            OutTarget.bDomainRoot = true;
            OutTarget.Alias = Alias;
            OutTarget.Name = TEXT("asset");
            OutTarget.Interfaces = {FName(TEXT("asset"))};
            OutTarget.CanonicalTarget = MakeCanonicalTarget(TEXT("asset"));
            return true;
        }
        TSharedPtr<FJsonObject> Args = MakeShared<FJsonObject>();
        Args->SetStringField(TEXT("path"), Path);
        if (!ResolveValue(Alias, MakeCall(TEXT("asset"), Args), bForPatch, OutTarget, OutError))
        {
            return false;
        }
        FString ExpectedType;
        Target->TryGetStringField(TEXT("type"), ExpectedType);
        const FString ActualType =
            OutTarget.Object != nullptr && OutTarget.Object->GetClass() != nullptr
                ? OutTarget.Object->GetClass()->GetPathName()
                : FString();
        if (!ExpectedType.IsEmpty() && ExpectedType != ActualType)
        {
            OutError = InvalidTarget(FString::Printf(
                TEXT("Asset target type %s does not match resolved native Class %s."),
                *ExpectedType,
                *ActualType));
            return false;
        }
        OutTarget.Domain = ESalDomain::Asset;
        OutTarget.Interfaces = {FName(TEXT("asset"))};
        OutTarget.CanonicalTarget = MakeCanonicalTarget(TEXT("asset"));
        OutTarget.CanonicalTarget->SetStringField(TEXT("path"), OutTarget.AssetPath);
        OutTarget.CanonicalTarget->SetStringField(TEXT("type"), ActualType);
        return true;
    }

    if (Domain == TEXT("blueprint") || Domain == TEXT("widget"))
    {
        FString Asset;
        FString Id;
        Target->TryGetStringField(TEXT("asset"), Asset);
        Target->TryGetStringField(TEXT("id"), Id);
        TSharedPtr<FJsonObject> Args = MakeShared<FJsonObject>();
        Args->SetStringField(TEXT("asset"), Asset);
        if (!Id.IsEmpty())
        {
            Args->SetStringField(TEXT("id"), Id);
        }
        if (!ResolveValue(Alias, MakeCall(TEXT("blueprint"), Args), bForPatch, OutTarget, OutError))
        {
            return false;
        }
        if (Domain == TEXT("widget") && !OutTarget.Blueprint->IsA<UWidgetBlueprint>())
        {
            OutError = FSalDiagnostics::Result(
                FSalDiagnostics::Error(
                    TEXT("capability.interface_unavailable"),
                    TEXT("Widget Domain requires a UWidgetBlueprint target."))
                    .Ref(OutTarget.AssetPath)
                    .Build());
            return false;
        }
        OutTarget.Domain =
            Domain == TEXT("widget") ? ESalDomain::Widget : ESalDomain::Blueprint;
        OutTarget.Interfaces = {
            FName(Domain == TEXT("widget") ? TEXT("widget") : TEXT("blueprint"))};
        OutTarget.CanonicalTarget = MakeCanonicalTarget(Domain);
        OutTarget.CanonicalTarget->SetStringField(TEXT("asset"), OutTarget.AssetPath);
        OutTarget.CanonicalTarget->SetStringField(TEXT("id"), OutTarget.Id);
        return true;
    }

    if (Domain == TEXT("class"))
    {
        FString Path;
        Target->TryGetStringField(TEXT("path"), Path);
        TSharedPtr<FJsonObject> Args = MakeShared<FJsonObject>();
        Args->SetStringField(TEXT("path"), Path);
        if (!ResolveValue(Alias, MakeCall(TEXT("class"), Args), bForPatch, OutTarget, OutError))
        {
            return false;
        }
        OutTarget.Domain = ESalDomain::Class;
        OutTarget.Interfaces = {FName(TEXT("class"))};
        OutTarget.CanonicalTarget = MakeCanonicalTarget(TEXT("class"));
        OutTarget.CanonicalTarget->SetStringField(
            TEXT("path"),
            OutTarget.Class != nullptr ? OutTarget.Class->GetPathName() : Path);
        return true;
    }

    if (Domain == TEXT("state_tree"))
    {
        FString Asset;
        FString ExpectedType;
        Target->TryGetStringField(TEXT("asset"), Asset);
        Target->TryGetStringField(TEXT("type"), ExpectedType);
        TSharedPtr<FJsonObject> Args = MakeShared<FJsonObject>();
        Args->SetStringField(TEXT("path"), Asset);
        if (!ExpectedType.IsEmpty())
        {
            Args->SetStringField(TEXT("type"), ExpectedType);
        }
        if (!ResolveValue(Alias, MakeCall(TEXT("asset"), Args), bForPatch, OutTarget, OutError))
        {
            return false;
        }
        UStateTree* StateTree = Cast<UStateTree>(OutTarget.Object);
        if (StateTree == nullptr)
        {
            OutError = FSalDiagnostics::Result(
                FSalDiagnostics::Error(
                    TEXT("capability.interface_unavailable"),
                    TEXT("StateTree Domain requires a UStateTree target."))
                    .Ref(OutTarget.AssetPath)
                    .Build());
            return false;
        }
        OutTarget.Domain = ESalDomain::StateTree;
        OutTarget.Interfaces = {FName(TEXT("state_tree"))};
        OutTarget.CanonicalTarget = MakeCanonicalTarget(TEXT("state_tree"));
        OutTarget.CanonicalTarget->SetStringField(TEXT("asset"), OutTarget.AssetPath);
        OutTarget.CanonicalTarget->SetStringField(
            TEXT("type"),
            StateTree->GetClass()->GetPathName());
        return true;
    }

    if (Domain == TEXT("graph"))
    {
        FString Asset;
        FString BlueprintId;
        FString Id;
        FString Name;
        Target->TryGetStringField(TEXT("asset"), Asset);
        Target->TryGetStringField(TEXT("blueprintId"), BlueprintId);
        Target->TryGetStringField(TEXT("id"), Id);
        Target->TryGetStringField(TEXT("name"), Name);

        TSharedPtr<FJsonObject> BlueprintArgs = MakeShared<FJsonObject>();
        BlueprintArgs->SetStringField(TEXT("asset"), Asset);
        if (!BlueprintId.IsEmpty())
        {
            BlueprintArgs->SetStringField(TEXT("id"), BlueprintId);
        }
        TSharedPtr<FJsonObject> GraphArgs = MakeShared<FJsonObject>();
        GraphArgs->SetObjectField(
            TEXT("asset"),
            MakeCall(TEXT("blueprint"), BlueprintArgs));
        if (!Id.IsEmpty())
        {
            GraphArgs->SetStringField(TEXT("id"), Id);
        }
        if (!Name.IsEmpty())
        {
            GraphArgs->SetStringField(TEXT("name"), Name);
        }
        if (!ResolveValue(Alias, MakeCall(TEXT("graph"), GraphArgs), bForPatch, OutTarget, OutError))
        {
            return false;
        }
        OutTarget.Domain = ESalDomain::Graph;
        OutTarget.Interfaces = {FName(TEXT("graph"))};
        OutTarget.CanonicalTarget = MakeCanonicalTarget(TEXT("graph"));
        OutTarget.CanonicalTarget->SetStringField(TEXT("asset"), OutTarget.AssetPath);
        OutTarget.CanonicalTarget->SetStringField(
            TEXT("blueprintId"),
            GuidText(OutTarget.Blueprint->GetBlueprintGuid()));
        OutTarget.CanonicalTarget->SetStringField(TEXT("id"), OutTarget.Id);
        return true;
    }

    if (Domain == TEXT("pcg"))
    {
        FString Asset;
        FString ExpectedType;
        Target->TryGetStringField(TEXT("asset"), Asset);
        Target->TryGetStringField(TEXT("type"), ExpectedType);

        const FSoftObjectPath RequestedPath(
            NormalizeObjectPath(Asset));
        if (!RequestedPath.IsValid()
            || !RequestedPath.GetSubPathString().IsEmpty())
        {
            OutError = InvalidTarget(
                TEXT("PCG Target asset must be one exact top-level object path."));
            return false;
        }

        const FAssetRegistryModule& AssetRegistryModule =
            FModuleManager::LoadModuleChecked<FAssetRegistryModule>(
                TEXT("AssetRegistry"));
        const FAssetData AssetData =
            AssetRegistryModule.Get().GetAssetByObjectPath(
                RequestedPath,
                true);
        if (!AssetData.IsValid()
            || !AssetData.IsTopLevelAsset())
        {
            OutError = ResolutionError(
                FString::Printf(
                    TEXT("Saved top-level PCG Graph asset was not found: %s."),
                    *RequestedPath.ToString()),
                RequestedPath.ToString(),
                TEXT("Save the UPCGGraph as a standalone asset, then query its exact Object Path."));
            return false;
        }

        const FSoftObjectPath CanonicalSoftPath =
            AssetData.GetSoftObjectPath();
        const FString CanonicalPath =
            CanonicalSoftPath.ToString();
        UClass* RegisteredClass =
            FindObject<UClass>(AssetData.AssetClassPath);
        if (!CanonicalSoftPath.IsValid()
            || !CanonicalSoftPath.GetSubPathString().IsEmpty()
            || CanonicalPath.IsEmpty()
            || !IsValid(RegisteredClass)
            || !RegisteredClass->IsChildOf(UPCGGraph::StaticClass()))
        {
            OutError = FSalDiagnostics::Result(
                FSalDiagnostics::Error(
                    TEXT("capability.interface_unavailable"),
                    TEXT("The saved top-level asset is not a loaded native UPCGGraph Class and cannot be opened through the PCG Domain."))
                    .Ref(CanonicalPath.IsEmpty()
                        ? RequestedPath.ToString()
                        : CanonicalPath)
                    .Build());
            return false;
        }

        const FString RegisteredType =
            AssetData.AssetClassPath.ToString();
        if (!ExpectedType.IsEmpty()
            && ExpectedType != RegisteredType)
        {
            OutError = InvalidTarget(FString::Printf(
                TEXT("PCG target type %s does not match registered native Class %s."),
                *ExpectedType,
                *RegisteredType));
            return false;
        }

        UPCGGraph* Graph =
            LoadObject<UPCGGraph>(nullptr, *CanonicalPath);
        if (!IsValid(Graph)
            || Graph->HasAnyFlags(IncompleteLoadFlags)
            || Graph->GetPathName() != CanonicalPath
            || Graph->GetClass() != RegisteredClass
            || Graph->GetOutermost() == GetTransientPackage()
            || Graph->GetOutermost()->HasAnyFlags(RF_Transient)
            || Graph->GetOutermost()->HasAnyPackageFlags(PKG_PlayInEditor))
        {
            OutError = ResolutionError(
                FString::Printf(
                    TEXT("The saved PCG Graph could not be opened as its exact registered object: %s."),
                    *CanonicalPath),
                CanonicalPath,
                TEXT("Refresh the Asset Registry and query the exact saved UPCGGraph again."));
            return false;
        }

        if (!Graph->IsAsset() || Graph->GetTypedOuter<UPCGGraph>() != nullptr)
        {
            OutError = InvalidTarget(
                TEXT("PCG Domain rejects embedded or instance-owned Graph objects; open an independently saved UPCGGraph asset."));
            return false;
        }

        const FString ActualType = Graph->GetClass()->GetPathName();
        if (ActualType != RegisteredType)
        {
            OutError = InvalidTarget(FString::Printf(
                TEXT("PCG registered native Class %s does not match resolved native Class %s."),
                *RegisteredType,
                *ActualType));
            return false;
        }

        OutTarget.Kind = ESalTargetKind::Asset;
        OutTarget.Alias = Alias;
        OutTarget.AssetPath = CanonicalPath;
        OutTarget.Name = AssetData.AssetName.ToString();
        OutTarget.Object = Graph;
        OutTarget.Package = Graph->GetOutermost();
        OutTarget.Domain = ESalDomain::Pcg;
        OutTarget.Interfaces = {FName(TEXT("pcg"))};
        OutTarget.CanonicalTarget = MakeCanonicalTarget(TEXT("pcg"));
        OutTarget.CanonicalTarget->SetStringField(TEXT("asset"), OutTarget.AssetPath);
        OutTarget.CanonicalTarget->SetStringField(TEXT("type"), ActualType);
        return true;
    }

    if (Domain == TEXT("level"))
    {
        FString Asset;
        FString ExpectedType;
        Target->TryGetStringField(TEXT("asset"), Asset);
        Target->TryGetStringField(TEXT("type"), ExpectedType);

        const FSoftObjectPath RequestedPath(
            NormalizeObjectPath(Asset));
        if (!RequestedPath.IsValid()
            || !RequestedPath.GetSubPathString().IsEmpty())
        {
            OutError = InvalidTarget(
                TEXT("Level Target asset must be one exact top-level World object path."));
            return false;
        }
        const FAssetRegistryModule& AssetRegistryModule =
            FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
        const FAssetData AssetData = AssetRegistryModule.Get().GetAssetByObjectPath(
            RequestedPath,
            true);
        const FSoftObjectPath CanonicalSoftPath =
            AssetData.GetSoftObjectPath();
        const FTopLevelAssetPath CanonicalAssetPath =
            CanonicalSoftPath.GetAssetPath();
        const FString PackageName = AssetData.PackageName.ToString();
        if (!AssetData.IsValid()
            || !AssetData.IsTopLevelAsset()
            || !CanonicalSoftPath.IsValid()
            || !CanonicalSoftPath.GetSubPathString().IsEmpty()
            || AssetData.PackageName != CanonicalAssetPath.GetPackageName()
            || AssetData.AssetName != CanonicalAssetPath.GetAssetName()
            || !FPackageName::IsValidLongPackageName(PackageName)
            || FPackageName::IsTempPackage(PackageName))
        {
            OutError = ResolutionError(
                TEXT("Level Domain requires one registered saved source-map asset and never loads a map to resolve it."),
                RequestedPath.ToString(),
                TEXT("Save the source map, then retry its exact top-level object path."));
            return false;
        }

        const FString ActualType = AssetData.AssetClassPath.ToString();
        const FString WorldType = UWorld::StaticClass()->GetClassPathName().ToString();
        if (ActualType != WorldType)
        {
            OutError = ResolutionError(
                FString::Printf(
                    TEXT("Level target %s resolves to native Class %s instead of %s."),
                    *RequestedPath.ToString(),
                    *ActualType,
                    *WorldType),
                RequestedPath.ToString());
            return false;
        }
        if (!ExpectedType.IsEmpty() && ExpectedType != ActualType)
        {
            OutError = InvalidTarget(FString::Printf(
                TEXT("Level target type %s does not match resolved native Class %s."),
                *ExpectedType,
                *ActualType));
            return false;
        }

        const FString CanonicalPath = CanonicalSoftPath.ToString();
        UWorld* LoadedWorld = FindObject<UWorld>(
            CanonicalAssetPath);
        if (!IsValid(LoadedWorld)
            || LoadedWorld->HasAnyFlags(IncompleteLoadFlags))
        {
            LoadedWorld = nullptr;
        }
        UWorld* EditorWorld = GEditor != nullptr
            ? GEditor->GetEditorWorldContext().World()
            : nullptr;
        ULevel* SourceLevel = LoadedWorld != nullptr
            ? LoadedWorld->PersistentLevel
            : nullptr;
        if (LoadedWorld != nullptr
            && (!IsValid(EditorWorld)
                || EditorWorld->HasAnyFlags(IncompleteLoadFlags)
                || EditorWorld->WorldType != EWorldType::Editor
                || (LoadedWorld->WorldType != EWorldType::Editor
                    && LoadedWorld->WorldType != EWorldType::Inactive)
                || !IsValid(SourceLevel)
                || SourceLevel->HasAnyFlags(IncompleteLoadFlags)
                || SourceLevel->GetWorld() != EditorWorld))
        {
            LoadedWorld = nullptr;
        }

        OutTarget.Kind = ESalTargetKind::Asset;
        OutTarget.Domain = ESalDomain::Level;
        OutTarget.AssetPath = CanonicalPath;
        OutTarget.Name = AssetData.AssetName.ToString();
        OutTarget.Object = LoadedWorld;
        OutTarget.Package = LoadedWorld != nullptr ? LoadedWorld->GetOutermost() : nullptr;
        OutTarget.Interfaces = {FName(TEXT("level"))};
        OutTarget.CanonicalTarget = MakeCanonicalTarget(TEXT("level"));
        OutTarget.CanonicalTarget->SetStringField(TEXT("asset"), CanonicalPath);
        OutTarget.CanonicalTarget->SetStringField(TEXT("type"), ActualType);
        return true;
    }

    if (Domain == TEXT("pcg_component"))
    {
        FString Asset;
        FString ActorId;
        FString Source;
        FString Id;
        FString ExpectedType;
        if (!Target->TryGetStringField(TEXT("asset"), Asset)
            || !Target->TryGetStringField(TEXT("actorId"), ActorId)
            || !Target->TryGetStringField(TEXT("source"), Source)
            || !Target->TryGetStringField(TEXT("id"), Id)
            || !Target->TryGetStringField(TEXT("type"), ExpectedType))
        {
            OutError = InvalidTarget(
                TEXT("pcg_component Target requires asset, actorId, source, id, and type."));
            return false;
        }

        TSharedPtr<FJsonObject> LevelTargetValue =
            MakeCanonicalTarget(TEXT("level"));
        LevelTargetValue->SetStringField(TEXT("asset"), Asset);
        LevelTargetValue->SetStringField(
            TEXT("type"),
            UWorld::StaticClass()->GetPathName());
        FSalResolvedTarget LevelTarget;
        if (!ResolveTarget(
                Alias,
                LevelTargetValue,
                false,
                LevelTarget,
                OutError))
        {
            return false;
        }

        UActorComponent* Component = nullptr;
        FString CanonicalActorId;
        FString CanonicalSource;
        FString CanonicalId;
        FString Name;
        FString ActualType;
        FString CreationMethod;
        FString DeclaringClass;
        FString Code;
        FString Message;
        if (!FSalLevelInterface::ResolveExactComponent(
                LevelTarget,
                ActorId,
                Source,
                Id,
                Component,
                CanonicalActorId,
                CanonicalSource,
                CanonicalId,
                Name,
                ActualType,
                CreationMethod,
                DeclaringClass,
                Code,
                Message))
        {
            OutError = PcgComponentResolutionError(
                Code.IsEmpty()
                    ? TEXT("resolution.object_not_found")
                    : Code,
                Message.IsEmpty()
                    ? TEXT("The exact persistent Component could not be resolved without loading authored state.")
                    : Message,
                ActorId + TEXT("/") + Source + TEXT("/") + Id);
            return false;
        }

        UPCGComponent* PCGComponent = Cast<UPCGComponent>(Component);
        if (PCGComponent == nullptr
            || PCGComponent->IsLocalComponent()
            || PCGComponent->GetConstOriginalComponent() != PCGComponent)
        {
            OutError = PcgComponentResolutionError(
                TEXT("capability.interface_unavailable"),
                TEXT("pcg_component Target requires one original authored UPCGComponent, not a generic, local, generated, or cleanup projection Component."),
                CanonicalId);
            return false;
        }
        if (ExpectedType != ActualType)
        {
            OutError = InvalidTarget(FString::Printf(
                TEXT("pcg_component target type %s does not match resolved native Class %s."),
                *ExpectedType,
                *ActualType));
            return false;
        }

        OutTarget.Kind = ESalTargetKind::Asset;
        OutTarget.Domain = ESalDomain::PcgComponent;
        OutTarget.Alias = Alias;
        OutTarget.AssetPath = LevelTarget.AssetPath;
        OutTarget.Id = CanonicalId;
        OutTarget.Name = Name;
        OutTarget.Object = PCGComponent;
        OutTarget.Package = PCGComponent->GetOutermost();
        OutTarget.Interfaces = {FName(TEXT("pcg_component"))};
        OutTarget.CanonicalTarget = MakeCanonicalTarget(
            TEXT("pcg_component"));
        OutTarget.CanonicalTarget->SetStringField(
            TEXT("asset"),
            LevelTarget.AssetPath);
        OutTarget.CanonicalTarget->SetStringField(
            TEXT("actorId"),
            CanonicalActorId);
        OutTarget.CanonicalTarget->SetStringField(
            TEXT("source"),
            CanonicalSource);
        OutTarget.CanonicalTarget->SetStringField(
            TEXT("id"),
            CanonicalId);
        OutTarget.CanonicalTarget->SetStringField(
            TEXT("type"),
            ActualType);
        return true;
    }

    OutError = InvalidTarget(TEXT("Unknown Target domain."));
    return false;
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
            OutError = InvalidTarget(TEXT("Asset Target requires one non-empty path."));
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
        if (Asset->IsA<UStateTree>())
        {
            FString ExpectedType;
            if (Args->HasField(TEXT("type"))
                && (!ReadStringArg(Args, TEXT("type"), ExpectedType)
                    || ExpectedType != Asset->GetClass()->GetPathName()))
            {
                OutError = InvalidTarget(FString::Printf(
                    TEXT("StateTree target type must exactly match the resolved native Class %s."),
                    *Asset->GetClass()->GetPathName()));
                return false;
            }
            OutTarget.Interfaces.Add(FName(TEXT("state_tree")));
        }
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
                    TEXT("Blueprint Target requires a non-empty asset field and accepts an optional id."));
                return false;
            }
            FSalResolvedTarget AssetOwner;
            if (!ResolveValue(Alias, *AssetCall, bForPatch, AssetOwner, OutError))
            {
                return false;
            }
            if (AssetOwner.Kind != ESalTargetKind::Asset)
            {
                OutError = InvalidTarget(TEXT("Blueprint Target asset did not resolve to an exact native container."));
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
        UObject* BlueprintContainer = LoadExactObject(Path);
        if (BlueprintContainer == nullptr)
        {
            OutError = ResolutionError(
                FString::Printf(TEXT("Blueprint container was not found: %s."), *Path),
                Path,
                TEXT("Run an Asset query before binding the Blueprint."));
            return false;
        }
        UBlueprint* Blueprint = Cast<UBlueprint>(BlueprintContainer);
        if (Blueprint == nullptr)
        {
            const IBlueprintAssetHandler* Handler =
                FBlueprintAssetHandler::Get().FindHandler(BlueprintContainer->GetClass());
            if (Handler == nullptr)
            {
                OutError = FSalDiagnostics::Result(
                    FSalDiagnostics::Error(
                        TEXT("capability.interface_unavailable"),
                        FString::Printf(
                            TEXT("Asset type %s has no active Blueprint container provider."),
                            *BlueprintContainer->GetClass()->GetPathName()))
                        .Ref(Path)
                        .Suggestion(TEXT("Bind a Blueprint asset, or enable the editor module that owns this container type."))
                        .Build());
                return false;
            }
            Blueprint = Handler->RetrieveBlueprint(BlueprintContainer);
        }
        if (Blueprint == nullptr)
        {
            OutError = ResolutionError(
                FString::Printf(TEXT("The Blueprint container has no retrievable Blueprint in its current authored state: %s."), *Path),
                Path,
                TEXT("Open or author the embedded Blueprint, then query the container again."));
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
        // A Blueprint may be embedded in another top-level asset (for example,
        // a World Level Script or a Level Sequence Director Blueprint). Keep
        // the public Target anchored to that durable container asset.
        OutTarget.AssetPath = BlueprintContainer->GetPathName();
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
            OutError = InvalidTarget(TEXT("Class Target requires one non-empty path."));
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
                TEXT("Graph Target asset did not resolve to an exact Blueprint owner."));
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
                    OutError = ResolutionError(TEXT("Graph Target is ambiguous in its Blueprint owner."), !Id.IsEmpty() ? Id : Name);
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
        OutTarget.AssetPath = Owner.AssetPath;
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
        FSalDiagnostics::Error(TEXT("capability.interface_unavailable"), FString::Printf(TEXT("Unsupported Target Domain %s."), *Callee))
            .Actual(Callee)
            .Supported({TEXT("asset"), TEXT("blueprint"), TEXT("class"), TEXT("graph")})
            .Suggestion(TEXT("Run sal_schema({}) to inspect active Targets."))
            .Build());
    return false;
}
}
