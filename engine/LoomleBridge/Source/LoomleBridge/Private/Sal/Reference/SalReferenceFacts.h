// Copyright 2026 Loomle contributors.

#pragma once

#include "CoreMinimal.h"

class FJsonObject;
class UBlueprint;
class UEdGraph;
class UEdGraphNode;
class UWidget;
struct FBPVariableDescription;

namespace Loomle::Sal
{
struct FSalResolvedTarget;

/**
 * Internal declaration kinds used while answering a SAL references Query.
 *
 * These are deliberately not a public SAL type system. They preserve the UE
 * declaration identity that the ordinary SAL objects already expose.
 */
enum class EReferenceDeclarationKind : uint8
{
    Invalid,
    BlueprintMemberVariable,
    LocalVariable,
    Dispatcher,
    Component,
    Widget,
    Function,
    Macro,
    CustomEvent,
    NativeProperty,
    NativeFunction
};

struct FCanonicalReference
{
    EReferenceDeclarationKind Kind = EReferenceDeclarationKind::Invalid;

    /** Blueprint asset path for authored declarations; authoritative Struct path for native fields. */
    FString OwnerPath;

    /** Authored declaration GUID. Native declarations intentionally leave this invalid. */
    FGuid Guid;

    /** Top-level Function Graph GUID. Used only by LocalVariable. */
    FGuid ScopeGraphGuid;

    /** Current readable name. It is not part of GUID-backed identity. */
    FName Name;

    bool IsValid() const;
    bool IsNative() const;
    FString StableKey() const;

    friend bool operator==(const FCanonicalReference& Left, const FCanonicalReference& Right);
    friend uint32 GetTypeHash(const FCanonicalReference& Value);
};

struct FReferenceSubject
{
    FCanonicalReference Identity;

    /** Copyable SAL subject text used to resolve this identity. */
    FString QueryRef;
};

enum class EReferenceResolutionStatus : uint8
{
    Resolved,
    NotFound,
    Ambiguous,
    Broken,
    Unsupported
};

enum class EReferenceCoverageIssueKind : uint8
{
    Broken,
    Unsupported
};

struct FReferenceCoverageIssue
{
    EReferenceCoverageIssueKind Kind = EReferenceCoverageIssueKind::Broken;
    FString ObjectRef;
    FString FieldPath;
    FString Message;
};

struct FReferenceSubjectResolution
{
    EReferenceResolutionStatus Status = EReferenceResolutionStatus::NotFound;
    FReferenceSubject Subject;
    FString Message;

    /** Complete, copyable replacement primary-operation lines for an ambiguous subject. */
    TArray<FString> Matches;

    /** Broken or unsupported native facts that prevented a factual resolution. */
    TArray<FReferenceCoverageIssue> Issues;

    bool IsResolved() const
    {
        return Status == EReferenceResolutionStatus::Resolved && Subject.Identity.IsValid();
    }
};

enum class EReferenceUseSiteKind : uint8
{
    Node,
    Graph,
    Variable,
    Widget,
    Blueprint
};

struct FReferenceUseSite
{
    EReferenceUseSiteKind Kind = EReferenceUseSiteKind::Node;
    UBlueprint* Blueprint = nullptr;
    UEdGraph* Graph = nullptr;
    UEdGraphNode* Node = nullptr;
    UWidget* Widget = nullptr;
    FBPVariableDescription* Variable = nullptr;

    /** Native fields through which this object stores the requested identity. */
    TArray<FString> MatchedPaths;

    /** True when the containing object stores more than one independent reference fact. */
    bool bCompound = false;
};

struct FReferenceScanResult
{
    /** Distinct authored objects. Several matching fields on one object are folded into one Site. */
    TArray<FReferenceUseSite> Sites;

    /** Facts that could match the target but could not be verified exactly. */
    TArray<FReferenceCoverageIssue> Issues;

    bool IsComplete() const
    {
        return Issues.IsEmpty();
    }
};

/**
 * UE-native fact extraction shared by local and project reference providers.
 *
 * All calls must run on the game thread. The returned raw UObject and variable
 * pointers are borrowed from the supplied loaded Blueprint and are valid only
 * while that authored state remains unchanged.
 */
class FSalReferenceFacts
{
public:
    static FReferenceSubjectResolution ResolveSubject(
        const FSalResolvedTarget& BoundTarget,
        const TSharedPtr<FJsonObject>& OperationTarget);

    static FReferenceScanResult ScanBlueprint(
        UBlueprint* Blueprint,
        UEdGraph* OptionalGraphScope,
        const FCanonicalReference& Target);
};
}
