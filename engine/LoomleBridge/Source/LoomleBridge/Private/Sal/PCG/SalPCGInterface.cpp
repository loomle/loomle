// Copyright 2026 Loomle contributors.

#include "SalPCGInterface.h"

#include "../SalDiagnostics.h"
#include "../SalObjectBuilder.h"
#include "../SalRuntime.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "FileHelpers.h"
#include "HAL/FileManager.h"
#include "Misc/Crc.h"
#include "Misc/PackageName.h"
#include "Misc/SecureHash.h"
#include "PCGCommon.h"
#include "PCGEdge.h"
#include "PCGGraph.h"
#include "PCGNode.h"
#include "PCGPin.h"
#include "PCGSettings.h"
#include "UObject/Package.h"
#include "UObject/UnrealType.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/UObjectHash.h"

namespace Loomle::Sal
{
namespace
{
constexpr int32 DefaultCollectionLimit = 50;
constexpr int32 MaxCollectionLimit = 200;
constexpr int32 MaxSettingsProperties = 32;
constexpr int32 MaxSettingsTextChars = 8192;
constexpr int32 MaxQueryDiagnostics = 64;

bool IsBoundGraphNode(const UPCGNode* Node, const UPCGGraph* Graph)
{
    return Node != nullptr
        && Graph != nullptr
        && Node->GetGraph() == Graph
        && Node->IsIn(Graph);
}

UPCGGraph* ResolvedGraph(const FSalResolvedTarget& Target)
{
    return Target.Domain == ESalDomain::Pcg
        ? Cast<UPCGGraph>(Target.Object)
        : nullptr;
}

TSharedPtr<FJsonObject> QueryError(
    const FString& Code,
    const FString& Message,
    const FString& Operation = FString(),
    const FString& Ref = FString(),
    const TArray<FString>& Supported = {})
{
    FSalDiagnosticBuilder Diagnostic = FSalDiagnostics::Error(Code, Message)
        .Interface(TEXT("pcg"));
    if (!Operation.IsEmpty())
    {
        Diagnostic.Operation(Operation);
    }
    if (!Ref.IsEmpty())
    {
        Diagnostic.Ref(Ref);
    }
    if (!Supported.IsEmpty())
    {
        Diagnostic.Supported(Supported);
    }
    return FSalDiagnostics::Result(Diagnostic.Build());
}

TSharedPtr<FJsonObject> Warning(
    const FString& Code,
    const FString& Message,
    const FString& Operation,
    const FString& Ref = FString())
{
    FSalDiagnosticBuilder Diagnostic = FSalDiagnostics::Warning(Code, Message)
        .Interface(TEXT("pcg"))
        .Operation(Operation);
    if (!Ref.IsEmpty())
    {
        Diagnostic.Ref(Ref);
    }
    return Diagnostic.Build();
}

TArray<UPCGNode*> GraphNodes(UPCGGraph* Graph)
{
    TArray<UPCGNode*> Result;
    TSet<const UPCGNode*> Seen;
    if (Graph == nullptr)
    {
        return Result;
    }
    if (UPCGNode* Input = Graph->GetInputNode())
    {
        if (IsBoundGraphNode(Input, Graph))
        {
            Result.Add(Input);
            Seen.Add(Input);
        }
    }
    for (UPCGNode* Node : Graph->GetNodes())
    {
        if (IsBoundGraphNode(Node, Graph) && !Seen.Contains(Node))
        {
            Result.Add(Node);
            Seen.Add(Node);
        }
    }
    if (UPCGNode* Output = Graph->GetOutputNode())
    {
        if (IsBoundGraphNode(Output, Graph) && !Seen.Contains(Output))
        {
            Result.Add(Output);
        }
    }
    return Result;
}

bool IsRegisteredGraphNode(const UPCGNode* Node, const UPCGGraph* Graph)
{
    if (!IsBoundGraphNode(Node, Graph))
    {
        return false;
    }
    if (Graph->GetInputNode() == Node || Graph->GetOutputNode() == Node)
    {
        return true;
    }
    for (const UPCGNode* AuthoredNode : Graph->GetNodes())
    {
        if (AuthoredNode == Node)
        {
            return true;
        }
    }
    return false;
}

FString NodeTitle(const UPCGNode* Node)
{
    return Node != nullptr
        ? Node->GetNodeTitle(EPCGNodeTitleType::ListView).ToString()
        : FString();
}

FString NodeComment(const UPCGNode* Node)
{
#if WITH_EDITORONLY_DATA
    return Node != nullptr ? Node->NodeComment : FString();
#else
    return FString();
#endif
}

bool IsGraphOwned(const UObject* Object, const UPCGGraph* Graph)
{
    return Object != nullptr
        && Graph != nullptr
        && (Object == Graph || Object->IsIn(Graph));
}

bool IsAuthoredSettingsProperty(const FProperty* Property)
{
    if (Property == nullptr
        || !Property->HasAnyPropertyFlags(CPF_Edit)
        || Property->HasAnyPropertyFlags(
            CPF_Transient
            | CPF_DuplicateTransient
            | CPF_NonPIEDuplicateTransient
            | CPF_Deprecated))
    {
        return false;
    }
    const FString Name = Property->GetName();
    return Name != TEXT("bEnabled")
        && Name != TEXT("bDebug")
        && Name != TEXT("DebugSettings")
        && Name != TEXT("bBreakDebugger")
        && Name != TEXT("bDisplayDebuggingProperties")
        && Name != TEXT("type")
        && Name != TEXT("isInstance")
        && Name != TEXT("ownership")
        && Name != TEXT("interfaceOwnership")
        && Name != TEXT("effectiveOwnership")
        && Name != TEXT("interfacePath")
        && Name != TEXT("effectiveType")
        && Name != TEXT("Settings")
        && Name != TEXT("snapshotTruncated");
}

bool IsSettingsSurfaceProperty(const FProperty* Property)
{
    return IsAuthoredSettingsProperty(Property)
        && (CastField<FNumericProperty>(Property) != nullptr
            || CastField<FBoolProperty>(Property) != nullptr
            || CastField<FEnumProperty>(Property) != nullptr
            || CastField<FNameProperty>(Property) != nullptr);
}

void AddOwnedSettingsSnapshot(
    const UPCGSettings* Settings,
    const TSharedPtr<FJsonObject>& Object)
{
    if (Settings == nullptr || !Object.IsValid())
    {
        return;
    }
    int32 PropertyCount = 0;
    int32 TextChars = 0;
    bool bTruncated = false;
    for (TFieldIterator<FProperty> It(Settings->GetClass()); It; ++It)
    {
        const FProperty* Property = *It;
        if (!IsAuthoredSettingsProperty(Property))
        {
            continue;
        }
        if (!IsSettingsSurfaceProperty(Property))
        {
            bTruncated = true;
            continue;
        }
        if (PropertyCount >= MaxSettingsProperties)
        {
            bTruncated = true;
            break;
        }
        const FString NativeText = ExportPropertyValue(Property, Settings);
        if (NativeText.Len() > MaxSettingsTextChars
            || TextChars + NativeText.Len() > MaxSettingsTextChars)
        {
            bTruncated = true;
            continue;
        }
        Object->SetField(Property->GetName(), NativeValue(NativeText));
        ++PropertyCount;
        TextChars += NativeText.Len();
    }
    if (bTruncated)
    {
        Object->SetBoolField(TEXT("snapshotTruncated"), true);
    }
}

TSharedPtr<FJsonValue> SettingsValue(
    const UPCGNode* Node,
    const UPCGGraph* Graph,
    const bool bIncludeOwnedSnapshot)
{
    const UPCGSettingsInterface* Interface =
        Node != nullptr ? Node->GetSettingsInterface() : nullptr;
    if (Interface == nullptr)
    {
        return Value::Null();
    }
    const UPCGSettings* Effective = Interface->GetSettings();
    const bool bInterfaceOwned = IsGraphOwned(Interface, Graph);
    const bool bEffectiveOwned = IsGraphOwned(Effective, Graph);
    const bool bOwned = !Interface->IsInstance()
        && bInterfaceOwned
        && bEffectiveOwned;

    TSharedPtr<FJsonObject> Object = MakeShared<FJsonObject>();
    Object->SetStringField(TEXT("type"), Interface->GetClass()->GetPathName());
    Object->SetBoolField(TEXT("isInstance"), Interface->IsInstance());
    Object->SetField(
        TEXT("ownership"),
        Value::Name(bOwned ? TEXT("owned") : TEXT("external")));
    Object->SetField(
        TEXT("interfaceOwnership"),
        Value::Name(bInterfaceOwned ? TEXT("owned") : TEXT("external")));
    Object->SetField(
        TEXT("effectiveOwnership"),
        Value::Name(bEffectiveOwned ? TEXT("owned") : TEXT("external")));
    Object->SetBoolField(TEXT("bEnabled"), Interface->bEnabled);
    Object->SetStringField(TEXT("interfacePath"), Interface->GetPathName());
    if (Effective != nullptr)
    {
        Object->SetStringField(TEXT("effectiveType"), Effective->GetClass()->GetPathName());
    }
    else
    {
        Object->SetField(TEXT("effectiveType"), Value::Null());
    }

    if (bOwned)
    {
        if (bIncludeOwnedSnapshot)
        {
            AddOwnedSettingsSnapshot(Effective, Object);
        }
    }
    else
    {
        // Evidence only. The external object is deliberately not projected as
        // a related Target and none of its authored properties are traversed.
        Object->SetField(
            TEXT("Settings"),
            Effective != nullptr
                ? Value::String(Effective->GetPathName())
                : Value::Null());
    }
    return MakeShared<FJsonValueObject>(Object);
}

FString SettingsSchemaText(
    const UPCGNode* Node,
    const UPCGGraph* Graph)
{
    const UPCGSettingsInterface* Interface =
        Node != nullptr ? Node->GetSettingsInterface() : nullptr;
    const UPCGSettings* Effective =
        Interface != nullptr ? Interface->GetSettings() : nullptr;
    if (Interface == nullptr)
    {
        return TEXT("SettingsInterface: null; readable descendants: none");
    }

    const bool bInterfaceOwned = IsGraphOwned(Interface, Graph);
    const bool bEffectiveOwned = IsGraphOwned(Effective, Graph);
    const bool bOwned = !Interface->IsInstance()
        && bInterfaceOwned
        && bEffectiveOwned;
    FString Result = FString::Printf(
        TEXT("SettingsInterface type: %s\ninterface ownership: %s; effective ownership: %s\nreadable interface fields: type, isInstance, ownership, interfaceOwnership, effectiveOwnership, bEnabled, interfacePath, effectiveType"),
        *Interface->GetClass()->GetPathName(),
        bInterfaceOwned ? TEXT("owned") : TEXT("external"),
        bEffectiveOwned ? TEXT("owned") : TEXT("external"));
    if (!bOwned || Effective == nullptr)
    {
        Result += TEXT("\nreadable authored Settings descendants: none (external evidence only: Settings object path)");
        return Result;
    }

    TArray<FString> Fields;
    for (TFieldIterator<FProperty> It(Effective->GetClass());
         It && Fields.Num() < MaxSettingsProperties;
         ++It)
    {
        const FProperty* Property = *It;
        if (IsSettingsSurfaceProperty(Property))
        {
            Fields.Add(FString::Printf(
                TEXT("SettingsInterface.%s: %s"),
                *Property->GetName(),
                *Property->GetCPPType()));
        }
    }
    Result += TEXT("\nreadable authored Settings fields: ");
    Result += Fields.IsEmpty()
        ? TEXT("none")
        : FString::Join(Fields, TEXT(", "));
    return Result;
}

TSharedPtr<FJsonValue> TypeIdentifierValue(
    const FPCGDataTypeIdentifier& Identifier)
{
    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    TArray<TSharedPtr<FJsonValue>> Ids;
    for (const FPCGDataTypeBaseId& Id : Identifier.GetIds())
    {
        TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
        const UScriptStruct* Struct = Id.GetStruct();
        Entry->SetStringField(
            TEXT("struct"),
            Struct != nullptr ? Struct->GetPathName() : Id.ToString());
        Ids.Add(MakeShared<FJsonValueObject>(Entry));
    }
    Result->SetArrayField(TEXT("ids"), Ids);
    Result->SetNumberField(TEXT("customSubtype"), Identifier.CustomSubtype);
    return MakeShared<FJsonValueObject>(Result);
}

bool TryGetPinDirection(const UPCGPin* Pin, bool& bOutOutput)
{
    bOutOutput = false;
    if (Pin == nullptr || Pin->Node == nullptr)
    {
        return false;
    }

    bool bInput = false;
    bool bOutput = false;
    for (const TObjectPtr<UPCGPin>& Candidate : Pin->Node->GetInputPins())
    {
        bInput |= Candidate == Pin;
    }
    for (const TObjectPtr<UPCGPin>& Candidate : Pin->Node->GetOutputPins())
    {
        bOutput |= Candidate == Pin;
    }
    if (bInput == bOutput)
    {
        return false;
    }
    bOutOutput = bOutput;
    return true;
}

bool TryGetBoundGraphEdgeEndpoints(
    const UPCGEdge* Edge,
    const UPCGGraph* Graph,
    const UPCGPin*& OutFrom,
    const UPCGPin*& OutTo);

bool HasSafeNativePinTopology(const UPCGNode* Node)
{
    if (Node == nullptr)
    {
        return false;
    }
    TSet<const UPCGPin*> SeenPins;
    for (const bool bOutput : {false, true})
    {
        TSet<FName> SeenLabels;
        const TArray<TObjectPtr<UPCGPin>>& Pins = bOutput
            ? Node->GetOutputPins()
            : Node->GetInputPins();
        for (const TObjectPtr<UPCGPin>& Pin : Pins)
        {
            if (Pin == nullptr
                || Pin->Node != Node
                || Pin->Properties.Label.IsNone()
                || SeenPins.Contains(Pin)
                || SeenLabels.Contains(Pin->Properties.Label))
            {
                return false;
            }
            SeenPins.Add(Pin);
            SeenLabels.Add(Pin->Properties.Label);
            for (const TObjectPtr<UPCGEdge>& Edge : Pin->Edges)
            {
                const UPCGPin* From = nullptr;
                const UPCGPin* To = nullptr;
                if (!TryGetBoundGraphEdgeEndpoints(
                        Edge,
                        Node->GetGraph(),
                        From,
                        To)
                    || (Pin != From && Pin != To))
                {
                    return false;
                }
                int32 FromOccurrences = 0;
                int32 ToOccurrences = 0;
                for (const TObjectPtr<UPCGEdge>& Adjacent : From->Edges)
                {
                    FromOccurrences += Adjacent == Edge ? 1 : 0;
                }
                for (const TObjectPtr<UPCGEdge>& Adjacent : To->Edges)
                {
                    ToOccurrences += Adjacent == Edge ? 1 : 0;
                }
                if (FromOccurrences != 1 || ToOccurrences != 1)
                {
                    return false;
                }
            }
        }
    }
    return true;
}

bool HasSafeNativeGraphTopology(UPCGGraph* Graph)
{
    if (Graph == nullptr)
    {
        return false;
    }
    for (const UPCGNode* DefaultNode : {
             static_cast<const UPCGNode*>(Graph->GetInputNode()),
             static_cast<const UPCGNode*>(Graph->GetOutputNode())})
    {
        if (DefaultNode != nullptr && !IsBoundGraphNode(DefaultNode, Graph))
        {
            return false;
        }
    }
    for (const UPCGNode* Node : Graph->GetNodes())
    {
        if (!IsBoundGraphNode(Node, Graph))
        {
            return false;
        }
    }
    const TArray<UPCGNode*> Nodes = GraphNodes(Graph);
    for (const UPCGNode* Node : Nodes)
    {
        if (!HasSafeNativePinTopology(Node))
        {
            return false;
        }
    }

    TMap<const UPCGNode*, int32> InDegree;
    TMap<const UPCGNode*, TArray<const UPCGNode*>> Downstream;
    TSet<const UPCGEdge*> SeenEdges;
    for (const UPCGNode* Node : Nodes)
    {
        InDegree.Add(Node, 0);
    }
    for (const UPCGNode* Node : Nodes)
    {
        for (const TObjectPtr<UPCGPin>& Pin : Node->GetOutputPins())
        {
            for (const TObjectPtr<UPCGEdge>& Edge : Pin->Edges)
            {
                if (SeenEdges.Contains(Edge))
                {
                    continue;
                }
                SeenEdges.Add(Edge);
                const UPCGPin* From = nullptr;
                const UPCGPin* To = nullptr;
                if (!TryGetBoundGraphEdgeEndpoints(
                        Edge,
                        Graph,
                        From,
                        To))
                {
                    return false;
                }
                if (!InDegree.Contains(From->Node))
                {
                    return false;
                }
                Downstream.FindOrAdd(From->Node).Add(To->Node);
                int32* ToInDegree = InDegree.Find(To->Node);
                if (ToInDegree == nullptr)
                {
                    return false;
                }
                ++(*ToInDegree);
            }
        }
    }

    TArray<const UPCGNode*> Ready;
    for (const TPair<const UPCGNode*, int32>& Pair : InDegree)
    {
        if (Pair.Value == 0)
        {
            Ready.Add(Pair.Key);
        }
    }
    int32 Visited = 0;
    while (!Ready.IsEmpty())
    {
        const UPCGNode* Node = Ready.Pop(EAllowShrinking::No);
        ++Visited;
        for (const UPCGNode* Next : Downstream.FindRef(Node))
        {
            int32& NextInDegree = InDegree.FindChecked(Next);
            --NextInDegree;
            if (NextInDegree == 0)
            {
                Ready.Add(Next);
            }
        }
    }
    return Visited == Nodes.Num();
}

bool TryGetBoundGraphEdgeEndpoints(
    const UPCGEdge* Edge,
    const UPCGGraph* Graph,
    const UPCGPin*& OutFrom,
    const UPCGPin*& OutTo)
{
    OutFrom = nullptr;
    OutTo = nullptr;
    if (Edge == nullptr || Graph == nullptr || !Edge->IsValid())
    {
        return false;
    }
    const UPCGPin* A = Edge->InputPin;
    const UPCGPin* B = Edge->OutputPin;
    bool bAOutput = false;
    bool bBOutput = false;
    if (A == nullptr
        || B == nullptr
        || A->Node == nullptr
        || B->Node == nullptr
        || !IsRegisteredGraphNode(A->Node, Graph)
        || !IsRegisteredGraphNode(B->Node, Graph)
        || !TryGetPinDirection(A, bAOutput)
        || !TryGetPinDirection(B, bBOutput)
        || bAOutput == bBOutput)
    {
        return false;
    }
    OutFrom = bAOutput ? A : B;
    OutTo = bAOutput ? B : A;
    return true;
}

int32 SafeConnectionCount(const UPCGPin* Pin, const UPCGGraph* Graph)
{
    int32 Count = 0;
    if (Pin == nullptr)
    {
        return Count;
    }
    for (const TObjectPtr<UPCGEdge>& Edge : Pin->Edges)
    {
        const UPCGPin* From = nullptr;
        const UPCGPin* To = nullptr;
        if (TryGetBoundGraphEdgeEndpoints(Edge, Graph, From, To))
        {
            ++Count;
        }
    }
    return Count;
}

TSharedPtr<FJsonValue> PinValue(
    const UPCGPin* Pin,
    const UPCGGraph* Graph,
    const bool bOutput,
    const bool bCanEvaluateCurrentTypes)
{
    if (Pin == nullptr || Pin->Node == nullptr)
    {
        return Value::Null();
    }
    TSharedPtr<FJsonObject> Args = MakeShared<FJsonObject>();
    Args->SetStringField(TEXT("id"), Pin->Properties.Label.ToString());
    Args->SetField(
        TEXT("direction"),
        Value::Name(bOutput ? TEXT("out") : TEXT("in")));
    Args->SetField(
        TEXT("allowedTypes"),
        TypeIdentifierValue(Pin->Properties.AllowedTypes));
    if (bCanEvaluateCurrentTypes)
    {
        const FPCGDataTypeIdentifier CurrentTypes = Pin->GetCurrentTypesID();
        Args->SetField(TEXT("currentTypes"), TypeIdentifierValue(CurrentTypes));
        Args->SetStringField(
            TEXT("typeDisplay"),
            CurrentTypes.ToDisplayText().ToString());
    }
    else
    {
        Args->SetField(TEXT("currentTypes"), Value::Null());
        Args->SetField(TEXT("typeDisplay"), Value::Null());
    }
    Args->SetBoolField(
        TEXT("allowsMultipleConnections"),
        bOutput || Pin->Properties.AllowsMultipleConnections());
    Args->SetBoolField(TEXT("allowsMultipleData"), Pin->Properties.bAllowMultipleData);
    Args->SetBoolField(TEXT("required"), Pin->Properties.IsRequiredPin());
    Args->SetBoolField(TEXT("advanced"), Pin->Properties.IsAdvancedPin());
    Args->SetBoolField(TEXT("invisible"), Pin->Properties.bInvisiblePin);
    Args->SetNumberField(
        TEXT("connectionCount"),
        SafeConnectionCount(Pin, Graph));
    return Value::Call(TEXT("pin"), Args);
}

TSharedPtr<FJsonValue> NodeValue(
    const UPCGNode* Node,
    const UPCGGraph* Graph,
    const FString& GraphAlias,
    const bool bExact,
    const bool bLayout)
{
    if (Node == nullptr)
    {
        return Value::Null();
    }
    TSharedPtr<FJsonObject> Args = MakeShared<FJsonObject>();
    Args->SetField(TEXT("graph"), Value::Local(GraphAlias));
    Args->SetStringField(TEXT("id"), Node->GetFName().ToString());
    Args->SetStringField(TEXT("type"), Node->GetClass()->GetPathName());
    Args->SetStringField(TEXT("title"), NodeTitle(Node));
    if (Node->HasAuthoredTitle())
    {
        Args->SetStringField(
            TEXT("titleOverride"),
            Node->GetAuthoredTitleName().ToString());
    }
    else
    {
        Args->SetField(TEXT("titleOverride"), Value::Null());
    }
    Args->SetNumberField(TEXT("inputPinCount"), Node->GetInputPins().Num());
    Args->SetNumberField(TEXT("outputPinCount"), Node->GetOutputPins().Num());
    Args->SetField(
        TEXT("SettingsInterface"),
        SettingsValue(Node, Graph, bExact));
    if (bExact)
    {
        Args->SetStringField(TEXT("NodeComment"), NodeComment(Node));
    }
    if (bLayout)
    {
#if WITH_EDITOR
        int32 X = 0;
        int32 Y = 0;
        Node->GetNodePosition(X, Y);
        Args->SetArrayField(
            TEXT("at"),
            {Value::Number(X), Value::Number(Y)});
#endif
    }
    return Value::Call(TEXT("node"), Args);
}

int32 GraphEdgeCount(
    const UPCGGraph* Graph,
    const TArray<UPCGNode*>& Nodes)
{
    int32 Count = 0;
    for (const UPCGNode* Node : Nodes)
    {
        if (Node == nullptr)
        {
            continue;
        }
        for (const TObjectPtr<UPCGPin>& Pin : Node->GetOutputPins())
        {
            if (Pin == nullptr)
            {
                continue;
            }
            for (const TObjectPtr<UPCGEdge>& Edge : Pin->Edges)
            {
                const UPCGPin* From = nullptr;
                const UPCGPin* To = nullptr;
                if (TryGetBoundGraphEdgeEndpoints(Edge, Graph, From, To))
                {
                    ++Count;
                }
            }
        }
    }
    return Count;
}

TSharedPtr<FJsonValue> GraphValue(
    const FSalResolvedTarget& Target,
    UPCGGraph* Graph,
    const bool bSummary)
{
    TSharedPtr<FJsonObject> Args = MakeShared<FJsonObject>();
    Args->SetStringField(
        TEXT("path"),
        !Target.AssetPath.IsEmpty() ? Target.AssetPath : Graph->GetPathName());
    Args->SetStringField(TEXT("type"), Graph->GetClass()->GetPathName());
    Args->SetStringField(
        TEXT("name"),
        !Target.Name.IsEmpty() ? Target.Name : Graph->GetName());
    Args->SetArrayField(
        TEXT("domains"),
        {Value::String(TEXT("asset")), Value::String(TEXT("pcg"))});
    Args->SetBoolField(TEXT("loaded"), true);
#if WITH_EDITORONLY_DATA
    Args->SetStringField(TEXT("Category"), Graph->Category.ToString());
    Args->SetStringField(TEXT("Description"), Graph->Description.ToString());
#endif
    if (const UPCGNode* Input = Graph->GetInputNode();
        IsBoundGraphNode(Input, Graph))
    {
        Args->SetField(
            TEXT("DefaultInputNode"),
            Value::Stable(TEXT("node"), {Input->GetFName().ToString()}));
    }
    if (const UPCGNode* Output = Graph->GetOutputNode();
        IsBoundGraphNode(Output, Graph))
    {
        Args->SetField(
            TEXT("DefaultOutputNode"),
            Value::Stable(TEXT("node"), {Output->GetFName().ToString()}));
    }
    if (bSummary)
    {
        const TArray<UPCGNode*> Nodes = GraphNodes(Graph);
        int32 InputPins = 0;
        int32 OutputPins = 0;
        for (const UPCGNode* Node : Nodes)
        {
            if (Node != nullptr)
            {
                InputPins += Node->GetInputPins().Num();
                OutputPins += Node->GetOutputPins().Num();
            }
        }
        Args->SetNumberField(TEXT("nodeCount"), Nodes.Num());
        Args->SetNumberField(TEXT("authoredNodeCount"), Graph->GetNodes().Num());
        Args->SetNumberField(TEXT("inputPinCount"), InputPins);
        Args->SetNumberField(TEXT("outputPinCount"), OutputPins);
        Args->SetNumberField(TEXT("edgeCount"), GraphEdgeCount(Graph, Nodes));
    }
    return Value::Call(TEXT("asset"), Args);
}

TArray<FString> NodeSearchFields(const UPCGNode* Node)
{
    if (Node == nullptr)
    {
        return {};
    }
    const UPCGSettingsInterface* Interface = Node->GetSettingsInterface();
    const UPCGSettings* Effective = Interface != nullptr ? Interface->GetSettings() : nullptr;
    return {
        Node->GetFName().ToString(),
        NodeTitle(Node),
        Node->GetAuthoredTitleName().ToString(),
        NodeComment(Node),
        Node->GetClass()->GetPathName(),
        Interface != nullptr ? Interface->GetClass()->GetPathName() : FString(),
        Effective != nullptr ? Effective->GetClass()->GetPathName() : FString()};
}

bool NodeMatchesText(const UPCGNode* Node, const FString& SearchText)
{
    if (Node == nullptr || SearchText.IsEmpty())
    {
        return Node != nullptr;
    }
    for (const FString& Field : NodeSearchFields(Node))
    {
        if (Field.Contains(SearchText, ESearchCase::IgnoreCase))
        {
            return true;
        }
    }
    return false;
}

FString PinIdentityText(const UPCGPin* Pin)
{
    if (Pin == nullptr || Pin->Node == nullptr)
    {
        return TEXT("<invalid pin>");
    }
    bool bOutput = false;
    if (!TryGetPinDirection(Pin, bOutput))
    {
        return FString::Printf(
            TEXT("@%s/<invalid>/%s"),
            *Pin->Node->GetFName().ToString(),
            *Pin->Properties.Label.ToString());
    }
    return FString::Printf(
        TEXT("@%s/%s/%s"),
        *Pin->Node->GetFName().ToString(),
        bOutput ? TEXT("out") : TEXT("in"),
        *Pin->Properties.Label.ToString());
}

void AddStructuralDiagnostics(
    UPCGGraph* Graph,
    const FString& Operation,
    TArray<TSharedPtr<FJsonObject>>& Diagnostics)
{
    bool bTruncated = false;
    const auto Add = [&](const TSharedPtr<FJsonObject>& Diagnostic)
    {
        if (Diagnostics.Num() < MaxQueryDiagnostics)
        {
            Diagnostics.Add(Diagnostic);
        }
        else
        {
            bTruncated = true;
        }
    };

    for (const UPCGNode* DefaultNode : {
             static_cast<const UPCGNode*>(Graph->GetInputNode()),
             static_cast<const UPCGNode*>(Graph->GetOutputNode())})
    {
        if (DefaultNode != nullptr && !IsBoundGraphNode(DefaultNode, Graph))
        {
            Add(Warning(
                TEXT("validation.reference_scan_incomplete"),
                TEXT("PCG Graph contains a default Node reference outside the selected Graph ownership boundary."),
                Operation));
        }
    }
    for (const UPCGNode* Node : Graph->GetNodes())
    {
        if (Node != nullptr && !IsBoundGraphNode(Node, Graph))
        {
            Add(Warning(
                TEXT("validation.reference_scan_incomplete"),
                TEXT("PCG Graph contains an authored Node reference outside the selected Graph ownership boundary."),
                Operation,
                Node->GetPathName()));
        }
    }

    TMap<FName, int32> NodeNames;
    TSet<const UPCGEdge*> SeenEdges;
    for (const UPCGNode* Node : GraphNodes(Graph))
    {
        if (Node == nullptr)
        {
            continue;
        }
        ++NodeNames.FindOrAdd(Node->GetFName());
        for (const bool bOutput : {false, true})
        {
            TMap<FName, int32> Labels;
            const TArray<TObjectPtr<UPCGPin>>& Pins = bOutput
                ? Node->GetOutputPins()
                : Node->GetInputPins();
            for (const TObjectPtr<UPCGPin>& Pin : Pins)
            {
                if (Pin == nullptr)
                {
                    Add(Warning(
                        TEXT("validation.reference_scan_incomplete"),
                        TEXT("PCG Node contains a null Pin entry."),
                        Operation,
                        Node->GetFName().ToString()));
                    continue;
                }
                bool bResolvedOutput = false;
                if (Pin->Node != Node
                    || !TryGetPinDirection(Pin, bResolvedOutput)
                    || bResolvedOutput != bOutput)
                {
                    Add(Warning(
                        TEXT("validation.reference_scan_incomplete"),
                        TEXT("PCG Pin is not owned by exactly one matching native direction array; current type was not evaluated."),
                        Operation,
                        PinIdentityText(Pin)));
                }
                ++Labels.FindOrAdd(Pin->Properties.Label);
                if (Pin->Properties.Label.IsNone())
                {
                    Add(Warning(
                        TEXT("resolution.identity_conflict"),
                        TEXT("PCG Pin has an empty Label and therefore has no StableRef identity."),
                        Operation,
                        Node->GetFName().ToString()));
                }
                for (const TObjectPtr<UPCGEdge>& Edge : Pin->Edges)
                {
                    if (Edge == nullptr)
                    {
                        Add(Warning(
                            TEXT("validation.reference_scan_incomplete"),
                            TEXT("PCG Pin contains a null Edge entry."),
                            Operation,
                            PinIdentityText(Pin)));
                        continue;
                    }
                    if (SeenEdges.Contains(Edge))
                    {
                        continue;
                    }
                    SeenEdges.Add(Edge);
                    const UPCGPin* From = nullptr;
                    const UPCGPin* To = nullptr;
                    if (!TryGetBoundGraphEdgeEndpoints(
                            Edge,
                            Graph,
                            From,
                            To))
                    {
                        Add(Warning(
                            TEXT("validation.reference_scan_incomplete"),
                            TEXT("PCG Graph contains an Edge without one bound-Graph output and one bound-Graph input endpoint."),
                            Operation,
                            PinIdentityText(Pin)));
                    }
                }
            }
            for (const TPair<FName, int32>& Pair : Labels)
            {
                if (!Pair.Key.IsNone() && Pair.Value > 1)
                {
                    Add(Warning(
                        TEXT("resolution.identity_conflict"),
                        TEXT("Multiple Pins share the same Node, direction, and Label; no exact StableRef can select one."),
                        Operation,
                        FString::Printf(
                            TEXT("@%s/%s/%s"),
                            *Node->GetFName().ToString(),
                            bOutput ? TEXT("out") : TEXT("in"),
                            *Pair.Key.ToString())));
                }
            }
        }
    }
    for (const TPair<FName, int32>& Pair : NodeNames)
    {
        if (Pair.Value > 1)
        {
            Add(Warning(
                TEXT("resolution.identity_conflict"),
                TEXT("Multiple PCG Nodes share the same serialized UObject name."),
                Operation,
            Pair.Key.ToString()));
        }
    }
    if (!HasSafeNativeGraphTopology(Graph))
    {
        Add(Warning(
            TEXT("validation.reference_scan_incomplete"),
            TEXT("PCG Graph topology is not safe for bounded native current-type evaluation; currentTypes and typeDisplay were omitted."),
            Operation));
    }
    if (bTruncated)
    {
        Diagnostics.Add(Warning(
            TEXT("validation.reference_scan_incomplete"),
            TEXT("Additional PCG structural diagnostics were omitted after the bounded diagnostic limit."),
            Operation));
    }
}

void AppendFingerprintToken(FString& Out, const FString& Value)
{
    Out += LexToString(Value.Len());
    Out += TEXT(":");
    Out += Value;
    Out += TEXT(";");
}

uint32 CursorHash(
    const FSalQuery& Query,
    const FSalResolvedTarget& Target,
    UPCGGraph* Graph,
    const int32 Limit)
{
    FString Canonical;
    FString SearchText;
    Query.Operation->TryGetStringField(TEXT("text"), SearchText);
    AppendFingerprintToken(Canonical, Target.AssetPath);
    AppendFingerprintToken(Canonical, SearchText);
    AppendFingerprintToken(Canonical, FString::Join(Query.With, TEXT(",")));
    AppendFingerprintToken(Canonical, LexToString(Limit));
    for (const UPCGNode* Node : GraphNodes(Graph))
    {
        if (Node == nullptr)
        {
            AppendFingerprintToken(Canonical, TEXT("<null>"));
            continue;
        }
        for (const FString& Field : NodeSearchFields(Node))
        {
            AppendFingerprintToken(Canonical, Field);
        }
        for (const TObjectPtr<UPCGPin>& Pin : Node->GetInputPins())
        {
            AppendFingerprintToken(
                Canonical,
                Pin != nullptr ? TEXT("in:") + Pin->Properties.Label.ToString() : TEXT("in:<null>"));
        }
        for (const TObjectPtr<UPCGPin>& Pin : Node->GetOutputPins())
        {
            AppendFingerprintToken(
                Canonical,
                Pin != nullptr ? TEXT("out:") + Pin->Properties.Label.ToString() : TEXT("out:<null>"));
        }
    }
    return FCrc::StrCrc32(*Canonical);
}

bool DecodePage(
    const FSalQuery& Query,
    const FSalResolvedTarget& Target,
    UPCGGraph* Graph,
    FSalPage& OutPage)
{
    OutPage.Offset = 0;
    OutPage.Limit = FMath::Clamp(
        Query.PageLimit > 0 ? Query.PageLimit : DefaultCollectionLimit,
        1,
        MaxCollectionLimit);
    if (Query.PageAfter.IsEmpty())
    {
        return true;
    }
    TArray<FString> Parts;
    Query.PageAfter.ParseIntoArray(Parts, TEXT(":"), false);
    const FString Expected = FString::Printf(
        TEXT("%08x"),
        CursorHash(Query, Target, Graph, OutPage.Limit));
    return Parts.Num() == 3
        && Parts[0] == TEXT("pcg1")
        && Parts[1].Equals(Expected, ESearchCase::IgnoreCase)
        && ParseNonNegativeInt32(Parts[2], OutPage.Offset);
}

void SetPage(
    const TSharedPtr<FJsonObject>& Result,
    const FSalQuery& Query,
    const FSalResolvedTarget& Target,
    UPCGGraph* Graph,
    const FSalPage& Page,
    const int32 NextOffset,
    const bool bHasNext)
{
    if (!Result.IsValid() || !bHasNext)
    {
        return;
    }
    TSharedPtr<FJsonObject> PageObject = MakeShared<FJsonObject>();
    PageObject->SetStringField(
        TEXT("next"),
        FString::Printf(
            TEXT("pcg1:%08x:%d"),
            CursorHash(Query, Target, Graph, Page.Limit),
            NextOffset));
    Result->SetObjectField(TEXT("page"), PageObject);
}

FString UniquePinMember(const FString& Label, TSet<FString>& Used)
{
    const FString Base = FSalObjectBuilder::SanitizeIdentifier(Label, TEXT("pin"));
    FString Candidate = Base;
    int32 Suffix = 2;
    while (Used.Contains(Candidate))
    {
        Candidate = FString::Printf(TEXT("%s_%d"), *Base, Suffix++);
    }
    Used.Add(Candidate);
    return Candidate;
}

struct FNodePinMembers
{
    TSet<FString> Input;
    TSet<FString> Output;
};

class FResultEncoder
{
public:
    FResultEncoder(
        const FSalQuery& Query,
        const FSalResolvedTarget& Target,
        UPCGGraph* Graph,
        const bool bSummary)
        : Query(Query)
        , Target(Target)
        , Graph(Graph)
        , bGraphTopologySafe(HasSafeNativeGraphTopology(Graph))
    {
        GraphAlias = Builder.UniqueAlias(
            Query.Alias.IsEmpty() ? TEXT("g") : Query.Alias);
        Builder.AddLocalBinding(
            GraphAlias,
            GraphValue(Target, Graph, bSummary));
    }

    FString AddNode(
        const UPCGNode* Node,
        const bool bExact,
        const bool bLayout)
    {
        if (const FString* Existing = NodeAliases.Find(Node))
        {
            return *Existing;
        }
        const FString Preferred = Node != nullptr
            ? Node->GetFName().ToString()
            : TEXT("node");
        const FString Alias = Builder.UniqueAlias(Preferred);
        Builder.AddLocalBinding(
            Alias,
            NodeValue(Node, Graph, GraphAlias, bExact, bLayout));
        NodeAliases.Add(Node, Alias);
        return Alias;
    }

    TSharedPtr<FJsonObject> AddPin(
        const UPCGPin* Pin,
        const bool bOwnerLayout)
    {
        if (const TSharedPtr<FJsonObject>* Existing = PinRefs.Find(Pin))
        {
            return *Existing;
        }
        if (Pin == nullptr || Pin->Node == nullptr)
        {
            return nullptr;
        }
        const FString NodeAlias = AddNode(Pin->Node, false, bOwnerLayout);
        bool bOutput = false;
        if (!TryGetPinDirection(Pin, bOutput))
        {
            AddDiagnostic(Warning(
                TEXT("validation.reference_scan_incomplete"),
                TEXT("A PCG Pin is not a member of exactly one native direction array."),
                TEXT("pin"),
                PinIdentityText(Pin)));
            return nullptr;
        }
        FNodePinMembers& Members = PinMembers.FindOrAdd(Pin->Node);
        TSet<FString>& Used = bOutput ? Members.Output : Members.Input;
        const FString Member = UniquePinMember(
            Pin->Properties.Label.ToString(),
            Used);
        const TArray<FString> Path = {
            bOutput ? TEXT("out") : TEXT("in"),
            Member};
        Builder.AddMemberBinding(
            NodeAlias,
            Path,
            PinValue(Pin, Graph, bOutput, bGraphTopologySafe));
        TSharedPtr<FJsonObject> Ref = Value::MemberObject(
            Value::LocalObject(NodeAlias),
            Path);
        PinRefs.Add(Pin, Ref);
        return Ref;
    }

    void AddAllPins(const UPCGNode* Node, const bool bOwnerLayout)
    {
        if (Node == nullptr)
        {
            return;
        }
        for (const TObjectPtr<UPCGPin>& Pin : Node->GetInputPins())
        {
            AddPin(Pin, bOwnerLayout);
        }
        for (const TObjectPtr<UPCGPin>& Pin : Node->GetOutputPins())
        {
            AddPin(Pin, bOwnerLayout);
        }
    }

    void AddIncidentEdges(
        const TArray<const UPCGPin*>& SubjectPins,
        const bool bOwnerLayout)
    {
        TSet<const UPCGEdge*> Seen;
        for (const UPCGPin* Subject : SubjectPins)
        {
            if (Subject == nullptr)
            {
                continue;
            }
            for (const TObjectPtr<UPCGEdge>& EdgePointer : Subject->Edges)
            {
                const UPCGEdge* Edge = EdgePointer;
                if (Edge == nullptr || Seen.Contains(Edge))
                {
                    continue;
                }
                Seen.Add(Edge);
                const UPCGPin* From = nullptr;
                const UPCGPin* To = nullptr;
                if (!TryGetBoundGraphEdgeEndpoints(
                        Edge,
                        Graph,
                        From,
                        To))
                {
                    AddDiagnostic(Warning(
                        TEXT("validation.reference_scan_incomplete"),
                        TEXT("An incident PCG Edge does not have one valid output and one valid input Pin in the bound Graph."),
                        TEXT("edge"),
                        PinIdentityText(Subject)));
                    continue;
                }
                const TSharedPtr<FJsonObject> FromRef = AddPin(From, bOwnerLayout);
                const TSharedPtr<FJsonObject> ToRef = AddPin(To, bOwnerLayout);
                Builder.AddEdge(FromRef, ToRef);
            }
        }
    }

    void AddSchema(const FString& Marker, const FString& Body)
    {
        Builder.AddComment(Marker + TEXT("\n") + Body);
    }

    TSharedPtr<FJsonObject> Build()
    {
        if (bDiagnosticsTruncated
            && Diagnostics.Num() < MaxQueryDiagnostics + 1)
        {
            Diagnostics.Add(Warning(
                TEXT("validation.reference_scan_incomplete"),
                TEXT("Additional PCG query diagnostics were omitted after the bounded diagnostic limit."),
                TEXT("query")));
        }
        return Builder.BuildResult(Diagnostics);
    }

    FSalObjectBuilder Builder;
    TArray<TSharedPtr<FJsonObject>> Diagnostics;
    FString GraphAlias;

private:
    void AddDiagnostic(const TSharedPtr<FJsonObject>& Diagnostic)
    {
        if (Diagnostics.Num() < MaxQueryDiagnostics)
        {
            Diagnostics.Add(Diagnostic);
        }
        else
        {
            bDiagnosticsTruncated = true;
        }
    }

    const FSalQuery& Query;
    const FSalResolvedTarget& Target;
    UPCGGraph* Graph = nullptr;
    TMap<const UPCGNode*, FString> NodeAliases;
    TMap<const UPCGNode*, FNodePinMembers> PinMembers;
    TMap<const UPCGPin*, TSharedPtr<FJsonObject>> PinRefs;
    bool bDiagnosticsTruncated = false;
    bool bGraphTopologySafe = false;
};

bool HasUnsupportedDetails(
    const FSalQuery& Query,
    const TSet<FString>& Supported,
    FString& OutDetail)
{
    for (const FString& Detail : Query.With)
    {
        if (!Supported.Contains(Detail))
        {
            OutDetail = Detail;
            return true;
        }
    }
    return false;
}

bool HasExactClauses(const FSalQuery& Query)
{
    return Query.Where.IsValid()
        || !Query.OrderBy.IsEmpty()
        || Query.PageLimit > 0
        || !Query.PageAfter.IsEmpty();
}

UPCGNode* FindNode(
    UPCGGraph* Graph,
    const FString& Id,
    bool& bAmbiguous)
{
    bAmbiguous = false;
    UPCGNode* Match = nullptr;
    const FName Name(*Id, FNAME_Find);
    for (UPCGNode* Node : GraphNodes(Graph))
    {
        if (Node == nullptr || Node->GetFName() != Name)
        {
            continue;
        }
        if (Match != nullptr)
        {
            bAmbiguous = true;
            return nullptr;
        }
        Match = Node;
    }
    return Match;
}

const UPCGPin* FindPin(
    const UPCGNode* Node,
    const FString& Direction,
    const FString& Label,
    bool& bAmbiguous)
{
    bAmbiguous = false;
    if (Node == nullptr || (Direction != TEXT("in") && Direction != TEXT("out")))
    {
        return nullptr;
    }
    const FName LabelName(*Label, FNAME_Find);
    if (LabelName.IsNone())
    {
        return nullptr;
    }
    const TArray<TObjectPtr<UPCGPin>>& Pins = Direction == TEXT("out")
        ? Node->GetOutputPins()
        : Node->GetInputPins();
    const UPCGPin* Match = nullptr;
    for (const TObjectPtr<UPCGPin>& Pin : Pins)
    {
        if (Pin == nullptr || Pin->Properties.Label != LabelName)
        {
            continue;
        }
        if (Match != nullptr)
        {
            bAmbiguous = true;
            return nullptr;
        }
        Match = Pin;
    }
    return Match;
}

TSharedPtr<FJsonObject> QueryTarget(
    const FSalQuery& Query,
    const FSalResolvedTarget& Target,
    UPCGGraph* Graph)
{
    FString Unsupported;
    if (HasExactClauses(Query))
    {
        return QueryError(
            TEXT("capability.clause_unavailable"),
            TEXT("Exact PCG target read accepts only optional with schema."),
            TEXT("target"));
    }
    if (HasUnsupportedDetails(Query, {TEXT("schema")}, Unsupported))
    {
        return QueryError(
            TEXT("capability.detail_unavailable"),
            TEXT("Exact PCG target read supports only with schema."),
            TEXT("target"),
            Unsupported);
    }
    FResultEncoder Encoder(Query, Target, Graph, false);
    if (Query.With.Contains(TEXT("schema")))
    {
        Encoder.AddSchema(
            TEXT("pcg target schema (read-only)"),
            TEXT("fields: path, type, name, Category, Description, DefaultInputNode, DefaultOutputNode\noperations: target, summary, nodes, exact Node, exact Pin\nparameters, context, data flow, and palette are not active in this slice"));
    }
    return Encoder.Build();
}

TSharedPtr<FJsonObject> QuerySummary(
    const FSalQuery& Query,
    const FSalResolvedTarget& Target,
    UPCGGraph* Graph)
{
    if (!Query.With.IsEmpty() || HasExactClauses(Query))
    {
        return QueryError(
            TEXT("capability.clause_unavailable"),
            TEXT("PCG summary accepts no Query clauses."),
            TEXT("summary"));
    }
    FResultEncoder Encoder(Query, Target, Graph, true);
    for (const UPCGNode* DefaultNode : {
             static_cast<const UPCGNode*>(Graph->GetInputNode()),
             static_cast<const UPCGNode*>(Graph->GetOutputNode())})
    {
        if (IsBoundGraphNode(DefaultNode, Graph))
        {
            Encoder.AddNode(DefaultNode, false, false);
            Encoder.AddAllPins(DefaultNode, false);
        }
    }
    AddStructuralDiagnostics(Graph, TEXT("summary"), Encoder.Diagnostics);
    return Encoder.Build();
}

TSharedPtr<FJsonObject> QueryNodes(
    const FSalQuery& Query,
    const FSalResolvedTarget& Target,
    UPCGGraph* Graph)
{
    FString Unsupported;
    if (Query.Where.IsValid() || !Query.OrderBy.IsEmpty())
    {
        return QueryError(
            TEXT("capability.clause_unavailable"),
            TEXT("PCG nodes accepts only optional text search, with layout, and cursor page clauses."),
            TEXT("nodes"));
    }
    if (HasUnsupportedDetails(Query, {TEXT("layout")}, Unsupported))
    {
        return QueryError(
            TEXT("capability.detail_unavailable"),
            TEXT("PCG nodes supports only with layout."),
            TEXT("nodes"),
            Unsupported);
    }
    FSalPage Page;
    if (!DecodePage(Query, Target, Graph, Page))
    {
        return QueryError(
            TEXT("validation.invalid_cursor"),
            TEXT("PCG cursor does not belong to this target, graph structure, search, detail, or page limit. Re-run the first page."),
            TEXT("nodes"),
            Query.PageAfter);
    }
    FString SearchText;
    Query.Operation->TryGetStringField(TEXT("text"), SearchText);
    TArray<UPCGNode*> Matches;
    for (UPCGNode* Node : GraphNodes(Graph))
    {
        if (NodeMatchesText(Node, SearchText))
        {
            Matches.Add(Node);
        }
    }
    if (Page.Offset > Matches.Num())
    {
        return QueryError(
            TEXT("validation.invalid_cursor"),
            TEXT("PCG cursor offset is outside the current result set. Re-run the first page."),
            TEXT("nodes"),
            Query.PageAfter);
    }
    const int32 End = static_cast<int32>(FMath::Min<int64>(
        Matches.Num(),
        static_cast<int64>(Page.Offset) + Page.Limit));
    FResultEncoder Encoder(Query, Target, Graph, false);
    const bool bLayout = Query.With.Contains(TEXT("layout"));
    for (int32 Index = Page.Offset; Index < End; ++Index)
    {
        Encoder.AddNode(Matches[Index], false, bLayout);
    }
    if (Matches.IsEmpty())
    {
        Encoder.Builder.AddComment(TEXT("no matches"));
    }
    TSharedPtr<FJsonObject> Result = Encoder.Build();
    SetPage(Result, Query, Target, Graph, Page, End, End < Matches.Num());
    return Result;
}

TSharedPtr<FJsonObject> QueryExactNode(
    const FSalQuery& Query,
    const FSalResolvedTarget& Target,
    UPCGGraph* Graph)
{
    FString Unsupported;
    if (HasExactClauses(Query))
    {
        return QueryError(
            TEXT("capability.clause_unavailable"),
            TEXT("Exact PCG Node read accepts only optional with schema and layout."),
            TEXT("node"));
    }
    if (HasUnsupportedDetails(Query, {TEXT("schema"), TEXT("layout")}, Unsupported))
    {
        return QueryError(
            TEXT("capability.detail_unavailable"),
            TEXT("Exact PCG Node read supports only with schema and layout."),
            TEXT("node"),
            Unsupported);
    }
    FString Id;
    Query.Operation->TryGetStringField(TEXT("id"), Id);
    bool bAmbiguous = false;
    UPCGNode* Node = FindNode(Graph, Id, bAmbiguous);
    if (Node == nullptr)
    {
        return QueryError(
            bAmbiguous
                ? TEXT("resolution.identity_conflict")
                : TEXT("resolution.node_not_found"),
            bAmbiguous
                ? TEXT("PCG Node identity is duplicated in the bound Graph.")
                : TEXT("PCG Node was not found in the bound Graph."),
            TEXT("node"),
            Id);
    }

    const bool bLayout = Query.With.Contains(TEXT("layout"));
    FResultEncoder Encoder(Query, Target, Graph, false);
    Encoder.AddNode(Node, true, bLayout);
    Encoder.AddAllPins(Node, bLayout);
    TArray<const UPCGPin*> Pins;
    for (const TObjectPtr<UPCGPin>& Pin : Node->GetInputPins())
    {
        if (Pin != nullptr)
        {
            Pins.Add(Pin);
        }
    }
    for (const TObjectPtr<UPCGPin>& Pin : Node->GetOutputPins())
    {
        if (Pin != nullptr)
        {
            Pins.Add(Pin);
        }
    }
    Encoder.AddIncidentEdges(Pins, bLayout);
    if (Query.With.Contains(TEXT("schema")))
    {
        const UPCGSettingsInterface* Interface = Node->GetSettingsInterface();
        const UPCGSettings* Effective = Interface != nullptr ? Interface->GetSettings() : nullptr;
        const bool bOwned = Interface != nullptr
            && !Interface->IsInstance()
            && IsGraphOwned(Interface, Graph)
            && IsGraphOwned(Effective, Graph);
        Encoder.AddSchema(
            TEXT("pcg node schema (read-only)"),
            FString::Printf(
                TEXT("identity: serialized UObject FName\nfields: graph, id, type, title, titleOverride, NodeComment, SettingsInterface, input/output Pins, at\n%s\noverall Settings mutation ownership: %s\noperations: none in this read-only slice"),
                *SettingsSchemaText(Node, Graph),
                bOwned ? TEXT("owned") : TEXT("external")));
    }
    AddStructuralDiagnostics(Graph, TEXT("node"), Encoder.Diagnostics);
    return Encoder.Build();
}

TSharedPtr<FJsonObject> QueryExactPin(
    const FSalQuery& Query,
    const FSalResolvedTarget& Target,
    UPCGGraph* Graph)
{
    FString Unsupported;
    if (HasExactClauses(Query))
    {
        return QueryError(
            TEXT("capability.clause_unavailable"),
            TEXT("Exact PCG Pin read accepts only optional with schema and layout."),
            TEXT("pin"));
    }
    if (HasUnsupportedDetails(Query, {TEXT("schema"), TEXT("layout")}, Unsupported))
    {
        return QueryError(
            TEXT("capability.detail_unavailable"),
            TEXT("Exact PCG Pin read supports only with schema and layout."),
            TEXT("pin"),
            Unsupported);
    }
    FString NodeId;
    FString Direction;
    FString Label;
    Query.Operation->TryGetStringField(TEXT("node"), NodeId);
    Query.Operation->TryGetStringField(TEXT("direction"), Direction);
    Query.Operation->TryGetStringField(TEXT("label"), Label);
    if (NodeId.IsEmpty()
        || (Direction != TEXT("in") && Direction != TEXT("out"))
        || Label.IsEmpty())
    {
        return QueryError(
            TEXT("validation.invalid_reference"),
            TEXT("Exact PCG Pin read requires node, direction (in or out), and label fields."),
            TEXT("pin"));
    }
    bool bNodeAmbiguous = false;
    UPCGNode* Node = FindNode(Graph, NodeId, bNodeAmbiguous);
    if (Node == nullptr)
    {
        return QueryError(
            bNodeAmbiguous
                ? TEXT("resolution.identity_conflict")
                : TEXT("resolution.node_not_found"),
            bNodeAmbiguous
                ? TEXT("PCG Pin owner Node identity is duplicated in the bound Graph.")
                : TEXT("PCG Pin owner Node was not found in the bound Graph."),
            TEXT("pin"),
            NodeId);
    }
    bool bPinAmbiguous = false;
    const UPCGPin* Pin = FindPin(Node, Direction, Label, bPinAmbiguous);
    if (Pin == nullptr)
    {
        return QueryError(
            bPinAmbiguous
                ? TEXT("resolution.identity_conflict")
                : TEXT("resolution.pin_not_found"),
            bPinAmbiguous
                ? TEXT("Multiple PCG Pins share this Node, direction, and Label identity.")
                : TEXT("PCG Pin was not found on the exact Node and direction."),
            TEXT("pin"),
            FString::Printf(TEXT("@%s/%s/%s"), *NodeId, *Direction, *Label));
    }

    const bool bLayout = Query.With.Contains(TEXT("layout"));
    FResultEncoder Encoder(Query, Target, Graph, false);
    Encoder.AddNode(Node, false, bLayout);
    Encoder.AddPin(Pin, bLayout);
    Encoder.AddIncidentEdges({Pin}, bLayout);
    if (Query.With.Contains(TEXT("schema")))
    {
        Encoder.AddSchema(
            TEXT("pcg pin schema (read-only)"),
            TEXT("identity: Node FName + native direction + exact Pin Label\nfields: id, direction, allowedTypes, currentTypes, typeDisplay, connection policy, incident Edges\noperations: none in this read-only slice"));
    }
    AddStructuralDiagnostics(Graph, TEXT("pin"), Encoder.Diagnostics);
    return Encoder.Build();
}
}

class FPCGPatchAliasAllocator
{
public:
    FString Allocate(const FString& Preferred, const FString& Fallback = TEXT("item"))
    {
        const FString Base = FSalObjectBuilder::SanitizeIdentifier(Preferred, Fallback);
        if (!Used.Contains(Base))
        {
            Used.Add(Base);
            return Base;
        }
        int32& Suffix = NextSuffix.FindOrAdd(Base);
        if (Suffix < 2)
        {
            Suffix = 2;
        }
        for (;;)
        {
            const FString Candidate = FString::Printf(
                TEXT("%s_%d"), *Base, Suffix++);
            if (!Used.Contains(Candidate))
            {
                Used.Add(Candidate);
                return Candidate;
            }
        }
    }
private:
    TSet<FString> Used;
    TMap<FString, int32> NextSuffix;
};

// ============================================================================
// PCG authored mutation (Slice 2-A)
//
// Palette-backed Node creation on one exact loaded UPCGGraph. Settings
// set/reset, move, connection, removal, and save land in later increments and
// fail closed here. Every creation re-enumerates native UPCGSettings classes
// and revalidates the opaque Palette id; a raw Settings Class is never
// accepted.
// ============================================================================

FString PCGNodePaletteId(const UClass* SettingsClass)
{
    return TEXT("pcg.node.")
        + FSHA1::HashBuffer(
            *SettingsClass->GetClassPathName().ToString(),
            SettingsClass->GetClassPathName().ToString().Len()
                * sizeof(TCHAR)).ToString();
}

void DiscoverPCGNodePaletteEntries(
    TArray<TSharedPtr<FJsonObject>>& OutEntries)
{
    TArray<UClass*> SettingsClasses;
    GetDerivedClasses(
        UPCGSettings::StaticClass(),
        SettingsClasses);
    for (UClass* Class : SettingsClasses)
    {
        if (Class == nullptr
            || Class->HasAnyClassFlags(CLASS_Abstract)
            || Class->HasAnyClassFlags(CLASS_Deprecated))
        {
            continue;
        }
        const UPCGSettings* CDO = Class->GetDefaultObject<UPCGSettings>();
        if (CDO == nullptr || !CDO->bExposeToLibrary)
        {
            continue;
        }
        TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
        Entry->SetStringField(
            TEXT("palette"),
            PCGNodePaletteId(Class));
        Entry->SetStringField(
            TEXT("name"),
            Class->GetDisplayNameText().ToString());
        Entry->SetStringField(
            TEXT("type"),
            Class->GetClassPathName().ToString());
        Entry->SetStringField(TEXT("creation"), TEXT("available"));
        OutEntries.Add(Entry);
    }
}

bool ResolvePCGNodePaletteEntry(
    const FString& PaletteId,
    const UPCGSettings*& OutCDO)
{
    OutCDO = nullptr;
    TArray<UClass*> SettingsClasses;
    GetDerivedClasses(
        UPCGSettings::StaticClass(),
        SettingsClasses);
    for (UClass* Class : SettingsClasses)
    {
        if (Class == nullptr
            || Class->HasAnyClassFlags(CLASS_Abstract)
            || Class->HasAnyClassFlags(CLASS_Deprecated))
        {
            continue;
        }
        const UPCGSettings* CDO = Class->GetDefaultObject<UPCGSettings>();
        if (CDO == nullptr || !CDO->bExposeToLibrary)
        {
            continue;
        }
        if (PCGNodePaletteId(Class) == PaletteId)
        {
            OutCDO = CDO;
            return true;
        }
    }
    return false;
}


TSharedPtr<FJsonObject> FSalPCGInterface::Query(
    const FSalQuery& Query,
    const FSalResolvedTarget& Target)
{
    UPCGGraph* Graph = ResolvedGraph(Target);
    if (Graph == nullptr)
    {
        return QueryError(
            TEXT("capability.interface_unavailable"),
            TEXT("The pcg Domain requires an exact top-level UPCGGraph asset Target."),
            TEXT("query"),
            Target.AssetPath);
    }
    FString Operation;
    if (!Query.Operation.IsValid()
        || !Query.Operation->TryGetStringField(TEXT("kind"), Operation))
    {
        return QueryError(
            TEXT("capability.operation_unavailable"),
            TEXT("PCG Query has no supported primary operation."),
            TEXT("query"),
            FString(),
            {TEXT("target"), TEXT("summary"), TEXT("nodes"), TEXT("node"), TEXT("pin")});
    }
    if (Operation == TEXT("target"))
    {
        return QueryTarget(Query, Target, Graph);
    }
    if (Operation == TEXT("summary"))
    {
        return QuerySummary(Query, Target, Graph);
    }
    if (Operation == TEXT("nodes"))
    {
        return QueryNodes(Query, Target, Graph);
    }
    if (Operation == TEXT("node"))
    {
        return QueryExactNode(Query, Target, Graph);
    }
    if (Operation == TEXT("pin"))
    {
        return QueryExactPin(Query, Target, Graph);
    }
    if (Operation == TEXT("palette_entries"))
    {
        FString Search;
        Query.Operation->TryGetStringField(TEXT("text"), Search);
        FSalObjectBuilder Builder;
        FPCGPatchAliasAllocator Aliases;
        TArray<TSharedPtr<FJsonObject>> Entries;
        DiscoverPCGNodePaletteEntries(Entries);
        for (const TSharedPtr<FJsonObject>& Entry : Entries)
        {
            if (!Entry.IsValid())
            {
                continue;
            }
            FString Name;
            Entry->TryGetStringField(TEXT("name"), Name);
            if (!Search.IsEmpty()
                && !Name.Contains(Search, ESearchCase::IgnoreCase))
            {
                continue;
            }
            Builder.AddLocalBinding(
                Aliases.Allocate(Name, TEXT("node")),
                MakeShared<FJsonValueObject>(Entry));
        }
        return Builder.BuildResult();
    }
    return QueryError(
        TEXT("capability.operation_unavailable"),
        FString::Printf(
            TEXT("PCG Query operation is not active in this read-only slice: %s."),
            *Operation),
        Operation,
        FString(),
        {TEXT("target"), TEXT("summary"), TEXT("nodes"), TEXT("node"), TEXT("pin"), TEXT("palette_entries")});
}

bool FSalPCGInterface::LowerStableReference(
    const FSalResolvedTarget& Target,
    const TArray<FString>& IdentityPath,
    const TSharedPtr<FJsonObject>& Ref,
    FString& OutCode,
    FString& OutMessage)
{
    OutCode = TEXT("resolution.object_not_found");
    OutMessage = TEXT("Stable reference was not found in the bound PCG Graph identity environment.");
    UPCGGraph* Graph = ResolvedGraph(Target);
    if (Graph == nullptr || !Ref.IsValid())
    {
        OutCode = TEXT("capability.interface_unavailable");
        OutMessage = TEXT("PCG StableRef resolution requires an exact UPCGGraph Target.");
        return false;
    }
    if (!(IdentityPath.Num() == 1 || IdentityPath.Num() == 3))
    {
        OutCode = TEXT("validation.invalid_reference");
        OutMessage = TEXT("PCG StableRef identity must be [Node] or [Node, in|out, PinLabel].");
        return false;
    }
    for (const FString& Segment : IdentityPath)
    {
        if (Segment.IsEmpty())
        {
            OutCode = TEXT("validation.invalid_reference");
            OutMessage = TEXT("PCG StableRef identity segments must be non-empty strings.");
            return false;
        }
    }

    bool bNodeAmbiguous = false;
    UPCGNode* Node = FindNode(Graph, IdentityPath[0], bNodeAmbiguous);
    if (Node == nullptr)
    {
        OutCode = bNodeAmbiguous
            ? TEXT("resolution.identity_conflict")
            : TEXT("resolution.node_not_found");
        OutMessage = bNodeAmbiguous
            ? TEXT("Multiple PCG Nodes share this serialized UObject name.")
            : TEXT("PCG Node was not found in the bound Graph.");
        return false;
    }
    const FString CanonicalNode = Node->GetFName().ToString();
    if (IdentityPath.Num() == 1)
    {
        Ref->Values.Reset();
        Ref->SetStringField(TEXT("kind"), TEXT("node"));
        Ref->SetStringField(TEXT("id"), CanonicalNode);
        OutCode.Reset();
        OutMessage.Reset();
        return true;
    }

    const FString& Direction = IdentityPath[1];
    if (Direction != TEXT("in") && Direction != TEXT("out"))
    {
        OutCode = TEXT("validation.invalid_reference");
        OutMessage = TEXT("PCG Pin StableRef direction must be exactly in or out.");
        return false;
    }
    if (IdentityPath[2].Equals(
            TEXT("None"),
            ESearchCase::IgnoreCase))
    {
        OutCode = TEXT("validation.invalid_reference");
        OutMessage = TEXT("A PCG Pin with an empty native Label has no StableRef identity.");
        return false;
    }
    bool bPinAmbiguous = false;
    const UPCGPin* Pin = FindPin(
        Node,
        Direction,
        IdentityPath[2],
        bPinAmbiguous);
    if (Pin == nullptr)
    {
        OutCode = bPinAmbiguous
            ? TEXT("resolution.identity_conflict")
            : TEXT("resolution.pin_not_found");
        OutMessage = bPinAmbiguous
            ? TEXT("Multiple PCG Pins share this Node, direction, and Label identity.")
            : TEXT("PCG Pin was not found on the exact Node and direction.");
        return false;
    }

    Ref->Values.Reset();
    Ref->SetStringField(TEXT("kind"), TEXT("pin"));
    Ref->SetStringField(TEXT("node"), CanonicalNode);
    Ref->SetStringField(TEXT("direction"), Direction);
    Ref->SetStringField(TEXT("label"), Pin->Properties.Label.ToString());
    OutCode.Reset();
    OutMessage.Reset();
    return true;
}

bool PCGValueImportText(
    const TSharedPtr<FJsonValue>& Expression,
    FString& OutText)
{
    OutText.Reset();
    if (!Expression.IsValid())
    {
        return false;
    }
    bool Boolean = false;
    double Number = 0.0;
    FString String;
    if (Expression->TryGetBool(Boolean))
    {
        OutText = Boolean ? TEXT("true") : TEXT("false");
        return true;
    }
    if (Expression->TryGetNumber(Number))
    {
        OutText = LexToString(Number);
        return true;
    }
    if (Expression->TryGetString(String))
    {
        OutText = String;
        return true;
    }
    return false;
}

bool PCGImportScalarValue(
    FProperty* Property,
    UObject* Object,
    const FString& Text,
    FString& OutError)
{
    if (Property == nullptr || Object == nullptr)
    {
        OutError = TEXT("PCG Patch edit target is invalid.");
        return false;
    }
    void* Value = Property->ContainerPtrToValuePtr<void>(Object);
    const TCHAR* End = Property->ImportText_Direct(
        *Text,
        Value,
        Object,
        PPF_None,
        GLog);
    if (End == nullptr)
    {
        OutError = FString::Printf(
            TEXT("UE could not import the requested value for %s."),
            *Property->GetName());
        return false;
    }
    while (*End != TEXT('\0') && FChar::IsWhitespace(*End))
    {
        ++End;
    }
    if (*End != TEXT('\0'))
    {
        OutError = FString::Printf(
            TEXT("The requested value for %s contains unconsumed text."),
            *Property->GetName());
        return false;
    }
    return true;
}

FString PCGExportScalarValue(
    const FProperty* Property,
    const UObject* Object)
{
    if (Property == nullptr || Object == nullptr)
    {
        return FString();
    }
    const void* Value = Property->ContainerPtrToValuePtr<void>(Object);
    FString Exported;
    Property->ExportText_Direct(
        Exported,
        Value,
        Value,
        const_cast<UObject*>(Object),
        PPF_None);
    return Exported;
}

bool PCGIsScalarSettingsField(const FProperty* Property)
{
    if (Property == nullptr
        || Property->HasAnyPropertyFlags(
            CPF_Transient | CPF_DuplicateTransient | CPF_Deprecated)
        || !Property->HasAnyPropertyFlags(CPF_Edit)
        || Property->HasAnyPropertyFlags(CPF_EditorOnly))
    {
        return false;
    }
    return Property->IsA(FBoolProperty::StaticClass())
        || Property->IsA(FIntProperty::StaticClass())
        || Property->IsA(FInt64Property::StaticClass())
        || Property->IsA(FFloatProperty::StaticClass())
        || Property->IsA(FDoubleProperty::StaticClass())
        || Property->IsA(FStrProperty::StaticClass())
        || Property->IsA(FNameProperty::StaticClass());
}

struct FPcgPlannedEdit
{
    FString Kind; // "set" | "reset" | "move"
    FString RefText;
    TWeakObjectPtr<UPCGNode> Node;
    FProperty* Property = nullptr;
    FString Before;
    FString After;
    int32 MoveX = 0;
    int32 MoveY = 0;
};

bool ReadPcgPinRef(
    const TSharedPtr<FJsonObject>& Ref,
    FString& OutNode,
    FString& OutDirection,
    FString& OutLabel)
{
    OutNode.Reset();
    OutDirection.Reset();
    OutLabel.Reset();
    FString Kind;
    if (!Ref.IsValid()
        || !Ref->TryGetStringField(TEXT("kind"), Kind)
        || Kind != TEXT("pin")
        || !Ref->TryGetStringField(TEXT("node"), OutNode)
        || !Ref->TryGetStringField(TEXT("direction"), OutDirection)
        || !Ref->TryGetStringField(TEXT("label"), OutLabel))
    {
        return false;
    }
    return !OutNode.IsEmpty() && !OutLabel.IsEmpty();
}

// PCG save persists only the outermost package that owns the exact PCG Graph
// Target. Related subgraph assets and external Settings assets are never
// included in the closure.
bool PCGSaveBuildClosure(
    const FSalResolvedTarget& Target,
    UPCGGraph* Graph,
    FString& OutPackageName,
    bool& OutDirty,
    bool& OutPersistentPath,
    FString& OutReason)
{
    OutPackageName.Reset();
    OutDirty = false;
    OutPersistentPath = false;
    OutReason.Reset();
    UPackage* Package = Graph != nullptr ? Graph->GetOutermost() : nullptr;
    if (Package == nullptr
        || Package == GetTransientPackage()
        || Package->HasAnyFlags(RF_Transient)
        || FPackageName::IsTempPackage(Package->GetName()))
    {
        OutReason = TEXT(
            "The exact PCG Graph Target has no persistent package to save.");
        return false;
    }
    OutPackageName = Package->GetName();
    OutDirty = Package->IsDirty();
    OutPersistentPath = !FPackageName::IsTempPackage(Package->GetName());
    return true;
}

TSharedPtr<FJsonObject> PCGSavePlanObject(
    const FSalResolvedTarget& Target,
    const FString& PackageName,
    const bool bDirty,
    const bool bPersistentPath,
    const FString& Status)
{
    TSharedPtr<FJsonObject> Plan = MakeShared<FJsonObject>();
    Plan->SetStringField(TEXT("operation"), TEXT("save"));
    Plan->SetStringField(TEXT("assetPath"), Target.AssetPath);
    Plan->SetStringField(TEXT("status"), Status);
    TArray<TSharedPtr<FJsonValue>> Closure;
    TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
    Entry->SetStringField(TEXT("package"), PackageName);
    Entry->SetStringField(TEXT("role"), TEXT("graph"));
    Entry->SetBoolField(TEXT("dirty"), bDirty);
    Entry->SetBoolField(TEXT("persistentPath"), bPersistentPath);
    Closure.Add(MakeShared<FJsonValueObject>(Entry));
    Plan->SetArrayField(TEXT("closure"), Closure);
    return Plan;
}

TSharedPtr<FJsonObject> FSalPCGInterface::Patch(
    const FSalPatch& Patch,
    const FSalResolvedTarget& Target)
{
    const TSharedPtr<FJsonObject> NoObjects =
        FSalObjectBuilder().BuildObject();

    if (Target.Domain != ESalDomain::Pcg || !IsValid(Target.Object))
    {
        return MakeMutationResult(
            NoObjects,
            {FSalDiagnostics::Error(
                    TEXT("validation.exact_pcg_graph_required"),
                    TEXT("PCG Patch requires the canonical exact loaded "
                        "PCG Graph Target."))
                .Interface(TEXT("pcg"))
                .Build()},
            Patch.bDryRun,
            false,
            false,
            Target.AssetPath,
            TEXT("pcg"));
    }
    UPCGGraph* Graph = Cast<UPCGGraph>(Target.Object);
    if (Graph == nullptr
        || Graph->GetOutermost() == GetTransientPackage()
        || Graph->GetOutermost()->HasAnyFlags(RF_Transient)
        || FPackageName::IsTempPackage(Graph->GetOutermost()->GetName()))
    {
        return MakeMutationResult(
            NoObjects,
            {FSalDiagnostics::Error(
                    TEXT("validation.exact_pcg_graph_required"),
                    TEXT("PCG Patch requires a persistent authored PCG "
                        "Graph asset."))
                .Interface(TEXT("pcg"))
                .Build()},
            Patch.bDryRun,
            false,
            false,
            Target.AssetPath,
            TEXT("pcg"));
    }

    // Terminal save: exactly one `save` statement, never mixed with authored
    // edits. Persists only the outermost package owning the exact Graph
    // Target; a dry run is advisory and never executes native PreSave.
    if (Patch.Statements.Num() == 1)
    {
        const TSharedPtr<FJsonObject>* Statement = nullptr;
        FString StatementKind;
        if (Patch.Statements[0].IsValid()
            && Patch.Statements[0]->TryGetObject(Statement)
            && Statement != nullptr
            && (*Statement).IsValid()
            && (*Statement)->TryGetStringField(
                TEXT("kind"),
                StatementKind)
            && StatementKind == TEXT("save"))
        {
            FString PackageName;
            bool bDirty = false;
            bool bPersistentPath = false;
            FString ClosureReason;
            if (!PCGSaveBuildClosure(
                    Target,
                    Graph,
                    PackageName,
                    bDirty,
                    bPersistentPath,
                    ClosureReason))
            {
                return MakeMutationResult(
                    NoObjects,
                    {FSalDiagnostics::Error(
                            TEXT("validation.save_plan_unavailable"),
                            ClosureReason)
                        .Interface(TEXT("pcg"))
                        .Operation(TEXT("save"))
                        .Build()},
                    Patch.bDryRun,
                    false,
                    false,
                    Target.AssetPath,
                    TEXT("save"));
            }
            const TSharedPtr<FJsonObject> Plan =
                PCGSavePlanObject(
                    Target,
                    PackageName,
                    bDirty,
                    bPersistentPath,
                    bDirty ? TEXT("dirty") : TEXT("clean"));
            if (Patch.bDryRun || !bDirty)
            {
                return MakeMutationResult(
                    NoObjects,
                    {},
                    Patch.bDryRun,
                    true,
                    false,
                    Target.AssetPath,
                    TEXT("save"),
                    Plan);
            }
            // Source-control and read-only preflight on the exact closure.
            const FString FileName =
                FPackageName::LongPackageNameToFilename(
                    PackageName,
                    FPackageName::GetAssetPackageExtension());
            if (IFileManager::Get().IsReadOnly(*FileName))
            {
                return MakeMutationResult(
                    NoObjects,
                    {FSalDiagnostics::Error(
                            TEXT("validation.package_read_only"),
                            FString::Printf(
                                TEXT("The PCG-owned package %s is "
                                    "read-only on disk."),
                                *PackageName))
                        .Interface(TEXT("pcg"))
                        .Operation(TEXT("save"))
                        .Build()},
                    false,
                    false,
                    false,
                    Target.AssetPath,
                    TEXT("save"),
                    Plan);
            }
            UPackage* Package = FindPackage(
                nullptr,
                *PackageName);
            if (Package == nullptr
                || !UEditorLoadingAndSavingUtils::SavePackages(
                    {Package},
                    true))
            {
                return MakeMutationResult(
                    NoObjects,
                    {FSalDiagnostics::Error(
                            TEXT("validation.save_failed"),
                            TEXT("UE failed to save the PCG-owned dirty "
                                "closure."))
                        .Interface(TEXT("pcg"))
                        .Operation(TEXT("save"))
                        .Build()},
                    false,
                    false,
                    false,
                    Target.AssetPath,
                    TEXT("save"),
                    Plan);
            }
            return MakeMutationResult(
                NoObjects,
                {},
                false,
                true,
                true,
                Target.AssetPath,
                TEXT("save"),
                Plan);
        }
    }

    TArray<TSharedPtr<FJsonObject>> Diagnostics;
    TMap<FString, FString> Bindings; // alias -> palette id
    for (const TSharedPtr<FJsonValue>& StatementValue : Patch.Statements)
    {
        const TSharedPtr<FJsonObject>* Statement = nullptr;
        if (!StatementValue.IsValid()
            || !StatementValue->TryGetObject(Statement)
            || Statement == nullptr
            || !(*Statement).IsValid())
        {
            continue;
        }
        const bool bBinding = (*Statement)->HasField(TEXT("target"))
            && (*Statement)->HasField(TEXT("value"))
            && !(*Statement)->HasField(TEXT("kind"));
        if (!bBinding)
        {
            continue;
        }
        const TSharedPtr<FJsonObject>* TargetRef = nullptr;
        const TSharedPtr<FJsonObject>* Value = nullptr;
        FString Alias;
        FString ValueKind;
        const TSharedPtr<FJsonObject>* Args = nullptr;
        if ((*Statement)->TryGetObjectField(TEXT("target"), TargetRef)
            && TargetRef != nullptr
            && (*TargetRef)->TryGetStringField(TEXT("name"), Alias)
            && (*Statement)->TryGetObjectField(TEXT("value"), Value)
            && Value != nullptr
            && (*Value)->TryGetStringField(TEXT("kind"), ValueKind)
            && ValueKind == TEXT("call")
            && (*Value)->TryGetObjectField(TEXT("args"), Args)
            && Args != nullptr)
        {
            FString PaletteId;
            if ((*Args)->TryGetStringField(TEXT("palette"), PaletteId)
                && !PaletteId.IsEmpty())
            {
                Bindings.Add(Alias, PaletteId);
            }
        }
    }

    struct FCreatedNode
    {
        FString Alias;
        FString PaletteId;
        TWeakObjectPtr<UPCGNode> Node;
    };
    struct FPendingEdge
    {
        FString Kind;
        TWeakObjectPtr<UPCGNode> From;
        FString FromLabel;
        TWeakObjectPtr<UPCGNode> To;
        FString ToLabel;
    };
    TArray<FCreatedNode> Created;
    TArray<TWeakObjectPtr<UPCGNode>> Removed;
    TArray<FPendingEdge> PendingEdges;
    TArray<TSharedPtr<FPcgPlannedEdit>> Edits;
    for (const TSharedPtr<FJsonValue>& StatementValue : Patch.Statements)
    {
        const TSharedPtr<FJsonObject>* Statement = nullptr;
        if (!StatementValue.IsValid()
            || !StatementValue->TryGetObject(Statement)
            || Statement == nullptr
            || !(*Statement).IsValid())
        {
            continue;
        }
        const bool bBinding = (*Statement)->HasField(TEXT("target"))
            && (*Statement)->HasField(TEXT("value"))
            && !(*Statement)->HasField(TEXT("kind"));
        if (bBinding)
        {
            continue;
        }
        FString Kind;
        (*Statement)->TryGetStringField(TEXT("kind"), Kind);
        if (Kind == TEXT("remove"))
        {
            const TSharedPtr<FJsonObject>* TargetRef = nullptr;
            FString TargetKind;
            if (!(*Statement)->TryGetObjectField(TEXT("target"), TargetRef)
                || TargetRef == nullptr
                || !(*TargetRef).IsValid()
                || !(*TargetRef)->TryGetStringField(TEXT("kind"), TargetKind)
                || TargetKind != TEXT("node"))
            {
                Diagnostics.Add(
                    FSalDiagnostics::Error(
                        TEXT("validation.edit_target_invalid"),
                        TEXT("PCG remove requires one exact Node "
                            "StableRef."))
                        .Interface(TEXT("pcg"))
                        .Operation(TEXT("remove"))
                        .Build());
                continue;
            }
            FString NodeId;
            if (!(*TargetRef)->TryGetStringField(TEXT("id"), NodeId))
            {
                Diagnostics.Add(
                    FSalDiagnostics::Error(
                        TEXT("validation.edit_target_invalid"),
                        TEXT("PCG remove Node identity is invalid."))
                        .Interface(TEXT("pcg"))
                        .Operation(TEXT("remove"))
                        .Build());
                continue;
            }
            bool bAmbiguous = false;
            UPCGNode* Node = FindNode(Graph, NodeId, bAmbiguous);
            if (Node == nullptr)
            {
                Diagnostics.Add(
                    FSalDiagnostics::Error(
                        bAmbiguous
                            ? TEXT("resolution.identity_conflict")
                            : TEXT("resolution.node_not_found"),
                        bAmbiguous
                            ? TEXT("Multiple PCG Nodes share this identity; "
                                "the removal is ambiguous.")
                            : TEXT("PCG Node was not found in the bound "
                                "Graph."))
                        .Interface(TEXT("pcg"))
                        .Operation(TEXT("remove"))
                        .Ref(NodeId)
                        .Build());
                continue;
            }
            if (Node == Graph->GetInputNode()
                || Node == Graph->GetOutputNode())
            {
                Diagnostics.Add(
                    FSalDiagnostics::Error(
                        TEXT("validation.required_node_protected"),
                        TEXT("Removal of the Graph default input or output "
                            "Node is not allowed."))
                        .Interface(TEXT("pcg"))
                        .Operation(TEXT("remove"))
                        .Ref(NodeId)
                        .Build());
                continue;
            }
            Removed.Add(Node);
            continue;
        }
        if (Kind == TEXT("connect") || Kind == TEXT("disconnect"))
        {
            const TSharedPtr<FJsonObject>* From = nullptr;
            const TSharedPtr<FJsonObject>* To = nullptr;
            FString FromNode;
            FString FromDirection;
            FString FromLabel;
            FString ToNode;
            FString ToDirection;
            FString ToLabel;
            if (!(*Statement)->TryGetObjectField(TEXT("from"), From)
                || From == nullptr
                || !ReadPcgPinRef(*From, FromNode, FromDirection, FromLabel)
                || !(*Statement)->TryGetObjectField(TEXT("to"), To)
                || To == nullptr
                || !ReadPcgPinRef(*To, ToNode, ToDirection, ToLabel))
            {
                Diagnostics.Add(
                    FSalDiagnostics::Error(
                        TEXT("validation.edit_target_invalid"),
                        TEXT("PCG connect/disconnect requires one exact "
                            "output Pin and one exact input Pin."))
                        .Interface(TEXT("pcg"))
                        .Operation(Kind)
                        .Build());
                continue;
            }
            bool bFromAmbiguous = false;
            UPCGNode* FromNodePtr = FindNode(
                Graph, FromNode, bFromAmbiguous);
            bool bToAmbiguous = false;
            UPCGNode* ToNodePtr = FindNode(Graph, ToNode, bToAmbiguous);
            if (FromNodePtr == nullptr || ToNodePtr == nullptr)
            {
                Diagnostics.Add(
                    FSalDiagnostics::Error(
                        TEXT("resolution.node_not_found"),
                        TEXT("PCG connect/disconnect references an "
                            "unknown Node."))
                        .Interface(TEXT("pcg"))
                        .Operation(Kind)
                        .Build());
                continue;
            }
            if (FromDirection != TEXT("out") || ToDirection != TEXT("in"))
            {
                Diagnostics.Add(
                    FSalDiagnostics::Error(
                        TEXT("validation.edit_target_invalid"),
                        TEXT("PCG connect requires an output source Pin "
                            "and an input destination Pin."))
                        .Interface(TEXT("pcg"))
                        .Operation(Kind)
                        .Build());
                continue;
            }
            const UPCGPin* FromPin = FindPin(
                FromNodePtr,
                TEXT("out"),
                FromLabel,
                bFromAmbiguous);
            const UPCGPin* ToPin = FindPin(
                ToNodePtr,
                TEXT("in"),
                ToLabel,
                bToAmbiguous);
            if (FromPin == nullptr || ToPin == nullptr)
            {
                Diagnostics.Add(
                    FSalDiagnostics::Error(
                        TEXT("resolution.pin_not_found"),
                        TEXT("PCG connect/disconnect references an "
                            "unknown Pin."))
                        .Interface(TEXT("pcg"))
                        .Operation(Kind)
                        .Build());
                continue;
            }
            PendingEdges.Add({
                Kind,
                TWeakObjectPtr<UPCGNode>(FromNodePtr),
                FromLabel,
                TWeakObjectPtr<UPCGNode>(ToNodePtr),
                ToLabel,
            });
            continue;
        }
        if (Kind == TEXT("set") || Kind == TEXT("reset"))
        {
            const TSharedPtr<FJsonObject>* TargetRef = nullptr;
            const TSharedPtr<FJsonObject>* Owner = nullptr;
            const TArray<TSharedPtr<FJsonValue>>* Path = nullptr;
            FString TargetKind;
            FString OwnerKind;
            FString NodeId;
            FString FieldName;
            if (!(*Statement)->TryGetObjectField(TEXT("target"), TargetRef)
                || TargetRef == nullptr
                || !(*TargetRef).IsValid()
                || !(*TargetRef)->TryGetStringField(TEXT("kind"), TargetKind)
                || TargetKind != TEXT("member")
                || !(*TargetRef)->TryGetObjectField(TEXT("object"), Owner)
                || Owner == nullptr
                || !(*Owner).IsValid()
                || !(*Owner)->TryGetStringField(TEXT("kind"), OwnerKind)
                || OwnerKind != TEXT("node")
                || !(*Owner)->TryGetStringField(TEXT("id"), NodeId)
                || !(*TargetRef)->TryGetArrayField(TEXT("path"), Path)
                || Path == nullptr
                || Path->Num() != 1
                || !(*Path)[0]->TryGetString(FieldName))
            {
                Diagnostics.Add(
                    FSalDiagnostics::Error(
                        TEXT("validation.edit_target_invalid"),
                        TEXT("PCG set/reset requires one exact Node member "
                            "field on the graph-owned Settings."))
                        .Interface(TEXT("pcg"))
                        .Operation(Kind)
                        .Build());
                continue;
            }
            bool bAmbiguous = false;
            UPCGNode* Node = FindNode(Graph, NodeId, bAmbiguous);
            if (Node == nullptr)
            {
                Diagnostics.Add(
                    FSalDiagnostics::Error(
                        bAmbiguous
                            ? TEXT("resolution.identity_conflict")
                            : TEXT("resolution.node_not_found"),
                        TEXT("PCG set/reset Node was not found."))
                        .Interface(TEXT("pcg"))
                        .Operation(Kind)
                        .Ref(NodeId)
                        .Build());
                continue;
            }
            UPCGSettings* Settings = Node->GetSettings();
            if (Settings == nullptr)
            {
                Diagnostics.Add(
                    FSalDiagnostics::Error(
                        TEXT("validation.settings_unavailable"),
                        TEXT("PCG set/reset requires graph-owned Settings; "
                            "external Settings instances are read-only "
                            "here."))
                        .Interface(TEXT("pcg"))
                        .Operation(Kind)
                        .Ref(NodeId)
                        .Build());
                continue;
            }
            FProperty* Property = FindFProperty<FProperty>(
                Settings->GetClass(),
                FName(*FieldName));
            if (Property == nullptr
                || !PCGIsScalarSettingsField(Property))
            {
                Diagnostics.Add(
                    FSalDiagnostics::Error(
                        TEXT("validation.edit_target_invalid"),
                        TEXT("PCG Settings field is not a certified scalar "
                            "set/reset field on this exact node."))
                        .Interface(TEXT("pcg"))
                        .Operation(Kind)
                        .Ref(NodeId + TEXT(".") + FieldName)
                        .Build());
                continue;
            }
            TSharedPtr<FPcgPlannedEdit> Edit =
                MakeShared<FPcgPlannedEdit>();
            Edit->Kind = Kind;
            Edit->RefText = NodeId + TEXT(".") + FieldName;
            Edit->Node = Node;
            Edit->Property = Property;
            Edit->Before = PCGExportScalarValue(Property, Settings);
            if (Kind == TEXT("set"))
            {
                const TSharedPtr<FJsonValue> Value =
                    (*Statement)->TryGetField(TEXT("value"));
                FString Text;
                FString Error;
                if (!Value.IsValid()
                    || !PCGValueImportText(Value, Text)
                    || !PCGImportScalarValue(
                        Property,
                        Settings,
                        Text,
                        Error))
                {
                    Diagnostics.Add(
                        FSalDiagnostics::Error(
                            TEXT("validation.value_invalid"),
                            Error.IsEmpty()
                                ? TEXT("PCG set value must be a scalar "
                                    "string, number, or Boolean.")
                                : Error)
                            .Interface(TEXT("pcg"))
                            .Operation(Kind)
                            .Ref(Edit->RefText)
                            .Build());
                    continue;
                }
                Edit->After = Text;
            }
            else
            {
                const UObject* Archetype = Settings->GetArchetype();
                if (Archetype == nullptr)
                {
                    Diagnostics.Add(
                        FSalDiagnostics::Error(
                            TEXT("validation.reset_source_unavailable"),
                            TEXT("PCG reset could not resolve one exact "
                                "native archetype value."))
                            .Interface(TEXT("pcg"))
                            .Operation(Kind)
                            .Ref(Edit->RefText)
                            .Build());
                    continue;
                }
                Edit->After = PCGExportScalarValue(
                    Property,
                    Archetype);
            }
            Edits.Add(Edit);
            continue;
        }
        if (Kind == TEXT("move"))
        {
            const TSharedPtr<FJsonObject>* TargetRef = nullptr;
            FString TargetKind;
            FString NodeId;
            const TArray<TSharedPtr<FJsonValue>>* Position = nullptr;
            const TSharedPtr<FJsonValue> To =
                (*Statement)->TryGetField(TEXT("to"));
            if (!(*Statement)->TryGetObjectField(TEXT("target"), TargetRef)
                || TargetRef == nullptr
                || !(*TargetRef).IsValid()
                || !(*TargetRef)->TryGetStringField(TEXT("kind"), TargetKind)
                || TargetKind != TEXT("node")
                || !(*TargetRef)->TryGetStringField(TEXT("id"), NodeId)
                || !To.IsValid()
                || !To->TryGetArray(Position)
                || Position == nullptr
                || Position->Num() != 2)
            {
                Diagnostics.Add(
                    FSalDiagnostics::Error(
                        TEXT("validation.edit_target_invalid"),
                        TEXT("PCG move requires one exact Node and an "
                            "absolute to (x, y) position."))
                        .Interface(TEXT("pcg"))
                        .Operation(TEXT("move"))
                        .Build());
                continue;
            }
            double X = 0.0;
            double Y = 0.0;
            if (!(*Position)[0].IsValid()
                || !(*Position)[0]->TryGetNumber(X)
                || !(*Position)[1].IsValid()
                || !(*Position)[1]->TryGetNumber(Y))
            {
                Diagnostics.Add(
                    FSalDiagnostics::Error(
                        TEXT("validation.edit_target_invalid"),
                        TEXT("PCG move position must be two numbers."))
                        .Interface(TEXT("pcg"))
                        .Operation(TEXT("move"))
                        .Build());
                continue;
            }
            bool bAmbiguous = false;
            UPCGNode* Node = FindNode(Graph, NodeId, bAmbiguous);
            if (Node == nullptr)
            {
                Diagnostics.Add(
                    FSalDiagnostics::Error(
                        bAmbiguous
                            ? TEXT("resolution.identity_conflict")
                            : TEXT("resolution.node_not_found"),
                        TEXT("PCG move Node was not found."))
                        .Interface(TEXT("pcg"))
                        .Operation(TEXT("move"))
                        .Ref(NodeId)
                        .Build());
                continue;
            }
            TSharedPtr<FPcgPlannedEdit> Edit =
                MakeShared<FPcgPlannedEdit>();
            Edit->Kind = TEXT("move");
            Edit->RefText = NodeId;
            Edit->Node = Node;
            Edit->MoveX = FMath::RoundToInt(X);
            Edit->MoveY = FMath::RoundToInt(Y);
            int32 PositionX = 0;
            int32 PositionY = 0;
            Node->GetNodePosition(PositionX, PositionY);
            Edit->Before = FString::Printf(
                TEXT("(%d, %d)"), PositionX, PositionY);
            Edit->After = FString::Printf(
                TEXT("(%d, %d)"), Edit->MoveX, Edit->MoveY);
            Edits.Add(Edit);
            continue;
        }
        if (Kind != TEXT("add"))
        {
            FString Reason;
            if (Kind == TEXT("save"))
            {
                Reason = TEXT(
                    "PCG save is a terminal statement: it must be the only "
                    "statement in the Patch and cannot be mixed with "
                    "authored edits.");
            }
            else
            {
                Reason = FString::Printf(
                    TEXT("PCG Patch does not yet support the %s "
                        "statement in this capability; supported "
                        "statements are add, remove, connect, "
                        "disconnect, set, reset, and move."),
                    *Kind);
            }
            Diagnostics.Add(
                FSalDiagnostics::Error(
                    TEXT("capability.operation_unavailable"),
                    Reason)
                    .Interface(TEXT("pcg"))
                    .Operation(Kind)
                    .Build());
            continue;
        }
        const TSharedPtr<FJsonObject>* TargetRef = nullptr;
        FString Alias;
        if (!(*Statement)->TryGetObjectField(TEXT("target"), TargetRef)
            || TargetRef == nullptr
            || !(*TargetRef)->TryGetStringField(TEXT("name"), Alias))
        {
            Diagnostics.Add(
                FSalDiagnostics::Error(
                    TEXT("validation.creation_invalid"),
                    TEXT("PCG add requires one declared creation binding "
                        "alias."))
                    .Interface(TEXT("pcg"))
                    .Operation(TEXT("add"))
                    .Build());
            continue;
        }
        const FString* PaletteId = Bindings.Find(Alias);
        if (PaletteId == nullptr)
        {
            Diagnostics.Add(
                FSalDiagnostics::Error(
                    TEXT("resolution.binding_not_found"),
                    TEXT("PCG add references no declared Palette creation "
                        "binding."))
                    .Interface(TEXT("pcg"))
                    .Operation(TEXT("add"))
                    .Ref(Alias)
                    .Build());
            continue;
        }
        const UPCGSettings* CDO = nullptr;
        if (!ResolvePCGNodePaletteEntry(*PaletteId, CDO))
        {
            Diagnostics.Add(
                FSalDiagnostics::Error(
                    TEXT("resolution.palette_not_found"),
                    TEXT("PCG Palette identity was not found for this "
                        "exact Graph; re-run palette entries."))
                    .Interface(TEXT("pcg"))
                    .Operation(TEXT("add"))
                    .Ref(*PaletteId)
                    .Build());
            continue;
        }
        FCreatedNode CreatedNode;
        CreatedNode.Alias = Alias;
        CreatedNode.PaletteId = *PaletteId;
        Created.Add(MoveTemp(CreatedNode));
    }

    if (!Diagnostics.IsEmpty())
    {
        return MakeMutationResult(
            NoObjects,
            Diagnostics,
            Patch.bDryRun,
            false,
            false,
            Target.AssetPath,
            TEXT("pcg"));
    }
    if (Created.IsEmpty() && Removed.IsEmpty() && PendingEdges.IsEmpty()
        && Edits.IsEmpty())
    {
        return MakeMutationResult(
            NoObjects,
            {FSalDiagnostics::Error(
                    TEXT("validation.no_operations"),
                    TEXT("PCG Patch contained no supported statements."))
                .Interface(TEXT("pcg"))
                .Build()},
            Patch.bDryRun,
            false,
            false,
            Target.AssetPath,
            TEXT("pcg"));
    }

    if (Patch.bDryRun)
    {
        TSharedPtr<FJsonObject> Planned = MakeShared<FJsonObject>();
        TArray<TSharedPtr<FJsonValue>> Operations;
        for (const FCreatedNode& CreatedNode : Created)
        {
            TSharedPtr<FJsonObject> Operation = MakeShared<FJsonObject>();
            Operation->SetStringField(TEXT("kind"), TEXT("add"));
            Operation->SetStringField(TEXT("alias"), CreatedNode.Alias);
            Operation->SetStringField(
                TEXT("palette"),
                CreatedNode.PaletteId);
            Operations.Add(MakeShared<FJsonValueObject>(Operation));
        }
        for (const TWeakObjectPtr<UPCGNode>& RemovedNode : Removed)
        {
            if (UPCGNode* Node = RemovedNode.Get())
            {
                TSharedPtr<FJsonObject> Operation = MakeShared<FJsonObject>();
                Operation->SetStringField(TEXT("kind"), TEXT("remove"));
                Operation->SetStringField(
                    TEXT("ref"),
                    Node->GetFName().ToString());
                Operations.Add(MakeShared<FJsonValueObject>(Operation));
            }
        }
        for (const FPendingEdge& Edge : PendingEdges)
        {
            TSharedPtr<FJsonObject> Operation = MakeShared<FJsonObject>();
            Operation->SetStringField(TEXT("kind"), Edge.Kind);
            if (UPCGNode* From = Edge.From.Get())
            {
                Operation->SetStringField(
                    TEXT("from"),
                    From->GetFName().ToString() + TEXT("/out/")
                        + Edge.FromLabel);
            }
            if (UPCGNode* To = Edge.To.Get())
            {
                Operation->SetStringField(
                    TEXT("to"),
                    To->GetFName().ToString() + TEXT("/in/")
                        + Edge.ToLabel);
            }
            Operations.Add(MakeShared<FJsonValueObject>(Operation));
        }
        for (const TSharedPtr<FPcgPlannedEdit>& Edit : Edits)
        {
            TSharedPtr<FJsonObject> Operation = MakeShared<FJsonObject>();
            Operation->SetStringField(TEXT("kind"), Edit->Kind);
            Operation->SetStringField(TEXT("ref"), Edit->RefText);
            Operation->SetStringField(TEXT("before"), Edit->Before);
            Operation->SetStringField(TEXT("after"), Edit->After);
            Operations.Add(MakeShared<FJsonValueObject>(Operation));
        }
        Planned->SetArrayField(TEXT("operations"), Operations);
        return MakeMutationResult(
            NoObjects,
            {},
            true,
            true,
            false,
            Target.AssetPath,
            TEXT("pcg"),
            Planned);
    }

    FScopedTransaction Transaction(
        FText::FromString(TEXT("SAL Edit PCG Graph")));
    if (!Transaction.IsOutstanding())
    {
        return MakeMutationResult(
            NoObjects,
            {FSalDiagnostics::Error(
                    TEXT("capability.transaction_unavailable"),
                    TEXT("UE did not open the required PCG Patch "
                        "transaction."))
                .Interface(TEXT("pcg"))
                .Build()},
            false,
            false,
            false,
            Target.AssetPath,
            TEXT("pcg"));
    }
    Graph->Modify();
    FSalObjectBuilder ResultBuilder;
    FPCGPatchAliasAllocator ResultAliases;
    for (FCreatedNode& CreatedNode : Created)
    {
        const UPCGSettings* CDO = nullptr;
        if (!ResolvePCGNodePaletteEntry(
                CreatedNode.PaletteId,
                CDO)
            || CDO == nullptr)
        {
            Transaction.Cancel();
            return MakeMutationResult(
                NoObjects,
                {FSalDiagnostics::Error(
                        TEXT("resolution.palette_not_found"),
                        TEXT("PCG Palette identity became stale before "
                            "apply; the transaction was rolled back."))
                    .Interface(TEXT("pcg"))
                    .Operation(TEXT("add"))
                    .Build()},
                false,
                false,
                false,
                Target.AssetPath,
                TEXT("pcg"));
        }
        UPCGSettings* DefaultSettings = nullptr;
        UPCGNode* NewNode = Graph->AddNodeOfType(
            CDO->GetClass(),
            DefaultSettings);
        if (NewNode == nullptr)
        {
            Transaction.Cancel();
            return MakeMutationResult(
                NoObjects,
                {FSalDiagnostics::Error(
                        TEXT("validation.apply_failed"),
                        TEXT("UE failed to create the requested PCG "
                            "Node."))
                    .Interface(TEXT("pcg"))
                    .Operation(TEXT("add"))
                    .Ref(CreatedNode.PaletteId)
                    .Build()},
                false,
                false,
                false,
                Target.AssetPath,
                TEXT("pcg"));
        }
        CreatedNode.Node = NewNode;
        TSharedPtr<FJsonObject> Args = MakeShared<FJsonObject>();
        Args->SetStringField(
            TEXT("id"),
            NewNode->GetFName().ToString());
        Args->SetStringField(
            TEXT("type"),
            NewNode->GetClass()->GetPathName());
        ResultBuilder.AddLocalBinding(
            ResultAliases.Allocate(CreatedNode.Alias, TEXT("node")),
            Value::Call(TEXT("node"), Args));
    }
    for (const TSharedPtr<FPcgPlannedEdit>& Edit : Edits)
    {
        UPCGNode* Node = Edit->Node.Get();
        if (!IsValid(Node))
        {
            Transaction.Cancel();
            return MakeMutationResult(
                NoObjects,
                {FSalDiagnostics::Error(
                        TEXT("validation.object_invalidated"),
                        TEXT("A PCG edit target was invalidated before "
                            "apply; the transaction was rolled back."))
                    .Interface(TEXT("pcg"))
                    .Operation(Edit->Kind)
                    .Ref(Edit->RefText)
                    .Build()},
                false,
                false,
                false,
                Target.AssetPath,
                TEXT("pcg"));
        }
        if (Edit->Kind == TEXT("move"))
        {
            Node->Modify();
            Node->SetNodePosition(Edit->MoveX, Edit->MoveY);
            continue;
        }
        UPCGSettings* Settings = Node->GetSettings();
        if (Settings == nullptr)
        {
            Transaction.Cancel();
            return MakeMutationResult(
                NoObjects,
                {FSalDiagnostics::Error(
                        TEXT("validation.settings_unavailable"),
                        TEXT("PCG Settings became unavailable before "
                            "apply; the transaction was rolled back."))
                    .Interface(TEXT("pcg"))
                    .Operation(Edit->Kind)
                    .Ref(Edit->RefText)
                    .Build()},
                false,
                false,
                false,
                Target.AssetPath,
                TEXT("pcg"));
        }
        Settings->Modify();
        FString Error;
        if (!PCGImportScalarValue(
                Edit->Property,
                Settings,
                Edit->After,
                Error))
        {
            Transaction.Cancel();
            return MakeMutationResult(
                NoObjects,
                {FSalDiagnostics::Error(
                        TEXT("validation.apply_failed"),
                        Error)
                    .Interface(TEXT("pcg"))
                    .Operation(Edit->Kind)
                    .Ref(Edit->RefText)
                    .Build()},
                false,
                false,
                false,
                Target.AssetPath,
                TEXT("pcg"));
        }
        Settings->OnSettingsChangedDelegate.Broadcast(
            Settings,
            EPCGChangeType::Settings | EPCGChangeType::Cosmetic);
    }
    for (const TWeakObjectPtr<UPCGNode>& RemovedNode : Removed)
    {
        UPCGNode* Node = RemovedNode.Get();
        if (!IsValid(Node))
        {
            Transaction.Cancel();
            return MakeMutationResult(
                NoObjects,
                {FSalDiagnostics::Error(
                        TEXT("validation.object_invalidated"),
                        TEXT("A PCG removal target was invalidated before "
                            "apply; the transaction was rolled back."))
                    .Interface(TEXT("pcg"))
                    .Operation(TEXT("remove"))
                    .Build()},
                false,
                false,
                false,
                Target.AssetPath,
                TEXT("pcg"));
        }
        Graph->Modify();
        Graph->RemoveNode(Node);
    }
    for (const FPendingEdge& Edge : PendingEdges)
    {
        UPCGNode* From = Edge.From.Get();
        UPCGNode* To = Edge.To.Get();
        if (!IsValid(From) || !IsValid(To))
        {
            Transaction.Cancel();
            return MakeMutationResult(
                NoObjects,
                {FSalDiagnostics::Error(
                        TEXT("validation.object_invalidated"),
                        TEXT("A PCG edge endpoint was invalidated before "
                            "apply; the transaction was rolled back."))
                    .Interface(TEXT("pcg"))
                    .Operation(Edge.Kind)
                    .Build()},
                false,
                false,
                false,
                Target.AssetPath,
                TEXT("pcg"));
        }
        Graph->Modify();
        if (Edge.Kind == TEXT("connect"))
        {
            if (!Graph->AddEdge(
                    From,
                    FName(*Edge.FromLabel),
                    To,
                    FName(*Edge.ToLabel)))
            {
                Transaction.Cancel();
                return MakeMutationResult(
                    NoObjects,
                    {FSalDiagnostics::Error(
                            TEXT("validation.connect_failed"),
                            TEXT("UE rejected the requested PCG edge."))
                        .Interface(TEXT("pcg"))
                        .Operation(TEXT("connect"))
                        .Build()},
                    false,
                    false,
                    false,
                    Target.AssetPath,
                    TEXT("pcg"));
            }
        }
        else if (!Graph->RemoveEdge(
                     From,
                     FName(*Edge.FromLabel),
                     To,
                     FName(*Edge.ToLabel)))
        {
            Transaction.Cancel();
            return MakeMutationResult(
                NoObjects,
                {FSalDiagnostics::Error(
                        TEXT("validation.disconnect_failed"),
                        TEXT("UE rejected the requested PCG edge "
                            "disconnect."))
                    .Interface(TEXT("pcg"))
                    .Operation(TEXT("disconnect"))
                    .Build()},
                false,
                false,
                false,
                Target.AssetPath,
                TEXT("pcg"));
        }
    }
    return MakeMutationResult(
        ResultBuilder.BuildObject(),
        {},
        false,
        true,
        true,
        Target.AssetPath,
        TEXT("pcg"));
}
}
