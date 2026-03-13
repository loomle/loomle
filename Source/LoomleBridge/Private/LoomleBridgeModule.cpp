#include "LoomleBridgeModule.h"

#include "Async/Async.h"
#include "Async/Future.h"
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
#include "Materials/MaterialExpressionMaterialFunctionCall.h"
#include "Materials/MaterialFunction.h"
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
#include "ScopedTransaction.h"
#include "Misc/App.h"
#include "Misc/Base64.h"
#include "Misc/DateTime.h"
#include "Misc/EngineVersion.h"
#include "Misc/FileHelper.h"
#include "Misc/Guid.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Misc/ScopeLock.h"
#include "Misc/OutputDevice.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/UObjectIterator.h"
#include "UObject/UnrealType.h"
#include "Misc/TransactionObjectEvent.h"
#include "Widgets/SWidget.h"
#include "Widgets/SWindow.h"
#include "BlueprintEditor.h"

DEFINE_LOG_CATEGORY_STATIC(LogLoomleBridge, Log, All);

using FCondensedJsonWriter = TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>;

namespace LoomleBridgeConstants
{
    static const TCHAR* PipeNamePrefix = TEXT("loomle");
    static const TCHAR* RpcVersion = TEXT("1.0");
    static const TCHAR* ExecuteToolName = TEXT("execute");
    static const TCHAR* GraphListToolName = TEXT("graph.list");
    static const TCHAR* GraphQueryToolName = TEXT("graph.query");
    static const TCHAR* GraphActionsToolName = TEXT("graph.actions");
    static const TCHAR* GraphMutateToolName = TEXT("graph.mutate");
    static const TCHAR* DiagTailToolName = TEXT("diag.tail");
    constexpr double GraphActionTokenTtlSeconds = 300.0;
    constexpr int32 MaxGraphActionTokenRegistryEntries = 2048;
}

namespace
{
class FLoomleDiagLogCaptureOutputDevice final : public FOutputDevice
{
public:
    explicit FLoomleDiagLogCaptureOutputDevice(TFunction<void(const FString&, ELogVerbosity::Type, const FName&)>&& InOnLine)
        : OnLine(MoveTemp(InOnLine))
    {
    }

    virtual bool CanBeUsedOnAnyThread() const override
    {
        return true;
    }

    virtual void Serialize(const TCHAR* V, ELogVerbosity::Type Verbosity, const FName& Category) override
    {
        if (!OnLine || V == nullptr)
        {
            return;
        }

        const ELogVerbosity::Type VerbosityMask = static_cast<ELogVerbosity::Type>(Verbosity & ELogVerbosity::VerbosityMask);
        if (VerbosityMask != ELogVerbosity::Warning
            && VerbosityMask != ELogVerbosity::Error
            && VerbosityMask != ELogVerbosity::Fatal)
        {
            return;
        }

        OnLine(FString(V).TrimStartAndEnd(), VerbosityMask, Category);
    }

private:
    TFunction<void(const FString&, ELogVerbosity::Type, const FName&)> OnLine;
};

#if PLATFORM_WINDOWS
uint64 StableFnv1a64(const FString& Input)
{
    constexpr uint64 OffsetBasis = 0xcbf29ce484222325ull;
    constexpr uint64 Prime = 0x100000001b3ull;

    FTCHARToUTF8 Utf8(*Input);
    const uint8* Bytes = reinterpret_cast<const uint8*>(Utf8.Get());
    uint64 Hash = OffsetBasis;
    for (int32 Index = 0; Index < Utf8.Length(); ++Index)
    {
        Hash ^= static_cast<uint64>(Bytes[Index]);
        Hash *= Prime;
    }
    return Hash;
}

FString NormalizeProjectRootForPipeName()
{
    FString ProjectRoot = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir());
    FPaths::NormalizeFilename(ProjectRoot);
    while (ProjectRoot.EndsWith(TEXT("/")))
    {
        ProjectRoot.LeftChopInline(1, EAllowShrinking::No);
    }

    if (ProjectRoot.IsEmpty())
    {
        ProjectRoot = TEXT("/");
    }

    ProjectRoot.ToLowerInline();
    return ProjectRoot;
}

FString GetRpcPipeNameForCurrentProject()
{
    const uint64 Hash = StableFnv1a64(NormalizeProjectRootForPipeName());
    return FString::Printf(TEXT("%s-%016llx"), LoomleBridgeConstants::PipeNamePrefix, static_cast<unsigned long long>(Hash));
}
#endif

FString NormalizeGraphType(FString GraphType)
{
    GraphType = GraphType.TrimStartAndEnd().ToLower();
    if (GraphType.IsEmpty())
    {
        return TEXT("blueprint");
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

enum class EGraphSelectionDomain : uint8
{
    Unknown,
    Blueprint,
    Material,
    Pcg
};

bool CollectSelectedGraphObjectsFromActiveWindow(TArray<UObject*>& OutSelectedObjects, EGraphSelectionDomain& OutDomain);

bool CollectSelectedBlueprintNodes(TArray<UEdGraphNode*>& OutNodes, UBlueprint*& OutBlueprint)
{
    OutNodes.Reset();
    OutBlueprint = nullptr;

    UBlueprint* EditedBlueprint = FindEditedBlueprint();
    if (!EditedBlueprint)
    {
        return false;
    }

    TArray<UObject*> SelectedObjects;
    EGraphSelectionDomain SelectedDomain = EGraphSelectionDomain::Unknown;
    if (!CollectSelectedGraphObjectsFromActiveWindow(SelectedObjects, SelectedDomain)
        || SelectedDomain != EGraphSelectionDomain::Blueprint)
    {
        return false;
    }

    for (UObject* SelectedObject : SelectedObjects)
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
    if (!EditedMaterial)
    {
        return false;
    }

    TArray<UObject*> SelectedObjects;
    EGraphSelectionDomain SelectedDomain = EGraphSelectionDomain::Unknown;
    if (!CollectSelectedGraphObjectsFromActiveWindow(SelectedObjects, SelectedDomain)
        || SelectedDomain != EGraphSelectionDomain::Material)
    {
        return false;
    }

    for (UObject* SelectedObject : SelectedObjects)
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

TSharedPtr<SWindow> GetActiveWindow()
{
    if (!FSlateApplication::IsInitialized())
    {
        return nullptr;
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
    return ActiveWindow;
}

void CollectGraphEditorsFromWidgetTree(const TSharedRef<SWidget>& RootWidget, TArray<TSharedPtr<SGraphEditor>>& OutGraphEditors)
{
    TArray<TSharedRef<SWidget>> Stack;
    Stack.Add(RootWidget);

    while (Stack.Num() > 0)
    {
        const TSharedRef<SWidget> CurrentWidget = Stack.Pop(EAllowShrinking::No);

        if (CurrentWidget->GetType() == FName(TEXT("SGraphEditor")))
        {
            OutGraphEditors.Add(StaticCastSharedRef<SGraphEditor>(CurrentWidget));
        }

        FChildren* Children = CurrentWidget->GetAllChildren();
        if (!Children)
        {
            continue;
        }

        for (int32 ChildIndex = 0; ChildIndex < Children->Num(); ++ChildIndex)
        {
            TSharedRef<SWidget> ChildWidget = Children->GetChildAt(ChildIndex);
            Stack.Add(ChildWidget);
        }
    }
}

EGraphSelectionDomain DetectGraphSelectionDomain(const TArray<UObject*>& SelectedObjects)
{
    bool bHasPcgNode = false;
    bool bHasMaterialNode = false;
    bool bHasGenericGraphNode = false;

    for (UObject* SelectedObject : SelectedObjects)
    {
        if (!SelectedObject)
        {
            continue;
        }

        if (SelectedObject->IsA<UMaterialExpression>() || SelectedObject->IsA<UMaterialGraphNode>())
        {
            bHasMaterialNode = true;
        }

        if (UEdGraphNode* GraphNode = Cast<UEdGraphNode>(SelectedObject))
        {
            bHasGenericGraphNode = true;

            if (GraphNode->IsA<UMaterialGraphNode>())
            {
                bHasMaterialNode = true;
            }

            const UClass* NodeClass = GraphNode->GetClass();
            const FString NodeClassPath = NodeClass ? NodeClass->GetPathName() : FString();
            if (NodeClassPath.Contains(TEXT("PCGEditorGraphNode")))
            {
                bHasPcgNode = true;
            }
        }
    }

    if (bHasPcgNode)
    {
        return EGraphSelectionDomain::Pcg;
    }
    if (bHasMaterialNode)
    {
        return EGraphSelectionDomain::Material;
    }
    if (bHasGenericGraphNode)
    {
        return EGraphSelectionDomain::Blueprint;
    }
    return EGraphSelectionDomain::Unknown;
}

bool CollectSelectedGraphObjectsFromActiveWindow(TArray<UObject*>& OutSelectedObjects, EGraphSelectionDomain& OutDomain)
{
    OutSelectedObjects.Reset();
    OutDomain = EGraphSelectionDomain::Unknown;

    TSharedPtr<SWindow> ActiveWindow = GetActiveWindow();
    if (!ActiveWindow.IsValid())
    {
        return false;
    }

    TArray<TSharedPtr<SGraphEditor>> GraphEditors;
    CollectGraphEditorsFromWidgetTree(ActiveWindow.ToSharedRef(), GraphEditors);

    int32 BestScore = TNumericLimits<int32>::Min();
    TArray<UObject*> BestSelection;
    EGraphSelectionDomain BestDomain = EGraphSelectionDomain::Unknown;

    for (const TSharedPtr<SGraphEditor>& GraphEditor : GraphEditors)
    {
        if (!GraphEditor.IsValid())
        {
            continue;
        }

        const FGraphPanelSelectionSet& SelectedNodes = GraphEditor->GetSelectedNodes();
        if (SelectedNodes.Num() == 0)
        {
            continue;
        }

        TArray<UObject*> CurrentSelection;
        CurrentSelection.Reserve(SelectedNodes.Num());
        for (UObject* SelectedObject : SelectedNodes)
        {
            if (!SelectedObject)
            {
                continue;
            }
            CurrentSelection.Add(SelectedObject);
        }

        if (CurrentSelection.Num() == 0)
        {
            continue;
        }

        const EGraphSelectionDomain CurrentDomain = DetectGraphSelectionDomain(CurrentSelection);
        int32 Score = CurrentSelection.Num();
        if (CurrentDomain != EGraphSelectionDomain::Unknown)
        {
            Score += 1000;
        }
        if (CurrentDomain == EGraphSelectionDomain::Material || CurrentDomain == EGraphSelectionDomain::Pcg)
        {
            Score += 100;
        }

        if (Score > BestScore)
        {
            BestScore = Score;
            BestSelection = MoveTemp(CurrentSelection);
            BestDomain = CurrentDomain;
        }
    }

    if (BestSelection.Num() == 0)
    {
        return false;
    }

    OutSelectedObjects = MoveTemp(BestSelection);
    OutDomain = BestDomain;
    return true;
}

UPCGNode* ResolvePcgNodeFromEditorNode(UEdGraphNode* GraphNode)
{
    if (!GraphNode)
    {
        return nullptr;
    }

    FObjectPropertyBase* PcgNodeProperty = FindFProperty<FObjectPropertyBase>(GraphNode->GetClass(), TEXT("PCGNode"));
    if (!PcgNodeProperty)
    {
        return nullptr;
    }

    UObject* PcgNodeObject = PcgNodeProperty->GetObjectPropertyValue_InContainer(GraphNode);
    return Cast<UPCGNode>(PcgNodeObject);
}

bool BuildPcgSelectionSnapshot(TSharedPtr<FJsonObject>& OutSelection)
{
    OutSelection.Reset();

    UObject* PcgAsset = FindEditedPcgAsset();
    if (!PcgAsset)
    {
        return false;
    }

    TArray<UObject*> SelectedObjects;
    EGraphSelectionDomain SelectedDomain = EGraphSelectionDomain::Unknown;
    if (!CollectSelectedGraphObjectsFromActiveWindow(SelectedObjects, SelectedDomain)
        || SelectedDomain != EGraphSelectionDomain::Pcg)
    {
        return false;
    }

    TArray<TSharedPtr<FJsonValue>> Items;
    Items.Reserve(SelectedObjects.Num());

    for (UObject* SelectedObject : SelectedObjects)
    {
        UEdGraphNode* GraphNode = Cast<UEdGraphNode>(SelectedObject);
        if (!GraphNode)
        {
            continue;
        }

        UPCGNode* PcgNode = ResolvePcgNodeFromEditorNode(GraphNode);
        if (!PcgNode)
        {
            continue;
        }

        int32 NodePosX = GraphNode->NodePosX;
        int32 NodePosY = GraphNode->NodePosY;
        PcgNode->GetNodePosition(NodePosX, NodePosY);

        const UClass* NodeClass = PcgNode->GetClass();
        TSharedPtr<FJsonObject> Item = MakeShared<FJsonObject>();
        Item->SetStringField(TEXT("id"), PcgNode->GetPathName());
        Item->SetStringField(TEXT("name"), PcgNode->NodeTitle.IsNone() ? PcgNode->GetName() : PcgNode->NodeTitle.ToString());
        Item->SetStringField(TEXT("class"), NodeClass ? NodeClass->GetPathName() : TEXT(""));
        Item->SetStringField(TEXT("path"), PcgNode->GetPathName());
        Item->SetNumberField(TEXT("nodePosX"), NodePosX);
        Item->SetNumberField(TEXT("nodePosY"), NodePosY);
        if (UEdGraph* Graph = GraphNode->GetGraph())
        {
            Item->SetStringField(TEXT("graphName"), Graph->GetName());
            Item->SetStringField(TEXT("graphPath"), Graph->GetPathName());
        }
        Items.Add(MakeShared<FJsonValueObject>(Item));
    }

    if (Items.Num() == 0)
    {
        return false;
    }

    OutSelection = MakeShared<FJsonObject>();
    OutSelection->SetBoolField(TEXT("isError"), false);
    OutSelection->SetStringField(TEXT("editorType"), TEXT("pcg"));
    OutSelection->SetStringField(TEXT("provider"), TEXT("pcg"));
    OutSelection->SetStringField(TEXT("assetPath"), PcgAsset->GetPathName());
    OutSelection->SetStringField(TEXT("selectionKind"), TEXT("graph_node"));
    OutSelection->SetArrayField(TEXT("items"), Items);
    OutSelection->SetNumberField(TEXT("count"), Items.Num());
    return true;
}

}

bool FLoomleBridgeModule::TickHealthSnapshot(float DeltaTime)
{
    (void)DeltaTime;
    UpdateHealthSnapshot();
    return true;
}

void FLoomleBridgeModule::UpdateHealthSnapshot()
{
    const bool bBridgeRunning = PipeServer.IsValid();
    const IPythonScriptPlugin* PythonScriptPlugin = IPythonScriptPlugin::Get();
    const bool bPythonReady = PythonScriptPlugin != nullptr && PythonScriptPlugin->IsPythonInitialized();
    const bool bIsPIE = GEditor != nullptr && GEditor->IsPlayingSessionInEditor();

    bBridgeRunningSnapshot.Store(bBridgeRunning);
    bPythonReadySnapshot.Store(bPythonReady);
    bIsPIESnapshot.Store(bIsPIE);
}

void FLoomleBridgeModule::StartupModule()
{
#if PLATFORM_WINDOWS
    const FString PipeName = GetRpcPipeNameForCurrentProject();
#endif
    PipeServer = MakeShared<FLoomlePipeServer, ESPMode::ThreadSafe>(
#if PLATFORM_WINDOWS
        PipeName,
#else
        LoomleBridgeConstants::PipeNamePrefix,
#endif
        [this](const FString& RequestLine)
        {
            return HandleRequest(RequestLine);
        });

    if (!PipeServer->Start())
    {
        UE_LOG(LogLoomleBridge, Error, TEXT("Failed to start Loomle pipe server."));
        PipeServer.Reset();
        bBridgeRunningSnapshot.Store(false);
        bPythonReadySnapshot.Store(false);
        bIsPIESnapshot.Store(false);
        return;
    }

    UpdateHealthSnapshot();
    InitializeDiagStore();
    if (GLog != nullptr && DiagLogOutputDevice == nullptr)
    {
        DiagLogOutputDevice = new FLoomleDiagLogCaptureOutputDevice(
            [this](const FString& Message, ELogVerbosity::Type Verbosity, const FName& Category)
            {
                HandleLogLine(Message, Verbosity, Category);
            });
        GLog->AddOutputDevice(DiagLogOutputDevice);
    }
    if (GEditor != nullptr && !BlueprintCompiledHandle.IsValid())
    {
        BlueprintCompiledHandle = GEditor->OnBlueprintCompiled().AddRaw(this, &FLoomleBridgeModule::HandleBlueprintCompiled);
    }
    HealthSnapshotTickerHandle = FTSTicker::GetCoreTicker().AddTicker(
        FTickerDelegate::CreateRaw(this, &FLoomleBridgeModule::TickHealthSnapshot),
        0.1f);

#if PLATFORM_WINDOWS
    UE_LOG(LogLoomleBridge, Display, TEXT("Loomle bridge started on named pipe \\\\.\\pipe\\%s"), *PipeName);
#else
    UE_LOG(LogLoomleBridge, Display, TEXT("Loomle bridge started on unix socket %s"), *FPaths::Combine(FPaths::ProjectIntermediateDir(), TEXT("loomle.sock")));
#endif

    bGraphMutateInProgress.Store(false);
}

void FLoomleBridgeModule::ShutdownModule()
{
    if (BlueprintCompiledHandle.IsValid())
    {
        if (GEditor != nullptr)
        {
            GEditor->OnBlueprintCompiled().Remove(BlueprintCompiledHandle);
        }
        BlueprintCompiledHandle.Reset();
    }
    if (DiagLogOutputDevice != nullptr)
    {
        if (GLog != nullptr)
        {
            GLog->RemoveOutputDevice(DiagLogOutputDevice);
        }
        delete DiagLogOutputDevice;
        DiagLogOutputDevice = nullptr;
    }

    if (HealthSnapshotTickerHandle.IsValid())
    {
        FTSTicker::GetCoreTicker().RemoveTicker(HealthSnapshotTickerHandle);
        HealthSnapshotTickerHandle.Reset();
    }

    if (PipeServer.IsValid())
    {
        PipeServer->StopServer();
        PipeServer.Reset();
    }

    bBridgeRunningSnapshot.Store(false);
    bPythonReadySnapshot.Store(false);
    bIsPIESnapshot.Store(false);
}

#include "LoomleBridgeRpc.inl"

#include "LoomleBridgeGraph.inl"

#include "LoomleBridgeRuntime.inl"

IMPLEMENT_MODULE(FLoomleBridgeModule, LoomleBridge)
