// Copyright 2026 Loomle contributors.

#if WITH_DEV_AUTOMATION_TESTS

#include "EditorControl/EditorControlService.h"
#include "LoomleTestObjectIteration.h"
#include "LoomleTestEditorState.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "EdGraph/EdGraph.h"
#include "Editor.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "GameFramework/Actor.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/App.h"
#include "Misc/AutomationTest.h"
#include "Misc/Guid.h"
#include "Sal/SalJson.h"
#include "Settings/EditorStyleSettings.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "UObject/GarbageCollection.h"
#include "UObject/GCObject.h"
#include "UObject/Package.h"
#include "UObject/UObjectHash.h"

namespace
{
using namespace Loomle::EditorControl;

FString GuidText(const FGuid& Guid)
{
    return Guid.ToString(EGuidFormats::DigitsWithHyphensLower);
}

TSharedPtr<FJsonObject> BlueprintTarget(UBlueprint* Blueprint)
{
    TSharedPtr<FJsonObject> Target = MakeShared<FJsonObject>();
    Target->SetStringField(TEXT("kind"), TEXT("target"));
    Target->SetStringField(TEXT("domain"), TEXT("blueprint"));
    Target->SetStringField(TEXT("asset"), Blueprint->GetPathName());
    Target->SetStringField(
        TEXT("id"),
        GuidText(Blueprint->GetBlueprintGuid()));
    return Target;
}

TSharedPtr<FJsonObject> GraphTarget(
    UBlueprint* Blueprint,
    UEdGraph* Graph)
{
    TSharedPtr<FJsonObject> Target = MakeShared<FJsonObject>();
    Target->SetStringField(TEXT("kind"), TEXT("target"));
    Target->SetStringField(TEXT("domain"), TEXT("graph"));
    Target->SetStringField(TEXT("asset"), Blueprint->GetPathName());
    Target->SetStringField(
        TEXT("blueprintId"),
        GuidText(Blueprint->GetBlueprintGuid()));
    Target->SetStringField(
        TEXT("id"),
        GuidText(Graph->GraphGuid));
    return Target;
}

bool ReadStatus(
    const TSharedPtr<FJsonObject>& Result,
    FString& OutOperation,
    FString& OutStatus,
    TSharedPtr<FJsonObject>& OutSubject)
{
    OutOperation.Reset();
    OutStatus.Reset();
    OutSubject.Reset();
    const TSharedPtr<FJsonObject>* Subject = nullptr;
    const TSharedPtr<FJsonObject>* Outcome = nullptr;
    return Result.IsValid()
        && Result->TryGetObjectField(TEXT("subject"), Subject)
        && Subject != nullptr
        && (OutSubject = *Subject).IsValid()
        && Result->TryGetObjectField(TEXT("outcome"), Outcome)
        && Outcome != nullptr
        && (*Outcome).IsValid()
        && (*Outcome)->TryGetStringField(
            TEXT("operation"),
            OutOperation)
        && (*Outcome)->TryGetStringField(
            TEXT("status"),
            OutStatus);
}

bool SubjectMatchesTarget(
    const TSharedPtr<FJsonObject>& Subject,
    const TSharedPtr<FJsonObject>& ExpectedTarget,
    FString& OutAlias)
{
    OutAlias.Reset();
    const TSharedPtr<FJsonObject>* Binding = nullptr;
    const TSharedPtr<FJsonObject>* ActualTarget = nullptr;
    if (!Subject.IsValid()
        || !ExpectedTarget.IsValid()
        || !Subject->TryGetObjectField(TEXT("target"), Binding)
        || Binding == nullptr
        || !(*Binding).IsValid()
        || !(*Binding)->TryGetStringField(TEXT("alias"), OutAlias)
        || !(*Binding)->TryGetObjectField(
            TEXT("target"),
            ActualTarget)
        || ActualTarget == nullptr
        || !(*ActualTarget).IsValid()
        || (*ActualTarget)->Values.Num()
            != ExpectedTarget->Values.Num())
    {
        return false;
    }
    for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair
        : ExpectedTarget->Values)
    {
        FString ExpectedValue;
        FString ActualValue;
        if (!Pair.Value.IsValid()
            || !Pair.Value->TryGetString(ExpectedValue)
            || !(*ActualTarget)->TryGetStringField(
                Pair.Key,
                ActualValue)
            || ActualValue != ExpectedValue)
        {
            return false;
        }
    }
    return true;
}

bool UnloadFixturePackage(UPackage* Package, FString& OutError)
{
    OutError.Reset();
    if (Package == nullptr)
    {
        return true;
    }
    const FString PackageName = Package->GetName();
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
    CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);
    if (FindPackage(nullptr, *PackageName) != nullptr)
    {
        OutError = TEXT("Fixture package remained loaded: ")
            + PackageName;
        return false;
    }
    return true;
}

class FEditorControlNativeFixture : public FGCObject
{
public:
    virtual ~FEditorControlNativeFixture() override
    {
        FString Ignored;
        Cleanup(Ignored);
    }

    virtual void AddReferencedObjects(
        FReferenceCollector& Collector) override
    {
        Collector.AddReferencedObject(Package);
        Collector.AddReferencedObject(Blueprint);
        Collector.AddReferencedObject(Graph);
    }

    virtual FString GetReferencerName() const override
    {
        return TEXT("Loomle.EditorControl.NativeFixture");
    }

    bool Initialize(FString& OutError)
    {
        OutError.Reset();
        if (GEditor == nullptr
            || GEditor->IsPlaySessionInProgress())
        {
            OutError = TEXT(
                "Editor control requires an idle Unreal Editor.");
            return false;
        }
        if (!Transactions.Initialize())
        {
            OutError = TEXT(
                "Could not install an isolated transaction buffer.");
            return false;
        }

        StyleSettings = GetMutableDefault<UEditorStyleSettings>();
        if (StyleSettings == nullptr)
        {
            OutError = TEXT("Editor Style settings are unavailable.");
            return false;
        }
        PreviousOpenLocation = StyleSettings->AssetEditorOpenLocation;
        StyleSettings->AssetEditorOpenLocation =
            EAssetEditorOpenLocation::NewWindow;
        bOpenLocationOverridden = true;

        const FString Token =
            FGuid::NewGuid().ToString(EGuidFormats::Digits);
        Package = CreatePackage(*FString::Printf(
            TEXT("/Game/LoomleTests/EditorControl_%s"),
            *Token));
        Blueprint = FKismetEditorUtilities::CreateBlueprint(
            AActor::StaticClass(),
            Package,
            *FString::Printf(
                TEXT("BP_EditorControl_%s"),
                *Token),
            BPTYPE_Normal,
            UBlueprint::StaticClass(),
            UBlueprintGeneratedClass::StaticClass(),
            NAME_None);
        Graph = Blueprint != nullptr
            ? FBlueprintEditorUtils::FindEventGraph(Blueprint)
            : nullptr;
        if (Package == nullptr
            || Blueprint == nullptr
            || Graph == nullptr
            || !Blueprint->GetBlueprintGuid().IsValid()
            || !Graph->GraphGuid.IsValid())
        {
            OutError = TEXT(
                "Could not create canonical Blueprint Graph fixtures.");
            return false;
        }

        Blueprint->bIsNewlyCreated = false;
        Blueprint->bForceFullEditor = true;
        Blueprint->LastEditedDocuments.Reset();
        Blueprint->LastEditedDocuments.Add(Graph.Get());
        FAssetRegistryModule::AssetCreated(Blueprint);
        bAssetRegistered = true;
        Package->SetDirtyFlag(false);
        return true;
    }

    bool Update(FAutomationTestBase& Test)
    {
        if (bFinished)
        {
            return true;
        }

        switch (Stage)
        {
        case 0:
            if (!Expect(
                    Test,
                    TEXT("open"),
                    GraphTarget(Blueprint, Graph),
                    TEXT("opened")))
            {
                return FinishAfterFailure(Test);
            }
            break;
        case 1:
            if (!Expect(
                    Test,
                    TEXT("open"),
                    GraphTarget(Blueprint, Graph),
                    TEXT("already_focused")))
            {
                return FinishAfterFailure(Test);
            }
            break;
        case 2:
            if (!Expect(
                    Test,
                    TEXT("close"),
                    GraphTarget(Blueprint, Graph),
                    TEXT("closed")))
            {
                return FinishAfterFailure(Test);
            }
            break;
        case 3:
            if (!Expect(
                    Test,
                    TEXT("close"),
                    GraphTarget(Blueprint, Graph),
                    TEXT("already_closed")))
            {
                return FinishAfterFailure(Test);
            }
            break;
        case 4:
            if (!Expect(
                    Test,
                    TEXT("close"),
                    BlueprintTarget(Blueprint),
                    TEXT("closed")))
            {
                return FinishAfterFailure(Test);
            }
            break;
        case 5:
            if (!Expect(
                    Test,
                    TEXT("close"),
                    BlueprintTarget(Blueprint),
                    TEXT("already_closed")))
            {
                return FinishAfterFailure(Test);
            }
            break;
        case 6:
            if (!Expect(
                    Test,
                    TEXT("open"),
                    BlueprintTarget(Blueprint),
                    TEXT("opened")))
            {
                return FinishAfterFailure(Test);
            }
            break;
        case 7:
            if (!Expect(
                    Test,
                    TEXT("open"),
                    BlueprintTarget(Blueprint),
                    TEXT("already_focused")))
            {
                return FinishAfterFailure(Test);
            }
            break;
        case 8:
            if (!Expect(
                    Test,
                    TEXT("close"),
                    BlueprintTarget(Blueprint),
                    TEXT("closed")))
            {
                return FinishAfterFailure(Test);
            }
            break;
        default:
        {
            FString CleanupError;
            Test.TestTrue(
                *FString::Printf(
                    TEXT("Editor control fixture cleans up: %s"),
                    *CleanupError),
                Cleanup(CleanupError));
            bFinished = true;
            return true;
        }
        }
        ++Stage;
        return false;
    }

    bool Cleanup(FString& OutError)
    {
        OutError.Reset();
        if (bCleaned)
        {
            return true;
        }
        bCleaned = true;

        bool bEditorClosed = true;
        if (GEditor != nullptr && Blueprint != nullptr)
        {
            UAssetEditorSubsystem* Editors =
                GEditor->GetEditorSubsystem<UAssetEditorSubsystem>();
            if (Editors != nullptr)
            {
                if (Package != nullptr)
                {
                    Package->SetDirtyFlag(false);
                }
                Editors->CloseAllEditorsForAsset(Blueprint);
                bEditorClosed =
                    Editors->FindEditorsForAsset(Blueprint).IsEmpty();
            }
        }

        if (bOpenLocationOverridden && StyleSettings != nullptr)
        {
            StyleSettings->AssetEditorOpenLocation =
                PreviousOpenLocation;
        }
        StyleSettings = nullptr;
        bOpenLocationOverridden = false;

        if (bAssetRegistered && Blueprint != nullptr)
        {
            FAssetRegistryModule::AssetDeleted(Blueprint);
        }
        bAssetRegistered = false;

        UPackage* PackageToUnload = Package;
        if (Blueprint != nullptr)
        {
            Blueprint->ClearFlags(RF_Public | RF_Standalone);
        }
        Package = nullptr;
        Blueprint = nullptr;
        Graph = nullptr;

        bool bPackageUnloaded = false;
        if (bEditorClosed)
        {
            bPackageUnloaded =
                UnloadFixturePackage(PackageToUnload, OutError);
        }
        else
        {
            OutError = TEXT(
                "The fixture Blueprint Editor remained open during cleanup.");
        }
        Transactions.Restore();
        return bEditorClosed && bPackageUnloaded;
    }

private:
    bool Expect(
        FAutomationTestBase& Test,
        const FString& Operation,
        const TSharedPtr<FJsonObject>& Target,
        const FString& ExpectedStatus)
    {
        TSharedPtr<FJsonObject> Arguments = MakeShared<FJsonObject>();
        Arguments->SetObjectField(TEXT("target"), Target);
        TSharedPtr<FJsonObject> DispatchError;
        const TSharedPtr<FJsonObject> Result =
            FEditorControlService::Execute(
                Operation,
                Arguments,
                DispatchError);
        if (DispatchError.IsValid())
        {
            FString Message;
            DispatchError->TryGetStringField(TEXT("message"), Message);
            Test.AddError(FString::Printf(
                TEXT("editor.%s returned a dispatch error: %s"),
                *Operation,
                *Message));
            return false;
        }

        FString ActualOperation;
        FString ActualStatus;
        TSharedPtr<FJsonObject> Subject;
        if (!ReadStatus(
                Result,
                ActualOperation,
                ActualStatus,
                Subject))
        {
            Test.AddError(FString::Printf(
                TEXT("editor.%s returned an invalid private wrapper."),
                *Operation));
            return false;
        }
        TSharedPtr<FJsonObject> ValidationError;
        Test.TestTrue(
            *FString::Printf(
                TEXT("editor.%s returns a valid SAL subject"),
                *Operation),
            Loomle::Sal::FSalJson::ValidateResult(
                Subject,
                ValidationError));
        FString Alias;
        const bool bSubjectMatches = SubjectMatchesTarget(
            Subject,
            Target,
            Alias);
        Test.TestTrue(
            *FString::Printf(
                TEXT("editor.%s retains the exact requested canonical Target"),
                *Operation),
            bSubjectMatches);
        Test.TestEqual(
            TEXT("Editor control uses its fixed SAL Target alias"),
            Alias,
            FString(TEXT("editorTarget")));
        Test.TestEqual(
            TEXT("Editor outcome preserves its operation"),
            ActualOperation,
            Operation);
        FString TargetDomain = TEXT("target");
        Target->TryGetStringField(TEXT("domain"), TargetDomain);
        Test.TestEqual(
            *FString::Printf(
                TEXT("editor.%s %s reaches %s"),
                *Operation,
                *TargetDomain,
                *ExpectedStatus),
            ActualStatus,
            ExpectedStatus);
        return bSubjectMatches
            && Alias == TEXT("editorTarget")
            && ActualOperation == Operation
            && ActualStatus == ExpectedStatus;
    }

    bool FinishAfterFailure(FAutomationTestBase& Test)
    {
        FString CleanupError;
        Test.TestTrue(
            *FString::Printf(
                TEXT("Failed Editor control fixture cleans up: %s"),
                *CleanupError),
            Cleanup(CleanupError));
        bFinished = true;
        return true;
    }

private:
    Loomle::Tests::FScopedIsolatedTransactor Transactions;
    TObjectPtr<UPackage> Package = nullptr;
    TObjectPtr<UBlueprint> Blueprint = nullptr;
    TObjectPtr<UEdGraph> Graph = nullptr;
    UEditorStyleSettings* StyleSettings = nullptr;
    EAssetEditorOpenLocation PreviousOpenLocation =
        EAssetEditorOpenLocation::Default;
    int32 Stage = 0;
    bool bOpenLocationOverridden = false;
    bool bAssetRegistered = false;
    bool bCleaned = false;
    bool bFinished = false;
};

class FRunEditorControlNativeFixture final
    : public IAutomationLatentCommand
{
public:
    FRunEditorControlNativeFixture(
        FAutomationTestBase* InTest,
        TSharedRef<FEditorControlNativeFixture> InFixture)
        : Test(InTest)
        , Fixture(MoveTemp(InFixture))
    {
    }

    virtual bool Update() override
    {
        return Fixture->Update(*Test);
    }

private:
    FAutomationTestBase* Test = nullptr;
    TSharedRef<FEditorControlNativeFixture> Fixture;
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FEditorControlNativeBlueprintGraphTest,
    "Loomle.EditorControl.NativeBlueprintGraph",
    EAutomationTestFlags::EditorContext
        | EAutomationTestFlags::EngineFilter)

bool FEditorControlNativeBlueprintGraphTest::RunTest(
    const FString& Parameters)
{
    if (!FApp::CanEverRender())
    {
        AddInfo(TEXT(
            "Native Blueprint Editor control requires a rendered Editor."));
        return true;
    }

    const TSharedRef<FEditorControlNativeFixture> Fixture =
        MakeShared<FEditorControlNativeFixture>();
    FString InitializeError;
    if (!TestTrue(
            *FString::Printf(
                TEXT("Editor control fixture initializes: %s"),
                *InitializeError),
            Fixture->Initialize(InitializeError)))
    {
        FString CleanupError;
        Fixture->Cleanup(CleanupError);
        return false;
    }

    ADD_LATENT_AUTOMATION_COMMAND(
        FRunEditorControlNativeFixture(
            this,
            Fixture));
    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
