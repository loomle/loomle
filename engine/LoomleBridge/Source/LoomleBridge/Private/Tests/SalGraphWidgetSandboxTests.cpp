// Copyright 2026 Loomle contributors.

#if WITH_DEV_AUTOMATION_TESTS

#include "Sal/Graph/SalGraphInterface.h"
#include "Sal/Widget/SalWidgetInterface.h"

#include "Animation/WidgetAnimation.h"
#include "Blueprint/BlueprintExtension.h"
#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetBlueprintGeneratedClass.h"
#include "Blueprint/WidgetNavigation.h"
#include "Blueprint/WidgetTree.h"
#include "BlueprintEditorSettings.h"
#include "Components/Button.h"
#include "Components/CanvasPanel.h"
#include "Components/PanelSlot.h"
#include "Curves/CurveFloat.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraphSchema_K2.h"
#include "Editor.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Engine/TimelineTemplate.h"
#include "GameFramework/Actor.h"
#include "K2Node_CustomEvent.h"
#include "K2Node_Timeline.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/AutomationTest.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/ScopeExit.h"
#include "MovieScene.h"
#include "Sal/SalModel.h"
#include "UObject/GarbageCollection.h"
#include "UObject/Package.h"
#include "UObject/UObjectHash.h"
#include "WidgetBlueprint.h"

namespace
{
using namespace Loomle::Sal;

FString GuidText(const FGuid& Guid)
{
    return Guid.ToString(EGuidFormats::DigitsWithHyphensLower);
}

bool ResultBool(
    const TSharedPtr<FJsonObject>& Result,
    const TCHAR* Field,
    const bool Default = false)
{
    bool Value = Default;
    return Result.IsValid()
        && Result->TryGetBoolField(Field, Value)
        ? Value
        : Default;
}

bool GuidMapsEqual(
    const TMap<FName, FGuid>& Expected,
    const TMap<FName, FGuid>& Actual)
{
    if (Expected.Num() != Actual.Num())
    {
        return false;
    }
    for (const TPair<FName, FGuid>& Pair : Expected)
    {
        const FGuid* ActualGuid = Actual.Find(Pair.Key);
        if (ActualGuid == nullptr || *ActualGuid != Pair.Value)
        {
            return false;
        }
    }
    return true;
}

bool BlueprintDebuggerSettingsEqual(
    const TMap<FString, FPerBlueprintSettings>& Expected,
    const TMap<FString, FPerBlueprintSettings>& Actual)
{
    if (Expected.Num() != Actual.Num())
    {
        return false;
    }
    for (const TPair<FString, FPerBlueprintSettings>& Pair : Expected)
    {
        const FPerBlueprintSettings* ActualSettings =
            Actual.Find(Pair.Key);
        if (ActualSettings == nullptr
            || !(*ActualSettings == Pair.Value))
        {
            return false;
        }
    }
    return true;
}

class FScopedConfigFileOperationsDisabled
{
public:
    FScopedConfigFileOperationsDisabled()
        : bWasDisabled(
            GConfig != nullptr
                && GConfig->AreFileOperationsDisabled())
    {
        if (GConfig != nullptr && !bWasDisabled)
        {
            GConfig->DisableFileOperations();
        }
    }

    ~FScopedConfigFileOperationsDisabled()
    {
        if (GConfig != nullptr && !bWasDisabled)
        {
            GConfig->EnableFileOperations();
        }
    }

private:
    bool bWasDisabled = false;
};

struct FTimelineFloatTrackSnapshot
{
    FName TrackName;
    FName PropertyName;
    bool bIsExternalCurve = false;
#if WITH_EDITORONLY_DATA
    bool bIsExpanded = false;
    bool bIsCurveViewSynchronized = false;
#endif
    UCurveFloat* Curve = nullptr;
    TArray<FRichCurveKey> Keys;
};

struct FTimelineTrackSnapshot
{
    int32 EventTrackCount = 0;
    int32 VectorTrackCount = 0;
    int32 LinearColorTrackCount = 0;
    TArray<FTTTrackId> DisplayOrder;
    TArray<FTimelineFloatTrackSnapshot> FloatTracks;
};

FTimelineTrackSnapshot CaptureTimelineTrackState(
    UTimelineTemplate* Timeline)
{
    FTimelineTrackSnapshot Snapshot;
    if (Timeline == nullptr)
    {
        return Snapshot;
    }

    Snapshot.EventTrackCount = Timeline->EventTracks.Num();
    Snapshot.VectorTrackCount = Timeline->VectorTracks.Num();
    Snapshot.LinearColorTrackCount =
        Timeline->LinearColorTracks.Num();
    for (int32 Index = 0;
         Index < Timeline->GetNumDisplayTracks();
         ++Index)
    {
        Snapshot.DisplayOrder.Add(
            Timeline->GetDisplayTrackId(Index));
    }
    for (const FTTFloatTrack& Track : Timeline->FloatTracks)
    {
        FTimelineFloatTrackSnapshot TrackSnapshot;
        TrackSnapshot.TrackName = Track.GetTrackName();
        TrackSnapshot.PropertyName = Track.GetPropertyName();
        TrackSnapshot.bIsExternalCurve = Track.bIsExternalCurve;
#if WITH_EDITORONLY_DATA
        TrackSnapshot.bIsExpanded = Track.bIsExpanded;
        TrackSnapshot.bIsCurveViewSynchronized =
            Track.bIsCurveViewSynchronized;
#endif
        TrackSnapshot.Curve = Track.CurveFloat;
        if (Track.CurveFloat != nullptr)
        {
            TrackSnapshot.Keys =
                Track.CurveFloat->FloatCurve.GetCopyOfKeys();
        }
        Snapshot.FloatTracks.Add(MoveTemp(TrackSnapshot));
    }
    return Snapshot;
}

bool TimelineTrackStateEqual(
    UTimelineTemplate* Timeline,
    const FTimelineTrackSnapshot& Expected)
{
    if (Timeline == nullptr
        || Timeline->EventTracks.Num() != Expected.EventTrackCount
        || Timeline->VectorTracks.Num() != Expected.VectorTrackCount
        || Timeline->LinearColorTracks.Num()
            != Expected.LinearColorTrackCount
        || Timeline->FloatTracks.Num()
            != Expected.FloatTracks.Num()
        || Timeline->GetNumDisplayTracks()
            != Expected.DisplayOrder.Num())
    {
        return false;
    }

    for (int32 Index = 0;
         Index < Expected.DisplayOrder.Num();
         ++Index)
    {
        const FTTTrackId Actual =
            Timeline->GetDisplayTrackId(Index);
        const FTTTrackId& ExpectedId =
            Expected.DisplayOrder[Index];
        if (Actual.TrackType != ExpectedId.TrackType
            || Actual.TrackIndex != ExpectedId.TrackIndex)
        {
            return false;
        }
    }

    for (int32 Index = 0;
         Index < Expected.FloatTracks.Num();
         ++Index)
    {
        const FTTFloatTrack& Actual =
            Timeline->FloatTracks[Index];
        const FTimelineFloatTrackSnapshot& ExpectedTrack =
            Expected.FloatTracks[Index];
        if (Actual.GetTrackName() != ExpectedTrack.TrackName
            || Actual.GetPropertyName()
                != ExpectedTrack.PropertyName
            || Actual.bIsExternalCurve
                != ExpectedTrack.bIsExternalCurve
#if WITH_EDITORONLY_DATA
            || Actual.bIsExpanded != ExpectedTrack.bIsExpanded
            || Actual.bIsCurveViewSynchronized
                != ExpectedTrack.bIsCurveViewSynchronized
#endif
            || Actual.CurveFloat.Get()
                != ExpectedTrack.Curve
            || Actual.CurveFloat == nullptr
            || Actual.CurveFloat->FloatCurve.GetCopyOfKeys()
                != ExpectedTrack.Keys)
        {
            return false;
        }
    }
    return true;
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
    Target.Id = Graph != nullptr ? GuidText(Graph->GraphGuid) : FString();
    Target.Object = Graph;
    Target.Package =
        Blueprint != nullptr ? Blueprint->GetOutermost() : nullptr;
    Target.Blueprint = Blueprint;
    Target.Class =
        Blueprint != nullptr ? Blueprint->GeneratedClass.Get() : nullptr;
    Target.Graph = Graph;
    Target.Interfaces = {FName(TEXT("graph"))};
    return Target;
}

FSalPatch MoveNodePatch(
    const UEdGraphNode* Node,
    const FIntPoint Delta)
{
    TSharedRef<FJsonObject> NodeRef = MakeShared<FJsonObject>();
    NodeRef->SetStringField(TEXT("kind"), TEXT("node"));
    NodeRef->SetStringField(
        TEXT("id"),
        Node != nullptr ? GuidText(Node->NodeGuid) : FString());

    TSharedRef<FJsonObject> Move = MakeShared<FJsonObject>();
    Move->SetStringField(TEXT("kind"), TEXT("move"));
    Move->SetObjectField(TEXT("target"), NodeRef);
    Move->SetArrayField(
        TEXT("by"),
        {
            MakeShared<FJsonValueNumber>(Delta.X),
            MakeShared<FJsonValueNumber>(Delta.Y)
        });

    FSalPatch Patch;
    Patch.Alias = TEXT("graph");
    Patch.bDryRun = true;
    Patch.Statements = {MakeShared<FJsonValueObject>(Move)};
    return Patch;
}

TSharedRef<FJsonValue> TimelineAddKeyStatement(
    const UK2Node_Timeline* Node,
    const FString& TrackName,
    const double Time,
    const double Value)
{
    TSharedRef<FJsonObject> NodeRef = MakeShared<FJsonObject>();
    NodeRef->SetStringField(TEXT("kind"), TEXT("node"));
    NodeRef->SetStringField(
        TEXT("id"),
        Node != nullptr ? GuidText(Node->NodeGuid) : FString());

    TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
    Args->SetStringField(TEXT("TrackName"), TrackName);
    Args->SetNumberField(TEXT("Time"), Time);
    Args->SetNumberField(TEXT("Value"), Value);

    TSharedRef<FJsonObject> Invoke = MakeShared<FJsonObject>();
    Invoke->SetStringField(TEXT("kind"), TEXT("invoke"));
    Invoke->SetObjectField(TEXT("target"), NodeRef);
    Invoke->SetStringField(TEXT("operation"), TEXT("AddKey"));
    Invoke->SetObjectField(TEXT("args"), Args);
    return MakeShared<FJsonValueObject>(Invoke);
}

bool UnloadSandboxTestPackage(
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

struct FGraphSandboxFixture
{
    FGraphSandboxFixture() = default;

    ~FGraphSandboxFixture()
    {
        FString Ignored;
        Cleanup(Ignored);
    }

    FGraphSandboxFixture(const FGraphSandboxFixture&) = delete;
    FGraphSandboxFixture& operator=(const FGraphSandboxFixture&) = delete;

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
        LegacyCurve = nullptr;
        LegacyTimeline = nullptr;
        SharedCurve = nullptr;
        TimelineNode = nullptr;
        Timeline = nullptr;
        Node = nullptr;
        Graph = nullptr;
        Blueprint = nullptr;
        Package = nullptr;
        return UnloadSandboxTestPackage(
            PackageToUnload,
            OutError);
    }

    UPackage* Package = nullptr;
    UBlueprint* Blueprint = nullptr;
    UEdGraph* Graph = nullptr;
    UK2Node_CustomEvent* Node = nullptr;
    UTimelineTemplate* Timeline = nullptr;
    UK2Node_Timeline* TimelineNode = nullptr;
    UCurveFloat* SharedCurve = nullptr;
    UTimelineTemplate* LegacyTimeline = nullptr;
    UCurveFloat* LegacyCurve = nullptr;

private:
    bool bCleaned = false;
};

void MakeGraphSandboxFixture(FGraphSandboxFixture& Fixture)
{
    const FString Token =
        FGuid::NewGuid().ToString(EGuidFormats::Digits);
    Fixture.Package = CreatePackage(*FString::Printf(
        TEXT("/Game/LoomleTests/SalGraphSandbox_%s"),
        *Token));
    Fixture.Blueprint = FKismetEditorUtilities::CreateBlueprint(
        AActor::StaticClass(),
        Fixture.Package,
        *FString::Printf(TEXT("BP_GraphSandbox_%s"), *Token),
        BPTYPE_Normal,
        UBlueprint::StaticClass(),
        UBlueprintGeneratedClass::StaticClass(),
        NAME_None);
    if (Fixture.Blueprint == nullptr)
    {
        return;
    }

    Fixture.Graph =
        FBlueprintEditorUtils::FindEventGraph(Fixture.Blueprint);
    if (Fixture.Graph != nullptr)
    {
        Fixture.Node =
            NewObject<UK2Node_CustomEvent>(
                Fixture.Graph,
                NAME_None,
                RF_Transactional);
        Fixture.Node->CustomFunctionName =
            TEXT("SandboxMoveTarget");
        Fixture.Node->CreateNewGuid();
        Fixture.Node->NodePosX = 140;
        Fixture.Node->NodePosY = 260;
        Fixture.Graph->AddNode(Fixture.Node, false, false);
        Fixture.Node->AllocateDefaultPins();
    }

    Fixture.Timeline = FBlueprintEditorUtils::AddNewTimeline(
        Fixture.Blueprint,
        TEXT("SandboxTimeline"));
    if (Fixture.Timeline != nullptr && Fixture.Graph != nullptr)
    {
        Fixture.TimelineNode =
            NewObject<UK2Node_Timeline>(
                Fixture.Graph,
                NAME_None,
                RF_Transactional);
        Fixture.TimelineNode->TimelineName =
            Fixture.Timeline->GetVariableName();
        Fixture.TimelineNode->TimelineGuid =
            Fixture.Timeline->TimelineGuid;
        Fixture.TimelineNode->CreateNewGuid();
        Fixture.Graph->AddNode(
            Fixture.TimelineNode,
            false,
            false);

        Fixture.SharedCurve = NewObject<UCurveFloat>(
            Fixture.Blueprint->GeneratedClass,
            TEXT("SandboxSharedCurve"),
            RF_Transactional);
        Fixture.SharedCurve->FloatCurve.AddKey(0.0f, 10.0f);
        Fixture.SharedCurve->FloatCurve.AddKey(1.0f, 20.0f);

        FTTFloatTrack First;
        First.bIsExternalCurve = false;
        First.CurveFloat = Fixture.SharedCurve;
        First.SetTrackName(TEXT("SharedA"), Fixture.Timeline);
        const int32 FirstTrackIndex =
            Fixture.Timeline->FloatTracks.Add(First);
        Fixture.Timeline->AddDisplayTrack(
            FTTTrackId(
                FTTTrackBase::TT_FloatInterp,
                FirstTrackIndex));

        FTTFloatTrack Second;
        Second.bIsExternalCurve = false;
        Second.CurveFloat = Fixture.SharedCurve;
        Second.SetTrackName(TEXT("SharedB"), Fixture.Timeline);
        const int32 SecondTrackIndex =
            Fixture.Timeline->FloatTracks.Add(Second);
        Fixture.Timeline->AddDisplayTrack(
            FTTTrackId(
                FTTTrackBase::TT_FloatInterp,
                SecondTrackIndex));

        Fixture.TimelineNode->AllocateDefaultPins();
    }
    Fixture.LegacyTimeline = FBlueprintEditorUtils::AddNewTimeline(
        Fixture.Blueprint,
        TEXT("LegacyTimeline"));
    if (Fixture.LegacyTimeline != nullptr)
    {
        Fixture.LegacyCurve = NewObject<UCurveFloat>(
            Fixture.Blueprint->GeneratedClass,
            TEXT("LegacyTimelineCurve"),
            RF_Transactional);
        Fixture.LegacyCurve->FloatCurve.AddKey(0.0f, 1.0f);

        FTTFloatTrack LegacyTrack;
        LegacyTrack.bIsExternalCurve = false;
        LegacyTrack.CurveFloat = Fixture.LegacyCurve;
        LegacyTrack.SetTrackName(
            TEXT("LegacyFloat"),
            Fixture.LegacyTimeline);
        Fixture.LegacyTimeline->FloatTracks.Add(
            MoveTemp(LegacyTrack));
        // Deliberately preserve the pre-display-order authored shape. UE 5.7
        // Serialize mutates this source state during StaticDuplicateObject
        // unless the sandbox layer restores both source and copy.
    }
    if (Fixture.Package != nullptr)
    {
        Fixture.Package->SetDirtyFlag(false);
    }
}

FSalResolvedTarget WidgetTarget(UWidgetBlueprint* Blueprint)
{
    FSalResolvedTarget Target;
    Target.Kind = ESalTargetKind::Blueprint;
    Target.Alias = TEXT("widget_blueprint");
    Target.AssetPath =
        Blueprint != nullptr ? Blueprint->GetPathName() : FString();
    Target.Id =
        Blueprint != nullptr
            ? GuidText(Blueprint->GetBlueprintGuid())
            : FString();
    Target.Object = Blueprint;
    Target.Package =
        Blueprint != nullptr ? Blueprint->GetOutermost() : nullptr;
    Target.Blueprint = Blueprint;
    Target.Class =
        Blueprint != nullptr ? Blueprint->GeneratedClass.Get() : nullptr;
    Target.Interfaces = {
        FName(TEXT("blueprint")),
        FName(TEXT("widget"))};
    return Target;
}

FSalPatch SetWidgetVariablePatch(
    const FGuid& WidgetGuid,
    const bool Value)
{
    TSharedRef<FJsonObject> WidgetRef = MakeShared<FJsonObject>();
    WidgetRef->SetStringField(TEXT("kind"), TEXT("widget"));
    WidgetRef->SetStringField(TEXT("id"), GuidText(WidgetGuid));

    TSharedRef<FJsonObject> Member = MakeShared<FJsonObject>();
    Member->SetStringField(TEXT("kind"), TEXT("member"));
    Member->SetObjectField(TEXT("object"), WidgetRef);
    Member->SetArrayField(
        TEXT("path"),
        {MakeShared<FJsonValueString>(TEXT("bIsVariable"))});

    TSharedRef<FJsonObject> Set = MakeShared<FJsonObject>();
    Set->SetStringField(TEXT("kind"), TEXT("set"));
    Set->SetObjectField(TEXT("target"), Member);
    Set->SetBoolField(TEXT("value"), Value);

    FSalPatch Patch;
    Patch.Alias = TEXT("widget_blueprint");
    Patch.bDryRun = true;
    Patch.Statements = {MakeShared<FJsonValueObject>(Set)};
    return Patch;
}

struct FWidgetSandboxFixture
{
    FWidgetSandboxFixture() = default;

    ~FWidgetSandboxFixture()
    {
        FString Ignored;
        Cleanup(Ignored);
    }

    FWidgetSandboxFixture(const FWidgetSandboxFixture&) = delete;
    FWidgetSandboxFixture& operator=(const FWidgetSandboxFixture&) = delete;

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
        Extension = nullptr;
        MovieScene = nullptr;
        Animation = nullptr;
        Navigation = nullptr;
        ChildSlot = nullptr;
        Child = nullptr;
        Root = nullptr;
        Blueprint = nullptr;
        Package = nullptr;
        return UnloadSandboxTestPackage(
            PackageToUnload,
            OutError);
    }

    UPackage* Package = nullptr;
    UWidgetBlueprint* Blueprint = nullptr;
    UCanvasPanel* Root = nullptr;
    UButton* Child = nullptr;
    UPanelSlot* ChildSlot = nullptr;
    UWidgetNavigation* Navigation = nullptr;
    UWidgetAnimation* Animation = nullptr;
    UMovieScene* MovieScene = nullptr;
    UBlueprintExtension* Extension = nullptr;
    FName SourceVariableName;
    FGuid ChildGuid;

private:
    bool bCleaned = false;
};

void MakeWidgetSandboxFixture(FWidgetSandboxFixture& Fixture)
{
    const FString Token =
        FGuid::NewGuid().ToString(EGuidFormats::Digits);
    Fixture.Package = CreatePackage(*FString::Printf(
        TEXT("/Game/LoomleTests/SalWidgetSandbox_%s"),
        *Token));
    Fixture.Blueprint = Cast<UWidgetBlueprint>(
        FKismetEditorUtilities::CreateBlueprint(
            UUserWidget::StaticClass(),
            Fixture.Package,
            *FString::Printf(TEXT("WBP_Sandbox_%s"), *Token),
            BPTYPE_Normal,
            UWidgetBlueprint::StaticClass(),
            UWidgetBlueprintGeneratedClass::StaticClass(),
            NAME_None));
    if (Fixture.Blueprint == nullptr)
    {
        return;
    }

    Fixture.SourceVariableName = TEXT("BindingSourceValue");
    FEdGraphPinType VariableType;
    VariableType.PinCategory = UEdGraphSchema_K2::PC_Boolean;
    FBlueprintEditorUtils::AddMemberVariable(
        Fixture.Blueprint,
        Fixture.SourceVariableName,
        VariableType);
    FKismetEditorUtilities::CompileBlueprint(Fixture.Blueprint);

    Fixture.Root =
        Fixture.Blueprint->WidgetTree
            ->ConstructWidget<UCanvasPanel>(
                UCanvasPanel::StaticClass(),
                TEXT("RootCanvas"));
    Fixture.Child =
        Fixture.Blueprint->WidgetTree
            ->ConstructWidget<UButton>(
                UButton::StaticClass(),
                TEXT("BoundButton"));
    if (Fixture.Root != nullptr && Fixture.Child != nullptr)
    {
        Fixture.Blueprint->WidgetTree->RootWidget = Fixture.Root;
        Fixture.ChildSlot = Fixture.Root->AddChild(Fixture.Child);
        Fixture.Navigation = NewObject<UWidgetNavigation>(
            Fixture.Child,
            TEXT("SandboxNavigation"),
            RF_Transactional);
        Fixture.Child->Navigation = Fixture.Navigation;
        Fixture.Blueprint->OnVariableAdded(Fixture.Root->GetFName());
        Fixture.Blueprint->OnVariableAdded(Fixture.Child->GetFName());
        Fixture.ChildGuid =
            Fixture.Blueprint->WidgetVariableNameToGuidMap.FindRef(
                Fixture.Child->GetFName());
    }

    Fixture.Animation = NewObject<UWidgetAnimation>(
        Fixture.Blueprint,
        TEXT("SandboxAnimation"),
        RF_Transactional);
    Fixture.MovieScene = NewObject<UMovieScene>(
        Fixture.Animation,
        TEXT("SandboxMovieScene"),
        RF_Transactional);
    Fixture.Animation->MovieScene = Fixture.MovieScene;
    Fixture.Blueprint->Animations.Add(Fixture.Animation);
    Fixture.Blueprint->OnVariableAdded(
        Fixture.Animation->GetFName());

    FProperty* SourceProperty =
        FindFProperty<FProperty>(
            Fixture.Blueprint->GeneratedClass,
            Fixture.SourceVariableName);
    const int32 VariableIndex =
        FBlueprintEditorUtils::FindNewVariableIndex(
            Fixture.Blueprint,
            Fixture.SourceVariableName);
    if (SourceProperty != nullptr && VariableIndex != INDEX_NONE)
    {
        TArray<FFieldVariant> BindingChain;
        BindingChain.Add(FFieldVariant(SourceProperty));
        FDelegateEditorBinding Binding;
        Binding.ObjectName =
            Fixture.Child != nullptr
                ? Fixture.Child->GetName()
                : FString();
        Binding.PropertyName = TEXT("IsEnabled");
        Binding.SourceProperty = Fixture.SourceVariableName;
        Binding.MemberGuid =
            Fixture.Blueprint->NewVariables[VariableIndex].VarGuid;
        Binding.Kind = EBindingKind::Property;
        Binding.SourcePath = FEditorPropertyPath(BindingChain);
        Fixture.Blueprint->Bindings.Add(Binding);
    }

    Fixture.Extension = NewObject<UBlueprintExtension>(
        Fixture.Blueprint,
        TEXT("SandboxExtension"),
        RF_Transactional);
    Fixture.Blueprint->AddExtension(Fixture.Extension);
    if (Fixture.Package != nullptr)
    {
        Fixture.Package->SetDirtyFlag(false);
    }
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FSalGraphDryRunSandboxIsolationTest,
    "Loomle.Sal.Graph.Mutation.DryRunSandboxIsolation",
    EAutomationTestFlags::EditorContext
        | EAutomationTestFlags::EngineFilter)

bool FSalGraphDryRunSandboxIsolationTest::RunTest(
    const FString& Parameters)
{
    FGraphSandboxFixture Fixture;
    MakeGraphSandboxFixture(Fixture);
    ON_SCOPE_EXIT
    {
        FString CleanupError;
        const bool bCleaned =
            Fixture.Cleanup(CleanupError);
        TestTrue(
            *FString::Printf(
                TEXT("Graph sandbox fixture unloads without changing editor selection or transactions: %s"),
                *CleanupError),
            bCleaned);
    };
    TestNotNull(TEXT("Graph sandbox Blueprint exists"), Fixture.Blueprint);
    TestNotNull(TEXT("Graph sandbox Graph exists"), Fixture.Graph);
    TestNotNull(TEXT("Graph sandbox Node exists"), Fixture.Node);
    TestNotNull(TEXT("Graph sandbox Timeline exists"), Fixture.Timeline);
    TestNotNull(
        TEXT("Graph sandbox Timeline Node exists"),
        Fixture.TimelineNode);
    TestNotNull(
        TEXT("Graph sandbox shared Curve exists"),
        Fixture.SharedCurve);
    TestNotNull(
        TEXT("Graph sandbox legacy Timeline exists"),
        Fixture.LegacyTimeline);
    TestNotNull(
        TEXT("Graph sandbox legacy Curve exists"),
        Fixture.LegacyCurve);
    if (Fixture.Blueprint == nullptr
        || Fixture.Graph == nullptr
        || Fixture.Node == nullptr
        || Fixture.Timeline == nullptr
        || Fixture.TimelineNode == nullptr
        || Fixture.SharedCurve == nullptr
        || Fixture.LegacyTimeline == nullptr
        || Fixture.LegacyCurve == nullptr)
    {
        return false;
    }

    UClass* SourceClass = Fixture.Blueprint->GeneratedClass.Get();
    UObject* SourceCDO =
        SourceClass != nullptr
            ? SourceClass->GetDefaultObject(false)
            : nullptr;
    const int32 SourceX = Fixture.Node->NodePosX;
    const int32 SourceY = Fixture.Node->NodePosY;
    const int32 SourceKeyCount =
        Fixture.SharedCurve->FloatCurve.GetNumKeys();
    const FTimelineTrackSnapshot SourceTimelineTracks =
        CaptureTimelineTrackState(Fixture.Timeline);
    const FTimelineTrackSnapshot SourceLegacyTimelineTracks =
        CaptureTimelineTrackState(Fixture.LegacyTimeline);
    TestEqual(
        TEXT("Graph fixture has complete native TrackDisplayOrder"),
        SourceTimelineTracks.DisplayOrder.Num(),
        Fixture.Timeline->FloatTracks.Num());
    TestEqual(
        TEXT("Legacy fixture preserves an empty TrackDisplayOrder"),
        SourceLegacyTimelineTracks.DisplayOrder.Num(),
        0);
    UBlueprintEditorSettings* EditorSettings =
        GetMutableDefault<UBlueprintEditorSettings>();
    TestNotNull(
        TEXT("Blueprint editor settings are available"),
        EditorSettings);
    TestTrue(
        TEXT("Graph sandbox Node exposes a watchable fixture Pin"),
        !Fixture.Node->Pins.IsEmpty());
    if (EditorSettings == nullptr || Fixture.Node->Pins.IsEmpty())
    {
        return false;
    }
    const TMap<FString, FPerBlueprintSettings> OriginalDebuggerSettings =
        EditorSettings->PerBlueprintSettings;
    const FScopedConfigFileOperationsDisabled
        DisableConfigFileOperations;
    ON_SCOPE_EXIT
    {
        EditorSettings->PerBlueprintSettings =
            OriginalDebuggerSettings;
        // Keep the config cache and CDO aligned while disk writes remain
        // disabled, so a future debugger-copy regression cannot persist a
        // transient Blueprint path into user settings.
        EditorSettings->SaveConfig();
    };
    FPerBlueprintSettings SourceDebuggerSettings;
    SourceDebuggerSettings.WatchedPins.Emplace(Fixture.Node->Pins[0]);
    EditorSettings->PerBlueprintSettings.Add(
        Fixture.Blueprint->GetPathName(),
        MoveTemp(SourceDebuggerSettings));
    const TMap<FString, FPerBlueprintSettings> ExpectedDebuggerSettings =
        EditorSettings->PerBlueprintSettings;

    FSalPatch DryRunPatch =
        MoveNodePatch(Fixture.Node, FIntPoint(31, -17));
    DryRunPatch.Statements.Add(
        TimelineAddKeyStatement(
            Fixture.TimelineNode,
            TEXT("SharedA"),
            0.5,
            15.0));
    const TSharedPtr<FJsonObject> DryRun =
        FSalGraphInterface::Patch(
            DryRunPatch,
            GraphTarget(Fixture.Blueprint, Fixture.Graph));
    TestTrue(
        TEXT("Graph public dry run validates"),
        ResultBool(DryRun, TEXT("valid")));
    TestTrue(
        TEXT("Graph public dry run is reported"),
        ResultBool(DryRun, TEXT("dryRun")));
    TestFalse(
        TEXT("Graph public dry run is not applied"),
        ResultBool(DryRun, TEXT("applied"), true));
    TestEqual(
        TEXT("Graph dry run preserves source X"),
        Fixture.Node->NodePosX,
        SourceX);
    TestEqual(
        TEXT("Graph dry run preserves source Y"),
        Fixture.Node->NodePosY,
        SourceY);
    TestEqual(
        TEXT("Graph dry run preserves source Curve keys"),
        Fixture.SharedCurve->FloatCurve.GetNumKeys(),
        SourceKeyCount);
    TestTrue(
        TEXT("Graph dry run preserves complete source Timeline track state"),
        TimelineTrackStateEqual(
            Fixture.Timeline,
            SourceTimelineTracks));
    TestTrue(
        TEXT("Graph dry run preserves legacy empty TrackDisplayOrder"),
        TimelineTrackStateEqual(
            Fixture.LegacyTimeline,
            SourceLegacyTimelineTracks));
    TestFalse(
        TEXT("Graph dry run preserves source dirty state"),
        Fixture.Package->IsDirty());
    TestTrue(
        TEXT("Graph dry run preserves source Class"),
        Fixture.Blueprint->GeneratedClass == SourceClass);
    TestTrue(
        TEXT("Graph dry run preserves source CDO"),
        SourceClass != nullptr
            && SourceClass->GetDefaultObject(false) == SourceCDO);
    TestTrue(
        TEXT("Graph dry run does not copy debugger state into transient settings"),
        BlueprintDebuggerSettingsEqual(
            ExpectedDebuggerSettings,
            EditorSettings->PerBlueprintSettings));

    TStrongObjectPtr<UBlueprint> SandboxOwner;
    FSalResolvedTarget SandboxTarget;
    FString SandboxError;
    TestTrue(
        *FString::Printf(
            TEXT("Graph domain sandbox builds: %s"),
            *SandboxError),
        FSalGraphInterface::BuildSandboxTargetForTesting(
            GraphTarget(Fixture.Blueprint, Fixture.Graph),
            SandboxOwner,
            SandboxTarget,
            SandboxError));
    UBlueprint* Sandbox = SandboxOwner.Get();
    TestNotNull(TEXT("Graph sandbox owner is strong"), Sandbox);
    if (Sandbox == nullptr)
    {
        return false;
    }
    TestTrue(
        TEXT("Graph sandbox uses a distinct Generated Class"),
        Sandbox->GeneratedClass != Fixture.Blueprint->GeneratedClass);
    TestTrue(
        TEXT("Graph sandbox uses a distinct CDO"),
        Sandbox->GeneratedClass != nullptr
            && Sandbox->GeneratedClass->GetDefaultObject(false)
                != SourceCDO);
    TestTrue(
        TEXT("Graph sandbox construction leaves debugger settings unchanged"),
        BlueprintDebuggerSettingsEqual(
            ExpectedDebuggerSettings,
            EditorSettings->PerBlueprintSettings));
    TestTrue(
        TEXT("Graph sandbox preserves target Graph identity"),
        SandboxTarget.Graph != nullptr
            && SandboxTarget.Graph->GraphGuid
                == Fixture.Graph->GraphGuid);
    TestTrue(
        TEXT("Graph sandbox construction preserves complete source Timeline track state"),
        TimelineTrackStateEqual(
            Fixture.Timeline,
            SourceTimelineTracks));
    TestTrue(
        TEXT("Graph sandbox construction preserves legacy source Timeline state"),
        TimelineTrackStateEqual(
            Fixture.LegacyTimeline,
            SourceLegacyTimelineTracks));

    UTimelineTemplate* SandboxTimeline =
        Sandbox->FindTimelineTemplateByVariableName(
            Fixture.Timeline->GetVariableName());
    TestNotNull(
        TEXT("Graph sandbox Timeline resolves by stable identity"),
        SandboxTimeline);
    UTimelineTemplate* SandboxLegacyTimeline =
        Sandbox->FindTimelineTemplateByVariableName(
            Fixture.LegacyTimeline->GetVariableName());
    TestNotNull(
        TEXT("Graph sandbox legacy Timeline resolves by stable identity"),
        SandboxLegacyTimeline);
    if (SandboxLegacyTimeline != nullptr)
    {
        TestEqual(
            TEXT("Graph sandbox preserves legacy empty TrackDisplayOrder"),
            SandboxLegacyTimeline->GetNumDisplayTracks(),
            0);
        TestEqual(
            TEXT("Graph sandbox preserves legacy Timeline track shape"),
            SandboxLegacyTimeline->FloatTracks.Num(),
            Fixture.LegacyTimeline->FloatTracks.Num());
        TestTrue(
            TEXT("Graph sandbox isolates the legacy Timeline Curve"),
            !SandboxLegacyTimeline->FloatTracks.IsEmpty()
                && SandboxLegacyTimeline->FloatTracks[0]
                    .CurveFloat
                    != Fixture.LegacyCurve);
    }
    if (SandboxTimeline != nullptr)
    {
        TestEqual(
            TEXT("Graph sandbox preserves both Timeline float tracks"),
            SandboxTimeline->FloatTracks.Num(),
            2);
        const bool bHasCompleteFloatTracks =
            SandboxTimeline->FloatTracks.Num() == 2
            && SandboxTimeline->FloatTracks[0].CurveFloat != nullptr
            && SandboxTimeline->FloatTracks[1].CurveFloat != nullptr;
        TestTrue(
            TEXT("Graph sandbox float tracks retain their Curves"),
            bHasCompleteFloatTracks);
        if (!bHasCompleteFloatTracks)
        {
            return false;
        }
        TestEqual(
            TEXT("Graph sandbox preserves Timeline GUID"),
            SandboxTimeline->TimelineGuid,
            Fixture.Timeline->TimelineGuid);
        TestEqual(
            TEXT("Graph sandbox preserves shared Curve alias"),
            SandboxTimeline->FloatTracks[0].CurveFloat.Get(),
            SandboxTimeline->FloatTracks[1].CurveFloat.Get());
        TestTrue(
            TEXT("Graph sandbox Curve is isolated"),
            SandboxTimeline->FloatTracks[0].CurveFloat
                != Fixture.SharedCurve);

        FSalPatch SandboxMutation;
        SandboxMutation.Alias = TEXT("graph");
        SandboxMutation.Statements = {
            TimelineAddKeyStatement(
                Fixture.TimelineNode,
                TEXT("SharedA"),
                0.5,
                15.0)};
        const TSharedPtr<FJsonObject> Applied =
            FSalGraphInterface::Patch(
                SandboxMutation,
                SandboxTarget);
        const bool bSandboxApplied =
            ResultBool(Applied, TEXT("applied"));
        TestTrue(
            TEXT("Graph sandbox Timeline AddKey validates"),
            ResultBool(Applied, TEXT("valid")));
        TestTrue(
            TEXT("Graph sandbox Timeline AddKey applies"),
            bSandboxApplied);
        TestEqual(
            TEXT("Graph sandbox Timeline AddKey mutates only the sandbox Curve"),
            SandboxTimeline->FloatTracks[0]
                .CurveFloat->FloatCurve.GetNumKeys(),
            SourceKeyCount + 1);
        TestTrue(
            TEXT("Graph sandbox Timeline AddKey preserves complete source Timeline track state"),
            TimelineTrackStateEqual(
                Fixture.Timeline,
                SourceTimelineTracks));
        TestFalse(
            TEXT("Graph sandbox Timeline AddKey preserves source dirty state"),
            Fixture.Package->IsDirty());
        if (bSandboxApplied)
        {
            TestTrue(
                TEXT("Graph sandbox Timeline mutation transaction is removed"),
                GEditor != nullptr
                    && GEditor->UndoTransaction(false));
        }
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FSalWidgetDryRunSandboxIsolationTest,
    "Loomle.Sal.Widget.Mutation.DryRunSandboxIsolation",
    EAutomationTestFlags::EditorContext
        | EAutomationTestFlags::EngineFilter)

bool FSalWidgetDryRunSandboxIsolationTest::RunTest(
    const FString& Parameters)
{
    FWidgetSandboxFixture Fixture;
    MakeWidgetSandboxFixture(Fixture);
    TestNotNull(TEXT("Widget sandbox Blueprint exists"), Fixture.Blueprint);
    TestNotNull(TEXT("Widget sandbox root exists"), Fixture.Root);
    TestNotNull(TEXT("Widget sandbox child exists"), Fixture.Child);
    TestNotNull(TEXT("Widget sandbox slot exists"), Fixture.ChildSlot);
    TestNotNull(
        TEXT("Widget sandbox navigation exists"),
        Fixture.Navigation);
    TestNotNull(
        TEXT("Widget sandbox animation exists"),
        Fixture.Animation);
    TestNotNull(
        TEXT("Widget sandbox MovieScene exists"),
        Fixture.MovieScene);
    TestNotNull(
        TEXT("Widget sandbox extension exists"),
        Fixture.Extension);
    const bool bHasBinding =
        Fixture.Blueprint != nullptr
        && Fixture.Blueprint->Bindings.Num() == 1
        && Fixture.Blueprint->Bindings[0]
            .SourcePath.Segments.Num() == 1;
    const bool bHasAnimation =
        Fixture.Blueprint != nullptr
        && Fixture.Blueprint->Animations.Num() == 1
        && Fixture.Blueprint->Animations[0]
            == Fixture.Animation;
    const bool bHasExtension =
        Fixture.Blueprint != nullptr
        && Fixture.Blueprint->GetExtensions().Num() == 1
        && Fixture.Blueprint->GetExtensions()[0]
            == Fixture.Extension;
    TestTrue(
        TEXT("Widget sandbox fixture owns one complete binding"),
        bHasBinding);
    TestTrue(
        TEXT("Widget sandbox fixture owns its Animation"),
        bHasAnimation);
    TestTrue(
        TEXT("Widget sandbox fixture owns its Extension"),
        bHasExtension);
    TestTrue(
        TEXT("Widget sandbox persistent ID exists"),
        Fixture.ChildGuid.IsValid());
    if (Fixture.Blueprint == nullptr
        || Fixture.Root == nullptr
        || Fixture.Child == nullptr
        || Fixture.ChildSlot == nullptr
        || Fixture.Navigation == nullptr
        || Fixture.Animation == nullptr
        || Fixture.MovieScene == nullptr
        || Fixture.Extension == nullptr
        || !bHasBinding
        || !bHasAnimation
        || !bHasExtension
        || !Fixture.ChildGuid.IsValid())
    {
        return false;
    }

    UClass* SourceClass = Fixture.Blueprint->GeneratedClass.Get();
    UObject* SourceCDO =
        SourceClass != nullptr
            ? SourceClass->GetDefaultObject(false)
            : nullptr;
    UWidgetTree* SourceTree = Fixture.Blueprint->WidgetTree;
    UStruct* SourceBindingStruct =
        !Fixture.Blueprint->Bindings.IsEmpty()
            && !Fixture.Blueprint->Bindings[0]
                    .SourcePath.Segments.IsEmpty()
            ? Fixture.Blueprint->Bindings[0]
                  .SourcePath.Segments[0]
                  .GetStruct()
            : nullptr;
    const TMap<FName, FGuid> SourceVariableGuids =
        Fixture.Blueprint->WidgetVariableNameToGuidMap;
    const bool bSourceIsVariable = Fixture.Child->bIsVariable;

    const TSharedPtr<FJsonObject> DryRun =
        FSalWidgetInterface::Patch(
            SetWidgetVariablePatch(
                Fixture.ChildGuid,
                !bSourceIsVariable),
            WidgetTarget(Fixture.Blueprint));
    TestTrue(
        TEXT("Widget public dry run validates"),
        ResultBool(DryRun, TEXT("valid")));
    TestTrue(
        TEXT("Widget public dry run is reported"),
        ResultBool(DryRun, TEXT("dryRun")));
    TestFalse(
        TEXT("Widget public dry run is not applied"),
        ResultBool(DryRun, TEXT("applied"), true));
    TestEqual(
        TEXT("Widget dry run preserves source field"),
        Fixture.Child->bIsVariable,
        bSourceIsVariable);
    TestTrue(
        TEXT("Widget dry run preserves source WidgetTree"),
        Fixture.Blueprint->WidgetTree == SourceTree);
    TestTrue(
        TEXT("Widget dry run preserves source Slot"),
        Fixture.Child->Slot == Fixture.ChildSlot);
    TestTrue(
        TEXT("Widget dry run preserves source Navigation"),
        Fixture.Child->Navigation == Fixture.Navigation);
    TestTrue(
        TEXT("Widget dry run preserves source Animation"),
        Fixture.Blueprint->Animations[0] == Fixture.Animation);
    TestTrue(
        TEXT("Widget dry run preserves source MovieScene"),
        Fixture.Animation->MovieScene == Fixture.MovieScene);
    TestTrue(
        TEXT("Widget dry run preserves source Extension"),
        Fixture.Blueprint->GetExtensions()[0]
            == Fixture.Extension);
    TestTrue(
        TEXT("Widget dry run preserves source binding base"),
        Fixture.Blueprint->Bindings[0]
                .SourcePath.Segments[0]
                .GetStruct()
            == SourceBindingStruct);
    TestTrue(
        TEXT("Widget dry run preserves source variable GUIDs"),
        GuidMapsEqual(
            SourceVariableGuids,
            Fixture.Blueprint->WidgetVariableNameToGuidMap));
    TestFalse(
        TEXT("Widget dry run preserves source dirty state"),
        Fixture.Package->IsDirty());
    TestTrue(
        TEXT("Widget dry run preserves source Class"),
        Fixture.Blueprint->GeneratedClass == SourceClass);
    TestTrue(
        TEXT("Widget dry run preserves source CDO"),
        SourceClass != nullptr
            && SourceClass->GetDefaultObject(false) == SourceCDO);

    TStrongObjectPtr<UBlueprint> SandboxOwner;
    FString SandboxError;
    UWidgetBlueprint* Sandbox =
        FSalWidgetInterface::DuplicateForPreflightForTesting(
            Fixture.Blueprint,
            SandboxOwner,
            SandboxError);
    TestNotNull(
        *FString::Printf(
            TEXT("Widget domain sandbox builds: %s"),
            *SandboxError),
        Sandbox);
    if (Sandbox == nullptr)
    {
        return false;
    }
    TestNotNull(
        TEXT("Widget sandbox retains its WidgetTree"),
        Sandbox->WidgetTree.Get());
    if (Sandbox->WidgetTree == nullptr)
    {
        return false;
    }
    UWidget* SandboxChild =
        Sandbox->WidgetTree->FindWidget(
            Fixture.Child->GetFName());
    TestTrue(
        TEXT("Widget sandbox owner retains the returned subtype"),
        SandboxOwner.Get() == Sandbox);
    TestTrue(
        TEXT("Widget sandbox uses a distinct Generated Class"),
        Sandbox->GeneratedClass != SourceClass);
    TestTrue(
        TEXT("Widget sandbox uses a distinct CDO"),
        Sandbox->GeneratedClass != nullptr
            && Sandbox->GeneratedClass->GetDefaultObject(false)
                != SourceCDO);
    TestTrue(
        TEXT("Widget sandbox isolates WidgetTree"),
        Sandbox->WidgetTree != SourceTree);
    TestNotNull(TEXT("Widget sandbox child resolves"), SandboxChild);
    TestTrue(
        TEXT("Widget sandbox isolates Slot"),
        SandboxChild != nullptr
            && SandboxChild->Slot != Fixture.ChildSlot);
    TestTrue(
        TEXT("Widget sandbox isolates Navigation"),
        SandboxChild != nullptr
            && SandboxChild->Navigation != Fixture.Navigation);
    TestTrue(
        TEXT("Widget sandbox preserves and rebases binding"),
        !Sandbox->Bindings.IsEmpty()
            && !Sandbox->Bindings[0]
                    .SourcePath.Segments.IsEmpty()
            && Sandbox->Bindings[0]
                    .SourcePath.Segments[0]
                    .GetStruct()
                == Sandbox->GeneratedClass);
    TestTrue(
        TEXT("Widget sandbox preserves variable GUID map"),
        GuidMapsEqual(
            SourceVariableGuids,
            Sandbox->WidgetVariableNameToGuidMap));
    TestTrue(
        TEXT("Widget sandbox isolates Animation"),
        !Sandbox->Animations.IsEmpty()
            && Sandbox->Animations[0] != Fixture.Animation);
    TestTrue(
        TEXT("Widget sandbox isolates MovieScene"),
        !Sandbox->Animations.IsEmpty()
            && Sandbox->Animations[0] != nullptr
            && Sandbox->Animations[0]->MovieScene
                != Fixture.MovieScene);
    TestTrue(
        TEXT("Widget sandbox isolates Extension"),
        !Sandbox->GetExtensions().IsEmpty()
            && Sandbox->GetExtensions()[0]
                != Fixture.Extension);
    SandboxOwner.Reset();
    FString CleanupError;
    const bool bCleaned = Fixture.Cleanup(CleanupError);
    TestTrue(
        *FString::Printf(
            TEXT("Widget sandbox fixture unloads without resetting editor transactions: %s"),
            *CleanupError),
        bCleaned);
    return true;
}

#endif
