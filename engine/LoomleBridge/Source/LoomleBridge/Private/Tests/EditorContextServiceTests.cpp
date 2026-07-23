// Copyright 2026 Loomle contributors.

#if WITH_DEV_AUTOMATION_TESTS

#include "EditorContext/EditorContextService.h"

#include "Dom/JsonObject.h"
#include "Framework/Application/SlateApplication.h"
#include "Misc/AutomationTest.h"
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
        return Builder.BuildResult();
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

#endif // WITH_DEV_AUTOMATION_TESTS
