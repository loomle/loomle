// Copyright 2026 Loomle contributors.

#include "Runtime/Pcg/TypedPcgWorldRegistry.h"

#include "../../Sal/SalTargetResolver.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Editor.h"
#include "Engine/Level.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "HAL/PlatformTime.h"
#include "Misc/Guid.h"
#include "Misc/ScopeLock.h"
#include "PCGComponent.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace Loomle::Runtime
{
namespace
{

TSharedPtr<FJsonObject> ParseJson(const FString& Text)
{
    TSharedPtr<FJsonObject> Object;
    const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Text);
    return FJsonSerializer::Deserialize(Reader, Object) && Object.IsValid()
        ? Object
        : nullptr;
}

}

bool FTypedPcgWorldSelector::Parse(
    const TSharedPtr<FJsonObject>& Selector,
    FString& OutError)
{
    OutError.Reset();
    WorldKind.Reset();
    PlayMode.Reset();
    PieInstance = -1;
    if (!Selector.IsValid())
    {
        OutError = TEXT(
            "A typed-PCG World selector is required.");
        return false;
    }
    if (!Selector->TryGetStringField(TEXT("worldKind"), WorldKind)
        || (WorldKind != TEXT("editor") && WorldKind != TEXT("pie")))
    {
        OutError = TEXT(
            "The typed-PCG World selector worldKind must be exactly editor or pie.");
        return false;
    }
    if (WorldKind == TEXT("pie"))
    {
        if (!Selector->TryGetStringField(TEXT("playMode"), PlayMode)
            || (PlayMode != TEXT("play") && PlayMode != TEXT("simulate")))
        {
            OutError = TEXT(
                "A typed-PCG pie selector requires playMode exactly play or simulate.");
            return false;
        }
        double Instance = 0.0;
        if (!Selector->TryGetNumberField(TEXT("pieInstance"), Instance)
            || Instance < 0.0
            || FMath::Frac(Instance) != 0.0)
        {
            OutError = TEXT(
                "A typed-PCG pie selector requires a non-negative integer pieInstance.");
            return false;
        }
        PieInstance = static_cast<int32>(Instance);
    }
    else
    {
        if (Selector->HasField(TEXT("playMode"))
            || Selector->HasField(TEXT("pieInstance")))
        {
            OutError = TEXT(
                "A typed-PCG editor selector must not carry playMode or pieInstance.");
            return false;
        }
    }
    return true;
}

bool FTypedPcgWorldSelector::IsValid() const
{
    if (WorldKind != TEXT("editor") && WorldKind != TEXT("pie"))
    {
        return false;
    }
    if (WorldKind == TEXT("editor"))
    {
        return PlayMode.IsEmpty() && PieInstance < 0;
    }
    return (PlayMode == TEXT("play") || PlayMode == TEXT("simulate"))
        && PieInstance >= 0;
}

FString FTypedPcgWorldSelector::NormalizeText() const
{
    if (!IsValid())
    {
        return FString();
    }
    if (WorldKind == TEXT("editor"))
    {
        return TEXT("editor");
    }
    return FString::Printf(
        TEXT("pie:%s:%d"),
        *PlayMode,
        PieInstance);
}

void FTypedPcgWorldRegistry::Startup()
{
    FScopeLock Lock(&Mutex);
    bInitialized = true;
    Epochs.Reset();
    Tickets.Reset();
}

void FTypedPcgWorldRegistry::Shutdown()
{
    FScopeLock Lock(&Mutex);
    bInitialized = false;
    Tickets.Reset();
    Epochs.Reset();
}

bool FTypedPcgWorldRegistry::ResolveWorld(
    const FTypedPcgWorldSelector& Selector,
    UWorld*& OutWorld,
    FString& OutError)
{
    OutWorld = nullptr;
    OutError.Reset();
    if (!Selector.IsValid())
    {
        OutError = TEXT(
            "The typed-PCG World selector is invalid.");
        return false;
    }
    if (GEditor == nullptr)
    {
        OutError = TEXT(
            "Typed-PCG World resolution requires an Editor runtime.");
        return false;
    }
    if (Selector.WorldKind == TEXT("editor"))
    {
        for (const FWorldContext& Context : GEditor->GetWorldContexts())
        {
            UWorld* World = Context.World();
            if (World == nullptr
                || World->IsPlayInEditor()
                || World->IsPreviewWorld())
            {
                continue;
            }
            if (OutWorld != nullptr)
            {
                OutError = TEXT(
                    "Multiple Editor Worlds are candidates; typed-PCG "
                    "resolution is ambiguous.");
                return false;
            }
            OutWorld = World;
        }
        if (OutWorld == nullptr)
        {
            OutError = TEXT(
                "No loaded Editor World is available for typed-PCG "
                "resolution.");
            return false;
        }
        return true;
    }

    // PIE: match the exact World context instance and play/simulate mode.
    // SIE shares the global editor simulate state across PIE contexts, so the
    // play/simulate discriminator is the editor play-session state.
    const bool bSimulate = GEditor->IsSimulatingInEditor();
    for (const FWorldContext& Context : GEditor->GetWorldContexts())
    {
        UWorld* World = Context.World();
        if (World == nullptr || !World->IsPlayInEditor())
        {
            continue;
        }
        if (Context.PIEInstance != Selector.PieInstance)
        {
            continue;
        }
        if ((Selector.PlayMode == TEXT("simulate")) != bSimulate)
        {
            continue;
        }
        if (OutWorld != nullptr)
        {
            OutError = TEXT(
                "Multiple PIE Worlds match the selector; typed-PCG "
                "resolution is ambiguous.");
            return false;
        }
        OutWorld = World;
    }
    if (OutWorld == nullptr)
    {
        OutError = FString::Printf(
            TEXT("No matching typed-PCG PIE World for %s."),
            *Selector.NormalizeText());
        return false;
    }
    return true;
}

uint64 FTypedPcgWorldRegistry::NextEpochLocked(const FString& WorldPath)
{
    uint64& Epoch = Epochs.FindOrAdd(WorldPath);
    ++Epoch;
    return Epoch;
}

bool FTypedPcgWorldRegistry::ResolveComponentForWorld(
    const FTypedPcgWorldSelector& Selector,
    UWorld* World,
    const FString& SourceTargetText,
    UPCGComponent*& OutComponent,
    FString& OutError)
{
    OutComponent = nullptr;
    OutError.Reset();
    const TSharedPtr<FJsonObject> TargetValue = ParseJson(SourceTargetText);
    if (!TargetValue.IsValid())
    {
        OutError = TEXT(
            "The typed-PCG source Target text is not canonical JSON.");
        return false;
    }
    Loomle::Sal::FSalResolvedTarget SourceTarget;
    TSharedPtr<FJsonObject> ResolveError;
    Loomle::Sal::FSalTargetResolver Resolver;
    if (!Resolver.Resolve(
            TEXT("pcg"),
            TargetValue,
            false,
            SourceTarget,
            ResolveError))
    {
        OutError = TEXT(
            "The typed-PCG source pcg_component Target could not be "
            "resolved.");
        return false;
    }
    UPCGComponent* SourceComponent =
        Cast<UPCGComponent>(SourceTarget.Object);
    if (SourceComponent == nullptr || !IsValid(SourceComponent))
    {
        OutError = TEXT(
            "The typed-PCG source Target resolved to no original authored "
            "UPCGComponent.");
        return false;
    }
    if (Selector.WorldKind == TEXT("editor"))
    {
        OutComponent = SourceComponent;
        return true;
    }
    // PIE: prove the source duplicate by persistent ActorGuid in the exact
    // PIE World, then match the Component slot (name and class).
    AActor* SourceActor = SourceComponent->GetOwner();
    if (SourceActor == nullptr)
    {
        OutError = TEXT(
            "The typed-PCG source Component has no owner Actor.");
        return false;
    }
    const FGuid SourceGuid = SourceActor->GetActorGuid();
    const FName ComponentName = SourceComponent->GetFName();
    for (const ULevel* Level : World->GetLevels())
    {
        if (Level == nullptr)
        {
            continue;
        }
        for (const AActor* Actor : Level->Actors)
        {
            if (Actor == nullptr
                || !IsValid(Actor)
                || Actor->GetActorGuid() != SourceGuid)
            {
                continue;
            }
            for (UActorComponent* Component : Actor->GetComponents())
            {
                UPCGComponent* Duplicate =
                    Cast<UPCGComponent>(Component);
                if (Duplicate != nullptr
                    && IsValid(Duplicate)
                    && Duplicate->GetFName() == ComponentName)
                {
                    OutComponent = Duplicate;
                    return true;
                }
            }
        }
    }
    OutError = TEXT(
        "No live PIE duplicate of the typed-PCG source Component exists in "
        "the exact PIE World.");
    return false;
}

FTypedPcgTicketPtr FTypedPcgWorldRegistry::Prepare(
    const FTypedPcgWorldSelector& Selector,
    const FString& SourceTargetText,
    FString& OutError)
{
    OutError.Reset();
    if (!Selector.IsValid())
    {
        OutError = TEXT(
            "The typed-PCG World selector is invalid.");
        return nullptr;
    }
    UWorld* World = nullptr;
    if (!ResolveWorld(Selector, World, OutError))
    {
        return nullptr;
    }
    UPCGComponent* Component = nullptr;
    if (!ResolveComponentForWorld(
            Selector,
            World,
            SourceTargetText,
            Component,
            OutError))
    {
        return nullptr;
    }

    FTypedPcgTicketPtr Ticket =
        MakeShared<FTypedPcgTicket, ESPMode::ThreadSafe>();
    Ticket->Id = TEXT("pcg_")
        + FGuid::NewGuid().ToString(EGuidFormats::Digits).ToLower();
    Ticket->SelectorText = Selector.NormalizeText();
    Ticket->WorldPath = World->GetPathName();
    Ticket->ComponentPath = Component->GetPathName();
    Ticket->SourceTarget = SourceTargetText;
    Ticket->IssuedAtSeconds = FPlatformTime::Seconds();
    {
        FScopeLock Lock(&Mutex);
        if (!bInitialized)
        {
            OutError = TEXT(
                "The typed-PCG World registry is not initialized.");
            return nullptr;
        }
        Ticket->Epoch = NextEpochLocked(Ticket->WorldPath);
        Ticket->World = World;
        Tickets.Add(Ticket->Id, Ticket);
    }
    return Ticket;
}

bool FTypedPcgWorldRegistry::Validate(
    const FTypedPcgWorldSelector& Selector,
    const FTypedPcgTicketPtr& Ticket,
    const FString& SourceTargetText,
    UPCGComponent*& OutComponent,
    FString& OutError)
{
    OutComponent = nullptr;
    OutError.Reset();
    if (!Selector.IsValid() || !Ticket.IsValid())
    {
        OutError = TEXT(
            "A typed-PCG selector and source-bound ticket are required.");
        return false;
    }
    FTypedPcgTicketPtr Stored;
    uint64 CurrentEpoch = 0;
    {
        FScopeLock Lock(&Mutex);
        if (!bInitialized)
        {
            OutError = TEXT(
                "The typed-PCG World registry is not initialized.");
            return false;
        }
        Stored = Tickets.FindRef(Ticket->Id);
        const uint64* FoundEpoch = Epochs.Find(Ticket->WorldPath);
        CurrentEpoch = FoundEpoch != nullptr ? *FoundEpoch : 0;
    }
    if (!Stored.IsValid() || Stored != Ticket)
    {
        OutError = TEXT(
            "The typed-PCG World ticket is unknown to this runtime.");
        return false;
    }
    if (FPlatformTime::Seconds() - Ticket->IssuedAtSeconds
        > TicketLifetimeSeconds)
    {
        OutError = TEXT(
            "The typed-PCG World ticket has expired.");
        return false;
    }
    if (Ticket->SelectorText != Selector.NormalizeText())
    {
        OutError = TEXT(
            "The typed-PCG World ticket selector does not match the request.");
        return false;
    }
    if (Ticket->SourceTarget != SourceTargetText)
    {
        OutError = TEXT(
            "The typed-PCG World ticket source Target does not match the "
            "request.");
        return false;
    }
    if (Ticket->Epoch != CurrentEpoch)
    {
        OutError = TEXT(
            "The typed-PCG World ticket is stale: the World incarnation "
            "changed since issue.");
        return false;
    }
    UWorld* World = Ticket->World.Get();
    if (World == nullptr || World->GetPathName() != Ticket->WorldPath)
    {
        OutError = TEXT(
            "The typed-PCG World ticket is stale: the bound World is gone or "
            "replaced.");
        return false;
    }
    UPCGComponent* Component = nullptr;
    if (!ResolveComponentForWorld(
            Selector,
            World,
            SourceTargetText,
            Component,
            OutError))
    {
        OutError = TEXT(
            "The typed-PCG World ticket is stale: the source Component "
            "incarnation no longer resolves.");
        return false;
    }
    if (Component->GetPathName() != Ticket->ComponentPath)
    {
        OutError = TEXT(
            "The typed-PCG World ticket is stale: the Component incarnation "
            "was reconstructed.");
        return false;
    }
    OutComponent = Component;
    return true;
}

void FTypedPcgWorldRegistry::InvalidateAll()
{
    FScopeLock Lock(&Mutex);
    Tickets.Reset();
    for (TPair<FString, uint64>& Pair : Epochs)
    {
        ++Pair.Value;
    }
}
}
