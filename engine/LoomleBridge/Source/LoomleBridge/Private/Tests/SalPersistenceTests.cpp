// Copyright 2026 Loomle contributors.

#if WITH_DEV_AUTOMATION_TESTS

#include "Sal/Blueprint/SalBlueprintInterface.h"
#include "Tests/LoomleTestEditorState.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Editor.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "GameFramework/Actor.h"
#include "HAL/FileManager.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/AutomationTest.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "Sal/SalModel.h"
#include "UObject/GarbageCollection.h"
#include "UObject/Package.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/UObjectHash.h"

namespace
{
using namespace Loomle::Sal;

FString PersistenceGuidText(const FGuid& Guid)
{
    return Guid.ToString(EGuidFormats::DigitsWithHyphensLower);
}

void PreparePersistencePackageForCollection(UPackage* Package)
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
        true);
}

class FBlueprintPersistenceFixture
{
public:
    ~FBlueprintPersistenceFixture()
    {
        FString Ignored;
        Cleanup(Ignored);
    }

    bool Create(FString& OutError)
    {
        const FString Token = FGuid::NewGuid().ToString(EGuidFormats::Digits);
        PackageName = FString::Printf(
            TEXT("/Game/LoomleTests/Persistence/%s/BP_Persistence"),
            *Token);
        ObjectPath = PackageName + TEXT(".BP_Persistence");
        Filename = FPackageName::LongPackageNameToFilename(
            PackageName,
            FPackageName::GetAssetPackageExtension());
        IFileManager::Get().MakeDirectory(*FPaths::GetPath(Filename), true);

        Package = CreatePackage(*PackageName);
        Blueprint = Package != nullptr
            ? FKismetEditorUtilities::CreateBlueprint(
                AActor::StaticClass(),
                Package,
                FName(TEXT("BP_Persistence")),
                BPTYPE_Normal,
                UBlueprint::StaticClass(),
                UBlueprintGeneratedClass::StaticClass(),
                NAME_None)
            : nullptr;
        if (Blueprint == nullptr)
        {
            OutError = TEXT("UE failed to create the persistent Blueprint fixture.");
            return false;
        }
        FAssetRegistryModule::AssetCreated(Blueprint);
        bRegistered = true;
        FKismetEditorUtilities::CompileBlueprint(Blueprint);
        if (Blueprint->Status == BS_Error
            || Blueprint->GeneratedClass == nullptr
            || !Blueprint->GetBlueprintGuid().IsValid())
        {
            OutError = TEXT("Persistent Blueprint fixture failed to compile with stable identity.");
            return false;
        }
        Package->SetDirtyFlag(false);
        return true;
    }

    bool Unload(FString& OutError)
    {
        UPackage* PackageToUnload = Package;
        PreparePersistencePackageForCollection(PackageToUnload);
        Blueprint = nullptr;
        Package = nullptr;
        CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);
        if (FindPackage(nullptr, *PackageName) != nullptr
            || FindObject<UObject>(nullptr, *ObjectPath) != nullptr)
        {
            OutError = TEXT("Persistent Blueprint fixture remained loaded after GC.");
            return false;
        }
        return true;
    }

    bool Reload(FString& OutError)
    {
        Blueprint = LoadObject<UBlueprint>(nullptr, *ObjectPath);
        Package = Blueprint != nullptr ? Blueprint->GetOutermost() : nullptr;
        if (Blueprint == nullptr || Package == nullptr)
        {
            OutError = TEXT("UE failed to reload the saved Blueprint fixture.");
            return false;
        }
        return true;
    }

    bool Cleanup(FString& OutError)
    {
        if (bCleaned)
        {
            return OutError.IsEmpty();
        }
        bCleaned = true;
        UObject* Loaded = !ObjectPath.IsEmpty()
            ? FindObject<UObject>(nullptr, *ObjectPath)
            : nullptr;
        if (Loaded != nullptr && bRegistered)
        {
            FAssetRegistryModule::AssetDeleted(Loaded);
            bRegistered = false;
        }
        UPackage* LoadedPackage = !PackageName.IsEmpty()
            ? FindPackage(nullptr, *PackageName)
            : nullptr;
        PreparePersistencePackageForCollection(LoadedPackage);
        Blueprint = nullptr;
        Package = nullptr;
        CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);
        if (!PackageName.IsEmpty()
            && FindPackage(nullptr, *PackageName) != nullptr)
        {
            OutError = TEXT("Persistent Blueprint fixture package remained loaded during cleanup.");
        }
        if (!Filename.IsEmpty()
            && IFileManager::Get().FileExists(*Filename)
            && !IFileManager::Get().Delete(*Filename, false, true, true))
        {
            if (!OutError.IsEmpty())
            {
                OutError += TEXT(" ");
            }
            OutError += TEXT("Persistent Blueprint fixture file could not be deleted.");
        }
        if (!Filename.IsEmpty())
        {
            const FString Directory = FPaths::GetPath(Filename);
            IFileManager::Get().DeleteDirectory(*Directory, false, true);
        }
        return OutError.IsEmpty();
    }

    UPackage* Package = nullptr;
    UBlueprint* Blueprint = nullptr;
    FString PackageName;
    FString ObjectPath;
    FString Filename;

private:
    bool bRegistered = false;
    bool bCleaned = false;
};

TSharedRef<FJsonObject> PersistenceLocal(const FString& Name)
{
    TSharedRef<FJsonObject> Ref = MakeShared<FJsonObject>();
    Ref->SetStringField(TEXT("kind"), TEXT("local"));
    Ref->SetStringField(TEXT("name"), Name);
    return Ref;
}

TSharedRef<FJsonObject> PersistenceMember(
    const TSharedRef<FJsonObject>& Owner,
    const FString& Field)
{
    TSharedRef<FJsonObject> Ref = MakeShared<FJsonObject>();
    Ref->SetStringField(TEXT("kind"), TEXT("member"));
    Ref->SetObjectField(TEXT("object"), Owner);
    Ref->SetArrayField(
        TEXT("path"),
        {MakeShared<FJsonValueString>(Field)});
    return Ref;
}

FSalPatch PersistenceDescriptionPatch(const FString& Description)
{
    TSharedRef<FJsonObject> Set = MakeShared<FJsonObject>();
    Set->SetStringField(TEXT("kind"), TEXT("set"));
    Set->SetObjectField(
        TEXT("target"),
        PersistenceMember(
            PersistenceLocal(TEXT("blueprint")),
            TEXT("BlueprintDescription")));
    Set->SetStringField(TEXT("value"), Description);
    FSalPatch Patch;
    Patch.Alias = TEXT("blueprint");
    Patch.Statements = {MakeShared<FJsonValueObject>(Set)};
    return Patch;
}

FSalPatch PersistenceSavePatch()
{
    TSharedRef<FJsonObject> Save = MakeShared<FJsonObject>();
    Save->SetStringField(TEXT("kind"), TEXT("save"));
    FSalPatch Patch;
    Patch.Alias = TEXT("blueprint");
    Patch.Statements = {MakeShared<FJsonValueObject>(Save)};
    return Patch;
}

FSalResolvedTarget PersistenceTarget(UBlueprint* Blueprint)
{
    FSalResolvedTarget Target;
    Target.Kind = ESalTargetKind::Blueprint;
    Target.Alias = TEXT("blueprint");
    Target.AssetPath = Blueprint != nullptr ? Blueprint->GetPathName() : FString();
    Target.Object = Blueprint;
    Target.Package = Blueprint != nullptr ? Blueprint->GetOutermost() : nullptr;
    Target.Blueprint = Blueprint;
    Target.Class = Blueprint != nullptr ? Blueprint->GeneratedClass.Get() : nullptr;
    Target.Id = Blueprint != nullptr
        ? PersistenceGuidText(Blueprint->GetBlueprintGuid())
        : FString();
    Target.Interfaces = {FName(TEXT("asset")), FName(TEXT("blueprint"))};
    return Target;
}

bool PersistenceResultBool(
    const TSharedPtr<FJsonObject>& Result,
    const TCHAR* Field,
    const bool Default = false)
{
    bool Value = Default;
    return Result.IsValid() && Result->TryGetBoolField(Field, Value)
        ? Value
        : Default;
}

bool JsonContainsStringField(
    const TSharedPtr<FJsonValue>& Value,
    const FString& Field,
    const FString& Expected)
{
    if (!Value.IsValid())
    {
        return false;
    }
    const TSharedPtr<FJsonObject>* Object = nullptr;
    if (Value->TryGetObject(Object)
        && Object != nullptr
        && (*Object).IsValid())
    {
        FString Actual;
        if ((*Object)->TryGetStringField(Field, Actual)
            && Actual == Expected)
        {
            return true;
        }
        for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : (*Object)->Values)
        {
            if (JsonContainsStringField(Pair.Value, Field, Expected))
            {
                return true;
            }
        }
    }
    const TArray<TSharedPtr<FJsonValue>>* Array = nullptr;
    if (Value->TryGetArray(Array) && Array != nullptr)
    {
        for (const TSharedPtr<FJsonValue>& Item : *Array)
        {
            if (JsonContainsStringField(Item, Field, Expected))
            {
                return true;
            }
        }
    }
    return false;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FSalBlueprintPersistenceRoundTripTest,
    "Loomle.Sal.Blueprint.Persistence.SaveUnloadReload",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSalBlueprintPersistenceRoundTripTest::RunTest(const FString& Parameters)
{
    if (GEditor == nullptr
        || GEditor->IsTransactionActive()
        || GEditor->IsPlaySessionInProgress())
    {
        AddError(TEXT("Blueprint persistence test requires an idle Editor outside PIE."));
        return false;
    }

    Loomle::Tests::FScopedIsolatedTransactor Transactions;
    const bool bTransactionsIsolated = Transactions.Initialize();
    TestTrue(
        TEXT("Persistence fixture isolates its transaction history"),
        bTransactionsIsolated);
    if (!bTransactionsIsolated)
    {
        return false;
    }

    FBlueprintPersistenceFixture Fixture;
    FString Error;
    const bool bCreated = Fixture.Create(Error);
    TestTrue(
        *FString::Printf(TEXT("Persistent fixture is created: %s"), *Error),
        bCreated);
    if (Fixture.Blueprint == nullptr)
    {
        return false;
    }

    const FString BlueprintId =
        PersistenceGuidText(Fixture.Blueprint->GetBlueprintGuid());
    const FString Description =
        TEXT("Persisted through SAL save and UE PostLoad");
    const TSharedPtr<FJsonObject> Applied =
        FSalBlueprintInterface::Patch(
            PersistenceDescriptionPatch(Description),
            PersistenceTarget(Fixture.Blueprint));
    TestTrue(
        TEXT("Persistent description Patch validates"),
        PersistenceResultBool(Applied, TEXT("valid")));
    TestTrue(
        TEXT("Persistent description Patch applies"),
        PersistenceResultBool(Applied, TEXT("applied")));
    TestEqual(
        TEXT("Persistent description is present before save"),
        Fixture.Blueprint->BlueprintDescription,
        Description);

    const TSharedPtr<FJsonObject> Saved =
        FSalBlueprintInterface::Patch(
            PersistenceSavePatch(),
            PersistenceTarget(Fixture.Blueprint));
    TestTrue(
        TEXT("Blueprint save Patch validates"),
        PersistenceResultBool(Saved, TEXT("valid")));
    TestTrue(
        TEXT("Blueprint save Patch writes a dirty package"),
        PersistenceResultBool(Saved, TEXT("applied")));
    TestFalse(
        TEXT("Blueprint package is clean after SAL save"),
        Fixture.Package->IsDirty());
    TestTrue(
        TEXT("SAL save creates the fixture package on disk"),
        IFileManager::Get().FileExists(*Fixture.Filename));

    Transactions.Restore();
    Error.Reset();
    const bool bUnloaded = Fixture.Unload(Error);
    TestTrue(
        *FString::Printf(TEXT("Saved fixture unloads: %s"), *Error),
        bUnloaded);
    Error.Reset();
    const bool bReloaded = Fixture.Reload(Error);
    TestTrue(
        *FString::Printf(TEXT("Saved fixture reloads: %s"), *Error),
        bReloaded);
    if (Fixture.Blueprint != nullptr)
    {
        TestEqual(
            TEXT("Reload preserves Blueprint identity"),
            PersistenceGuidText(Fixture.Blueprint->GetBlueprintGuid()),
            BlueprintId);
        TestEqual(
            TEXT("Reload preserves the SAL-authored description"),
            Fixture.Blueprint->BlueprintDescription,
            Description);

        FSalQuery Query;
        Query.Alias = TEXT("blueprint");
        Query.Operation = MakeShared<FJsonObject>();
        Query.Operation->SetStringField(TEXT("kind"), TEXT("blueprint"));
        Query.Operation->SetStringField(TEXT("id"), BlueprintId);
        const TSharedPtr<FJsonObject> Readback =
            FSalBlueprintInterface::Query(
                Query,
                PersistenceTarget(Fixture.Blueprint));
        TestFalse(
            TEXT("PostLoad exact Blueprint Query succeeds"),
            PersistenceResultBool(Readback, TEXT("isError")));
        TestTrue(
            TEXT("PostLoad SAL Query returns BlueprintDescription"),
            JsonContainsStringField(
                MakeShared<FJsonValueObject>(Readback),
                TEXT("BlueprintDescription"),
                Description));
    }

    Error.Reset();
    const bool bCleaned = Fixture.Cleanup(Error);
    TestTrue(
        *FString::Printf(TEXT("Persistent fixture is removed: %s"), *Error),
        bCleaned);
    return true;
}

#endif
