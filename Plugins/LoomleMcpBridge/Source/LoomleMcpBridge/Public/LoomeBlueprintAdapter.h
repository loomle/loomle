#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "LoomeBlueprintAdapter.generated.h"

UCLASS()
class LOOMLEMCPBRIDGE_API ULoomeBlueprintAdapter : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()

public:
    UFUNCTION(BlueprintCallable, Category = "Loomle|LoomeBlueprintAdapter")
    static bool CreateBlueprint(const FString& AssetPath, const FString& ParentClassPath, FString& OutBlueprintObjectPath, FString& OutError);

    UFUNCTION(BlueprintCallable, Category = "Loomle|LoomeBlueprintAdapter")
    static bool AddComponent(const FString& BlueprintAssetPath, const FString& ComponentClassPath, const FString& ComponentName, const FString& ParentComponentName, FString& OutError);
    
    UFUNCTION(BlueprintCallable, Category = "Loomle|LoomeBlueprintAdapter")
    static bool SetStaticMeshComponentAsset(const FString& BlueprintAssetPath, const FString& ComponentName, const FString& MeshAssetPath, FString& OutError);

    UFUNCTION(BlueprintCallable, Category = "Loomle|LoomeBlueprintAdapter")
    static bool SetSceneComponentRelativeLocation(const FString& BlueprintAssetPath, const FString& ComponentName, FVector Location, FString& OutError);

    UFUNCTION(BlueprintCallable, Category = "Loomle|LoomeBlueprintAdapter")
    static bool SetSceneComponentRelativeScale3D(const FString& BlueprintAssetPath, const FString& ComponentName, FVector Scale3D, FString& OutError);

    UFUNCTION(BlueprintCallable, Category = "Loomle|LoomeBlueprintAdapter")
    static bool SetPrimitiveComponentCollisionEnabled(const FString& BlueprintAssetPath, const FString& ComponentName, const FString& CollisionMode, FString& OutError);

    UFUNCTION(BlueprintCallable, Category = "Loomle|LoomeBlueprintAdapter")
    static bool SetBoxComponentExtent(const FString& BlueprintAssetPath, const FString& ComponentName, FVector Extent, FString& OutError);

    UFUNCTION(BlueprintCallable, Category = "Loomle|LoomeBlueprintAdapter")
    static bool SetPrimitiveComponentGenerateOverlapEvents(const FString& BlueprintAssetPath, const FString& ComponentName, bool bGenerate, FString& OutError);

    UFUNCTION(BlueprintCallable, Category = "Loomle|LoomeBlueprintAdapter")
    static bool AddEventNode(const FString& BlueprintAssetPath, const FString& EventName, const FString& EventClassPath, int32 NodePosX, int32 NodePosY, FString& OutNodeGuid, FString& OutError);

    UFUNCTION(BlueprintCallable, Category = "Loomle|LoomeBlueprintAdapter")
    static bool AddCastNode(const FString& BlueprintAssetPath, const FString& TargetClassPath, int32 NodePosX, int32 NodePosY, FString& OutNodeGuid, FString& OutError);

    UFUNCTION(BlueprintCallable, Category = "Loomle|LoomeBlueprintAdapter")
    static bool AddCallFunctionNode(const FString& BlueprintAssetPath, const FString& FunctionClassPath, const FString& FunctionName, int32 NodePosX, int32 NodePosY, FString& OutNodeGuid, FString& OutError);

    UFUNCTION(BlueprintCallable, Category = "Loomle|LoomeBlueprintAdapter")
    static bool ConnectPins(const FString& BlueprintAssetPath, const FString& FromNodeGuid, const FString& FromPinName, const FString& ToNodeGuid, const FString& ToPinName, FString& OutError);

    UFUNCTION(BlueprintCallable, Category = "Loomle|LoomeBlueprintAdapter")
    static bool SetPinDefaultValue(const FString& BlueprintAssetPath, const FString& NodeGuid, const FString& PinName, const FString& Value, FString& OutError);

    UFUNCTION(BlueprintCallable, Category = "Loomle|LoomeBlueprintAdapter")
    static bool ListEventGraphNodes(const FString& BlueprintAssetPath, FString& OutNodesJson, FString& OutError);

    UFUNCTION(BlueprintCallable, Category = "Loomle|LoomeBlueprintAdapter")
    static bool GetNodeDetails(const FString& BlueprintAssetPath, const FString& NodeGuid, FString& OutNodeJson, FString& OutError);

    UFUNCTION(BlueprintCallable, Category = "Loomle|LoomeBlueprintAdapter")
    static bool FindNodesByClass(const FString& BlueprintAssetPath, const FString& NodeClassPathOrName, FString& OutNodesJson, FString& OutError);

    UFUNCTION(BlueprintCallable, Category = "Loomle|LoomeBlueprintAdapter")
    static bool CompileBlueprint(const FString& BlueprintAssetPath, FString& OutError);

    UFUNCTION(BlueprintCallable, Category = "Loomle|LoomeBlueprintAdapter")
    static bool SpawnBlueprintActor(const FString& BlueprintAssetPath, FVector Location, FRotator Rotation, FString& OutActorPath, FString& OutError);
};
