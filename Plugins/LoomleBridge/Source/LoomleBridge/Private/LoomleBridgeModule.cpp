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
#include "EdGraph/EdGraphSchema.h"
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
#include "EdGraphSchema_K2.h"
#include "BlueprintActionFilter.h"
#include "BlueprintActionMenuBuilder.h"
#include "BlueprintActionMenuUtils.h"
#include "Engine/Blueprint.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpression.h"
#include "MaterialEditingLibrary.h"
#include "IMaterialEditor.h"
#include "MaterialGraph/MaterialGraphNode.h"
#include "PCGGraph.h"
#include "PCGEdge.h"
#include "PCGNode.h"
#include "PCGPin.h"
#include "PCGSettings.h"
#include "LoomleBlueprintAdapter.h"
#include "LoomlePipeServer.h"
#include "Misc/App.h"
#include "Misc/Base64.h"
#include "Misc/DateTime.h"
#include "Misc/EngineVersion.h"
#include "Misc/FileHelper.h"
#include "Misc/Guid.h"
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
    static const TCHAR* ExecuteToolName = TEXT("execute");
    static const TCHAR* GraphToolName = TEXT("graph");
    static const TCHAR* GraphListToolName = TEXT("graph.list");
    static const TCHAR* GraphQueryToolName = TEXT("graph.query");
    static const TCHAR* GraphAddableToolName = TEXT("graph.addable");
    static const TCHAR* GraphMutateToolName = TEXT("graph.mutate");
    constexpr double GraphActionTokenTtlSeconds = 300.0;
    constexpr int32 MaxGraphActionTokenRegistryEntries = 2048;
}

namespace
{
FString NormalizeGraphType(FString GraphType)
{
    GraphType = GraphType.TrimStartAndEnd().ToLower();
    if (GraphType.IsEmpty())
    {
        return TEXT("blueprint");
    }
    if (GraphType.Equals(TEXT("shader")))
    {
        return TEXT("material");
    }
    return GraphType;
}

FString GetGraphTypeFromArgs(const TSharedPtr<FJsonObject>& Arguments)
{
    FString GraphType = TEXT("blueprint");
    if (Arguments.IsValid())
    {
        Arguments->TryGetStringField(TEXT("graphType"), GraphType);
    }
    return NormalizeGraphType(GraphType);
}

bool IsSupportedGraphType(const FString& GraphType)
{
    return GraphType.Equals(TEXT("blueprint"))
        || GraphType.Equals(TEXT("material"))
        || GraphType.Equals(TEXT("pcg"));
}

FString NormalizeAssetPath(const FString& InAssetPath)
{
    FString AssetPath = InAssetPath;
    const int32 DotIndex = AssetPath.Find(TEXT("."));
    if (DotIndex > 0)
    {
        AssetPath = AssetPath.Left(DotIndex);
    }
    return AssetPath;
}

FString NormalizeBlueprintAssetPath(const FString& InAssetPath)
{
    return NormalizeAssetPath(InAssetPath);
}

UBlueprint* LoadBlueprintByAssetPath(const FString& InAssetPath)
{
    const FString AssetPath = NormalizeBlueprintAssetPath(InAssetPath);
    if (!FPackageName::IsValidLongPackageName(AssetPath))
    {
        return nullptr;
    }

    const FString AssetName = FPackageName::GetLongPackageAssetName(AssetPath);
    const FString ObjectPath = FString::Printf(TEXT("%s.%s"), *AssetPath, *AssetName);
    return LoadObject<UBlueprint>(nullptr, *ObjectPath);
}

UMaterial* LoadMaterialByAssetPath(const FString& InAssetPath)
{
    const FString AssetPath = NormalizeAssetPath(InAssetPath);
    if (!FPackageName::IsValidLongPackageName(AssetPath))
    {
        return nullptr;
    }

    const FString AssetName = FPackageName::GetLongPackageAssetName(AssetPath);
    const FString ObjectPath = FString::Printf(TEXT("%s.%s"), *AssetPath, *AssetName);
    return LoadObject<UMaterial>(nullptr, *ObjectPath);
}

UObject* LoadObjectByAssetPath(const FString& InAssetPath)
{
    const FString AssetPath = NormalizeAssetPath(InAssetPath);
    if (!FPackageName::IsValidLongPackageName(AssetPath))
    {
        return nullptr;
    }

    const FString AssetName = FPackageName::GetLongPackageAssetName(AssetPath);
    const FString ObjectPath = FString::Printf(TEXT("%s.%s"), *AssetPath, *AssetName);
    return LoadObject<UObject>(nullptr, *ObjectPath);
}

bool IsLikelyPcgAsset(const UObject* Asset)
{
    if (Asset == nullptr || Asset->GetClass() == nullptr)
    {
        return false;
    }

    const FString ClassPath = Asset->GetClass()->GetPathName();
    const FString ClassName = Asset->GetClass()->GetName();
    return ClassPath.Contains(TEXT("PCGGraph"))
        || ClassPath.Contains(TEXT("/PCG."))
        || ClassName.Contains(TEXT("PCGGraph"));
}

UPCGGraph* ResolvePcgGraphFromAsset(UObject* Asset)
{
    if (Asset == nullptr)
    {
        return nullptr;
    }

    if (UPCGGraph* Graph = Cast<UPCGGraph>(Asset))
    {
        return Graph;
    }

    if (UPCGGraphInterface* GraphInterface = Cast<UPCGGraphInterface>(Asset))
    {
        return GraphInterface->GetMutablePCGGraph();
    }

    return nullptr;
}

UPCGGraph* LoadPcgGraphByAssetPath(const FString& InAssetPath)
{
    return ResolvePcgGraphFromAsset(LoadObjectByAssetPath(InAssetPath));
}

UPCGNode* FindPcgNodeById(UPCGGraph* Graph, const FString& NodeId)
{
    if (Graph == nullptr)
    {
        return nullptr;
    }

    for (UPCGNode* Node : Graph->GetNodes())
    {
        if (Node == nullptr)
        {
            continue;
        }

        if (Node->GetPathName().Equals(NodeId)
            || Node->GetName().Equals(NodeId)
            || Node->NodeTitle.ToString().Equals(NodeId, ESearchCase::IgnoreCase))
        {
            return Node;
        }
    }

    return nullptr;
}

UPCGPin* FindPcgPin(UPCGNode* Node, const FString& PinName, bool bOutputPin)
{
    if (Node == nullptr || PinName.IsEmpty())
    {
        return nullptr;
    }

    const TArray<TObjectPtr<UPCGPin>>& Pins = bOutputPin ? Node->GetOutputPins() : Node->GetInputPins();
    for (UPCGPin* Pin : Pins)
    {
        if (Pin == nullptr)
        {
            continue;
        }

        const FString Label = Pin->Properties.Label.ToString();
        if (Label.Equals(PinName, ESearchCase::IgnoreCase))
        {
            return Pin;
        }
    }

    return nullptr;
}

UMaterialExpression* FindMaterialExpressionById(UMaterial* Material, const FString& NodeId)
{
    if (Material == nullptr || NodeId.IsEmpty())
    {
        return nullptr;
    }

    for (UMaterialExpression* Expression : Material->GetExpressions())
    {
        if (Expression == nullptr)
        {
            continue;
        }

        if (Expression->GetPathName().Equals(NodeId)
            || Expression->GetName().Equals(NodeId))
        {
            return Expression;
        }
    }
    return nullptr;
}

FString MaterialExpressionId(const UMaterialExpression* Expression)
{
    return Expression ? Expression->GetPathName() : FString();
}

int32 FindMaterialInputIndexByName(UMaterialExpression* Expression, const FString& PinName)
{
    if (Expression == nullptr)
    {
        return INDEX_NONE;
    }

    if (PinName.IsEmpty())
    {
        return 0;
    }

    const int32 MaxInputs = 128;
    for (int32 Index = 0; Index < MaxInputs; ++Index)
    {
        FExpressionInput* Input = Expression->GetInput(Index);
        if (Input == nullptr)
        {
            break;
        }

        const FName InputName = Expression->GetInputName(Index);
        if (InputName.ToString().Equals(PinName, ESearchCase::IgnoreCase))
        {
            return Index;
        }
    }

    return INDEX_NONE;
}

UEdGraph* ResolveBlueprintGraph(UBlueprint* Blueprint, const FString& GraphName)
{
    if (Blueprint == nullptr)
    {
        return nullptr;
    }

    auto MatchGraphName = [&GraphName](UEdGraph* Graph) -> bool
    {
        return Graph != nullptr && (Graph->GetName().Equals(GraphName) || Graph->GetFName().ToString().Equals(GraphName));
    };

    const FString EffectiveGraphName = GraphName.IsEmpty() ? TEXT("EventGraph") : GraphName;
    for (UEdGraph* Graph : Blueprint->UbergraphPages)
    {
        if (MatchGraphName(Graph))
        {
            return Graph;
        }
    }
    for (UEdGraph* Graph : Blueprint->FunctionGraphs)
    {
        if (MatchGraphName(Graph))
        {
            return Graph;
        }
    }
    for (UEdGraph* Graph : Blueprint->MacroGraphs)
    {
        if (MatchGraphName(Graph))
        {
            return Graph;
        }
    }
    for (const FBPInterfaceDescription& InterfaceDesc : Blueprint->ImplementedInterfaces)
    {
        for (UEdGraph* Graph : InterfaceDesc.Graphs)
        {
            if (MatchGraphName(Graph))
            {
                return Graph;
            }
        }
    }

    if (EffectiveGraphName.Equals(TEXT("EventGraph"), ESearchCase::IgnoreCase))
    {
        return FBlueprintEditorUtils::FindEventGraph(Blueprint);
    }
    return nullptr;
}

UEdGraphNode* FindNodeByGuid(UEdGraph* Graph, const FString& NodeGuidText)
{
    if (Graph == nullptr)
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
        if (Node != nullptr && Node->NodeGuid == NodeGuid)
        {
            return Node;
        }
    }
    return nullptr;
}

UEdGraphPin* FindPinByName(UEdGraphNode* Node, const FString& PinName)
{
    if (Node == nullptr || PinName.IsEmpty())
    {
        return nullptr;
    }
    return Node->FindPin(*PinName);
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

UObject* FindEditedPcgAsset()
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
    UObject* FallbackAsset = nullptr;

    const TArray<UObject*> EditedAssets = AssetEditorSubsystem->GetAllEditedAssets();
    for (UObject* Asset : EditedAssets)
    {
        if (!IsLikelyPcgAsset(Asset))
        {
            continue;
        }

        if (!FallbackAsset)
        {
            FallbackAsset = Asset;
        }

        if (!ActiveWindowTitle.IsEmpty() && ActiveWindowTitle.Contains(Asset->GetName(), ESearchCase::IgnoreCase))
        {
            return Asset;
        }
    }

    return ActiveWindowTitle.IsEmpty() ? FallbackAsset : nullptr;
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

bool BuildPcgContextSnapshot(TSharedPtr<FJsonObject>& OutContext)
{
    OutContext.Reset();
    UObject* PcgAsset = FindEditedPcgAsset();
    if (!PcgAsset)
    {
        return false;
    }

    OutContext = MakeShared<FJsonObject>();
    OutContext->SetBoolField(TEXT("isError"), false);
    OutContext->SetStringField(TEXT("editorType"), TEXT("pcg"));
    OutContext->SetStringField(TEXT("provider"), TEXT("pcg"));
    OutContext->SetStringField(TEXT("assetName"), PcgAsset->GetName());
    OutContext->SetStringField(TEXT("assetPath"), PcgAsset->GetPathName());
    OutContext->SetStringField(TEXT("assetClass"), PcgAsset->GetClass() ? PcgAsset->GetClass()->GetPathName() : TEXT(""));
    OutContext->SetStringField(TEXT("status"), TEXT("active"));
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

    bGraphMutateInProgress = false;
}

void FLoomleBridgeModule::ShutdownModule()
{
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
            GraphTypeProperty->SetStringField(TEXT("description"), TEXT("Graph type descriptor. Supported: blueprint, material(shader), pcg. Default: blueprint."));
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
        Tools.Add(MakeTool(LoomleBridgeConstants::GraphListToolName, TEXT("List readable graph names in a graph asset."), Schema));
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

            TSharedPtr<FJsonObject> ContextProperty = MakeShared<FJsonObject>();
            ContextProperty->SetStringField(TEXT("type"), TEXT("object"));
            Properties->SetObjectField(TEXT("context"), ContextProperty);

            TSharedPtr<FJsonObject> QueryProperty = MakeShared<FJsonObject>();
            QueryProperty->SetStringField(TEXT("type"), TEXT("string"));
            Properties->SetObjectField(TEXT("query"), QueryProperty);

            TSharedPtr<FJsonObject> LimitProperty = MakeShared<FJsonObject>();
            LimitProperty->SetStringField(TEXT("type"), TEXT("number"));
            Properties->SetObjectField(TEXT("limit"), LimitProperty);
        }
        Schema->SetObjectField(TEXT("properties"), Properties);
        Tools.Add(MakeTool(LoomleBridgeConstants::GraphAddableToolName, TEXT("List graph actions addable in current graph/pin context."), Schema));
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

    TSharedPtr<FJsonObject> Arguments = MakeShared<FJsonObject>();
    const TSharedPtr<FJsonObject>* ArgumentsPtr = nullptr;
    if (Params->TryGetObjectField(TEXT("arguments"), ArgumentsPtr) && ArgumentsPtr && (*ArgumentsPtr).IsValid())
    {
        Arguments = *ArgumentsPtr;
    }
    else
    {
        FString ArgumentsJson;
        if (Params->TryGetStringField(TEXT("arguments"), ArgumentsJson) && !ArgumentsJson.IsEmpty())
        {
            const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(ArgumentsJson);
            TSharedPtr<FJsonObject> ParsedArguments;
            if (FJsonSerializer::Deserialize(Reader, ParsedArguments) && ParsedArguments.IsValid())
            {
                Arguments = ParsedArguments;
            }
        }
    }

    // Some wrappers flatten execute arguments into params.{code,mode}; recover those fields.
    if (Name.Equals(LoomleBridgeConstants::ExecuteToolName) && !Arguments->HasField(TEXT("code")))
    {
        FString FlatCode;
        if (Params->TryGetStringField(TEXT("code"), FlatCode) && !FlatCode.IsEmpty())
        {
            Arguments->SetStringField(TEXT("code"), FlatCode);
        }

        FString FlatMode;
        if (Params->TryGetStringField(TEXT("mode"), FlatMode) && !FlatMode.IsEmpty())
        {
            Arguments->SetStringField(TEXT("mode"), FlatMode);
        }
    }

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
    else if (Name.Equals(LoomleBridgeConstants::GraphAddableToolName))
    {
        Payload = BuildGraphAddableToolResult(Arguments);
        bIsError = Payload->GetBoolField(TEXT("isError"));
    }
    else if (Name.Equals(LoomleBridgeConstants::GraphMutateToolName))
    {
        Payload = BuildGraphMutateToolResult(Arguments);
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
        else
        {
            TSharedPtr<FJsonObject> PcgContext;
            if (BuildPcgContextSnapshot(PcgContext) && PcgContext.IsValid())
            {
                Context = PcgContext;
                Context->SetStringField(TEXT("source"), TEXT("unified"));
            }
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
    IPythonScriptPlugin* PythonScriptPlugin = IPythonScriptPlugin::Get();
    const bool bPythonModuleLoaded = PythonScriptPlugin != nullptr;
    const bool bPythonReady = bPythonModuleLoaded && PythonScriptPlugin->IsPythonInitialized();

    TSharedPtr<FJsonObject> Status = MakeShared<FJsonObject>();
    Status->SetStringField(TEXT("serverName"), TEXT("Loomle Bridge"));
    Status->SetStringField(TEXT("serverVersion"), TEXT("0.1.0"));
    Status->SetStringField(TEXT("protocolVersion"), TEXT("2025-06-18"));
    Status->SetBoolField(TEXT("bridgeRunning"), bBridgeRunning);
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
    Capabilities.Add(MakeCapability(TEXT("graph"), TEXT("Graph descriptor (capabilities + schema)."), bBridgeRunning, bBridgeRunning ? TEXT("") : TEXT("Bridge server is not running.")));
    Capabilities.Add(MakeCapability(TEXT("graph.list"), TEXT("List readable graphs in a graph asset."), bBridgeRunning, bBridgeRunning ? TEXT("") : TEXT("Bridge server is not running.")));
    Capabilities.Add(MakeCapability(TEXT("graph.query"), TEXT("Graph query API."), bBridgeRunning, bBridgeRunning ? TEXT("") : TEXT("Bridge server is not running.")));
    Capabilities.Add(MakeCapability(TEXT("graph.addable"), TEXT("List addable graph actions for current context."), bBridgeRunning, bBridgeRunning ? TEXT("") : TEXT("Bridge server is not running.")));
    Capabilities.Add(MakeCapability(TEXT("graph.mutate"), TEXT("Graph mutate API."), bBridgeRunning, bBridgeRunning ? TEXT("") : TEXT("Bridge server is not running.")));
    Capabilities.Add(MakeCapability(TEXT("execute"), TEXT("Execute Python in editor."), bPythonReady, bPythonReady ? TEXT("") : TEXT("Python runtime is not initialized yet.")));
    Result->SetArrayField(TEXT("capabilities"), Capabilities);

    Result->SetStringField(
        TEXT("message"),
        bBridgeRunning ? TEXT("Loomle Bridge is running.") : TEXT("Loomle Bridge is not running. Restart Unreal Editor."));
    return Result;
}

TSharedPtr<FJsonObject> FLoomleBridgeModule::BuildGraphToolResult(const TSharedPtr<FJsonObject>& Arguments) const
{
    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetBoolField(TEXT("isError"), false);

    const FString GraphType = GetGraphTypeFromArgs(Arguments);
    if (!IsSupportedGraphType(GraphType))
    {
        Result->SetBoolField(TEXT("isError"), true);
        Result->SetStringField(TEXT("code"), TEXT("UNSUPPORTED_GRAPH_TYPE"));
        Result->SetStringField(TEXT("message"), TEXT("Supported graphType values: blueprint, material(shader), pcg."));
        return Result;
    }

    Result->SetStringField(TEXT("version"), TEXT("1.0"));
    Result->SetStringField(TEXT("graphType"), GraphType);

    TSharedPtr<FJsonObject> Features = MakeShared<FJsonObject>();
    const bool bBlueprint = GraphType.Equals(TEXT("blueprint"));
    const bool bMaterial = GraphType.Equals(TEXT("material"));
    const bool bPcg = GraphType.Equals(TEXT("pcg"));

    Features->SetBoolField(TEXT("list"), true);
    Features->SetBoolField(TEXT("query"), true);
    Features->SetBoolField(TEXT("addable"), bBlueprint || bMaterial || bPcg);
    Features->SetBoolField(TEXT("mutate"), bBlueprint || bMaterial || bPcg);
    Features->SetBoolField(TEXT("watch"), true);
    Features->SetBoolField(TEXT("revision"), true);
    Features->SetBoolField(TEXT("dryRun"), true);
    Features->SetBoolField(TEXT("transactions"), bBlueprint || bMaterial || bPcg);
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

    Result->SetArrayField(TEXT("nodeCoreFields"), ToStringArray({TEXT("id"), TEXT("nodeClassPath"), TEXT("title"), TEXT("graphName"), TEXT("position"), TEXT("enabled"), TEXT("pins")}));
    Result->SetArrayField(TEXT("pinCoreFields"), ToStringArray({TEXT("name"), TEXT("direction"), TEXT("type"), TEXT("default"), TEXT("links")}));
    if (bBlueprint)
    {
        Result->SetArrayField(TEXT("nodeExtensions"), ToStringArray({TEXT("memberReference"), TEXT("functionReference"), TEXT("k2Extensions"), TEXT("cast"), TEXT("macro"), TEXT("comment"), TEXT("timeline")}));
        Result->SetArrayField(TEXT("ops"), ToStringArray({
            TEXT("addNode.byClass"), TEXT("addNode.byAction"),
            TEXT("connectPins"), TEXT("disconnectPins"), TEXT("breakPinLinks"),
            TEXT("setPinDefault"), TEXT("removeNode"), TEXT("moveNode"),
            TEXT("compile"), TEXT("runScript")
        }));
    }
    else if (bMaterial)
    {
        Result->SetArrayField(TEXT("nodeExtensions"), ToStringArray({TEXT("materialExpression")}));
        Result->SetArrayField(TEXT("ops"), ToStringArray({
            TEXT("addNode.byClass"), TEXT("addNode.byAction"),
            TEXT("connectPins"), TEXT("disconnectPins"), TEXT("breakPinLinks"),
            TEXT("removeNode"), TEXT("moveNode"),
            TEXT("compile")
        }));
    }
    else if (bPcg)
    {
        Result->SetArrayField(TEXT("nodeExtensions"), ToStringArray({TEXT("pcgNode")}));
        Result->SetArrayField(TEXT("ops"), ToStringArray({
            TEXT("addNode.byClass"), TEXT("addNode.byAction"),
            TEXT("connectPins"), TEXT("disconnectPins"), TEXT("breakPinLinks"),
            TEXT("removeNode"), TEXT("moveNode"),
            TEXT("compile")
        }));
    }
    TSharedPtr<FJsonObject> Extensions = MakeShared<FJsonObject>();
    Extensions->SetBoolField(TEXT("scriptOp"), bBlueprint);
    if (bBlueprint)
    {
        Extensions->SetArrayField(TEXT("scriptMode"), ToStringArray({TEXT("inlineCode"), TEXT("scriptId")}));
        Extensions->SetStringField(TEXT("scriptInlineDefault"), TEXT("enabled"));
    }
    Result->SetObjectField(TEXT("extensions"), Extensions);
    return Result;
}

TSharedPtr<FJsonObject> FLoomleBridgeModule::BuildGraphListToolResult(const TSharedPtr<FJsonObject>& Arguments) const
{
    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetBoolField(TEXT("isError"), false);

    const FString GraphType = GetGraphTypeFromArgs(Arguments);
    if (!IsSupportedGraphType(GraphType))
    {
        Result->SetBoolField(TEXT("isError"), true);
        Result->SetStringField(TEXT("code"), TEXT("UNSUPPORTED_GRAPH_TYPE"));
        Result->SetStringField(TEXT("message"), TEXT("Supported graphType values: blueprint, material(shader), pcg."));
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
    AssetPath = NormalizeAssetPath(AssetPath);

    if (GraphType.Equals(TEXT("material")))
    {
        if (LoadMaterialByAssetPath(AssetPath) == nullptr)
        {
            Result->SetBoolField(TEXT("isError"), true);
            Result->SetStringField(TEXT("code"), TEXT("ASSET_NOT_FOUND"));
            Result->SetStringField(TEXT("message"), TEXT("Material asset not found."));
            return Result;
        }

        TArray<TSharedPtr<FJsonValue>> Graphs;
        TSharedPtr<FJsonObject> GraphInfo = MakeShared<FJsonObject>();
        GraphInfo->SetStringField(TEXT("graphName"), TEXT("MaterialGraph"));
        GraphInfo->SetStringField(TEXT("graphKind"), TEXT("Material"));
        GraphInfo->SetStringField(TEXT("graphClassPath"), TEXT("/Script/Engine.MaterialGraph"));
        Graphs.Add(MakeShared<FJsonValueObject>(GraphInfo));

        Result->SetStringField(TEXT("graphType"), GraphType);
        Result->SetStringField(TEXT("assetPath"), AssetPath);
        Result->SetArrayField(TEXT("graphs"), Graphs);
        Result->SetArrayField(TEXT("diagnostics"), TArray<TSharedPtr<FJsonValue>>{});
        return Result;
    }

    if (GraphType.Equals(TEXT("pcg")))
    {
        UObject* Asset = LoadObjectByAssetPath(AssetPath);
        if (!IsLikelyPcgAsset(Asset))
        {
            Result->SetBoolField(TEXT("isError"), true);
            Result->SetStringField(TEXT("code"), TEXT("ASSET_NOT_FOUND"));
            Result->SetStringField(TEXT("message"), TEXT("PCG asset not found."));
            return Result;
        }

        TArray<TSharedPtr<FJsonValue>> Graphs;
        TSharedPtr<FJsonObject> GraphInfo = MakeShared<FJsonObject>();
        GraphInfo->SetStringField(TEXT("graphName"), TEXT("PCGGraph"));
        GraphInfo->SetStringField(TEXT("graphKind"), TEXT("PCG"));
        GraphInfo->SetStringField(TEXT("graphClassPath"), Asset->GetClass() ? Asset->GetClass()->GetPathName() : TEXT(""));
        Graphs.Add(MakeShared<FJsonValueObject>(GraphInfo));

        Result->SetStringField(TEXT("graphType"), GraphType);
        Result->SetStringField(TEXT("assetPath"), AssetPath);
        Result->SetArrayField(TEXT("graphs"), Graphs);
        Result->SetArrayField(TEXT("diagnostics"), TArray<TSharedPtr<FJsonValue>>{});
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

    const FString GraphType = GetGraphTypeFromArgs(Arguments);
    if (!IsSupportedGraphType(GraphType))
    {
        Result->SetBoolField(TEXT("isError"), true);
        Result->SetStringField(TEXT("code"), TEXT("UNSUPPORTED_GRAPH_TYPE"));
        Result->SetStringField(TEXT("message"), TEXT("Supported graphType values: blueprint, material(shader), pcg."));
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
    AssetPath = NormalizeAssetPath(AssetPath);

    auto BuildMinimalSnapshotResult = [&Result, &GraphType, &AssetPath, &GraphName](const FString& RevisionPrefix, const TArray<TSharedPtr<FJsonValue>>& Nodes, const TArray<TSharedPtr<FJsonValue>>& Edges, const TArray<TSharedPtr<FJsonValue>>& Diagnostics)
    {
        TArray<FString> SignatureNodeTokens;
        TArray<FString> SignatureEdgeTokens;
        SignatureNodeTokens.Reserve(Nodes.Num());
        SignatureEdgeTokens.Reserve(Edges.Num());

        for (const TSharedPtr<FJsonValue>& NodeValue : Nodes)
        {
            const TSharedPtr<FJsonObject>* NodeObj = nullptr;
            if (NodeValue.IsValid() && NodeValue->TryGetObject(NodeObj) && NodeObj && (*NodeObj).IsValid())
            {
                FString Id;
                if (!(*NodeObj)->TryGetStringField(TEXT("id"), Id))
                {
                    (*NodeObj)->TryGetStringField(TEXT("guid"), Id);
                }
                if (!Id.IsEmpty())
                {
                    SignatureNodeTokens.Add(Id);
                }
            }
        }

        for (const TSharedPtr<FJsonValue>& EdgeValue : Edges)
        {
            const TSharedPtr<FJsonObject>* EdgeObj = nullptr;
            if (EdgeValue.IsValid() && EdgeValue->TryGetObject(EdgeObj) && EdgeObj && (*EdgeObj).IsValid())
            {
                FString FromNodeId;
                FString FromPin;
                FString ToNodeId;
                FString ToPin;
                (*EdgeObj)->TryGetStringField(TEXT("fromNodeId"), FromNodeId);
                (*EdgeObj)->TryGetStringField(TEXT("fromPin"), FromPin);
                (*EdgeObj)->TryGetStringField(TEXT("toNodeId"), ToNodeId);
                (*EdgeObj)->TryGetStringField(TEXT("toPin"), ToPin);
                SignatureEdgeTokens.Add(FromNodeId + TEXT("|") + FromPin + TEXT("->") + ToNodeId + TEXT("|") + ToPin);
            }
        }

        Algo::Sort(SignatureNodeTokens);
        Algo::Sort(SignatureEdgeTokens);
        const FString Signature = FString::Join(SignatureNodeTokens, TEXT(";")) + TEXT("#") + FString::Join(SignatureEdgeTokens, TEXT(";"));
        const FString Revision = FString::Printf(TEXT("%s:%08x"), *RevisionPrefix, GetTypeHash(AssetPath + TEXT("|") + GraphName + TEXT("|") + Signature));

        TSharedPtr<FJsonObject> Snapshot = MakeShared<FJsonObject>();
        Snapshot->SetStringField(TEXT("signature"), Signature);
        Snapshot->SetArrayField(TEXT("nodes"), Nodes);
        Snapshot->SetArrayField(TEXT("edges"), Edges);

        TSharedPtr<FJsonObject> Meta = MakeShared<FJsonObject>();
        Meta->SetNumberField(TEXT("totalNodes"), Nodes.Num());
        Meta->SetNumberField(TEXT("returnedNodes"), Nodes.Num());
        Meta->SetNumberField(TEXT("totalEdges"), Edges.Num());
        Meta->SetNumberField(TEXT("returnedEdges"), Edges.Num());
        Meta->SetBoolField(TEXT("truncated"), false);

        Result->SetStringField(TEXT("graphType"), GraphType);
        Result->SetStringField(TEXT("assetPath"), AssetPath);
        Result->SetStringField(TEXT("graphName"), GraphName);
        Result->SetStringField(TEXT("revision"), Revision);
        Result->SetObjectField(TEXT("semanticSnapshot"), Snapshot);
        Result->SetStringField(TEXT("nextCursor"), TEXT(""));
        Result->SetObjectField(TEXT("meta"), Meta);
        Result->SetArrayField(TEXT("diagnostics"), Diagnostics);
    };

    if (GraphType.Equals(TEXT("material")))
    {
        UMaterial* Material = LoadMaterialByAssetPath(AssetPath);
        if (Material == nullptr)
        {
            Result->SetBoolField(TEXT("isError"), true);
            Result->SetStringField(TEXT("code"), TEXT("ASSET_NOT_FOUND"));
            Result->SetStringField(TEXT("message"), TEXT("Material asset not found."));
            return Result;
        }

        TArray<TSharedPtr<FJsonValue>> Nodes;
        TArray<TSharedPtr<FJsonValue>> Diagnostics;
        TArray<TSharedPtr<FJsonValue>> Edges;
        for (UMaterialExpression* Expression : Material->GetExpressions())
        {
            if (Expression == nullptr)
            {
                continue;
            }

            TSharedPtr<FJsonObject> Node = MakeShared<FJsonObject>();
            const FString NodeId = MaterialExpressionId(Expression);
            Node->SetStringField(TEXT("id"), NodeId);
            Node->SetStringField(TEXT("guid"), NodeId);
            Node->SetStringField(TEXT("nodeClassPath"), Expression->GetClass() ? Expression->GetClass()->GetPathName() : TEXT(""));
            Node->SetStringField(TEXT("title"), Expression->GetName());
            Node->SetStringField(TEXT("graphName"), GraphName);
            TSharedPtr<FJsonObject> Position = MakeShared<FJsonObject>();
            Position->SetNumberField(TEXT("x"), Expression->MaterialExpressionEditorX);
            Position->SetNumberField(TEXT("y"), Expression->MaterialExpressionEditorY);
            Node->SetObjectField(TEXT("position"), Position);
            Node->SetBoolField(TEXT("enabled"), true);

            TArray<TSharedPtr<FJsonValue>> Pins;
            const int32 MaxInputs = 128;
            for (int32 Index = 0; Index < MaxInputs; ++Index)
            {
                FExpressionInput* Input = Expression->GetInput(Index);
                if (Input == nullptr)
                {
                    break;
                }

                const FString InputName = Expression->GetInputName(Index).ToString();
                TSharedPtr<FJsonObject> PinObj = MakeShared<FJsonObject>();
                PinObj->SetStringField(TEXT("name"), InputName);
                PinObj->SetStringField(TEXT("direction"), TEXT("input"));
                PinObj->SetStringField(TEXT("category"), TEXT("material"));
                PinObj->SetStringField(TEXT("subCategory"), TEXT(""));
                PinObj->SetStringField(TEXT("subCategoryObject"), TEXT(""));
                PinObj->SetBoolField(TEXT("isReference"), false);
                PinObj->SetBoolField(TEXT("isConst"), false);
                PinObj->SetBoolField(TEXT("isArray"), false);
                PinObj->SetStringField(TEXT("defaultValue"), TEXT(""));
                PinObj->SetStringField(TEXT("defaultObject"), TEXT(""));
                PinObj->SetStringField(TEXT("defaultText"), TEXT(""));
                TSharedPtr<FJsonObject> PinTypeObject = MakeShared<FJsonObject>();
                PinTypeObject->SetStringField(TEXT("category"), TEXT("material"));
                PinTypeObject->SetStringField(TEXT("subCategory"), TEXT(""));
                PinTypeObject->SetStringField(TEXT("object"), TEXT(""));
                PinTypeObject->SetStringField(TEXT("container"), TEXT("none"));
                PinObj->SetObjectField(TEXT("type"), PinTypeObject);
                TSharedPtr<FJsonObject> PinDefaultObject = MakeShared<FJsonObject>();
                PinDefaultObject->SetStringField(TEXT("value"), TEXT(""));
                PinDefaultObject->SetStringField(TEXT("object"), TEXT(""));
                PinDefaultObject->SetStringField(TEXT("text"), TEXT(""));
                PinObj->SetObjectField(TEXT("default"), PinDefaultObject);

                TArray<TSharedPtr<FJsonValue>> Links;
                if (Input->Expression != nullptr)
                {
                    const FString FromNodeId = MaterialExpressionId(Input->Expression);
                    const FString FromPinName = Input->InputName.ToString();

                    TSharedPtr<FJsonObject> LinkObj = MakeShared<FJsonObject>();
                    LinkObj->SetStringField(TEXT("toNodeId"), FromNodeId);
                    LinkObj->SetStringField(TEXT("toPin"), FromPinName);
                    LinkObj->SetStringField(TEXT("nodeName"), Input->Expression->GetName());
                    LinkObj->SetStringField(TEXT("nodeGuid"), FromNodeId);
                    LinkObj->SetStringField(TEXT("direction"), TEXT("input"));
                    Links.Add(MakeShared<FJsonValueObject>(LinkObj));

                    TSharedPtr<FJsonObject> EdgeObj = MakeShared<FJsonObject>();
                    EdgeObj->SetStringField(TEXT("fromNodeId"), FromNodeId);
                    EdgeObj->SetStringField(TEXT("fromPin"), FromPinName);
                    EdgeObj->SetStringField(TEXT("toNodeId"), NodeId);
                    EdgeObj->SetStringField(TEXT("toPin"), InputName);
                    Edges.Add(MakeShared<FJsonValueObject>(EdgeObj));
                }

                PinObj->SetArrayField(TEXT("links"), Links);
                PinObj->SetArrayField(TEXT("linkedTo"), Links);
                Pins.Add(MakeShared<FJsonValueObject>(PinObj));
            }

            Node->SetArrayField(TEXT("pins"), Pins);
            Nodes.Add(MakeShared<FJsonValueObject>(Node));
        }

        if (Nodes.Num() == 0)
        {
            TSharedPtr<FJsonObject> Diagnostic = MakeShared<FJsonObject>();
            Diagnostic->SetStringField(TEXT("code"), TEXT("QUERY_DEGRADED"));
            Diagnostic->SetStringField(TEXT("message"), TEXT("Material graph has no expressions."));
            Diagnostics.Add(MakeShared<FJsonValueObject>(Diagnostic));
        }

        BuildMinimalSnapshotResult(TEXT("mat"), Nodes, Edges, Diagnostics);
        return Result;
    }

    if (GraphType.Equals(TEXT("pcg")))
    {
        UPCGGraph* PcgGraph = LoadPcgGraphByAssetPath(AssetPath);
        if (PcgGraph == nullptr)
        {
            Result->SetBoolField(TEXT("isError"), true);
            Result->SetStringField(TEXT("code"), TEXT("ASSET_NOT_FOUND"));
            Result->SetStringField(TEXT("message"), TEXT("PCG asset not found."));
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

        int32 Limit = 200;
        double LimitNumber = 0.0;
        if (Arguments->TryGetNumberField(TEXT("limit"), LimitNumber))
        {
            Limit = FMath::Clamp(static_cast<int32>(LimitNumber), 1, 1000);
        }

        TArray<TSharedPtr<FJsonValue>> Nodes;
        TArray<TSharedPtr<FJsonValue>> Edges;
        TSet<FString> EmittedEdgeKeys;
        int32 AddedCount = 0;
        for (UPCGNode* NodeObj : PcgGraph->GetNodes())
        {
            if (NodeObj == nullptr)
            {
                continue;
            }

            const FString NodeClassPath = (NodeObj->GetSettings() && NodeObj->GetSettings()->GetClass())
                ? NodeObj->GetSettings()->GetClass()->GetPathName()
                : (NodeObj->GetClass() ? NodeObj->GetClass()->GetPathName() : TEXT(""));

            if (FilterClasses.Num() > 0)
            {
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

            int32 NodePosX = 0;
            int32 NodePosY = 0;
            NodeObj->GetNodePosition(NodePosX, NodePosY);

            const FString NodeId = NodeObj->GetPathName();
            TSharedPtr<FJsonObject> Node = MakeShared<FJsonObject>();
            Node->SetStringField(TEXT("id"), NodeId);
            Node->SetStringField(TEXT("guid"), NodeId);
            Node->SetStringField(TEXT("nodeClassPath"), NodeClassPath);
            Node->SetStringField(TEXT("title"), NodeObj->NodeTitle.IsNone() ? NodeObj->GetName() : NodeObj->NodeTitle.ToString());
            Node->SetStringField(TEXT("graphName"), GraphName);
            TSharedPtr<FJsonObject> Position = MakeShared<FJsonObject>();
            Position->SetNumberField(TEXT("x"), NodePosX);
            Position->SetNumberField(TEXT("y"), NodePosY);
            Node->SetObjectField(TEXT("position"), Position);
            Node->SetBoolField(TEXT("enabled"), true);

            TArray<TSharedPtr<FJsonValue>> Pins;
            auto SerializePcgPin = [&](UPCGPin* Pin, const FString& Direction)
            {
                if (Pin == nullptr)
                {
                    return;
                }

                TSharedPtr<FJsonObject> PinObj = MakeShared<FJsonObject>();
                const FString PinLabel = Pin->Properties.Label.ToString();
                PinObj->SetStringField(TEXT("name"), PinLabel);
                PinObj->SetStringField(TEXT("direction"), Direction);
                PinObj->SetStringField(TEXT("category"), TEXT("pcg"));
                PinObj->SetStringField(TEXT("subCategory"), TEXT(""));
                PinObj->SetStringField(TEXT("subCategoryObject"), TEXT(""));
                PinObj->SetBoolField(TEXT("isReference"), false);
                PinObj->SetBoolField(TEXT("isConst"), false);
                PinObj->SetBoolField(TEXT("isArray"), Pin->Properties.bAllowMultipleData);
                PinObj->SetStringField(TEXT("defaultValue"), TEXT(""));
                PinObj->SetStringField(TEXT("defaultObject"), TEXT(""));
                PinObj->SetStringField(TEXT("defaultText"), TEXT(""));

                TSharedPtr<FJsonObject> PinTypeObject = MakeShared<FJsonObject>();
                PinTypeObject->SetStringField(TEXT("category"), TEXT("pcg"));
                PinTypeObject->SetStringField(TEXT("subCategory"), TEXT(""));
                PinTypeObject->SetStringField(TEXT("object"), TEXT(""));
                PinTypeObject->SetStringField(TEXT("container"), Pin->Properties.bAllowMultipleData ? TEXT("array") : TEXT("none"));
                PinObj->SetObjectField(TEXT("type"), PinTypeObject);

                TSharedPtr<FJsonObject> PinDefaultObject = MakeShared<FJsonObject>();
                PinDefaultObject->SetStringField(TEXT("value"), TEXT(""));
                PinDefaultObject->SetStringField(TEXT("object"), TEXT(""));
                PinDefaultObject->SetStringField(TEXT("text"), TEXT(""));
                PinObj->SetObjectField(TEXT("default"), PinDefaultObject);

                TArray<TSharedPtr<FJsonValue>> Links;
                for (UPCGEdge* Edge : Pin->Edges)
                {
                    if (Edge == nullptr)
                    {
                        continue;
                    }

                    const UPCGPin* OtherPin = Edge->GetOtherPin(Pin);
                    if (OtherPin == nullptr || OtherPin->Node == nullptr)
                    {
                        continue;
                    }

                    const FString OtherNodeId = OtherPin->Node->GetPathName();
                    const FString OtherPinName = OtherPin->Properties.Label.ToString();

                    TSharedPtr<FJsonObject> LinkObj = MakeShared<FJsonObject>();
                    LinkObj->SetStringField(TEXT("toNodeId"), OtherNodeId);
                    LinkObj->SetStringField(TEXT("toPin"), OtherPinName);
                    LinkObj->SetStringField(TEXT("nodeName"), OtherPin->Node->GetName());
                    LinkObj->SetStringField(TEXT("nodeGuid"), OtherNodeId);
                    LinkObj->SetStringField(TEXT("direction"), Direction);
                    Links.Add(MakeShared<FJsonValueObject>(LinkObj));

                    if (Direction.Equals(TEXT("output")))
                    {
                        const FString EdgeKey = NodeId + TEXT("|") + PinLabel + TEXT("->") + OtherNodeId + TEXT("|") + OtherPinName;
                        if (!EmittedEdgeKeys.Contains(EdgeKey))
                        {
                            EmittedEdgeKeys.Add(EdgeKey);
                            TSharedPtr<FJsonObject> EdgeObj = MakeShared<FJsonObject>();
                            EdgeObj->SetStringField(TEXT("fromNodeId"), NodeId);
                            EdgeObj->SetStringField(TEXT("fromPin"), PinLabel);
                            EdgeObj->SetStringField(TEXT("toNodeId"), OtherNodeId);
                            EdgeObj->SetStringField(TEXT("toPin"), OtherPinName);
                            Edges.Add(MakeShared<FJsonValueObject>(EdgeObj));
                        }
                    }
                }

                PinObj->SetArrayField(TEXT("links"), Links);
                PinObj->SetArrayField(TEXT("linkedTo"), Links);
                Pins.Add(MakeShared<FJsonValueObject>(PinObj));
            };

            for (UPCGPin* InputPin : NodeObj->GetInputPins())
            {
                SerializePcgPin(InputPin, TEXT("input"));
            }
            for (UPCGPin* OutputPin : NodeObj->GetOutputPins())
            {
                SerializePcgPin(OutputPin, TEXT("output"));
            }

            Node->SetArrayField(TEXT("pins"), Pins);
            Nodes.Add(MakeShared<FJsonValueObject>(Node));
        }

        TArray<TSharedPtr<FJsonValue>> Diagnostics;
        if (Nodes.Num() == 0)
        {
            TSharedPtr<FJsonObject> Diagnostic = MakeShared<FJsonObject>();
            Diagnostic->SetStringField(TEXT("code"), TEXT("QUERY_EMPTY"));
            Diagnostic->SetStringField(TEXT("message"), TEXT("PCG graph has no nodes."));
            Diagnostics.Add(MakeShared<FJsonValueObject>(Diagnostic));
        }

        BuildMinimalSnapshotResult(TEXT("pcg"), Nodes, Edges, Diagnostics);
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

void FLoomleBridgeModule::PruneGraphActionTokenRegistry()
{
    const double NowSeconds = FPlatformTime::Seconds();
    TArray<FString> KeysToRemove;
    KeysToRemove.Reserve(GraphActionTokenRegistry.Num());
    for (const TPair<FString, FGraphActionTokenEntry>& Pair : GraphActionTokenRegistry)
    {
        const bool bHasExecutablePayload = Pair.Value.Action.IsValid() || !Pair.Value.LegacyActionId.IsEmpty();
        if (!bHasExecutablePayload || (NowSeconds - Pair.Value.CreatedAtSeconds) > LoomleBridgeConstants::GraphActionTokenTtlSeconds)
        {
            KeysToRemove.Add(Pair.Key);
        }
    }
    for (const FString& Key : KeysToRemove)
    {
        GraphActionTokenRegistry.Remove(Key);
    }

    if (GraphActionTokenRegistry.Num() <= LoomleBridgeConstants::MaxGraphActionTokenRegistryEntries)
    {
        return;
    }

    struct FTokenAgeEntry
    {
        FString Token;
        double CreatedAtSeconds = 0.0;
    };
    TArray<FTokenAgeEntry> AgeEntries;
    AgeEntries.Reserve(GraphActionTokenRegistry.Num());
    for (const TPair<FString, FGraphActionTokenEntry>& Pair : GraphActionTokenRegistry)
    {
        FTokenAgeEntry AgeEntry;
        AgeEntry.Token = Pair.Key;
        AgeEntry.CreatedAtSeconds = Pair.Value.CreatedAtSeconds;
        AgeEntries.Add(AgeEntry);
    }
    Algo::Sort(AgeEntries, [](const FTokenAgeEntry& A, const FTokenAgeEntry& B)
    {
        return A.CreatedAtSeconds < B.CreatedAtSeconds;
    });

    const int32 Overflow = GraphActionTokenRegistry.Num() - LoomleBridgeConstants::MaxGraphActionTokenRegistryEntries;
    for (int32 Index = 0; Index < Overflow; ++Index)
    {
        GraphActionTokenRegistry.Remove(AgeEntries[Index].Token);
    }
}

bool FLoomleBridgeModule::ResolveGraphActionToken(const FString& ActionToken, const FString& GraphType, const FString& AssetPath, const FString& GraphName, FGraphActionTokenEntry& OutEntry, FString& OutErrorCode, FString& OutErrorMessage)
{
    OutErrorCode.Empty();
    OutErrorMessage.Empty();
    OutEntry = FGraphActionTokenEntry();
    PruneGraphActionTokenRegistry();

    const FGraphActionTokenEntry* Found = GraphActionTokenRegistry.Find(ActionToken);
    if (Found == nullptr || (!Found->Action.IsValid() && Found->LegacyActionId.IsEmpty()))
    {
        OutErrorCode = TEXT("ACTION_TOKEN_INVALID");
        OutErrorMessage = TEXT("Unknown actionToken. Refresh with graph.addable and retry.");
        return false;
    }

    const double NowSeconds = FPlatformTime::Seconds();
    if ((NowSeconds - Found->CreatedAtSeconds) > LoomleBridgeConstants::GraphActionTokenTtlSeconds)
    {
        GraphActionTokenRegistry.Remove(ActionToken);
        OutErrorCode = TEXT("ACTION_TOKEN_EXPIRED");
        OutErrorMessage = TEXT("actionToken expired. Refresh with graph.addable and retry.");
        return false;
    }

    if (!Found->GraphType.Equals(GraphType) || !Found->AssetPath.Equals(AssetPath) || !Found->GraphName.Equals(GraphName))
    {
        OutErrorCode = TEXT("ACTION_TOKEN_CONTEXT_MISMATCH");
        OutErrorMessage = TEXT("actionToken does not match requested graph context.");
        return false;
    }

    OutEntry = *Found;
    return true;
}

TSharedPtr<FJsonObject> FLoomleBridgeModule::BuildGraphAddableToolResult(const TSharedPtr<FJsonObject>& Arguments)
{
    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetBoolField(TEXT("isError"), false);

    const FString GraphType = GetGraphTypeFromArgs(Arguments);
    if (!IsSupportedGraphType(GraphType))
    {
        Result->SetBoolField(TEXT("isError"), true);
        Result->SetStringField(TEXT("code"), TEXT("UNSUPPORTED_GRAPH_TYPE"));
        Result->SetStringField(TEXT("message"), TEXT("Supported graphType values: blueprint, material(shader), pcg."));
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
    AssetPath = NormalizeAssetPath(AssetPath);

    FString GraphName;
    if (!Arguments->TryGetStringField(TEXT("graphName"), GraphName) || GraphName.IsEmpty())
    {
        Result->SetBoolField(TEXT("isError"), true);
        Result->SetStringField(TEXT("code"), TEXT("INVALID_ARGUMENT"));
        Result->SetStringField(TEXT("message"), TEXT("arguments.graphName is required."));
        return Result;
    }

    if (!GraphType.Equals(TEXT("blueprint")))
    {
        PruneGraphActionTokenRegistry();

        int32 Limit = 100;
        double LimitNumber = 0.0;
        if (Arguments->TryGetNumberField(TEXT("limit"), LimitNumber))
        {
            Limit = FMath::Clamp(static_cast<int32>(LimitNumber), 1, 1000);
        }

        struct FSimpleActionSpec
        {
            const TCHAR* ActionId;
            const TCHAR* Title;
            const TCHAR* Category;
            const TCHAR* NodeClassPath;
        };

        TArray<FSimpleActionSpec> ActionSpecs;
        if (GraphType.Equals(TEXT("material")))
        {
            ActionSpecs.Add({ TEXT("mat.constant"), TEXT("Constant"), TEXT("Material|Constants"), TEXT("/Script/Engine.MaterialExpressionConstant") });
            ActionSpecs.Add({ TEXT("mat.constant3"), TEXT("Constant3Vector"), TEXT("Material|Constants"), TEXT("/Script/Engine.MaterialExpressionConstant3Vector") });
            ActionSpecs.Add({ TEXT("mat.multiply"), TEXT("Multiply"), TEXT("Material|Math"), TEXT("/Script/Engine.MaterialExpressionMultiply") });
            ActionSpecs.Add({ TEXT("mat.textureSample"), TEXT("Texture Sample"), TEXT("Material|Texture"), TEXT("/Script/Engine.MaterialExpressionTextureSample") });
            ActionSpecs.Add({ TEXT("mat.scalarParameter"), TEXT("Scalar Parameter"), TEXT("Material|Parameters"), TEXT("/Script/Engine.MaterialExpressionScalarParameter") });
            ActionSpecs.Add({ TEXT("mat.vectorParameter"), TEXT("Vector Parameter"), TEXT("Material|Parameters"), TEXT("/Script/Engine.MaterialExpressionVectorParameter") });
        }
        else if (GraphType.Equals(TEXT("pcg")))
        {
            ActionSpecs.Add({ TEXT("pcg.addTag"), TEXT("Add Tag"), TEXT("PCG|Metadata"), TEXT("/Script/PCG.PCGAddTagSettings") });
            ActionSpecs.Add({ TEXT("pcg.filterByTag"), TEXT("Filter By Tag"), TEXT("PCG|Filter"), TEXT("/Script/PCG.PCGFilterByTagSettings") });
            ActionSpecs.Add({ TEXT("pcg.createPoints"), TEXT("Create Points"), TEXT("PCG|Create"), TEXT("/Script/PCG.PCGCreatePointsSettings") });
            ActionSpecs.Add({ TEXT("pcg.surfaceSampler"), TEXT("Surface Sampler"), TEXT("PCG|Sampling"), TEXT("/Script/PCG.PCGSurfaceSamplerSettings") });
        }

        TSharedPtr<FJsonObject> ContextEcho = MakeShared<FJsonObject>();
        ContextEcho->SetStringField(TEXT("mode"), TEXT("graph"));

        TArray<TSharedPtr<FJsonValue>> Actions;
        int32 Total = 0;
        for (const FSimpleActionSpec& Spec : ActionSpecs)
        {
            if (Actions.Num() >= Limit)
            {
                break;
            }

            UClass* NodeClass = LoadObject<UClass>(nullptr, Spec.NodeClassPath);
            if (NodeClass == nullptr)
            {
                continue;
            }
            ++Total;

            const FString ActionToken = FString::Printf(TEXT("act:%s:%s"), *GraphType, *FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphensLower));
            FGraphActionTokenEntry TokenEntry;
            TokenEntry.GraphType = GraphType;
            TokenEntry.AssetPath = AssetPath;
            TokenEntry.GraphName = GraphName;
            TokenEntry.LegacyActionId = Spec.NodeClassPath;
            TokenEntry.CreatedAtSeconds = FPlatformTime::Seconds();
            GraphActionTokenRegistry.Add(ActionToken, TokenEntry);

            TSharedPtr<FJsonObject> ActionObject = MakeShared<FJsonObject>();
            ActionObject->SetStringField(TEXT("actionToken"), ActionToken);
            ActionObject->SetStringField(TEXT("title"), Spec.Title);
            ActionObject->SetStringField(TEXT("categoryPath"), Spec.Category);
            ActionObject->SetStringField(TEXT("tooltip"), TEXT(""));
            ActionObject->SetStringField(TEXT("keywords"), TEXT(""));
            TSharedPtr<FJsonObject> Compatibility = MakeShared<FJsonObject>();
            Compatibility->SetBoolField(TEXT("isCompatible"), true);
            Compatibility->SetArrayField(TEXT("reasons"), TArray<TSharedPtr<FJsonValue>>{});
            ActionObject->SetObjectField(TEXT("compatibility"), Compatibility);
            TSharedPtr<FJsonObject> SpawnObj = MakeShared<FJsonObject>();
            SpawnObj->SetStringField(TEXT("nodeClassPath"), Spec.NodeClassPath);
            ActionObject->SetObjectField(TEXT("spawn"), SpawnObj);
            Actions.Add(MakeShared<FJsonValueObject>(ActionObject));
        }

        TSharedPtr<FJsonObject> Meta = MakeShared<FJsonObject>();
        Meta->SetNumberField(TEXT("total"), Total);
        Meta->SetNumberField(TEXT("returned"), Actions.Num());
        Meta->SetBoolField(TEXT("truncated"), false);

        TArray<TSharedPtr<FJsonValue>> Diagnostics;
        if (Actions.Num() == 0)
        {
            TSharedPtr<FJsonObject> Diagnostic = MakeShared<FJsonObject>();
            Diagnostic->SetStringField(TEXT("code"), TEXT("ADDABLE_EMPTY"));
            Diagnostic->SetStringField(TEXT("message"), TEXT("No addable actions are available for current graph type in this editor build."));
            Diagnostics.Add(MakeShared<FJsonValueObject>(Diagnostic));
        }

        Result->SetStringField(TEXT("graphType"), GraphType);
        Result->SetStringField(TEXT("assetPath"), AssetPath);
        Result->SetStringField(TEXT("graphName"), GraphName);
        Result->SetObjectField(TEXT("contextEcho"), ContextEcho);
        Result->SetArrayField(TEXT("actions"), Actions);
        Result->SetStringField(TEXT("nextCursor"), TEXT(""));
        Result->SetObjectField(TEXT("meta"), Meta);
        Result->SetArrayField(TEXT("diagnostics"), Diagnostics);
        return Result;
    }

    UBlueprint* Blueprint = LoadBlueprintByAssetPath(AssetPath);
    UEdGraph* TargetGraph = ResolveBlueprintGraph(Blueprint, GraphName);
    if (Blueprint == nullptr || TargetGraph == nullptr)
    {
        Result->SetBoolField(TEXT("isError"), true);
        Result->SetStringField(TEXT("code"), TEXT("GRAPH_NOT_FOUND"));
        Result->SetStringField(TEXT("message"), TEXT("Failed to resolve blueprint/target graph."));
        return Result;
    }

    UEdGraphPin* FromPin = nullptr;
    FString FromNodeId;
    FString FromPinName;
    const TSharedPtr<FJsonObject>* ContextObj = nullptr;
    if (Arguments->TryGetObjectField(TEXT("context"), ContextObj) && ContextObj != nullptr && (*ContextObj).IsValid())
    {
        const TSharedPtr<FJsonObject>* FromPinObj = nullptr;
        if ((*ContextObj)->TryGetObjectField(TEXT("fromPin"), FromPinObj) && FromPinObj != nullptr && (*FromPinObj).IsValid())
        {
            (*FromPinObj)->TryGetStringField(TEXT("nodeId"), FromNodeId);
            (*FromPinObj)->TryGetStringField(TEXT("pinName"), FromPinName);
            if (!FromNodeId.IsEmpty() && !FromPinName.IsEmpty())
            {
                if (UEdGraphNode* Node = FindNodeByGuid(TargetGraph, FromNodeId))
                {
                    FromPin = FindPinByName(Node, FromPinName);
                }
            }
        }
    }

    FBlueprintActionContext ActionContext;
    ActionContext.Blueprints.Add(Blueprint);
    ActionContext.Graphs.Add(TargetGraph);
    if (FromPin != nullptr)
    {
        ActionContext.Pins.Add(FromPin);
    }

    FBlueprintActionMenuBuilder MenuBuilder;
    UEdGraph* TempOwnerGraph = NewObject<UEdGraph>((UObject*)Blueprint);
    if (TempOwnerGraph != nullptr)
    {
        TempOwnerGraph->Schema = UEdGraphSchema_K2::StaticClass();
        TempOwnerGraph->SetFlags(RF_Transient);
        MenuBuilder.OwnerOfTemporaries = TempOwnerGraph;
    }

    const uint32 ClassTargetMask =
        EContextTargetFlags::TARGET_Blueprint |
        EContextTargetFlags::TARGET_SubComponents |
        EContextTargetFlags::TARGET_NodeTarget |
        EContextTargetFlags::TARGET_PinObject |
        EContextTargetFlags::TARGET_SiblingPinObjects |
        EContextTargetFlags::TARGET_BlueprintLibraries |
        EContextTargetFlags::TARGET_NonImportedTypes;
    FBlueprintActionMenuUtils::MakeContextMenu(ActionContext, true, ClassTargetMask, MenuBuilder);

    int32 Limit = 100;
    double LimitNumber = 0.0;
    if (Arguments->TryGetNumberField(TEXT("limit"), LimitNumber))
    {
        Limit = FMath::Clamp(static_cast<int32>(LimitNumber), 1, 1000);
    }

    FString Query;
    Arguments->TryGetStringField(TEXT("query"), Query);
    const FString QueryLower = Query.TrimStartAndEnd().ToLower();
    const bool bHasPinContext = (FromPin != nullptr);

    PruneGraphActionTokenRegistry();
    TArray<TSharedPtr<FJsonValue>> Actions;
    TArray<TSharedPtr<FJsonValue>> Diagnostics;
    int32 TotalActions = 0;

    struct FActionCandidate
    {
        TSharedPtr<FEdGraphSchemaAction> Action;
        FString Title;
        FString Category;
        FString Tooltip;
        FString Keywords;
        FString FullSearchText;
        int32 Score = 0;
        int32 Grouping = 0;
    };
    TArray<FActionCandidate> Candidates;
    Candidates.Reserve(MenuBuilder.GetNumActions());

    auto ComputeActionScore = [&QueryLower, bHasPinContext](const FActionCandidate& Candidate) -> int32
    {
        const FString TitleLower = Candidate.Title.ToLower();
        const FString CategoryLower = Candidate.Category.ToLower();
        const FString KeywordsLower = Candidate.Keywords.ToLower();
        const FString FullSearchLower = Candidate.FullSearchText.ToLower();
        int32 Score = Candidate.Grouping * 10;

        if (!QueryLower.IsEmpty())
        {
            if (TitleLower.Equals(QueryLower))
            {
                Score += 2400;
            }
            else if (TitleLower.StartsWith(QueryLower))
            {
                Score += 1300;
            }
            else if (TitleLower.Contains(QueryLower))
            {
                Score += 700;
            }

            if (CategoryLower.Contains(QueryLower))
            {
                Score += 260;
            }
            if (KeywordsLower.Contains(QueryLower))
            {
                Score += 300;
            }
            if (FullSearchLower.Contains(QueryLower))
            {
                Score += 120;
            }
        }
        else
        {
            const bool bIsCastAction = TitleLower.StartsWith(TEXT("cast to "));
            const bool bIsClassVariant = TitleLower.EndsWith(TEXT(" class")) || TitleLower.Contains(TEXT(" class"));
            if (bIsCastAction && !bHasPinContext)
            {
                Score -= 550;
            }
            if (bIsClassVariant && !bHasPinContext)
            {
                Score -= 260;
            }
            if (CategoryLower.Contains(TEXT("casting")) && !bHasPinContext)
            {
                Score -= 140;
            }

            const bool bIsHighPriorityNode =
                TitleLower.Equals(TEXT("branch"))
                || TitleLower.Equals(TEXT("sequence"))
                || TitleLower.Equals(TEXT("comment"))
                || TitleLower.Equals(TEXT("reroute"))
                || TitleLower.Equals(TEXT("for loop"))
                || TitleLower.Equals(TEXT("for each loop"))
                || TitleLower.Equals(TEXT("do once"))
                || TitleLower.Equals(TEXT("gate"))
                || TitleLower.Equals(TEXT("delay"))
                || TitleLower.Equals(TEXT("print string"));
            if (bIsHighPriorityNode)
            {
                Score += 900;
            }
            if (CategoryLower.Contains(TEXT("flow control")))
            {
                Score += 260;
            }
            if (CategoryLower.Contains(TEXT("utilities")))
            {
                Score += 80;
            }
        }

        if (!TitleLower.StartsWith(TEXT("cast to ")))
        {
            Score += 40;
        }
        return Score;
    };

    const int32 NumActions = MenuBuilder.GetNumActions();
    for (int32 Index = 0; Index < NumActions; ++Index)
    {
        TSharedPtr<FEdGraphSchemaAction>& ActionRef = MenuBuilder.GetSchemaAction(Index);
        if (!ActionRef.IsValid())
        {
            continue;
        }

        const FString Title = ActionRef->GetMenuDescription().ToString();
        const FString Category = ActionRef->GetCategory().ToString();
        const FString Tooltip = ActionRef->GetTooltipDescription().ToString();
        const FString Keywords = ActionRef->GetKeywords().ToString();
        const FString FullSearchText = ActionRef->GetFullSearchText();
        const FName ActionTypeId = ActionRef->GetTypeId();
        if (ActionTypeId == FEdGraphSchemaAction_Dummy::StaticGetTypeId() && QueryLower.IsEmpty())
        {
            continue;
        }

        if (!QueryLower.IsEmpty())
        {
            const FString SearchBlob = (Title + TEXT("|") + Category + TEXT("|") + Tooltip + TEXT("|") + Keywords + TEXT("|") + FullSearchText).ToLower();
            if (!SearchBlob.Contains(QueryLower))
            {
                continue;
            }
        }

        FActionCandidate Candidate;
        Candidate.Action = ActionRef;
        Candidate.Title = Title;
        Candidate.Category = Category;
        Candidate.Tooltip = Tooltip;
        Candidate.Keywords = Keywords;
        Candidate.FullSearchText = FullSearchText;
        Candidate.Grouping = ActionRef->GetGrouping();
        Candidate.Score = ComputeActionScore(Candidate);
        Candidates.Add(Candidate);
    }

    Algo::Sort(Candidates, [](const FActionCandidate& A, const FActionCandidate& B)
    {
        if (A.Score != B.Score)
        {
            return A.Score > B.Score;
        }
        if (A.Grouping != B.Grouping)
        {
            return A.Grouping > B.Grouping;
        }
        return A.Title < B.Title;
    });

    TotalActions = Candidates.Num();
    for (const FActionCandidate& Candidate : Candidates)
    {
        if (Actions.Num() >= Limit)
        {
            break;
        }

        const FString ActionToken = FString::Printf(TEXT("act:bp:%s"), *FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphensLower));
        FGraphActionTokenEntry TokenEntry;
        TokenEntry.GraphType = GraphType;
        TokenEntry.AssetPath = AssetPath;
        TokenEntry.GraphName = GraphName;
        TokenEntry.FromNodeId = FromNodeId;
        TokenEntry.FromPinName = FromPinName;
        TokenEntry.Action = Candidate.Action;
        TokenEntry.CreatedAtSeconds = FPlatformTime::Seconds();
        GraphActionTokenRegistry.Add(ActionToken, TokenEntry);

        TSharedPtr<FJsonObject> ActionObject = MakeShared<FJsonObject>();
        ActionObject->SetStringField(TEXT("actionToken"), ActionToken);
        ActionObject->SetStringField(TEXT("title"), Candidate.Title);
        ActionObject->SetStringField(TEXT("categoryPath"), Candidate.Category);
        ActionObject->SetStringField(TEXT("tooltip"), Candidate.Tooltip);
        ActionObject->SetStringField(TEXT("keywords"), Candidate.Keywords);

        TSharedPtr<FJsonObject> Compatibility = MakeShared<FJsonObject>();
        Compatibility->SetBoolField(TEXT("isCompatible"), true);
        Compatibility->SetArrayField(TEXT("reasons"), TArray<TSharedPtr<FJsonValue>>{});
        ActionObject->SetObjectField(TEXT("compatibility"), Compatibility);

        TSharedPtr<FJsonObject> SpawnObj = MakeShared<FJsonObject>();
        if (Candidate.Action->GetTypeId() == FEdGraphSchemaAction_NewNode::StaticGetTypeId())
        {
            const FEdGraphSchemaAction_NewNode* NewNodeAction = static_cast<const FEdGraphSchemaAction_NewNode*>(Candidate.Action.Get());
            if (NewNodeAction != nullptr && NewNodeAction->NodeTemplate != nullptr && NewNodeAction->NodeTemplate->GetClass() != nullptr)
            {
                SpawnObj->SetStringField(TEXT("nodeClassPath"), NewNodeAction->NodeTemplate->GetClass()->GetPathName());
            }
        }
        ActionObject->SetObjectField(TEXT("spawn"), SpawnObj);
        Actions.Add(MakeShared<FJsonValueObject>(ActionObject));
    }

    if (TotalActions == 0)
    {
        struct FFallbackActionSpec
        {
            const TCHAR* ActionId;
            const TCHAR* Title;
            const TCHAR* Category;
        };
        const FFallbackActionSpec FallbackActions[] = {
            {TEXT("event"), TEXT("Event"), TEXT("Events")},
            {TEXT("cast"), TEXT("Cast"), TEXT("Utilities|Casting")},
            {TEXT("callFunction"), TEXT("Call Function"), TEXT("Functions")},
            {TEXT("branch"), TEXT("Branch"), TEXT("Flow Control")},
            {TEXT("variableGet"), TEXT("Get Variable"), TEXT("Variables")},
            {TEXT("variableSet"), TEXT("Set Variable"), TEXT("Variables")},
            {TEXT("comment"), TEXT("Comment"), TEXT("Utilities")},
            {TEXT("knot"), TEXT("Reroute"), TEXT("Utilities")}
        };

        for (const FFallbackActionSpec& Fallback : FallbackActions)
        {
            if (Actions.Num() >= Limit)
            {
                break;
            }

            ++TotalActions;
            const FString ActionToken = FString::Printf(TEXT("act:bp:%s"), *FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphensLower));
            FGraphActionTokenEntry TokenEntry;
            TokenEntry.GraphType = GraphType;
            TokenEntry.AssetPath = AssetPath;
            TokenEntry.GraphName = GraphName;
            TokenEntry.FromNodeId = FromNodeId;
            TokenEntry.FromPinName = FromPinName;
            TokenEntry.LegacyActionId = Fallback.ActionId;
            TokenEntry.CreatedAtSeconds = FPlatformTime::Seconds();
            GraphActionTokenRegistry.Add(ActionToken, TokenEntry);

            TSharedPtr<FJsonObject> ActionObject = MakeShared<FJsonObject>();
            ActionObject->SetStringField(TEXT("actionToken"), ActionToken);
            ActionObject->SetStringField(TEXT("title"), Fallback.Title);
            ActionObject->SetStringField(TEXT("categoryPath"), Fallback.Category);
            ActionObject->SetStringField(TEXT("tooltip"), TEXT(""));
            ActionObject->SetStringField(TEXT("keywords"), TEXT(""));
            TSharedPtr<FJsonObject> Compatibility = MakeShared<FJsonObject>();
            Compatibility->SetBoolField(TEXT("isCompatible"), true);
            Compatibility->SetArrayField(TEXT("reasons"), TArray<TSharedPtr<FJsonValue>>{});
            ActionObject->SetObjectField(TEXT("compatibility"), Compatibility);
            ActionObject->SetObjectField(TEXT("spawn"), MakeShared<FJsonObject>());
            Actions.Add(MakeShared<FJsonValueObject>(ActionObject));
        }

        TSharedPtr<FJsonObject> Diagnostic = MakeShared<FJsonObject>();
        Diagnostic->SetStringField(TEXT("code"), TEXT("ADDABLE_FALLBACK_USED"));
        Diagnostic->SetStringField(TEXT("message"), TEXT("Schema returned no actions, fallback action set was used."));
        Diagnostics.Add(MakeShared<FJsonValueObject>(Diagnostic));
    }
    PruneGraphActionTokenRegistry();

    TSharedPtr<FJsonObject> ContextEcho = MakeShared<FJsonObject>();
    ContextEcho->SetStringField(TEXT("mode"), FromPin != nullptr ? TEXT("pin") : TEXT("graph"));
    if (!FromNodeId.IsEmpty())
    {
        ContextEcho->SetStringField(TEXT("fromNodeId"), FromNodeId);
    }
    if (!FromPinName.IsEmpty())
    {
        ContextEcho->SetStringField(TEXT("fromPinName"), FromPinName);
    }

    TSharedPtr<FJsonObject> Meta = MakeShared<FJsonObject>();
    Meta->SetNumberField(TEXT("total"), TotalActions);
    Meta->SetNumberField(TEXT("returned"), Actions.Num());
    Meta->SetBoolField(TEXT("truncated"), Actions.Num() < TotalActions);

    Result->SetStringField(TEXT("graphType"), GraphType);
    Result->SetStringField(TEXT("assetPath"), AssetPath);
    Result->SetStringField(TEXT("graphName"), GraphName);
    Result->SetObjectField(TEXT("contextEcho"), ContextEcho);
    Result->SetArrayField(TEXT("actions"), Actions);
    Result->SetStringField(TEXT("nextCursor"), TEXT(""));
    Result->SetObjectField(TEXT("meta"), Meta);
    Result->SetArrayField(TEXT("diagnostics"), Diagnostics);
    return Result;
}

TSharedPtr<FJsonObject> FLoomleBridgeModule::BuildGraphMutateToolResult(const TSharedPtr<FJsonObject>& Arguments)
{
    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetBoolField(TEXT("isError"), false);

    const FString GraphType = GetGraphTypeFromArgs(Arguments);
    if (!IsSupportedGraphType(GraphType))
    {
        Result->SetBoolField(TEXT("isError"), true);
        Result->SetStringField(TEXT("code"), TEXT("UNSUPPORTED_GRAPH_TYPE"));
        Result->SetStringField(TEXT("message"), TEXT("Supported graphType values: blueprint, material(shader), pcg."));
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
    AssetPath = NormalizeAssetPath(AssetPath);

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

    if (!GraphType.Equals(TEXT("blueprint")))
    {
        TMap<FString, FString> LocalNodeRefs;
        TArray<TSharedPtr<FJsonValue>> LocalOpResults;
        bool bAnyErrorLocal = false;
        FString FirstErrorLocal;

        auto ResolveNodeTokenLocal = [&LocalNodeRefs](const TSharedPtr<FJsonObject>& Obj, FString& OutNodeId) -> bool
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
            if (Obj->TryGetStringField(TEXT("nodeRef"), NodeRef) && LocalNodeRefs.Contains(NodeRef))
            {
                OutNodeId = LocalNodeRefs[NodeRef];
                return !OutNodeId.IsEmpty();
            }
            return false;
        };

        auto GetPointFromObjectLocal = [](const TSharedPtr<FJsonObject>& Obj, int32& OutX, int32& OutY)
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

        UObject* MutableAsset = nullptr;
        UMaterial* MaterialAsset = nullptr;
        UPCGGraph* PcgGraph = nullptr;
        if (GraphType.Equals(TEXT("material")))
        {
            MaterialAsset = LoadMaterialByAssetPath(AssetPath);
            MutableAsset = MaterialAsset;
            if (MaterialAsset == nullptr)
            {
                Result->SetBoolField(TEXT("isError"), true);
                Result->SetStringField(TEXT("code"), TEXT("ASSET_NOT_FOUND"));
                Result->SetStringField(TEXT("message"), TEXT("Material asset not found."));
                return Result;
            }
        }
        else if (GraphType.Equals(TEXT("pcg")))
        {
            PcgGraph = LoadPcgGraphByAssetPath(AssetPath);
            MutableAsset = PcgGraph;
            if (PcgGraph == nullptr)
            {
                Result->SetBoolField(TEXT("isError"), true);
                Result->SetStringField(TEXT("code"), TEXT("ASSET_NOT_FOUND"));
                Result->SetStringField(TEXT("message"), TEXT("PCG asset not found."));
                return Result;
            }
        }

        if (!bDryRun && MutableAsset != nullptr)
        {
            MutableAsset->Modify();
        }

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

            FString NodeId;
            FString ClientRef;
            FString Error;
            bool bOk = true;
            bool bChanged = false;
            FString GraphEventName;
            TSharedPtr<FJsonObject> GraphEventData = MakeShared<FJsonObject>();
            (*OpObj)->TryGetStringField(TEXT("clientRef"), ClientRef);

            const TSharedPtr<FJsonObject>* ArgsObjPtr = nullptr;
            const TSharedPtr<FJsonObject> ArgsObj =
                ((*OpObj)->TryGetObjectField(TEXT("args"), ArgsObjPtr) && ArgsObjPtr && (*ArgsObjPtr).IsValid())
                    ? *ArgsObjPtr
                    : MakeShared<FJsonObject>();

            if (!bDryRun)
            {
                if (GraphType.Equals(TEXT("material")))
                {
                    if (Op.Equals(TEXT("addnode.byclass")) || Op.Equals(TEXT("addnode.byaction")))
                    {
                        FString NodeClassPath;
                        if (Op.Equals(TEXT("addnode.byclass")))
                        {
                            ArgsObj->TryGetStringField(TEXT("nodeClassPath"), NodeClassPath);
                        }
                        else
                        {
                            FString ActionToken;
                            ArgsObj->TryGetStringField(TEXT("actionToken"), ActionToken);
                            ArgsObj->TryGetStringField(TEXT("actionId"), NodeClassPath);
                            if (!ActionToken.IsEmpty())
                            {
                                FGraphActionTokenEntry TokenEntry;
                                FString ErrorCode;
                                if (ResolveGraphActionToken(ActionToken, GraphType, AssetPath, GraphName, TokenEntry, ErrorCode, Error))
                                {
                                    if (!TokenEntry.LegacyActionId.IsEmpty())
                                    {
                                        NodeClassPath = TokenEntry.LegacyActionId;
                                    }
                                }
                                else
                                {
                                    bOk = false;
                                }
                            }
                        }

                        UClass* ExpressionClass = LoadObject<UClass>(nullptr, *NodeClassPath);
                        if (bOk && (ExpressionClass == nullptr || !ExpressionClass->IsChildOf(UMaterialExpression::StaticClass())))
                        {
                            bOk = false;
                            Error = TEXT("Invalid material expression class.");
                        }

                        int32 X = 0;
                        int32 Y = 0;
                        GetPointFromObjectLocal(ArgsObj, X, Y);
                        if (bOk)
                        {
                            UMaterialExpression* NewExpression = UMaterialEditingLibrary::CreateMaterialExpression(MaterialAsset, ExpressionClass, X, Y);
                            if (NewExpression == nullptr)
                            {
                                bOk = false;
                                Error = TEXT("Failed to create material expression.");
                            }
                            else
                            {
                                NodeId = MaterialExpressionId(NewExpression);
                                bChanged = true;
                                GraphEventName = TEXT("graph.node_added");
                                GraphEventData->SetStringField(TEXT("nodeId"), NodeId);
                                GraphEventData->SetStringField(TEXT("nodeType"), Op.Equals(TEXT("addnode.byaction")) ? TEXT("by_action") : TEXT("by_class"));
                                GraphEventData->SetStringField(TEXT("nodeClassPath"), NodeClassPath);
                                GraphEventData->SetStringField(TEXT("op"), Op);
                            }
                        }
                    }
                    else if (Op.Equals(TEXT("removenode")))
                    {
                        FString TargetNodeId;
                        bOk = ResolveNodeTokenLocal(ArgsObj, TargetNodeId);
                        if (!bOk)
                        {
                            Error = TEXT("Missing target nodeId.");
                        }
                        else if (UMaterialExpression* Expression = FindMaterialExpressionById(MaterialAsset, TargetNodeId))
                        {
                            UMaterialEditingLibrary::DeleteMaterialExpression(MaterialAsset, Expression);
                            bChanged = true;
                            GraphEventName = TEXT("graph.node_removed");
                            GraphEventData->SetStringField(TEXT("nodeId"), TargetNodeId);
                            GraphEventData->SetStringField(TEXT("op"), Op);
                        }
                        else
                        {
                            bOk = false;
                            Error = TEXT("Material expression not found.");
                        }
                    }
                    else if (Op.Equals(TEXT("movenode")))
                    {
                        FString TargetNodeId;
                        bOk = ResolveNodeTokenLocal(ArgsObj, TargetNodeId);
                        if (!bOk)
                        {
                            Error = TEXT("Missing target nodeId.");
                        }
                        else if (UMaterialExpression* Expression = FindMaterialExpressionById(MaterialAsset, TargetNodeId))
                        {
                            int32 X = 0;
                            int32 Y = 0;
                            GetPointFromObjectLocal(ArgsObj, X, Y);
                            Expression->MaterialExpressionEditorX = X;
                            Expression->MaterialExpressionEditorY = Y;
                            bChanged = true;
                            GraphEventName = TEXT("graph.node_moved");
                            GraphEventData->SetStringField(TEXT("nodeId"), TargetNodeId);
                            GraphEventData->SetNumberField(TEXT("x"), X);
                            GraphEventData->SetNumberField(TEXT("y"), Y);
                            GraphEventData->SetStringField(TEXT("op"), Op);
                        }
                        else
                        {
                            bOk = false;
                            Error = TEXT("Material expression not found.");
                        }
                    }
                    else if (Op.Equals(TEXT("connectpins")))
                    {
                        const TSharedPtr<FJsonObject>* FromObj = nullptr;
                        const TSharedPtr<FJsonObject>* ToObj = nullptr;
                        FString FromNodeId;
                        FString ToNodeId;
                        FString FromPinName;
                        FString ToPinName;
                        if (!ArgsObj->TryGetObjectField(TEXT("from"), FromObj) || !FromObj || !(*FromObj).IsValid()
                            || !ArgsObj->TryGetObjectField(TEXT("to"), ToObj) || !ToObj || !(*ToObj).IsValid()
                            || !ResolveNodeTokenLocal(*FromObj, FromNodeId)
                            || !ResolveNodeTokenLocal(*ToObj, ToNodeId))
                        {
                            bOk = false;
                            Error = TEXT("connectPins requires from/to node references.");
                        }
                        else
                        {
                            (*FromObj)->TryGetStringField(TEXT("pinName"), FromPinName);
                            (*ToObj)->TryGetStringField(TEXT("pinName"), ToPinName);
                            UMaterialExpression* FromExpr = FindMaterialExpressionById(MaterialAsset, FromNodeId);
                            UMaterialExpression* ToExpr = FindMaterialExpressionById(MaterialAsset, ToNodeId);
                            if (FromExpr == nullptr || ToExpr == nullptr)
                            {
                                bOk = false;
                                Error = TEXT("Material expression not found.");
                            }
                            else
                            {
                                bOk = UMaterialEditingLibrary::ConnectMaterialExpressions(FromExpr, FromPinName, ToExpr, ToPinName);
                                if (!bOk)
                                {
                                    Error = TEXT("Failed to connect material expressions.");
                                }
                                else
                                {
                                    bChanged = true;
                                    GraphEventName = TEXT("graph.node_connected");
                                    GraphEventData->SetStringField(TEXT("fromNodeId"), FromNodeId);
                                    GraphEventData->SetStringField(TEXT("fromPin"), FromPinName);
                                    GraphEventData->SetStringField(TEXT("toNodeId"), ToNodeId);
                                    GraphEventData->SetStringField(TEXT("toPin"), ToPinName);
                                    GraphEventData->SetStringField(TEXT("op"), Op);
                                }
                            }
                        }
                    }
                    else if (Op.Equals(TEXT("disconnectpins")) || Op.Equals(TEXT("breakpinlinks")))
                    {
                        FString TargetNodeId;
                        FString TargetPinName;
                        if (Op.Equals(TEXT("disconnectpins")))
                        {
                            const TSharedPtr<FJsonObject>* ToObj = nullptr;
                            if (!ArgsObj->TryGetObjectField(TEXT("to"), ToObj) || !ToObj || !(*ToObj).IsValid() || !ResolveNodeTokenLocal(*ToObj, TargetNodeId))
                            {
                                bOk = false;
                                Error = TEXT("disconnectPins requires args.to.");
                            }
                            else
                            {
                                (*ToObj)->TryGetStringField(TEXT("pinName"), TargetPinName);
                            }
                        }
                        else
                        {
                            const TSharedPtr<FJsonObject>* TargetObj = nullptr;
                            if (!ArgsObj->TryGetObjectField(TEXT("target"), TargetObj) || !TargetObj || !(*TargetObj).IsValid() || !ResolveNodeTokenLocal(*TargetObj, TargetNodeId))
                            {
                                bOk = false;
                                Error = TEXT("breakPinLinks requires args.target.");
                            }
                            else
                            {
                                (*TargetObj)->TryGetStringField(TEXT("pinName"), TargetPinName);
                            }
                        }

                        if (bOk)
                        {
                            UMaterialExpression* Expr = FindMaterialExpressionById(MaterialAsset, TargetNodeId);
                            if (Expr == nullptr)
                            {
                                bOk = false;
                                Error = TEXT("Material expression not found.");
                            }
                            else
                            {
                                bool bDisconnected = false;
                                if (TargetPinName.IsEmpty())
                                {
                                    const int32 MaxInputs = 128;
                                    for (int32 InputIndex = 0; InputIndex < MaxInputs; ++InputIndex)
                                    {
                                        if (FExpressionInput* Input = Expr->GetInput(InputIndex))
                                        {
                                            if (Input->Expression != nullptr)
                                            {
                                                Input->Expression = nullptr;
                                                Input->OutputIndex = 0;
                                                bDisconnected = true;
                                            }
                                        }
                                        else
                                        {
                                            break;
                                        }
                                    }
                                }
                                else
                                {
                                    const int32 InputIndex = FindMaterialInputIndexByName(Expr, TargetPinName);
                                    if (InputIndex != INDEX_NONE)
                                    {
                                        if (FExpressionInput* Input = Expr->GetInput(InputIndex))
                                        {
                                            if (Input->Expression != nullptr)
                                            {
                                                Input->Expression = nullptr;
                                                Input->OutputIndex = 0;
                                                bDisconnected = true;
                                            }
                                        }
                                    }
                                }

                                if (!bDisconnected)
                                {
                                    bOk = false;
                                    Error = TEXT("No links were removed.");
                                }
                                else
                                {
                                    bChanged = true;
                                    GraphEventName = TEXT("graph.links_changed");
                                    GraphEventData->SetStringField(TEXT("nodeId"), TargetNodeId);
                                    GraphEventData->SetStringField(TEXT("pinName"), TargetPinName);
                                    GraphEventData->SetStringField(TEXT("op"), Op);
                                }
                            }
                        }
                    }
                    else if (Op.Equals(TEXT("compile")))
                    {
                        UMaterialEditingLibrary::RecompileMaterial(MaterialAsset);
                        bChanged = true;
                        GraphEventName = TEXT("graph.compiled");
                        GraphEventData->SetStringField(TEXT("op"), Op);
                    }
                    else
                    {
                        bOk = false;
                        Error = FString::Printf(TEXT("Unsupported op for material: %s"), *Op);
                    }
                }
                else if (GraphType.Equals(TEXT("pcg")))
                {
                    if (Op.Equals(TEXT("addnode.byclass")) || Op.Equals(TEXT("addnode.byaction")))
                    {
                        FString SettingsClassPath;
                        if (Op.Equals(TEXT("addnode.byclass")))
                        {
                            ArgsObj->TryGetStringField(TEXT("nodeClassPath"), SettingsClassPath);
                        }
                        else
                        {
                            FString ActionToken;
                            ArgsObj->TryGetStringField(TEXT("actionToken"), ActionToken);
                            ArgsObj->TryGetStringField(TEXT("actionId"), SettingsClassPath);
                            if (!ActionToken.IsEmpty())
                            {
                                FGraphActionTokenEntry TokenEntry;
                                FString ErrorCode;
                                if (ResolveGraphActionToken(ActionToken, GraphType, AssetPath, GraphName, TokenEntry, ErrorCode, Error))
                                {
                                    if (!TokenEntry.LegacyActionId.IsEmpty())
                                    {
                                        SettingsClassPath = TokenEntry.LegacyActionId;
                                    }
                                }
                                else
                                {
                                    bOk = false;
                                }
                            }
                        }

                        UClass* SettingsClass = LoadObject<UClass>(nullptr, *SettingsClassPath);
                        if (bOk && (SettingsClass == nullptr || !SettingsClass->IsChildOf(UPCGSettings::StaticClass())))
                        {
                            bOk = false;
                            Error = TEXT("Invalid PCG settings class.");
                        }

                        if (bOk)
                        {
                            UPCGSettings* DefaultSettings = nullptr;
                            UPCGNode* NewNode = PcgGraph->AddNodeOfType(SettingsClass, DefaultSettings);
                            if (NewNode == nullptr)
                            {
                                bOk = false;
                                Error = TEXT("Failed to create PCG node.");
                            }
                            else
                            {
                                int32 X = 0;
                                int32 Y = 0;
                                GetPointFromObjectLocal(ArgsObj, X, Y);
                                NewNode->SetNodePosition(X, Y);
                                NodeId = NewNode->GetPathName();
                                bChanged = true;
                                GraphEventName = TEXT("graph.node_added");
                                GraphEventData->SetStringField(TEXT("nodeId"), NodeId);
                                GraphEventData->SetStringField(TEXT("nodeType"), Op.Equals(TEXT("addnode.byaction")) ? TEXT("by_action") : TEXT("by_class"));
                                GraphEventData->SetStringField(TEXT("nodeClassPath"), SettingsClassPath);
                                GraphEventData->SetStringField(TEXT("op"), Op);
                            }
                        }
                    }
                    else if (Op.Equals(TEXT("removenode")))
                    {
                        FString TargetNodeId;
                        bOk = ResolveNodeTokenLocal(ArgsObj, TargetNodeId);
                        if (!bOk)
                        {
                            Error = TEXT("Missing target nodeId.");
                        }
                        else if (UPCGNode* Node = FindPcgNodeById(PcgGraph, TargetNodeId))
                        {
                            PcgGraph->RemoveNode(Node);
                            bChanged = true;
                            GraphEventName = TEXT("graph.node_removed");
                            GraphEventData->SetStringField(TEXT("nodeId"), TargetNodeId);
                            GraphEventData->SetStringField(TEXT("op"), Op);
                        }
                        else
                        {
                            bOk = false;
                            Error = TEXT("PCG node not found.");
                        }
                    }
                    else if (Op.Equals(TEXT("movenode")))
                    {
                        FString TargetNodeId;
                        bOk = ResolveNodeTokenLocal(ArgsObj, TargetNodeId);
                        if (!bOk)
                        {
                            Error = TEXT("Missing target nodeId.");
                        }
                        else if (UPCGNode* Node = FindPcgNodeById(PcgGraph, TargetNodeId))
                        {
                            int32 X = 0;
                            int32 Y = 0;
                            GetPointFromObjectLocal(ArgsObj, X, Y);
                            Node->SetNodePosition(X, Y);
                            bChanged = true;
                            GraphEventName = TEXT("graph.node_moved");
                            GraphEventData->SetStringField(TEXT("nodeId"), TargetNodeId);
                            GraphEventData->SetNumberField(TEXT("x"), X);
                            GraphEventData->SetNumberField(TEXT("y"), Y);
                            GraphEventData->SetStringField(TEXT("op"), Op);
                        }
                        else
                        {
                            bOk = false;
                            Error = TEXT("PCG node not found.");
                        }
                    }
                    else if (Op.Equals(TEXT("connectpins")) || Op.Equals(TEXT("disconnectpins")))
                    {
                        const TSharedPtr<FJsonObject>* FromObj = nullptr;
                        const TSharedPtr<FJsonObject>* ToObj = nullptr;
                        FString FromNodeId;
                        FString ToNodeId;
                        FString FromPinName;
                        FString ToPinName;
                        if (!ArgsObj->TryGetObjectField(TEXT("from"), FromObj) || !FromObj || !(*FromObj).IsValid()
                            || !ArgsObj->TryGetObjectField(TEXT("to"), ToObj) || !ToObj || !(*ToObj).IsValid()
                            || !ResolveNodeTokenLocal(*FromObj, FromNodeId)
                            || !ResolveNodeTokenLocal(*ToObj, ToNodeId))
                        {
                            bOk = false;
                            Error = FString::Printf(TEXT("%s requires from/to node references."), *Op);
                        }
                        else
                        {
                            (*FromObj)->TryGetStringField(TEXT("pinName"), FromPinName);
                            (*ToObj)->TryGetStringField(TEXT("pinName"), ToPinName);
                            UPCGNode* FromNode = FindPcgNodeById(PcgGraph, FromNodeId);
                            UPCGNode* ToNode = FindPcgNodeById(PcgGraph, ToNodeId);
                            if (FromNode == nullptr || ToNode == nullptr)
                            {
                                bOk = false;
                                Error = TEXT("PCG node not found.");
                            }
                            else if (Op.Equals(TEXT("connectpins")))
                            {
                                bOk = (PcgGraph->AddEdge(FromNode, FName(*FromPinName), ToNode, FName(*ToPinName)) != nullptr);
                                if (bOk)
                                {
                                    bChanged = true;
                                    GraphEventName = TEXT("graph.node_connected");
                                    GraphEventData->SetStringField(TEXT("fromNodeId"), FromNodeId);
                                    GraphEventData->SetStringField(TEXT("fromPin"), FromPinName);
                                    GraphEventData->SetStringField(TEXT("toNodeId"), ToNodeId);
                                    GraphEventData->SetStringField(TEXT("toPin"), ToPinName);
                                    GraphEventData->SetStringField(TEXT("op"), Op);
                                }
                                else
                                {
                                    Error = TEXT("Failed to add PCG edge.");
                                }
                            }
                            else
                            {
                                bOk = PcgGraph->RemoveEdge(FromNode, FName(*FromPinName), ToNode, FName(*ToPinName));
                                if (bOk)
                                {
                                    bChanged = true;
                                    GraphEventName = TEXT("graph.links_changed");
                                    GraphEventData->SetStringField(TEXT("fromNodeId"), FromNodeId);
                                    GraphEventData->SetStringField(TEXT("fromPin"), FromPinName);
                                    GraphEventData->SetStringField(TEXT("toNodeId"), ToNodeId);
                                    GraphEventData->SetStringField(TEXT("toPin"), ToPinName);
                                    GraphEventData->SetStringField(TEXT("op"), Op);
                                }
                                else
                                {
                                    Error = TEXT("Failed to remove PCG edge.");
                                }
                            }
                        }
                    }
                    else if (Op.Equals(TEXT("breakpinlinks")))
                    {
                        const TSharedPtr<FJsonObject>* TargetObj = nullptr;
                        FString TargetNodeId;
                        FString TargetPinName;
                        if (!ArgsObj->TryGetObjectField(TEXT("target"), TargetObj) || !TargetObj || !(*TargetObj).IsValid() || !ResolveNodeTokenLocal(*TargetObj, TargetNodeId))
                        {
                            bOk = false;
                            Error = TEXT("breakPinLinks requires args.target.");
                        }
                        else
                        {
                            (*TargetObj)->TryGetStringField(TEXT("pinName"), TargetPinName);
                            UPCGNode* Node = FindPcgNodeById(PcgGraph, TargetNodeId);
                            if (Node == nullptr)
                            {
                                bOk = false;
                                Error = TEXT("PCG node not found.");
                            }
                            else
                            {
                                UPCGPin* Pin = FindPcgPin(Node, TargetPinName, false);
                                if (Pin == nullptr)
                                {
                                    Pin = FindPcgPin(Node, TargetPinName, true);
                                }
                                if (Pin == nullptr)
                                {
                                    bOk = false;
                                    Error = TEXT("PCG pin not found.");
                                }
                                else
                                {
                                    bOk = Pin->BreakAllEdges();
                                    if (bOk)
                                    {
                                        bChanged = true;
                                        GraphEventName = TEXT("graph.links_changed");
                                        GraphEventData->SetStringField(TEXT("nodeId"), TargetNodeId);
                                        GraphEventData->SetStringField(TEXT("pinName"), TargetPinName);
                                        GraphEventData->SetStringField(TEXT("op"), Op);
                                    }
                                    else
                                    {
                                        Error = TEXT("No links were removed.");
                                    }
                                }
                            }
                        }
                    }
                    else if (Op.Equals(TEXT("compile")))
                    {
                        bOk = PcgGraph->Recompile();
                        if (bOk)
                        {
                            bChanged = true;
                            GraphEventName = TEXT("graph.compiled");
                            GraphEventData->SetStringField(TEXT("op"), Op);
                        }
                        else
                        {
                            Error = TEXT("PCG graph compile failed.");
                        }
                    }
                    else
                    {
                        bOk = false;
                        Error = FString::Printf(TEXT("Unsupported op for pcg: %s"), *Op);
                    }
                }
            }

            if (bChanged && MutableAsset != nullptr)
            {
                MutableAsset->MarkPackageDirty();
            }

            if (bChanged && MaterialAsset != nullptr)
            {
                MaterialAsset->PostEditChange();
            }

            if (bOk && !ClientRef.IsEmpty() && !NodeId.IsEmpty())
            {
                LocalNodeRefs.Add(ClientRef, NodeId);
            }

            TSharedPtr<FJsonObject> OpResult = MakeShared<FJsonObject>();
            OpResult->SetNumberField(TEXT("index"), Index);
            OpResult->SetStringField(TEXT("op"), Op);
            OpResult->SetBoolField(TEXT("ok"), bOk);
            if (!NodeId.IsEmpty())
            {
                OpResult->SetStringField(TEXT("nodeId"), NodeId);
            }
            OpResult->SetBoolField(TEXT("changed"), bChanged);
            OpResult->SetStringField(TEXT("error"), bOk ? TEXT("") : Error);
            LocalOpResults.Add(MakeShared<FJsonValueObject>(OpResult));

            if (!bOk)
            {
                bAnyErrorLocal = true;
                if (FirstErrorLocal.IsEmpty())
                {
                    FirstErrorLocal = Error;
                }
                if (bStopOnError)
                {
                    break;
                }
            }
        }

        Result->SetBoolField(TEXT("isError"), bAnyErrorLocal);
        if (bAnyErrorLocal)
        {
            Result->SetStringField(TEXT("code"), TEXT("INTERNAL_ERROR"));
            Result->SetStringField(TEXT("message"), FirstErrorLocal.IsEmpty() ? TEXT("graph.mutate failed") : FirstErrorLocal);
        }
        Result->SetBoolField(TEXT("applied"), !bAnyErrorLocal);
        Result->SetStringField(TEXT("graphType"), GraphType);
        Result->SetStringField(TEXT("assetPath"), AssetPath);
        Result->SetStringField(TEXT("graphName"), GraphName);
        Result->SetStringField(TEXT("previousRevision"), FString::Printf(TEXT("%s:%08x"), GraphType.Equals(TEXT("material")) ? TEXT("mat") : TEXT("pcg"), GetTypeHash(AssetPath + TEXT("|prev"))));
        Result->SetStringField(TEXT("newRevision"), FString::Printf(TEXT("%s:%08x"), GraphType.Equals(TEXT("material")) ? TEXT("mat") : TEXT("pcg"), GetTypeHash(AssetPath + TEXT("|new") + FString::FromInt(LocalOpResults.Num()))));
        Result->SetArrayField(TEXT("opResults"), LocalOpResults);
        Result->SetArrayField(TEXT("diagnostics"), TArray<TSharedPtr<FJsonValue>>{});
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
                FString ActionToken;
                FString ActionId;
                ArgsObj->TryGetStringField(TEXT("actionToken"), ActionToken);
                ArgsObj->TryGetStringField(TEXT("actionId"), ActionId);
                int32 X = 0;
                int32 Y = 0;
                GetPointFromObject(ArgsObj, X, Y);

                if (!ActionToken.IsEmpty())
                {
                    FGraphActionTokenEntry TokenEntry;
                    FString ErrorCode;
                    bOk = ResolveGraphActionToken(ActionToken, GraphType, AssetPath, OpGraphName, TokenEntry, ErrorCode, Error);
                    if (!bOk && !ErrorCode.IsEmpty() && !Error.IsEmpty())
                    {
                        Error = ErrorCode + TEXT(": ") + Error;
                    }
                    if (bOk)
                    {
                        UBlueprint* Blueprint = LoadBlueprintByAssetPath(AssetPath);
                        UEdGraph* TargetGraph = ResolveBlueprintGraph(Blueprint, OpGraphName);
                        if (Blueprint == nullptr || TargetGraph == nullptr)
                        {
                            bOk = false;
                            Error = TEXT("Failed to resolve blueprint/target graph.");
                        }
                        else
                        {
                            UEdGraphPin* SourcePin = nullptr;
                            if (!TokenEntry.FromNodeId.IsEmpty() && !TokenEntry.FromPinName.IsEmpty())
                            {
                                if (UEdGraphNode* SourceNode = FindNodeByGuid(TargetGraph, TokenEntry.FromNodeId))
                                {
                                    SourcePin = FindPinByName(SourceNode, TokenEntry.FromPinName);
                                }
                            }

                            UEdGraphNode* NewNode = nullptr;
                            if (TokenEntry.Action.IsValid())
                            {
                                NewNode = TokenEntry.Action->PerformAction(TargetGraph, SourcePin, FVector2f(static_cast<float>(X), static_cast<float>(Y)), true);
                            }

                            if (NewNode == nullptr)
                            {
                                if (!TokenEntry.LegacyActionId.IsEmpty())
                                {
                                    bOk = ULoomleBlueprintAdapter::AddNodeByAction(AssetPath, OpGraphName, TokenEntry.LegacyActionId, SerializeJsonObject(ArgsObj), X, Y, NodeId, Error);
                                    if (bOk && ActionId.IsEmpty())
                                    {
                                        ActionId = TokenEntry.LegacyActionId;
                                    }
                                }
                                else
                                {
                                    bOk = false;
                                    Error = TEXT("Action token execution produced no node.");
                                }
                            }
                            else
                            {
                                NodeId = NewNode->NodeGuid.ToString(EGuidFormats::DigitsWithHyphensLower);
                            }
                        }
                    }
                }
                else
                {
                    bOk = ULoomleBlueprintAdapter::AddNodeByAction(AssetPath, OpGraphName, ActionId, SerializeJsonObject(ArgsObj), X, Y, NodeId, Error);
                }

                if (bOk)
                {
                    GraphEventName = TEXT("graph.node_added");
                    GraphEventData->SetStringField(TEXT("nodeId"), NodeId);
                    GraphEventData->SetStringField(TEXT("nodeType"), TEXT("by_action"));
                    if (!ActionToken.IsEmpty())
                    {
                        GraphEventData->SetStringField(TEXT("actionToken"), ActionToken);
                    }
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
