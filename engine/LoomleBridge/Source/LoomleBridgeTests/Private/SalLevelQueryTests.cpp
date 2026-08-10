// Copyright 2026 Loomle contributors.

#if WITH_DEV_AUTOMATION_TESTS

#include "LoomleTestObjectIteration.h"
#include "Sal/SalModule.h"

#include "AssetRegistry/AssetData.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Editor.h"
#include "Editor/Transactor.h"
#include "Engine/Level.h"
#include "Engine/Selection.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "HAL/FileManager.h"
#include "Helpers/PCGHelpers.h"
#include "Misc/AutomationTest.h"
#include "Misc/CoreDelegates.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "UObject/GarbageCollection.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/UObjectHash.h"
#include "UObject/UnrealType.h"

namespace
{
using namespace Loomle::Sal;

FString LevelGuidText(const FGuid& Guid)
{
    return Guid.ToString(EGuidFormats::DigitsWithHyphensLower);
}

TArray<TSharedPtr<FJsonValue>> LevelStringValues(
    const TArray<FString>& Strings)
{
    TArray<TSharedPtr<FJsonValue>> Values;
    Values.Reserve(Strings.Num());
    for (const FString& String : Strings)
    {
        Values.Add(MakeShared<FJsonValueString>(String));
    }
    return Values;
}

TSharedRef<FJsonObject> LevelTarget(
    const FString& Asset,
    const FString& Type = FString())
{
    TSharedRef<FJsonObject> Target = MakeShared<FJsonObject>();
    Target->SetStringField(TEXT("kind"), TEXT("target"));
    Target->SetStringField(TEXT("domain"), TEXT("level"));
    Target->SetStringField(TEXT("asset"), Asset);
    if (!Type.IsEmpty())
    {
        Target->SetStringField(TEXT("type"), Type);
    }
    return Target;
}

TSharedRef<FJsonObject> LevelTargetBinding(
    const TSharedRef<FJsonObject>& Target)
{
    TSharedRef<FJsonObject> Binding = MakeShared<FJsonObject>();
    Binding->SetStringField(TEXT("alias"), TEXT("level_scope"));
    Binding->SetObjectField(TEXT("target"), Target);
    return Binding;
}

TSharedRef<FJsonObject> LevelOperation(
    const FString& Kind,
    const FString& SearchText = FString())
{
    TSharedRef<FJsonObject> Operation = MakeShared<FJsonObject>();
    Operation->SetStringField(TEXT("kind"), Kind);
    if (!SearchText.IsEmpty())
    {
        Operation->SetStringField(TEXT("text"), SearchText);
    }
    return Operation;
}

TSharedRef<FJsonObject> LevelStableRef(const FString& ActorId)
{
    TSharedRef<FJsonObject> Ref = MakeShared<FJsonObject>();
    Ref->SetStringField(TEXT("kind"), TEXT("stable_ref"));
    Ref->SetArrayField(
        TEXT("identityPath"),
        LevelStringValues({ActorId}));
    return Ref;
}

TSharedRef<FJsonObject> LevelExactOperation(const FString& ActorId)
{
    TSharedRef<FJsonObject> Operation = LevelOperation(TEXT("object"));
    Operation->SetObjectField(TEXT("target"), LevelStableRef(ActorId));
    return Operation;
}

TSharedRef<FJsonObject> LevelQueryArguments(
    const TSharedRef<FJsonObject>& Target,
    const TSharedRef<FJsonObject>& Operation,
    const int32 PageLimit = 0,
    const FString& PageAfter = FString())
{
    TSharedRef<FJsonObject> Query = MakeShared<FJsonObject>();
    Query->SetStringField(TEXT("kind"), TEXT("query"));
    Query->SetObjectField(TEXT("target"), LevelTargetBinding(Target));
    Query->SetObjectField(TEXT("operation"), Operation);
    if (PageLimit > 0 || !PageAfter.IsEmpty())
    {
        TSharedRef<FJsonObject> Page = MakeShared<FJsonObject>();
        if (PageLimit > 0)
        {
            Page->SetNumberField(TEXT("limit"), PageLimit);
        }
        if (!PageAfter.IsEmpty())
        {
            Page->SetStringField(TEXT("after"), PageAfter);
        }
        Query->SetObjectField(TEXT("page"), Page);
    }

    TSharedRef<FJsonObject> Arguments = MakeShared<FJsonObject>();
    Arguments->SetObjectField(TEXT("object"), Query);
    return Arguments;
}

bool LevelHasDiagnostic(
    const TSharedPtr<FJsonObject>& Result,
    const FString& ExpectedCode)
{
    const TArray<TSharedPtr<FJsonValue>>* Diagnostics = nullptr;
    if (!Result.IsValid()
        || !Result->TryGetArrayField(TEXT("diagnostics"), Diagnostics)
        || Diagnostics == nullptr)
    {
        return false;
    }
    for (const TSharedPtr<FJsonValue>& Value : *Diagnostics)
    {
        const TSharedPtr<FJsonObject>* Diagnostic = nullptr;
        FString Code;
        if (Value.IsValid()
            && Value->TryGetObject(Diagnostic)
            && Diagnostic != nullptr
            && (*Diagnostic).IsValid()
            && (*Diagnostic)->TryGetStringField(TEXT("code"), Code)
            && Code == ExpectedCode)
        {
            return true;
        }
    }
    return false;
}

bool LevelHasError(const TSharedPtr<FJsonObject>& Result)
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
            && (*Diagnostic)->TryGetStringField(TEXT("severity"), Severity)
            && Severity == TEXT("error"))
        {
            return true;
        }
    }
    return false;
}

bool LevelHasTargetContext(
    const TSharedPtr<FJsonObject>& Result,
    const FString& Expected)
{
    FString Context;
    return Result.IsValid()
        && Result->TryGetStringField(TEXT("targetContext"), Context)
        && Context == Expected;
}

bool HasCanonicalLevelTarget(
    const TSharedPtr<FJsonObject>& Result,
    const FString& ExpectedAsset)
{
    const TSharedPtr<FJsonObject>* Binding = nullptr;
    const TSharedPtr<FJsonObject>* Target = nullptr;
    FString Domain;
    FString Asset;
    FString Type;
    return Result.IsValid()
        && Result->TryGetObjectField(TEXT("target"), Binding)
        && Binding != nullptr
        && (*Binding).IsValid()
        && (*Binding)->TryGetObjectField(TEXT("target"), Target)
        && Target != nullptr
        && (*Target).IsValid()
        && (*Target)->TryGetStringField(TEXT("domain"), Domain)
        && Domain == TEXT("level")
        && (*Target)->TryGetStringField(TEXT("asset"), Asset)
        && Asset == ExpectedAsset
        && (*Target)->TryGetStringField(TEXT("type"), Type)
        && Type == UWorld::StaticClass()->GetPathName();
}

bool TryReadLevelObjectFields(
    const TSharedPtr<FJsonValue>& Value,
    const TSharedPtr<FJsonObject>*& OutFields,
    FString* OutSemanticTag = nullptr)
{
    OutFields = nullptr;
    const TSharedPtr<FJsonObject>* Object = nullptr;
    FString Kind;
    if (!Value.IsValid()
        || !Value->TryGetObject(Object)
        || Object == nullptr
        || !(*Object).IsValid()
        || !(*Object)->TryGetStringField(TEXT("kind"), Kind)
        || Kind != TEXT("object")
        || !(*Object)->TryGetObjectField(TEXT("fields"), OutFields)
        || OutFields == nullptr)
    {
        return false;
    }
    if (OutSemanticTag != nullptr)
    {
        OutSemanticTag->Reset();
        (*Object)->TryGetStringField(TEXT("semanticTag"), *OutSemanticTag);
    }
    return true;
}

TArray<TSharedPtr<FJsonObject>> LevelTaggedObjectFields(
    const TSharedPtr<FJsonObject>& Result,
    const FString& SemanticTag)
{
    TArray<TSharedPtr<FJsonObject>> Matches;
    const TSharedPtr<FJsonObject>* Object = nullptr;
    const TArray<TSharedPtr<FJsonValue>>* Statements = nullptr;
    if (!Result.IsValid()
        || !Result->TryGetObjectField(TEXT("object"), Object)
        || Object == nullptr
        || !(*Object).IsValid()
        || !(*Object)->TryGetArrayField(TEXT("statements"), Statements)
        || Statements == nullptr)
    {
        return Matches;
    }
    for (const TSharedPtr<FJsonValue>& StatementValue : *Statements)
    {
        const TSharedPtr<FJsonObject>* Statement = nullptr;
        const TSharedPtr<FJsonObject>* Fields = nullptr;
        FString ActualTag;
        if (StatementValue.IsValid()
            && StatementValue->TryGetObject(Statement)
            && Statement != nullptr
            && (*Statement).IsValid()
            && TryReadLevelObjectFields(
                (*Statement)->TryGetField(TEXT("value")),
                Fields,
                &ActualTag)
            && Fields != nullptr
            && ActualTag == SemanticTag)
        {
            Matches.Add(*Fields);
        }
    }
    return Matches;
}

TSharedPtr<FJsonObject> FindLevelFieldsById(
    const TArray<TSharedPtr<FJsonObject>>& Objects,
    const FString& ExpectedId)
{
    for (const TSharedPtr<FJsonObject>& Fields : Objects)
    {
        FString Id;
        if (Fields.IsValid()
            && Fields->TryGetStringField(TEXT("id"), Id)
            && Id == ExpectedId)
        {
            return Fields;
        }
    }
    return nullptr;
}

int32 CountLevelFieldsById(
    const TArray<TSharedPtr<FJsonObject>>& Objects,
    const FString& ExpectedId)
{
    int32 Count = 0;
    for (const TSharedPtr<FJsonObject>& Fields : Objects)
    {
        FString Id;
        if (Fields.IsValid()
            && Fields->TryGetStringField(TEXT("id"), Id)
            && Id == ExpectedId)
        {
            ++Count;
        }
    }
    return Count;
}

TSharedPtr<FJsonObject> FirstLevelAssetFields(
    const TSharedPtr<FJsonObject>& Result)
{
    const TSharedPtr<FJsonObject>* Object = nullptr;
    const TArray<TSharedPtr<FJsonValue>>* Statements = nullptr;
    if (!Result.IsValid()
        || !Result->TryGetObjectField(TEXT("object"), Object)
        || Object == nullptr
        || !(*Object).IsValid()
        || !(*Object)->TryGetArrayField(TEXT("statements"), Statements)
        || Statements == nullptr)
    {
        return nullptr;
    }

    TSharedPtr<FJsonObject> Fields = MakeShared<FJsonObject>();
    for (const TSharedPtr<FJsonValue>& StatementValue : *Statements)
    {
        const TSharedPtr<FJsonObject>* Statement = nullptr;
        const TSharedPtr<FJsonObject>* Target = nullptr;
        const TSharedPtr<FJsonObject>* Owner = nullptr;
        const TArray<TSharedPtr<FJsonValue>>* Path = nullptr;
        FString TargetKind;
        FString OwnerKind;
        FString OwnerName;
        FString FieldName;
        if (StatementValue.IsValid()
            && StatementValue->TryGetObject(Statement)
            && Statement != nullptr
            && (*Statement).IsValid()
            && (*Statement)->TryGetObjectField(TEXT("target"), Target)
            && Target != nullptr
            && (*Target).IsValid()
            && (*Target)->TryGetStringField(TEXT("kind"), TargetKind)
            && TargetKind == TEXT("member")
            && (*Target)->TryGetObjectField(TEXT("object"), Owner)
            && Owner != nullptr
            && (*Owner).IsValid()
            && (*Owner)->TryGetStringField(TEXT("kind"), OwnerKind)
            && OwnerKind == TEXT("local")
            && (*Owner)->TryGetStringField(TEXT("name"), OwnerName)
            && OwnerName == TEXT("level_scope")
            && (*Target)->TryGetArrayField(TEXT("path"), Path)
            && Path != nullptr
            && Path->Num() == 1
            && (*Path)[0].IsValid()
            && (*Path)[0]->TryGetString(FieldName)
            && !FieldName.IsEmpty())
        {
            Fields->SetField(
                FieldName,
                (*Statement)->TryGetField(TEXT("value")));
        }
    }
    return Fields->Values.IsEmpty() ? nullptr : Fields;
}

bool ReadLevelNextCursor(
    const TSharedPtr<FJsonObject>& Result,
    FString& OutCursor)
{
    OutCursor.Reset();
    const TSharedPtr<FJsonObject>* Page = nullptr;
    return Result.IsValid()
        && Result->TryGetObjectField(TEXT("page"), Page)
        && Page != nullptr
        && (*Page).IsValid()
        && (*Page)->TryGetStringField(TEXT("next"), OutCursor)
        && !OutCursor.IsEmpty();
}

void PrepareLevelPackageForCollection(UPackage* Package)
{
    if (Package == nullptr)
    {
        return;
    }
    Package->SetDirtyFlag(false);
    Package->ClearFlags(RF_Public | RF_Standalone);
    ForEachObjectWithPackage(
        Package,
        [](UObject* Object)
        {
            Object->ClearFlags(RF_Public | RF_Standalone);
            return true;
        },
        Loomle::Tests::IncludeNestedObjects);
}

bool SetFixtureGuidProperty(
    AActor* Actor,
    const FName PropertyName,
    const FGuid& Guid)
{
    FStructProperty* GuidProperty =
        FindFProperty<FStructProperty>(
            AActor::StaticClass(),
            PropertyName);
    if (Actor == nullptr
        || GuidProperty == nullptr
        || GuidProperty->Struct != TBaseStructure<FGuid>::Get())
    {
        return false;
    }
    *GuidProperty->ContainerPtrToValuePtr<FGuid>(Actor) = Guid;
    return true;
}

bool SetFixtureActorGuid(AActor* Actor, const FGuid& Guid)
{
    return SetFixtureGuidProperty(
            Actor,
            TEXT("ActorGuid"),
            Guid)
        && Actor->GetActorGuid() == Guid;
}

struct FLevelMapRecord
{
    FString PackageName;
    FString ObjectPath;
    FString Filename;
    UWorld* World = nullptr;
    bool bRegisteredInMemory = false;
};

class FScopedLevelQueryFixture
{
public:
    ~FScopedLevelQueryFixture()
    {
        FString Ignored;
        Cleanup(Ignored);
    }

    FScopedLevelQueryFixture(const FScopedLevelQueryFixture&) = delete;
    FScopedLevelQueryFixture& operator=(const FScopedLevelQueryFixture&) = delete;
    FScopedLevelQueryFixture() = default;

    bool Build(FString& OutError)
    {
        Token = FGuid::NewGuid().ToString(EGuidFormats::Digits);
        RootPackagePath = FString::Printf(
            TEXT("/Game/LoomleTests/Level/%s"),
            *Token);

        if (!CreateMap(TEXT("L_10_Loaded"), Loaded, OutError))
        {
            return false;
        }
        Alpha = SpawnActor(
            Loaded.World,
            TEXT("Actor_Alpha"),
            TEXT("Loomle Level Alpha"),
            FTransform(FVector(100.0, 20.0, 5.0)),
            OutError);
        Beta = SpawnActor(
            Loaded.World,
            TEXT("Actor_Beta"),
            TEXT("Loomle Level Beta"),
            FTransform(FVector(-25.0, 50.0, 10.0)),
            OutError);
        Gamma = SpawnActor(
            Loaded.World,
            TEXT("Actor_Gamma"),
            TEXT("Loomle Level Gamma"),
            FTransform(FVector(0.0, 0.0, 125.0)),
            OutError);
        InstanceProjection = SpawnActor(
            Loaded.World,
            TEXT("Actor_InstanceProjection"),
            TEXT("Loomle Instance Projection"),
            FTransform::Identity,
            OutError);
        ExternalContent = SpawnActor(
            Loaded.World,
            TEXT("Actor_ExternalContent"),
            TEXT("Loomle External Content"),
            FTransform::Identity,
            OutError);
        PcgGenerated = SpawnActor(
            Loaded.World,
            TEXT("Actor_PCGGenerated"),
            TEXT("Loomle PCG Generated"),
            FTransform::Identity,
            OutError);
        if (Alpha == nullptr
            || Beta == nullptr
            || Gamma == nullptr
            || InstanceProjection == nullptr
            || ExternalContent == nullptr
            || PcgGenerated == nullptr)
        {
            return false;
        }
        const FGuid InstanceGuid = FGuid::NewGuid();
        const FGuid ContentBundleGuid = FGuid::NewGuid();
        if (!SetFixtureGuidProperty(
            InstanceProjection,
            TEXT("ActorInstanceGuid"),
            InstanceGuid)
            || InstanceProjection->GetActorInstanceGuid() != InstanceGuid
            || !SetFixtureGuidProperty(
            ExternalContent,
            TEXT("ContentBundleGuid"),
            ContentBundleGuid)
            || !ExternalContent->HasExternalContent())
        {
            OutError = TEXT("The Level fixture could not author projection and external-content evidence.");
            return false;
        }
        PcgGenerated->Tags.AddUnique(
            PCGHelpers::DefaultPCGActorTag);
        AlphaId = Alpha->GetActorGuid();
        BetaId = Beta->GetActorGuid();
        GammaId = Gamma->GetActorGuid();
        InstanceProjectionId = InstanceProjection->GetActorGuid();
        ExternalContentId = ExternalContent->GetActorGuid();
        PcgGeneratedId = PcgGenerated->GetActorGuid();
        if (!AlphaId.IsValid()
            || !BetaId.IsValid()
            || !GammaId.IsValid()
            || !InstanceProjectionId.IsValid()
            || !ExternalContentId.IsValid()
            || !PcgGeneratedId.IsValid())
        {
            OutError = TEXT("UE did not assign persistent ActorGuids to the loaded fixture Actors.");
            return false;
        }
        if (!SaveMap(Loaded, OutError))
        {
            return false;
        }

        if (!CreateMap(TEXT("L_20_Unloaded"), Unloaded, OutError)
            || !SaveMap(Unloaded, OutError)
            || !UnloadMap(Unloaded, OutError))
        {
            return false;
        }

        if (!CreateMap(TEXT("L_30_Corrupt"), Corrupt, OutError))
        {
            return false;
        }
        CorruptStable = SpawnActor(
            Corrupt.World,
            TEXT("Actor_Stable"),
            TEXT("Loomle Stable Evidence"),
            FTransform::Identity,
            OutError);
        if (CorruptStable == nullptr || !SaveMap(Corrupt, OutError))
        {
            return false;
        }
        InvalidGuidActor = SpawnActor(
            Corrupt.World,
            TEXT("Actor_InvalidGuid"),
            TEXT("Loomle Invalid Guid"),
            FTransform::Identity,
            OutError);
        DuplicateFirst = SpawnActor(
            Corrupt.World,
            TEXT("Actor_DuplicateA"),
            TEXT("Loomle Duplicate A"),
            FTransform::Identity,
            OutError);
        DuplicateSecond = SpawnActor(
            Corrupt.World,
            TEXT("Actor_DuplicateB"),
            TEXT("Loomle Duplicate B"),
            FTransform::Identity,
            OutError);
        DuplicateId = DuplicateFirst != nullptr
            ? DuplicateFirst->GetActorGuid()
            : FGuid();
        if (InvalidGuidActor == nullptr
            || DuplicateFirst == nullptr
            || DuplicateSecond == nullptr
            || !DuplicateId.IsValid()
            || !SetFixtureActorGuid(InvalidGuidActor, FGuid())
            || !SetFixtureActorGuid(DuplicateSecond, DuplicateId))
        {
            OutError = TEXT("The corrupt Level fixture could not author invalid and duplicate ActorGuid evidence.");
            return false;
        }
        Corrupt.World->GetOutermost()->SetDirtyFlag(false);
        return ValidateOnDiskEvidence(Loaded, false, OutError)
            && ValidateOnDiskEvidence(Unloaded, true, OutError)
            && ValidateOnDiskEvidence(Corrupt, false, OutError);
    }

    bool Cleanup(FString& OutError)
    {
        OutError.Reset();
        if (bCleaned)
        {
            return true;
        }
        bCleaned = true;

        if (bEditorContextChanged && GEditor != nullptr)
        {
            GEditor->GetEditorWorldContext().SetCurrentWorld(
                OriginalEditorWorld);
            bEditorContextChanged = false;
        }

        IAssetRegistry& Registry =
            FModuleManager::LoadModuleChecked<FAssetRegistryModule>(
                TEXT("AssetRegistry"))
                .Get();
        for (FLevelMapRecord* Record : {&Loaded, &Unloaded, &Corrupt})
        {
            if (Record->World != nullptr)
            {
                if (Record->bRegisteredInMemory)
                {
                    FAssetRegistryModule::AssetDeleted(Record->World);
                    Record->bRegisteredInMemory = false;
                }
                UPackage* Package = Record->World->GetOutermost();
                Record->World->DestroyWorld(false);
                PrepareLevelPackageForCollection(Package);
                Record->World = nullptr;
            }
            else if (!Record->PackageName.IsEmpty())
            {
                if (UPackage* Package = FindPackage(
                        nullptr,
                        *Record->PackageName))
                {
                    PrepareLevelPackageForCollection(Package);
                }
            }
        }

        Alpha = nullptr;
        Beta = nullptr;
        Gamma = nullptr;
        InstanceProjection = nullptr;
        ExternalContent = nullptr;
        PcgGenerated = nullptr;
        CorruptStable = nullptr;
        InvalidGuidActor = nullptr;
        DuplicateFirst = nullptr;
        DuplicateSecond = nullptr;
        CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);

        TArray<FString> Filenames;
        for (const FLevelMapRecord* Record : {&Loaded, &Unloaded, &Corrupt})
        {
            if (!Record->Filename.IsEmpty())
            {
                IFileManager::Get().Delete(
                    *Record->Filename,
                    false,
                    true,
                    true);
                Filenames.Add(Record->Filename);
            }
            if (!Record->PackageName.IsEmpty()
                && FindPackage(nullptr, *Record->PackageName) != nullptr
                && OutError.IsEmpty())
            {
                OutError = TEXT("Level fixture package remained loaded during cleanup: ")
                    + Record->PackageName;
            }
        }
        if (!Filenames.IsEmpty())
        {
            Registry.ScanModifiedAssetFiles(Filenames);
        }
        return OutError.IsEmpty();
    }

    bool IsUnloadedMapStillUnloaded() const
    {
        return FindPackage(nullptr, *Unloaded.PackageName) == nullptr
            && FindObject<UWorld>(nullptr, *Unloaded.ObjectPath) == nullptr;
    }

    bool Activate(
        const FLevelMapRecord& Record,
        FString& OutError)
    {
        if (GEditor == nullptr || Record.World == nullptr)
        {
            OutError = TEXT("Cannot activate a null Level fixture in the Editor World context.");
            return false;
        }
        FWorldContext& Context = GEditor->GetEditorWorldContext();
        if (!bEditorContextChanged)
        {
            OriginalEditorWorld = Context.World();
            bEditorContextChanged = true;
        }
        Context.SetCurrentWorld(Record.World);
        if (Context.World() != Record.World)
        {
            OutError = TEXT("UE failed to bind the saved Level fixture to the Editor World context.");
            return false;
        }
        return true;
    }

    FLevelMapRecord Loaded;
    FLevelMapRecord Unloaded;
    FLevelMapRecord Corrupt;
    AActor* Alpha = nullptr;
    AActor* Beta = nullptr;
    AActor* Gamma = nullptr;
    AActor* InstanceProjection = nullptr;
    AActor* ExternalContent = nullptr;
    AActor* PcgGenerated = nullptr;
    AActor* CorruptStable = nullptr;
    AActor* InvalidGuidActor = nullptr;
    AActor* DuplicateFirst = nullptr;
    AActor* DuplicateSecond = nullptr;
    FGuid AlphaId;
    FGuid BetaId;
    FGuid GammaId;
    FGuid InstanceProjectionId;
    FGuid ExternalContentId;
    FGuid PcgGeneratedId;
    FGuid DuplicateId;

private:
    static UWorld::InitializationValues FixtureInitializationValues()
    {
        return UWorld::InitializationValues()
            .RequiresHitProxies(false)
            .ShouldSimulatePhysics(false)
            .EnableTraceCollision(false)
            .CreateNavigation(false)
            .CreateAISystem(false)
            .AllowAudioPlayback(false)
            .CreatePhysicsScene(false);
    }

    bool CreateMap(
        const FString& AssetName,
        FLevelMapRecord& OutRecord,
        FString& OutError)
    {
        OutRecord.PackageName = RootPackagePath + TEXT("/") + AssetName;
        OutRecord.ObjectPath = OutRecord.PackageName
            + TEXT(".")
            + AssetName;
        OutRecord.Filename = FPackageName::LongPackageNameToFilename(
            OutRecord.PackageName,
            FPackageName::GetMapPackageExtension());
        IFileManager::Get().MakeDirectory(
            *FPaths::GetPath(OutRecord.Filename),
            true);

        UPackage* Package = CreatePackage(*OutRecord.PackageName);
        const UWorld::InitializationValues InitValues =
            FixtureInitializationValues();
        OutRecord.World = UWorld::CreateWorld(
            EWorldType::Editor,
            false,
            FName(*AssetName),
            Package,
            false,
            ERHIFeatureLevel::Num,
            &InitValues);
        if (OutRecord.World == nullptr
            || OutRecord.World->PersistentLevel == nullptr
            || OutRecord.World->GetPathName() != OutRecord.ObjectPath)
        {
            OutError = TEXT("UE failed to create an exact Editor UWorld fixture at ")
                + OutRecord.ObjectPath;
            return false;
        }
        OutRecord.World->SetFlags(
            RF_Public | RF_Standalone | RF_Transactional);
        FAssetRegistryModule::AssetCreated(OutRecord.World);
        OutRecord.bRegisteredInMemory = true;
        OutRecord.World->GetOutermost()->SetDirtyFlag(false);
        return true;
    }

    static AActor* SpawnActor(
        UWorld* World,
        const FString& Name,
        const FString& Label,
        const FTransform& Transform,
        FString& OutError)
    {
        FActorSpawnParameters Params;
        Params.Name = FName(*Name);
        Params.OverrideLevel = World != nullptr
            ? World->PersistentLevel
            : nullptr;
        Params.ObjectFlags = RF_Transactional;
        AActor* Actor = World != nullptr
            ? World->SpawnActor<AActor>(
                AActor::StaticClass(),
                Transform,
                Params)
            : nullptr;
        if (Actor == nullptr)
        {
            OutError = TEXT("UE failed to spawn Level fixture Actor ") + Name;
            return nullptr;
        }
        Actor->SetActorLabel(Label, false);
        return Actor;
    }

    static bool SaveMap(
        FLevelMapRecord& Record,
        FString& OutError)
    {
        if (Record.World == nullptr)
        {
            OutError = TEXT("Cannot save a null Level fixture World.");
            return false;
        }
        UPackage* Package = Record.World->GetOutermost();
        Package->SetDirtyFlag(true);
        Package->FullyLoad();
        FSavePackageArgs SaveArgs;
        SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
        SaveArgs.Error = GLog;
        if (!UPackage::SavePackage(
                Package,
                Record.World,
                *Record.Filename,
                SaveArgs))
        {
            OutError = TEXT("UE failed to save Level fixture ")
                + Record.ObjectPath;
            return false;
        }
        Package->SetDirtyFlag(false);
        FModuleManager::LoadModuleChecked<FAssetRegistryModule>(
            TEXT("AssetRegistry"))
            .Get()
            .ScanModifiedAssetFiles({Record.Filename});
        return true;
    }

    static bool UnloadMap(
        FLevelMapRecord& Record,
        FString& OutError)
    {
        if (Record.World == nullptr)
        {
            OutError = TEXT("Cannot unload a null Level fixture World.");
            return false;
        }
        UPackage* Package = Record.World->GetOutermost();
        if (Record.bRegisteredInMemory)
        {
            FAssetRegistryModule::AssetDeleted(Record.World);
            Record.bRegisteredInMemory = false;
        }
        Record.World->DestroyWorld(false);
        PrepareLevelPackageForCollection(Package);
        Record.World = nullptr;
        CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);

        FModuleManager::LoadModuleChecked<FAssetRegistryModule>(
            TEXT("AssetRegistry"))
            .Get()
            .ScanModifiedAssetFiles({Record.Filename});
        if (FindPackage(nullptr, *Record.PackageName) != nullptr
            || FindObject<UWorld>(nullptr, *Record.ObjectPath) != nullptr)
        {
            OutError = TEXT("Saved Level fixture remained loaded: ")
                + Record.ObjectPath;
            return false;
        }
        return true;
    }

    static bool ValidateOnDiskEvidence(
        const FLevelMapRecord& Record,
        const bool bExpectUnloaded,
        FString& OutError)
    {
        IAssetRegistry& Registry =
            FModuleManager::LoadModuleChecked<FAssetRegistryModule>(
                TEXT("AssetRegistry"))
                .Get();
        const FAssetData Data = Registry.GetAssetByObjectPath(
            FSoftObjectPath(Record.ObjectPath),
            true);
        if (!Data.IsValid()
            || Data.AssetClassPath != UWorld::StaticClass()->GetClassPathName())
        {
            OutError = TEXT("Asset Registry lacks on-disk UWorld evidence for ")
                + Record.ObjectPath;
            return false;
        }
        if (bExpectUnloaded
            && (Data.FastGetAsset(false) != nullptr
                || FindPackage(nullptr, *Record.PackageName) != nullptr))
        {
            OutError = TEXT("On-disk Level fixture unexpectedly remained loaded: ")
                + Record.ObjectPath;
            return false;
        }
        return true;
    }

    FString Token;
    FString RootPackagePath;
    bool bCleaned = false;
    UWorld* OriginalEditorWorld = nullptr;
    bool bEditorContextChanged = false;
};

struct FFixtureActorState
{
    TWeakObjectPtr<AActor> Actor;
    FGuid Guid;
    FString Label;
    FTransform Transform;
};

class FLevelReadInvariant
{
public:
    explicit FLevelReadInvariant(UWorld* InWorld)
        : World(InWorld)
        , GWorldBefore(GWorld.GetReference())
        , EditorWorldBefore(
            GEditor != nullptr
                ? GEditor->GetEditorWorldContext().World()
                : nullptr)
        , CurrentLevelBefore(
            InWorld != nullptr ? InWorld->GetCurrentLevel() : nullptr)
        , PackageDirtyBefore(
            InWorld != nullptr && InWorld->GetOutermost()->IsDirty())
        , SelectedActorCountBefore(
            GEditor != nullptr ? GEditor->GetSelectedActorCount() : -1)
        , SelectedComponentCountBefore(
            GEditor != nullptr ? GEditor->GetSelectedComponentCount() : -1)
        , UndoCountBefore(
            GEditor != nullptr && GEditor->Trans != nullptr
                ? GEditor->Trans->GetUndoCount()
                : -1)
        , QueueLengthBefore(
            GEditor != nullptr && GEditor->Trans != nullptr
                ? GEditor->Trans->GetQueueLength()
                : -1)
    {
        if (World != nullptr && World->PersistentLevel != nullptr)
        {
            for (AActor* Actor : World->PersistentLevel->Actors)
            {
                if (Actor == nullptr)
                {
                    continue;
                }
                FFixtureActorState& State = ActorStates.AddDefaulted_GetRef();
                State.Actor = Actor;
                State.Guid = Actor->GetActorGuid();
                State.Label = Actor->GetActorLabel();
                State.Transform = Actor->GetActorTransform();
            }
        }
        TransactionHandle = FCoreUObjectDelegates::OnObjectTransacted.AddLambda(
            [this](UObject*, const FTransactionObjectEvent&)
            {
                ++TransactionEventCount;
            });
    }

    ~FLevelReadInvariant()
    {
        FCoreUObjectDelegates::OnObjectTransacted.Remove(TransactionHandle);
    }

    bool Verify(FAutomationTestBase& Test) const
    {
        bool bOk = true;
        bOk &= Test.TestEqual(
            TEXT("Level Query preserves GWorld"),
            GWorld.GetReference(),
            GWorldBefore);
        if (GEditor != nullptr)
        {
            bOk &= Test.TestEqual(
                TEXT("Level Query preserves the Editor World context"),
                GEditor->GetEditorWorldContext().World(),
                EditorWorldBefore);
            bOk &= Test.TestEqual(
                TEXT("Level Query preserves Actor selection"),
                GEditor->GetSelectedActorCount(),
                SelectedActorCountBefore);
            bOk &= Test.TestEqual(
                TEXT("Level Query preserves Component selection"),
                GEditor->GetSelectedComponentCount(),
                SelectedComponentCountBefore);
            if (GEditor->Trans != nullptr)
            {
                bOk &= Test.TestEqual(
                    TEXT("Level Query creates no Undo entry"),
                    GEditor->Trans->GetUndoCount(),
                    UndoCountBefore);
                bOk &= Test.TestEqual(
                    TEXT("Level Query preserves the transaction queue"),
                    GEditor->Trans->GetQueueLength(),
                    QueueLengthBefore);
            }
        }
        bOk &= Test.TestEqual(
            TEXT("Level Query emits no UObject transaction"),
            TransactionEventCount,
            0);
        if (World != nullptr)
        {
            bOk &= Test.TestEqual(
                TEXT("Level Query preserves the World's current Level"),
                World->GetCurrentLevel(),
                CurrentLevelBefore);
            bOk &= Test.TestEqual(
                TEXT("Level Query preserves the map package dirty state"),
                World->GetOutermost()->IsDirty(),
                PackageDirtyBefore);
        }
        for (const FFixtureActorState& State : ActorStates)
        {
            AActor* Actor = State.Actor.Get();
            bOk &= Test.TestNotNull(
                TEXT("Level Query preserves each source Actor"),
                Actor);
            if (Actor != nullptr)
            {
                bOk &= Test.TestEqual(
                    TEXT("Level Query preserves ActorGuid"),
                    Actor->GetActorGuid(),
                    State.Guid);
                bOk &= Test.TestEqual(
                    TEXT("Level Query preserves ActorLabel"),
                    Actor->GetActorLabel(),
                    State.Label);
                bOk &= Test.TestTrue(
                    TEXT("Level Query preserves Actor transform"),
                    Actor->GetActorTransform().Equals(State.Transform));
            }
        }
        return bOk;
    }

private:
    UWorld* World = nullptr;
    UWorld* GWorldBefore = nullptr;
    UWorld* EditorWorldBefore = nullptr;
    ULevel* CurrentLevelBefore = nullptr;
    bool PackageDirtyBefore = false;
    int32 SelectedActorCountBefore = -1;
    int32 SelectedComponentCountBefore = -1;
    int32 UndoCountBefore = -1;
    int32 QueueLengthBefore = -1;
    TArray<FFixtureActorState> ActorStates;
    FDelegateHandle TransactionHandle;
    int32 TransactionEventCount = 0;
};

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FSalLevelNoLoadTargetTest,
    "Loomle.Sal.Level.Query.NoLoadCanonicalTarget",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSalLevelNoLoadTargetTest::RunTest(const FString& Parameters)
{
    FScopedLevelQueryFixture Fixture;
    FString Error;
    if (!TestTrue(TEXT("Level no-load fixture builds"), Fixture.Build(Error)))
    {
        AddError(Error);
        return false;
    }
    TestTrue(
        TEXT("No-load fixture starts with no UPackage or UWorld"),
        Fixture.IsUnloadedMapStillUnloaded());

    UWorld* SourceWorld = Fixture.Loaded.World;
    ULevel* SourceLevel = SourceWorld != nullptr
        ? SourceWorld->PersistentLevel
        : nullptr;
    UWorld* ActiveEditorWorld = GEditor != nullptr
        ? GEditor->GetEditorWorldContext().World()
        : nullptr;
    if (!TestNotNull(TEXT("Inactive source fixture has a PersistentLevel"), SourceLevel)
        || !TestNotNull(TEXT("Streamed source fixture has an active Editor World"), ActiveEditorWorld))
    {
        return false;
    }
    const EWorldType::Type OriginalSourceType = SourceWorld->WorldType;
    UWorld* OriginalOwningWorld = SourceLevel->OwningWorld;
    SourceWorld->WorldType = EWorldType::Inactive;
    SourceLevel->OwningWorld = SourceWorld;

    const TSharedRef<FJsonObject> InactiveTarget =
        LevelTarget(Fixture.Loaded.ObjectPath);
    const TSharedPtr<FJsonObject> InactiveTargetResult =
        FSalModule::BuildQueryResult(
            LevelQueryArguments(
                InactiveTarget,
                LevelOperation(TEXT("target"))));
    const TSharedPtr<FJsonObject> InactiveTargetFields =
        FirstLevelAssetFields(InactiveTargetResult);
    bool bInactiveLoaded = true;
    TestTrue(
        TEXT("A saved inactive Editor World canonicalizes without becoming Level content authority"),
        !LevelHasError(InactiveTargetResult)
            && InactiveTargetFields.IsValid()
            && InactiveTargetFields->TryGetBoolField(
                TEXT("loaded"),
                bInactiveLoaded)
            && !bInactiveLoaded);
    const TSharedPtr<FJsonObject> InactiveSummary =
        FSalModule::BuildQueryResult(
            LevelQueryArguments(
                InactiveTarget,
                LevelOperation(TEXT("summary"))));
    TestTrue(
        TEXT("An inactive Editor World cannot serve Level content Query"),
        LevelHasDiagnostic(
            InactiveSummary,
            TEXT("capability.level_not_loaded")));

    SourceLevel->OwningWorld = ActiveEditorWorld;
    const TSharedPtr<FJsonObject> StreamedSummary =
        FSalModule::BuildQueryResult(
            LevelQueryArguments(
                InactiveTarget,
                LevelOperation(TEXT("summary"))));
    const bool bStreamedShapePreserved =
        SourceWorld->WorldType == EWorldType::Inactive
        && SourceLevel->OwningWorld == ActiveEditorWorld;
    SourceLevel->OwningWorld = OriginalOwningWorld;
    SourceWorld->WorldType = OriginalSourceType;
    TestTrue(
        TEXT("An inactive exact source World is readable when its PersistentLevel belongs to the active Editor World"),
        !LevelHasError(StreamedSummary)
            && bStreamedShapePreserved
            && FirstLevelAssetFields(StreamedSummary).IsValid());

    const TSharedRef<FJsonObject> Target =
        LevelTarget(Fixture.Unloaded.ObjectPath);
    const TSharedPtr<FJsonObject> TargetResult =
        FSalModule::BuildQueryResult(
            LevelQueryArguments(Target, LevelOperation(TEXT("target"))));
    TestTrue(
        TEXT("Level target canonicalizes from on-disk Asset Registry evidence"),
        !LevelHasError(TargetResult)
            && LevelHasTargetContext(TargetResult, TEXT("exact_target"))
            && HasCanonicalLevelTarget(
                TargetResult,
                Fixture.Unloaded.ObjectPath));
    const TSharedPtr<FJsonObject> TargetFields =
        FirstLevelAssetFields(TargetResult);
    bool bLoaded = true;
    FString Path;
    TestTrue(
        TEXT("No-load target truthfully reports the canonical unloaded map"),
        TargetFields.IsValid()
            && TargetFields->TryGetStringField(TEXT("path"), Path)
            && Path == Fixture.Unloaded.ObjectPath
            && TargetFields->TryGetBoolField(TEXT("loaded"), bLoaded)
            && !bLoaded);
    TestTrue(
        TEXT("Canonical target Query does not load the map"),
        Fixture.IsUnloadedMapStillUnloaded());

    for (const FString& Operation : {TEXT("summary"), TEXT("actors")})
    {
        const TSharedPtr<FJsonObject> Result =
            FSalModule::BuildQueryResult(
                LevelQueryArguments(Target, LevelOperation(Operation)));
        TestTrue(
            *FString::Printf(
                TEXT("Unloaded Level %s fails with exact loading-state context"),
                *Operation),
            LevelHasDiagnostic(
                Result,
                TEXT("capability.level_not_loaded"))
                && LevelHasTargetContext(Result, TEXT("exact_target"))
                && HasCanonicalLevelTarget(
                    Result,
                    Fixture.Unloaded.ObjectPath));
        TestTrue(
            *FString::Printf(
                TEXT("Unloaded Level %s does not load the map"),
                *Operation),
            Fixture.IsUnloadedMapStillUnloaded());
    }

    const TSharedPtr<FJsonObject> Exact =
        FSalModule::BuildQueryResult(
            LevelQueryArguments(
                Target,
                LevelExactOperation(LevelGuidText(FGuid::NewGuid()))));
    TestTrue(
        TEXT("StableRef resolution on an unloaded exact target reports level_not_loaded"),
        LevelHasDiagnostic(
            Exact,
            TEXT("capability.level_not_loaded"))
            && LevelHasTargetContext(Exact, TEXT("exact_target"))
            && Fixture.IsUnloadedMapStillUnloaded());

    if (!Fixture.Cleanup(Error))
    {
        AddError(Error);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FSalLevelLoadedActorQueryTest,
    "Loomle.Sal.Level.Query.LoadedActorsSummaryStableRefSearchAndPage",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSalLevelLoadedActorQueryTest::RunTest(const FString& Parameters)
{
    FScopedLevelQueryFixture Fixture;
    FString Error;
    if (!TestTrue(TEXT("Loaded Level fixture builds"), Fixture.Build(Error)))
    {
        AddError(Error);
        return false;
    }
    const TSharedRef<FJsonObject> Target =
        LevelTarget(
            Fixture.Loaded.ObjectPath,
            UWorld::StaticClass()->GetPathName());
    if (!TestTrue(
            TEXT("Loaded Level fixture binds to the Editor World context"),
            Fixture.Activate(Fixture.Loaded, Error)))
    {
        AddError(Error);
        return false;
    }
    FLevelReadInvariant Invariant(Fixture.Loaded.World);

    const TSharedPtr<FJsonObject> TargetResult =
        FSalModule::BuildQueryResult(
            LevelQueryArguments(Target, LevelOperation(TEXT("target"))));
    const TSharedPtr<FJsonObject> TargetFields =
        FirstLevelAssetFields(TargetResult);
    bool bLoaded = false;
    FString WorldType;
    TestTrue(
        TEXT("Loaded Level target reports the exact Editor source World"),
        !LevelHasError(TargetResult)
            && HasCanonicalLevelTarget(
                TargetResult,
                Fixture.Loaded.ObjectPath)
            && TargetFields.IsValid()
            && TargetFields->TryGetBoolField(TEXT("loaded"), bLoaded)
            && bLoaded
            && TargetFields->TryGetStringField(TEXT("worldType"), WorldType)
            && WorldType == TEXT("Editor"));

    const TSharedPtr<FJsonObject> Summary =
        FSalModule::BuildQueryResult(
            LevelQueryArguments(Target, LevelOperation(TEXT("summary"))));
    const TSharedPtr<FJsonObject> SummaryFields =
        FirstLevelAssetFields(Summary);
    double ActorCount = 0.0;
    double LoadedActorCount = 0.0;
    double UnloadedDescriptorCount = -1.0;
    double StableActorCount = 0.0;
    bool bIdentityComplete = false;
    TestTrue(
        TEXT("Level summary reports a complete non-WP loaded Actor identity set"),
        !LevelHasError(Summary)
            && SummaryFields.IsValid()
            && SummaryFields->TryGetNumberField(TEXT("actorCount"), ActorCount)
            && ActorCount >= 3.0
            && SummaryFields->TryGetNumberField(
                TEXT("loadedActorCount"),
                LoadedActorCount)
            && LoadedActorCount == ActorCount
            && SummaryFields->TryGetNumberField(
                TEXT("unloadedDescriptorCount"),
                UnloadedDescriptorCount)
            && UnloadedDescriptorCount == 0.0
            && SummaryFields->TryGetNumberField(
                TEXT("stableActorCount"),
                StableActorCount)
            && StableActorCount == ActorCount
            && SummaryFields->TryGetBoolField(
                TEXT("identityComplete"),
                bIdentityComplete)
            && bIdentityComplete);

    const TSharedPtr<FJsonObject> ActorsResult =
        FSalModule::BuildQueryResult(
            LevelQueryArguments(Target, LevelOperation(TEXT("actors"))));
    const TArray<TSharedPtr<FJsonObject>> Actors =
        LevelTaggedObjectFields(ActorsResult, TEXT("actor"));
    const FString AlphaId = LevelGuidText(Fixture.AlphaId);
    const FString BetaId = LevelGuidText(Fixture.BetaId);
    const FString GammaId = LevelGuidText(Fixture.GammaId);
    TestTrue(
        TEXT("Loaded Level actors returns every authored fixture Actor"),
        !LevelHasError(ActorsResult)
            && FindLevelFieldsById(Actors, AlphaId).IsValid()
            && FindLevelFieldsById(Actors, BetaId).IsValid()
            && FindLevelFieldsById(Actors, GammaId).IsValid());
    TestTrue(
        TEXT("Level actors excludes instance projections, external-content Actors, and PCG-generated output"),
        !FindLevelFieldsById(
            Actors,
            LevelGuidText(Fixture.InstanceProjectionId)).IsValid()
            && !FindLevelFieldsById(
                Actors,
                LevelGuidText(Fixture.ExternalContentId)).IsValid()
            && !FindLevelFieldsById(
                Actors,
                LevelGuidText(Fixture.PcgGeneratedId)).IsValid());

    const TSharedPtr<FJsonObject> ExactResult =
        FSalModule::BuildQueryResult(
            LevelQueryArguments(Target, LevelExactOperation(BetaId)));
    const TSharedPtr<FJsonObject> ExactFields =
        FindLevelFieldsById(
            LevelTaggedObjectFields(ExactResult, TEXT("actor")),
            BetaId);
    FString ExactLabel;
    bool bExactLoaded = false;
    bool bStableRefAvailable = false;
    TestTrue(
        TEXT("ActorGuid StableRef resolves the exact loaded Actor"),
        !LevelHasError(ExactResult)
            && ExactFields.IsValid()
            && ExactFields->TryGetStringField(
                TEXT("ActorLabel"),
                ExactLabel)
            && ExactLabel == TEXT("Loomle Level Beta")
            && ExactFields->TryGetBoolField(
                TEXT("loaded"),
                bExactLoaded)
            && bExactLoaded
            && ExactFields->TryGetBoolField(
                TEXT("stableRefAvailable"),
                bStableRefAvailable)
            && bStableRefAvailable);

    for (const FString& NonCanonicalId : {
             BetaId.ToUpper(),
             Fixture.BetaId.ToString(EGuidFormats::Digits)})
    {
        const TSharedPtr<FJsonObject> NonCanonicalExact =
            FSalModule::BuildQueryResult(
                LevelQueryArguments(
                    Target,
                    LevelExactOperation(NonCanonicalId)));
        TestTrue(
            TEXT("Actor StableRef rejects non-canonical ActorGuid text"),
            LevelHasDiagnostic(
                NonCanonicalExact,
                TEXT("validation.invalid_reference")));
    }

    const TSharedPtr<FJsonObject> SearchByLabel =
        FSalModule::BuildQueryResult(
            LevelQueryArguments(
                Target,
                LevelOperation(
                    TEXT("actors"),
                    TEXT("Loomle Level Alpha"))));
    const TArray<TSharedPtr<FJsonObject>> SearchActors =
        LevelTaggedObjectFields(SearchByLabel, TEXT("actor"));
    TestTrue(
        TEXT("Actor search matches the current ActorLabel without using it as identity"),
        !LevelHasError(SearchByLabel)
            && SearchActors.Num() == 1
            && FindLevelFieldsById(SearchActors, AlphaId).IsValid());

    TArray<FString> FullOrder;
    for (const TSharedPtr<FJsonObject>& Fields : Actors)
    {
        FString Id;
        if (Fields.IsValid() && Fields->TryGetStringField(TEXT("id"), Id))
        {
            FullOrder.Add(Id);
        }
    }
    TArray<FString> PagedOrder;
    FString Cursor;
    for (int32 PageIndex = 0; PageIndex < 64; ++PageIndex)
    {
        const TSharedPtr<FJsonObject> Page =
            FSalModule::BuildQueryResult(
                LevelQueryArguments(
                    Target,
                    LevelOperation(TEXT("actors")),
                    1,
                    Cursor));
        if (LevelHasError(Page))
        {
            AddError(TEXT("A deterministic Level Actor continuation page failed."));
            break;
        }
        const TArray<TSharedPtr<FJsonObject>> PageActors =
            LevelTaggedObjectFields(Page, TEXT("actor"));
        if (PageActors.Num() != 1)
        {
            AddError(TEXT("A non-final Level Actor page did not contain exactly one Actor."));
            break;
        }
        FString Id;
        if (!PageActors[0]->TryGetStringField(TEXT("id"), Id))
        {
            AddError(TEXT("A Level Actor page omitted ActorGuid identity."));
            break;
        }
        PagedOrder.Add(Id);
        FString Next;
        if (!ReadLevelNextCursor(Page, Next))
        {
            break;
        }
        Cursor = Next;
    }
    TestEqual(
        TEXT("Cursor pagination reproduces the deterministic full Actor order"),
        PagedOrder,
        FullOrder);

    const TSharedPtr<FJsonObject> FirstSearchPage =
        FSalModule::BuildQueryResult(
            LevelQueryArguments(
                Target,
                LevelOperation(TEXT("actors")),
                1));
    FString SearchBoundCursor;
    if (ReadLevelNextCursor(FirstSearchPage, SearchBoundCursor))
    {
        const TSharedPtr<FJsonObject> ReusedForOtherSearch =
            FSalModule::BuildQueryResult(
                LevelQueryArguments(
                    Target,
                    LevelOperation(TEXT("actors"), TEXT("Alpha")),
                    1,
                    SearchBoundCursor));
        TestTrue(
            TEXT("Actor cursor is bound to its search and snapshot"),
            LevelHasDiagnostic(
                ReusedForOtherSearch,
                TEXT("validation.invalid_cursor")));

        const FString OriginalLabel = Fixture.Alpha->GetActorLabel();
        const bool bOriginalDirty =
            Fixture.Loaded.World->GetOutermost()->IsDirty();
        Fixture.Alpha->SetActorLabel(
            TEXT("Loomle Level Alpha Changed"),
            false);
        const TSharedPtr<FJsonObject> ReusedAfterSnapshotChange =
            FSalModule::BuildQueryResult(
                LevelQueryArguments(
                    Target,
                    LevelOperation(TEXT("actors")),
                    1,
                    SearchBoundCursor));
        TestTrue(
            TEXT("Actor cursor is invalid after any emitted snapshot field changes"),
            LevelHasDiagnostic(
                ReusedAfterSnapshotChange,
                TEXT("validation.invalid_cursor")));
        Fixture.Alpha->SetActorLabel(OriginalLabel, false);
        Fixture.Loaded.World->GetOutermost()->SetDirtyFlag(
            bOriginalDirty);
    }

    const FString OriginalGammaLabel = Fixture.Gamma->GetActorLabel(false);
    const bool bDirtyBeforeLabelRead =
        Fixture.Loaded.World->GetOutermost()->IsDirty();
    Fixture.Gamma->ClearActorLabel();
    int32 ActorLabelChangedEvents = 0;
    const FDelegateHandle LabelChangedHandle =
        FCoreDelegates::OnActorLabelChanged.AddLambda(
            [&ActorLabelChangedEvents](AActor*)
            {
                ++ActorLabelChangedEvents;
            });
    const TSharedPtr<FJsonObject> UnlabeledExact =
        FSalModule::BuildQueryResult(
            LevelQueryArguments(Target, LevelExactOperation(GammaId)));
    const TSharedPtr<FJsonObject> UnlabeledFields =
        FindLevelFieldsById(
            LevelTaggedObjectFields(UnlabeledExact, TEXT("actor")),
            GammaId);
    const bool bLabelRemainedEmpty =
        Fixture.Gamma->GetActorLabel(false).IsEmpty();
    FCoreDelegates::OnActorLabelChanged.Remove(LabelChangedHandle);
    Fixture.Gamma->SetActorLabel(OriginalGammaLabel, false);
    Fixture.Loaded.World->GetOutermost()->SetDirtyFlag(
        bDirtyBeforeLabelRead);
    TestTrue(
        TEXT("Exact Actor Query observes an absent ActorLabel without creating one"),
        !LevelHasError(UnlabeledExact)
            && UnlabeledFields.IsValid()
            && !UnlabeledFields->HasField(TEXT("ActorLabel"))
            && bLabelRemainedEmpty
            && ActorLabelChangedEvents == 0);

    TestTrue(
        TEXT("All loaded Level reads preserve Editor and authored state"),
        Invariant.Verify(*this));

    Fixture.Loaded.World->GetOutermost()->SetDirtyFlag(true);
    const TSharedPtr<FJsonObject> DirtySummary =
        FSalModule::BuildQueryResult(
            LevelQueryArguments(Target, LevelOperation(TEXT("summary"))));
    TestTrue(
        TEXT("Read Query preserves a pre-existing dirty map package"),
        !LevelHasError(DirtySummary)
            && Fixture.Loaded.World->GetOutermost()->IsDirty());
    Fixture.Loaded.World->GetOutermost()->SetDirtyFlag(false);

    if (!Fixture.Cleanup(Error))
    {
        AddError(Error);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FSalLevelActorGuidCorruptionTest,
    "Loomle.Sal.Level.Query.InvalidAndDuplicateActorGuid",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSalLevelActorGuidCorruptionTest::RunTest(const FString& Parameters)
{
    FScopedLevelQueryFixture Fixture;
    FString Error;
    if (!TestTrue(TEXT("Corrupt Level fixture builds"), Fixture.Build(Error)))
    {
        AddError(Error);
        return false;
    }
    const TSharedRef<FJsonObject> Target =
        LevelTarget(
            Fixture.Corrupt.ObjectPath,
            UWorld::StaticClass()->GetPathName());
    if (!TestTrue(
            TEXT("Corrupt Level fixture binds to the Editor World context"),
            Fixture.Activate(Fixture.Corrupt, Error)))
    {
        AddError(Error);
        return false;
    }
    FLevelReadInvariant Invariant(Fixture.Corrupt.World);

    const TSharedPtr<FJsonObject> Summary =
        FSalModule::BuildQueryResult(
            LevelQueryArguments(Target, LevelOperation(TEXT("summary"))));
    const TSharedPtr<FJsonObject> SummaryFields =
        FirstLevelAssetFields(Summary);
    double InvalidCount = 0.0;
    double DuplicateCount = 0.0;
    TestTrue(
        TEXT("Summary exposes bounded invalid and duplicate ActorGuid corruption evidence"),
        !LevelHasError(Summary)
            && LevelHasDiagnostic(
                Summary,
                TEXT("resolution.identity_conflict"))
            && SummaryFields.IsValid()
            && SummaryFields->TryGetNumberField(
                TEXT("invalidActorGuidCount"),
                InvalidCount)
            && InvalidCount == 1.0
            && SummaryFields->TryGetNumberField(
                TEXT("duplicateActorGuidCount"),
                DuplicateCount)
            && DuplicateCount == 1.0);

    const TSharedPtr<FJsonObject> ActorsResult =
        FSalModule::BuildQueryResult(
            LevelQueryArguments(Target, LevelOperation(TEXT("actors"))));
    const TArray<TSharedPtr<FJsonObject>> Actors =
        LevelTaggedObjectFields(ActorsResult, TEXT("actor"));
    const FString InvalidId = LevelGuidText(FGuid());
    const FString DuplicateId = LevelGuidText(Fixture.DuplicateId);
    const TSharedPtr<FJsonObject> InvalidFields =
        FindLevelFieldsById(Actors, InvalidId);
    bool bIdentityValid = true;
    bool bStableRefAvailable = true;
    TestTrue(
        TEXT("Invalid ActorGuid remains readable but receives no StableRef"),
        !LevelHasError(ActorsResult)
            && InvalidFields.IsValid()
            && InvalidFields->TryGetBoolField(
                TEXT("identityValid"),
                bIdentityValid)
            && !bIdentityValid
            && InvalidFields->TryGetBoolField(
                TEXT("stableRefAvailable"),
                bStableRefAvailable)
            && !bStableRefAvailable);
    TestEqual(
        TEXT("Duplicate ActorGuid publishes both corruption records"),
        CountLevelFieldsById(Actors, DuplicateId),
        2);
    for (const TSharedPtr<FJsonObject>& Fields : Actors)
    {
        FString Id;
        if (!Fields.IsValid()
            || !Fields->TryGetStringField(TEXT("id"), Id)
            || Id != DuplicateId)
        {
            continue;
        }
        bool bUnique = true;
        bool bAvailable = true;
        TestTrue(
            TEXT("Each duplicate record denies unique StableRef identity"),
            Fields->TryGetBoolField(TEXT("identityUnique"), bUnique)
                && !bUnique
                && Fields->TryGetBoolField(
                    TEXT("stableRefAvailable"),
                    bAvailable)
                && !bAvailable);
    }

    const TSharedPtr<FJsonObject> DuplicateExact =
        FSalModule::BuildQueryResult(
            LevelQueryArguments(
                Target,
                LevelExactOperation(DuplicateId)));
    TestTrue(
        TEXT("Duplicate ActorGuid StableRef resolution fails closed"),
        LevelHasDiagnostic(
            DuplicateExact,
            TEXT("resolution.identity_conflict")));

    const TSharedPtr<FJsonObject> InvalidExact =
        FSalModule::BuildQueryResult(
            LevelQueryArguments(
                Target,
                LevelExactOperation(InvalidId)));
    TestTrue(
        TEXT("Invalid ActorGuid text cannot become a StableRef"),
        LevelHasDiagnostic(
            InvalidExact,
            TEXT("validation.invalid_reference")));
    TestTrue(
        TEXT("Corruption reads preserve the corrupt source evidence exactly"),
        Invariant.Verify(*this));

    if (!Fixture.Cleanup(Error))
    {
        AddError(Error);
    }
    return true;
}

}

#endif
