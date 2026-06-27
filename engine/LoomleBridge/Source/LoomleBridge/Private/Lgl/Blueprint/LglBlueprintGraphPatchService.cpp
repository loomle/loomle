// Copyright 2026 Loomle contributors.

#include "LglBlueprintGraphPatchService.h"

#include "BlueprintActionMenuItem.h"
#include "BlueprintNodeSpawner.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraph/EdGraphSchema.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "../../LoomleMutationResult.h"
#include "../Graph/LglGraphProtocol.h"
#include "../LglDiagnostics.h"
#include "LglBlueprintGraphPaletteService.h"
#include "LglBlueprintGraphResolver.h"

namespace Loomle::Lgl
{
namespace
{
FString SanitizeAlias(const FString& Text)
{
    FString Alias;
    Alias.Reserve(Text.Len());
    for (const TCHAR Character : Text)
    {
        if (FChar::IsAlnum(Character) || Character == TEXT('_'))
        {
            Alias.AppendChar(Character);
        }
        else
        {
            Alias.AppendChar(TEXT('_'));
        }
    }

    if (Alias.IsEmpty())
    {
        Alias = TEXT("node");
    }
    if (FChar::IsDigit(Alias[0]))
    {
        Alias.InsertAt(0, TEXT('_'));
    }
    return Alias;
}

FString MakeUniqueAlias(const FString& Base, TSet<FString>& UsedAliases)
{
    FString Alias = SanitizeAlias(Base);
    int32 Suffix = 2;
    while (UsedAliases.Contains(Alias))
    {
        Alias = FString::Printf(TEXT("%s_%d"), *SanitizeAlias(Base), Suffix++);
    }
    UsedAliases.Add(Alias);
    return Alias;
}

FString NodeId(const UEdGraphNode* Node)
{
    return Node != nullptr ? Node->NodeGuid.ToString(EGuidFormats::DigitsWithHyphensLower) : FString();
}

FString NodeType(const UEdGraphNode* Node)
{
    return Node != nullptr && Node->GetClass() != nullptr ? Node->GetClass()->GetName() : TEXT("Node");
}

FString NodeTitle(const UEdGraphNode* Node)
{
    return Node != nullptr ? Node->GetNodeTitle(ENodeTitleType::ListView).ToString() : FString();
}

bool TryExprToPinDefaultString(const TSharedPtr<FJsonValue>& Value, FString& OutString)
{
    OutString.Reset();
    if (!Value.IsValid() || Value->IsNull())
    {
        return false;
    }

    FString StringValue;
    if (Value->TryGetString(StringValue))
    {
        OutString = StringValue;
        return true;
    }

    double NumberValue = 0.0;
    if (Value->TryGetNumber(NumberValue))
    {
        OutString = FString::SanitizeFloat(NumberValue);
        return true;
    }

    bool BoolValue = false;
    if (Value->TryGetBool(BoolValue))
    {
        OutString = BoolValue ? TEXT("true") : TEXT("false");
        return true;
    }

    const TSharedPtr<FJsonObject>* ObjectValue = nullptr;
    if (Value->TryGetObject(ObjectValue) && ObjectValue != nullptr && (*ObjectValue).IsValid())
    {
        FString Kind;
        FString Name;
        if ((*ObjectValue)->TryGetStringField(TEXT("kind"), Kind)
            && (Kind == TEXT("name") || Kind == TEXT("local") || Kind == TEXT("id"))
            && (*ObjectValue)->TryGetStringField(Kind == TEXT("id") ? TEXT("id") : TEXT("name"), Name))
        {
            OutString = Name;
            return true;
        }
    }

    return false;
}

FString ReadLocalBindingName(const TSharedPtr<FJsonObject>& Binding)
{
    const TSharedPtr<FJsonObject>* Target = nullptr;
    if (!Binding.IsValid()
        || !Binding->TryGetObjectField(TEXT("target"), Target)
        || Target == nullptr
        || !(*Target).IsValid())
    {
        return FString();
    }

    FString Kind;
    FString Name;
    if ((*Target)->TryGetStringField(TEXT("kind"), Kind)
        && Kind == TEXT("local")
        && (*Target)->TryGetStringField(TEXT("name"), Name))
    {
        return Name;
    }
    return FString();
}

TMap<FString, TSharedPtr<FJsonObject>> ReadPatchBindings(const FLglObjectRequest& Request)
{
    TMap<FString, TSharedPtr<FJsonObject>> Bindings;
    const TArray<TSharedPtr<FJsonValue>>* BindingValues = nullptr;
    if (!Request.Object.IsValid()
        || !Request.Object->TryGetArrayField(TEXT("bindings"), BindingValues)
        || BindingValues == nullptr)
    {
        return Bindings;
    }

    for (const TSharedPtr<FJsonValue>& BindingValue : *BindingValues)
    {
        const TSharedPtr<FJsonObject>* Binding = nullptr;
        if (!BindingValue.IsValid()
            || !BindingValue->TryGetObject(Binding)
            || Binding == nullptr
            || !(*Binding).IsValid())
        {
            continue;
        }

        const FString Name = ReadLocalBindingName(*Binding);
        if (!Name.IsEmpty())
        {
            Bindings.Add(Name, *Binding);
        }
    }
    return Bindings;
}

UEdGraphNode* TemplateNodeForPatchAction(
    const TSharedPtr<FEdGraphSchemaAction>& Action,
    const FLglBlueprintResolvedGraph& ResolvedGraph)
{
    if (!Action.IsValid() || Action->GetTypeId() != FBlueprintActionMenuItem::StaticGetTypeId())
    {
        return nullptr;
    }

    const FBlueprintActionMenuItem* MenuItem = static_cast<const FBlueprintActionMenuItem*>(Action.Get());
    const UBlueprintNodeSpawner* Spawner = MenuItem != nullptr ? MenuItem->GetRawAction() : nullptr;
    UEdGraphNode* TemplateNode = Spawner != nullptr ? Spawner->GetTemplateNode(ResolvedGraph.Graph) : nullptr;
    if (TemplateNode != nullptr && TemplateNode->Pins.Num() == 0)
    {
        TemplateNode->AllocateDefaultPins();
    }
    return TemplateNode;
}

UEdGraphPin* FindEditableInputPin(UEdGraphNode* Node, const FString& PinName)
{
    if (Node == nullptr)
    {
        return nullptr;
    }
    UEdGraphPin* Pin = Node->FindPin(*PinName);
    return Pin != nullptr && Pin->Direction == EGPD_Input ? Pin : nullptr;
}

void BuildPatchNodesByAlias(const FLglBlueprintResolvedGraph& ResolvedGraph, TMap<FString, UEdGraphNode*>& OutNodesByAlias)
{
    OutNodesByAlias.Reset();
    TSet<FString> UsedAliases;
    if (ResolvedGraph.Graph == nullptr)
    {
        return;
    }

    for (UEdGraphNode* Node : ResolvedGraph.Graph->Nodes)
    {
        if (Node == nullptr)
        {
            continue;
        }

        const FString AliasBase = !NodeTitle(Node).IsEmpty() ? NodeTitle(Node) : NodeType(Node);
        OutNodesByAlias.Add(MakeUniqueAlias(AliasBase, UsedAliases), Node);
    }
}

UEdGraphPin* FindPatchPin(const TMap<FString, UEdGraphNode*>& NodesByAlias, const FString& NodeAlias, const FString& PinName)
{
    UEdGraphNode* const* Node = NodesByAlias.Find(NodeAlias);
    return Node != nullptr && *Node != nullptr ? (*Node)->FindPin(*PinName) : nullptr;
}

bool PinHasLinkTo(const UEdGraphPin* FromPin, const UEdGraphPin* ToPin)
{
    return FromPin != nullptr && ToPin != nullptr && FromPin->LinkedTo.Contains(const_cast<UEdGraphPin*>(ToPin));
}

FVector2f NextPatchNodeLocation(const FLglBlueprintResolvedGraph& ResolvedGraph)
{
    if (ResolvedGraph.Graph == nullptr || ResolvedGraph.Graph->Nodes.Num() == 0)
    {
        return FVector2f(0.0f, 0.0f);
    }

    float Right = 0.0f;
    float Top = 0.0f;
    bool bHasNode = false;
    for (const UEdGraphNode* Node : ResolvedGraph.Graph->Nodes)
    {
        if (Node == nullptr)
        {
            continue;
        }

        const float Width = Node->NodeWidth > 0 ? static_cast<float>(Node->NodeWidth) : 240.0f;
        Right = bHasNode ? FMath::Max(Right, static_cast<float>(Node->NodePosX) + Width) : static_cast<float>(Node->NodePosX) + Width;
        Top = bHasNode ? FMath::Min(Top, static_cast<float>(Node->NodePosY)) : static_cast<float>(Node->NodePosY);
        bHasNode = true;
    }
    return FVector2f(Right + 320.0f, Top);
}

struct FLglGraphAddPlan
{
    int32 Index = 0;
    FString BindingName;
    FString PaletteId;
    TMap<FString, FString> Defaults;
    TSharedPtr<FEdGraphSchemaAction> Action;
    FString NodeId;
};

struct FLglGraphConnectPlan
{
    int32 Index = 0;
    FLglGraphEdge Edge;
};

struct FLglGraphPatchCommand
{
    FString Kind;
    int32 PlanIndex = INDEX_NONE;
};

struct FLglGraphPatchPlanData
{
    TArray<FLglGraphAddPlan> Adds;
    TArray<FLglGraphConnectPlan> Connects;
    TArray<FLglGraphPatchCommand> Commands;
    TArray<TSharedPtr<FJsonValue>> OpResults;
    TArray<TSharedPtr<FJsonValue>> Diagnostics;
    bool bValid = true;
};

TSharedPtr<FJsonObject> MakeGraphPatchOpResult(
    int32 Index,
    const FString& Operation,
    bool bOk,
    bool bChanged,
    const FString& ErrorCode = FString(),
    const FString& ErrorMessage = FString())
{
    return LoomleMutation::MakeOpResult(Index, Operation, bOk, bChanged, ErrorCode, ErrorMessage);
}

void AddFailedPatchOp(
    FLglGraphPatchPlanData& Plan,
    int32 Index,
    const FString& Operation,
    const FString& Code,
    const FString& Message)
{
    Plan.bValid = false;
    Plan.OpResults.Add(MakeShared<FJsonValueObject>(
        MakeGraphPatchOpResult(Index, Operation, false, false, Code, Message)));
    Plan.Diagnostics.Add(MakeGraphPatchDiagnostic(TEXT("error"), Code, Message, Index, Operation));
}

void AddValidPatchOp(
    FLglGraphPatchPlanData& Plan,
    int32 Index,
    const FString& Operation,
    const FString& BindingName,
    const FString& PaletteId,
    bool bChanged,
    const FString& NodeIdValue = FString())
{
    TSharedPtr<FJsonObject> OpResult = MakeGraphPatchOpResult(Index, Operation, true, bChanged);
    if (!BindingName.IsEmpty())
    {
        OpResult->SetStringField(TEXT("binding"), BindingName);
    }
    if (!PaletteId.IsEmpty())
    {
        OpResult->SetStringField(TEXT("palette"), PaletteId);
    }
    if (!NodeIdValue.IsEmpty())
    {
        OpResult->SetStringField(TEXT("nodeId"), NodeIdValue);
    }
    Plan.OpResults.Add(MakeShared<FJsonValueObject>(OpResult));
}

void AddValidConnectOp(
    FLglGraphPatchPlanData& Plan,
    int32 Index,
    const FLglGraphConnectPlan& ConnectPlan,
    bool bChanged)
{
    TSharedPtr<FJsonObject> OpResult = MakeGraphPatchOpResult(Index, TEXT("connect"), true, bChanged);
    OpResult->SetObjectField(TEXT("edge"), MakeGraphEdgeObject(ConnectPlan.Edge));
    Plan.OpResults.Add(MakeShared<FJsonValueObject>(OpResult));
}

bool ReadPaletteNodeBinding(
    const TSharedPtr<FJsonObject>& Binding,
    FString& OutPaletteId,
    TMap<FString, FString>& OutDefaults,
    FString& OutErrorCode,
    FString& OutErrorMessage)
{
    OutPaletteId.Reset();
    OutDefaults.Reset();
    OutErrorCode.Reset();
    OutErrorMessage.Reset();

    const TSharedPtr<FJsonObject>* Value = nullptr;
    if (!Binding.IsValid()
        || !Binding->TryGetObjectField(TEXT("value"), Value)
        || Value == nullptr
        || !(*Value).IsValid())
    {
        OutErrorCode = TEXT("language.invalid_object_shape");
        OutErrorMessage = TEXT("Add operation binding is missing value object.");
        return false;
    }

    FString CreationKind;
    if (!(*Value)->TryGetStringField(TEXT("kind"), CreationKind))
    {
        OutErrorCode = TEXT("language.invalid_object_shape");
        OutErrorMessage = TEXT("Add operation binding value is missing kind.");
        return false;
    }

    if (CreationKind != TEXT("palette_node"))
    {
        OutErrorCode = TEXT("capability.not_implemented");
        OutErrorMessage = FString::Printf(
            TEXT("Graph add supports palette_node bindings first; %s is not implemented yet."),
            *CreationKind);
        return false;
    }

    if (!(*Value)->TryGetStringField(TEXT("palette"), OutPaletteId) || OutPaletteId.IsEmpty())
    {
        OutErrorCode = TEXT("language.invalid_object_shape");
        OutErrorMessage = TEXT("Palette node binding is missing required string field palette.");
        return false;
    }

    const TSharedPtr<FJsonObject>* Defaults = nullptr;
    if ((*Value)->TryGetObjectField(TEXT("defaults"), Defaults) && Defaults != nullptr && (*Defaults).IsValid())
    {
        for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : (*Defaults)->Values)
        {
            FString DefaultValue;
            if (!TryExprToPinDefaultString(Pair.Value, DefaultValue))
            {
                OutErrorCode = TEXT("language.unsupported_value");
                OutErrorMessage = FString::Printf(
                    TEXT("Default value for pin %s cannot be converted to a Blueprint pin default string."),
                    *Pair.Key);
                return false;
            }
            OutDefaults.Add(Pair.Key, DefaultValue);
        }
    }

    return true;
}

bool ValidatePatchDefaultPins(
    const FLglGraphAddPlan& AddPlan,
    const FLglBlueprintResolvedGraph& ResolvedGraph,
    FString& OutErrorCode,
    FString& OutErrorMessage)
{
    OutErrorCode.Reset();
    OutErrorMessage.Reset();
    UEdGraphNode* TemplateNode = TemplateNodeForPatchAction(AddPlan.Action, ResolvedGraph);
    if (TemplateNode == nullptr)
    {
        OutErrorCode = TEXT("resolution.palette_not_spawnable");
        OutErrorMessage = FString::Printf(
            TEXT("Palette entry %s is not a Blueprint node creation action."),
            *AddPlan.PaletteId);
        return false;
    }

    for (const TPair<FString, FString>& Default : AddPlan.Defaults)
    {
        if (FindEditableInputPin(TemplateNode, Default.Key) == nullptr)
        {
            OutErrorCode = TEXT("resolution.pin_not_found");
            OutErrorMessage = FString::Printf(
                TEXT("Palette node %s does not expose editable input pin %s."),
                *AddPlan.PaletteId,
                *Default.Key);
            return false;
        }
    }
    return true;
}

bool ReadConnectEdge(
    const TSharedPtr<FJsonObject>& Op,
    FLglGraphConnectPlan& OutConnect,
    FString& OutErrorCode,
    FString& OutErrorMessage)
{
    OutConnect = {};
    const TSharedPtr<FJsonObject>* Edge = nullptr;
    if (!Op.IsValid()
        || !Op->TryGetObjectField(TEXT("edge"), Edge)
        || Edge == nullptr
        || !(*Edge).IsValid())
    {
        OutErrorCode = TEXT("language.invalid_object_shape");
        OutErrorMessage = TEXT("Graph connect operation is missing required edge object.");
        return false;
    }

    return ReadGraphEdge(*Edge, OutConnect.Edge, OutErrorCode, OutErrorMessage);
}

bool ValidatePatchPinExists(
    const TMap<FString, UEdGraphNode*>& ExistingNodesByAlias,
    const TMap<FString, UEdGraphNode*>& TemplateNodesByAlias,
    const FLglGraphPinRef& PinRef,
    UEdGraphPin*& OutPin,
    FString& OutErrorCode,
    FString& OutErrorMessage)
{
    OutPin = FindPatchPin(ExistingNodesByAlias, PinRef.Node, PinRef.Pin);
    if (OutPin != nullptr)
    {
        return true;
    }

    OutPin = FindPatchPin(TemplateNodesByAlias, PinRef.Node, PinRef.Pin);
    if (OutPin != nullptr)
    {
        return true;
    }

    if (!ExistingNodesByAlias.Contains(PinRef.Node) && !TemplateNodesByAlias.Contains(PinRef.Node))
    {
        OutErrorCode = TEXT("resolution.node_not_found");
        OutErrorMessage = FString::Printf(TEXT("Graph connect node %s was not found."), *PinRef.Node);
        return false;
    }

    OutErrorCode = TEXT("resolution.pin_not_found");
    OutErrorMessage = FString::Printf(TEXT("Graph connect pin %s.%s was not found."), *PinRef.Node, *PinRef.Pin);
    return false;
}

bool ValidateConnectResponse(
    const FPinConnectionResponse& Response,
    FString& OutErrorCode,
    FString& OutErrorMessage)
{
    if (Response.Response == CONNECT_RESPONSE_DISALLOW)
    {
        OutErrorCode = TEXT("resolution.pin_not_connectable");
        OutErrorMessage = FString::Printf(TEXT("Pins cannot be connected. %s"), *Response.Message.ToString());
        return false;
    }

    if (Response.Response == CONNECT_RESPONSE_MAKE_WITH_CONVERSION_NODE
        || Response.Response == CONNECT_RESPONSE_MAKE_WITH_PROMOTION)
    {
        OutErrorCode = TEXT("capability.not_implemented");
        OutErrorMessage = FString::Printf(
            TEXT("Pins require conversion or promotion nodes, which LGL graph connect does not auto-insert yet. %s"),
            *Response.Message.ToString());
        return false;
    }

    return true;
}

bool ValidateConnectPlan(
    const FLglGraphConnectPlan& ConnectPlan,
    const TMap<FString, UEdGraphNode*>& ExistingNodesByAlias,
    const TMap<FString, UEdGraphNode*>& TemplateNodesByAlias,
    FString& OutErrorCode,
    FString& OutErrorMessage)
{
    UEdGraphPin* FromPin = nullptr;
    if (!ValidatePatchPinExists(ExistingNodesByAlias, TemplateNodesByAlias, ConnectPlan.Edge.From, FromPin, OutErrorCode, OutErrorMessage))
    {
        return false;
    }

    UEdGraphPin* ToPin = nullptr;
    if (!ValidatePatchPinExists(ExistingNodesByAlias, TemplateNodesByAlias, ConnectPlan.Edge.To, ToPin, OutErrorCode, OutErrorMessage))
    {
        return false;
    }

    if (FromPin->Direction != EGPD_Output || ToPin->Direction != EGPD_Input)
    {
        OutErrorCode = TEXT("resolution.pin_direction_mismatch");
        OutErrorMessage = TEXT("Graph connect requires from to be an output pin and to to be an input pin.");
        return false;
    }

    if (FromPin->GetOwningNode() == ToPin->GetOwningNode())
    {
        OutErrorCode = TEXT("resolution.pin_not_connectable");
        OutErrorMessage = TEXT("Graph connect cannot connect pins on the same node.");
        return false;
    }

    const bool bUsesTemplatePin =
        !ExistingNodesByAlias.Contains(ConnectPlan.Edge.From.Node)
        || !ExistingNodesByAlias.Contains(ConnectPlan.Edge.To.Node);
    if (bUsesTemplatePin)
    {
        return true;
    }

    if (PinHasLinkTo(FromPin, ToPin))
    {
        return true;
    }

    const UEdGraphSchema* Schema = FromPin->GetSchema();
    if (Schema == nullptr)
    {
        OutErrorCode = TEXT("resolution.graph_schema_missing");
        OutErrorMessage = TEXT("Graph connect could not resolve a schema for the source pin.");
        return false;
    }

    return ValidateConnectResponse(Schema->CanCreateConnection(FromPin, ToPin), OutErrorCode, OutErrorMessage);
}

FLglGraphPatchPlanData BuildGraphPatchPlanData(
    const FLglObjectRequest& Request,
    const FLglBlueprintResolvedGraph& ResolvedGraph,
    const TArray<TSharedPtr<FJsonValue>>& Ops)
{
    FLglGraphPatchPlanData Plan;
    const TMap<FString, TSharedPtr<FJsonObject>> Bindings = ReadPatchBindings(Request);
    TMap<FString, UEdGraphNode*> ExistingNodesByAlias;
    BuildPatchNodesByAlias(ResolvedGraph, ExistingNodesByAlias);
    TMap<FString, UEdGraphNode*> TemplateNodesByAlias;
    FLglBlueprintGraphPaletteService PaletteService;

    for (int32 Index = 0; Index < Ops.Num(); ++Index)
    {
        const FString Kind = ReadGraphPatchOpKind(Ops[Index]);
        const FString Operation = Kind.IsEmpty() ? TEXT("unknown") : Kind;

        const TSharedPtr<FJsonObject>* Op = nullptr;
        if (!Ops[Index].IsValid()
            || !Ops[Index]->TryGetObject(Op)
            || Op == nullptr
            || !(*Op).IsValid())
        {
            AddFailedPatchOp(
                Plan,
                Index,
                Operation,
                TEXT("language.invalid_object_shape"),
                TEXT("Graph patch operation must be an object."));
            continue;
        }

        if (Kind == TEXT("connect"))
        {
            FLglGraphConnectPlan ConnectPlan;
            ConnectPlan.Index = Index;
            FString ErrorCode;
            FString ErrorMessage;
            if (!ReadConnectEdge(*Op, ConnectPlan, ErrorCode, ErrorMessage))
            {
                AddFailedPatchOp(Plan, Index, Operation, ErrorCode, ErrorMessage);
                continue;
            }

            if (!ValidateConnectPlan(
                ConnectPlan,
                ExistingNodesByAlias,
                TemplateNodesByAlias,
                ErrorCode,
                ErrorMessage))
            {
                AddFailedPatchOp(Plan, Index, Operation, ErrorCode, ErrorMessage);
                continue;
            }

            const int32 ConnectPlanIndex = Plan.Connects.Add(ConnectPlan);
            Plan.Commands.Add({TEXT("connect"), ConnectPlanIndex});
            AddValidConnectOp(Plan, Index, ConnectPlan, false);
            continue;
        }

        if (Kind != TEXT("add"))
        {
            AddFailedPatchOp(
                Plan,
                Index,
                Operation,
                TEXT("capability.not_implemented"),
                FString::Printf(TEXT("Graph patch operation %s is not implemented in the LGL bridge yet."), *Operation));
            continue;
        }

        FString BindingName;
        if (!(*Op)->TryGetStringField(TEXT("binding"), BindingName) || BindingName.IsEmpty())
        {
            AddFailedPatchOp(
                Plan,
                Index,
                Operation,
                TEXT("language.invalid_object_shape"),
                TEXT("Graph add operation is missing required string field binding."));
            continue;
        }

        const TSharedPtr<FJsonObject>* Binding = Bindings.Find(BindingName);
        if (Binding == nullptr || !(*Binding).IsValid())
        {
            AddFailedPatchOp(
                Plan,
                Index,
                Operation,
                TEXT("resolution.binding_not_found"),
                FString::Printf(TEXT("Graph add binding %s was not found."), *BindingName));
            continue;
        }

        FLglGraphAddPlan AddPlan;
        AddPlan.Index = Index;
        AddPlan.BindingName = BindingName;
        FString ErrorCode;
        FString ErrorMessage;
        if (!ReadPaletteNodeBinding(*Binding, AddPlan.PaletteId, AddPlan.Defaults, ErrorCode, ErrorMessage))
        {
            AddFailedPatchOp(Plan, Index, Operation, ErrorCode, ErrorMessage);
            continue;
        }

        FLglObjectResult PaletteError;
        if (!PaletteService.ResolvePaletteAction(ResolvedGraph, AddPlan.PaletteId, AddPlan.Action, PaletteError))
        {
            FString Message = FString::Printf(TEXT("Palette entry %s was not found for this graph context."), *AddPlan.PaletteId);
            if (PaletteError.Diagnostics.Num() > 0 && PaletteError.Diagnostics[0].IsValid())
            {
                PaletteError.Diagnostics[0]->TryGetStringField(TEXT("message"), Message);
            }
            AddFailedPatchOp(Plan, Index, Operation, TEXT("resolution.palette_not_found"), Message);
            continue;
        }

        if (!ValidatePatchDefaultPins(AddPlan, ResolvedGraph, ErrorCode, ErrorMessage))
        {
            AddFailedPatchOp(Plan, Index, Operation, ErrorCode, ErrorMessage);
            continue;
        }

        UEdGraphNode* TemplateNode = TemplateNodeForPatchAction(AddPlan.Action, ResolvedGraph);
        if (TemplateNode != nullptr)
        {
            TemplateNodesByAlias.Add(BindingName, TemplateNode);
        }

        const int32 AddPlanIndex = Plan.Adds.Add(AddPlan);
        Plan.Commands.Add({TEXT("add"), AddPlanIndex});
        AddValidPatchOp(Plan, Index, Operation, BindingName, AddPlan.PaletteId, false);
    }

    return Plan;
}

bool ApplyGraphPatchPlan(
    FLglGraphPatchPlanData& Plan,
    const FLglBlueprintResolvedGraph& ResolvedGraph,
    const TSharedPtr<FJsonObject>& ResolvedRefs)
{
    if (!Plan.bValid || ResolvedGraph.Graph == nullptr)
    {
        return false;
    }

    TMap<FString, UEdGraphNode*> NodesByAlias;
    BuildPatchNodesByAlias(ResolvedGraph, NodesByAlias);
    FVector2f Location = NextPatchNodeLocation(ResolvedGraph);
    TArray<TSharedPtr<FJsonValue>> AppliedOpResults;
    AppliedOpResults.Reserve(Plan.Commands.Num());

    for (const FLglGraphPatchCommand& Command : Plan.Commands)
    {
        if (Command.Kind == TEXT("add"))
        {
            if (!Plan.Adds.IsValidIndex(Command.PlanIndex))
            {
                AddFailedPatchOp(Plan, INDEX_NONE, TEXT("add"), TEXT("internal.invalid_plan"), TEXT("Graph add plan index is invalid."));
                return false;
            }

            FLglGraphAddPlan& AddPlan = Plan.Adds[Command.PlanIndex];
            if (!AddPlan.Action.IsValid() || AddPlan.Action->GetTypeId() != FBlueprintActionMenuItem::StaticGetTypeId())
            {
                AddFailedPatchOp(
                    Plan,
                    AddPlan.Index,
                    TEXT("add"),
                    TEXT("resolution.palette_not_spawnable"),
                    FString::Printf(TEXT("Palette entry %s is not a Blueprint node creation action."), *AddPlan.PaletteId));
                return false;
            }

            FBlueprintActionMenuItem* MenuItem = static_cast<FBlueprintActionMenuItem*>(AddPlan.Action.Get());
            UEdGraphNode* NewNode = MenuItem->PerformAction(ResolvedGraph.Graph, nullptr, Location, false);
            if (NewNode == nullptr)
            {
                AddFailedPatchOp(
                    Plan,
                    AddPlan.Index,
                    TEXT("add"),
                    TEXT("mutation.spawn_failed"),
                    FString::Printf(TEXT("Palette entry %s did not create a node."), *AddPlan.PaletteId));
                return false;
            }

            for (const TPair<FString, FString>& Default : AddPlan.Defaults)
            {
                UEdGraphPin* Pin = FindEditableInputPin(NewNode, Default.Key);
                if (Pin == nullptr)
                {
                    AddFailedPatchOp(
                        Plan,
                        AddPlan.Index,
                        TEXT("add"),
                        TEXT("resolution.pin_not_found"),
                        FString::Printf(TEXT("New node does not expose editable input pin %s."), *Default.Key));
                    return false;
                }

                if (const UEdGraphSchema* Schema = Pin->GetSchema())
                {
                    Schema->TrySetDefaultValue(*Pin, Default.Value, true);
                }
                else
                {
                    Pin->DefaultValue = Default.Value;
                }
            }

            AddPlan.NodeId = NodeId(NewNode);
            NodesByAlias.Add(AddPlan.BindingName, NewNode);
            SetGraphResolvedNode(ResolvedRefs, AddPlan.BindingName, AddPlan.NodeId);
            Location.X += 320.0f;

            TSharedPtr<FJsonObject> Applied = MakeGraphPatchOpResult(AddPlan.Index, TEXT("add"), true, true);
            Applied->SetStringField(TEXT("binding"), AddPlan.BindingName);
            Applied->SetStringField(TEXT("palette"), AddPlan.PaletteId);
            Applied->SetStringField(TEXT("nodeId"), AddPlan.NodeId);
            AppliedOpResults.Add(MakeShared<FJsonValueObject>(Applied));
            continue;
        }

        if (Command.Kind == TEXT("connect"))
        {
            if (!Plan.Connects.IsValidIndex(Command.PlanIndex))
            {
                AddFailedPatchOp(Plan, INDEX_NONE, TEXT("connect"), TEXT("internal.invalid_plan"), TEXT("Graph connect plan index is invalid."));
                return false;
            }

            const FLglGraphConnectPlan& ConnectPlan = Plan.Connects[Command.PlanIndex];
            UEdGraphPin* FromPin = FindPatchPin(NodesByAlias, ConnectPlan.Edge.From.Node, ConnectPlan.Edge.From.Pin);
            UEdGraphPin* ToPin = FindPatchPin(NodesByAlias, ConnectPlan.Edge.To.Node, ConnectPlan.Edge.To.Pin);
            if (FromPin == nullptr || ToPin == nullptr)
            {
                AddFailedPatchOp(
                    Plan,
                    ConnectPlan.Index,
                    TEXT("connect"),
                    TEXT("resolution.pin_not_found"),
                    TEXT("Graph connect pin was not found during apply."));
                return false;
            }

            const UEdGraphSchema* Schema = FromPin->GetSchema();
            if (Schema == nullptr)
            {
                AddFailedPatchOp(
                    Plan,
                    ConnectPlan.Index,
                    TEXT("connect"),
                    TEXT("resolution.graph_schema_missing"),
                    TEXT("Graph connect could not resolve a schema for the source pin."));
                return false;
            }

            const bool bAlreadyConnected = PinHasLinkTo(FromPin, ToPin);
            FString ErrorCode;
            FString ErrorMessage;
            if (!bAlreadyConnected && !ValidateConnectResponse(Schema->CanCreateConnection(FromPin, ToPin), ErrorCode, ErrorMessage))
            {
                AddFailedPatchOp(Plan, ConnectPlan.Index, TEXT("connect"), ErrorCode, ErrorMessage);
                return false;
            }

            bool bChanged = false;
            if (!bAlreadyConnected)
            {
                if (ResolvedGraph.Blueprint != nullptr)
                {
                    ResolvedGraph.Blueprint->Modify();
                }
                ResolvedGraph.Graph->Modify();
                FromPin->GetOwningNode()->Modify();
                ToPin->GetOwningNode()->Modify();

                if (!Schema->TryCreateConnection(FromPin, ToPin))
                {
                    AddFailedPatchOp(
                        Plan,
                        ConnectPlan.Index,
                        TEXT("connect"),
                        TEXT("mutation.connect_failed"),
                        TEXT("UE graph schema rejected the connection during apply."));
                    return false;
                }

                bChanged = true;
                ResolvedGraph.Graph->NotifyGraphChanged();
                if (ResolvedGraph.Blueprint != nullptr)
                {
                    FBlueprintEditorUtils::MarkBlueprintAsModified(ResolvedGraph.Blueprint);
                    ResolvedGraph.Blueprint->MarkPackageDirty();
                }
            }

            TSharedPtr<FJsonObject> Applied = MakeGraphPatchOpResult(ConnectPlan.Index, TEXT("connect"), true, bChanged);
            Applied->SetObjectField(TEXT("edge"), MakeGraphEdgeObject(ConnectPlan.Edge));
            AppliedOpResults.Add(MakeShared<FJsonValueObject>(Applied));
            continue;
        }
    }

    Plan.OpResults = AppliedOpResults;
    return Plan.Diagnostics.IsEmpty();
}
}

FLglObjectResult FLglBlueprintGraphPatchService::Patch(
    const FLglObjectRequest& Request,
    const FLglBlueprintResolvedGraph& ResolvedGraph) const
{
    bool bDryRun = false;
    Request.Object->TryGetBoolField(TEXT("dryRun"), bDryRun);

    const TArray<TSharedPtr<FJsonValue>> Ops = ReadGraphPatchOps(Request);
    const TSharedPtr<FJsonObject> ResolvedRefs = BuildGraphResolvedRefs(ResolvedGraph.AssetPath, ResolvedGraph.GraphName, ResolvedGraph.GraphId);
    FLglGraphPatchPlanData PatchPlan = BuildGraphPatchPlanData(Request, ResolvedGraph, Ops);
    bool bApplied = false;
    if (PatchPlan.bValid && !bDryRun)
    {
        bApplied = ApplyGraphPatchPlan(PatchPlan, ResolvedGraph, ResolvedRefs);
    }

    TSharedPtr<FJsonObject> Object = MakeShared<FJsonObject>();
    Object->SetStringField(TEXT("kind"), TEXT("mutation_result"));
    Object->SetArrayField(TEXT("ops"), Ops);
    Object->SetArrayField(TEXT("opResults"), PatchPlan.OpResults);
    Object->SetObjectField(
        TEXT("planned"),
        LoomleMutation::BuildBatchPlanFromOpResults(
            TEXT("lgl.graph.patch"),
            ResolvedGraph.AssetPath,
            TEXT("patch"),
            Ops.Num(),
            PatchPlan.OpResults,
            ResolvedRefs));
    Object->SetObjectField(TEXT("resolvedRefs"), ResolvedRefs);

    LoomleMutation::SetDiagnostics(Object, PatchPlan.Diagnostics);
    LoomleMutation::SetMutationEnvelope(
        Object,
        TEXT("lgl.graph.patch"),
        ResolvedGraph.AssetPath,
        TEXT("patch"),
        bDryRun,
        bApplied,
        PatchPlan.bValid && PatchPlan.Diagnostics.IsEmpty());

    FLglObjectResult Result;
    Result.Object = Object;
    return Result;
}
}
