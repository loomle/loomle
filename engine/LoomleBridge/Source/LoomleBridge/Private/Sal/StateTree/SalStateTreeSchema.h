// Copyright 2026 Loomle contributors.

#pragma once

#include "CoreMinimal.h"
#include "PropertyBindingDataView.h"
#include "PropertyBindingPath.h"
#include "StateTreeTypes.h"

class FJsonObject;
class FProperty;
struct FInstancedPropertyBag;
struct FPropertyBagPropertyDesc;
struct FStateTreeExternalDataDesc;
class UStateTree;
class UStateTreeEditorData;

namespace Loomle::Sal::StateTreeSchema
{
enum class EMemberSurface : uint8
{
    Reflected,
    ParameterValue,
    ParameterName,
    ParameterType,
    ParameterMetaData,
};

enum class EMemberPurpose : uint8
{
    ReadOrEdit,
    BindingSource,
    BindingTarget,
};

/** One reflected member resolved from a canonical StateTree SAL reference. */
struct FResolvedMember
{
    FString OwnerKind;
    FString OwnerId;
    TArray<FString> SalPath;
    FPropertyBindingDataView OwnerView;
    FPropertyBindingPath NativePath;
    const FProperty* RootProperty = nullptr;
    const FProperty* LeafProperty = nullptr;
    EMemberSurface Surface = EMemberSurface::Reflected;
    EStateTreePropertyUsage Usage = EStateTreePropertyUsage::Invalid;
    bool bReadable = false;
    bool bWritable = false;
    bool bResettable = false;
    bool bBindingSource = false;
    bool bBindingTarget = false;
    bool bReadOnlyOwner = false;
    bool bLayoutEditable = false;
    bool bValueOverrideWritable = false;
    bool bPropertyFunctionOwner = false;
    bool bRootParameterOwner = false;
    FGuid ParameterContainerId;
    FGuid ParameterPropertyId;
    const FInstancedPropertyBag* ParameterBag = nullptr;
    const FPropertyBagPropertyDesc* ParameterDesc = nullptr;
};

/** Resolve an exact stable owner/member path against current authored data. */
bool ResolveMember(
    const UStateTree& StateTree,
    const UStateTreeEditorData& EditorData,
    const FString& OwnerKind,
    const FString& OwnerId,
    const TArray<FString>& SalPath,
    FResolvedMember& OutMember,
    FString& OutMessage,
    EMemberPurpose Purpose = EMemberPurpose::ReadOrEdit);

/** Resolve a normalized stable MemberRef; local target aliases are not members. */
bool ResolveMemberReference(
    const UStateTree& StateTree,
    const UStateTreeEditorData& EditorData,
    const TSharedPtr<FJsonObject>& MemberRef,
    FResolvedMember& OutMember,
    FString& OutMessage,
    EMemberPurpose Purpose = EMemberPurpose::ReadOrEdit);

/** Apply StateTree's data-direction and native Property Binding compatibility. */
bool AreBindingCompatible(
    const UStateTreeEditorData& EditorData,
    const FResolvedMember& Source,
    const FResolvedMember& Target,
    FString& OutMessage);

/**
 * Resolve one Schema Context descriptor whose native StructID is also unique
 * across UE's complete VisitAllNodes Binding surface.
 */
bool ResolveCanonicalContext(
    const UStateTreeEditorData& EditorData,
    const FGuid& ContextId,
    const FStateTreeExternalDataDesc*& OutDescriptor,
    FString& OutMessage);

/**
 * Shared fail-complete budget for exact object and Palette schema text.
 * Callers may append incrementally, but Finish() never exposes partial text.
 */
class FExactSchemaTextBuilder
{
public:
    static constexpr int32 MaxFields = 2048;
    static constexpr int64 MaxCharacters = 1024 * 1024;

    bool Append(const FString& Text, int32 AddedFields = 0);
    bool Finish(FString& OutText, FString& OutError) const;

private:
    FString Text;
    int32 Fields = 0;
    int64 Characters = 0;
    bool bExceeded = false;
};

/** Adjacent human/agent guidance used by exact `with schema` Query results. */
bool DescribeExactObject(
    const UStateTree& StateTree,
    const UStateTreeEditorData& EditorData,
    const FString& Kind,
    const FString& Id,
    FString& OutText,
    FString& OutError);
}
