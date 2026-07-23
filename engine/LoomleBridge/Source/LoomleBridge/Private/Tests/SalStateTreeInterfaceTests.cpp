// Copyright 2026 Loomle contributors.

#if WITH_DEV_AUTOMATION_TESTS

#include "Sal/SalModel.h"
#include "Sal/SalObjectBuilder.h"
#include "Sal/SalTargetResolver.h"
#include "Sal/StateTree/SalStateTreeInterface.h"
#include "SalStateTreeTestSchema.h"

#include "Blueprint/StateTreeTaskBlueprintBase.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Misc/AutomationTest.h"
#include "Misc/PackageName.h"
#include "PropertyBindingPath.h"
#include "StateTree.h"
#include "StateTreeConditionBase.h"
#include "StateTreeEditingSubsystem.h"
#include "StateTreeEditorData.h"
#include "StateTreeEditorNode.h"
#include "StateTreeEditorPropertyBindings.h"
#include "StateTreeEvents.h"
#include "StateTreeEvaluatorBase.h"
#include "StateTreeConsiderationBase.h"
#include "StateTreeState.h"
#include "StateTreeTaskBase.h"
#include "StructUtils/PropertyBag.h"
#include "UObject/Package.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/UnrealType.h"

namespace
{
using namespace Loomle::Sal;

struct FTestStateTree
{
    UStateTree* Asset = nullptr;
    UStateTreeEditorData* EditorData = nullptr;
    UStateTreeState* Root = nullptr;
    UStateTreeState* First = nullptr;
    UStateTreeState* Second = nullptr;
};

UPackage* MakeTestPackage()
{
    return CreatePackage(*FString::Printf(
        TEXT("/LoomleTests/StateTreeRead_%s"),
        *FGuid::NewGuid().ToString(EGuidFormats::Digits)));
}

FTestStateTree MakeStateTree(UObject* Outer)
{
    FTestStateTree Result;
    Result.Asset = NewObject<UStateTree>(Outer, NAME_None, RF_Transient);
    Result.EditorData = NewObject<UStateTreeEditorData>(Result.Asset, NAME_None, RF_Transient);
    Result.Asset->EditorData = Result.EditorData;

    Result.Root = &Result.EditorData->AddSubTree(FName(TEXT("Root")));
    Result.Root->ID = FGuid(0x10000001, 0x10000002, 0x10000003, 0x10000004);
    Result.First = &Result.Root->AddChildState(FName(TEXT("First")));
    Result.First->ID = FGuid(0x20000001, 0x20000002, 0x20000003, 0x20000004);
    Result.Second = &Result.Root->AddChildState(FName(TEXT("Second")));
    Result.Second->ID = FGuid(0x30000001, 0x30000002, 0x30000003, 0x30000004);
    return Result;
}

FSalResolvedTarget ResolvedTarget(const FTestStateTree& Tree)
{
    FSalResolvedTarget Target;
    Target.Kind = ESalTargetKind::Asset;
    Target.Alias = TEXT("tree");
    Target.AssetPath = Tree.Asset->GetPathName();
    Target.Object = Tree.Asset;
    Target.Package = Tree.Asset->GetOutermost();
    Target.Interfaces = {FName(TEXT("asset")), FName(TEXT("state_tree"))};
    return Target;
}

FSalQuery Query(const FString& Kind, const FString& Alias = TEXT("tree"))
{
    FSalQuery Result;
    Result.Alias = Alias;
    Result.Operation = MakeShared<FJsonObject>();
    Result.Operation->SetStringField(TEXT("kind"), Kind);
    return Result;
}

template <typename NodeType>
FStateTreeEditorNode& AddNamedNode(
    TArray<FStateTreeEditorNode>& Nodes,
    const FGuid& Id,
    const FName Name)
{
    FStateTreeEditorNode& Node = Nodes.AddDefaulted_GetRef();
    Node.ID = Id;
    Node.Node.InitializeAs<NodeType>();
    Node.Node.template GetMutable<NodeType>().Name = Name;
    return Node;
}

template <typename NodeType>
FStateTreeEditorNode& SetNamedNode(
    TStateTreeEditorNode<NodeType>& Node,
    const FGuid& Id,
    const FName Name)
{
    Node.ID = Id;
    Node.Node.template GetMutable<NodeType>().Name = Name;
    return Node;
}

FStateTreeEditorNode& AddBindingTask(
    TArray<FStateTreeEditorNode>& Nodes,
    const FGuid& Id,
    const FName Name)
{
    FStateTreeEditorNode& Node = AddNamedNode<FSalStateTreeBindingTask>(Nodes, Id, Name);
    Node.Instance.InitializeAs<FSalStateTreeBindingTaskInstanceData>();
    FSalStateTreeBindingTaskInstanceData& Instance =
        Node.Instance.GetMutable<FSalStateTreeBindingTaskInstanceData>();
    Instance.InputValues.Add(0);
    Instance.OutputValues.Add(0);
    return Node;
}

FStateTreeEditorNode& AddBindingCondition(
    TArray<FStateTreeEditorNode>& Nodes,
    const FGuid& Id,
    const FName Name)
{
    FStateTreeEditorNode& Node = AddNamedNode<FSalStateTreeBindingCondition>(Nodes, Id, Name);
    Node.Instance.InitializeAs<FSalStateTreeBindingTaskInstanceData>();
    return Node;
}

FStateTreeEditorNode& AddContextTask(
    TArray<FStateTreeEditorNode>& Nodes,
    const FGuid& Id,
    const FName Name)
{
    FStateTreeEditorNode& Node = AddNamedNode<FSalStateTreeContextTask>(Nodes, Id, Name);
    Node.Instance.InitializeAs<FSalStateTreeContextTaskInstanceData>();
    return Node;
}

FStateTreeEditorNode& AddIneligibleNodeContextTask(
    TArray<FStateTreeEditorNode>& Nodes,
    const FGuid& Id,
    const FName Name)
{
    FStateTreeEditorNode& Node = AddNamedNode<FSalStateTreeIneligibleNodeContextTask>(
        Nodes,
        Id,
        Name);
    Node.Instance.InitializeAs<FSalStateTreeContextTaskInstanceData>();
    return Node;
}

bool HasError(const TSharedPtr<FJsonObject>& Result)
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

TArray<FString> CallIds(const TSharedPtr<FJsonObject>& Result, const FString& Callee)
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
    for (const TSharedPtr<FJsonValue>& StatementValue : *Statements)
    {
        const TSharedPtr<FJsonObject>* Statement = nullptr;
        const TSharedPtr<FJsonObject>* Call = nullptr;
        const TSharedPtr<FJsonObject>* Args = nullptr;
        FString ActualCallee;
        FString Id;
        if (StatementValue.IsValid()
            && StatementValue->TryGetObject(Statement)
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

TArray<FString> CallNativeNames(
    const TSharedPtr<FJsonObject>& Result,
    const FString& Callee,
    const FString& Field)
{
    TArray<FString> Names;
    const TSharedPtr<FJsonObject>* Object = nullptr;
    const TArray<TSharedPtr<FJsonValue>>* Statements = nullptr;
    if (!Result.IsValid()
        || !Result->TryGetObjectField(TEXT("object"), Object)
        || Object == nullptr
        || !(*Object)->TryGetArrayField(TEXT("statements"), Statements)
        || Statements == nullptr)
    {
        return Names;
    }
    for (const TSharedPtr<FJsonValue>& StatementValue : *Statements)
    {
        const TSharedPtr<FJsonObject>* Statement = nullptr;
        const TSharedPtr<FJsonObject>* Call = nullptr;
        const TSharedPtr<FJsonObject>* Args = nullptr;
        const TSharedPtr<FJsonObject>* NativeName = nullptr;
        FString ActualCallee;
        FString Name;
        if (StatementValue.IsValid()
            && StatementValue->TryGetObject(Statement)
            && Statement != nullptr
            && (*Statement)->TryGetObjectField(TEXT("value"), Call)
            && Call != nullptr
            && (*Call)->TryGetStringField(TEXT("callee"), ActualCallee)
            && ActualCallee == Callee
            && (*Call)->TryGetObjectField(TEXT("args"), Args)
            && Args != nullptr
            && (*Args)->TryGetObjectField(Field, NativeName)
            && NativeName != nullptr
            && (*NativeName)->TryGetStringField(TEXT("name"), Name))
        {
            Names.Add(Name);
        }
    }
    return Names;
}

int32 CallCount(const TSharedPtr<FJsonObject>& Result, const FString& Callee)
{
    int32 Count = 0;
    const TSharedPtr<FJsonObject>* Object = nullptr;
    const TArray<TSharedPtr<FJsonValue>>* Statements = nullptr;
    if (!Result.IsValid()
        || !Result->TryGetObjectField(TEXT("object"), Object)
        || Object == nullptr
        || !(*Object)->TryGetArrayField(TEXT("statements"), Statements)
        || Statements == nullptr)
    {
        return Count;
    }
    for (const TSharedPtr<FJsonValue>& StatementValue : *Statements)
    {
        const TSharedPtr<FJsonObject>* Statement = nullptr;
        const TSharedPtr<FJsonObject>* Call = nullptr;
        FString ActualCallee;
        if (StatementValue.IsValid()
            && StatementValue->TryGetObject(Statement)
            && Statement != nullptr
            && (*Statement)->TryGetObjectField(TEXT("value"), Call)
            && Call != nullptr
            && (*Call)->TryGetStringField(TEXT("callee"), ActualCallee)
            && ActualCallee == Callee)
        {
            ++Count;
        }
    }
    return Count;
}

bool HasDiagnosticContaining(const TSharedPtr<FJsonObject>& Result, const FString& Needle)
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
        FString Message;
        if (Value.IsValid()
            && Value->TryGetObject(Diagnostic)
            && Diagnostic != nullptr
            && (*Diagnostic)->TryGetStringField(TEXT("message"), Message)
            && Message.Contains(Needle))
        {
            return true;
        }
    }
    return false;
}

bool HasDiagnosticCode(const TSharedPtr<FJsonObject>& Result, const FString& ExpectedCode)
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
            && Code == ExpectedCode)
        {
            return true;
        }
    }
    return false;
}

bool HasDiagnosticCodeAndSeverity(
    const TSharedPtr<FJsonObject>& Result,
    const FString& ExpectedCode,
    const FString& ExpectedSeverity)
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
        FString Severity;
        if (Value.IsValid()
            && Value->TryGetObject(Diagnostic)
            && Diagnostic != nullptr
            && (*Diagnostic)->TryGetStringField(TEXT("code"), Code)
            && (*Diagnostic)->TryGetStringField(TEXT("severity"), Severity)
            && Code == ExpectedCode
            && Severity == ExpectedSeverity)
        {
            return true;
        }
    }
    return false;
}

FString NextCursor(const TSharedPtr<FJsonObject>& Result)
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

TArray<FString> CallCallees(const TSharedPtr<FJsonObject>& Result)
{
    TArray<FString> Callees;
    const TSharedPtr<FJsonObject>* Object = nullptr;
    const TArray<TSharedPtr<FJsonValue>>* Statements = nullptr;
    if (!Result.IsValid()
        || !Result->TryGetObjectField(TEXT("object"), Object)
        || Object == nullptr
        || !(*Object)->TryGetArrayField(TEXT("statements"), Statements)
        || Statements == nullptr)
    {
        return Callees;
    }
    for (const TSharedPtr<FJsonValue>& StatementValue : *Statements)
    {
        const TSharedPtr<FJsonObject>* Statement = nullptr;
        const TSharedPtr<FJsonObject>* Call = nullptr;
        FString Callee;
        if (StatementValue.IsValid()
            && StatementValue->TryGetObject(Statement)
            && Statement != nullptr
            && (*Statement)->TryGetObjectField(TEXT("value"), Call)
            && Call != nullptr
            && (*Call)->TryGetStringField(TEXT("callee"), Callee))
        {
            Callees.Add(Callee);
        }
    }
    return Callees;
}

FString RefText(const TSharedPtr<FJsonObject>& Ref)
{
    if (!Ref.IsValid())
    {
        return TEXT("<invalid>");
    }

    FString Kind;
    if (!Ref->TryGetStringField(TEXT("kind"), Kind))
    {
        return TEXT("<invalid>");
    }
    if (Kind == TEXT("member"))
    {
        const TSharedPtr<FJsonObject>* Owner = nullptr;
        const TArray<TSharedPtr<FJsonValue>>* Path = nullptr;
        if (!Ref->TryGetObjectField(TEXT("object"), Owner)
            || Owner == nullptr
            || !Ref->TryGetArrayField(TEXT("path"), Path)
            || Path == nullptr)
        {
            return TEXT("<invalid>");
        }
        FString Result = RefText(*Owner);
        for (const TSharedPtr<FJsonValue>& Segment : *Path)
        {
            FString Text;
            if (Segment.IsValid()
                && Segment->Type == EJson::String
                && Segment->TryGetString(Text))
            {
                Result += TEXT(".") + Text;
                continue;
            }
            double Number = 0.0;
            if (Segment.IsValid()
                && Segment->Type == EJson::Number
                && Segment->TryGetNumber(Number)
                && Number >= 0.0
                && Number <= static_cast<double>(MAX_int32)
                && static_cast<double>(static_cast<int32>(Number)) == Number)
            {
                Result += FString::Printf(TEXT("[%d]"), static_cast<int32>(Number));
                continue;
            }
            return TEXT("<invalid>");
        }
        return Result;
    }

    FString Id;
    if (Ref->TryGetStringField(TEXT("id"), Id))
    {
        return Kind + TEXT("@") + Id;
    }
    FString Name;
    if (Ref->TryGetStringField(TEXT("name"), Name))
    {
        return Kind + TEXT(":") + Name;
    }
    return Kind;
}

TArray<FString> EdgeTexts(const TSharedPtr<FJsonObject>& Result)
{
    TArray<FString> Edges;
    const TSharedPtr<FJsonObject>* Object = nullptr;
    const TArray<TSharedPtr<FJsonValue>>* Statements = nullptr;
    if (!Result.IsValid()
        || !Result->TryGetObjectField(TEXT("object"), Object)
        || Object == nullptr
        || !(*Object)->TryGetArrayField(TEXT("statements"), Statements)
        || Statements == nullptr)
    {
        return Edges;
    }
    for (const TSharedPtr<FJsonValue>& StatementValue : *Statements)
    {
        const TSharedPtr<FJsonObject>* Statement = nullptr;
        const TSharedPtr<FJsonObject>* From = nullptr;
        const TSharedPtr<FJsonObject>* To = nullptr;
        if (StatementValue.IsValid()
            && StatementValue->TryGetObject(Statement)
            && Statement != nullptr
            && (*Statement)->TryGetObjectField(TEXT("from"), From)
            && From != nullptr
            && (*Statement)->TryGetObjectField(TEXT("to"), To)
            && To != nullptr)
        {
            Edges.Add(RefText(*From) + TEXT(" -> ") + RefText(*To));
        }
    }
    return Edges;
}

void CountMemberPathSegmentKinds(
    const TSharedPtr<FJsonObject>& Ref,
    int32& OutNumberSegments,
    int32& OutStringZeroSegments)
{
    if (!Ref.IsValid())
    {
        return;
    }
    FString Kind;
    const TArray<TSharedPtr<FJsonValue>>* Path = nullptr;
    if (!Ref->TryGetStringField(TEXT("kind"), Kind)
        || Kind != TEXT("member")
        || !Ref->TryGetArrayField(TEXT("path"), Path)
        || Path == nullptr)
    {
        return;
    }
    for (const TSharedPtr<FJsonValue>& Segment : *Path)
    {
        double Number = 0.0;
        FString Text;
        if (Segment.IsValid()
            && Segment->Type == EJson::Number
            && Segment->TryGetNumber(Number))
        {
            ++OutNumberSegments;
        }
        else if (Segment.IsValid()
            && Segment->Type == EJson::String
            && Segment->TryGetString(Text)
            && Text == TEXT("0"))
        {
            ++OutStringZeroSegments;
        }
    }
}

bool EdgePathSegmentKinds(
    const TSharedPtr<FJsonObject>& Result,
    const FString& EdgeText,
    int32& OutNumberSegments,
    int32& OutStringZeroSegments)
{
    OutNumberSegments = 0;
    OutStringZeroSegments = 0;
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
        const TSharedPtr<FJsonObject>* From = nullptr;
        const TSharedPtr<FJsonObject>* To = nullptr;
        if (StatementValue.IsValid()
            && StatementValue->TryGetObject(Statement)
            && Statement != nullptr
            && (*Statement)->TryGetObjectField(TEXT("from"), From)
            && From != nullptr
            && (*Statement)->TryGetObjectField(TEXT("to"), To)
            && To != nullptr
            && RefText(*From) + TEXT(" -> ") + RefText(*To) == EdgeText)
        {
            CountMemberPathSegmentKinds(*From, OutNumberSegments, OutStringZeroSegments);
            CountMemberPathSegmentKinds(*To, OutNumberSegments, OutStringZeroSegments);
            return true;
        }
    }
    return false;
}

bool HasCommentImmediatelyBeforeEdge(
    const TSharedPtr<FJsonObject>& Result,
    const FString& EdgeText,
    const FString& CommentNeedle)
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
    for (int32 Index = 1; Index < Statements->Num(); ++Index)
    {
        const TSharedPtr<FJsonObject>* Edge = nullptr;
        const TSharedPtr<FJsonObject>* From = nullptr;
        const TSharedPtr<FJsonObject>* To = nullptr;
        if (!(*Statements)[Index].IsValid()
            || !(*Statements)[Index]->TryGetObject(Edge)
            || Edge == nullptr
            || !(*Edge)->TryGetObjectField(TEXT("from"), From)
            || From == nullptr
            || !(*Edge)->TryGetObjectField(TEXT("to"), To)
            || To == nullptr
            || RefText(*From) + TEXT(" -> ") + RefText(*To) != EdgeText)
        {
            continue;
        }

        const TSharedPtr<FJsonObject>* Comment = nullptr;
        FString Kind;
        FString Text;
        return (*Statements)[Index - 1].IsValid()
            && (*Statements)[Index - 1]->TryGetObject(Comment)
            && Comment != nullptr
            && (*Comment)->TryGetStringField(TEXT("kind"), Kind)
            && Kind == TEXT("comment")
            && (*Comment)->TryGetStringField(TEXT("text"), Text)
            && Text.Contains(CommentNeedle);
    }
    return false;
}

TSharedPtr<FJsonObject> FirstCallArgs(
    const TSharedPtr<FJsonObject>& Result,
    const FString& Callee)
{
    const TSharedPtr<FJsonObject>* Object = nullptr;
    const TArray<TSharedPtr<FJsonValue>>* Statements = nullptr;
    if (!Result.IsValid()
        || !Result->TryGetObjectField(TEXT("object"), Object)
        || Object == nullptr
        || !(*Object)->TryGetArrayField(TEXT("statements"), Statements)
        || Statements == nullptr)
    {
        return nullptr;
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
            return *Args;
        }
    }
    return nullptr;
}

bool HasCommentContaining(const TSharedPtr<FJsonObject>& Result, const FString& Needle)
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

bool NativeNameEquals(
    const TSharedPtr<FJsonObject>& Object,
    const FString& Field,
    const FString& Expected)
{
    const TSharedPtr<FJsonObject>* Name = nullptr;
    FString Kind;
    FString Actual;
    return Object.IsValid()
        && Object->TryGetObjectField(Field, Name)
        && Name != nullptr
        && (*Name)->TryGetStringField(TEXT("kind"), Kind)
        && Kind == TEXT("name")
        && (*Name)->TryGetStringField(TEXT("name"), Actual)
        && Actual == Expected;
}

FPropertyBagPropertyDesc ParameterDesc(
    const FName Name,
    const EPropertyBagPropertyType Type,
    const FGuid& Id,
    TArray<FPropertyBagPropertyDescMetaData> MetaData = {})
{
    FPropertyBagPropertyDesc Desc(Name, Type);
    Desc.ID = Id;
#if WITH_EDITORONLY_DATA
    Desc.MetaData = MoveTemp(MetaData);
#endif
    Desc.PropertyFlags = static_cast<uint64>(CPF_Edit | CPF_BlueprintVisible);
    return Desc;
}

struct FTestParameterIds
{
    FGuid GlobalSpeed{0xD1000001, 0xD1000002, 0xD1000003, 0xD1000004};
    FGuid Shared{0xD2000001, 0xD2000002, 0xD2000003, 0xD2000004};
    FGuid RootStateThreshold{0xD3000001, 0xD3000002, 0xD3000003, 0xD3000004};
    FGuid ChildLabel{0xD4000001, 0xD4000002, 0xD4000003, 0xD4000004};
};

FTestParameterIds AddTestParameters(const FTestStateTree& Tree)
{
    const FTestParameterIds Ids;
    FInstancedPropertyBag& RootBag = const_cast<FInstancedPropertyBag&>(
        Tree.EditorData->GetRootParametersPropertyBag());
    RootBag.AddProperties({
        ParameterDesc(
            TEXT("GlobalSpeed"),
            EPropertyBagPropertyType::Float,
            Ids.GlobalSpeed,
            {
                {TEXT("FirstMeta"), TEXT("AlphaMetaValue")},
                {TEXT("SecondMeta"), TEXT("BetaMetaValue")},
            }),
        ParameterDesc(TEXT("GlobalCount"), EPropertyBagPropertyType::Int32, Ids.Shared),
    });
    RootBag.SetValueFloat(TEXT("GlobalSpeed"), 125.5f);
    RootBag.SetValueInt32(TEXT("GlobalCount"), 7);

    Tree.Root->Parameters.ID = FGuid(0xDA000001, 0xDA000002, 0xDA000003, 0xDA000004);
    Tree.Root->Parameters.Parameters.AddProperties({
        ParameterDesc(
            TEXT("RootStateThreshold"),
            EPropertyBagPropertyType::Double,
            Ids.RootStateThreshold),
    });
    Tree.Root->Parameters.Parameters.SetValueDouble(TEXT("RootStateThreshold"), 0.75);

    Tree.First->Parameters.ID = FGuid(0xDB000001, 0xDB000002, 0xDB000003, 0xDB000004);
    Tree.Second->Parameters.ID = FGuid(0xDC000001, 0xDC000002, 0xDC000003, 0xDC000004);
    Tree.Second->Parameters.Parameters.AddProperties({
        ParameterDesc(
            TEXT("ChildLabel"),
            EPropertyBagPropertyType::String,
            Ids.ChildLabel,
            {{TEXT("OwnerSearchKey"), TEXT("FirstChildMetadata")}}),
        ParameterDesc(TEXT("SharedInFirst"), EPropertyBagPropertyType::Bool, Ids.Shared),
    });
    Tree.Second->Parameters.Parameters.SetValueString(TEXT("ChildLabel"), TEXT("InheritedLabel"));
    Tree.Second->Parameters.Parameters.SetValueBool(TEXT("SharedInFirst"), false);
    Tree.Second->Type = EStateTreeStateType::Subtree;

    Tree.First->Type = EStateTreeStateType::Linked;
    Tree.First->LinkedSubtree = Tree.Second->GetLinkToState();
    Tree.First->Parameters.bFixedLayout = true;
    Tree.First->UpdateParametersFromLinkedSubtree();
    Tree.First->SetParametersPropertyOverridden(Ids.ChildLabel, true);
    Tree.First->Parameters.Parameters.SetValueString(TEXT("ChildLabel"), TEXT("Scout"));
    return Ids;
}

FString ParameterId(const FGuid& ContainerId, const FGuid& PropertyId)
{
    return FString::Printf(
        TEXT("%s/%s"),
        *ContainerId.ToString(EGuidFormats::DigitsWithHyphensLower),
        *PropertyId.ToString(EGuidFormats::DigitsWithHyphensLower));
}

FPropertyBindingPath EventPayloadValuePath(const FGuid& EventId)
{
    FPropertyBindingPath Path;
    Path.FromString(TEXT("Payload.Value"));
    FStateTreeEvent Event;
    Event.Payload.InitializeAs<FSalStateTreeBindingEventPayload>();
    Path.UpdateSegmentsFromValue(FStateTreeDataView(FStructView::Make(Event)));
    return FPropertyBindingPath(EventId, Path.GetSegments());
}

bool AppendRawPropertyBindings(
    FStateTreeEditorPropertyBindings& Bindings,
    const int32 Count,
    const FPropertyBindingPath& Source,
    const FPropertyBindingPath& Target)
{
    FArrayProperty* PropertyBindingsProperty = FindFProperty<FArrayProperty>(
        FStateTreeEditorPropertyBindings::StaticStruct(),
        TEXT("PropertyBindings"));
    const FStructProperty* BindingProperty = PropertyBindingsProperty != nullptr
        ? CastField<FStructProperty>(PropertyBindingsProperty->Inner)
        : nullptr;
    if (PropertyBindingsProperty == nullptr
        || BindingProperty == nullptr
        || BindingProperty->Struct != FStateTreePropertyPathBinding::StaticStruct())
    {
        return false;
    }

    FScriptArrayHelper Helper(
        PropertyBindingsProperty,
        PropertyBindingsProperty->ContainerPtrToValuePtr<void>(&Bindings));
    const int32 FirstIndex = Helper.AddValues(Count);
    for (int32 Index = 0; Index < Count; ++Index)
    {
        FStateTreePropertyPathBinding* Binding =
            reinterpret_cast<FStateTreePropertyPathBinding*>(Helper.GetRawPtr(FirstIndex + Index));
        *Binding = FStateTreePropertyPathBinding(Source, Target, false);
    }
    return true;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FSalStateTreeTargetResolutionTest,
    "Loomle.Sal.StateTree.TargetResolution",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSalStateTreeTargetResolutionTest::RunTest(const FString& Parameters)
{
    const FString PackageName = FString::Printf(
        TEXT("/LoomleTests/StateTree_%s"),
        *FGuid::NewGuid().ToString(EGuidFormats::Digits));
    UPackage* Package = CreatePackage(*PackageName);
    const FName AssetName(*FPackageName::GetLongPackageAssetName(PackageName));
    UStateTree* Asset = NewObject<UStateTree>(Package, AssetName, RF_Public | RF_Standalone | RF_Transient);
    Asset->EditorData = NewObject<UStateTreeEditorData>(Asset, NAME_None, RF_Transient);

    TSharedPtr<FJsonObject> Args = MakeShared<FJsonObject>();
    Args->SetStringField(TEXT("path"), Asset->GetPathName());
    Args->SetStringField(TEXT("type"), Asset->GetClass()->GetPathName());
    FSalResolvedTarget Target;
    TSharedPtr<FJsonObject> Error;
    const bool bResolved = FSalTargetResolver().Resolve(
        TEXT("tree"),
        Value::CallObject(TEXT("asset"), Args),
        false,
        Target,
        Error);
    TestTrue(TEXT("Exact UStateTree resolves"), bResolved);
    TestTrue(TEXT("Asset capability remains composed"), Target.HasInterface(FName(TEXT("asset"))));
    TestTrue(TEXT("StateTree capability is composed"), Target.HasInterface(FName(TEXT("state_tree"))));
    TestTrue(TEXT("Resolved native object is exact"), Target.Object == Asset);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FSalStateTreeAuthoredOrderTest,
    "Loomle.Sal.StateTree.AuthoredOrderAndExactState",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSalStateTreeAuthoredOrderTest::RunTest(const FString& Parameters)
{
    const FTestStateTree Tree = MakeStateTree(MakeTestPackage());
    const FSalResolvedTarget Target = ResolvedTarget(Tree);

    FSalQuery TreeQuery = Query(TEXT("tree"));
    const TSharedPtr<FJsonObject> TreeResult = FSalStateTreeInterface::Query(TreeQuery, Target);
    TestFalse(TEXT("Tree Query succeeds"), HasError(TreeResult));
    TestEqual(TEXT("Tree Query declares one compact Asset owner"), CallCount(TreeResult, TEXT("asset")), 1);
    const TArray<FString> StateIds = CallIds(TreeResult, TEXT("state"));
    TestEqual(TEXT("Tree contains root and direct children"), StateIds.Num(), 3);
    if (StateIds.Num() == 3)
    {
        TestEqual(TEXT("Root remains first"), StateIds[0], Tree.Root->ID.ToString(EGuidFormats::DigitsWithHyphensLower));
        TestEqual(TEXT("First authored child remains first"), StateIds[1], Tree.First->ID.ToString(EGuidFormats::DigitsWithHyphensLower));
        TestEqual(TEXT("Second authored child remains second"), StateIds[2], Tree.Second->ID.ToString(EGuidFormats::DigitsWithHyphensLower));
    }

    FSalQuery ExactQuery = Query(TEXT("state"));
    ExactQuery.Operation->SetStringField(
        TEXT("id"),
        Tree.Second->ID.ToString(EGuidFormats::DigitsWithHyphensLower));
    const TSharedPtr<FJsonObject> ExactResult = FSalStateTreeInterface::Query(ExactQuery, Target);
    TestFalse(TEXT("Exact State Query succeeds"), HasError(ExactResult));
    TestEqual(TEXT("Exact State Query declares one compact Asset owner"), CallCount(ExactResult, TEXT("asset")), 1);
    const TArray<FString> ExactIds = CallIds(ExactResult, TEXT("state"));
    TestEqual(TEXT("Exact State Query returns one State"), ExactIds.Num(), 1);
    if (ExactIds.Num() == 1)
    {
        TestEqual(
            TEXT("Exact State identity remains native"),
            ExactIds[0],
            Tree.Second->ID.ToString(EGuidFormats::DigitsWithHyphensLower));
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FSalStateTreeQueryDoesNotMutateTest,
    "Loomle.Sal.StateTree.QueryDoesNotMutate",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSalStateTreeQueryDoesNotMutateTest::RunTest(const FString& Parameters)
{
    const FTestStateTree Tree = MakeStateTree(MakeTestPackage());
    const FSalResolvedTarget Target = ResolvedTarget(Tree);
    const FTestParameterIds ParameterIds = AddTestParameters(Tree);
    USalStateTreeTestSchema* Schema = NewObject<USalStateTreeTestSchema>(Tree.EditorData);
    const FGuid ContextId(0xDD000001, 0xDD000002, 0xDD000003, 0xDD000004);
    Schema->ContextData.Add(FStateTreeExternalDataDesc(
        FName(TEXT("ReadOnlyContext")), UObject::StaticClass(), ContextId));
    Tree.EditorData->Schema = Schema;
    const FGuid TaskId(0x51000001, 0x51000002, 0x51000003, 0x51000004);
    const FGuid TransitionId(0x52000001, 0x52000002, 0x52000003, 0x52000004);
    AddNamedNode<FStateTreeTaskBase>(Tree.Root->Tasks, TaskId, FName(TEXT("Read Only Task")));
    FStateTreeTransition& Transition = Tree.Root->AddTransition(
        EStateTreeTransitionTrigger::OnStateCompleted,
        EStateTreeTransitionType::Succeeded);
    Transition.ID = TransitionId;
    Tree.Asset->LastCompiledEditorDataHash = UStateTreeEditingSubsystem::CalculateStateTreeHash(Tree.Asset);
    Tree.Asset->GetOutermost()->SetDirtyFlag(false);
    const uint32 BeforeHash = UStateTreeEditingSubsystem::CalculateStateTreeHash(Tree.Asset);
    const uint32 BeforeCompiledHash = Tree.Asset->LastCompiledEditorDataHash;
    const bool bBeforeDirty = Tree.Asset->GetOutermost()->IsDirty();

    FSalQuery TargetQuery = Query(TEXT("target"));
    FSalQuery SummaryQuery = Query(TEXT("summary"));
    FSalQuery TreeQuery = Query(TEXT("tree"));
    FSalQuery StatesQuery = Query(TEXT("states"));
    FSalQuery NodesQuery = Query(TEXT("nodes"));
    FSalQuery ParametersQuery = Query(TEXT("parameters"));
    FSalQuery StateQuery = Query(TEXT("state"));
    StateQuery.Operation->SetStringField(
        TEXT("id"),
        Tree.First->ID.ToString(EGuidFormats::DigitsWithHyphensLower));
    FSalQuery NodeQuery = Query(TEXT("node"));
    NodeQuery.Operation->SetStringField(TEXT("id"), TaskId.ToString(EGuidFormats::DigitsWithHyphensLower));
    FSalQuery TransitionQuery = Query(TEXT("transition"));
    TransitionQuery.Operation->SetStringField(
        TEXT("id"),
        TransitionId.ToString(EGuidFormats::DigitsWithHyphensLower));
    FSalQuery ParameterQuery = Query(TEXT("parameter"));
    ParameterQuery.Operation->SetStringField(
        TEXT("id"),
        ParameterId(Tree.EditorData->GetRootParametersGuid(), ParameterIds.GlobalSpeed));
    FSalQuery ContextQuery = Query(TEXT("object"));
    ContextQuery.Operation->SetStringField(
        TEXT("id"),
        ContextId.ToString(EGuidFormats::DigitsWithHyphensLower));
    TestFalse(TEXT("Target read succeeds"), HasError(FSalStateTreeInterface::Query(TargetQuery, Target)));
    TestFalse(TEXT("Summary read succeeds"), HasError(FSalStateTreeInterface::Query(SummaryQuery, Target)));
    TestFalse(TEXT("Tree read succeeds"), HasError(FSalStateTreeInterface::Query(TreeQuery, Target)));
    TestFalse(TEXT("States collection read succeeds"), HasError(FSalStateTreeInterface::Query(StatesQuery, Target)));
    TestFalse(TEXT("Nodes collection read succeeds"), HasError(FSalStateTreeInterface::Query(NodesQuery, Target)));
    TestFalse(TEXT("Parameters collection read succeeds"), HasError(FSalStateTreeInterface::Query(ParametersQuery, Target)));
    TestFalse(TEXT("State read succeeds"), HasError(FSalStateTreeInterface::Query(StateQuery, Target)));
    TestFalse(TEXT("Node read succeeds"), HasError(FSalStateTreeInterface::Query(NodeQuery, Target)));
    TestFalse(TEXT("Transition read succeeds"), HasError(FSalStateTreeInterface::Query(TransitionQuery, Target)));
    TestFalse(TEXT("Parameter read succeeds"), HasError(FSalStateTreeInterface::Query(ParameterQuery, Target)));
    TestFalse(TEXT("Context Data read succeeds"), HasError(FSalStateTreeInterface::Query(ContextQuery, Target)));

    TestEqual(
        TEXT("Authored hash is unchanged"),
        UStateTreeEditingSubsystem::CalculateStateTreeHash(Tree.Asset),
        BeforeHash);
    TestEqual(TEXT("Compiled hash is unchanged"), Tree.Asset->LastCompiledEditorDataHash, BeforeCompiledHash);
    TestEqual(TEXT("Package dirty state is unchanged"), Tree.Asset->GetOutermost()->IsDirty(), bBeforeDirty);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FSalStateTreeExactIdentityFailClosedTest,
    "Loomle.Sal.StateTree.ExactIdentityFailsClosed",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSalStateTreeExactIdentityFailClosedTest::RunTest(const FString& Parameters)
{
    const FTestStateTree Tree = MakeStateTree(MakeTestPackage());
    const FSalResolvedTarget Target = ResolvedTarget(Tree);

    FSalQuery StaleQuery = Query(TEXT("state"));
    StaleQuery.Operation->SetStringField(
        TEXT("id"),
        FGuid(0x70000001, 0x70000002, 0x70000003, 0x70000004)
            .ToString(EGuidFormats::DigitsWithHyphensLower));
    TestTrue(
        TEXT("Stale State id fails closed"),
        HasError(FSalStateTreeInterface::Query(StaleQuery, Target)));

    Tree.Second->ID = Tree.First->ID;
    FSalQuery DuplicateQuery = Query(TEXT("state"));
    DuplicateQuery.Operation->SetStringField(
        TEXT("id"),
        Tree.First->ID.ToString(EGuidFormats::DigitsWithHyphensLower));
    TestTrue(
        TEXT("Duplicate State id fails closed"),
        HasError(FSalStateTreeInterface::Query(DuplicateQuery, Target)));

    FSalQuery InvalidQuery = Query(TEXT("state"));
    InvalidQuery.Operation->SetStringField(
        TEXT("id"),
        FGuid().ToString(EGuidFormats::DigitsWithHyphensLower));
    TestTrue(
        TEXT("Invalid native State id fails closed"),
        HasError(FSalStateTreeInterface::Query(InvalidQuery, Target)));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FSalStateTreeMalformedHierarchyIsBoundedTest,
    "Loomle.Sal.StateTree.MalformedHierarchyIsBounded",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSalStateTreeMalformedHierarchyIsBoundedTest::RunTest(const FString& Parameters)
{
    const FTestStateTree Tree = MakeStateTree(MakeTestPackage());
    const FSalResolvedTarget Target = ResolvedTarget(Tree);
    Tree.First->Children.Add(Tree.Root);

    FSalQuery TreeQuery = Query(TEXT("tree"));
    const TSharedPtr<FJsonObject> Result = FSalStateTreeInterface::Query(TreeQuery, Target);
    TestFalse(TEXT("Malformed hierarchy returns readable data with warnings"), HasError(Result));
    const TArray<FString> StateIds = CallIds(Result, TEXT("state"));
    TestEqual(TEXT("Each unique State is emitted once"), CallCount(Result, TEXT("state")), 3);
    TestEqual(TEXT("Only the State outside the cycle keeps a canonical id"), StateIds.Num(), 1);

    FSalQuery AmbiguousQuery = Query(TEXT("state"));
    AmbiguousQuery.Operation->SetStringField(
        TEXT("id"),
        Tree.Root->ID.ToString(EGuidFormats::DigitsWithHyphensLower));
    TestTrue(
        TEXT("Cycle-owned State exact reference fails closed"),
        HasError(FSalStateTreeInterface::Query(AmbiguousQuery, Target)));

    AmbiguousQuery.Operation->SetStringField(
        TEXT("id"),
        Tree.First->ID.ToString(EGuidFormats::DigitsWithHyphensLower));
    TestTrue(
        TEXT("Every State participating in the cycle fails closed"),
        HasError(FSalStateTreeInterface::Query(AmbiguousQuery, Target)));

    AmbiguousQuery.Operation->SetStringField(
        TEXT("id"),
        Tree.Second->ID.ToString(EGuidFormats::DigitsWithHyphensLower));
    TestFalse(
        TEXT("A State outside the cycle remains exact-readable"),
        HasError(FSalStateTreeInterface::Query(AmbiguousQuery, Target)));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FSalStateTreeSingleTaskIdentityTest,
    "Loomle.Sal.StateTree.SingleTaskInvalidIdentity",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSalStateTreeSingleTaskIdentityTest::RunTest(const FString& Parameters)
{
    const FTestStateTree Tree = MakeStateTree(MakeTestPackage());
    const FSalResolvedTarget Target = ResolvedTarget(Tree);
    Tree.Root->SingleTask.Node.InitializeAs<FStateTreeTaskBase>();
    Tree.Root->SingleTask.ID = FGuid();

    const TSharedPtr<FJsonObject> Result = FSalStateTreeInterface::Query(Query(TEXT("tree")), Target);
    TestFalse(TEXT("SingleTask with invalid identity remains readable"), HasError(Result));
    TestEqual(TEXT("Valid SingleTask Node is emitted"), CallCount(Result, TEXT("node")), 1);
    TestEqual(TEXT("Invalid SingleTask id is not emitted as canonical"), CallIds(Result, TEXT("node")).Num(), 0);
    TestTrue(
        TEXT("Invalid SingleTask identity is diagnosed"),
        HasDiagnosticContaining(Result, TEXT("invalid native id")));

    Tree.Root->SingleTask.Node.Reset();
    Tree.Root->SingleTask.ID = FGuid(0x80000001, 0x80000002, 0x80000003, 0x80000004);
    const TSharedPtr<FJsonObject> StaleResult = FSalStateTreeInterface::Query(Query(TEXT("tree")), Target);
    TestEqual(TEXT("Empty SingleTask with stale id is not emitted"), CallCount(StaleResult, TEXT("node")), 0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FSalStateTreeRejectsForeignStateTest,
    "Loomle.Sal.StateTree.RejectsForeignState",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSalStateTreeRejectsForeignStateTest::RunTest(const FString& Parameters)
{
    const FTestStateTree Tree = MakeStateTree(MakeTestPackage());
    const FTestStateTree Other = MakeStateTree(MakeTestPackage());
    Other.Root->ID = FGuid(0x90000001, 0x90000002, 0x90000003, 0x90000004);
    Tree.Root->Children.Add(Other.Root);
    const FSalResolvedTarget Target = ResolvedTarget(Tree);

    const TSharedPtr<FJsonObject> Result = FSalStateTreeInterface::Query(Query(TEXT("tree")), Target);
    TestFalse(TEXT("Foreign State is rejected without failing the whole read"), HasError(Result));
    TestEqual(TEXT("Foreign State is not emitted"), CallCount(Result, TEXT("state")), 3);
    TestTrue(
        TEXT("Foreign State rejection is diagnosed"),
        HasDiagnosticContaining(Result, TEXT("outside the bound UStateTreeEditorData")));

    FSalQuery ExactQuery = Query(TEXT("state"));
    ExactQuery.Operation->SetStringField(
        TEXT("id"),
        Other.Root->ID.ToString(EGuidFormats::DigitsWithHyphensLower));
    TestTrue(
        TEXT("Foreign State id is not exact-readable in this target"),
        HasError(FSalStateTreeInterface::Query(ExactQuery, Target)));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FSalStateTreeStatesCollectionTest,
    "Loomle.Sal.StateTree.StatesCollection",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSalStateTreeStatesCollectionTest::RunTest(const FString& Parameters)
{
    const FTestStateTree Tree = MakeStateTree(MakeTestPackage());
    const FSalResolvedTarget Target = ResolvedTarget(Tree);
    Tree.First->Name = FName(TEXT("Alpha Match"));
    Tree.First->Description = TEXT("Find this authored State by description");
    Tree.Second->Name = FName(TEXT("Beta Match"));

    FSalQuery Search = Query(TEXT("states"));
    Search.Operation->SetStringField(TEXT("text"), TEXT("match"));
    const TSharedPtr<FJsonObject> SearchResult = FSalStateTreeInterface::Query(Search, Target);
    TestFalse(TEXT("State search succeeds"), HasError(SearchResult));
    TestEqual(TEXT("State search declares one compact Asset owner"), CallCount(SearchResult, TEXT("asset")), 1);
    const TArray<FString> SearchIds = CallIds(SearchResult, TEXT("state"));
    TestEqual(TEXT("State search returns both name matches"), SearchIds.Num(), 2);
    if (SearchIds.Num() == 2)
    {
        TestEqual(
            TEXT("State search preserves first authored match"),
            SearchIds[0],
            Tree.First->ID.ToString(EGuidFormats::DigitsWithHyphensLower));
        TestEqual(
            TEXT("State search preserves second authored match"),
            SearchIds[1],
            Tree.Second->ID.ToString(EGuidFormats::DigitsWithHyphensLower));
    }

    FSalQuery DescriptionSearch = Query(TEXT("states"));
    DescriptionSearch.Operation->SetStringField(TEXT("text"), TEXT("DESCRIPTION"));
    const TArray<FString> DescriptionIds = CallIds(
        FSalStateTreeInterface::Query(DescriptionSearch, Target),
        TEXT("state"));
    TestEqual(TEXT("State search is case-insensitive and searches Description"), DescriptionIds.Num(), 1);
    if (DescriptionIds.Num() == 1)
    {
        TestEqual(
            TEXT("Description search returns the authored State"),
            DescriptionIds[0],
            Tree.First->ID.ToString(EGuidFormats::DigitsWithHyphensLower));
    }

    FSalQuery FirstPage = Query(TEXT("states"));
    FirstPage.PageLimit = 2;
    const TSharedPtr<FJsonObject> FirstPageResult = FSalStateTreeInterface::Query(FirstPage, Target);
    TestFalse(TEXT("First State page succeeds"), HasError(FirstPageResult));
    const TArray<FString> FirstPageIds = CallIds(FirstPageResult, TEXT("state"));
    TestEqual(TEXT("First State page honors its limit"), FirstPageIds.Num(), 2);
    if (FirstPageIds.Num() == 2)
    {
        TestEqual(
            TEXT("First page begins with the root"),
            FirstPageIds[0],
            Tree.Root->ID.ToString(EGuidFormats::DigitsWithHyphensLower));
        TestEqual(
            TEXT("First page continues in authored hierarchy order"),
            FirstPageIds[1],
            Tree.First->ID.ToString(EGuidFormats::DigitsWithHyphensLower));
    }
    const FString Cursor = NextCursor(FirstPageResult);
    TestFalse(TEXT("A truncated State page returns a next cursor"), Cursor.IsEmpty());

    FSalQuery SecondPage = FirstPage;
    SecondPage.PageAfter = Cursor;
    const TSharedPtr<FJsonObject> SecondPageResult = FSalStateTreeInterface::Query(SecondPage, Target);
    TestFalse(TEXT("Second State page succeeds"), HasError(SecondPageResult));
    const TArray<FString> SecondPageIds = CallIds(SecondPageResult, TEXT("state"));
    TestEqual(TEXT("Second State page returns the remainder"), SecondPageIds.Num(), 1);
    if (SecondPageIds.Num() == 1)
    {
        TestEqual(
            TEXT("Second page resumes at the exact authored offset"),
            SecondPageIds[0],
            Tree.Second->ID.ToString(EGuidFormats::DigitsWithHyphensLower));
    }
    TestTrue(TEXT("Final State page has no next cursor"), NextCursor(SecondPageResult).IsEmpty());

    FSalQuery MalformedCursor = FirstPage;
    MalformedCursor.PageAfter = TEXT("state_tree1:not-a-real-cursor:2");
    TestTrue(
        TEXT("Malformed State cursor fails closed"),
        HasError(FSalStateTreeInterface::Query(MalformedCursor, Target)));

    FSalQuery DifferentSearch = FirstPage;
    DifferentSearch.Operation = MakeShared<FJsonObject>();
    DifferentSearch.Operation->SetStringField(TEXT("kind"), TEXT("states"));
    DifferentSearch.Operation->SetStringField(TEXT("text"), TEXT("alpha"));
    DifferentSearch.PageAfter = Cursor;
    TestTrue(
        TEXT("State cursor cannot be reused for a different search"),
        HasError(FSalStateTreeInterface::Query(DifferentSearch, Target)));

    Tree.Second->Name = FName(TEXT("Authored State Changed"));
    FSalQuery StaleCursor = FirstPage;
    StaleCursor.PageAfter = Cursor;
    TestTrue(
        TEXT("State cursor expires when authored StateTree data changes"),
        HasError(FSalStateTreeInterface::Query(StaleCursor, Target)));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FSalStateTreeNodesCollectionAndExactReadTest,
    "Loomle.Sal.StateTree.NodesCollectionAndExactRead",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSalStateTreeNodesCollectionAndExactReadTest::RunTest(const FString& Parameters)
{
    const FTestStateTree Tree = MakeStateTree(MakeTestPackage());
    const FSalResolvedTarget Target = ResolvedTarget(Tree);

    const FGuid EvaluatorId(0x41000001, 0x41000002, 0x41000003, 0x41000004);
    const FGuid GlobalTaskId(0x42000001, 0x42000002, 0x42000003, 0x42000004);
    const FGuid EnterConditionId(0x43000001, 0x43000002, 0x43000003, 0x43000004);
    const FGuid TaskId(0x44000001, 0x44000002, 0x44000003, 0x44000004);
    const FGuid ConsiderationId(0x45000001, 0x45000002, 0x45000003, 0x45000004);
    const FGuid TransitionId(0x46000001, 0x46000002, 0x46000003, 0x46000004);
    const FGuid TransitionConditionId(0x47000001, 0x47000002, 0x47000003, 0x47000004);
    const FGuid ChildTaskId(0x48000001, 0x48000002, 0x48000003, 0x48000004);
    const FGuid SingleTaskId(0x49000001, 0x49000002, 0x49000003, 0x49000004);

    AddNamedNode<FStateTreeEvaluatorBase>(
        Tree.EditorData->Evaluators, EvaluatorId, FName(TEXT("Observe Target")));
    AddNamedNode<FStateTreeTaskBase>(
        Tree.EditorData->GlobalTasks, GlobalTaskId, FName(TEXT("Global Patrol")));
    FStateTreeEditorNode& EnterCondition = AddNamedNode<FStateTreeConditionBase>(
        Tree.Root->EnterConditions, EnterConditionId, FName(TEXT("Can Enter")));
    EnterCondition.Node.GetMutable<FStateTreeConditionBase>().EvaluationMode =
        EStateTreeConditionEvaluationMode::ForcedTrue;
    FStateTreeEditorNode& Task = AddNamedNode<FStateTreeTaskBase>(
        Tree.Root->Tasks, TaskId, FName(TEXT("Follow Target")));
    Task.Node.GetMutable<FStateTreeTaskBase>().bTaskEnabled = false;
#if WITH_EDITORONLY_DATA
    Task.Node.GetMutable<FStateTreeTaskBase>().bConsideredForCompletion = false;
#endif
    Task.InstanceObject = NewObject<UObject>(Tree.Asset);
    AddNamedNode<FStateTreeConsiderationBase>(
        Tree.Root->Considerations, ConsiderationId, FName(TEXT("Distance Score")));
    FStateTreeTransition& Transition = Tree.Root->AddTransition(
        EStateTreeTransitionTrigger::OnStateCompleted,
        EStateTreeTransitionType::Succeeded);
    Transition.ID = TransitionId;
    Transition.Priority = EStateTreeTransitionPriority::High;
    Transition.bDelayTransition = true;
    Transition.DelayDuration = 1.25f;
    SetNamedNode(
        Transition.AddCondition<FStateTreeConditionBase>(),
        TransitionConditionId,
        FName(TEXT("Transition Ready")));
    AddNamedNode<FStateTreeTaskBase>(
        Tree.First->Tasks, ChildTaskId, FName(TEXT("Child Follow")));
    Tree.Second->SingleTask.ID = SingleTaskId;
    Tree.Second->SingleTask.Node.InitializeAs<FStateTreeTaskBase>();
    Tree.Second->SingleTask.Node.GetMutable<FStateTreeTaskBase>().Name = FName(TEXT("Single Guard"));

    const TArray<FString> ExpectedOrder = {
        EvaluatorId.ToString(EGuidFormats::DigitsWithHyphensLower),
        GlobalTaskId.ToString(EGuidFormats::DigitsWithHyphensLower),
        EnterConditionId.ToString(EGuidFormats::DigitsWithHyphensLower),
        TaskId.ToString(EGuidFormats::DigitsWithHyphensLower),
        ConsiderationId.ToString(EGuidFormats::DigitsWithHyphensLower),
        TransitionConditionId.ToString(EGuidFormats::DigitsWithHyphensLower),
        ChildTaskId.ToString(EGuidFormats::DigitsWithHyphensLower),
        SingleTaskId.ToString(EGuidFormats::DigitsWithHyphensLower),
    };

    const TSharedPtr<FJsonObject> CollectionResult = FSalStateTreeInterface::Query(
        Query(TEXT("nodes")),
        Target);
    TestFalse(TEXT("Node collection succeeds"), HasError(CollectionResult));
    TestEqual(TEXT("Node collection declares one compact Asset owner"), CallCount(CollectionResult, TEXT("asset")), 1);
    const TArray<FString> NodeIds = CallIds(CollectionResult, TEXT("node"));
    TestEqual(TEXT("Node collection returns every direct authored Node"), NodeIds.Num(), ExpectedOrder.Num());
    if (NodeIds.Num() == ExpectedOrder.Num())
    {
        for (int32 Index = 0; Index < ExpectedOrder.Num(); ++Index)
        {
            TestEqual(
                *FString::Printf(TEXT("Node %d preserves UE authored role order"), Index),
                NodeIds[Index],
                ExpectedOrder[Index]);
        }
    }

    FSalQuery Search = Query(TEXT("nodes"));
    Search.Operation->SetStringField(TEXT("text"), TEXT("follow"));
    const TArray<FString> SearchIds = CallIds(
        FSalStateTreeInterface::Query(Search, Target),
        TEXT("node"));
    TestEqual(TEXT("Node name search returns both authored matches"), SearchIds.Num(), 2);
    if (SearchIds.Num() == 2)
    {
        TestEqual(
            TEXT("Node search preserves the parent State match first"),
            SearchIds[0],
            TaskId.ToString(EGuidFormats::DigitsWithHyphensLower));
        TestEqual(
            TEXT("Node search preserves the child State match second"),
            SearchIds[1],
            ChildTaskId.ToString(EGuidFormats::DigitsWithHyphensLower));
    }

    FSalQuery OwnerSearch = Query(TEXT("nodes"));
    OwnerSearch.Operation->SetStringField(TEXT("text"), TEXT("First"));
    const TArray<FString> OwnerSearchIds = CallIds(
        FSalStateTreeInterface::Query(OwnerSearch, Target),
        TEXT("node"));
    TestEqual(TEXT("Node search follows the owning State path"), OwnerSearchIds.Num(), 1);
    if (OwnerSearchIds.Num() == 1)
    {
        TestEqual(
            TEXT("Owner-path search returns the child Task"),
            OwnerSearchIds[0],
            ChildTaskId.ToString(EGuidFormats::DigitsWithHyphensLower));
    }

    FSalQuery RolePathSearch = Query(TEXT("nodes"));
    RolePathSearch.Operation->SetStringField(
        TEXT("text"),
        TEXT("Transitions[0].Conditions[0]"));
    const TArray<FString> RolePathSearchIds = CallIds(
        FSalStateTreeInterface::Query(RolePathSearch, Target),
        TEXT("node"));
    TestEqual(TEXT("Node search follows the complete native role path"), RolePathSearchIds.Num(), 1);
    if (RolePathSearchIds.Num() == 1)
    {
        TestEqual(
            TEXT("Transition Condition is discoverable by its role path"),
            RolePathSearchIds[0],
            TransitionConditionId.ToString(EGuidFormats::DigitsWithHyphensLower));
    }

    FSalQuery FirstPage = Query(TEXT("nodes"));
    FirstPage.PageLimit = 3;
    const TSharedPtr<FJsonObject> FirstPageResult = FSalStateTreeInterface::Query(FirstPage, Target);
    TestFalse(TEXT("First Node page succeeds"), HasError(FirstPageResult));
    const TArray<FString> FirstPageIds = CallIds(FirstPageResult, TEXT("node"));
    TestEqual(TEXT("First Node page honors its limit"), FirstPageIds.Num(), 3);
    const FString Cursor = NextCursor(FirstPageResult);
    TestFalse(TEXT("A truncated Node page returns a cursor"), Cursor.IsEmpty());
    FSalQuery SecondPage = FirstPage;
    SecondPage.PageAfter = Cursor;
    const TSharedPtr<FJsonObject> SecondPageResult = FSalStateTreeInterface::Query(SecondPage, Target);
    TestFalse(TEXT("Second Node page succeeds"), HasError(SecondPageResult));
    TestEqual(TEXT("Second Node page returns the remaining Nodes"), CallIds(SecondPageResult, TEXT("node")).Num(), 5);

    FSalQuery ExactNode = Query(TEXT("node"));
    ExactNode.Operation->SetStringField(TEXT("id"), TaskId.ToString(EGuidFormats::DigitsWithHyphensLower));
    const TSharedPtr<FJsonObject> ExactNodeResult = FSalStateTreeInterface::Query(ExactNode, Target);
    TestFalse(TEXT("Exact Node read succeeds"), HasError(ExactNodeResult));
    TestEqual(TEXT("Exact Node read declares one Asset owner"), CallCount(ExactNodeResult, TEXT("asset")), 1);
    TestEqual(TEXT("Exact Node read returns one Node"), CallCount(ExactNodeResult, TEXT("node")), 1);
    const TArray<FString> ExactNodeIds = CallIds(ExactNodeResult, TEXT("node"));
    TestEqual(TEXT("Exact Node read preserves its native id"), ExactNodeIds.Num(), 1);
    if (ExactNodeIds.Num() == 1)
    {
        TestEqual(
            TEXT("Exact Node identity remains native"),
            ExactNodeIds[0],
            TaskId.ToString(EGuidFormats::DigitsWithHyphensLower));
    }
    const TSharedPtr<FJsonObject> ExactNodeArgs = FirstCallArgs(ExactNodeResult, TEXT("node"));
    FString ExactNodeType;
    TestTrue(
        TEXT("A native Node keeps its selected UScriptStruct type when its instance data is a UObject"),
        ExactNodeArgs.IsValid()
            && ExactNodeArgs->TryGetStringField(TEXT("type"), ExactNodeType)
            && ExactNodeType == FStateTreeTaskBase::StaticStruct()->GetPathName());
    TestTrue(TEXT("Exact Node returns its native Node surface"), ExactNodeArgs.IsValid() && ExactNodeArgs->HasField(TEXT("Node")));
    TestTrue(
        TEXT("Derived display name is not synthesized as a top-level Node field"),
        ExactNodeArgs.IsValid() && !ExactNodeArgs->HasField(TEXT("Name")));
    const TSharedPtr<FJsonObject>* NativeNodeSurface = nullptr;
    TestTrue(
        TEXT("Exact Node native surface remains structured"),
        ExactNodeArgs.IsValid()
            && ExactNodeArgs->TryGetObjectField(TEXT("Node"), NativeNodeSurface)
            && NativeNodeSurface != nullptr
            && (*NativeNodeSurface)->HasField(TEXT("Name")));
    bool bTaskEnabled = true;
    TestTrue(
        TEXT("Exact Task preserves the custom editor-authored enabled flag"),
        NativeNodeSurface != nullptr
            && (*NativeNodeSurface)->TryGetBoolField(TEXT("bTaskEnabled"), bTaskEnabled)
            && !bTaskEnabled);
#if WITH_EDITORONLY_DATA
    bool bConsideredForCompletion = true;
    TestTrue(
        TEXT("Exact Task preserves the custom editor-authored completion flag"),
        NativeNodeSurface != nullptr
            && (*NativeNodeSurface)->TryGetBoolField(
                TEXT("bConsideredForCompletion"),
                bConsideredForCompletion)
            && !bConsideredForCompletion);
#endif

    FSalQuery ExactCondition = Query(TEXT("node"));
    ExactCondition.Operation->SetStringField(
        TEXT("id"),
        EnterConditionId.ToString(EGuidFormats::DigitsWithHyphensLower));
    const TSharedPtr<FJsonObject> ExactConditionArgs = FirstCallArgs(
        FSalStateTreeInterface::Query(ExactCondition, Target),
        TEXT("node"));
    const TSharedPtr<FJsonObject>* ConditionSurface = nullptr;
    const TSharedPtr<FJsonObject>* EvaluationMode = nullptr;
    FString EvaluationModeName;
    TestTrue(
        TEXT("Exact Condition preserves the custom editor-authored evaluation mode"),
        ExactConditionArgs.IsValid()
            && ExactConditionArgs->TryGetObjectField(TEXT("Node"), ConditionSurface)
            && ConditionSurface != nullptr
            && (*ConditionSurface)->TryGetObjectField(TEXT("EvaluationMode"), EvaluationMode)
            && EvaluationMode != nullptr
            && (*EvaluationMode)->TryGetStringField(TEXT("name"), EvaluationModeName)
            && EvaluationModeName == TEXT("ForcedTrue"));

    FSalQuery ExactSingleTask = Query(TEXT("node"));
    ExactSingleTask.Operation->SetStringField(
        TEXT("id"),
        SingleTaskId.ToString(EGuidFormats::DigitsWithHyphensLower));
    const TSharedPtr<FJsonObject> ExactSingleTaskResult = FSalStateTreeInterface::Query(
        ExactSingleTask,
        Target);
    TestFalse(TEXT("SingleTask remains exact-readable as node@id"), HasError(ExactSingleTaskResult));
    const TArray<FString> ExactSingleTaskIds = CallIds(ExactSingleTaskResult, TEXT("node"));
    TestEqual(TEXT("Exact SingleTask read returns one Node"), ExactSingleTaskIds.Num(), 1);
    if (ExactSingleTaskIds.Num() == 1)
    {
        TestEqual(
            TEXT("Exact SingleTask preserves its native id"),
            ExactSingleTaskIds[0],
            SingleTaskId.ToString(EGuidFormats::DigitsWithHyphensLower));
    }

    FSalQuery ExactTransition = Query(TEXT("transition"));
    ExactTransition.Operation->SetStringField(
        TEXT("id"),
        TransitionId.ToString(EGuidFormats::DigitsWithHyphensLower));
    const TSharedPtr<FJsonObject> ExactTransitionResult = FSalStateTreeInterface::Query(ExactTransition, Target);
    TestFalse(TEXT("Exact Transition read succeeds"), HasError(ExactTransitionResult));
    TestEqual(TEXT("Exact Transition read returns one Transition"), CallCount(ExactTransitionResult, TEXT("transition")), 1);
    const TArray<FString> ExactTransitionIds = CallIds(ExactTransitionResult, TEXT("transition"));
    TestEqual(TEXT("Exact Transition read preserves its native id"), ExactTransitionIds.Num(), 1);
    if (ExactTransitionIds.Num() == 1)
    {
        TestEqual(
            TEXT("Exact Transition identity remains native"),
            ExactTransitionIds[0],
            TransitionId.ToString(EGuidFormats::DigitsWithHyphensLower));
    }
    const TSharedPtr<FJsonObject> ExactTransitionArgs = FirstCallArgs(
        ExactTransitionResult,
        TEXT("transition"));
    TestTrue(
        TEXT("Exact Transition preserves native Trigger"),
        ExactTransitionArgs.IsValid() && ExactTransitionArgs->HasField(TEXT("Trigger")));
    TestTrue(
        TEXT("Exact Transition preserves native Priority"),
        ExactTransitionArgs.IsValid() && ExactTransitionArgs->HasField(TEXT("Priority")));
    TestTrue(
        TEXT("Exact Transition preserves native delay fields"),
        ExactTransitionArgs.IsValid()
            && ExactTransitionArgs->HasField(TEXT("bDelayTransition"))
            && ExactTransitionArgs->HasField(TEXT("DelayDuration")));
    const TArray<FString> OwnedConditionIds = CallIds(ExactTransitionResult, TEXT("node"));
    TestEqual(TEXT("Exact Transition returns its owned Conditions"), OwnedConditionIds.Num(), 1);
    if (OwnedConditionIds.Num() == 1)
    {
        TestEqual(
            TEXT("Owned Transition Condition keeps its native id"),
            OwnedConditionIds[0],
            TransitionConditionId.ToString(EGuidFormats::DigitsWithHyphensLower));
    }
    const TArray<FString> ExactTransitionCallees = CallCallees(ExactTransitionResult);
    TestTrue(
        TEXT("Exact Transition is emitted before its owned Condition"),
        ExactTransitionCallees.IndexOfByKey(TEXT("transition"))
            < ExactTransitionCallees.IndexOfByKey(TEXT("node")));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FSalStateTreeCollectionClauseValidationTest,
    "Loomle.Sal.StateTree.CollectionClauseValidation",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSalStateTreeCollectionClauseValidationTest::RunTest(const FString& Parameters)
{
    const FTestStateTree Tree = MakeStateTree(MakeTestPackage());
    const FSalResolvedTarget Target = ResolvedTarget(Tree);

    for (const FString& Kind : {TEXT("states"), TEXT("nodes"), TEXT("parameters")})
    {
        FSalQuery With = Query(Kind);
        With.With.Add(TEXT("schema"));
        TestTrue(
            *FString::Printf(TEXT("%s rejects with schema in this slice"), *Kind),
            HasError(FSalStateTreeInterface::Query(With, Target)));

        FSalQuery Where = Query(Kind);
        Where.Where = MakeShared<FJsonObject>();
        Where.Where->SetStringField(TEXT("kind"), TEXT("eq"));
        TestTrue(
            *FString::Printf(TEXT("%s rejects where"), *Kind),
            HasError(FSalStateTreeInterface::Query(Where, Target)));

        FSalQuery Order = Query(Kind);
        TSharedPtr<FJsonObject> OrderItem = MakeShared<FJsonObject>();
        OrderItem->SetStringField(TEXT("key"), TEXT("name"));
        OrderItem->SetStringField(TEXT("direction"), TEXT("asc"));
        Order.OrderBy.Add(OrderItem);
        TestTrue(
            *FString::Printf(TEXT("%s rejects order by"), *Kind),
            HasError(FSalStateTreeInterface::Query(Order, Target)));
    }

    FSalQuery ExactNodePage = Query(TEXT("node"));
    ExactNodePage.Operation->SetStringField(
        TEXT("id"),
        FGuid(0xA1000001, 0xA1000002, 0xA1000003, 0xA1000004)
            .ToString(EGuidFormats::DigitsWithHyphensLower));
    ExactNodePage.PageLimit = 1;
    TestTrue(
        TEXT("Exact Node read rejects page clauses"),
        HasError(FSalStateTreeInterface::Query(ExactNodePage, Target)));

    FSalQuery ExactParameterPage = Query(TEXT("parameter"));
    ExactParameterPage.Operation->SetStringField(
        TEXT("id"),
        ParameterId(
            Tree.EditorData->GetRootParametersGuid(),
            FGuid(0xA2000001, 0xA2000002, 0xA2000003, 0xA2000004)));
    ExactParameterPage.PageLimit = 1;
    TestTrue(
        TEXT("Exact Parameter read rejects page clauses"),
        HasError(FSalStateTreeInterface::Query(ExactParameterPage, Target)));

    FSalQuery ExactContextWhere = Query(TEXT("object"));
    ExactContextWhere.Operation->SetStringField(
        TEXT("id"),
        FGuid(0xA3000001, 0xA3000002, 0xA3000003, 0xA3000004)
            .ToString(EGuidFormats::DigitsWithHyphensLower));
    ExactContextWhere.Where = MakeShared<FJsonObject>();
    ExactContextWhere.Where->SetStringField(TEXT("kind"), TEXT("eq"));
    TestTrue(
        TEXT("Exact Context Data read rejects where"),
        HasError(FSalStateTreeInterface::Query(ExactContextWhere, Target)));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FSalStateTreeNodeAndTransitionIdentityFailClosedTest,
    "Loomle.Sal.StateTree.NodeAndTransitionIdentityFailClosed",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSalStateTreeNodeAndTransitionIdentityFailClosedTest::RunTest(const FString& Parameters)
{
    const FTestStateTree Tree = MakeStateTree(MakeTestPackage());
    const FSalResolvedTarget Target = ResolvedTarget(Tree);
    const FGuid DuplicateNodeId(0xB1000001, 0xB1000002, 0xB1000003, 0xB1000004);
    AddNamedNode<FStateTreeTaskBase>(
        Tree.Root->Tasks, DuplicateNodeId, FName(TEXT("Duplicate A")));
    AddNamedNode<FStateTreeTaskBase>(
        Tree.First->Tasks, DuplicateNodeId, FName(TEXT("Duplicate B")));

    FSalQuery DuplicateNode = Query(TEXT("node"));
    DuplicateNode.Operation->SetStringField(
        TEXT("id"),
        DuplicateNodeId.ToString(EGuidFormats::DigitsWithHyphensLower));
    TestTrue(
        TEXT("Duplicate Node id fails closed"),
        HasError(FSalStateTreeInterface::Query(DuplicateNode, Target)));

    FSalQuery StaleNode = Query(TEXT("node"));
    StaleNode.Operation->SetStringField(
        TEXT("id"),
        FGuid(0xB2000001, 0xB2000002, 0xB2000003, 0xB2000004)
            .ToString(EGuidFormats::DigitsWithHyphensLower));
    TestTrue(
        TEXT("Stale Node id fails closed"),
        HasError(FSalStateTreeInterface::Query(StaleNode, Target)));

    FSalQuery InvalidNode = Query(TEXT("node"));
    InvalidNode.Operation->SetStringField(TEXT("id"), FGuid().ToString(EGuidFormats::DigitsWithHyphensLower));
    TestTrue(
        TEXT("Invalid native Node id fails closed"),
        HasError(FSalStateTreeInterface::Query(InvalidNode, Target)));

    const FGuid DuplicateTransitionId(0xB3000001, 0xB3000002, 0xB3000003, 0xB3000004);
    FStateTreeTransition& FirstTransition = Tree.Root->AddTransition(
        EStateTreeTransitionTrigger::OnTick,
        EStateTreeTransitionType::Succeeded);
    FirstTransition.ID = DuplicateTransitionId;
    FStateTreeTransition& SecondTransition = Tree.First->AddTransition(
        EStateTreeTransitionTrigger::OnTick,
        EStateTreeTransitionType::Failed);
    SecondTransition.ID = DuplicateTransitionId;

    FSalQuery DuplicateTransition = Query(TEXT("transition"));
    DuplicateTransition.Operation->SetStringField(
        TEXT("id"),
        DuplicateTransitionId.ToString(EGuidFormats::DigitsWithHyphensLower));
    TestTrue(
        TEXT("Duplicate Transition id fails closed"),
        HasError(FSalStateTreeInterface::Query(DuplicateTransition, Target)));

    FSalQuery StaleTransition = Query(TEXT("transition"));
    StaleTransition.Operation->SetStringField(
        TEXT("id"),
        FGuid(0xB4000001, 0xB4000002, 0xB4000003, 0xB4000004)
            .ToString(EGuidFormats::DigitsWithHyphensLower));
    TestTrue(
        TEXT("Stale Transition id fails closed"),
        HasError(FSalStateTreeInterface::Query(StaleTransition, Target)));

    FSalQuery InvalidTransition = Query(TEXT("transition"));
    InvalidTransition.Operation->SetStringField(
        TEXT("id"),
        FGuid().ToString(EGuidFormats::DigitsWithHyphensLower));
    TestTrue(
        TEXT("Invalid native Transition id fails closed"),
        HasError(FSalStateTreeInterface::Query(InvalidTransition, Target)));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FSalStateTreeBlueprintNodeTypeTest,
    "Loomle.Sal.StateTree.BlueprintNodeType",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSalStateTreeBlueprintNodeTypeTest::RunTest(const FString& Parameters)
{
    const FTestStateTree Tree = MakeStateTree(MakeTestPackage());
    const FSalResolvedTarget Target = ResolvedTarget(Tree);
    const FGuid TaskId(0xB5000001, 0xB5000002, 0xB5000003, 0xB5000004);
    FStateTreeEditorNode& Task = AddNamedNode<FStateTreeBlueprintTaskWrapper>(
        Tree.Root->Tasks,
        TaskId,
        FName(TEXT("Blueprint Task")));
    Task.Node.GetMutable<FStateTreeBlueprintTaskWrapper>().TaskClass =
        UStateTreeTaskBlueprintBase::StaticClass();
    Task.InstanceObject = NewObject<UObject>(Tree.Asset);

    FSalQuery Exact = Query(TEXT("node"));
    Exact.Operation->SetStringField(
        TEXT("id"),
        TaskId.ToString(EGuidFormats::DigitsWithHyphensLower));
    const TSharedPtr<FJsonObject> Result = FSalStateTreeInterface::Query(Exact, Target);
    TestFalse(TEXT("Malformed Blueprint wrapper remains inspectable"), HasError(Result));
    const TSharedPtr<FJsonObject> Args = FirstCallArgs(Result, TEXT("node"));
    FString Type;
    TestTrue(
        TEXT("Blueprint wrapper type is the selected Blueprint Class rather than the storage wrapper or InstanceObject"),
        Args.IsValid()
            && Args->TryGetStringField(TEXT("type"), Type)
            && Type == UStateTreeTaskBlueprintBase::StaticClass()->GetPathName());
    TestTrue(
        TEXT("Blueprint wrapper and InstanceObject Class mismatch is diagnosed without repair"),
        HasDiagnosticContaining(Result, TEXT("stores InstanceObject Class")));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FSalStateTreePropertyFunctionIdentityTest,
    "Loomle.Sal.StateTree.PropertyFunctionIdentity",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSalStateTreePropertyFunctionIdentityTest::RunTest(const FString& Parameters)
{
    const FTestStateTree Tree = MakeStateTree(MakeTestPackage());
    const FSalResolvedTarget Target = ResolvedTarget(Tree);
    const FGuid TaskId(0xC1000001, 0xC1000002, 0xC1000003, 0xC1000004);
    AddNamedNode<FStateTreeTaskBase>(Tree.Root->Tasks, TaskId, FName(TEXT("Binding Target")));

    UScriptStruct* PropertyFunctionStruct = FindObject<UScriptStruct>(
        nullptr,
        TEXT("/Script/StateTreeModule.StateTreeAddFloatPropertyFunction"));
    if (!TestNotNull(TEXT("Built-in native Property Function type is registered"), PropertyFunctionStruct))
    {
        return false;
    }
    Tree.EditorData->AddPropertyBinding(
        PropertyFunctionStruct,
        {FPropertyBindingPathSegment(TEXT("Result"))},
        FPropertyBindingPath(TaskId, TEXT("bTaskEnabled")));
    const TConstArrayView<FStateTreePropertyPathBinding> Bindings =
        Tree.EditorData->GetPropertyEditorBindings()->GetBindings();
    TestEqual(TEXT("Property Function binding was authored"), Bindings.Num(), 1);
    if (Bindings.Num() != 1)
    {
        return false;
    }
    const FConstStructView PropertyFunctionView = Bindings[0].GetPropertyFunctionNode();
    const FStateTreeEditorNode* PropertyFunction = PropertyFunctionView.GetPtr<const FStateTreeEditorNode>();
    if (!TestNotNull(TEXT("Property Function binding owns an editor Node"), PropertyFunction))
    {
        return false;
    }

    const TSharedPtr<FJsonObject> CollectionResult = FSalStateTreeInterface::Query(
        Query(TEXT("nodes")),
        Target);
    TestFalse(TEXT("Node collection with a Property Function succeeds"), HasError(CollectionResult));
    const TArray<FString> CollectionIds = CallIds(CollectionResult, TEXT("node"));
    TestEqual(TEXT("Property Function is excluded from direct Node discovery"), CollectionIds.Num(), 1);
    if (CollectionIds.Num() == 1)
    {
        TestEqual(
            TEXT("Direct Node remains discoverable"),
            CollectionIds[0],
            TaskId.ToString(EGuidFormats::DigitsWithHyphensLower));
    }

    FSalQuery ExactPropertyFunction = Query(TEXT("node"));
    ExactPropertyFunction.Operation->SetStringField(
        TEXT("id"),
        PropertyFunction->ID.ToString(EGuidFormats::DigitsWithHyphensLower));
    const TSharedPtr<FJsonObject> ExactResult = FSalStateTreeInterface::Query(ExactPropertyFunction, Target);
    TestFalse(TEXT("Property Function remains exact-readable as node@id"), HasError(ExactResult));
    const TArray<FString> ExactIds = CallIds(ExactResult, TEXT("node"));
    TestEqual(TEXT("Exact Property Function read returns one Node"), ExactIds.Num(), 1);
    if (ExactIds.Num() == 1)
    {
        TestEqual(
            TEXT("Exact Property Function identity remains native"),
            ExactIds[0],
            PropertyFunction->ID.ToString(EGuidFormats::DigitsWithHyphensLower));
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FSalStateTreeParametersCollectionAndExactReadTest,
    "Loomle.Sal.StateTree.ParametersCollectionAndExactRead",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSalStateTreeParametersCollectionAndExactReadTest::RunTest(const FString& Parameters)
{
    const FTestStateTree Tree = MakeStateTree(MakeTestPackage());
    const FSalResolvedTarget Target = ResolvedTarget(Tree);
    const FTestParameterIds Ids = AddTestParameters(Tree);
    const FGuid RootContainer = Tree.EditorData->GetRootParametersGuid();

    const TArray<FString> ExpectedOrder = {
        ParameterId(RootContainer, Ids.GlobalSpeed),
        ParameterId(RootContainer, Ids.Shared),
        ParameterId(Tree.Root->Parameters.ID, Ids.RootStateThreshold),
        ParameterId(Tree.First->Parameters.ID, Ids.ChildLabel),
        ParameterId(Tree.First->Parameters.ID, Ids.Shared),
        ParameterId(Tree.Second->Parameters.ID, Ids.ChildLabel),
        ParameterId(Tree.Second->Parameters.ID, Ids.Shared),
    };
    const TSharedPtr<FJsonObject> CollectionResult = FSalStateTreeInterface::Query(
        Query(TEXT("parameters")),
        Target);
    TestFalse(TEXT("Parameter collection succeeds"), HasError(CollectionResult));
    TestEqual(TEXT("Parameter collection declares one compact Asset owner"), CallCount(CollectionResult, TEXT("asset")), 1);
    const TArray<FString> ParameterIds = CallIds(CollectionResult, TEXT("parameter"));
    TestEqual(TEXT("Parameter collection returns every Property Bag descriptor"), ParameterIds.Num(), ExpectedOrder.Num());
    if (ParameterIds.Num() == ExpectedOrder.Num())
    {
        for (int32 Index = 0; Index < ExpectedOrder.Num(); ++Index)
        {
            TestEqual(
                *FString::Printf(TEXT("Parameter %d preserves root then State preorder and descriptor order"), Index),
                ParameterIds[Index],
                ExpectedOrder[Index]);
        }
    }

    struct FSearchExpectation
    {
        const TCHAR* Text;
        TArray<FString> ExpectedIds;
    };
    const TArray<FSearchExpectation> Searches = {
        {TEXT("globalspeed"), {ExpectedOrder[0]}},
        {TEXT("FloatProperty"), {ExpectedOrder[0]}},
        {TEXT("SecondMeta"), {ExpectedOrder[0]}},
        {TEXT("BetaMetaValue"), {ExpectedOrder[0]}},
        {TEXT("Root/First"), {ExpectedOrder[3], ExpectedOrder[4]}},
    };
    for (const FSearchExpectation& SearchExpectation : Searches)
    {
        FSalQuery Search = Query(TEXT("parameters"));
        Search.Operation->SetStringField(TEXT("text"), SearchExpectation.Text);
        const TSharedPtr<FJsonObject> SearchResult = FSalStateTreeInterface::Query(Search, Target);
        TestFalse(
            *FString::Printf(TEXT("Parameter search '%s' succeeds"), SearchExpectation.Text),
            HasError(SearchResult));
        const TArray<FString> SearchIds = CallIds(SearchResult, TEXT("parameter"));
        TestEqual(
            *FString::Printf(TEXT("Parameter search '%s' returns the expected count"), SearchExpectation.Text),
            SearchIds.Num(),
            SearchExpectation.ExpectedIds.Num());
        if (SearchIds.Num() == SearchExpectation.ExpectedIds.Num())
        {
            for (int32 Index = 0; Index < SearchIds.Num(); ++Index)
            {
                TestEqual(
                    *FString::Printf(TEXT("Parameter search '%s' preserves authored match %d"), SearchExpectation.Text, Index),
                    SearchIds[Index],
                    SearchExpectation.ExpectedIds[Index]);
            }
        }
    }

    FSalQuery FirstPage = Query(TEXT("parameters"));
    FirstPage.PageLimit = 2;
    const TSharedPtr<FJsonObject> FirstPageResult = FSalStateTreeInterface::Query(FirstPage, Target);
    TestFalse(TEXT("First Parameter page succeeds"), HasError(FirstPageResult));
    const TArray<FString> FirstPageIds = CallIds(FirstPageResult, TEXT("parameter"));
    TestEqual(TEXT("First Parameter page honors its limit"), FirstPageIds.Num(), 2);
    if (FirstPageIds.Num() == 2)
    {
        TestEqual(TEXT("First Parameter page begins at the first root descriptor"), FirstPageIds[0], ExpectedOrder[0]);
        TestEqual(TEXT("First Parameter page preserves root descriptor order"), FirstPageIds[1], ExpectedOrder[1]);
    }
    const FString Cursor = NextCursor(FirstPageResult);
    TestFalse(TEXT("A truncated Parameter page returns a next cursor"), Cursor.IsEmpty());
    FSalQuery SecondPage = FirstPage;
    SecondPage.PageAfter = Cursor;
    const TSharedPtr<FJsonObject> SecondPageResult = FSalStateTreeInterface::Query(SecondPage, Target);
    TestFalse(TEXT("Second Parameter page succeeds"), HasError(SecondPageResult));
    const TArray<FString> SecondPageIds = CallIds(SecondPageResult, TEXT("parameter"));
    TestEqual(TEXT("Second Parameter page returns the next descriptor pair"), SecondPageIds.Num(), 2);
    if (SecondPageIds.Num() == 2)
    {
        TestEqual(TEXT("Second Parameter page resumes at the exact offset"), SecondPageIds[0], ExpectedOrder[2]);
        TestEqual(TEXT("Second Parameter page preserves State preorder"), SecondPageIds[1], ExpectedOrder[3]);
    }

    FSalQuery DifferentSearch = FirstPage;
    DifferentSearch.Operation = MakeShared<FJsonObject>();
    DifferentSearch.Operation->SetStringField(TEXT("kind"), TEXT("parameters"));
    DifferentSearch.Operation->SetStringField(TEXT("text"), TEXT("Global"));
    DifferentSearch.PageAfter = Cursor;
    TestTrue(
        TEXT("A Parameter cursor cannot be reused for a different search"),
        HasError(FSalStateTreeInterface::Query(DifferentSearch, Target)));
    FInstancedPropertyBag& RootBag = const_cast<FInstancedPropertyBag&>(
        Tree.EditorData->GetRootParametersPropertyBag());
    RootBag.SetValueFloat(TEXT("GlobalSpeed"), 126.5f);
    FSalQuery StaleCursor = FirstPage;
    StaleCursor.PageAfter = Cursor;
    TestTrue(
        TEXT("A Parameter cursor expires when authored values change"),
        HasError(FSalStateTreeInterface::Query(StaleCursor, Target)));

    FSalQuery ExactRoot = Query(TEXT("parameter"));
    ExactRoot.Operation->SetStringField(TEXT("id"), ExpectedOrder[0]);
    const TSharedPtr<FJsonObject> ExactRootResult = FSalStateTreeInterface::Query(ExactRoot, Target);
    TestFalse(TEXT("Exact root Parameter succeeds"), HasError(ExactRootResult));
    TestEqual(TEXT("Exact root Parameter emits one Parameter"), CallCount(ExactRootResult, TEXT("parameter")), 1);
    const TSharedPtr<FJsonObject> ExactRootArgs = FirstCallArgs(ExactRootResult, TEXT("parameter"));
    FString RootType;
    FString RootFlags;
    double RootValue = 0.0;
    TestTrue(
        TEXT("Exact root Parameter uses native FProperty type text"),
        ExactRootArgs.IsValid()
            && ExactRootArgs->TryGetStringField(TEXT("type"), RootType)
            && RootType == TEXT("FloatProperty"));
    TestTrue(
        TEXT("Exact root Parameter preserves the current reflected value"),
        ExactRootArgs.IsValid()
            && ExactRootArgs->TryGetNumberField(TEXT("Value"), RootValue)
            && FMath::IsNearlyEqual(RootValue, 126.5));
    TestTrue(
        TEXT("Exact root Parameter preserves native PropertyFlags text"),
        ExactRootArgs.IsValid()
            && ExactRootArgs->TryGetStringField(TEXT("PropertyFlags"), RootFlags)
            && RootFlags.Contains(TEXT("CPF_Edit"))
            && RootFlags.Contains(TEXT("CPF_BlueprintVisible")));
    TestTrue(
        TEXT("Exact root Parameter preserves its native Name"),
        NativeNameEquals(ExactRootArgs, TEXT("Name"), TEXT("GlobalSpeed")));
    const TArray<TSharedPtr<FJsonValue>>* MetaData = nullptr;
    TestTrue(
        TEXT("Exact root Parameter returns ordered descriptor metadata"),
        ExactRootArgs.IsValid()
            && ExactRootArgs->TryGetArrayField(TEXT("MetaData"), MetaData)
            && MetaData != nullptr
            && MetaData->Num() == 2);
    if (MetaData != nullptr && MetaData->Num() == 2)
    {
        const TSharedPtr<FJsonObject>* FirstMeta = nullptr;
        const TSharedPtr<FJsonObject>* SecondMeta = nullptr;
        FString FirstValue;
        FString SecondValue;
        TestTrue(
            TEXT("First Parameter metadata entry remains first"),
            (*MetaData)[0]->TryGetObject(FirstMeta)
                && FirstMeta != nullptr
                && NativeNameEquals(*FirstMeta, TEXT("Key"), TEXT("FirstMeta"))
                && (*FirstMeta)->TryGetStringField(TEXT("Value"), FirstValue)
                && FirstValue == TEXT("AlphaMetaValue"));
        TestTrue(
            TEXT("Second Parameter metadata entry remains second"),
            (*MetaData)[1]->TryGetObject(SecondMeta)
                && SecondMeta != nullptr
                && NativeNameEquals(*SecondMeta, TEXT("Key"), TEXT("SecondMeta"))
                && (*SecondMeta)->TryGetStringField(TEXT("Value"), SecondValue)
                && SecondValue == TEXT("BetaMetaValue"));
    }
    TestTrue(TEXT("Exact root Parameter identifies root ownership"), HasCommentContaining(ExactRootResult, TEXT("owner: root parameters")));

    FSalQuery ExactState = Query(TEXT("parameter"));
    ExactState.Operation->SetStringField(TEXT("id"), ExpectedOrder[3]);
    const TSharedPtr<FJsonObject> ExactStateResult = FSalStateTreeInterface::Query(ExactState, Target);
    TestFalse(TEXT("Exact State Parameter succeeds"), HasError(ExactStateResult));
    const TSharedPtr<FJsonObject> ExactStateArgs = FirstCallArgs(ExactStateResult, TEXT("parameter"));
    FString StateValue;
    TestTrue(
        TEXT("Exact State Parameter preserves its string Value"),
        ExactStateArgs.IsValid()
            && ExactStateArgs->TryGetStringField(TEXT("Value"), StateValue)
            && StateValue == TEXT("Scout"));
    TestTrue(TEXT("Exact State Parameter identifies its owner State"), HasCommentContaining(ExactStateResult, TEXT("owner state:")));
    TestTrue(TEXT("Exact State Parameter reports its owner path"), HasCommentContaining(ExactStateResult, TEXT("state path: Root/First")));
    TestTrue(TEXT("Exact State Parameter reports fixed layout"), HasCommentContaining(ExactStateResult, TEXT("bFixedLayout: true")));
    TestTrue(TEXT("Exact State Parameter reports its explicit override"), HasCommentContaining(ExactStateResult, TEXT("value source: local override")));

    FSalQuery ExactSharedRoot = Query(TEXT("parameter"));
    ExactSharedRoot.Operation->SetStringField(TEXT("id"), ExpectedOrder[1]);
    const TSharedPtr<FJsonObject> ExactSharedRootArgs = FirstCallArgs(
        FSalStateTreeInterface::Query(ExactSharedRoot, Target),
        TEXT("parameter"));
    FSalQuery ExactSharedState = Query(TEXT("parameter"));
    ExactSharedState.Operation->SetStringField(TEXT("id"), ExpectedOrder[4]);
    const TSharedPtr<FJsonObject> ExactSharedStateResult = FSalStateTreeInterface::Query(ExactSharedState, Target);
    const TSharedPtr<FJsonObject> ExactSharedStateArgs = FirstCallArgs(ExactSharedStateResult, TEXT("parameter"));
    bool bInheritedSharedValue = true;
    TestTrue(
        TEXT("The same Property Guid remains exact in the root container"),
        NativeNameEquals(ExactSharedRootArgs, TEXT("Name"), TEXT("GlobalCount")));
    TestTrue(
        TEXT("The same Property Guid remains exact in a State container"),
        NativeNameEquals(ExactSharedStateArgs, TEXT("Name"), TEXT("SharedInFirst")));
    TestTrue(
        TEXT("The linked Parameter keeps the Subtree's inherited value"),
        ExactSharedStateArgs.IsValid()
            && ExactSharedStateArgs->TryGetBoolField(TEXT("Value"), bInheritedSharedValue)
            && !bInheritedSharedValue);
    TestTrue(
        TEXT("Fixed-layout value without an override is identified as inherited"),
        HasCommentContaining(ExactSharedStateResult, TEXT("value source: inherited")));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FSalStateTreeParameterNativeValueTest,
    "Loomle.Sal.StateTree.ParameterNativeValue",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSalStateTreeParameterNativeValueTest::RunTest(const FString& Parameters)
{
    const FTestStateTree Tree = MakeStateTree(MakeTestPackage());
    const FSalResolvedTarget Target = ResolvedTarget(Tree);
    const FGuid TextId(0xD5000001, 0xD5000002, 0xD5000003, 0xD5000004);
    const FGuid LargeUnsignedId(0xD6000001, 0xD6000002, 0xD6000003, 0xD6000004);
    const FGuid OversizedId(0xD7000001, 0xD7000002, 0xD7000003, 0xD7000004);
    FInstancedPropertyBag& RootBag = const_cast<FInstancedPropertyBag&>(
        Tree.EditorData->GetRootParametersPropertyBag());
    RootBag.AddProperties({
        ParameterDesc(TEXT("InvariantText"), EPropertyBagPropertyType::Text, TextId),
        ParameterDesc(TEXT("LargeUnsigned"), EPropertyBagPropertyType::UInt64, LargeUnsignedId),
        ParameterDesc(TEXT("OversizedValue"), EPropertyBagPropertyType::String, OversizedId),
    });
    RootBag.SetValueText(TEXT("InvariantText"), FText::AsCultureInvariant(TEXT("Invariant Greeting")));
    RootBag.SetValueUInt64(TEXT("LargeUnsigned"), 9007199254740993ULL);
    RootBag.SetValueString(TEXT("OversizedValue"), FString::ChrN(1024 * 1024 + 1, TEXT('x')));

    const FGuid ContainerId = Tree.EditorData->GetRootParametersGuid();
    FSalQuery ExactText = Query(TEXT("parameter"));
    ExactText.Operation->SetStringField(TEXT("id"), ParameterId(ContainerId, TextId));
    const TSharedPtr<FJsonObject> ExactTextResult = FSalStateTreeInterface::Query(ExactText, Target);
    const TSharedPtr<FJsonObject> ExactTextArgs = FirstCallArgs(ExactTextResult, TEXT("parameter"));
    FString TextValue;
    TestFalse(TEXT("Exact FText Parameter succeeds"), HasError(ExactTextResult));
    TestTrue(
        TEXT("Exact FText Parameter preserves UE culture-invariant native text"),
        ExactTextArgs.IsValid()
            && ExactTextArgs->TryGetStringField(TEXT("Value"), TextValue)
            && TextValue == TEXT("INVTEXT(\"Invariant Greeting\")"));

    FSalQuery ExactLargeUnsigned = Query(TEXT("parameter"));
    ExactLargeUnsigned.Operation->SetStringField(
        TEXT("id"),
        ParameterId(ContainerId, LargeUnsignedId));
    const TSharedPtr<FJsonObject> ExactLargeUnsignedResult = FSalStateTreeInterface::Query(
        ExactLargeUnsigned,
        Target);
    const TSharedPtr<FJsonObject> ExactLargeUnsignedArgs = FirstCallArgs(
        ExactLargeUnsignedResult,
        TEXT("parameter"));
    FString LargeUnsignedValue;
    TestFalse(TEXT("Exact UInt64 Parameter succeeds"), HasError(ExactLargeUnsignedResult));
    TestTrue(
        TEXT("UInt64 above JSON's exact integer range remains UE native text"),
        ExactLargeUnsignedArgs.IsValid()
            && ExactLargeUnsignedArgs->TryGetStringField(TEXT("Value"), LargeUnsignedValue)
            && LargeUnsignedValue == TEXT("9007199254740993"));

    FSalQuery ExactOversized = Query(TEXT("parameter"));
    ExactOversized.Operation->SetStringField(TEXT("id"), ParameterId(ContainerId, OversizedId));
    const TSharedPtr<FJsonObject> ExactOversizedResult = FSalStateTreeInterface::Query(
        ExactOversized,
        Target);
    TestTrue(TEXT("An oversized exact Parameter fails instead of truncating Value"), HasError(ExactOversizedResult));
    TestTrue(
        TEXT("Oversized exact Parameter reports the hard result limit"),
        HasDiagnosticContaining(ExactOversizedResult, TEXT("above the hard limit")));
    TestEqual(
        TEXT("Oversized exact Parameter returns no partial Object Text"),
        CallCount(ExactOversizedResult, TEXT("parameter")),
        0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FSalStateTreeParameterIdentityFailClosedTest,
    "Loomle.Sal.StateTree.ParameterIdentityFailClosed",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSalStateTreeParameterIdentityFailClosedTest::RunTest(const FString& Parameters)
{
    const FTestStateTree Tree = MakeStateTree(MakeTestPackage());
    const FSalResolvedTarget Target = ResolvedTarget(Tree);
    const FTestParameterIds Ids = AddTestParameters(Tree);

    FSalQuery Malformed = Query(TEXT("parameter"));
    Malformed.Operation->SetStringField(TEXT("id"), TEXT("not-a-compound-id"));
    TestTrue(TEXT("A malformed Parameter compound id fails closed"), HasError(FSalStateTreeInterface::Query(Malformed, Target)));

    FSalQuery Stale = Query(TEXT("parameter"));
    Stale.Operation->SetStringField(
        TEXT("id"),
        ParameterId(
            Tree.First->Parameters.ID,
            FGuid(0xDE000001, 0xDE000002, 0xDE000003, 0xDE000004)));
    TestTrue(TEXT("A stale Property Guid fails closed"), HasError(FSalStateTreeInterface::Query(Stale, Target)));

    FSalQuery CrossContainer = Query(TEXT("parameter"));
    CrossContainer.Operation->SetStringField(
        TEXT("id"),
        ParameterId(Tree.First->Parameters.ID, Ids.GlobalSpeed));
    TestTrue(TEXT("A Property Guid cannot be paired with a foreign container"), HasError(FSalStateTreeInterface::Query(CrossContainer, Target)));

    const FTestStateTree Other = MakeStateTree(MakeTestPackage());
    AddTestParameters(Other);
    FSalQuery CrossAsset = Query(TEXT("parameter"));
    CrossAsset.Operation->SetStringField(
        TEXT("id"),
        ParameterId(Other.EditorData->GetRootParametersGuid(), Ids.GlobalSpeed));
    TestTrue(TEXT("A Parameter identity from another StateTree fails in this target"), HasError(FSalStateTreeInterface::Query(CrossAsset, Target)));

    const FTestStateTree InvalidOwnerTree = MakeStateTree(MakeTestPackage());
    const FSalResolvedTarget InvalidOwnerTarget = ResolvedTarget(InvalidOwnerTree);
    AddTestParameters(InvalidOwnerTree);
    InvalidOwnerTree.First->ID.Invalidate();
    FSalQuery InvalidOwner = Query(TEXT("parameter"));
    InvalidOwner.Operation->SetStringField(
        TEXT("id"),
        ParameterId(InvalidOwnerTree.First->Parameters.ID, Ids.ChildLabel));
    TestFalse(
        TEXT("A canonical Parameter remains exact when only its owner State id is invalid"),
        HasError(FSalStateTreeInterface::Query(InvalidOwner, InvalidOwnerTarget)));

    const FTestStateTree DuplicateOwnerTree = MakeStateTree(MakeTestPackage());
    const FSalResolvedTarget DuplicateOwnerTarget = ResolvedTarget(DuplicateOwnerTree);
    AddTestParameters(DuplicateOwnerTree);
    DuplicateOwnerTree.Second->ID = DuplicateOwnerTree.First->ID;
    FSalQuery DuplicateOwner = Query(TEXT("parameter"));
    DuplicateOwner.Operation->SetStringField(
        TEXT("id"),
        ParameterId(DuplicateOwnerTree.First->Parameters.ID, Ids.ChildLabel));
    TestFalse(
        TEXT("A canonical Parameter remains exact when its owner State id is duplicated"),
        HasError(FSalStateTreeInterface::Query(DuplicateOwner, DuplicateOwnerTarget)));

    const FTestStateTree DuplicateTree = MakeStateTree(MakeTestPackage());
    const FSalResolvedTarget DuplicateTarget = ResolvedTarget(DuplicateTree);
    const FGuid DuplicateId(0xDF000001, 0xDF000002, 0xDF000003, 0xDF000004);
    DuplicateTree.Root->Parameters.Parameters.AddProperties({
        ParameterDesc(TEXT("DuplicateA"), EPropertyBagPropertyType::Bool, DuplicateId),
        ParameterDesc(
            TEXT("DuplicateB"),
            EPropertyBagPropertyType::Int32,
            FGuid(0xDF100001, 0xDF100002, 0xDF100003, 0xDF100004)),
    });
    const UPropertyBag* DuplicateBag = DuplicateTree.Root->Parameters.Parameters.GetPropertyBagStruct();
    if (!TestNotNull(TEXT("Duplicate Parameter fixture has a Property Bag"), DuplicateBag))
    {
        return false;
    }
    TConstArrayView<FPropertyBagPropertyDesc> DuplicateDescs = DuplicateBag->GetPropertyDescs();
    if (!TestEqual(TEXT("Duplicate Parameter fixture has two descriptors"), DuplicateDescs.Num(), 2))
    {
        return false;
    }
    const_cast<FPropertyBagPropertyDesc&>(DuplicateDescs[1]).ID = DuplicateId;
    FSalQuery Duplicate = Query(TEXT("parameter"));
    Duplicate.Operation->SetStringField(
        TEXT("id"),
        ParameterId(DuplicateTree.Root->Parameters.ID, DuplicateId));
    TestTrue(TEXT("Duplicate compound Parameter identity fails closed"), HasError(FSalStateTreeInterface::Query(Duplicate, DuplicateTarget)));
    const TSharedPtr<FJsonObject> DuplicateCollection = FSalStateTreeInterface::Query(
        Query(TEXT("parameters")),
        DuplicateTarget);
    TestFalse(TEXT("Duplicate Parameters remain discoverable"), HasError(DuplicateCollection));
    TestEqual(TEXT("Both duplicate descriptors remain visible"), CallCount(DuplicateCollection, TEXT("parameter")), 2);
    TestEqual(TEXT("Duplicate descriptors expose no canonical id"), CallIds(DuplicateCollection, TEXT("parameter")).Num(), 0);
    TestTrue(TEXT("Duplicate Parameter identity is diagnosed"), HasDiagnosticContaining(DuplicateCollection, TEXT("occurs 2 times")));

    const FTestStateTree InvalidTree = MakeStateTree(MakeTestPackage());
    const FSalResolvedTarget InvalidTarget = ResolvedTarget(InvalidTree);
    const FGuid InitiallyValidId(0xE1000001, 0xE1000002, 0xE1000003, 0xE1000004);
    InvalidTree.Root->Parameters.Parameters.AddProperties({
        ParameterDesc(TEXT("InvalidIdentity"), EPropertyBagPropertyType::Bool, InitiallyValidId),
    });
    const UPropertyBag* InvalidBag = InvalidTree.Root->Parameters.Parameters.GetPropertyBagStruct();
    if (!TestNotNull(TEXT("Invalid Parameter fixture has a Property Bag"), InvalidBag))
    {
        return false;
    }
    TConstArrayView<FPropertyBagPropertyDesc> InvalidDescs = InvalidBag->GetPropertyDescs();
    if (!TestEqual(TEXT("Invalid Parameter fixture has one descriptor"), InvalidDescs.Num(), 1))
    {
        return false;
    }
    const_cast<FPropertyBagPropertyDesc&>(InvalidDescs[0]).ID.Invalidate();
    const TSharedPtr<FJsonObject> InvalidCollection = FSalStateTreeInterface::Query(
        Query(TEXT("parameters")),
        InvalidTarget);
    TestFalse(TEXT("A descriptor with invalid identity remains discoverable"), HasError(InvalidCollection));
    TestEqual(TEXT("Invalid descriptor remains visible"), CallCount(InvalidCollection, TEXT("parameter")), 1);
    TestEqual(TEXT("Invalid descriptor exposes no canonical id"), CallIds(InvalidCollection, TEXT("parameter")).Num(), 0);
    TestTrue(TEXT("Invalid Parameter identity is diagnosed"), HasDiagnosticContaining(InvalidCollection, TEXT("invalid")));

    FSalQuery Invalid = Query(TEXT("parameter"));
    Invalid.Operation->SetStringField(
        TEXT("id"),
        ParameterId(InvalidTree.Root->Parameters.ID, FGuid()));
    TestTrue(TEXT("An invalid Property Guid cannot be exact-read"), HasError(FSalStateTreeInterface::Query(Invalid, InvalidTarget)));

    const FTestStateTree DuplicateContainerTree = MakeStateTree(MakeTestPackage());
    const FSalResolvedTarget DuplicateContainerTarget = ResolvedTarget(DuplicateContainerTree);
    const FGuid FirstProperty(0xE2000001, 0xE2000002, 0xE2000003, 0xE2000004);
    DuplicateContainerTree.First->Parameters.Parameters.AddProperties({
        ParameterDesc(TEXT("FirstProperty"), EPropertyBagPropertyType::Bool, FirstProperty),
    });
    DuplicateContainerTree.Second->Parameters.ID = DuplicateContainerTree.First->Parameters.ID;
    DuplicateContainerTree.Second->Parameters.Parameters.AddProperties({
        ParameterDesc(
            TEXT("SecondProperty"),
            EPropertyBagPropertyType::Bool,
            FGuid(0xE3000001, 0xE3000002, 0xE3000003, 0xE3000004)),
    });
    FSalQuery DuplicateContainer = Query(TEXT("parameter"));
    DuplicateContainer.Operation->SetStringField(
        TEXT("id"),
        ParameterId(DuplicateContainerTree.First->Parameters.ID, FirstProperty));
    TestTrue(
        TEXT("A duplicate Parameter container Guid makes its members non-canonical"),
        HasError(FSalStateTreeInterface::Query(DuplicateContainer, DuplicateContainerTarget)));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FSalStateTreeContextDataReadTest,
    "Loomle.Sal.StateTree.ContextDataRead",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSalStateTreeContextDataReadTest::RunTest(const FString& Parameters)
{
    const FTestStateTree Tree = MakeStateTree(MakeTestPackage());
    const FSalResolvedTarget Target = ResolvedTarget(Tree);
    USalStateTreeTestSchema* Schema = NewObject<USalStateTreeTestSchema>(Tree.EditorData);
    Tree.EditorData->Schema = Schema;
    const FGuid ActorId(0xE4000001, 0xE4000002, 0xE4000003, 0xE4000004);
    const FGuid StructId(0xE5000001, 0xE5000002, 0xE5000003, 0xE5000004);
    const FGuid DuplicateId(0xE6000001, 0xE6000002, 0xE6000003, 0xE6000004);
    const FGuid NullStructId(0xE7000001, 0xE7000002, 0xE7000003, 0xE7000004);
    Schema->ContextData.Add(FStateTreeExternalDataDesc(
        FName(TEXT("ActorContext")), UObject::StaticClass(), ActorId));
    FStateTreeExternalDataDesc StructContext(
        FName(TEXT("StructContext")), TBaseStructure<FGuid>::Get(), StructId);
    StructContext.Requirement = EStateTreeExternalDataRequirement::Optional;
    Schema->ContextData.Add(StructContext);
    Schema->ContextData.Add(FStateTreeExternalDataDesc(
        FName(TEXT("InvalidContext")), UObject::StaticClass(), FGuid()));
    Schema->ContextData.Add(FStateTreeExternalDataDesc(
        FName(TEXT("DuplicateContextA")), UObject::StaticClass(), DuplicateId));
    Schema->ContextData.Add(FStateTreeExternalDataDesc(
        FName(TEXT("DuplicateContextB")), TBaseStructure<FGuid>::Get(), DuplicateId));
    Schema->ContextData.Add(FStateTreeExternalDataDesc(
        FName(TEXT("NullStructContext")), nullptr, NullStructId));

    const TSharedPtr<FJsonObject> Summary = FSalStateTreeInterface::Query(
        Query(TEXT("summary")),
        Target);
    TestFalse(TEXT("Summary with malformed Context Data remains readable"), HasError(Summary));
    const TArray<FString> ContextNames = CallNativeNames(Summary, TEXT("object"), TEXT("Name"));
    const TArray<FString> ExpectedNames = {
        TEXT("ActorContext"),
        TEXT("StructContext"),
        TEXT("InvalidContext"),
        TEXT("DuplicateContextA"),
        TEXT("DuplicateContextB"),
        TEXT("NullStructContext"),
    };
    TestEqual(TEXT("Summary keeps every Schema Context Data descriptor visible"), ContextNames.Num(), ExpectedNames.Num());
    if (ContextNames.Num() == ExpectedNames.Num())
    {
        for (int32 Index = 0; Index < ExpectedNames.Num(); ++Index)
        {
            TestEqual(
                *FString::Printf(TEXT("Context Data descriptor %d preserves Schema order"), Index),
                ContextNames[Index],
                ExpectedNames[Index]);
        }
    }
    const TArray<FString> CanonicalContextIds = CallIds(Summary, TEXT("object"));
    const TArray<FString> ExpectedCanonicalIds = {
        ActorId.ToString(EGuidFormats::DigitsWithHyphensLower),
        StructId.ToString(EGuidFormats::DigitsWithHyphensLower),
        NullStructId.ToString(EGuidFormats::DigitsWithHyphensLower),
    };
    TestEqual(TEXT("Only valid unique Context Data descriptors expose ids"), CanonicalContextIds.Num(), ExpectedCanonicalIds.Num());
    if (CanonicalContextIds.Num() == ExpectedCanonicalIds.Num())
    {
        for (int32 Index = 0; Index < ExpectedCanonicalIds.Num(); ++Index)
        {
            TestEqual(
                *FString::Printf(TEXT("Canonical Context Data id %d preserves Schema order"), Index),
                CanonicalContextIds[Index],
                ExpectedCanonicalIds[Index]);
        }
    }
    TestTrue(TEXT("Invalid Context Data id is diagnosed"), HasDiagnosticContaining(Summary, TEXT("invalid")));
    TestTrue(TEXT("Duplicate Context Data id is diagnosed"), HasDiagnosticContaining(Summary, TEXT("occurs 2 times")));
    TestTrue(TEXT("Null Context Data Struct is diagnosed"), HasDiagnosticContaining(Summary, TEXT("has no native Struct")));

    FSalQuery ExactActor = Query(TEXT("object"));
    ExactActor.Operation->SetStringField(
        TEXT("id"),
        ActorId.ToString(EGuidFormats::DigitsWithHyphensLower));
    const TSharedPtr<FJsonObject> ExactActorResult = FSalStateTreeInterface::Query(ExactActor, Target);
    TestFalse(TEXT("Exact Context Data read succeeds"), HasError(ExactActorResult));
    TestEqual(TEXT("Exact Context Data read returns one object"), CallCount(ExactActorResult, TEXT("object")), 1);
    const TSharedPtr<FJsonObject> ExactActorArgs = FirstCallArgs(ExactActorResult, TEXT("object"));
    FString ContextType;
    FString ContextStruct;
    TestTrue(
        TEXT("Exact Context Data preserves its native descriptor type"),
        ExactActorArgs.IsValid()
            && ExactActorArgs->TryGetStringField(TEXT("type"), ContextType)
            && ContextType == FStateTreeExternalDataDesc::StaticStruct()->GetPathName());
    TestTrue(TEXT("Exact Context Data preserves native Name"), NativeNameEquals(ExactActorArgs, TEXT("Name"), TEXT("ActorContext")));
    TestTrue(
        TEXT("Exact Context Data preserves native Struct"),
        ExactActorArgs.IsValid()
            && ExactActorArgs->TryGetStringField(TEXT("Struct"), ContextStruct)
            && ContextStruct == UObject::StaticClass()->GetPathName());
    TestTrue(TEXT("Exact Context Data preserves Requirement"), NativeNameEquals(ExactActorArgs, TEXT("Requirement"), TEXT("Required")));
    TestTrue(TEXT("Exact Context Data excludes compiled Handle"), ExactActorArgs.IsValid() && !ExactActorArgs->HasField(TEXT("Handle")));
    TestTrue(TEXT("Exact Context Data reports Schema ownership"), HasCommentContaining(ExactActorResult, TEXT("owner: Schema Context Data")));
    TestTrue(TEXT("Exact Context Data explains absent runtime values"), HasCommentContaining(ExactActorResult, TEXT("runtime value: unavailable in authored asset")));

    FSalQuery ExactStruct = Query(TEXT("object"));
    ExactStruct.Operation->SetStringField(
        TEXT("id"),
        StructId.ToString(EGuidFormats::DigitsWithHyphensLower));
    const TSharedPtr<FJsonObject> ExactStructArgs = FirstCallArgs(
        FSalStateTreeInterface::Query(ExactStruct, Target),
        TEXT("object"));
    FString StructType;
    TestTrue(
        TEXT("Exact struct Context Data preserves its UScriptStruct path"),
        ExactStructArgs.IsValid()
            && ExactStructArgs->TryGetStringField(TEXT("Struct"), StructType)
            && StructType == TBaseStructure<FGuid>::Get()->GetPathName());
    TestTrue(
        TEXT("Exact Context Data preserves Optional requirement"),
        NativeNameEquals(ExactStructArgs, TEXT("Requirement"), TEXT("Optional")));

    FSalQuery ExactNullStruct = Query(TEXT("object"));
    ExactNullStruct.Operation->SetStringField(
        TEXT("id"),
        NullStructId.ToString(EGuidFormats::DigitsWithHyphensLower));
    const TSharedPtr<FJsonObject> ExactNullResult = FSalStateTreeInterface::Query(ExactNullStruct, Target);
    TestFalse(TEXT("Context Data with null Struct remains exact-readable"), HasError(ExactNullResult));
    const TSharedPtr<FJsonObject> ExactNullArgs = FirstCallArgs(ExactNullResult, TEXT("object"));
    TestTrue(TEXT("Null native Struct is not synthesized"), ExactNullArgs.IsValid() && !ExactNullArgs->HasField(TEXT("Struct")));
    TestTrue(TEXT("Exact null Struct is diagnosed"), HasDiagnosticContaining(ExactNullResult, TEXT("has no native Struct")));

    FSalQuery Duplicate = Query(TEXT("object"));
    Duplicate.Operation->SetStringField(
        TEXT("id"),
        DuplicateId.ToString(EGuidFormats::DigitsWithHyphensLower));
    TestTrue(TEXT("Duplicate Context Data id fails closed"), HasError(FSalStateTreeInterface::Query(Duplicate, Target)));
    FSalQuery Invalid = Query(TEXT("object"));
    Invalid.Operation->SetStringField(TEXT("id"), FGuid().ToString(EGuidFormats::DigitsWithHyphensLower));
    TestTrue(TEXT("Invalid Context Data id fails closed"), HasError(FSalStateTreeInterface::Query(Invalid, Target)));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FSalStateTreeExactRelationshipReadTest,
    "Loomle.Sal.StateTree.ExactRelationshipRead",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSalStateTreeExactRelationshipReadTest::RunTest(const FString& Parameters)
{
    const FTestStateTree Tree = MakeStateTree(MakeTestPackage());
    const FSalResolvedTarget Target = ResolvedTarget(Tree);
    const FGuid InputParameterId(0xF1000001, 0xF1000002, 0xF1000003, 0xF1000004);
    const FGuid EnabledParameterId(0xF1100001, 0xF1100002, 0xF1100003, 0xF1100004);
    const FGuid FunctionParameterId(0xF1200001, 0xF1200002, 0xF1200003, 0xF1200004);
    const FGuid ExplicitObjectParameterId(0xF1300001, 0xF1300002, 0xF1300003, 0xF1300004);
    const FGuid UnrelatedParameterId(0xF1400001, 0xF1400002, 0xF1400003, 0xF1400004);
    FPropertyBagPropertyDesc ExplicitObjectDesc(
        TEXT("ExplicitObject"),
        EPropertyBagPropertyType::Object,
        UObject::StaticClass());
    ExplicitObjectDesc.ID = ExplicitObjectParameterId;
    FInstancedPropertyBag& RootBag = const_cast<FInstancedPropertyBag&>(
        Tree.EditorData->GetRootParametersPropertyBag());
    RootBag.AddProperties({
        ParameterDesc(TEXT("InputValue"), EPropertyBagPropertyType::Int32, InputParameterId),
        ParameterDesc(TEXT("Enabled"), EPropertyBagPropertyType::Bool, EnabledParameterId),
        ParameterDesc(TEXT("FunctionInput"), EPropertyBagPropertyType::Float, FunctionParameterId),
        ExplicitObjectDesc,
        ParameterDesc(TEXT("Unrelated"), EPropertyBagPropertyType::Int32, UnrelatedParameterId),
    });

    USalStateTreeTestSchema* Schema = NewObject<USalStateTreeTestSchema>(Tree.EditorData);
    Tree.EditorData->Schema = Schema;
    const FGuid ContextId(0xF2000001, 0xF2000002, 0xF2000003, 0xF2000004);
    Schema->ContextData.Add(FStateTreeExternalDataDesc(
        FName(TEXT("ContextObject")),
        UObject::StaticClass(),
        ContextId));

    const FGuid ProducerId(0xF3000001, 0xF3000002, 0xF3000003, 0xF3000004);
    const FGuid ConsumerId(0xF3100001, 0xF3100002, 0xF3100003, 0xF3100004);
    const FGuid OutputConsumerAId(0xF3200001, 0xF3200002, 0xF3200003, 0xF3200004);
    const FGuid OutputConsumerBId(0xF3300001, 0xF3300002, 0xF3300003, 0xF3300004);
    const FGuid AutomaticContextNodeId(0xF3400001, 0xF3400002, 0xF3400003, 0xF3400004);
    const FGuid ExplicitContextNodeId(0xF3500001, 0xF3500002, 0xF3500003, 0xF3500004);
    const FGuid UnrelatedNodeId(0xF3600001, 0xF3600002, 0xF3600003, 0xF3600004);
    const FGuid StateEventConsumerId(0xF3700001, 0xF3700002, 0xF3700003, 0xF3700004);
    const FGuid TransitionEventConsumerId(0xF3800001, 0xF3800002, 0xF3800003, 0xF3800004);
    const FGuid FunctionConsumerId(0xF3900001, 0xF3900002, 0xF3900003, 0xF3900004);
    const FGuid IndexedProducerId(0xF3A00001, 0xF3A00002, 0xF3A00003, 0xF3A00004);
    const FGuid IndexedConsumerId(0xF3B00001, 0xF3B00002, 0xF3B00003, 0xF3B00004);
    const FGuid IneligibleNodeContextId(0xF3C00001, 0xF3C00002, 0xF3C00003, 0xF3C00004);
    FStateTreeEditorNode& Producer = AddBindingTask(Tree.Root->Tasks, ProducerId, TEXT("Producer"));
    FStateTreeEditorNode& Consumer = AddBindingTask(Tree.Root->Tasks, ConsumerId, TEXT("Consumer"));
    AddBindingTask(Tree.Root->Tasks, OutputConsumerAId, TEXT("Output Consumer A"));
    AddBindingTask(Tree.Root->Tasks, OutputConsumerBId, TEXT("Output Consumer B"));
    AddContextTask(Tree.Root->Tasks, AutomaticContextNodeId, TEXT("Automatic Context"));
    AddContextTask(Tree.Root->Tasks, ExplicitContextNodeId, TEXT("Explicit Context"));
    AddBindingTask(Tree.Root->Tasks, UnrelatedNodeId, TEXT("Unrelated"));
    AddBindingTask(Tree.First->Tasks, StateEventConsumerId, TEXT("State Event Consumer"));
    AddBindingTask(Tree.Root->Tasks, FunctionConsumerId, TEXT("Function Consumer"));
    AddBindingTask(Tree.Root->Tasks, IndexedProducerId, TEXT("Indexed Producer"));
    AddBindingTask(Tree.Root->Tasks, IndexedConsumerId, TEXT("Indexed Consumer"));
    AddIneligibleNodeContextTask(
        Tree.Root->Tasks,
        IneligibleNodeContextId,
        TEXT("Ineligible Node Context"));

    const FGuid RootContainerId = Tree.EditorData->GetRootParametersGuid();
    Tree.EditorData->AddPropertyBinding(
        FPropertyBindingPath(RootContainerId, TEXT("InputValue")),
        FPropertyBindingPath(ConsumerId, TEXT("InputValue")));
    Tree.EditorData->AddPropertyBinding(
        FPropertyBindingPath(RootContainerId, TEXT("Enabled")),
        FPropertyBindingPath(Consumer.GetNodeID(), TEXT("bTaskEnabled")));
    Tree.EditorData->GetPropertyEditorBindings()->AddOutputBinding(
        FPropertyBindingPath(OutputConsumerAId, TEXT("InputValue")),
        FPropertyBindingPath(ProducerId, TEXT("OutputValue")));
    Tree.EditorData->GetPropertyEditorBindings()->AddOutputBinding(
        FPropertyBindingPath(OutputConsumerBId, TEXT("InputValue")),
        FPropertyBindingPath(ProducerId, TEXT("OutputValue")));
    Tree.EditorData->AddPropertyBinding(
        FPropertyBindingPath(RootContainerId, TEXT("ExplicitObject")),
        FPropertyBindingPath(ExplicitContextNodeId, TEXT("ContextObject")));
    Tree.EditorData->AddPropertyBinding(
        FPropertyBindingPath(RootContainerId, TEXT("Unrelated")),
        FPropertyBindingPath(UnrelatedNodeId, TEXT("InputValue")));
    const TArray<FPropertyBindingPathSegment> IndexedSourceSegments = {
        FPropertyBindingPathSegment(TEXT("OutputValues"), 0),
    };
    const TArray<FPropertyBindingPathSegment> IndexedTargetSegments = {
        FPropertyBindingPathSegment(TEXT("InputValues"), 0),
    };
    Tree.EditorData->AddPropertyBinding(
        FPropertyBindingPath(IndexedProducerId, IndexedSourceSegments),
        FPropertyBindingPath(IndexedConsumerId, IndexedTargetSegments));

    Tree.First->bHasRequiredEventToEnter = true;
    Tree.First->RequiredEventToEnter.PayloadStruct = FSalStateTreeBindingEventPayload::StaticStruct();
    Tree.EditorData->AddPropertyBinding(
        EventPayloadValuePath(Tree.First->GetEventID()),
        FPropertyBindingPath(StateEventConsumerId, TEXT("InputValue")));

    FStateTreeTransition& Transition = Tree.First->AddTransition(
        EStateTreeTransitionTrigger::OnEvent,
        FGameplayTag(),
        EStateTreeTransitionType::Succeeded);
    Transition.ID = FGuid(0xF4000001, 0xF4000002, 0xF4000003, 0xF4000004);
    Transition.RequiredEvent.PayloadStruct = FSalStateTreeBindingEventPayload::StaticStruct();
    AddBindingCondition(
        Transition.Conditions,
        TransitionEventConsumerId,
        TEXT("Transition Event Consumer"));
    Tree.EditorData->AddPropertyBinding(
        EventPayloadValuePath(Transition.GetEventID()),
        FPropertyBindingPath(TransitionEventConsumerId, TEXT("InputValue")));

    UScriptStruct* PropertyFunctionStruct = FindObject<UScriptStruct>(
        nullptr,
        TEXT("/Script/StateTreeModule.StateTreeAddFloatPropertyFunction"));
    if (!TestNotNull(TEXT("Built-in Property Function fixture is registered"), PropertyFunctionStruct))
    {
        return false;
    }
    Tree.EditorData->AddPropertyBinding(
        PropertyFunctionStruct,
        {FPropertyBindingPathSegment(TEXT("Result"))},
        FPropertyBindingPath(FunctionConsumerId, TEXT("FloatInput")));
    const TConstArrayView<FStateTreePropertyPathBinding> FunctionBindings =
        Tree.EditorData->GetPropertyEditorBindings()->GetBindings();
    const FStateTreeEditorNode* PropertyFunction =
        FunctionBindings.Last().GetPropertyFunctionNode().GetPtr<const FStateTreeEditorNode>();
    if (!TestNotNull(TEXT("Property Function owns an editor Node"), PropertyFunction))
    {
        return false;
    }
    const FGuid PropertyFunctionId = PropertyFunction->ID;
    Tree.EditorData->AddPropertyBinding(
        FPropertyBindingPath(RootContainerId, TEXT("FunctionInput")),
        FPropertyBindingPath(PropertyFunctionId, TEXT("Left")));

    Tree.Asset->LastCompiledEditorDataHash = UStateTreeEditingSubsystem::CalculateStateTreeHash(Tree.Asset);
    Tree.Asset->GetOutermost()->SetDirtyFlag(false);
    const uint32 BeforeHash = UStateTreeEditingSubsystem::CalculateStateTreeHash(Tree.Asset);
    const uint32 BeforeCompiledHash = Tree.Asset->LastCompiledEditorDataHash;
    const bool bBeforeDirty = Tree.Asset->GetOutermost()->IsDirty();

    const auto GuidRef = [](const TCHAR* Kind, const FGuid& Id)
    {
        return FString::Printf(
            TEXT("%s@%s"),
            Kind,
            *Id.ToString(EGuidFormats::DigitsWithHyphensLower));
    };
    const FString InputParameterRef = TEXT("parameter@") + ParameterId(RootContainerId, InputParameterId);
    const FString EnabledParameterRef = TEXT("parameter@") + ParameterId(RootContainerId, EnabledParameterId);
    const FString FunctionParameterRef = TEXT("parameter@") + ParameterId(RootContainerId, FunctionParameterId);
    const FString ExplicitObjectParameterRef = TEXT("parameter@") + ParameterId(RootContainerId, ExplicitObjectParameterId);
    const FString NormalEdge = InputParameterRef + TEXT(" -> ")
        + GuidRef(TEXT("node"), ConsumerId) + TEXT(".Instance.InputValue");
    const FString NodeSurfaceEdge = EnabledParameterRef + TEXT(" -> ")
        + GuidRef(TEXT("node"), ConsumerId) + TEXT(".Node.bTaskEnabled");
    const FString OutputEdgeA = GuidRef(TEXT("node"), ProducerId) + TEXT(".Instance.OutputValue -> ")
        + GuidRef(TEXT("node"), OutputConsumerAId) + TEXT(".Instance.InputValue");
    const FString OutputEdgeB = GuidRef(TEXT("node"), ProducerId) + TEXT(".Instance.OutputValue -> ")
        + GuidRef(TEXT("node"), OutputConsumerBId) + TEXT(".Instance.InputValue");
    const FString AutomaticContextEdge = GuidRef(TEXT("object"), ContextId) + TEXT(" -> ")
        + GuidRef(TEXT("node"), AutomaticContextNodeId) + TEXT(".Instance.ContextObject");
    const FString ExplicitContextEdge = ExplicitObjectParameterRef + TEXT(" -> ")
        + GuidRef(TEXT("node"), ExplicitContextNodeId) + TEXT(".Instance.ContextObject");
    const FString StateEventEdge = GuidRef(TEXT("state"), Tree.First->ID)
        + TEXT(".RequiredEventToEnter.Payload.Value -> ")
        + GuidRef(TEXT("node"), StateEventConsumerId) + TEXT(".Instance.InputValue");
    const FString TransitionEventEdge = GuidRef(TEXT("transition"), Transition.ID)
        + TEXT(".RequiredEvent.Payload.Value -> ")
        + GuidRef(TEXT("node"), TransitionEventConsumerId) + TEXT(".Instance.InputValue");
    const FString FunctionOutputEdge = GuidRef(TEXT("node"), PropertyFunctionId)
        + TEXT(".Instance.Result -> ")
        + GuidRef(TEXT("node"), FunctionConsumerId) + TEXT(".Instance.FloatInput");
    const FString FunctionInputEdge = FunctionParameterRef + TEXT(" -> ")
        + GuidRef(TEXT("node"), PropertyFunctionId) + TEXT(".Instance.Left");
    const FString IndexedEdge = GuidRef(TEXT("node"), IndexedProducerId)
        + TEXT(".Instance.OutputValues[0] -> ")
        + GuidRef(TEXT("node"), IndexedConsumerId) + TEXT(".Instance.InputValues[0]");
    const FString EligibleInstanceContextEdge = GuidRef(TEXT("object"), ContextId) + TEXT(" -> ")
        + GuidRef(TEXT("node"), IneligibleNodeContextId) + TEXT(".Instance.ContextObject");
    const FString IneligibleNodeContextEdge = GuidRef(TEXT("object"), ContextId) + TEXT(" -> ")
        + GuidRef(TEXT("node"), IneligibleNodeContextId) + TEXT(".Node.NodeContextObject");

    const auto Exact = [&Target](const FString& Kind, const FGuid& Id)
    {
        FSalQuery ExactQuery = Query(Kind);
        ExactQuery.Operation->SetStringField(
            TEXT("id"),
            Id.ToString(EGuidFormats::DigitsWithHyphensLower));
        return FSalStateTreeInterface::Query(ExactQuery, Target);
    };
    const auto ExactParameter = [&Target](const FGuid& ContainerId, const FGuid& PropertyId)
    {
        FSalQuery ExactQuery = Query(TEXT("parameter"));
        ExactQuery.Operation->SetStringField(TEXT("id"), ParameterId(ContainerId, PropertyId));
        return FSalStateTreeInterface::Query(ExactQuery, Target);
    };

    const TSharedPtr<FJsonObject> ConsumerResult = Exact(TEXT("node"), ConsumerId);
    TestFalse(TEXT("Exact Node relationship read succeeds"), HasError(ConsumerResult));
    const TArray<FString> ConsumerEdges = EdgeTexts(ConsumerResult);
    TestEqual(TEXT("Exact Node returns only its two direct relationships"), ConsumerEdges.Num(), 2);
    TestTrue(TEXT("Ordinary Binding points from source Parameter to target Node Instance"), ConsumerEdges.Contains(NormalEdge));
    TestTrue(TEXT("Node-template native ID maps to the public Node surface"), ConsumerEdges.Contains(NodeSurfaceEdge));

    const TSharedPtr<FJsonObject> ProducerResult = Exact(TEXT("node"), ProducerId);
    const TArray<FString> ProducerEdges = EdgeTexts(ProducerResult);
    TestFalse(TEXT("Exact output producer read succeeds"), HasError(ProducerResult));
    TestEqual(TEXT("One UE Output may fan out to two consumers"), ProducerEdges.Num(), 2);
    TestTrue(TEXT("First Output Binding is reversed into real data-flow direction"), ProducerEdges.Contains(OutputEdgeA));
    TestTrue(TEXT("Second Output Binding is preserved rather than replaced"), ProducerEdges.Contains(OutputEdgeB));

    const TSharedPtr<FJsonObject> IndexedResult = Exact(TEXT("node"), IndexedConsumerId);
    const TArray<FString> IndexedEdges = EdgeTexts(IndexedResult);
    TestFalse(TEXT("Exact indexed native Binding read succeeds"), HasError(IndexedResult));
    TestEqual(TEXT("Exact indexed Node returns its one direct relationship"), IndexedEdges.Num(), 1);
    TestTrue(TEXT("Numeric member segments format as native [0] suffixes"), IndexedEdges.Contains(IndexedEdge));
    int32 NumericSegments = 0;
    int32 StringZeroSegments = 0;
    TestTrue(
        TEXT("Indexed edge can be inspected in the normalized Object Text JSON"),
        EdgePathSegmentKinds(IndexedResult, IndexedEdge, NumericSegments, StringZeroSegments));
    TestEqual(TEXT("Both endpoint indexes are JSON Number path segments"), NumericSegments, 2);
    TestEqual(TEXT("No endpoint index is serialized as string \"0\""), StringZeroSegments, 0);

    const TSharedPtr<FJsonObject> IneligibleNodeContextResult = Exact(
        TEXT("node"),
        IneligibleNodeContextId);
    const TArray<FString> IneligibleNodeContextEdges = EdgeTexts(IneligibleNodeContextResult);
    TestFalse(
        TEXT("Exact Node with an eligible Instance Context remains readable"),
        HasError(IneligibleNodeContextResult));
    TestEqual(
        TEXT("UE-ineligible Node template does not add a second automatic Context relationship"),
        IneligibleNodeContextEdges.Num(),
        1);
    TestTrue(
        TEXT("The eligible Instance Context relationship remains visible"),
        IneligibleNodeContextEdges.Contains(EligibleInstanceContextEdge));
    TestFalse(
        TEXT("Node struct without PropertyRef or Delegate markers is not a Binding surface"),
        IneligibleNodeContextEdges.Contains(IneligibleNodeContextEdge));

    const TSharedPtr<FJsonObject> AutomaticContextResult = Exact(TEXT("node"), AutomaticContextNodeId);
    const TArray<FString> AutomaticContextEdges = EdgeTexts(AutomaticContextResult);
    TestEqual(TEXT("Unbound Context usage derives exactly one automatic relationship"), AutomaticContextEdges.Num(), 1);
    TestTrue(TEXT("Automatic Context uses the canonical Schema object as its source"), AutomaticContextEdges.Contains(AutomaticContextEdge));
    TestTrue(
        TEXT("Automatic Context is identified by the immediately adjacent comment"),
        HasCommentImmediatelyBeforeEdge(AutomaticContextResult, AutomaticContextEdge, TEXT("automatic Context")));

    const TSharedPtr<FJsonObject> ExplicitContextResult = Exact(TEXT("node"), ExplicitContextNodeId);
    const TArray<FString> ExplicitContextEdges = EdgeTexts(ExplicitContextResult);
    TestEqual(TEXT("Explicit Context override returns one authored relationship"), ExplicitContextEdges.Num(), 1);
    TestTrue(TEXT("Explicit Binding suppresses the automatic Context source"), ExplicitContextEdges.Contains(ExplicitContextEdge));
    TestFalse(
        TEXT("Explicit Context relationship is not labelled automatic"),
        HasCommentImmediatelyBeforeEdge(ExplicitContextResult, ExplicitContextEdge, TEXT("automatic Context")));

    const TSharedPtr<FJsonObject> ParameterResult = ExactParameter(RootContainerId, InputParameterId);
    TestFalse(TEXT("Exact Parameter relationship read succeeds"), HasError(ParameterResult));
    const TArray<FString> ParameterEdges = EdgeTexts(ParameterResult);
    TestEqual(TEXT("Exact Parameter returns only direct uses of that descriptor"), ParameterEdges.Num(), 1);
    TestTrue(TEXT("Exact Parameter endpoint uses its composite stable ref"), ParameterEdges.Contains(NormalEdge));

    const TSharedPtr<FJsonObject> ContextResult = Exact(TEXT("object"), ContextId);
    const TArray<FString> ContextEdges = EdgeTexts(ContextResult);
    TestFalse(TEXT("Exact Context Data relationship read succeeds"), HasError(ContextResult));
    TestEqual(TEXT("Exact Context Data returns its direct automatic use"), ContextEdges.Num(), 1);
    TestTrue(TEXT("Context exact read preserves the automatic arrow"), ContextEdges.Contains(AutomaticContextEdge));

    const TSharedPtr<FJsonObject> StateResult = Exact(TEXT("state"), Tree.First->ID);
    const TArray<FString> StateEdges = EdgeTexts(StateResult);
    TestFalse(TEXT("Exact State Required Event relationship read succeeds"), HasError(StateResult));
    TestEqual(TEXT("Exact State returns its Required Event edge but not owned-Node relationships"), StateEdges.Num(), 1);
    TestTrue(TEXT("Derived State Event ID maps back to the stable State member"), StateEdges.Contains(StateEventEdge));
    TestEqual(
        TEXT("Exact root State does not recursively return relationships of owned Nodes"),
        EdgeTexts(Exact(TEXT("state"), Tree.Root->ID)).Num(),
        0);

    const TSharedPtr<FJsonObject> TransitionResult = Exact(TEXT("transition"), Transition.ID);
    const TArray<FString> TransitionEdges = EdgeTexts(TransitionResult);
    TestFalse(TEXT("Exact Transition Required Event relationship read succeeds"), HasError(TransitionResult));
    TestEqual(TEXT("Exact Transition returns only its direct Required Event relationship"), TransitionEdges.Num(), 1);
    TestTrue(TEXT("Derived Transition Event ID maps back to the stable Transition member"), TransitionEdges.Contains(TransitionEventEdge));

    const TSharedPtr<FJsonObject> PropertyFunctionResult = Exact(TEXT("node"), PropertyFunctionId);
    const TArray<FString> PropertyFunctionEdges = EdgeTexts(PropertyFunctionResult);
    TestFalse(TEXT("Exact Property Function relationship read succeeds"), HasError(PropertyFunctionResult));
    TestEqual(TEXT("Property Function returns its owning output and direct input"), PropertyFunctionEdges.Num(), 2);
    TestTrue(TEXT("Property Function output keeps normal data-flow direction"), PropertyFunctionEdges.Contains(FunctionOutputEdge));
    TestTrue(TEXT("Property Function input remains an ordinary Parameter relationship"), PropertyFunctionEdges.Contains(FunctionInputEdge));

    TestEqual(
        TEXT("Relationship reads leave the authored StateTree hash unchanged"),
        UStateTreeEditingSubsystem::CalculateStateTreeHash(Tree.Asset),
        BeforeHash);
    TestEqual(
        TEXT("Relationship reads leave the compiled editor-data hash unchanged"),
        Tree.Asset->LastCompiledEditorDataHash,
        BeforeCompiledHash);
    TestEqual(
        TEXT("Relationship reads leave package dirty state unchanged"),
        Tree.Asset->GetOutermost()->IsDirty(),
        bBeforeDirty);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FSalStateTreeMalformedRelationshipReadTest,
    "Loomle.Sal.StateTree.MalformedRelationshipRead",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSalStateTreeMalformedRelationshipReadTest::RunTest(const FString& Parameters)
{
    const FTestStateTree Tree = MakeStateTree(MakeTestPackage());
    const FSalResolvedTarget Target = ResolvedTarget(Tree);
    const FGuid SourceParameterId(0xFA000001, 0xFA000002, 0xFA000003, 0xFA000004);
    FInstancedPropertyBag& RootBag = const_cast<FInstancedPropertyBag&>(
        Tree.EditorData->GetRootParametersPropertyBag());
    RootBag.AddProperties({
        ParameterDesc(TEXT("Source"), EPropertyBagPropertyType::Int32, SourceParameterId),
    });
    const FGuid RootContainerId = Tree.EditorData->GetRootParametersGuid();

    const FGuid StaleTargetId(0xFB000001, 0xFB000002, 0xFB000003, 0xFB000004);
    const FGuid BrokenPathTargetId(0xFB100001, 0xFB100002, 0xFB100003, 0xFB100004);
    const FGuid DuplicateSourceId(0xFB200001, 0xFB200002, 0xFB200003, 0xFB200004);
    const FGuid DuplicateTargetId(0xFB300001, 0xFB300002, 0xFB300003, 0xFB300004);
    const FGuid ContextTargetId(0xFB400001, 0xFB400002, 0xFB400003, 0xFB400004);
    AddBindingTask(Tree.Root->Tasks, StaleTargetId, TEXT("Stale Source Target"));
    AddBindingTask(Tree.Root->Tasks, BrokenPathTargetId, TEXT("Broken Path Target"));
    AddBindingTask(Tree.Root->Tasks, DuplicateSourceId, TEXT("Duplicate Source A"));
    AddBindingTask(Tree.Root->Tasks, DuplicateSourceId, TEXT("Duplicate Source B"));
    AddBindingTask(Tree.Root->Tasks, DuplicateTargetId, TEXT("Duplicate Source Target"));
    AddContextTask(Tree.Root->Tasks, ContextTargetId, TEXT("Ambiguous Context Target"));

    Tree.EditorData->AddPropertyBinding(
        FPropertyBindingPath(FGuid(0xFC000001, 0xFC000002, 0xFC000003, 0xFC000004), TEXT("Missing")),
        FPropertyBindingPath(StaleTargetId, TEXT("InputValue")));
    Tree.EditorData->AddPropertyBinding(
        FPropertyBindingPath(RootContainerId, TEXT("Source")),
        FPropertyBindingPath(BrokenPathTargetId, TEXT("MissingProperty")));
    Tree.EditorData->AddPropertyBinding(
        FPropertyBindingPath(DuplicateSourceId, TEXT("OutputValue")),
        FPropertyBindingPath(DuplicateTargetId, TEXT("InputValue")));

    USalStateTreeTestSchema* Schema = NewObject<USalStateTreeTestSchema>(Tree.EditorData);
    Tree.EditorData->Schema = Schema;
    const FGuid DuplicateContextId(0xFD000001, 0xFD000002, 0xFD000003, 0xFD000004);
    Schema->ContextData.Add(FStateTreeExternalDataDesc(
        FName(TEXT("ContextObjectA")),
        UObject::StaticClass(),
        DuplicateContextId));
    Schema->ContextData.Add(FStateTreeExternalDataDesc(
        FName(TEXT("ContextObjectB")),
        UObject::StaticClass(),
        DuplicateContextId));

    const auto ExactNode = [&Target](const FGuid& Id)
    {
        FSalQuery Exact = Query(TEXT("node"));
        Exact.Operation->SetStringField(
            TEXT("id"),
            Id.ToString(EGuidFormats::DigitsWithHyphensLower));
        return FSalStateTreeInterface::Query(Exact, Target);
    };
    const TArray<TPair<FString, TSharedPtr<FJsonObject>>> Results = {
        {TEXT("stale Struct ID"), ExactNode(StaleTargetId)},
        {TEXT("unresolvable native path"), ExactNode(BrokenPathTargetId)},
        {TEXT("duplicate endpoint identity"), ExactNode(DuplicateTargetId)},
        {TEXT("duplicate automatic Context identity"), ExactNode(ContextTargetId)},
    };
    for (const TPair<FString, TSharedPtr<FJsonObject>>& Result : Results)
    {
        TestFalse(
            *FString::Printf(TEXT("Exact Node with %s remains readable"), *Result.Key),
            HasError(Result.Value));
        TestEqual(
            *FString::Printf(TEXT("%s never produces a guessed relationship"), *Result.Key),
            EdgeTexts(Result.Value).Num(),
            0);
        TestTrue(
            *FString::Printf(TEXT("%s is reported diagnostically"), *Result.Key),
            HasDiagnosticCode(Result.Value, TEXT("validation.invalid_target")));
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FSalStateTreeEffectiveOutputDirectionTest,
    "Loomle.Sal.StateTree.EffectiveOutputDirection",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSalStateTreeEffectiveOutputDirectionTest::RunTest(const FString& Parameters)
{
    const FTestStateTree Tree = MakeStateTree(MakeTestPackage());
    const FSalResolvedTarget Target = ResolvedTarget(Tree);
    const FGuid StoredOrdinaryProducerId(0xFE000001, 0xFE000002, 0xFE000003, 0xFE000004);
    const FGuid StoredOrdinaryConsumerId(0xFE100001, 0xFE100002, 0xFE100003, 0xFE100004);
    const FGuid StoredOutputProducerId(0xFE200001, 0xFE200002, 0xFE200003, 0xFE200004);
    const FGuid StoredOutputConsumerId(0xFE300001, 0xFE300002, 0xFE300003, 0xFE300004);
    AddBindingTask(Tree.Root->Tasks, StoredOrdinaryProducerId, TEXT("Stored Ordinary Producer"));
    AddBindingTask(Tree.Root->Tasks, StoredOrdinaryConsumerId, TEXT("Stored Ordinary Consumer"));
    AddBindingTask(Tree.Root->Tasks, StoredOutputProducerId, TEXT("Stored Output Producer"));
    AddBindingTask(Tree.Root->Tasks, StoredOutputConsumerId, TEXT("Stored Output Consumer"));

    // Targeting an Output-usage root is effectively an output Binding even when the stored bit is stale.
    Tree.EditorData->AddPropertyBinding(
        FPropertyBindingPath(StoredOrdinaryConsumerId, TEXT("InputValue")),
        FPropertyBindingPath(StoredOrdinaryProducerId, TEXT("OutputValue")));
    // Targeting a normal Input root is effectively ordinary even when the stored bit says output.
    Tree.EditorData->GetPropertyEditorBindings()->AddOutputBinding(
        FPropertyBindingPath(StoredOutputProducerId, TEXT("OutputValue")),
        FPropertyBindingPath(StoredOutputConsumerId, TEXT("InputValue")));

    const TConstArrayView<FStateTreePropertyPathBinding> StoredBindings =
        Tree.EditorData->GetPropertyEditorBindings()->GetBindings();
    TestEqual(TEXT("Output mismatch fixture authored two Bindings"), StoredBindings.Num(), 2);
    if (StoredBindings.Num() != 2)
    {
        return false;
    }
    TestFalse(TEXT("First Binding keeps its stale stored ordinary bit"), StoredBindings[0].IsOutputBinding());
    TestTrue(TEXT("Second Binding keeps its stale stored output bit"), StoredBindings[1].IsOutputBinding());

    const auto ExactNode = [&Target](const FGuid& Id)
    {
        FSalQuery Exact = Query(TEXT("node"));
        Exact.Operation->SetStringField(
            TEXT("id"),
            Id.ToString(EGuidFormats::DigitsWithHyphensLower));
        return FSalStateTreeInterface::Query(Exact, Target);
    };
    const auto NodeRef = [](const FGuid& Id)
    {
        return TEXT("node@") + Id.ToString(EGuidFormats::DigitsWithHyphensLower);
    };
    const FString EffectiveOutputEdge = NodeRef(StoredOrdinaryProducerId)
        + TEXT(".Instance.OutputValue -> ")
        + NodeRef(StoredOrdinaryConsumerId) + TEXT(".Instance.InputValue");
    const FString EffectiveOrdinaryEdge = NodeRef(StoredOutputProducerId)
        + TEXT(".Instance.OutputValue -> ")
        + NodeRef(StoredOutputConsumerId) + TEXT(".Instance.InputValue");

    const TSharedPtr<FJsonObject> EffectiveOutputResult = ExactNode(StoredOrdinaryProducerId);
    const TArray<FString> EffectiveOutputEdges = EdgeTexts(EffectiveOutputResult);
    TestFalse(TEXT("Stored ordinary/effective output read succeeds"), HasError(EffectiveOutputResult));
    TestEqual(TEXT("Stored ordinary/effective output emits one direct edge"), EffectiveOutputEdges.Num(), 1);
    TestTrue(TEXT("Output target Usage determines the real reversed data flow"), EffectiveOutputEdges.Contains(EffectiveOutputEdge));
    TestTrue(
        TEXT("Stored ordinary/effective output mismatch is an informational diagnostic"),
        HasDiagnosticCodeAndSeverity(
            EffectiveOutputResult,
            TEXT("validation.invalid_target"),
            TEXT("info")));

    const TSharedPtr<FJsonObject> EffectiveOrdinaryResult = ExactNode(StoredOutputProducerId);
    const TArray<FString> EffectiveOrdinaryEdges = EdgeTexts(EffectiveOrdinaryResult);
    TestFalse(TEXT("Stored output/effective ordinary read succeeds"), HasError(EffectiveOrdinaryResult));
    TestEqual(TEXT("Stored output/effective ordinary emits one direct edge"), EffectiveOrdinaryEdges.Num(), 1);
    TestTrue(TEXT("Input target Usage preserves ordinary source-to-target flow"), EffectiveOrdinaryEdges.Contains(EffectiveOrdinaryEdge));
    TestTrue(
        TEXT("Stored output/effective ordinary mismatch is an informational diagnostic"),
        HasDiagnosticCodeAndSeverity(
            EffectiveOrdinaryResult,
            TEXT("validation.invalid_target"),
            TEXT("info")));

    const TConstArrayView<FStateTreePropertyPathBinding> AfterQueryBindings =
        Tree.EditorData->GetPropertyEditorBindings()->GetBindings();
    TestFalse(TEXT("Query does not repair the first stored output bit"), AfterQueryBindings[0].IsOutputBinding());
    TestTrue(TEXT("Query does not repair the second stored output bit"), AfterQueryBindings[1].IsOutputBinding());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FSalStateTreeRelationshipDiagnosticIsolationTest,
    "Loomle.Sal.StateTree.RelationshipDiagnosticIsolation",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSalStateTreeRelationshipDiagnosticIsolationTest::RunTest(const FString& Parameters)
{
    const FTestStateTree Tree = MakeStateTree(MakeTestPackage());
    const FSalResolvedTarget Target = ResolvedTarget(Tree);
    const FGuid BrokenTargetId(0xFF000001, 0xFF000002, 0xFF000003, 0xFF000004);
    const FGuid CleanNodeId(0xFF100001, 0xFF100002, 0xFF100003, 0xFF100004);
    AddBindingTask(Tree.Root->Tasks, BrokenTargetId, TEXT("Broken Target"));
    AddBindingTask(Tree.Root->Tasks, CleanNodeId, TEXT("Clean Node"));
    Tree.EditorData->AddPropertyBinding(
        FPropertyBindingPath(FGuid(0xFF200001, 0xFF200002, 0xFF200003, 0xFF200004), TEXT("Missing")),
        FPropertyBindingPath(BrokenTargetId, TEXT("InputValue")));

    const auto ExactNode = [&Target](const FGuid& Id)
    {
        FSalQuery Exact = Query(TEXT("node"));
        Exact.Operation->SetStringField(
            TEXT("id"),
            Id.ToString(EGuidFormats::DigitsWithHyphensLower));
        return FSalStateTreeInterface::Query(Exact, Target);
    };
    const TSharedPtr<FJsonObject> BrokenResult = ExactNode(BrokenTargetId);
    TestFalse(TEXT("Exact owner of a malformed Binding remains readable"), HasError(BrokenResult));
    TestEqual(TEXT("Malformed Binding emits no guessed edge"), EdgeTexts(BrokenResult).Num(), 0);
    TestTrue(
        TEXT("Malformed Binding diagnostic is attached to its resolvable target owner"),
        HasDiagnosticCode(BrokenResult, TEXT("validation.invalid_target")));

    const TSharedPtr<FJsonObject> CleanResult = ExactNode(CleanNodeId);
    TestFalse(TEXT("Unrelated exact Node remains readable"), HasError(CleanResult));
    TestEqual(TEXT("Unrelated exact Node has no relationships"), EdgeTexts(CleanResult).Num(), 0);
    TestFalse(
        TEXT("Unrelated exact Node does not inherit another Binding's diagnostic"),
        HasDiagnosticCode(CleanResult, TEXT("validation.invalid_target")));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FSalStateTreeNativeTargetPathIdentityTest,
    "Loomle.Sal.StateTree.NativeTargetPathIdentity",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSalStateTreeNativeTargetPathIdentityTest::RunTest(const FString& Parameters)
{
    const FTestStateTree Tree = MakeStateTree(MakeTestPackage());
    const FSalResolvedTarget Target = ResolvedTarget(Tree);
    const FGuid SourceAId(0xFF300001, 0xFF300002, 0xFF300003, 0xFF300004);
    const FGuid SourceBId(0xFF400001, 0xFF400002, 0xFF400003, 0xFF400004);
    const FGuid TargetId(0xFF500001, 0xFF500002, 0xFF500003, 0xFF500004);
    AddBindingTask(Tree.Root->Tasks, SourceAId, TEXT("Instanced Source A"));
    AddBindingTask(Tree.Root->Tasks, SourceBId, TEXT("Instanced Source B"));
    AddBindingTask(Tree.Root->Tasks, TargetId, TEXT("Instanced Target"));

    const TArray<FPropertyBindingPathSegment> TargetASegments = {
        FPropertyBindingPathSegment(
            TEXT("InstancedInput"),
            INDEX_NONE,
            FSalStateTreeInstancedInputA::StaticStruct(),
            EPropertyBindingPropertyAccessType::StructInstance),
        FPropertyBindingPathSegment(TEXT("SharedValue")),
    };
    const TArray<FPropertyBindingPathSegment> TargetBSegments = {
        FPropertyBindingPathSegment(
            TEXT("InstancedInput"),
            INDEX_NONE,
            FSalStateTreeInstancedInputB::StaticStruct(),
            EPropertyBindingPropertyAccessType::StructInstance),
        FPropertyBindingPathSegment(TEXT("SharedValue")),
    };
    const FPropertyBindingPath TargetA(TargetId, TargetASegments);
    const FPropertyBindingPath TargetB(TargetId, TargetBSegments);
    TestFalse(
        TEXT("Different InstancedStruct instance types make native Target paths unequal"),
        TargetA == TargetB);
    TestEqual(
        TEXT("Native Target paths intentionally collapse to the same SAL member spelling"),
        TargetA.ToString(),
        TargetB.ToString());

    Tree.EditorData->GetPropertyEditorBindings()->AddStateTreeBinding(
        FStateTreePropertyPathBinding(
            FPropertyBindingPath(SourceAId, TEXT("OutputValue")),
            TargetA,
            false));
    Tree.EditorData->GetPropertyEditorBindings()->AddStateTreeBinding(
        FStateTreePropertyPathBinding(
            FPropertyBindingPath(SourceBId, TEXT("OutputValue")),
            TargetB,
            false));
    const TConstArrayView<FStateTreePropertyPathBinding> Bindings =
        Tree.EditorData->GetPropertyEditorBindings()->GetBindings();
    TestEqual(TEXT("Both native-distinct authored Bindings remain stored"), Bindings.Num(), 2);
    if (Bindings.Num() == 2)
    {
        TestFalse(
            TEXT("Stored Target identity retains the distinct native instance types"),
            Bindings[0].GetTargetPath() == Bindings[1].GetTargetPath());
        TestEqual(
            TEXT("Stored native-distinct Targets still have one SAL member spelling"),
            Bindings[0].GetTargetPath().ToString(),
            Bindings[1].GetTargetPath().ToString());
    }

    FSalQuery Exact = Query(TEXT("node"));
    Exact.Operation->SetStringField(
        TEXT("id"),
        TargetId.ToString(EGuidFormats::DigitsWithHyphensLower));
    const TSharedPtr<FJsonObject> Result = FSalStateTreeInterface::Query(Exact, Target);
    TestFalse(TEXT("Native-distinct Target path read succeeds"), HasError(Result));
    const TArray<FString> Edges = EdgeTexts(Result);
    const FString TargetRef = TEXT("node@")
        + TargetId.ToString(EGuidFormats::DigitsWithHyphensLower)
        + TEXT(".Instance.InstancedInput.SharedValue");
    const FString EdgeA = TEXT("node@")
        + SourceAId.ToString(EGuidFormats::DigitsWithHyphensLower)
        + TEXT(".Instance.OutputValue -> ") + TargetRef;
    const FString EdgeB = TEXT("node@")
        + SourceBId.ToString(EGuidFormats::DigitsWithHyphensLower)
        + TEXT(".Instance.OutputValue -> ") + TargetRef;
    TestEqual(
        TEXT("SAL preserves both authored arrows even though their Target text is identical"),
        Edges.Num(),
        2);
    TestTrue(TEXT("First native Target identity preserves its authored arrow"), Edges.Contains(EdgeA));
    TestTrue(TEXT("Second native Target identity preserves its authored arrow"), Edges.Contains(EdgeB));
    TestFalse(
        TEXT("Native-distinct Targets are not diagnosed as duplicate ordinary Bindings"),
        HasDiagnosticCode(Result, TEXT("validation.invalid_target")));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FSalStateTreeBrokenExplicitContextOverrideTest,
    "Loomle.Sal.StateTree.BrokenExplicitContextOverride",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSalStateTreeBrokenExplicitContextOverrideTest::RunTest(const FString& Parameters)
{
    const FTestStateTree Tree = MakeStateTree(MakeTestPackage());
    const FSalResolvedTarget Target = ResolvedTarget(Tree);
    FInstancedPropertyBag& RootBag = const_cast<FInstancedPropertyBag&>(
        Tree.EditorData->GetRootParametersPropertyBag());
    const FGuid SourceParameterId(0xFF600001, 0xFF600002, 0xFF600003, 0xFF600004);
    FPropertyBagPropertyDesc SourceDesc(
        TEXT("ExplicitObject"),
        EPropertyBagPropertyType::Object,
        UObject::StaticClass());
    SourceDesc.ID = SourceParameterId;
    RootBag.AddProperties({SourceDesc});

    USalStateTreeTestSchema* Schema = NewObject<USalStateTreeTestSchema>(Tree.EditorData);
    Tree.EditorData->Schema = Schema;
    const FGuid ContextId(0xFF700001, 0xFF700002, 0xFF700003, 0xFF700004);
    Schema->ContextData.Add(FStateTreeExternalDataDesc(
        FName(TEXT("ContextObject")),
        UObject::StaticClass(),
        ContextId));

    const FGuid ContextTargetId(0xFF800001, 0xFF800002, 0xFF800003, 0xFF800004);
    AddContextTask(Tree.Root->Tasks, ContextTargetId, TEXT("Broken Explicit Context"));
    const TArray<FPropertyBindingPathSegment> BrokenTargetSegments = {
        FPropertyBindingPathSegment(TEXT("ContextObject")),
        FPropertyBindingPathSegment(TEXT("MissingMember")),
    };
    Tree.EditorData->AddPropertyBinding(
        FPropertyBindingPath(
            Tree.EditorData->GetRootParametersGuid(),
            TEXT("ExplicitObject")),
        FPropertyBindingPath(ContextTargetId, BrokenTargetSegments));

    FSalQuery Exact = Query(TEXT("node"));
    Exact.Operation->SetStringField(
        TEXT("id"),
        ContextTargetId.ToString(EGuidFormats::DigitsWithHyphensLower));
    const TSharedPtr<FJsonObject> Result = FSalStateTreeInterface::Query(Exact, Target);
    TestFalse(TEXT("Node with a damaged explicit Context override remains readable"), HasError(Result));
    TestEqual(
        TEXT("Damaged explicit Context override emits neither a guessed nor automatic arrow"),
        EdgeTexts(Result).Num(),
        0);
    TestTrue(
        TEXT("Damaged explicit Context override remains visible as a diagnostic"),
        HasDiagnosticCode(Result, TEXT("validation.invalid_target")));
    const FString AutomaticEdge = TEXT("object@")
        + ContextId.ToString(EGuidFormats::DigitsWithHyphensLower)
        + TEXT(" -> node@")
        + ContextTargetId.ToString(EGuidFormats::DigitsWithHyphensLower)
        + TEXT(".Instance.ContextObject");
    TestFalse(
        TEXT("Presence of the authored target root suppresses automatic Context fallback"),
        EdgeTexts(Result).Contains(AutomaticEdge));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FSalStateTreeOwnerlessRelationshipDiagnosticTest,
    "Loomle.Sal.StateTree.OwnerlessRelationshipDiagnostic",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSalStateTreeOwnerlessRelationshipDiagnosticTest::RunTest(const FString& Parameters)
{
    const FTestStateTree Tree = MakeStateTree(MakeTestPackage());
    const FSalResolvedTarget Target = ResolvedTarget(Tree);
    const FGuid CleanNodeId(0xFF900001, 0xFF900002, 0xFF900003, 0xFF900004);
    AddBindingTask(Tree.Root->Tasks, CleanNodeId, TEXT("Clean Exact Node"));
    Tree.EditorData->AddPropertyBinding(
        FPropertyBindingPath(
            FGuid(0xFFA00001, 0xFFA00002, 0xFFA00003, 0xFFA00004),
            TEXT("MissingSource")),
        FPropertyBindingPath(
            FGuid(0xFFB00001, 0xFFB00002, 0xFFB00003, 0xFFB00004),
            TEXT("MissingTarget")));

    const TSharedPtr<FJsonObject> Summary = FSalStateTreeInterface::Query(
        Query(TEXT("summary")),
        Target);
    TestFalse(TEXT("Summary with a fully ownerless Binding remains readable"), HasError(Summary));
    TestTrue(
        TEXT("Summary exposes a Binding whose two native endpoint owners are unknown"),
        HasDiagnosticCode(Summary, TEXT("validation.invalid_target")));

    FSalQuery Exact = Query(TEXT("node"));
    Exact.Operation->SetStringField(
        TEXT("id"),
        CleanNodeId.ToString(EGuidFormats::DigitsWithHyphensLower));
    const TSharedPtr<FJsonObject> CleanResult = FSalStateTreeInterface::Query(Exact, Target);
    TestFalse(TEXT("Clean exact Node remains readable"), HasError(CleanResult));
    TestEqual(TEXT("Clean exact Node has no relationship arrows"), EdgeTexts(CleanResult).Num(), 0);
    TestFalse(
        TEXT("Ownerless Binding diagnostic does not leak into an unrelated exact object"),
        HasDiagnosticCode(CleanResult, TEXT("validation.invalid_target")));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FSalStateTreeUnresolvedParameterDescriptorDiagnosticTest,
    "Loomle.Sal.StateTree.UnresolvedParameterDescriptorDiagnostic",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSalStateTreeUnresolvedParameterDescriptorDiagnosticTest::RunTest(const FString& Parameters)
{
    const FTestStateTree Tree = MakeStateTree(MakeTestPackage());
    const FSalResolvedTarget Target = ResolvedTarget(Tree);
    const FGuid FirstParameterId(0xFFC00001, 0xFFC00002, 0xFFC00003, 0xFFC00004);
    const FGuid SecondParameterId(0xFFD00001, 0xFFD00002, 0xFFD00003, 0xFFD00004);
    FInstancedPropertyBag& RootBag = const_cast<FInstancedPropertyBag&>(
        Tree.EditorData->GetRootParametersPropertyBag());
    RootBag.AddProperties({
        ParameterDesc(TEXT("FirstParameter"), EPropertyBagPropertyType::Int32, FirstParameterId),
        ParameterDesc(TEXT("SecondParameter"), EPropertyBagPropertyType::Int32, SecondParameterId),
    });
    const FGuid TargetNodeId(0xFFE00001, 0xFFE00002, 0xFFE00003, 0xFFE00004);
    AddBindingTask(Tree.Root->Tasks, TargetNodeId, TEXT("Bad Parameter Target"));
    const FGuid RootContainerId = Tree.EditorData->GetRootParametersGuid();
    Tree.EditorData->AddPropertyBinding(
        FPropertyBindingPath(RootContainerId, TEXT("MissingDescriptor")),
        FPropertyBindingPath(TargetNodeId, TEXT("InputValue")));

    const TSharedPtr<FJsonObject> Summary = FSalStateTreeInterface::Query(
        Query(TEXT("summary")),
        Target);
    TestFalse(TEXT("Summary with an unresolved Parameter member remains readable"), HasError(Summary));
    TestTrue(
        TEXT("Summary exposes the unresolved Parameter-container Binding"),
        HasDiagnosticCode(Summary, TEXT("validation.invalid_target")));

    const auto ExactParameter = [&Target, &RootContainerId](const FGuid& PropertyId)
    {
        FSalQuery Exact = Query(TEXT("parameter"));
        Exact.Operation->SetStringField(TEXT("id"), ParameterId(RootContainerId, PropertyId));
        return FSalStateTreeInterface::Query(Exact, Target);
    };
    const TSharedPtr<FJsonObject> FirstResult = ExactParameter(FirstParameterId);
    TestFalse(TEXT("First Parameter remains readable"), HasError(FirstResult));
    TestFalse(
        TEXT("Unresolved container path is not guessed to belong to the first Parameter"),
        HasDiagnosticCode(FirstResult, TEXT("validation.invalid_target")));
    const TSharedPtr<FJsonObject> SecondResult = ExactParameter(SecondParameterId);
    TestFalse(TEXT("Second Parameter remains readable"), HasError(SecondResult));
    TestFalse(
        TEXT("Unresolved container path is not guessed to belong to any Parameter"),
        HasDiagnosticCode(SecondResult, TEXT("validation.invalid_target")));

    FSalQuery ExactTarget = Query(TEXT("node"));
    ExactTarget.Operation->SetStringField(
        TEXT("id"),
        TargetNodeId.ToString(EGuidFormats::DigitsWithHyphensLower));
    const TSharedPtr<FJsonObject> TargetResult = FSalStateTreeInterface::Query(ExactTarget, Target);
    TestEqual(TEXT("Unresolved Parameter path emits no guessed arrow"), EdgeTexts(TargetResult).Num(), 0);
    TestTrue(
        TEXT("Diagnostic remains scoped to the Binding's resolvable target owner"),
        HasDiagnosticCode(TargetResult, TEXT("validation.invalid_target")));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FSalStateTreeRelationshipAnalysisBudgetTest,
    "Loomle.Sal.StateTree.RelationshipAnalysisBudget",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSalStateTreeRelationshipAnalysisBudgetTest::RunTest(const FString& Parameters)
{
    const FTestStateTree Tree = MakeStateTree(MakeTestPackage());
    const FSalResolvedTarget Target = ResolvedTarget(Tree);
    const FGuid NodeId(0xFFF00001, 0xFFF00002, 0xFFF00003, 0xFFF00004);
    AddBindingTask(Tree.Root->Tasks, NodeId, TEXT("Budget Target"));

    constexpr int32 RawBindingCount = 50001;
    if (!TestTrue(
            TEXT("Raw authored Binding fixture can exceed the relationship analysis budget without editor-side repair"),
            AppendRawPropertyBindings(
                *Tree.EditorData->GetPropertyEditorBindings(),
                RawBindingCount,
                FPropertyBindingPath(NodeId),
                FPropertyBindingPath(NodeId))))
    {
        return false;
    }
    TestEqual(
        TEXT("Relationship budget fixture contains every raw authored Binding"),
        Tree.EditorData->GetPropertyEditorBindings()->GetBindings().Num(),
        RawBindingCount);

    FSalQuery Exact = Query(TEXT("node"));
    Exact.Operation->SetStringField(
        TEXT("id"),
        NodeId.ToString(EGuidFormats::DigitsWithHyphensLower));
    const TSharedPtr<FJsonObject> Result = FSalStateTreeInterface::Query(Exact, Target);
    TestFalse(TEXT("Relationship budget overflow remains a readable exact Query"), HasError(Result));
    TestEqual(TEXT("Relationship budget overflow emits no partial arrows"), EdgeTexts(Result).Num(), 0);
    TestTrue(
        TEXT("Relationship budget overflow returns the dedicated incomplete diagnostic"),
        HasDiagnosticCode(Result, TEXT("validation.reference_scan_incomplete")));
    return true;
}

#endif
