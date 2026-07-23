// Copyright 2026 Loomle contributors.

#include "SalStateTreeSchema.h"

#include "../SalRuntime.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "HAL/IConsoleManager.h"
#include "PropertyBindingBindableStructDescriptor.h"
#include "PropertyBindingTypes.h"
#include "StateTree.h"
#include "StateTreeDelegate.h"
#include "StateTreeEditorData.h"
#include "StateTreeEditorNode.h"
#include "StateTreeEditorPropertyBindings.h"
#include "StateTreeEditorSchema.h"
#include "StateTreePropertyBindings.h"
#include "StateTreePropertyFunctionBase.h"
#include "StateTreePropertyRef.h"
#include "StateTreePropertyRefHelpers.h"
#include "StateTreeSchema.h"
#include "StateTreeState.h"
#include "StructUtils/PropertyBag.h"
#include "UObject/UnrealType.h"

namespace Loomle::Sal::StateTreeSchema
{
namespace
{
bool ParseGuid(const FString& Text, FGuid& OutGuid)
{
    return FGuid::Parse(Text, OutGuid) && OutGuid.IsValid();
}

bool ParseParameterId(const FString& Text, FGuid& OutContainerId, FGuid& OutPropertyId)
{
    FString Container;
    FString Property;
    return Text.Split(TEXT("/"), &Container, &Property)
        && ParseGuid(Container, OutContainerId)
        && ParseGuid(Property, OutPropertyId);
}

struct FBindableSurface
{
    const UStruct* Struct = nullptr;
    const void* Memory = nullptr;
    EStateTreeBindableStructSource Source = EStateTreeBindableStructSource::Context;
};

/**
 * UE's public Binding lookup deliberately returns the first matching StructID.
 * SAL stable references need a stricter view: every native Binding StructID must
 * identify exactly one surface across the complete VisitAllNodes enumeration.
 */
class FBindableSurfaceIndex
{
public:
    explicit FBindableSurfaceIndex(const UStateTreeEditorData& EditorData)
    {
        EditorData.VisitAllNodes(
            [&](const UStateTreeState*,
                const FStateTreeBindableStructDesc& Desc,
                const FStateTreeDataView View)
            {
                ById.FindOrAdd(Desc.ID).Add({
                    View.GetStruct(),
                    View.GetMemory(),
                    Desc.DataSource});
                return EStateTreeVisitor::Continue;
            });
    }

    const TArray<FBindableSurface>* Find(const FGuid& Id) const
    {
        return ById.Find(Id);
    }

private:
    TMap<FGuid, TArray<FBindableSurface>> ById;
};

bool ResolveCanonicalBindableSurface(
    const UStateTreeEditorData& EditorData,
    const FGuid& StructId,
    const FPropertyBindingDataView& ExpectedView,
    const TOptional<EStateTreeBindableStructSource> ExpectedSource,
    FString& OutMessage)
{
    if (!StructId.IsValid())
    {
        OutMessage = TEXT("Binding StructID is invalid.");
        return false;
    }

    const FBindableSurfaceIndex Index(EditorData);
    const TArray<FBindableSurface>* Matches = Index.Find(StructId);
    if (Matches == nullptr || Matches->IsEmpty())
    {
        OutMessage = TEXT("Binding StructID is not present on UE's current VisitAllNodes surface.");
        return false;
    }
    if (Matches->Num() != 1)
    {
        OutMessage = FString::Printf(
            TEXT("Binding StructID collides across %d UE StateTree Binding surfaces."),
            Matches->Num());
        return false;
    }

    const FBindableSurface& Match = (*Matches)[0];
    if (Match.Struct != ExpectedView.GetStruct()
        || Match.Memory != ExpectedView.GetMemory()
        || (ExpectedSource.IsSet() && Match.Source != ExpectedSource.GetValue()))
    {
        OutMessage = TEXT("Binding StructID resolves to a different UE StateTree Binding surface.");
        return false;
    }
    return true;
}

TOptional<EStateTreeBindableStructSource> ExpectedSourceForMember(
    const FResolvedMember& Member)
{
    if (Member.OwnerKind == TEXT("object"))
    {
        return EStateTreeBindableStructSource::Context;
    }
    if (Member.OwnerKind == TEXT("parameter"))
    {
        return Member.bRootParameterOwner
            ? EStateTreeBindableStructSource::Parameter
            : EStateTreeBindableStructSource::StateParameter;
    }
    if (Member.OwnerKind == TEXT("state")
        && !Member.SalPath.IsEmpty()
        && Member.SalPath[0] == TEXT("RequiredEventToEnter"))
    {
        return EStateTreeBindableStructSource::StateEvent;
    }
    if (Member.OwnerKind == TEXT("transition"))
    {
        if (!Member.SalPath.IsEmpty() && Member.SalPath[0] == TEXT("RequiredEvent"))
        {
            return EStateTreeBindableStructSource::TransitionEvent;
        }
        return EStateTreeBindableStructSource::Transition;
    }
    if (Member.OwnerKind == TEXT("node") && Member.bPropertyFunctionOwner)
    {
        return EStateTreeBindableStructSource::PropertyFunction;
    }
    return TOptional<EStateTreeBindableStructSource>();
}

bool IsCanonicalResolvedMember(
    const UStateTreeEditorData& EditorData,
    const FResolvedMember& Member,
    FString& OutMessage)
{
    return ResolveCanonicalBindableSurface(
        EditorData,
        Member.NativePath.GetStructID(),
        Member.OwnerView,
        ExpectedSourceForMember(Member),
        OutMessage);
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

const FStateTreeTransition* FindUniqueTransition(const UStateTreeEditorData& EditorData, const FGuid& Id)
{
    const FStateTreeTransition* Match = nullptr;
    bool bAmbiguous = false;
    VisitStates(EditorData, [&](const UStateTreeState& State)
    {
        for (const FStateTreeTransition& Transition : State.Transitions)
        {
            if (Transition.ID == Id)
            {
                if (Match != nullptr)
                {
                    bAmbiguous = true;
                    return false;
                }
                Match = &Transition;
            }
        }
        return true;
    });
    return bAmbiguous ? nullptr : Match;
}

void ConsiderNode(
    const FStateTreeEditorNode& Node,
    const FGuid& Id,
    const FStateTreeEditorNode*& InOutMatch,
    bool& bInOutAmbiguous)
{
    if (Node.ID != Id)
    {
        return;
    }
    if (InOutMatch != nullptr)
    {
        bInOutAmbiguous = true;
    }
    else
    {
        InOutMatch = &Node;
    }
}

const FStateTreeEditorNode* FindUniqueNode(const UStateTreeEditorData& EditorData, const FGuid& Id)
{
    const FStateTreeEditorNode* Match = nullptr;
    bool bAmbiguous = false;
    for (const FStateTreeEditorNode& Node : EditorData.Evaluators) ConsiderNode(Node, Id, Match, bAmbiguous);
    for (const FStateTreeEditorNode& Node : EditorData.GlobalTasks) ConsiderNode(Node, Id, Match, bAmbiguous);
    VisitStates(EditorData, [&](const UStateTreeState& State)
    {
        for (const FStateTreeEditorNode& Node : State.EnterConditions) ConsiderNode(Node, Id, Match, bAmbiguous);
        for (const FStateTreeEditorNode& Node : State.Tasks) ConsiderNode(Node, Id, Match, bAmbiguous);
        ConsiderNode(State.SingleTask, Id, Match, bAmbiguous);
        for (const FStateTreeEditorNode& Node : State.Considerations) ConsiderNode(Node, Id, Match, bAmbiguous);
        for (const FStateTreeTransition& Transition : State.Transitions)
        {
            for (const FStateTreeEditorNode& Node : Transition.Conditions) ConsiderNode(Node, Id, Match, bAmbiguous);
        }
        return !bAmbiguous;
    });
    for (const FStateTreePropertyPathBinding& Binding : EditorData.EditorBindings.GetBindings())
    {
        const FConstStructView FunctionView = Binding.GetPropertyFunctionNode();
        if (const FStateTreeEditorNode* Node = FunctionView.GetPtr<const FStateTreeEditorNode>())
        {
            ConsiderNode(*Node, Id, Match, bAmbiguous);
        }
    }
    return bAmbiguous ? nullptr : Match;
}

const FInstancedPropertyBag* FindParameterBag(
    const UStateTreeEditorData& EditorData,
    const FGuid& ContainerId,
    const UStateTreeState** OutOwnerState = nullptr)
{
    if (OutOwnerState != nullptr)
    {
        *OutOwnerState = nullptr;
    }
    const FInstancedPropertyBag* Match = ContainerId == EditorData.GetRootParametersGuid()
        ? &EditorData.GetRootParametersPropertyBag()
        : nullptr;
    int32 MatchCount = Match != nullptr ? 1 : 0;
    VisitStates(EditorData, [&](const UStateTreeState& State)
    {
        if (State.Parameters.ID == ContainerId)
        {
            ++MatchCount;
            if (MatchCount > 1)
            {
                Match = nullptr;
                return false;
            }
            Match = &State.Parameters.Parameters;
            if (OutOwnerState != nullptr)
            {
                *OutOwnerState = &State;
            }
        }
        return true;
    });
    return Match;
}

bool IsNumericSegment(const FString& Segment, int32& OutIndex)
{
    if (Segment.IsEmpty())
    {
        return false;
    }
    for (const TCHAR Character : Segment)
    {
        if (Character < TEXT('0') || Character > TEXT('9'))
        {
            return false;
        }
    }
    OutIndex = FCString::Atoi(*Segment);
    return OutIndex >= 0;
}

bool BuildNativePath(
    const FGuid& StructId,
    const TArray<FString>& Segments,
    FPropertyBindingPath& OutPath,
    FString& OutMessage)
{
    OutPath = FPropertyBindingPath(StructId);
    for (const FString& Segment : Segments)
    {
        int32 ArrayIndex = INDEX_NONE;
        if (IsNumericSegment(Segment, ArrayIndex))
        {
            if (OutPath.NumSegments() == 0
                || OutPath.GetMutableSegments().Last().GetArrayIndex() != INDEX_NONE)
            {
                OutMessage = TEXT("An indexed SAL member must follow exactly one array field.");
                return false;
            }
            OutPath.GetMutableSegments().Last().SetArrayIndex(ArrayIndex);
        }
        else
        {
            OutPath.AddPathSegment(FName(*Segment));
        }
    }
    return !OutPath.IsPathEmpty();
}

bool ResolveNativePath(
    const FPropertyBindingDataView& View,
    FPropertyBindingPath& InOutPath,
    const FProperty*& OutRoot,
    const FProperty*& OutLeaf,
    FString& OutMessage)
{
    OutRoot = nullptr;
    OutLeaf = nullptr;
    if (View.GetStruct() == nullptr || InOutPath.IsPathEmpty())
    {
        OutMessage = TEXT("The exact object does not expose this reflected member surface.");
        return false;
    }

    if (View.IsValid())
    {
        FString UpdateError;
        if (!InOutPath.UpdateSegmentsFromValue(View, &UpdateError))
        {
            OutMessage = UpdateError.IsEmpty() ? TEXT("The exact member path is invalid.") : UpdateError;
            return false;
        }
    }
    else
    {
        FString UpdateError;
        if (!InOutPath.UpdateSegments(View.GetStruct(), &UpdateError))
        {
            OutMessage = UpdateError.IsEmpty() ? TEXT("The exact member path is invalid.") : UpdateError;
            return false;
        }
    }

    TArray<FPropertyBindingPathIndirection, TInlineAllocator<16>> Indirections;
    const bool bResolved = View.IsValid()
        ? InOutPath.ResolveIndirectionsWithValue(View, Indirections, &OutMessage, true)
        : InOutPath.ResolveIndirections(View.GetStruct(), Indirections, &OutMessage, true);
    if (!bResolved || Indirections.IsEmpty())
    {
        if (OutMessage.IsEmpty())
        {
            OutMessage = TEXT("The exact member path cannot be resolved.");
        }
        return false;
    }
    for (const FPropertyBindingPathIndirection& Indirection : Indirections)
    {
        if (Indirection.GetProperty() == nullptr)
        {
            continue;
        }
        if (OutRoot == nullptr)
        {
            OutRoot = Indirection.GetProperty();
        }
        OutLeaf = Indirection.GetProperty();
    }
    return OutRoot != nullptr && OutLeaf != nullptr;
}

bool IsDispatcher(const FProperty* Property)
{
    const FStructProperty* StructProperty = CastField<FStructProperty>(Property);
    return StructProperty != nullptr
        && StructProperty->Struct == FStateTreeDelegateDispatcher::StaticStruct();
}

bool IsListener(const FProperty* Property)
{
    const FStructProperty* StructProperty = CastField<FStructProperty>(Property);
    return StructProperty != nullptr
        && (StructProperty->Struct == FStateTreeDelegateListener::StaticStruct()
            || StructProperty->Struct == FStateTreeTransitionDelegateListener::StaticStruct());
}

bool IsUnsupportedOutputBindingProperty(const FProperty* Property)
{
    const FStructProperty* StructProperty = CastField<FStructProperty>(Property);
    return StructProperty != nullptr
        && StructProperty->Struct != nullptr
        && (StructProperty->Struct->IsChildOf(FStateTreeDelegateDispatcher::StaticStruct())
            || StructProperty->Struct->IsChildOf(FStateTreeDelegateListener::StaticStruct())
            || StructProperty->Struct->IsChildOf(FStateTreeTransitionDelegateListener::StaticStruct())
            || StructProperty->Struct->IsChildOf(FStateTreePropertyRef::StaticStruct()));
}

bool IsRootParameterDelegateDispatcherBindingEnabled()
{
    const IConsoleVariable* Variable = IConsoleManager::Get().FindConsoleVariable(
        TEXT("StateTree.Compiler.EnableParameterDelegateDispatcherBinding"));
    return Variable != nullptr && Variable->GetBool();
}

bool IsTargetDirectWritable(
    const UStateTreeEditorData& EditorData,
    const FProperty& RootProperty)
{
    return RootProperty.GetFName()
            == GET_MEMBER_NAME_CHECKED(UStateTreeEditorData, GlobalTasksCompletion)
        && EditorData.Schema != nullptr
        && EditorData.Schema->AllowTasksCompletion();
}

bool IsStateStructuralProperty(const FProperty& RootProperty)
{
    const FName Name = RootProperty.GetFName();
    return Name == GET_MEMBER_NAME_CHECKED(UStateTreeState, Children)
        || Name == GET_MEMBER_NAME_CHECKED(UStateTreeState, EnterConditions)
        || Name == GET_MEMBER_NAME_CHECKED(UStateTreeState, Tasks)
        || Name == GET_MEMBER_NAME_CHECKED(UStateTreeState, SingleTask)
        || Name == GET_MEMBER_NAME_CHECKED(UStateTreeState, Considerations)
        || Name == GET_MEMBER_NAME_CHECKED(UStateTreeState, Transitions)
        || Name == GET_MEMBER_NAME_CHECKED(UStateTreeState, Parameters)
        || Name == GET_MEMBER_NAME_CHECKED(UStateTreeState, ID)
        || Name == GET_MEMBER_NAME_CHECKED(UStateTreeState, Parent);
}

bool IsStateDirectWritable(
    const UStateTreeEditorData& EditorData,
    const FProperty& RootProperty)
{
    if (IsStateStructuralProperty(RootProperty))
    {
        return false;
    }
    const UStateTreeSchema* Schema = EditorData.Schema;
    const FName Name = RootProperty.GetFName();
    if (Name == GET_MEMBER_NAME_CHECKED(UStateTreeState, TasksCompletion))
    {
        return Schema != nullptr && Schema->AllowTasksCompletion();
    }
    if (Name == GET_MEMBER_NAME_CHECKED(UStateTreeState, CustomTickRate)
        || Name == GET_MEMBER_NAME_CHECKED(UStateTreeState, bHasCustomTickRate))
    {
        return Schema != nullptr && Schema->IsScheduledTickAllowed();
    }
    return true;
}

bool IsTransitionStructuralProperty(const FProperty& RootProperty)
{
    const FName Name = RootProperty.GetFName();
    return Name == GET_MEMBER_NAME_CHECKED(FStateTreeTransition, Conditions)
        || Name == GET_MEMBER_NAME_CHECKED(FStateTreeTransition, ID)
        || Name == GET_MEMBER_NAME_CHECKED(FStateTreeTransition, DelegateListener);
}

const TCHAR* BoolText(const bool bValue)
{
    return bValue ? TEXT("true") : TEXT("false");
}

template<typename EnumType, typename PredicateType>
FString AllowedEnumValues(PredicateType&& Predicate)
{
    FString Result;
    const UEnum* Enum = StaticEnum<EnumType>();
    if (Enum == nullptr)
    {
        return TEXT("unavailable");
    }
    for (int32 EnumIndex = 0; EnumIndex < Enum->NumEnums(); ++EnumIndex)
    {
        const int64 Value = Enum->GetValueByIndex(EnumIndex);
        const FString Name = Enum->GetNameStringByIndex(EnumIndex);
        if (Value == INDEX_NONE
            || Name.EndsWith(TEXT("MAX"))
            || !Predicate(static_cast<EnumType>(Value)))
        {
            continue;
        }
        Result += (Result.IsEmpty() ? FString() : TEXT(", ")) + Name;
    }
    return Result.IsEmpty() ? TEXT("none") : Result;
}

bool CanCreateSchemaApprovedState(const UStateTreeSchema* Schema)
{
    if (Schema == nullptr)
    {
        return false;
    }
    bool bHasOrdinarySelection = false;
    if (const UEnum* SelectionEnum = StaticEnum<EStateTreeStateSelectionBehavior>())
    {
        for (int32 Index = 0; Index < SelectionEnum->NumEnums(); ++Index)
        {
            const int64 RawValue = SelectionEnum->GetValueByIndex(Index);
            const FString Name = SelectionEnum->GetNameStringByIndex(Index);
            if (RawValue != INDEX_NONE
                && !Name.EndsWith(TEXT("MAX"))
                && Schema->IsStateSelectionAllowed(
                    static_cast<EStateTreeStateSelectionBehavior>(RawValue)))
            {
                bHasOrdinarySelection = true;
                break;
            }
        }
    }
    if (const UEnum* TypeEnum = StaticEnum<EStateTreeStateType>())
    {
        for (int32 Index = 0; Index < TypeEnum->NumEnums(); ++Index)
        {
            const int64 RawValue = TypeEnum->GetValueByIndex(Index);
            const FString Name = TypeEnum->GetNameStringByIndex(Index);
            if (RawValue == INDEX_NONE || Name.EndsWith(TEXT("MAX")))
            {
                continue;
            }
            const EStateTreeStateType Type = static_cast<EStateTreeStateType>(RawValue);
            if (!Schema->IsStateTypeAllowed(Type))
            {
                continue;
            }
            if (Type == EStateTreeStateType::Linked
                || Type == EStateTreeStateType::LinkedAsset
                || bHasOrdinarySelection)
            {
                return true;
            }
        }
    }
    return false;
}

FString DescribeNativeSchemaCapabilities(const UStateTreeEditorData& EditorData)
{
    const UStateTreeSchema* Schema = EditorData.Schema;
    if (Schema == nullptr)
    {
        return TEXT("\nnative schema capabilities:\n  Schema: unavailable");
    }
    const FString AllowedTypes = AllowedEnumValues<EStateTreeStateType>(
        [&](const EStateTreeStateType Value)
        {
            return Schema->IsStateTypeAllowed(Value);
        });
    const FString AllowedSelection = AllowedEnumValues<EStateTreeStateSelectionBehavior>(
        [&](const EStateTreeStateSelectionBehavior Value)
        {
            return Schema->IsStateSelectionAllowed(Value);
        });
    FString Result = FString::Printf(
        TEXT("\nnative schema capabilities:")
        TEXT("\n  AllowEvaluators: %s")
        TEXT("\n  AllowGlobalParameters: %s")
        TEXT("\n  AllowTasksCompletion: %s")
        TEXT("\n  IsScheduledTickAllowed: %s")
        TEXT("\n  AllowEnterConditions: %s")
        TEXT("\n  AllowUtilityConsiderations: %s")
        TEXT("\n  AllowMultipleTasks: %s")
        TEXT("\n  IsStateTypeAllowed: %s")
        TEXT("\n  IsStateSelectionAllowed: %s"),
        BoolText(Schema->AllowEvaluators()),
        BoolText(Schema->AllowGlobalParameters()),
        BoolText(Schema->AllowTasksCompletion()),
        BoolText(Schema->IsScheduledTickAllowed()),
        BoolText(Schema->AllowEnterConditions()),
        BoolText(Schema->AllowUtilityConsiderations()),
        BoolText(Schema->AllowMultipleTasks()),
        *AllowedTypes,
        *AllowedSelection);
    if (EditorData.EditorSchema != nullptr)
    {
        Result += FString::Printf(
            TEXT("\n  EditorSchema.AllowExtensions: %s"),
            BoolText(EditorData.EditorSchema->AllowExtensions()));
    }
    return Result;
}

FString DescribeTargetDestinations(const UStateTreeEditorData& EditorData)
{
    const UStateTreeSchema* Schema = EditorData.Schema;
    const bool bCanCreateState = CanCreateSchemaApprovedState(Schema);
    return FString::Printf(
        TEXT("\ncreation destinations:")
        TEXT("\n  SubTrees: %s")
        TEXT("\n  Evaluators: %s")
        TEXT("\n  GlobalTasks: %s")
        TEXT("\n  RootParameters: %s")
        TEXT("\ndirect patch:")
        TEXT("\n  GlobalTasksCompletion: %s"),
        bCanCreateState ? TEXT("available") : TEXT("unavailable"),
        Schema != nullptr && Schema->AllowEvaluators() ? TEXT("available") : TEXT("unavailable"),
        Schema != nullptr ? TEXT("available") : TEXT("unavailable"),
        Schema != nullptr && Schema->AllowGlobalParameters() ? TEXT("available") : TEXT("unavailable"),
        Schema != nullptr && Schema->AllowTasksCompletion()
            ? TEXT("set/reset available")
            : TEXT("read-only"));
}

FString DescribeStateDestinations(
    const UStateTreeEditorData& EditorData,
    const UStateTreeState& State)
{
    const UStateTreeSchema* Schema = EditorData.Schema;
    const bool bCanCreateState = CanCreateSchemaApprovedState(Schema);
    const bool bDirectState = State.Type == EStateTreeStateType::State
        || State.Type == EStateTreeStateType::Subtree;
    const bool bChildren = State.Type != EStateTreeStateType::Linked
        && State.Type != EStateTreeStateType::LinkedAsset;
    return FString::Printf(
        TEXT("\ncreation destinations:")
        TEXT("\n  Children: %s")
        TEXT("\n  EnterConditions: %s")
        TEXT("\n  Tasks: %s")
        TEXT("\n  SingleTask: %s")
        TEXT("\n  Considerations: %s")
        TEXT("\n  Transitions: available")
        TEXT("\n  Parameters: %s"),
        bChildren && bCanCreateState
            ? TEXT("available")
            : TEXT("unavailable for current Type or Schema"),
        Schema != nullptr && Schema->AllowEnterConditions()
            ? TEXT("available")
            : TEXT("unavailable"),
        Schema != nullptr && Schema->AllowMultipleTasks() && bDirectState
            ? TEXT("available")
            : TEXT("unavailable"),
        Schema != nullptr && !Schema->AllowMultipleTasks() && bDirectState
            ? TEXT("available")
            : TEXT("unavailable"),
        Schema != nullptr && Schema->AllowUtilityConsiderations()
            ? TEXT("available")
            : TEXT("unavailable"),
        !State.Parameters.bFixedLayout ? TEXT("available") : TEXT("unavailable; fixed layout"));
}

const TCHAR* UsageName(const EStateTreePropertyUsage Usage)
{
    switch (Usage)
    {
    case EStateTreePropertyUsage::Context: return TEXT("context");
    case EStateTreePropertyUsage::Input: return TEXT("input");
    case EStateTreePropertyUsage::Parameter: return TEXT("parameter");
    case EStateTreePropertyUsage::Output: return TEXT("output");
    default: return TEXT("native");
    }
}

FString PropertyTypeText(const FProperty& Property)
{
    return NativePropertyTypeText(&Property);
}

void FinalizeCapabilities(
    FResolvedMember& OutMember,
    const bool bNodeTemplate,
    const bool bEventBindingSource)
{
    OutMember.bReadable = OutMember.LeafProperty != nullptr;
    OutMember.Usage = UE::StateTree::GetUsageFromMetaData(OutMember.RootProperty);
    const bool bEdit = OutMember.LeafProperty != nullptr
        && OutMember.LeafProperty->HasAnyPropertyFlags(CPF_Edit)
        && !OutMember.LeafProperty->HasAnyPropertyFlags(CPF_EditConst | CPF_DisableEditOnInstance);
    OutMember.bWritable = bEdit && !OutMember.bReadOnlyOwner;
    OutMember.bResettable = OutMember.bWritable;

    const bool bPropertyRef = OutMember.RootProperty != nullptr
        && UE::StateTree::PropertyRefHelpers::IsPropertyRef(*OutMember.RootProperty);
    const bool bListener = IsListener(OutMember.RootProperty);
    const bool bDispatcher = IsDispatcher(OutMember.RootProperty);
    OutMember.bBindingTarget = !bDispatcher
        && !OutMember.bReadOnlyOwner
        && (!bNodeTemplate || bPropertyRef || bListener)
        && (bPropertyRef
            || bListener
            || OutMember.Usage == EStateTreePropertyUsage::Parameter
            || ((OutMember.Usage == EStateTreePropertyUsage::Input
                    || OutMember.Usage == EStateTreePropertyUsage::Context)
                && OutMember.NativePath.NumSegments() == 1));
    OutMember.bBindingSource = !bListener
        && (bDispatcher
            || OutMember.bReadOnlyOwner
            || OutMember.Usage == EStateTreePropertyUsage::Output
            || OutMember.Usage == EStateTreePropertyUsage::Parameter
            || OutMember.Usage == EStateTreePropertyUsage::Context);

    // State/Transition editor structs are not generic Binding containers. Only
    // their native Event/Delegate surfaces participate in Property Bindings.
    if (OutMember.OwnerKind == TEXT("state"))
    {
        OutMember.bBindingTarget = false;
        OutMember.bBindingSource = bEventBindingSource;
    }
    else if (OutMember.OwnerKind == TEXT("transition"))
    {
        const bool bDelegate = !OutMember.SalPath.IsEmpty()
            && OutMember.SalPath[0] == TEXT("DelegateListener");
        OutMember.bBindingTarget = bDelegate && bListener;
        OutMember.bBindingSource = bEventBindingSource || bDispatcher;
    }
}

using FSchemaWriteFilter = TFunction<bool(const FProperty&)>;

enum class ESchemaBindingSurface : uint8
{
    None,
    NodeTemplate,
    NodeInstance,
    ReadOnlySource,
    TransitionDelegate,
};

bool AppendStructDescription(
    FExactSchemaTextBuilder& Builder,
    const UStruct& Struct,
    const bool bReadOnly,
    const FString& Prefix = FString(),
    const FSchemaWriteFilter& WriteFilter = FSchemaWriteFilter(),
    const ESchemaBindingSurface BindingSurface = ESchemaBindingSurface::None,
    const bool bIncludeHeader = true)
{
    if (bIncludeHeader && !Builder.Append(TEXT("schema:\nfields:")))
    {
        return false;
    }
    for (TFieldIterator<FProperty> It(&Struct, EFieldIteratorFlags::IncludeSuper); It; ++It)
    {
        const FProperty* Property = *It;
        if (Property == nullptr || Property->HasAnyPropertyFlags(CPF_Transient | CPF_DuplicateTransient))
        {
            continue;
        }
        const EStateTreePropertyUsage Usage = UE::StateTree::GetUsageFromMetaData(Property);
        const bool bWritable = !bReadOnly
            && Property->HasAnyPropertyFlags(CPF_Edit)
            && !Property->HasAnyPropertyFlags(CPF_EditConst | CPF_DisableEditOnInstance)
            && (!WriteFilter || WriteFilter(*Property));
        FString Capabilities = bWritable ? TEXT("read/write/reset") : TEXT("read-only");
        if (BindingSurface != ESchemaBindingSurface::None)
        {
            const bool bPropertyRef = UE::StateTree::PropertyRefHelpers::IsPropertyRef(*Property);
            const bool bListener = IsListener(Property);
            const bool bDispatcher = IsDispatcher(Property);
            const bool bTransitionDelegate = BindingSurface
                == ESchemaBindingSurface::TransitionDelegate;
            const bool bBindingSource = !bTransitionDelegate
                && !bListener
                && (bDispatcher
                    || BindingSurface == ESchemaBindingSurface::ReadOnlySource
                    || Usage == EStateTreePropertyUsage::Output
                    || Usage == EStateTreePropertyUsage::Parameter
                    || Usage == EStateTreePropertyUsage::Context);
            const bool bBindingTarget = bTransitionDelegate
                ? bListener
                : BindingSurface != ESchemaBindingSurface::ReadOnlySource
                    && (BindingSurface != ESchemaBindingSurface::NodeTemplate
                        || bPropertyRef
                        || bListener)
                    && (bPropertyRef
                        || bListener
                        || Usage == EStateTreePropertyUsage::Input
                        || Usage == EStateTreePropertyUsage::Context
                        || Usage == EStateTreePropertyUsage::Parameter);
            if (bBindingSource)
            {
                Capabilities += TEXT("; binding source");
            }
            if (bBindingTarget)
            {
                Capabilities += TEXT("; binding target");
            }
        }
        FString FieldText = FString::Printf(
            TEXT("\n  %s%s: %s; %s; usage=%s"),
            *Prefix,
            *Property->GetName(),
            *PropertyTypeText(*Property),
            *Capabilities,
            UsageName(Usage));
        const FString EditCondition = Property->GetMetaData(TEXT("EditCondition"));
        if (!EditCondition.IsEmpty())
        {
            FieldText += TEXT("; active when ") + EditCondition;
        }
        if (!Builder.Append(FieldText, 1))
        {
            return false;
        }
    }
    return Builder.Append(
        TEXT("\nconstraints:\n  Exact fields retain UE native names and types; Binding eligibility is destination- and execution-path-dependent."));
}

bool AppendEventBindingSurface(
    FExactSchemaTextBuilder& Builder,
    const FStateTreeEventDesc& Descriptor,
    const FString& Prefix,
    const FString& ActivationCondition,
    const bool bCanonicalBindingSource)
{
    const TCHAR* BindingCapability = bCanonicalBindingSource
        ? TEXT("; binding source")
        : TEXT("");
    if (const FProperty* Tag = FindFProperty<FProperty>(
            FStateTreeEvent::StaticStruct(),
            GET_MEMBER_NAME_CHECKED(FStateTreeEvent, Tag)))
    {
        if (!Builder.Append(
                FString::Printf(
                    TEXT("\n  %sTag: %s; read-only%s"),
                    *Prefix,
                    *PropertyTypeText(*Tag),
                    BindingCapability),
                1))
        {
            return false;
        }
    }
    if (Descriptor.PayloadStruct != nullptr)
    {
        for (TFieldIterator<FProperty> It(Descriptor.PayloadStruct, EFieldIteratorFlags::IncludeSuper); It; ++It)
        {
            const FProperty* Property = *It;
            if (Property == nullptr
                || Property->HasAnyPropertyFlags(CPF_Transient | CPF_DuplicateTransient))
            {
                continue;
            }
            if (!Builder.Append(
                    FString::Printf(
                        TEXT("\n  %sPayload.%s: %s; read-only%s"),
                        *Prefix,
                        *Property->GetName(),
                        *PropertyTypeText(*Property),
                        BindingCapability),
                    1))
            {
                return false;
            }
        }
    }
    else
    {
        if (!Builder.Append(FString::Printf(
                TEXT("\n  %sPayload.*: unavailable until PayloadStruct is authored"),
                *Prefix)))
        {
            return false;
        }
    }
    if (const FProperty* Origin = FindFProperty<FProperty>(
            FStateTreeEvent::StaticStruct(),
            GET_MEMBER_NAME_CHECKED(FStateTreeEvent, Origin)))
    {
        if (!Builder.Append(
                FString::Printf(
                    TEXT("\n  %sOrigin: %s; read-only%s"),
                    *Prefix,
                    *PropertyTypeText(*Origin),
                    BindingCapability),
                1))
        {
            return false;
        }
    }
    if (!Builder.Append(TEXT("\n  activation: ") + ActivationCondition))
    {
        return false;
    }
    if (!bCanonicalBindingSource)
    {
        if (!Builder.Append(TEXT("\n  Binding source: unavailable until the active Event StructID uniquely identifies this UE Binding surface")))
        {
            return false;
        }
    }
    return true;
}
}

bool FExactSchemaTextBuilder::Append(const FString& AddedText, const int32 AddedFields)
{
    const int64 AddedCharacters = AddedText.Len();
    if (bExceeded
        || AddedFields < 0
        || AddedFields > MaxFields - Fields
        || AddedCharacters < 0
        || AddedCharacters > MaxCharacters - Characters)
    {
        bExceeded = true;
        return false;
    }
    Text += AddedText;
    Fields += AddedFields;
    Characters += AddedCharacters;
    return true;
}

bool FExactSchemaTextBuilder::Finish(FString& OutText, FString& OutError) const
{
    if (bExceeded)
    {
        OutText.Reset();
        OutError = FString::Printf(
            TEXT("Exact schema exceeds the hard limit of %d fields or %lld text characters."),
            MaxFields,
            static_cast<long long>(MaxCharacters));
        return false;
    }
    OutText = Text;
    OutError.Reset();
    return true;
}

bool ResolveCanonicalContext(
    const UStateTreeEditorData& EditorData,
    const FGuid& ContextId,
    const FStateTreeExternalDataDesc*& OutDescriptor,
    FString& OutMessage)
{
    OutDescriptor = nullptr;
    OutMessage.Reset();
    if (!ContextId.IsValid())
    {
        OutMessage = TEXT("Context object id is invalid.");
        return false;
    }
    if (EditorData.Schema == nullptr)
    {
        OutMessage = TEXT("The current StateTree Schema exposes no Context Data.");
        return false;
    }

    int32 DescriptorMatches = 0;
    for (const FStateTreeExternalDataDesc& Descriptor :
        EditorData.Schema->GetContextDataDescs())
    {
        if (Descriptor.ID == ContextId)
        {
            ++DescriptorMatches;
            OutDescriptor = &Descriptor;
        }
    }
    if (DescriptorMatches != 1 || OutDescriptor == nullptr)
    {
        OutDescriptor = nullptr;
        OutMessage = DescriptorMatches == 0
            ? TEXT("Context object id is not declared by the current StateTree Schema.")
            : TEXT("Context object id is duplicated by the current StateTree Schema.");
        return false;
    }
    if (OutDescriptor->Struct == nullptr)
    {
        OutDescriptor = nullptr;
        OutMessage = TEXT("Context object has no native Struct and cannot be a Binding surface.");
        return false;
    }

    const FPropertyBindingDataView ExpectedView(OutDescriptor->Struct, nullptr);
    if (!ResolveCanonicalBindableSurface(
            EditorData,
            ContextId,
            ExpectedView,
            EStateTreeBindableStructSource::Context,
            OutMessage))
    {
        OutDescriptor = nullptr;
        return false;
    }
    return true;
}

bool ResolveMember(
    const UStateTree& StateTree,
    const UStateTreeEditorData& EditorData,
    const FString& OwnerKind,
    const FString& OwnerId,
    const TArray<FString>& SalPath,
    FResolvedMember& OutMember,
    FString& OutMessage,
    const EMemberPurpose Purpose)
{
    OutMember = FResolvedMember();
    OutMember.OwnerKind = OwnerKind;
    OutMember.OwnerId = OwnerId;
    OutMember.SalPath = SalPath;
    OutMessage.Reset();

    FGuid StructId;
    TArray<FString> NativeSegments;
    bool bNodeTemplate = false;
    bool bEventBindingSource = false;
    bool bParameterBindingAllowed = true;
    if (OwnerKind == TEXT("target"))
    {
        if (OwnerId != StateTree.GetPathName() || SalPath.IsEmpty())
        {
            OutMessage = TEXT("Target member requires the current bound StateTree target and one exact field path.");
            return false;
        }
        StructId = FGuid::NewDeterministicGuid(
            StateTree.GetPathName() + TEXT(":EditorData"));
        NativeSegments = SalPath;
        OutMember.OwnerView = FPropertyBindingDataView(
            const_cast<UStateTreeEditorData*>(&EditorData));
    }
    else if (OwnerKind == TEXT("node"))
    {
        FGuid NodeId;
        const FStateTreeEditorNode* Node = ParseGuid(OwnerId, NodeId) ? FindUniqueNode(EditorData, NodeId) : nullptr;
        if (Node == nullptr || SalPath.Num() < 2)
        {
            OutMessage = TEXT("Node member requires one exact node@id.Instance or node@id.Node field path.");
            return false;
        }
        NativeSegments = SalPath;
        const FString Surface = NativeSegments[0];
        NativeSegments.RemoveAt(0);
        if (Surface == TEXT("Instance"))
        {
            StructId = Node->ID;
            OutMember.OwnerView = Node->GetInstance();
        }
        else if (Surface == TEXT("Node"))
        {
            StructId = Node->GetNodeID();
            const TStructView<FStateTreeNodeBase> View = Node->GetNode();
            OutMember.OwnerView = FPropertyBindingDataView(View.GetScriptStruct(), View.GetMemory());
            bNodeTemplate = true;
        }
        else
        {
            OutMessage = TEXT("Node reflected fields are rooted at Instance or Node.");
            return false;
        }
        OutMember.bPropertyFunctionOwner = Node->Node.GetScriptStruct() != nullptr
            && Node->Node.GetScriptStruct()->IsChildOf(FStateTreePropertyFunctionBase::StaticStruct());
    }
    else if (OwnerKind == TEXT("object"))
    {
        FGuid ContextId;
        if (!ParseGuid(OwnerId, ContextId) || SalPath.IsEmpty())
        {
            OutMessage = TEXT("Context member requires one exact object@id field path.");
            return false;
        }
        StructId = ContextId;
        const FStateTreeExternalDataDesc* ContextDesc = nullptr;
        if (!ResolveCanonicalContext(
                EditorData,
                ContextId,
                ContextDesc,
                OutMessage))
        {
            return false;
        }
        // Context values exist only at runtime. Resolve against the unique
        // Schema-declared type instead of UE's first-match ID lookup, which is
        // intentionally tolerant of duplicate native descriptors.
        OutMember.OwnerView = FPropertyBindingDataView(ContextDesc->Struct, nullptr);
        NativeSegments = SalPath;
        OutMember.bReadOnlyOwner = true;
    }
    else if (OwnerKind == TEXT("parameter"))
    {
        FGuid ContainerId;
        FGuid PropertyId;
        if (!ParseParameterId(OwnerId, ContainerId, PropertyId))
        {
            OutMessage = TEXT("Parameter identity must be container-guid/property-guid.");
            return false;
        }
        const UStateTreeState* OwnerState = nullptr;
        const FInstancedPropertyBag* Bag = FindParameterBag(EditorData, ContainerId, &OwnerState);
        const FPropertyBagPropertyDesc* Desc = Bag != nullptr ? Bag->FindPropertyDescByID(PropertyId) : nullptr;
        if (Bag == nullptr || Desc == nullptr || Bag->GetPropertyBagStruct() == nullptr)
        {
            OutMessage = TEXT("Parameter does not exist in the current StateTree.");
            return false;
        }
        OutMember.OwnerView = FPropertyBindingDataView(
            const_cast<FInstancedPropertyBag*>(Bag)->GetMutableValue());
        StructId = ContainerId;
        const bool bFixedLayout = OwnerState != nullptr && OwnerState->Parameters.bFixedLayout;
        bParameterBindingAllowed = OwnerState == nullptr
            || OwnerState->Type != EStateTreeStateType::Subtree;
        OutMember.bLayoutEditable = !bFixedLayout;
        OutMember.bValueOverrideWritable = true;
        OutMember.ParameterContainerId = ContainerId;
        OutMember.ParameterPropertyId = PropertyId;
        OutMember.ParameterBag = Bag;
        OutMember.ParameterDesc = Desc;
        OutMember.bRootParameterOwner = OwnerState == nullptr;

        TArray<FString> ValuePath = SalPath;
        if (!ValuePath.IsEmpty()
            && (ValuePath[0] == TEXT("Name")
                || ValuePath[0] == TEXT("type")
                || ValuePath[0] == TEXT("MetaData")))
        {
            const FString Surface = ValuePath[0];
            OutMember.Surface = Surface == TEXT("Name")
                ? EMemberSurface::ParameterName
                : Surface == TEXT("type")
                    ? EMemberSurface::ParameterType
                    : EMemberSurface::ParameterMetaData;
            OutMember.bReadable = true;
            OutMember.bWritable = OutMember.bLayoutEditable;
            OutMember.bResettable = false;
            OutMember.bBindingSource = false;
            OutMember.bBindingTarget = false;
            if (Purpose != EMemberPurpose::ReadOrEdit)
            {
                OutMessage = TEXT("Parameter descriptor fields are not StateTree Binding endpoints.");
                return false;
            }
            return true;
        }
        if (!ValuePath.IsEmpty() && ValuePath[0] == TEXT("Value"))
        {
            ValuePath.RemoveAt(0);
        }
        OutMember.Surface = EMemberSurface::ParameterValue;
        NativeSegments.Add(Desc->Name.ToString());
        NativeSegments.Append(ValuePath);
    }
    else if (OwnerKind == TEXT("state"))
    {
        FGuid StateId;
        const UStateTreeState* State = ParseGuid(OwnerId, StateId) ? FindUniqueState(EditorData, StateId) : nullptr;
        if (State == nullptr || SalPath.IsEmpty())
        {
            OutMessage = TEXT("State member does not exist or is ambiguous.");
            return false;
        }
        const bool bBindingEventPath = Purpose == EMemberPurpose::BindingSource
            && SalPath.Num() >= 2
            && SalPath[0] == TEXT("RequiredEventToEnter")
            && (SalPath[1] == TEXT("Tag")
                || SalPath[1] == TEXT("Payload")
                || SalPath[1] == TEXT("Origin"));
        if (bBindingEventPath)
        {
            StructId = State->GetEventID();
            NativeSegments = SalPath;
            NativeSegments.RemoveAt(0);
            FStateTreeEvent& Event = const_cast<UStateTreeState*>(State)
                ->RequiredEventToEnter.GetTemporaryEvent();
            OutMember.OwnerView = FPropertyBindingDataView(
                FStateTreeEvent::StaticStruct(),
                &Event);
            OutMember.bReadOnlyOwner = true;
            bEventBindingSource = true;
        }
        else
        {
            StructId = StateId;
            NativeSegments = SalPath;
            OutMember.OwnerView = FPropertyBindingDataView(const_cast<UStateTreeState*>(State));
        }
    }
    else if (OwnerKind == TEXT("transition"))
    {
        FGuid TransitionId;
        const FStateTreeTransition* Transition = ParseGuid(OwnerId, TransitionId)
            ? FindUniqueTransition(EditorData, TransitionId)
            : nullptr;
        if (Transition == nullptr || SalPath.IsEmpty())
        {
            OutMessage = TEXT("Transition member does not exist or is ambiguous.");
            return false;
        }
        const bool bBindingEventPath = Purpose == EMemberPurpose::BindingSource
            && SalPath.Num() >= 2
            && SalPath[0] == TEXT("RequiredEvent")
            && (SalPath[1] == TEXT("Tag")
                || SalPath[1] == TEXT("Payload")
                || SalPath[1] == TEXT("Origin"));
        if (bBindingEventPath)
        {
            StructId = Transition->GetEventID();
            NativeSegments = SalPath;
            NativeSegments.RemoveAt(0);
            FStateTreeEvent& Event = const_cast<FStateTreeTransition*>(Transition)
                ->RequiredEvent.GetTemporaryEvent();
            OutMember.OwnerView = FPropertyBindingDataView(
                FStateTreeEvent::StaticStruct(),
                &Event);
            OutMember.bReadOnlyOwner = true;
            bEventBindingSource = true;
        }
        else
        {
            StructId = TransitionId;
            NativeSegments = SalPath;
            OutMember.OwnerView = FPropertyBindingDataView(
                FStateTreeTransition::StaticStruct(),
                const_cast<FStateTreeTransition*>(Transition));
        }
    }
    else
    {
        OutMessage = TEXT("This StateTree object kind does not expose member schema.");
        return false;
    }

    if (!BuildNativePath(StructId, NativeSegments, OutMember.NativePath, OutMessage)
        || !ResolveNativePath(
            OutMember.OwnerView,
            OutMember.NativePath,
            OutMember.RootProperty,
            OutMember.LeafProperty,
            OutMessage))
    {
        return false;
    }
    if (Purpose != EMemberPurpose::ReadOrEdit
        && !IsCanonicalResolvedMember(EditorData, OutMember, OutMessage))
    {
        return false;
    }
    FinalizeCapabilities(OutMember, bNodeTemplate, bEventBindingSource);
    if (OwnerKind == TEXT("target"))
    {
        const bool bDirectWritable = OutMember.RootProperty != nullptr
            && IsTargetDirectWritable(EditorData, *OutMember.RootProperty);
        OutMember.bWritable = OutMember.bWritable && bDirectWritable;
        OutMember.bResettable = OutMember.bResettable && bDirectWritable;
        OutMember.bBindingSource = false;
        OutMember.bBindingTarget = false;
    }
    else if (OwnerKind == TEXT("state")
        && OutMember.RootProperty != nullptr
        && !IsStateDirectWritable(EditorData, *OutMember.RootProperty))
    {
        OutMember.bWritable = false;
        OutMember.bResettable = false;
        if (IsStateStructuralProperty(*OutMember.RootProperty))
        {
            OutMember.bBindingSource = false;
            OutMember.bBindingTarget = false;
        }
    }
    else if (OwnerKind == TEXT("state")
        && OutMember.RootProperty != nullptr
        && OutMember.RootProperty->GetFName()
            == GET_MEMBER_NAME_CHECKED(UStateTreeState, LinkedSubtree)
        && SalPath.Num() != 1)
    {
        // UE edits LinkedSubtree as one semantic State link. Allowing a caller
        // to write only ID, Name, or LinkType would bypass SetLinkedState(),
        // its parameter synchronization, and the Patch cascade plan.
        OutMember.bWritable = false;
        OutMember.bResettable = false;
    }
    else if (OwnerKind == TEXT("transition")
        && OutMember.RootProperty != nullptr
        && IsTransitionStructuralProperty(*OutMember.RootProperty))
    {
        OutMember.bWritable = false;
        OutMember.bResettable = false;
        if (OutMember.RootProperty->GetFName()
            != GET_MEMBER_NAME_CHECKED(FStateTreeTransition, DelegateListener))
        {
            OutMember.bBindingSource = false;
            OutMember.bBindingTarget = false;
        }
    }
    else if (OwnerKind == TEXT("transition")
        && OutMember.RootProperty != nullptr
        && OutMember.RootProperty->GetFName()
            == GET_MEMBER_NAME_CHECKED(FStateTreeTransition, State)
        && SalPath.Num() != 1)
    {
        // A transition destination is one native FStateTreeStateLink value.
        // Partial writes can create mismatched ID/Name/LinkType tuples and
        // evade the semantic link resolver used by Patch.
        OutMember.bWritable = false;
        OutMember.bResettable = false;
    }
    else if (OwnerKind == TEXT("parameter"))
    {
        // A fixed layout freezes descriptor identity/layout, not its authored
        // local value override or its Property Binding endpoint.
        OutMember.bWritable = OutMember.bValueOverrideWritable;
        OutMember.bResettable = OutMember.bValueOverrideWritable;
        OutMember.bBindingTarget = OutMember.bBindingTarget && bParameterBindingAllowed;
        if (OutMember.bRootParameterOwner
            && IsDispatcher(OutMember.RootProperty)
            && !IsRootParameterDelegateDispatcherBindingEnabled())
        {
            OutMember.bBindingSource = false;
        }
    }
    return true;
}

bool ResolveMemberReference(
    const UStateTree& StateTree,
    const UStateTreeEditorData& EditorData,
    const TSharedPtr<FJsonObject>& MemberRef,
    FResolvedMember& OutMember,
    FString& OutMessage,
    const EMemberPurpose Purpose)
{
    FString Kind;
    const TSharedPtr<FJsonObject>* Owner = nullptr;
    const TArray<TSharedPtr<FJsonValue>>* PathValues = nullptr;
    if (!MemberRef.IsValid()
        || !MemberRef->TryGetStringField(TEXT("kind"), Kind)
        || Kind != TEXT("member")
        || !MemberRef->TryGetObjectField(TEXT("object"), Owner)
        || Owner == nullptr
        || !(*Owner).IsValid()
        || !MemberRef->TryGetArrayField(TEXT("path"), PathValues)
        || PathValues == nullptr)
    {
        OutMessage = TEXT("Expected one normalized stable Member Reference.");
        return false;
    }
    FString OwnerKind;
    FString OwnerId;
    if (!(*Owner)->TryGetStringField(TEXT("kind"), OwnerKind))
    {
        OutMessage = TEXT("StateTree member owner must be one stable object reference.");
        return false;
    }
    if (OwnerKind == TEXT("local"))
    {
        FString LocalName;
        if (!(*Owner)->TryGetStringField(TEXT("name"), LocalName)
            || LocalName.IsEmpty())
        {
            OutMessage = TEXT("StateTree target member owner requires the current target alias.");
            return false;
        }
        OwnerKind = TEXT("target");
        OwnerId = StateTree.GetPathName();
    }
    else if (!(*Owner)->TryGetStringField(TEXT("id"), OwnerId))
    {
        OutMessage = TEXT("StateTree member owner must be one stable object reference.");
        return false;
    }
    TArray<FString> Path;
    for (const TSharedPtr<FJsonValue>& Value : *PathValues)
    {
        FString Segment;
        double Number = 0;
        if (Value.IsValid() && Value->TryGetString(Segment))
        {
            Path.Add(MoveTemp(Segment));
        }
        else if (Value.IsValid() && Value->TryGetNumber(Number)
            && Number >= 0 && Number <= MAX_int32 && FMath::FloorToDouble(Number) == Number)
        {
            Path.Add(LexToString(static_cast<int32>(Number)));
        }
        else
        {
            OutMessage = TEXT("StateTree member path contains an invalid segment.");
            return false;
        }
    }
    return ResolveMember(
        StateTree,
        EditorData,
        OwnerKind,
        OwnerId,
        Path,
        OutMember,
        OutMessage,
        Purpose);
}

bool AreBindingCompatible(
    const UStateTreeEditorData& EditorData,
    const FResolvedMember& Source,
    const FResolvedMember& Target,
    FString& OutMessage)
{
    OutMessage.Reset();
    FString CanonicalError;
    if (!IsCanonicalResolvedMember(EditorData, Source, CanonicalError))
    {
        OutMessage = TEXT("Binding source is stale or ambiguous: ") + CanonicalError;
        return false;
    }
    if (!IsCanonicalResolvedMember(EditorData, Target, CanonicalError))
    {
        OutMessage = TEXT("Binding target is stale or ambiguous: ") + CanonicalError;
        return false;
    }
    if (!Source.bBindingSource || !Target.bBindingTarget
        || Source.LeafProperty == nullptr || Target.LeafProperty == nullptr)
    {
        OutMessage = TEXT("Source or target is not an advertised StateTree Binding endpoint.");
        return false;
    }
    if (IsListener(Source.LeafProperty) || IsDispatcher(Target.LeafProperty))
    {
        OutMessage = TEXT("Delegate Bindings flow from Dispatcher to Listener.");
        return false;
    }
    const bool bNativeOutputBinding = Source.Usage == EStateTreePropertyUsage::Output
        && !Source.bPropertyFunctionOwner;
    if (bNativeOutputBinding)
    {
        if (Target.OwnerKind != TEXT("parameter"))
        {
            OutMessage = TEXT("A StateTree Output can write only to a global Parameter or State Parameter.");
            return false;
        }
        if (IsUnsupportedOutputBindingProperty(Source.RootProperty)
            || IsUnsupportedOutputBindingProperty(Source.LeafProperty)
            || IsUnsupportedOutputBindingProperty(Target.RootProperty)
            || IsUnsupportedOutputBindingProperty(Target.LeafProperty))
        {
            OutMessage = TEXT("StateTree Output Bindings do not accept Delegate or PropertyRef values.");
            return false;
        }
    }
    if (IsListener(Target.LeafProperty))
    {
        if (!IsDispatcher(Source.LeafProperty))
        {
            OutMessage = TEXT("A StateTree Delegate Listener accepts only a Dispatcher.");
            return false;
        }
    }
    else if (UE::PropertyBinding::GetPropertyCompatibility(Source.LeafProperty, Target.LeafProperty)
        == UE::PropertyBinding::EPropertyCompatibility::Incompatible)
    {
        OutMessage = TEXT("Native UE Property Binding types are incompatible.");
        return false;
    }

    const FGuid ExecutionOwnerId = bNativeOutputBinding
        ? Source.NativePath.GetStructID()
        : Target.NativePath.GetStructID();
    const FGuid CandidateId = bNativeOutputBinding
        ? Target.NativePath.GetStructID()
        : Source.NativePath.GetStructID();
    TArray<TInstancedStruct<FPropertyBindingBindableStructDescriptor>> Accessible;
    EditorData.GetBindableStructs(ExecutionOwnerId, Accessible);
    const bool bVisible = CandidateId == ExecutionOwnerId
        || Accessible.ContainsByPredicate([&](const TInstancedStruct<FPropertyBindingBindableStructDescriptor>& Item)
        {
            const FStateTreeBindableStructDesc* Desc = Item.GetPtr<FStateTreeBindableStructDesc>();
            return Desc != nullptr && Desc->ID == CandidateId;
        });
    if (!bVisible)
    {
        OutMessage = TEXT("Binding source is not accessible on the target's current StateTree execution path.");
        return false;
    }
    return true;
}

bool DescribeExactObject(
    const UStateTree& StateTree,
    const UStateTreeEditorData& EditorData,
    const FString& Kind,
    const FString& Id,
    FString& OutText,
    FString& OutError)
{
    FExactSchemaTextBuilder Builder;
    const auto Finish = [&]()
    {
        return Builder.Finish(OutText, OutError);
    };

    if (Kind == TEXT("target"))
    {
        if (!AppendStructDescription(
                Builder,
            *EditorData.GetClass(),
            false,
            FString(),
            [&](const FProperty& Property)
            {
                return IsTargetDirectWritable(EditorData, Property);
                })
            || !Builder.Append(DescribeNativeSchemaCapabilities(EditorData))
            || !Builder.Append(DescribeTargetDestinations(EditorData)))
        {
            return Finish();
        }
        return Finish();
    }
    if (Kind == TEXT("state"))
    {
        FGuid Guid;
        if (ParseGuid(Id, Guid))
        {
            if (const UStateTreeState* State = FindUniqueState(EditorData, Guid))
            {
                if (!AppendStructDescription(
                        Builder,
                        *UStateTreeState::StaticClass(),
                        false,
                        FString(),
                        [&](const FProperty& Property)
                        {
                            return IsStateDirectWritable(EditorData, Property);
                        })
                    || !Builder.Append(DescribeNativeSchemaCapabilities(EditorData))
                    || !Builder.Append(DescribeStateDestinations(EditorData, *State))
                    || !Builder.Append(
                        TEXT("\n  RequiredEventToEnter.PayloadStruct: UScriptStruct; descriptor; read/write/reset"),
                        1))
                {
                    return Finish();
                }
                FStateTreeEvent& Event = const_cast<UStateTreeState*>(State)
                    ->RequiredEventToEnter.GetTemporaryEvent();
                FString CanonicalError;
                const bool bCanonicalEvent = ResolveCanonicalBindableSurface(
                    EditorData,
                    State->GetEventID(),
                    FPropertyBindingDataView(FStateTreeEvent::StaticStruct(), &Event),
                    EStateTreeBindableStructSource::StateEvent,
                    CanonicalError);
                AppendEventBindingSurface(
                    Builder,
                    State->RequiredEventToEnter,
                    TEXT("RequiredEventToEnter."),
                    TEXT("bHasRequiredEventToEnter == true"),
                    bCanonicalEvent);
                return Finish();
            }
        }
    }
    if (Kind == TEXT("transition"))
    {
        FGuid Guid;
        if (ParseGuid(Id, Guid))
        {
            if (const FStateTreeTransition* Transition = FindUniqueTransition(EditorData, Guid))
            {
                const FPropertyBindingDataView TransitionView(
                    FStateTreeTransition::StaticStruct(),
                    const_cast<FStateTreeTransition*>(Transition));
                FString CanonicalError;
                const bool bCanonicalTransition = ResolveCanonicalBindableSurface(
                    EditorData,
                    Transition->ID,
                    TransitionView,
                    EStateTreeBindableStructSource::Transition,
                    CanonicalError);
                if (!AppendStructDescription(
                        Builder,
                        *FStateTreeTransition::StaticStruct(),
                        false,
                        FString(),
                        [](const FProperty& Property)
                        {
                            return !IsTransitionStructuralProperty(Property);
                        },
                        bCanonicalTransition
                            ? ESchemaBindingSurface::TransitionDelegate
                            : ESchemaBindingSurface::None)
                    || !Builder.Append(
                        TEXT("\n  RequiredEvent.PayloadStruct: UScriptStruct; descriptor; read/write/reset"),
                        1))
                {
                    return Finish();
                }
                FStateTreeEvent& Event = const_cast<FStateTreeTransition*>(Transition)
                    ->RequiredEvent.GetTemporaryEvent();
                const bool bCanonicalEvent = ResolveCanonicalBindableSurface(
                    EditorData,
                    Transition->GetEventID(),
                    FPropertyBindingDataView(FStateTreeEvent::StaticStruct(), &Event),
                    EStateTreeBindableStructSource::TransitionEvent,
                    CanonicalError);
                AppendEventBindingSurface(
                    Builder,
                    Transition->RequiredEvent,
                    TEXT("RequiredEvent."),
                    TEXT("Trigger == OnEvent"),
                    bCanonicalEvent);
                return Finish();
            }
        }
    }
    if (Kind == TEXT("node"))
    {
        FGuid Guid;
        if (ParseGuid(Id, Guid))
        {
            if (const FStateTreeEditorNode* Node = FindUniqueNode(EditorData, Guid))
            {
                if (!Builder.Append(TEXT("schema:\nfields:")))
                {
                    return Finish();
                }
                if (Node->GetNode().GetScriptStruct() != nullptr)
                {
                    const TStructView<FStateTreeNodeBase> NodeView = Node->GetNode();
                    FString CanonicalError;
                    const bool bCanonicalNode = ResolveCanonicalBindableSurface(
                        EditorData,
                        Node->GetNodeID(),
                        FPropertyBindingDataView(
                            NodeView.GetScriptStruct(),
                        NodeView.GetMemory()),
                        TOptional<EStateTreeBindableStructSource>(),
                        CanonicalError);
                    if (!AppendStructDescription(
                            Builder,
                            *Node->GetNode().GetScriptStruct(),
                            false,
                            TEXT("Node."),
                            FSchemaWriteFilter(),
                            bCanonicalNode
                                ? ESchemaBindingSurface::NodeTemplate
                                : ESchemaBindingSurface::None,
                            false))
                    {
                        return Finish();
                    }
                }
                if (Node->GetInstance().GetStruct() != nullptr)
                {
                    const FPropertyBindingDataView InstanceView = Node->GetInstance();
                    FString CanonicalError;
                    const bool bCanonicalInstance = ResolveCanonicalBindableSurface(
                        EditorData,
                        Node->ID,
                        InstanceView,
                        Node->Node.GetScriptStruct() != nullptr
                                && Node->Node.GetScriptStruct()->IsChildOf(
                                    FStateTreePropertyFunctionBase::StaticStruct())
                            ? TOptional<EStateTreeBindableStructSource>(
                                EStateTreeBindableStructSource::PropertyFunction)
                            : TOptional<EStateTreeBindableStructSource>(),
                        CanonicalError);
                    if (!AppendStructDescription(
                            Builder,
                            *Node->GetInstance().GetStruct(),
                            false,
                            TEXT("Instance."),
                            FSchemaWriteFilter(),
                            bCanonicalInstance
                                ? ESchemaBindingSurface::NodeInstance
                                : ESchemaBindingSurface::None,
                            false))
                    {
                        return Finish();
                    }
                }
                const FStateTreeDataView RuntimeView = Node->GetExecutionRuntimeData();
                if (RuntimeView.IsValid()
                    && !AppendStructDescription(
                        Builder,
                        *RuntimeView.GetStruct(),
                        true,
                        TEXT("ExecutionRuntimeData."),
                        FSchemaWriteFilter(),
                        ESchemaBindingSurface::None,
                        false))
                {
                    return Finish();
                }
                return Finish();
            }
        }
    }
    if (Kind == TEXT("parameter"))
    {
        FGuid ContainerId;
        FGuid PropertyId;
        const FInstancedPropertyBag* Bag = nullptr;
        const UStateTreeState* OwnerState = nullptr;
        if (ParseParameterId(Id, ContainerId, PropertyId)
            && (Bag = FindParameterBag(EditorData, ContainerId, &OwnerState)) != nullptr)
        {
            if (const FPropertyBagPropertyDesc* Desc = Bag->FindPropertyDescByID(PropertyId))
            {
                const FString NativeType = Desc->CachedProperty != nullptr
                    ? PropertyTypeText(*Desc->CachedProperty)
                    : TEXT("unknown");
                const bool bFixedLayout = OwnerState != nullptr && OwnerState->Parameters.bFixedLayout;
                const bool bParameterBindingTarget = OwnerState == nullptr
                    || OwnerState->Type != EStateTreeStateType::Subtree;
                const bool bDispatcher = IsDispatcher(Desc->CachedProperty);
                const bool bRootDispatcherEnabled = OwnerState != nullptr
                    || IsRootParameterDelegateDispatcherBindingEnabled();
                FString CanonicalError;
                const bool bCanonicalContainer = ResolveCanonicalBindableSurface(
                    EditorData,
                    ContainerId,
                    FPropertyBindingDataView(
                        const_cast<FInstancedPropertyBag*>(Bag)->GetMutableValue()),
                    OwnerState == nullptr
                        ? EStateTreeBindableStructSource::Parameter
                        : EStateTreeBindableStructSource::StateParameter,
                    CanonicalError);
                const bool bBindingSource = bCanonicalContainer
                    && (!bDispatcher || bRootDispatcherEnabled);
                const bool bBindingTarget = bCanonicalContainer
                    && bParameterBindingTarget
                    && !bDispatcher;
                FString BindingCapabilities;
                if (bBindingSource)
                {
                    BindingCapabilities = TEXT("binding source");
                }
                if (bBindingTarget)
                {
                    BindingCapabilities += BindingCapabilities.IsEmpty()
                        ? TEXT("binding target")
                        : TEXT("/target");
                }
                if (BindingCapabilities.IsEmpty())
                {
                    BindingCapabilities = TEXT("not a Binding endpoint");
                }
                FString DispatcherConstraint;
                if (bDispatcher && OwnerState == nullptr)
                {
                    DispatcherConstraint = FString::Printf(
                        TEXT("\n  root Parameter Dispatcher source: %s; controlled by StateTree.Compiler.EnableParameterDelegateDispatcherBinding"),
                        bRootDispatcherEnabled ? TEXT("available") : TEXT("unavailable"));
                }
                Builder.Append(
                    FString::Printf(
                        TEXT("schema:\nfields:\n  Name: FName; %s\n  type: %s; %s\n  Value: %s; read/write/reset; %s\n  MetaData: FPropertyBagPropertyDescMetaData[]; %s\nconstraints:\n  identity: container-guid/property-guid\n  remove: %s\n  fixed layouts preserve descriptor identity but still permit local Value override and reset; Subtree Parameter values are not Binding targets.%s"),
                        bFixedLayout ? TEXT("read-only") : TEXT("read/write"),
                        *NativeType,
                        bFixedLayout ? TEXT("read-only") : TEXT("read/write"),
                        *NativeType,
                        *BindingCapabilities,
                        bFixedLayout ? TEXT("read-only") : TEXT("read/write"),
                        bFixedLayout ? TEXT("unavailable") : TEXT("available"),
                        *DispatcherConstraint),
                    4);
                return Finish();
            }
        }
    }
    if (Kind == TEXT("object"))
    {
        FGuid Guid;
        const FStateTreeExternalDataDesc* Descriptor = nullptr;
        FString CanonicalError;
        if (ParseGuid(Id, Guid)
            && ResolveCanonicalContext(
                EditorData,
                Guid,
                Descriptor,
                CanonicalError)
            && Descriptor != nullptr
            && Descriptor->Struct != nullptr)
        {
            AppendStructDescription(
                Builder,
                *Descriptor->Struct,
                true,
                FString(),
                FSchemaWriteFilter(),
                ESchemaBindingSurface::ReadOnlySource);
            return Finish();
        }
    }
    Builder.Append(TEXT("schema:\nconstraints:\n  Exact object is stale, ambiguous, or has no reflected surface."));
    return Finish();
}
}
