// Copyright 2026 Loomle contributors.

#include "SalStateTreeInterface.h"
#include "SalStateTreePalette.h"
#include "SalStateTreeSchema.h"

#include "../SalDiagnostics.h"
#include "../SalObjectBuilder.h"
#include "../SalRuntime.h"
#include "Algo/Reverse.h"
#include "Blueprint/StateTreeConditionBlueprintBase.h"
#include "Blueprint/StateTreeConsiderationBlueprintBase.h"
#include "Blueprint/StateTreeEvaluatorBlueprintBase.h"
#include "Blueprint/StateTreeTaskBlueprintBase.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "StateTree.h"
#include "StateTreeConditionBase.h"
#include "StateTreeDelegate.h"
#include "StateTreeEditingSubsystem.h"
#include "StateTreeEditorData.h"
#include "StateTreeEditorNode.h"
#include "StateTreeEditorPropertyBindings.h"
#include "StateTreeEditorSchema.h"
#include "StateTreeExecutionTypes.h"
#include "StateTreePropertyBindings.h"
#include "StateTreePropertyRef.h"
#include "StateTreePropertyRefHelpers.h"
#include "StateTreeSchema.h"
#include "StateTreeState.h"
#include "StateTreeTaskBase.h"
#include "StateTreeTasksStatus.h"
#include "StateTreeTypes.h"
#include "StructUtils/PropertyBag.h"
#include "Misc/SecureHash.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "UObject/UnrealType.h"

namespace Loomle::Sal
{
namespace StateTreeRead
{
constexpr const TCHAR* InterfaceName = TEXT("state_tree");
constexpr int32 DefaultTreeDepth = 20;
constexpr int32 DefaultCollectionLimit = 50;
constexpr int32 MaxCollectionLimit = 200;
constexpr int32 MaxAuthoredStates = 10000;
constexpr int32 MaxAuthoredStateLinks = 50000;
constexpr int32 MaxAuthoredIndexIssues = 256;
constexpr int32 MaxIdentityObjects = 50000;
constexpr int32 MaxEmittedObjects = 4096;
constexpr int32 MaxEmittedRelationships = 4096;
constexpr int32 MaxAnalysisObjects = 50000;
constexpr int32 MaxRelationshipAnalysisObjects = 50000;
constexpr int32 MaxNodeEligibilityValues = 65536;
constexpr int32 MaxNodeEligibilityObjects = 1024;
constexpr int64 MaxMemberPathSegments = 65536;
constexpr int32 MaxExactParameterValueCharacters = 1024 * 1024;

enum class EStateEmission : uint8
{
    Compact,
    Tree,
    Exact
};

struct FStateTreeCounts
{
    int32 States = 0;
    int32 Evaluators = 0;
    int32 GlobalTasks = 0;
    int32 Tasks = 0;
    int32 Conditions = 0;
    int32 Considerations = 0;
    int32 Transitions = 0;
    int32 Parameters = 0;
    int32 PropertyFunctions = 0;
    int32 PropertyBindings = 0;
    int32 ContextData = 0;
    bool bComplete = true;
};

struct FAuthoredStateEntry
{
    const UStateTreeState* State = nullptr;
    int32 ParentIndex = INDEX_NONE;
    TArray<int32> Children;
    FString MemberName;
};

struct FAuthoredNodeEntry
{
    const FStateTreeEditorNode* Node = nullptr;
    int32 OwnerStateIndex = INDEX_NONE;
    int32 OwnerTransitionIndex = INDEX_NONE;
    int32 RoleIndex = INDEX_NONE;
    int32 BindingIndex = INDEX_NONE;
    FString Role;
    FString MemberName;
    FString BindingTargetPath;
    bool bPropertyFunction = false;
};

struct FAuthoredTransitionEntry
{
    const FStateTreeTransition* Transition = nullptr;
    int32 OwnerStateIndex = INDEX_NONE;
    int32 ArrayIndex = INDEX_NONE;
    FString MemberName;
};

struct FAuthoredParameterEntry
{
    const FInstancedPropertyBag* Bag = nullptr;
    const FPropertyBagPropertyDesc* Desc = nullptr;
    FGuid ContainerId;
    int32 OwnerStateIndex = INDEX_NONE;
    int32 DescriptorIndex = INDEX_NONE;
    FString MemberName;
    bool bRoot = false;
};

struct FAuthoredContextEntry
{
    const FStateTreeExternalDataDesc* Desc = nullptr;
    int32 DescriptorIndex = INDEX_NONE;
};

struct FReadIssue
{
    FString Message;
    FString Ref;
};

FString GuidText(const FGuid& Guid);
FString ParameterIdentityText(const FGuid& ContainerId, const FGuid& PropertyId);
const UClass* BlueprintNodeClass(const FStateTreeEditorNode& Node);
bool IsBlueprintNodeWrapper(const UScriptStruct* Struct);

class FReadContext
{
public:
    void AddIssue(const FString& Message, const FString& Ref = FString())
    {
        if (Issues.Num() < MaxAuthoredIndexIssues)
        {
            Issues.Add({Message, Ref});
        }
        else
        {
            ++OmittedIssues;
        }
    }

    bool ConsumeIdentityObject()
    {
        if (IdentityObjects >= MaxIdentityObjects)
        {
            AddLimitIssueOnce(
                TEXT("identity"),
                FString::Printf(
                    TEXT("StateTree identity scan stopped at the hard limit of %d authored objects; affected identities are not canonical."),
                    MaxIdentityObjects));
            return false;
        }
        ++IdentityObjects;
        return true;
    }

    bool ConsumeEmittedObject()
    {
        if (EmittedObjects >= MaxEmittedObjects)
        {
            AddLimitIssueOnce(
                TEXT("emission"),
                FString::Printf(
                    TEXT("StateTree Object Text was truncated at the hard limit of %d emitted authored objects."),
                    MaxEmittedObjects));
            return false;
        }
        ++EmittedObjects;
        return true;
    }

    bool ConsumeEmittedRelationship()
    {
        if (EmittedRelationships >= MaxEmittedRelationships)
        {
            AddLimitIssueOnce(
                TEXT("relationship_emission"),
                FString::Printf(
                    TEXT("StateTree Binding arrows were truncated at the hard limit of %d directly incident relationships."),
                    MaxEmittedRelationships));
            return false;
        }
        ++EmittedRelationships;
        return true;
    }

    bool ConsumeAnalysisObject()
    {
        if (AnalysisObjects >= MaxAnalysisObjects)
        {
            AddLimitIssueOnce(
                TEXT("analysis"),
                FString::Printf(
                    TEXT("StateTree authored analysis stopped at the hard limit of %d objects; reported counts are partial."),
                    MaxAnalysisObjects));
            return false;
        }
        ++AnalysisObjects;
        return true;
    }

    bool ConsumeMemberPath(const int32 SegmentCount)
    {
        if (SegmentCount < 0
            || MemberPathSegments > MaxMemberPathSegments - static_cast<int64>(SegmentCount))
        {
            AddLimitIssueOnce(
                TEXT("member_path"),
                FString::Printf(
                    TEXT("StateTree Object Text was truncated at the hard limit of %lld cumulative member-path segments."),
                    static_cast<long long>(MaxMemberPathSegments)));
            return false;
        }
        MemberPathSegments += SegmentCount;
        return true;
    }

    bool ConsumeNativeSurfaceField(const int32 ValueCharacters)
    {
        if (ValueCharacters < 0
            || NativeSurfaceFields >= StateTreeSchema::FExactSchemaTextBuilder::MaxFields
            || NativeSurfaceCharacters
                > StateTreeSchema::FExactSchemaTextBuilder::MaxCharacters
                    - static_cast<int64>(ValueCharacters))
        {
            bNativeSurfaceExceeded = true;
            return false;
        }
        ++NativeSurfaceFields;
        NativeSurfaceCharacters += ValueCharacters;
        return true;
    }

    bool HasNativeSurfaceOverflow() const
    {
        return bNativeSurfaceExceeded;
    }

    void AddComments(FSalObjectBuilder& Builder) const
    {
        for (const FReadIssue& Issue : Issues)
        {
            Builder.AddComment(Issue.Message);
        }
        if (OmittedIssues > 0)
        {
            Builder.AddComment(FString::Printf(
                TEXT("StateTree read diagnostics truncated: %d additional issue(s)"),
                OmittedIssues));
        }
    }

    void AddDiagnostics(TArray<TSharedPtr<FJsonObject>>& OutDiagnostics) const
    {
        for (const FReadIssue& Issue : Issues)
        {
            FSalDiagnosticBuilder Diagnostic = FSalDiagnostics::Warning(
                TEXT("validation.invalid_target"),
                Issue.Message)
                .Interface(InterfaceName);
            if (!Issue.Ref.IsEmpty())
            {
                Diagnostic.Ref(Issue.Ref);
            }
            Diagnostic.Suggestion(TEXT("Inspect or repair the authored StateTree in the StateTree Editor; Query did not modify it."));
            OutDiagnostics.Add(Diagnostic.Build());
        }
        if (OmittedIssues > 0)
        {
            OutDiagnostics.Add(
                FSalDiagnostics::Warning(
                    TEXT("validation.invalid_target"),
                    FString::Printf(
                        TEXT("StateTree read has %d additional issue(s) beyond the diagnostic limit."),
                        OmittedIssues))
                    .Interface(InterfaceName)
                    .Build());
        }
    }

private:
    void AddLimitIssueOnce(const TCHAR* Key, const FString& Message)
    {
        const FName KeyName(Key);
        if (!ReportedLimits.Contains(KeyName))
        {
            ReportedLimits.Add(KeyName);
            AddIssue(Message);
        }
    }

    TArray<FReadIssue> Issues;
    TSet<FName> ReportedLimits;
    int32 OmittedIssues = 0;
    int32 IdentityObjects = 0;
    int32 EmittedObjects = 0;
    int32 EmittedRelationships = 0;
    int32 AnalysisObjects = 0;
    int64 MemberPathSegments = 0;
    int32 NativeSurfaceFields = 0;
    int64 NativeSurfaceCharacters = 0;
    bool bNativeSurfaceExceeded = false;
};

class FUniqueNameAllocator
{
public:
    void Reserve(const FString& Name)
    {
        Used.Add(Name);
    }

    FString Allocate(const FString& Preferred, const FString& Fallback = TEXT("item"))
    {
        const FString Base = FSalObjectBuilder::SanitizeIdentifier(Preferred, Fallback);
        if (!Used.Contains(Base))
        {
            Used.Add(Base);
            NextSuffix.FindOrAdd(Base) = 2;
            return Base;
        }

        int32& Suffix = NextSuffix.FindOrAdd(Base);
        if (Suffix < 2)
        {
            Suffix = 2;
        }
        for (;;)
        {
            const FString Candidate = FString::Printf(TEXT("%s_%d"), *Base, Suffix++);
            if (!Used.Contains(Candidate))
            {
                Used.Add(Candidate);
                return Candidate;
            }
        }
    }

private:
    TSet<FString> Used;
    TMap<FString, int32> NextSuffix;
};

class FAuthoredStateIndex
{
public:
    FAuthoredStateIndex(const UStateTreeEditorData& InEditorData, FReadContext& InContext)
        : EditorData(InEditorData)
        , Context(InContext)
    {
        Build();
        FinalizeStateIdentities();
        ScanNodeAndTransitionIdentities();
        ScanParameterIdentities();
        ScanContextIdentities();
        ValidateStateLinks();
    }

    const TArray<FAuthoredStateEntry>& GetEntries() const
    {
        return Entries;
    }

    const TArray<int32>& GetRoots() const
    {
        return Roots;
    }

    const TArray<FAuthoredNodeEntry>& GetNodeEntries() const
    {
        return NodeEntries;
    }

    const TArray<FAuthoredTransitionEntry>& GetTransitionEntries() const
    {
        return TransitionEntries;
    }

    const TArray<FAuthoredParameterEntry>& GetParameterEntries() const
    {
        return ParameterEntries;
    }

    const TArray<FAuthoredContextEntry>& GetContextEntries() const
    {
        return ContextEntries;
    }

    const FAuthoredStateEntry* GetEntry(const int32 Index) const
    {
        return Entries.IsValidIndex(Index) ? &Entries[Index] : nullptr;
    }

    TArray<int32> FindById(const FGuid& Id) const
    {
        const TArray<int32>* Matches = ById.Find(Id);
        return Matches != nullptr ? *Matches : TArray<int32>();
    }

    const FAuthoredNodeEntry* GetNodeEntry(const int32 Index) const
    {
        return NodeEntries.IsValidIndex(Index) ? &NodeEntries[Index] : nullptr;
    }

    const FAuthoredTransitionEntry* GetTransitionEntry(const int32 Index) const
    {
        return TransitionEntries.IsValidIndex(Index) ? &TransitionEntries[Index] : nullptr;
    }

    const FAuthoredParameterEntry* GetParameterEntry(const int32 Index) const
    {
        return ParameterEntries.IsValidIndex(Index) ? &ParameterEntries[Index] : nullptr;
    }

    const FAuthoredContextEntry* GetContextEntry(const int32 Index) const
    {
        return ContextEntries.IsValidIndex(Index) ? &ContextEntries[Index] : nullptr;
    }

    TArray<int32> FindNodeById(const FGuid& Id) const
    {
        const TArray<int32>* Matches = NodeById.Find(Id);
        return Matches != nullptr ? *Matches : TArray<int32>();
    }

    TArray<int32> FindTransitionById(const FGuid& Id) const
    {
        const TArray<int32>* Matches = TransitionById.Find(Id);
        return Matches != nullptr ? *Matches : TArray<int32>();
    }

    TArray<int32> FindParameterById(const FGuid& ContainerId, const FGuid& PropertyId) const
    {
        const TArray<int32>* Matches = ParameterById.Find(ParameterIdentityText(ContainerId, PropertyId));
        return Matches != nullptr ? *Matches : TArray<int32>();
    }

    TArray<int32> FindContextById(const FGuid& Id) const
    {
        const TArray<int32>* Matches = ContextById.Find(Id);
        return Matches != nullptr ? *Matches : TArray<int32>();
    }

    bool IsUnsafeState(const UStateTreeState& State) const
    {
        return UnsafeStates.Contains(&State);
    }

    bool IsHierarchyComplete() const
    {
        return bHierarchyComplete;
    }

    bool IsCanonicalState(const UStateTreeState& State) const
    {
        if (!bHierarchyComplete
            || !bStateIdentityComplete
            || !State.ID.IsValid()
            || IsUnsafeState(State))
        {
            return false;
        }
        const TArray<int32>* Matches = ById.Find(State.ID);
        return Matches != nullptr
            && Matches->Num() == 1
            && Entries.IsValidIndex((*Matches)[0])
            && Entries[(*Matches)[0]].State == &State;
    }

    bool IsCanonicalStateId(const FGuid& Id) const
    {
        if (!bHierarchyComplete || !bStateIdentityComplete || !Id.IsValid())
        {
            return false;
        }
        const TArray<int32>* Matches = ById.Find(Id);
        return Matches != nullptr
            && Matches->Num() == 1
            && Entries.IsValidIndex((*Matches)[0])
            && Entries[(*Matches)[0]].State != nullptr
            && !IsUnsafeState(*Entries[(*Matches)[0]].State);
    }

    bool IsCanonicalNode(const FStateTreeEditorNode& Node) const
    {
        if (!bNodeIdentityComplete || !Node.ID.IsValid())
        {
            return false;
        }
        const TArray<int32>* Matches = NodeById.Find(Node.ID);
        if (Matches == nullptr || Matches->Num() != 1)
        {
            return false;
        }
        const FAuthoredNodeEntry* Entry = GetNodeEntry((*Matches)[0]);
        return Entry != nullptr && Entry->Node == &Node && IsSafeNodeEntry(*Entry);
    }

    bool IsCanonicalTransition(const FStateTreeTransition& Transition) const
    {
        if (!bTransitionIdentityComplete || !Transition.ID.IsValid())
        {
            return false;
        }
        const TArray<int32>* Matches = TransitionById.Find(Transition.ID);
        if (Matches == nullptr || Matches->Num() != 1)
        {
            return false;
        }
        const FAuthoredTransitionEntry* Entry = GetTransitionEntry((*Matches)[0]);
        return Entry != nullptr && Entry->Transition == &Transition && IsSafeTransitionEntry(*Entry);
    }

    bool IsNodeIdentityComplete() const
    {
        return bNodeIdentityComplete;
    }

    bool IsTransitionIdentityComplete() const
    {
        return bTransitionIdentityComplete;
    }

    bool IsParameterIdentityComplete() const
    {
        return bParameterIdentityComplete;
    }

    bool IsContextIdentityComplete() const
    {
        return bContextIdentityComplete;
    }

    bool IsSafeNodeEntry(const FAuthoredNodeEntry& Entry) const
    {
        if (Entry.Node == nullptr)
        {
            return false;
        }
        if (Entry.OwnerStateIndex != INDEX_NONE)
        {
            const FAuthoredStateEntry* Owner = GetEntry(Entry.OwnerStateIndex);
            return Owner != nullptr
                && Owner->State != nullptr
                && IsCanonicalState(*Owner->State);
        }
        return true;
    }

    bool IsSafeTransitionEntry(const FAuthoredTransitionEntry& Entry) const
    {
        if (Entry.Transition == nullptr)
        {
            return false;
        }
        const FAuthoredStateEntry* Owner = GetEntry(Entry.OwnerStateIndex);
        return Owner != nullptr
            && Owner->State != nullptr
            && IsCanonicalState(*Owner->State);
    }

    bool IsCanonicalParameter(const FAuthoredParameterEntry& Entry) const
    {
        if (!bParameterIdentityComplete
            || Entry.Bag == nullptr
            || Entry.Desc == nullptr
            || !Entry.ContainerId.IsValid()
            || !Entry.Desc->ID.IsValid()
            || ParameterContainerCounts.FindRef(Entry.ContainerId) != 1)
        {
            return false;
        }
        const TArray<int32>* Matches = ParameterById.Find(
            ParameterIdentityText(Entry.ContainerId, Entry.Desc->ID));
        return Matches != nullptr
            && Matches->Num() == 1
            && GetParameterEntry((*Matches)[0]) == &Entry;
    }

    bool IsCanonicalContext(const FAuthoredContextEntry& Entry) const
    {
#if WITH_EDITORONLY_DATA
        if (!bContextIdentityComplete
            || Entry.Desc == nullptr
            || !Entry.Desc->ID.IsValid())
        {
            return false;
        }
        const TArray<int32>* Matches = ContextById.Find(Entry.Desc->ID);
        const FStateTreeExternalDataDesc* CanonicalDescriptor = nullptr;
        FString CanonicalError;
        return Matches != nullptr
            && Matches->Num() == 1
            && GetContextEntry((*Matches)[0]) == &Entry
            && StateTreeSchema::ResolveCanonicalContext(
                EditorData,
                Entry.Desc->ID,
                CanonicalDescriptor,
                CanonicalError)
            && CanonicalDescriptor == Entry.Desc;
#else
        return false;
#endif
    }

    bool TryGetNativePath(const int32 EntryIndex, FString& OutPath)
    {
        TArray<FString> Segments;
        int32 CurrentIndex = EntryIndex;
        while (Entries.IsValidIndex(CurrentIndex) && Segments.Num() < Entries.Num())
        {
            const FAuthoredStateEntry& Entry = Entries[CurrentIndex];
            if (Entry.State == nullptr)
            {
                break;
            }
            if (!Context.ConsumeMemberPath(1))
            {
                OutPath.Reset();
                return false;
            }
            Segments.Add(Entry.State->Name.IsNone() ? TEXT("state") : Entry.State->Name.ToString());
            CurrentIndex = Entry.ParentIndex;
        }
        Algo::Reverse(Segments);
        OutPath = FString::Join(Segments, TEXT("/"));
        return true;
    }

    FString DescribeState(const UStateTreeState& State) const
    {
        if (IsCanonicalState(State))
        {
            return FString::Printf(TEXT("state@%s"), *GuidText(State.ID));
        }
        return FString::Printf(
            TEXT("State '%s' (native id %s; not canonical)"),
            State.Name.IsNone() ? TEXT("None") : *State.Name.ToString(),
            *GuidText(State.ID));
    }

    void AddComments(FSalObjectBuilder& Builder) const
    {
        Context.AddComments(Builder);
    }

    void AddDiagnostics(TArray<TSharedPtr<FJsonObject>>& OutDiagnostics) const
    {
        Context.AddDiagnostics(OutDiagnostics);
    }

private:
    struct FPending
    {
        const UStateTreeState* State = nullptr;
        int32 ParentIndex = INDEX_NONE;
        FString MemberName;
        bool bExit = false;
    };

    bool IsOwnedByEditorData(const UStateTreeState* State) const
    {
        return State != nullptr && State->GetTypedOuter<UStateTreeEditorData>() == &EditorData;
    }

    void MarkHierarchyIncomplete(const FString& Message)
    {
        if (bHierarchyComplete)
        {
            Context.AddIssue(Message);
        }
        bHierarchyComplete = false;
        bStateIdentityComplete = false;
    }

    void MarkCycleUnsafe(const int32 ParentIndex, const int32 CycleStartIndex)
    {
        int32 CurrentIndex = ParentIndex;
        int32 Guard = 0;
        while (Entries.IsValidIndex(CurrentIndex) && Guard++ < Entries.Num())
        {
            if (Entries[CurrentIndex].State != nullptr)
            {
                UnsafeStates.Add(Entries[CurrentIndex].State);
            }
            if (CurrentIndex == CycleStartIndex)
            {
                return;
            }
            CurrentIndex = Entries[CurrentIndex].ParentIndex;
        }
        MarkHierarchyIncomplete(TEXT("State Children cycle ancestry could not be reconstructed safely."));
    }

    void Build()
    {
        TArray<FPending> Stack;
        Stack.Reserve(FMath::Min(EditorData.SubTrees.Num(), MaxAuthoredStates / 2) * 2);
        FUniqueNameAllocator RootNames;
        TArray<FPending> PendingRoots;
        PendingRoots.Reserve(FMath::Min(EditorData.SubTrees.Num(), MaxAuthoredStateLinks));
        for (int32 Index = 0; Index < EditorData.SubTrees.Num(); ++Index)
        {
            const UStateTreeState* Root = EditorData.SubTrees[Index];
            if (++InspectedLinks > MaxAuthoredStateLinks)
            {
                MarkHierarchyIncomplete(FString::Printf(
                    TEXT("authored hierarchy traversal stopped after %d State links"),
                    MaxAuthoredStateLinks));
                break;
            }
            if (Root == nullptr)
            {
                Context.AddIssue(FString::Printf(TEXT("null top-level State at authored index %d"), Index));
                continue;
            }
            if (!IsOwnedByEditorData(Root))
            {
                Context.AddIssue(FString::Printf(
                    TEXT("rejected top-level State pointer outside the bound UStateTreeEditorData at authored index %d: %s"),
                    Index,
                    *Root->GetPathName()));
                continue;
            }
            const FString Name = Root->Name.IsNone() ? TEXT("state") : Root->Name.ToString();
            PendingRoots.Add({Root, INDEX_NONE, RootNames.Allocate(Name, TEXT("state")), false});
        }
        for (int32 Index = PendingRoots.Num() - 1; Index >= 0; --Index)
        {
            Stack.Add(MoveTemp(PendingRoots[Index]));
        }

        TMap<const UStateTreeState*, int32> ActiveEntries;
        TSet<const UStateTreeState*> Visited;
        while (!Stack.IsEmpty())
        {
            const FPending Pending = Stack.Pop(EAllowShrinking::No);
            if (Pending.bExit)
            {
                ActiveEntries.Remove(Pending.State);
                continue;
            }
            if (!IsOwnedByEditorData(Pending.State))
            {
                Context.AddIssue(FString::Printf(
                    TEXT("rejected child State pointer outside the bound UStateTreeEditorData: %s"),
                    Pending.State != nullptr ? *Pending.State->GetPathName() : TEXT("null")));
                continue;
            }
            if (const int32* CycleStartIndex = ActiveEntries.Find(Pending.State))
            {
                MarkCycleUnsafe(Pending.ParentIndex, *CycleStartIndex);
                Context.AddIssue(FString::Printf(
                    TEXT("State Children cycle detected while revisiting native State id %s; every State in the cycle is non-canonical."),
                    *GuidText(Pending.State->ID)));
                continue;
            }
            if (Visited.Contains(Pending.State))
            {
                UnsafeStates.Add(Pending.State);
                Context.AddIssue(FString::Printf(
                    TEXT("State has repeated authored ownership and is non-canonical: native id %s"),
                    *GuidText(Pending.State->ID)));
                continue;
            }
            if (Entries.Num() >= MaxAuthoredStates)
            {
                MarkHierarchyIncomplete(FString::Printf(
                    TEXT("authored hierarchy traversal stopped after %d unique States"),
                    MaxAuthoredStates));
                return;
            }

            Visited.Add(Pending.State);
            const int32 EntryIndex = Entries.Add({
                Pending.State,
                Pending.ParentIndex,
                {},
                Pending.MemberName});
            ActiveEntries.Add(Pending.State, EntryIndex);
            if (!Context.ConsumeIdentityObject())
            {
                bStateIdentityComplete = false;
            }
            ById.FindOrAdd(Pending.State->ID).Add(EntryIndex);
            if (Pending.ParentIndex == INDEX_NONE)
            {
                Roots.Add(EntryIndex);
            }
            else if (Entries.IsValidIndex(Pending.ParentIndex))
            {
                Entries[Pending.ParentIndex].Children.Add(EntryIndex);
            }

            Stack.Add({Pending.State, INDEX_NONE, FString(), true});
            TArray<FPending> Children;
            Children.Reserve(FMath::Min(
                Pending.State->Children.Num(),
                FMath::Max(0, MaxAuthoredStateLinks - InspectedLinks)));
            FUniqueNameAllocator ChildNames;
            bool bBudgetExceeded = false;
            for (int32 ChildIndex = 0; ChildIndex < Pending.State->Children.Num(); ++ChildIndex)
            {
                if (++InspectedLinks > MaxAuthoredStateLinks)
                {
                    MarkHierarchyIncomplete(FString::Printf(
                        TEXT("authored hierarchy traversal stopped after %d State links"),
                        MaxAuthoredStateLinks));
                    bBudgetExceeded = true;
                    break;
                }
                const UStateTreeState* Child = Pending.State->Children[ChildIndex];
                if (Child == nullptr)
                {
                    Context.AddIssue(FString::Printf(
                        TEXT("null child State at native State id %s Children[%d]"),
                        *GuidText(Pending.State->ID),
                        ChildIndex));
                    continue;
                }
                if (!IsOwnedByEditorData(Child))
                {
                    Context.AddIssue(FString::Printf(
                        TEXT("rejected child State pointer outside the bound UStateTreeEditorData at native State id %s Children[%d]: %s"),
                        *GuidText(Pending.State->ID),
                        ChildIndex,
                        *Child->GetPathName()));
                    continue;
                }
                const FString Name = Child->Name.IsNone() ? TEXT("state") : Child->Name.ToString();
                Children.Add({
                    Child,
                    EntryIndex,
                    ChildNames.Allocate(Name, TEXT("state")),
                    false});
            }
            if (bBudgetExceeded)
            {
                return;
            }
            for (int32 ChildIndex = Children.Num() - 1; ChildIndex >= 0; --ChildIndex)
            {
                Stack.Add(MoveTemp(Children[ChildIndex]));
            }
        }
    }

    template <typename CountMapType>
    void AddIdentityIssues(const TCHAR* Kind, const CountMapType& Counts)
    {
        for (const TPair<FGuid, int32>& Pair : Counts)
        {
            if (!Pair.Key.IsValid())
            {
                Context.AddIssue(FString::Printf(
                    TEXT("Authored %s has an invalid native id and is not an exact reference."),
                    Kind));
            }
            else if (Pair.Value > 1)
            {
                Context.AddIssue(FString::Printf(
                    TEXT("Authored %s native id %s occurs %d times and is not an exact reference."),
                    Kind,
                    *GuidText(Pair.Key),
                    Pair.Value));
            }
        }
    }

    void FinalizeStateIdentities()
    {
        if (!bHierarchyComplete || !bStateIdentityComplete)
        {
            Context.AddIssue(TEXT("State identity scan is incomplete; emitted States omit canonical ids and exact State links are unavailable."));
            return;
        }
        TMap<FGuid, int32> Counts;
        for (const TPair<FGuid, TArray<int32>>& Pair : ById)
        {
            Counts.Add(Pair.Key, Pair.Value.Num());
        }
        AddIdentityIssues(TEXT("state"), Counts);
    }

    bool AddNodeIdentity(
        const FStateTreeEditorNode& Node,
        const int32 OwnerStateIndex,
        const int32 OwnerTransitionIndex,
        const FString& Role,
        const int32 RoleIndex,
        const FString& MemberName,
        const bool bPropertyFunction = false,
        const int32 BindingIndex = INDEX_NONE,
        const FString& BindingTargetPath = FString())
    {
        if (!Context.ConsumeIdentityObject())
        {
            return false;
        }
        const UScriptStruct* NodeStruct = Node.Node.GetScriptStruct();
        if (IsBlueprintNodeWrapper(NodeStruct))
        {
            const UClass* SelectedClass = BlueprintNodeClass(Node);
            if (SelectedClass == nullptr)
            {
                Context.AddIssue(FString::Printf(
                    TEXT("Blueprint StateTree Node %s has no selected native Class."),
                    *GuidText(Node.ID)));
            }
            if (Node.InstanceObject == nullptr)
            {
                Context.AddIssue(FString::Printf(
                    TEXT("Blueprint StateTree Node %s has no authored InstanceObject."),
                    *GuidText(Node.ID)));
            }
            else if (SelectedClass != nullptr && Node.InstanceObject->GetClass() != SelectedClass)
            {
                Context.AddIssue(FString::Printf(
                    TEXT("Blueprint StateTree Node %s selects %s but stores InstanceObject Class %s."),
                    *GuidText(Node.ID),
                    *SelectedClass->GetPathName(),
                    *Node.InstanceObject->GetClass()->GetPathName()));
            }
        }
        const int32 EntryIndex = NodeEntries.Add({
            &Node,
            OwnerStateIndex,
            OwnerTransitionIndex,
            RoleIndex,
            BindingIndex,
            Role,
            MemberName,
            BindingTargetPath,
            bPropertyFunction});
        NodeById.FindOrAdd(Node.ID).Add(EntryIndex);
        return true;
    }

    bool AddNodeArray(
        const TArray<FStateTreeEditorNode>& Nodes,
        const int32 OwnerStateIndex,
        const int32 OwnerTransitionIndex,
        const FString& Role)
    {
        FUniqueNameAllocator Names;
        for (int32 Index = 0; Index < Nodes.Num(); ++Index)
        {
            const FStateTreeEditorNode& Node = Nodes[Index];
            const FString Preferred = Node.GetName().IsNone()
                ? FString::Printf(TEXT("node_%d"), Index + 1)
                : Node.GetName().ToString();
            if (!AddNodeIdentity(
                    Node,
                    OwnerStateIndex,
                    OwnerTransitionIndex,
                    Role,
                    Index,
                    Names.Allocate(Preferred)))
            {
                return false;
            }
        }
        return true;
    }

    bool AddTransitionIdentity(
        const FStateTreeTransition& Transition,
        const int32 OwnerStateIndex,
        const int32 ArrayIndex,
        int32& OutEntryIndex)
    {
        if (!Context.ConsumeIdentityObject())
        {
            return false;
        }
        OutEntryIndex = TransitionEntries.Add({
            &Transition,
            OwnerStateIndex,
            ArrayIndex,
            FString::Printf(TEXT("transition_%d"), ArrayIndex + 1)});
        TransitionById.FindOrAdd(Transition.ID).Add(OutEntryIndex);
        return true;
    }

    void ScanNodeAndTransitionIdentities()
    {
        if (!bHierarchyComplete)
        {
            bNodeIdentityComplete = false;
            bTransitionIdentityComplete = false;
            return;
        }
        if (!AddNodeArray(EditorData.Evaluators, INDEX_NONE, INDEX_NONE, TEXT("Evaluators"))
            || !AddNodeArray(EditorData.GlobalTasks, INDEX_NONE, INDEX_NONE, TEXT("GlobalTasks")))
        {
            bNodeIdentityComplete = false;
            bTransitionIdentityComplete = false;
            return;
        }

        for (int32 StateIndex = 0; StateIndex < Entries.Num(); ++StateIndex)
        {
            const FAuthoredStateEntry& Entry = Entries[StateIndex];
            if (Entry.State == nullptr)
            {
                continue;
            }
            const UStateTreeState& State = *Entry.State;
            if (!AddNodeArray(State.EnterConditions, StateIndex, INDEX_NONE, TEXT("EnterConditions"))
                || !AddNodeArray(State.Tasks, StateIndex, INDEX_NONE, TEXT("Tasks")))
            {
                bNodeIdentityComplete = false;
                bTransitionIdentityComplete = false;
                return;
            }
            if (State.SingleTask.Node.IsValid()
                && !AddNodeIdentity(
                    State.SingleTask,
                    StateIndex,
                    INDEX_NONE,
                    TEXT("SingleTask"),
                    0,
                    TEXT("SingleTask")))
            {
                bNodeIdentityComplete = false;
                bTransitionIdentityComplete = false;
                return;
            }
            if (!AddNodeArray(State.Considerations, StateIndex, INDEX_NONE, TEXT("Considerations")))
            {
                bNodeIdentityComplete = false;
                bTransitionIdentityComplete = false;
                return;
            }
            for (int32 TransitionIndex = 0; TransitionIndex < State.Transitions.Num(); ++TransitionIndex)
            {
                const FStateTreeTransition& Transition = State.Transitions[TransitionIndex];
                int32 AuthoredTransitionIndex = INDEX_NONE;
                if (!AddTransitionIdentity(Transition, StateIndex, TransitionIndex, AuthoredTransitionIndex)
                    || !AddNodeArray(
                        Transition.Conditions,
                        StateIndex,
                        AuthoredTransitionIndex,
                        TEXT("Conditions")))
                {
                    bNodeIdentityComplete = false;
                    bTransitionIdentityComplete = false;
                    return;
                }
            }
        }

        FUniqueNameAllocator PropertyFunctionNames;
        const TConstArrayView<FStateTreePropertyPathBinding> Bindings = EditorData.EditorBindings.GetBindings();
        for (int32 BindingIndex = 0; BindingIndex < Bindings.Num(); ++BindingIndex)
        {
            const FStateTreePropertyPathBinding& Binding = Bindings[BindingIndex];
            const FConstStructView FunctionNodeView = Binding.GetPropertyFunctionNode();
            const FStateTreeEditorNode* FunctionNode = FunctionNodeView.GetPtr<const FStateTreeEditorNode>();
            if (FunctionNode == nullptr)
            {
                continue;
            }
            const FString Preferred = FunctionNode->GetName().IsNone()
                ? FString::Printf(TEXT("property_function_%d"), BindingIndex + 1)
                : FunctionNode->GetName().ToString();
            if (!AddNodeIdentity(
                    *FunctionNode,
                    INDEX_NONE,
                    INDEX_NONE,
                    TEXT("PropertyFunctions"),
                    BindingIndex,
                    PropertyFunctionNames.Allocate(Preferred, TEXT("property_function")),
                    true,
                    BindingIndex,
                    Binding.GetTargetPath().ToString()))
            {
                bNodeIdentityComplete = false;
                break;
            }
        }

        TMap<FGuid, int32> NodeCounts;
        for (const TPair<FGuid, TArray<int32>>& Pair : NodeById)
        {
            NodeCounts.Add(Pair.Key, Pair.Value.Num());
        }
        TMap<FGuid, int32> TransitionCounts;
        for (const TPair<FGuid, TArray<int32>>& Pair : TransitionById)
        {
            TransitionCounts.Add(Pair.Key, Pair.Value.Num());
        }
        AddIdentityIssues(TEXT("node"), NodeCounts);
        AddIdentityIssues(TEXT("transition"), TransitionCounts);
    }

    bool AddParameterBag(
        const FInstancedPropertyBag& Bag,
        const FGuid& ContainerId,
        const int32 OwnerStateIndex,
        const bool bRoot)
    {
        const UPropertyBag* BagStruct = Bag.GetPropertyBagStruct();
        const TConstArrayView<FPropertyBagPropertyDesc> Descs = BagStruct != nullptr
            ? BagStruct->GetPropertyDescs()
            : TConstArrayView<FPropertyBagPropertyDesc>();
        ++ParameterContainerCounts.FindOrAdd(ContainerId);
        FUniqueNameAllocator Names;
        for (int32 DescriptorIndex = 0; DescriptorIndex < Descs.Num(); ++DescriptorIndex)
        {
            if (!Context.ConsumeIdentityObject())
            {
                return false;
            }
            const FPropertyBagPropertyDesc& Desc = Descs[DescriptorIndex];
            const int32 EntryIndex = ParameterEntries.Add({
                &Bag,
                &Desc,
                ContainerId,
                OwnerStateIndex,
                DescriptorIndex,
                Names.Allocate(Desc.Name.ToString(), TEXT("parameter")),
                bRoot});
            ParameterById.FindOrAdd(ParameterIdentityText(ContainerId, Desc.ID)).Add(EntryIndex);
            if (Desc.CachedProperty == nullptr)
            {
                Context.AddIssue(FString::Printf(
                    TEXT("Parameter %s has no cached native FProperty; native type and value are unavailable."),
                    *ParameterIdentityText(ContainerId, Desc.ID)));
            }
        }
        return true;
    }

    void ScanParameterIdentities()
    {
        if (!bHierarchyComplete)
        {
            bParameterIdentityComplete = false;
            Context.AddIssue(TEXT("Parameter identity scan cannot prove complete container uniqueness because the State hierarchy scan is incomplete; discovered Parameters remain visible without canonical ids."));
        }

        if (!AddParameterBag(
                EditorData.GetRootParametersPropertyBag(),
                EditorData.GetRootParametersGuid(),
                INDEX_NONE,
                true))
        {
            bParameterIdentityComplete = false;
            Context.AddIssue(TEXT("Parameter identity scan is incomplete; no Parameter exact reference is canonical."));
            return;
        }
        for (int32 StateIndex = 0; StateIndex < Entries.Num(); ++StateIndex)
        {
            const FAuthoredStateEntry& Entry = Entries[StateIndex];
            if (Entry.State == nullptr)
            {
                continue;
            }
            if (!AddParameterBag(
                    Entry.State->Parameters.Parameters,
                    Entry.State->Parameters.ID,
                    StateIndex,
                    false))
            {
                bParameterIdentityComplete = false;
                Context.AddIssue(TEXT("Parameter identity scan is incomplete; no Parameter exact reference is canonical."));
                return;
            }
        }

        for (const TPair<FGuid, int32>& Pair : ParameterContainerCounts)
        {
            if (!Pair.Key.IsValid())
            {
                bool bHasParameters = false;
                for (const FAuthoredParameterEntry& Entry : ParameterEntries)
                {
                    if (Entry.ContainerId == Pair.Key)
                    {
                        bHasParameters = true;
                        break;
                    }
                }
                if (bHasParameters)
                {
                    Context.AddIssue(TEXT("A Parameter container has an invalid native id; its Parameters are not exact references."));
                }
            }
            else if (Pair.Value > 1)
            {
                Context.AddIssue(FString::Printf(
                    TEXT("Parameter container native id %s occurs %d times; Parameters in those containers are not exact references."),
                    *GuidText(Pair.Key),
                    Pair.Value));
            }
        }
        for (const TPair<FString, TArray<int32>>& Pair : ParameterById)
        {
            const FAuthoredParameterEntry* Entry = Pair.Value.IsEmpty()
                ? nullptr
                : GetParameterEntry(Pair.Value[0]);
            if (Entry == nullptr || Entry->Desc == nullptr)
            {
                continue;
            }
            if (!Entry->Desc->ID.IsValid())
            {
                Context.AddIssue(FString::Printf(
                    TEXT("Parameter in container %s has an invalid native property id; exact reference unavailable."),
                    *GuidText(Entry->ContainerId)));
            }
            else if (Pair.Value.Num() > 1)
            {
                Context.AddIssue(FString::Printf(
                    TEXT("Parameter property id %s occurs %d times in container %s; exact reference unavailable."),
                    *GuidText(Entry->Desc->ID),
                    Pair.Value.Num(),
                    *GuidText(Entry->ContainerId)));
            }
        }
    }

    void ScanContextIdentities()
    {
        if (EditorData.Schema == nullptr)
        {
            return;
        }
        const TConstArrayView<FStateTreeExternalDataDesc> Descs = EditorData.Schema->GetContextDataDescs();
        for (int32 DescriptorIndex = 0; DescriptorIndex < Descs.Num(); ++DescriptorIndex)
        {
            if (!Context.ConsumeIdentityObject())
            {
                bContextIdentityComplete = false;
                Context.AddIssue(TEXT("Schema Context Data identity scan is incomplete; no Context Data exact reference is canonical."));
                return;
            }
            const FStateTreeExternalDataDesc& Desc = Descs[DescriptorIndex];
            const int32 EntryIndex = ContextEntries.Add({&Desc, DescriptorIndex});
#if WITH_EDITORONLY_DATA
            ContextById.FindOrAdd(Desc.ID).Add(EntryIndex);
#else
            bContextIdentityComplete = false;
#endif
            if (Desc.Struct == nullptr)
            {
                Context.AddIssue(FString::Printf(
                    TEXT("Schema Context Data %s has no native Struct; its descriptor remains readable but member Binding resolution is unavailable."),
                    *Desc.Name.ToString()));
            }
        }
#if WITH_EDITORONLY_DATA
        TMap<FGuid, int32> Counts;
        for (const TPair<FGuid, TArray<int32>>& Pair : ContextById)
        {
            Counts.Add(Pair.Key, Pair.Value.Num());
        }
        AddIdentityIssues(TEXT("Schema Context Data"), Counts);
#endif
    }

    void ValidateStateLink(const FStateTreeStateLink& Link, const FString& OwnerDescription)
    {
#if WITH_EDITORONLY_DATA
        if (Link.LinkType != EStateTreeTransitionType::GotoState)
        {
            return;
        }
        if (!Link.ID.IsValid())
        {
            Context.AddIssue(FString::Printf(
                TEXT("%s has a GotoState link with an invalid native State id; stable reference unavailable."),
                *OwnerDescription));
            return;
        }
        if (!IsCanonicalStateId(Link.ID))
        {
            const TArray<int32>* Matches = ById.Find(Link.ID);
            const TCHAR* Reason = Matches == nullptr || Matches->IsEmpty()
                ? TEXT("dangling")
                : Matches->Num() > 1
                    ? TEXT("ambiguous")
                    : TEXT("unsafe");
            Context.AddIssue(FString::Printf(
                TEXT("%s has a %s GotoState link to native State id %s; stable reference unavailable."),
                *OwnerDescription,
                Reason,
                *GuidText(Link.ID)));
        }
#endif
    }

    void ValidateStateLinks()
    {
        if (!bHierarchyComplete || !bStateIdentityComplete)
        {
            return;
        }
        for (const FAuthoredStateEntry& Entry : Entries)
        {
            if (Entry.State == nullptr)
            {
                continue;
            }
            const UStateTreeState& State = *Entry.State;
            if (State.Type == EStateTreeStateType::Linked
                && Context.ConsumeAnalysisObject())
            {
                ValidateStateLink(State.LinkedSubtree, DescribeState(State));
            }
            for (const FStateTreeTransition& Transition : State.Transitions)
            {
                if (!Context.ConsumeAnalysisObject())
                {
                    return;
                }
                ValidateStateLink(
                    Transition.State,
                    FString::Printf(TEXT("Transition native id %s"), *GuidText(Transition.ID)));
            }
        }
    }

    const UStateTreeEditorData& EditorData;
    FReadContext& Context;
    TArray<FAuthoredStateEntry> Entries;
    TArray<int32> Roots;
    TMap<FGuid, TArray<int32>> ById;
    TArray<FAuthoredNodeEntry> NodeEntries;
    TArray<FAuthoredTransitionEntry> TransitionEntries;
    TMap<FGuid, TArray<int32>> NodeById;
    TMap<FGuid, TArray<int32>> TransitionById;
    TArray<FAuthoredParameterEntry> ParameterEntries;
    TMap<FString, TArray<int32>> ParameterById;
    TMap<FGuid, int32> ParameterContainerCounts;
    TArray<FAuthoredContextEntry> ContextEntries;
    TMap<FGuid, TArray<int32>> ContextById;
    TSet<const UStateTreeState*> UnsafeStates;
    int32 InspectedLinks = 0;
    bool bHierarchyComplete = true;
    bool bStateIdentityComplete = true;
    bool bNodeIdentityComplete = true;
    bool bTransitionIdentityComplete = true;
    bool bParameterIdentityComplete = true;
    bool bContextIdentityComplete = true;
};

FString GuidText(const FGuid& Guid)
{
    return Guid.ToString(EGuidFormats::DigitsWithHyphensLower);
}

FString ParameterIdentityText(const FGuid& ContainerId, const FGuid& PropertyId)
{
    return GuidText(ContainerId) + TEXT("/") + GuidText(PropertyId);
}

TSharedPtr<FJsonValue> NativeNameValue(const FName Name)
{
    const FString Text = Name.IsNone() ? TEXT("None") : Name.ToString();
    return FSalObjectBuilder::IsIdentifier(Text) ? Value::Name(Text) : Value::String(Text);
}

template <typename T>
TSharedPtr<FJsonValue> EnumValue(const T ValueToEncode)
{
    const UEnum* Enum = StaticEnum<T>();
    const FString Name = Enum != nullptr
        ? Enum->GetNameStringByValue(static_cast<int64>(ValueToEncode))
        : FString::Printf(TEXT("%lld"), static_cast<long long>(ValueToEncode));
    return FSalObjectBuilder::IsIdentifier(Name) ? Value::Name(Name) : Value::String(Name);
}

FString StateTreePropertyFlagsText(const uint64 Flags)
{
    return NativePropertyFlagsText(Flags);
}

FString StateTreePropertyTypeText(const FProperty* Property)
{
    return NativePropertyTypeText(Property);
}

UStateTree* ResolvedStateTree(const FSalResolvedTarget& Target)
{
    return Target.Kind == ESalTargetKind::Asset
        && Target.HasInterface(FName(InterfaceName))
        ? Cast<UStateTree>(Target.Object)
        : nullptr;
}

UStateTreeEditorData* AuthoredData(UStateTree* StateTree)
{
#if WITH_EDITORONLY_DATA
    return StateTree != nullptr ? Cast<UStateTreeEditorData>(StateTree->EditorData) : nullptr;
#else
    return nullptr;
#endif
}

TSharedPtr<FJsonObject> QueryError(
    const FString& Code,
    const FString& Message,
    const FString& Operation = FString(),
    const FString& Ref = FString(),
    const TArray<FString>& Supported = {})
{
    FSalDiagnosticBuilder Diagnostic = FSalDiagnostics::Error(Code, Message).Interface(InterfaceName);
    if (!Operation.IsEmpty())
    {
        Diagnostic.Operation(Operation);
    }
    if (!Ref.IsEmpty())
    {
        Diagnostic.Ref(Ref);
    }
    if (!Supported.IsEmpty())
    {
        Diagnostic.Supported(Supported);
    }
    return FSalDiagnostics::Result(Diagnostic.Build());
}

bool HasUnsupportedExactClauses(const FSalQuery& Query)
{
    return Query.Where.IsValid()
        || !Query.OrderBy.IsEmpty()
        || Query.PageLimit > 0
        || !Query.PageAfter.IsEmpty();
}

bool HasUnsupportedCollectionClauses(const FSalQuery& Query)
{
    return Query.Where.IsValid()
        || !Query.OrderBy.IsEmpty()
        || !Query.With.IsEmpty();
}

bool WantsExactSchema(const FSalQuery& Query)
{
    return Query.With.Num() == 1 && Query.With[0] == TEXT("schema");
}

bool HasUnsupportedExactDetail(const FSalQuery& Query)
{
    return !Query.With.IsEmpty() && !WantsExactSchema(Query);
}

TSharedPtr<FJsonObject> StateTreeAssetArgs(
    const FSalResolvedTarget& Target,
    const UStateTree* StateTree,
    const UStateTreeEditorData* EditorData)
{
    TSharedPtr<FJsonObject> Args = MakeShared<FJsonObject>();
    Args->SetStringField(TEXT("path"), Target.AssetPath);
    Args->SetStringField(TEXT("type"), StateTree->GetClass()->GetPathName());
    TArray<TSharedPtr<FJsonValue>> Domains;
    Domains.Add(Value::Name(TEXT("asset")));
    Domains.Add(Value::Name(InterfaceName));
    Args->SetArrayField(TEXT("domains"), Domains);
    Args->SetBoolField(TEXT("loaded"), true);
    if (EditorData != nullptr && EditorData->Schema != nullptr)
    {
        Args->SetStringField(TEXT("Schema"), EditorData->Schema->GetClass()->GetPathName());
    }
    if (EditorData != nullptr && EditorData->EditorSchema != nullptr)
    {
        Args->SetStringField(
            TEXT("EditorSchema"),
            EditorData->EditorSchema->GetClass()->GetPathName());
    }
    Args->SetBoolField(TEXT("IsReadyToRun"), StateTree->IsReadyToRun());
    Args->SetNumberField(TEXT("LastCompiledEditorDataHash"), StateTree->LastCompiledEditorDataHash);
    if (EditorData != nullptr)
    {
        Args->SetField(
            TEXT("GlobalTasksCompletion"),
            EnumValue(EditorData->GlobalTasksCompletion));
        Args->SetNumberField(
            TEXT("EditorDataHash"),
            UStateTreeEditingSubsystem::CalculateStateTreeHash(StateTree));
    }
    return Args;
}

TSharedPtr<FJsonValue> StateTreeAssetValue(
    const FSalResolvedTarget& Target,
    const UStateTree* StateTree,
    const UStateTreeEditorData* EditorData)
{
    return Value::Call(TEXT("asset"), StateTreeAssetArgs(Target, StateTree, EditorData));
}

TSharedPtr<FJsonValue> StateTreeAssetIdentityValue(
    const FSalResolvedTarget& Target,
    const UStateTree* StateTree)
{
    TSharedPtr<FJsonObject> Args = MakeShared<FJsonObject>();
    Args->SetStringField(TEXT("path"), Target.AssetPath);
    Args->SetStringField(TEXT("type"), StateTree->GetClass()->GetPathName());
    return Value::Call(TEXT("asset"), Args);
}

TSharedPtr<FJsonValue> StateLinkValue(
    const FStateTreeStateLink& Link,
    const FAuthoredStateIndex& StateIndex)
{
    TSharedPtr<FJsonObject> Object = MakeShared<FJsonObject>();
#if WITH_EDITORONLY_DATA
    Object->SetField(TEXT("Name"), NativeNameValue(Link.Name));
    Object->SetField(TEXT("LinkType"), EnumValue(Link.LinkType));
    if (Link.LinkType == EStateTreeTransitionType::GotoState)
    {
        if (StateIndex.IsCanonicalStateId(Link.ID))
        {
            Object->SetField(TEXT("ID"), Value::Stable(TEXT("state"), GuidText(Link.ID)));
        }
    }
#endif
    Object->SetField(TEXT("Fallback"), EnumValue(Link.Fallback));
    return MakeShared<FJsonValueObject>(Object);
}

TSharedPtr<FJsonValue> EventValue(const FStateTreeEventDesc& Event)
{
    TSharedPtr<FJsonObject> Object = MakeShared<FJsonObject>();
    if (Event.Tag.IsValid())
    {
        Object->SetStringField(TEXT("Tag"), Event.Tag.ToString());
    }
    if (Event.PayloadStruct != nullptr)
    {
        Object->SetStringField(TEXT("PayloadStruct"), Event.PayloadStruct->GetPathName());
    }
    Object->SetBoolField(TEXT("bConsumeEventOnSelect"), Event.bConsumeEventOnSelect);
    return MakeShared<FJsonValueObject>(Object);
}

const UClass* BlueprintNodeClass(const FStateTreeEditorNode& Node)
{
    const UScriptStruct* NodeStruct = Node.Node.GetScriptStruct();
    if (NodeStruct == FStateTreeBlueprintTaskWrapper::StaticStruct())
    {
        const FStateTreeBlueprintTaskWrapper* Wrapper =
            Node.Node.GetPtr<FStateTreeBlueprintTaskWrapper>();
        return Wrapper != nullptr ? Wrapper->TaskClass.Get() : nullptr;
    }
    if (NodeStruct == FStateTreeBlueprintEvaluatorWrapper::StaticStruct())
    {
        const FStateTreeBlueprintEvaluatorWrapper* Wrapper =
            Node.Node.GetPtr<FStateTreeBlueprintEvaluatorWrapper>();
        return Wrapper != nullptr ? Wrapper->EvaluatorClass.Get() : nullptr;
    }
    if (NodeStruct == FStateTreeBlueprintConditionWrapper::StaticStruct())
    {
        const FStateTreeBlueprintConditionWrapper* Wrapper =
            Node.Node.GetPtr<FStateTreeBlueprintConditionWrapper>();
        return Wrapper != nullptr ? Wrapper->ConditionClass.Get() : nullptr;
    }
    if (NodeStruct == FStateTreeBlueprintConsiderationWrapper::StaticStruct())
    {
        const FStateTreeBlueprintConsiderationWrapper* Wrapper =
            Node.Node.GetPtr<FStateTreeBlueprintConsiderationWrapper>();
        return Wrapper != nullptr ? Wrapper->ConsiderationClass.Get() : nullptr;
    }
    return nullptr;
}

bool IsBlueprintNodeWrapper(const UScriptStruct* Struct)
{
    return Struct == FStateTreeBlueprintTaskWrapper::StaticStruct()
        || Struct == FStateTreeBlueprintEvaluatorWrapper::StaticStruct()
        || Struct == FStateTreeBlueprintConditionWrapper::StaticStruct()
        || Struct == FStateTreeBlueprintConsiderationWrapper::StaticStruct();
}

FString NodeTypePath(const FStateTreeEditorNode& Node)
{
    const UScriptStruct* NodeStruct = Node.Node.GetScriptStruct();
    if (IsBlueprintNodeWrapper(NodeStruct))
    {
        if (const UClass* SelectedClass = BlueprintNodeClass(Node))
        {
            return SelectedClass->GetPathName();
        }
        return FString();
    }
    return NodeStruct != nullptr ? NodeStruct->GetPathName() : FString();
}

bool IsStateTreeUsageSurfaceProperty(const FProperty* Property)
{
#if WITH_EDITOR
    const EStateTreePropertyUsage Usage = UE::StateTree::GetUsageFromMetaData(Property);
    return Usage == EStateTreePropertyUsage::Input
        || Usage == EStateTreePropertyUsage::Output
        || Usage == EStateTreePropertyUsage::Context;
#else
    return false;
#endif
}

bool IsCustomAuthoredNodeSurfaceProperty(
    const FProperty* Property,
    const UStruct* SurfaceStruct)
{
    if (Property == nullptr)
    {
        return false;
    }

    const UStruct* Owner = Property->GetOwnerStruct();
    const FName Name = Property->GetFName();
    if (Owner == FStateTreeTaskBase::StaticStruct())
    {
        if (Name == FName(TEXT("bTaskEnabled")))
        {
            return true;
        }
#if WITH_EDITORONLY_DATA
        if (Name == FName(TEXT("bConsideredForCompletion")))
        {
            return SurfaceStruct != FStateTreeBlueprintTaskWrapper::StaticStruct();
        }
#endif
    }
    return Owner == FStateTreeConditionBase::StaticStruct()
        && Name == GET_MEMBER_NAME_CHECKED(FStateTreeConditionBase, EvaluationMode);
}

bool IsAuthoredNodeSurfaceProperty(
    const FProperty* Property,
    const UStruct* SurfaceStruct)
{
    if (Property == nullptr)
    {
        return false;
    }
    const UStruct* Owner = Property->GetOwnerStruct();
    if (Owner == UStateTreeTaskBlueprintBase::StaticClass()
        && Property->GetFName() == FName(TEXT("bCanEditConsideredForCompletion")))
    {
        return false;
    }
    return (Property->HasAnyPropertyFlags(CPF_Edit)
            || IsStateTreeUsageSurfaceProperty(Property)
            || IsCustomAuthoredNodeSurfaceProperty(Property, SurfaceStruct))
        && !Property->HasAnyPropertyFlags(
            CPF_Transient
            | CPF_DuplicateTransient
            | CPF_NonPIEDuplicateTransient
            | CPF_Deprecated
            | CPF_SkipSerialization);
}

TSharedPtr<FJsonValue> NodeSurfacePropertyValue(const FProperty* Property, const void* Container)
{
    if (Property == nullptr || Container == nullptr)
    {
        return Value::Null();
    }
    if (Property->ArrayDim != 1)
    {
        return NativeValue(ExportPropertyValue(Property, Container));
    }
    if (const FBoolProperty* Bool = CastField<FBoolProperty>(Property))
    {
        return Value::Bool(Bool->GetPropertyValue_InContainer(Container));
    }
    if (const FEnumProperty* Enum = CastField<FEnumProperty>(Property))
    {
        const void* Address = Enum->ContainerPtrToValuePtr<void>(Container);
        const int64 Raw = Enum->GetUnderlyingProperty()->GetSignedIntPropertyValue(Address);
        const FString Name = Enum->GetEnum()->GetNameStringByValue(Raw);
        return FSalObjectBuilder::IsIdentifier(Name) ? Value::Name(Name) : Value::String(Name);
    }
    if (const FByteProperty* Byte = CastField<FByteProperty>(Property); Byte != nullptr && Byte->Enum != nullptr)
    {
        const FString Name = Byte->Enum->GetNameStringByValue(Byte->GetPropertyValue_InContainer(Container));
        return FSalObjectBuilder::IsIdentifier(Name) ? Value::Name(Name) : Value::String(Name);
    }
    if (const FNumericProperty* Numeric = CastField<FNumericProperty>(Property))
    {
        const void* Address = Numeric->ContainerPtrToValuePtr<void>(Container);
        if (Numeric->IsFloatingPoint())
        {
            return Value::Number(Numeric->GetFloatingPointPropertyValue(Address));
        }
        if (Numeric->IsInteger() && Numeric->GetSize() < 8)
        {
            return Value::Number(static_cast<double>(Numeric->GetSignedIntPropertyValue(Address)));
        }
    }
    if (const FNameProperty* Name = CastField<FNameProperty>(Property))
    {
        return NativeNameValue(Name->GetPropertyValue_InContainer(Container));
    }
    if (const FStrProperty* String = CastField<FStrProperty>(Property))
    {
        return Value::String(String->GetPropertyValue_InContainer(Container));
    }
    if (const FTextProperty* Text = CastField<FTextProperty>(Property))
    {
        return Value::String(Text->GetPropertyValue_InContainer(Container).ToString());
    }
    return NativeValue(ExportPropertyValue(Property, Container));
}

TSharedPtr<FJsonObject> AuthoredNodeSurfaceValue(
    const UStruct* Struct,
    const void* Memory,
    FReadContext& Context)
{
    TSharedPtr<FJsonObject> Object = MakeShared<FJsonObject>();
    if (Struct == nullptr || Memory == nullptr)
    {
        return Object;
    }
    for (TFieldIterator<FProperty> It(Struct); It; ++It)
    {
        const FProperty* Property = *It;
        if (!IsAuthoredNodeSurfaceProperty(Property, Struct))
        {
            continue;
        }
        const FString NativeText = ExportPropertyValue(Property, Memory);
        if (!Context.ConsumeNativeSurfaceField(NativeText.Len()))
        {
            break;
        }
        Object->SetField(Property->GetName(), NodeSurfacePropertyValue(Property, Memory));
    }
    return Object;
}

TSharedPtr<FJsonValue> NodeValue(
    const FStateTreeEditorNode& Node,
    const FAuthoredStateIndex& StateIndex,
    FReadContext* Context = nullptr,
    const bool bExact = false)
{
    TSharedPtr<FJsonObject> Args = MakeShared<FJsonObject>();
    if (StateIndex.IsCanonicalNode(Node))
    {
        Args->SetStringField(TEXT("id"), GuidText(Node.ID));
    }
    Args->SetStringField(TEXT("type"), NodeTypePath(Node));
    if (Node.ExpressionIndent != 0)
    {
        Args->SetNumberField(TEXT("ExpressionIndent"), Node.ExpressionIndent);
    }
    if (Node.ExpressionOperand != EStateTreeExpressionOperand::And)
    {
        Args->SetField(TEXT("ExpressionOperand"), EnumValue(Node.ExpressionOperand));
    }
    if (bExact && Context != nullptr)
    {
        const TStructView<FStateTreeNodeBase> NodeView = Node.GetNode();
        if (NodeView.IsValid())
        {
            Args->SetObjectField(
                TEXT("Node"),
                AuthoredNodeSurfaceValue(NodeView.GetScriptStruct(), NodeView.GetMemory(), *Context));
        }
        const FStateTreeDataView InstanceView = Node.GetInstance();
        if (InstanceView.IsValid())
        {
            Args->SetObjectField(
                TEXT("Instance"),
                AuthoredNodeSurfaceValue(InstanceView.GetStruct(), InstanceView.GetMemory(), *Context));
        }
        const FStateTreeDataView RuntimeView = Node.GetExecutionRuntimeData();
        if (RuntimeView.IsValid())
        {
            Args->SetObjectField(
                TEXT("ExecutionRuntimeData"),
                AuthoredNodeSurfaceValue(RuntimeView.GetStruct(), RuntimeView.GetMemory(), *Context));
        }
    }
    return Value::Call(TEXT("node"), Args);
}

TSharedPtr<FJsonValue> TransitionValue(
    const FStateTreeTransition& Transition,
    const FAuthoredStateIndex& StateIndex)
{
    TSharedPtr<FJsonObject> Args = MakeShared<FJsonObject>();
    if (StateIndex.IsCanonicalTransition(Transition))
    {
        Args->SetStringField(TEXT("id"), GuidText(Transition.ID));
    }
    Args->SetStringField(TEXT("type"), FStateTreeTransition::StaticStruct()->GetPathName());
    Args->SetField(TEXT("Trigger"), EnumValue(Transition.Trigger));
    if (Transition.RequiredEvent.IsValid())
    {
        Args->SetField(TEXT("RequiredEvent"), EventValue(Transition.RequiredEvent));
    }
    Args->SetField(TEXT("State"), StateLinkValue(Transition.State, StateIndex));
    Args->SetField(TEXT("Priority"), EnumValue(Transition.Priority));
    Args->SetBoolField(TEXT("bDelayTransition"), Transition.bDelayTransition);
    if (Transition.bDelayTransition)
    {
        Args->SetNumberField(TEXT("DelayDuration"), Transition.DelayDuration);
        Args->SetNumberField(TEXT("DelayRandomVariance"), Transition.DelayRandomVariance);
    }
    Args->SetBoolField(TEXT("bTransitionEnabled"), Transition.bTransitionEnabled);
    return Value::Call(TEXT("transition"), Args);
}

TSharedPtr<FJsonValue> StateValue(
    const UStateTreeState& State,
    const EStateEmission Emission,
    const FAuthoredStateIndex& StateIndex)
{
    TSharedPtr<FJsonObject> Args = MakeShared<FJsonObject>();
    if (StateIndex.IsCanonicalState(State))
    {
        Args->SetStringField(TEXT("id"), GuidText(State.ID));
    }
    Args->SetStringField(TEXT("type"), State.GetClass()->GetPathName());
    Args->SetField(TEXT("Name"), NativeNameValue(State.Name));
    Args->SetField(TEXT("Type"), EnumValue(State.Type));
    Args->SetField(TEXT("SelectionBehavior"), EnumValue(State.SelectionBehavior));
    Args->SetBoolField(TEXT("bEnabled"), State.bEnabled);
    if (Emission == EStateEmission::Compact)
    {
        return Value::Call(TEXT("state"), Args);
    }

    if (!State.Description.IsEmpty())
    {
        Args->SetStringField(TEXT("Description"), State.Description);
    }
    if (State.Tag.IsValid())
    {
        Args->SetStringField(TEXT("Tag"), State.Tag.ToString());
    }
    if (const FProperty* ColorProperty = FindFProperty<FProperty>(
            UStateTreeState::StaticClass(),
            GET_MEMBER_NAME_CHECKED(UStateTreeState, ColorRef)))
    {
        const FString ColorText = ExportPropertyValue(ColorProperty, &State);
        if (!ColorText.IsEmpty())
        {
            Args->SetField(TEXT("ColorRef"), NativeValue(ColorText));
        }
    }
    Args->SetField(TEXT("TasksCompletion"), EnumValue(State.TasksCompletion));
    const bool bHasAuthoredLinkedSubtree = State.LinkedSubtree.ID.IsValid()
        || !State.LinkedSubtree.Name.IsNone()
        || State.LinkedSubtree.LinkType == EStateTreeTransitionType::GotoState;
    if (bHasAuthoredLinkedSubtree)
    {
        Args->SetField(TEXT("LinkedSubtree"), StateLinkValue(State.LinkedSubtree, StateIndex));
    }
    if (State.LinkedAsset != nullptr)
    {
        Args->SetStringField(TEXT("LinkedAsset"), State.LinkedAsset->GetPathName());
    }
    Args->SetBoolField(TEXT("bHasCustomTickRate"), State.bHasCustomTickRate);
    if (State.bHasCustomTickRate)
    {
        Args->SetNumberField(TEXT("CustomTickRate"), State.CustomTickRate);
    }
    TSharedPtr<FJsonObject> Parameters = MakeShared<FJsonObject>();
    Parameters->SetStringField(TEXT("ID"), GuidText(State.Parameters.ID));
    Parameters->SetBoolField(TEXT("bFixedLayout"), State.Parameters.bFixedLayout);
    TArray<TSharedPtr<FJsonValue>> PropertyOverrides;
    PropertyOverrides.Reserve(State.Parameters.PropertyOverrides.Num());
    for (const FGuid& PropertyId : State.Parameters.PropertyOverrides)
    {
        PropertyOverrides.Add(Value::String(GuidText(PropertyId)));
    }
    Parameters->SetArrayField(TEXT("PropertyOverrides"), PropertyOverrides);
    Args->SetObjectField(TEXT("Parameters"), Parameters);
    Args->SetBoolField(
        TEXT("bCheckPrerequisitesWhenActivatingChildDirectly"),
        State.bCheckPrerequisitesWhenActivatingChildDirectly);
    Args->SetBoolField(TEXT("bHasRequiredEventToEnter"), State.bHasRequiredEventToEnter);
    if (State.bHasRequiredEventToEnter || State.RequiredEventToEnter.IsValid())
    {
        Args->SetField(TEXT("RequiredEventToEnter"), EventValue(State.RequiredEventToEnter));
    }
    Args->SetNumberField(TEXT("Weight"), State.Weight);
    return Value::Call(TEXT("state"), Args);
}

TSharedPtr<FJsonValue> ParameterNativeValue(const FProperty* Property, const void* Container)
{
    if (Property == nullptr || Container == nullptr)
    {
        return Value::Null();
    }
    if (Property->ArrayDim != 1)
    {
        return NativeValue(ExportPropertyValue(Property, Container));
    }
    if (const FBoolProperty* Bool = CastField<FBoolProperty>(Property))
    {
        return Value::Bool(Bool->GetPropertyValue_InContainer(Container));
    }
    if (const FEnumProperty* Enum = CastField<FEnumProperty>(Property))
    {
        const void* Address = Enum->ContainerPtrToValuePtr<void>(Container);
        const int64 Raw = Enum->GetUnderlyingProperty()->GetSignedIntPropertyValue(Address);
        const FString Name = Enum->GetEnum() != nullptr
            ? Enum->GetEnum()->GetNameStringByValue(Raw)
            : FString();
        if (!Name.IsEmpty())
        {
            return FSalObjectBuilder::IsIdentifier(Name) ? Value::Name(Name) : Value::String(Name);
        }
        return NativeValue(ExportPropertyValue(Property, Container));
    }
    if (const FByteProperty* Byte = CastField<FByteProperty>(Property); Byte != nullptr && Byte->Enum != nullptr)
    {
        const FString Name = Byte->Enum->GetNameStringByValue(Byte->GetPropertyValue_InContainer(Container));
        if (!Name.IsEmpty())
        {
            return FSalObjectBuilder::IsIdentifier(Name) ? Value::Name(Name) : Value::String(Name);
        }
        return NativeValue(ExportPropertyValue(Property, Container));
    }
    if (const FNumericProperty* Numeric = CastField<FNumericProperty>(Property))
    {
        const void* Address = Numeric->ContainerPtrToValuePtr<void>(Container);
        if (Numeric->IsFloatingPoint())
        {
            const double Number = Numeric->GetFloatingPointPropertyValue(Address);
            return FMath::IsFinite(Number)
                ? Value::Number(Number)
                : NativeValue(ExportPropertyValue(Property, Container));
        }
        if (Numeric->IsInteger() && Numeric->GetSize() < 8)
        {
            const bool bUnsigned = CastField<FByteProperty>(Property) != nullptr
                || CastField<FUInt16Property>(Property) != nullptr
                || CastField<FUInt32Property>(Property) != nullptr;
            return Value::Number(bUnsigned
                ? static_cast<double>(Numeric->GetUnsignedIntPropertyValue(Address))
                : static_cast<double>(Numeric->GetSignedIntPropertyValue(Address)));
        }
    }
    if (const FNameProperty* Name = CastField<FNameProperty>(Property))
    {
        return NativeNameValue(Name->GetPropertyValue_InContainer(Container));
    }
    if (const FStrProperty* String = CastField<FStrProperty>(Property))
    {
        return Value::String(String->GetPropertyValue_InContainer(Container));
    }
    return NativeValue(ExportPropertyValue(Property, Container));
}

TSharedPtr<FJsonValue> ParameterValue(
    const FAuthoredParameterEntry& Entry,
    const FAuthoredStateIndex& Index,
    const bool bExact)
{
    TSharedPtr<FJsonObject> Args = MakeShared<FJsonObject>();
    if (Index.IsCanonicalParameter(Entry))
    {
        Args->SetStringField(
            TEXT("id"),
            ParameterIdentityText(Entry.ContainerId, Entry.Desc->ID));
    }
    const FProperty* Property = Entry.Desc != nullptr ? Entry.Desc->CachedProperty : nullptr;
    if (Property != nullptr)
    {
        Args->SetStringField(TEXT("type"), StateTreePropertyTypeText(Property));
    }
    if (Entry.Desc != nullptr)
    {
        Args->SetField(TEXT("Name"), NativeNameValue(Entry.Desc->Name));
    }
    if (bExact && Entry.Desc != nullptr)
    {
        const FConstStructView BagValue = Entry.Bag != nullptr
            ? Entry.Bag->GetValue()
            : FConstStructView();
        if (Property != nullptr && BagValue.IsValid() && BagValue.GetMemory() != nullptr)
        {
            Args->SetField(
                TEXT("Value"),
                ParameterNativeValue(Property, BagValue.GetMemory()));
        }
        Args->SetStringField(
            TEXT("PropertyFlags"),
            StateTreePropertyFlagsText(Entry.Desc->PropertyFlags));
        TArray<TSharedPtr<FJsonValue>> MetaData;
#if WITH_EDITORONLY_DATA
        MetaData.Reserve(Entry.Desc->MetaData.Num());
        for (const FPropertyBagPropertyDescMetaData& Item : Entry.Desc->MetaData)
        {
            TSharedPtr<FJsonObject> Meta = MakeShared<FJsonObject>();
            Meta->SetField(TEXT("Key"), NativeNameValue(Item.Key));
            Meta->SetStringField(TEXT("Value"), Item.Value);
            MetaData.Add(MakeShared<FJsonValueObject>(Meta));
        }
        if (Entry.Desc->MetaClass != nullptr)
        {
            Args->SetStringField(TEXT("MetaClass"), Entry.Desc->MetaClass->GetPathName());
        }
#endif
        if (!MetaData.IsEmpty())
        {
            Args->SetArrayField(TEXT("MetaData"), MetaData);
        }
    }
    return Value::Call(TEXT("parameter"), Args);
}

TSharedPtr<FJsonValue> ContextValue(
    const FStateTreeExternalDataDesc& Desc,
    const FAuthoredContextEntry& Entry,
    const FAuthoredStateIndex& Index)
{
    TSharedPtr<FJsonObject> Args = MakeShared<FJsonObject>();
#if WITH_EDITORONLY_DATA
    if (Index.IsCanonicalContext(Entry))
    {
        Args->SetStringField(TEXT("id"), GuidText(Desc.ID));
    }
#endif
    Args->SetStringField(TEXT("type"), FStateTreeExternalDataDesc::StaticStruct()->GetPathName());
    Args->SetField(TEXT("Name"), NativeNameValue(Desc.Name));
    if (Desc.Struct != nullptr)
    {
        Args->SetStringField(TEXT("Struct"), Desc.Struct->GetPathName());
    }
    Args->SetField(TEXT("Requirement"), EnumValue(Desc.Requirement));
    return Value::Call(TEXT("object"), Args);
}

enum class ERelationshipOwnerKind : uint8
{
    State,
    Node,
    Transition,
    Parameter,
    Context
};

struct FRelationshipPathSegment
{
    FString Name;
    int32 Index = INDEX_NONE;

    static FRelationshipPathSegment Field(const FString& InName)
    {
        return {InName, INDEX_NONE};
    }

    static FRelationshipPathSegment ArrayIndex(const int32 InIndex)
    {
        return {FString(), InIndex};
    }
};

struct FNativeBindingPathKey
{
    FPropertyBindingPath Path;

    FNativeBindingPathKey() = default;

    explicit FNativeBindingPathKey(const FPropertyBindingPath& InPath)
        : Path(InPath)
    {
    }

    bool operator==(const FNativeBindingPathKey& Other) const
    {
        return Path == Other.Path;
    }

    friend uint32 GetTypeHash(const FNativeBindingPathKey& Key)
    {
#if WITH_EDITORONLY_DATA
        uint32 Hash = GetTypeHash(Key.Path.GetStructID());
#else
        uint32 Hash = 0;
#endif
        Hash = HashCombine(Hash, GetTypeHash(Key.Path.GetSegments().Num()));
        for (const FPropertyBindingPathSegment& Segment : Key.Path.GetSegments())
        {
            Hash = HashCombine(Hash, GetTypeHash(Segment.GetName()));
            Hash = HashCombine(Hash, PointerHash(Segment.GetInstanceStruct()));
            Hash = HashCombine(Hash, GetTypeHash(Segment.GetArrayIndex()));
        }
        return Hash;
    }
};

struct FRelationshipEndpoint
{
    ERelationshipOwnerKind OwnerKind = ERelationshipOwnerKind::Node;
    int32 OwnerIndex = INDEX_NONE;
    FString StableKind;
    FString StableId;
    TArray<FRelationshipPathSegment> Path;
    TArray<FString> Notes;

    bool IsOwnedBy(const ERelationshipOwnerKind Kind, const int32 Index) const
    {
        return OwnerKind == Kind && OwnerIndex == Index;
    }

    bool HasOwner() const
    {
        return OwnerIndex != INDEX_NONE && !StableKind.IsEmpty() && !StableId.IsEmpty();
    }

    TSharedPtr<FJsonObject> Ref() const
    {
        TSharedPtr<FJsonObject> Stable = Value::StableObject(StableKind, StableId);
        if (Path.IsEmpty())
        {
            return Stable;
        }

        TSharedPtr<FJsonObject> Member = MakeShared<FJsonObject>();
        Member->SetStringField(TEXT("kind"), TEXT("member"));
        Member->SetObjectField(TEXT("object"), Stable);
        TArray<TSharedPtr<FJsonValue>> Segments;
        Segments.Reserve(Path.Num());
        for (const FRelationshipPathSegment& Segment : Path)
        {
            Segments.Add(Segment.Index != INDEX_NONE
                ? Value::Number(Segment.Index)
                : Value::String(Segment.Name));
        }
        Member->SetArrayField(TEXT("path"), Segments);
        return Member;
    }
};

struct FStateTreeRelationship
{
    FRelationshipEndpoint From;
    FRelationshipEndpoint To;
    bool bAutomaticContext = false;
    TArray<FString> Notes;
};

struct FRelationshipOwnerRef
{
    ERelationshipOwnerKind Kind = ERelationshipOwnerKind::Node;
    int32 Index = INDEX_NONE;

    bool IsValid() const
    {
        return Index != INDEX_NONE;
    }

    bool operator==(const FRelationshipOwnerRef& Other) const
    {
        return Kind == Other.Kind && Index == Other.Index;
    }

    friend uint32 GetTypeHash(const FRelationshipOwnerRef& Ref)
    {
        return HashCombine(
            GetTypeHash(static_cast<uint8>(Ref.Kind)),
            GetTypeHash(Ref.Index));
    }
};

struct FRelationshipIssue
{
    FString Code = TEXT("validation.invalid_target");
    FString Message;
    FString Ref;
    TArray<FRelationshipOwnerRef> Owners;
    bool bGlobal = false;
    bool bAssetScope = false;
    bool bInfo = false;
};

enum class ENativeEndpointOwnerKind : uint8
{
    NodeInstance,
    NodeStruct,
    ParameterContainer,
    Context,
    StateEvent,
    TransitionEvent,
    Transition
};

struct FNativeEndpointOwner
{
    ENativeEndpointOwnerKind Kind = ENativeEndpointOwnerKind::NodeInstance;
    int32 EntryIndex = INDEX_NONE;
    const FInstancedPropertyBag* ParameterBag = nullptr;
};

class FStateTreeRelationshipIndex
{
public:
    FStateTreeRelationshipIndex(
        const UStateTreeEditorData& InEditorData,
        const FAuthoredStateIndex& InStateIndex,
        FReadContext& InContext)
        : EditorData(InEditorData)
        , StateIndex(InStateIndex)
        , Context(InContext)
    {
        BuildNativeOwners();
        if (bBuildComplete)
        {
            BuildExplicitRelationships();
        }
        if (bBuildComplete)
        {
            BuildAutomaticContextRelationships();
        }
    }

    void EmitForOwner(
        FSalObjectBuilder& Builder,
        const ERelationshipOwnerKind OwnerKind,
        const int32 OwnerIndex) const
    {
        for (const FStateTreeRelationship& Relationship : Relationships)
        {
            if (!Relationship.From.IsOwnedBy(OwnerKind, OwnerIndex)
                && !Relationship.To.IsOwnedBy(OwnerKind, OwnerIndex))
            {
                continue;
            }
            if (!Context.ConsumeEmittedRelationship())
            {
                break;
            }
            if (Relationship.bAutomaticContext)
            {
                Builder.AddComment(TEXT("automatic Context: derived by UStateTreeEditorData::FindContextData; no authored Binding record"));
            }
            for (const FString& Note : Relationship.Notes)
            {
                Builder.AddComment(Note);
            }
            for (const FString& Note : Relationship.From.Notes)
            {
                Builder.AddComment(Note);
            }
            for (const FString& Note : Relationship.To.Notes)
            {
                if (!Relationship.From.Notes.Contains(Note))
                {
                    Builder.AddComment(Note);
                }
            }
            Builder.AddEdge(Relationship.From.Ref(), Relationship.To.Ref());
        }
        for (const FRelationshipIssue& Issue : Issues)
        {
            if (!Issue.bInfo && IsIssueRelevant(Issue, OwnerKind, OwnerIndex))
            {
                Builder.AddComment(Issue.Message);
            }
        }
    }

    void AddDiagnosticsForOwner(
        TArray<TSharedPtr<FJsonObject>>& OutDiagnostics,
        const ERelationshipOwnerKind OwnerKind,
        const int32 OwnerIndex) const
    {
        for (const FRelationshipIssue& Issue : Issues)
        {
            if (!IsIssueRelevant(Issue, OwnerKind, OwnerIndex))
            {
                continue;
            }
            FSalDiagnosticBuilder Diagnostic = Issue.bInfo
                ? FSalDiagnostics::Info(Issue.Code, Issue.Message)
                : FSalDiagnostics::Warning(Issue.Code, Issue.Message);
            Diagnostic.Interface(InterfaceName);
            if (!Issue.Ref.IsEmpty())
            {
                Diagnostic.Ref(Issue.Ref);
            }
            Diagnostic.Suggestion(
                Issue.Code == TEXT("validation.reference_scan_incomplete")
                    ? TEXT("Reduce the authored StateTree size or inspect the asset in the StateTree Editor; Query emitted no partial Binding relationship set.")
                    : TEXT("Inspect or repair the authored StateTree Binding in the StateTree Editor; Query did not modify it."));
            OutDiagnostics.Add(Diagnostic.Build());
        }
    }

    void AddDiagnosticsForAsset(
        FSalObjectBuilder& Builder,
        TArray<TSharedPtr<FJsonObject>>& OutDiagnostics) const
    {
        for (const FRelationshipIssue& Issue : Issues)
        {
            if (!Issue.bGlobal && !Issue.bAssetScope && !Issue.Owners.IsEmpty())
            {
                continue;
            }
            Builder.AddComment(Issue.Message);
            FSalDiagnosticBuilder Diagnostic = Issue.bInfo
                ? FSalDiagnostics::Info(Issue.Code, Issue.Message)
                : FSalDiagnostics::Warning(Issue.Code, Issue.Message);
            Diagnostic.Interface(InterfaceName);
            if (!Issue.Ref.IsEmpty())
            {
                Diagnostic.Ref(Issue.Ref);
            }
            Diagnostic.Suggestion(
                Issue.Code == TEXT("validation.reference_scan_incomplete")
                    ? TEXT("Reduce the authored StateTree size or inspect the asset in the StateTree Editor; Query emitted no partial Binding relationship set.")
                    : TEXT("Inspect or repair the authored StateTree Binding in the StateTree Editor; Query did not modify it."));
            OutDiagnostics.Add(Diagnostic.Build());
        }
    }

    const TArray<FStateTreeRelationship>& GetRelationships() const
    {
        return Relationships;
    }

    bool IsComplete() const
    {
        return bBuildComplete;
    }

    int32 GetAutomaticContextRelationshipCount() const
    {
        int32 Count = 0;
        for (const FStateTreeRelationship& Relationship : Relationships)
        {
            Count += Relationship.bAutomaticContext ? 1 : 0;
        }
        return Count;
    }

private:
    static bool IsIssueRelevant(
        const FRelationshipIssue& Issue,
        const ERelationshipOwnerKind OwnerKind,
        const int32 OwnerIndex)
    {
        const FRelationshipOwnerRef Owner = {OwnerKind, OwnerIndex};
        return Issue.bGlobal
            || Issue.Owners.Contains(Owner);
    }

    static FRelationshipOwnerRef NativeOwnerRef(const FNativeEndpointOwner& NativeOwner)
    {
        FRelationshipOwnerRef Ref;
        Ref.Index = NativeOwner.EntryIndex;
        switch (NativeOwner.Kind)
        {
        case ENativeEndpointOwnerKind::NodeInstance:
        case ENativeEndpointOwnerKind::NodeStruct:
            Ref.Kind = ERelationshipOwnerKind::Node;
            break;
        case ENativeEndpointOwnerKind::ParameterContainer:
            Ref.Kind = ERelationshipOwnerKind::Parameter;
            break;
        case ENativeEndpointOwnerKind::Context:
            Ref.Kind = ERelationshipOwnerKind::Context;
            break;
        case ENativeEndpointOwnerKind::StateEvent:
            Ref.Kind = ERelationshipOwnerKind::State;
            break;
        case ENativeEndpointOwnerKind::TransitionEvent:
        case ENativeEndpointOwnerKind::Transition:
            Ref.Kind = ERelationshipOwnerKind::Transition;
            break;
        }
        return Ref;
    }

    static void AppendUniqueOwnerRefs(
        TArray<FRelationshipOwnerRef>& OutOwners,
        const TArray<FRelationshipOwnerRef>& Owners)
    {
        TSet<FRelationshipOwnerRef> SeenOwners;
        for (const FRelationshipOwnerRef& Owner : OutOwners)
        {
            if (Owner.IsValid())
            {
                SeenOwners.Add(Owner);
            }
        }
        for (const FRelationshipOwnerRef& Owner : Owners)
        {
            if (Owner.IsValid() && !SeenOwners.Contains(Owner))
            {
                SeenOwners.Add(Owner);
                OutOwners.Add(Owner);
            }
        }
    }

    void AddRelationshipIssue(
        const FString& Message,
        const FString& Ref,
        const TArray<FRelationshipOwnerRef>& Owners,
        const bool bGlobal = false,
        const bool bInfo = false,
        const bool bAssetScope = false,
        const FString& Code = TEXT("validation.invalid_target"))
    {
        if (!bBuildComplete && Code != TEXT("validation.reference_scan_incomplete"))
        {
            return;
        }
        FRelationshipIssue& Issue = Issues.AddDefaulted_GetRef();
        Issue.Code = Code;
        Issue.Message = Message;
        Issue.Ref = Ref;
        Issue.bGlobal = bGlobal;
        Issue.bAssetScope = bAssetScope;
        Issue.bInfo = bInfo;
        TSet<FRelationshipOwnerRef> SeenOwners;
        for (const FRelationshipOwnerRef& Owner : Owners)
        {
            if (Owner.IsValid() && !SeenOwners.Contains(Owner))
            {
                SeenOwners.Add(Owner);
                Issue.Owners.Add(Owner);
            }
        }
    }

    void MarkBuildIncomplete()
    {
        if (!bBuildComplete)
        {
            return;
        }
        bBuildComplete = false;
        NativeOwners.Reset();
        ExplicitTargetRoots.Reset();
        Relationships.Reset();
        Issues.Reset();
        AddRelationshipIssue(
            FString::Printf(
                TEXT("StateTree Binding relationship analysis exceeded the hard limit of %d bounded analysis units; no partial relationship set was emitted."),
                MaxRelationshipAnalysisObjects),
            TEXT("EditorBindings"),
            {},
            true,
            false,
            false,
            TEXT("validation.reference_scan_incomplete"));
    }

    bool ConsumeRelationshipAnalysis(const int32 Units = 1)
    {
        if (!bBuildComplete
            || Units < 0
            || RelationshipAnalysisObjects > MaxRelationshipAnalysisObjects - Units)
        {
            MarkBuildIncomplete();
            return false;
        }
        RelationshipAnalysisObjects += Units;
        return true;
    }

    void AddNativeOwner(
        const FGuid& NativeId,
        const ENativeEndpointOwnerKind Kind,
        const int32 EntryIndex,
        const FInstancedPropertyBag* ParameterBag = nullptr)
    {
        NativeOwners.FindOrAdd(NativeId).Add({Kind, EntryIndex, ParameterBag});
    }

    void BuildNativeOwners()
    {
        for (int32 EntryIndex = 0; EntryIndex < StateIndex.GetNodeEntries().Num(); ++EntryIndex)
        {
            if (!ConsumeRelationshipAnalysis())
            {
                return;
            }
            const FAuthoredNodeEntry* Entry = StateIndex.GetNodeEntry(EntryIndex);
            if (Entry == nullptr || Entry->Node == nullptr)
            {
                continue;
            }
            AddNativeOwner(Entry->Node->ID, ENativeEndpointOwnerKind::NodeInstance, EntryIndex);
            AddNativeOwner(Entry->Node->GetNodeID(), ENativeEndpointOwnerKind::NodeStruct, EntryIndex);
        }

        TSet<const FInstancedPropertyBag*> SeenParameterBags;
        for (int32 EntryIndex = 0; EntryIndex < StateIndex.GetParameterEntries().Num(); ++EntryIndex)
        {
            if (!ConsumeRelationshipAnalysis())
            {
                return;
            }
            const FAuthoredParameterEntry* Entry = StateIndex.GetParameterEntry(EntryIndex);
            if (Entry == nullptr || Entry->Bag == nullptr || SeenParameterBags.Contains(Entry->Bag))
            {
                continue;
            }
            SeenParameterBags.Add(Entry->Bag);
            AddNativeOwner(
                Entry->ContainerId,
                ENativeEndpointOwnerKind::ParameterContainer,
                EntryIndex,
                Entry->Bag);
        }

        for (int32 EntryIndex = 0; EntryIndex < StateIndex.GetContextEntries().Num(); ++EntryIndex)
        {
            if (!ConsumeRelationshipAnalysis())
            {
                return;
            }
            const FAuthoredContextEntry* Entry = StateIndex.GetContextEntry(EntryIndex);
            if (Entry != nullptr && Entry->Desc != nullptr)
            {
#if WITH_EDITORONLY_DATA
                AddNativeOwner(Entry->Desc->ID, ENativeEndpointOwnerKind::Context, EntryIndex);
#endif
            }
        }

        for (int32 EntryIndex = 0; EntryIndex < StateIndex.GetEntries().Num(); ++EntryIndex)
        {
            if (!ConsumeRelationshipAnalysis())
            {
                return;
            }
            const FAuthoredStateEntry* Entry = StateIndex.GetEntry(EntryIndex);
            if (Entry != nullptr && Entry->State != nullptr)
            {
                AddNativeOwner(Entry->State->GetEventID(), ENativeEndpointOwnerKind::StateEvent, EntryIndex);
            }
        }

        for (int32 EntryIndex = 0; EntryIndex < StateIndex.GetTransitionEntries().Num(); ++EntryIndex)
        {
            if (!ConsumeRelationshipAnalysis())
            {
                return;
            }
            const FAuthoredTransitionEntry* Entry = StateIndex.GetTransitionEntry(EntryIndex);
            if (Entry == nullptr || Entry->Transition == nullptr)
            {
                continue;
            }
            AddNativeOwner(Entry->Transition->ID, ENativeEndpointOwnerKind::Transition, EntryIndex);
            AddNativeOwner(Entry->Transition->GetEventID(), ENativeEndpointOwnerKind::TransitionEvent, EntryIndex);
        }
    }

    static FString BindingRef(const int32 BindingIndex)
    {
        return FString::Printf(TEXT("EditorBindings[%d]"), BindingIndex);
    }

    bool ResolvePathSegments(
        const FPropertyBindingPath& NativePath,
        const UStruct* BaseStruct,
        TArray<FString>& OutNames,
        const FProperty*& OutRootProperty,
        FString& OutError) const
    {
        OutNames.Reset();
        OutRootProperty = nullptr;
        if (BaseStruct == nullptr)
        {
            OutError = TEXT("native endpoint has no current Struct");
            return false;
        }
        if (NativePath.GetSegments().IsEmpty())
        {
            return true;
        }

        TArray<FPropertyBindingPathIndirection, TInlineAllocator<16>> Indirections;
        if (!NativePath.ResolveIndirections(BaseStruct, Indirections, &OutError, true)
            || Indirections.IsEmpty())
        {
            if (OutError.IsEmpty())
            {
                OutError = TEXT("native member path could not be resolved");
            }
            return false;
        }

        OutNames.SetNum(NativePath.GetSegments().Num());
        TArray<bool> bResolved;
        bResolved.Init(false, NativePath.GetSegments().Num());
        for (const FPropertyBindingPathIndirection& Indirection : Indirections)
        {
            const int32 SegmentIndex = Indirection.GetPathSegmentIndex();
            const FProperty* Property = Indirection.GetProperty();
            if (!OutNames.IsValidIndex(SegmentIndex) || Property == nullptr || bResolved[SegmentIndex])
            {
                continue;
            }
            OutNames[SegmentIndex] = Property->GetName();
            bResolved[SegmentIndex] = true;
            if (SegmentIndex == 0)
            {
                OutRootProperty = Property;
            }
        }
        for (int32 SegmentIndex = 0; SegmentIndex < bResolved.Num(); ++SegmentIndex)
        {
            if (!bResolved[SegmentIndex]
                || !FSalObjectBuilder::IsIdentifier(OutNames[SegmentIndex]))
            {
                OutError = FString::Printf(
                    TEXT("native path segment %d cannot be represented as one exact SAL member"),
                    SegmentIndex);
                return false;
            }
            const int32 ArrayIndex = NativePath.GetSegments()[SegmentIndex].GetArrayIndex();
            if (ArrayIndex < INDEX_NONE)
            {
                OutError = FString::Printf(TEXT("native path segment %d has an invalid array index"), SegmentIndex);
                return false;
            }
        }
        return true;
    }

    static void AppendNativeSegments(
        const FPropertyBindingPath& NativePath,
        const TArray<FString>& Names,
        const int32 FirstSegment,
        TArray<FRelationshipPathSegment>& OutPath)
    {
        for (int32 SegmentIndex = FirstSegment; SegmentIndex < NativePath.GetSegments().Num(); ++SegmentIndex)
        {
            OutPath.Add(FRelationshipPathSegment::Field(Names[SegmentIndex]));
            const int32 ArrayIndex = NativePath.GetSegments()[SegmentIndex].GetArrayIndex();
            if (ArrayIndex != INDEX_NONE)
            {
                OutPath.Add(FRelationshipPathSegment::ArrayIndex(ArrayIndex));
            }
        }
    }

    bool ResolveParameterEndpoint(
        const FNativeEndpointOwner& NativeOwner,
        const FPropertyBindingPath& NativePath,
        const TArray<FString>& ResolvedNames,
        const FProperty* RootProperty,
        FRelationshipEndpoint& OutEndpoint,
        FString& OutError)
    {
        if (NativeOwner.ParameterBag == nullptr
            || NativePath.GetSegments().IsEmpty()
            || RootProperty == nullptr)
        {
            OutError = TEXT("Parameter Binding path has no resolvable descriptor member");
            return false;
        }

        TArray<int32> Matches;
        for (int32 EntryIndex = 0; EntryIndex < StateIndex.GetParameterEntries().Num(); ++EntryIndex)
        {
            if (!ConsumeRelationshipAnalysis())
            {
                OutError = TEXT("relationship analysis budget was exhausted");
                return false;
            }
            const FAuthoredParameterEntry* Entry = StateIndex.GetParameterEntry(EntryIndex);
            if (Entry != nullptr
                && Entry->Bag == NativeOwner.ParameterBag
                && Entry->Desc != nullptr
                && (Entry->Desc->CachedProperty == RootProperty
                    || Entry->Desc->Name == RootProperty->GetFName()))
            {
                Matches.Add(EntryIndex);
            }
        }
        if (Matches.Num() != 1)
        {
            OutError = Matches.IsEmpty()
                ? TEXT("Parameter Binding root property is not a current descriptor")
                : TEXT("Parameter Binding root property resolves to multiple descriptors");
            return false;
        }

        const FAuthoredParameterEntry* Entry = StateIndex.GetParameterEntry(Matches[0]);
        if (Entry == nullptr || Entry->Desc == nullptr || !StateIndex.IsCanonicalParameter(*Entry))
        {
            OutError = TEXT("Parameter Binding endpoint has no canonical container/property identity");
            return false;
        }

        OutEndpoint.OwnerKind = ERelationshipOwnerKind::Parameter;
        OutEndpoint.OwnerIndex = Matches[0];
        OutEndpoint.StableKind = TEXT("parameter");
        OutEndpoint.StableId = ParameterIdentityText(Entry->ContainerId, Entry->Desc->ID);
        const int32 DescriptorArrayIndex = NativePath.GetSegments()[0].GetArrayIndex();
        if (DescriptorArrayIndex != INDEX_NONE)
        {
            OutEndpoint.Path.Add(FRelationshipPathSegment::ArrayIndex(DescriptorArrayIndex));
        }
        AppendNativeSegments(NativePath, ResolvedNames, 1, OutEndpoint.Path);
        return true;
    }

    bool TryResolveParameterOwnerFromFirstSegment(
        const FNativeEndpointOwner& NativeOwner,
        const FPropertyBindingPath& NativePath,
        FRelationshipOwnerRef& OutOwner)
    {
        OutOwner = {};
        OutOwner.Index = INDEX_NONE;
        if (NativeOwner.ParameterBag == nullptr || NativePath.GetSegments().IsEmpty())
        {
            return false;
        }

        const FPropertyBindingPathSegment& RootSegment = NativePath.GetSegments()[0];
#if WITH_EDITORONLY_DATA
        const FGuid PropertyGuid = RootSegment.GetPropertyGuid();
#else
        const FGuid PropertyGuid;
#endif
        TArray<int32, TInlineAllocator<2>> Matches;
        for (int32 EntryIndex = 0; EntryIndex < StateIndex.GetParameterEntries().Num(); ++EntryIndex)
        {
            if (!ConsumeRelationshipAnalysis())
            {
                return false;
            }
            const FAuthoredParameterEntry* Entry = StateIndex.GetParameterEntry(EntryIndex);
            if (Entry == nullptr || Entry->Bag != NativeOwner.ParameterBag || Entry->Desc == nullptr)
            {
                continue;
            }
            const bool bMatches = PropertyGuid.IsValid()
                ? Entry->Desc->ID == PropertyGuid
                : Entry->Desc->Name == RootSegment.GetName();
            if (bMatches)
            {
                Matches.Add(EntryIndex);
            }
        }
        if (Matches.Num() != 1)
        {
            return false;
        }
        OutOwner.Kind = ERelationshipOwnerKind::Parameter;
        OutOwner.Index = Matches[0];
        return true;
    }

    bool ResolveEndpoint(
        const FPropertyBindingPath& NativePath,
        FRelationshipEndpoint& OutEndpoint,
        TArray<FRelationshipOwnerRef>& OutRelatedOwners,
        FString& OutError,
        EStateTreePropertyUsage* OutRootUsage = nullptr,
        const FProperty** OutResolvedRootProperty = nullptr,
        bool* OutForceAssetScopeOnFailure = nullptr)
    {
        OutEndpoint = {};
        OutRelatedOwners.Reset();
        OutError.Reset();
        if (OutRootUsage != nullptr)
        {
            *OutRootUsage = EStateTreePropertyUsage::Invalid;
        }
        if (OutResolvedRootProperty != nullptr)
        {
            *OutResolvedRootProperty = nullptr;
        }
        if (OutForceAssetScopeOnFailure != nullptr)
        {
            *OutForceAssetScopeOnFailure = false;
        }
#if !WITH_EDITORONLY_DATA
        OutError = TEXT("Binding endpoints require editor-only native Struct ids");
        return false;
#else
        if (!ConsumeRelationshipAnalysis(NativePath.GetSegments().Num()))
        {
            OutError = TEXT("relationship analysis budget was exhausted");
            return false;
        }
        const FGuid NativeId = NativePath.GetStructID();
        const TArray<FNativeEndpointOwner>* Owners = NativeOwners.Find(NativeId);
        if (Owners != nullptr)
        {
            TSet<FRelationshipOwnerRef> SeenOwners;
            bool bSawParameterOwner = false;
            for (const FNativeEndpointOwner& Owner : *Owners)
            {
                if (!ConsumeRelationshipAnalysis())
                {
                    OutError = TEXT("relationship analysis budget was exhausted");
                    return false;
                }
                FRelationshipOwnerRef OwnerRef;
                bool bHasPreciseOwner = true;
                if (Owner.Kind == ENativeEndpointOwnerKind::ParameterContainer)
                {
                    bSawParameterOwner = true;
                    bHasPreciseOwner = TryResolveParameterOwnerFromFirstSegment(
                        Owner,
                        NativePath,
                        OwnerRef);
                    if (!bBuildComplete)
                    {
                        OutError = TEXT("relationship analysis budget was exhausted");
                        return false;
                    }
                }
                else
                {
                    OwnerRef = NativeOwnerRef(Owner);
                }
                if (bHasPreciseOwner
                    && OwnerRef.IsValid()
                    && !SeenOwners.Contains(OwnerRef))
                {
                    SeenOwners.Add(OwnerRef);
                    OutRelatedOwners.Add(OwnerRef);
                }
            }
            if (bSawParameterOwner
                && OutRelatedOwners.IsEmpty()
                && OutForceAssetScopeOnFailure != nullptr)
            {
                *OutForceAssetScopeOnFailure = true;
            }
        }
        if (!NativeId.IsValid() || Owners == nullptr || Owners->Num() != 1)
        {
            const FString Reason = !NativeId.IsValid()
                ? TEXT("invalid native Struct id")
                : Owners == nullptr
                    ? TEXT("unknown native Struct id")
                    : TEXT("native Struct id collides across authored endpoint owners");
            OutError = FString::Printf(
                TEXT("endpoint %s (%s) cannot be mapped to one stable SAL owner: %s"),
                *NativePath.ToString(),
                *GuidText(NativeId),
                *Reason);
            return false;
        }

        const FNativeEndpointOwner& NativeOwner = (*Owners)[0];
        const UStruct* BaseStruct = nullptr;
        TArray<FRelationshipPathSegment> Prefix;
        switch (NativeOwner.Kind)
        {
        case ENativeEndpointOwnerKind::NodeInstance:
        case ENativeEndpointOwnerKind::NodeStruct:
        {
            const FAuthoredNodeEntry* Entry = StateIndex.GetNodeEntry(NativeOwner.EntryIndex);
            if (Entry == nullptr || Entry->Node == nullptr || !StateIndex.IsCanonicalNode(*Entry->Node))
            {
                OutError = TEXT("Node endpoint has no canonical node@id owner");
                return false;
            }
            OutEndpoint.OwnerKind = ERelationshipOwnerKind::Node;
            OutEndpoint.OwnerIndex = NativeOwner.EntryIndex;
            OutEndpoint.StableKind = TEXT("node");
            OutEndpoint.StableId = GuidText(Entry->Node->ID);
            if (NativeOwner.Kind == ENativeEndpointOwnerKind::NodeInstance)
            {
                Prefix.Add(FRelationshipPathSegment::Field(TEXT("Instance")));
                BaseStruct = Entry->Node->GetInstance().GetStruct();
            }
            else
            {
                Prefix.Add(FRelationshipPathSegment::Field(TEXT("Node")));
                BaseStruct = Entry->Node->GetNode().GetScriptStruct();
            }
            break;
        }
        case ENativeEndpointOwnerKind::ParameterContainer:
            BaseStruct = NativeOwner.ParameterBag != nullptr
                ? NativeOwner.ParameterBag->GetPropertyBagStruct()
                : nullptr;
            break;
        case ENativeEndpointOwnerKind::Context:
        {
            const FAuthoredContextEntry* Entry = StateIndex.GetContextEntry(NativeOwner.EntryIndex);
            if (Entry == nullptr || Entry->Desc == nullptr || !StateIndex.IsCanonicalContext(*Entry))
            {
                OutError = TEXT("Context endpoint has no canonical object@id owner");
                return false;
            }
            OutEndpoint.OwnerKind = ERelationshipOwnerKind::Context;
            OutEndpoint.OwnerIndex = NativeOwner.EntryIndex;
            OutEndpoint.StableKind = TEXT("object");
            OutEndpoint.StableId = GuidText(Entry->Desc->ID);
            BaseStruct = Entry->Desc->Struct;
            break;
        }
        case ENativeEndpointOwnerKind::StateEvent:
        {
            const FAuthoredStateEntry* Entry = StateIndex.GetEntry(NativeOwner.EntryIndex);
            if (Entry == nullptr || Entry->State == nullptr || !StateIndex.IsCanonicalState(*Entry->State))
            {
                OutError = TEXT("State Event endpoint has no canonical state@id owner");
                return false;
            }
            OutEndpoint.OwnerKind = ERelationshipOwnerKind::State;
            OutEndpoint.OwnerIndex = NativeOwner.EntryIndex;
            OutEndpoint.StableKind = TEXT("state");
            OutEndpoint.StableId = GuidText(Entry->State->ID);
            Prefix.Add(FRelationshipPathSegment::Field(TEXT("RequiredEventToEnter")));
            BaseStruct = FStateTreeEvent::StaticStruct();
            if (!Entry->State->bHasRequiredEventToEnter)
            {
                OutEndpoint.Notes.Add(TEXT("inactive: State.bHasRequiredEventToEnter is false"));
            }
            else if (!Entry->State->RequiredEventToEnter.IsValid())
            {
                OutEndpoint.Notes.Add(TEXT("invalid: State RequiredEventToEnter has neither Tag nor PayloadStruct"));
            }
            break;
        }
        case ENativeEndpointOwnerKind::TransitionEvent:
        {
            const FAuthoredTransitionEntry* Entry = StateIndex.GetTransitionEntry(NativeOwner.EntryIndex);
            if (Entry == nullptr || Entry->Transition == nullptr || !StateIndex.IsCanonicalTransition(*Entry->Transition))
            {
                OutError = TEXT("Transition Event endpoint has no canonical transition@id owner");
                return false;
            }
            OutEndpoint.OwnerKind = ERelationshipOwnerKind::Transition;
            OutEndpoint.OwnerIndex = NativeOwner.EntryIndex;
            OutEndpoint.StableKind = TEXT("transition");
            OutEndpoint.StableId = GuidText(Entry->Transition->ID);
            Prefix.Add(FRelationshipPathSegment::Field(TEXT("RequiredEvent")));
            BaseStruct = FStateTreeEvent::StaticStruct();
            if (Entry->Transition->Trigger != EStateTreeTransitionTrigger::OnEvent)
            {
                OutEndpoint.Notes.Add(TEXT("inactive: Transition.Trigger is not OnEvent"));
            }
            else if (!Entry->Transition->RequiredEvent.IsValid())
            {
                OutEndpoint.Notes.Add(TEXT("invalid: Transition RequiredEvent has neither Tag nor PayloadStruct"));
            }
            break;
        }
        case ENativeEndpointOwnerKind::Transition:
        {
            const FAuthoredTransitionEntry* Entry = StateIndex.GetTransitionEntry(NativeOwner.EntryIndex);
            if (Entry == nullptr || Entry->Transition == nullptr || !StateIndex.IsCanonicalTransition(*Entry->Transition))
            {
                OutError = TEXT("Transition endpoint has no canonical transition@id owner");
                return false;
            }
            OutEndpoint.OwnerKind = ERelationshipOwnerKind::Transition;
            OutEndpoint.OwnerIndex = NativeOwner.EntryIndex;
            OutEndpoint.StableKind = TEXT("transition");
            OutEndpoint.StableId = GuidText(Entry->Transition->ID);
            BaseStruct = FStateTreeTransition::StaticStruct();
            if (!NativePath.GetSegments().IsEmpty()
                && NativePath.GetSegments()[0].GetName()
                    == GET_MEMBER_NAME_CHECKED(FStateTreeTransition, DelegateListener)
                && Entry->Transition->Trigger != EStateTreeTransitionTrigger::OnDelegate)
            {
                OutEndpoint.Notes.Add(TEXT("inactive: Transition.Trigger is not OnDelegate"));
            }
            break;
        }
        }

        const FProperty* RootProperty = nullptr;
        if (!NativePath.GetSegments().IsEmpty() && BaseStruct != nullptr)
        {
            const FPropertyBindingPathSegment& RootSegment = NativePath.GetSegments()[0];
            const FPropertyBindingPath RootPath(
                NativeId,
                MakeArrayView(&RootSegment, 1));
            TArray<FPropertyBindingPathIndirection, TInlineAllocator<16>> RootIndirections;
            if (RootPath.ResolveIndirections(BaseStruct, RootIndirections, nullptr, true)
                && !RootIndirections.IsEmpty())
            {
                RootProperty = RootIndirections[0].GetProperty();
                if (OutRootUsage != nullptr && RootProperty != nullptr)
                {
                    *OutRootUsage = UE::StateTree::GetUsageFromMetaData(RootProperty);
                }
                if (OutResolvedRootProperty != nullptr)
                {
                    *OutResolvedRootProperty = RootProperty;
                }
            }
        }
        TArray<FString> ResolvedNames;
        const FProperty* FullyResolvedRootProperty = nullptr;
        FString Error;
        if (!ResolvePathSegments(
                NativePath,
                BaseStruct,
                ResolvedNames,
                FullyResolvedRootProperty,
                Error))
        {
            OutError = FString::Printf(
                TEXT("endpoint %s cannot be represented exactly: %s"),
                *NativePath.ToString(),
                *Error);
            return false;
        }
        if (FullyResolvedRootProperty != nullptr)
        {
            RootProperty = FullyResolvedRootProperty;
            if (OutRootUsage != nullptr)
            {
                *OutRootUsage = UE::StateTree::GetUsageFromMetaData(RootProperty);
            }
            if (OutResolvedRootProperty != nullptr)
            {
                *OutResolvedRootProperty = RootProperty;
            }
        }
        if (NativeOwner.Kind == ENativeEndpointOwnerKind::ParameterContainer)
        {
            if (!ResolveParameterEndpoint(
                    NativeOwner,
                    NativePath,
                    ResolvedNames,
                    RootProperty,
                    OutEndpoint,
                    Error))
            {
                OutError = FString::Printf(
                    TEXT("endpoint %s cannot be represented exactly: %s"),
                    *NativePath.ToString(),
                    *Error);
                return false;
            }
            OutRelatedOwners = {{OutEndpoint.OwnerKind, OutEndpoint.OwnerIndex}};
            return true;
        }

        OutEndpoint.Path = MoveTemp(Prefix);
        AppendNativeSegments(NativePath, ResolvedNames, 0, OutEndpoint.Path);
        OutRelatedOwners = {{OutEndpoint.OwnerKind, OutEndpoint.OwnerIndex}};
        return true;
#endif
    }

    void BuildExplicitRelationships()
    {
        struct FPendingRelationship
        {
            FRelationshipEndpoint Source;
            FRelationshipEndpoint Target;
            FString Ref;
            FNativeBindingPathKey TargetKey;
            bool bOutput = false;
            TArray<FString> Notes;
        };

        const TConstArrayView<FStateTreePropertyPathBinding> Bindings = EditorData.EditorBindings.GetBindings();
        TArray<FPendingRelationship> Pending;
        Pending.Reserve(FMath::Min(Bindings.Num(), MaxRelationshipAnalysisObjects));
        TMap<FNativeBindingPathKey, int32> OrdinaryTargetCounts;
        for (int32 BindingIndex = 0; BindingIndex < Bindings.Num(); ++BindingIndex)
        {
            if (!ConsumeRelationshipAnalysis())
            {
                return;
            }
            const FStateTreePropertyPathBinding& Binding = Bindings[BindingIndex];
            const FString Ref = BindingRef(BindingIndex);

            FRelationshipEndpoint NativeSource;
            FRelationshipEndpoint NativeTarget;
            TArray<FRelationshipOwnerRef> SourceOwners;
            TArray<FRelationshipOwnerRef> TargetOwners;
            FString SourceError;
            FString TargetError;
            EStateTreePropertyUsage TargetRootUsage = EStateTreePropertyUsage::Invalid;
            const FProperty* TargetRootProperty = nullptr;
            bool bSourceForceAssetScope = false;
            bool bTargetForceAssetScope = false;
            const bool bSourceResolved = ResolveEndpoint(
                Binding.GetSourcePath(),
                NativeSource,
                SourceOwners,
                SourceError,
                nullptr,
                nullptr,
                &bSourceForceAssetScope);
            const bool bTargetResolved = ResolveEndpoint(
                Binding.GetTargetPath(),
                NativeTarget,
                TargetOwners,
                TargetError,
                &TargetRootUsage,
                &TargetRootProperty,
                &bTargetForceAssetScope);
            if (!bBuildComplete)
            {
                return;
            }
            TArray<FRelationshipOwnerRef> IncidentOwners = SourceOwners;
            AppendUniqueOwnerRefs(IncidentOwners, TargetOwners);
            if (!Binding.GetTargetPath().IsPathEmpty()
                && TargetRootProperty != nullptr)
            {
                ExplicitTargetRoots.Add(ExplicitTargetRootKey(
                    Binding.GetTargetPath().GetStructID(),
                    *TargetRootProperty));
            }

            if (!bSourceResolved || !bTargetResolved)
            {
                if (!bSourceResolved)
                {
                    const FString Message = FString::Printf(
                        TEXT("StateTree Binding source %s."),
                        *SourceError);
                    AddRelationshipIssue(
                        Message,
                        Ref,
                        IncidentOwners,
                        false,
                        false,
                        bSourceForceAssetScope);
                }
                if (!bTargetResolved)
                {
                    const FString Message = FString::Printf(
                        TEXT("StateTree Binding target %s."),
                        *TargetError);
                    AddRelationshipIssue(
                        Message,
                        Ref,
                        IncidentOwners,
                        false,
                        false,
                        bTargetForceAssetScope);
                }
                continue;
            }

            if (Binding.GetTargetPath().IsPathEmpty())
            {
                AddRelationshipIssue(
                    TEXT("StateTree Binding target path cannot address an entire Struct."),
                    Ref,
                    IncidentOwners);
                continue;
            }

            const bool bEffectiveOutput = TargetRootUsage == EStateTreePropertyUsage::Output;
            TArray<FString> Notes;
            if (bEffectiveOutput != Binding.IsOutputBinding())
            {
                const FString Message = FString::Printf(
                    TEXT("StateTree Binding stored output flag is %s but target root Usage is %s; Query used the target Usage, matching the compiler's read-time direction repair without modifying the asset."),
                    Binding.IsOutputBinding() ? TEXT("true") : TEXT("false"),
                    bEffectiveOutput ? TEXT("Output") : TEXT("non-Output"));
                AddRelationshipIssue(Message, Ref, IncidentOwners, false, true);
                Notes.Add(Message);
            }

            FPendingRelationship& Item = Pending.AddDefaulted_GetRef();
            Item.Source = MoveTemp(NativeSource);
            Item.Target = MoveTemp(NativeTarget);
            Item.Ref = Ref;
            Item.TargetKey = FNativeBindingPathKey(Binding.GetTargetPath());
            Item.bOutput = bEffectiveOutput;
            Item.Notes = MoveTemp(Notes);
            if (!Item.bOutput)
            {
                ++OrdinaryTargetCounts.FindOrAdd(Item.TargetKey);
            }
        }

        for (FPendingRelationship& Item : Pending)
        {
            if (!bBuildComplete)
            {
                return;
            }
            if (!Item.bOutput && OrdinaryTargetCounts.FindRef(Item.TargetKey) > 1)
            {
                TArray<FRelationshipOwnerRef> IncidentOwners = {
                    {Item.Source.OwnerKind, Item.Source.OwnerIndex},
                    {Item.Target.OwnerKind, Item.Target.OwnerIndex}};
                AddRelationshipIssue(
                    TEXT("Multiple ordinary StateTree Bindings have the same native TargetPath; no ambiguous arrow was emitted."),
                    Item.Ref,
                    IncidentOwners);
                continue;
            }
            if (Item.bOutput)
            {
                Relationships.Add({
                    MoveTemp(Item.Target),
                    MoveTemp(Item.Source),
                    false,
                    MoveTemp(Item.Notes)});
            }
            else
            {
                Relationships.Add({
                    MoveTemp(Item.Source),
                    MoveTemp(Item.Target),
                    false,
                    MoveTemp(Item.Notes)});
            }
        }
    }

    static FString ExplicitTargetRootKey(
        const FGuid& StructId,
        const FProperty& Property)
    {
        return FString::Printf(
            TEXT("%s|%s"),
            *GuidText(StructId),
            *Property.GetPathName());
    }

    bool HasExplicitTargetRoot(
        const FGuid& StructId,
        const FProperty& Property) const
    {
        return ExplicitTargetRoots.Contains(ExplicitTargetRootKey(StructId, Property));
    }

    void AddAutomaticContextForSurface(const FGuid& StructId, const UStruct* Struct)
    {
#if WITH_EDITORONLY_DATA
        if (Struct == nullptr)
        {
            return;
        }
        for (TFieldIterator<FProperty> It(Struct); It; ++It)
        {
            if (!ConsumeRelationshipAnalysis())
            {
                return;
            }
            const FProperty* Property = *It;
            if (Property == nullptr
                || UE::StateTree::GetUsageFromMetaData(Property) != EStateTreePropertyUsage::Context
                || HasExplicitTargetRoot(StructId, *Property))
            {
                continue;
            }

            const FString TargetRef = GuidText(StructId) + TEXT(":") + Property->GetName();
            const FPropertyBindingPath TargetPath(StructId, Property->GetFName());
            FRelationshipEndpoint Target;
            TArray<FRelationshipOwnerRef> TargetOwners;
            FString TargetError;
            if (!ResolveEndpoint(TargetPath, Target, TargetOwners, TargetError))
            {
                AddRelationshipIssue(
                    FString::Printf(
                        TEXT("StateTree automatic Context target %s."),
                        *TargetError),
                    TargetRef,
                    TargetOwners);
                continue;
            }

            const UStruct* ContextType = nullptr;
            if (const FStructProperty* StructProperty = CastField<FStructProperty>(Property))
            {
                ContextType = StructProperty->Struct;
            }
            else if (const FObjectPropertyBase* ObjectProperty = CastField<FObjectPropertyBase>(Property))
            {
                ContextType = ObjectProperty->PropertyClass;
            }
            if (ContextType == nullptr)
            {
                AddRelationshipIssue(
                    TEXT("A StateTree Context-usage Property is neither an Object reference nor a Struct and has no automatic Context relationship."),
                    TargetRef,
                    TargetOwners);
                continue;
            }

            const int32 ContextDescriptorCount = EditorData.Schema != nullptr
                ? EditorData.Schema->GetContextDataDescs().Num()
                : 0;
            if (!ConsumeRelationshipAnalysis(ContextDescriptorCount))
            {
                return;
            }
            const FStateTreeBindableStructDesc Desc = EditorData.FindContextData(
                ContextType,
                Property->GetName());
            if (!Desc.IsValid())
            {
                AddRelationshipIssue(
                    FString::Printf(
                        TEXT("No Schema Context Data matches automatic Context target %s."),
                        *TargetRef),
                    TargetRef,
                    TargetOwners);
                continue;
            }

            FRelationshipEndpoint Source;
            TArray<FRelationshipOwnerRef> SourceOwners;
            FString SourceError;
            const FPropertyBindingPath SourcePath(Desc.ID);
            if (!ResolveEndpoint(SourcePath, Source, SourceOwners, SourceError))
            {
                AppendUniqueOwnerRefs(SourceOwners, TargetOwners);
                AddRelationshipIssue(
                    FString::Printf(
                        TEXT("StateTree automatic Context source %s."),
                        *SourceError),
                    TargetRef,
                    SourceOwners);
                continue;
            }
            Relationships.Add({MoveTemp(Source), MoveTemp(Target), true, {}});
        }
#endif
    }

    bool IsNodeStructEligibleForBinding(
        const FStateTreeEditorNode& Node,
        const int32 NodeEntryIndex)
    {
        const TStructView<FStateTreeNodeBase> NodeView = Node.GetNode();
        if (!NodeView.IsValid())
        {
            return false;
        }

        TArray<FStateTreeDataView> Pending;
        Pending.Add(FStateTreeDataView(
            NodeView.GetScriptStruct(),
            const_cast<uint8*>(NodeView.GetMemory())));
        TSet<const UObject*> VisitedObjects;
        int32 VisitedValues = 0;
        while (!Pending.IsEmpty())
        {
            const FStateTreeDataView Data = Pending.Pop(EAllowShrinking::No);
            if (!Data.IsValid())
            {
                continue;
            }
            for (TPropertyValueIterator<FProperty> It(Data.GetStruct(), Data.GetMemory()); It; ++It)
            {
                if (!ConsumeRelationshipAnalysis())
                {
                    return false;
                }
                if (++VisitedValues > MaxNodeEligibilityValues)
                {
                    AddRelationshipIssue(
                        FString::Printf(
                            TEXT("Node-struct Binding eligibility scan exceeded %d reflected values; automatic Context on its Node surface was suppressed."),
                            MaxNodeEligibilityValues),
                        FString::Printf(TEXT("node@%s.Node"), *GuidText(Node.ID)),
                        {{ERelationshipOwnerKind::Node, NodeEntryIndex}});
                    return false;
                }

                const FProperty* Property = It->Key;
                const void* Address = It->Value;
                if (Property == nullptr || Address == nullptr)
                {
                    continue;
                }
                if (const FStructProperty* StructProperty = CastField<FStructProperty>(Property))
                {
                    const UScriptStruct* Struct = StructProperty->Struct;
                    if (Struct != nullptr
                        && (Struct->IsChildOf(FStateTreePropertyRef::StaticStruct())
                            || Struct->IsChildOf(FStateTreeDelegateDispatcher::StaticStruct())
                            || Struct->IsChildOf(FStateTreeDelegateListener::StaticStruct())))
                    {
                        return true;
                    }
                }

                const FObjectPropertyBase* ObjectProperty = CastField<FObjectPropertyBase>(Property);
                const UObject* Object = ObjectProperty != nullptr
                    ? ObjectProperty->GetObjectPropertyValue(Address)
                    : nullptr;
                if (Object == nullptr
                    || !Property->HasAnyPropertyFlags(CPF_InstancedReference)
                    || VisitedObjects.Contains(Object))
                {
                    continue;
                }
                if (VisitedObjects.Num() >= MaxNodeEligibilityObjects)
                {
                    AddRelationshipIssue(
                        FString::Printf(
                            TEXT("Node-struct Binding eligibility scan exceeded %d instanced objects; automatic Context on its Node surface was suppressed."),
                            MaxNodeEligibilityObjects),
                        FString::Printf(TEXT("node@%s.Node"), *GuidText(Node.ID)),
                        {{ERelationshipOwnerKind::Node, NodeEntryIndex}});
                    return false;
                }
                VisitedObjects.Add(Object);
                Pending.Add(FStateTreeDataView(const_cast<UObject*>(Object)));
            }
        }
        return false;
    }

    void BuildAutomaticContextRelationships()
    {
        for (int32 EntryIndex = 0; EntryIndex < StateIndex.GetNodeEntries().Num(); ++EntryIndex)
        {
            const FAuthoredNodeEntry& Entry = StateIndex.GetNodeEntries()[EntryIndex];
            if (Entry.Node == nullptr || !StateIndex.IsCanonicalNode(*Entry.Node))
            {
                continue;
            }
            AddAutomaticContextForSurface(Entry.Node->ID, Entry.Node->GetInstance().GetStruct());
            if (!bBuildComplete)
            {
                return;
            }
            if (!Entry.bPropertyFunction
                && IsNodeStructEligibleForBinding(*Entry.Node, EntryIndex))
            {
                AddAutomaticContextForSurface(
                    Entry.Node->GetNodeID(),
                    Entry.Node->GetNode().GetScriptStruct());
            }
            if (!bBuildComplete)
            {
                return;
            }
        }

        TSet<const FInstancedPropertyBag*> SeenParameterBags;
        for (const FAuthoredParameterEntry& Entry : StateIndex.GetParameterEntries())
        {
            if (Entry.Bag == nullptr
                || Entry.OwnerStateIndex == INDEX_NONE
                || SeenParameterBags.Contains(Entry.Bag))
            {
                continue;
            }
            const FAuthoredStateEntry* StateEntry = StateIndex.GetEntry(Entry.OwnerStateIndex);
            if (StateEntry == nullptr
                || StateEntry->State == nullptr
                || StateEntry->State->Type == EStateTreeStateType::Subtree)
            {
                continue;
            }
            SeenParameterBags.Add(Entry.Bag);
            AddAutomaticContextForSurface(Entry.ContainerId, Entry.Bag->GetPropertyBagStruct());
            if (!bBuildComplete)
            {
                return;
            }
        }
    }

    const UStateTreeEditorData& EditorData;
    const FAuthoredStateIndex& StateIndex;
    FReadContext& Context;
    TMap<FGuid, TArray<FNativeEndpointOwner>> NativeOwners;
    TSet<FString> ExplicitTargetRoots;
    TArray<FStateTreeRelationship> Relationships;
    TArray<FRelationshipIssue> Issues;
    int32 RelationshipAnalysisObjects = 0;
    bool bBuildComplete = true;
};

bool EmitNodeArray(
    FSalObjectBuilder& Builder,
    FReadContext& Context,
    const FAuthoredStateIndex& StateIndex,
    const FString& OwnerAlias,
    const TArray<FString>& OwnerPath,
    const FString& Role,
    const TArray<FStateTreeEditorNode>& Nodes)
{
    FUniqueNameAllocator Names;
    for (int32 Index = 0; Index < Nodes.Num(); ++Index)
    {
        if (!Context.ConsumeEmittedObject())
        {
            return false;
        }
        const FStateTreeEditorNode& Node = Nodes[Index];
        const FString Preferred = Node.GetName().IsNone()
            ? FString::Printf(TEXT("node_%d"), Index + 1)
            : Node.GetName().ToString();
        TArray<FString> Path = OwnerPath;
        Path.Add(Role);
        Path.Add(Names.Allocate(Preferred));
        if (!Context.ConsumeMemberPath(Path.Num()))
        {
            return false;
        }
        Builder.AddMemberBinding(OwnerAlias, Path, NodeValue(Node, StateIndex));
    }
    return true;
}

bool EmitTransitionArray(
    FSalObjectBuilder& Builder,
    FReadContext& Context,
    const FAuthoredStateIndex& StateIndex,
    const FString& OwnerAlias,
    const TArray<FString>& OwnerPath,
    const TArray<FStateTreeTransition>& Transitions)
{
    for (int32 Index = 0; Index < Transitions.Num(); ++Index)
    {
        if (!Context.ConsumeEmittedObject())
        {
            return false;
        }
        const FStateTreeTransition& Transition = Transitions[Index];
        TArray<FString> Path = OwnerPath;
        Path.Add(TEXT("Transitions"));
        Path.Add(FString::Printf(TEXT("transition_%d"), Index + 1));
        if (!Context.ConsumeMemberPath(Path.Num()))
        {
            return false;
        }
        Builder.AddMemberBinding(OwnerAlias, Path, TransitionValue(Transition, StateIndex));
        if (!EmitNodeArray(
            Builder,
            Context,
            StateIndex,
            OwnerAlias,
            Path,
            TEXT("Conditions"),
            Transition.Conditions))
        {
            return false;
        }
    }
    return true;
}

bool EmitOwnedStateObjects(
    FSalObjectBuilder& Builder,
    FReadContext& Context,
    const FAuthoredStateIndex& StateIndex,
    const FString& OwnerAlias,
    const TArray<FString>& StatePath,
    const UStateTreeState& State)
{
    if (!EmitNodeArray(
            Builder, Context, StateIndex, OwnerAlias, StatePath, TEXT("EnterConditions"), State.EnterConditions)
        || !EmitNodeArray(
            Builder, Context, StateIndex, OwnerAlias, StatePath, TEXT("Tasks"), State.Tasks))
    {
        return false;
    }
    if (State.SingleTask.Node.IsValid())
    {
        if (!Context.ConsumeEmittedObject())
        {
            return false;
        }
        TArray<FString> Path = StatePath;
        Path.Add(TEXT("SingleTask"));
        if (!Context.ConsumeMemberPath(Path.Num()))
        {
            return false;
        }
        Builder.AddMemberBinding(OwnerAlias, Path, NodeValue(State.SingleTask, StateIndex));
    }
    if (!EmitNodeArray(
            Builder, Context, StateIndex, OwnerAlias, StatePath, TEXT("Considerations"), State.Considerations))
    {
        return false;
    }
    return EmitTransitionArray(Builder, Context, StateIndex, OwnerAlias, StatePath, State.Transitions);
}

bool EmitIndexedStateSubtree(
    FSalObjectBuilder& Builder,
    FReadContext& Context,
    const FAuthoredStateIndex& StateIndex,
    const int32 StartIndex,
    const FString& OwnerAlias,
    const int32 MaxDepth)
{
    struct FPendingEmission
    {
        int32 EntryIndex = INDEX_NONE;
        int32 Depth = 0;
        bool bLocalRoot = false;
        bool bExit = false;
    };

    TArray<FPendingEmission> Stack;
    Stack.Add({StartIndex, 0, true, false});
    TArray<FString> CurrentPath;
    while (!Stack.IsEmpty())
    {
        const FPendingEmission Pending = Stack.Pop(EAllowShrinking::No);
        if (Pending.bExit)
        {
            if (!Pending.bLocalRoot && !CurrentPath.IsEmpty())
            {
                CurrentPath.Pop(EAllowShrinking::No);
            }
            continue;
        }
        const FAuthoredStateEntry* Entry = StateIndex.GetEntry(Pending.EntryIndex);
        if (Entry == nullptr || Entry->State == nullptr)
        {
            continue;
        }
        if (!Pending.bLocalRoot)
        {
            CurrentPath.Add(Entry->MemberName);
        }
        if (!Context.ConsumeEmittedObject())
        {
            return false;
        }
        if (Pending.bLocalRoot)
        {
            Builder.AddLocalBinding(
                OwnerAlias,
                StateValue(*Entry->State, EStateEmission::Tree, StateIndex));
        }
        else
        {
            if (!Context.ConsumeMemberPath(CurrentPath.Num()))
            {
                return false;
            }
            Builder.AddMemberBinding(
                OwnerAlias,
                CurrentPath,
                StateValue(*Entry->State, EStateEmission::Tree, StateIndex));
        }
        if (!EmitOwnedStateObjects(
                Builder,
                Context,
                StateIndex,
                OwnerAlias,
                CurrentPath,
                *Entry->State))
        {
            return false;
        }
        Stack.Add({Pending.EntryIndex, Pending.Depth, Pending.bLocalRoot, true});
        if (Pending.Depth >= MaxDepth)
        {
            if (!Entry->Children.IsEmpty())
            {
                Builder.AddComment(FString::Printf(
                    TEXT("truncated by requested depth: %s has %d indexed child state(s)"),
                    *StateIndex.DescribeState(*Entry->State),
                    Entry->Children.Num()));
            }
            continue;
        }
        for (int32 ChildPosition = Entry->Children.Num() - 1; ChildPosition >= 0; --ChildPosition)
        {
            const int32 ChildIndex = Entry->Children[ChildPosition];
            const FAuthoredStateEntry* Child = StateIndex.GetEntry(ChildIndex);
            if (Child == nullptr)
            {
                continue;
            }
            Stack.Add({ChildIndex, Pending.Depth + 1, false, false});
        }
    }
    return true;
}

int32 PropertyCount(const FInstancedPropertyBag& Bag)
{
    const UPropertyBag* Struct = Bag.GetPropertyBagStruct();
    return Struct != nullptr ? Struct->GetPropertyDescs().Num() : 0;
}

FStateTreeCounts CountAuthored(
    const UStateTreeEditorData& EditorData,
    const FAuthoredStateIndex& Index,
    FReadContext& Context)
{
    FStateTreeCounts Counts;
    Counts.bComplete = Index.IsHierarchyComplete() && Index.IsNodeIdentityComplete();
    Counts.Evaluators = EditorData.Evaluators.Num();
    Counts.GlobalTasks = EditorData.GlobalTasks.Num();
    Counts.Parameters = PropertyCount(EditorData.GetRootParametersPropertyBag());
    Counts.PropertyBindings = EditorData.EditorBindings.GetBindings().Num();
    for (const FAuthoredNodeEntry& NodeEntry : Index.GetNodeEntries())
    {
        Counts.PropertyFunctions += NodeEntry.bPropertyFunction ? 1 : 0;
    }
    if (EditorData.Schema != nullptr)
    {
        Counts.ContextData = EditorData.Schema->GetContextDataDescs().Num();
    }
    for (const FAuthoredStateEntry& Entry : Index.GetEntries())
    {
        if (Entry.State != nullptr)
        {
            if (!Context.ConsumeAnalysisObject())
            {
                Counts.bComplete = false;
                break;
            }
            const UStateTreeState& State = *Entry.State;
            ++Counts.States;
            Counts.Tasks += State.Tasks.Num() + (State.SingleTask.Node.IsValid() ? 1 : 0);
            Counts.Conditions += State.EnterConditions.Num();
            Counts.Considerations += State.Considerations.Num();
            Counts.Transitions += State.Transitions.Num();
            Counts.Parameters += PropertyCount(State.Parameters.Parameters);
            for (const FStateTreeTransition& Transition : State.Transitions)
            {
                if (!Context.ConsumeAnalysisObject())
                {
                    Counts.bComplete = false;
                    break;
                }
                Counts.Conditions += Transition.Conditions.Num();
            }
            if (!Counts.bComplete)
            {
                break;
            }
        }
    }
    return Counts;
}

bool ContainsSearchText(const FString& Value, const FString& SearchText)
{
    return SearchText.IsEmpty() || Value.Contains(SearchText, ESearchCase::IgnoreCase);
}

FString StateTypeText(const UStateTreeState& State)
{
    const UEnum* Enum = StaticEnum<EStateTreeStateType>();
    return Enum != nullptr
        ? Enum->GetNameStringByValue(static_cast<int64>(State.Type))
        : FString::Printf(TEXT("%d"), static_cast<int32>(State.Type));
}

FString StatePathFor(
    FAuthoredStateIndex& Index,
    const int32 StateIndex,
    TMap<int32, FString>& Cache)
{
    if (const FString* Existing = Cache.Find(StateIndex))
    {
        return *Existing;
    }
    FString Path;
    Index.TryGetNativePath(StateIndex, Path);
    Cache.Add(StateIndex, Path);
    return Path;
}

FString NodeRolePath(
    const FAuthoredStateIndex& Index,
    const FAuthoredNodeEntry& Entry)
{
    if (Entry.bPropertyFunction)
    {
        return FString::Printf(TEXT("EditorBindings[%d].PropertyFunction"), Entry.BindingIndex);
    }
    if (Entry.OwnerTransitionIndex != INDEX_NONE)
    {
        const FAuthoredTransitionEntry* Transition = Index.GetTransitionEntry(
            Entry.OwnerTransitionIndex);
        return FString::Printf(
            TEXT("Transitions[%d].Conditions[%d]"),
            Transition != nullptr ? Transition->ArrayIndex : INDEX_NONE,
            Entry.RoleIndex);
    }
    if (Entry.Role == TEXT("SingleTask"))
    {
        return TEXT("SingleTask");
    }
    return FString::Printf(TEXT("%s[%d]"), *Entry.Role, Entry.RoleIndex);
}

FString NodeOwnerText(
    FAuthoredStateIndex& Index,
    const FAuthoredNodeEntry& Entry,
    TMap<int32, FString>& StatePaths)
{
    if (Entry.bPropertyFunction)
    {
        return FString::Printf(
            TEXT("owner: EditorBindings[%d] Property Function\nbinding target: %s\nlifecycle: Binding-owned"),
            Entry.BindingIndex,
            Entry.BindingTargetPath.IsEmpty() ? TEXT("<empty native path>") : *Entry.BindingTargetPath);
    }
    if (Entry.OwnerStateIndex == INDEX_NONE)
    {
        return FString::Printf(
            TEXT("owner: StateTree.%s"),
            *NodeRolePath(Index, Entry));
    }
    const FString StatePath = StatePathFor(Index, Entry.OwnerStateIndex, StatePaths);
    return FString::Printf(
        TEXT("owner state: %s\nrole: %s"),
        StatePath.IsEmpty() ? TEXT("<path unavailable>") : *StatePath,
        *NodeRolePath(Index, Entry));
}

FString TransitionOwnerText(
    FAuthoredStateIndex& Index,
    const FAuthoredTransitionEntry& Entry,
    TMap<int32, FString>& StatePaths)
{
    const FString StatePath = StatePathFor(Index, Entry.OwnerStateIndex, StatePaths);
    return FString::Printf(
        TEXT("owner state: %s\nrole: Transitions[%d]"),
        StatePath.IsEmpty() ? TEXT("<path unavailable>") : *StatePath,
        Entry.ArrayIndex);
}

void AppendFingerprintToken(FString& Out, const FString& Value)
{
    Out += FString::Printf(TEXT("%d:"), Value.Len());
    Out += Value;
    Out += TEXT(";");
}

FString StateTreeCursorFingerprint(
    const FSalResolvedTarget& Target,
    UStateTree* StateTree,
    const FSalQuery& Query,
    const int32 EffectiveLimit)
{
    FString Operation;
    FString SearchText;
    Query.Operation->TryGetStringField(TEXT("kind"), Operation);
    Query.Operation->TryGetStringField(TEXT("text"), SearchText);
    FString OperationText;
    const TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
        TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&OperationText);
    FJsonSerializer::Serialize(Query.Operation.ToSharedRef(), Writer);
    FString Canonical;
    AppendFingerprintToken(Canonical, Target.AssetPath);
    AppendFingerprintToken(Canonical, StateTree != nullptr ? StateTree->GetClass()->GetPathName() : FString());
    AppendFingerprintToken(
        Canonical,
        StateTree != nullptr
            ? LexToString(UStateTreeEditingSubsystem::CalculateStateTreeHash(StateTree))
            : FString());
    AppendFingerprintToken(Canonical, Operation);
    AppendFingerprintToken(Canonical, SearchText);
    AppendFingerprintToken(Canonical, OperationText);
    AppendFingerprintToken(Canonical, LexToString(EffectiveLimit));
    uint8 Digest[FSHA1::DigestSize];
    FSHA1::HashBuffer(*Canonical, Canonical.Len() * sizeof(TCHAR), Digest);
    return BytesToHex(Digest, UE_ARRAY_COUNT(Digest)).ToLower();
}

FString EncodeStateTreeCursor(
    const FSalResolvedTarget& Target,
    UStateTree* StateTree,
    const FSalQuery& Query,
    const int32 EffectiveLimit,
    const int32 Offset)
{
    return TEXT("state_tree1:")
        + StateTreeCursorFingerprint(Target, StateTree, Query, EffectiveLimit)
        + TEXT(":")
        + LexToString(Offset);
}

bool DecodeStateTreePage(
    const FSalResolvedTarget& Target,
    UStateTree* StateTree,
    const FSalQuery& Query,
    FSalPage& OutPage)
{
    OutPage.Offset = 0;
    OutPage.Limit = FMath::Clamp(
        Query.PageLimit > 0 ? Query.PageLimit : DefaultCollectionLimit,
        1,
        MaxCollectionLimit);
    if (Query.PageAfter.IsEmpty())
    {
        return true;
    }
    TArray<FString> Parts;
    Query.PageAfter.ParseIntoArray(Parts, TEXT(":"), false);
    return Parts.Num() == 3
        && Parts[0] == TEXT("state_tree1")
        && Parts[1] == StateTreeCursorFingerprint(Target, StateTree, Query, OutPage.Limit)
        && ParseNonNegativeInt32(Parts[2], OutPage.Offset);
}

TSharedPtr<FJsonObject> MakeStateTreePageResult(
    const TSharedPtr<FJsonObject>& Result,
    const FSalResolvedTarget& Target,
    UStateTree* StateTree,
    const FSalQuery& Query,
    const FSalPage& Page,
    const int32 NextOffset,
    const bool bHasNext)
{
    if (Result.IsValid() && bHasNext)
    {
        TSharedPtr<FJsonObject> PageObject = MakeShared<FJsonObject>();
        PageObject->SetStringField(
            TEXT("next"),
            EncodeStateTreeCursor(Target, StateTree, Query, Page.Limit, NextOffset));
        Result->SetObjectField(TEXT("page"), PageObject);
    }
    return Result;
}

TSharedPtr<FJsonObject> QueryTarget(
    const FSalQuery& Query,
    const FSalResolvedTarget& Target,
    UStateTree* StateTree,
    UStateTreeEditorData* EditorData,
    FAuthoredStateIndex& Index,
    FReadContext& Context)
{
    if (HasUnsupportedExactClauses(Query))
    {
        return QueryError(
            TEXT("capability.clause_unavailable"),
            TEXT("Exact StateTree target read accepts only optional with schema."),
            TEXT("target"));
    }
    if (HasUnsupportedExactDetail(Query))
    {
        return QueryError(
            TEXT("capability.detail_unavailable"),
            TEXT("Exact StateTree target read supports only with schema."),
            TEXT("target"),
            Query.With[0]);
    }
    FSalObjectBuilder Builder;
    Builder.AddLocalBinding(Query.Alias, StateTreeAssetValue(Target, StateTree, EditorData));
    bool bEmissionComplete = true;
    for (const FAuthoredParameterEntry& Entry : Index.GetParameterEntries())
    {
        if (!Entry.bRoot || Entry.Desc == nullptr)
        {
            continue;
        }
        if (!Context.ConsumeEmittedObject())
        {
            bEmissionComplete = false;
            break;
        }
        Builder.AddMemberBinding(
            Query.Alias,
            {TEXT("RootParameters"), Entry.MemberName},
            ParameterValue(Entry, Index, false));
    }
    if (bEmissionComplete)
    {
        bEmissionComplete = EmitNodeArray(
            Builder, Context, Index, Query.Alias, {}, TEXT("Evaluators"), EditorData->Evaluators);
    }
    if (bEmissionComplete)
    {
        EmitNodeArray(
            Builder, Context, Index, Query.Alias, {}, TEXT("GlobalTasks"), EditorData->GlobalTasks);
    }
    if (WantsExactSchema(Query))
    {
        FString SchemaText;
        FString SchemaError;
        if (!StateTreeSchema::DescribeExactObject(
                *StateTree,
                *EditorData,
                TEXT("target"),
                FString(),
                SchemaText,
                SchemaError))
        {
            return QueryError(
                TEXT("validation.result_too_large"),
                SchemaError,
                TEXT("target"),
                Target.AssetPath);
        }
        Builder.AddComment(SchemaText);
    }
    Index.AddComments(Builder);
    TArray<TSharedPtr<FJsonObject>> Diagnostics;
    Index.AddDiagnostics(Diagnostics);
    return Builder.BuildResult(Diagnostics);
}

TSharedPtr<FJsonObject> QuerySummary(
    const FSalQuery& Query,
    const FSalResolvedTarget& Target,
    UStateTree* StateTree,
    UStateTreeEditorData* EditorData,
    FAuthoredStateIndex& Index,
    FReadContext& Context)
{
    if (!Query.With.IsEmpty() || HasUnsupportedExactClauses(Query))
    {
        return QueryError(
            TEXT("capability.clause_unavailable"),
            TEXT("StateTree summary accepts no Query clauses."),
            TEXT("summary"));
    }

    FSalObjectBuilder Builder;
    Builder.AddLocalBinding(Query.Alias, StateTreeAssetValue(Target, StateTree, EditorData));
    TArray<TSharedPtr<FJsonObject>> Diagnostics;
    FStateTreeRelationshipIndex Relationships(*EditorData, Index, Context);
    FUniqueNameAllocator LocalAliases;
    LocalAliases.Reserve(Query.Alias);

    for (const FAuthoredContextEntry& Entry : Index.GetContextEntries())
    {
        if (Entry.Desc == nullptr || !Context.ConsumeEmittedObject())
        {
            break;
        }
        const FString Alias = LocalAliases.Allocate(Entry.Desc->Name.ToString(), TEXT("context"));
        Builder.AddLocalBinding(Alias, ContextValue(*Entry.Desc, Entry, Index));
    }

    bool bEmissionComplete = EmitNodeArray(
        Builder, Context, Index, Query.Alias, {}, TEXT("Evaluators"), EditorData->Evaluators);
    if (bEmissionComplete)
    {
        bEmissionComplete = EmitNodeArray(
            Builder, Context, Index, Query.Alias, {}, TEXT("GlobalTasks"), EditorData->GlobalTasks);
    }
    for (const int32 RootIndex : Index.GetRoots())
    {
        if (!bEmissionComplete || !Context.ConsumeEmittedObject())
        {
            break;
        }
        const FAuthoredStateEntry* Root = Index.GetEntry(RootIndex);
        if (Root == nullptr || Root->State == nullptr)
        {
            continue;
        }
        const FString Alias = LocalAliases.Allocate(Root->State->Name.ToString(), TEXT("state"));
        Builder.AddLocalBinding(Alias, StateValue(*Root->State, EStateEmission::Compact, Index));
    }

    const FStateTreeCounts Counts = CountAuthored(*EditorData, Index, Context);
    Builder.AddComment(FString::Printf(
        TEXT("%s authored counts:\n  states: %d\n  evaluators: %d\n  global tasks: %d\n  tasks: %d\n  conditions: %d\n  considerations: %d\n  transitions: %d\n  parameters: %d\n  property functions: %d\n  property bindings: %d\n  context data: %d"),
        Counts.bComplete ? TEXT("complete") : TEXT("partial"),
        Counts.States,
        Counts.Evaluators,
        Counts.GlobalTasks,
        Counts.Tasks,
        Counts.Conditions,
        Counts.Considerations,
        Counts.Transitions,
        Counts.Parameters,
        Counts.PropertyFunctions,
        Counts.PropertyBindings,
        Counts.ContextData));
    const uint32 EditorHash = UStateTreeEditingSubsystem::CalculateStateTreeHash(StateTree);
    Builder.AddComment(FString::Printf(
        TEXT("compile orientation:\n  ready to run: %s\n  editor data hash: %u\n  last compiled editor data hash: %u\n  stale: %s"),
        StateTree->IsReadyToRun() ? TEXT("true") : TEXT("false"),
        EditorHash,
        StateTree->LastCompiledEditorDataHash,
        EditorHash == StateTree->LastCompiledEditorDataHash ? TEXT("false") : TEXT("true")));
    Builder.AddComment(FString::Printf(
        TEXT("automatic context relationships: %d"),
        Relationships.GetAutomaticContextRelationshipCount()));
    Index.AddComments(Builder);
    Index.AddDiagnostics(Diagnostics);
    Relationships.AddDiagnosticsForAsset(Builder, Diagnostics);
    return Builder.BuildResult(Diagnostics);
}

TSharedPtr<FJsonObject> QueryTree(
    const FSalQuery& Query,
    const FSalResolvedTarget& Target,
    UStateTree* StateTree,
    FAuthoredStateIndex& Index,
    FReadContext& Context)
{
    if (!Query.With.IsEmpty() || HasUnsupportedExactClauses(Query))
    {
        return QueryError(
            TEXT("capability.clause_unavailable"),
            TEXT("StateTree tree accepts only an optional state root and depth."),
            TEXT("tree"));
    }
    double RequestedDepth = DefaultTreeDepth;
    Query.Operation->TryGetNumberField(TEXT("depth"), RequestedDepth);
    const int32 Depth = FMath::Max(1, static_cast<int32>(RequestedDepth));

    const TSharedPtr<FJsonObject>* RootRef = nullptr;
    if (Query.Operation->TryGetObjectField(TEXT("root"), RootRef)
        && RootRef != nullptr
        && (*RootRef).IsValid())
    {
        FString Kind;
        FString IdText;
        FGuid Id;
        if (!(*RootRef)->TryGetStringField(TEXT("kind"), Kind)
            || Kind != TEXT("state")
            || !(*RootRef)->TryGetStringField(TEXT("id"), IdText)
            || !FGuid::Parse(IdText, Id)
            || !Id.IsValid())
        {
            return QueryError(
                TEXT("resolution.invalid_traversal_target"),
                TEXT("StateTree tree root must be one valid state@id reference."),
                TEXT("tree"),
                IdText);
        }
        if (!Index.IsHierarchyComplete())
        {
            return QueryError(
                TEXT("validation.reference_scan_incomplete"),
                TEXT("The authored State hierarchy exceeded a hard traversal limit, so this state@id cannot be resolved exactly."),
                TEXT("tree"),
                FString::Printf(TEXT("state@%s"), *IdText));
        }
        const TArray<int32> Matches = Index.FindById(Id);
        if (Matches.Num() != 1 || !Index.IsCanonicalStateId(Id))
        {
            return QueryError(
                Matches.IsEmpty() ? TEXT("resolution.object_not_found") : TEXT("resolution.ambiguous_selector"),
                Matches.IsEmpty()
                    ? TEXT("StateTree tree root State was not found in the bound asset.")
                    : TEXT("StateTree tree root State does not have one unambiguous authored ownership."),
                TEXT("tree"),
                FString::Printf(TEXT("state@%s"), *IdText));
        }
        FSalObjectBuilder Builder;
        Builder.AddLocalBinding(Query.Alias, StateTreeAssetIdentityValue(Target, StateTree));
        const FAuthoredStateEntry* Root = Index.GetEntry(Matches[0]);
        if (Root == nullptr || Root->State == nullptr)
        {
            return QueryError(
                TEXT("resolution.object_not_found"),
                TEXT("Indexed StateTree root is unavailable."),
                TEXT("tree"),
                FString::Printf(TEXT("state@%s"), *IdText));
        }
        FUniqueNameAllocator Aliases;
        Aliases.Reserve(Query.Alias);
        const FString Alias = Aliases.Allocate(Root->State->Name.ToString(), TEXT("state"));
        EmitIndexedStateSubtree(Builder, Context, Index, Matches[0], Alias, Depth);
        Index.AddComments(Builder);
        TArray<TSharedPtr<FJsonObject>> Diagnostics;
        Index.AddDiagnostics(Diagnostics);
        return Builder.BuildResult(Diagnostics);
    }

    FSalObjectBuilder Builder;
    Builder.AddLocalBinding(Query.Alias, StateTreeAssetIdentityValue(Target, StateTree));
    FUniqueNameAllocator Aliases;
    Aliases.Reserve(Query.Alias);
    for (const int32 RootIndex : Index.GetRoots())
    {
        const FAuthoredStateEntry* Root = Index.GetEntry(RootIndex);
        if (Root == nullptr || Root->State == nullptr)
        {
            continue;
        }
        const FString Alias = Aliases.Allocate(Root->State->Name.ToString(), TEXT("state"));
        if (!EmitIndexedStateSubtree(Builder, Context, Index, RootIndex, Alias, Depth))
        {
            break;
        }
    }
    if (Index.GetRoots().IsEmpty())
    {
        Builder.AddComment(TEXT("empty StateTree hierarchy"));
    }
    Index.AddComments(Builder);
    TArray<TSharedPtr<FJsonObject>> Diagnostics;
    Index.AddDiagnostics(Diagnostics);
    return Builder.BuildResult(Diagnostics);
}

TSharedPtr<FJsonObject> QueryState(
    const FSalQuery& Query,
    const FSalResolvedTarget& Target,
    UStateTree* StateTree,
    UStateTreeEditorData* EditorData,
    FAuthoredStateIndex& Index,
    FReadContext& Context)
{
    if (HasUnsupportedExactClauses(Query))
    {
        return QueryError(
            TEXT("capability.clause_unavailable"),
            TEXT("Exact State Query accepts only optional with schema."),
            TEXT("state"));
    }
    if (HasUnsupportedExactDetail(Query))
    {
        return QueryError(
            TEXT("capability.detail_unavailable"),
            TEXT("Exact State Query supports only with schema."),
            TEXT("state"),
            Query.With[0]);
    }
    FString IdText;
    FGuid Id;
    if (!Query.Operation->TryGetStringField(TEXT("id"), IdText)
        || !FGuid::Parse(IdText, Id)
        || !Id.IsValid())
    {
        return QueryError(
            TEXT("validation.invalid_target"),
            TEXT("Exact State Query requires one valid native State Guid."),
            TEXT("state"),
            IdText);
    }
    if (!Index.IsHierarchyComplete())
    {
        return QueryError(
            TEXT("validation.reference_scan_incomplete"),
            TEXT("The authored State hierarchy exceeded a hard traversal limit, so this state@id cannot be resolved exactly."),
            TEXT("state"),
            FString::Printf(TEXT("state@%s"), *IdText));
    }
    const TArray<int32> Matches = Index.FindById(Id);
    if (Matches.Num() != 1 || !Index.IsCanonicalStateId(Id))
    {
        return QueryError(
            Matches.IsEmpty() ? TEXT("resolution.object_not_found") : TEXT("resolution.ambiguous_selector"),
            Matches.IsEmpty()
                ? TEXT("State was not found in the bound StateTree asset.")
                : TEXT("State does not have one unambiguous authored ownership in the bound StateTree asset."),
            TEXT("state"),
            FString::Printf(TEXT("state@%s"), *IdText));
    }

    const FAuthoredStateEntry* StateEntry = Index.GetEntry(Matches[0]);
    if (StateEntry == nullptr || StateEntry->State == nullptr)
    {
        return QueryError(
            TEXT("resolution.object_not_found"),
            TEXT("Indexed State is unavailable."),
            TEXT("state"),
            FString::Printf(TEXT("state@%s"), *IdText));
    }
    const UStateTreeState& State = *StateEntry->State;
    FStateTreeRelationshipIndex Relationships(*EditorData, Index, Context);
    FSalObjectBuilder Builder;
    Builder.AddLocalBinding(Query.Alias, StateTreeAssetIdentityValue(Target, StateTree));
    FUniqueNameAllocator Aliases;
    Aliases.Reserve(Query.Alias);
    const FString Alias = Aliases.Allocate(State.Name.ToString(), TEXT("state"));
    bool bEmissionComplete = Context.ConsumeEmittedObject();
    if (bEmissionComplete)
    {
        Builder.AddLocalBinding(Alias, StateValue(State, EStateEmission::Exact, Index));
        bEmissionComplete = EmitOwnedStateObjects(Builder, Context, Index, Alias, {}, State);
    }
    for (const int32 ChildIndex : StateEntry->Children)
    {
        if (!bEmissionComplete || !Context.ConsumeEmittedObject())
        {
            break;
        }
        const FAuthoredStateEntry* Child = Index.GetEntry(ChildIndex);
        if (Child == nullptr || Child->State == nullptr)
        {
            continue;
        }
        const TArray<FString> ChildPath = {Child->MemberName};
        if (!Context.ConsumeMemberPath(ChildPath.Num()))
        {
            break;
        }
        Builder.AddMemberBinding(
            Alias,
            ChildPath,
            StateValue(*Child->State, EStateEmission::Compact, Index));
    }
    FString NativePath;
    if (Index.TryGetNativePath(Matches[0], NativePath))
    {
        Builder.AddComment(FString::Printf(
            TEXT("owner asset: %s\nstate path: %s"),
            *Target.AssetPath,
            *NativePath));
    }
    else
    {
        Builder.AddComment(FString::Printf(TEXT("owner asset: %s\nstate path: truncated"), *Target.AssetPath));
    }
    if (State.Type != EStateTreeStateType::Linked
        && (State.LinkedSubtree.ID.IsValid()
            || !State.LinkedSubtree.Name.IsNone()
            || State.LinkedSubtree.LinkType == EStateTreeTransitionType::GotoState))
    {
        Builder.AddComment(TEXT("dormant: LinkedSubtree is authored but inactive while State.Type is not Linked"));
    }
    if (!State.bHasRequiredEventToEnter && State.RequiredEventToEnter.IsValid())
    {
        Builder.AddComment(TEXT("inactive: RequiredEventToEnter is authored while bHasRequiredEventToEnter is false"));
    }
    if (WantsExactSchema(Query))
    {
        FString SchemaText;
        FString SchemaError;
        if (!StateTreeSchema::DescribeExactObject(
                *StateTree,
                *EditorData,
                TEXT("state"),
                IdText,
                SchemaText,
                SchemaError))
        {
            return QueryError(
                TEXT("validation.result_too_large"),
                SchemaError,
                TEXT("state"),
                FString::Printf(TEXT("state@%s"), *IdText));
        }
        Builder.AddComment(SchemaText);
    }
    Relationships.EmitForOwner(Builder, ERelationshipOwnerKind::State, Matches[0]);
    Index.AddComments(Builder);
    TArray<TSharedPtr<FJsonObject>> Diagnostics;
    Index.AddDiagnostics(Diagnostics);
    Relationships.AddDiagnosticsForOwner(
        Diagnostics,
        ERelationshipOwnerKind::State,
        Matches[0]);
    return Builder.BuildResult(Diagnostics);
}

TSharedPtr<FJsonObject> QueryStates(
    const FSalQuery& Query,
    const FSalResolvedTarget& Target,
    UStateTree* StateTree,
    FAuthoredStateIndex& Index,
    FReadContext& Context)
{
    if (HasUnsupportedCollectionClauses(Query))
    {
        return QueryError(
            TEXT("capability.clause_unavailable"),
            TEXT("StateTree states accepts only optional text search and cursor page clauses."),
            TEXT("states"));
    }
    FSalPage Page;
    if (!DecodeStateTreePage(Target, StateTree, Query, Page))
    {
        return QueryError(
            TEXT("validation.invalid_cursor"),
            TEXT("StateTree cursor does not belong to this target, authored revision, operation, search, or page limit. Re-run the first page."),
            TEXT("states"),
            Query.PageAfter);
    }

    FString SearchText;
    Query.Operation->TryGetStringField(TEXT("text"), SearchText);
    TArray<int32> Matches;
    TMap<int32, FString> StatePaths;
    for (int32 StateIndex = 0; StateIndex < Index.GetEntries().Num(); ++StateIndex)
    {
        const FAuthoredStateEntry* Entry = Index.GetEntry(StateIndex);
        if (Entry == nullptr || Entry->State == nullptr)
        {
            continue;
        }
        const UStateTreeState& State = *Entry->State;
        bool bMatch = SearchText.IsEmpty()
            || ContainsSearchText(State.Name.ToString(), SearchText)
            || ContainsSearchText(State.Description, SearchText)
            || ContainsSearchText(State.Tag.ToString(), SearchText)
            || ContainsSearchText(State.GetClass()->GetPathName(), SearchText)
            || ContainsSearchText(StateTypeText(State), SearchText);
        if (!bMatch)
        {
            bMatch = ContainsSearchText(StatePathFor(Index, StateIndex, StatePaths), SearchText);
        }
        if (bMatch)
        {
            Matches.Add(StateIndex);
        }
    }
    if (Page.Offset > Matches.Num())
    {
        return QueryError(
            TEXT("validation.invalid_cursor"),
            TEXT("StateTree cursor offset is outside the current authored result set. Re-run the first page."),
            TEXT("states"),
            Query.PageAfter);
    }

    const int32 End = static_cast<int32>(FMath::Min<int64>(
        static_cast<int64>(Matches.Num()),
        static_cast<int64>(Page.Offset) + static_cast<int64>(Page.Limit)));
    FSalObjectBuilder Builder;
    Builder.AddLocalBinding(Query.Alias, StateTreeAssetIdentityValue(Target, StateTree));
    FUniqueNameAllocator Aliases;
    Aliases.Reserve(Query.Alias);
    for (int32 MatchIndex = Page.Offset; MatchIndex < End; ++MatchIndex)
    {
        const int32 StateIndex = Matches[MatchIndex];
        const FAuthoredStateEntry* Entry = Index.GetEntry(StateIndex);
        if (Entry == nullptr || Entry->State == nullptr || !Context.ConsumeEmittedObject())
        {
            continue;
        }
        const FString Alias = Aliases.Allocate(Entry->State->Name.ToString(), TEXT("state"));
        Builder.AddLocalBinding(Alias, StateValue(*Entry->State, EStateEmission::Compact, Index));
        const FString Path = StatePathFor(Index, StateIndex, StatePaths);
        Builder.AddComment(FString::Printf(
            TEXT("state path: %s"),
            Path.IsEmpty() ? TEXT("<path unavailable>") : *Path));
    }
    if (Matches.IsEmpty())
    {
        Builder.AddComment(TEXT("no matches"));
    }
    Index.AddComments(Builder);
    TArray<TSharedPtr<FJsonObject>> Diagnostics;
    Index.AddDiagnostics(Diagnostics);
    return MakeStateTreePageResult(
        Builder.BuildResult(Diagnostics),
        Target,
        StateTree,
        Query,
        Page,
        End,
        End < Matches.Num());
}

TSharedPtr<FJsonObject> QueryNodes(
    const FSalQuery& Query,
    const FSalResolvedTarget& Target,
    UStateTree* StateTree,
    FAuthoredStateIndex& Index,
    FReadContext& Context)
{
    if (HasUnsupportedCollectionClauses(Query))
    {
        return QueryError(
            TEXT("capability.clause_unavailable"),
            TEXT("StateTree nodes accepts only optional text search and cursor page clauses."),
            TEXT("nodes"));
    }
    FSalPage Page;
    if (!DecodeStateTreePage(Target, StateTree, Query, Page))
    {
        return QueryError(
            TEXT("validation.invalid_cursor"),
            TEXT("StateTree cursor does not belong to this target, authored revision, operation, search, or page limit. Re-run the first page."),
            TEXT("nodes"),
            Query.PageAfter);
    }

    FString SearchText;
    Query.Operation->TryGetStringField(TEXT("text"), SearchText);
    TArray<int32> Matches;
    TMap<int32, FString> StatePaths;
    const TArray<FAuthoredNodeEntry>& Nodes = Index.GetNodeEntries();
    for (int32 NodeIndex = 0; NodeIndex < Nodes.Num(); ++NodeIndex)
    {
        const FAuthoredNodeEntry& Entry = Nodes[NodeIndex];
        if (Entry.Node == nullptr || Entry.bPropertyFunction)
        {
            continue;
        }
        bool bMatch = SearchText.IsEmpty()
            || ContainsSearchText(Entry.Node->GetName().ToString(), SearchText)
            || ContainsSearchText(NodeTypePath(*Entry.Node), SearchText)
            || ContainsSearchText(Entry.Role, SearchText)
            || ContainsSearchText(Entry.MemberName, SearchText)
            || ContainsSearchText(NodeRolePath(Index, Entry), SearchText);
        if (!bMatch && Entry.OwnerStateIndex != INDEX_NONE)
        {
            const FAuthoredStateEntry* StateEntry = Index.GetEntry(Entry.OwnerStateIndex);
            bMatch = (StateEntry != nullptr
                    && StateEntry->State != nullptr
                    && ContainsSearchText(StateEntry->State->Name.ToString(), SearchText))
                || ContainsSearchText(
                    StatePathFor(Index, Entry.OwnerStateIndex, StatePaths),
                    SearchText);
        }
        if (bMatch)
        {
            Matches.Add(NodeIndex);
        }
    }
    if (Page.Offset > Matches.Num())
    {
        return QueryError(
            TEXT("validation.invalid_cursor"),
            TEXT("StateTree cursor offset is outside the current authored result set. Re-run the first page."),
            TEXT("nodes"),
            Query.PageAfter);
    }

    const int32 End = static_cast<int32>(FMath::Min<int64>(
        static_cast<int64>(Matches.Num()),
        static_cast<int64>(Page.Offset) + static_cast<int64>(Page.Limit)));
    FSalObjectBuilder Builder;
    Builder.AddLocalBinding(Query.Alias, StateTreeAssetIdentityValue(Target, StateTree));
    FUniqueNameAllocator Aliases;
    Aliases.Reserve(Query.Alias);
    for (int32 MatchIndex = Page.Offset; MatchIndex < End; ++MatchIndex)
    {
        const FAuthoredNodeEntry* Entry = Index.GetNodeEntry(Matches[MatchIndex]);
        if (Entry == nullptr || Entry->Node == nullptr || !Context.ConsumeEmittedObject())
        {
            continue;
        }
        const FString Preferred = Entry->Node->GetName().IsNone()
            ? Entry->MemberName
            : Entry->Node->GetName().ToString();
        Builder.AddLocalBinding(
            Aliases.Allocate(Preferred, TEXT("node")),
            NodeValue(*Entry->Node, Index));
        Builder.AddComment(NodeOwnerText(Index, *Entry, StatePaths));
    }
    if (Matches.IsEmpty())
    {
        Builder.AddComment(TEXT("no matches"));
    }
    Index.AddComments(Builder);
    TArray<TSharedPtr<FJsonObject>> Diagnostics;
    Index.AddDiagnostics(Diagnostics);
    return MakeStateTreePageResult(
        Builder.BuildResult(Diagnostics),
        Target,
        StateTree,
        Query,
        Page,
        End,
        End < Matches.Num());
}

FString ParameterOwnerText(
    FAuthoredStateIndex& Index,
    const FAuthoredParameterEntry& Entry,
    TMap<int32, FString>& StatePaths)
{
    if (Entry.bRoot)
    {
        return FString::Printf(
            TEXT("owner: root parameters\nrole: RootParameters[%d]\nlayout: editable\nvalue source: local"),
            Entry.DescriptorIndex);
    }
    const FAuthoredStateEntry* StateEntry = Index.GetEntry(Entry.OwnerStateIndex);
    const FString StatePath = StatePathFor(Index, Entry.OwnerStateIndex, StatePaths);
    const bool bFixedLayout = StateEntry != nullptr
        && StateEntry->State != nullptr
        && StateEntry->State->Parameters.bFixedLayout;
    const bool bOverridden = StateEntry != nullptr
        && StateEntry->State != nullptr
        && Entry.Desc != nullptr
        && StateEntry->State->Parameters.PropertyOverrides.Contains(Entry.Desc->ID);
    const bool bInheritedLayout = StateEntry != nullptr
        && StateEntry->State != nullptr
        && (StateEntry->State->Type == EStateTreeStateType::Linked
            || StateEntry->State->Type == EStateTreeStateType::LinkedAsset);
    const bool bHasInheritedSource = bInheritedLayout
        && StateEntry->State->GetDefaultParameters() != nullptr;
    const TCHAR* ValueSource = !bInheritedLayout
        ? TEXT("local")
        : bOverridden
            ? TEXT("local override")
            : bHasInheritedSource
                ? TEXT("inherited")
                : TEXT("unavailable (inheritance source unresolved)");
    const FString Owner = StateEntry != nullptr && StateEntry->State != nullptr
        ? Index.DescribeState(*StateEntry->State)
        : TEXT("<unavailable>");
    return FString::Printf(
        TEXT("owner state: %s\nstate path: %s\nrole: Parameters[%d]\nbFixedLayout: %s\nvalue source: %s"),
        *Owner,
        StatePath.IsEmpty() ? TEXT("<path unavailable>") : *StatePath,
        Entry.DescriptorIndex,
        bFixedLayout ? TEXT("true") : TEXT("false"),
        ValueSource);
}

TSharedPtr<FJsonObject> QueryParameters(
    const FSalQuery& Query,
    const FSalResolvedTarget& Target,
    UStateTree* StateTree,
    FAuthoredStateIndex& Index,
    FReadContext& Context)
{
    if (HasUnsupportedCollectionClauses(Query))
    {
        return QueryError(
            TEXT("capability.clause_unavailable"),
            TEXT("StateTree parameters accepts only optional text search and cursor page clauses."),
            TEXT("parameters"));
    }
    FSalPage Page;
    if (!DecodeStateTreePage(Target, StateTree, Query, Page))
    {
        return QueryError(
            TEXT("validation.invalid_cursor"),
            TEXT("StateTree cursor does not belong to this target, authored revision, operation, search, or page limit. Re-run the first page."),
            TEXT("parameters"),
            Query.PageAfter);
    }

    FString SearchText;
    Query.Operation->TryGetStringField(TEXT("text"), SearchText);
    TArray<int32> Matches;
    TMap<int32, FString> StatePaths;
    const TArray<FAuthoredParameterEntry>& Parameters = Index.GetParameterEntries();
    for (int32 ParameterIndex = 0; ParameterIndex < Parameters.Num(); ++ParameterIndex)
    {
        const FAuthoredParameterEntry& Entry = Parameters[ParameterIndex];
        if (Entry.Desc == nullptr)
        {
            continue;
        }
        const FString OwnerPath = Entry.bRoot
            ? TEXT("RootParameters")
            : StatePathFor(Index, Entry.OwnerStateIndex, StatePaths) + TEXT("/Parameters");
        bool bMatch = SearchText.IsEmpty()
            || ContainsSearchText(Entry.Desc->Name.ToString(), SearchText)
            || ContainsSearchText(StateTreePropertyTypeText(Entry.Desc->CachedProperty), SearchText)
            || ContainsSearchText(OwnerPath, SearchText);
#if WITH_EDITORONLY_DATA
        if (!bMatch)
        {
            for (const FPropertyBagPropertyDescMetaData& Item : Entry.Desc->MetaData)
            {
                if (ContainsSearchText(Item.Key.ToString(), SearchText)
                    || ContainsSearchText(Item.Value, SearchText))
                {
                    bMatch = true;
                    break;
                }
            }
        }
#endif
        if (bMatch)
        {
            Matches.Add(ParameterIndex);
        }
    }
    if (Page.Offset > Matches.Num())
    {
        return QueryError(
            TEXT("validation.invalid_cursor"),
            TEXT("StateTree cursor offset is outside the current authored result set. Re-run the first page."),
            TEXT("parameters"),
            Query.PageAfter);
    }

    const int32 End = static_cast<int32>(FMath::Min<int64>(
        static_cast<int64>(Matches.Num()),
        static_cast<int64>(Page.Offset) + static_cast<int64>(Page.Limit)));
    FSalObjectBuilder Builder;
    Builder.AddLocalBinding(Query.Alias, StateTreeAssetIdentityValue(Target, StateTree));
    FUniqueNameAllocator Aliases;
    Aliases.Reserve(Query.Alias);
    TMap<int32, FString> StateAliases;
    for (int32 MatchIndex = Page.Offset; MatchIndex < End; ++MatchIndex)
    {
        const FAuthoredParameterEntry* Entry = Index.GetParameterEntry(Matches[MatchIndex]);
        if (Entry == nullptr || Entry->Desc == nullptr || !Context.ConsumeEmittedObject())
        {
            continue;
        }
        if (Entry->bRoot)
        {
            Builder.AddMemberBinding(
                Query.Alias,
                {TEXT("RootParameters"), Entry->MemberName},
                ParameterValue(*Entry, Index, false));
        }
        else
        {
            const FAuthoredStateEntry* StateEntry = Index.GetEntry(Entry->OwnerStateIndex);
            if (StateEntry == nullptr || StateEntry->State == nullptr)
            {
                continue;
            }
            FString* StateAlias = StateAliases.Find(Entry->OwnerStateIndex);
            if (StateAlias == nullptr)
            {
                const FString NewAlias = Aliases.Allocate(StateEntry->State->Name.ToString(), TEXT("state"));
                StateAlias = &StateAliases.Add(Entry->OwnerStateIndex, NewAlias);
                Builder.AddLocalBinding(
                    *StateAlias,
                    StateValue(*StateEntry->State, EStateEmission::Compact, Index));
            }
            Builder.AddMemberBinding(
                *StateAlias,
                {TEXT("Parameters"), Entry->MemberName},
                ParameterValue(*Entry, Index, false));
        }
        Builder.AddComment(ParameterOwnerText(Index, *Entry, StatePaths));
    }
    if (Matches.IsEmpty())
    {
        Builder.AddComment(TEXT("no matches"));
    }
    Index.AddComments(Builder);
    TArray<TSharedPtr<FJsonObject>> Diagnostics;
    Index.AddDiagnostics(Diagnostics);
    return MakeStateTreePageResult(
        Builder.BuildResult(Diagnostics),
        Target,
        StateTree,
        Query,
        Page,
        End,
        End < Matches.Num());
}

bool ParseParameterId(const FString& IdText, FGuid& OutContainerId, FGuid& OutPropertyId)
{
    TArray<FString> Parts;
    IdText.ParseIntoArray(Parts, TEXT("/"), false);
    return Parts.Num() == 2
        && FGuid::Parse(Parts[0], OutContainerId)
        && OutContainerId.IsValid()
        && FGuid::Parse(Parts[1], OutPropertyId)
        && OutPropertyId.IsValid();
}

TSharedPtr<FJsonObject> QueryParameter(
    const FSalQuery& Query,
    const FSalResolvedTarget& Target,
    UStateTree* StateTree,
    UStateTreeEditorData* EditorData,
    FAuthoredStateIndex& Index,
    FReadContext& Context)
{
    if (HasUnsupportedExactClauses(Query))
    {
        return QueryError(
            TEXT("capability.clause_unavailable"),
            TEXT("Exact StateTree Parameter Query accepts only optional with schema."),
            TEXT("parameter"));
    }
    if (HasUnsupportedExactDetail(Query))
    {
        return QueryError(
            TEXT("capability.detail_unavailable"),
            TEXT("Exact StateTree Parameter Query supports only with schema."),
            TEXT("parameter"),
            Query.With[0]);
    }
    FString IdText;
    FGuid ContainerId;
    FGuid PropertyId;
    if (!Query.Operation->TryGetStringField(TEXT("id"), IdText)
        || !ParseParameterId(IdText, ContainerId, PropertyId))
    {
        return QueryError(
            TEXT("validation.invalid_target"),
            TEXT("Exact StateTree Parameter Query requires one valid parameter@container-guid/property-guid reference."),
            TEXT("parameter"),
            IdText);
    }
    const FString Ref = FString::Printf(TEXT("parameter@%s"), *IdText);
    if (!Index.IsParameterIdentityComplete())
    {
        return QueryError(
            TEXT("validation.reference_scan_incomplete"),
            TEXT("The authored StateTree Parameter identity scan exceeded a hard traversal limit or could not prove complete container uniqueness."),
            TEXT("parameter"),
            Ref);
    }
    const TArray<int32> Matches = Index.FindParameterById(ContainerId, PropertyId);
    const FAuthoredParameterEntry* Entry = Matches.Num() == 1
        ? Index.GetParameterEntry(Matches[0])
        : nullptr;
    if (Matches.Num() != 1 || Entry == nullptr || !Index.IsCanonicalParameter(*Entry))
    {
        const bool bMissing = Matches.IsEmpty();
        return QueryError(
            bMissing ? TEXT("resolution.object_not_found") : TEXT("resolution.ambiguous_selector"),
            bMissing
                ? TEXT("Parameter was not found in the bound StateTree asset.")
                : TEXT("Parameter does not have one unambiguous safe authored container and property identity in the bound StateTree asset."),
            TEXT("parameter"),
            Ref);
    }
    if (Entry->Bag == nullptr || Entry->Desc == nullptr)
    {
        return QueryError(
            TEXT("validation.invalid_target"),
            TEXT("The exact Parameter's authored descriptor is unavailable."),
            TEXT("parameter"),
            Ref);
    }
    const FConstStructView BagValue = Entry->Bag->GetValue();
    if (Entry->Desc->CachedProperty != nullptr)
    {
        if (!BagValue.IsValid() || BagValue.GetMemory() == nullptr)
        {
            Context.AddIssue(
                TEXT("The exact Parameter has no compatible authored Property Bag value storage; Value was omitted."),
                Ref);
        }
        else
        {
            const FString NativeValueText = ExportPropertyValue(
                Entry->Desc->CachedProperty,
                BagValue.GetMemory());
            if (NativeValueText.Len() > MaxExactParameterValueCharacters)
            {
                return QueryError(
                    TEXT("validation.result_too_large"),
                    FString::Printf(
                        TEXT("The exact Parameter native value requires %d characters, above the hard limit of %d. The value was not truncated."),
                        NativeValueText.Len(),
                        MaxExactParameterValueCharacters),
                    TEXT("parameter"),
                    Ref);
            }
        }
    }

    FStateTreeRelationshipIndex Relationships(*EditorData, Index, Context);
    FSalObjectBuilder Builder;
    Builder.AddLocalBinding(Query.Alias, StateTreeAssetIdentityValue(Target, StateTree));
    FUniqueNameAllocator Aliases;
    Aliases.Reserve(Query.Alias);
    TMap<int32, FString> StatePaths;
    if (Entry->bRoot)
    {
        Builder.AddMemberBinding(
            Query.Alias,
            {TEXT("RootParameters"), Entry->MemberName},
            ParameterValue(*Entry, Index, true));
    }
    else
    {
        const FAuthoredStateEntry* StateEntry = Index.GetEntry(Entry->OwnerStateIndex);
        if (StateEntry == nullptr || StateEntry->State == nullptr)
        {
            return QueryError(
                TEXT("resolution.object_not_found"),
                TEXT("The exact Parameter's owner State is unavailable."),
                TEXT("parameter"),
                Ref);
        }
        const FString StateAlias = Aliases.Allocate(StateEntry->State->Name.ToString(), TEXT("state"));
        Builder.AddLocalBinding(
            StateAlias,
            StateValue(*StateEntry->State, EStateEmission::Compact, Index));
        Builder.AddMemberBinding(
            StateAlias,
            {TEXT("Parameters"), Entry->MemberName},
            ParameterValue(*Entry, Index, true));
    }
    Builder.AddComment(FString::Printf(
        TEXT("owner asset: %s\n%s"),
        *Target.AssetPath,
        *ParameterOwnerText(Index, *Entry, StatePaths)));
    if (WantsExactSchema(Query))
    {
        FString SchemaText;
        FString SchemaError;
        if (!StateTreeSchema::DescribeExactObject(
                *StateTree,
                *EditorData,
                TEXT("parameter"),
                IdText,
                SchemaText,
                SchemaError))
        {
            return QueryError(
                TEXT("validation.result_too_large"),
                SchemaError,
                TEXT("parameter"),
                FString::Printf(TEXT("parameter@%s"), *IdText));
        }
        Builder.AddComment(SchemaText);
    }
    Relationships.EmitForOwner(Builder, ERelationshipOwnerKind::Parameter, Matches[0]);
    Index.AddComments(Builder);
    TArray<TSharedPtr<FJsonObject>> Diagnostics;
    Index.AddDiagnostics(Diagnostics);
    Relationships.AddDiagnosticsForOwner(
        Diagnostics,
        ERelationshipOwnerKind::Parameter,
        Matches[0]);
    return Builder.BuildResult(Diagnostics);
}

TSharedPtr<FJsonObject> QueryContextObject(
    const FSalQuery& Query,
    const FSalResolvedTarget& Target,
    UStateTree* StateTree,
    UStateTreeEditorData* EditorData,
    FAuthoredStateIndex& Index,
    FReadContext& Context)
{
    if (HasUnsupportedExactClauses(Query))
    {
        return QueryError(
            TEXT("capability.clause_unavailable"),
            TEXT("Exact StateTree Context Data Query accepts only optional with schema."),
            TEXT("object"));
    }
    if (HasUnsupportedExactDetail(Query))
    {
        return QueryError(
            TEXT("capability.detail_unavailable"),
            TEXT("Exact StateTree Context Data Query supports only with schema."),
            TEXT("object"),
            Query.With[0]);
    }
    FString IdText;
    FGuid Id;
    if (!Query.Operation->TryGetStringField(TEXT("id"), IdText)
        || !FGuid::Parse(IdText, Id)
        || !Id.IsValid())
    {
        return QueryError(
            TEXT("validation.invalid_target"),
            TEXT("Exact StateTree Context Data Query requires one valid object@context-guid reference."),
            TEXT("object"),
            IdText);
    }
    const FString Ref = FString::Printf(TEXT("object@%s"), *IdText);
    if (!Index.IsContextIdentityComplete())
    {
        return QueryError(
            TEXT("validation.reference_scan_incomplete"),
            TEXT("The Schema Context Data identity scan exceeded a hard traversal limit, so this object@id cannot be resolved exactly."),
            TEXT("object"),
            Ref);
    }
    const TArray<int32> Matches = Index.FindContextById(Id);
    const FAuthoredContextEntry* Entry = Matches.Num() == 1
        ? Index.GetContextEntry(Matches[0])
        : nullptr;
    const FStateTreeExternalDataDesc* CanonicalDescriptor = nullptr;
    FString CanonicalError;
    if (Matches.Num() != 1
        || Entry == nullptr
        || !StateTreeSchema::ResolveCanonicalContext(
            *EditorData,
            Id,
            CanonicalDescriptor,
            CanonicalError)
        || CanonicalDescriptor != Entry->Desc)
    {
        const bool bMissing = Matches.IsEmpty();
        return QueryError(
            bMissing ? TEXT("resolution.object_not_found") : TEXT("resolution.ambiguous_selector"),
            bMissing
                ? TEXT("Schema Context Data was not found in the bound StateTree asset.")
                : TEXT("Schema Context Data does not have one unambiguous native id in the bound StateTree asset."),
            TEXT("object"),
            Ref);
    }
    if (Entry->Desc == nullptr)
    {
        return QueryError(
            TEXT("resolution.object_not_found"),
            TEXT("Indexed Schema Context Data is unavailable."),
            TEXT("object"),
            Ref);
    }

    FStateTreeRelationshipIndex Relationships(*EditorData, Index, Context);
    FSalObjectBuilder Builder;
    Builder.AddLocalBinding(Query.Alias, StateTreeAssetIdentityValue(Target, StateTree));
    FUniqueNameAllocator Aliases;
    Aliases.Reserve(Query.Alias);
    Builder.AddLocalBinding(
        Aliases.Allocate(Entry->Desc->Name.ToString(), TEXT("context")),
        ContextValue(*Entry->Desc, *Entry, Index));
    Builder.AddComment(FString::Printf(
        TEXT("owner asset: %s\nowner: Schema Context Data\nschema: %s\nruntime value: unavailable in authored asset"),
        *Target.AssetPath,
        EditorData->Schema != nullptr ? *EditorData->Schema->GetClass()->GetPathName() : TEXT("<unavailable>")));
    if (WantsExactSchema(Query))
    {
        FString SchemaText;
        FString SchemaError;
        if (!StateTreeSchema::DescribeExactObject(
                *StateTree,
                *EditorData,
                TEXT("object"),
                IdText,
                SchemaText,
                SchemaError))
        {
            return QueryError(
                TEXT("validation.result_too_large"),
                SchemaError,
                TEXT("object"),
                FString::Printf(TEXT("object@%s"), *IdText));
        }
        Builder.AddComment(SchemaText);
    }
    Relationships.EmitForOwner(Builder, ERelationshipOwnerKind::Context, Matches[0]);
    Index.AddComments(Builder);
    TArray<TSharedPtr<FJsonObject>> Diagnostics;
    Index.AddDiagnostics(Diagnostics);
    Relationships.AddDiagnosticsForOwner(
        Diagnostics,
        ERelationshipOwnerKind::Context,
        Matches[0]);
    return Builder.BuildResult(Diagnostics);
}

TArray<FString> NodeMemberPath(
    const FAuthoredStateIndex& Index,
    const FAuthoredNodeEntry& Entry)
{
    if (Entry.OwnerTransitionIndex != INDEX_NONE)
    {
        const FAuthoredTransitionEntry* Transition = Index.GetTransitionEntry(Entry.OwnerTransitionIndex);
        return {
            TEXT("Transitions"),
            Transition != nullptr ? Transition->MemberName : TEXT("transition"),
            TEXT("Conditions"),
            Entry.MemberName};
    }
    if (Entry.Role == TEXT("SingleTask"))
    {
        return {TEXT("SingleTask")};
    }
    return {Entry.Role, Entry.MemberName};
}

TSharedPtr<FJsonObject> QueryNode(
    const FSalQuery& Query,
    const FSalResolvedTarget& Target,
    UStateTree* StateTree,
    UStateTreeEditorData* EditorData,
    FAuthoredStateIndex& Index,
    FReadContext& Context)
{
    if (HasUnsupportedExactClauses(Query))
    {
        return QueryError(
            TEXT("capability.clause_unavailable"),
            TEXT("Exact StateTree Node Query accepts only optional with schema."),
            TEXT("node"));
    }
    if (HasUnsupportedExactDetail(Query))
    {
        return QueryError(
            TEXT("capability.detail_unavailable"),
            TEXT("Exact StateTree Node Query supports only with schema."),
            TEXT("node"),
            Query.With[0]);
    }
    FString IdText;
    FGuid Id;
    if (!Query.Operation->TryGetStringField(TEXT("id"), IdText)
        || !FGuid::Parse(IdText, Id)
        || !Id.IsValid())
    {
        return QueryError(
            TEXT("validation.invalid_target"),
            TEXT("Exact StateTree Node Query requires one valid native Node Guid."),
            TEXT("node"),
            IdText);
    }
    if (!Index.IsNodeIdentityComplete())
    {
        return QueryError(
            TEXT("validation.reference_scan_incomplete"),
            TEXT("The authored StateTree Node identity scan exceeded a hard traversal limit, so this node@id cannot be resolved exactly."),
            TEXT("node"),
            FString::Printf(TEXT("node@%s"), *IdText));
    }
    const TArray<int32> Matches = Index.FindNodeById(Id);
    const FAuthoredNodeEntry* Entry = Matches.Num() == 1 ? Index.GetNodeEntry(Matches[0]) : nullptr;
    if (Matches.Num() != 1 || Entry == nullptr || !Index.IsSafeNodeEntry(*Entry))
    {
        const bool bMissing = Matches.IsEmpty();
        return QueryError(
            bMissing ? TEXT("resolution.object_not_found") : TEXT("resolution.ambiguous_selector"),
            bMissing
                ? TEXT("Node was not found in the bound StateTree asset.")
                : TEXT("Node does not have one unambiguous safe authored ownership in the bound StateTree asset."),
            TEXT("node"),
            FString::Printf(TEXT("node@%s"), *IdText));
    }
    if (Entry->Node == nullptr)
    {
        return QueryError(
            TEXT("resolution.object_not_found"),
            TEXT("Indexed StateTree Node is unavailable."),
            TEXT("node"),
            FString::Printf(TEXT("node@%s"), *IdText));
    }

    FStateTreeRelationshipIndex Relationships(*EditorData, Index, Context);
    FSalObjectBuilder Builder;
    Builder.AddLocalBinding(Query.Alias, StateTreeAssetIdentityValue(Target, StateTree));
    FUniqueNameAllocator Aliases;
    Aliases.Reserve(Query.Alias);
    TMap<int32, FString> StatePaths;
    if (Entry->bPropertyFunction)
    {
        const FString Alias = Aliases.Allocate(
            Entry->Node->GetName().IsNone() ? Entry->MemberName : Entry->Node->GetName().ToString(),
            TEXT("property_function"));
        if (Context.ConsumeEmittedObject())
        {
            Builder.AddLocalBinding(Alias, NodeValue(*Entry->Node, Index, &Context, true));
        }
    }
    else if (Entry->OwnerStateIndex == INDEX_NONE)
    {
        if (Context.ConsumeEmittedObject())
        {
            Builder.AddMemberBinding(
                Query.Alias,
                NodeMemberPath(Index, *Entry),
                NodeValue(*Entry->Node, Index, &Context, true));
        }
    }
    else
    {
        const FAuthoredStateEntry* StateEntry = Index.GetEntry(Entry->OwnerStateIndex);
        if (StateEntry == nullptr || StateEntry->State == nullptr)
        {
            return QueryError(
                TEXT("resolution.object_not_found"),
                TEXT("The exact Node's owner State is unavailable."),
                TEXT("node"),
                FString::Printf(TEXT("node@%s"), *IdText));
        }
        const FString StateAlias = Aliases.Allocate(StateEntry->State->Name.ToString(), TEXT("state"));
        Builder.AddLocalBinding(StateAlias, StateValue(*StateEntry->State, EStateEmission::Compact, Index));
        if (Context.ConsumeEmittedObject())
        {
            Builder.AddMemberBinding(
                StateAlias,
                NodeMemberPath(Index, *Entry),
                NodeValue(*Entry->Node, Index, &Context, true));
        }
    }
    if (Context.HasNativeSurfaceOverflow())
    {
        return QueryError(
            TEXT("validation.result_too_large"),
            FString::Printf(
                TEXT("Exact StateTree Node read exceeds the hard limit of %d fields or %lld native-value characters."),
                StateTreeSchema::FExactSchemaTextBuilder::MaxFields,
                static_cast<long long>(StateTreeSchema::FExactSchemaTextBuilder::MaxCharacters)),
            TEXT("node"),
            FString::Printf(TEXT("node@%s"), *IdText));
    }
    Builder.AddComment(NodeOwnerText(Index, *Entry, StatePaths));
    if (WantsExactSchema(Query))
    {
        FString SchemaText;
        FString SchemaError;
        if (!StateTreeSchema::DescribeExactObject(
                *StateTree,
                *EditorData,
                TEXT("node"),
                IdText,
                SchemaText,
                SchemaError))
        {
            return QueryError(
                TEXT("validation.result_too_large"),
                SchemaError,
                TEXT("node"),
                FString::Printf(TEXT("node@%s"), *IdText));
        }
        Builder.AddComment(SchemaText);
    }
    Relationships.EmitForOwner(Builder, ERelationshipOwnerKind::Node, Matches[0]);
    Index.AddComments(Builder);
    TArray<TSharedPtr<FJsonObject>> Diagnostics;
    Index.AddDiagnostics(Diagnostics);
    Relationships.AddDiagnosticsForOwner(
        Diagnostics,
        ERelationshipOwnerKind::Node,
        Matches[0]);
    return Builder.BuildResult(Diagnostics);
}

TSharedPtr<FJsonObject> QueryTransition(
    const FSalQuery& Query,
    const FSalResolvedTarget& Target,
    UStateTree* StateTree,
    UStateTreeEditorData* EditorData,
    FAuthoredStateIndex& Index,
    FReadContext& Context)
{
    if (HasUnsupportedExactClauses(Query))
    {
        return QueryError(
            TEXT("capability.clause_unavailable"),
            TEXT("Exact StateTree Transition Query accepts only optional with schema."),
            TEXT("transition"));
    }
    if (HasUnsupportedExactDetail(Query))
    {
        return QueryError(
            TEXT("capability.detail_unavailable"),
            TEXT("Exact StateTree Transition Query supports only with schema."),
            TEXT("transition"),
            Query.With[0]);
    }
    FString IdText;
    FGuid Id;
    if (!Query.Operation->TryGetStringField(TEXT("id"), IdText)
        || !FGuid::Parse(IdText, Id)
        || !Id.IsValid())
    {
        return QueryError(
            TEXT("validation.invalid_target"),
            TEXT("Exact StateTree Transition Query requires one valid native Transition Guid."),
            TEXT("transition"),
            IdText);
    }
    if (!Index.IsTransitionIdentityComplete())
    {
        return QueryError(
            TEXT("validation.reference_scan_incomplete"),
            TEXT("The authored StateTree Transition identity scan exceeded a hard traversal limit, so this transition@id cannot be resolved exactly."),
            TEXT("transition"),
            FString::Printf(TEXT("transition@%s"), *IdText));
    }
    const TArray<int32> Matches = Index.FindTransitionById(Id);
    const FAuthoredTransitionEntry* Entry = Matches.Num() == 1
        ? Index.GetTransitionEntry(Matches[0])
        : nullptr;
    if (Matches.Num() != 1 || Entry == nullptr || !Index.IsSafeTransitionEntry(*Entry))
    {
        const bool bMissing = Matches.IsEmpty();
        return QueryError(
            bMissing ? TEXT("resolution.object_not_found") : TEXT("resolution.ambiguous_selector"),
            bMissing
                ? TEXT("Transition was not found in the bound StateTree asset.")
                : TEXT("Transition does not have one unambiguous safe authored ownership in the bound StateTree asset."),
            TEXT("transition"),
            FString::Printf(TEXT("transition@%s"), *IdText));
    }
    if (Entry->Transition == nullptr)
    {
        return QueryError(
            TEXT("resolution.object_not_found"),
            TEXT("Indexed StateTree Transition is unavailable."),
            TEXT("transition"),
            FString::Printf(TEXT("transition@%s"), *IdText));
    }
    const FAuthoredStateEntry* StateEntry = Index.GetEntry(Entry->OwnerStateIndex);
    if (StateEntry == nullptr || StateEntry->State == nullptr)
    {
        return QueryError(
            TEXT("resolution.object_not_found"),
            TEXT("The exact Transition's owner State is unavailable."),
            TEXT("transition"),
            FString::Printf(TEXT("transition@%s"), *IdText));
    }

    FStateTreeRelationshipIndex Relationships(*EditorData, Index, Context);
    FSalObjectBuilder Builder;
    Builder.AddLocalBinding(Query.Alias, StateTreeAssetIdentityValue(Target, StateTree));
    FUniqueNameAllocator Aliases;
    Aliases.Reserve(Query.Alias);
    const FString StateAlias = Aliases.Allocate(StateEntry->State->Name.ToString(), TEXT("state"));
    Builder.AddLocalBinding(StateAlias, StateValue(*StateEntry->State, EStateEmission::Compact, Index));
    const TArray<FString> TransitionPath = {TEXT("Transitions"), Entry->MemberName};
    if (Context.ConsumeEmittedObject())
    {
        Builder.AddMemberBinding(
            StateAlias,
            TransitionPath,
            TransitionValue(*Entry->Transition, Index));
    }
    for (const FAuthoredNodeEntry& NodeEntry : Index.GetNodeEntries())
    {
        if (NodeEntry.OwnerTransitionIndex != Matches[0]
            || NodeEntry.Node == nullptr
            || !Context.ConsumeEmittedObject())
        {
            continue;
        }
        TArray<FString> ConditionPath = TransitionPath;
        ConditionPath.Add(TEXT("Conditions"));
        ConditionPath.Add(NodeEntry.MemberName);
        Builder.AddMemberBinding(StateAlias, ConditionPath, NodeValue(*NodeEntry.Node, Index));
    }
    TMap<int32, FString> StatePaths;
    Builder.AddComment(TransitionOwnerText(Index, *Entry, StatePaths));
    if (WantsExactSchema(Query))
    {
        FString SchemaText;
        FString SchemaError;
        if (!StateTreeSchema::DescribeExactObject(
                *StateTree,
                *EditorData,
                TEXT("transition"),
                IdText,
                SchemaText,
                SchemaError))
        {
            return QueryError(
                TEXT("validation.result_too_large"),
                SchemaError,
                TEXT("transition"),
                FString::Printf(TEXT("transition@%s"), *IdText));
        }
        Builder.AddComment(SchemaText);
    }
    Relationships.EmitForOwner(Builder, ERelationshipOwnerKind::Transition, Matches[0]);
    Index.AddComments(Builder);
    TArray<TSharedPtr<FJsonObject>> Diagnostics;
    Index.AddDiagnostics(Diagnostics);
    Relationships.AddDiagnosticsForOwner(
        Diagnostics,
        ERelationshipOwnerKind::Transition,
        Matches[0]);
    return Builder.BuildResult(Diagnostics);
}

struct FReferenceSubject
{
    ERelationshipOwnerKind OwnerKind = ERelationshipOwnerKind::Node;
    int32 OwnerIndex = INDEX_NONE;
    FString StableKind;
    FString StableId;
    TArray<FString> Path;
};

struct FReferenceUse
{
    ERelationshipOwnerKind OwnerKind = ERelationshipOwnerKind::Node;
    int32 OwnerIndex = INDEX_NONE;
    TArray<FString> Notes;
};

bool ReadReferencePath(
    const TSharedPtr<FJsonObject>& Ref,
    TSharedPtr<FJsonObject>& OutOwner,
    TArray<FString>& OutPath)
{
    OutOwner.Reset();
    OutPath.Reset();
    if (!Ref.IsValid())
    {
        return false;
    }
    FString Kind;
    if (!Ref->TryGetStringField(TEXT("kind"), Kind))
    {
        return false;
    }
    if (Kind != TEXT("member"))
    {
        OutOwner = Ref;
        return true;
    }
    const TSharedPtr<FJsonObject>* Owner = nullptr;
    const TArray<TSharedPtr<FJsonValue>>* Segments = nullptr;
    if (!Ref->TryGetObjectField(TEXT("object"), Owner)
        || Owner == nullptr
        || !(*Owner).IsValid()
        || !Ref->TryGetArrayField(TEXT("path"), Segments)
        || Segments == nullptr
        || Segments->IsEmpty())
    {
        return false;
    }
    for (const TSharedPtr<FJsonValue>& Segment : *Segments)
    {
        FString Text;
        double Number = 0;
        if (Segment.IsValid() && Segment->TryGetString(Text) && !Text.IsEmpty())
        {
            OutPath.Add(MoveTemp(Text));
        }
        else if (Segment.IsValid()
            && Segment->TryGetNumber(Number)
            && Number >= 0
            && Number <= MAX_int32
            && FMath::FloorToDouble(Number) == Number)
        {
            OutPath.Add(LexToString(static_cast<int32>(Number)));
        }
        else
        {
            return false;
        }
    }
    OutOwner = *Owner;
    return true;
}

bool ResolveReferenceSubject(
    const UStateTree& StateTree,
    const UStateTreeEditorData& EditorData,
    FAuthoredStateIndex& Index,
    const TSharedPtr<FJsonObject>& Ref,
    FReferenceSubject& OutSubject,
    FString& OutCode,
    FString& OutMessage)
{
    OutSubject = {};
    OutCode = TEXT("validation.invalid_target");
    OutMessage.Reset();
    TSharedPtr<FJsonObject> Owner;
    if (!ReadReferencePath(Ref, Owner, OutSubject.Path) || !Owner.IsValid())
    {
        OutMessage = TEXT("StateTree references requires one exact stable object or member reference.");
        return false;
    }
    if (!Owner->TryGetStringField(TEXT("kind"), OutSubject.StableKind)
        || !Owner->TryGetStringField(TEXT("id"), OutSubject.StableId))
    {
        OutMessage = TEXT("StateTree references requires one stable state, node, transition, parameter, or object reference.");
        return false;
    }

    auto ResolveSingle = [&](const TArray<int32>& Matches, const bool bComplete, const auto IsSafe)
    {
        if (!bComplete)
        {
            OutCode = TEXT("validation.reference_scan_incomplete");
            OutMessage = TEXT("The authored identity scan is incomplete, so references cannot return a complete factual result.");
            return false;
        }
        if (Matches.Num() != 1 || !IsSafe(Matches[0]))
        {
            OutCode = Matches.IsEmpty()
                ? TEXT("resolution.object_not_found")
                : TEXT("resolution.ambiguous_selector");
            OutMessage = Matches.IsEmpty()
                ? TEXT("The StateTree reference subject was not found in the bound asset.")
                : TEXT("The StateTree reference subject is not uniquely owned in the bound asset.");
            return false;
        }
        OutSubject.OwnerIndex = Matches[0];
        return true;
    };

    if (OutSubject.StableKind == TEXT("state"))
    {
        FGuid Id;
        if (!FGuid::Parse(OutSubject.StableId, Id) || !Id.IsValid())
        {
            OutMessage = TEXT("State reference id is not a valid native Guid.");
            return false;
        }
        OutSubject.OwnerKind = ERelationshipOwnerKind::State;
        if (!ResolveSingle(
                Index.FindById(Id),
                Index.IsHierarchyComplete(),
                [&](const int32 Match) { return Index.IsCanonicalStateId(Id); }))
        {
            return false;
        }
    }
    else if (OutSubject.StableKind == TEXT("node"))
    {
        FGuid Id;
        if (!FGuid::Parse(OutSubject.StableId, Id) || !Id.IsValid())
        {
            OutMessage = TEXT("Node reference id is not a valid native Guid.");
            return false;
        }
        OutSubject.OwnerKind = ERelationshipOwnerKind::Node;
        if (!ResolveSingle(
                Index.FindNodeById(Id),
                Index.IsNodeIdentityComplete(),
                [&](const int32 Match)
                {
                    const FAuthoredNodeEntry* Entry = Index.GetNodeEntry(Match);
                    return Entry != nullptr && Index.IsSafeNodeEntry(*Entry);
                }))
        {
            return false;
        }
    }
    else if (OutSubject.StableKind == TEXT("transition"))
    {
        FGuid Id;
        if (!FGuid::Parse(OutSubject.StableId, Id) || !Id.IsValid())
        {
            OutMessage = TEXT("Transition reference id is not a valid native Guid.");
            return false;
        }
        OutSubject.OwnerKind = ERelationshipOwnerKind::Transition;
        if (!ResolveSingle(
                Index.FindTransitionById(Id),
                Index.IsTransitionIdentityComplete(),
                [&](const int32 Match)
                {
                    const FAuthoredTransitionEntry* Entry = Index.GetTransitionEntry(Match);
                    return Entry != nullptr && Index.IsSafeTransitionEntry(*Entry);
                }))
        {
            return false;
        }
    }
    else if (OutSubject.StableKind == TEXT("parameter"))
    {
        FGuid ContainerId;
        FGuid PropertyId;
        if (!ParseParameterId(OutSubject.StableId, ContainerId, PropertyId))
        {
            OutMessage = TEXT("Parameter reference id must be container-guid/property-guid.");
            return false;
        }
        OutSubject.OwnerKind = ERelationshipOwnerKind::Parameter;
        if (!ResolveSingle(
                Index.FindParameterById(ContainerId, PropertyId),
                Index.IsParameterIdentityComplete(),
                [&](const int32 Match)
                {
                    const FAuthoredParameterEntry* Entry = Index.GetParameterEntry(Match);
                    return Entry != nullptr && Index.IsCanonicalParameter(*Entry);
                }))
        {
            return false;
        }
        // A parameter@id denotes its native descriptor value. `.Value` is the
        // explicit schema spelling of the same Binding endpoint.
        if (!OutSubject.Path.IsEmpty() && OutSubject.Path[0] == TEXT("Value"))
        {
            OutSubject.Path.RemoveAt(0);
        }
    }
    else if (OutSubject.StableKind == TEXT("object"))
    {
        FGuid Id;
        if (!FGuid::Parse(OutSubject.StableId, Id) || !Id.IsValid())
        {
            OutMessage = TEXT("Context object reference id is not a valid native Guid.");
            return false;
        }
        OutSubject.OwnerKind = ERelationshipOwnerKind::Context;
        if (!ResolveSingle(
                Index.FindContextById(Id),
                Index.IsContextIdentityComplete(),
                [&](const int32 Match)
                {
                    const FAuthoredContextEntry* Entry = Index.GetContextEntry(Match);
                    return Entry != nullptr && Index.IsCanonicalContext(*Entry);
                }))
        {
            return false;
        }
    }
    else
    {
        OutCode = TEXT("capability.reference_unavailable");
        OutMessage = TEXT("StateTree local references supports state, node, transition, parameter, and Schema Context object subjects.");
        return false;
    }

    if (!OutSubject.Path.IsEmpty())
    {
        StateTreeSchema::FResolvedMember Member;
        FString Message;
        if (!StateTreeSchema::ResolveMember(
                StateTree,
                EditorData,
                OutSubject.StableKind,
                OutSubject.StableId,
                OutSubject.Path,
                Member,
                Message)
            && !StateTreeSchema::ResolveMember(
                StateTree,
                EditorData,
                OutSubject.StableKind,
                OutSubject.StableId,
                OutSubject.Path,
                Member,
                Message,
                StateTreeSchema::EMemberPurpose::BindingSource))
        {
            OutCode = TEXT("resolution.object_not_found");
            OutMessage = Message.IsEmpty()
                ? TEXT("The exact StateTree member reference is not present in the current authored schema.")
                : Message;
            return false;
        }
    }
    return true;
}

bool EndpointMatchesReference(
    const FRelationshipEndpoint& Endpoint,
    const FReferenceSubject& Subject)
{
    if (!Endpoint.IsOwnedBy(Subject.OwnerKind, Subject.OwnerIndex))
    {
        return false;
    }
    if (Subject.Path.IsEmpty())
    {
        return true;
    }
    if (Subject.Path.Num() > Endpoint.Path.Num())
    {
        return false;
    }
    for (int32 Index = 0; Index < Subject.Path.Num(); ++Index)
    {
        const FRelationshipPathSegment& Segment = Endpoint.Path[Index];
        const FString EndpointText = Segment.Index != INDEX_NONE
            ? LexToString(Segment.Index)
            : Segment.Name;
        if (EndpointText != Subject.Path[Index])
        {
            return false;
        }
    }
    return true;
}

FString RelationshipEndpointText(const FRelationshipEndpoint& Endpoint)
{
    FString Result = Endpoint.StableKind + TEXT("@") + Endpoint.StableId;
    for (const FRelationshipPathSegment& Segment : Endpoint.Path)
    {
        Result += Segment.Index != INDEX_NONE
            ? FString::Printf(TEXT("[%d]"), Segment.Index)
            : TEXT(".") + Segment.Name;
    }
    return Result;
}

void AddReferenceUse(
    TArray<FReferenceUse>& Uses,
    TSet<FString>& Seen,
    const ERelationshipOwnerKind Kind,
    const int32 Index,
    const TArray<FString>& Notes = {})
{
    if (Index == INDEX_NONE)
    {
        return;
    }
    const FString Key = FString::Printf(TEXT("%d:%d"), static_cast<int32>(Kind), Index);
    if (Seen.Contains(Key))
    {
        for (FReferenceUse& Existing : Uses)
        {
            if (Existing.OwnerKind == Kind && Existing.OwnerIndex == Index)
            {
                for (const FString& Note : Notes)
                {
                    Existing.Notes.AddUnique(Note);
                }
                break;
            }
        }
        return;
    }
    Seen.Add(Key);
    Uses.Add({Kind, Index, Notes});
}

bool CollectStateLinksFromPropertyValue(
    const FProperty& Property,
    const void* ValueAddress,
    const FString& Path,
    const FGuid& StateId,
    TArray<FString>& OutPaths,
    int32& InOutVisitedValues)
{
    if (ValueAddress == nullptr || ++InOutVisitedValues > MaxRelationshipAnalysisObjects)
    {
        return false;
    }
    if (const FStructProperty* StructProperty = CastField<FStructProperty>(&Property))
    {
        if (StructProperty->Struct == FStateTreeStateLink::StaticStruct())
        {
            const FStateTreeStateLink* Link = static_cast<const FStateTreeStateLink*>(ValueAddress);
            if (Link->LinkType == EStateTreeTransitionType::GotoState && Link->ID == StateId)
            {
                OutPaths.Add(Path);
            }
            return true;
        }
        if (StructProperty->Struct == nullptr)
        {
            return true;
        }
        for (TFieldIterator<FProperty> It(StructProperty->Struct, EFieldIteratorFlags::IncludeSuper); It; ++It)
        {
            const FProperty* Child = *It;
            if (Child == nullptr)
            {
                continue;
            }
            for (int32 StaticIndex = 0; StaticIndex < Child->ArrayDim; ++StaticIndex)
            {
                const void* ChildValue = Child->ContainerPtrToValuePtr<void>(ValueAddress, StaticIndex);
                const FString ChildPath = Path + TEXT(".") + Child->GetName()
                    + (Child->ArrayDim > 1
                        ? FString::Printf(TEXT("[%d]"), StaticIndex)
                        : FString());
                if (!CollectStateLinksFromPropertyValue(
                        *Child, ChildValue, ChildPath, StateId, OutPaths, InOutVisitedValues))
                {
                    return false;
                }
            }
        }
        return true;
    }
    if (const FArrayProperty* ArrayProperty = CastField<FArrayProperty>(&Property))
    {
        FScriptArrayHelper Array(ArrayProperty, ValueAddress);
        for (int32 Index = 0; Index < Array.Num(); ++Index)
        {
            if (!CollectStateLinksFromPropertyValue(
                    *ArrayProperty->Inner,
                    Array.GetRawPtr(Index),
                    FString::Printf(TEXT("%s[%d]"), *Path, Index),
                    StateId,
                    OutPaths,
                    InOutVisitedValues))
            {
                return false;
            }
        }
        return true;
    }
    if (const FSetProperty* SetProperty = CastField<FSetProperty>(&Property))
    {
        FScriptSetHelper Set(SetProperty, ValueAddress);
        for (int32 Index = 0; Index < Set.GetMaxIndex(); ++Index)
        {
            if (Set.IsValidIndex(Index)
                && !CollectStateLinksFromPropertyValue(
                    *SetProperty->ElementProp,
                    Set.GetElementPtr(Index),
                    FString::Printf(TEXT("%s[%d]"), *Path, Index),
                    StateId,
                    OutPaths,
                    InOutVisitedValues))
            {
                return false;
            }
        }
        return true;
    }
    if (const FMapProperty* MapProperty = CastField<FMapProperty>(&Property))
    {
        FScriptMapHelper Map(MapProperty, ValueAddress);
        for (int32 Index = 0; Index < Map.GetMaxIndex(); ++Index)
        {
            if (!Map.IsValidIndex(Index))
            {
                continue;
            }
            if (!CollectStateLinksFromPropertyValue(
                    *MapProperty->KeyProp,
                    Map.GetKeyPtr(Index),
                    FString::Printf(TEXT("%s[%d].Key"), *Path, Index),
                    StateId,
                    OutPaths,
                    InOutVisitedValues)
                || !CollectStateLinksFromPropertyValue(
                    *MapProperty->ValueProp,
                    Map.GetValuePtr(Index),
                    FString::Printf(TEXT("%s[%d].Value"), *Path, Index),
                    StateId,
                    OutPaths,
                    InOutVisitedValues))
            {
                return false;
            }
        }
    }
    return true;
}

bool CollectStateLinkPaths(
    const UStruct* Struct,
    const void* Memory,
    const FString& Prefix,
    const FGuid& StateId,
    TArray<FString>& OutPaths,
    int32& InOutVisitedValues)
{
    if (Struct == nullptr || Memory == nullptr)
    {
        return true;
    }
    for (TFieldIterator<FProperty> It(Struct, EFieldIteratorFlags::IncludeSuper); It; ++It)
    {
        const FProperty* Property = *It;
        if (Property == nullptr)
        {
            continue;
        }
        for (int32 StaticIndex = 0; StaticIndex < Property->ArrayDim; ++StaticIndex)
        {
            const void* ValueAddress = Property->ContainerPtrToValuePtr<void>(Memory, StaticIndex);
            const FString Path = Prefix + Property->GetName()
                + (Property->ArrayDim > 1
                    ? FString::Printf(TEXT("[%d]"), StaticIndex)
                    : FString());
            if (!CollectStateLinksFromPropertyValue(
                    *Property, ValueAddress, Path, StateId, OutPaths, InOutVisitedValues))
            {
                return false;
            }
        }
    }
    return true;
}

bool EmitReferenceUse(
    FSalObjectBuilder& Builder,
    FReadContext& Context,
    FAuthoredStateIndex& Index,
    FUniqueNameAllocator& Aliases,
    const FReferenceUse& Use)
{
    if (!Context.ConsumeEmittedObject())
    {
        return false;
    }
    switch (Use.OwnerKind)
    {
    case ERelationshipOwnerKind::State:
        if (const FAuthoredStateEntry* Entry = Index.GetEntry(Use.OwnerIndex);
            Entry != nullptr && Entry->State != nullptr)
        {
            Builder.AddLocalBinding(
                Aliases.Allocate(Entry->State->Name.ToString(), TEXT("state")),
                StateValue(*Entry->State, EStateEmission::Compact, Index));
        }
        break;
    case ERelationshipOwnerKind::Node:
        if (const FAuthoredNodeEntry* Entry = Index.GetNodeEntry(Use.OwnerIndex);
            Entry != nullptr && Entry->Node != nullptr)
        {
            Builder.AddLocalBinding(
                Aliases.Allocate(Entry->Node->GetName().ToString(), TEXT("node")),
                NodeValue(*Entry->Node, Index));
        }
        break;
    case ERelationshipOwnerKind::Transition:
        if (const FAuthoredTransitionEntry* Entry = Index.GetTransitionEntry(Use.OwnerIndex);
            Entry != nullptr && Entry->Transition != nullptr)
        {
            Builder.AddLocalBinding(
                Aliases.Allocate(TEXT("transition"), TEXT("transition")),
                TransitionValue(*Entry->Transition, Index));
        }
        break;
    case ERelationshipOwnerKind::Parameter:
        if (const FAuthoredParameterEntry* Entry = Index.GetParameterEntry(Use.OwnerIndex);
            Entry != nullptr && Entry->Desc != nullptr)
        {
            Builder.AddLocalBinding(
                Aliases.Allocate(Entry->Desc->Name.ToString(), TEXT("parameter")),
                ParameterValue(*Entry, Index, false));
        }
        break;
    case ERelationshipOwnerKind::Context:
        if (const FAuthoredContextEntry* Entry = Index.GetContextEntry(Use.OwnerIndex);
            Entry != nullptr && Entry->Desc != nullptr)
        {
            Builder.AddLocalBinding(
                Aliases.Allocate(Entry->Desc->Name.ToString(), TEXT("context")),
                ContextValue(*Entry->Desc, *Entry, Index));
        }
        break;
    }
    for (const FString& Note : Use.Notes)
    {
        Builder.AddComment(Note);
    }
    return true;
}

TSharedPtr<FJsonObject> QueryReferences(
    const FSalQuery& Query,
    const FSalResolvedTarget& Target,
    UStateTree* StateTree,
    UStateTreeEditorData* EditorData,
    FAuthoredStateIndex& Index,
    FReadContext& Context)
{
    if (!Query.With.IsEmpty() || Query.Where.IsValid() || !Query.OrderBy.IsEmpty())
    {
        return QueryError(
            TEXT("capability.clause_unavailable"),
            TEXT("StateTree references accepts only an exact subject, local scope, and cursor page clauses."),
            TEXT("references"));
    }
    FString Scope;
    Query.Operation->TryGetStringField(TEXT("scope"), Scope);
    if (!Scope.IsEmpty() && Scope != TEXT("local"))
    {
        return QueryError(
            TEXT("capability.reference_unavailable"),
            TEXT("StateTree project references requires a dedicated zero-load index and is not available; no assets were loaded."),
            TEXT("references"),
            Scope);
    }
    const TSharedPtr<FJsonObject>* SubjectRef = nullptr;
    if (!Query.Operation->TryGetObjectField(TEXT("target"), SubjectRef)
        || SubjectRef == nullptr
        || !(*SubjectRef).IsValid())
    {
        return QueryError(
            TEXT("validation.invalid_target"),
            TEXT("StateTree references requires one exact target reference."),
            TEXT("references"));
    }
    FReferenceSubject Subject;
    FString Code;
    FString Message;
    if (!ResolveReferenceSubject(
            *StateTree, *EditorData, Index, *SubjectRef, Subject, Code, Message))
    {
        return QueryError(Code, Message, TEXT("references"), Subject.StableId);
    }

    FSalPage Page;
    if (!DecodeStateTreePage(Target, StateTree, Query, Page))
    {
        return QueryError(
            TEXT("validation.invalid_cursor"),
            TEXT("StateTree reference cursor does not belong to this exact subject, target, authored revision, or page limit."),
            TEXT("references"),
            Query.PageAfter);
    }

    FStateTreeRelationshipIndex Relationships(*EditorData, Index, Context);
    if (!Relationships.IsComplete())
    {
        return QueryError(
            TEXT("validation.reference_scan_incomplete"),
            TEXT("The StateTree relationship scan exceeded its hard budget; references emitted no partial result."),
            TEXT("references"),
            Subject.StableId);
    }

    TArray<FReferenceUse> Uses;
    TSet<FString> Seen;
    int32 StateLinkVisitedValues = 0;
    bool bStateLinkScanComplete = true;
    if (Subject.OwnerKind == ERelationshipOwnerKind::State && Subject.Path.IsEmpty())
    {
        FGuid StateId;
        FGuid::Parse(Subject.StableId, StateId);
        for (int32 StateIndex = 0; StateIndex < Index.GetEntries().Num(); ++StateIndex)
        {
            const FAuthoredStateEntry* StateEntry = Index.GetEntry(StateIndex);
            if (StateEntry == nullptr || StateEntry->State == nullptr)
            {
                continue;
            }
            const UStateTreeState& AuthoredState = *StateEntry->State;
            if (AuthoredState.LinkedSubtree.LinkType == EStateTreeTransitionType::GotoState
                && AuthoredState.LinkedSubtree.ID == StateId)
            {
                AddReferenceUse(
                    Uses,
                    Seen,
                    ERelationshipOwnerKind::State,
                    StateIndex,
                    {FString::Printf(
                        TEXT("reference member: state@%s.LinkedSubtree"),
                        *GuidText(AuthoredState.ID))});
            }
        }
        for (int32 TransitionIndex = 0; TransitionIndex < Index.GetTransitionEntries().Num(); ++TransitionIndex)
        {
            const FAuthoredTransitionEntry* TransitionEntry = Index.GetTransitionEntry(TransitionIndex);
            if (TransitionEntry != nullptr
                && TransitionEntry->Transition != nullptr
                && TransitionEntry->Transition->State.LinkType == EStateTreeTransitionType::GotoState
                && TransitionEntry->Transition->State.ID == StateId)
            {
                AddReferenceUse(
                    Uses,
                    Seen,
                    ERelationshipOwnerKind::Transition,
                    TransitionIndex,
                    {FString::Printf(
                        TEXT("reference member: transition@%s.State"),
                        *GuidText(TransitionEntry->Transition->ID))});
            }
        }
        for (int32 NodeIndex = 0; NodeIndex < Index.GetNodeEntries().Num(); ++NodeIndex)
        {
            const FAuthoredNodeEntry* NodeEntry = Index.GetNodeEntry(NodeIndex);
            if (NodeEntry == nullptr || NodeEntry->Node == nullptr)
            {
                continue;
            }
            const TStructView<FStateTreeNodeBase> NodeView = NodeEntry->Node->GetNode();
            const FStateTreeDataView InstanceView = NodeEntry->Node->GetInstance();
            TArray<FString> Paths;
            if (NodeView.IsValid())
            {
                bStateLinkScanComplete = CollectStateLinkPaths(
                    NodeView.GetScriptStruct(),
                    NodeView.GetMemory(),
                    TEXT("Node."),
                    StateId,
                    Paths,
                    StateLinkVisitedValues);
            }
            if (bStateLinkScanComplete && InstanceView.IsValid())
            {
                bStateLinkScanComplete = CollectStateLinkPaths(
                    InstanceView.GetStruct(),
                    InstanceView.GetMemory(),
                    TEXT("Instance."),
                    StateId,
                    Paths,
                    StateLinkVisitedValues);
            }
            if (!bStateLinkScanComplete)
            {
                break;
            }
            if (!Paths.IsEmpty())
            {
                TArray<FString> Notes;
                for (const FString& Path : Paths)
                {
                    Notes.Add(FString::Printf(
                        TEXT("reference member: node@%s.%s"),
                        *GuidText(NodeEntry->Node->ID),
                        *Path));
                }
                AddReferenceUse(
                    Uses, Seen, ERelationshipOwnerKind::Node, NodeIndex, Notes);
            }
        }
        for (int32 ParameterIndex = 0;
            bStateLinkScanComplete && ParameterIndex < Index.GetParameterEntries().Num();
            ++ParameterIndex)
        {
            const FAuthoredParameterEntry* ParameterEntry = Index.GetParameterEntry(ParameterIndex);
            if (ParameterEntry == nullptr
                || ParameterEntry->Bag == nullptr
                || ParameterEntry->Desc == nullptr
                || ParameterEntry->Desc->CachedProperty == nullptr)
            {
                continue;
            }
            const FConstStructView BagValue = ParameterEntry->Bag->GetValue();
            if (!BagValue.IsValid() || BagValue.GetMemory() == nullptr)
            {
                continue;
            }
            const FProperty& Property = *ParameterEntry->Desc->CachedProperty;
            TArray<FString> Paths;
            for (int32 StaticIndex = 0; StaticIndex < Property.ArrayDim; ++StaticIndex)
            {
                const void* ValueAddress = Property.ContainerPtrToValuePtr<void>(
                    BagValue.GetMemory(), StaticIndex);
                const FString ValuePath = TEXT("Value")
                    + (Property.ArrayDim > 1
                        ? FString::Printf(TEXT("[%d]"), StaticIndex)
                        : FString());
                if (!CollectStateLinksFromPropertyValue(
                        Property,
                        ValueAddress,
                        ValuePath,
                        StateId,
                        Paths,
                        StateLinkVisitedValues))
                {
                    bStateLinkScanComplete = false;
                    break;
                }
            }
            if (!Paths.IsEmpty())
            {
                TArray<FString> Notes;
                const FString ParameterId = ParameterIdentityText(
                    ParameterEntry->ContainerId,
                    ParameterEntry->Desc->ID);
                for (const FString& Path : Paths)
                {
                    Notes.Add(FString::Printf(
                        TEXT("reference member: parameter@%s.%s"),
                        *ParameterId,
                        *Path));
                }
                AddReferenceUse(
                    Uses,
                    Seen,
                    ERelationshipOwnerKind::Parameter,
                    ParameterIndex,
                    Notes);
            }
        }
        if (!bStateLinkScanComplete)
        {
            return QueryError(
                TEXT("validation.reference_scan_incomplete"),
                TEXT("The StateTree embedded State-link scan exceeded its hard budget; references emitted no partial result."),
                TEXT("references"),
                Subject.StableId);
        }
    }

    for (const FStateTreeRelationship& Relationship : Relationships.GetRelationships())
    {
        if (EndpointMatchesReference(Relationship.From, Subject))
        {
            TArray<FString> Notes = Relationship.Notes;
            Notes.Add(TEXT("reference member: ") + RelationshipEndpointText(Relationship.To));
            Notes.Append(Relationship.To.Notes);
            if (Relationship.bAutomaticContext)
            {
                Notes.Add(TEXT("automatic Context: derived by UStateTreeEditorData::FindContextData; no authored Binding record"));
            }
            AddReferenceUse(
                Uses, Seen, Relationship.To.OwnerKind, Relationship.To.OwnerIndex, Notes);
        }
        if (EndpointMatchesReference(Relationship.To, Subject))
        {
            TArray<FString> Notes = Relationship.Notes;
            Notes.Add(TEXT("reference member: ") + RelationshipEndpointText(Relationship.From));
            Notes.Append(Relationship.From.Notes);
            if (Relationship.bAutomaticContext)
            {
                Notes.Add(TEXT("automatic Context: derived by UStateTreeEditorData::FindContextData; no authored Binding record"));
            }
            AddReferenceUse(
                Uses, Seen, Relationship.From.OwnerKind, Relationship.From.OwnerIndex, Notes);
        }
    }
    if (Page.Offset > Uses.Num())
    {
        return QueryError(
            TEXT("validation.invalid_cursor"),
            TEXT("StateTree reference cursor is outside the current factual result set."),
            TEXT("references"),
            Query.PageAfter);
    }
    const int32 End = FMath::Min(Uses.Num(), Page.Offset + Page.Limit);
    FSalObjectBuilder Builder;
    Builder.AddLocalBinding(Query.Alias, StateTreeAssetIdentityValue(Target, StateTree));
    FUniqueNameAllocator Aliases;
    Aliases.Reserve(Query.Alias);
    for (int32 UseIndex = Page.Offset; UseIndex < End; ++UseIndex)
    {
        if (!EmitReferenceUse(Builder, Context, Index, Aliases, Uses[UseIndex]))
        {
            break;
        }
    }
    if (Uses.IsEmpty())
    {
        Builder.AddComment(TEXT("no references"));
    }
    Index.AddComments(Builder);
    TArray<TSharedPtr<FJsonObject>> Diagnostics;
    Index.AddDiagnostics(Diagnostics);
    return MakeStateTreePageResult(
        Builder.BuildResult(Diagnostics),
        Target,
        StateTree,
        Query,
        Page,
        End,
        End < Uses.Num());
}

bool PaletteDestinationUsesCurrentTarget(
    const TSharedPtr<FJsonObject>& Destination,
    const FString& TargetAlias)
{
    if (!Destination.IsValid())
    {
        return false;
    }
    FString Kind;
    if (!Destination->TryGetStringField(TEXT("kind"), Kind))
    {
        return false;
    }
    const TSharedPtr<FJsonObject>* Owner = &Destination;
    if (Kind == TEXT("member"))
    {
        if (!Destination->TryGetObjectField(TEXT("object"), Owner)
            || Owner == nullptr
            || !(*Owner).IsValid()
            || !(*Owner)->TryGetStringField(TEXT("kind"), Kind))
        {
            return false;
        }
    }
    if (Kind != TEXT("local"))
    {
        return true;
    }
    FString Name;
    return (*Owner)->TryGetStringField(TEXT("name"), Name)
        && Name == TargetAlias;
}

bool PaletteSchemaText(
    const StateTreePalette::FEntry& Entry,
    const StateTreePalette::FDestination& Destination,
    const UStateTreeEditorData& EditorData,
    FString& OutText,
    FString& OutError)
{
    StateTreeSchema::FExactSchemaTextBuilder Builder;
    const auto Finish = [&]()
    {
        return Builder.Finish(OutText, OutError);
    };
    const FString NativeTypeText = Entry.ConstructorKind
            == StateTreePalette::EConstructorKind::Parameter
        ? Entry.ParameterType
        : Entry.NativeType.IsNull()
            ? TEXT("<destination-defined>")
            : Entry.NativeType.ToString();
    if (!Builder.Append(FString::Printf(
            TEXT("palette schema:\n  constructor: %s\n  destination: %s\n  native type: %s\n  palette: %s"),
            StateTreePalette::ConstructorName(Entry.ConstructorKind),
            StateTreePalette::DestinationRoleName(Destination.Role),
            *NativeTypeText,
            *Entry.Id)))
    {
        return Finish();
    }
    using FPaletteWriteFilter = TFunction<bool(const FProperty&)>;
    const auto AppendStructFields = [&](
        const UStruct* Struct,
        const FString& Prefix,
        const bool bBindingSurface,
        const bool bNodeTemplate,
        const FPaletteWriteFilter& WriteFilter = FPaletteWriteFilter())
    {
        if (Struct == nullptr)
        {
            return true;
        }
        for (TFieldIterator<FProperty> It(Struct, EFieldIteratorFlags::IncludeSuper); It; ++It)
        {
            const FProperty* Property = *It;
            if (Property == nullptr
                || Property->HasAnyPropertyFlags(CPF_Transient | CPF_DuplicateTransient))
            {
                continue;
            }
            const bool bWritable = Property->HasAnyPropertyFlags(CPF_Edit)
                && !Property->HasAnyPropertyFlags(CPF_EditConst | CPF_DisableEditOnInstance)
                && (!WriteFilter || WriteFilter(*Property));
            const EStateTreePropertyUsage Usage = UE::StateTree::GetUsageFromMetaData(Property);
            FString Capabilities = bWritable ? TEXT("read/write/reset") : TEXT("read-only");
            const FStructProperty* StructProperty = CastField<FStructProperty>(Property);
            const bool bPropertyRef = UE::StateTree::PropertyRefHelpers::IsPropertyRef(*Property);
            const bool bListener = StructProperty != nullptr
                && StructProperty->Struct != nullptr
                && (StructProperty->Struct->IsChildOf(FStateTreeDelegateListener::StaticStruct())
                    || StructProperty->Struct->IsChildOf(FStateTreeTransitionDelegateListener::StaticStruct()));
            const bool bDispatcher = StructProperty != nullptr
                && StructProperty->Struct != nullptr
                && StructProperty->Struct->IsChildOf(FStateTreeDelegateDispatcher::StaticStruct());
            if (bBindingSurface
                && !bDispatcher
                && (!bNodeTemplate || bPropertyRef || bListener)
                && (bPropertyRef
                    || bListener
                    || Usage == EStateTreePropertyUsage::Input
                    || Usage == EStateTreePropertyUsage::Context
                    || Usage == EStateTreePropertyUsage::Parameter))
            {
                Capabilities += TEXT("; binding target");
            }
            if (bBindingSurface
                && !bListener
                && (bDispatcher
                    || Usage == EStateTreePropertyUsage::Output
                    || Usage == EStateTreePropertyUsage::Parameter
                    || Usage == EStateTreePropertyUsage::Context))
            {
                Capabilities += TEXT("; binding source");
            }
            if (!Builder.Append(
                    FString::Printf(
                        TEXT("\n  %s%s: %s; %s"),
                        *Prefix,
                        *Property->GetName(),
                        *NativePropertyTypeText(Property),
                        *Capabilities),
                    1))
            {
                return false;
            }
        }
        return true;
    };
    if (Entry.ConstructorKind == StateTreePalette::EConstructorKind::State)
    {
        if (!AppendStructFields(
                UStateTreeState::StaticClass(),
                FString(),
                false,
                false,
                [&](const FProperty& Property)
                {
                    const FName Name = Property.GetFName();
                    const bool bStructural = Name == GET_MEMBER_NAME_CHECKED(UStateTreeState, Children)
                        || Name == GET_MEMBER_NAME_CHECKED(UStateTreeState, EnterConditions)
                        || Name == GET_MEMBER_NAME_CHECKED(UStateTreeState, Tasks)
                        || Name == GET_MEMBER_NAME_CHECKED(UStateTreeState, SingleTask)
                        || Name == GET_MEMBER_NAME_CHECKED(UStateTreeState, Considerations)
                        || Name == GET_MEMBER_NAME_CHECKED(UStateTreeState, Transitions)
                        || Name == GET_MEMBER_NAME_CHECKED(UStateTreeState, Parameters)
                        || Name == GET_MEMBER_NAME_CHECKED(UStateTreeState, ID)
                        || Name == GET_MEMBER_NAME_CHECKED(UStateTreeState, Parent);
                    if (bStructural
                        || (Entry.bFixedStateType
                            && (Name == GET_MEMBER_NAME_CHECKED(UStateTreeState, Type)
                                || Name == GET_MEMBER_NAME_CHECKED(UStateTreeState, SelectionBehavior)))
                        || (Entry.LinkedSubtreeId.IsValid()
                            && Name == GET_MEMBER_NAME_CHECKED(UStateTreeState, LinkedSubtree))
                        || (!Entry.bFixedStateType
                            && (Name == GET_MEMBER_NAME_CHECKED(UStateTreeState, LinkedSubtree)
                                || Name == GET_MEMBER_NAME_CHECKED(UStateTreeState, LinkedAsset))))
                    {
                        return false;
                    }
                    if (EditorData.Schema == nullptr)
                    {
                        return Name != GET_MEMBER_NAME_CHECKED(UStateTreeState, TasksCompletion)
                            && Name != GET_MEMBER_NAME_CHECKED(UStateTreeState, CustomTickRate)
                            && Name != GET_MEMBER_NAME_CHECKED(UStateTreeState, bHasCustomTickRate);
                    }
                    if (Name == GET_MEMBER_NAME_CHECKED(UStateTreeState, TasksCompletion))
                    {
                        return EditorData.Schema->AllowTasksCompletion();
                    }
                    if (Name == GET_MEMBER_NAME_CHECKED(UStateTreeState, CustomTickRate)
                        || Name == GET_MEMBER_NAME_CHECKED(UStateTreeState, bHasCustomTickRate))
                    {
                        return EditorData.Schema->IsScheduledTickAllowed();
                    }
                    return true;
                }))
        {
            return Finish();
        }
        if (Entry.bFixedStateType)
        {
            FString FixedSelection = TEXT("TryEnterState");
            if (const UStateTreeState* Defaults = GetDefault<UStateTreeState>())
            {
                if (const UEnum* Enum = StaticEnum<EStateTreeStateSelectionBehavior>())
                {
                    const FString NativeSelection = Enum->GetNameStringByValue(
                        static_cast<int64>(Defaults->SelectionBehavior));
                    if (!NativeSelection.IsEmpty())
                    {
                        FixedSelection = NativeSelection;
                    }
                }
            }
            if (!Builder.Append(FString::Printf(
                    TEXT("\nconstraints:\n  Type: fixed to %s; the copied constructor must preserve this Palette capability\n  SelectionBehavior: fixed to %s for this fixed State capability"),
                    *Entry.StateType,
                    *FixedSelection)))
            {
                return Finish();
            }
            if (Entry.LinkedSubtreeId.IsValid())
            {
                if (!Builder.Append(FString::Printf(
                        TEXT("\n  LinkedSubtree: fixed to state@%s; the copied constructor must preserve this exact destination-bound target"),
                        *GuidText(Entry.LinkedSubtreeId))))
                {
                    return Finish();
                }
            }
            else if (Entry.StateType == TEXT("LinkedAsset"))
            {
                if (!Builder.Append(TEXT("\n  LinkedAsset: nullable native UStateTree reference; it is not fixed or discovered by Palette")))
                {
                    return Finish();
                }
            }
        }
        else if (EditorData.Schema != nullptr)
        {
            FString AllowedTypes;
            if (const UEnum* Enum = StaticEnum<EStateTreeStateType>())
            {
                for (int32 EnumIndex = 0; EnumIndex < Enum->NumEnums(); ++EnumIndex)
                {
                    const int64 Value = Enum->GetValueByIndex(EnumIndex);
                    const FString Name = Enum->GetNameStringByIndex(EnumIndex);
                    if (Value == INDEX_NONE
                        || Name.EndsWith(TEXT("MAX"))
                        || (Value != static_cast<int64>(EStateTreeStateType::State)
                            && Value != static_cast<int64>(EStateTreeStateType::Group)
                            && Value != static_cast<int64>(EStateTreeStateType::Subtree))
                        || !EditorData.Schema->IsStateTypeAllowed(
                            static_cast<EStateTreeStateType>(Value)))
                    {
                        continue;
                    }
                    AllowedTypes += (AllowedTypes.IsEmpty() ? FString() : TEXT(", ")) + Name;
                }
            }
            FString AllowedSelection;
            if (const UEnum* Enum = StaticEnum<EStateTreeStateSelectionBehavior>())
            {
                for (int32 EnumIndex = 0; EnumIndex < Enum->NumEnums(); ++EnumIndex)
                {
                    const int64 Value = Enum->GetValueByIndex(EnumIndex);
                    const FString Name = Enum->GetNameStringByIndex(EnumIndex);
                    if (Value == INDEX_NONE
                        || Name.EndsWith(TEXT("MAX"))
                        || !EditorData.Schema->IsStateSelectionAllowed(
                            static_cast<EStateTreeStateSelectionBehavior>(Value)))
                    {
                        continue;
                    }
                    AllowedSelection += (AllowedSelection.IsEmpty() ? FString() : TEXT(", ")) + Name;
                }
            }
            if (!Builder.Append(
                    FString(TEXT("\nconstraints:"))
                    + TEXT("\n  UStateTreeSchema::IsStateTypeAllowed: ") + AllowedTypes
                    + TEXT("\n  UStateTreeSchema::IsStateSelectionAllowed: ") + AllowedSelection
                    + TEXT("\n  LinkedSubtree and LinkedAsset: unavailable on the ordinary State/Group/Subtree capability; use their fixed Palette entries")))
            {
                return Finish();
            }
        }
    }
    else if (Entry.ConstructorKind == StateTreePalette::EConstructorKind::Transition)
    {
        if (!AppendStructFields(
                FStateTreeTransition::StaticStruct(),
                FString(),
                false,
                false,
                [](const FProperty& Property)
                {
                    const FName Name = Property.GetFName();
                    return Name != GET_MEMBER_NAME_CHECKED(FStateTreeTransition, Conditions)
                        && Name != GET_MEMBER_NAME_CHECKED(FStateTreeTransition, ID)
                        && Name != GET_MEMBER_NAME_CHECKED(FStateTreeTransition, DelegateListener);
                })
            || !Builder.Append(
                TEXT("\nconstraints:")
                TEXT("\n  RequiredEvent Binding source is active when Trigger == OnEvent")
                TEXT("\n  DelegateListener Binding target is active when Trigger == OnDelegate")))
        {
            return Finish();
        }
    }
    else if (Entry.ConstructorKind == StateTreePalette::EConstructorKind::Node)
    {
        if (!AppendStructFields(Entry.NodeStruct, TEXT("Node."), true, true)
            || !AppendStructFields(Entry.InstanceDataType, TEXT("Instance."), true, false))
        {
            return Finish();
        }
    }
    if (Entry.ConstructorKind == StateTreePalette::EConstructorKind::Parameter)
    {
        if (!Builder.Append(
                TEXT("\n  required fields: Name, type (UE native FProperty text)\n  optional fields: Value, MetaData"),
                4))
        {
            return Finish();
        }
    }
    if (Entry.PropertyFunctionOutput != nullptr)
    {
        if (!Builder.Append(FString::Printf(
                TEXT("\n  property function output: Instance.%s"),
                *Entry.PropertyFunctionOutput->GetName())))
        {
            return Finish();
        }
    }
    Builder.Append(TEXT("\n  constraint: the palette id is valid only for this exact destination and is revalidated before mutation"));
    return Finish();
}

TSharedPtr<FJsonObject> QueryPaletteEntries(
    const FSalQuery& Query,
    const FSalResolvedTarget& Target,
    UStateTree* StateTree,
    UStateTreeEditorData* EditorData)
{
    if (HasUnsupportedCollectionClauses(Query))
    {
        return QueryError(
            TEXT("capability.clause_unavailable"),
            TEXT("StateTree palette entries accepts only optional text search, exact destination, and cursor page clauses."),
            TEXT("palette_entries"));
    }
    const TSharedPtr<FJsonObject>* DestinationRef = nullptr;
    if (!Query.Operation->TryGetObjectField(TEXT("to"), DestinationRef)
        || DestinationRef == nullptr
        || !(*DestinationRef).IsValid())
    {
        return QueryError(
            TEXT("validation.palette_context_invalid"),
            TEXT("StateTree palette entries requires one exact destination after to."),
            TEXT("palette_entries"));
    }
    if (!PaletteDestinationUsesCurrentTarget(*DestinationRef, Query.Alias))
    {
        return QueryError(
            TEXT("validation.palette_context_invalid"),
            TEXT("A local StateTree Palette destination must name the currently bound target alias."),
            TEXT("palette_entries"));
    }
    StateTreePalette::FDestination Destination;
    FString Message;
    if (!StateTreePalette::ResolveDestination(
            *StateTree, *EditorData, *DestinationRef, Destination, Message))
    {
        return QueryError(
            TEXT("validation.palette_context_invalid"),
            Message,
            TEXT("palette_entries"));
    }
    FSalPage Page;
    if (!DecodeStateTreePage(Target, StateTree, Query, Page))
    {
        return QueryError(
            TEXT("validation.invalid_cursor"),
            TEXT("StateTree Palette cursor does not belong to this destination, target, authored revision, search, or page limit."),
            TEXT("palette_entries"),
            Query.PageAfter);
    }
    FString SearchText;
    Query.Operation->TryGetStringField(TEXT("text"), SearchText);
    StateTreePalette::FPage PalettePage;
    if (!StateTreePalette::DiscoverEntries(
            *StateTree,
            *EditorData,
            Destination,
            SearchText,
            Page.Offset,
            Page.Limit,
            PalettePage,
            Message))
    {
        return QueryError(
            TEXT("validation.palette_context_invalid"),
            Message,
            TEXT("palette_entries"));
    }

    FSalObjectBuilder Builder;
    Builder.AddLocalBinding(Query.Alias, StateTreeAssetIdentityValue(Target, StateTree));
    FUniqueNameAllocator Aliases;
    Aliases.Reserve(Query.Alias);
    for (const StateTreePalette::FEntry& Entry : PalettePage.Entries)
    {
        Builder.AddLocalBinding(
            Aliases.Allocate(Entry.DisplayName, StateTreePalette::ConstructorName(Entry.ConstructorKind)),
            StateTreePalette::MakeConstructor(Entry));
    }
    if (PalettePage.Entries.IsEmpty())
    {
        Builder.AddComment(TEXT("no palette matches in this bounded discovery page"));
    }
    return MakeStateTreePageResult(
        Builder.BuildResult(),
        Target,
        StateTree,
        Query,
        Page,
        PalettePage.NextOffset,
        PalettePage.NextOffset != INDEX_NONE);
}

TSharedPtr<FJsonObject> QueryPalette(
    const FSalQuery& Query,
    const FSalResolvedTarget& Target,
    UStateTree* StateTree,
    UStateTreeEditorData* EditorData)
{
    if (HasUnsupportedExactClauses(Query))
    {
        return QueryError(
            TEXT("capability.clause_unavailable"),
            TEXT("Exact StateTree Palette Query accepts only optional with schema."),
            TEXT("palette"));
    }
    if (HasUnsupportedExactDetail(Query))
    {
        return QueryError(
            TEXT("capability.detail_unavailable"),
            TEXT("Exact StateTree Palette Query supports only with schema."),
            TEXT("palette"),
            Query.With[0]);
    }
    FString PaletteId;
    const TSharedPtr<FJsonObject>* DestinationRef = nullptr;
    if (!Query.Operation->TryGetStringField(TEXT("id"), PaletteId)
        || PaletteId.IsEmpty()
        || !Query.Operation->TryGetObjectField(TEXT("to"), DestinationRef)
        || DestinationRef == nullptr
        || !(*DestinationRef).IsValid())
    {
        return QueryError(
            TEXT("validation.palette_context_invalid"),
            TEXT("Exact StateTree Palette Query requires palette @id and one exact destination after to."),
            TEXT("palette"),
            PaletteId);
    }
    if (!PaletteDestinationUsesCurrentTarget(*DestinationRef, Query.Alias))
    {
        return QueryError(
            TEXT("validation.palette_context_invalid"),
            TEXT("A local StateTree Palette destination must name the currently bound target alias."),
            TEXT("palette"),
            PaletteId);
    }
    StateTreePalette::FDestination Destination;
    FString Message;
    if (!StateTreePalette::ResolveDestination(
            *StateTree, *EditorData, *DestinationRef, Destination, Message))
    {
        return QueryError(
            TEXT("validation.palette_context_invalid"),
            Message,
            TEXT("palette"),
            PaletteId);
    }
    StateTreePalette::FEntry Entry;
    if (!StateTreePalette::ResolveEntry(
            *StateTree, *EditorData, Destination, PaletteId, Entry, Message))
    {
        return QueryError(
            TEXT("resolution.palette_not_spawnable"),
            Message,
            TEXT("palette"),
            PaletteId);
    }
    FSalObjectBuilder Builder;
    Builder.AddLocalBinding(Query.Alias, StateTreeAssetIdentityValue(Target, StateTree));
    FUniqueNameAllocator Aliases;
    Aliases.Reserve(Query.Alias);
    Builder.AddLocalBinding(
        Aliases.Allocate(
            Entry.DisplayName,
            StateTreePalette::ConstructorName(Entry.ConstructorKind)),
        StateTreePalette::MakeConstructor(Entry));
    if (WantsExactSchema(Query))
    {
        FString SchemaText;
        FString SchemaError;
        if (!PaletteSchemaText(
                Entry,
                Destination,
                *EditorData,
                SchemaText,
                SchemaError))
        {
            return QueryError(
                TEXT("validation.result_too_large"),
                SchemaError,
                TEXT("palette"),
                PaletteId);
        }
        Builder.AddComment(SchemaText);
    }
    return Builder.BuildResult();
}
}

TSharedPtr<FJsonObject> FSalStateTreeInterface::Query(
    const FSalQuery& Query,
    const FSalResolvedTarget& Target)
{
    using namespace StateTreeRead;

    UStateTree* StateTree = ResolvedStateTree(Target);
    UStateTreeEditorData* EditorData = AuthoredData(StateTree);
    if (StateTree == nullptr)
    {
        return QueryError(
            TEXT("capability.interface_unavailable"),
            TEXT("The state_tree interface requires an exact UStateTree asset target."),
            FString(),
            Target.AssetPath);
    }
    if (EditorData == nullptr)
    {
        return QueryError(
            TEXT("validation.invalid_target"),
            TEXT("The resolved UStateTree has no compatible authored UStateTreeEditorData."),
            FString(),
            Target.AssetPath);
    }

    FString Operation;
    if (!Query.Operation.IsValid() || !Query.Operation->TryGetStringField(TEXT("kind"), Operation))
    {
        return QueryError(
            TEXT("capability.unsupported_query_operation"),
            TEXT("StateTree Query has no supported primary operation."));
    }
    if (Operation == TEXT("target"))
    {
        FReadContext Context;
        FAuthoredStateIndex Index(*EditorData, Context);
        return QueryTarget(Query, Target, StateTree, EditorData, Index, Context);
    }
    if (Operation == TEXT("summary"))
    {
        FReadContext Context;
        FAuthoredStateIndex Index(*EditorData, Context);
        return QuerySummary(Query, Target, StateTree, EditorData, Index, Context);
    }
    if (Operation == TEXT("tree"))
    {
        FReadContext Context;
        FAuthoredStateIndex Index(*EditorData, Context);
        return QueryTree(Query, Target, StateTree, Index, Context);
    }
    if (Operation == TEXT("state"))
    {
        FReadContext Context;
        FAuthoredStateIndex Index(*EditorData, Context);
        return QueryState(Query, Target, StateTree, EditorData, Index, Context);
    }
    if (Operation == TEXT("states"))
    {
        FReadContext Context;
        FAuthoredStateIndex Index(*EditorData, Context);
        return QueryStates(Query, Target, StateTree, Index, Context);
    }
    if (Operation == TEXT("nodes"))
    {
        FReadContext Context;
        FAuthoredStateIndex Index(*EditorData, Context);
        return QueryNodes(Query, Target, StateTree, Index, Context);
    }
    if (Operation == TEXT("parameters"))
    {
        FReadContext Context;
        FAuthoredStateIndex Index(*EditorData, Context);
        return QueryParameters(Query, Target, StateTree, Index, Context);
    }
    if (Operation == TEXT("node"))
    {
        FReadContext Context;
        FAuthoredStateIndex Index(*EditorData, Context);
        return QueryNode(Query, Target, StateTree, EditorData, Index, Context);
    }
    if (Operation == TEXT("transition"))
    {
        FReadContext Context;
        FAuthoredStateIndex Index(*EditorData, Context);
        return QueryTransition(Query, Target, StateTree, EditorData, Index, Context);
    }
    if (Operation == TEXT("parameter"))
    {
        FReadContext Context;
        FAuthoredStateIndex Index(*EditorData, Context);
        return QueryParameter(Query, Target, StateTree, EditorData, Index, Context);
    }
    if (Operation == TEXT("object"))
    {
        FReadContext Context;
        FAuthoredStateIndex Index(*EditorData, Context);
        return QueryContextObject(Query, Target, StateTree, EditorData, Index, Context);
    }
    if (Operation == TEXT("references"))
    {
        FReadContext Context;
        FAuthoredStateIndex Index(*EditorData, Context);
        return QueryReferences(Query, Target, StateTree, EditorData, Index, Context);
    }
    if (Operation == TEXT("palette_entries"))
    {
        return QueryPaletteEntries(Query, Target, StateTree, EditorData);
    }
    if (Operation == TEXT("palette"))
    {
        return QueryPalette(Query, Target, StateTree, EditorData);
    }
    return QueryError(
        TEXT("capability.unsupported_query_operation"),
        FString::Printf(TEXT("StateTree Query operation is not active: %s."), *Operation),
        Operation,
        FString(),
        {TEXT("target"), TEXT("summary"), TEXT("tree"), TEXT("states"), TEXT("nodes"), TEXT("parameters"), TEXT("state"), TEXT("node"), TEXT("transition"), TEXT("parameter"), TEXT("object"), TEXT("references"), TEXT("palette_entries"), TEXT("palette")});
}
}
