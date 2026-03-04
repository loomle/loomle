#include "LoomleBridgeModule.h"

#include "Async/Async.h"
#include "Editor.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Algo/Sort.h"
#include "Components/ActorComponent.h"
#include "Components/BoxComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Containers/Ticker.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "Engine/Engine.h"
#include "Engine/Selection.h"
#include "Engine/SCS_Node.h"
#include "Engine/SimpleConstructionScript.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Character.h"
#include "Framework/Application/SlateApplication.h"
#include "HAL/PlatformTime.h"
#include "IPythonScriptPlugin.h"
#include "Json.h"
#include "HAL/FileManager.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Engine/Blueprint.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpression.h"
#include "IMaterialEditor.h"
#include "MaterialGraph/MaterialGraphNode.h"
#include "LoomleBlueprintAdapter.h"
#include "LoomlePipeServer.h"
#include "Misc/App.h"
#include "Misc/Base64.h"
#include "Misc/DateTime.h"
#include "Misc/EngineVersion.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/UnrealType.h"
#include "Misc/TransactionObjectEvent.h"
#include "Widgets/SWindow.h"
#include "BlueprintEditor.h"

DEFINE_LOG_CATEGORY_STATIC(LogLoomleBridge, Log, All);

using FCondensedJsonWriter = TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>;

namespace LoomleBridgeConstants
{
    static const TCHAR* PipeName = TEXT("loomle");
    static const TCHAR* LiveToolName = TEXT("live");
    static const TCHAR* ExecuteToolName = TEXT("execute");
    static const TCHAR* GraphToolName = TEXT("graph");
    static const TCHAR* GraphListToolName = TEXT("graph.list");
    static const TCHAR* GraphQueryToolName = TEXT("graph.query");
    static const TCHAR* GraphMutateToolName = TEXT("graph.mutate");
    static const TCHAR* GraphWatchToolName = TEXT("graph.watch");
    static const TCHAR* LiveNotificationMethod = TEXT("notifications/live");
    static const TCHAR* LiveLogFileName = TEXT("live_events.jsonl");
    constexpr int32 MaxLiveLogRecords = 100;
    constexpr int32 MaxLiveMemoryRecords = 300;
    constexpr int32 LiveLogTrimInterval = 25;
}

namespace
{
TSharedPtr<FJsonObject> ToVectorJson(const FVector& Value)
{
    TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
    Obj->SetNumberField(TEXT("x"), Value.X);
    Obj->SetNumberField(TEXT("y"), Value.Y);
    Obj->SetNumberField(TEXT("z"), Value.Z);
    return Obj;
}

TSharedPtr<FJsonObject> ToRotatorJson(const FRotator& Value)
{
    TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
    Obj->SetNumberField(TEXT("pitch"), Value.Pitch);
    Obj->SetNumberField(TEXT("yaw"), Value.Yaw);
    Obj->SetNumberField(TEXT("roll"), Value.Roll);
    return Obj;
}

TSharedPtr<FJsonObject> ToTransformJson(const FTransform& Value)
{
    TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
    Obj->SetObjectField(TEXT("location"), ToVectorJson(Value.GetLocation()));
    Obj->SetObjectField(TEXT("rotation"), ToRotatorJson(Value.Rotator()));
    Obj->SetObjectField(TEXT("scale"), ToVectorJson(Value.GetScale3D()));
    return Obj;
}

bool IsLiveLifecycleEvent(const FString& EventName)
{
    return EventName.Equals(TEXT("live_started")) || EventName.Equals(TEXT("live_stopping"));
}

bool IsGraphEventName(const FString& EventName)
{
    return EventName.StartsWith(TEXT("graph."));
}

bool IsLikelySystemActorIdentity(const FString& ActorName, const FString& ActorPath, const FString& ClassPath)
{
    if (ClassPath.Equals(TEXT("/Script/Engine.DefaultPhysicsVolume"))
        || ClassPath.Equals(TEXT("/Script/GameplayDebugger.GameplayDebuggerPlayerManager"))
        || ClassPath.Equals(TEXT("/Script/NavigationSystem.AbstractNavData")))
    {
        return true;
    }

    return ActorName.Contains(TEXT("DefaultPhysicsVolume"))
        || ActorName.Contains(TEXT("GameplayDebugger"))
        || ActorName.Contains(TEXT("ChaosDebugDraw"))
        || ActorName.Contains(TEXT("AbstractNavData"))
        || ActorPath.Contains(TEXT("DefaultPhysicsVolume"))
        || ActorPath.Contains(TEXT("GameplayDebugger"))
        || ActorPath.Contains(TEXT("ChaosDebugDraw"))
        || ActorPath.Contains(TEXT("AbstractNavData"));
}

FString DeriveEventOrigin(const FString& EventName, const TSharedPtr<FJsonObject>& Data)
{
    if (IsLiveLifecycleEvent(EventName))
    {
        return TEXT("system");
    }

    if (EventName.Equals(TEXT("selection_changed"))
        || EventName.Equals(TEXT("graph.selection_changed"))
        || EventName.Equals(TEXT("actor_moved"))
        || EventName.Equals(TEXT("actor_deleted"))
        || EventName.Equals(TEXT("actor_attached"))
        || EventName.Equals(TEXT("actor_detached"))
        || EventName.Equals(TEXT("pie_started"))
        || EventName.Equals(TEXT("pie_stopped"))
        || EventName.Equals(TEXT("pie_paused"))
        || EventName.Equals(TEXT("pie_resumed"))
        || EventName.Equals(TEXT("undo_redo")))
    {
        return TEXT("user");
    }

    if (EventName.Equals(TEXT("actor_added")) && Data.IsValid())
    {
        FString Name;
        FString Path;
        FString ClassPath;
        Data->TryGetStringField(TEXT("name"), Name);
        Data->TryGetStringField(TEXT("path"), Path);
        Data->TryGetStringField(TEXT("class"), ClassPath);
        return IsLikelySystemActorIdentity(Name, Path, ClassPath) ? TEXT("system") : TEXT("user");
    }

    if (EventName.Equals(TEXT("object_property_changed")) && Data.IsValid())
    {
        FString ObjectClass;
        FString Property;
        FString ActorName;
        FString ActorPath;
        Data->TryGetStringField(TEXT("objectClass"), ObjectClass);
        Data->TryGetStringField(TEXT("property"), Property);
        Data->TryGetStringField(TEXT("actorName"), ActorName);
        Data->TryGetStringField(TEXT("actorPath"), ActorPath);

        if (ObjectClass.Equals(TEXT("/Script/NavigationSystem.AbstractNavData"))
            || Property.Equals(TEXT("ActorLabel"))
            || IsLikelySystemActorIdentity(ActorName, ActorPath, ObjectClass))
        {
            return TEXT("system");
        }
        return TEXT("user");
    }

    if (IsGraphEventName(EventName))
    {
        return TEXT("user");
    }

    return TEXT("unknown");
}

bool ShouldReturnInLivePull(const TSharedPtr<FJsonObject>& EventObject)
{
    if (!EventObject.IsValid())
    {
        return false;
    }

    const TSharedPtr<FJsonObject>* Params = nullptr;
    if (!EventObject->TryGetObjectField(TEXT("params"), Params) || Params == nullptr || !(*Params).IsValid())
    {
        return true;
    }

    FString EventName;
    if (!(*Params)->TryGetStringField(TEXT("event"), EventName))
    {
        return true;
    }

    return !IsLiveLifecycleEvent(EventName);
}

bool TryReadEventParams(const TSharedPtr<FJsonObject>& EventObject, const TSharedPtr<FJsonObject>*& OutParams)
{
    OutParams = nullptr;
    if (!EventObject.IsValid())
    {
        return false;
    }

    if (!EventObject->TryGetObjectField(TEXT("params"), OutParams) || OutParams == nullptr || !(*OutParams).IsValid())
    {
        return false;
    }

    return true;
}

bool ShouldIncludeEventForScope(const TSharedPtr<FJsonObject>& EventObject, const FString& ScopeFilter)
{
    if (ScopeFilter.IsEmpty())
    {
        return true;
    }

    const TSharedPtr<FJsonObject>* Params = nullptr;
    if (!TryReadEventParams(EventObject, Params))
    {
        return false;
    }

    FString Scope;
    if ((*Params)->TryGetStringField(TEXT("scope"), Scope))
    {
        return Scope.Equals(ScopeFilter, ESearchCase::IgnoreCase);
    }

    FString EventName;
    (*Params)->TryGetStringField(TEXT("event"), EventName);
    if (ScopeFilter.Equals(TEXT("graph"), ESearchCase::IgnoreCase))
    {
        return IsGraphEventName(EventName);
    }
    return !IsGraphEventName(EventName);
}

bool ShouldIncludeEventForAsset(const TSharedPtr<FJsonObject>& EventObject, const FString& AssetPathFilter)
{
    if (AssetPathFilter.IsEmpty())
    {
        return true;
    }

    const TSharedPtr<FJsonObject>* Params = nullptr;
    if (!TryReadEventParams(EventObject, Params))
    {
        return false;
    }

    const TSharedPtr<FJsonObject>* Data = nullptr;
    if (!(*Params)->TryGetObjectField(TEXT("data"), Data) || Data == nullptr || !(*Data).IsValid())
    {
        return false;
    }

    FString AssetPath;
    if ((*Data)->TryGetStringField(TEXT("assetPath"), AssetPath))
    {
        return AssetPath.Equals(AssetPathFilter);
    }

    return false;
}

TSharedPtr<FJsonObject> BuildActiveWindowJson()
{
    TSharedPtr<FJsonObject> Window = MakeShared<FJsonObject>();
    Window->SetBoolField(TEXT("isValid"), false);
    Window->SetStringField(TEXT("title"), TEXT(""));

    if (!FSlateApplication::IsInitialized())
    {
        return Window;
    }

    TSharedPtr<SWindow> ActiveWindow = FSlateApplication::Get().GetActiveTopLevelWindow();
    if (!ActiveWindow.IsValid())
    {
        const TSharedPtr<SWidget> FocusedWidget = FSlateApplication::Get().GetKeyboardFocusedWidget();
        if (FocusedWidget.IsValid())
        {
            ActiveWindow = FSlateApplication::Get().FindWidgetWindow(FocusedWidget.ToSharedRef());
        }
    }

    if (!ActiveWindow.IsValid())
    {
        return Window;
    }

    Window->SetBoolField(TEXT("isValid"), true);
    Window->SetStringField(TEXT("title"), ActiveWindow->GetTitle().ToString());
    return Window;
}

FString GetActiveWindowTitle()
{
    if (!FSlateApplication::IsInitialized())
    {
        return FString();
    }

    TSharedPtr<SWindow> ActiveWindow = FSlateApplication::Get().GetActiveTopLevelWindow();
    if (!ActiveWindow.IsValid())
    {
        const TSharedPtr<SWidget> FocusedWidget = FSlateApplication::Get().GetKeyboardFocusedWidget();
        if (FocusedWidget.IsValid())
        {
            ActiveWindow = FSlateApplication::Get().FindWidgetWindow(FocusedWidget.ToSharedRef());
        }
    }

    return ActiveWindow.IsValid() ? ActiveWindow->GetTitle().ToString() : FString();
}

UBlueprint* FindEditedBlueprint()
{
    if (!GEditor)
    {
        return nullptr;
    }

    UAssetEditorSubsystem* AssetEditorSubsystem = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>();
    if (!AssetEditorSubsystem)
    {
        return nullptr;
    }

    const FString ActiveWindowTitle = GetActiveWindowTitle();
    UBlueprint* FallbackBlueprint = nullptr;
    UBlueprint* BestTitleMatchBlueprint = nullptr;
    int32 BestTitleMatchNameLen = -1;

    const TArray<UObject*> EditedAssets = AssetEditorSubsystem->GetAllEditedAssets();
    for (UObject* Asset : EditedAssets)
    {
        UBlueprint* Blueprint = Cast<UBlueprint>(Asset);
        if (!Blueprint)
        {
            continue;
        }

        if (!FallbackBlueprint)
        {
            FallbackBlueprint = Blueprint;
        }

        if (!ActiveWindowTitle.IsEmpty()
            && ActiveWindowTitle.Contains(Blueprint->GetName(), ESearchCase::IgnoreCase))
        {
            const int32 NameLen = Blueprint->GetName().Len();
            if (NameLen > BestTitleMatchNameLen)
            {
                BestTitleMatchNameLen = NameLen;
                BestTitleMatchBlueprint = Blueprint;
            }
        }
    }

    if (BestTitleMatchBlueprint)
    {
        return BestTitleMatchBlueprint;
    }

    return ActiveWindowTitle.IsEmpty() ? FallbackBlueprint : nullptr;
}

bool CollectSelectedBlueprintNodes(TArray<UEdGraphNode*>& OutNodes, UBlueprint*& OutBlueprint)
{
    OutNodes.Reset();
    OutBlueprint = nullptr;

    UBlueprint* EditedBlueprint = FindEditedBlueprint();
    if (!EditedBlueprint || !GEditor)
    {
        return false;
    }

    UAssetEditorSubsystem* AssetEditorSubsystem = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>();
    if (!AssetEditorSubsystem)
    {
        return false;
    }

    IAssetEditorInstance* AssetEditorInstance = AssetEditorSubsystem->FindEditorForAsset(EditedBlueprint, false);
    FBlueprintEditor* BlueprintEditor = static_cast<FBlueprintEditor*>(AssetEditorInstance);
    if (!BlueprintEditor)
    {
        return false;
    }

    const FGraphPanelSelectionSet SelectedNodes = BlueprintEditor->GetSelectedNodes();
    for (UObject* SelectedObject : SelectedNodes)
    {
        UEdGraphNode* Node = Cast<UEdGraphNode>(SelectedObject);
        if (Node)
        {
            OutNodes.Add(Node);
        }
    }

    OutBlueprint = EditedBlueprint;
    return OutNodes.Num() > 0;
}

bool BuildBlueprintContextSnapshot(TSharedPtr<FJsonObject>& OutContext)
{
    OutContext.Reset();
    UBlueprint* Blueprint = FindEditedBlueprint();
    if (!Blueprint)
    {
        return false;
    }

    OutContext = MakeShared<FJsonObject>();
    OutContext->SetBoolField(TEXT("isError"), false);
    OutContext->SetStringField(TEXT("editorType"), TEXT("blueprint"));
    OutContext->SetStringField(TEXT("provider"), TEXT("blueprint_adapter"));
    OutContext->SetStringField(TEXT("assetName"), Blueprint->GetName());
    OutContext->SetStringField(TEXT("assetPath"), Blueprint->GetPathName());
    OutContext->SetStringField(TEXT("assetClass"), Blueprint->GetClass()->GetPathName());
    OutContext->SetStringField(TEXT("status"), TEXT("active"));
    return true;
}

bool BuildBlueprintSelectionSnapshot(TSharedPtr<FJsonObject>& OutSelection)
{
    OutSelection.Reset();

    TArray<UEdGraphNode*> SelectedNodes;
    UBlueprint* Blueprint = nullptr;
    if (!CollectSelectedBlueprintNodes(SelectedNodes, Blueprint) || !Blueprint)
    {
        return false;
    }

    OutSelection = MakeShared<FJsonObject>();
    OutSelection->SetBoolField(TEXT("isError"), false);
    OutSelection->SetStringField(TEXT("editorType"), TEXT("blueprint"));
    OutSelection->SetStringField(TEXT("provider"), TEXT("blueprint_adapter"));
    OutSelection->SetStringField(TEXT("assetPath"), Blueprint->GetPathName());
    OutSelection->SetStringField(TEXT("selectionKind"), TEXT("graph_node"));

    TArray<TSharedPtr<FJsonValue>> Items;
    Items.Reserve(SelectedNodes.Num());
    for (UEdGraphNode* Node : SelectedNodes)
    {
        if (!Node)
        {
            continue;
        }

        TSharedPtr<FJsonObject> Item = MakeShared<FJsonObject>();
        Item->SetStringField(TEXT("id"), Node->NodeGuid.ToString(EGuidFormats::DigitsWithHyphensLower));
        Item->SetStringField(TEXT("name"), Node->GetName());
        Item->SetStringField(TEXT("class"), Node->GetClass() ? Node->GetClass()->GetPathName() : TEXT(""));
        Item->SetStringField(TEXT("path"), Node->GetPathName());
        Item->SetNumberField(TEXT("nodePosX"), Node->NodePosX);
        Item->SetNumberField(TEXT("nodePosY"), Node->NodePosY);
        if (UEdGraph* Graph = Node->GetGraph())
        {
            Item->SetStringField(TEXT("graphName"), Graph->GetName());
            Item->SetStringField(TEXT("graphPath"), Graph->GetPathName());
        }
        Items.Add(MakeShared<FJsonValueObject>(Item));
    }

    OutSelection->SetArrayField(TEXT("items"), Items);
    OutSelection->SetNumberField(TEXT("count"), Items.Num());
    return true;
}

UMaterial* FindEditedMaterial()
{
    if (!GEditor)
    {
        return nullptr;
    }

    UAssetEditorSubsystem* AssetEditorSubsystem = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>();
    if (!AssetEditorSubsystem)
    {
        return nullptr;
    }

    const FString ActiveWindowTitle = GetActiveWindowTitle();
    UMaterial* FallbackMaterial = nullptr;

    const TArray<UObject*> EditedAssets = AssetEditorSubsystem->GetAllEditedAssets();
    for (UObject* Asset : EditedAssets)
    {
        UMaterial* Material = Cast<UMaterial>(Asset);
        if (!Material)
        {
            continue;
        }

        if (!FallbackMaterial)
        {
            FallbackMaterial = Material;
        }

        if (!ActiveWindowTitle.IsEmpty()
            && ActiveWindowTitle.Contains(Material->GetName(), ESearchCase::IgnoreCase))
        {
            return Material;
        }
    }

    return ActiveWindowTitle.IsEmpty() ? FallbackMaterial : nullptr;
}

bool CollectSelectedMaterialExpressions(TArray<UMaterialExpression*>& OutExpressions, UMaterial*& OutMaterial)
{
    OutExpressions.Reset();
    OutMaterial = nullptr;

    UMaterial* EditedMaterial = FindEditedMaterial();
    if (!EditedMaterial || !GEditor)
    {
        return false;
    }

    UAssetEditorSubsystem* AssetEditorSubsystem = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>();
    if (!AssetEditorSubsystem)
    {
        return false;
    }

    IAssetEditorInstance* AssetEditorInstance = AssetEditorSubsystem->FindEditorForAsset(EditedMaterial, false);
    IMaterialEditor* MaterialEditor = static_cast<IMaterialEditor*>(AssetEditorInstance);
    if (!MaterialEditor)
    {
        return false;
    }

    const TSet<UObject*> SelectedNodes = MaterialEditor->GetSelectedNodes();
    for (UObject* SelectedObject : SelectedNodes)
    {
        UMaterialExpression* Expression = Cast<UMaterialExpression>(SelectedObject);
        if (!Expression)
        {
            if (UMaterialGraphNode* GraphNode = Cast<UMaterialGraphNode>(SelectedObject))
            {
                Expression = GraphNode->MaterialExpression;
            }
        }

        if (Expression)
        {
            OutExpressions.Add(Expression);
        }
    }

    OutMaterial = EditedMaterial;
    return OutExpressions.Num() > 0 && OutMaterial != nullptr;
}

bool BuildMaterialContextSnapshot(TSharedPtr<FJsonObject>& OutContext)
{
    OutContext.Reset();
    UMaterial* Material = FindEditedMaterial();
    if (!Material)
    {
        return false;
    }

    OutContext = MakeShared<FJsonObject>();
    OutContext->SetBoolField(TEXT("isError"), false);
    OutContext->SetStringField(TEXT("editorType"), TEXT("material"));
    OutContext->SetStringField(TEXT("provider"), TEXT("material"));
    OutContext->SetStringField(TEXT("assetName"), Material->GetName());
    OutContext->SetStringField(TEXT("assetPath"), Material->GetPathName());
    OutContext->SetStringField(TEXT("assetClass"), Material->GetClass()->GetPathName());
    OutContext->SetStringField(TEXT("status"), TEXT("active"));
    return true;
}

bool BuildMaterialSelectionSnapshot(TSharedPtr<FJsonObject>& OutSelection)
{
    OutSelection.Reset();
    TArray<UMaterialExpression*> SelectedExpressions;
    UMaterial* Material = nullptr;
    if (!CollectSelectedMaterialExpressions(SelectedExpressions, Material))
    {
        return false;
    }

    OutSelection = MakeShared<FJsonObject>();
    OutSelection->SetBoolField(TEXT("isError"), false);
    OutSelection->SetStringField(TEXT("editorType"), TEXT("material"));
    OutSelection->SetStringField(TEXT("provider"), TEXT("material"));
    OutSelection->SetStringField(TEXT("assetPath"), Material->GetPathName());
    OutSelection->SetStringField(TEXT("selectionKind"), TEXT("graph_node"));

    TArray<TSharedPtr<FJsonValue>> Items;
    Items.Reserve(SelectedExpressions.Num());
    for (UMaterialExpression* Expression : SelectedExpressions)
    {
        if (!Expression)
        {
            continue;
        }

        TSharedPtr<FJsonObject> Item = MakeShared<FJsonObject>();
        Item->SetStringField(TEXT("id"), Expression->GetPathName());
        Item->SetStringField(TEXT("name"), Expression->GetName());
        Item->SetStringField(TEXT("class"), Expression->GetClass() ? Expression->GetClass()->GetPathName() : TEXT(""));
        Item->SetStringField(TEXT("path"), Expression->GetPathName());
        Item->SetNumberField(TEXT("nodePosX"), Expression->MaterialExpressionEditorX);
        Item->SetNumberField(TEXT("nodePosY"), Expression->MaterialExpressionEditorY);
        Items.Add(MakeShared<FJsonValueObject>(Item));
    }

    OutSelection->SetArrayField(TEXT("items"), Items);
    OutSelection->SetNumberField(TEXT("count"), Items.Num());
    return true;
}

}

void FLoomleBridgeModule::StartupModule()
{
    PipeServer = MakeShared<FLoomlePipeServer, ESPMode::ThreadSafe>(
        LoomleBridgeConstants::PipeName,
        [this](const FString& RequestLine)
        {
            return HandleRequest(RequestLine);
        });

    if (!PipeServer->Start())
    {
        UE_LOG(LogLoomleBridge, Error, TEXT("Failed to start Loomle pipe server."));
        PipeServer.Reset();
        return;
    }

#if PLATFORM_WINDOWS
    UE_LOG(LogLoomleBridge, Display, TEXT("Loomle bridge started on named pipe \\\\.\\pipe\\%s"), LoomleBridgeConstants::PipeName);
#else
    UE_LOG(LogLoomleBridge, Display, TEXT("Loomle bridge started on unix socket %s"), *FPaths::Combine(FPaths::ProjectIntermediateDir(), TEXT("loomle.sock")));
#endif

    RegisterEditorStreamDelegates();
    bEditorStreamEnabled = true;
    NextLiveSequence = 1;
    LiveLogAppendSinceTrim = 0;
    LiveEventBuffer.Empty();
    bSelectionEventPending = false;
    PendingSelectionCount = 0;
    PendingSelectionSignature.Empty();
    PendingSelectionPaths.Empty();
    LastEmittedSelectionSignature.Empty();
    LastEmittedGraphSelectionSignature.Empty();
    LastSelectionChangeTimeSeconds = 0.0;
    LastActorTransformByPath.Empty();
    LastSceneComponentRelativeTransformByPath.Empty();
    PendingDirtyBlueprintGraphReasons.Empty();
    LastBlueprintGraphSignatureByAssetPath.Empty();
    LastBlueprintGraphNodeCountByAssetPath.Empty();
    LastBlueprintGraphEdgeCountByAssetPath.Empty();
    LastBlueprintGraphNodeTokensByAssetPath.Empty();
    LastBlueprintGraphEdgeTokensByAssetPath.Empty();
    bGraphMutateInProgress = false;

    TSharedPtr<FJsonObject> StartedData = MakeShared<FJsonObject>();
    StartedData->SetStringField(TEXT("reason"), TEXT("loomle_started"));
    StartedData->SetStringField(TEXT("message"), TEXT("live started"));
    SendEditorStreamEvent(TEXT("live_started"), StartedData);
}

void FLoomleBridgeModule::ShutdownModule()
{
    if (bEditorStreamEnabled)
    {
        TSharedPtr<FJsonObject> StoppedData = MakeShared<FJsonObject>();
        StoppedData->SetStringField(TEXT("reason"), TEXT("loomle_stopping"));
        StoppedData->SetStringField(TEXT("message"), TEXT("live stopping"));
        SendEditorStreamEvent(TEXT("live_stopping"), StoppedData);
    }

    UnregisterEditorStreamDelegates();
    bEditorStreamEnabled = false;
    LiveLogAppendSinceTrim = 0;

    if (PipeServer.IsValid())
    {
        PipeServer->StopServer();
        PipeServer.Reset();
    }
}

FString FLoomleBridgeModule::HandleRequest(const FString& RequestLine)
{
    TSharedPtr<FJsonObject> RequestObject;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(RequestLine);

    if (!FJsonSerializer::Deserialize(Reader, RequestObject) || !RequestObject.IsValid())
    {
        return MakeJsonError(MakeShared<FJsonValueNull>(), -32700, TEXT("Parse error"));
    }

    TSharedPtr<FJsonValue> IdValue = RequestObject->TryGetField(TEXT("id"));
    if (!IdValue.IsValid())
    {
        IdValue = MakeShared<FJsonValueNull>();
    }

    FString Method;
    if (!RequestObject->TryGetStringField(TEXT("method"), Method))
    {
        return MakeJsonError(IdValue, -32600, TEXT("Invalid Request: method is required"));
    }

    const TSharedPtr<FJsonObject>* ParamsPtr = nullptr;
    RequestObject->TryGetObjectField(TEXT("params"), ParamsPtr);
    const TSharedPtr<FJsonObject> Params = ParamsPtr ? *ParamsPtr : MakeShared<FJsonObject>();

    if (Method.Equals(TEXT("initialize")))
    {
        return MakeJsonResponse(IdValue, BuildInitializeResult(Params));
    }

    if (Method.Equals(TEXT("notifications/initialized")))
    {
        return FString();
    }

    if (Method.Equals(TEXT("tools/list")))
    {
        return MakeJsonResponse(IdValue, BuildToolsListResult());
    }

    if (Method.Equals(TEXT("tools/call")))
    {
        return MakeJsonResponse(IdValue, BuildToolCallResult(Params));
    }

    return MakeJsonError(IdValue, -32601, FString::Printf(TEXT("Method not found: %s"), *Method));
}

TSharedPtr<FJsonObject> FLoomleBridgeModule::BuildInitializeResult(const TSharedPtr<FJsonObject>&) const
{
    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetStringField(TEXT("protocolVersion"), TEXT("2025-06-18"));

    TSharedPtr<FJsonObject> ServerInfo = MakeShared<FJsonObject>();
    ServerInfo->SetStringField(TEXT("name"), TEXT("Loomle Bridge"));
    ServerInfo->SetStringField(TEXT("version"), TEXT("0.1.0"));
    Result->SetObjectField(TEXT("serverInfo"), ServerInfo);

    TSharedPtr<FJsonObject> Capabilities = MakeShared<FJsonObject>();
    Capabilities->SetObjectField(TEXT("tools"), MakeShared<FJsonObject>());
    Result->SetObjectField(TEXT("capabilities"), Capabilities);

    return Result;
}

TSharedPtr<FJsonObject> FLoomleBridgeModule::BuildToolsListResult() const
{
    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    TArray<TSharedPtr<FJsonValue>> Tools;

    auto MakeTool = [](const FString& Name, const FString& Description, const TSharedPtr<FJsonObject>& InputSchema)
    {
        TSharedPtr<FJsonObject> Tool = MakeShared<FJsonObject>();
        Tool->SetStringField(TEXT("name"), Name);
        Tool->SetStringField(TEXT("description"), Description);
        Tool->SetObjectField(TEXT("inputSchema"), InputSchema);
        return MakeShared<FJsonValueObject>(Tool);
    };

    {
        TSharedPtr<FJsonObject> Schema = MakeShared<FJsonObject>();
        Schema->SetStringField(TEXT("type"), TEXT("object"));
        Schema->SetArrayField(TEXT("required"), TArray<TSharedPtr<FJsonValue>>{});
        Schema->SetObjectField(TEXT("properties"), MakeShared<FJsonObject>());
        Tools.Add(MakeTool(TEXT("context"), TEXT("Get unified UE editor snapshot (context + selection)."), Schema));
    }

    {
        TSharedPtr<FJsonObject> Schema = MakeShared<FJsonObject>();
        Schema->SetStringField(TEXT("type"), TEXT("object"));
        Schema->SetArrayField(TEXT("required"), TArray<TSharedPtr<FJsonValue>>{});
        Schema->SetObjectField(TEXT("properties"), MakeShared<FJsonObject>());
        Tools.Add(MakeTool(TEXT("loomle"), TEXT("Inspect Loomle Bridge health and callable capabilities."), Schema));
    }

    {
        TSharedPtr<FJsonObject> Schema = MakeShared<FJsonObject>();
        Schema->SetStringField(TEXT("type"), TEXT("object"));
        Schema->SetArrayField(TEXT("required"), TArray<TSharedPtr<FJsonValue>>{});
        TSharedPtr<FJsonObject> Properties = MakeShared<FJsonObject>();
        {
            TSharedPtr<FJsonObject> GraphTypeProperty = MakeShared<FJsonObject>();
            GraphTypeProperty->SetStringField(TEXT("type"), TEXT("string"));
            GraphTypeProperty->SetStringField(TEXT("description"), TEXT("Graph type descriptor. Default: blueprint."));
            Properties->SetObjectField(TEXT("graphType"), GraphTypeProperty);
        }
        Schema->SetObjectField(TEXT("properties"), Properties);
        Tools.Add(MakeTool(LoomleBridgeConstants::GraphToolName, TEXT("Return Loomle Graph descriptor (capabilities + schema)."), Schema));
    }

    {
        TSharedPtr<FJsonObject> Schema = MakeShared<FJsonObject>();
        Schema->SetStringField(TEXT("type"), TEXT("object"));
        TArray<TSharedPtr<FJsonValue>> Required;
        Required.Add(MakeShared<FJsonValueString>(TEXT("assetPath")));
        Schema->SetArrayField(TEXT("required"), Required);
        TSharedPtr<FJsonObject> Properties = MakeShared<FJsonObject>();
        {
            TSharedPtr<FJsonObject> GraphTypeProperty = MakeShared<FJsonObject>();
            GraphTypeProperty->SetStringField(TEXT("type"), TEXT("string"));
            Properties->SetObjectField(TEXT("graphType"), GraphTypeProperty);
            TSharedPtr<FJsonObject> AssetPathProperty = MakeShared<FJsonObject>();
            AssetPathProperty->SetStringField(TEXT("type"), TEXT("string"));
            Properties->SetObjectField(TEXT("assetPath"), AssetPathProperty);
        }
        Schema->SetObjectField(TEXT("properties"), Properties);
        Tools.Add(MakeTool(LoomleBridgeConstants::GraphListToolName, TEXT("List readable graph names in a blueprint asset."), Schema));
    }

    {
        TSharedPtr<FJsonObject> Schema = MakeShared<FJsonObject>();
        Schema->SetStringField(TEXT("type"), TEXT("object"));
        TArray<TSharedPtr<FJsonValue>> Required;
        Required.Add(MakeShared<FJsonValueString>(TEXT("assetPath")));
        Required.Add(MakeShared<FJsonValueString>(TEXT("graphName")));
        Schema->SetArrayField(TEXT("required"), Required);
        TSharedPtr<FJsonObject> Properties = MakeShared<FJsonObject>();
        {
            TSharedPtr<FJsonObject> GraphTypeProperty = MakeShared<FJsonObject>();
            GraphTypeProperty->SetStringField(TEXT("type"), TEXT("string"));
            Properties->SetObjectField(TEXT("graphType"), GraphTypeProperty);
            TSharedPtr<FJsonObject> AssetPathProperty = MakeShared<FJsonObject>();
            AssetPathProperty->SetStringField(TEXT("type"), TEXT("string"));
            Properties->SetObjectField(TEXT("assetPath"), AssetPathProperty);
            TSharedPtr<FJsonObject> GraphNameProperty = MakeShared<FJsonObject>();
            GraphNameProperty->SetStringField(TEXT("type"), TEXT("string"));
            Properties->SetObjectField(TEXT("graphName"), GraphNameProperty);
            TSharedPtr<FJsonObject> LimitProperty = MakeShared<FJsonObject>();
            LimitProperty->SetStringField(TEXT("type"), TEXT("number"));
            Properties->SetObjectField(TEXT("limit"), LimitProperty);
            TSharedPtr<FJsonObject> CursorProperty = MakeShared<FJsonObject>();
            CursorProperty->SetStringField(TEXT("type"), TEXT("string"));
            Properties->SetObjectField(TEXT("cursor"), CursorProperty);
            TSharedPtr<FJsonObject> FilterProperty = MakeShared<FJsonObject>();
            FilterProperty->SetStringField(TEXT("type"), TEXT("object"));
            Properties->SetObjectField(TEXT("filter"), FilterProperty);
        }
        Schema->SetObjectField(TEXT("properties"), Properties);
        Tools.Add(MakeTool(LoomleBridgeConstants::GraphQueryToolName, TEXT("Query graph nodes/edges for a graph asset."), Schema));
    }

    {
        TSharedPtr<FJsonObject> Schema = MakeShared<FJsonObject>();
        Schema->SetStringField(TEXT("type"), TEXT("object"));
        Schema->SetArrayField(TEXT("required"), TArray<TSharedPtr<FJsonValue>>{});
        TSharedPtr<FJsonObject> Properties = MakeShared<FJsonObject>();
        {
            TSharedPtr<FJsonObject> GraphTypeProperty = MakeShared<FJsonObject>();
            GraphTypeProperty->SetStringField(TEXT("type"), TEXT("string"));
            Properties->SetObjectField(TEXT("graphType"), GraphTypeProperty);
            TSharedPtr<FJsonObject> AssetPathProperty = MakeShared<FJsonObject>();
            AssetPathProperty->SetStringField(TEXT("type"), TEXT("string"));
            Properties->SetObjectField(TEXT("assetPath"), AssetPathProperty);
            TSharedPtr<FJsonObject> OpsProperty = MakeShared<FJsonObject>();
            OpsProperty->SetStringField(TEXT("type"), TEXT("array"));
            TSharedPtr<FJsonObject> OpsItems = MakeShared<FJsonObject>();
            OpsItems->SetStringField(TEXT("type"), TEXT("object"));
            OpsProperty->SetObjectField(TEXT("items"), OpsItems);
            Properties->SetObjectField(TEXT("ops"), OpsProperty);
            TSharedPtr<FJsonObject> DryRunProperty = MakeShared<FJsonObject>();
            DryRunProperty->SetStringField(TEXT("type"), TEXT("boolean"));
            Properties->SetObjectField(TEXT("dryRun"), DryRunProperty);
        }
        Schema->SetObjectField(TEXT("properties"), Properties);
        Tools.Add(MakeTool(LoomleBridgeConstants::GraphMutateToolName, TEXT("Apply graph mutation operations to a graph asset."), Schema));
    }

    {
        TSharedPtr<FJsonObject> Schema = MakeShared<FJsonObject>();
        Schema->SetStringField(TEXT("type"), TEXT("object"));
        Schema->SetArrayField(TEXT("required"), TArray<TSharedPtr<FJsonValue>>{});
        TSharedPtr<FJsonObject> Properties = MakeShared<FJsonObject>();
        {
            TSharedPtr<FJsonObject> CursorProperty = MakeShared<FJsonObject>();
            CursorProperty->SetStringField(TEXT("type"), TEXT("number"));
            Properties->SetObjectField(TEXT("cursor"), CursorProperty);
            TSharedPtr<FJsonObject> LimitProperty = MakeShared<FJsonObject>();
            LimitProperty->SetStringField(TEXT("type"), TEXT("number"));
            Properties->SetObjectField(TEXT("limit"), LimitProperty);
            TSharedPtr<FJsonObject> GraphTypeProperty = MakeShared<FJsonObject>();
            GraphTypeProperty->SetStringField(TEXT("type"), TEXT("string"));
            Properties->SetObjectField(TEXT("graphType"), GraphTypeProperty);
            TSharedPtr<FJsonObject> AssetPathProperty = MakeShared<FJsonObject>();
            AssetPathProperty->SetStringField(TEXT("type"), TEXT("string"));
            Properties->SetObjectField(TEXT("assetPath"), AssetPathProperty);
        }
        Schema->SetObjectField(TEXT("properties"), Properties);
        Tools.Add(MakeTool(LoomleBridgeConstants::GraphWatchToolName, TEXT("Watch graph-scoped events incrementally."), Schema));
    }

    {
        TSharedPtr<FJsonObject> Schema = MakeShared<FJsonObject>();
        Schema->SetStringField(TEXT("type"), TEXT("object"));
        Schema->SetArrayField(TEXT("required"), TArray<TSharedPtr<FJsonValue>>{});

        TSharedPtr<FJsonObject> Properties = MakeShared<FJsonObject>();
        {
            TSharedPtr<FJsonObject> CursorProperty = MakeShared<FJsonObject>();
            CursorProperty->SetStringField(TEXT("type"), TEXT("number"));
            CursorProperty->SetStringField(TEXT("description"), TEXT("Last consumed live sequence. Return events after this cursor."));
            Properties->SetObjectField(TEXT("cursor"), CursorProperty);

            TSharedPtr<FJsonObject> LimitProperty = MakeShared<FJsonObject>();
            LimitProperty->SetStringField(TEXT("type"), TEXT("number"));
            LimitProperty->SetStringField(TEXT("description"), TEXT("Maximum events to return, default 20, max 100."));
            Properties->SetObjectField(TEXT("limit"), LimitProperty);
        }

        Schema->SetObjectField(TEXT("properties"), Properties);
        Tools.Add(MakeTool(LoomleBridgeConstants::LiveToolName, TEXT("Pull live editor events incrementally (auto-managed with bridge lifecycle)."), Schema));
    }

    {
        TSharedPtr<FJsonObject> Schema = MakeShared<FJsonObject>();
        Schema->SetStringField(TEXT("type"), TEXT("object"));
        TArray<TSharedPtr<FJsonValue>> Required;
        Required.Add(MakeShared<FJsonValueString>(TEXT("code")));
        Schema->SetArrayField(TEXT("required"), Required);

        TSharedPtr<FJsonObject> Properties = MakeShared<FJsonObject>();
        {
            TSharedPtr<FJsonObject> CodeProperty = MakeShared<FJsonObject>();
            CodeProperty->SetStringField(TEXT("type"), TEXT("string"));
            CodeProperty->SetStringField(TEXT("description"), TEXT("Inline Python code executed by UE PythonScriptPlugin."));
            Properties->SetObjectField(TEXT("code"), CodeProperty);

            TSharedPtr<FJsonObject> ModeProperty = MakeShared<FJsonObject>();
            ModeProperty->SetStringField(TEXT("type"), TEXT("string"));
            ModeProperty->SetStringField(TEXT("description"), TEXT("Execution mode: exec (default) or eval."));
            TArray<TSharedPtr<FJsonValue>> EnumValues;
            EnumValues.Add(MakeShared<FJsonValueString>(TEXT("exec")));
            EnumValues.Add(MakeShared<FJsonValueString>(TEXT("eval")));
            ModeProperty->SetArrayField(TEXT("enum"), EnumValues);
            Properties->SetObjectField(TEXT("mode"), ModeProperty);
        }

        Schema->SetObjectField(TEXT("properties"), Properties);
        Tools.Add(MakeTool(LoomleBridgeConstants::ExecuteToolName, TEXT("Execute inline Python code inside UE Editor."), Schema));
    }

    Result->SetArrayField(TEXT("tools"), Tools);
    return Result;
}

TSharedPtr<FJsonObject> FLoomleBridgeModule::BuildToolCallResult(const TSharedPtr<FJsonObject>& Params)
{
    FString Name;
    if (!Params->TryGetStringField(TEXT("name"), Name))
    {
        TSharedPtr<FJsonObject> Error = MakeShared<FJsonObject>();
        Error->SetBoolField(TEXT("isError"), true);
        Error->SetStringField(TEXT("message"), TEXT("Missing tool name."));
        return Error;
    }

    const TSharedPtr<FJsonObject>* ArgumentsPtr = nullptr;
    Params->TryGetObjectField(TEXT("arguments"), ArgumentsPtr);
    TSharedPtr<FJsonObject> Arguments = ArgumentsPtr ? *ArgumentsPtr : MakeShared<FJsonObject>();

    TSharedPtr<FJsonObject> Payload;
    bool bIsError = false;

    if (Name.Equals(TEXT("context")))
    {
        Payload = BuildGetContextToolResult(Arguments);
    }
    else if (Name.Equals(TEXT("loomle")))
    {
        Payload = BuildLoomleToolResult();
        bIsError = Payload->GetBoolField(TEXT("isError"));
    }
    else if (Name.Equals(LoomleBridgeConstants::LiveToolName))
    {
        Payload = BuildLiveToolResult(Arguments);
        bIsError = Payload->GetBoolField(TEXT("isError"));
    }
    else if (Name.Equals(LoomleBridgeConstants::GraphToolName))
    {
        Payload = BuildGraphToolResult(Arguments);
        bIsError = Payload->GetBoolField(TEXT("isError"));
    }
    else if (Name.Equals(LoomleBridgeConstants::GraphListToolName))
    {
        Payload = BuildGraphListToolResult(Arguments);
        bIsError = Payload->GetBoolField(TEXT("isError"));
    }
    else if (Name.Equals(LoomleBridgeConstants::GraphQueryToolName))
    {
        Payload = BuildGraphQueryToolResult(Arguments);
        bIsError = Payload->GetBoolField(TEXT("isError"));
    }
    else if (Name.Equals(LoomleBridgeConstants::GraphMutateToolName))
    {
        Payload = BuildGraphMutateToolResult(Arguments);
        bIsError = Payload->GetBoolField(TEXT("isError"));
    }
    else if (Name.Equals(LoomleBridgeConstants::GraphWatchToolName))
    {
        Payload = BuildGraphWatchToolResult(Arguments);
        bIsError = Payload->GetBoolField(TEXT("isError"));
    }
    else if (Name.Equals(LoomleBridgeConstants::ExecuteToolName))
    {
        Payload = BuildExecutePythonToolResult(Arguments);
        bIsError = Payload->GetBoolField(TEXT("isError"));
    }
    else
    {
        bIsError = true;
        Payload = MakeShared<FJsonObject>();
        Payload->SetStringField(TEXT("message"), FString::Printf(TEXT("Unknown tool: %s"), *Name));
    }

    FString PayloadText;
    TSharedRef<FCondensedJsonWriter> Writer = TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&PayloadText);
    FJsonSerializer::Serialize(Payload.ToSharedRef(), Writer);

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    TArray<TSharedPtr<FJsonValue>> Content;

    TSharedPtr<FJsonObject> TextContent = MakeShared<FJsonObject>();
    TextContent->SetStringField(TEXT("type"), TEXT("text"));
    TextContent->SetStringField(TEXT("text"), PayloadText);
    Content.Add(MakeShared<FJsonValueObject>(TextContent));

    Result->SetArrayField(TEXT("content"), Content);
    if (bIsError)
    {
        Result->SetBoolField(TEXT("isError"), true);
    }

    return Result;
}

TSharedPtr<FJsonObject> FLoomleBridgeModule::BuildGetContextToolResult(const TSharedPtr<FJsonObject>& Arguments) const
{
    (void)Arguments;

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetBoolField(TEXT("isError"), false);
    Result->SetStringField(TEXT("timestamp"), FDateTime::UtcNow().ToIso8601());

    TSharedPtr<FJsonObject> Runtime = MakeShared<FJsonObject>();
    Runtime->SetStringField(TEXT("projectName"), FApp::GetProjectName());
    Runtime->SetStringField(TEXT("projectFilePath"), FPaths::GetProjectFilePath());
    Runtime->SetStringField(TEXT("engineVersion"), FEngineVersion::Current().ToString(EVersionComponent::Patch));
    UWorld* EditorWorld = nullptr;
    if (GEditor)
    {
        EditorWorld = GEditor->GetEditorWorldContext().World();
        Runtime->SetBoolField(TEXT("isPIE"), GEditor->IsPlayingSessionInEditor());
    }
    else
    {
        Runtime->SetBoolField(TEXT("isPIE"), false);
    }
    Runtime->SetStringField(TEXT("editorWorld"), EditorWorld ? EditorWorld->GetName() : TEXT(""));
    Result->SetObjectField(TEXT("runtime"), Runtime);
    Result->SetObjectField(TEXT("activeWindow"), BuildActiveWindowJson());

    TSharedPtr<FJsonObject> Context = MakeShared<FJsonObject>();
    Context->SetStringField(TEXT("source"), TEXT("unified"));
    TSharedPtr<FJsonObject> BlueprintContext;
    if (BuildBlueprintContextSnapshot(BlueprintContext) && BlueprintContext.IsValid())
    {
        Context = BlueprintContext;
        Context->SetStringField(TEXT("source"), TEXT("unified"));
    }
    else
    {
        TSharedPtr<FJsonObject> MaterialContext;
        if (BuildMaterialContextSnapshot(MaterialContext) && MaterialContext.IsValid())
        {
            Context = MaterialContext;
            Context->SetStringField(TEXT("source"), TEXT("unified"));
        }
    }
    Result->SetObjectField(TEXT("context"), Context);

    TSharedPtr<FJsonObject> Selection = BuildSelectionTransformToolResult();
    Result->SetObjectField(TEXT("selection"), Selection);

    return Result;
}

TSharedPtr<FJsonObject> FLoomleBridgeModule::BuildLoomleToolResult() const
{
    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetBoolField(TEXT("isError"), false);

    const bool bBridgeRunning = PipeServer.IsValid();
    const bool bLiveRunning = bEditorStreamEnabled;

    IPythonScriptPlugin* PythonScriptPlugin = IPythonScriptPlugin::Get();
    const bool bPythonModuleLoaded = PythonScriptPlugin != nullptr;
    const bool bPythonReady = bPythonModuleLoaded && PythonScriptPlugin->IsPythonInitialized();

    TSharedPtr<FJsonObject> Status = MakeShared<FJsonObject>();
    Status->SetStringField(TEXT("serverName"), TEXT("Loomle Bridge"));
    Status->SetStringField(TEXT("serverVersion"), TEXT("0.1.0"));
    Status->SetStringField(TEXT("protocolVersion"), TEXT("2025-06-18"));
    Status->SetBoolField(TEXT("bridgeRunning"), bBridgeRunning);
    Status->SetBoolField(TEXT("liveRunning"), bLiveRunning);
    Status->SetBoolField(TEXT("pythonModuleLoaded"), bPythonModuleLoaded);
    Status->SetBoolField(TEXT("pythonReady"), bPythonReady);
#if PLATFORM_WINDOWS
    Status->SetStringField(TEXT("transport"), TEXT("\\\\.\\pipe\\loomle"));
#else
    Status->SetStringField(TEXT("transport"), FPaths::Combine(FPaths::ProjectIntermediateDir(), TEXT("loomle.sock")));
#endif
    Result->SetObjectField(TEXT("status"), Status);

    auto MakeCapability = [](const FString& Name, const FString& Summary, bool bReady, const FString& Reason)
    {
        TSharedPtr<FJsonObject> Capability = MakeShared<FJsonObject>();
        Capability->SetStringField(TEXT("name"), Name);
        Capability->SetStringField(TEXT("summary"), Summary);
        Capability->SetBoolField(TEXT("ready"), bReady);
        if (!Reason.IsEmpty())
        {
            Capability->SetStringField(TEXT("reason"), Reason);
        }
        return MakeShared<FJsonValueObject>(Capability);
    };

    TArray<TSharedPtr<FJsonValue>> Capabilities;
    Capabilities.Add(MakeCapability(TEXT("loomle"), TEXT("Bridge health and capabilities."), bBridgeRunning, bBridgeRunning ? TEXT("") : TEXT("Bridge server is not running.")));
    Capabilities.Add(MakeCapability(TEXT("context"), TEXT("Current editor context."), bBridgeRunning, bBridgeRunning ? TEXT("") : TEXT("Bridge server is not running.")));
    Capabilities.Add(MakeCapability(TEXT("live"), TEXT("Incremental live event feed by cursor."), bBridgeRunning && bLiveRunning, bLiveRunning ? TEXT("") : TEXT("Live stream is not running. Restart Unreal Editor.")));
    Capabilities.Add(MakeCapability(TEXT("graph"), TEXT("Graph descriptor (capabilities + schema)."), bBridgeRunning, bBridgeRunning ? TEXT("") : TEXT("Bridge server is not running.")));
    Capabilities.Add(MakeCapability(TEXT("graph.list"), TEXT("List readable graphs in a graph asset."), bBridgeRunning, bBridgeRunning ? TEXT("") : TEXT("Bridge server is not running.")));
    Capabilities.Add(MakeCapability(TEXT("graph.query"), TEXT("Graph query API."), bBridgeRunning, bBridgeRunning ? TEXT("") : TEXT("Bridge server is not running.")));
    Capabilities.Add(MakeCapability(TEXT("graph.mutate"), TEXT("Graph mutate API."), bBridgeRunning, bBridgeRunning ? TEXT("") : TEXT("Bridge server is not running.")));
    Capabilities.Add(MakeCapability(TEXT("graph.watch"), TEXT("Graph watch API."), bBridgeRunning && bLiveRunning, bLiveRunning ? TEXT("") : TEXT("Live stream is not running. Restart Unreal Editor.")));
    Capabilities.Add(MakeCapability(TEXT("execute"), TEXT("Execute Python in editor."), bPythonReady, bPythonReady ? TEXT("") : TEXT("Python runtime is not initialized yet.")));
    Result->SetArrayField(TEXT("capabilities"), Capabilities);

    Result->SetStringField(
        TEXT("message"),
        bBridgeRunning ? TEXT("Loomle Bridge is running.") : TEXT("Loomle Bridge is not running. Restart Unreal Editor."));
    return Result;
}

TSharedPtr<FJsonObject> FLoomleBridgeModule::BuildEventPullResult(
    const TSharedPtr<FJsonObject>& Arguments,
    const FString& ScopeFilter,
    const FString& AssetPathFilter,
    bool bFilterLifecycle) const
{
    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetBoolField(TEXT("isError"), false);
    Result->SetBoolField(TEXT("running"), bEditorStreamEnabled);

    if (!bEditorStreamEnabled)
    {
        Result->SetBoolField(TEXT("isError"), true);
        Result->SetStringField(TEXT("message"), TEXT("Live stream is not running. Please restart Unreal Editor to recover Loomle Bridge."));
        return Result;
    }

    int64 Cursor = 0;
    double CursorNumber = 0.0;
    if (Arguments->TryGetNumberField(TEXT("cursor"), CursorNumber))
    {
        Cursor = static_cast<int64>(CursorNumber);
    }
    if (Cursor < 0)
    {
        Result->SetBoolField(TEXT("isError"), true);
        Result->SetStringField(TEXT("message"), TEXT("arguments.cursor must be >= 0."));
        return Result;
    }

    int32 Limit = 20;
    double LimitNumber = 0.0;
    if (Arguments->TryGetNumberField(TEXT("limit"), LimitNumber))
    {
        Limit = FMath::Clamp(static_cast<int32>(LimitNumber), 1, LoomleBridgeConstants::MaxLiveLogRecords);
    }

    TArray<TSharedPtr<FJsonValue>> Events;
    int64 NextCursor = Cursor;
    bool bDropped = false;
    bool bServedFromFile = false;

    int64 EarliestBufferSeq = NextLiveSequence;
    int64 LatestBufferSeq = Cursor;
    if (LiveEventBuffer.Num() > 0)
    {
        int64 Seq = 0;
        TSharedPtr<FJsonObject> Parsed;
        if (ParseLiveEventLine(LiveEventBuffer[0], Seq, Parsed))
        {
            EarliestBufferSeq = Seq;
        }
        if (ParseLiveEventLine(LiveEventBuffer.Last(), Seq, Parsed))
        {
            LatestBufferSeq = Seq;
        }
    }

    for (const FString& Line : LiveEventBuffer)
    {
        if (Events.Num() >= Limit)
        {
            break;
        }

        int64 Seq = 0;
        TSharedPtr<FJsonObject> EventObject;
        if (!ParseLiveEventLine(Line, Seq, EventObject) || !EventObject.IsValid())
        {
            continue;
        }
        if (Seq <= Cursor)
        {
            continue;
        }

        NextCursor = FMath::Max(NextCursor, Seq);
        if (bFilterLifecycle && !ShouldReturnInLivePull(EventObject))
        {
            continue;
        }
        if (!ShouldIncludeEventForScope(EventObject, ScopeFilter))
        {
            continue;
        }
        if (!ShouldIncludeEventForAsset(EventObject, AssetPathFilter))
        {
            continue;
        }

        Events.Add(MakeShared<FJsonValueObject>(EventObject));
    }

    if (Events.Num() == 0 && Cursor < EarliestBufferSeq - 1)
    {
        const FString StreamLogPath = FPaths::Combine(FPaths::ProjectDir(), TEXT("Loomle"), TEXT("runtime"), LoomleBridgeConstants::LiveLogFileName);
        TArray<FString> LogLines;
        FFileHelper::LoadFileToStringArray(LogLines, *StreamLogPath);
        bServedFromFile = true;

        int64 EarliestFileSeq = MAX_int64;
        int64 LatestFileSeq = 0;
        for (const FString& Line : LogLines)
        {
            int64 Seq = 0;
            TSharedPtr<FJsonObject> EventObject;
            if (!ParseLiveEventLine(Line, Seq, EventObject) || !EventObject.IsValid())
            {
                continue;
            }
            EarliestFileSeq = FMath::Min(EarliestFileSeq, Seq);
            LatestFileSeq = FMath::Max(LatestFileSeq, Seq);

            if (Seq <= Cursor || Events.Num() >= Limit)
            {
                continue;
            }

            NextCursor = FMath::Max(NextCursor, Seq);
            if (bFilterLifecycle && !ShouldReturnInLivePull(EventObject))
            {
                continue;
            }
            if (!ShouldIncludeEventForScope(EventObject, ScopeFilter))
            {
                continue;
            }
            if (!ShouldIncludeEventForAsset(EventObject, AssetPathFilter))
            {
                continue;
            }

            Events.Add(MakeShared<FJsonValueObject>(EventObject));
        }

        if (EarliestFileSeq != MAX_int64 && Cursor < EarliestFileSeq - 1)
        {
            bDropped = true;
        }
        if (LatestFileSeq > 0)
        {
            NextCursor = FMath::Max(NextCursor, LatestFileSeq);
        }
    }

    Result->SetNumberField(TEXT("cursor"), static_cast<double>(Cursor));
    Result->SetNumberField(TEXT("nextCursor"), static_cast<double>(NextCursor));
    Result->SetBoolField(TEXT("dropped"), bDropped);
    Result->SetStringField(TEXT("source"), bServedFromFile ? TEXT("file") : TEXT("memory"));
    Result->SetArrayField(TEXT("events"), Events);
    Result->SetNumberField(TEXT("count"), Events.Num());

    if (Events.Num() == 0 && Cursor < LatestBufferSeq)
    {
        Result->SetStringField(TEXT("message"), TEXT("No new events."));
    }

    return Result;
}

TSharedPtr<FJsonObject> FLoomleBridgeModule::BuildLiveToolResult(const TSharedPtr<FJsonObject>& Arguments) const
{
    return BuildEventPullResult(Arguments, TEXT(""), TEXT(""), true);
}

TSharedPtr<FJsonObject> FLoomleBridgeModule::BuildGraphToolResult(const TSharedPtr<FJsonObject>& Arguments) const
{
    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetBoolField(TEXT("isError"), false);

    FString GraphType = TEXT("blueprint");
    Arguments->TryGetStringField(TEXT("graphType"), GraphType);
    GraphType = GraphType.ToLower();
    if (!GraphType.Equals(TEXT("blueprint")))
    {
        Result->SetBoolField(TEXT("isError"), true);
        Result->SetStringField(TEXT("code"), TEXT("UNSUPPORTED_GRAPH_TYPE"));
        Result->SetStringField(TEXT("message"), TEXT("Only blueprint graphType is currently supported."));
        return Result;
    }

    Result->SetStringField(TEXT("version"), TEXT("1.0"));
    Result->SetStringField(TEXT("graphType"), GraphType);

    TSharedPtr<FJsonObject> Features = MakeShared<FJsonObject>();
    Features->SetBoolField(TEXT("list"), true);
    Features->SetBoolField(TEXT("query"), true);
    Features->SetBoolField(TEXT("mutate"), true);
    Features->SetBoolField(TEXT("watch"), true);
    Features->SetBoolField(TEXT("revision"), true);
    Features->SetBoolField(TEXT("dryRun"), true);
    Features->SetBoolField(TEXT("transactions"), true);
    Result->SetObjectField(TEXT("features"), Features);

    TSharedPtr<FJsonObject> Limits = MakeShared<FJsonObject>();
    Limits->SetNumberField(TEXT("defaultLimit"), 200);
    Limits->SetNumberField(TEXT("maxLimit"), 1000);
    Limits->SetNumberField(TEXT("maxOpsPerMutate"), 200);
    Result->SetObjectField(TEXT("limits"), Limits);

    auto ToStringArray = [](std::initializer_list<const TCHAR*> Values)
    {
        TArray<TSharedPtr<FJsonValue>> Out;
        for (const TCHAR* Value : Values)
        {
            Out.Add(MakeShared<FJsonValueString>(Value));
        }
        return Out;
    };

    Result->SetArrayField(TEXT("nodeCoreFields"), ToStringArray({TEXT("id"), TEXT("nodeClassPath"), TEXT("title"), TEXT("graphName"), TEXT("position"), TEXT("enabled"), TEXT("pins"), TEXT("memberReference"), TEXT("functionReference"), TEXT("k2Extensions")}));
    Result->SetArrayField(TEXT("pinCoreFields"), ToStringArray({TEXT("name"), TEXT("direction"), TEXT("type"), TEXT("default"), TEXT("links")}));
    Result->SetArrayField(TEXT("nodeExtensions"), ToStringArray({TEXT("cast"), TEXT("macro"), TEXT("comment"), TEXT("timeline")}));
    Result->SetArrayField(TEXT("ops"), ToStringArray({
        TEXT("addNode.byClass"), TEXT("addNode.byAction"),
        TEXT("connectPins"), TEXT("disconnectPins"), TEXT("breakPinLinks"),
        TEXT("setPinDefault"), TEXT("removeNode"), TEXT("moveNode"),
        TEXT("compile"), TEXT("runScript")
    }));
    TSharedPtr<FJsonObject> Extensions = MakeShared<FJsonObject>();
    Extensions->SetBoolField(TEXT("scriptOp"), true);
    Extensions->SetArrayField(TEXT("scriptMode"), ToStringArray({TEXT("inlineCode"), TEXT("scriptId")}));
    Extensions->SetStringField(TEXT("scriptInlineDefault"), TEXT("enabled"));
    Result->SetObjectField(TEXT("extensions"), Extensions);
    return Result;
}

TSharedPtr<FJsonObject> FLoomleBridgeModule::BuildGraphListToolResult(const TSharedPtr<FJsonObject>& Arguments) const
{
    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetBoolField(TEXT("isError"), false);

    FString GraphType = TEXT("blueprint");
    Arguments->TryGetStringField(TEXT("graphType"), GraphType);
    GraphType = GraphType.ToLower();
    if (!GraphType.Equals(TEXT("blueprint")))
    {
        Result->SetBoolField(TEXT("isError"), true);
        Result->SetStringField(TEXT("code"), TEXT("UNSUPPORTED_GRAPH_TYPE"));
        Result->SetStringField(TEXT("message"), TEXT("Only blueprint graphType is currently supported."));
        return Result;
    }

    FString AssetPath;
    if (!Arguments->TryGetStringField(TEXT("assetPath"), AssetPath) || AssetPath.IsEmpty())
    {
        Result->SetBoolField(TEXT("isError"), true);
        Result->SetStringField(TEXT("code"), TEXT("INVALID_ARGUMENT"));
        Result->SetStringField(TEXT("message"), TEXT("arguments.assetPath is required."));
        return Result;
    }

    FString GraphsJson;
    FString Error;
    if (!ULoomleBlueprintAdapter::ListBlueprintGraphs(AssetPath, GraphsJson, Error))
    {
        Result->SetBoolField(TEXT("isError"), true);
        Result->SetStringField(TEXT("code"), TEXT("INTERNAL_ERROR"));
        Result->SetStringField(TEXT("message"), Error.IsEmpty() ? TEXT("graph.list failed") : Error);
        return Result;
    }

    TArray<TSharedPtr<FJsonValue>> Graphs;
    {
        const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(GraphsJson);
        FJsonSerializer::Deserialize(Reader, Graphs);
    }

    Result->SetStringField(TEXT("graphType"), GraphType);
    Result->SetStringField(TEXT("assetPath"), AssetPath);
    Result->SetArrayField(TEXT("graphs"), Graphs);
    Result->SetArrayField(TEXT("diagnostics"), TArray<TSharedPtr<FJsonValue>>{});
    return Result;
}

TSharedPtr<FJsonObject> FLoomleBridgeModule::BuildGraphQueryToolResult(const TSharedPtr<FJsonObject>& Arguments) const
{
    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetBoolField(TEXT("isError"), false);

    FString GraphType = TEXT("blueprint");
    Arguments->TryGetStringField(TEXT("graphType"), GraphType);
    GraphType = GraphType.ToLower();
    if (!GraphType.Equals(TEXT("blueprint")))
    {
        Result->SetBoolField(TEXT("isError"), true);
        Result->SetStringField(TEXT("code"), TEXT("UNSUPPORTED_GRAPH_TYPE"));
        Result->SetStringField(TEXT("message"), TEXT("Only blueprint graphType is currently supported."));
        return Result;
    }

    FString AssetPath;
    if (!Arguments->TryGetStringField(TEXT("assetPath"), AssetPath) || AssetPath.IsEmpty())
    {
        Result->SetBoolField(TEXT("isError"), true);
        Result->SetStringField(TEXT("code"), TEXT("INVALID_ARGUMENT"));
        Result->SetStringField(TEXT("message"), TEXT("arguments.assetPath is required."));
        return Result;
    }

    FString GraphName;
    if (!Arguments->TryGetStringField(TEXT("graphName"), GraphName) || GraphName.IsEmpty())
    {
        Result->SetBoolField(TEXT("isError"), true);
        Result->SetStringField(TEXT("code"), TEXT("INVALID_ARGUMENT"));
        Result->SetStringField(TEXT("message"), TEXT("arguments.graphName is required."));
        return Result;
    }

    TArray<FString> FilterClasses;
    const TSharedPtr<FJsonObject>* FilterObj = nullptr;
    if (Arguments->TryGetObjectField(TEXT("filter"), FilterObj) && FilterObj && (*FilterObj).IsValid())
    {
        const TArray<TSharedPtr<FJsonValue>>* NodeClasses = nullptr;
        if ((*FilterObj)->TryGetArrayField(TEXT("nodeClasses"), NodeClasses) && NodeClasses)
        {
            for (const TSharedPtr<FJsonValue>& NodeClassValue : *NodeClasses)
            {
                FString NodeClass;
                if (NodeClassValue.IsValid() && NodeClassValue->TryGetString(NodeClass) && !NodeClass.IsEmpty())
                {
                    FilterClasses.Add(NodeClass);
                }
            }
        }
    }

    FString NodesJson;
    FString Error;
    const bool bOk = ULoomleBlueprintAdapter::ListGraphNodes(AssetPath, GraphName, NodesJson, Error);

    if (!bOk)
    {
        Result->SetBoolField(TEXT("isError"), true);
        Result->SetStringField(TEXT("code"), Error.Contains(TEXT("Graph not found")) ? TEXT("GRAPH_NOT_FOUND") : TEXT("INTERNAL_ERROR"));
        Result->SetStringField(TEXT("message"), Error.IsEmpty() ? TEXT("graph.query failed") : Error);
        return Result;
    }

    TArray<TSharedPtr<FJsonValue>> Nodes;
    {
        TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(NodesJson);
        FJsonSerializer::Deserialize(Reader, Nodes);
    }

    int32 Limit = 200;
    double LimitNumber = 0.0;
    if (Arguments->TryGetNumberField(TEXT("limit"), LimitNumber))
    {
        Limit = FMath::Clamp(static_cast<int32>(LimitNumber), 1, 1000);
    }

    TArray<TSharedPtr<FJsonValue>> SnapshotNodes;
    TArray<TSharedPtr<FJsonValue>> Edges;
    SnapshotNodes.Reserve(FMath::Min(Limit, Nodes.Num()));
    TArray<FString> SignatureNodeTokens;
    TArray<FString> SignatureEdgeTokens;

    int32 AddedCount = 0;
    for (int32 Index = 0; Index < Nodes.Num(); ++Index)
    {
        const TSharedPtr<FJsonValue>& NodeValue = Nodes[Index];
        const TSharedPtr<FJsonObject>* NodeObj = nullptr;
        if (!NodeValue.IsValid() || !NodeValue->TryGetObject(NodeObj) || !NodeObj || !(*NodeObj).IsValid())
        {
            continue;
        }

        if (FilterClasses.Num() > 0)
        {
            FString NodeClassPath;
            (*NodeObj)->TryGetStringField(TEXT("nodeClassPath"), NodeClassPath);
            if (NodeClassPath.IsEmpty())
            {
                (*NodeObj)->TryGetStringField(TEXT("classPath"), NodeClassPath);
            }
            bool bClassMatched = false;
            for (const FString& FilterClass : FilterClasses)
            {
                if (NodeClassPath.Equals(FilterClass))
                {
                    bClassMatched = true;
                    break;
                }
            }
            if (!bClassMatched)
            {
                continue;
            }
        }

        if (AddedCount >= Limit)
        {
            break;
        }
        ++AddedCount;
        SnapshotNodes.Add(NodeValue);

        FString FromNodeId;
        (*NodeObj)->TryGetStringField(TEXT("guid"), FromNodeId);
        SignatureNodeTokens.Add(FromNodeId);
        const TArray<TSharedPtr<FJsonValue>>* Pins = nullptr;
        if (!(*NodeObj)->TryGetArrayField(TEXT("pins"), Pins) || !Pins)
        {
            continue;
        }

        for (const TSharedPtr<FJsonValue>& PinValue : *Pins)
        {
            const TSharedPtr<FJsonObject>* PinObj = nullptr;
            if (!PinValue.IsValid() || !PinValue->TryGetObject(PinObj) || !PinObj || !(*PinObj).IsValid())
            {
                continue;
            }

            FString FromPin;
            (*PinObj)->TryGetStringField(TEXT("name"), FromPin);

            const TArray<TSharedPtr<FJsonValue>>* Linked = nullptr;
            if (!(*PinObj)->TryGetArrayField(TEXT("linkedTo"), Linked) || !Linked)
            {
                continue;
            }

            for (const TSharedPtr<FJsonValue>& LinkValue : *Linked)
            {
                const TSharedPtr<FJsonObject>* LinkObj = nullptr;
                if (!LinkValue.IsValid() || !LinkValue->TryGetObject(LinkObj) || !LinkObj || !(*LinkObj).IsValid())
                {
                    continue;
                }

                FString ToNodeId;
                FString ToPin;
                (*LinkObj)->TryGetStringField(TEXT("nodeGuid"), ToNodeId);
                (*LinkObj)->TryGetStringField(TEXT("pin"), ToPin);
                if (ToNodeId.IsEmpty() || ToPin.IsEmpty())
                {
                    continue;
                }

                TSharedPtr<FJsonObject> Edge = MakeShared<FJsonObject>();
                Edge->SetStringField(TEXT("fromNodeId"), FromNodeId);
                Edge->SetStringField(TEXT("fromPin"), FromPin);
                Edge->SetStringField(TEXT("toNodeId"), ToNodeId);
                Edge->SetStringField(TEXT("toPin"), ToPin);
                Edges.Add(MakeShared<FJsonValueObject>(Edge));
                SignatureEdgeTokens.Add(FromNodeId + TEXT("|") + FromPin + TEXT("->") + ToNodeId + TEXT("|") + ToPin);
            }
        }
    }

    Algo::Sort(SignatureNodeTokens);
    Algo::Sort(SignatureEdgeTokens);
    const FString Signature = FString::Join(SignatureNodeTokens, TEXT(";")) + TEXT("#") + FString::Join(SignatureEdgeTokens, TEXT(";"));
    const FString Revision = FString::Printf(TEXT("bp:%08x"), GetTypeHash(AssetPath + TEXT("|") + GraphName + TEXT("|") + Signature));

    TSharedPtr<FJsonObject> Snapshot = MakeShared<FJsonObject>();
    Snapshot->SetStringField(TEXT("signature"), Signature);
    Snapshot->SetArrayField(TEXT("nodes"), SnapshotNodes);
    Snapshot->SetArrayField(TEXT("edges"), Edges);

    Result->SetStringField(TEXT("graphType"), GraphType);
    Result->SetStringField(TEXT("assetPath"), AssetPath);
    Result->SetStringField(TEXT("graphName"), GraphName);
    Result->SetStringField(TEXT("revision"), Revision);
    Result->SetObjectField(TEXT("semanticSnapshot"), Snapshot);
    Result->SetStringField(TEXT("nextCursor"), TEXT(""));

    TSharedPtr<FJsonObject> Meta = MakeShared<FJsonObject>();
    Meta->SetNumberField(TEXT("totalNodes"), Nodes.Num());
    Meta->SetNumberField(TEXT("returnedNodes"), SnapshotNodes.Num());
    Meta->SetNumberField(TEXT("totalEdges"), Edges.Num());
    Meta->SetNumberField(TEXT("returnedEdges"), Edges.Num());
    Meta->SetBoolField(TEXT("truncated"), AddedCount < Nodes.Num() && SnapshotNodes.Num() >= Limit);
    Result->SetObjectField(TEXT("meta"), Meta);
    Result->SetArrayField(TEXT("diagnostics"), TArray<TSharedPtr<FJsonValue>>{});
    return Result;
}

TSharedPtr<FJsonObject> FLoomleBridgeModule::BuildGraphMutateToolResult(const TSharedPtr<FJsonObject>& Arguments)
{
    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetBoolField(TEXT("isError"), false);

    FString GraphType = TEXT("blueprint");
    Arguments->TryGetStringField(TEXT("graphType"), GraphType);
    GraphType = GraphType.ToLower();
    if (!GraphType.Equals(TEXT("blueprint")))
    {
        Result->SetBoolField(TEXT("isError"), true);
        Result->SetStringField(TEXT("code"), TEXT("UNSUPPORTED_GRAPH_TYPE"));
        Result->SetStringField(TEXT("message"), TEXT("Only blueprint graphType is currently supported."));
        return Result;
    }

    FString AssetPath;
    if (!Arguments->TryGetStringField(TEXT("assetPath"), AssetPath) || AssetPath.IsEmpty())
    {
        Result->SetBoolField(TEXT("isError"), true);
        Result->SetStringField(TEXT("code"), TEXT("INVALID_ARGUMENT"));
        Result->SetStringField(TEXT("message"), TEXT("arguments.assetPath is required."));
        return Result;
    }

    FString GraphName = TEXT("EventGraph");
    Arguments->TryGetStringField(TEXT("graphName"), GraphName);

    bool bDryRun = false;
    Arguments->TryGetBoolField(TEXT("dryRun"), bDryRun);

    bool bStopOnError = true;
    bool bContinueOnError = false;
    Arguments->TryGetBoolField(TEXT("continueOnError"), bContinueOnError);
    if (bContinueOnError)
    {
        bStopOnError = false;
    }

    int32 MaxOps = 200;
    if (const TSharedPtr<FJsonObject>* ExecutionPolicy = nullptr;
        Arguments->TryGetObjectField(TEXT("executionPolicy"), ExecutionPolicy) && ExecutionPolicy && (*ExecutionPolicy).IsValid())
    {
        bool StopOnErrorValue = true;
        if ((*ExecutionPolicy)->TryGetBoolField(TEXT("stopOnError"), StopOnErrorValue))
        {
            bStopOnError = StopOnErrorValue;
        }
        double MaxOpsNumber = 0.0;
        if ((*ExecutionPolicy)->TryGetNumberField(TEXT("maxOps"), MaxOpsNumber))
        {
            MaxOps = FMath::Clamp(static_cast<int32>(MaxOpsNumber), 1, 200);
        }
    }

    const TArray<TSharedPtr<FJsonValue>>* Ops = nullptr;
    if (!Arguments->TryGetArrayField(TEXT("ops"), Ops) || !Ops)
    {
        Result->SetBoolField(TEXT("isError"), true);
        Result->SetStringField(TEXT("code"), TEXT("INVALID_ARGUMENT"));
        Result->SetStringField(TEXT("message"), TEXT("arguments.ops must be an array."));
        return Result;
    }
    if (Ops->Num() > MaxOps)
    {
        Result->SetBoolField(TEXT("isError"), true);
        Result->SetStringField(TEXT("code"), TEXT("LIMIT_EXCEEDED"));
        Result->SetStringField(TEXT("message"), FString::Printf(TEXT("arguments.ops exceeds executionPolicy.maxOps (%d)."), MaxOps));
        return Result;
    }

    TMap<FString, FString> NodeRefs;
    TArray<TSharedPtr<FJsonValue>> OpResults;
    bool bAnyError = false;
    FString FirstError;

    auto ResolveNodeToken = [&NodeRefs](const TSharedPtr<FJsonObject>& Obj, FString& OutNodeId) -> bool
    {
        if (!Obj.IsValid())
        {
            return false;
        }
        if (Obj->TryGetStringField(TEXT("nodeId"), OutNodeId) && !OutNodeId.IsEmpty())
        {
            return true;
        }
        FString NodeRef;
        if (Obj->TryGetStringField(TEXT("nodeRef"), NodeRef) && NodeRefs.Contains(NodeRef))
        {
            OutNodeId = NodeRefs[NodeRef];
            return !OutNodeId.IsEmpty();
        }
        return false;
    };

    auto GetPointFromObject = [](const TSharedPtr<FJsonObject>& Obj, int32& OutX, int32& OutY)
    {
        OutX = 0;
        OutY = 0;
        if (!Obj.IsValid())
        {
            return;
        }

        if (const TSharedPtr<FJsonObject>* Position = nullptr;
            Obj->TryGetObjectField(TEXT("position"), Position) && Position && (*Position).IsValid())
        {
            double Xn = 0.0;
            double Yn = 0.0;
            (*Position)->TryGetNumberField(TEXT("x"), Xn);
            (*Position)->TryGetNumberField(TEXT("y"), Yn);
            OutX = static_cast<int32>(Xn);
            OutY = static_cast<int32>(Yn);
        }
    };

    auto SerializeJsonObject = [](const TSharedPtr<FJsonObject>& Obj) -> FString
    {
        if (!Obj.IsValid())
        {
            return TEXT("{}");
        }
        FString Out;
        TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Out);
        FJsonSerializer::Serialize(Obj.ToSharedRef(), Writer);
        return Out;
    };

    auto EscapePythonSingleQuoted = [](const FString& In) -> FString
    {
        FString Out = In;
        Out.ReplaceInline(TEXT("\\"), TEXT("\\\\"));
        Out.ReplaceInline(TEXT("'"), TEXT("\\'"));
        Out.ReplaceInline(TEXT("\r"), TEXT("\\r"));
        Out.ReplaceInline(TEXT("\n"), TEXT("\\n"));
        return Out;
    };

    bGraphMutateInProgress = !bDryRun;
    for (int32 Index = 0; Index < Ops->Num(); ++Index)
    {
        const TSharedPtr<FJsonObject>* OpObj = nullptr;
        if (!(*Ops)[Index].IsValid() || !(*Ops)[Index]->TryGetObject(OpObj) || !OpObj || !(*OpObj).IsValid())
        {
            continue;
        }

        FString Op;
        (*OpObj)->TryGetStringField(TEXT("op"), Op);
        Op = Op.ToLower();
        FString OpGraphName = GraphName;
        (*OpObj)->TryGetStringField(TEXT("targetGraphName"), OpGraphName);

        bool bOk = true;
        FString Error;
        FString NodeId;
        FString ClientRef;
        FString GraphEventName;
        TSharedPtr<FJsonObject> GraphEventData = MakeShared<FJsonObject>();
        TSharedPtr<FJsonObject> ScriptResultForOp;
        (*OpObj)->TryGetStringField(TEXT("clientRef"), ClientRef);

        const TSharedPtr<FJsonObject>* ArgsObjPtr = nullptr;
        const TSharedPtr<FJsonObject> ArgsObj =
            ((*OpObj)->TryGetObjectField(TEXT("args"), ArgsObjPtr) && ArgsObjPtr && (*ArgsObjPtr).IsValid())
                ? *ArgsObjPtr
                : MakeShared<FJsonObject>();

        if (!bDryRun)
        {
            if (Op.Equals(TEXT("addnode.byclass")))
            {
                FString NodeClassPath;
                ArgsObj->TryGetStringField(TEXT("nodeClassPath"), NodeClassPath);
                int32 X = 0;
                int32 Y = 0;
                GetPointFromObject(ArgsObj, X, Y);
                bOk = ULoomleBlueprintAdapter::AddNodeByClass(AssetPath, OpGraphName, NodeClassPath, SerializeJsonObject(ArgsObj), X, Y, NodeId, Error);
                if (bOk)
                {
                    GraphEventName = TEXT("graph.node_added");
                    GraphEventData->SetStringField(TEXT("nodeId"), NodeId);
                    GraphEventData->SetStringField(TEXT("nodeType"), TEXT("by_class"));
                    GraphEventData->SetStringField(TEXT("nodeClassPath"), NodeClassPath);
                    GraphEventData->SetStringField(TEXT("op"), Op);
                }
            }
            else if (Op.Equals(TEXT("addnode.byaction")))
            {
                FString ActionId;
                ArgsObj->TryGetStringField(TEXT("actionId"), ActionId);
                int32 X = 0;
                int32 Y = 0;
                GetPointFromObject(ArgsObj, X, Y);
                bOk = ULoomleBlueprintAdapter::AddNodeByAction(AssetPath, OpGraphName, ActionId, SerializeJsonObject(ArgsObj), X, Y, NodeId, Error);
                if (bOk)
                {
                    GraphEventName = TEXT("graph.node_added");
                    GraphEventData->SetStringField(TEXT("nodeId"), NodeId);
                    GraphEventData->SetStringField(TEXT("nodeType"), TEXT("by_action"));
                    GraphEventData->SetStringField(TEXT("actionId"), ActionId);
                    GraphEventData->SetStringField(TEXT("op"), Op);
                }
            }
            else if (Op.Equals(TEXT("connectpins")))
            {
                const TSharedPtr<FJsonObject>* FromObj = nullptr;
                const TSharedPtr<FJsonObject>* ToObj = nullptr;
                FString FromNodeId, ToNodeId, FromPin, ToPin;
                if (ArgsObj->TryGetObjectField(TEXT("from"), FromObj) && FromObj && (*FromObj).IsValid())
                {
                    ResolveNodeToken(*FromObj, FromNodeId);
                    (*FromObj)->TryGetStringField(TEXT("pin"), FromPin);
                }
                if (ArgsObj->TryGetObjectField(TEXT("to"), ToObj) && ToObj && (*ToObj).IsValid())
                {
                    ResolveNodeToken(*ToObj, ToNodeId);
                    (*ToObj)->TryGetStringField(TEXT("pin"), ToPin);
                }
                bOk = !FromNodeId.IsEmpty() && !ToNodeId.IsEmpty() && !FromPin.IsEmpty() && !ToPin.IsEmpty()
                    && ULoomleBlueprintAdapter::ConnectPins(AssetPath, OpGraphName, FromNodeId, FromPin, ToNodeId, ToPin, Error);
                if (!bOk && Error.IsEmpty())
                {
                    Error = TEXT("Failed to resolve connectPins node ids/pins.");
                }
                if (bOk)
                {
                    GraphEventName = TEXT("graph.node_connected");
                    GraphEventData->SetStringField(TEXT("fromNodeId"), FromNodeId);
                    GraphEventData->SetStringField(TEXT("fromPin"), FromPin);
                    GraphEventData->SetStringField(TEXT("toNodeId"), ToNodeId);
                    GraphEventData->SetStringField(TEXT("toPin"), ToPin);
                    GraphEventData->SetStringField(TEXT("op"), Op);
                }
            }
            else if (Op.Equals(TEXT("disconnectpins")))
            {
                const TSharedPtr<FJsonObject>* FromObj = nullptr;
                const TSharedPtr<FJsonObject>* ToObj = nullptr;
                FString FromNodeId, ToNodeId, FromPin, ToPin;
                if (ArgsObj->TryGetObjectField(TEXT("from"), FromObj) && FromObj && (*FromObj).IsValid())
                {
                    ResolveNodeToken(*FromObj, FromNodeId);
                    (*FromObj)->TryGetStringField(TEXT("pin"), FromPin);
                }
                if (ArgsObj->TryGetObjectField(TEXT("to"), ToObj) && ToObj && (*ToObj).IsValid())
                {
                    ResolveNodeToken(*ToObj, ToNodeId);
                    (*ToObj)->TryGetStringField(TEXT("pin"), ToPin);
                }
                bOk = !FromNodeId.IsEmpty() && !ToNodeId.IsEmpty() && !FromPin.IsEmpty() && !ToPin.IsEmpty()
                    && ULoomleBlueprintAdapter::DisconnectPins(AssetPath, OpGraphName, FromNodeId, FromPin, ToNodeId, ToPin, Error);
                if (!bOk && Error.IsEmpty())
                {
                    Error = TEXT("Failed to resolve disconnectPins node ids/pins.");
                }
                if (bOk)
                {
                    GraphEventName = TEXT("graph.links_changed");
                    GraphEventData->SetStringField(TEXT("change"), TEXT("disconnected"));
                    GraphEventData->SetStringField(TEXT("fromNodeId"), FromNodeId);
                    GraphEventData->SetStringField(TEXT("fromPin"), FromPin);
                    GraphEventData->SetStringField(TEXT("toNodeId"), ToNodeId);
                    GraphEventData->SetStringField(TEXT("toPin"), ToPin);
                    GraphEventData->SetStringField(TEXT("op"), Op);
                }
            }
            else if (Op.Equals(TEXT("breakpinlinks")))
            {
                const TSharedPtr<FJsonObject>* TargetObj = nullptr;
                FString NodeToken, Pin;
                if (ArgsObj->TryGetObjectField(TEXT("target"), TargetObj) && TargetObj && (*TargetObj).IsValid())
                {
                    ResolveNodeToken(*TargetObj, NodeToken);
                    (*TargetObj)->TryGetStringField(TEXT("pin"), Pin);
                }
                bOk = !NodeToken.IsEmpty() && !Pin.IsEmpty()
                    && ULoomleBlueprintAdapter::BreakPinLinks(AssetPath, OpGraphName, NodeToken, Pin, Error);
                if (!bOk && Error.IsEmpty())
                {
                    Error = TEXT("Failed to resolve breakPinLinks target.");
                }
                if (bOk)
                {
                    GraphEventName = TEXT("graph.links_changed");
                    GraphEventData->SetStringField(TEXT("change"), TEXT("break_all"));
                    GraphEventData->SetStringField(TEXT("nodeId"), NodeToken);
                    GraphEventData->SetStringField(TEXT("pin"), Pin);
                    GraphEventData->SetStringField(TEXT("op"), Op);
                }
            }
            else if (Op.Equals(TEXT("setpindefault")))
            {
                const TSharedPtr<FJsonObject>* TargetObj = nullptr;
                FString NodeToken, Pin, Value;
                if (ArgsObj->TryGetObjectField(TEXT("target"), TargetObj) && TargetObj && (*TargetObj).IsValid())
                {
                    ResolveNodeToken(*TargetObj, NodeToken);
                    (*TargetObj)->TryGetStringField(TEXT("pin"), Pin);
                }
                ArgsObj->TryGetStringField(TEXT("value"), Value);
                bOk = !NodeToken.IsEmpty() && !Pin.IsEmpty()
                    && ULoomleBlueprintAdapter::SetPinDefaultValue(AssetPath, OpGraphName, NodeToken, Pin, Value, Error);
                if (!bOk && Error.IsEmpty())
                {
                    Error = TEXT("Failed to resolve setPinDefault target.");
                }
                if (bOk)
                {
                    GraphEventName = TEXT("graph.pin_default_changed");
                    GraphEventData->SetStringField(TEXT("nodeId"), NodeToken);
                    GraphEventData->SetStringField(TEXT("pin"), Pin);
                    GraphEventData->SetStringField(TEXT("value"), Value);
                    GraphEventData->SetStringField(TEXT("op"), Op);
                }
            }
            else if (Op.Equals(TEXT("removenode")))
            {
                FString NodeToken;
                if (const TSharedPtr<FJsonObject>* TargetObj = nullptr; ArgsObj->TryGetObjectField(TEXT("target"), TargetObj) && TargetObj && (*TargetObj).IsValid())
                {
                    ResolveNodeToken(*TargetObj, NodeToken);
                }
                if (NodeToken.IsEmpty())
                {
                    ArgsObj->TryGetStringField(TEXT("nodeId"), NodeToken);
                }
                bOk = !NodeToken.IsEmpty() && ULoomleBlueprintAdapter::RemoveNode(AssetPath, OpGraphName, NodeToken, Error);
                if (!bOk && Error.IsEmpty())
                {
                    Error = TEXT("Failed to resolve removeNode target.");
                }
                if (bOk)
                {
                    GraphEventName = TEXT("graph.node_removed");
                    GraphEventData->SetStringField(TEXT("nodeId"), NodeToken);
                    GraphEventData->SetStringField(TEXT("op"), Op);
                }
            }
            else if (Op.Equals(TEXT("movenode")))
            {
                FString NodeToken;
                if (const TSharedPtr<FJsonObject>* TargetObj = nullptr; ArgsObj->TryGetObjectField(TEXT("target"), TargetObj) && TargetObj && (*TargetObj).IsValid())
                {
                    ResolveNodeToken(*TargetObj, NodeToken);
                }
                if (NodeToken.IsEmpty())
                {
                    ArgsObj->TryGetStringField(TEXT("nodeId"), NodeToken);
                }
                int32 X = 0;
                int32 Y = 0;
                GetPointFromObject(ArgsObj, X, Y);
                bOk = !NodeToken.IsEmpty() && ULoomleBlueprintAdapter::MoveNode(AssetPath, OpGraphName, NodeToken, X, Y, Error);
                if (!bOk && Error.IsEmpty())
                {
                    Error = TEXT("Failed to resolve moveNode target.");
                }
                if (bOk)
                {
                    GraphEventName = TEXT("graph.node_moved");
                    GraphEventData->SetStringField(TEXT("nodeId"), NodeToken);
                    GraphEventData->SetNumberField(TEXT("x"), X);
                    GraphEventData->SetNumberField(TEXT("y"), Y);
                    GraphEventData->SetStringField(TEXT("op"), Op);
                }
            }
            else if (Op.Equals(TEXT("compile")))
            {
                bOk = ULoomleBlueprintAdapter::CompileBlueprint(AssetPath, OpGraphName, Error);
                if (bOk)
                {
                    GraphEventName = TEXT("graph.compiled");
                    GraphEventData->SetStringField(TEXT("op"), Op);
                }
            }
            else if (Op.Equals(TEXT("runscript")))
            {
                FString Mode = TEXT("inlineCode");
                ArgsObj->TryGetStringField(TEXT("mode"), Mode);
                const FString ModeNormalized = Mode.ToLower();
                FString Entry = TEXT("run");
                ArgsObj->TryGetStringField(TEXT("entry"), Entry);
                FString ScriptCode;
                ArgsObj->TryGetStringField(TEXT("code"), ScriptCode);
                FString ScriptId;
                ArgsObj->TryGetStringField(TEXT("scriptId"), ScriptId);

                TSharedPtr<FJsonObject> ScriptInput = MakeShared<FJsonObject>();
                if (const TSharedPtr<FJsonObject>* InputObj = nullptr;
                    ArgsObj->TryGetObjectField(TEXT("input"), InputObj) && InputObj && (*InputObj).IsValid())
                {
                    ScriptInput = *InputObj;
                }

                if (ModeNormalized.Equals(TEXT("inlinecode")) && ScriptCode.IsEmpty())
                {
                    bOk = false;
                    Error = TEXT("runScript requires args.code when mode=inlineCode.");
                }
                else if (ModeNormalized.Equals(TEXT("scriptid")) && ScriptId.IsEmpty())
                {
                    bOk = false;
                    Error = TEXT("runScript requires args.scriptId when mode=scriptId.");
                }
                else
                {
                    TSharedPtr<FJsonObject> ScriptContext = MakeShared<FJsonObject>();
                    ScriptContext->SetStringField(TEXT("assetPath"), AssetPath);
                    ScriptContext->SetStringField(TEXT("graphName"), OpGraphName);
                    ScriptContext->SetNumberField(TEXT("opIndex"), Index);
                    ScriptContext->SetBoolField(TEXT("dryRun"), bDryRun);
                    ScriptContext->SetObjectField(TEXT("input"), ScriptInput);

                    TSharedPtr<FJsonObject> NodeRefsObject = MakeShared<FJsonObject>();
                    for (const TPair<FString, FString>& Pair : NodeRefs)
                    {
                        NodeRefsObject->SetStringField(Pair.Key, Pair.Value);
                    }
                    ScriptContext->SetObjectField(TEXT("nodeRefs"), NodeRefsObject);

                    const FString ContextJson = SerializeJsonObject(ScriptContext);
                    const FString ContextB64 = FBase64::Encode(ContextJson);
                    const FString CodeB64 = FBase64::Encode(ScriptCode);
                    const FString ScriptIdB64 = FBase64::Encode(ScriptId);
                    const FString EntryEscaped = EscapePythonSingleQuoted(Entry);
                    const FString ModeEscaped = EscapePythonSingleQuoted(Mode);

                    FString PythonSource;
                    PythonSource += TEXT("import base64, json, importlib\n");
                    PythonSource += FString::Printf(TEXT("_ctx = json.loads(base64.b64decode('%s').decode('utf-8'))\n"), *ContextB64);
                    PythonSource += FString::Printf(TEXT("_mode = '%s'\n"), *ModeEscaped);
                    PythonSource += FString::Printf(TEXT("_entry = '%s'\n"), *EntryEscaped);
                    PythonSource += TEXT("_fn = None\n");
                    PythonSource += TEXT("if _mode.lower() == 'inlinecode':\n");
                    PythonSource += FString::Printf(TEXT("    _src = base64.b64decode('%s').decode('utf-8')\n"), *CodeB64);
                    PythonSource += TEXT("    _ns = {}\n");
                    PythonSource += TEXT("    exec(_src, _ns)\n");
                    PythonSource += TEXT("    _fn = _ns.get(_entry)\n");
                    PythonSource += TEXT("else:\n");
                    PythonSource += FString::Printf(TEXT("    _mod_name = base64.b64decode('%s').decode('utf-8')\n"), *ScriptIdB64);
                    PythonSource += TEXT("    _mod = importlib.import_module(_mod_name)\n");
                    PythonSource += TEXT("    _fn = getattr(_mod, _entry)\n");
                    PythonSource += TEXT("if _fn is None:\n");
                    PythonSource += TEXT("    raise RuntimeError(f'Entry not found: {_entry}')\n");
                    PythonSource += TEXT("_out = _fn(_ctx)\n");
                    PythonSource += TEXT("if _out is None:\n");
                    PythonSource += TEXT("    _out = {}\n");
                    PythonSource += TEXT("print('__LOOMLE_SCRIPT_RESULT__' + json.dumps(_out, ensure_ascii=False))\n");

                    IPythonScriptPlugin* PythonScriptPlugin = IPythonScriptPlugin::Get();
                    if (PythonScriptPlugin == nullptr)
                    {
                        bOk = false;
                        Error = TEXT("PythonScriptPlugin module is not loaded.");
                    }
                    else
                    {
                        if (!PythonScriptPlugin->IsPythonInitialized())
                        {
                            PythonScriptPlugin->ForceEnablePythonAtRuntime();
                        }
                        if (!PythonScriptPlugin->IsPythonInitialized())
                        {
                            bOk = false;
                            Error = TEXT("Python runtime is not initialized.");
                        }
                        else
                        {
                            FPythonCommandEx PythonCommand;
                            PythonCommand.ExecutionMode = EPythonCommandExecutionMode::ExecuteFile;
                            PythonCommand.Command = PythonSource;
                            bOk = PythonScriptPlugin->ExecPythonCommandEx(PythonCommand);
                            if (!bOk)
                            {
                                Error = PythonCommand.CommandResult.IsEmpty() ? TEXT("runScript execution failed.") : PythonCommand.CommandResult;
                            }
                            else
                            {
                                FString ScriptResultJson;
                                for (const FPythonLogOutputEntry& EntryLine : PythonCommand.LogOutput)
                                {
                                    const FString Prefix = TEXT("__LOOMLE_SCRIPT_RESULT__");
                                    if (EntryLine.Output.Contains(Prefix))
                                    {
                                        const int32 PrefixIndex = EntryLine.Output.Find(Prefix);
                                        ScriptResultJson = EntryLine.Output.Mid(PrefixIndex + Prefix.Len()).TrimStartAndEnd();
                                    }
                                }

                                if (ScriptResultJson.IsEmpty())
                                {
                                    bOk = false;
                                    Error = TEXT("runScript did not emit structured result.");
                                }
                                else
                                {
                                    TSharedPtr<FJsonObject> ScriptResultObj;
                                    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(ScriptResultJson);
                                    if (!FJsonSerializer::Deserialize(Reader, ScriptResultObj) || !ScriptResultObj.IsValid())
                                    {
                                        bOk = false;
                                        Error = TEXT("runScript returned invalid JSON result.");
                                    }
                                    else
                                    {
                                        ScriptResultForOp = ScriptResultObj;
                                        GraphEventName = TEXT("graph.script_executed");
                                        GraphEventData->SetStringField(TEXT("mode"), Mode);
                                        GraphEventData->SetStringField(TEXT("entry"), Entry);
                                        if (!ScriptId.IsEmpty())
                                        {
                                            GraphEventData->SetStringField(TEXT("scriptId"), ScriptId);
                                        }
                                        GraphEventData->SetStringField(TEXT("op"), Op);
                                    }
                                }
                            }
                        }
                    }
                }
            }
            else
            {
                bOk = false;
                Error = FString::Printf(TEXT("Unsupported mutate op: %s"), *Op);
            }
        }

        if (!ClientRef.IsEmpty() && !NodeId.IsEmpty())
        {
            NodeRefs.Add(ClientRef, NodeId);
        }

        TSharedPtr<FJsonObject> OpResult = MakeShared<FJsonObject>();
        OpResult->SetNumberField(TEXT("index"), Index);
        OpResult->SetStringField(TEXT("op"), Op);
        OpResult->SetBoolField(TEXT("ok"), bOk);
        if (!NodeId.IsEmpty())
        {
            OpResult->SetStringField(TEXT("nodeId"), NodeId);
        }
        if (!Error.IsEmpty())
        {
            OpResult->SetStringField(TEXT("error"), Error);
        }
        if (ScriptResultForOp.IsValid())
        {
            OpResult->SetObjectField(TEXT("scriptResult"), ScriptResultForOp);
        }
        OpResults.Add(MakeShared<FJsonValueObject>(OpResult));

        if (!bOk)
        {
            bAnyError = true;
            if (FirstError.IsEmpty())
            {
                FirstError = Error;
            }
            if (bStopOnError)
            {
                break;
            }
        }

        if (!bDryRun && !GraphEventName.IsEmpty() && GraphEventData.IsValid())
        {
            GraphEventData->SetStringField(TEXT("graphType"), TEXT("blueprint"));
            GraphEventData->SetStringField(TEXT("assetPath"), AssetPath);
            GraphEventData->SetStringField(TEXT("graphName"), OpGraphName);
            GraphEventData->SetStringField(TEXT("sourceKind"), TEXT("graph.mutate"));
            SendEditorStreamEvent(GraphEventName, GraphEventData);
        }
    }
    bGraphMutateInProgress = false;

    Result->SetBoolField(TEXT("isError"), bAnyError);
    if (bAnyError)
    {
        Result->SetStringField(TEXT("code"), TEXT("INTERNAL_ERROR"));
        Result->SetStringField(TEXT("message"), FirstError.IsEmpty() ? TEXT("graph.mutate failed") : FirstError);
    }

    Result->SetBoolField(TEXT("applied"), !bAnyError);
    Result->SetStringField(TEXT("graphType"), GraphType);
    Result->SetStringField(TEXT("assetPath"), AssetPath);
    Result->SetStringField(TEXT("graphName"), GraphName);
    Result->SetStringField(TEXT("previousRevision"), FString::Printf(TEXT("bp:%08x"), GetTypeHash(AssetPath + TEXT("|prev"))));
    Result->SetStringField(TEXT("newRevision"), FString::Printf(TEXT("bp:%08x"), GetTypeHash(AssetPath + TEXT("|new") + FString::FromInt(OpResults.Num()))));
    Result->SetArrayField(TEXT("opResults"), OpResults);
    Result->SetArrayField(TEXT("diagnostics"), TArray<TSharedPtr<FJsonValue>>{});
    return Result;
}

TSharedPtr<FJsonObject> FLoomleBridgeModule::BuildGraphWatchToolResult(const TSharedPtr<FJsonObject>& Arguments) const
{
    FString GraphType = TEXT("blueprint");
    Arguments->TryGetStringField(TEXT("graphType"), GraphType);
    GraphType = GraphType.ToLower();
    if (!GraphType.Equals(TEXT("blueprint")))
    {
        TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
        Result->SetBoolField(TEXT("isError"), true);
        Result->SetStringField(TEXT("code"), TEXT("UNSUPPORTED_GRAPH_TYPE"));
        Result->SetStringField(TEXT("message"), TEXT("Only blueprint graphType is currently supported."));
        return Result;
    }

    FString AssetPath;
    Arguments->TryGetStringField(TEXT("assetPath"), AssetPath);

    TSharedPtr<FJsonObject> Result = BuildEventPullResult(Arguments, TEXT("graph"), AssetPath, false);
    Result->SetStringField(TEXT("graphType"), GraphType);
    if (!AssetPath.IsEmpty())
    {
        Result->SetStringField(TEXT("assetPath"), AssetPath);
    }
    return Result;
}

TSharedPtr<FJsonObject> FLoomleBridgeModule::BuildSelectionTransformToolResult() const
{
    TSharedPtr<FJsonObject> BlueprintSelection;
    if (BuildBlueprintSelectionSnapshot(BlueprintSelection) && BlueprintSelection.IsValid())
    {
        BlueprintSelection->SetStringField(TEXT("source"), TEXT("unified"));
        return BlueprintSelection;
    }

    TSharedPtr<FJsonObject> MaterialSelection;
    if (BuildMaterialSelectionSnapshot(MaterialSelection) && MaterialSelection.IsValid())
    {
        MaterialSelection->SetStringField(TEXT("source"), TEXT("unified"));
        return MaterialSelection;
    }

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetBoolField(TEXT("isError"), false);

    TArray<TSharedPtr<FJsonValue>> Items;
    FBox AggregateBox(EForceInit::ForceInit);
    TSharedPtr<FJsonObject> ActiveItem;

    if (GEditor)
    {
        USelection* Selection = GEditor->GetSelectedActors();
        if (Selection)
        {
            for (FSelectionIterator It(*Selection); It; ++It)
            {
                AActor* Actor = Cast<AActor>(*It);
                if (!Actor)
                {
                    continue;
                }

                const FVector Location = Actor->GetActorLocation();
                const FRotator Rotation = Actor->GetActorRotation();
                const FVector Scale = Actor->GetActorScale3D();

                FVector BoundsOrigin(ForceInitToZero);
                FVector BoundsExtent(ForceInitToZero);
                Actor->GetActorBounds(true, BoundsOrigin, BoundsExtent);
                AggregateBox += (BoundsOrigin - BoundsExtent);
                AggregateBox += (BoundsOrigin + BoundsExtent);

                TSharedPtr<FJsonObject> Item = MakeShared<FJsonObject>();
                Item->SetStringField(TEXT("id"), Actor->GetPathName());
                Item->SetStringField(TEXT("name"), Actor->GetActorLabel());
                Item->SetStringField(TEXT("class"), Actor->GetClass() ? Actor->GetClass()->GetPathName() : TEXT(""));
                Item->SetStringField(TEXT("path"), Actor->GetPathName());

                TSharedPtr<FJsonObject> LocationObj = MakeShared<FJsonObject>();
                LocationObj->SetNumberField(TEXT("x"), Location.X);
                LocationObj->SetNumberField(TEXT("y"), Location.Y);
                LocationObj->SetNumberField(TEXT("z"), Location.Z);
                Item->SetObjectField(TEXT("location"), LocationObj);

                TSharedPtr<FJsonObject> RotationObj = MakeShared<FJsonObject>();
                RotationObj->SetNumberField(TEXT("pitch"), Rotation.Pitch);
                RotationObj->SetNumberField(TEXT("yaw"), Rotation.Yaw);
                RotationObj->SetNumberField(TEXT("roll"), Rotation.Roll);
                Item->SetObjectField(TEXT("rotation"), RotationObj);

                TSharedPtr<FJsonObject> ScaleObj = MakeShared<FJsonObject>();
                ScaleObj->SetNumberField(TEXT("x"), Scale.X);
                ScaleObj->SetNumberField(TEXT("y"), Scale.Y);
                ScaleObj->SetNumberField(TEXT("z"), Scale.Z);
                Item->SetObjectField(TEXT("scale"), ScaleObj);

                TSharedPtr<FJsonObject> BoundsObj = MakeShared<FJsonObject>();
                TSharedPtr<FJsonObject> OriginObj = MakeShared<FJsonObject>();
                OriginObj->SetNumberField(TEXT("x"), BoundsOrigin.X);
                OriginObj->SetNumberField(TEXT("y"), BoundsOrigin.Y);
                OriginObj->SetNumberField(TEXT("z"), BoundsOrigin.Z);
                BoundsObj->SetObjectField(TEXT("origin"), OriginObj);

                TSharedPtr<FJsonObject> ExtentObj = MakeShared<FJsonObject>();
                ExtentObj->SetNumberField(TEXT("x"), BoundsExtent.X);
                ExtentObj->SetNumberField(TEXT("y"), BoundsExtent.Y);
                ExtentObj->SetNumberField(TEXT("z"), BoundsExtent.Z);
                BoundsObj->SetObjectField(TEXT("boxExtent"), ExtentObj);

                BoundsObj->SetNumberField(TEXT("sphereRadius"), BoundsExtent.Size());
                Item->SetObjectField(TEXT("bounds"), BoundsObj);

                if (!ActiveItem.IsValid())
                {
                    ActiveItem = Item;
                }

                Items.Add(MakeShared<FJsonValueObject>(Item));
            }
        }
    }

    Result->SetArrayField(TEXT("items"), Items);
    Result->SetNumberField(TEXT("count"), Items.Num());

    if (Items.Num() > 0)
    {
        TSharedPtr<FJsonObject> Aggregate = MakeShared<FJsonObject>();
        const FVector Center = AggregateBox.GetCenter();
        const FVector Extent = AggregateBox.GetExtent();

        TSharedPtr<FJsonObject> CenterObj = MakeShared<FJsonObject>();
        CenterObj->SetNumberField(TEXT("x"), Center.X);
        CenterObj->SetNumberField(TEXT("y"), Center.Y);
        CenterObj->SetNumberField(TEXT("z"), Center.Z);
        Aggregate->SetObjectField(TEXT("center"), CenterObj);

        TSharedPtr<FJsonObject> ExtentObj = MakeShared<FJsonObject>();
        ExtentObj->SetNumberField(TEXT("x"), Extent.X);
        ExtentObj->SetNumberField(TEXT("y"), Extent.Y);
        ExtentObj->SetNumberField(TEXT("z"), Extent.Z);
        Aggregate->SetObjectField(TEXT("boxExtent"), ExtentObj);

        Result->SetObjectField(TEXT("aggregate"), Aggregate);
    }

    if (ActiveItem.IsValid())
    {
        Result->SetObjectField(TEXT("active"), ActiveItem);
    }

    return Result;
}

void FLoomleBridgeModule::SendEditorStreamEvent(const FString& EventName, const TSharedPtr<FJsonObject>& Data)
{
    if (!bEditorStreamEnabled || !PipeServer.IsValid())
    {
        return;
    }

    TSharedPtr<FJsonObject> Params = MakeShared<FJsonObject>();
    const int64 Sequence = NextLiveSequence++;
    Params->SetStringField(TEXT("event"), EventName);
    Params->SetNumberField(TEXT("seq"), static_cast<double>(Sequence));
    Params->SetStringField(TEXT("timestamp"), FDateTime::UtcNow().ToIso8601());
    Params->SetStringField(TEXT("origin"), DeriveEventOrigin(EventName, Data));
    Params->SetStringField(TEXT("scope"), IsGraphEventName(EventName) ? TEXT("graph") : TEXT("editor"));
    Params->SetObjectField(TEXT("data"), Data);

    TSharedPtr<FJsonObject> Notification = MakeShared<FJsonObject>();
    Notification->SetStringField(TEXT("jsonrpc"), TEXT("2.0"));
    Notification->SetStringField(TEXT("method"), LoomleBridgeConstants::LiveNotificationMethod);
    Notification->SetObjectField(TEXT("params"), Params);

    FString Output;
    TSharedRef<FCondensedJsonWriter> Writer = TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Output);
    FJsonSerializer::Serialize(Notification.ToSharedRef(), Writer);
    LiveEventBuffer.Add(Output);
    if (LiveEventBuffer.Num() > LoomleBridgeConstants::MaxLiveMemoryRecords)
    {
        LiveEventBuffer.RemoveAt(0, LiveEventBuffer.Num() - LoomleBridgeConstants::MaxLiveMemoryRecords, EAllowShrinking::No);
    }
    AppendEditorStreamEventLog(Output);

    const TSharedPtr<FLoomlePipeServer, ESPMode::ThreadSafe> PipeServerSnapshot = PipeServer;
    Async(EAsyncExecution::ThreadPool, [PipeServerSnapshot, Output]()
    {
        if (!PipeServerSnapshot.IsValid())
        {
            return;
        }

        PipeServerSnapshot->SendServerNotification(Output);
    });
}

void FLoomleBridgeModule::AppendEditorStreamEventLog(const FString& JsonLine)
{
    FScopeLock ScopeLock(&LiveLogMutex);

    const FString RuntimeDir = FPaths::Combine(FPaths::ProjectDir(), TEXT("Loomle"), TEXT("runtime"));
    IFileManager::Get().MakeDirectory(*RuntimeDir, true);
    const FString StreamLogPath = FPaths::Combine(RuntimeDir, LoomleBridgeConstants::LiveLogFileName);

    const FString JsonLineWithNewline = JsonLine + TEXT("\n");
    FFileHelper::SaveStringToFile(
        JsonLineWithNewline,
        *StreamLogPath,
        FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM,
        &IFileManager::Get(),
        FILEWRITE_Append | FILEWRITE_AllowRead);

    ++LiveLogAppendSinceTrim;
    if (LiveLogAppendSinceTrim < LoomleBridgeConstants::LiveLogTrimInterval)
    {
        return;
    }
    LiveLogAppendSinceTrim = 0;

    TArray<FString> Lines;
    if (!FFileHelper::LoadFileToStringArray(Lines, *StreamLogPath))
    {
        return;
    }

    if (Lines.Num() <= LoomleBridgeConstants::MaxLiveLogRecords)
    {
        return;
    }

    Lines.RemoveAt(0, Lines.Num() - LoomleBridgeConstants::MaxLiveLogRecords, EAllowShrinking::No);
    FFileHelper::SaveStringArrayToFile(
        Lines,
        *StreamLogPath,
        FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
}

bool FLoomleBridgeModule::ParseLiveEventLine(const FString& JsonLine, int64& OutSeq, TSharedPtr<FJsonObject>& OutEventObject) const
{
    OutSeq = 0;
    OutEventObject.Reset();

    TSharedPtr<FJsonObject> EventObject;
    const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonLine);
    if (!FJsonSerializer::Deserialize(Reader, EventObject) || !EventObject.IsValid())
    {
        return false;
    }

    const TSharedPtr<FJsonObject>* Params = nullptr;
    if (!EventObject->TryGetObjectField(TEXT("params"), Params) || Params == nullptr || !(*Params).IsValid())
    {
        return false;
    }

    double SeqNumber = 0.0;
    if (!(*Params)->TryGetNumberField(TEXT("seq"), SeqNumber))
    {
        return false;
    }

    OutSeq = static_cast<int64>(SeqNumber);
    OutEventObject = EventObject;
    return true;
}

void FLoomleBridgeModule::RegisterEditorStreamDelegates()
{
    if (!SelectionChangedHandle.IsValid())
    {
        SelectionChangedHandle = USelection::SelectionChangedEvent.AddRaw(this, &FLoomleBridgeModule::OnSelectionChanged);
    }

    if (!MapOpenedHandle.IsValid())
    {
        MapOpenedHandle = FEditorDelegates::OnMapOpened.AddRaw(this, &FLoomleBridgeModule::OnMapOpened);
    }

    if (GEngine && !ActorMovedHandle.IsValid())
    {
        ActorMovedHandle = GEngine->OnActorMoved().AddRaw(this, &FLoomleBridgeModule::OnActorMoved);
    }

    if (GEngine && !ActorAddedHandle.IsValid())
    {
        ActorAddedHandle = GEngine->OnLevelActorAdded().AddRaw(this, &FLoomleBridgeModule::OnActorAdded);
    }

    if (GEngine && !ActorDeletedHandle.IsValid())
    {
        ActorDeletedHandle = GEngine->OnLevelActorDeleted().AddRaw(this, &FLoomleBridgeModule::OnActorDeleted);
    }

    if (GEngine && !ActorAttachedHandle.IsValid())
    {
        ActorAttachedHandle = GEngine->OnLevelActorAttached().AddRaw(this, &FLoomleBridgeModule::OnActorAttached);
    }

    if (GEngine && !ActorDetachedHandle.IsValid())
    {
        ActorDetachedHandle = GEngine->OnLevelActorDetached().AddRaw(this, &FLoomleBridgeModule::OnActorDetached);
    }

    if (!BeginPieHandle.IsValid())
    {
        BeginPieHandle = FEditorDelegates::BeginPIE.AddRaw(this, &FLoomleBridgeModule::OnBeginPIE);
    }

    if (!EndPieHandle.IsValid())
    {
        EndPieHandle = FEditorDelegates::EndPIE.AddRaw(this, &FLoomleBridgeModule::OnEndPIE);
    }

    if (!PausePieHandle.IsValid())
    {
        PausePieHandle = FEditorDelegates::PausePIE.AddRaw(this, &FLoomleBridgeModule::OnPausePIE);
    }

    if (!ResumePieHandle.IsValid())
    {
        ResumePieHandle = FEditorDelegates::ResumePIE.AddRaw(this, &FLoomleBridgeModule::OnResumePIE);
    }

    if (!PostUndoRedoHandle.IsValid())
    {
        PostUndoRedoHandle = FEditorDelegates::PostUndoRedo.AddRaw(this, &FLoomleBridgeModule::OnPostUndoRedo);
    }

    if (!ObjectPropertyChangedHandle.IsValid())
    {
        ObjectPropertyChangedHandle = FCoreUObjectDelegates::OnObjectPropertyChanged.AddRaw(this, &FLoomleBridgeModule::OnObjectPropertyChanged);
    }

    if (!ObjectTransactedHandle.IsValid())
    {
        ObjectTransactedHandle = FCoreUObjectDelegates::OnObjectTransacted.AddRaw(this, &FLoomleBridgeModule::OnObjectTransacted);
    }

    if (!SelectionDebounceTickerHandle.IsValid())
    {
        SelectionDebounceTickerHandle = FTSTicker::GetCoreTicker().AddTicker(
            FTickerDelegate::CreateLambda([this](float)
            {
                if (!bEditorStreamEnabled)
                {
                    return true;
                }

                if (bSelectionEventPending)
                {
                    const double Now = FPlatformTime::Seconds();
                    constexpr double DebounceSeconds = 0.1;
                    if (Now - LastSelectionChangeTimeSeconds >= DebounceSeconds)
                    {
                        TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
                        Data->SetNumberField(TEXT("count"), PendingSelectionCount);
                        Data->SetStringField(TEXT("signature"), PendingSelectionSignature);
                        TArray<TSharedPtr<FJsonValue>> PathValues;
                        PathValues.Reserve(PendingSelectionPaths.Num());
                        for (const FString& Path : PendingSelectionPaths)
                        {
                            PathValues.Add(MakeShared<FJsonValueString>(Path));
                        }
                        Data->SetArrayField(TEXT("paths"), PathValues);
                        SendEditorStreamEvent(TEXT("selection_changed"), Data);
                        LastEmittedSelectionSignature = PendingSelectionSignature;
                        bSelectionEventPending = false;
                    }
                }

                EmitDirtyBlueprintGraphChanges();
                EmitGraphSelectionIfChanged();
                return true;
            }),
            0.02f);
    }
}

void FLoomleBridgeModule::UnregisterEditorStreamDelegates()
{
    if (SelectionChangedHandle.IsValid())
    {
        USelection::SelectionChangedEvent.Remove(SelectionChangedHandle);
        SelectionChangedHandle.Reset();
    }

    if (MapOpenedHandle.IsValid())
    {
        FEditorDelegates::OnMapOpened.Remove(MapOpenedHandle);
        MapOpenedHandle.Reset();
    }

    if (GEngine && ActorMovedHandle.IsValid())
    {
        GEngine->OnActorMoved().Remove(ActorMovedHandle);
        ActorMovedHandle.Reset();
    }

    if (GEngine && ActorAddedHandle.IsValid())
    {
        GEngine->OnLevelActorAdded().Remove(ActorAddedHandle);
        ActorAddedHandle.Reset();
    }

    if (GEngine && ActorDeletedHandle.IsValid())
    {
        GEngine->OnLevelActorDeleted().Remove(ActorDeletedHandle);
        ActorDeletedHandle.Reset();
    }

    if (GEngine && ActorAttachedHandle.IsValid())
    {
        GEngine->OnLevelActorAttached().Remove(ActorAttachedHandle);
        ActorAttachedHandle.Reset();
    }

    if (GEngine && ActorDetachedHandle.IsValid())
    {
        GEngine->OnLevelActorDetached().Remove(ActorDetachedHandle);
        ActorDetachedHandle.Reset();
    }

    if (BeginPieHandle.IsValid())
    {
        FEditorDelegates::BeginPIE.Remove(BeginPieHandle);
        BeginPieHandle.Reset();
    }

    if (EndPieHandle.IsValid())
    {
        FEditorDelegates::EndPIE.Remove(EndPieHandle);
        EndPieHandle.Reset();
    }

    if (PausePieHandle.IsValid())
    {
        FEditorDelegates::PausePIE.Remove(PausePieHandle);
        PausePieHandle.Reset();
    }

    if (ResumePieHandle.IsValid())
    {
        FEditorDelegates::ResumePIE.Remove(ResumePieHandle);
        ResumePieHandle.Reset();
    }

    if (PostUndoRedoHandle.IsValid())
    {
        FEditorDelegates::PostUndoRedo.Remove(PostUndoRedoHandle);
        PostUndoRedoHandle.Reset();
    }

    if (ObjectPropertyChangedHandle.IsValid())
    {
        FCoreUObjectDelegates::OnObjectPropertyChanged.Remove(ObjectPropertyChangedHandle);
        ObjectPropertyChangedHandle.Reset();
    }

    if (ObjectTransactedHandle.IsValid())
    {
        FCoreUObjectDelegates::OnObjectTransacted.Remove(ObjectTransactedHandle);
        ObjectTransactedHandle.Reset();
    }

    if (SelectionDebounceTickerHandle.IsValid())
    {
        FTSTicker::GetCoreTicker().RemoveTicker(SelectionDebounceTickerHandle);
        SelectionDebounceTickerHandle.Reset();
    }
}

void FLoomleBridgeModule::OnSelectionChanged(UObject*)
{
    int32 Count = 0;
    TArray<FString> Paths;
    if (GEditor)
    {
        USelection* Selection = GEditor->GetSelectedActors();
        if (Selection)
        {
            Count = Selection->Num();
            Paths.Reserve(Count);
            for (FSelectionIterator It(*Selection); It; ++It)
            {
                if (AActor* Actor = Cast<AActor>(*It))
                {
                    Paths.Add(Actor->GetPathName());
                }
            }
        }
    }

    Algo::Sort(Paths);
    const FString Signature = FString::Join(Paths, TEXT("|"));
    if (Signature == LastEmittedSelectionSignature)
    {
        return;
    }

    PendingSelectionCount = Count;
    PendingSelectionSignature = Signature;
    PendingSelectionPaths = MoveTemp(Paths);
    LastSelectionChangeTimeSeconds = FPlatformTime::Seconds();
    bSelectionEventPending = true;
}

void FLoomleBridgeModule::EmitGraphSelectionIfChanged()
{
    TSharedPtr<FJsonObject> GraphData;
    FString GraphSignature;
    bool bHasGraphSelection = false;

    TSharedPtr<FJsonObject> BlueprintSelection;
    if (BuildBlueprintSelectionSnapshot(BlueprintSelection) && BlueprintSelection.IsValid())
    {
        const TArray<TSharedPtr<FJsonValue>>* ItemsPtr = nullptr;
        TArray<FString> SignatureParts;
        if (BlueprintSelection->TryGetArrayField(TEXT("items"), ItemsPtr) && ItemsPtr)
        {
            SignatureParts.Reserve((*ItemsPtr).Num());
            for (const TSharedPtr<FJsonValue>& ItemValue : *ItemsPtr)
            {
                const TSharedPtr<FJsonObject>* ItemObj = nullptr;
                if (!ItemValue.IsValid() || !ItemValue->TryGetObject(ItemObj) || !ItemObj || !(*ItemObj).IsValid())
                {
                    continue;
                }

                FString SignatureToken;
                if (!(*ItemObj)->TryGetStringField(TEXT("id"), SignatureToken) || SignatureToken.IsEmpty())
                {
                    (*ItemObj)->TryGetStringField(TEXT("path"), SignatureToken);
                }
                if (!SignatureToken.IsEmpty())
                {
                    SignatureParts.Add(SignatureToken);
                }
            }
        }

        Algo::Sort(SignatureParts);
        GraphSignature = FString::Join(SignatureParts, TEXT("|"));
        BlueprintSelection->SetStringField(TEXT("signature"), GraphSignature);
        BlueprintSelection->SetStringField(TEXT("source"), TEXT("adapter"));
        FString GraphAssetPath;
        if (BlueprintSelection->TryGetStringField(TEXT("assetPath"), GraphAssetPath) && !GraphAssetPath.IsEmpty())
        {
            // Prime baseline before first manual edit so first diff is not treated as full-graph add.
            EnsureBlueprintGraphBaseline(GraphAssetPath);
        }
        GraphData = BlueprintSelection;
        bHasGraphSelection = true;
    }

    if (bHasGraphSelection)
    {
        if (GraphSignature == LastEmittedGraphSelectionSignature)
        {
            return;
        }

        SendEditorStreamEvent(TEXT("graph.selection_changed"), GraphData);
        LastEmittedGraphSelectionSignature = GraphSignature;
    }
    else if (!LastEmittedGraphSelectionSignature.IsEmpty())
    {
        TSharedPtr<FJsonObject> ClearedGraphData = MakeShared<FJsonObject>();
        ClearedGraphData->SetStringField(TEXT("selectionKind"), TEXT("graph_node"));
        ClearedGraphData->SetStringField(TEXT("editorType"), TEXT("none"));
        ClearedGraphData->SetStringField(TEXT("assetPath"), TEXT(""));
        ClearedGraphData->SetArrayField(TEXT("assetPaths"), TArray<TSharedPtr<FJsonValue>>{});
        ClearedGraphData->SetArrayField(TEXT("items"), TArray<TSharedPtr<FJsonValue>>{});
        ClearedGraphData->SetNumberField(TEXT("count"), 0);
        ClearedGraphData->SetStringField(TEXT("signature"), TEXT(""));
        SendEditorStreamEvent(TEXT("graph.selection_changed"), ClearedGraphData);
        LastEmittedGraphSelectionSignature.Empty();
    }
}

void FLoomleBridgeModule::EnsureBlueprintGraphBaseline(const FString& BlueprintAssetPath)
{
    if (BlueprintAssetPath.IsEmpty())
    {
        return;
    }

    FString NormalizedAssetPath = BlueprintAssetPath;
    const int32 DotIndex = NormalizedAssetPath.Find(TEXT("."));
    if (DotIndex > 0)
    {
        NormalizedAssetPath = NormalizedAssetPath.Left(DotIndex);
    }

    if (LastBlueprintGraphSignatureByAssetPath.Contains(NormalizedAssetPath))
    {
        return;
    }

    FString Signature;
    int32 NodeCount = 0;
    int32 EdgeCount = 0;
    TArray<FString> NodeTokens;
    TArray<FString> EdgeTokens;
    if (!CaptureBlueprintGraphSnapshot(NormalizedAssetPath, Signature, NodeCount, EdgeCount, NodeTokens, EdgeTokens))
    {
        return;
    }

    LastBlueprintGraphSignatureByAssetPath.Add(NormalizedAssetPath, Signature);
    LastBlueprintGraphNodeCountByAssetPath.Add(NormalizedAssetPath, NodeCount);
    LastBlueprintGraphEdgeCountByAssetPath.Add(NormalizedAssetPath, EdgeCount);
    LastBlueprintGraphNodeTokensByAssetPath.Add(NormalizedAssetPath, NodeTokens);
    LastBlueprintGraphEdgeTokensByAssetPath.Add(NormalizedAssetPath, EdgeTokens);
}

void FLoomleBridgeModule::EmitDirtyBlueprintGraphChanges()
{
    if (PendingDirtyBlueprintGraphReasons.Num() == 0)
    {
        return;
    }

    TMap<FString, FString> DirtyGraphReasons = PendingDirtyBlueprintGraphReasons;
    PendingDirtyBlueprintGraphReasons.Empty();

    for (const TPair<FString, FString>& Entry : DirtyGraphReasons)
    {
        const FString& AssetPath = Entry.Key;
        const FString& Reason = Entry.Value;
        FString SignatureAfter;
        int32 NodeCountAfter = 0;
        int32 EdgeCountAfter = 0;
        TArray<FString> NodeTokensAfter;
        TArray<FString> EdgeTokensAfter;
        if (!CaptureBlueprintGraphSnapshot(AssetPath, SignatureAfter, NodeCountAfter, EdgeCountAfter, NodeTokensAfter, EdgeTokensAfter))
        {
            continue;
        }

        const FString SignatureBefore = LastBlueprintGraphSignatureByAssetPath.FindRef(AssetPath);
        const int32 NodeCountBefore = LastBlueprintGraphNodeCountByAssetPath.FindRef(AssetPath);
        const int32 EdgeCountBefore = LastBlueprintGraphEdgeCountByAssetPath.FindRef(AssetPath);
        const TArray<FString> NodeTokensBefore = LastBlueprintGraphNodeTokensByAssetPath.FindRef(AssetPath);
        const TArray<FString> EdgeTokensBefore = LastBlueprintGraphEdgeTokensByAssetPath.FindRef(AssetPath);

        if (!SignatureBefore.IsEmpty() && SignatureBefore.Equals(SignatureAfter))
        {
            continue;
        }

        TSet<FString> NodeBeforeSet;
        NodeBeforeSet.Reserve(NodeTokensBefore.Num());
        for (const FString& Token : NodeTokensBefore)
        {
            NodeBeforeSet.Add(Token);
        }
        TSet<FString> NodeAfterSet;
        NodeAfterSet.Reserve(NodeTokensAfter.Num());
        for (const FString& Token : NodeTokensAfter)
        {
            NodeAfterSet.Add(Token);
        }
        TSet<FString> EdgeBeforeSet;
        EdgeBeforeSet.Reserve(EdgeTokensBefore.Num());
        for (const FString& Token : EdgeTokensBefore)
        {
            EdgeBeforeSet.Add(Token);
        }
        TSet<FString> EdgeAfterSet;
        EdgeAfterSet.Reserve(EdgeTokensAfter.Num());
        for (const FString& Token : EdgeTokensAfter)
        {
            EdgeAfterSet.Add(Token);
        }

        TArray<FString> RemovedNodeIds;
        TArray<FString> AddedNodeIds;
        TArray<FString> RemovedEdges;
        TArray<FString> AddedEdges;

        for (const FString& Token : NodeBeforeSet)
        {
            if (!NodeAfterSet.Contains(Token))
            {
                RemovedNodeIds.Add(Token);
            }
        }
        for (const FString& Token : NodeAfterSet)
        {
            if (!NodeBeforeSet.Contains(Token))
            {
                AddedNodeIds.Add(Token);
            }
        }
        for (const FString& Token : EdgeBeforeSet)
        {
            if (!EdgeAfterSet.Contains(Token))
            {
                RemovedEdges.Add(Token);
            }
        }
        for (const FString& Token : EdgeAfterSet)
        {
            if (!EdgeBeforeSet.Contains(Token))
            {
                AddedEdges.Add(Token);
            }
        }

        Algo::Sort(RemovedNodeIds);
        Algo::Sort(AddedNodeIds);
        Algo::Sort(RemovedEdges);
        Algo::Sort(AddedEdges);

        TSharedPtr<FJsonObject> Summary = MakeShared<FJsonObject>();
        Summary->SetNumberField(TEXT("nodeCountDelta"), NodeCountAfter - NodeCountBefore);
        Summary->SetNumberField(TEXT("edgeCountDelta"), EdgeCountAfter - EdgeCountBefore);
        Summary->SetStringField(TEXT("signatureBefore"), SignatureBefore);
        Summary->SetStringField(TEXT("signatureAfter"), SignatureAfter);
        Summary->SetStringField(TEXT("reason"), Reason.IsEmpty() ? TEXT("manual_edit") : Reason);
        {
            TArray<TSharedPtr<FJsonValue>> RemovedNodeValues;
            RemovedNodeValues.Reserve(RemovedNodeIds.Num());
            for (const FString& Token : RemovedNodeIds)
            {
                RemovedNodeValues.Add(MakeShared<FJsonValueString>(Token));
            }
            Summary->SetArrayField(TEXT("removedNodeIds"), RemovedNodeValues);
        }
        {
            TArray<TSharedPtr<FJsonValue>> AddedNodeValues;
            AddedNodeValues.Reserve(AddedNodeIds.Num());
            for (const FString& Token : AddedNodeIds)
            {
                AddedNodeValues.Add(MakeShared<FJsonValueString>(Token));
            }
            Summary->SetArrayField(TEXT("addedNodeIds"), AddedNodeValues);
        }
        {
            TArray<TSharedPtr<FJsonValue>> RemovedEdgeValues;
            RemovedEdgeValues.Reserve(RemovedEdges.Num());
            for (const FString& Token : RemovedEdges)
            {
                RemovedEdgeValues.Add(MakeShared<FJsonValueString>(Token));
            }
            Summary->SetArrayField(TEXT("removedEdges"), RemovedEdgeValues);
        }
        {
            TArray<TSharedPtr<FJsonValue>> AddedEdgeValues;
            AddedEdgeValues.Reserve(AddedEdges.Num());
            for (const FString& Token : AddedEdges)
            {
                AddedEdgeValues.Add(MakeShared<FJsonValueString>(Token));
            }
            Summary->SetArrayField(TEXT("addedEdges"), AddedEdgeValues);
        }

        TSharedPtr<FJsonObject> EventData = MakeShared<FJsonObject>();
        EventData->SetStringField(TEXT("graphType"), TEXT("blueprint"));
        EventData->SetStringField(TEXT("assetPath"), AssetPath);
        EventData->SetStringField(TEXT("graphName"), TEXT("EventGraph"));
        EventData->SetStringField(TEXT("sourceKind"), TEXT("editor.manual"));
        EventData->SetObjectField(TEXT("summary"), Summary);
        SendEditorStreamEvent(TEXT("graph.changed"), EventData);

        LastBlueprintGraphSignatureByAssetPath.Add(AssetPath, SignatureAfter);
        LastBlueprintGraphNodeCountByAssetPath.Add(AssetPath, NodeCountAfter);
        LastBlueprintGraphEdgeCountByAssetPath.Add(AssetPath, EdgeCountAfter);
        LastBlueprintGraphNodeTokensByAssetPath.Add(AssetPath, NodeTokensAfter);
        LastBlueprintGraphEdgeTokensByAssetPath.Add(AssetPath, EdgeTokensAfter);
    }
}

void FLoomleBridgeModule::MarkBlueprintGraphDirty(const FString& BlueprintAssetPath, const FString& Reason)
{
    if (BlueprintAssetPath.IsEmpty())
    {
        return;
    }

    PendingDirtyBlueprintGraphReasons.FindOrAdd(BlueprintAssetPath) = Reason;
}

bool FLoomleBridgeModule::CaptureBlueprintGraphSnapshot(
    const FString& BlueprintAssetPath,
    FString& OutSignature,
    int32& OutNodeCount,
    int32& OutEdgeCount,
    TArray<FString>& OutNodeTokens,
    TArray<FString>& OutEdgeTokens) const
{
    OutSignature.Empty();
    OutNodeCount = 0;
    OutEdgeCount = 0;
    OutNodeTokens.Reset();
    OutEdgeTokens.Reset();

    FString NodesJson;
    FString Error;
    if (!ULoomleBlueprintAdapter::ListEventGraphNodes(BlueprintAssetPath, NodesJson, Error))
    {
        return false;
    }

    TArray<TSharedPtr<FJsonValue>> Nodes;
    {
        TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(NodesJson);
        if (!FJsonSerializer::Deserialize(Reader, Nodes))
        {
            return false;
        }
    }

    OutNodeTokens.Reserve(Nodes.Num());

    for (const TSharedPtr<FJsonValue>& NodeValue : Nodes)
    {
        const TSharedPtr<FJsonObject>* NodeObj = nullptr;
        if (!NodeValue.IsValid() || !NodeValue->TryGetObject(NodeObj) || !NodeObj || !(*NodeObj).IsValid())
        {
            continue;
        }

        FString NodeId;
        if (!(*NodeObj)->TryGetStringField(TEXT("guid"), NodeId) || NodeId.IsEmpty())
        {
            continue;
        }
        OutNodeTokens.Add(NodeId);

        const TArray<TSharedPtr<FJsonValue>>* Pins = nullptr;
        if (!(*NodeObj)->TryGetArrayField(TEXT("pins"), Pins) || !Pins)
        {
            continue;
        }

        for (const TSharedPtr<FJsonValue>& PinValue : *Pins)
        {
            const TSharedPtr<FJsonObject>* PinObj = nullptr;
            if (!PinValue.IsValid() || !PinValue->TryGetObject(PinObj) || !PinObj || !(*PinObj).IsValid())
            {
                continue;
            }

            FString FromPin;
            (*PinObj)->TryGetStringField(TEXT("name"), FromPin);

            const TArray<TSharedPtr<FJsonValue>>* Linked = nullptr;
            if (!(*PinObj)->TryGetArrayField(TEXT("linkedTo"), Linked) || !Linked)
            {
                continue;
            }

            for (const TSharedPtr<FJsonValue>& LinkValue : *Linked)
            {
                const TSharedPtr<FJsonObject>* LinkObj = nullptr;
                if (!LinkValue.IsValid() || !LinkValue->TryGetObject(LinkObj) || !LinkObj || !(*LinkObj).IsValid())
                {
                    continue;
                }

                FString ToNodeId;
                FString ToPin;
                (*LinkObj)->TryGetStringField(TEXT("nodeGuid"), ToNodeId);
                (*LinkObj)->TryGetStringField(TEXT("pin"), ToPin);
                if (ToNodeId.IsEmpty() || ToPin.IsEmpty() || FromPin.IsEmpty())
                {
                    continue;
                }

                OutEdgeTokens.Add(NodeId + TEXT("|") + FromPin + TEXT("->") + ToNodeId + TEXT("|") + ToPin);
            }
        }
    }

    Algo::Sort(OutNodeTokens);
    Algo::Sort(OutEdgeTokens);

    OutNodeCount = OutNodeTokens.Num();
    OutEdgeCount = OutEdgeTokens.Num();
    OutSignature = FString::Join(OutNodeTokens, TEXT(";")) + TEXT("#") + FString::Join(OutEdgeTokens, TEXT(";"));
    return true;
}

bool FLoomleBridgeModule::TryResolveBlueprintAssetPath(UObject* Object, FString& OutBlueprintAssetPath)
{
    OutBlueprintAssetPath.Empty();
    if (!Object)
    {
        return false;
    }

    UBlueprint* Blueprint = Cast<UBlueprint>(Object);
    if (!Blueprint)
    {
        if (UEdGraph* GraphObject = Cast<UEdGraph>(Object))
        {
            Blueprint = Cast<UBlueprint>(GraphObject->GetOuter());
        }
        else if (UEdGraphNode* Node = Cast<UEdGraphNode>(Object))
        {
            if (UEdGraph* NodeGraph = Node->GetGraph())
            {
                Blueprint = Cast<UBlueprint>(NodeGraph->GetOuter());
            }
        }
    }

    if (!Blueprint)
    {
        UObject* Outer = Object->GetOuter();
        while (Outer && !Blueprint)
        {
            Blueprint = Cast<UBlueprint>(Outer);
            Outer = Outer->GetOuter();
        }
    }

    if (!Blueprint || !Blueprint->GetOutermost())
    {
        return false;
    }

    OutBlueprintAssetPath = Blueprint->GetOutermost()->GetName();
    return !OutBlueprintAssetPath.IsEmpty() && FPackageName::IsValidLongPackageName(OutBlueprintAssetPath);
}

void FLoomleBridgeModule::OnMapOpened(const FString& Filename, bool bAsTemplate)
{
    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetStringField(TEXT("filename"), Filename);
    Data->SetStringField(TEXT("mapName"), FPaths::GetBaseFilename(Filename));
    Data->SetBoolField(TEXT("asTemplate"), bAsTemplate);
    SendEditorStreamEvent(TEXT("map_opened"), Data);
}

void FLoomleBridgeModule::OnActorMoved(AActor* Actor)
{
    if (!Actor)
    {
        return;
    }

    const FString ActorPath = Actor->GetPathName();
    const FTransform CurrentTransform(Actor->GetActorRotation(), Actor->GetActorLocation(), Actor->GetActorScale3D());
    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetStringField(TEXT("name"), Actor->GetActorLabel());
    Data->SetStringField(TEXT("path"), ActorPath);
    Data->SetObjectField(TEXT("currentTransform"), ToTransformJson(CurrentTransform));

    if (const FTransform* PreviousTransform = LastActorTransformByPath.Find(ActorPath))
    {
        Data->SetObjectField(TEXT("previousTransform"), ToTransformJson(*PreviousTransform));

        TSharedPtr<FJsonObject> Delta = MakeShared<FJsonObject>();
        Delta->SetObjectField(TEXT("location"), ToVectorJson(CurrentTransform.GetLocation() - PreviousTransform->GetLocation()));
        Delta->SetObjectField(TEXT("rotation"), ToRotatorJson((CurrentTransform.Rotator() - PreviousTransform->Rotator()).GetNormalized()));
        Delta->SetObjectField(TEXT("scale"), ToVectorJson(CurrentTransform.GetScale3D() - PreviousTransform->GetScale3D()));
        Data->SetObjectField(TEXT("delta"), Delta);
    }

    LastActorTransformByPath.Add(ActorPath, CurrentTransform);

    SendEditorStreamEvent(TEXT("actor_moved"), Data);
}

void FLoomleBridgeModule::OnActorAdded(AActor* Actor)
{
    if (!Actor)
    {
        return;
    }

    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetStringField(TEXT("name"), Actor->GetActorLabel());
    Data->SetStringField(TEXT("path"), Actor->GetPathName());
    Data->SetStringField(TEXT("class"), Actor->GetClass() ? Actor->GetClass()->GetPathName() : TEXT(""));
    Data->SetObjectField(TEXT("initialTransform"), ToTransformJson(FTransform(Actor->GetActorRotation(), Actor->GetActorLocation(), Actor->GetActorScale3D())));
    LastActorTransformByPath.Add(Actor->GetPathName(), FTransform(Actor->GetActorRotation(), Actor->GetActorLocation(), Actor->GetActorScale3D()));
    TArray<USceneComponent*> SceneComponents;
    Actor->GetComponents<USceneComponent>(SceneComponents);
    for (USceneComponent* SceneComponent : SceneComponents)
    {
        if (SceneComponent)
        {
            LastSceneComponentRelativeTransformByPath.Add(
                SceneComponent->GetPathName(),
                FTransform(SceneComponent->GetRelativeRotation(), SceneComponent->GetRelativeLocation(), SceneComponent->GetRelativeScale3D()));
        }
    }
    SendEditorStreamEvent(TEXT("actor_added"), Data);
}

void FLoomleBridgeModule::OnActorDeleted(AActor* Actor)
{
    if (!Actor)
    {
        return;
    }

    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetStringField(TEXT("name"), Actor->GetActorLabel());
    Data->SetStringField(TEXT("path"), Actor->GetPathName());
    if (const FTransform* PreviousTransform = LastActorTransformByPath.Find(Actor->GetPathName()))
    {
        Data->SetObjectField(TEXT("lastKnownTransform"), ToTransformJson(*PreviousTransform));
    }
    const FString ActorPath = Actor->GetPathName();
    LastActorTransformByPath.Remove(ActorPath);
    for (auto It = LastSceneComponentRelativeTransformByPath.CreateIterator(); It; ++It)
    {
        if (It.Key().StartsWith(ActorPath + TEXT(".")))
        {
            It.RemoveCurrent();
        }
    }
    SendEditorStreamEvent(TEXT("actor_deleted"), Data);
}

void FLoomleBridgeModule::OnActorAttached(AActor* Actor, const AActor* ParentActor)
{
    if (!Actor)
    {
        return;
    }

    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetStringField(TEXT("name"), Actor->GetActorLabel());
    Data->SetStringField(TEXT("path"), Actor->GetPathName());
    Data->SetStringField(TEXT("parentPath"), ParentActor ? ParentActor->GetPathName() : TEXT(""));
    SendEditorStreamEvent(TEXT("actor_attached"), Data);
}

void FLoomleBridgeModule::OnActorDetached(AActor* Actor, const AActor* ParentActor)
{
    if (!Actor)
    {
        return;
    }

    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetStringField(TEXT("name"), Actor->GetActorLabel());
    Data->SetStringField(TEXT("path"), Actor->GetPathName());
    Data->SetStringField(TEXT("formerParentPath"), ParentActor ? ParentActor->GetPathName() : TEXT(""));
    SendEditorStreamEvent(TEXT("actor_detached"), Data);
}

void FLoomleBridgeModule::OnBeginPIE(bool bIsSimulating)
{
    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetBoolField(TEXT("isSimulating"), bIsSimulating);
    SendEditorStreamEvent(TEXT("pie_started"), Data);
}

void FLoomleBridgeModule::OnEndPIE(bool bIsSimulating)
{
    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetBoolField(TEXT("isSimulating"), bIsSimulating);
    SendEditorStreamEvent(TEXT("pie_stopped"), Data);
}

void FLoomleBridgeModule::OnPausePIE(bool bIsSimulating)
{
    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetBoolField(TEXT("isSimulating"), bIsSimulating);
    SendEditorStreamEvent(TEXT("pie_paused"), Data);
}

void FLoomleBridgeModule::OnResumePIE(bool bIsSimulating)
{
    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetBoolField(TEXT("isSimulating"), bIsSimulating);
    SendEditorStreamEvent(TEXT("pie_resumed"), Data);
}

void FLoomleBridgeModule::OnObjectPropertyChanged(UObject* Object, FPropertyChangedEvent& PropertyChangedEvent)
{
    if (!Object)
    {
        return;
    }

    FString BlueprintAssetPath;
    if (!bGraphMutateInProgress && TryResolveBlueprintAssetPath(Object, BlueprintAssetPath))
    {
        MarkBlueprintGraphDirty(BlueprintAssetPath, TEXT("property_change"));
    }

    AActor* Actor = Cast<AActor>(Object);
    if (!Actor)
    {
        if (const UActorComponent* Component = Cast<UActorComponent>(Object))
        {
            Actor = Component->GetOwner();
        }
    }

    if (!Actor)
    {
        return;
    }

    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetStringField(TEXT("actorName"), Actor->GetActorLabel());
    Data->SetStringField(TEXT("actorPath"), Actor->GetPathName());
    Data->SetStringField(TEXT("objectPath"), Object->GetPathName());
    Data->SetStringField(TEXT("objectClass"), Object->GetClass() ? Object->GetClass()->GetPathName() : TEXT(""));
    Data->SetStringField(TEXT("property"), PropertyChangedEvent.GetPropertyName().ToString());
    Data->SetStringField(TEXT("memberProperty"), PropertyChangedEvent.GetMemberPropertyName().ToString());
    Data->SetNumberField(TEXT("changeType"), static_cast<int32>(PropertyChangedEvent.ChangeType));

    // Only export value from class-level member property. Nested struct leaf properties
    // (for example RelativeScale3D.Z) may not be directly addressable from UObject container.
    if (FProperty* SafeValueProperty = PropertyChangedEvent.MemberProperty)
    {
        bool bCanExportInContainer = false;
        if (const UObject* OwnerObject = SafeValueProperty->GetOwnerUObject())
        {
            if (const UClass* OwnerClass = Cast<UClass>(OwnerObject))
            {
                bCanExportInContainer = Object->IsA(OwnerClass);
            }
            else
            {
                bCanExportInContainer = true;
            }
        }

        if (bCanExportInContainer)
        {
            FString NewValue;
            SafeValueProperty->ExportText_InContainer(0, NewValue, Object, Object, Object, PPF_None);
            if (!NewValue.IsEmpty())
            {
                Data->SetStringField(TEXT("newValue"), NewValue);
            }
        }
    }

    const FString ActorPath = Actor->GetPathName();
    const FTransform CurrentActorTransform(Actor->GetActorRotation(), Actor->GetActorLocation(), Actor->GetActorScale3D());
    Data->SetObjectField(TEXT("actorCurrentTransform"), ToTransformJson(CurrentActorTransform));
    if (const FTransform* PreviousActorTransform = LastActorTransformByPath.Find(ActorPath))
    {
        Data->SetObjectField(TEXT("actorPreviousTransform"), ToTransformJson(*PreviousActorTransform));
    }
    LastActorTransformByPath.Add(ActorPath, CurrentActorTransform);

    if (USceneComponent* SceneComponent = Cast<USceneComponent>(Object))
    {
        const FString ComponentPath = SceneComponent->GetPathName();
        const FTransform CurrentRelative(
            SceneComponent->GetRelativeRotation(),
            SceneComponent->GetRelativeLocation(),
            SceneComponent->GetRelativeScale3D());
        Data->SetObjectField(TEXT("componentCurrentRelativeTransform"), ToTransformJson(CurrentRelative));

        if (const FTransform* PreviousRelative = LastSceneComponentRelativeTransformByPath.Find(ComponentPath))
        {
            Data->SetObjectField(TEXT("componentPreviousRelativeTransform"), ToTransformJson(*PreviousRelative));

            TSharedPtr<FJsonObject> Delta = MakeShared<FJsonObject>();
            Delta->SetObjectField(TEXT("location"), ToVectorJson(CurrentRelative.GetLocation() - PreviousRelative->GetLocation()));
            Delta->SetObjectField(TEXT("rotation"), ToRotatorJson((CurrentRelative.Rotator() - PreviousRelative->Rotator()).GetNormalized()));
            Delta->SetObjectField(TEXT("scale"), ToVectorJson(CurrentRelative.GetScale3D() - PreviousRelative->GetScale3D()));
            Data->SetObjectField(TEXT("componentDelta"), Delta);
        }

        LastSceneComponentRelativeTransformByPath.Add(ComponentPath, CurrentRelative);
    }

    SendEditorStreamEvent(TEXT("object_property_changed"), Data);
}

void FLoomleBridgeModule::OnObjectTransacted(UObject* Object, const FTransactionObjectEvent& TransactionEvent)
{
    if (!Object || bGraphMutateInProgress)
    {
        return;
    }

    FString BlueprintAssetPath;
    if (!TryResolveBlueprintAssetPath(Object, BlueprintAssetPath))
    {
        return;
    }

    const ETransactionObjectEventType EventType = TransactionEvent.GetEventType();
    if (EventType == ETransactionObjectEventType::Finalized)
    {
        return;
    }

    MarkBlueprintGraphDirty(BlueprintAssetPath, TEXT("manual_edit"));
}

void FLoomleBridgeModule::OnPostUndoRedo()
{
    if (UBlueprint* EditedBlueprint = FindEditedBlueprint())
    {
        const FString BlueprintAssetPath = EditedBlueprint->GetOutermost() ? EditedBlueprint->GetOutermost()->GetName() : FString();
        if (!BlueprintAssetPath.IsEmpty())
        {
            MarkBlueprintGraphDirty(BlueprintAssetPath, TEXT("undo_redo"));
        }
    }

    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    SendEditorStreamEvent(TEXT("undo_redo"), Data);
}

TSharedPtr<FJsonObject> FLoomleBridgeModule::BuildExecutePythonToolResult(const TSharedPtr<FJsonObject>& Arguments) const
{
    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

    FString Code;
    if (!Arguments->TryGetStringField(TEXT("code"), Code))
    {
        Result->SetBoolField(TEXT("isError"), true);
        Result->SetStringField(TEXT("message"), TEXT("arguments.code is required."));
        return Result;
    }

    FString Mode = TEXT("exec");
    Arguments->TryGetStringField(TEXT("mode"), Mode);
    Mode = Mode.ToLower();
    if (!Mode.Equals(TEXT("exec")) && !Mode.Equals(TEXT("eval")))
    {
        Result->SetBoolField(TEXT("isError"), true);
        Result->SetStringField(TEXT("message"), TEXT("arguments.mode must be 'exec' or 'eval'."));
        return Result;
    }

    IPythonScriptPlugin* PythonScriptPlugin = IPythonScriptPlugin::Get();
    if (PythonScriptPlugin == nullptr)
    {
        Result->SetBoolField(TEXT("isError"), true);
        Result->SetStringField(TEXT("message"), TEXT("PythonScriptPlugin module is not loaded."));
        return Result;
    }

    if (!PythonScriptPlugin->IsPythonInitialized())
    {
        PythonScriptPlugin->ForceEnablePythonAtRuntime();
        if (!PythonScriptPlugin->IsPythonInitialized())
        {
            Result->SetBoolField(TEXT("isError"), true);
            Result->SetStringField(TEXT("message"), TEXT("Python runtime is not initialized."));
            return Result;
        }
    }

    FPythonCommandEx PythonCommand;
    PythonCommand.ExecutionMode = Mode.Equals(TEXT("eval"))
        ? EPythonCommandExecutionMode::EvaluateStatement
        : EPythonCommandExecutionMode::ExecuteFile;
    PythonCommand.Command = Code;

    const bool bSuccess = PythonScriptPlugin->ExecPythonCommandEx(PythonCommand);
    Result->SetBoolField(TEXT("isError"), !bSuccess);
    Result->SetBoolField(TEXT("success"), bSuccess);
    Result->SetStringField(TEXT("mode"), Mode);
    Result->SetStringField(TEXT("result"), PythonCommand.CommandResult);

    TArray<TSharedPtr<FJsonValue>> Logs;
    for (const FPythonLogOutputEntry& Entry : PythonCommand.LogOutput)
    {
        TSharedPtr<FJsonObject> LogEntry = MakeShared<FJsonObject>();
        LogEntry->SetStringField(TEXT("type"), LexToString(Entry.Type));
        LogEntry->SetStringField(TEXT("output"), Entry.Output);
        Logs.Add(MakeShared<FJsonValueObject>(LogEntry));
    }
    Result->SetArrayField(TEXT("logs"), Logs);

    return Result;
}

FString FLoomleBridgeModule::MakeJsonResponse(const TSharedPtr<FJsonValue>& Id, const TSharedPtr<FJsonObject>& Result) const
{
    TSharedPtr<FJsonObject> Response = MakeShared<FJsonObject>();
    Response->SetStringField(TEXT("jsonrpc"), TEXT("2.0"));
    Response->SetField(TEXT("id"), Id);
    Response->SetObjectField(TEXT("result"), Result);

    FString Output;
    TSharedRef<FCondensedJsonWriter> Writer = TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Output);
    FJsonSerializer::Serialize(Response.ToSharedRef(), Writer);
    return Output;
}

FString FLoomleBridgeModule::MakeJsonError(const TSharedPtr<FJsonValue>& Id, int32 Code, const FString& Message) const
{
    TSharedPtr<FJsonObject> Error = MakeShared<FJsonObject>();
    Error->SetNumberField(TEXT("code"), Code);
    Error->SetStringField(TEXT("message"), Message);

    TSharedPtr<FJsonObject> Response = MakeShared<FJsonObject>();
    Response->SetStringField(TEXT("jsonrpc"), TEXT("2.0"));
    Response->SetField(TEXT("id"), Id);
    Response->SetObjectField(TEXT("error"), Error);

    FString Output;
    TSharedRef<FCondensedJsonWriter> Writer = TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Output);
    FJsonSerializer::Serialize(Response.ToSharedRef(), Writer);
    return Output;
}

IMPLEMENT_MODULE(FLoomleBridgeModule, LoomleBridge)
