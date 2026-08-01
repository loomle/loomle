// Copyright 2026 Loomle contributors.

#if WITH_DEV_AUTOMATION_TESTS

#include "EditorContext/EditorContextService.h"
#include "SalTestObjectModel.h"
#include "Tests/LoomleTestEditorState.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "BlueprintEditor.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "EdGraph/EdGraph.h"
#include "EdGraphSchema_K2.h"
#include "Editor.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Docking/TabManager.h"
#include "GameFramework/Actor.h"
#include "GraphEditor.h"
#include "HAL/PlatformTime.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/App.h"
#include "Misc/AutomationTest.h"
#include "Sal/SalJson.h"
#include "Sal/SalObjectBuilder.h"
#include "Settings/EditorStyleSettings.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "UObject/GarbageCollection.h"
#include "UObject/Package.h"
#include "UObject/UObjectHash.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/SWidget.h"
#include "Widgets/SWindow.h"

namespace
{
using namespace Loomle::EditorContext;
using namespace Loomle::Sal;

class FEditorContextTestProvider final : public IEditorContextProvider
{
public:
    FEditorContextTestProvider(
        const FName InName,
        const int32 InPriority,
        FString InMarker)
        : ProviderName(InName)
        , ProviderPriority(InPriority)
        , Marker(MoveTemp(InMarker))
    {
    }

    virtual FName Name() const override
    {
        return ProviderName;
    }

    virtual int32 Priority() const override
    {
        return ProviderPriority;
    }

    virtual bool Recognize(
        const FRecognitionInput& Input,
        FInteractionRecord& OutRecord) const override
    {
        ++RecognitionCount;
        if (!bRecognize)
        {
            return false;
        }

        OutRecord.Provider = ProviderName;
        OutRecord.Surface = ProviderName;
        OutRecord.TabId = Input.TabId;
        OutRecord.EditorName = Input.EditorName;
        OutRecord.Tab = Input.ActiveTab;
        OutRecord.DetailsView = Input.DetailsView;
        OutRecord.AssetEditor = Input.AssetEditor;
        OutRecord.bHadTab = Input.ActiveTab.IsValid();
        OutRecord.bHadFocusPath = Input.FocusPath != nullptr && Input.FocusPath->IsValid();
        OutRecord.bRecoveredHostFromWindow = Input.bRecoveredHostFromWindow;

        if (bOverrideTrackedTab)
        {
            OutRecord.Tab = TrackedTab;
            OutRecord.TabId = FName(TEXT("LoomleEditorContextTestTab"));
            OutRecord.bHadTab = true;
        }
        return true;
    }

    virtual TSharedPtr<FJsonObject> Build(const FInteractionRecord&) const override
    {
        ++BuildCount;
        FSalObjectBuilder Builder;
        Builder.AddComment(Marker);
        TSharedPtr<FJsonObject> Result = Builder.BuildResult();
        Result->SetStringField(
            TEXT("targetContext"),
            TEXT("exact_target"));
        TSharedPtr<FJsonObject> Target = MakeShared<FJsonObject>();
        Target->SetStringField(TEXT("kind"), TEXT("target"));
        Target->SetStringField(TEXT("domain"), TEXT("class"));
        Target->SetStringField(
            TEXT("path"),
            TEXT("/Script/CoreUObject.Object"));
        TSharedPtr<FJsonObject> Binding = MakeShared<FJsonObject>();
        Binding->SetStringField(TEXT("alias"), TEXT("context_root"));
        Binding->SetObjectField(TEXT("target"), Target);
        Result->SetObjectField(TEXT("target"), Binding);
        return Result;
    }

    void SetRecognize(const bool bValue)
    {
        bRecognize = bValue;
    }

    void OverrideTrackedTab(const TSharedPtr<SDockTab>& Tab)
    {
        TrackedTab = Tab;
        bOverrideTrackedTab = true;
    }

    int32 GetBuildCount() const
    {
        return BuildCount;
    }

private:
    FName ProviderName;
    int32 ProviderPriority = 0;
    FString Marker;
    TWeakPtr<SDockTab> TrackedTab;
    mutable int32 RecognitionCount = 0;
    mutable int32 BuildCount = 0;
    bool bRecognize = true;
    bool bOverrideTrackedTab = false;
};

bool ResultContainsComment(
    const TSharedPtr<FJsonObject>& Result,
    const FString& Expected)
{
    if (!Result.IsValid())
    {
        return false;
    }

    const TSharedPtr<FJsonObject>* Object = nullptr;
    if (!Result->TryGetObjectField(TEXT("object"), Object)
        || Object == nullptr
        || !(*Object).IsValid())
    {
        return false;
    }

    const TArray<TSharedPtr<FJsonValue>>* Statements = nullptr;
    if (!(*Object)->TryGetArrayField(TEXT("statements"), Statements)
        || Statements == nullptr)
    {
        return false;
    }

    for (const TSharedPtr<FJsonValue>& Value : *Statements)
    {
        const TSharedPtr<FJsonObject> Statement = Value.IsValid()
            ? Value->AsObject()
            : nullptr;
        FString Kind;
        FString Text;
        if (Statement.IsValid()
            && Statement->TryGetStringField(TEXT("kind"), Kind)
            && Kind == TEXT("comment")
            && Statement->TryGetStringField(TEXT("text"), Text)
            && Text.Contains(Expected))
        {
            return true;
        }
    }
    return false;
}

bool ResultContainsDiagnosticCode(
    const TSharedPtr<FJsonObject>& Result,
    const FString& ExpectedCode)
{
    if (!Result.IsValid())
    {
        return false;
    }

    const TArray<TSharedPtr<FJsonValue>>* Diagnostics = nullptr;
    if (!Result->TryGetArrayField(TEXT("diagnostics"), Diagnostics)
        || Diagnostics == nullptr)
    {
        return false;
    }

    for (const TSharedPtr<FJsonValue>& Value : *Diagnostics)
    {
        const TSharedPtr<FJsonObject> Diagnostic = Value.IsValid()
            ? Value->AsObject()
            : nullptr;
        FString Code;
        if (Diagnostic.IsValid()
            && Diagnostic->TryGetStringField(TEXT("code"), Code)
            && Code == ExpectedCode)
        {
            return true;
        }
    }
    return false;
}

bool ResultHasTargetContext(
    const TSharedPtr<FJsonObject>& Result,
    const FString& Expected)
{
    FString Context;
    return Result.IsValid()
        && Result->TryGetStringField(
            TEXT("targetContext"),
            Context)
        && Context == Expected;
}

bool IsValidSalResult(
    FAutomationTestBase& Test,
    const TSharedPtr<FJsonObject>& Result)
{
    TSharedPtr<FJsonObject> Error;
    const bool bValid = FSalJson::ValidateResult(Result, Error);
    Test.TestTrue(
        TEXT("Editor Context output satisfies the SAL v3 Result schema"),
        bValid);
    return bValid;
}

void RestoreEditorContextService(
    FEditorContextService& Service,
    const TArray<FName>& ProviderNames,
    const TSharedPtr<SWidget>& PreviousFocus)
{
    for (const FName ProviderName : ProviderNames)
    {
        Service.UnregisterProvider(ProviderName);
    }
    Service.Shutdown();
    if (FSlateApplication::IsInitialized() && PreviousFocus.IsValid())
    {
        FSlateApplication::Get().SetKeyboardFocus(
            PreviousFocus,
            EFocusCause::SetDirectly);
    }
    Service.Startup();
}

FString EditorContextGuidText(const FGuid& Guid)
{
    return Guid.ToString(EGuidFormats::DigitsWithHyphensLower);
}

bool ResultHasExactGraphTarget(
    const TSharedPtr<FJsonObject>& Result,
    const UBlueprint* Blueprint,
    const UEdGraph* Graph)
{
    FString TargetContext;
    const TSharedPtr<FJsonObject>* Binding = nullptr;
    const TSharedPtr<FJsonObject>* Target = nullptr;
    FString Domain;
    FString Asset;
    FString BlueprintId;
    FString GraphId;
    return Result.IsValid()
        && Blueprint != nullptr
        && Graph != nullptr
        && Result->TryGetStringField(
            TEXT("targetContext"),
            TargetContext)
        && TargetContext == TEXT("exact_target")
        && Result->TryGetObjectField(TEXT("target"), Binding)
        && Binding != nullptr
        && (*Binding)->TryGetObjectField(TEXT("target"), Target)
        && Target != nullptr
        && (*Target)->TryGetStringField(TEXT("domain"), Domain)
        && Domain == TEXT("graph")
        && (*Target)->TryGetStringField(TEXT("asset"), Asset)
        && Asset == Blueprint->GetPathName()
        && (*Target)->TryGetStringField(
            TEXT("blueprintId"),
            BlueprintId)
        && BlueprintId.Equals(
            EditorContextGuidText(Blueprint->GetBlueprintGuid()),
            ESearchCase::IgnoreCase)
        && (*Target)->TryGetStringField(TEXT("id"), GraphId)
        && GraphId.Equals(
            EditorContextGuidText(Graph->GraphGuid),
            ESearchCase::IgnoreCase);
}

bool ResultHasExactBlueprintTarget(
    const TSharedPtr<FJsonObject>& Result,
    const UBlueprint* Blueprint)
{
    FString TargetContext;
    const TSharedPtr<FJsonObject>* Binding = nullptr;
    const TSharedPtr<FJsonObject>* Target = nullptr;
    FString Domain;
    FString Asset;
    FString BlueprintId;
    return Result.IsValid()
        && Blueprint != nullptr
        && Result->TryGetStringField(
            TEXT("targetContext"),
            TargetContext)
        && TargetContext == TEXT("exact_target")
        && Result->TryGetObjectField(TEXT("target"), Binding)
        && Binding != nullptr
        && (*Binding)->TryGetObjectField(TEXT("target"), Target)
        && Target != nullptr
        && (*Target)->TryGetStringField(TEXT("domain"), Domain)
        && Domain == TEXT("blueprint")
        && (*Target)->TryGetStringField(TEXT("asset"), Asset)
        && Asset == Blueprint->GetPathName()
        && (*Target)->TryGetStringField(TEXT("id"), BlueprintId)
        && BlueprintId.Equals(
            EditorContextGuidText(Blueprint->GetBlueprintGuid()),
            ESearchCase::IgnoreCase);
}

bool ResultHasExactClassTarget(
    const TSharedPtr<FJsonObject>& Result,
    const UClass* Class)
{
    FString TargetContext;
    const TSharedPtr<FJsonObject>* Binding = nullptr;
    const TSharedPtr<FJsonObject>* Target = nullptr;
    FString Domain;
    FString Path;
    return Result.IsValid()
        && Class != nullptr
        && Result->TryGetStringField(
            TEXT("targetContext"),
            TargetContext)
        && TargetContext == TEXT("exact_target")
        && Result->TryGetObjectField(TEXT("target"), Binding)
        && Binding != nullptr
        && (*Binding)->TryGetObjectField(TEXT("target"), Target)
        && Target != nullptr
        && (*Target)->TryGetStringField(TEXT("domain"), Domain)
        && Domain == TEXT("class")
        && (*Target)->TryGetStringField(TEXT("path"), Path)
        && Path == Class->GetPathName();
}

bool ResultContainsObjectExpression(
    const TSharedPtr<FJsonObject>& Result,
    const FString& SemanticTag)
{
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
        return false;
    }

    for (const TSharedPtr<FJsonValue>& StatementValue : *Statements)
    {
        const TSharedPtr<FJsonObject>* Statement = nullptr;
        const TSharedPtr<FJsonObject>* Value = nullptr;
        const TSharedPtr<FJsonObject>* Fields = nullptr;
        if (StatementValue.IsValid()
            && StatementValue->TryGetObject(Statement)
            && Statement != nullptr
            && (*Statement)->TryGetObjectField(TEXT("value"), Value)
            && Value != nullptr
            && Loomle::Tests::Sal::TryReadObjectExpr(
                *Value,
                SemanticTag,
                Fields))
        {
            return true;
        }
    }
    return false;
}

bool ResultHasErrorDiagnostic(
    const TSharedPtr<FJsonObject>& Result)
{
    const TArray<TSharedPtr<FJsonValue>>* Diagnostics = nullptr;
    if (!Result.IsValid()
        || !Result->TryGetArrayField(
            TEXT("diagnostics"),
            Diagnostics)
        || Diagnostics == nullptr)
    {
        return true;
    }
    for (const TSharedPtr<FJsonValue>& Value : *Diagnostics)
    {
        const TSharedPtr<FJsonObject> Diagnostic = Value.IsValid()
            ? Value->AsObject()
            : nullptr;
        FString Severity;
        if (Diagnostic.IsValid()
            && Diagnostic->TryGetStringField(
                TEXT("severity"),
                Severity)
            && Severity == TEXT("error"))
        {
            return true;
        }
    }
    return false;
}

bool UnloadEditorContextTestPackage(
    UPackage* Package,
    FString& OutError)
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
        true);
    CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);
    if (FindPackage(nullptr, *PackageName) != nullptr)
    {
        OutError = TEXT("Fixture package remained loaded: ")
            + PackageName;
        return false;
    }
    return true;
}

class FStandaloneBlueprintEditorContext
{
public:
    ~FStandaloneBlueprintEditorContext()
    {
        FString Ignored;
        Cleanup(Ignored);
    }

    bool Initialize(FString& OutError)
    {
        OutError.Reset();
        if (GEditor == nullptr
            || GEditor->IsPlaySessionInProgress()
            || !FSlateApplication::IsInitialized())
        {
            OutError = TEXT(
                "Standalone Blueprint Context requires an idle Editor with initialized Slate.");
            return false;
        }
        if (!Transactions.Initialize())
        {
            OutError = TEXT(
                "Could not install an isolated transaction buffer.");
            return false;
        }

        FEditorContextService& Service =
            FEditorContextService::Get();
        bServiceWasStarted = Service.IsStarted();
        PreviousFocus =
            FSlateApplication::Get().GetKeyboardFocusedWidget();
        Service.Shutdown();

        StyleSettings = GetMutableDefault<UEditorStyleSettings>();
        if (StyleSettings == nullptr)
        {
            OutError = TEXT("Editor Style settings are unavailable.");
            return false;
        }
        PreviousOpenLocation =
            StyleSettings->AssetEditorOpenLocation;
        StyleSettings->AssetEditorOpenLocation =
            EAssetEditorOpenLocation::NewWindow;
        bOpenLocationOverridden = true;

        const FString Token =
            FGuid::NewGuid().ToString(EGuidFormats::Digits);
        Package = CreatePackage(*FString::Printf(
            TEXT("/Game/LoomleTests/EditorContextStandalone_%s"),
            *Token));
        Blueprint = FKismetEditorUtilities::CreateBlueprint(
            AActor::StaticClass(),
            Package,
            *FString::Printf(
                TEXT("BP_EditorContextStandalone_%s"),
                *Token),
            BPTYPE_Normal,
            UBlueprint::StaticClass(),
            UBlueprintGeneratedClass::StaticClass(),
            NAME_None);
        Graph = Blueprint != nullptr
            ? FBlueprintEditorUtils::FindEventGraph(Blueprint)
            : nullptr;
        OtherGraph = Blueprint != nullptr
            ? FBlueprintEditorUtils::CreateNewGraph(
                Blueprint,
                FName(TEXT("ContextSecondGraph")),
                UEdGraph::StaticClass(),
                UEdGraphSchema_K2::StaticClass())
            : nullptr;
        if (Blueprint != nullptr
            && Graph != nullptr
            && OtherGraph != nullptr)
        {
            FBlueprintEditorUtils::AddFunctionGraph(
                Blueprint,
                OtherGraph,
                true,
                static_cast<UClass*>(nullptr));

            // This fixture models reopening an existing Blueprint. UE opens a
            // newly-created Actor Blueprint in Components mode, so opt into
            // its full Editor and ask the native RestoreEditedObjectState path
            // to restore only the EventGraph. Graph B is opened explicitly
            // after the initial Context check.
            Blueprint->bIsNewlyCreated = false;
            Blueprint->bForceFullEditor = true;
            Blueprint->LastEditedDocuments.Reset();
            Blueprint->LastEditedDocuments.Add(Graph);
        }
        if (Package == nullptr
            || Blueprint == nullptr
            || Graph == nullptr
            || OtherGraph == nullptr
            || Blueprint->GeneratedClass == nullptr
            || !Blueprint->GetBlueprintGuid().IsValid()
            || !Graph->GraphGuid.IsValid()
            || !OtherGraph->GraphGuid.IsValid())
        {
            OutError = TEXT(
                "Could not create the Standalone Blueprint Graph fixtures.");
            return false;
        }

        FAssetRegistryModule::AssetCreated(Blueprint);
        bAssetRegistered = true;
        Package->SetDirtyFlag(false);

        // The Service must be observing before UE foregrounds the new Major
        // Tab. UAssetEditorSubsystem publishes the editor association later
        // in the same open sequence; starting after OpenEditorForAsset would
        // bypass the Standalone registration race this test guards.
        Service.Startup();

        UAssetEditorSubsystem* Editors =
            GEditor->GetEditorSubsystem<UAssetEditorSubsystem>();
        TSharedPtr<IToolkitHost> NoToolkitHost;
        if (Editors == nullptr
            || !Editors->OpenEditorForAsset(
                Blueprint,
                EToolkitMode::Standalone,
                NoToolkitHost,
                false))
        {
            OutError = TEXT(
                "UAssetEditorSubsystem could not open the Blueprint in Standalone mode.");
            return false;
        }
        FSlateApplication::Get().ClearKeyboardFocus(
            EFocusCause::SetDirectly);

        StartSeconds = FPlatformTime::Seconds();
        return true;
    }

    bool Update(FAutomationTestBase& Test)
    {
        if (FPlatformTime::Seconds() - StartSeconds > 30.0)
        {
            Test.AddError(FString::Printf(
                TEXT("Timed out waiting for Standalone Blueprint Context (last reason: %s)."),
                LastUnavailableReason.IsEmpty()
                    ? TEXT("surface_not_ready")
                    : *LastUnavailableReason));
            FString CleanupError;
            Test.TestTrue(
                *FString::Printf(
                    TEXT("Timed-out Standalone Context fixture cleans up: %s"),
                    *CleanupError),
                Cleanup(CleanupError));
            return true;
        }

        if (!PrepareGraphSurface())
        {
            return false;
        }

        if (!bOpeningRaceVerified)
        {
            if (!IsOpeningSurfaceReady())
            {
                return false;
            }
            FSlateApplication::Get().ClearKeyboardFocus(
                EFocusCause::SetDirectly);
            Test.TestFalse(
                TEXT("Opening-race assertion has no Slate keyboard focus"),
                FSlateApplication::Get()
                    .GetKeyboardFocusedWidget()
                    .IsValid());

            // No SGraphEditor focus is introduced before this call. The
            // Service must recover the same foreground Standalone Major Tab
            // after UAssetEditorSubsystem publishes its editor association.
            FInteractionRecord PathlessOpeningRecord;
            const bool bHasPathlessOpeningRecord =
                FEditorContextService::Get()
                    .GetTrackedRecordForTesting(
                        PathlessOpeningRecord);
            Test.TestTrue(
                TEXT("Standalone opening race retains a pre-Build tracker snapshot"),
                bHasPathlessOpeningRecord);
            if (bHasPathlessOpeningRecord)
            {
                Test.TestEqual(
                    TEXT("Pathless Standalone snapshot starts with the Unknown Provider"),
                    PathlessOpeningRecord.Provider,
                    FName(TEXT("unknown")));
                Test.TestEqual(
                    TEXT("Pathless Standalone snapshot starts with the Unknown surface"),
                    PathlessOpeningRecord.Surface,
                    FName(TEXT("unknown")));
                Test.TestNull(
                    TEXT("Pathless Standalone snapshot predates Asset Editor registration"),
                    PathlessOpeningRecord.AssetEditor);
                Test.TestFalse(
                    TEXT("Pathless Standalone snapshot has no Focus Path"),
                    PathlessOpeningRecord.bHadFocusPath);
                Test.TestTrue(
                    TEXT("Pathless Standalone snapshot retains the same Major Tab"),
                    PathlessOpeningRecord.Tab.Pin() == OwnerTab);
                Test.TestEqual(
                    TEXT("Pathless Standalone snapshot records the Standalone Toolkit Tab"),
                    PathlessOpeningRecord.TabId,
                    FName(TEXT("StandaloneToolkit")));
            }
            const TSharedPtr<FJsonObject> OpeningResult =
                FEditorContextService::Get().BuildResult();
            Test.TestTrue(
                TEXT("Standalone registration timing resolves the empty EventGraph exactly"),
                ResultHasExactGraphTarget(
                    OpeningResult,
                    Blueprint,
                    Graph));
            Test.TestTrue(
                TEXT("Opening-time EventGraph reports an empty selection"),
                ResultContainsComment(
                    OpeningResult,
                    TEXT("selected: none")));
            Test.TestFalse(
                TEXT("Opening-time exact Context has no error diagnostic"),
                ResultHasErrorDiagnostic(OpeningResult));
            IsValidSalResult(Test, OpeningResult);

            FRecognitionInput DeferredOpeningInput;
            DeferredOpeningInput.AssetEditor = Editor;
            DeferredOpeningInput.EditorName =
                FName(TEXT("BlueprintEditor"));
            DeferredOpeningInput.ActiveTab = OwnerTab;
            DeferredOpeningInput.bDeferredTabRecognition = true;
            FInteractionRecord DeferredOpeningRecord;
            Test.TestTrue(
                TEXT("Blueprint Provider recognizes the deferred Standalone owner Tab"),
                FEditorContextService::Get().RecognizeProviderForTesting(
                    FName(TEXT("blueprint")),
                    DeferredOpeningInput,
                    DeferredOpeningRecord));
            Test.TestTrue(
                TEXT("Deferred Standalone recognition records the exact focused Graph document"),
                DeferredOpeningRecord.BlueprintGraph.Get() == Graph);
            Test.TestFalse(
                TEXT("Deferred Standalone recognition has no SGraphEditor Focus Path evidence"),
                DeferredOpeningRecord.bGraphSurfaceFromFocusPath);
            Test.TestTrue(
                TEXT("Deferred Standalone recognition records focused-document evidence"),
                DeferredOpeningRecord.bGraphSurfaceFromFocusedDocument);
            Test.TestEqual(
                TEXT("Deferred Standalone recognition records the observed Blueprint UI state"),
                DeferredOpeningRecord.BlueprintSelectionState,
                Editor->GetUISelectionState());

            bOpeningRaceVerified = true;
            FocusGraphSurface();
            return false;
        }

        if (!bGraphIdentityMismatchStarted)
        {
            if (!IsGraphSurfaceReady())
            {
                FocusGraphSurface();
                return false;
            }

            Editor->SetUISelectionState(
                FBlueprintEditor::SelectionState_ClassDefaults);
            FRecognitionInput GraphAInput;
            FWidgetPath GraphAFocusPath;
            const TSharedPtr<SWidget> GraphAFocusedWidget =
                FSlateApplication::Get()
                    .GetKeyboardFocusedWidget();
            const bool bHasGraphAFocusPath =
                GraphAFocusedWidget.IsValid()
                && FSlateApplication::Get()
                    .GeneratePathToWidgetUnchecked(
                        GraphAFocusedWidget.ToSharedRef(),
                        GraphAFocusPath)
                && GraphAFocusPath.IsValid();
            Test.TestTrue(
                TEXT("Graph A recognition uses a real Slate Focus Path"),
                bHasGraphAFocusPath);
            Test.TestTrue(
                TEXT("Graph A Focus Path contains its exact SGraphEditor"),
                bHasGraphAFocusPath
                    && GraphAFocusPath.ContainsWidget(
                        GraphEditor.Get()));
            GraphAInput.AssetEditor = Editor;
            GraphAInput.EditorName =
                FName(TEXT("BlueprintEditor"));
            GraphAInput.FocusPath = bHasGraphAFocusPath
                ? &GraphAFocusPath
                : nullptr;
            GraphAInput.ActiveTab = GraphTab;
            GraphAInput.TabId = GraphTab.IsValid()
                ? GraphTab->GetLayoutIdentifier().TabType
                : NAME_None;
            if (bHasGraphAFocusPath)
            {
                for (int32 Index = 0;
                    Index < GraphAFocusPath.Widgets.Num();
                    ++Index)
                {
                    GraphAInput.WidgetTypes.AddUnique(
                        GraphAFocusPath.Widgets[Index]
                            .Widget->GetType());
                }
            }
            Test.TestEqual(
                TEXT("Graph A focus preserves the stale Class Defaults UI state"),
                Editor->GetUISelectionState(),
                FBlueprintEditor::SelectionState_ClassDefaults);
            const bool bRecognizedGraphA =
                FEditorContextService::Get()
                    .RecognizeProviderForTesting(
                        FName(TEXT("blueprint")),
                        GraphAInput,
                        GraphARecord);
            Test.TestTrue(
                TEXT("Blueprint Provider captures the real Graph A interaction"),
                bRecognizedGraphA);
            Test.TestEqual(
                TEXT("Graph A record preserves the Graph surface"),
                GraphARecord.Surface,
                FName(TEXT("blueprint_graph")));
            Test.TestEqual(
                TEXT("Graph A record preserves the observed stale UI state"),
                GraphARecord.BlueprintSelectionState,
                FBlueprintEditor::SelectionState_ClassDefaults);
            Test.TestTrue(
                TEXT("Graph A record uses exact SGraphEditor Focus Path evidence"),
                GraphARecord.bGraphSurfaceFromFocusPath);
            Test.TestTrue(
                TEXT("Graph A record preserves the real Focus Path interaction"),
                GraphARecord.bHadFocusPath);
            Test.TestFalse(
                TEXT("Graph A record does not require deferred focused-document evidence"),
                GraphARecord.bGraphSurfaceFromFocusedDocument);
            Test.TestTrue(
                TEXT("Graph A record preserves the exact EventGraph identity"),
                GraphARecord.BlueprintGraph.Get() == Graph);

            OtherGraphEditor =
                Editor->OpenGraphAndBringToFront(
                    OtherGraph,
                    true);
            Test.TestTrue(
                TEXT("The real Standalone Editor opens Graph B"),
                OtherGraphEditor.IsValid());
            bGraphIdentityMismatchStarted = true;
            return false;
        }

        if (!bGraphIdentityMismatchVerified)
        {
            if (!IsOtherGraphSurfaceReady())
            {
                FocusOtherGraphSurface();
                return false;
            }

            const TSharedPtr<FJsonObject> StaleGraphAResult =
                FEditorContextService::Get()
                    .BuildProviderForTesting(
                        FName(TEXT("blueprint")),
                        GraphARecord);
            Test.TestTrue(
                TEXT("A Graph A record becomes owner_invalid after the real Editor focuses Graph B"),
                ResultContainsDiagnosticCode(
                    StaleGraphAResult,
                    TEXT("context.owner_invalid")));
            Test.TestFalse(
                TEXT("A stale Graph A record cannot emit an exact Graph Target while Graph B is focused"),
                ResultHasExactGraphTarget(
                    StaleGraphAResult,
                    Blueprint,
                    Graph));
            IsValidSalResult(Test, StaleGraphAResult);

            GraphEditor = Editor->OpenGraphAndBringToFront(
                Graph,
                true);
            Test.TestTrue(
                TEXT("The real Standalone Editor restores Graph A"),
                GraphEditor.IsValid());
            bGraphIdentityMismatchVerified = true;
            return false;
        }

        if (!IsGraphSurfaceReady())
        {
            FocusGraphSurface();
            return false;
        }

        Editor->SetUISelectionState(
            FBlueprintEditor::SelectionState_ClassDefaults);
        Test.TestEqual(
            TEXT("Public Context fixture preserves a stale non-Graph Blueprint UI state"),
            Editor->GetUISelectionState(),
            FBlueprintEditor::SelectionState_ClassDefaults);
        const TSharedPtr<FJsonObject> FocusedResult =
            FEditorContextService::Get().BuildResult();
        Test.TestTrue(
            TEXT("Standalone EventGraph focus resolves its exact Graph Target"),
            ResultHasExactGraphTarget(
                FocusedResult,
                Blueprint,
                Graph));
        Test.TestTrue(
            TEXT("Standalone EventGraph reports an empty Graph selection"),
            ResultContainsComment(
                FocusedResult,
                TEXT("selected: none")));
        Test.TestTrue(
            TEXT("Standalone EventGraph records its Blueprint Graph surface"),
            ResultContainsComment(
                FocusedResult,
                TEXT("Blueprint Editor / Graph")));
        Test.TestFalse(
            TEXT("Empty Standalone EventGraph selection does not invent a Node"),
            ResultContainsObjectExpression(
                FocusedResult,
                TEXT("node")));
        Test.TestFalse(
            TEXT("Empty Standalone EventGraph selection does not invent a Pin"),
            ResultContainsObjectExpression(
                FocusedResult,
                TEXT("pin")));
        Test.TestFalse(
            TEXT("Standalone EventGraph exact Context has no error diagnostic"),
            ResultHasErrorDiagnostic(FocusedResult));
        IsValidSalResult(Test, FocusedResult);

        FSlateApplication::Get().ClearKeyboardFocus(
            EFocusCause::SetDirectly);
        Test.TestFalse(
            TEXT("Standalone Context fixture clears Slate keyboard focus"),
            FSlateApplication::Get()
                .GetKeyboardFocusedWidget()
                .IsValid());

        const TSharedPtr<FJsonObject> FocusLostResult =
            FEditorContextService::Get().BuildResult();
        Test.TestTrue(
            TEXT("Focus loss retains the last exact Standalone EventGraph Target"),
            ResultHasExactGraphTarget(
                FocusLostResult,
                Blueprint,
                Graph));
        Test.TestTrue(
            TEXT("Focus-loss Context rereads the empty Graph selection"),
            ResultContainsComment(
                FocusLostResult,
                TEXT("selected: none")));
        Test.TestFalse(
            TEXT("Focus-loss Standalone Context has no error diagnostic"),
            ResultHasErrorDiagnostic(FocusLostResult));
        IsValidSalResult(Test, FocusLostResult);

        FRecognitionInput ExplicitSurfaceInput;
        ExplicitSurfaceInput.AssetEditor = Editor;
        ExplicitSurfaceInput.EditorName =
            FName(TEXT("BlueprintEditor"));
        Test.TestTrue(
            TEXT("Negative surface fixture retains an owned FocusedGraph"),
            Editor->GetFocusedGraph() == Graph);

        Editor->SetUISelectionState(
            FBlueprintEditor::SelectionState_ClassDefaults);
        FInteractionRecord ClassDefaultsRecord;
        Test.TestTrue(
            TEXT("Blueprint Provider recognizes explicit non-deferred Class Defaults state"),
            FEditorContextService::Get().RecognizeProviderForTesting(
                FName(TEXT("blueprint")),
                ExplicitSurfaceInput,
                ClassDefaultsRecord));
        Test.TestFalse(
            TEXT("Class Defaults has no SGraphEditor Focus Path evidence"),
            ClassDefaultsRecord.bGraphSurfaceFromFocusPath);
        Test.TestFalse(
            TEXT("FocusedGraph alone is not focused-document evidence for Class Defaults"),
            ClassDefaultsRecord.bGraphSurfaceFromFocusedDocument);
        Test.TestTrue(
            TEXT("Class Defaults negative fixture still records the retained FocusedGraph"),
            ClassDefaultsRecord.BlueprintGraph.Get() == Graph);
        Test.TestEqual(
            TEXT("Class Defaults record preserves its explicit Blueprint UI state"),
            ClassDefaultsRecord.BlueprintSelectionState,
            FBlueprintEditor::SelectionState_ClassDefaults);
        const TSharedPtr<FJsonObject> ClassDefaultsResult =
            FEditorContextService::Get().BuildProviderForTesting(
                FName(TEXT("blueprint")),
                ClassDefaultsRecord);
        Test.TestTrue(
            TEXT("Explicit non-deferred Class Defaults wins over a retained FocusedGraph"),
            ResultHasExactClassTarget(
                ClassDefaultsResult,
                Blueprint->GeneratedClass));
        Test.TestTrue(
            TEXT("Class Defaults Context records its explicit surface"),
            ResultContainsComment(
                ClassDefaultsResult,
                TEXT("Blueprint Editor / Class Defaults")));
        Test.TestFalse(
            TEXT("Class Defaults Context is not rewritten as a Graph Target"),
            ResultHasExactGraphTarget(
                ClassDefaultsResult,
                Blueprint,
                Graph));
        Test.TestFalse(
            TEXT("Explicit Class Defaults Context has no error diagnostic"),
            ResultHasErrorDiagnostic(ClassDefaultsResult));
        IsValidSalResult(Test, ClassDefaultsResult);

        Editor->SetUISelectionState(
            FBlueprintEditor::SelectionState_Components);
        FInteractionRecord ComponentsRecord;
        Test.TestTrue(
            TEXT("Blueprint Provider recognizes explicit non-deferred Components state"),
            FEditorContextService::Get().RecognizeProviderForTesting(
                FName(TEXT("blueprint")),
                ExplicitSurfaceInput,
                ComponentsRecord));
        Test.TestFalse(
            TEXT("Components has no SGraphEditor Focus Path evidence"),
            ComponentsRecord.bGraphSurfaceFromFocusPath);
        Test.TestFalse(
            TEXT("FocusedGraph alone is not focused-document evidence for Components"),
            ComponentsRecord.bGraphSurfaceFromFocusedDocument);
        Test.TestTrue(
            TEXT("Components negative fixture still records the retained FocusedGraph"),
            ComponentsRecord.BlueprintGraph.Get() == Graph);
        Test.TestEqual(
            TEXT("Components record preserves its explicit Blueprint UI state"),
            ComponentsRecord.BlueprintSelectionState,
            FBlueprintEditor::SelectionState_Components);
        const TSharedPtr<FJsonObject> ComponentsResult =
            FEditorContextService::Get().BuildProviderForTesting(
                FName(TEXT("blueprint")),
                ComponentsRecord);
        Test.TestTrue(
            TEXT("Explicit non-deferred Components wins over a retained FocusedGraph"),
            ResultHasExactBlueprintTarget(
                ComponentsResult,
                Blueprint));
        Test.TestTrue(
            TEXT("Components Context records its explicit surface"),
            ResultContainsComment(
                ComponentsResult,
                TEXT("Blueprint Editor / Components")));
        Test.TestFalse(
            TEXT("Components Context is not rewritten as a Graph Target"),
            ResultHasExactGraphTarget(
                ComponentsResult,
                Blueprint,
                Graph));
        Test.TestFalse(
            TEXT("Explicit Components Context has no error diagnostic"),
            ResultHasErrorDiagnostic(ComponentsResult));
        IsValidSalResult(Test, ComponentsResult);

        FString CleanupError;
        Test.TestTrue(
            *FString::Printf(
                TEXT("Standalone Context fixture cleans up: %s"),
                *CleanupError),
            Cleanup(CleanupError));
        return true;
    }

    bool Cleanup(FString& OutError)
    {
        OutError.Reset();
        if (bCleaned)
        {
            return true;
        }
        bCleaned = true;

        FEditorContextService& Service =
            FEditorContextService::Get();
        Service.Shutdown();

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

        GraphEditor.Reset();
        OtherGraphEditor.Reset();
        GraphTab.Reset();
        OtherGraphTab.Reset();
        OwnerTab.Reset();
        Editor = nullptr;

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
        OtherGraph = nullptr;

        bool bPackageUnloaded = true;
        if (bEditorClosed)
        {
            bPackageUnloaded = UnloadEditorContextTestPackage(
                PackageToUnload,
                OutError);
        }
        else
        {
            OutError = TEXT(
                "The Standalone Blueprint Editor remained open during cleanup.");
            bPackageUnloaded = false;
        }

        Transactions.Restore();
        if (FSlateApplication::IsInitialized()
            && PreviousFocus.IsValid())
        {
            FSlateApplication::Get().SetKeyboardFocus(
                PreviousFocus,
                EFocusCause::SetDirectly);
        }
        PreviousFocus.Reset();
        if (bServiceWasStarted)
        {
            Service.Startup();
        }
        return bEditorClosed && bPackageUnloaded;
    }

    FString LastUnavailableReason;

private:
    bool PrepareGraphSurface()
    {
        if (Editor == nullptr)
        {
            UAssetEditorSubsystem* Editors =
                GEditor != nullptr
                    ? GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()
                    : nullptr;
            IAssetEditorInstance* Instance =
                Editors != nullptr && Blueprint != nullptr
                    ? Editors->FindEditorForAsset(Blueprint, false)
                    : nullptr;
            if (Instance == nullptr)
            {
                LastUnavailableReason = TEXT("asset_editor_not_registered");
                return false;
            }
            if (Instance->GetEditorName()
                != FName(TEXT("BlueprintEditor")))
            {
                LastUnavailableReason = TEXT("wrong_asset_editor_type");
                return false;
            }
            Editor = static_cast<FBlueprintEditor*>(Instance);
        }

        const TSharedPtr<FTabManager> Manager =
            Editor->GetAssociatedTabManager();

        const bool bGraphTabMatchesEditor =
            GraphEditor.IsValid()
            && GraphTab.IsValid()
            && &GraphTab->GetContent().Get()
                == static_cast<SWidget*>(GraphEditor.Get());
        if (!bGraphTabMatchesEditor)
        {
            TArray<TSharedPtr<SDockTab>> GraphTabs;
            Editor->FindOpenTabsContainingDocument(
                Graph,
                GraphTabs);
            GraphTab.Reset();
            if (GraphEditor.IsValid())
            {
                for (const TSharedPtr<SDockTab>& Candidate
                    : GraphTabs)
                {
                    if (Candidate.IsValid()
                        && &Candidate->GetContent().Get()
                            == static_cast<SWidget*>(
                                GraphEditor.Get()))
                    {
                        GraphTab = Candidate;
                        break;
                    }
                }
            }
            else if (!GraphTabs.IsEmpty()
                && GraphTabs[0].IsValid())
            {
                GraphTab = GraphTabs[0];
                GraphEditor = StaticCastSharedRef<SGraphEditor>(
                    GraphTab->GetContent());
            }
            if (!GraphEditor.IsValid()
                || !GraphTab.IsValid())
            {
                LastUnavailableReason =
                    TEXT("restored_event_graph_tab_not_open");
                return false;
            }
        }

        if (!OwnerTab.IsValid())
        {
            OwnerTab = Manager.IsValid()
                ? Manager->GetOwnerTab()
                : nullptr;
            if (!OwnerTab.IsValid())
            {
                LastUnavailableReason = TEXT("owner_major_tab_missing");
                return false;
            }
        }
        return true;
    }

    bool IsOpeningSurfaceReady()
    {
        if (Editor == nullptr
            || !GraphEditor.IsValid()
            || !GraphTab.IsValid()
            || !OwnerTab.IsValid())
        {
            LastUnavailableReason = TEXT("fixture_or_editor_missing");
            return false;
        }
        if (GraphEditor->GetCurrentGraph() != Graph
            || Editor->GetFocusedGraph() != Graph)
        {
            LastUnavailableReason =
                TEXT("restored_event_graph_not_current");
            return false;
        }
        if (!Editor->GetSelectedNodes().IsEmpty())
        {
            LastUnavailableReason =
                TEXT("opening_graph_selection_not_empty");
            return false;
        }
        if (!OwnerTab->IsForeground())
        {
            LastUnavailableReason =
                TEXT("owner_major_tab_not_foreground");
            return false;
        }
        if (!GraphTab->IsForeground())
        {
            LastUnavailableReason =
                TEXT("opening_event_graph_tab_not_foreground");
            return false;
        }

        const TSharedPtr<SWindow> GraphWindow =
            FSlateApplication::Get().FindWidgetWindow(
                GraphEditor.ToSharedRef());
        if (!GraphWindow.IsValid()
            || GraphWindow->GetType() != EWindowType::Normal
            || !GraphWindow->IsVisible()
            || GraphWindow->IsWindowMinimized())
        {
            LastUnavailableReason =
                TEXT("opening_graph_window_not_interactive");
            return false;
        }
        if (FSlateApplication::Get()
                .GetActiveTopLevelRegularWindow()
            != GraphWindow)
        {
            LastUnavailableReason =
                TEXT("opening_graph_window_not_active");
            return false;
        }
        LastUnavailableReason.Reset();
        return true;
    }

    void FocusGraphSurface()
    {
        if (OwnerTab.IsValid())
        {
            OwnerTab->ActivateInParent(
                ETabActivationCause::SetDirectly);
            const TSharedPtr<SWindow> OwnerWindow =
                OwnerTab->GetParentWindow();
            if (OwnerWindow.IsValid())
            {
                OwnerWindow->BringToFront();
            }
        }
        if (Editor != nullptr
            && Graph != nullptr)
        {
            GraphEditor = Editor->OpenGraphAndBringToFront(
                Graph,
                true);
            GraphTab.Reset();
        }
    }

    void FocusOtherGraphSurface()
    {
        if (OwnerTab.IsValid())
        {
            OwnerTab->ActivateInParent(
                ETabActivationCause::SetDirectly);
            const TSharedPtr<SWindow> OwnerWindow =
                OwnerTab->GetParentWindow();
            if (OwnerWindow.IsValid())
            {
                OwnerWindow->BringToFront();
            }
        }
        if (Editor != nullptr
            && OtherGraph != nullptr)
        {
            OtherGraphEditor = Editor->OpenGraphAndBringToFront(
                OtherGraph,
                true);
            OtherGraphTab.Reset();
        }
    }

    bool IsOtherGraphSurfaceReady()
    {
        if (Editor == nullptr
            || !OtherGraphEditor.IsValid()
            || OtherGraph == nullptr)
        {
            LastUnavailableReason =
                TEXT("other_graph_fixture_missing");
            return false;
        }
        const bool bOtherGraphTabMatchesEditor =
            OtherGraphTab.IsValid()
            && &OtherGraphTab->GetContent().Get()
                == static_cast<SWidget*>(
                    OtherGraphEditor.Get());
        if (!bOtherGraphTabMatchesEditor)
        {
            TArray<TSharedPtr<SDockTab>> GraphTabs;
            Editor->FindOpenTabsContainingDocument(
                OtherGraph,
                GraphTabs);
            OtherGraphTab.Reset();
            for (const TSharedPtr<SDockTab>& Candidate
                : GraphTabs)
            {
                if (Candidate.IsValid()
                    && &Candidate->GetContent().Get()
                        == static_cast<SWidget*>(
                            OtherGraphEditor.Get()))
                {
                    OtherGraphTab = Candidate;
                    break;
                }
            }
        }
        if (!OtherGraphTab.IsValid())
        {
            LastUnavailableReason =
                TEXT("other_graph_tab_not_open");
            return false;
        }
        if (OtherGraphEditor->GetCurrentGraph() != OtherGraph
            || Editor->GetFocusedGraph() != OtherGraph)
        {
            LastUnavailableReason =
                TEXT("other_graph_not_focused");
            return false;
        }
        if (!OtherGraphTab->IsForeground())
        {
            LastUnavailableReason =
                TEXT("other_graph_tab_not_foreground");
            return false;
        }
        LastUnavailableReason.Reset();
        return true;
    }

    bool IsGraphSurfaceReady()
    {
        if (Editor == nullptr
            || !GraphEditor.IsValid()
            || !OwnerTab.IsValid())
        {
            LastUnavailableReason = TEXT("fixture_or_editor_missing");
            return false;
        }
        if (GraphEditor->GetCurrentGraph() != Graph
            || Editor->GetFocusedGraph() != Graph)
        {
            LastUnavailableReason = TEXT("event_graph_not_focused");
            return false;
        }
        if (!Editor->GetSelectedNodes().IsEmpty())
        {
            GraphEditor->ClearSelectionSet();
            LastUnavailableReason = TEXT("graph_selection_not_empty");
            return false;
        }
        if (!OwnerTab->IsForeground())
        {
            LastUnavailableReason = TEXT("owner_major_tab_not_foreground");
            return false;
        }
        if (!GraphTab.IsValid()
            || !GraphTab->IsForeground())
        {
            LastUnavailableReason = TEXT("event_graph_tab_not_foreground");
            return false;
        }

        const TSharedPtr<SWindow> GraphWindow =
            FSlateApplication::Get().FindWidgetWindow(
                GraphEditor.ToSharedRef());
        if (!GraphWindow.IsValid()
            || GraphWindow->GetType() != EWindowType::Normal
            || !GraphWindow->IsVisible()
            || GraphWindow->IsWindowMinimized())
        {
            LastUnavailableReason = TEXT("graph_window_not_interactive");
            return false;
        }

        const TSharedPtr<SWidget> FocusedWidget =
            FSlateApplication::Get().GetKeyboardFocusedWidget();
        if (!FocusedWidget.IsValid())
        {
            LastUnavailableReason = TEXT("keyboard_focus_missing");
            return false;
        }
        const TSharedPtr<SWindow> FocusedWindow =
            FSlateApplication::Get().FindWidgetWindow(
                FocusedWidget.ToSharedRef());
        if (FocusedWindow != GraphWindow)
        {
            LastUnavailableReason = TEXT("keyboard_focus_in_other_window");
            return false;
        }

        FWidgetPath FocusPath;
        if (!FSlateApplication::Get().GeneratePathToWidgetUnchecked(
                FocusedWidget.ToSharedRef(),
                FocusPath)
            || !FocusPath.IsValid())
        {
            LastUnavailableReason = TEXT("focused_widget_path_unavailable");
            return false;
        }
        if (!FocusPath.ContainsWidget(GraphEditor.Get()))
        {
            LastUnavailableReason = TEXT("focus_path_outside_event_graph");
            return false;
        }
        LastUnavailableReason.Reset();
        return true;
    }

private:
    Loomle::Tests::FScopedIsolatedTransactor Transactions;
    UPackage* Package = nullptr;
    UBlueprint* Blueprint = nullptr;
    UEdGraph* Graph = nullptr;
    UEdGraph* OtherGraph = nullptr;
    FBlueprintEditor* Editor = nullptr;
    TSharedPtr<SGraphEditor> GraphEditor;
    TSharedPtr<SGraphEditor> OtherGraphEditor;
    TSharedPtr<SDockTab> GraphTab;
    TSharedPtr<SDockTab> OtherGraphTab;
    TSharedPtr<SDockTab> OwnerTab;
    FInteractionRecord GraphARecord;
    UEditorStyleSettings* StyleSettings = nullptr;
    EAssetEditorOpenLocation PreviousOpenLocation =
        EAssetEditorOpenLocation::Default;
    TSharedPtr<SWidget> PreviousFocus;
    double StartSeconds = 0.0;
    bool bServiceWasStarted = false;
    bool bOpeningRaceVerified = false;
    bool bGraphIdentityMismatchStarted = false;
    bool bGraphIdentityMismatchVerified = false;
    bool bOpenLocationOverridden = false;
    bool bAssetRegistered = false;
    bool bCleaned = false;
};

class FRunStandaloneBlueprintEditorContextCommand final
    : public IAutomationLatentCommand
{
public:
    FRunStandaloneBlueprintEditorContextCommand(
        FAutomationTestBase* InTest,
        TSharedRef<FStandaloneBlueprintEditorContext> InContext)
        : Test(InTest)
        , Context(MoveTemp(InContext))
    {
    }

    virtual bool Update() override
    {
        return Context->Update(*Test);
    }

private:
    FAutomationTestBase* Test = nullptr;
    TSharedRef<FStandaloneBlueprintEditorContext> Context;
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FEditorContextProviderPriorityAndFocusFallbackTest,
    "Loomle.EditorContext.ProviderPriorityAndFocusFallback",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEditorContextProviderPriorityAndFocusFallbackTest::RunTest(
    const FString& Parameters)
{
    if (!FSlateApplication::IsInitialized())
    {
        AddError(TEXT("Editor Context lifecycle tests require initialized Slate."));
        return false;
    }

    FEditorContextService& Service = FEditorContextService::Get();
    const TSharedPtr<SWidget> PreviousFocus =
        FSlateApplication::Get().GetKeyboardFocusedWidget();
    const FName LowName(TEXT("loomle_test_context_low"));
    const FName HighName(TEXT("loomle_test_context_high"));
    const TSharedRef<FEditorContextTestProvider> Low =
        MakeShared<FEditorContextTestProvider>(
            LowName,
            5000,
            TEXT("test provider: low"));
    const TSharedRef<FEditorContextTestProvider> High =
        MakeShared<FEditorContextTestProvider>(
            HighName,
            6000,
            TEXT("test provider: high"));

    Service.Shutdown();
    Service.RegisterProvider(Low);
    Service.RegisterProvider(High);
    Service.Startup();

    const TSharedPtr<FJsonObject> First = Service.BuildResult();
    TestTrue(
        TEXT("The highest-priority matching Provider owns the current interaction"),
        ResultContainsComment(First, TEXT("test provider: high")));
    TestTrue(
        TEXT("An exact Provider result retains its Target context"),
        ResultHasTargetContext(First, TEXT("exact_target")));
    IsValidSalResult(*this, First);
    TestEqual(
        TEXT("A lower-priority matching Provider is not asked to build"),
        Low->GetBuildCount(),
        0);

    High->SetRecognize(false);
    Low->SetRecognize(false);
    FSlateApplication::Get().ClearKeyboardFocus(EFocusCause::SetDirectly);

    const TSharedPtr<FJsonObject> AfterFocusLoss = Service.BuildResult();
    TestTrue(
        TEXT("Losing Slate keyboard focus retains the last exact interaction"),
        ResultContainsComment(AfterFocusLoss, TEXT("test provider: high")));
    IsValidSalResult(*this, AfterFocusLoss);
    TestEqual(
        TEXT("The retained Provider reads current state again"),
        High->GetBuildCount(),
        2);

    RestoreEditorContextService(
        Service,
        {LowName, HighName},
        PreviousFocus);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FEditorContextStaleTrackedTabTest,
    "Loomle.EditorContext.StaleTrackedTab",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEditorContextStaleTrackedTabTest::RunTest(
    const FString& Parameters)
{
    if (!FSlateApplication::IsInitialized())
    {
        AddError(TEXT("Editor Context lifecycle tests require initialized Slate."));
        return false;
    }

    FEditorContextService& Service = FEditorContextService::Get();
    const TSharedPtr<SWidget> PreviousFocus =
        FSlateApplication::Get().GetKeyboardFocusedWidget();
    const FName ProviderName(TEXT("loomle_test_context_stale_tab"));
    const TSharedRef<FEditorContextTestProvider> Provider =
        MakeShared<FEditorContextTestProvider>(
            ProviderName,
            6000,
            TEXT("test provider: tracked tab"));
    TSharedPtr<SDockTab> TrackedTab =
        SNew(SDockTab)
        .TabRole(ETabRole::NomadTab);
    Provider->OverrideTrackedTab(TrackedTab);

    Service.Shutdown();
    Service.RegisterProvider(Provider);
    Service.Startup();

    const TSharedPtr<FJsonObject> Live = Service.BuildResult();
    TestTrue(
        TEXT("A live tracked DockTab can be read"),
        ResultContainsComment(Live, TEXT("test provider: tracked tab")));

    Provider->SetRecognize(false);
    FSlateApplication::Get().ClearKeyboardFocus(EFocusCause::SetDirectly);
    TrackedTab.Reset();

    const TSharedPtr<FJsonObject> Stale = Service.BuildResult();
    TestTrue(
        TEXT("A destroyed tracked DockTab fails closed"),
        ResultContainsDiagnosticCode(Stale, TEXT("context.owner_invalid")));
    TestTrue(
        TEXT("A stale tracked surface has unresolved Target context"),
        ResultHasTargetContext(
            Stale,
            TEXT("unresolved_target")));
    IsValidSalResult(*this, Stale);
    TestTrue(
        TEXT("The stale surface is reported rather than silently replaced"),
        ResultContainsComment(Stale, TEXT("selection: unavailable")));
    TestEqual(
        TEXT("The stale Provider is not allowed to build from an invalid owner"),
        Provider->GetBuildCount(),
        1);

    RestoreEditorContextService(
        Service,
        {ProviderName},
        PreviousFocus);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FEditorContextBuiltInModalProviderTest,
    "Loomle.EditorContext.BuiltIn.ModalProvider",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEditorContextBuiltInModalProviderTest::RunTest(
    const FString& Parameters)
{
    FEditorContextService& Service = FEditorContextService::Get();
    FRecognitionInput Input;
    Input.bModal = true;
    FInteractionRecord Record;

    TestTrue(
        TEXT("The built-in Modal Provider recognizes native modal state"),
        Service.RecognizeProviderForTesting(
            FName(TEXT("modal")),
            Input,
            Record));
    TestEqual(
        TEXT("Modal recognition records the normalized surface"),
        Record.Surface,
        FName(TEXT("modal_dialog")));

    const TSharedPtr<FJsonObject> Result =
        Service.BuildProviderForTesting(FName(TEXT("modal")), Record);
    TestTrue(
        TEXT("Modal context suppresses the previous editor selection"),
        ResultContainsComment(Result, TEXT("previous context: suppressed")));
    TestTrue(
        TEXT("Modal suppression has unresolved Target context"),
        ResultHasTargetContext(
            Result,
            TEXT("unresolved_target")));
    IsValidSalResult(*this, Result);

    Input.bModal = false;
    FInteractionRecord Rejected;
    TestFalse(
        TEXT("The Modal Provider does not infer modal state from other fields"),
        Service.RecognizeProviderForTesting(
            FName(TEXT("modal")),
            Input,
            Rejected));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FEditorContextBuiltInContentBrowserOwnershipTest,
    "Loomle.EditorContext.BuiltIn.ContentBrowserOwnership",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEditorContextBuiltInContentBrowserOwnershipTest::RunTest(
    const FString& Parameters)
{
    FEditorContextService& Service = FEditorContextService::Get();
    FRecognitionInput PickerInput;
    PickerInput.WidgetTypes.Add(FName(TEXT("SAssetView")));
    FInteractionRecord Rejected;
    TestFalse(
        TEXT("An embedded Asset Picker is not mistaken for the Content Browser"),
        Service.RecognizeProviderForTesting(
            FName(TEXT("content_browser")),
            PickerInput,
            Rejected));

    FRecognitionInput BrowserInput = PickerInput;
    BrowserInput.WidgetTypes.Add(FName(TEXT("SContentBrowser")));
    FInteractionRecord BrowserRecord;
    TestTrue(
        TEXT("A native Content Browser ancestor establishes ownership"),
        Service.RecognizeProviderForTesting(
            FName(TEXT("content_browser")),
            BrowserInput,
            BrowserRecord));
    TestEqual(
        TEXT("Content Browser recognition records its stable surface"),
        BrowserRecord.Surface,
        FName(TEXT("content_browser")));

    const TSharedPtr<FJsonObject> Result =
        Service.BuildProviderForTesting(
            FName(TEXT("content_browser")),
            BrowserRecord);
    TestTrue(
        TEXT("A structurally owned subview without a live SAssetView fails closed"),
        ResultContainsComment(
            Result,
            TEXT("no public side-effect-free selection API")));
    TestTrue(
        TEXT("Content Browser without one Asset retains Asset Domain root"),
        ResultHasTargetContext(Result, TEXT("domain_root")));
    IsValidSalResult(*this, Result);

    FRecognitionInput DrawerInput;
    DrawerInput.TabId = FName(TEXT("ContentBrowserDrawer7"));
    FInteractionRecord DrawerRecord;
    TestTrue(
        TEXT("Native Content Browser drawer tab identifiers are recognized"),
        Service.RecognizeProviderForTesting(
            FName(TEXT("content_browser")),
            DrawerInput,
            DrawerRecord));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FEditorContextBuiltInLevelEditorOwnershipTest,
    "Loomle.EditorContext.BuiltIn.LevelEditorOwnership",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEditorContextBuiltInLevelEditorOwnershipTest::RunTest(
    const FString& Parameters)
{
    FEditorContextService& Service = FEditorContextService::Get();
    FRecognitionInput GenericViewportInput;
    GenericViewportInput.WidgetTypes.Add(FName(TEXT("SEditorViewport")));
    GenericViewportInput.Tags.Add(FName(TEXT("LevelEditorViewport")));
    FInteractionRecord Rejected;
    TestFalse(
        TEXT("Generic viewport structure cannot claim the global Level selection"),
        Service.RecognizeProviderForTesting(
            FName(TEXT("level_editor")),
            GenericViewportInput,
            Rejected));

    FRecognitionInput LevelInput = GenericViewportInput;
    LevelInput.TabId = FName(TEXT("LevelEditorViewport"));
    FInteractionRecord Record;
    TestTrue(
        TEXT("The native Level Editor viewport tab establishes ownership"),
        Service.RecognizeProviderForTesting(
            FName(TEXT("level_editor")),
            LevelInput,
            Record));
    TestEqual(
        TEXT("Level Editor recognition records its stable surface"),
        Record.Surface,
        FName(TEXT("level_editor")));

    LevelInput.AssetEditor =
        reinterpret_cast<IAssetEditorInstance*>(static_cast<UPTRINT>(1));
    FInteractionRecord AssetOwnedRecord;
    TestFalse(
        TEXT("An asset editor hosted viewport cannot claim Level selection"),
        Service.RecognizeProviderForTesting(
            FName(TEXT("level_editor")),
            LevelInput,
            AssetOwnedRecord));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FEditorContextBuiltInUnknownFallbackTest,
    "Loomle.EditorContext.BuiltIn.UnknownFallback",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEditorContextBuiltInUnknownFallbackTest::RunTest(
    const FString& Parameters)
{
    FEditorContextService& Service = FEditorContextService::Get();
    FRecognitionInput Input;
    Input.TabId = FName(TEXT("LoomleUnrecognizedNativeTab"));
    FInteractionRecord Record;
    TestTrue(
        TEXT("The built-in Unknown Provider is an explicit total fallback"),
        Service.RecognizeProviderForTesting(
            FName(TEXT("unknown")),
            Input,
            Record));

    const TSharedPtr<FJsonObject> Result =
        Service.BuildProviderForTesting(FName(TEXT("unknown")), Record);
    TestTrue(
        TEXT("Unknown surfaces preserve the structural tab identifier"),
        ResultContainsComment(Result, TEXT("LoomleUnrecognizedNativeTab")));
    TestTrue(
        TEXT("Unknown surfaces do not invent a selection"),
        ResultContainsComment(Result, TEXT("selection: unavailable")));
    TestTrue(
        TEXT("Unknown surfaces have unresolved Target context"),
        ResultHasTargetContext(
            Result,
            TEXT("unresolved_target")));
    TestTrue(
        TEXT("Unknown surfaces explain why no exact Target is available"),
        ResultContainsDiagnosticCode(
            Result,
            TEXT("resolution.unresolved_target")));
    IsValidSalResult(*this, Result);

    FInteractionRecord MissingRecord;
    TestFalse(
        TEXT("A missing Provider name is not silently redirected"),
        Service.RecognizeProviderForTesting(
            FName(TEXT("not_registered")),
            Input,
            MissingRecord));
    TestFalse(
        TEXT("A missing Provider cannot build a result"),
        Service.BuildProviderForTesting(
            FName(TEXT("not_registered")),
            MissingRecord).IsValid());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FEditorContextBuiltInStandaloneBlueprintGraphTest,
    "Loomle.EditorContext.BuiltIn.StandaloneBlueprintGraph",
    EAutomationTestFlags::EditorContext
        | EAutomationTestFlags::EngineFilter)

bool FEditorContextBuiltInStandaloneBlueprintGraphTest::RunTest(
    const FString& Parameters)
{
    if (!FApp::CanEverRender())
    {
        AddInfo(TEXT(
            "Standalone Blueprint Context requires a rendered Editor window; "
            "UE 5.7 macOS UnrealEditor-Cmd uses FGenericWindow, whose "
            "Standalone layout persistence and close paths are unsupported."));
        return true;
    }

    const TSharedRef<FStandaloneBlueprintEditorContext> Context =
        MakeShared<FStandaloneBlueprintEditorContext>();
    FString InitializeError;
    if (!TestTrue(
            *FString::Printf(
                TEXT("Standalone Blueprint Context fixture initializes: %s"),
                *InitializeError),
            Context->Initialize(InitializeError)))
    {
        FString CleanupError;
        Context->Cleanup(CleanupError);
        return false;
    }

    ADD_LATENT_AUTOMATION_COMMAND(
        FRunStandaloneBlueprintEditorContextCommand(
            this,
            Context));
    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
