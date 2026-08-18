// Copyright 2026 Loomle contributors.

#include "SalLevelInterface.h"

#include "SalLevelComponentLocator.h"

#include "../SalDiagnostics.h"
#include "../SalObjectBuilder.h"
#include "../SalResultTargets.h"
#include "../SalRuntime.h"
#include "../SalTargetResolver.h"
#include "AssetRegistry/AssetData.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "ComponentTypeRegistry.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Editor.h"
#include "Editor/PlacementMode/Public/IPlacementModeModule.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Engine/Level.h"
#include "Engine/LevelScriptActor.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "GameFramework/Actor.h"
#include "Helpers/PCGHelpers.h"
#include "Kismet2/ComponentEditorUtils.h"
#include "LevelInstance/LevelInstanceActor.h"
#include "LevelInstance/LevelInstanceInterface.h"
#include "LevelInstance/LevelInstanceSubsystem.h"
#include "Misc/PackageName.h"
#include "Modules/ModuleManager.h"
#include "PCGComponent.h"
#include "SComponentClassCombo.h"
#include "UObject/Package.h"
#include "UObject/SoftObjectPath.h"
#include "UObject/UnrealType.h"
#include "Misc/SecureHash.h"
#include "UObject/UObjectGlobals.h"
#include "WorldPartition/ActorDescContainerInstance.h"
#include "WorldPartition/WorldPartition.h"
#include "WorldPartition/WorldPartitionActorDesc.h"
#include "WorldPartition/WorldPartitionActorDescInstance.h"

namespace Loomle::Sal
{
namespace
{
constexpr int32 DefaultCollectionLimit = 50;
constexpr int32 MaxCollectionLimit = 200;
constexpr int32 MaxQueryDiagnostics = 64;
constexpr int32 MaxIdentityConflictMatches = 32;
constexpr int32 MaxLevelExaminedCandidates = 100000;
constexpr int32 MaxLevelInstanceCursorSources = 4096;
constexpr int64 MaxLevelSnapshotStringBytes = 64ll * 1024ll * 1024ll;
constexpr EObjectFlags IncompleteLoadFlags =
    RF_NeedLoad
    | RF_NeedPostLoad
    | RF_NeedPostLoadSubobjects
    | RF_WillBeLoaded;

FString GuidText(const FGuid& Guid)
{
    return Guid.ToString(EGuidFormats::DigitsWithHyphensLower);
}

TSharedPtr<FJsonObject> QueryError(
    const FString& Code,
    const FString& Message,
    const FString& Operation,
    const FString& Ref = FString(),
    const TArray<FString>& Supported = {})
{
    FSalDiagnosticBuilder Diagnostic = FSalDiagnostics::Error(Code, Message)
        .Interface(TEXT("level"))
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
    const FString& Code,
    const FString& Message,
    const FString& Operation,
    const FString& Ref = FString())
{
    FSalDiagnosticBuilder Diagnostic = FSalDiagnostics::Warning(Code, Message)
        .Interface(TEXT("level"))
        .Operation(Operation);
    if (!Ref.IsEmpty())
    {
        Diagnostic.Ref(Ref);
    }
    return Diagnostic.Build();
}

FString TargetType(const FSalResolvedTarget& Target)
{
    FString Type;
    if (Target.CanonicalTarget.IsValid())
    {
        Target.CanonicalTarget->TryGetStringField(TEXT("type"), Type);
    }
    return Type.IsEmpty()
        ? UWorld::StaticClass()->GetPathName()
        : Type;
}

FString TargetName(const FSalResolvedTarget& Target)
{
    if (!Target.Name.IsEmpty())
    {
        return Target.Name;
    }
    const FString Name = FPackageName::ObjectPathToObjectName(Target.AssetPath);
    return Name.IsEmpty() ? TEXT("Level") : Name;
}

bool IsExactLoadedSource(
    const FSalResolvedTarget& Target,
    UWorld*& OutWorld,
    ULevel*& OutLevel,
    FString& OutReason)
{
    OutWorld = nullptr;
    OutLevel = nullptr;
    OutReason.Reset();

    UWorld* World = Target.Domain == ESalDomain::Level
            && IsValid(Target.Object)
        ? Cast<UWorld>(Target.Object)
        : nullptr;
    if (World == nullptr)
    {
        OutReason = TEXT("The exact saved Level Target is not loaded as an authored Editor source World.");
        return false;
    }
    if (Target.AssetPath.IsEmpty()
        || !World->GetPathName().Equals(
            Target.AssetPath,
            ESearchCase::CaseSensitive))
    {
        OutReason = TEXT("The loaded World does not exactly match the canonical Level Target path.");
        return false;
    }
    if (World->HasAnyFlags(IncompleteLoadFlags)
        || (World->WorldType != EWorldType::Editor
            && World->WorldType != EWorldType::Inactive))
    {
        OutReason = TEXT("Level content Query accepts only an Editor source World or an inactive source World whose PersistentLevel belongs to the active Editor World; PIE, game, and preview Worlds are rejected.");
        return false;
    }
    if (World->IsTemplate()
        || World->HasAnyFlags(RF_Transient)
        || World->GetOutermost() == GetTransientPackage()
        || World->GetOutermost()->HasAnyFlags(RF_Transient)
        || World->GetOutermost()->HasAnyPackageFlags(PKG_PlayInEditor))
    {
        OutReason = TEXT("The loaded World is transient, a template, or a PIE package rather than the saved authored source World.");
        return false;
    }
    ULevel* Level = World->PersistentLevel;
    if (!IsValid(Level)
        || Level->HasAnyFlags(IncompleteLoadFlags)
        || Level->GetTypedOuter<UWorld>() != World
        || Level->GetOutermost() != World->GetOutermost())
    {
        OutReason = TEXT("The loaded source World has no exact package-owned PersistentLevel.");
        return false;
    }
    UWorld* OwningWorld = Level->GetWorld();
    UWorld* ActiveEditorWorld = GEditor != nullptr
        ? GEditor->GetEditorWorldContext().World()
        : nullptr;
    if (!IsValid(OwningWorld)
        || OwningWorld->HasAnyFlags(IncompleteLoadFlags)
        || OwningWorld != ActiveEditorWorld
        || OwningWorld->WorldType != EWorldType::Editor)
    {
        OutReason = TEXT("The exact source PersistentLevel does not belong to the active authored Editor World.");
        return false;
    }

    OutWorld = World;
    OutLevel = Level;
    return true;
}

bool IsPersistedLevelActor(const AActor* Actor, const ULevel* Level)
{
    if (!IsValid(Actor)
        || Level == nullptr
        || Actor->GetLevel() != Level
        || Actor->IsChildActor()
        || Actor->GetActorInstanceGuid() != Actor->GetActorGuid()
        || Actor->HasExternalContent()
        || Actor->ActorHasTag(PCGHelpers::DefaultPCGActorTag)
        || Actor->ActorHasTag(PCGHelpers::MarkedForCleanupPCGTag)
        || Actor->IsTemplate()
        || Actor->HasAnyFlags(
            RF_Transient
            | RF_ClassDefaultObject
            | RF_ArchetypeObject))
    {
        return false;
    }
    const UPackage* Package = Actor->GetOutermost();
    return Package != nullptr
        && Package != GetTransientPackage()
        && !Package->HasAnyFlags(RF_Transient)
        && !Package->HasAnyPackageFlags(PKG_PlayInEditor);
}

bool IsExcludedDescriptor(const FWorldPartitionActorDesc* ActorDesc)
{
    if (ActorDesc == nullptr)
    {
        return true;
    }
    const TArray<FName>& Tags = ActorDesc->GetTags();
    return Tags.Contains(PCGHelpers::DefaultPCGActorTag)
        || Tags.Contains(PCGHelpers::MarkedForCleanupPCGTag);
}

bool IsAuthoredLevelInstanceClass(const UClass* ActorClass)
{
    return ActorClass != nullptr
        && ActorClass->IsChildOf(ALevelInstance::StaticClass());
}

bool TryReadLoadedLevelInstanceSource(
    const AActor* Actor,
    FString& OutLocator,
    bool& bOutLocatorIsObjectPath)
{
    OutLocator.Reset();
    bOutLocatorIsObjectPath = false;
    if (Actor == nullptr
        || !IsAuthoredLevelInstanceClass(Actor->GetClass()))
    {
        return false;
    }
    const ILevelInstanceInterface* LevelInstance =
        Cast<ILevelInstanceInterface>(Actor);
    if (LevelInstance == nullptr)
    {
        return false;
    }
    const FSoftObjectPath SourcePath =
        LevelInstance->GetWorldAsset().ToSoftObjectPath();
    if (!SourcePath.IsValid()
        || !SourcePath.GetSubPathString().IsEmpty())
    {
        return false;
    }
    OutLocator = SourcePath.ToString();
    bOutLocatorIsObjectPath = true;
    return !OutLocator.IsEmpty();
}

bool TryReadDescriptorLevelInstanceSource(
    const FWorldPartitionActorDesc* ActorDesc,
    FString& OutLocator,
    bool& bOutLocatorIsObjectPath)
{
    OutLocator.Reset();
    bOutLocatorIsObjectPath = false;
    if (ActorDesc == nullptr
        || !IsAuthoredLevelInstanceClass(
            ActorDesc->GetActorNativeClass()))
    {
        return false;
    }
    const FName SourcePackage = ActorDesc->GetChildContainerPackage();
    if (SourcePackage.IsNone())
    {
        return false;
    }
    OutLocator = SourcePackage.ToString();
    return FPackageName::IsValidLongPackageName(OutLocator)
        && !FPackageName::IsTempPackage(OutLocator);
}

TSharedPtr<FJsonObject> LevelTargetValue(const FString& AssetPath)
{
    if (AssetPath.IsEmpty())
    {
        return nullptr;
    }
    TSharedPtr<FJsonObject> Target = MakeShared<FJsonObject>();
    Target->SetStringField(TEXT("kind"), TEXT("target"));
    Target->SetStringField(TEXT("domain"), TEXT("level"));
    Target->SetStringField(TEXT("asset"), AssetPath);
    Target->SetStringField(
        TEXT("type"),
        UWorld::StaticClass()->GetPathName());
    return Target;
}

TSharedPtr<FJsonObject> AssetTargetValue(
    const FSalResolvedTarget& LevelTarget)
{
    if (LevelTarget.AssetPath.IsEmpty())
    {
        return nullptr;
    }
    TSharedPtr<FJsonObject> Target = MakeShared<FJsonObject>();
    Target->SetStringField(TEXT("kind"), TEXT("target"));
    Target->SetStringField(TEXT("domain"), TEXT("asset"));
    Target->SetStringField(TEXT("path"), LevelTarget.AssetPath);
    Target->SetStringField(TEXT("type"), TargetType(LevelTarget));
    return Target;
}

bool IsValidDeclarationClass(const UClass* Class)
{
    if (!IsValid(Class)
        || Class->HasAnyFlags(
            IncompleteLoadFlags
            | RF_NewerVersionExists)
        || Class->HasAnyClassFlags(CLASS_NewerVersionExists)
        // Native UClass singletons are intentionally RF_Transient even though
        // their /Script identity is durable. Transient generated Classes are
        // reconstruction products and must never become navigation Targets.
        || (Class->HasAnyFlags(RF_Transient)
            && !Class->HasAnyClassFlags(CLASS_Native))
        || Class->GetName().StartsWith(TEXT("SKEL_"))
        || Class->GetName().StartsWith(TEXT("REINST_")))
    {
        return false;
    }
    const UPackage* Package = Class->GetOutermost();
    return Package != nullptr
        && Package != GetTransientPackage()
        && !Package->HasAnyFlags(RF_Transient)
        && !Package->HasAnyPackageFlags(PKG_PlayInEditor);
}

TSharedPtr<FJsonObject> ClassTargetValue(const UClass* Class)
{
    if (!IsValidDeclarationClass(Class))
    {
        return nullptr;
    }
    const FString Path = Class->GetPathName();
    if (Path.IsEmpty())
    {
        return nullptr;
    }
    TSharedPtr<FJsonObject> Target = MakeShared<FJsonObject>();
    Target->SetStringField(TEXT("kind"), TEXT("target"));
    Target->SetStringField(TEXT("domain"), TEXT("class"));
    Target->SetStringField(TEXT("path"), Path);
    return Target;
}

UBlueprint* ProvenGeneratingBlueprint(
    UClass* CandidateClass,
    FString& OutContainerPath)
{
    OutContainerPath.Reset();
    UBlueprintGeneratedClass* GeneratedClass =
        Cast<UBlueprintGeneratedClass>(CandidateClass);
    UBlueprint* Blueprint = GeneratedClass != nullptr
        ? Cast<UBlueprint>(GeneratedClass->ClassGeneratedBy)
        : nullptr;
    if (!IsValidDeclarationClass(GeneratedClass)
        || !IsValid(Blueprint)
        || Blueprint->HasAnyFlags(
            IncompleteLoadFlags
            | RF_Transient
            | RF_NewerVersionExists)
        || Blueprint->GeneratedClass != GeneratedClass
        || !Blueprint->GetBlueprintGuid().IsValid())
    {
        return nullptr;
    }

    UObject* Container = Blueprint;
    while (Container != nullptr && !Container->IsAsset())
    {
        Container = Container->GetOuter();
    }
    if (!IsValid(Container)
        || Container->HasAnyFlags(IncompleteLoadFlags | RF_Transient)
        || Container->GetOutermost() == GetTransientPackage()
        || Container->GetOutermost()->HasAnyFlags(RF_Transient)
        || Container->GetOutermost()->HasAnyPackageFlags(PKG_PlayInEditor)
        || !(Blueprint == Container || Blueprint->IsIn(Container)))
    {
        return nullptr;
    }
    OutContainerPath = Container->GetPathName();
    if (OutContainerPath.IsEmpty())
    {
        OutContainerPath.Reset();
        return nullptr;
    }
    return Blueprint;
}

UClass* FindLoadedClassInActorHierarchy(
    const AActor* Actor,
    const FString& ExactPath)
{
    constexpr int32 MaxClassDepth = 256;
    TSet<const UClass*> Seen;
    UClass* Match = nullptr;
    int32 MatchCount = 0;
    int32 Depth = 0;
    for (UClass* Current = Actor != nullptr
             ? Actor->GetClass()
             : nullptr;
         Current != nullptr;
         Current = Current->GetSuperClass())
    {
        if (++Depth > MaxClassDepth
            || Seen.Contains(Current)
            || !IsValidDeclarationClass(Current))
        {
            return nullptr;
        }
        Seen.Add(Current);
        if (Current->GetPathName() == ExactPath)
        {
            Match = Current;
            ++MatchCount;
        }
    }
    return MatchCount == 1 ? Match : nullptr;
}

bool ResultHasErrorDiagnostic(
    const TSharedPtr<FJsonObject>& Result)
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
        if (!Value.IsValid()
            || !Value->TryGetObject(Diagnostic)
            || Diagnostic == nullptr
            || !(*Diagnostic).IsValid()
            || !(*Diagnostic)->TryGetStringField(
                TEXT("severity"),
                Severity)
            || Severity == TEXT("error"))
        {
            return true;
        }
    }
    return false;
}

void AddSourceAssetHandoff(
    const TSharedPtr<FJsonObject>& Result,
    const FSalResolvedTarget& LevelTarget)
{
    if (ResultHasErrorDiagnostic(Result))
    {
        return;
    }
    ResultTargets::AddHandoff(
        Result,
        AssetTargetValue(LevelTarget),
        TargetName(LevelTarget) + TEXT("_asset"),
        TEXT("inspect_asset"));
}

void AddDeclarationHandoffs(
    const TSharedPtr<FJsonObject>& Result,
    UClass* ActualClass,
    UClass* BlueprintDeclaringClass = nullptr,
    const bool bUseExactBlueprintDeclaringClass = false)
{
    if (ResultHasErrorDiagnostic(Result))
    {
        return;
    }

    const TSharedPtr<FJsonObject> ClassTarget =
        ClassTargetValue(ActualClass);
    if (ClassTarget.IsValid())
    {
        ResultTargets::AddHandoff(
            Result,
            ClassTarget,
            ActualClass->GetName() + TEXT("_class"),
            TEXT("inspect_class"));
    }

    FString ContainerPath;
    UBlueprint* Blueprint = ProvenGeneratingBlueprint(
        bUseExactBlueprintDeclaringClass
            ? BlueprintDeclaringClass
            : ActualClass,
        ContainerPath);
    if (Blueprint != nullptr)
    {
        ResultTargets::AddHandoff(
            Result,
            ResultTargets::Blueprint(
                ContainerPath,
                GuidText(Blueprint->GetBlueprintGuid())),
            Blueprint->GetName() + TEXT("_blueprint"),
            TEXT("inspect_blueprint"));
    }
}

struct FLevelActorEntry
{
    FGuid Guid;
    TWeakObjectPtr<AActor> Actor;
    FString Type;
    FString Name;
    FString Label;
    FString ObjectPath;
    FString PackagePath;
    FTransform Transform = FTransform::Identity;
    FBox Bounds = FBox(ForceInit);
    bool bLoaded = false;
    bool bHasDescriptor = false;
    bool bExternal = false;
    bool bHasTransform = false;
    bool bHasBounds = false;
    bool bLevelInstance = false;
    bool bLevelInstanceSourceIsObjectPath = false;
    FString LevelInstanceSourceLocator;
    FString LevelInstanceSourceAsset;
    FString LevelInstanceSourceFingerprint;
    int32 IdentityMultiplicity = 0;

    FString EvidenceKey() const
    {
        if (!ObjectPath.IsEmpty())
        {
            return ObjectPath;
        }
        if (!PackagePath.IsEmpty())
        {
            return PackagePath + TEXT(":") + Name;
        }
        if (!Name.IsEmpty())
        {
            return Name;
        }
        return Type.IsEmpty() ? TEXT("<unknown actor>") : Type;
    }

};

struct FPendingLevelInstanceHandoff
{
    TSharedPtr<FJsonObject> ActorFields;
    TSharedPtr<FJsonObject> Target;
    FString PreferredAlias;
};

struct FPendingPcgComponentHandoff
{
    TSharedPtr<FJsonObject> ComponentFields;
    TSharedPtr<FJsonObject> Target;
    FString PreferredAlias;
};

struct FLevelSnapshot
{
    UWorld* World = nullptr;
    ULevel* Level = nullptr;
    TArray<FLevelActorEntry> Actors;
    TArray<TSharedPtr<FJsonObject>> Diagnostics;
    int32 RootDescriptorCount = 0;
    int32 InvalidGuidCount = 0;
    int32 DuplicateGuidCount = 0;
    int32 StableActorCount = 0;
    int32 ComponentCount = 0;
    int32 NativeComponentCount = 0;
    int32 SCSComponentCount = 0;
    int32 InstanceComponentCount = 0;
    int32 PCGComponentCount = 0;
    bool bIdentityComplete = true;
    bool bComponentIdentityComplete = true;
    bool bDiagnosticsTruncated = false;

    void AddDiagnostic(const TSharedPtr<FJsonObject>& Diagnostic)
    {
        if (Diagnostics.Num() < MaxQueryDiagnostics)
        {
            Diagnostics.Add(Diagnostic);
        }
        else
        {
            bDiagnosticsTruncated = true;
        }
    }

    TArray<TSharedPtr<FJsonObject>> FinalDiagnostics(
        const FString& Operation) const
    {
        TArray<TSharedPtr<FJsonObject>> Result = Diagnostics;
        if (bDiagnosticsTruncated && Result.Num() < MaxQueryDiagnostics + 1)
        {
            Result.Add(Warning(
                TEXT("validation.reference_scan_incomplete"),
                TEXT("Additional Level identity diagnostics were omitted after the bounded diagnostic limit."),
                Operation));
        }
        return Result;
    }
};

FLevelActorEntry LoadedActorEntry(AActor* Actor)
{
    FLevelActorEntry Entry;
    Entry.Guid = Actor->GetActorGuid();
    Entry.Actor = Actor;
    Entry.Type = Actor->GetClass()->GetPathName();
    Entry.Name = Actor->GetFName().ToString();
#if WITH_EDITOR
    Entry.Label = Actor->GetActorLabel(false);
#endif
    Entry.ObjectPath = Actor->GetPathName();
    Entry.PackagePath = Actor->GetOutermost()->GetName();
    Entry.Transform = Actor->GetActorTransform();
    Entry.bLoaded = true;
    Entry.bExternal = Actor->IsPackageExternal();
    Entry.bHasTransform = true;
    Entry.bLevelInstance = IsAuthoredLevelInstanceClass(Actor->GetClass());
    if (Entry.bLevelInstance)
    {
        TryReadLoadedLevelInstanceSource(
            Actor,
            Entry.LevelInstanceSourceLocator,
            Entry.bLevelInstanceSourceIsObjectPath);
    }
    return Entry;
}

FLevelActorEntry DescriptorEntry(
    const FWorldPartitionActorDesc* ActorDesc,
    const FName SourcePackage,
    FString ObjectPath)
{
    FLevelActorEntry Entry;
    Entry.Guid = ActorDesc->GetGuid();
    const FTopLevelAssetPath BaseClass = ActorDesc->GetBaseClass();
    Entry.Type = (BaseClass.IsValid()
        ? BaseClass
        : ActorDesc->GetNativeClass()).ToString();
    Entry.Name = ActorDesc->GetActorNameString();
    Entry.Label = ActorDesc->GetActorLabelString();
    Entry.ObjectPath = MoveTemp(ObjectPath);
    Entry.PackagePath = ActorDesc->GetActorPackage().ToString();
    // Slice 1A reports unloaded descriptor evidence in the exact source
    // asset's coordinate space. Do not apply a live container-instance
    // transform, and do not expand the current contract with descriptor
    // Transform: only the raw authored bounds are emitted.
    Entry.Bounds = ActorDesc->GetEditorBounds();
    Entry.bHasDescriptor = true;
    Entry.bExternal = ActorDesc->GetActorPackage() != SourcePackage;
    Entry.bHasBounds = Entry.Bounds.IsValid != 0;
    Entry.bLevelInstance = IsAuthoredLevelInstanceClass(
        ActorDesc->GetActorNativeClass());
    if (Entry.bLevelInstance)
    {
        TryReadDescriptorLevelInstanceSource(
            ActorDesc,
            Entry.LevelInstanceSourceLocator,
            Entry.bLevelInstanceSourceIsObjectPath);
    }
    return Entry;
}

class FLevelScanBudget
{
public:
    bool TryExamineCandidate()
    {
        if (ExaminedCandidates >= MaxLevelExaminedCandidates)
        {
            return false;
        }
        ++ExaminedCandidates;
        return true;
    }

    bool TryConsumeString(const FString& Value)
    {
        return TryConsumeBytes(StringStorageBytes(Value));
    }

    bool TryConsumeEntryStrings(
        const FLevelActorEntry& Entry,
        const int32 ObjectPathCopies,
        const bool bIncludeName = true,
        const bool bIncludeLabel = true)
    {
        int64 Bytes = StringStorageBytes(Entry.Type)
            + StringStorageBytes(Entry.PackagePath)
            + StringStorageBytes(Entry.LevelInstanceSourceLocator)
            + StringStorageBytes(Entry.LevelInstanceSourceAsset);
        if (bIncludeName)
        {
            Bytes += StringStorageBytes(Entry.Name);
        }
        if (bIncludeLabel)
        {
            Bytes += StringStorageBytes(Entry.Label);
        }
        for (int32 Copy = 0; Copy < ObjectPathCopies; ++Copy)
        {
            Bytes += StringStorageBytes(Entry.ObjectPath);
        }
        return TryConsumeBytes(Bytes);
    }

private:
    static int64 StringStorageBytes(const FString& Value)
    {
        return Value.IsEmpty()
            ? 0
            : (static_cast<int64>(Value.Len()) + 1) * sizeof(TCHAR);
    }

    bool TryConsumeBytes(const int64 Bytes)
    {
        if (Bytes < 0
            || Bytes > MaxLevelSnapshotStringBytes - SnapshotStringBytes)
        {
            return false;
        }
        SnapshotStringBytes += Bytes;
        return true;
    }

    int32 ExaminedCandidates = 0;
    int64 SnapshotStringBytes = 0;
};

enum class ELevelInstanceSourceResolution : uint8
{
    NotLevelInstance,
    Resolved,
    Missing,
    Invalid,
    Ambiguous,
    SelfReference
};

ELevelInstanceSourceResolution ResolveLevelInstanceSource(
    const FLevelActorEntry& Entry,
    const FString& MainAssetPath,
    const IAssetRegistry& Registry,
    FString& OutAssetPath)
{
    OutAssetPath.Reset();
    if (!Entry.bLevelInstance)
    {
        return ELevelInstanceSourceResolution::NotLevelInstance;
    }
    if (Entry.LevelInstanceSourceLocator.IsEmpty())
    {
        return ELevelInstanceSourceResolution::Missing;
    }

    const FTopLevelAssetPath WorldClassPath =
        UWorld::StaticClass()->GetClassPathName();
    FAssetData SourceData;
    if (Entry.bLevelInstanceSourceIsObjectPath)
    {
        const FSoftObjectPath SourcePath(Entry.LevelInstanceSourceLocator);
        if (!SourcePath.IsValid()
            || !SourcePath.GetSubPathString().IsEmpty())
        {
            return ELevelInstanceSourceResolution::Invalid;
        }
        const FString SourcePackage = SourcePath.GetLongPackageName();
        if (!FPackageName::IsValidLongPackageName(SourcePackage)
            || FPackageName::IsTempPackage(SourcePackage))
        {
            return ELevelInstanceSourceResolution::Invalid;
        }
        SourceData = Registry.GetAssetByObjectPath(SourcePath, true);
    }
    else
    {
        if (!FPackageName::IsValidLongPackageName(
                Entry.LevelInstanceSourceLocator)
            || FPackageName::IsTempPackage(
                Entry.LevelInstanceSourceLocator))
        {
            return ELevelInstanceSourceResolution::Invalid;
        }
        TArray<FAssetData> PackageAssets;
        Registry.GetAssetsByPackageName(
            FName(*Entry.LevelInstanceSourceLocator),
            PackageAssets,
            true);
        for (const FAssetData& Candidate : PackageAssets)
        {
            if (!Candidate.IsValid()
                || !Candidate.IsTopLevelAsset()
                || Candidate.AssetClassPath != WorldClassPath)
            {
                continue;
            }
            if (SourceData.IsValid())
            {
                return ELevelInstanceSourceResolution::Ambiguous;
            }
            SourceData = Candidate;
        }
    }
    if (!SourceData.IsValid())
    {
        return ELevelInstanceSourceResolution::Missing;
    }
    if (!SourceData.IsTopLevelAsset()
        || SourceData.AssetClassPath != WorldClassPath
        || !FPackageName::IsValidLongPackageName(
            SourceData.PackageName.ToString())
        || FPackageName::IsTempPackage(
            SourceData.PackageName.ToString()))
    {
        return ELevelInstanceSourceResolution::Invalid;
    }

    OutAssetPath = SourceData.GetSoftObjectPath().ToString();
    if (OutAssetPath.IsEmpty())
    {
        return ELevelInstanceSourceResolution::Invalid;
    }
    if (OutAssetPath == MainAssetPath)
    {
        OutAssetPath.Reset();
        return ELevelInstanceSourceResolution::SelfReference;
    }
    return ELevelInstanceSourceResolution::Resolved;
}

FString LevelInstanceResolutionText(
    const ELevelInstanceSourceResolution Resolution)
{
    switch (Resolution)
    {
    case ELevelInstanceSourceResolution::NotLevelInstance:
        return TEXT("not_level_instance");
    case ELevelInstanceSourceResolution::Resolved:
        return TEXT("resolved");
    case ELevelInstanceSourceResolution::Missing:
        return TEXT("missing");
    case ELevelInstanceSourceResolution::Invalid:
        return TEXT("invalid");
    case ELevelInstanceSourceResolution::Ambiguous:
        return TEXT("ambiguous");
    case ELevelInstanceSourceResolution::SelfReference:
        return TEXT("self_reference");
    default:
        return TEXT("invalid");
    }
}

FString MakeLevelInstanceSourceFingerprint(
    const ELevelInstanceSourceResolution Resolution,
    const FString& AssetPath)
{
    const FString Evidence = LevelInstanceResolutionText(Resolution)
        + TEXT("\n")
        + AssetPath;
    FSHA1 Hash;
    Hash.UpdateWithString(*Evidence, Evidence.Len());
    Hash.Final();
    uint8 Digest[FSHA1::DigestSize];
    Hash.GetHash(Digest);
    return BytesToHex(Digest, UE_ARRAY_COUNT(Digest)).ToLower();
}

bool PrepareLevelInstanceCursorEvidence(
    FLevelSnapshot& Snapshot,
    const FSalResolvedTarget& Target,
    FString& OutReason)
{
    OutReason.Reset();
    const IAssetRegistry& Registry =
        FModuleManager::LoadModuleChecked<FAssetRegistryModule>(
            TEXT("AssetRegistry"))
            .Get();
    TMap<FString, FString> FingerprintsByLocator;
    for (FLevelActorEntry& Entry : Snapshot.Actors)
    {
        if (!Entry.bLevelInstance)
        {
            continue;
        }
        const FString LocatorKey =
            (Entry.bLevelInstanceSourceIsObjectPath
                ? TEXT("object|")
                : TEXT("package|"))
            + Entry.LevelInstanceSourceLocator;
        if (const FString* Existing =
                FingerprintsByLocator.Find(LocatorKey))
        {
            Entry.LevelInstanceSourceFingerprint = *Existing;
            continue;
        }
        if (FingerprintsByLocator.Num()
            >= MaxLevelInstanceCursorSources)
        {
            OutReason = FString::Printf(
                TEXT("Level Actor pagination cannot bind more than %d distinct Level Instance source locators in one exact snapshot."),
                MaxLevelInstanceCursorSources);
            return false;
        }
        FString SourceAsset;
        const ELevelInstanceSourceResolution Resolution =
            ResolveLevelInstanceSource(
                Entry,
                Target.AssetPath,
                Registry,
                SourceAsset);
        const FString Fingerprint =
            MakeLevelInstanceSourceFingerprint(
                Resolution,
                SourceAsset);
        FingerprintsByLocator.Add(LocatorKey, Fingerprint);
        Entry.LevelInstanceSourceFingerprint = Fingerprint;
    }
    return true;
}

void AuditIdentities(
    FLevelSnapshot& Snapshot,
    const FString& Operation)
{
    TMap<FGuid, TArray<int32>> ByGuid;
    for (int32 Index = 0; Index < Snapshot.Actors.Num(); ++Index)
    {
        FLevelActorEntry& Entry = Snapshot.Actors[Index];
        if (!Entry.Guid.IsValid())
        {
            ++Snapshot.InvalidGuidCount;
            Snapshot.AddDiagnostic(Warning(
                TEXT("resolution.identity_conflict"),
                TEXT("A persisted Level Actor or root World Partition descriptor has an invalid ActorGuid and cannot receive a StableRef."),
                Operation,
                Entry.EvidenceKey()));
            continue;
        }
        ByGuid.FindOrAdd(Entry.Guid).Add(Index);
    }

    for (const TPair<FGuid, TArray<int32>>& Pair : ByGuid)
    {
        const int32 Multiplicity = Pair.Value.Num();
        for (const int32 Index : Pair.Value)
        {
            Snapshot.Actors[Index].IdentityMultiplicity = Multiplicity;
        }
        if (Multiplicity == 1)
        {
            if (Snapshot.bIdentityComplete)
            {
                ++Snapshot.StableActorCount;
            }
            continue;
        }

        ++Snapshot.DuplicateGuidCount;
        TArray<FString> Matches;
        Matches.Reserve(FMath::Min(
            Multiplicity,
            MaxIdentityConflictMatches));
        for (const int32 Index : Pair.Value)
        {
            FString Evidence = Snapshot.Actors[Index].EvidenceKey();
            if (Matches.Num() < MaxIdentityConflictMatches)
            {
                Matches.Add(MoveTemp(Evidence));
                continue;
            }
            int32 GreatestIndex = 0;
            for (int32 MatchIndex = 1;
                 MatchIndex < Matches.Num();
                 ++MatchIndex)
            {
                if (Matches[GreatestIndex].Compare(Matches[MatchIndex]) < 0)
                {
                    GreatestIndex = MatchIndex;
                }
            }
            if (Evidence.Compare(Matches[GreatestIndex]) < 0)
            {
                Matches[GreatestIndex] = MoveTemp(Evidence);
            }
        }
        Matches.Sort();
        const int32 OmittedMatches = Multiplicity - Matches.Num();
        const FString Message = OmittedMatches > 0
            ? FString::Printf(
                TEXT("Multiple persisted Level Actor records share one ActorGuid; none can receive that StableRef. A bounded deterministic evidence subset is shown and %d additional matches were omitted."),
                OmittedMatches)
            : TEXT("Multiple persisted Level Actor records share one ActorGuid; none can receive that StableRef.");
        FSalDiagnosticBuilder Diagnostic = FSalDiagnostics::Warning(
                TEXT("resolution.identity_conflict"),
                Message)
            .Interface(TEXT("level"))
            .Operation(Operation)
            .Ref(GuidText(Pair.Key))
            .Matches(Matches);
        Snapshot.AddDiagnostic(Diagnostic.Build());
    }
}

void ResolveLevelInstanceSourceForEntry(
    FLevelSnapshot& Snapshot,
    FLevelActorEntry& Entry,
    const FSalResolvedTarget& Target,
    FLevelScanBudget& ScanBudget,
    const FString& Operation,
    const IAssetRegistry& Registry)
{
    if (!Entry.bLevelInstance)
    {
        return;
    }
    FString SourceAsset;
    const ELevelInstanceSourceResolution Resolution =
        ResolveLevelInstanceSource(
            Entry,
            Target.AssetPath,
            Registry,
            SourceAsset);
    if (Resolution == ELevelInstanceSourceResolution::Resolved)
    {
        if (!ScanBudget.TryConsumeString(SourceAsset))
        {
            Snapshot.AddDiagnostic(Warning(
                TEXT("resolution.level_instance_source_unavailable"),
                TEXT("A Level Instance source Target exceeded the bounded snapshot-string budget and was omitted."),
                Operation,
                Entry.EvidenceKey()));
            return;
        }
        Entry.LevelInstanceSourceAsset = MoveTemp(SourceAsset);
        return;
    }
    if (Resolution != ELevelInstanceSourceResolution::NotLevelInstance)
    {
        Snapshot.AddDiagnostic(Warning(
            TEXT("resolution.level_instance_source_unavailable"),
            FString::Printf(
                TEXT("The Level Instance placement remains readable, but its saved source Level could not be canonicalized without loading it (%s)."),
                *LevelInstanceResolutionText(Resolution)),
            Operation,
            Entry.EvidenceKey()));
    }
}

bool BuildSnapshot(
    const FSalResolvedTarget& Target,
    const FString& Operation,
    FLevelSnapshot& Out,
    FString& OutReason)
{
    Out = FLevelSnapshot();
    if (!IsExactLoadedSource(
            Target,
            Out.World,
            Out.Level,
            OutReason))
    {
        return false;
    }

    bool bScanLimitReached = false;
    const auto MarkScanLimitReached = [&](const FString& Limit)
    {
        if (bScanLimitReached)
        {
            return;
        }
        bScanLimitReached = true;
        Out.bIdentityComplete = false;
        Out.AddDiagnostic(Warning(
            TEXT("validation.reference_scan_incomplete"),
            FString::Printf(
                TEXT("The Level Actor identity scan reached its bounded %s budget; Actor StableRefs are fail-closed."),
                *Limit),
            Operation,
            Target.AssetPath));
    };

    FLevelScanBudget ScanBudget;
    TSet<const AActor*> SeenLoadedActors;
    TMap<FString, int32> LoadedActorIndicesByPath;
    TMap<FGuid, TArray<int32>> LoadedActorIndicesByGuid;
    for (AActor* Actor : Out.Level->Actors)
    {
        if (!ScanBudget.TryExamineCandidate())
        {
            MarkScanLimitReached(TEXT("examined-candidate"));
            break;
        }
        if (!IsPersistedLevelActor(Actor, Out.Level))
        {
            continue;
        }
        if (SeenLoadedActors.Contains(Actor))
        {
            Out.AddDiagnostic(Warning(
                TEXT("validation.reference_scan_incomplete"),
                TEXT("The PersistentLevel Actor array contains the same native Actor pointer more than once; the repeated storage entry was ignored before materializing Actor text."),
                Operation,
                Actor->GetFName().ToString()));
            continue;
        }
        SeenLoadedActors.Add(Actor);
        const FString& ActorLabel = Actor->GetActorLabel(false);
        if (!ScanBudget.TryConsumeString(ActorLabel))
        {
            MarkScanLimitReached(TEXT("snapshot-string-byte"));
            break;
        }
        FLevelActorEntry Entry = LoadedActorEntry(Actor);
        if (LoadedActorIndicesByPath.Contains(Entry.ObjectPath))
        {
            if (!ScanBudget.TryConsumeEntryStrings(
                    Entry,
                    1,
                    true,
                    false))
            {
                MarkScanLimitReached(TEXT("snapshot-string-byte"));
                break;
            }
            Out.AddDiagnostic(Warning(
                TEXT("validation.reference_scan_incomplete"),
                TEXT("The PersistentLevel Actor array contains the same native Actor more than once; the repeated storage entry was ignored."),
                Operation,
                Entry.ObjectPath));
            continue;
        }
        if (!ScanBudget.TryConsumeEntryStrings(
                Entry,
                2,
                true,
                false))
        {
            MarkScanLimitReached(TEXT("snapshot-string-byte"));
            break;
        }
        const int32 Index = Out.Actors.Add(MoveTemp(Entry));
        LoadedActorIndicesByPath.Add(Out.Actors[Index].ObjectPath, Index);
        if (Out.Actors[Index].Guid.IsValid())
        {
            LoadedActorIndicesByGuid.FindOrAdd(
                Out.Actors[Index].Guid).Add(Index);
        }
    }

    UWorldPartition* WorldPartition = Out.World->GetWorldPartition();
    if (WorldPartition != nullptr && !bScanLimitReached)
    {
        if (!IsValid(WorldPartition) || WorldPartition->HasAnyFlags(IncompleteLoadFlags))
        {
            Out.bIdentityComplete = false;
            Out.AddDiagnostic(Warning(TEXT("validation.reference_scan_incomplete"),
                                      TEXT("The source World Partition is being loaded or torn "
                                           "down; Actor StableRefs are fail-closed."),
                                      Operation,
                                      Target.AssetPath));
        }
        else
        {
            UActorDescContainerInstance* Root = WorldPartition->GetActorDescContainerInstance();
            const FName SourcePackage = Out.World->GetOutermost()->GetFName();
            if (!IsValid(Root) || Root->HasAnyFlags(IncompleteLoadFlags) ||
                !Root->IsInitialized() || Root->GetContainerPackage() != SourcePackage)
            {
                Out.bIdentityComplete = false;
                Out.AddDiagnostic(Warning(
                    TEXT("validation.reference_scan_incomplete"),
                    TEXT(
                        "The root World Partition Actor descriptor container is unavailable or "
                        "does not match the exact source World; Actor StableRefs are fail-closed."),
                    Operation,
                    Target.AssetPath));
            }
            else
            {
                for (UActorDescContainerInstance::TConstIterator<> Iterator(Root); Iterator;
                     ++Iterator)
                {
                    if (!ScanBudget.TryExamineCandidate())
                    {
                        MarkScanLimitReached(TEXT("examined-candidate"));
                        break;
                    }
                    const FWorldPartitionActorDescInstance* Descriptor = *Iterator;
                    const FWorldPartitionActorDesc* ActorDesc =
                        Descriptor != nullptr ? Descriptor->GetActorDesc() : nullptr;
                    if (ActorDesc == nullptr)
                    {
                        Out.bIdentityComplete = false;
                        Out.AddDiagnostic(
                            Warning(TEXT("validation.reference_scan_incomplete"),
                                    TEXT("The root World Partition container contains an invalid "
                                         "Actor descriptor; Actor StableRefs are fail-closed."),
                                    Operation,
                                    Target.AssetPath));
                        continue;
                    }
                    if (IsExcludedDescriptor(ActorDesc))
                    {
                        continue;
                    }
                    ++Out.RootDescriptorCount;

                    const FGuid DescriptorGuid = ActorDesc->GetGuid();
                    FString DescriptorPath = ActorDesc->GetActorSoftPath().ToString();
                    if (!ScanBudget.TryConsumeString(DescriptorPath))
                    {
                        MarkScanLimitReached(TEXT("snapshot-string-byte"));
                        break;
                    }
                    int32 LoadedIndex = INDEX_NONE;
                    if (DescriptorGuid.IsValid())
                    {
                        if (const TArray<int32>* GuidMatches =
                                LoadedActorIndicesByGuid.Find(DescriptorGuid))
                        {
                            for (const int32 Candidate : *GuidMatches)
                            {
                                if (Out.Actors.IsValidIndex(Candidate) &&
                                    Out.Actors[Candidate].ObjectPath == DescriptorPath)
                                {
                                    LoadedIndex = Candidate;
                                    break;
                                }
                            }
                            if (LoadedIndex == INDEX_NONE && !GuidMatches->IsEmpty())
                            {
                                LoadedIndex = (*GuidMatches)[0];
                            }
                        }
                    }
                    if (Out.Actors.IsValidIndex(LoadedIndex))
                    {
                        FLevelActorEntry& GuidMatch = Out.Actors[LoadedIndex];
                        GuidMatch.bHasDescriptor = true;
                        if (GuidMatch.ObjectPath != DescriptorPath)
                        {
                            Out.AddDiagnostic(Warning(
                                TEXT("resolution.identity_conflict"),
                                TEXT("A loaded Actor and root World Partition descriptor share "
                                     "ActorGuid but disagree on object path; ActorGuid remains the "
                                     "identity and the loaded Actor evidence wins."),
                                Operation,
                                GuidText(DescriptorGuid)));
                        }
                        continue;
                    }

                    if (const int32* PathMatch = LoadedActorIndicesByPath.Find(DescriptorPath))
                    {
                        Out.bIdentityComplete = false;
                        Out.AddDiagnostic(
                            Warning(TEXT("resolution.identity_conflict"),
                                    TEXT("A loaded Actor and root World Partition descriptor share "
                                         "an object path but disagree on ActorGuid; both records "
                                         "are preserved and Actor StableRefs are fail-closed."),
                                    Operation,
                                    Out.Actors.IsValidIndex(*PathMatch)
                                        ? Out.Actors[*PathMatch].EvidenceKey()
                                        : DescriptorPath));
                    }
                    const FString& DescriptorName = ActorDesc->GetActorNameString();
                    const FString& DescriptorLabel = ActorDesc->GetActorLabelString();
                    if (!ScanBudget.TryConsumeString(DescriptorName) ||
                        !ScanBudget.TryConsumeString(DescriptorLabel))
                    {
                        MarkScanLimitReached(TEXT("snapshot-string-byte"));
                        break;
                    }
                    FLevelActorEntry Entry =
                        DescriptorEntry(ActorDesc, SourcePackage, MoveTemp(DescriptorPath));
                    if (!ScanBudget.TryConsumeEntryStrings(Entry, 0, false, false))
                    {
                        MarkScanLimitReached(TEXT("snapshot-string-byte"));
                        break;
                    }
                    Out.Actors.Add(MoveTemp(Entry));
                }
            }
        }
    }

    AuditIdentities(Out, Operation);
    Out.Actors.Sort([](
        const FLevelActorEntry& Left,
        const FLevelActorEntry& Right)
    {
        if (Left.Guid != Right.Guid)
        {
            return Left.Guid < Right.Guid;
        }
        if (Left.bLoaded != Right.bLoaded)
        {
            return Left.bLoaded;
        }
        const int32 ObjectPathOrder = Left.ObjectPath.Compare(Right.ObjectPath);
        if (ObjectPathOrder != 0)
        {
            return ObjectPathOrder < 0;
        }
        const int32 PackageOrder = Left.PackagePath.Compare(Right.PackagePath);
        if (PackageOrder != 0)
        {
            return PackageOrder < 0;
        }
        const int32 NameOrder = Left.Name.Compare(Right.Name);
        if (NameOrder != 0)
        {
            return NameOrder < 0;
        }
        return Left.Type < Right.Type;
    });
    return true;
}

TArray<AActor*> StableLoadedActors(const FLevelSnapshot& Snapshot)
{
    TArray<AActor*> Actors;
    Actors.Reserve(Snapshot.Actors.Num());
    for (const FLevelActorEntry& Entry : Snapshot.Actors)
    {
        AActor* Actor = Entry.Actor.Get();
        if (Entry.bLoaded
            && Entry.Guid.IsValid()
            && Entry.IdentityMultiplicity == 1
            && IsValid(Actor))
        {
            Actors.Add(Actor);
        }
    }
    return Actors;
}

bool BuildComponentsForSnapshot(
    FLevelSnapshot& Snapshot,
    const FString& Operation,
    FSalLevelComponentSnapshot& Out,
    FString& OutReason)
{
    Out = FSalLevelComponentSnapshot();
    OutReason.Reset();
    if (!Snapshot.bIdentityComplete)
    {
        Snapshot.bComponentIdentityComplete = false;
        OutReason = TEXT("The Level Actor identity environment is incomplete; Component identity is fail-closed.");
        return false;
    }
    const bool bBuilt = BuildLevelComponentSnapshot(
        StableLoadedActors(Snapshot),
        Operation,
        Out,
        OutReason);
    Snapshot.bComponentIdentityComplete = Out.bIdentityComplete;
    Snapshot.ComponentCount = Out.Entries.Num();
    for (const FSalLevelComponentEntry& Entry : Out.Entries)
    {
        Snapshot.NativeComponentCount += Entry.Source == TEXT("native") ? 1 : 0;
        Snapshot.SCSComponentCount += Entry.Source == TEXT("scs") ? 1 : 0;
        Snapshot.InstanceComponentCount += Entry.Source == TEXT("instance") ? 1 : 0;
        UActorComponent* Component = Entry.Component.Get();
        Snapshot.PCGComponentCount += Component != nullptr
                && Component->IsA<UPCGComponent>()
            ? 1
            : 0;
    }
    for (const TSharedPtr<FJsonObject>& Diagnostic : Out.FinalDiagnostics(Operation))
    {
        Snapshot.AddDiagnostic(Diagnostic);
    }
    if ((!bBuilt || !Out.bIdentityComplete) && OutReason.IsEmpty())
    {
        OutReason = TEXT("The bounded Component source audit is incomplete; Component StableRefs are fail-closed.");
    }
    return bBuilt && Out.bIdentityComplete;
}

TSharedPtr<FJsonValue> VectorValue(const FVector& Vector)
{
    TSharedPtr<FJsonObject> Fields = MakeShared<FJsonObject>();
    Fields->SetNumberField(TEXT("X"), Vector.X);
    Fields->SetNumberField(TEXT("Y"), Vector.Y);
    Fields->SetNumberField(TEXT("Z"), Vector.Z);
    return Value::Call(TEXT("vector"), Fields);
}

TSharedPtr<FJsonValue> QuaternionValue(const FQuat& Rotation)
{
    TSharedPtr<FJsonObject> Fields = MakeShared<FJsonObject>();
    Fields->SetNumberField(TEXT("X"), Rotation.X);
    Fields->SetNumberField(TEXT("Y"), Rotation.Y);
    Fields->SetNumberField(TEXT("Z"), Rotation.Z);
    Fields->SetNumberField(TEXT("W"), Rotation.W);
    return Value::Call(TEXT("quaternion"), Fields);
}

TSharedPtr<FJsonValue> TransformValue(const FTransform& Transform)
{
    TSharedPtr<FJsonObject> Fields = MakeShared<FJsonObject>();
    Fields->SetField(
        TEXT("Translation"),
        VectorValue(Transform.GetTranslation()));
    Fields->SetField(
        TEXT("Rotation"),
        QuaternionValue(Transform.GetRotation()));
    Fields->SetField(
        TEXT("Scale3D"),
        VectorValue(Transform.GetScale3D()));
    return Value::Call(TEXT("transform"), Fields);
}

TSharedPtr<FJsonValue> BoundsValue(const FBox& Bounds)
{
    TSharedPtr<FJsonObject> Fields = MakeShared<FJsonObject>();
    Fields->SetField(TEXT("Min"), VectorValue(Bounds.Min));
    Fields->SetField(TEXT("Max"), VectorValue(Bounds.Max));
    return Value::Call(TEXT("bounds"), Fields);
}

TSharedPtr<FJsonValue> LevelValue(
    const FSalResolvedTarget& Target,
    const FLevelSnapshot* Snapshot,
    const bool bSummary)
{
    UWorld* World = nullptr;
    ULevel* Level = nullptr;
    FString Reason;
    const bool bLoaded = Snapshot != nullptr
        || IsExactLoadedSource(Target, World, Level, Reason);

    TSharedPtr<FJsonObject> Fields = MakeShared<FJsonObject>();
    Fields->SetStringField(TEXT("path"), Target.AssetPath);
    Fields->SetStringField(TEXT("type"), TargetType(Target));
    Fields->SetStringField(TEXT("name"), TargetName(Target));
    Fields->SetArrayField(
        TEXT("domains"),
        {Value::String(TEXT("asset")), Value::String(TEXT("level"))});
    Fields->SetBoolField(TEXT("loaded"), bLoaded);
    if (bLoaded)
    {
        UWorld* LoadedWorld = Snapshot != nullptr ? Snapshot->World : World;
        Fields->SetStringField(TEXT("worldType"), TEXT("Editor"));
        Fields->SetBoolField(
            TEXT("worldPartition"),
            LoadedWorld != nullptr
                && LoadedWorld->GetWorldPartition() != nullptr);
    }
    if (bSummary && Snapshot != nullptr)
    {
        int32 LoadedActors = 0;
        int32 UnloadedDescriptors = 0;
        int32 ExternalActors = 0;
        for (const FLevelActorEntry& Entry : Snapshot->Actors)
        {
            LoadedActors += Entry.bLoaded ? 1 : 0;
            UnloadedDescriptors += !Entry.bLoaded && Entry.bHasDescriptor ? 1 : 0;
            ExternalActors += Entry.bExternal ? 1 : 0;
        }
        Fields->SetNumberField(TEXT("actorCount"), Snapshot->Actors.Num());
        Fields->SetNumberField(TEXT("loadedActorCount"), LoadedActors);
        Fields->SetNumberField(
            TEXT("unloadedDescriptorCount"),
            UnloadedDescriptors);
        Fields->SetNumberField(
            TEXT("rootDescriptorCount"),
            Snapshot->RootDescriptorCount);
        Fields->SetNumberField(TEXT("externalActorCount"), ExternalActors);
        Fields->SetNumberField(
            TEXT("stableActorCount"),
            Snapshot->StableActorCount);
        Fields->SetNumberField(
            TEXT("invalidActorGuidCount"),
            Snapshot->InvalidGuidCount);
        Fields->SetNumberField(
            TEXT("duplicateActorGuidCount"),
            Snapshot->DuplicateGuidCount);
        Fields->SetBoolField(
            TEXT("identityComplete"),
            Snapshot->bIdentityComplete);
        Fields->SetNumberField(
            TEXT("componentCount"),
            Snapshot->ComponentCount);
        Fields->SetNumberField(
            TEXT("nativeComponentCount"),
            Snapshot->NativeComponentCount);
        Fields->SetNumberField(
            TEXT("scsComponentCount"),
            Snapshot->SCSComponentCount);
        Fields->SetNumberField(
            TEXT("instanceComponentCount"),
            Snapshot->InstanceComponentCount);
        Fields->SetNumberField(
            TEXT("pcgComponentCount"),
            Snapshot->PCGComponentCount);
        Fields->SetBoolField(
            TEXT("componentIdentityComplete"),
            Snapshot->bComponentIdentityComplete);
    }
    return Value::Call(TEXT("asset"), Fields);
}

TSharedPtr<FJsonValue> ActorValue(
    const FLevelActorEntry& Entry,
    const FString& LevelAlias,
    const bool bIdentityComplete,
    TSharedPtr<FJsonObject>* OutFields = nullptr)
{
    TSharedPtr<FJsonObject> Fields = MakeShared<FJsonObject>();
    Fields->SetStringField(TEXT("id"), GuidText(Entry.Guid));
    Fields->SetStringField(TEXT("type"), Entry.Type);
    if (!Entry.Name.IsEmpty())
    {
        Fields->SetStringField(TEXT("Name"), Entry.Name);
    }
    if (!Entry.Label.IsEmpty())
    {
        Fields->SetStringField(TEXT("ActorLabel"), Entry.Label);
    }
    if (!Entry.ObjectPath.IsEmpty())
    {
        Fields->SetStringField(TEXT("path"), Entry.ObjectPath);
    }
    if (!Entry.PackagePath.IsEmpty())
    {
        Fields->SetStringField(TEXT("package"), Entry.PackagePath);
    }
    Fields->SetField(TEXT("level"), Value::Local(LevelAlias));
    if (Entry.bLevelInstance)
    {
        Fields->SetBoolField(TEXT("levelInstance"), true);
    }
    Fields->SetBoolField(TEXT("loaded"), Entry.bLoaded);
    Fields->SetBoolField(TEXT("external"), Entry.bExternal);
    if (!Entry.bLoaded && Entry.bHasDescriptor)
    {
        Fields->SetBoolField(TEXT("descriptor"), true);
    }
    if (Entry.bHasTransform)
    {
        Fields->SetField(TEXT("Transform"), TransformValue(Entry.Transform));
    }
    if (Entry.bHasBounds)
    {
        Fields->SetField(TEXT("bounds"), BoundsValue(Entry.Bounds));
    }

    const bool bIdentityValid = Entry.Guid.IsValid();
    const bool bIdentityUnique = Entry.IdentityMultiplicity == 1;
    const bool bStableRefAvailable =
        bIdentityComplete && bIdentityValid && bIdentityUnique;
    Fields->SetBoolField(TEXT("identityValid"), bIdentityValid);
    Fields->SetBoolField(TEXT("identityUnique"), bIdentityUnique);
    Fields->SetBoolField(
        TEXT("stableRefAvailable"),
        bStableRefAvailable);
    if (bStableRefAvailable)
    {
        Fields->SetField(
            TEXT("ref"),
            Value::Stable(TEXT("actor"), GuidText(Entry.Guid)));
    }
    Fields->SetField(
        TEXT("identityStatus"),
        Value::Name(
            !bIdentityComplete
                ? TEXT("incomplete")
                : !bIdentityValid
                    ? TEXT("invalid")
                    : !bIdentityUnique
                        ? TEXT("conflict")
                        : TEXT("stable")));
    if (OutFields != nullptr)
    {
        *OutFields = Fields;
    }
    return Value::Call(TEXT("actor"), Fields);
}

TSharedPtr<FJsonValue> ComponentValue(
    const FSalLevelComponentEntry& Entry,
    TSharedPtr<FJsonObject>* OutFields = nullptr)
{
    TSharedPtr<FJsonObject> Fields = MakeShared<FJsonObject>();
    const FString ActorId = GuidText(Entry.ActorGuid);
    Fields->SetField(
        TEXT("actor"),
        Value::Stable(TEXT("actor"), ActorId));
    Fields->SetStringField(TEXT("id"), Entry.Id);
    Fields->SetStringField(TEXT("name"), Entry.Name);
    Fields->SetField(TEXT("source"), Value::Name(Entry.Source));
    Fields->SetStringField(TEXT("type"), Entry.Type);
    Fields->SetField(
        TEXT("CreationMethod"),
        Value::Name(Entry.CreationMethod));
    if (!Entry.DeclaringClass.IsEmpty())
    {
        Fields->SetStringField(
            TEXT("declaringClass"),
            Entry.DeclaringClass);
    }
    Fields->SetBoolField(TEXT("loaded"), true);
    Fields->SetBoolField(TEXT("registered"), Entry.bRegistered);
    Fields->SetBoolField(TEXT("stableRefAvailable"), true);
    Fields->SetField(
        TEXT("ref"),
        Value::Stable(
            TEXT("component"),
            TArray<FString>{ActorId, Entry.Source, Entry.Id}));
    if (OutFields != nullptr)
    {
        *OutFields = Fields;
    }
    return Value::Call(TEXT("component"), Fields);
}

TSharedPtr<FJsonObject> PcgComponentTargetValue(
    const FSalResolvedTarget& LevelTarget,
    const FSalLevelComponentEntry& Entry)
{
    UPCGComponent* Component = Cast<UPCGComponent>(Entry.Component.Get());
    if (LevelTarget.AssetPath.IsEmpty()
        || !IsValid(Component)
        || Component->IsLocalComponent()
        || Component->GetConstOriginalComponent() != Component
        || Entry.Type != Component->GetClass()->GetPathName())
    {
        return nullptr;
    }
    TSharedPtr<FJsonObject> Target = MakeShared<FJsonObject>();
    Target->SetStringField(TEXT("kind"), TEXT("target"));
    Target->SetStringField(TEXT("domain"), TEXT("pcg_component"));
    Target->SetStringField(TEXT("asset"), LevelTarget.AssetPath);
    Target->SetStringField(
        TEXT("actorId"),
        GuidText(Entry.ActorGuid));
    Target->SetStringField(TEXT("source"), Entry.Source);
    Target->SetStringField(TEXT("id"), Entry.Id);
    Target->SetStringField(TEXT("type"), Entry.Type);
    return Target;
}

bool ComponentMatchesText(
    const FSalLevelComponentEntry& Entry,
    const FString& SearchText)
{
    if (SearchText.IsEmpty())
    {
        return true;
    }
    for (const FString& Field : {
             GuidText(Entry.ActorGuid),
             Entry.Source,
             Entry.Id,
             Entry.Name,
             Entry.Type,
             Entry.CreationMethod,
             Entry.DeclaringClass})
    {
        if (Field.Contains(SearchText, ESearchCase::IgnoreCase))
        {
            return true;
        }
    }
    return false;
}

void AddLevelInstanceHandoffs(
    const TSharedPtr<FJsonObject>& Result,
    const TArray<FPendingLevelInstanceHandoff>& Pending)
{
    if (!Result.IsValid())
    {
        return;
    }
    for (const FPendingLevelInstanceHandoff& Item : Pending)
    {
        if (!Item.ActorFields.IsValid()
            || !Item.Target.IsValid())
        {
            continue;
        }
        const FString Alias = ResultTargets::AddHandoff(
            Result,
            Item.Target,
            Item.PreferredAlias,
            TEXT("inspect_source_level"));
        if (!Alias.IsEmpty())
        {
            Item.ActorFields->SetField(
                TEXT("sourceLevel"),
                Value::Local(Alias));
        }
    }
}

void AddPcgComponentHandoffs(
    const TSharedPtr<FJsonObject>& Result,
    const TArray<FPendingPcgComponentHandoff>& Pending)
{
    if (!Result.IsValid())
    {
        return;
    }
    for (const FPendingPcgComponentHandoff& Item : Pending)
    {
        if (!Item.ComponentFields.IsValid()
            || !Item.Target.IsValid())
        {
            continue;
        }
        const FString Alias = ResultTargets::AddHandoff(
            Result,
            Item.Target,
            Item.PreferredAlias,
            TEXT("inspect_pcg_component"));
        if (!Alias.IsEmpty())
        {
            Item.ComponentFields->SetField(
                TEXT("pcgComponent"),
                Value::Local(Alias));
        }
    }
}

bool EntryMatchesText(
    const FLevelActorEntry& Entry,
    const FString& SearchText)
{
    if (SearchText.IsEmpty())
    {
        return true;
    }
    for (const FString& Field : {
             GuidText(Entry.Guid),
             Entry.Name,
             Entry.Label,
             Entry.ObjectPath,
             Entry.PackagePath,
             Entry.Type})
    {
        if (Field.Contains(SearchText, ESearchCase::IgnoreCase))
        {
            return true;
        }
    }
    return false;
}

class FCursorFingerprintBuilder
{
public:
    void Add(const FString& Value)
    {
        const FString Length = LexToString(Value.Len());
        Hash.UpdateWithString(*Length, Length.Len());
        Hash.UpdateWithString(TEXT(":"), 1);
        Hash.UpdateWithString(*Value, Value.Len());
        Hash.UpdateWithString(TEXT(";"), 1);
    }

    FString Finalize()
    {
        Hash.Final();
        uint8 Digest[FSHA1::DigestSize];
        Hash.GetHash(Digest);
        return BytesToHex(Digest, UE_ARRAY_COUNT(Digest)).ToLower();
    }

private:
    FSHA1 Hash;
};

void AppendVectorFingerprint(
    FCursorFingerprintBuilder& Out,
    const FVector& Vector)
{
    Out.Add(LexToString(Vector.X));
    Out.Add(LexToString(Vector.Y));
    Out.Add(LexToString(Vector.Z));
}

void AppendQuaternionFingerprint(
    FCursorFingerprintBuilder& Out,
    const FQuat& Quaternion)
{
    Out.Add(LexToString(Quaternion.X));
    Out.Add(LexToString(Quaternion.Y));
    Out.Add(LexToString(Quaternion.Z));
    Out.Add(LexToString(Quaternion.W));
}

FString CursorFingerprint(
    const FSalQuery& Query,
    const FSalResolvedTarget& Target,
    const FLevelSnapshot& Snapshot,
    const int32 Limit)
{
    FString SearchText;
    Query.Operation->TryGetStringField(TEXT("text"), SearchText);
    FCursorFingerprintBuilder Fingerprint;
    Fingerprint.Add(Target.AssetPath);
    Fingerprint.Add(SearchText);
    Fingerprint.Add(LexToString(Limit));
    Fingerprint.Add(
        Snapshot.bIdentityComplete ? TEXT("complete") : TEXT("incomplete"));
    for (const FLevelActorEntry& Entry : Snapshot.Actors)
    {
        Fingerprint.Add(GuidText(Entry.Guid));
        Fingerprint.Add(Entry.Type);
        Fingerprint.Add(Entry.Name);
        Fingerprint.Add(Entry.Label);
        Fingerprint.Add(Entry.ObjectPath);
        Fingerprint.Add(Entry.PackagePath);
        Fingerprint.Add(
            Entry.bLevelInstance
                ? TEXT("level_instance")
                : TEXT("ordinary_actor"));
        Fingerprint.Add(Entry.LevelInstanceSourceLocator);
        Fingerprint.Add(Entry.LevelInstanceSourceFingerprint);
        Fingerprint.Add(Entry.bLoaded ? TEXT("loaded") : TEXT("unloaded"));
        Fingerprint.Add(
            Entry.bHasDescriptor ? TEXT("descriptor") : TEXT("no_descriptor"));
        Fingerprint.Add(
            Entry.bExternal ? TEXT("external") : TEXT("internal"));
        Fingerprint.Add(
            Entry.bHasTransform ? TEXT("transform") : TEXT("no_transform"));
        if (Entry.bHasTransform)
        {
            AppendVectorFingerprint(
                Fingerprint,
                Entry.Transform.GetTranslation());
            AppendQuaternionFingerprint(
                Fingerprint,
                Entry.Transform.GetRotation());
            AppendVectorFingerprint(
                Fingerprint,
                Entry.Transform.GetScale3D());
        }
        Fingerprint.Add(
            Entry.bHasBounds ? TEXT("bounds") : TEXT("no_bounds"));
        if (Entry.bHasBounds)
        {
            AppendVectorFingerprint(Fingerprint, Entry.Bounds.Min);
            AppendVectorFingerprint(Fingerprint, Entry.Bounds.Max);
        }
        Fingerprint.Add(LexToString(Entry.IdentityMultiplicity));
    }
    return Fingerprint.Finalize();
}

FString ComponentCursorFingerprint(
    const FSalQuery& Query,
    const FSalResolvedTarget& Target,
    const FLevelSnapshot& LevelSnapshot,
    const FSalLevelComponentSnapshot& ComponentSnapshot,
    const int32 Limit)
{
    FString SearchText;
    Query.Operation->TryGetStringField(TEXT("text"), SearchText);
    FCursorFingerprintBuilder Fingerprint;
    Fingerprint.Add(TEXT("level_component1"));
    Fingerprint.Add(Target.AssetPath);
    Fingerprint.Add(TargetType(Target));
    Fingerprint.Add(SearchText);
    Fingerprint.Add(LexToString(Limit));
    Fingerprint.Add(
        LevelSnapshot.bIdentityComplete
            ? TEXT("actor_identity_complete")
            : TEXT("actor_identity_incomplete"));
    Fingerprint.Add(
        ComponentSnapshot.bIdentityComplete
            ? TEXT("component_identity_complete")
            : TEXT("component_identity_incomplete"));
    for (const FLevelActorEntry& Actor : LevelSnapshot.Actors)
    {
        Fingerprint.Add(GuidText(Actor.Guid));
        Fingerprint.Add(Actor.bLoaded ? TEXT("loaded") : TEXT("unloaded"));
        Fingerprint.Add(LexToString(Actor.IdentityMultiplicity));
    }
    for (const FSalLevelComponentEntry& Entry : ComponentSnapshot.Entries)
    {
        Fingerprint.Add(GuidText(Entry.ActorGuid));
        Fingerprint.Add(Entry.Source);
        Fingerprint.Add(Entry.Id);
        Fingerprint.Add(Entry.Name);
        Fingerprint.Add(Entry.Type);
        Fingerprint.Add(Entry.CreationMethod);
        Fingerprint.Add(Entry.DeclaringClass);
        Fingerprint.Add(
            Entry.bRegistered ? TEXT("registered") : TEXT("unregistered"));
    }
    return Fingerprint.Finalize();
}

bool DecodePage(
    const FSalQuery& Query,
    const FSalResolvedTarget& Target,
    const FLevelSnapshot& Snapshot,
    FSalPage& OutPage,
    FString& OutFingerprint)
{
    OutPage.Offset = 0;
    OutPage.Limit = FMath::Clamp(
        Query.PageLimit > 0 ? Query.PageLimit : DefaultCollectionLimit,
        1,
        MaxCollectionLimit);
    OutFingerprint = CursorFingerprint(
        Query,
        Target,
        Snapshot,
        OutPage.Limit);
    if (Query.PageAfter.IsEmpty())
    {
        return true;
    }
    TArray<FString> Parts;
    Query.PageAfter.ParseIntoArray(Parts, TEXT(":"), false);
    return Parts.Num() == 3
        && Parts[0] == TEXT("level1")
        && Parts[1].Equals(OutFingerprint, ESearchCase::IgnoreCase)
        && ParseNonNegativeInt32(Parts[2], OutPage.Offset);
}

bool DecodeComponentPage(
    const FSalQuery& Query,
    const FSalResolvedTarget& Target,
    const FLevelSnapshot& LevelSnapshot,
    const FSalLevelComponentSnapshot& ComponentSnapshot,
    FSalPage& OutPage,
    FString& OutFingerprint)
{
    OutPage.Offset = 0;
    OutPage.Limit = FMath::Clamp(
        Query.PageLimit > 0 ? Query.PageLimit : DefaultCollectionLimit,
        1,
        MaxCollectionLimit);
    OutFingerprint = ComponentCursorFingerprint(
        Query,
        Target,
        LevelSnapshot,
        ComponentSnapshot,
        OutPage.Limit);
    if (Query.PageAfter.IsEmpty())
    {
        return true;
    }
    TArray<FString> Parts;
    Query.PageAfter.ParseIntoArray(Parts, TEXT(":"), false);
    return Parts.Num() == 3
        && Parts[0] == TEXT("level_component1")
        && Parts[1].Equals(OutFingerprint, ESearchCase::IgnoreCase)
        && ParseNonNegativeInt32(Parts[2], OutPage.Offset);
}

void SetPage(
    const TSharedPtr<FJsonObject>& Result,
    const FString& Fingerprint,
    const int32 NextOffset,
    const bool bHasNext)
{
    if (!Result.IsValid() || !bHasNext)
    {
        return;
    }
    TSharedPtr<FJsonObject> PageObject = MakeShared<FJsonObject>();
    PageObject->SetStringField(
        TEXT("next"),
        TEXT("level1:") + Fingerprint + TEXT(":") +
            LexToString(NextOffset));
    Result->SetObjectField(TEXT("page"), PageObject);
}

void SetComponentPage(
    const TSharedPtr<FJsonObject>& Result,
    const FString& Fingerprint,
    const int32 NextOffset,
    const bool bHasNext)
{
    if (!Result.IsValid() || !bHasNext)
    {
        return;
    }
    TSharedPtr<FJsonObject> PageObject = MakeShared<FJsonObject>();
    PageObject->SetStringField(
        TEXT("next"),
        TEXT("level_component1:") + Fingerprint + TEXT(":")
            + LexToString(NextOffset));
    Result->SetObjectField(TEXT("page"), PageObject);
}

bool HasAnyClauses(const FSalQuery& Query)
{
    return Query.Where.IsValid()
        || !Query.With.IsEmpty()
        || !Query.OrderBy.IsEmpty()
        || Query.PageLimit > 0
        || !Query.PageAfter.IsEmpty();
}

TSharedPtr<FJsonObject> ContentUnavailable(
    const FString& Operation,
    const FSalResolvedTarget& Target,
    const FString& Reason)
{
    return QueryError(
        TEXT("capability.level_not_loaded"),
        Reason.IsEmpty()
            ? TEXT("The exact Level Target is not loaded as an authored Editor source World.")
            : Reason,
        Operation,
        Target.AssetPath);
}

TSharedPtr<FJsonObject> QueryTarget(
    const FSalQuery& Query,
    const FSalResolvedTarget& Target)
{
    if (HasAnyClauses(Query))
    {
        return QueryError(
            TEXT("capability.clause_unavailable"),
            TEXT("Exact Level target read accepts no Query clauses in this read-only slice."),
            TEXT("target"));
    }
    FSalObjectBuilder Builder;
    const FString Alias = Builder.UniqueAlias(
        Query.Alias.IsEmpty() ? TargetName(Target) : Query.Alias);
    Builder.AddLocalBinding(Alias, LevelValue(Target, nullptr, false));
    return Builder.BuildResult();
}

TSharedPtr<FJsonObject> QuerySummary(
    const FSalQuery& Query,
    const FSalResolvedTarget& Target)
{
    if (HasAnyClauses(Query))
    {
        return QueryError(
            TEXT("capability.clause_unavailable"),
            TEXT("Level summary accepts no Query clauses."),
            TEXT("summary"));
    }
    FLevelSnapshot Snapshot;
    FString Reason;
    if (!BuildSnapshot(Target, TEXT("summary"), Snapshot, Reason))
    {
        return ContentUnavailable(TEXT("summary"), Target, Reason);
    }
    FSalLevelComponentSnapshot Components;
    BuildComponentsForSnapshot(
        Snapshot,
        TEXT("summary"),
        Components,
        Reason);
    FSalObjectBuilder Builder;
    const FString Alias = Builder.UniqueAlias(
        Query.Alias.IsEmpty() ? TargetName(Target) : Query.Alias);
    Builder.AddLocalBinding(
        Alias,
        LevelValue(Target, &Snapshot, true));
    return Builder.BuildResult(Snapshot.FinalDiagnostics(TEXT("summary")));
}

TSharedPtr<FJsonObject> QueryActors(
    const FSalQuery& Query,
    const FSalResolvedTarget& Target)
{
    if (Query.Where.IsValid()
        || !Query.With.IsEmpty()
        || !Query.OrderBy.IsEmpty())
    {
        return QueryError(
            TEXT("capability.clause_unavailable"),
            TEXT("Level actors accepts only optional text search and cursor page clauses in this read-only slice."),
            TEXT("actors"));
    }
    FLevelSnapshot Snapshot;
    FString Reason;
    if (!BuildSnapshot(Target, TEXT("actors"), Snapshot, Reason))
    {
        return ContentUnavailable(TEXT("actors"), Target, Reason);
    }
    if (!PrepareLevelInstanceCursorEvidence(
            Snapshot,
            Target,
            Reason))
    {
        return QueryError(
            TEXT("validation.reference_scan_incomplete"),
            Reason,
            TEXT("actors"),
            Target.AssetPath);
    }
    FSalPage Page;
    FString Fingerprint;
    if (!DecodePage(Query, Target, Snapshot, Page, Fingerprint))
    {
        return QueryError(
            TEXT("validation.invalid_cursor"),
            TEXT("Level cursor does not belong to this Target, Actor snapshot, search, or page limit. Re-run the first page."),
            TEXT("actors"),
            Query.PageAfter);
    }

    FString SearchText;
    Query.Operation->TryGetStringField(TEXT("text"), SearchText);
    TArray<FLevelActorEntry*> Matches;
    for (FLevelActorEntry& Entry : Snapshot.Actors)
    {
        if (EntryMatchesText(Entry, SearchText))
        {
            Matches.Add(&Entry);
        }
    }
    if (Page.Offset > Matches.Num())
    {
        return QueryError(
            TEXT("validation.invalid_cursor"),
            TEXT("Level cursor offset is outside the current Actor result set. Re-run the first page."),
            TEXT("actors"),
            Query.PageAfter);
    }
    const int32 End = static_cast<int32>(FMath::Min<int64>(
        Matches.Num(),
        static_cast<int64>(Page.Offset) + Page.Limit));

    FSalObjectBuilder Builder;
    const FString LevelAlias = Builder.UniqueAlias(
        Query.Alias.IsEmpty() ? TargetName(Target) : Query.Alias);
    Builder.AddLocalBinding(
        LevelAlias,
        LevelValue(Target, &Snapshot, false));
    TArray<FPendingLevelInstanceHandoff> PendingHandoffs;
    FLevelScanBudget SourceBudget;
    const IAssetRegistry& Registry =
        FModuleManager::LoadModuleChecked<FAssetRegistryModule>(
            TEXT("AssetRegistry"))
            .Get();
    for (int32 Index = Page.Offset; Index < End; ++Index)
    {
        FLevelActorEntry& Entry = *Matches[Index];
        ResolveLevelInstanceSourceForEntry(
            Snapshot,
            Entry,
            Target,
            SourceBudget,
            TEXT("actors"),
            Registry);
        const FString Preferred = !Entry.Label.IsEmpty()
            ? Entry.Label
            : !Entry.Name.IsEmpty()
                ? Entry.Name
                : TEXT("actor");
        const FString Alias = Builder.UniqueAlias(Preferred);
        TSharedPtr<FJsonObject> ActorFields;
        Builder.AddLocalBinding(
            Alias,
            ActorValue(
                Entry,
                LevelAlias,
                Snapshot.bIdentityComplete,
                &ActorFields));
        if (ActorFields.IsValid()
            && !Entry.LevelInstanceSourceAsset.IsEmpty())
        {
            FPendingLevelInstanceHandoff& Pending =
                PendingHandoffs.AddDefaulted_GetRef();
            Pending.ActorFields = MoveTemp(ActorFields);
            Pending.Target = LevelTargetValue(
                Entry.LevelInstanceSourceAsset);
            Pending.PreferredAlias =
                FPackageName::ObjectPathToObjectName(
                    Entry.LevelInstanceSourceAsset)
                + TEXT("_source");
        }
    }
    if (Matches.IsEmpty())
    {
        Builder.AddComment(TEXT("no matches"));
    }
    TSharedPtr<FJsonObject> Result = Builder.BuildResult(
        Snapshot.FinalDiagnostics(TEXT("actors")));
    AddLevelInstanceHandoffs(Result, PendingHandoffs);
    SetPage(
        Result,
        Fingerprint,
        End,
        End < Matches.Num());
    return Result;
}

TSharedPtr<FJsonObject> QueryComponents(
    const FSalQuery& Query,
    const FSalResolvedTarget& Target)
{
    if (Query.Where.IsValid()
        || !Query.With.IsEmpty()
        || !Query.OrderBy.IsEmpty())
    {
        return QueryError(
            TEXT("capability.clause_unavailable"),
            TEXT("Level components accepts only optional text search and cursor page clauses in this read-only slice."),
            TEXT("components"));
    }
    FLevelSnapshot LevelSnapshot;
    FString Reason;
    if (!BuildSnapshot(Target, TEXT("components"), LevelSnapshot, Reason))
    {
        return ContentUnavailable(TEXT("components"), Target, Reason);
    }
    FSalLevelComponentSnapshot ComponentSnapshot;
    if (!BuildComponentsForSnapshot(
            LevelSnapshot,
            TEXT("components"),
            ComponentSnapshot,
            Reason))
    {
        return QueryError(
            TEXT("validation.reference_scan_incomplete"),
            Reason,
            TEXT("components"),
            Target.AssetPath);
    }

    FSalPage Page;
    FString Fingerprint;
    if (!DecodeComponentPage(
            Query,
            Target,
            LevelSnapshot,
            ComponentSnapshot,
            Page,
            Fingerprint))
    {
        return QueryError(
            TEXT("validation.invalid_cursor"),
            TEXT("Level Component cursor does not belong to this Target, Component snapshot, search, or page limit. Re-run the first page."),
            TEXT("components"),
            Query.PageAfter);
    }

    FString SearchText;
    Query.Operation->TryGetStringField(TEXT("text"), SearchText);
    TArray<const FSalLevelComponentEntry*> Matches;
    for (const FSalLevelComponentEntry& Entry : ComponentSnapshot.Entries)
    {
        if (ComponentMatchesText(Entry, SearchText))
        {
            Matches.Add(&Entry);
        }
    }
    if (Page.Offset > Matches.Num())
    {
        return QueryError(
            TEXT("validation.invalid_cursor"),
            TEXT("Level Component cursor offset is outside the current result set. Re-run the first page."),
            TEXT("components"),
            Query.PageAfter);
    }
    const int32 End = static_cast<int32>(FMath::Min<int64>(
        Matches.Num(),
        static_cast<int64>(Page.Offset) + Page.Limit));

    FSalObjectBuilder Builder;
    const FString LevelAlias = Builder.UniqueAlias(
        Query.Alias.IsEmpty() ? TargetName(Target) : Query.Alias);
    Builder.AddLocalBinding(
        LevelAlias,
        LevelValue(Target, &LevelSnapshot, false));
    TArray<FPendingPcgComponentHandoff> PendingPcgHandoffs;
    for (int32 Index = Page.Offset; Index < End; ++Index)
    {
        const FSalLevelComponentEntry& Entry = *Matches[Index];
        TSharedPtr<FJsonObject> ComponentFields;
        Builder.AddLocalBinding(
            Builder.UniqueAlias(
                Entry.Name.IsEmpty() ? TEXT("component") : Entry.Name),
            ComponentValue(Entry, &ComponentFields));
        TSharedPtr<FJsonObject> PcgTarget =
            PcgComponentTargetValue(Target, Entry);
        if (ComponentFields.IsValid() && PcgTarget.IsValid())
        {
            FPendingPcgComponentHandoff& Pending =
                PendingPcgHandoffs.AddDefaulted_GetRef();
            Pending.ComponentFields = MoveTemp(ComponentFields);
            Pending.Target = MoveTemp(PcgTarget);
            Pending.PreferredAlias = Entry.Name.IsEmpty()
                ? TEXT("pcg_component")
                : Entry.Name + TEXT("_pcg");
        }
    }
    if (Matches.IsEmpty())
    {
        Builder.AddComment(TEXT("no matches"));
    }
    TSharedPtr<FJsonObject> Result = Builder.BuildResult(
        LevelSnapshot.FinalDiagnostics(TEXT("components")));
    AddPcgComponentHandoffs(Result, PendingPcgHandoffs);
    SetComponentPage(
        Result,
        Fingerprint,
        End,
        End < Matches.Num());
    return Result;
}

bool ParseActorGuid(const FString& Text, FGuid& OutGuid)
{
    OutGuid.Invalidate();
    return FGuid::ParseExact(
            Text,
            EGuidFormats::DigitsWithHyphens,
            OutGuid)
        && OutGuid.IsValid()
        && GuidText(OutGuid).Equals(Text, ESearchCase::CaseSensitive);
}

// ============================================================================
// Level exact schema (Slice 2-B)
//
// Conservative, instance-specific editable schema for one exact loaded Actor
// or Component. Authored mutation Patch is not active in this capability, so
// the schema classifies the surface (read-only identity fields, scalar
// set/reset candidates, compound transform, lifecycle) and reports the exact
// availability constraints. Every advertised field is revalidated against
// the full native setter, cascade, reset-source, and readback gates at Patch
// planning time; nothing here claims a live mutation capability.
// ============================================================================

bool LevelExactSchemaGate(
    const FSalQuery& Query,
    FString& OutUnsupportedDetail)
{
    OutUnsupportedDetail.Reset();
    for (const FString& Detail : Query.With)
    {
        if (Detail != TEXT("schema"))
        {
            OutUnsupportedDetail = Detail;
            return false;
        }
    }
    return !Query.Where.IsValid()
        && Query.OrderBy.IsEmpty()
        && Query.PageLimit <= 0
        && Query.PageAfter.IsEmpty();
}

bool IsLevelScalarSetCandidate(const FProperty* Property)
{
    if (Property == nullptr
        || Property->HasAnyPropertyFlags(
            CPF_Transient | CPF_DuplicateTransient | CPF_Deprecated)
        || !Property->HasAnyPropertyFlags(CPF_Edit)
        || Property->HasAnyPropertyFlags(CPF_EditorOnly))
    {
        return false;
    }
    return Property->IsA(FBoolProperty::StaticClass())
        || Property->IsA(FIntProperty::StaticClass())
        || Property->IsA(FInt64Property::StaticClass())
        || Property->IsA(FFloatProperty::StaticClass())
        || Property->IsA(FDoubleProperty::StaticClass())
        || Property->IsA(FStrProperty::StaticClass())
        || Property->IsA(FNameProperty::StaticClass());
}

void AppendLevelFieldSchema(
    FString& OutText,
    const UObject* Object,
    const TSet<FName>& IdentityFields)
{
    if (Object == nullptr)
    {
        return;
    }
    constexpr int32 MaxSchemaFields = 64;
    int32 FieldCount = 0;
    for (TFieldIterator<FProperty> It(Object->GetClass()); It; ++It)
    {
        const FProperty* Property = *It;
        if (Property == nullptr
            || Property->HasAnyPropertyFlags(CPF_EditorOnly)
            || IdentityFields.Contains(Property->GetFName()))
        {
            continue;
        }
        if (FieldCount >= MaxSchemaFields)
        {
            OutText += TEXT("\n  ... additional fields bounded");
            break;
        }
        const bool bWritable = IsLevelScalarSetCandidate(Property);
        const bool bResettable = bWritable
            && !Property->HasMetaData(TEXT("NoResetToDefault"));
        OutText += FString::Printf(
            TEXT("\n  %s:\n    type: %s\n    writable: %s\n    resettable: %s"),
            *Property->GetName(),
            *NativePropertyTypeText(Property),
            bWritable ? TEXT("true") : TEXT("false"),
            bResettable ? TEXT("true") : TEXT("false"));
        ++FieldCount;
    }
}

bool HasPCGManagedComponent(const AActor* Actor)
{
    if (Actor == nullptr)
    {
        return false;
    }
    if (Actor->FindComponentByClass<UPCGComponent>() != nullptr)
    {
        return true;
    }
    for (const UActorComponent* Component : Actor->GetComponents())
    {
        if (Component != nullptr
            && Component->IsA(UPCGComponent::StaticClass()))
        {
            return true;
        }
    }
    return false;
}

FString LevelActorSchemaText(
    const FLevelActorEntry& Entry,
    const FString& ActorId)
{
    FString Text = FString::Printf(
        TEXT("schema:\n  subject: actor\n  identity: @%s\n  type: %s\n  loaded: %s"),
        *ActorId,
        *Entry.Type,
        Entry.bLoaded ? TEXT("true") : TEXT("false"));
    if (!Entry.bLoaded)
    {
        Text += TEXT(
            "\n  mutation: unavailable (Actor is an unloaded descriptor; live "
            "property schema is not advertised)");
        return Text;
    }
    Text += TEXT(
        "\n  mutation: active for schema-advertised set/reset fields and the "
        "compound transform; lifecycle creation and removal are partial");
    const AActor* Actor = Entry.Actor.Get();
    if (!IsValid(Actor))
    {
        Text += TEXT(
            "\n  mutation: unavailable (the loaded Actor UObject is invalid)");
        return Text;
    }
    if (HasPCGManagedComponent(Actor))
    {
        Text += TEXT(
            "\n  constraint: PCG async suppression is not proven; transform, "
            "reconstruction, and lifecycle edits on this Actor are "
            "unavailable");
    }
    Text += TEXT(
        "\n  identity fields (read-only): id, type, Name, ActorLabel, path, "
        "package, levelInstance, loaded, external, descriptor, "
        "identityValid, identityUnique");
    Text += TEXT(
        "\n  compound:\n"
        "    transform:\n"
        "      operation: SetActorTransform (invoke)\n"
        "      location: [x, y, z] (omit to preserve)\n"
        "      rotation: [pitch, yaw, roll] degrees (omit to preserve)\n"
        "      scale: [x, y, z] (omit to preserve)");
    Text += TEXT(
        "\n  lifecycle:\n"
        "    create: available through Palette-backed add (class-only "
        "entries)\n"
        "    remove: available for Palette-created and instance-owned "
        "Actors");
    Text += TEXT("\n  fields:");
    AppendLevelFieldSchema(
        Text,
        Actor,
        {
            TEXT("id"),
            TEXT("type"),
            TEXT("Name"),
            TEXT("ActorLabel"),
            TEXT("path"),
            TEXT("package"),
            TEXT("levelInstance"),
            TEXT("loaded"),
            TEXT("external"),
            TEXT("descriptor"),
            TEXT("identityValid"),
            TEXT("identityUnique"),
        });
    return Text;
}

FString LevelComponentSchemaText(
    const FLevelActorEntry& ActorEntry,
    const FSalLevelComponentEntry& ComponentEntry,
    const FString& ActorId,
    const FString& Source,
    const FString& Id,
    const FString& ComponentType)
{
    FString Text = FString::Printf(
        TEXT("schema:\n  subject: component\n  identity: @%s/%s/%s\n  type: %s\n  source: %s"),
        *ActorId,
        *Source,
        *Id,
        *ComponentType,
        *Source);
    Text += TEXT(
        "\n  mutation: inactive (authored Patch capability is not landed; "
        "fields are classified candidates revalidated at Patch planning)");
    const AActor* Actor = ActorEntry.Actor.Get();
    const UActorComponent* Component = ComponentEntry.Component.Get();
    if (!IsValid(Actor) || !IsValid(Component))
    {
        Text += TEXT(
            "\n  mutation: unavailable (the loaded Component UObject is "
            "invalid)");
        return Text;
    }
    if (HasPCGManagedComponent(Actor))
    {
        Text += TEXT(
            "\n  constraint: PCG async suppression is not proven; edits that "
            "could schedule generate or cleanup are unavailable");
    }
    Text += TEXT(
        "\n  identity fields (read-only): id, name, type, loaded, "
        "registered, stableRefAvailable");
    Text += TEXT(
        "\n  lifecycle:\n"
        "    create: unavailable\n"
        "    remove: unavailable (preview-World execution is required)");
    Text += TEXT("\n  fields:");
    AppendLevelFieldSchema(
        Text,
        Component,
        {
            TEXT("id"),
            TEXT("name"),
            TEXT("type"),
            TEXT("loaded"),
            TEXT("registered"),
            TEXT("stableRefAvailable"),
        });
    return Text;
}

TSharedPtr<FJsonObject> QueryExactActor(
    const FSalQuery& Query,
    const FSalResolvedTarget& Target)
{
    FString UnsupportedDetail;
    if (!LevelExactSchemaGate(Query, UnsupportedDetail))
    {
        return QueryError(
            UnsupportedDetail.IsEmpty()
                ? TEXT("capability.clause_unavailable")
                : TEXT("capability.detail_unavailable"),
            UnsupportedDetail.IsEmpty()
                ? TEXT("Exact Level Actor read accepts only optional with schema.")
                : FString::Printf(
                    TEXT("Exact Level Actor read does not support with %s in this capability."),
                    *UnsupportedDetail),
            TEXT("actor"));
    }
    FString Id;
    Query.Operation->TryGetStringField(TEXT("id"), Id);
    FGuid Guid;
    if (!ParseActorGuid(Id, Guid))
    {
        return QueryError(
            TEXT("validation.invalid_reference"),
            TEXT("Exact Level Actor identity must be one valid hyphenated ActorGuid."),
            TEXT("actor"),
            Id);
    }

    FLevelSnapshot Snapshot;
    FString Reason;
    if (!BuildSnapshot(Target, TEXT("actor"), Snapshot, Reason))
    {
        return ContentUnavailable(TEXT("actor"), Target, Reason);
    }
    if (!Snapshot.bIdentityComplete)
    {
        return QueryError(
            TEXT("validation.reference_scan_incomplete"),
            TEXT("The Level Actor identity environment is incomplete; exact Actor resolution is fail-closed."),
            TEXT("actor"),
            Id);
    }

    FLevelActorEntry* Match = nullptr;
    int32 MatchCount = 0;
    for (FLevelActorEntry& Entry : Snapshot.Actors)
    {
        if (Entry.Guid == Guid)
        {
            Match = &Entry;
            ++MatchCount;
        }
    }
    if (MatchCount != 1 || Match == nullptr)
    {
        return QueryError(
            MatchCount > 1
                ? TEXT("resolution.identity_conflict")
                : TEXT("resolution.object_not_found"),
            MatchCount > 1
                ? TEXT("Multiple persisted Level Actor records share this ActorGuid.")
                : TEXT("No persisted Actor or root World Partition descriptor has this ActorGuid in the exact Level Target."),
            TEXT("actor"),
            Id);
    }

    FLevelScanBudget SourceBudget;
    const IAssetRegistry& Registry =
        FModuleManager::LoadModuleChecked<FAssetRegistryModule>(
            TEXT("AssetRegistry"))
            .Get();
    ResolveLevelInstanceSourceForEntry(
        Snapshot,
        *Match,
        Target,
        SourceBudget,
        TEXT("actor"),
        Registry);

    FSalObjectBuilder Builder;
    const FString LevelAlias = Builder.UniqueAlias(
        Query.Alias.IsEmpty() ? TargetName(Target) : Query.Alias);
    Builder.AddLocalBinding(
        LevelAlias,
        LevelValue(Target, &Snapshot, false));
    const FString Preferred = !Match->Label.IsEmpty()
        ? Match->Label
        : !Match->Name.IsEmpty()
            ? Match->Name
            : TEXT("actor");
    const FString ActorAlias = Builder.UniqueAlias(Preferred);
    TSharedPtr<FJsonObject> ActorFields;
    Builder.AddLocalBinding(
        ActorAlias,
        ActorValue(
            *Match,
            LevelAlias,
            true,
            &ActorFields));
    if (Query.With.Contains(TEXT("schema")))
    {
        Builder.AddComment(
            LevelActorSchemaText(*Match, GuidText(Guid)));
    }
    TSharedPtr<FJsonObject> Result = Builder.BuildResult(
        Snapshot.FinalDiagnostics(TEXT("actor")));
    if (ActorFields.IsValid()
        && !Match->LevelInstanceSourceAsset.IsEmpty())
    {
        FPendingLevelInstanceHandoff Pending;
        Pending.ActorFields = MoveTemp(ActorFields);
        Pending.Target = LevelTargetValue(
            Match->LevelInstanceSourceAsset);
        Pending.PreferredAlias =
            FPackageName::ObjectPathToObjectName(
                Match->LevelInstanceSourceAsset)
            + TEXT("_source");
        AddLevelInstanceHandoffs(Result, {Pending});
    }
    AActor* Actor = Match->Actor.Get();
    if (IsValid(Actor))
    {
        AddDeclarationHandoffs(
            Result,
            Actor->GetClass());
    }
    return Result;
}

TSharedPtr<FJsonObject> QueryExactComponent(
    const FSalQuery& Query,
    const FSalResolvedTarget& Target)
{
    FString UnsupportedDetail;
    if (!LevelExactSchemaGate(Query, UnsupportedDetail))
    {
        return QueryError(
            UnsupportedDetail.IsEmpty()
                ? TEXT("capability.clause_unavailable")
                : TEXT("capability.detail_unavailable"),
            UnsupportedDetail.IsEmpty()
                ? TEXT("Exact Level Component read accepts only optional with schema.")
                : FString::Printf(
                    TEXT("Exact Level Component read does not support with %s in this capability."),
                    *UnsupportedDetail),
            TEXT("component"));
    }
    FString ActorId;
    FString Source;
    FString Id;
    if (!Query.Operation->TryGetStringField(TEXT("actorId"), ActorId)
        || !Query.Operation->TryGetStringField(TEXT("source"), Source)
        || !Query.Operation->TryGetStringField(TEXT("id"), Id))
    {
        return QueryError(
            TEXT("validation.invalid_reference"),
            TEXT("Exact Level Component identity requires actorId, source, and id."),
            TEXT("component"));
    }
    FGuid ActorGuid;
    if (!ParseActorGuid(ActorId, ActorGuid)
        || (Source != TEXT("native")
            && Source != TEXT("scs")
            && Source != TEXT("instance"))
        || Id.IsEmpty())
    {
        return QueryError(
            TEXT("validation.invalid_reference"),
            TEXT("Exact Level Component identity must contain a canonical ActorGuid, source native/scs/instance, and a non-empty canonical slot id."),
            TEXT("component"));
    }

    FLevelSnapshot LevelSnapshot;
    FString Reason;
    if (!BuildSnapshot(Target, TEXT("component"), LevelSnapshot, Reason))
    {
        return ContentUnavailable(TEXT("component"), Target, Reason);
    }
    if (!LevelSnapshot.bIdentityComplete)
    {
        return QueryError(
            TEXT("validation.reference_scan_incomplete"),
            TEXT("The Level Actor identity environment is incomplete; exact Component resolution is fail-closed."),
            TEXT("component"),
            ActorId);
    }
    FLevelActorEntry* Owner = nullptr;
    int32 OwnerMatches = 0;
    for (FLevelActorEntry& Entry : LevelSnapshot.Actors)
    {
        if (Entry.Guid == ActorGuid)
        {
            Owner = &Entry;
            ++OwnerMatches;
        }
    }
    if (OwnerMatches != 1 || Owner == nullptr)
    {
        return QueryError(
            OwnerMatches > 1
                ? TEXT("resolution.identity_conflict")
                : TEXT("resolution.object_not_found"),
            OwnerMatches > 1
                ? TEXT("Multiple persisted Level Actor records share the Component owner ActorGuid.")
                : TEXT("The Component owner ActorGuid was not found in the exact Level Target."),
            TEXT("component"),
            ActorId);
    }
    AActor* OwnerActor = Owner->Actor.Get();
    if (!Owner->bLoaded || !IsValid(OwnerActor))
    {
        return QueryError(
            TEXT("capability.component_owner_not_loaded"),
            TEXT("Component Query cannot inspect an unloaded Actor descriptor and will not load or pin its Actor."),
            TEXT("component"),
            ActorId);
    }

    FSalLevelComponentSnapshot ComponentSnapshot;
    if (!BuildLevelComponentSnapshot(
            TArray<AActor*>{OwnerActor},
            TEXT("component"),
            ComponentSnapshot,
            Reason)
        || !ComponentSnapshot.bIdentityComplete)
    {
        return QueryError(
            TEXT("validation.reference_scan_incomplete"),
            Reason.IsEmpty()
                ? TEXT("The Component source identity scan is incomplete; exact resolution is fail-closed.")
                : Reason,
            TEXT("component"),
            Id);
    }
    const FSalLevelComponentEntry* Match = nullptr;
    int32 MatchCount = 0;
    for (const FSalLevelComponentEntry& Entry : ComponentSnapshot.Entries)
    {
        if (Entry.ActorGuid == ActorGuid
            && Entry.Source == Source
            && Entry.Id == Id)
        {
            Match = &Entry;
            ++MatchCount;
        }
    }
    if (MatchCount != 1 || Match == nullptr)
    {
        return QueryError(
            MatchCount > 1
                ? TEXT("resolution.identity_conflict")
                : TEXT("resolution.object_not_found"),
            MatchCount > 1
                ? TEXT("Multiple persistent Components share this source-qualified slot id.")
                : TEXT("No persistent Component matches this exact source-qualified identity."),
            TEXT("component"),
            Id);
    }

    FSalObjectBuilder Builder;
    const FString LevelAlias = Builder.UniqueAlias(
        Query.Alias.IsEmpty() ? TargetName(Target) : Query.Alias);
    Builder.AddLocalBinding(
        LevelAlias,
        LevelValue(Target, &LevelSnapshot, false));
    TSharedPtr<FJsonObject> ComponentFields;
    Builder.AddLocalBinding(
        Builder.UniqueAlias(
            Match->Name.IsEmpty() ? TEXT("component") : Match->Name),
        ComponentValue(*Match, &ComponentFields));
    if (Query.With.Contains(TEXT("schema")))
    {
        Builder.AddComment(
            LevelComponentSchemaText(
                *Owner,
                *Match,
                GuidText(ActorGuid),
                Match->Source,
                Match->Id,
                Match->Type));
    }
    TSharedPtr<FJsonObject> Result = Builder.BuildResult(
        LevelSnapshot.FinalDiagnostics(TEXT("component")));
    TSharedPtr<FJsonObject> PcgTarget =
        PcgComponentTargetValue(Target, *Match);
    if (ComponentFields.IsValid() && PcgTarget.IsValid())
    {
        FPendingPcgComponentHandoff Pending;
        Pending.ComponentFields = MoveTemp(ComponentFields);
        Pending.Target = MoveTemp(PcgTarget);
        Pending.PreferredAlias = Match->Name.IsEmpty()
            ? TEXT("pcg_component")
            : Match->Name + TEXT("_pcg");
        AddPcgComponentHandoffs(Result, {Pending});
    }
    UActorComponent* Component = Match->Component.Get();
    if (IsValid(Component))
    {
        UClass* BlueprintDeclaringClass = nullptr;
        if (Match->Source == TEXT("scs")
            && !Match->DeclaringClass.IsEmpty())
        {
            BlueprintDeclaringClass =
                FindLoadedClassInActorHierarchy(
                    Match->Actor.Get(),
                    Match->DeclaringClass);
        }
        AddDeclarationHandoffs(
            Result,
            Component->GetClass(),
            BlueprintDeclaringClass,
            Match->Source == TEXT("scs"));
    }
    return Result;
}

// ============================================================================
// Level Palette discovery (Slice 2)
//
// Destination-bound discovery of Actor and instance Component creation
// capabilities for one exact Level Target. Creation Patch is not active in
// this capability: every entry reports creation as unavailable until the
// preview-World execution path lands. Discovery is read-only; it never loads
// an asset or Actor, switches a map, changes selection, dirties a package,
// or starts an editor workflow. Opaque Palette ids are deterministic digests
// of the exact capability material and are revalidated by re-enumeration on
// every exact replay, so an id never bypasses destination or engine checks.
// ============================================================================

namespace LevelPalette
{
enum class EDestinationKind : uint8
{
    Actor,
    Component
};

struct FDestination
{
    EDestinationKind Kind = EDestinationKind::Actor;
    FGuid ActorGuid;
    FString ActorLabel;
};

struct FEntry
{
    FString Id;
    FString DisplayName;
    FString Category;
    FString NativeType;
    FString SourceAssetType;
    FString KindName;
    bool bCreationAvailable = false;
    FString UnavailableReason;
    // Request-scoped native creation evidence (never retained as identity).
    UClass* ActorClass = nullptr;
    UActorFactory* Factory = nullptr;
    UClass* ComponentClass = nullptr;
};
}

class FLevelPaletteUniqueNameAllocator
{
public:
    void Reserve(const FString& Name)
    {
        Used.Add(Name);
    }

    FString Allocate(const FString& Preferred, const FString& Fallback = TEXT("item"))
    {
        const FString Base = FSalObjectBuilder::SanitizeIdentifier(Preferred, Fallback);
        if (!Used.Contains(Base))
        {
            Used.Add(Base);
            NextSuffix.FindOrAdd(Base) = 2;
            return Base;
        }
        int32& Suffix = NextSuffix.FindOrAdd(Base);
        if (Suffix < 2)
        {
            Suffix = 2;
        }
        for (;;)
        {
            const FString Candidate = FString::Printf(TEXT("%s_%d"), *Base, Suffix++);
            if (!Used.Contains(Candidate))
            {
                Used.Add(Candidate);
                return Candidate;
            }
        }
    }

private:
    TSet<FString> Used;
    TMap<FString, int32> NextSuffix;
};

FString LevelPaletteDigest(const FString& Material)
{
    return FSHA1::HashBuffer(
        *Material,
        Material.Len() * sizeof(TCHAR)).ToString();
}

FString ActorPaletteId(
    const FString& ClassPath,
    const FString& FactoryPath,
    const FString& AssetType,
    const FString& Category)
{
    return TEXT("level.actor.")
        + LevelPaletteDigest(
            TEXT("actor|") + ClassPath + TEXT("|") + FactoryPath
            + TEXT("|") + AssetType + TEXT("|") + Category);
}

FString ComponentPaletteId(const FString& ClassPath)
{
    return TEXT("level.component.") + LevelPaletteDigest(
        TEXT("component|") + ClassPath);
}

bool ResolveLevelPaletteDestination(
    const TSharedPtr<FJsonObject>& Ref,
    const FString& TargetAlias,
    LevelPalette::FDestination& Out,
    FString& OutMessage)
{
    Out = LevelPalette::FDestination();
    OutMessage.Reset();
    if (!Ref.IsValid())
    {
        OutMessage =
            TEXT("Level palette requires one exact destination after to.");
        return false;
    }
    FString RefKind;
    const TArray<TSharedPtr<FJsonValue>>* Path = nullptr;
    if (!Ref->TryGetStringField(TEXT("kind"), RefKind)
        || RefKind != TEXT("member")
        || !Ref->TryGetArrayField(TEXT("path"), Path)
        || Path == nullptr
        || Path->Num() != 1)
    {
        OutMessage = TEXT(
            "A Level Palette destination must be a member reference such as "
            "arena.Actors or @actorGuid.Components.");
        return false;
    }
    FString Member;
    if (!(*Path)[0]->TryGetString(Member))
    {
        OutMessage = TEXT(
            "A Level Palette destination member must be a path name.");
        return false;
    }
    const TSharedPtr<FJsonObject>* ObjectRef = nullptr;
    if (!Ref->TryGetObjectField(TEXT("object"), ObjectRef)
        || ObjectRef == nullptr
        || !(*ObjectRef).IsValid())
    {
        OutMessage = TEXT(
            "A Level Palette destination must name a Level Target or one "
            "exact persisted Actor.");
        return false;
    }
    FString ObjectKind;
    (*ObjectRef)->TryGetStringField(TEXT("kind"), ObjectKind);
    if (Member == TEXT("Actors"))
    {
        FString AliasName;
        if (ObjectKind != TEXT("local")
            || !(*ObjectRef)->TryGetStringField(TEXT("name"), AliasName)
            || AliasName != TargetAlias)
        {
            OutMessage = TEXT(
                "A Level Actor Palette destination must name the currently "
                "bound target alias, such as arena.Actors.");
            return false;
        }
        Out.Kind = LevelPalette::EDestinationKind::Actor;
        return true;
    }
    if (Member == TEXT("Components"))
    {
        FGuid ActorId;
        if (ObjectKind == TEXT("stable_ref"))
        {
            const TArray<TSharedPtr<FJsonValue>>* IdentityPath = nullptr;
            if (!(*ObjectRef)->TryGetArrayField(TEXT("identityPath"), IdentityPath)
                || IdentityPath == nullptr
                || IdentityPath->Num() != 1)
            {
                OutMessage = TEXT(
                    "A Level Component Palette destination Actor identity "
                    "must be exactly one ActorGuid.");
                return false;
            }
            FString GuidText;
            if (!(*IdentityPath)[0]->TryGetString(GuidText)
                || !ParseActorGuid(GuidText, ActorId))
            {
                OutMessage = TEXT(
                    "A Level Component Palette destination ActorGuid is "
                    "invalid or not a persisted Actor identity.");
                return false;
            }
        }
        else if (ObjectKind == TEXT("actor"))
        {
            // LowerQueryForDomain lowers a stable-ref owner through
            // FSalLevelInterface::LowerStableReference, which rewrites it to
            // the exact actor shape {kind: actor, id: ActorGuid}.
            FString ActorIdText;
            if (!(*ObjectRef)->TryGetStringField(TEXT("id"), ActorIdText)
                || !ParseActorGuid(ActorIdText, ActorId))
            {
                OutMessage = TEXT(
                    "A Level Component Palette destination Actor identity "
                    "is invalid or not a persisted Actor identity.");
                return false;
            }
        }
        else
        {
            OutMessage = TEXT(
                "A Level Component Palette destination must be one exact "
                "persisted Actor StableRef, such as @actorGuid.Components.");
            return false;
        }
        Out.ActorGuid = ActorId;
        Out.Kind = LevelPalette::EDestinationKind::Component;
        return true;
    }
    OutMessage = TEXT(
        "A Level Palette destination path must be exactly Actors or "
        "Components.");
    return false;
}

bool ResolveLevelPaletteDestination(
    const FSalQuery& Query,
    LevelPalette::FDestination& Out,
    FString& OutMessage)
{
    const TSharedPtr<FJsonObject>* DestinationRef = nullptr;
    if (!Query.Operation.IsValid()
        || !Query.Operation->TryGetObjectField(TEXT("to"), DestinationRef)
        || DestinationRef == nullptr
        || !(*DestinationRef).IsValid())
    {
        OutMessage =
            TEXT("Level palette requires one exact destination after to.");
        return false;
    }
    return ResolveLevelPaletteDestination(
        *DestinationRef,
        Query.Alias,
        Out,
        OutMessage);
}

// Returns nullptr on success, otherwise the fail-closed Query error result.
TSharedPtr<FJsonObject> ResolveLevelPaletteContext(
    const FSalQuery& Query,
    const FSalResolvedTarget& Target,
    const FString& Operation,
    LevelPalette::FDestination& Out,
    FString& OutActorLabel)
{
    OutActorLabel.Reset();
    FString Message;
    if (!ResolveLevelPaletteDestination(Query, Out, Message))
    {
        return QueryError(
            TEXT("validation.palette_context_invalid"),
            Message,
            Operation);
    }
    if (Out.Kind == LevelPalette::EDestinationKind::Actor)
    {
        UWorld* World = nullptr;
        ULevel* Level = nullptr;
        FString Reason;
        if (!IsExactLoadedSource(Target, World, Level, Reason))
        {
            return ContentUnavailable(Operation, Target, Reason);
        }
        return nullptr;
    }
    FLevelSnapshot Snapshot;
    FString SnapshotReason;
    if (!BuildSnapshot(Target, Operation, Snapshot, SnapshotReason))
    {
        return ContentUnavailable(Operation, Target, SnapshotReason);
    }
    if (!Snapshot.bIdentityComplete)
    {
        return QueryError(
            TEXT("validation.reference_scan_incomplete"),
            TEXT("The Level Actor identity environment is incomplete; "
                "Palette destination resolution is fail-closed."),
            Operation);
    }
    const FLevelActorEntry* Match = nullptr;
    int32 MatchCount = 0;
    for (const FLevelActorEntry& Entry : Snapshot.Actors)
    {
        if (Entry.Guid == Out.ActorGuid)
        {
            Match = &Entry;
            ++MatchCount;
        }
    }
    if (MatchCount != 1 || Match == nullptr || !IsValid(Match->Actor.Get()))
    {
        return QueryError(
            MatchCount > 1
                ? TEXT("resolution.identity_conflict")
                : TEXT("resolution.object_not_found"),
            MatchCount > 1
                ? TEXT("Multiple persisted Level Actor records share this "
                    "ActorGuid.")
                : TEXT("No loaded persisted Actor has this ActorGuid in the "
                    "exact Level Target."),
            Operation,
            Out.ActorGuid.ToString(EGuidFormats::DigitsWithHyphensLower));
    }
    OutActorLabel = Match->Label;
    return nullptr;
}

bool DiscoverActorPaletteEntries(TArray<LevelPalette::FEntry>& Out)
{
    // The placement catalog module is an editor module loaded on demand; in
    // headless automation it is not resident until requested.
    IPlacementModeModule& Placement = IPlacementModeModule::Get();
    TArray<FPlacementCategoryInfo> Categories;
    Placement.GetSortedCategories(Categories);
    for (const FPlacementCategoryInfo& Category : Categories)
    {
        const FName Handle = Category.UniqueHandle;
        if (Handle == FBuiltInPlacementCategories::Favorites()
            || Handle == FBuiltInPlacementCategories::RecentlyPlaced())
        {
            // Session state is not authored capability.
            continue;
        }
        TArray<TSharedPtr<FPlaceableItem>> Items;
        Placement.GetFilteredItemsForCategory(
            Handle,
            Items,
            [](const TSharedPtr<FPlaceableItem>&)
            {
                return true;
            });
        for (const TSharedPtr<FPlaceableItem>& Item : Items)
        {
            if (!Item.IsValid() || Item->DragHandler.IsValid())
            {
                // Custom drag handlers are editor UI, not placeable classes.
                continue;
            }
            UClass* ActorClass = nullptr;
            if (Item->Factory != nullptr)
            {
                ActorClass = Item->Factory->GetDefaultActorClass(
                    Item->AssetData);
            }
            if (ActorClass == nullptr
                || !ActorClass->IsChildOf(AActor::StaticClass()))
            {
                continue;
            }
            const FString ClassPath =
                ActorClass->GetClassPathName().ToString();
            const FString FactoryPath =
                Item->Factory != nullptr
                    ? Item->Factory->GetClass()->GetClassPathName().ToString()
                    : FString();
            FString SourceAssetType;
            if (Item->AssetData.IsValid()
                && Item->AssetData.GetClass() != nullptr
                && Item->AssetData.GetClass() != UClass::StaticClass())
            {
                SourceAssetType =
                    Item->AssetData.AssetClassPath.ToString();
            }
            LevelPalette::FEntry Entry;
            Entry.Category = Category.DisplayName.ToString();
            Entry.Id = ActorPaletteId(
                ClassPath,
                FactoryPath,
                SourceAssetType,
                Entry.Category);
            Entry.DisplayName = Item->DisplayName.ToString();
            if (Entry.DisplayName.IsEmpty())
            {
                Entry.DisplayName = Item->NativeName;
            }
            Entry.NativeType = ClassPath;
            Entry.SourceAssetType = SourceAssetType;
            Entry.KindName = TEXT("actor");
            Entry.ActorClass = ActorClass;
            Entry.Factory = Item->Factory;
            if (SourceAssetType.IsEmpty())
            {
                Entry.bCreationAvailable = true;
            }
            else
            {
                Entry.bCreationAvailable = false;
                Entry.UnavailableReason = TEXT(
                    "This Actor entry requires an explicit source Asset, "
                    "which Level creation Patch does not yet accept.");
            }
            Out.Add(MoveTemp(Entry));
        }
    }
    return true;
}

void DiscoverComponentPaletteEntries(TArray<LevelPalette::FEntry>& Out)
{
    TArray<FComponentClassComboEntryPtr>* ComponentList = nullptr;
    FComponentTypeRegistry& Registry = FComponentTypeRegistry::Get();
    Registry.SubscribeToComponentList(ComponentList);
    if (ComponentList == nullptr)
    {
        return;
    }
    for (const FComponentClassComboEntryPtr& RawEntry : *ComponentList)
    {
        if (!RawEntry.IsValid()
            || RawEntry->IsHeading()
            || RawEntry->IsSeparator()
            || !RawEntry->IsClass()
            || RawEntry->GetComponentCreateAction()
                != EComponentCreateAction::SpawnExistingClass)
        {
            continue;
        }
        const UClass* ComponentClass = RawEntry->GetComponentClass();
        if (ComponentClass == nullptr
            || !ComponentClass->IsChildOf(UActorComponent::StaticClass())
            || ComponentClass->HasAnyClassFlags(CLASS_Abstract))
        {
            continue;
        }
        const FString ClassPath =
            ComponentClass->GetClassPathName().ToString();
        LevelPalette::FEntry Entry;
        Entry.Id = ComponentPaletteId(ClassPath);
        Entry.DisplayName = ComponentClass->GetDisplayNameText().ToString();
        if (Entry.DisplayName.IsEmpty())
        {
            Entry.DisplayName = ComponentClass->GetName();
        }
        Entry.Category = TEXT("Components");
        Entry.NativeType = ClassPath;
        Entry.KindName = TEXT("component");
        Entry.ComponentClass = const_cast<UClass*>(ComponentClass);
        Entry.bCreationAvailable = true;
        Out.Add(MoveTemp(Entry));
    }
}

bool LevelPaletteMatchesSearch(
    const LevelPalette::FEntry& Entry,
    const FString& Search)
{
    if (Search.IsEmpty())
    {
        return true;
    }
    return Entry.DisplayName.Contains(Search, ESearchCase::IgnoreCase)
        || Entry.Category.Contains(Search, ESearchCase::IgnoreCase)
        || Entry.NativeType.Contains(Search, ESearchCase::IgnoreCase);
}

void SortLevelPaletteEntries(TArray<LevelPalette::FEntry>& Entries)
{
    Entries.StableSort(
        [](const LevelPalette::FEntry& A, const LevelPalette::FEntry& B)
        {
            const int32 CategoryCompare = A.Category.Compare(B.Category);
            if (CategoryCompare != 0)
            {
                return CategoryCompare < 0;
            }
            return A.DisplayName.Compare(B.DisplayName) < 0;
        });
}

FString LevelPaletteFingerprint(
    const FSalQuery& Query,
    const FSalResolvedTarget& Target,
    const LevelPalette::FDestination& Destination)
{
    FCursorFingerprintBuilder Fingerprint;
    Fingerprint.Add(TEXT("level_palette1"));
    Fingerprint.Add(Target.AssetPath);
    Fingerprint.Add(
        Destination.Kind == LevelPalette::EDestinationKind::Actor
            ? TEXT("actor")
            : TEXT("component"));
    Fingerprint.Add(
        Destination.ActorGuid.ToString(EGuidFormats::DigitsWithHyphensLower));
    FString Search;
    Query.Operation->TryGetStringField(TEXT("text"), Search);
    Fingerprint.Add(Search);
    return Fingerprint.Finalize();
}

bool DecodeLevelPalettePage(
    const FSalQuery& Query,
    const FSalResolvedTarget& Target,
    const LevelPalette::FDestination& Destination,
    int32& OutOffset)
{
    OutOffset = 0;
    if (Query.PageAfter.IsEmpty())
    {
        return true;
    }
    const FString Expected =
        LevelPaletteFingerprint(Query, Target, Destination);
    TArray<FString> Parts;
    Query.PageAfter.ParseIntoArray(Parts, TEXT(":"), false);
    return Parts.Num() == 3
        && Parts[0] == TEXT("level_palette1")
        && Parts[1].Equals(Expected, ESearchCase::IgnoreCase)
        && ParseNonNegativeInt32(Parts[2], OutOffset);
}

TSharedPtr<FJsonValue> MakeLevelPaletteEntryValue(
    const LevelPalette::FEntry& Entry)
{
    TSharedPtr<FJsonObject> Args = MakeShared<FJsonObject>();
    Args->SetStringField(TEXT("palette"), Entry.Id);
    Args->SetStringField(TEXT("name"), Entry.DisplayName);
    Args->SetStringField(TEXT("category"), Entry.Category);
    Args->SetStringField(TEXT("type"), Entry.NativeType);
    if (!Entry.SourceAssetType.IsEmpty())
    {
        Args->SetStringField(TEXT("asset"), Entry.SourceAssetType);
    }
    Args->SetStringField(
        TEXT("creation"),
        Entry.bCreationAvailable ? TEXT("available") : TEXT("unavailable"));
    if (!Entry.UnavailableReason.IsEmpty())
    {
        Args->SetStringField(TEXT("reason"), Entry.UnavailableReason);
    }
    return Value::Call(Entry.KindName, Args);
}

TSharedPtr<FJsonObject> QueryLevelPaletteEntries(
    const FSalQuery& Query,
    const FSalResolvedTarget& Target)
{
    if (Query.Where.IsValid()
        || !Query.With.IsEmpty()
        || !Query.OrderBy.IsEmpty())
    {
        return QueryError(
            TEXT("capability.clause_unavailable"),
            TEXT("Level palette entries accepts only an optional text search "
                "and a page clause."),
            TEXT("palette_entries"));
    }
    LevelPalette::FDestination Destination;
    FString ActorLabel;
    if (const TSharedPtr<FJsonObject> Error =
            ResolveLevelPaletteContext(
                Query,
                Target,
                TEXT("palette_entries"),
                Destination,
                ActorLabel))
    {
        return Error;
    }
    int32 Offset = 0;
    if (!DecodeLevelPalettePage(Query, Target, Destination, Offset))
    {
        return QueryError(
            TEXT("validation.invalid_cursor"),
            TEXT("Level Palette cursor does not belong to this destination, "
                "target, search, or page limit."),
            TEXT("palette_entries"),
            Query.PageAfter);
    }

    TArray<LevelPalette::FEntry> Entries;
    if (Destination.Kind == LevelPalette::EDestinationKind::Actor)
    {
        DiscoverActorPaletteEntries(Entries);
    }
    else
    {
        DiscoverComponentPaletteEntries(Entries);
    }
    FString SearchText;
    Query.Operation->TryGetStringField(TEXT("text"), SearchText);
    TArray<LevelPalette::FEntry> Matches;
    for (const LevelPalette::FEntry& Entry : Entries)
    {
        if (LevelPaletteMatchesSearch(Entry, SearchText))
        {
            Matches.Add(Entry);
        }
    }
    SortLevelPaletteEntries(Matches);

    const int32 Limit = FMath::Clamp(
        Query.PageLimit > 0 ? Query.PageLimit : DefaultCollectionLimit,
        1,
        MaxCollectionLimit);
    FSalObjectBuilder Builder;
    FLevelPaletteUniqueNameAllocator Aliases;
    Aliases.Reserve(Query.Alias);
    int32 Added = 0;
    for (int32 Index = Offset;
        Index < Matches.Num() && Added < Limit;
        ++Index)
    {
        const LevelPalette::FEntry& Entry = Matches[Index];
        Builder.AddLocalBinding(
            Aliases.Allocate(Entry.DisplayName, Entry.KindName),
            MakeLevelPaletteEntryValue(Entry));
        ++Added;
    }
    if (Matches.IsEmpty())
    {
        Builder.AddComment(
            TEXT("no palette matches in this bounded discovery page"));
    }
    TSharedPtr<FJsonObject> Result = Builder.BuildResult();
    const int32 NextOffset = Offset + Added;
    if (NextOffset < Matches.Num())
    {
        TSharedPtr<FJsonObject> PageObject = MakeShared<FJsonObject>();
        PageObject->SetStringField(
            TEXT("next"),
            TEXT("level_palette1:")
                + LevelPaletteFingerprint(Query, Target, Destination)
                + TEXT(":") + LexToString(NextOffset));
        Result->SetObjectField(TEXT("page"), PageObject);
    }
    return Result;
}

TSharedPtr<FJsonObject> QueryLevelPalette(
    const FSalQuery& Query,
    const FSalResolvedTarget& Target)
{
    if (Query.Where.IsValid()
        || !Query.OrderBy.IsEmpty()
        || Query.PageLimit > 0
        || !Query.PageAfter.IsEmpty())
    {
        return QueryError(
            TEXT("capability.clause_unavailable"),
            TEXT("Exact Level Palette Query accepts only optional with "
                "schema."),
            TEXT("palette"));
    }
    for (const FString& Detail : Query.With)
    {
        if (Detail != TEXT("schema"))
        {
            return QueryError(
                TEXT("capability.detail_unavailable"),
                TEXT("Exact Level Palette Query supports only with schema."),
                TEXT("palette"),
                Detail);
        }
    }
    FString PaletteId;
    if (!Query.Operation->TryGetStringField(TEXT("id"), PaletteId)
        || PaletteId.IsEmpty())
    {
        return QueryError(
            TEXT("validation.palette_context_invalid"),
            TEXT("Exact Level Palette Query requires a Palette identity and "
                "one exact destination after to."),
            TEXT("palette"),
            PaletteId);
    }
    LevelPalette::FDestination Destination;
    FString ActorLabel;
    if (const TSharedPtr<FJsonObject> Error =
            ResolveLevelPaletteContext(
                Query,
                Target,
                TEXT("palette"),
                Destination,
                ActorLabel))
    {
        return Error;
    }

    TArray<LevelPalette::FEntry> Entries;
    if (Destination.Kind == LevelPalette::EDestinationKind::Actor)
    {
        DiscoverActorPaletteEntries(Entries);
    }
    else
    {
        DiscoverComponentPaletteEntries(Entries);
    }
    const LevelPalette::FEntry* Match = nullptr;
    int32 MatchCount = 0;
    for (const LevelPalette::FEntry& Entry : Entries)
    {
        if (Entry.Id == PaletteId)
        {
            Match = &Entry;
            ++MatchCount;
        }
    }
    if (MatchCount != 1 || Match == nullptr)
    {
        return QueryError(
            MatchCount > 1
                ? TEXT("resolution.palette_ambiguous")
                : TEXT("resolution.palette_not_found"),
            MatchCount > 1
                ? TEXT("Palette identity matches multiple creation "
                    "capabilities for this destination.")
                : TEXT("Palette entry was not found for this exact "
                    "destination; the capability may be stale or engine-"
                    "specific."),
            TEXT("palette"),
            PaletteId,
            {TEXT("Run palette entries again for the same destination.")});
    }

    FSalObjectBuilder Builder;
    FLevelPaletteUniqueNameAllocator Aliases;
    Aliases.Reserve(Query.Alias);
    Builder.AddLocalBinding(
        Aliases.Allocate(Match->DisplayName, Match->KindName),
        MakeLevelPaletteEntryValue(*Match));
    if (Query.With.Contains(TEXT("schema")))
    {
        Builder.AddComment(FString::Printf(
            TEXT("palette schema:\n"
                "  creation object: { palette: \"%s\" }\n"
                "  destination: %s\n"
                "  native type: %s\n"
                "  creation: unavailable (%s)"),
            *Match->Id,
            Destination.Kind == LevelPalette::EDestinationKind::Actor
                ? TEXT("arena.Actors (Level Actor creation)")
                : *FString::Printf(
                    TEXT("@%s.Components (instance Component creation)"),
                    *Destination.ActorGuid.ToString(
                        EGuidFormats::DigitsWithHyphensLower)),
            *Match->NativeType,
            *Match->UnavailableReason));
    }
    return Builder.BuildResult();
}

// ============================================================================
// Level authored mutation (Slice 3)
//
// Exact-schema `set` and `reset` on loaded persisted Actor and Component
// scalar fields, planned and applied inside one top-level transaction with
// dry-run planning, native notifications, readback, and rollback. Lifecycle,
// transform invoke, attachment, and save land in later increments and fail
// closed here. Every edit is revalidated against the schema-advertised
// surface before planning.
// ============================================================================

struct FLevelPlannedEdit
{
    FString Kind; // "set" | "reset" | "transform"
    FString RefText;
    TWeakObjectPtr<UObject> Object;
    FProperty* Property = nullptr;
    FString Before;
    FString After;
    // Compound Actor transform (SetActorTransform) inputs; unset means the
    // native current value is preserved.
    TOptional<FVector> TransformLocation;
    TOptional<FVector> TransformRotation; // degrees: pitch, yaw, roll
    TOptional<FVector> TransformScale;
};

struct FLevelPlannedLifecycle
{
    FString Kind; // "add" | "remove"
    FString Alias;     // add: the creation binding alias
    FString PaletteId; // add: the opaque entry id
    FString RefText;   // remove: canonical identity
    TWeakObjectPtr<AActor> Actor; // remove target or created actor
    LevelPalette::FEntry Entry;   // add: resolved creation capability
    FString CreatedId;            // add: final ActorGuid after apply
    FString CreatedType;
};

bool LevelValueImportText(
    const TSharedPtr<FJsonValue>& Expression,
    FString& OutText)
{
    OutText.Reset();
    if (!Expression.IsValid())
    {
        return false;
    }
    bool Boolean = false;
    double Number = 0.0;
    FString String;
    if (Expression->TryGetBool(Boolean))
    {
        OutText = Boolean ? TEXT("true") : TEXT("false");
        return true;
    }
    if (Expression->TryGetNumber(Number))
    {
        OutText = LexToString(Number);
        return true;
    }
    if (Expression->TryGetString(String))
    {
        OutText = String;
        return true;
    }
    return false;
}

bool LevelImportScalarValue(
    FProperty* Property,
    UObject* Object,
    const FString& Text,
    FString& OutError)
{
    if (Property == nullptr || Object == nullptr)
    {
        OutError = TEXT("Level Patch edit target is invalid.");
        return false;
    }
    void* Value = Property->ContainerPtrToValuePtr<void>(Object);
    const TCHAR* End = Property->ImportText_Direct(
        *Text,
        Value,
        Object,
        PPF_None,
        GLog);
    if (End == nullptr)
    {
        OutError = FString::Printf(
            TEXT("UE could not import the requested value for %s."),
            *Property->GetName());
        return false;
    }
    while (*End != TEXT('\0') && FChar::IsWhitespace(*End))
    {
        ++End;
    }
    if (*End != TEXT('\0'))
    {
        OutError = FString::Printf(
            TEXT("The requested value for %s contains unconsumed text."),
            *Property->GetName());
        return false;
    }
    return true;
}

// Validate that UE can import the requested scalar without mutating the
// live object. Planning must never write the instance, including in dry runs.
bool LevelValidateScalarImport(
    FProperty* Property,
    const FString& Text,
    FString& OutError)
{
    if (Property == nullptr)
    {
        OutError = TEXT("Level Patch edit target is invalid.");
        return false;
    }
    void* Scratch = FMemory::Malloc(
        Property->GetSize(),
        Property->GetMinAlignment());
    Property->InitializeValue(Scratch);
    const TCHAR* End = Property->ImportText_Direct(
        *Text,
        Scratch,
        nullptr,
        PPF_None,
        GLog);
    bool bValid = End != nullptr;
    if (bValid)
    {
        while (*End != TEXT('\0') && FChar::IsWhitespace(*End))
        {
            ++End;
        }
        bValid = *End == TEXT('\0');
    }
    if (!bValid && OutError.IsEmpty())
    {
        OutError = FString::Printf(
            TEXT("UE could not import the requested value for %s."),
            *Property->GetName());
    }
    Property->DestroyValue(Scratch);
    FMemory::Free(Scratch);
    return bValid;
}

FString LevelExportScalarValue(
    const FProperty* Property,
    const UObject* Object)
{
    if (Property == nullptr || Object == nullptr)
    {
        return FString();
    }
    const void* Value = Property->ContainerPtrToValuePtr<void>(Object);
    FString Exported;
    Property->ExportText_Direct(
        Exported,
        Value,
        Value,
        const_cast<UObject*>(Object),
        PPF_None);
    return Exported;
}

// Resolve one lowered exact Actor or Component owner ref and the exact
// member field inside the loaded source Level.
bool ResolveLevelPatchMember(
    const TSharedPtr<FJsonObject>& Statement,
    const FSalResolvedTarget& Target,
    FLevelSnapshot& Snapshot,
    FString& OutOwnerIdentity,
    UObject*& OutObject,
    FProperty*& OutProperty,
    FString& OutFieldName,
    FString& OutError)
{
    OutObject = nullptr;
    OutProperty = nullptr;
    OutError.Reset();
    const TSharedPtr<FJsonObject>* TargetRef = nullptr;
    const TSharedPtr<FJsonObject>* Owner = nullptr;
    const TArray<TSharedPtr<FJsonValue>>* Path = nullptr;
    FString TargetKind;
    if (!Statement.IsValid()
        || !Statement->TryGetObjectField(TEXT("target"), TargetRef)
        || TargetRef == nullptr
        || !(*TargetRef).IsValid()
        || !(*TargetRef)->TryGetStringField(TEXT("kind"), TargetKind)
        || TargetKind != TEXT("member")
        || !(*TargetRef)->TryGetObjectField(TEXT("object"), Owner)
        || Owner == nullptr
        || !(*Owner).IsValid()
        || !(*TargetRef)->TryGetArrayField(TEXT("path"), Path)
        || Path == nullptr
        || Path->Num() != 1
        || !(*Path)[0]->TryGetString(OutFieldName)
        || OutFieldName.IsEmpty())
    {
        OutError = TEXT(
            "Level set/reset requires exactly one member field on one exact "
            "persisted Actor or Component.");
        return false;
    }

    FString OwnerKind;
    (*Owner)->TryGetStringField(TEXT("kind"), OwnerKind);
    if (OwnerKind == TEXT("actor"))
    {
        FString Id;
        FGuid Guid;
        if (!(*Owner)->TryGetStringField(TEXT("id"), Id)
            || !ParseActorGuid(Id, Guid))
        {
            OutError = TEXT(
                "Level set/reset owner Actor identity is invalid.");
            return false;
        }
        FLevelActorEntry* Match = nullptr;
        int32 MatchCount = 0;
        for (FLevelActorEntry& Entry : Snapshot.Actors)
        {
            if (Entry.Guid == Guid)
            {
                Match = &Entry;
                ++MatchCount;
            }
        }
        if (MatchCount != 1 || Match == nullptr || !Match->bLoaded)
        {
            OutError = MatchCount > 1
                ? TEXT("Multiple persisted Level Actor records share this "
                    "ActorGuid; the edit is ambiguous.")
                : TEXT("The exact persisted Actor is not loaded or not "
                    "present; Level Patch will not load an unloaded "
                    "descriptor.");
            return false;
        }
        AActor* Actor = Match->Actor.Get();
        if (!IsValid(Actor))
        {
            OutError = TEXT("The exact persisted Actor UObject is invalid.");
            return false;
        }
        OutOwnerIdentity = Guid.ToString(
            EGuidFormats::DigitsWithHyphensLower);
        OutObject = Actor;
    }
    else if (OwnerKind == TEXT("component"))
    {
        FString ActorId;
        FString Source;
        FString Id;
        FGuid ActorGuid;
        if (!(*Owner)->TryGetStringField(TEXT("actorId"), ActorId)
            || !(*Owner)->TryGetStringField(TEXT("source"), Source)
            || !(*Owner)->TryGetStringField(TEXT("id"), Id)
            || !ParseActorGuid(ActorId, ActorGuid))
        {
            OutError = TEXT(
                "Level set/reset owner Component identity is invalid.");
            return false;
        }
        FLevelActorEntry* OwnerEntry = nullptr;
        int32 OwnerMatches = 0;
        for (FLevelActorEntry& Entry : Snapshot.Actors)
        {
            if (Entry.Guid == ActorGuid)
            {
                OwnerEntry = &Entry;
                ++OwnerMatches;
            }
        }
        AActor* OwnerActor = OwnerEntry != nullptr
            ? OwnerEntry->Actor.Get()
            : nullptr;
        if (OwnerMatches != 1 || OwnerEntry == nullptr
            || !OwnerEntry->bLoaded || !IsValid(OwnerActor))
        {
            OutError = TEXT(
                "Level set/reset owner Actor is not a loaded persisted "
                "Actor.");
            return false;
        }
        FSalLevelComponentSnapshot ComponentSnapshot;
        FString ComponentReason;
        if (!BuildLevelComponentSnapshot(
                TArray<AActor*>{OwnerActor},
                TEXT("patch"),
                ComponentSnapshot,
                ComponentReason)
            || !ComponentSnapshot.bIdentityComplete)
        {
            OutError = ComponentReason.IsEmpty()
                ? TEXT("The Component source identity scan is incomplete; "
                    "the edit is fail-closed.")
                : ComponentReason;
            return false;
        }
        const FSalLevelComponentEntry* Match = nullptr;
        int32 MatchCount = 0;
        for (const FSalLevelComponentEntry& Entry
            : ComponentSnapshot.Entries)
        {
            if (Entry.ActorGuid == ActorGuid
                && Entry.Source == Source
                && Entry.Id == Id)
            {
                Match = &Entry;
                ++MatchCount;
            }
        }
        if (MatchCount != 1 || Match == nullptr)
        {
            OutError = TEXT(
                "No unique persisted Component matches the requested "
                "source-qualified identity.");
            return false;
        }
        UActorComponent* Component = Match->Component.Get();
        if (!IsValid(Component))
        {
            OutError = TEXT("The exact persisted Component UObject is "
                "invalid.");
            return false;
        }
        OutOwnerIdentity = ActorId + TEXT("/") + Source + TEXT("/") + Id;
        OutObject = Component;
    }
    else
    {
        OutError = TEXT(
            "Level set/reset owner must be one exact persisted Actor or "
            "Component.");
        return false;
    }

    OutProperty = FindFProperty<FProperty>(
        OutObject->GetClass(),
        FName(*OutFieldName));
    if (OutProperty == nullptr)
    {
        OutError = FString::Printf(
            TEXT("The exact schema does not advertise a field named %s on "
                "this object."),
            *OutFieldName);
        return false;
    }
    if (!IsLevelScalarSetCandidate(OutProperty))
    {
        OutError = FString::Printf(
            TEXT("Field %s is not a schema-advertised scalar set/reset "
                "field on this exact instance."),
            *OutFieldName);
        return false;
    }
    return true;
}

// Resolve one lowered exact Actor owner ref inside the loaded source Level.
bool ResolveLevelPatchActor(
    const TSharedPtr<FJsonObject>& Statement,
    FLevelSnapshot& Snapshot,
    AActor*& OutActor,
    FString& OutIdentity,
    FString& OutError)
{
    OutActor = nullptr;
    OutError.Reset();
    const TSharedPtr<FJsonObject>* TargetRef = nullptr;
    FString TargetKind;
    if (!Statement.IsValid()
        || !Statement->TryGetObjectField(TEXT("target"), TargetRef)
        || TargetRef == nullptr
        || !(*TargetRef).IsValid()
        || !(*TargetRef)->TryGetStringField(TEXT("kind"), TargetKind)
        || TargetKind != TEXT("actor"))
    {
        OutError = TEXT(
            "Level invoke target must be one exact persisted Actor.");
        return false;
    }
    FString Id;
    FGuid Guid;
    if (!(*TargetRef)->TryGetStringField(TEXT("id"), Id)
        || !ParseActorGuid(Id, Guid))
    {
        OutError = TEXT("Level invoke Actor identity is invalid.");
        return false;
    }
    FLevelActorEntry* Match = nullptr;
    int32 MatchCount = 0;
    for (FLevelActorEntry& Entry : Snapshot.Actors)
    {
        if (Entry.Guid == Guid)
        {
            Match = &Entry;
            ++MatchCount;
        }
    }
    if (MatchCount != 1 || Match == nullptr || !Match->bLoaded)
    {
        OutError = MatchCount > 1
            ? TEXT("Multiple persisted Level Actor records share this "
                "ActorGuid; the invoke is ambiguous.")
            : TEXT("The exact persisted Actor is not loaded or not present; "
                "Level Patch will not load an unloaded descriptor.");
        return false;
    }
    AActor* Actor = Match->Actor.Get();
    if (!IsValid(Actor))
    {
        OutError = TEXT("The exact persisted Actor UObject is invalid.");
        return false;
    }
    OutIdentity = Guid.ToString(EGuidFormats::DigitsWithHyphensLower);
    OutActor = Actor;
    return true;
}

bool LevelParseTransformVectorArg(
    const TSharedPtr<FJsonObject>& Args,
    const FString& Name,
    TOptional<FVector>& Out)
{
    if (!Args.IsValid())
    {
        return false;
    }
    const TSharedPtr<FJsonValue> Value = Args->TryGetField(Name);
    if (!Value.IsValid())
    {
        return true; // omitted; the native current value is preserved
    }
    const TArray<TSharedPtr<FJsonValue>>* Elements = nullptr;
    if (!Value->TryGetArray(Elements)
        || Elements == nullptr
        || Elements->Num() != 3)
    {
        return false;
    }
    FVector Result;
    for (int32 Index = 0; Index < 3; ++Index)
    {
        double Number = 0.0;
        if (!(*Elements)[Index].IsValid()
            || !(*Elements)[Index]->TryGetNumber(Number))
        {
            return false;
        }
        Result[Index] = Number;
    }
    Out = Result;
    return true;
}

bool LevelBuildTransformEdit(
    const TSharedPtr<FJsonObject>& Statement,
    AActor* Actor,
    const FString& Identity,
    FLevelPlannedEdit& OutEdit,
    FString& OutError)
{
    OutError.Reset();
    const TSharedPtr<FJsonObject>* Args = nullptr;
    const TArray<TSharedPtr<FJsonValue>>* Outputs = nullptr;
    if (!Statement->TryGetObjectField(TEXT("args"), Args)
        || Args == nullptr
        || !Statement->TryGetArrayField(TEXT("outputs"), Outputs)
        || Outputs == nullptr
        || !Outputs->IsEmpty())
    {
        OutError = TEXT(
            "SetActorTransform accepts only location, rotation, and scale "
            "arguments and has no output object.");
        return false;
    }
    if (HasPCGManagedComponent(Actor))
    {
        OutError = TEXT(
            "Transform on this Actor could schedule PCG generate or cleanup, "
            "and async suppression is not proven; the transform is "
            "unavailable.");
        return false;
    }
    OutEdit.Kind = TEXT("transform");
    OutEdit.RefText = Identity + TEXT(".transform");
    OutEdit.Object = Actor;
    const FTransform Current = Actor->GetActorTransform();
    const FVector CurrentLocation = Current.GetLocation();
    const FRotator CurrentRotation = Current.Rotator();
    const FVector CurrentScale = Current.GetScale3D();
    OutEdit.TransformLocation = CurrentLocation;
    OutEdit.TransformRotation =
        FVector(CurrentRotation.Pitch, CurrentRotation.Yaw, CurrentRotation.Roll);
    OutEdit.TransformScale = CurrentScale;
    if (!LevelParseTransformVectorArg(
            *Args,
            TEXT("location"),
            OutEdit.TransformLocation)
        || !LevelParseTransformVectorArg(
            *Args,
            TEXT("rotation"),
            OutEdit.TransformRotation)
        || !LevelParseTransformVectorArg(
            *Args,
            TEXT("scale"),
            OutEdit.TransformScale))
    {
        OutError = TEXT(
            "SetActorTransform location, rotation, and scale must each be a "
            "three-number array of finite values.");
        return false;
    }
    const FTransform Planned(
        FRotator(
            OutEdit.TransformRotation.GetValue().X,
            OutEdit.TransformRotation.GetValue().Y,
            OutEdit.TransformRotation.GetValue().Z),
        OutEdit.TransformLocation.GetValue(),
        OutEdit.TransformScale.GetValue());
    OutEdit.Before = FString::Printf(
        TEXT("location=(%s) rotation=(%s) scale=(%s)"),
        *CurrentLocation.ToString(),
        *CurrentRotation.ToString(),
        *CurrentScale.ToString());
    OutEdit.After = FString::Printf(
        TEXT("location=(%s) rotation=(%s) scale=(%s)"),
        *Planned.GetLocation().ToString(),
        *Planned.Rotator().ToString(),
        *Planned.GetScale3D().ToString());
    return true;
}

// Returns the exact native archetype/template value for reset.
bool LevelResetValue(
    const FProperty* Property,
    const UObject* Object,
    FString& OutText)
{
    const UObject* Archetype = Object != nullptr
        ? Object->GetArchetype()
        : nullptr;
    if (Property == nullptr || Archetype == nullptr)
    {
        return false;
    }
    const void* Value = Property->ContainerPtrToValuePtr<void>(Archetype);
    FString Exported;
    Property->ExportText_Direct(
        Exported,
        Value,
        Value,
        const_cast<UObject*>(Archetype),
        PPF_None);
    OutText = Exported;
    return true;
}

TSharedPtr<FJsonObject> LevelPlannedObject(
    const TArray<TSharedPtr<FLevelPlannedEdit>>& Edits,
    const TArray<TSharedPtr<FLevelPlannedLifecycle>>& Lifecycle = {})
{
    TSharedPtr<FJsonObject> Planned = MakeShared<FJsonObject>();
    TArray<TSharedPtr<FJsonValue>> Operations;
    for (const TSharedPtr<FLevelPlannedEdit>& Edit : Edits)
    {
        if (!Edit.IsValid())
        {
            continue;
        }
        TSharedPtr<FJsonObject> Operation = MakeShared<FJsonObject>();
        Operation->SetStringField(TEXT("kind"), Edit->Kind);
        Operation->SetStringField(TEXT("ref"), Edit->RefText);
        Operation->SetStringField(
            TEXT("field"),
            Edit->Property != nullptr
                ? Edit->Property->GetName()
                : FString());
        Operation->SetStringField(TEXT("before"), Edit->Before);
        Operation->SetStringField(TEXT("after"), Edit->After);
        Operations.Add(MakeShared<FJsonValueObject>(Operation));
    }
    Planned->SetArrayField(TEXT("operations"), Operations);
    if (!Lifecycle.IsEmpty())
    {
        TArray<TSharedPtr<FJsonValue>> LifecycleOps;
        for (const TSharedPtr<FLevelPlannedLifecycle>& Op : Lifecycle)
        {
            if (!Op.IsValid())
            {
                continue;
            }
            TSharedPtr<FJsonObject> Operation = MakeShared<FJsonObject>();
            Operation->SetStringField(TEXT("kind"), Op->Kind);
            if (Op->Kind == TEXT("add"))
            {
                Operation->SetStringField(TEXT("alias"), Op->Alias);
                Operation->SetStringField(TEXT("palette"), Op->PaletteId);
                Operation->SetStringField(
                    TEXT("type"),
                    Op->Entry.NativeType);
            }
            else
            {
                Operation->SetStringField(TEXT("ref"), Op->RefText);
            }
            LifecycleOps.Add(MakeShared<FJsonValueObject>(Operation));
        }
        Planned->SetArrayField(TEXT("lifecycle"), LifecycleOps);
    }
    return Planned;
}

}

// ============================================================================
// Level lifecycle (Slice 4)
//
// Palette-backed Actor and instance Component creation plus exact removal,
// inside the same top-level transaction. Creation re-enumerates the exact
// Palette destination and revalidates the opaque id; raw Class or factory
// selection is never accepted.
// ============================================================================

struct FLevelCreationBinding
{
    FString Alias;
    FString PaletteId;
    FString Kind; // "actor" | "component"
};

bool CollectLevelCreationBindings(
    const FSalPatch& Patch,
    TMap<FString, FLevelCreationBinding>& OutBindings,
    TArray<TSharedPtr<FJsonObject>>& OutDiagnostics)
{
    for (const TSharedPtr<FJsonValue>& StatementValue : Patch.Statements)
    {
        const TSharedPtr<FJsonObject>* Statement = nullptr;
        if (!StatementValue.IsValid()
            || !StatementValue->TryGetObject(Statement)
            || Statement == nullptr
            || !(*Statement).IsValid())
        {
            continue;
        }
        const bool bBinding = (*Statement)->HasField(TEXT("target"))
            && (*Statement)->HasField(TEXT("value"))
            && !(*Statement)->HasField(TEXT("kind"));
        if (!bBinding)
        {
            continue;
        }
        const TSharedPtr<FJsonObject>* Target = nullptr;
        const TSharedPtr<FJsonObject>* Value = nullptr;
        FString TargetKind;
        FString Alias;
        if (!(*Statement)->TryGetObjectField(TEXT("target"), Target)
            || Target == nullptr
            || !(*Target).IsValid()
            || !(*Target)->TryGetStringField(TEXT("kind"), TargetKind)
            || TargetKind != TEXT("local")
            || !(*Target)->TryGetStringField(TEXT("name"), Alias)
            || !(*Statement)->TryGetObjectField(TEXT("value"), Value)
            || Value == nullptr
            || !(*Value).IsValid())
        {
            continue;
        }
        FString ValueKind;
        const TSharedPtr<FJsonObject>* Args = nullptr;
        if (!(*Value)->TryGetStringField(TEXT("kind"), ValueKind)
            || ValueKind != TEXT("call")
            || !(*Value)->TryGetObjectField(TEXT("args"), Args)
            || Args == nullptr)
        {
            continue;
        }
        FString PaletteId;
        if (!(*Args)->TryGetStringField(TEXT("palette"), PaletteId)
            || PaletteId.IsEmpty())
        {
            OutDiagnostics.Add(
                FSalDiagnostics::Error(
                    TEXT("validation.creation_invalid"),
                    TEXT("Level creation binding requires one opaque Palette "
                        "identity."))
                    .Interface(TEXT("level"))
                    .Ref(Alias)
                    .Build());
            continue;
        }
        FLevelCreationBinding Binding;
        Binding.Alias = Alias;
        Binding.PaletteId = PaletteId;
        if (!FSalLevelInterface::ResolveCreationKind(
                PaletteId,
                Binding.Kind))
        {
            OutDiagnostics.Add(
                FSalDiagnostics::Error(
                    TEXT("validation.creation_invalid"),
                    TEXT("Level Palette identity does not belong to the "
                        "level Domain."))
                    .Interface(TEXT("level"))
                    .Ref(Alias)
                    .Build());
            continue;
        }
        OutBindings.Add(Alias, Binding);
    }
    return true;
}

// Re-enumerate the exact Palette destination and resolve one opaque id.
bool ResolveLevelPaletteEntryForCreate(
    const FSalResolvedTarget& Target,
    const LevelPalette::FDestination& Destination,
    const FString& PaletteId,
    LevelPalette::FEntry& OutEntry,
    FString& OutError)
{
    TArray<LevelPalette::FEntry> Entries;
    if (Destination.Kind == LevelPalette::EDestinationKind::Actor)
    {
        DiscoverActorPaletteEntries(Entries);
    }
    else
    {
        DiscoverComponentPaletteEntries(Entries);
    }
    const LevelPalette::FEntry* Match = nullptr;
    int32 MatchCount = 0;
    for (const LevelPalette::FEntry& Entry : Entries)
    {
        if (Entry.Id == PaletteId)
        {
            Match = &Entry;
            ++MatchCount;
        }
    }
    if (MatchCount != 1 || Match == nullptr)
    {
        OutError = MatchCount > 1
            ? TEXT("Palette identity is ambiguous for this destination.")
            : TEXT("Palette identity was not found for this exact "
                "destination; re-run palette entries.");
        return false;
    }
    if (!Match->bCreationAvailable)
    {
        OutError = Match->UnavailableReason.IsEmpty()
            ? TEXT("This Palette entry is not creation-available.")
            : Match->UnavailableReason;
        return false;
    }
    OutEntry = *Match;
    return true;
}

// Resolve an add destination: arena.Actors or @actorGuid.Components.
bool ResolveLevelAddDestination(
    const TSharedPtr<FJsonObject>& Statement,
    const FString& TargetAlias,
    LevelPalette::FDestination& Out,
    FString& OutError)
{
    const TSharedPtr<FJsonObject>* DestinationRef = nullptr;
    if (!Statement->TryGetObjectField(TEXT("to"), DestinationRef)
        || DestinationRef == nullptr)
    {
        OutError = TEXT("Level add requires one exact destination after to.");
        return false;
    }
    return ResolveLevelPaletteDestination(
        *DestinationRef,
        TargetAlias,
        Out,
        OutError);
}

TSharedPtr<FJsonObject> LevelPatchError(
    const FString& Code,
    const FString& Message,
    const FString& Operation,
    const FString& Ref = FString())
{
    FSalDiagnosticBuilder Diagnostic =
        FSalDiagnostics::Error(Code, Message)
            .Interface(TEXT("level"))
            .Operation(Operation);
    if (!Ref.IsEmpty())
    {
        Diagnostic.Ref(Ref);
    }
    return Diagnostic.Build();
}

bool FSalLevelInterface::ResolveCreationKind(
    const FString& PaletteId,
    FString& OutKind)
{
    OutKind.Reset();
    if (PaletteId.StartsWith(TEXT("level.actor.")))
    {
        OutKind = TEXT("actor");
        return true;
    }
    if (PaletteId.StartsWith(TEXT("level.component.")))
    {
        OutKind = TEXT("component");
        return true;
    }
    return false;
}

TSharedPtr<FJsonObject> FSalLevelInterface::Query(
    const FSalQuery& Query,
    const FSalResolvedTarget& Target)
{
    const auto Finalize = [&Target](
        const TSharedPtr<FJsonObject>& Result)
    {
        AddSourceAssetHandoff(Result, Target);
        return Result;
    };
    FString Operation;
    if (!Query.Operation.IsValid()
        || !Query.Operation->TryGetStringField(TEXT("kind"), Operation))
    {
        return QueryError(
            TEXT("capability.operation_unavailable"),
            TEXT("Level Query has no supported primary operation."),
            TEXT("query"),
            FString(),
            {TEXT("target"), TEXT("summary"), TEXT("actors"), TEXT("components"), TEXT("actor"), TEXT("component"), TEXT("palette_entries"), TEXT("palette")});
    }
    if (Operation == TEXT("target"))
    {
        return Finalize(QueryTarget(Query, Target));
    }
    if (Operation == TEXT("summary"))
    {
        return Finalize(QuerySummary(Query, Target));
    }
    if (Operation == TEXT("actors"))
    {
        return Finalize(QueryActors(Query, Target));
    }
    if (Operation == TEXT("components"))
    {
        return Finalize(QueryComponents(Query, Target));
    }
    if (Operation == TEXT("actor"))
    {
        return Finalize(QueryExactActor(Query, Target));
    }
    if (Operation == TEXT("component"))
    {
        return Finalize(QueryExactComponent(Query, Target));
    }
    if (Operation == TEXT("palette_entries"))
    {
        return Finalize(QueryLevelPaletteEntries(Query, Target));
    }
    if (Operation == TEXT("palette"))
    {
        return Finalize(QueryLevelPalette(Query, Target));
    }
    return QueryError(
        TEXT("capability.operation_unavailable"),
        FString::Printf(
            TEXT("Level Query operation is not active in this read-only slice: %s."),
            *Operation),
        Operation,
        FString(),
        {TEXT("target"), TEXT("summary"), TEXT("actors"), TEXT("components"), TEXT("actor"), TEXT("component"), TEXT("palette_entries"), TEXT("palette")});
}

TSharedPtr<FJsonObject> FSalLevelInterface::Patch(
    const FSalPatch& Patch,
    const FSalResolvedTarget& Target)
{
    // The mutation envelope's object must be a valid ObjectText even when
    // nothing is returned; an empty statements document is the neutral form.
    const TSharedPtr<FJsonObject> NoObjects =
        FSalObjectBuilder().BuildObject();

    if (Target.Domain != ESalDomain::Level)
    {
        return MakeMutationResult(
            NoObjects,
            {FSalDiagnostics::Error(
                    TEXT("validation.exact_level_required"),
                    TEXT("Level Patch requires the canonical exact Level "
                        "Target."))
                .Interface(TEXT("level"))
                .Build()},
            Patch.bDryRun,
            false,
            false,
            Target.AssetPath,
            TEXT("level"));
    }

    UWorld* World = nullptr;
    ULevel* Level = nullptr;
    FString SourceReason;
    if (!IsExactLoadedSource(Target, World, Level, SourceReason))
    {
        return MakeMutationResult(
            NoObjects,
            {FSalDiagnostics::Error(
                    TEXT("capability.level_not_loaded"),
                    SourceReason.IsEmpty()
                        ? TEXT("Level Patch requires the exact authored "
                            "Editor source World to be loaded.")
                        : SourceReason)
                .Interface(TEXT("level"))
                .Build()},
            Patch.bDryRun,
            false,
            false,
            Target.AssetPath,
            TEXT("level"));
    }

    FLevelSnapshot Snapshot;
    FString SnapshotReason;
    if (!BuildSnapshot(Target, TEXT("patch"), Snapshot, SnapshotReason))
    {
        return MakeMutationResult(
            NoObjects,
            {FSalDiagnostics::Error(
                    TEXT("capability.level_not_loaded"),
                    SnapshotReason.IsEmpty()
                        ? TEXT("Level Patch identity environment is "
                            "unavailable.")
                        : SnapshotReason)
                .Interface(TEXT("level"))
                .Build()},
            Patch.bDryRun,
            false,
            false,
            Target.AssetPath,
            TEXT("level"));
    }
    if (!Snapshot.bIdentityComplete)
    {
        return MakeMutationResult(
            NoObjects,
            {FSalDiagnostics::Error(
                    TEXT("validation.reference_scan_incomplete"),
                    TEXT("The Level Actor identity environment is "
                        "incomplete; Level Patch is fail-closed."))
                .Interface(TEXT("level"))
                .Build()},
            Patch.bDryRun,
            false,
            false,
            Target.AssetPath,
            TEXT("level"));
    }

    TArray<TSharedPtr<FJsonObject>> Diagnostics;
    TArray<TSharedPtr<FLevelPlannedEdit>> Edits;
    TArray<TSharedPtr<FLevelPlannedLifecycle>> Lifecycle;
    TMap<FString, FLevelCreationBinding> Bindings;
    CollectLevelCreationBindings(Patch, Bindings, Diagnostics);
    for (const TSharedPtr<FJsonValue>& StatementValue : Patch.Statements)
    {
        const TSharedPtr<FJsonObject>* Statement = nullptr;
        if (!StatementValue.IsValid()
            || !StatementValue->TryGetObject(Statement)
            || Statement == nullptr
            || !(*Statement).IsValid())
        {
            Diagnostics.Add(
                FSalDiagnostics::Error(
                    TEXT("validation.statement_invalid"),
                    TEXT("Level Patch statement is malformed."))
                    .Interface(TEXT("level"))
                    .Build());
            continue;
        }
        FString Kind;
        (*Statement)->TryGetStringField(TEXT("kind"), Kind);
        if (Kind == TEXT("add"))
        {
            const TSharedPtr<FJsonObject>* TargetRef = nullptr;
            FString Alias;
            FString TargetKind;
            if (!(*Statement)->TryGetObjectField(TEXT("target"), TargetRef)
                || TargetRef == nullptr
                || !(*TargetRef).IsValid()
                || !(*TargetRef)->TryGetStringField(TEXT("kind"), TargetKind)
                || TargetKind != TEXT("local")
                || !(*TargetRef)->TryGetStringField(TEXT("name"), Alias))
            {
                Diagnostics.Add(
                    LevelPatchError(
                        TEXT("validation.creation_invalid"),
                        TEXT("Level add requires one declared creation "
                            "binding alias."),
                        TEXT("add")));
                continue;
            }
            const FLevelCreationBinding* Binding = Bindings.Find(Alias);
            if (Binding == nullptr)
            {
                Diagnostics.Add(
                    LevelPatchError(
                        TEXT("resolution.binding_not_found"),
                        TEXT("Level add references no declared Palette "
                            "creation binding."),
                        TEXT("add"),
                        Alias));
                continue;
            }
            if (Binding->Kind != TEXT("actor"))
            {
                Diagnostics.Add(
                    LevelPatchError(
                        TEXT("capability.operation_unavailable"),
                        TEXT("Level Component creation Patch is not yet "
                            "active in this capability."),
                        TEXT("add"),
                        Alias));
                continue;
            }
            LevelPalette::FDestination Destination;
            FString Error;
            if (!ResolveLevelAddDestination(
                    *Statement,
                    Patch.Alias,
                    Destination,
                    Error))
            {
                Diagnostics.Add(
                    LevelPatchError(
                        TEXT("validation.palette_context_invalid"),
                        Error,
                        TEXT("add"),
                        Alias));
                continue;
            }
            if (Destination.Kind != LevelPalette::EDestinationKind::Actor)
            {
                Diagnostics.Add(
                    LevelPatchError(
                        TEXT("validation.palette_context_invalid"),
                        TEXT("Actor creation requires the Level Actor "
                            "destination arena.Actors."),
                        TEXT("add"),
                        Alias));
                continue;
            }
            LevelPalette::FEntry Entry;
            if (!ResolveLevelPaletteEntryForCreate(
                    Target,
                    Destination,
                    Binding->PaletteId,
                    Entry,
                    Error))
            {
                Diagnostics.Add(
                    LevelPatchError(
                        TEXT("resolution.palette_not_found"),
                        Error,
                        TEXT("add"),
                        Alias));
                continue;
            }
            TSharedPtr<FLevelPlannedLifecycle> Plan =
                MakeShared<FLevelPlannedLifecycle>();
            Plan->Kind = TEXT("add");
            Plan->Alias = Alias;
            Plan->PaletteId = Binding->PaletteId;
            Plan->Entry = Entry;
            Lifecycle.Add(Plan);
            continue;
        }
        if (Kind == TEXT("remove"))
        {
            const TSharedPtr<FJsonObject>* TargetRef = nullptr;
            FString TargetKind;
            if (!(*Statement)->TryGetObjectField(TEXT("target"), TargetRef)
                || TargetRef == nullptr
                || !(*TargetRef).IsValid()
                || !(*TargetRef)->TryGetStringField(TEXT("kind"), TargetKind)
                || TargetKind != TEXT("actor"))
            {
                Diagnostics.Add(
                    LevelPatchError(
                        TEXT("validation.edit_target_invalid"),
                        TEXT("Level remove requires one exact persisted "
                            "Actor."),
                        TEXT("remove")));
                continue;
            }
            FString Id;
            FGuid Guid;
            if (!(*TargetRef)->TryGetStringField(TEXT("id"), Id)
                || !ParseActorGuid(Id, Guid))
            {
                Diagnostics.Add(
                    LevelPatchError(
                        TEXT("validation.edit_target_invalid"),
                        TEXT("Level remove Actor identity is invalid."),
                        TEXT("remove"),
                        Id));
                continue;
            }
            FLevelActorEntry* Match = nullptr;
            int32 MatchCount = 0;
            for (FLevelActorEntry& Entry : Snapshot.Actors)
            {
                if (Entry.Guid == Guid)
                {
                    Match = &Entry;
                    ++MatchCount;
                }
            }
            if (MatchCount != 1 || Match == nullptr || !Match->bLoaded)
            {
                Diagnostics.Add(
                    LevelPatchError(
                        TEXT("resolution.object_not_found"),
                        MatchCount > 1
                            ? TEXT("Multiple persisted Level Actor records "
                                "share this ActorGuid; the removal is "
                                "ambiguous.")
                            : TEXT("The exact persisted Actor is not loaded "
                                "or not present; Level Patch will not load "
                                "an unloaded descriptor."),
                        TEXT("remove"),
                        Id));
                continue;
            }
            AActor* Actor = Match->Actor.Get();
            if (!IsValid(Actor))
            {
                Diagnostics.Add(
                    LevelPatchError(
                        TEXT("validation.object_invalidated"),
                        TEXT("The exact persisted Actor UObject is "
                            "invalid."),
                        TEXT("remove"),
                        Id));
                continue;
            }
            if (Actor->IsA(AWorldSettings::StaticClass())
                || Actor->IsA(ALevelScriptActor::StaticClass()))
            {
                Diagnostics.Add(
                    LevelPatchError(
                        TEXT("validation.required_actor_protected"),
                        TEXT("Removal of a required Level Actor such as "
                            "WorldSettings or LevelScriptActor is not "
                            "allowed."),
                        TEXT("remove"),
                        Id));
                continue;
            }
            if (HasPCGManagedComponent(Actor))
            {
                Diagnostics.Add(
                    LevelPatchError(
                        TEXT("capability.pcg_async_unproven"),
                        TEXT("Removal of an Actor with a managed PCG "
                            "Component could leave generated state "
                            "unattributed; async suppression is unproven."),
                        TEXT("remove"),
                        Id));
                continue;
            }
            TSharedPtr<FLevelPlannedLifecycle> Plan =
                MakeShared<FLevelPlannedLifecycle>();
            Plan->Kind = TEXT("remove");
            Plan->RefText = Id;
            Plan->Actor = Actor;
            Lifecycle.Add(Plan);
            continue;
        }
        if (Kind == TEXT("invoke"))
        {
            FString Operation;
            (*Statement)->TryGetStringField(TEXT("operation"), Operation);
            if (Operation != TEXT("SetActorTransform"))
            {
                Diagnostics.Add(
                    FSalDiagnostics::Error(
                        TEXT("capability.operation_unavailable"),
                        FString::Printf(
                            TEXT("Level Patch does not yet support the %s "
                                "invoke operation in this capability."),
                            *Operation))
                        .Interface(TEXT("level"))
                        .Operation(Kind)
                        .Build());
                continue;
            }
            AActor* Actor = nullptr;
            FString ActorIdentity;
            FString Error;
            if (!ResolveLevelPatchActor(
                    *Statement,
                    Snapshot,
                    Actor,
                    ActorIdentity,
                    Error))
            {
                Diagnostics.Add(
                    FSalDiagnostics::Error(
                        TEXT("validation.edit_target_invalid"),
                        Error)
                        .Interface(TEXT("level"))
                        .Operation(Kind)
                        .Build());
                continue;
            }
            TSharedPtr<FLevelPlannedEdit> Edit =
                MakeShared<FLevelPlannedEdit>();
            if (!LevelBuildTransformEdit(
                    *Statement,
                    Actor,
                    ActorIdentity,
                    *Edit,
                    Error))
            {
                Diagnostics.Add(
                    FSalDiagnostics::Error(
                        TEXT("validation.operation_arguments_invalid"),
                        Error)
                        .Interface(TEXT("level"))
                        .Operation(Kind)
                        .Ref(ActorIdentity)
                        .Build());
                continue;
            }
            Edits.Add(Edit);
            continue;
        }
        if (Kind != TEXT("set") && Kind != TEXT("reset"))
        {
            Diagnostics.Add(
                FSalDiagnostics::Error(
                    TEXT("capability.operation_unavailable"),
                    FString::Printf(
                        TEXT("Level Patch does not yet support the %s "
                            "statement in this capability; supported "
                            "statements are set, reset, and transform "
                            "invoke."),
                        *Kind))
                    .Interface(TEXT("level"))
                    .Operation(Kind)
                    .Build());
            continue;
        }
        FString OwnerIdentity;
        UObject* Object = nullptr;
        FProperty* Property = nullptr;
        FString FieldName;
        FString Error;
        if (!ResolveLevelPatchMember(
                *Statement,
                Target,
                Snapshot,
                OwnerIdentity,
                Object,
                Property,
                FieldName,
                Error))
        {
            Diagnostics.Add(
                FSalDiagnostics::Error(
                    TEXT("validation.edit_target_invalid"),
                    Error)
                    .Interface(TEXT("level"))
                    .Operation(Kind)
                    .Build());
            continue;
        }
        TSharedPtr<FLevelPlannedEdit> Edit =
            MakeShared<FLevelPlannedEdit>();
        Edit->Kind = Kind;
        Edit->RefText = OwnerIdentity + TEXT(".") + FieldName;
        Edit->Object = Object;
        Edit->Property = Property;
        Edit->Before = LevelExportScalarValue(Property, Object);
        if (Kind == TEXT("set"))
        {
            const TSharedPtr<FJsonValue> Value =
                (*Statement)->TryGetField(TEXT("value"));
            FString Text;
            if (!Value.IsValid()
                || !LevelValueImportText(Value, Text)
                || !LevelValidateScalarImport(
                    Property,
                    Text,
                    Error))
            {
                Diagnostics.Add(
                    FSalDiagnostics::Error(
                        TEXT("validation.value_invalid"),
                        Error.IsEmpty()
                            ? TEXT("Level set value must be a scalar "
                                "string, number, or Boolean.")
                            : Error)
                        .Interface(TEXT("level"))
                        .Operation(Kind)
                        .Ref(Edit->RefText)
                        .Build());
                continue;
            }
            Edit->After = Text;
        }
        else
        {
            FString ResetText;
            if (!LevelResetValue(Property, Object, ResetText))
            {
                Diagnostics.Add(
                    FSalDiagnostics::Error(
                        TEXT("validation.reset_source_unavailable"),
                        TEXT("Level reset could not resolve one exact "
                            "native archetype or template value."))
                        .Interface(TEXT("level"))
                        .Operation(Kind)
                        .Ref(Edit->RefText)
                        .Build());
                continue;
            }
            Edit->After = ResetText;
        }
        Edits.Add(Edit);
    }

    const bool bHasErrors = !Diagnostics.IsEmpty();
    if (bHasErrors)
    {
        for (const TSharedPtr<FJsonObject>& Diagnostic : Diagnostics)
        {
            FString Code;
            if (Diagnostic.IsValid())
            {
                Diagnostic->TryGetStringField(TEXT("code"), Code);
            }
            UE_LOG(
                LogTemp,
                Warning,
                TEXT("Loomle Level Patch diagnostic: %s (valid=%d)"),
                *Code,
                Diagnostic.IsValid() ? 1 : 0);
        }
        return MakeMutationResult(
            NoObjects,
            Diagnostics,
            Patch.bDryRun,
            false,
            false,
            Target.AssetPath,
            TEXT("level"),
            LevelPlannedObject(Edits, Lifecycle));
    }
    if (Edits.IsEmpty() && Lifecycle.IsEmpty())
    {
        return MakeMutationResult(
            NoObjects,
            {FSalDiagnostics::Error(
                    TEXT("validation.no_operations"),
                    TEXT("Level Patch contained no supported set, reset, "
                        "transform, add, or remove statements."))
                .Interface(TEXT("level"))
                .Build()},
            Patch.bDryRun,
            false,
            false,
            Target.AssetPath,
            TEXT("level"));
    }

    if (Patch.bDryRun)
    {
        return MakeMutationResult(
            NoObjects,
            {},
            true,
            true,
            false,
            Target.AssetPath,
            TEXT("level"),
            LevelPlannedObject(Edits, Lifecycle));
    }

    FScopedTransaction Transaction(
        FText::FromString(TEXT("SAL Edit Level")));
    if (!Transaction.IsOutstanding())
    {
        return MakeMutationResult(
            NoObjects,
            {FSalDiagnostics::Error(
                    TEXT("capability.transaction_unavailable"),
                    TEXT("UE did not open the required Level Patch "
                        "transaction."))
                .Interface(TEXT("level"))
                .Build()},
            false,
            false,
            false,
            Target.AssetPath,
            TEXT("level"),
            LevelPlannedObject(Edits, Lifecycle));
    }
    for (const TSharedPtr<FLevelPlannedEdit>& Edit : Edits)
    {
        UObject* Object = Edit->Object.Get();
        if (!IsValid(Object))
        {
            Transaction.Cancel();
            return MakeMutationResult(
                NoObjects,
                {FSalDiagnostics::Error(
                        TEXT("validation.object_invalidated"),
                        TEXT("A Level Patch edit target was invalidated "
                            "before apply; the transaction was rolled "
                            "back."))
                    .Interface(TEXT("level"))
                    .Ref(Edit->RefText)
                    .Build()},
                false,
                false,
                false,
                Target.AssetPath,
                TEXT("level"),
                LevelPlannedObject(Edits, Lifecycle));
        }
        Object->Modify();
        if (Edit->Kind == TEXT("transform"))
        {
            AActor* Actor = Cast<AActor>(Object);
            if (Actor == nullptr)
            {
                Transaction.Cancel();
                return MakeMutationResult(
                    NoObjects,
                    {FSalDiagnostics::Error(
                            TEXT("validation.object_invalidated"),
                            TEXT("Level transform target is not an "
                                "Actor."))
                        .Interface(TEXT("level"))
                        .Ref(Edit->RefText)
                        .Build()},
                    false,
                    false,
                    false,
                    Target.AssetPath,
                    TEXT("level"),
                    LevelPlannedObject(Edits, Lifecycle));
            }
            const FTransform Planned(
                FRotator(
                    Edit->TransformRotation.GetValue().X,
                    Edit->TransformRotation.GetValue().Y,
                    Edit->TransformRotation.GetValue().Z),
                Edit->TransformLocation.GetValue(),
                Edit->TransformScale.GetValue());
            Actor->SetActorTransform(Planned);
            continue;
        }
        FString Error;
        if (!LevelImportScalarValue(
                Edit->Property,
                Object,
                Edit->After,
                Error))
        {
            Transaction.Cancel();
            return MakeMutationResult(
                NoObjects,
                {FSalDiagnostics::Error(
                        TEXT("validation.apply_failed"),
                        Error)
                    .Interface(TEXT("level"))
                    .Ref(Edit->RefText)
                    .Build()},
                false,
                false,
                false,
                Target.AssetPath,
                TEXT("level"),
                LevelPlannedObject(Edits, Lifecycle));
        }
        FPropertyChangedEvent ChangedEvent(Edit->Property);
        Object->PostEditChangeProperty(ChangedEvent);
    }
    for (const TSharedPtr<FLevelPlannedLifecycle>& Op : Lifecycle)
    {
        if (Op->Kind == TEXT("add"))
        {
            UWorld* SourceWorld = World;
            ULevel* SourceLevel = SourceWorld != nullptr
                ? SourceWorld->PersistentLevel
                : nullptr;
            if (SourceWorld == nullptr || SourceLevel == nullptr)
            {
                Transaction.Cancel();
                return MakeMutationResult(
                    NoObjects,
                    {FSalDiagnostics::Error(
                            TEXT("capability.level_not_loaded"),
                            TEXT("Level Actor creation requires the exact "
                                "authored source Level."))
                        .Interface(TEXT("level"))
                        .Operation(TEXT("add"))
                        .Build()},
                    false,
                    false,
                    false,
                    Target.AssetPath,
                    TEXT("level"),
                    LevelPlannedObject(Edits, Lifecycle));
            }
            if (Op->Entry.ActorClass == nullptr)
            {
                Transaction.Cancel();
                return MakeMutationResult(
                    NoObjects,
                    {FSalDiagnostics::Error(
                            TEXT("validation.creation_invalid"),
                            TEXT("Level Palette entry carries no exact "
                                "Actor Class for creation."))
                        .Interface(TEXT("level"))
                        .Operation(TEXT("add"))
                        .Ref(Op->PaletteId)
                        .Build()},
                    false,
                    false,
                    false,
                    Target.AssetPath,
                    TEXT("level"),
                    LevelPlannedObject(Edits, Lifecycle));
            }
            FActorSpawnParameters SpawnParams;
            SpawnParams.OverrideLevel = SourceLevel;
            SpawnParams.ObjectFlags = RF_Transactional;
            AActor* NewActor = SourceWorld->SpawnActor(
                Op->Entry.ActorClass,
                &FTransform::Identity,
                SpawnParams);
            if (!IsValid(NewActor))
            {
                Transaction.Cancel();
                return MakeMutationResult(
                    NoObjects,
                    {FSalDiagnostics::Error(
                            TEXT("validation.apply_failed"),
                            TEXT("UE failed to spawn the requested "
                                "Actor."))
                        .Interface(TEXT("level"))
                        .Operation(TEXT("add"))
                        .Ref(Op->PaletteId)
                        .Build()},
                    false,
                    false,
                    false,
                    Target.AssetPath,
                    TEXT("level"),
                    LevelPlannedObject(Edits, Lifecycle));
            }
            const FGuid NewGuid = NewActor->GetActorGuid();
            Op->CreatedId = NewGuid.IsValid()
                ? NewGuid.ToString(EGuidFormats::DigitsWithHyphensLower)
                : FString();
            Op->CreatedType = NewActor->GetClass()->GetPathName();
            Op->Actor = NewActor;
            continue;
        }
        if (Op->Kind == TEXT("remove"))
        {
            AActor* Actor = Op->Actor.Get();
            if (!IsValid(Actor))
            {
                Transaction.Cancel();
                return MakeMutationResult(
                    NoObjects,
                    {FSalDiagnostics::Error(
                            TEXT("validation.object_invalidated"),
                            TEXT("Level remove target was invalidated before "
                                "apply; the transaction was rolled back."))
                        .Interface(TEXT("level"))
                        .Operation(TEXT("remove"))
                        .Ref(Op->RefText)
                        .Build()},
                    false,
                    false,
                    false,
                    Target.AssetPath,
                    TEXT("level"),
                    LevelPlannedObject(Edits, Lifecycle));
            }
            Actor->Modify();
            Actor->Destroy();
            if (!Actor->IsActorBeingDestroyed())
            {
                Transaction.Cancel();
                return MakeMutationResult(
                    NoObjects,
                    {FSalDiagnostics::Error(
                            TEXT("validation.apply_failed"),
                            TEXT("UE did not destroy the requested Actor."))
                        .Interface(TEXT("level"))
                        .Operation(TEXT("remove"))
                        .Ref(Op->RefText)
                        .Build()},
                    false,
                    false,
                    false,
                    Target.AssetPath,
                    TEXT("level"),
                    LevelPlannedObject(Edits, Lifecycle));
            }
            continue;
        }
    }

    for (const TSharedPtr<FLevelPlannedEdit>& Edit : Edits)
    {
        UObject* Object = Edit->Object.Get();
        if (Edit->Kind == TEXT("transform"))
        {
            AActor* Actor = Cast<AActor>(Object);
            if (IsValid(Actor))
            {
                const FTransform Readback = Actor->GetActorTransform();
                Edit->Before = FString::Printf(
                    TEXT("location=(%s) rotation=(%s) scale=(%s)"),
                    *Readback.GetLocation().ToString(),
                    *Readback.Rotator().ToString(),
                    *Readback.GetScale3D().ToString());
            }
            continue;
        }
        if (IsValid(Object) && Edit->Property != nullptr)
        {
            Edit->Before = LevelExportScalarValue(
                Edit->Property,
                Object);
        }
    }
    FSalObjectBuilder ResultBuilder;
    FLevelPaletteUniqueNameAllocator ResultAliases;
    for (const TSharedPtr<FLevelPlannedLifecycle>& Op : Lifecycle)
    {
        if (Op->Kind == TEXT("add") && !Op->CreatedId.IsEmpty())
        {
            TSharedPtr<FJsonObject> Args = MakeShared<FJsonObject>();
            Args->SetStringField(TEXT("id"), Op->CreatedId);
            Args->SetStringField(TEXT("type"), Op->CreatedType);
            ResultBuilder.AddLocalBinding(
                ResultAliases.Allocate(Op->Alias, TEXT("actor")),
                Value::Call(TEXT("actor"), Args));
        }
        else if (Op->Kind == TEXT("remove"))
        {
            ResultBuilder.AddComment(
                FString::Printf(
                    TEXT("removed actor @%s"),
                    *Op->RefText));
        }
    }
    return MakeMutationResult(
        ResultBuilder.BuildObject(),
        {},
        false,
        true,
        true,
        Target.AssetPath,
        TEXT("level"),
        LevelPlannedObject(Edits, Lifecycle));
}

bool FSalLevelInterface::ResolveEditorContextTarget(
    UWorld* EditorWorld,
    const ULevel* SourceLevel,
    FSalResolvedTarget& OutTarget,
    FString& OutCode,
    FString& OutMessage,
    FString& OutSuggestion)
{
    OutTarget = FSalResolvedTarget();
    OutCode = TEXT("resolution.unresolved_target");
    OutMessage = TEXT(
        "The Level Editor map has no registered persistent Level Target.");
    OutSuggestion = TEXT(
        "Save the current map, then retry editor with no arguments.");

    if (!IsValid(EditorWorld))
    {
        OutCode = TEXT("context.owner_invalid");
        OutMessage = TEXT("The Editor World is unavailable.");
        OutSuggestion.Reset();
        return false;
    }

    const FString EditorPackageName =
        EditorWorld->GetOutermost()->GetName();
    if (EditorWorld->HasAnyFlags(RF_Transient)
        || EditorWorld->GetOutermost() == GetTransientPackage()
        || EditorWorld->GetOutermost()->HasAnyFlags(RF_Transient)
        || EditorWorld->GetOutermost()->HasAnyPackageFlags(PKG_PlayInEditor)
        || !FPackageName::IsValidLongPackageName(EditorPackageName)
        || FPackageName::IsTempPackage(EditorPackageName))
    {
        return false;
    }
    if (EditorWorld->WorldType != EWorldType::Editor
        || EditorWorld->HasAnyFlags(IncompleteLoadFlags)
        || GEditor == nullptr
        || GEditor->GetEditorWorldContext().World() != EditorWorld)
    {
        OutCode = TEXT("context.owner_invalid");
        OutMessage = TEXT(
            "Editor Context accepts only the active authored Editor World, never PIE, preview, game, or a replaced World.");
        OutSuggestion.Reset();
        return false;
    }

    const ULevel* EffectiveLevel = SourceLevel != nullptr
        ? SourceLevel
        : EditorWorld->PersistentLevel;
    if (!IsValid(EffectiveLevel)
        || EffectiveLevel->HasAnyFlags(IncompleteLoadFlags)
        || EffectiveLevel->GetWorld() != EditorWorld)
    {
        OutCode = TEXT("context.owner_invalid");
        OutMessage = TEXT(
            "The observed Level is unavailable or is not composed into the active Editor World.");
        OutSuggestion.Reset();
        return false;
    }

    FSoftObjectPath SourcePath;
    if (ULevelInstanceSubsystem* LevelInstances =
            EditorWorld->GetSubsystem<ULevelInstanceSubsystem>())
    {
        if (ILevelInstanceInterface* LevelInstance =
                LevelInstances->GetOwningLevelInstance(EffectiveLevel))
        {
            SourcePath =
                LevelInstance->GetWorldAsset().ToSoftObjectPath();
        }
    }
    if (!SourcePath.IsValid())
    {
        UWorld* SourceWorld =
            EffectiveLevel->GetTypedOuter<UWorld>();
        if (!IsValid(SourceWorld)
            || SourceWorld->PersistentLevel != EffectiveLevel)
        {
            OutCode = TEXT("context.native_inconsistent");
            OutMessage = TEXT(
                "The observed Level has no exact package-owned source World.");
            OutSuggestion.Reset();
            return false;
        }
        SourcePath = FSoftObjectPath(SourceWorld);
    }
    if (!SourcePath.IsValid()
        || !SourcePath.GetSubPathString().IsEmpty())
    {
        OutMessage = TEXT(
            "The observed Level source does not name one exact saved top-level map asset.");
        return false;
    }

    TSharedPtr<FJsonObject> TargetValue =
        MakeShared<FJsonObject>();
    TargetValue->SetStringField(TEXT("kind"), TEXT("target"));
    TargetValue->SetStringField(TEXT("domain"), TEXT("level"));
    TargetValue->SetStringField(
        TEXT("asset"),
        SourcePath.ToString());
    TargetValue->SetStringField(
        TEXT("type"),
        UWorld::StaticClass()->GetPathName());
    TSharedPtr<FJsonObject> ResolutionError;
    if (!FSalTargetResolver().Resolve(
            TEXT("level_context"),
            TargetValue,
            false,
            OutTarget,
            ResolutionError))
    {
        OutTarget = FSalResolvedTarget();
        OutMessage = TEXT(
            "The observed Level source has no registered persistent Level Target.");
        return false;
    }

    UWorld* ExactWorld = nullptr;
    ULevel* ExactLevel = nullptr;
    FString Reason;
    if (!IsExactLoadedSource(
            OutTarget,
            ExactWorld,
            ExactLevel,
            Reason)
        || ExactLevel != EffectiveLevel)
    {
        OutTarget = FSalResolvedTarget();
        OutCode = TEXT("context.native_inconsistent");
        OutMessage = Reason.IsEmpty()
            ? TEXT(
                "The canonical source Level does not match the Level observed by Editor Context.")
            : Reason;
        OutSuggestion.Reset();
        return false;
    }

    OutCode.Reset();
    OutMessage.Reset();
    OutSuggestion.Reset();
    return true;
}

bool FSalLevelInterface::ResolveEditorContextActor(
    const FSalResolvedTarget& Target,
    const AActor* Actor,
    FString& OutActorId,
    FString& OutCode,
    FString& OutMessage)
{
    OutActorId.Reset();
    OutCode = TEXT("context.identity_missing");
    OutMessage = TEXT(
        "The selected Actor has no persistent identity in the exact source Level.");
    if (!IsValid(Actor))
    {
        return false;
    }

    FLevelSnapshot Snapshot;
    FString Reason;
    if (!BuildSnapshot(
            Target,
            TEXT("editor_context"),
            Snapshot,
            Reason))
    {
        OutCode = TEXT("context.native_inconsistent");
        OutMessage = Reason.IsEmpty()
            ? TEXT(
                "The selected Actor's exact source Level is no longer available.")
            : Reason;
        return false;
    }
    if (!Snapshot.bIdentityComplete)
    {
        OutCode = TEXT("context.identity_ambiguous");
        OutMessage = TEXT(
            "The complete root Actor/ActorDesc identity audit did not finish; selected Actor projection is fail-closed.");
        return false;
    }

    const FLevelActorEntry* Match = nullptr;
    int32 PointerMatches = 0;
    for (const FLevelActorEntry& Entry : Snapshot.Actors)
    {
        if (Entry.Actor.Get() == Actor)
        {
            Match = &Entry;
            ++PointerMatches;
        }
    }
    if (PointerMatches != 1
        || Match == nullptr
        || !Match->bLoaded
        || Actor->GetLevel() != Snapshot.Level)
    {
        OutCode = TEXT("context.native_inconsistent");
        OutMessage = TEXT(
            "The selected Actor is not the unique loaded authored source Actor in the canonical Level Target.");
        return false;
    }
    if (!Match->Guid.IsValid()
        || Actor->GetActorGuid() != Match->Guid)
    {
        return false;
    }
    if (Match->IdentityMultiplicity != 1)
    {
        OutCode = TEXT("context.identity_duplicate");
        OutMessage = TEXT(
            "The selected ActorGuid is duplicated in the exact Level identity environment.");
        return false;
    }

    OutActorId = GuidText(Match->Guid);
    OutCode.Reset();
    OutMessage.Reset();
    return true;
}

bool FSalLevelInterface::ResolveExactComponent(
    const FSalResolvedTarget& Target,
    const FString& ActorId,
    const FString& Source,
    const FString& Id,
    UActorComponent*& OutComponent,
    FString& OutCanonicalActorId,
    FString& OutCanonicalSource,
    FString& OutCanonicalId,
    FString& OutName,
    FString& OutType,
    FString& OutCreationMethod,
    FString& OutDeclaringClass,
    FString& OutCode,
    FString& OutMessage)
{
    OutComponent = nullptr;
    OutCanonicalActorId.Reset();
    OutCanonicalSource.Reset();
    OutCanonicalId.Reset();
    OutName.Reset();
    OutType.Reset();
    OutCreationMethod.Reset();
    OutDeclaringClass.Reset();
    OutCode = TEXT("resolution.object_not_found");
    OutMessage = TEXT("No persistent Component matches this exact source-qualified identity.");

    FGuid ActorGuid;
    if (!ParseActorGuid(ActorId, ActorGuid)
        || (Source != TEXT("native")
            && Source != TEXT("scs")
            && Source != TEXT("instance"))
        || Id.IsEmpty())
    {
        OutCode = TEXT("validation.invalid_reference");
        OutMessage = TEXT("Component identity requires a canonical ActorGuid, source native/scs/instance, and a non-empty source-qualified slot id.");
        return false;
    }

    FLevelSnapshot LevelSnapshot;
    FString Reason;
    if (!BuildSnapshot(
            Target,
            TEXT("pcg_component_target"),
            LevelSnapshot,
            Reason))
    {
        OutCode = TEXT("capability.level_not_loaded");
        OutMessage = Reason.IsEmpty()
            ? TEXT("Component Target resolution requires an exact loaded authored Editor source World.")
            : Reason;
        return false;
    }
    if (!LevelSnapshot.bIdentityComplete)
    {
        OutCode = TEXT("validation.reference_scan_incomplete");
        OutMessage = TEXT("The Level Actor identity environment is incomplete; Component Target resolution is fail-closed.");
        return false;
    }

    const FLevelActorEntry* Owner = nullptr;
    int32 OwnerMatches = 0;
    for (const FLevelActorEntry& Entry : LevelSnapshot.Actors)
    {
        if (Entry.Guid == ActorGuid)
        {
            Owner = &Entry;
            ++OwnerMatches;
        }
    }
    if (OwnerMatches != 1 || Owner == nullptr)
    {
        OutCode = OwnerMatches > 1
            ? TEXT("resolution.identity_conflict")
            : TEXT("resolution.object_not_found");
        OutMessage = OwnerMatches > 1
            ? TEXT("Multiple persisted Level Actor records share the Component owner ActorGuid.")
            : TEXT("The Component owner ActorGuid was not found in the exact Level Target.");
        return false;
    }

    AActor* OwnerActor = Owner->Actor.Get();
    if (!Owner->bLoaded || !IsValid(OwnerActor))
    {
        OutCode = TEXT("capability.component_owner_not_loaded");
        OutMessage = TEXT("Component Target resolution will not load or pin an unloaded Actor descriptor.");
        return false;
    }

    FSalLevelComponentSnapshot ComponentSnapshot;
    if (!BuildLevelComponentSnapshot(
            TArray<AActor*>{OwnerActor},
            TEXT("pcg_component_target"),
            ComponentSnapshot,
            Reason)
        || !ComponentSnapshot.bIdentityComplete)
    {
        OutCode = TEXT("validation.reference_scan_incomplete");
        OutMessage = Reason.IsEmpty()
            ? TEXT("The Component source identity scan is incomplete; Target resolution is fail-closed.")
            : Reason;
        return false;
    }

    const FSalLevelComponentEntry* Match = nullptr;
    int32 MatchCount = 0;
    for (const FSalLevelComponentEntry& Entry : ComponentSnapshot.Entries)
    {
        if (Entry.ActorGuid == ActorGuid
            && Entry.Source == Source
            && Entry.Id == Id)
        {
            Match = &Entry;
            ++MatchCount;
        }
    }
    if (MatchCount != 1 || Match == nullptr)
    {
        OutCode = MatchCount > 1
            ? TEXT("resolution.identity_conflict")
            : TEXT("resolution.object_not_found");
        OutMessage = MatchCount > 1
            ? TEXT("Multiple persistent Components share this source-qualified slot identity.")
            : TEXT("No persistent Component matches this exact source-qualified identity.");
        return false;
    }

    UActorComponent* Component = Match->Component.Get();
    if (!IsValid(Component)
        || Component->GetOwner() != OwnerActor
        || Component->GetOuter() != OwnerActor)
    {
        OutCode = TEXT("validation.reference_scan_incomplete");
        OutMessage = TEXT("The resolved Component changed ownership or lifetime during the bounded identity audit.");
        return false;
    }

    OutComponent = Component;
    OutCanonicalActorId = GuidText(Match->ActorGuid);
    OutCanonicalSource = Match->Source;
    OutCanonicalId = Match->Id;
    OutName = Match->Name;
    OutType = Match->Type;
    OutCreationMethod = Match->CreationMethod;
    OutDeclaringClass = Match->DeclaringClass;
    OutCode.Reset();
    OutMessage.Reset();
    return true;
}

bool FSalLevelInterface::LowerStableReference(
    const FSalResolvedTarget& Target,
    const TArray<FString>& IdentityPath,
    const TSharedPtr<FJsonObject>& Ref,
    FString& OutCode,
    FString& OutMessage)
{
    OutCode = TEXT("resolution.object_not_found");
    OutMessage = TEXT("StableRef was not found in the exact Level Target.");
    if (!Ref.IsValid()
        || (IdentityPath.Num() != 1 && IdentityPath.Num() != 3)
        || IdentityPath.ContainsByPredicate([](const FString& Segment)
        {
            return Segment.IsEmpty();
        }))
    {
        OutCode = TEXT("validation.invalid_reference");
        OutMessage = TEXT("Level StableRef identity must be one ActorGuid segment or ActorGuid/source/component-id.");
        return false;
    }
    FGuid Guid;
    if (!ParseActorGuid(IdentityPath[0], Guid))
    {
        OutCode = TEXT("validation.invalid_reference");
        OutMessage = TEXT("Level StableRef identity must begin with one canonical lowercase hyphenated ActorGuid.");
        return false;
    }
    if (IdentityPath.Num() == 3
        && IdentityPath[1] != TEXT("native")
        && IdentityPath[1] != TEXT("scs")
        && IdentityPath[1] != TEXT("instance"))
    {
        OutCode = TEXT("validation.invalid_reference");
        OutMessage = TEXT("Level Component StableRef source must be native, scs, or instance.");
        return false;
    }

    FLevelSnapshot Snapshot;
    FString Reason;
    if (!BuildSnapshot(Target, TEXT("stable_ref"), Snapshot, Reason))
    {
        OutCode = TEXT("capability.level_not_loaded");
        OutMessage = Reason.IsEmpty()
            ? TEXT("Level StableRef resolution requires an exact loaded authored Editor source World.")
            : Reason;
        return false;
    }
    if (!Snapshot.bIdentityComplete)
    {
        OutCode = TEXT("validation.reference_scan_incomplete");
        OutMessage = TEXT("The Level Actor identity environment is incomplete; StableRef resolution is fail-closed.");
        return false;
    }

    const FLevelActorEntry* Match = nullptr;
    int32 MatchCount = 0;
    for (const FLevelActorEntry& Entry : Snapshot.Actors)
    {
        if (Entry.Guid == Guid)
        {
            Match = &Entry;
            ++MatchCount;
        }
    }
    if (MatchCount != 1 || Match == nullptr)
    {
        OutCode = MatchCount > 1
            ? TEXT("resolution.identity_conflict")
            : TEXT("resolution.object_not_found");
        OutMessage = MatchCount > 1
            ? TEXT("Multiple persisted Level Actor records share this ActorGuid; the StableRef is ambiguous.")
            : TEXT("No persisted Actor or root World Partition descriptor has this ActorGuid in the exact Level Target.");
        return false;
    }

    if (IdentityPath.Num() == 1)
    {
        Ref->Values.Reset();
        Ref->SetStringField(TEXT("kind"), TEXT("actor"));
        Ref->SetStringField(TEXT("id"), GuidText(Guid));
        return true;
    }

    AActor* OwnerActor = Match->Actor.Get();
    if (!Match->bLoaded || !IsValid(OwnerActor))
    {
        OutCode = TEXT("capability.component_owner_not_loaded");
        OutMessage = TEXT("Component StableRef resolution will not load or pin an unloaded Actor descriptor.");
        return false;
    }
    FSalLevelComponentSnapshot ComponentSnapshot;
    if (!BuildLevelComponentSnapshot(
            TArray<AActor*>{OwnerActor},
            TEXT("stable_ref"),
            ComponentSnapshot,
            Reason)
        || !ComponentSnapshot.bIdentityComplete)
    {
        OutCode = TEXT("validation.reference_scan_incomplete");
        OutMessage = Reason.IsEmpty()
            ? TEXT("The Component source identity scan is incomplete; StableRef resolution is fail-closed.")
            : Reason;
        return false;
    }
    const FSalLevelComponentEntry* ComponentMatch = nullptr;
    int32 ComponentMatchCount = 0;
    for (const FSalLevelComponentEntry& Entry : ComponentSnapshot.Entries)
    {
        if (Entry.ActorGuid == Guid
            && Entry.Source == IdentityPath[1]
            && Entry.Id == IdentityPath[2])
        {
            ComponentMatch = &Entry;
            ++ComponentMatchCount;
        }
    }
    if (ComponentMatchCount != 1 || ComponentMatch == nullptr)
    {
        OutCode = ComponentMatchCount > 1
            ? TEXT("resolution.identity_conflict")
            : TEXT("resolution.object_not_found");
        OutMessage = ComponentMatchCount > 1
            ? TEXT("Multiple persistent Components share this source-qualified identity.")
            : TEXT("No persistent Component matches this exact source-qualified identity.");
        return false;
    }

    Ref->Values.Reset();
    Ref->SetStringField(TEXT("kind"), TEXT("component"));
    Ref->SetStringField(TEXT("actorId"), GuidText(Guid));
    Ref->SetStringField(TEXT("source"), ComponentMatch->Source);
    Ref->SetStringField(TEXT("id"), ComponentMatch->Id);
    return true;
}
}
