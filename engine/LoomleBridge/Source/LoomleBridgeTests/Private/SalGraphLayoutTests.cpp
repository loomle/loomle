// Copyright 2026 Loomle contributors.

#if WITH_DEV_AUTOMATION_TESTS

#include "Sal/Graph/SalGraphInterface.h"
#include "Sal/SalModel.h"
#include "LoomleTestObjectIteration.h"
#include "SalTestObjectModel.h"
#include "LoomleTestEditorState.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "BlueprintEditor.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraphSchema_K2.h"
#include "Editor.h"
#include "Editor/Transactor.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Docking/TabManager.h"
#include "GameFramework/Actor.h"
#include "GraphEditor.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformTime.h"
#include "K2Node_CustomEvent.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/App.h"
#include "Misc/AutomationTest.h"
#include "SGraphNode.h"
#include "SGraphPanel.h"
#include "SGraphPin.h"
#include "Settings/EditorStyleSettings.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "UObject/GarbageCollection.h"
#include "UObject/Package.h"
#include "UObject/UObjectHash.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/SWindow.h"

namespace
{
using namespace Loomle::Sal;

constexpr TCHAR LayoutUnavailableCode[] =
    TEXT("capability.layout_geometry_unavailable");
constexpr float SyntheticLayoutZoom = 0.75f;
constexpr float RenderedLayoutZoom = 0.25f;

FString GuidText(const FGuid& Guid)
{
    return Guid.ToString(EGuidFormats::DigitsWithHyphensLower);
}

FSalQuery ExactNodeLayoutQuery(const UEdGraphNode* Node)
{
    FSalQuery Query;
    Query.Alias = TEXT("graph");
    Query.Operation = MakeShared<FJsonObject>();
    Query.Operation->SetStringField(TEXT("kind"), TEXT("node"));
    Query.Operation->SetStringField(
        TEXT("id"),
        Node != nullptr ? GuidText(Node->NodeGuid) : FString());
    Query.With.Add(TEXT("layout"));
    return Query;
}

FSalResolvedTarget GraphTarget(
    UBlueprint* Blueprint,
    UEdGraph* Graph)
{
    FSalResolvedTarget Target;
    Target.Kind = ESalTargetKind::Graph;
    Target.Alias = TEXT("graph");
    Target.AssetPath =
        Blueprint != nullptr ? Blueprint->GetPathName() : FString();
    Target.Id =
        Graph != nullptr ? GuidText(Graph->GraphGuid) : FString();
    Target.Name =
        Graph != nullptr ? Graph->GetName() : FString();
    Target.Object = Graph;
    Target.Package =
        Blueprint != nullptr ? Blueprint->GetOutermost() : nullptr;
    Target.Blueprint = Blueprint;
    Target.Class =
        Blueprint != nullptr
            ? Blueprint->GeneratedClass.Get()
            : nullptr;
    Target.Graph = Graph;
    Target.Interfaces = {FName(TEXT("graph"))};
    return Target;
}

bool HasError(const TSharedPtr<FJsonObject>& Result)
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

bool TryReadDiagnosticReason(
    const TSharedPtr<FJsonObject>& Result,
    const FString& Code,
    FString& OutReason)
{
    OutReason.Reset();
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
        const TSharedPtr<FJsonObject>* Actual = nullptr;
        FString ActualCode;
        if (Value.IsValid()
            && Value->TryGetObject(Diagnostic)
            && Diagnostic != nullptr
            && (*Diagnostic)->TryGetStringField(
                TEXT("code"),
                ActualCode)
            && ActualCode == Code
            && (*Diagnostic)->TryGetObjectField(
                TEXT("actual"),
                Actual)
            && Actual != nullptr
            && (*Actual)->TryGetStringField(
                TEXT("reason"),
                OutReason))
        {
            return true;
        }
    }
    return false;
}

TArray<TSharedPtr<FJsonObject>> CallArgs(
    const TSharedPtr<FJsonObject>& Result,
    const FString& Callee)
{
    TArray<TSharedPtr<FJsonObject>> Calls;
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
        return Calls;
    }

    for (const TSharedPtr<FJsonValue>& StatementValue : *Statements)
    {
        const TSharedPtr<FJsonObject>* Statement = nullptr;
        const TSharedPtr<FJsonObject>* Call = nullptr;
        const TSharedPtr<FJsonObject>* Fields = nullptr;
        if (StatementValue.IsValid()
            && StatementValue->TryGetObject(Statement)
            && Statement != nullptr
            && (*Statement)->TryGetObjectField(TEXT("value"), Call)
            && Call != nullptr
            && Loomle::Tests::Sal::TryReadObjectExpr(
                *Call,
                Callee,
                Fields))
        {
            Calls.Add(*Fields);
        }
    }
    return Calls;
}

TSharedPtr<FJsonObject> CallArgsWithId(
    const TSharedPtr<FJsonObject>& Result,
    const FString& Callee,
    const FString& Id)
{
    for (const TSharedPtr<FJsonObject>& Args :
         CallArgs(Result, Callee))
    {
        FString ActualId;
        if (Args.IsValid()
            && Args->TryGetStringField(TEXT("id"), ActualId)
            && ActualId.Equals(Id, ESearchCase::IgnoreCase))
        {
            return Args;
        }
    }
    return nullptr;
}

bool ReadPoint(
    const TSharedPtr<FJsonObject>& Object,
    const TCHAR* Field,
    FVector2f& OutPoint)
{
    const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
    double X = 0.0;
    double Y = 0.0;
    if (!Object.IsValid()
        || !Object->TryGetArrayField(Field, Values)
        || Values == nullptr
        || Values->Num() != 2
        || !(*Values)[0].IsValid()
        || !(*Values)[1].IsValid()
        || !(*Values)[0]->TryGetNumber(X)
        || !(*Values)[1]->TryGetNumber(Y))
    {
        return false;
    }
    OutPoint = FVector2f(
        static_cast<float>(X),
        static_cast<float>(Y));
    return true;
}

bool ReadBounds(
    const TSharedPtr<FJsonObject>& Object,
    const TCHAR* Field,
    FSlateRect& OutBounds)
{
    const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
    double Left = 0.0;
    double Top = 0.0;
    double Right = 0.0;
    double Bottom = 0.0;
    if (!Object.IsValid()
        || !Object->TryGetArrayField(Field, Values)
        || Values == nullptr
        || Values->Num() != 4
        || !(*Values)[0].IsValid()
        || !(*Values)[1].IsValid()
        || !(*Values)[2].IsValid()
        || !(*Values)[3].IsValid()
        || !(*Values)[0]->TryGetNumber(Left)
        || !(*Values)[1]->TryGetNumber(Top)
        || !(*Values)[2]->TryGetNumber(Right)
        || !(*Values)[3]->TryGetNumber(Bottom))
    {
        return false;
    }
    OutBounds = FSlateRect(
        static_cast<float>(Left),
        static_cast<float>(Top),
        static_cast<float>(Right),
        static_cast<float>(Bottom));
    return true;
}

bool ReadName(
    const TSharedPtr<FJsonValue>& Value,
    FString& OutName)
{
    OutName.Reset();
    const TSharedPtr<FJsonObject>* Object = nullptr;
    FString Kind;
    return Value.IsValid()
        && Value->TryGetObject(Object)
        && Object != nullptr
        && (*Object).IsValid()
        && (*Object)->TryGetStringField(TEXT("kind"), Kind)
        && Kind == TEXT("name")
        && (*Object)->TryGetStringField(TEXT("name"), OutName);
}

bool HasGeometryReason(
    const TSharedPtr<FJsonObject>& Fields,
    const FString& Expected)
{
    const TArray<TSharedPtr<FJsonValue>>* Reasons = nullptr;
    if (!Fields.IsValid()
        || !Fields->TryGetArrayField(
            TEXT("geometryReasons"),
            Reasons)
        || Reasons == nullptr)
    {
        return false;
    }
    for (const TSharedPtr<FJsonValue>& Value : *Reasons)
    {
        FString Reason;
        if (ReadName(Value, Reason)
            && Reason == Expected)
        {
            return true;
        }
    }
    return false;
}

bool HasAnyVisualField(const TSharedPtr<FJsonObject>& Fields)
{
    static const TCHAR* Names[] = {
        TEXT("visualBounds"),
        TEXT("visualState"),
        TEXT("visualCenter"),
        TEXT("placementAnchor"),
        TEXT("placementAnchorKind"),
        TEXT("geometryReasons")};
    if (!Fields.IsValid())
    {
        return false;
    }
    for (const TCHAR* Name : Names)
    {
        if (Fields->HasField(Name))
        {
            return true;
        }
    }
    return false;
}

bool ResultHasAnyVisualField(
    const TSharedPtr<FJsonObject>& Result)
{
    for (const FString& Callee : {TEXT("node"), TEXT("pin")})
    {
        for (const TSharedPtr<FJsonObject>& Fields :
             CallArgs(Result, Callee))
        {
            if (HasAnyVisualField(Fields))
            {
                return true;
            }
        }
    }
    return false;
}

bool NearlyEqual(
    const FVector2f& A,
    const FVector2f& B,
    const float Tolerance)
{
    return FMath::IsNearlyEqual(A.X, B.X, Tolerance)
        && FMath::IsNearlyEqual(A.Y, B.Y, Tolerance);
}

bool NearlyEqual(
    const FSlateRect& A,
    const FSlateRect& B,
    const float Tolerance)
{
    return FMath::IsNearlyEqual(A.Left, B.Left, Tolerance)
        && FMath::IsNearlyEqual(A.Top, B.Top, Tolerance)
        && FMath::IsNearlyEqual(A.Right, B.Right, Tolerance)
        && FMath::IsNearlyEqual(A.Bottom, B.Bottom, Tolerance);
}

FString RectText(const FSlateRect& Rect)
{
    return FString::Printf(
        TEXT("[%g, %g, %g, %g]"),
        Rect.Left,
        Rect.Top,
        Rect.Right,
        Rect.Bottom);
}

bool ExpectPoint(
    FAutomationTestBase& Test,
    const FString& Label,
    const TSharedPtr<FJsonObject>& Fields,
    const TCHAR* Field,
    const FVector2f& Expected,
    const float Tolerance = 1.5f)
{
    FVector2f Actual;
    if (!ReadPoint(Fields, Field, Actual))
    {
        Test.AddError(Label + TEXT(" is absent or malformed."));
        return false;
    }
    if (!NearlyEqual(Actual, Expected, Tolerance))
    {
        Test.AddError(FString::Printf(
            TEXT("%s differs: actual [%g, %g], expected [%g, %g]."),
            *Label,
            Actual.X,
            Actual.Y,
            Expected.X,
            Expected.Y));
        return false;
    }
    return true;
}

bool ExpectBounds(
    FAutomationTestBase& Test,
    const FString& Label,
    const TSharedPtr<FJsonObject>& Fields,
    const TCHAR* Field,
    const FSlateRect& Expected,
    const float Tolerance = 1.5f)
{
    FSlateRect Actual;
    if (!ReadBounds(Fields, Field, Actual))
    {
        Test.AddError(Label + TEXT(" is absent or malformed."));
        return false;
    }
    if (!NearlyEqual(Actual, Expected, Tolerance))
    {
        Test.AddError(FString::Printf(
            TEXT("%s differs: actual %s, expected %s."),
            *Label,
            *RectText(Actual),
            *RectText(Expected)));
        return false;
    }
    return true;
}

bool WidgetBoundsInGraph(
    const SGraphPanel& Panel,
    const SWidget& Widget,
    FSlateRect& OutBounds)
{
    const FGeometry& PanelGeometry = Panel.GetTickSpaceGeometry();
    const FSlateRect Absolute =
        Widget.GetTickSpaceGeometry().GetLayoutBoundingRect();
    const FVector2f PanelTopLeft =
        PanelGeometry.AbsoluteToLocal(
            FVector2f(Absolute.Left, Absolute.Top));
    const FVector2f PanelBottomRight =
        PanelGeometry.AbsoluteToLocal(
            FVector2f(Absolute.Right, Absolute.Bottom));
    const FVector2f GraphTopLeft =
        Panel.PanelCoordToGraphCoord(PanelTopLeft);
    const FVector2f GraphBottomRight =
        Panel.PanelCoordToGraphCoord(PanelBottomRight);
    OutBounds = FSlateRect(GraphTopLeft, GraphBottomRight);
    return FMath::IsFinite(OutBounds.Left)
        && FMath::IsFinite(OutBounds.Top)
        && FMath::IsFinite(OutBounds.Right)
        && FMath::IsFinite(OutBounds.Bottom)
        && OutBounds.Right > OutBounds.Left
        && OutBounds.Bottom > OutBounds.Top;
}

bool UnloadTestPackage(
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
        Loomle::Tests::IncludeNestedObjects);
    CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);
    if (FindPackage(nullptr, *PackageName) != nullptr)
    {
        OutError =
            TEXT("Fixture package remained loaded: ")
            + PackageName;
        return false;
    }
    return true;
}

class FGraphLayoutFixture
{
public:
    FGraphLayoutFixture()
    {
        const FString Token =
            FGuid::NewGuid().ToString(EGuidFormats::Digits);
        Package = CreatePackage(*FString::Printf(
            TEXT("/Game/LoomleTests/SalGraphLayout_%s"),
            *Token));
        Blueprint = FKismetEditorUtilities::CreateBlueprint(
            AActor::StaticClass(),
            Package,
            *FString::Printf(
                TEXT("BP_SalGraphLayout_%s"),
                *Token),
            BPTYPE_Normal,
            UBlueprint::StaticClass(),
            UBlueprintGeneratedClass::StaticClass(),
            NAME_None);
        if (Blueprint == nullptr)
        {
            return;
        }

        Graph = FBlueprintEditorUtils::FindEventGraph(Blueprint);
        if (Graph == nullptr)
        {
            return;
        }

        FGraphNodeCreator<UK2Node_CustomEvent> Creator(*Graph);
        Node = Creator.CreateNode(false);
        Node->CustomFunctionName = TEXT("LayoutGeometryCoverage");
        Node->NodePosX = 320;
        Node->NodePosY = 144;
        Creator.Finalize();
        Node->NodeWidth = 240;
        Node->NodeHeight = 120;

        FEdGraphPinType StringType;
        StringType.PinCategory = UEdGraphSchema_K2::PC_String;
        NativeHiddenPin = Node->CreateUserDefinedPin(
            TEXT("NativeHidden"),
            StringType,
            EGPD_Output);
        AdvancedPin = Node->CreateUserDefinedPin(
            TEXT("AdvancedHidden"),
            StringType,
            EGPD_Output);
        VisiblePin = Node->CreateUserDefinedPin(
            TEXT("VisibleValue"),
            StringType,
            EGPD_Output);
        if (NativeHiddenPin != nullptr)
        {
            NativeHiddenPin->bHidden = true;
        }
        if (AdvancedPin != nullptr)
        {
            AdvancedPin->bAdvancedView = true;
        }
        Node->AdvancedPinDisplay = ENodeAdvancedPins::Hidden;

        Package->SetDirtyFlag(false);
    }

    ~FGraphLayoutFixture()
    {
        FString Ignored;
        Cleanup(Ignored);
    }

    FGraphLayoutFixture(const FGraphLayoutFixture&) = delete;
    FGraphLayoutFixture& operator=(
        const FGraphLayoutFixture&) = delete;

    bool IsValid() const
    {
        return Package != nullptr
            && Blueprint != nullptr
            && Graph != nullptr
            && Graph->GraphGuid.IsValid()
            && Node != nullptr
            && Node->NodeGuid.IsValid()
            && NativeHiddenPin != nullptr
            && NativeHiddenPin->PinId.IsValid()
            && AdvancedPin != nullptr
            && AdvancedPin->PinId.IsValid()
            && VisiblePin != nullptr
            && VisiblePin->PinId.IsValid();
    }

    bool Cleanup(FString& OutError)
    {
        if (bCleaned)
        {
            OutError.Reset();
            return true;
        }
        bCleaned = true;

        UPackage* PackageToUnload = Package;
        if (Blueprint != nullptr)
        {
            Blueprint->ClearFlags(RF_Public | RF_Standalone);
        }
        Package = nullptr;
        Blueprint = nullptr;
        Graph = nullptr;
        Node = nullptr;
        NativeHiddenPin = nullptr;
        AdvancedPin = nullptr;
        VisiblePin = nullptr;
        return UnloadTestPackage(PackageToUnload, OutError);
    }

    UPackage* Package = nullptr;
    UBlueprint* Blueprint = nullptr;
    UEdGraph* Graph = nullptr;
    UK2Node_CustomEvent* Node = nullptr;
    UEdGraphPin* NativeHiddenPin = nullptr;
    UEdGraphPin* AdvancedPin = nullptr;
    UEdGraphPin* VisiblePin = nullptr;

private:
    bool bCleaned = false;
};

bool RequireEditor(
    FAutomationTestBase& Test,
    const FString& Surface,
    const bool bRequireSlate)
{
    if (GEditor == nullptr)
    {
        Test.AddError(Surface + TEXT(" requires GEditor."));
        return false;
    }
    if (GEditor->IsPlaySessionInProgress())
    {
        Test.AddError(
            Surface
            + TEXT(" must run outside PIE."));
        return false;
    }
    if (bRequireSlate
        && !FSlateApplication::IsInitialized())
    {
        Test.AddError(
            Surface
            + TEXT(" requires an initialized Slate application."));
        return false;
    }
    return true;
}

struct FPinOracle
{
    FSlateRect Bounds;
    FVector2f Center;
    FVector2f Anchor;
    FString AnchorKind;
};

bool BuildPinOracle(
    SGraphPanel& Panel,
    const TSharedRef<SGraphPin>& PinWidget,
    const UEdGraphPin* Pin,
    FPinOracle& Out)
{
    if (!WidgetBoundsInGraph(
            Panel,
            PinWidget.Get(),
            Out.Bounds))
    {
        return false;
    }

    Out.Center = FVector2f(
        (Out.Bounds.Left + Out.Bounds.Right) * 0.5f,
        (Out.Bounds.Top + Out.Bounds.Bottom) * 0.5f);
    const TSharedPtr<SWidget> Image =
        PinWidget->GetPinImageWidget();
    FSlateRect ImageBounds;
    if (Image.IsValid()
        && WidgetBoundsInGraph(Panel, *Image, ImageBounds))
    {
        Out.Anchor = FVector2f(
            (ImageBounds.Left + ImageBounds.Right) * 0.5f,
            (ImageBounds.Top + ImageBounds.Bottom) * 0.5f);
        Out.AnchorKind = TEXT("pin_image_center");
    }
    else
    {
        Out.Anchor = FVector2f(
            Pin != nullptr && Pin->Direction == EGPD_Output
                ? Out.Bounds.Right
                : Out.Bounds.Left,
            Out.Center.Y);
        Out.AnchorKind = TEXT("pin_row_edge_midpoint");
    }
    return true;
}

enum class EGraphLayoutSurfaceMode
{
    HeadlessSynthetic,
    RenderedBlueprintEditor
};

class FLiveGraphLayoutContext
{
public:
    explicit FLiveGraphLayoutContext(
        const EGraphLayoutSurfaceMode InMode)
        : Mode(InMode)
    {
    }

    ~FLiveGraphLayoutContext()
    {
        FString Ignored;
        Cleanup(Ignored);
    }

    bool Initialize(FString& OutError)
    {
        OutError.Reset();
        if (!Transactions.Initialize())
        {
            OutError =
                TEXT("Could not install an isolated transaction buffer.");
            return false;
        }
        Fixture = MakeUnique<FGraphLayoutFixture>();
        if (!Fixture->IsValid())
        {
            OutError = TEXT("Could not create the Graph layout fixture.");
            return false;
        }

        if (Mode == EGraphLayoutSurfaceMode::HeadlessSynthetic)
        {
            HeadlessDrawingCVar =
                IConsoleManager::Get().FindConsoleVariable(
                    TEXT("Slate.SkipWidgetDrawingInHeadlessMode"));
            if (HeadlessDrawingCVar == nullptr)
            {
                OutError = TEXT(
                    "Slate.SkipWidgetDrawingInHeadlessMode is unavailable.");
                return false;
            }
            HeadlessDrawingBefore = HeadlessDrawingCVar->GetInt();
            HeadlessDrawingCVar->ReplaceCurrentPriorityAndTag(
                0,
                ECVF_SetByConsole,
                ECVF_SetByConstructor);
            bHeadlessDrawingOverridden = true;
            if (HeadlessDrawingCVar->GetInt() != 0)
            {
                OutError = TEXT(
                    "Could not enable Slate drawing for the headless fixture.");
                return false;
            }

            GraphEditor =
                SNew(SGraphEditor)
                .GraphToEdit(Fixture->Graph);
            GraphEditor->SetViewLocation(
                FVector2f::ZeroVector,
                SyntheticLayoutZoom);
            Window =
                SNew(SWindow)
                .Title(FText::FromString(
                    TEXT("Loomle Graph Layout Automation")))
                .ClientSize(FVector2f(960.0f, 720.0f))
                .SupportsMaximize(false)
                .SupportsMinimize(false)
                [
                    GraphEditor.ToSharedRef()
                ];
            FSlateApplication::Get().AddWindow(
                Window.ToSharedRef(),
                true);
            FSlateApplication::Get().ForceRedrawWindow(
                Window.ToSharedRef());
        }
        else
        {
            PreviousFocus =
                FSlateApplication::Get().GetKeyboardFocusedWidget();
            StyleSettings =
                GetMutableDefault<UEditorStyleSettings>();
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

            Fixture->Blueprint->bIsNewlyCreated = false;
            Fixture->Blueprint->bForceFullEditor = true;
            Fixture->Blueprint->LastEditedDocuments.Reset();
            Fixture->Blueprint->LastEditedDocuments.Add(
                Fixture->Graph);
            FAssetRegistryModule::AssetCreated(Fixture->Blueprint);
            bAssetRegistered = true;
            Fixture->Package->SetDirtyFlag(false);

            UAssetEditorSubsystem* Editors =
                GEditor->GetEditorSubsystem<UAssetEditorSubsystem>();
            TSharedPtr<IToolkitHost> NoToolkitHost;
            if (Editors == nullptr
                || !Editors->OpenEditorForAsset(
                    Fixture->Blueprint,
                    EToolkitMode::Standalone,
                    NoToolkitHost,
                    false))
            {
                OutError = TEXT(
                    "UAssetEditorSubsystem could not open the Blueprint in Standalone mode.");
                return false;
            }
        }
        StartSeconds = FPlatformTime::Seconds();
        return true;
    }

    bool IsTimedOut() const
    {
        const double TimeoutSeconds =
            Mode == EGraphLayoutSurfaceMode::RenderedBlueprintEditor
                ? 30.0
                : 12.0;
        return FPlatformTime::Seconds() - StartSeconds > TimeoutSeconds;
    }

    bool IsSurfaceReady()
    {
        if (!Fixture.IsValid())
        {
            LastUnavailableReason = TEXT("fixture_missing");
            return false;
        }
        if (Mode == EGraphLayoutSurfaceMode::RenderedBlueprintEditor
            && !PrepareRenderedSurface())
        {
            return false;
        }
        if (!GraphEditor.IsValid() || !Window.IsValid())
        {
            LastUnavailableReason = TEXT("surface_missing");
            return false;
        }
        if (!Window->IsVisible())
        {
            LastUnavailableReason = TEXT("window_not_visible");
            return false;
        }
        SGraphPanel* Panel = GraphEditor->GetGraphPanel();
        if (Panel == nullptr
            || Panel->GetGraphObj() != Fixture->Graph
            || GraphEditor->GetCurrentGraph() != Fixture->Graph)
        {
            LastUnavailableReason = TEXT("panel_not_bound");
            return false;
        }
        const FVector2f PanelSize =
            UE::Slate::CastToVector2f(
                Panel->GetTickSpaceGeometry().GetLocalSize());
        if (PanelSize.X <= 0.0f || PanelSize.Y <= 0.0f)
        {
            LastUnavailableReason = TEXT("panel_geometry_empty");
            return false;
        }
        if (Panel->HasMoved()
            || Panel->HasDeferredObjectFocus()
            || Panel->HasDeferredZoomDestination())
        {
            LastUnavailableReason = TEXT("panel_not_stable");
            return false;
        }

        const TSharedPtr<SGraphNode> NodeWidget =
            Panel->GetNodeWidgetFromGuid(Fixture->Node->NodeGuid);
        if (!NodeWidget.IsValid()
            || NodeWidget->GetNodeObj() != Fixture->Node
            || NodeWidget->GetDesiredSize().X <= 0.0f
            || NodeWidget->GetDesiredSize().Y <= 0.0f)
        {
            LastUnavailableReason = TEXT("node_widget_not_ready");
            return false;
        }

        const TSharedPtr<SGraphPin> VisibleWidget =
            NodeWidget->FindWidgetForPin(Fixture->VisiblePin);
        if (!VisibleWidget.IsValid()
            || !VisibleWidget->GetVisibility().IsVisible())
        {
            LastUnavailableReason = TEXT("visible_pin_not_ready");
            return false;
        }
        LastUnavailableReason.Reset();
        return true;
    }

    bool TryRunAssertions(FAutomationTestBase& Test)
    {
        SGraphPanel* Panel = GraphEditor->GetGraphPanel();
        if (Panel == nullptr)
        {
            LastUnavailableReason = TEXT("panel_missing");
            return false;
        }
        const TSharedPtr<SGraphNode> NodeWidget =
            Panel->GetNodeWidgetFromGuid(Fixture->Node->NodeGuid);
        const TSharedPtr<SGraphPin> VisibleWidget =
            NodeWidget.IsValid()
                ? NodeWidget->FindWidgetForPin(Fixture->VisiblePin)
                : nullptr;
        if (!NodeWidget.IsValid() || !VisibleWidget.IsValid())
        {
            LastUnavailableReason = TEXT("widget_missing");
            return false;
        }

        FVector2f NodeMin;
        FVector2f NodeMax;
        FPinOracle PinOracle;
        if (!Panel->GetBoundsForNode(
                Fixture->Node,
                NodeMin,
                NodeMax)
            || !BuildPinOracle(
                *Panel,
                VisibleWidget.ToSharedRef(),
                Fixture->VisiblePin,
                PinOracle))
        {
            LastUnavailableReason = TEXT("oracle_not_ready");
            return false;
        }

        FVector2f ViewBefore;
        float ZoomBefore = 0.0f;
        GraphEditor->GetViewLocation(ViewBefore, ZoomBefore);
        Test.TestFalse(
            TEXT("Live layout oracle exercises a non-unit Graph zoom"),
            FMath::IsNearlyEqual(
                ZoomBefore,
                1.0f,
                KINDA_SMALL_NUMBER));
        if (Mode
            == EGraphLayoutSurfaceMode::RenderedBlueprintEditor)
        {
            Test.TestTrue(
                TEXT("Rendered layout oracle exercises UE's compact low-LOD Pin presentation"),
                Panel->GetCurrentLOD()
                    <= EGraphRenderingLOD::LowDetail);
        }
        const bool bDirtyBefore = Fixture->Package->IsDirty();
        const int32 QueueLengthBefore =
            GEditor->Trans->GetQueueLength();
        const int32 UndoCountBefore =
            GEditor->Trans->GetUndoCount();
        const FIntPoint PositionBefore(
            Fixture->Node->NodePosX,
            Fixture->Node->NodePosY);

        const FSalResolvedTarget Target =
            GraphTarget(Fixture->Blueprint, Fixture->Graph);
        const TSharedPtr<FJsonObject> Result =
            FSalGraphInterface::Query(
                ExactNodeLayoutQuery(Fixture->Node),
                Target);
        FString UnavailableReason;
        if (TryReadDiagnosticReason(
                Result,
                LayoutUnavailableCode,
                UnavailableReason))
        {
            LastUnavailableReason = UnavailableReason;
            return false;
        }
        if (HasError(Result))
        {
            Test.AddError(
                TEXT("Live layout Query returned an error diagnostic."));
            return true;
        }

        const FString NodeId = GuidText(Fixture->Node->NodeGuid);
        const FString VisiblePinId =
            GuidText(Fixture->VisiblePin->PinId);
        const FString NativeHiddenPinId =
            GuidText(Fixture->NativeHiddenPin->PinId);
        const FString AdvancedPinId =
            GuidText(Fixture->AdvancedPin->PinId);
        const TSharedPtr<FJsonObject> NodeFields =
            CallArgsWithId(Result, TEXT("node"), NodeId);
        const TSharedPtr<FJsonObject> VisibleFields =
            CallArgsWithId(Result, TEXT("pin"), VisiblePinId);
        const TSharedPtr<FJsonObject> NativeHiddenFields =
            CallArgsWithId(
                Result,
                TEXT("pin"),
                NativeHiddenPinId);
        const TSharedPtr<FJsonObject> AdvancedFields =
            CallArgsWithId(
                Result,
                TEXT("pin"),
                AdvancedPinId);

        Test.TestNotNull(
            TEXT("Live response contains the exact Node"),
            NodeFields.Get());
        Test.TestNotNull(
            TEXT("Live response contains the visible Pin"),
            VisibleFields.Get());
        Test.TestNotNull(
            TEXT("Live response contains the native-hidden Pin"),
            NativeHiddenFields.Get());
        Test.TestNotNull(
            TEXT("Live response contains the advanced-hidden Pin"),
            AdvancedFields.Get());

        const FSlateRect ExpectedNodeBounds(NodeMin, NodeMax);
        ExpectBounds(
            Test,
            TEXT("Node visualBounds"),
            NodeFields,
            TEXT("visualBounds"),
            ExpectedNodeBounds);

        FString VisibleState;
        Test.TestTrue(
            TEXT("Visible Pin is measured"),
            VisibleFields.IsValid()
                && ReadName(
                    VisibleFields->TryGetField(
                        TEXT("visualState")),
                    VisibleState)
                && VisibleState == TEXT("measured"));
        ExpectBounds(
            Test,
            TEXT("Visible Pin visualBounds"),
            VisibleFields,
            TEXT("visualBounds"),
            PinOracle.Bounds);
        ExpectPoint(
            Test,
            TEXT("Visible Pin visualCenter"),
            VisibleFields,
            TEXT("visualCenter"),
            PinOracle.Center);
        ExpectPoint(
            Test,
            TEXT("Visible Pin placementAnchor"),
            VisibleFields,
            TEXT("placementAnchor"),
            PinOracle.Anchor);
        FString AnchorKind;
        Test.TestTrue(
            TEXT("Visible Pin placementAnchorKind matches its Slate oracle"),
            VisibleFields.IsValid()
                && ReadName(
                    VisibleFields->TryGetField(
                        TEXT("placementAnchorKind")),
                    AnchorKind)
                && AnchorKind == PinOracle.AnchorKind);

        FString NativeState;
        Test.TestTrue(
            TEXT("Native-hidden Pin is intentionally not presented"),
            NativeHiddenFields.IsValid()
                && ReadName(
                    NativeHiddenFields->TryGetField(
                        TEXT("visualState")),
                    NativeState)
                && NativeState
                    == TEXT("intentionally_not_presented")
                && HasGeometryReason(
                    NativeHiddenFields,
                    TEXT("hidden_native"))
                && !NativeHiddenFields->HasField(
                    TEXT("visualBounds")));
        FString AdvancedState;
        Test.TestTrue(
            TEXT("Advanced-hidden Pin is intentionally not presented"),
            AdvancedFields.IsValid()
                && ReadName(
                    AdvancedFields->TryGetField(
                        TEXT("visualState")),
                    AdvancedState)
                && AdvancedState
                    == TEXT("intentionally_not_presented")
                && HasGeometryReason(
                    AdvancedFields,
                    TEXT("hidden_advanced"))
                && !AdvancedFields->HasField(
                    TEXT("visualBounds")));

        FVector2f ViewAfter;
        float ZoomAfter = 0.0f;
        GraphEditor->GetViewLocation(ViewAfter, ZoomAfter);
        Test.TestTrue(
            TEXT("Layout Query does not change the Graph viewport"),
            NearlyEqual(ViewBefore, ViewAfter, KINDA_SMALL_NUMBER)
                && FMath::IsNearlyEqual(
                    ZoomBefore,
                    ZoomAfter,
                    KINDA_SMALL_NUMBER));
        Test.TestEqual(
            TEXT("Layout Query does not dirty the package"),
            Fixture->Package->IsDirty(),
            bDirtyBefore);
        Test.TestEqual(
            TEXT("Layout Query does not append a transaction"),
            GEditor->Trans->GetQueueLength(),
            QueueLengthBefore);
        Test.TestEqual(
            TEXT("Layout Query does not move the undo cursor"),
            GEditor->Trans->GetUndoCount(),
            UndoCountBefore);
        Test.TestEqual(
            TEXT("Layout Query does not change Node X"),
            Fixture->Node->NodePosX,
            PositionBefore.X);
        Test.TestEqual(
            TEXT("Layout Query does not change Node Y"),
            Fixture->Node->NodePosY,
            PositionBefore.Y);

        // Deliberately desynchronize the UObject Pin inventory from the
        // already-built SGraphNode. The exact response must fall back as one
        // unit instead of mixing authoritative and missing visual geometry.
        Fixture->NativeHiddenPin->bHidden = false;
        const TSharedPtr<FJsonObject> MismatchResult =
            FSalGraphInterface::Query(
                ExactNodeLayoutQuery(Fixture->Node),
                Target);
        Fixture->NativeHiddenPin->bHidden = true;

        FString MismatchReason;
        Test.TestTrue(
            TEXT("A missing Pin widget produces the layout warning"),
            TryReadDiagnosticReason(
                MismatchResult,
                LayoutUnavailableCode,
                MismatchReason)
                && MismatchReason
                    == TEXT("pin_widget_unavailable"));
        Test.TestFalse(
            TEXT("Pin widget mismatch strips visual fields from the whole response"),
            ResultHasAnyVisualField(MismatchResult));
        Test.TestFalse(
            TEXT("Pin widget mismatch remains a usable stored-layout response"),
            HasError(MismatchResult));
        const TSharedPtr<FJsonObject> MismatchNodeFields =
            CallArgsWithId(
                MismatchResult,
                TEXT("node"),
                NodeId);
        FVector2f MismatchAt;
        FVector2f MismatchSize;
        Test.TestTrue(
            TEXT("Pin widget mismatch preserves stored Node layout"),
            ReadPoint(
                MismatchNodeFields,
                TEXT("at"),
                MismatchAt)
                && MismatchAt
                    == FVector2f(
                        Fixture->Node->NodePosX,
                        Fixture->Node->NodePosY)
                && ReadPoint(
                    MismatchNodeFields,
                    TEXT("size"),
                    MismatchSize)
                && MismatchSize
                    == FVector2f(
                        Fixture->Node->NodeWidth,
                        Fixture->Node->NodeHeight));
        Test.TestEqual(
            TEXT("Fallback Query does not dirty the package"),
            Fixture->Package->IsDirty(),
            bDirtyBefore);
        Test.TestEqual(
            TEXT("Fallback Query does not append a transaction"),
            GEditor->Trans->GetQueueLength(),
            QueueLengthBefore);
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

        bool bEditorClosed = true;
        if (Mode == EGraphLayoutSurfaceMode::RenderedBlueprintEditor
            && GEditor != nullptr
            && Fixture.IsValid()
            && Fixture->Blueprint != nullptr)
        {
            UAssetEditorSubsystem* Editors =
                GEditor->GetEditorSubsystem<UAssetEditorSubsystem>();
            if (Editors != nullptr)
            {
                if (Fixture->Package != nullptr)
                {
                    Fixture->Package->SetDirtyFlag(false);
                }
                Editors->CloseAllEditorsForAsset(Fixture->Blueprint);
                bEditorClosed =
                    Editors->FindEditorsForAsset(
                        Fixture->Blueprint).IsEmpty();
            }
        }
        else if (Window.IsValid()
            && FSlateApplication::IsInitialized())
        {
            FSlateApplication::Get().DestroyWindowImmediately(
                Window.ToSharedRef());
        }
        GraphEditor.Reset();
        GraphTab.Reset();
        OwnerTab.Reset();
        Window.Reset();
        Editor = nullptr;

        if (bOpenLocationOverridden && StyleSettings != nullptr)
        {
            StyleSettings->AssetEditorOpenLocation =
                PreviousOpenLocation;
        }
        StyleSettings = nullptr;
        bOpenLocationOverridden = false;
        if (bAssetRegistered
            && Fixture.IsValid()
            && Fixture->Blueprint != nullptr)
        {
            FAssetRegistryModule::AssetDeleted(
                Fixture->Blueprint);
        }
        bAssetRegistered = false;

        if (HeadlessDrawingCVar != nullptr
            && bHeadlessDrawingOverridden)
        {
            HeadlessDrawingCVar->ReplaceCurrentPriorityAndTag(
                HeadlessDrawingBefore,
                ECVF_SetByConsole,
                ECVF_SetByConstructor);
        }
        HeadlessDrawingCVar = nullptr;
        bHeadlessDrawingOverridden = false;
        Transactions.Restore();

        bool bFixtureCleaned = true;
        if (Fixture.IsValid())
        {
            bFixtureCleaned = Fixture->Cleanup(OutError);
            Fixture.Reset();
        }
        if (!bEditorClosed && OutError.IsEmpty())
        {
            OutError = TEXT(
                "The Standalone Blueprint Editor remained open during cleanup.");
        }
        if (FSlateApplication::IsInitialized()
            && PreviousFocus.IsValid())
        {
            FSlateApplication::Get().SetKeyboardFocus(
                PreviousFocus,
                EFocusCause::SetDirectly);
        }
        PreviousFocus.Reset();
        return bEditorClosed && bFixtureCleaned;
    }

    FString LastUnavailableReason;

private:
    bool PrepareRenderedSurface()
    {
        if (Editor == nullptr)
        {
            UAssetEditorSubsystem* Editors =
                GEditor != nullptr
                    ? GEditor->GetEditorSubsystem<
                        UAssetEditorSubsystem>()
                    : nullptr;
            IAssetEditorInstance* Instance =
                Editors != nullptr && Fixture.IsValid()
                    ? Editors->FindEditorForAsset(
                        Fixture->Blueprint,
                        false)
                    : nullptr;
            if (Instance == nullptr)
            {
                LastUnavailableReason =
                    TEXT("asset_editor_not_registered");
                return false;
            }
            if (Instance->GetEditorName()
                != FName(TEXT("BlueprintEditor")))
            {
                LastUnavailableReason =
                    TEXT("wrong_asset_editor_type");
                return false;
            }
            Editor = static_cast<FBlueprintEditor*>(Instance);
        }

        const TSharedPtr<FTabManager> Manager =
            Editor->GetAssociatedTabManager();
        if (!OwnerTab.IsValid())
        {
            OwnerTab = Manager.IsValid()
                ? Manager->GetOwnerTab()
                : nullptr;
            if (!OwnerTab.IsValid())
            {
                LastUnavailableReason =
                    TEXT("owner_major_tab_missing");
                return false;
            }
        }

        if (!GraphEditor.IsValid()
            || GraphEditor->GetCurrentGraph() != Fixture->Graph)
        {
            FocusRenderedSurface();
            if (!GraphEditor.IsValid())
            {
                LastUnavailableReason =
                    TEXT("graph_document_not_open");
                return false;
            }
        }

        const bool bGraphTabMatchesEditor =
            GraphTab.IsValid()
            && &GraphTab->GetContent().Get()
                == static_cast<SWidget*>(GraphEditor.Get());
        if (!bGraphTabMatchesEditor)
        {
            TArray<TSharedPtr<SDockTab>> GraphTabs;
            Editor->FindOpenTabsContainingDocument(
                Fixture->Graph,
                GraphTabs);
            GraphTab.Reset();
            for (const TSharedPtr<SDockTab>& Candidate : GraphTabs)
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
        if (!GraphTab.IsValid())
        {
            LastUnavailableReason = TEXT("graph_tab_not_open");
            return false;
        }

        if (Editor->GetFocusedGraph() != Fixture->Graph
            || !OwnerTab->IsForeground()
            || !GraphTab->IsForeground())
        {
            FocusRenderedSurface();
            LastUnavailableReason = TEXT("graph_document_not_foreground");
            return false;
        }

        Window = FSlateApplication::Get().FindWidgetWindow(
            GraphEditor.ToSharedRef());
        if (!Window.IsValid()
            || Window->GetType() != EWindowType::Normal
            || !Window->IsVisible()
            || Window->IsWindowMinimized()
            || !Window->GetNativeWindow().IsValid())
        {
            LastUnavailableReason =
                TEXT("graph_window_not_interactive");
            return false;
        }
        if (FSlateApplication::Get()
                .GetActiveTopLevelRegularWindow()
            != Window)
        {
            FocusRenderedSurface();
            LastUnavailableReason = TEXT("graph_window_not_active");
            return false;
        }

        const TSharedPtr<SWidget> FocusedWidget =
            FSlateApplication::Get().GetKeyboardFocusedWidget();
        FWidgetPath FocusPath;
        if (!FocusedWidget.IsValid()
            || !FSlateApplication::Get()
                .GeneratePathToWidgetUnchecked(
                    FocusedWidget.ToSharedRef(),
                    FocusPath)
            || !FocusPath.IsValid()
            || !FocusPath.ContainsWidget(GraphEditor.Get()))
        {
            FocusRenderedSurface();
            LastUnavailableReason =
                TEXT("focus_path_outside_graph");
            return false;
        }

        if (!bViewConfigured)
        {
            GraphEditor->SetViewLocation(
                FVector2f::ZeroVector,
                RenderedLayoutZoom);
            FSlateApplication::Get().ForceRedrawWindow(
                Window.ToSharedRef());
            bViewConfigured = true;
            LastUnavailableReason = TEXT("graph_view_configuring");
            return false;
        }
        return true;
    }

    void FocusRenderedSurface()
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
            && Fixture.IsValid()
            && Fixture->Graph != nullptr)
        {
            const TSharedPtr<SGraphEditor> FocusedGraphEditor =
                Editor->OpenGraphAndBringToFront(
                    Fixture->Graph,
                    true);
            if (FocusedGraphEditor.IsValid()
                && FocusedGraphEditor != GraphEditor)
            {
                GraphEditor = FocusedGraphEditor;
                GraphTab.Reset();
                bViewConfigured = false;
            }
        }
    }

    EGraphLayoutSurfaceMode Mode;
    Loomle::Tests::FScopedIsolatedTransactor Transactions;
    TUniquePtr<FGraphLayoutFixture> Fixture;
    FBlueprintEditor* Editor = nullptr;
    TSharedPtr<SGraphEditor> GraphEditor;
    TSharedPtr<SDockTab> GraphTab;
    TSharedPtr<SDockTab> OwnerTab;
    TSharedPtr<SWindow> Window;
    UEditorStyleSettings* StyleSettings = nullptr;
    EAssetEditorOpenLocation PreviousOpenLocation =
        EAssetEditorOpenLocation::Default;
    TSharedPtr<SWidget> PreviousFocus;
    IConsoleVariable* HeadlessDrawingCVar = nullptr;
    int32 HeadlessDrawingBefore = 1;
    double StartSeconds = 0.0;
    bool bHeadlessDrawingOverridden = false;
    bool bOpenLocationOverridden = false;
    bool bAssetRegistered = false;
    bool bViewConfigured = false;
    bool bCleaned = false;
};

class FRunLiveGraphLayoutCommand final
    : public IAutomationLatentCommand
{
public:
    FRunLiveGraphLayoutCommand(
        FAutomationTestBase* InTest,
        TSharedRef<FLiveGraphLayoutContext> InContext)
        : Test(InTest)
        , Context(MoveTemp(InContext))
    {
    }

    virtual bool Update() override
    {
        if (Context->IsSurfaceReady()
            && Context->TryRunAssertions(*Test))
        {
            FString CleanupError;
            Test->TestTrue(
                *FString::Printf(
                    TEXT("Live layout fixture unloads: %s"),
                    *CleanupError),
                Context->Cleanup(CleanupError));
            return true;
        }
        if (!Context->IsTimedOut())
        {
            return false;
        }

        Test->AddError(FString::Printf(
            TEXT("Timed out waiting for authoritative live Graph geometry (last reason: %s)."),
            Context->LastUnavailableReason.IsEmpty()
                ? TEXT("surface_not_ready")
                : *Context->LastUnavailableReason));
        FString CleanupError;
        Test->TestTrue(
            *FString::Printf(
                TEXT("Timed-out layout fixture unloads: %s"),
                *CleanupError),
            Context->Cleanup(CleanupError));
        return true;
    }

private:
    FAutomationTestBase* Test = nullptr;
    TSharedRef<FLiveGraphLayoutContext> Context;
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FSalGraphStoredLayoutFallbackTest,
    "Loomle.Sal.Graph.Layout.StoredFallback",
    EAutomationTestFlags::EditorContext
        | EAutomationTestFlags::EngineFilter)

bool FSalGraphStoredLayoutFallbackTest::RunTest(
    const FString& Parameters)
{
    if (!RequireEditor(
            *this,
            TEXT("Stored Graph layout fallback test"),
            false))
    {
        return false;
    }

    Loomle::Tests::FScopedIsolatedTransactor Transactions;
    if (!TestTrue(
            TEXT("Stored fallback fixture owns an isolated transaction buffer"),
            Transactions.Initialize()))
    {
        return false;
    }
    FGraphLayoutFixture Fixture;
    if (!TestTrue(
            TEXT("Stored fallback fixture is valid"),
            Fixture.IsValid()))
    {
        Transactions.Restore();
        return false;
    }

    const TSharedPtr<FJsonObject> Result =
        FSalGraphInterface::Query(
            ExactNodeLayoutQuery(Fixture.Node),
            GraphTarget(Fixture.Blueprint, Fixture.Graph));
    TestFalse(
        TEXT("Closed Graph layout Query remains successful"),
        HasError(Result));

    const TSharedPtr<FJsonObject> NodeFields =
        CallArgsWithId(
            Result,
            TEXT("node"),
            GuidText(Fixture.Node->NodeGuid));
    FVector2f At;
    FVector2f Size;
    TestTrue(
        TEXT("Closed Graph returns the stored Node position"),
        ReadPoint(NodeFields, TEXT("at"), At)
            && At
                == FVector2f(
                    Fixture.Node->NodePosX,
                    Fixture.Node->NodePosY));
    TestTrue(
        TEXT("Closed Graph returns the stored Node size"),
        ReadPoint(NodeFields, TEXT("size"), Size)
            && Size
                == FVector2f(
                    Fixture.Node->NodeWidth,
                    Fixture.Node->NodeHeight));
    TestFalse(
        TEXT("Closed Graph strips every Node and Pin visual field"),
        ResultHasAnyVisualField(Result));

    FString Reason;
    TestTrue(
        TEXT("Closed Graph warns that its live surface is not open"),
        TryReadDiagnosticReason(
            Result,
            LayoutUnavailableCode,
            Reason)
            && Reason == TEXT("graph_not_open"));

    Transactions.Restore();
    FString CleanupError;
    TestTrue(
        *FString::Printf(
            TEXT("Stored fallback fixture unloads: %s"),
            *CleanupError),
        Fixture.Cleanup(CleanupError));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FSalGraphHeadlessSyntheticLayoutGeometryTest,
    "Loomle.Sal.Graph.Layout.HeadlessSyntheticGeometry",
    EAutomationTestFlags::EditorContext
        | EAutomationTestFlags::EngineFilter)

bool FSalGraphHeadlessSyntheticLayoutGeometryTest::RunTest(
    const FString& Parameters)
{
    if (!RequireEditor(
            *this,
            TEXT("Headless synthetic Graph layout geometry test"),
            true))
    {
        return false;
    }

    const TSharedRef<FLiveGraphLayoutContext> Context =
        MakeShared<FLiveGraphLayoutContext>(
            EGraphLayoutSurfaceMode::HeadlessSynthetic);
    FString InitializeError;
    if (!TestTrue(
            *FString::Printf(
                TEXT("Headless synthetic Graph layout fixture initializes: %s"),
                *InitializeError),
            Context->Initialize(InitializeError)))
    {
        FString CleanupError;
        Context->Cleanup(CleanupError);
        return false;
    }

    ADD_LATENT_AUTOMATION_COMMAND(
        FRunLiveGraphLayoutCommand(this, Context));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FSalGraphLiveLayoutGeometryTest,
    "Loomle.Sal.Graph.Layout.LiveGeometry",
    EAutomationTestFlags::EditorContext
        | EAutomationTestFlags::EngineFilter)

bool FSalGraphLiveLayoutGeometryTest::RunTest(
    const FString& Parameters)
{
    if (!RequireEditor(
            *this,
            TEXT("Live Graph layout geometry test"),
            true))
    {
        return false;
    }
    if (!FApp::CanEverRender())
    {
        AddInfo(TEXT(
            "Rendered Blueprint Graph layout geometry is skipped because this Editor process cannot render."));
        return true;
    }

    const TSharedRef<FLiveGraphLayoutContext> Context =
        MakeShared<FLiveGraphLayoutContext>(
            EGraphLayoutSurfaceMode::RenderedBlueprintEditor);
    FString InitializeError;
    if (!TestTrue(
            *FString::Printf(
                TEXT("Live Graph layout fixture initializes: %s"),
                *InitializeError),
            Context->Initialize(InitializeError)))
    {
        FString CleanupError;
        Context->Cleanup(CleanupError);
        return false;
    }

    ADD_LATENT_AUTOMATION_COMMAND(
        FRunLiveGraphLayoutCommand(this, Context));
    return true;
}

#endif
