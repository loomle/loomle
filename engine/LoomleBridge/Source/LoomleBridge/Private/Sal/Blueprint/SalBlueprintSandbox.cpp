// Copyright 2026 Loomle contributors.

#include "SalBlueprintSandbox.h"

#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintCore.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Engine/SCS_Node.h"
#include "Engine/SimpleConstructionScript.h"
#include "Engine/TimelineTemplate.h"
#include "K2Node_BaseMCDelegate.h"
#include "K2Node_Variable.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "BlueprintEditorSettings.h"
#include "UObject/UObjectHash.h"
#include "UObject/UnrealType.h"

namespace Loomle::Sal
{
namespace
{
struct FBlueprintStableIdSnapshot
{
    FGuid BlueprintGuid;
    TMap<FString, FGuid> Variables;
    TMap<FString, FGuid> Graphs;
    TMap<FString, FGuid> Nodes;
    TMap<FString, FGuid> Pins;
    TMap<FString, FGuid> MemberReferences;
    TMap<FString, FGuid> SCSNodes;
    TMap<FString, FGuid> Timelines;
};

struct FTimelineDisplayOrderSnapshot
{
    FGuid TimelineGuid;
    FName VariableName;
    TArray<FTTTrackId> DisplayOrder;
    TWeakObjectPtr<UTimelineTemplate> Source;
};

void RestoreTimelineDisplayOrder(
    UTimelineTemplate* Timeline,
    const TArray<FTTTrackId>& DisplayOrder)
{
    if (Timeline == nullptr)
    {
        return;
    }
    while (Timeline->GetNumDisplayTracks() > 0)
    {
        Timeline->RemoveDisplayTrack(
            Timeline->GetNumDisplayTracks() - 1);
    }
    for (const FTTTrackId& TrackId : DisplayOrder)
    {
        Timeline->AddDisplayTrack(TrackId);
    }
}

void RestoreAuthoredDuplicateTransientFields(
    const UBlueprint* Source,
    UBlueprint* Sandbox)
{
    if (Source == nullptr || Sandbox == nullptr)
    {
        return;
    }

    // UE intentionally resets DuplicateTransient properties during ordinary
    // duplication. Editable properties still represent authored Blueprint
    // state, however, so a preflight sandbox must see their exact source
    // values. In UE 5.7 this includes BlueprintDisplayName and
    // BlueprintDescription.
    for (TFieldIterator<FProperty> Property(Source->GetClass());
         Property;
         ++Property)
    {
        if (Property->HasAllPropertyFlags(
                CPF_Edit | CPF_DuplicateTransient))
        {
            Property->CopyCompleteValue_InContainer(
                Sandbox,
                Source);
        }
    }
}

class FScopedSourceTimelineDisplayOrderRestoration
{
public:
    explicit FScopedSourceTimelineDisplayOrderRestoration(
        UBlueprint* Source)
    {
        if (Source == nullptr)
        {
            return;
        }
        Snapshots.Reserve(Source->Timelines.Num());
        for (UTimelineTemplate* Timeline : Source->Timelines)
        {
            if (Timeline == nullptr)
            {
                continue;
            }
            FTimelineDisplayOrderSnapshot Snapshot;
            Snapshot.TimelineGuid = Timeline->TimelineGuid;
            Snapshot.VariableName = Timeline->GetVariableName();
            Snapshot.Source = Timeline;
            Snapshot.DisplayOrder.Reserve(
                Timeline->GetNumDisplayTracks());
            for (int32 Index = 0;
                 Index < Timeline->GetNumDisplayTracks();
                 ++Index)
            {
                Snapshot.DisplayOrder.Add(
                    Timeline->GetDisplayTrackId(Index));
            }
            Snapshots.Add(MoveTemp(Snapshot));
        }
    }

    ~FScopedSourceTimelineDisplayOrderRestoration()
    {
        RestoreSource();
    }

    void RestoreSource() const
    {
        for (const FTimelineDisplayOrderSnapshot& Snapshot : Snapshots)
        {
            RestoreTimelineDisplayOrder(
                Snapshot.Source.Get(),
                Snapshot.DisplayOrder);
        }
    }

    const TArray<FTimelineDisplayOrderSnapshot>& GetSnapshots() const
    {
        return Snapshots;
    }

private:
    TArray<FTimelineDisplayOrderSnapshot> Snapshots;
};

bool RestoreSandboxTimelineDisplayOrders(
    UBlueprint* Sandbox,
    const TArray<FTimelineDisplayOrderSnapshot>& Snapshots,
    FString& OutMessage)
{
    if (Sandbox == nullptr
        || Sandbox->Timelines.Num() != Snapshots.Num())
    {
        OutMessage =
            TEXT("Blueprint transient Timeline collection changed during duplication.");
        return false;
    }

    TSet<UTimelineTemplate*> Matched;
    for (const FTimelineDisplayOrderSnapshot& Snapshot : Snapshots)
    {
        UTimelineTemplate* Match = nullptr;
        for (UTimelineTemplate* Timeline : Sandbox->Timelines)
        {
            if (Timeline != nullptr
                && Timeline->TimelineGuid == Snapshot.TimelineGuid
                && Timeline->GetVariableName() == Snapshot.VariableName)
            {
                if (Match != nullptr)
                {
                    OutMessage =
                        TEXT("Blueprint transient Timeline identity is ambiguous after duplication.");
                    return false;
                }
                Match = Timeline;
            }
        }
        if (Match == nullptr || Matched.Contains(Match))
        {
            OutMessage =
                TEXT("Blueprint transient Timeline identity could not be restored after duplication.");
            return false;
        }
        Matched.Add(Match);
        RestoreTimelineDisplayOrder(
            Match,
            Snapshot.DisplayOrder);
    }
    return true;
}

/**
 * UE's native Blueprint PostDuplicate path copies breakpoints and watched
 * pins through FKismetDebugUtilities. Those helpers key the duplicate by its
 * transient object path and immediately save EditorPerProjectUserSettings.
 * Temporarily hiding only the source Blueprint's debugger entry preserves the
 * native structural duplication path without making preflight write user
 * configuration.
 */
class FScopedBlueprintDebuggerSettingsSuppression
{
public:
    explicit FScopedBlueprintDebuggerSettingsSuppression(
        const UBlueprint* Source)
        : Settings(GetMutableDefault<UBlueprintEditorSettings>())
        , SourcePath(Source != nullptr ? Source->GetPathName() : FString())
    {
        if (Settings == nullptr || SourcePath.IsEmpty())
        {
            return;
        }
        if (const FPerBlueprintSettings* Existing =
                Settings->PerBlueprintSettings.Find(SourcePath))
        {
            HiddenSettings.Emplace(*Existing);
            Settings->PerBlueprintSettings.Remove(SourcePath);
        }
    }

    ~FScopedBlueprintDebuggerSettingsSuppression()
    {
        if (Settings != nullptr && HiddenSettings.IsSet())
        {
            Settings->PerBlueprintSettings.Add(
                SourcePath,
                MoveTemp(HiddenSettings.GetValue()));
        }
    }

private:
    UBlueprintEditorSettings* Settings = nullptr;
    FString SourcePath;
    TOptional<FPerBlueprintSettings> HiddenSettings;
};

bool AddStableGuid(
    TMap<FString, FGuid>& Target,
    const FString& Key,
    const FGuid& Guid,
    const TCHAR* Kind,
    FString& OutMessage)
{
    if (Target.Contains(Key))
    {
        OutMessage = FString::Printf(
            TEXT("Blueprint transient identity contains duplicate %s key: %s."),
            Kind,
            *Key);
        return false;
    }
    Target.Add(Key, Guid);
    return true;
}

FString RelativeBlueprintObjectPath(
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

bool CaptureBlueprintStableIds(
    const UBlueprint* Blueprint,
    FBlueprintStableIdSnapshot& OutSnapshot,
    FString& OutMessage)
{
    if (Blueprint == nullptr)
    {
        OutMessage =
            TEXT("Blueprint is unavailable for transient identity audit.");
        return false;
    }

    OutSnapshot.BlueprintGuid = Blueprint->GetBlueprintGuid();
    for (const FBPVariableDescription& Variable : Blueprint->NewVariables)
    {
        if (!AddStableGuid(
                OutSnapshot.Variables,
                Variable.VarName.ToString(),
                Variable.VarGuid,
                TEXT("variable"),
                OutMessage))
        {
            return false;
        }
    }

    TArray<UEdGraph*> Graphs;
    Blueprint->GetAllGraphs(Graphs);
    for (const UEdGraph* Graph : Graphs)
    {
        if (Graph == nullptr)
        {
            OutMessage =
                TEXT("Blueprint transient identity contains a null Graph.");
            return false;
        }
        const FString GraphKey =
            RelativeBlueprintObjectPath(Graph, Blueprint);
        if (!AddStableGuid(
                OutSnapshot.Graphs,
                GraphKey,
                Graph->GraphGuid,
                TEXT("graph"),
                OutMessage))
        {
            return false;
        }

        for (const UEdGraphNode* Node : Graph->Nodes)
        {
            if (Node == nullptr)
            {
                OutMessage = FString::Printf(
                    TEXT("Blueprint transient identity contains a null Node in %s."),
                    *GraphKey);
                return false;
            }
            const FString NodeKey =
                GraphKey + TEXT("/node:") + Node->GetName();
            if (!AddStableGuid(
                    OutSnapshot.Nodes,
                    NodeKey,
                    Node->NodeGuid,
                    TEXT("node"),
                    OutMessage))
            {
                return false;
            }

            if (const UK2Node_Variable* VariableNode =
                    Cast<UK2Node_Variable>(Node))
            {
                if (!AddStableGuid(
                        OutSnapshot.MemberReferences,
                        NodeKey,
                        VariableNode->VariableReference.GetMemberGuid(),
                        TEXT("member reference"),
                        OutMessage))
                {
                    return false;
                }
            }
            else if (const UK2Node_BaseMCDelegate* DelegateNode =
                         Cast<UK2Node_BaseMCDelegate>(Node))
            {
                if (!AddStableGuid(
                        OutSnapshot.MemberReferences,
                        NodeKey,
                        DelegateNode->DelegateReference.GetMemberGuid(),
                        TEXT("member reference"),
                        OutMessage))
                {
                    return false;
                }
            }

            TMap<FString, int32> PinOccurrences;
            for (const UEdGraphPin* Pin : Node->Pins)
            {
                if (Pin == nullptr)
                {
                    OutMessage = FString::Printf(
                        TEXT("Blueprint transient identity contains a null Pin on %s."),
                        *NodeKey);
                    return false;
                }
                const FString PinBase = FString::Printf(
                    TEXT("%s/%s:%d"),
                    *NodeKey,
                    *Pin->PinName.ToString(),
                    static_cast<int32>(Pin->Direction));
                const int32 Occurrence =
                    PinOccurrences.FindOrAdd(PinBase)++;
                if (!AddStableGuid(
                        OutSnapshot.Pins,
                        FString::Printf(
                            TEXT("%s:%d"),
                            *PinBase,
                            Occurrence),
                        Pin->PinId,
                        TEXT("pin"),
                        OutMessage))
                {
                    return false;
                }
            }
        }
    }

    if (Blueprint->SimpleConstructionScript != nullptr)
    {
        for (const USCS_Node* Node :
             Blueprint->SimpleConstructionScript->GetAllNodes())
        {
            if (Node == nullptr)
            {
                OutMessage =
                    TEXT("Blueprint transient identity contains a null SCS Node.");
                return false;
            }
            if (!AddStableGuid(
                    OutSnapshot.SCSNodes,
                    FString::Printf(
                        TEXT("%s/%s"),
                        *Node->GetVariableName().ToString(),
                        *Node->GetName()),
                    Node->VariableGuid,
                    TEXT("SCS node"),
                    OutMessage))
            {
                return false;
            }
        }
    }

    for (const UTimelineTemplate* Timeline : Blueprint->Timelines)
    {
        if (Timeline == nullptr)
        {
            OutMessage =
                TEXT("Blueprint transient identity contains a null Timeline.");
            return false;
        }
        if (!AddStableGuid(
                OutSnapshot.Timelines,
                Timeline->GetVariableName().ToString(),
                Timeline->TimelineGuid,
                TEXT("timeline"),
                OutMessage))
        {
            return false;
        }
    }
    return true;
}

bool StableGuidMapsMatch(
    const TMap<FString, FGuid>& Source,
    const TMap<FString, FGuid>& Copy,
    const TCHAR* Kind,
    FString& OutMessage)
{
    if (Source.Num() != Copy.Num())
    {
        OutMessage = FString::Printf(
            TEXT("UE transient duplication changed the %s count (%d -> %d)."),
            Kind,
            Source.Num(),
            Copy.Num());
        return false;
    }
    for (const TPair<FString, FGuid>& Pair : Source)
    {
        const FGuid* CopyGuid = Copy.Find(Pair.Key);
        if (CopyGuid == nullptr || *CopyGuid != Pair.Value)
        {
            OutMessage = FString::Printf(
                TEXT("UE transient duplication changed %s identity: %s."),
                Kind,
                *Pair.Key);
            return false;
        }
    }
    return true;
}

bool ValidateTransientBlueprintIdentity(
    const FBlueprintStableIdSnapshot& OriginalSourceIds,
    const UBlueprint* Source,
    const UBlueprint* Copy,
    FString& OutMessage)
{
    FBlueprintStableIdSnapshot CurrentSourceIds;
    FBlueprintStableIdSnapshot CopyIds;
    if (!CaptureBlueprintStableIds(
            Source,
            CurrentSourceIds,
            OutMessage)
        || !CaptureBlueprintStableIds(Copy, CopyIds, OutMessage))
    {
        return false;
    }

    const auto SnapshotMatches =
        [&OutMessage](
            const FBlueprintStableIdSnapshot& Expected,
            const FBlueprintStableIdSnapshot& Actual,
            const TCHAR* Target)
        {
            if (Expected.BlueprintGuid != Actual.BlueprintGuid)
            {
                OutMessage = FString::Printf(
                    TEXT("UE transient duplication changed the %s Blueprint GUID."),
                    Target);
                return false;
            }
            if (!StableGuidMapsMatch(
                    Expected.Variables,
                    Actual.Variables,
                    TEXT("variable"),
                    OutMessage)
                || !StableGuidMapsMatch(
                    Expected.Graphs,
                    Actual.Graphs,
                    TEXT("graph"),
                    OutMessage)
                || !StableGuidMapsMatch(
                    Expected.Nodes,
                    Actual.Nodes,
                    TEXT("node"),
                    OutMessage)
                || !StableGuidMapsMatch(
                    Expected.Pins,
                    Actual.Pins,
                    TEXT("pin"),
                    OutMessage)
                || !StableGuidMapsMatch(
                    Expected.MemberReferences,
                    Actual.MemberReferences,
                    TEXT("member reference"),
                    OutMessage)
                || !StableGuidMapsMatch(
                    Expected.SCSNodes,
                    Actual.SCSNodes,
                    TEXT("SCS node"),
                    OutMessage)
                || !StableGuidMapsMatch(
                    Expected.Timelines,
                    Actual.Timelines,
                    TEXT("timeline"),
                    OutMessage))
            {
                OutMessage = FString::Printf(
                    TEXT("%s identity changed during transient duplication: %s"),
                    Target,
                    *OutMessage);
                return false;
            }
            return true;
        };

    return SnapshotMatches(
               OriginalSourceIds,
               CurrentSourceIds,
               TEXT("source"))
        && SnapshotMatches(
               OriginalSourceIds,
               CopyIds,
               TEXT("plan"));
}

FString PinStructuralLocator(const UEdGraphPin* Pin)
{
    if (Pin == nullptr)
    {
        return TEXT("<null>");
    }
    const FString Segment = FString::Printf(
        TEXT("%s:%d"),
        *Pin->PinName.ToString(),
        static_cast<int32>(Pin->Direction));
    return Pin->ParentPin != nullptr
        ? PinStructuralLocator(Pin->ParentPin) + TEXT("/") + Segment
        : Segment;
}

bool PinsHaveEquivalentStructure(
    const UEdGraphPin* Source,
    const UEdGraphPin* Copy)
{
    return Source != nullptr
        && Copy != nullptr
        && Source->PinName == Copy->PinName
        && Source->Direction == Copy->Direction
        && Source->PinType == Copy->PinType
        && (Source->ParentPin != nullptr)
            == (Copy->ParentPin != nullptr)
        && Source->SubPins.Num() == Copy->SubPins.Num()
        && PinStructuralLocator(Source)
            == PinStructuralLocator(Copy);
}

bool RestoreNodePinIdentities(
    const FString& GraphKey,
    const UEdGraphNode* SourceNode,
    UEdGraphNode* CopyNode,
    FString& OutMessage)
{
    if (SourceNode == nullptr || CopyNode == nullptr)
    {
        OutMessage = FString::Printf(
            TEXT("Blueprint transient Pin identity has a missing Node in %s."),
            *GraphKey);
        return false;
    }
    if (SourceNode->GetClass() != CopyNode->GetClass()
        || SourceNode->NodeGuid != CopyNode->NodeGuid)
    {
        OutMessage = FString::Printf(
            TEXT("UE transient duplication changed Node identity before Pin restoration: %s/%s."),
            *GraphKey,
            *SourceNode->GetName());
        return false;
    }
    if (SourceNode->Pins.Num() != CopyNode->Pins.Num())
    {
        OutMessage = FString::Printf(
            TEXT("UE transient duplication changed the Pin count on %s/%s (%d -> %d)."),
            *GraphKey,
            *SourceNode->GetName(),
            SourceNode->Pins.Num(),
            CopyNode->Pins.Num());
        return false;
    }

    TArray<int32> SourceToCopy;
    SourceToCopy.Init(INDEX_NONE, SourceNode->Pins.Num());
    TSet<int32> UsedCopyPins;

    const auto MatchPass =
        [&](
            const TCHAR* MatchKind,
            TFunctionRef<bool(
                const UEdGraphPin*,
                const UEdGraphPin*)> IsCandidate)
        {
            for (int32 SourceIndex = 0;
                 SourceIndex < SourceNode->Pins.Num();
                 ++SourceIndex)
            {
                if (SourceToCopy[SourceIndex] != INDEX_NONE)
                {
                    continue;
                }
                const UEdGraphPin* SourcePin =
                    SourceNode->Pins[SourceIndex];
                if (SourcePin == nullptr)
                {
                    OutMessage = FString::Printf(
                        TEXT("Blueprint transient identity contains a null source Pin on %s/%s."),
                        *GraphKey,
                        *SourceNode->GetName());
                    return false;
                }

                int32 MatchIndex = INDEX_NONE;
                for (int32 CopyIndex = 0;
                     CopyIndex < CopyNode->Pins.Num();
                     ++CopyIndex)
                {
                    if (UsedCopyPins.Contains(CopyIndex))
                    {
                        continue;
                    }
                    UEdGraphPin* CopyPin =
                        CopyNode->Pins[CopyIndex];
                    if (CopyPin != nullptr
                        && IsCandidate(SourcePin, CopyPin))
                    {
                        if (MatchIndex != INDEX_NONE)
                        {
                            OutMessage = FString::Printf(
                                TEXT("Blueprint transient Pin identity is ambiguous after duplication (%s match): %s/%s/%s."),
                                MatchKind,
                                *GraphKey,
                                *SourceNode->GetName(),
                                *PinStructuralLocator(SourcePin));
                            return false;
                        }
                        MatchIndex = CopyIndex;
                    }
                }
                if (MatchIndex != INDEX_NONE)
                {
                    SourceToCopy[SourceIndex] = MatchIndex;
                    UsedCopyPins.Add(MatchIndex);
                }
            }
            return true;
        };

    if (!MatchPass(
            TEXT("PinId"),
            [](const UEdGraphPin* SourcePin, const UEdGraphPin* CopyPin)
            {
                return SourcePin->PinId == CopyPin->PinId;
            })
        || !MatchPass(
            TEXT("PersistentGuid"),
            [](const UEdGraphPin* SourcePin, const UEdGraphPin* CopyPin)
            {
                return SourcePin->PersistentGuid.IsValid()
                    && SourcePin->PersistentGuid
                        == CopyPin->PersistentGuid
                    && PinsHaveEquivalentStructure(
                        SourcePin,
                        CopyPin);
            })
        || !MatchPass(
            TEXT("structure"),
            [](const UEdGraphPin* SourcePin, const UEdGraphPin* CopyPin)
            {
                return PinsHaveEquivalentStructure(
                    SourcePin,
                    CopyPin);
            }))
    {
        return false;
    }

    TSet<FGuid> SourcePinIds;
    for (int32 SourceIndex = 0;
         SourceIndex < SourceNode->Pins.Num();
         ++SourceIndex)
    {
        const UEdGraphPin* SourcePin =
            SourceNode->Pins[SourceIndex];
        const int32 CopyIndex = SourceToCopy[SourceIndex];
        if (SourcePin == nullptr
            || !CopyNode->Pins.IsValidIndex(CopyIndex)
            || CopyNode->Pins[CopyIndex] == nullptr)
        {
            OutMessage = FString::Printf(
                TEXT("UE transient duplication could not uniquely restore Pin identity: %s/%s/%s."),
                *GraphKey,
                *SourceNode->GetName(),
                *PinStructuralLocator(SourcePin));
            return false;
        }
        if (!SourcePin->PinId.IsValid()
            || SourcePinIds.Contains(SourcePin->PinId))
        {
            OutMessage = FString::Printf(
                TEXT("Blueprint source contains an invalid or duplicate PinId: %s/%s/%s."),
                *GraphKey,
                *SourceNode->GetName(),
                *PinStructuralLocator(SourcePin));
            return false;
        }
        SourcePinIds.Add(SourcePin->PinId);
        CopyNode->Pins[CopyIndex]->PinId = SourcePin->PinId;
    }
    return true;
}

bool RestoreSandboxPinIdentities(
    const UBlueprint* Source,
    UBlueprint* Copy,
    FString& OutMessage)
{
    TArray<UEdGraph*> SourceGraphs;
    TArray<UEdGraph*> CopyGraphs;
    Source->GetAllGraphs(SourceGraphs);
    Copy->GetAllGraphs(CopyGraphs);
    if (SourceGraphs.Num() != CopyGraphs.Num())
    {
        OutMessage = FString::Printf(
            TEXT("UE transient duplication changed the Graph count before Pin restoration (%d -> %d)."),
            SourceGraphs.Num(),
            CopyGraphs.Num());
        return false;
    }

    TMap<FString, UEdGraph*> CopyGraphsByPath;
    for (UEdGraph* CopyGraph : CopyGraphs)
    {
        if (CopyGraph == nullptr)
        {
            OutMessage =
                TEXT("Blueprint transient identity contains a null copied Graph.");
            return false;
        }
        const FString GraphKey =
            RelativeBlueprintObjectPath(CopyGraph, Copy);
        if (CopyGraphsByPath.Contains(GraphKey))
        {
            OutMessage = FString::Printf(
                TEXT("Blueprint transient Graph identity is ambiguous after duplication: %s."),
                *GraphKey);
            return false;
        }
        CopyGraphsByPath.Add(GraphKey, CopyGraph);
    }

    for (const UEdGraph* SourceGraph : SourceGraphs)
    {
        if (SourceGraph == nullptr)
        {
            OutMessage =
                TEXT("Blueprint transient identity contains a null source Graph.");
            return false;
        }
        const FString GraphKey =
            RelativeBlueprintObjectPath(SourceGraph, Source);
        UEdGraph* const* CopyGraphPtr =
            CopyGraphsByPath.Find(GraphKey);
        UEdGraph* CopyGraph =
            CopyGraphPtr != nullptr ? *CopyGraphPtr : nullptr;
        if (CopyGraph == nullptr
            || CopyGraph->GraphGuid != SourceGraph->GraphGuid
            || CopyGraph->Nodes.Num() != SourceGraph->Nodes.Num())
        {
            OutMessage = FString::Printf(
                TEXT("UE transient duplication changed Graph identity or Node count before Pin restoration: %s."),
                *GraphKey);
            return false;
        }

        TMap<FName, UEdGraphNode*> CopyNodesByName;
        for (UEdGraphNode* CopyNode : CopyGraph->Nodes)
        {
            if (CopyNode == nullptr
                || CopyNodesByName.Contains(CopyNode->GetFName()))
            {
                OutMessage = FString::Printf(
                    TEXT("Blueprint transient Node identity is missing or ambiguous after duplication: %s."),
                    *GraphKey);
                return false;
            }
            CopyNodesByName.Add(CopyNode->GetFName(), CopyNode);
        }
        for (const UEdGraphNode* SourceNode : SourceGraph->Nodes)
        {
            UEdGraphNode* const* CopyNodePtr =
                SourceNode != nullptr
                    ? CopyNodesByName.Find(SourceNode->GetFName())
                    : nullptr;
            if (!RestoreNodePinIdentities(
                    GraphKey,
                    SourceNode,
                    CopyNodePtr != nullptr ? *CopyNodePtr : nullptr,
                    OutMessage))
            {
                return false;
            }
        }
    }
    return true;
}

bool RestoreTransientBlueprintIdentity(
    const UBlueprint* Source,
    UBlueprint* Copy,
    FString& OutMessage)
{
    if (Source == nullptr || Copy == nullptr)
    {
        OutMessage =
            TEXT("Blueprint identity is unavailable for transient preflight.");
        return false;
    }
    if (Source->NewVariables.Num() != Copy->NewVariables.Num())
    {
        OutMessage = FString::Printf(
            TEXT("UE transient duplication changed the Blueprint variable count (%d -> %d)."),
            Source->NewVariables.Num(),
            Copy->NewVariables.Num());
        return false;
    }

    FStructProperty* BlueprintGuidProperty =
        FindFProperty<FStructProperty>(
            UBlueprintCore::StaticClass(),
            TEXT("BlueprintGuid"));
    FGuid* CopyBlueprintGuid =
        BlueprintGuidProperty != nullptr
            ? BlueprintGuidProperty->ContainerPtrToValuePtr<FGuid>(Copy)
            : nullptr;
    if (CopyBlueprintGuid == nullptr)
    {
        OutMessage =
            TEXT("UE Blueprint GUID storage is unavailable for transient preflight.");
        return false;
    }
    *CopyBlueprintGuid = Source->GetBlueprintGuid();

    if (!RestoreSandboxPinIdentities(Source, Copy, OutMessage))
    {
        return false;
    }

    TMap<FGuid, FGuid> DuplicatedToSourceVarGuids;
    for (int32 SourceIndex = 0;
         SourceIndex < Source->NewVariables.Num();
         ++SourceIndex)
    {
        const FBPVariableDescription& SourceVariable =
            Source->NewVariables[SourceIndex];
        if (!Copy->NewVariables.IsValidIndex(SourceIndex)
            || Copy->NewVariables[SourceIndex].VarName
                != SourceVariable.VarName)
        {
            OutMessage = FString::Printf(
                TEXT("UE transient duplication changed variable order or name at index %d (%s)."),
                SourceIndex,
                *SourceVariable.VarName.ToString());
            return false;
        }

        FBPVariableDescription& CopyVariable =
            Copy->NewVariables[SourceIndex];
        if (CopyVariable.VarGuid != SourceVariable.VarGuid)
        {
            DuplicatedToSourceVarGuids.Add(
                CopyVariable.VarGuid,
                SourceVariable.VarGuid);
            CopyVariable.VarGuid = SourceVariable.VarGuid;
        }
    }

    if (!DuplicatedToSourceVarGuids.IsEmpty())
    {
        TArray<UEdGraphNode*> Nodes;
        FBlueprintEditorUtils::GetAllNodesOfClass(Copy, Nodes);
        for (UEdGraphNode* Node : Nodes)
        {
            FMemberReference* MemberReference = nullptr;
            if (UK2Node_Variable* VariableNode =
                    Cast<UK2Node_Variable>(Node))
            {
                MemberReference = &VariableNode->VariableReference;
            }
            else if (UK2Node_BaseMCDelegate* DelegateNode =
                         Cast<UK2Node_BaseMCDelegate>(Node))
            {
                MemberReference = &DelegateNode->DelegateReference;
            }
            if (MemberReference == nullptr)
            {
                continue;
            }
            const FGuid* SourceGuid =
                DuplicatedToSourceVarGuids.Find(
                    MemberReference->GetMemberGuid());
            if (SourceGuid == nullptr)
            {
                continue;
            }
            if (MemberReference->IsSelfContext())
            {
                MemberReference->SetSelfMember(
                    MemberReference->GetMemberName(),
                    *SourceGuid);
            }
            else
            {
                MemberReference->SetExternalMember(
                    MemberReference->GetMemberName(),
                    MemberReference->GetMemberParentClass(),
                    *SourceGuid);
            }
        }
    }

    TSet<UBlueprintGeneratedClass*> ClassesWithRestoredGuids;
    for (UClass* Class : {
             Copy->GeneratedClass.Get(),
             Copy->SkeletonGeneratedClass.Get()})
    {
        UBlueprintGeneratedClass* BlueprintClass =
            Cast<UBlueprintGeneratedClass>(Class);
        if (BlueprintClass == nullptr)
        {
            OutMessage =
                TEXT("UE Blueprint Class GUID storage is unavailable for transient preflight.");
            return false;
        }
        if (ClassesWithRestoredGuids.Contains(BlueprintClass))
        {
            continue;
        }
        ClassesWithRestoredGuids.Add(BlueprintClass);
        for (const FBPVariableDescription& Variable : Copy->NewVariables)
        {
            BlueprintClass->PropertyGuids.FindOrAdd(
                Variable.VarName) = Variable.VarGuid;
            if (BlueprintClass->FindBlueprintPropertyGuidFromName(
                    Variable.VarName)
                    != Variable.VarGuid
                || Copy->FindBlueprintPropertyGuidFromName(
                    Variable.VarName)
                    != Variable.VarGuid)
            {
                OutMessage = FString::Printf(
                    TEXT("UE failed to restore property GUID identity for %s."),
                    *Variable.VarName.ToString());
                return false;
            }
        }
    }
    return true;
}

void MarkTransientSandboxOwnedObjects(UObject* Root)
{
    if (Root == nullptr)
    {
        return;
    }
    const auto MarkTransient = [](UObject* Object)
    {
        if (Object != nullptr)
        {
            Object->ClearFlags(RF_Public | RF_Standalone);
            Object->SetFlags(RF_Transient | RF_Transactional);
        }
    };
    MarkTransient(Root);
    ForEachObjectWithOuter(
        Root,
        [&MarkTransient](UObject* Object)
        {
            MarkTransient(Object);
        },
        true);
}
}

bool ValidateBlueprintModificationClassState(
    UBlueprint* Blueprint,
    FString& OutMessage)
{
    if (Blueprint == nullptr)
    {
        OutMessage =
            TEXT("Blueprint is unavailable for modification class-state validation.");
        return false;
    }
    UBlueprintGeneratedClass* GeneratedClass =
        Cast<UBlueprintGeneratedClass>(Blueprint->GeneratedClass.Get());
    if (GeneratedClass == nullptr)
    {
        OutMessage =
            TEXT("Blueprint Generated Class is unavailable for modification.");
        return false;
    }
    if (GeneratedClass->GetDefaultObject(false) == nullptr)
    {
        OutMessage = FString::Printf(
            TEXT("Blueprint Generated Class has no default object: %s."),
            *GeneratedClass->GetPathName());
        return false;
    }

    TArray<UClass*> DerivedClasses;
    GetDerivedClasses(GeneratedClass, DerivedClasses);
    for (UClass* DerivedClass : DerivedClasses)
    {
        UBlueprintGeneratedClass* DerivedBPGC =
            Cast<UBlueprintGeneratedClass>(DerivedClass);
        if (DerivedBPGC == nullptr)
        {
            OutMessage = FString::Printf(
                TEXT("Loaded derived Class is not a BlueprintGeneratedClass: %s."),
                *GetPathNameSafe(DerivedClass));
            return false;
        }
        if (DerivedBPGC->GetDefaultObject(false) == nullptr)
        {
            OutMessage = FString::Printf(
                TEXT("Loaded derived BlueprintGeneratedClass has no default object: %s."),
                *DerivedBPGC->GetPathName());
            return false;
        }
    }
    return true;
}

TStrongObjectPtr<UBlueprint> MakeBlueprintSandbox(
    UBlueprint* Source,
    FString& OutMessage)
{
    if (Source == nullptr)
    {
        OutMessage =
            TEXT("Blueprint is unavailable for transient preflight.");
        return nullptr;
    }
    if (!ValidateBlueprintModificationClassState(Source, OutMessage))
    {
        return nullptr;
    }

    FBlueprintStableIdSnapshot OriginalSourceIds;
    if (!CaptureBlueprintStableIds(Source, OriginalSourceIds, OutMessage))
    {
        return nullptr;
    }
    UBlueprintGeneratedClass* OriginalSourceGeneratedClass =
        Cast<UBlueprintGeneratedClass>(Source->GeneratedClass.Get());
    UObject* OriginalSourceCDO =
        OriginalSourceGeneratedClass != nullptr
            ? OriginalSourceGeneratedClass->GetDefaultObject(false)
            : nullptr;

    UPackage* TransientPackage = GetTransientPackage();
    UBlueprint* Copy = nullptr;
    const FScopedSourceTimelineDisplayOrderRestoration
        RestoreSourceTimelineDisplayOrders(Source);
    {
        const FScopedBlueprintDebuggerSettingsSuppression
            SuppressDebuggerState(Source);
        const FBlueprintDuplicationScopeFlags DuplicationFlags(
            FBlueprintDuplicationScopeFlags::NoExtraCompilation
            | FBlueprintDuplicationScopeFlags::TheSameTimelineGuid
            | FBlueprintDuplicationScopeFlags::TheSameNodeGuid
            | FBlueprintDuplicationScopeFlags::
                ValidatePinsUsingSourceClass);
        Copy = Cast<UBlueprint>(StaticDuplicateObject(
            Source,
            TransientPackage,
            MakeUniqueObjectName(
                TransientPackage,
                Source->GetClass(),
                Source->GetFName())));
    }
    // UTimelineTemplate::Serialize populates an empty TrackDisplayOrder even
    // while saving the source into StaticDuplicateObject. Restore both sides
    // to the exact authored state so preflight has no source side effect and
    // does not validate a normalized model that live apply would not see.
    RestoreSourceTimelineDisplayOrders.RestoreSource();
    if (Copy == nullptr)
    {
        OutMessage =
            TEXT("UE failed to duplicate the Blueprint into a transient preflight model.");
        return nullptr;
    }
    RestoreAuthoredDuplicateTransientFields(Source, Copy);

    TStrongObjectPtr<UBlueprint> CopyGuard(Copy);
    // StaticDuplicateObject copies RF_Public/RF_Standalone from authored
    // assets. Strip those flags from the complete duplicated object graph
    // before any validation that can fail, otherwise an early return can
    // leave the rejected sandbox permanently rooted after CopyGuard releases.
    MarkTransientSandboxOwnedObjects(Copy);
    MarkTransientSandboxOwnedObjects(Copy->GeneratedClass.Get());
    if (Copy->SkeletonGeneratedClass != Copy->GeneratedClass)
    {
        MarkTransientSandboxOwnedObjects(
            Copy->SkeletonGeneratedClass.Get());
    }
    if (Copy->GeneratedClass != nullptr)
    {
        MarkTransientSandboxOwnedObjects(
            Copy->GeneratedClass->GetDefaultObject(false));
    }
    if (Copy->SkeletonGeneratedClass != nullptr
        && Copy->SkeletonGeneratedClass != Copy->GeneratedClass)
    {
        MarkTransientSandboxOwnedObjects(
            Copy->SkeletonGeneratedClass->GetDefaultObject(false));
    }
    if (!RestoreSandboxTimelineDisplayOrders(
            Copy,
            RestoreSourceTimelineDisplayOrders.GetSnapshots(),
            OutMessage))
    {
        return nullptr;
    }
    if (!RestoreTransientBlueprintIdentity(Source, Copy, OutMessage))
    {
        return nullptr;
    }

    UBlueprintGeneratedClass* SourceGeneratedClass =
        Cast<UBlueprintGeneratedClass>(Source->GeneratedClass.Get());
    UBlueprintGeneratedClass* PlanGeneratedClass =
        Cast<UBlueprintGeneratedClass>(Copy->GeneratedClass.Get());
    UObject* SourceCDO =
        SourceGeneratedClass != nullptr
            ? SourceGeneratedClass->GetDefaultObject(false)
            : nullptr;
    UObject* PlanCDO =
        PlanGeneratedClass != nullptr
            ? PlanGeneratedClass->GetDefaultObject(false)
            : nullptr;
    if (PlanGeneratedClass == nullptr
        || PlanGeneratedClass == SourceGeneratedClass
        || SourceGeneratedClass != OriginalSourceGeneratedClass
        || SourceGeneratedClass->GetDefaultObject(false)
            != OriginalSourceCDO
        || !PlanGeneratedClass->IsIn(TransientPackage)
        || PlanCDO == nullptr
        || PlanCDO == SourceCDO
        || PlanCDO->GetClass() != PlanGeneratedClass
        || !PlanCDO->IsIn(TransientPackage))
    {
        OutMessage =
            TEXT("UE failed to create an isolated Generated Class and default object for transient preflight.");
        return nullptr;
    }
    if (!ValidateBlueprintModificationClassState(Copy, OutMessage)
        || !ValidateTransientBlueprintIdentity(
            OriginalSourceIds,
            Source,
            Copy,
            OutMessage))
    {
        return nullptr;
    }

    return CopyGuard;
}
}
