// Copyright 2026 Loomle contributors.

#include "SalGraphInterface.h"

#include "BlueprintActionFilter.h"
#include "BlueprintActionMenuBuilder.h"
#include "BlueprintActionMenuItem.h"
#include "BlueprintActionMenuUtils.h"
#include "BlueprintBoundEventNodeSpawner.h"
#include "BlueprintNodeSignature.h"
#include "BlueprintNodeSpawner.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Widget.h"
#include "Curves/CurveFloat.h"
#include "Curves/CurveLinearColor.h"
#include "Curves/CurveVector.h"
#include "Curves/RichCurve.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "EdGraphUtilities.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraph/EdGraphSchema.h"
#include "EdGraphSchema_K2.h"
#include "Editor.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Engine/LevelScriptBlueprint.h"
#include "Engine/SCS_Node.h"
#include "Engine/SimpleConstructionScript.h"
#include "Engine/TimelineTemplate.h"
#include "GameFramework/Actor.h"
#include "K2Node_AddPinInterface.h"
#include "K2Node_CommutativeAssociativeBinaryOperator.h"
#include "K2Node_CustomEvent.h"
#include "K2Node_DoOnceMultiInput.h"
#include "K2Node_EditablePinBase.h"
#include "K2Node_ExecutionSequence.h"
#include "K2Node_Event.h"
#include "K2Node_FunctionEntry.h"
#include "K2Node_FunctionResult.h"
#include "K2Node_MakeArray.h"
#include "K2Node_MakeContainer.h"
#include "K2Node_MakeMap.h"
#include "K2Node_MakeSet.h"
#include "K2Node_PromotableOperator.h"
#include "K2Node_Select.h"
#include "K2Node_Switch.h"
#include "K2Node_Timeline.h"
#include "K2Node_Tunnel.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Logging/TokenizedMessage.h"
#include "LoomleMutationTransaction.h"
#include "Misc/Crc.h"
#include "ScopedTransaction.h"
#include "Sal/Blueprint/SalBlueprintSandbox.h"
#include "Sal/SalDiagnostics.h"
#include "Sal/SalModel.h"
#include "Sal/SalObjectBuilder.h"
#include "Sal/SalRuntime.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "UObject/SavePackage.h"
#include "UObject/UnrealType.h"
#include "WidgetBlueprint.h"

#define LOCTEXT_NAMESPACE "LoomleSalGraph"

namespace Loomle::Sal
{
namespace
{
FString NodeId(const UEdGraphNode* Node);
FString PinId(const UEdGraphPin* Pin);
UEdGraphNode* FindNode(const UEdGraph* Graph, const FString& Id);
UEdGraphPin* FindPin(const UEdGraph* Graph, const FString& Id, bool* OutAmbiguous = nullptr);
FString SanitizeIdentifier(const FString& Source, const FString& Fallback);
TSharedPtr<FJsonObject> GraphErrorResult(
    const FString& Code,
    const FString& Message,
    const FString& Operation,
    const FString& Ref = FString(),
    const FString& Suggestion = FString());
TSharedPtr<FEdGraphSchemaAction> ResolvePaletteAction(const FSalResolvedTarget& Target, const FString& Id);
UEdGraphNode* TemplateForAction(const TSharedPtr<FEdGraphSchemaAction>& Action, UEdGraph* Graph);
bool ImportPinType(const FString& Text, FEdGraphPinType& OutType);
bool IsExecPin(const UEdGraphPin* Pin);
struct FPatchState;
bool SetObjectField(FPatchState& State, UObject* Object, const FString& Field, const TSharedPtr<FJsonValue>& Value, bool bReset, FString& OutError);

struct FGraphObjectRef
{
    UEdGraphNode* Node = nullptr;
    UEdGraphPin* Pin = nullptr;
    UEdGraph* Graph = nullptr;
    bool bPinAmbiguous = false;

    bool IsValid() const { return Node != nullptr || Pin != nullptr || Graph != nullptr; }
};

struct FNodeDefinition
{
    FString Alias;
    FString Palette;
    TSharedPtr<FEdGraphSchemaAction> Action;
    UEdGraphNode* Template = nullptr;
    TSharedPtr<FJsonObject> ConstructorArgs;
    bool bConsumed = false;
};

struct FPatchState
{
    const FSalResolvedTarget& Target;
    bool bApply = false;
    TMap<FString, FNodeDefinition> Definitions;
    TMap<FString, UEdGraphNode*> LocalNodes;
    TMap<FString, UEdGraphPin*> LocalPins;
    TSet<UEdGraphNode*> TouchedNodes;
    TSet<UEdGraphPin*> TouchedPins;
    TSharedPtr<FJsonObject> ResolvedRefs = MakeShared<FJsonObject>();
    TArray<TSharedPtr<FJsonObject>> Diagnostics;
    int32 ChangedOps = 0;

    FPatchState(const FSalResolvedTarget& InTarget, const bool bInApply)
        : Target(InTarget), bApply(bInApply)
    {
    }
};

void AddPatchError(FPatchState& State, const FString& Code, const FString& Message, const FString& Operation, const FString& Ref = FString())
{
    FSalDiagnosticBuilder Diagnostic = FSalDiagnostics::Error(Code, Message)
        .Interface(TEXT("graph"))
        .Operation(Operation);
    if (!Ref.IsEmpty()) Diagnostic.Ref(Ref);
    State.Diagnostics.Add(Diagnostic.Build());
}

FString ExprNativeText(const TSharedPtr<FJsonValue>& Value)
{
    if (!Value.IsValid() || Value->IsNull()) return TEXT("None");
    FString String;
    double Number = 0.0;
    bool bBool = false;
    if (Value->TryGetString(String)) return String;
    if (Value->TryGetNumber(Number)) return FString::SanitizeFloat(Number);
    if (Value->TryGetBool(bBool)) return bBool ? TEXT("true") : TEXT("false");
    const TArray<TSharedPtr<FJsonValue>>* Array = nullptr;
    if (Value->TryGetArray(Array) && Array != nullptr)
    {
        TArray<FString> Items;
        for (const TSharedPtr<FJsonValue>& Item : *Array) Items.Add(ExprNativeText(Item));
        return TEXT("(") + FString::Join(Items, TEXT(",")) + TEXT(")");
    }
    const TSharedPtr<FJsonObject>* Object = nullptr;
    if (!Value->TryGetObject(Object) || Object == nullptr || !(*Object).IsValid()) return FString();
    FString Kind;
    (*Object)->TryGetStringField(TEXT("kind"), Kind);
    if (Kind == TEXT("name") || Kind == TEXT("local"))
    {
        (*Object)->TryGetStringField(TEXT("name"), String);
        return String;
    }
    if ((*Object)->TryGetStringField(TEXT("id"), String)) return String;
    TArray<FString> Keys;
    (*Object)->Values.GetKeys(Keys);
    Keys.Sort();
    TArray<FString> Fields;
    for (const FString& Key : Keys) Fields.Add(Key + TEXT("=") + ExprNativeText((*Object)->TryGetField(Key)));
    return TEXT("(") + FString::Join(Fields, TEXT(",")) + TEXT(")");
}

bool ReadArgText(const TSharedPtr<FJsonObject>* Args, const TCHAR* Field, FString& Out)
{
    Out.Reset();
    if (Args == nullptr || !(*Args).IsValid()) return false;
    const TSharedPtr<FJsonValue> Value = (*Args)->TryGetField(Field);
    if (!Value.IsValid()) return false;
    Out = ExprNativeText(Value);
    return !Out.IsEmpty();
}

bool ReadArgNumber(const TSharedPtr<FJsonObject>* Args, const TCHAR* Field, double& Out)
{
    if (Args == nullptr || !(*Args).IsValid()) return false;
    const TSharedPtr<FJsonValue> Value = (*Args)->TryGetField(Field);
    if (Value.IsValid() && Value->TryGetNumber(Out)) return true;
    const FString Text = ExprNativeText(Value);
    if (!Text.IsNumeric()) return false;
    Out = FCString::Atod(*Text);
    return true;
}

UTimelineTemplate* FindTimelineTemplate(UBlueprint* Blueprint, const UK2Node_Timeline* Node, FString* OutError = nullptr)
{
    if (OutError != nullptr) OutError->Reset();
    if (Blueprint == nullptr || Node == nullptr || Node->TimelineName.IsNone())
    {
        if (OutError != nullptr) *OutError = TEXT("Timeline Node has no resolvable authored name.");
        return nullptr;
    }
    UTimelineTemplate* Match = nullptr;
    int32 Matches = 0;
    for (UTimelineTemplate* Timeline : Blueprint->Timelines)
    {
        if (Timeline != nullptr && Timeline->GetVariableName() == Node->TimelineName)
        {
            Match = Timeline;
            ++Matches;
        }
    }
    TArray<UK2Node_Timeline*> TimelineNodes;
    FBlueprintEditorUtils::GetAllNodesOfClass<UK2Node_Timeline>(Blueprint, TimelineNodes);
    int32 NodeMatches = 0;
    for (UK2Node_Timeline* Candidate : TimelineNodes)
    {
        if (Candidate != nullptr && Candidate->TimelineName == Node->TimelineName)
        {
            ++NodeMatches;
            if (Candidate != Node) Match = nullptr;
        }
    }
    if (Matches != 1 || NodeMatches != 1 || Match == nullptr || FBlueprintEditorUtils::FindNodeForTimeline(Blueprint, Match) != Node)
    {
        if (OutError != nullptr) *OutError = TEXT("Timeline Node and UTimelineTemplate do not form one unique native pair.");
        return nullptr;
    }
    return Match;
}

void SyncTimelineNodeCache(UK2Node_Timeline* Node, const UTimelineTemplate* Timeline)
{
    if (Node == nullptr || Timeline == nullptr) return;
    Node->bAutoPlay = Timeline->bAutoPlay;
    Node->bLoop = Timeline->bLoop;
    Node->bReplicated = Timeline->bReplicated;
    Node->bIgnoreTimeDilation = Timeline->bIgnoreTimeDilation;
}

UEdGraphPin* FindUniqueTimelineTrackPin(const UK2Node_Timeline* Node, const FName TrackName, int32* OutMatches = nullptr)
{
    UEdGraphPin* Match = nullptr;
    int32 Matches = 0;
    if (Node != nullptr && !TrackName.IsNone())
    {
        for (UEdGraphPin* Pin : Node->Pins)
        {
            if (Pin != nullptr && Pin->Direction == EGPD_Output && Pin->PinName == TrackName)
            {
                Match = Pin;
                ++Matches;
            }
        }
    }
    if (OutMatches != nullptr) *OutMatches = Matches;
    return Matches == 1 ? Match : nullptr;
}

bool ValidateTimelineStructure(const UK2Node_Timeline* Node, UTimelineTemplate* Timeline, FString& OutError)
{
    OutError.Reset();
    if (Node == nullptr || Timeline == nullptr) { OutError = TEXT("Timeline pair is unavailable."); return false; }
    const int32 TrackCount = Timeline->EventTracks.Num() + Timeline->FloatTracks.Num() + Timeline->VectorTracks.Num() + Timeline->LinearColorTracks.Num();
    if (Timeline->GetNumDisplayTracks() != TrackCount)
    {
        OutError = TEXT("Timeline Track arrays and TrackDisplayOrder have different cardinality.");
        return false;
    }
    TSet<FName> Names;
    TSet<uint64> DisplayIds;
    auto ValidateTrack = [&](const FTTTrackBase& Track, const UCurveBase* Curve) -> bool
    {
        const FName Name = Track.GetTrackName();
        int32 PinMatches = 0;
        FindUniqueTimelineTrackPin(Node, Name, &PinMatches);
        if (Name.IsNone() || Names.Contains(Name) || Curve == nullptr || PinMatches != 1)
        {
            OutError = TEXT("Timeline has a missing Curve, duplicate TrackName, or non-unique generated Track Pin.");
            return false;
        }
        Names.Add(Name);
        return true;
    };
    for (int32 Index = 0; Index < Timeline->EventTracks.Num(); ++Index) if (!ValidateTrack(Timeline->EventTracks[Index], Timeline->EventTracks[Index].CurveKeys)) return false;
    for (int32 Index = 0; Index < Timeline->FloatTracks.Num(); ++Index) if (!ValidateTrack(Timeline->FloatTracks[Index], Timeline->FloatTracks[Index].CurveFloat)) return false;
    for (int32 Index = 0; Index < Timeline->VectorTracks.Num(); ++Index) if (!ValidateTrack(Timeline->VectorTracks[Index], Timeline->VectorTracks[Index].CurveVector)) return false;
    for (int32 Index = 0; Index < Timeline->LinearColorTracks.Num(); ++Index) if (!ValidateTrack(Timeline->LinearColorTracks[Index], Timeline->LinearColorTracks[Index].CurveLinearColor)) return false;
    for (int32 Display = 0; Display < Timeline->GetNumDisplayTracks(); ++Display)
    {
        const FTTTrackId Id = Timeline->GetDisplayTrackId(Display);
        const bool bValid = Id.TrackIndex >= 0 && (Id.TrackType == FTTTrackBase::TT_Event ? Timeline->EventTracks.IsValidIndex(Id.TrackIndex)
            : Id.TrackType == FTTTrackBase::TT_FloatInterp ? Timeline->FloatTracks.IsValidIndex(Id.TrackIndex)
            : Id.TrackType == FTTTrackBase::TT_VectorInterp ? Timeline->VectorTracks.IsValidIndex(Id.TrackIndex)
            : Id.TrackType == FTTTrackBase::TT_LinearColorInterp && Timeline->LinearColorTracks.IsValidIndex(Id.TrackIndex));
        const uint64 Key = (static_cast<uint64>(static_cast<uint32>(Id.TrackType)) << 32) | static_cast<uint32>(Id.TrackIndex);
        if (!bValid || DisplayIds.Contains(Key)) { OutError = TEXT("Timeline TrackDisplayOrder contains an invalid or duplicate native Track id."); return false; }
        DisplayIds.Add(Key);
    }
    return true;
}

bool JsonObjectValue(const TSharedPtr<FJsonValue>& Value, TSharedPtr<FJsonObject>& Out)
{
    Out.Reset();
    const TSharedPtr<FJsonObject>* Pointer = nullptr;
    if (!Value.IsValid() || !Value->TryGetObject(Pointer) || Pointer == nullptr || !(*Pointer).IsValid())
    {
        return false;
    }
    Out = *Pointer;
    return true;
}

bool ReadPath(const TSharedPtr<FJsonObject>& Ref, TArray<FString>& OutPath)
{
    OutPath.Reset();
    const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
    if (!Ref.IsValid() || !Ref->TryGetArrayField(TEXT("path"), Values) || Values == nullptr)
    {
        return false;
    }
    for (const TSharedPtr<FJsonValue>& Value : *Values)
    {
        FString Segment;
        if (!Value.IsValid() || !Value->TryGetString(Segment)) return false;
        OutPath.Add(Segment);
    }
    return !OutPath.IsEmpty();
}

UEdGraphPin* FindPinByMember(const UEdGraphNode* Node, const FString& Member)
{
    if (Node == nullptr)
    {
        return nullptr;
    }
    UEdGraphPin* NativeMatch = nullptr;
    UEdGraphPin* SanitizedMatch = nullptr;
    for (UEdGraphPin* Pin : Node->Pins)
    {
        if (Pin == nullptr) continue;
        const FString Native = Pin->PinName.ToString();
        if (Native == Member)
        {
            if (NativeMatch != nullptr) return nullptr;
            NativeMatch = Pin;
            continue;
        }
        if (SanitizeIdentifier(Native, TEXT("pin")) == Member)
        {
            if (SanitizedMatch != nullptr) return nullptr;
            SanitizedMatch = Pin;
        }
    }
    return NativeMatch != nullptr ? NativeMatch : SanitizedMatch;
}

FGraphObjectRef ResolveSimpleRef(FPatchState& State, const TSharedPtr<FJsonObject>& Ref)
{
    FGraphObjectRef Result;
    if (!Ref.IsValid()) return Result;
    FString Kind;
    Ref->TryGetStringField(TEXT("kind"), Kind);
    if (Kind == TEXT("node"))
    {
        FString Id;
        Ref->TryGetStringField(TEXT("id"), Id);
        Result.Node = FindNode(State.Target.Graph, Id);
        return Result;
    }
    if (Kind == TEXT("pin"))
    {
        FString Id;
        Ref->TryGetStringField(TEXT("id"), Id);
        Result.Pin = FindPin(State.Target.Graph, Id, &Result.bPinAmbiguous);
        Result.Node = Result.Pin != nullptr ? Result.Pin->GetOwningNode() : nullptr;
        return Result;
    }
    if (Kind == TEXT("graph"))
    {
        FString Id;
        Ref->TryGetStringField(TEXT("id"), Id);
        if (State.Target.Id.Equals(Id, ESearchCase::IgnoreCase)) Result.Graph = State.Target.Graph;
        return Result;
    }
    if (Kind == TEXT("local"))
    {
        FString Alias;
        Ref->TryGetStringField(TEXT("name"), Alias);
        if (Alias == State.Target.Alias)
        {
            Result.Graph = State.Target.Graph;
            return Result;
        }
        Result.Node = State.LocalNodes.FindRef(Alias);
        Result.Pin = State.LocalPins.FindRef(Alias);
        if (Result.Pin != nullptr && Result.Node == nullptr) Result.Node = Result.Pin->GetOwningNode();
        return Result;
    }
    if (Kind == TEXT("member"))
    {
        const TSharedPtr<FJsonObject>* Owner = nullptr;
        TArray<FString> Path;
        if (!Ref->TryGetObjectField(TEXT("object"), Owner) || Owner == nullptr || !ReadPath(Ref, Path)) return Result;
        FGraphObjectRef OwnerRef = ResolveSimpleRef(State, *Owner);
        if (OwnerRef.bPinAmbiguous)
        {
            Result.bPinAmbiguous = true;
            return Result;
        }
        if (OwnerRef.Node != nullptr && Path.Num() == 1)
        {
            Result.Pin = FindPinByMember(OwnerRef.Node, Path[0]);
            Result.Node = OwnerRef.Node;
        }
        return Result;
    }
    return Result;
}

bool ResolveRequiredRef(
    FPatchState& State,
    const TSharedPtr<FJsonObject>& Ref,
    const FString& Operation,
    FGraphObjectRef& Out)
{
    Out = ResolveSimpleRef(State, Ref);
    if (Out.IsValid()) return true;
    FString Kind;
    FString Id;
    if (Ref.IsValid())
    {
        Ref->TryGetStringField(TEXT("kind"), Kind);
        Ref->TryGetStringField(Kind == TEXT("local") ? TEXT("name") : TEXT("id"), Id);
    }
    AddPatchError(
        State,
        Out.bPinAmbiguous
            ? TEXT("resolution.pin_ambiguous")
            : Kind == TEXT("pin")
                ? TEXT("resolution.pin_not_found")
                : TEXT("resolution.object_not_found"),
        Out.bPinAmbiguous
            ? TEXT("PinId matches multiple Pins in the bound Graph.")
            : TEXT("Patch reference could not be resolved in the bound Graph or prior Patch statements."),
        Operation,
        Id);
    return false;
}

bool ReadOperationRef(const TSharedPtr<FJsonObject>& Operation, const TCHAR* Field, TSharedPtr<FJsonObject>& Out)
{
    Out.Reset();
    const TSharedPtr<FJsonObject>* Value = nullptr;
    if (!Operation.IsValid() || !Operation->TryGetObjectField(Field, Value) || Value == nullptr || !(*Value).IsValid()) return false;
    Out = *Value;
    return true;
}

bool ReadLocalAlias(const TSharedPtr<FJsonObject>& Ref, FString& OutAlias)
{
    OutAlias.Reset();
    FString Kind;
    return Ref.IsValid()
        && Ref->TryGetStringField(TEXT("kind"), Kind)
        && Kind == TEXT("local")
        && Ref->TryGetStringField(TEXT("name"), OutAlias)
        && !OutAlias.IsEmpty();
}

bool RegisterDefinition(FPatchState& State, const TSharedPtr<FJsonObject>& Binding)
{
    const TSharedPtr<FJsonObject>* TargetRef = nullptr;
    const TSharedPtr<FJsonObject>* Call = nullptr;
    FString Alias;
    if (!Binding->TryGetObjectField(TEXT("target"), TargetRef)
        || TargetRef == nullptr
        || !ReadLocalAlias(*TargetRef, Alias)
        || !Binding->TryGetObjectField(TEXT("value"), Call)
        || Call == nullptr)
    {
        AddPatchError(State, TEXT("capability.unsupported_constructor"), TEXT("Graph creation binding must bind one local alias to node(palette: ...)."), TEXT("binding"));
        return false;
    }
    if (State.Definitions.Contains(Alias) || State.LocalNodes.Contains(Alias) || State.LocalPins.Contains(Alias))
    {
        AddPatchError(State, TEXT("validation.duplicate_alias"), TEXT("Patch alias is already defined."), TEXT("binding"), Alias);
        return false;
    }
    FString Kind;
    FString Callee;
    const TSharedPtr<FJsonObject>* Args = nullptr;
    (*Call)->TryGetStringField(TEXT("kind"), Kind);
    (*Call)->TryGetStringField(TEXT("callee"), Callee);
    if (Kind != TEXT("call") || Callee != TEXT("node") || !(*Call)->TryGetObjectField(TEXT("args"), Args) || Args == nullptr)
    {
        AddPatchError(State, TEXT("capability.unsupported_constructor"), TEXT("Graph creation accepts only the Palette-returned node(...) constructor."), TEXT("binding"), Alias);
        return false;
    }
    FString Palette;
    (*Args)->TryGetStringField(TEXT("palette"), Palette);
    TSharedPtr<FEdGraphSchemaAction> Action = ResolvePaletteAction(State.Target, Palette);
    UEdGraphNode* Template = TemplateForAction(Action, State.Target.Graph);
    if (!Action.IsValid() || Template == nullptr)
    {
        AddPatchError(State, TEXT("resolution.palette_not_found"), TEXT("Palette entry is not available or not spawnable in the current Graph context."), TEXT("binding"), Palette);
        return false;
    }
    FNodeDefinition Definition;
    Definition.Alias = Alias;
    Definition.Palette = Palette;
    Definition.Action = Action;
    Definition.Template = Template;
    Definition.ConstructorArgs = *Args;
    if (Cast<UK2Node_Timeline>(Template) != nullptr)
    {
        FString TimelineName;
        if (!ReadArgText(Args, TEXT("TimelineName"), TimelineName) || TimelineName.IsEmpty())
        {
            AddPatchError(State, TEXT("validation.creation_invalid"), TEXT("Timeline Palette binding requires an exact TimelineName."), TEXT("binding"), Alias);
            return false;
        }
        static const TSet<FString> TimelineFields = {
            TEXT("palette"), TEXT("type"), TEXT("TimelineName"), TEXT("TimelineLength"), TEXT("LengthMode"),
            TEXT("bAutoPlay"), TEXT("bLoop"), TEXT("bReplicated"), TEXT("bIgnoreTimeDilation"),
            TEXT("MetaDataArray"), TEXT("TimelineTickGroup")
        };
        for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : (*Args)->Values)
        {
            if (!TimelineFields.Contains(Pair.Key))
            {
                AddPatchError(State, TEXT("validation.creation_invalid"), TEXT("Timeline constructor field is unavailable; Tracks are edited through invoke."), TEXT("binding"), Pair.Key);
                return false;
            }
        }
    }
    else
    {
        for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : (*Args)->Values)
        {
            if (Pair.Key != TEXT("palette") && Pair.Key != TEXT("type"))
            {
                AddPatchError(State, TEXT("validation.creation_invalid"), TEXT("This Palette entry does not advertise writable constructor fields."), TEXT("binding"), Pair.Key);
                return false;
            }
        }
    }
    State.Definitions.Add(Alias, Definition);
    return true;
}

FVector2f NextNodeLocation(const UEdGraph* Graph)
{
    float Right = 0.0f;
    float Top = 0.0f;
    bool bFound = false;
    if (Graph != nullptr)
    {
        for (const UEdGraphNode* Node : Graph->Nodes)
        {
            if (Node == nullptr) continue;
            const float Width = Node->NodeWidth > 0 ? static_cast<float>(Node->NodeWidth) : 240.0f;
            Right = bFound ? FMath::Max(Right, Node->NodePosX + Width) : Node->NodePosX + Width;
            Top = bFound ? FMath::Min(Top, static_cast<float>(Node->NodePosY)) : static_cast<float>(Node->NodePosY);
            bFound = true;
        }
    }
    return bFound ? FVector2f(Right + 320.0f, Top) : FVector2f::ZeroVector;
}

UEdGraphNode* MaterializeDefinition(FPatchState& State, const FString& Alias, const FString& Operation)
{
    FNodeDefinition* Definition = State.Definitions.Find(Alias);
    if (Definition == nullptr)
    {
        AddPatchError(State, TEXT("resolution.binding_not_found"), TEXT("Node creation alias was not declared earlier in the Patch."), Operation, Alias);
        return nullptr;
    }
    if (Definition->bConsumed)
    {
        AddPatchError(State, TEXT("resolution.binding_already_consumed"), TEXT("A creation binding may be consumed exactly once."), Operation, Alias);
        return nullptr;
    }
    Definition->bConsumed = true;
    if (!State.bApply)
    {
        State.LocalNodes.Add(Alias, Definition->Template);
        return Definition->Template;
    }
    if (!Definition->Action.IsValid() || Definition->Action->GetTypeId() != FBlueprintActionMenuItem::StaticGetTypeId())
    {
        AddPatchError(State, TEXT("resolution.palette_not_spawnable"), TEXT("Palette action cannot create a Node."), Operation, Definition->Palette);
        return nullptr;
    }
    FBlueprintActionMenuItem* Item = static_cast<FBlueprintActionMenuItem*>(Definition->Action.Get());
    UEdGraphNode* NewNode = Item->PerformAction(State.Target.Graph, nullptr, NextNodeLocation(State.Target.Graph), false);
    if (NewNode == nullptr)
    {
        AddPatchError(State, TEXT("validation.spawn_failed"), TEXT("UE Palette action did not create a Node."), Operation, Definition->Palette);
        return nullptr;
    }
    State.LocalNodes.Add(Alias, NewNode);
    State.TouchedNodes.Add(NewNode);
    State.ResolvedRefs->SetStringField(Alias, NodeId(NewNode));
    ++State.ChangedOps;
    if (Cast<UK2Node_Timeline>(NewNode) != nullptr && Definition->ConstructorArgs.IsValid())
    {
        static const TCHAR* OrderedFields[] = {
            TEXT("TimelineName"), TEXT("TimelineLength"), TEXT("LengthMode"), TEXT("bAutoPlay"),
            TEXT("bLoop"), TEXT("bReplicated"), TEXT("bIgnoreTimeDilation"), TEXT("MetaDataArray"), TEXT("TimelineTickGroup")
        };
        for (const TCHAR* Field : OrderedFields)
        {
            const TSharedPtr<FJsonValue> Value = Definition->ConstructorArgs->TryGetField(Field);
            if (!Value.IsValid()) continue;
            FString Error;
            if (!SetObjectField(State, NewNode, Field, Value, false, Error))
            {
                AddPatchError(State, TEXT("validation.creation_invalid"), Error.IsEmpty() ? TEXT("Timeline constructor field was rejected by UE.") : Error, Operation, Field);
                return nullptr;
            }
        }
    }
    return NewNode;
}

bool ReadPoint(const TSharedPtr<FJsonValue>& Value, FIntPoint& Out)
{
    const TArray<TSharedPtr<FJsonValue>>* Items = nullptr;
    if (!Value.IsValid() || !Value->TryGetArray(Items) || Items == nullptr || Items->Num() != 2) return false;
    double X = 0.0;
    double Y = 0.0;
    if (!(*Items)[0]->TryGetNumber(X) || !(*Items)[1]->TryGetNumber(Y)) return false;
    Out = FIntPoint(static_cast<int32>(X), static_cast<int32>(Y));
    return true;
}

bool PinsAreLinked(const UEdGraphPin* A, const UEdGraphPin* B)
{
    return A != nullptr && B != nullptr && A->LinkedTo.Contains(const_cast<UEdGraphPin*>(B));
}

bool ValidateConnection(UEdGraphPin* From, UEdGraphPin* To, FString& OutMessage)
{
    if (From == nullptr || To == nullptr)
    {
        OutMessage = TEXT("Both connection endpoints must resolve to Pins.");
        return false;
    }
    if (From->Direction != EGPD_Output || To->Direction != EGPD_Input)
    {
        OutMessage = TEXT("Connection requires an output Pin followed by an input Pin.");
        return false;
    }
    if (From->GetOwningNode() == To->GetOwningNode())
    {
        OutMessage = TEXT("Pins on the same Node cannot be connected by this operation.");
        return false;
    }
    if (PinsAreLinked(From, To)) return true;
    if (From->GetOwningNode()->GetGraph() != To->GetOwningNode()->GetGraph())
    {
        OutMessage = TEXT("Connection endpoints must belong to the same Graph.");
        return false;
    }
    const UEdGraphSchema* Schema = From->GetSchema();
    if (Schema == nullptr)
    {
        OutMessage = TEXT("Owning Graph Schema is unavailable.");
        return false;
    }
    const FPinConnectionResponse Response = Schema->CanCreateConnection(From, To);
    if (Response.Response == CONNECT_RESPONSE_DISALLOW)
    {
        OutMessage = Response.Message.ToString();
        return false;
    }
    return true;
}

bool ApplyConnect(FPatchState& State, UEdGraphPin* From, UEdGraphPin* To, const FString& Operation)
{
    FString Message;
    if (!ValidateConnection(From, To, Message))
    {
        AddPatchError(State, TEXT("resolution.pin_not_connectable"), Message, Operation);
        return false;
    }
    if (!State.bApply || PinsAreLinked(From, To)) return true;
    const UEdGraphSchema* Schema = From->GetSchema();
    TSet<UEdGraphNode*> NodesBefore;
    if (State.Target.Graph != nullptr) for (UEdGraphNode* Existing : State.Target.Graph->Nodes) if (Existing != nullptr) NodesBefore.Add(Existing);
    for (UEdGraphPin* Existing : From->LinkedTo) if (Existing != nullptr && Existing->GetOwningNode() != nullptr) State.TouchedNodes.Add(Existing->GetOwningNode());
    for (UEdGraphPin* Existing : To->LinkedTo) if (Existing != nullptr && Existing->GetOwningNode() != nullptr) State.TouchedNodes.Add(Existing->GetOwningNode());
    From->GetOwningNode()->Modify();
    To->GetOwningNode()->Modify();
    if (Schema == nullptr || !Schema->TryCreateConnection(From, To))
    {
        AddPatchError(State, TEXT("validation.connect_failed"), TEXT("UE Graph Schema rejected the connection during apply."), Operation);
        return false;
    }
    State.TouchedPins.Add(From);
    State.TouchedPins.Add(To);
    State.TouchedNodes.Add(From->GetOwningNode());
    State.TouchedNodes.Add(To->GetOwningNode());
    if (State.Target.Graph != nullptr)
    {
        for (UEdGraphNode* Existing : State.Target.Graph->Nodes)
        {
            if (Existing != nullptr && !NodesBefore.Contains(Existing)) State.TouchedNodes.Add(Existing);
        }
    }
    ++State.ChangedOps;
    return true;
}

bool ResolvePinRef(FPatchState& State, const TSharedPtr<FJsonObject>& Ref, const FString& Operation, UEdGraphPin*& OutPin)
{
    FGraphObjectRef Object;
    if (!ResolveRequiredRef(State, Ref, Operation, Object)) return false;
    OutPin = Object.Pin;
    if (OutPin == nullptr)
    {
        AddPatchError(State, TEXT("resolution.pin_not_found"), TEXT("Operation requires a Pin reference."), Operation);
        return false;
    }
    return true;
}

bool HandleAdd(FPatchState& State, const TSharedPtr<FJsonObject>& Operation)
{
    TSharedPtr<FJsonObject> TargetRef;
    FString Alias;
    if (!ReadOperationRef(Operation, TEXT("target"), TargetRef) || !ReadLocalAlias(TargetRef, Alias))
    {
        AddPatchError(State, TEXT("validation.invalid_add"), TEXT("Graph add requires a local creation alias."), TEXT("add"));
        return false;
    }
    UEdGraphNode* Node = MaterializeDefinition(State, Alias, TEXT("add"));
    if (Node == nullptr) return false;

    for (const TCHAR* Placement : {TEXT("to"), TEXT("before"), TEXT("after")})
    {
        TSharedPtr<FJsonObject> Ref;
        if (ReadOperationRef(Operation, Placement, Ref))
        {
            AddPatchError(State, TEXT("capability.clause_unavailable"), TEXT("Graph add does not use tree placement clauses; connect the new Node explicitly."), TEXT("add"));
            return false;
        }
    }
    return true;
}

bool HandleConnectLike(FPatchState& State, const TSharedPtr<FJsonObject>& Operation, const bool bConnect)
{
    TSharedPtr<FJsonObject> FromRef;
    TSharedPtr<FJsonObject> ToRef;
    UEdGraphPin* From = nullptr;
    UEdGraphPin* To = nullptr;
    const FString Name = bConnect ? TEXT("connect") : TEXT("disconnect");
    if (!ReadOperationRef(Operation, TEXT("from"), FromRef)
        || !ReadOperationRef(Operation, TEXT("to"), ToRef)
        || !ResolvePinRef(State, FromRef, Name, From)
        || !ResolvePinRef(State, ToRef, Name, To))
    {
        return false;
    }
    if (bConnect) return ApplyConnect(State, From, To, Name);
    if (!PinsAreLinked(From, To))
    {
        AddPatchError(State, TEXT("resolution.edge_not_found"), TEXT("disconnect requires one exact existing Edge."), Name);
        return false;
    }
    if (State.bApply)
    {
        const UEdGraphSchema* Schema = From->GetSchema();
        if (Schema == nullptr) return false;
        From->GetOwningNode()->Modify();
        To->GetOwningNode()->Modify();
        Schema->BreakSinglePinLink(From, To);
        State.TouchedPins.Add(From);
        State.TouchedPins.Add(To);
        State.TouchedNodes.Add(From->GetOwningNode());
        State.TouchedNodes.Add(To->GetOwningNode());
        ++State.ChangedOps;
    }
    return true;
}

bool HandleBreak(FPatchState& State, const TSharedPtr<FJsonObject>& Operation)
{
    TSharedPtr<FJsonObject> TargetRef;
    UEdGraphPin* Pin = nullptr;
    if (!ReadOperationRef(Operation, TEXT("target"), TargetRef) || !ResolvePinRef(State, TargetRef, TEXT("break"), Pin)) return false;
    if (State.bApply && !Pin->LinkedTo.IsEmpty())
    {
        const UEdGraphSchema* Schema = Pin->GetSchema();
        if (Schema == nullptr) return false;
        TArray<UEdGraphPin*> Linked = Pin->LinkedTo;
        Pin->GetOwningNode()->Modify();
        for (UEdGraphPin* Other : Linked)
        {
            if (Other != nullptr)
            {
                Other->GetOwningNode()->Modify();
                State.TouchedPins.Add(Other);
                State.TouchedNodes.Add(Other->GetOwningNode());
            }
        }
        Schema->BreakPinLinks(*Pin, true);
        State.TouchedPins.Add(Pin);
        State.TouchedNodes.Add(Pin->GetOwningNode());
        ++State.ChangedOps;
    }
    return true;
}

bool ResolveFieldTarget(
    FPatchState& State,
    const TSharedPtr<FJsonObject>& Member,
    const FString& Operation,
    FGraphObjectRef& OutObject,
    FString& OutField)
{
    OutObject = {};
    OutField.Reset();
    const TSharedPtr<FJsonObject>* Owner = nullptr;
    TArray<FString> Path;
    if (!Member.IsValid()
        || !Member->TryGetObjectField(TEXT("object"), Owner)
        || Owner == nullptr
        || !ReadPath(Member, Path))
    {
        AddPatchError(State, TEXT("validation.invalid_field_target"), TEXT("Field operation requires a member reference."), Operation);
        return false;
    }
    OutObject = ResolveSimpleRef(State, *Owner);
    if (!OutObject.IsValid())
    {
        AddPatchError(
            State,
            OutObject.bPinAmbiguous
                ? TEXT("resolution.pin_ambiguous")
                : TEXT("resolution.object_not_found"),
            OutObject.bPinAmbiguous
                ? TEXT("PinId matches multiple Pins in the bound Graph.")
                : TEXT("Field owner could not be resolved."),
            Operation);
        return false;
    }
    if (OutObject.Pin != nullptr)
    {
        if (Path.Num() != 1)
        {
            AddPatchError(State, TEXT("validation.invalid_field_target"), TEXT("Pin field reference must contain exactly one field segment."), Operation);
            return false;
        }
        OutField = Path[0];
        return true;
    }
    if (OutObject.Node != nullptr && Path.Num() == 2)
    {
        UEdGraphPin* Pin = FindPinByMember(OutObject.Node, Path[0]);
        if (Pin == nullptr)
        {
            AddPatchError(State, TEXT("resolution.pin_not_found"), TEXT("Node member does not resolve to one Pin."), Operation, Path[0]);
            return false;
        }
        OutObject.Pin = Pin;
        OutField = Path[1];
        return true;
    }
    if (Path.Num() != 1)
    {
        AddPatchError(State, TEXT("validation.invalid_field_target"), TEXT("Structured UE values are written as native text; arbitrary deep field paths are unsupported."), Operation);
        return false;
    }
    OutField = Path[0];
    return true;
}

TSharedPtr<FUserPinInfo> FindUserPinInfo(UK2Node_EditablePinBase* Node, const FName Name)
{
    if (Node == nullptr) return nullptr;
    for (const TSharedPtr<FUserPinInfo>& Info : Node->UserDefinedPins)
    {
        if (Info.IsValid() && Info->PinName == Name) return Info;
    }
    return nullptr;
}

void SignatureOwnersForPin(UEdGraphPin* Pin, TArray<UK2Node_EditablePinBase*>& Out)
{
    Out.Reset();
    UK2Node_EditablePinBase* Editable = Pin != nullptr ? Cast<UK2Node_EditablePinBase>(Pin->GetOwningNode()) : nullptr;
    if (Editable == nullptr || !Editable->bIsEditable || !FindUserPinInfo(Editable, Pin->PinName).IsValid()) return;
    if (UK2Node_FunctionResult* Result = Cast<UK2Node_FunctionResult>(Editable))
    {
        TArray<UK2Node_FunctionResult*> Results = Result->GetAllResultNodes();
        for (UK2Node_FunctionResult* Item : Results)
        {
            if (Item != nullptr && Item->bIsEditable && FindUserPinInfo(Item, Pin->PinName).IsValid()) Out.Add(Item);
        }
    }
    else
    {
        Out.Add(Editable);
    }
}

void PropagateSignatureChanges(const TArray<UK2Node_EditablePinBase*>& Nodes)
{
    UK2Node_EditablePinBase* Primary = nullptr;
    for (UK2Node_EditablePinBase* Node : Nodes)
    {
        if (Node == nullptr) continue;
        if (Primary == nullptr) Primary = Node;
        const bool bOldDisableOrphans = Node->bDisableOrphanPinSaving;
        Node->bDisableOrphanPinSaving = true;
        Node->ReconstructNode();
        Node->bDisableOrphanPinSaving = bOldDisableOrphans;
    }
    if (Primary != nullptr) GetDefault<UEdGraphSchema_K2>()->HandleParameterDefaultValueChanged(Primary);
}

bool SetSignaturePinField(
    FPatchState& State,
    UEdGraphPin* Pin,
    const FString& Field,
    const TSharedPtr<FJsonValue>& Value,
    const bool bReset,
    FString& OutError)
{
    TArray<UK2Node_EditablePinBase*> Owners;
    SignatureOwnersForPin(Pin, Owners);
    if (Owners.IsEmpty()) return false;
    if (!(Field == TEXT("PinName") || Field == TEXT("type") || Field == TEXT("DefaultValue")))
    {
        OutError = TEXT("Authored signature Pins expose only PinName, type, and DefaultValue for mutation.");
        return true;
    }
    if (bReset && (Field == TEXT("PinName") || Field == TEXT("type")))
    {
        OutError = TEXT("Signature Pin identity and type have no generic reset operation.");
        return true;
    }
    if (Field == TEXT("DefaultValue") && Pin->bDefaultValueIsReadOnly)
    {
        OutError = TEXT("UE marks this signature Pin default as read-only.");
        return true;
    }
    const FName OldName = Pin->PinName;
    const FString Text = ExprNativeText(Value);
    if (Field == TEXT("PinName"))
    {
        const FName NewName(*Text);
        if (NewName.IsNone()) { OutError = TEXT("PinName cannot be empty."); return true; }
        if (NewName == OldName) return true;
        for (UK2Node_EditablePinBase* Owner : Owners)
        {
            if (Owner->RenameUserDefinedPin(OldName, NewName, true) != ERenamePinResult_Success)
            {
                OutError = TEXT("UE rejected the signature Pin rename or found a collision.");
                return true;
            }
        }
        for (UK2Node_EditablePinBase* Owner : Owners)
        {
            Owner->Modify();
            if (Owner->RenameUserDefinedPin(OldName, NewName, false) != ERenamePinResult_Success)
            {
                OutError = TEXT("UE failed to apply the signature Pin rename.");
                return true;
            }
            const TSharedPtr<FUserPinInfo> Info = FindUserPinInfo(Owner, OldName);
            if (!Info.IsValid()) { OutError = TEXT("Authored signature Pin state disappeared during rename."); return true; }
            Info->PinName = NewName;
        }
    }
    else if (Field == TEXT("type"))
    {
        FEdGraphPinType NewType;
        if (!ImportPinType(Text, NewType)) { OutError = TEXT("type requires complete native FEdGraphPinType text."); return true; }
        if (Pin->PinType == NewType) return true;
        for (UK2Node_EditablePinBase* Owner : Owners)
        {
            const TSharedPtr<FUserPinInfo> Info = FindUserPinInfo(Owner, OldName);
            FText NativeError;
            if (!Info.IsValid() || !Owner->CanCreateUserDefinedPin(NewType, Info->DesiredPinDirection, NativeError))
            {
                OutError = NativeError.IsEmpty() ? TEXT("UE rejected the requested signature Pin type.") : NativeError.ToString();
                return true;
            }
        }
        for (UK2Node_EditablePinBase* Owner : Owners)
        {
            Owner->Modify();
            FindUserPinInfo(Owner, OldName)->PinType = NewType;
        }
    }
    else
    {
        const FString NewDefault = bReset ? FString() : Text;
        const TSharedPtr<FUserPinInfo> First = FindUserPinInfo(Owners[0], OldName);
        if (First.IsValid() && First->PinDefaultValue == NewDefault) return true;
        for (UK2Node_EditablePinBase* Owner : Owners)
        {
            const TSharedPtr<FUserPinInfo> Info = FindUserPinInfo(Owner, OldName);
            Owner->Modify();
            if (!Info.IsValid() || !Owner->ModifyUserDefinedPinDefaultValue(Info, NewDefault))
            {
                OutError = TEXT("UE rejected the signature Pin default value.");
                return true;
            }
        }
    }
    for (UK2Node_EditablePinBase* Owner : Owners) State.TouchedNodes.Add(Owner);
    PropagateSignatureChanges(Owners);
    ++State.ChangedOps;
    return true;
}

bool SetPinField(FPatchState& State, UEdGraphPin* Pin, const FString& Field, const TSharedPtr<FJsonValue>& Value, const bool bReset, FString& OutError)
{
    OutError.Reset();
    if (Pin == nullptr) return false;
    if (SetSignaturePinField(State, Pin, Field, Value, bReset, OutError)) return OutError.IsEmpty();
    if (Pin->bDefaultValueIsReadOnly && (Field == TEXT("DefaultValue") || Field == TEXT("DefaultObject") || Field == TEXT("DefaultTextValue")))
    {
        OutError = TEXT("UE marks this Pin default as read-only.");
        return false;
    }
    const FString Text = ExprNativeText(Value);
    if (!State.bApply)
    {
        static const TSet<FString> Fields = {
            TEXT("DefaultValue"), TEXT("AutogeneratedDefaultValue"), TEXT("DefaultObject"), TEXT("DefaultTextValue"),
            TEXT("PinName"), TEXT("type"), TEXT("bHidden"), TEXT("bNotConnectable"), TEXT("bDefaultValueIsReadOnly"),
            TEXT("bDefaultValueIsIgnored"), TEXT("bAdvancedView"), TEXT("bDeprecated"), TEXT("bOrphanedPin")
        };
        if (!Fields.Contains(Field)) OutError = TEXT("Pin field is not schema-approved for mutation.");
        if (Field == TEXT("DefaultValue") && Pin->bDefaultValueIsReadOnly) OutError = TEXT("Pin DefaultValue is read-only.");
        return OutError.IsEmpty();
    }

    const UEdGraphSchema* Schema = Pin->GetSchema();
    if (Field == TEXT("DefaultValue"))
    {
        const FString NewValue = bReset ? Pin->AutogeneratedDefaultValue : Text;
        if (Pin->DefaultValue == NewValue) return true;
        if (const UEdGraphSchema_K2* K2 = Cast<UEdGraphSchema_K2>(Schema))
        {
            const FString NativeError = K2->IsPinDefaultValid(Pin, NewValue, Pin->DefaultObject, Pin->DefaultTextValue);
            if (!NativeError.IsEmpty()) { OutError = NativeError; return false; }
        }
        Pin->Modify();
        if (Schema != nullptr) Schema->TrySetDefaultValue(*Pin, NewValue, true);
        else Pin->DefaultValue = NewValue;
    }
    else if (Field == TEXT("DefaultObject"))
    {
        UObject* Object = bReset || Text.IsEmpty() ? nullptr : LoadObject<UObject>(nullptr, *Text);
        if (!bReset && !Text.IsEmpty() && Object == nullptr) { OutError = TEXT("DefaultObject path could not be resolved."); return false; }
        if (Pin->DefaultObject == Object) return true;
        if (const UEdGraphSchema_K2* K2 = Cast<UEdGraphSchema_K2>(Schema))
        {
            const FString NativeError = K2->IsPinDefaultValid(Pin, Pin->DefaultValue, Object, Pin->DefaultTextValue);
            if (!NativeError.IsEmpty()) { OutError = NativeError; return false; }
        }
        Pin->Modify();
        if (Schema != nullptr) Schema->TrySetDefaultObject(*Pin, Object, true);
        else Pin->DefaultObject = Object;
    }
    else if (Field == TEXT("DefaultTextValue"))
    {
        const FText NewValue = bReset ? FText::GetEmpty() : FText::FromString(Text);
        if (Pin->DefaultTextValue.EqualTo(NewValue)) return true;
        if (const UEdGraphSchema_K2* K2 = Cast<UEdGraphSchema_K2>(Schema))
        {
            const FString NativeError = K2->IsPinDefaultValid(Pin, Pin->DefaultValue, Pin->DefaultObject, NewValue);
            if (!NativeError.IsEmpty()) { OutError = NativeError; return false; }
        }
        Pin->Modify();
        if (Schema != nullptr) Schema->TrySetDefaultText(*Pin, NewValue, true);
        else Pin->DefaultTextValue = NewValue;
    }
    else if (Field == TEXT("PinName")) { OutError = TEXT("PinName is writable only for authored signature Pins."); return false; }
    else if (Field == TEXT("type"))
    {
        OutError = TEXT("Pin type is writable only for authored signature Pins.");
        return false;
    }
    else
    {
        OutError = TEXT("Structural Pin flags are readable native state, not generic writable fields.");
        return false;
    }
    State.TouchedPins.Add(Pin);
    State.TouchedNodes.Add(Pin->GetOwningNode());
    ++State.ChangedOps;
    return true;
}

bool IsTimelineCompoundField(const FString& Field)
{
    static const TSet<FString> Fields = {
        TEXT("TimelineName"), TEXT("TimelineLength"), TEXT("LengthMode"), TEXT("bAutoPlay"),
        TEXT("bLoop"), TEXT("bReplicated"), TEXT("bIgnoreTimeDilation"), TEXT("MetaDataArray"),
        TEXT("TimelineTickGroup"), TEXT("EventTracks"), TEXT("FloatTracks"), TEXT("VectorTracks"),
        TEXT("LinearColorTracks"), TEXT("TrackDisplayOrder"), TEXT("TimelineGuid"),
        TEXT("DirectionPropertyName"), TEXT("UpdateFunctionName"), TEXT("FinishedFunctionName")
    };
    return Fields.Contains(Field);
}

struct FNodePropertyAccess
{
    bool bReadable = false;
    bool bWritable = false;
    bool bResettable = false;
    const UObject* DefaultObject = nullptr;
};

FNodePropertyAccess GetNodePropertyAccess(const UObject* Object, const FProperty* Property)
{
    FNodePropertyAccess Access;
    const UEdGraphNode* Node = Cast<UEdGraphNode>(Object);
    if (Node == nullptr || Property == nullptr) return Access;

    static const TSet<FName> Reserved = {
        TEXT("Pins"), TEXT("DeprecatedPins"), TEXT("NodeGuid"),
        TEXT("NodePosX"), TEXT("NodePosY"), TEXT("NodeWidth"), TEXT("NodeHeight"),
        TEXT("ErrorType"), TEXT("ErrorMsg"), TEXT("bHasCompilerMessage"),
        TEXT("bCommentBubbleVisible_InDetailsPanel"), TEXT("AdvancedPinDisplay")
    };
    static const TSet<FName> TimelineReserved = {
        TEXT("TimelineName"), TEXT("TimelineGuid"), TEXT("bAutoPlay"), TEXT("bLoop"),
        TEXT("bReplicated"), TEXT("bIgnoreTimeDilation")
    };
    if (Reserved.Contains(Property->GetFName())
        || (Cast<UK2Node_Timeline>(Node) != nullptr && TimelineReserved.Contains(Property->GetFName()))
        || Property->HasAnyPropertyFlags(CPF_Transient | CPF_DuplicateTransient | CPF_NonPIEDuplicateTransient | CPF_Deprecated))
    {
        return Access;
    }

    Access.bReadable = true;
    Access.DefaultObject = Object->GetClass() != nullptr ? Object->GetClass()->GetDefaultObject() : nullptr;
    // NodeComment is persistent editor state authored directly through the
    // Graph editor, but UE does not mark the native property CPF_Edit because
    // it is not exposed through the Details panel. SAL's documented Graph
    // contract intentionally exposes that native editor operation.
    const bool bGraphEditorWritable =
        Property->GetFName()
        == GET_MEMBER_NAME_CHECKED(UEdGraphNode, NodeComment);
    Access.bWritable = bGraphEditorWritable
        || (Property->HasAnyPropertyFlags(CPF_Edit)
            && !Property->HasAnyPropertyFlags(CPF_EditConst)
            && !(Object->IsTemplate() && Property->HasAnyPropertyFlags(CPF_DisableEditOnTemplate))
            && !(!Object->IsTemplate() && Property->HasAnyPropertyFlags(CPF_DisableEditOnInstance))
            && Object->CanEditChange(Property));
    Access.bResettable = Access.bWritable
        && !Property->HasMetaData(TEXT("NoResetToDefault"))
        && Access.DefaultObject != nullptr;
    return Access;
}

bool SetTimelineField(
    FPatchState& State,
    UK2Node_Timeline* Node,
    const FString& Field,
    const TSharedPtr<FJsonValue>& Value,
    const bool bReset,
    FString& OutError)
{
    FString PairError;
    UTimelineTemplate* Timeline = FindTimelineTemplate(State.Target.Blueprint, Node, &PairError);
    if (Timeline == nullptr) { OutError = PairError; return false; }
    if (!ValidateTimelineStructure(Node, Timeline, OutError)) return false;
    static const TSet<FString> Structural = {
        TEXT("EventTracks"), TEXT("FloatTracks"), TEXT("VectorTracks"), TEXT("LinearColorTracks"),
        TEXT("TrackDisplayOrder"), TEXT("TimelineGuid"), TEXT("DirectionPropertyName"),
        TEXT("UpdateFunctionName"), TEXT("FinishedFunctionName")
    };
    if (Structural.Contains(Field))
    {
        OutError = TEXT("Timeline structural or derived field is read-only; use the target-local Timeline Operations.");
        return false;
    }
    if (Field == TEXT("TimelineName"))
    {
        if (bReset) { OutError = TEXT("TimelineName has no reset operation."); return false; }
        const FString NewName = ExprNativeText(Value);
        if (NewName.IsEmpty() || NewName == TEXT("None")) { OutError = TEXT("TimelineName cannot be empty."); return false; }
        if (Node->TimelineName.ToString() == NewName) return true;
        Node->Modify();
        Timeline->Modify();
        if (!Node->RenameTimeline(NewName))
        {
            OutError = TEXT("UE rejected TimelineName because the variable or Template name is unavailable.");
            return false;
        }
        State.TouchedNodes.Add(Node);
        ++State.ChangedOps;
        return true;
    }

    static const TSet<FString> Writable = {
        TEXT("TimelineLength"), TEXT("LengthMode"), TEXT("bAutoPlay"), TEXT("bLoop"),
        TEXT("bReplicated"), TEXT("bIgnoreTimeDilation"), TEXT("MetaDataArray"), TEXT("TimelineTickGroup")
    };
    if (!Writable.Contains(Field))
    {
        OutError = TEXT("Timeline field is not writable through the compound Node schema.");
        return false;
    }
    FProperty* Property = FindFProperty<FProperty>(UTimelineTemplate::StaticClass(), *Field);
    const UTimelineTemplate* DefaultTimeline = GetDefault<UTimelineTemplate>();
    if (Property == nullptr || DefaultTimeline == nullptr)
    {
        OutError = TEXT("Timeline native field or default is unavailable.");
        return false;
    }
    FString NewText = bReset ? ExportPropertyValue(Property, DefaultTimeline) : ExprNativeText(Value);
    if (Field == TEXT("TimelineLength"))
    {
        const double Length = FCString::Atod(*NewText);
        if (Length <= KINDA_SMALL_NUMBER)
        {
            OutError = TEXT("TimelineLength must be greater than KINDA_SMALL_NUMBER.");
            return false;
        }
    }
    const FString OldText = ExportPropertyValue(Property, Timeline);
    if (OldText == NewText) return true;
    Timeline->Modify();
    if (!ImportPropertyValue(Property, Timeline, NewText, OutError)) return false;
    Node->Modify();
    SyncTimelineNodeCache(Node, Timeline);
    State.TouchedNodes.Add(Node);
    ++State.ChangedOps;
    return true;
}

bool SetObjectField(FPatchState& State, UObject* Object, const FString& Field, const TSharedPtr<FJsonValue>& Value, const bool bReset, FString& OutError)
{
    OutError.Reset();
    if (Object == nullptr) { OutError = TEXT("Object is unavailable."); return false; }
    if (UK2Node_Timeline* Timeline = Cast<UK2Node_Timeline>(Object); Timeline != nullptr && IsTimelineCompoundField(Field))
    {
        return SetTimelineField(State, Timeline, Field, Value, bReset, OutError);
    }
    FProperty* Property = FindFProperty<FProperty>(Object->GetClass(), *Field);
    const FNodePropertyAccess Access = GetNodePropertyAccess(Object, Property);
    if (!Access.bReadable)
    {
        OutError = TEXT("Native field is unavailable or not persistent.");
        return false;
    }
    if (!Access.bWritable)
    {
        OutError = TEXT("Native field is readable but is not editor-writable on this Node instance.");
        return false;
    }
    if (bReset && !Access.bResettable)
    {
        OutError = TEXT("Native field cannot reset because UE disables reset or no Class default is available.");
        return false;
    }
    if (!State.bApply) return true;
    FString NewText;
    if (bReset)
    {
        NewText = ExportPropertyValue(Property, Access.DefaultObject);
    }
    else
    {
        NewText = ExprNativeText(Value);
    }
    if (ExportPropertyValue(Property, Object) == NewText) return true;
    Object->Modify();
    Object->PreEditChange(Property);
    if (!ImportPropertyValue(Property, Object, NewText, OutError))
    {
        Object->PostEditChange();
        return false;
    }
    FPropertyChangedEvent ChangedEvent(Property, EPropertyChangeType::ValueSet);
    Object->PostEditChangeProperty(ChangedEvent);
    if (UEdGraphNode* Node = Cast<UEdGraphNode>(Object)) State.TouchedNodes.Add(Node);
    ++State.ChangedOps;
    return true;
}

bool HandleSetReset(FPatchState& State, const TSharedPtr<FJsonObject>& Operation, const bool bReset)
{
    TSharedPtr<FJsonObject> Member;
    const FString Name = bReset ? TEXT("reset") : TEXT("set");
    if (!ReadOperationRef(Operation, TEXT("target"), Member)) return false;
    FGraphObjectRef Object;
    FString Field;
    if (!ResolveFieldTarget(State, Member, Name, Object, Field)) return false;
    const TSharedPtr<FJsonValue> Value = Operation->TryGetField(TEXT("value"));
    FString Error;
    bool bOk = false;
    if (Object.Pin != nullptr) bOk = SetPinField(State, Object.Pin, Field, Value, bReset, Error);
    else if (Object.Node != nullptr) bOk = SetObjectField(State, Object.Node, Field, Value, bReset, Error);
    else if (Object.Graph != nullptr) Error = TEXT("Graph identity and native Graph fields are read-only in the Graph interface.");
    if (!bOk) AddPatchError(State, TEXT("validation.field_write_failed"), Error.IsEmpty() ? TEXT("Field write failed.") : Error, Name, Field);
    return bOk;
}

bool HandleMove(FPatchState& State, const TSharedPtr<FJsonObject>& Operation)
{
    TSharedPtr<FJsonObject> TargetRef;
    FGraphObjectRef Object;
    if (!ReadOperationRef(Operation, TEXT("target"), TargetRef) || !ResolveRequiredRef(State, TargetRef, TEXT("move"), Object)) return false;
    if (Object.Node == nullptr || Object.Pin != nullptr)
    {
        AddPatchError(State, TEXT("validation.invalid_target"), TEXT("Graph move accepts only a Node."), TEXT("move"));
        return false;
    }
    FIntPoint Point;
    bool bBy = false;
    if (Operation->HasField(TEXT("to")))
    {
        if (!ReadPoint(Operation->TryGetField(TEXT("to")), Point))
        {
            AddPatchError(State, TEXT("validation.layout_invalid"), TEXT("Graph move to requires an integer point."), TEXT("move"));
            return false;
        }
    }
    else if (Operation->HasField(TEXT("by")))
    {
        if (!ReadPoint(Operation->TryGetField(TEXT("by")), Point)) return false;
        bBy = true;
    }
    else
    {
        AddPatchError(State, TEXT("capability.clause_unavailable"), TEXT("Graph move supports only to (x, y) or by (dx, dy)."), TEXT("move"));
        return false;
    }
    if (State.bApply)
    {
        Object.Node->Modify();
        const FIntPoint NewPoint = bBy ? FIntPoint(Object.Node->NodePosX + Point.X, Object.Node->NodePosY + Point.Y) : Point;
        if (Object.Node->NodePosX != NewPoint.X || Object.Node->NodePosY != NewPoint.Y)
        {
            Object.Node->NodePosX = NewPoint.X;
            Object.Node->NodePosY = NewPoint.Y;
            State.TouchedNodes.Add(Object.Node);
            ++State.ChangedOps;
        }
    }
    return true;
}

bool HandleRemove(FPatchState& State, const TSharedPtr<FJsonObject>& Operation)
{
    TSharedPtr<FJsonObject> TargetRef;
    FGraphObjectRef Object;
    if (!ReadOperationRef(Operation, TEXT("target"), TargetRef) || !ResolveRequiredRef(State, TargetRef, TEXT("remove"), Object)) return false;
    if (Object.Node == nullptr || Object.Pin != nullptr)
    {
        AddPatchError(State, TEXT("validation.invalid_target"), TEXT("Graph remove accepts only a Node; Pin removal is a schema-discovered invoke operation."), TEXT("remove"));
        return false;
    }
    if (!Object.Node->CanUserDeleteNode())
    {
        AddPatchError(State, TEXT("validation.node_not_deletable"), TEXT("UE marks this Node as not user-deletable."), TEXT("remove"), NodeId(Object.Node));
        return false;
    }
    if (State.bApply)
    {
        for (UEdGraphPin* Pin : Object.Node->Pins)
        {
            if (Pin == nullptr) continue;
            for (UEdGraphPin* Linked : Pin->LinkedTo)
            {
                if (Linked != nullptr && Linked->GetOwningNode() != nullptr) State.TouchedNodes.Add(Linked->GetOwningNode());
            }
        }
        State.TouchedNodes.Add(Object.Node);
        FBlueprintEditorUtils::RemoveNode(State.Target.Blueprint, Object.Node, true);
        ++State.ChangedOps;
    }
    return true;
}

bool HandleInsert(FPatchState& State, const TSharedPtr<FJsonObject>& Operation)
{
    TSharedPtr<FJsonObject> FromRef;
    TSharedPtr<FJsonObject> InputRef;
    TSharedPtr<FJsonObject> OutputRef;
    TSharedPtr<FJsonObject> ToRef;
    if (!ReadOperationRef(Operation, TEXT("from"), FromRef)
        || !ReadOperationRef(Operation, TEXT("input"), InputRef)
        || !ReadOperationRef(Operation, TEXT("output"), OutputRef)
        || !ReadOperationRef(Operation, TEXT("to"), ToRef)) return false;

    auto MemberOwnerAlias = [](const TSharedPtr<FJsonObject>& Ref) -> FString
    {
        FString Kind;
        Ref->TryGetStringField(TEXT("kind"), Kind);
        const TSharedPtr<FJsonObject>* Owner = nullptr;
        FString Alias;
        if (Kind == TEXT("member") && Ref->TryGetObjectField(TEXT("object"), Owner) && Owner != nullptr) ReadLocalAlias(*Owner, Alias);
        return Alias;
    };
    const FString Alias = MemberOwnerAlias(InputRef);
    if (Alias.IsEmpty() || MemberOwnerAlias(OutputRef) != Alias)
    {
        AddPatchError(State, TEXT("validation.insert_invalid"), TEXT("insert input and output must be members of the same unmaterialized Node alias."), TEXT("insert"));
        return false;
    }
    UEdGraphPin* From = nullptr;
    UEdGraphPin* To = nullptr;
    if (!ResolvePinRef(State, FromRef, TEXT("insert"), From) || !ResolvePinRef(State, ToRef, TEXT("insert"), To)) return false;
    if (!PinsAreLinked(From, To))
    {
        AddPatchError(State, TEXT("resolution.edge_not_found"), TEXT("insert requires one exact existing Edge to replace."), TEXT("insert"));
        return false;
    }
    UEdGraphNode* NewNode = MaterializeDefinition(State, Alias, TEXT("insert"));
    if (NewNode == nullptr) return false;
    UEdGraphPin* Input = nullptr;
    UEdGraphPin* Output = nullptr;
    if (!ResolvePinRef(State, InputRef, TEXT("insert"), Input) || !ResolvePinRef(State, OutputRef, TEXT("insert"), Output)) return false;
    FString Error;
    if (!ValidateConnection(From, Input, Error) || !ValidateConnection(Output, To, Error))
    {
        AddPatchError(State, TEXT("resolution.pin_not_connectable"), Error, TEXT("insert"));
        return false;
    }
    if (State.bApply)
    {
        const UEdGraphSchema* Schema = From->GetSchema();
        if (Schema == nullptr) return false;
        Schema->BreakSinglePinLink(From, To);
        if (!ApplyConnect(State, From, Input, TEXT("insert")) || !ApplyConnect(State, Output, To, TEXT("insert")))
        {
            // All compatibility checks ran before the old Edge was broken. Restore it if an
            // unexpected native apply failure still occurs.
            Schema->TryCreateConnection(From, To);
            if (NewNode->CanUserDeleteNode()) FBlueprintEditorUtils::RemoveNode(State.Target.Blueprint, NewNode, true);
            return false;
        }
    }
    return true;
}

TArray<UEdGraphPin*> DifferencePins(const UEdGraphNode* Node, const TSet<UEdGraphPin*>& Before)
{
    TArray<UEdGraphPin*> Result;
    if (Node != nullptr)
    {
        for (UEdGraphPin* Pin : Node->Pins)
        {
            if (Pin != nullptr && !Before.Contains(Pin)) Result.Add(Pin);
        }
    }
    return Result;
}

bool BindInvokeOutputs(
    FPatchState& State,
    const TSharedPtr<FJsonObject>& Operation,
    const TArray<UEdGraphPin*>& Pins,
    UEdGraphNode* NodeOutput,
    const FString& OperationName)
{
    const TArray<TSharedPtr<FJsonValue>>* Outputs = nullptr;
    if (!Operation->TryGetArrayField(TEXT("outputs"), Outputs) || Outputs == nullptr) return true;
    TSet<UEdGraphPin*> UsedPins;
    for (const TSharedPtr<FJsonValue>& Value : *Outputs)
    {
        TSharedPtr<FJsonObject> Output;
        if (!JsonObjectValue(Value, Output)) return false;
        FString Alias;
        FString Selector;
        Output->TryGetStringField(TEXT("alias"), Alias);
        Output->TryGetStringField(TEXT("selector"), Selector);
        if (State.LocalNodes.Contains(Alias) || State.LocalPins.Contains(Alias) || State.Definitions.Contains(Alias))
        {
            AddPatchError(State, TEXT("validation.duplicate_alias"), TEXT("Invoke output alias is already defined."), OperationName, Alias);
            return false;
        }

        if (NodeOutput != nullptr && (Selector == TEXT("result") || (Selector.IsEmpty() && Pins.IsEmpty())))
        {
            State.LocalNodes.Add(Alias, NodeOutput);
            if (State.bApply) State.ResolvedRefs->SetStringField(Alias, NodeId(NodeOutput));
            continue;
        }

        UEdGraphPin* Match = nullptr;
        if (Selector.IsEmpty() || Selector == TEXT("pin") || Selector == TEXT("parent"))
        {
            for (UEdGraphPin* Pin : Pins)
            {
                if (Pin != nullptr && !UsedPins.Contains(Pin)) { Match = Pin; break; }
            }
        }
        else if (Selector == TEXT("key") || Selector == TEXT("value") || Selector.StartsWith(TEXT("subpins.")))
        {
            const FString Key = Selector.StartsWith(TEXT("subpins.")) ? Selector.Mid(8) : Selector;
            for (UEdGraphPin* Pin : Pins)
            {
                if (Pin == nullptr || UsedPins.Contains(Pin)) continue;
                const FString Name = Pin->PinName.ToString();
                const bool bKeyMatch = Selector == TEXT("key") ? Name.Contains(TEXT("Key"), ESearchCase::IgnoreCase) :
                    Selector == TEXT("value") ? Name.Contains(TEXT("Value"), ESearchCase::IgnoreCase) :
                    (Name == Key || Name.EndsWith(TEXT("_") + Key) || Name.EndsWith(Key));
                if (bKeyMatch) { Match = Pin; break; }
            }
        }
        else if (Selector == TEXT("input") || Selector == TEXT("output"))
        {
            const EEdGraphPinDirection Direction = Selector == TEXT("input") ? EGPD_Input : EGPD_Output;
            for (UEdGraphPin* Pin : Pins)
            {
                if (Pin != nullptr && Pin->Direction == Direction && !UsedPins.Contains(Pin)) { Match = Pin; break; }
            }
        }
        if (Match == nullptr)
        {
            AddPatchError(State, TEXT("resolution.invoke_output_not_found"), TEXT("Invoke selector did not resolve one primary output."), OperationName, Selector);
            return false;
        }
        UsedPins.Add(Match);
        State.LocalPins.Add(Alias, Match);
        if (State.bApply) State.ResolvedRefs->SetStringField(Alias, PinId(Match));
    }
    return true;
}

struct FTimelineTrackRef
{
    FTTTrackBase::ETrackType Type = FTTTrackBase::TT_Event;
    int32 ArrayIndex = INDEX_NONE;
    int32 DisplayIndex = INDEX_NONE;
    FTTTrackBase* Base = nullptr;
    UCurveBase* Curve = nullptr;
};

bool FindTimelineTrack(UTimelineTemplate* Timeline, const FName Name, FTimelineTrackRef& Out, FString& OutError)
{
    Out = {};
    int32 Matches = 0;
    auto Accept = [&](FTTTrackBase::ETrackType Type, int32 Index, FTTTrackBase& Base, UCurveBase* Curve)
    {
        if (Base.GetTrackName() != Name) return;
        ++Matches;
        Out.Type = Type;
        Out.ArrayIndex = Index;
        Out.Base = &Base;
        Out.Curve = Curve;
    };
    if (Timeline != nullptr)
    {
        for (int32 Index = 0; Index < Timeline->EventTracks.Num(); ++Index) Accept(FTTTrackBase::TT_Event, Index, Timeline->EventTracks[Index], Timeline->EventTracks[Index].CurveKeys);
        for (int32 Index = 0; Index < Timeline->FloatTracks.Num(); ++Index) Accept(FTTTrackBase::TT_FloatInterp, Index, Timeline->FloatTracks[Index], Timeline->FloatTracks[Index].CurveFloat);
        for (int32 Index = 0; Index < Timeline->VectorTracks.Num(); ++Index) Accept(FTTTrackBase::TT_VectorInterp, Index, Timeline->VectorTracks[Index], Timeline->VectorTracks[Index].CurveVector);
        for (int32 Index = 0; Index < Timeline->LinearColorTracks.Num(); ++Index) Accept(FTTTrackBase::TT_LinearColorInterp, Index, Timeline->LinearColorTracks[Index], Timeline->LinearColorTracks[Index].CurveLinearColor);
        for (int32 Display = 0; Display < Timeline->GetNumDisplayTracks(); ++Display)
        {
            const FTTTrackId Id = Timeline->GetDisplayTrackId(Display);
            if (Id.TrackType == static_cast<int32>(Out.Type) && Id.TrackIndex == Out.ArrayIndex) Out.DisplayIndex = Display;
        }
    }
    if (Matches == 1 && Out.Base != nullptr && Out.DisplayIndex != INDEX_NONE) return true;
    OutError = Matches > 1 ? TEXT("Timeline TrackName is ambiguous in invalid native state.") : TEXT("Timeline TrackName was not found in native display order.");
    return false;
}

bool TimelineTrackNameAvailable(UTimelineTemplate* Timeline, UK2Node_Timeline* Node, const FName Name, FString& OutError)
{
    if (Name.IsNone()) { OutError = TEXT("TrackName cannot be empty."); return false; }
    if (Timeline == nullptr || !Timeline->IsNewTrackNameValid(Name)) { OutError = TEXT("TrackName is already used by this Timeline."); return false; }
    if (Node != nullptr && Node->FindPin(Name) != nullptr) { OutError = TEXT("TrackName collides with a fixed Timeline Pin."); return false; }
    return true;
}

FRichCurve* TimelineRichCurve(const FTimelineTrackRef& Track, const FString& Channel, FString& OutError)
{
    if (Track.Base == nullptr || Track.Curve == nullptr) { OutError = TEXT("Timeline Track has no Curve."); return nullptr; }
    if (Track.Base->bIsExternalCurve) { OutError = TEXT("External Curve Keys are not mutated through a Timeline Node."); return nullptr; }
    if (Track.Type == FTTTrackBase::TT_Event || Track.Type == FTTTrackBase::TT_FloatInterp)
    {
        if (!Channel.IsEmpty()) { OutError = TEXT("Event and Float Tracks do not accept Channel."); return nullptr; }
        return &CastChecked<UCurveFloat>(Track.Curve)->FloatCurve;
    }
    if (Track.Type == FTTTrackBase::TT_VectorInterp)
    {
        const int32 Index = Channel == TEXT("X") ? 0 : Channel == TEXT("Y") ? 1 : Channel == TEXT("Z") ? 2 : INDEX_NONE;
        if (Index == INDEX_NONE) { OutError = TEXT("Vector Track requires Channel X, Y, or Z."); return nullptr; }
        return &CastChecked<UCurveVector>(Track.Curve)->FloatCurves[Index];
    }
    const int32 Index = Channel == TEXT("R") ? 0 : Channel == TEXT("G") ? 1 : Channel == TEXT("B") ? 2 : Channel == TEXT("A") ? 3 : INDEX_NONE;
    if (Index == INDEX_NONE) { OutError = TEXT("Linear Color Track requires Channel R, G, B, or A."); return nullptr; }
    return &CastChecked<UCurveLinearColor>(Track.Curve)->FloatCurves[Index];
}

bool ReadTimelineTrackArg(const TSharedPtr<FJsonObject>* Args, UTimelineTemplate* Timeline, FTimelineTrackRef& Out, FString& OutError)
{
    FString TrackName;
    if (!ReadArgText(Args, TEXT("TrackName"), TrackName)) { OutError = TEXT("Timeline Operation requires TrackName."); return false; }
    return FindTimelineTrack(Timeline, FName(*TrackName), Out, OutError);
}

bool ApplyRichCurveKeyFields(const TSharedPtr<FJsonObject>* Args, FRichCurveKey& Key, const bool bRequireValue, FString& OutError)
{
    double Number = 0.0;
    if (ReadArgNumber(Args, TEXT("Value"), Number)) Key.Value = static_cast<float>(Number);
    else if (Args != nullptr && (*Args).IsValid() && (*Args)->HasField(TEXT("Value"))) { OutError = TEXT("Value must be numeric."); return false; }
    else if (bRequireValue) { OutError = TEXT("Non-event Timeline Key requires Value."); return false; }
    FString Text;
    if (ReadArgText(Args, TEXT("InterpMode"), Text))
    {
        const int64 Value = StaticEnum<ERichCurveInterpMode>()->GetValueByNameString(Text);
        if (Value == INDEX_NONE) { OutError = TEXT("InterpMode is not a native ERichCurveInterpMode value."); return false; }
        Key.InterpMode = static_cast<ERichCurveInterpMode>(Value);
    }
    if (ReadArgText(Args, TEXT("TangentMode"), Text))
    {
        const int64 Value = StaticEnum<ERichCurveTangentMode>()->GetValueByNameString(Text);
        if (Value == INDEX_NONE) { OutError = TEXT("TangentMode is not a native ERichCurveTangentMode value."); return false; }
        Key.TangentMode = static_cast<ERichCurveTangentMode>(Value);
    }
    if (ReadArgText(Args, TEXT("TangentWeightMode"), Text))
    {
        const int64 Value = StaticEnum<ERichCurveTangentWeightMode>()->GetValueByNameString(Text);
        if (Value == INDEX_NONE) { OutError = TEXT("TangentWeightMode is not a native ERichCurveTangentWeightMode value."); return false; }
        Key.TangentWeightMode = static_cast<ERichCurveTangentWeightMode>(Value);
    }
    for (const TCHAR* Field : {TEXT("ArriveTangent"), TEXT("ArriveTangentWeight"), TEXT("LeaveTangent"), TEXT("LeaveTangentWeight")})
    {
        if (ReadArgNumber(Args, Field, Number))
        {
            if (FCString::Strcmp(Field, TEXT("ArriveTangent")) == 0) Key.ArriveTangent = static_cast<float>(Number);
            else if (FCString::Strcmp(Field, TEXT("ArriveTangentWeight")) == 0) Key.ArriveTangentWeight = static_cast<float>(Number);
            else if (FCString::Strcmp(Field, TEXT("LeaveTangent")) == 0) Key.LeaveTangent = static_cast<float>(Number);
            else Key.LeaveTangentWeight = static_cast<float>(Number);
        }
        else if (Args != nullptr && (*Args).IsValid() && (*Args)->HasField(Field))
        {
            OutError = FString::Printf(TEXT("%s must be numeric."), Field);
            return false;
        }
    }
    return true;
}

bool RichCurveKeyEqual(const FRichCurveKey& A, const FRichCurveKey& B)
{
    return A.Time == B.Time && A.Value == B.Value && A.InterpMode == B.InterpMode
        && A.TangentMode == B.TangentMode && A.TangentWeightMode == B.TangentWeightMode
        && A.ArriveTangent == B.ArriveTangent && A.ArriveTangentWeight == B.ArriveTangentWeight
        && A.LeaveTangent == B.LeaveTangent && A.LeaveTangentWeight == B.LeaveTangentWeight;
}

bool ExactKeyHandle(FRichCurve& Curve, const float Time, FKeyHandle& Out, FString& OutError, const bool bMustExist)
{
    int32 Matches = 0;
    for (const FRichCurveKey& Key : Curve.GetConstRefOfKeys()) if (Key.Time == Time) ++Matches;
    if (Matches > 1) { OutError = TEXT("Multiple native Keys occupy the selected Time."); return false; }
    if ((Matches == 0) == bMustExist)
    {
        OutError = bMustExist ? TEXT("Timeline Key was not found at the exact Time.") : TEXT("A Timeline Key already occupies the exact Time.");
        return false;
    }
    Out = Matches == 1 ? Curve.FindKey(Time, 0.0f) : FKeyHandle::Invalid();
    return true;
}

void CopyRichCurveKeys(const FRichCurve& Source, FRichCurve& Destination)
{
    Destination.SetKeys(Source.GetCopyOfKeys());
}

bool ExecuteTimelineInvoke(
    FPatchState& State,
    UK2Node_Timeline* Node,
    const TSharedPtr<FJsonObject>& Operation,
    const FString& Name)
{
    FString PairError;
    UTimelineTemplate* Timeline = FindTimelineTemplate(State.Target.Blueprint, Node, &PairError);
    if (Timeline == nullptr)
    {
        AddPatchError(State, TEXT("validation.timeline_inconsistent"), PairError, Name, NodeId(Node));
        return false;
    }
    if (!ValidateTimelineStructure(Node, Timeline, PairError))
    {
        AddPatchError(State, TEXT("validation.timeline_inconsistent"), PairError, Name, NodeId(Node));
        return false;
    }
    const TSharedPtr<FJsonObject>* Args = nullptr;
    Operation->TryGetObjectField(TEXT("args"), Args);
    UObject* CurveOuter = State.Target.Blueprint != nullptr ? static_cast<UObject*>(State.Target.Blueprint->GeneratedClass.Get()) : nullptr;
    if (CurveOuter == nullptr) CurveOuter = State.Target.Blueprint;

    if (Name == TEXT("AddFloatTrack") || Name == TEXT("AddVectorTrack") || Name == TEXT("AddEventTrack") || Name == TEXT("AddLinearColorTrack"))
    {
        FString TrackNameText;
        FString Error;
        if (!ReadArgText(Args, TEXT("TrackName"), TrackNameText) || !TimelineTrackNameAvailable(Timeline, Node, FName(*TrackNameText), Error))
        {
            AddPatchError(State, TEXT("validation.timeline_track_invalid"), Error.IsEmpty() ? TEXT("TrackName is required.") : Error, Name, TrackNameText);
            return false;
        }
        Node->Modify();
        Timeline->Modify();
        FTTTrackId Id;
        if (Name == TEXT("AddEventTrack"))
        {
            Id = FTTTrackId(FTTTrackBase::TT_Event, Timeline->EventTracks.Num());
            FTTEventTrack Track;
            Track.SetTrackName(FName(*TrackNameText), Timeline);
            Track.CurveKeys = NewObject<UCurveFloat>(CurveOuter, NAME_None, RF_Public | RF_Transactional);
            Track.CurveKeys->bIsEventCurve = true;
            Timeline->EventTracks.Add(Track);
        }
        else if (Name == TEXT("AddFloatTrack"))
        {
            Id = FTTTrackId(FTTTrackBase::TT_FloatInterp, Timeline->FloatTracks.Num());
            FTTFloatTrack Track;
            Track.SetTrackName(FName(*TrackNameText), Timeline);
            Track.CurveFloat = NewObject<UCurveFloat>(CurveOuter, NAME_None, RF_Public | RF_Transactional);
            Timeline->FloatTracks.Add(Track);
        }
        else if (Name == TEXT("AddVectorTrack"))
        {
            Id = FTTTrackId(FTTTrackBase::TT_VectorInterp, Timeline->VectorTracks.Num());
            FTTVectorTrack Track;
            Track.SetTrackName(FName(*TrackNameText), Timeline);
            Track.CurveVector = NewObject<UCurveVector>(CurveOuter, NAME_None, RF_Public | RF_Transactional);
            Timeline->VectorTracks.Add(Track);
        }
        else
        {
            Id = FTTTrackId(FTTTrackBase::TT_LinearColorInterp, Timeline->LinearColorTracks.Num());
            FTTLinearColorTrack Track;
            Track.SetTrackName(FName(*TrackNameText), Timeline);
            Track.CurveLinearColor = NewObject<UCurveLinearColor>(CurveOuter, NAME_None, RF_Public | RF_Transactional);
            Timeline->LinearColorTracks.Add(Track);
        }
        Timeline->AddDisplayTrack(Id);
        Node->ReconstructNode();
        UEdGraphPin* Created = FindUniqueTimelineTrackPin(Node, FName(*TrackNameText));
        if (Created == nullptr) { AddPatchError(State, TEXT("validation.timeline_edit_failed"), TEXT("Timeline reconstruction did not create the Track Pin."), Name); return false; }
        State.TouchedNodes.Add(Node);
        State.TouchedPins.Add(Created);
        ++State.ChangedOps;
        return BindInvokeOutputs(State, Operation, {Created}, nullptr, Name);
    }

    if (Name == TEXT("AddTrackFromCurve"))
    {
        FString CurvePath;
        if (!ReadArgText(Args, TEXT("Curve"), CurvePath)) { AddPatchError(State, TEXT("validation.operation_arguments_invalid"), TEXT("AddTrackFromCurve requires Curve."), Name); return false; }
        UCurveBase* Curve = LoadObject<UCurveBase>(nullptr, *CurvePath);
        FString Error;
        if (Curve == nullptr || !TimelineTrackNameAvailable(Timeline, Node, Curve != nullptr ? Curve->GetFName() : NAME_None, Error))
        {
            AddPatchError(State, Curve == nullptr ? TEXT("resolution.curve_not_found") : TEXT("validation.timeline_track_invalid"), Curve == nullptr ? TEXT("Curve Asset was not found.") : Error, Name, CurvePath);
            return false;
        }
        Node->Modify();
        Timeline->Modify();
        FTTTrackId Id;
        if (UCurveFloat* Float = Cast<UCurveFloat>(Curve))
        {
            if (Float->bIsEventCurve)
            {
                Id = FTTTrackId(FTTTrackBase::TT_Event, Timeline->EventTracks.Num());
                FTTEventTrack Track; Track.SetTrackName(Curve->GetFName(), Timeline); Track.CurveKeys = Float; Track.bIsExternalCurve = true; Timeline->EventTracks.Add(Track);
            }
            else
            {
                Id = FTTTrackId(FTTTrackBase::TT_FloatInterp, Timeline->FloatTracks.Num());
                FTTFloatTrack Track; Track.SetTrackName(Curve->GetFName(), Timeline); Track.CurveFloat = Float; Track.bIsExternalCurve = true; Timeline->FloatTracks.Add(Track);
            }
        }
        else if (UCurveVector* Vector = Cast<UCurveVector>(Curve))
        {
            Id = FTTTrackId(FTTTrackBase::TT_VectorInterp, Timeline->VectorTracks.Num());
            FTTVectorTrack Track; Track.SetTrackName(Curve->GetFName(), Timeline); Track.CurveVector = Vector; Track.bIsExternalCurve = true; Timeline->VectorTracks.Add(Track);
        }
        else if (UCurveLinearColor* Color = Cast<UCurveLinearColor>(Curve))
        {
            Id = FTTTrackId(FTTTrackBase::TT_LinearColorInterp, Timeline->LinearColorTracks.Num());
            FTTLinearColorTrack Track; Track.SetTrackName(Curve->GetFName(), Timeline); Track.CurveLinearColor = Color; Track.bIsExternalCurve = true; Timeline->LinearColorTracks.Add(Track);
        }
        else { AddPatchError(State, TEXT("validation.curve_type_invalid"), TEXT("Curve must be UCurveFloat, UCurveVector, or UCurveLinearColor."), Name, CurvePath); return false; }
        Timeline->AddDisplayTrack(Id);
        Node->ReconstructNode();
        UEdGraphPin* Created = FindUniqueTimelineTrackPin(Node, Curve->GetFName());
        State.TouchedNodes.Add(Node);
        if (Created != nullptr) State.TouchedPins.Add(Created);
        ++State.ChangedOps;
        if (Created == nullptr)
        {
            AddPatchError(State, TEXT("validation.timeline_edit_failed"), TEXT("Timeline reconstruction did not create the external Track Pin."), Name);
            return false;
        }
        return BindInvokeOutputs(State, Operation, {Created}, nullptr, Name);
    }

    if (Name == TEXT("RenameTrack"))
    {
        FTimelineTrackRef Track;
        FString Error;
        FString TrackNameText;
        FString NewNameText;
        if (!ReadArgText(Args, TEXT("TrackName"), TrackNameText) || !ReadArgText(Args, TEXT("NewName"), NewNameText))
        {
            AddPatchError(State, TEXT("validation.operation_arguments_invalid"), TEXT("RenameTrack requires TrackName and NewName."), Name);
            return false;
        }
        if (!FindTimelineTrack(Timeline, FName(*TrackNameText), Track, Error))
        {
            AddPatchError(State, TEXT("resolution.timeline_track_not_found"), Error, Name, TrackNameText);
            return false;
        }
        if (Track.Base->GetTrackName().ToString() == NewNameText) return BindInvokeOutputs(State, Operation, {}, nullptr, Name);
        if (!TimelineTrackNameAvailable(Timeline, Node, FName(*NewNameText), Error)) { AddPatchError(State, TEXT("validation.timeline_track_invalid"), Error, Name, NewNameText); return false; }
        UEdGraphPin* TrackPin = FindUniqueTimelineTrackPin(Node, Track.Base->GetTrackName());
        if (TrackPin == nullptr) { AddPatchError(State, TEXT("validation.timeline_inconsistent"), TEXT("Timeline Track has no unique generated output Pin."), Name); return false; }
        Node->Modify(); Timeline->Modify(); TrackPin->Modify();
        TrackPin->PinName = FName(*NewNameText);
        Track.Base->SetTrackName(FName(*NewNameText), Timeline);
        State.TouchedNodes.Add(Node); State.TouchedPins.Add(TrackPin); ++State.ChangedOps;
        return BindInvokeOutputs(State, Operation, {}, nullptr, Name);
    }

    if (Name == TEXT("MoveTrack"))
    {
        FTimelineTrackRef Track;
        FString Error;
        if (!ReadTimelineTrackArg(Args, Timeline, Track, Error)) { AddPatchError(State, TEXT("resolution.timeline_track_not_found"), Error, Name); return false; }
        FString Before;
        FString After;
        const bool bBefore = ReadArgText(Args, TEXT("Before"), Before);
        const bool bAfter = ReadArgText(Args, TEXT("After"), After);
        if (bBefore == bAfter) { AddPatchError(State, TEXT("validation.operation_arguments_invalid"), TEXT("MoveTrack requires exactly one of Before or After."), Name); return false; }
        FTimelineTrackRef Anchor;
        if (!FindTimelineTrack(Timeline, FName(*(bBefore ? Before : After)), Anchor, Error)) { AddPatchError(State, TEXT("resolution.timeline_track_not_found"), Error, Name); return false; }
        int32 Desired = bBefore ? Anchor.DisplayIndex : Anchor.DisplayIndex + 1;
        if (Track.DisplayIndex < Desired) --Desired;
        if (Desired == Track.DisplayIndex) return BindInvokeOutputs(State, Operation, {}, nullptr, Name);
        Node->Modify(); Timeline->Modify();
        int32 Current = Track.DisplayIndex;
        while (Current < Desired) { Timeline->MoveDisplayTrack(Current, 1); ++Current; }
        while (Current > Desired) { Timeline->MoveDisplayTrack(Current, -1); --Current; }
        Node->ReconstructNode();
        State.TouchedNodes.Add(Node); ++State.ChangedOps;
        return BindInvokeOutputs(State, Operation, {}, nullptr, Name);
    }

    if (Name == TEXT("RemoveTrack"))
    {
        FTimelineTrackRef Track;
        FString Error;
        if (!ReadTimelineTrackArg(Args, Timeline, Track, Error)) { AddPatchError(State, TEXT("resolution.timeline_track_not_found"), Error, Name); return false; }
        Node->Modify(); Timeline->Modify();
        Timeline->RemoveDisplayTrack(Track.DisplayIndex);
        if (Track.Type == FTTTrackBase::TT_Event) Timeline->EventTracks.RemoveAt(Track.ArrayIndex);
        else if (Track.Type == FTTTrackBase::TT_FloatInterp) Timeline->FloatTracks.RemoveAt(Track.ArrayIndex);
        else if (Track.Type == FTTTrackBase::TT_VectorInterp) Timeline->VectorTracks.RemoveAt(Track.ArrayIndex);
        else Timeline->LinearColorTracks.RemoveAt(Track.ArrayIndex);
        Node->ReconstructNode();
        State.TouchedNodes.Add(Node); ++State.ChangedOps;
        return BindInvokeOutputs(State, Operation, {}, nullptr, Name);
    }

    if (Name == TEXT("AddKey") || Name == TEXT("SetKey") || Name == TEXT("RemoveKey"))
    {
        FTimelineTrackRef Track;
        FString Error;
        if (!ReadTimelineTrackArg(Args, Timeline, Track, Error)) { AddPatchError(State, TEXT("resolution.timeline_track_not_found"), Error, Name); return false; }
        FString Channel;
        ReadArgText(Args, TEXT("Channel"), Channel);
        FRichCurve* Curve = TimelineRichCurve(Track, Channel, Error);
        double TimeNumber = 0.0;
        if (Curve == nullptr || !ReadArgNumber(Args, TEXT("Time"), TimeNumber))
        {
            AddPatchError(State, TEXT("validation.operation_arguments_invalid"), Error.IsEmpty() ? TEXT("Timeline Key Operation requires numeric Time.") : Error, Name);
            return false;
        }
        const float Time = static_cast<float>(TimeNumber);
        FKeyHandle Handle;
        if (!ExactKeyHandle(*Curve, Time, Handle, Error, Name != TEXT("AddKey")))
        {
            AddPatchError(State, TEXT("validation.timeline_key_conflict"), Error, Name, FString::SanitizeFloat(Time));
            return false;
        }
        Track.Curve->Modify();
        if (Name == TEXT("AddKey"))
        {
            FRichCurveKey NewKey(Time, 0.0f);
            if (!ApplyRichCurveKeyFields(Args, NewKey, Track.Type != FTTTrackBase::TT_Event, Error)) { AddPatchError(State, TEXT("validation.timeline_key_invalid"), Error, Name); return false; }
            Handle = Curve->AddKey(NewKey.Time, NewKey.Value);
            Curve->GetKey(Handle) = NewKey;
        }
        else if (Name == TEXT("SetKey"))
        {
            const FRichCurveKey OldKey = Curve->GetKey(Handle);
            FRichCurveKey NewKey = OldKey;
            if (!ApplyRichCurveKeyFields(Args, NewKey, false, Error)) { AddPatchError(State, TEXT("validation.timeline_key_invalid"), Error, Name); return false; }
            double NewTimeNumber = 0.0;
            if (ReadArgNumber(Args, TEXT("NewTime"), NewTimeNumber))
            {
                const float NewTime = static_cast<float>(NewTimeNumber);
                if (NewTime != Time)
                {
                    FKeyHandle Occupied;
                    if (!ExactKeyHandle(*Curve, NewTime, Occupied, Error, false)) { AddPatchError(State, TEXT("validation.timeline_key_conflict"), Error, Name); return false; }
                    Curve->SetKeyTime(Handle, NewTime);
                    Handle = Curve->FindKey(NewTime, 0.0f);
                    NewKey.Time = NewTime;
                }
            }
            else if (Args != nullptr && (*Args).IsValid() && (*Args)->HasField(TEXT("NewTime")))
            {
                AddPatchError(State, TEXT("validation.timeline_key_invalid"), TEXT("NewTime must be numeric."), Name);
                return false;
            }
            if (RichCurveKeyEqual(OldKey, NewKey)) return BindInvokeOutputs(State, Operation, {}, nullptr, Name);
            Curve->GetKey(Handle) = NewKey;
        }
        else
        {
            Curve->DeleteKey(Handle);
        }
        Curve->AutoSetTangents();
        State.TouchedNodes.Add(Node); ++State.ChangedOps;
        return BindInvokeOutputs(State, Operation, {}, nullptr, Name);
    }

    if (Name == TEXT("UseExternalCurve") || Name == TEXT("UseInternalCurve"))
    {
        FTimelineTrackRef Track;
        FString Error;
        if (!ReadTimelineTrackArg(Args, Timeline, Track, Error)) { AddPatchError(State, TEXT("resolution.timeline_track_not_found"), Error, Name); return false; }
        Node->Modify(); Timeline->Modify();
        if (Name == TEXT("UseExternalCurve"))
        {
            FString CurvePath;
            if (!ReadArgText(Args, TEXT("Curve"), CurvePath)) { AddPatchError(State, TEXT("validation.operation_arguments_invalid"), TEXT("UseExternalCurve requires Curve."), Name); return false; }
            UCurveBase* Curve = LoadObject<UCurveBase>(nullptr, *CurvePath);
            const bool bCompatible = (Track.Type == FTTTrackBase::TT_Event || Track.Type == FTTTrackBase::TT_FloatInterp) ? Curve != nullptr && Curve->IsA<UCurveFloat>()
                : Track.Type == FTTTrackBase::TT_VectorInterp ? Curve != nullptr && Curve->IsA<UCurveVector>() : Curve != nullptr && Curve->IsA<UCurveLinearColor>();
            if (!bCompatible) { AddPatchError(State, Curve == nullptr ? TEXT("resolution.curve_not_found") : TEXT("validation.curve_type_invalid"), TEXT("External Curve Class does not match the native Track kind."), Name, CurvePath); return false; }
            if (Track.Base->bIsExternalCurve && Track.Curve == Curve) return BindInvokeOutputs(State, Operation, {}, nullptr, Name);
            if (Track.Type == FTTTrackBase::TT_Event) Timeline->EventTracks[Track.ArrayIndex].CurveKeys = CastChecked<UCurveFloat>(Curve);
            else if (Track.Type == FTTTrackBase::TT_FloatInterp) Timeline->FloatTracks[Track.ArrayIndex].CurveFloat = CastChecked<UCurveFloat>(Curve);
            else if (Track.Type == FTTTrackBase::TT_VectorInterp) Timeline->VectorTracks[Track.ArrayIndex].CurveVector = CastChecked<UCurveVector>(Curve);
            else Timeline->LinearColorTracks[Track.ArrayIndex].CurveLinearColor = CastChecked<UCurveLinearColor>(Curve);
            Track.Base->bIsExternalCurve = true;
        }
        else
        {
            if (!Track.Base->bIsExternalCurve) { AddPatchError(State, TEXT("capability.operation_unavailable"), TEXT("UseInternalCurve is available only for an external Track."), Name); return false; }
            if (Track.Type == FTTTrackBase::TT_Event)
            {
                UCurveFloat* Source = Timeline->EventTracks[Track.ArrayIndex].CurveKeys;
                UCurveFloat* Dest = NewObject<UCurveFloat>(CurveOuter, NAME_None, RF_Public | RF_Transactional);
                if (Source != nullptr) CopyRichCurveKeys(Source->FloatCurve, Dest->FloatCurve);
                Dest->bIsEventCurve = true;
                Timeline->EventTracks[Track.ArrayIndex].CurveKeys = Dest;
            }
            else if (Track.Type == FTTTrackBase::TT_FloatInterp)
            {
                UCurveFloat* Source = Timeline->FloatTracks[Track.ArrayIndex].CurveFloat;
                UCurveFloat* Dest = NewObject<UCurveFloat>(CurveOuter, NAME_None, RF_Public | RF_Transactional);
                if (Source != nullptr) CopyRichCurveKeys(Source->FloatCurve, Dest->FloatCurve);
                Timeline->FloatTracks[Track.ArrayIndex].CurveFloat = Dest;
            }
            else if (Track.Type == FTTTrackBase::TT_VectorInterp)
            {
                UCurveVector* Source = Timeline->VectorTracks[Track.ArrayIndex].CurveVector;
                UCurveVector* Dest = NewObject<UCurveVector>(CurveOuter, NAME_None, RF_Public | RF_Transactional);
                if (Source != nullptr) for (int32 Index = 0; Index < 3; ++Index) CopyRichCurveKeys(Source->FloatCurves[Index], Dest->FloatCurves[Index]);
                Timeline->VectorTracks[Track.ArrayIndex].CurveVector = Dest;
            }
            else
            {
                UCurveLinearColor* Source = Timeline->LinearColorTracks[Track.ArrayIndex].CurveLinearColor;
                UCurveLinearColor* Dest = NewObject<UCurveLinearColor>(CurveOuter, NAME_None, RF_Public | RF_Transactional);
                if (Source != nullptr) for (int32 Index = 0; Index < 4; ++Index) CopyRichCurveKeys(Source->FloatCurves[Index], Dest->FloatCurves[Index]);
                Timeline->LinearColorTracks[Track.ArrayIndex].CurveLinearColor = Dest;
            }
            Track.Base->bIsExternalCurve = false;
        }
        State.TouchedNodes.Add(Node); ++State.ChangedOps;
        return BindInvokeOutputs(State, Operation, {}, nullptr, Name);
    }

    if (Name == TEXT("Duplicate"))
    {
        if (!Node->CanDuplicateNode()) { AddPatchError(State, TEXT("capability.operation_unavailable"), TEXT("UE marks this Timeline Node as not duplicatable."), Name); return false; }
        Node->Modify();
        Node->PrepareForCopying();
        TSet<UObject*> ExportNodes;
        ExportNodes.Add(Node);
        FString Exported;
        FEdGraphUtilities::ExportNodesToText(ExportNodes, Exported);
        TSet<UEdGraphNode*> Imported;
        FEdGraphUtilities::ImportNodesFromText(Node->GetGraph(), Exported, Imported);
        UK2Node_Timeline* Copy = nullptr;
        for (UEdGraphNode* ImportedNode : Imported)
        {
            if (UK2Node_Timeline* Candidate = Cast<UK2Node_Timeline>(ImportedNode))
            {
                if (Copy != nullptr) { AddPatchError(State, TEXT("validation.timeline_edit_failed"), TEXT("Native Timeline duplication produced more than one Timeline Node."), Name); return false; }
                Copy = Candidate;
            }
        }
        if (Copy == nullptr) { AddPatchError(State, TEXT("validation.timeline_edit_failed"), TEXT("Native Timeline duplication produced no Timeline Node."), Name); return false; }
        Copy->CreateNewGuid();
        Copy->NodePosX = Node->NodePosX + 320;
        Copy->NodePosY = Node->NodePosY;
        State.TouchedNodes.Add(Copy);
        ++State.ChangedOps;
        return BindInvokeOutputs(State, Operation, {}, Copy, Name);
    }

    return false;
}

bool ExecuteNodeInvoke(
    FPatchState& State,
    UEdGraphNode* OriginalNode,
    UEdGraphPin* OriginalPin,
    const TSharedPtr<FJsonObject>& Operation,
    const FString& Name)
{
    UEdGraphNode* Node = OriginalNode;
    UEdGraphPin* Pin = OriginalPin;
    if (Node == nullptr) return false;

    if (UK2Node_Timeline* Timeline = Cast<UK2Node_Timeline>(Node))
    {
        static const TSet<FString> TimelineOperations = {
            TEXT("AddFloatTrack"), TEXT("AddVectorTrack"), TEXT("AddEventTrack"), TEXT("AddLinearColorTrack"),
            TEXT("AddTrackFromCurve"), TEXT("RenameTrack"), TEXT("MoveTrack"), TEXT("RemoveTrack"),
            TEXT("AddKey"), TEXT("SetKey"), TEXT("RemoveKey"), TEXT("UseExternalCurve"),
            TEXT("UseInternalCurve"), TEXT("Duplicate")
        };
        if (TimelineOperations.Contains(Name)) return ExecuteTimelineInvoke(State, Timeline, Operation, Name);
    }

    TSet<UEdGraphPin*> Before;
    for (UEdGraphPin* Existing : Node->Pins) Before.Add(Existing);
    TArray<UEdGraphPin*> PrimaryPins;
    UEdGraphNode* NodeOutput = nullptr;

    if (Name == TEXT("AddExecutionPin"))
    {
        if (Cast<UK2Node_ExecutionSequence>(Node) != nullptr)
        {
            IK2Node_AddPinInterface* AddPin = Cast<IK2Node_AddPinInterface>(Node);
            if (AddPin == nullptr || !AddPin->CanAddPin()) { AddPatchError(State, TEXT("capability.operation_unavailable"), TEXT("Sequence cannot add another execution Pin."), Name); return false; }
            AddPin->AddInputPin();
        }
        else if (UK2Node_Switch* Switch = Cast<UK2Node_Switch>(Node))
        {
            if (!Switch->SupportsAddPinButton()) { AddPatchError(State, TEXT("capability.operation_unavailable"), TEXT("This Switch kind has no native add-Pin action."), Name); return false; }
            Switch->AddPinToSwitchNode();
        }
        else { AddPatchError(State, TEXT("capability.operation_unavailable"), TEXT("AddExecutionPin is unavailable on this Node."), Name); return false; }
    }
    else if (Name == TEXT("InsertExecutionPinBefore") || Name == TEXT("InsertExecutionPinAfter"))
    {
        UK2Node_ExecutionSequence* Sequence = Cast<UK2Node_ExecutionSequence>(Node);
        if (Sequence == nullptr || Pin == nullptr || Pin->GetOwningNode() != Sequence || Pin->Direction != EGPD_Output || !IsExecPin(Pin))
        {
            AddPatchError(State, TEXT("capability.operation_unavailable"), TEXT("Execution Pin insertion requires one Sequence execution-output Pin."), Name);
            return false;
        }
        Sequence->InsertPinIntoExecutionNode(Pin, Name.EndsWith(TEXT("Before")) ? EPinInsertPosition::Before : EPinInsertPosition::After);
    }
    else if (Name == TEXT("RemoveExecutionPin"))
    {
        if (Pin == nullptr) { AddPatchError(State, TEXT("validation.invalid_target"), TEXT("RemoveExecutionPin requires one Pin."), Name); return false; }
        if (UK2Node_ExecutionSequence* Sequence = Cast<UK2Node_ExecutionSequence>(Node))
        {
            if (Pin->GetOwningNode() != Sequence || Pin->Direction != EGPD_Output || !IsExecPin(Pin) || !Sequence->CanRemoveExecutionPin())
            {
                AddPatchError(State, TEXT("capability.operation_unavailable"), TEXT("Selected Sequence Pin is not removable."), Name);
                return false;
            }
            Sequence->RemovePinFromExecutionNode(Pin);
        }
        else if (UK2Node_Switch* Switch = Cast<UK2Node_Switch>(Node))
        {
            if (Pin->GetOwningNode() != Switch || !Switch->CanRemoveExecutionPin(Pin))
            {
                AddPatchError(State, TEXT("capability.operation_unavailable"), TEXT("Selected Switch Pin is not removable."), Name);
                return false;
            }
            Switch->RemovePinFromSwitchNode(Pin);
        }
        else { AddPatchError(State, TEXT("capability.operation_unavailable"), TEXT("RemoveExecutionPin is unavailable on this Pin."), Name); return false; }
    }
    else if (Name == TEXT("AddOptionPin"))
    {
        UK2Node_Select* Select = Cast<UK2Node_Select>(Node);
        IK2Node_AddPinInterface* AddPin = Select != nullptr ? Cast<IK2Node_AddPinInterface>(Select) : nullptr;
        if (AddPin == nullptr || !AddPin->CanAddPin()) { AddPatchError(State, TEXT("capability.operation_unavailable"), TEXT("Select cannot add another option Pin."), Name); return false; }
        AddPin->AddInputPin();
    }
    else if (Name == TEXT("AddArrayElementPin") || Name == TEXT("AddSetElementPin") || Name == TEXT("AddKeyValuePair"))
    {
        const bool bClassMatches = Name == TEXT("AddArrayElementPin") ? Cast<UK2Node_MakeArray>(Node) != nullptr
            : Name == TEXT("AddSetElementPin") ? Cast<UK2Node_MakeSet>(Node) != nullptr
            : Cast<UK2Node_MakeMap>(Node) != nullptr;
        IK2Node_AddPinInterface* AddPin = bClassMatches ? Cast<IK2Node_AddPinInterface>(Node) : nullptr;
        if (AddPin == nullptr || !AddPin->CanAddPin()) { AddPatchError(State, TEXT("capability.operation_unavailable"), TEXT("This container Node cannot add another native element."), Name); return false; }
        AddPin->AddInputPin();
    }
    else if (Name == TEXT("AddInputPin"))
    {
        const bool bClassMatches = Cast<UK2Node_CommutativeAssociativeBinaryOperator>(Node) != nullptr
            || Cast<UK2Node_PromotableOperator>(Node) != nullptr
            || Cast<UK2Node_DoOnceMultiInput>(Node) != nullptr;
        IK2Node_AddPinInterface* AddPin = bClassMatches ? Cast<IK2Node_AddPinInterface>(Node) : nullptr;
        if (AddPin == nullptr || !AddPin->CanAddPin()) { AddPatchError(State, TEXT("capability.operation_unavailable"), TEXT("This Node cannot add another dynamic Pin."), Name); return false; }
        AddPin->AddInputPin();
    }
    else if (Name == TEXT("RemoveOptionPin"))
    {
        UK2Node_Select* Select = Cast<UK2Node_Select>(Node);
        if (Select == nullptr || !Select->CanRemoveOptionPinToNode()) { AddPatchError(State, TEXT("capability.operation_unavailable"), TEXT("Select has no removable option Pin."), Name); return false; }
        Select->RemoveOptionPinToNode();
    }
    else if (Name == TEXT("RemoveArrayElementPin") || Name == TEXT("RemoveSetElementPin")
        || Name == TEXT("RemoveKeyValuePair") || Name == TEXT("RemoveInputPin"))
    {
        const bool bClassMatches = Name == TEXT("RemoveArrayElementPin") ? Cast<UK2Node_MakeArray>(Node) != nullptr
            : Name == TEXT("RemoveSetElementPin") ? Cast<UK2Node_MakeSet>(Node) != nullptr
            : Name == TEXT("RemoveKeyValuePair") ? Cast<UK2Node_MakeMap>(Node) != nullptr
            : (Cast<UK2Node_CommutativeAssociativeBinaryOperator>(Node) != nullptr
                || Cast<UK2Node_PromotableOperator>(Node) != nullptr
                || Cast<UK2Node_DoOnceMultiInput>(Node) != nullptr);
        IK2Node_AddPinInterface* AddPin = bClassMatches ? Cast<IK2Node_AddPinInterface>(Node) : nullptr;
        if (AddPin == nullptr || Pin == nullptr || !AddPin->CanRemovePin(Pin))
        {
            AddPatchError(State, TEXT("capability.operation_unavailable"), TEXT("Selected Pin cannot be removed by this Node's native add-pin interface."), Name);
            return false;
        }
        AddPin->RemoveInputPin(Pin);
    }
    else if (Name == TEXT("SplitStructPin"))
    {
        const UEdGraphSchema_K2* K2Schema = GetDefault<UEdGraphSchema_K2>();
        if (Pin == nullptr || !K2Schema->CanSplitStructPin(*Pin)) { AddPatchError(State, TEXT("capability.operation_unavailable"), TEXT("K2 Schema does not allow this Struct Pin to split."), Name); return false; }
        K2Schema->SplitPin(Pin, false);
        PrimaryPins = Pin->SubPins;
    }
    else if (Name == TEXT("RecombineStructPin"))
    {
        const UEdGraphSchema_K2* K2Schema = GetDefault<UEdGraphSchema_K2>();
        if (Pin == nullptr || Pin->ParentPin == nullptr || !K2Schema->CanRecombineStructPin(*Pin)) { AddPatchError(State, TEXT("capability.operation_unavailable"), TEXT("K2 Schema does not allow this child Pin to recombine."), Name); return false; }
        UEdGraphPin* Parent = Pin->ParentPin;
        K2Schema->RecombinePin(Pin);
        PrimaryPins.Add(Parent);
    }
    else
    {
        AddPatchError(State, TEXT("capability.operation_unavailable"), TEXT("Unknown or unavailable Graph Node/Pin operation."), Name);
        return false;
    }

    if (PrimaryPins.IsEmpty()) PrimaryPins = DifferencePins(Node, Before);
    State.TouchedNodes.Add(Node);
    for (UEdGraphPin* Created : PrimaryPins) State.TouchedPins.Add(Created);
    ++State.ChangedOps;
    return BindInvokeOutputs(State, Operation, PrimaryPins, NodeOutput, Name);
}

UK2Node_EditablePinBase* FindSignatureNode(UEdGraph* Graph, const bool bOutput, const bool bCreateResult, FString& OutError)
{
    OutError.Reset();
    if (Graph == nullptr) { OutError = TEXT("Signature Graph is unavailable."); return nullptr; }
    TArray<UK2Node_EditablePinBase*> Matches;
    UK2Node_FunctionEntry* FunctionEntry = nullptr;
    for (UEdGraphNode* Node : Graph->Nodes)
    {
        if (!bOutput)
        {
            if (UK2Node_FunctionEntry* Entry = Cast<UK2Node_FunctionEntry>(Node)) { Matches.Add(Entry); FunctionEntry = Entry; }
            else if (UK2Node_Tunnel* Tunnel = Cast<UK2Node_Tunnel>(Node); Tunnel != nullptr && Tunnel->bCanHaveOutputs) Matches.Add(Tunnel);
        }
        else
        {
            if (UK2Node_FunctionResult* Result = Cast<UK2Node_FunctionResult>(Node)) Matches.Add(Result);
            else if (UK2Node_FunctionEntry* Entry = Cast<UK2Node_FunctionEntry>(Node)) FunctionEntry = Entry;
            else if (UK2Node_Tunnel* Tunnel = Cast<UK2Node_Tunnel>(Node); Tunnel != nullptr && Tunnel->bCanHaveInputs) Matches.Add(Tunnel);
        }
    }
    if (bOutput && Matches.IsEmpty() && bCreateResult && FunctionEntry != nullptr)
    {
        if (UK2Node_FunctionResult* Created = FBlueprintEditorUtils::FindOrCreateFunctionResultNode(FunctionEntry)) Matches.Add(Created);
    }
    if (Matches.IsEmpty()) { OutError = TEXT("The Graph has no editable native owner for this signature side."); return nullptr; }
    if (Matches.Num() > 1 && Cast<UK2Node_FunctionResult>(Matches[0]) == nullptr)
    {
        OutError = TEXT("The Graph has ambiguous native signature boundary Nodes.");
        return nullptr;
    }
    return Matches[0];
}

UFunction* ResolveNativeFunction(const FString& Path)
{
    FString ClassPath;
    FString FunctionName;
    if (!Path.Split(TEXT(":"), &ClassPath, &FunctionName, ESearchCase::CaseSensitive, ESearchDir::FromEnd)
        || ClassPath.IsEmpty() || FunctionName.IsEmpty()) return nullptr;
    UClass* Class = LoadObject<UClass>(nullptr, *ClassPath);
    return Class != nullptr ? Class->FindFunctionByName(FName(*FunctionName)) : nullptr;
}

bool ExecuteCopySignatureFrom(
    FPatchState& State,
    UEdGraph* Graph,
    const TSharedPtr<FJsonObject>& Operation,
    const TSharedPtr<FJsonObject>* Args,
    const FString& Name)
{
    if (Graph == nullptr || State.Target.Blueprint == nullptr || !FBlueprintEditorUtils::IsDelegateSignatureGraph(Graph))
    {
        AddPatchError(State, TEXT("capability.operation_unavailable"), TEXT("CopySignatureFrom is available only on an authored Dispatcher Signature Graph."), Name);
        return false;
    }
    FString FunctionPath;
    if (!ReadArgText(Args, TEXT("function"), FunctionPath))
    {
        AddPatchError(State, TEXT("validation.operation_arguments_invalid"), TEXT("CopySignatureFrom requires one exact function path."), Name);
        return false;
    }
    TArray<UK2Node_FunctionEntry*> Entries;
    TArray<UK2Node_FunctionResult*> Results;
    Graph->GetNodesOfClass(Entries);
    Graph->GetNodesOfClass(Results);
    if (Entries.Num() != 1 || !Results.IsEmpty() || Entries[0] == nullptr || !Entries[0]->bIsEditable)
    {
        AddPatchError(State, TEXT("validation.signature_inconsistent"), TEXT("Dispatcher Signature Graph must have exactly one editable Entry and no Result Nodes."), Name);
        return false;
    }
    UClass* SkeletonClass = State.Target.Blueprint->SkeletonGeneratedClass;
    FMulticastDelegateProperty* Delegate = SkeletonClass != nullptr
        ? FindFProperty<FMulticastDelegateProperty>(SkeletonClass, Graph->GetFName())
        : nullptr;
    UClass* ScopeClass = Delegate != nullptr ? Delegate->GetOwner<UClass>() : nullptr;
    UFunction* Function = ResolveNativeFunction(FunctionPath);
    if (Delegate == nullptr || ScopeClass == nullptr || Function == nullptr
        || ScopeClass->FindFunctionByName(Function->GetFName()) != Function)
    {
        AddPatchError(State, Function == nullptr ? TEXT("resolution.function_not_found") : TEXT("validation.signature_inconsistent"), TEXT("Function is unavailable in the Dispatcher property's native Class scope."), Name, FunctionPath);
        return false;
    }
    if (!UEdGraphSchema_K2::FunctionCanBeUsedInDelegate(Function) || UEdGraphSchema_K2::HasFunctionAnyOutputParameter(Function))
    {
        AddPatchError(State, TEXT("validation.signature_incompatible"), TEXT("Function is not delegate-compatible or contains an output parameter."), Name, FunctionPath);
        return false;
    }
    const UEdGraphSchema_K2* Schema = GetDefault<UEdGraphSchema_K2>();
    TArray<TPair<FName, FEdGraphPinType>> Parameters;
    for (TFieldIterator<FProperty> It(Function); It && It->HasAnyPropertyFlags(CPF_Parm); ++It)
    {
        FProperty* Parameter = *It;
        FEdGraphPinType Type;
        FText NativeError;
        if (!Schema->ConvertPropertyToPinType(Parameter, Type)
            || !Schema->SupportsDropPinOnNode(Entries[0], Type, EGPD_Input, NativeError))
        {
            AddPatchError(State, TEXT("validation.signature_incompatible"), NativeError.IsEmpty() ? TEXT("Function parameter cannot be represented by this Dispatcher.") : NativeError.ToString(), Name, Parameter->GetName());
            return false;
        }
        Parameters.Add({Parameter->GetFName(), Type});
    }
    Entries[0]->Modify();
    while (!Entries[0]->UserDefinedPins.IsEmpty()) Entries[0]->RemoveUserDefinedPin(Entries[0]->UserDefinedPins[0]);
    for (const TPair<FName, FEdGraphPinType>& Parameter : Parameters)
    {
        if (Entries[0]->CreateUserDefinedPin(Parameter.Key, Parameter.Value, EGPD_Output, false) == nullptr)
        {
            AddPatchError(State, TEXT("validation.signature_edit_failed"), TEXT("UE failed while recreating the Dispatcher signature."), Name, Parameter.Key.ToString());
            return false;
        }
    }
    PropagateSignatureChanges({Entries[0]});
    State.TouchedNodes.Add(Entries[0]);
    ++State.ChangedOps;
    return BindInvokeOutputs(State, Operation, {}, nullptr, Name);
}

bool ExecuteSignatureInvoke(
    FPatchState& State,
    UEdGraph* Graph,
    UEdGraphNode* NodeTarget,
    UEdGraphPin* PinTarget,
    const TSharedPtr<FJsonObject>& Operation,
    const FString& Name)
{
    const TSharedPtr<FJsonObject>* Args = nullptr;
    Operation->TryGetObjectField(TEXT("args"), Args);
    if (Name == TEXT("CopySignatureFrom")) return ExecuteCopySignatureFrom(State, Graph, Operation, Args, Name);
    if (Name == TEXT("AddInputParameter") || Name == TEXT("AddOutputParameter") || Name == TEXT("AddParameter"))
    {
        FString RequestedName;
        FString TypeText;
        if (!ReadArgText(Args, TEXT("name"), RequestedName) || !ReadArgText(Args, TEXT("type"), TypeText))
        {
            AddPatchError(State, TEXT("validation.operation_arguments_invalid"), TEXT("Signature parameter creation requires name and native type."), Name);
            return false;
        }
        FEdGraphPinType Type;
        if (!ImportPinType(TypeText, Type))
        {
            AddPatchError(State, TEXT("validation.native_value_invalid"), TEXT("type is not valid native FEdGraphPinType text."), Name, TypeText);
            return false;
        }
        UK2Node_EditablePinBase* Owner = nullptr;
        EEdGraphPinDirection DesiredDirection = EGPD_Output;
        FString OwnerError;
        if (Name == TEXT("AddParameter"))
        {
            UK2Node_CustomEvent* CustomEvent = Cast<UK2Node_CustomEvent>(NodeTarget);
            Owner = CustomEvent != nullptr && CustomEvent->bIsEditable ? CustomEvent : nullptr;
        }
        else
        {
            const bool bOutput = Name == TEXT("AddOutputParameter");
            if (bOutput && FBlueprintEditorUtils::IsDelegateSignatureGraph(Graph))
            {
                AddPatchError(State, TEXT("capability.operation_unavailable"), TEXT("Dispatcher signatures do not have output parameters."), Name);
                return false;
            }
            Owner = FindSignatureNode(Graph, bOutput, bOutput, OwnerError);
            DesiredDirection = bOutput ? EGPD_Input : EGPD_Output;
        }
        if (Owner == nullptr)
        {
            AddPatchError(State, TEXT("resolution.signature_node_not_found"), OwnerError.IsEmpty() ? TEXT("Editable signature owner was not found.") : OwnerError, Name);
            return false;
        }
        if (Owner->FindPin(FName(*RequestedName)) != nullptr)
        {
            AddPatchError(State, TEXT("resolution.name_collision"), TEXT("Requested parameter name already exists; SAL does not silently suffix it."), Name, RequestedName);
            return false;
        }
        const UEdGraphSchema_K2* Schema = GetDefault<UEdGraphSchema_K2>();
        const EEdGraphPinDirection SourceDirection = DesiredDirection == EGPD_Output ? EGPD_Input : EGPD_Output;
        FText NativeError;
        if (!Schema->SupportsDropPinOnNode(Owner, Type, SourceDirection, NativeError))
        {
            AddPatchError(State, TEXT("capability.operation_unavailable"), NativeError.ToString(), Name);
            return false;
        }
        UEdGraphPin* Created = Schema->DropPinOnNode(Owner, FName(*RequestedName), Type, SourceDirection);
        if (Created == nullptr)
        {
            AddPatchError(State, TEXT("validation.signature_edit_failed"), TEXT("UE did not create the requested signature Pin."), Name);
            return false;
        }
        Created = Owner->FindPin(FName(*RequestedName));
        if (Created == nullptr)
        {
            AddPatchError(State, TEXT("validation.signature_edit_failed"), TEXT("Final propagated signature Pin could not be resolved."), Name, RequestedName);
            return false;
        }
        TArray<UK2Node_EditablePinBase*> FinalOwners;
        SignatureOwnersForPin(Created, FinalOwners);
        if (FinalOwners.IsEmpty()) State.TouchedNodes.Add(Owner);
        else for (UK2Node_EditablePinBase* FinalOwner : FinalOwners) State.TouchedNodes.Add(FinalOwner);
        State.TouchedPins.Add(Created);
        ++State.ChangedOps;
        return BindInvokeOutputs(State, Operation, {Created}, Name == TEXT("AddOutputParameter") ? Owner : nullptr, Name);
    }

    UK2Node_EditablePinBase* Editable = PinTarget != nullptr ? Cast<UK2Node_EditablePinBase>(PinTarget->GetOwningNode()) : nullptr;
    if (Name == TEXT("RemoveParameter"))
    {
        if (Editable == nullptr || PinTarget == nullptr || !FindUserPinInfo(Editable, PinTarget->PinName).IsValid())
        {
            AddPatchError(State, TEXT("capability.operation_unavailable"), TEXT("Pin is not an editable authored parameter."), Name);
            return false;
        }
        TArray<UK2Node_EditablePinBase*> Owners;
        SignatureOwnersForPin(PinTarget, Owners);
        if (Owners.IsEmpty())
        {
            AddPatchError(State, TEXT("capability.operation_unavailable"), TEXT("Pin has no editable authored signature state."), Name);
            return false;
        }
        const FName PinName = PinTarget->PinName;
        for (UK2Node_EditablePinBase* Owner : Owners) { Owner->Modify(); Owner->RemoveUserDefinedPinByName(PinName); State.TouchedNodes.Add(Owner); }
        PropagateSignatureChanges(Owners);
        ++State.ChangedOps;
        return BindInvokeOutputs(State, Operation, {}, nullptr, Name);
    }
    if (Name == TEXT("MoveParameterBefore") || Name == TEXT("MoveParameterAfter"))
    {
        const TSharedPtr<FJsonObject>* AnchorRef = nullptr;
        if (Editable == nullptr || PinTarget == nullptr || Args == nullptr || !(*Args)->TryGetObjectField(TEXT("anchor"), AnchorRef) || AnchorRef == nullptr)
        {
            AddPatchError(State, TEXT("validation.operation_arguments_invalid"), TEXT("Parameter move requires an editable target and anchor Pin."), Name);
            return false;
        }
        FGraphObjectRef Anchor = ResolveSimpleRef(State, *AnchorRef);
        UK2Node_EditablePinBase* AnchorOwner = Anchor.Pin != nullptr ? Cast<UK2Node_EditablePinBase>(Anchor.Pin->GetOwningNode()) : nullptr;
        if (AnchorOwner == nullptr || AnchorOwner->GetGraph() != Editable->GetGraph()
            || (Cast<UK2Node_FunctionResult>(AnchorOwner) != nullptr) != (Cast<UK2Node_FunctionResult>(Editable) != nullptr))
        {
            AddPatchError(State, TEXT("resolution.invalid_anchor"), TEXT("Parameter anchor must belong to the same semantic signature side."), Name);
            return false;
        }
        TArray<UK2Node_EditablePinBase*> Owners;
        SignatureOwnersForPin(PinTarget, Owners);
        if (Owners.IsEmpty()) { AddPatchError(State, TEXT("capability.operation_unavailable"), TEXT("Pin has no editable authored signature state."), Name); return false; }
        const FName SourceName = PinTarget->PinName;
        const FName AnchorName = Anchor.Pin->PinName;
        if (SourceName == AnchorName) return BindInvokeOutputs(State, Operation, {}, nullptr, Name);
        int32 CurrentSourceIndex = INDEX_NONE;
        int32 CurrentAnchorIndex = INDEX_NONE;
        for (int32 Index = 0; Index < Editable->UserDefinedPins.Num(); ++Index)
        {
            if (Editable->UserDefinedPins[Index].IsValid() && Editable->UserDefinedPins[Index]->PinName == SourceName) CurrentSourceIndex = Index;
            if (Editable->UserDefinedPins[Index].IsValid() && Editable->UserDefinedPins[Index]->PinName == AnchorName) CurrentAnchorIndex = Index;
        }
        if ((Name.EndsWith(TEXT("Before")) && CurrentSourceIndex + 1 == CurrentAnchorIndex)
            || (Name.EndsWith(TEXT("After")) && CurrentSourceIndex == CurrentAnchorIndex + 1))
        {
            return BindInvokeOutputs(State, Operation, {}, nullptr, Name);
        }
        for (UK2Node_EditablePinBase* Owner : Owners)
        {
            int32 SourceIndex = INDEX_NONE;
            int32 AnchorIndex = INDEX_NONE;
            for (int32 Index = 0; Index < Owner->UserDefinedPins.Num(); ++Index)
            {
                if (Owner->UserDefinedPins[Index].IsValid() && Owner->UserDefinedPins[Index]->PinName == SourceName) SourceIndex = Index;
                if (Owner->UserDefinedPins[Index].IsValid() && Owner->UserDefinedPins[Index]->PinName == AnchorName) AnchorIndex = Index;
            }
            if (SourceIndex == INDEX_NONE || AnchorIndex == INDEX_NONE)
            {
                AddPatchError(State, TEXT("validation.signature_inconsistent"), TEXT("Mirrored signature Pins do not share one native order."), Name);
                return false;
            }
            Owner->Modify();
            TSharedPtr<FUserPinInfo> Info = Owner->UserDefinedPins[SourceIndex];
            Owner->UserDefinedPins.RemoveAt(SourceIndex);
            if (SourceIndex < AnchorIndex) --AnchorIndex;
            Owner->UserDefinedPins.Insert(Info, Name.EndsWith(TEXT("After")) ? AnchorIndex + 1 : AnchorIndex);
            State.TouchedNodes.Add(Owner);
        }
        PropagateSignatureChanges(Owners);
        ++State.ChangedOps;
        return BindInvokeOutputs(State, Operation, {}, nullptr, Name);
    }
    return false;
}

bool ValidateInvokeArguments(FPatchState& State, const TSharedPtr<FJsonObject>& Operation, const FString& Name)
{
    const TSharedPtr<FJsonObject>* Args = nullptr;
    Operation->TryGetObjectField(TEXT("args"), Args);
    TSet<FString> Allowed;
    if (Name == TEXT("AddInputParameter") || Name == TEXT("AddOutputParameter") || Name == TEXT("AddParameter")) Allowed = {TEXT("name"), TEXT("type")};
    else if (Name == TEXT("MoveParameterBefore") || Name == TEXT("MoveParameterAfter")) Allowed = {TEXT("anchor")};
    else if (Name == TEXT("CopySignatureFrom")) Allowed = {TEXT("function")};
    else if (Name == TEXT("AddFloatTrack") || Name == TEXT("AddVectorTrack") || Name == TEXT("AddEventTrack") || Name == TEXT("AddLinearColorTrack")) Allowed = {TEXT("TrackName")};
    else if (Name == TEXT("AddTrackFromCurve")) Allowed = {TEXT("Curve")};
    else if (Name == TEXT("RenameTrack")) Allowed = {TEXT("TrackName"), TEXT("NewName")};
    else if (Name == TEXT("MoveTrack")) Allowed = {TEXT("TrackName"), TEXT("Before"), TEXT("After")};
    else if (Name == TEXT("RemoveTrack") || Name == TEXT("UseInternalCurve")) Allowed = {TEXT("TrackName")};
    else if (Name == TEXT("UseExternalCurve")) Allowed = {TEXT("TrackName"), TEXT("Curve")};
    else if (Name == TEXT("AddKey") || Name == TEXT("SetKey"))
    {
        Allowed = {TEXT("TrackName"), TEXT("Channel"), TEXT("Time"), TEXT("Value"), TEXT("InterpMode"), TEXT("TangentMode"),
            TEXT("TangentWeightMode"), TEXT("ArriveTangent"), TEXT("ArriveTangentWeight"), TEXT("LeaveTangent"), TEXT("LeaveTangentWeight")};
        if (Name == TEXT("SetKey")) Allowed.Add(TEXT("NewTime"));
    }
    else if (Name == TEXT("RemoveKey")) Allowed = {TEXT("TrackName"), TEXT("Channel"), TEXT("Time")};
    if (Args != nullptr && (*Args).IsValid())
    {
        for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : (*Args)->Values)
        {
            if (!Allowed.Contains(Pair.Key))
            {
                AddPatchError(State, TEXT("validation.operation_arguments_invalid"), TEXT("Argument is not part of this exact target-local Operation schema."), Name, Pair.Key);
                return false;
            }
        }
    }
    return true;
}

bool HandleInvoke(FPatchState& State, const TSharedPtr<FJsonObject>& Operation)
{
    TSharedPtr<FJsonObject> TargetRef;
    FGraphObjectRef Target;
    FString Name;
    Operation->TryGetStringField(TEXT("operation"), Name);
    if (!ValidateInvokeArguments(State, Operation, Name)) return false;
    if (!ReadOperationRef(Operation, TEXT("target"), TargetRef) || !ResolveRequiredRef(State, TargetRef, Name, Target)) return false;

    if (Name == TEXT("AddInputParameter") || Name == TEXT("AddOutputParameter") || Name == TEXT("AddParameter")
        || Name == TEXT("RemoveParameter") || Name == TEXT("MoveParameterBefore") || Name == TEXT("MoveParameterAfter")
        || Name == TEXT("CopySignatureFrom"))
    {
        if (ExecuteSignatureInvoke(State, Target.Graph != nullptr ? Target.Graph : State.Target.Graph, Target.Node, Target.Pin, Operation, Name)) return true;
        if (!State.Diagnostics.IsEmpty()) return false;
    }
    if (Target.Node != nullptr)
    {
        return ExecuteNodeInvoke(State, Target.Node, Target.Pin, Operation, Name);
    }
    AddPatchError(State, TEXT("capability.operation_unavailable"), TEXT("Operation is unavailable on this Graph object."), Name);
    return false;
}

TSharedPtr<FJsonObject> BuildTouchedObject(const FPatchState& State);

bool RunPatch(FPatchState& State, const FSalPatch& Patch)
{
    for (const TSharedPtr<FJsonValue>& StatementValue : Patch.Statements)
    {
        TSharedPtr<FJsonObject> Statement;
        if (!JsonObjectValue(StatementValue, Statement))
        {
            AddPatchError(State, TEXT("validation.patch_state_invalid"), TEXT("Patch statement must be an object."), TEXT("patch"));
            return false;
        }
        FString Kind;
        if (!Statement->TryGetStringField(TEXT("kind"), Kind))
        {
            if (!RegisterDefinition(State, Statement)) return false;
            continue;
        }
        bool bOk = false;
        if (Kind == TEXT("add")) bOk = HandleAdd(State, Statement);
        else if (Kind == TEXT("connect")) bOk = HandleConnectLike(State, Statement, true);
        else if (Kind == TEXT("disconnect")) bOk = HandleConnectLike(State, Statement, false);
        else if (Kind == TEXT("break")) bOk = HandleBreak(State, Statement);
        else if (Kind == TEXT("set")) bOk = HandleSetReset(State, Statement, false);
        else if (Kind == TEXT("reset")) bOk = HandleSetReset(State, Statement, true);
        else if (Kind == TEXT("move")) bOk = HandleMove(State, Statement);
        else if (Kind == TEXT("remove")) bOk = HandleRemove(State, Statement);
        else if (Kind == TEXT("insert")) bOk = HandleInsert(State, Statement);
        else if (Kind == TEXT("invoke")) bOk = HandleInvoke(State, Statement);
        else
        {
            AddPatchError(State, TEXT("capability.operation_unavailable"), FString::Printf(TEXT("Patch operation %s is not available on Graph."), *Kind), Kind);
            bOk = false;
        }
        if (!bOk) return false;
    }
    for (const TPair<FString, FNodeDefinition>& Pair : State.Definitions)
    {
        if (!Pair.Value.bConsumed)
        {
            AddPatchError(State, TEXT("validation.unused_binding"), TEXT("Every Graph creation binding must be consumed exactly once by add or insert."), TEXT("patch"), Pair.Key);
            return false;
        }
    }
    return State.Diagnostics.IsEmpty();
}

template <typename TCurve>
bool IsolateSandboxCurve(
    TCurve* SourceCurve,
    const bool bExternal,
    TObjectPtr<TCurve>& SandboxCurve,
    UObject* CurveOuter,
    TMap<const UObject*, UObject*>& CurveCopies,
    FString& OutError)
{
    if (bExternal)
    {
        // External Timeline assets are references, not sandbox-owned state.
        SandboxCurve = SourceCurve;
        return true;
    }
    if (SourceCurve == nullptr || CurveOuter == nullptr)
    {
        OutError =
            TEXT("An internal Timeline Curve is unavailable for dry run.");
        return false;
    }

    TCurve* CurveCopy = nullptr;
    if (UObject** Existing = CurveCopies.Find(SourceCurve))
    {
        CurveCopy = Cast<TCurve>(*Existing);
        if (CurveCopy == nullptr)
        {
            OutError =
                TEXT("Internal Timeline Curve aliases disagree on Curve type.");
            return false;
        }
    }
    else
    {
        const FName Name = MakeUniqueObjectName(
            CurveOuter,
            SourceCurve->GetClass(),
            SourceCurve->GetFName());
        CurveCopy =
            DuplicateObject<TCurve>(SourceCurve, CurveOuter, Name);
        if (CurveCopy == nullptr)
        {
            OutError =
                TEXT("Could not isolate an internal Timeline Curve for dry run.");
            return false;
        }
        CurveCopy->ClearFlags(RF_Public | RF_Standalone);
        CurveCopy->SetFlags(RF_Transient | RF_Transactional);
        CurveCopies.Add(SourceCurve, CurveCopy);
    }

    SandboxCurve = CurveCopy;
    if (SandboxCurve == SourceCurve
        || !SandboxCurve->IsIn(GetTransientPackage()))
    {
        OutError =
            TEXT("Internal Timeline Curve was not isolated for dry run.");
        return false;
    }
    return true;
}

bool DetachSandboxTimelineCurves(
    const UTimelineTemplate* Source,
    UTimelineTemplate* Sandbox,
    UObject* CurveOuter,
    TMap<const UObject*, UObject*>& CurveCopies,
    FString& OutError)
{
    if (Source == nullptr || Sandbox == nullptr || CurveOuter == nullptr)
    {
        OutError = TEXT("A Timeline Template is unavailable for dry run.");
        return false;
    }
    if (Source->EventTracks.Num() != Sandbox->EventTracks.Num()
        || Source->FloatTracks.Num() != Sandbox->FloatTracks.Num()
        || Source->VectorTracks.Num() != Sandbox->VectorTracks.Num()
        || Source->LinearColorTracks.Num()
            != Sandbox->LinearColorTracks.Num())
    {
        OutError =
            TEXT("UE transient duplication changed the Timeline track shape.");
        return false;
    }

    for (int32 Index = 0; Index < Source->EventTracks.Num(); ++Index)
    {
        const FTTEventTrack& SourceTrack = Source->EventTracks[Index];
        FTTEventTrack& SandboxTrack = Sandbox->EventTracks[Index];
        if (SourceTrack.GetTrackName() != SandboxTrack.GetTrackName()
            || SourceTrack.bIsExternalCurve
                != SandboxTrack.bIsExternalCurve
            || !IsolateSandboxCurve(
                SourceTrack.CurveKeys.Get(),
                SourceTrack.bIsExternalCurve,
                SandboxTrack.CurveKeys,
                CurveOuter,
                CurveCopies,
                OutError))
        {
            if (OutError.IsEmpty())
            {
                OutError =
                    TEXT("UE transient duplication changed an Event Timeline track.");
            }
            return false;
        }
    }
    for (int32 Index = 0; Index < Source->FloatTracks.Num(); ++Index)
    {
        const FTTFloatTrack& SourceTrack = Source->FloatTracks[Index];
        FTTFloatTrack& SandboxTrack = Sandbox->FloatTracks[Index];
        if (SourceTrack.GetTrackName() != SandboxTrack.GetTrackName()
            || SourceTrack.bIsExternalCurve
                != SandboxTrack.bIsExternalCurve
            || !IsolateSandboxCurve(
                SourceTrack.CurveFloat.Get(),
                SourceTrack.bIsExternalCurve,
                SandboxTrack.CurveFloat,
                CurveOuter,
                CurveCopies,
                OutError))
        {
            if (OutError.IsEmpty())
            {
                OutError =
                    TEXT("UE transient duplication changed a Float Timeline track.");
            }
            return false;
        }
    }
    for (int32 Index = 0; Index < Source->VectorTracks.Num(); ++Index)
    {
        const FTTVectorTrack& SourceTrack = Source->VectorTracks[Index];
        FTTVectorTrack& SandboxTrack = Sandbox->VectorTracks[Index];
        if (SourceTrack.GetTrackName() != SandboxTrack.GetTrackName()
            || SourceTrack.bIsExternalCurve
                != SandboxTrack.bIsExternalCurve
            || !IsolateSandboxCurve(
                SourceTrack.CurveVector.Get(),
                SourceTrack.bIsExternalCurve,
                SandboxTrack.CurveVector,
                CurveOuter,
                CurveCopies,
                OutError))
        {
            if (OutError.IsEmpty())
            {
                OutError =
                    TEXT("UE transient duplication changed a Vector Timeline track.");
            }
            return false;
        }
    }
    for (int32 Index = 0;
         Index < Source->LinearColorTracks.Num();
         ++Index)
    {
        const FTTLinearColorTrack& SourceTrack =
            Source->LinearColorTracks[Index];
        FTTLinearColorTrack& SandboxTrack =
            Sandbox->LinearColorTracks[Index];
        if (SourceTrack.GetTrackName() != SandboxTrack.GetTrackName()
            || SourceTrack.bIsExternalCurve
                != SandboxTrack.bIsExternalCurve
            || !IsolateSandboxCurve(
                SourceTrack.CurveLinearColor.Get(),
                SourceTrack.bIsExternalCurve,
                SandboxTrack.CurveLinearColor,
                CurveOuter,
                CurveCopies,
                OutError))
        {
            if (OutError.IsEmpty())
            {
                OutError =
                    TEXT("UE transient duplication changed a Linear Color Timeline track.");
            }
            return false;
        }
    }
    return true;
}

bool BuildSandboxTarget(
    const FSalResolvedTarget& Source,
    TStrongObjectPtr<UBlueprint>& OutSandboxOwner,
    FSalResolvedTarget& Out,
    FString& OutError)
{
    OutError.Reset();
    if (Source.Blueprint == nullptr || Source.Graph == nullptr)
    {
        OutError =
            TEXT("A Blueprint-owned Graph is unavailable for dry run.");
        return false;
    }

    OutSandboxOwner =
        MakeBlueprintSandbox(Source.Blueprint, OutError);
    UBlueprint* Sandbox = OutSandboxOwner.Get();
    if (Sandbox == nullptr)
    {
        return false;
    }

    UBlueprintGeneratedClass* Generated =
        Cast<UBlueprintGeneratedClass>(Sandbox->GeneratedClass.Get());
    UObject* TimelineOuter =
        Generated != nullptr
            ? static_cast<UObject*>(Generated)
            : static_cast<UObject*>(Sandbox);

    TMap<FGuid, UTimelineTemplate*> SandboxTimelinesByGuid;
    for (UTimelineTemplate* Timeline : Sandbox->Timelines)
    {
        if (Timeline == nullptr
            || Timeline->TimelineGuid.IsValid() == false
            || SandboxTimelinesByGuid.Contains(Timeline->TimelineGuid))
        {
            OutError =
                TEXT("Transient dry-run state contains an invalid or duplicate Timeline identity.");
            return false;
        }
        SandboxTimelinesByGuid.Add(Timeline->TimelineGuid, Timeline);
    }
    if (SandboxTimelinesByGuid.Num() != Source.Blueprint->Timelines.Num())
    {
        OutError =
            TEXT("UE transient duplication changed the Timeline count.");
        return false;
    }

    TMap<const UObject*, UObject*> CurveCopies;
    for (const UTimelineTemplate* SourceTimeline :
         Source.Blueprint->Timelines)
    {
        UTimelineTemplate* const* SandboxTimelinePtr =
            SourceTimeline != nullptr
                ? SandboxTimelinesByGuid.Find(
                    SourceTimeline->TimelineGuid)
                : nullptr;
        UTimelineTemplate* SandboxTimeline =
            SandboxTimelinePtr != nullptr
                ? *SandboxTimelinePtr
                : nullptr;
        if (SourceTimeline == nullptr
            || SandboxTimeline == nullptr
            || SandboxTimeline == SourceTimeline
            || !SandboxTimeline->IsIn(GetTransientPackage())
            || !DetachSandboxTimelineCurves(
                SourceTimeline,
                SandboxTimeline,
                TimelineOuter,
                CurveCopies,
                OutError))
        {
            if (OutError.IsEmpty())
            {
                OutError =
                    TEXT("Could not isolate a Timeline Template for dry run.");
            }
            return false;
        }
    }

    TArray<UEdGraph*> Graphs;
    Sandbox->GetAllGraphs(Graphs);
    UEdGraph* SandboxGraph = nullptr;
    for (UEdGraph* Graph : Graphs)
    {
        if (Graph != nullptr
            && Graph->GraphGuid == Source.Graph->GraphGuid)
        {
            if (!Graph->IsIn(Sandbox))
            {
                OutError =
                    TEXT("Target Graph was not isolated from the live Blueprint during dry run.");
                return false;
            }
            if (SandboxGraph != nullptr)
            {
                OutError =
                    TEXT("Dry-run Blueprint contains duplicate Graph identities.");
                return false;
            }
            SandboxGraph = Graph;
        }
    }
    if (SandboxGraph == nullptr)
    {
        OutError =
            TEXT("Could not resolve the target Graph in transient dry-run state.");
        return false;
    }

    Out = Source;
    Out.Object = SandboxGraph;
    Out.Package = GetTransientPackage();
    Out.Blueprint = Sandbox;
    Out.Class = Generated;
    Out.Graph = SandboxGraph;
    return true;
}
FString GraphPlanRef(const TSharedPtr<FJsonObject>& Ref)
{
    if (!Ref.IsValid()) return FString();
    FString Kind;
    Ref->TryGetStringField(TEXT("kind"), Kind);
    if (Kind == TEXT("local"))
    {
        FString Alias;
        Ref->TryGetStringField(TEXT("name"), Alias);
        return Alias;
    }
    if (Kind == TEXT("node") || Kind == TEXT("pin") || Kind == TEXT("graph"))
    {
        FString Id;
        Ref->TryGetStringField(TEXT("id"), Id);
        return Id.IsEmpty() ? FString() : Kind + TEXT("@") + Id;
    }
    if (Kind == TEXT("member"))
    {
        const TSharedPtr<FJsonObject>* Owner = nullptr;
        TArray<FString> Path;
        if (Ref->TryGetObjectField(TEXT("object"), Owner) && Owner != nullptr && ReadPath(Ref, Path))
        {
            const FString OwnerRef = GraphPlanRef(*Owner);
            return OwnerRef.IsEmpty() ? FString() : OwnerRef + TEXT(".") + FString::Join(Path, TEXT("."));
        }
    }
    return FString();
}

TSharedPtr<FJsonObject> BuildGraphPlan(
    const FSalPatch& Patch,
    const FPatchState& Preflight,
    const FSalResolvedTarget& LiveTarget)
{
    TSharedPtr<FJsonObject> Plan = MakeShared<FJsonObject>();
    Plan->SetNumberField(TEXT("statementCount"), Patch.Statements.Num());
    Plan->SetStringField(TEXT("targetGraph"), LiveTarget.Id);
    Plan->SetNumberField(TEXT("changedOperationCount"), Preflight.ChangedOps);

    TArray<TSharedPtr<FJsonValue>> Operations;
    TArray<TSharedPtr<FJsonValue>> Effects;
    Operations.Reserve(Patch.Statements.Num());
    Effects.Add(MakeShared<FJsonValueString>(FString::Printf(TEXT("changed operations: %d"), Preflight.ChangedOps)));

    for (int32 Index = 0; Index < Patch.Statements.Num(); ++Index)
    {
        TSharedPtr<FJsonObject> Statement;
        if (!JsonObjectValue(Patch.Statements[Index], Statement)) continue;

        FString Kind;
        if (!Statement->TryGetStringField(TEXT("kind"), Kind)) Kind = TEXT("binding");
        TSharedPtr<FJsonObject> Operation = MakeShared<FJsonObject>();
        Operation->SetNumberField(TEXT("index"), Index);
        Operation->SetStringField(TEXT("operation"), Kind);

        auto ReadRefField = [&](const TCHAR* Field) -> FString
        {
            const TSharedPtr<FJsonObject>* Ref = nullptr;
            return Statement->TryGetObjectField(Field, Ref) && Ref != nullptr ? GraphPlanRef(*Ref) : FString();
        };
        auto SetRefField = [&](const TCHAR* SourceField, const TCHAR* PlanField)
        {
            const FString Ref = ReadRefField(SourceField);
            if (!Ref.IsEmpty()) Operation->SetStringField(PlanField, Ref);
        };

        const FString TargetRef = ReadRefField(TEXT("target"));
        if (Kind == TEXT("binding"))
        {
            if (!TargetRef.IsEmpty()) Operation->SetStringField(TEXT("binding"), TargetRef);
            const TSharedPtr<FJsonObject>* ValueObject = nullptr;
            const TSharedPtr<FJsonObject>* Args = nullptr;
            FString Palette;
            if (Statement->TryGetObjectField(TEXT("value"), ValueObject) && ValueObject != nullptr
                && (*ValueObject)->TryGetObjectField(TEXT("args"), Args) && Args != nullptr
                && (*Args)->TryGetStringField(TEXT("palette"), Palette) && !Palette.IsEmpty())
            {
                Operation->SetStringField(TEXT("palette"), Palette);
            }
            const FNodeDefinition* Definition = Preflight.Definitions.Find(TargetRef);
            if (Definition != nullptr && Definition->bConsumed)
            {
                Effects.Add(MakeShared<FJsonValueString>(TEXT("creation alias consumed: ") + TargetRef));
            }
        }
        else
        {
            if (!TargetRef.IsEmpty()) Operation->SetStringField(TEXT("ref"), TargetRef);
            SetRefField(TEXT("from"), TEXT("from"));
            SetRefField(TEXT("to"), TEXT("to"));
            SetRefField(TEXT("input"), TEXT("input"));
            SetRefField(TEXT("output"), TEXT("output"));

            FString Invoke;
            if (Kind == TEXT("invoke") && Statement->TryGetStringField(TEXT("operation"), Invoke) && !Invoke.IsEmpty())
            {
                Operation->SetStringField(TEXT("invoke"), Invoke);
            }

            FString Effect;
            const FString From = ReadRefField(TEXT("from"));
            const FString To = ReadRefField(TEXT("to"));
            if (Kind == TEXT("connect") || Kind == TEXT("disconnect"))
            {
                Effect = Kind + TEXT(": ") + From + TEXT(" -> ") + To;
            }
            else if (Kind == TEXT("insert"))
            {
                Effect = Kind + TEXT(": ") + From + TEXT(" -> ") + ReadRefField(TEXT("input"))
                    + TEXT(" / ") + ReadRefField(TEXT("output")) + TEXT(" -> ") + To;
            }
            else if (Kind == TEXT("invoke"))
            {
                Effect = Kind + TEXT(" ") + Invoke + (TargetRef.IsEmpty() ? FString() : TEXT(": ") + TargetRef);
            }
            else
            {
                Effect = Kind + (TargetRef.IsEmpty() ? FString() : TEXT(": ") + TargetRef);
            }
            Effects.Add(MakeShared<FJsonValueString>(Effect));
        }
        Operations.Add(MakeShared<FJsonValueObject>(Operation));
    }

    // Only report preflight objects that can be re-resolved by a stable id in
    // the live Graph. Newly created transient Node and Pin ids never cross the
    // dry-run boundary; their source aliases above are the durable plan keys.
    if (LiveTarget.Graph != nullptr && Preflight.Target.Graph != nullptr)
    {
        for (UEdGraphNode* LiveNode : LiveTarget.Graph->Nodes)
        {
            if (LiveNode == nullptr) continue;
            UEdGraphNode* SandboxNode = FindNode(Preflight.Target.Graph, NodeId(LiveNode));
            if (SandboxNode != nullptr && Preflight.TouchedNodes.Contains(SandboxNode))
            {
                Effects.Add(MakeShared<FJsonValueString>(TEXT("touched node@") + NodeId(LiveNode)));
            }
            for (UEdGraphPin* LivePin : LiveNode->Pins)
            {
                if (LivePin == nullptr) continue;
                UEdGraphNode* SandboxOwner = FindNode(
                    Preflight.Target.Graph,
                    NodeId(LiveNode));
                UEdGraphPin* SandboxPin = SandboxOwner != nullptr
                    ? SandboxOwner->FindPinById(LivePin->PinId)
                    : nullptr;
                if (SandboxPin != nullptr && Preflight.TouchedPins.Contains(SandboxPin))
                {
                    Effects.Add(MakeShared<FJsonValueString>(TEXT("touched pin@") + PinId(LivePin)));
                }
            }
        }
    }

    Plan->SetArrayField(TEXT("operations"), Operations);
    Plan->SetArrayField(TEXT("effects"), Effects);
    return Plan;
}
}

#if WITH_DEV_AUTOMATION_TESTS
bool FSalGraphInterface::BuildSandboxTargetForTesting(
    const FSalResolvedTarget& Source,
    TStrongObjectPtr<UBlueprint>& OutSandboxOwner,
    FSalResolvedTarget& OutTarget,
    FString& OutError)
{
    return BuildSandboxTarget(
        Source,
        OutSandboxOwner,
        OutTarget,
        OutError);
}
#endif

TSharedPtr<FJsonObject> FSalGraphInterface::Patch(const FSalPatch& Patch, const FSalResolvedTarget& Target)
{
    if (Target.Graph == nullptr || Target.Blueprint == nullptr)
    {
        return GraphErrorResult(TEXT("resolution.graph_not_found"), TEXT("A Blueprint-owned Graph target is required for Graph patch."), TEXT("patch"));
    }
    if (GEditor == nullptr || !GEditor->CanTransact() || GEditor->IsTransactionActive())
    {
        return GraphErrorResult(TEXT("capability.transaction_unavailable"), TEXT("Graph Patch requires one available top-level Editor transaction, including for isolated native dry run."), TEXT("patch"));
    }

    TStrongObjectPtr<UBlueprint> SandboxOwner;
    FSalResolvedTarget SandboxTarget;
    FString SandboxError;
    if (!BuildSandboxTarget(
            Target,
            SandboxOwner,
            SandboxTarget,
            SandboxError))
    {
        return GraphErrorResult(TEXT("capability.operation_unavailable"), SandboxError, TEXT("patch"));
    }
    FPatchState Preflight(SandboxTarget, true);
    bool bValid = false;
    {
        FScopedTransaction SandboxTransaction(LOCTEXT("SalGraphDryRun", "SAL Graph Dry Run"));
        bValid = RunPatch(Preflight, Patch);
        // The native path has executed only against transient objects. Cancel its
        // transaction record so dry run cannot pollute the Editor undo stack.
        SandboxTransaction.Cancel();
    }
    if (!bValid)
    {
        return MakeMutationResult(nullptr, Preflight.Diagnostics, Patch.bDryRun, false, false, Target.AssetPath, TEXT("patch"), nullptr, nullptr);
    }

    TSharedPtr<FJsonObject> Planned = BuildGraphPlan(Patch, Preflight, Target);
    if (Patch.bDryRun)
    {
        FPatchState Current(Target, true);
        for (UEdGraphNode* Node : Preflight.TouchedNodes)
        {
            if (UEdGraphNode* Live = FindNode(Target.Graph, NodeId(Node))) Current.TouchedNodes.Add(Live);
        }
        return MakeMutationResult(BuildTouchedObject(Current), {}, true, true, false, Target.AssetPath, TEXT("patch"), Planned, nullptr);
    }

    if (GEditor == nullptr || !GEditor->CanTransact() || GEditor->IsTransactionActive())
    {
        TArray<TSharedPtr<FJsonObject>> Diagnostics;
        Diagnostics.Add(FSalDiagnostics::Error(TEXT("capability.transaction_unavailable"), TEXT("Graph Patch requires one available top-level Editor transaction."))
            .Interface(TEXT("graph")).Operation(TEXT("patch")).Build());
        return MakeMutationResult(nullptr, Diagnostics, false, false, false, Target.AssetPath, TEXT("patch"), Planned, nullptr);
    }

    FPatchState Apply(Target, true);
    const bool bWasDirty = Target.Package != nullptr && Target.Package->IsDirty();
    bool bApplied = false;
    bool bRolledBack = false;
    bool bTransactionAvailable = false;
    {
        LoomleMutation::FScopedAtomicTransaction Transaction(
            LOCTEXT("SalGraphPatch", "SAL Graph Patch"));
        bTransactionAvailable = Transaction.IsOutstanding();
        if (bTransactionAvailable)
        {
            Target.Blueprint->Modify();
            Target.Graph->Modify();
            bApplied = RunPatch(Apply, Patch);
            if (!bApplied)
            {
                bRolledBack = Transaction.RevertAndCancel();
            }
            else if (Apply.ChangedOps == 0)
            {
                Transaction.Cancel();
            }
        }
    }
    if (!bTransactionAvailable)
    {
        AddPatchError(
            Apply,
            TEXT("capability.transaction_unavailable"),
            TEXT("UE did not open the required Graph Patch transaction."),
            TEXT("patch"));
        return MakeMutationResult(
            nullptr,
            Apply.Diagnostics,
            false,
            false,
            false,
            Target.AssetPath,
            TEXT("patch"),
            Planned,
            Apply.ResolvedRefs);
    }
    if (!bApplied)
    {
        if (Target.Package != nullptr)
        {
            // A failed rollback leaves the live asset in an unknown partially
            // mutated state. Never restore a clean flag in that case.
            Target.Package->SetDirtyFlag(bRolledBack ? bWasDirty : true);
        }
        if (!bRolledBack)
        {
            AddPatchError(Apply, TEXT("validation.rollback_failed"), TEXT("UE could not undo the failed Graph Patch transaction."), TEXT("patch"));
        }
        return MakeMutationResult(nullptr, Apply.Diagnostics, false, false, !bRolledBack, Target.AssetPath, TEXT("patch"), Planned, Apply.ResolvedRefs);
    }
    if (Apply.ChangedOps > 0)
    {
        Target.Graph->NotifyGraphChanged();
        FBlueprintEditorUtils::MarkBlueprintAsModified(Target.Blueprint);
        Target.Blueprint->MarkPackageDirty();
    }
    TSharedPtr<FJsonObject> Diff = MakeShared<FJsonObject>();
    Diff->SetNumberField(TEXT("changedOperations"), Apply.ChangedOps);
    return MakeMutationResult(
        BuildTouchedObject(Apply),
        Apply.Diagnostics,
        false,
        true,
        Apply.ChangedOps > 0,
        Target.AssetPath,
        TEXT("patch"),
        Planned,
        Apply.ResolvedRefs,
        Diff);
}

namespace
{
constexpr int32 DefaultCollectionLimit = 50;
constexpr int32 MaxCollectionLimit = 200;

FString GuidText(const FGuid& Guid)
{
    return Guid.ToString(EGuidFormats::DigitsWithHyphensLower);
}

FString GraphTypeText(const UEdGraph* Graph)
{
    const UEdGraphSchema* Schema = Graph != nullptr ? Graph->GetSchema() : nullptr;
    switch (Schema != nullptr ? Schema->GetGraphType(Graph) : GT_MAX)
    {
    case GT_Function: return TEXT("GT_Function");
    case GT_Ubergraph: return TEXT("GT_Ubergraph");
    case GT_Macro: return TEXT("GT_Macro");
    case GT_Animation: return TEXT("GT_Animation");
    case GT_StateMachine: return TEXT("GT_StateMachine");
    default: return TEXT("GT_MAX");
    }
}

FString NodeId(const UEdGraphNode* Node)
{
    return Node != nullptr ? GuidText(Node->NodeGuid) : FString();
}

FString PinId(const UEdGraphPin* Pin)
{
    return Pin != nullptr ? GuidText(Pin->PinId) : FString();
}

FString NodeType(const UEdGraphNode* Node)
{
    return Node != nullptr && Node->GetClass() != nullptr ? Node->GetClass()->GetPathName() : FString();
}

FString NodeTitle(const UEdGraphNode* Node)
{
    return Node != nullptr ? Node->GetNodeTitle(ENodeTitleType::ListView).ToString() : FString();
}

FString PinTypeText(const UEdGraphPin* Pin)
{
    if (Pin == nullptr)
    {
        return FString();
    }
    FString Text;
    FEdGraphPinType::StaticStruct()->ExportText(
        Text,
        &Pin->PinType,
        nullptr,
        Pin->GetOwningNodeUnchecked(),
        PPF_None,
        nullptr,
        false);
    return Text;
}

bool ImportPinType(const FString& Text, FEdGraphPinType& OutType)
{
    OutType = FEdGraphPinType();
    return FEdGraphPinType::StaticStruct()->ImportText(
        *Text,
        &OutType,
        nullptr,
        PPF_None,
        GLog,
        TEXT("FEdGraphPinType")) != nullptr;
}

bool IsExecPin(const UEdGraphPin* Pin)
{
    return Pin != nullptr && UEdGraphSchema_K2::IsExecPin(*Pin);
}

UEdGraphNode* FindNode(const UEdGraph* Graph, const FString& Id)
{
    if (Graph == nullptr)
    {
        return nullptr;
    }
    for (UEdGraphNode* Node : Graph->Nodes)
    {
        if (Node != nullptr && NodeId(Node).Equals(Id, ESearchCase::IgnoreCase))
        {
            return Node;
        }
    }
    return nullptr;
}

UEdGraphPin* FindPin(const UEdGraph* Graph, const FString& Id, bool* OutAmbiguous)
{
    if (OutAmbiguous != nullptr)
    {
        *OutAmbiguous = false;
    }
    if (Graph == nullptr)
    {
        return nullptr;
    }
    UEdGraphPin* Match = nullptr;
    for (UEdGraphNode* Node : Graph->Nodes)
    {
        if (Node == nullptr)
        {
            continue;
        }
        for (UEdGraphPin* Pin : Node->Pins)
        {
            if (Pin != nullptr && PinId(Pin).Equals(Id, ESearchCase::IgnoreCase))
            {
                if (Match != nullptr)
                {
                    if (OutAmbiguous != nullptr)
                    {
                        *OutAmbiguous = true;
                    }
                    return nullptr;
                }
                Match = Pin;
            }
        }
    }
    return Match;
}

FString SanitizeIdentifier(const FString& Source, const FString& Fallback)
{
    auto IsAsciiAlpha = [](const TCHAR Character)
    {
        return (Character >= TEXT('A') && Character <= TEXT('Z'))
            || (Character >= TEXT('a') && Character <= TEXT('z'));
    };
    auto IsAsciiDigit = [](const TCHAR Character)
    {
        return Character >= TEXT('0') && Character <= TEXT('9');
    };
    FString Result;
    Result.Reserve(Source.Len());
    for (const TCHAR Character : Source)
    {
        Result.AppendChar(IsAsciiAlpha(Character) || IsAsciiDigit(Character) || Character == TEXT('_') ? Character : TEXT('_'));
    }
    if (Result.IsEmpty())
    {
        Result = Fallback;
    }
    if (IsAsciiDigit(Result[0]))
    {
        Result.InsertAt(0, TEXT('_'));
    }
    if (Result == TEXT("true") || Result == TEXT("false") || Result == TEXT("null"))
    {
        Result += TEXT("_item");
    }
    return Result;
}

bool IsSalIdentifier(const FString& Text)
{
    auto IsAsciiAlpha = [](const TCHAR Character)
    {
        return (Character >= TEXT('A') && Character <= TEXT('Z'))
            || (Character >= TEXT('a') && Character <= TEXT('z'));
    };
    auto IsAsciiDigit = [](const TCHAR Character)
    {
        return Character >= TEXT('0') && Character <= TEXT('9');
    };
    if (Text.IsEmpty() || !(IsAsciiAlpha(Text[0]) || Text[0] == TEXT('_')))
    {
        return false;
    }
    for (const TCHAR Character : Text)
    {
        if (!(IsAsciiAlpha(Character) || IsAsciiDigit(Character) || Character == TEXT('_')))
        {
            return false;
        }
    }
    return Text != TEXT("true") && Text != TEXT("false") && Text != TEXT("null");
}

TSharedPtr<FJsonValue> NativeNameValue(const FString& Text)
{
    return IsSalIdentifier(Text) ? Value::Name(Text) : Value::String(Text);
}

FString UniqueMemberAlias(const FString& Preferred, TSet<FString>& Used)
{
    const FString Base = SanitizeIdentifier(Preferred, TEXT("pin"));
    FString Alias = Base;
    int32 Suffix = 2;
    while (Used.Contains(Alias))
    {
        Alias = FString::Printf(TEXT("%s_%d"), *Base, Suffix++);
    }
    Used.Add(Alias);
    return Alias;
}

TSharedPtr<FJsonObject> GraphErrorResult(
    const FString& Code,
    const FString& Message,
    const FString& Operation,
    const FString& Ref,
    const FString& Suggestion)
{
    FSalDiagnosticBuilder Diagnostic = FSalDiagnostics::Error(Code, Message)
        .Interface(TEXT("graph"))
        .Operation(Operation);
    if (!Ref.IsEmpty())
    {
        Diagnostic.Ref(Ref);
    }
    if (!Suggestion.IsEmpty())
    {
        Diagnostic.Suggestion(Suggestion);
    }
    return FSalDiagnostics::Result(Diagnostic.Build());
}

bool QueryHasPage(const FSalQuery& Query)
{
    return Query.Source.IsValid() && Query.Source->HasField(TEXT("page"));
}

bool HasOnlyDetails(const FSalQuery& Query, const TSet<FString>& Allowed, FString& OutInvalid)
{
    for (const FString& Detail : Query.With)
    {
        if (!Allowed.Contains(Detail))
        {
            OutInvalid = Detail;
            return false;
        }
    }
    return true;
}

bool IsStringExpr(const TSharedPtr<FJsonValue>& Value)
{
    FString Text;
    return Value.IsValid() && Value->TryGetString(Text);
}

bool IsPaletteObjectExpr(const TSharedPtr<FJsonValue>& Value, const FString& ExpectedKind)
{
    const TSharedPtr<FJsonObject>* Object = nullptr;
    if (!Value.IsValid() || !Value->TryGetObject(Object) || Object == nullptr || !(*Object).IsValid()) return false;
    FString Kind;
    if (!(*Object)->TryGetStringField(TEXT("kind"), Kind)) return false;
    if (Kind == TEXT("name"))
    {
        FString Name;
        return (*Object)->TryGetStringField(TEXT("name"), Name) && !Name.IsEmpty();
    }
    FString Id;
    return Kind == ExpectedKind && (*Object)->TryGetStringField(TEXT("id"), Id) && !Id.IsEmpty();
}

bool ValidateNodeCondition(const TSharedPtr<FJsonObject>& Condition, FString& OutMessage)
{
    FString Kind;
    Condition->TryGetStringField(TEXT("kind"), Kind);
    if (Kind == TEXT("not"))
    {
        const TSharedPtr<FJsonObject>* Inner = nullptr;
        return Condition->TryGetObjectField(TEXT("condition"), Inner) && Inner != nullptr && ValidateNodeCondition(*Inner, OutMessage);
    }
    if (Kind == TEXT("and") || Kind == TEXT("or"))
    {
        const TArray<TSharedPtr<FJsonValue>>* Conditions = nullptr;
        if (!Condition->TryGetArrayField(TEXT("conditions"), Conditions) || Conditions == nullptr) return false;
        for (const TSharedPtr<FJsonValue>& Value : *Conditions)
        {
            const TSharedPtr<FJsonObject>* Inner = nullptr;
            if (!Value.IsValid() || !Value->TryGetObject(Inner) || Inner == nullptr || !ValidateNodeCondition(*Inner, OutMessage)) return false;
        }
        return true;
    }
    const FString Field = ConditionField(Condition);
    if (!(Field == TEXT("type") || Field == TEXT("id") || Field == TEXT("NodeComment")) || !IsStringExpr(Condition->TryGetField(TEXT("value"))))
    {
        OutMessage = TEXT("nodes where supports string fields type, id, and NodeComment only.");
        return false;
    }
    if (Kind == TEXT("eq") || Kind == TEXT("ne")) return true;
    if (Kind == TEXT("contains") && Field == TEXT("NodeComment")) return true;
    OutMessage = TEXT("nodes supports = and !=; ~= is available only for NodeComment.");
    return false;
}

bool ValidatePaletteCondition(
    const TSharedPtr<FJsonObject>& Condition,
    TSet<FString>& OutObjectFields,
    FString& OutMessage)
{
    FString Kind;
    Condition->TryGetStringField(TEXT("kind"), Kind);
    if (Kind == TEXT("and"))
    {
        const TArray<TSharedPtr<FJsonValue>>* Conditions = nullptr;
        if (!Condition->TryGetArrayField(TEXT("conditions"), Conditions) || Conditions == nullptr) return false;
        for (const TSharedPtr<FJsonValue>& Value : *Conditions)
        {
            const TSharedPtr<FJsonObject>* Inner = nullptr;
            if (!Value.IsValid() || !Value->TryGetObject(Inner) || Inner == nullptr
                || !ValidatePaletteCondition(*Inner, OutObjectFields, OutMessage)) return false;
        }
        return true;
    }
    const FString Field = ConditionField(Condition);
    const TSharedPtr<FJsonValue> Value = Condition->TryGetField(TEXT("value"));
    if (Field == TEXT("contextSensitive"))
    {
        bool bValue = false;
        if (!(Kind == TEXT("eq") || Kind == TEXT("ne")) || !Value.IsValid() || !Value->TryGetBool(bValue))
        {
            OutMessage = TEXT("contextSensitive accepts only = or != with a Boolean value.");
            return false;
        }
        return true;
    }
    if (Field == TEXT("widget") || Field == TEXT("component") || Field == TEXT("actor"))
    {
        if (Kind != TEXT("eq") || !IsPaletteObjectExpr(Value, Field))
        {
            OutMessage = FString::Printf(TEXT("%s accepts only = with an exact Name or %s@id."), *Field, *Field);
            return false;
        }
        OutObjectFields.Add(Field);
        if (OutObjectFields.Num() > 1)
        {
            OutMessage = TEXT("widget, component, and actor Palette contexts are mutually exclusive.");
            return false;
        }
        return true;
    }
    OutMessage = TEXT("palette entries where supports widget, component, actor, and contextSensitive only.");
    return false;
}

TSharedPtr<FJsonObject> ValidateGraphQuery(const FSalQuery& Query, const FString& Kind)
{
    const bool bWhere = Query.Where.IsValid();
    const bool bOrder = !Query.OrderBy.IsEmpty();
    const bool bPage = QueryHasPage(Query);
    TSet<FString> Details;
    bool bAllowWhere = false;
    bool bAllowOrderPage = false;
    if (Kind == TEXT("nodes"))
    {
        bAllowWhere = true;
        bAllowOrderPage = true;
        Details.Add(TEXT("layout"));
    }
    else if (Kind == TEXT("graph") || Kind == TEXT("palette"))
    {
        Details.Add(TEXT("schema"));
    }
    else if (Kind == TEXT("node") || Kind == TEXT("pin"))
    {
        Details.Add(TEXT("schema"));
        Details.Add(TEXT("layout"));
    }
    else if (Kind == TEXT("context") || Kind == TEXT("exec_flow") || Kind == TEXT("data_flow"))
    {
        Details.Add(TEXT("layout"));
    }
    else if (Kind == TEXT("palette_entries"))
    {
        bAllowWhere = true;
        bAllowOrderPage = true;
    }

    if (bWhere && !bAllowWhere)
    {
        return GraphErrorResult(TEXT("capability.clause_unavailable"), TEXT("where is unavailable for this Graph query operation."), Kind);
    }
    if ((bOrder || bPage) && !bAllowOrderPage)
    {
        return GraphErrorResult(TEXT("capability.clause_unavailable"), TEXT("order by and page are unavailable for this Graph query operation."), Kind);
    }
    FString InvalidDetail;
    if (!HasOnlyDetails(Query, Details, InvalidDetail))
    {
        return GraphErrorResult(TEXT("capability.detail_unavailable"), TEXT("Requested with detail is unavailable for this Graph query operation."), Kind, InvalidDetail);
    }
    if (Kind == TEXT("nodes") && bWhere)
    {
        FString Message;
        if (!ValidateNodeCondition(Query.Where, Message)) return GraphErrorResult(TEXT("capability.condition_unavailable"), Message, Kind);
    }
    if (Kind == TEXT("palette_entries") && bWhere)
    {
        TSet<FString> ObjectFields;
        FString Message;
        if (!ValidatePaletteCondition(Query.Where, ObjectFields, Message)) return GraphErrorResult(TEXT("capability.condition_unavailable"), Message, Kind);
    }
    const TSet<FString> OrderKeys = Kind == TEXT("nodes")
        ? TSet<FString>{TEXT("type"), TEXT("id")}
        : TSet<FString>{TEXT("name"), TEXT("category"), TEXT("id")};
    for (const TSharedPtr<FJsonObject>& Order : Query.OrderBy)
    {
        FString Key;
        Order->TryGetStringField(TEXT("key"), Key);
        if (!OrderKeys.Contains(Key))
        {
            return GraphErrorResult(TEXT("capability.order_unavailable"), TEXT("Unsupported order key for this Graph collection."), Kind, Key);
        }
    }
    return nullptr;
}

FString CompactJson(const TSharedPtr<FJsonObject>& Object)
{
    if (!Object.IsValid()) return FString();
    FString Text;
    const TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
        TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Text);
    FJsonSerializer::Serialize(Object.ToSharedRef(), Writer);
    return Text;
}

uint32 GraphCursorHash(const FSalQuery& Query, const FSalResolvedTarget& Target)
{
    FString Material = Target.AssetPath + TEXT("|") + Target.Id + TEXT("|") + CompactJson(Query.Operation)
        + TEXT("|") + CompactJson(Query.Where) + TEXT("|") + FString::Join(Query.With, TEXT(","));
    for (const TSharedPtr<FJsonObject>& Order : Query.OrderBy) Material += TEXT("|") + CompactJson(Order);
    return FCrc::StrCrc32(*Material);
}

bool DecodeGraphPage(
    const FSalQuery& Query,
    const FSalResolvedTarget& Target,
    FSalPage& OutPage,
    TSharedPtr<FJsonObject>& OutError)
{
    OutPage.Limit = FMath::Clamp(Query.PageLimit > 0 ? Query.PageLimit : DefaultCollectionLimit, 1, MaxCollectionLimit);
    OutPage.Offset = 0;
    if (Query.PageAfter.IsEmpty()) return true;
    TArray<FString> Parts;
    Query.PageAfter.ParseIntoArray(Parts, TEXT(":"), false);
    const FString ExpectedHash = FString::Printf(TEXT("%08x"), GraphCursorHash(Query, Target));
    if (Parts.Num() != 3
        || Parts[0] != TEXT("graph1")
        || !Parts[1].Equals(ExpectedHash, ESearchCase::IgnoreCase)
        || !ParseNonNegativeInt32(Parts[2], OutPage.Offset))
    {
        OutError = GraphErrorResult(
            TEXT("validation.invalid_cursor"),
            TEXT("Graph cursor does not belong to this target, operation, filter, detail, or ordering."),
            TEXT("page"),
            Query.PageAfter,
            TEXT("Run the collection query again without page after."));
        return false;
    }
    return true;
}

void SetGraphPage(
    const TSharedPtr<FJsonObject>& Result,
    const FSalQuery& Query,
    const FSalResolvedTarget& Target,
    const int32 NextOffset,
    const bool bHasNext)
{
    if (!Result.IsValid() || !bHasNext) return;
    TSharedPtr<FJsonObject> Page = MakeShared<FJsonObject>();
    Page->SetStringField(
        TEXT("next"),
        FString::Printf(TEXT("graph1:%08x:%d"), GraphCursorHash(Query, Target), NextOffset));
    Result->SetObjectField(TEXT("page"), Page);
}

TSharedPtr<FJsonValue> GraphValue(const FSalResolvedTarget& Target, const bool bFull)
{
    TSharedPtr<FJsonObject> Args = MakeShared<FJsonObject>();
    Args->SetStringField(TEXT("id"), Target.Id);
    if (bFull)
    {
        Args->SetField(TEXT("name"), NativeNameValue(Target.Name));
        Args->SetField(TEXT("type"), Value::Name(GraphTypeText(Target.Graph)));
    }
    return Value::Call(TEXT("graph"), Args);
}

TSharedPtr<FJsonObject> RichCurveValue(const FRichCurve& Curve)
{
    TSharedPtr<FJsonObject> Object = MakeShared<FJsonObject>();
    TArray<TSharedPtr<FJsonValue>> Keys;
    for (const FRichCurveKey& Key : Curve.GetConstRefOfKeys())
    {
        TSharedPtr<FJsonObject> Item = MakeShared<FJsonObject>();
        Item->SetNumberField(TEXT("Time"), Key.Time);
        Item->SetNumberField(TEXT("Value"), Key.Value);
        Item->SetField(TEXT("InterpMode"), Value::Name(StaticEnum<ERichCurveInterpMode>()->GetNameStringByValue(Key.InterpMode)));
        Item->SetField(TEXT("TangentMode"), Value::Name(StaticEnum<ERichCurveTangentMode>()->GetNameStringByValue(Key.TangentMode)));
        Item->SetField(TEXT("TangentWeightMode"), Value::Name(StaticEnum<ERichCurveTangentWeightMode>()->GetNameStringByValue(Key.TangentWeightMode)));
        if (!FMath::IsNearlyZero(Key.ArriveTangent)) Item->SetNumberField(TEXT("ArriveTangent"), Key.ArriveTangent);
        if (!FMath::IsNearlyZero(Key.ArriveTangentWeight)) Item->SetNumberField(TEXT("ArriveTangentWeight"), Key.ArriveTangentWeight);
        if (!FMath::IsNearlyZero(Key.LeaveTangent)) Item->SetNumberField(TEXT("LeaveTangent"), Key.LeaveTangent);
        if (!FMath::IsNearlyZero(Key.LeaveTangentWeight)) Item->SetNumberField(TEXT("LeaveTangentWeight"), Key.LeaveTangentWeight);
        Keys.Add(MakeShared<FJsonValueObject>(Item));
    }
    Object->SetArrayField(TEXT("Keys"), Keys);
    return Object;
}

TSharedPtr<FJsonValue> CurveValue(const UCurveFloat* Curve, const bool bExternal)
{
    if (Curve == nullptr) return Value::Null();
    if (bExternal) return Value::String(Curve->GetPathName());
    TSharedPtr<FJsonObject> Object = MakeShared<FJsonObject>();
    Object->SetObjectField(TEXT("FloatCurve"), RichCurveValue(Curve->FloatCurve));
    return MakeShared<FJsonValueObject>(Object);
}

TSharedPtr<FJsonValue> CurveValue(const UCurveVector* Curve, const bool bExternal)
{
    if (Curve == nullptr) return Value::Null();
    if (bExternal) return Value::String(Curve->GetPathName());
    TSharedPtr<FJsonObject> Object = MakeShared<FJsonObject>();
    static const TCHAR* Channels[] = {TEXT("X"), TEXT("Y"), TEXT("Z")};
    for (int32 Index = 0; Index < 3; ++Index) Object->SetObjectField(Channels[Index], RichCurveValue(Curve->FloatCurves[Index]));
    return MakeShared<FJsonValueObject>(Object);
}

TSharedPtr<FJsonValue> CurveValue(const UCurveLinearColor* Curve, const bool bExternal)
{
    if (Curve == nullptr) return Value::Null();
    if (bExternal) return Value::String(Curve->GetPathName());
    TSharedPtr<FJsonObject> Object = MakeShared<FJsonObject>();
    static const TCHAR* Channels[] = {TEXT("R"), TEXT("G"), TEXT("B"), TEXT("A")};
    for (int32 Index = 0; Index < 4; ++Index) Object->SetObjectField(Channels[Index], RichCurveValue(Curve->FloatCurves[Index]));
    return MakeShared<FJsonValueObject>(Object);
}

template <typename TTrack, typename TCurveGetter>
TArray<TSharedPtr<FJsonValue>> TimelineTrackValues(const TArray<TTrack>& Tracks, TCurveGetter&& GetCurve, const TCHAR* CurveField)
{
    TArray<TSharedPtr<FJsonValue>> Values;
    for (const TTrack& Track : Tracks)
    {
        TSharedPtr<FJsonObject> Item = MakeShared<FJsonObject>();
        Item->SetField(TEXT("TrackName"), NativeNameValue(Track.GetTrackName().ToString()));
        Item->SetBoolField(TEXT("bIsExternalCurve"), Track.bIsExternalCurve);
        Item->SetField(CurveField, CurveValue(GetCurve(Track), Track.bIsExternalCurve));
        Values.Add(MakeShared<FJsonValueObject>(Item));
    }
    return Values;
}

bool AddTimelineNativeFields(const UK2Node_Timeline* Node, const TSharedPtr<FJsonObject>& Args)
{
    UBlueprint* Blueprint = Node != nullptr ? FBlueprintEditorUtils::FindBlueprintForNode(Node) : nullptr;
    UTimelineTemplate* Timeline = FindTimelineTemplate(Blueprint, Node);
    if (Timeline == nullptr || !Args.IsValid()) return false;
    Args->SetField(TEXT("TimelineName"), NativeNameValue(Node->TimelineName.ToString()));
    for (const TCHAR* Field : {TEXT("TimelineLength"), TEXT("LengthMode"), TEXT("bAutoPlay"), TEXT("bLoop"), TEXT("bReplicated"),
        TEXT("bIgnoreTimeDilation"), TEXT("MetaDataArray"), TEXT("TimelineTickGroup")})
    {
        if (FProperty* Property = FindFProperty<FProperty>(UTimelineTemplate::StaticClass(), Field))
        {
            Args->SetField(Field, NativeValue(ExportPropertyValue(Property, Timeline)));
        }
    }
    Args->SetArrayField(TEXT("EventTracks"), TimelineTrackValues(Timeline->EventTracks, [](const FTTEventTrack& Track) { return Track.CurveKeys.Get(); }, TEXT("CurveKeys")));
    Args->SetArrayField(TEXT("FloatTracks"), TimelineTrackValues(Timeline->FloatTracks, [](const FTTFloatTrack& Track) { return Track.CurveFloat.Get(); }, TEXT("CurveFloat")));
    Args->SetArrayField(TEXT("VectorTracks"), TimelineTrackValues(Timeline->VectorTracks, [](const FTTVectorTrack& Track) { return Track.CurveVector.Get(); }, TEXT("CurveVector")));
    Args->SetArrayField(TEXT("LinearColorTracks"), TimelineTrackValues(Timeline->LinearColorTracks, [](const FTTLinearColorTrack& Track) { return Track.CurveLinearColor.Get(); }, TEXT("CurveLinearColor")));
    TArray<TSharedPtr<FJsonValue>> DisplayOrder;
    for (int32 Index = 0; Index < Timeline->GetNumDisplayTracks(); ++Index)
    {
        const FTTTrackId Id = Timeline->GetDisplayTrackId(Index);
        FName Name = NAME_None;
        if (Id.TrackType == FTTTrackBase::TT_Event && Timeline->EventTracks.IsValidIndex(Id.TrackIndex)) Name = Timeline->EventTracks[Id.TrackIndex].GetTrackName();
        else if (Id.TrackType == FTTTrackBase::TT_FloatInterp && Timeline->FloatTracks.IsValidIndex(Id.TrackIndex)) Name = Timeline->FloatTracks[Id.TrackIndex].GetTrackName();
        else if (Id.TrackType == FTTTrackBase::TT_VectorInterp && Timeline->VectorTracks.IsValidIndex(Id.TrackIndex)) Name = Timeline->VectorTracks[Id.TrackIndex].GetTrackName();
        else if (Id.TrackType == FTTTrackBase::TT_LinearColorInterp && Timeline->LinearColorTracks.IsValidIndex(Id.TrackIndex)) Name = Timeline->LinearColorTracks[Id.TrackIndex].GetTrackName();
        DisplayOrder.Add(NativeNameValue(Name.IsNone() ? TEXT("None") : Name.ToString()));
    }
    Args->SetArrayField(TEXT("TrackDisplayOrder"), DisplayOrder);
    return true;
}

void AddNodeNativeFields(const UEdGraphNode* Node, const TSharedPtr<FJsonObject>& Args)
{
    if (Node == nullptr || !Args.IsValid())
    {
        return;
    }

    const bool bTimeline = Cast<UK2Node_Timeline>(Node) != nullptr;
    if (bTimeline) AddTimelineNativeFields(CastChecked<UK2Node_Timeline>(Node), Args);
    for (TFieldIterator<FProperty> It(Node->GetClass()); It; ++It)
    {
        FProperty* Property = *It;
        const FNodePropertyAccess Access = GetNodePropertyAccess(Node, Property);
        if (!Access.bReadable
            || (Access.DefaultObject != nullptr && Property->Identical_InContainer(Node, Access.DefaultObject)))
        {
            continue;
        }
        const FString Name = Property->GetName();
        if (Name == TEXT("graph") || Name == TEXT("id") || Name == TEXT("type") || Name == TEXT("at") || Name == TEXT("size"))
        {
            continue;
        }
        Args->SetField(Name, NativeValue(ExportPropertyValue(Property, Node)));
    }
}

TSharedPtr<FJsonValue> NodeValue(
    const UEdGraphNode* Node,
    const FString& GraphAlias,
    const bool bFull,
    const bool bLayout)
{
    TSharedPtr<FJsonObject> Args = MakeShared<FJsonObject>();
    Args->SetField(TEXT("graph"), Value::Local(GraphAlias));
    Args->SetStringField(TEXT("id"), NodeId(Node));
    Args->SetStringField(TEXT("type"), NodeType(Node));
    if (bFull)
    {
        AddNodeNativeFields(Node, Args);
    }
    if (bLayout && Node != nullptr)
    {
        TArray<TSharedPtr<FJsonValue>> At;
        At.Add(Value::Number(Node->NodePosX));
        At.Add(Value::Number(Node->NodePosY));
        Args->SetArrayField(TEXT("at"), At);
        if (Node->NodeWidth > 0 && Node->NodeHeight > 0)
        {
            TArray<TSharedPtr<FJsonValue>> Size;
            Size.Add(Value::Number(Node->NodeWidth));
            Size.Add(Value::Number(Node->NodeHeight));
            Args->SetArrayField(TEXT("size"), Size);
        }
    }
    return Value::Call(TEXT("node"), Args);
}

TSharedPtr<FJsonValue> PinValue(const UEdGraphPin* Pin, const bool bFuture = false)
{
    TSharedPtr<FJsonObject> Args = MakeShared<FJsonObject>();
    if (!bFuture && Pin != nullptr)
    {
        Args->SetStringField(TEXT("id"), PinId(Pin));
    }
    Args->SetStringField(TEXT("type"), PinTypeText(Pin));
    Args->SetField(TEXT("direction"), Value::Name(Pin != nullptr && Pin->Direction == EGPD_Output ? TEXT("out") : TEXT("in")));
    if (Pin == nullptr)
    {
        return Value::Call(TEXT("pin"), Args);
    }

    if (!Pin->DefaultValue.IsEmpty()) Args->SetStringField(TEXT("DefaultValue"), Pin->DefaultValue);
    if (!Pin->AutogeneratedDefaultValue.IsEmpty()) Args->SetStringField(TEXT("AutogeneratedDefaultValue"), Pin->AutogeneratedDefaultValue);
    if (Pin->DefaultObject != nullptr) Args->SetStringField(TEXT("DefaultObject"), Pin->DefaultObject->GetPathName());
    if (!Pin->DefaultTextValue.IsEmpty()) Args->SetStringField(TEXT("DefaultTextValue"), Pin->DefaultTextValue.ToString());
    if (Pin->bHidden) Args->SetBoolField(TEXT("bHidden"), true);
    if (Pin->bNotConnectable) Args->SetBoolField(TEXT("bNotConnectable"), true);
    if (Pin->bDefaultValueIsReadOnly) Args->SetBoolField(TEXT("bDefaultValueIsReadOnly"), true);
    if (Pin->bDefaultValueIsIgnored) Args->SetBoolField(TEXT("bDefaultValueIsIgnored"), true);
    if (Pin->bAdvancedView) Args->SetBoolField(TEXT("bAdvancedView"), true);
    if (Pin->bDeprecated) Args->SetBoolField(TEXT("bDeprecated"), true);
    if (Pin->bOrphanedPin) Args->SetBoolField(TEXT("bOrphanedPin"), true);
    if (!Pin->PinFriendlyName.IsEmpty()) Args->SetStringField(TEXT("PinFriendlyName"), Pin->PinFriendlyName.ToString());
    if (Pin->PersistentGuid.IsValid()) Args->SetStringField(TEXT("PersistentGuid"), GuidText(Pin->PersistentGuid));
    if (Pin->ParentPin != nullptr) Args->SetField(TEXT("ParentPin"), Value::Stable(TEXT("pin"), PinId(Pin->ParentPin)));
    if (Pin->ReferencePassThroughConnection != nullptr)
    {
        Args->SetField(TEXT("ReferencePassThroughConnection"), Value::Stable(TEXT("pin"), PinId(Pin->ReferencePassThroughConnection)));
    }
    return Value::Call(TEXT("pin"), Args);
}

bool HasNodeCompilerMessage(const UEdGraphNode* Node)
{
    return Node != nullptr && Node->bHasCompilerMessage && !Node->ErrorMsg.IsEmpty();
}

FString NodeCompilerSeverityText(const int32 ErrorType)
{
    if (ErrorType == 0) return TEXT("CriticalError");
    switch (ErrorType)
    {
    case EMessageSeverity::Error: return TEXT("Error");
    case EMessageSeverity::PerformanceWarning: return TEXT("PerformanceWarning");
    case EMessageSeverity::Warning: return TEXT("Warning");
    case EMessageSeverity::Info: return TEXT("Info");
    default: return FString::Printf(TEXT("ErrorType=%d"), ErrorType);
    }
}

FString HealthComment(const FString& Header, const FString& Message)
{
    return Message.Contains(TEXT("\n"))
        ? Header + TEXT("\n") + Message
        : Header + TEXT(": ") + Message;
}

bool HasNodeUpgradeMessage(const UEdGraphNode* Node)
{
#if WITH_EDITORONLY_DATA
    return Node != nullptr && !Node->NodeUpgradeMessage.IsEmpty();
#else
    return false;
#endif
}

bool HasNodeVisualWarning(const UEdGraphNode* Node)
{
    return Node != nullptr && Node->ShowVisualWarning();
}

bool HasPinDeprecation(const UEdGraphPin* Pin)
{
#if WITH_EDITORONLY_DATA
    return Pin != nullptr && Pin->bDeprecated && !Pin->DeprecationMessage.IsEmpty();
#else
    return false;
#endif
}

FString StaleBlueprintStatusText(const UBlueprint* Blueprint)
{
    if (Blueprint == nullptr) return FString();
    if (Blueprint->Status == BS_Dirty) return TEXT("BS_Dirty");
    if (Blueprint->Status == BS_Unknown) return TEXT("BS_Unknown");
    return FString();
}

TArray<FString> NodeHealthLabels(const UEdGraphNode* Node, bool& bHasCompilerMessage)
{
    TArray<FString> Labels;
    bHasCompilerMessage = HasNodeCompilerMessage(Node);
    if (bHasCompilerMessage)
    {
        Labels.Add(TEXT("compiler ") + NodeCompilerSeverityText(Node->ErrorType));
    }
    if (HasNodeUpgradeMessage(Node))
    {
        Labels.Add(TEXT("upgrade note"));
    }
    if (HasNodeVisualWarning(Node))
    {
        Labels.Add(TEXT("visual warning"));
    }
    if (Node != nullptr)
    {
        for (const UEdGraphPin* Pin : Node->Pins)
        {
            if (HasPinDeprecation(Pin))
            {
                Labels.Add(TEXT("pin deprecation"));
                break;
            }
        }
    }
    return Labels;
}

bool AddNodeComments(
    FSalObjectBuilder& Builder,
    const UEdGraphNode* Node,
    const bool bIncludeHealthComments)
{
    const FString Title = NodeTitle(Node);
    if (!Title.IsEmpty())
    {
        Builder.AddComment(Title);
    }
    if (!bIncludeHealthComments)
    {
        return false;
    }
    const bool bHasCompilerMessage = HasNodeCompilerMessage(Node);
    if (bHasCompilerMessage)
    {
        Builder.AddComment(
            FString::Printf(TEXT("UE node diagnostic: %s\n%s"), *NodeCompilerSeverityText(Node->ErrorType), *Node->ErrorMsg));
    }
#if WITH_EDITORONLY_DATA
    if (HasNodeUpgradeMessage(Node))
    {
        Builder.AddComment(HealthComment(TEXT("UE node upgrade note"), Node->NodeUpgradeMessage.ToString()));
    }
#endif
    if (HasNodeVisualWarning(Node))
    {
        const FString Message = Node->GetVisualWarningTooltipText().ToString();
        Builder.AddComment(Message.IsEmpty()
            ? TEXT("UE node visual warning")
            : HealthComment(TEXT("UE node visual warning"), Message));
    }
    return bHasCompilerMessage;
}

void AddPinComments(FSalObjectBuilder& Builder, const UEdGraphPin* Pin)
{
    if (!HasPinDeprecation(Pin)) return;
#if WITH_EDITORONLY_DATA
    Builder.AddComment(HealthComment(TEXT("UE pin deprecation"), Pin->DeprecationMessage));
#endif
}

struct FEncodedGraph
{
    FSalObjectBuilder Builder;
    FString GraphAlias;
    UBlueprint* Blueprint = nullptr;
    bool bAddedCompilerStaleNote = false;
    bool bIncludeHealthComments = true;
    TMap<const UEdGraphNode*, FString> NodeAliases;
    TMap<const UEdGraphPin*, TSharedPtr<FJsonObject>> PinRefs;

    explicit FEncodedGraph(
        const FSalResolvedTarget& Target,
        const bool bFullGraph = false,
        const bool bInIncludeHealthComments = true)
    {
        Blueprint = Target.Blueprint;
        bIncludeHealthComments = bInIncludeHealthComments;
        GraphAlias = Builder.UniqueAlias(TEXT("g"));
        Builder.AddLocalBinding(GraphAlias, GraphValue(Target, bFullGraph));
    }

    void AddCompilerStaleNoteIfNeeded(const bool bHasCompilerMessage)
    {
        if (!bHasCompilerMessage || bAddedCompilerStaleNote || Blueprint == nullptr)
        {
            return;
        }
        const FString Status = StaleBlueprintStatusText(Blueprint);
        if (Status.IsEmpty())
        {
            return;
        }
        Builder.AddComment(FString::Printf(
            TEXT("UE node compiler messages may be stale: owning Blueprint Status is %s. Run an explicit Blueprint compile to refresh them."),
            *Status));
        bAddedCompilerStaleNote = true;
    }

    FString AddNode(const UEdGraphNode* Node, const bool bFull, const bool bLayout)
    {
        if (const FString* Existing = NodeAliases.Find(Node))
        {
            return *Existing;
        }
        const FString Preferred = NodeTitle(Node).IsEmpty() ? (Node != nullptr ? Node->GetClass()->GetName() : TEXT("node")) : NodeTitle(Node);
        const FString Alias = Builder.UniqueAlias(Preferred);
        Builder.AddLocalBinding(Alias, NodeValue(Node, GraphAlias, bFull, bLayout));
        AddCompilerStaleNoteIfNeeded(AddNodeComments(Builder, Node, bIncludeHealthComments));
        NodeAliases.Add(Node, Alias);
        return Alias;
    }

    TSharedPtr<FJsonObject> AddPin(const UEdGraphPin* Pin, const FString& NodeAlias, TSet<FString>& UsedMembers, const bool bFuture = false)
    {
        if (const TSharedPtr<FJsonObject>* Existing = PinRefs.Find(Pin))
        {
            return *Existing;
        }
        const FString NativeName = Pin != nullptr ? Pin->PinName.ToString() : TEXT("pin");
        const FString Member = UniqueMemberAlias(NativeName, UsedMembers);
        TSharedPtr<FJsonValue> Encoded = PinValue(Pin, bFuture);
        const TSharedPtr<FJsonObject>* EncodedObject = nullptr;
        if (Encoded->TryGetObject(EncodedObject) && EncodedObject != nullptr && (*EncodedObject).IsValid() && Member != NativeName)
        {
            (*EncodedObject)->GetObjectField(TEXT("args"))->SetStringField(TEXT("PinName"), NativeName);
        }
        Builder.AddMemberBinding(NodeAlias, {Member}, Encoded);
        if (!bFuture && bIncludeHealthComments) AddPinComments(Builder, Pin);
        TSharedPtr<FJsonObject> Ref = Value::MemberObject(Value::LocalObject(NodeAlias), {Member});
        PinRefs.Add(Pin, Ref);
        return Ref;
    }
};

FString ReadConditionValue(const UEdGraphNode* Node, const FString& Field)
{
    if (Field == TEXT("id")) return NodeId(Node);
    if (Field == TEXT("type")) return NodeType(Node);
    if (Field == TEXT("NodeComment")) return Node != nullptr ? Node->NodeComment : FString();
    return FString();
}

bool MatchesCondition(const UEdGraphNode* Node, const TSharedPtr<FJsonObject>& Condition)
{
    if (!Condition.IsValid())
    {
        return true;
    }
    FString Kind;
    Condition->TryGetStringField(TEXT("kind"), Kind);
    if (Kind == TEXT("not"))
    {
        const TSharedPtr<FJsonObject>* Inner = nullptr;
        return Condition->TryGetObjectField(TEXT("condition"), Inner) && Inner != nullptr && !MatchesCondition(Node, *Inner);
    }
    if (Kind == TEXT("and") || Kind == TEXT("or"))
    {
        const TArray<TSharedPtr<FJsonValue>>* Items = nullptr;
        if (!Condition->TryGetArrayField(TEXT("conditions"), Items) || Items == nullptr)
        {
            return false;
        }
        const bool bAnd = Kind == TEXT("and");
        for (const TSharedPtr<FJsonValue>& Item : *Items)
        {
            const TSharedPtr<FJsonObject>* Object = nullptr;
            const bool bMatch = Item.IsValid() && Item->TryGetObject(Object) && Object != nullptr && MatchesCondition(Node, *Object);
            if (bAnd && !bMatch) return false;
            if (!bAnd && bMatch) return true;
        }
        return bAnd;
    }
    const FString Left = ReadConditionValue(Node, ConditionField(Condition));
    const FString Right = ExprString(Condition->TryGetField(TEXT("value")));
    if (Kind == TEXT("eq")) return Left == Right;
    if (Kind == TEXT("ne")) return Left != Right;
    if (Kind == TEXT("contains")) return Left.Contains(Right, ESearchCase::IgnoreCase);
    return false;
}

bool MatchesNodeText(const UEdGraphNode* Node, const FString& Text)
{
    return Text.IsEmpty()
        || NodeTitle(Node).Contains(Text, ESearchCase::IgnoreCase)
        || NodeId(Node).Contains(Text, ESearchCase::IgnoreCase)
        || NodeType(Node).Contains(Text, ESearchCase::IgnoreCase)
        || (Node != nullptr && Node->NodeComment.Contains(Text, ESearchCase::IgnoreCase));
}

void AddSchemaComment(FSalObjectBuilder& Builder, const FString& Subject, const TArray<FString>& Fields, const TArray<FString>& Operations)
{
    FString Text = FString::Printf(TEXT("schema: %s"), *Subject);
    if (!Fields.IsEmpty())
    {
        Text += TEXT("\nfields:");
        for (const FString& Field : Fields) Text += TEXT("\n  ") + Field;
    }
    if (!Operations.IsEmpty())
    {
        Text += TEXT("\noperations:");
        for (const FString& Operation : Operations) Text += TEXT("\n  ") + Operation;
    }
    Builder.AddComment(Text);
}

struct FPaletteContextData
{
    bool bContextSensitive = true;
    TArray<UEdGraphPin*> Pins;
    TArray<FFieldVariant> SelectedObjects;
    FString Descriptor = TEXT("cs1");
};

const TSharedPtr<FJsonValue> FindConditionValue(const TSharedPtr<FJsonObject>& Condition, const FString& Field)
{
    if (!Condition.IsValid()) return nullptr;
    FString Kind;
    Condition->TryGetStringField(TEXT("kind"), Kind);
    if (Kind == TEXT("and"))
    {
        const TArray<TSharedPtr<FJsonValue>>* Conditions = nullptr;
        if (Condition->TryGetArrayField(TEXT("conditions"), Conditions) && Conditions != nullptr)
        {
            for (const TSharedPtr<FJsonValue>& Value : *Conditions)
            {
                const TSharedPtr<FJsonObject>* Inner = nullptr;
                if (Value.IsValid() && Value->TryGetObject(Inner) && Inner != nullptr)
                {
                    if (const TSharedPtr<FJsonValue> Match = FindConditionValue(*Inner, Field)) return Match;
                }
            }
        }
        return nullptr;
    }
    return ConditionField(Condition) == Field ? Condition->TryGetField(TEXT("value")) : nullptr;
}

bool ReadConditionRef(const TSharedPtr<FJsonValue>& Value, FString& OutKind, FString& OutIdentity)
{
    OutKind.Reset();
    OutIdentity.Reset();
    const TSharedPtr<FJsonObject>* Object = nullptr;
    if (!Value.IsValid() || !Value->TryGetObject(Object) || Object == nullptr || !(*Object).IsValid()
        || !(*Object)->TryGetStringField(TEXT("kind"), OutKind)) return false;
    return (*Object)->TryGetStringField(OutKind == TEXT("name") ? TEXT("name") : TEXT("id"), OutIdentity) && !OutIdentity.IsEmpty();
}

FString WidgetStableId(const UWidgetBlueprint* Blueprint, const UWidget* Widget)
{
    if (Blueprint == nullptr || Widget == nullptr) return FString();
    if (const FGuid* Guid = Blueprint->WidgetVariableNameToGuidMap.Find(Widget->GetFName())) return GuidText(*Guid);
    return FString();
}

UWidget* FindPaletteWidget(UWidgetBlueprint* Blueprint, const FString& Kind, const FString& Identity)
{
    if (Blueprint == nullptr) return nullptr;
    UWidget* Match = nullptr;
    for (UWidget* Widget : Blueprint->GetAllSourceWidgets())
    {
        const bool bMatches = Widget != nullptr && (Kind == TEXT("name")
            ? Widget->GetName() == Identity
            : WidgetStableId(Blueprint, Widget).Equals(Identity, ESearchCase::IgnoreCase));
        if (!bMatches) continue;
        if (Match != nullptr) return nullptr;
        Match = Widget;
    }
    return Match;
}

USCS_Node* FindPaletteComponent(UBlueprint* Blueprint, const FString& Kind, const FString& Identity)
{
    if (Blueprint == nullptr || Blueprint->SimpleConstructionScript == nullptr) return nullptr;
    USCS_Node* Match = nullptr;
    for (USCS_Node* Node : Blueprint->SimpleConstructionScript->GetAllNodes())
    {
        const bool bMatches = Node != nullptr && (Kind == TEXT("name")
            ? Node->GetVariableName().ToString() == Identity
            : GuidText(Node->VariableGuid).Equals(Identity, ESearchCase::IgnoreCase));
        if (!bMatches) continue;
        if (Match != nullptr) return nullptr;
        Match = Node;
    }
    return Match;
}

AActor* FindPaletteActor(UBlueprint* Blueprint, const FString& Kind, const FString& Identity)
{
    const ULevelScriptBlueprint* LevelBlueprint = Cast<ULevelScriptBlueprint>(Blueprint);
    ULevel* Level = LevelBlueprint != nullptr ? LevelBlueprint->GetLevel() : nullptr;
    if (Level == nullptr) return nullptr;
    AActor* Match = nullptr;
    for (AActor* Actor : Level->Actors)
    {
        const bool bMatches = Actor != nullptr && (Kind == TEXT("name")
            ? Actor->GetName() == Identity
            : GuidText(Actor->GetActorGuid()).Equals(Identity, ESearchCase::IgnoreCase));
        if (!bMatches) continue;
        if (Match != nullptr) return nullptr;
        Match = Actor;
    }
    return Match;
}

FObjectProperty* FindGeneratedObjectProperty(UBlueprint* Blueprint, const FName Name)
{
    if (Blueprint == nullptr) return nullptr;
    if (FObjectProperty* Property = Blueprint->SkeletonGeneratedClass != nullptr
        ? FindFProperty<FObjectProperty>(Blueprint->SkeletonGeneratedClass, Name)
        : nullptr) return Property;
    return Blueprint->GeneratedClass != nullptr ? FindFProperty<FObjectProperty>(Blueprint->GeneratedClass, Name) : nullptr;
}

bool AddPaletteObjectContext(
    const FSalResolvedTarget& Target,
    const FString& ObjectKind,
    const FString& RefKind,
    const FString& Identity,
    FPaletteContextData& Out,
    FString& OutStableId,
    FString& OutMessage)
{
    if (ObjectKind == TEXT("widget"))
    {
        UWidgetBlueprint* Blueprint = Cast<UWidgetBlueprint>(Target.Blueprint);
        UWidget* Widget = FindPaletteWidget(Blueprint, RefKind, Identity);
        FObjectProperty* Property = Widget != nullptr && Widget->bIsVariable
            ? FindGeneratedObjectProperty(Blueprint, Widget->GetFName())
            : nullptr;
        if (Widget == nullptr || Property == nullptr)
        {
            OutMessage = TEXT("Widget Palette context requires one exact variable Widget with a generated FObjectProperty.");
            return false;
        }
        OutStableId = WidgetStableId(Blueprint, Widget);
        Out.SelectedObjects.Add(FFieldVariant(Property));
        return true;
    }
    if (ObjectKind == TEXT("component"))
    {
        USCS_Node* Node = FindPaletteComponent(Target.Blueprint, RefKind, Identity);
        FObjectProperty* Property = Node != nullptr ? FindGeneratedObjectProperty(Target.Blueprint, Node->GetVariableName()) : nullptr;
        if (Node == nullptr || Property == nullptr)
        {
            OutMessage = TEXT("Component Palette context requires one exact SCS Component with a generated FObjectProperty.");
            return false;
        }
        OutStableId = GuidText(Node->VariableGuid);
        Out.SelectedObjects.Add(FFieldVariant(Property));
        return true;
    }
    AActor* Actor = FindPaletteActor(Target.Blueprint, RefKind, Identity);
    if (Actor == nullptr || !Actor->GetActorGuid().IsValid())
    {
        OutMessage = TEXT("Actor Palette context requires one exact Actor in the target Level Blueprint.");
        return false;
    }
    OutStableId = GuidText(Actor->GetActorGuid());
    Out.SelectedObjects.Add(FFieldVariant(Actor));
    return true;
}

bool BuildPaletteContextFromQuery(
    const FSalQuery& Query,
    const FSalResolvedTarget& Target,
    FPaletteContextData& Out,
    TSharedPtr<FJsonObject>& OutError)
{
    Out = FPaletteContextData();
    if (FindConditionValue(Query.Where, TEXT("contextSensitive")).IsValid())
    {
        // For an `and`, find the exact leaf so `!=` remains meaningful.
        TFunction<void(const TSharedPtr<FJsonObject>&)> ReadLeaf = [&](const TSharedPtr<FJsonObject>& Condition)
        {
            if (!Condition.IsValid()) return;
            FString Kind;
            Condition->TryGetStringField(TEXT("kind"), Kind);
            if (Kind == TEXT("and"))
            {
                const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
                if (Condition->TryGetArrayField(TEXT("conditions"), Values) && Values != nullptr)
                {
                    for (const TSharedPtr<FJsonValue>& Value : *Values)
                    {
                        const TSharedPtr<FJsonObject>* Inner = nullptr;
                        if (Value->TryGetObject(Inner) && Inner != nullptr) ReadLeaf(*Inner);
                    }
                }
            }
            else if (ConditionField(Condition) == TEXT("contextSensitive"))
            {
                bool bValue = true;
                Condition->TryGetField(TEXT("value"))->TryGetBool(bValue);
                Out.bContextSensitive = Kind == TEXT("ne") ? !bValue : bValue;
            }
        };
        ReadLeaf(Query.Where);
    }

    const TSharedPtr<FJsonObject>* PinContext = nullptr;
    if (Query.Operation->TryGetObjectField(TEXT("pinContext"), PinContext) && PinContext != nullptr)
    {
        FString Direction;
        const TSharedPtr<FJsonObject>* PinRef = nullptr;
        FString Id;
        (*PinContext)->TryGetStringField(TEXT("direction"), Direction);
        if (!(*PinContext)->TryGetObjectField(TEXT("pin"), PinRef) || PinRef == nullptr || !(*PinRef)->TryGetStringField(TEXT("id"), Id)) return false;
        bool bAmbiguous = false;
        UEdGraphPin* Pin = FindPin(Target.Graph, Id, &bAmbiguous);
        const EEdGraphPinDirection ExpectedDirection = Direction == TEXT("from") ? EGPD_Output : EGPD_Input;
        if (Pin == nullptr || Pin->Direction != ExpectedDirection)
        {
            OutError = GraphErrorResult(
                bAmbiguous ? TEXT("resolution.pin_ambiguous") : TEXT("resolution.pin_not_found"),
                bAmbiguous
                    ? TEXT("Palette Pin context matches multiple Pins in the bound Graph.")
                    : TEXT("Palette Pin context is missing or has the wrong direction."),
                TEXT("palette entries"),
                Id);
            return false;
        }
        Out.Pins.Add(Pin);
        Out.Descriptor += FString::Printf(TEXT(";pin.%s.%s"), *Direction, *Id);
    }

    for (const TCHAR* ObjectKindText : {TEXT("widget"), TEXT("component"), TEXT("actor")})
    {
        const FString ObjectKind(ObjectKindText);
        const TSharedPtr<FJsonValue> Value = FindConditionValue(Query.Where, ObjectKind);
        if (!Value.IsValid()) continue;
        FString RefKind;
        FString Identity;
        if (!ReadConditionRef(Value, RefKind, Identity)) return false;
        FString StableId;
        FString Message;
        if (!AddPaletteObjectContext(Target, ObjectKind, RefKind, Identity, Out, StableId, Message))
        {
            OutError = GraphErrorResult(TEXT("resolution.object_not_found"), Message, TEXT("palette entries"), Identity);
            return false;
        }
        Out.Descriptor += FString::Printf(TEXT(";%s.%s"), *ObjectKind, *StableId);
    }
    Out.Descriptor[2] = Out.bContextSensitive ? TCHAR('1') : TCHAR('0');
    return true;
}

bool BuildPaletteContextFromDescriptor(
    const FSalResolvedTarget& Target,
    const FString& Descriptor,
    FPaletteContextData& Out,
    FString& OutMessage)
{
    Out = FPaletteContextData();
    TArray<FString> Parts;
    Descriptor.ParseIntoArray(Parts, TEXT(";"), false);
    if (Parts.IsEmpty() || !(Parts[0] == TEXT("cs0") || Parts[0] == TEXT("cs1")))
    {
        OutMessage = TEXT("Palette id has an invalid context descriptor.");
        return false;
    }
    Out.bContextSensitive = Parts[0] == TEXT("cs1");
    Out.Descriptor = Parts[0];
    for (int32 Index = 1; Index < Parts.Num(); ++Index)
    {
        TArray<FString> Segments;
        Parts[Index].ParseIntoArray(Segments, TEXT("."), false);
        if (Segments.Num() == 3 && Segments[0] == TEXT("pin"))
        {
            UEdGraphPin* Pin = FindPin(Target.Graph, Segments[2]);
            const EEdGraphPinDirection Expected = Segments[1] == TEXT("from") ? EGPD_Output : EGPD_Input;
            if (Pin == nullptr || Pin->Direction != Expected)
            {
                OutMessage = TEXT("Palette Pin context is stale.");
                return false;
            }
            Out.Pins.Add(Pin);
        }
        else if (Segments.Num() == 2 && (Segments[0] == TEXT("widget") || Segments[0] == TEXT("component") || Segments[0] == TEXT("actor")))
        {
            FString StableId;
            if (!AddPaletteObjectContext(Target, Segments[0], Segments[0], Segments[1], Out, StableId, OutMessage)) return false;
        }
        else
        {
            OutMessage = TEXT("Palette id has an invalid context descriptor.");
            return false;
        }
        Out.Descriptor += TEXT(";") + Parts[Index];
    }
    return true;
}

void BuildActionMenu(
    const FSalResolvedTarget& Target,
    const TArray<UEdGraphPin*>& ContextPins,
    const TArray<FFieldVariant>& SelectedObjects,
    const bool bContextSensitive,
    FBlueprintActionMenuBuilder& Builder)
{
    FBlueprintActionContext Context;
    Context.Blueprints.Add(Target.Blueprint);
    Context.Graphs.Add(Target.Graph);
    Context.Pins.Append(ContextPins);
    Context.SelectedObjects.Append(SelectedObjects);
    const uint32 TargetMask =
        EContextTargetFlags::TARGET_Blueprint |
        EContextTargetFlags::TARGET_SubComponents |
        EContextTargetFlags::TARGET_NodeTarget |
        EContextTargetFlags::TARGET_PinObject |
        EContextTargetFlags::TARGET_SiblingPinObjects |
        EContextTargetFlags::TARGET_BlueprintLibraries;
    FBlueprintActionMenuUtils::MakeContextMenu(Context, bContextSensitive, TargetMask, Builder);
    if (Target.Graph != nullptr && Target.Graph->GetSchema() != nullptr)
    {
        Target.Graph->GetSchema()->InsertAdditionalActions(Context.Blueprints, Context.Graphs, Context.Pins, Builder);
    }
}

FString PaletteActionToken(const TSharedPtr<FEdGraphSchemaAction>& Action)
{
    if (!Action.IsValid() || Action->GetTypeId() != FBlueprintActionMenuItem::StaticGetTypeId())
    {
        return FString();
    }
    const FBlueprintActionMenuItem* Item = static_cast<const FBlueprintActionMenuItem*>(Action.Get());
    const UBlueprintNodeSpawner* Spawner = Item->GetRawAction();
    if (Spawner == nullptr)
    {
        return FString();
    }
    const FBlueprintNodeSignature Signature = Spawner->GetSpawnerSignature();
    if (Signature.IsValid()) return GuidText(Signature.AsGuid());

    FString Material = Spawner->GetClass()->GetPathName() + TEXT("|")
        + (Spawner->NodeClass != nullptr ? Spawner->NodeClass->GetPathName() : FString()) + TEXT("|")
        + Action->GetMenuDescription().ToString() + TEXT("|")
        + Action->GetCategory().ToString() + TEXT("|")
        + Action->GetTooltipDescription().ToString();
    if (const UBlueprintBoundEventNodeSpawner* EventSpawner = Cast<UBlueprintBoundEventNodeSpawner>(Spawner))
    {
        if (const FMulticastDelegateProperty* Delegate = EventSpawner->GetEventDelegate()) Material += TEXT("|") + Delegate->GetPathName();
    }
    return FString::Printf(TEXT("%08x%08x"), FCrc::StrCrc32(*Material), FCrc::StrCrc32(*Material, 0x9e3779b9));
}

FString PaletteId(const TSharedPtr<FEdGraphSchemaAction>& Action, const FString& Descriptor)
{
    const FString Token = PaletteActionToken(Action);
    return Token.IsEmpty() ? FString() : TEXT("P_") + Token + TEXT("|") + Descriptor;
}

bool SplitPaletteId(const FString& Id, FString& OutToken, FString& OutDescriptor)
{
    OutToken.Reset();
    OutDescriptor.Reset();
    if (!Id.StartsWith(TEXT("P_"))) return false;
    return Id.Mid(2).Split(TEXT("|"), &OutToken, &OutDescriptor)
        && !OutToken.IsEmpty() && !OutDescriptor.IsEmpty();
}

void BuildActionFilterTerms(
    const FString& Text,
    TArray<FString>& OutTerms,
    TArray<FString>& OutSanitizedTerms)
{
    OutTerms.Reset();
    OutSanitizedTerms.Reset();
    FString TrimmedText = Text;
    TrimmedText.TrimStartAndEndInline();
    TrimmedText.ParseIntoArray(OutTerms, TEXT(" "), true);
    for (FString& Term : OutTerms)
    {
        Term.ToLowerInline();
        FString Sanitized =
            FName::NameToDisplayString(Term, false);
        Sanitized.ReplaceInline(TEXT(" "), TEXT(""));
        OutSanitizedTerms.Add(MoveTemp(Sanitized));
    }
}

bool ActionMatches(
    const TSharedPtr<FEdGraphSchemaAction>& Action,
    const TArray<FString>& Terms,
    const TArray<FString>& SanitizedTerms)
{
    if (!Action.IsValid())
    {
        return false;
    }
    const FString& SearchText = Action->GetFullSearchText();
    for (int32 Index = 0; Index < Terms.Num(); ++Index)
    {
        if (!SearchText.Contains(Terms[Index], ESearchCase::CaseSensitive)
            && !SearchText.Contains(
                SanitizedTerms[Index],
                ESearchCase::CaseSensitive))
        {
            return false;
        }
    }
    return true;
}

int32 ActionTitleRank(
    const TSharedPtr<FEdGraphSchemaAction>& Action,
    const FString& Text)
{
    if (!Action.IsValid() || Text.IsEmpty())
    {
        return 0;
    }
    FString TrimmedText = Text;
    TrimmedText.TrimStartAndEndInline();
    const FText& MenuDescription = Action->GetMenuDescription();
    const TArray<FString> Titles = {
        MenuDescription.ToString(),
        MenuDescription.BuildSourceString()};
    int32 Rank = 2;
    for (const FString& Title : Titles)
    {
        if (Title.Equals(TrimmedText, ESearchCase::IgnoreCase))
        {
            return 0;
        }
        if (Title.StartsWith(TrimmedText, ESearchCase::IgnoreCase))
        {
            Rank = 1;
        }
    }
    return Rank;
}

TSharedPtr<FEdGraphSchemaAction> ResolvePaletteAction(const FSalResolvedTarget& Target, const FString& Id)
{
    FString Token;
    FString Descriptor;
    FPaletteContextData Context;
    FString Message;
    if (!SplitPaletteId(Id, Token, Descriptor) || !BuildPaletteContextFromDescriptor(Target, Descriptor, Context, Message)) return nullptr;
    FBlueprintActionMenuBuilder Builder;
    BuildActionMenu(Target, Context.Pins, Context.SelectedObjects, Context.bContextSensitive, Builder);
    for (int32 Index = 0; Index < Builder.GetNumActions(); ++Index)
    {
        TSharedPtr<FEdGraphSchemaAction> Action = Builder.GetSchemaAction(Index);
        if (PaletteActionToken(Action) == Token)
        {
            return Action;
        }
    }
    return nullptr;
}

UEdGraphNode* TemplateForAction(const TSharedPtr<FEdGraphSchemaAction>& Action, UEdGraph* Graph)
{
    if (!Action.IsValid() || Action->GetTypeId() != FBlueprintActionMenuItem::StaticGetTypeId())
    {
        return nullptr;
    }
    const FBlueprintActionMenuItem* Item = static_cast<const FBlueprintActionMenuItem*>(Action.Get());
    const UBlueprintNodeSpawner* Spawner = Item->GetRawAction();
    UEdGraphNode* Template =
        Spawner != nullptr ? Spawner->GetTemplateNode(Graph) : nullptr;
    // GetTemplateNode(TargetGraph) may produce a graph-specific template that
    // has not been primed. UE's own Blueprint action filter allocates the
    // default Pins before inspecting such a template.
    if (Template != nullptr && Template->Pins.IsEmpty())
    {
        Template->AllocateDefaultPins();
    }
    return Template;
}

const UK2Node_Event* ExistingBoundEvent(
    const TSharedPtr<FEdGraphSchemaAction>& Action,
    const FPaletteContextData& Context,
    UBlueprint* Blueprint)
{
    if (!Action.IsValid() || Action->GetTypeId() != FBlueprintActionMenuItem::StaticGetTypeId() || Context.SelectedObjects.Num() != 1) return nullptr;
    const FBlueprintActionMenuItem* Item = static_cast<const FBlueprintActionMenuItem*>(Action.Get());
    const UBlueprintBoundEventNodeSpawner* Spawner = Cast<UBlueprintBoundEventNodeSpawner>(Item->GetRawAction());
    if (Spawner == nullptr) return nullptr;
    IBlueprintNodeBinder::FBindingSet Bindings;
    Bindings.Add(FBindingObject(Context.SelectedObjects[0]));
    return Spawner->FindPreExistingEvent(Blueprint, Bindings);
}

FString BoundEventNavigation(const FSalResolvedTarget& Target, const UK2Node_Event* Event, const FString& Label)
{
    const UEdGraph* Graph = Event != nullptr ? Event->GetGraph() : nullptr;
    return FString::Printf(
        TEXT("%s already exists\ninspect with:\n  existingBlueprint = blueprint(asset: \"%s\", id: \"%s\")\n  existingGraph = graph(asset: existingBlueprint, id: \"%s\")\n  query existingGraph\n  node@%s"),
        *Label,
        *Target.AssetPath,
        Target.Blueprint != nullptr ? *GuidText(Target.Blueprint->GetBlueprintGuid()) : TEXT(""),
        Graph != nullptr ? *GuidText(Graph->GraphGuid) : TEXT(""),
        Event != nullptr ? *NodeId(Event) : TEXT(""));
}

TSharedPtr<FJsonObject> QueryPalette(const FSalQuery& Query, const FSalResolvedTarget& Target, const bool bExact)
{
    FString ExactId;
    FString ExactToken;
    FString ExactDescriptor;
    FString Text;
    if (bExact) Query.Operation->TryGetStringField(TEXT("id"), ExactId);
    else Query.Operation->TryGetStringField(TEXT("text"), Text);

    FPaletteContextData Context;
    TSharedPtr<FJsonObject> ContextError;
    if (bExact)
    {
        FString Message;
        if (!SplitPaletteId(ExactId, ExactToken, ExactDescriptor)
            || !BuildPaletteContextFromDescriptor(Target, ExactDescriptor, Context, Message))
        {
            return GraphErrorResult(TEXT("resolution.palette_not_found"), Message.IsEmpty() ? TEXT("Palette id is malformed or stale.") : Message, TEXT("palette"), ExactId, TEXT("Run palette entries again in the same Graph context."));
        }
    }
    else if (!BuildPaletteContextFromQuery(Query, Target, Context, ContextError))
    {
        return ContextError.IsValid() ? ContextError : GraphErrorResult(TEXT("validation.palette_context_invalid"), TEXT("Palette context could not be resolved."), TEXT("palette entries"));
    }

    FBlueprintActionMenuBuilder Menu;
    BuildActionMenu(Target, Context.Pins, Context.SelectedObjects, Context.bContextSensitive, Menu);
    TArray<FString> FilterTerms;
    TArray<FString> SanitizedFilterTerms;
    BuildActionFilterTerms(Text, FilterTerms, SanitizedFilterTerms);
    const UEdGraphSchema* ActionSchema =
        Target.Graph != nullptr ? Target.Graph->GetSchema() : nullptr;
    struct FMatch
    {
        TSharedPtr<FEdGraphSchemaAction> Action;
        int32 Index;
        int32 TitleRank;
        float Weight;
        FString Name;
        FString Category;
        FString Id;
    };
    TArray<FMatch> Matches;
    TArray<FString> ExistingNavigation;
    for (int32 Index = 0; Index < Menu.GetNumActions(); ++Index)
    {
        TSharedPtr<FEdGraphSchemaAction> Action = Menu.GetSchemaAction(Index);
        const FString Id = PaletteId(Action, Context.Descriptor);
        if (Id.IsEmpty()
            || (bExact && PaletteActionToken(Action) != ExactToken)
            || !ActionMatches(
                Action,
                FilterTerms,
                SanitizedFilterTerms))
        {
            continue;
        }
        const FString Label = Action->GetMenuDescription().ToString();
        if (const UK2Node_Event* Existing = ExistingBoundEvent(Action, Context, Target.Blueprint))
        {
            const FString Navigation = BoundEventNavigation(Target, Existing, Label);
            if (bExact)
            {
                return GraphErrorResult(
                    TEXT("resolution.palette_not_found"),
                    TEXT("Bound Event Palette entry is no longer available because the event already exists."),
                    TEXT("palette"),
                    ExactId,
                    Navigation);
            }
            ExistingNavigation.AddUnique(Navigation);
            continue;
        }
        const float Weight = FilterTerms.IsEmpty() || ActionSchema == nullptr
            ? 0.0f
            : ActionSchema->GetActionFilteredWeight(
                *Action,
                FilterTerms,
                SanitizedFilterTerms,
                Context.Pins);
        Matches.Add({
            Action,
            Index,
            ActionTitleRank(Action, Text),
            Weight,
            Label,
            Action->GetCategory().ToString(),
            Id});
    }
    if (Query.OrderBy.IsEmpty())
    {
        Matches.StableSort([](const FMatch& A, const FMatch& B)
        {
            if (A.TitleRank != B.TitleRank)
            {
                return A.TitleRank < B.TitleRank;
            }
            if (A.Weight != B.Weight)
            {
                return A.Weight > B.Weight;
            }
            return A.Index < B.Index;
        });
    }
    else
    {
        Matches.StableSort([&Query](const FMatch& A, const FMatch& B)
        {
            for (const TSharedPtr<FJsonObject>& Order : Query.OrderBy)
            {
                FString Key;
                FString Direction;
                Order->TryGetStringField(TEXT("key"), Key);
                Order->TryGetStringField(TEXT("direction"), Direction);
                const FString& Left = Key == TEXT("name") ? A.Name : (Key == TEXT("category") ? A.Category : A.Id);
                const FString& Right = Key == TEXT("name") ? B.Name : (Key == TEXT("category") ? B.Category : B.Id);
                const int32 Compare = Left.Compare(Right);
                if (Compare != 0) return Direction == TEXT("desc") ? Compare > 0 : Compare < 0;
            }
            return A.Index < B.Index;
        });
    }
    if (bExact && Matches.IsEmpty())
    {
        return GraphErrorResult(TEXT("resolution.palette_not_found"), TEXT("Palette entry was not found in the current Graph context."), TEXT("palette"), ExactId, TEXT("Run palette entries again in the same Graph context."));
    }

    FSalObjectBuilder Out;
    FSalPage Page;
    TSharedPtr<FJsonObject> PageError;
    if (!bExact && !DecodeGraphPage(Query, Target, Page, PageError)) return PageError;
    int32 Added = 0;
    for (int32 MatchIndex = bExact ? 0 : Page.Offset; MatchIndex < Matches.Num() && (bExact || Added < Page.Limit); ++MatchIndex)
    {
        const FMatch& Match = Matches[MatchIndex];
        const FString Label = Match.Action->GetMenuDescription().ToString();
        const FString Alias = Out.UniqueAlias(Label.IsEmpty() ? TEXT("entry") : Label);
        TSharedPtr<FJsonObject> Args = MakeShared<FJsonObject>();
        Args->SetStringField(TEXT("palette"), PaletteId(Match.Action, Context.Descriptor));
        if (UEdGraphNode* Template = TemplateForAction(Match.Action, Target.Graph))
        {
            Args->SetStringField(TEXT("type"), NodeType(Template));
        }
        Out.AddLocalBinding(Alias, Value::Call(TEXT("node"), Args));
        if (!Label.IsEmpty()) Out.AddComment(Label);
        const FString Category = Match.Action->GetCategory().ToString();
        if (!Category.IsEmpty()) Out.AddComment(FString::Printf(TEXT("Category: %s"), *Category));
        if (bExact)
        {
            if (UEdGraphNode* Template = TemplateForAction(Match.Action, Target.Graph))
            {
                TSet<FString> UsedMembers;
                for (UEdGraphPin* Pin : Template->Pins)
                {
                    const FString NativeName = Pin != nullptr ? Pin->PinName.ToString() : TEXT("pin");
                    const FString Member = UniqueMemberAlias(NativeName, UsedMembers);
                    TSharedPtr<FJsonValue> Encoded = PinValue(Pin, true);
                    const TSharedPtr<FJsonObject>* EncodedObject = nullptr;
                    if (Encoded->TryGetObject(EncodedObject) && EncodedObject != nullptr && Member != NativeName)
                    {
                        (*EncodedObject)->GetObjectField(TEXT("args"))->SetStringField(TEXT("PinName"), NativeName);
                    }
                    Out.AddMemberBinding(Alias, {Member}, Encoded);
                }
            }
            if (HasDetail(Query, TEXT("schema")))
            {
                TArray<FString> Fields = {TEXT("palette, type, future pins: read only")};
                if (Cast<UK2Node_Timeline>(TemplateForAction(Match.Action, Target.Graph)) != nullptr)
                {
                    Fields.Add(TEXT("TimelineName: required constructor field"));
                    Fields.Add(TEXT("TimelineLength, LengthMode, bAutoPlay, bLoop, bReplicated, bIgnoreTimeDilation, MetaDataArray, TimelineTickGroup: optional constructor fields"));
                    Fields.Add(TEXT("Track arrays and TrackDisplayOrder: unavailable in constructor; use invoke after add"));
                }
                AddSchemaComment(Out, TEXT("palette entry"), Fields, {TEXT("bind node(palette: ...) then add or insert")});
            }
        }
        ++Added;
        if (bExact) break;
    }
    for (const FString& Navigation : ExistingNavigation) Out.AddComment(Navigation);
    TSharedPtr<FJsonObject> Result = Out.BuildResult();
    if (!bExact)
    {
        SetGraphPage(Result, Query, Target, Page.Offset + Added, Page.Offset + Added < Matches.Num());
    }
    return Result;
}

TSharedPtr<FJsonObject> QueryNodes(const FSalQuery& Query, const FSalResolvedTarget& Target)
{
    FString Text;
    Query.Operation->TryGetStringField(TEXT("text"), Text);
    TArray<UEdGraphNode*> Nodes;
    for (UEdGraphNode* Node : Target.Graph->Nodes)
    {
        if (Node != nullptr && MatchesNodeText(Node, Text) && MatchesCondition(Node, Query.Where))
        {
            Nodes.Add(Node);
        }
    }
    if (!Query.OrderBy.IsEmpty())
    {
        Nodes.StableSort([&Query](const UEdGraphNode& A, const UEdGraphNode& B)
        {
            for (const TSharedPtr<FJsonObject>& Order : Query.OrderBy)
            {
                FString Key;
                FString Direction;
                Order->TryGetStringField(TEXT("key"), Key);
                Order->TryGetStringField(TEXT("direction"), Direction);
                const int32 Compare = ReadConditionValue(&A, Key).Compare(ReadConditionValue(&B, Key));
                if (Compare != 0) return Direction == TEXT("desc") ? Compare > 0 : Compare < 0;
            }
            return false;
        });
    }
    FSalPage Page;
    TSharedPtr<FJsonObject> PageError;
    if (!DecodeGraphPage(Query, Target, Page, PageError)) return PageError;
    FEncodedGraph Out(Target);
    int32 Added = 0;
    for (int32 Index = Page.Offset; Index < Nodes.Num() && Added < Page.Limit; ++Index, ++Added)
    {
        Out.AddNode(Nodes[Index], false, HasDetail(Query, TEXT("layout")));
    }
    TSharedPtr<FJsonObject> Result = Out.Builder.BuildResult();
    SetGraphPage(Result, Query, Target, Page.Offset + Added, Page.Offset + Added < Nodes.Num());
    return Result;
}

bool IsDynamicPinRemovable(UEdGraphPin* Pin, FString& OutOperation)
{
    OutOperation.Reset();
    if (Pin == nullptr) return false;
    UEdGraphNode* Node = Pin->GetOwningNode();
    if (UK2Node_ExecutionSequence* Sequence = Cast<UK2Node_ExecutionSequence>(Node))
    {
        if (Pin->Direction == EGPD_Output && IsExecPin(Pin) && Sequence->CanRemoveExecutionPin()) { OutOperation = TEXT("RemoveExecutionPin()"); return true; }
        return false;
    }
    if (UK2Node_Switch* Switch = Cast<UK2Node_Switch>(Node))
    {
        if (Switch->CanRemoveExecutionPin(Pin)) { OutOperation = TEXT("RemoveExecutionPin()"); return true; }
        return false;
    }
    IK2Node_AddPinInterface* AddPin = Cast<IK2Node_AddPinInterface>(Node);
    if (AddPin == nullptr || !AddPin->CanRemovePin(Pin)) return false;
    if (Cast<UK2Node_MakeArray>(Node) != nullptr) OutOperation = TEXT("RemoveArrayElementPin()");
    else if (Cast<UK2Node_MakeSet>(Node) != nullptr) OutOperation = TEXT("RemoveSetElementPin()");
    else if (Cast<UK2Node_MakeMap>(Node) != nullptr) OutOperation = TEXT("RemoveKeyValuePair()");
    else if (Cast<UK2Node_CommutativeAssociativeBinaryOperator>(Node) != nullptr
        || Cast<UK2Node_PromotableOperator>(Node) != nullptr
        || Cast<UK2Node_DoOnceMultiInput>(Node) != nullptr) OutOperation = TEXT("RemoveInputPin()");
    return !OutOperation.IsEmpty();
}

void AddExactNodeSchema(UEdGraphNode* Node, TArray<FString>& Fields, TArray<FString>& Operations)
{
    Fields = {TEXT("id, type, graph: read only"), TEXT("at, size: layout only")};
    if (UK2Node_Timeline* TimelineNode = Cast<UK2Node_Timeline>(Node))
    {
        Fields.Add(TEXT("TimelineName: read/write"));
        Fields.Add(TEXT("TimelineLength, LengthMode, bAutoPlay, bLoop, bReplicated, bIgnoreTimeDilation, MetaDataArray, TimelineTickGroup: read/write/reset"));
        Fields.Add(TEXT("EventTracks, FloatTracks, VectorTracks, LinearColorTracks, TrackDisplayOrder: read only"));
        Operations.Append({
            TEXT("AddFloatTrack(TrackName) -> pin"), TEXT("AddVectorTrack(TrackName) -> pin"),
            TEXT("AddEventTrack(TrackName) -> pin"), TEXT("AddLinearColorTrack(TrackName) -> pin"),
            TEXT("AddTrackFromCurve(Curve) -> pin")
        });
        UTimelineTemplate* Timeline = FindTimelineTemplate(FBlueprintEditorUtils::FindBlueprintForNode(TimelineNode), TimelineNode);
        const int32 TrackCount = Timeline != nullptr ? Timeline->EventTracks.Num() + Timeline->FloatTracks.Num() + Timeline->VectorTracks.Num() + Timeline->LinearColorTracks.Num() : 0;
        bool bHasInternal = false;
        bool bHasExternal = false;
        if (Timeline != nullptr)
        {
            auto ReadOwnership = [&](const FTTTrackBase& Track) { bHasExternal |= Track.bIsExternalCurve; bHasInternal |= !Track.bIsExternalCurve; };
            for (const FTTEventTrack& Track : Timeline->EventTracks) ReadOwnership(Track);
            for (const FTTFloatTrack& Track : Timeline->FloatTracks) ReadOwnership(Track);
            for (const FTTVectorTrack& Track : Timeline->VectorTracks) ReadOwnership(Track);
            for (const FTTLinearColorTrack& Track : Timeline->LinearColorTracks) ReadOwnership(Track);
        }
        if (TrackCount > 0)
        {
            Operations.Add(TEXT("RenameTrack(TrackName, NewName)"));
            if (TrackCount > 1) Operations.Add(TEXT("MoveTrack(TrackName, Before|After)"));
            Operations.Add(TEXT("RemoveTrack(TrackName)"));
            Operations.Add(TEXT("UseExternalCurve(TrackName, Curve)"));
        }
        if (bHasInternal)
        {
            Operations.Add(TEXT("AddKey(TrackName, Channel?, Time, Value?, native FRichCurveKey fields?)"));
            Operations.Add(TEXT("SetKey(TrackName, Channel?, Time, NewTime?, Value?, native FRichCurveKey fields?)"));
            Operations.Add(TEXT("RemoveKey(TrackName, Channel?, Time)"));
        }
        if (bHasExternal) Operations.Add(TEXT("UseInternalCurve(TrackName)"));
        if (TimelineNode->CanDuplicateNode()) Operations.Add(TEXT("Duplicate() -> node"));
    }
    for (TFieldIterator<FProperty> It(Node->GetClass()); It; ++It)
    {
        FProperty* Property = *It;
        const FNodePropertyAccess Access = GetNodePropertyAccess(Node, Property);
        if (!Access.bReadable) continue;
        const FString Availability = !Access.bWritable
            ? TEXT("read only")
            : Access.bResettable ? TEXT("read/write/reset") : TEXT("read/write");
        Fields.Add(Property->GetName() + TEXT(": ") + Availability);
    }
    if (Node->CanUserDeleteNode()) Operations.Add(TEXT("remove"));
    Operations.Add(TEXT("move to (x, y) | move by (dx, dy)"));
    if (UK2Node_CustomEvent* Event = Cast<UK2Node_CustomEvent>(Node); Event != nullptr && Event->bIsEditable)
    {
        Operations.Add(TEXT("AddParameter(name, type) -> pin"));
    }
    if (UK2Node_ExecutionSequence* Sequence = Cast<UK2Node_ExecutionSequence>(Node))
    {
        IK2Node_AddPinInterface* AddPin = Cast<IK2Node_AddPinInterface>(Sequence);
        if (AddPin != nullptr && AddPin->CanAddPin()) Operations.Add(TEXT("AddExecutionPin() -> pin"));
    }
    else if (UK2Node_Switch* Switch = Cast<UK2Node_Switch>(Node))
    {
        if (Switch->SupportsAddPinButton()) Operations.Add(TEXT("AddExecutionPin() -> pin"));
    }
    else if (UK2Node_Select* Select = Cast<UK2Node_Select>(Node))
    {
        IK2Node_AddPinInterface* AddPin = Cast<IK2Node_AddPinInterface>(Select);
        if (AddPin != nullptr && AddPin->CanAddPin()) Operations.Add(TEXT("AddOptionPin() -> pin"));
        if (Select->CanRemoveOptionPinToNode()) Operations.Add(TEXT("RemoveOptionPin()"));
    }
    else if (IK2Node_AddPinInterface* AddPin = Cast<IK2Node_AddPinInterface>(Node); AddPin != nullptr && AddPin->CanAddPin())
    {
        if (Cast<UK2Node_MakeArray>(Node) != nullptr) Operations.Add(TEXT("AddArrayElementPin() -> pin"));
        else if (Cast<UK2Node_MakeSet>(Node) != nullptr) Operations.Add(TEXT("AddSetElementPin() -> pin"));
        else if (Cast<UK2Node_MakeMap>(Node) != nullptr) Operations.Add(TEXT("AddKeyValuePair() -> key, value"));
        else if (Cast<UK2Node_DoOnceMultiInput>(Node) != nullptr) Operations.Add(TEXT("AddInputPin() -> input, output"));
        else if (Cast<UK2Node_CommutativeAssociativeBinaryOperator>(Node) != nullptr || Cast<UK2Node_PromotableOperator>(Node) != nullptr) Operations.Add(TEXT("AddInputPin() -> pin"));
    }
}

void AddExactPinSchema(UEdGraphPin* Pin, TArray<FString>& Fields, TArray<FString>& Operations)
{
    Fields = {TEXT("id, type, direction, ParentPin, PersistentGuid and structural flags: read only")};
    TArray<UK2Node_EditablePinBase*> SignatureOwners;
    SignatureOwnersForPin(Pin, SignatureOwners);
    if (!SignatureOwners.IsEmpty())
    {
        Fields.Add(TEXT("PinName, type: read/write"));
        Fields.Add(Pin->bDefaultValueIsReadOnly ? TEXT("DefaultValue: read only") : TEXT("DefaultValue: read/write/reset"));
        Operations.Add(TEXT("RemoveParameter()"));
        Operations.Add(TEXT("MoveParameterBefore(anchor: pin)"));
        Operations.Add(TEXT("MoveParameterAfter(anchor: pin)"));
    }
    else
    {
        if (!Pin->bDefaultValueIsReadOnly) Fields.Add(TEXT("DefaultValue, DefaultObject, DefaultTextValue: read/write/reset"));
        Fields.Add(TEXT("AutogeneratedDefaultValue: read only"));
    }
    Operations.Add(TEXT("break"));
    Operations.Add(TEXT("connect/disconnect when direction and Graph Schema permit"));
    FString DynamicRemove;
    if (IsDynamicPinRemovable(Pin, DynamicRemove)) Operations.Add(DynamicRemove);
    if (UK2Node_ExecutionSequence* Sequence = Cast<UK2Node_ExecutionSequence>(Pin->GetOwningNode()))
    {
        if (Pin->Direction == EGPD_Output && IsExecPin(Pin))
        {
            Operations.Add(TEXT("InsertExecutionPinBefore() -> pin"));
            Operations.Add(TEXT("InsertExecutionPinAfter() -> pin"));
        }
    }
    const UEdGraphSchema_K2* K2Schema = GetDefault<UEdGraphSchema_K2>();
    if (Pin->ParentPin == nullptr && K2Schema->CanSplitStructPin(*Pin)) Operations.Add(TEXT("SplitStructPin() -> subpins.<native member>"));
    if (Pin->ParentPin != nullptr && K2Schema->CanRecombineStructPin(*Pin)) Operations.Add(TEXT("RecombineStructPin() -> parent"));
}

void AddGraphSignatureSchema(const FSalResolvedTarget& Target, TArray<FString>& Operations)
{
    if (Target.Graph == nullptr) return;
    FString Error;
    UK2Node_EditablePinBase* Input = FindSignatureNode(Target.Graph, false, false, Error);
    if (Input == nullptr || !Input->bIsEditable) return;
    if (FBlueprintEditorUtils::IsDelegateSignatureGraph(Target.Graph))
    {
        TArray<UK2Node_FunctionEntry*> Entries;
        TArray<UK2Node_FunctionResult*> Results;
        Target.Graph->GetNodesOfClass(Entries);
        Target.Graph->GetNodesOfClass(Results);
        UClass* Skeleton = Target.Blueprint != nullptr ? Target.Blueprint->SkeletonGeneratedClass.Get() : nullptr;
        FMulticastDelegateProperty* Delegate = Skeleton != nullptr ? FindFProperty<FMulticastDelegateProperty>(Skeleton, Target.Graph->GetFName()) : nullptr;
        if (Entries.Num() == 1 && Results.IsEmpty() && Delegate != nullptr)
        {
            Operations.Add(TEXT("AddInputParameter(name, type) -> pin"));
            Operations.Add(TEXT("CopySignatureFrom(function)"));
        }
        return;
    }
    Operations.Add(TEXT("AddInputParameter(name, type) -> pin"));
    if (Cast<UK2Node_FunctionEntry>(Input) != nullptr)
    {
        Operations.Add(TEXT("AddOutputParameter(name, type) -> pin, result"));
        return;
    }
    FString OutputError;
    if (FindSignatureNode(Target.Graph, true, false, OutputError) != nullptr)
    {
        Operations.Add(TEXT("AddOutputParameter(name, type) -> pin"));
    }
}

void AddExactNode(FEncodedGraph& Out, UEdGraphNode* Node, const bool bLayout, const bool bSchema)
{
    const FString Alias = Out.AddNode(Node, true, bLayout);
    TSet<FString> UsedMembers;
    for (UEdGraphPin* Pin : Node->Pins)
    {
        Out.AddPin(Pin, Alias, UsedMembers);
    }
    if (bSchema)
    {
        TArray<FString> Fields;
        TArray<FString> Operations;
        AddExactNodeSchema(Node, Fields, Operations);
        AddSchemaComment(Out.Builder, NodeType(Node), Fields, Operations);
    }
}

TSharedPtr<FJsonObject> QueryExact(const FSalQuery& Query, const FSalResolvedTarget& Target, const FString& Kind)
{
    FString Id;
    Query.Operation->TryGetStringField(TEXT("id"), Id);
    FEncodedGraph Out(Target, Kind == TEXT("graph"));
    if (Kind == TEXT("graph"))
    {
        if (!Target.Id.Equals(Id, ESearchCase::IgnoreCase))
        {
            return GraphErrorResult(TEXT("resolution.graph_not_found"), TEXT("The scoped Graph id does not match the bound Graph."), TEXT("graph"), Id);
        }
        if (HasDetail(Query, TEXT("schema")))
        {
            TArray<FString> Operations = {TEXT("summary"), TEXT("nodes"), TEXT("node@id"), TEXT("pin@id"), TEXT("context"), TEXT("exec flow"), TEXT("data flow"), TEXT("palette entries"), TEXT("palette @id")};
            AddGraphSignatureSchema(Target, Operations);
            AddSchemaComment(
                Out.Builder,
                TEXT("graph"),
                {TEXT("id: read only"), TEXT("name: read only"), TEXT("type: read only")},
                Operations);
        }
        return Out.Builder.BuildResult();
    }
    if (Kind == TEXT("node"))
    {
        UEdGraphNode* Node = FindNode(Target.Graph, Id);
        if (Node == nullptr) return GraphErrorResult(TEXT("resolution.node_not_found"), TEXT("Node was not found in the bound Graph."), TEXT("node"), Id);
        if (UK2Node_Timeline* Timeline = Cast<UK2Node_Timeline>(Node))
        {
            FString PairError;
            UTimelineTemplate* Template = FindTimelineTemplate(Target.Blueprint, Timeline, &PairError);
            if (Template == nullptr)
            {
                return GraphErrorResult(TEXT("validation.timeline_inconsistent"), PairError, TEXT("node"), Id);
            }
            if (!ValidateTimelineStructure(Timeline, Template, PairError))
            {
                return GraphErrorResult(TEXT("validation.timeline_inconsistent"), PairError, TEXT("node"), Id);
            }
        }
        AddExactNode(Out, Node, HasDetail(Query, TEXT("layout")), HasDetail(Query, TEXT("schema")));
        return Out.Builder.BuildResult();
    }
    bool bAmbiguous = false;
    UEdGraphPin* Pin = FindPin(Target.Graph, Id, &bAmbiguous);
    if (Pin == nullptr)
    {
        return GraphErrorResult(
            bAmbiguous ? TEXT("resolution.pin_ambiguous") : TEXT("resolution.pin_not_found"),
            bAmbiguous
                ? TEXT("PinId matches multiple Pins in the bound Graph.")
                : TEXT("Pin was not found in the bound Graph."),
            TEXT("pin"),
            Id);
    }
    UEdGraphNode* Owner = Pin->GetOwningNode();
    const FString Alias = Out.AddNode(Owner, false, HasDetail(Query, TEXT("layout")));
    TSet<FString> UsedMembers;
    Out.AddPin(Pin, Alias, UsedMembers);
    if (HasDetail(Query, TEXT("schema")))
    {
        TArray<FString> Fields;
        TArray<FString> Operations;
        AddExactPinSchema(Pin, Fields, Operations);
        AddSchemaComment(Out.Builder, TEXT("pin"), Fields, Operations);
    }
    return Out.Builder.BuildResult();
}

struct FTraversal
{
    TSet<UEdGraphNode*> Nodes;
    TSet<UEdGraphPin*> Pins;
    TArray<TPair<UEdGraphPin*, UEdGraphPin*>> Edges;
    bool bPinAmbiguous = false;
};

void AddUniqueEdge(FTraversal& Traversal, UEdGraphPin* A, UEdGraphPin* B)
{
    if (A == nullptr || B == nullptr)
    {
        return;
    }
    UEdGraphPin* From = A->Direction == EGPD_Output ? A : B;
    UEdGraphPin* To = A->Direction == EGPD_Input ? A : B;
    for (const TPair<UEdGraphPin*, UEdGraphPin*>& Edge : Traversal.Edges)
    {
        if (Edge.Key == From && Edge.Value == To) return;
    }
    Traversal.Edges.Add({From, To});
    Traversal.Pins.Add(A);
    Traversal.Pins.Add(B);
}

bool ReadSubject(
    const TSharedPtr<FJsonObject>& Operation,
    UEdGraph* Graph,
    UEdGraphNode*& OutNode,
    UEdGraphPin*& OutPin,
    FString& OutId,
    bool& OutPinAmbiguous)
{
    OutNode = nullptr;
    OutPin = nullptr;
    OutId.Reset();
    OutPinAmbiguous = false;
    const TSharedPtr<FJsonObject>* Subject = nullptr;
    if (!Operation->TryGetObjectField(TEXT("target"), Subject) || Subject == nullptr)
    {
        return false;
    }
    FString Kind;
    (*Subject)->TryGetStringField(TEXT("kind"), Kind);
    (*Subject)->TryGetStringField(TEXT("id"), OutId);
    if (Kind == TEXT("node")) OutNode = FindNode(Graph, OutId);
    else if (Kind == TEXT("pin"))
    {
        OutPin = FindPin(Graph, OutId, &OutPinAmbiguous);
        OutNode = OutPin != nullptr ? OutPin->GetOwningNode() : nullptr;
    }
    return OutNode != nullptr;
}

FTraversal Traverse(const FSalQuery& Query, const FSalResolvedTarget& Target, const FString& Kind, FString& OutError)
{
    FTraversal Result;
    UEdGraphNode* SeedNode = nullptr;
    UEdGraphPin* SeedPin = nullptr;
    FString Id;
    bool bPinAmbiguous = false;
    if (!ReadSubject(Query.Operation, Target.Graph, SeedNode, SeedPin, Id, bPinAmbiguous))
    {
        Result.bPinAmbiguous = bPinAmbiguous;
        OutError = bPinAmbiguous
            ? FString::Printf(TEXT("PinId matches multiple Pins in the bound Graph: %s"), *Id)
            : FString::Printf(TEXT("Traversal target was not found: %s"), *Id);
        return Result;
    }
    FString Direction;
    Query.Operation->TryGetStringField(TEXT("direction"), Direction);
    double DepthNumber = 1.0;
    Query.Operation->TryGetNumberField(TEXT("depth"), DepthNumber);
    const int32 MaxDepth = FMath::Max(1, static_cast<int32>(DepthNumber));
    const bool bFlow = Kind == TEXT("exec_flow") || Kind == TEXT("data_flow");
    const bool bExec = Kind == TEXT("exec_flow");
    const EEdGraphPinDirection TraverseDirection = Direction == TEXT("to") ? EGPD_Input : EGPD_Output;

    if (bFlow && SeedPin != nullptr)
    {
        if (SeedPin->Direction != TraverseDirection || IsExecPin(SeedPin) != bExec)
        {
            OutError = TEXT("Traversal Pin type or direction does not match the requested flow.");
            return Result;
        }
    }

    Result.Nodes.Add(SeedNode);
    if (SeedPin != nullptr) Result.Pins.Add(SeedPin);
    struct FQueueItem { UEdGraphNode* Node; UEdGraphPin* FirstPin; int32 Depth; };
    TArray<FQueueItem> Queue;
    Queue.Add({SeedNode, SeedPin, 0});
    TMap<UEdGraphNode*, int32> SeenDepth;
    SeenDepth.Add(SeedNode, 0);
    for (int32 QueueIndex = 0; QueueIndex < Queue.Num(); ++QueueIndex)
    {
        const FQueueItem Item = Queue[QueueIndex];
        TArray<UEdGraphPin*> CandidatePins;
        if (Item.Depth == 0 && Item.FirstPin != nullptr) CandidatePins.Add(Item.FirstPin);
        else CandidatePins = Item.Node->Pins;
        for (UEdGraphPin* Pin : CandidatePins)
        {
            if (Pin == nullptr) continue;
            if (bFlow && (Pin->Direction != TraverseDirection || IsExecPin(Pin) != bExec)) continue;
            if (Kind == TEXT("data_flow") && Direction == TEXT("to") && Pin->Direction == EGPD_Input && Pin->LinkedTo.IsEmpty())
            {
                Result.Pins.Add(Pin);
            }
            for (UEdGraphPin* Linked : Pin->LinkedTo)
            {
                if (Linked == nullptr) continue;
                if (bFlow && IsExecPin(Linked) != bExec) continue;
                UEdGraphNode* Next = Linked->GetOwningNode();
                if (Next == nullptr) continue;
                const int32 NextDepth = Item.Depth + 1;
                if (NextDepth > MaxDepth)
                {
                    Result.Pins.Add(Pin);
                    continue;
                }
                AddUniqueEdge(Result, Pin, Linked);
                Result.Nodes.Add(Next);
                const int32* Previous = SeenDepth.Find(Next);
                if (Previous == nullptr || NextDepth < *Previous)
                {
                    SeenDepth.Add(Next, NextDepth);
                    Queue.Add({Next, nullptr, NextDepth});
                }
            }
        }
    }
    return Result;
}

TSharedPtr<FJsonObject> EncodeTraversal(const FSalQuery& Query, const FSalResolvedTarget& Target, const FString& Kind)
{
    FString Error;
    const FTraversal Traversal = Traverse(Query, Target, Kind, Error);
    if (!Error.IsEmpty())
    {
        return GraphErrorResult(
            Traversal.bPinAmbiguous
                ? TEXT("resolution.pin_ambiguous")
                : TEXT("resolution.invalid_traversal_target"),
            Error,
            Kind);
    }
    FEncodedGraph Out(Target);
    TMap<UEdGraphNode*, TSet<FString>> UsedMembers;
    for (UEdGraphNode* Node : Target.Graph->Nodes)
    {
        if (Node != nullptr && Traversal.Nodes.Contains(Node))
        {
            const FString Alias = Out.AddNode(Node, false, HasDetail(Query, TEXT("layout")));
            TSet<FString>& Used = UsedMembers.FindOrAdd(Node);
            for (UEdGraphPin* Pin : Node->Pins)
            {
                if (Traversal.Pins.Contains(Pin))
                {
                    Out.AddPin(Pin, Alias, Used);
                }
            }
        }
    }
    for (const TPair<UEdGraphPin*, UEdGraphPin*>& Edge : Traversal.Edges)
    {
        const TSharedPtr<FJsonObject>* From = Out.PinRefs.Find(Edge.Key);
        const TSharedPtr<FJsonObject>* To = Out.PinRefs.Find(Edge.Value);
        if (From != nullptr && To != nullptr) Out.Builder.AddEdge(*From, *To);
    }
    if (Kind == TEXT("data_flow"))
    {
        FString Direction;
        Query.Operation->TryGetStringField(TEXT("direction"), Direction);
        if (Direction == TEXT("from")) Out.Builder.AddComment(TEXT("Data flow from is conservative node-level impact analysis across wired outputs."));
    }
    return Out.Builder.BuildResult();
}

void AddGraphHealthIndex(FEncodedGraph& Out, const UEdGraph* Graph)
{
    if (Graph == nullptr) return;
    TArray<FString> Lines = {TEXT("UE graph diagnostics")};
    bool bHasCompilerMessage = false;
    for (const UEdGraphNode* Node : Graph->Nodes)
    {
        bool bNodeHasCompilerMessage = false;
        const TArray<FString> Labels = NodeHealthLabels(Node, bNodeHasCompilerMessage);
        if (Labels.IsEmpty()) continue;
        const FString Id = NodeId(Node);
        if (Id.IsEmpty()) continue;
        Lines.Add(TEXT("node@") + Id + TEXT(": ") + FString::Join(Labels, TEXT(", ")));
        bHasCompilerMessage |= bNodeHasCompilerMessage;
    }
    if (Lines.Num() == 1) return;

    const FString Status = bHasCompilerMessage ? StaleBlueprintStatusText(Out.Blueprint) : FString();
    if (!Status.IsEmpty())
    {
        Lines.Add(FString::Printf(
            TEXT("compiler messages may be stale: owning Blueprint Status is %s; run an explicit Blueprint compile to refresh them"),
            *Status));
        Out.bAddedCompilerStaleNote = true;
    }
    Out.Builder.AddComment(FString::Join(Lines, TEXT("\n")));
}

TSharedPtr<FJsonObject> QuerySummary(const FSalResolvedTarget& Target)
{
    FEncodedGraph Out(Target, false, false);
    TArray<UEdGraphNode*> SemanticNodes;
    TArray<UEdGraphNode*> Entries;
    int32 EdgeCount = 0;
    for (UEdGraphNode* Node : Target.Graph->Nodes)
    {
        if (Node == nullptr || Node->Pins.IsEmpty()) continue;
        SemanticNodes.Add(Node);
        bool bHasExecOutput = false;
        bool bHasLinkedExecInput = false;
        for (UEdGraphPin* Pin : Node->Pins)
        {
            if (Pin == nullptr) continue;
            if (Pin->Direction == EGPD_Output) EdgeCount += Pin->LinkedTo.Num();
            if (IsExecPin(Pin) && Pin->Direction == EGPD_Output) bHasExecOutput = true;
            if (IsExecPin(Pin) && Pin->Direction == EGPD_Input && !Pin->LinkedTo.IsEmpty()) bHasLinkedExecInput = true;
        }
        if (bHasExecOutput && !bHasLinkedExecInput) Entries.Add(Node);
    }
    TSet<UEdGraphNode*> Covered;
    for (UEdGraphNode* Entry : Entries)
    {
        Out.AddNode(Entry, false, false);
        TArray<UEdGraphNode*> Queue = {Entry};
        Covered.Add(Entry);
        for (int32 Index = 0; Index < Queue.Num(); ++Index)
        {
            for (UEdGraphPin* Pin : Queue[Index]->Pins)
            {
                for (UEdGraphPin* Linked : Pin->LinkedTo)
                {
                    UEdGraphNode* Next = Linked != nullptr ? Linked->GetOwningNode() : nullptr;
                    if (Next != nullptr && !Covered.Contains(Next)) { Covered.Add(Next); Queue.Add(Next); }
                }
            }
        }
    }
    int32 DisconnectedRegions = 0;
    for (UEdGraphNode* Candidate : SemanticNodes)
    {
        if (Covered.Contains(Candidate)) continue;
        TArray<UEdGraphNode*> Queue = {Candidate};
        Covered.Add(Candidate);
        int32 RegionSize = 0;
        for (int32 Index = 0; Index < Queue.Num(); ++Index)
        {
            ++RegionSize;
            for (UEdGraphPin* Pin : Queue[Index]->Pins)
            {
                for (UEdGraphPin* Linked : Pin->LinkedTo)
                {
                    UEdGraphNode* Next = Linked != nullptr ? Linked->GetOwningNode() : nullptr;
                    if (Next != nullptr && !Covered.Contains(Next)) { Covered.Add(Next); Queue.Add(Next); }
                }
            }
        }
        Out.AddNode(Candidate, false, false);
        Out.Builder.AddComment(FString::Printf(TEXT("Disconnected semantic region: %d node%s"), RegionSize, RegionSize == 1 ? TEXT("") : TEXT("s")));
        ++DisconnectedRegions;
    }
    AddGraphHealthIndex(Out, Target.Graph);
    Out.Builder.AddComment(FString::Printf(
        TEXT("summary:\n  nodes: %d\n  edges: %d\n  entry nodes: %d\n  disconnected regions: %d"),
        Target.Graph->Nodes.Num(),
        EdgeCount,
        Entries.Num(),
        DisconnectedRegions));
    return Out.Builder.BuildResult();
}

TSharedPtr<FJsonObject> BuildTouchedObject(const FPatchState& State)
{
    FEncodedGraph Out(State.Target);
    TMap<UEdGraphNode*, TSet<FString>> UsedMembers;
    for (UEdGraphNode* Node : State.Target.Graph->Nodes)
    {
        if (Node != nullptr && State.TouchedNodes.Contains(Node))
        {
            const FString Alias = Out.AddNode(Node, true, true);
            TSet<FString>& Used = UsedMembers.FindOrAdd(Node);
            for (UEdGraphPin* Pin : Node->Pins) Out.AddPin(Pin, Alias, Used);
        }
    }
    TSet<FString> AddedEdges;
    for (UEdGraphNode* Node : State.Target.Graph->Nodes)
    {
        if (Node == nullptr || !State.TouchedNodes.Contains(Node)) continue;
        for (UEdGraphPin* Pin : Node->Pins)
        {
            if (Pin == nullptr) continue;
            for (UEdGraphPin* Linked : Pin->LinkedTo)
            {
                if (Linked == nullptr) continue;
                UEdGraphPin* From = Pin->Direction == EGPD_Output ? Pin : Linked;
                UEdGraphPin* To = Pin->Direction == EGPD_Input ? Pin : Linked;
                const FString Key = PinId(From) + TEXT("->") + PinId(To);
                if (AddedEdges.Contains(Key)) continue;
                AddedEdges.Add(Key);
                for (UEdGraphPin* Endpoint : {From, To})
                {
                    UEdGraphNode* Owner = Endpoint->GetOwningNode();
                    const FString Alias = Out.AddNode(Owner, State.TouchedNodes.Contains(Owner), true);
                    Out.AddPin(Endpoint, Alias, UsedMembers.FindOrAdd(Owner));
                }
                Out.Builder.AddEdge(Out.PinRefs.FindChecked(From), Out.PinRefs.FindChecked(To));
            }
        }
    }
    return Out.Builder.BuildObject();
}
}

TSharedPtr<FJsonObject> FSalGraphInterface::Query(const FSalQuery& Query, const FSalResolvedTarget& Target)
{
    if (Target.Graph == nullptr || !Query.Operation.IsValid())
    {
        return GraphErrorResult(TEXT("resolution.graph_not_found"), TEXT("A resolved Graph is required."), TEXT("query"));
    }
    FString Kind;
    Query.Operation->TryGetStringField(TEXT("kind"), Kind);
    if (TSharedPtr<FJsonObject> CapabilityError = ValidateGraphQuery(Query, Kind)) return CapabilityError;
    if (Kind == TEXT("summary")) return QuerySummary(Target);
    if (Kind == TEXT("nodes")) return QueryNodes(Query, Target);
    if (Kind == TEXT("graph") || Kind == TEXT("node") || Kind == TEXT("pin")) return QueryExact(Query, Target, Kind);
    if (Kind == TEXT("context") || Kind == TEXT("exec_flow") || Kind == TEXT("data_flow")) return EncodeTraversal(Query, Target, Kind);
    if (Kind == TEXT("palette_entries")) return QueryPalette(Query, Target, false);
    if (Kind == TEXT("palette")) return QueryPalette(Query, Target, true);
    return GraphErrorResult(
        TEXT("capability.operation_unavailable"),
        FString::Printf(TEXT("Graph does not support query operation %s."), *Kind),
        Kind,
        FString(),
        TEXT("Use graph@id with schema or sal_schema({ module: \"graph\" }) to inspect supported operations."));
}

// Patch implementation follows the query implementation so the same live
// identity, Palette, Pin, and native property helpers are shared.

}

#undef LOCTEXT_NAMESPACE
