// Copyright 2026 Loomle contributors.

#include "SalPCGComponentInterface.h"

#include "../SalDiagnostics.h"
#include "../SalObjectBuilder.h"
#include "../SalResultTargets.h"
#include "AssetRegistry/AssetData.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Components/ActorComponent.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "GameFramework/Actor.h"
#include "Misc/PackageName.h"
#include "Modules/ModuleManager.h"
#include "PCGComponent.h"
#include "PCGGraph.h"
#include "UObject/Package.h"

namespace Loomle::Sal
{
namespace
{
constexpr int32 MaxGraphInterfaceDepth = 32;
constexpr int32 MaxGraphBindingStringChars = 64 * 1024;
constexpr EObjectFlags IncompleteLoadFlags =
    RF_NeedLoad
    | RF_NeedPostLoad
    | RF_NeedPostLoadSubobjects
    | RF_WillBeLoaded;

struct FGraphBindingSnapshot
{
    UPCGGraphInterface* Direct = nullptr;
    UPCGGraph* TopGraph = nullptr;
    FString Kind = TEXT("none");
    bool bComplete = true;
    TSharedPtr<FJsonObject> PcgTarget;
    TArray<TSharedPtr<FJsonObject>> Diagnostics;
};

TSharedPtr<FJsonObject> QueryError(
    const FString& Code,
    const FString& Message,
    const FString& Operation,
    const FString& Ref = FString(),
    const TArray<FString>& Supported = {})
{
    FSalDiagnosticBuilder Diagnostic = FSalDiagnostics::Error(Code, Message)
        .Interface(TEXT("pcg_component"))
        .Operation(Operation);
    if (!Ref.IsEmpty())
    {
        Diagnostic.Ref(Ref);
    }
    if (!Supported.IsEmpty())
    {
        Diagnostic.Supported(Supported);
    }
    return FSalDiagnostics::Result(Diagnostic.Build());
}

TSharedPtr<FJsonObject> Warning(
    const FString& Message,
    const FString& Operation,
    const FString& Ref = FString())
{
    FSalDiagnosticBuilder Diagnostic = FSalDiagnostics::Warning(
            TEXT("validation.reference_scan_incomplete"),
            Message)
        .Interface(TEXT("pcg_component"))
        .Operation(Operation);
    if (!Ref.IsEmpty())
    {
        Diagnostic.Ref(Ref);
    }
    return Diagnostic.Build();
}

UPCGComponent* ResolvedComponent(const FSalResolvedTarget& Target)
{
    UPCGComponent* Component = Target.Domain == ESalDomain::PcgComponent
        ? Cast<UPCGComponent>(Target.Object)
        : nullptr;
    return IsValid(Component)
            && !Component->HasAnyFlags(IncompleteLoadFlags)
            && !Component->IsLocalComponent()
            && Component->GetConstOriginalComponent() == Component
        ? Component
        : nullptr;
}

FString CanonicalField(
    const FSalResolvedTarget& Target,
    const TCHAR* Name)
{
    FString Value;
    if (Target.CanonicalTarget.IsValid())
    {
        Target.CanonicalTarget->TryGetStringField(Name, Value);
    }
    return Value;
}

FString CreationMethodText(const UActorComponent* Component)
{
    if (Component == nullptr)
    {
        return FString();
    }
    switch (Component->CreationMethod)
    {
    case EComponentCreationMethod::Native:
        return TEXT("Native");
    case EComponentCreationMethod::SimpleConstructionScript:
        return TEXT("SimpleConstructionScript");
    case EComponentCreationMethod::Instance:
        return TEXT("Instance");
    case EComponentCreationMethod::UserConstructionScript:
    default:
        return TEXT("UserConstructionScript");
    }
}

FString DeclaringClassFromSCSId(const FString& Source, const FString& Id)
{
    int32 Separator = INDEX_NONE;
    return Source == TEXT("scs")
            && Id.FindLastChar(TEXT('#'), Separator)
            && Separator > 0
        ? Id.Left(Separator)
        : FString();
}

bool IsLoadedGraphObject(const UObject* Object)
{
    return IsValid(Object)
        && !Object->HasAnyFlags(
            IncompleteLoadFlags
            | RF_NewerVersionExists);
}

bool ConsumeGraphObjectText(
    const UObject* Object,
    int32& InOutChars)
{
    if (Object == nullptr)
    {
        return true;
    }
    const FString Path = Object->GetPathName();
    const FString Type = Object->GetClass()->GetPathName();
    if (Path.Len() > MaxGraphBindingStringChars
        || Type.Len() > MaxGraphBindingStringChars
        || InOutChars > MaxGraphBindingStringChars - Path.Len()
        || InOutChars + Path.Len()
            > MaxGraphBindingStringChars - Type.Len())
    {
        return false;
    }
    InOutChars += Path.Len() + Type.Len();
    return true;
}

TSharedPtr<FJsonObject> CanonicalPcgTarget(UPCGGraph* Graph)
{
    if (!IsLoadedGraphObject(Graph)
        || !Graph->IsAsset()
        || Graph->GetTypedOuter<UPCGGraph>() != nullptr)
    {
        return nullptr;
    }
    UPackage* Package = Graph->GetOutermost();
    if (Package == nullptr
        || Package == GetTransientPackage()
        || Package->HasAnyFlags(RF_Transient)
        || Package->HasAnyPackageFlags(PKG_PlayInEditor)
        || !FPackageName::IsValidLongPackageName(
            Package->GetName())
        || FPackageName::IsTempPackage(Package->GetName()))
    {
        return nullptr;
    }

    const IAssetRegistry& Registry =
        FModuleManager::LoadModuleChecked<FAssetRegistryModule>(
            TEXT("AssetRegistry"))
            .Get();
    const FAssetData Data = Registry.GetAssetByObjectPath(
        FSoftObjectPath(Graph->GetPathName()),
        true);
    const FString ActualType = Graph->GetClass()->GetPathName();
    if (!Data.IsValid()
        || Data.AssetClassPath.ToString() != ActualType
        || Data.GetSoftObjectPath().ToString() != Graph->GetPathName())
    {
        return nullptr;
    }

    TSharedPtr<FJsonObject> Target = MakeShared<FJsonObject>();
    Target->SetStringField(TEXT("kind"), TEXT("target"));
    Target->SetStringField(TEXT("domain"), TEXT("pcg"));
    Target->SetStringField(
        TEXT("asset"),
        Data.GetSoftObjectPath().ToString());
    Target->SetStringField(TEXT("type"), ActualType);
    return Target;
}

FGraphBindingSnapshot ReadGraphBinding(
    UPCGComponent* Component,
    const FString& Operation)
{
    FGraphBindingSnapshot Out;
    UPCGGraphInstance* Owned = Component != nullptr
        ? Component->GetGraphInstance()
        : nullptr;
    if (!IsLoadedGraphObject(Owned)
        || Owned->GetOuter() != Component)
    {
        Out.bComplete = false;
        Out.Diagnostics.Add(Warning(
            TEXT("The Component-owned GraphInstance is absent, incomplete, or does not belong directly to the exact Component."),
            Operation));
        return Out;
    }

    if (!Owned->Graph.IsResolved())
    {
        Out.bComplete = false;
        Out.Diagnostics.Add(Warning(
            TEXT("The direct PCG GraphInterface is not already resolved; Query refused to resolve or load its object handle."),
            Operation,
            Component->GetPathName()));
        return Out;
    }

    UPCGGraphInterface* Direct = Owned->Graph.Get();
    if (Direct == nullptr)
    {
        return Out;
    }
    int32 TextChars = 0;
    if (!IsLoadedGraphObject(Direct)
        || !ConsumeGraphObjectText(Direct, TextChars))
    {
        Out.bComplete = false;
        Out.Kind = TEXT("none");
        Out.Diagnostics.Add(Warning(
            TEXT("The direct PCG GraphInterface is incomplete, superseded, or exceeds the bounded string budget; Query did not inspect it."),
            Operation,
            Component->GetPathName()));
        return Out;
    }
    Out.Direct = Direct;
    Out.Kind = Out.Direct->IsA<UPCGGraph>()
        ? TEXT("graph")
        : Out.Direct->IsA<UPCGGraphInstance>()
            ? TEXT("graph_instance")
            : TEXT("none");
    if (Out.Kind == TEXT("none"))
    {
        Out.Direct = nullptr;
        Out.bComplete = false;
        Out.Diagnostics.Add(Warning(
            TEXT("The direct PCG GraphInterface has an unsupported native subtype; Query did not reinterpret it as a Graph or GraphInstance."),
            Operation,
            Component->GetPathName()));
        return Out;
    }

    TSet<const UPCGGraphInterface*> Seen;
    UPCGGraphInterface* Current = Out.Direct;
    for (int32 Depth = 0; Current != nullptr; ++Depth)
    {
        if (Depth >= MaxGraphInterfaceDepth
            || Seen.Contains(Current)
            || !IsLoadedGraphObject(Current)
            || (Current != Out.Direct
                && !ConsumeGraphObjectText(Current, TextChars)))
        {
            Out.bComplete = false;
            break;
        }
        Seen.Add(Current);
        if (UPCGGraph* Graph = Cast<UPCGGraph>(Current))
        {
            Out.TopGraph = Graph;
            break;
        }
        UPCGGraphInstance* Instance = Cast<UPCGGraphInstance>(Current);
        if (Instance == nullptr)
        {
            Out.bComplete = false;
            break;
        }
        if (!Instance->Graph.IsResolved())
        {
            Out.bComplete = false;
            break;
        }
        UPCGGraphInterface* Next = Instance->Graph.Get();
        if (Next == nullptr)
        {
            Current = nullptr;
            break;
        }
        Current = Next;
    }
    if (!Out.bComplete)
    {
        Out.Diagnostics.Add(Warning(
            TEXT("The loaded PCG GraphInterface chain is cyclic, incomplete, unsupported, unresolved, or exceeds its bounded depth/string budget; Query did not follow it further."),
            Operation,
            Component->GetPathName()));
        return Out;
    }
    Out.PcgTarget = CanonicalPcgTarget(Out.TopGraph);
    return Out;
}

TSharedPtr<FJsonValue> DirectGraphInterfaceValue(
    const UPCGGraphInterface* Interface,
    const FString& Kind)
{
    if (Interface == nullptr)
    {
        return Value::Null();
    }
    TSharedPtr<FJsonObject> Fields = MakeShared<FJsonObject>();
    Fields->SetStringField(TEXT("path"), Interface->GetPathName());
    Fields->SetStringField(
        TEXT("type"),
        Interface->GetClass()->GetPathName());
    // These tokens include the reserved SAL word "graph", so their wire
    // representation is deliberately a string rather than a NameExpr.
    Fields->SetStringField(TEXT("kind"), Kind);
    // graph is a reserved structural word, so this encodes one anonymous
    // ObjectExpr rather than inventing a public semantic tag.
    return Value::Call(TEXT("graph"), Fields);
}

TSharedPtr<FJsonValue> TopGraphValue(const UPCGGraph* Graph)
{
    if (Graph == nullptr)
    {
        return Value::Null();
    }
    TSharedPtr<FJsonObject> Fields = MakeShared<FJsonObject>();
    Fields->SetStringField(TEXT("path"), Graph->GetPathName());
    Fields->SetStringField(
        TEXT("type"),
        Graph->GetClass()->GetPathName());
    return Value::Call(TEXT("graph"), Fields);
}

TSharedPtr<FJsonObject> LevelTargetValue(
    const FSalResolvedTarget& Target)
{
    if (Target.AssetPath.IsEmpty())
    {
        return nullptr;
    }
    TSharedPtr<FJsonObject> LevelTarget = MakeShared<FJsonObject>();
    LevelTarget->SetStringField(TEXT("kind"), TEXT("target"));
    LevelTarget->SetStringField(TEXT("domain"), TEXT("level"));
    LevelTarget->SetStringField(TEXT("asset"), Target.AssetPath);
    LevelTarget->SetStringField(
        TEXT("type"),
        TEXT("/Script/Engine.World"));
    return LevelTarget;
}

TSharedPtr<FJsonObject> BuildComponentResult(
    const FSalQuery& Query,
    const FSalResolvedTarget& Target,
    UPCGComponent* Component,
    const bool bSummary,
    const bool bSchema)
{
    const FString ActorId = CanonicalField(Target, TEXT("actorId"));
    const FString Source = CanonicalField(Target, TEXT("source"));
    const FString Id = CanonicalField(Target, TEXT("id"));
    const FString Type = CanonicalField(Target, TEXT("type"));

    FSalObjectBuilder Builder;
    const FString Alias = Builder.UniqueAlias(
        Query.Alias.IsEmpty()
            ? (Target.Name.IsEmpty() ? TEXT("pcg_component") : Target.Name)
            : Query.Alias);
    TSharedPtr<FJsonObject> Fields = MakeShared<FJsonObject>();
    Fields->SetStringField(TEXT("asset"), Target.AssetPath);
    Fields->SetStringField(TEXT("actorId"), ActorId);
    Fields->SetField(TEXT("source"), Value::Name(Source));
    Fields->SetStringField(TEXT("id"), Id);
    Fields->SetStringField(TEXT("name"), Component->GetFName().ToString());
    Fields->SetStringField(TEXT("type"), Type);
    Fields->SetField(
        TEXT("CreationMethod"),
        Value::Name(CreationMethodText(Component)));
    const FString DeclaringClass = DeclaringClassFromSCSId(Source, Id);
    if (!DeclaringClass.IsEmpty())
    {
        Fields->SetStringField(TEXT("declaringClass"), DeclaringClass);
    }
    Fields->SetBoolField(TEXT("loaded"), true);
    FGraphBindingSnapshot GraphBinding;
    if (bSummary)
    {
        GraphBinding = ReadGraphBinding(Component, TEXT("summary"));
        Fields->SetStringField(
            TEXT("graphBindingKind"),
            GraphBinding.Kind);
        Fields->SetBoolField(
            TEXT("graphBindingComplete"),
            GraphBinding.bComplete);
        Fields->SetField(
            TEXT("graphInterface"),
            DirectGraphInterfaceValue(
                GraphBinding.Direct,
                GraphBinding.Direct != nullptr
                        && GraphBinding.Direct->IsA<UPCGGraphInstance>()
                    ? TEXT("graph_instance")
                    : TEXT("graph")));
        Fields->SetField(
            TEXT("graph"),
            TopGraphValue(GraphBinding.TopGraph));
    }

    Builder.AddLocalBinding(
        Alias,
        Value::Call(TEXT("component"), Fields));
    if (bSchema)
    {
        Builder.AddComment(
            TEXT("pcg_component target schema (read-only)\n")
            TEXT("fields: asset, actorId, source, id, name, type, CreationMethod, declaringClass, loaded\n")
            TEXT("operations: target, summary\n")
            TEXT("parameters, execution state, generated resources, inspection, schema mutation, and Patch are not active in this slice"));
    }

    TSharedPtr<FJsonObject> Result = Builder.BuildResult(
        GraphBinding.Diagnostics);
    const FString LevelAlias = ResultTargets::AddHandoff(
        Result,
        LevelTargetValue(Target),
        TEXT("owning_level"),
        TEXT("inspect_level"));
    (void)LevelAlias;
    if (bSummary && GraphBinding.PcgTarget.IsValid())
    {
        const FString GraphAlias = ResultTargets::AddHandoff(
            Result,
            GraphBinding.PcgTarget,
            TEXT("graph"),
            TEXT("inspect_graph"));
        (void)GraphAlias;
    }
    return Result;
}

bool HasExactClauses(const FSalQuery& Query)
{
    return Query.Where.IsValid()
        || !Query.OrderBy.IsEmpty()
        || Query.PageLimit > 0
        || !Query.PageAfter.IsEmpty();
}
}

TSharedPtr<FJsonObject> FSalPCGComponentInterface::Query(
    const FSalQuery& Query,
    const FSalResolvedTarget& Target)
{
    UPCGComponent* Component = ResolvedComponent(Target);
    if (Component == nullptr)
    {
        return QueryError(
            TEXT("capability.interface_unavailable"),
            TEXT("pcg_component Query requires one exact loaded original authored UPCGComponent Target."),
            TEXT("query"));
    }
    FString Operation;
    if (!Query.Operation.IsValid()
        || !Query.Operation->TryGetStringField(TEXT("kind"), Operation))
    {
        return QueryError(
            TEXT("capability.operation_unavailable"),
            TEXT("pcg_component Query has no supported primary operation."),
            TEXT("query"),
            FString(),
            {TEXT("target"), TEXT("summary")});
    }
    if (Operation == TEXT("target"))
    {
        if (HasExactClauses(Query))
        {
            return QueryError(
                TEXT("capability.clause_unavailable"),
                TEXT("Exact pcg_component target read accepts only optional with schema."),
                Operation);
        }
        for (const FString& Detail : Query.With)
        {
            if (Detail != TEXT("schema"))
            {
                return QueryError(
                    TEXT("capability.detail_unavailable"),
                    TEXT("Exact pcg_component target read supports only with schema."),
                    Operation,
                    Detail);
            }
        }
        return BuildComponentResult(
            Query,
            Target,
            Component,
            false,
            Query.With.Contains(TEXT("schema")));
    }
    if (Operation == TEXT("summary"))
    {
        if (!Query.With.IsEmpty() || HasExactClauses(Query))
        {
            return QueryError(
                TEXT("capability.clause_unavailable"),
                TEXT("pcg_component summary accepts no Query clauses."),
                Operation);
        }
        return BuildComponentResult(
            Query,
            Target,
            Component,
            true,
            false);
    }
    return QueryError(
        TEXT("capability.operation_unavailable"),
        FString::Printf(
            TEXT("pcg_component Query operation is not active in this read-only slice: %s."),
            *Operation),
        Operation,
        FString(),
        {TEXT("target"), TEXT("summary")});
}
}
