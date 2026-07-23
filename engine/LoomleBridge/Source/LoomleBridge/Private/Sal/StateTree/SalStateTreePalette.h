// Copyright 2026 Loomle contributors.

#pragma once

#include "CoreMinimal.h"
#include "UObject/TopLevelAssetPath.h"

class FJsonObject;
class FJsonValue;
class FProperty;
class UClass;
class UScriptStruct;
class UStateTree;
class UStateTreeEditorData;

namespace Loomle::Sal::StateTreeSchema
{
struct FResolvedMember;
}

namespace Loomle::Sal::StateTreePalette
{
namespace Schema = Loomle::Sal::StateTreeSchema;
enum class EConstructorKind : uint8
{
    State,
    Node,
    Transition,
    Parameter,
};

enum class EDestinationRole : uint8
{
    RootState,
    ChildState,
    GlobalEvaluator,
    GlobalTask,
    EnterCondition,
    Task,
    Consideration,
    Transition,
    TransitionCondition,
    Parameter,
    PropertyFunction,
};

/** Exact, revalidated interpretation of a SAL Palette destination. */
struct FDestination
{
    EDestinationRole Role = EDestinationRole::RootState;
    FGuid OwnerId;
    TArray<FString> MemberPath;
    const UScriptStruct* RequiredNodeStruct = nullptr;
    const UClass* RequiredNodeClass = nullptr;
    TSharedPtr<Schema::FResolvedMember> BindingTarget;
};

/** One currently available constructor in an exact StateTree destination. */
struct FEntry
{
    FString Id;
    FString DisplayName;
    EConstructorKind ConstructorKind = EConstructorKind::Node;
    EDestinationRole DestinationRole = EDestinationRole::Task;
    FTopLevelAssetPath NativeType;
    UScriptStruct* NodeStruct = nullptr;
    UClass* NodeClass = nullptr;
    const UStruct* InstanceDataType = nullptr;
    const FProperty* PropertyFunctionOutput = nullptr;
    /** Copyable native Type seed. */
    FString StateType;
    FString StateSelectionBehavior;
    /** True when the Palette capability requires its exact native State Type. */
    bool bFixedStateType = false;
    /** Required linked target carried by a destination-bound Linked State entry. */
    FGuid LinkedSubtreeId;
    FString ParameterName;
    FString ParameterType;
    bool bBlueprint = false;
    bool bSpawnable = false;
};

struct FPage
{
    TArray<FEntry> Entries;
    int32 NextOffset = INDEX_NONE;
    bool bComplete = true;
};

/** Resolve and validate the destination against the current authored StateTree. */
bool ResolveDestination(
    const UStateTree& StateTree,
    const UStateTreeEditorData& EditorData,
    const TSharedPtr<FJsonObject>& DestinationRef,
    FDestination& OutDestination,
    FString& OutMessage);

/**
 * Discover one bounded page without loading candidates outside that page's
 * bounded evaluation window. NextOffset is an opaque raw discovery offset.
 */
bool DiscoverEntries(
    const UStateTree& StateTree,
    const UStateTreeEditorData& EditorData,
    const FDestination& Destination,
    const FString& SearchText,
    int32 Offset,
    int32 Limit,
    FPage& OutPage,
    FString& OutMessage);

/** Resolve one stable Palette id and revalidate it for the exact destination. */
bool ResolveEntry(
    const UStateTree& StateTree,
    const UStateTreeEditorData& EditorData,
    const FDestination& Destination,
    const FString& Id,
    FEntry& OutEntry,
    FString& OutMessage);

const TCHAR* ConstructorName(EConstructorKind Kind);
const TCHAR* DestinationRoleName(EDestinationRole Role);
TSharedPtr<FJsonValue> MakeConstructor(const FEntry& Entry);
}
