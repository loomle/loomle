// Copyright 2026 Loomle contributors.

#if WITH_DEV_AUTOMATION_TESTS

#include "Sal/Blueprint/SalBlueprintInterface.h"
#include "Sal/SalModule.h"
#include "LoomleTestObjectIteration.h"
#include "LoomleTestEditorState.h"

#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetBlueprintGeneratedClass.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "Editor.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "EdGraphSchema_K2.h"
#include "Engine/SCS_Node.h"
#include "Engine/SimpleConstructionScript.h"
#include "Engine/TimelineTemplate.h"
#include "GameFramework/Actor.h"
#include "K2Node_BaseMCDelegate.h"
#include "K2Node_CallFunction.h"
#include "K2Node_FunctionEntry.h"
#include "K2Node_Variable.h"
#include "K2Node_VariableGet.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/CompilerResultsLog.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/AutomationTest.h"
#include "ScopedTransaction.h"
#include "UObject/GarbageCollection.h"
#include "UObject/UObjectHash.h"
#include "UObject/Package.h"
#include "UObject/UnrealType.h"
#include "WidgetBlueprint.h"

namespace
{
using namespace Loomle::Sal;

enum class EBlueprintMutationFixtureKind : uint8
{
    Blueprint,
    WidgetBlueprint
};

bool UnloadBlueprintMutationTestPackage(
    UPackage* Package,
    FString& OutError)
{
    OutError.Reset();
    if (Package == nullptr)
    {
        return true;
    }

    const FString PackageName = Package->GetName();
    Package->SetDirtyFlag(false);
    Package->ClearFlags(RF_Public | RF_Standalone);
    ForEachObjectWithPackage(
        Package,
        [](UObject* Object)
        {
            Object->ClearFlags(RF_Public | RF_Standalone);
            return true;
        },
        Loomle::Tests::IncludeNestedObjects);

    CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);
    if (FindPackage(nullptr, *PackageName) != nullptr)
    {
        OutError = TEXT("Fixture package remained loaded: ")
            + PackageName;
        return false;
    }
    return true;
}

class FBlueprintMutationFixture
{
public:
    explicit FBlueprintMutationFixture(
        const EBlueprintMutationFixtureKind Kind)
        : Label(
            Kind == EBlueprintMutationFixtureKind::WidgetBlueprint
                ? TEXT("Widget Blueprint")
                : TEXT("Blueprint"))
    {
        const FString Token =
            FGuid::NewGuid().ToString(EGuidFormats::Digits);
        Package = CreatePackage(*FString::Printf(
            TEXT("/Game/LoomleTests/SalBlueprintMutation_%s"),
            *Token));
        const FName BlueprintName(*FString::Printf(
            TEXT("BP_Mutation_%s"),
            *Token));
        if (Kind == EBlueprintMutationFixtureKind::WidgetBlueprint)
        {
            Blueprint = FKismetEditorUtilities::CreateBlueprint(
                UUserWidget::StaticClass(),
                Package,
                BlueprintName,
                BPTYPE_Normal,
                UWidgetBlueprint::StaticClass(),
                UWidgetBlueprintGeneratedClass::StaticClass(),
                NAME_None);
        }
        else
        {
            Blueprint = FKismetEditorUtilities::CreateBlueprint(
                AActor::StaticClass(),
                Package,
                BlueprintName,
                BPTYPE_Normal,
                UBlueprint::StaticClass(),
                UBlueprintGeneratedClass::StaticClass(),
                NAME_None);
        }
        if (Package != nullptr)
        {
            Package->SetDirtyFlag(false);
        }
    }

    ~FBlueprintMutationFixture()
    {
        FString Ignored;
        Cleanup(Ignored);
    }

    FBlueprintMutationFixture(const FBlueprintMutationFixture&) = delete;
    FBlueprintMutationFixture& operator=(const FBlueprintMutationFixture&) = delete;

    bool Cleanup(FString& OutError)
    {
        if (bCleaned)
        {
            OutError.Reset();
            return true;
        }
        bCleaned = true;

        UPackage* PackageToUnload = Package;
        if (Blueprint != nullptr)
        {
            Blueprint->ClearFlags(RF_Public | RF_Standalone);
        }
        Blueprint = nullptr;
        Package = nullptr;
        return UnloadBlueprintMutationTestPackage(
            PackageToUnload,
            OutError);
    }

    UPackage* Package = nullptr;
    UBlueprint* Blueprint = nullptr;
    FString Label;

private:
    bool bCleaned = false;
};

TSharedRef<FJsonObject> LocalReference(const FString& Alias)
{
    TSharedRef<FJsonObject> Reference = MakeShared<FJsonObject>();
    Reference->SetStringField(TEXT("kind"), TEXT("local"));
    Reference->SetStringField(TEXT("name"), Alias);
    return Reference;
}

TSharedRef<FJsonObject> MemberReference(
    const TSharedRef<FJsonObject>& Owner,
    const FString& Field)
{
    TSharedRef<FJsonObject> Reference = MakeShared<FJsonObject>();
    Reference->SetStringField(TEXT("kind"), TEXT("member"));
    Reference->SetObjectField(TEXT("object"), Owner);
    Reference->SetArrayField(
        TEXT("path"),
        {MakeShared<FJsonValueString>(Field)});
    return Reference;
}

FSalPatch DescriptionPatch(
    const FString& Description,
    const bool bDryRun)
{
    static const FString Alias = TEXT("blueprint");
    TSharedRef<FJsonObject> Set = MakeShared<FJsonObject>();
    Set->SetStringField(TEXT("kind"), TEXT("set"));
    Set->SetObjectField(
        TEXT("target"),
        MemberReference(
            LocalReference(Alias),
            TEXT("BlueprintDescription")));
    Set->SetStringField(TEXT("value"), Description);

    FSalPatch Patch;
    Patch.Alias = Alias;
    Patch.bDryRun = bDryRun;
    Patch.Statements = {MakeShared<FJsonValueObject>(Set)};
    return Patch;
}

/**
 * Test-only fault injector for the gap between transient preflight and live
 * apply. A writable string field is imported twice during preflight (validate,
 * then apply to the sandbox). The next import is the live validation pass and
 * fails, after an earlier statement has already mutated the live Blueprint.
 *
 * Production SAL JSON values are immutable. This deliberately stateful value
 * models an equivalent UE-native failure that can occur only on the live
 * object, without adding a production-only failure hook.
 */
class FJsonValueFailAfterPreflight final : public FJsonValueString
{
public:
    explicit FJsonValueFailAfterPreflight(const FString& InValue)
        : FJsonValueString(InValue)
    {
    }

    virtual ~FJsonValueFailAfterPreflight() override = default;

    virtual bool TryGetString(FString& OutString) const override
    {
        ++ReadCount;
        if (ShouldFailConversions())
        {
            return false;
        }
        return FJsonValueString::TryGetString(OutString);
    }

    virtual bool TryGetNumber(double& OutNumber) const override
    {
        return ShouldFailConversions()
            ? false
            : FJsonValueString::TryGetNumber(OutNumber);
    }

    virtual bool TryGetBool(bool& OutBool) const override
    {
        return ShouldFailConversions()
            ? false
            : FJsonValueString::TryGetBool(OutBool);
    }

    int32 GetReadCount() const
    {
        return ReadCount;
    }

private:
    bool ShouldFailConversions() const
    {
        return ReadCount > 2;
    }

    mutable int32 ReadCount = 0;
};

TSharedRef<FJsonObject> DescriptionSetStatement(
    const TSharedPtr<FJsonValue>& Value)
{
    static const FString Alias = TEXT("blueprint");
    TSharedRef<FJsonObject> Set = MakeShared<FJsonObject>();
    Set->SetStringField(TEXT("kind"), TEXT("set"));
    Set->SetObjectField(
        TEXT("target"),
        MemberReference(
            LocalReference(Alias),
            TEXT("BlueprintDescription")));
    Set->SetField(TEXT("value"), Value);
    return Set;
}

FSalResolvedTarget BlueprintTarget(UBlueprint* Blueprint)
{
    FSalResolvedTarget Target;
    Target.Kind = ESalTargetKind::Blueprint;
    Target.Alias = TEXT("blueprint");
    Target.AssetPath = Blueprint != nullptr ? Blueprint->GetPathName() : FString();
    Target.Object = Blueprint;
    Target.Package = Blueprint != nullptr ? Blueprint->GetOutermost() : nullptr;
    Target.Blueprint = Blueprint;
    Target.Class = Blueprint != nullptr ? Blueprint->GeneratedClass.Get() : nullptr;
    Target.Interfaces = {
        FName(TEXT("asset")),
        FName(TEXT("blueprint"))};
    return Target;
}

bool ResultBool(
    const TSharedPtr<FJsonObject>& Result,
    const TCHAR* Field,
    const bool Default = false)
{
    bool Value = Default;
    return Result.IsValid() && Result->TryGetBoolField(Field, Value)
        ? Value
        : Default;
}

bool HasMutationDiagnosticCode(
    const TSharedPtr<FJsonObject>& Result,
    const FString& Expected)
{
    const TArray<TSharedPtr<FJsonValue>>* Diagnostics = nullptr;
    if (!Result.IsValid()
        || !Result->TryGetArrayField(TEXT("diagnostics"), Diagnostics)
        || Diagnostics == nullptr)
    {
        return false;
    }
    for (const TSharedPtr<FJsonValue>& Value : *Diagnostics)
    {
        const TSharedPtr<FJsonObject>* Diagnostic = nullptr;
        FString Code;
        if (Value.IsValid() && Value->TryGetObject(Diagnostic)
            && Diagnostic != nullptr
            && (*Diagnostic)->TryGetStringField(TEXT("code"), Code)
            && Code == Expected)
        {
            return true;
        }
    }
    return false;
}

class FScopedTestDefaultObjectOverride
{
public:
    FScopedTestDefaultObjectOverride(UClass* InClass, UObject* Replacement)
        : Class(InClass)
        , OriginalCDO(InClass->GetDefaultObject(false))
    {
        Class->SetDefaultObject(Replacement);
    }

    ~FScopedTestDefaultObjectOverride()
    {
        Class->SetDefaultObject(OriginalCDO.Get());
    }

private:
    UClass* Class;
    // The original CDO is temporarily detached from its class. Retain it until
    // restoration; none of these rejection scenarios compiles the class.
    TStrongObjectPtr<UObject> OriginalCDO;
};

struct FBlueprintTestStableIds
{
    TMap<FString, FGuid> Graphs;
    TMap<FString, FGuid> Nodes;
    TMap<FString, FGuid> Pins;
    TMap<FString, FGuid> MemberReferences;
};

FString StableGraphPath(
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

FBlueprintTestStableIds CaptureTestStableIds(
    const UBlueprint* Blueprint)
{
    FBlueprintTestStableIds Snapshot;
    TArray<UEdGraph*> Graphs;
    Blueprint->GetAllGraphs(Graphs);
    for (const UEdGraph* Graph : Graphs)
    {
        if (Graph == nullptr)
        {
            continue;
        }
        const FString GraphKey =
            StableGraphPath(Graph, Blueprint);
        Snapshot.Graphs.Add(GraphKey, Graph->GraphGuid);
        for (const UEdGraphNode* Node : Graph->Nodes)
        {
            if (Node == nullptr)
            {
                continue;
            }
            const FString NodeKey =
                GraphKey + TEXT("/node:") + Node->GetName();
            Snapshot.Nodes.Add(NodeKey, Node->NodeGuid);
            if (const UK2Node_Variable* VariableNode =
                    Cast<UK2Node_Variable>(Node))
            {
                Snapshot.MemberReferences.Add(
                    NodeKey,
                    VariableNode->VariableReference.GetMemberGuid());
            }
            else if (const UK2Node_BaseMCDelegate* DelegateNode =
                         Cast<UK2Node_BaseMCDelegate>(Node))
            {
                Snapshot.MemberReferences.Add(
                    NodeKey,
                    DelegateNode->DelegateReference.GetMemberGuid());
            }

            TMap<FString, int32> Occurrences;
            for (const UEdGraphPin* Pin : Node->Pins)
            {
                if (Pin == nullptr)
                {
                    continue;
                }
                const FString PinBase = FString::Printf(
                    TEXT("%s/%s:%d"),
                    *NodeKey,
                    *Pin->PinName.ToString(),
                    static_cast<int32>(Pin->Direction));
                const int32 Occurrence =
                    Occurrences.FindOrAdd(PinBase)++;
                Snapshot.Pins.Add(
                    FString::Printf(
                        TEXT("%s:%d"),
                        *PinBase,
                        Occurrence),
                    Pin->PinId);
            }
        }
    }
    return Snapshot;
}

bool TestStableGuidMapsEqual(
    const TMap<FString, FGuid>& Source,
    const TMap<FString, FGuid>& Plan)
{
    if (Source.Num() != Plan.Num())
    {
        return false;
    }
    for (const TPair<FString, FGuid>& Pair : Source)
    {
        const FGuid* PlanGuid = Plan.Find(Pair.Key);
        if (PlanGuid == nullptr || *PlanGuid != Pair.Value)
        {
            return false;
        }
    }
    return true;
}

TSharedRef<FJsonObject> PublicBlueprintRemoveGraphPatch(
    const UBlueprint* Blueprint,
    const UEdGraph* Graph,
    const bool bDryRun = true)
{
    TSharedRef<FJsonObject> BlueprintTarget = MakeShared<FJsonObject>();
    BlueprintTarget->SetStringField(TEXT("kind"), TEXT("target"));
    BlueprintTarget->SetStringField(TEXT("domain"), TEXT("blueprint"));
    BlueprintTarget->SetStringField(TEXT("asset"), Blueprint->GetPathName());
    BlueprintTarget->SetStringField(
        TEXT("id"),
        Blueprint->GetBlueprintGuid().ToString(
            EGuidFormats::DigitsWithHyphensLower));

    TSharedRef<FJsonObject> Target = MakeShared<FJsonObject>();
    Target->SetStringField(TEXT("alias"), TEXT("sourceBlueprint"));
    Target->SetObjectField(TEXT("target"), BlueprintTarget);

    TSharedRef<FJsonObject> GraphReference = MakeShared<FJsonObject>();
    GraphReference->SetStringField(TEXT("kind"), TEXT("stable_ref"));
    GraphReference->SetArrayField(
        TEXT("identityPath"),
        {MakeShared<FJsonValueString>(Graph->GraphGuid.ToString(
            EGuidFormats::DigitsWithHyphensLower))});

    TSharedRef<FJsonObject> Remove = MakeShared<FJsonObject>();
    Remove->SetStringField(TEXT("kind"), TEXT("remove"));
    Remove->SetObjectField(TEXT("target"), GraphReference);

    TSharedRef<FJsonObject> Patch = MakeShared<FJsonObject>();
    Patch->SetStringField(TEXT("kind"), TEXT("patch"));
    Patch->SetObjectField(TEXT("target"), Target);
    Patch->SetBoolField(TEXT("dryRun"), bDryRun);
    Patch->SetArrayField(
        TEXT("statements"),
        {MakeShared<FJsonValueObject>(Remove)});

    TSharedRef<FJsonObject> Arguments = MakeShared<FJsonObject>();
    Arguments->SetObjectField(TEXT("object"), Patch);
    return Arguments;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FSalBlueprintDescriptionMutationTest,
    "Loomle.Sal.Blueprint.Mutation.Description",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSalBlueprintDescriptionMutationTest::RunTest(const FString& Parameters)
{
    if (GEditor == nullptr)
    {
        AddError(TEXT("Blueprint mutation tests require GEditor."));
        return false;
    }

    for (const EBlueprintMutationFixtureKind Kind : {
        EBlueprintMutationFixtureKind::Blueprint,
        EBlueprintMutationFixtureKind::WidgetBlueprint})
    {
        FBlueprintMutationFixture Fixture(Kind);
        TestNotNull(
            *FString::Printf(TEXT("%s fixture is created"), *Fixture.Label),
            Fixture.Blueprint);
        if (Fixture.Blueprint == nullptr)
        {
            continue;
        }

        const FString OriginalDescription =
            TEXT("Description before SAL mutation");
        const FString NewDescription =
            TEXT("Description authored through SAL");
        Fixture.Blueprint->BlueprintDescription = OriginalDescription;
        Fixture.Package->SetDirtyFlag(false);
        UObject* SourceCDO =
            Fixture.Blueprint->GeneratedClass != nullptr
                ? Fixture.Blueprint->GeneratedClass->GetDefaultObject(false)
                : nullptr;

        const TSharedPtr<FJsonObject> DryRun =
            FSalBlueprintInterface::Patch(
                DescriptionPatch(NewDescription, true),
                BlueprintTarget(Fixture.Blueprint));
        TestTrue(
            *FString::Printf(TEXT("%s dry-run validates"), *Fixture.Label),
            ResultBool(DryRun, TEXT("valid")));
        TestTrue(
            *FString::Printf(TEXT("%s dry-run is reported"), *Fixture.Label),
            ResultBool(DryRun, TEXT("dryRun")));
        TestFalse(
            *FString::Printf(TEXT("%s dry-run does not apply"), *Fixture.Label),
            ResultBool(DryRun, TEXT("applied"), true));
        TestEqual(
            *FString::Printf(TEXT("%s dry-run preserves source description"), *Fixture.Label),
            Fixture.Blueprint->BlueprintDescription,
            OriginalDescription);
        TestFalse(
            *FString::Printf(TEXT("%s dry-run preserves source dirty state"), *Fixture.Label),
            Fixture.Package->IsDirty());
        TestTrue(
            *FString::Printf(TEXT("%s dry-run preserves source CDO identity"), *Fixture.Label),
            Fixture.Blueprint->GeneratedClass != nullptr
                && Fixture.Blueprint->GeneratedClass->GetDefaultObject(false)
                    == SourceCDO);

        const TSharedPtr<FJsonObject> Applied =
            FSalBlueprintInterface::Patch(
                DescriptionPatch(NewDescription, false),
                BlueprintTarget(Fixture.Blueprint));
        TestTrue(
            *FString::Printf(TEXT("%s apply validates"), *Fixture.Label),
            ResultBool(Applied, TEXT("valid")));
        TestTrue(
            *FString::Printf(TEXT("%s apply reports a mutation"), *Fixture.Label),
            ResultBool(Applied, TEXT("applied")));
        TestEqual(
            *FString::Printf(TEXT("%s apply writes BlueprintDescription"), *Fixture.Label),
            Fixture.Blueprint->BlueprintDescription,
            NewDescription);

        const bool bApplied =
            ResultBool(Applied, TEXT("applied"));
        const bool bUndid =
            bApplied
            && GEditor->UndoTransaction(false);
        TestTrue(
            *FString::Printf(TEXT("%s mutation is one undo step"), *Fixture.Label),
            bUndid);
        TestEqual(
            *FString::Printf(TEXT("%s undo restores BlueprintDescription"), *Fixture.Label),
            Fixture.Blueprint->BlueprintDescription,
            OriginalDescription);

        const TSharedPtr<FJsonObject> Cleared =
            FSalBlueprintInterface::Patch(
                DescriptionPatch(FString(), false),
                BlueprintTarget(Fixture.Blueprint));
        TestTrue(
            *FString::Printf(TEXT("%s empty string validates"), *Fixture.Label),
            ResultBool(Cleared, TEXT("valid")));
        TestTrue(
            *FString::Printf(TEXT("%s empty string is applied"), *Fixture.Label),
            ResultBool(Cleared, TEXT("applied")));
        TestTrue(
            *FString::Printf(TEXT("%s BlueprintDescription can be cleared"), *Fixture.Label),
            Fixture.Blueprint->BlueprintDescription.IsEmpty());
        const bool bCleared =
            ResultBool(Cleared, TEXT("applied"));
        TestTrue(
            *FString::Printf(TEXT("%s clear is one undo step"), *Fixture.Label),
            bCleared
                && GEditor->UndoTransaction(false));
        TestEqual(
            *FString::Printf(TEXT("%s undo restores description after clear"), *Fixture.Label),
            Fixture.Blueprint->BlueprintDescription,
            OriginalDescription);
        Fixture.Package->SetDirtyFlag(false);
        FString CleanupError;
        const bool bCleaned = Fixture.Cleanup(CleanupError);
        TestTrue(
            *FString::Printf(
                TEXT("%s fixture unloads without resetting editor transactions: %s"),
                *Fixture.Label,
                *CleanupError),
            bCleaned);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FSalBlueprintWidgetObjectDefaultDryRunTest,
    "Loomle.Sal.Blueprint.Mutation.WidgetObjectDefaultDryRun",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSalBlueprintWidgetObjectDefaultDryRunTest::RunTest(
    const FString& Parameters)
{
    (void)Parameters;
    if (GEditor == nullptr || GEditor->PlayWorld != nullptr
        || GEditor->IsTransactionActive())
    {
        AddError(TEXT("Widget object default regression requires an idle Editor."));
        return false;
    }

    // Issue #201 reports a healthy ObjectProperty with a None CDO default.
    // Create that state through UE authoring and compilation, never by
    // constructing a generated class or corrupting an existing CDO.
    FBlueprintMutationFixture ReferencedWidget(
        EBlueprintMutationFixtureKind::WidgetBlueprint);
    if (!TestNotNull(
            TEXT("Referenced Widget Blueprint exists"),
            ReferencedWidget.Blueprint))
    {
        return false;
    }
    FKismetEditorUtilities::CompileBlueprint(ReferencedWidget.Blueprint);
    if (!TestTrue(
            TEXT("Referenced Widget Blueprint compiles successfully"),
            ReferencedWidget.Blueprint->Status == BS_UpToDate
                && IsValid(ReferencedWidget.Blueprint->GeneratedClass.Get())))
    {
        return false;
    }
    ReferencedWidget.Package->SetDirtyFlag(false);

    for (const EBlueprintMutationFixtureKind Kind : {
        EBlueprintMutationFixtureKind::Blueprint,
        EBlueprintMutationFixtureKind::WidgetBlueprint})
    {
        FBlueprintMutationFixture Fixture(Kind);
        if (!TestNotNull(
                TEXT("Object default source Blueprint exists"),
                Fixture.Blueprint))
        {
            return false;
        }
        Loomle::Tests::FScopedIsolatedTransactor Transactions;
        if (!TestTrue(
                TEXT("Object default fixture owns isolated authoring transactions"),
                Transactions.Initialize()))
        {
            return false;
        }

        const FName ReferenceName(TEXT("Shooter_Hud_Reference"));
        FEdGraphPinType ReferenceType;
        ReferenceType.PinCategory = UEdGraphSchema_K2::PC_Object;
        ReferenceType.PinSubCategoryObject =
            ReferencedWidget.Blueprint->GeneratedClass.Get();
        if (!TestTrue(
                TEXT("Cross-Blueprint Widget object variable is authored"),
                FBlueprintEditorUtils::AddMemberVariable(
                    Fixture.Blueprint,
                    ReferenceName,
                    ReferenceType,
                    TEXT("None"))))
        {
            return false;
        }
        UEdGraph* EventGraph =
            FBlueprintEditorUtils::FindEventGraph(Fixture.Blueprint);
        if (!TestNotNull(TEXT("Object default Event Graph exists"), EventGraph)
            || !TestNotNull(
                TEXT("Object default getter is authored"),
                GetDefault<UEdGraphSchema_K2>()->SpawnVariableGetNode(
                    FVector2D(240.0, 160.0),
                    EventGraph,
                    ReferenceName,
                    nullptr)))
        {
            return false;
        }
        UEdGraph* RemovableGraph = FBlueprintEditorUtils::CreateNewGraph(
            Fixture.Blueprint,
            TEXT("SandboxRemovalTarget"),
            UEdGraph::StaticClass(),
            UEdGraphSchema_K2::StaticClass());
        if (!TestNotNull(TEXT("Removable Function Graph exists"), RemovableGraph))
        {
            return false;
        }
        FBlueprintEditorUtils::AddFunctionGraph(
            Fixture.Blueprint,
            RemovableGraph,
            true,
            static_cast<UClass*>(nullptr));
        const FGuid BlueprintGuid = Fixture.Blueprint->GetBlueprintGuid();
        const FGuid GraphGuid = RemovableGraph->GraphGuid;
        const int32 VariableIndex = FBlueprintEditorUtils::FindNewVariableIndex(
            Fixture.Blueprint,
            ReferenceName);
        if (!TestTrue(TEXT("Object variable has a stable GUID"), VariableIndex != INDEX_NONE))
        {
            return false;
        }
        const FGuid VariableGuid = Fixture.Blueprint->NewVariables[VariableIndex].VarGuid;

        // Recompile between calls so coverage includes warm compiler and GC
        // state as well as the first sandbox made from a freshly compiled asset.
        for (int32 Round = 0; Round < 3; ++Round)
        {
            FKismetEditorUtilities::CompileBlueprint(Fixture.Blueprint);
            CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);
            const FString Context = FString::Printf(
                TEXT("%s ObjectProperty dry run, round %d"),
                *Fixture.Label,
                Round + 1);
            if (!TestTrue(
                    *(Context + TEXT(" starts from a successfully compiled Blueprint")),
                    Fixture.Blueprint->Status == BS_UpToDate))
            {
                return false;
            }

            UClass* SourceClass = Fixture.Blueprint->GeneratedClass.Get();
            UObject* SourceCDO = IsValid(SourceClass)
                ? SourceClass->GetDefaultObject(false)
                : nullptr;
            UClass* SourceSkeleton = Fixture.Blueprint->SkeletonGeneratedClass.Get();
            UClass* ReferencedClass = ReferencedWidget.Blueprint->GeneratedClass.Get();
            UObject* ReferencedCDO = IsValid(ReferencedClass)
                ? ReferencedClass->GetDefaultObject(false)
                : nullptr;
            if (!TestTrue(
                    *(Context + TEXT(" starts with a valid class and CDO")),
                    IsValid(SourceClass) && IsValid(SourceCDO)
                        && SourceClass->IsValidLowLevelFast()
                        && SourceCDO->IsValidLowLevelFast()
                        && SourceCDO->GetClass() == SourceClass
                        && SourceCDO->HasAnyFlags(RF_ClassDefaultObject)))
            {
                return false;
            }
            FObjectProperty* ReferenceProperty = FindFProperty<FObjectProperty>(
                SourceClass,
                ReferenceName);
            if (!TestTrue(
                    *(Context + TEXT(" starts with a Widget ObjectProperty defaulting to None")),
                    ReferenceProperty != nullptr
                        && ReferenceProperty->HasAllPropertyFlags(
                            CPF_Edit | CPF_BlueprintVisible | CPF_DisableEditOnInstance)
                        && ReferenceProperty->PropertyClass
                            == ReferencedClass
                        && ReferenceProperty->GetObjectPropertyValue_InContainer(SourceCDO)
                            == nullptr))
            {
                return false;
            }
            const FBlueprintTestStableIds Before = CaptureTestStableIds(Fixture.Blueprint);
            const int32 FunctionCount = Fixture.Blueprint->FunctionGraphs.Num();
            Fixture.Package->SetDirtyFlag(false);

            const TSharedPtr<FJsonObject> Result = FSalModule::BuildPatchResult(
                PublicBlueprintRemoveGraphPatch(Fixture.Blueprint, RemovableGraph));
            TestFalse(*(Context + TEXT(" has no public-path error")), ResultBool(Result, TEXT("isError"), true));
            TestTrue(*(Context + TEXT(" validates graph removal")), ResultBool(Result, TEXT("valid")));
            TestTrue(*(Context + TEXT(" is reported as dry run")), ResultBool(Result, TEXT("dryRun")));
            TestFalse(*(Context + TEXT(" does not apply")), ResultBool(Result, TEXT("applied"), true));
            if (!TestTrue(
                    *(Context + TEXT(" preserves valid source class and CDO identity")),
                    IsValid(SourceClass) && IsValid(SourceCDO)
                        && SourceClass->IsValidLowLevelFast()
                        && SourceCDO->IsValidLowLevelFast()
                        && Fixture.Blueprint->GeneratedClass.Get() == SourceClass
                        && Fixture.Blueprint->SkeletonGeneratedClass.Get() == SourceSkeleton
                        && SourceClass->GetDefaultObject(false) == SourceCDO
                        && SourceCDO->GetClass() == SourceClass))
            {
                return false;
            }
            ReferenceProperty = FindFProperty<FObjectProperty>(SourceClass, ReferenceName);
            TestTrue(
                *(Context + TEXT(" preserves referenced Widget class and CDO identity")),
                IsValid(ReferencedClass) && IsValid(ReferencedCDO)
                    && ReferencedWidget.Blueprint->GeneratedClass.Get() == ReferencedClass
                    && ReferencedClass->GetDefaultObject(false) == ReferencedCDO);
            TestTrue(
                *(Context + TEXT(" preserves the Widget object default")),
                ReferenceProperty != nullptr
                    && ReferenceProperty->PropertyClass == ReferencedWidget.Blueprint->GeneratedClass.Get()
                    && ReferenceProperty->GetObjectPropertyValue_InContainer(SourceCDO) == nullptr);
            TestFalse(*(Context + TEXT(" preserves source dirty state")), Fixture.Package->IsDirty());
            TestFalse(*(Context + TEXT(" preserves referenced asset dirty state")), ReferencedWidget.Package->IsDirty());
            TestTrue(
                *(Context + TEXT(" leaves the requested graph in the source")),
                Fixture.Blueprint->FunctionGraphs.Contains(RemovableGraph)
                    && Fixture.Blueprint->FunctionGraphs.Num() == FunctionCount
                    && RemovableGraph->GraphGuid == GraphGuid);
            TestEqual(*(Context + TEXT(" preserves Blueprint GUID")), Fixture.Blueprint->GetBlueprintGuid(), BlueprintGuid);
            const int32 CurrentVariableIndex = FBlueprintEditorUtils::FindNewVariableIndex(
                Fixture.Blueprint,
                ReferenceName);
            TestTrue(
                *(Context + TEXT(" preserves variable GUID")),
                CurrentVariableIndex != INDEX_NONE
                    && Fixture.Blueprint->NewVariables[CurrentVariableIndex].VarGuid == VariableGuid);
            const FBlueprintTestStableIds After = CaptureTestStableIds(Fixture.Blueprint);
            TestTrue(
                *(Context + TEXT(" preserves graph, node, pin and member GUIDs")),
                TestStableGuidMapsEqual(Before.Graphs, After.Graphs)
                    && TestStableGuidMapsEqual(Before.Nodes, After.Nodes)
                    && TestStableGuidMapsEqual(Before.Pins, After.Pins)
                    && TestStableGuidMapsEqual(Before.MemberReferences, After.MemberReferences));
            if (ResultBool(Result, TEXT("isError"), true)
                || !ResultBool(Result, TEXT("valid")))
            {
                return false;
            }
        }
        Transactions.Restore();
        FString CleanupError;
        const bool bCleaned = Fixture.Cleanup(CleanupError);
        TestTrue(*FString::Printf(TEXT("Object default source fixture unloads: %s"), *CleanupError), bCleaned);
    }
    FString CleanupError;
    const bool bCleaned = ReferencedWidget.Cleanup(CleanupError);
    TestTrue(*FString::Printf(TEXT("Referenced Widget fixture unloads: %s"), *CleanupError), bCleaned);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FSalBlueprintCompileErrorGraphRepairTest,
    "Loomle.Sal.Blueprint.Mutation.CompileErrorGraphRepair",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSalBlueprintCompileErrorGraphRepairTest::RunTest(
    const FString& Parameters)
{
    (void)Parameters;
    if (GEditor == nullptr || GEditor->PlayWorld != nullptr
        || GEditor->IsTransactionActive())
    {
        AddError(TEXT("Compile-error repair regression requires an idle Editor."));
        return false;
    }

    FBlueprintMutationFixture Fixture(EBlueprintMutationFixtureKind::Blueprint);
    Loomle::Tests::FScopedIsolatedTransactor Transactions;
    if (!TestNotNull(TEXT("Compile-error Blueprint exists"), Fixture.Blueprint)
        || !TestTrue(TEXT("Compile-error fixture owns isolated Undo history"), Transactions.Initialize()))
    {
        return false;
    }
    UEdGraph* BrokenGraph = FBlueprintEditorUtils::CreateNewGraph(
        Fixture.Blueprint,
        TEXT("RepairableBrokenFunction"),
        UEdGraph::StaticClass(),
        UEdGraphSchema_K2::StaticClass());
    if (!TestNotNull(TEXT("Repairable Function Graph exists"), BrokenGraph))
    {
        return false;
    }
    FBlueprintEditorUtils::AddFunctionGraph(
        Fixture.Blueprint, BrokenGraph, true, static_cast<UClass*>(nullptr));
    TArray<UK2Node_FunctionEntry*> Entries;
    BrokenGraph->GetNodesOfClass(Entries);
    UFunction* NativeFunction = AActor::StaticClass()->FindFunctionByName(TEXT("K2_DestroyActor"));
    if (!TestEqual(TEXT("Repair fixture has one Function Entry"), Entries.Num(), 1)
        || !TestNotNull(TEXT("Native impure function exists"), NativeFunction))
    {
        return false;
    }
    UK2Node_CallFunction* BrokenCall = NewObject<UK2Node_CallFunction>(
        BrokenGraph, NAME_None, RF_Transactional);
    BrokenCall->CreateNewGuid();
    BrokenCall->SetFromFunction(NativeFunction);
    BrokenGraph->AddNode(BrokenCall, false, false);
    BrokenCall->AllocateDefaultPins();
    UEdGraphPin* EntryExec = Entries[0]->FindPin(UEdGraphSchema_K2::PN_Then, EGPD_Output);
    UEdGraphPin* CallExec = BrokenCall->FindPin(UEdGraphSchema_K2::PN_Execute, EGPD_Input);
    if (!TestTrue(TEXT("Native schema connects the impure function into execution"),
            EntryExec != nullptr && CallExec != nullptr
                && GetDefault<UEdGraphSchema_K2>()->TryCreateConnection(EntryExec, CallExec)))
    {
        return false;
    }
    // Keep the native call's execution pins, but refer to a genuinely missing
    // function. K2Node_CallFunction::ValidateNodeDuringCompilation reports this
    // as a compiler error; BS_Error itself is never assigned by the test.
    BrokenCall->FunctionReference.SetSelfMember(TEXT("LoomleMissingRepairTarget"));
    FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Fixture.Blueprint);
    AddExpectedError(TEXT("LoomleMissingRepairTarget"), EAutomationExpectedErrorFlags::Contains, 0);
    FCompilerResultsLog FailedCompile;
    FKismetEditorUtilities::CompileBlueprint(
        Fixture.Blueprint, EBlueprintCompileOptions::None, &FailedCompile);
    if (!TestTrue(TEXT("Native compiler produces a real repairable BS_Error Blueprint"),
            FailedCompile.NumErrors > 0 && Fixture.Blueprint->Status == BS_Error))
    {
        return false;
    }
    FString ClassStateMessage;
    if (!TestTrue(TEXT("Compiler errors retain class/CDO state suitable for repair"),
            FSalBlueprintInterface::ValidateModificationClassStateForTesting(
                Fixture.Blueprint, ClassStateMessage)))
    {
        return false;
    }
    Fixture.Package->SetDirtyFlag(false);
    const FGuid BlueprintGuid = Fixture.Blueprint->GetBlueprintGuid();
    const FGuid GraphGuid = BrokenGraph->GraphGuid;
    const FGuid CallGuid = BrokenCall->NodeGuid;
    const int32 FunctionCount = Fixture.Blueprint->FunctionGraphs.Num();
    const FBlueprintTestStableIds Before = CaptureTestStableIds(Fixture.Blueprint);
    UClass* SourceClass = Fixture.Blueprint->GeneratedClass.Get();
    UClass* SourceSkeleton = Fixture.Blueprint->SkeletonGeneratedClass.Get();
    UObject* SourceCDO = SourceClass->GetDefaultObject(false);
    UObject* SourceSkeletonCDO = SourceSkeleton->GetDefaultObject(false);

    const TSharedPtr<FJsonObject> DryRun = FSalModule::BuildPatchResult(
        PublicBlueprintRemoveGraphPatch(Fixture.Blueprint, BrokenGraph));
    if (!TestTrue(TEXT("Public dry run validates removal from a BS_Error Blueprint"),
            ResultBool(DryRun, TEXT("valid")) && !ResultBool(DryRun, TEXT("isError"), true)))
    {
        return false;
    }
    TestTrue(TEXT("Compile-error repair is reported as dry run"), ResultBool(DryRun, TEXT("dryRun")));
    TestFalse(TEXT("Compile-error dry run does not apply"), ResultBool(DryRun, TEXT("applied"), true));
    TestTrue(TEXT("Dry run preserves source compiler status, classes and CDOs"),
        Fixture.Blueprint->Status == BS_Error
            && Fixture.Blueprint->GeneratedClass.Get() == SourceClass
            && Fixture.Blueprint->SkeletonGeneratedClass.Get() == SourceSkeleton
            && SourceClass->GetDefaultObject(false) == SourceCDO
            && SourceSkeleton->GetDefaultObject(false) == SourceSkeletonCDO);
    TestEqual(TEXT("Dry run preserves source Function Graph membership"),
        Fixture.Blueprint->FunctionGraphs.Num(), FunctionCount);
    TestFalse(TEXT("Dry run preserves source dirty state"), Fixture.Package->IsDirty());
    const FBlueprintTestStableIds After = CaptureTestStableIds(Fixture.Blueprint);
    TestTrue(TEXT("Dry run preserves graph, node, pin and member GUIDs"),
        TestStableGuidMapsEqual(Before.Graphs, After.Graphs)
            && TestStableGuidMapsEqual(Before.Nodes, After.Nodes)
            && TestStableGuidMapsEqual(Before.Pins, After.Pins)
            && TestStableGuidMapsEqual(Before.MemberReferences, After.MemberReferences));

    const TSharedPtr<FJsonObject> Applied = FSalModule::BuildPatchResult(
        PublicBlueprintRemoveGraphPatch(Fixture.Blueprint, BrokenGraph, false));
    if (!TestTrue(TEXT("Public live Patch repairs a BS_Error Blueprint"),
            ResultBool(Applied, TEXT("valid")) && ResultBool(Applied, TEXT("applied"))
                && !ResultBool(Applied, TEXT("isError"), true)))
    {
        return false;
    }
    TestEqual(TEXT("Live repair removes exactly the broken Function Graph"),
        Fixture.Blueprint->FunctionGraphs.Num(), FunctionCount - 1);
    FKismetEditorUtilities::CompileBlueprint(Fixture.Blueprint);
    TestTrue(TEXT("Repaired Blueprint compiles successfully through UE"),
        Fixture.Blueprint->Status == BS_UpToDate
            || Fixture.Blueprint->Status == BS_UpToDateWithWarnings);
    TestEqual(TEXT("Repair preserves Blueprint identity"), Fixture.Blueprint->GetBlueprintGuid(), BlueprintGuid);
    if (!TestTrue(TEXT("Live repair is one Undo step"), GEditor->UndoTransaction(false)))
    {
        return false;
    }
    UEdGraph* RestoredGraph = nullptr;
    for (UEdGraph* Graph : Fixture.Blueprint->FunctionGraphs)
    {
        if (Graph != nullptr && Graph->GraphGuid == GraphGuid)
        {
            RestoredGraph = Graph;
            break;
        }
    }
    TestEqual(TEXT("Undo restores Function Graph membership"),
        Fixture.Blueprint->FunctionGraphs.Num(), FunctionCount);
    if (!TestNotNull(TEXT("Undo restores the same Function Graph identity"), RestoredGraph))
    {
        return false;
    }
    TestTrue(TEXT("Undo restores the broken call's node identity"),
        RestoredGraph->Nodes.ContainsByPredicate([CallGuid](const UEdGraphNode* Node)
        {
            return Node != nullptr && Node->NodeGuid == CallGuid;
        }));
    FCompilerResultsLog RestoredCompile;
    FKismetEditorUtilities::CompileBlueprint(
        Fixture.Blueprint, EBlueprintCompileOptions::None, &RestoredCompile);
    TestTrue(TEXT("Undo restores the native compiler error as well as the graph"),
        RestoredCompile.NumErrors > 0 && Fixture.Blueprint->Status == BS_Error);
    Transactions.Restore();
    FString CleanupError;
    TestTrue(*FString::Printf(TEXT("Compile-error fixture unloads: %s"), *CleanupError),
        Fixture.Cleanup(CleanupError));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FSalBlueprintInvalidDefaultObjectPreflightTest,
    "Loomle.Sal.Blueprint.Mutation.InvalidDefaultObjectPreflight",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSalBlueprintInvalidDefaultObjectPreflightTest::RunTest(
    const FString& Parameters)
{
    (void)Parameters;
    if (GEditor == nullptr || GEditor->PlayWorld != nullptr
        || GEditor->IsTransactionActive())
    {
        AddError(TEXT("CDO validation regression requires an idle Editor."));
        return false;
    }

    FBlueprintMutationFixture ForeignFixture(
        EBlueprintMutationFixtureKind::Blueprint);
    if (!TestNotNull(TEXT("Foreign CDO fixture exists"), ForeignFixture.Blueprint))
    {
        return false;
    }
    FKismetEditorUtilities::CompileBlueprint(ForeignFixture.Blueprint);
    UClass* ForeignClass = ForeignFixture.Blueprint->GeneratedClass.Get();
    UObject* ForeignCDO = IsValid(ForeignClass)
        ? ForeignClass->GetDefaultObject(false)
        : nullptr;
    if (!TestTrue(
            TEXT("Foreign CDO is a valid native-compiled class default object"),
            IsValid(ForeignCDO)
                && ForeignCDO->GetClass() == ForeignClass
                && ForeignCDO->HasAnyFlags(RF_ClassDefaultObject)))
    {
        return false;
    }
    ForeignFixture.Package->SetDirtyFlag(false);

    for (const EBlueprintMutationFixtureKind Kind : {
        EBlueprintMutationFixtureKind::Blueprint,
        EBlueprintMutationFixtureKind::WidgetBlueprint})
    {
        FBlueprintMutationFixture Fixture(Kind);
        if (!TestNotNull(TEXT("CDO validation source exists"), Fixture.Blueprint))
        {
            return false;
        }
        Loomle::Tests::FScopedIsolatedTransactor Transactions;
        if (!TestTrue(
                TEXT("CDO validation fixture owns isolated transactions"),
                Transactions.Initialize()))
        {
            return false;
        }
        FKismetEditorUtilities::CompileBlueprint(Fixture.Blueprint);
        UClass* GeneratedClass = Fixture.Blueprint->GeneratedClass.Get();
        UClass* SkeletonClass = Fixture.Blueprint->SkeletonGeneratedClass.Get();
        UObject* GeneratedCDO = IsValid(GeneratedClass)
            ? GeneratedClass->GetDefaultObject(false)
            : nullptr;
        UObject* SkeletonCDO = IsValid(SkeletonClass)
            ? SkeletonClass->GetDefaultObject(false)
            : nullptr;
        FString HealthyMessage;
        if (!TestTrue(
                TEXT("Native-compiled source class state is initially healthy"),
                IsValid(GeneratedCDO) && IsValid(SkeletonCDO)
                    && FSalBlueprintInterface::ValidateModificationClassStateForTesting(
                        Fixture.Blueprint,
                        HealthyMessage)))
        {
            return false;
        }
        Fixture.Package->SetDirtyFlag(false);
        const FString Description = Fixture.Blueprint->BlueprintDescription;
        const FGuid BlueprintGuid = Fixture.Blueprint->GetBlueprintGuid();
        const FBlueprintTestStableIds Before = CaptureTestStableIds(Fixture.Blueprint);

        struct FInvalidCDOCase
        {
            const TCHAR* Label;
            UClass* Class;
            UObject* Replacement;
        };
        for (const FInvalidCDOCase& Case : {
            FInvalidCDOCase{TEXT("Generated Class mismatched CDO"), GeneratedClass, ForeignCDO},
            FInvalidCDOCase{TEXT("Skeleton Class missing CDO"), SkeletonClass, nullptr},
            FInvalidCDOCase{TEXT("Skeleton Class mismatched CDO"), SkeletonClass, ForeignCDO}})
        {
            const FString Context = Fixture.Label + TEXT(" ") + Case.Label;
            {
                const FScopedTestDefaultObjectOverride Override(Case.Class, Case.Replacement);
                FString ValidationMessage;
                // Check the validator first. If it regresses, report that
                // failure and restore safely before an unsafe duplicate call.
                if (!TestFalse(
                        *(Context + TEXT(" is rejected before duplication")),
                        FSalBlueprintInterface::ValidateModificationClassStateForTesting(
                            Fixture.Blueprint,
                            ValidationMessage)))
                {
                    return false;
                }
                TestFalse(*(Context + TEXT(" explains the invalid state")), ValidationMessage.IsEmpty());
                const TSharedPtr<FJsonObject> Result = FSalBlueprintInterface::Patch(
                    DescriptionPatch(TEXT("Unsafe state must not reach sandbox apply"), true),
                    BlueprintTarget(Fixture.Blueprint));
                TestTrue(
                    *(Context + TEXT(" returns capability.preflight_unavailable")),
                    HasMutationDiagnosticCode(Result, TEXT("capability.preflight_unavailable")));
                TestTrue(*(Context + TEXT(" reports an error")), ResultBool(Result, TEXT("isError")));
                TestFalse(*(Context + TEXT(" is not valid")), ResultBool(Result, TEXT("valid"), true));
                TestFalse(*(Context + TEXT(" is not applied")), ResultBool(Result, TEXT("applied"), true));
                TestTrue(
                    *(Context + TEXT(" leaves both source class identities unchanged")),
                    Fixture.Blueprint->GeneratedClass.Get() == GeneratedClass
                        && Fixture.Blueprint->SkeletonGeneratedClass.Get() == SkeletonClass
                        && Case.Class->GetDefaultObject(false) == Case.Replacement);
                TestEqual(*(Context + TEXT(" preserves source description")), Fixture.Blueprint->BlueprintDescription, Description);
                TestEqual(*(Context + TEXT(" preserves Blueprint GUID")), Fixture.Blueprint->GetBlueprintGuid(), BlueprintGuid);
                TestFalse(*(Context + TEXT(" preserves source dirty state")), Fixture.Package->IsDirty());
                TestTrue(
                    *(Context + TEXT(" leaves the foreign CDO untouched")),
                    IsValid(ForeignCDO) && ForeignClass->GetDefaultObject(false) == ForeignCDO
                        && ForeignCDO->GetClass() == ForeignClass
                        && ForeignCDO->HasAnyFlags(RF_ClassDefaultObject)
                        && !ForeignFixture.Package->IsDirty());
                const FBlueprintTestStableIds After = CaptureTestStableIds(Fixture.Blueprint);
                TestTrue(
                    *(Context + TEXT(" preserves graph, node, pin and member GUIDs")),
                    TestStableGuidMapsEqual(Before.Graphs, After.Graphs)
                        && TestStableGuidMapsEqual(Before.Nodes, After.Nodes)
                        && TestStableGuidMapsEqual(Before.Pins, After.Pins)
                        && TestStableGuidMapsEqual(Before.MemberReferences, After.MemberReferences));
            }
            TestTrue(
                *(Context + TEXT(" restores original CDOs before cleanup")),
                GeneratedClass->GetDefaultObject(false) == GeneratedCDO
                    && SkeletonClass->GetDefaultObject(false) == SkeletonCDO);
        }
        Transactions.Restore();
        FString CleanupError;
        const bool bCleaned = Fixture.Cleanup(CleanupError);
        TestTrue(*FString::Printf(TEXT("CDO validation source fixture unloads: %s"), *CleanupError), bCleaned);
    }
    FString CleanupError;
    const bool bCleaned = ForeignFixture.Cleanup(CleanupError);
    TestTrue(*FString::Printf(TEXT("Foreign CDO fixture unloads: %s"), *CleanupError), bCleaned);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FSalBlueprintAtomicApplyRollbackTest,
    "Loomle.Sal.Blueprint.Mutation.AtomicApplyRollback",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSalBlueprintAtomicApplyRollbackTest::RunTest(
    const FString& Parameters)
{
    (void)Parameters;
    if (GEditor == nullptr)
    {
        AddError(TEXT("Blueprint rollback test requires GEditor."));
        return false;
    }

    FBlueprintMutationFixture Fixture(
        EBlueprintMutationFixtureKind::Blueprint);
    if (!TestNotNull(
            TEXT("Rollback fixture Blueprint is created"),
            Fixture.Blueprint))
    {
        return false;
    }

    Loomle::Tests::FScopedIsolatedTransactor Transactions;
    if (!TestTrue(
            TEXT("Rollback test owns an isolated UE transaction buffer"),
            Transactions.Initialize()))
    {
        return false;
    }

    const FString OriginalDescription =
        TEXT("Description before failed atomic Patch");
    const FString IntermediateDescription =
        TEXT("Live mutation that must be rolled back");
    const FString OriginalCategory = TEXT("Before redo");
    const FString RedoCategory = TEXT("After redo");
    Fixture.Blueprint->BlueprintDescription = OriginalDescription;
    Fixture.Blueprint->BlueprintCategory = OriginalCategory;
    Fixture.Package->SetDirtyFlag(false);

    {
        FScopedTransaction SeedTransaction(
            FText::FromString(TEXT("Loomle rollback test redo seed")));
        if (!TestTrue(
                TEXT("Redo seed transaction opened"),
                SeedTransaction.IsOutstanding()))
        {
            return false;
        }
        Fixture.Blueprint->Modify();
        Fixture.Blueprint->BlueprintCategory = RedoCategory;
    }
    TestTrue(
        TEXT("Redo seed can be undone while retaining redo"),
        GEditor->UndoTransaction(true));
    TestEqual(
        TEXT("Seed undo restores BlueprintCategory"),
        Fixture.Blueprint->BlueprintCategory,
        OriginalCategory);
    TestTrue(
        TEXT("Seed undo exposes a redo entry"),
        GEditor->Trans->CanRedo());
    const FTransactionContext ExpectedRedo =
        GEditor->Trans->GetRedoContext();
    TestTrue(
        TEXT("Seed redo has a stable identity"),
        ExpectedRedo.TransactionId.IsValid());
    Fixture.Package->SetDirtyFlag(false);

    const TSharedRef<FJsonValueFailAfterPreflight> FailingValue =
        MakeShared<FJsonValueFailAfterPreflight>(
            TEXT("Value accepted only by transient preflight"));
    FSalPatch Patch;
    Patch.Alias = TEXT("blueprint");
    Patch.bDryRun = false;
    Patch.Statements = {
        MakeShared<FJsonValueObject>(
            DescriptionSetStatement(
                MakeShared<FJsonValueString>(
                    IntermediateDescription))),
        MakeShared<FJsonValueObject>(
            DescriptionSetStatement(FailingValue))};

    const TSharedPtr<FJsonObject> Result =
        FSalBlueprintInterface::Patch(
            Patch,
            BlueprintTarget(Fixture.Blueprint));

    TestFalse(
        TEXT("Live-only second-statement failure rejects the Patch"),
        ResultBool(Result, TEXT("valid"), true));
    TestFalse(
        TEXT("Rolled-back Patch is not reported as applied"),
        ResultBool(Result, TEXT("applied"), true));
    TestEqual(
        TEXT("Atomic rollback restores the earlier live statement"),
        Fixture.Blueprint->BlueprintDescription,
        OriginalDescription);
    TestFalse(
        TEXT("Atomic rollback restores the original package dirty state"),
        Fixture.Package->IsDirty());
    TestEqual(
        TEXT("Fault injector reaches live validation after sandbox apply"),
        FailingValue->GetReadCount(),
        3);
    TestFalse(
        TEXT("Failed Patch leaves no active UE transaction"),
        GEditor->IsTransactionActive());
    TestTrue(
        TEXT("Failed Patch preserves the user's pre-existing redo"),
        GEditor->Trans->CanRedo());
    const FTransactionContext ActualRedo =
        GEditor->Trans->GetRedoContext();
    TestEqual(
        TEXT("Failed Patch preserves the redo transaction identity"),
        ActualRedo.TransactionId,
        ExpectedRedo.TransactionId);
    TestTrue(
        TEXT("Preserved redo still executes"),
        GEditor->RedoTransaction());
    TestEqual(
        TEXT("Preserved redo still restores its authored value"),
        Fixture.Blueprint->BlueprintCategory,
        RedoCategory);
    TestEqual(
        TEXT("Preserved redo does not reintroduce the failed Patch"),
        Fixture.Blueprint->BlueprintDescription,
        OriginalDescription);

    Fixture.Package->SetDirtyFlag(false);
    Transactions.Restore();
    FString CleanupError;
    const bool bCleaned = Fixture.Cleanup(CleanupError);
    TestTrue(
        *FString::Printf(
            TEXT("Rollback fixture unloads cleanly: %s"),
            *CleanupError),
        bCleaned);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FSalBlueprintTransientPlanClassStateTest,
    "Loomle.Sal.Blueprint.Mutation.TransientPlanClassState",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSalBlueprintTransientPlanClassStateTest::RunTest(
    const FString& Parameters)
{
    for (const EBlueprintMutationFixtureKind Kind : {
        EBlueprintMutationFixtureKind::Blueprint,
        EBlueprintMutationFixtureKind::WidgetBlueprint})
    {
        FBlueprintMutationFixture Fixture(Kind);
        TestNotNull(
            *FString::Printf(TEXT("%s plan fixture is created"), *Fixture.Label),
            Fixture.Blueprint);
        if (Fixture.Blueprint == nullptr)
        {
            continue;
        }
        Loomle::Tests::FScopedIsolatedTransactor FixtureTransactions;
        if (!TestTrue(
                *FString::Printf(
                    TEXT("%s fixture authoring owns an isolated transaction buffer"),
                    *Fixture.Label),
                FixtureTransactions.Initialize()))
        {
            continue;
        }

        FEdGraphPinType IdentityVariableType;
        IdentityVariableType.PinCategory =
            UEdGraphSchema_K2::PC_Int;
        const FName IdentityVariableName(TEXT("PlanIdentityValue"));
        TestTrue(
            *FString::Printf(
                TEXT("%s identity variable is created"),
                *Fixture.Label),
            FBlueprintEditorUtils::AddMemberVariable(
                Fixture.Blueprint,
                IdentityVariableName,
                IdentityVariableType));
        const int32 SourceVariableIndex =
            FBlueprintEditorUtils::FindNewVariableIndex(
                Fixture.Blueprint,
                IdentityVariableName);
        TestTrue(
            *FString::Printf(
                TEXT("%s identity variable is discoverable"),
                *Fixture.Label),
            SourceVariableIndex != INDEX_NONE);
        const FGuid SourceVariableGuid =
            SourceVariableIndex != INDEX_NONE
                ? Fixture.Blueprint
                      ->NewVariables[SourceVariableIndex]
                      .VarGuid
                : FGuid();
        const FGuid SourceBlueprintGuid =
            Fixture.Blueprint->GetBlueprintGuid();
        UEdGraph* SourceEventGraph =
            FBlueprintEditorUtils::FindEventGraph(
                Fixture.Blueprint);
        TestNotNull(
            *FString::Printf(
                TEXT("%s Event Graph is available"),
                *Fixture.Label),
            SourceEventGraph);
        UK2Node_VariableGet* SourceVariableNode =
            SourceEventGraph != nullptr
                ? GetDefault<UEdGraphSchema_K2>()
                      ->SpawnVariableGetNode(
                          FVector2D(480.0, 240.0),
                          SourceEventGraph,
                          IdentityVariableName,
                          nullptr)
                : nullptr;
        TestNotNull(
            *FString::Printf(
                TEXT("%s self variable node is created"),
                *Fixture.Label),
            SourceVariableNode);
        TestTrue(
            *FString::Printf(
                TEXT("%s self variable node uses authored variable GUID"),
                *Fixture.Label),
            SourceVariableNode != nullptr
                && SourceVariableNode
                       ->VariableReference.GetMemberGuid()
                    == SourceVariableGuid);

        UTimelineTemplate* SourceTimeline = nullptr;
        if (Kind == EBlueprintMutationFixtureKind::Blueprint)
        {
            UTimelineTemplate* AddedTimeline =
                FBlueprintEditorUtils::AddNewTimeline(
                    Fixture.Blueprint,
                    TEXT("PlanIdentityTimeline"));
            TestNotNull(
                TEXT("Blueprint identity Timeline is created"),
                AddedTimeline);
            SourceTimeline =
                Fixture.Blueprint
                    ->FindTimelineTemplateByVariableName(
                        TEXT("PlanIdentityTimeline"));
        }
        const FGuid SourceTimelineGuid =
            SourceTimeline != nullptr
                ? SourceTimeline->TimelineGuid
                : FGuid();
        const FBlueprintTestStableIds SourceStableIds =
            CaptureTestStableIds(Fixture.Blueprint);
        UBlueprintGeneratedClass* SourceClassBeforePlan =
            Cast<UBlueprintGeneratedClass>(
                Fixture.Blueprint->GeneratedClass.Get());
        UObject* SourceCDOBeforePlan =
            SourceClassBeforePlan != nullptr
                ? SourceClassBeforePlan->GetDefaultObject(false)
                : nullptr;

        FString Message;
        TStrongObjectPtr<UBlueprint> PlanGuard =
            FSalBlueprintInterface::MakeTransientPlanForTesting(
                Fixture.Blueprint,
                Message);
        UBlueprint* Plan = PlanGuard.Get();
        TestNotNull(
            *FString::Printf(
                TEXT("%s transient plan is created: %s"),
                *Fixture.Label,
                *Message),
            Plan);
        if (Plan == nullptr)
        {
            continue;
        }

        UBlueprintGeneratedClass* SourceClass =
            Cast<UBlueprintGeneratedClass>(
                Fixture.Blueprint->GeneratedClass.Get());
        UBlueprintGeneratedClass* PlanClass =
            Cast<UBlueprintGeneratedClass>(Plan->GeneratedClass.Get());
        UObject* SourceCDO =
            SourceClass != nullptr
                ? SourceClass->GetDefaultObject(false)
                : nullptr;
        UObject* PlanCDO =
            PlanClass != nullptr
                ? PlanClass->GetDefaultObject(false)
                : nullptr;
        TestTrue(
            *FString::Printf(TEXT("%s plan preserves Blueprint subtype"), *Fixture.Label),
            Kind != EBlueprintMutationFixtureKind::WidgetBlueprint
                || Plan->IsA<UWidgetBlueprint>());
        TestTrue(
            *FString::Printf(TEXT("%s plan Generated Class is isolated"), *Fixture.Label),
            PlanClass != nullptr
                && PlanClass != SourceClass
                && PlanClass->IsIn(GetTransientPackage()));
        TestFalse(
            *FString::Printf(
                TEXT("%s plan does not retain Standalone lifetime"),
                *Fixture.Label),
            Plan->HasAnyFlags(RF_Standalone)
                || (PlanClass != nullptr
                    && PlanClass->HasAnyFlags(RF_Standalone)));
        TestTrue(
            *FString::Printf(TEXT("%s plan CDO invariant holds"), *Fixture.Label),
            PlanCDO != nullptr
                && PlanCDO != SourceCDO
                && PlanCDO->GetClass() == PlanClass
                && PlanCDO->HasAnyFlags(RF_ClassDefaultObject)
                && PlanCDO->IsIn(GetTransientPackage()));
        TestTrue(
            *FString::Printf(
                TEXT("%s plan preserves source Class/CDO identity"),
                *Fixture.Label),
            SourceClass == SourceClassBeforePlan
                && SourceCDO == SourceCDOBeforePlan);
        TestEqual(
            *FString::Printf(
                TEXT("%s plan preserves Blueprint GUID"),
                *Fixture.Label),
            Plan->GetBlueprintGuid(),
            SourceBlueprintGuid);
        const int32 PlanVariableIndex =
            FBlueprintEditorUtils::FindNewVariableIndex(
                Plan,
                IdentityVariableName);
        TestTrue(
            *FString::Printf(
                TEXT("%s plan preserves variable identity"),
                *Fixture.Label),
            PlanVariableIndex != INDEX_NONE
                && Plan->NewVariables[PlanVariableIndex].VarGuid
                    == SourceVariableGuid);
        UBlueprintGeneratedClass* PlanSkeletonClass =
            Cast<UBlueprintGeneratedClass>(
                Plan->SkeletonGeneratedClass.Get());
        TestTrue(
            *FString::Printf(
                TEXT("%s plan Generated/Skeleton Class GUID maps are coherent"),
                *Fixture.Label),
            PlanClass != nullptr
                && PlanSkeletonClass != nullptr
                && PlanClass->FindBlueprintPropertyGuidFromName(
                       IdentityVariableName)
                    == SourceVariableGuid
                && PlanSkeletonClass
                       ->FindBlueprintPropertyGuidFromName(
                           IdentityVariableName)
                    == SourceVariableGuid);
        const FBlueprintTestStableIds PlanStableIds =
            CaptureTestStableIds(Plan);
        TestTrue(
            *FString::Printf(
                TEXT("%s plan preserves every Graph GUID"),
                *Fixture.Label),
            TestStableGuidMapsEqual(
                SourceStableIds.Graphs,
                PlanStableIds.Graphs));
        TestTrue(
            *FString::Printf(
                TEXT("%s plan preserves every Node GUID"),
                *Fixture.Label),
            TestStableGuidMapsEqual(
                SourceStableIds.Nodes,
                PlanStableIds.Nodes));
        TestTrue(
            *FString::Printf(
                TEXT("%s plan preserves every Pin ID"),
                *Fixture.Label),
            TestStableGuidMapsEqual(
                SourceStableIds.Pins,
                PlanStableIds.Pins));
        TestTrue(
            *FString::Printf(
                TEXT("%s plan restores every variable/delegate member GUID"),
                *Fixture.Label),
            TestStableGuidMapsEqual(
                SourceStableIds.MemberReferences,
                PlanStableIds.MemberReferences));

        if (Kind == EBlueprintMutationFixtureKind::Blueprint)
        {
            USimpleConstructionScript* SourceSCS =
                Fixture.Blueprint->SimpleConstructionScript;
            USimpleConstructionScript* PlanSCS =
                Plan->SimpleConstructionScript;
            TestTrue(
                TEXT("Blueprint plan isolates SimpleConstructionScript"),
                SourceSCS != nullptr
                    && PlanSCS != nullptr
                    && PlanSCS != SourceSCS
                    && PlanSCS->IsIn(PlanClass));
            const TArray<USCS_Node*> SourceSCSNodes =
                SourceSCS != nullptr
                    ? SourceSCS->GetAllNodes()
                    : TArray<USCS_Node*>();
            const TArray<USCS_Node*> PlanSCSNodes =
                PlanSCS != nullptr
                    ? PlanSCS->GetAllNodes()
                    : TArray<USCS_Node*>();
            TestTrue(
                TEXT("Blueprint fixture contains SCS identity state"),
                !SourceSCSNodes.IsEmpty());
            TestEqual(
                TEXT("Blueprint plan preserves SCS Node count"),
                PlanSCSNodes.Num(),
                SourceSCSNodes.Num());
            for (const USCS_Node* SourceNode : SourceSCSNodes)
            {
                const USCS_Node* PlanNode = nullptr;
                for (const USCS_Node* Candidate : PlanSCSNodes)
                {
                    if (Candidate != nullptr
                        && SourceNode != nullptr
                        && Candidate->GetFName()
                            == SourceNode->GetFName()
                        && Candidate->GetVariableName()
                            == SourceNode->GetVariableName())
                    {
                        PlanNode = Candidate;
                        break;
                    }
                }
                TestTrue(
                    TEXT("Blueprint plan preserves and isolates each SCS Node"),
                    SourceNode != nullptr
                        && PlanNode != nullptr
                        && PlanNode != SourceNode
                        && PlanNode->VariableGuid
                            == SourceNode->VariableGuid
                        && PlanNode->IsIn(PlanClass));
                if (SourceNode != nullptr
                    && SourceNode->ComponentTemplate != nullptr)
                {
                    TestTrue(
                        TEXT("Blueprint plan isolates each SCS Component Template"),
                        PlanNode != nullptr
                            && PlanNode->ComponentTemplate != nullptr
                            && PlanNode->ComponentTemplate
                                != SourceNode->ComponentTemplate
                            && PlanNode->ComponentTemplate
                                   ->IsIn(PlanClass));
                }
            }

            UTimelineTemplate* PlanTimeline =
                Plan->FindTimelineTemplateByVariableName(
                    TEXT("PlanIdentityTimeline"));
            TestTrue(
                TEXT("Blueprint plan preserves and isolates Timeline identity"),
                SourceTimeline != nullptr
                    && PlanTimeline != nullptr
                    && PlanTimeline != SourceTimeline
                    && PlanTimeline->TimelineGuid
                        == SourceTimelineGuid
                    && PlanTimeline->IsIn(PlanClass));
        }

        FString ValidationMessage;
        TestTrue(
            *FString::Printf(
                TEXT("%s plan is safe for UE modification callbacks: %s"),
                *Fixture.Label,
                *ValidationMessage),
            FSalBlueprintInterface::
                ValidateModificationClassStateForTesting(
                    Plan,
                    ValidationMessage));
        PlanGuard.Reset();
        FixtureTransactions.Restore();
        FString CleanupError;
        const bool bCleaned = Fixture.Cleanup(CleanupError);
        TestTrue(
            *FString::Printf(
                TEXT("%s transient-plan fixture unloads without resetting editor state: %s"),
                *Fixture.Label,
                *CleanupError),
            bCleaned);
    }

    FBlueprintMutationFixture Fixture(
        EBlueprintMutationFixtureKind::Blueprint);
    TestNotNull(TEXT("Derived-Class fail-closed fixture is created"), Fixture.Blueprint);
    if (Fixture.Blueprint != nullptr
        && Fixture.Blueprint->GeneratedClass != nullptr)
    {
        UBlueprintGeneratedClass* DerivedWithoutCDO =
            NewObject<UBlueprintGeneratedClass>(
                GetTransientPackage(),
                MakeUniqueObjectName(
                    GetTransientPackage(),
                    UBlueprintGeneratedClass::StaticClass(),
                    TEXT("SALDerivedWithoutCDO")),
                RF_Transient);
        DerivedWithoutCDO->SetSuperStruct(
            Fixture.Blueprint->GeneratedClass.Get());
        TestNull(
            TEXT("Synthetic loaded derived BPGC starts without a CDO"),
            DerivedWithoutCDO->GetDefaultObject(false));

        FString ValidationMessage;
        TestFalse(
            TEXT("Loaded derived BPGC without a CDO fails closed"),
            FSalBlueprintInterface::
                ValidateModificationClassStateForTesting(
                    Fixture.Blueprint,
                    ValidationMessage));
        TestTrue(
            TEXT("Derived-Class failure identifies the missing CDO"),
            ValidationMessage.Contains(TEXT("derived"))
                && ValidationMessage.Contains(TEXT("default object")));

        const FString OriginalDescription =
            Fixture.Blueprint->BlueprintDescription;
        const TSharedPtr<FJsonObject> Rejected =
            FSalBlueprintInterface::Patch(
                DescriptionPatch(TEXT("Must not be written"), true),
                BlueprintTarget(Fixture.Blueprint));
        TestFalse(
            TEXT("Patch rejects unsafe loaded derived Class state"),
            ResultBool(Rejected, TEXT("valid"), true));
        TestEqual(
            TEXT("Fail-closed Patch preserves the source Blueprint"),
            Fixture.Blueprint->BlueprintDescription,
            OriginalDescription);

        DerivedWithoutCDO->SetSuperStruct(nullptr);
        DerivedWithoutCDO->MarkAsGarbage();
    }
    FString CleanupError;
    const bool bCleaned = Fixture.Cleanup(CleanupError);
    TestTrue(
        *FString::Printf(
            TEXT("Derived-Class fixture unloads without resetting editor transactions: %s"),
            *CleanupError),
        bCleaned);
    return true;
}

#endif
