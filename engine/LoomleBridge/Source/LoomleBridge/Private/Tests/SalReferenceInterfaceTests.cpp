// Copyright 2026 Loomle contributors.

#if WITH_DEV_AUTOMATION_TESTS

#include "Sal/Reference/SalReferenceInterface.h"
#include "Tests/LoomleTestEditorState.h"

#include "AssetRegistry/AssetData.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Blueprint/BlueprintSupport.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraphSchema_K2.h"
#include "EdGraphSchema_K2_Actions.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "GameFramework/Actor.h"
#include "HAL/FileManager.h"
#include "K2Node_VariableGet.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "LoomleRequestCancellation.h"
#include "Misc/AutomationTest.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "Sal/SalModel.h"
#include "UObject/GarbageCollection.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/UObjectHash.h"

namespace
{
using namespace Loomle::Sal;

FString ReferenceGuidText(const FGuid& Guid)
{
    return Guid.ToString(EGuidFormats::DigitsWithHyphensLower);
}

struct FSavedBlueprint
{
    FString PackageName;
    FString ObjectPath;
    FString Filename;
    FString NodeId;
    bool bRegisteredInMemory = false;
};

class FScopedProjectReferenceFixture
{
public:
    ~FScopedProjectReferenceFixture()
    {
        FString Ignored;
        Cleanup(Ignored);
    }

    FScopedProjectReferenceFixture(const FScopedProjectReferenceFixture&) = delete;
    FScopedProjectReferenceFixture& operator=(const FScopedProjectReferenceFixture&) = delete;

    FScopedProjectReferenceFixture() = default;

    bool Build(FString& OutError)
    {
        Loomle::Tests::FScopedIsolatedTransactor FixtureTransactions;
        if (!FixtureTransactions.Initialize())
        {
            OutError =
                TEXT("Reference fixture could not isolate its authoring transactions.");
            return false;
        }

        // UE 5.7 UBlueprint::PreSave writes the current FiB payload. Saving
        // real packages and then using UE's native unload path makes the
        // public query choose its on-disk Asset Registry/FiB route without a
        // test-only production hook.
        Token = FGuid::NewGuid().ToString(EGuidFormats::Digits);
        RootPackagePath = FString::Printf(
            TEXT("/Game/LoomleTests/Reference/%s"),
            *Token);

        Source = CreateBlueprint(
            TEXT("BP_00_Source"),
            AActor::StaticClass(),
            SourceAsset,
            OutError);
        if (Source == nullptr)
        {
            return false;
        }

        VariableName = TEXT("Health");
        const FEdGraphPinType VariableType(
            UEdGraphSchema_K2::PC_Int,
            NAME_None,
            nullptr,
            EPinContainerType::None,
            false,
            FEdGraphTerminalType());
        if (!FBlueprintEditorUtils::AddMemberVariable(
                Source,
                VariableName,
                VariableType))
        {
            OutError = TEXT("UE failed to add the source Blueprint variable.");
            return false;
        }
        FKismetEditorUtilities::CompileBlueprint(Source);
        VariableId = FBlueprintEditorUtils::FindMemberVariableGuidByName(
            Source,
            VariableName);
        if (!VariableId.IsValid()
            || Source->GeneratedClass == nullptr
            || Source->Status == BS_Error)
        {
            OutError = TEXT("The source Blueprint variable did not compile to a stable declaration.");
            return false;
        }
        if (!SaveBlueprint(Source, SourceAsset, OutError))
        {
            return false;
        }

        UBlueprint* First = CreateReferenceCandidate(
            TEXT("BP_10_First"),
            FirstAsset,
            OutError);
        if (First == nullptr
            || !SaveBlueprint(First, FirstAsset, OutError))
        {
            return false;
        }

        UBlueprint* Second = CreateReferenceCandidate(
            TEXT("BP_20_Second"),
            SecondAsset,
            OutError);
        if (Second == nullptr
            || !SaveBlueprint(Second, SecondAsset, OutError))
        {
            return false;
        }

        UBlueprint* Decoy = CreateBlueprint(
            TEXT("BP_30_Unrelated"),
            AActor::StaticClass(),
            DecoyAsset,
            OutError);
        if (Decoy == nullptr
            || !SaveBlueprint(Decoy, DecoyAsset, OutError))
        {
            return false;
        }

        IAssetRegistry& Registry =
            FModuleManager::LoadModuleChecked<FAssetRegistryModule>(
                TEXT("AssetRegistry"))
                .Get();
        Registry.ScanModifiedAssetFiles(AllFilenames());
        FixtureTransactions.Restore();

        const TArray<UPackage*> PackagesToUnload = {
            First->GetOutermost(),
            Second->GetOutermost(),
            Decoy->GetOutermost()};
        UnregisterLoadedAsset(First, FirstAsset);
        UnregisterLoadedAsset(Second, SecondAsset);
        UnregisterLoadedAsset(Decoy, DecoyAsset);
        TArray<FString> PackageNames;
        for (UPackage* Package : PackagesToUnload)
        {
            if (Package != nullptr)
            {
                PackageNames.Add(Package->GetName());
                PreparePackageForCollection(Package);
            }
        }
        First = nullptr;
        Second = nullptr;
        Decoy = nullptr;
        CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);
        for (const FString& PackageName : PackageNames)
        {
            if (FindPackage(nullptr, *PackageName) != nullptr)
            {
                OutError =
                    TEXT("Reference candidate package remained loaded: ")
                    + PackageName;
                return false;
            }
        }

        TArray<FString> CandidateFilenames;
        for (const FString* Filename : {
                 &FirstAsset.Filename,
                 &SecondAsset.Filename,
                 &DecoyAsset.Filename})
        {
            if (Filename != nullptr && !Filename->IsEmpty())
            {
                CandidateFilenames.Add(*Filename);
            }
        }
        Registry.ScanModifiedAssetFiles(CandidateFilenames);
        if (!ValidateUnloadedIndex(FirstAsset, Registry, OutError)
            || !ValidateUnloadedIndex(SecondAsset, Registry, OutError)
            || !ValidateUnloadedAsset(DecoyAsset, Registry, OutError))
        {
            return false;
        }
        return true;
    }

    FSalResolvedTarget Target() const
    {
        FSalResolvedTarget Result;
        Result.Kind = ESalTargetKind::Blueprint;
        Result.Alias = TEXT("source");
        Result.AssetPath = Source != nullptr
            ? Source->GetPathName()
            : FString();
        Result.Object = Source;
        Result.Package = Source != nullptr
            ? Source->GetOutermost()
            : nullptr;
        Result.Blueprint = Source;
        Result.Class = Source != nullptr
            ? Source->GeneratedClass.Get()
            : nullptr;
        Result.Interfaces = {
            FName(TEXT("asset")),
            FName(TEXT("blueprint"))};
        return Result;
    }

    FSalQuery ProjectQuery() const
    {
        TSharedRef<FJsonObject> Subject = MakeShared<FJsonObject>();
        Subject->SetStringField(TEXT("kind"), TEXT("variable"));
        Subject->SetStringField(
            TEXT("id"),
            ReferenceGuidText(VariableId));

        FSalQuery Query;
        Query.Alias = TEXT("source");
        Query.Operation = MakeShared<FJsonObject>();
        Query.Operation->SetStringField(TEXT("kind"), TEXT("references"));
        Query.Operation->SetObjectField(TEXT("target"), Subject);
        Query.Operation->SetStringField(TEXT("scope"), TEXT("project"));
        Query.PageLimit = 1;
        return Query;
    }

    TSet<FName> UnloadedPackageNames() const
    {
        return {
            FName(*FirstAsset.PackageName),
            FName(*SecondAsset.PackageName),
            FName(*DecoyAsset.PackageName)};
    }

    TArray<FString> ExpectedPaths() const
    {
        return {
            FirstAsset.ObjectPath,
            SecondAsset.ObjectPath};
    }

    TArray<FString> ExpectedNodeIds() const
    {
        return {
            FirstAsset.NodeId,
            SecondAsset.NodeId};
    }

    bool AreCandidatesStillUnloaded() const
    {
        return IsUnloaded(FirstAsset)
            && IsUnloaded(SecondAsset)
            && IsUnloaded(DecoyAsset);
    }

    bool Cleanup(FString& OutError)
    {
        return CleanupInternal(OutError);
    }

private:
    static void PreparePackageForCollection(UPackage* Package)
    {
        if (Package == nullptr)
        {
            return;
        }
        Package->SetDirtyFlag(false);
        Package->ClearFlags(RF_Public | RF_Standalone);
        ForEachObjectWithPackage(
            Package,
            [](UObject* Inner)
            {
                Inner->ClearFlags(RF_Public | RF_Standalone);
                return true;
            },
            true);
    }

    static void UnregisterLoadedAsset(
        UObject* Object,
        FSavedBlueprint& Asset)
    {
        if (Object == nullptr)
        {
            return;
        }
        if (Asset.bRegisteredInMemory)
        {
            FAssetRegistryModule::AssetDeleted(Object);
            Asset.bRegisteredInMemory = false;
        }
        Object->ClearFlags(RF_Public | RF_Standalone);
    }

    UBlueprint* CreateBlueprint(
        const FString& AssetName,
        UClass* ParentClass,
        FSavedBlueprint& OutAsset,
        FString& OutError)
    {
        OutAsset.PackageName = RootPackagePath + TEXT("/") + AssetName;
        OutAsset.ObjectPath = OutAsset.PackageName + TEXT(".") + AssetName;
        OutAsset.Filename = FPackageName::LongPackageNameToFilename(
            OutAsset.PackageName,
            FPackageName::GetAssetPackageExtension());
        IFileManager::Get().MakeDirectory(
            *FPaths::GetPath(OutAsset.Filename),
            true);

        UPackage* Package = CreatePackage(*OutAsset.PackageName);
        UBlueprint* Blueprint = Package != nullptr
            ? FKismetEditorUtilities::CreateBlueprint(
                ParentClass,
                Package,
                FName(*AssetName),
                BPTYPE_Normal,
                UBlueprint::StaticClass(),
                UBlueprintGeneratedClass::StaticClass(),
                NAME_None)
            : nullptr;
        if (Blueprint == nullptr)
        {
            OutError = TEXT("UE failed to create the Blueprint fixture ")
                + OutAsset.ObjectPath;
            return nullptr;
        }
        FAssetRegistryModule::AssetCreated(Blueprint);
        OutAsset.bRegisteredInMemory = true;
        Blueprint->GetOutermost()->SetDirtyFlag(false);
        return Blueprint;
    }

    UBlueprint* CreateReferenceCandidate(
        const FString& AssetName,
        FSavedBlueprint& OutAsset,
        FString& OutError)
    {
        UBlueprint* Candidate = CreateBlueprint(
            AssetName,
            Source != nullptr ? Source->GeneratedClass.Get() : nullptr,
            OutAsset,
            OutError);
        if (Candidate == nullptr)
        {
            return nullptr;
        }

        UEdGraph* EventGraph =
            FBlueprintEditorUtils::FindEventGraph(Candidate);
        UK2Node_VariableGet* VariableNode =
            EventGraph != nullptr
            ? FEdGraphSchemaAction_K2NewNode::SpawnNode<UK2Node_VariableGet>(
                EventGraph,
                FVector2D(320.0, 160.0),
                EK2NewNodeFlags::None,
                [this](UK2Node_VariableGet* Node)
                {
                    Node->VariableReference.SetExternalMember(
                        VariableName,
                        Source->GeneratedClass,
                        VariableId);
                })
            : nullptr;
        if (VariableNode == nullptr
            || !VariableNode->NodeGuid.IsValid())
        {
            OutError = TEXT("UE failed to create the indexed variable reference Node in ")
                + OutAsset.ObjectPath;
            return nullptr;
        }
        FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Candidate);
        FKismetEditorUtilities::CompileBlueprint(Candidate);
        if (Candidate->Status == BS_Error
            || VariableNode->VariableReference.GetMemberGuid() != VariableId)
        {
            OutError = TEXT("The candidate Blueprint did not preserve the exact source Variable GUID.");
            return nullptr;
        }
        OutAsset.NodeId =
            ReferenceGuidText(VariableNode->NodeGuid);
        return Candidate;
    }

    bool SaveBlueprint(
        UBlueprint* Blueprint,
        const FSavedBlueprint& Asset,
        FString& OutError)
    {
        if (Blueprint == nullptr)
        {
            OutError = TEXT("Cannot save a null Blueprint fixture.");
            return false;
        }
        UPackage* Package = Blueprint->GetOutermost();
        Package->SetDirtyFlag(true);
        Package->FullyLoad();
        FSavePackageArgs SaveArgs;
        SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
        SaveArgs.Error = GLog;
        if (!UPackage::SavePackage(
                Package,
                Blueprint,
                *Asset.Filename,
                SaveArgs))
        {
            OutError = TEXT("UE failed to save Blueprint fixture ")
                + Asset.ObjectPath;
            return false;
        }
        Package->SetDirtyFlag(false);
        return true;
    }

    static bool IsUnloaded(const FSavedBlueprint& Asset)
    {
        return FindPackage(nullptr, *Asset.PackageName) == nullptr
            && FindObject<UObject>(nullptr, *Asset.ObjectPath) == nullptr;
    }

    static bool ValidateUnloadedAsset(
        const FSavedBlueprint& Asset,
        IAssetRegistry& Registry,
        FString& OutError)
    {
        if (!IsUnloaded(Asset))
        {
            OutError = TEXT("Fixture package remained loaded before the project query: ")
                + Asset.PackageName;
            return false;
        }
        const FAssetData Data = Registry.GetAssetByObjectPath(
            FSoftObjectPath(Asset.ObjectPath),
            true);
        if (!Data.IsValid()
            || Data.FastGetAsset(false) != nullptr)
        {
            OutError = TEXT("Asset Registry did not retain an unloaded on-disk fixture: ")
                + Asset.ObjectPath;
            return false;
        }
        return true;
    }

    static bool ValidateUnloadedIndex(
        const FSavedBlueprint& Asset,
        IAssetRegistry& Registry,
        FString& OutError)
    {
        if (!ValidateUnloadedAsset(Asset, Registry, OutError))
        {
            return false;
        }
        const FAssetData Data = Registry.GetAssetByObjectPath(
            FSoftObjectPath(Asset.ObjectPath),
            true);
        const FAssetDataTagMapSharedView::FFindTagResult FiB =
            Data.TagsAndValues.FindTag(
                FBlueprintTags::FindInBlueprintsData);
        if (!FiB.IsSet() || FiB.GetValue().IsEmpty())
        {
            OutError = TEXT("Saved candidate has no latest UE 5.7 FiB index: ")
                + Asset.ObjectPath;
            return false;
        }
        return true;
    }

    TArray<FString> AllFilenames() const
    {
        TArray<FString> Filenames;
        for (const FString* Filename : {
                 &SourceAsset.Filename,
                 &FirstAsset.Filename,
                 &SecondAsset.Filename,
                 &DecoyAsset.Filename})
        {
            if (Filename != nullptr && !Filename->IsEmpty())
            {
                Filenames.Add(*Filename);
            }
        }
        return Filenames;
    }

    bool CleanupInternal(FString& OutError)
    {
        OutError.Reset();
        if (bCleaned)
        {
            return true;
        }
        bCleaned = true;

        TArray<FString> PackageNames;
        for (FSavedBlueprint* Asset : {
                 &SourceAsset,
                 &FirstAsset,
                 &SecondAsset,
                 &DecoyAsset})
        {
            if (!Asset->PackageName.IsEmpty())
            {
                PackageNames.Add(Asset->PackageName);
            }
            UObject* Object = !Asset->ObjectPath.IsEmpty()
                ? FindObject<UObject>(nullptr, *Asset->ObjectPath)
                : nullptr;
            UnregisterLoadedAsset(Object, *Asset);
            if (!Asset->PackageName.IsEmpty())
            {
                if (UPackage* Package =
                        FindPackage(nullptr, *Asset->PackageName))
                {
                    PreparePackageForCollection(Package);
                }
            }
        }
        Source = nullptr;
        CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);
        for (const FString& PackageName : PackageNames)
        {
            if (FindPackage(nullptr, *PackageName) != nullptr)
            {
                if (!OutError.IsEmpty())
                {
                    OutError += TEXT(" ");
                }
                OutError +=
                    TEXT("Fixture package remained loaded: ")
                    + PackageName;
            }
        }

        const TArray<FString> Filenames = AllFilenames();
        for (const FString& Filename : Filenames)
        {
            if (IFileManager::Get().FileExists(*Filename)
                && !IFileManager::Get().Delete(
                    *Filename,
                    false,
                    true,
                    true))
            {
                if (!OutError.IsEmpty())
                {
                    OutError += TEXT(" ");
                }
                OutError +=
                    TEXT("Fixture file could not be deleted: ")
                    + Filename;
            }
        }
        if (!RootPackagePath.IsEmpty())
        {
            const FString Directory =
                FPackageName::LongPackageNameToFilename(RootPackagePath);
            if (IFileManager::Get().DirectoryExists(*Directory))
            {
                IFileManager::Get().DeleteDirectory(
                    *Directory,
                    false,
                    true);
            }
            if (IFileManager::Get().DirectoryExists(*Directory))
            {
                if (!OutError.IsEmpty())
                {
                    OutError += TEXT(" ");
                }
                OutError +=
                    TEXT("Fixture directory could not be deleted: ")
                    + Directory;
            }
        }
        if (FAssetRegistryModule* Module =
                FModuleManager::GetModulePtr<FAssetRegistryModule>(
                    TEXT("AssetRegistry")))
        {
            IAssetRegistry& Registry = Module->Get();
            Registry.ScanModifiedAssetFiles(Filenames);
            for (const FSavedBlueprint* Asset : {
                     &SourceAsset,
                     &FirstAsset,
                     &SecondAsset,
                     &DecoyAsset})
            {
                if (!Asset->ObjectPath.IsEmpty()
                    && Registry.GetAssetByObjectPath(
                        FSoftObjectPath(Asset->ObjectPath),
                        true).IsValid())
                {
                    if (!OutError.IsEmpty())
                    {
                        OutError += TEXT(" ");
                    }
                    OutError +=
                        TEXT("Asset Registry retained fixture: ")
                        + Asset->ObjectPath;
                }
            }
        }
        return OutError.IsEmpty();
    }

    FString Token;
    FString RootPackagePath;
    FName VariableName;
    FGuid VariableId;
    UBlueprint* Source = nullptr;
    FSavedBlueprint SourceAsset;
    FSavedBlueprint FirstAsset;
    FSavedBlueprint SecondAsset;
    FSavedBlueprint DecoyAsset;
    bool bCleaned = false;
};

class FScopedPackageLoadObserver
{
public:
    explicit FScopedPackageLoadObserver(
        const TSet<FName>& InObservedPackages)
        : ObservedPackages(InObservedPackages)
    {
        // Observe both asset creation from a linker and completed package
        // loads. The post-query FindPackage checks below independently verify
        // that no observed package remains resident.
        AssetLoadedHandle =
            FCoreUObjectDelegates::OnAssetLoaded.AddRaw(
                this,
                &FScopedPackageLoadObserver::OnAssetLoaded);
        EndLoadHandle =
            FCoreUObjectDelegates::OnEndLoadPackage.AddRaw(
                this,
                &FScopedPackageLoadObserver::OnEndLoadPackage);
    }

    ~FScopedPackageLoadObserver()
    {
        FCoreUObjectDelegates::OnAssetLoaded.Remove(
            AssetLoadedHandle);
        FCoreUObjectDelegates::OnEndLoadPackage.Remove(
            EndLoadHandle);
    }

    bool SawObservedLoad() const
    {
        return !LoadedPackages.IsEmpty();
    }

    FString LoadedPackageList() const
    {
        TArray<FString> Names;
        for (const FName Name : LoadedPackages)
        {
            Names.Add(Name.ToString());
        }
        Names.Sort();
        return FString::Join(Names, TEXT(", "));
    }

private:
    void NotePackage(const UPackage* Package)
    {
        if (Package != nullptr
            && ObservedPackages.Contains(Package->GetFName()))
        {
            LoadedPackages.Add(Package->GetFName());
        }
    }

    void OnAssetLoaded(UObject* Asset)
    {
        NotePackage(
            Asset != nullptr
                ? Asset->GetOutermost()
                : nullptr);
    }

    void OnEndLoadPackage(
        const FEndLoadPackageContext& Context)
    {
        for (const UPackage* Package : Context.LoadedPackages)
        {
            NotePackage(Package);
        }
    }

    TSet<FName> ObservedPackages;
    TSet<FName> LoadedPackages;
    FDelegateHandle AssetLoadedHandle;
    FDelegateHandle EndLoadHandle;
};

bool HasReferenceDiagnostic(
    const TSharedPtr<FJsonObject>& Result,
    const FString& Code,
    const FString& OptionalSeverity = FString())
{
    const TArray<TSharedPtr<FJsonValue>>* Diagnostics = nullptr;
    if (!Result.IsValid()
        || !Result->TryGetArrayField(
            TEXT("diagnostics"),
            Diagnostics)
        || Diagnostics == nullptr)
    {
        return false;
    }
    for (const TSharedPtr<FJsonValue>& Value : *Diagnostics)
    {
        const TSharedPtr<FJsonObject>* Diagnostic = nullptr;
        FString ActualCode;
        FString Severity;
        if (Value.IsValid()
            && Value->TryGetObject(Diagnostic)
            && Diagnostic != nullptr
            && (*Diagnostic)->TryGetStringField(
                TEXT("code"),
                ActualCode)
            && ActualCode == Code
            && (OptionalSeverity.IsEmpty()
                || ((*Diagnostic)->TryGetStringField(
                        TEXT("severity"),
                        Severity)
                    && Severity == OptionalSeverity)))
        {
            return true;
        }
    }
    return false;
}

bool HasReferenceErrorDiagnostic(
    const TSharedPtr<FJsonObject>& Result)
{
    const TArray<TSharedPtr<FJsonValue>>* Diagnostics = nullptr;
    if (!Result.IsValid()
        || !Result->TryGetArrayField(
            TEXT("diagnostics"),
            Diagnostics)
        || Diagnostics == nullptr)
    {
        return false;
    }
    for (const TSharedPtr<FJsonValue>& Value : *Diagnostics)
    {
        const TSharedPtr<FJsonObject>* Diagnostic = nullptr;
        FString Severity;
        if (Value.IsValid()
            && Value->TryGetObject(Diagnostic)
            && Diagnostic != nullptr
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

TArray<FString> ReferenceDiagnosticMessages(
    const TSharedPtr<FJsonObject>& Result)
{
    TArray<FString> Messages;
    const TArray<TSharedPtr<FJsonValue>>* Diagnostics = nullptr;
    if (!Result.IsValid()
        || !Result->TryGetArrayField(
            TEXT("diagnostics"),
            Diagnostics)
        || Diagnostics == nullptr)
    {
        return Messages;
    }
    for (const TSharedPtr<FJsonValue>& Value : *Diagnostics)
    {
        const TSharedPtr<FJsonObject>* Diagnostic = nullptr;
        FString Code;
        FString Message;
        if (Value.IsValid()
            && Value->TryGetObject(Diagnostic)
            && Diagnostic != nullptr)
        {
            (*Diagnostic)->TryGetStringField(TEXT("code"), Code);
            (*Diagnostic)->TryGetStringField(
                TEXT("message"),
                Message);
            Messages.Add(Code + TEXT(": ") + Message);
        }
    }
    return Messages;
}

FString ReferenceNextCursor(
    const TSharedPtr<FJsonObject>& Result)
{
    const TSharedPtr<FJsonObject>* Page = nullptr;
    FString Cursor;
    if (Result.IsValid()
        && Result->TryGetObjectField(TEXT("page"), Page)
        && Page != nullptr)
    {
        (*Page)->TryGetStringField(TEXT("next"), Cursor);
    }
    return Cursor;
}

struct FReferencePageFacts
{
    TArray<FString> BlueprintAssetPaths;
    TArray<FString> Comments;
    int32 BlueprintLocatorCount = 0;
    bool bAllBlueprintsHaveExactAsset = true;
};

FReferencePageFacts ReadReferencePageFacts(
    const TSharedPtr<FJsonObject>& Result)
{
    FReferencePageFacts Facts;
    const TSharedPtr<FJsonObject>* Object = nullptr;
    const TArray<TSharedPtr<FJsonValue>>* Statements = nullptr;
    if (!Result.IsValid()
        || !Result->TryGetObjectField(TEXT("object"), Object)
        || Object == nullptr
        || !(*Object)->TryGetArrayField(
            TEXT("statements"),
            Statements)
        || Statements == nullptr)
    {
        return Facts;
    }

    TMap<FString, FString> AssetPathByAlias;
    for (const TSharedPtr<FJsonValue>& Value : *Statements)
    {
        const TSharedPtr<FJsonObject>* Statement = nullptr;
        if (!Value.IsValid()
            || !Value->TryGetObject(Statement)
            || Statement == nullptr)
        {
            continue;
        }

        FString StatementKind;
        FString Comment;
        if ((*Statement)->TryGetStringField(
                TEXT("kind"),
                StatementKind)
            && StatementKind == TEXT("comment")
            && (*Statement)->TryGetStringField(
                TEXT("text"),
                Comment))
        {
            Facts.Comments.Add(Comment);
        }

        const TSharedPtr<FJsonObject>* BindingTarget = nullptr;
        const TSharedPtr<FJsonObject>* Call = nullptr;
        const TSharedPtr<FJsonObject>* Args = nullptr;
        FString TargetKind;
        FString Alias;
        FString ValueKind;
        FString Callee;
        if (!(*Statement)->TryGetObjectField(
                TEXT("target"),
                BindingTarget)
            || BindingTarget == nullptr
            || !(*BindingTarget)->TryGetStringField(
                TEXT("kind"),
                TargetKind)
            || TargetKind != TEXT("local")
            || !(*BindingTarget)->TryGetStringField(
                TEXT("name"),
                Alias)
            || !(*Statement)->TryGetObjectField(
                TEXT("value"),
                Call)
            || Call == nullptr
            || !(*Call)->TryGetStringField(
                TEXT("kind"),
                ValueKind)
            || ValueKind != TEXT("call")
            || !(*Call)->TryGetStringField(
                TEXT("callee"),
                Callee)
            || !(*Call)->TryGetObjectField(
                TEXT("args"),
                Args)
            || Args == nullptr)
        {
            continue;
        }
        if (Callee == TEXT("asset"))
        {
            FString Path;
            if ((*Args)->TryGetStringField(TEXT("path"), Path))
            {
                AssetPathByAlias.Add(Alias, Path);
            }
        }
    }

    for (const TSharedPtr<FJsonValue>& Value : *Statements)
    {
        const TSharedPtr<FJsonObject>* Statement = nullptr;
        const TSharedPtr<FJsonObject>* Call = nullptr;
        const TSharedPtr<FJsonObject>* Args = nullptr;
        FString ValueKind;
        FString Callee;
        if (!Value.IsValid()
            || !Value->TryGetObject(Statement)
            || Statement == nullptr
            || !(*Statement)->TryGetObjectField(
                TEXT("value"),
                Call)
            || Call == nullptr
            || !(*Call)->TryGetStringField(
                TEXT("kind"),
                ValueKind)
            || ValueKind != TEXT("call")
            || !(*Call)->TryGetStringField(
                TEXT("callee"),
                Callee)
            || Callee != TEXT("blueprint")
            || !(*Call)->TryGetObjectField(
                TEXT("args"),
                Args)
            || Args == nullptr)
        {
            continue;
        }
        ++Facts.BlueprintLocatorCount;
        const TSharedPtr<FJsonObject>* AssetReference = nullptr;
        FString RefKind;
        FString AssetAlias;
        if (!(*Args)->TryGetObjectField(
                TEXT("asset"),
                AssetReference)
            || AssetReference == nullptr
            || !(*AssetReference)->TryGetStringField(
                TEXT("kind"),
                RefKind)
            || RefKind != TEXT("local")
            || !(*AssetReference)->TryGetStringField(
                TEXT("name"),
                AssetAlias)
            || !AssetPathByAlias.Contains(AssetAlias))
        {
            Facts.bAllBlueprintsHaveExactAsset = false;
            continue;
        }
        Facts.BlueprintAssetPaths.Add(
            AssetPathByAlias.FindChecked(AssetAlias));
    }
    return Facts;
}

struct FReferenceScanPages
{
    TArray<FString> Paths;
    TArray<FString> Comments;
    TArray<FString> Cursors;
    TArray<FString> Diagnostics;
};

bool ReadAllProjectPages(
    FAutomationTestBase& Test,
    const FSalQuery& FirstQuery,
    const FSalResolvedTarget& Target,
    FReferenceScanPages& Out)
{
    FString Cursor;
    TSet<FString> SeenCursors;
    for (int32 PageIndex = 0; PageIndex < 64; ++PageIndex)
    {
        FSalQuery Query = FirstQuery;
        Query.PageAfter = Cursor;
        const TSharedPtr<FJsonObject> Result =
            FSalReferenceInterface::Query(Query, Target);
        if (!Test.TestNotNull(
                TEXT("Reference page returns a result"),
                Result.Get()))
        {
            return false;
        }
        if (!Test.TestFalse(
                TEXT("Reference page has no error diagnostic"),
                HasReferenceErrorDiagnostic(Result)))
        {
            return false;
        }

        const FReferencePageFacts Facts =
            ReadReferencePageFacts(Result);
        Test.TestTrue(
            TEXT("Every Blueprint result is tied to an exact Asset locator"),
            Facts.bAllBlueprintsHaveExactAsset);
        Test.TestEqual(
            TEXT("Every Blueprint locator resolves through one returned Asset path"),
            Facts.BlueprintLocatorCount,
            Facts.BlueprintAssetPaths.Num());
        Test.TestTrue(
            TEXT("page limit bounds emitted Reference records"),
            Facts.BlueprintAssetPaths.Num() <= FirstQuery.PageLimit);
        Out.Paths.Append(Facts.BlueprintAssetPaths);
        Out.Comments.Append(Facts.Comments);
        Out.Diagnostics.Append(
            ReferenceDiagnosticMessages(Result));

        const FString Next = ReferenceNextCursor(Result);
        if (Next.IsEmpty())
        {
            return true;
        }
        if (!Test.TestFalse(
                TEXT("Each continuation cursor is single-use and advances"),
                SeenCursors.Contains(Next)))
        {
            return false;
        }
        SeenCursors.Add(Next);
        Out.Cursors.Add(Next);
        Cursor = Next;
    }
    Test.AddError(TEXT("Reference pagination did not terminate within 64 pages."));
    return false;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FSalProjectReferenceZeroLoadReleaseGateTest,
    "Loomle.Sal.Reference.ProjectZeroLoadReleaseGate",
    EAutomationTestFlags::EditorContext
        | EAutomationTestFlags::EngineFilter)

bool FSalProjectReferenceZeroLoadReleaseGateTest::RunTest(
    const FString& Parameters)
{
    FScopedProjectReferenceFixture Fixture;
    FString FixtureError;
    const bool bFixtureBuilt = Fixture.Build(FixtureError);
    TestTrue(
        TEXT("Project Reference fixture is ready"),
        bFixtureBuilt);
    if (!bFixtureBuilt)
    {
        AddError(
            FixtureError.IsEmpty()
                ? TEXT("Project Reference fixture setup failed without an error.")
                : FixtureError);
        return false;
    }

    TestTrue(
        TEXT("FiB candidates and the unrelated package start unloaded"),
        Fixture.AreCandidatesStillUnloaded());
    FScopedPackageLoadObserver LoadObserver(
        Fixture.UnloadedPackageNames());

    FReferenceScanPages FirstScan;
    if (!ReadAllProjectPages(
            *this,
            Fixture.ProjectQuery(),
            Fixture.Target(),
            FirstScan))
    {
        return false;
    }
    const TArray<FString> ExpectedPaths =
        Fixture.ExpectedPaths();
    if (FirstScan.Paths.Num() != ExpectedPaths.Num()
        && !FirstScan.Diagnostics.IsEmpty())
    {
        AddInfo(
            TEXT("Reference scan diagnostics: ")
            + FString::Join(
                FirstScan.Diagnostics,
                TEXT(" | ")));
    }
    TestEqual(
        TEXT("Project FiB scan returns exactly two candidates"),
        FirstScan.Paths.Num(),
        ExpectedPaths.Num());
    for (int32 Index = 0;
         Index < FMath::Min(
             FirstScan.Paths.Num(),
             ExpectedPaths.Num());
         ++Index)
    {
        TestEqual(
            *FString::Printf(
                TEXT("Candidate %d is returned in Registry package order"),
                Index),
            FirstScan.Paths[Index],
            ExpectedPaths[Index]);
    }
    TestTrue(
        TEXT("Two results with page limit one require a continuation cursor"),
        !FirstScan.Cursors.IsEmpty());
    for (const FString& NodeId : Fixture.ExpectedNodeIds())
    {
        TestTrue(
            *FString::Printf(
                TEXT("FiB result retains indexed NodeGuid provenance %s"),
                *NodeId),
            FirstScan.Comments.ContainsByPredicate(
                [&NodeId](const FString& Comment)
                {
                    return Comment.Contains(
                        TEXT("indexed NodeGuid=") + NodeId);
                }));
    }

    FReferenceScanPages SecondScan;
    if (!ReadAllProjectPages(
            *this,
            Fixture.ProjectQuery(),
            Fixture.Target(),
            SecondScan))
    {
        return false;
    }
    TestEqual(
        TEXT("Repeated project scan returns the same result count"),
        SecondScan.Paths.Num(),
        FirstScan.Paths.Num());
    for (int32 Index = 0;
         Index < FMath::Min(
             SecondScan.Paths.Num(),
             FirstScan.Paths.Num());
         ++Index)
    {
        TestEqual(
            *FString::Printf(
                TEXT("Repeated project scan preserves item order at %d"),
                Index),
            SecondScan.Paths[Index],
            FirstScan.Paths[Index]);
    }

    const TSharedRef<
        Loomle::Runtime::FRequestCancellationState,
        ESPMode::ThreadSafe>
        Cancellation =
            MakeShared<
                Loomle::Runtime::FRequestCancellationState,
                ESPMode::ThreadSafe>();
    Cancellation->Cancel();
    TSharedPtr<FJsonObject> CancelledResult;
    {
        Loomle::Runtime::FScopedRequestCancellation Scope(
            Cancellation);
        CancelledResult = FSalReferenceInterface::Query(
            Fixture.ProjectQuery(),
            Fixture.Target());
    }
    TestTrue(
        TEXT("Cancelled project scan fails closed with the registered diagnostic"),
        HasReferenceDiagnostic(
            CancelledResult,
            TEXT("runtime.request_cancelled"),
            TEXT("error")));
    TestTrue(
        TEXT("Cancelled project scan emits no authored locator"),
        ReadReferencePageFacts(CancelledResult)
            .BlueprintAssetPaths.IsEmpty());

    TestTrue(
        *FString::Printf(
            TEXT("Project scans did not load any observed package: %s"),
            *LoadObserver.LoadedPackageList()),
        !LoadObserver.SawObservedLoad());
    TestTrue(
        TEXT("FiB candidates and unrelated asset remain unloaded after every page"),
        Fixture.AreCandidatesStillUnloaded());
    FString CleanupError;
    const bool bCleaned = Fixture.Cleanup(CleanupError);
    TestTrue(
        *FString::Printf(
            TEXT("Reference fixture unregisters and unloads without resetting editor transactions: %s"),
            *CleanupError),
        bCleaned);
    return true;
}

#endif
