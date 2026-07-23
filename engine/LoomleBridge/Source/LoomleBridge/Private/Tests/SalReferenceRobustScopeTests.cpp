// Copyright 2026 Loomle contributors.

#if WITH_DEV_AUTOMATION_TESTS

#include "Sal/Reference/SalReferenceInterface.h"

#include "Components/SceneComponent.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "EdGraph/EdGraph.h"
#include "EdGraphSchema_K2.h"
#include "EdGraphSchema_K2_Actions.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Engine/SCS_Node.h"
#include "Engine/SimpleConstructionScript.h"
#include "GameFramework/Actor.h"
#include "K2Node_CallDelegate.h"
#include "K2Node_CallFunction.h"
#include "K2Node_CustomEvent.h"
#include "K2Node_FunctionEntry.h"
#include "K2Node_VariableGet.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/AutomationTest.h"
#include "Sal/SalModel.h"
#include "UObject/GarbageCollection.h"
#include "UObject/Package.h"
#include "UObject/UObjectHash.h"

namespace
{
using namespace Loomle::Sal;

FString RobustReferenceGuid(const FGuid& Guid)
{
    return Guid.ToString(EGuidFormats::DigitsWithHyphensLower);
}

bool RobustReferenceHasError(
    const TSharedPtr<FJsonObject>& Result)
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
            && (*Diagnostic)->TryGetStringField(TEXT("severity"), Severity)
            && Severity == TEXT("error"))
        {
            return true;
        }
    }
    return false;
}

bool RobustReferenceHasCode(
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

TArray<FString> RobustReferenceCallIds(
    const TSharedPtr<FJsonObject>& Result,
    const FString& Callee)
{
    TArray<FString> Ids;
    const TSharedPtr<FJsonObject>* Object = nullptr;
    const TArray<TSharedPtr<FJsonValue>>* Statements = nullptr;
    if (!Result.IsValid()
        || !Result->TryGetObjectField(TEXT("object"), Object)
        || Object == nullptr
        || !(*Object)->TryGetArrayField(TEXT("statements"), Statements)
        || Statements == nullptr)
    {
        return Ids;
    }
    for (const TSharedPtr<FJsonValue>& Value : *Statements)
    {
        const TSharedPtr<FJsonObject>* Statement = nullptr;
        const TSharedPtr<FJsonObject>* Call = nullptr;
        const TSharedPtr<FJsonObject>* Args = nullptr;
        FString ActualCallee;
        FString Id;
        if (Value.IsValid()
            && Value->TryGetObject(Statement)
            && Statement != nullptr
            && (*Statement)->TryGetObjectField(TEXT("value"), Call)
            && Call != nullptr
            && (*Call)->TryGetStringField(TEXT("callee"), ActualCallee)
            && ActualCallee == Callee
            && (*Call)->TryGetObjectField(TEXT("args"), Args)
            && Args != nullptr
            && (*Args)->TryGetStringField(TEXT("id"), Id))
        {
            Ids.Add(Id);
        }
    }
    return Ids;
}

FString RobustReferenceNext(
    const TSharedPtr<FJsonObject>& Result)
{
    const TSharedPtr<FJsonObject>* Page = nullptr;
    FString Next;
    if (Result.IsValid()
        && Result->TryGetObjectField(TEXT("page"), Page)
        && Page != nullptr)
    {
        (*Page)->TryGetStringField(TEXT("next"), Next);
    }
    return Next;
}

FSalQuery RobustReferenceQuery(
    const FString& Kind,
    const FGuid& Id,
    const int32 Limit = 50)
{
    TSharedRef<FJsonObject> Subject = MakeShared<FJsonObject>();
    Subject->SetStringField(TEXT("kind"), Kind);
    Subject->SetStringField(TEXT("id"), RobustReferenceGuid(Id));
    FSalQuery Query;
    Query.Alias = TEXT("fixture");
    Query.Operation = MakeShared<FJsonObject>();
    Query.Operation->SetStringField(TEXT("kind"), TEXT("references"));
    Query.Operation->SetObjectField(TEXT("target"), Subject);
    Query.PageLimit = Limit;
    return Query;
}

class FRobustReferenceFixture
{
public:
    ~FRobustReferenceFixture()
    {
        Cleanup();
    }

    bool Initialize(FString& OutError)
    {
        Package = CreatePackage(*FString::Printf(
            TEXT("/Game/LoomleTests/ReferenceRobust_%s"),
            *FGuid::NewGuid().ToString(EGuidFormats::Digits)));
        Blueprint = Package != nullptr
            ? FKismetEditorUtilities::CreateBlueprint(
                AActor::StaticClass(),
                Package,
                FName(TEXT("BP_ReferenceRobust")),
                BPTYPE_Normal,
                UBlueprint::StaticClass(),
                UBlueprintGeneratedClass::StaticClass(),
                NAME_None)
            : nullptr;
        EventGraph = Blueprint != nullptr
            ? FBlueprintEditorUtils::FindEventGraph(Blueprint)
            : nullptr;
        if (Blueprint == nullptr || EventGraph == nullptr)
        {
            OutError = TEXT("UE could not create robust Reference fixture.");
            return false;
        }

        const FEdGraphPinType IntType(
            UEdGraphSchema_K2::PC_Int,
            NAME_None,
            nullptr,
            EPinContainerType::None,
            false,
            FEdGraphTerminalType());
        if (!FBlueprintEditorUtils::AddMemberVariable(
                Blueprint,
                MemberName,
                IntType))
        {
            OutError = TEXT("UE could not add robust member Variable.");
            return false;
        }
        MemberId = FBlueprintEditorUtils::FindMemberVariableGuidByName(
            Blueprint,
            MemberName);

        FEdGraphPinType DelegateType(
            UEdGraphSchema_K2::PC_MCDelegate,
            NAME_None,
            nullptr,
            EPinContainerType::None,
            false,
            FEdGraphTerminalType());
        if (!FBlueprintEditorUtils::AddMemberVariable(
                Blueprint,
                DispatcherName,
                DelegateType))
        {
            OutError = TEXT("UE could not add robust Dispatcher.");
            return false;
        }
        UEdGraph* DispatcherSignature =
            FBlueprintEditorUtils::CreateNewGraph(
                Blueprint,
                DispatcherName,
                UEdGraph::StaticClass(),
                UEdGraphSchema_K2::StaticClass());
        const UEdGraphSchema_K2* K2Schema =
            GetDefault<UEdGraphSchema_K2>();
        if (DispatcherSignature == nullptr || K2Schema == nullptr)
        {
            OutError = TEXT("UE could not create robust Dispatcher signature.");
            return false;
        }
        DispatcherSignature->bEditable = false;
        K2Schema->CreateDefaultNodesForGraph(*DispatcherSignature);
        K2Schema->CreateFunctionGraphTerminators(
            *DispatcherSignature,
            static_cast<UClass*>(nullptr));
        K2Schema->AddExtraFunctionFlags(
            DispatcherSignature,
            FUNC_BlueprintCallable | FUNC_BlueprintEvent | FUNC_Public);
        K2Schema->MarkFunctionEntryAsEditable(
            DispatcherSignature,
            true);
        Blueprint->DelegateSignatureGraphs.Add(DispatcherSignature);
        DispatcherId =
            FBlueprintEditorUtils::FindMemberVariableGuidByName(
                Blueprint,
                DispatcherName);

        FunctionGraph = FBlueprintEditorUtils::CreateNewGraph(
            Blueprint,
            FunctionName,
            UEdGraph::StaticClass(),
            UEdGraphSchema_K2::StaticClass());
        if (FunctionGraph == nullptr)
        {
            OutError = TEXT("UE could not create robust Function Graph.");
            return false;
        }
        FBlueprintEditorUtils::AddFunctionGraph<UClass>(
            Blueprint,
            FunctionGraph,
            true,
            nullptr);
        FunctionId = FunctionGraph->GraphGuid;
        TArray<UK2Node_FunctionEntry*> Entries;
        FunctionGraph->GetNodesOfClass(Entries);
        if (Entries.Num() != 1 || Entries[0] == nullptr)
        {
            OutError = TEXT("Robust Function Graph has no exact Function Entry.");
            return false;
        }
        FBPVariableDescription Local;
        Local.VarName = LocalName;
        Local.VarGuid = FGuid::NewGuid();
        Local.VarType = IntType;
        Entries[0]->LocalVariables.Add(Local);
        LocalId = Local.VarGuid;

        Component = Blueprint->SimpleConstructionScript != nullptr
            ? Blueprint->SimpleConstructionScript->CreateNode(
                USceneComponent::StaticClass(),
                ComponentName)
            : nullptr;
        if (Component == nullptr)
        {
            OutError = TEXT("UE could not create robust Component declaration.");
            return false;
        }
        Blueprint->SimpleConstructionScript->AddNode(Component);
        ComponentId = Component->VariableGuid;

        CustomEvent =
            FEdGraphSchemaAction_K2NewNode::SpawnNode<UK2Node_CustomEvent>(
                EventGraph,
                FVector2D(0.0, 400.0),
                EK2NewNodeFlags::None,
                [](UK2Node_CustomEvent* Node)
                {
                    Node->CustomFunctionName =
                        FName(TEXT("RobustEvent"));
                });
        if (CustomEvent == nullptr)
        {
            OutError = TEXT("UE could not create robust Custom Event.");
            return false;
        }
        EventId = CustomEvent->NodeGuid;

        // Compile declarations before authoring use-sites so UE creates the
        // exact Function scope and generated member properties used by native
        // FMemberReference resolution.
        FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
        FKismetEditorUtilities::CompileBlueprint(Blueprint);
        if (Blueprint->Status == BS_Error
            || !MemberId.IsValid()
            || !DispatcherId.IsValid()
            || !FunctionId.IsValid()
            || !LocalId.IsValid()
            || !ComponentId.IsValid()
            || !EventId.IsValid())
        {
            OutError = TEXT("Robust Reference declarations did not compile with stable identity.");
            return false;
        }

        EventVariableUse = SpawnVariableUse(
            EventGraph,
            FVector2D(300.0, 0.0),
            MemberName,
            MemberId,
            false);
        FunctionVariableUse = SpawnVariableUse(
            FunctionGraph,
            FVector2D(300.0, 0.0),
            MemberName,
            MemberId,
            false);
        LocalUse = SpawnVariableUse(
            FunctionGraph,
            FVector2D(300.0, 120.0),
            LocalName,
            LocalId,
            true);
        ComponentUse = SpawnVariableUse(
            EventGraph,
            FVector2D(300.0, 120.0),
            ComponentName,
            ComponentId,
            false);
        DispatcherUse =
            FEdGraphSchemaAction_K2NewNode::SpawnNode<UK2Node_CallDelegate>(
                EventGraph,
                FVector2D(300.0, 240.0),
                EK2NewNodeFlags::None,
                [&](UK2Node_CallDelegate* Node)
                {
                    Node->DelegateReference.SetSelfMember(
                        DispatcherName,
                        DispatcherId);
                });
        FunctionUse =
            FEdGraphSchemaAction_K2NewNode::SpawnNode<UK2Node_CallFunction>(
                EventGraph,
                FVector2D(500.0, 0.0),
                EK2NewNodeFlags::None,
                [&](UK2Node_CallFunction* Node)
                {
                    Node->FunctionReference.SetSelfMember(
                        FunctionName,
                        FunctionId);
                });
        EventUse =
            FEdGraphSchemaAction_K2NewNode::SpawnNode<UK2Node_CallFunction>(
                EventGraph,
                FVector2D(500.0, 120.0),
                EK2NewNodeFlags::None,
                [&](UK2Node_CallFunction* Node)
                {
                    Node->FunctionReference.SetSelfMember(
                        CustomEvent->CustomFunctionName,
                        EventId);
                });
        if (EventVariableUse == nullptr
            || FunctionVariableUse == nullptr
            || LocalUse == nullptr
            || ComponentUse == nullptr
            || DispatcherUse == nullptr
            || FunctionUse == nullptr
            || EventUse == nullptr)
        {
            OutError = TEXT("UE could not author every robust Reference use-site.");
            return false;
        }
        FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
        Package->SetDirtyFlag(false);
        return true;
    }

    FSalResolvedTarget BlueprintTarget() const
    {
        FSalResolvedTarget Target;
        Target.Kind = ESalTargetKind::Blueprint;
        Target.Alias = TEXT("fixture");
        Target.AssetPath = Blueprint->GetPathName();
        Target.Id = RobustReferenceGuid(Blueprint->GetBlueprintGuid());
        Target.Object = Blueprint;
        Target.Package = Package;
        Target.Blueprint = Blueprint;
        Target.Class = Blueprint->GeneratedClass.Get();
        Target.Interfaces = {
            FName(TEXT("blueprint")),
            FName(TEXT("graph"))};
        return Target;
    }

    FSalResolvedTarget GraphTarget(UEdGraph* Graph) const
    {
        FSalResolvedTarget Target = BlueprintTarget();
        Target.Kind = ESalTargetKind::Graph;
        Target.Graph = Graph;
        Target.Id = Graph != nullptr
            ? RobustReferenceGuid(Graph->GraphGuid)
            : FString();
        return Target;
    }

    void Cleanup()
    {
        if (bCleaned)
        {
            return;
        }
        bCleaned = true;
        UPackage* PackageToUnload = Package;
        if (Blueprint != nullptr)
        {
            Blueprint->ClearFlags(RF_Public | RF_Standalone);
        }
        Blueprint = nullptr;
        Package = nullptr;
        EventGraph = nullptr;
        FunctionGraph = nullptr;
        Component = nullptr;
        CustomEvent = nullptr;
        EventVariableUse = nullptr;
        FunctionVariableUse = nullptr;
        LocalUse = nullptr;
        ComponentUse = nullptr;
        DispatcherUse = nullptr;
        FunctionUse = nullptr;
        EventUse = nullptr;
        if (PackageToUnload != nullptr)
        {
            PackageToUnload->SetDirtyFlag(false);
            PackageToUnload->ClearFlags(RF_Public | RF_Standalone);
            ForEachObjectWithPackage(
                PackageToUnload,
                [](UObject* Object)
                {
                    Object->ClearFlags(RF_Public | RF_Standalone);
                    return true;
                },
                true);
        }
        CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);
    }

    static UK2Node_VariableGet* SpawnVariableUse(
        UEdGraph* Graph,
        const FVector2D Position,
        const FName Name,
        const FGuid& Guid,
        const bool bLocal)
    {
        return FEdGraphSchemaAction_K2NewNode::SpawnNode<UK2Node_VariableGet>(
            Graph,
            Position,
            EK2NewNodeFlags::None,
            [&](UK2Node_VariableGet* Node)
            {
                if (bLocal)
                {
                    Node->VariableReference.SetLocalMember(
                        Name,
                        FunctionName.ToString(),
                        Guid);
                }
                else
                {
                    Node->VariableReference.SetSelfMember(Name, Guid);
                }
            });
    }

    static const FName MemberName;
    static const FName DispatcherName;
    static const FName FunctionName;
    static const FName LocalName;
    static const FName ComponentName;

    UPackage* Package = nullptr;
    UBlueprint* Blueprint = nullptr;
    UEdGraph* EventGraph = nullptr;
    UEdGraph* FunctionGraph = nullptr;
    USCS_Node* Component = nullptr;
    UK2Node_CustomEvent* CustomEvent = nullptr;
    UK2Node_VariableGet* EventVariableUse = nullptr;
    UK2Node_VariableGet* FunctionVariableUse = nullptr;
    UK2Node_VariableGet* LocalUse = nullptr;
    UK2Node_VariableGet* ComponentUse = nullptr;
    UK2Node_CallDelegate* DispatcherUse = nullptr;
    UK2Node_CallFunction* FunctionUse = nullptr;
    UK2Node_CallFunction* EventUse = nullptr;
    FGuid MemberId;
    FGuid DispatcherId;
    FGuid FunctionId;
    FGuid LocalId;
    FGuid ComponentId;
    FGuid EventId;

private:
    bool bCleaned = false;
};

const FName FRobustReferenceFixture::MemberName(TEXT("RobustHealth"));
const FName FRobustReferenceFixture::DispatcherName(TEXT("OnRobustReady"));
const FName FRobustReferenceFixture::FunctionName(TEXT("RobustFunction"));
const FName FRobustReferenceFixture::LocalName(TEXT("RobustLocal"));
const FName FRobustReferenceFixture::ComponentName(TEXT("RobustComponent"));
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FSalReferenceRobustDeclarationMatrixTest,
    "Loomle.Sal.Reference.Robust.LocalDeclarationKindMatrix",
    EAutomationTestFlags::EditorContext
        | EAutomationTestFlags::EngineFilter)

bool FSalReferenceRobustDeclarationMatrixTest::RunTest(
    const FString& Parameters)
{
    (void)Parameters;
    FRobustReferenceFixture Fixture;
    FString Error;
    if (!TestTrue(TEXT("Robust Reference fixture initializes"), Fixture.Initialize(Error)))
    {
        AddError(Error);
        return false;
    }
    const FSalResolvedTarget Target = Fixture.BlueprintTarget();
    struct FCase
    {
        FString Label;
        FString Kind;
        FGuid Subject;
        UEdGraphNode* ExpectedUse;
    };
    const TArray<FCase> Cases = {
        {TEXT("member Variable"), TEXT("variable"), Fixture.MemberId, Fixture.EventVariableUse},
        {TEXT("local Variable"), TEXT("variable"), Fixture.LocalId, Fixture.LocalUse},
        {TEXT("Dispatcher"), TEXT("dispatcher"), Fixture.DispatcherId, Fixture.DispatcherUse},
        {TEXT("Component"), TEXT("component"), Fixture.ComponentId, Fixture.ComponentUse},
        {TEXT("Function"), TEXT("graph"), Fixture.FunctionId, Fixture.FunctionUse},
        {TEXT("Custom Event"), TEXT("node"), Fixture.EventId, Fixture.EventUse}};
    for (const FCase& Case : Cases)
    {
        const TSharedPtr<FJsonObject> Result =
            FSalReferenceInterface::Query(
                RobustReferenceQuery(Case.Kind, Case.Subject),
                Target);
        TestFalse(
            *FString::Printf(TEXT("%s reference Query is complete"), *Case.Label),
            RobustReferenceHasError(Result));
        const TArray<FString> NodeIds =
            RobustReferenceCallIds(Result, TEXT("node"));
        TestTrue(
            *FString::Printf(TEXT("%s resolves its exact native use-site"), *Case.Label),
            Case.ExpectedUse != nullptr
                && NodeIds.Contains(
                    RobustReferenceGuid(Case.ExpectedUse->NodeGuid)));
    }
    Fixture.Cleanup();
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FSalReferenceRobustScopePaginationTest,
    "Loomle.Sal.Reference.Robust.GraphScopePaginationDedup",
    EAutomationTestFlags::EditorContext
        | EAutomationTestFlags::EngineFilter)

bool FSalReferenceRobustScopePaginationTest::RunTest(
    const FString& Parameters)
{
    (void)Parameters;
    FRobustReferenceFixture Fixture;
    FString Error;
    if (!Fixture.Initialize(Error))
    {
        AddError(Error);
        return false;
    }

    FSalQuery FirstQuery =
        RobustReferenceQuery(TEXT("variable"), Fixture.MemberId, 1);
    const TSharedPtr<FJsonObject> First =
        FSalReferenceInterface::Query(
            FirstQuery,
            Fixture.BlueprintTarget());
    TestFalse(TEXT("Blueprint-scope first page succeeds"), RobustReferenceHasError(First));
    const TArray<FString> FirstIds =
        RobustReferenceCallIds(First, TEXT("node"));
    const FString Cursor = RobustReferenceNext(First);
    TestEqual(TEXT("Blueprint-scope page limit is respected"), FirstIds.Num(), 1);
    TestFalse(TEXT("Blueprint-scope first page returns a continuation"), Cursor.IsEmpty());

    FSalQuery SecondQuery = FirstQuery;
    SecondQuery.PageAfter = Cursor;
    const TSharedPtr<FJsonObject> Second =
        FSalReferenceInterface::Query(
            SecondQuery,
            Fixture.BlueprintTarget());
    TestFalse(TEXT("Blueprint-scope continuation succeeds"), RobustReferenceHasError(Second));
    const TArray<FString> SecondIds =
        RobustReferenceCallIds(Second, TEXT("node"));
    TestEqual(TEXT("Blueprint-scope second page has the remaining use-site"), SecondIds.Num(), 1);
    TSet<FString> BlueprintSites;
    BlueprintSites.Append(FirstIds);
    BlueprintSites.Append(SecondIds);
    TestEqual(
        TEXT("Blueprint scope emits both Graph use-sites exactly once"),
        BlueprintSites.Num(),
        2);
    TestTrue(
        TEXT("Blueprint scope includes the Event Graph use-site"),
        BlueprintSites.Contains(
            RobustReferenceGuid(
                Fixture.EventVariableUse->NodeGuid)));
    TestTrue(
        TEXT("Blueprint scope includes the Function Graph use-site"),
        BlueprintSites.Contains(
            RobustReferenceGuid(
                Fixture.FunctionVariableUse->NodeGuid)));

    const TSharedPtr<FJsonObject> EventScoped =
        FSalReferenceInterface::Query(
            RobustReferenceQuery(TEXT("variable"), Fixture.MemberId),
            Fixture.GraphTarget(Fixture.EventGraph));
    const TArray<FString> EventIds =
        RobustReferenceCallIds(EventScoped, TEXT("node"));
    TestFalse(TEXT("Event Graph-scope Query succeeds"), RobustReferenceHasError(EventScoped));
    TestEqual(TEXT("Event Graph scope returns one use-site"), EventIds.Num(), 1);
    TestTrue(
        TEXT("Event Graph scope excludes the Function Graph use-site"),
        EventIds.Contains(
            RobustReferenceGuid(Fixture.EventVariableUse->NodeGuid)));

    FSalQuery WrongScope = FirstQuery;
    WrongScope.PageAfter = Cursor;
    const TSharedPtr<FJsonObject> ReusedAcrossScope =
        FSalReferenceInterface::Query(
            WrongScope,
            Fixture.GraphTarget(Fixture.EventGraph));
    TestTrue(
        TEXT("Blueprint cursor cannot be reused for a Graph-local scan"),
        RobustReferenceHasCode(
            ReusedAcrossScope,
            TEXT("validation.invalid_cursor")));

    Fixture.Cleanup();
    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
