// Copyright 2026 Loomle contributors.

#include "SalProjectionService.h"

#include "../SalDiagnostics.h"
#include "../SalModule.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Editor.h"
#include "Components/ActorComponent.h"
#include "Engine/Blueprint.h"
#include "Engine/Level.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Misc/PackageName.h"
#include "PCGComponent.h"
#include "PCGGraph.h"
#include "UObject/Class.h"
#include "UObject/Package.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/UObjectIterator.h"

namespace Loomle::Sal
{
namespace
{

TSharedRef<FJsonObject> ProjectionError(
    const FString& Code,
    const FString& Message,
    const FString& Ref)
{
    return FSalDiagnostics::Error(Code, Message)
        .Interface(TEXT("python"))
        .Operation(TEXT("projection"))
        .Ref(Ref)
        .Build();
}

TSharedPtr<FJsonValue> MarkerValue(const TSharedPtr<FJsonObject>& Object)
{
    return Object.IsValid()
        ? Object->TryGetField(FSalProjectionService::MarkerKey)
        : TSharedPtr<FJsonValue>();
}

// A source map World: the loaded authored Editor World that is not a
// PIE/SIE/transient play World.
UWorld* FindSourceEditorWorld()
{
    if (GEditor == nullptr)
    {
        return nullptr;
    }
    for (const FWorldContext& Context : GEditor->GetWorldContexts())
    {
        UWorld* World = Context.World();
        if (World == nullptr
            || World->IsPlayInEditor()
            || World->IsPreviewWorld()
            || World->GetOutermost() == GetTransientPackage()
            || World->GetOutermost()->HasAnyFlags(RF_Transient))
        {
            continue;
        }
        return World;
    }
    return nullptr;
}

bool IsTransientWorldObject(const UObject* Object)
{
    if (Object == nullptr)
    {
        return true;
    }
    const UPackage* Outermost = Object->GetOutermost();
    return Outermost == nullptr
        || Outermost == GetTransientPackage()
        || Outermost->HasAnyFlags(RF_Transient)
        || FPackageName::IsTempPackage(Outermost->GetName());
}

AActor* FindSourceActorByGuid(const FGuid& Guid)
{
    if (!Guid.IsValid())
    {
        return nullptr;
    }
    UWorld* SourceWorld = FindSourceEditorWorld();
    if (SourceWorld == nullptr)
    {
        return nullptr;
    }
    for (const ULevel* Level : SourceWorld->GetLevels())
    {
        if (Level == nullptr)
        {
            continue;
        }
        for (const AActor* Actor : Level->Actors)
        {
            if (Actor != nullptr
                && IsValid(Actor)
                && Actor->GetActorGuid() == Guid)
            {
                return const_cast<AActor*>(Actor);
            }
        }
    }
    return nullptr;
}

TSharedRef<FJsonObject> LevelTarget(const UWorld* World)
{
    TSharedRef<FJsonObject> Target = MakeShared<FJsonObject>();
    Target->SetStringField(TEXT("kind"), TEXT("target"));
    Target->SetStringField(TEXT("domain"), TEXT("level"));
    Target->SetStringField(
        TEXT("asset"),
        World != nullptr ? World->GetPathName() : FString());
    Target->SetStringField(
        TEXT("type"),
        World != nullptr
            ? World->GetClass()->GetPathName()
            : FString());
    return Target;
}

TSharedRef<FJsonObject> QueryArguments(
    const TSharedRef<FJsonObject>& Target,
    const TSharedRef<FJsonObject>& Operation)
{
    TSharedRef<FJsonObject> Binding = MakeShared<FJsonObject>();
    Binding->SetStringField(TEXT("alias"), TEXT("projection"));
    Binding->SetObjectField(TEXT("target"), Target);
    TSharedRef<FJsonObject> Query = MakeShared<FJsonObject>();
    Query->SetStringField(TEXT("kind"), TEXT("query"));
    Query->SetObjectField(TEXT("target"), Binding);
    Query->SetObjectField(TEXT("operation"), Operation);
    TSharedRef<FJsonObject> Arguments = MakeShared<FJsonObject>();
    Arguments->SetObjectField(TEXT("object"), Query);
    return Arguments;
}

TSharedRef<FJsonObject> TargetOperation()
{
    TSharedRef<FJsonObject> Operation = MakeShared<FJsonObject>();
    Operation->SetStringField(TEXT("kind"), TEXT("target"));
    return Operation;
}

TSharedRef<FJsonObject> ExactRefOperation(
    const TArray<FString>& IdentityPath)
{
    TSharedRef<FJsonObject> Ref = MakeShared<FJsonObject>();
    Ref->SetStringField(TEXT("kind"), TEXT("stable_ref"));
    TArray<TSharedPtr<FJsonValue>> Segments;
    Segments.Reserve(IdentityPath.Num());
    for (const FString& Segment : IdentityPath)
    {
        Segments.Add(MakeShared<FJsonValueString>(Segment));
    }
    Ref->SetArrayField(TEXT("identityPath"), Segments);
    TSharedRef<FJsonObject> Operation = MakeShared<FJsonObject>();
    Operation->SetStringField(TEXT("kind"), TEXT("object"));
    Operation->SetObjectField(TEXT("target"), Ref);
    return Operation;
}

TSharedRef<FJsonObject> AssetTarget(
    const FString& ObjectPath,
    const FString& Type)
{
    TSharedRef<FJsonObject> Target = MakeShared<FJsonObject>();
    Target->SetStringField(TEXT("kind"), TEXT("target"));
    Target->SetStringField(TEXT("domain"), TEXT("asset"));
    Target->SetStringField(TEXT("path"), ObjectPath);
    Target->SetStringField(TEXT("type"), Type);
    return Target;
}

TSharedRef<FJsonObject> ClassTarget(const FString& ClassPath)
{
    TSharedRef<FJsonObject> Target = MakeShared<FJsonObject>();
    Target->SetStringField(TEXT("kind"), TEXT("target"));
    Target->SetStringField(TEXT("domain"), TEXT("class"));
    Target->SetStringField(TEXT("path"), ClassPath);
    return Target;
}

TSharedRef<FJsonObject> BlueprintTarget(
    const FString& AssetPath,
    const FString& BlueprintId)
{
    TSharedRef<FJsonObject> Target = MakeShared<FJsonObject>();
    Target->SetStringField(TEXT("kind"), TEXT("target"));
    Target->SetStringField(TEXT("domain"), TEXT("blueprint"));
    Target->SetStringField(TEXT("asset"), AssetPath);
    Target->SetStringField(TEXT("id"), BlueprintId);
    return Target;
}

TSharedRef<FJsonObject> PcgTarget(
    const FString& AssetPath,
    const FString& Type)
{
    TSharedRef<FJsonObject> Target = MakeShared<FJsonObject>();
    Target->SetStringField(TEXT("kind"), TEXT("target"));
    Target->SetStringField(TEXT("domain"), TEXT("pcg"));
    Target->SetStringField(TEXT("asset"), AssetPath);
    Target->SetStringField(TEXT("type"), Type);
    return Target;
}

TSharedRef<FJsonObject> PcgComponentTarget(
    const FString& MapPath,
    const FString& ActorId,
    const FString& Source,
    const FString& Id,
    const FString& Type)
{
    TSharedRef<FJsonObject> Target = MakeShared<FJsonObject>();
    Target->SetStringField(TEXT("kind"), TEXT("target"));
    Target->SetStringField(TEXT("domain"), TEXT("pcg_component"));
    Target->SetStringField(TEXT("asset"), MapPath);
    Target->SetStringField(TEXT("actorId"), ActorId);
    Target->SetStringField(TEXT("source"), Source);
    Target->SetStringField(TEXT("id"), Id);
    Target->SetStringField(TEXT("type"), Type);
    return Target;
}

bool HasErrorDiagnostic(const TSharedPtr<FJsonObject>& Result)
{
    const TArray<TSharedPtr<FJsonValue>>* Diagnostics = nullptr;
    if (!Result.IsValid()
        || !Result->TryGetArrayField(TEXT("diagnostics"), Diagnostics)
        || Diagnostics == nullptr)
    {
        return true;
    }
    for (const TSharedPtr<FJsonValue>& Value : *Diagnostics)
    {
        const TSharedPtr<FJsonObject>* Diagnostic = nullptr;
        FString Severity;
        if (Value.IsValid()
            && Value->TryGetObject(Diagnostic)
            && Diagnostic != nullptr
            && (*Diagnostic).IsValid()
            && (*Diagnostic)->TryGetStringField(
                TEXT("severity"),
                Severity)
            && Severity == TEXT("error"))
        {
            return true;
        }
    }
    return false;
}

TSharedPtr<FJsonObject> RunQuery(
    const TSharedRef<FJsonObject>& Target,
    const TSharedRef<FJsonObject>& Operation)
{
    return FSalModule::BuildQueryResult(
        QueryArguments(Target, Operation));
}

// Classify one marked UObject and emit status, relation, and a canonical
// view (when projected). bTransientDuplicate tells the caller that the object
// lives in a transient/PIE World and can only be an authored_source view.
bool ProjectLevelActor(
    AActor* Actor,
    const bool bTransient,
    FString& OutStatus,
    FString& OutRelation,
    TSharedPtr<FJsonObject>& OutView,
    TArray<TSharedPtr<FJsonObject>>& OutDiagnostics)
{
    if (Actor == nullptr || !IsValid(Actor))
    {
        OutStatus = TEXT("stale");
        OutDiagnostics.Add(ProjectionError(
            TEXT("projection.stale"),
            TEXT("The marked Actor no longer resolves at terminalization."),
            Actor != nullptr ? Actor->GetPathName() : FString()));
        return true;
    }
    AActor* Subject = Actor;
    if (bTransient)
    {
        AActor* Source = FindSourceActorByGuid(Actor->GetActorGuid());
        if (Source == nullptr)
        {
            OutStatus = TEXT("unsupported");
            OutDiagnostics.Add(ProjectionError(
                TEXT("projection.transient_only"),
                TEXT("The marked Actor is a transient-only object with no "
                    "uniquely proven authored source."),
                Actor->GetPathName()));
            return true;
        }
        Subject = Source;
        OutRelation = TEXT("authored_source");
    }
    else
    {
        OutRelation = TEXT("exact");
    }
    UWorld* World = Subject->GetWorld();
    if (World == nullptr)
    {
        OutStatus = TEXT("unsupported");
        OutDiagnostics.Add(ProjectionError(
            TEXT("projection.unsupported"),
            TEXT("The marked Actor has no resolvable World for a level view."),
            Actor->GetPathName()));
        return true;
    }
    const FString ActorId = Subject->GetActorGuid().ToString(
        EGuidFormats::DigitsWithHyphensLower);
    if (ActorId.IsEmpty() || ActorId == TEXT("00000000-0000-0000-0000-000000000000"))
    {
        OutStatus = TEXT("unsupported");
        OutDiagnostics.Add(ProjectionError(
            TEXT("projection.unsupported"),
            TEXT("The marked Actor has no persistent ActorGuid for a level view."),
            Actor->GetPathName()));
        return true;
    }
    OutView = RunQuery(
        LevelTarget(World),
        ExactRefOperation({ActorId}));
    if (!OutView.IsValid() || HasErrorDiagnostic(OutView))
    {
        OutStatus = TEXT("unsupported");
        OutDiagnostics.Add(ProjectionError(
            TEXT("projection.unsupported"),
            TEXT("The marked Actor could not be projected to a canonical "
                "level view."),
            Actor->GetPathName()));
        OutView.Reset();
        return true;
    }
    OutStatus = TEXT("projected");
    return true;
}

bool ProjectLevelComponent(
    UActorComponent* Component,
    const bool bTransient,
    FString& OutStatus,
    FString& OutRelation,
    TSharedPtr<FJsonObject>& OutView,
    TArray<TSharedPtr<FJsonObject>>& OutDiagnostics)
{
    if (Component == nullptr || !IsValid(Component))
    {
        OutStatus = TEXT("stale");
        OutDiagnostics.Add(ProjectionError(
            TEXT("projection.stale"),
            TEXT("The marked Component no longer resolves at "
                "terminalization."),
            Component != nullptr ? Component->GetPathName() : FString()));
        return true;
    }
    AActor* Owner = Component->GetOwner();
    if (Owner == nullptr)
    {
        OutStatus = TEXT("unsupported");
        OutDiagnostics.Add(ProjectionError(
            TEXT("projection.unsupported"),
            TEXT("The marked Component has no owner Actor for a level view."),
            Component->GetPathName()));
        return true;
    }
    FString Source;
    switch (Component->CreationMethod)
    {
    case EComponentCreationMethod::Native:
        Source = TEXT("native");
        break;
    case EComponentCreationMethod::Instance:
        Source = TEXT("instance");
        break;
    case EComponentCreationMethod::SimpleConstructionScript:
        Source = TEXT("scs");
        break;
    default:
        Source.Reset();
        break;
    }
    if (Source.IsEmpty())
    {
        OutStatus = TEXT("unsupported");
        OutDiagnostics.Add(ProjectionError(
            TEXT("projection.unsupported"),
            TEXT("The marked Component has no serialized Level source kind "
                "for a level view."),
            Component->GetPathName()));
        return true;
    }
    if (Source == TEXT("scs"))
    {
        OutStatus = TEXT("unsupported");
        OutDiagnostics.Add(ProjectionError(
            TEXT("projection.unsupported"),
            TEXT("SCS Component projection is not certified in this "
                "increment."),
            Component->GetPathName()));
        return true;
    }
    AActor* SubjectOwner = Owner;
    if (bTransient)
    {
        AActor* SourceActor = FindSourceActorByGuid(Owner->GetActorGuid());
        if (SourceActor == nullptr)
        {
            OutStatus = TEXT("unsupported");
            OutDiagnostics.Add(ProjectionError(
                TEXT("projection.transient_only"),
                TEXT("The marked Component's owner has no uniquely proven "
                    "authored source."),
                Component->GetPathName()));
            return true;
        }
        SubjectOwner = SourceActor;
        OutRelation = TEXT("authored_source");
    }
    else
    {
        OutRelation = TEXT("exact");
    }
    UWorld* World = SubjectOwner->GetWorld();
    if (World == nullptr)
    {
        OutStatus = TEXT("unsupported");
        OutDiagnostics.Add(ProjectionError(
            TEXT("projection.unsupported"),
            TEXT("The marked Component's owner has no resolvable World for "
                "a level view."),
            Component->GetPathName()));
        return true;
    }
    const FString ActorId = SubjectOwner->GetActorGuid().ToString(
        EGuidFormats::DigitsWithHyphensLower);
    const FString Slot = Component->GetFName().ToString();
    if (ActorId.IsEmpty()
        || ActorId == TEXT("00000000-0000-0000-0000-000000000000")
        || Slot.IsEmpty())
    {
        OutStatus = TEXT("unsupported");
        OutDiagnostics.Add(ProjectionError(
            TEXT("projection.unsupported"),
            TEXT("The marked Component has no complete Level identity for a "
                "level view."),
            Component->GetPathName()));
        return true;
    }
    OutView = RunQuery(
        LevelTarget(World),
        ExactRefOperation({ActorId, Source, Slot}));
    if (!OutView.IsValid() || HasErrorDiagnostic(OutView))
    {
        OutStatus = TEXT("unsupported");
        OutDiagnostics.Add(ProjectionError(
            TEXT("projection.unsupported"),
            TEXT("The marked Component could not be projected to a "
                "canonical level view."),
            Component->GetPathName()));
        OutView.Reset();
        return true;
    }
    OutStatus = TEXT("projected");
    return true;
}

bool ProjectObject(
    const FString& Path,
    FString& OutStatus,
    FString& OutRelation,
    TSharedPtr<FJsonObject>& OutView,
    TArray<TSharedPtr<FJsonObject>>& OutDiagnostics)
{
    OutStatus.Reset();
    OutRelation.Reset();
    OutView.Reset();
    OutDiagnostics.Reset();

    UObject* Object = FindObject<UObject>(nullptr, *Path);
    if (Object == nullptr || !IsValid(Object))
    {
        OutStatus = TEXT("stale");
        OutDiagnostics.Add(ProjectionError(
            TEXT("projection.stale"),
            TEXT("The marked object no longer resolves at terminalization."),
            Path));
        return true;
    }
    const bool bTransient = IsTransientWorldObject(Object);

    if (AActor* Actor = Cast<AActor>(Object))
    {
        return ProjectLevelActor(
            Actor,
            bTransient,
            OutStatus,
            OutRelation,
            OutView,
            OutDiagnostics);
    }
    if (UPCGComponent* PcgComponent = Cast<UPCGComponent>(Object))
    {
        if (bTransient)
        {
            AActor* Owner = PcgComponent->GetOwner();
            AActor* SourceActor = Owner != nullptr
                ? FindSourceActorByGuid(Owner->GetActorGuid())
                : nullptr;
            if (SourceActor == nullptr)
            {
                OutStatus = TEXT("unsupported");
                OutDiagnostics.Add(ProjectionError(
                    TEXT("projection.transient_only"),
                    TEXT("The marked PCG Component has no uniquely proven "
                        "authored source."),
                    Path));
                return true;
            }
            Object = SourceActor->GetComponentByClass(
                UPCGComponent::StaticClass());
            if (Object == nullptr)
            {
                OutStatus = TEXT("unsupported");
                OutDiagnostics.Add(ProjectionError(
                    TEXT("projection.transient_only"),
                    TEXT("The marked PCG Component's authored source carries "
                        "no matching PCG Component."),
                    Path));
                return true;
            }
            OutRelation = TEXT("authored_source");
        }
        else
        {
            OutRelation = TEXT("exact");
        }
        UPCGComponent* Subject = Cast<UPCGComponent>(Object);
        if (Subject == nullptr || !IsValid(Subject))
        {
            OutStatus = TEXT("unsupported");
            OutDiagnostics.Add(ProjectionError(
                TEXT("projection.unsupported"),
                TEXT("The marked PCG Component has no resolvable authored "
                    "subject."),
                Path));
            return true;
        }
        AActor* Owner = Subject->GetOwner();
        UWorld* World = Owner != nullptr ? Owner->GetWorld() : nullptr;
        if (Owner == nullptr || World == nullptr)
        {
            OutStatus = TEXT("unsupported");
            OutDiagnostics.Add(ProjectionError(
                TEXT("projection.unsupported"),
                TEXT("The marked PCG Component has no resolvable owner for a "
                    "pcg_component view."),
                Path));
            return true;
        }
        FString Source;
        switch (Subject->CreationMethod)
        {
        case EComponentCreationMethod::Native:
            Source = TEXT("native");
            break;
        case EComponentCreationMethod::Instance:
            Source = TEXT("instance");
            break;
        case EComponentCreationMethod::SimpleConstructionScript:
            Source = TEXT("scs");
            break;
        default:
            Source.Reset();
            break;
        }
        if (Source.IsEmpty())
        {
            OutStatus = TEXT("unsupported");
            OutDiagnostics.Add(ProjectionError(
                TEXT("projection.unsupported"),
                TEXT("The marked PCG Component has no serialized Level source "
                    "kind for a pcg_component view."),
                Path));
            return true;
        }
        const FString ActorId = Owner->GetActorGuid().ToString(
            EGuidFormats::DigitsWithHyphensLower);
        OutView = RunQuery(
            PcgComponentTarget(
                World->GetPathName(),
                ActorId,
                Source,
                Subject->GetFName().ToString(),
                Subject->GetClass()->GetPathName()),
            TargetOperation());
        if (!OutView.IsValid() || HasErrorDiagnostic(OutView))
        {
            OutStatus = TEXT("unsupported");
            OutDiagnostics.Add(ProjectionError(
                TEXT("projection.unsupported"),
                TEXT("The marked PCG Component could not be projected to a "
                    "canonical pcg_component view."),
                Path));
            OutView.Reset();
            return true;
        }
        OutStatus = TEXT("projected");
        return true;
    }
    if (UActorComponent* Component = Cast<UActorComponent>(Object))
    {
        return ProjectLevelComponent(
            Component,
            bTransient,
            OutStatus,
            OutRelation,
            OutView,
            OutDiagnostics);
    }
    if (UPCGGraph* Graph = Cast<UPCGGraph>(Object))
    {
        if (bTransient || !Graph->IsAsset())
        {
            OutStatus = TEXT("unsupported");
            OutDiagnostics.Add(ProjectionError(
                TEXT("projection.unsupported"),
                TEXT("The marked Graph is not an independently saved "
                    "top-level asset."),
                Path));
            return true;
        }
        OutRelation = TEXT("exact");
        OutView = RunQuery(
            PcgTarget(
                Graph->GetPathName(),
                Graph->GetClass()->GetPathName()),
            TargetOperation());
        if (!OutView.IsValid() || HasErrorDiagnostic(OutView))
        {
            OutStatus = TEXT("unsupported");
            OutDiagnostics.Add(ProjectionError(
                TEXT("projection.unsupported"),
                TEXT("The marked Graph could not be projected to a "
                    "canonical pcg view."),
                Path));
            OutView.Reset();
            return true;
        }
        OutStatus = TEXT("projected");
        return true;
    }
    if (UClass* Class = Cast<UClass>(Object))
    {
        if (bTransient)
        {
            OutStatus = TEXT("unsupported");
            OutDiagnostics.Add(ProjectionError(
                TEXT("projection.transient_only"),
                TEXT("The marked Class has no persistent native identity."),
                Path));
            return true;
        }
        OutRelation = TEXT("exact");
        OutView = RunQuery(
            ClassTarget(Class->GetPathName()),
            TargetOperation());
        if (!OutView.IsValid() || HasErrorDiagnostic(OutView))
        {
            OutStatus = TEXT("unsupported");
            OutDiagnostics.Add(ProjectionError(
                TEXT("projection.unsupported"),
                TEXT("The marked Class could not be projected to a "
                    "canonical class view."),
                Path));
            OutView.Reset();
            return true;
        }
        OutStatus = TEXT("projected");
        return true;
    }
    if (UBlueprint* Blueprint = Cast<UBlueprint>(Object))
    {
        if (bTransient)
        {
            OutStatus = TEXT("unsupported");
            OutDiagnostics.Add(ProjectionError(
                TEXT("projection.transient_only"),
                TEXT("The marked Blueprint has no persistent asset identity."),
                Path));
            return true;
        }
        OutRelation = TEXT("exact");
        OutView = RunQuery(
            BlueprintTarget(
                Blueprint->GetPathName(),
                Blueprint->GetBlueprintGuid().ToString(
                    EGuidFormats::DigitsWithHyphensLower)),
            TargetOperation());
        if (!OutView.IsValid() || HasErrorDiagnostic(OutView))
        {
            OutStatus = TEXT("unsupported");
            OutDiagnostics.Add(ProjectionError(
                TEXT("projection.unsupported"),
                TEXT("The marked Blueprint could not be projected to a "
                    "canonical blueprint view."),
                Path));
            OutView.Reset();
            return true;
        }
        OutStatus = TEXT("projected");
        return true;
    }
    if (!bTransient
        && Object->IsAsset()
        && Object->GetOutermost() != GetTransientPackage())
    {
        OutRelation = TEXT("exact");
        OutView = RunQuery(
            AssetTarget(
                Object->GetPathName(),
                Object->GetClass()->GetPathName()),
            TargetOperation());
        if (!OutView.IsValid() || HasErrorDiagnostic(OutView))
        {
            OutStatus = TEXT("unsupported");
            OutDiagnostics.Add(ProjectionError(
                TEXT("projection.unsupported"),
                TEXT("The marked asset could not be projected to a "
                    "canonical asset view."),
                Path));
            OutView.Reset();
            return true;
        }
        OutStatus = TEXT("projected");
        return true;
    }
    OutStatus = TEXT("unsupported");
    OutDiagnostics.Add(ProjectionError(
        TEXT("projection.transient_only"),
        TEXT("The marked object is a transient-only object with no "
            "published SAL view."),
        Path));
    return true;
}

TSharedPtr<FJsonObject> ProjectionRecord(
    const FString& Path,
    int32& OutProjected)
{
    FString Status;
    FString Relation;
    TSharedPtr<FJsonObject> View;
    TArray<TSharedPtr<FJsonObject>> Diagnostics;
    if (!ProjectObject(
            Path,
            Status,
            Relation,
            View,
            Diagnostics))
    {
        Status = TEXT("failed");
        Diagnostics.Add(ProjectionError(
            TEXT("projection.failed"),
            TEXT("The Bridge projector failed while building the "
                "sal.object() view."),
            Path));
    }
    TSharedRef<FJsonObject> Record = MakeShared<FJsonObject>();
    Record->SetStringField(TEXT("status"), Status);
    if (Status == TEXT("projected"))
    {
        Record->SetStringField(TEXT("relation"), Relation);
        Record->SetObjectField(TEXT("view"), View);
        ++OutProjected;
    }
    if (!Diagnostics.IsEmpty())
    {
        TArray<TSharedPtr<FJsonValue>> Values;
        Values.Reserve(Diagnostics.Num());
        for (const TSharedPtr<FJsonObject>& Diagnostic : Diagnostics)
        {
            if (Diagnostic.IsValid())
            {
                Values.Add(MakeShared<FJsonValueObject>(Diagnostic));
            }
        }
        Record->SetArrayField(TEXT("diagnostics"), Values);
    }
    return Record;
}

// Walk an arbitrary JSON value and replace every sal.object() marker in
// place. Returns false when a Bridge integrity fault occurred.
bool WalkAndProject(
    const TSharedPtr<FJsonValue>& Value,
    int32& OutMarked,
    int32& OutProjected,
    bool& OutComplete)
{
    if (!Value.IsValid())
    {
        return true;
    }
    const TSharedPtr<FJsonObject>* Object = nullptr;
    if (Value->TryGetObject(Object) && Object != nullptr && (*Object).IsValid())
    {
        if ((*Object)->HasField(FSalProjectionService::MarkerKey))
        {
            FString Path;
            if (!(*Object)->TryGetStringField(
                    FSalProjectionService::MarkerKey,
                    Path)
                || Path.IsEmpty())
            {
                OutComplete = false;
                return false;
            }
            ++OutMarked;
            const TSharedPtr<FJsonObject> Record =
                ProjectionRecord(Path, OutProjected);
            (*Object)->Values.Reset();
            for (const TPair<FString, TSharedPtr<FJsonValue>>& Entry
                : Record->Values)
            {
                (*Object)->SetField(Entry.Key, Entry.Value);
            }
            return true;
        }
        for (const TPair<FString, TSharedPtr<FJsonValue>>& Entry
            : (*Object)->Values)
        {
            if (!WalkAndProject(
                    Entry.Value,
                    OutMarked,
                    OutProjected,
                    OutComplete))
            {
                return false;
            }
        }
        return true;
    }
    const TArray<TSharedPtr<FJsonValue>>* Array = nullptr;
    if (Value->TryGetArray(Array) && Array != nullptr)
    {
        for (const TSharedPtr<FJsonValue>& Element : *Array)
        {
            if (!WalkAndProject(
                    Element,
                    OutMarked,
                    OutProjected,
                    OutComplete))
            {
                return false;
            }
        }
    }
    return true;
}

}

bool FSalProjectionService::ProjectResult(TSharedPtr<FJsonObject>& Result)
{
    if (!Result.IsValid())
    {
        return true;
    }
    int32 Marked = 0;
    int32 Projected = 0;
    bool bComplete = true;
    WalkAndProject(
        MakeShared<FJsonValueObject>(Result),
        Marked,
        Projected,
        bComplete);
    if (Marked > 0)
    {
        TSharedRef<FJsonObject> Annex = MakeShared<FJsonObject>();
        Annex->SetBoolField(TEXT("complete"), bComplete);
        Annex->SetNumberField(TEXT("marked"), static_cast<double>(Marked));
        Annex->SetNumberField(
            TEXT("projected"),
            static_cast<double>(Projected));
        Result->SetObjectField(TEXT("projection"), Annex);
    }
    return bComplete;
}
}
