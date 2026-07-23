// Copyright 2026 Loomle contributors.

#if WITH_DEV_AUTOMATION_TESTS

#include "Sal/Asset/SalAssetInterface.h"

#include "Algo/Reverse.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "GameFramework/Actor.h"
#include "HAL/FileManager.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/AutomationTest.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "UObject/GarbageCollection.h"
#include "UObject/Package.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/UObjectHash.h"

namespace
{
using namespace Loomle::Sal;

class FScopedAssetQueryFixture
{
public:
    FScopedAssetQueryFixture()
    {
        const FString Token = FGuid::NewGuid().ToString(EGuidFormats::Digits);
        Package = CreatePackage(*FString::Printf(
            TEXT("/Game/LoomleTests/SalAssetQuery_%s"),
            *Token));
        const FName BlueprintName(*FString::Printf(
            TEXT("BP_AssetQuery_%s"),
            *Token));
        Blueprint = FKismetEditorUtilities::CreateBlueprint(
            AActor::StaticClass(),
            Package,
            BlueprintName,
            BPTYPE_Normal,
            UBlueprint::StaticClass(),
            UBlueprintGeneratedClass::StaticClass(),
            NAME_None);
        if (Blueprint != nullptr)
        {
            Blueprint->BlueprintDescription =
                FString::ChrN(9000, TEXT('x'));
            FModuleManager::LoadModuleChecked<FAssetRegistryModule>(
                TEXT("AssetRegistry"));
            FAssetRegistryModule::AssetCreated(Blueprint);
            bRegistered = true;
        }
        if (Package != nullptr)
        {
            Package->SetDirtyFlag(false);
        }
    }

    ~FScopedAssetQueryFixture()
    {
        if (bRegistered && Blueprint != nullptr)
        {
            FAssetRegistryModule::AssetDeleted(Blueprint);
        }
        if (Blueprint != nullptr)
        {
            Blueprint->ClearFlags(RF_Public | RF_Standalone);
        }
        if (Package != nullptr)
        {
            Package->SetDirtyFlag(false);
        }
    }

    FScopedAssetQueryFixture(const FScopedAssetQueryFixture&) = delete;
    FScopedAssetQueryFixture& operator=(const FScopedAssetQueryFixture&) = delete;

    UPackage* Package = nullptr;
    UBlueprint* Blueprint = nullptr;

private:
    bool bRegistered = false;
};

FSalResolvedTarget AssetRootTarget()
{
    FSalResolvedTarget Target;
    Target.Kind = ESalTargetKind::AssetRoot;
    Target.Alias = TEXT("asset");
    Target.Interfaces = {FName(TEXT("asset"))};
    return Target;
}

FSalQuery AssetQuery(const FString& Text = FString())
{
    FSalQuery Query;
    Query.Alias = TEXT("asset");
    Query.Operation = MakeShared<FJsonObject>();
    Query.Operation->SetStringField(TEXT("kind"), TEXT("assets"));
    if (!Text.IsEmpty())
    {
        Query.Operation->SetStringField(TEXT("text"), Text);
    }
    return Query;
}

TSharedPtr<FJsonObject> FieldCondition(
    const FString& Kind,
    const TArray<FString>& Path,
    const FString& Value)
{
    TSharedPtr<FJsonObject> Field = MakeShared<FJsonObject>();
    TArray<TSharedPtr<FJsonValue>> Segments;
    for (const FString& Segment : Path)
    {
        Segments.Add(MakeShared<FJsonValueString>(Segment));
    }
    Field->SetArrayField(TEXT("path"), MoveTemp(Segments));

    TSharedPtr<FJsonObject> Condition = MakeShared<FJsonObject>();
    Condition->SetStringField(TEXT("kind"), Kind);
    Condition->SetObjectField(TEXT("field"), Field);
    Condition->SetStringField(TEXT("value"), Value);
    return Condition;
}

TArray<TSharedPtr<FJsonObject>> CallArgs(
    const TSharedPtr<FJsonObject>& Result,
    const FString& Callee)
{
    TArray<TSharedPtr<FJsonObject>> Args;
    const TSharedPtr<FJsonObject>* Object = nullptr;
    const TArray<TSharedPtr<FJsonValue>>* Statements = nullptr;
    if (!Result.IsValid()
        || !Result->TryGetObjectField(TEXT("object"), Object)
        || Object == nullptr
        || !(*Object)->TryGetArrayField(TEXT("statements"), Statements)
        || Statements == nullptr)
    {
        return Args;
    }
    for (const TSharedPtr<FJsonValue>& StatementValue : *Statements)
    {
        const TSharedPtr<FJsonObject>* Statement = nullptr;
        const TSharedPtr<FJsonObject>* Call = nullptr;
        const TSharedPtr<FJsonObject>* CallArgsObject = nullptr;
        FString ActualCallee;
        if (StatementValue.IsValid()
            && StatementValue->TryGetObject(Statement)
            && Statement != nullptr
            && (*Statement)->TryGetObjectField(TEXT("value"), Call)
            && Call != nullptr
            && (*Call)->TryGetStringField(TEXT("callee"), ActualCallee)
            && ActualCallee == Callee
            && (*Call)->TryGetObjectField(TEXT("args"), CallArgsObject)
            && CallArgsObject != nullptr)
        {
            Args.Add(*CallArgsObject);
        }
    }
    return Args;
}

bool HasCommentContaining(
    const TSharedPtr<FJsonObject>& Result,
    const FString& Text)
{
    const TSharedPtr<FJsonObject>* Object = nullptr;
    const TArray<TSharedPtr<FJsonValue>>* Statements = nullptr;
    if (!Result.IsValid()
        || !Result->TryGetObjectField(TEXT("object"), Object)
        || Object == nullptr
        || !(*Object)->TryGetArrayField(TEXT("statements"), Statements)
        || Statements == nullptr)
    {
        return false;
    }
    for (const TSharedPtr<FJsonValue>& StatementValue : *Statements)
    {
        const TSharedPtr<FJsonObject>* Statement = nullptr;
        FString Kind;
        FString Comment;
        if (StatementValue.IsValid()
            && StatementValue->TryGetObject(Statement)
            && Statement != nullptr
            && (*Statement)->TryGetStringField(TEXT("kind"), Kind)
            && Kind == TEXT("comment")
            && (*Statement)->TryGetStringField(TEXT("text"), Comment)
            && Comment.Contains(Text))
        {
            return true;
        }
    }
    return false;
}

bool HasDiagnosticCode(
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
    for (const TSharedPtr<FJsonValue>& DiagnosticValue : *Diagnostics)
    {
        const TSharedPtr<FJsonObject>* Diagnostic = nullptr;
        FString Code;
        if (DiagnosticValue.IsValid()
            && DiagnosticValue->TryGetObject(Diagnostic)
            && Diagnostic != nullptr
            && (*Diagnostic)->TryGetStringField(TEXT("code"), Code)
            && Code == ExpectedCode)
        {
            return true;
        }
    }
    return false;
}

bool ContainsAssetPath(
    const TSharedPtr<FJsonObject>& Result,
    const FString& ExpectedPath)
{
    for (const TSharedPtr<FJsonObject>& Args : CallArgs(Result, TEXT("asset")))
    {
        FString Path;
        if (Args.IsValid()
            && Args->TryGetStringField(TEXT("path"), Path)
            && Path == ExpectedPath)
        {
            return true;
        }
    }
    return false;
}

TSharedPtr<FJsonObject> LogicalCondition(
    const FString& Kind,
    const TArray<TSharedPtr<FJsonObject>>& Conditions)
{
    TArray<TSharedPtr<FJsonValue>> Values;
    Values.Reserve(Conditions.Num());
    for (const TSharedPtr<FJsonObject>& Condition : Conditions)
    {
        Values.Add(MakeShared<FJsonValueObject>(Condition));
    }
    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetStringField(TEXT("kind"), Kind);
    Result->SetArrayField(TEXT("conditions"), MoveTemp(Values));
    return Result;
}

TSharedPtr<FJsonObject> OrderBy(
    const FString& Key,
    const FString& Direction)
{
    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetStringField(TEXT("key"), Key);
    Result->SetStringField(TEXT("direction"), Direction);
    return Result;
}

FString NextCursor(const TSharedPtr<FJsonObject>& Result)
{
    const TSharedPtr<FJsonObject>* Page = nullptr;
    FString Next;
    if (Result.IsValid()
        && Result->TryGetObjectField(TEXT("page"), Page)
        && Page != nullptr)
    {
        (*Page)->TryGetStringField(TEXT("next"), Next);
    }
    return Next;
}

TArray<FString> AssetPaths(const TSharedPtr<FJsonObject>& Result)
{
    TArray<FString> Paths;
    for (const TSharedPtr<FJsonObject>& Args : CallArgs(Result, TEXT("asset")))
    {
        FString Path;
        if (Args.IsValid() && Args->TryGetStringField(TEXT("path"), Path))
        {
            Paths.Add(MoveTemp(Path));
        }
    }
    return Paths;
}

FSalPatch SavePatch(const bool bDryRun, const int32 StatementCount = 1)
{
    FSalPatch Patch;
    Patch.Alias = TEXT("asset");
    Patch.bDryRun = bDryRun;
    for (int32 Index = 0; Index < StatementCount; ++Index)
    {
        TSharedRef<FJsonObject> Save = MakeShared<FJsonObject>();
        Save->SetStringField(TEXT("kind"), TEXT("save"));
        Patch.Statements.Add(MakeShared<FJsonValueObject>(Save));
    }
    return Patch;
}

FSalResolvedTarget ExactAssetTarget(UObject* Asset)
{
    FSalResolvedTarget Target;
    Target.Kind = ESalTargetKind::Asset;
    Target.Alias = TEXT("asset");
    Target.AssetPath = Asset != nullptr ? Asset->GetPathName() : FString();
    Target.Object = Asset;
    Target.Package = Asset != nullptr ? Asset->GetOutermost() : nullptr;
    Target.Interfaces = {FName(TEXT("asset"))};
    return Target;
}

bool ResultBool(
    const TSharedPtr<FJsonObject>& Result,
    const TCHAR* Field,
    const bool Default = false)
{
    bool Value = Default;
    return Result.IsValid() && Result->TryGetBoolField(Field, Value)
        ? Value
        : Default;
}

void PrepareAssetPackageForCollection(UPackage* Package)
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

class FAssetCollectionFixture
{
public:
    FAssetCollectionFixture()
    {
        Token = FGuid::NewGuid().ToString(EGuidFormats::Digits);
        RootPath = TEXT("/Game/LoomleTests/SalAssetCollection_") + Token;
        static const TCHAR* Names[] = {
            TEXT("BP_Alpha"),
            TEXT("BP_Bravo"),
            TEXT("BP_Charlie")};
        for (const TCHAR* Name : Names)
        {
            UPackage* Package = CreatePackage(
                *FString::Printf(TEXT("%s/%s"), *RootPath, Name));
            UBlueprint* Blueprint =
                Package != nullptr
                    ? FKismetEditorUtilities::CreateBlueprint(
                        AActor::StaticClass(),
                        Package,
                        FName(Name),
                        BPTYPE_Normal,
                        UBlueprint::StaticClass(),
                        UBlueprintGeneratedClass::StaticClass(),
                        NAME_None)
                    : nullptr;
            if (Blueprint != nullptr)
            {
                FAssetRegistryModule::AssetCreated(Blueprint);
                Assets.Add(Blueprint);
                Paths.Add(Blueprint->GetPathName());
            }
            if (Package != nullptr)
            {
                Package->SetDirtyFlag(false);
            }
        }
        Paths.Sort();
    }

    ~FAssetCollectionFixture()
    {
        TArray<UPackage*> Packages;
        for (UBlueprint* Blueprint : Assets)
        {
            if (Blueprint != nullptr)
            {
                FAssetRegistryModule::AssetDeleted(Blueprint);
                Blueprint->ClearFlags(RF_Public | RF_Standalone);
                if (UPackage* Package = Blueprint->GetOutermost())
                {
                    Packages.AddUnique(Package);
                }
            }
        }
        Assets.Reset();
        for (UPackage* Package : Packages)
        {
            PrepareAssetPackageForCollection(Package);
        }
        CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);
    }

    bool IsValid() const
    {
        return Assets.Num() == 3 && Paths.Num() == 3;
    }

    FString Token;
    FString RootPath;
    TArray<UBlueprint*> Assets;
    TArray<FString> Paths;
};

class FPersistentAssetFixture
{
public:
    ~FPersistentAssetFixture()
    {
        FString Ignored;
        Cleanup(Ignored);
    }

    bool Create(FString& OutError)
    {
        const FString Token =
            FGuid::NewGuid().ToString(EGuidFormats::Digits);
        PackageName = FString::Printf(
            TEXT("/Game/LoomleTests/AssetPersistence/%s/BP_Asset"),
            *Token);
        ObjectPath = PackageName + TEXT(".BP_Asset");
        Filename = FPackageName::LongPackageNameToFilename(
            PackageName,
            FPackageName::GetAssetPackageExtension());
        IFileManager::Get().MakeDirectory(
            *FPaths::GetPath(Filename),
            true);
        Package = CreatePackage(*PackageName);
        Blueprint =
            Package != nullptr
                ? FKismetEditorUtilities::CreateBlueprint(
                    AActor::StaticClass(),
                    Package,
                    FName(TEXT("BP_Asset")),
                    BPTYPE_Normal,
                    UBlueprint::StaticClass(),
                    UBlueprintGeneratedClass::StaticClass(),
                    NAME_None)
                : nullptr;
        if (Blueprint == nullptr)
        {
            OutError = TEXT("UE failed to create the persistent Asset fixture.");
            return false;
        }
        Description = TEXT("Persisted by the exact Asset save interface");
        Blueprint->BlueprintDescription = Description;
        FAssetRegistryModule::AssetCreated(Blueprint);
        bRegistered = true;
        Package->SetDirtyFlag(true);
        return true;
    }

    bool Unload(FString& OutError)
    {
        PrepareAssetPackageForCollection(Package);
        Blueprint = nullptr;
        Package = nullptr;
        CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);
        if (FindPackage(nullptr, *PackageName) != nullptr
            || FindObject<UObject>(nullptr, *ObjectPath) != nullptr)
        {
            OutError =
                TEXT("Persistent Asset fixture remained loaded after GC.");
            return false;
        }
        return true;
    }

    bool Reload(FString& OutError)
    {
        Blueprint = LoadObject<UBlueprint>(nullptr, *ObjectPath);
        Package =
            Blueprint != nullptr ? Blueprint->GetOutermost() : nullptr;
        if (Blueprint == nullptr || Package == nullptr)
        {
            OutError = TEXT("UE failed to reload the saved Asset fixture.");
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
        UObject* Loaded =
            !ObjectPath.IsEmpty()
                ? FindObject<UObject>(nullptr, *ObjectPath)
                : nullptr;
        if (Loaded != nullptr && bRegistered)
        {
            FAssetRegistryModule::AssetDeleted(Loaded);
            bRegistered = false;
        }
        UPackage* LoadedPackage =
            !PackageName.IsEmpty()
                ? FindPackage(nullptr, *PackageName)
                : nullptr;
        PrepareAssetPackageForCollection(LoadedPackage);
        Blueprint = nullptr;
        Package = nullptr;
        CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);
        if (!PackageName.IsEmpty()
            && FindPackage(nullptr, *PackageName) != nullptr)
        {
            OutError =
                TEXT("Persistent Asset package remained loaded during cleanup.");
        }
        if (!Filename.IsEmpty()
            && IFileManager::Get().FileExists(*Filename)
            && !IFileManager::Get().Delete(*Filename, false, true, true))
        {
            if (!OutError.IsEmpty())
            {
                OutError += TEXT(" ");
            }
            OutError += TEXT("Persistent Asset file could not be deleted.");
        }
        if (!Filename.IsEmpty())
        {
            IFileManager::Get().DeleteDirectory(
                *FPaths::GetPath(Filename),
                false,
                true);
        }
        return OutError.IsEmpty();
    }

    UPackage* Package = nullptr;
    UBlueprint* Blueprint = nullptr;
    FString PackageName;
    FString ObjectPath;
    FString Filename;
    FString Description;

private:
    bool bRegistered = false;
    bool bCleaned = false;
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FSalAssetQuerySmokeTest,
    "Loomle.Sal.Asset.Query.ExactAndCollection",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSalAssetQuerySmokeTest::RunTest(const FString& Parameters)
{
    FScopedAssetQueryFixture Fixture;
    TestNotNull(TEXT("In-memory Blueprint fixture is created"), Fixture.Blueprint);
    if (Fixture.Blueprint == nullptr)
    {
        return false;
    }

    const FString AssetPath = Fixture.Blueprint->GetPathName();
    const FString AssetName = Fixture.Blueprint->GetName();

    FSalQuery Search = AssetQuery(AssetName);
    Search.PageLimit = 10;
    const TSharedPtr<FJsonObject> SearchResult =
        FSalAssetInterface::Query(Search, AssetRootTarget());
    TestTrue(
        TEXT("Collection search returns the registered in-memory asset"),
        ContainsAssetPath(SearchResult, AssetPath));

    FSalQuery Exact = AssetQuery();
    Exact.PageLimit = 2;
    Exact.Where = FieldCondition(TEXT("eq"), {TEXT("path")}, AssetPath);
    const TSharedPtr<FJsonObject> ExactResult =
        FSalAssetInterface::Query(Exact, AssetRootTarget());
    const TArray<TSharedPtr<FJsonObject>> ExactAssets =
        CallArgs(ExactResult, TEXT("asset"));
    TestEqual(TEXT("Exact path filter returns one asset"), ExactAssets.Num(), 1);
    if (ExactAssets.Num() == 1)
    {
        FString Type;
        bool bLoaded = false;
        const TArray<TSharedPtr<FJsonValue>>* Domains = nullptr;
        TestTrue(
            TEXT("Exact asset preserves its native Blueprint type"),
            ExactAssets[0]->TryGetStringField(TEXT("type"), Type)
                && Type == UBlueprint::StaticClass()->GetPathName());
        TestTrue(
            TEXT("Exact in-memory asset reports loaded state"),
            ExactAssets[0]->TryGetBoolField(TEXT("loaded"), bLoaded)
                && bLoaded);
        TestTrue(
            TEXT("Exact Blueprint asset exposes composed domains"),
            ExactAssets[0]->TryGetArrayField(TEXT("domains"), Domains)
                && Domains != nullptr
                && Domains->Num() >= 2);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FSalAssetRegistryTagSafetyTest,
    "Loomle.Sal.Asset.Query.RegistryTagSafety",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSalAssetRegistryTagSafetyTest::RunTest(const FString& Parameters)
{
    FScopedAssetQueryFixture Fixture;
    TestNotNull(TEXT("Blueprint Registry Tag fixture is created"), Fixture.Blueprint);
    if (Fixture.Blueprint == nullptr)
    {
        return false;
    }

    FSalQuery Query = AssetQuery();
    Query.PageLimit = 2;
    Query.Where = FieldCondition(
        TEXT("eq"),
        {TEXT("path")},
        Fixture.Blueprint->GetPathName());
    Query.With.Add(TEXT("registryTags"));
    const TSharedPtr<FJsonObject> Result =
        FSalAssetInterface::Query(Query, AssetRootTarget());
    const TArray<TSharedPtr<FJsonObject>> Assets =
        CallArgs(Result, TEXT("asset"));
    TestEqual(TEXT("Registry Tag query returns the exact fixture"), Assets.Num(), 1);
    if (Assets.Num() == 1)
    {
        const TSharedPtr<FJsonObject>* Tags = nullptr;
        TestTrue(
            TEXT("Registry Tags remain an inline object"),
            Assets[0]->TryGetObjectField(TEXT("registryTags"), Tags)
                && Tags != nullptr);
        if (Tags != nullptr)
        {
            TestFalse(
                TEXT("FiBData is never returned as an inline Registry Tag"),
                (*Tags)->HasField(TEXT("FiBData")));
            TestFalse(
                TEXT("Legacy FiB is never returned as an inline Registry Tag"),
                (*Tags)->HasField(TEXT("FiB")));
            TestFalse(
                TEXT("Oversized ordinary Registry Tags are not returned inline"),
                (*Tags)->HasField(TEXT("BlueprintDescription")));
        }
    }
    TestTrue(
        TEXT("Omitted oversized Registry Tags report only size and reason"),
        HasCommentContaining(
            Result,
            TEXT("BlueprintDescription: reason=value_too_large")));

    FSalQuery UnsafeComparison = AssetQuery();
    UnsafeComparison.Where = FieldCondition(
        TEXT("eq"),
        {TEXT("registryTag"), TEXT("FiBData")},
        TEXT(""));
    const TSharedPtr<FJsonObject> UnsafeResult =
        FSalAssetInterface::Query(UnsafeComparison, AssetRootTarget());
    TestTrue(
        TEXT("Opaque FiBData comparison is rejected before registry scanning"),
        HasDiagnosticCode(UnsafeResult, TEXT("capability.unsupported_query")));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FSalAssetCollectionContractTest,
    "Loomle.Sal.Asset.Query.FilterOrderCursorAndIdentity",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSalAssetCollectionContractTest::RunTest(const FString& Parameters)
{
    FAssetCollectionFixture Fixture;
    TestTrue(
        TEXT("Asset collection fixture registers three exact assets"),
        Fixture.IsValid());
    if (!Fixture.IsValid())
    {
        return false;
    }

    FSalQuery Query = AssetQuery(Fixture.Token);
    Query.PageLimit = 1;
    Query.Where = LogicalCondition(
        TEXT("and"),
        {
            FieldCondition(TEXT("eq"), {TEXT("root")}, TEXT("/Game")),
            FieldCondition(
                TEXT("eq"),
                {TEXT("type")},
                UBlueprint::StaticClass()->GetPathName()),
            FieldCondition(
                TEXT("contains"),
                {TEXT("path")},
                Fixture.RootPath)
        });
    Query.OrderBy = {OrderBy(TEXT("path"), TEXT("desc"))};

    TArray<FString> ActualPaths;
    FString Cursor;
    for (int32 PageIndex = 0; PageIndex < 3; ++PageIndex)
    {
        FSalQuery PageQuery = Query;
        PageQuery.PageAfter = Cursor;
        const TSharedPtr<FJsonObject> Result =
            FSalAssetInterface::Query(PageQuery, AssetRootTarget());
        const TArray<FString> PagePaths = AssetPaths(Result);
        TestEqual(
            *FString::Printf(
                TEXT("Asset page %d contains exactly one record"),
                PageIndex + 1),
            PagePaths.Num(),
            1);
        ActualPaths.Append(PagePaths);
        Cursor = NextCursor(Result);
        TestEqual(
            *FString::Printf(
                TEXT("Asset page %d cursor termination is exact"),
                PageIndex + 1),
            Cursor.IsEmpty(),
            PageIndex == 2);
    }

    TArray<FString> ExpectedPaths = Fixture.Paths;
    Algo::Reverse(ExpectedPaths);
    TestTrue(
        TEXT("Filtered, paged Asset query preserves explicit descending order"),
        ActualPaths == ExpectedPaths);

    FSalQuery ChangedQuery = Query;
    ChangedQuery.PageAfter = NextCursor(
        FSalAssetInterface::Query(Query, AssetRootTarget()));
    ChangedQuery.OrderBy = {OrderBy(TEXT("path"), TEXT("asc"))};
    TestTrue(
        TEXT("Asset cursor cannot be reused after changing query order"),
        HasDiagnosticCode(
            FSalAssetInterface::Query(
                ChangedQuery,
                AssetRootTarget()),
            TEXT("validation.invalid_cursor")));

    FSalQuery InvalidClause = Query;
    InvalidClause.PageAfter.Reset();
    InvalidClause.OrderBy =
        {OrderBy(TEXT("unsupported_field"), TEXT("asc"))};
    TestTrue(
        TEXT("Unsupported Asset order clauses fail explicitly"),
        HasDiagnosticCode(
            FSalAssetInterface::Query(
                InvalidClause,
                AssetRootTarget()),
            TEXT("capability.unsupported_query")));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FSalAssetSavePersistenceTest,
    "Loomle.Sal.Asset.Mutation.SaveDryLiveReloadAndZeroLoadQuery",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSalAssetSavePersistenceTest::RunTest(const FString& Parameters)
{
    FPersistentAssetFixture Fixture;
    FString Error;
    const bool bCreated = Fixture.Create(Error);
    TestTrue(
        *FString::Printf(
            TEXT("Persistent Asset fixture is created: %s"),
            *Error),
        bCreated);
    if (!bCreated)
    {
        return false;
    }

    const TSharedPtr<FJsonObject> Invalid =
        FSalAssetInterface::Patch(
            SavePatch(true, 2),
            ExactAssetTarget(Fixture.Blueprint));
    TestTrue(
        TEXT("Asset save rejects more than one independent operation"),
        HasDiagnosticCode(
            Invalid,
            TEXT("capability.unsupported_patch")));

    const TSharedPtr<FJsonObject> DryRun =
        FSalAssetInterface::Patch(
            SavePatch(true),
            ExactAssetTarget(Fixture.Blueprint));
    TestTrue(
        TEXT("Asset save dry-run validates"),
        ResultBool(DryRun, TEXT("valid")));
    TestTrue(
        TEXT("Asset save reports dry-run"),
        ResultBool(DryRun, TEXT("dryRun")));
    TestFalse(
        TEXT("Asset save dry-run does not write"),
        ResultBool(DryRun, TEXT("applied"), true));
    TestTrue(
        TEXT("Asset save dry-run preserves dirty state"),
        Fixture.Package->IsDirty());
    TestFalse(
        TEXT("Asset save dry-run creates no package file"),
        IFileManager::Get().FileExists(*Fixture.Filename));

    const TSharedPtr<FJsonObject> Saved =
        FSalAssetInterface::Patch(
            SavePatch(false),
            ExactAssetTarget(Fixture.Blueprint));
    TestTrue(
        TEXT("Asset save validates"),
        ResultBool(Saved, TEXT("valid")));
    TestTrue(
        TEXT("Asset save writes the dirty package"),
        ResultBool(Saved, TEXT("applied")));
    TestFalse(
        TEXT("Asset save leaves the package clean"),
        Fixture.Package->IsDirty());
    TestTrue(
        TEXT("Asset save creates the exact package file"),
        IFileManager::Get().FileExists(*Fixture.Filename));

    const TSharedPtr<FJsonObject> CleanSave =
        FSalAssetInterface::Patch(
            SavePatch(false),
            ExactAssetTarget(Fixture.Blueprint));
    TestTrue(
        TEXT("Saving an already-clean Asset remains valid"),
        ResultBool(CleanSave, TEXT("valid")));
    TestFalse(
        TEXT("Saving an already-clean Asset is a no-op"),
        ResultBool(CleanSave, TEXT("applied"), true));

    Error.Reset();
    const bool bUnloaded = Fixture.Unload(Error);
    TestTrue(
        *FString::Printf(
            TEXT("Saved Asset fixture unloads: %s"),
            *Error),
        bUnloaded);
    if (!bUnloaded)
    {
        return false;
    }

    FSalQuery Exact = AssetQuery();
    Exact.PageLimit = 1;
    Exact.Where = FieldCondition(
        TEXT("eq"),
        {TEXT("path")},
        Fixture.ObjectPath);
    const TSharedPtr<FJsonObject> UnloadedResult =
        FSalAssetInterface::Query(Exact, AssetRootTarget());
    const TArray<TSharedPtr<FJsonObject>> UnloadedAssets =
        CallArgs(UnloadedResult, TEXT("asset"));
    TestEqual(
        TEXT("Exact Asset Registry query finds the saved unloaded asset"),
        UnloadedAssets.Num(),
        1);
    if (UnloadedAssets.Num() == 1)
    {
        bool bLoaded = true;
        TestTrue(
            TEXT("Exact Asset Registry query reports loaded=false"),
            UnloadedAssets[0]->TryGetBoolField(
                TEXT("loaded"),
                bLoaded)
                && !bLoaded);
    }
    TestNull(
        TEXT("Asset Registry query does not load the exact asset"),
        FindObject<UObject>(nullptr, *Fixture.ObjectPath));

    Error.Reset();
    const bool bReloaded = Fixture.Reload(Error);
    TestTrue(
        *FString::Printf(
            TEXT("Saved Asset fixture reloads: %s"),
            *Error),
        bReloaded);
    if (Fixture.Blueprint != nullptr)
    {
        TestEqual(
            TEXT("Reload preserves the authored Asset field"),
            Fixture.Blueprint->BlueprintDescription,
            Fixture.Description);
    }

    Error.Reset();
    const bool bCleaned = Fixture.Cleanup(Error);
    TestTrue(
        *FString::Printf(
            TEXT("Persistent Asset fixture is removed: %s"),
            *Error),
        bCleaned);
    return true;
}

#endif
