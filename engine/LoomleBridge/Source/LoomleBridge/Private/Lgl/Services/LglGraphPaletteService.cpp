// Copyright 2026 Loomle contributors.

#include "LglGraphPaletteService.h"

#include "BlueprintActionFilter.h"
#include "BlueprintActionMenuBuilder.h"
#include "BlueprintActionMenuUtils.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "GraphEditorActions.h"
#include "LglGraphResolver.h"
#include "Misc/SecureHash.h"
#include "../LglDiagnostics.h"
#include "../LglObjectModel.h"
#include "../LglResult.h"

namespace Loomle::Lgl
{
namespace
{
constexpr int32 DefaultLimit = 50;
constexpr int32 MaxLimit = 200;

FString TextToString(const FText& Text)
{
    return Text.IsEmpty() ? FString() : Text.ToString();
}

FString SanitizeEntryName(const FString& Text)
{
    FString Name;
    Name.Reserve(Text.Len());
    for (const TCHAR Character : Text)
    {
        if (FChar::IsAlnum(Character) || Character == TEXT('_'))
        {
            Name.AppendChar(Character);
        }
        else
        {
            Name.AppendChar(TEXT('_'));
        }
    }
    if (Name.IsEmpty())
    {
        Name = TEXT("Entry");
    }
    if (FChar::IsDigit(Name[0]))
    {
        Name.InsertAt(0, TEXT("_"));
    }
    return Name;
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
        Alias.InsertAt(0, TEXT("_"));
    }
    return Alias;
}

FString MakeUniqueAlias(const FString& Base, TSet<FString>& UsedAliases)
{
    const FString SanitizedBase = SanitizeAlias(Base);
    FString Alias = SanitizedBase;
    int32 Suffix = 2;
    while (UsedAliases.Contains(Alias))
    {
        Alias = FString::Printf(TEXT("%s_%d"), *SanitizedBase, Suffix++);
    }
    UsedAliases.Add(Alias);
    return Alias;
}

FString NodeType(const UEdGraphNode* Node)
{
    return Node != nullptr && Node->GetClass() != nullptr ? Node->GetClass()->GetName() : TEXT("Node");
}

FString NodeTitle(const UEdGraphNode* Node)
{
    return Node != nullptr ? Node->GetNodeTitle(ENodeTitleType::ListView).ToString() : FString();
}

FString MakeUniqueName(const FString& Label, TSet<FString>& UsedNames)
{
    const FString Base = SanitizeEntryName(Label);
    FString Name = Base;
    int32 Suffix = 2;
    while (UsedNames.Contains(Name))
    {
        Name = FString::Printf(TEXT("%s_%d"), *Base, Suffix++);
    }
    UsedNames.Add(Name);
    return Name;
}

FString ReadFindText(const FLglObjectRequest& Request)
{
    const TSharedPtr<FJsonObject>* Find = nullptr;
    if (!Request.Object.IsValid()
        || !Request.Object->TryGetObjectField(TEXT("find"), Find)
        || Find == nullptr
        || !(*Find).IsValid())
    {
        return FString();
    }

    FString Text;
    (*Find)->TryGetStringField(TEXT("text"), Text);
    return Text;
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

bool HasField(const FLglObjectRequest& Request, const FString& Field)
{
    return Request.Object.IsValid() && Request.Object->HasField(Field);
}

bool HasDetails(const FLglObjectRequest& Request)
{
    const TArray<TSharedPtr<FJsonValue>>* Details = nullptr;
    return Request.Object.IsValid()
        && Request.Object->TryGetArrayField(TEXT("with"), Details)
        && Details != nullptr
        && Details->Num() > 0;
}

FLglObjectResult UnsupportedPaletteFeature(const FString& Feature, const FString& Suggestion)
{
    return FLglResult::FromDiagnostic(
        FLglDiagnostics::Error(
            TEXT("capability.not_implemented"),
            FString::Printf(TEXT("Graph palette entry discovery does not support %s yet."), *Feature))
            .Domain(TEXT("graph"))
            .Operation(TEXT("find palette entry"))
            .Actual(Feature)
            .Suggestion(Suggestion)
            .Build());
}

bool ActionMatchesText(const TSharedPtr<FEdGraphSchemaAction>& Action, const FString& Text)
{
    if (!Action.IsValid() || Text.IsEmpty())
    {
        return true;
    }

    const FString Query = Text.ToLower();
    TArray<FString> Haystack;
    Haystack.Add(TextToString(Action->GetMenuDescription()));
    Haystack.Add(TextToString(Action->GetCategory()));
    Haystack.Add(TextToString(Action->GetTooltipDescription()));
    Haystack.Add(TextToString(Action->GetKeywords()));
    Haystack.Append(Action->GetSearchTitleArray());
    Haystack.Append(Action->GetSearchCategoryArray());
    Haystack.Append(Action->GetSearchKeywordsArray());

    for (const FString& Value : Haystack)
    {
        if (Value.ToLower().Contains(Query))
        {
            return true;
        }
    }
    return false;
}

int32 ActionScore(const TSharedPtr<FEdGraphSchemaAction>& Action, const FString& Text)
{
    if (!Action.IsValid() || Text.IsEmpty())
    {
        return 0;
    }

    const FString Query = Text.ToLower();
    const FString Label = TextToString(Action->GetMenuDescription()).ToLower();
    if (Label == Query)
    {
        return 0;
    }
    if (Label.StartsWith(Query))
    {
        return 10;
    }
    if (Label.Contains(Query))
    {
        return 20;
    }
    return 30;
}

FString PaletteActionId(const TSharedPtr<FEdGraphSchemaAction>& Action, int32 Index)
{
    const FString StableText = Action.IsValid()
        ? FString::Printf(
            TEXT("%s|%s|%s|%d"),
            *TextToString(Action->GetCategory()),
            *TextToString(Action->GetMenuDescription()),
            *TextToString(Action->GetTooltipDescription()),
            Action->GetGrouping())
        : FString::Printf(TEXT("invalid:%d"), Index);
    return FString::Printf(TEXT("palette:%s"), *FMD5::HashAnsiString(*StableText));
}

void BuildNodesByAlias(const FLglResolvedGraph& ResolvedGraph, TMap<FString, UEdGraphNode*>& OutNodesByAlias)
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

bool ReadPinContext(const FLglObjectRequest& Request, FString& OutDirection, FString& OutNodeAlias, FString& OutPinName)
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

    const TSharedPtr<FJsonObject>* PinContext = nullptr;
    if (!(*Find)->TryGetObjectField(TEXT("pinContext"), PinContext)
        || PinContext == nullptr
        || !(*PinContext).IsValid())
    {
        return false;
    }

    (*PinContext)->TryGetStringField(TEXT("direction"), OutDirection);
    const TSharedPtr<FJsonObject>* Pin = nullptr;
    if (!(*PinContext)->TryGetObjectField(TEXT("pin"), Pin) || Pin == nullptr || !(*Pin).IsValid())
    {
        return false;
    }

    (*Pin)->TryGetStringField(TEXT("node"), OutNodeAlias);
    (*Pin)->TryGetStringField(TEXT("pin"), OutPinName);
    return !OutDirection.IsEmpty() && !OutNodeAlias.IsEmpty() && !OutPinName.IsEmpty();
}

bool ResolvePinContext(
    const FLglObjectRequest& Request,
    const FLglResolvedGraph& ResolvedGraph,
    TArray<UEdGraphPin*>& OutPins,
    FLglObjectResult& OutError)
{
    OutPins.Reset();

    FString Direction;
    FString NodeAlias;
    FString PinName;
    if (!ReadPinContext(Request, Direction, NodeAlias, PinName))
    {
        return true;
    }

    TMap<FString, UEdGraphNode*> NodesByAlias;
    BuildNodesByAlias(ResolvedGraph, NodesByAlias);

    UEdGraphNode* const* Node = NodesByAlias.Find(NodeAlias);
    if (Node == nullptr || *Node == nullptr)
    {
        OutError = FLglResult::FromDiagnostic(
            FLglDiagnostics::Error(
                TEXT("resolution.node_not_found"),
                FString::Printf(TEXT("Graph palette pin context node %s was not found."), *NodeAlias))
                .Domain(TEXT("graph"))
                .Operation(TEXT("find palette entry"))
                .Path({TEXT("find"), TEXT("pinContext"), TEXT("pin"), TEXT("node")})
                .Actual(NodeAlias)
                .Suggestion(TEXT("Use a node alias returned by a graph query."))
                .Build());
        return false;
    }

    UEdGraphPin* Pin = (*Node)->FindPin(*PinName);
    if (Pin == nullptr)
    {
        OutError = FLglResult::FromDiagnostic(
            FLglDiagnostics::Error(
                TEXT("resolution.pin_not_found"),
                FString::Printf(TEXT("Graph palette pin context pin %s.%s was not found."), *NodeAlias, *PinName))
                .Domain(TEXT("graph"))
                .Operation(TEXT("find palette entry"))
                .Path({TEXT("find"), TEXT("pinContext"), TEXT("pin"), TEXT("pin")})
                .Actual(FString::Printf(TEXT("%s.%s"), *NodeAlias, *PinName))
                .Suggestion(TEXT("Query the graph with pins and use an existing pin name."))
                .Build());
        return false;
    }

    OutPins.Add(Pin);
    return true;
}

TSharedPtr<FJsonObject> EncodePaletteEntry(
    const TSharedPtr<FEdGraphSchemaAction>& Action,
    int32 Index,
    TSet<FString>& UsedNames)
{
    const FString Label = Action.IsValid() ? TextToString(Action->GetMenuDescription()) : FString();
    const FString Category = Action.IsValid() ? TextToString(Action->GetCategory()) : FString();

    TSharedPtr<FJsonObject> PaletteRef = MakeShared<FJsonObject>();
    PaletteRef->SetStringField(TEXT("kind"), TEXT("palette"));
    PaletteRef->SetStringField(TEXT("id"), PaletteActionId(Action, Index));

    TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
    Entry->SetStringField(TEXT("name"), MakeUniqueName(Label, UsedNames));
    Entry->SetObjectField(TEXT("palette"), PaletteRef);
    if (!Label.IsEmpty())
    {
        Entry->SetStringField(TEXT("label"), Label);
    }
    if (!Category.IsEmpty())
    {
        Entry->SetStringField(TEXT("category"), Category);
    }
    return Entry;
}

void BuildActionMenu(
    const FLglResolvedGraph& ResolvedGraph,
    const TArray<UEdGraphPin*>& ContextPins,
    FBlueprintActionMenuBuilder& Builder)
{
    FBlueprintActionContext Context;
    Context.Blueprints.Add(ResolvedGraph.Blueprint);
    Context.Graphs.Add(ResolvedGraph.Graph);
    Context.Pins.Append(ContextPins);

    constexpr bool bContextSensitive = true;
    const uint32 TargetMask =
        EContextTargetFlags::TARGET_Blueprint |
        EContextTargetFlags::TARGET_SubComponents |
        EContextTargetFlags::TARGET_NodeTarget |
        EContextTargetFlags::TARGET_PinObject |
        EContextTargetFlags::TARGET_SiblingPinObjects |
        EContextTargetFlags::TARGET_BlueprintLibraries;

    FBlueprintActionMenuUtils::MakeContextMenu(Context, bContextSensitive, TargetMask, Builder);
    if (ResolvedGraph.Graph != nullptr)
    {
        if (const UEdGraphSchema* Schema = ResolvedGraph.Graph->GetSchema())
        {
            Schema->InsertAdditionalActions(Context.Blueprints, Context.Graphs, Context.Pins, Builder);
        }
    }
}
}

FLglObjectResult FLglGraphPaletteService::QueryPaletteEntries(
    const FLglObjectRequest& Request,
    const FLglResolvedGraph& ResolvedGraph) const
{
    if (HasField(Request, TEXT("where")))
    {
        return UnsupportedPaletteFeature(
            TEXT("where filters"),
            TEXT("Use the primary find text for this first palette query implementation."));
    }
    if (HasField(Request, TEXT("orderBy")))
    {
        return UnsupportedPaletteFeature(
            TEXT("orderBy"),
            TEXT("Palette entries are currently sorted by text match score and UE action order."));
    }
    if (HasDetails(Request))
    {
        return UnsupportedPaletteFeature(
            TEXT("with details"),
            TEXT("Query palette entries without with pins/defaults until template-node detail expansion is implemented."));
    }

    const FString Text = ReadFindText(Request);
    const int32 Limit = ReadPageLimit(Request);

    TArray<UEdGraphPin*> ContextPins;
    FLglObjectResult Error;
    if (!ResolvePinContext(Request, ResolvedGraph, ContextPins, Error))
    {
        return Error;
    }

    FBlueprintActionMenuBuilder Builder;
    BuildActionMenu(ResolvedGraph, ContextPins, Builder);

    struct FMatch
    {
        TSharedPtr<FEdGraphSchemaAction> Action;
        int32 Index = 0;
        int32 Score = 0;
    };

    TArray<FMatch> Matches;
    for (int32 Index = 0; Index < Builder.GetNumActions(); ++Index)
    {
        TSharedPtr<FEdGraphSchemaAction> Action = Builder.GetSchemaAction(Index);
        if (ActionMatchesText(Action, Text))
        {
            Matches.Add({Action, Index, ActionScore(Action, Text)});
        }
    }

    Matches.Sort([](const FMatch& Left, const FMatch& Right)
    {
        return Left.Score == Right.Score ? Left.Index < Right.Index : Left.Score < Right.Score;
    });

    TSet<FString> UsedNames;
    TArray<TSharedPtr<FJsonValue>> Entries;
    for (const FMatch& Match : Matches)
    {
        if (Entries.Num() >= Limit)
        {
            break;
        }
        Entries.Add(MakeShared<FJsonValueObject>(EncodePaletteEntry(Match.Action, Match.Index, UsedNames)));
    }

    TSharedPtr<FJsonObject> Object = MakeShared<FJsonObject>();
    Object->SetStringField(TEXT("kind"), TEXT("palette_result"));
    Object->SetObjectField(TEXT("target"), Request.Object->GetObjectField(TEXT("target")));
    Object->SetArrayField(TEXT("entries"), Entries);

    FLglObjectResult Result;
    Result.Object = Object;
    return Result;
}
}
