// Copyright 2026 Loomle contributors.

#if WITH_DEV_AUTOMATION_TESTS

#include "Sal/Blueprint/SalBlueprintInterface.h"
#include "Tests/LoomleTestEditorState.h"

#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetBlueprintGeneratedClass.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "Editor.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "EdGraphSchema_K2.h"
#include "Engine/SCS_Node.h"
#include "Engine/SimpleConstructionScript.h"
#include "Engine/TimelineTemplate.h"
#include "GameFramework/Actor.h"
#include "K2Node_BaseMCDelegate.h"
#include "K2Node_Variable.h"
#include "K2Node_VariableGet.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/AutomationTest.h"
#include "UObject/GarbageCollection.h"
#include "UObject/UObjectHash.h"
#include "UObject/Package.h"
#include "WidgetBlueprint.h"

namespace
{
using namespace Loomle::Sal;

enum class EBlueprintMutationFixtureKind : uint8
{
    Blueprint,
    WidgetBlueprint
};

bool UnloadBlueprintMutationTestPackage(
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

class FBlueprintMutationFixture
{
public:
    explicit FBlueprintMutationFixture(
        const EBlueprintMutationFixtureKind Kind)
        : Label(
            Kind == EBlueprintMutationFixtureKind::WidgetBlueprint
                ? TEXT("Widget Blueprint")
                : TEXT("Blueprint"))
    {
        const FString Token =
            FGuid::NewGuid().ToString(EGuidFormats::Digits);
        Package = CreatePackage(*FString::Printf(
            TEXT("/Game/LoomleTests/SalBlueprintMutation_%s"),
            *Token));
        const FName BlueprintName(*FString::Printf(
            TEXT("BP_Mutation_%s"),
            *Token));
        if (Kind == EBlueprintMutationFixtureKind::WidgetBlueprint)
        {
            Blueprint = FKismetEditorUtilities::CreateBlueprint(
                UUserWidget::StaticClass(),
                Package,
                BlueprintName,
                BPTYPE_Normal,
                UWidgetBlueprint::StaticClass(),
                UWidgetBlueprintGeneratedClass::StaticClass(),
                NAME_None);
        }
        else
        {
            Blueprint = FKismetEditorUtilities::CreateBlueprint(
                AActor::StaticClass(),
                Package,
                BlueprintName,
                BPTYPE_Normal,
                UBlueprint::StaticClass(),
                UBlueprintGeneratedClass::StaticClass(),
                NAME_None);
        }
        if (Package != nullptr)
        {
            Package->SetDirtyFlag(false);
        }
    }

    ~FBlueprintMutationFixture()
    {
        FString Ignored;
        Cleanup(Ignored);
    }

    FBlueprintMutationFixture(const FBlueprintMutationFixture&) = delete;
    FBlueprintMutationFixture& operator=(const FBlueprintMutationFixture&) = delete;

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
        Blueprint = nullptr;
        Package = nullptr;
        return UnloadBlueprintMutationTestPackage(
            PackageToUnload,
            OutError);
    }

    UPackage* Package = nullptr;
    UBlueprint* Blueprint = nullptr;
    FString Label;

private:
    bool bCleaned = false;
};

TSharedRef<FJsonObject> LocalReference(const FString& Alias)
{
    TSharedRef<FJsonObject> Reference = MakeShared<FJsonObject>();
    Reference->SetStringField(TEXT("kind"), TEXT("local"));
    Reference->SetStringField(TEXT("name"), Alias);
    return Reference;
}

TSharedRef<FJsonObject> MemberReference(
    const TSharedRef<FJsonObject>& Owner,
    const FString& Field)
{
    TSharedRef<FJsonObject> Reference = MakeShared<FJsonObject>();
    Reference->SetStringField(TEXT("kind"), TEXT("member"));
    Reference->SetObjectField(TEXT("object"), Owner);
    Reference->SetArrayField(
        TEXT("path"),
        {MakeShared<FJsonValueString>(Field)});
    return Reference;
}

FSalPatch DescriptionPatch(
    const FString& Description,
    const bool bDryRun)
{
    static const FString Alias = TEXT("blueprint");
    TSharedRef<FJsonObject> Set = MakeShared<FJsonObject>();
    Set->SetStringField(TEXT("kind"), TEXT("set"));
    Set->SetObjectField(
        TEXT("target"),
        MemberReference(
            LocalReference(Alias),
            TEXT("BlueprintDescription")));
    Set->SetStringField(TEXT("value"), Description);

    FSalPatch Patch;
    Patch.Alias = Alias;
    Patch.bDryRun = bDryRun;
    Patch.Statements = {MakeShared<FJsonValueObject>(Set)};
    return Patch;
}

FSalResolvedTarget BlueprintTarget(UBlueprint* Blueprint)
{
    FSalResolvedTarget Target;
    Target.Kind = ESalTargetKind::Blueprint;
    Target.Alias = TEXT("blueprint");
    Target.AssetPath = Blueprint != nullptr ? Blueprint->GetPathName() : FString();
    Target.Object = Blueprint;
    Target.Package = Blueprint != nullptr ? Blueprint->GetOutermost() : nullptr;
    Target.Blueprint = Blueprint;
    Target.Class = Blueprint != nullptr ? Blueprint->GeneratedClass.Get() : nullptr;
    Target.Interfaces = {
        FName(TEXT("asset")),
        FName(TEXT("blueprint"))};
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

struct FBlueprintTestStableIds
{
    TMap<FString, FGuid> Graphs;
    TMap<FString, FGuid> Nodes;
    TMap<FString, FGuid> Pins;
    TMap<FString, FGuid> MemberReferences;
};

FString StableGraphPath(
    const UObject* Object,
    const UBlueprint* Blueprint)
{
    TArray<FString> Segments;
    const UObject* Current = Object;
    const UClass* GeneratedClass =
        Blueprint != nullptr ? Blueprint->GeneratedClass.Get() : nullptr;
    const UClass* SkeletonClass =
        Blueprint != nullptr
            ? Blueprint->SkeletonGeneratedClass.Get()
            : nullptr;
    while (Current != nullptr
        && Current != Blueprint
        && Current != GeneratedClass
        && Current != SkeletonClass)
    {
        Segments.Insert(Current->GetName(), 0);
        Current = Current->GetOuter();
    }
    return FString::Join(Segments, TEXT("/"));
}

FBlueprintTestStableIds CaptureTestStableIds(
    const UBlueprint* Blueprint)
{
    FBlueprintTestStableIds Snapshot;
    TArray<UEdGraph*> Graphs;
    Blueprint->GetAllGraphs(Graphs);
    for (const UEdGraph* Graph : Graphs)
    {
        if (Graph == nullptr)
        {
            continue;
        }
        const FString GraphKey =
            StableGraphPath(Graph, Blueprint);
        Snapshot.Graphs.Add(GraphKey, Graph->GraphGuid);
        for (const UEdGraphNode* Node : Graph->Nodes)
        {
            if (Node == nullptr)
            {
                continue;
            }
            const FString NodeKey =
                GraphKey + TEXT("/node:") + Node->GetName();
            Snapshot.Nodes.Add(NodeKey, Node->NodeGuid);
            if (const UK2Node_Variable* VariableNode =
                    Cast<UK2Node_Variable>(Node))
            {
                Snapshot.MemberReferences.Add(
                    NodeKey,
                    VariableNode->VariableReference.GetMemberGuid());
            }
            else if (const UK2Node_BaseMCDelegate* DelegateNode =
                         Cast<UK2Node_BaseMCDelegate>(Node))
            {
                Snapshot.MemberReferences.Add(
                    NodeKey,
                    DelegateNode->DelegateReference.GetMemberGuid());
            }

            TMap<FString, int32> Occurrences;
            for (const UEdGraphPin* Pin : Node->Pins)
            {
                if (Pin == nullptr)
                {
                    continue;
                }
                const FString PinBase = FString::Printf(
                    TEXT("%s/%s:%d"),
                    *NodeKey,
                    *Pin->PinName.ToString(),
                    static_cast<int32>(Pin->Direction));
                const int32 Occurrence =
                    Occurrences.FindOrAdd(PinBase)++;
                Snapshot.Pins.Add(
                    FString::Printf(
                        TEXT("%s:%d"),
                        *PinBase,
                        Occurrence),
                    Pin->PinId);
            }
        }
    }
    return Snapshot;
}

bool TestStableGuidMapsEqual(
    const TMap<FString, FGuid>& Source,
    const TMap<FString, FGuid>& Plan)
{
    if (Source.Num() != Plan.Num())
    {
        return false;
    }
    for (const TPair<FString, FGuid>& Pair : Source)
    {
        const FGuid* PlanGuid = Plan.Find(Pair.Key);
        if (PlanGuid == nullptr || *PlanGuid != Pair.Value)
        {
            return false;
        }
    }
    return true;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FSalBlueprintDescriptionMutationTest,
    "Loomle.Sal.Blueprint.Mutation.Description",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSalBlueprintDescriptionMutationTest::RunTest(const FString& Parameters)
{
    if (GEditor == nullptr)
    {
        AddError(TEXT("Blueprint mutation tests require GEditor."));
        return false;
    }

    for (const EBlueprintMutationFixtureKind Kind : {
        EBlueprintMutationFixtureKind::Blueprint,
        EBlueprintMutationFixtureKind::WidgetBlueprint})
    {
        FBlueprintMutationFixture Fixture(Kind);
        TestNotNull(
            *FString::Printf(TEXT("%s fixture is created"), *Fixture.Label),
            Fixture.Blueprint);
        if (Fixture.Blueprint == nullptr)
        {
            continue;
        }

        const FString OriginalDescription =
            TEXT("Description before SAL mutation");
        const FString NewDescription =
            TEXT("Description authored through SAL");
        Fixture.Blueprint->BlueprintDescription = OriginalDescription;
        Fixture.Package->SetDirtyFlag(false);
        UObject* SourceCDO =
            Fixture.Blueprint->GeneratedClass != nullptr
                ? Fixture.Blueprint->GeneratedClass->GetDefaultObject(false)
                : nullptr;

        const TSharedPtr<FJsonObject> DryRun =
            FSalBlueprintInterface::Patch(
                DescriptionPatch(NewDescription, true),
                BlueprintTarget(Fixture.Blueprint));
        TestTrue(
            *FString::Printf(TEXT("%s dry-run validates"), *Fixture.Label),
            ResultBool(DryRun, TEXT("valid")));
        TestTrue(
            *FString::Printf(TEXT("%s dry-run is reported"), *Fixture.Label),
            ResultBool(DryRun, TEXT("dryRun")));
        TestFalse(
            *FString::Printf(TEXT("%s dry-run does not apply"), *Fixture.Label),
            ResultBool(DryRun, TEXT("applied"), true));
        TestEqual(
            *FString::Printf(TEXT("%s dry-run preserves source description"), *Fixture.Label),
            Fixture.Blueprint->BlueprintDescription,
            OriginalDescription);
        TestFalse(
            *FString::Printf(TEXT("%s dry-run preserves source dirty state"), *Fixture.Label),
            Fixture.Package->IsDirty());
        TestTrue(
            *FString::Printf(TEXT("%s dry-run preserves source CDO identity"), *Fixture.Label),
            Fixture.Blueprint->GeneratedClass != nullptr
                && Fixture.Blueprint->GeneratedClass->GetDefaultObject(false)
                    == SourceCDO);

        const TSharedPtr<FJsonObject> Applied =
            FSalBlueprintInterface::Patch(
                DescriptionPatch(NewDescription, false),
                BlueprintTarget(Fixture.Blueprint));
        TestTrue(
            *FString::Printf(TEXT("%s apply validates"), *Fixture.Label),
            ResultBool(Applied, TEXT("valid")));
        TestTrue(
            *FString::Printf(TEXT("%s apply reports a mutation"), *Fixture.Label),
            ResultBool(Applied, TEXT("applied")));
        TestEqual(
            *FString::Printf(TEXT("%s apply writes BlueprintDescription"), *Fixture.Label),
            Fixture.Blueprint->BlueprintDescription,
            NewDescription);

        const bool bApplied =
            ResultBool(Applied, TEXT("applied"));
        const bool bUndid =
            bApplied
            && GEditor->UndoTransaction(false);
        TestTrue(
            *FString::Printf(TEXT("%s mutation is one undo step"), *Fixture.Label),
            bUndid);
        TestEqual(
            *FString::Printf(TEXT("%s undo restores BlueprintDescription"), *Fixture.Label),
            Fixture.Blueprint->BlueprintDescription,
            OriginalDescription);

        const TSharedPtr<FJsonObject> Cleared =
            FSalBlueprintInterface::Patch(
                DescriptionPatch(FString(), false),
                BlueprintTarget(Fixture.Blueprint));
        TestTrue(
            *FString::Printf(TEXT("%s empty string validates"), *Fixture.Label),
            ResultBool(Cleared, TEXT("valid")));
        TestTrue(
            *FString::Printf(TEXT("%s empty string is applied"), *Fixture.Label),
            ResultBool(Cleared, TEXT("applied")));
        TestTrue(
            *FString::Printf(TEXT("%s BlueprintDescription can be cleared"), *Fixture.Label),
            Fixture.Blueprint->BlueprintDescription.IsEmpty());
        const bool bCleared =
            ResultBool(Cleared, TEXT("applied"));
        TestTrue(
            *FString::Printf(TEXT("%s clear is one undo step"), *Fixture.Label),
            bCleared
                && GEditor->UndoTransaction(false));
        TestEqual(
            *FString::Printf(TEXT("%s undo restores description after clear"), *Fixture.Label),
            Fixture.Blueprint->BlueprintDescription,
            OriginalDescription);
        Fixture.Package->SetDirtyFlag(false);
        FString CleanupError;
        const bool bCleaned = Fixture.Cleanup(CleanupError);
        TestTrue(
            *FString::Printf(
                TEXT("%s fixture unloads without resetting editor transactions: %s"),
                *Fixture.Label,
                *CleanupError),
            bCleaned);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FSalBlueprintTransientPlanClassStateTest,
    "Loomle.Sal.Blueprint.Mutation.TransientPlanClassState",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSalBlueprintTransientPlanClassStateTest::RunTest(
    const FString& Parameters)
{
    for (const EBlueprintMutationFixtureKind Kind : {
        EBlueprintMutationFixtureKind::Blueprint,
        EBlueprintMutationFixtureKind::WidgetBlueprint})
    {
        FBlueprintMutationFixture Fixture(Kind);
        TestNotNull(
            *FString::Printf(TEXT("%s plan fixture is created"), *Fixture.Label),
            Fixture.Blueprint);
        if (Fixture.Blueprint == nullptr)
        {
            continue;
        }
        Loomle::Tests::FScopedIsolatedTransactor FixtureTransactions;
        if (!TestTrue(
                *FString::Printf(
                    TEXT("%s fixture authoring owns an isolated transaction buffer"),
                    *Fixture.Label),
                FixtureTransactions.Initialize()))
        {
            continue;
        }

        FEdGraphPinType IdentityVariableType;
        IdentityVariableType.PinCategory =
            UEdGraphSchema_K2::PC_Int;
        const FName IdentityVariableName(TEXT("PlanIdentityValue"));
        TestTrue(
            *FString::Printf(
                TEXT("%s identity variable is created"),
                *Fixture.Label),
            FBlueprintEditorUtils::AddMemberVariable(
                Fixture.Blueprint,
                IdentityVariableName,
                IdentityVariableType));
        const int32 SourceVariableIndex =
            FBlueprintEditorUtils::FindNewVariableIndex(
                Fixture.Blueprint,
                IdentityVariableName);
        TestTrue(
            *FString::Printf(
                TEXT("%s identity variable is discoverable"),
                *Fixture.Label),
            SourceVariableIndex != INDEX_NONE);
        const FGuid SourceVariableGuid =
            SourceVariableIndex != INDEX_NONE
                ? Fixture.Blueprint
                      ->NewVariables[SourceVariableIndex]
                      .VarGuid
                : FGuid();
        const FGuid SourceBlueprintGuid =
            Fixture.Blueprint->GetBlueprintGuid();
        UEdGraph* SourceEventGraph =
            FBlueprintEditorUtils::FindEventGraph(
                Fixture.Blueprint);
        TestNotNull(
            *FString::Printf(
                TEXT("%s Event Graph is available"),
                *Fixture.Label),
            SourceEventGraph);
        UK2Node_VariableGet* SourceVariableNode =
            SourceEventGraph != nullptr
                ? GetDefault<UEdGraphSchema_K2>()
                      ->SpawnVariableGetNode(
                          FVector2D(480.0, 240.0),
                          SourceEventGraph,
                          IdentityVariableName,
                          nullptr)
                : nullptr;
        TestNotNull(
            *FString::Printf(
                TEXT("%s self variable node is created"),
                *Fixture.Label),
            SourceVariableNode);
        TestTrue(
            *FString::Printf(
                TEXT("%s self variable node uses authored variable GUID"),
                *Fixture.Label),
            SourceVariableNode != nullptr
                && SourceVariableNode
                       ->VariableReference.GetMemberGuid()
                    == SourceVariableGuid);

        UTimelineTemplate* SourceTimeline = nullptr;
        if (Kind == EBlueprintMutationFixtureKind::Blueprint)
        {
            UTimelineTemplate* AddedTimeline =
                FBlueprintEditorUtils::AddNewTimeline(
                    Fixture.Blueprint,
                    TEXT("PlanIdentityTimeline"));
            TestNotNull(
                TEXT("Blueprint identity Timeline is created"),
                AddedTimeline);
            SourceTimeline =
                Fixture.Blueprint
                    ->FindTimelineTemplateByVariableName(
                        TEXT("PlanIdentityTimeline"));
        }
        const FGuid SourceTimelineGuid =
            SourceTimeline != nullptr
                ? SourceTimeline->TimelineGuid
                : FGuid();
        const FBlueprintTestStableIds SourceStableIds =
            CaptureTestStableIds(Fixture.Blueprint);
        UBlueprintGeneratedClass* SourceClassBeforePlan =
            Cast<UBlueprintGeneratedClass>(
                Fixture.Blueprint->GeneratedClass.Get());
        UObject* SourceCDOBeforePlan =
            SourceClassBeforePlan != nullptr
                ? SourceClassBeforePlan->GetDefaultObject(false)
                : nullptr;

        FString Message;
        TStrongObjectPtr<UBlueprint> PlanGuard =
            FSalBlueprintInterface::MakeTransientPlanForTesting(
                Fixture.Blueprint,
                Message);
        UBlueprint* Plan = PlanGuard.Get();
        TestNotNull(
            *FString::Printf(
                TEXT("%s transient plan is created: %s"),
                *Fixture.Label,
                *Message),
            Plan);
        if (Plan == nullptr)
        {
            continue;
        }

        UBlueprintGeneratedClass* SourceClass =
            Cast<UBlueprintGeneratedClass>(
                Fixture.Blueprint->GeneratedClass.Get());
        UBlueprintGeneratedClass* PlanClass =
            Cast<UBlueprintGeneratedClass>(Plan->GeneratedClass.Get());
        UObject* SourceCDO =
            SourceClass != nullptr
                ? SourceClass->GetDefaultObject(false)
                : nullptr;
        UObject* PlanCDO =
            PlanClass != nullptr
                ? PlanClass->GetDefaultObject(false)
                : nullptr;
        TestTrue(
            *FString::Printf(TEXT("%s plan preserves Blueprint subtype"), *Fixture.Label),
            Kind != EBlueprintMutationFixtureKind::WidgetBlueprint
                || Plan->IsA<UWidgetBlueprint>());
        TestTrue(
            *FString::Printf(TEXT("%s plan Generated Class is isolated"), *Fixture.Label),
            PlanClass != nullptr
                && PlanClass != SourceClass
                && PlanClass->IsIn(GetTransientPackage()));
        TestFalse(
            *FString::Printf(
                TEXT("%s plan does not retain Standalone lifetime"),
                *Fixture.Label),
            Plan->HasAnyFlags(RF_Standalone)
                || (PlanClass != nullptr
                    && PlanClass->HasAnyFlags(RF_Standalone)));
        TestTrue(
            *FString::Printf(TEXT("%s plan CDO invariant holds"), *Fixture.Label),
            PlanCDO != nullptr
                && PlanCDO != SourceCDO
                && PlanCDO->GetClass() == PlanClass
                && PlanCDO->HasAnyFlags(RF_ClassDefaultObject)
                && PlanCDO->IsIn(GetTransientPackage()));
        TestTrue(
            *FString::Printf(
                TEXT("%s plan preserves source Class/CDO identity"),
                *Fixture.Label),
            SourceClass == SourceClassBeforePlan
                && SourceCDO == SourceCDOBeforePlan);
        TestEqual(
            *FString::Printf(
                TEXT("%s plan preserves Blueprint GUID"),
                *Fixture.Label),
            Plan->GetBlueprintGuid(),
            SourceBlueprintGuid);
        const int32 PlanVariableIndex =
            FBlueprintEditorUtils::FindNewVariableIndex(
                Plan,
                IdentityVariableName);
        TestTrue(
            *FString::Printf(
                TEXT("%s plan preserves variable identity"),
                *Fixture.Label),
            PlanVariableIndex != INDEX_NONE
                && Plan->NewVariables[PlanVariableIndex].VarGuid
                    == SourceVariableGuid);
        UBlueprintGeneratedClass* PlanSkeletonClass =
            Cast<UBlueprintGeneratedClass>(
                Plan->SkeletonGeneratedClass.Get());
        TestTrue(
            *FString::Printf(
                TEXT("%s plan Generated/Skeleton Class GUID maps are coherent"),
                *Fixture.Label),
            PlanClass != nullptr
                && PlanSkeletonClass != nullptr
                && PlanClass->FindBlueprintPropertyGuidFromName(
                       IdentityVariableName)
                    == SourceVariableGuid
                && PlanSkeletonClass
                       ->FindBlueprintPropertyGuidFromName(
                           IdentityVariableName)
                    == SourceVariableGuid);
        const FBlueprintTestStableIds PlanStableIds =
            CaptureTestStableIds(Plan);
        TestTrue(
            *FString::Printf(
                TEXT("%s plan preserves every Graph GUID"),
                *Fixture.Label),
            TestStableGuidMapsEqual(
                SourceStableIds.Graphs,
                PlanStableIds.Graphs));
        TestTrue(
            *FString::Printf(
                TEXT("%s plan preserves every Node GUID"),
                *Fixture.Label),
            TestStableGuidMapsEqual(
                SourceStableIds.Nodes,
                PlanStableIds.Nodes));
        TestTrue(
            *FString::Printf(
                TEXT("%s plan preserves every Pin ID"),
                *Fixture.Label),
            TestStableGuidMapsEqual(
                SourceStableIds.Pins,
                PlanStableIds.Pins));
        TestTrue(
            *FString::Printf(
                TEXT("%s plan restores every variable/delegate member GUID"),
                *Fixture.Label),
            TestStableGuidMapsEqual(
                SourceStableIds.MemberReferences,
                PlanStableIds.MemberReferences));

        if (Kind == EBlueprintMutationFixtureKind::Blueprint)
        {
            USimpleConstructionScript* SourceSCS =
                Fixture.Blueprint->SimpleConstructionScript;
            USimpleConstructionScript* PlanSCS =
                Plan->SimpleConstructionScript;
            TestTrue(
                TEXT("Blueprint plan isolates SimpleConstructionScript"),
                SourceSCS != nullptr
                    && PlanSCS != nullptr
                    && PlanSCS != SourceSCS
                    && PlanSCS->IsIn(PlanClass));
            const TArray<USCS_Node*> SourceSCSNodes =
                SourceSCS != nullptr
                    ? SourceSCS->GetAllNodes()
                    : TArray<USCS_Node*>();
            const TArray<USCS_Node*> PlanSCSNodes =
                PlanSCS != nullptr
                    ? PlanSCS->GetAllNodes()
                    : TArray<USCS_Node*>();
            TestTrue(
                TEXT("Blueprint fixture contains SCS identity state"),
                !SourceSCSNodes.IsEmpty());
            TestEqual(
                TEXT("Blueprint plan preserves SCS Node count"),
                PlanSCSNodes.Num(),
                SourceSCSNodes.Num());
            for (const USCS_Node* SourceNode : SourceSCSNodes)
            {
                const USCS_Node* PlanNode = nullptr;
                for (const USCS_Node* Candidate : PlanSCSNodes)
                {
                    if (Candidate != nullptr
                        && SourceNode != nullptr
                        && Candidate->GetFName()
                            == SourceNode->GetFName()
                        && Candidate->GetVariableName()
                            == SourceNode->GetVariableName())
                    {
                        PlanNode = Candidate;
                        break;
                    }
                }
                TestTrue(
                    TEXT("Blueprint plan preserves and isolates each SCS Node"),
                    SourceNode != nullptr
                        && PlanNode != nullptr
                        && PlanNode != SourceNode
                        && PlanNode->VariableGuid
                            == SourceNode->VariableGuid
                        && PlanNode->IsIn(PlanClass));
                if (SourceNode != nullptr
                    && SourceNode->ComponentTemplate != nullptr)
                {
                    TestTrue(
                        TEXT("Blueprint plan isolates each SCS Component Template"),
                        PlanNode != nullptr
                            && PlanNode->ComponentTemplate != nullptr
                            && PlanNode->ComponentTemplate
                                != SourceNode->ComponentTemplate
                            && PlanNode->ComponentTemplate
                                   ->IsIn(PlanClass));
                }
            }

            UTimelineTemplate* PlanTimeline =
                Plan->FindTimelineTemplateByVariableName(
                    TEXT("PlanIdentityTimeline"));
            TestTrue(
                TEXT("Blueprint plan preserves and isolates Timeline identity"),
                SourceTimeline != nullptr
                    && PlanTimeline != nullptr
                    && PlanTimeline != SourceTimeline
                    && PlanTimeline->TimelineGuid
                        == SourceTimelineGuid
                    && PlanTimeline->IsIn(PlanClass));
        }

        FString ValidationMessage;
        TestTrue(
            *FString::Printf(
                TEXT("%s plan is safe for UE modification callbacks: %s"),
                *Fixture.Label,
                *ValidationMessage),
            FSalBlueprintInterface::
                ValidateModificationClassStateForTesting(
                    Plan,
                    ValidationMessage));
        PlanGuard.Reset();
        FixtureTransactions.Restore();
        FString CleanupError;
        const bool bCleaned = Fixture.Cleanup(CleanupError);
        TestTrue(
            *FString::Printf(
                TEXT("%s transient-plan fixture unloads without resetting editor state: %s"),
                *Fixture.Label,
                *CleanupError),
            bCleaned);
    }

    FBlueprintMutationFixture Fixture(
        EBlueprintMutationFixtureKind::Blueprint);
    TestNotNull(TEXT("Derived-Class fail-closed fixture is created"), Fixture.Blueprint);
    if (Fixture.Blueprint != nullptr
        && Fixture.Blueprint->GeneratedClass != nullptr)
    {
        UBlueprintGeneratedClass* DerivedWithoutCDO =
            NewObject<UBlueprintGeneratedClass>(
                GetTransientPackage(),
                MakeUniqueObjectName(
                    GetTransientPackage(),
                    UBlueprintGeneratedClass::StaticClass(),
                    TEXT("SALDerivedWithoutCDO")),
                RF_Transient);
        DerivedWithoutCDO->SetSuperStruct(
            Fixture.Blueprint->GeneratedClass.Get());
        TestNull(
            TEXT("Synthetic loaded derived BPGC starts without a CDO"),
            DerivedWithoutCDO->GetDefaultObject(false));

        FString ValidationMessage;
        TestFalse(
            TEXT("Loaded derived BPGC without a CDO fails closed"),
            FSalBlueprintInterface::
                ValidateModificationClassStateForTesting(
                    Fixture.Blueprint,
                    ValidationMessage));
        TestTrue(
            TEXT("Derived-Class failure identifies the missing CDO"),
            ValidationMessage.Contains(TEXT("derived"))
                && ValidationMessage.Contains(TEXT("default object")));

        const FString OriginalDescription =
            Fixture.Blueprint->BlueprintDescription;
        const TSharedPtr<FJsonObject> Rejected =
            FSalBlueprintInterface::Patch(
                DescriptionPatch(TEXT("Must not be written"), true),
                BlueprintTarget(Fixture.Blueprint));
        TestFalse(
            TEXT("Patch rejects unsafe loaded derived Class state"),
            ResultBool(Rejected, TEXT("valid"), true));
        TestEqual(
            TEXT("Fail-closed Patch preserves the source Blueprint"),
            Fixture.Blueprint->BlueprintDescription,
            OriginalDescription);

        DerivedWithoutCDO->SetSuperStruct(nullptr);
        DerivedWithoutCDO->MarkAsGarbage();
    }
    FString CleanupError;
    const bool bCleaned = Fixture.Cleanup(CleanupError);
    TestTrue(
        *FString::Printf(
            TEXT("Derived-Class fixture unloads without resetting editor transactions: %s"),
            *CleanupError),
        bCleaned);
    return true;
}

#endif
