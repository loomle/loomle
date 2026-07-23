// Copyright 2026 Loomle contributors.

#if WITH_DEV_AUTOMATION_TESTS

#include "Sal/Blueprint/SalBlueprintInterface.h"
#include "Tests/LoomleTestEditorState.h"

#include "Components/AudioComponent.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "EdGraph/EdGraph.h"
#include "EdGraphSchema_K2.h"
#include "Editor.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Engine/SCS_Node.h"
#include "Engine/SimpleConstructionScript.h"
#include "GameFramework/Actor.h"
#include "K2Node_FunctionEntry.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/AutomationTest.h"
#include "UObject/GarbageCollection.h"
#include "UObject/Package.h"
#include "UObject/UObjectHash.h"

namespace
{
using namespace Loomle::Sal;

FString RobustBlueprintGuidText(const FGuid& Guid)
{
    return Guid.ToString(EGuidFormats::DigitsWithHyphensLower);
}

bool RobustBlueprintRequireIdleEditor(
    FAutomationTestBase& Test,
    const FString& Surface)
{
    if (GEditor == nullptr)
    {
        Test.AddError(Surface + TEXT(" requires GEditor."));
        return false;
    }
    if (GEditor->IsPlaySessionInProgress()
        || GEditor->IsTransactionActive())
    {
        Test.AddError(
            Surface
            + TEXT(" requires an idle Editor outside PIE and outside another transaction."));
        return false;
    }
    return true;
}

bool RobustBlueprintResultBool(
    const TSharedPtr<FJsonObject>& Result,
    const TCHAR* Field,
    const bool Default = false)
{
    bool Value = Default;
    return Result.IsValid()
        && Result->TryGetBoolField(Field, Value)
        ? Value
        : Default;
}

bool RobustBlueprintHasDiagnosticCode(
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
        if (Value.IsValid()
            && Value->TryGetObject(Diagnostic)
            && Diagnostic != nullptr
            && (*Diagnostic)->TryGetStringField(TEXT("code"), Code)
            && Code == Expected)
        {
            return true;
        }
    }
    return false;
}

bool RobustBlueprintHasError(const TSharedPtr<FJsonObject>& Result)
{
    const TArray<TSharedPtr<FJsonValue>>* Diagnostics = nullptr;
    if (!Result.IsValid()
        || !Result->TryGetArrayField(TEXT("diagnostics"), Diagnostics)
        || Diagnostics == nullptr)
    {
        return true;
    }
    for (const TSharedPtr<FJsonValue>& Value : *Diagnostics)
    {
        const TSharedPtr<FJsonObject>* Diagnostic = nullptr;
        FString Severity;
        if (Value.IsValid()
            && Value->TryGetObject(Diagnostic)
            && Diagnostic != nullptr
            && (*Diagnostic)->TryGetStringField(
                TEXT("severity"),
                Severity)
            && Severity == TEXT("error"))
        {
            return true;
        }
    }
    return false;
}

TArray<TSharedPtr<FJsonObject>> RobustBlueprintCallArgs(
    const TSharedPtr<FJsonObject>& Result,
    const FString& Callee)
{
    TArray<TSharedPtr<FJsonObject>> Calls;
    const TSharedPtr<FJsonObject>* Object = nullptr;
    const TArray<TSharedPtr<FJsonValue>>* Statements = nullptr;
    if (!Result.IsValid()
        || !Result->TryGetObjectField(TEXT("object"), Object)
        || Object == nullptr
        || !(*Object)->TryGetArrayField(TEXT("statements"), Statements)
        || Statements == nullptr)
    {
        return Calls;
    }
    for (const TSharedPtr<FJsonValue>& StatementValue : *Statements)
    {
        const TSharedPtr<FJsonObject>* Statement = nullptr;
        const TSharedPtr<FJsonObject>* Call = nullptr;
        const TSharedPtr<FJsonObject>* Args = nullptr;
        FString ActualCallee;
        if (StatementValue.IsValid()
            && StatementValue->TryGetObject(Statement)
            && Statement != nullptr
            && (*Statement)->TryGetObjectField(TEXT("value"), Call)
            && Call != nullptr
            && (*Call)->TryGetStringField(TEXT("callee"), ActualCallee)
            && ActualCallee == Callee
            && (*Call)->TryGetObjectField(TEXT("args"), Args)
            && Args != nullptr)
        {
            Calls.Add(*Args);
        }
    }
    return Calls;
}

TSharedPtr<FJsonObject> RobustBlueprintCallWithId(
    const TSharedPtr<FJsonObject>& Result,
    const FString& Callee,
    const FString& Id)
{
    for (const TSharedPtr<FJsonObject>& Args :
         RobustBlueprintCallArgs(Result, Callee))
    {
        FString Actual;
        if (Args.IsValid()
            && Args->TryGetStringField(TEXT("id"), Actual)
            && Actual.Equals(Id, ESearchCase::IgnoreCase))
        {
            return Args;
        }
    }
    return nullptr;
}

bool RobustBlueprintHasComment(
    const TSharedPtr<FJsonObject>& Result,
    const FString& Needle)
{
    const TSharedPtr<FJsonObject>* Object = nullptr;
    const TArray<TSharedPtr<FJsonValue>>* Statements = nullptr;
    if (!Result.IsValid()
        || !Result->TryGetObjectField(TEXT("object"), Object)
        || Object == nullptr
        || !(*Object)->TryGetArrayField(TEXT("statements"), Statements)
        || Statements == nullptr)
    {
        return false;
    }
    for (const TSharedPtr<FJsonValue>& StatementValue : *Statements)
    {
        const TSharedPtr<FJsonObject>* Statement = nullptr;
        FString Kind;
        FString Text;
        if (StatementValue.IsValid()
            && StatementValue->TryGetObject(Statement)
            && Statement != nullptr
            && (*Statement)->TryGetStringField(TEXT("kind"), Kind)
            && Kind == TEXT("comment")
            && (*Statement)->TryGetStringField(TEXT("text"), Text)
            && Text.Contains(Needle))
        {
            return true;
        }
    }
    return false;
}

FString RobustBlueprintNextCursor(
    const TSharedPtr<FJsonObject>& Result)
{
    const TSharedPtr<FJsonObject>* Page = nullptr;
    FString Cursor;
    if (Result.IsValid()
        && Result->TryGetObjectField(TEXT("page"), Page)
        && Page != nullptr)
    {
        (*Page)->TryGetStringField(TEXT("next"), Cursor);
    }
    return Cursor;
}

TSharedRef<FJsonObject> RobustBlueprintLocal(const FString& Name)
{
    TSharedRef<FJsonObject> Ref = MakeShared<FJsonObject>();
    Ref->SetStringField(TEXT("kind"), TEXT("local"));
    Ref->SetStringField(TEXT("name"), Name);
    return Ref;
}

TSharedRef<FJsonObject> RobustBlueprintTyped(
    const FString& Kind,
    const FString& Id)
{
    TSharedRef<FJsonObject> Ref = MakeShared<FJsonObject>();
    Ref->SetStringField(TEXT("kind"), Kind);
    Ref->SetStringField(TEXT("id"), Id);
    return Ref;
}

TSharedRef<FJsonObject> RobustBlueprintMember(
    const TSharedRef<FJsonObject>& Owner,
    const FString& Field)
{
    TSharedRef<FJsonObject> Ref = MakeShared<FJsonObject>();
    Ref->SetStringField(TEXT("kind"), TEXT("member"));
    Ref->SetObjectField(TEXT("object"), Owner);
    Ref->SetArrayField(
        TEXT("path"),
        {MakeShared<FJsonValueString>(Field)});
    return Ref;
}

TSharedRef<FJsonObject> RobustBlueprintCall(
    const FString& Callee,
    const FString& Palette,
    const TOptional<FString>& Type = TOptional<FString>())
{
    TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
    Args->SetStringField(TEXT("palette"), Palette);
    if (Type.IsSet())
    {
        Args->SetStringField(TEXT("type"), Type.GetValue());
    }
    TSharedRef<FJsonObject> Call = MakeShared<FJsonObject>();
    Call->SetStringField(TEXT("kind"), TEXT("call"));
    Call->SetStringField(TEXT("callee"), Callee);
    Call->SetObjectField(TEXT("args"), Args);
    return Call;
}

TSharedRef<FJsonValue> RobustBlueprintBinding(
    const TSharedRef<FJsonObject>& Target,
    const TSharedRef<FJsonObject>& Value)
{
    TSharedRef<FJsonObject> Statement = MakeShared<FJsonObject>();
    Statement->SetObjectField(TEXT("target"), Target);
    Statement->SetObjectField(TEXT("value"), Value);
    return MakeShared<FJsonValueObject>(Statement);
}

TSharedRef<FJsonValue> RobustBlueprintUnary(
    const FString& Kind,
    const TSharedRef<FJsonObject>& Target)
{
    TSharedRef<FJsonObject> Statement = MakeShared<FJsonObject>();
    Statement->SetStringField(TEXT("kind"), Kind);
    Statement->SetObjectField(TEXT("target"), Target);
    return MakeShared<FJsonValueObject>(Statement);
}

TSharedRef<FJsonValue> RobustBlueprintSet(
    const TSharedRef<FJsonObject>& Target,
    const TSharedPtr<FJsonValue>& Value)
{
    TSharedRef<FJsonObject> Statement = MakeShared<FJsonObject>();
    Statement->SetStringField(TEXT("kind"), TEXT("set"));
    Statement->SetObjectField(TEXT("target"), Target);
    Statement->SetField(TEXT("value"), Value);
    return MakeShared<FJsonValueObject>(Statement);
}

TSharedRef<FJsonValue> RobustBlueprintMove(
    const TSharedRef<FJsonObject>& Target,
    const FString& Placement,
    const TSharedRef<FJsonObject>& Anchor)
{
    TSharedRef<FJsonObject> Statement = MakeShared<FJsonObject>();
    Statement->SetStringField(TEXT("kind"), TEXT("move"));
    Statement->SetObjectField(TEXT("target"), Target);
    Statement->SetObjectField(Placement, Anchor);
    return MakeShared<FJsonValueObject>(Statement);
}

FString RobustBlueprintPinTypeText(const FName Category)
{
    FEdGraphPinType Type;
    Type.PinCategory = Category;
    FString Text;
    FEdGraphPinType::StaticStruct()->ExportText(
        Text,
        &Type,
        nullptr,
        nullptr,
        PPF_None,
        nullptr);
    return Text;
}

FSalResolvedTarget RobustBlueprintTarget(UBlueprint* Blueprint)
{
    FSalResolvedTarget Target;
    Target.Kind = ESalTargetKind::Blueprint;
    Target.Alias = TEXT("blueprint");
    Target.AssetPath =
        Blueprint != nullptr ? Blueprint->GetPathName() : FString();
    Target.Id =
        Blueprint != nullptr
            ? RobustBlueprintGuidText(Blueprint->GetBlueprintGuid())
            : FString();
    Target.Object = Blueprint;
    Target.Package =
        Blueprint != nullptr ? Blueprint->GetOutermost() : nullptr;
    Target.Blueprint = Blueprint;
    Target.Class =
        Blueprint != nullptr ? Blueprint->GeneratedClass.Get() : nullptr;
    Target.Interfaces = {
        FName(TEXT("asset")),
        FName(TEXT("blueprint"))};
    return Target;
}

FSalQuery RobustBlueprintQuery(
    const FString& Kind,
    const FString& Alias = TEXT("blueprint"))
{
    FSalQuery Query;
    Query.Alias = Alias;
    Query.Operation = MakeShared<FJsonObject>();
    Query.Operation->SetStringField(TEXT("kind"), Kind);
    return Query;
}

TSharedPtr<FJsonObject> RobustBlueprintEq(
    const FString& Field,
    const FString& Value)
{
    TSharedPtr<FJsonObject> FieldPath = MakeShared<FJsonObject>();
    FieldPath->SetArrayField(
        TEXT("path"),
        {MakeShared<FJsonValueString>(Field)});
    TSharedPtr<FJsonObject> Condition = MakeShared<FJsonObject>();
    Condition->SetStringField(TEXT("kind"), TEXT("eq"));
    Condition->SetObjectField(TEXT("field"), FieldPath);
    Condition->SetStringField(TEXT("value"), Value);
    return Condition;
}

void RobustBlueprintPreparePackageForCollection(UPackage* Package)
{
    if (Package == nullptr)
    {
        return;
    }
    Package->SetDirtyFlag(false);
    Package->ClearFlags(RF_Public | RF_Standalone);
    ForEachObjectWithPackage(
        Package,
        [](UObject* Object)
        {
            Object->ClearFlags(RF_Public | RF_Standalone);
            return true;
        },
        true);
}

class FRobustBlueprintFixture
{
public:
    FRobustBlueprintFixture()
    {
        const FString Token =
            FGuid::NewGuid().ToString(EGuidFormats::Digits);
        Package = CreatePackage(*FString::Printf(
            TEXT("/Game/LoomleTests/RobustBlueprint_%s"),
            *Token));
        Blueprint = Package != nullptr
            ? FKismetEditorUtilities::CreateBlueprint(
                AActor::StaticClass(),
                Package,
                *FString::Printf(
                    TEXT("BP_RobustBlueprint_%s"),
                    *Token),
                BPTYPE_Normal,
                UBlueprint::StaticClass(),
                UBlueprintGeneratedClass::StaticClass(),
                NAME_None)
            : nullptr;
        if (Blueprint == nullptr)
        {
            return;
        }

        FEdGraphPinType BoolType;
        BoolType.PinCategory = UEdGraphSchema_K2::PC_Boolean;
        bSeedAlphaAdded = FBlueprintEditorUtils::AddMemberVariable(
            Blueprint,
            SeedAlphaName,
            BoolType);
        bSeedBetaAdded = FBlueprintEditorUtils::AddMemberVariable(
            Blueprint,
            SeedBetaName,
            BoolType);

        FEdGraphPinType DelegateType;
        DelegateType.PinCategory = UEdGraphSchema_K2::PC_MCDelegate;
        bDispatcherAdded = FBlueprintEditorUtils::AddMemberVariable(
            Blueprint,
            SeedDispatcherName,
            DelegateType);
        const UEdGraphSchema_K2* K2Schema =
            GetDefault<UEdGraphSchema_K2>();
        DispatcherGraph = FBlueprintEditorUtils::CreateNewGraph(
            Blueprint,
            SeedDispatcherName,
            UEdGraph::StaticClass(),
            UEdGraphSchema_K2::StaticClass());
        if (DispatcherGraph != nullptr && K2Schema != nullptr)
        {
            DispatcherGraph->bEditable = false;
            K2Schema->CreateDefaultNodesForGraph(*DispatcherGraph);
            K2Schema->CreateFunctionGraphTerminators(
                *DispatcherGraph,
                static_cast<UClass*>(nullptr));
            K2Schema->AddExtraFunctionFlags(
                DispatcherGraph,
                FUNC_BlueprintCallable | FUNC_BlueprintEvent | FUNC_Public);
            K2Schema->MarkFunctionEntryAsEditable(
                DispatcherGraph,
                true);
            Blueprint->DelegateSignatureGraphs.Add(DispatcherGraph);
        }

        FunctionGraph = FBlueprintEditorUtils::CreateNewGraph(
            Blueprint,
            SeedFunctionName,
            UEdGraph::StaticClass(),
            UEdGraphSchema_K2::StaticClass());
        if (FunctionGraph != nullptr)
        {
            FBlueprintEditorUtils::AddFunctionGraph(
                Blueprint,
                FunctionGraph,
                true,
                static_cast<UClass*>(nullptr));
        }

        if (Blueprint->SimpleConstructionScript != nullptr)
        {
            RootComponent =
                Blueprint->SimpleConstructionScript->CreateNode(
                    USceneComponent::StaticClass(),
                    TEXT("RobustRoot"));
            ChildComponent =
                Blueprint->SimpleConstructionScript->CreateNode(
                    UStaticMeshComponent::StaticClass(),
                    TEXT("RobustMesh"));
            if (RootComponent != nullptr)
            {
                Blueprint->SimpleConstructionScript->AddNode(
                    RootComponent);
            }
            if (RootComponent != nullptr && ChildComponent != nullptr)
            {
                RootComponent->AddChildNode(ChildComponent);
            }
        }
        FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(
            Blueprint);
        Package->SetDirtyFlag(false);
    }

    ~FRobustBlueprintFixture()
    {
        FString Ignored;
        Cleanup(Ignored);
    }

    FRobustBlueprintFixture(const FRobustBlueprintFixture&) = delete;
    FRobustBlueprintFixture& operator=(
        const FRobustBlueprintFixture&) = delete;

    bool IsValid() const
    {
        return Package != nullptr
            && Blueprint != nullptr
            && bSeedAlphaAdded
            && bSeedBetaAdded
            && bDispatcherAdded
            && DispatcherGraph != nullptr
            && FunctionGraph != nullptr
            && RootComponent != nullptr
            && ChildComponent != nullptr
            && RootComponent->VariableGuid.IsValid()
            && ChildComponent->VariableGuid.IsValid();
    }

    bool Cleanup(FString& OutError)
    {
        if (bCleaned)
        {
            OutError.Reset();
            return true;
        }
        bCleaned = true;
        UPackage* PackageToUnload = Package;
        const FString PackageName =
            PackageToUnload != nullptr
                ? PackageToUnload->GetName()
                : FString();
        RobustBlueprintPreparePackageForCollection(PackageToUnload);
        RootComponent = nullptr;
        ChildComponent = nullptr;
        FunctionGraph = nullptr;
        DispatcherGraph = nullptr;
        Blueprint = nullptr;
        Package = nullptr;
        CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);
        if (!PackageName.IsEmpty()
            && FindPackage(nullptr, *PackageName) != nullptr)
        {
            OutError = TEXT("Robust Blueprint fixture package remained loaded.");
            return false;
        }
        OutError.Reset();
        return true;
    }

    FBPVariableDescription* FindVariable(const FName Name)
    {
        if (Blueprint == nullptr)
        {
            return nullptr;
        }
        const int32 Index =
            FBlueprintEditorUtils::FindNewVariableIndex(Blueprint, Name);
        return Blueprint->NewVariables.IsValidIndex(Index)
            ? &Blueprint->NewVariables[Index]
            : nullptr;
    }

    UPackage* Package = nullptr;
    UBlueprint* Blueprint = nullptr;
    UEdGraph* DispatcherGraph = nullptr;
    UEdGraph* FunctionGraph = nullptr;
    USCS_Node* RootComponent = nullptr;
    USCS_Node* ChildComponent = nullptr;
    const FName SeedAlphaName = TEXT("SeedAlpha");
    const FName SeedBetaName = TEXT("SeedBeta");
    const FName SeedDispatcherName = TEXT("SeedSignal");
    const FName SeedFunctionName = TEXT("SeedFunction");

private:
    bool bSeedAlphaAdded = false;
    bool bSeedBetaAdded = false;
    bool bDispatcherAdded = false;
    bool bCleaned = false;
};

FSalPatch RobustBlueprintCreationPatch(const bool bDryRun)
{
    const FString BoolType =
        RobustBlueprintPinTypeText(UEdGraphSchema_K2::PC_Boolean);
    FSalPatch Patch;
    Patch.Alias = TEXT("blueprint");
    Patch.bDryRun = bDryRun;
    Patch.Statements = {
        RobustBlueprintBinding(
            RobustBlueprintMember(
                RobustBlueprintLocal(TEXT("blueprint")),
                TEXT("RuntimeValue")),
            RobustBlueprintCall(
                TEXT("variable"),
                TEXT("blueprint.variable"),
                BoolType)),
        RobustBlueprintUnary(
            TEXT("add"),
            RobustBlueprintMember(
                RobustBlueprintLocal(TEXT("blueprint")),
                TEXT("RuntimeValue"))),
        RobustBlueprintBinding(
            RobustBlueprintMember(
                RobustBlueprintLocal(TEXT("blueprint")),
                TEXT("RuntimeSignal")),
            RobustBlueprintCall(
                TEXT("dispatcher"),
                TEXT("blueprint.dispatcher"))),
        RobustBlueprintUnary(
            TEXT("add"),
            RobustBlueprintMember(
                RobustBlueprintLocal(TEXT("blueprint")),
                TEXT("RuntimeSignal")))
    };
    return Patch;
}

FSalPatch RobustBlueprintGraphComponentCreationPatch(
    const bool bDryRun)
{
    const FString ComponentPalette =
        TEXT("blueprint.component:")
        + UAudioComponent::StaticClass()->GetPathName();
    FSalPatch Patch;
    Patch.Alias = TEXT("blueprint");
    Patch.bDryRun = bDryRun;
    Patch.Statements = {
        RobustBlueprintBinding(
            RobustBlueprintLocal(TEXT("RuntimeFunction")),
            RobustBlueprintCall(
                TEXT("graph"),
                TEXT("blueprint.graph.function"))),
        RobustBlueprintUnary(
            TEXT("add"),
            RobustBlueprintLocal(TEXT("RuntimeFunction"))),
        RobustBlueprintBinding(
            RobustBlueprintLocal(TEXT("RuntimeAudio")),
            RobustBlueprintCall(
                TEXT("component"),
                ComponentPalette)),
        RobustBlueprintUnary(
            TEXT("add"),
            RobustBlueprintLocal(TEXT("RuntimeAudio"))),
        RobustBlueprintSet(
            RobustBlueprintMember(
                RobustBlueprintLocal(TEXT("RuntimeAudio")),
                TEXT("bAutoActivate")),
            MakeShared<FJsonValueBoolean>(false))
    };
    return Patch;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FSalRobustBlueprintDiscoveryCoverageTest,
    "Loomle.Sal.Robustness.Blueprint.DiscoveryCollectionsPalette",
    EAutomationTestFlags::EditorContext
        | EAutomationTestFlags::EngineFilter)

bool FSalRobustBlueprintDiscoveryCoverageTest::RunTest(
    const FString& Parameters)
{
    if (!RobustBlueprintRequireIdleEditor(
            *this,
            TEXT("Blueprint discovery coverage")))
    {
        return false;
    }
    FRobustBlueprintFixture Fixture;
    if (!TestTrue(TEXT("Blueprint discovery fixture is valid"), Fixture.IsValid()))
    {
        return false;
    }
    const FSalResolvedTarget Target =
        RobustBlueprintTarget(Fixture.Blueprint);

    FSalQuery Dispatchers = RobustBlueprintQuery(TEXT("dispatchers"));
    Dispatchers.Operation->SetStringField(TEXT("text"), TEXT("Seed"));
    const TSharedPtr<FJsonObject> DispatcherResult =
        FSalBlueprintInterface::Query(Dispatchers, Target);
    TestFalse(
        TEXT("Dispatcher search succeeds"),
        RobustBlueprintHasError(DispatcherResult));
    TestEqual(
        TEXT("Dispatcher search returns one exact authored Dispatcher"),
        RobustBlueprintCallArgs(
            DispatcherResult,
            TEXT("dispatcher")).Num(),
        1);

    FSalQuery ExactDispatcher =
        RobustBlueprintQuery(TEXT("dispatcher"));
    ExactDispatcher.Operation->SetStringField(
        TEXT("name"),
        Fixture.SeedDispatcherName.ToString());
    ExactDispatcher.With.Add(TEXT("schema"));
    const TSharedPtr<FJsonObject> ExactDispatcherResult =
        FSalBlueprintInterface::Query(ExactDispatcher, Target);
    TestFalse(
        TEXT("Exact Dispatcher with schema succeeds"),
        RobustBlueprintHasError(ExactDispatcherResult));
    TestNotNull(
        TEXT("Exact Dispatcher returns its paired Signature Graph"),
        RobustBlueprintCallWithId(
            ExactDispatcherResult,
            TEXT("graph"),
            RobustBlueprintGuidText(
                Fixture.DispatcherGraph->GraphGuid)).Get());
    TestTrue(
        TEXT("Exact Dispatcher schema describes writable fields"),
        RobustBlueprintHasComment(
            ExactDispatcherResult,
            TEXT("schema")));

    FSalQuery Components = RobustBlueprintQuery(TEXT("components"));
    Components.PageLimit = 1;
    TSharedPtr<FJsonObject> FirstComponentPage =
        FSalBlueprintInterface::Query(Components, Target);
    TestFalse(
        TEXT("First Component page succeeds"),
        RobustBlueprintHasError(FirstComponentPage));
    TestEqual(
        TEXT("Component page limit is enforced"),
        RobustBlueprintCallArgs(
            FirstComponentPage,
            TEXT("component")).Num(),
        1);
    const FString ComponentCursor =
        RobustBlueprintNextCursor(FirstComponentPage);
    TestFalse(
        TEXT("Truncated Component collection returns a cursor"),
        ComponentCursor.IsEmpty());
    Components.PageAfter = ComponentCursor;
    const TSharedPtr<FJsonObject> SecondComponentPage =
        FSalBlueprintInterface::Query(Components, Target);
    TestFalse(
        TEXT("Second Component page succeeds"),
        RobustBlueprintHasError(SecondComponentPage));
    TestEqual(
        TEXT("Second Component page returns the remaining Component"),
        RobustBlueprintCallArgs(
            SecondComponentPage,
            TEXT("component")).Num(),
        1);
    TestTrue(
        TEXT("Final Component page has no continuation"),
        RobustBlueprintNextCursor(SecondComponentPage).IsEmpty());

    FSalQuery FilteredVariables =
        RobustBlueprintQuery(TEXT("variables"));
    FilteredVariables.Where = RobustBlueprintEq(
        TEXT("name"),
        Fixture.SeedBetaName.ToString());
    TSharedPtr<FJsonObject> Order = MakeShared<FJsonObject>();
    Order->SetStringField(TEXT("key"), TEXT("name"));
    Order->SetStringField(TEXT("direction"), TEXT("desc"));
    FilteredVariables.OrderBy.Add(Order);
    const TSharedPtr<FJsonObject> FilteredVariableResult =
        FSalBlueprintInterface::Query(
            FilteredVariables,
            Target);
    TestFalse(
        TEXT("Variable where/order query succeeds"),
        RobustBlueprintHasError(FilteredVariableResult));
    const TArray<TSharedPtr<FJsonObject>> FilteredVariableCalls =
        RobustBlueprintCallArgs(
            FilteredVariableResult,
            TEXT("variable"));
    FString FilteredId;
    TestTrue(
        TEXT("Variable where remains exact"),
        FilteredVariableCalls.Num() == 1
            && FilteredVariableCalls[0]->TryGetStringField(
                TEXT("id"),
                FilteredId)
            && Fixture.FindVariable(Fixture.SeedBetaName) != nullptr
            && FilteredId.Equals(
                RobustBlueprintGuidText(
                    Fixture.FindVariable(
                        Fixture.SeedBetaName)->VarGuid),
                ESearchCase::IgnoreCase));

    FSalQuery ExactComponent =
        RobustBlueprintQuery(TEXT("component"));
    ExactComponent.Operation->SetStringField(
        TEXT("id"),
        RobustBlueprintGuidText(
            Fixture.ChildComponent->VariableGuid));
    ExactComponent.With.Add(TEXT("schema"));
    const TSharedPtr<FJsonObject> ExactComponentResult =
        FSalBlueprintInterface::Query(ExactComponent, Target);
    TestFalse(
        TEXT("Exact nested Component with schema succeeds"),
        RobustBlueprintHasError(ExactComponentResult));
    TestNotNull(
        TEXT("Exact nested Component includes its stable object"),
        RobustBlueprintCallWithId(
            ExactComponentResult,
            TEXT("component"),
            RobustBlueprintGuidText(
                Fixture.ChildComponent->VariableGuid)).Get());
    TestTrue(
        TEXT("Exact Component schema publishes native field policy"),
        RobustBlueprintHasComment(
            ExactComponentResult,
            TEXT("fields:")));

    FSalQuery Palette =
        RobustBlueprintQuery(TEXT("palette_entries"));
    Palette.Operation->SetStringField(TEXT("text"), TEXT("Variable"));
    const TSharedPtr<FJsonObject> PaletteResult =
        FSalBlueprintInterface::Query(Palette, Target);
    TestFalse(
        TEXT("Blueprint Palette search succeeds"),
        RobustBlueprintHasError(PaletteResult));
    const TArray<TSharedPtr<FJsonObject>> VariableConstructors =
        RobustBlueprintCallArgs(PaletteResult, TEXT("variable"));
    FString PaletteId;
    TestTrue(
        TEXT("Variable Palette result contains a copyable exact id"),
        VariableConstructors.Num() == 1
            && VariableConstructors[0]->TryGetStringField(
                TEXT("palette"),
                PaletteId)
            && PaletteId == TEXT("blueprint.variable"));

    FSalQuery ExactPalette =
        RobustBlueprintQuery(TEXT("palette"));
    ExactPalette.Operation->SetStringField(
        TEXT("id"),
        PaletteId);
    ExactPalette.With.Add(TEXT("schema"));
    const TSharedPtr<FJsonObject> ExactPaletteResult =
        FSalBlueprintInterface::Query(ExactPalette, Target);
    TestFalse(
        TEXT("Exact Blueprint Palette schema succeeds"),
        RobustBlueprintHasError(ExactPaletteResult));
    TestTrue(
        TEXT("Exact Blueprint Palette schema describes constructor type"),
        RobustBlueprintHasComment(
            ExactPaletteResult,
            TEXT("FEdGraphPinType")));

    Components.PageAfter = ComponentCursor + TEXT("stale");
    TestTrue(
        TEXT("Tampered Blueprint cursor fails closed"),
        RobustBlueprintHasDiagnosticCode(
            FSalBlueprintInterface::Query(Components, Target),
            TEXT("validation.invalid_cursor")));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FSalRobustBlueprintDeclarationLifecycleTest,
    "Loomle.Sal.Robustness.Blueprint.DeclarationLifecycle",
    EAutomationTestFlags::EditorContext
        | EAutomationTestFlags::EngineFilter)

bool FSalRobustBlueprintDeclarationLifecycleTest::RunTest(
    const FString& Parameters)
{
    if (!RobustBlueprintRequireIdleEditor(
            *this,
            TEXT("Blueprint declaration lifecycle")))
    {
        return false;
    }
    Loomle::Tests::FScopedIsolatedTransactor Transactions;
    if (!TestTrue(
            TEXT("Declaration lifecycle isolates Undo history"),
            Transactions.Initialize()))
    {
        return false;
    }
    FRobustBlueprintFixture Fixture;
    if (!TestTrue(TEXT("Declaration lifecycle fixture is valid"), Fixture.IsValid()))
    {
        Transactions.Restore();
        return false;
    }
    const FSalResolvedTarget Target =
        RobustBlueprintTarget(Fixture.Blueprint);
    const int32 OriginalDeclarationCount =
        Fixture.Blueprint->NewVariables.Num();

    const TSharedPtr<FJsonObject> DryRun =
        FSalBlueprintInterface::Patch(
            RobustBlueprintCreationPatch(true),
            Target);
    TestTrue(
        TEXT("Variable and Dispatcher creation dry run validates"),
        RobustBlueprintResultBool(DryRun, TEXT("valid")));
    TestFalse(
        TEXT("Creation dry run does not apply"),
        RobustBlueprintResultBool(DryRun, TEXT("applied")));
    TestEqual(
        TEXT("Creation dry run leaves declaration count unchanged"),
        Fixture.Blueprint->NewVariables.Num(),
        OriginalDeclarationCount);

    const TSharedPtr<FJsonObject> Applied =
        FSalBlueprintInterface::Patch(
            RobustBlueprintCreationPatch(false),
            Target);
    TestTrue(
        TEXT("Variable and Dispatcher creation applies"),
        RobustBlueprintResultBool(Applied, TEXT("valid"))
            && RobustBlueprintResultBool(Applied, TEXT("applied")));
    FBPVariableDescription* RuntimeValue =
        Fixture.FindVariable(TEXT("RuntimeValue"));
    FBPVariableDescription* RuntimeSignal =
        Fixture.FindVariable(TEXT("RuntimeSignal"));
    TestNotNull(TEXT("Created Variable exists natively"), RuntimeValue);
    TestNotNull(TEXT("Created Dispatcher exists natively"), RuntimeSignal);
    if (RuntimeValue == nullptr || RuntimeSignal == nullptr)
    {
        Transactions.Restore();
        return false;
    }
    const FString RuntimeValueId =
        RobustBlueprintGuidText(RuntimeValue->VarGuid);
    const FString RuntimeSignalId =
        RobustBlueprintGuidText(RuntimeSignal->VarGuid);

    FSalPatch EditPatch;
    EditPatch.Alias = TEXT("blueprint");
    EditPatch.Statements = {
        RobustBlueprintSet(
            RobustBlueprintMember(
                RobustBlueprintTyped(
                    TEXT("variable"),
                    RuntimeValueId),
                TEXT("Category")),
            MakeShared<FJsonValueString>(TEXT("Robustness"))),
        RobustBlueprintMove(
            RobustBlueprintTyped(
                TEXT("variable"),
                RuntimeValueId),
            TEXT("before"),
            RobustBlueprintTyped(
                TEXT("variable"),
                RobustBlueprintGuidText(
                    Fixture.FindVariable(
                        Fixture.SeedAlphaName)->VarGuid)))
    };
    const TSharedPtr<FJsonObject> Edited =
        FSalBlueprintInterface::Patch(EditPatch, Target);
    TestTrue(
        TEXT("Variable set and move apply atomically"),
        RobustBlueprintResultBool(Edited, TEXT("valid"))
            && RobustBlueprintResultBool(Edited, TEXT("applied")));
    RuntimeValue = Fixture.FindVariable(TEXT("RuntimeValue"));
    const int32 RuntimeIndex =
        FBlueprintEditorUtils::FindNewVariableIndex(
            Fixture.Blueprint,
            TEXT("RuntimeValue"));
    const int32 SeedAlphaIndex =
        FBlueprintEditorUtils::FindNewVariableIndex(
            Fixture.Blueprint,
            Fixture.SeedAlphaName);
    TestTrue(
        TEXT("Variable native Category is changed"),
        RuntimeValue != nullptr
            && RuntimeValue->Category.EqualTo(
                FText::FromString(TEXT("Robustness"))));
    TestTrue(
        TEXT("Variable native order follows move before"),
        RuntimeIndex != INDEX_NONE
            && SeedAlphaIndex != INDEX_NONE
            && RuntimeIndex < SeedAlphaIndex);

    FSalQuery ExactVariable =
        RobustBlueprintQuery(TEXT("variable"));
    ExactVariable.Operation->SetStringField(
        TEXT("id"),
        RuntimeValueId);
    const TSharedPtr<FJsonObject> VariableReadback =
        FSalBlueprintInterface::Query(ExactVariable, Target);
    TestFalse(
        TEXT("Exact Variable readback succeeds after edit"),
        RobustBlueprintHasError(VariableReadback));
    TSharedPtr<FJsonObject> VariableArgs =
        RobustBlueprintCallWithId(
            VariableReadback,
            TEXT("variable"),
            RuntimeValueId);
    FString ReadbackCategory;
    TestTrue(
        TEXT("SAL Variable readback observes native Category"),
        VariableArgs.IsValid()
            && VariableArgs->TryGetStringField(
                TEXT("Category"),
                ReadbackCategory)
            && ReadbackCategory == TEXT("Robustness"));

    FSalPatch ResetRemovePatch;
    ResetRemovePatch.Alias = TEXT("blueprint");
    ResetRemovePatch.Statements = {
        RobustBlueprintUnary(
            TEXT("reset"),
            RobustBlueprintMember(
                RobustBlueprintTyped(
                    TEXT("variable"),
                    RuntimeValueId),
                TEXT("Category"))),
        RobustBlueprintUnary(
            TEXT("remove"),
            RobustBlueprintTyped(
                TEXT("dispatcher"),
                RuntimeSignalId))
    };
    const TSharedPtr<FJsonObject> ResetRemoved =
        FSalBlueprintInterface::Patch(ResetRemovePatch, Target);
    TestTrue(
        TEXT("Variable reset and Dispatcher remove apply"),
        RobustBlueprintResultBool(ResetRemoved, TEXT("valid"))
            && RobustBlueprintResultBool(ResetRemoved, TEXT("applied")));
    TestTrue(
        TEXT("Variable Category reset uses the native declaration default"),
        Fixture.FindVariable(TEXT("RuntimeValue")) != nullptr
            && Fixture.FindVariable(TEXT("RuntimeValue"))
                ->Category.EqualTo(
                    UEdGraphSchema_K2::VR_DefaultCategory));
    TestNull(
        TEXT("Dispatcher declaration is removed"),
        Fixture.FindVariable(TEXT("RuntimeSignal")));
    TestFalse(
        TEXT("Dispatcher Signature Graph is removed with declaration"),
        Fixture.Blueprint->DelegateSignatureGraphs.ContainsByPredicate(
            [](const UEdGraph* Graph)
            {
                return Graph != nullptr
                    && Graph->GetFName() == TEXT("RuntimeSignal");
            }));

    TestTrue(
        TEXT("Undo restores reset and remove as one step"),
        GEditor->UndoTransaction(false));
    TestNotNull(
        TEXT("Undo restores Dispatcher declaration"),
        Fixture.FindVariable(TEXT("RuntimeSignal")));
    TestTrue(
        TEXT("Undo restores the edited Variable Category"),
        Fixture.FindVariable(TEXT("RuntimeValue")) != nullptr
            && Fixture.FindVariable(TEXT("RuntimeValue"))
                ->Category.EqualTo(
                    FText::FromString(TEXT("Robustness"))));

    TestTrue(
        TEXT("Second Undo restores move and set"),
        GEditor->UndoTransaction(false));
    TestTrue(
        TEXT("Second Undo restores Variable Category default"),
        Fixture.FindVariable(TEXT("RuntimeValue")) != nullptr
            && Fixture.FindVariable(TEXT("RuntimeValue"))
                ->Category.EqualTo(
                    UEdGraphSchema_K2::VR_DefaultCategory));

    TestTrue(
        TEXT("Third Undo removes the created declarations"),
        GEditor->UndoTransaction(false));
    TestNull(
        TEXT("Undo creation removes Variable"),
        Fixture.FindVariable(TEXT("RuntimeValue")));
    TestNull(
        TEXT("Undo creation removes Dispatcher"),
        Fixture.FindVariable(TEXT("RuntimeSignal")));
    TestEqual(
        TEXT("Declaration count returns to baseline"),
        Fixture.Blueprint->NewVariables.Num(),
        OriginalDeclarationCount);

    Transactions.Restore();
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FSalRobustBlueprintGraphComponentLifecycleTest,
    "Loomle.Sal.Robustness.Blueprint.GraphComponentLifecycle",
    EAutomationTestFlags::EditorContext
        | EAutomationTestFlags::EngineFilter)

bool FSalRobustBlueprintGraphComponentLifecycleTest::RunTest(
    const FString& Parameters)
{
    if (!RobustBlueprintRequireIdleEditor(
            *this,
            TEXT("Blueprint Graph/Component lifecycle")))
    {
        return false;
    }
    Loomle::Tests::FScopedIsolatedTransactor Transactions;
    if (!TestTrue(
            TEXT("Graph/Component lifecycle isolates Undo history"),
            Transactions.Initialize()))
    {
        return false;
    }
    FRobustBlueprintFixture Fixture;
    if (!TestTrue(TEXT("Graph/Component lifecycle fixture is valid"), Fixture.IsValid()))
    {
        Transactions.Restore();
        return false;
    }
    const FSalResolvedTarget Target =
        RobustBlueprintTarget(Fixture.Blueprint);
    const int32 OriginalFunctionCount =
        Fixture.Blueprint->FunctionGraphs.Num();
    const int32 OriginalComponentCount =
        Fixture.Blueprint->SimpleConstructionScript->GetAllNodes().Num();

    const TSharedPtr<FJsonObject> DryRun =
        FSalBlueprintInterface::Patch(
            RobustBlueprintGraphComponentCreationPatch(true),
            Target);
    TestTrue(
        TEXT("Graph/Component creation dry run validates"),
        RobustBlueprintResultBool(DryRun, TEXT("valid")));
    TestEqual(
        TEXT("Graph creation dry run leaves source unchanged"),
        Fixture.Blueprint->FunctionGraphs.Num(),
        OriginalFunctionCount);
    TestEqual(
        TEXT("Component creation dry run leaves source unchanged"),
        Fixture.Blueprint->SimpleConstructionScript->GetAllNodes().Num(),
        OriginalComponentCount);

    const TSharedPtr<FJsonObject> Applied =
        FSalBlueprintInterface::Patch(
            RobustBlueprintGraphComponentCreationPatch(false),
            Target);
    TestTrue(
        TEXT("Graph/Component creation applies"),
        RobustBlueprintResultBool(Applied, TEXT("valid"))
            && RobustBlueprintResultBool(Applied, TEXT("applied")));

    UEdGraph* RuntimeFunction =
        Fixture.Blueprint->FunctionGraphs.FindByPredicate(
            [](const UEdGraph* Graph)
            {
                return Graph != nullptr
                    && Graph->GetFName() == TEXT("RuntimeFunction");
            })
            ? *Fixture.Blueprint->FunctionGraphs.FindByPredicate(
                [](const UEdGraph* Graph)
                {
                    return Graph != nullptr
                        && Graph->GetFName() == TEXT("RuntimeFunction");
                })
            : nullptr;
    USCS_Node* RuntimeAudio = nullptr;
    for (USCS_Node* Node :
         Fixture.Blueprint->SimpleConstructionScript->GetAllNodes())
    {
        if (Node != nullptr
            && Node->GetVariableName() == TEXT("RuntimeAudio"))
        {
            RuntimeAudio = Node;
            break;
        }
    }
    TestNotNull(TEXT("Function Graph exists natively"), RuntimeFunction);
    TestNotNull(TEXT("Audio Component exists natively"), RuntimeAudio);
    TestTrue(
        TEXT("Component constructor followed by set updates native template"),
        RuntimeAudio != nullptr
            && RuntimeAudio->ComponentTemplate != nullptr
            && !RuntimeAudio->ComponentTemplate->bAutoActivate);
    if (RuntimeFunction == nullptr || RuntimeAudio == nullptr)
    {
        Transactions.Restore();
        return false;
    }

    FSalQuery ExactGraph = RobustBlueprintQuery(TEXT("graph"));
    ExactGraph.Operation->SetStringField(
        TEXT("id"),
        RobustBlueprintGuidText(RuntimeFunction->GraphGuid));
    FSalQuery ExactComponent =
        RobustBlueprintQuery(TEXT("component"));
    ExactComponent.Operation->SetStringField(
        TEXT("id"),
        RobustBlueprintGuidText(RuntimeAudio->VariableGuid));
    TestFalse(
        TEXT("Created Graph is immediately queryable by stable id"),
        RobustBlueprintHasError(
            FSalBlueprintInterface::Query(ExactGraph, Target)));
    TestFalse(
        TEXT("Created Component is immediately queryable by stable id"),
        RobustBlueprintHasError(
            FSalBlueprintInterface::Query(ExactComponent, Target)));

    FSalPatch MovePatch;
    MovePatch.Alias = TEXT("blueprint");
    MovePatch.Statements = {
        RobustBlueprintMove(
            RobustBlueprintTyped(
                TEXT("graph"),
                RobustBlueprintGuidText(RuntimeFunction->GraphGuid)),
            TEXT("before"),
            RobustBlueprintTyped(
                TEXT("graph"),
                RobustBlueprintGuidText(
                    Fixture.FunctionGraph->GraphGuid)))
    };
    const TSharedPtr<FJsonObject> Moved =
        FSalBlueprintInterface::Patch(MovePatch, Target);
    TestTrue(
        TEXT("Top-level Graph order move applies"),
        RobustBlueprintResultBool(Moved, TEXT("valid"))
            && RobustBlueprintResultBool(Moved, TEXT("applied")));
    TestTrue(
        TEXT("Function Graph native order follows SAL move"),
        Fixture.Blueprint->FunctionGraphs.IndexOfByKey(RuntimeFunction)
            < Fixture.Blueprint->FunctionGraphs.IndexOfByKey(
                Fixture.FunctionGraph));

    FSalPatch RemovePatch;
    RemovePatch.Alias = TEXT("blueprint");
    RemovePatch.Statements = {
        RobustBlueprintUnary(
            TEXT("remove"),
            RobustBlueprintTyped(
                TEXT("component"),
                RobustBlueprintGuidText(RuntimeAudio->VariableGuid))),
        RobustBlueprintUnary(
            TEXT("remove"),
            RobustBlueprintTyped(
                TEXT("graph"),
                RobustBlueprintGuidText(RuntimeFunction->GraphGuid)))
    };
    const TSharedPtr<FJsonObject> Removed =
        FSalBlueprintInterface::Patch(RemovePatch, Target);
    TestTrue(
        TEXT("Graph and Component removal applies atomically"),
        RobustBlueprintResultBool(Removed, TEXT("valid"))
            && RobustBlueprintResultBool(Removed, TEXT("applied")));
    TestEqual(
        TEXT("Graph removal returns function count to baseline"),
        Fixture.Blueprint->FunctionGraphs.Num(),
        OriginalFunctionCount);
    TestEqual(
        TEXT("Component removal returns SCS count to baseline"),
        Fixture.Blueprint->SimpleConstructionScript->GetAllNodes().Num(),
        OriginalComponentCount);

    TestTrue(
        TEXT("Undo restores Graph and Component removal"),
        GEditor->UndoTransaction(false));
    TestTrue(
        TEXT("Undo restores Function Graph identity"),
        Fixture.Blueprint->FunctionGraphs.ContainsByPredicate(
            [RuntimeFunction](const UEdGraph* Graph)
            {
                return Graph == RuntimeFunction;
            }));
    TestTrue(
        TEXT("Undo restores Component identity"),
        Fixture.Blueprint->SimpleConstructionScript
            ->GetAllNodes()
            .ContainsByPredicate(
                [RuntimeAudio](const USCS_Node* Node)
                {
                    return Node == RuntimeAudio;
                }));

    Transactions.Restore();
    return true;
}

#endif
