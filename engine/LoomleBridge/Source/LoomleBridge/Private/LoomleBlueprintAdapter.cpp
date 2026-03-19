#include "LoomleBlueprintAdapter.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "Components/BoxComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraphSchema_K2.h"
#include "Editor.h"
#include "Engine/Blueprint.h"
#include "Engine/SCS_Node.h"
#include "Engine/SimpleConstructionScript.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "K2Node_CallFunction.h"
#include "K2Node_DynamicCast.h"
#include "K2Node_Event.h"
#include "K2Node_ExecutionSequence.h"
#include "K2Node_IfThenElse.h"
#include "K2Node_MacroInstance.h"
#include "K2Node_Knot.h"
#include "K2Node_Timeline.h"
#include "K2Node_Variable.h"
#include "K2Node_VariableGet.h"
#include "K2Node_VariableSet.h"
#include "EdGraphNode_Comment.h"
#include "Misc/Guid.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Misc/PackageName.h"

namespace LoomleBlueprintAdapterInternal
{
    static TSharedPtr<FJsonObject> MakeLayoutObject(
        int32 PositionX,
        int32 PositionY,
        const FString& Source,
        bool bReliable,
        const TOptional<FVector2D>& Size = TOptional<FVector2D>())
    {
        TSharedPtr<FJsonObject> Layout = MakeShared<FJsonObject>();

        TSharedPtr<FJsonObject> Position = MakeShared<FJsonObject>();
        Position->SetNumberField(TEXT("x"), PositionX);
        Position->SetNumberField(TEXT("y"), PositionY);
        Layout->SetObjectField(TEXT("position"), Position);

        if (Size.IsSet())
        {
            const FVector2D SizeValue = Size.GetValue();

            TSharedPtr<FJsonObject> SizeObject = MakeShared<FJsonObject>();
            SizeObject->SetNumberField(TEXT("width"), SizeValue.X);
            SizeObject->SetNumberField(TEXT("height"), SizeValue.Y);
            Layout->SetObjectField(TEXT("size"), SizeObject);

            TSharedPtr<FJsonObject> Bounds = MakeShared<FJsonObject>();
            Bounds->SetNumberField(TEXT("left"), PositionX);
            Bounds->SetNumberField(TEXT("top"), PositionY);
            Bounds->SetNumberField(TEXT("right"), PositionX + SizeValue.X);
            Bounds->SetNumberField(TEXT("bottom"), PositionY + SizeValue.Y);
            Layout->SetObjectField(TEXT("bounds"), Bounds);
        }

        Layout->SetStringField(TEXT("source"), Source);
        Layout->SetBoolField(TEXT("reliable"), bReliable);
        Layout->SetStringField(TEXT("sizeSource"), Size.IsSet() ? TEXT("model") : TEXT("unsupported"));
        Layout->SetStringField(TEXT("boundsSource"), Size.IsSet() ? TEXT("model") : TEXT("unsupported"));
        return Layout;
    }

    static UBlueprint* LoadBlueprintByAssetPath(const FString& AssetPath)
    {
        if (!FPackageName::IsValidLongPackageName(AssetPath))
        {
            return nullptr;
        }

        const FString AssetName = FPackageName::GetLongPackageAssetName(AssetPath);
        const FString ObjectPath = FString::Printf(TEXT("%s.%s"), *AssetPath, *AssetName);
        return LoadObject<UBlueprint>(nullptr, *ObjectPath);
    }

    static UClass* ResolveClass(const FString& ClassPathOrName)
    {
        if (ClassPathOrName.IsEmpty())
        {
            return nullptr;
        }

        if (UClass* LoadedClass = LoadObject<UClass>(nullptr, *ClassPathOrName))
        {
            return LoadedClass;
        }

        if (UObject* Found = StaticFindObject(UObject::StaticClass(), nullptr, *ClassPathOrName))
        {
            return Cast<UClass>(Found);
        }

        const FString Normalized = ClassPathOrName.StartsWith(TEXT("/Script/"))
            ? ClassPathOrName
            : FString::Printf(TEXT("/Script/Engine.%s"), *ClassPathOrName);
        return LoadObject<UClass>(nullptr, *Normalized);
    }

    static UEdGraph* GetEventGraph(UBlueprint* Blueprint)
    {
        return Blueprint ? FBlueprintEditorUtils::FindEventGraph(Blueprint) : nullptr;
    }

    static UEdGraph* FindGraphByName(UBlueprint* Blueprint, const FString& GraphName)
    {
        if (!Blueprint || GraphName.IsEmpty())
        {
            return nullptr;
        }

        auto Match = [&GraphName](UEdGraph* Graph) -> bool
        {
            return Graph && (Graph->GetName().Equals(GraphName) || Graph->GetFName().ToString().Equals(GraphName));
        };

        for (UEdGraph* Graph : Blueprint->UbergraphPages)
        {
            if (Match(Graph))
            {
                return Graph;
            }
        }
        for (UEdGraph* Graph : Blueprint->FunctionGraphs)
        {
            if (Match(Graph))
            {
                return Graph;
            }
        }
        for (UEdGraph* Graph : Blueprint->MacroGraphs)
        {
            if (Match(Graph))
            {
                return Graph;
            }
        }
        for (const FBPInterfaceDescription& InterfaceDesc : Blueprint->ImplementedInterfaces)
        {
            for (UEdGraph* Graph : InterfaceDesc.Graphs)
            {
                if (Match(Graph))
                {
                    return Graph;
                }
            }
        }
        return nullptr;
    }

    static UEdGraph* ResolveTargetGraph(UBlueprint* Blueprint, const FString& GraphName)
    {
        if (!Blueprint)
        {
            return nullptr;
        }

        if (GraphName.IsEmpty())
        {
            return GetEventGraph(Blueprint);
        }

        if (UEdGraph* NamedGraph = FindGraphByName(Blueprint, GraphName))
        {
            return NamedGraph;
        }

        if (GraphName.Equals(TEXT("EventGraph"), ESearchCase::IgnoreCase))
        {
            return GetEventGraph(Blueprint);
        }

        return nullptr;
    }

    static UEdGraph* ResolveMacroGraph(const FString& MacroLibraryAssetPath, const FString& MacroGraphName)
    {
        if (MacroLibraryAssetPath.IsEmpty() || MacroGraphName.IsEmpty())
        {
            return nullptr;
        }

        UBlueprint* MacroLibrary = LoadBlueprintByAssetPath(MacroLibraryAssetPath);
        return FindGraphByName(MacroLibrary, MacroGraphName);
    }

    static bool ParsePayloadJson(const FString& PayloadJson, TSharedPtr<FJsonObject>& OutPayload)
    {
        OutPayload = MakeShared<FJsonObject>();
        if (PayloadJson.IsEmpty())
        {
            return true;
        }

        TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(PayloadJson);
        return FJsonSerializer::Deserialize(Reader, OutPayload) && OutPayload.IsValid();
    }

    static void AppendGraphListEntries(const TArray<UEdGraph*>& Graphs, const FString& GraphKind, TArray<TSharedPtr<FJsonValue>>& OutGraphs)
    {
        for (UEdGraph* Graph : Graphs)
        {
            if (!Graph)
            {
                continue;
            }

            TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
            Entry->SetStringField(TEXT("graphName"), Graph->GetName());
            Entry->SetStringField(TEXT("graphKind"), GraphKind);
            Entry->SetStringField(TEXT("graphClassPath"), Graph->GetClass() ? Graph->GetClass()->GetPathName() : TEXT(""));
            OutGraphs.Add(MakeShared<FJsonValueObject>(Entry));
        }
    }

    static USCS_Node* FindComponentNode(UBlueprint* Blueprint, const FString& ComponentName)
    {
        if (!Blueprint || !Blueprint->SimpleConstructionScript)
        {
            return nullptr;
        }

        for (USCS_Node* Node : Blueprint->SimpleConstructionScript->GetAllNodes())
        {
            if (Node && Node->GetVariableName().ToString().Equals(ComponentName))
            {
                return Node;
            }
        }
        return nullptr;
    }

    static UEdGraphNode* FindNodeByGuid(UEdGraph* Graph, const FString& NodeGuidText)
    {
        if (!Graph)
        {
            return nullptr;
        }

        FGuid NodeGuid;
        if (!FGuid::Parse(NodeGuidText, NodeGuid))
        {
            return nullptr;
        }

        for (UEdGraphNode* Node : Graph->Nodes)
        {
            if (Node && Node->NodeGuid == NodeGuid)
            {
                return Node;
            }
        }
        return nullptr;
    }

    static UEdGraphNode* FindNodeByPath(UEdGraph* Graph, const FString& NodePath)
    {
        if (!Graph || NodePath.IsEmpty())
        {
            return nullptr;
        }

        for (UEdGraphNode* Node : Graph->Nodes)
        {
            if (Node && Node->GetPathName().Equals(NodePath, ESearchCase::IgnoreCase))
            {
                return Node;
            }
        }
        return nullptr;
    }

    static UEdGraphNode* FindNodeByName(UEdGraph* Graph, const FString& NodeName)
    {
        if (!Graph || NodeName.IsEmpty())
        {
            return nullptr;
        }

        for (UEdGraphNode* Node : Graph->Nodes)
        {
            if (Node && Node->GetName().Equals(NodeName, ESearchCase::IgnoreCase))
            {
                return Node;
            }
        }
        return nullptr;
    }

    static UEdGraphNode* ResolveNodeByToken(UEdGraph* Graph, const FString& NodeToken)
    {
        if (NodeToken.IsEmpty())
        {
            return nullptr;
        }

        if (UEdGraphNode* Node = FindNodeByGuid(Graph, NodeToken))
        {
            return Node;
        }

        if (UEdGraphNode* Node = FindNodeByPath(Graph, NodeToken))
        {
            return Node;
        }

        return FindNodeByName(Graph, NodeToken);
    }

    static FString NormalizePinToken(FString Value)
    {
        Value = Value.ToLower();
        Value.ReplaceInline(TEXT(" "), TEXT(""));
        Value.ReplaceInline(TEXT("_"), TEXT(""));
        Value.ReplaceInline(TEXT("-"), TEXT(""));
        return Value;
    }

    static FString NormalizeIdentifier(FString Value)
    {
        Value = Value.ToLower();
        FString Out;
        Out.Reserve(Value.Len());
        for (const TCHAR Ch : Value)
        {
            if (FChar::IsAlnum(Ch))
            {
                Out.AppendChar(Ch);
            }
        }
        return Out;
    }

    static bool ResolveVariableReferenceFromNode(const UK2Node_Variable* VariableNode, FString& OutMemberName, UClass*& OutOwnerClass, FString& OutSignatureId)
    {
        OutMemberName = VariableNode ? VariableNode->VariableReference.GetMemberName().ToString() : TEXT("");
        OutOwnerClass = VariableNode ? VariableNode->VariableReference.GetMemberParentClass() : nullptr;
        OutSignatureId = VariableNode ? VariableNode->VariableReference.GetMemberGuid().ToString(EGuidFormats::DigitsWithHyphens) : TEXT("");

        if (!VariableNode)
        {
            return false;
        }

        if (const FProperty* Property = VariableNode->GetPropertyForVariable())
        {
            if (OutMemberName.IsEmpty())
            {
                OutMemberName = Property->GetName();
            }
            if (OutOwnerClass == nullptr)
            {
                OutOwnerClass = Property->GetOwnerClass();
            }
            if (OutSignatureId.IsEmpty() || OutSignatureId.Equals(TEXT("00000000-0000-0000-0000-000000000000")))
            {
                OutSignatureId = FString::Printf(TEXT("%s:%s"),
                    OutOwnerClass ? *OutOwnerClass->GetPathName() : TEXT(""),
                    *Property->GetName());
            }
            return true;
        }

        return !OutMemberName.IsEmpty() || OutOwnerClass != nullptr;
    }

    static UEdGraphPin* ResolvePin(UEdGraphNode* Node, const FString& RequestedName)
    {
        if (!Node)
        {
            return nullptr;
        }

        if (UEdGraphPin* Direct = Node->FindPin(*RequestedName))
        {
            return Direct;
        }

        const FString Key = NormalizePinToken(RequestedName);

        if (UK2Node_DynamicCast* DynamicCastNode = Cast<UK2Node_DynamicCast>(Node))
        {
            if (Key.Equals(TEXT("object")) || Key.Equals(TEXT("objecttocast")))
            {
                if (UEdGraphPin* P = DynamicCastNode->GetCastSourcePin()) return P;
            }
            if (Key.Equals(TEXT("castsucceeded")) || Key.Equals(TEXT("success")) || Key.Equals(TEXT("valid")))
            {
                if (UEdGraphPin* P = DynamicCastNode->GetValidCastPin()) return P;
            }
            if (Key.Equals(TEXT("castfailed")) || Key.Equals(TEXT("invalid")) || Key.Equals(TEXT("fail")))
            {
                if (UEdGraphPin* P = DynamicCastNode->GetInvalidCastPin()) return P;
            }
            if (Key.Equals(TEXT("as")) || Key.Equals(TEXT("result")) || Key.Equals(TEXT("castresult")) || Key.StartsWith(TEXT("as")))
            {
                if (UEdGraphPin* P = DynamicCastNode->GetCastResultPin()) return P;
            }
        }

        auto TrySchemaPin = [Node](const FName& SchemaPinName) -> UEdGraphPin*
        {
            return Node->FindPin(SchemaPinName);
        };

        if (Key.Equals(TEXT("execute")) || Key.Equals(TEXT("execin")))
        {
            if (UEdGraphPin* P = TrySchemaPin(UEdGraphSchema_K2::PN_Execute)) return P;
        }
        if (Key.Equals(TEXT("then")) || Key.Equals(TEXT("execout")))
        {
            if (UEdGraphPin* P = TrySchemaPin(UEdGraphSchema_K2::PN_Then)) return P;
        }
        if (Key.Equals(TEXT("self")))
        {
            if (UEdGraphPin* P = TrySchemaPin(UEdGraphSchema_K2::PN_Self)) return P;
        }
        if (Key.Equals(TEXT("object")) || Key.Equals(TEXT("objecttocast")))
        {
            if (UEdGraphPin* P = TrySchemaPin(UEdGraphSchema_K2::PN_ObjectToCast)) return P;
        }
        if (Key.Equals(TEXT("castsucceeded")) || Key.Equals(TEXT("success")))
        {
            if (UEdGraphPin* P = TrySchemaPin(UEdGraphSchema_K2::PN_CastSucceeded)) return P;
        }
        if (Key.Equals(TEXT("input")) || Key.Equals(TEXT("inputpin")))
        {
            if (UEdGraphPin* P = Node->FindPin(TEXT("InputPin"))) return P;
        }
        if (Key.Equals(TEXT("output")) || Key.Equals(TEXT("outputpin")))
        {
            if (UEdGraphPin* P = Node->FindPin(TEXT("OutputPin"))) return P;
        }

        const FString NormalizedRequested = NormalizePinToken(RequestedName);
        for (UEdGraphPin* Pin : Node->Pins)
        {
            if (Pin && NormalizePinToken(Pin->PinName.ToString()).Equals(NormalizedRequested))
            {
                return Pin;
            }
        }
        return nullptr;
    }

    static FString PinDirectionToString(EEdGraphPinDirection Direction)
    {
        switch (Direction)
        {
            case EGPD_Input:
                return TEXT("input");
            case EGPD_Output:
                return TEXT("output");
            default:
                return TEXT("unknown");
        }
    }

    static TSharedPtr<FJsonObject> SerializePin(const UEdGraphPin* Pin)
    {
        TSharedPtr<FJsonObject> PinObject = MakeShared<FJsonObject>();
        if (!Pin)
        {
            return PinObject;
        }

        PinObject->SetStringField(TEXT("name"), Pin->PinName.ToString());
        PinObject->SetStringField(TEXT("direction"), PinDirectionToString(Pin->Direction));
        PinObject->SetStringField(TEXT("category"), Pin->PinType.PinCategory.ToString());
        PinObject->SetStringField(TEXT("subCategory"), Pin->PinType.PinSubCategory.ToString());
        PinObject->SetStringField(TEXT("subCategoryObject"), Pin->PinType.PinSubCategoryObject.IsValid() ? Pin->PinType.PinSubCategoryObject->GetPathName() : TEXT(""));
        PinObject->SetBoolField(TEXT("isReference"), Pin->PinType.bIsReference);
        PinObject->SetBoolField(TEXT("isConst"), Pin->PinType.bIsConst);
        PinObject->SetBoolField(TEXT("isArray"), Pin->PinType.ContainerType == EPinContainerType::Array);
        PinObject->SetStringField(TEXT("defaultValue"), Pin->DefaultValue);
        PinObject->SetStringField(TEXT("defaultObject"), Pin->DefaultObject ? Pin->DefaultObject->GetPathName() : TEXT(""));
        PinObject->SetStringField(TEXT("defaultText"), Pin->DefaultTextValue.ToString());

        TSharedPtr<FJsonObject> PinTypeObject = MakeShared<FJsonObject>();
        PinTypeObject->SetStringField(TEXT("category"), Pin->PinType.PinCategory.ToString());
        PinTypeObject->SetStringField(TEXT("subCategory"), Pin->PinType.PinSubCategory.ToString());
        PinTypeObject->SetStringField(TEXT("object"), Pin->PinType.PinSubCategoryObject.IsValid() ? Pin->PinType.PinSubCategoryObject->GetPathName() : TEXT(""));
        FString ContainerType = TEXT("none");
        if (Pin->PinType.ContainerType == EPinContainerType::Array)
        {
            ContainerType = TEXT("array");
        }
        else if (Pin->PinType.ContainerType == EPinContainerType::Set)
        {
            ContainerType = TEXT("set");
        }
        else if (Pin->PinType.ContainerType == EPinContainerType::Map)
        {
            ContainerType = TEXT("map");
        }
        PinTypeObject->SetStringField(TEXT("container"), ContainerType);
        PinObject->SetObjectField(TEXT("type"), PinTypeObject);

        TSharedPtr<FJsonObject> PinDefaultObject = MakeShared<FJsonObject>();
        PinDefaultObject->SetStringField(TEXT("value"), Pin->DefaultValue);
        PinDefaultObject->SetStringField(TEXT("object"), Pin->DefaultObject ? Pin->DefaultObject->GetPathName() : TEXT(""));
        PinDefaultObject->SetStringField(TEXT("text"), Pin->DefaultTextValue.ToString());
        PinObject->SetObjectField(TEXT("default"), PinDefaultObject);

        TArray<TSharedPtr<FJsonValue>> Links;
        TArray<TSharedPtr<FJsonValue>> SemanticLinks;
        for (const UEdGraphPin* LinkedPin : Pin->LinkedTo)
        {
            if (!LinkedPin)
            {
                continue;
            }

            TSharedPtr<FJsonObject> LinkObject = MakeShared<FJsonObject>();
            LinkObject->SetStringField(TEXT("pin"), LinkedPin->PinName.ToString());

            const UEdGraphNode* LinkedNode = LinkedPin->GetOwningNodeUnchecked();
            if (LinkedNode)
            {
                LinkObject->SetStringField(TEXT("nodeName"), LinkedNode->GetName());
                LinkObject->SetStringField(TEXT("nodeGuid"), LinkedNode->NodeGuid.ToString(EGuidFormats::DigitsWithHyphens));
                LinkObject->SetStringField(TEXT("nodePath"), LinkedNode->GetPathName());
            }

            Links.Add(MakeShared<FJsonValueObject>(LinkObject));

            TSharedPtr<FJsonObject> SemanticLinkObject = MakeShared<FJsonObject>();
            SemanticLinkObject->SetStringField(TEXT("toPin"), LinkedPin->PinName.ToString());
            SemanticLinkObject->SetStringField(TEXT("toNodeId"), LinkedNode ? LinkedNode->NodeGuid.ToString(EGuidFormats::DigitsWithHyphens) : TEXT(""));
            SemanticLinks.Add(MakeShared<FJsonValueObject>(SemanticLinkObject));
        }
        PinObject->SetArrayField(TEXT("linkedTo"), Links);
        PinObject->SetArrayField(TEXT("links"), SemanticLinks);

        return PinObject;
    }

    static TSharedPtr<FJsonObject> SerializeNode(const UEdGraphNode* Node)
    {
        TSharedPtr<FJsonObject> NodeObject = MakeShared<FJsonObject>();
        if (!Node)
        {
            return NodeObject;
        }

        const FString NodeClassPath = Node->GetClass() ? Node->GetClass()->GetPathName() : TEXT("");
        const FString NodeTitle = Node->GetNodeTitle(ENodeTitleType::ListView).ToString();
        NodeObject->SetStringField(TEXT("name"), Node->GetName());
        NodeObject->SetStringField(TEXT("guid"), Node->NodeGuid.ToString(EGuidFormats::DigitsWithHyphens));
        NodeObject->SetStringField(TEXT("id"), Node->NodeGuid.ToString(EGuidFormats::DigitsWithHyphens));
        NodeObject->SetStringField(TEXT("className"), Node->GetClass()->GetName());
        NodeObject->SetStringField(TEXT("classPath"), NodeClassPath);
        NodeObject->SetStringField(TEXT("nodeClassPath"), NodeClassPath);
        NodeObject->SetStringField(TEXT("path"), Node->GetPathName());
        NodeObject->SetNumberField(TEXT("nodePosX"), Node->NodePosX);
        NodeObject->SetNumberField(TEXT("nodePosY"), Node->NodePosY);
        NodeObject->SetStringField(TEXT("nodeTitle"), NodeTitle);
        NodeObject->SetStringField(TEXT("nodeTitleFull"), Node->GetNodeTitle(ENodeTitleType::FullTitle).ToString());
        NodeObject->SetStringField(TEXT("title"), NodeTitle);
        NodeObject->SetBoolField(TEXT("isNodeEnabled"), Node->IsNodeEnabled());
        NodeObject->SetBoolField(TEXT("enabled"), Node->IsNodeEnabled());

        TSharedPtr<FJsonObject> Position = MakeShared<FJsonObject>();
        Position->SetNumberField(TEXT("x"), Node->NodePosX);
        Position->SetNumberField(TEXT("y"), Node->NodePosY);
        NodeObject->SetObjectField(TEXT("position"), Position);
        NodeObject->SetObjectField(
            TEXT("layout"),
            MakeLayoutObject(Node->NodePosX, Node->NodePosY, TEXT("model"), true));

        TSharedPtr<FJsonObject> MemberReference = MakeShared<FJsonObject>();
        MemberReference->SetStringField(TEXT("memberKind"), TEXT(""));
        MemberReference->SetStringField(TEXT("ownerClassPath"), TEXT(""));
        MemberReference->SetStringField(TEXT("memberName"), TEXT(""));
        MemberReference->SetStringField(TEXT("signatureId"), TEXT(""));

        TSharedPtr<FJsonObject> FunctionReference = MakeShared<FJsonObject>();
        FunctionReference->SetStringField(TEXT("classPath"), TEXT(""));
        FunctionReference->SetStringField(TEXT("functionName"), TEXT(""));
        FunctionReference->SetStringField(TEXT("signatureId"), TEXT(""));

        if (const UK2Node_CallFunction* CallNode = Cast<UK2Node_CallFunction>(Node))
        {
            const UFunction* TargetFunction = CallNode->GetTargetFunction();
            UClass* ParentClass = CallNode->FunctionReference.GetMemberParentClass();
            FName MemberName = CallNode->FunctionReference.GetMemberName();
            FGuid MemberGuid = CallNode->FunctionReference.GetMemberGuid();

            // Fallback: some call-function nodes do not carry a complete FunctionReference.
            if ((!ParentClass || MemberName.IsNone()) && TargetFunction)
            {
                ParentClass = TargetFunction->GetOuterUClass();
                MemberName = TargetFunction->GetFName();
            }

            FString SignatureId = MemberGuid.ToString(EGuidFormats::DigitsWithHyphens);
            if ((SignatureId.IsEmpty() || SignatureId.Equals(TEXT("00000000-0000-0000-0000-000000000000"))) && TargetFunction)
            {
                SignatureId = TargetFunction->GetPathName();
            }
            MemberReference->SetStringField(TEXT("memberKind"), TEXT("function"));
            MemberReference->SetStringField(TEXT("ownerClassPath"), ParentClass ? ParentClass->GetPathName() : TEXT(""));
            MemberReference->SetStringField(TEXT("memberName"), MemberName.ToString());
            MemberReference->SetStringField(TEXT("signatureId"), SignatureId);

            FunctionReference->SetStringField(TEXT("classPath"), ParentClass ? ParentClass->GetPathName() : TEXT(""));
            FunctionReference->SetStringField(TEXT("functionName"), MemberName.ToString());
            FunctionReference->SetStringField(TEXT("signatureId"), SignatureId);
        }
        else if (const UK2Node_VariableGet* VariableGetNode = Cast<UK2Node_VariableGet>(Node))
        {
            FString MemberName;
            UClass* ParentClass = nullptr;
            FString SignatureId;
            ResolveVariableReferenceFromNode(VariableGetNode, MemberName, ParentClass, SignatureId);
            MemberReference->SetStringField(TEXT("memberKind"), TEXT("variable"));
            MemberReference->SetStringField(TEXT("ownerClassPath"), ParentClass ? ParentClass->GetPathName() : TEXT(""));
            MemberReference->SetStringField(TEXT("memberName"), MemberName);
            MemberReference->SetStringField(TEXT("rawMemberName"), VariableGetNode->VariableReference.GetMemberName().ToString());
            MemberReference->SetStringField(TEXT("resolvedOwnerClassPath"), ParentClass ? ParentClass->GetPathName() : TEXT(""));
            MemberReference->SetStringField(TEXT("signatureId"), SignatureId);
        }
        else if (const UK2Node_VariableSet* VariableSetNode = Cast<UK2Node_VariableSet>(Node))
        {
            FString MemberName;
            UClass* ParentClass = nullptr;
            FString SignatureId;
            ResolveVariableReferenceFromNode(VariableSetNode, MemberName, ParentClass, SignatureId);
            MemberReference->SetStringField(TEXT("memberKind"), TEXT("variable"));
            MemberReference->SetStringField(TEXT("ownerClassPath"), ParentClass ? ParentClass->GetPathName() : TEXT(""));
            MemberReference->SetStringField(TEXT("memberName"), MemberName);
            MemberReference->SetStringField(TEXT("rawMemberName"), VariableSetNode->VariableReference.GetMemberName().ToString());
            MemberReference->SetStringField(TEXT("resolvedOwnerClassPath"), ParentClass ? ParentClass->GetPathName() : TEXT(""));
            MemberReference->SetStringField(TEXT("signatureId"), SignatureId);
        }

        NodeObject->SetObjectField(TEXT("memberReference"), MemberReference);
        NodeObject->SetObjectField(TEXT("functionReference"), FunctionReference);

        TSharedPtr<FJsonObject> K2Extensions = MakeShared<FJsonObject>();
        if (const UK2Node_DynamicCast* CastNode = Cast<UK2Node_DynamicCast>(Node))
        {
            TSharedPtr<FJsonObject> CastExt = MakeShared<FJsonObject>();
            CastExt->SetStringField(TEXT("targetClassPath"), CastNode->TargetType ? CastNode->TargetType->GetPathName() : TEXT(""));
            K2Extensions->SetObjectField(TEXT("cast"), CastExt);
        }
        if (const UK2Node_MacroInstance* MacroNode = Cast<UK2Node_MacroInstance>(Node))
        {
            TSharedPtr<FJsonObject> MacroExt = MakeShared<FJsonObject>();
            MacroExt->SetStringField(TEXT("macroGraph"), MacroNode->GetMacroGraph() ? MacroNode->GetMacroGraph()->GetName() : TEXT(""));
            K2Extensions->SetObjectField(TEXT("macro"), MacroExt);
        }
        if (const UEdGraphNode_Comment* CommentNode = Cast<UEdGraphNode_Comment>(Node))
        {
            TSharedPtr<FJsonObject> CommentExt = MakeShared<FJsonObject>();
            CommentExt->SetStringField(TEXT("text"), CommentNode->NodeComment);
            CommentExt->SetNumberField(TEXT("width"), CommentNode->NodeWidth);
            CommentExt->SetNumberField(TEXT("height"), CommentNode->NodeHeight);
            K2Extensions->SetObjectField(TEXT("comment"), CommentExt);
            NodeObject->SetObjectField(
                TEXT("layout"),
                MakeLayoutObject(
                    Node->NodePosX,
                    Node->NodePosY,
                    TEXT("model"),
                    true,
                    FVector2D(CommentNode->NodeWidth, CommentNode->NodeHeight)));
        }
        if (const UK2Node_Timeline* TimelineNode = Cast<UK2Node_Timeline>(Node))
        {
            TSharedPtr<FJsonObject> TimelineExt = MakeShared<FJsonObject>();
            TimelineExt->SetStringField(TEXT("timelineName"), TimelineNode->TimelineName.ToString());
            K2Extensions->SetObjectField(TEXT("timeline"), TimelineExt);
        }
        NodeObject->SetObjectField(TEXT("k2Extensions"), K2Extensions);

        TArray<TSharedPtr<FJsonValue>> Pins;
        for (const UEdGraphPin* Pin : Node->Pins)
        {
            Pins.Add(MakeShared<FJsonValueObject>(SerializePin(Pin)));
        }
        NodeObject->SetArrayField(TEXT("pins"), Pins);

        return NodeObject;
    }

    static FString JsonObjectToString(const TSharedPtr<FJsonObject>& Object)
    {
        FString Output;
        TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Output);
        FJsonSerializer::Serialize(Object.ToSharedRef(), Writer);
        return Output;
    }

    static FString JsonArrayToString(const TArray<TSharedPtr<FJsonValue>>& ArrayValues)
    {
        FString Output;
        TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Output);
        FJsonSerializer::Serialize(ArrayValues, Writer);
        return Output;
    }

    static bool NodeClassMatches(const UEdGraphNode* Node, const FString& Filter)
    {
        if (!Node || Filter.IsEmpty())
        {
            return false;
        }

        const FString NormalizedFilter = NormalizePinToken(Filter);
        const UClass* NodeClass = Node->GetClass();
        if (!NodeClass)
        {
            return false;
        }

        const FString ClassName = NormalizePinToken(NodeClass->GetName());
        const FString ClassPath = NormalizePinToken(NodeClass->GetPathName());
        return ClassName.Equals(NormalizedFilter) || ClassPath.Equals(NormalizedFilter);
    }

    static bool NodeMatchesListOptions(const UEdGraphNode* Node, const FLoomleBlueprintNodeListOptions* Options)
    {
        if (!Node)
        {
            return false;
        }
        if (Options == nullptr)
        {
            return true;
        }

        const FString NodeGuid = Node->NodeGuid.ToString(EGuidFormats::DigitsWithHyphens);
        if (Options->NodeIds.Num() > 0)
        {
            bool bIdMatched = false;
            for (const FString& RequestedId : Options->NodeIds)
            {
                if (RequestedId.Equals(NodeGuid, ESearchCase::IgnoreCase))
                {
                    bIdMatched = true;
                    break;
                }
            }
            if (!bIdMatched)
            {
                return false;
            }
        }

        if (Options->NodeClasses.Num() > 0)
        {
            bool bClassMatched = false;
            for (const FString& NodeClassFilter : Options->NodeClasses)
            {
                if (NodeClassMatches(Node, NodeClassFilter))
                {
                    bClassMatched = true;
                    break;
                }
            }
            if (!bClassMatched)
            {
                return false;
            }
        }

        if (!Options->Text.IsEmpty())
        {
            const FString NodeTitle = Node->GetNodeTitle(ENodeTitleType::ListView).ToString();
            const FString NodeName = Node->GetName();
            const FString NodeClassPath = Node->GetClass() ? Node->GetClass()->GetPathName() : TEXT("");
            if (!NodeTitle.Contains(Options->Text, ESearchCase::IgnoreCase)
                && !NodeName.Contains(Options->Text, ESearchCase::IgnoreCase)
                && !NodeGuid.Contains(Options->Text, ESearchCase::IgnoreCase)
                && !NodeClassPath.Contains(Options->Text, ESearchCase::IgnoreCase))
            {
                return false;
            }
        }

        return true;
    }

    static void SerializeFilteredNodeList(
        const TArray<UEdGraphNode*>& GraphNodes,
        const FString& GraphName,
        const FLoomleBlueprintNodeListOptions* Options,
        FString& OutNodesJson,
        FLoomleBlueprintNodeListStats* OutStats)
    {
        TArray<TSharedPtr<FJsonValue>> Nodes;
        int32 MatchingNodes = 0;
        const int32 Offset = Options ? FMath::Max(Options->Offset, 0) : 0;
        const int32 Limit = Options ? FMath::Max(Options->Limit, 1) : TNumericLimits<int32>::Max();

        Nodes.Reserve(FMath::Min(Limit, GraphNodes.Num()));
        for (const UEdGraphNode* Node : GraphNodes)
        {
            if (!NodeMatchesListOptions(Node, Options))
            {
                continue;
            }

            ++MatchingNodes;
            if (MatchingNodes <= Offset)
            {
                continue;
            }
            if (Nodes.Num() >= Limit)
            {
                continue;
            }

            TSharedPtr<FJsonObject> NodeObject = SerializeNode(Node);
            NodeObject->SetStringField(TEXT("graphName"), GraphName);
            Nodes.Add(MakeShared<FJsonValueObject>(NodeObject));
        }

        if (OutStats)
        {
            OutStats->TotalNodes = GraphNodes.Num();
            OutStats->MatchingNodes = MatchingNodes;
        }
        OutNodesJson = JsonArrayToString(Nodes);
    }

    static const FProperty* ResolveVariableProperty(
        UBlueprint* Blueprint,
        const FString& VariableName,
        const FString& VariableClassPath,
        bool& bOutSelfContext,
        UClass*& OutOwnerClass,
        FString* OutResolvedVariableName = nullptr)
    {
        bOutSelfContext = true;
        OutOwnerClass = nullptr;
        if (OutResolvedVariableName)
        {
            OutResolvedVariableName->Empty();
        }
        if (!Blueprint || VariableName.IsEmpty())
        {
            return nullptr;
        }

        UClass* SearchClass = nullptr;
        if (!VariableClassPath.IsEmpty())
        {
            SearchClass = ResolveClass(VariableClassPath);
            if (!SearchClass)
            {
                return nullptr;
            }
        }
        else
        {
            SearchClass = Blueprint->GeneratedClass ? Blueprint->GeneratedClass : Blueprint->SkeletonGeneratedClass;
        }

        const FProperty* Property = nullptr;
        const FString TargetNormalized = NormalizeIdentifier(VariableName);
        for (UClass* Class = SearchClass; Class != nullptr && Property == nullptr; Class = Class->GetSuperClass())
        {
            Property = FindFProperty<FProperty>(Class, *VariableName);
            if (Property)
            {
                OutOwnerClass = Class;
                if (OutResolvedVariableName)
                {
                    *OutResolvedVariableName = Property->GetName();
                }
                break;
            }

            for (TFieldIterator<FProperty> It(Class, EFieldIteratorFlags::ExcludeSuper); It; ++It)
            {
                const FProperty* Candidate = *It;
                if (!Candidate)
                {
                    continue;
                }

                const FString CandidateName = Candidate->GetName();
                const FString CandidateDisplay = FName::NameToDisplayString(CandidateName, false);
                const FString CandidateDisplayText = Candidate->GetDisplayNameText().ToString();

                if (NormalizeIdentifier(CandidateName).Equals(TargetNormalized)
                    || NormalizeIdentifier(CandidateDisplay).Equals(TargetNormalized)
                    || NormalizeIdentifier(CandidateDisplayText).Equals(TargetNormalized))
                {
                    Property = Candidate;
                    OutOwnerClass = Class;
                    if (OutResolvedVariableName)
                    {
                        *OutResolvedVariableName = CandidateName;
                    }
                    break;
                }
            }
        }
        if (!Property)
        {
            return nullptr;
        }

        UClass* BlueprintClass = Blueprint->GeneratedClass ? Blueprint->GeneratedClass : Blueprint->SkeletonGeneratedClass;
        bOutSelfContext = (OutOwnerClass == nullptr) || (BlueprintClass != nullptr && OutOwnerClass->IsChildOf(BlueprintClass));
        return Property;
    }
}

bool FLoomleBlueprintAdapter::CreateBlueprint(const FString& AssetPath, const FString& ParentClassPath, FString& OutBlueprintObjectPath, FString& OutError)
{
    OutBlueprintObjectPath.Empty();
    OutError.Empty();

    if (!FPackageName::IsValidLongPackageName(AssetPath))
    {
        OutError = TEXT("Invalid AssetPath; expected /Game/... long package name.");
        return false;
    }

    if (UBlueprint* Existing = LoomleBlueprintAdapterInternal::LoadBlueprintByAssetPath(AssetPath))
    {
        const FString AssetName = FPackageName::GetLongPackageAssetName(AssetPath);
        OutBlueprintObjectPath = FString::Printf(TEXT("%s.%s"), *AssetPath, *AssetName);
        Existing->MarkPackageDirty();
        return true;
    }

    UClass* ParentClass = LoomleBlueprintAdapterInternal::ResolveClass(ParentClassPath);
    if (!ParentClass || !ParentClass->IsChildOf(AActor::StaticClass()))
    {
        OutError = FString::Printf(TEXT("Failed to resolve actor parent class: %s"), *ParentClassPath);
        return false;
    }

    const FString AssetName = FPackageName::GetLongPackageAssetName(AssetPath);
    UPackage* Package = CreatePackage(*AssetPath);
    if (!Package)
    {
        OutError = FString::Printf(TEXT("Failed to create package: %s"), *AssetPath);
        return false;
    }

    UBlueprint* Blueprint = FKismetEditorUtilities::CreateBlueprint(
        ParentClass,
        Package,
        *AssetName,
        BPTYPE_Normal,
        UBlueprint::StaticClass(),
        UBlueprintGeneratedClass::StaticClass(),
        NAME_None);

    if (!Blueprint)
    {
        OutError = TEXT("FKismetEditorUtilities::CreateBlueprint failed.");
        return false;
    }

    FAssetRegistryModule::AssetCreated(Blueprint);
    Blueprint->MarkPackageDirty();
    OutBlueprintObjectPath = FString::Printf(TEXT("%s.%s"), *AssetPath, *AssetName);
    return true;
}

bool FLoomleBlueprintAdapter::AddComponent(const FString& BlueprintAssetPath, const FString& ComponentClassPath, const FString& ComponentName, const FString& ParentComponentName, FString& OutError)
{
    OutError.Empty();
    UBlueprint* Blueprint = LoomleBlueprintAdapterInternal::LoadBlueprintByAssetPath(BlueprintAssetPath);
    if (!Blueprint || !Blueprint->SimpleConstructionScript)
    {
        OutError = FString::Printf(TEXT("Blueprint not found or no SCS: %s"), *BlueprintAssetPath);
        return false;
    }

    UClass* ComponentClass = LoomleBlueprintAdapterInternal::ResolveClass(ComponentClassPath);
    if (!ComponentClass || !ComponentClass->IsChildOf(UActorComponent::StaticClass()))
    {
        OutError = FString::Printf(TEXT("Failed to resolve component class: %s"), *ComponentClassPath);
        return false;
    }

    USimpleConstructionScript* SCS = Blueprint->SimpleConstructionScript;
    USCS_Node* NewNode = SCS->CreateNode(ComponentClass, *ComponentName);
    if (!NewNode)
    {
        OutError = TEXT("Failed to create SCS node.");
        return false;
    }

    USCS_Node* ParentNode = nullptr;
    if (!ParentComponentName.IsEmpty())
    {
        for (USCS_Node* Node : SCS->GetAllNodes())
        {
            if (Node && Node->GetVariableName().ToString().Equals(ParentComponentName))
            {
                ParentNode = Node;
                break;
            }
        }
    }

    if (ParentNode)
    {
        ParentNode->AddChildNode(NewNode);
    }
    else
    {
        SCS->AddNode(NewNode);
    }

    FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
    Blueprint->MarkPackageDirty();
    return true;
}

bool FLoomleBlueprintAdapter::SetStaticMeshComponentAsset(const FString& BlueprintAssetPath, const FString& ComponentName, const FString& MeshAssetPath, FString& OutError)
{
    OutError.Empty();
    UBlueprint* Blueprint = LoomleBlueprintAdapterInternal::LoadBlueprintByAssetPath(BlueprintAssetPath);
    USCS_Node* Node = LoomleBlueprintAdapterInternal::FindComponentNode(Blueprint, ComponentName);
    UStaticMeshComponent* MeshComp = Node ? Cast<UStaticMeshComponent>(Node->ComponentTemplate) : nullptr;
    UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, *MeshAssetPath);
    if (!Blueprint || !MeshComp || !Mesh)
    {
        OutError = TEXT("Failed to resolve blueprint/static mesh component/mesh asset.");
        return false;
    }

    MeshComp->SetStaticMesh(Mesh);
    Blueprint->MarkPackageDirty();
    return true;
}

bool FLoomleBlueprintAdapter::SetSceneComponentRelativeLocation(const FString& BlueprintAssetPath, const FString& ComponentName, FVector Location, FString& OutError)
{
    OutError.Empty();
    UBlueprint* Blueprint = LoomleBlueprintAdapterInternal::LoadBlueprintByAssetPath(BlueprintAssetPath);
    USCS_Node* Node = LoomleBlueprintAdapterInternal::FindComponentNode(Blueprint, ComponentName);
    USceneComponent* Comp = Node ? Cast<USceneComponent>(Node->ComponentTemplate) : nullptr;
    if (!Blueprint || !Comp)
    {
        OutError = TEXT("Failed to resolve blueprint/scene component.");
        return false;
    }

    Comp->SetRelativeLocation(Location);
    Blueprint->MarkPackageDirty();
    return true;
}

bool FLoomleBlueprintAdapter::SetSceneComponentRelativeScale3D(const FString& BlueprintAssetPath, const FString& ComponentName, FVector Scale3D, FString& OutError)
{
    OutError.Empty();
    UBlueprint* Blueprint = LoomleBlueprintAdapterInternal::LoadBlueprintByAssetPath(BlueprintAssetPath);
    USCS_Node* Node = LoomleBlueprintAdapterInternal::FindComponentNode(Blueprint, ComponentName);
    USceneComponent* Comp = Node ? Cast<USceneComponent>(Node->ComponentTemplate) : nullptr;
    if (!Blueprint || !Comp)
    {
        OutError = TEXT("Failed to resolve blueprint/scene component.");
        return false;
    }

    Comp->SetRelativeScale3D(Scale3D);
    Blueprint->MarkPackageDirty();
    return true;
}

bool FLoomleBlueprintAdapter::SetPrimitiveComponentCollisionEnabled(const FString& BlueprintAssetPath, const FString& ComponentName, const FString& CollisionMode, FString& OutError)
{
    OutError.Empty();
    UBlueprint* Blueprint = LoomleBlueprintAdapterInternal::LoadBlueprintByAssetPath(BlueprintAssetPath);
    USCS_Node* Node = LoomleBlueprintAdapterInternal::FindComponentNode(Blueprint, ComponentName);
    UPrimitiveComponent* Comp = Node ? Cast<UPrimitiveComponent>(Node->ComponentTemplate) : nullptr;
    if (!Blueprint || !Comp)
    {
        OutError = TEXT("Failed to resolve blueprint/primitive component.");
        return false;
    }

    const FString Key = CollisionMode.ToLower();
    if (Key.Equals(TEXT("nocollision")))
    {
        Comp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    }
    else if (Key.Equals(TEXT("queryonly")))
    {
        Comp->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
    }
    else if (Key.Equals(TEXT("queryandphysics")))
    {
        Comp->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
    }
    else
    {
        OutError = FString::Printf(TEXT("Unknown collision mode: %s"), *CollisionMode);
        return false;
    }

    Blueprint->MarkPackageDirty();
    return true;
}

bool FLoomleBlueprintAdapter::SetBoxComponentExtent(const FString& BlueprintAssetPath, const FString& ComponentName, FVector Extent, FString& OutError)
{
    OutError.Empty();
    UBlueprint* Blueprint = LoomleBlueprintAdapterInternal::LoadBlueprintByAssetPath(BlueprintAssetPath);
    USCS_Node* Node = LoomleBlueprintAdapterInternal::FindComponentNode(Blueprint, ComponentName);
    UBoxComponent* Comp = Node ? Cast<UBoxComponent>(Node->ComponentTemplate) : nullptr;
    if (!Blueprint || !Comp)
    {
        OutError = TEXT("Failed to resolve blueprint/box component.");
        return false;
    }

    Comp->SetBoxExtent(Extent);
    Blueprint->MarkPackageDirty();
    return true;
}

bool FLoomleBlueprintAdapter::SetPrimitiveComponentGenerateOverlapEvents(const FString& BlueprintAssetPath, const FString& ComponentName, bool bGenerate, FString& OutError)
{
    OutError.Empty();
    UBlueprint* Blueprint = LoomleBlueprintAdapterInternal::LoadBlueprintByAssetPath(BlueprintAssetPath);
    USCS_Node* Node = LoomleBlueprintAdapterInternal::FindComponentNode(Blueprint, ComponentName);
    UPrimitiveComponent* Comp = Node ? Cast<UPrimitiveComponent>(Node->ComponentTemplate) : nullptr;
    if (!Blueprint || !Comp)
    {
        OutError = TEXT("Failed to resolve blueprint/primitive component.");
        return false;
    }

    Comp->SetGenerateOverlapEvents(bGenerate);
    Blueprint->MarkPackageDirty();
    return true;
}

bool FLoomleBlueprintAdapter::AddEventNode(const FString& BlueprintAssetPath, const FString& GraphName, const FString& EventName, const FString& EventClassPath, int32 NodePosX, int32 NodePosY, FString& OutNodeGuid, FString& OutError)
{
    OutNodeGuid.Empty();
    OutError.Empty();

    UBlueprint* Blueprint = LoomleBlueprintAdapterInternal::LoadBlueprintByAssetPath(BlueprintAssetPath);
    UEdGraph* EventGraph = LoomleBlueprintAdapterInternal::ResolveTargetGraph(Blueprint, GraphName);
    UClass* EventClass = LoomleBlueprintAdapterInternal::ResolveClass(EventClassPath);
    if (!Blueprint || !EventGraph || !EventClass)
    {
        OutError = TEXT("Failed to resolve blueprint/target graph/event class.");
        return false;
    }

    int32 PosY = NodePosY;
    UK2Node_Event* Node = FKismetEditorUtilities::AddDefaultEventNode(Blueprint, EventGraph, *EventName, EventClass, PosY);
    if (!Node)
    {
        OutError = FString::Printf(TEXT("Failed to add event node: %s"), *EventName);
        return false;
    }

    Node->NodePosX = NodePosX;
    Node->NodePosY = NodePosY;
    OutNodeGuid = Node->NodeGuid.ToString(EGuidFormats::DigitsWithHyphens);
    return true;
}

bool FLoomleBlueprintAdapter::AddCastNode(const FString& BlueprintAssetPath, const FString& GraphName, const FString& TargetClassPath, int32 NodePosX, int32 NodePosY, FString& OutNodeGuid, FString& OutError)
{
    OutNodeGuid.Empty();
    OutError.Empty();

    UBlueprint* Blueprint = LoomleBlueprintAdapterInternal::LoadBlueprintByAssetPath(BlueprintAssetPath);
    UEdGraph* EventGraph = LoomleBlueprintAdapterInternal::ResolveTargetGraph(Blueprint, GraphName);
    UClass* TargetClass = LoomleBlueprintAdapterInternal::ResolveClass(TargetClassPath);
    if (!Blueprint || !EventGraph || !TargetClass)
    {
        OutError = TEXT("Failed to resolve blueprint/target graph/target class.");
        return false;
    }

    FGraphNodeCreator<UK2Node_DynamicCast> Creator(*EventGraph);
    UK2Node_DynamicCast* Node = Creator.CreateNode();
    Node->TargetType = TargetClass;
    Node->NodePosX = NodePosX;
    Node->NodePosY = NodePosY;
    Node->AllocateDefaultPins();
    Node->SetPurity(false);
    Creator.Finalize();

    OutNodeGuid = Node->NodeGuid.ToString(EGuidFormats::DigitsWithHyphens);
    return true;
}

bool FLoomleBlueprintAdapter::AddCallFunctionNode(const FString& BlueprintAssetPath, const FString& GraphName, const FString& FunctionClassPath, const FString& FunctionName, int32 NodePosX, int32 NodePosY, FString& OutNodeGuid, FString& OutError)
{
    OutNodeGuid.Empty();
    OutError.Empty();

    UBlueprint* Blueprint = LoomleBlueprintAdapterInternal::LoadBlueprintByAssetPath(BlueprintAssetPath);
    UEdGraph* EventGraph = LoomleBlueprintAdapterInternal::ResolveTargetGraph(Blueprint, GraphName);
    UClass* FunctionClass = LoomleBlueprintAdapterInternal::ResolveClass(FunctionClassPath);
    UFunction* Function = FunctionClass ? FunctionClass->FindFunctionByName(*FunctionName) : nullptr;
    if (!Blueprint || !EventGraph || !Function)
    {
        OutError = TEXT("Failed to resolve blueprint/target graph/function.");
        return false;
    }

    FGraphNodeCreator<UK2Node_CallFunction> Creator(*EventGraph);
    UK2Node_CallFunction* Node = Creator.CreateNode();
    Node->NodePosX = NodePosX;
    Node->NodePosY = NodePosY;
    Node->SetFromFunction(Function);
    Node->AllocateDefaultPins();
    Creator.Finalize();

    OutNodeGuid = Node->NodeGuid.ToString(EGuidFormats::DigitsWithHyphens);
    return true;
}

bool FLoomleBlueprintAdapter::AddBranchNode(const FString& BlueprintAssetPath, const FString& GraphName, int32 NodePosX, int32 NodePosY, FString& OutNodeGuid, FString& OutError)
{
    OutNodeGuid.Empty();
    OutError.Empty();

    UBlueprint* Blueprint = LoomleBlueprintAdapterInternal::LoadBlueprintByAssetPath(BlueprintAssetPath);
    UEdGraph* EventGraph = LoomleBlueprintAdapterInternal::ResolveTargetGraph(Blueprint, GraphName);
    if (!Blueprint || !EventGraph)
    {
        OutError = TEXT("Failed to resolve blueprint/target graph.");
        return false;
    }

    FGraphNodeCreator<UK2Node_IfThenElse> Creator(*EventGraph);
    UK2Node_IfThenElse* Node = Creator.CreateNode();
    Node->NodePosX = NodePosX;
    Node->NodePosY = NodePosY;
    Node->AllocateDefaultPins();
    Creator.Finalize();

    OutNodeGuid = Node->NodeGuid.ToString(EGuidFormats::DigitsWithHyphens);
    return true;
}

bool FLoomleBlueprintAdapter::AddExecutionSequenceNode(const FString& BlueprintAssetPath, const FString& GraphName, int32 NodePosX, int32 NodePosY, FString& OutNodeGuid, FString& OutError)
{
    OutNodeGuid.Empty();
    OutError.Empty();

    UBlueprint* Blueprint = LoomleBlueprintAdapterInternal::LoadBlueprintByAssetPath(BlueprintAssetPath);
    UEdGraph* EventGraph = LoomleBlueprintAdapterInternal::ResolveTargetGraph(Blueprint, GraphName);
    if (!Blueprint || !EventGraph)
    {
        OutError = TEXT("Failed to resolve blueprint/target graph.");
        return false;
    }

    FGraphNodeCreator<UK2Node_ExecutionSequence> Creator(*EventGraph);
    UK2Node_ExecutionSequence* Node = Creator.CreateNode();
    Node->NodePosX = NodePosX;
    Node->NodePosY = NodePosY;
    Node->AllocateDefaultPins();
    Creator.Finalize();

    OutNodeGuid = Node->NodeGuid.ToString(EGuidFormats::DigitsWithHyphens);
    return true;
}

bool FLoomleBlueprintAdapter::AddMacroNode(const FString& BlueprintAssetPath, const FString& GraphName, const FString& MacroLibraryAssetPath, const FString& MacroGraphName, int32 NodePosX, int32 NodePosY, FString& OutNodeGuid, FString& OutError)
{
    OutNodeGuid.Empty();
    OutError.Empty();

    UBlueprint* Blueprint = LoomleBlueprintAdapterInternal::LoadBlueprintByAssetPath(BlueprintAssetPath);
    UEdGraph* EventGraph = LoomleBlueprintAdapterInternal::ResolveTargetGraph(Blueprint, GraphName);
    UEdGraph* MacroGraph = LoomleBlueprintAdapterInternal::ResolveMacroGraph(MacroLibraryAssetPath, MacroGraphName);
    if (!Blueprint || !EventGraph || !MacroGraph)
    {
        OutError = TEXT("Failed to resolve blueprint/target graph/macro graph.");
        return false;
    }

    FGraphNodeCreator<UK2Node_MacroInstance> Creator(*EventGraph);
    UK2Node_MacroInstance* Node = Creator.CreateNode();
    Node->NodePosX = NodePosX;
    Node->NodePosY = NodePosY;
    Node->SetMacroGraph(MacroGraph);
    Node->AllocateDefaultPins();
    Creator.Finalize();

    OutNodeGuid = Node->NodeGuid.ToString(EGuidFormats::DigitsWithHyphens);
    return true;
}

bool FLoomleBlueprintAdapter::AddCommentNode(const FString& BlueprintAssetPath, const FString& GraphName, const FString& CommentText, int32 NodePosX, int32 NodePosY, int32 Width, int32 Height, FString& OutNodeGuid, FString& OutError)
{
    OutNodeGuid.Empty();
    OutError.Empty();

    UBlueprint* Blueprint = LoomleBlueprintAdapterInternal::LoadBlueprintByAssetPath(BlueprintAssetPath);
    UEdGraph* EventGraph = LoomleBlueprintAdapterInternal::ResolveTargetGraph(Blueprint, GraphName);
    if (!Blueprint || !EventGraph)
    {
        OutError = TEXT("Failed to resolve blueprint/target graph.");
        return false;
    }

    FGraphNodeCreator<UEdGraphNode_Comment> Creator(*EventGraph);
    UEdGraphNode_Comment* Node = Creator.CreateNode();
    Node->NodePosX = NodePosX;
    Node->NodePosY = NodePosY;
    Node->NodeComment = CommentText;
    Node->NodeWidth = Width > 0 ? Width : Node->NodeWidth;
    Node->NodeHeight = Height > 0 ? Height : Node->NodeHeight;
    Creator.Finalize();

    OutNodeGuid = Node->NodeGuid.ToString(EGuidFormats::DigitsWithHyphens);
    return true;
}

bool FLoomleBlueprintAdapter::AddKnotNode(const FString& BlueprintAssetPath, const FString& GraphName, int32 NodePosX, int32 NodePosY, FString& OutNodeGuid, FString& OutError)
{
    OutNodeGuid.Empty();
    OutError.Empty();

    UBlueprint* Blueprint = LoomleBlueprintAdapterInternal::LoadBlueprintByAssetPath(BlueprintAssetPath);
    UEdGraph* EventGraph = LoomleBlueprintAdapterInternal::ResolveTargetGraph(Blueprint, GraphName);
    if (!Blueprint || !EventGraph)
    {
        OutError = TEXT("Failed to resolve blueprint/target graph.");
        return false;
    }

    FGraphNodeCreator<UK2Node_Knot> Creator(*EventGraph);
    UK2Node_Knot* Node = Creator.CreateNode();
    Node->NodePosX = NodePosX;
    Node->NodePosY = NodePosY;
    Node->AllocateDefaultPins();
    Creator.Finalize();

    OutNodeGuid = Node->NodeGuid.ToString(EGuidFormats::DigitsWithHyphens);
    return true;
}

bool FLoomleBlueprintAdapter::AddVariableGetNode(const FString& BlueprintAssetPath, const FString& GraphName, const FString& VariableName, const FString& VariableClassPath, int32 NodePosX, int32 NodePosY, FString& OutNodeGuid, FString& OutError)
{
    OutNodeGuid.Empty();
    OutError.Empty();

    UBlueprint* Blueprint = LoomleBlueprintAdapterInternal::LoadBlueprintByAssetPath(BlueprintAssetPath);
    UEdGraph* EventGraph = LoomleBlueprintAdapterInternal::ResolveTargetGraph(Blueprint, GraphName);
    if (!Blueprint || !EventGraph)
    {
        OutError = TEXT("Failed to resolve blueprint/target graph.");
        return false;
    }

    bool bSelfContext = true;
    UClass* OwnerClass = nullptr;
    const FProperty* Property = LoomleBlueprintAdapterInternal::ResolveVariableProperty(
        Blueprint, VariableName, VariableClassPath, bSelfContext, OwnerClass, nullptr);
    if (!Property)
    {
        OutError = FString::Printf(TEXT("Failed to resolve variable property: %s"), *VariableName);
        return false;
    }

    FGraphNodeCreator<UK2Node_VariableGet> Creator(*EventGraph);
    UK2Node_VariableGet* Node = Creator.CreateNode();
    Node->NodePosX = NodePosX;
    Node->NodePosY = NodePosY;
    Node->SetFromProperty(Property, bSelfContext, OwnerClass);
    Node->AllocateDefaultPins();
    Creator.Finalize();

    OutNodeGuid = Node->NodeGuid.ToString(EGuidFormats::DigitsWithHyphens);
    return true;
}

bool FLoomleBlueprintAdapter::AddVariableSetNode(const FString& BlueprintAssetPath, const FString& GraphName, const FString& VariableName, const FString& VariableClassPath, int32 NodePosX, int32 NodePosY, FString& OutNodeGuid, FString& OutError)
{
    OutNodeGuid.Empty();
    OutError.Empty();

    UBlueprint* Blueprint = LoomleBlueprintAdapterInternal::LoadBlueprintByAssetPath(BlueprintAssetPath);
    UEdGraph* EventGraph = LoomleBlueprintAdapterInternal::ResolveTargetGraph(Blueprint, GraphName);
    if (!Blueprint || !EventGraph)
    {
        OutError = TEXT("Failed to resolve blueprint/target graph.");
        return false;
    }

    bool bSelfContext = true;
    UClass* OwnerClass = nullptr;
    const FProperty* Property = LoomleBlueprintAdapterInternal::ResolveVariableProperty(
        Blueprint, VariableName, VariableClassPath, bSelfContext, OwnerClass, nullptr);
    if (!Property)
    {
        OutError = FString::Printf(TEXT("Failed to resolve variable property: %s"), *VariableName);
        return false;
    }

    FGraphNodeCreator<UK2Node_VariableSet> Creator(*EventGraph);
    UK2Node_VariableSet* Node = Creator.CreateNode();
    Node->NodePosX = NodePosX;
    Node->NodePosY = NodePosY;
    Node->SetFromProperty(Property, bSelfContext, OwnerClass);
    Node->AllocateDefaultPins();
    Creator.Finalize();

    OutNodeGuid = Node->NodeGuid.ToString(EGuidFormats::DigitsWithHyphens);
    return true;
}

bool FLoomleBlueprintAdapter::AddNodeByClass(const FString& BlueprintAssetPath, const FString& GraphName, const FString& NodeClassPath, const FString& PayloadJson, int32 NodePosX, int32 NodePosY, FString& OutNodeGuid, FString& OutError)
{
    OutNodeGuid.Empty();
    OutError.Empty();

    TSharedPtr<FJsonObject> Payload;
    if (!LoomleBlueprintAdapterInternal::ParsePayloadJson(PayloadJson, Payload))
    {
        OutError = TEXT("Invalid payload JSON.");
        return false;
    }

    const FString NormalizedClass = NodeClassPath.ToLower();
    if (NormalizedClass.Contains(TEXT("k2node_event")))
    {
        FString EventName;
        FString EventClassPath;
        Payload->TryGetStringField(TEXT("eventName"), EventName);
        Payload->TryGetStringField(TEXT("eventClassPath"), EventClassPath);
        return AddEventNode(BlueprintAssetPath, GraphName, EventName, EventClassPath, NodePosX, NodePosY, OutNodeGuid, OutError);
    }
    if (NormalizedClass.Contains(TEXT("k2node_dynamiccast")))
    {
        FString TargetClassPath;
        Payload->TryGetStringField(TEXT("targetClassPath"), TargetClassPath);
        return AddCastNode(BlueprintAssetPath, GraphName, TargetClassPath, NodePosX, NodePosY, OutNodeGuid, OutError);
    }
    if (NormalizedClass.Contains(TEXT("k2node_callfunction")))
    {
        FString FunctionClassPath;
        FString FunctionName;
        Payload->TryGetStringField(TEXT("functionClassPath"), FunctionClassPath);
        Payload->TryGetStringField(TEXT("functionName"), FunctionName);
        return AddCallFunctionNode(BlueprintAssetPath, GraphName, FunctionClassPath, FunctionName, NodePosX, NodePosY, OutNodeGuid, OutError);
    }
    if (NormalizedClass.Contains(TEXT("k2node_executionsequence")))
    {
        return AddExecutionSequenceNode(BlueprintAssetPath, GraphName, NodePosX, NodePosY, OutNodeGuid, OutError);
    }
    if (NormalizedClass.Contains(TEXT("k2node_ifthenelse")))
    {
        return AddBranchNode(BlueprintAssetPath, GraphName, NodePosX, NodePosY, OutNodeGuid, OutError);
    }
    if (NormalizedClass.Contains(TEXT("k2node_macroinstance")))
    {
        FString MacroLibraryAssetPath;
        FString MacroGraphName;
        Payload->TryGetStringField(TEXT("macroLibraryAssetPath"), MacroLibraryAssetPath);
        Payload->TryGetStringField(TEXT("macroGraphName"), MacroGraphName);
        return AddMacroNode(BlueprintAssetPath, GraphName, MacroLibraryAssetPath, MacroGraphName, NodePosX, NodePosY, OutNodeGuid, OutError);
    }
    if (NormalizedClass.Contains(TEXT("k2node_variableget")))
    {
        FString VariableName;
        FString VariableClassPath;
        Payload->TryGetStringField(TEXT("variableName"), VariableName);
        Payload->TryGetStringField(TEXT("variableClassPath"), VariableClassPath);
        return AddVariableGetNode(BlueprintAssetPath, GraphName, VariableName, VariableClassPath, NodePosX, NodePosY, OutNodeGuid, OutError);
    }
    if (NormalizedClass.Contains(TEXT("k2node_variableset")))
    {
        FString VariableName;
        FString VariableClassPath;
        Payload->TryGetStringField(TEXT("variableName"), VariableName);
        Payload->TryGetStringField(TEXT("variableClassPath"), VariableClassPath);
        return AddVariableSetNode(BlueprintAssetPath, GraphName, VariableName, VariableClassPath, NodePosX, NodePosY, OutNodeGuid, OutError);
    }
    if (NormalizedClass.Contains(TEXT("edgraphnode_comment")))
    {
        FString Text;
        double WidthNumber = 0.0;
        double HeightNumber = 0.0;
        Payload->TryGetStringField(TEXT("text"), Text);
        Payload->TryGetNumberField(TEXT("width"), WidthNumber);
        Payload->TryGetNumberField(TEXT("height"), HeightNumber);
        return AddCommentNode(BlueprintAssetPath, GraphName, Text, NodePosX, NodePosY, static_cast<int32>(WidthNumber), static_cast<int32>(HeightNumber), OutNodeGuid, OutError);
    }
    if (NormalizedClass.Contains(TEXT("k2node_knot")))
    {
        return AddKnotNode(BlueprintAssetPath, GraphName, NodePosX, NodePosY, OutNodeGuid, OutError);
    }

    OutError = FString::Printf(TEXT("Unsupported nodeClassPath for addNode.byClass: %s"), *NodeClassPath);
    return false;
}

bool FLoomleBlueprintAdapter::ConnectPins(const FString& BlueprintAssetPath, const FString& GraphName, const FString& FromNodeGuid, const FString& FromPinName, const FString& ToNodeGuid, const FString& ToPinName, FString& OutError)
{
    OutError.Empty();

    UBlueprint* Blueprint = LoomleBlueprintAdapterInternal::LoadBlueprintByAssetPath(BlueprintAssetPath);
    UEdGraph* EventGraph = LoomleBlueprintAdapterInternal::ResolveTargetGraph(Blueprint, GraphName);
    if (!Blueprint || !EventGraph)
    {
        OutError = TEXT("Failed to resolve blueprint/target graph.");
        return false;
    }

    UEdGraphNode* FromNode = LoomleBlueprintAdapterInternal::FindNodeByGuid(EventGraph, FromNodeGuid);
    UEdGraphNode* ToNode = LoomleBlueprintAdapterInternal::FindNodeByGuid(EventGraph, ToNodeGuid);
    UEdGraphPin* FromPin = LoomleBlueprintAdapterInternal::ResolvePin(FromNode, FromPinName);
    UEdGraphPin* ToPin = LoomleBlueprintAdapterInternal::ResolvePin(ToNode, ToPinName);
    if (!FromNode || !ToNode || !FromPin || !ToPin)
    {
        OutError = TEXT("Failed to resolve nodes or pins.");
        return false;
    }

    const UEdGraphSchema_K2* Schema = GetDefault<UEdGraphSchema_K2>();
    if (!Schema || !Schema->TryCreateConnection(FromPin, ToPin))
    {
        OutError = TEXT("TryCreateConnection failed.");
        return false;
    }

    return true;
}

bool FLoomleBlueprintAdapter::DisconnectPins(const FString& BlueprintAssetPath, const FString& GraphName, const FString& FromNodeGuid, const FString& FromPinName, const FString& ToNodeGuid, const FString& ToPinName, FString& OutError)
{
    OutError.Empty();

    UBlueprint* Blueprint = LoomleBlueprintAdapterInternal::LoadBlueprintByAssetPath(BlueprintAssetPath);
    UEdGraph* EventGraph = LoomleBlueprintAdapterInternal::ResolveTargetGraph(Blueprint, GraphName);
    if (!Blueprint || !EventGraph)
    {
        OutError = TEXT("Failed to resolve blueprint/target graph.");
        return false;
    }

    UEdGraphNode* FromNode = LoomleBlueprintAdapterInternal::FindNodeByGuid(EventGraph, FromNodeGuid);
    UEdGraphNode* ToNode = LoomleBlueprintAdapterInternal::FindNodeByGuid(EventGraph, ToNodeGuid);
    UEdGraphPin* FromPin = LoomleBlueprintAdapterInternal::ResolvePin(FromNode, FromPinName);
    UEdGraphPin* ToPin = LoomleBlueprintAdapterInternal::ResolvePin(ToNode, ToPinName);
    if (!FromNode || !ToNode || !FromPin || !ToPin)
    {
        OutError = TEXT("Failed to resolve nodes or pins.");
        return false;
    }

    if (!(FromPin->LinkedTo.Contains(ToPin) || ToPin->LinkedTo.Contains(FromPin)))
    {
        OutError = TEXT("Specified pin link does not exist.");
        return false;
    }

    FromPin->BreakLinkTo(ToPin);
    return true;
}

bool FLoomleBlueprintAdapter::BreakPinLinks(const FString& BlueprintAssetPath, const FString& GraphName, const FString& NodeGuid, const FString& PinName, FString& OutError)
{
    OutError.Empty();

    UBlueprint* Blueprint = LoomleBlueprintAdapterInternal::LoadBlueprintByAssetPath(BlueprintAssetPath);
    UEdGraph* EventGraph = LoomleBlueprintAdapterInternal::ResolveTargetGraph(Blueprint, GraphName);
    if (!Blueprint || !EventGraph)
    {
        OutError = TEXT("Failed to resolve blueprint/target graph.");
        return false;
    }

    UEdGraphNode* Node = LoomleBlueprintAdapterInternal::FindNodeByGuid(EventGraph, NodeGuid);
    UEdGraphPin* Pin = LoomleBlueprintAdapterInternal::ResolvePin(Node, PinName);
    if (!Node || !Pin)
    {
        OutError = TEXT("Failed to resolve node or pin.");
        return false;
    }

    Pin->BreakAllPinLinks(true);
    return true;
}

bool FLoomleBlueprintAdapter::RemoveNode(const FString& BlueprintAssetPath, const FString& GraphName, const FString& NodeGuid, FString& OutError)
{
    OutError.Empty();

    UBlueprint* Blueprint = LoomleBlueprintAdapterInternal::LoadBlueprintByAssetPath(BlueprintAssetPath);
    UEdGraph* EventGraph = LoomleBlueprintAdapterInternal::ResolveTargetGraph(Blueprint, GraphName);
    if (!Blueprint || !EventGraph)
    {
        OutError = TEXT("Failed to resolve blueprint/target graph.");
        return false;
    }

    UEdGraphNode* Node = LoomleBlueprintAdapterInternal::ResolveNodeByToken(EventGraph, NodeGuid);
    if (!Node)
    {
        OutError = FString::Printf(TEXT("Node not found in graph by id/path/name: %s"), *NodeGuid);
        return false;
    }

    Blueprint->Modify();
    EventGraph->Modify();
    Node->DestroyNode();

    if (LoomleBlueprintAdapterInternal::ResolveNodeByToken(EventGraph, NodeGuid) != nullptr)
    {
        UEdGraphNode* FallbackNode = LoomleBlueprintAdapterInternal::ResolveNodeByToken(EventGraph, NodeGuid);
        if (FallbackNode)
        {
            FallbackNode->Modify();
            FallbackNode->BreakAllNodeLinks();
            EventGraph->RemoveNode(FallbackNode);
        }
    }

    if (LoomleBlueprintAdapterInternal::ResolveNodeByToken(EventGraph, NodeGuid) != nullptr)
    {
        OutError = FString::Printf(TEXT("Node remained in graph after removal attempt: %s"), *NodeGuid);
        return false;
    }

    FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
    Blueprint->MarkPackageDirty();
    return true;
}

bool FLoomleBlueprintAdapter::MoveNode(const FString& BlueprintAssetPath, const FString& GraphName, const FString& NodeGuid, int32 NodePosX, int32 NodePosY, FString& OutError)
{
    OutError.Empty();

    UBlueprint* Blueprint = LoomleBlueprintAdapterInternal::LoadBlueprintByAssetPath(BlueprintAssetPath);
    UEdGraph* EventGraph = LoomleBlueprintAdapterInternal::ResolveTargetGraph(Blueprint, GraphName);
    if (!Blueprint || !EventGraph)
    {
        OutError = TEXT("Failed to resolve blueprint/target graph.");
        return false;
    }

    UEdGraphNode* Node = LoomleBlueprintAdapterInternal::FindNodeByGuid(EventGraph, NodeGuid);
    if (!Node)
    {
        OutError = FString::Printf(TEXT("Node not found by guid: %s"), *NodeGuid);
        return false;
    }

    EventGraph->Modify();
    Node->Modify();
    const int32 OriginalX = Node->NodePosX;
    const int32 OriginalY = Node->NodePosY;
    Node->NodePosX = NodePosX;
    Node->NodePosY = NodePosY;
    Node->SnapToGrid(16);
    EventGraph->NotifyGraphChanged();
    FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
    Blueprint->MarkPackageDirty();

    if ((OriginalX != NodePosX || OriginalY != NodePosY)
        && Node->NodePosX == OriginalX
        && Node->NodePosY == OriginalY)
    {
        OutError = FString::Printf(
            TEXT("Node move verification failed. Requested (%d, %d), observed no movement from (%d, %d)."),
            NodePosX,
            NodePosY,
            OriginalX,
            OriginalY);
        return false;
    }

    return true;
}

bool FLoomleBlueprintAdapter::SetPinDefaultValue(const FString& BlueprintAssetPath, const FString& GraphName, const FString& NodeGuid, const FString& PinName, const FString& Value, FString& OutError)
{
    OutError.Empty();

    UBlueprint* Blueprint = LoomleBlueprintAdapterInternal::LoadBlueprintByAssetPath(BlueprintAssetPath);
    UEdGraph* EventGraph = LoomleBlueprintAdapterInternal::ResolveTargetGraph(Blueprint, GraphName);
    if (!Blueprint || !EventGraph)
    {
        OutError = TEXT("Failed to resolve blueprint/target graph.");
        return false;
    }

    UEdGraphNode* Node = LoomleBlueprintAdapterInternal::ResolveNodeByToken(EventGraph, NodeGuid);
    UEdGraphPin* Pin = LoomleBlueprintAdapterInternal::ResolvePin(Node, PinName);
    if (!Node)
    {
        OutError = FString::Printf(TEXT("Node not found in graph by id/path/name: %s"), *NodeGuid);
        return false;
    }
    if (!Pin)
    {
        OutError = FString::Printf(TEXT("Pin not found on node %s: %s"), *Node->GetName(), *PinName);
        return false;
    }

    Pin->DefaultValue = Value;
    return true;
}

bool FLoomleBlueprintAdapter::DescribePinTarget(
    const FString& BlueprintAssetPath,
    const FString& GraphName,
    const FString& NodeToken,
    const FString& PinName,
    FString& OutDetailsJson,
    FString& OutError)
{
    OutDetailsJson = TEXT("{}");
    OutError.Empty();

    UBlueprint* Blueprint = LoomleBlueprintAdapterInternal::LoadBlueprintByAssetPath(BlueprintAssetPath);
    UEdGraph* EventGraph = LoomleBlueprintAdapterInternal::ResolveTargetGraph(Blueprint, GraphName);
    if (!Blueprint || !EventGraph)
    {
        OutError = TEXT("Failed to resolve blueprint/target graph.");
        return false;
    }

    TSharedPtr<FJsonObject> Details = MakeShared<FJsonObject>();
    Details->SetStringField(TEXT("assetPath"), BlueprintAssetPath);
    Details->SetStringField(TEXT("graphName"), GraphName);

    TSharedPtr<FJsonObject> RequestedTarget = MakeShared<FJsonObject>();
    RequestedTarget->SetStringField(TEXT("nodeToken"), NodeToken);
    RequestedTarget->SetStringField(TEXT("pinName"), PinName);
    Details->SetObjectField(TEXT("requestedTarget"), RequestedTarget);

    TArray<TSharedPtr<FJsonValue>> ExpectedTargetForms;
    auto AddExpectedTargetForm = [&ExpectedTargetForms](const FString& NodeField, const FString& PinField)
    {
        TSharedPtr<FJsonObject> Form = MakeShared<FJsonObject>();
        TSharedPtr<FJsonObject> Target = MakeShared<FJsonObject>();
        Target->SetStringField(NodeField, FString::Printf(TEXT("<%s>"), *NodeField));
        Target->SetStringField(PinField, FString::Printf(TEXT("<%s>"), *PinField));
        Form->SetObjectField(TEXT("target"), Target);
        ExpectedTargetForms.Add(MakeShared<FJsonValueObject>(Form));
    };
    AddExpectedTargetForm(TEXT("nodeId"), TEXT("pinName"));
    AddExpectedTargetForm(TEXT("nodeId"), TEXT("pin"));
    AddExpectedTargetForm(TEXT("nodePath"), TEXT("pinName"));
    AddExpectedTargetForm(TEXT("nodeName"), TEXT("pin"));
    AddExpectedTargetForm(TEXT("nodeRef"), TEXT("pin"));
    Details->SetArrayField(TEXT("expectedTargetForms"), ExpectedTargetForms);

    UEdGraphNode* Node = LoomleBlueprintAdapterInternal::ResolveNodeByToken(EventGraph, NodeToken);
    Details->SetBoolField(TEXT("nodeFound"), Node != nullptr);
    if (Node)
    {
        TSharedPtr<FJsonObject> NodeSummary = MakeShared<FJsonObject>();
        NodeSummary->SetStringField(TEXT("nodeId"), Node->NodeGuid.ToString(EGuidFormats::DigitsWithHyphens));
        NodeSummary->SetStringField(TEXT("nodeName"), Node->GetName());
        NodeSummary->SetStringField(TEXT("nodePath"), Node->GetPathName());
        NodeSummary->SetStringField(TEXT("nodeTitle"), Node->GetNodeTitle(ENodeTitleType::ListView).ToString());
        Details->SetObjectField(TEXT("matchedNode"), NodeSummary);

        TArray<TSharedPtr<FJsonValue>> CandidatePins;
        for (const UEdGraphPin* CandidatePin : Node->Pins)
        {
            if (!CandidatePin)
            {
                continue;
            }

            TSharedPtr<FJsonObject> PinSummary = MakeShared<FJsonObject>();
            PinSummary->SetStringField(TEXT("pinName"), CandidatePin->PinName.ToString());
            PinSummary->SetStringField(TEXT("direction"), LoomleBlueprintAdapterInternal::PinDirectionToString(CandidatePin->Direction));
            PinSummary->SetStringField(TEXT("category"), CandidatePin->PinType.PinCategory.ToString());
            PinSummary->SetStringField(TEXT("subCategory"), CandidatePin->PinType.PinSubCategory.ToString());
            PinSummary->SetStringField(TEXT("defaultValue"), CandidatePin->DefaultValue);
            CandidatePins.Add(MakeShared<FJsonValueObject>(PinSummary));
        }
        Details->SetArrayField(TEXT("candidatePins"), CandidatePins);
    }
    else
    {
        Details->SetArrayField(TEXT("candidatePins"), TArray<TSharedPtr<FJsonValue>>{});
    }

    OutDetailsJson = LoomleBlueprintAdapterInternal::JsonObjectToString(Details);
    return true;
}

bool FLoomleBlueprintAdapter::ListEventGraphNodes(const FString& BlueprintAssetPath, FString& OutNodesJson, FString& OutError)
{
    return ListGraphNodes(BlueprintAssetPath, TEXT("EventGraph"), OutNodesJson, OutError);
}

bool FLoomleBlueprintAdapter::ListBlueprintGraphs(const FString& BlueprintAssetPath, FString& OutGraphsJson, FString& OutError)
{
    OutGraphsJson = TEXT("[]");
    OutError.Empty();

    UBlueprint* Blueprint = LoomleBlueprintAdapterInternal::LoadBlueprintByAssetPath(BlueprintAssetPath);
    if (!Blueprint)
    {
        OutError = FString::Printf(TEXT("Blueprint not found: %s"), *BlueprintAssetPath);
        return false;
    }

    TArray<TSharedPtr<FJsonValue>> Graphs;
    LoomleBlueprintAdapterInternal::AppendGraphListEntries(Blueprint->UbergraphPages, TEXT("Event"), Graphs);
    LoomleBlueprintAdapterInternal::AppendGraphListEntries(Blueprint->FunctionGraphs, TEXT("Function"), Graphs);
    LoomleBlueprintAdapterInternal::AppendGraphListEntries(Blueprint->MacroGraphs, TEXT("Macro"), Graphs);
    for (const FBPInterfaceDescription& InterfaceDesc : Blueprint->ImplementedInterfaces)
    {
        LoomleBlueprintAdapterInternal::AppendGraphListEntries(InterfaceDesc.Graphs, TEXT("Interface"), Graphs);
    }

    OutGraphsJson = LoomleBlueprintAdapterInternal::JsonArrayToString(Graphs);
    return true;
}

bool FLoomleBlueprintAdapter::ListGraphNodes(
    const FString& BlueprintAssetPath,
    const FString& GraphName,
    FString& OutNodesJson,
    FString& OutError,
    const FLoomleBlueprintNodeListOptions* Options,
    FLoomleBlueprintNodeListStats* OutStats)
{
    OutNodesJson = TEXT("[]");
    OutError.Empty();
    if (OutStats)
    {
        *OutStats = FLoomleBlueprintNodeListStats{};
    }

    if (GraphName.IsEmpty())
    {
        OutError = TEXT("GraphName is required.");
        return false;
    }

    UBlueprint* Blueprint = LoomleBlueprintAdapterInternal::LoadBlueprintByAssetPath(BlueprintAssetPath);
    UEdGraph* Graph = LoomleBlueprintAdapterInternal::FindGraphByName(Blueprint, GraphName);
    if (!Blueprint || !Graph)
    {
        OutError = FString::Printf(TEXT("Graph not found: %s"), *GraphName);
        return false;
    }

    LoomleBlueprintAdapterInternal::SerializeFilteredNodeList(
        Graph->Nodes,
        Graph->GetName(),
        Options,
        OutNodesJson,
        OutStats);
    return true;
}

bool FLoomleBlueprintAdapter::ListCompositeSubgraphNodes(
    const FString& BlueprintAssetPath,
    const FString& CompositeNodeGuid,
    FString& OutSubgraphName,
    FString& OutNodesJson,
    FString& OutError,
    const FLoomleBlueprintNodeListOptions* Options,
    FLoomleBlueprintNodeListStats* OutStats)
{
    OutSubgraphName.Empty();
    OutNodesJson = TEXT("[]");
    OutError.Empty();
    if (OutStats)
    {
        *OutStats = FLoomleBlueprintNodeListStats{};
    }

    if (CompositeNodeGuid.IsEmpty())
    {
        OutError = TEXT("CompositeNodeGuid is required.");
        return false;
    }

    UBlueprint* Blueprint = LoomleBlueprintAdapterInternal::LoadBlueprintByAssetPath(BlueprintAssetPath);
    if (!Blueprint)
    {
        OutError = FString::Printf(TEXT("Blueprint not found: %s"), *BlueprintAssetPath);
        return false;
    }

    FGuid NodeGuid;
    if (!FGuid::Parse(CompositeNodeGuid, NodeGuid))
    {
        OutError = FString::Printf(TEXT("CompositeNodeGuid is not a valid FGuid: %s"), *CompositeNodeGuid);
        return false;
    }

    // Search all graphs for a node with this guid.
    TArray<UEdGraph*> AllGraphs;
    AllGraphs.Append(Blueprint->UbergraphPages);
    AllGraphs.Append(Blueprint->FunctionGraphs);
    AllGraphs.Append(Blueprint->MacroGraphs);

    for (UEdGraph* Graph : AllGraphs)
    {
        if (!Graph)
        {
            continue;
        }
        for (UEdGraphNode* Node : Graph->Nodes)
        {
            if (!Node || Node->NodeGuid != NodeGuid)
            {
                continue;
            }

            // Resolve composite subgraph via BoundGraph property.
            FObjectPropertyBase* BoundGraphProp = FindFProperty<FObjectPropertyBase>(Node->GetClass(), TEXT("BoundGraph"));
            if (!BoundGraphProp)
            {
                OutError = FString::Printf(TEXT("Node %s is not a composite node (no BoundGraph property)."), *CompositeNodeGuid);
                return false;
            }

            UEdGraph* SubGraph = Cast<UEdGraph>(BoundGraphProp->GetObjectPropertyValue_InContainer(Node));
            if (!SubGraph)
            {
                OutError = FString::Printf(TEXT("Node %s BoundGraph is null."), *CompositeNodeGuid);
                return false;
            }

            OutSubgraphName = SubGraph->GetName();

            LoomleBlueprintAdapterInternal::SerializeFilteredNodeList(
                SubGraph->Nodes,
                OutSubgraphName,
                Options,
                OutNodesJson,
                OutStats);
            return true;
        }
    }

    OutError = FString::Printf(TEXT("Node with guid %s not found in blueprint %s."), *CompositeNodeGuid, *BlueprintAssetPath);
    return false;
}

bool FLoomleBlueprintAdapter::GetNodeDetails(const FString& BlueprintAssetPath, const FString& NodeGuid, FString& OutNodeJson, FString& OutError)
{
    OutNodeJson = TEXT("{}");
    OutError.Empty();

    UBlueprint* Blueprint = LoomleBlueprintAdapterInternal::LoadBlueprintByAssetPath(BlueprintAssetPath);
    UEdGraph* EventGraph = LoomleBlueprintAdapterInternal::GetEventGraph(Blueprint);
    if (!Blueprint || !EventGraph)
    {
        OutError = TEXT("Failed to resolve blueprint/event graph.");
        return false;
    }

    UEdGraphNode* Node = LoomleBlueprintAdapterInternal::FindNodeByGuid(EventGraph, NodeGuid);
    if (!Node)
    {
        OutError = FString::Printf(TEXT("Node not found by guid: %s"), *NodeGuid);
        return false;
    }

    OutNodeJson = LoomleBlueprintAdapterInternal::JsonObjectToString(LoomleBlueprintAdapterInternal::SerializeNode(Node));
    return true;
}

bool FLoomleBlueprintAdapter::FindNodesByClass(const FString& BlueprintAssetPath, const FString& NodeClassPathOrName, FString& OutNodesJson, FString& OutError)
{
    OutNodesJson = TEXT("[]");
    OutError.Empty();

    if (NodeClassPathOrName.IsEmpty())
    {
        OutError = TEXT("NodeClassPathOrName is empty.");
        return false;
    }

    UBlueprint* Blueprint = LoomleBlueprintAdapterInternal::LoadBlueprintByAssetPath(BlueprintAssetPath);
    UEdGraph* EventGraph = LoomleBlueprintAdapterInternal::GetEventGraph(Blueprint);
    if (!Blueprint || !EventGraph)
    {
        OutError = TEXT("Failed to resolve blueprint/event graph.");
        return false;
    }

    TArray<TSharedPtr<FJsonValue>> Nodes;
    for (const UEdGraphNode* Node : EventGraph->Nodes)
    {
        if (LoomleBlueprintAdapterInternal::NodeClassMatches(Node, NodeClassPathOrName))
        {
            Nodes.Add(MakeShared<FJsonValueObject>(LoomleBlueprintAdapterInternal::SerializeNode(Node)));
        }
    }

    OutNodesJson = LoomleBlueprintAdapterInternal::JsonArrayToString(Nodes);
    return true;
}

bool FLoomleBlueprintAdapter::CompileBlueprint(const FString& BlueprintAssetPath, const FString& GraphName, FString& OutError)
{
    OutError.Empty();
    (void)GraphName;

    UBlueprint* Blueprint = LoomleBlueprintAdapterInternal::LoadBlueprintByAssetPath(BlueprintAssetPath);
    if (!Blueprint)
    {
        OutError = FString::Printf(TEXT("Blueprint not found: %s"), *BlueprintAssetPath);
        return false;
    }

    FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
    FBlueprintEditorUtils::RefreshAllNodes(Blueprint);
    FKismetEditorUtilities::CompileBlueprint(Blueprint);
    Blueprint->MarkPackageDirty();

    if (Blueprint->Status == BS_Error)
    {
        OutError = TEXT("Compile failed. Check UE compiler log.");
        return false;
    }

    return true;
}

bool FLoomleBlueprintAdapter::SpawnBlueprintActor(const FString& BlueprintAssetPath, FVector Location, FRotator Rotation, FString& OutActorPath, FString& OutError)
{
    OutActorPath.Empty();
    OutError.Empty();

    UBlueprint* Blueprint = LoomleBlueprintAdapterInternal::LoadBlueprintByAssetPath(BlueprintAssetPath);
    if (!Blueprint || !Blueprint->GeneratedClass)
    {
        OutError = TEXT("Blueprint or GeneratedClass not found.");
        return false;
    }

    if (!GEditor)
    {
        OutError = TEXT("GEditor is not available.");
        return false;
    }

    UWorld* EditorWorld = GEditor->GetEditorWorldContext().World();
    if (!EditorWorld)
    {
        OutError = TEXT("Editor world not found.");
        return false;
    }

    AActor* Spawned = EditorWorld->SpawnActor<AActor>(Blueprint->GeneratedClass, Location, Rotation);
    if (!Spawned)
    {
        OutError = TEXT("SpawnActor failed.");
        return false;
    }

    OutActorPath = Spawned->GetPathName();
    return true;
}
