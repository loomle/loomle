// Copyright 2026 Loomle contributors.

#include "SalStateTreePalette.h"
#include "SalStateTreeSchema.h"

#include "../SalObjectBuilder.h"

#include "Blueprint/StateTreeConditionBlueprintBase.h"
#include "Blueprint/StateTreeConsiderationBlueprintBase.h"
#include "Blueprint/StateTreeEvaluatorBlueprintBase.h"
#include "Blueprint/StateTreeTaskBlueprintBase.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Misc/SecureHash.h"
#include "StateTree.h"
#include "StateTreeConditionBase.h"
#include "StateTreeConsiderationBase.h"
#include "StateTreeEditorData.h"
#include "StateTreeEditorModule.h"
#include "StateTreeEvaluatorBase.h"
#include "StateTreeNodeBase.h"
#include "StateTreeNodeClassCache.h"
#include "StateTreePropertyFunctionBase.h"
#include "PropertyBindingTypes.h"
#include "StateTreePropertyBindings.h"
#include "StateTreeSchema.h"
#include "StateTreeState.h"
#include "StateTreeTaskBase.h"
#include "StructUtils/InstancedStruct.h"
#include "StructUtils/PropertyBag.h"
#include "UObject/UnrealType.h"

namespace Loomle::Sal::StateTreePalette
{
namespace
{
constexpr const TCHAR* NodePalettePrefix = TEXT("state_tree.node.");
constexpr const TCHAR* StatePaletteId = TEXT("state_tree.state");
constexpr const TCHAR* LinkedAssetStatePaletteId = TEXT("state_tree.state.linked_asset");
constexpr const TCHAR* LinkedStatePalettePrefix = TEXT("state_tree.state.linked.");
constexpr const TCHAR* TransitionPaletteId = TEXT("state_tree.transition");
constexpr const TCHAR* ParameterPaletteId = TEXT("state_tree.parameter");

struct FRawNodeCandidate
{
    FTopLevelAssetPath Path;
    TSharedPtr<FStateTreeNodeClassData> Data;
    bool bBlueprint = false;
};

bool ParseGuid(const FString& Text, FGuid& OutGuid)
{
    return FGuid::Parse(Text, OutGuid) && OutGuid.IsValid();
}

bool ReadMemberReference(
    const TSharedPtr<FJsonObject>& Reference,
    FString& OutOwnerKind,
    FString& OutOwnerId,
    TArray<FString>& OutPath)
{
    OutOwnerKind.Reset();
    OutOwnerId.Reset();
    OutPath.Reset();

    FString Kind;
    const TSharedPtr<FJsonObject>* Owner = nullptr;
    const TArray<TSharedPtr<FJsonValue>>* Segments = nullptr;
    if (!Reference.IsValid()
        || !Reference->TryGetStringField(TEXT("kind"), Kind)
        || Kind != TEXT("member")
        || !Reference->TryGetObjectField(TEXT("object"), Owner)
        || Owner == nullptr
        || !(*Owner).IsValid()
        || !Reference->TryGetArrayField(TEXT("path"), Segments)
        || Segments == nullptr
        || Segments->IsEmpty()
        || !(*Owner)->TryGetStringField(TEXT("kind"), OutOwnerKind))
    {
        return false;
    }
    (*Owner)->TryGetStringField(TEXT("id"), OutOwnerId);
    if (OutOwnerKind == TEXT("local"))
    {
        (*Owner)->TryGetStringField(TEXT("name"), OutOwnerId);
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
    return true;
}

void VisitStates(
    const UStateTreeEditorData& EditorData,
    TFunctionRef<bool(const UStateTreeState&)> Visitor)
{
    TArray<const UStateTreeState*> Pending;
    for (int32 Index = EditorData.SubTrees.Num() - 1; Index >= 0; --Index)
    {
        if (EditorData.SubTrees[Index] != nullptr)
        {
            Pending.Add(EditorData.SubTrees[Index]);
        }
    }
    TSet<const UStateTreeState*> Visited;
    while (!Pending.IsEmpty())
    {
        const UStateTreeState* State = Pending.Pop(EAllowShrinking::No);
        if (State == nullptr || Visited.Contains(State))
        {
            continue;
        }
        Visited.Add(State);
        if (!Visitor(*State))
        {
            return;
        }
        for (int32 Index = State->Children.Num() - 1; Index >= 0; --Index)
        {
            if (State->Children[Index] != nullptr)
            {
                Pending.Add(State->Children[Index]);
            }
        }
    }
}

const UStateTreeState* FindUniqueState(const UStateTreeEditorData& EditorData, const FGuid& Id)
{
    const UStateTreeState* Match = nullptr;
    bool bAmbiguous = false;
    VisitStates(EditorData, [&](const UStateTreeState& State)
    {
        if (State.ID == Id)
        {
            if (Match != nullptr)
            {
                bAmbiguous = true;
                return false;
            }
            Match = &State;
        }
        return true;
    });
    return bAmbiguous ? nullptr : Match;
}

bool ContainsUniqueNodeOrTransitionId(
    const UStateTreeEditorData& EditorData,
    const FGuid& Id,
    const bool bTransition)
{
    int32 Matches = 0;
    const auto CountNode = [&](const FStateTreeEditorNode& Node)
    {
        if (!bTransition && Node.ID == Id)
        {
            ++Matches;
        }
    };
    for (const FStateTreeEditorNode& Node : EditorData.Evaluators) CountNode(Node);
    for (const FStateTreeEditorNode& Node : EditorData.GlobalTasks) CountNode(Node);
    VisitStates(EditorData, [&](const UStateTreeState& State)
    {
        for (const FStateTreeEditorNode& Node : State.EnterConditions) CountNode(Node);
        for (const FStateTreeEditorNode& Node : State.Tasks) CountNode(Node);
        CountNode(State.SingleTask);
        for (const FStateTreeEditorNode& Node : State.Considerations) CountNode(Node);
        for (const FStateTreeTransition& Transition : State.Transitions)
        {
            if (bTransition && Transition.ID == Id)
            {
                ++Matches;
            }
            for (const FStateTreeEditorNode& Node : Transition.Conditions) CountNode(Node);
        }
        return Matches <= 1;
    });
    return Matches == 1;
}

bool ContainsUniqueContextId(const UStateTreeEditorData& EditorData, const FGuid& Id)
{
    if (EditorData.Schema == nullptr)
    {
        return false;
    }
    int32 Matches = 0;
    for (const FStateTreeExternalDataDesc& Desc : EditorData.Schema->GetContextDataDescs())
    {
        if (Desc.ID == Id)
        {
            ++Matches;
        }
    }
    return Matches == 1;
}

bool SetNodeBasesFromProperty(
    const UStruct& Owner,
    const FName PropertyName,
    FDestination& OutDestination)
{
    const FProperty* Property = FindFProperty<FProperty>(&Owner, PropertyName);
    if (Property == nullptr)
    {
        return false;
    }
    const FString BaseStructPath = Property->GetMetaData(TEXT("BaseStruct"));
    const FString BaseClassPath = Property->GetMetaData(TEXT("BaseClass"));
    OutDestination.RequiredNodeStruct = BaseStructPath.IsEmpty()
        ? nullptr
        : FindObject<UScriptStruct>(nullptr, *BaseStructPath);
    OutDestination.RequiredNodeClass = BaseClassPath.IsEmpty()
        ? nullptr
        : FindObject<UClass>(nullptr, *BaseClassPath);
    return OutDestination.RequiredNodeStruct != nullptr;
}

bool IsRoleEnabled(
    const UStateTreeEditorData& EditorData,
    const FDestination& Destination)
{
    const UStateTreeSchema* Schema = EditorData.Schema;
    if (Schema == nullptr)
    {
        return false;
    }
    switch (Destination.Role)
    {
    case EDestinationRole::RootState:
    case EDestinationRole::ChildState:
        // Fixed State entry availability is finalized together with the
        // concrete native Type/SelectionBehavior pair below. A linked State
        // has compiler-defined TryEnterState selection and therefore does not
        // require an independently schema-approved selection value.
        return true;
    case EDestinationRole::GlobalEvaluator:
        return Schema->AllowEvaluators();
    case EDestinationRole::EnterCondition:
        return Schema->AllowEnterConditions();
    case EDestinationRole::Consideration:
        return Schema->AllowUtilityConsiderations();
    case EDestinationRole::Task:
        return true;
    case EDestinationRole::Parameter:
        return Destination.OwnerId.IsValid() || Schema->AllowGlobalParameters();
    default:
        return true;
    }
}

FString CandidateId(const FTopLevelAssetPath& Path)
{
    const FString Text = Path.ToString();
    return FString(NodePalettePrefix)
        + FSHA1::HashBuffer(*Text, Text.Len() * sizeof(TCHAR)).ToString();
}

FString CandidateDisplayName(const FTopLevelAssetPath& Path)
{
    FString Name = Path.GetAssetName().ToString();
    Name.RemoveFromEnd(TEXT("_C"));
    return Name;
}

FString GuidText(const FGuid& Guid)
{
    return Guid.ToString(EGuidFormats::DigitsWithHyphensLower);
}

bool MatchesSearch(const FRawNodeCandidate& Candidate, const FString& SearchText)
{
    return SearchText.IsEmpty()
        || CandidateDisplayName(Candidate.Path).Contains(SearchText, ESearchCase::IgnoreCase)
        || Candidate.Path.ToString().Contains(SearchText, ESearchCase::IgnoreCase);
}

void GatherNodeCandidates(
    const FDestination& Destination,
    const FString& SearchText,
    TArray<FRawNodeCandidate>& OutCandidates)
{
    OutCandidates.Reset();
    const TSharedPtr<FStateTreeNodeClassCache> Cache = FStateTreeEditorModule::GetModule().GetNodeClassCache();
    if (!Cache.IsValid())
    {
        return;
    }
    if (Destination.RequiredNodeStruct != nullptr)
    {
        TArray<TSharedPtr<FStateTreeNodeClassData>> Structs;
        Cache->GetStructs(Destination.RequiredNodeStruct, Structs);
        for (const TSharedPtr<FStateTreeNodeClassData>& Data : Structs)
        {
            if (!Data.IsValid() || Data->GetStructPath() == Destination.RequiredNodeStruct->GetStructPathName())
            {
                continue;
            }
            FRawNodeCandidate Candidate{Data->GetStructPath(), Data, false};
            if (MatchesSearch(Candidate, SearchText))
            {
                OutCandidates.Add(MoveTemp(Candidate));
            }
        }
    }
    if (Destination.RequiredNodeClass != nullptr)
    {
        TArray<TSharedPtr<FStateTreeNodeClassData>> Classes;
        Cache->GetClasses(Destination.RequiredNodeClass, Classes);
        for (const TSharedPtr<FStateTreeNodeClassData>& Data : Classes)
        {
            if (!Data.IsValid() || Data->GetStructPath() == Destination.RequiredNodeClass->GetClassPathName())
            {
                continue;
            }
            FRawNodeCandidate Candidate{Data->GetStructPath(), Data, true};
            if (MatchesSearch(Candidate, SearchText))
            {
                OutCandidates.Add(MoveTemp(Candidate));
            }
        }
    }
    OutCandidates.Sort([](const FRawNodeCandidate& A, const FRawNodeCandidate& B)
    {
        return A.Path.ToString() < B.Path.ToString();
    });
}

UScriptStruct* BlueprintWrapperForRole(const EDestinationRole Role)
{
    switch (Role)
    {
    case EDestinationRole::GlobalEvaluator:
        return FStateTreeBlueprintEvaluatorWrapper::StaticStruct();
    case EDestinationRole::EnterCondition:
    case EDestinationRole::TransitionCondition:
        return FStateTreeBlueprintConditionWrapper::StaticStruct();
    case EDestinationRole::Consideration:
        return FStateTreeBlueprintConsiderationWrapper::StaticStruct();
    case EDestinationRole::GlobalTask:
    case EDestinationRole::Task:
        return FStateTreeBlueprintTaskWrapper::StaticStruct();
    default:
        return nullptr;
    }
}

bool MaterializeCandidate(
    const UStateTreeEditorData& EditorData,
    const FDestination& Destination,
    const FRawNodeCandidate& Candidate,
    FEntry& OutEntry)
{
    const UStateTreeSchema* Schema = EditorData.Schema;
    if (Schema == nullptr || !Candidate.Data.IsValid())
    {
        return false;
    }

    UStruct* Struct = Candidate.Data->GetStruct(true);
    if (Struct == nullptr)
    {
        return false;
    }

    OutEntry = FEntry();
    OutEntry.Id = CandidateId(Candidate.Path);
    OutEntry.DisplayName = Struct->GetDisplayNameText().ToString();
    OutEntry.ConstructorKind = EConstructorKind::Node;
    OutEntry.DestinationRole = Destination.Role;
    OutEntry.NativeType = Candidate.Path;
    OutEntry.bBlueprint = Candidate.bBlueprint;

    if (UScriptStruct* ScriptStruct = Cast<UScriptStruct>(Struct))
    {
        if (ScriptStruct == Destination.RequiredNodeStruct
            || ScriptStruct->HasMetaData(TEXT("Hidden"))
            || ScriptStruct->HasMetaData(TEXT("Deprecated"))
            || !ScriptStruct->IsChildOf(Destination.RequiredNodeStruct)
            || !Schema->IsStructAllowed(ScriptStruct))
        {
            return false;
        }
        TInstancedStruct<FStateTreeNodeBase> NodeValue;
        NodeValue.InitializeAsScriptStruct(ScriptStruct);
        OutEntry.NodeStruct = ScriptStruct;
        OutEntry.InstanceDataType = NodeValue.Get().GetInstanceDataType();
        if (Destination.Role == EDestinationRole::PropertyFunction)
        {
            const FProperty* Output = OutEntry.InstanceDataType != nullptr
                ? UE::StateTree::GetStructSingleOutputProperty(*OutEntry.InstanceDataType)
                : nullptr;
            if (Output == nullptr
                || !Destination.BindingTarget.IsValid()
                || Destination.BindingTarget->LeafProperty == nullptr
                || UE::PropertyBinding::GetPropertyCompatibility(
                    Output,
                    Destination.BindingTarget->LeafProperty)
                    == UE::PropertyBinding::EPropertyCompatibility::Incompatible)
            {
                return false;
            }
            OutEntry.PropertyFunctionOutput = Output;
        }
        OutEntry.bSpawnable = OutEntry.InstanceDataType != nullptr;
        return OutEntry.bSpawnable;
    }

    UClass* Class = Cast<UClass>(Struct);
    if (Class == nullptr
        || Destination.RequiredNodeClass == nullptr
        || Class == Destination.RequiredNodeClass
        || !Class->IsChildOf(Destination.RequiredNodeClass)
        || Class->HasAnyClassFlags(CLASS_Abstract | CLASS_Hidden | CLASS_HideDropDown | CLASS_Deprecated | CLASS_NewerVersionExists)
        || Class->HasMetaData(TEXT("Hidden"))
        || !Schema->IsClassAllowed(Class))
    {
        return false;
    }
    OutEntry.NodeStruct = BlueprintWrapperForRole(Destination.Role);
    OutEntry.NodeClass = Class;
    OutEntry.InstanceDataType = Class;
    OutEntry.bSpawnable = OutEntry.NodeStruct != nullptr;
    return OutEntry.bSpawnable;
}

FEntry FixedEntry(
    const FString& Id,
    const FString& Name,
    const EConstructorKind Kind,
    const EDestinationRole Role,
    const UStruct* NativeType)
{
    FEntry Entry;
    Entry.Id = Id;
    Entry.DisplayName = Name;
    Entry.ConstructorKind = Kind;
    Entry.DestinationRole = Role;
    Entry.NativeType = NativeType != nullptr ? NativeType->GetStructPathName() : FTopLevelAssetPath();
    Entry.bSpawnable = NativeType != nullptr;
    return Entry;
}

template<typename EnumType, typename PredicateType>
void AppendAllowedEnumValues(
    const EnumType Preferred,
    PredicateType&& Predicate,
    TArray<EnumType>& OutValues)
{
    OutValues.Reset();
    if (Predicate(Preferred))
    {
        OutValues.Add(Preferred);
    }
    const UEnum* Enum = StaticEnum<EnumType>();
    if (Enum == nullptr)
    {
        return;
    }
    for (int32 Index = 0; Index < Enum->NumEnums(); ++Index)
    {
        const int64 RawValue = Enum->GetValueByIndex(Index);
        const FString Name = Enum->GetNameStringByIndex(Index);
        if (RawValue == INDEX_NONE
            || Name.EndsWith(TEXT("MAX"))
            || Enum->HasMetaData(TEXT("Hidden"), Index))
        {
            continue;
        }
        const EnumType Value = static_cast<EnumType>(RawValue);
        if (Predicate(Value) && !OutValues.Contains(Value))
        {
            OutValues.Add(Value);
        }
    }
}

bool SetOrdinaryStateConstructorDefaults(
    const UStateTreeEditorData& EditorData,
    FEntry& OutEntry)
{
    const UStateTreeSchema* Schema = EditorData.Schema;
    const UStateTreeState* Defaults = GetDefault<UStateTreeState>();
    if (Schema == nullptr || Defaults == nullptr)
    {
        return false;
    }
    TArray<EStateTreeStateType> Types;
    AppendAllowedEnumValues<EStateTreeStateType>(
        Defaults->Type,
        [&](const EStateTreeStateType Value)
        {
            return (Value == EStateTreeStateType::State
                    || Value == EStateTreeStateType::Group
                    || Value == EStateTreeStateType::Subtree)
                && Schema->IsStateTypeAllowed(Value);
        },
        Types);
    TArray<EStateTreeStateSelectionBehavior> Selections;
    AppendAllowedEnumValues<EStateTreeStateSelectionBehavior>(
        Defaults->SelectionBehavior,
        [&](const EStateTreeStateSelectionBehavior Value)
        {
            return Schema->IsStateSelectionAllowed(Value);
        },
        Selections);
    const UEnum* TypeEnum = StaticEnum<EStateTreeStateType>();
    const UEnum* SelectionEnum = StaticEnum<EStateTreeStateSelectionBehavior>();
    if (TypeEnum == nullptr || SelectionEnum == nullptr)
    {
        return false;
    }
    for (const EStateTreeStateType Type : Types)
    {
        OutEntry.StateType = TypeEnum->GetNameStringByValue(static_cast<int64>(Type));
        if (!Selections.IsEmpty())
        {
            OutEntry.StateSelectionBehavior = SelectionEnum->GetNameStringByValue(
                static_cast<int64>(Selections[0]));
            return !OutEntry.StateType.IsEmpty()
                && !OutEntry.StateSelectionBehavior.IsEmpty();
        }
    }
    return false;
}

bool SetLinkedAssetStateConstructorDefaults(
    const UStateTreeEditorData& EditorData,
    FEntry& OutEntry)
{
    const UStateTreeSchema* Schema = EditorData.Schema;
    const UEnum* TypeEnum = StaticEnum<EStateTreeStateType>();
    if (Schema == nullptr
        || TypeEnum == nullptr
        || !Schema->IsStateTypeAllowed(EStateTreeStateType::LinkedAsset))
    {
        return false;
    }
    OutEntry.Id = LinkedAssetStatePaletteId;
    OutEntry.DisplayName = TEXT("Linked Asset State");
    OutEntry.StateType = TypeEnum->GetNameStringByValue(
        static_cast<int64>(EStateTreeStateType::LinkedAsset));
    OutEntry.StateSelectionBehavior.Reset();
    OutEntry.bFixedStateType = true;
    return !OutEntry.StateType.IsEmpty();
}

bool SetLinkedStateConstructorDefaults(
    const UStateTreeEditorData& EditorData,
    FEntry& OutEntry)
{
    const UStateTreeSchema* Schema = EditorData.Schema;
    const UEnum* TypeEnum = StaticEnum<EStateTreeStateType>();
    if (Schema == nullptr
        || TypeEnum == nullptr
        || !Schema->IsStateTypeAllowed(EStateTreeStateType::Linked))
    {
        return false;
    }
    OutEntry.StateType = TypeEnum->GetNameStringByValue(
        static_cast<int64>(EStateTreeStateType::Linked));
    OutEntry.StateSelectionBehavior.Reset();
    OutEntry.bFixedStateType = true;
    return !OutEntry.StateType.IsEmpty();
}

const FInstancedPropertyBag* ParameterBagForDestination(
    const UStateTreeEditorData& EditorData,
    const FDestination& Destination)
{
    if (Destination.Role != EDestinationRole::Parameter)
    {
        return nullptr;
    }
    if (!Destination.OwnerId.IsValid())
    {
        return &EditorData.GetRootParametersPropertyBag();
    }
    const UStateTreeState* State = FindUniqueState(EditorData, Destination.OwnerId);
    return State != nullptr ? &State->Parameters.Parameters : nullptr;
}

FString UniqueParameterName(
    const UStateTreeEditorData& EditorData,
    const FDestination& Destination)
{
    const FInstancedPropertyBag* Bag = ParameterBagForDestination(EditorData, Destination);
    const UPropertyBag* Struct = Bag != nullptr ? Bag->GetPropertyBagStruct() : nullptr;
    TSet<FName> Existing;
    if (Struct != nullptr)
    {
        for (const FPropertyBagPropertyDesc& Desc : Struct->GetPropertyDescs())
        {
            Existing.Add(Desc.Name);
        }
    }
    const int32 CandidateCount = Existing.Num() + 1;
    for (int32 Index = 0; Index < CandidateCount; ++Index)
    {
        const FString Text = Index == 0
            ? TEXT("NewParameter")
            : FString::Printf(TEXT("NewParameter_%d"), Index);
        if (!Existing.Contains(FName(*Text)))
        {
            return Text;
        }
    }
    return FString();
}

void GatherLinkedStateEntries(
    const UStateTreeEditorData& EditorData,
    const FDestination& Destination,
    const FEntry& BaseEntry,
    TArray<FEntry>& OutEntries)
{
    TArray<const UStateTreeState*> States;
    TMap<FGuid, int32> IdCounts;
    VisitStates(EditorData, [&](const UStateTreeState& State)
    {
        States.Add(&State);
        if (State.ID.IsValid())
        {
            ++IdCounts.FindOrAdd(State.ID);
        }
        return true;
    });

    TSet<const UStateTreeState*> DestinationAncestors;
    const UStateTreeState* Ancestor = Destination.OwnerId.IsValid()
        ? FindUniqueState(EditorData, Destination.OwnerId)
        : nullptr;
    while (Ancestor != nullptr && !DestinationAncestors.Contains(Ancestor))
    {
        DestinationAncestors.Add(Ancestor);
        Ancestor = Ancestor->Parent;
    }

    for (const UStateTreeState* Candidate : States)
    {
        if (Candidate == nullptr
            || Candidate->Type != EStateTreeStateType::Subtree
            || !Candidate->ID.IsValid()
            || IdCounts.FindRef(Candidate->ID) != 1
            || DestinationAncestors.Contains(Candidate))
        {
            continue;
        }
        FEntry Entry = BaseEntry;
        Entry.Id = FString(LinkedStatePalettePrefix) + GuidText(Candidate->ID);
        Entry.DisplayName = Candidate->Name.IsNone()
            ? FString::Printf(TEXT("Linked State -> state@%s"), *GuidText(Candidate->ID))
            : FString::Printf(TEXT("Linked State -> %s"), *Candidate->Name.ToString());
        Entry.LinkedSubtreeId = Candidate->ID;
        Entry.bSpawnable = true;
        OutEntries.Add(MoveTemp(Entry));
    }
}

bool FixedDestinationEntries(
    const UStateTreeEditorData& EditorData,
    const FDestination& Destination,
    TArray<FEntry>& OutEntries)
{
    OutEntries.Reset();
    switch (Destination.Role)
    {
    case EDestinationRole::RootState:
    case EDestinationRole::ChildState:
    {
        const FEntry BaseEntry = FixedEntry(
            StatePaletteId,
            TEXT("State"),
            EConstructorKind::State,
            Destination.Role,
            UStateTreeState::StaticClass());
        if (!BaseEntry.bSpawnable)
        {
            return true;
        }

        FEntry OrdinaryEntry = BaseEntry;
        if (SetOrdinaryStateConstructorDefaults(EditorData, OrdinaryEntry))
        {
            OrdinaryEntry.bSpawnable = true;
            OutEntries.Add(MoveTemp(OrdinaryEntry));
        }

        FEntry LinkedAssetEntry = BaseEntry;
        if (SetLinkedAssetStateConstructorDefaults(EditorData, LinkedAssetEntry))
        {
            LinkedAssetEntry.bSpawnable = true;
            OutEntries.Add(MoveTemp(LinkedAssetEntry));
        }

        FEntry LinkedEntry = BaseEntry;
        if (SetLinkedStateConstructorDefaults(EditorData, LinkedEntry))
        {
            GatherLinkedStateEntries(EditorData, Destination, LinkedEntry, OutEntries);
        }
        return true;
    }
    case EDestinationRole::Transition:
        OutEntries.Add(FixedEntry(
            TransitionPaletteId,
            TEXT("Transition"),
            EConstructorKind::Transition,
            Destination.Role,
            FStateTreeTransition::StaticStruct()));
        return true;
    case EDestinationRole::Parameter:
    {
        FEntry Entry = FixedEntry(
            ParameterPaletteId,
            TEXT("Parameter"),
            EConstructorKind::Parameter,
            Destination.Role,
            FStateTreeStateParameters::StaticStruct());
        Entry.ParameterName = UniqueParameterName(EditorData, Destination);
        // This is an explicit, valid UE native seed type, not an implicit SAL
        // default. The copied constructor may replace it with any Property Bag
        // type accepted by exact schema and Patch validation.
        Entry.ParameterType = TEXT("FloatProperty");
        Entry.bSpawnable = Entry.bSpawnable && !Entry.ParameterName.IsEmpty();
        if (Entry.bSpawnable)
        {
            OutEntries.Add(MoveTemp(Entry));
        }
        return true;
    }
    default:
        return false;
    }
}
}

bool ResolveDestination(
    const UStateTree& StateTree,
    const UStateTreeEditorData& EditorData,
    const TSharedPtr<FJsonObject>& DestinationRef,
    FDestination& OutDestination,
    FString& OutMessage)
{
    OutDestination = FDestination();
    OutMessage.Reset();
    FString OwnerKind;
    FString OwnerIdText;
    TArray<FString> Path;
    FString DirectKind;
    FString DirectId;
    if (DestinationRef.IsValid()
        && DestinationRef->TryGetStringField(TEXT("kind"), DirectKind)
        && DirectKind == TEXT("parameter")
        && DestinationRef->TryGetStringField(TEXT("id"), DirectId))
    {
        TSharedPtr<StateTreeSchema::FResolvedMember> TargetMember =
            MakeShared<StateTreeSchema::FResolvedMember>();
        if (!StateTreeSchema::ResolveMember(
                StateTree,
                EditorData,
                DirectKind,
                DirectId,
                {},
                *TargetMember,
                OutMessage,
                StateTreeSchema::EMemberPurpose::BindingTarget)
            || !TargetMember->bBindingTarget)
        {
            if (OutMessage.IsEmpty())
            {
                OutMessage = TEXT("Parameter Palette destination is not a writable StateTree Binding target.");
            }
            return false;
        }
        OutDestination.Role = EDestinationRole::PropertyFunction;
        OutDestination.RequiredNodeStruct = FStateTreePropertyFunctionBase::StaticStruct();
        OutDestination.BindingTarget = MoveTemp(TargetMember);
        return true;
    }
    if (!ReadMemberReference(DestinationRef, OwnerKind, OwnerIdText, Path))
    {
        OutMessage = TEXT("StateTree Palette requires an exact member destination.");
        return false;
    }

    OutDestination.MemberPath = Path;
    const auto ResolveBindingTarget = [&]()
    {
        TSharedPtr<StateTreeSchema::FResolvedMember> TargetMember =
            MakeShared<StateTreeSchema::FResolvedMember>();
        FString Message;
        if (!StateTreeSchema::ResolveMemberReference(
                StateTree,
                EditorData,
                DestinationRef,
                *TargetMember,
                Message,
                StateTreeSchema::EMemberPurpose::BindingTarget)
            || !TargetMember->bBindingTarget)
        {
            OutMessage = Message.IsEmpty()
                ? TEXT("Property Function Palette destination is not a writable StateTree Binding target.")
                : Message;
            return false;
        }
        OutDestination.Role = EDestinationRole::PropertyFunction;
        OutDestination.RequiredNodeStruct = FStateTreePropertyFunctionBase::StaticStruct();
        OutDestination.BindingTarget = MoveTemp(TargetMember);
        return true;
    };

    const FString& Field = Path[0];
    if (OwnerKind == TEXT("local"))
    {
        if (Path.Num() != 1)
        {
            OutMessage = TEXT("Target lifecycle Palette destinations contain exactly one canonical native member.");
            return false;
        }
        if (Field == TEXT("SubTrees"))
        {
            OutDestination.Role = EDestinationRole::RootState;
        }
        else if (Field == TEXT("Evaluators"))
        {
            OutDestination.Role = EDestinationRole::GlobalEvaluator;
            SetNodeBasesFromProperty(
                *EditorData.GetClass(),
                GET_MEMBER_NAME_CHECKED(UStateTreeEditorData, Evaluators),
                OutDestination);
        }
        else if (Field == TEXT("GlobalTasks"))
        {
            OutDestination.Role = EDestinationRole::GlobalTask;
            SetNodeBasesFromProperty(
                *EditorData.GetClass(),
                GET_MEMBER_NAME_CHECKED(UStateTreeEditorData, GlobalTasks),
                OutDestination);
        }
        else if (Field == TEXT("RootParameters"))
        {
            OutDestination.Role = EDestinationRole::Parameter;
        }
        else
        {
            OutMessage = TEXT("The target member is not a StateTree Palette destination.");
            return false;
        }
    }
    else if (OwnerKind == TEXT("state"))
    {
        FGuid OwnerId;
        const UStateTreeState* State = ParseGuid(OwnerIdText, OwnerId)
            ? FindUniqueState(EditorData, OwnerId)
            : nullptr;
        if (State == nullptr)
        {
            OutMessage = TEXT("StateTree Palette destination State does not exist or is ambiguous.");
            return false;
        }
        OutDestination.OwnerId = OwnerId;
        if (Path.Num() != 1)
        {
            return ResolveBindingTarget();
        }
        if (Field == TEXT("Children"))
        {
            if (State->Type == EStateTreeStateType::Linked
                || State->Type == EStateTreeStateType::LinkedAsset)
            {
                OutMessage = TEXT("Linked State types cannot accept authored child States.");
                return false;
            }
            OutDestination.Role = EDestinationRole::ChildState;
        }
        else if (Field == TEXT("EnterConditions"))
        {
            OutDestination.Role = EDestinationRole::EnterCondition;
            SetNodeBasesFromProperty(*UStateTreeState::StaticClass(), GET_MEMBER_NAME_CHECKED(UStateTreeState, EnterConditions), OutDestination);
        }
        else if (Field == TEXT("Tasks") || Field == TEXT("SingleTask"))
        {
            if (State->Type != EStateTreeStateType::State
                && State->Type != EStateTreeStateType::Subtree)
            {
                OutMessage = TEXT("The current State type does not expose a Task destination.");
                return false;
            }
            if (!State->Tasks.IsEmpty() && State->SingleTask.Node.IsValid())
            {
                OutMessage = TEXT("State contains authored Nodes in both Tasks and SingleTask; Task destination is ambiguous.");
                return false;
            }
            OutDestination.Role = EDestinationRole::Task;
            const FName NativeField = Field == TEXT("Tasks")
                ? GET_MEMBER_NAME_CHECKED(UStateTreeState, Tasks)
                : GET_MEMBER_NAME_CHECKED(UStateTreeState, SingleTask);
            SetNodeBasesFromProperty(*UStateTreeState::StaticClass(), NativeField, OutDestination);
            if (EditorData.Schema != nullptr
                && ((Field == TEXT("Tasks") && !EditorData.Schema->AllowMultipleTasks())
                    || (Field != TEXT("Tasks") && EditorData.Schema->AllowMultipleTasks())))
            {
                OutMessage = TEXT("The selected Task destination is disabled by the current StateTree Schema.");
                return false;
            }
        }
        else if (Field == TEXT("Considerations"))
        {
            OutDestination.Role = EDestinationRole::Consideration;
            SetNodeBasesFromProperty(*UStateTreeState::StaticClass(), GET_MEMBER_NAME_CHECKED(UStateTreeState, Considerations), OutDestination);
        }
        else if (Field == TEXT("Transitions"))
        {
            OutDestination.Role = EDestinationRole::Transition;
        }
        else if (Field == TEXT("Parameters"))
        {
            if (State->Parameters.bFixedLayout)
            {
                OutMessage = TEXT("Fixed-layout State Parameters do not allow descriptor creation.");
                return false;
            }
            OutDestination.Role = EDestinationRole::Parameter;
        }
        else
        {
            return ResolveBindingTarget();
        }
    }
    else if (OwnerKind == TEXT("transition"))
    {
        FGuid OwnerId;
        if (!ParseGuid(OwnerIdText, OwnerId) || !ContainsUniqueNodeOrTransitionId(EditorData, OwnerId, true))
        {
            OutMessage = TEXT("StateTree Palette destination Transition does not exist or is ambiguous.");
            return false;
        }
        OutDestination.OwnerId = OwnerId;
        if (Path.Num() != 1)
        {
            return ResolveBindingTarget();
        }
        if (Field != TEXT("Conditions"))
        {
            return ResolveBindingTarget();
        }
        OutDestination.Role = EDestinationRole::TransitionCondition;
        SetNodeBasesFromProperty(*FStateTreeTransition::StaticStruct(), GET_MEMBER_NAME_CHECKED(FStateTreeTransition, Conditions), OutDestination);
    }
    else if (OwnerKind == TEXT("node")
        || OwnerKind == TEXT("object")
        || OwnerKind == TEXT("parameter"))
    {
        return ResolveBindingTarget();
    }
    else
    {
        OutMessage = TEXT("This reference kind is not a StateTree Palette destination.");
        return false;
    }

    if (!IsRoleEnabled(EditorData, OutDestination))
    {
        OutMessage = TEXT("The current StateTree Schema disables this Palette destination.");
        return false;
    }
    if ((OutDestination.Role == EDestinationRole::GlobalEvaluator
            || OutDestination.Role == EDestinationRole::GlobalTask
            || OutDestination.Role == EDestinationRole::EnterCondition
            || OutDestination.Role == EDestinationRole::Task
            || OutDestination.Role == EDestinationRole::Consideration
            || OutDestination.Role == EDestinationRole::TransitionCondition
            || OutDestination.Role == EDestinationRole::PropertyFunction)
        && OutDestination.RequiredNodeStruct == nullptr)
    {
        OutMessage = TEXT("The native destination does not expose a valid StateTree node base type.");
        return false;
    }
    return true;
}

bool DiscoverEntries(
    const UStateTree& StateTree,
    const UStateTreeEditorData& EditorData,
    const FDestination& Destination,
    const FString& SearchText,
    const int32 Offset,
    const int32 Limit,
    FPage& OutPage,
    FString& OutMessage)
{
    OutPage = FPage();
    OutMessage.Reset();
    if (Offset < 0 || Limit <= 0)
    {
        OutMessage = TEXT("StateTree Palette page bounds are invalid.");
        return false;
    }
    if (!IsRoleEnabled(EditorData, Destination))
    {
        OutMessage = TEXT("The current StateTree Schema disables this Palette destination.");
        return false;
    }

    TArray<FEntry> FixedEntries;
    if (FixedDestinationEntries(EditorData, Destination, FixedEntries))
    {
        FixedEntries.RemoveAll(
            [&](const FEntry& Entry)
            {
                return !Entry.bSpawnable
                    || !(SearchText.IsEmpty()
                        || Entry.DisplayName.Contains(SearchText, ESearchCase::IgnoreCase)
                        || Entry.Id.Contains(SearchText, ESearchCase::IgnoreCase)
                        || Entry.ParameterType.Contains(SearchText, ESearchCase::IgnoreCase));
            });
        int32 RawIndex = FMath::Min(Offset, FixedEntries.Num());
        const int32 EndIndex = RawIndex + FMath::Min(Limit, FixedEntries.Num() - RawIndex);
        while (RawIndex < EndIndex)
        {
            OutPage.Entries.Add(MoveTemp(FixedEntries[RawIndex]));
            ++RawIndex;
        }
        if (RawIndex < FixedEntries.Num())
        {
            OutPage.NextOffset = RawIndex;
            OutPage.bComplete = false;
        }
        return true;
    }

    TArray<FRawNodeCandidate> Candidates;
    GatherNodeCandidates(Destination, SearchText, Candidates);
    int32 RawIndex = FMath::Min(Offset, Candidates.Num());
    int32 Evaluated = 0;
    while (RawIndex < Candidates.Num()
        && OutPage.Entries.Num() < Limit
        && Evaluated < Limit)
    {
        FEntry Entry;
        if (MaterializeCandidate(EditorData, Destination, Candidates[RawIndex], Entry))
        {
            OutPage.Entries.Add(MoveTemp(Entry));
        }
        ++RawIndex;
        ++Evaluated;
    }
    if (RawIndex < Candidates.Num())
    {
        OutPage.NextOffset = RawIndex;
        OutPage.bComplete = false;
    }
    return true;
}

bool ResolveEntry(
    const UStateTree& StateTree,
    const UStateTreeEditorData& EditorData,
    const FDestination& Destination,
    const FString& Id,
    FEntry& OutEntry,
    FString& OutMessage)
{
    OutEntry = FEntry();
    OutMessage.Reset();

    TArray<FEntry> FixedEntries;
    if (FixedDestinationEntries(EditorData, Destination, FixedEntries))
    {
        FEntry* Match = nullptr;
        for (FEntry& Entry : FixedEntries)
        {
            if (Entry.Id != Id || !Entry.bSpawnable)
            {
                continue;
            }
            if (Match != nullptr)
            {
                OutMessage = TEXT("Palette capability id is ambiguous.");
                return false;
            }
            Match = &Entry;
        }
        if (Match != nullptr && IsRoleEnabled(EditorData, Destination))
        {
            OutEntry = MoveTemp(*Match);
            return true;
        }
        OutMessage = TEXT("Palette capability is not spawnable at this destination.");
        return false;
    }

    if (!Id.StartsWith(NodePalettePrefix))
    {
        OutMessage = TEXT("Palette capability does not belong to StateTree node discovery.");
        return false;
    }
    TArray<FRawNodeCandidate> Candidates;
    GatherNodeCandidates(Destination, FString(), Candidates);
    const FRawNodeCandidate* Match = nullptr;
    for (const FRawNodeCandidate& Candidate : Candidates)
    {
        if (CandidateId(Candidate.Path) == Id)
        {
            if (Match != nullptr)
            {
                OutMessage = TEXT("Palette capability id is ambiguous.");
                return false;
            }
            Match = &Candidate;
        }
    }
    if (Match == nullptr || !MaterializeCandidate(EditorData, Destination, *Match, OutEntry))
    {
        OutMessage = TEXT("Palette capability is not spawnable at this destination.");
        return false;
    }
    return true;
}

const TCHAR* ConstructorName(const EConstructorKind Kind)
{
    switch (Kind)
    {
    case EConstructorKind::State: return TEXT("state");
    case EConstructorKind::Node: return TEXT("node");
    case EConstructorKind::Transition: return TEXT("transition");
    case EConstructorKind::Parameter: return TEXT("parameter");
    default: return TEXT("object");
    }
}

const TCHAR* DestinationRoleName(const EDestinationRole Role)
{
    switch (Role)
    {
    case EDestinationRole::RootState: return TEXT("root_state");
    case EDestinationRole::ChildState: return TEXT("child_state");
    case EDestinationRole::GlobalEvaluator: return TEXT("global_evaluator");
    case EDestinationRole::GlobalTask: return TEXT("global_task");
    case EDestinationRole::EnterCondition: return TEXT("enter_condition");
    case EDestinationRole::Task: return TEXT("task");
    case EDestinationRole::Consideration: return TEXT("consideration");
    case EDestinationRole::Transition: return TEXT("transition");
    case EDestinationRole::TransitionCondition: return TEXT("transition_condition");
    case EDestinationRole::Parameter: return TEXT("parameter");
    case EDestinationRole::PropertyFunction: return TEXT("property_function");
    default: return TEXT("unknown");
    }
}

TSharedPtr<FJsonValue> MakeConstructor(const FEntry& Entry)
{
    TSharedPtr<FJsonObject> Args = MakeShared<FJsonObject>();
    Args->SetStringField(TEXT("palette"), Entry.Id);
    if (Entry.ConstructorKind == EConstructorKind::State)
    {
        Args->SetStringField(TEXT("Type"), Entry.StateType);
        if (!Entry.StateSelectionBehavior.IsEmpty())
        {
            Args->SetStringField(
                TEXT("SelectionBehavior"),
                Entry.StateSelectionBehavior);
        }
        if (Entry.LinkedSubtreeId.IsValid())
        {
            Args->SetField(
                TEXT("LinkedSubtree"),
                Value::Stable(TEXT("state"), GuidText(Entry.LinkedSubtreeId)));
        }
    }
    else if (Entry.ConstructorKind == EConstructorKind::Parameter)
    {
        Args->SetStringField(TEXT("Name"), Entry.ParameterName);
        Args->SetStringField(TEXT("type"), Entry.ParameterType);
    }
    return Value::Call(ConstructorName(Entry.ConstructorKind), Args);
}
}
