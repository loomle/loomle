// Copyright 2026 Loomle contributors.

#include "SalStateTreeInterface.h"

#include "SalStateTreePalette.h"
#include "SalStateTreeSchema.h"
#include "../SalDiagnostics.h"
#include "../SalObjectBuilder.h"
#include "../SalRuntime.h"

#include "Blueprint/StateTreeConditionBlueprintBase.h"
#include "Blueprint/StateTreeConsiderationBlueprintBase.h"
#include "Blueprint/StateTreeEvaluatorBlueprintBase.h"
#include "Blueprint/StateTreeTaskBlueprintBase.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Editor.h"
#include "Subsystems/EditorAssetSubsystem.h"
#include "Editor/EditorEngine.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Misc/SecureHash.h"
#include "ScopedTransaction.h"
#include "PropertyBindingBindableStructDescriptor.h"
#include "PropertyBindingDataView.h"
#include "PropertyBindingPath.h"
#include "PropertyBindingTypes.h"
#include "StateTree.h"
#include "StateTreeCompilerLog.h"
#include "StateTreeConditionBase.h"
#include "StateTreeConsiderationBase.h"
#include "StateTreeEditingSubsystem.h"
#include "StateTreeEditorData.h"
#include "StateTreeEditorDataExtension.h"
#include "StateTreeEditorNode.h"
#include "StateTreeEditorPropertyBindings.h"
#include "StateTreeEditorSchema.h"
#include "StateTreeEvaluatorBase.h"
#include "StateTreeNodeBase.h"
#include "StateTreePropertyBindings.h"
#include "StateTreePropertyFunctionBase.h"
#include "StateTreeSchema.h"
#include "StateTreeState.h"
#include "StateTreeTaskBase.h"
#include "StateTreeViewModel.h"
#include "StructUtils/PropertyBag.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/StructOnScope.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/UnrealType.h"
#include "UObject/UObjectIterator.h"

namespace Loomle::Sal
{
namespace StateTreePatch
{
constexpr const TCHAR* InterfaceName = TEXT("state_tree");

FString GuidText(const FGuid& Guid)
{
    return Guid.ToString(EGuidFormats::DigitsWithHyphensLower);
}

bool ParseGuid(const FString& Text, FGuid& OutGuid)
{
    return FGuid::Parse(Text, OutGuid) && OutGuid.IsValid();
}

TSharedPtr<FJsonObject> ErrorResult(
    const FSalPatch& Patch,
    const FSalResolvedTarget& Target,
    const FString& Code,
    const FString& Message,
    const FString& Operation,
    const FString& Ref = FString(),
    const TSharedPtr<FJsonObject>& Planned = nullptr,
    const bool bApplied = false)
{
    FSalDiagnosticBuilder Diagnostic = FSalDiagnostics::Error(Code, Message)
        .Interface(InterfaceName)
        .Operation(Operation);
    if (!Ref.IsEmpty())
    {
        Diagnostic.Ref(Ref);
    }
    return MakeMutationResult(
        nullptr,
        {Diagnostic.Build()},
        Patch.bDryRun,
        false,
        bApplied,
        Target.AssetPath,
        TEXT("patch"),
        Planned);
}

TSharedPtr<FJsonObject> CurrentObject(const FSalResolvedTarget& Target)
{
    FSalQuery Query;
    Query.Alias = Target.Alias;
    Query.Operation = MakeShared<FJsonObject>();
    Query.Operation->SetStringField(TEXT("kind"), TEXT("target"));
    const TSharedPtr<FJsonObject> Result = FSalStateTreeInterface::Query(Query, Target);
    const TSharedPtr<FJsonObject>* Object = nullptr;
    if (Result.IsValid()
        && Result->TryGetObjectField(TEXT("object"), Object)
        && Object != nullptr)
    {
        return *Object;
    }
    return nullptr;
}

struct FIdentityMap
{
    TMap<FGuid, FGuid> States;
    TMap<FGuid, FGuid> Nodes;
    TMap<FGuid, FGuid> Transitions;
    TMap<FGuid, FGuid> ParameterContainers;
    // Native Property Binding struct ids include authored ids and derived ids
    // such as Node template and Required Event ids. Keeping their duplicate
    // mapping makes preflight evidence stable without inventing public ids.
    TMap<FGuid, FGuid> NativeStructs;
    TMap<FString, FString> Parameters;

    static FGuid ResolveFrom(const TMap<FGuid, FGuid>& Map, const FGuid& Requested)
    {
        const FGuid Mapped = Map.FindRef(Requested);
        return Mapped.IsValid() ? Mapped : Requested;
    }

    static FGuid RestoreFrom(const TMap<FGuid, FGuid>& Map, const FGuid& Current)
    {
        for (const TPair<FGuid, FGuid>& Pair : Map)
        {
            if (Pair.Value == Current)
            {
                return Pair.Key;
            }
        }
        return Current;
    }

    FGuid ResolveState(const FGuid& Requested) const { return ResolveFrom(States, Requested); }
    FGuid ResolveNode(const FGuid& Requested) const { return ResolveFrom(Nodes, Requested); }
    FGuid ResolveTransition(const FGuid& Requested) const { return ResolveFrom(Transitions, Requested); }
    FGuid RestoreState(const FGuid& Current) const { return RestoreFrom(States, Current); }
    FGuid RestoreNode(const FGuid& Current) const { return RestoreFrom(Nodes, Current); }
    FGuid RestoreTransition(const FGuid& Current) const { return RestoreFrom(Transitions, Current); }
    FGuid ResolveNativeStruct(const FGuid& Requested) const { return ResolveFrom(NativeStructs, Requested); }
    FGuid RestoreNativeStruct(const FGuid& Current) const { return RestoreFrom(NativeStructs, Current); }
    FGuid RestoreParameterContainer(const FGuid& Current) const
    {
        return RestoreFrom(ParameterContainers, Current);
    }

    FString ResolveParameter(const FString& Requested) const
    {
        if (const FString* Mapped = Parameters.Find(Requested))
        {
            return *Mapped;
        }
        return Requested;
    }

    FString RestoreParameter(const FString& Current) const
    {
        for (const TPair<FString, FString>& Pair : Parameters)
        {
            if (Pair.Value == Current)
            {
                return Pair.Key;
            }
        }
        FString ContainerText;
        FString PropertyText;
        FGuid Container;
        FGuid Property;
        if (Current.Split(TEXT("/"), &ContainerText, &PropertyText)
            && FGuid::Parse(ContainerText, Container)
            && FGuid::Parse(PropertyText, Property))
        {
            FGuid RestoredProperty = Property;
            for (const TPair<FString, FString>& Pair : Parameters)
            {
                FString SourceContainer;
                FString SourceProperty;
                FString CopyContainer;
                FString CopyProperty;
                FGuid CopyGuid;
                FGuid SourceGuid;
                if (Pair.Key.Split(TEXT("/"), &SourceContainer, &SourceProperty)
                    && Pair.Value.Split(TEXT("/"), &CopyContainer, &CopyProperty)
                    && FGuid::Parse(CopyProperty, CopyGuid)
                    && FGuid::Parse(SourceProperty, SourceGuid)
                    && CopyGuid == Property)
                {
                    RestoredProperty = SourceGuid;
                    break;
                }
            }
            return GuidText(RestoreFrom(ParameterContainers, Container))
                + TEXT("/")
                + GuidText(RestoredProperty);
        }
        return Current;
    }
};

enum class ENodeRole : uint8
{
    Evaluator,
    GlobalTask,
    EnterCondition,
    Task,
    SingleTask,
    Consideration,
    TransitionCondition,
    PropertyFunction,
};

struct FStateRef
{
    UStateTreeState* State = nullptr;
    UStateTreeState* Parent = nullptr;
    TArray<TObjectPtr<UStateTreeState>>* Siblings = nullptr;
    int32 Index = INDEX_NONE;
};

struct FNodeRef
{
    FStateTreeEditorNode* Node = nullptr;
    UStateTreeState* OwnerState = nullptr;
    FStateTreeTransition* OwnerTransition = nullptr;
    TArray<FStateTreeEditorNode>* Array = nullptr;
    ENodeRole Role = ENodeRole::Task;
    int32 Index = INDEX_NONE;
    int32 BindingIndex = INDEX_NONE;
    bool bPropertyFunction = false;
};

struct FTransitionRef
{
    FStateTreeTransition* Transition = nullptr;
    UStateTreeState* OwnerState = nullptr;
    int32 Index = INDEX_NONE;
};

struct FParameterRef
{
    FInstancedPropertyBag* Bag = nullptr;
    FPropertyBagPropertyDesc Desc;
    UStateTreeState* OwnerState = nullptr;
    FGuid ContainerId;
    int32 Index = INDEX_NONE;
    bool bRoot = false;
    bool bFixedLayout = false;
};

class FAuthoredIndex
{
public:
    FAuthoredIndex(UStateTree& InTree, UStateTreeEditorData& InData, const FIdentityMap& InIdentities)
        : Tree(InTree)
        , Data(InData)
        , Identities(InIdentities)
    {
        Build();
    }

    bool IsValid() const
    {
        return bValid;
    }

    const FString& GetError() const
    {
        return Error;
    }

    FStateRef* FindState(const FGuid& Requested)
    {
        return Unique(States, Identities.ResolveState(Requested));
    }

    FNodeRef* FindNode(const FGuid& Requested)
    {
        return Unique(Nodes, Identities.ResolveNode(Requested));
    }

    FTransitionRef* FindTransition(const FGuid& Requested)
    {
        return Unique(Transitions, Identities.ResolveTransition(Requested));
    }

    FParameterRef* FindParameter(const FString& Requested)
    {
        return Unique(Parameters, Identities.ResolveParameter(Requested));
    }

    bool IsAmbiguousState(const FGuid& Requested) const
    {
        return Count(States, Identities.ResolveState(Requested)) > 1;
    }

    bool IsAmbiguousNode(const FGuid& Requested) const
    {
        return Count(Nodes, Identities.ResolveNode(Requested)) > 1;
    }

    bool IsAmbiguousTransition(const FGuid& Requested) const
    {
        return Count(Transitions, Identities.ResolveTransition(Requested)) > 1;
    }

    FNodeRef* FindNodeByNativeStructId(const FGuid& StructId)
    {
        FNodeRef* Match = nullptr;
        for (auto It = Nodes.CreateIterator(); It; ++It)
        {
            FNodeRef& Candidate = It.Value();
            if (Candidate.Node != nullptr
                && (Candidate.Node->ID == StructId || Candidate.Node->GetNodeID() == StructId))
            {
                if (Match != nullptr && Match != &Candidate)
                {
                    return nullptr;
                }
                Match = &Candidate;
            }
        }
        return Match;
    }

    FParameterRef* FindParameterByNativePath(
        const FGuid& ContainerId,
        const FPropertyBindingPath& Path)
    {
        if (Path.GetSegments().IsEmpty())
        {
            return nullptr;
        }
        const FName PropertyName = Path.GetSegments()[0].GetName();
        FParameterRef* Match = nullptr;
        for (auto It = Parameters.CreateIterator(); It; ++It)
        {
            FParameterRef& Candidate = It.Value();
            if (Candidate.ContainerId != ContainerId || Candidate.Desc.Name != PropertyName)
            {
                continue;
            }
            if (Match != nullptr && Match != &Candidate)
            {
                return nullptr;
            }
            Match = &Candidate;
        }
        return Match;
    }

    void GetAllNodes(TArray<FNodeRef*>& OutNodes)
    {
        OutNodes.Reset();
        OutNodes.Reserve(Nodes.Num());
        for (auto It = Nodes.CreateIterator(); It; ++It)
        {
            OutNodes.Add(&It.Value());
        }
        OutNodes.Sort(
            [](const FNodeRef& Left, const FNodeRef& Right)
            {
                return GuidText(Left.Node->ID) < GuidText(Right.Node->ID);
            });
    }

    void GetAllStates(TArray<FStateRef*>& OutStates)
    {
        OutStates.Reset();
        OutStates.Reserve(States.Num());
        for (auto It = States.CreateIterator(); It; ++It)
        {
            OutStates.Add(&It.Value());
        }
    }

    void GetAllTransitions(TArray<FTransitionRef*>& OutTransitions)
    {
        OutTransitions.Reset();
        OutTransitions.Reserve(Transitions.Num());
        for (auto It = Transitions.CreateIterator(); It; ++It)
        {
            OutTransitions.Add(&It.Value());
        }
    }

    void GetAllParameters(TArray<FParameterRef*>& OutParameters)
    {
        OutParameters.Reset();
        OutParameters.Reserve(Parameters.Num());
        for (auto It = Parameters.CreateIterator(); It; ++It)
        {
            OutParameters.Add(&It.Value());
        }
    }

private:
    template <typename Key, typename Value>
    static Value* Unique(TMultiMap<Key, Value>& Map, const Key& KeyValue)
    {
        TArray<Value*> Matches;
        for (auto It = Map.CreateKeyIterator(KeyValue); It; ++It)
        {
            Matches.Add(&It.Value());
        }
        return Matches.Num() == 1 ? Matches[0] : nullptr;
    }

    template <typename Key, typename Value>
    static int32 Count(const TMultiMap<Key, Value>& Map, const Key& KeyValue)
    {
        int32 Result = 0;
        for (auto It = Map.CreateConstKeyIterator(KeyValue); It; ++It)
        {
            ++Result;
        }
        return Result;
    }

    void AddNode(
        FStateTreeEditorNode& Node,
        UStateTreeState* OwnerState,
        FStateTreeTransition* OwnerTransition,
        TArray<FStateTreeEditorNode>* Array,
        const ENodeRole Role,
        const int32 Index,
        const int32 BindingIndex = INDEX_NONE,
        const bool bPropertyFunction = false)
    {
        if (!bValid)
        {
            return;
        }
        if (!ConsumeObject(TEXT("Node")))
        {
            return;
        }
        if (!Node.ID.IsValid())
        {
            Fail(TEXT("StateTree contains a Node with an invalid authored id."));
            return;
        }
        if (SeenNodeIds.Contains(Node.ID))
        {
            Fail(FString::Printf(TEXT("StateTree Node id %s is duplicated."), *GuidText(Node.ID)));
            return;
        }
        SeenNodeIds.Add(Node.ID);
        Nodes.Add(Node.ID, {&Node, OwnerState, OwnerTransition, Array, Role, Index, BindingIndex, bPropertyFunction});
    }

    void AddNodeArray(
        TArray<FStateTreeEditorNode>& Array,
        UStateTreeState* OwnerState,
        FStateTreeTransition* OwnerTransition,
        const ENodeRole Role)
    {
        for (int32 Index = 0; Index < Array.Num(); ++Index)
        {
            AddNode(Array[Index], OwnerState, OwnerTransition, &Array, Role, Index);
        }
    }

    void AddParameters(
        FInstancedPropertyBag& Bag,
        const FGuid& ContainerId,
        UStateTreeState* OwnerState,
        const bool bRoot,
        const bool bFixedLayout)
    {
        if (!bValid)
        {
            return;
        }
        const UPropertyBag* BagStruct = Bag.GetPropertyBagStruct();
        if (!ContainerId.IsValid())
        {
            Fail(TEXT("StateTree contains a Parameter container with an invalid authored id."));
            return;
        }
        if (SeenParameterContainers.Contains(ContainerId))
        {
            Fail(FString::Printf(TEXT("StateTree Parameter container id %s is duplicated."), *GuidText(ContainerId)));
            return;
        }
        SeenParameterContainers.Add(ContainerId);
        if (BagStruct == nullptr)
        {
            return;
        }
        const TConstArrayView<FPropertyBagPropertyDesc> Descs = BagStruct->GetPropertyDescs();
        for (int32 Index = 0; Index < Descs.Num(); ++Index)
        {
            if (!ConsumeObject(TEXT("Parameter")))
            {
                return;
            }
            const FPropertyBagPropertyDesc& Desc = Descs[Index];
            if (!Desc.ID.IsValid())
            {
                Fail(TEXT("StateTree contains a Parameter with an invalid descriptor id."));
                return;
            }
            const FString Id = GuidText(ContainerId) + TEXT("/") + GuidText(Desc.ID);
            if (SeenParameterIds.Contains(Id))
            {
                Fail(FString::Printf(TEXT("StateTree Parameter identity %s is duplicated."), *Id));
                return;
            }
            SeenParameterIds.Add(Id);
            Parameters.Add(Id, {&Bag, Desc, OwnerState, ContainerId, Index, bRoot, bFixedLayout});
        }
    }

    void AddState(
        UStateTreeState& State,
        UStateTreeState* Parent,
        TArray<TObjectPtr<UStateTreeState>>& Siblings,
        const int32 StateIndex)
    {
        if (!bValid)
        {
            return;
        }
        if (!ConsumeObject(TEXT("State")))
        {
            return;
        }
        if (VisitedStates.Contains(&State))
        {
            Fail(TEXT("StateTree hierarchy contains a repeated or cyclic State reference."));
            return;
        }
        VisitedStates.Add(&State);
        if (State.GetTypedOuter<UStateTreeEditorData>() != &Data)
        {
            Fail(TEXT("StateTree hierarchy contains a State owned outside the bound EditorData."));
            return;
        }
        if (State.Parent != Parent)
        {
            Fail(TEXT("StateTree hierarchy contains an inconsistent native Parent pointer."));
            return;
        }
        if (!State.ID.IsValid())
        {
            Fail(TEXT("StateTree hierarchy contains a State with an invalid authored id."));
            return;
        }
        if (SeenStateIds.Contains(State.ID))
        {
            Fail(FString::Printf(TEXT("StateTree State id %s is duplicated."), *GuidText(State.ID)));
            return;
        }
        SeenStateIds.Add(State.ID);
        States.Add(State.ID, {&State, Parent, &Siblings, StateIndex});
        AddParameters(State.Parameters.Parameters, State.Parameters.ID, &State, false, State.Parameters.bFixedLayout);
        AddNodeArray(State.EnterConditions, &State, nullptr, ENodeRole::EnterCondition);
        AddNodeArray(State.Tasks, &State, nullptr, ENodeRole::Task);
        if (State.SingleTask.ID.IsValid()
            || State.SingleTask.Node.IsValid()
            || State.SingleTask.Instance.IsValid()
            || State.SingleTask.InstanceObject != nullptr
            || State.SingleTask.ExecutionRuntimeData.IsValid()
            || State.SingleTask.ExecutionRuntimeDataObject != nullptr)
        {
            AddNode(State.SingleTask, &State, nullptr, nullptr, ENodeRole::SingleTask, 0);
        }
        AddNodeArray(State.Considerations, &State, nullptr, ENodeRole::Consideration);
        for (int32 Index = 0; Index < State.Transitions.Num(); ++Index)
        {
            if (!ConsumeObject(TEXT("Transition")))
            {
                return;
            }
            FStateTreeTransition& Transition = State.Transitions[Index];
            if (Transition.ID.IsValid())
            {
                if (SeenTransitionIds.Contains(Transition.ID))
                {
                    Fail(FString::Printf(TEXT("StateTree Transition id %s is duplicated."), *GuidText(Transition.ID)));
                    return;
                }
                SeenTransitionIds.Add(Transition.ID);
                Transitions.Add(Transition.ID, {&Transition, &State, Index});
            }
            else
            {
                Fail(TEXT("StateTree contains a Transition with an invalid authored id."));
                return;
            }
            AddNodeArray(Transition.Conditions, &State, &Transition, ENodeRole::TransitionCondition);
        }
        for (int32 Index = 0; Index < State.Children.Num(); ++Index)
        {
            if (UStateTreeState* Child = State.Children[Index])
            {
                AddState(*Child, &State, State.Children, Index);
            }
            else
            {
                Fail(TEXT("StateTree hierarchy contains a null child State entry."));
                return;
            }
        }
    }

    void AddPropertyFunctions()
    {
        int32 BindingIndex = 0;
        for (FStateTreePropertyPathBinding& Binding : Data.EditorBindings.GetMutableBindings())
        {
            FStructView FunctionValue = Binding.GetMutablePropertyFunctionNode();
            FStateTreeEditorNode* Function = FunctionValue.GetPtr<FStateTreeEditorNode>();
            if (Function != nullptr)
            {
                AddNode(
                    *Function,
                    nullptr,
                    nullptr,
                    nullptr,
                    ENodeRole::PropertyFunction,
                    INDEX_NONE,
                    BindingIndex,
                    true);
            }
            ++BindingIndex;
        }
    }

    void Build()
    {
        AddParameters(
            const_cast<FInstancedPropertyBag&>(Data.GetRootParametersPropertyBag()),
            Data.GetRootParametersGuid(),
            nullptr,
            true,
            false);
        AddNodeArray(Data.Evaluators, nullptr, nullptr, ENodeRole::Evaluator);
        AddNodeArray(Data.GlobalTasks, nullptr, nullptr, ENodeRole::GlobalTask);
        for (int32 Index = 0; Index < Data.SubTrees.Num(); ++Index)
        {
            if (UStateTreeState* State = Data.SubTrees[Index])
            {
                AddState(*State, nullptr, Data.SubTrees, Index);
            }
            else
            {
                Fail(TEXT("StateTree hierarchy contains a null top-level State entry."));
                return;
            }
        }
        AddPropertyFunctions();
    }

    bool ConsumeObject(const TCHAR* Kind)
    {
        if (++VisitedObjectCount <= MaxAuthoredObjects)
        {
            return true;
        }
        Fail(FString::Printf(
            TEXT("StateTree Patch index exceeded the hard limit of %d authored objects while scanning %s."),
            MaxAuthoredObjects,
            Kind));
        return false;
    }

    void Fail(const FString& Message)
    {
        if (bValid)
        {
            bValid = false;
            Error = Message;
        }
    }

    static constexpr int32 MaxAuthoredObjects = 50000;
    UStateTree& Tree;
    UStateTreeEditorData& Data;
    const FIdentityMap& Identities;
    bool bValid = true;
    FString Error;
    int32 VisitedObjectCount = 0;
    TSet<const UStateTreeState*> VisitedStates;
    TSet<FGuid> SeenStateIds;
    TSet<FGuid> SeenNodeIds;
    TSet<FGuid> SeenTransitionIds;
    TSet<FGuid> SeenParameterContainers;
    TSet<FString> SeenParameterIds;
    TMultiMap<FGuid, FStateRef> States;
    TMultiMap<FGuid, FNodeRef> Nodes;
    TMultiMap<FGuid, FTransitionRef> Transitions;
    TMultiMap<FString, FParameterRef> Parameters;
};

bool AddIdentity(
    TMap<FGuid, FGuid>& Map,
    const TCHAR* Kind,
    const FGuid& Source,
    const FGuid& Copy,
    FString& OutError)
{
    if (!Source.IsValid() || !Copy.IsValid())
    {
        OutError = TEXT("StateTree preflight encountered an invalid authored identity.");
        return false;
    }
    if (const FGuid* Existing = Map.Find(Source))
    {
        if (*Existing != Copy)
        {
            OutError = FString::Printf(
                TEXT("StateTree preflight cannot map duplicate %s id %s uniquely."),
                Kind,
                *GuidText(Source));
            return false;
        }
        return true;
    }
    Map.Add(Source, Copy);
    return true;
}

bool MapParameterBag(
    const FInstancedPropertyBag& Source,
    const FGuid& SourceContainer,
    const FInstancedPropertyBag& Copy,
    const FGuid& CopyContainer,
    FIdentityMap& OutMap,
    FString& OutError)
{
    if (!AddIdentity(
            OutMap.ParameterContainers,
            TEXT("Parameter container"),
            SourceContainer,
            CopyContainer,
            OutError)
        || !AddIdentity(
            OutMap.NativeStructs,
            TEXT("Parameter container native struct"),
            SourceContainer,
            CopyContainer,
            OutError))
    {
        return false;
    }
    const UPropertyBag* SourceStruct = Source.GetPropertyBagStruct();
    const UPropertyBag* CopyStruct = Copy.GetPropertyBagStruct();
    const TConstArrayView<FPropertyBagPropertyDesc> SourceDescs = SourceStruct != nullptr
        ? SourceStruct->GetPropertyDescs()
        : TConstArrayView<FPropertyBagPropertyDesc>();
    const TConstArrayView<FPropertyBagPropertyDesc> CopyDescs = CopyStruct != nullptr
        ? CopyStruct->GetPropertyDescs()
        : TConstArrayView<FPropertyBagPropertyDesc>();
    if (SourceDescs.Num() != CopyDescs.Num())
    {
        OutError = TEXT("UE did not preserve a Parameter layout while duplicating StateTree preflight state.");
        return false;
    }
    for (int32 Index = 0; Index < SourceDescs.Num(); ++Index)
    {
        if (!SourceDescs[Index].ID.IsValid() || !CopyDescs[Index].ID.IsValid())
        {
            OutError = TEXT("StateTree preflight encountered an invalid Parameter descriptor identity.");
            return false;
        }
        const FString SourceId = GuidText(SourceContainer) + TEXT("/") + GuidText(SourceDescs[Index].ID);
        const FString CopyId = GuidText(CopyContainer) + TEXT("/") + GuidText(CopyDescs[Index].ID);
        if (OutMap.Parameters.Contains(SourceId))
        {
            OutError = FString::Printf(TEXT("StateTree Parameter identity %s is ambiguous."), *SourceId);
            return false;
        }
        OutMap.Parameters.Add(SourceId, CopyId);
    }
    return true;
}

bool MapNode(
    const FStateTreeEditorNode& Source,
    const FStateTreeEditorNode& Copy,
    FIdentityMap& OutMap,
    FString& OutError)
{
    if (Source.Node.GetScriptStruct() != Copy.Node.GetScriptStruct())
    {
        OutError = TEXT("UE did not preserve a Node type while duplicating StateTree preflight state.");
        return false;
    }
    return AddIdentity(OutMap.Nodes, TEXT("Node"), Source.ID, Copy.ID, OutError)
        && AddIdentity(
            OutMap.NativeStructs,
            TEXT("Node instance native struct"),
            Source.ID,
            Copy.ID,
            OutError)
        && AddIdentity(
            OutMap.NativeStructs,
            TEXT("Node template native struct"),
            Source.GetNodeID(),
            Copy.GetNodeID(),
            OutError);
}

bool MapNodeArray(
    const TArray<FStateTreeEditorNode>& Source,
    const TArray<FStateTreeEditorNode>& Copy,
    FIdentityMap& OutMap,
    FString& OutError)
{
    if (Source.Num() != Copy.Num())
    {
        OutError = TEXT("UE did not preserve a Node collection while duplicating StateTree preflight state.");
        return false;
    }
    for (int32 Index = 0; Index < Source.Num(); ++Index)
    {
        if (!MapNode(Source[Index], Copy[Index], OutMap, OutError))
        {
            return false;
        }
    }
    return true;
}

bool MapState(
    const UStateTreeState& Source,
    const UStateTreeState& Copy,
    FIdentityMap& OutMap,
    FString& OutError,
    TSet<const UStateTreeState*>& VisitedSource,
    TSet<const UStateTreeState*>& VisitedCopy,
    int32& VisitedCount)
{
    if (++VisitedCount > 50000
        || VisitedSource.Contains(&Source)
        || VisitedCopy.Contains(&Copy))
    {
        OutError = TEXT("StateTree preflight hierarchy is cyclic, repeated, or exceeds the authored-object hard limit.");
        return false;
    }
    VisitedSource.Add(&Source);
    VisitedCopy.Add(&Copy);
    if (!AddIdentity(OutMap.States, TEXT("State"), Source.ID, Copy.ID, OutError)
        || !AddIdentity(
            OutMap.NativeStructs,
            TEXT("State native struct"),
            Source.ID,
            Copy.ID,
            OutError)
        || !AddIdentity(
            OutMap.NativeStructs,
            TEXT("State Required Event native struct"),
            Source.GetEventID(),
            Copy.GetEventID(),
            OutError)
        || !MapParameterBag(
            Source.Parameters.Parameters,
            Source.Parameters.ID,
            Copy.Parameters.Parameters,
            Copy.Parameters.ID,
            OutMap,
            OutError)
        || !MapNodeArray(Source.EnterConditions, Copy.EnterConditions, OutMap, OutError)
        || !MapNodeArray(Source.Tasks, Copy.Tasks, OutMap, OutError)
        || !MapNodeArray(Source.Considerations, Copy.Considerations, OutMap, OutError))
    {
        return false;
    }
    if (Source.SingleTask.ID.IsValid() != Copy.SingleTask.ID.IsValid()
        || (Source.SingleTask.ID.IsValid() && !MapNode(Source.SingleTask, Copy.SingleTask, OutMap, OutError)))
    {
        OutError = TEXT("UE did not preserve a SingleTask while duplicating StateTree preflight state.");
        return false;
    }
    if (Source.Transitions.Num() != Copy.Transitions.Num())
    {
        OutError = TEXT("UE did not preserve a Transition collection while duplicating StateTree preflight state.");
        return false;
    }
    for (int32 Index = 0; Index < Source.Transitions.Num(); ++Index)
    {
        if (!AddIdentity(
                OutMap.Transitions,
                TEXT("Transition"),
                Source.Transitions[Index].ID,
                Copy.Transitions[Index].ID,
                OutError)
            || !AddIdentity(
                OutMap.NativeStructs,
                TEXT("Transition native struct"),
                Source.Transitions[Index].ID,
                Copy.Transitions[Index].ID,
                OutError)
            || !AddIdentity(
                OutMap.NativeStructs,
                TEXT("Transition Required Event native struct"),
                Source.Transitions[Index].GetEventID(),
                Copy.Transitions[Index].GetEventID(),
                OutError)
            || !MapNodeArray(
                Source.Transitions[Index].Conditions,
                Copy.Transitions[Index].Conditions,
                OutMap,
                OutError))
        {
            return false;
        }
    }
    if (Source.Children.Num() != Copy.Children.Num())
    {
        OutError = TEXT("UE did not preserve the State hierarchy while duplicating StateTree preflight state.");
        return false;
    }
    for (int32 Index = 0; Index < Source.Children.Num(); ++Index)
    {
        if (Source.Children[Index] == nullptr
            || Copy.Children[Index] == nullptr
            || !MapState(
                *Source.Children[Index],
                *Copy.Children[Index],
                OutMap,
                OutError,
                VisitedSource,
                VisitedCopy,
                VisitedCount))
        {
            if (OutError.IsEmpty())
            {
                OutError = TEXT("UE duplicated a null State hierarchy entry.");
            }
            return false;
        }
    }
    return true;
}

bool BuildIdentityMap(
    const UStateTree& SourceTree,
    const UStateTreeEditorData& Source,
    const UStateTree& CopyTree,
    const UStateTreeEditorData& Copy,
    FIdentityMap& OutMap,
    FString& OutError)
{
    OutMap = {};
    if (!MapParameterBag(
            Source.GetRootParametersPropertyBag(),
            Source.GetRootParametersGuid(),
            Copy.GetRootParametersPropertyBag(),
            Copy.GetRootParametersGuid(),
            OutMap,
            OutError)
        || !MapNodeArray(Source.Evaluators, Copy.Evaluators, OutMap, OutError)
        || !MapNodeArray(Source.GlobalTasks, Copy.GlobalTasks, OutMap, OutError))
    {
        return false;
    }
    if (Source.SubTrees.Num() != Copy.SubTrees.Num())
    {
        OutError = TEXT("UE did not preserve top-level States while duplicating StateTree preflight state.");
        return false;
    }
    TSet<const UStateTreeState*> VisitedSource;
    TSet<const UStateTreeState*> VisitedCopy;
    int32 VisitedCount = 0;
    for (int32 Index = 0; Index < Source.SubTrees.Num(); ++Index)
    {
        if (Source.SubTrees[Index] == nullptr
            || Copy.SubTrees[Index] == nullptr
            || !MapState(
                *Source.SubTrees[Index],
                *Copy.SubTrees[Index],
                OutMap,
                OutError,
                VisitedSource,
                VisitedCopy,
                VisitedCount))
        {
            if (OutError.IsEmpty())
            {
                OutError = TEXT("UE duplicated a null top-level State.");
            }
            return false;
        }
    }
    const TConstArrayView<FStateTreePropertyPathBinding> SourceBindings = Source.EditorBindings.GetBindings();
    const TConstArrayView<FStateTreePropertyPathBinding> CopyBindings = Copy.EditorBindings.GetBindings();
    if (SourceBindings.Num() != CopyBindings.Num())
    {
        OutError = TEXT("UE did not preserve Property Bindings while duplicating StateTree preflight state.");
        return false;
    }
    for (int32 Index = 0; Index < SourceBindings.Num(); ++Index)
    {
        const FStateTreeEditorNode* SourceFunction =
            SourceBindings[Index].GetPropertyFunctionNode().GetPtr<const FStateTreeEditorNode>();
        const FStateTreeEditorNode* CopyFunction =
            CopyBindings[Index].GetPropertyFunctionNode().GetPtr<const FStateTreeEditorNode>();
        if ((SourceFunction == nullptr) != (CopyFunction == nullptr)
            || (SourceFunction != nullptr && !MapNode(*SourceFunction, *CopyFunction, OutMap, OutError)))
        {
            OutError = TEXT("UE did not preserve a Property Function while duplicating StateTree preflight state.");
            return false;
        }
    }
    return true;
}

bool BuildCompileReplacementIdentityMap(
    const UStateTreeEditorData& Source,
    const UStateTreeEditorData& Replacement,
    FIdentityMap& OutMap,
    bool& bOutRootReset,
    FString& OutError)
{
    const auto MapSurvivingNodes = [](
        const TArray<FStateTreeEditorNode>& Before,
        const TArray<FStateTreeEditorNode>& After,
        FIdentityMap& Map,
        FString& Error) -> bool
    {
        TSet<int32> Used;
        for (const FStateTreeEditorNode& Current : After)
        {
            TArray<int32> Candidates;
            for (int32 Index = 0; Index < Before.Num(); ++Index)
            {
                if (Used.Contains(Index)
                    || Before[Index].Node.GetScriptStruct()
                        != Current.Node.GetScriptStruct())
                {
                    continue;
                }
                if (Before[Index].ID == Current.ID)
                {
                    Candidates = {Index};
                    break;
                }
                Candidates.Add(Index);
            }
            if (Candidates.Num() != 1)
            {
                Error = TEXT("Native StateTree validation left an ambiguous surviving Node after EditorData replacement.");
                return false;
            }
            Used.Add(Candidates[0]);
            if (!MapNode(Before[Candidates[0]], Current, Map, Error))
            {
                return false;
            }
        }
        return true;
    };
    const auto MapSurvivingParameters = [](
        const FInstancedPropertyBag& Before,
        const FGuid& BeforeContainer,
        const FInstancedPropertyBag& After,
        const FGuid& AfterContainer,
        FIdentityMap& Map,
        FString& Error) -> bool
    {
        if (!AddIdentity(
                Map.ParameterContainers,
                TEXT("Parameter container"),
                BeforeContainer,
                AfterContainer,
                Error)
            || !AddIdentity(
                Map.NativeStructs,
                TEXT("Parameter container native struct"),
                BeforeContainer,
                AfterContainer,
                Error))
        {
            return false;
        }
        const UPropertyBag* BeforeStruct = Before.GetPropertyBagStruct();
        const UPropertyBag* AfterStruct = After.GetPropertyBagStruct();
        const TConstArrayView<FPropertyBagPropertyDesc> BeforeDescs = BeforeStruct != nullptr
            ? BeforeStruct->GetPropertyDescs()
            : TConstArrayView<FPropertyBagPropertyDesc>();
        const TConstArrayView<FPropertyBagPropertyDesc> AfterDescs = AfterStruct != nullptr
            ? AfterStruct->GetPropertyDescs()
            : TConstArrayView<FPropertyBagPropertyDesc>();
        TSet<int32> Used;
        for (const FPropertyBagPropertyDesc& Current : AfterDescs)
        {
            TArray<int32> Candidates;
            for (int32 Index = 0; Index < BeforeDescs.Num(); ++Index)
            {
                if (Used.Contains(Index))
                {
                    continue;
                }
                if (BeforeDescs[Index].ID == Current.ID)
                {
                    Candidates = {Index};
                    break;
                }
                if (BeforeDescs[Index].Name == Current.Name
                    && BeforeDescs[Index].ValueType == Current.ValueType
                    && BeforeDescs[Index].ValueTypeObject == Current.ValueTypeObject
                    && BeforeDescs[Index].ContainerTypes == Current.ContainerTypes)
                {
                    Candidates.Add(Index);
                }
            }
            if (Candidates.IsEmpty())
            {
                continue;
            }
            if (Candidates.Num() != 1)
            {
                Error = TEXT("Native StateTree validation left an ambiguous surviving Parameter after EditorData replacement.");
                return false;
            }
            Used.Add(Candidates[0]);
            const FPropertyBagPropertyDesc& Previous = BeforeDescs[Candidates[0]];
            Map.Parameters.Add(
                GuidText(BeforeContainer) + TEXT("/") + GuidText(Previous.ID),
                GuidText(AfterContainer) + TEXT("/") + GuidText(Current.ID));
        }
        return true;
    };
    TFunction<bool(
        const UStateTreeState&,
        const UStateTreeState&,
        TSet<const UStateTreeState*>&,
        TSet<const UStateTreeState*>&,
        int32&)> MapSurvivingState;
    MapSurvivingState = [&](
        const UStateTreeState& Before,
        const UStateTreeState& After,
        TSet<const UStateTreeState*>& VisitedBefore,
        TSet<const UStateTreeState*>& VisitedAfter,
        int32& VisitedCount) -> bool
    {
        if (++VisitedCount > 50000
            || VisitedBefore.Contains(&Before)
            || VisitedAfter.Contains(&After))
        {
            OutError = TEXT("Native StateTree validation produced a cyclic or oversized replacement hierarchy.");
            return false;
        }
        VisitedBefore.Add(&Before);
        VisitedAfter.Add(&After);
        if (!AddIdentity(OutMap.States, TEXT("State"), Before.ID, After.ID, OutError)
            || !AddIdentity(OutMap.NativeStructs, TEXT("State native struct"), Before.ID, After.ID, OutError)
            || !AddIdentity(
                OutMap.NativeStructs,
                TEXT("State Required Event native struct"),
                Before.GetEventID(),
                After.GetEventID(),
                OutError)
            || !MapSurvivingParameters(
                Before.Parameters.Parameters,
                Before.Parameters.ID,
                After.Parameters.Parameters,
                After.Parameters.ID,
                OutMap,
                OutError)
            || !MapSurvivingNodes(Before.EnterConditions, After.EnterConditions, OutMap, OutError)
            || !MapSurvivingNodes(Before.Tasks, After.Tasks, OutMap, OutError)
            || !MapSurvivingNodes(Before.Considerations, After.Considerations, OutMap, OutError))
        {
            return false;
        }
        if (After.SingleTask.ID.IsValid())
        {
            if (!Before.SingleTask.ID.IsValid()
                || !MapNode(Before.SingleTask, After.SingleTask, OutMap, OutError))
            {
                OutError = TEXT("Native StateTree validation created or ambiguously replaced a SingleTask.");
                return false;
            }
        }
        if (Before.Transitions.Num() != After.Transitions.Num())
        {
            OutError = TEXT("Native StateTree validation changed Transition structure during EditorData replacement.");
            return false;
        }
        for (int32 Index = 0; Index < Before.Transitions.Num(); ++Index)
        {
            const FStateTreeTransition& Previous = Before.Transitions[Index];
            const FStateTreeTransition& Current = After.Transitions[Index];
            if (!AddIdentity(OutMap.Transitions, TEXT("Transition"), Previous.ID, Current.ID, OutError)
                || !AddIdentity(OutMap.NativeStructs, TEXT("Transition native struct"), Previous.ID, Current.ID, OutError)
                || !AddIdentity(
                    OutMap.NativeStructs,
                    TEXT("Transition Required Event native struct"),
                    Previous.GetEventID(),
                    Current.GetEventID(),
                    OutError)
                || !MapSurvivingNodes(Previous.Conditions, Current.Conditions, OutMap, OutError))
            {
                return false;
            }
        }
        if (Before.Children.Num() != After.Children.Num())
        {
            OutError = TEXT("Native StateTree validation changed the State hierarchy during EditorData replacement.");
            return false;
        }
        for (int32 Index = 0; Index < Before.Children.Num(); ++Index)
        {
            if (Before.Children[Index] == nullptr
                || After.Children[Index] == nullptr
                || !MapSurvivingState(
                    *Before.Children[Index],
                    *After.Children[Index],
                    VisitedBefore,
                    VisitedAfter,
                    VisitedCount))
            {
                return false;
            }
        }
        return true;
    };

    OutMap = {};
    bOutRootReset = false;
    if (!MapSurvivingParameters(
            Source.GetRootParametersPropertyBag(),
            Source.GetRootParametersGuid(),
            Replacement.GetRootParametersPropertyBag(),
            Replacement.GetRootParametersGuid(),
            OutMap,
            OutError)
        || !MapSurvivingNodes(Source.Evaluators, Replacement.Evaluators, OutMap, OutError)
        || !MapSurvivingNodes(Source.GlobalTasks, Replacement.GlobalTasks, OutMap, OutError))
    {
        return false;
    }

    const bool bNativeRootReset = Source.SubTrees.IsEmpty()
        && Replacement.SubTrees.Num() == 1;
    if (Source.SubTrees.Num() != Replacement.SubTrees.Num() && !bNativeRootReset)
    {
        OutError = TEXT("Native StateTree validation replaced EditorData with an unmappable root hierarchy.");
        return false;
    }
    bOutRootReset = bNativeRootReset;
    TSet<const UStateTreeState*> VisitedSource;
    TSet<const UStateTreeState*> VisitedReplacement;
    int32 VisitedCount = 0;
    for (int32 Index = 0; Index < Source.SubTrees.Num(); ++Index)
    {
        if (Source.SubTrees[Index] == nullptr
            || Replacement.SubTrees[Index] == nullptr
            || !MapSurvivingState(
                *Source.SubTrees[Index],
                *Replacement.SubTrees[Index],
                VisitedSource,
                VisitedReplacement,
                VisitedCount))
        {
            if (OutError.IsEmpty())
            {
                OutError = TEXT("Native StateTree validation replaced EditorData with an unmappable State hierarchy.");
            }
            return false;
        }
    }

    // Property Function nodes live inside Binding records. Validation may
    // intentionally remove invalid records, so map each surviving function by
    // its already-mapped target instead of requiring identical Binding arrays.
    const TConstArrayView<FStateTreePropertyPathBinding> ReplacementBindings =
        Replacement.EditorBindings.GetBindings();
    for (const FStateTreePropertyPathBinding& SourceBinding : Source.EditorBindings.GetBindings())
    {
        const FStateTreeEditorNode* SourceFunction =
            SourceBinding.GetPropertyFunctionNode().GetPtr<const FStateTreeEditorNode>();
        if (SourceFunction == nullptr)
        {
            continue;
        }
        FPropertyBindingPath MappedTarget = SourceBinding.GetTargetPath();
        MappedTarget.SetStructID(OutMap.ResolveNativeStruct(MappedTarget.GetStructID()));
        const FStateTreeEditorNode* ReplacementFunction = nullptr;
        int32 Matches = 0;
        for (const FStateTreePropertyPathBinding& Candidate : ReplacementBindings)
        {
            if (Candidate.GetTargetPath() != MappedTarget)
            {
                continue;
            }
            const FStateTreeEditorNode* CandidateFunction =
                Candidate.GetPropertyFunctionNode().GetPtr<const FStateTreeEditorNode>();
            if (CandidateFunction != nullptr
                && CandidateFunction->Node.GetScriptStruct()
                    == SourceFunction->Node.GetScriptStruct())
            {
                ReplacementFunction = CandidateFunction;
                ++Matches;
            }
        }
        if (Matches > 1)
        {
            OutError = TEXT("Native StateTree validation replaced EditorData with ambiguous Property Function ownership.");
            return false;
        }
        if (ReplacementFunction != nullptr
            && !MapNode(*SourceFunction, *ReplacementFunction, OutMap, OutError))
        {
            return false;
        }
    }
    return true;
}

template<typename KeyType>
void ComposeIdentityTable(
    TMap<KeyType, KeyType>& Base,
    const TMap<KeyType, KeyType>& Next)
{
    TSet<KeyType> Consumed;
    for (TPair<KeyType, KeyType>& Pair : Base)
    {
        if (const KeyType* Mapped = Next.Find(Pair.Value))
        {
            Consumed.Add(Pair.Value);
            Pair.Value = *Mapped;
        }
    }
    for (const TPair<KeyType, KeyType>& Pair : Next)
    {
        if (!Consumed.Contains(Pair.Key) && !Base.Contains(Pair.Key))
        {
            Base.Add(Pair.Key, Pair.Value);
        }
    }
}

void ComposeIdentityMap(FIdentityMap& Base, const FIdentityMap& Next)
{
    ComposeIdentityTable(Base.States, Next.States);
    ComposeIdentityTable(Base.Nodes, Next.Nodes);
    ComposeIdentityTable(Base.Transitions, Next.Transitions);
    ComposeIdentityTable(Base.ParameterContainers, Next.ParameterContainers);
    ComposeIdentityTable(Base.NativeStructs, Next.NativeStructs);
    ComposeIdentityTable(Base.Parameters, Next.Parameters);
}

TStrongObjectPtr<UStateTree> DuplicateForPreflight(
    UStateTree& Source,
    FIdentityMap& OutMap,
    FString& OutError)
{
    const FName Name = MakeUniqueObjectName(
        GetTransientPackage(),
        Source.GetClass(),
        FName(*(Source.GetName() + TEXT("_SALDryRun"))));
    TMap<UObject*, UObject*> CreatedObjects;
    FObjectDuplicationParameters Parameters(&Source, GetTransientPackage());
    Parameters.DestName = Name;
    Parameters.FlagMask &= ~(RF_Public | RF_Standalone);
    Parameters.ApplyFlags |= RF_Transient;
    Parameters.bSkipPostLoad = true;
    Parameters.CreatedObjects = &CreatedObjects;
    UStateTree* Copy = Cast<UStateTree>(StaticDuplicateObjectEx(Parameters));
    TStrongObjectPtr<UStateTree> CopyOwner(Copy);
    for (const TPair<UObject*, UObject*>& Pair : CreatedObjects)
    {
        if (Pair.Value != nullptr)
        {
            // The source graph is already fully loaded. This transient
            // snapshot intentionally keeps PostDuplicate (including StateTree
            // ID remapping) but must never run the normal StateTree PostLoad,
            // which compiles and repairs authored data before SAL can plan it.
            Pair.Value->ClearFlags(
                RF_NeedPostLoad
                | RF_NeedPostLoadSubobjects
                | RF_Public
                | RF_Standalone);
            Pair.Value->SetFlags(RF_Transient);
        }
    }
    UStateTreeEditorData* SourceData = Cast<UStateTreeEditorData>(Source.EditorData);
    UStateTreeEditorData* CopyData = Copy != nullptr
        ? Cast<UStateTreeEditorData>(Copy->EditorData)
        : nullptr;
    if (Copy == nullptr || SourceData == nullptr || CopyData == nullptr || CopyData == SourceData)
    {
        OutError = TEXT("UE could not create an isolated transient StateTree preflight copy.");
        return {};
    }
    Copy->SetFlags(RF_Transient | RF_Transactional);
    CopyData->SetFlags(RF_Transient | RF_Transactional);
    if (!BuildIdentityMap(Source, *SourceData, *Copy, *CopyData, OutMap, OutError))
    {
        return {};
    }
    return CopyOwner;
}

struct FConstructorDefinition
{
    FString Alias;
    FString Callee;
    FString PaletteId;
    TSharedPtr<FJsonObject> Args;
    FGuid PlannedId;
    FGuid PlannedParameterContainerId;
    bool bConsumed = false;
};

struct FCreatedRef
{
    FString Kind;
    FString Id;
};

struct FPlanIdentities
{
    TMap<FString, FGuid> ByAlias;
    TMap<FString, FGuid> ParameterContainerByAlias;
};

bool ReadObject(const TSharedPtr<FJsonValue>& Value, TSharedPtr<FJsonObject>& Out)
{
    const TSharedPtr<FJsonObject>* Object = nullptr;
    if (!Value.IsValid() || !Value->TryGetObject(Object) || Object == nullptr || !(*Object).IsValid())
    {
        Out.Reset();
        return false;
    }
    Out = *Object;
    return true;
}

bool ReadRefObject(
    const TSharedPtr<FJsonObject>& Owner,
    FString& OutKind,
    FString& OutId,
    FString& OutLocal)
{
    OutKind.Reset();
    OutId.Reset();
    OutLocal.Reset();
    if (!Owner.IsValid() || !Owner->TryGetStringField(TEXT("kind"), OutKind))
    {
        return false;
    }
    if (OutKind == TEXT("local"))
    {
        return Owner->TryGetStringField(TEXT("name"), OutLocal) && !OutLocal.IsEmpty();
    }
    return Owner->TryGetStringField(TEXT("id"), OutId) && !OutId.IsEmpty();
}

bool ReadMember(
    const TSharedPtr<FJsonObject>& Ref,
    TSharedPtr<FJsonObject>& OutOwner,
    const TArray<TSharedPtr<FJsonValue>>*& OutPath)
{
    FString Kind;
    const TSharedPtr<FJsonObject>* Owner = nullptr;
    OutOwner.Reset();
    OutPath = nullptr;
    if (!Ref.IsValid()
        || !Ref->TryGetStringField(TEXT("kind"), Kind)
        || Kind != TEXT("member")
        || !Ref->TryGetObjectField(TEXT("object"), Owner)
        || Owner == nullptr
        || !Ref->TryGetArrayField(TEXT("path"), OutPath)
        || OutPath == nullptr)
    {
        return false;
    }
    OutOwner = *Owner;
    return true;
}

bool ParseParameterId(const FString& Text, FGuid& OutContainer, FGuid& OutProperty)
{
    FString ContainerText;
    FString PropertyText;
    return Text.Split(TEXT("/"), &ContainerText, &PropertyText)
        && !PropertyText.Contains(TEXT("/"))
        && ParseGuid(ContainerText, OutContainer)
        && ParseGuid(PropertyText, OutProperty);
}

TSharedPtr<FJsonObject> StableRef(const FString& Kind, const FString& Id)
{
    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetStringField(TEXT("kind"), Kind);
    Result->SetStringField(TEXT("id"), Id);
    return Result;
}

class FPatchContext
{
public:
    FPatchContext(
        UStateTree& InTree,
        UStateTreeEditorData& InData,
        const FIdentityMap& InIdentities,
        const FPlanIdentities& InPlanIdentities,
        const FString& InTargetAlias,
        const bool bInInitiallyStale,
        const bool bInPreflight)
        : Tree(InTree)
        , Data(InData)
        , Identities(InIdentities)
        , PlanIdentities(InPlanIdentities)
        , TargetAlias(InTargetAlias)
        , bInitiallyStale(bInInitiallyStale)
        , bPreflight(bInPreflight)
    {
    }

    bool Fail(
        const FString& Code,
        const FString& Message,
        const FString& Operation,
        const FString& Ref = FString())
    {
        FSalDiagnosticBuilder Diagnostic = FSalDiagnostics::Error(Code, Message)
            .Interface(InterfaceName)
            .Operation(Operation);
        if (!Ref.IsEmpty())
        {
            Diagnostic.Ref(Ref);
        }
        Diagnostics.Add(Diagnostic.Build());
        return false;
    }

    FAuthoredIndex MakeIndex()
    {
        return FAuthoredIndex(Tree, Data, Identities);
    }

    FString PublicId(const FString& Kind, const FString& Current) const
    {
        FGuid Guid;
        if (Kind == TEXT("state") && ParseGuid(Current, Guid))
        {
            return GuidText(Identities.RestoreState(Guid));
        }
        if (Kind == TEXT("node") && ParseGuid(Current, Guid))
        {
            return GuidText(Identities.RestoreNode(Guid));
        }
        if (Kind == TEXT("transition") && ParseGuid(Current, Guid))
        {
            return GuidText(Identities.RestoreTransition(Guid));
        }
        if (Kind == TEXT("parameter"))
        {
            return Identities.RestoreParameter(Current);
        }
        return Current;
    }

    FString PublicRef(const FString& Kind, const FString& Current) const
    {
        return Kind + TEXT("@") + PublicId(Kind, Current);
    }

    bool AddDefinition(const TSharedPtr<FJsonObject>& Statement)
    {
        const TSharedPtr<FJsonObject>* Target = nullptr;
        const TSharedPtr<FJsonObject>* Call = nullptr;
        const TSharedPtr<FJsonObject>* Args = nullptr;
        FString TargetKind;
        FString Alias;
        FString ValueKind;
        FString Callee;
        FString PaletteId;
        if (!Statement.IsValid()
            || !Statement->TryGetObjectField(TEXT("target"), Target)
            || Target == nullptr
            || !(*Target)->TryGetStringField(TEXT("kind"), TargetKind)
            || TargetKind != TEXT("local")
            || !(*Target)->TryGetStringField(TEXT("name"), Alias)
            || !Statement->TryGetObjectField(TEXT("value"), Call)
            || Call == nullptr
            || !(*Call)->TryGetStringField(TEXT("kind"), ValueKind)
            || ValueKind != TEXT("call")
            || !(*Call)->TryGetStringField(TEXT("callee"), Callee)
            || !(*Call)->TryGetObjectField(TEXT("args"), Args)
            || Args == nullptr
            || !(*Args)->TryGetStringField(TEXT("palette"), PaletteId)
            || PaletteId.IsEmpty())
        {
            return Fail(
                TEXT("validation.creation_invalid"),
                TEXT("StateTree creation bindings must be local Palette-backed constructor calls."),
                TEXT("patch"));
        }
        if (!(Callee == TEXT("state")
            || Callee == TEXT("node")
            || Callee == TEXT("transition")
            || Callee == TEXT("parameter")))
        {
            return Fail(
                TEXT("capability.unsupported_constructor"),
                FString::Printf(TEXT("Constructor %s is not owned by the StateTree interface."), *Callee),
                TEXT("patch"),
                Alias);
        }
        if (Definitions.Contains(Alias))
        {
            return Fail(
                TEXT("language.duplicate_binding"),
                TEXT("StateTree Patch contains a duplicate creation alias."),
                TEXT("patch"),
                Alias);
        }
        const FGuid* PlannedId = PlanIdentities.ByAlias.Find(Alias);
        if (PlannedId == nullptr || !PlannedId->IsValid())
        {
            return Fail(
                TEXT("validation.patch_state_invalid"),
                TEXT("StateTree Patch did not allocate a shared preflight/apply identity for a constructor."),
                TEXT("patch"),
                Alias);
        }
        const FGuid ParameterContainerId = PlanIdentities.ParameterContainerByAlias.FindRef(Alias);
        Definitions.Add(Alias, {Alias, Callee, PaletteId, *Args, *PlannedId, ParameterContainerId, false});
        return true;
    }

    bool ResolveRef(
        const TSharedPtr<FJsonObject>& Ref,
        FString& OutKind,
        FString& OutId,
        FString& OutError) const
    {
        FString Local;
        if (!ReadRefObject(Ref, OutKind, OutId, Local))
        {
            OutError = TEXT("Expected one exact stable or created local reference.");
            return false;
        }
        if (OutKind == TEXT("local"))
        {
            if (Local == TargetAlias)
            {
                OutKind = TEXT("asset");
                OutId = Tree.GetPathName();
                return true;
            }
            const FCreatedRef* Created = CreatedRefs.Find(Local);
            if (Created == nullptr)
            {
                OutError = FString::Printf(TEXT("Local alias %s has not been materialized yet."), *Local);
                return false;
            }
            OutKind = Created->Kind;
            OutId = Created->Id;
            return true;
        }
        if (OutKind == TEXT("parameter"))
        {
            OutId = Identities.ResolveParameter(OutId);
            return true;
        }
        FGuid Guid;
        if (OutKind == TEXT("state") && ParseGuid(OutId, Guid))
        {
            OutId = GuidText(Identities.ResolveState(Guid));
        }
        else if (OutKind == TEXT("node") && ParseGuid(OutId, Guid))
        {
            OutId = GuidText(Identities.ResolveNode(Guid));
        }
        else if (OutKind == TEXT("transition") && ParseGuid(OutId, Guid))
        {
            OutId = GuidText(Identities.ResolveTransition(Guid));
        }
        // Schema Context object ids are deterministic descriptors, not
        // duplicated authored identities. Never remap object@id through a
        // State/Node/Transition map even when GUID values happen to collide.
        return true;
    }

    TSharedPtr<FJsonObject> RebindRef(
        const TSharedPtr<FJsonObject>& Ref,
        FString& OutError) const
    {
        if (!Ref.IsValid())
        {
            OutError = TEXT("Reference is unavailable.");
            return nullptr;
        }
        FString Kind;
        Ref->TryGetStringField(TEXT("kind"), Kind);
        if (Kind != TEXT("member"))
        {
            FString StableKind;
            FString StableId;
            if (!ResolveRef(Ref, StableKind, StableId, OutError))
            {
                return nullptr;
            }
            if (StableKind == TEXT("asset"))
            {
                TSharedPtr<FJsonObject> Local = MakeShared<FJsonObject>();
                Local->SetStringField(TEXT("kind"), TEXT("local"));
                Local->SetStringField(TEXT("name"), TargetAlias);
                return Local;
            }
            return StableRef(StableKind, StableId);
        }
        TSharedPtr<FJsonObject> Owner;
        const TArray<TSharedPtr<FJsonValue>>* Path = nullptr;
        if (!ReadMember(Ref, Owner, Path))
        {
            OutError = TEXT("Member reference is malformed.");
            return nullptr;
        }
        TSharedPtr<FJsonObject> ReboundOwner = RebindRef(Owner, OutError);
        if (!ReboundOwner.IsValid())
        {
            return nullptr;
        }
        TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
        Result->SetStringField(TEXT("kind"), TEXT("member"));
        Result->SetObjectField(TEXT("object"), ReboundOwner);
        Result->SetArrayField(TEXT("path"), *Path);
        return Result;
    }

    bool ResolveMemberEndpoint(
        const TSharedPtr<FJsonObject>& Ref,
        StateTreeSchema::FResolvedMember& OutMember,
        FString& OutError,
        const StateTreeSchema::EMemberPurpose Purpose) const
    {
        const TSharedPtr<FJsonObject> Rebound = RebindRef(Ref, OutError);
        if (!Rebound.IsValid())
        {
            return false;
        }
        FString Kind;
        Rebound->TryGetStringField(TEXT("kind"), Kind);
        if (Kind == TEXT("member"))
        {
            return StateTreeSchema::ResolveMemberReference(
                Tree,
                Data,
                Rebound,
                OutMember,
                OutError,
                Purpose);
        }
        FString OwnerKind;
        FString OwnerId;
        FString Local;
        if (!ReadRefObject(Rebound, OwnerKind, OwnerId, Local))
        {
            OutError = TEXT("Binding endpoint is not an exact stable object or member.");
            return false;
        }
        return StateTreeSchema::ResolveMember(
            Tree,
            Data,
            OwnerKind,
            OwnerId,
            {},
            OutMember,
            OutError,
            Purpose);
    }

    UStateTree& Tree;
    UStateTreeEditorData& Data;
    const FIdentityMap& Identities;
    const FPlanIdentities& PlanIdentities;
    FString TargetAlias;
    bool bInitiallyStale = false;
    bool bPreflight = false;
    TMap<FString, FConstructorDefinition> Definitions;
    TMap<FString, FCreatedRef> CreatedRefs;
    TArray<TSharedPtr<FJsonObject>> Diagnostics;
    TArray<TSharedPtr<FJsonValue>> PlannedOperations;
    TArray<TSharedPtr<FJsonValue>> PlannedEffects;
    TArray<FString> ResultComments;
    // Ordinary authored edits may deliberately leave Required Event or
    // Delegate edges dormant. Native Validate removes some Event edges; these
    // exact entries are classified without authorizing unrelated repairs.
    TSet<FString> ExpectedValidationBindingRepairs;
    TSharedPtr<FJsonObject> ResolvedRefs = MakeShared<FJsonObject>();
    int32 ChangedOperations = 0;
    bool bCompiled = false;
    bool bCompileSucceeded = false;
    bool bSaveRequested = false;
    bool bSaved = false;
};

bool SeedPlanIdentities(
    const FSalPatch& Patch,
    FPlanIdentities& OutPlan,
    FString& OutError)
{
    OutPlan = {};
    for (const TSharedPtr<FJsonValue>& Value : Patch.Statements)
    {
        TSharedPtr<FJsonObject> Statement;
        if (!ReadObject(Value, Statement))
        {
            OutError = TEXT("StateTree Patch statement is unavailable.");
            return false;
        }
        FString Kind;
        Statement->TryGetStringField(TEXT("kind"), Kind);
        if (!Kind.IsEmpty())
        {
            continue;
        }
        const TSharedPtr<FJsonObject>* Target = nullptr;
        FString TargetKind;
        FString Alias;
        if (!Statement->TryGetObjectField(TEXT("target"), Target)
            || Target == nullptr
            || !(*Target)->TryGetStringField(TEXT("kind"), TargetKind)
            || TargetKind != TEXT("local")
            || !(*Target)->TryGetStringField(TEXT("name"), Alias)
            || Alias.IsEmpty())
        {
            OutError = TEXT("StateTree Patch only accepts local constructor declarations as binding statements.");
            return false;
        }
        OutPlan.ByAlias.Add(Alias, FGuid::NewGuid());
        OutPlan.ParameterContainerByAlias.Add(Alias, FGuid::NewGuid());
    }
    return true;
}

TSharedPtr<FJsonObject> BuildPlan(const FPatchContext& Context)
{
    TSharedPtr<FJsonObject> Plan = MakeShared<FJsonObject>();
    Plan->SetArrayField(TEXT("operations"), Context.PlannedOperations);
    Plan->SetArrayField(TEXT("effects"), Context.PlannedEffects);
    return Plan;
}

TSharedPtr<FJsonObject> MemberRef(
    const TSharedPtr<FJsonObject>& Owner,
    const TArray<FString>& Path)
{
    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetStringField(TEXT("kind"), TEXT("member"));
    Result->SetObjectField(TEXT("object"), Owner);
    TArray<TSharedPtr<FJsonValue>> Segments;
    for (const FString& Segment : Path)
    {
        Segments.Add(MakeShared<FJsonValueString>(Segment));
    }
    Result->SetArrayField(TEXT("path"), Segments);
    return Result;
}

TSharedPtr<FJsonObject> LocalRef(const FString& Alias)
{
    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetStringField(TEXT("kind"), TEXT("local"));
    Result->SetStringField(TEXT("name"), Alias);
    return Result;
}

const TCHAR* NodeRoleMember(const ENodeRole Role)
{
    switch (Role)
    {
    case ENodeRole::Evaluator: return TEXT("Evaluators");
    case ENodeRole::GlobalTask: return TEXT("GlobalTasks");
    case ENodeRole::EnterCondition: return TEXT("EnterConditions");
    case ENodeRole::Task: return TEXT("Tasks");
    case ENodeRole::SingleTask: return TEXT("SingleTask");
    case ENodeRole::Consideration: return TEXT("Considerations");
    case ENodeRole::TransitionCondition: return TEXT("Conditions");
    case ENodeRole::PropertyFunction: return TEXT("PropertyFunction");
    }
    return TEXT("Node");
}

TSharedPtr<FJsonObject> DestinationForAnchor(
    FPatchContext& Context,
    const TSharedPtr<FJsonObject>& Anchor,
    FString& OutError)
{
    FString Kind;
    FString Id;
    if (!Context.ResolveRef(Anchor, Kind, Id, OutError))
    {
        return nullptr;
    }
    FAuthoredIndex Index = Context.MakeIndex();
    if (!Index.IsValid())
    {
        OutError = Index.GetError();
        return nullptr;
    }
    if (Kind == TEXT("state"))
    {
        FGuid Guid;
        FStateRef* State = ParseGuid(Id, Guid) ? Index.FindState(Guid) : nullptr;
        if (State == nullptr)
        {
            OutError = TEXT("State placement anchor is missing or ambiguous.");
            return nullptr;
        }
        return State->Parent != nullptr
            ? MemberRef(StableRef(TEXT("state"), GuidText(State->Parent->ID)), {TEXT("Children")})
            : MemberRef(LocalRef(Context.TargetAlias), {TEXT("SubTrees")});
    }
    if (Kind == TEXT("node"))
    {
        FGuid Guid;
        FNodeRef* Node = ParseGuid(Id, Guid) ? Index.FindNode(Guid) : nullptr;
        if (Node == nullptr || Node->bPropertyFunction || Node->Role == ENodeRole::SingleTask)
        {
            OutError = TEXT("Node placement anchor is missing, ambiguous, or not in an ordered destination.");
            return nullptr;
        }
        if (Node->OwnerTransition != nullptr)
        {
            return MemberRef(
                StableRef(TEXT("transition"), GuidText(Node->OwnerTransition->ID)),
                {NodeRoleMember(Node->Role)});
        }
        if (Node->OwnerState != nullptr)
        {
            return MemberRef(
                StableRef(TEXT("state"), GuidText(Node->OwnerState->ID)),
                {NodeRoleMember(Node->Role)});
        }
        return MemberRef(LocalRef(Context.TargetAlias), {NodeRoleMember(Node->Role)});
    }
    if (Kind == TEXT("transition"))
    {
        FGuid Guid;
        FTransitionRef* Transition = ParseGuid(Id, Guid) ? Index.FindTransition(Guid) : nullptr;
        if (Transition == nullptr || Transition->OwnerState == nullptr)
        {
            OutError = TEXT("Transition placement anchor is missing or ambiguous.");
            return nullptr;
        }
        return MemberRef(
            StableRef(TEXT("state"), GuidText(Transition->OwnerState->ID)),
            {TEXT("Transitions")});
    }
    if (Kind == TEXT("parameter"))
    {
        FParameterRef* Parameter = Index.FindParameter(Id);
        if (Parameter == nullptr)
        {
            OutError = TEXT("Parameter placement anchor is missing or ambiguous.");
            return nullptr;
        }
        return Parameter->bRoot
            ? MemberRef(LocalRef(Context.TargetAlias), {TEXT("RootParameters")})
            : MemberRef(
                StableRef(TEXT("state"), GuidText(Parameter->OwnerState->ID)),
                {TEXT("Parameters")});
    }
    OutError = TEXT("Placement anchor kind is not supported by StateTree.");
    return nullptr;
}

bool ReadPlacement(
    FPatchContext& Context,
    const TSharedPtr<FJsonObject>& Operation,
    TSharedPtr<FJsonObject>& OutDestination,
    TSharedPtr<FJsonObject>& OutAnchor,
    bool& bOutBefore,
    bool& bOutAfter,
    FString& OutError)
{
    OutDestination.Reset();
    OutAnchor.Reset();
    bOutBefore = false;
    bOutAfter = false;
    const TSharedPtr<FJsonObject>* Ref = nullptr;
    if (Operation->TryGetObjectField(TEXT("to"), Ref) && Ref != nullptr)
    {
        OutDestination = Context.RebindRef(*Ref, OutError);
        return OutDestination.IsValid();
    }
    if (Operation->TryGetObjectField(TEXT("before"), Ref) && Ref != nullptr)
    {
        bOutBefore = true;
        OutAnchor = Context.RebindRef(*Ref, OutError);
    }
    else if (Operation->TryGetObjectField(TEXT("after"), Ref) && Ref != nullptr)
    {
        bOutAfter = true;
        OutAnchor = Context.RebindRef(*Ref, OutError);
    }
    else
    {
        OutError = TEXT("StateTree add/move requires one exact to, before, or after placement.");
        return false;
    }
    if (!OutAnchor.IsValid())
    {
        return false;
    }
    OutDestination = DestinationForAnchor(Context, OutAnchor, OutError);
    return OutDestination.IsValid();
}

bool ConstructorMatches(
    const FString& Callee,
    const StateTreePalette::EConstructorKind Kind)
{
    using StateTreePalette::EConstructorKind;
    return (Callee == TEXT("state") && Kind == EConstructorKind::State)
        || (Callee == TEXT("node") && Kind == EConstructorKind::Node)
        || (Callee == TEXT("transition") && Kind == EConstructorKind::Transition)
        || (Callee == TEXT("parameter") && Kind == EConstructorKind::Parameter);
}

struct FParameterType
{
    EPropertyBagPropertyType ValueType = EPropertyBagPropertyType::None;
    FPropertyBagContainerTypes Containers;
    UObject* ValueTypeObject = nullptr;
};

bool SplitNativeTypeCall(
    const FString& Text,
    FString& OutName,
    TArray<FString>& OutArguments,
    FString& OutError)
{
    OutName.Reset();
    OutArguments.Reset();
    const FString Trimmed = Text.TrimStartAndEnd();
    const int32 Open = Trimmed.Find(TEXT("("));
    if (Open == INDEX_NONE)
    {
        if (Trimmed.IsEmpty())
        {
            OutError = TEXT("Parameter type is empty.");
            return false;
        }
        OutName = Trimmed;
        return true;
    }
    if (!Trimmed.EndsWith(TEXT(")")) || Open == 0)
    {
        OutError = TEXT("Parameter type is not valid NativePropertyTypeText grammar.");
        return false;
    }
    OutName = Trimmed.Left(Open).TrimStartAndEnd();
    const FString Body = Trimmed.Mid(Open + 1, Trimmed.Len() - Open - 2);
    int32 Depth = 0;
    int32 Start = 0;
    for (int32 Index = 0; Index < Body.Len(); ++Index)
    {
        const TCHAR Character = Body[Index];
        if (Character == TEXT('('))
        {
            ++Depth;
        }
        else if (Character == TEXT(')'))
        {
            if (--Depth < 0)
            {
                OutError = TEXT("Parameter type contains unbalanced parentheses.");
                return false;
            }
        }
        else if (Character == TEXT(',') && Depth == 0)
        {
            OutArguments.Add(Body.Mid(Start, Index - Start).TrimStartAndEnd());
            Start = Index + 1;
        }
    }
    if (Depth != 0)
    {
        OutError = TEXT("Parameter type contains unbalanced parentheses.");
        return false;
    }
    OutArguments.Add(Body.Mid(Start).TrimStartAndEnd());
    if (OutArguments.Num() == 1 && OutArguments[0].IsEmpty())
    {
        OutArguments.Reset();
    }
    return true;
}

template <typename TObjectType>
TObjectType* ResolveUniqueNativeTypeObject(const FString& Text, FString& OutError)
{
    TObjectType* Match = nullptr;
    for (TObjectIterator<TObjectType> It; It; ++It)
    {
        TObjectType* Candidate = *It;
        if (Candidate == nullptr
            || !(Candidate->GetPathName() == Text || Candidate->GetName() == Text))
        {
            continue;
        }
        if (Match != nullptr && Match != Candidate)
        {
            OutError = FString::Printf(TEXT("Native type token %s is ambiguous; use the full UE path."), *Text);
            return nullptr;
        }
        Match = Candidate;
    }
    if (Match == nullptr && Text.StartsWith(TEXT("/")))
    {
        Match = Cast<TObjectType>(StaticLoadObject(TObjectType::StaticClass(), nullptr, *Text));
    }
    if (Match == nullptr)
    {
        OutError = FString::Printf(TEXT("Native type object %s is not resolvable."), *Text);
    }
    return Match;
}

bool ParseParameterLeafType(const FString& Text, FParameterType& OutType, FString& OutError)
{
    FString Name;
    TArray<FString> Args;
    if (!SplitNativeTypeCall(Text, Name, Args, OutError))
    {
        return false;
    }
    struct FSimpleType
    {
        const TCHAR* Name;
        EPropertyBagPropertyType Type;
    };
    static const FSimpleType SimpleTypes[] = {
        {TEXT("BoolProperty"), EPropertyBagPropertyType::Bool},
        {TEXT("ByteProperty"), EPropertyBagPropertyType::Byte},
        {TEXT("IntProperty"), EPropertyBagPropertyType::Int32},
        {TEXT("Int64Property"), EPropertyBagPropertyType::Int64},
        {TEXT("UInt32Property"), EPropertyBagPropertyType::UInt32},
        {TEXT("UInt64Property"), EPropertyBagPropertyType::UInt64},
        {TEXT("FloatProperty"), EPropertyBagPropertyType::Float},
        {TEXT("DoubleProperty"), EPropertyBagPropertyType::Double},
        {TEXT("NameProperty"), EPropertyBagPropertyType::Name},
        {TEXT("StrProperty"), EPropertyBagPropertyType::String},
        {TEXT("TextProperty"), EPropertyBagPropertyType::Text},
    };
    for (const FSimpleType& Simple : SimpleTypes)
    {
        if (Name == Simple.Name && Args.IsEmpty())
        {
            OutType.ValueType = Simple.Type;
            return true;
        }
    }
    if (Name == TEXT("ByteProperty") && Args.Num() == 1)
    {
        OutType.ValueType = EPropertyBagPropertyType::Enum;
        OutType.ValueTypeObject = ResolveUniqueNativeTypeObject<UEnum>(Args[0], OutError);
        return OutType.ValueTypeObject != nullptr;
    }
    if (Name == TEXT("EnumProperty") && Args.Num() == 2)
    {
        FParameterType Underlying;
        if (!ParseParameterLeafType(Args[1], Underlying, OutError)
            || !Underlying.Containers.IsEmpty()
            || !(Underlying.ValueType == EPropertyBagPropertyType::Byte
                || Underlying.ValueType == EPropertyBagPropertyType::Int32
                || Underlying.ValueType == EPropertyBagPropertyType::Int64
                || Underlying.ValueType == EPropertyBagPropertyType::UInt32
                || Underlying.ValueType == EPropertyBagPropertyType::UInt64))
        {
            if (OutError.IsEmpty())
            {
                OutError = TEXT("EnumProperty has an unsupported native underlying property type.");
            }
            return false;
        }
        OutType.ValueType = EPropertyBagPropertyType::Enum;
        OutType.ValueTypeObject = ResolveUniqueNativeTypeObject<UEnum>(Args[0], OutError);
        return OutType.ValueTypeObject != nullptr;
    }
    if (Name == TEXT("StructProperty") && Args.Num() == 1)
    {
        OutType.ValueType = EPropertyBagPropertyType::Struct;
        OutType.ValueTypeObject = ResolveUniqueNativeTypeObject<UScriptStruct>(Args[0], OutError);
        return OutType.ValueTypeObject != nullptr;
    }
    if ((Name == TEXT("ObjectProperty") || Name == TEXT("ObjectPtrProperty")) && Args.Num() == 1)
    {
        OutType.ValueType = EPropertyBagPropertyType::Object;
        OutType.ValueTypeObject = ResolveUniqueNativeTypeObject<UClass>(Args[0], OutError);
        return OutType.ValueTypeObject != nullptr;
    }
    if (Name == TEXT("SoftObjectProperty") && Args.Num() == 1)
    {
        OutType.ValueType = EPropertyBagPropertyType::SoftObject;
        OutType.ValueTypeObject = ResolveUniqueNativeTypeObject<UClass>(Args[0], OutError);
        return OutType.ValueTypeObject != nullptr;
    }
    if (Name == TEXT("ClassProperty") && Args.Num() == 1)
    {
        OutType.ValueType = EPropertyBagPropertyType::Class;
        OutType.ValueTypeObject = ResolveUniqueNativeTypeObject<UClass>(Args[0], OutError);
        return OutType.ValueTypeObject != nullptr;
    }
    if (Name == TEXT("SoftClassProperty") && Args.Num() == 1)
    {
        OutType.ValueType = EPropertyBagPropertyType::SoftClass;
        OutType.ValueTypeObject = ResolveUniqueNativeTypeObject<UClass>(Args[0], OutError);
        return OutType.ValueTypeObject != nullptr;
    }
    if (Name == TEXT("MapProperty") || Name == TEXT("OptionalProperty"))
    {
        OutError = FString::Printf(
            TEXT("%s is a valid UE Property type but Property Bag does not support it."),
            *Name);
        return false;
    }
    OutError = FString::Printf(
        TEXT("Native property type %s cannot be represented losslessly by FPropertyBagPropertyDesc."),
        *Text);
    return false;
}

bool ParseParameterType(const FString& Text, FParameterType& OutType, FString& OutError)
{
    OutType = {};
    FString Cursor = Text.TrimStartAndEnd();
    while (true)
    {
        FString Name;
        TArray<FString> Args;
        if (!SplitNativeTypeCall(Cursor, Name, Args, OutError))
        {
            return false;
        }
        EPropertyBagContainerType Container = EPropertyBagContainerType::None;
        if (Name == TEXT("ArrayProperty"))
        {
            Container = EPropertyBagContainerType::Array;
        }
        else if (Name == TEXT("SetProperty"))
        {
            Container = EPropertyBagContainerType::Set;
        }
        else
        {
            return ParseParameterLeafType(Cursor, OutType, OutError);
        }
        if (Args.Num() != 1 || !OutType.Containers.Add(Container))
        {
            OutError = TEXT("Property Bag container nesting is invalid or exceeds UE's native limit.");
            return false;
        }
        Cursor = Args[0];
        FParameterType Leaf;
        if (Cursor.StartsWith(TEXT("ArrayProperty(")) || Cursor.StartsWith(TEXT("SetProperty(")))
        {
            continue;
        }
        if (!ParseParameterLeafType(Cursor, Leaf, OutError))
        {
            return false;
        }
        OutType.ValueType = Leaf.ValueType;
        OutType.ValueTypeObject = Leaf.ValueTypeObject;
        return true;
    }
}

bool ReadNativeNameValue(const TSharedPtr<FJsonValue>& Value, FName& OutName)
{
    FString Text;
    if (Value.IsValid() && Value->TryGetString(Text) && !Text.IsEmpty())
    {
        OutName = FName(*Text);
        return !OutName.IsNone();
    }
    const TSharedPtr<FJsonObject>* Object = nullptr;
    FString Kind;
    return Value.IsValid()
        && Value->TryGetObject(Object)
        && Object != nullptr
        && (*Object)->TryGetStringField(TEXT("kind"), Kind)
        && Kind == TEXT("name")
        && (*Object)->TryGetStringField(TEXT("name"), Text)
        && !Text.IsEmpty()
        && (OutName = FName(*Text), !OutName.IsNone());
}

bool ReadParameterMetaData(
    const TSharedPtr<FJsonValue>& Value,
    TArray<FPropertyBagPropertyDescMetaData>& OutMetaData,
    FString& OutError)
{
    OutMetaData.Reset();
    const TArray<TSharedPtr<FJsonValue>>* Items = nullptr;
    if (!Value.IsValid() || !Value->TryGetArray(Items) || Items == nullptr)
    {
        OutError = TEXT("Parameter MetaData must be an ordered array of { Key, Value } records.");
        return false;
    }
    TSet<FName> Keys;
    for (const TSharedPtr<FJsonValue>& Item : *Items)
    {
        const TSharedPtr<FJsonObject>* Object = nullptr;
        FString Key;
        FString Text;
        if (!Item.IsValid()
            || !Item->TryGetObject(Object)
            || Object == nullptr
            || (*Object)->Values.Num() != 2
            || !(*Object)->TryGetStringField(TEXT("Key"), Key)
            || !(*Object)->TryGetStringField(TEXT("Value"), Text)
            || Key.IsEmpty())
        {
            OutError = TEXT("Parameter MetaData contains a malformed { Key, Value } record.");
            return false;
        }
        const FName NativeKey(*Key);
        if (Keys.Contains(NativeKey))
        {
            OutError = FString::Printf(TEXT("Parameter MetaData key %s is duplicated."), *Key);
            return false;
        }
        Keys.Add(NativeKey);
        OutMetaData.Emplace(NativeKey, Text);
    }
    return true;
}

FProperty* ParameterOwnerProperty(UObject& Owner, const bool bRoot)
{
    return bRoot
        ? Owner.GetClass()->FindPropertyByName(FName(TEXT("RootParameterPropertyBag")))
        : Owner.GetClass()->FindPropertyByName(GET_MEMBER_NAME_CHECKED(UStateTreeState, Parameters));
}

bool ApplyParameterLayoutChange(
    UObject& Owner,
    const bool bRoot,
    const EPropertyChangeType::Type ChangeType,
    TFunctionRef<bool()> Change,
    FString& OutError)
{
    FProperty* Property = ParameterOwnerProperty(Owner, bRoot);
    if (Property == nullptr)
    {
        OutError = TEXT("UE does not expose the native Parameter container property for editor notification.");
        return false;
    }
    FEditPropertyChain Chain;
    Chain.AddTail(Property);
    Chain.SetActiveMemberPropertyNode(Property);
    Chain.SetActivePropertyNode(Property);
    Owner.Modify();
    Owner.PreEditChange(Chain);
    const bool bChanged = Change();
    FPropertyChangedEvent Event(Property, ChangeType);
    FPropertyChangedChainEvent ChainEvent(Chain, Event);
    Owner.PostEditChangeChainProperty(ChainEvent);
    return bChanged;
}

bool InitializeNode(
    FStateTreeEditorNode& Node,
    UObject& Outer,
    const StateTreePalette::FEntry& Entry,
    const FGuid& PlannedId,
    FString& OutError)
{
    Node.Reset();
    if (Entry.NodeStruct == nullptr
        || !Entry.NodeStruct->IsChildOf(FStateTreeNodeBase::StaticStruct()))
    {
        OutError = TEXT("Palette entry has no valid native StateTree Node struct.");
        return false;
    }
    Node.Node.InitializeAs(Entry.NodeStruct);
    if (Entry.NodeClass != nullptr)
    {
        bool bAssignedClass = false;
        for (TFieldIterator<FClassProperty> It(Entry.NodeStruct); It; ++It)
        {
            FClassProperty* ClassProperty = *It;
            if (ClassProperty != nullptr
                && Entry.NodeClass->IsChildOf(ClassProperty->MetaClass))
            {
                ClassProperty->SetObjectPropertyValue_InContainer(
                    Node.Node.GetMutableMemory(),
                    Entry.NodeClass);
                bAssignedClass = true;
                break;
            }
        }
        if (!bAssignedClass)
        {
            OutError = TEXT("Blueprint Palette entry does not match its native wrapper Class field.");
            Node.Reset();
            return false;
        }
        Node.InstanceObject = NewObject<UObject>(&Outer, Entry.NodeClass, NAME_None, RF_Transactional);
        if (Node.InstanceObject == nullptr)
        {
            OutError = TEXT("UE could not instantiate Blueprint StateTree Node data.");
            Node.Reset();
            return false;
        }
    }
    FStateTreeNodeBase* NodeBase = Node.Node.GetMutablePtr<FStateTreeNodeBase>();
    if (NodeBase == nullptr)
    {
        OutError = TEXT("Palette Node is not a native FStateTreeNodeBase.");
        Node.Reset();
        return false;
    }
    if (Entry.NodeClass == nullptr)
    {
        if (const UScriptStruct* InstanceStruct = Cast<UScriptStruct>(NodeBase->GetInstanceDataType()))
        {
            Node.Instance.InitializeAs(InstanceStruct);
        }
        else if (const UClass* InstanceClass = Cast<UClass>(NodeBase->GetInstanceDataType()))
        {
            Node.InstanceObject = NewObject<UObject>(&Outer, InstanceClass, NAME_None, RF_Transactional);
        }
    }
    if (const UScriptStruct* RuntimeStruct = Cast<UScriptStruct>(NodeBase->GetExecutionRuntimeDataType()))
    {
        Node.ExecutionRuntimeData.InitializeAs(RuntimeStruct);
    }
    else if (const UClass* RuntimeClass = Cast<UClass>(NodeBase->GetExecutionRuntimeDataType()))
    {
        Node.ExecutionRuntimeDataObject = NewObject<UObject>(&Outer, RuntimeClass, NAME_None, RF_Transactional);
    }
    Node.ID = PlannedId;
    return true;
}

int32 AnchorIndex(
    const TSharedPtr<FJsonObject>& Anchor,
    const FString& ExpectedKind,
    FPatchContext& Context,
    FString& OutError)
{
    if (!Anchor.IsValid())
    {
        return INDEX_NONE;
    }
    FString Kind;
    FString Id;
    if (!Context.ResolveRef(Anchor, Kind, Id, OutError) || Kind != ExpectedKind)
    {
        OutError = FString::Printf(TEXT("Placement anchor must be a %s reference."), *ExpectedKind);
        return INDEX_NONE;
    }
    FAuthoredIndex Index = Context.MakeIndex();
    if (!Index.IsValid())
    {
        OutError = Index.GetError();
        return INDEX_NONE;
    }
    if (Kind == TEXT("state"))
    {
        FGuid Guid;
        FStateRef* Item = ParseGuid(Id, Guid) ? Index.FindState(Guid) : nullptr;
        return Item != nullptr ? Item->Index : INDEX_NONE;
    }
    if (Kind == TEXT("node"))
    {
        FGuid Guid;
        FNodeRef* Item = ParseGuid(Id, Guid) ? Index.FindNode(Guid) : nullptr;
        return Item != nullptr ? Item->Index : INDEX_NONE;
    }
    if (Kind == TEXT("transition"))
    {
        FGuid Guid;
        FTransitionRef* Item = ParseGuid(Id, Guid) ? Index.FindTransition(Guid) : nullptr;
        return Item != nullptr ? Item->Index : INDEX_NONE;
    }
    if (Kind == TEXT("parameter"))
    {
        FParameterRef* Item = Index.FindParameter(Id);
        return Item != nullptr ? Item->Index : INDEX_NONE;
    }
    return INDEX_NONE;
}

bool ApplySetReset(
    FPatchContext& Context,
    const TSharedPtr<FJsonObject>& Member,
    const TSharedPtr<FJsonValue>& Value,
    bool bReset,
    const FString& OperationName,
    bool bRecordOperation = true);

bool ApplyConstructorFields(
    FPatchContext& Context,
    const FConstructorDefinition& Definition,
    const FCreatedRef& Created)
{
    TArray<FString> Keys;
    Definition.Args->Values.GetKeys(Keys);
    Keys.Remove(TEXT("palette"));
    if (Created.Kind == TEXT("parameter"))
    {
        // Descriptor identity, layout, type and metadata are established
        // atomically by Property Bag materialization. Only the value remains a
        // normal member edit after the descriptor exists.
        Keys.Remove(TEXT("Name"));
        Keys.Remove(TEXT("type"));
        Keys.Remove(TEXT("MetaData"));
    }
    Keys.Sort([](const FString& Left, const FString& Right)
    {
        auto Rank = [](const FString& Key)
        {
            if (Key == TEXT("Type") || Key == TEXT("Trigger")) return 0;
            if (Key == TEXT("Name")) return 1;
            if (Key == TEXT("LinkedSubtree") || Key == TEXT("LinkedAsset") || Key == TEXT("State")) return 2;
            return 3;
        };
        const int32 LeftRank = Rank(Left);
        const int32 RightRank = Rank(Right);
        return LeftRank == RightRank ? Left < Right : LeftRank < RightRank;
    });
    for (const FString& Key : Keys)
    {
        const TSharedPtr<FJsonObject> Target = MemberRef(
            StableRef(Created.Kind, Created.Id),
            {Key});
        if (!ApplySetReset(
                Context,
                Target,
                Definition.Args->TryGetField(Key),
                false,
                TEXT("add"),
                false))
        {
            return false;
        }
    }
    return true;
}

int32 StateAnchorIn(
    const TArray<TObjectPtr<UStateTreeState>>& Array,
    const TSharedPtr<FJsonObject>& Anchor,
    FPatchContext& Context,
    FString& OutError)
{
    if (!Anchor.IsValid())
    {
        return Array.Num();
    }
    FString Kind;
    FString Id;
    FGuid Guid;
    if (!Context.ResolveRef(Anchor, Kind, Id, OutError)
        || Kind != TEXT("state")
        || !ParseGuid(Id, Guid))
    {
        OutError = TEXT("State placement anchor is invalid.");
        return INDEX_NONE;
    }
    int32 Match = INDEX_NONE;
    for (int32 Index = 0; Index < Array.Num(); ++Index)
    {
        if (Array[Index] != nullptr && Array[Index]->ID == Guid)
        {
            if (Match != INDEX_NONE)
            {
                OutError = TEXT("State placement anchor is ambiguous in its destination.");
                return INDEX_NONE;
            }
            Match = Index;
        }
    }
    if (Match == INDEX_NONE)
    {
        OutError = TEXT("State placement anchor does not belong to the exact destination.");
    }
    return Match;
}

int32 NodeAnchorIn(
    const TArray<FStateTreeEditorNode>& Array,
    const TSharedPtr<FJsonObject>& Anchor,
    FPatchContext& Context,
    FString& OutError)
{
    if (!Anchor.IsValid())
    {
        return Array.Num();
    }
    FString Kind;
    FString Id;
    FGuid Guid;
    if (!Context.ResolveRef(Anchor, Kind, Id, OutError)
        || Kind != TEXT("node")
        || !ParseGuid(Id, Guid))
    {
        OutError = TEXT("Node placement anchor is invalid.");
        return INDEX_NONE;
    }
    int32 Match = INDEX_NONE;
    for (int32 Index = 0; Index < Array.Num(); ++Index)
    {
        if (Array[Index].ID == Guid)
        {
            if (Match != INDEX_NONE)
            {
                OutError = TEXT("Node placement anchor is ambiguous in its destination.");
                return INDEX_NONE;
            }
            Match = Index;
        }
    }
    if (Match == INDEX_NONE)
    {
        OutError = TEXT("Node placement anchor does not belong to the exact destination.");
    }
    return Match;
}

int32 TransitionAnchorIn(
    const TArray<FStateTreeTransition>& Array,
    const TSharedPtr<FJsonObject>& Anchor,
    FPatchContext& Context,
    FString& OutError)
{
    if (!Anchor.IsValid())
    {
        return Array.Num();
    }
    FString Kind;
    FString Id;
    FGuid Guid;
    if (!Context.ResolveRef(Anchor, Kind, Id, OutError)
        || Kind != TEXT("transition")
        || !ParseGuid(Id, Guid))
    {
        OutError = TEXT("Transition placement anchor is invalid.");
        return INDEX_NONE;
    }
    int32 Match = INDEX_NONE;
    for (int32 Index = 0; Index < Array.Num(); ++Index)
    {
        if (Array[Index].ID == Guid)
        {
            if (Match != INDEX_NONE)
            {
                OutError = TEXT("Transition placement anchor is ambiguous in its destination.");
                return INDEX_NONE;
            }
            Match = Index;
        }
    }
    if (Match == INDEX_NONE)
    {
        OutError = TEXT("Transition placement anchor does not belong to the exact destination.");
    }
    return Match;
}

bool MaterializeState(
    FPatchContext& Context,
    const StateTreePalette::FDestination& Destination,
    const FConstructorDefinition& Definition,
    const TSharedPtr<FJsonObject>& Anchor,
    const bool bAfter,
    FCreatedRef& OutCreated,
    FString& OutError)
{
    TArray<TObjectPtr<UStateTreeState>>* Array = nullptr;
    UStateTreeState* Parent = nullptr;
    UObject* Outer = &Context.Data;
    if (Destination.Role == StateTreePalette::EDestinationRole::RootState)
    {
        Array = &Context.Data.SubTrees;
    }
    else if (Destination.Role == StateTreePalette::EDestinationRole::ChildState)
    {
        FAuthoredIndex Index = Context.MakeIndex();
        FStateRef* ParentRef = Index.IsValid() ? Index.FindState(Destination.OwnerId) : nullptr;
        Parent = ParentRef != nullptr ? ParentRef->State : nullptr;
        if (!Index.IsValid() || Parent == nullptr)
        {
            OutError = Index.IsValid()
                ? TEXT("Child State destination owner is missing or ambiguous.")
                : Index.GetError();
            return false;
        }
        Array = &Parent->Children;
        Outer = Parent;
    }
    else
    {
        OutError = TEXT("State constructor cannot be used in this destination.");
        return false;
    }
    int32 InsertIndex = StateAnchorIn(*Array, Anchor, Context, OutError);
    if (InsertIndex == INDEX_NONE)
    {
        return false;
    }
    if (Anchor.IsValid() && bAfter)
    {
        ++InsertIndex;
    }
    UStateTreeState* State = NewObject<UStateTreeState>(Outer, NAME_None, RF_Transactional);
    if (State == nullptr)
    {
        OutError = TEXT("UE could not allocate a State in the exact destination.");
        return false;
    }
    State->ID = Definition.PlannedId;
    State->Parameters.ID = Definition.PlannedParameterContainerId;
    State->Parent = Parent;
    Outer->Modify();
    Array->Insert(State, InsertIndex);
    OutCreated = {TEXT("state"), GuidText(State->ID)};
    return true;
}

bool ResolveNodeDestination(
    FPatchContext& Context,
    const StateTreePalette::FDestination& Destination,
    TArray<FStateTreeEditorNode>*& OutArray,
    FStateTreeEditorNode*& OutSingle,
    UObject*& OutOuter,
    FString& OutError)
{
    OutArray = nullptr;
    OutSingle = nullptr;
    OutOuter = &Context.Data;
    FAuthoredIndex Index = Context.MakeIndex();
    if (!Index.IsValid())
    {
        OutError = Index.GetError();
        return false;
    }
    switch (Destination.Role)
    {
    case StateTreePalette::EDestinationRole::GlobalEvaluator:
        OutArray = &Context.Data.Evaluators;
        return true;
    case StateTreePalette::EDestinationRole::GlobalTask:
        OutArray = &Context.Data.GlobalTasks;
        return true;
    case StateTreePalette::EDestinationRole::EnterCondition:
    case StateTreePalette::EDestinationRole::Task:
    case StateTreePalette::EDestinationRole::Consideration:
    {
        FStateRef* Owner = Index.FindState(Destination.OwnerId);
        if (Owner == nullptr)
        {
            OutError = TEXT("State-owned Node destination is missing or ambiguous.");
            return false;
        }
        OutOuter = Owner->State;
        if (Destination.Role == StateTreePalette::EDestinationRole::EnterCondition)
        {
            OutArray = &Owner->State->EnterConditions;
        }
        else if (Destination.Role == StateTreePalette::EDestinationRole::Consideration)
        {
            OutArray = &Owner->State->Considerations;
        }
        else if (!Destination.MemberPath.IsEmpty()
            && Destination.MemberPath.Last() == TEXT("SingleTask"))
        {
            OutSingle = &Owner->State->SingleTask;
        }
        else
        {
            OutArray = &Owner->State->Tasks;
        }
        return true;
    }
    case StateTreePalette::EDestinationRole::TransitionCondition:
    {
        FTransitionRef* Owner = Index.FindTransition(Destination.OwnerId);
        if (Owner == nullptr || Owner->OwnerState == nullptr)
        {
            OutError = TEXT("Transition-owned Condition destination is missing or ambiguous.");
            return false;
        }
        OutArray = &Owner->Transition->Conditions;
        OutOuter = Owner->OwnerState;
        return true;
    }
    case StateTreePalette::EDestinationRole::PropertyFunction:
        OutError = TEXT("Property Function Nodes are materialized only by their owning result Binding.");
        return false;
    default:
        OutError = TEXT("Node constructor cannot be used in this destination.");
        return false;
    }
}

bool MaterializeNode(
    FPatchContext& Context,
    const StateTreePalette::FDestination& Destination,
    const StateTreePalette::FEntry& Entry,
    const FConstructorDefinition& Definition,
    const TSharedPtr<FJsonObject>& Anchor,
    const bool bAfter,
    FCreatedRef& OutCreated,
    FString& OutError)
{
    TArray<FStateTreeEditorNode>* Array = nullptr;
    FStateTreeEditorNode* Single = nullptr;
    UObject* Outer = nullptr;
    if (!ResolveNodeDestination(Context, Destination, Array, Single, Outer, OutError))
    {
        return false;
    }
    if (Single != nullptr)
    {
        if (Anchor.IsValid())
        {
            OutError = TEXT("SingleTask is not an ordered destination and cannot use before/after.");
            return false;
        }
        if (Single->ID.IsValid() || Single->Node.IsValid())
        {
            OutError = TEXT("SingleTask cardinality is one and the destination is already occupied.");
            return false;
        }
        Outer->Modify();
        if (!InitializeNode(*Single, *Outer, Entry, Definition.PlannedId, OutError))
        {
            return false;
        }
    }
    else
    {
        int32 InsertIndex = NodeAnchorIn(*Array, Anchor, Context, OutError);
        if (InsertIndex == INDEX_NONE)
        {
            return false;
        }
        if (Anchor.IsValid() && bAfter)
        {
            ++InsertIndex;
        }
        FStateTreeEditorNode Node;
        if (!InitializeNode(Node, *Outer, Entry, Definition.PlannedId, OutError))
        {
            return false;
        }
        Outer->Modify();
        Array->Insert(MoveTemp(Node), InsertIndex);
    }
    OutCreated = {TEXT("node"), GuidText(Definition.PlannedId)};
    return true;
}

bool MaterializeTransition(
    FPatchContext& Context,
    const StateTreePalette::FDestination& Destination,
    const FConstructorDefinition& Definition,
    const TSharedPtr<FJsonObject>& Anchor,
    const bool bAfter,
    FCreatedRef& OutCreated,
    FString& OutError)
{
    if (Destination.Role != StateTreePalette::EDestinationRole::Transition)
    {
        OutError = TEXT("Transition constructor cannot be used in this destination.");
        return false;
    }
    FAuthoredIndex Index = Context.MakeIndex();
    FStateRef* Owner = Index.IsValid() ? Index.FindState(Destination.OwnerId) : nullptr;
    if (!Index.IsValid() || Owner == nullptr)
    {
        OutError = Index.IsValid()
            ? TEXT("Transition destination State is missing or ambiguous.")
            : Index.GetError();
        return false;
    }
    TArray<FStateTreeTransition>& Array = Owner->State->Transitions;
    int32 InsertIndex = TransitionAnchorIn(Array, Anchor, Context, OutError);
    if (InsertIndex == INDEX_NONE)
    {
        return false;
    }
    if (Anchor.IsValid() && bAfter)
    {
        ++InsertIndex;
    }
    const UStateTreeState* RootState = Owner->State->GetRootState();
    FProperty* TransitionsProperty = UStateTreeState::StaticClass()->FindPropertyByName(
        GET_MEMBER_NAME_CHECKED(UStateTreeState, Transitions));
    if (RootState == nullptr || TransitionsProperty == nullptr)
    {
        OutError = TEXT("Transition destination has no native root State or reflected Transitions property.");
        return false;
    }
    Owner->State->Modify();
    FEditPropertyChain Chain;
    Chain.AddTail(TransitionsProperty);
    Chain.SetActiveMemberPropertyNode(TransitionsProperty);
    Chain.SetActivePropertyNode(TransitionsProperty);
    Owner->State->PreEditChange(Chain);
    Array.InsertDefaulted(InsertIndex);
    FPropertyChangedEvent Event(TransitionsProperty, EPropertyChangeType::ArrayAdd);
    TArray<TMap<FString, int32>> PerObjectIndices;
    TMap<FString, int32>& Indices = PerObjectIndices.AddDefaulted_GetRef();
    Indices.Add(TransitionsProperty->GetName(), InsertIndex);
    Event.ObjectIteratorIndex = 0;
    Event.SetArrayIndexPerObject(PerObjectIndices);
    FPropertyChangedChainEvent ChainEvent(Chain, Event);
    Owner->State->PostEditChangeChainProperty(ChainEvent);
    if (!Array.IsValidIndex(InsertIndex)
        || Array[InsertIndex].Trigger != EStateTreeTransitionTrigger::OnStateCompleted
        || Array[InsertIndex].State.ID != RootState->ID)
    {
        OutError = FString::Printf(
            TEXT("UE did not apply native Transition ArrayAdd initialization")
            TEXT(" (requested index %d, event index %d, count %d, trigger %d, target %s, root %s)."),
            InsertIndex,
            ChainEvent.GetArrayIndex(TransitionsProperty->GetName()),
            Array.Num(),
            Array.IsValidIndex(InsertIndex)
                ? static_cast<int32>(Array[InsertIndex].Trigger)
                : INDEX_NONE,
            Array.IsValidIndex(InsertIndex)
                ? *GuidText(Array[InsertIndex].State.ID)
                : TEXT("<missing>"),
            *GuidText(RootState->ID));
        return false;
    }
    // Native ArrayAdd owns all defaults; SAL owns only the preflight-stable id.
    Array[InsertIndex].ID = Definition.PlannedId;
    OutCreated = {TEXT("transition"), GuidText(Definition.PlannedId)};
    return true;
}

bool MaterializeParameter(
    FPatchContext& Context,
    const StateTreePalette::FDestination& Destination,
    const FConstructorDefinition& Definition,
    const TSharedPtr<FJsonObject>& Anchor,
    const bool bAfter,
    FCreatedRef& OutCreated,
    FString& OutError)
{
    if (Destination.Role != StateTreePalette::EDestinationRole::Parameter)
    {
        OutError = TEXT("Parameter constructor cannot be used in this destination.");
        return false;
    }
    FInstancedPropertyBag* Bag = nullptr;
    UObject* Owner = nullptr;
    FGuid ContainerId;
    bool bRoot = false;
    if (Destination.OwnerId.IsValid())
    {
        FAuthoredIndex Index = Context.MakeIndex();
        FStateRef* State = Index.IsValid() ? Index.FindState(Destination.OwnerId) : nullptr;
        if (!Index.IsValid() || State == nullptr)
        {
            OutError = Index.IsValid()
                ? TEXT("Parameter destination State is missing or ambiguous.")
                : Index.GetError();
            return false;
        }
        if (State->State->Parameters.bFixedLayout)
        {
            OutError = TEXT("Fixed-layout State Parameters do not allow descriptor creation.");
            return false;
        }
        Bag = &State->State->Parameters.Parameters;
        ContainerId = State->State->Parameters.ID;
        Owner = State->State;
    }
    else
    {
        Bag = &const_cast<FInstancedPropertyBag&>(Context.Data.GetRootParametersPropertyBag());
        ContainerId = Context.Data.GetRootParametersGuid();
        Owner = &Context.Data;
        bRoot = true;
    }
    if (Bag == nullptr || Owner == nullptr || !ContainerId.IsValid())
    {
        OutError = TEXT("Parameter destination has no valid native Property Bag container.");
        return false;
    }

    FName Name;
    if (!ReadNativeNameValue(Definition.Args->TryGetField(TEXT("Name")), Name))
    {
        OutError = TEXT("Parameter constructor requires a non-empty native Name field.");
        return false;
    }
    FString TypeText;
    if (!Definition.Args->TryGetStringField(TEXT("type"), TypeText))
    {
        OutError = TEXT("Parameter constructor requires one NativePropertyTypeText type field.");
        return false;
    }
    FParameterType Type;
    if (!ParseParameterType(TypeText, Type, OutError))
    {
        return false;
    }
    FPropertyBagPropertyDesc Desc(Name, Type.Containers, Type.ValueType, Type.ValueTypeObject);
    Desc.ID = Definition.PlannedId;
    if (Definition.Args->HasField(TEXT("MetaData"))
        && !ReadParameterMetaData(
            Definition.Args->TryGetField(TEXT("MetaData")),
            Desc.MetaData,
            OutError))
    {
        return false;
    }

    FName AnchorName = NAME_None;
    if (Anchor.IsValid())
    {
        FString AnchorKind;
        FString AnchorId;
        if (!Context.ResolveRef(Anchor, AnchorKind, AnchorId, OutError)
            || AnchorKind != TEXT("parameter"))
        {
            OutError = TEXT("Parameter placement anchor is invalid.");
            return false;
        }
        FAuthoredIndex Index = Context.MakeIndex();
        FParameterRef* Parameter = Index.IsValid() ? Index.FindParameter(AnchorId) : nullptr;
        if (!Index.IsValid() || Parameter == nullptr || Parameter->ContainerId != ContainerId)
        {
            OutError = Index.IsValid()
                ? TEXT("Parameter placement anchor must belong to the exact destination container.")
                : Index.GetError();
            return false;
        }
        AnchorName = Parameter->Desc.Name;
    }

    bool bBagChanged = false;
    if (!ApplyParameterLayoutChange(
            *Owner,
            bRoot,
            EPropertyChangeType::ArrayAdd,
            [&]()
            {
                const EPropertyBagAlterationResult Result = Bag->AddProperties({Desc}, false);
                if (Result != EPropertyBagAlterationResult::Success)
                {
                    OutError = FString::Printf(
                        TEXT("UE rejected the Parameter descriptor with Property Bag result %d."),
                        static_cast<int32>(Result));
                    return false;
                }
                bBagChanged = true;
                if (Anchor.IsValid())
                {
                    const UPropertyBag* Struct = Bag->GetPropertyBagStruct();
                    const FPropertyBagPropertyDesc* Added = Struct != nullptr
                        ? Struct->FindPropertyDescByID(Definition.PlannedId)
                        : nullptr;
                    if (Added == nullptr)
                    {
                        OutError = TEXT("UE did not preserve the planned Parameter identity after creation.");
                        return false;
                    }
                    const EPropertyBagAlterationResult Reorder = Bag->ReorderProperty(
                        Added->Name,
                        AnchorName,
                        !bAfter);
                    if (Reorder != EPropertyBagAlterationResult::Success)
                    {
                        OutError = TEXT("UE could not place the new Parameter relative to its exact anchor.");
                        return false;
                    }
                }
                return true;
            },
            OutError))
    {
        return false;
    }
    const UPropertyBag* ResultStruct = Bag->GetPropertyBagStruct();
    const FPropertyBagPropertyDesc* ResultDesc = ResultStruct != nullptr
        ? ResultStruct->FindPropertyDescByID(Definition.PlannedId)
        : nullptr;
    if (!bBagChanged || ResultDesc == nullptr || ResultDesc->Name != Name)
    {
        OutError = TEXT("UE changed or lost the planned Parameter identity during creation.");
        return false;
    }
    OutCreated = {
        TEXT("parameter"),
        GuidText(ContainerId) + TEXT("/") + GuidText(ResultDesc->ID)};
    return true;
}

bool HandleAdd(FPatchContext& Context, const TSharedPtr<FJsonObject>& Operation)
{
    const TSharedPtr<FJsonObject>* TargetRef = nullptr;
    FString TargetKind;
    FString Alias;
    if (!Operation->TryGetObjectField(TEXT("target"), TargetRef)
        || TargetRef == nullptr
        || !(*TargetRef)->TryGetStringField(TEXT("kind"), TargetKind)
        || TargetKind != TEXT("local")
        || !(*TargetRef)->TryGetStringField(TEXT("name"), Alias))
    {
        return Context.Fail(
            TEXT("validation.creation_invalid"),
            TEXT("StateTree add consumes one local Palette-backed constructor alias."),
            TEXT("add"));
    }
    FConstructorDefinition* Definition = Context.Definitions.Find(Alias);
    if (Definition == nullptr)
    {
        return Context.Fail(
            TEXT("resolution.binding_not_found"),
            TEXT("StateTree add references no declared constructor binding."),
            TEXT("add"),
            Alias);
    }
    if (Definition->bConsumed)
    {
        return Context.Fail(
            TEXT("resolution.binding_already_consumed"),
            TEXT("A StateTree constructor binding can be materialized exactly once."),
            TEXT("add"),
            Alias);
    }

    TSharedPtr<FJsonObject> DestinationRef;
    TSharedPtr<FJsonObject> Anchor;
    bool bBefore = false;
    bool bAfter = false;
    FString Error;
    if (!ReadPlacement(Context, Operation, DestinationRef, Anchor, bBefore, bAfter, Error))
    {
        return Context.Fail(TEXT("resolution.invalid_anchor"), Error, TEXT("add"), Alias);
    }
    StateTreePalette::FDestination Destination;
    if (!StateTreePalette::ResolveDestination(
            Context.Tree,
            Context.Data,
            DestinationRef,
            Destination,
            Error))
    {
        return Context.Fail(TEXT("resolution.palette_not_found"), Error, TEXT("add"), Alias);
    }
    StateTreePalette::FEntry Entry;
    if (!StateTreePalette::ResolveEntry(
            Context.Tree,
            Context.Data,
            Destination,
            Definition->PaletteId,
            Entry,
            Error))
    {
        return Context.Fail(TEXT("resolution.palette_entry_not_found"), Error, TEXT("add"), Alias);
    }
    if (!Entry.bSpawnable)
    {
        return Context.Fail(
            TEXT("resolution.palette_not_spawnable"),
            TEXT("The exact StateTree Palette entry is currently not spawnable."),
            TEXT("add"),
            Alias);
    }
    if (!ConstructorMatches(Definition->Callee, Entry.ConstructorKind))
    {
        return Context.Fail(
            TEXT("validation.creation_invalid"),
            TEXT("Constructor callee does not match the exact Palette entry kind."),
            TEXT("add"),
            Alias);
    }
    if (Entry.ConstructorKind == StateTreePalette::EConstructorKind::State)
    {
        FString RequestedType;
        if (!Definition->Args->TryGetStringField(TEXT("Type"), RequestedType)
            || RequestedType.IsEmpty())
        {
            return Context.Fail(
                TEXT("validation.creation_invalid"),
                TEXT("State constructor requires one explicit native Type."),
                TEXT("add"),
                Alias);
        }
        const UEnum* StateTypeEnum = StaticEnum<EStateTreeStateType>();
        if (StateTypeEnum == nullptr)
        {
            return Context.Fail(
                TEXT("validation.creation_invalid"),
                TEXT("UE native State Type metadata is unavailable."),
                TEXT("add"),
                Alias);
        }
        const auto ResolveStateType = [StateTypeEnum](const FString& Text)
        {
            int64 Value = StateTypeEnum->GetValueByNameString(Text);
            if (Value == INDEX_NONE)
            {
                Value = StateTypeEnum->GetValueByName(FName(*Text));
            }
            return StateTypeEnum->IsValidEnumValue(Value) ? Value : int64(INDEX_NONE);
        };
        const int64 RequestedTypeValue = ResolveStateType(RequestedType);
        const int64 EntryTypeValue = ResolveStateType(Entry.StateType);
        if (RequestedTypeValue == INDEX_NONE || EntryTypeValue == INDEX_NONE)
        {
            return Context.Fail(
                TEXT("validation.creation_invalid"),
                TEXT("State constructor requires one valid native Type from its exact Palette capability."),
                TEXT("add"),
                Alias);
        }
        if (Entry.bFixedStateType && RequestedTypeValue != EntryTypeValue)
        {
            return Context.Fail(
                TEXT("validation.creation_invalid"),
                TEXT("This exact State Palette capability fixes its native Type."),
                TEXT("add"),
                Alias);
        }
        if (!Entry.bFixedStateType
            && RequestedTypeValue != static_cast<int64>(EStateTreeStateType::State)
            && RequestedTypeValue != static_cast<int64>(EStateTreeStateType::Group)
            && RequestedTypeValue != static_cast<int64>(EStateTreeStateType::Subtree))
        {
            return Context.Fail(
                TEXT("validation.creation_invalid"),
                TEXT("The ordinary State Palette capability only covers native State, Group, and Subtree Types."),
                TEXT("add"),
                Alias);
        }
        if (Entry.bFixedStateType
            && Entry.StateSelectionBehavior.IsEmpty()
            && Definition->Args->HasField(TEXT("SelectionBehavior")))
        {
            return Context.Fail(
                TEXT("validation.creation_invalid"),
                TEXT("This exact State Palette capability uses UE's predefined SelectionBehavior and does not accept an override."),
                TEXT("add"),
                Alias);
        }
        const bool bRequestedLinked = RequestedTypeValue
            == static_cast<int64>(EStateTreeStateType::Linked);
        if (Entry.LinkedSubtreeId.IsValid())
        {
            const TSharedPtr<FJsonObject>* LinkedValue = nullptr;
            FString LinkedKind;
            FString LinkedId;
            FString LinkedLocal;
            FGuid LinkedGuid;
            if (!bRequestedLinked
                || !Definition->Args->TryGetObjectField(TEXT("LinkedSubtree"), LinkedValue)
                || LinkedValue == nullptr
                || !ReadRefObject(*LinkedValue, LinkedKind, LinkedId, LinkedLocal)
                || LinkedKind != TEXT("state")
                || !ParseGuid(LinkedId, LinkedGuid)
                || Context.Identities.ResolveState(LinkedGuid) != Entry.LinkedSubtreeId)
            {
                return Context.Fail(
                    TEXT("validation.creation_invalid"),
                    TEXT("Linked State constructor must preserve the exact destination-bound LinkedSubtree Palette capability."),
                    TEXT("add"),
                    Alias);
            }
        }
        else if (bRequestedLinked)
        {
            return Context.Fail(
                TEXT("validation.creation_invalid"),
                TEXT("State Type Linked requires one destination-bound Linked State Palette entry."),
                TEXT("add"),
                Alias);
        }
    }

    Context.Tree.Modify();
    Context.Data.Modify();
    FCreatedRef Created;
    bool bCreated = false;
    switch (Entry.ConstructorKind)
    {
    case StateTreePalette::EConstructorKind::State:
        bCreated = MaterializeState(Context, Destination, *Definition, Anchor, bAfter, Created, Error);
        break;
    case StateTreePalette::EConstructorKind::Node:
        bCreated = MaterializeNode(Context, Destination, Entry, *Definition, Anchor, bAfter, Created, Error);
        break;
    case StateTreePalette::EConstructorKind::Transition:
        bCreated = MaterializeTransition(Context, Destination, *Definition, Anchor, bAfter, Created, Error);
        break;
    case StateTreePalette::EConstructorKind::Parameter:
        bCreated = MaterializeParameter(Context, Destination, *Definition, Anchor, bAfter, Created, Error);
        break;
    }
    if (!bCreated)
    {
        return Context.Fail(TEXT("validation.creation_invalid"), Error, TEXT("add"), Alias);
    }
    Context.CreatedRefs.Add(Alias, Created);
    Definition->bConsumed = true;
    if (!ApplyConstructorFields(Context, *Definition, Created))
    {
        return false;
    }
    ++Context.ChangedOperations;
    Context.ResolvedRefs->SetStringField(Alias, Created.Kind + TEXT("@") + Created.Id);
    TSharedPtr<FJsonObject> Planned = MakeShared<FJsonObject>();
    Planned->SetStringField(TEXT("kind"), TEXT("add"));
    Planned->SetStringField(TEXT("target"), Created.Kind + TEXT("@") + Created.Id);
    Planned->SetStringField(TEXT("palette"), Definition->PaletteId);
    Context.PlannedOperations.Add(MakeShared<FJsonValueObject>(Planned));
    Context.PlannedEffects.Add(MakeShared<FJsonValueString>(TEXT("created ") + Created.Kind + TEXT("@") + Created.Id));
    return true;
}

FString QuoteNativeString(FString Text)
{
    Text.ReplaceInline(TEXT("\\"), TEXT("\\\\"));
    Text.ReplaceInline(TEXT("\""), TEXT("\\\""));
    return TEXT("\"") + Text + TEXT("\"");
}

FString NativeExpressionText(
    const TSharedPtr<FJsonValue>& Value,
    const FProperty* Property,
    FString& OutError)
{
    OutError.Reset();
    if (!Value.IsValid() || Value->IsNull())
    {
        return TEXT("None");
    }
    FString String;
    if (Value->TryGetString(String))
    {
        if (CastField<FStrProperty>(Property) != nullptr
            || CastField<FTextProperty>(Property) != nullptr
            || CastField<FNameProperty>(Property) != nullptr)
        {
            return QuoteNativeString(String);
        }
        return String;
    }
    double Number = 0.0;
    if (Value->TryGetNumber(Number))
    {
        return FString::SanitizeFloat(Number);
    }
    bool bBoolean = false;
    if (Value->TryGetBool(bBoolean))
    {
        return bBoolean ? TEXT("true") : TEXT("false");
    }
    const TArray<TSharedPtr<FJsonValue>>* Array = nullptr;
    if (Value->TryGetArray(Array) && Array != nullptr)
    {
        const FProperty* ElementProperty = nullptr;
        if (const FArrayProperty* ArrayProperty = CastField<FArrayProperty>(Property))
        {
            ElementProperty = ArrayProperty->Inner;
        }
        else if (const FSetProperty* SetProperty = CastField<FSetProperty>(Property))
        {
            ElementProperty = SetProperty->ElementProp;
        }
        TArray<FString> Items;
        for (const TSharedPtr<FJsonValue>& Item : *Array)
        {
            FString ItemError;
            const FString Text = NativeExpressionText(Item, ElementProperty, ItemError);
            if (!ItemError.IsEmpty())
            {
                OutError = ItemError;
                return FString();
            }
            Items.Add(Text);
        }
        return TEXT("(") + FString::Join(Items, TEXT(",")) + TEXT(")");
    }
    const TSharedPtr<FJsonObject>* Object = nullptr;
    if (!Value->TryGetObject(Object) || Object == nullptr || !(*Object).IsValid())
    {
        OutError = TEXT("SAL value cannot be translated to one native UE property value.");
        return FString();
    }
    FString Kind;
    (*Object)->TryGetStringField(TEXT("kind"), Kind);
    if (Kind == TEXT("name"))
    {
        if (!(*Object)->TryGetStringField(TEXT("name"), String))
        {
            OutError = TEXT("Name expression is missing its native token.");
            return FString();
        }
        return String;
    }
    if (Kind == TEXT("state")
        || Kind == TEXT("node")
        || Kind == TEXT("transition")
        || Kind == TEXT("parameter")
        || Kind == TEXT("object"))
    {
        if (!(*Object)->TryGetStringField(TEXT("id"), String))
        {
            OutError = TEXT("Stable reference value is missing its id.");
            return FString();
        }
        return String;
    }
    if (Kind == TEXT("call"))
    {
        const TSharedPtr<FJsonObject>* Args = nullptr;
        if ((*Object)->TryGetObjectField(TEXT("args"), Args) && Args != nullptr)
        {
            for (const TCHAR* Field : {TEXT("path"), TEXT("type"), TEXT("name")})
            {
                if ((*Args)->TryGetStringField(Field, String))
                {
                    return String;
                }
            }
        }
        OutError = TEXT("Call expression is not a native property literal.");
        return FString();
    }
    if (!Kind.IsEmpty())
    {
        OutError = TEXT("SAL expression kind is not supported as a native property value.");
        return FString();
    }
    const FStructProperty* StructProperty = CastField<FStructProperty>(Property);
    TArray<FString> Fields;
    TArray<FString> Keys;
    (*Object)->Values.GetKeys(Keys);
    Keys.Sort();
    for (const FString& Key : Keys)
    {
        const FProperty* Child = StructProperty != nullptr && StructProperty->Struct != nullptr
            ? StructProperty->Struct->FindPropertyByName(FName(*Key))
            : nullptr;
        FString ChildError;
        const FString ChildText = NativeExpressionText((*Object)->TryGetField(Key), Child, ChildError);
        if (!ChildError.IsEmpty())
        {
            OutError = Key + TEXT(": ") + ChildError;
            return FString();
        }
        Fields.Add(Key + TEXT("=") + ChildText);
    }
    return TEXT("(") + FString::Join(Fields, TEXT(",")) + TEXT(")");
}

struct FChangeNotification
{
    UObject* Owner = nullptr;
    FEditPropertyChain Chain;
    TMap<FString, int32> ArrayIndices;

    bool IsValid() const
    {
        return Owner != nullptr && Chain.GetHead() != nullptr;
    }

    void Before()
    {
        if (IsValid())
        {
            Owner->PreEditChange(Chain);
        }
    }

    void After(const EPropertyChangeType::Type ChangeType = EPropertyChangeType::ValueSet)
    {
        if (!IsValid())
        {
            return;
        }
        FProperty* Leaf = Chain.GetTail()->GetValue();
        FPropertyChangedEvent Event(Leaf, ChangeType);
        TArray<TMap<FString, int32>> PerObject;
        if (!ArrayIndices.IsEmpty())
        {
            PerObject.Add(ArrayIndices);
            Event.ObjectIteratorIndex = 0;
            Event.SetArrayIndexPerObject(PerObject);
        }
        FPropertyChangedChainEvent ChainEvent(Chain, Event);
        Owner->PostEditChangeChainProperty(ChainEvent);
    }
};

void AppendProperty(FChangeNotification& Notification, FProperty* Property, const int32 ArrayIndex = INDEX_NONE)
{
    if (Property == nullptr)
    {
        return;
    }
    if (Notification.Chain.GetTail() == nullptr
        || Notification.Chain.GetTail()->GetValue() != Property)
    {
        Notification.Chain.AddTail(Property);
    }
    if (ArrayIndex != INDEX_NONE)
    {
        Notification.ArrayIndices.Add(Property->GetName(), ArrayIndex);
    }
}

void AppendNativePath(
    FChangeNotification& Notification,
    const StateTreeSchema::FResolvedMember& Member)
{
    TArray<FPropertyBindingPathIndirection, TInlineAllocator<16>> Indirections;
    FString Ignored;
    if (!Member.NativePath.ResolveIndirectionsWithValue(
            Member.OwnerView,
            Indirections,
            &Ignored,
            true))
    {
        return;
    }
    for (const FPropertyBindingPathIndirection& Indirection : Indirections)
    {
        AppendProperty(
            Notification,
            const_cast<FProperty*>(Indirection.GetProperty()),
            Indirection.GetArrayIndex());
    }
}

bool BuildNotification(
    FPatchContext& Context,
    const StateTreeSchema::FResolvedMember& Member,
    FChangeNotification& Out,
    FString& OutError)
{
    Out.Owner = nullptr;
    Out.ArrayIndices.Reset();
    FAuthoredIndex Index = Context.MakeIndex();
    if (!Index.IsValid())
    {
        OutError = Index.GetError();
        return false;
    }
    if (Member.OwnerKind == TEXT("target"))
    {
        Out.Owner = &Context.Data;
        Context.Data.Modify();
        AppendNativePath(Out, Member);
    }
    else if (Member.OwnerKind == TEXT("state"))
    {
        FGuid Id;
        FStateRef* State = ParseGuid(Member.OwnerId, Id) ? Index.FindState(Id) : nullptr;
        if (State == nullptr)
        {
            OutError = TEXT("State member owner is missing or ambiguous.");
            return false;
        }
        Out.Owner = State->State;
        State->State->Modify();
        AppendNativePath(Out, Member);
    }
    else if (Member.OwnerKind == TEXT("transition"))
    {
        FGuid Id;
        FTransitionRef* Transition = ParseGuid(Member.OwnerId, Id) ? Index.FindTransition(Id) : nullptr;
        if (Transition == nullptr || Transition->OwnerState == nullptr)
        {
            OutError = TEXT("Transition member owner is missing or ambiguous.");
            return false;
        }
        Out.Owner = Transition->OwnerState;
        Transition->OwnerState->Modify();
        AppendProperty(
            Out,
            UStateTreeState::StaticClass()->FindPropertyByName(
                GET_MEMBER_NAME_CHECKED(UStateTreeState, Transitions)),
            Transition->Index);
        AppendNativePath(Out, Member);
    }
    else if (Member.OwnerKind == TEXT("node"))
    {
        FGuid Id;
        FNodeRef* Node = ParseGuid(Member.OwnerId, Id) ? Index.FindNode(Id) : nullptr;
        if (Node == nullptr || Node->Node == nullptr)
        {
            OutError = TEXT("Node member owner is missing or ambiguous.");
            return false;
        }
        if (Node->OwnerState != nullptr)
        {
            Out.Owner = Node->OwnerState;
            Node->OwnerState->Modify();
            if (Node->OwnerTransition != nullptr)
            {
                const int32 TransitionIndex = Node->OwnerState->Transitions.IndexOfByPredicate(
                    [&](const FStateTreeTransition& Item)
                    {
                        return &Item == Node->OwnerTransition;
                    });
                AppendProperty(
                    Out,
                    UStateTreeState::StaticClass()->FindPropertyByName(
                        GET_MEMBER_NAME_CHECKED(UStateTreeState, Transitions)),
                    TransitionIndex);
                AppendProperty(
                    Out,
                    FStateTreeTransition::StaticStruct()->FindPropertyByName(
                        GET_MEMBER_NAME_CHECKED(FStateTreeTransition, Conditions)),
                    Node->Index);
            }
            else
            {
                AppendProperty(
                    Out,
                    UStateTreeState::StaticClass()->FindPropertyByName(FName(NodeRoleMember(Node->Role))),
                    Node->Role == ENodeRole::SingleTask ? INDEX_NONE : Node->Index);
            }
        }
        else
        {
            Out.Owner = &Context.Data;
            Context.Data.Modify();
            if (Node->bPropertyFunction)
            {
                if (Node->BindingIndex == INDEX_NONE
                    || !Context.Data.EditorBindings.GetBindings().IsValidIndex(Node->BindingIndex))
                {
                    OutError = TEXT("Property Function binding owner is missing or ambiguous.");
                    return false;
                }
                AppendProperty(
                    Out,
                    UStateTreeEditorData::StaticClass()->FindPropertyByName(
                        GET_MEMBER_NAME_CHECKED(UStateTreeEditorData, EditorBindings)));
                AppendProperty(
                    Out,
                    FStateTreeEditorPropertyBindings::StaticStruct()->FindPropertyByName(
                        FName(TEXT("PropertyBindings"))),
                    Node->BindingIndex);
                AppendProperty(
                    Out,
                    FStateTreePropertyPathBinding::StaticStruct()->FindPropertyByName(
                        FName(TEXT("PropertyFunctionNode"))));
            }
            else
            {
                AppendProperty(
                    Out,
                    UStateTreeEditorData::StaticClass()->FindPropertyByName(FName(NodeRoleMember(Node->Role))),
                    Node->Index);
            }
        }
        const bool bNodeSurface = !Member.SalPath.IsEmpty() && Member.SalPath[0] == TEXT("Node");
        const FName SurfaceName = bNodeSurface
            ? GET_MEMBER_NAME_CHECKED(FStateTreeEditorNode, Node)
            : Node->Node->InstanceObject != nullptr
                ? GET_MEMBER_NAME_CHECKED(FStateTreeEditorNode, InstanceObject)
                : GET_MEMBER_NAME_CHECKED(FStateTreeEditorNode, Instance);
        AppendProperty(
            Out,
            FStateTreeEditorNode::StaticStruct()->FindPropertyByName(SurfaceName));
        AppendNativePath(Out, Member);
        if (Node->Node->InstanceObject != nullptr)
        {
            Node->Node->InstanceObject->Modify();
        }
    }
    else if (Member.OwnerKind == TEXT("parameter"))
    {
        FParameterRef* Parameter = Index.FindParameter(Member.OwnerId);
        if (Parameter == nullptr)
        {
            OutError = TEXT("Parameter member owner is missing or ambiguous.");
            return false;
        }
        if (Parameter->OwnerState != nullptr)
        {
            Out.Owner = Parameter->OwnerState;
            Parameter->OwnerState->Modify();
            AppendProperty(
                Out,
                UStateTreeState::StaticClass()->FindPropertyByName(
                    GET_MEMBER_NAME_CHECKED(UStateTreeState, Parameters)));
        }
        else
        {
            Out.Owner = &Context.Data;
            Context.Data.Modify();
            AppendProperty(
                Out,
                UStateTreeEditorData::StaticClass()->FindPropertyByName(
                    FName(TEXT("RootParameterPropertyBag"))));
        }
    }
    else
    {
        OutError = TEXT("This StateTree member owner is read-only or cannot be notified atomically.");
        return false;
    }
    if (Out.Chain.GetHead() != nullptr)
    {
        Out.Chain.SetActiveMemberPropertyNode(Out.Chain.GetHead()->GetValue());
        Out.Chain.SetActivePropertyNode(Out.Chain.GetTail()->GetValue());
    }
    return true;
}

bool ImportMemberValue(
    const StateTreeSchema::FResolvedMember& Member,
    const TSharedPtr<FJsonValue>& Value,
    UObject* Owner,
    FString& OutError)
{
    TArray<FPropertyBindingPathIndirection, TInlineAllocator<16>> Indirections;
    if (!Member.NativePath.ResolveIndirectionsWithValue(
            Member.OwnerView,
            Indirections,
            &OutError,
            true)
        || Indirections.IsEmpty())
    {
        if (OutError.IsEmpty())
        {
            OutError = TEXT("Native StateTree member address cannot be resolved.");
        }
        return false;
    }
    const FPropertyBindingPathIndirection& Leaf = Indirections.Last();
    FProperty* Property = const_cast<FProperty*>(Leaf.GetProperty());
    FString ConversionError;
    const FString Text = NativeExpressionText(Value, Property, ConversionError);
    if (!ConversionError.IsEmpty())
    {
        OutError = ConversionError;
        return false;
    }
    const TCHAR* End = Property->ImportText_Direct(
        *Text,
        Leaf.GetMutablePropertyAddress(),
        Owner,
        PPF_None,
        GLog);
    if (End == nullptr)
    {
        OutError = FString::Printf(TEXT("UE could not import native value for %s."), *Property->GetName());
        return false;
    }
    while (*End != TEXT('\0') && FChar::IsWhitespace(*End))
    {
        ++End;
    }
    if (*End != TEXT('\0'))
    {
        OutError = FString::Printf(TEXT("Native value for %s contains unconsumed text."), *Property->GetName());
        return false;
    }
    return true;
}

enum class ESemanticSetResult : uint8
{
    NotHandled,
    Succeeded,
    Failed,
};

bool SynchronizeStateLinkNames(
    FPatchContext& Context,
    const FGuid& RenamedId,
    const FName NewName)
{
    Context.Data.VisitHierarchy(
        [&](UStateTreeState& State, UStateTreeState*)
        {
            if (State.LinkedSubtree.ID == RenamedId && State.LinkedSubtree.Name != NewName)
            {
                State.Modify();
                State.LinkedSubtree.Name = NewName;
                Context.PlannedEffects.Add(MakeShared<FJsonValueString>(
                    TEXT("updated ")
                    + Context.PublicRef(TEXT("state"), GuidText(State.ID))
                    + TEXT(".LinkedSubtree.Name")));
            }
            for (FStateTreeTransition& Transition : State.Transitions)
            {
                if (Transition.State.ID == RenamedId && Transition.State.Name != NewName)
                {
                    State.Modify();
                    Transition.State.Name = NewName;
                    Context.PlannedEffects.Add(MakeShared<FJsonValueString>(
                        TEXT("updated ")
                        + Context.PublicRef(TEXT("transition"), GuidText(Transition.ID))
                        + TEXT(".State.Name")));
                }
            }
            return EStateTreeVisitor::Continue;
        });
    return true;
}

bool ResolveStateValue(
    FPatchContext& Context,
    const TSharedPtr<FJsonObject>& Ref,
    UStateTreeState*& OutState,
    FString& OutError)
{
    OutState = nullptr;
    FString Kind;
    FString Id;
    if (!Context.ResolveRef(Ref, Kind, Id, OutError) || Kind != TEXT("state"))
    {
        if (OutError.IsEmpty())
        {
            OutError = TEXT("StateTree link requires one exact state@id relationship.");
        }
        return false;
    }
    FGuid Guid;
    FAuthoredIndex Index = Context.MakeIndex();
    FStateRef* State = Index.IsValid() && ParseGuid(Id, Guid) ? Index.FindState(Guid) : nullptr;
    if (!Index.IsValid() || State == nullptr)
    {
        OutError = Index.IsValid()
            ? TEXT("StateTree link target is missing or ambiguous.")
            : Index.GetError();
        return false;
    }
    OutState = State->State;
    return true;
}

bool ImportStateLinkValue(
    FPatchContext& Context,
    const StateTreeSchema::FResolvedMember& Member,
    const TSharedPtr<FJsonValue>& Value,
    FStateTreeStateLink& OutLink,
    UStateTreeState*& OutTargetState,
    FString& OutError)
{
    OutLink = FStateTreeStateLink();
    OutTargetState = nullptr;
    const TSharedPtr<FJsonObject>* Object = nullptr;
    if (!Value.IsValid() || !Value->TryGetObject(Object) || Object == nullptr || !(*Object).IsValid())
    {
        OutError = TEXT("StateTree State Link requires a native link object or one exact state@id reference.");
        return false;
    }
    FString Kind;
    (*Object)->TryGetStringField(TEXT("kind"), Kind);
    if (!Kind.IsEmpty())
    {
        if (!ResolveStateValue(Context, *Object, OutTargetState, OutError))
        {
            return false;
        }
        OutLink = OutTargetState->GetLinkToState();
        return true;
    }

    TSharedPtr<FJsonObject> NativeObject = MakeShared<FJsonObject>();
    NativeObject->Values = (*Object)->Values;
    if (const TSharedPtr<FJsonValue>* IdValue = NativeObject->Values.Find(TEXT("ID")))
    {
        const TSharedPtr<FJsonObject>* IdRef = nullptr;
        if (!(*IdValue).IsValid()
            || !(*IdValue)->TryGetObject(IdRef)
            || IdRef == nullptr
            || !ResolveStateValue(Context, *IdRef, OutTargetState, OutError))
        {
            if (OutError.IsEmpty())
            {
                OutError = TEXT("StateTree State Link ID must be one exact state@id relationship.");
            }
            return false;
        }
        NativeObject->SetStringField(TEXT("ID"), GuidText(OutTargetState->ID));
    }

    const FStructProperty* LinkProperty = CastField<FStructProperty>(Member.LeafProperty);
    if (LinkProperty == nullptr || LinkProperty->Struct != FStateTreeStateLink::StaticStruct())
    {
        OutError = TEXT("Resolved StateTree link member is not FStateTreeStateLink.");
        return false;
    }
    FString ConversionError;
    const FString Text = NativeExpressionText(
        MakeShared<FJsonValueObject>(NativeObject),
        LinkProperty,
        ConversionError);
    if (!ConversionError.IsEmpty())
    {
        OutError = ConversionError;
        return false;
    }
    const TCHAR* End = LinkProperty->ImportText_Direct(
        *Text,
        &OutLink,
        &Context.Tree,
        PPF_None,
        GLog);
    if (End == nullptr)
    {
        OutError = TEXT("UE could not import the native FStateTreeStateLink value.");
        return false;
    }
    while (*End != TEXT('\0') && FChar::IsWhitespace(*End))
    {
        ++End;
    }
    if (*End != TEXT('\0'))
    {
        OutError = TEXT("Native FStateTreeStateLink value contains unconsumed text.");
        return false;
    }
    if (OutTargetState != nullptr)
    {
        if (OutLink.LinkType != EStateTreeTransitionType::GotoState)
        {
            OutError = TEXT("A concrete state@id relationship requires native LinkType GotoState.");
            return false;
        }
        OutLink.ID = OutTargetState->ID;
        OutLink.Name = OutTargetState->Name;
    }
    return true;
}

ESemanticSetResult ApplyStateTreeSemanticSet(
    FPatchContext& Context,
    const StateTreeSchema::FResolvedMember& Member,
    const TSharedPtr<FJsonValue>& Value,
    FString& OutError)
{
    if (Member.OwnerKind == TEXT("state")
        && Member.SalPath.Num() == 1
        && Member.SalPath[0] == TEXT("Name"))
    {
        FGuid StateId;
        FAuthoredIndex Index = Context.MakeIndex();
        FStateRef* Owner = Index.IsValid() && ParseGuid(Member.OwnerId, StateId)
            ? Index.FindState(StateId)
            : nullptr;
        FName NewName;
        if (!Index.IsValid() || Owner == nullptr || !ReadNativeNameValue(Value, NewName))
        {
            OutError = !Index.IsValid()
                ? Index.GetError()
                : Owner == nullptr
                    ? TEXT("Renamed State owner is missing or ambiguous.")
                    : TEXT("State Name requires one non-empty native FName value.");
            return ESemanticSetResult::Failed;
        }
        Owner->State->Name = NewName;
        SynchronizeStateLinkNames(Context, Owner->State->ID, NewName);
        return ESemanticSetResult::Succeeded;
    }
    if (Member.OwnerKind == TEXT("state")
        && Member.SalPath.Num() == 1
        && Member.SalPath[0] == TEXT("LinkedSubtree"))
    {
        FGuid StateId;
        FAuthoredIndex Index = Context.MakeIndex();
        FStateRef* Owner = Index.IsValid() && ParseGuid(Member.OwnerId, StateId)
            ? Index.FindState(StateId)
            : nullptr;
        if (!Index.IsValid() || Owner == nullptr)
        {
            OutError = Index.IsValid() ? TEXT("Linked State owner is missing or ambiguous.") : Index.GetError();
            return ESemanticSetResult::Failed;
        }
        if (Owner->State->Type != EStateTreeStateType::Linked)
        {
            OutError = TEXT("Set State.Type to Linked before assigning LinkedSubtree.");
            return ESemanticSetResult::Failed;
        }
        FStateTreeStateLink Link;
        UStateTreeState* TargetState = nullptr;
        if (!ImportStateLinkValue(Context, Member, Value, Link, TargetState, OutError)
            || TargetState == nullptr)
        {
            if (OutError.IsEmpty())
            {
                OutError = TEXT("LinkedSubtree requires one concrete state@id relationship.");
            }
            return ESemanticSetResult::Failed;
        }
        Owner->State->SetLinkedState(Link);
        return ESemanticSetResult::Succeeded;
    }
    if (Member.OwnerKind == TEXT("state")
        && Member.SalPath.Num() == 1
        && Member.SalPath[0] == TEXT("LinkedAsset"))
    {
        FGuid StateId;
        FAuthoredIndex Index = Context.MakeIndex();
        FStateRef* Owner = Index.IsValid() && ParseGuid(Member.OwnerId, StateId)
            ? Index.FindState(StateId)
            : nullptr;
        if (!Index.IsValid() || Owner == nullptr)
        {
            OutError = Index.IsValid() ? TEXT("Linked Asset State owner is missing or ambiguous.") : Index.GetError();
            return ESemanticSetResult::Failed;
        }
        if (Owner->State->Type != EStateTreeStateType::LinkedAsset)
        {
            OutError = TEXT("Set State.Type to LinkedAsset before assigning LinkedAsset.");
            return ESemanticSetResult::Failed;
        }
        FString Path = NativeExpressionText(Value, Member.LeafProperty, OutError);
        if (!OutError.IsEmpty() || Path.IsEmpty() || Path == TEXT("None"))
        {
            if (OutError.IsEmpty())
            {
                OutError = TEXT("LinkedAsset requires one exact native UStateTree asset path.");
            }
            return ESemanticSetResult::Failed;
        }
        Path.TrimQuotesInline();
        UStateTree* LinkedAsset = Cast<UStateTree>(StaticLoadObject(UStateTree::StaticClass(), nullptr, *Path));
        if (LinkedAsset == nullptr)
        {
            OutError = FString::Printf(TEXT("Linked StateTree Asset %s could not be resolved."), *Path);
            return ESemanticSetResult::Failed;
        }
        Owner->State->SetLinkedStateAsset(LinkedAsset);
        return ESemanticSetResult::Succeeded;
    }
    if (Member.OwnerKind == TEXT("transition")
        && Member.SalPath.Num() == 1
        && Member.SalPath[0] == TEXT("State"))
    {
        FGuid TransitionId;
        FAuthoredIndex Index = Context.MakeIndex();
        FTransitionRef* Owner = Index.IsValid() && ParseGuid(Member.OwnerId, TransitionId)
            ? Index.FindTransition(TransitionId)
            : nullptr;
        if (!Index.IsValid() || Owner == nullptr)
        {
            OutError = Index.IsValid() ? TEXT("Transition owner is missing or ambiguous.") : Index.GetError();
            return ESemanticSetResult::Failed;
        }
        FStateTreeStateLink Link;
        UStateTreeState* TargetState = nullptr;
        if (!ImportStateLinkValue(Context, Member, Value, Link, TargetState, OutError))
        {
            return ESemanticSetResult::Failed;
        }
        Owner->Transition->State = MoveTemp(Link);
        return ESemanticSetResult::Succeeded;
    }
    return ESemanticSetResult::NotHandled;
}

bool ResetMemberValue(
    const StateTreeSchema::FResolvedMember& Member,
    FString& OutError)
{
    TArray<FPropertyBindingPathIndirection, TInlineAllocator<16>> Current;
    if (!Member.NativePath.ResolveIndirectionsWithValue(
            Member.OwnerView,
            Current,
            &OutError,
            true)
        || Current.IsEmpty())
    {
        if (OutError.IsEmpty())
        {
            OutError = TEXT("Native StateTree reset target cannot be resolved.");
        }
        return false;
    }

    const UStruct* Struct = Member.OwnerView.GetStruct();
    FPropertyBindingDataView DefaultView;
    TUniquePtr<FStructOnScope> StructDefault;
    if (const UClass* Class = Cast<UClass>(Struct))
    {
        UObject* Object = static_cast<UObject*>(Member.OwnerView.GetMutableMemory());
        UObject* Archetype = Object != nullptr ? Object->GetArchetype() : nullptr;
        if (Archetype == nullptr)
        {
            Archetype = Class->GetDefaultObject();
        }
        DefaultView = FPropertyBindingDataView(Archetype);
    }
    else if (const UScriptStruct* ScriptStruct = Cast<UScriptStruct>(Struct))
    {
        StructDefault = MakeUnique<FStructOnScope>(ScriptStruct);
        DefaultView = FPropertyBindingDataView(ScriptStruct, StructDefault->GetStructMemory());
    }
    if (!DefaultView.IsValid())
    {
        OutError = TEXT("UE native default storage is unavailable for this member.");
        return false;
    }
    FPropertyBindingPath DefaultPath = Member.NativePath;
    TArray<FPropertyBindingPathIndirection, TInlineAllocator<16>> Defaults;
    if (!DefaultPath.UpdateSegmentsFromValue(DefaultView, &OutError)
        || !DefaultPath.ResolveIndirectionsWithValue(DefaultView, Defaults, &OutError, true)
        || Defaults.IsEmpty()
        || Defaults.Last().GetProperty() != Current.Last().GetProperty())
    {
        if (OutError.IsEmpty())
        {
            OutError = TEXT("UE native default does not contain the exact current member path.");
        }
        return false;
    }
    FProperty* Property = const_cast<FProperty*>(Current.Last().GetProperty());
    Property->CopySingleValue(
        Current.Last().GetMutablePropertyAddress(),
        Defaults.Last().GetPropertyAddress());
    return true;
}

ESemanticSetResult ApplyStateTreeSemanticReset(
    FPatchContext& Context,
    const StateTreeSchema::FResolvedMember& Member,
    FString& OutError)
{
    if (Member.OwnerKind != TEXT("state") || Member.SalPath.Num() != 1)
    {
        return ESemanticSetResult::NotHandled;
    }

    FGuid StateId;
    FAuthoredIndex Index = Context.MakeIndex();
    FStateRef* Owner = Index.IsValid() && ParseGuid(Member.OwnerId, StateId)
        ? Index.FindState(StateId)
        : nullptr;
    if (!Index.IsValid() || Owner == nullptr)
    {
        OutError = Index.IsValid()
            ? TEXT("State reset owner is missing or ambiguous.")
            : Index.GetError();
        return ESemanticSetResult::Failed;
    }

    const FString& Root = Member.SalPath[0];
    if (Root == TEXT("Name"))
    {
        if (!ResetMemberValue(Member, OutError))
        {
            return ESemanticSetResult::Failed;
        }
        SynchronizeStateLinkNames(Context, Owner->State->ID, Owner->State->Name);
        return ESemanticSetResult::Succeeded;
    }
    if (Root == TEXT("LinkedSubtree"))
    {
        if (Owner->State->Type != EStateTreeStateType::Linked)
        {
            OutError = TEXT("LinkedSubtree can only be reset while State.Type is Linked.");
            return ESemanticSetResult::Failed;
        }
        Owner->State->SetLinkedState(FStateTreeStateLink());
        return ESemanticSetResult::Succeeded;
    }
    if (Root == TEXT("LinkedAsset"))
    {
        if (Owner->State->Type != EStateTreeStateType::LinkedAsset)
        {
            OutError = TEXT("LinkedAsset can only be reset while State.Type is LinkedAsset.");
            return ESemanticSetResult::Failed;
        }
        Owner->State->SetLinkedStateAsset(nullptr);
        return ESemanticSetResult::Succeeded;
    }
    return ESemanticSetResult::NotHandled;
}

bool ApplyParameterDescriptorSet(
    FPatchContext& Context,
    const StateTreeSchema::FResolvedMember& Member,
    const TSharedPtr<FJsonValue>& Value,
    FString& OutError)
{
    FAuthoredIndex Index = Context.MakeIndex();
    FParameterRef* Parameter = Index.IsValid() ? Index.FindParameter(Member.OwnerId) : nullptr;
    if (!Index.IsValid() || Parameter == nullptr || Parameter->Bag == nullptr)
    {
        OutError = Index.IsValid()
            ? TEXT("Parameter descriptor owner is missing or ambiguous.")
            : Index.GetError();
        return false;
    }
    if (Parameter->bFixedLayout || !Member.bLayoutEditable)
    {
        OutError = TEXT("This Parameter belongs to a fixed layout and its descriptor cannot be edited.");
        return false;
    }
    UObject* Owner = Parameter->OwnerState != nullptr
        ? static_cast<UObject*>(Parameter->OwnerState)
        : static_cast<UObject*>(&Context.Data);
    const bool bRoot = Parameter->bRoot;
    FPropertyBagPropertyDesc NewDesc = Parameter->Desc;
    EPropertyChangeType::Type ChangeType = EPropertyChangeType::ValueSet;
    TFunction<bool()> Change;
    switch (Member.Surface)
    {
    case StateTreeSchema::EMemberSurface::ParameterName:
    {
        FName NewName;
        if (!ReadNativeNameValue(Value, NewName))
        {
            OutError = TEXT("Parameter Name requires one non-empty native FName value.");
            return false;
        }
        const FName OldName = Parameter->Desc.Name;
        Change = [&, OldName, NewName]()
        {
            const EPropertyBagAlterationResult Result = Parameter->Bag->RenameProperty(OldName, NewName);
            if (Result != EPropertyBagAlterationResult::Success)
            {
                OutError = FString::Printf(TEXT("UE rejected Parameter rename with Property Bag result %d."), static_cast<int32>(Result));
                return false;
            }
            return true;
        };
        break;
    }
    case StateTreeSchema::EMemberSurface::ParameterType:
    {
        FString TypeText;
        if (!Value.IsValid() || !Value->TryGetString(TypeText))
        {
            OutError = TEXT("Parameter type requires one NativePropertyTypeText string.");
            return false;
        }
        FParameterType Type;
        if (!ParseParameterType(TypeText, Type, OutError))
        {
            return false;
        }
        NewDesc.ValueType = Type.ValueType;
        NewDesc.ValueTypeObject = Type.ValueTypeObject;
        NewDesc.ContainerTypes = Type.Containers;
        Change = [&, NewDesc]()
        {
            const EPropertyBagAlterationResult Result = Parameter->Bag->AddProperties({NewDesc}, true);
            if (Result != EPropertyBagAlterationResult::Success)
            {
                OutError = FString::Printf(TEXT("UE rejected Parameter type change with Property Bag result %d."), static_cast<int32>(Result));
                return false;
            }
            return true;
        };
        break;
    }
    case StateTreeSchema::EMemberSurface::ParameterMetaData:
    {
        if (!ReadParameterMetaData(Value, NewDesc.MetaData, OutError))
        {
            return false;
        }
        Change = [&, NewDesc]()
        {
            const EPropertyBagAlterationResult Result = Parameter->Bag->AddProperties({NewDesc}, true);
            if (Result != EPropertyBagAlterationResult::Success)
            {
                OutError = FString::Printf(TEXT("UE rejected Parameter metadata change with Property Bag result %d."), static_cast<int32>(Result));
                return false;
            }
            return true;
        };
        break;
    }
    default:
        OutError = TEXT("The resolved Parameter member is not a descriptor-edit surface.");
        return false;
    }
    return ApplyParameterLayoutChange(*Owner, bRoot, ChangeType, Change, OutError);
}

template<typename EnumType>
bool ReadNativeEnumCandidate(
    const TSharedPtr<FJsonValue>& Value,
    const FProperty* Property,
    const EnumType ResetDefault,
    const bool bReset,
    EnumType& OutValue,
    FString& OutError)
{
    if (bReset)
    {
        OutValue = ResetDefault;
        return true;
    }
    const UEnum* Enum = StaticEnum<EnumType>();
    if (Enum == nullptr)
    {
        OutError = TEXT("UE native enum metadata is unavailable.");
        return false;
    }
    FString Text = NativeExpressionText(Value, Property, OutError);
    if (!OutError.IsEmpty())
    {
        return false;
    }
    Text.TrimStartAndEndInline();
    int64 Numeric = Enum->GetValueByNameString(Text);
    if (Numeric == INDEX_NONE)
    {
        Numeric = Enum->GetValueByName(FName(*Text));
    }
    if (Numeric == INDEX_NONE || !Enum->IsValidEnumValue(Numeric))
    {
        OutError = FString::Printf(TEXT("%s is not a valid native enum value."), *Text);
        return false;
    }
    OutValue = static_cast<EnumType>(Numeric);
    return true;
}

bool ValidateSchemaMemberEdit(
    FPatchContext& Context,
    const StateTreeSchema::FResolvedMember& Member,
    const TSharedPtr<FJsonValue>& Value,
    const bool bReset,
    FString& OutError)
{
    const UStateTreeSchema* Schema = Context.Data.Schema;
    if (Schema == nullptr || Member.SalPath.IsEmpty())
    {
        OutError = Schema == nullptr
            ? TEXT("StateTree Schema is unavailable for this authored edit.")
            : TEXT("StateTree authored edit requires one exact native field.");
        return false;
    }
    const FString& Root = Member.SalPath[0];
    if (Member.OwnerKind == TEXT("target") && Root == TEXT("GlobalTasksCompletion"))
    {
        if (!Schema->AllowTasksCompletion())
        {
            OutError = TEXT("The current StateTree Schema does not allow Global Tasks completion edits.");
            return false;
        }
        return true;
    }
    if (Member.OwnerKind != TEXT("state"))
    {
        return true;
    }
    if (Root == TEXT("TasksCompletion") && !Schema->AllowTasksCompletion())
    {
        OutError = TEXT("The current StateTree Schema does not allow State Tasks completion edits.");
        return false;
    }
    if ((Root == TEXT("CustomTickRate") || Root == TEXT("bHasCustomTickRate"))
        && !Schema->IsScheduledTickAllowed())
    {
        OutError = TEXT("The current StateTree Schema does not allow scheduled State ticking.");
        return false;
    }
    if (Root == TEXT("Type"))
    {
        EStateTreeStateType Candidate = EStateTreeStateType::State;
        const UStateTreeState* Defaults = GetDefault<UStateTreeState>();
        if (!ReadNativeEnumCandidate(
                Value,
                Member.LeafProperty,
                Defaults->Type,
                bReset,
                Candidate,
                OutError))
        {
            return false;
        }
        if (!Schema->IsStateTypeAllowed(Candidate))
        {
            OutError = TEXT("The current StateTree Schema rejects this State Type.");
            return false;
        }
    }
    else if (Root == TEXT("SelectionBehavior"))
    {
        EStateTreeStateSelectionBehavior Candidate = EStateTreeStateSelectionBehavior::TrySelectChildrenInOrder;
        const UStateTreeState* Defaults = GetDefault<UStateTreeState>();
        if (!ReadNativeEnumCandidate(
                Value,
                Member.LeafProperty,
                Defaults->SelectionBehavior,
                bReset,
                Candidate,
                OutError))
        {
            return false;
        }
        if (!Schema->IsStateSelectionAllowed(Candidate))
        {
            OutError = TEXT("The current StateTree Schema rejects this State SelectionBehavior.");
            return false;
        }
    }
    return true;
}

struct FSetSemanticTracker;

TSharedPtr<FSetSemanticTracker> BeginSetSemanticTracking(
    FPatchContext& Context,
    const StateTreeSchema::FResolvedMember& Member);

bool FinishSetSemanticTracking(
    FPatchContext& Context,
    const StateTreeSchema::FResolvedMember& Member,
    const TSharedPtr<FSetSemanticTracker>& Tracker,
    FString& OutError);

bool CompletePostEditNotification(
    FPatchContext& Context,
    FChangeNotification& Notification,
    const StateTreeSchema::FResolvedMember& Member,
    FString& OutError);

bool ApplySetReset(
    FPatchContext& Context,
    const TSharedPtr<FJsonObject>& Member,
    const TSharedPtr<FJsonValue>& Value,
    const bool bReset,
    const FString& OperationName,
    const bool bRecordOperation)
{
    FString Error;
    StateTreeSchema::FResolvedMember Resolved;
    const TSharedPtr<FJsonObject> Rebound = Context.RebindRef(Member, Error);
    if (!Rebound.IsValid()
        || !StateTreeSchema::ResolveMemberReference(
            Context.Tree,
            Context.Data,
            Rebound,
            Resolved,
            Error))
    {
        return Context.Fail(TEXT("resolution.property_not_found"), Error, OperationName);
    }
    if ((bReset && !Resolved.bResettable) || (!bReset && !Resolved.bWritable))
    {
        return Context.Fail(
            TEXT("capability.operation_unavailable"),
            TEXT("The exact StateTree schema marks this member read-only for the requested operation."),
            OperationName,
            Resolved.OwnerKind + TEXT("@") + Resolved.OwnerId);
    }
    if (!ValidateSchemaMemberEdit(Context, Resolved, Value, bReset, Error))
    {
        return Context.Fail(
            TEXT("validation.operation_arguments_invalid"),
            Error,
            OperationName,
            Resolved.OwnerKind + TEXT("@") + Resolved.OwnerId);
    }
    if (Resolved.Surface == StateTreeSchema::EMemberSurface::ParameterName
        || Resolved.Surface == StateTreeSchema::EMemberSurface::ParameterType
        || Resolved.Surface == StateTreeSchema::EMemberSurface::ParameterMetaData)
    {
        if (bReset)
        {
            return Context.Fail(
                TEXT("capability.operation_unavailable"),
                TEXT("Parameter descriptor fields have no implicit reset default."),
                OperationName,
                Resolved.OwnerKind + TEXT("@") + Resolved.OwnerId);
        }
        Context.Tree.Modify();
        Context.Data.Modify();
        if (!ApplyParameterDescriptorSet(Context, Resolved, Value, Error))
        {
            return Context.Fail(TEXT("validation.operation_arguments_invalid"), Error, OperationName);
        }
        ++Context.ChangedOperations;
        TSharedPtr<FJsonObject> Planned = MakeShared<FJsonObject>();
        Planned->SetStringField(TEXT("kind"), TEXT("set"));
        Planned->SetStringField(TEXT("target"), Context.PublicRef(Resolved.OwnerKind, Resolved.OwnerId));
        Context.PlannedOperations.Add(MakeShared<FJsonValueObject>(Planned));
        Context.PlannedEffects.Add(MakeShared<FJsonValueString>(
            TEXT("set Parameter descriptor ") + Context.PublicId(Resolved.OwnerKind, Resolved.OwnerId)));
        return true;
    }
    FChangeNotification Notification;
    if (!BuildNotification(Context, Resolved, Notification, Error))
    {
        return Context.Fail(TEXT("validation.patch_state_invalid"), Error, OperationName);
    }
    Context.Tree.Modify();
    Context.Data.Modify();
    const TSharedPtr<FSetSemanticTracker> SemanticTracker = BeginSetSemanticTracking(
        Context,
        Resolved);

    FAuthoredIndex Index = Context.MakeIndex();
    FParameterRef* Parameter = Resolved.OwnerKind == TEXT("parameter") && Index.IsValid()
        ? Index.FindParameter(Resolved.OwnerId)
        : nullptr;
    if (Parameter != nullptr && Parameter->bFixedLayout && Parameter->OwnerState != nullptr)
    {
        if (bReset)
        {
            Notification.Before();
            Parameter->OwnerState->SetParametersPropertyOverridden(Parameter->Desc.ID, false);
        }
        else
        {
            Notification.Before();
            if (!ImportMemberValue(Resolved, Value, Notification.Owner, Error))
            {
                Notification.After();
                return Context.Fail(TEXT("validation.operation_arguments_invalid"), Error, OperationName);
            }
            Parameter->OwnerState->SetParametersPropertyOverridden(Parameter->Desc.ID, true);
        }
    }
    else
    {
        Notification.Before();
        bool bSucceeded = false;
        if (bReset)
        {
            const ESemanticSetResult Semantic = ApplyStateTreeSemanticReset(
                Context,
                Resolved,
                Error);
            bSucceeded = Semantic == ESemanticSetResult::Succeeded
                || (Semantic == ESemanticSetResult::NotHandled
                    && ResetMemberValue(Resolved, Error));
        }
        else
        {
            const ESemanticSetResult Semantic = ApplyStateTreeSemanticSet(
                Context,
                Resolved,
                Value,
                Error);
            bSucceeded = Semantic == ESemanticSetResult::Succeeded
                || (Semantic == ESemanticSetResult::NotHandled
                    && ImportMemberValue(Resolved, Value, Notification.Owner, Error));
        }
        if (!bSucceeded)
        {
            Notification.After();
            return Context.Fail(TEXT("validation.operation_arguments_invalid"), Error, OperationName);
        }
    }
    if (!CompletePostEditNotification(Context, Notification, Resolved, Error))
    {
        return Context.Fail(
            TEXT("validation.patch_state_invalid"),
            Error,
            OperationName,
            Resolved.OwnerKind + TEXT("@") + Resolved.OwnerId);
    }
    if (!FinishSetSemanticTracking(Context, Resolved, SemanticTracker, Error))
    {
        return Context.Fail(
            TEXT("validation.patch_state_invalid"),
            Error,
            OperationName,
            Resolved.OwnerKind + TEXT("@") + Resolved.OwnerId);
    }

    if (bRecordOperation)
    {
        ++Context.ChangedOperations;
        TSharedPtr<FJsonObject> Planned = MakeShared<FJsonObject>();
        Planned->SetStringField(TEXT("kind"), bReset ? TEXT("reset") : TEXT("set"));
        Planned->SetStringField(TEXT("target"), Context.PublicRef(Resolved.OwnerKind, Resolved.OwnerId));
        Context.PlannedOperations.Add(MakeShared<FJsonValueObject>(Planned));
        Context.PlannedEffects.Add(MakeShared<FJsonValueString>(
            FString(bReset ? TEXT("reset ") : TEXT("set "))
            + Context.PublicRef(Resolved.OwnerKind, Resolved.OwnerId)));
    }
    return true;
}

bool HandleSetReset(
    FPatchContext& Context,
    const TSharedPtr<FJsonObject>& Operation,
    const bool bReset)
{
    const TSharedPtr<FJsonObject>* Target = nullptr;
    if (!Operation->TryGetObjectField(TEXT("target"), Target) || Target == nullptr)
    {
        return Context.Fail(
            TEXT("language.expected_member"),
            TEXT("StateTree set/reset requires one exact Member Reference."),
            bReset ? TEXT("reset") : TEXT("set"));
    }
    return ApplySetReset(
        Context,
        *Target,
        Operation->TryGetField(TEXT("value")),
        bReset,
        bReset ? TEXT("reset") : TEXT("set"));
}

void AddLifecyclePlan(
    FPatchContext& Context,
    const FString& Kind,
    const FString& Target,
    const FString& Effect)
{
    ++Context.ChangedOperations;
    TSharedPtr<FJsonObject> Planned = MakeShared<FJsonObject>();
    Planned->SetStringField(TEXT("kind"), Kind);
    Planned->SetStringField(TEXT("target"), Target);
    Context.PlannedOperations.Add(MakeShared<FJsonValueObject>(Planned));
    Context.PlannedEffects.Add(MakeShared<FJsonValueString>(Effect));
}

int32 RemoveInvalidBindings(FPatchContext& Context)
{
    const int32 Before = Context.Data.EditorBindings.GetBindings().Num();
    TMap<FGuid, const FPropertyBindingDataView> Values;
    Context.Data.GetAllStructValues(Values);
    Context.Data.EditorBindings.RemoveInvalidBindings(Values);
    return Before - Context.Data.EditorBindings.GetBindings().Num();
}

FString AppendNativeMemberSegments(
    FString Prefix,
    const FPropertyBindingPath& Path,
    const int32 FirstSegment)
{
    const TConstArrayView<FPropertyBindingPathSegment> Segments = Path.GetSegments();
    for (int32 Index = FirstSegment; Index < Segments.Num(); ++Index)
    {
        Prefix += TEXT(".") + Segments[Index].GetName().ToString();
        if (Segments[Index].GetArrayIndex() != INDEX_NONE)
        {
            Prefix += FString::Printf(TEXT("[%d]"), Segments[Index].GetArrayIndex());
        }
    }
    return Prefix;
}

FString PublicBindingEndpoint(
    FPatchContext& Context,
    FAuthoredIndex& Index,
    const FPropertyBindingPath& Path)
{
    const FGuid StructId = Path.GetStructID();
    if (FNodeRef* Node = Index.FindNodeByNativeStructId(StructId))
    {
        FString Prefix = Context.PublicRef(TEXT("node"), GuidText(Node->Node->ID));
        Prefix += Node->Node->ID == StructId ? TEXT(".Instance") : TEXT(".Node");
        return AppendNativeMemberSegments(MoveTemp(Prefix), Path, 0);
    }
    if (FStateRef* State = Index.FindState(StructId))
    {
        return AppendNativeMemberSegments(
            Context.PublicRef(TEXT("state"), GuidText(State->State->ID)),
            Path,
            0);
    }
    if (FTransitionRef* Transition = Index.FindTransition(StructId))
    {
        return AppendNativeMemberSegments(
            Context.PublicRef(TEXT("transition"), GuidText(Transition->Transition->ID)),
            Path,
            0);
    }
    if (FParameterRef* Parameter = Index.FindParameterByNativePath(StructId, Path))
    {
        FString Prefix = Context.PublicRef(
            TEXT("parameter"),
            GuidText(Parameter->ContainerId) + TEXT("/") + GuidText(Parameter->Desc.ID));
        const TConstArrayView<FPropertyBindingPathSegment> Segments = Path.GetSegments();
        if (!Segments.IsEmpty() && Segments[0].GetArrayIndex() != INDEX_NONE)
        {
            Prefix += FString::Printf(TEXT("[%d]"), Segments[0].GetArrayIndex());
        }
        return AppendNativeMemberSegments(MoveTemp(Prefix), Path, 1);
    }

    FString EventEndpoint;
    Context.Data.VisitHierarchy(
        [&](UStateTreeState& State, UStateTreeState*)
        {
            if (State.GetEventID() == StructId)
            {
                EventEndpoint = Context.PublicRef(TEXT("state"), GuidText(State.ID))
                    + TEXT(".RequiredEventToEnter");
                return EStateTreeVisitor::Break;
            }
            for (FStateTreeTransition& Transition : State.Transitions)
            {
                if (Transition.GetEventID() == StructId)
                {
                    EventEndpoint = Context.PublicRef(TEXT("transition"), GuidText(Transition.ID))
                        + TEXT(".RequiredEvent");
                    return EStateTreeVisitor::Break;
                }
            }
            return EStateTreeVisitor::Continue;
        });
    if (!EventEndpoint.IsEmpty())
    {
        return AppendNativeMemberSegments(MoveTemp(EventEndpoint), Path, 0);
    }

    // Unknown ids are retained as native evidence rather than being promoted
    // to a fake SAL object kind. Restore duplicate ids so dry-run/live plans
    // remain byte-for-byte comparable.
    return AppendNativeMemberSegments(
        TEXT("native struct ") + GuidText(Context.Identities.RestoreNativeStruct(StructId)),
        Path,
        0);
}

bool IsBindingPathResolved(
    const TMap<FGuid, const FPropertyBindingDataView>& Values,
    const FPropertyBindingPath& Path,
    const bool bTarget)
{
    const FPropertyBindingDataView* View = Values.Find(Path.GetStructID());
    if (View == nullptr || !View->IsValid() || (bTarget && Path.IsPathEmpty()))
    {
        return false;
    }
    if (Path.IsPathEmpty())
    {
        return true;
    }
    FString Error;
    TArray<FPropertyBindingPathIndirection, TInlineAllocator<16>> Indirections;
    return Path.ResolveIndirectionsWithValue(*View, Indirections, &Error, true)
        && !Indirections.IsEmpty();
}

struct FBindingSnapshot
{
    FPropertyBindingPath Source;
    FPropertyBindingPath Target;
    FGuid PropertyFunctionId;
    bool bOutput = false;
    bool bSourceResolved = false;
    bool bTargetResolved = false;
    FString Evidence;
};

TArray<FBindingSnapshot> CaptureBindingSnapshot(FPatchContext& Context)
{
    TMap<FGuid, const FPropertyBindingDataView> Values;
    Context.Data.GetAllStructValues(Values);
    FAuthoredIndex Index = Context.MakeIndex();
    TArray<FBindingSnapshot> Result;
    if (!Index.IsValid())
    {
        return Result;
    }
    Result.Reserve(Context.Data.EditorBindings.GetBindings().Num());
    for (const FStateTreePropertyPathBinding& Binding : Context.Data.EditorBindings.GetBindings())
    {
        FBindingSnapshot& Item = Result.AddDefaulted_GetRef();
        Item.Source = Binding.GetSourcePath();
        Item.Target = Binding.GetTargetPath();
        Item.bOutput = Binding.IsOutputBinding();
        Item.bSourceResolved = IsBindingPathResolved(Values, Item.Source, false);
        Item.bTargetResolved = IsBindingPathResolved(Values, Item.Target, true);
        Item.Evidence = TEXT("native Binding source ")
            + PublicBindingEndpoint(Context, Index, Item.Source)
            + TEXT("; target ")
            + PublicBindingEndpoint(Context, Index, Item.Target)
            + (Item.bOutput ? TEXT("; mode output") : TEXT("; mode ordinary"));
        if (const FStateTreeEditorNode* Function =
                Binding.GetPropertyFunctionNode().GetPtr<const FStateTreeEditorNode>();
            Function != nullptr && Function->ID.IsValid())
        {
            Item.PropertyFunctionId = Context.Identities.RestoreNode(Function->ID);
            Item.Evidence += TEXT("; owning Property Function node@")
                + GuidText(Item.PropertyFunctionId);
        }
    }
    return Result;
}

const FBindingSnapshot* FindBindingSnapshot(
    const TArray<FBindingSnapshot>& Before,
    const FPropertyBindingPath& Source,
    const FPropertyBindingPath& Target,
    const bool bOutput)
{
    return Before.FindByPredicate(
        [&](const FBindingSnapshot& Item)
        {
            return Item.Source == Source
                && Item.Target == Target
                && Item.bOutput == bOutput;
        });
}

bool CleanupNewlyInvalidBindings(
    FPatchContext& Context,
    const TArray<FBindingSnapshot>& Before,
    TArray<FString>& OutRemovedEvidence)
{
    OutRemovedEvidence.Reset();
    const int32 MaxPasses = Before.Num() + 1;
    for (int32 Pass = 0; Pass < MaxPasses; ++Pass)
    {
        TMap<FGuid, const FPropertyBindingDataView> Values;
        Context.Data.GetAllStructValues(Values);
        TArray<TPair<FPropertyBindingPath, FPropertyBindingPath>> Remove;
        for (const FStateTreePropertyPathBinding& Binding : Context.Data.EditorBindings.GetBindings())
        {
            const FBindingSnapshot* Item = FindBindingSnapshot(
                Before,
                Binding.GetSourcePath(),
                Binding.GetTargetPath(),
                Binding.IsOutputBinding());
            if (Item == nullptr)
            {
                continue;
            }
            const bool bSourceLost = Item->bSourceResolved
                && !IsBindingPathResolved(Values, Binding.GetSourcePath(), false);
            const bool bTargetLost = Item->bTargetResolved
                && !IsBindingPathResolved(Values, Binding.GetTargetPath(), true);
            if (bSourceLost || bTargetLost)
            {
                Remove.Emplace(Binding.GetSourcePath(), Binding.GetTargetPath());
                OutRemovedEvidence.AddUnique(Item->Evidence);
            }
        }
        if (Remove.IsEmpty())
        {
            return true;
        }
        Context.Data.EditorBindings.RemoveBindings(
            [&](FPropertyBindingBinding& Binding)
            {
                const int32 Match = Remove.IndexOfByPredicate(
                    [&](const TPair<FPropertyBindingPath, FPropertyBindingPath>& Pair)
                    {
                        return Binding.GetSourcePath() == Pair.Key
                            && Binding.GetTargetPath() == Pair.Value;
                    });
                if (Match == INDEX_NONE)
                {
                    return false;
                }
                Remove.RemoveAtSwap(Match);
                return true;
            });
    }
    return false;
}

void AddRemovedBindingEffects(
    FPatchContext& Context,
    const TArray<FString>& RemovedEvidence)
{
    for (const FString& Evidence : RemovedEvidence)
    {
        Context.PlannedEffects.Add(MakeShared<FJsonValueString>(TEXT("removed ") + Evidence));
    }
}

TArray<FString> FindRemovedBindingEvidence(
    const UStateTreeEditorData& CurrentData,
    const TArray<FBindingSnapshot>& Before,
    const FIdentityMap& BeforeToCurrent);

TArray<FString> FindRemovedBindingEvidence(
    FPatchContext& Context,
    const TArray<FBindingSnapshot>& Before)
{
    const FIdentityMap Identity;
    return FindRemovedBindingEvidence(Context.Data, Before, Identity);
}

TArray<FString> FindRemovedBindingEvidence(
    const UStateTreeEditorData& CurrentData,
    const TArray<FBindingSnapshot>& Before,
    const FIdentityMap& BeforeToCurrent)
{
    const TConstArrayView<FStateTreePropertyPathBinding> Current =
        CurrentData.EditorBindings.GetBindings();
    TBitArray<> Matched(false, Current.Num());
    TArray<FString> Removed;
    for (const FBindingSnapshot& Previous : Before)
    {
        FPropertyBindingPath ExpectedSource = Previous.Source;
        FPropertyBindingPath ExpectedTarget = Previous.Target;
        ExpectedSource.SetStructID(
            BeforeToCurrent.ResolveNativeStruct(ExpectedSource.GetStructID()));
        ExpectedTarget.SetStructID(
            BeforeToCurrent.ResolveNativeStruct(ExpectedTarget.GetStructID()));
        int32 Match = INDEX_NONE;
        for (int32 Index = 0; Index < Current.Num(); ++Index)
        {
            if (!Matched[Index]
                && Current[Index].GetSourcePath() == ExpectedSource
                && Current[Index].GetTargetPath() == ExpectedTarget
                && Current[Index].IsOutputBinding() == Previous.bOutput)
            {
                Match = Index;
                break;
            }
        }
        if (Match != INDEX_NONE)
        {
            Matched[Match] = true;
        }
        else
        {
            Removed.Add(Previous.Evidence);
        }
    }
    return Removed;
}

struct FNodePostEditSnapshot
{
    FGuid StableId;
    FString Node;
    FString Instance;
    FString ExecutionRuntimeData;
};

struct FPostEditCascadeTracker
{
    uint32 AuthoredHash = 0;
    FGuid TransitionId;
    bool bTransitionDelay = false;
    TArray<FNodePostEditSnapshot> Nodes;
    TArray<FBindingSnapshot> Bindings;
};

struct FCompileManifestEntry
{
    FString Key;
    FString Owner;
    FString Role;
    int32 Order = INDEX_NONE;
    FString Content;
};

FString ManifestLocation(const FCompileManifestEntry& Entry)
{
    FString Result = Entry.Owner;
    if (!Entry.Role.IsEmpty())
    {
        Result += TEXT(".") + Entry.Role;
    }
    if (Entry.Order != INDEX_NONE)
    {
        Result += FString::Printf(TEXT("[%d]"), Entry.Order);
    }
    return Result;
}

bool CaptureCompileManifest(
    UStateTree& Tree,
    UStateTreeEditorData& Data,
    const FIdentityMap& Identities,
    TArray<FCompileManifestEntry>& OutManifest,
    FString& OutError);

void AddNativeCallbackManifestEffects(
    FPatchContext& Context,
    const FString& EffectPrefix,
    const TArray<FCompileManifestEntry>& Before,
    const TArray<FCompileManifestEntry>& After,
    const TSet<FString>& DetailedContentKeys,
    const TSet<FString>& DetailedRemovalKeys,
    bool& bOutChanged);

class FFramedSha1
{
public:
    void Add(const FStringView Type, const FStringView Value)
    {
        AddFrame(Type);
        AddFrame(Value);
    }

    FString Finalize()
    {
        Hash.Final();
        uint8 Digest[FSHA1::DigestSize];
        Hash.GetHash(Digest);
        return BytesToHex(Digest, UE_ARRAY_COUNT(Digest));
    }

private:
    void AddFrame(const FStringView Text)
    {
        const uint64 ByteLength = static_cast<uint64>(Text.Len()) * sizeof(TCHAR);
        uint8 Length[sizeof(uint64)];
        for (int32 Index = 0; Index < UE_ARRAY_COUNT(Length); ++Index)
        {
            Length[UE_ARRAY_COUNT(Length) - Index - 1]
                = static_cast<uint8>((ByteLength >> (Index * 8)) & 0xff);
        }
        Hash.Update(Length, UE_ARRAY_COUNT(Length));
        if (ByteLength > 0)
        {
            Hash.Update(
                reinterpret_cast<const uint8*>(Text.GetData()),
                ByteLength);
        }
    }

    FSHA1 Hash;
};

bool FingerprintNodeView(
    const FPropertyBindingDataView& View,
    int64& InOutBudget,
    FString& OutFingerprint,
    FString& OutError,
    const TSet<FName>* ExcludedProperties = nullptr)
{
    if (!View.IsValid())
    {
        OutFingerprint = TEXT("none");
        return true;
    }
    constexpr int64 MaxSnapshotText = 16 * 1024 * 1024;
    FFramedSha1 Hash;
    const UStruct* Struct = View.GetStruct();
    Hash.Add(TEXT("struct_type"), Struct->GetPathName());
    for (TFieldIterator<FProperty> It(Struct); It; ++It)
    {
        const FProperty* Property = *It;
        if (Property == nullptr
            || (ExcludedProperties != nullptr
                && ExcludedProperties->Contains(Property->GetFName()))
            || Property->HasAnyPropertyFlags(
                CPF_Transient | CPF_DuplicateTransient | CPF_NonPIEDuplicateTransient))
        {
            continue;
        }
        FString Text;
        const void* Address = Property->ContainerPtrToValuePtr<void>(View.GetMemory());
        Property->ExportTextItem_Direct(Text, Address, nullptr, nullptr, PPF_None);
        InOutBudget += Text.Len();
        if (InOutBudget > MaxSnapshotText)
        {
            OutError = TEXT("Native PostEdit authored snapshot exceeded its 16 MiB text budget.");
            return false;
        }
        Hash.Add(TEXT("property_name"), Property->GetName());
        Hash.Add(TEXT("property_value"), Text);
    }
    OutFingerprint = Hash.Finalize();
    return true;
}

bool CaptureNodePostEditSnapshots(
    FPatchContext& Context,
    TArray<FNodePostEditSnapshot>& OutNodes,
    FString& OutError)
{
    FAuthoredIndex Index = Context.MakeIndex();
    if (!Index.IsValid())
    {
        OutError = Index.GetError();
        return false;
    }
    TArray<FNodeRef*> Nodes;
    Index.GetAllNodes(Nodes);
    int64 Budget = 0;
    OutNodes.Reset();
    OutNodes.Reserve(Nodes.Num());
    for (const FNodeRef* Ref : Nodes)
    {
        if (Ref == nullptr || Ref->Node == nullptr)
        {
            OutError = TEXT("Native PostEdit authored snapshot encountered an unavailable Node.");
            return false;
        }
        FNodePostEditSnapshot& Snapshot = OutNodes.AddDefaulted_GetRef();
        Snapshot.StableId = Context.Identities.RestoreNode(Ref->Node->ID);
        const FPropertyBindingDataView NodeView(
            Ref->Node->Node.GetScriptStruct(),
            Ref->Node->Node.GetMutableMemory());
        if (!FingerprintNodeView(NodeView, Budget, Snapshot.Node, OutError)
            || !FingerprintNodeView(
                Ref->Node->GetInstance(),
                Budget,
                Snapshot.Instance,
                OutError)
            || !FingerprintNodeView(
                Ref->Node->GetExecutionRuntimeData(),
                Budget,
                Snapshot.ExecutionRuntimeData,
                OutError))
        {
            return false;
        }
    }
    OutNodes.Sort(
        [](const FNodePostEditSnapshot& Left, const FNodePostEditSnapshot& Right)
        {
            return GuidText(Left.StableId) < GuidText(Right.StableId);
        });
    return true;
}

TArray<FString> FindAddedBindingEvidence(
    const TArray<FBindingSnapshot>& Before,
    const TArray<FBindingSnapshot>& After)
{
    TBitArray<> Matched(false, Before.Num());
    TArray<FString> Added;
    for (const FBindingSnapshot& Current : After)
    {
        int32 Match = INDEX_NONE;
        for (int32 Index = 0; Index < Before.Num(); ++Index)
        {
            if (!Matched[Index]
                && Before[Index].Source == Current.Source
                && Before[Index].Target == Current.Target
                && Before[Index].bOutput == Current.bOutput)
            {
                Match = Index;
                break;
            }
        }
        if (Match == INDEX_NONE)
        {
            Added.Add(Current.Evidence);
        }
        else
        {
            Matched[Match] = true;
        }
    }
    return Added;
}

bool CompletePostEditNotification(
    FPatchContext& Context,
    FChangeNotification& Notification,
    const StateTreeSchema::FResolvedMember& Member,
    FString& OutError)
{
    FPostEditCascadeTracker Before;
    Before.AuthoredHash = UStateTreeEditingSubsystem::CalculateStateTreeHash(&Context.Tree);
    if (!CaptureNodePostEditSnapshots(Context, Before.Nodes, OutError))
    {
        Notification.After();
        return false;
    }
    Before.Bindings = CaptureBindingSnapshot(Context);
    TArray<FCompileManifestEntry> BeforeManifest;
    if (!CaptureCompileManifest(
            Context.Tree,
            Context.Data,
            Context.Identities,
            BeforeManifest,
            OutError))
    {
        Notification.After();
        return false;
    }
    const bool bStateTypeCascade = Member.OwnerKind == TEXT("state")
        && !Member.SalPath.IsEmpty()
        && Member.SalPath[0] == TEXT("Type");
    TSet<FGuid> StateTypeTaskIds;
    if (bStateTypeCascade)
    {
        FGuid RequestedId;
        FAuthoredIndex Index = Context.MakeIndex();
        FStateRef* State = Index.IsValid() && ParseGuid(Member.OwnerId, RequestedId)
            ? Index.FindState(RequestedId)
            : nullptr;
        if (!Index.IsValid() || State == nullptr)
        {
            OutError = Index.IsValid()
                ? TEXT("Native PostEdit snapshot could not resolve its State owner.")
                : Index.GetError();
            Notification.After();
            return false;
        }
        for (const FStateTreeEditorNode& Task : State->State->Tasks)
        {
            if (Task.ID.IsValid())
            {
                StateTypeTaskIds.Add(Context.Identities.RestoreNode(Task.ID));
            }
        }
    }
    if (Member.OwnerKind == TEXT("transition"))
    {
        FGuid RequestedId;
        FAuthoredIndex Index = Context.MakeIndex();
        FTransitionRef* Transition = Index.IsValid() && ParseGuid(Member.OwnerId, RequestedId)
            ? Index.FindTransition(RequestedId)
            : nullptr;
        if (!Index.IsValid() || Transition == nullptr)
        {
            OutError = Index.IsValid()
                ? TEXT("Native PostEdit snapshot could not resolve its Transition owner.")
                : Index.GetError();
            Notification.After();
            return false;
        }
        Before.TransitionId = Context.Identities.RestoreTransition(
            Transition->Transition->ID);
        Before.bTransitionDelay = Transition->Transition->bDelayTransition;
    }
    Notification.After();

    TArray<FNodePostEditSnapshot> AfterNodes;
    if (!CaptureNodePostEditSnapshots(Context, AfterNodes, OutError))
    {
        return false;
    }
    const TArray<FBindingSnapshot> AfterBindings = CaptureBindingSnapshot(Context);
    TArray<FCompileManifestEntry> AfterManifest;
    if (!CaptureCompileManifest(
            Context.Tree,
            Context.Data,
            Context.Identities,
            AfterManifest,
            OutError))
    {
        return false;
    }
    TSet<FGuid> RemovedPropertyFunctions;
    for (const FBindingSnapshot& Previous : Before.Bindings)
    {
        if (!Previous.PropertyFunctionId.IsValid())
        {
            continue;
        }
        const bool bBindingSurvived = AfterBindings.ContainsByPredicate(
            [&](const FBindingSnapshot& Current)
            {
                return Current.Source == Previous.Source
                    && Current.Target == Previous.Target
                    && Current.bOutput == Previous.bOutput;
            });
        if (!bBindingSurvived)
        {
            RemovedPropertyFunctions.Add(Previous.PropertyFunctionId);
        }
    }
    bool bClassifiedCascade = false;
    TSet<FString> DetailedContentKeys;
    TSet<FString> DetailedRemovalKeys;
    int32 BeforeIndex = 0;
    int32 AfterIndex = 0;
    while (BeforeIndex < Before.Nodes.Num() || AfterIndex < AfterNodes.Num())
    {
        if (BeforeIndex >= Before.Nodes.Num())
        {
            OutError = TEXT("A native PostEdit callback added an authored Node unexpectedly.");
            return false;
        }
        if (AfterIndex >= AfterNodes.Num())
        {
            const FNodePostEditSnapshot& Previous = Before.Nodes[BeforeIndex];
            const bool bRemovedFunction = RemovedPropertyFunctions.Contains(
                Previous.StableId);
            if (!StateTypeTaskIds.Contains(Previous.StableId) && !bRemovedFunction)
            {
                OutError = TEXT("A native PostEdit callback removed an authored Node unexpectedly.");
                return false;
            }
            bClassifiedCascade = true;
            const FString RemovedRef = TEXT("node@") + GuidText(Previous.StableId);
            DetailedRemovalKeys.Add(RemovedRef);
            if (bRemovedFunction)
            {
                Context.PlannedEffects.Add(MakeShared<FJsonValueString>(
                    TEXT("native PostEdit cascade removed ")
                    + RemovedRef
                    + TEXT(" with its owning Property Function Binding")));
            }
            else
            {
                Context.PlannedEffects.Add(MakeShared<FJsonValueString>(
                    TEXT("native PostEdit cascade removed ")
                    + RemovedRef
                    + TEXT(" from the State Type owner")));
            }
            ++BeforeIndex;
            continue;
        }
        const FNodePostEditSnapshot& Previous = Before.Nodes[BeforeIndex];
        const FNodePostEditSnapshot& Current = AfterNodes[AfterIndex];
        const FString PreviousId = GuidText(Previous.StableId);
        const FString CurrentId = GuidText(Current.StableId);
        if (PreviousId < CurrentId)
        {
            const bool bRemovedFunction = RemovedPropertyFunctions.Contains(
                Previous.StableId);
            if (!StateTypeTaskIds.Contains(Previous.StableId) && !bRemovedFunction)
            {
                OutError = TEXT("A native PostEdit callback removed an authored Node unexpectedly.");
                return false;
            }
            bClassifiedCascade = true;
            const FString RemovedRef = TEXT("node@") + GuidText(Previous.StableId);
            DetailedRemovalKeys.Add(RemovedRef);
            if (bRemovedFunction)
            {
                Context.PlannedEffects.Add(MakeShared<FJsonValueString>(
                    TEXT("native PostEdit cascade removed ")
                    + RemovedRef
                    + TEXT(" with its owning Property Function Binding")));
            }
            else
            {
                Context.PlannedEffects.Add(MakeShared<FJsonValueString>(
                    TEXT("native PostEdit cascade removed ")
                    + RemovedRef
                    + TEXT(" from the State Type owner")));
            }
            ++BeforeIndex;
            continue;
        }
        if (CurrentId < PreviousId)
        {
            OutError = TEXT("A native PostEdit callback added or changed an authored Node identity unexpectedly.");
            return false;
        }
        const FString NodeRef = TEXT("node@") + GuidText(Current.StableId);
        if (Previous.Node != Current.Node)
        {
            bClassifiedCascade = true;
            DetailedContentKeys.Add(NodeRef);
            Context.PlannedEffects.Add(MakeShared<FJsonValueString>(
                TEXT("native PostEdit cascade updated ") + NodeRef + TEXT(".Node")));
        }
        if (Previous.Instance != Current.Instance)
        {
            bClassifiedCascade = true;
            DetailedContentKeys.Add(NodeRef);
            Context.PlannedEffects.Add(MakeShared<FJsonValueString>(
                TEXT("native PostEdit cascade updated ") + NodeRef + TEXT(".Instance")));
        }
        if (Previous.ExecutionRuntimeData != Current.ExecutionRuntimeData)
        {
            bClassifiedCascade = true;
            DetailedContentKeys.Add(NodeRef);
            Context.PlannedEffects.Add(MakeShared<FJsonValueString>(
                TEXT("native PostEdit cascade updated ")
                + NodeRef
                + TEXT(".ExecutionRuntimeData")));
        }
        ++BeforeIndex;
        ++AfterIndex;
    }

    if (Before.TransitionId.IsValid())
    {
        FAuthoredIndex Index = Context.MakeIndex();
        FTransitionRef* Transition = Index.IsValid()
            ? Index.FindTransition(Before.TransitionId)
            : nullptr;
        if (!Index.IsValid() || Transition == nullptr)
        {
            OutError = Index.IsValid()
                ? TEXT("A native PostEdit callback removed its Transition owner unexpectedly.")
                : Index.GetError();
            return false;
        }
        if (Before.bTransitionDelay != Transition->Transition->bDelayTransition)
        {
            bClassifiedCascade = true;
            Context.PlannedEffects.Add(MakeShared<FJsonValueString>(
                TEXT("native PostEdit cascade updated transition@")
                + GuidText(Before.TransitionId)
                + TEXT(".bDelayTransition")));
        }
    }

    const TArray<FString> RemovedBindings = FindRemovedBindingEvidence(
        Context,
        Before.Bindings);
    const TArray<FString> AddedBindings = FindAddedBindingEvidence(
        Before.Bindings,
        AfterBindings);
    for (const FString& Evidence : RemovedBindings)
    {
        bClassifiedCascade = true;
        Context.PlannedEffects.Add(MakeShared<FJsonValueString>(
            TEXT("native PostEdit cascade removed ") + Evidence));
    }
    for (const FString& Evidence : AddedBindings)
    {
        bClassifiedCascade = true;
        Context.PlannedEffects.Add(MakeShared<FJsonValueString>(
            TEXT("native PostEdit cascade added ") + Evidence));
    }

    bool bManifestChanged = false;
    AddNativeCallbackManifestEffects(
        Context,
        TEXT("native PostEdit cascade"),
        BeforeManifest,
        AfterManifest,
        DetailedContentKeys,
        DetailedRemovalKeys,
        bManifestChanged);
    bClassifiedCascade |= bManifestChanged;

    const uint32 AfterHash = UStateTreeEditingSubsystem::CalculateStateTreeHash(&Context.Tree);
    if (Before.AuthoredHash != AfterHash && !bClassifiedCascade)
    {
        OutError = TEXT("A native PostEdit callback changed authored StateTree data outside the complete authored manifest or Binding diff.");
        return false;
    }
    return true;
}

struct FSetSemanticTracker
{
    UStateTreeState* State = nullptr;
    FStateTreeTransition* Transition = nullptr;
    FString ParameterId;
    FString RootMember;
    TArray<FGuid> TaskIds;
    TArray<FString> ParameterIds;
    EStateTreeStateSelectionBehavior SelectionBehavior = EStateTreeStateSelectionBehavior::TrySelectChildrenInOrder;
    FStateTreeStateLink LinkedSubtree;
    TObjectPtr<UStateTree> LinkedAsset = nullptr;
    FStateTreeStateParameters Parameters;
    TArray<FGuid> AllNodeIds;
    TArray<FBindingSnapshot> Bindings;
};

void CollectAllStableNodeIds(FPatchContext& Context, TArray<FGuid>& OutIds)
{
    OutIds.Reset();
    FAuthoredIndex Index = Context.MakeIndex();
    if (!Index.IsValid())
    {
        return;
    }
    TArray<FNodeRef*> Nodes;
    Index.GetAllNodes(Nodes);
    for (const FNodeRef* Node : Nodes)
    {
        if (Node != nullptr && Node->Node != nullptr)
        {
            OutIds.Add(Context.Identities.RestoreNode(Node->Node->ID));
        }
    }
}

void CollectStateTaskIds(
    FPatchContext& Context,
    const UStateTreeState& State,
    TArray<FGuid>& OutIds)
{
    OutIds.Reset();
    for (const FStateTreeEditorNode& Task : State.Tasks)
    {
        if (Task.ID.IsValid())
        {
            OutIds.Add(Context.Identities.RestoreNode(Task.ID));
        }
    }
    if (State.SingleTask.ID.IsValid())
    {
        OutIds.Add(Context.Identities.RestoreNode(State.SingleTask.ID));
    }
}

void CollectStateParameterIds(
    FPatchContext& Context,
    const UStateTreeState& State,
    TArray<FString>& OutIds)
{
    OutIds.Reset();
    const UPropertyBag* Bag = State.Parameters.Parameters.GetPropertyBagStruct();
    if (Bag == nullptr)
    {
        return;
    }
    for (const FPropertyBagPropertyDesc& Desc : Bag->GetPropertyDescs())
    {
        OutIds.Add(Context.PublicId(
            TEXT("parameter"),
            GuidText(State.Parameters.ID) + TEXT("/") + GuidText(Desc.ID)));
    }
}

TSharedPtr<FSetSemanticTracker> BeginSetSemanticTracking(
    FPatchContext& Context,
    const StateTreeSchema::FResolvedMember& Member)
{
    if (Member.SalPath.IsEmpty())
    {
        return nullptr;
    }
    const FString& Root = Member.SalPath[0];
    const bool bStateTracked = Member.OwnerKind == TEXT("state")
        && (Root == TEXT("Type")
            || Root == TEXT("LinkedSubtree")
            || Root == TEXT("LinkedAsset")
            || Root == TEXT("bHasRequiredEventToEnter")
            || Root == TEXT("RequiredEventToEnter"));
    // Every Transition property notification passes through UE's native
    // PostEditChangeChainProperty cascade, which clears delay for any
    // OnStateCompleted Trigger, including malformed legacy data.
    const bool bTransitionTracked = Member.OwnerKind == TEXT("transition");
    const bool bParameterTracked = Member.OwnerKind == TEXT("parameter");
    if (!bStateTracked && !bTransitionTracked && !bParameterTracked)
    {
        return nullptr;
    }
    FAuthoredIndex Index = Context.MakeIndex();
    if (!Index.IsValid())
    {
        return nullptr;
    }
    TSharedPtr<FSetSemanticTracker> Tracker = MakeShared<FSetSemanticTracker>();
    Tracker->RootMember = Root;
    Tracker->Bindings = CaptureBindingSnapshot(Context);
    CollectAllStableNodeIds(Context, Tracker->AllNodeIds);
    if (bStateTracked)
    {
        FGuid Id;
        FStateRef* State = ParseGuid(Member.OwnerId, Id) ? Index.FindState(Id) : nullptr;
        if (State == nullptr)
        {
            return nullptr;
        }
        Tracker->State = State->State;
        CollectStateTaskIds(Context, *State->State, Tracker->TaskIds);
        CollectStateParameterIds(Context, *State->State, Tracker->ParameterIds);
        Tracker->SelectionBehavior = State->State->SelectionBehavior;
        Tracker->LinkedSubtree = State->State->LinkedSubtree;
        Tracker->LinkedAsset = State->State->LinkedAsset;
        Tracker->Parameters = State->State->Parameters;
    }
    else if (bTransitionTracked)
    {
        FGuid Id;
        FTransitionRef* Transition = ParseGuid(Member.OwnerId, Id)
            ? Index.FindTransition(Id)
            : nullptr;
        if (Transition == nullptr)
        {
            return nullptr;
        }
        Tracker->Transition = Transition->Transition;
    }
    else
    {
        FParameterRef* Parameter = Index.FindParameter(Member.OwnerId);
        if (Parameter == nullptr || !Parameter->bFixedLayout)
        {
            return nullptr;
        }
        Tracker->ParameterId = Member.OwnerId;
    }
    return Tracker;
}

void AddDormantEventEffects(
    FPatchContext& Context,
    const TArray<FBindingSnapshot>& Before,
    const FGuid& EventId,
    const FString& Reason)
{
    TMap<FGuid, const FPropertyBindingDataView> Values;
    Context.Data.GetAllStructValues(Values);
    for (const FBindingSnapshot& Binding : Before)
    {
        if (Binding.Source.GetStructID() != EventId
            || !Binding.bSourceResolved
            || IsBindingPathResolved(Values, Binding.Source, false))
        {
            continue;
        }
        Context.ExpectedValidationBindingRepairs.Add(Binding.Evidence);
        Context.PlannedEffects.Add(MakeShared<FJsonValueString>(
            TEXT("preserved ") + Reason + TEXT(" ") + Binding.Evidence));
        Context.ResultComments.Add(Reason + TEXT(": ") + Binding.Evidence);
    }
}

void AddDormantDelegateEffects(
    FPatchContext& Context,
    const TArray<FBindingSnapshot>& Before,
    const FGuid& TransitionId)
{
    for (const FBindingSnapshot& Binding : Before)
    {
        if (Binding.Target.GetStructID() != TransitionId
            || Binding.Target.GetSegments().IsEmpty()
            || Binding.Target.GetSegments()[0].GetName()
                != GET_MEMBER_NAME_CHECKED(FStateTreeTransition, DelegateListener))
        {
            continue;
        }
        Context.PlannedEffects.Add(MakeShared<FJsonValueString>(
            TEXT("preserved inactive Delegate ") + Binding.Evidence));
        Context.ResultComments.Add(TEXT("inactive Delegate: ") + Binding.Evidence);
    }
}

bool FinishSetSemanticTracking(
    FPatchContext& Context,
    const StateTreeSchema::FResolvedMember& Member,
    const TSharedPtr<FSetSemanticTracker>& Tracker,
    FString& OutError)
{
    if (!Tracker.IsValid())
    {
        return true;
    }
    if (Tracker->State != nullptr)
    {
        UStateTreeState& State = *Tracker->State;
        const bool bNativeCascade = Tracker->RootMember == TEXT("Type")
            || Tracker->RootMember == TEXT("LinkedSubtree")
            || Tracker->RootMember == TEXT("LinkedAsset");
        if (bNativeCascade)
        {
            TArray<FGuid> CurrentTaskIds;
            CollectStateTaskIds(Context, State, CurrentTaskIds);
            for (const FGuid& Id : Tracker->TaskIds)
            {
                if (!CurrentTaskIds.Contains(Id))
                {
                    Context.PlannedEffects.Add(MakeShared<FJsonValueString>(
                        TEXT("native State cascade removed node@")
                        + GuidText(Id)));
                }
            }
            TArray<FString> CurrentParameterIds;
            CollectStateParameterIds(Context, State, CurrentParameterIds);
            for (const FString& Id : Tracker->ParameterIds)
            {
                if (!CurrentParameterIds.Contains(Id))
                {
                    Context.PlannedEffects.Add(MakeShared<FJsonValueString>(
                        TEXT("native State cascade removed parameter@") + Id));
                }
            }
            for (const FString& Id : CurrentParameterIds)
            {
                if (!Tracker->ParameterIds.Contains(Id))
                {
                    Context.PlannedEffects.Add(MakeShared<FJsonValueString>(
                        TEXT("native State cascade synchronized parameter@") + Id));
                }
            }
            if (Tracker->SelectionBehavior != State.SelectionBehavior)
            {
                Context.PlannedEffects.Add(MakeShared<FJsonValueString>(
                    TEXT("native State cascade updated ")
                    + Context.PublicRef(TEXT("state"), GuidText(State.ID))
                    + TEXT(".SelectionBehavior")));
            }
            if (!FStateTreeStateLink::StaticStruct()->CompareScriptStruct(
                    &Tracker->LinkedSubtree,
                    &State.LinkedSubtree,
                    PPF_None))
            {
                Context.PlannedEffects.Add(MakeShared<FJsonValueString>(
                    TEXT("native State cascade updated ")
                    + Context.PublicRef(TEXT("state"), GuidText(State.ID))
                    + TEXT(".LinkedSubtree")));
            }
            if (Tracker->LinkedAsset != State.LinkedAsset)
            {
                Context.PlannedEffects.Add(MakeShared<FJsonValueString>(
                    TEXT("native State cascade updated ")
                    + Context.PublicRef(TEXT("state"), GuidText(State.ID))
                    + TEXT(".LinkedAsset")));
            }
            if (!FStateTreeStateParameters::StaticStruct()->CompareScriptStruct(
                    &Tracker->Parameters,
                    &State.Parameters,
                    PPF_None))
            {
                Context.PlannedEffects.Add(MakeShared<FJsonValueString>(
                    TEXT("native State cascade synchronized ")
                    + Context.PublicRef(TEXT("state"), GuidText(State.ID))
                    + TEXT(".Parameters")));
            }
            TArray<FString> RemovedBindings;
            if (!CleanupNewlyInvalidBindings(Context, Tracker->Bindings, RemovedBindings))
            {
                OutError = TEXT("Native State cascade Binding cleanup exceeded its bounded pass limit.");
                return false;
            }
            RemovedBindings = FindRemovedBindingEvidence(Context, Tracker->Bindings);
            AddRemovedBindingEffects(Context, RemovedBindings);
        }
        if (Tracker->RootMember == TEXT("bHasRequiredEventToEnter")
            || Tracker->RootMember == TEXT("RequiredEventToEnter"))
        {
            AddDormantEventEffects(
                Context,
                Tracker->Bindings,
                State.GetEventID(),
                State.bHasRequiredEventToEnter
                    ? TEXT("structurally invalid Required Event Binding")
                    : TEXT("inactive Required Event Binding"));
        }
    }
    else if (Tracker->Transition != nullptr)
    {
        FStateTreeTransition& Transition = *Tracker->Transition;
        if (Tracker->RootMember == TEXT("Trigger")
            || Tracker->RootMember == TEXT("RequiredEvent"))
        {
            AddDormantEventEffects(
                Context,
                Tracker->Bindings,
                Transition.GetEventID(),
                Transition.Trigger == EStateTreeTransitionTrigger::OnEvent
                    ? TEXT("structurally invalid Transition Event Binding")
                    : TEXT("inactive Transition Event Binding"));
        }
        if (Tracker->RootMember == TEXT("Trigger")
            && Transition.Trigger != EStateTreeTransitionTrigger::OnDelegate)
        {
            AddDormantDelegateEffects(Context, Tracker->Bindings, Transition.ID);
        }
    }
    else if (!Tracker->ParameterId.IsEmpty())
    {
        const TArray<FString> RemovedBindings = FindRemovedBindingEvidence(
            Context,
            Tracker->Bindings);
        AddRemovedBindingEffects(Context, RemovedBindings);
    }
    TArray<FGuid> CurrentNodeIds;
    CollectAllStableNodeIds(Context, CurrentNodeIds);
    for (const FGuid& Id : Tracker->AllNodeIds)
    {
        if (!CurrentNodeIds.Contains(Id) && !Tracker->TaskIds.Contains(Id))
        {
            Context.PlannedEffects.Add(MakeShared<FJsonValueString>(
                TEXT("native cascade removed node@")
                + GuidText(Id)
                + TEXT(" with its owning Property Function Binding")));
        }
    }
    return true;
}

bool IsStateDescendantOf(const UStateTreeState* Candidate, const UStateTreeState* Ancestor)
{
    for (const UStateTreeState* Current = Candidate; Current != nullptr; Current = Current->Parent)
    {
        if (Current == Ancestor)
        {
            return true;
        }
    }
    return false;
}

bool ValidateBindingExecutionVisibility(FPatchContext& Context, FString& OutError)
{
    for (const FStateTreePropertyPathBinding& Binding : Context.Data.EditorBindings.GetBindings())
    {
        if (Binding.GetPropertyFunctionNode().IsValid())
        {
            continue;
        }
        const FPropertyBindingPath& Candidate = Binding.GetSourcePath();
        const FPropertyBindingPath& ExecutionOwner = Binding.GetTargetPath();
        FPropertyBindingDataView TargetView;
        TArray<FPropertyBindingPathIndirection, TInlineAllocator<16>> TargetIndirections;
        FString ResolveError;
        if (!Context.Data.GetBindingDataViewByID(ExecutionOwner.GetStructID(), TargetView)
            || !ExecutionOwner.ResolveIndirectionsWithValue(
                TargetView,
                TargetIndirections,
                &ResolveError,
                true)
            || TargetIndirections.IsEmpty())
        {
            OutError = ResolveError.IsEmpty()
                ? TEXT("Authored Binding target path cannot be resolved after the move.")
                : ResolveError;
            return false;
        }
        const EStateTreePropertyUsage TargetUsage = UE::StateTree::GetUsageFromMetaData(
            TargetIndirections[0].GetProperty());
        const bool bOutput = TargetUsage == EStateTreePropertyUsage::Output;
        TArray<TInstancedStruct<FPropertyBindingBindableStructDescriptor>> Accessible;
        Context.Data.GetBindableStructs(ExecutionOwner.GetStructID(), Accessible);
        const FStateTreeBindableStructDesc* CandidateDesc = nullptr;
        const bool bVisible = Candidate.GetStructID() == ExecutionOwner.GetStructID()
            || Accessible.ContainsByPredicate(
            [&](const TInstancedStruct<FPropertyBindingBindableStructDescriptor>& Descriptor)
            {
                const FStateTreeBindableStructDesc* Desc = Descriptor.GetPtr<FStateTreeBindableStructDesc>();
                if (Desc != nullptr && Desc->ID == Candidate.GetStructID())
                {
                    CandidateDesc = Desc;
                    return true;
                }
                return false;
            });
        if (!bVisible)
        {
            OutError = FString::Printf(
                TEXT("Move would make authored Binding candidate %s inaccessible to execution owner %s."),
                *GuidText(Candidate.GetStructID()),
                *GuidText(ExecutionOwner.GetStructID()));
            return false;
        }
        if (bOutput
            && Candidate.GetStructID() != ExecutionOwner.GetStructID()
            && (CandidateDesc == nullptr
                || (CandidateDesc->DataSource != EStateTreeBindableStructSource::Parameter
                    && CandidateDesc->DataSource != EStateTreeBindableStructSource::StateParameter)))
        {
            OutError = TEXT("A native StateTree Output Binding can write only to a visible global Parameter or State Parameter.");
            return false;
        }
    }
    return true;
}

void ModifyStateArrayOwner(FPatchContext& Context, UStateTreeState* Parent)
{
    if (Parent != nullptr)
    {
        Parent->Modify();
    }
    else
    {
        Context.Data.Modify();
    }
}

bool MoveState(
    FPatchContext& Context,
    FStateRef& Source,
    const StateTreePalette::FDestination& Destination,
    const TSharedPtr<FJsonObject>& Anchor,
    const bool bAfter,
    FString& OutError)
{
    UStateTreeState* NewParent = nullptr;
    TArray<TObjectPtr<UStateTreeState>>* DestinationArray = nullptr;
    if (Destination.Role == StateTreePalette::EDestinationRole::RootState)
    {
        DestinationArray = &Context.Data.SubTrees;
    }
    else if (Destination.Role == StateTreePalette::EDestinationRole::ChildState)
    {
        FAuthoredIndex Index = Context.MakeIndex();
        FStateRef* Parent = Index.IsValid() ? Index.FindState(Destination.OwnerId) : nullptr;
        if (!Index.IsValid() || Parent == nullptr)
        {
            OutError = Index.IsValid() ? TEXT("Move destination State is missing or ambiguous.") : Index.GetError();
            return false;
        }
        NewParent = Parent->State;
        DestinationArray = &NewParent->Children;
    }
    else
    {
        OutError = TEXT("State can move only to SubTrees or Children.");
        return false;
    }
    if (NewParent == Source.State || IsStateDescendantOf(NewParent, Source.State))
    {
        OutError = TEXT("A State cannot be moved into itself or one of its descendants.");
        return false;
    }
    FString AnchorKind;
    FString AnchorId;
    FGuid AnchorGuid;
    if (Anchor.IsValid()
        && (!Context.ResolveRef(Anchor, AnchorKind, AnchorId, OutError)
            || AnchorKind != TEXT("state")
            || !ParseGuid(AnchorId, AnchorGuid)
            || AnchorGuid == Source.State->ID))
    {
        OutError = TEXT("State move anchor is invalid or is the moved State itself.");
        return false;
    }

    UStateTreeState* OldParent = Source.Parent;
    TArray<TObjectPtr<UStateTreeState>>* SourceArray = Source.Siblings;
    if (SourceArray == nullptr || SourceArray->Find(Source.State) == INDEX_NONE)
    {
        OutError = TEXT("State source collection is inconsistent.");
        return false;
    }
    Source.State->Modify();
    ModifyStateArrayOwner(Context, OldParent);
    ModifyStateArrayOwner(Context, NewParent);
    SourceArray->RemoveSingle(Source.State);

    int32 InsertIndex = DestinationArray->Num();
    if (Anchor.IsValid())
    {
        InsertIndex = DestinationArray->IndexOfByPredicate(
            [&](const TObjectPtr<UStateTreeState>& Item)
            {
                return Item != nullptr && Item->ID == AnchorGuid;
            });
        if (InsertIndex == INDEX_NONE)
        {
            OutError = TEXT("State move anchor does not belong to the exact destination after removal.");
            return false;
        }
        if (bAfter)
        {
            ++InsertIndex;
        }
    }
    DestinationArray->Insert(Source.State, InsertIndex);
    Source.State->Parent = NewParent;
    return true;
}

bool ReparentNodeObjects(FStateTreeEditorNode& Node, UObject& NewOuter, FString& OutError)
{
    constexpr ERenameFlags Flags = REN_DontCreateRedirectors | REN_DoNotDirty;
    for (UObject* Object : {Node.InstanceObject.Get(), Node.ExecutionRuntimeDataObject.Get()})
    {
        if (Object != nullptr && Object->GetOuter() != &NewOuter)
        {
            Object->Modify();
            if (!Object->Rename(nullptr, &NewOuter, Flags))
            {
                OutError = TEXT("UE could not move StateTree Node object data to its new native owner.");
                return false;
            }
        }
    }
    return true;
}

bool MoveNode(
    FPatchContext& Context,
    FNodeRef& Source,
    const StateTreePalette::FDestination& Destination,
    const TSharedPtr<FJsonObject>& Anchor,
    const bool bAfter,
    FString& OutError)
{
    if (Source.bPropertyFunction || Source.Array == nullptr || Source.Role == ENodeRole::SingleTask)
    {
        OutError = TEXT("Binding-owned Property Functions and SingleTask do not have an independent ordered move lifecycle.");
        return false;
    }
    TArray<FStateTreeEditorNode>* DestinationArray = nullptr;
    FStateTreeEditorNode* DestinationSingle = nullptr;
    UObject* DestinationOuter = nullptr;
    if (!ResolveNodeDestination(
            Context,
            Destination,
            DestinationArray,
            DestinationSingle,
            DestinationOuter,
            OutError)
        || DestinationArray == nullptr
        || DestinationSingle != nullptr
        || DestinationOuter == nullptr)
    {
        if (OutError.IsEmpty())
        {
            OutError = TEXT("Node move requires one compatible ordered Node destination.");
        }
        return false;
    }
    const UScriptStruct* NodeType = Source.Node->Node.GetScriptStruct();
    if (NodeType == nullptr
        || Destination.RequiredNodeStruct == nullptr
        || !NodeType->IsChildOf(Destination.RequiredNodeStruct))
    {
        OutError = TEXT("Node native type is incompatible with the exact destination role.");
        return false;
    }
    FString AnchorKind;
    FString AnchorId;
    FGuid AnchorGuid;
    if (Anchor.IsValid()
        && (!Context.ResolveRef(Anchor, AnchorKind, AnchorId, OutError)
            || AnchorKind != TEXT("node")
            || !ParseGuid(AnchorId, AnchorGuid)
            || AnchorGuid == Source.Node->ID))
    {
        OutError = TEXT("Node move anchor is invalid or is the moved Node itself.");
        return false;
    }
    UObject* SourceOuter = Source.OwnerState != nullptr
        ? static_cast<UObject*>(Source.OwnerState)
        : static_cast<UObject*>(&Context.Data);
    SourceOuter->Modify();
    DestinationOuter->Modify();
    const FGuid SourceId = Source.Node->ID;
    const int32 SourceIndex = Source.Array->IndexOfByPredicate(
        [&](const FStateTreeEditorNode& Item) { return Item.ID == SourceId; });
    if (SourceIndex == INDEX_NONE)
    {
        OutError = TEXT("Node source collection is inconsistent.");
        return false;
    }
    FStateTreeEditorNode Moved = MoveTemp((*Source.Array)[SourceIndex]);
    Source.Array->RemoveAt(SourceIndex);
    int32 InsertIndex = DestinationArray->Num();
    if (Anchor.IsValid())
    {
        InsertIndex = DestinationArray->IndexOfByPredicate(
            [&](const FStateTreeEditorNode& Item) { return Item.ID == AnchorGuid; });
        if (InsertIndex == INDEX_NONE)
        {
            OutError = TEXT("Node move anchor does not belong to the exact destination after removal.");
            return false;
        }
        if (bAfter)
        {
            ++InsertIndex;
        }
    }
    if (!ReparentNodeObjects(Moved, *DestinationOuter, OutError))
    {
        return false;
    }
    DestinationArray->Insert(MoveTemp(Moved), InsertIndex);
    return true;
}

bool MoveTransition(
    FPatchContext& Context,
    FTransitionRef& Source,
    const StateTreePalette::FDestination& Destination,
    const TSharedPtr<FJsonObject>& Anchor,
    const bool bAfter,
    FString& OutError)
{
    if (Destination.Role != StateTreePalette::EDestinationRole::Transition)
    {
        OutError = TEXT("Transition can move only to a State Transitions destination.");
        return false;
    }
    FAuthoredIndex Index = Context.MakeIndex();
    FStateRef* DestinationState = Index.IsValid() ? Index.FindState(Destination.OwnerId) : nullptr;
    if (!Index.IsValid() || DestinationState == nullptr || Source.OwnerState == nullptr)
    {
        OutError = Index.IsValid() ? TEXT("Transition move owner is missing or ambiguous.") : Index.GetError();
        return false;
    }
    FString AnchorKind;
    FString AnchorId;
    FGuid AnchorGuid;
    if (Anchor.IsValid()
        && (!Context.ResolveRef(Anchor, AnchorKind, AnchorId, OutError)
            || AnchorKind != TEXT("transition")
            || !ParseGuid(AnchorId, AnchorGuid)
            || AnchorGuid == Source.Transition->ID))
    {
        OutError = TEXT("Transition move anchor is invalid or is the moved Transition itself.");
        return false;
    }
    Source.OwnerState->Modify();
    DestinationState->State->Modify();
    TArray<FStateTreeTransition>& SourceArray = Source.OwnerState->Transitions;
    const FGuid SourceId = Source.Transition->ID;
    const int32 SourceIndex = SourceArray.IndexOfByPredicate(
        [&](const FStateTreeTransition& Item) { return Item.ID == SourceId; });
    if (SourceIndex == INDEX_NONE)
    {
        OutError = TEXT("Transition source collection is inconsistent.");
        return false;
    }
    FStateTreeTransition Moved = MoveTemp(SourceArray[SourceIndex]);
    SourceArray.RemoveAt(SourceIndex);
    TArray<FStateTreeTransition>& DestinationArray = DestinationState->State->Transitions;
    int32 InsertIndex = DestinationArray.Num();
    if (Anchor.IsValid())
    {
        InsertIndex = DestinationArray.IndexOfByPredicate(
            [&](const FStateTreeTransition& Item) { return Item.ID == AnchorGuid; });
        if (InsertIndex == INDEX_NONE)
        {
            OutError = TEXT("Transition move anchor does not belong to the exact destination after removal.");
            return false;
        }
        if (bAfter)
        {
            ++InsertIndex;
        }
    }
    for (FStateTreeEditorNode& Condition : Moved.Conditions)
    {
        if (!ReparentNodeObjects(Condition, *DestinationState->State, OutError))
        {
            return false;
        }
    }
    DestinationArray.Insert(MoveTemp(Moved), InsertIndex);
    return true;
}

bool MoveParameter(
    FPatchContext& Context,
    FParameterRef& Source,
    const TSharedPtr<FJsonObject>& Anchor,
    const bool bBefore,
    const bool bAfter,
    FString& OutError)
{
    if (Source.bFixedLayout || Source.Bag == nullptr || !Anchor.IsValid() || (!bBefore && !bAfter))
    {
        OutError = TEXT("Parameter move requires before/after in one editable local layout.");
        return false;
    }
    FString Kind;
    FString Id;
    if (!Context.ResolveRef(Anchor, Kind, Id, OutError) || Kind != TEXT("parameter"))
    {
        OutError = TEXT("Parameter move anchor is invalid.");
        return false;
    }
    FAuthoredIndex Index = Context.MakeIndex();
    FParameterRef* Target = Index.IsValid() ? Index.FindParameter(Id) : nullptr;
    if (!Index.IsValid() || Target == nullptr || Target->ContainerId != Source.ContainerId)
    {
        OutError = Index.IsValid()
            ? TEXT("Parameter move anchor must belong to the same editable container.")
            : Index.GetError();
        return false;
    }
    UObject* Owner = Source.OwnerState != nullptr
        ? static_cast<UObject*>(Source.OwnerState)
        : static_cast<UObject*>(&Context.Data);
    return ApplyParameterLayoutChange(
        *Owner,
        Source.bRoot,
        EPropertyChangeType::ArrayMove,
        [&]()
        {
            const EPropertyBagAlterationResult Result = Source.Bag->ReorderProperty(
                Source.Desc.Name,
                Target->Desc.Name,
                bBefore);
            if (Result != EPropertyBagAlterationResult::Success)
            {
                OutError = TEXT("UE rejected the exact Parameter reorder.");
                return false;
            }
            return true;
        },
        OutError);
}

bool HandleMove(FPatchContext& Context, const TSharedPtr<FJsonObject>& Operation)
{
    const TSharedPtr<FJsonObject>* Target = nullptr;
    if (!Operation->TryGetObjectField(TEXT("target"), Target) || Target == nullptr)
    {
        return Context.Fail(TEXT("resolution.object_not_found"), TEXT("StateTree move requires one exact authored object."), TEXT("move"));
    }
    FString Kind;
    FString Id;
    FString Error;
    if (!Context.ResolveRef(*Target, Kind, Id, Error))
    {
        return Context.Fail(TEXT("resolution.object_not_found"), Error, TEXT("move"));
    }
    TSharedPtr<FJsonObject> DestinationRef;
    TSharedPtr<FJsonObject> Anchor;
    bool bBefore = false;
    bool bAfter = false;
    if (!ReadPlacement(Context, Operation, DestinationRef, Anchor, bBefore, bAfter, Error))
    {
        return Context.Fail(TEXT("resolution.invalid_anchor"), Error, TEXT("move"), Kind + TEXT("@") + Id);
    }
    FAuthoredIndex Index = Context.MakeIndex();
    if (!Index.IsValid())
    {
        return Context.Fail(TEXT("validation.patch_state_invalid"), Index.GetError(), TEXT("move"));
    }
    bool bMoved = false;
    if (Kind == TEXT("parameter"))
    {
        FParameterRef* Source = Index.FindParameter(Id);
        bMoved = Source != nullptr && MoveParameter(Context, *Source, Anchor, bBefore, bAfter, Error);
    }
    else
    {
        StateTreePalette::FDestination Destination;
        if (!StateTreePalette::ResolveDestination(Context.Tree, Context.Data, DestinationRef, Destination, Error))
        {
            return Context.Fail(TEXT("resolution.invalid_anchor"), Error, TEXT("move"), Kind + TEXT("@") + Id);
        }
        FGuid Guid;
        if (!ParseGuid(Id, Guid))
        {
            return Context.Fail(TEXT("resolution.object_not_found"), TEXT("StateTree move id is malformed."), TEXT("move"), Id);
        }
        if (Kind == TEXT("state"))
        {
            FStateRef* Source = Index.FindState(Guid);
            bMoved = Source != nullptr && MoveState(Context, *Source, Destination, Anchor, bAfter, Error);
        }
        else if (Kind == TEXT("node"))
        {
            FNodeRef* Source = Index.FindNode(Guid);
            bMoved = Source != nullptr && MoveNode(Context, *Source, Destination, Anchor, bAfter, Error);
        }
        else if (Kind == TEXT("transition"))
        {
            FTransitionRef* Source = Index.FindTransition(Guid);
            bMoved = Source != nullptr && MoveTransition(Context, *Source, Destination, Anchor, bAfter, Error);
        }
        else
        {
            Error = TEXT("This StateTree object kind has no authored move lifecycle.");
        }
    }
    if (!bMoved)
    {
        if (Error.IsEmpty())
        {
            Error = TEXT("StateTree move source is missing or ambiguous.");
        }
        return Context.Fail(TEXT("capability.operation_unavailable"), Error, TEXT("move"), Kind + TEXT("@") + Id);
    }
    if (!ValidateBindingExecutionVisibility(Context, Error))
    {
        return Context.Fail(
            TEXT("validation.operation_arguments_invalid"),
            Error,
            TEXT("move"),
            Context.PublicRef(Kind, Id));
    }
    Context.Tree.Modify();
    Context.Data.Modify();
    const FString PublicTarget = Context.PublicRef(Kind, Id);
    AddLifecyclePlan(Context, TEXT("move"), PublicTarget, TEXT("moved ") + PublicTarget);
    return true;
}

bool CollectStateSubtreeCascade(
    FPatchContext& Context,
    const UStateTreeState* State,
    TArray<FString>& OutRefs,
    TSet<FGuid>& OutStateIds,
    FString& OutError)
{
    OutRefs.Reset();
    OutStateIds.Reset();
    if (State == nullptr)
    {
        return true;
    }
    TArray<const UStateTreeState*> Stack{State};
    TSet<const UStateTreeState*> Visited;
    while (!Stack.IsEmpty())
    {
        const UStateTreeState* Current = Stack.Pop(EAllowShrinking::No);
        if (Current == nullptr || Visited.Contains(Current))
        {
            OutError = TEXT("State removal subtree contains a null, repeated, or cyclic State.");
            return false;
        }
        Visited.Add(Current);
        OutStateIds.Add(Current->ID);
        OutRefs.Add(Context.PublicRef(TEXT("state"), GuidText(Current->ID)));
        const UPropertyBag* Bag = Current->Parameters.Parameters.GetPropertyBagStruct();
        if (Bag != nullptr)
        {
            for (const FPropertyBagPropertyDesc& Desc : Bag->GetPropertyDescs())
            {
                OutRefs.Add(Context.PublicRef(
                    TEXT("parameter"),
                    GuidText(Current->Parameters.ID) + TEXT("/") + GuidText(Desc.ID)));
            }
        }
        for (const FStateTreeEditorNode& Node : Current->EnterConditions)
        {
            OutRefs.Add(Context.PublicRef(TEXT("node"), GuidText(Node.ID)));
        }
        for (const FStateTreeEditorNode& Node : Current->Tasks)
        {
            OutRefs.Add(Context.PublicRef(TEXT("node"), GuidText(Node.ID)));
        }
        if (Current->SingleTask.ID.IsValid())
        {
            OutRefs.Add(Context.PublicRef(TEXT("node"), GuidText(Current->SingleTask.ID)));
        }
        for (const FStateTreeEditorNode& Node : Current->Considerations)
        {
            OutRefs.Add(Context.PublicRef(TEXT("node"), GuidText(Node.ID)));
        }
        for (const FStateTreeTransition& Transition : Current->Transitions)
        {
            OutRefs.Add(Context.PublicRef(TEXT("transition"), GuidText(Transition.ID)));
            for (const FStateTreeEditorNode& Node : Transition.Conditions)
            {
                OutRefs.Add(Context.PublicRef(TEXT("node"), GuidText(Node.ID)));
            }
        }
        if (OutRefs.Num() > 50000)
        {
            OutError = TEXT("State removal subtree exceeds the authored-object hard limit.");
            return false;
        }
        for (const UStateTreeState* Child : Current->Children)
        {
            Stack.Add(Child);
        }
    }
    return true;
}

void AddDanglingStateLinkEffects(
    FPatchContext& Context,
    const TSet<FGuid>& RemovedStateIds)
{
    if (RemovedStateIds.IsEmpty())
    {
        return;
    }
    Context.Data.VisitHierarchy(
        [&](UStateTreeState& State, UStateTreeState*)
        {
            if (RemovedStateIds.Contains(State.LinkedSubtree.ID))
            {
                Context.PlannedEffects.Add(MakeShared<FJsonValueString>(
                    TEXT("preserved dangling ")
                    + Context.PublicRef(TEXT("state"), GuidText(State.ID))
                    + TEXT(".LinkedSubtree -> ")
                    + Context.PublicRef(TEXT("state"), GuidText(State.LinkedSubtree.ID))));
            }
            for (const FStateTreeTransition& Transition : State.Transitions)
            {
                if (RemovedStateIds.Contains(Transition.State.ID))
                {
                    Context.PlannedEffects.Add(MakeShared<FJsonValueString>(
                        TEXT("preserved dangling ")
                        + Context.PublicRef(TEXT("transition"), GuidText(Transition.ID))
                        + TEXT(".State -> ")
                        + Context.PublicRef(TEXT("state"), GuidText(Transition.State.ID))));
                }
            }
            return EStateTreeVisitor::Continue;
        });
}

bool HandleRemove(FPatchContext& Context, const TSharedPtr<FJsonObject>& Operation)
{
    const TSharedPtr<FJsonObject>* Target = nullptr;
    FString Kind;
    FString Id;
    FString Error;
    if (!Operation->TryGetObjectField(TEXT("target"), Target)
        || Target == nullptr
        || !Context.ResolveRef(*Target, Kind, Id, Error))
    {
        return Context.Fail(TEXT("resolution.object_not_found"), Error.IsEmpty() ? TEXT("StateTree remove requires one exact authored object.") : Error, TEXT("remove"));
    }
    FAuthoredIndex Index = Context.MakeIndex();
    if (!Index.IsValid())
    {
        return Context.Fail(TEXT("validation.patch_state_invalid"), Index.GetError(), TEXT("remove"));
    }
    const TArray<FBindingSnapshot> BindingBefore = CaptureBindingSnapshot(Context);
    TArray<FString> RemovedRefs;
    TSet<FGuid> RemovedStateIds;
    bool bRemoved = false;
    if (Kind == TEXT("state"))
    {
        FGuid Guid;
        FStateRef* Source = ParseGuid(Id, Guid) ? Index.FindState(Guid) : nullptr;
        if (Source != nullptr && Source->Siblings != nullptr)
        {
            if (!CollectStateSubtreeCascade(
                    Context,
                    Source->State,
                    RemovedRefs,
                    RemovedStateIds,
                    Error))
            {
                return Context.Fail(TEXT("validation.patch_state_invalid"), Error, TEXT("remove"), Kind + TEXT("@") + Id);
            }
            Source->State->Modify();
            ModifyStateArrayOwner(Context, Source->Parent);
            bRemoved = Source->Siblings->RemoveSingle(Source->State) == 1;
            if (bRemoved)
            {
                Source->State->Parent = nullptr;
            }
        }
    }
    else if (Kind == TEXT("node"))
    {
        FGuid Guid;
        FNodeRef* Source = ParseGuid(Id, Guid) ? Index.FindNode(Guid) : nullptr;
        if (Source != nullptr && !Source->bPropertyFunction)
        {
            RemovedRefs.Add(Context.PublicRef(TEXT("node"), GuidText(Source->Node->ID)));
            UObject* Owner = Source->OwnerState != nullptr
                ? static_cast<UObject*>(Source->OwnerState)
                : static_cast<UObject*>(&Context.Data);
            Owner->Modify();
            if (Source->Role == ENodeRole::SingleTask && Source->OwnerState != nullptr)
            {
                Source->OwnerState->SingleTask.Reset();
                bRemoved = true;
            }
            else if (Source->Array != nullptr && Source->Array->IsValidIndex(Source->Index))
            {
                Source->Array->RemoveAt(Source->Index);
                bRemoved = true;
            }
        }
        else if (Source != nullptr && Source->bPropertyFunction)
        {
            Error = TEXT("A Property Function Node is owned by its result Binding and cannot be removed independently.");
        }
    }
    else if (Kind == TEXT("transition"))
    {
        FGuid Guid;
        FTransitionRef* Source = ParseGuid(Id, Guid) ? Index.FindTransition(Guid) : nullptr;
        if (Source != nullptr && Source->OwnerState != nullptr
            && Source->OwnerState->Transitions.IsValidIndex(Source->Index))
        {
            RemovedRefs.Add(Context.PublicRef(
                TEXT("transition"),
                GuidText(Source->Transition->ID)));
            for (const FStateTreeEditorNode& Condition : Source->Transition->Conditions)
            {
                RemovedRefs.Add(Context.PublicRef(TEXT("node"), GuidText(Condition.ID)));
            }
            Source->OwnerState->Modify();
            Source->OwnerState->Transitions.RemoveAt(Source->Index);
            bRemoved = true;
        }
    }
    else if (Kind == TEXT("parameter"))
    {
        FParameterRef* Source = Index.FindParameter(Id);
        if (Source != nullptr && Source->Bag != nullptr && !Source->bFixedLayout)
        {
            RemovedRefs.Add(Context.PublicRef(TEXT("parameter"), Id));
            UObject* Owner = Source->OwnerState != nullptr
                ? static_cast<UObject*>(Source->OwnerState)
                : static_cast<UObject*>(&Context.Data);
            bRemoved = ApplyParameterLayoutChange(
                *Owner,
                Source->bRoot,
                EPropertyChangeType::ArrayRemove,
                [&]()
                {
                    const EPropertyBagAlterationResult Result = Source->Bag->RemovePropertyByName(Source->Desc.Name);
                    if (Result != EPropertyBagAlterationResult::Success)
                    {
                        Error = TEXT("UE rejected removal from the exact Parameter layout.");
                        return false;
                    }
                    return true;
                },
                Error);
        }
        else if (Source != nullptr)
        {
            Error = TEXT("A fixed-layout Parameter descriptor cannot be removed.");
        }
    }
    if (!bRemoved)
    {
        return Context.Fail(
            TEXT("capability.operation_unavailable"),
            Error.IsEmpty() ? TEXT("StateTree remove target is missing, ambiguous, or read-only.") : Error,
            TEXT("remove"),
            Kind + TEXT("@") + Id);
    }
    Context.Tree.Modify();
    Context.Data.Modify();
    TArray<FString> RemovedBindings;
    if (!CleanupNewlyInvalidBindings(Context, BindingBefore, RemovedBindings))
    {
        return Context.Fail(
            TEXT("validation.patch_state_invalid"),
            TEXT("StateTree removal Binding cleanup exceeded its bounded pass limit."),
            TEXT("remove"),
            Context.PublicRef(Kind, Id));
    }
    RemovedBindings = FindRemovedBindingEvidence(Context, BindingBefore);
    for (const FString& Ref : RemovedRefs)
    {
        Context.PlannedEffects.Add(MakeShared<FJsonValueString>(TEXT("removed ") + Ref));
    }
    AddRemovedBindingEffects(Context, RemovedBindings);
    AddDanglingStateLinkEffects(Context, RemovedStateIds);
    if (!ValidateBindingExecutionVisibility(Context, Error))
    {
        return Context.Fail(
            TEXT("validation.operation_arguments_invalid"),
            Error,
            TEXT("remove"),
            Context.PublicRef(Kind, Id));
    }
    AddLifecyclePlan(
        Context,
        TEXT("remove"),
        Context.PublicRef(Kind, Id),
        FString::Printf(
            TEXT("removed %s; authored cascade: %d"),
            *Context.PublicRef(Kind, Id),
            RemovedRefs.Num()));
    return true;
}

bool PathsEqual(const FPropertyBindingPath& Left, const FPropertyBindingPath& Right)
{
    return Left == Right;
}

struct FBindingMatches
{
    int32 ExactCount = 0;
    int32 TargetCount = 0;
    int32 ExactIndex = INDEX_NONE;
    bool bExactStoredOutput = false;
};

FBindingMatches FindBindingMatches(
    const UStateTreeEditorData& Data,
    const FPropertyBindingPath& NativeSource,
    const FPropertyBindingPath& NativeTarget)
{
    FBindingMatches Result;
    const TConstArrayView<FStateTreePropertyPathBinding> Bindings = Data.EditorBindings.GetBindings();
    for (int32 Index = 0; Index < Bindings.Num(); ++Index)
    {
        const FStateTreePropertyPathBinding& Binding = Bindings[Index];
        if (PathsEqual(Binding.GetTargetPath(), NativeTarget))
        {
            ++Result.TargetCount;
            if (PathsEqual(Binding.GetSourcePath(), NativeSource))
            {
                ++Result.ExactCount;
                Result.ExactIndex = Index;
                Result.bExactStoredOutput = Binding.IsOutputBinding();
            }
        }
    }
    return Result;
}

bool NotifyTargetNodeBindingChangedWithManifest(
    FPatchContext& Context,
    const FPropertyBindingPath& SourcePath,
    const FPropertyBindingPath& TargetPath,
    FString& OutError)
{
    OutError.Reset();
    FAuthoredIndex Index = Context.MakeIndex();
    if (!Index.IsValid())
    {
        OutError = Index.GetError();
        return false;
    }
    FNodeRef* TargetNode = Index.FindNodeByNativeStructId(TargetPath.GetStructID());
    if (TargetNode == nullptr
        || TargetNode->Node == nullptr
        || TargetNode->Node->ID != TargetPath.GetStructID())
    {
        return true;
    }
    FStateTreeNodeBase* Node = TargetNode->Node->Node.GetMutablePtr<FStateTreeNodeBase>();
    FStateTreeDataView InstanceView = TargetNode->Node->GetInstance();
    if (Node == nullptr || !InstanceView.IsValid())
    {
        return true;
    }

    TArray<FCompileManifestEntry> BeforeManifest;
    if (!CaptureCompileManifest(
            Context.Tree,
            Context.Data,
            Context.Identities,
            BeforeManifest,
            OutError))
    {
        return false;
    }
    const uint32 BeforeHash =
        UStateTreeEditingSubsystem::CalculateStateTreeHash(&Context.Tree);

    if (TargetNode->OwnerState != nullptr)
    {
        TargetNode->OwnerState->Modify();
    }
    else
    {
        Context.Data.Modify();
    }
    const FStateTreeBindingLookup Lookup(&Context.Data);
    Node->OnBindingChanged(TargetNode->Node->ID, InstanceView, SourcePath, TargetPath, Lookup);

    TArray<FCompileManifestEntry> AfterManifest;
    if (!CaptureCompileManifest(
            Context.Tree,
            Context.Data,
            Context.Identities,
            AfterManifest,
            OutError))
    {
        return false;
    }
    const TSet<FString> NoDetailedContent;
    const TSet<FString> NoDetailedRemoval;
    bool bManifestChanged = false;
    AddNativeCallbackManifestEffects(
        Context,
        TEXT("native Binding callback"),
        BeforeManifest,
        AfterManifest,
        NoDetailedContent,
        NoDetailedRemoval,
        bManifestChanged);

    const uint32 AfterHash =
        UStateTreeEditingSubsystem::CalculateStateTreeHash(&Context.Tree);
    if (BeforeHash != AfterHash && !bManifestChanged)
    {
        OutError = TEXT("A native Binding callback changed authored StateTree data outside the complete authored manifest.");
        return false;
    }
    return true;
}

bool ReadLocalFunctionEndpoint(
    const TSharedPtr<FJsonObject>& Ref,
    FString& OutAlias,
    TArray<FString>& OutPath)
{
    OutAlias.Reset();
    OutPath.Reset();
    TSharedPtr<FJsonObject> Owner;
    const TArray<TSharedPtr<FJsonValue>>* Path = nullptr;
    FString OwnerKind;
    FString OwnerId;
    if (!ReadMember(Ref, Owner, Path)
        || !ReadRefObject(Owner, OwnerKind, OwnerId, OutAlias)
        || OwnerKind != TEXT("local")
        || Path == nullptr)
    {
        return false;
    }
    for (const TSharedPtr<FJsonValue>& Segment : *Path)
    {
        FString Text;
        if (!Segment.IsValid() || !Segment->TryGetString(Text) || Text.IsEmpty())
        {
            return false;
        }
        OutPath.Add(Text);
    }
    return true;
}

bool MaterializePropertyFunction(
    FPatchContext& Context,
    const TSharedPtr<FJsonObject>& From,
    const TSharedPtr<FJsonObject>& To,
    FString& OutAlias,
    FString& OutError,
    int32& OutReplacedBindings)
{
    TArray<FString> PublicPath;
    if (!ReadLocalFunctionEndpoint(From, OutAlias, PublicPath))
    {
        return false;
    }
    FConstructorDefinition* Definition = Context.Definitions.Find(OutAlias);
    if (Definition == nullptr || Definition->Callee != TEXT("node") || Definition->bConsumed)
    {
        OutError = TEXT("Property Function result Binding requires one preceding unconsumed local node constructor.");
        return false;
    }
    const TSharedPtr<FJsonObject> ReboundTarget = Context.RebindRef(To, OutError);
    if (!ReboundTarget.IsValid())
    {
        return false;
    }
    StateTreePalette::FDestination Destination;
    if (!StateTreePalette::ResolveDestination(
            Context.Tree,
            Context.Data,
            ReboundTarget,
            Destination,
            OutError)
        || Destination.Role != StateTreePalette::EDestinationRole::PropertyFunction
        || !Destination.BindingTarget.IsValid())
    {
        if (OutError.IsEmpty())
        {
            OutError = TEXT("Property Function constructor is not valid for this exact Binding target.");
        }
        return false;
    }
    StateTreePalette::FEntry Entry;
    if (!StateTreePalette::ResolveEntry(
            Context.Tree,
            Context.Data,
            Destination,
            Definition->PaletteId,
            Entry,
            OutError)
        || !Entry.bSpawnable
        || Entry.ConstructorKind != StateTreePalette::EConstructorKind::Node
        || Entry.DestinationRole != StateTreePalette::EDestinationRole::PropertyFunction
        || Entry.NodeStruct == nullptr
        || Entry.PropertyFunctionOutput == nullptr)
    {
        if (OutError.IsEmpty())
        {
            OutError = TEXT("Exact Property Function Palette capability is stale or not spawnable.");
        }
        return false;
    }
    if (PublicPath.Num() != 2
        || PublicPath[0] != TEXT("Instance")
        || PublicPath[1] != Entry.PropertyFunctionOutput->GetName())
    {
        OutError = FString::Printf(
            TEXT("Property Function owning Binding must use Instance.%s from exact Palette schema."),
            *Entry.PropertyFunctionOutput->GetName());
        return false;
    }

    const TArray<FBindingSnapshot> ExistingBindings = CaptureBindingSnapshot(Context);
    // A Property Function result is an ordinary target-owned Binding.
    for (const FStateTreePropertyPathBinding& Binding : Context.Data.EditorBindings.GetBindings())
    {
        if (!PathsEqual(Binding.GetTargetPath(), Destination.BindingTarget->NativePath))
        {
            continue;
        }
        ++OutReplacedBindings;
        if (const FBindingSnapshot* Existing = FindBindingSnapshot(
                ExistingBindings,
                Binding.GetSourcePath(),
                Binding.GetTargetPath(),
                Binding.IsOutputBinding()))
        {
            Context.PlannedEffects.Add(MakeShared<FJsonValueString>(
                TEXT("replaced ") + Existing->Evidence));
        }
    }
    Context.Data.Modify();
    FPropertyBindingPathSegment OutputSegment(Entry.PropertyFunctionOutput->GetFName());
    FPropertyBindingPath SourcePath = Context.Data.EditorBindings.AddFunctionBinding(
        Entry.NodeStruct,
        MakeArrayView(&OutputSegment, 1),
        Destination.BindingTarget->NativePath);
    if (!SourcePath.GetStructID().IsValid() || SourcePath.GetSegments().IsEmpty())
    {
        OutError = TEXT("UE failed to materialize the Property Function result Binding.");
        return false;
    }
    const FGuid GeneratedId = SourcePath.GetStructID();
    FStateTreeEditorNode* FunctionNode = nullptr;
    for (FStateTreePropertyPathBinding& Binding : Context.Data.EditorBindings.GetMutableBindings())
    {
        if (Binding.GetSourcePath().GetStructID() != GeneratedId)
        {
            continue;
        }
        FunctionNode = Binding.GetMutablePropertyFunctionNode().GetPtr<FStateTreeEditorNode>();
        if (FunctionNode != nullptr)
        {
            FunctionNode->ID = Definition->PlannedId;
            Binding.GetMutableSourcePath().SetStructID(Definition->PlannedId);
            break;
        }
    }
    if (FunctionNode == nullptr)
    {
        OutError = TEXT("UE created no inspectable Property Function Node for the result Binding.");
        return false;
    }
    TArray<FString> RemovedFunctionInputs;
    if (!CleanupNewlyInvalidBindings(Context, ExistingBindings, RemovedFunctionInputs))
    {
        OutError = TEXT("Property Function replacement cleanup exceeded its bounded pass limit.");
        return false;
    }
    AddRemovedBindingEffects(Context, RemovedFunctionInputs);
    Context.CreatedRefs.Add(OutAlias, {TEXT("node"), GuidText(Definition->PlannedId)});
    Context.ResolvedRefs->SetStringField(OutAlias, TEXT("node@") + GuidText(Definition->PlannedId));
    Definition->bConsumed = true;
    if (!ApplyConstructorFields(Context, *Definition, Context.CreatedRefs.FindChecked(OutAlias)))
    {
        OutError = TEXT("Property Function constructor fields could not be applied.");
        return false;
    }
    return true;
}

bool ValidateActiveBindingEndpoints(
    FPatchContext& Context,
    const StateTreeSchema::FResolvedMember& Source,
    const StateTreeSchema::FResolvedMember& Target,
    FString& OutError)
{
    FAuthoredIndex Index = Context.MakeIndex();
    if (!Index.IsValid())
    {
        OutError = Index.GetError();
        return false;
    }
    if (Source.OwnerKind == TEXT("state")
        && !Source.SalPath.IsEmpty()
        && Source.SalPath[0] == TEXT("RequiredEventToEnter"))
    {
        FGuid Id;
        FStateRef* State = ParseGuid(Source.OwnerId, Id) ? Index.FindState(Id) : nullptr;
        if (State == nullptr || !State->State->bHasRequiredEventToEnter)
        {
            OutError = TEXT("A new Binding cannot use an inactive State RequiredEventToEnter source; enable bHasRequiredEventToEnter first.");
            return false;
        }
    }
    if (Source.OwnerKind == TEXT("transition")
        && !Source.SalPath.IsEmpty()
        && Source.SalPath[0] == TEXT("RequiredEvent"))
    {
        FGuid Id;
        FTransitionRef* Transition = ParseGuid(Source.OwnerId, Id) ? Index.FindTransition(Id) : nullptr;
        if (Transition == nullptr || Transition->Transition->Trigger != EStateTreeTransitionTrigger::OnEvent)
        {
            OutError = TEXT("A new Binding cannot use an inactive Transition RequiredEvent source; set Trigger to OnEvent first.");
            return false;
        }
    }
    if (Target.OwnerKind == TEXT("transition")
        && !Target.SalPath.IsEmpty()
        && Target.SalPath[0] == TEXT("DelegateListener"))
    {
        FGuid Id;
        FTransitionRef* Transition = ParseGuid(Target.OwnerId, Id) ? Index.FindTransition(Id) : nullptr;
        if (Transition == nullptr || Transition->Transition->Trigger != EStateTreeTransitionTrigger::OnDelegate)
        {
            OutError = TEXT("A new Delegate Binding requires Transition.Trigger OnDelegate.");
            return false;
        }
    }
    return true;
}

FString PublicResolvedMember(
    FPatchContext& Context,
    const StateTreeSchema::FResolvedMember& Member)
{
    FString Result = Member.OwnerKind == TEXT("target")
        ? Context.TargetAlias
        : Context.PublicRef(Member.OwnerKind, Member.OwnerId);
    for (const FString& Segment : Member.SalPath)
    {
        if (!Segment.IsEmpty() && Segment.IsNumeric())
        {
            Result += TEXT("[") + Segment + TEXT("]");
        }
        else
        {
            Result += TEXT(".") + Segment;
        }
    }
    return Result;
}

FString AutomaticContextEdge(
    FPatchContext& Context,
    const StateTreeSchema::FResolvedMember& Target)
{
    if (Target.Usage != EStateTreePropertyUsage::Context || Target.RootProperty == nullptr)
    {
        return FString();
    }
    const UStruct* ContextType = nullptr;
    if (const FStructProperty* StructProperty = CastField<FStructProperty>(Target.RootProperty))
    {
        ContextType = StructProperty->Struct;
    }
    else if (const FObjectPropertyBase* ObjectProperty = CastField<FObjectPropertyBase>(Target.RootProperty))
    {
        ContextType = ObjectProperty->PropertyClass;
    }
    if (ContextType == nullptr)
    {
        return FString();
    }
    const FStateTreeBindableStructDesc ContextDesc = Context.Data.FindContextData(
        ContextType,
        Target.RootProperty->GetName());
    const FStateTreeExternalDataDesc* Canonical = nullptr;
    FString ResolveError;
    if (!ContextDesc.IsValid()
        || !StateTreeSchema::ResolveCanonicalContext(
            Context.Data,
            ContextDesc.ID,
            Canonical,
            ResolveError)
        || Canonical == nullptr)
    {
        return FString();
    }
    return TEXT("object@") + GuidText(Canonical->ID)
        + TEXT(" -> ")
        + PublicResolvedMember(Context, Target);
}

bool HandleBindUnbind(
    FPatchContext& Context,
    const TSharedPtr<FJsonObject>& Operation,
    const bool bBind)
{
    const TSharedPtr<FJsonObject>* FromRef = nullptr;
    const TSharedPtr<FJsonObject>* ToRef = nullptr;
    if (!Operation->TryGetObjectField(TEXT("from"), FromRef)
        || FromRef == nullptr
        || !Operation->TryGetObjectField(TEXT("to"), ToRef)
        || ToRef == nullptr)
    {
        return Context.Fail(
            TEXT("validation.operation_arguments_invalid"),
            TEXT("StateTree bind/unbind requires one exact from and to endpoint."),
            bBind ? TEXT("bind") : TEXT("unbind"));
    }
    FString Error;
    FString FunctionAlias;
    TArray<FString> FunctionPath;
    int32 ReplacedBindings = 0;
    const bool bLocalFunction = bBind && ReadLocalFunctionEndpoint(*FromRef, FunctionAlias, FunctionPath);
    if (bBind && bLocalFunction
        && !MaterializePropertyFunction(
            Context,
            *FromRef,
            *ToRef,
            FunctionAlias,
            Error,
            ReplacedBindings))
    {
        return Context.Fail(TEXT("validation.creation_invalid"), Error, TEXT("bind"), FunctionAlias);
    }

    StateTreeSchema::FResolvedMember From;
    StateTreeSchema::FResolvedMember To;
    if (!Context.ResolveMemberEndpoint(
            *FromRef,
            From,
            Error,
            StateTreeSchema::EMemberPurpose::BindingSource))
    {
        return Context.Fail(TEXT("resolution.property_not_found"), Error, bBind ? TEXT("bind") : TEXT("unbind"));
    }
    if (!Context.ResolveMemberEndpoint(
            *ToRef,
            To,
            Error,
            StateTreeSchema::EMemberPurpose::BindingTarget))
    {
        return Context.Fail(TEXT("resolution.property_not_found"), Error, bBind ? TEXT("bind") : TEXT("unbind"));
    }
    if (bBind && !ValidateActiveBindingEndpoints(Context, From, To, Error))
    {
        return Context.Fail(TEXT("validation.operation_arguments_invalid"), Error, TEXT("bind"));
    }
    if (bBind && !StateTreeSchema::AreBindingCompatible(Context.Data, From, To, Error))
    {
        return Context.Fail(TEXT("validation.operation_arguments_invalid"), Error, bBind ? TEXT("bind") : TEXT("unbind"));
    }
    const bool bOutput = From.Usage == EStateTreePropertyUsage::Output
        && !From.bPropertyFunctionOwner;
    const FPropertyBindingPath& NativeSource = bOutput ? To.NativePath : From.NativePath;
    const FPropertyBindingPath& NativeTarget = bOutput ? From.NativePath : To.NativePath;
    const FBindingMatches Matches = FindBindingMatches(
        Context.Data,
        NativeSource,
        NativeTarget);
    if (Matches.ExactCount > 1)
    {
        return Context.Fail(
            TEXT("validation.patch_state_invalid"),
            TEXT("StateTree contains duplicate exact Property Bindings."),
            bBind ? TEXT("bind") : TEXT("unbind"));
    }
    const FString ContextEdge = AutomaticContextEdge(Context, To);
    const TArray<FBindingSnapshot> BindingBefore = CaptureBindingSnapshot(Context);

    Context.Tree.Modify();
    Context.Data.Modify();
    if (bBind)
    {
        if (bLocalFunction)
        {
            // AddFunctionBinding already authored the exact owning result edge.
            if (Matches.ExactCount != 1)
            {
                return Context.Fail(
                    TEXT("validation.patch_state_invalid"),
                    TEXT("UE did not preserve the exact Property Function owning Binding."),
                    TEXT("bind"));
            }
        }
        else if (Matches.ExactCount == 1
            && Matches.bExactStoredOutput == bOutput
            && (bOutput || Matches.TargetCount == 1))
        {
            Context.PlannedEffects.Add(MakeShared<FJsonValueString>(TEXT("Binding already exists; no authored change")));
            return true;
        }
        else if (bOutput)
        {
            if (Matches.ExactCount == 1)
            {
                if (const FBindingSnapshot* Existing = FindBindingSnapshot(
                        BindingBefore,
                        NativeSource,
                        NativeTarget,
                        Matches.bExactStoredOutput))
                {
                    Context.PlannedEffects.Add(MakeShared<FJsonValueString>(
                        TEXT("repaired stored output direction for ") + Existing->Evidence));
                }
                Context.Data.EditorBindings.RemoveBindings(
                    [&](FPropertyBindingBinding& Binding)
                    {
                        return PathsEqual(Binding.GetSourcePath(), NativeSource)
                            && PathsEqual(Binding.GetTargetPath(), NativeTarget);
                    });
            }
            if (Context.Data.EditorBindings.AddOutputBinding(NativeSource, NativeTarget) == nullptr)
            {
                return Context.Fail(TEXT("validation.patch_state_invalid"), TEXT("UE rejected the output Property Binding."), TEXT("bind"));
            }
        }
        else
        {
            for (const FStateTreePropertyPathBinding& Binding : Context.Data.EditorBindings.GetBindings())
            {
                if (!PathsEqual(Binding.GetTargetPath(), NativeTarget))
                {
                    continue;
                }
                if (const FBindingSnapshot* Existing = FindBindingSnapshot(
                        BindingBefore,
                        Binding.GetSourcePath(),
                        Binding.GetTargetPath(),
                        Binding.IsOutputBinding()))
                {
                    Context.PlannedEffects.Add(MakeShared<FJsonValueString>(
                        TEXT("replaced ") + Existing->Evidence));
                }
            }
            ReplacedBindings += Matches.TargetCount;
            Context.Data.EditorBindings.AddBinding(NativeSource, NativeTarget);
        }
        if (!NotifyTargetNodeBindingChangedWithManifest(
                Context,
                NativeSource,
                NativeTarget,
                Error))
        {
            return Context.Fail(
                TEXT("validation.patch_state_invalid"),
                Error,
                TEXT("bind"));
        }
        const bool bSuppressedAutomaticContext = !ContextEdge.IsEmpty()
            && (bLocalFunction ? ReplacedBindings == 0 : Matches.TargetCount == 0);
        if (bSuppressedAutomaticContext)
        {
            Context.PlannedEffects.Add(MakeShared<FJsonValueString>(
                TEXT("suppressed automatic Context ") + ContextEdge));
            Context.ResultComments.Add(TEXT("suppressed automatic Context: ") + ContextEdge);
        }
        if (ReplacedBindings > 0)
        {
            Context.PlannedEffects.Add(MakeShared<FJsonValueString>(FString::Printf(
                TEXT("replaced Bindings: %d"),
                ReplacedBindings)));
        }
        AddLifecyclePlan(
            Context,
            TEXT("bind"),
            Context.PublicRef(From.OwnerKind, From.OwnerId),
            TEXT("authored Property Binding"));
        return true;
    }

    if (Matches.ExactCount == 0)
    {
        return Context.Fail(
            TEXT("resolution.binding_not_found"),
            Matches.TargetCount > 0
                ? TEXT("An authored Binding reaches this target, but its source does not match the exact unbind pair.")
                : TEXT("No explicit authored Binding matches this pair; automatic Context relationships cannot be unbound."),
            TEXT("unbind"));
    }
    const bool bOwnedFunction = Context.Data.EditorBindings.GetBindings()[Matches.ExactIndex]
        .GetPropertyFunctionNode().IsValid();
    const FBindingSnapshot* ExactBefore = FindBindingSnapshot(
        BindingBefore,
        NativeSource,
        NativeTarget,
        Matches.bExactStoredOutput);
    Context.Data.EditorBindings.RemoveBindings(
        [&](FPropertyBindingBinding& Binding)
        {
            return PathsEqual(Binding.GetSourcePath(), NativeSource)
                && PathsEqual(Binding.GetTargetPath(), NativeTarget);
        });
    TArray<FString> Cascade;
    if (!CleanupNewlyInvalidBindings(Context, BindingBefore, Cascade))
    {
        return Context.Fail(
            TEXT("validation.patch_state_invalid"),
            TEXT("Property Function Binding cleanup exceeded its bounded pass limit."),
            TEXT("unbind"));
    }
    const FPropertyBindingPath EmptySource;
    if (!NotifyTargetNodeBindingChangedWithManifest(
            Context,
            EmptySource,
            NativeTarget,
            Error))
    {
        return Context.Fail(
            TEXT("validation.patch_state_invalid"),
            Error,
            TEXT("unbind"));
    }
    if (ExactBefore != nullptr)
    {
        Context.PlannedEffects.Add(MakeShared<FJsonValueString>(
            TEXT("removed ") + ExactBefore->Evidence));
    }
    AddRemovedBindingEffects(Context, Cascade);
    if (bOwnedFunction)
    {
        Context.PlannedEffects.Add(MakeShared<FJsonValueString>(
            TEXT("removed owning Property Function subtree")));
    }
    if (!ContextEdge.IsEmpty())
    {
        const FBindingMatches Remaining = FindBindingMatches(
            Context.Data,
            NativeSource,
            NativeTarget);
        if (Remaining.TargetCount == 0)
        {
            Context.PlannedEffects.Add(MakeShared<FJsonValueString>(
                TEXT("restored automatic Context ") + ContextEdge));
            Context.ResultComments.Add(TEXT("restored automatic Context: ") + ContextEdge);
        }
    }
    AddLifecyclePlan(
        Context,
        TEXT("unbind"),
        Context.PublicRef(From.OwnerKind, From.OwnerId),
        TEXT("removed exact Property Binding"));
    return true;
}

bool ReadStatementKind(const TSharedPtr<FJsonObject>& Statement, FString& OutKind)
{
    OutKind.Reset();
    return Statement.IsValid()
        && (!Statement->HasField(TEXT("kind"))
            || Statement->TryGetStringField(TEXT("kind"), OutKind));
}

bool ValidateTerminalSequence(
    const TArray<TSharedPtr<FJsonValue>>& Statements,
    bool& bOutCompile,
    bool& bOutSave)
{
    bOutCompile = false;
    bOutSave = false;
    if (Statements.IsEmpty() || Statements.Num() > 2)
    {
        return false;
    }
    for (int32 Index = 0; Index < Statements.Num(); ++Index)
    {
        TSharedPtr<FJsonObject> Statement;
        FString Kind;
        if (!ReadObject(Statements[Index], Statement)
            || !ReadStatementKind(Statement, Kind)
            || Kind.IsEmpty())
        {
            return false;
        }
        if (Kind == TEXT("compile") && Index == 0 && !bOutCompile)
        {
            bOutCompile = true;
        }
        else if (Kind == TEXT("save") && Index == Statements.Num() - 1 && !bOutSave)
        {
            bOutSave = true;
        }
        else
        {
            return false;
        }
    }
    return bOutCompile || bOutSave;
}

struct FInspectableStateTreeCompilerLog : FStateTreeCompilerLog
{
    const TArray<FStateTreeCompilerLogMessage>& GetMessages() const
    {
        return Messages;
    }
};

struct FCompileStateSnapshot
{
    FGuid StableId;
    FName LinkedSubtreeName;
    bool bLinkedParameters = false;
    FStateTreeStateParameters Parameters;
};

struct FCompileTransitionSnapshot
{
    FGuid StableId;
    FName StateName;
};

bool CaptureCompileManifest(
    UStateTree& Tree,
    UStateTreeEditorData& Data,
    const FIdentityMap& Identities,
    TArray<FCompileManifestEntry>& OutManifest,
    FString& OutError)
{
    FAuthoredIndex Index(Tree, Data, Identities);
    if (!Index.IsValid())
    {
        OutError = Index.GetError();
        return false;
    }
    constexpr int32 MaxManifestEntries = 50000;
    int64 Budget = 0;
    OutManifest.Reset();
    const auto Add = [&](FCompileManifestEntry&& Entry) -> bool
    {
        if (OutManifest.Num() >= MaxManifestEntries)
        {
            OutError = TEXT("Compile authored manifest exceeded its 50,000 object hard limit.");
            return false;
        }
        OutManifest.Add(MoveTemp(Entry));
        return true;
    };

    TSet<FName> TargetExcluded = {
        GET_MEMBER_NAME_CHECKED(UStateTreeEditorData, Schema),
        GET_MEMBER_NAME_CHECKED(UStateTreeEditorData, EditorSchema),
        GET_MEMBER_NAME_CHECKED(UStateTreeEditorData, Extensions),
        GET_MEMBER_NAME_CHECKED(UStateTreeEditorData, SubTrees),
        GET_MEMBER_NAME_CHECKED(UStateTreeEditorData, Evaluators),
        GET_MEMBER_NAME_CHECKED(UStateTreeEditorData, GlobalTasks),
        GET_MEMBER_NAME_CHECKED(UStateTreeEditorData, EditorBindings),
    };
    FString TargetFingerprint;
    if (!FingerprintNodeView(
            FPropertyBindingDataView(&Data),
            Budget,
            TargetFingerprint,
            OutError,
            &TargetExcluded)
        || !Add({TEXT("target"), TEXT("asset"), TEXT("EditorData"), INDEX_NONE, MoveTemp(TargetFingerprint)}))
    {
        return false;
    }

    const auto AddInstancedObject = [&](
        const FString& Key,
        const FString& Role,
        const int32 Order,
        UObject* Object) -> bool
    {
        FString Content;
        return FingerprintNodeView(
                FPropertyBindingDataView(Object),
                Budget,
                Content,
                OutError)
            && Add({Key, TEXT("target"), Role, Order, MoveTemp(Content)});
    };
    if (!AddInstancedObject(
            TEXT("schema"),
            TEXT("Schema"),
            INDEX_NONE,
            Data.Schema.Get())
        || !AddInstancedObject(
            TEXT("editor_schema"),
            TEXT("EditorSchema"),
            INDEX_NONE,
            Data.EditorSchema.Get()))
    {
        return false;
    }
    for (int32 ExtensionIndex = 0;
         ExtensionIndex < Data.Extensions.Num();
         ++ExtensionIndex)
    {
        if (!AddInstancedObject(
                FString::Printf(TEXT("editor_extension@%d"), ExtensionIndex),
                TEXT("Extensions"),
                ExtensionIndex,
                Data.Extensions[ExtensionIndex].Get()))
        {
            return false;
        }
    }

    const TConstArrayView<FStateTreePropertyPathBinding> Bindings =
        Data.EditorBindings.GetBindings();
    for (int32 BindingIndex = 0; BindingIndex < Bindings.Num(); ++BindingIndex)
    {
        const FStateTreePropertyPathBinding& Binding = Bindings[BindingIndex];
        FFramedSha1 Content;
        const auto AddBindingField = [&](
            const FStringView Type,
            const FString& Value) -> bool
        {
            Budget += Value.Len();
            if (Budget > 16 * 1024 * 1024)
            {
                OutError = TEXT("Compile authored manifest exceeded its 16 MiB text budget.");
                return false;
            }
            Content.Add(Type, Value);
            return true;
        };
        const auto AddPath = [&](
            const FString& Prefix,
            const FPropertyBindingPath& Path) -> bool
        {
            if (!AddBindingField(
                    Prefix + TEXT("_struct_id"),
                    GuidText(Identities.RestoreNativeStruct(Path.GetStructID()))))
            {
                return false;
            }
            const TConstArrayView<FPropertyBindingPathSegment> Segments =
                Path.GetSegments();
            for (int32 SegmentIndex = 0;
                 SegmentIndex < Segments.Num();
                 ++SegmentIndex)
            {
                const FPropertyBindingPathSegment& Segment = Segments[SegmentIndex];
                const FString SegmentPrefix = FString::Printf(
                    TEXT("%s_segment_%d"),
                    *Prefix,
                    SegmentIndex);
                if (!AddBindingField(
                        SegmentPrefix + TEXT("_name"),
                        Segment.GetName().ToString())
                    || !AddBindingField(
                        SegmentPrefix + TEXT("_array_index"),
                        FString::FromInt(Segment.GetArrayIndex()))
                    || !AddBindingField(
                        SegmentPrefix + TEXT("_instance_type"),
                        GetPathNameSafe(Segment.GetInstanceStruct()))
                    || !AddBindingField(
                        SegmentPrefix + TEXT("_access"),
                        FString::FromInt(static_cast<int32>(
                            Segment.GetInstancedStructAccessType()))))
                {
                    return false;
                }
            }
            return true;
        };
        if (!AddPath(TEXT("source"), Binding.GetSourcePath())
            || !AddPath(TEXT("target"), Binding.GetTargetPath())
            || !AddBindingField(
                TEXT("output"),
                Binding.IsOutputBinding() ? TEXT("true") : TEXT("false")))
        {
            return false;
        }
        const FConstStructView FunctionView = Binding.GetPropertyFunctionNode();
        const FStateTreeEditorNode* Function =
            FunctionView.GetPtr<const FStateTreeEditorNode>();
        if (!AddBindingField(
                TEXT("property_function_type"),
                GetPathNameSafe(FunctionView.GetScriptStruct()))
            || !AddBindingField(
                TEXT("property_function_id"),
                Function != nullptr && Function->ID.IsValid()
                    ? GuidText(Identities.RestoreNode(Function->ID))
                    : TEXT("none"))
            || !Add({
                FString::Printf(TEXT("EditorBindings[%08d]"), BindingIndex),
                TEXT("target"),
                TEXT("EditorBindings"),
                BindingIndex,
                Content.Finalize()}))
        {
            return false;
        }
    }

    TArray<FStateRef*> States;
    Index.GetAllStates(States);
    for (const FStateRef* Ref : States)
    {
        TSet<FName> Excluded = {
            GET_MEMBER_NAME_CHECKED(UStateTreeState, Children),
            GET_MEMBER_NAME_CHECKED(UStateTreeState, EnterConditions),
            GET_MEMBER_NAME_CHECKED(UStateTreeState, Tasks),
            GET_MEMBER_NAME_CHECKED(UStateTreeState, SingleTask),
            GET_MEMBER_NAME_CHECKED(UStateTreeState, Considerations),
            GET_MEMBER_NAME_CHECKED(UStateTreeState, Transitions),
            GET_MEMBER_NAME_CHECKED(UStateTreeState, ID),
            GET_MEMBER_NAME_CHECKED(UStateTreeState, Parent),
        };
        FString Content;
        if (!FingerprintNodeView(
                FPropertyBindingDataView(Ref->State),
                Budget,
                Content,
                OutError,
                &Excluded))
        {
            return false;
        }
        const FGuid StableId = Identities.RestoreState(Ref->State->ID);
        const FString Owner = Ref->Parent != nullptr
            ? TEXT("state@") + GuidText(Identities.RestoreState(Ref->Parent->ID))
            : TEXT("target");
        if (!Add({
                TEXT("state@") + GuidText(StableId),
                Owner,
                Ref->Parent != nullptr ? TEXT("Children") : TEXT("SubTrees"),
                Ref->Index,
                MoveTemp(Content)}))
        {
            return false;
        }
    }

    TArray<FTransitionRef*> Transitions;
    Index.GetAllTransitions(Transitions);
    for (const FTransitionRef* Ref : Transitions)
    {
        TSet<FName> Excluded = {
            GET_MEMBER_NAME_CHECKED(FStateTreeTransition, Conditions),
            GET_MEMBER_NAME_CHECKED(FStateTreeTransition, ID),
        };
        FString Content;
        if (!FingerprintNodeView(
                FPropertyBindingDataView(
                    FStateTreeTransition::StaticStruct(),
                    Ref->Transition),
                Budget,
                Content,
                OutError,
                &Excluded)
            || !Add({
                TEXT("transition@")
                    + GuidText(Identities.RestoreTransition(Ref->Transition->ID)),
                TEXT("state@")
                    + GuidText(Identities.RestoreState(Ref->OwnerState->ID)),
                TEXT("Transitions"),
                Ref->Index,
                MoveTemp(Content)}))
        {
            return false;
        }
    }

    TArray<FNodeRef*> Nodes;
    Index.GetAllNodes(Nodes);
    for (const FNodeRef* Ref : Nodes)
    {
        FString NodeContent;
        FString InstanceContent;
        FString RuntimeContent;
        if (!FingerprintNodeView(
                FPropertyBindingDataView(
                    Ref->Node->Node.GetScriptStruct(),
                    Ref->Node->Node.GetMutableMemory()),
                Budget,
                NodeContent,
                OutError)
            || !FingerprintNodeView(
                Ref->Node->GetInstance(),
                Budget,
                InstanceContent,
                OutError)
            || !FingerprintNodeView(
                Ref->Node->GetExecutionRuntimeData(),
                Budget,
                RuntimeContent,
                OutError))
        {
            return false;
        }
        FString Owner = TEXT("target");
        if (Ref->OwnerTransition != nullptr)
        {
            Owner = TEXT("transition@")
                + GuidText(Identities.RestoreTransition(Ref->OwnerTransition->ID));
        }
        else if (Ref->OwnerState != nullptr)
        {
            Owner = TEXT("state@")
                + GuidText(Identities.RestoreState(Ref->OwnerState->ID));
        }
        else if (Ref->bPropertyFunction)
        {
            Owner = TEXT("binding");
        }
        const int32 Order = Ref->bPropertyFunction ? Ref->BindingIndex : Ref->Index;
        FFramedSha1 NodeFingerprint;
        NodeFingerprint.Add(TEXT("node"), NodeContent);
        NodeFingerprint.Add(TEXT("instance"), InstanceContent);
        NodeFingerprint.Add(TEXT("execution_runtime_data"), RuntimeContent);
        if (!Add({
                TEXT("node@") + GuidText(Identities.RestoreNode(Ref->Node->ID)),
                Owner,
                FString(NodeRoleMember(Ref->Role)),
                Order,
                NodeFingerprint.Finalize()}))
        {
            return false;
        }
    }

    TArray<FParameterRef*> Parameters;
    Index.GetAllParameters(Parameters);
    for (const FParameterRef* Ref : Parameters)
    {
        FFramedSha1 Content;
        const auto AddParameterField = [&](
            const FStringView Type,
            const FString& Value) -> bool
        {
            Budget += Value.Len();
            if (Budget > 16 * 1024 * 1024)
            {
                OutError = TEXT("Compile authored manifest exceeded its 16 MiB text budget.");
                return false;
            }
            Content.Add(Type, Value);
            return true;
        };
        if (!AddParameterField(TEXT("name"), Ref->Desc.Name.ToString())
            || !AddParameterField(
                TEXT("value_type"),
                FString::FromInt(static_cast<int32>(Ref->Desc.ValueType)))
            || !AddParameterField(
                TEXT("value_type_object"),
                GetPathNameSafe(Ref->Desc.ValueTypeObject.Get()))
            || !AddParameterField(
                TEXT("property_flags"),
                FString::Printf(
                    TEXT("%llu"),
                    static_cast<unsigned long long>(Ref->Desc.PropertyFlags))))
        {
            return false;
        }
        for (uint32 ContainerIndex = 0;
             ContainerIndex < Ref->Desc.ContainerTypes.Num();
             ++ContainerIndex)
        {
            const EPropertyBagContainerType Container =
                Ref->Desc.ContainerTypes[ContainerIndex];
            if (!AddParameterField(
                    TEXT("container_type"),
                    FString::FromInt(static_cast<int32>(Container))))
            {
                return false;
            }
        }
        TArray<FPropertyBagPropertyDescMetaData> MetaData = Ref->Desc.MetaData;
        MetaData.Sort(
            [](const FPropertyBagPropertyDescMetaData& Left,
               const FPropertyBagPropertyDescMetaData& Right)
            {
                return Left.Key == Right.Key
                    ? Left.Value < Right.Value
                    : Left.Key.LexicalLess(Right.Key);
            });
        for (const FPropertyBagPropertyDescMetaData& Item : MetaData)
        {
            if (!AddParameterField(TEXT("metadata_key"), Item.Key.ToString())
                || !AddParameterField(TEXT("metadata_value"), Item.Value))
            {
                return false;
            }
        }
        if (Ref->Desc.CachedProperty != nullptr && Ref->Bag != nullptr)
        {
            FString Value;
            const void* Address = Ref->Desc.CachedProperty->ContainerPtrToValuePtr<void>(
                Ref->Bag->GetValue().GetMemory());
            Ref->Desc.CachedProperty->ExportTextItem_Direct(
                Value,
                Address,
                nullptr,
                nullptr,
                PPF_None);
            if (!AddParameterField(TEXT("value"), Value))
            {
                return false;
            }
        }
        const FString StableId = Identities.RestoreParameter(
            GuidText(Ref->ContainerId) + TEXT("/") + GuidText(Ref->Desc.ID));
        const FString Owner = Ref->OwnerState != nullptr
            ? TEXT("state@") + GuidText(Identities.RestoreState(Ref->OwnerState->ID))
            : TEXT("target");
        if (!Add({
                TEXT("parameter@") + StableId,
                Owner,
                Ref->bRoot ? TEXT("RootParameters") : TEXT("Parameters"),
                Ref->Index,
                Content.Finalize()}))
        {
            return false;
        }
    }

    OutManifest.Sort(
        [](const FCompileManifestEntry& Left, const FCompileManifestEntry& Right)
        {
            return Left.Key < Right.Key;
        });
    return true;
}

void AddNativeCallbackManifestEffects(
    FPatchContext& Context,
    const FString& EffectPrefix,
    const TArray<FCompileManifestEntry>& Before,
    const TArray<FCompileManifestEntry>& After,
    const TSet<FString>& DetailedContentKeys,
    const TSet<FString>& DetailedRemovalKeys,
    bool& bOutChanged)
{
    bOutChanged = false;
    int32 BeforeIndex = 0;
    int32 AfterIndex = 0;
    while (BeforeIndex < Before.Num() || AfterIndex < After.Num())
    {
        if (BeforeIndex >= Before.Num())
        {
            const FCompileManifestEntry& Entry = After[AfterIndex++];
            bOutChanged = true;
            Context.PlannedEffects.Add(MakeShared<FJsonValueString>(
                EffectPrefix
                + TEXT(" created ")
                + Entry.Key
                + TEXT(" at ")
                + ManifestLocation(Entry)));
            continue;
        }
        if (AfterIndex >= After.Num())
        {
            const FCompileManifestEntry& Entry = Before[BeforeIndex++];
            bOutChanged = true;
            if (!DetailedRemovalKeys.Contains(Entry.Key))
            {
                Context.PlannedEffects.Add(MakeShared<FJsonValueString>(
                    EffectPrefix
                    + TEXT(" removed ")
                    + Entry.Key
                    + TEXT(" from ")
                    + ManifestLocation(Entry)));
            }
            continue;
        }
        const FCompileManifestEntry& Previous = Before[BeforeIndex];
        const FCompileManifestEntry& Current = After[AfterIndex];
        if (Previous.Key < Current.Key)
        {
            bOutChanged = true;
            if (!DetailedRemovalKeys.Contains(Previous.Key))
            {
                Context.PlannedEffects.Add(MakeShared<FJsonValueString>(
                    EffectPrefix
                    + TEXT(" removed ")
                    + Previous.Key
                    + TEXT(" from ")
                    + ManifestLocation(Previous)));
            }
            ++BeforeIndex;
            continue;
        }
        if (Current.Key < Previous.Key)
        {
            bOutChanged = true;
            Context.PlannedEffects.Add(MakeShared<FJsonValueString>(
                EffectPrefix
                + TEXT(" created ")
                + Current.Key
                + TEXT(" at ")
                + ManifestLocation(Current)));
            ++AfterIndex;
            continue;
        }
        if (Previous.Owner != Current.Owner
            || Previous.Role != Current.Role
            || Previous.Order != Current.Order)
        {
            bOutChanged = true;
            Context.PlannedEffects.Add(MakeShared<FJsonValueString>(
                EffectPrefix
                + TEXT(" moved ")
                + Current.Key
                + TEXT(" from ")
                + ManifestLocation(Previous)
                + TEXT(" to ")
                + ManifestLocation(Current)));
        }
        if (Previous.Content != Current.Content)
        {
            bOutChanged = true;
            if (!DetailedContentKeys.Contains(Current.Key))
            {
                Context.PlannedEffects.Add(MakeShared<FJsonValueString>(
                    EffectPrefix + TEXT(" updated ") + Current.Key));
            }
        }
        ++BeforeIndex;
        ++AfterIndex;
    }
}

bool AddCompileManifestEffects(
    FPatchContext& Context,
    const TArray<FCompileManifestEntry>& Before,
    const TArray<FCompileManifestEntry>& After,
    const bool bIgnoreNativeRootCreation,
    bool& bOutChanged,
    FString& OutError)
{
    bOutChanged = false;
    int32 BeforeIndex = 0;
    int32 AfterIndex = 0;
    while (BeforeIndex < Before.Num() || AfterIndex < After.Num())
    {
        if (BeforeIndex >= Before.Num())
        {
            const FCompileManifestEntry& Entry = After[AfterIndex++];
            bOutChanged = true;
            if (bIgnoreNativeRootCreation
                && Entry.Key.StartsWith(TEXT("state@"))
                && Entry.Owner == TEXT("target")
                && Entry.Role == TEXT("SubTrees"))
            {
                continue;
            }
            Context.PlannedEffects.Add(MakeShared<FJsonValueString>(
                TEXT("compile validation created ")
                + Entry.Key
                + TEXT(" at ")
                + ManifestLocation(Entry)));
            continue;
        }
        if (AfterIndex >= After.Num())
        {
            const FCompileManifestEntry& Entry = Before[BeforeIndex++];
            bOutChanged = true;
            Context.PlannedEffects.Add(MakeShared<FJsonValueString>(
                TEXT("compile validation removed ")
                + Entry.Key
                + TEXT(" from ")
                + ManifestLocation(Entry)));
            continue;
        }
        const FCompileManifestEntry& Previous = Before[BeforeIndex];
        const FCompileManifestEntry& Current = After[AfterIndex];
        if (Previous.Key < Current.Key)
        {
            bOutChanged = true;
            Context.PlannedEffects.Add(MakeShared<FJsonValueString>(
                TEXT("compile validation removed ")
                + Previous.Key
                + TEXT(" from ")
                + ManifestLocation(Previous)));
            ++BeforeIndex;
            continue;
        }
        if (Current.Key < Previous.Key)
        {
            bOutChanged = true;
            if (bIgnoreNativeRootCreation
                && Current.Key.StartsWith(TEXT("state@"))
                && Current.Owner == TEXT("target")
                && Current.Role == TEXT("SubTrees"))
            {
                ++AfterIndex;
                continue;
            }
            Context.PlannedEffects.Add(MakeShared<FJsonValueString>(
                TEXT("compile validation created ")
                + Current.Key
                + TEXT(" at ")
                + ManifestLocation(Current)));
            ++AfterIndex;
            continue;
        }
        if (Previous.Owner != Current.Owner
            || Previous.Role != Current.Role
            || Previous.Order != Current.Order)
        {
            bOutChanged = true;
            Context.PlannedEffects.Add(MakeShared<FJsonValueString>(
                TEXT("compile validation moved ")
                + Current.Key
                + TEXT(" from ")
                + ManifestLocation(Previous)
                + TEXT(" to ")
                + ManifestLocation(Current)));
        }
        if (Previous.Content != Current.Content)
        {
            bOutChanged = true;
            Context.PlannedEffects.Add(MakeShared<FJsonValueString>(
                TEXT("compile validation updated ") + Current.Key));
        }
        ++BeforeIndex;
        ++AfterIndex;
    }
    return true;
}

void CaptureCompileState(
    FPatchContext& Context,
    TArray<FCompileStateSnapshot>& OutStates,
    TArray<FCompileTransitionSnapshot>& OutTransitions)
{
    OutStates.Reset();
    OutTransitions.Reset();
    Context.Data.VisitHierarchy(
        [&](const UStateTreeState& State, UStateTreeState*)
        {
            FCompileStateSnapshot& StateSnapshot = OutStates.AddDefaulted_GetRef();
            StateSnapshot.StableId = Context.Identities.RestoreState(State.ID);
            StateSnapshot.LinkedSubtreeName = State.LinkedSubtree.Name;
            StateSnapshot.bLinkedParameters = State.Type == EStateTreeStateType::Linked
                || State.Type == EStateTreeStateType::LinkedAsset;
            StateSnapshot.Parameters = State.Parameters;
            for (const FStateTreeTransition& Transition : State.Transitions)
            {
                FCompileTransitionSnapshot& TransitionSnapshot =
                    OutTransitions.AddDefaulted_GetRef();
                TransitionSnapshot.StableId =
                    Context.Identities.RestoreTransition(Transition.ID);
                TransitionSnapshot.StateName = Transition.State.Name;
            }
            return EStateTreeVisitor::Continue;
        });
}

bool AddCompileRepairEffects(
    FPatchContext& Context,
    UStateTreeEditorData& CurrentData,
    const FIdentityMap& CurrentIdentities,
    const TArray<FCompileStateSnapshot>& BeforeStates,
    const TArray<FCompileTransitionSnapshot>& BeforeTransitions,
    FString& OutError)
{
    FAuthoredIndex Index(Context.Tree, CurrentData, CurrentIdentities);
    if (!Index.IsValid())
    {
        OutError = Index.GetError();
        return false;
    }
    for (const FCompileStateSnapshot& Before : BeforeStates)
    {
        FStateRef* Current = Index.FindState(Before.StableId);
        if (Current == nullptr)
        {
            OutError = TEXT("Native compile could not map a pre-existing State after EditorData validation.");
            return false;
        }
        if (Before.LinkedSubtreeName != Current->State->LinkedSubtree.Name)
        {
            Context.PlannedEffects.Add(MakeShared<FJsonValueString>(
                TEXT("compile validation updated state@")
                + GuidText(Before.StableId)
                + TEXT(".LinkedSubtree.Name")));
        }
        if (Before.bLinkedParameters
            && !FStateTreeStateParameters::StaticStruct()->CompareScriptStruct(
                &Before.Parameters,
                &Current->State->Parameters,
                PPF_None))
        {
            Context.PlannedEffects.Add(MakeShared<FJsonValueString>(
                TEXT("compile validation synchronized state@")
                + GuidText(Before.StableId)
                + TEXT(".Parameters")));
        }
    }
    for (const FCompileTransitionSnapshot& Before : BeforeTransitions)
    {
        FTransitionRef* Current = Index.FindTransition(Before.StableId);
        if (Current == nullptr)
        {
            OutError = TEXT("Native compile could not map a pre-existing Transition after EditorData validation.");
            return false;
        }
        if (Before.StateName != Current->Transition->State.Name)
        {
            Context.PlannedEffects.Add(MakeShared<FJsonValueString>(
                TEXT("compile validation updated transition@")
                + GuidText(Before.StableId)
                + TEXT(".State.Name")));
        }
    }
    return true;
}

FString CompilerSeverity(const int32 Severity)
{
    switch (static_cast<EMessageSeverity::Type>(Severity))
    {
    case EMessageSeverity::Error: return TEXT("error");
    case EMessageSeverity::PerformanceWarning:
    case EMessageSeverity::Warning: return TEXT("warning");
    default: return TEXT("info");
    }
}

void AddPlanOperation(FPatchContext& Context, const FString& Kind, const FString& Effect)
{
    TSharedPtr<FJsonObject> Planned = MakeShared<FJsonObject>();
    Planned->SetStringField(TEXT("kind"), Kind);
    Context.PlannedOperations.Add(MakeShared<FJsonValueObject>(Planned));
    if (!Effect.IsEmpty())
    {
        Context.PlannedEffects.Add(MakeShared<FJsonValueString>(Effect));
    }
}

void AddCompilerMessages(
    FPatchContext& Context,
    UStateTreeEditorData& CurrentData,
    const FIdentityMap& CurrentIdentities,
    const FInspectableStateTreeCompilerLog& Log,
    int32& OutErrors,
    int32& OutWarnings)
{
    OutErrors = 0;
    OutWarnings = 0;
    for (const FStateTreeCompilerLogMessage& Message : Log.GetMessages())
    {
        const FString Severity = CompilerSeverity(Message.Severity);
        OutErrors += Severity == TEXT("error") ? 1 : 0;
        OutWarnings += Severity == TEXT("warning") ? 1 : 0;
        TArray<FString> Refs;
        if (Message.State != nullptr && Message.State->ID.IsValid())
        {
            Refs.Add(TEXT("state@") + GuidText(CurrentIdentities.RestoreState(Message.State->ID)));
        }
        if (Message.Item.ID.IsValid())
        {
            FAuthoredIndex Index(Context.Tree, CurrentData, CurrentIdentities);
            if (FNodeRef* Node = Index.IsValid() ? Index.FindNodeByNativeStructId(Message.Item.ID) : nullptr)
            {
                Refs.AddUnique(TEXT("node@") + GuidText(CurrentIdentities.RestoreNode(Node->Node->ID)));
            }
            else
            {
                const FGuid StateId = CurrentIdentities.RestoreState(Message.Item.ID);
                const FGuid TransitionId = CurrentIdentities.RestoreTransition(Message.Item.ID);
                if (StateId != Message.Item.ID || Index.FindState(Message.Item.ID) != nullptr)
                {
                    Refs.AddUnique(TEXT("state@") + GuidText(StateId));
                }
                else if (TransitionId != Message.Item.ID || Index.FindTransition(Message.Item.ID) != nullptr)
                {
                    Refs.AddUnique(TEXT("transition@") + GuidText(TransitionId));
                }
            }
        }
        FString ContextText;
        if (!Message.Item.Name.IsNone())
        {
            ContextText = TEXT(" ") + Message.Item.Name.ToString();
        }
        Context.ResultComments.Add(
            Severity
            + (Refs.IsEmpty() ? FString() : TEXT(" ") + FString::Join(Refs, TEXT(" ")))
            + ContextText
            + TEXT(": ")
            + Message.Message);
    }
}

bool CompileStateTree(FPatchContext& Context)
{
    if (GEditor == nullptr || GEditor->IsPlaySessionInProgress())
    {
        return Context.Fail(
            TEXT("capability.compile_unavailable"),
            TEXT("StateTree compile is unavailable while PIE is active or the Editor is unavailable."),
            TEXT("compile"));
    }
    const uint32 BeforeHash = UStateTreeEditingSubsystem::CalculateStateTreeHash(&Context.Tree);
    const TArray<FBindingSnapshot> BeforeBindings = CaptureBindingSnapshot(Context);
    TArray<FCompileManifestEntry> BeforeManifest;
    FString ManifestError;
    if (!CaptureCompileManifest(
            Context.Tree,
            Context.Data,
            Context.Identities,
            BeforeManifest,
            ManifestError))
    {
        return Context.Fail(
            TEXT("validation.patch_state_invalid"),
            ManifestError,
            TEXT("compile"));
    }
    TArray<FCompileStateSnapshot> BeforeStates;
    TArray<FCompileTransitionSnapshot> BeforeTransitions;
    CaptureCompileState(Context, BeforeStates, BeforeTransitions);
    const bool bHadEditorSchema = Context.Data.EditorSchema != nullptr;
    const FString BeforeEditorDataClass = Context.Data.GetClass()->GetPathName();
    Context.Tree.Modify();
    Context.Data.Modify();
    FInspectableStateTreeCompilerLog Log;
    Context.bCompileSucceeded = UStateTreeEditingSubsystem::CompileStateTree(&Context.Tree, Log);
    Context.bCompiled = true;
    ++Context.ChangedOperations;

    UStateTreeEditorData* CurrentData = Cast<UStateTreeEditorData>(Context.Tree.EditorData);
    if (CurrentData == nullptr)
    {
        return Context.Fail(
            TEXT("validation.patch_state_invalid"),
            TEXT("Native StateTree validation left no usable EditorData object."),
            TEXT("compile"));
    }
    FIdentityMap ReplacementIdentities;
    FIdentityMap CurrentIdentities = Context.Identities;
    bool bRootReset = false;
    FString ReplacementError;
    if (CurrentData != &Context.Data)
    {
        if (!BuildCompileReplacementIdentityMap(
                Context.Data,
                *CurrentData,
                ReplacementIdentities,
                bRootReset,
                ReplacementError))
        {
            return Context.Fail(
                TEXT("validation.patch_state_invalid"),
                ReplacementError.IsEmpty()
                    ? TEXT("Native StateTree validation replaced EditorData, but its authored structure could not be mapped exactly.")
                    : ReplacementError,
                TEXT("compile"));
        }
        ComposeIdentityMap(CurrentIdentities, ReplacementIdentities);
        Context.PlannedEffects.Add(MakeShared<FJsonValueString>(
            TEXT("compile validation replaced EditorData ")
            + BeforeEditorDataClass
            + TEXT(" -> ")
            + CurrentData->GetClass()->GetPathName()));
        if (bRootReset)
        {
            Context.PlannedEffects.Add(MakeShared<FJsonValueString>(
                TEXT("compile validation created the required native root State")));
        }
    }
    const uint32 AfterHash = UStateTreeEditingSubsystem::CalculateStateTreeHash(&Context.Tree);
    const TArray<FString> RemovedBindings = FindRemovedBindingEvidence(
        *CurrentData,
        BeforeBindings,
        ReplacementIdentities);
    AddRemovedBindingEffects(Context, RemovedBindings);
    TArray<FCompileManifestEntry> AfterManifest;
    bool bManifestChanged = false;
    if (!CaptureCompileManifest(
            Context.Tree,
            *CurrentData,
            CurrentIdentities,
            AfterManifest,
            ManifestError)
        || !AddCompileManifestEffects(
            Context,
            BeforeManifest,
            AfterManifest,
            bRootReset,
            bManifestChanged,
            ManifestError))
    {
        return Context.Fail(
            TEXT("validation.patch_state_invalid"),
            ManifestError.IsEmpty()
                ? TEXT("Native compile authored manifest could not be compared safely.")
                : ManifestError,
            TEXT("compile"));
    }
    if (!AddCompileRepairEffects(
            Context,
            *CurrentData,
            CurrentIdentities,
            BeforeStates,
            BeforeTransitions,
            ReplacementError))
    {
        return Context.Fail(
            TEXT("validation.patch_state_invalid"),
            ReplacementError,
            TEXT("compile"));
    }
    int32 Errors = 0;
    int32 Warnings = 0;
    AddCompilerMessages(Context, *CurrentData, CurrentIdentities, Log, Errors, Warnings);
    if (!bHadEditorSchema && CurrentData->EditorSchema != nullptr)
    {
        Context.PlannedEffects.Add(MakeShared<FJsonValueString>(
            TEXT("compile validation created the native EditorSchema")));
    }
    if (BeforeHash != AfterHash && !bManifestChanged)
    {
        return Context.Fail(
            TEXT("validation.patch_state_invalid"),
            TEXT("Native compile changed authored EditorData outside the complete authored manifest."),
            TEXT("compile"));
    }
    const FString Summary = FString::Printf(
        TEXT("compile: %s; %d errors; %d warnings"),
        Context.bCompileSucceeded ? TEXT("succeeded") : TEXT("failed"),
        Errors,
        Warnings);
    Context.ResultComments.Insert(Summary, 0);
    AddPlanOperation(Context, TEXT("compile"), Summary);
    return true;
}

bool RunTerminalPatch(
    FPatchContext& Context,
    const bool bCompile,
    const bool bSave)
{
    if (bCompile && !CompileStateTree(Context))
    {
        return false;
    }
    if (bSave)
    {
        Context.bSaveRequested = true;
        AddPlanOperation(Context, TEXT("save"), TEXT("save owning StateTree Package"));
        if (!bCompile && Context.bInitiallyStale)
        {
            Context.ResultComments.Add(TEXT("warning: save persists stale compiled StateTree data; save does not compile"));
            Context.PlannedEffects.Add(MakeShared<FJsonValueString>(
                TEXT("save preserves stale compiled StateTree data")));
        }
    }
    return true;
}

bool ValidateNoUnplannedRepairs(FPatchContext& Context)
{
    FIdentityMap ValidationIdentities;
    FString Error;
    TStrongObjectPtr<UStateTree> ValidationTreeOwner = DuplicateForPreflight(
        Context.Tree,
        ValidationIdentities,
        Error);
    UStateTree* ValidationTree = ValidationTreeOwner.Get();
    UStateTreeEditorData* ValidationData = ValidationTree != nullptr
        ? Cast<UStateTreeEditorData>(ValidationTree->EditorData)
        : nullptr;
    if (ValidationTree == nullptr || ValidationData == nullptr)
    {
        return Context.Fail(
            TEXT("capability.preflight_unavailable"),
            Error.IsEmpty()
                ? TEXT("UE could not isolate native StateTree validation from the authored preflight plan.")
                : Error,
            TEXT("patch"));
    }

    const uint32 BeforeHash = UStateTreeEditingSubsystem::CalculateStateTreeHash(ValidationTree);
    UStateTreeEditorData* const BeforeData = ValidationData;
    UStateTreeEditorSchema* const BeforeEditorSchema = ValidationData->EditorSchema;
    const FStateTreeEditorPropertyBindings BeforeBindings = ValidationData->EditorBindings;
    const TArray<FBindingSnapshot> PublicBefore = CaptureBindingSnapshot(Context);

    UStateTreeEditingSubsystem::ValidateStateTree(ValidationTree);
    UStateTreeEditorData* const AfterData = Cast<UStateTreeEditorData>(ValidationTree->EditorData);
    if (AfterData != BeforeData)
    {
        return Context.Fail(
            TEXT("validation.preflight_failed"),
            TEXT("Native StateTree validation would replace EditorData during an ordinary authored Patch; use explicit compile after repairing the schema/editor-data mismatch."),
            TEXT("patch"));
    }

    TArray<FString> RemovedEvidence;
    const TConstArrayView<FStateTreePropertyPathBinding> ValidatedBindings = AfterData->EditorBindings.GetBindings();
    for (const FBindingSnapshot& Binding : PublicBefore)
    {
        FPropertyBindingPath ValidationSource = Binding.Source;
        FPropertyBindingPath ValidationTarget = Binding.Target;
        ValidationSource.SetStructID(
            ValidationIdentities.ResolveNativeStruct(Binding.Source.GetStructID()));
        ValidationTarget.SetStructID(
            ValidationIdentities.ResolveNativeStruct(Binding.Target.GetStructID()));
        const bool bStillPresent = ValidatedBindings.ContainsByPredicate(
            [&](const FStateTreePropertyPathBinding& Candidate)
            {
                return Candidate.GetSourcePath() == ValidationSource
                    && Candidate.GetTargetPath() == ValidationTarget
                    && Candidate.IsOutputBinding() == Binding.bOutput;
            });
        if (!bStillPresent)
        {
            RemovedEvidence.Add(Binding.Evidence);
        }
    }
    for (const FString& Evidence : RemovedEvidence)
    {
        if (!Context.ExpectedValidationBindingRepairs.Contains(Evidence))
        {
            return Context.Fail(
                TEXT("validation.preflight_failed"),
                TEXT("Native StateTree validation would remove an unrelated authored Binding: ")
                    + Evidence,
            TEXT("patch"));
        }
    }
    if (ValidatedBindings.Num() + RemovedEvidence.Num()
        != BeforeBindings.GetBindings().Num())
    {
        return Context.Fail(
            TEXT("validation.preflight_failed"),
            TEXT("Native StateTree validation changed the authored Binding set beyond the classified dormant Event consequences."),
            TEXT("patch"));
    }

    // Restore only the classified binding removals on the isolated validation
    // copy, then hash it. Equality proves validation made no other authored
    // repair; the actual preflight document was never mutated.
    AfterData->EditorBindings = BeforeBindings;
    AfterData->EditorBindings.SetBindingsOwner(AfterData);
    const uint32 AfterRestoredHash = UStateTreeEditingSubsystem::CalculateStateTreeHash(ValidationTree);
    if (AfterRestoredHash != BeforeHash || AfterData->EditorSchema != BeforeEditorSchema)
    {
        return Context.Fail(
            TEXT("validation.preflight_failed"),
            TEXT("Native StateTree validation discovered an unplanned authored repair after this Patch. Refresh the exact object/schema and repair that source state explicitly before retrying."),
            TEXT("patch"));
    }
    return true;
}

bool RunAuthoredPatch(FPatchContext& Context, const FSalPatch& Patch)
{
    for (const TSharedPtr<FJsonValue>& Value : Patch.Statements)
    {
        TSharedPtr<FJsonObject> Statement;
        FString Kind;
        if (!ReadObject(Value, Statement) || !ReadStatementKind(Statement, Kind))
        {
            return Context.Fail(
                TEXT("validation.patch_state_invalid"),
                TEXT("Decoded StateTree Patch contains an unrepresentable statement."),
                TEXT("patch"));
        }
        if (Kind.IsEmpty())
        {
            if (!Context.AddDefinition(Statement))
            {
                return false;
            }
        }
        else if (Kind == TEXT("add"))
        {
            if (!HandleAdd(Context, Statement)) return false;
        }
        else if (Kind == TEXT("set"))
        {
            if (!HandleSetReset(Context, Statement, false)) return false;
        }
        else if (Kind == TEXT("reset"))
        {
            if (!HandleSetReset(Context, Statement, true)) return false;
        }
        else if (Kind == TEXT("move"))
        {
            if (!HandleMove(Context, Statement)) return false;
        }
        else if (Kind == TEXT("remove"))
        {
            if (!HandleRemove(Context, Statement)) return false;
        }
        else if (Kind == TEXT("bind"))
        {
            if (!HandleBindUnbind(Context, Statement, true)) return false;
        }
        else if (Kind == TEXT("unbind"))
        {
            if (!HandleBindUnbind(Context, Statement, false)) return false;
        }
        else if (Kind == TEXT("invoke"))
        {
            return Context.Fail(
                TEXT("capability.operation_unavailable"),
                TEXT("This StateTree schema advertises no designed invoke Operation for the exact subject."),
                TEXT("invoke"));
        }
        else
        {
            return Context.Fail(
                TEXT("capability.operation_unavailable"),
                FString::Printf(TEXT("StateTree Patch does not own operation %s."), *Kind),
                Kind);
        }
    }
    for (const TPair<FString, FConstructorDefinition>& Pair : Context.Definitions)
    {
        if (!Pair.Value.bConsumed)
        {
            return Context.Fail(
                TEXT("validation.unused_binding"),
                TEXT("Every StateTree constructor binding must be consumed exactly once by add or its owning bind."),
                TEXT("patch"),
                Pair.Key);
        }
    }
    if (Context.bPreflight && !ValidateNoUnplannedRepairs(Context))
    {
        return false;
    }
    FAuthoredIndex FinalIndex = Context.MakeIndex();
    if (!FinalIndex.IsValid())
    {
        return Context.Fail(
            TEXT("validation.patch_state_invalid"),
            FinalIndex.GetError(),
            TEXT("patch"));
    }
    return true;
}

void AppendComments(TSharedPtr<FJsonObject>& Object, const TArray<FString>& Comments)
{
    if (!Object.IsValid() || Comments.IsEmpty())
    {
        return;
    }
    TArray<TSharedPtr<FJsonValue>> Statements;
    const TArray<TSharedPtr<FJsonValue>>* Existing = nullptr;
    if (Object->TryGetArrayField(TEXT("statements"), Existing) && Existing != nullptr)
    {
        Statements = *Existing;
    }
    for (const FString& Text : Comments)
    {
        if (Text.IsEmpty())
        {
            continue;
        }
        TSharedPtr<FJsonObject> Comment = MakeShared<FJsonObject>();
        Comment->SetStringField(TEXT("kind"), TEXT("comment"));
        Comment->SetStringField(TEXT("text"), Text);
        Statements.Add(MakeShared<FJsonValueObject>(Comment));
    }
    Object->SetArrayField(TEXT("statements"), MoveTemp(Statements));
}

FString PlanSignature(const FPatchContext& Context)
{
    return ExprString(MakeShared<FJsonValueObject>(BuildPlan(Context)));
}

} // namespace StateTreePatch

TSharedPtr<FJsonObject> FSalStateTreeInterface::Patch(
    const FSalPatch& Patch,
    const FSalResolvedTarget& Target)
{
    using namespace StateTreePatch;

    UStateTree* StateTree = Cast<UStateTree>(Target.Object);
    UStateTreeEditorData* EditorData = StateTree != nullptr
        ? Cast<UStateTreeEditorData>(StateTree->EditorData)
        : nullptr;
    if (StateTree == nullptr || EditorData == nullptr)
    {
        return ErrorResult(
            Patch,
            Target,
            TEXT("resolution.target_not_found"),
            TEXT("StateTree Patch requires an exact existing UStateTree Asset with authored EditorData."),
            TEXT("patch"));
    }

    FIdentityMap LiveIdentities;
    FAuthoredIndex InitialIndex(*StateTree, *EditorData, LiveIdentities);
    if (!InitialIndex.IsValid())
    {
        return ErrorResult(
            Patch,
            Target,
            TEXT("validation.patch_state_invalid"),
            InitialIndex.GetError(),
            TEXT("patch"));
    }
    bool bCompile = false;
    bool bSave = false;
    bool bHasTerminal = false;
    for (const TSharedPtr<FJsonValue>& Value : Patch.Statements)
    {
        TSharedPtr<FJsonObject> Statement;
        FString Kind;
        if (ReadObject(Value, Statement) && ReadStatementKind(Statement, Kind))
        {
            bHasTerminal |= Kind == TEXT("compile") || Kind == TEXT("save");
        }
    }
    if (bHasTerminal && !ValidateTerminalSequence(Patch.Statements, bCompile, bSave))
    {
        return ErrorResult(
            Patch,
            Target,
            TEXT("validation.finalization_must_be_independent"),
            TEXT("StateTree terminal Patch must be compile, save, or compile followed by save, with no authored edits."),
            TEXT("patch"));
    }
    if (GEditor == nullptr || !GEditor->CanTransact() || GEditor->IsTransactionActive())
    {
        return ErrorResult(
            Patch,
            Target,
            TEXT("capability.transaction_unavailable"),
            TEXT("StateTree Patch requires one available top-level Editor transaction for native preflight and atomic apply."),
            TEXT("patch"));
    }
    if (bCompile && GEditor->IsPlaySessionInProgress())
    {
        return ErrorResult(
            Patch,
            Target,
            TEXT("capability.compile_unavailable"),
            TEXT("UE does not allow StateTree compilation while a Play session is in progress."),
            TEXT("compile"));
    }
    if (bSave && GEditor->GetEditorSubsystem<UEditorAssetSubsystem>() == nullptr)
    {
        return ErrorResult(
            Patch,
            Target,
            TEXT("capability.save_unavailable"),
            TEXT("UEditorAssetSubsystem is unavailable for StateTree save."),
            TEXT("save"));
    }

    FPlanIdentities PlanIdentities;
    FString Error;
    if (!SeedPlanIdentities(Patch, PlanIdentities, Error))
    {
        return ErrorResult(
            Patch,
            Target,
            TEXT("validation.patch_state_invalid"),
            Error,
            TEXT("patch"));
    }
    FIdentityMap PreflightIdentities;
    TStrongObjectPtr<UStateTree> PlanningTreeOwner = DuplicateForPreflight(
        *StateTree,
        PreflightIdentities,
        Error);
    UStateTree* PlanningTree = PlanningTreeOwner.Get();
    UStateTreeEditorData* PlanningData = PlanningTree != nullptr
        ? Cast<UStateTreeEditorData>(PlanningTree->EditorData)
        : nullptr;
    if (PlanningTree == nullptr || PlanningData == nullptr)
    {
        return ErrorResult(
            Patch,
            Target,
            TEXT("capability.preflight_unavailable"),
            Error.IsEmpty() ? TEXT("UE could not create an isolated StateTree preflight copy.") : Error,
            TEXT("patch"));
    }

    const FString TargetAlias = !Patch.Alias.IsEmpty() ? Patch.Alias : Target.Alias;
    const bool bInitiallyStale = UStateTreeEditingSubsystem::CalculateStateTreeHash(StateTree)
        != StateTree->LastCompiledEditorDataHash;
    FPatchContext Preflight(
        *PlanningTree,
        *PlanningData,
        PreflightIdentities,
        PlanIdentities,
        TargetAlias,
        bInitiallyStale,
        true);
    bool bPreflightSucceeded = false;
    {
        FScopedTransaction Transaction(NSLOCTEXT(
            "Loomle",
            "SalStateTreePatchPreflight",
            "SAL StateTree Patch Preflight"));
        PlanningTree->Modify(false);
        PlanningData->Modify(false);
        bPreflightSucceeded = bHasTerminal
            ? RunTerminalPatch(Preflight, bCompile, bSave)
            : RunAuthoredPatch(Preflight, Patch);
        Transaction.Cancel();
    }
    const TSharedPtr<FJsonObject> Planned = BuildPlan(Preflight);
    if (!bPreflightSucceeded)
    {
        return MakeMutationResult(
            nullptr,
            Preflight.Diagnostics,
            Patch.bDryRun,
            false,
            false,
            Target.AssetPath,
            TEXT("patch"),
            Planned,
            Preflight.ResolvedRefs);
    }
    if (Patch.bDryRun)
    {
        if (bSave)
        {
            Preflight.ResultComments.Add(TEXT("would save owning StateTree Package"));
        }
        TSharedPtr<FJsonObject> Object = CurrentObject(Target);
        AppendComments(Object, Preflight.ResultComments);
        TSharedPtr<FJsonObject> Diff = MakeShared<FJsonObject>();
        Diff->SetNumberField(TEXT("changedOperations"), Preflight.ChangedOperations);
        return MakeMutationResult(
            Object,
            Preflight.Diagnostics,
            true,
            true,
            false,
            Target.AssetPath,
            TEXT("patch"),
            Planned,
            Preflight.ResolvedRefs,
            Diff);
    }

    UPackage* Package = StateTree->GetOutermost();
    const bool bWasDirty = Package != nullptr && Package->IsDirty();
    StateTree->SetFlags(RF_Transactional);
    EditorData->SetFlags(RF_Transactional);
    FPatchContext Applied(
        *StateTree,
        *EditorData,
        LiveIdentities,
        PlanIdentities,
        TargetAlias,
        bInitiallyStale,
        false);
    bool bCaptured = false;
    bool bApplySucceeded = false;
    {
        FScopedTransaction Transaction(NSLOCTEXT(
            "Loomle",
            "SalStateTreePatch",
            "SAL StateTree Patch"));
        const bool bTreeCaptured = StateTree->Modify(false);
        const bool bDataCaptured = EditorData->Modify(false);
        bCaptured = bTreeCaptured && bDataCaptured;
        if (!bCaptured)
        {
            Applied.Fail(
                TEXT("capability.transaction_unavailable"),
                TEXT("UE could not capture the StateTree and its EditorData in the private SAL transaction."),
                TEXT("patch"));
            Transaction.Cancel();
        }
        else
        {
            bApplySucceeded = bHasTerminal
                ? RunTerminalPatch(Applied, bCompile, bSave)
                : RunAuthoredPatch(Applied, Patch);
            if (bApplySucceeded
                && (Applied.ChangedOperations != Preflight.ChangedOperations
                    || Applied.bCompileSucceeded != Preflight.bCompileSucceeded
                    || PlanSignature(Applied) != PlanSignature(Preflight)))
            {
                bApplySucceeded = Applied.Fail(
                    TEXT("validation.preflight_failed"),
                    TEXT("Live StateTree effects diverged from the isolated native preflight plan; the Patch was rolled back."),
                    TEXT("patch"));
            }
            if (bApplySucceeded && Applied.ChangedOperations == 0)
            {
                Transaction.Cancel();
            }
            else if (bApplySucceeded && Package != nullptr)
            {
                Package->SetDirtyFlag(true);
            }
        }
    }
    if (!bCaptured)
    {
        return MakeMutationResult(
            nullptr,
            Applied.Diagnostics,
            false,
            false,
            false,
            Target.AssetPath,
            TEXT("patch"),
            Planned,
            Applied.ResolvedRefs);
    }
    if (!bApplySucceeded)
    {
        const bool bRolledBack = GEditor->UndoTransaction(false);
        if (Package != nullptr)
        {
            Package->SetDirtyFlag(bRolledBack ? bWasDirty : true);
        }
        if (!bRolledBack)
        {
            Applied.Fail(
                TEXT("validation.rollback_failed"),
                TEXT("UE could not undo the failed StateTree Patch transaction."),
                TEXT("patch"));
        }
        return MakeMutationResult(
            nullptr,
            Applied.Diagnostics,
            false,
            false,
            !bRolledBack,
            Target.AssetPath,
            TEXT("patch"),
            Planned,
            Applied.ResolvedRefs);
    }

    bool bSavedChange = false;
    if (Applied.bSaveRequested)
    {
        if (Package == nullptr)
        {
            Applied.Fail(
                TEXT("resolution.package_not_found"),
                TEXT("StateTree owning Package is unavailable for save."),
                TEXT("save"));
        }
        else if (!Package->IsDirty())
        {
            Applied.ResultComments.Add(TEXT("save: already clean"));
        }
        else if (UEditorAssetSubsystem* AssetSubsystem = GEditor->GetEditorSubsystem<UEditorAssetSubsystem>();
            AssetSubsystem != nullptr && AssetSubsystem->SaveLoadedAsset(StateTree, true))
        {
            Applied.bSaved = true;
            bSavedChange = true;
            Applied.ResultComments.Add(TEXT("save: saved"));
        }
        else
        {
            Applied.Fail(
                TEXT("validation.save_failed"),
                TEXT("UE failed to save the StateTree Package after completing prior requested mutations."),
                TEXT("save"));
        }
    }

    if (Applied.ChangedOperations > 0)
    {
        if (UStateTreeEditingSubsystem* Subsystem = GEditor->GetEditorSubsystem<UStateTreeEditingSubsystem>())
        {
            if (TSharedPtr<FStateTreeViewModel> ViewModel = Subsystem->FindViewModel(StateTree))
            {
                ViewModel->NotifyAssetChangedExternally();
            }
        }
    }
    TSharedPtr<FJsonObject> Object = CurrentObject(Target);
    AppendComments(Object, Applied.ResultComments);
    TSharedPtr<FJsonObject> Diff = MakeShared<FJsonObject>();
    Diff->SetNumberField(TEXT("changedOperations"), Applied.ChangedOperations);
    Diff->SetBoolField(TEXT("saved"), Applied.bSaved);
    const bool bValid = Applied.Diagnostics.IsEmpty();
    return MakeMutationResult(
        Object,
        Applied.Diagnostics,
        false,
        bValid,
        Applied.ChangedOperations > 0 || bSavedChange,
        Target.AssetPath,
        TEXT("patch"),
        Planned,
        Applied.ResolvedRefs,
        Diff);
}
}
