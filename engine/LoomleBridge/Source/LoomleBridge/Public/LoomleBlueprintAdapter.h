#pragma once

#include "CoreMinimal.h"

struct LOOMLEBRIDGE_API FLoomleBlueprintNodeListOptions
{
    TArray<FString> NodeClasses;
    TArray<FString> NodeIds;
    FString Text;
    int32 Limit = TNumericLimits<int32>::Max();
    int32 Offset = 0;
};

struct LOOMLEBRIDGE_API FLoomleBlueprintNodeListStats
{
    int32 TotalNodes = 0;
    int32 MatchingNodes = 0;
};

class LOOMLEBRIDGE_API FLoomleBlueprintAdapter
{

public:
    static bool CreateBlueprint(const FString& AssetPath, const FString& ParentClassPath, FString& OutBlueprintObjectPath, FString& OutError);
    static bool SetParentClass(const FString& BlueprintAssetPath, const FString& ParentClassPath, FString& OutError);
    static bool ListImplementedInterfaces(const FString& BlueprintAssetPath, FString& OutInterfacesJson, FString& OutError);
    static bool AddInterface(const FString& BlueprintAssetPath, const FString& InterfaceClassPath, FString& OutError);
    static bool RemoveInterface(const FString& BlueprintAssetPath, const FString& InterfaceClassPath, bool bPreserveFunctions, FString& OutError);
    static bool EditComponentMember(const FString& BlueprintAssetPath, const FString& Operation, const FString& PayloadJson, FString& OutError);
    static bool EditVariableMember(const FString& BlueprintAssetPath, const FString& Operation, const FString& PayloadJson, FString& OutError);
    static bool EditFunctionMember(const FString& BlueprintAssetPath, const FString& Operation, const FString& PayloadJson, FString& OutError);
    static bool EditMacroMember(const FString& BlueprintAssetPath, const FString& Operation, const FString& PayloadJson, FString& OutError);
    static bool EditDispatcherMember(const FString& BlueprintAssetPath, const FString& Operation, const FString& PayloadJson, FString& OutError);
    static bool EditEventMember(const FString& BlueprintAssetPath, const FString& Operation, const FString& PayloadJson, FString& OutError);
    static bool AddComponent(const FString& BlueprintAssetPath, const FString& ComponentClassPath, const FString& ComponentName, const FString& ParentComponentName, FString& OutError);
    static bool SetStaticMeshComponentAsset(const FString& BlueprintAssetPath, const FString& ComponentName, const FString& MeshAssetPath, FString& OutError);
    static bool SetSceneComponentRelativeLocation(const FString& BlueprintAssetPath, const FString& ComponentName, FVector Location, FString& OutError);
    static bool SetSceneComponentRelativeScale3D(const FString& BlueprintAssetPath, const FString& ComponentName, FVector Scale3D, FString& OutError);
    static bool SetPrimitiveComponentCollisionEnabled(const FString& BlueprintAssetPath, const FString& ComponentName, const FString& CollisionMode, FString& OutError);
    static bool SetBoxComponentExtent(const FString& BlueprintAssetPath, const FString& ComponentName, FVector Extent, FString& OutError);
    static bool SetPrimitiveComponentGenerateOverlapEvents(const FString& BlueprintAssetPath, const FString& ComponentName, bool bGenerate, FString& OutError);
    static bool AddEventNode(const FString& BlueprintAssetPath, const FString& GraphName, const FString& EventName, const FString& EventClassPath, int32 NodePosX, int32 NodePosY, FString& OutNodeGuid, FString& OutError);
    static bool AddCustomEventNode(const FString& BlueprintAssetPath, const FString& GraphName, const FString& EventName, const FString& PayloadJson, int32 NodePosX, int32 NodePosY, FString& OutNodeGuid, FString& OutError);
    static bool AddCastNode(const FString& BlueprintAssetPath, const FString& GraphName, const FString& TargetClassPath, int32 NodePosX, int32 NodePosY, FString& OutNodeGuid, FString& OutError);
    static bool AddCallFunctionNode(const FString& BlueprintAssetPath, const FString& GraphName, const FString& FunctionClassPath, const FString& FunctionName, int32 NodePosX, int32 NodePosY, FString& OutNodeGuid, FString& OutError);
    static bool AddBranchNode(const FString& BlueprintAssetPath, const FString& GraphName, int32 NodePosX, int32 NodePosY, FString& OutNodeGuid, FString& OutError);
    static bool AddExecutionSequenceNode(const FString& BlueprintAssetPath, const FString& GraphName, int32 NodePosX, int32 NodePosY, FString& OutNodeGuid, FString& OutError);
    static bool AddMacroNode(const FString& BlueprintAssetPath, const FString& GraphName, const FString& MacroLibraryAssetPath, const FString& MacroGraphName, int32 NodePosX, int32 NodePosY, FString& OutNodeGuid, FString& OutError);
    static bool AddCommentNode(const FString& BlueprintAssetPath, const FString& GraphName, const FString& CommentText, int32 NodePosX, int32 NodePosY, int32 Width, int32 Height, FString& OutNodeGuid, FString& OutError);
    static bool AddKnotNode(const FString& BlueprintAssetPath, const FString& GraphName, int32 NodePosX, int32 NodePosY, FString& OutNodeGuid, FString& OutError);
    static bool AddVariableGetNode(const FString& BlueprintAssetPath, const FString& GraphName, const FString& VariableName, const FString& VariableClassPath, int32 NodePosX, int32 NodePosY, FString& OutNodeGuid, FString& OutError);
    static bool AddVariableSetNode(const FString& BlueprintAssetPath, const FString& GraphName, const FString& VariableName, const FString& VariableClassPath, int32 NodePosX, int32 NodePosY, FString& OutNodeGuid, FString& OutError);
    static bool AddNodeByClass(const FString& BlueprintAssetPath, const FString& GraphName, const FString& NodeClassPath, const FString& PayloadJson, int32 NodePosX, int32 NodePosY, FString& OutNodeGuid, FString& OutError);
    static bool DuplicateNode(const FString& BlueprintAssetPath, const FString& GraphName, const FString& NodeGuid, int32 DeltaX, int32 DeltaY, FString& OutNewNodeGuid, FString& OutError);
    static bool ConnectPins(const FString& BlueprintAssetPath, const FString& GraphName, const FString& FromNodeGuid, const FString& FromPinName, const FString& ToNodeGuid, const FString& ToPinName, FString& OutError);
    static bool DisconnectPins(const FString& BlueprintAssetPath, const FString& GraphName, const FString& FromNodeGuid, const FString& FromPinName, const FString& ToNodeGuid, const FString& ToPinName, FString& OutError);
    static bool BreakPinLinks(const FString& BlueprintAssetPath, const FString& GraphName, const FString& NodeGuid, const FString& PinName, FString& OutError);
    static bool RemoveNode(const FString& BlueprintAssetPath, const FString& GraphName, const FString& NodeGuid, FString& OutError);
    static bool MoveNode(const FString& BlueprintAssetPath, const FString& GraphName, const FString& NodeGuid, int32 NodePosX, int32 NodePosY, FString& OutError);
    static bool SetPinDefaultValue(const FString& BlueprintAssetPath, const FString& GraphName, const FString& NodeGuid, const FString& PinName, const FString& Value, FString& OutError);
    static bool SetNodeComment(const FString& BlueprintAssetPath, const FString& GraphName, const FString& NodeGuid, const FString& Comment, FString& OutError);
    static bool SetNodeEnabled(const FString& BlueprintAssetPath, const FString& GraphName, const FString& NodeGuid, bool bEnabled, FString& OutError);
    static bool AddFunctionGraph(const FString& AssetPath, const FString& GraphName, FString& OutError);
    static bool AddMacroGraph(const FString& AssetPath, const FString& GraphName, FString& OutError);
    static bool RenameGraph(const FString& AssetPath, const FString& OldGraphName, const FString& NewGraphName, FString& OutError);
    static bool DeleteGraph(const FString& AssetPath, const FString& GraphName, FString& OutError);
    static bool DescribePinTarget(const FString& BlueprintAssetPath, const FString& GraphName, const FString& NodeToken, const FString& PinName, FString& OutDetailsJson, FString& OutError);
    static bool ListEventGraphNodes(const FString& BlueprintAssetPath, FString& OutNodesJson, FString& OutError);
    static bool ListBlueprintGraphs(const FString& BlueprintAssetPath, FString& OutGraphsJson, FString& OutError);
    static bool ResolveGraphIdByName(const FString& BlueprintAssetPath, const FString& GraphName, FString& OutGraphId, FString& OutError);
    static bool ResolveGraphNameById(const FString& BlueprintAssetPath, const FString& GraphId, FString& OutGraphName, FString& OutError);
    static bool ListGraphNodes(
        const FString& BlueprintAssetPath,
        const FString& GraphName,
        FString& OutNodesJson,
        FString& OutError,
        const FLoomleBlueprintNodeListOptions* Options = nullptr,
        FLoomleBlueprintNodeListStats* OutStats = nullptr);
    static bool ListCompositeSubgraphs(
        const FString& BlueprintAssetPath,
        FString& OutGraphsJson,
        FString& OutError);
    static bool ListCompositeSubgraphNodes(
        const FString& BlueprintAssetPath,
        const FString& CompositeNodeGuid,
        FString& OutSubgraphName,
        FString& OutNodesJson,
        FString& OutError,
        const FLoomleBlueprintNodeListOptions* Options = nullptr,
        FLoomleBlueprintNodeListStats* OutStats = nullptr);
    static bool GetNodeDetails(const FString& BlueprintAssetPath, const FString& NodeGuid, FString& OutNodeJson, FString& OutError);
    static bool FindNodesByClass(const FString& BlueprintAssetPath, const FString& NodeClassPathOrName, FString& OutNodesJson, FString& OutError);
    static bool CompileBlueprint(const FString& BlueprintAssetPath, const FString& GraphName, FString& OutError);
    static bool SpawnBlueprintActor(const FString& BlueprintAssetPath, FVector Location, FRotator Rotation, FString& OutActorPath, FString& OutError);
};
