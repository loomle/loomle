// Copyright 2026 Loomle contributors.

#include "SalLevelComponentLocator.h"

#include "../SalDiagnostics.h"
#include "Components/ActorComponent.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Engine/SCS_Node.h"
#include "Engine/SimpleConstructionScript.h"
#include "GameFramework/Actor.h"
#include "PCGComponent.h"
#include "Helpers/PCGHelpers.h"
#include "UObject/Package.h"
#include "UObject/UnrealType.h"

namespace Loomle::Sal
{
namespace
{
constexpr int32 MaxDiagnostics = 64;
constexpr int32 MaxComponents = 100000;
constexpr int32 MaxComponentsPerActor = 4096;
constexpr int32 MaxSCSClassesPerActor = 256;
constexpr int32 MaxSCSNodes = 100000;
constexpr int32 MaxSCSEdges = 200000;
constexpr int32 MaxSCSDepth = 4096;
constexpr int64 MaxStringBytes = 64ll * 1024ll * 1024ll;
constexpr EObjectFlags IncompleteLoadFlags =
    RF_NeedLoad
    | RF_NeedPostLoad
    | RF_NeedPostLoadSubobjects
    | RF_WillBeLoaded;

TSharedPtr<FJsonObject> ComponentWarning(
    const FString& Code,
    const FString& Message,
    const FString& Operation,
    const FString& Ref = FString())
{
    FSalDiagnosticBuilder Builder = FSalDiagnostics::Warning(Code, Message)
        .Interface(TEXT("level"))
        .Operation(Operation);
    if (!Ref.IsEmpty())
    {
        Builder.Ref(Ref);
    }
    return Builder.Build();
}

class FComponentBudget
{
public:
    bool ExamineComponent()
    {
        return ComponentCount++ < MaxComponents;
    }

    bool ExamineNode()
    {
        return NodeCount++ < MaxSCSNodes;
    }

    bool ExamineEdge()
    {
        return EdgeCount++ < MaxSCSEdges;
    }

    bool Consume(const FString& Value)
    {
        const int64 Bytes = Value.IsEmpty()
            ? 0
            : (static_cast<int64>(Value.Len()) + 1) * sizeof(TCHAR);
        if (Bytes < 0 || Bytes > MaxStringBytes - StringBytes)
        {
            return false;
        }
        StringBytes += Bytes;
        return true;
    }

private:
    int32 ComponentCount = 0;
    int32 NodeCount = 0;
    int32 EdgeCount = 0;
    int64 StringBytes = 0;
};

void AddDiagnostic(
    FSalLevelComponentSnapshot& Out,
    const TSharedPtr<FJsonObject>& Diagnostic)
{
    if (Out.Diagnostics.Num() < MaxDiagnostics)
    {
        Out.Diagnostics.Add(Diagnostic);
    }
    else
    {
        Out.bDiagnosticsTruncated = true;
    }
}

void MarkIncomplete(
    FSalLevelComponentSnapshot& Out,
    FString& OutReason,
    const FString& Message,
    const FString& Operation,
    const FString& Ref = FString())
{
    Out.bIdentityComplete = false;
    if (OutReason.IsEmpty())
    {
        OutReason = Message;
    }
    AddDiagnostic(
        Out,
        ComponentWarning(
            TEXT("validation.reference_scan_incomplete"),
            Message,
            Operation,
            Ref));
}

bool IsDurableDirectComponent(
    const UActorComponent* Component,
    const AActor* Actor)
{
    if (!IsValid(Component)
        || !IsValid(Actor)
        || Component->GetOwner() != Actor
        || Component->GetOuter() != Actor
        || Component->IsTemplate()
        || Component->IsBeingDestroyed()
        || Component->IsVisualizationComponent()
        || Component->HasAnyFlags(
            RF_Transient
            | RF_ClassDefaultObject
            | RF_ArchetypeObject
            | RF_NewerVersionExists
            | IncompleteLoadFlags))
    {
        return false;
    }
    if (Component->ComponentHasTag(PCGHelpers::DefaultPCGTag)
        || Component->ComponentHasTag(PCGHelpers::DefaultPCGDebugTag)
        || Component->ComponentHasTag(PCGHelpers::MarkedForCleanupPCGTag))
    {
        return false;
    }
    const UPackage* ActorPackage = Actor->GetOutermost();
    if (ActorPackage == nullptr
        || Component->GetOutermost() != ActorPackage
        || ActorPackage == GetTransientPackage()
        || ActorPackage->HasAnyFlags(RF_Transient)
        || ActorPackage->HasAnyPackageFlags(PKG_PlayInEditor))
    {
        return false;
    }
    if (const UPCGComponent* PCG = Cast<UPCGComponent>(Component))
    {
        if (PCG->IsLocalComponent()
            || PCG->GetConstOriginalComponent() != PCG)
        {
            return false;
        }
    }
    return true;
}

bool IsValidLoadedClass(const UClass* Class)
{
    return IsValid(Class)
        && !Class->HasAnyFlags(IncompleteLoadFlags | RF_NewerVersionExists)
        && !Class->HasAnyClassFlags(CLASS_NewerVersionExists);
}

bool ProveNative(
    UActorComponent* Component,
    AActor* Actor)
{
    if (Component->CreationMethod != EComponentCreationMethod::Native
        || !Component->HasAnyFlags(RF_DefaultSubObject)
        || Component->GetFName().IsNone())
    {
        return false;
    }
    UClass* ActorClass = Actor->GetClass();
    AActor* CDO = IsValidLoadedClass(ActorClass)
        ? Cast<AActor>(ActorClass->GetDefaultObject(false))
        : nullptr;
    UActorComponent* DefaultComponent = CDO != nullptr
        ? Cast<UActorComponent>(
            CDO->GetDefaultSubobjectByName(Component->GetFName()))
        : nullptr;
    return IsValid(DefaultComponent)
        && DefaultComponent->GetOuter() == CDO
        && DefaultComponent->CreationMethod == EComponentCreationMethod::Native
        && DefaultComponent->HasAnyFlags(RF_DefaultSubObject)
        && DefaultComponent->GetClass() == Component->GetClass();
}

template <typename ElementType>
TMap<UActorComponent*, int32> BuildPointerMultiplicity(
    const TArray<ElementType>& Values)
{
    TMap<UActorComponent*, int32> Counts;
    Counts.Reserve(Values.Num());
    for (UActorComponent* Value : Values)
    {
        ++Counts.FindOrAdd(Value);
    }
    return Counts;
}

struct FSCSProof
{
    UActorComponent* Component = nullptr;
    FString Id;
    FString DeclaringClass;
};

bool BuildSCSProofs(
    AActor* Actor,
    const TSet<UActorComponent*>& DurableComponents,
    const TMap<UActorComponent*, int32>& BlueprintMultiplicity,
    const FString& Operation,
    FComponentBudget& Budget,
    FSalLevelComponentSnapshot& Out,
    FString& OutReason,
    TMap<UActorComponent*, TArray<FSCSProof>>& OutProofs)
{
    TSet<const UClass*> SeenClasses;
    int32 ClassDepth = 0;
    for (UClass* Class = Actor->GetClass(); Class != nullptr;
         Class = Class->GetSuperClass())
    {
        if (++ClassDepth > MaxSCSClassesPerActor
            || SeenClasses.Contains(Class))
        {
            MarkIncomplete(
                Out,
                OutReason,
                TEXT("The Blueprint generated-Class hierarchy is cyclic or exceeds the bounded Component identity depth."),
                Operation,
                Actor->GetPathName());
            return false;
        }
        SeenClasses.Add(Class);
        UBlueprintGeneratedClass* BPGC = Cast<UBlueprintGeneratedClass>(Class);
        if (BPGC == nullptr)
        {
            continue;
        }
        if (!IsValidLoadedClass(BPGC))
        {
            MarkIncomplete(
                Out,
                OutReason,
                TEXT("A Blueprint generated Class is incomplete or superseded during Component identity Query."),
                Operation,
                BPGC->GetPathName());
            return false;
        }
        USimpleConstructionScript* SCS = BPGC->SimpleConstructionScript;
        if (SCS == nullptr)
        {
            continue;
        }
        if (!IsValid(SCS)
            || SCS->HasAnyFlags(IncompleteLoadFlags | RF_NewerVersionExists)
            || SCS->GetOuter() != BPGC)
        {
            MarkIncomplete(
                Out,
                OutReason,
                TEXT("A SimpleConstructionScript is incomplete or does not belong to its declaring generated Class."),
                Operation,
                BPGC->GetPathName());
            return false;
        }

        struct FStackItem
        {
            USCS_Node* Node = nullptr;
            int32 Depth = 0;
        };
        TArray<FStackItem> Stack;
        const TArray<USCS_Node*>& Roots = SCS->GetRootNodes();
        if (Roots.Num() > MaxSCSNodes)
        {
            MarkIncomplete(
                Out,
                OutReason,
                TEXT("The SimpleConstructionScript root count exceeds the bounded node budget."),
                Operation,
                BPGC->GetPathName());
            return false;
        }
        Stack.Reserve(Roots.Num());
        for (int32 Index = Roots.Num() - 1; Index >= 0; --Index)
        {
            Stack.Add({Roots[Index], 0});
        }
        TSet<const USCS_Node*> SeenNodes;
        TSet<FGuid> SeenVariableGuids;
        TSet<FName> SeenVariableNames;
        while (!Stack.IsEmpty())
        {
            const FStackItem Item = Stack.Pop(EAllowShrinking::No);
            USCS_Node* Node = Item.Node;
            if (!Budget.ExamineNode()
                || Item.Depth > MaxSCSDepth)
            {
                MarkIncomplete(
                    Out,
                    OutReason,
                    TEXT("The bounded SimpleConstructionScript node budget was exceeded."),
                    Operation,
                    BPGC->GetPathName());
                return false;
            }
            if (!IsValid(Node)
                || Node->HasAnyFlags(IncompleteLoadFlags | RF_NewerVersionExists)
                || Node->GetOuter() != SCS
                || SeenNodes.Contains(Node))
            {
                MarkIncomplete(
                    Out,
                    OutReason,
                    TEXT("The SimpleConstructionScript tree contains a null, superseded, shared, or cyclic node."),
                    Operation,
                    BPGC->GetPathName());
                return false;
            }
            SeenNodes.Add(Node);
            const TArray<USCS_Node*>& Children = Node->GetChildNodes();
            for (int32 Index = Children.Num() - 1; Index >= 0; --Index)
            {
                if (!Budget.ExamineEdge())
                {
                    MarkIncomplete(
                        Out,
                        OutReason,
                        TEXT("The bounded SimpleConstructionScript edge budget was exceeded."),
                        Operation,
                        BPGC->GetPathName());
                    return false;
                }
                Stack.Add({Children[Index], Item.Depth + 1});
            }

            const FName VariableName = Node->GetVariableName();
            if (Node->VariableGuid.IsValid()
                && SeenVariableGuids.Contains(Node->VariableGuid))
            {
                MarkIncomplete(
                    Out,
                    OutReason,
                    TEXT("One declaring SimpleConstructionScript contains duplicate persistent VariableGuid identities."),
                    Operation,
                    BPGC->GetPathName());
                return false;
            }
            if (Node->VariableGuid.IsValid())
            {
                SeenVariableGuids.Add(Node->VariableGuid);
            }
            if (!VariableName.IsNone()
                && SeenVariableNames.Contains(VariableName))
            {
                MarkIncomplete(
                    Out,
                    OutReason,
                    TEXT("One declaring SimpleConstructionScript contains duplicate Component variable names."),
                    Operation,
                    BPGC->GetPathName());
                return false;
            }
            if (!VariableName.IsNone())
            {
                SeenVariableNames.Add(VariableName);
            }
            if (!Node->VariableGuid.IsValid()
                || VariableName.IsNone()
                || !IsValidLoadedClass(Node->ComponentClass))
            {
                continue;
            }
            FObjectPropertyBase* Property = FindFProperty<FObjectPropertyBase>(
                Actor->GetClass(),
                VariableName);
            if (Property == nullptr
                || Property->GetOwnerClass() != BPGC
                || (Property->PropertyClass != nullptr
                    && Property->PropertyClass != Node->ComponentClass))
            {
                continue;
            }
            UActorComponent* Live = Cast<UActorComponent>(
                Property->GetObjectPropertyValue_InContainer(Actor));
            if (!DurableComponents.Contains(Live)
                || Live->CreationMethod
                    != EComponentCreationMethod::SimpleConstructionScript
                || Live->GetClass() != Node->ComponentClass
                || BlueprintMultiplicity.FindRef(Live) != 1)
            {
                continue;
            }
            FSCSProof Proof;
            Proof.Component = Live;
            Proof.DeclaringClass = BPGC->GetPathName();
            Proof.Id = Proof.DeclaringClass
                + TEXT("#")
                + Node->VariableGuid.ToString(
                    EGuidFormats::DigitsWithHyphensLower);
            OutProofs.FindOrAdd(Live).Add(MoveTemp(Proof));
        }
    }
    return true;
}

bool AddEntry(
    UActorComponent* Component,
    AActor* Actor,
    const FString& Source,
    const FString& Id,
    const FString& CreationMethod,
    const FString& DeclaringClass,
    const FString& Operation,
    FComponentBudget& Budget,
    FSalLevelComponentSnapshot& Out,
    FString& OutReason)
{
    const FString Name = Component->GetFName().ToString();
    const FString Type = Component->GetClass()->GetPathName();
    for (const FString& Value : {
             Actor->GetActorGuid().ToString(
                 EGuidFormats::DigitsWithHyphensLower),
             Source,
             Id,
             Name,
             Type,
             CreationMethod,
             DeclaringClass})
    {
        if (!Budget.Consume(Value))
        {
            MarkIncomplete(
                Out,
                OutReason,
                TEXT("The bounded Component identity string budget was exceeded."),
                Operation,
                Actor->GetPathName());
            return false;
        }
    }
    FSalLevelComponentEntry& Entry = Out.Entries.AddDefaulted_GetRef();
    Entry.ActorGuid = Actor->GetActorGuid();
    Entry.Actor = Actor;
    Entry.Component = Component;
    Entry.Source = Source;
    Entry.Id = Id;
    Entry.Name = Name;
    Entry.Type = Type;
    Entry.CreationMethod = CreationMethod;
    Entry.DeclaringClass = DeclaringClass;
    Entry.bRegistered = Component->IsRegistered();
    return true;
}

int32 SourceRank(const FString& Source)
{
    if (Source == TEXT("native"))
    {
        return 0;
    }
    if (Source == TEXT("scs"))
    {
        return 1;
    }
    return 2;
}
}

TArray<TSharedPtr<FJsonObject>> FSalLevelComponentSnapshot::FinalDiagnostics(
    const FString& Operation) const
{
    TArray<TSharedPtr<FJsonObject>> Result = Diagnostics;
    if (bDiagnosticsTruncated)
    {
        Result.Add(ComponentWarning(
            TEXT("validation.reference_scan_incomplete"),
            TEXT("Additional Component identity diagnostics were omitted after the bounded diagnostic limit."),
            Operation));
    }
    return Result;
}

bool BuildLevelComponentSnapshot(
    const TArray<AActor*>& Actors,
    const FString& Operation,
    FSalLevelComponentSnapshot& Out,
    FString& OutReason)
{
    Out = FSalLevelComponentSnapshot();
    OutReason.Reset();
    FComponentBudget Budget;
    TSet<const AActor*> SeenActors;
    for (AActor* Actor : Actors)
    {
        if (!IsValid(Actor)
            || !Actor->GetActorGuid().IsValid()
            || SeenActors.Contains(Actor))
        {
            MarkIncomplete(
                Out,
                OutReason,
                TEXT("The Component identity input contains an invalid or repeated Actor."),
                Operation);
            continue;
        }
        SeenActors.Add(Actor);

        const TSet<UActorComponent*>& Components = Actor->GetComponents();
        const TArray<UActorComponent*>& InstanceComponents =
            Actor->GetInstanceComponents();
        if (Components.Num() > MaxComponentsPerActor
            || InstanceComponents.Num() > MaxComponentsPerActor
            || Actor->BlueprintCreatedComponents.Num()
                > MaxComponentsPerActor)
        {
            MarkIncomplete(
                Out,
                OutReason,
                TEXT("An Actor exceeds a bounded owned, instance, or Blueprint-created Component identity budget."),
                Operation,
                Actor->GetPathName());
            continue;
        }
        const TMap<UActorComponent*, int32> InstanceMultiplicity =
            BuildPointerMultiplicity(InstanceComponents);
        const TMap<UActorComponent*, int32> BlueprintMultiplicity =
            BuildPointerMultiplicity(Actor->BlueprintCreatedComponents);
        TSet<UActorComponent*> Durable;
        for (UActorComponent* Component : Components)
        {
            if (!Budget.ExamineComponent())
            {
                MarkIncomplete(
                    Out,
                    OutReason,
                    TEXT("The bounded total Component identity budget was exceeded."),
                    Operation,
                    Actor->GetPathName());
                break;
            }
            if (IsDurableDirectComponent(Component, Actor))
            {
                Durable.Add(Component);
            }
        }
        if (!Out.bIdentityComplete)
        {
            continue;
        }

        TMap<UActorComponent*, TArray<FSCSProof>> SCSProofs;
        if (!BuildSCSProofs(
                Actor,
                Durable,
                BlueprintMultiplicity,
                Operation,
                Budget,
                Out,
                OutReason,
                SCSProofs))
        {
            continue;
        }
        for (UActorComponent* Component : Components)
        {
            if (!Durable.Contains(Component))
            {
                continue;
            }
            switch (Component->CreationMethod)
            {
            case EComponentCreationMethod::Native:
                if (ProveNative(Component, Actor))
                {
                    if (!AddEntry(
                            Component,
                            Actor,
                            TEXT("native"),
                            Component->GetFName().ToString(),
                            TEXT("Native"),
                            FString(),
                            Operation,
                            Budget,
                            Out,
                            OutReason))
                    {
                        break;
                    }
                }
                break;
            case EComponentCreationMethod::Instance:
                if (!Component->GetFName().IsNone()
                    && InstanceMultiplicity.FindRef(Component) == 1)
                {
                    if (!AddEntry(
                            Component,
                            Actor,
                            TEXT("instance"),
                            Component->GetFName().ToString(),
                            TEXT("Instance"),
                            FString(),
                            Operation,
                            Budget,
                            Out,
                            OutReason))
                    {
                        break;
                    }
                }
                break;
            case EComponentCreationMethod::SimpleConstructionScript:
                if (const TArray<FSCSProof>* Proofs = SCSProofs.Find(Component))
                {
                    if (Proofs->Num() == 1)
                    {
                        if (!AddEntry(
                                Component,
                                Actor,
                                TEXT("scs"),
                                (*Proofs)[0].Id,
                                TEXT("SimpleConstructionScript"),
                                (*Proofs)[0].DeclaringClass,
                                Operation,
                                Budget,
                                Out,
                                OutReason))
                        {
                            break;
                        }
                    }
                    else if (Proofs->Num() > 1)
                    {
                        MarkIncomplete(
                            Out,
                            OutReason,
                            TEXT("One live SCS Component maps to multiple persistent SCS declarations."),
                            Operation,
                            Component->GetPathName());
                    }
                }
                break;
            case EComponentCreationMethod::UserConstructionScript:
            default:
                break;
            }
        }
    }

    TMap<FString, int32> IdentityCounts;
    for (const FSalLevelComponentEntry& Entry : Out.Entries)
    {
        const FString Key = Entry.ActorGuid.ToString(
                EGuidFormats::DigitsWithHyphensLower)
            + TEXT("|") + Entry.Source
            + TEXT("|") + Entry.Id;
        ++IdentityCounts.FindOrAdd(Key);
    }
    for (const TPair<FString, int32>& Pair : IdentityCounts)
    {
        if (Pair.Value > 1)
        {
            MarkIncomplete(
                Out,
                OutReason,
                TEXT("Multiple Components share one source-qualified persistent slot identity."),
                Operation,
                Pair.Key);
        }
    }
    Out.Entries.Sort([](
        const FSalLevelComponentEntry& Left,
        const FSalLevelComponentEntry& Right)
    {
        if (Left.ActorGuid != Right.ActorGuid)
        {
            return Left.ActorGuid < Right.ActorGuid;
        }
        const int32 LeftRank = SourceRank(Left.Source);
        const int32 RightRank = SourceRank(Right.Source);
        if (LeftRank != RightRank)
        {
            return LeftRank < RightRank;
        }
        const int32 IdOrder = Left.Id.Compare(Right.Id);
        if (IdOrder != 0)
        {
            return IdOrder < 0;
        }
        const int32 TypeOrder = Left.Type.Compare(Right.Type);
        if (TypeOrder != 0)
        {
            return TypeOrder < 0;
        }
        return Left.Name < Right.Name;
    });
    return Out.bIdentityComplete;
}
}
