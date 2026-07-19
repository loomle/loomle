// Copyright 2026 Loomle contributors.

#include "SalReferenceFacts.h"

#include "Blueprint/WidgetTree.h"
#include "Components/ActorComponent.h"
#include "Components/Widget.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraphSchema_K2.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Engine/MemberReference.h"
#include "Engine/SCS_Node.h"
#include "Engine/SimpleConstructionScript.h"
#include "K2Node.h"
#include "K2Node_BaseMCDelegate.h"
#include "K2Node_CallFunction.h"
#include "K2Node_CallFunctionOnMember.h"
#include "K2Node_ComponentBoundEvent.h"
#include "K2Node_CreateDelegate.h"
#include "K2Node_CustomEvent.h"
#include "K2Node_Event.h"
#include "K2Node_FunctionEntry.h"
#include "K2Node_FunctionResult.h"
#include "K2Node_FunctionTerminator.h"
#include "K2Node_GetClassDefaults.h"
#include "K2Node_MacroInstance.h"
#include "K2Node_Variable.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Sal/SalModel.h"
#include "UObject/UnrealType.h"
#include "WidgetBlueprint.h"

namespace Loomle::Sal
{
namespace ReferenceFactsPrivate
{
enum class EExpectedMemberKind : uint8
{
    Any,
    Property,
    Function
};

struct FExtractedReference
{
    FCanonicalReference Identity;
    FString FieldPath;
    bool bSelectableMemberPath = false;
    bool bCompound = false;
};

struct FRawReferenceHint
{
    FGuid Guid;
    FGuid ScopeGraphGuid;
    FName Name;
    FString OwnerPath;
    EExpectedMemberKind ExpectedKind = EExpectedMemberKind::Any;
    bool bCouldAffectAnyTarget = false;
};

struct FExtractionResult
{
    struct FUnresolved
    {
        FReferenceCoverageIssue Issue;
        FRawReferenceHint Hint;
    };

    TArray<FExtractedReference> Facts;
    TArray<FUnresolved> Unresolved;
};

FString GuidText(const FGuid& Guid)
{
    return Guid.ToString(EGuidFormats::DigitsWithHyphensLower);
}

FString DeclarationKindText(const EReferenceDeclarationKind Kind)
{
    switch (Kind)
    {
    case EReferenceDeclarationKind::BlueprintMemberVariable: return TEXT("variable");
    case EReferenceDeclarationKind::LocalVariable: return TEXT("variable");
    case EReferenceDeclarationKind::Dispatcher: return TEXT("dispatcher");
    case EReferenceDeclarationKind::Component: return TEXT("component");
    case EReferenceDeclarationKind::Widget: return TEXT("widget");
    case EReferenceDeclarationKind::Function: return TEXT("graph");
    case EReferenceDeclarationKind::Macro: return TEXT("graph");
    case EReferenceDeclarationKind::CustomEvent: return TEXT("node");
    case EReferenceDeclarationKind::NativeProperty: return TEXT("native_property");
    case EReferenceDeclarationKind::NativeFunction: return TEXT("native_function");
    default: return TEXT("invalid");
    }
}

FString BlueprintPath(const UBlueprint* Blueprint)
{
    return Blueprint != nullptr ? Blueprint->GetPathName() : FString();
}

FString NodeRef(const UEdGraphNode* Node)
{
    return Node != nullptr && Node->NodeGuid.IsValid()
        ? TEXT("node@") + GuidText(Node->NodeGuid)
        : TEXT("node@<invalid>");
}

FString GraphRef(const UEdGraph* Graph)
{
    return Graph != nullptr && Graph->GraphGuid.IsValid()
        ? TEXT("graph@") + GuidText(Graph->GraphGuid)
        : TEXT("graph@<invalid>");
}

bool ParseGuid(const FString& Text, FGuid& OutGuid)
{
    return FGuid::Parse(Text, OutGuid) && OutGuid.IsValid();
}

bool IsDispatcher(const FBPVariableDescription& Variable)
{
    return Variable.VarType.PinCategory == UEdGraphSchema_K2::PC_MCDelegate;
}

UClass* BlueprintClass(const UBlueprint* Blueprint)
{
    if (Blueprint == nullptr)
    {
        return nullptr;
    }
    if (Blueprint->SkeletonGeneratedClass != nullptr)
    {
        return Blueprint->SkeletonGeneratedClass;
    }
    return Blueprint->GeneratedClass;
}

UClass* AuthoritativeClass(UClass* Class)
{
    if (Class == nullptr)
    {
        return nullptr;
    }
#if WITH_EDITOR
    return Class->GetAuthoritativeClass();
#else
    return Class;
#endif
}

UBlueprint* BlueprintForClass(const UClass* Class)
{
    return Class != nullptr ? UBlueprint::GetBlueprintFromClass(Class) : nullptr;
}

void AddBlueprintHierarchy(UBlueprint* Start, TArray<UBlueprint*>& OutBlueprints)
{
    UBlueprint* Current = Start;
    TSet<UBlueprint*> Seen;
    while (Current != nullptr && !Seen.Contains(Current))
    {
        Seen.Add(Current);
        OutBlueprints.Add(Current);
        UClass* ParentClass = Current->ParentClass;
        Current = BlueprintForClass(ParentClass);
    }
}

TArray<UBlueprint*> MemberReferenceBlueprints(UBlueprint* SelfBlueprint, const FMemberReference& Reference)
{
    TArray<UBlueprint*> Result;
    if (Reference.IsSelfContext() || Reference.IsLocalScope())
    {
        AddBlueprintHierarchy(SelfBlueprint, Result);
        return Result;
    }
    if (UBlueprint* Explicit = BlueprintForClass(Reference.GetMemberParentClass()))
    {
        AddBlueprintHierarchy(Explicit, Result);
    }
    return Result;
}

bool IsFunctionDeclarationGraph(const UBlueprint* Blueprint, const UEdGraph* Graph)
{
    if (Blueprint == nullptr || Graph == nullptr)
    {
        return false;
    }
    if (Graph == FBlueprintEditorUtils::FindUserConstructionScript(Blueprint)
        || Blueprint->DelegateSignatureGraphs.Contains(Graph)
        || Blueprint->UbergraphPages.Contains(Graph)
        || Blueprint->EventGraphs.Contains(Graph))
    {
        return false;
    }
    if (Blueprint->FunctionGraphs.Contains(Graph))
    {
        return true;
    }
    for (const FBPInterfaceDescription& Interface : Blueprint->ImplementedInterfaces)
    {
        if (Interface.Graphs.Contains(Graph))
        {
            return true;
        }
    }
    return false;
}

bool IsMacroDeclarationGraph(const UBlueprint* Blueprint, const UEdGraph* Graph)
{
    return Blueprint != nullptr && Graph != nullptr && Blueprint->MacroGraphs.Contains(Graph);
}

TArray<UEdGraph*> BlueprintGraphs(UBlueprint* Blueprint)
{
    TArray<UEdGraph*> Graphs;
    if (Blueprint != nullptr)
    {
        Blueprint->GetAllGraphs(Graphs);
    }
    return Graphs;
}

void AddUniqueCanonical(TArray<FCanonicalReference>& Values, const FCanonicalReference& Value)
{
    if (Value.IsValid() && !Values.Contains(Value))
    {
        Values.Add(Value);
    }
}

FCanonicalReference MakeGuidIdentity(
    const EReferenceDeclarationKind Kind,
    UBlueprint* Blueprint,
    const FGuid& Guid,
    const FName Name,
    const FGuid& ScopeGraphGuid = FGuid())
{
    FCanonicalReference Result;
    Result.Kind = Kind;
    Result.OwnerPath = BlueprintPath(Blueprint);
    Result.Guid = Guid;
    Result.ScopeGraphGuid = ScopeGraphGuid;
    Result.Name = Name;
    return Result;
}

FCanonicalReference MakeNativeIdentity(
    const EReferenceDeclarationKind Kind,
    const UStruct* Owner,
    const FName Name)
{
    FCanonicalReference Result;
    Result.Kind = Kind;
    if (const UClass* OwnerClass = Cast<UClass>(Owner))
    {
        Result.OwnerPath = AuthoritativeClass(const_cast<UClass*>(OwnerClass))->GetPathName();
    }
    else if (Owner != nullptr)
    {
        Result.OwnerPath = Owner->GetPathName();
    }
    Result.Name = Name;
    return Result;
}

FCanonicalReference MakeNativeFunctionIdentity(const UObject* Owner, const FName Name)
{
    FCanonicalReference Result;
    Result.Kind = EReferenceDeclarationKind::NativeFunction;
    if (const UClass* OwnerClass = Cast<UClass>(Owner))
    {
        Result.OwnerPath = AuthoritativeClass(const_cast<UClass*>(OwnerClass))->GetPathName();
    }
    else if (Owner != nullptr)
    {
        Result.OwnerPath = Owner->GetPathName();
    }
    Result.Name = Name;
    return Result;
}

bool HasGeneratedProperty(UBlueprint* Blueprint, const FName Name, FProperty*& OutProperty)
{
    OutProperty = nullptr;
    for (UClass* Class : {Blueprint != nullptr ? Blueprint->SkeletonGeneratedClass.Get() : nullptr,
             Blueprint != nullptr ? Blueprint->GeneratedClass.Get() : nullptr})
    {
        if (Class == nullptr)
        {
            continue;
        }
        if (FProperty* Property = FindFProperty<FProperty>(Class, Name))
        {
            OutProperty = Property;
            return true;
        }
    }
    return false;
}

void CollectPropertyDeclarationsByGuid(
    UBlueprint* Blueprint,
    const FGuid& Guid,
    TArray<FCanonicalReference>& OutMatches)
{
    if (Blueprint == nullptr || !Guid.IsValid())
    {
        return;
    }

    for (const FBPVariableDescription& Variable : Blueprint->NewVariables)
    {
        if (Variable.VarGuid == Guid)
        {
            OutMatches.Add(MakeGuidIdentity(
                IsDispatcher(Variable)
                    ? EReferenceDeclarationKind::Dispatcher
                    : EReferenceDeclarationKind::BlueprintMemberVariable,
                Blueprint,
                Variable.VarGuid,
                Variable.VarName));
        }
    }

    if (Blueprint->SimpleConstructionScript != nullptr)
    {
        for (USCS_Node* Node : Blueprint->SimpleConstructionScript->GetAllNodes())
        {
            if (Node == nullptr || Node->VariableGuid != Guid)
            {
                continue;
            }
            OutMatches.Add(MakeGuidIdentity(
                EReferenceDeclarationKind::Component,
                Blueprint,
                Node->VariableGuid,
                Node->GetVariableName()));
        }
    }

    UWidgetBlueprint* WidgetBlueprint = Cast<UWidgetBlueprint>(Blueprint);
    if (WidgetBlueprint == nullptr)
    {
        return;
    }
    for (UWidget* Widget : WidgetBlueprint->GetAllSourceWidgets())
    {
        if (Widget == nullptr)
        {
            continue;
        }
        const FGuid* WidgetGuid = WidgetBlueprint->WidgetVariableNameToGuidMap.Find(Widget->GetFName());
        if (WidgetGuid == nullptr || *WidgetGuid != Guid)
        {
            continue;
        }
        FProperty* GeneratedProperty = nullptr;
        if (HasGeneratedProperty(Blueprint, Widget->GetFName(), GeneratedProperty)
            && CastField<FObjectPropertyBase>(GeneratedProperty) != nullptr)
        {
            OutMatches.Add(MakeGuidIdentity(
                EReferenceDeclarationKind::Widget,
                Blueprint,
                *WidgetGuid,
                Widget->GetFName()));
        }
    }
}

void CollectFunctionDeclarationsByGuid(
    UBlueprint* Blueprint,
    const FGuid& Guid,
    TArray<FCanonicalReference>& OutMatches)
{
    if (Blueprint == nullptr || !Guid.IsValid())
    {
        return;
    }
    for (UEdGraph* Graph : BlueprintGraphs(Blueprint))
    {
        if (Graph == nullptr || Graph->GraphGuid != Guid)
        {
            continue;
        }
        if (IsFunctionDeclarationGraph(Blueprint, Graph))
        {
            OutMatches.Add(MakeGuidIdentity(
                EReferenceDeclarationKind::Function,
                Blueprint,
                Graph->GraphGuid,
                Graph->GetFName()));
        }
    }
    for (UEdGraph* Graph : BlueprintGraphs(Blueprint))
    {
        if (Graph == nullptr)
        {
            continue;
        }
        for (UEdGraphNode* Node : Graph->Nodes)
        {
            UK2Node_CustomEvent* CustomEvent = Cast<UK2Node_CustomEvent>(Node);
            if (CustomEvent != nullptr && !CustomEvent->IsOverride() && CustomEvent->NodeGuid == Guid)
            {
                OutMatches.Add(MakeGuidIdentity(
                    EReferenceDeclarationKind::CustomEvent,
                    Blueprint,
                    CustomEvent->NodeGuid,
                    CustomEvent->CustomFunctionName));
            }
        }
    }
}

void CollectMacroDeclarationsByGuid(
    UBlueprint* Blueprint,
    const FGuid& Guid,
    TArray<FCanonicalReference>& OutMatches)
{
    if (Blueprint == nullptr || !Guid.IsValid())
    {
        return;
    }
    for (UEdGraph* Graph : Blueprint->MacroGraphs)
    {
        if (Graph != nullptr && Graph->GraphGuid == Guid)
        {
            OutMatches.Add(MakeGuidIdentity(
                EReferenceDeclarationKind::Macro,
                Blueprint,
                Graph->GraphGuid,
                Graph->GetFName()));
        }
    }
}

bool FindSingleFunctionEntry(UEdGraph* Graph, UK2Node_FunctionEntry*& OutEntry)
{
    OutEntry = nullptr;
    if (Graph == nullptr)
    {
        return false;
    }
    TArray<UK2Node_FunctionEntry*> Entries;
    Graph->GetNodesOfClass(Entries);
    if (Entries.Num() != 1 || Entries[0] == nullptr)
    {
        return false;
    }
    OutEntry = Entries[0];
    return true;
}

void CollectLocalDeclaration(
    UBlueprint* Blueprint,
    const FMemberReference& Reference,
    TArray<FCanonicalReference>& OutMatches,
    FReferenceCoverageIssue& OutFailure)
{
    UClass* ScopeClass = BlueprintClass(Blueprint);
    UStruct* Scope = ScopeClass != nullptr ? Reference.GetMemberScope(ScopeClass) : nullptr;
    UEdGraph* ScopeGraph = Scope != nullptr ? FBlueprintEditorUtils::FindScopeGraph(Blueprint, Scope) : nullptr;
    ScopeGraph = ScopeGraph != nullptr ? FBlueprintEditorUtils::GetTopLevelGraph(ScopeGraph) : nullptr;
    UK2Node_FunctionEntry* Entry = nullptr;
    if (ScopeGraph == nullptr || !FindSingleFunctionEntry(ScopeGraph, Entry))
    {
        OutFailure.Kind = EReferenceCoverageIssueKind::Broken;
        OutFailure.FieldPath = TEXT("MemberScope");
        OutFailure.Message = TEXT("Local-variable MemberScope does not resolve to exactly one Function Entry.");
        return;
    }

    for (FBPVariableDescription& Local : Entry->LocalVariables)
    {
        if (Local.VarGuid == Reference.GetMemberGuid())
        {
            OutMatches.Add(MakeGuidIdentity(
                EReferenceDeclarationKind::LocalVariable,
                Blueprint,
                Local.VarGuid,
                Local.VarName,
                ScopeGraph->GraphGuid));
        }
    }
    if (OutMatches.IsEmpty())
    {
        OutFailure.Kind = EReferenceCoverageIssueKind::Broken;
        OutFailure.FieldPath = TEXT("MemberGuid");
        OutFailure.Message = TEXT("Local-variable MemberGuid is absent from its exact Function scope.");
    }
}

EExpectedMemberKind ExpectedKindForPath(const FString& FieldPath)
{
    if (FieldPath.Contains(TEXT("Function")) || FieldPath.Contains(TEXT("Event")))
    {
        return EExpectedMemberKind::Function;
    }
    if (FieldPath.Contains(TEXT("Variable"))
        || FieldPath.Contains(TEXT("Property"))
        || FieldPath.Contains(TEXT("Delegate"))
        || FieldPath.Contains(TEXT("Member")))
    {
        return EExpectedMemberKind::Property;
    }
    return EExpectedMemberKind::Any;
}

bool IsEmptyMemberReference(const FMemberReference& Reference)
{
    return !Reference.GetMemberGuid().IsValid()
        && Reference.GetMemberName().IsNone()
        && !Reference.IsLocalScope()
        && Reference.GetMemberParentClass() == nullptr
        && Reference.GetMemberParentPackage() == nullptr;
}
}

bool FCanonicalReference::IsNative() const
{
    return Kind == EReferenceDeclarationKind::NativeProperty
        || Kind == EReferenceDeclarationKind::NativeFunction;
}

bool FCanonicalReference::IsValid() const
{
    if (Kind == EReferenceDeclarationKind::Invalid || OwnerPath.IsEmpty())
    {
        return false;
    }
    if (IsNative())
    {
        return !Name.IsNone() && !Guid.IsValid() && !ScopeGraphGuid.IsValid();
    }
    return Guid.IsValid()
        && (Kind != EReferenceDeclarationKind::LocalVariable || ScopeGraphGuid.IsValid());
}

FString FCanonicalReference::StableKey() const
{
    if (!IsValid())
    {
        return TEXT("invalid");
    }
    if (IsNative())
    {
        return FString::Printf(TEXT("%d|%s|%s"), static_cast<int32>(Kind), *OwnerPath, *Name.ToString());
    }
    return FString::Printf(
        TEXT("%d|%s|%s|%s"),
        static_cast<int32>(Kind),
        *OwnerPath,
        *ReferenceFactsPrivate::GuidText(Guid),
        ScopeGraphGuid.IsValid() ? *ReferenceFactsPrivate::GuidText(ScopeGraphGuid) : TEXT("-"));
}

bool operator==(const FCanonicalReference& Left, const FCanonicalReference& Right)
{
    if (Left.Kind != Right.Kind || Left.OwnerPath != Right.OwnerPath)
    {
        return false;
    }
    if (Left.IsNative() || Right.IsNative())
    {
        return Left.IsNative() && Right.IsNative() && Left.Name == Right.Name;
    }
    return Left.Guid == Right.Guid && Left.ScopeGraphGuid == Right.ScopeGraphGuid;
}

uint32 GetTypeHash(const FCanonicalReference& Value)
{
    return GetTypeHash(Value.StableKey());
}

namespace ReferenceFactsPrivate
{
void SetIssue(
    FReferenceCoverageIssue& OutIssue,
    const EReferenceCoverageIssueKind Kind,
    const FString& ObjectRef,
    const FString& FieldPath,
    const FString& Message)
{
    OutIssue.Kind = Kind;
    OutIssue.ObjectRef = ObjectRef;
    OutIssue.FieldPath = FieldPath;
    OutIssue.Message = Message;
}

bool ResolveGuidBackedMember(
    UBlueprint* SelfBlueprint,
    const FMemberReference& Reference,
    const EExpectedMemberKind ExpectedKind,
    FCanonicalReference& OutIdentity,
    FReferenceCoverageIssue& OutIssue)
{
    const FGuid Guid = Reference.GetMemberGuid();
    if (!Guid.IsValid())
    {
        return false;
    }

    TArray<UBlueprint*> CandidateBlueprints = MemberReferenceBlueprints(SelfBlueprint, Reference);
    if (CandidateBlueprints.IsEmpty())
    {
        SetIssue(
            OutIssue,
            EReferenceCoverageIssueKind::Broken,
            FString(),
            TEXT("MemberParent"),
            TEXT("A GUID-backed Blueprint member reference has no resolvable Blueprint owner."));
        return false;
    }

    TArray<FCanonicalReference> Matches;
    if (Reference.IsLocalScope())
    {
        FReferenceCoverageIssue LocalFailure;
        CollectLocalDeclaration(CandidateBlueprints[0], Reference, Matches, LocalFailure);
        if (Matches.IsEmpty())
        {
            OutIssue = MoveTemp(LocalFailure);
            return false;
        }
    }
    else
    {
        for (UBlueprint* Candidate : CandidateBlueprints)
        {
            if (ExpectedKind != EExpectedMemberKind::Function)
            {
                CollectPropertyDeclarationsByGuid(Candidate, Guid, Matches);
            }
            if (ExpectedKind != EExpectedMemberKind::Property)
            {
                CollectFunctionDeclarationsByGuid(Candidate, Guid, Matches);
            }
        }
    }

    if (Matches.Num() == 1)
    {
        OutIdentity = Matches[0];
        return true;
    }
    if (Matches.Num() > 1)
    {
        SetIssue(
            OutIssue,
            EReferenceCoverageIssueKind::Broken,
            FString(),
            TEXT("MemberGuid"),
            TEXT("MemberGuid resolves to several authored declarations in its owner hierarchy."));
        return false;
    }

    bool bKnownUnsupportedGeneratedField = false;
    for (UBlueprint* Candidate : CandidateBlueprints)
    {
        UClass* CandidateClass = BlueprintClass(Candidate);
        if (CandidateClass == nullptr)
        {
            continue;
        }
        if (ExpectedKind != EExpectedMemberKind::Function
            && UBlueprint::GetFieldNameFromClassByGuid<FProperty>(CandidateClass, Guid) != NAME_None)
        {
            bKnownUnsupportedGeneratedField = true;
        }
        if (ExpectedKind != EExpectedMemberKind::Property
            && UBlueprint::GetFieldNameFromClassByGuid<UFunction>(CandidateClass, Guid) != NAME_None)
        {
            bKnownUnsupportedGeneratedField = true;
        }
    }
    SetIssue(
        OutIssue,
        bKnownUnsupportedGeneratedField
            ? EReferenceCoverageIssueKind::Unsupported
            : EReferenceCoverageIssueKind::Broken,
        FString(),
        TEXT("MemberGuid"),
        bKnownUnsupportedGeneratedField
            ? TEXT("MemberGuid names generated Blueprint state that has no confirmed SAL declaration mapping.")
            : TEXT("MemberGuid is absent from the exact Blueprint owner hierarchy; the raw name was not used as a fallback."));
    return false;
}

bool ResolveNativeProperty(
    UBlueprint* SelfBlueprint,
    UClass* SearchClass,
    const FName RawName,
    FCanonicalReference& OutIdentity,
    FReferenceCoverageIssue& OutIssue)
{
    if (SearchClass == nullptr || RawName.IsNone())
    {
        return false;
    }
    FProperty* Property = FindFProperty<FProperty>(SearchClass, RawName);
    if (Property == nullptr)
    {
        return false;
    }
    UClass* OwnerClass = Property->GetOwnerClass();
    if (UBlueprint* OwnerBlueprint = BlueprintForClass(OwnerClass))
    {
        FGuid Guid;
        UBlueprint::GetGuidFromClassByFieldName<FProperty>(OwnerClass, Property->GetFName(), Guid);
        if (!Guid.IsValid())
        {
            SetIssue(
                OutIssue,
                EReferenceCoverageIssueKind::Broken,
                FString(),
                TEXT("MemberGuid"),
                TEXT("A Blueprint-authored Property reference has no GUID; its raw name was not accepted as identity."));
            return false;
        }
        TArray<FCanonicalReference> Matches;
        CollectPropertyDeclarationsByGuid(OwnerBlueprint, Guid, Matches);
        if (Matches.Num() == 1)
        {
            OutIdentity = Matches[0];
            return true;
        }
        SetIssue(
            OutIssue,
            Matches.Num() > 1 ? EReferenceCoverageIssueKind::Broken : EReferenceCoverageIssueKind::Unsupported,
            FString(),
            TEXT("MemberGuid"),
            Matches.Num() > 1
                ? TEXT("Generated Property GUID resolves to several authored declarations.")
                : TEXT("Generated Property has no confirmed Variable, Dispatcher, Component, or Widget declaration mapping."));
        return false;
    }
    OutIdentity = MakeNativeIdentity(
        EReferenceDeclarationKind::NativeProperty,
        Property->GetOwnerStruct(),
        Property->GetFName());
    return OutIdentity.IsValid();
}

bool ResolveNativeFunction(
    UClass* SearchClass,
    const FName RawName,
    FCanonicalReference& OutIdentity,
    FReferenceCoverageIssue& OutIssue)
{
    if (SearchClass == nullptr || RawName.IsNone())
    {
        return false;
    }
    UFunction* Function = SearchClass->FindFunctionByName(RawName, EIncludeSuperFlag::IncludeSuper);
    if (Function == nullptr)
    {
        return false;
    }
    UClass* OwnerClass = Function->GetOwnerClass();
    if (UBlueprint* OwnerBlueprint = BlueprintForClass(OwnerClass))
    {
        FGuid Guid;
        UBlueprint::GetGuidFromClassByFieldName<UFunction>(OwnerClass, Function->GetFName(), Guid);
        if (!Guid.IsValid())
        {
            SetIssue(
                OutIssue,
                EReferenceCoverageIssueKind::Broken,
                FString(),
                TEXT("MemberGuid"),
                TEXT("A Blueprint-authored Function reference has no GUID; its raw name was not accepted as identity."));
            return false;
        }
        TArray<FCanonicalReference> Matches;
        CollectFunctionDeclarationsByGuid(OwnerBlueprint, Guid, Matches);
        if (Matches.Num() == 1)
        {
            OutIdentity = Matches[0];
            return true;
        }
        SetIssue(
            OutIssue,
            Matches.Num() > 1 ? EReferenceCoverageIssueKind::Broken : EReferenceCoverageIssueKind::Unsupported,
            FString(),
            TEXT("MemberGuid"),
            Matches.Num() > 1
                ? TEXT("Generated Function GUID resolves to several authored declarations.")
                : TEXT("Generated Function has no confirmed Function Graph or Custom Event declaration mapping."));
        return false;
    }
    OutIdentity = MakeNativeFunctionIdentity(OwnerClass, Function->GetFName());
    return OutIdentity.IsValid();
}

bool ResolveNameBackedMember(
    UBlueprint* SelfBlueprint,
    const FMemberReference& Reference,
    const EExpectedMemberKind ExpectedKind,
    FCanonicalReference& OutIdentity,
    FReferenceCoverageIssue& OutIssue)
{
    if (Reference.IsLocalScope())
    {
        SetIssue(
            OutIssue,
            EReferenceCoverageIssueKind::Broken,
            FString(),
            TEXT("MemberGuid"),
            TEXT("A Blueprint local-variable reference has no GUID; its name was not accepted as identity."));
        return false;
    }
    const FName RawName = Reference.GetMemberName();
    UClass* SearchClass = Reference.IsSelfContext()
        ? BlueprintClass(SelfBlueprint)
        : Reference.GetMemberParentClass();

    TArray<FCanonicalReference> Matches;
    TArray<FReferenceCoverageIssue> Failures;
    if (ExpectedKind != EExpectedMemberKind::Function)
    {
        FCanonicalReference Property;
        FReferenceCoverageIssue Failure;
        if (ResolveNativeProperty(SelfBlueprint, SearchClass, RawName, Property, Failure))
        {
            if (Property.IsNative())
            {
                AddUniqueCanonical(Matches, Property);
            }
            else
            {
                SetIssue(
                    Failure,
                    EReferenceCoverageIssueKind::Broken,
                    FString(),
                    TEXT("MemberGuid"),
                    TEXT("A name-only FMemberReference resolves to Blueprint-authored Property state; Loomle does not accept that name as a substitute for its missing GUID."));
                Failures.Add(Failure);
            }
        }
        else if (!Failure.Message.IsEmpty())
        {
            Failures.Add(Failure);
        }
    }
    if (ExpectedKind != EExpectedMemberKind::Property)
    {
        FCanonicalReference Function;
        FReferenceCoverageIssue Failure;
        if (ResolveNativeFunction(SearchClass, RawName, Function, Failure))
        {
            if (Function.IsNative())
            {
                AddUniqueCanonical(Matches, Function);
            }
            else
            {
                SetIssue(
                    Failure,
                    EReferenceCoverageIssueKind::Broken,
                    FString(),
                    TEXT("MemberGuid"),
                    TEXT("A name-only FMemberReference resolves to Blueprint-authored Function state; Loomle does not accept that name as a substitute for its missing GUID."));
                Failures.Add(Failure);
            }
        }
        else if (!Failure.Message.IsEmpty())
        {
            Failures.Add(Failure);
        }
    }

    if (Matches.Num() == 1 && Failures.IsEmpty())
    {
        OutIdentity = Matches[0];
        return true;
    }
    if (Matches.Num() > 1)
    {
        SetIssue(
            OutIssue,
            EReferenceCoverageIssueKind::Broken,
            FString(),
            TEXT("MemberName"),
            TEXT("Raw member name resolves as both a Property and Function; an exact native field kind is required."));
        return false;
    }
    if (!Failures.IsEmpty())
    {
        OutIssue = Failures[0];
        return false;
    }

    if (UPackage* ParentPackage = Reference.GetMemberParentPackage();
        ExpectedKind != EExpectedMemberKind::Property && ParentPackage != nullptr && !RawName.IsNone())
    {
        if (UFunction* GlobalFunction = FindObject<UFunction>(ParentPackage, *RawName.ToString()))
        {
            OutIdentity = MakeNativeFunctionIdentity(ParentPackage, GlobalFunction->GetFName());
            return OutIdentity.IsValid();
        }
    }

    SetIssue(
        OutIssue,
        EReferenceCoverageIssueKind::Broken,
        FString(),
        TEXT("MemberName"),
        TEXT("Raw native member name is absent from its authoritative owner; redirects were not followed."));
    return false;
}

bool ResolveMemberReferenceExact(
    UBlueprint* SelfBlueprint,
    const FMemberReference& SourceReference,
    const FString& ObjectRef,
    const FString& FieldPath,
    FCanonicalReference& OutIdentity,
    FReferenceCoverageIssue& OutIssue)
{
    OutIdentity = FCanonicalReference();
    OutIssue = FReferenceCoverageIssue();
    if (IsEmptyMemberReference(SourceReference))
    {
        SetIssue(
            OutIssue,
            EReferenceCoverageIssueKind::Broken,
            ObjectRef,
            FieldPath,
            TEXT("Reference-bearing FMemberReference is empty."));
        return false;
    }

    // Deliberately operate on an immutable snapshot. Never call ResolveMember
    // on the authored instance: UE's editor implementation mutates it and may
    // follow redirects, which would destroy the raw reference fact.
    const FMemberReference Reference = SourceReference;
    const EExpectedMemberKind ExpectedKind = ExpectedKindForPath(FieldPath);
    bool bResolved = Reference.GetMemberGuid().IsValid()
        ? ResolveGuidBackedMember(SelfBlueprint, Reference, ExpectedKind, OutIdentity, OutIssue)
        : ResolveNameBackedMember(SelfBlueprint, Reference, ExpectedKind, OutIdentity, OutIssue);
    if (!bResolved)
    {
        OutIssue.ObjectRef = ObjectRef;
        OutIssue.FieldPath = FieldPath;
    }
    return bResolved;
}

UBlueprint* BlueprintForOwnerPath(const FString& OwnerPath)
{
    if (OwnerPath.IsEmpty())
    {
        return nullptr;
    }
    if (UBlueprint* Blueprint = FindObject<UBlueprint>(nullptr, *OwnerPath))
    {
        return Blueprint;
    }
    return BlueprintForClass(FindObject<UClass>(nullptr, *OwnerPath));
}

UClass* ClassForOwnerPath(const FString& OwnerPath)
{
    if (UBlueprint* Blueprint = BlueprintForOwnerPath(OwnerPath))
    {
        return BlueprintClass(Blueprint);
    }
    return FindObject<UClass>(nullptr, *OwnerPath);
}

bool OwnerMayReferToTarget(const FString& HintOwnerPath, const FCanonicalReference& Target)
{
    if (HintOwnerPath.IsEmpty() || HintOwnerPath == Target.OwnerPath)
    {
        return true;
    }

    UBlueprint* HintBlueprint = BlueprintForOwnerPath(HintOwnerPath);
    UBlueprint* TargetBlueprint = Target.IsNative()
        ? nullptr
        : BlueprintForOwnerPath(Target.OwnerPath);
    if (TargetBlueprint != nullptr)
    {
        if (HintBlueprint == nullptr)
        {
            // A known non-Blueprint owner cannot name this Blueprint
            // declaration. An owner path that no longer resolves is broken
            // evidence and must keep the scan incomplete.
            return FindObject<UObject>(nullptr, *HintOwnerPath) == nullptr;
        }
        TSet<UBlueprint*> Seen;
        for (UBlueprint* Candidate = HintBlueprint;
             Candidate != nullptr && !Seen.Contains(Candidate);
             Candidate = BlueprintForClass(Candidate->ParentClass))
        {
            Seen.Add(Candidate);
            if (Candidate == TargetBlueprint || BlueprintPath(Candidate) == Target.OwnerPath)
            {
                return true;
            }
        }
        return false;
    }

    UClass* HintClass = ClassForOwnerPath(HintOwnerPath);
    UClass* TargetClass = FindObject<UClass>(nullptr, *Target.OwnerPath);
    if (HintClass != nullptr && TargetClass != nullptr)
    {
        return HintClass->IsChildOf(TargetClass);
    }

    UObject* HintOwner = FindObject<UObject>(nullptr, *HintOwnerPath);
    UObject* TargetOwner = FindObject<UObject>(nullptr, *Target.OwnerPath);
    if (HintOwner != nullptr && TargetOwner != nullptr)
    {
        // Non-Class native owners have no inheritance relationship. Their
        // unequal live object identities are sufficient to prove separation.
        return false;
    }

    // Broken or unavailable owner state cannot prove that a same-named fact is
    // unrelated. Keep the scan incomplete rather than claim a false zero.
    return true;
}

bool MayIssueAffectTarget(
    const FCanonicalReference& Target,
    const FRawReferenceHint& Hint)
{
    if (Hint.bCouldAffectAnyTarget)
    {
        return true;
    }
    if (Hint.Guid.IsValid())
    {
        return Target.Guid.IsValid()
            && Target.Guid == Hint.Guid
            && OwnerMayReferToTarget(Hint.OwnerPath, Target)
            && (!Hint.ScopeGraphGuid.IsValid() || Target.ScopeGraphGuid == Hint.ScopeGraphGuid);
    }
    if (Hint.Name.IsNone() || Target.Name != Hint.Name)
    {
        return false;
    }
    if (Hint.ExpectedKind == EExpectedMemberKind::Property
        && Target.Kind != EReferenceDeclarationKind::BlueprintMemberVariable
        && Target.Kind != EReferenceDeclarationKind::LocalVariable
        && Target.Kind != EReferenceDeclarationKind::Dispatcher
        && Target.Kind != EReferenceDeclarationKind::Component
        && Target.Kind != EReferenceDeclarationKind::Widget
        && Target.Kind != EReferenceDeclarationKind::NativeProperty)
    {
        return false;
    }
    if (Hint.ExpectedKind == EExpectedMemberKind::Function
        && Target.Kind != EReferenceDeclarationKind::Function
        && Target.Kind != EReferenceDeclarationKind::CustomEvent
        && Target.Kind != EReferenceDeclarationKind::NativeFunction)
    {
        return false;
    }
    return OwnerMayReferToTarget(Hint.OwnerPath, Target);
}

FRawReferenceHint HintForMemberReference(
    UBlueprint* SelfBlueprint,
    const FMemberReference& Reference,
    const FString& FieldPath)
{
    FRawReferenceHint Hint;
    Hint.Guid = Reference.GetMemberGuid();
    Hint.Name = Reference.GetMemberName();
    Hint.ExpectedKind = ExpectedKindForPath(FieldPath);
    if (Reference.IsSelfContext() || Reference.IsLocalScope())
    {
        Hint.OwnerPath = BlueprintPath(SelfBlueprint);
    }
    else if (UBlueprint* OwnerBlueprint = BlueprintForClass(Reference.GetMemberParentClass()))
    {
        Hint.OwnerPath = BlueprintPath(OwnerBlueprint);
    }
    else if (UClass* OwnerClass = Reference.GetMemberParentClass())
    {
        Hint.OwnerPath = AuthoritativeClass(OwnerClass)->GetPathName();
    }
    else if (UPackage* Package = Reference.GetMemberParentPackage())
    {
        Hint.OwnerPath = Package->GetPathName();
    }
    if (Reference.IsLocalScope())
    {
        UClass* ScopeClass = BlueprintClass(SelfBlueprint);
        UStruct* Scope = ScopeClass != nullptr ? Reference.GetMemberScope(ScopeClass) : nullptr;
        if (UEdGraph* Graph = Scope != nullptr ? FBlueprintEditorUtils::FindScopeGraph(SelfBlueprint, Scope) : nullptr)
        {
            Graph = FBlueprintEditorUtils::GetTopLevelGraph(Graph);
            Hint.ScopeGraphGuid = Graph != nullptr ? Graph->GraphGuid : FGuid();
        }
    }
    return Hint;
}
}
}

namespace Loomle::Sal
{
namespace ReferenceFactsPrivate
{
void AddFact(
    FExtractionResult& Result,
    const FCanonicalReference& Identity,
    const FString& FieldPath,
    const bool bSelectableMemberPath,
    const bool bCompound)
{
    if (!Identity.IsValid())
    {
        return;
    }
    for (FExtractedReference& Existing : Result.Facts)
    {
        if (Existing.Identity == Identity && Existing.FieldPath == FieldPath)
        {
            Existing.bSelectableMemberPath |= bSelectableMemberPath;
            Existing.bCompound |= bCompound;
            return;
        }
    }
    FExtractedReference& Added = Result.Facts.AddDefaulted_GetRef();
    Added.Identity = Identity;
    Added.FieldPath = FieldPath;
    Added.bSelectableMemberPath = bSelectableMemberPath;
    Added.bCompound = bCompound;
}

void AddUnresolved(
    FExtractionResult& Result,
    FReferenceCoverageIssue Issue,
    const FRawReferenceHint& Hint)
{
    FExtractionResult::FUnresolved& Added = Result.Unresolved.AddDefaulted_GetRef();
    Added.Issue = MoveTemp(Issue);
    Added.Hint = Hint;
}

bool ShouldSkipDefinitionMemberInScan(const UEdGraphNode* Node, const FString& FieldPath)
{
    if (Node == nullptr)
    {
        return false;
    }
    if (Node->IsA<UK2Node_FunctionTerminator>() && FieldPath == TEXT("FunctionReference"))
    {
        return true;
    }
    return Node->IsA<UK2Node_Event>()
        && !Node->IsA<UK2Node_ComponentBoundEvent>()
        && FieldPath == TEXT("EventReference");
}

void ExtractReflectedMemberReferences(
    UBlueprint* Blueprint,
    UEdGraphNode* Node,
    const bool bIncludeDefinitionRelationships,
    FExtractionResult& OutResult)
{
    if (Blueprint == nullptr || Node == nullptr)
    {
        return;
    }
    const FString UseSiteRef = NodeRef(Node);
    for (TFieldIterator<FStructProperty> It(Node->GetClass()); It; ++It)
    {
        FStructProperty* Property = *It;
        if (Property == nullptr || Property->Struct != FMemberReference::StaticStruct())
        {
            continue;
        }
        const FString FieldPath = Property->GetName();
        if (!bIncludeDefinitionRelationships && ShouldSkipDefinitionMemberInScan(Node, FieldPath))
        {
            continue;
        }
        const FMemberReference* Reference = Property->ContainerPtrToValuePtr<FMemberReference>(Node);
        if (Reference == nullptr || IsEmptyMemberReference(*Reference))
        {
            continue;
        }
        FCanonicalReference Identity;
        FReferenceCoverageIssue Issue;
        if (ResolveMemberReferenceExact(Blueprint, *Reference, UseSiteRef, FieldPath, Identity, Issue))
        {
            AddFact(OutResult, Identity, FieldPath, true, false);
        }
        else
        {
            AddUnresolved(OutResult, MoveTemp(Issue), HintForMemberReference(Blueprint, *Reference, FieldPath));
        }
    }
}

bool ReadGraphReferenceGuid(const FGraphReference& Reference, FGuid& OutGuid)
{
    const FStructProperty* GuidProperty = FindFProperty<FStructProperty>(
        FGraphReference::StaticStruct(),
        TEXT("GraphGuid"));
    if (GuidProperty == nullptr || GuidProperty->Struct != TBaseStructure<FGuid>::Get())
    {
        return false;
    }
    const FGuid* Value = GuidProperty->ContainerPtrToValuePtr<FGuid>(&Reference);
    if (Value == nullptr)
    {
        return false;
    }
    OutGuid = *Value;
    return true;
}

void ExtractMacroReference(
    UBlueprint* SelfBlueprint,
    UK2Node_MacroInstance* Node,
    FExtractionResult& OutResult)
{
    if (Node == nullptr)
    {
        return;
    }
    const FStructProperty* MacroReferenceProperty = FindFProperty<FStructProperty>(
        Node->GetClass(),
        TEXT("MacroGraphReference"));
    const FGraphReference* MacroReference = MacroReferenceProperty != nullptr
        && MacroReferenceProperty->Struct == FGraphReference::StaticStruct()
        ? MacroReferenceProperty->ContainerPtrToValuePtr<FGraphReference>(Node)
        : nullptr;
    FGuid GraphGuid;
    UBlueprint* OwnerBlueprint = MacroReference != nullptr ? MacroReference->GetBlueprint() : nullptr;
    FRawReferenceHint Hint;
    Hint.ExpectedKind = EExpectedMemberKind::Function;
    Hint.OwnerPath = BlueprintPath(OwnerBlueprint);
    if (MacroReference == nullptr || !ReadGraphReferenceGuid(*MacroReference, GraphGuid))
    {
        FReferenceCoverageIssue Issue;
        SetIssue(
            Issue,
            EReferenceCoverageIssueKind::Unsupported,
            NodeRef(Node),
            TEXT("MacroGraphReference"),
            TEXT("FGraphReference.GraphGuid could not be read without calling its mutable GetGraph path."));
        AddUnresolved(OutResult, MoveTemp(Issue), Hint);
        return;
    }
    Hint.Guid = GraphGuid;
    TArray<FCanonicalReference> Matches;
    CollectMacroDeclarationsByGuid(OwnerBlueprint, GraphGuid, Matches);
    if (Matches.Num() == 1)
    {
        AddFact(OutResult, Matches[0], TEXT("MacroGraphReference"), true, false);
        return;
    }
    FReferenceCoverageIssue Issue;
    SetIssue(
        Issue,
        EReferenceCoverageIssueKind::Broken,
        NodeRef(Node),
        TEXT("MacroGraphReference"),
        Matches.Num() > 1
            ? TEXT("Macro GraphGuid is duplicated inside its owning Blueprint.")
            : TEXT("MacroGraphReference owner and GraphGuid do not resolve to an authored Macro Graph."));
    AddUnresolved(OutResult, MoveTemp(Issue), Hint);
}

void ExtractCreateDelegate(
    UBlueprint* SelfBlueprint,
    UK2Node_CreateDelegate* Node,
    FExtractionResult& OutResult)
{
    if (SelfBlueprint == nullptr || Node == nullptr)
    {
        return;
    }
    const UClass* ScopeClass = Node->GetScopeClass();
    UBlueprint* ScopeBlueprint = BlueprintForClass(ScopeClass);
    if (ScopeBlueprint == nullptr && ScopeClass == nullptr)
    {
        ScopeBlueprint = SelfBlueprint;
    }

    FRawReferenceHint Hint;
    Hint.Guid = Node->SelectedFunctionGuid;
    Hint.Name = Node->SelectedFunctionName;
    Hint.ExpectedKind = EExpectedMemberKind::Function;
    Hint.OwnerPath = ScopeBlueprint != nullptr
        ? BlueprintPath(ScopeBlueprint)
        : (ScopeClass != nullptr ? AuthoritativeClass(const_cast<UClass*>(ScopeClass))->GetPathName() : FString());

    FCanonicalReference Identity;
    if (Node->SelectedFunctionGuid.IsValid())
    {
        TArray<FCanonicalReference> Matches;
        TArray<UBlueprint*> Hierarchy;
        AddBlueprintHierarchy(ScopeBlueprint, Hierarchy);
        for (UBlueprint* Candidate : Hierarchy)
        {
            CollectFunctionDeclarationsByGuid(Candidate, Node->SelectedFunctionGuid, Matches);
        }
        if (Matches.Num() == 1)
        {
            AddFact(OutResult, Matches[0], TEXT("SelectedFunctionGuid"), true, true);
            return;
        }
        FReferenceCoverageIssue Issue;
        SetIssue(
            Issue,
            EReferenceCoverageIssueKind::Broken,
            NodeRef(Node),
            TEXT("SelectedFunctionGuid"),
            Matches.Num() > 1
                ? TEXT("Create Delegate selection GUID resolves to several authored Functions or Custom Events.")
                : TEXT("Create Delegate selection GUID is absent from its exact Blueprint scope; its name was not used as fallback."));
        AddUnresolved(OutResult, MoveTemp(Issue), Hint);
        return;
    }

    if (ScopeBlueprint != nullptr)
    {
        FReferenceCoverageIssue Issue;
        SetIssue(
            Issue,
            EReferenceCoverageIssueKind::Broken,
            NodeRef(Node),
            TEXT("SelectedFunctionName"),
            TEXT("Create Delegate targets Blueprint-authored state but has no selection GUID."));
        AddUnresolved(OutResult, MoveTemp(Issue), Hint);
        return;
    }
    FReferenceCoverageIssue Failure;
    if (ResolveNativeFunction(
            const_cast<UClass*>(ScopeClass),
            Node->SelectedFunctionName,
            Identity,
            Failure))
    {
        AddFact(OutResult, Identity, TEXT("SelectedFunctionName"), true, true);
        return;
    }
    Failure.ObjectRef = NodeRef(Node);
    Failure.FieldPath = TEXT("SelectedFunctionName");
    AddUnresolved(OutResult, MoveTemp(Failure), Hint);
}

void ExtractComponentBoundEvent(
    UBlueprint* Blueprint,
    UK2Node_ComponentBoundEvent* Node,
    FExtractionResult& OutResult)
{
    if (Blueprint == nullptr || Node == nullptr)
    {
        return;
    }
    const FString UseSiteRef = NodeRef(Node);
    const FName ComponentName = Node->GetComponentPropertyName();
    if (!ComponentName.IsNone())
    {
        FCanonicalReference Component;
        FReferenceCoverageIssue Failure;
        FRawReferenceHint Hint;
        Hint.Name = ComponentName;
        Hint.ExpectedKind = EExpectedMemberKind::Property;
        Hint.OwnerPath = BlueprintPath(Blueprint);
        if (ResolveNativeProperty(Blueprint, BlueprintClass(Blueprint), ComponentName, Component, Failure))
        {
            AddFact(OutResult, Component, TEXT("ComponentPropertyName"), true, true);
        }
        else
        {
            Failure.ObjectRef = UseSiteRef;
            Failure.FieldPath = TEXT("ComponentPropertyName");
            AddUnresolved(OutResult, MoveTemp(Failure), Hint);
        }
    }

    if (!Node->DelegatePropertyName.IsNone())
    {
        FCanonicalReference Delegate;
        FReferenceCoverageIssue Failure;
        FRawReferenceHint Hint;
        Hint.Name = Node->DelegatePropertyName;
        Hint.ExpectedKind = EExpectedMemberKind::Property;
        Hint.OwnerPath = Node->DelegateOwnerClass != nullptr
            ? AuthoritativeClass(Node->DelegateOwnerClass)->GetPathName()
            : FString();
        if (ResolveNativeProperty(
                Blueprint,
                Node->DelegateOwnerClass,
                Node->DelegatePropertyName,
                Delegate,
                Failure))
        {
            AddFact(OutResult, Delegate, TEXT("DelegatePropertyName"), true, true);
        }
        else
        {
            Failure.ObjectRef = UseSiteRef;
            Failure.FieldPath = TEXT("DelegatePropertyName");
            AddUnresolved(OutResult, MoveTemp(Failure), Hint);
        }
    }
}

void ExtractClassDefaults(
    UBlueprint* Blueprint,
    UK2Node_GetClassDefaults* Node,
    FExtractionResult& OutResult)
{
    if (Node == nullptr)
    {
        return;
    }
    UClass* InputClass = Node->GetInputClass();
    if (InputClass == nullptr)
    {
        return;
    }
    for (UEdGraphPin* Pin : Node->Pins)
    {
        if (Pin == nullptr || Pin->Direction != EGPD_Output || Pin->PinName.IsNone())
        {
            continue;
        }
        FCanonicalReference Property;
        FReferenceCoverageIssue Failure;
        FRawReferenceHint Hint;
        Hint.Name = Pin->PinName;
        Hint.ExpectedKind = EExpectedMemberKind::Property;
        if (UBlueprint* OwnerBlueprint = BlueprintForClass(InputClass))
        {
            Hint.OwnerPath = BlueprintPath(OwnerBlueprint);
        }
        else
        {
            Hint.OwnerPath = AuthoritativeClass(InputClass)->GetPathName();
        }
        if (ResolveNativeProperty(Blueprint, InputClass, Pin->PinName, Property, Failure))
        {
            // ShowPinForProperties is native authored state, but its entries are
            // not public SAL Member paths. Bare-node ambiguity therefore points
            // callers back to the direct declaration rather than inventing an
            // array index or pin role.
            AddFact(OutResult, Property, TEXT("ShowPinForProperties"), false, true);
        }
        else
        {
            Failure.ObjectRef = NodeRef(Node);
            Failure.FieldPath = TEXT("ShowPinForProperties");
            AddUnresolved(OutResult, MoveTemp(Failure), Hint);
        }
    }
}

bool HasKnownExactNodeCoverage(const UEdGraphNode* Node)
{
    return Node != nullptr
        && (Node->IsA<UK2Node_Variable>()
            || Node->IsA<UK2Node_CallFunction>()
            || Node->IsA<UK2Node_BaseMCDelegate>()
            || Node->IsA<UK2Node_ComponentBoundEvent>()
            || Node->IsA<UK2Node_CustomEvent>()
            || Node->IsA<UK2Node_FunctionTerminator>()
            || Node->IsA<UK2Node_MacroInstance>()
            || Node->IsA<UK2Node_CreateDelegate>()
            || Node->IsA<UK2Node_GetClassDefaults>());
}

FExtractionResult ExtractNodeFacts(
    UBlueprint* Blueprint,
    UEdGraphNode* Node,
    const bool bIncludeDefinitionRelationships)
{
    FExtractionResult Result;
    ExtractReflectedMemberReferences(Blueprint, Node, bIncludeDefinitionRelationships, Result);
    if (UK2Node_MacroInstance* Macro = Cast<UK2Node_MacroInstance>(Node))
    {
        ExtractMacroReference(Blueprint, Macro, Result);
    }
    if (UK2Node_CreateDelegate* CreateDelegate = Cast<UK2Node_CreateDelegate>(Node))
    {
        ExtractCreateDelegate(Blueprint, CreateDelegate, Result);
    }
    if (UK2Node_ComponentBoundEvent* ComponentEvent = Cast<UK2Node_ComponentBoundEvent>(Node))
    {
        ExtractComponentBoundEvent(Blueprint, ComponentEvent, Result);
    }
    if (UK2Node_GetClassDefaults* ClassDefaults = Cast<UK2Node_GetClassDefaults>(Node))
    {
        ExtractClassDefaults(Blueprint, ClassDefaults, Result);
    }
    return Result;
}

bool ResolveStoredFunctionName(
    UBlueprint* Blueprint,
    const FName StoredName,
    FCanonicalReference& OutIdentity,
    FReferenceCoverageIssue& OutIssue)
{
    if (Blueprint == nullptr || StoredName.IsNone())
    {
        SetIssue(
            OutIssue,
            EReferenceCoverageIssueKind::Broken,
            FString(),
            FString(),
            TEXT("Stored Function name is empty."));
        return false;
    }
    TArray<FCanonicalReference> Matches;
    TArray<UBlueprint*> Hierarchy;
    AddBlueprintHierarchy(Blueprint, Hierarchy);
    for (UBlueprint* Candidate : Hierarchy)
    {
        for (UEdGraph* Graph : BlueprintGraphs(Candidate))
        {
            if (IsFunctionDeclarationGraph(Candidate, Graph) && Graph->GetFName() == StoredName)
            {
                Matches.Add(MakeGuidIdentity(
                    EReferenceDeclarationKind::Function,
                    Candidate,
                    Graph->GraphGuid,
                    Graph->GetFName()));
            }
            if (Graph == nullptr)
            {
                continue;
            }
            for (UEdGraphNode* Node : Graph->Nodes)
            {
                UK2Node_CustomEvent* Event = Cast<UK2Node_CustomEvent>(Node);
                if (Event != nullptr && !Event->IsOverride() && Event->CustomFunctionName == StoredName)
                {
                    Matches.Add(MakeGuidIdentity(
                        EReferenceDeclarationKind::CustomEvent,
                        Candidate,
                        Event->NodeGuid,
                        Event->CustomFunctionName));
                }
            }
        }
    }
    if (Matches.Num() == 1)
    {
        OutIdentity = Matches[0];
        return true;
    }
    if (Matches.Num() > 1)
    {
        SetIssue(
            OutIssue,
            EReferenceCoverageIssueKind::Broken,
            FString(),
            FString(),
            TEXT("Stored Function name resolves to several authored declarations."));
        return false;
    }
    if (ResolveNativeFunction(BlueprintClass(Blueprint), StoredName, OutIdentity, OutIssue))
    {
        return true;
    }
    if (OutIssue.Message.IsEmpty())
    {
        SetIssue(
            OutIssue,
            EReferenceCoverageIssueKind::Broken,
            FString(),
            FString(),
            TEXT("Stored Function name is absent from the exact Blueprint owner hierarchy."));
    }
    return false;
}

UWidget* FindUniqueSourceWidget(UWidgetBlueprint* Blueprint, const FName Name, bool& bDuplicate)
{
    bDuplicate = false;
    UWidget* Match = nullptr;
    if (Blueprint == nullptr || Name.IsNone())
    {
        return nullptr;
    }
    for (UWidget* Widget : Blueprint->GetAllSourceWidgets())
    {
        if (Widget == nullptr || Widget->GetFName() != Name)
        {
            continue;
        }
        if (Match != nullptr)
        {
            bDuplicate = true;
            return nullptr;
        }
        Match = Widget;
    }
    return Match;
}

bool WidgetIdentity(
    UWidgetBlueprint* Blueprint,
    UWidget* Widget,
    FCanonicalReference& OutIdentity,
    FReferenceCoverageIssue& OutIssue)
{
    if (Blueprint == nullptr || Widget == nullptr)
    {
        return false;
    }
    const FGuid* Guid = Blueprint->WidgetVariableNameToGuidMap.Find(Widget->GetFName());
    if (Guid == nullptr || !Guid->IsValid())
    {
        SetIssue(
            OutIssue,
            EReferenceCoverageIssueKind::Broken,
            FString(),
            TEXT("WidgetVariableNameToGuidMap"),
            TEXT("Source Widget has no persistent Widget GUID."));
        return false;
    }
    OutIdentity = MakeGuidIdentity(
        EReferenceDeclarationKind::Widget,
        Blueprint,
        *Guid,
        Widget->GetFName());
    return true;
}

bool ReadPathSegmentRaw(
    const FEditorPropertyPathSegment& Segment,
    FName& OutName,
    bool& bOutIsProperty)
{
    const FNameProperty* NameProperty = FindFProperty<FNameProperty>(
        FEditorPropertyPathSegment::StaticStruct(),
        TEXT("MemberName"));
    const FBoolProperty* IsProperty = FindFProperty<FBoolProperty>(
        FEditorPropertyPathSegment::StaticStruct(),
        TEXT("IsProperty"));
    if (NameProperty == nullptr || IsProperty == nullptr)
    {
        return false;
    }
    OutName = NameProperty->GetPropertyValue_InContainer(&Segment);
    bOutIsProperty = IsProperty->GetPropertyValue_InContainer(&Segment);
    return true;
}

bool ResolvePathSegment(
    UBlueprint* SelfBlueprint,
    const FEditorPropertyPathSegment& Segment,
    FCanonicalReference& OutIdentity,
    FReferenceCoverageIssue& OutIssue,
    FRawReferenceHint& OutHint)
{
    FName RawName;
    bool bIsProperty = true;
    if (!ReadPathSegmentRaw(Segment, RawName, bIsProperty))
    {
        SetIssue(
            OutIssue,
            EReferenceCoverageIssueKind::Unsupported,
            FString(),
            TEXT("SourcePath"),
            TEXT("Widget SourcePath raw MemberName and IsProperty fields are unavailable."));
        return false;
    }
    OutHint.Guid = Segment.GetMemberGuid();
    OutHint.Name = RawName;
    OutHint.ExpectedKind = bIsProperty ? EExpectedMemberKind::Property : EExpectedMemberKind::Function;
    UStruct* SegmentStruct = Segment.GetStruct();
    if (SegmentStruct != nullptr)
    {
        OutHint.OwnerPath = SegmentStruct->GetPathName();
    }
    if (UClass* SegmentClass = Cast<UClass>(SegmentStruct))
    {
        if (UBlueprint* SegmentBlueprint = BlueprintForClass(SegmentClass))
        {
            OutHint.OwnerPath = BlueprintPath(SegmentBlueprint);
        }
        else
        {
            OutHint.OwnerPath = AuthoritativeClass(SegmentClass)->GetPathName();
        }
        if (Segment.GetMemberGuid().IsValid())
        {
            FMemberReference Snapshot;
            Snapshot.SetDirect(RawName, Segment.GetMemberGuid(), SegmentClass, false);
            const bool bResolved = ResolveGuidBackedMember(
                SelfBlueprint,
                Snapshot,
                bIsProperty ? EExpectedMemberKind::Property : EExpectedMemberKind::Function,
                OutIdentity,
                OutIssue);
            if (!bResolved)
            {
                OutIssue.FieldPath = TEXT("SourcePath");
            }
            return bResolved;
        }
        return bIsProperty
            ? ResolveNativeProperty(SelfBlueprint, SegmentClass, RawName, OutIdentity, OutIssue)
            : ResolveNativeFunction(SegmentClass, RawName, OutIdentity, OutIssue);
    }
    if (bIsProperty && SegmentStruct != nullptr && !Segment.GetMemberGuid().IsValid())
    {
        if (FProperty* Property = FindFProperty<FProperty>(SegmentStruct, RawName))
        {
            OutHint.OwnerPath = SegmentStruct->GetPathName();
            OutIdentity = MakeNativeIdentity(
                EReferenceDeclarationKind::NativeProperty,
                Property->GetOwnerStruct(),
                Property->GetFName());
            return OutIdentity.IsValid();
        }
    }
    SetIssue(
        OutIssue,
        EReferenceCoverageIssueKind::Unsupported,
        FString(),
        TEXT("SourcePath"),
        TEXT("Widget SourcePath segment owner has no confirmed Class-member declaration mapping."));
    return false;
}

FExtractionResult ExtractWidgetBindingFacts(
    UWidgetBlueprint* Blueprint,
    const FDelegateEditorBinding& Binding,
    UWidget*& OutWidget)
{
    FExtractionResult Result;
    OutWidget = nullptr;
    const FName WidgetName(*Binding.ObjectName);
    bool bDuplicateWidget = false;
    OutWidget = FindUniqueSourceWidget(Blueprint, WidgetName, bDuplicateWidget);
    if (OutWidget == nullptr)
    {
        FReferenceCoverageIssue Issue;
        SetIssue(
            Issue,
            EReferenceCoverageIssueKind::Broken,
            TEXT("widget@<unresolved>"),
            TEXT("Bindings.ObjectName"),
            bDuplicateWidget
                ? TEXT("Widget Binding ObjectName resolves to several source Widgets.")
                : TEXT("Widget Binding ObjectName does not resolve to a source Widget; no synthetic Binding result was invented."));
        FRawReferenceHint Hint;
        Hint.bCouldAffectAnyTarget = true;
        AddUnresolved(Result, MoveTemp(Issue), Hint);
        return Result;
    }

    FCanonicalReference DestinationWidget;
    FReferenceCoverageIssue WidgetFailure;
    if (WidgetIdentity(Blueprint, OutWidget, DestinationWidget, WidgetFailure))
    {
        AddFact(Result, DestinationWidget, TEXT("Bindings.ObjectName"), false, true);
    }
    else
    {
        WidgetFailure.ObjectRef = TEXT("widget@<unresolved>");
        FRawReferenceHint Hint;
        if (const FGuid* Guid = Blueprint->WidgetVariableNameToGuidMap.Find(WidgetName))
        {
            Hint.Guid = *Guid;
        }
        Hint.Name = WidgetName;
        Hint.ExpectedKind = EExpectedMemberKind::Property;
        Hint.OwnerPath = BlueprintPath(Blueprint);
        AddUnresolved(Result, MoveTemp(WidgetFailure), Hint);
    }

    if (!Binding.PropertyName.IsNone())
    {
        const FName AttributeDelegateName(*(Binding.PropertyName.ToString() + TEXT("Delegate")));
        const bool bAttribute = FindFProperty<FDelegateProperty>(OutWidget->GetClass(), AttributeDelegateName) != nullptr;
        const FName ReferencedName = Binding.PropertyName;
        FCanonicalReference DestinationMember;
        FReferenceCoverageIssue Failure;
        FRawReferenceHint Hint;
        Hint.Name = ReferencedName;
        Hint.ExpectedKind = EExpectedMemberKind::Property;
        Hint.OwnerPath = AuthoritativeClass(OutWidget->GetClass())->GetPathName();
        if (ResolveNativeProperty(
                Blueprint,
                OutWidget->GetClass(),
                bAttribute ? Binding.PropertyName : Binding.PropertyName,
                DestinationMember,
                Failure))
        {
            AddFact(Result, DestinationMember, TEXT("Bindings.PropertyName"), false, true);
        }
        else
        {
            Failure.ObjectRef = TEXT("widget@") + GuidText(DestinationWidget.Guid);
            Failure.FieldPath = TEXT("Bindings.PropertyName");
            AddUnresolved(Result, MoveTemp(Failure), Hint);
        }
    }

    if (Binding.Kind == EBindingKind::Function)
    {
        const FString SourceFunctionPath = Binding.MemberGuid.IsValid()
            ? TEXT("Bindings.MemberGuid")
            : TEXT("Bindings.FunctionName");
        FCanonicalReference SourceFunction;
        FReferenceCoverageIssue Failure;
        FRawReferenceHint Hint;
        Hint.Guid = Binding.MemberGuid;
        Hint.Name = Binding.FunctionName;
        Hint.ExpectedKind = EExpectedMemberKind::Function;
        Hint.OwnerPath = BlueprintPath(Blueprint);
        if (Binding.MemberGuid.IsValid())
        {
            TArray<FCanonicalReference> Matches;
            TArray<UBlueprint*> Hierarchy;
            AddBlueprintHierarchy(Blueprint, Hierarchy);
            for (UBlueprint* Candidate : Hierarchy)
            {
                CollectFunctionDeclarationsByGuid(Candidate, Binding.MemberGuid, Matches);
            }
            if (Matches.Num() == 1)
            {
                SourceFunction = Matches[0];
            }
            else
            {
                SetIssue(
                    Failure,
                    EReferenceCoverageIssueKind::Broken,
                    FString(),
                    TEXT("Bindings.MemberGuid"),
                    Matches.Num() > 1
                        ? TEXT("Widget Binding MemberGuid resolves to several Functions or Custom Events.")
                        : TEXT("Widget Binding MemberGuid is unresolved; FunctionName was not used as fallback."));
            }
        }
        else
        {
            ResolveStoredFunctionName(Blueprint, Binding.FunctionName, SourceFunction, Failure);
        }
        if (SourceFunction.IsValid())
        {
            AddFact(Result, SourceFunction, SourceFunctionPath, false, true);
        }
        else
        {
            Failure.ObjectRef = TEXT("widget@") + GuidText(DestinationWidget.Guid);
            if (Failure.FieldPath.IsEmpty())
            {
                Failure.FieldPath = SourceFunctionPath;
            }
            AddUnresolved(Result, MoveTemp(Failure), Hint);
        }
    }

    if (!Binding.SourceProperty.IsNone())
    {
        FCanonicalReference LegacySource;
        FReferenceCoverageIssue Failure;
        FRawReferenceHint Hint;
        Hint.Name = Binding.SourceProperty;
        Hint.ExpectedKind = EExpectedMemberKind::Property;
        Hint.OwnerPath = BlueprintPath(Blueprint);
        if (ResolveNativeProperty(
                Blueprint,
                BlueprintClass(Blueprint),
                Binding.SourceProperty,
                LegacySource,
                Failure))
        {
            AddFact(Result, LegacySource, TEXT("Bindings.SourceProperty"), false, true);
        }
        else
        {
            Failure.ObjectRef = TEXT("widget@") + GuidText(DestinationWidget.Guid);
            Failure.FieldPath = TEXT("Bindings.SourceProperty");
            AddUnresolved(Result, MoveTemp(Failure), Hint);
        }
    }

    for (const FEditorPropertyPathSegment& Segment : Binding.SourcePath.Segments)
    {
        FCanonicalReference SegmentIdentity;
        FReferenceCoverageIssue Failure;
        FRawReferenceHint Hint;
        if (ResolvePathSegment(Blueprint, Segment, SegmentIdentity, Failure, Hint))
        {
            AddFact(Result, SegmentIdentity, TEXT("Bindings.SourcePath"), false, true);
        }
        else
        {
            Failure.ObjectRef = TEXT("widget@") + GuidText(DestinationWidget.Guid);
            Failure.FieldPath = TEXT("Bindings.SourcePath");
            AddUnresolved(Result, MoveTemp(Failure), Hint);
        }
    }
    return Result;
}

bool ReadStableRef(
    const TSharedPtr<FJsonObject>& Ref,
    FString& OutKind,
    FString& OutId)
{
    OutKind.Reset();
    OutId.Reset();
    return Ref.IsValid()
        && Ref->TryGetStringField(TEXT("kind"), OutKind)
        && OutKind != TEXT("member")
        && OutKind != TEXT("local")
        && Ref->TryGetStringField(TEXT("id"), OutId)
        && !OutKind.IsEmpty()
        && !OutId.IsEmpty();
}

bool ReadSubjectRef(
    const TSharedPtr<FJsonObject>& Value,
    TSharedPtr<FJsonObject>& OutStable,
    TArray<FString>& OutPath)
{
    OutStable.Reset();
    OutPath.Reset();
    FString Kind;
    if (!Value.IsValid() || !Value->TryGetStringField(TEXT("kind"), Kind))
    {
        return false;
    }
    if (Kind != TEXT("member"))
    {
        OutStable = Value;
        return true;
    }
    const TSharedPtr<FJsonObject>* Object = nullptr;
    const TArray<TSharedPtr<FJsonValue>>* Path = nullptr;
    if (!Value->TryGetObjectField(TEXT("object"), Object)
        || Object == nullptr
        || !(*Object).IsValid()
        || !Value->TryGetArrayField(TEXT("path"), Path)
        || Path == nullptr
        || Path->IsEmpty())
    {
        return false;
    }
    for (const TSharedPtr<FJsonValue>& SegmentValue : *Path)
    {
        FString Segment;
        if (!SegmentValue.IsValid() || !SegmentValue->TryGetString(Segment) || Segment.IsEmpty())
        {
            return false;
        }
        OutPath.Add(Segment);
    }
    OutStable = *Object;
    return true;
}

FString SubjectText(const FString& Kind, const FString& Id, const TArray<FString>& Path)
{
    FString Result = Kind + TEXT("@") + Id;
    if (!Path.IsEmpty())
    {
        Result += TEXT(".") + FString::Join(Path, TEXT("."));
    }
    return Result;
}

void SetResolutionFailure(
    FReferenceSubjectResolution& Out,
    const EReferenceResolutionStatus Status,
    const FString& Message)
{
    Out.Status = Status;
    Out.Message = Message;
}

void CollectDirectVariables(
    UBlueprint* Blueprint,
    UEdGraph* OptionalGraphScope,
    const FGuid& Guid,
    const bool bDispatcher,
    TArray<FCanonicalReference>& OutMatches,
    FBPVariableDescription** OutVariable = nullptr)
{
    if (OutVariable != nullptr)
    {
        *OutVariable = nullptr;
    }
    if (Blueprint == nullptr)
    {
        return;
    }
    for (FBPVariableDescription& Variable : Blueprint->NewVariables)
    {
        if (Variable.VarGuid == Guid && IsDispatcher(Variable) == bDispatcher)
        {
            OutMatches.Add(MakeGuidIdentity(
                bDispatcher
                    ? EReferenceDeclarationKind::Dispatcher
                    : EReferenceDeclarationKind::BlueprintMemberVariable,
                Blueprint,
                Variable.VarGuid,
                Variable.VarName));
            if (OutVariable != nullptr)
            {
                *OutVariable = &Variable;
            }
        }
    }
    if (bDispatcher)
    {
        return;
    }
    TArray<UEdGraph*> Graphs;
    if (OptionalGraphScope != nullptr)
    {
        UEdGraph* DeclarationGraph = FBlueprintEditorUtils::GetTopLevelGraph(OptionalGraphScope);
        if (IsFunctionDeclarationGraph(Blueprint, DeclarationGraph))
        {
            Graphs.Add(DeclarationGraph);
        }
    }
    else
    {
        Graphs = BlueprintGraphs(Blueprint);
    }
    for (UEdGraph* Graph : Graphs)
    {
        UK2Node_FunctionEntry* Entry = nullptr;
        if (!FindSingleFunctionEntry(Graph, Entry))
        {
            continue;
        }
        for (FBPVariableDescription& Local : Entry->LocalVariables)
        {
            if (Local.VarGuid == Guid)
            {
                OutMatches.Add(MakeGuidIdentity(
                    EReferenceDeclarationKind::LocalVariable,
                    Blueprint,
                    Local.VarGuid,
                    Local.VarName,
                    Graph->GraphGuid));
                if (OutVariable != nullptr)
                {
                    *OutVariable = &Local;
                }
            }
        }
    }
}

UEdGraph* FindUniqueGraph(
    UBlueprint* Blueprint,
    UEdGraph* OptionalGraphScope,
    const FGuid& Guid,
    bool& bDuplicate)
{
    bDuplicate = false;
    UEdGraph* Match = nullptr;
    const TArray<UEdGraph*> Graphs = OptionalGraphScope != nullptr
        ? TArray<UEdGraph*>({OptionalGraphScope})
        : BlueprintGraphs(Blueprint);
    for (UEdGraph* Graph : Graphs)
    {
        if (Graph == nullptr || Graph->GraphGuid != Guid)
        {
            continue;
        }
        if (Match != nullptr)
        {
            bDuplicate = true;
            return nullptr;
        }
        Match = Graph;
    }
    return Match;
}

UEdGraphNode* FindUniqueNode(
    UBlueprint* Blueprint,
    UEdGraph* OptionalGraphScope,
    const FGuid& Guid,
    bool& bDuplicate)
{
    bDuplicate = false;
    UEdGraphNode* Match = nullptr;
    const TArray<UEdGraph*> Graphs = OptionalGraphScope != nullptr
        ? TArray<UEdGraph*>({OptionalGraphScope})
        : BlueprintGraphs(Blueprint);
    for (UEdGraph* Graph : Graphs)
    {
        if (Graph == nullptr)
        {
            continue;
        }
        for (UEdGraphNode* Node : Graph->Nodes)
        {
            if (Node == nullptr || Node->NodeGuid != Guid)
            {
                continue;
            }
            if (Match != nullptr)
            {
                bDuplicate = true;
                return nullptr;
            }
            Match = Node;
        }
    }
    return Match;
}

USCS_Node* FindUniqueComponent(UBlueprint* Blueprint, const FGuid& Guid, bool& bDuplicate)
{
    bDuplicate = false;
    USCS_Node* Match = nullptr;
    if (Blueprint == nullptr || Blueprint->SimpleConstructionScript == nullptr)
    {
        return nullptr;
    }
    for (USCS_Node* Node : Blueprint->SimpleConstructionScript->GetAllNodes())
    {
        if (Node == nullptr || Node->VariableGuid != Guid)
        {
            continue;
        }
        if (Match != nullptr)
        {
            bDuplicate = true;
            return nullptr;
        }
        Match = Node;
    }
    return Match;
}

UWidget* FindUniqueWidgetByGuid(UWidgetBlueprint* Blueprint, const FGuid& Guid, bool& bDuplicate)
{
    bDuplicate = false;
    UWidget* Match = nullptr;
    if (Blueprint == nullptr)
    {
        return nullptr;
    }
    for (UWidget* Widget : Blueprint->GetAllSourceWidgets())
    {
        if (Widget == nullptr)
        {
            continue;
        }
        const FGuid* CandidateGuid = Blueprint->WidgetVariableNameToGuidMap.Find(Widget->GetFName());
        if (CandidateGuid == nullptr || *CandidateGuid != Guid)
        {
            continue;
        }
        if (Match != nullptr)
        {
            bDuplicate = true;
            return nullptr;
        }
        Match = Widget;
    }
    return Match;
}

FReferenceSubjectResolution ResolveFromExtractedFacts(
    const FString& BaseSubject,
    const TArray<FString>& ExplicitPath,
    const FExtractionResult& Extracted)
{
    FReferenceSubjectResolution Result;
    TArray<const FExtractedReference*> SelectedFacts;
    for (const FExtractedReference& Fact : Extracted.Facts)
    {
        if (ExplicitPath.IsEmpty() || (ExplicitPath.Num() == 1 && Fact.FieldPath == ExplicitPath[0]))
        {
            SelectedFacts.Add(&Fact);
        }
    }
    TArray<FReferenceCoverageIssue> SelectedIssues;
    for (const FExtractionResult::FUnresolved& Unresolved : Extracted.Unresolved)
    {
        if (ExplicitPath.IsEmpty()
            || (ExplicitPath.Num() == 1 && Unresolved.Issue.FieldPath == ExplicitPath[0]))
        {
            SelectedIssues.Add(Unresolved.Issue);
        }
    }
    if (!ExplicitPath.IsEmpty() && SelectedFacts.IsEmpty() && SelectedIssues.IsEmpty())
    {
        SetResolutionFailure(
            Result,
            EReferenceResolutionStatus::Unsupported,
            TEXT("The exact member path is not a supported reference-bearing native field."));
        return Result;
    }
    if (!SelectedIssues.IsEmpty())
    {
        Result.Issues = MoveTemp(SelectedIssues);
        const bool bBroken = Result.Issues.ContainsByPredicate([](const FReferenceCoverageIssue& Issue)
        {
            return Issue.Kind == EReferenceCoverageIssueKind::Broken;
        });
        SetResolutionFailure(
            Result,
            bBroken ? EReferenceResolutionStatus::Broken : EReferenceResolutionStatus::Unsupported,
            TEXT("The subject contains unresolved reference state, so Loomle cannot select a declaration factually."));
        return Result;
    }

    TArray<FCanonicalReference> Identities;
    for (const FExtractedReference* Fact : SelectedFacts)
    {
        AddUniqueCanonical(Identities, Fact->Identity);
    }
    if (Identities.Num() == 1)
    {
        Result.Status = EReferenceResolutionStatus::Resolved;
        Result.Subject.Identity = Identities[0];
        Result.Subject.QueryRef = ExplicitPath.IsEmpty()
            ? BaseSubject
            : BaseSubject + TEXT(".") + FString::Join(ExplicitPath, TEXT("."));
        return Result;
    }
    if (Identities.IsEmpty())
    {
        SetResolutionFailure(
            Result,
            EReferenceResolutionStatus::Unsupported,
            TEXT("The exact object is neither a supported declaration nor a supported static reference use-site."));
        return Result;
    }

    if (!ExplicitPath.IsEmpty())
    {
        SetResolutionFailure(
            Result,
            EReferenceResolutionStatus::Broken,
            TEXT("The exact native member path resolves to several declaration identities."));
        return Result;
    }

    TMap<FString, TSet<FCanonicalReference>> PathTargets;
    for (const FExtractedReference* Fact : SelectedFacts)
    {
        if (Fact->bSelectableMemberPath)
        {
            PathTargets.FindOrAdd(Fact->FieldPath).Add(Fact->Identity);
        }
    }
    TSet<FCanonicalReference> SelectableTargets;
    for (const TPair<FString, TSet<FCanonicalReference>>& Pair : PathTargets)
    {
        if (Pair.Value.Num() == 1)
        {
            for (const FCanonicalReference& Identity : Pair.Value)
            {
                SelectableTargets.Add(Identity);
            }
        }
    }
    if (SelectableTargets.Num() != Identities.Num())
    {
        SetResolutionFailure(
            Result,
            EReferenceResolutionStatus::Unsupported,
            TEXT("The object contains several declaration targets, but current SAL Member paths cannot select every target exactly; query the intended declaration directly."));
        return Result;
    }

    Result.Status = EReferenceResolutionStatus::Ambiguous;
    Result.Message = TEXT("The object contains several distinct declaration targets; select one native member path.");
    for (const FExtractedReference* Fact : SelectedFacts)
    {
        const TSet<FCanonicalReference>* TargetsForPath = PathTargets.Find(Fact->FieldPath);
        if (!Fact->bSelectableMemberPath || TargetsForPath == nullptr || TargetsForPath->Num() != 1)
        {
            continue;
        }
        const FString Candidate = TEXT("references to ") + BaseSubject + TEXT(".") + Fact->FieldPath;
        Result.Matches.AddUnique(Candidate);
    }
    return Result;
}

FReferenceSubjectResolution ResolveInterfaceGuid(
    UBlueprint* Blueprint,
    UEdGraph* Graph,
    const FString& QueryRef)
{
    FReferenceSubjectResolution Result;
    if (Blueprint == nullptr || Graph == nullptr || !Graph->InterfaceGuid.IsValid())
    {
        SetResolutionFailure(
            Result,
            EReferenceResolutionStatus::Broken,
            TEXT("Graph InterfaceGuid is absent or invalid."));
        return Result;
    }
    TArray<FCanonicalReference> Matches;
    for (const FBPInterfaceDescription& Interface : Blueprint->ImplementedInterfaces)
    {
        UBlueprint* InterfaceBlueprint = BlueprintForClass(Interface.Interface);
        CollectFunctionDeclarationsByGuid(InterfaceBlueprint, Graph->InterfaceGuid, Matches);
    }
    if (Matches.Num() != 1)
    {
        SetResolutionFailure(
            Result,
            EReferenceResolutionStatus::Broken,
            Matches.Num() > 1
                ? TEXT("Graph InterfaceGuid resolves to several Interface Function declarations.")
                : TEXT("Graph InterfaceGuid does not resolve in the Blueprint's implemented Interfaces."));
        return Result;
    }
    Result.Status = EReferenceResolutionStatus::Resolved;
    Result.Subject.Identity = Matches[0];
    Result.Subject.QueryRef = QueryRef;
    return Result;
}

FReferenceSubjectResolution ResolveRepNotify(
    UBlueprint* Blueprint,
    FBPVariableDescription* Variable,
    const FString& QueryRef)
{
    FReferenceSubjectResolution Result;
    if (Blueprint == nullptr || Variable == nullptr || Variable->RepNotifyFunc.IsNone())
    {
        SetResolutionFailure(
            Result,
            EReferenceResolutionStatus::Broken,
            TEXT("Variable RepNotifyFunc is absent or empty."));
        return Result;
    }
    FReferenceCoverageIssue Failure;
    if (!ResolveStoredFunctionName(Blueprint, Variable->RepNotifyFunc, Result.Subject.Identity, Failure))
    {
        Result.Status = Failure.Kind == EReferenceCoverageIssueKind::Unsupported
            ? EReferenceResolutionStatus::Unsupported
            : EReferenceResolutionStatus::Broken;
        Result.Message = Failure.Message;
        Result.Issues.Add(Failure);
        return Result;
    }
    Result.Status = EReferenceResolutionStatus::Resolved;
    Result.Subject.QueryRef = QueryRef;
    return Result;
}
}

FReferenceSubjectResolution FSalReferenceFacts::ResolveSubject(
    const FSalResolvedTarget& BoundTarget,
    const TSharedPtr<FJsonObject>& OperationTarget)
{
    using namespace ReferenceFactsPrivate;

    FReferenceSubjectResolution Result;
    if (!IsInGameThread())
    {
        SetResolutionFailure(
            Result,
            EReferenceResolutionStatus::Unsupported,
            TEXT("Reference facts must be resolved on the Unreal game thread."));
        return Result;
    }
    UBlueprint* Blueprint = BoundTarget.Blueprint;
    UEdGraph* GraphScope = BoundTarget.Kind == ESalTargetKind::Graph ? BoundTarget.Graph : nullptr;
    if (Blueprint == nullptr
        || (BoundTarget.Kind != ESalTargetKind::Blueprint && BoundTarget.Kind != ESalTargetKind::Graph))
    {
        SetResolutionFailure(
            Result,
            EReferenceResolutionStatus::Unsupported,
            TEXT("The confirmed reference fact provider requires a bound Blueprint or Graph target."));
        return Result;
    }

    TSharedPtr<FJsonObject> StableRef;
    TArray<FString> MemberPath;
    FString StableKind;
    FString StableId;
    if (!ReadSubjectRef(OperationTarget, StableRef, MemberPath)
        || !ReadStableRef(StableRef, StableKind, StableId))
    {
        SetResolutionFailure(
            Result,
            EReferenceResolutionStatus::Unsupported,
            TEXT("References target must be one typed StableRef or its direct native Member path."));
        return Result;
    }
    if (MemberPath.Num() > 1)
    {
        SetResolutionFailure(
            Result,
            EReferenceResolutionStatus::Unsupported,
            TEXT("This provider exposes only confirmed direct native reference-bearing Member paths."));
        return Result;
    }
    FGuid StableGuid;
    if (!ParseGuid(StableId, StableGuid))
    {
        SetResolutionFailure(
            Result,
            EReferenceResolutionStatus::NotFound,
            TEXT("The stable subject id is not a valid UE GUID."));
        return Result;
    }
    const FString BaseSubject = SubjectText(StableKind, StableId, {});
    const FString QueryRef = SubjectText(StableKind, StableId, MemberPath);

    if (StableKind == TEXT("variable") || StableKind == TEXT("dispatcher"))
    {
        TArray<FCanonicalReference> Matches;
        FBPVariableDescription* Variable = nullptr;
        CollectDirectVariables(
            Blueprint,
            GraphScope,
            StableGuid,
            StableKind == TEXT("dispatcher"),
            Matches,
            &Variable);
        if (Matches.Num() > 1)
        {
            SetResolutionFailure(
                Result,
                EReferenceResolutionStatus::Broken,
                TEXT("The stable Variable GUID is duplicated inside the bound scope."));
            return Result;
        }
        if (Matches.IsEmpty())
        {
            SetResolutionFailure(
                Result,
                EReferenceResolutionStatus::NotFound,
                TEXT("The exact Variable or Dispatcher declaration was not found in the bound scope."));
            return Result;
        }
        if (!MemberPath.IsEmpty())
        {
            if (MemberPath[0] != TEXT("RepNotifyFunc")
                || StableKind == TEXT("dispatcher")
                || Matches[0].Kind == EReferenceDeclarationKind::LocalVariable)
            {
                SetResolutionFailure(
                    Result,
                    EReferenceResolutionStatus::Unsupported,
                    TEXT("The exact Variable member is not a confirmed reference-bearing field."));
                return Result;
            }
            return ResolveRepNotify(Blueprint, Variable, QueryRef);
        }
        Result.Status = EReferenceResolutionStatus::Resolved;
        Result.Subject.Identity = Matches[0];
        Result.Subject.QueryRef = QueryRef;
        return Result;
    }

    if (StableKind == TEXT("graph"))
    {
        bool bDuplicate = false;
        // Graph declarations are owned by the Blueprint. The bound Graph limits
        // use-site scanning, not declaration lookup.
        UEdGraph* Graph = FindUniqueGraph(Blueprint, nullptr, StableGuid, bDuplicate);
        if (bDuplicate)
        {
            SetResolutionFailure(
                Result,
                EReferenceResolutionStatus::Broken,
                TEXT("Graph GUID is duplicated inside the bound scope."));
            return Result;
        }
        if (Graph == nullptr)
        {
            SetResolutionFailure(Result, EReferenceResolutionStatus::NotFound, TEXT("Graph was not found in the bound scope."));
            return Result;
        }
        if (!MemberPath.IsEmpty())
        {
            if (MemberPath[0] == TEXT("InterfaceGuid"))
            {
                return ResolveInterfaceGuid(Blueprint, Graph, QueryRef);
            }
            SetResolutionFailure(
                Result,
                EReferenceResolutionStatus::Unsupported,
                TEXT("The exact Graph member is not a confirmed reference-bearing field."));
            return Result;
        }
        if (IsFunctionDeclarationGraph(Blueprint, Graph))
        {
            Result.Subject.Identity = MakeGuidIdentity(
                EReferenceDeclarationKind::Function,
                Blueprint,
                Graph->GraphGuid,
                Graph->GetFName());
        }
        else if (IsMacroDeclarationGraph(Blueprint, Graph))
        {
            Result.Subject.Identity = MakeGuidIdentity(
                EReferenceDeclarationKind::Macro,
                Blueprint,
                Graph->GraphGuid,
                Graph->GetFName());
        }
        else
        {
            SetResolutionFailure(
                Result,
                EReferenceResolutionStatus::Unsupported,
                TEXT("This Graph kind is authored state but not a Function or Macro declaration."));
            return Result;
        }
        Result.Status = EReferenceResolutionStatus::Resolved;
        Result.Subject.QueryRef = QueryRef;
        return Result;
    }

    if (StableKind == TEXT("node"))
    {
        bool bDuplicate = false;
        UEdGraphNode* Node = FindUniqueNode(Blueprint, GraphScope, StableGuid, bDuplicate);
        if (bDuplicate)
        {
            SetResolutionFailure(
                Result,
                EReferenceResolutionStatus::Broken,
                TEXT("Node GUID is duplicated inside the bound scope."));
            return Result;
        }
        if (Node == nullptr)
        {
            SetResolutionFailure(Result, EReferenceResolutionStatus::NotFound, TEXT("Node was not found in the bound scope."));
            return Result;
        }
        if (MemberPath.IsEmpty())
        {
            if (UK2Node_CustomEvent* Event = Cast<UK2Node_CustomEvent>(Node);
                Event != nullptr && !Event->IsOverride())
            {
                Result.Status = EReferenceResolutionStatus::Resolved;
                Result.Subject.Identity = MakeGuidIdentity(
                    EReferenceDeclarationKind::CustomEvent,
                    Blueprint,
                    Event->NodeGuid,
                    Event->CustomFunctionName);
                Result.Subject.QueryRef = QueryRef;
                return Result;
            }
            if (Node->IsA<UK2Node_FunctionTerminator>())
            {
                UEdGraph* OwnerGraph = Node->GetGraph();
                if (IsFunctionDeclarationGraph(Blueprint, OwnerGraph))
                {
                    Result.Status = EReferenceResolutionStatus::Resolved;
                    Result.Subject.Identity = MakeGuidIdentity(
                        EReferenceDeclarationKind::Function,
                        Blueprint,
                        OwnerGraph->GraphGuid,
                        OwnerGraph->GetFName());
                    Result.Subject.QueryRef = QueryRef;
                    return Result;
                }
            }
        }
        return ResolveFromExtractedFacts(
            BaseSubject,
            MemberPath,
            ExtractNodeFacts(Blueprint, Node, !MemberPath.IsEmpty()));
    }

    if (StableKind == TEXT("component"))
    {
        if (!MemberPath.IsEmpty())
        {
            SetResolutionFailure(
                Result,
                EReferenceResolutionStatus::Unsupported,
                TEXT("Component declarations are Blueprint-owned and expose no confirmed reference-bearing Member path."));
            return Result;
        }
        bool bDuplicate = false;
        USCS_Node* Component = FindUniqueComponent(Blueprint, StableGuid, bDuplicate);
        if (bDuplicate)
        {
            SetResolutionFailure(Result, EReferenceResolutionStatus::Broken, TEXT("Component VariableGuid is duplicated."));
            return Result;
        }
        if (Component == nullptr)
        {
            SetResolutionFailure(Result, EReferenceResolutionStatus::NotFound, TEXT("Component declaration was not found."));
            return Result;
        }
        Result.Status = EReferenceResolutionStatus::Resolved;
        Result.Subject.Identity = MakeGuidIdentity(
            EReferenceDeclarationKind::Component,
            Blueprint,
            Component->VariableGuid,
            Component->GetVariableName());
        Result.Subject.QueryRef = QueryRef;
        return Result;
    }

    if (StableKind == TEXT("widget"))
    {
        UWidgetBlueprint* WidgetBlueprint = Cast<UWidgetBlueprint>(Blueprint);
        if (WidgetBlueprint == nullptr || !MemberPath.IsEmpty())
        {
            SetResolutionFailure(
                Result,
                EReferenceResolutionStatus::Unsupported,
                TEXT("Widget declarations require a bound Widget Blueprint and expose no confirmed reference-bearing Member path."));
            return Result;
        }
        bool bDuplicate = false;
        UWidget* Widget = FindUniqueWidgetByGuid(WidgetBlueprint, StableGuid, bDuplicate);
        if (bDuplicate)
        {
            SetResolutionFailure(Result, EReferenceResolutionStatus::Broken, TEXT("Widget GUID is duplicated."));
            return Result;
        }
        if (Widget == nullptr)
        {
            SetResolutionFailure(Result, EReferenceResolutionStatus::NotFound, TEXT("Widget declaration was not found."));
            return Result;
        }
        FReferenceCoverageIssue Failure;
        if (!WidgetIdentity(WidgetBlueprint, Widget, Result.Subject.Identity, Failure))
        {
            Result.Status = Failure.Kind == EReferenceCoverageIssueKind::Unsupported
                ? EReferenceResolutionStatus::Unsupported
                : EReferenceResolutionStatus::Broken;
            Result.Message = Failure.Message;
            Result.Issues.Add(Failure);
            return Result;
        }
        Result.Status = EReferenceResolutionStatus::Resolved;
        Result.Subject.QueryRef = QueryRef;
        return Result;
    }

    SetResolutionFailure(
        Result,
        EReferenceResolutionStatus::Unsupported,
        TEXT("This StableRef kind has no confirmed declaration identity in the Blueprint reference provider."));
    return Result;
}

namespace ReferenceFactsPrivate
{
bool IsVariableLikeTarget(const FCanonicalReference& Target)
{
    return Target.Kind == EReferenceDeclarationKind::BlueprintMemberVariable
        || Target.Kind == EReferenceDeclarationKind::LocalVariable
        || Target.Kind == EReferenceDeclarationKind::Dispatcher
        || Target.Kind == EReferenceDeclarationKind::Component
        || Target.Kind == EReferenceDeclarationKind::Widget
        || Target.Kind == EReferenceDeclarationKind::NativeProperty;
}

bool IsFunctionLikeTarget(const FCanonicalReference& Target)
{
    return Target.Kind == EReferenceDeclarationKind::Function
        || Target.Kind == EReferenceDeclarationKind::CustomEvent
        || Target.Kind == EReferenceDeclarationKind::NativeFunction;
}

UBlueprint* FindLoadedBlueprintByPath(const FString& Path)
{
    return !Path.IsEmpty() ? FindObject<UBlueprint>(nullptr, *Path) : nullptr;
}

UStruct* ScopeForTarget(const FCanonicalReference& Target)
{
    if (Target.IsNative())
    {
        return FindObject<UStruct>(nullptr, *Target.OwnerPath);
    }
    UBlueprint* OwnerBlueprint = FindLoadedBlueprintByPath(Target.OwnerPath);
    UClass* OwnerClass = BlueprintClass(OwnerBlueprint);
    if (Target.Kind != EReferenceDeclarationKind::LocalVariable)
    {
        return OwnerClass;
    }
    for (UEdGraph* Graph : BlueprintGraphs(OwnerBlueprint))
    {
        if (Graph != nullptr && Graph->GraphGuid == Target.ScopeGraphGuid && OwnerClass != nullptr)
        {
            return OwnerClass->FindFunctionByName(Graph->GetFName(), EIncludeSuperFlag::ExcludeSuper);
        }
    }
    return nullptr;
}

bool SameSite(const FReferenceUseSite& Left, const FReferenceUseSite& Right)
{
    if (Left.Kind != Right.Kind || Left.Blueprint != Right.Blueprint)
    {
        return false;
    }
    switch (Left.Kind)
    {
    case EReferenceUseSiteKind::Node: return Left.Graph == Right.Graph && Left.Node == Right.Node;
    case EReferenceUseSiteKind::Graph: return Left.Graph == Right.Graph;
    case EReferenceUseSiteKind::Variable: return Left.Variable == Right.Variable;
    case EReferenceUseSiteKind::Widget: return Left.Widget == Right.Widget;
    case EReferenceUseSiteKind::Blueprint: return true;
    default: return false;
    }
}

void AddSite(FReferenceScanResult& Result, FReferenceUseSite Site, const FString& MatchedPath)
{
    for (FReferenceUseSite& Existing : Result.Sites)
    {
        if (SameSite(Existing, Site))
        {
            Existing.MatchedPaths.AddUnique(MatchedPath);
            Existing.bCompound |= Site.bCompound;
            return;
        }
    }
    Site.MatchedPaths.AddUnique(MatchedPath);
    Result.Sites.Add(MoveTemp(Site));
}

void AddIssueUnique(FReferenceScanResult& Result, const FReferenceCoverageIssue& Issue)
{
    const bool bExists = Result.Issues.ContainsByPredicate([&Issue](const FReferenceCoverageIssue& Existing)
    {
        return Existing.Kind == Issue.Kind
            && Existing.ObjectRef == Issue.ObjectRef
            && Existing.FieldPath == Issue.FieldPath
            && Existing.Message == Issue.Message;
    });
    if (!bExists)
    {
        Result.Issues.Add(Issue);
    }
}

int32 DistinctFactCount(const FExtractionResult& Extracted)
{
    TSet<FCanonicalReference> Identities;
    for (const FExtractedReference& Fact : Extracted.Facts)
    {
        Identities.Add(Fact.Identity);
    }
    return Identities.Num();
}

void MergeNodeExtraction(
    FReferenceScanResult& OutResult,
    UBlueprint* Blueprint,
    UEdGraph* Graph,
    UEdGraphNode* Node,
    const FCanonicalReference& Target,
    const FExtractionResult& Extracted)
{
    bool bMatched = false;
    const bool bCompoundObject = DistinctFactCount(Extracted) > 1;
    for (const FExtractedReference& Fact : Extracted.Facts)
    {
        if (!(Fact.Identity == Target))
        {
            continue;
        }
        bMatched = true;
        FReferenceUseSite Site;
        Site.Kind = EReferenceUseSiteKind::Node;
        Site.Blueprint = Blueprint;
        Site.Graph = Graph;
        Site.Node = Node;
        Site.bCompound = bCompoundObject || Fact.bCompound;
        AddSite(OutResult, MoveTemp(Site), Fact.FieldPath);
    }
    for (const FExtractionResult::FUnresolved& Unresolved : Extracted.Unresolved)
    {
        if (MayIssueAffectTarget(Target, Unresolved.Hint))
        {
            AddIssueUnique(OutResult, Unresolved.Issue);
        }
    }

    UK2Node* K2Node = Cast<UK2Node>(Node);
    if (K2Node == nullptr || HasKnownExactNodeCoverage(Node) || bMatched || Target.Name.IsNone())
    {
        return;
    }
    UStruct* TargetScope = ScopeForTarget(Target);
    const bool bOpaqueVariableMatch = IsVariableLikeTarget(Target)
        && K2Node->ReferencesVariable(Target.Name, TargetScope);
    const bool bOpaqueFunctionMatch = IsFunctionLikeTarget(Target)
        && K2Node->ReferencesFunction(Target.Name, TargetScope);
    if (bOpaqueVariableMatch || bOpaqueFunctionMatch)
    {
        FReferenceCoverageIssue Issue;
        SetIssue(
            Issue,
            EReferenceCoverageIssueKind::Unsupported,
            NodeRef(Node),
            bOpaqueVariableMatch ? TEXT("ReferencesVariable") : TEXT("ReferencesFunction"),
            TEXT("The Node reports a possible reference through UE's coarse coverage API, but no exact native identity extractor is registered for its authored state."));
        AddIssueUnique(OutResult, Issue);
    }
}

void ScanRepNotifyFacts(
    FReferenceScanResult& OutResult,
    UBlueprint* Blueprint,
    const FCanonicalReference& Target)
{
    for (FBPVariableDescription& Variable : Blueprint->NewVariables)
    {
        if (Variable.RepNotifyFunc.IsNone())
        {
            continue;
        }
        FCanonicalReference Function;
        FReferenceCoverageIssue Failure;
        if (ResolveStoredFunctionName(Blueprint, Variable.RepNotifyFunc, Function, Failure))
        {
            if (Function == Target)
            {
                FReferenceUseSite Site;
                Site.Kind = EReferenceUseSiteKind::Variable;
                Site.Blueprint = Blueprint;
                Site.Variable = &Variable;
                Site.bCompound = false;
                AddSite(OutResult, MoveTemp(Site), TEXT("RepNotifyFunc"));
            }
            continue;
        }
        FRawReferenceHint Hint;
        Hint.Name = Variable.RepNotifyFunc;
        Hint.ExpectedKind = EExpectedMemberKind::Function;
        Hint.OwnerPath = BlueprintPath(Blueprint);
        if (MayIssueAffectTarget(Target, Hint))
        {
            Failure.ObjectRef = TEXT("variable@") + GuidText(Variable.VarGuid);
            Failure.FieldPath = TEXT("RepNotifyFunc");
            AddIssueUnique(OutResult, Failure);
        }
    }
}

void ScanWidgetBindingFacts(
    FReferenceScanResult& OutResult,
    UWidgetBlueprint* Blueprint,
    const FCanonicalReference& Target)
{
    if (Blueprint == nullptr)
    {
        return;
    }
    for (const FDelegateEditorBinding& Binding : Blueprint->Bindings)
    {
        UWidget* Widget = nullptr;
        const FExtractionResult Extracted = ExtractWidgetBindingFacts(Blueprint, Binding, Widget);
        const bool bCompoundObject = DistinctFactCount(Extracted) > 1;
        for (const FExtractedReference& Fact : Extracted.Facts)
        {
            if (!(Fact.Identity == Target))
            {
                continue;
            }
            if (Fact.FieldPath == TEXT("Bindings.ObjectName")
                && Target.Kind == EReferenceDeclarationKind::Widget)
            {
                // The binding is authored on the Widget Blueprint, while its
                // destination Widget is the declaration being queried. Return
                // the containing Blueprint rather than the declaration itself
                // or a synthetic Binding object.
                FReferenceUseSite Site;
                Site.Kind = EReferenceUseSiteKind::Blueprint;
                Site.Blueprint = Blueprint;
                Site.bCompound = true;
                AddSite(OutResult, MoveTemp(Site), Fact.FieldPath);
                continue;
            }
            FReferenceUseSite Site;
            Site.Kind = EReferenceUseSiteKind::Widget;
            Site.Blueprint = Blueprint;
            Site.Widget = Widget;
            Site.bCompound = bCompoundObject || Fact.bCompound;
            AddSite(OutResult, MoveTemp(Site), Fact.FieldPath);
        }
        for (const FExtractionResult::FUnresolved& Unresolved : Extracted.Unresolved)
        {
            if (MayIssueAffectTarget(Target, Unresolved.Hint))
            {
                AddIssueUnique(OutResult, Unresolved.Issue);
            }
        }
    }
}
}

FReferenceScanResult FSalReferenceFacts::ScanBlueprint(
    UBlueprint* Blueprint,
    UEdGraph* OptionalGraphScope,
    const FCanonicalReference& Target)
{
    using namespace ReferenceFactsPrivate;

    FReferenceScanResult Result;
    if (!IsInGameThread())
    {
        FReferenceCoverageIssue Issue;
        SetIssue(
            Issue,
            EReferenceCoverageIssueKind::Unsupported,
            FString(),
            FString(),
            TEXT("Reference facts must be scanned on the Unreal game thread."));
        Result.Issues.Add(Issue);
        return Result;
    }
    if (Blueprint == nullptr || !Target.IsValid())
    {
        FReferenceCoverageIssue Issue;
        SetIssue(
            Issue,
            EReferenceCoverageIssueKind::Broken,
            FString(),
            FString(),
            TEXT("Reference scan requires one loaded Blueprint and one valid canonical declaration identity."));
        Result.Issues.Add(Issue);
        return Result;
    }
    if (OptionalGraphScope != nullptr && OptionalGraphScope->GetTypedOuter<UBlueprint>() != Blueprint)
    {
        FReferenceCoverageIssue Issue;
        SetIssue(
            Issue,
            EReferenceCoverageIssueKind::Broken,
            GraphRef(OptionalGraphScope),
            FString(),
            TEXT("Optional Graph scope is not owned by the supplied Blueprint."));
        Result.Issues.Add(Issue);
        return Result;
    }

    const TArray<UEdGraph*> Graphs = OptionalGraphScope != nullptr
        ? TArray<UEdGraph*>({OptionalGraphScope})
        : BlueprintGraphs(Blueprint);
    for (UEdGraph* Graph : Graphs)
    {
        if (Graph == nullptr)
        {
            continue;
        }
        for (UEdGraphNode* Node : Graph->Nodes)
        {
            if (Node == nullptr)
            {
                continue;
            }
            MergeNodeExtraction(
                Result,
                Blueprint,
                Graph,
                Node,
                Target,
                ExtractNodeFacts(Blueprint, Node, false));
        }
    }

    if (OptionalGraphScope == nullptr)
    {
        ScanRepNotifyFacts(Result, Blueprint, Target);
        ScanWidgetBindingFacts(Result, Cast<UWidgetBlueprint>(Blueprint), Target);
    }
    return Result;
}
}
