// Copyright 2026 Loomle contributors.

#if WITH_DEV_AUTOMATION_TESTS

#include "Sal/Blueprint/SalBlueprintInterface.h"
#include "Sal/Graph/SalGraphInterface.h"
#include "Tests/LoomleTestEditorState.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraphSchema_K2.h"
#include "Editor.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "GameFramework/Actor.h"
#include "HAL/FileManager.h"
#include "K2Node_CallFunction.h"
#include "K2Node_CustomEvent.h"
#include "K2Node_IfThenElse.h"
#include "Kismet/KismetMathLibrary.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/AutomationTest.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "PackageTools.h"
#include "UObject/Package.h"
#include "UObject/UObjectGlobals.h"

namespace
{
using namespace Loomle::Sal;

FString RobustGraphGuidText(const FGuid& Guid)
{
    return Guid.ToString(EGuidFormats::DigitsWithHyphensLower);
}

bool RobustGraphRequireIdleEditor(
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

bool RobustGraphResultBool(
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

bool RobustGraphHasError(const TSharedPtr<FJsonObject>& Result)
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

bool RobustGraphHasDiagnosticCode(
    const TSharedPtr<FJsonObject>& Result,
    const FString& Code)
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
        FString Actual;
        if (Value.IsValid()
            && Value->TryGetObject(Diagnostic)
            && Diagnostic != nullptr
            && (*Diagnostic)->TryGetStringField(TEXT("code"), Actual)
            && Actual == Code)
        {
            return true;
        }
    }
    return false;
}

FString RobustGraphDiagnosticsText(
    const TSharedPtr<FJsonObject>& Result)
{
    const TArray<TSharedPtr<FJsonValue>>* Diagnostics = nullptr;
    if (!Result.IsValid())
    {
        return TEXT("result unavailable");
    }
    if (!Result->TryGetArrayField(TEXT("diagnostics"), Diagnostics)
        || Diagnostics == nullptr
        || Diagnostics->IsEmpty())
    {
        return TEXT("no diagnostics");
    }
    TArray<FString> Lines;
    for (const TSharedPtr<FJsonValue>& Value : *Diagnostics)
    {
        const TSharedPtr<FJsonObject>* Diagnostic = nullptr;
        if (!Value.IsValid()
            || !Value->TryGetObject(Diagnostic)
            || Diagnostic == nullptr)
        {
            Lines.Add(TEXT("<invalid diagnostic>"));
            continue;
        }
        FString Severity;
        FString Code;
        FString Message;
        (*Diagnostic)->TryGetStringField(TEXT("severity"), Severity);
        (*Diagnostic)->TryGetStringField(TEXT("code"), Code);
        (*Diagnostic)->TryGetStringField(TEXT("message"), Message);
        Lines.Add(FString::Printf(
            TEXT("%s %s: %s"),
            *Severity,
            *Code,
            *Message));
    }
    return FString::Join(Lines, TEXT(" | "));
}

TArray<TSharedPtr<FJsonObject>> RobustGraphCallArgs(
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

bool RobustGraphContainsCallId(
    const TSharedPtr<FJsonObject>& Result,
    const FString& Callee,
    const FGuid& Id)
{
    const FString Expected = RobustGraphGuidText(Id);
    for (const TSharedPtr<FJsonObject>& Args :
         RobustGraphCallArgs(Result, Callee))
    {
        FString Actual;
        if (Args.IsValid()
            && Args->TryGetStringField(TEXT("id"), Actual)
            && Actual.Equals(Expected, ESearchCase::IgnoreCase))
        {
            return true;
        }
    }
    return false;
}

bool RobustGraphHasComment(
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

int32 RobustGraphEdgeCount(const TSharedPtr<FJsonObject>& Result)
{
    const TSharedPtr<FJsonObject>* Object = nullptr;
    const TArray<TSharedPtr<FJsonValue>>* Statements = nullptr;
    if (!Result.IsValid()
        || !Result->TryGetObjectField(TEXT("object"), Object)
        || Object == nullptr
        || !(*Object)->TryGetArrayField(TEXT("statements"), Statements)
        || Statements == nullptr)
    {
        return 0;
    }
    int32 Count = 0;
    for (const TSharedPtr<FJsonValue>& StatementValue : *Statements)
    {
        const TSharedPtr<FJsonObject>* Statement = nullptr;
        if (StatementValue.IsValid()
            && StatementValue->TryGetObject(Statement)
            && Statement != nullptr
            && (*Statement)->HasTypedField<EJson::Object>(TEXT("from"))
            && (*Statement)->HasTypedField<EJson::Object>(TEXT("to")))
        {
            ++Count;
        }
    }
    return Count;
}

FString RobustGraphNextCursor(
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

TSharedRef<FJsonObject> RobustGraphLocal(const FString& Name)
{
    TSharedRef<FJsonObject> Ref = MakeShared<FJsonObject>();
    Ref->SetStringField(TEXT("kind"), TEXT("local"));
    Ref->SetStringField(TEXT("name"), Name);
    return Ref;
}

TSharedRef<FJsonObject> RobustGraphTyped(
    const FString& Kind,
    const FGuid& Id)
{
    TSharedRef<FJsonObject> Ref = MakeShared<FJsonObject>();
    Ref->SetStringField(TEXT("kind"), Kind);
    Ref->SetStringField(TEXT("id"), RobustGraphGuidText(Id));
    return Ref;
}

TSharedRef<FJsonObject> RobustGraphMember(
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

TSharedRef<FJsonValue> RobustGraphBinding(
    const FString& Alias,
    const FString& Palette,
    const FString& Type = FString())
{
    TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
    Args->SetStringField(TEXT("palette"), Palette);
    if (!Type.IsEmpty())
    {
        Args->SetStringField(TEXT("type"), Type);
    }
    TSharedRef<FJsonObject> Call = MakeShared<FJsonObject>();
    Call->SetStringField(TEXT("kind"), TEXT("call"));
    Call->SetStringField(TEXT("callee"), TEXT("node"));
    Call->SetObjectField(TEXT("args"), Args);
    TSharedRef<FJsonObject> Statement = MakeShared<FJsonObject>();
    Statement->SetObjectField(
        TEXT("target"),
        RobustGraphLocal(Alias));
    Statement->SetObjectField(TEXT("value"), Call);
    return MakeShared<FJsonValueObject>(Statement);
}

TSharedRef<FJsonValue> RobustGraphUnary(
    const FString& Kind,
    const TSharedRef<FJsonObject>& Target)
{
    TSharedRef<FJsonObject> Statement = MakeShared<FJsonObject>();
    Statement->SetStringField(TEXT("kind"), Kind);
    Statement->SetObjectField(TEXT("target"), Target);
    return MakeShared<FJsonValueObject>(Statement);
}

TSharedRef<FJsonValue> RobustGraphEdgeOperation(
    const FString& Kind,
    const TSharedRef<FJsonObject>& From,
    const TSharedRef<FJsonObject>& To)
{
    TSharedRef<FJsonObject> Statement = MakeShared<FJsonObject>();
    Statement->SetStringField(TEXT("kind"), Kind);
    Statement->SetObjectField(TEXT("from"), From);
    Statement->SetObjectField(TEXT("to"), To);
    return MakeShared<FJsonValueObject>(Statement);
}

TSharedRef<FJsonValue> RobustGraphSet(
    const TSharedRef<FJsonObject>& Target,
    const TSharedPtr<FJsonValue>& Value)
{
    TSharedRef<FJsonObject> Statement = MakeShared<FJsonObject>();
    Statement->SetStringField(TEXT("kind"), TEXT("set"));
    Statement->SetObjectField(TEXT("target"), Target);
    Statement->SetField(TEXT("value"), Value);
    return MakeShared<FJsonValueObject>(Statement);
}

TSharedRef<FJsonValue> RobustGraphMove(
    const TSharedRef<FJsonObject>& Target,
    const FIntPoint Delta)
{
    TSharedRef<FJsonObject> Statement = MakeShared<FJsonObject>();
    Statement->SetStringField(TEXT("kind"), TEXT("move"));
    Statement->SetObjectField(TEXT("target"), Target);
    Statement->SetArrayField(
        TEXT("by"),
        {
            MakeShared<FJsonValueNumber>(Delta.X),
            MakeShared<FJsonValueNumber>(Delta.Y)
        });
    return MakeShared<FJsonValueObject>(Statement);
}

TSharedRef<FJsonValue> RobustGraphInsert(
    const TSharedRef<FJsonObject>& From,
    const TSharedRef<FJsonObject>& Input,
    const TSharedRef<FJsonObject>& Output,
    const TSharedRef<FJsonObject>& To)
{
    TSharedRef<FJsonObject> Statement = MakeShared<FJsonObject>();
    Statement->SetStringField(TEXT("kind"), TEXT("insert"));
    Statement->SetObjectField(TEXT("from"), From);
    Statement->SetObjectField(TEXT("input"), Input);
    Statement->SetObjectField(TEXT("output"), Output);
    Statement->SetObjectField(TEXT("to"), To);
    return MakeShared<FJsonValueObject>(Statement);
}

TSharedRef<FJsonValue> RobustGraphInvoke(
    const TSharedRef<FJsonObject>& Target,
    const FString& Operation,
    const TSharedPtr<FJsonObject>& Args = nullptr)
{
    TSharedRef<FJsonObject> Statement = MakeShared<FJsonObject>();
    Statement->SetStringField(TEXT("kind"), TEXT("invoke"));
    Statement->SetObjectField(TEXT("target"), Target);
    Statement->SetStringField(TEXT("operation"), Operation);
    if (Args.IsValid())
    {
        Statement->SetObjectField(TEXT("args"), Args);
    }
    return MakeShared<FJsonValueObject>(Statement);
}

FString RobustGraphPinTypeText(const FName Category)
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

FSalResolvedTarget RobustGraphTarget(
    UBlueprint* Blueprint,
    UEdGraph* Graph)
{
    FSalResolvedTarget Target;
    Target.Kind = ESalTargetKind::Graph;
    Target.Alias = TEXT("graph");
    Target.AssetPath =
        Blueprint != nullptr ? Blueprint->GetPathName() : FString();
    Target.Id =
        Graph != nullptr
            ? RobustGraphGuidText(Graph->GraphGuid)
            : FString();
    Target.Name = Graph != nullptr ? Graph->GetName() : FString();
    Target.Object = Graph;
    Target.Package =
        Blueprint != nullptr ? Blueprint->GetOutermost() : nullptr;
    Target.Blueprint = Blueprint;
    Target.Class =
        Blueprint != nullptr ? Blueprint->GeneratedClass.Get() : nullptr;
    Target.Graph = Graph;
    Target.Interfaces = {FName(TEXT("graph"))};
    return Target;
}

FSalResolvedTarget RobustGraphBlueprintTarget(UBlueprint* Blueprint)
{
    FSalResolvedTarget Target;
    Target.Kind = ESalTargetKind::Blueprint;
    Target.Alias = TEXT("blueprint");
    Target.AssetPath =
        Blueprint != nullptr ? Blueprint->GetPathName() : FString();
    Target.Id =
        Blueprint != nullptr
            ? RobustGraphGuidText(Blueprint->GetBlueprintGuid())
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

FSalQuery RobustGraphQuery(const FString& Kind)
{
    FSalQuery Query;
    Query.Alias = TEXT("graph");
    Query.Operation = MakeShared<FJsonObject>();
    Query.Operation->SetStringField(TEXT("kind"), Kind);
    return Query;
}

FSalQuery RobustGraphTraversal(
    const FString& Kind,
    const FString& Direction,
    const TSharedRef<FJsonObject>& Target,
    const int32 Depth)
{
    FSalQuery Query = RobustGraphQuery(Kind);
    Query.Operation->SetObjectField(TEXT("target"), Target);
    if (!Direction.IsEmpty())
    {
        Query.Operation->SetStringField(
            TEXT("direction"),
            Direction);
    }
    Query.Operation->SetNumberField(TEXT("depth"), Depth);
    return Query;
}

UEdGraphPin* RobustGraphFindPin(
    UEdGraphNode* Node,
    const FName Name,
    const EEdGraphPinDirection Direction)
{
    return Node != nullptr ? Node->FindPin(Name, Direction) : nullptr;
}

UK2Node_CustomEvent* RobustGraphAddEvent(
    UEdGraph* Graph,
    const FName Name,
    const FIntPoint Position)
{
    if (Graph == nullptr)
    {
        return nullptr;
    }
    UK2Node_CustomEvent* Node =
        NewObject<UK2Node_CustomEvent>(
            Graph,
            NAME_None,
            RF_Transactional);
    Node->CustomFunctionName = Name;
    Node->CreateNewGuid();
    Node->NodePosX = Position.X;
    Node->NodePosY = Position.Y;
    Graph->AddNode(Node, false, false);
    Node->AllocateDefaultPins();
    return Node;
}

UK2Node_IfThenElse* RobustGraphAddBranch(
    UEdGraph* Graph,
    const FIntPoint Position)
{
    if (Graph == nullptr)
    {
        return nullptr;
    }
    UK2Node_IfThenElse* Node =
        NewObject<UK2Node_IfThenElse>(
            Graph,
            NAME_None,
            RF_Transactional);
    Node->CreateNewGuid();
    Node->NodePosX = Position.X;
    Node->NodePosY = Position.Y;
    Graph->AddNode(Node, false, false);
    Node->AllocateDefaultPins();
    return Node;
}

UK2Node_CallFunction* RobustGraphAddNot(
    UEdGraph* Graph,
    const FIntPoint Position)
{
    UFunction* Function =
        UKismetMathLibrary::StaticClass()->FindFunctionByName(
            GET_FUNCTION_NAME_CHECKED(
                UKismetMathLibrary,
                Not_PreBool));
    if (Graph == nullptr || Function == nullptr)
    {
        return nullptr;
    }
    UK2Node_CallFunction* Node =
        NewObject<UK2Node_CallFunction>(
            Graph,
            NAME_None,
            RF_Transactional);
    Node->CreateNewGuid();
    Node->NodePosX = Position.X;
    Node->NodePosY = Position.Y;
    Graph->AddNode(Node, false, false);
    Node->SetFromFunction(Function);
    Node->AllocateDefaultPins();
    return Node;
}

bool RobustGraphUnloadPackage(
    UPackage* Package,
    FString& OutError)
{
    OutError.Reset();
    if (Package == nullptr)
    {
        return true;
    }

    Package->SetDirtyFlag(false);
    TArray<UPackage*> Packages = {Package};
    UPackageTools::FUnloadPackageParams Params(Packages);
    Params.bUnloadDirtyPackages = true;
    // Fixture mutations use a disposable transactor that has already been
    // restored. Preserve the Editor's real Undo/Redo history while still
    // following UE's native Blueprint/package unload path.
    Params.bResetTransBuffer = false;
    const bool bUnloaded = UPackageTools::UnloadPackages(Params);
    if (!bUnloaded)
    {
        OutError = Params.OutErrorMessage.IsEmpty()
            ? TEXT("UE package unload did not unload the Graph fixture.")
            : Params.OutErrorMessage.ToString();
    }
    return bUnloaded;
}

class FRobustGraphFixture
{
public:
    explicit FRobustGraphFixture(const bool bPersistent = false)
        : bPersistentFixture(bPersistent)
    {
        const FString Token =
            FGuid::NewGuid().ToString(EGuidFormats::Digits);
        const FString AssetName = bPersistent
            ? TEXT("BP_GraphTopology")
            : FString::Printf(TEXT("BP_RobustGraph_%s"), *Token);
        PackageName = bPersistent
            ? FString::Printf(
                TEXT("/Game/LoomleTests/GraphPersistence/%s/%s"),
                *Token,
                *AssetName)
            : FString::Printf(
                TEXT("/Game/LoomleTests/RobustGraph_%s"),
                *Token);
        ObjectPath = PackageName + TEXT(".") + AssetName;
        if (bPersistent)
        {
            Filename = FPackageName::LongPackageNameToFilename(
                PackageName,
                FPackageName::GetAssetPackageExtension());
            IFileManager::Get().MakeDirectory(
                *FPaths::GetPath(Filename),
                true);
        }

        Package = CreatePackage(*PackageName);
        Blueprint = Package != nullptr
            ? FKismetEditorUtilities::CreateBlueprint(
                AActor::StaticClass(),
                Package,
                FName(*AssetName),
                BPTYPE_Normal,
                UBlueprint::StaticClass(),
                UBlueprintGeneratedClass::StaticClass(),
                NAME_None)
            : nullptr;
        if (Blueprint == nullptr)
        {
            return;
        }
        if (bPersistent)
        {
            FAssetRegistryModule::AssetCreated(Blueprint);
            bRegistered = true;
        }

        Graph = FBlueprintEditorUtils::FindEventGraph(Blueprint);
        Entry = RobustGraphAddEvent(
            Graph,
            TEXT("RobustEntry"),
            FIntPoint(0, 0));
        LooseEntry = RobustGraphAddEvent(
            Graph,
            TEXT("RobustLooseEntry"),
            FIntPoint(0, 500));
        BranchA = RobustGraphAddBranch(
            Graph,
            FIntPoint(350, 0));
        BranchB = RobustGraphAddBranch(
            Graph,
            FIntPoint(700, -150));
        BranchC = RobustGraphAddBranch(
            Graph,
            FIntPoint(700, 200));
        NotNode = RobustGraphAddNot(
            Graph,
            FIntPoint(50, -300));

        const UEdGraphSchema_K2* Schema =
            GetDefault<UEdGraphSchema_K2>();
        if (Schema != nullptr
            && Entry != nullptr
            && BranchA != nullptr
            && BranchB != nullptr
            && BranchC != nullptr
            && NotNode != nullptr)
        {
            EntryThen = Entry->GetThenPin();
            LooseThen = LooseEntry != nullptr
                ? LooseEntry->GetThenPin()
                : nullptr;
            BranchAExec = BranchA->GetExecPin();
            BranchAThen = BranchA->GetThenPin();
            BranchAElse = BranchA->GetElsePin();
            BranchACondition = BranchA->GetConditionPin();
            BranchBExec = BranchB->GetExecPin();
            BranchBCondition = BranchB->GetConditionPin();
            BranchCExec = BranchC->GetExecPin();
            NotOutput = RobustGraphFindPin(
                NotNode,
                UEdGraphSchema_K2::PN_ReturnValue,
                EGPD_Output);
            bTopologyCreated =
                Schema->TryCreateConnection(
                    EntryThen,
                    BranchAExec)
                && Schema->TryCreateConnection(
                    BranchAThen,
                    BranchBExec)
                && Schema->TryCreateConnection(
                    BranchAElse,
                    BranchCExec)
                && Schema->TryCreateConnection(
                    NotOutput,
                    BranchACondition)
                && Schema->TryCreateConnection(
                    NotOutput,
                    BranchBCondition);
        }
        FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(
            Blueprint);
        // A persistent fixture has authored unsaved topology that the terminal
        // save operation must actually write. Transient fixtures stay clean so
        // their ordinary lifecycle assertions are isolated from package state.
        Package->SetDirtyFlag(bPersistentFixture);
    }

    ~FRobustGraphFixture()
    {
        FString Ignored;
        Cleanup(Ignored);
    }

    FRobustGraphFixture(const FRobustGraphFixture&) = delete;
    FRobustGraphFixture& operator=(const FRobustGraphFixture&) = delete;

    bool IsValid() const
    {
        return Package != nullptr
            && Blueprint != nullptr
            && Graph != nullptr
            && Entry != nullptr
            && LooseEntry != nullptr
            && BranchA != nullptr
            && BranchB != nullptr
            && BranchC != nullptr
            && NotNode != nullptr
            && EntryThen != nullptr
            && LooseThen != nullptr
            && BranchAExec != nullptr
            && BranchAThen != nullptr
            && BranchAElse != nullptr
            && BranchACondition != nullptr
            && BranchBExec != nullptr
            && BranchBCondition != nullptr
            && BranchCExec != nullptr
            && NotOutput != nullptr
            && bTopologyCreated;
    }

    bool Unload(FString& OutError)
    {
        if (!bPersistentFixture)
        {
            OutError = TEXT("Only persistent Graph fixture can unload and reload.");
            return false;
        }
        UPackage* PackageToUnload = Package;
        ClearObjectPointers();
        if (!RobustGraphUnloadPackage(PackageToUnload, OutError))
        {
            return false;
        }
        if (FindPackage(nullptr, *PackageName) != nullptr
            || FindObject<UObject>(nullptr, *ObjectPath) != nullptr)
        {
            OutError = TEXT("Persistent Graph fixture remained loaded.");
            return false;
        }
        OutError.Reset();
        return true;
    }

    bool Reload(FString& OutError)
    {
        Blueprint = LoadObject<UBlueprint>(nullptr, *ObjectPath);
        Package =
            Blueprint != nullptr ? Blueprint->GetOutermost() : nullptr;
        if (Blueprint == nullptr || Package == nullptr)
        {
            OutError = TEXT("Persistent Graph fixture did not reload.");
            return false;
        }
        OutError.Reset();
        return true;
    }

    bool Cleanup(FString& OutError)
    {
        if (bCleaned)
        {
            return OutError.IsEmpty();
        }
        bCleaned = true;
        UObject* Loaded =
            !ObjectPath.IsEmpty()
                ? FindObject<UObject>(nullptr, *ObjectPath)
                : nullptr;
        if (Loaded != nullptr && bRegistered)
        {
            FAssetRegistryModule::AssetDeleted(Loaded);
            bRegistered = false;
        }
        UPackage* LoadedPackage =
            !PackageName.IsEmpty()
                ? FindPackage(nullptr, *PackageName)
                : nullptr;
        ClearObjectPointers();
        FString UnloadError;
        if (!RobustGraphUnloadPackage(LoadedPackage, UnloadError))
        {
            OutError = UnloadError;
        }
        if (!PackageName.IsEmpty()
            && FindPackage(nullptr, *PackageName) != nullptr)
        {
            if (!OutError.IsEmpty())
            {
                OutError += TEXT(" ");
            }
            OutError +=
                TEXT("Robust Graph fixture package remained loaded during cleanup.");
        }
        if (!Filename.IsEmpty()
            && IFileManager::Get().FileExists(*Filename)
            && !IFileManager::Get().Delete(
                *Filename,
                false,
                true,
                true))
        {
            if (!OutError.IsEmpty())
            {
                OutError += TEXT(" ");
            }
            OutError +=
                TEXT("Persistent Graph fixture file could not be deleted.");
        }
        if (!Filename.IsEmpty())
        {
            IFileManager::Get().DeleteDirectory(
                *FPaths::GetPath(Filename),
                false,
                true);
        }
        return OutError.IsEmpty();
    }

    UEdGraph* FindGraphByGuid(const FGuid& Id) const
    {
        if (Blueprint == nullptr)
        {
            return nullptr;
        }
        TArray<UEdGraph*> Graphs;
        Blueprint->GetAllGraphs(Graphs);
        for (UEdGraph* Candidate : Graphs)
        {
            if (Candidate != nullptr
                && Candidate->GraphGuid == Id)
            {
                return Candidate;
            }
        }
        return nullptr;
    }

    static UEdGraphNode* FindNodeByGuid(
        UEdGraph* InGraph,
        const FGuid& Id)
    {
        if (InGraph == nullptr)
        {
            return nullptr;
        }
        for (UEdGraphNode* Candidate : InGraph->Nodes)
        {
            if (Candidate != nullptr
                && Candidate->NodeGuid == Id)
            {
                return Candidate;
            }
        }
        return nullptr;
    }

    static UEdGraphPin* FindPinByGuid(
        UEdGraph* InGraph,
        const FGuid& Id)
    {
        if (InGraph == nullptr)
        {
            return nullptr;
        }
        for (UEdGraphNode* Node : InGraph->Nodes)
        {
            if (Node == nullptr)
            {
                continue;
            }
            for (UEdGraphPin* Pin : Node->Pins)
            {
                if (Pin != nullptr && Pin->PinId == Id)
                {
                    return Pin;
                }
            }
        }
        return nullptr;
    }

    UPackage* Package = nullptr;
    UBlueprint* Blueprint = nullptr;
    UEdGraph* Graph = nullptr;
    UK2Node_CustomEvent* Entry = nullptr;
    UK2Node_CustomEvent* LooseEntry = nullptr;
    UK2Node_IfThenElse* BranchA = nullptr;
    UK2Node_IfThenElse* BranchB = nullptr;
    UK2Node_IfThenElse* BranchC = nullptr;
    UK2Node_CallFunction* NotNode = nullptr;
    UEdGraphPin* EntryThen = nullptr;
    UEdGraphPin* LooseThen = nullptr;
    UEdGraphPin* BranchAExec = nullptr;
    UEdGraphPin* BranchAThen = nullptr;
    UEdGraphPin* BranchAElse = nullptr;
    UEdGraphPin* BranchACondition = nullptr;
    UEdGraphPin* BranchBExec = nullptr;
    UEdGraphPin* BranchBCondition = nullptr;
    UEdGraphPin* BranchCExec = nullptr;
    UEdGraphPin* NotOutput = nullptr;
    FString PackageName;
    FString ObjectPath;
    FString Filename;

private:
    void ClearObjectPointers()
    {
        EntryThen = nullptr;
        LooseThen = nullptr;
        BranchAExec = nullptr;
        BranchAThen = nullptr;
        BranchAElse = nullptr;
        BranchACondition = nullptr;
        BranchBExec = nullptr;
        BranchBCondition = nullptr;
        BranchCExec = nullptr;
        NotOutput = nullptr;
        Entry = nullptr;
        LooseEntry = nullptr;
        BranchA = nullptr;
        BranchB = nullptr;
        BranchC = nullptr;
        NotNode = nullptr;
        Graph = nullptr;
        Blueprint = nullptr;
        Package = nullptr;
    }

    bool bTopologyCreated = false;
    bool bPersistentFixture = false;
    bool bRegistered = false;
    bool bCleaned = false;
};

FString RobustGraphFindPaletteId(
    const TSharedPtr<FJsonObject>& Result,
    const FString& ExpectedType)
{
    for (const TSharedPtr<FJsonObject>& Args :
         RobustGraphCallArgs(Result, TEXT("node")))
    {
        FString Type;
        FString Palette;
        if (Args.IsValid()
            && Args->TryGetStringField(TEXT("type"), Type)
            && Type == ExpectedType
            && Args->TryGetStringField(TEXT("palette"), Palette))
        {
            return Palette;
        }
    }
    return FString();
}

FString RobustGraphResolvedRef(
    const TSharedPtr<FJsonObject>& Result,
    const FString& Alias)
{
    const TSharedPtr<FJsonObject>* Resolved = nullptr;
    FString Id;
    if (Result.IsValid()
        && Result->TryGetObjectField(TEXT("resolvedRefs"), Resolved)
        && Resolved != nullptr)
    {
        (*Resolved)->TryGetStringField(Alias, Id);
    }
    return Id;
}

FSalPatch RobustGraphTerminalPatch()
{
    TSharedRef<FJsonObject> Compile = MakeShared<FJsonObject>();
    Compile->SetStringField(TEXT("kind"), TEXT("compile"));
    TSharedRef<FJsonObject> Save = MakeShared<FJsonObject>();
    Save->SetStringField(TEXT("kind"), TEXT("save"));
    FSalPatch Patch;
    Patch.Alias = TEXT("blueprint");
    Patch.Statements = {
        MakeShared<FJsonValueObject>(Compile),
        MakeShared<FJsonValueObject>(Save)};
    return Patch;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FSalRobustGraphPinIdentityScopeTest,
    "Loomle.Sal.Robustness.Graph.PinIdentityScope",
    EAutomationTestFlags::EditorContext
        | EAutomationTestFlags::EngineFilter)

bool FSalRobustGraphPinIdentityScopeTest::RunTest(
    const FString& Parameters)
{
    if (!RobustGraphRequireIdleEditor(
            *this,
            TEXT("Graph Pin identity scope coverage")))
    {
        return false;
    }
    Loomle::Tests::FScopedIsolatedTransactor Transactions;
    if (!TestTrue(
            TEXT("Pin identity scope isolates transaction history"),
            Transactions.Initialize()))
    {
        return false;
    }
    FRobustGraphFixture Fixture;
    if (!TestTrue(TEXT("Pin identity scope fixture is valid"), Fixture.IsValid()))
    {
        Transactions.Restore();
        return false;
    }

    UEdGraph* OtherGraph = FBlueprintEditorUtils::CreateNewGraph(
        Fixture.Blueprint,
        TEXT("PinIdentityOtherGraph"),
        UEdGraph::StaticClass(),
        UEdGraphSchema_K2::StaticClass());
    if (OtherGraph != nullptr)
    {
        FBlueprintEditorUtils::AddFunctionGraph(
            Fixture.Blueprint,
            OtherGraph,
            true,
            static_cast<UClass*>(nullptr));
    }
    UK2Node_IfThenElse* OtherBranch =
        RobustGraphAddBranch(OtherGraph, FIntPoint(0, 0));
    UEdGraphPin* OtherExec =
        OtherBranch != nullptr ? OtherBranch->GetExecPin() : nullptr;
    if (!TestNotNull(TEXT("Other Graph duplicate Pin fixture exists"), OtherExec))
    {
        Transactions.Restore();
        return false;
    }
    OtherExec->PinId = Fixture.BranchAExec->PinId;

    FSalQuery ExactPin = RobustGraphQuery(TEXT("pin"));
    ExactPin.Operation->SetStringField(
        TEXT("id"),
        RobustGraphGuidText(Fixture.BranchAExec->PinId));
    const FSalResolvedTarget Target =
        RobustGraphTarget(Fixture.Blueprint, Fixture.Graph);
    const TSharedPtr<FJsonObject> CrossGraphRead =
        FSalGraphInterface::Query(ExactPin, Target);
    TestFalse(
        TEXT("Same PinId in another Graph does not affect exact read"),
        RobustGraphHasError(CrossGraphRead));

    TStrongObjectPtr<UBlueprint> SandboxOwner;
    FSalResolvedTarget SandboxTarget;
    FString SandboxError;
    TestTrue(
        *FString::Printf(
            TEXT("Sandbox accepts PinId reuse across Graphs [%s]"),
            *SandboxError),
        FSalGraphInterface::BuildSandboxTargetForTesting(
            Target,
            SandboxOwner,
            SandboxTarget,
            SandboxError));

    Fixture.BranchBExec->PinId = Fixture.BranchAExec->PinId;
    const TSharedPtr<FJsonObject> SameGraphRead =
        FSalGraphInterface::Query(ExactPin, Target);
    TestTrue(
        TEXT("Same PinId on another Node in the bound Graph is ambiguous"),
        RobustGraphHasDiagnosticCode(
            SameGraphRead,
            TEXT("resolution.pin_ambiguous")));

    FSalQuery Context = RobustGraphTraversal(
        TEXT("context"),
        FString(),
        RobustGraphTyped(
            TEXT("pin"),
            Fixture.BranchAExec->PinId),
        1);
    const TSharedPtr<FJsonObject> AmbiguousTraversal =
        FSalGraphInterface::Query(Context, Target);
    TestTrue(
        TEXT("Traversal reports ambiguous PinId"),
        RobustGraphHasDiagnosticCode(
            AmbiguousTraversal,
            TEXT("resolution.pin_ambiguous")));

    FSalPatch AmbiguousPinPatch;
    AmbiguousPinPatch.Alias = TEXT("graph");
    AmbiguousPinPatch.bDryRun = true;
    AmbiguousPinPatch.Statements = {
        RobustGraphUnary(
            TEXT("break"),
            RobustGraphTyped(
                TEXT("pin"),
                Fixture.BranchAExec->PinId))};
    const TSharedPtr<FJsonObject> AmbiguousMutation =
        FSalGraphInterface::Patch(AmbiguousPinPatch, Target);
    TestTrue(
        TEXT("Pin mutation reports ambiguous PinId"),
        RobustGraphHasDiagnosticCode(
            AmbiguousMutation,
            TEXT("resolution.pin_ambiguous")));

    FSalPatch AmbiguousPinFieldPatch;
    AmbiguousPinFieldPatch.Alias = TEXT("graph");
    AmbiguousPinFieldPatch.bDryRun = true;
    AmbiguousPinFieldPatch.Statements = {
        RobustGraphSet(
            RobustGraphMember(
                RobustGraphTyped(
                    TEXT("pin"),
                    Fixture.BranchAExec->PinId),
                TEXT("DefaultValue")),
            MakeShared<FJsonValueString>(TEXT("unused")))};
    const TSharedPtr<FJsonObject> AmbiguousFieldMutation =
        FSalGraphInterface::Patch(AmbiguousPinFieldPatch, Target);
    TestTrue(
        TEXT("Pin field mutation reports ambiguous PinId"),
        RobustGraphHasDiagnosticCode(
            AmbiguousFieldMutation,
            TEXT("resolution.pin_ambiguous")));

    FSalPatch UnrelatedNodePatch;
    UnrelatedNodePatch.Alias = TEXT("graph");
    UnrelatedNodePatch.bDryRun = true;
    UnrelatedNodePatch.Statements = {
        RobustGraphMove(
            RobustGraphTyped(
                TEXT("node"),
                Fixture.BranchC->NodeGuid),
            FIntPoint(16, 0))};
    const TSharedPtr<FJsonObject> UnrelatedMutation =
        FSalGraphInterface::Patch(UnrelatedNodePatch, Target);
    TestTrue(
        *FString::Printf(
            TEXT("Unrelated Node mutation survives duplicate PinIds [%s]"),
            *RobustGraphDiagnosticsText(UnrelatedMutation)),
        RobustGraphResultBool(UnrelatedMutation, TEXT("valid")));

    Transactions.Restore();
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FSalRobustGraphTraversalPaletteTest,
    "Loomle.Sal.Robustness.Graph.TraversalPalette",
    EAutomationTestFlags::EditorContext
        | EAutomationTestFlags::EngineFilter)

bool FSalRobustGraphTraversalPaletteTest::RunTest(
    const FString& Parameters)
{
    if (!RobustGraphRequireIdleEditor(
            *this,
            TEXT("Graph traversal and Palette coverage")))
    {
        return false;
    }
    FRobustGraphFixture Fixture;
    if (!TestTrue(TEXT("Graph traversal fixture is valid"), Fixture.IsValid()))
    {
        return false;
    }
    const FSalResolvedTarget Target =
        RobustGraphTarget(Fixture.Blueprint, Fixture.Graph);

    const TSharedPtr<FJsonObject> ExecDepthOne =
        FSalGraphInterface::Query(
            RobustGraphTraversal(
                TEXT("exec_flow"),
                TEXT("from"),
                RobustGraphTyped(
                    TEXT("node"),
                    Fixture.Entry->NodeGuid),
                1),
            Target);
    TestFalse(
        TEXT("Exec flow depth 1 succeeds"),
        RobustGraphHasError(ExecDepthOne));
    TestTrue(
        TEXT("Exec flow depth 1 contains entry"),
        RobustGraphContainsCallId(
            ExecDepthOne,
            TEXT("node"),
            Fixture.Entry->NodeGuid));
    TestTrue(
        TEXT("Exec flow depth 1 contains immediate Branch"),
        RobustGraphContainsCallId(
            ExecDepthOne,
            TEXT("node"),
            Fixture.BranchA->NodeGuid));
    TestFalse(
        TEXT("Exec flow depth 1 does not over-expand"),
        RobustGraphContainsCallId(
            ExecDepthOne,
            TEXT("node"),
            Fixture.BranchB->NodeGuid));
    TestEqual(
        TEXT("Exec flow depth 1 emits one complete Edge"),
        RobustGraphEdgeCount(ExecDepthOne),
        1);

    const TSharedPtr<FJsonObject> ExecDepthTwo =
        FSalGraphInterface::Query(
            RobustGraphTraversal(
                TEXT("exec_flow"),
                TEXT("from"),
                RobustGraphTyped(
                    TEXT("node"),
                    Fixture.Entry->NodeGuid),
                2),
            Target);
    TestFalse(
        TEXT("Exec flow depth 2 succeeds"),
        RobustGraphHasError(ExecDepthTwo));
    TestTrue(
        TEXT("Exec depth 2 follows Then branch"),
        RobustGraphContainsCallId(
            ExecDepthTwo,
            TEXT("node"),
            Fixture.BranchB->NodeGuid));
    TestTrue(
        TEXT("Exec depth 2 follows Else branch"),
        RobustGraphContainsCallId(
            ExecDepthTwo,
            TEXT("node"),
            Fixture.BranchC->NodeGuid));
    TestEqual(
        TEXT("Exec depth 2 emits three execution Edges"),
        RobustGraphEdgeCount(ExecDepthTwo),
        3);

    const TSharedPtr<FJsonObject> DataFlow =
        FSalGraphInterface::Query(
            RobustGraphTraversal(
                TEXT("data_flow"),
                TEXT("from"),
                RobustGraphTyped(
                    TEXT("node"),
                    Fixture.NotNode->NodeGuid),
                1),
            Target);
    TestFalse(
        TEXT("Data flow succeeds"),
        RobustGraphHasError(DataFlow));
    TestTrue(
        TEXT("Data flow reaches first Boolean consumer"),
        RobustGraphContainsCallId(
            DataFlow,
            TEXT("node"),
            Fixture.BranchA->NodeGuid));
    TestTrue(
        TEXT("Data flow reaches second Boolean consumer"),
        RobustGraphContainsCallId(
            DataFlow,
            TEXT("node"),
            Fixture.BranchB->NodeGuid));
    TestEqual(
        TEXT("Data flow emits both data Edges"),
        RobustGraphEdgeCount(DataFlow),
        2);

    const TSharedPtr<FJsonObject> Context =
        FSalGraphInterface::Query(
            RobustGraphTraversal(
                TEXT("context"),
                FString(),
                RobustGraphTyped(
                    TEXT("node"),
                    Fixture.BranchA->NodeGuid),
                1),
            Target);
    TestFalse(
        TEXT("Context traversal succeeds"),
        RobustGraphHasError(Context));
    TestTrue(
        TEXT("Context includes upstream execution owner"),
        RobustGraphContainsCallId(
            Context,
            TEXT("node"),
            Fixture.Entry->NodeGuid));
    TestTrue(
        TEXT("Context includes upstream data owner"),
        RobustGraphContainsCallId(
            Context,
            TEXT("node"),
            Fixture.NotNode->NodeGuid));

    FSalQuery Palette = RobustGraphQuery(TEXT("palette_entries"));
    Palette.Operation->SetStringField(TEXT("text"), TEXT("Branch"));
    Palette.PageLimit = 1;
    const TSharedPtr<FJsonObject> PaletteResult =
        FSalGraphInterface::Query(Palette, Target);
    TestFalse(
        TEXT("Graph Palette search succeeds"),
        RobustGraphHasError(PaletteResult));
    const FString BranchPalette = RobustGraphFindPaletteId(
        PaletteResult,
        TEXT("/Script/BlueprintGraph.K2Node_IfThenElse"));
    TestFalse(
        TEXT("Graph Palette returns exact Branch constructor"),
        BranchPalette.IsEmpty());

    FSalQuery ExactPalette = RobustGraphQuery(TEXT("palette"));
    ExactPalette.Operation->SetStringField(TEXT("id"), BranchPalette);
    ExactPalette.With.Add(TEXT("schema"));
    const TSharedPtr<FJsonObject> ExactPaletteResult =
        FSalGraphInterface::Query(ExactPalette, Target);
    TestFalse(
        TEXT("Exact Graph Palette entry with schema succeeds"),
        RobustGraphHasError(ExactPaletteResult));
    TestTrue(
        TEXT("Exact Branch Palette exposes its future Pins"),
        RobustGraphCallArgs(
            ExactPaletteResult,
            TEXT("pin")).Num() >= 4);
    TestTrue(
        TEXT("Exact Branch Palette exposes constructor schema"),
        RobustGraphHasComment(
            ExactPaletteResult,
            TEXT("schema:")));

    Palette.PageAfter = TEXT("graph1:not-this-query:1");
    TestTrue(
        TEXT("Invalid Graph Palette cursor fails closed"),
        RobustGraphHasDiagnosticCode(
            FSalGraphInterface::Query(Palette, Target),
            TEXT("validation.invalid_cursor")));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FSalRobustGraphNodeLifecycleTest,
    "Loomle.Sal.Robustness.Graph.NodeLifecycle",
    EAutomationTestFlags::EditorContext
        | EAutomationTestFlags::EngineFilter)

bool FSalRobustGraphNodeLifecycleTest::RunTest(
    const FString& Parameters)
{
    if (!RobustGraphRequireIdleEditor(
            *this,
            TEXT("Graph Node lifecycle coverage")))
    {
        return false;
    }
    Loomle::Tests::FScopedIsolatedTransactor Transactions;
    if (!TestTrue(
            TEXT("Node lifecycle isolates Undo history"),
            Transactions.Initialize()))
    {
        return false;
    }
    FRobustGraphFixture Fixture;
    if (!TestTrue(TEXT("Node lifecycle fixture is valid"), Fixture.IsValid()))
    {
        Transactions.Restore();
        return false;
    }
    const FSalResolvedTarget Target =
        RobustGraphTarget(Fixture.Blueprint, Fixture.Graph);
    FSalQuery Palette = RobustGraphQuery(TEXT("palette_entries"));
    Palette.Operation->SetStringField(TEXT("text"), TEXT("Branch"));
    const FString BranchPalette = RobustGraphFindPaletteId(
        FSalGraphInterface::Query(Palette, Target),
        TEXT("/Script/BlueprintGraph.K2Node_IfThenElse"));
    if (!TestFalse(TEXT("Node lifecycle discovers Branch Palette"), BranchPalette.IsEmpty()))
    {
        Transactions.Restore();
        return false;
    }

    FSalPatch Patch;
    Patch.Alias = TEXT("graph");
    Patch.bDryRun = true;
    Patch.Statements = {
        RobustGraphBinding(
            TEXT("CreatedBranch"),
            BranchPalette,
            TEXT("/Script/BlueprintGraph.K2Node_IfThenElse")),
        RobustGraphUnary(
            TEXT("add"),
            RobustGraphLocal(TEXT("CreatedBranch"))),
        RobustGraphEdgeOperation(
            TEXT("connect"),
            RobustGraphTyped(
                TEXT("pin"),
                Fixture.LooseThen->PinId),
            RobustGraphMember(
                RobustGraphLocal(TEXT("CreatedBranch")),
                TEXT("execute"))),
        RobustGraphSet(
            RobustGraphMember(
                RobustGraphLocal(TEXT("CreatedBranch")),
                TEXT("NodeComment")),
            MakeShared<FJsonValueString>(
                TEXT("Created through robust SAL lifecycle"))),
        RobustGraphMove(
            RobustGraphLocal(TEXT("CreatedBranch")),
            FIntPoint(256, 128))
    };
    const int32 OriginalNodeCount = Fixture.Graph->Nodes.Num();
    const TSharedPtr<FJsonObject> DryRun =
        FSalGraphInterface::Patch(Patch, Target);
    const bool bDryRunValid =
        RobustGraphResultBool(DryRun, TEXT("valid"));
    TestTrue(
        *FString::Printf(
            TEXT("Node add/connect/set/move dry run validates [%s]"),
            *RobustGraphDiagnosticsText(DryRun)),
        bDryRunValid);
    TestFalse(
        TEXT("Node lifecycle dry run does not apply"),
        RobustGraphResultBool(DryRun, TEXT("applied")));
    TestEqual(
        TEXT("Node lifecycle dry run leaves source count unchanged"),
        Fixture.Graph->Nodes.Num(),
        OriginalNodeCount);
    TestTrue(
        TEXT("Node lifecycle dry run leaves source Edge unchanged"),
        Fixture.LooseThen->LinkedTo.IsEmpty());
    if (!bDryRunValid)
    {
        Transactions.Restore();
        return false;
    }

    Patch.bDryRun = false;
    const TSharedPtr<FJsonObject> Applied =
        FSalGraphInterface::Patch(Patch, Target);
    const bool bApplied =
        RobustGraphResultBool(Applied, TEXT("valid"))
        && RobustGraphResultBool(Applied, TEXT("applied"));
    TestTrue(
        *FString::Printf(
            TEXT("Node add/connect/set/move applies [%s]"),
            *RobustGraphDiagnosticsText(Applied)),
        bApplied);
    if (!bApplied)
    {
        Transactions.Restore();
        return false;
    }
    TestEqual(
        TEXT("Live lifecycle adds exactly one Node"),
        Fixture.Graph->Nodes.Num(),
        OriginalNodeCount + 1);
    const FString CreatedId =
        RobustGraphResolvedRef(Applied, TEXT("CreatedBranch"));
    FGuid CreatedGuid;
    TestTrue(
        TEXT("Live lifecycle resolves creation alias to stable Node id"),
        FGuid::Parse(CreatedId, CreatedGuid));
    UEdGraphNode* Created =
        FRobustGraphFixture::FindNodeByGuid(
            Fixture.Graph,
            CreatedGuid);
    TestNotNull(TEXT("Created Node resolves natively"), Created);
    TestTrue(
        TEXT("Created Node is connected to prior source Pin"),
        Created != nullptr
            && Fixture.LooseThen->LinkedTo.Contains(
                Created->FindPin(
                    UEdGraphSchema_K2::PN_Execute,
                    EGPD_Input)));
    TestTrue(
        TEXT("Created Node preserves authored comment"),
        Created != nullptr
            && Created->NodeComment
                == TEXT("Created through robust SAL lifecycle"));

    FSalQuery ExactNode = RobustGraphQuery(TEXT("node"));
    ExactNode.Operation->SetStringField(TEXT("id"), CreatedId);
    ExactNode.With = {TEXT("layout"), TEXT("schema")};
    const TSharedPtr<FJsonObject> Readback =
        FSalGraphInterface::Query(ExactNode, Target);
    TestFalse(
        TEXT("Created Node is queryable immediately"),
        RobustGraphHasError(Readback));
    TestTrue(
        TEXT("Created Node readback includes all Branch Pins"),
        RobustGraphCallArgs(Readback, TEXT("pin")).Num() >= 4);

    FSalPatch Remove;
    Remove.Alias = TEXT("graph");
    Remove.Statements = {
        RobustGraphUnary(
            TEXT("remove"),
            RobustGraphTyped(TEXT("node"), CreatedGuid))};
    const TSharedPtr<FJsonObject> Removed =
        FSalGraphInterface::Patch(Remove, Target);
    TestTrue(
        TEXT("Created Node removal applies"),
        RobustGraphResultBool(Removed, TEXT("valid"))
            && RobustGraphResultBool(Removed, TEXT("applied")));
    TestNull(
        TEXT("Created Node is absent after remove"),
        FRobustGraphFixture::FindNodeByGuid(
            Fixture.Graph,
            CreatedGuid));
    TestTrue(
        TEXT("Remove automatically breaks connected Edge"),
        Fixture.LooseThen->LinkedTo.IsEmpty());

    TestTrue(
        TEXT("Undo restores removed Node and Edge"),
        GEditor->UndoTransaction(false));
    TestNotNull(
        TEXT("Undo restores Node identity"),
        FRobustGraphFixture::FindNodeByGuid(
            Fixture.Graph,
            CreatedGuid));
    TestFalse(
        TEXT("Undo restores source Edge"),
        Fixture.LooseThen->LinkedTo.IsEmpty());

    TestTrue(
        TEXT("Second Undo removes the original lifecycle batch"),
        GEditor->UndoTransaction(false));
    TestNull(
        TEXT("Undo creation removes the Node"),
        FRobustGraphFixture::FindNodeByGuid(
            Fixture.Graph,
            CreatedGuid));
    TestEqual(
        TEXT("Undo creation returns Node count to baseline"),
        Fixture.Graph->Nodes.Num(),
        OriginalNodeCount);
    Transactions.Restore();
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FSalRobustGraphEdgeOperationsTest,
    "Loomle.Sal.Robustness.Graph.EdgeOperationsInsert",
    EAutomationTestFlags::EditorContext
        | EAutomationTestFlags::EngineFilter)

bool FSalRobustGraphEdgeOperationsTest::RunTest(
    const FString& Parameters)
{
    if (!RobustGraphRequireIdleEditor(
            *this,
            TEXT("Graph Edge operation coverage")))
    {
        return false;
    }
    Loomle::Tests::FScopedIsolatedTransactor Transactions;
    if (!TestTrue(
            TEXT("Edge operation coverage isolates Undo history"),
            Transactions.Initialize()))
    {
        return false;
    }
    FRobustGraphFixture Fixture;
    if (!TestTrue(TEXT("Edge operation fixture is valid"), Fixture.IsValid()))
    {
        Transactions.Restore();
        return false;
    }
    const FSalResolvedTarget Target =
        RobustGraphTarget(Fixture.Blueprint, Fixture.Graph);

    FSalPatch Disconnect;
    Disconnect.Alias = TEXT("graph");
    Disconnect.Statements = {
        RobustGraphEdgeOperation(
            TEXT("disconnect"),
            RobustGraphTyped(
                TEXT("pin"),
                Fixture.BranchAThen->PinId),
            RobustGraphTyped(
                TEXT("pin"),
                Fixture.BranchBExec->PinId))};
    const TSharedPtr<FJsonObject> Disconnected =
        FSalGraphInterface::Patch(Disconnect, Target);
    TestTrue(
        TEXT("Exact disconnect applies"),
        RobustGraphResultBool(Disconnected, TEXT("valid"))
            && RobustGraphResultBool(Disconnected, TEXT("applied")));
    TestFalse(
        TEXT("Exact disconnect removes only requested Edge"),
        Fixture.BranchAThen->LinkedTo.Contains(
            Fixture.BranchBExec));
    TestTrue(
        TEXT("Exact disconnect preserves sibling execution Edge"),
        Fixture.BranchAElse->LinkedTo.Contains(
            Fixture.BranchCExec));
    TestTrue(
        TEXT("Undo restores exact disconnected Edge"),
        GEditor->UndoTransaction(false));
    TestTrue(
        TEXT("Disconnected Edge is restored"),
        Fixture.BranchAThen->LinkedTo.Contains(
            Fixture.BranchBExec));

    FSalPatch Break;
    Break.Alias = TEXT("graph");
    Break.Statements = {
        RobustGraphUnary(
            TEXT("break"),
            RobustGraphTyped(
                TEXT("pin"),
                Fixture.NotOutput->PinId))};
    const TSharedPtr<FJsonObject> Broken =
        FSalGraphInterface::Patch(Break, Target);
    TestTrue(
        TEXT("Break All Pin Links applies"),
        RobustGraphResultBool(Broken, TEXT("valid"))
            && RobustGraphResultBool(Broken, TEXT("applied")));
    TestTrue(
        TEXT("Break removes every data Edge on the Pin"),
        Fixture.NotOutput->LinkedTo.IsEmpty());
    TestTrue(
        TEXT("Undo restores all broken data Edges"),
        GEditor->UndoTransaction(false));
    TestEqual(
        TEXT("Both data consumers are restored"),
        Fixture.NotOutput->LinkedTo.Num(),
        2);

    FSalQuery Palette = RobustGraphQuery(TEXT("palette_entries"));
    Palette.Operation->SetStringField(TEXT("text"), TEXT("Sequence"));
    const FString SequencePalette = RobustGraphFindPaletteId(
        FSalGraphInterface::Query(Palette, Target),
        TEXT("/Script/BlueprintGraph.K2Node_ExecutionSequence"));
    if (!TestFalse(
            TEXT("Insert discovers Sequence Palette"),
            SequencePalette.IsEmpty()))
    {
        Transactions.Restore();
        return false;
    }

    FSalPatch Insert;
    Insert.Alias = TEXT("graph");
    Insert.Statements = {
        RobustGraphBinding(
            TEXT("InsertedSequence"),
            SequencePalette,
            TEXT("/Script/BlueprintGraph.K2Node_ExecutionSequence")),
        RobustGraphInsert(
            RobustGraphTyped(
                TEXT("pin"),
                Fixture.BranchAThen->PinId),
            RobustGraphMember(
                RobustGraphLocal(TEXT("InsertedSequence")),
                TEXT("execute")),
            RobustGraphMember(
                RobustGraphLocal(TEXT("InsertedSequence")),
                TEXT("Then_0")),
            RobustGraphTyped(
                TEXT("pin"),
                Fixture.BranchBExec->PinId))
    };
    const int32 OriginalNodeCount = Fixture.Graph->Nodes.Num();
    const TSharedPtr<FJsonObject> Inserted =
        FSalGraphInterface::Patch(Insert, Target);
    TestTrue(
        TEXT("Two-sided insert applies as one native operation"),
        RobustGraphResultBool(Inserted, TEXT("valid"))
            && RobustGraphResultBool(Inserted, TEXT("applied")));
    const FString SequenceId =
        RobustGraphResolvedRef(
            Inserted,
            TEXT("InsertedSequence"));
    FGuid SequenceGuid;
    TestTrue(
        TEXT("Insert resolves the new Node identity"),
        FGuid::Parse(SequenceId, SequenceGuid));
    UEdGraphNode* Sequence =
        FRobustGraphFixture::FindNodeByGuid(
            Fixture.Graph,
            SequenceGuid);
    UEdGraphPin* SequenceExec =
        Sequence != nullptr
            ? Sequence->FindPin(
                UEdGraphSchema_K2::PN_Execute,
                EGPD_Input)
            : nullptr;
    UEdGraphPin* SequenceThen =
        Sequence != nullptr
            ? Sequence->FindPin(TEXT("Then_0"), EGPD_Output)
            : nullptr;
    TestEqual(
        TEXT("Insert adds exactly one Node"),
        Fixture.Graph->Nodes.Num(),
        OriginalNodeCount + 1);
    TestFalse(
        TEXT("Insert removes replaced direct Edge"),
        Fixture.BranchAThen->LinkedTo.Contains(
            Fixture.BranchBExec));
    TestTrue(
        TEXT("Insert connects source to new input"),
        SequenceExec != nullptr
            && Fixture.BranchAThen->LinkedTo.Contains(
                SequenceExec));
    TestTrue(
        TEXT("Insert connects new output to destination"),
        SequenceThen != nullptr
            && SequenceThen->LinkedTo.Contains(
                Fixture.BranchBExec));

    TestTrue(
        TEXT("Undo restores insert atomically"),
        GEditor->UndoTransaction(false));
    TestNull(
        TEXT("Undo removes inserted Node"),
        FRobustGraphFixture::FindNodeByGuid(
            Fixture.Graph,
            SequenceGuid));
    TestTrue(
        TEXT("Undo restores replaced direct Edge"),
        Fixture.BranchAThen->LinkedTo.Contains(
            Fixture.BranchBExec));
    Transactions.Restore();
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FSalRobustGraphDynamicPinTest,
    "Loomle.Sal.Robustness.Graph.DynamicPinLifecycle",
    EAutomationTestFlags::EditorContext
        | EAutomationTestFlags::EngineFilter)

bool FSalRobustGraphDynamicPinTest::RunTest(
    const FString& Parameters)
{
    if (!RobustGraphRequireIdleEditor(
            *this,
            TEXT("Graph dynamic Pin coverage")))
    {
        return false;
    }
    Loomle::Tests::FScopedIsolatedTransactor Transactions;
    if (!TestTrue(
            TEXT("Dynamic Pin coverage isolates Undo history"),
            Transactions.Initialize()))
    {
        return false;
    }
    FRobustGraphFixture Fixture;
    if (!TestTrue(TEXT("Dynamic Pin fixture is valid"), Fixture.IsValid()))
    {
        Transactions.Restore();
        return false;
    }
    const FSalResolvedTarget Target =
        RobustGraphTarget(Fixture.Blueprint, Fixture.Graph);
    TSharedPtr<FJsonObject> Args = MakeShared<FJsonObject>();
    Args->SetStringField(TEXT("name"), TEXT("Payload"));
    Args->SetStringField(
        TEXT("type"),
        RobustGraphPinTypeText(UEdGraphSchema_K2::PC_Int));

    FSalPatch AddParameter;
    AddParameter.Alias = TEXT("graph");
    AddParameter.bDryRun = true;
    AddParameter.Statements = {
        RobustGraphInvoke(
            RobustGraphTyped(
                TEXT("node"),
                Fixture.LooseEntry->NodeGuid),
            TEXT("AddParameter"),
            Args)};
    const int32 OriginalPinCount =
        Fixture.LooseEntry->Pins.Num();
    const TSharedPtr<FJsonObject> DryRun =
        FSalGraphInterface::Patch(AddParameter, Target);
    TestTrue(
        TEXT("Dynamic Pin dry run validates through native reconstruction"),
        RobustGraphResultBool(DryRun, TEXT("valid")));
    TestEqual(
        TEXT("Dynamic Pin dry run leaves source Pins unchanged"),
        Fixture.LooseEntry->Pins.Num(),
        OriginalPinCount);

    AddParameter.bDryRun = false;
    const TSharedPtr<FJsonObject> Applied =
        FSalGraphInterface::Patch(AddParameter, Target);
    TestTrue(
        TEXT("Custom Event AddParameter applies"),
        RobustGraphResultBool(Applied, TEXT("valid"))
            && RobustGraphResultBool(Applied, TEXT("applied")));
    UEdGraphPin* Payload =
        Fixture.LooseEntry->FindPin(TEXT("Payload"), EGPD_Output);
    TestNotNull(
        TEXT("Dynamic Pin exists on native Custom Event"),
        Payload);
    TestEqual(
        TEXT("Dynamic Pin uses requested UE native type"),
        Payload != nullptr
            ? Payload->PinType.PinCategory
            : NAME_None,
        UEdGraphSchema_K2::PC_Int);
    if (Payload == nullptr)
    {
        Transactions.Restore();
        return false;
    }
    const FGuid PayloadId = Payload->PinId;

    FSalQuery ExactPin = RobustGraphQuery(TEXT("pin"));
    ExactPin.Operation->SetStringField(
        TEXT("id"),
        RobustGraphGuidText(PayloadId));
    ExactPin.With.Add(TEXT("schema"));
    const TSharedPtr<FJsonObject> PinSchema =
        FSalGraphInterface::Query(ExactPin, Target);
    TestFalse(
        TEXT("Dynamic Pin exact schema query succeeds"),
        RobustGraphHasError(PinSchema));
    TestTrue(
        TEXT("Dynamic authored Pin advertises RemoveParameter"),
        RobustGraphHasComment(
            PinSchema,
            TEXT("RemoveParameter")));

    FSalPatch RemoveParameter;
    RemoveParameter.Alias = TEXT("graph");
    RemoveParameter.Statements = {
        RobustGraphInvoke(
            RobustGraphTyped(TEXT("pin"), PayloadId),
            TEXT("RemoveParameter"))};
    const TSharedPtr<FJsonObject> Removed =
        FSalGraphInterface::Patch(RemoveParameter, Target);
    TestTrue(
        TEXT("Dynamic Pin RemoveParameter applies"),
        RobustGraphResultBool(Removed, TEXT("valid"))
            && RobustGraphResultBool(Removed, TEXT("applied")));
    TestNull(
        TEXT("Dynamic Pin is removed natively"),
        Fixture.LooseEntry->FindPin(TEXT("Payload"), EGPD_Output));
    TestTrue(
        TEXT("Undo restores removed dynamic Pin"),
        GEditor->UndoTransaction(false));
    TestNotNull(
        TEXT("Undo restores dynamic Pin by name"),
        Fixture.LooseEntry->FindPin(TEXT("Payload"), EGPD_Output));
    TestTrue(
        TEXT("Second Undo removes originally added dynamic Pin"),
        GEditor->UndoTransaction(false));
    TestNull(
        TEXT("Undo addition returns Pin list to baseline"),
        Fixture.LooseEntry->FindPin(TEXT("Payload"), EGPD_Output));
    TestEqual(
        TEXT("Dynamic Pin count returns to baseline"),
        Fixture.LooseEntry->Pins.Num(),
        OriginalPinCount);
    Transactions.Restore();
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FSalRobustGraphTopologyPersistenceTest,
    "Loomle.Sal.Robustness.Graph.TopologySaveUnloadReload",
    EAutomationTestFlags::EditorContext
        | EAutomationTestFlags::EngineFilter)

bool FSalRobustGraphTopologyPersistenceTest::RunTest(
    const FString& Parameters)
{
    if (!RobustGraphRequireIdleEditor(
            *this,
            TEXT("Graph topology persistence coverage")))
    {
        return false;
    }
    Loomle::Tests::FScopedIsolatedTransactor Transactions;
    if (!TestTrue(
            TEXT("Topology persistence isolates transaction history"),
            Transactions.Initialize()))
    {
        return false;
    }
    FRobustGraphFixture Fixture(true);
    if (!TestTrue(TEXT("Persistent Graph fixture is valid"), Fixture.IsValid()))
    {
        Transactions.Restore();
        return false;
    }

    const FGuid BlueprintId =
        Fixture.Blueprint->GetBlueprintGuid();
    const FGuid GraphId = Fixture.Graph->GraphGuid;
    const FGuid EntryId = Fixture.Entry->NodeGuid;
    const FGuid BranchAId = Fixture.BranchA->NodeGuid;
    const FGuid EntryThenId = Fixture.EntryThen->PinId;
    const FGuid BranchExecId = Fixture.BranchAExec->PinId;
    const TSharedPtr<FJsonObject> Finalized =
        FSalBlueprintInterface::Patch(
            RobustGraphTerminalPatch(),
            RobustGraphBlueprintTarget(Fixture.Blueprint));
    TestTrue(
        TEXT("Blueprint compile/save terminal Patch validates"),
        RobustGraphResultBool(Finalized, TEXT("valid")));
    TestTrue(
        TEXT("Blueprint compile/save terminal Patch applies"),
        RobustGraphResultBool(Finalized, TEXT("applied")));
    TestTrue(
        TEXT("Topology package is written to disk"),
        IFileManager::Get().FileExists(*Fixture.Filename));
    TestFalse(
        TEXT("Topology package is clean after save"),
        Fixture.Package->IsDirty());

    Transactions.Restore();
    FString Error;
    const bool bUnloaded = Fixture.Unload(Error);
    TestTrue(
        *FString::Printf(
            TEXT("Saved topology unloads: %s"),
            *Error),
        bUnloaded);
    Error.Reset();
    const bool bReloaded = Fixture.Reload(Error);
    TestTrue(
        *FString::Printf(
            TEXT("Saved topology reloads: %s"),
            *Error),
        bReloaded);
    if (Fixture.Blueprint == nullptr)
    {
        return false;
    }
    TestEqual(
        TEXT("Reload preserves Blueprint identity"),
        Fixture.Blueprint->GetBlueprintGuid(),
        BlueprintId);
    UEdGraph* ReloadedGraph =
        Fixture.FindGraphByGuid(GraphId);
    TestNotNull(
        TEXT("Reload preserves Graph identity"),
        ReloadedGraph);
    UEdGraphNode* ReloadedEntry =
        FRobustGraphFixture::FindNodeByGuid(
            ReloadedGraph,
            EntryId);
    UEdGraphNode* ReloadedBranch =
        FRobustGraphFixture::FindNodeByGuid(
            ReloadedGraph,
            BranchAId);
    UEdGraphPin* ReloadedThen =
        FRobustGraphFixture::FindPinByGuid(
            ReloadedGraph,
            EntryThenId);
    UEdGraphPin* ReloadedExec =
        FRobustGraphFixture::FindPinByGuid(
            ReloadedGraph,
            BranchExecId);
    TestNotNull(
        TEXT("Reload preserves source Node identity"),
        ReloadedEntry);
    TestNotNull(
        TEXT("Reload preserves destination Node identity"),
        ReloadedBranch);
    TestNotNull(
        TEXT("Reload preserves source Pin identity"),
        ReloadedThen);
    TestNotNull(
        TEXT("Reload preserves destination Pin identity"),
        ReloadedExec);
    TestTrue(
        TEXT("Reload preserves exact authored Edge topology"),
        ReloadedThen != nullptr
            && ReloadedExec != nullptr
            && ReloadedThen->LinkedTo.Contains(ReloadedExec)
            && ReloadedExec->LinkedTo.Contains(ReloadedThen));

    if (ReloadedGraph != nullptr)
    {
        const TSharedPtr<FJsonObject> Readback =
            FSalGraphInterface::Query(
                RobustGraphTraversal(
                    TEXT("exec_flow"),
                    TEXT("from"),
                    RobustGraphTyped(TEXT("node"), EntryId),
                    1),
                RobustGraphTarget(
                    Fixture.Blueprint,
                    ReloadedGraph));
        TestFalse(
            TEXT("PostLoad SAL exec flow succeeds"),
            RobustGraphHasError(Readback));
        TestTrue(
            TEXT("PostLoad SAL readback contains preserved destination"),
            RobustGraphContainsCallId(
                Readback,
                TEXT("node"),
                BranchAId));
        TestEqual(
            TEXT("PostLoad SAL readback contains preserved Edge"),
            RobustGraphEdgeCount(Readback),
            1);
    }

    Error.Reset();
    const bool bCleaned = Fixture.Cleanup(Error);
    TestTrue(
        *FString::Printf(
            TEXT("Persistent topology fixture is removed: %s"),
            *Error),
        bCleaned);
    return true;
}

#endif
