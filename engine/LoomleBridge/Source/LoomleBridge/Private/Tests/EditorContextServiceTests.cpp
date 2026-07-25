// Copyright 2026 Loomle contributors.

#if WITH_DEV_AUTOMATION_TESTS

#include "EditorContext/EditorContextService.h"

#include "Dom/JsonObject.h"
#include "Framework/Application/SlateApplication.h"
#include "Misc/AutomationTest.h"
#include "Sal/SalJson.h"
#include "Sal/SalObjectBuilder.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/SWidget.h"

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
        if (Statement.IsValid()
            && Statement->GetStringField(TEXT("kind")) == TEXT("comment")
            && Statement->GetStringField(TEXT("text")).Contains(Expected))
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

#endif // WITH_DEV_AUTOMATION_TESTS
