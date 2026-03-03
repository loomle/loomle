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
#include "K2Node_IfThenElse.h"
#include "K2Node_VariableGet.h"
#include "K2Node_VariableSet.h"
#include "Misc/Guid.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Misc/PackageName.h"

namespace LoomleBlueprintAdapterInternal
{
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

    static FString NormalizePinToken(FString Value)
    {
        Value = Value.ToLower();
        Value.ReplaceInline(TEXT(" "), TEXT(""));
        Value.ReplaceInline(TEXT("_"), TEXT(""));
        Value.ReplaceInline(TEXT("-"), TEXT(""));
        return Value;
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

        TArray<TSharedPtr<FJsonValue>> Links;
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
        }
        PinObject->SetArrayField(TEXT("linkedTo"), Links);

        return PinObject;
    }

    static TSharedPtr<FJsonObject> SerializeNode(const UEdGraphNode* Node)
    {
        TSharedPtr<FJsonObject> NodeObject = MakeShared<FJsonObject>();
        if (!Node)
        {
            return NodeObject;
        }

        NodeObject->SetStringField(TEXT("name"), Node->GetName());
        NodeObject->SetStringField(TEXT("guid"), Node->NodeGuid.ToString(EGuidFormats::DigitsWithHyphens));
        NodeObject->SetStringField(TEXT("className"), Node->GetClass()->GetName());
        NodeObject->SetStringField(TEXT("classPath"), Node->GetClass()->GetPathName());
        NodeObject->SetStringField(TEXT("path"), Node->GetPathName());
        NodeObject->SetNumberField(TEXT("nodePosX"), Node->NodePosX);
        NodeObject->SetNumberField(TEXT("nodePosY"), Node->NodePosY);
        NodeObject->SetStringField(TEXT("nodeTitle"), Node->GetNodeTitle(ENodeTitleType::ListView).ToString());
        NodeObject->SetStringField(TEXT("nodeTitleFull"), Node->GetNodeTitle(ENodeTitleType::FullTitle).ToString());
        NodeObject->SetBoolField(TEXT("isNodeEnabled"), Node->IsNodeEnabled());

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

    static const FProperty* ResolveVariableProperty(UBlueprint* Blueprint, const FString& VariableName, const FString& VariableClassPath, bool& bOutSelfContext, UClass*& OutOwnerClass)
    {
        bOutSelfContext = true;
        OutOwnerClass = nullptr;
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
        for (UClass* Class = SearchClass; Class != nullptr && Property == nullptr; Class = Class->GetSuperClass())
        {
            Property = FindFProperty<FProperty>(Class, *VariableName);
            if (Property)
            {
                OutOwnerClass = Class;
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

bool ULoomleBlueprintAdapter::CreateBlueprint(const FString& AssetPath, const FString& ParentClassPath, FString& OutBlueprintObjectPath, FString& OutError)
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

bool ULoomleBlueprintAdapter::AddComponent(const FString& BlueprintAssetPath, const FString& ComponentClassPath, const FString& ComponentName, const FString& ParentComponentName, FString& OutError)
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

bool ULoomleBlueprintAdapter::SetStaticMeshComponentAsset(const FString& BlueprintAssetPath, const FString& ComponentName, const FString& MeshAssetPath, FString& OutError)
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

bool ULoomleBlueprintAdapter::SetSceneComponentRelativeLocation(const FString& BlueprintAssetPath, const FString& ComponentName, FVector Location, FString& OutError)
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

bool ULoomleBlueprintAdapter::SetSceneComponentRelativeScale3D(const FString& BlueprintAssetPath, const FString& ComponentName, FVector Scale3D, FString& OutError)
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

bool ULoomleBlueprintAdapter::SetPrimitiveComponentCollisionEnabled(const FString& BlueprintAssetPath, const FString& ComponentName, const FString& CollisionMode, FString& OutError)
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

bool ULoomleBlueprintAdapter::SetBoxComponentExtent(const FString& BlueprintAssetPath, const FString& ComponentName, FVector Extent, FString& OutError)
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

bool ULoomleBlueprintAdapter::SetPrimitiveComponentGenerateOverlapEvents(const FString& BlueprintAssetPath, const FString& ComponentName, bool bGenerate, FString& OutError)
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

bool ULoomleBlueprintAdapter::AddEventNode(const FString& BlueprintAssetPath, const FString& EventName, const FString& EventClassPath, int32 NodePosX, int32 NodePosY, FString& OutNodeGuid, FString& OutError)
{
    OutNodeGuid.Empty();
    OutError.Empty();

    UBlueprint* Blueprint = LoomleBlueprintAdapterInternal::LoadBlueprintByAssetPath(BlueprintAssetPath);
    UEdGraph* EventGraph = LoomleBlueprintAdapterInternal::GetEventGraph(Blueprint);
    UClass* EventClass = LoomleBlueprintAdapterInternal::ResolveClass(EventClassPath);
    if (!Blueprint || !EventGraph || !EventClass)
    {
        OutError = TEXT("Failed to resolve blueprint/event graph/event class.");
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

bool ULoomleBlueprintAdapter::AddCastNode(const FString& BlueprintAssetPath, const FString& TargetClassPath, int32 NodePosX, int32 NodePosY, FString& OutNodeGuid, FString& OutError)
{
    OutNodeGuid.Empty();
    OutError.Empty();

    UBlueprint* Blueprint = LoomleBlueprintAdapterInternal::LoadBlueprintByAssetPath(BlueprintAssetPath);
    UEdGraph* EventGraph = LoomleBlueprintAdapterInternal::GetEventGraph(Blueprint);
    UClass* TargetClass = LoomleBlueprintAdapterInternal::ResolveClass(TargetClassPath);
    if (!Blueprint || !EventGraph || !TargetClass)
    {
        OutError = TEXT("Failed to resolve blueprint/event graph/target class.");
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

bool ULoomleBlueprintAdapter::AddCallFunctionNode(const FString& BlueprintAssetPath, const FString& FunctionClassPath, const FString& FunctionName, int32 NodePosX, int32 NodePosY, FString& OutNodeGuid, FString& OutError)
{
    OutNodeGuid.Empty();
    OutError.Empty();

    UBlueprint* Blueprint = LoomleBlueprintAdapterInternal::LoadBlueprintByAssetPath(BlueprintAssetPath);
    UEdGraph* EventGraph = LoomleBlueprintAdapterInternal::GetEventGraph(Blueprint);
    UClass* FunctionClass = LoomleBlueprintAdapterInternal::ResolveClass(FunctionClassPath);
    UFunction* Function = FunctionClass ? FunctionClass->FindFunctionByName(*FunctionName) : nullptr;
    if (!Blueprint || !EventGraph || !Function)
    {
        OutError = TEXT("Failed to resolve blueprint/event graph/function.");
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

bool ULoomleBlueprintAdapter::AddBranchNode(const FString& BlueprintAssetPath, int32 NodePosX, int32 NodePosY, FString& OutNodeGuid, FString& OutError)
{
    OutNodeGuid.Empty();
    OutError.Empty();

    UBlueprint* Blueprint = LoomleBlueprintAdapterInternal::LoadBlueprintByAssetPath(BlueprintAssetPath);
    UEdGraph* EventGraph = LoomleBlueprintAdapterInternal::GetEventGraph(Blueprint);
    if (!Blueprint || !EventGraph)
    {
        OutError = TEXT("Failed to resolve blueprint/event graph.");
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

bool ULoomleBlueprintAdapter::AddVariableGetNode(const FString& BlueprintAssetPath, const FString& VariableName, const FString& VariableClassPath, int32 NodePosX, int32 NodePosY, FString& OutNodeGuid, FString& OutError)
{
    OutNodeGuid.Empty();
    OutError.Empty();

    UBlueprint* Blueprint = LoomleBlueprintAdapterInternal::LoadBlueprintByAssetPath(BlueprintAssetPath);
    UEdGraph* EventGraph = LoomleBlueprintAdapterInternal::GetEventGraph(Blueprint);
    if (!Blueprint || !EventGraph)
    {
        OutError = TEXT("Failed to resolve blueprint/event graph.");
        return false;
    }

    bool bSelfContext = true;
    UClass* OwnerClass = nullptr;
    const FProperty* Property = LoomleBlueprintAdapterInternal::ResolveVariableProperty(Blueprint, VariableName, VariableClassPath, bSelfContext, OwnerClass);
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

bool ULoomleBlueprintAdapter::AddVariableSetNode(const FString& BlueprintAssetPath, const FString& VariableName, const FString& VariableClassPath, int32 NodePosX, int32 NodePosY, FString& OutNodeGuid, FString& OutError)
{
    OutNodeGuid.Empty();
    OutError.Empty();

    UBlueprint* Blueprint = LoomleBlueprintAdapterInternal::LoadBlueprintByAssetPath(BlueprintAssetPath);
    UEdGraph* EventGraph = LoomleBlueprintAdapterInternal::GetEventGraph(Blueprint);
    if (!Blueprint || !EventGraph)
    {
        OutError = TEXT("Failed to resolve blueprint/event graph.");
        return false;
    }

    bool bSelfContext = true;
    UClass* OwnerClass = nullptr;
    const FProperty* Property = LoomleBlueprintAdapterInternal::ResolveVariableProperty(Blueprint, VariableName, VariableClassPath, bSelfContext, OwnerClass);
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

bool ULoomleBlueprintAdapter::ConnectPins(const FString& BlueprintAssetPath, const FString& FromNodeGuid, const FString& FromPinName, const FString& ToNodeGuid, const FString& ToPinName, FString& OutError)
{
    OutError.Empty();

    UBlueprint* Blueprint = LoomleBlueprintAdapterInternal::LoadBlueprintByAssetPath(BlueprintAssetPath);
    UEdGraph* EventGraph = LoomleBlueprintAdapterInternal::GetEventGraph(Blueprint);
    if (!Blueprint || !EventGraph)
    {
        OutError = TEXT("Failed to resolve blueprint/event graph.");
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

bool ULoomleBlueprintAdapter::DisconnectPins(const FString& BlueprintAssetPath, const FString& FromNodeGuid, const FString& FromPinName, const FString& ToNodeGuid, const FString& ToPinName, FString& OutError)
{
    OutError.Empty();

    UBlueprint* Blueprint = LoomleBlueprintAdapterInternal::LoadBlueprintByAssetPath(BlueprintAssetPath);
    UEdGraph* EventGraph = LoomleBlueprintAdapterInternal::GetEventGraph(Blueprint);
    if (!Blueprint || !EventGraph)
    {
        OutError = TEXT("Failed to resolve blueprint/event graph.");
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

bool ULoomleBlueprintAdapter::BreakPinLinks(const FString& BlueprintAssetPath, const FString& NodeGuid, const FString& PinName, FString& OutError)
{
    OutError.Empty();

    UBlueprint* Blueprint = LoomleBlueprintAdapterInternal::LoadBlueprintByAssetPath(BlueprintAssetPath);
    UEdGraph* EventGraph = LoomleBlueprintAdapterInternal::GetEventGraph(Blueprint);
    if (!Blueprint || !EventGraph)
    {
        OutError = TEXT("Failed to resolve blueprint/event graph.");
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

bool ULoomleBlueprintAdapter::RemoveNode(const FString& BlueprintAssetPath, const FString& NodeGuid, FString& OutError)
{
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

    Node->DestroyNode();
    return true;
}

bool ULoomleBlueprintAdapter::MoveNode(const FString& BlueprintAssetPath, const FString& NodeGuid, int32 NodePosX, int32 NodePosY, FString& OutError)
{
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

    Node->Modify();
    Node->NodePosX = NodePosX;
    Node->NodePosY = NodePosY;
    Node->SnapToGrid(16);
    return true;
}

bool ULoomleBlueprintAdapter::SetPinDefaultValue(const FString& BlueprintAssetPath, const FString& NodeGuid, const FString& PinName, const FString& Value, FString& OutError)
{
    OutError.Empty();

    UBlueprint* Blueprint = LoomleBlueprintAdapterInternal::LoadBlueprintByAssetPath(BlueprintAssetPath);
    UEdGraph* EventGraph = LoomleBlueprintAdapterInternal::GetEventGraph(Blueprint);
    if (!Blueprint || !EventGraph)
    {
        OutError = TEXT("Failed to resolve blueprint/event graph.");
        return false;
    }

    UEdGraphNode* Node = LoomleBlueprintAdapterInternal::FindNodeByGuid(EventGraph, NodeGuid);
    UEdGraphPin* Pin = LoomleBlueprintAdapterInternal::ResolvePin(Node, PinName);
    if (!Node || !Pin)
    {
        OutError = TEXT("Failed to resolve node or pin.");
        return false;
    }

    Pin->DefaultValue = Value;
    return true;
}

bool ULoomleBlueprintAdapter::ListEventGraphNodes(const FString& BlueprintAssetPath, FString& OutNodesJson, FString& OutError)
{
    OutNodesJson = TEXT("[]");
    OutError.Empty();

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
        Nodes.Add(MakeShared<FJsonValueObject>(LoomleBlueprintAdapterInternal::SerializeNode(Node)));
    }

    OutNodesJson = LoomleBlueprintAdapterInternal::JsonArrayToString(Nodes);
    return true;
}

bool ULoomleBlueprintAdapter::GetNodeDetails(const FString& BlueprintAssetPath, const FString& NodeGuid, FString& OutNodeJson, FString& OutError)
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

bool ULoomleBlueprintAdapter::FindNodesByClass(const FString& BlueprintAssetPath, const FString& NodeClassPathOrName, FString& OutNodesJson, FString& OutError)
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

bool ULoomleBlueprintAdapter::CompileBlueprint(const FString& BlueprintAssetPath, FString& OutError)
{
    OutError.Empty();

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

bool ULoomleBlueprintAdapter::SpawnBlueprintActor(const FString& BlueprintAssetPath, FVector Location, FRotator Rotation, FString& OutActorPath, FString& OutError)
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
