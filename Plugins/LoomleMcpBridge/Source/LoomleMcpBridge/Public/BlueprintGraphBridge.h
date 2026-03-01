#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "BlueprintGraphBridge.generated.h"

UCLASS()
class LOOMLEMCPBRIDGE_API UBlueprintGraphBridge : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()

public:
    UFUNCTION(BlueprintCallable, Category = "Loomle|BlueprintGraphBridge")
    static bool CreateBlueprint(const FString& AssetPath, const FString& ParentClassPath, FString& OutBlueprintObjectPath, FString& OutError);

    UFUNCTION(BlueprintCallable, Category = "Loomle|BlueprintGraphBridge")
    static bool AddComponent(const FString& BlueprintAssetPath, const FString& ComponentClassPath, const FString& ComponentName, const FString& ParentComponentName, FString& OutError);
    
    UFUNCTION(BlueprintCallable, Category = "Loomle|BlueprintGraphBridge")
    static bool SetStaticMeshComponentAsset(const FString& BlueprintAssetPath, const FString& ComponentName, const FString& MeshAssetPath, FString& OutError);

    UFUNCTION(BlueprintCallable, Category = "Loomle|BlueprintGraphBridge")
    static bool SetSceneComponentRelativeLocation(const FString& BlueprintAssetPath, const FString& ComponentName, FVector Location, FString& OutError);

    UFUNCTION(BlueprintCallable, Category = "Loomle|BlueprintGraphBridge")
    static bool SetSceneComponentRelativeScale3D(const FString& BlueprintAssetPath, const FString& ComponentName, FVector Scale3D, FString& OutError);

    UFUNCTION(BlueprintCallable, Category = "Loomle|BlueprintGraphBridge")
    static bool SetPrimitiveComponentCollisionEnabled(const FString& BlueprintAssetPath, const FString& ComponentName, const FString& CollisionMode, FString& OutError);

    UFUNCTION(BlueprintCallable, Category = "Loomle|BlueprintGraphBridge")
    static bool SetBoxComponentExtent(const FString& BlueprintAssetPath, const FString& ComponentName, FVector Extent, FString& OutError);

    UFUNCTION(BlueprintCallable, Category = "Loomle|BlueprintGraphBridge")
    static bool SetPrimitiveComponentGenerateOverlapEvents(const FString& BlueprintAssetPath, const FString& ComponentName, bool bGenerate, FString& OutError);

    UFUNCTION(BlueprintCallable, Category = "Loomle|BlueprintGraphBridge")
    static bool AddEventNode(const FString& BlueprintAssetPath, const FString& EventName, const FString& EventClassPath, int32 NodePosX, int32 NodePosY, FString& OutNodeGuid, FString& OutError);

    UFUNCTION(BlueprintCallable, Category = "Loomle|BlueprintGraphBridge")
    static bool AddCastNode(const FString& BlueprintAssetPath, const FString& TargetClassPath, int32 NodePosX, int32 NodePosY, FString& OutNodeGuid, FString& OutError);

    UFUNCTION(BlueprintCallable, Category = "Loomle|BlueprintGraphBridge")
    static bool AddCallFunctionNode(const FString& BlueprintAssetPath, const FString& FunctionClassPath, const FString& FunctionName, int32 NodePosX, int32 NodePosY, FString& OutNodeGuid, FString& OutError);

    UFUNCTION(BlueprintCallable, Category = "Loomle|BlueprintGraphBridge")
    static bool ConnectPins(const FString& BlueprintAssetPath, const FString& FromNodeGuid, const FString& FromPinName, const FString& ToNodeGuid, const FString& ToPinName, FString& OutError);

    UFUNCTION(BlueprintCallable, Category = "Loomle|BlueprintGraphBridge")
    static bool SetPinDefaultValue(const FString& BlueprintAssetPath, const FString& NodeGuid, const FString& PinName, const FString& Value, FString& OutError);

    UFUNCTION(BlueprintCallable, Category = "Loomle|BlueprintGraphBridge")
    static bool CompileBlueprint(const FString& BlueprintAssetPath, FString& OutError);

    UFUNCTION(BlueprintCallable, Category = "Loomle|BlueprintGraphBridge")
    static bool SpawnBlueprintActor(const FString& BlueprintAssetPath, FVector Location, FRotator Rotation, FString& OutActorPath, FString& OutError);
};
