// Copyright 2026 Loomle contributors.

#include "LglGraphAdapter.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "../../LoomleMutationResult.h"
#include "../LglCapabilityValidator.h"
#include "../LglDiagnostics.h"
#include "../LglResult.h"
#include "../Services/LglGraphPaletteService.h"
#include "../Services/LglGraphResolver.h"

namespace Loomle::Lgl
{
namespace
{
constexpr int32 DefaultLimit = 50;
constexpr int32 MaxLimit = 200;

FLglQueryCapabilities GraphQueryCapabilities()
{
    FLglQueryCapabilities Capabilities;
    Capabilities.Domain = TEXT("graph");
    Capabilities.bValidateFindKinds = true;
    Capabilities.FindKinds = {TEXT("nodes"), TEXT("path"), TEXT("palette_entry")};
    Capabilities.bValidateWhereFields = true;
    Capabilities.WhereFields = {
        TEXT("type"),
        TEXT("name"),
        TEXT("id")
    };
    Capabilities.bValidateDetails = true;
    Capabilities.Details = {TEXT("pins"), TEXT("defaults"), TEXT("layout")};
    Capabilities.bValidateOrderKeys = true;
    Capabilities.OrderKeys = {TEXT("name"), TEXT("type")};
    Capabilities.bSupportsPageAfter = false;
    Capabilities.bSupportsCompare = false;
    return Capabilities;
}

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

FString PinDirection(const UEdGraphPin* Pin)
{
    return Pin != nullptr && Pin->Direction == EGPD_Output ? TEXT("out") : TEXT("in");
}

FString PinType(const UEdGraphPin* Pin)
{
    if (Pin == nullptr)
    {
        return TEXT("unknown");
    }
    if (!Pin->PinType.PinCategory.IsNone())
    {
        return Pin->PinType.PinCategory.ToString();
    }
    return TEXT("unknown");
}

FString PinDefaultValue(const UEdGraphPin* Pin)
{
    if (Pin == nullptr)
    {
        return FString();
    }
    if (!Pin->DefaultValue.IsEmpty())
    {
        return Pin->DefaultValue;
    }
    if (Pin->DefaultObject != nullptr)
    {
        return Pin->DefaultObject->GetPathName();
    }
    if (!Pin->DefaultTextValue.IsEmpty())
    {
        return Pin->DefaultTextValue.ToString();
    }
    return FString();
}

FString ExprToString(const TSharedPtr<FJsonValue>& Value)
{
    if (!Value.IsValid() || Value->IsNull())
    {
        return FString();
    }

    FString StringValue;
    if (Value->TryGetString(StringValue))
    {
        return StringValue;
    }

    double NumberValue = 0.0;
    if (Value->TryGetNumber(NumberValue))
    {
        return FString::SanitizeFloat(NumberValue);
    }

    bool BoolValue = false;
    if (Value->TryGetBool(BoolValue))
    {
        return BoolValue ? TEXT("true") : TEXT("false");
    }

    const TSharedPtr<FJsonObject>* ObjectValue = nullptr;
    if (Value->TryGetObject(ObjectValue) && ObjectValue != nullptr && (*ObjectValue).IsValid())
    {
        FString Kind;
        if ((*ObjectValue)->TryGetStringField(TEXT("kind"), Kind))
        {
            FString Name;
            if ((Kind == TEXT("name") || Kind == TEXT("local") || Kind == TEXT("id"))
                && (*ObjectValue)->TryGetStringField(Kind == TEXT("id") ? TEXT("id") : TEXT("name"), Name))
            {
                return Name;
            }
        }
    }

    return FString();
}

FString ConditionField(const TSharedPtr<FJsonObject>& Condition)
{
    const TSharedPtr<FJsonObject>* Field = nullptr;
    if (!Condition.IsValid()
        || !Condition->TryGetObjectField(TEXT("field"), Field)
        || Field == nullptr
        || !(*Field).IsValid())
    {
        return FString();
    }

    const TArray<TSharedPtr<FJsonValue>>* Path = nullptr;
    if (!(*Field)->TryGetArrayField(TEXT("path"), Path) || Path == nullptr || Path->Num() == 0)
    {
        return FString();
    }

    FString FieldName;
    (*Path)[0]->TryGetString(FieldName);
    return FieldName;
}

FString ReadNodeField(const UEdGraphNode* Node, const FString& Alias, const FString& Field)
{
    if (Field == TEXT("name") || Field == TEXT("alias"))
    {
        return Alias;
    }
    if (Field == TEXT("id"))
    {
        return NodeId(Node);
    }
    if (Field == TEXT("type"))
    {
        return NodeType(Node);
    }
    return FString();
}

bool MatchesCondition(const UEdGraphNode* Node, const FString& Alias, const TSharedPtr<FJsonObject>& Condition)
{
    if (!Condition.IsValid())
    {
        return true;
    }

    FString Kind;
    if (!Condition->TryGetStringField(TEXT("kind"), Kind))
    {
        return true;
    }

    if (Kind == TEXT("not"))
    {
        const TSharedPtr<FJsonObject>* Inner = nullptr;
        return Condition->TryGetObjectField(TEXT("condition"), Inner) && Inner != nullptr
            ? !MatchesCondition(Node, Alias, *Inner)
            : true;
    }

    if (Kind == TEXT("and") || Kind == TEXT("or"))
    {
        const TArray<TSharedPtr<FJsonValue>>* Conditions = nullptr;
        if (!Condition->TryGetArrayField(TEXT("conditions"), Conditions) || Conditions == nullptr)
        {
            return true;
        }

        const bool bAnd = Kind == TEXT("and");
        for (const TSharedPtr<FJsonValue>& Item : *Conditions)
        {
            const TSharedPtr<FJsonObject>* ItemObject = nullptr;
            const bool bMatches = Item.IsValid()
                && Item->TryGetObject(ItemObject)
                && ItemObject != nullptr
                && MatchesCondition(Node, Alias, *ItemObject);
            if (bAnd && !bMatches)
            {
                return false;
            }
            if (!bAnd && bMatches)
            {
                return true;
            }
        }
        return bAnd;
    }

    const FString Left = ReadNodeField(Node, Alias, ConditionField(Condition));
    const TSharedPtr<FJsonValue> Value = Condition->TryGetField(TEXT("value"));
    const FString Right = ExprToString(Value);
    if (Kind == TEXT("eq"))
    {
        return Left == Right;
    }
    if (Kind == TEXT("ne"))
    {
        return Left != Right;
    }
    if (Kind == TEXT("contains"))
    {
        return Left.Contains(Right, ESearchCase::IgnoreCase);
    }
    return true;
}

bool MatchesText(const UEdGraphNode* Node, const FString& Alias, const FString& Text)
{
    if (Text.IsEmpty())
    {
        return true;
    }

    return Alias.Contains(Text, ESearchCase::IgnoreCase)
        || NodeId(Node).Contains(Text, ESearchCase::IgnoreCase)
        || NodeType(Node).Contains(Text, ESearchCase::IgnoreCase)
        || NodeTitle(Node).Contains(Text, ESearchCase::IgnoreCase);
}

TSharedPtr<FJsonObject> MakePinRef(const FString& NodeAlias, const FString& PinName)
{
    TSharedPtr<FJsonObject> Ref = MakeShared<FJsonObject>();
    Ref->SetStringField(TEXT("node"), NodeAlias);
    Ref->SetStringField(TEXT("pin"), PinName);
    return Ref;
}

TSharedPtr<FJsonObject> EncodeNode(
    const UEdGraphNode* Node,
    const FString& Alias,
    bool bIncludeDefaults,
    bool bIncludeLayout)
{
    TSharedPtr<FJsonObject> Object = MakeShared<FJsonObject>();
    Object->SetStringField(TEXT("alias"), Alias);
    Object->SetStringField(TEXT("id"), NodeId(Node));
    Object->SetStringField(TEXT("type"), NodeType(Node));
    TSharedPtr<FJsonObject> Fields = MakeShared<FJsonObject>();
    if (bIncludeDefaults && Node != nullptr)
    {
        for (const UEdGraphPin* Pin : Node->Pins)
        {
            const FString DefaultValue = PinDefaultValue(Pin);
            if (Pin != nullptr && !DefaultValue.IsEmpty())
            {
                Fields->SetStringField(Pin->PinName.ToString(), DefaultValue);
            }
        }
    }
    Object->SetObjectField(TEXT("fields"), Fields);

    if (bIncludeLayout)
    {
        TArray<TSharedPtr<FJsonValue>> At;
        At.Add(MakeShared<FJsonValueNumber>(Node != nullptr ? Node->NodePosX : 0));
        At.Add(MakeShared<FJsonValueNumber>(Node != nullptr ? Node->NodePosY : 0));
        Object->SetArrayField(TEXT("at"), At);

        if (Node != nullptr && Node->NodeWidth > 0 && Node->NodeHeight > 0)
        {
            TArray<TSharedPtr<FJsonValue>> Size;
            Size.Add(MakeShared<FJsonValueNumber>(Node->NodeWidth));
            Size.Add(MakeShared<FJsonValueNumber>(Node->NodeHeight));
            Object->SetArrayField(TEXT("size"), Size);
        }
    }
    return Object;
}

TSharedPtr<FJsonObject> EncodePin(const UEdGraphPin* Pin, const FString& NodeAlias, bool bIncludeDefaults)
{
    TSharedPtr<FJsonObject> Object = MakeShared<FJsonObject>();
    Object->SetStringField(TEXT("node"), NodeAlias);
    Object->SetStringField(TEXT("name"), Pin != nullptr ? Pin->PinName.ToString() : FString());
    Object->SetStringField(TEXT("type"), PinType(Pin));
    Object->SetStringField(TEXT("direction"), PinDirection(Pin));
    const FString DefaultValue = bIncludeDefaults ? PinDefaultValue(Pin) : FString();
    if (!DefaultValue.IsEmpty())
    {
        Object->SetStringField(TEXT("value"), DefaultValue);
    }
    return Object;
}

TSharedPtr<FJsonObject> EncodeEdge(const FString& FromNode, const UEdGraphPin* FromPin, const FString& ToNode, const UEdGraphPin* ToPin)
{
    TSharedPtr<FJsonObject> Object = MakeShared<FJsonObject>();
    Object->SetObjectField(TEXT("from"), MakePinRef(FromNode, FromPin != nullptr ? FromPin->PinName.ToString() : FString()));
    Object->SetObjectField(TEXT("to"), MakePinRef(ToNode, ToPin != nullptr ? ToPin->PinName.ToString() : FString()));
    return Object;
}

bool QueryIncludes(const FLglObjectRequest& Request, const FString& Detail)
{
    const TArray<TSharedPtr<FJsonValue>>* Details = nullptr;
    if (!Request.Object.IsValid() || !Request.Object->TryGetArrayField(TEXT("with"), Details) || Details == nullptr)
    {
        return false;
    }
    for (const TSharedPtr<FJsonValue>& Value : *Details)
    {
        FString Text;
        if (Value.IsValid() && Value->TryGetString(Text) && Text == Detail)
        {
            return true;
        }
    }
    return false;
}

void ReadFindNodesText(const FLglObjectRequest& Request, FString& OutText)
{
    OutText.Reset();
    const TSharedPtr<FJsonObject>* Find = nullptr;
    if (Request.Object.IsValid()
        && Request.Object->TryGetObjectField(TEXT("find"), Find)
        && Find != nullptr
        && (*Find).IsValid())
    {
        FString Kind;
        (*Find)->TryGetStringField(TEXT("kind"), Kind);
        if (Kind == TEXT("nodes"))
        {
            (*Find)->TryGetStringField(TEXT("text"), OutText);
        }
    }
}

bool ReadFindPath(const FLglObjectRequest& Request, FString& OutDirection, FString& OutNodeAlias, FString& OutPinName)
{
    OutDirection.Reset();
    OutNodeAlias.Reset();
    OutPinName.Reset();

    const TSharedPtr<FJsonObject>* Find = nullptr;
    if (!Request.Object.IsValid()
        || !Request.Object->TryGetObjectField(TEXT("find"), Find)
        || Find == nullptr
        || !(*Find).IsValid())
    {
        return false;
    }

    FString Kind;
    (*Find)->TryGetStringField(TEXT("kind"), Kind);
    if (Kind != TEXT("path"))
    {
        return false;
    }

    (*Find)->TryGetStringField(TEXT("direction"), OutDirection);
    const TSharedPtr<FJsonObject>* Pin = nullptr;
    if (!(*Find)->TryGetObjectField(TEXT("pin"), Pin) || Pin == nullptr || !(*Pin).IsValid())
    {
        return false;
    }

    (*Pin)->TryGetStringField(TEXT("node"), OutNodeAlias);
    (*Pin)->TryGetStringField(TEXT("pin"), OutPinName);
    return !OutDirection.IsEmpty() && !OutNodeAlias.IsEmpty() && !OutPinName.IsEmpty();
}

FString ReadFindKind(const FLglObjectRequest& Request)
{
    const TSharedPtr<FJsonObject>* Find = nullptr;
    if (!Request.Object.IsValid()
        || !Request.Object->TryGetObjectField(TEXT("find"), Find)
        || Find == nullptr
        || !(*Find).IsValid())
    {
        return FString();
    }

    FString Kind;
    (*Find)->TryGetStringField(TEXT("kind"), Kind);
    return Kind;
}

int32 ReadPageLimit(const FLglObjectRequest& Request)
{
    const TSharedPtr<FJsonObject>* Page = nullptr;
    if (!Request.Object.IsValid()
        || !Request.Object->TryGetObjectField(TEXT("page"), Page)
        || Page == nullptr
        || !(*Page).IsValid())
    {
        return DefaultLimit;
    }

    double Limit = 0.0;
    if ((*Page)->TryGetNumberField(TEXT("limit"), Limit))
    {
        return FMath::Clamp(static_cast<int32>(Limit), 1, MaxLimit);
    }
    return DefaultLimit;
}

void ReadOrderBy(const FLglObjectRequest& Request, TArray<TPair<FString, FString>>& OutOrderBy)
{
    OutOrderBy.Reset();
    const TArray<TSharedPtr<FJsonValue>>* Orders = nullptr;
    if (!Request.Object.IsValid() || !Request.Object->TryGetArrayField(TEXT("orderBy"), Orders) || Orders == nullptr)
    {
        return;
    }

    for (const TSharedPtr<FJsonValue>& Value : *Orders)
    {
        const TSharedPtr<FJsonObject>* Order = nullptr;
        if (!Value.IsValid() || !Value->TryGetObject(Order) || Order == nullptr || !(*Order).IsValid())
        {
            continue;
        }

        FString Key;
        FString Direction;
        (*Order)->TryGetStringField(TEXT("key"), Key);
        (*Order)->TryGetStringField(TEXT("direction"), Direction);
        if (!Key.IsEmpty())
        {
            OutOrderBy.Add(TPair<FString, FString>(Key, Direction));
        }
    }
}

void SortNodes(TArray<UEdGraphNode*>& Nodes, const TMap<const UEdGraphNode*, FString>& AliasesByNode, const TArray<TPair<FString, FString>>& OrderBy)
{
    if (OrderBy.IsEmpty())
    {
        return;
    }

    Nodes.Sort([&AliasesByNode, &OrderBy](const UEdGraphNode& Left, const UEdGraphNode& Right)
    {
        for (const TPair<FString, FString>& Order : OrderBy)
        {
            const FString LeftValue = ReadNodeField(&Left, AliasesByNode.FindRef(&Left), Order.Key);
            const FString RightValue = ReadNodeField(&Right, AliasesByNode.FindRef(&Right), Order.Key);
            const int32 Compare = LeftValue.Compare(RightValue, ESearchCase::IgnoreCase);
            if (Compare == 0)
            {
                continue;
            }
            return Order.Value == TEXT("desc") ? Compare > 0 : Compare < 0;
        }
        return NodeId(&Left) < NodeId(&Right);
    });
}

UEdGraphPin* FindPinByAlias(
    const TMap<FString, UEdGraphNode*>& NodesByAlias,
    const FString& NodeAlias,
    const FString& PinName)
{
    UEdGraphNode* const* Node = NodesByAlias.Find(NodeAlias);
    if (Node == nullptr || *Node == nullptr)
    {
        return nullptr;
    }
    return (*Node)->FindPin(*PinName);
}

void WalkPathFrom(UEdGraphPin* StartPin, TArray<UEdGraphNode*>& OutNodes)
{
    OutNodes.Reset();
    UEdGraphPin* CurrentPin = StartPin;
    TSet<UEdGraphNode*> SeenNodes;

    while (CurrentPin != nullptr)
    {
        UEdGraphNode* CurrentNode = CurrentPin->GetOwningNode();
        if (CurrentNode == nullptr || SeenNodes.Contains(CurrentNode))
        {
            break;
        }

        OutNodes.Add(CurrentNode);
        SeenNodes.Add(CurrentNode);

        if (CurrentPin->Direction != EGPD_Output || CurrentPin->LinkedTo.IsEmpty())
        {
            break;
        }
        CurrentPin = CurrentPin->LinkedTo[0];
    }
}

void WalkPathTo(UEdGraphPin* StartPin, TArray<UEdGraphNode*>& OutNodes)
{
    OutNodes.Reset();
    UEdGraphPin* CurrentPin = StartPin;
    TSet<UEdGraphNode*> SeenNodes;

    while (CurrentPin != nullptr)
    {
        UEdGraphNode* CurrentNode = CurrentPin->GetOwningNode();
        if (CurrentNode == nullptr || SeenNodes.Contains(CurrentNode))
        {
            break;
        }

        OutNodes.Insert(CurrentNode, 0);
        SeenNodes.Add(CurrentNode);

        if (CurrentPin->Direction != EGPD_Input || CurrentPin->LinkedTo.IsEmpty())
        {
            break;
        }
        CurrentPin = CurrentPin->LinkedTo[0];
    }
}

TSharedPtr<FJsonObject> BuildGraphReadback(
    const FLglObjectRequest& Request,
    const FLglResolvedGraph& ResolvedGraph)
{
    const bool bIncludePins = QueryIncludes(Request, TEXT("pins"));
    const bool bIncludeDefaults = QueryIncludes(Request, TEXT("defaults"));
    const bool bIncludeLayout = QueryIncludes(Request, TEXT("layout"));
    const int32 Limit = ReadPageLimit(Request);
    TArray<TPair<FString, FString>> OrderBy;
    ReadOrderBy(Request, OrderBy);
    FString FindText;
    ReadFindNodesText(Request, FindText);

    TSharedPtr<FJsonObject> Where;
    const TSharedPtr<FJsonObject>* WherePtr = nullptr;
    if (Request.Object->TryGetObjectField(TEXT("where"), WherePtr) && WherePtr != nullptr)
    {
        Where = *WherePtr;
    }
    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetStringField(TEXT("kind"), TEXT("graph"));
    Result->SetObjectField(TEXT("target"), Request.Object->GetObjectField(TEXT("target")));

    TMap<const UEdGraphNode*, FString> AliasesByNode;
    TMap<FString, UEdGraphNode*> NodesByAlias;
    TArray<UEdGraphNode*> IncludedNodes;
    TSet<FString> UsedAliases;

    for (UEdGraphNode* Node : ResolvedGraph.Graph->Nodes)
    {
        if (Node == nullptr)
        {
            continue;
        }

        const FString AliasBase = !NodeTitle(Node).IsEmpty() ? NodeTitle(Node) : NodeType(Node);
        const FString Alias = MakeUniqueAlias(AliasBase, UsedAliases);
        AliasesByNode.Add(Node, Alias);
        NodesByAlias.Add(Alias, Node);

        if (MatchesText(Node, Alias, FindText) && MatchesCondition(Node, Alias, Where))
        {
            IncludedNodes.Add(Node);
        }
    }

    FString PathDirection;
    FString PathNodeAlias;
    FString PathPinName;
    if (ReadFindPath(Request, PathDirection, PathNodeAlias, PathPinName))
    {
        UEdGraphPin* StartPin = FindPinByAlias(NodesByAlias, PathNodeAlias, PathPinName);
        if (PathDirection == TEXT("from"))
        {
            WalkPathFrom(StartPin, IncludedNodes);
        }
        else
        {
            WalkPathTo(StartPin, IncludedNodes);
        }
    }
    else
    {
        SortNodes(IncludedNodes, AliasesByNode, OrderBy);
        if (IncludedNodes.Num() > Limit)
        {
            IncludedNodes.SetNum(Limit);
        }
    }

    TSet<UEdGraphNode*> IncludedNodeSet;
    for (UEdGraphNode* Node : IncludedNodes)
    {
        IncludedNodeSet.Add(Node);
    }
    TArray<TSharedPtr<FJsonValue>> Nodes;
    TArray<TSharedPtr<FJsonValue>> Pins;
    for (UEdGraphNode* Node : IncludedNodes)
    {
        const FString Alias = AliasesByNode.FindRef(Node);
        Nodes.Add(MakeShared<FJsonValueObject>(EncodeNode(Node, Alias, bIncludeDefaults, bIncludeLayout)));

        if (bIncludePins)
        {
            for (UEdGraphPin* Pin : Node->Pins)
            {
                Pins.Add(MakeShared<FJsonValueObject>(EncodePin(Pin, Alias, bIncludeDefaults)));
            }
        }
    }

    TArray<TSharedPtr<FJsonValue>> Edges;
    for (UEdGraphNode* Node : IncludedNodes)
    {
        const FString FromAlias = AliasesByNode.FindRef(Node);
        for (UEdGraphPin* Pin : Node->Pins)
        {
            if (Pin == nullptr || Pin->Direction != EGPD_Output)
            {
                continue;
            }
            for (UEdGraphPin* LinkedPin : Pin->LinkedTo)
            {
                UEdGraphNode* LinkedNode = LinkedPin != nullptr ? LinkedPin->GetOwningNode() : nullptr;
                if (LinkedNode != nullptr && IncludedNodeSet.Contains(LinkedNode))
                {
                    Edges.Add(MakeShared<FJsonValueObject>(
                        EncodeEdge(FromAlias, Pin, AliasesByNode.FindRef(LinkedNode), LinkedPin)));
                }
            }
        }
    }

    Result->SetArrayField(TEXT("nodes"), Nodes);
    Result->SetArrayField(TEXT("edges"), Edges);
    if (bIncludePins)
    {
        Result->SetArrayField(TEXT("pins"), Pins);
    }
    return Result;
}

TArray<TSharedPtr<FJsonValue>> ReadPatchOps(const FLglObjectRequest& Request)
{
    const TArray<TSharedPtr<FJsonValue>>* Ops = nullptr;
    if (!Request.Object.IsValid() || !Request.Object->TryGetArrayField(TEXT("ops"), Ops) || Ops == nullptr)
    {
        return {};
    }
    return *Ops;
}

FString ReadPatchOpKind(const TSharedPtr<FJsonValue>& OpValue)
{
    const TSharedPtr<FJsonObject>* OpObject = nullptr;
    if (!OpValue.IsValid() || !OpValue->TryGetObject(OpObject) || OpObject == nullptr || !(*OpObject).IsValid())
    {
        return FString();
    }

    FString Kind;
    (*OpObject)->TryGetStringField(TEXT("kind"), Kind);
    return Kind;
}

TSharedPtr<FJsonValue> MakePatchDiagnostic(
    const FString& Severity,
    const FString& Code,
    const FString& Message,
    int32 Index,
    const FString& Operation)
{
    TSharedPtr<FJsonObject> Diagnostic = FLglDiagnostics::Make(Severity, Code, Message);
    Diagnostic->SetStringField(TEXT("domain"), TEXT("graph"));
    Diagnostic->SetStringField(TEXT("operation"), Operation);
    TArray<TSharedPtr<FJsonValue>> Path;
    Path.Add(MakeShared<FJsonValueString>(TEXT("ops")));
    Path.Add(MakeShared<FJsonValueNumber>(Index));
    Diagnostic->SetArrayField(TEXT("path"), Path);
    return MakeShared<FJsonValueObject>(Diagnostic);
}

TSharedPtr<FJsonObject> BuildGraphPatchResolvedRefs(const FLglResolvedGraph& ResolvedGraph)
{
    TSharedPtr<FJsonObject> ResolvedRefs = MakeShared<FJsonObject>();

    TSharedPtr<FJsonObject> Asset = MakeShared<FJsonObject>();
    Asset->SetStringField(TEXT("path"), ResolvedGraph.AssetPath);
    ResolvedRefs->SetObjectField(TEXT("asset"), Asset);

    TSharedPtr<FJsonObject> Graph = MakeShared<FJsonObject>();
    Graph->SetStringField(TEXT("name"), ResolvedGraph.GraphName);
    if (!ResolvedGraph.GraphId.IsEmpty())
    {
        Graph->SetStringField(TEXT("id"), ResolvedGraph.GraphId);
    }
    ResolvedRefs->SetObjectField(TEXT("graph"), Graph);

    return ResolvedRefs;
}

TSharedPtr<FJsonObject> BuildGraphPatchPlan(
    const FString& AssetPath,
    const TArray<TSharedPtr<FJsonValue>>& Ops,
    const TSharedPtr<FJsonObject>& ResolvedRefs)
{
    TArray<TSharedPtr<FJsonValue>> PlannedCommands;
    PlannedCommands.Reserve(Ops.Num());
    for (int32 Index = 0; Index < Ops.Num(); ++Index)
    {
        const FString Kind = ReadPatchOpKind(Ops[Index]);
        TSharedPtr<FJsonObject> Command = MakeShared<FJsonObject>();
        Command->SetNumberField(TEXT("index"), Index);
        Command->SetStringField(TEXT("op"), Kind.IsEmpty() ? TEXT("unknown") : Kind);
        Command->SetBoolField(TEXT("valid"), false);
        Command->SetBoolField(TEXT("changed"), false);
        Command->SetStringField(TEXT("errorCode"), TEXT("capability.not_implemented"));
        PlannedCommands.Add(MakeShared<FJsonValueObject>(Command));
    }
    return LoomleMutation::BuildBatchPlan(
        TEXT("lgl.graph.patch"),
        AssetPath,
        TEXT("patch"),
        PlannedCommands,
        ResolvedRefs);
}

FLglObjectResult InvalidTarget(const FString& Message, const FString& Suggestion)
{
    return FLglResult::FromDiagnostic(
        FLglDiagnostics::Error(TEXT("language.invalid_object_shape"), Message)
            .Domain(TEXT("graph"))
            .Suggestion(Suggestion)
            .Build());
}

bool ValidateGraphTarget(const FLglObjectRequest& Request, FLglObjectResult& OutError)
{
    const TSharedPtr<FJsonObject>* Target = nullptr;
    if (!Request.Object.IsValid()
        || !Request.Object->TryGetObjectField(TEXT("target"), Target)
        || Target == nullptr
        || !(*Target).IsValid())
    {
        OutError = InvalidTarget(
            TEXT("Graph query is missing target object."),
            TEXT("Use target.domain = \"graph\" with target.asset and target.graph."));
        return false;
    }

    FString Asset;
    if (!(*Target)->TryGetStringField(TEXT("asset"), Asset) || Asset.IsEmpty())
    {
        OutError = InvalidTarget(
            TEXT("Graph query target is missing required string field asset."),
            TEXT("Use target.asset to identify the Blueprint, Material, PCG, or other graph-owning asset."));
        return false;
    }

    const TSharedPtr<FJsonObject>* Graph = nullptr;
    if (!(*Target)->TryGetObjectField(TEXT("graph"), Graph) || Graph == nullptr || !(*Graph).IsValid())
    {
        OutError = InvalidTarget(
            TEXT("Graph query target is missing required graph reference object."),
            TEXT("Use target.graph = { \"kind\": \"name\", \"name\": \"EventGraph\" } or an id reference."));
        return false;
    }

    return true;
}

}

FString FLglGraphAdapter::GetDomain() const
{
    return TEXT("graph");
}

FLglObjectResult FLglGraphAdapter::Query(const FLglObjectRequest& Request)
{
    FLglObjectResult Error;
    if (!ValidateGraphTarget(Request, Error))
    {
        return Error;
    }

    if (!FLglCapabilityValidator::ValidateQuery(Request, GraphQueryCapabilities(), Error))
    {
        return Error;
    }

    FLglResolvedGraph ResolvedGraph;
    FLglGraphResolver Resolver;
    if (!Resolver.Resolve(Request.Object->GetObjectField(TEXT("target")), ResolvedGraph, Error))
    {
        return Error;
    }

    if (ReadFindKind(Request) == TEXT("palette_entry"))
    {
        FLglGraphPaletteService PaletteService;
        return PaletteService.QueryPaletteEntries(Request, ResolvedGraph);
    }

    FLglObjectResult Result;
    Result.Object = BuildGraphReadback(Request, ResolvedGraph);
    return Result;
}

FLglObjectResult FLglGraphAdapter::Patch(const FLglObjectRequest& Request)
{
    FLglObjectResult Error;
    if (!ValidateGraphTarget(Request, Error))
    {
        return Error;
    }

    FLglResolvedGraph ResolvedGraph;
    FLglGraphResolver Resolver;
    if (!Resolver.Resolve(Request.Object->GetObjectField(TEXT("target")), ResolvedGraph, Error))
    {
        return Error;
    }

    bool bDryRun = false;
    Request.Object->TryGetBoolField(TEXT("dryRun"), bDryRun);

    const TArray<TSharedPtr<FJsonValue>> Ops = ReadPatchOps(Request);
    const TSharedPtr<FJsonObject> ResolvedRefs = BuildGraphPatchResolvedRefs(ResolvedGraph);

    TSharedPtr<FJsonObject> Object = MakeShared<FJsonObject>();
    Object->SetStringField(TEXT("kind"), TEXT("mutation_result"));
    Object->SetArrayField(TEXT("ops"), Ops);
    Object->SetObjectField(TEXT("planned"), BuildGraphPatchPlan(ResolvedGraph.AssetPath, Ops, ResolvedRefs));
    Object->SetObjectField(TEXT("resolvedRefs"), ResolvedRefs);

    TArray<TSharedPtr<FJsonValue>> Diagnostics;
    for (int32 Index = 0; Index < Ops.Num(); ++Index)
    {
        const FString Kind = ReadPatchOpKind(Ops[Index]);
        Diagnostics.Add(MakePatchDiagnostic(
            TEXT("error"),
            TEXT("capability.not_implemented"),
            FString::Printf(
                TEXT("Graph patch operation %s is not implemented in the LGL bridge yet."),
                Kind.IsEmpty() ? TEXT("unknown") : *Kind),
            Index,
            Kind.IsEmpty() ? TEXT("unknown") : Kind));
    }
    LoomleMutation::SetDiagnostics(Object, Diagnostics);
    LoomleMutation::SetMutationEnvelope(
        Object,
        TEXT("lgl.graph.patch"),
        ResolvedGraph.AssetPath,
        TEXT("patch"),
        bDryRun,
        false,
        Diagnostics.IsEmpty());

    FLglObjectResult Result;
    Result.Object = Object;
    return Result;
}
}
