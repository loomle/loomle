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
#include "ImageUtils.h"
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
#include "Materials/MaterialFunctionInterface.h"
#include "Materials/MaterialInterface.h"
#include "MaterialEditingLibrary.h"
#include "IMaterialEditor.h"
#include "MaterialGraph/MaterialGraph.h"
#include "MaterialGraph/MaterialGraphNode.h"
#include "MaterialGraph/MaterialGraphNode_Root.h"
#include "MaterialGraph/MaterialGraphSchema.h"
#include "PCGComponent.h"
#include "PCGGraph.h"
#include "PCGEdge.h"
#include "PCGNode.h"
#include "PCGPin.h"
#include "PCGSettings.h"
#include "PCGEditor.h"
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
#include "BlueprintEditorTabs.h"

DEFINE_LOG_CATEGORY_STATIC(LogLoomleBridge, Log, All);

using FCondensedJsonWriter = TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>;

namespace LoomleBridgeConstants
{
    static const TCHAR* PipeNamePrefix = TEXT("loomle");
    static const TCHAR* RpcVersion = TEXT("1.0");
    static const TCHAR* ExecuteToolName = TEXT("execute");
    static const TCHAR* EditorOpenToolName = TEXT("editor.open");
    static const TCHAR* EditorFocusToolName = TEXT("editor.focus");
    static const TCHAR* EditorScreenshotToolName = TEXT("editor.screenshot");
    static const TCHAR* GraphListToolName = TEXT("graph.list");
    static const TCHAR* GraphResolveToolName = TEXT("graph.resolve");
    static const TCHAR* GraphQueryToolName = TEXT("graph.query");
    static const TCHAR* GraphActionsToolName = TEXT("graph.actions");
    static const TCHAR* GraphMutateToolName = TEXT("graph.mutate");
    static const TCHAR* DiagTailToolName = TEXT("diag.tail");
    constexpr double GraphActionTokenTtlSeconds = 300.0;
    constexpr int32 MaxGraphActionTokenRegistryEntries = 2048;
    constexpr double MutateIdempotencyTtlSeconds = 1800.0;
    constexpr int32 MaxMutateIdempotencyEntries = 2048;
}

namespace
{
TSharedPtr<FJsonObject> CloneJsonObject(const TSharedPtr<FJsonObject>& Source)
{
    if (!Source.IsValid())
    {
        return nullptr;
    }

    FString Serialized;
    {
        const TSharedRef<FCondensedJsonWriter> Writer = TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Serialized);
        FJsonSerializer::Serialize(Source.ToSharedRef(), Writer);
    }

    TSharedPtr<FJsonObject> Clone = MakeShared<FJsonObject>();
    const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Serialized);
    if (!FJsonSerializer::Deserialize(Reader, Clone) || !Clone.IsValid())
    {
        return nullptr;
    }
    return Clone;
}

FString SerializeJsonObjectCondensed(const TSharedPtr<FJsonObject>& Source)
{
    if (!Source.IsValid())
    {
        return TEXT("");
    }

    FString Serialized;
    const TSharedRef<FCondensedJsonWriter> Writer = TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Serialized);
    FJsonSerializer::Serialize(Source.ToSharedRef(), Writer);
    return Serialized;
}

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

UPCGGraph* ResolvePcgGraphFromAsset(UObject* Asset);
UObject* FindEditedMaterialAsset();
UObject* FindEditedPcgAsset();

bool IsLikelyPcgAsset(const UObject* Asset)
{
    if (Asset == nullptr)
    {
        return false;
    }

    return ResolvePcgGraphFromAsset(const_cast<UObject*>(Asset)) != nullptr;
}

UPCGNode* ResolvePcgNodeFromEditorNode(UEdGraphNode* GraphNode);

bool TryGetAssetPathFromObject(const UObject* Object, FString& OutAssetPath)
{
    OutAssetPath.Empty();
    if (Object == nullptr)
    {
        return false;
    }

    const UPackage* Package = Object->GetPackage();
    OutAssetPath = Package ? Package->GetPathName() : Object->GetPathName();
    OutAssetPath = NormalizeAssetPath(OutAssetPath);
    return !OutAssetPath.IsEmpty() && FPackageName::IsValidLongPackageName(OutAssetPath);
}

bool IsTransientAssetPath(const FString& AssetPath)
{
    return AssetPath.StartsWith(TEXT("/Engine/Transient"));
}

TSharedPtr<FJsonObject> MakeAssetGraphRefJson(const FString& InAssetPath, const FString& InGraphName = FString())
{
    TSharedPtr<FJsonObject> GraphRef = MakeShared<FJsonObject>();
    GraphRef->SetStringField(TEXT("kind"), TEXT("asset"));
    GraphRef->SetStringField(TEXT("assetPath"), NormalizeAssetPath(InAssetPath));
    if (!InGraphName.IsEmpty())
    {
        GraphRef->SetStringField(TEXT("graphName"), InGraphName);
    }
    return GraphRef;
}

TSharedPtr<FJsonObject> MakeInlineGraphRefJson(const FString& InAssetPath, const FString& InNodeGuid)
{
    TSharedPtr<FJsonObject> GraphRef = MakeShared<FJsonObject>();
    GraphRef->SetStringField(TEXT("kind"), TEXT("inline"));
    GraphRef->SetStringField(TEXT("assetPath"), NormalizeAssetPath(InAssetPath));
    GraphRef->SetStringField(TEXT("nodeGuid"), InNodeGuid);
    return GraphRef;
}

FString MakeResolvedGraphRefKey(const FString& GraphType, const TSharedPtr<FJsonObject>& GraphRef)
{
    if (!GraphRef.IsValid())
    {
        return GraphType + TEXT("|invalid");
    }

    FString Kind;
    FString AssetPath;
    FString GraphName;
    FString NodeGuid;
    GraphRef->TryGetStringField(TEXT("kind"), Kind);
    GraphRef->TryGetStringField(TEXT("assetPath"), AssetPath);
    GraphRef->TryGetStringField(TEXT("graphName"), GraphName);
    GraphRef->TryGetStringField(TEXT("nodeGuid"), NodeGuid);

    return GraphType + TEXT("|") + Kind + TEXT("|") + NormalizeAssetPath(AssetPath) + TEXT("|") + GraphName + TEXT("|") + NodeGuid;
}

void AddResolvedGraphRefEntry(
    TArray<TSharedPtr<FJsonValue>>& OutRefs,
    TSet<FString>& SeenKeys,
    const FString& GraphType,
    const TSharedPtr<FJsonObject>& GraphRef,
    const FString& Relation,
    const FString& LoadStatus)
{
    if (!GraphRef.IsValid())
    {
        return;
    }

    const FString SeenKey = MakeResolvedGraphRefKey(GraphType, GraphRef);
    if (SeenKeys.Contains(SeenKey))
    {
        return;
    }

    SeenKeys.Add(SeenKey);

    TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
    Entry->SetStringField(TEXT("graphType"), GraphType);
    Entry->SetStringField(TEXT("relation"), Relation);
    Entry->SetStringField(TEXT("loadStatus"), LoadStatus);
    Entry->SetObjectField(TEXT("graphRef"), GraphRef);
    OutRefs.Add(MakeShared<FJsonValueObject>(Entry));
}

void SetResolvedGraphRefsFieldIfAny(const TSharedPtr<FJsonObject>& Target, const TArray<TSharedPtr<FJsonValue>>& Refs)
{
    if (Target.IsValid() && Refs.Num() > 0)
    {
        Target->SetArrayField(TEXT("resolvedGraphRefs"), Refs);
    }
}

void CopyResolvedGraphRefEntries(
    const TArray<TSharedPtr<FJsonValue>>& Source,
    TArray<TSharedPtr<FJsonValue>>& Dest,
    TSet<FString>& DestSeenKeys)
{
    for (const TSharedPtr<FJsonValue>& Value : Source)
    {
        const TSharedPtr<FJsonObject>* EntryObj = nullptr;
        if (!Value.IsValid() || !Value->TryGetObject(EntryObj) || EntryObj == nullptr || !(*EntryObj).IsValid())
        {
            continue;
        }

        FString GraphType;
        FString Relation;
        FString LoadStatus;
        const TSharedPtr<FJsonObject>* GraphRefObj = nullptr;
        (*EntryObj)->TryGetStringField(TEXT("graphType"), GraphType);
        (*EntryObj)->TryGetStringField(TEXT("relation"), Relation);
        (*EntryObj)->TryGetStringField(TEXT("loadStatus"), LoadStatus);
        if (!(*EntryObj)->TryGetObjectField(TEXT("graphRef"), GraphRefObj) || GraphRefObj == nullptr || !(*GraphRefObj).IsValid())
        {
            continue;
        }

        AddResolvedGraphRefEntry(Dest, DestSeenKeys, GraphType, *GraphRefObj, Relation, LoadStatus);
    }
}

void AppendBlueprintRootGraphRefs(
    UBlueprint* Blueprint,
    const FString& Relation,
    TArray<TSharedPtr<FJsonValue>>& OutRefs,
    TSet<FString>& SeenKeys)
{
    if (Blueprint == nullptr)
    {
        return;
    }

    FString AssetPath;
    if (!TryGetAssetPathFromObject(Blueprint, AssetPath))
    {
        return;
    }

    auto AddGraphRefForGraph = [&](UEdGraph* Graph)
    {
        if (Graph == nullptr)
        {
            return;
        }
        AddResolvedGraphRefEntry(
            OutRefs,
            SeenKeys,
            TEXT("blueprint"),
            MakeAssetGraphRefJson(AssetPath, Graph->GetName()),
            Relation,
            TEXT("loaded"));
    };

    for (UEdGraph* Graph : Blueprint->UbergraphPages)
    {
        AddGraphRefForGraph(Graph);
    }
    for (UEdGraph* Graph : Blueprint->FunctionGraphs)
    {
        AddGraphRefForGraph(Graph);
    }
    for (UEdGraph* Graph : Blueprint->MacroGraphs)
    {
        AddGraphRefForGraph(Graph);
    }
}

void AppendMaterialGraphRefs(
    UObject* MaterialAsset,
    const FString& Relation,
    TArray<TSharedPtr<FJsonValue>>& OutRefs,
    TSet<FString>& SeenKeys)
{
    FString AssetPath;
    if (!TryGetAssetPathFromObject(MaterialAsset, AssetPath))
    {
        return;
    }

    AddResolvedGraphRefEntry(
        OutRefs,
        SeenKeys,
        TEXT("material"),
        MakeAssetGraphRefJson(AssetPath),
        Relation,
        TEXT("loaded"));
}

void AppendPcgGraphRefs(
    UObject* PcgAsset,
    const FString& Relation,
    TArray<TSharedPtr<FJsonValue>>& OutRefs,
    TSet<FString>& SeenKeys)
{
    UObject* ResolvedPcgAsset = ResolvePcgGraphFromAsset(PcgAsset);
    if (ResolvedPcgAsset == nullptr)
    {
        ResolvedPcgAsset = PcgAsset;
    }

    FString AssetPath;
    if (!TryGetAssetPathFromObject(ResolvedPcgAsset, AssetPath))
    {
        return;
    }

    AddResolvedGraphRefEntry(
        OutRefs,
        SeenKeys,
        TEXT("pcg"),
        MakeAssetGraphRefJson(AssetPath),
        Relation,
        TEXT("loaded"));
}

void AppendPcgComponentGraphRefs(
    UPCGComponent* PcgComponent,
    const FString& Relation,
    TArray<TSharedPtr<FJsonValue>>& OutRefs,
    TSet<FString>& SeenKeys)
{
    if (PcgComponent == nullptr)
    {
        return;
    }

    auto AppendComponentGraphRef = [&](UPCGComponent* SourceComponent)
    {
        if (SourceComponent == nullptr)
        {
            return;
        }

        if (UPCGGraphInstance* GraphInstance = SourceComponent->GetGraphInstance())
        {
            AppendPcgGraphRefs(GraphInstance, Relation, OutRefs, SeenKeys);
            return;
        }

        if (UPCGGraph* Graph = SourceComponent->GetGraph())
        {
            AppendPcgGraphRefs(Graph, Relation, OutRefs, SeenKeys);
        }
    };

    if (UPCGComponent* OriginalComponent = PcgComponent->GetOriginalComponent())
    {
        if (OriginalComponent != PcgComponent)
        {
            AppendComponentGraphRef(OriginalComponent);
        }
    }

    AppendComponentGraphRef(PcgComponent);
}

void AppendSupportedGraphRefsFromAsset(
    UObject* AssetObject,
    const FString& Relation,
    TArray<TSharedPtr<FJsonValue>>& OutRefs,
    TSet<FString>& SeenKeys)
{
    if (AssetObject == nullptr)
    {
        return;
    }

    if (UBlueprint* Blueprint = Cast<UBlueprint>(AssetObject))
    {
        AppendBlueprintRootGraphRefs(Blueprint, Relation, OutRefs, SeenKeys);
        return;
    }

    if (AssetObject->IsA<UMaterial>() || AssetObject->IsA<UMaterialFunction>())
    {
        AppendMaterialGraphRefs(AssetObject, Relation, OutRefs, SeenKeys);
        return;
    }

    if (IsLikelyPcgAsset(AssetObject))
    {
        AppendPcgGraphRefs(AssetObject, Relation, OutRefs, SeenKeys);
    }
}

void AppendSoftGraphRefFromPath(
    const FString& GraphType,
    const FString& AssetPath,
    const FString& Relation,
    TArray<TSharedPtr<FJsonValue>>& OutRefs,
    TSet<FString>& SeenKeys)
{
    const FString NormalizedAssetPath = NormalizeAssetPath(AssetPath);
    if (NormalizedAssetPath.IsEmpty() || !FPackageName::IsValidLongPackageName(NormalizedAssetPath))
    {
        return;
    }

    AddResolvedGraphRefEntry(
        OutRefs,
        SeenKeys,
        GraphType,
        MakeAssetGraphRefJson(NormalizedAssetPath),
        Relation,
        TEXT("not_loaded"));
}

void AppendSupportedGraphRefsFromObjectProperties(
    UObject* SourceObject,
    const FString& Relation,
    TArray<TSharedPtr<FJsonValue>>& OutRefs,
    TSet<FString>& SeenKeys)
{
    if (SourceObject == nullptr || SourceObject->GetClass() == nullptr)
    {
        return;
    }

    for (TFieldIterator<FObjectPropertyBase> PropIt(SourceObject->GetClass()); PropIt; ++PropIt)
    {
        FObjectPropertyBase* Prop = *PropIt;
        UObject* PropValue = Prop ? Prop->GetObjectPropertyValue_InContainer(SourceObject) : nullptr;
        AppendSupportedGraphRefsFromAsset(PropValue, Relation, OutRefs, SeenKeys);
    }

    for (TFieldIterator<FSoftObjectProperty> PropIt(SourceObject->GetClass()); PropIt; ++PropIt)
    {
        FSoftObjectProperty* SoftProp = *PropIt;
        if (SoftProp == nullptr || SoftProp->PropertyClass == nullptr)
        {
            continue;
        }

        const FString PropertyClassPath = SoftProp->PropertyClass->GetPathName();
        const FSoftObjectPtr SoftPtr = SoftProp->GetPropertyValue_InContainer(SourceObject);
        const FSoftObjectPath SoftPath = SoftPtr.ToSoftObjectPath();
        if (SoftPath.IsNull())
        {
            continue;
        }

        if (PropertyClassPath.Contains(TEXT("PCGGraph")) || PropertyClassPath.Contains(TEXT("/PCG.")))
        {
            const FString AssetPath = NormalizeAssetPath(SoftPath.GetAssetPathString());
            if (AssetPath.IsEmpty() || !FPackageName::IsValidLongPackageName(AssetPath))
            {
                continue;
            }

            if (UObject* AssetObject = LoadObjectByAssetPath(AssetPath))
            {
                if (ResolvePcgGraphFromAsset(AssetObject) != nullptr)
                {
                    AppendPcgGraphRefs(AssetObject, Relation, OutRefs, SeenKeys);
                }
            }
            else
            {
                AppendSoftGraphRefFromPath(TEXT("pcg"), AssetPath, Relation, OutRefs, SeenKeys);
            }
        }
        else if (PropertyClassPath.Contains(TEXT("Material")) || PropertyClassPath.Contains(TEXT("MaterialFunction")))
        {
            AppendSoftGraphRefFromPath(TEXT("material"), SoftPath.GetAssetPathString(), Relation, OutRefs, SeenKeys);
        }
        else if (PropertyClassPath.Contains(TEXT("Blueprint")))
        {
            const FString AssetPath = NormalizeAssetPath(SoftPath.GetAssetPathString());
            if (!AssetPath.IsEmpty() && FPackageName::IsValidLongPackageName(AssetPath))
            {
                UObject* AssetObject = LoadObjectByAssetPath(AssetPath);
                if (AssetObject != nullptr)
                {
                    AppendSupportedGraphRefsFromAsset(AssetObject, Relation, OutRefs, SeenKeys);
                }
            }
        }
    }
}

void AppendPcgSubgraphRefsFromNode(
    UPCGNode* PcgNode,
    TArray<TSharedPtr<FJsonValue>>& OutRefs,
    TSet<FString>& SeenKeys)
{
    if (PcgNode == nullptr)
    {
        return;
    }

    UPCGSettings* NodeSettings = PcgNode->GetSettings();
    if (NodeSettings == nullptr || NodeSettings->GetClass() == nullptr)
    {
        return;
    }

    for (TFieldIterator<FObjectPropertyBase> PropIt(NodeSettings->GetClass()); PropIt; ++PropIt)
    {
        FObjectPropertyBase* Prop = *PropIt;
        UObject* PropValue = Prop ? Prop->GetObjectPropertyValue_InContainer(NodeSettings) : nullptr;
        if (PropValue != nullptr && IsLikelyPcgAsset(PropValue))
        {
            AppendPcgGraphRefs(PropValue, TEXT("child"), OutRefs, SeenKeys);
        }
    }

    for (TFieldIterator<FSoftObjectProperty> PropIt(NodeSettings->GetClass()); PropIt; ++PropIt)
    {
        FSoftObjectProperty* SoftProp = *PropIt;
        if (SoftProp == nullptr || SoftProp->PropertyClass == nullptr)
        {
            continue;
        }

        const FString PropertyClassPath = SoftProp->PropertyClass->GetPathName();
        if (!PropertyClassPath.Contains(TEXT("PCGGraph")) && !PropertyClassPath.Contains(TEXT("/PCG.")))
        {
            continue;
        }

        const FSoftObjectPtr SoftPtr = SoftProp->GetPropertyValue_InContainer(NodeSettings);
        const FSoftObjectPath SoftPath = SoftPtr.ToSoftObjectPath();
        if (!SoftPath.IsNull())
        {
            AppendSoftGraphRefFromPath(TEXT("pcg"), SoftPath.GetAssetPathString(), TEXT("child"), OutRefs, SeenKeys);
        }
    }
}

void AppendActorResolvedGraphRefs(
    AActor* Actor,
    TArray<TSharedPtr<FJsonValue>>& OutRefs,
    TSet<FString>& SeenKeys)
{
    if (Actor == nullptr)
    {
        return;
    }

    if (UBlueprint* GeneratedBlueprint = Cast<UBlueprint>(Actor->GetClass() ? Actor->GetClass()->ClassGeneratedBy : nullptr))
    {
        AppendBlueprintRootGraphRefs(GeneratedBlueprint, TEXT("generated_blueprint"), OutRefs, SeenKeys);
    }

    AppendSupportedGraphRefsFromObjectProperties(Actor, TEXT("attached"), OutRefs, SeenKeys);

    TInlineComponentArray<UActorComponent*> Components(Actor);
    for (UActorComponent* Component : Components)
    {
        if (UPCGComponent* PcgComponent = Cast<UPCGComponent>(Component))
        {
            AppendPcgComponentGraphRefs(PcgComponent, TEXT("component_source"), OutRefs, SeenKeys);
        }
        AppendSupportedGraphRefsFromObjectProperties(Component, TEXT("component_source"), OutRefs, SeenKeys);
    }
}

UObject* ResolveRuntimeObjectFromPath(const FString& InObjectPath)
{
    const FString ObjectPath = InObjectPath.TrimStartAndEnd();
    if (ObjectPath.IsEmpty())
    {
        return nullptr;
    }

    if (UObject* FoundObject = StaticFindObject(UObject::StaticClass(), nullptr, *ObjectPath))
    {
        return FoundObject;
    }

    if (ObjectPath.StartsWith(TEXT("/")))
    {
        if (ObjectPath.Contains(TEXT(".")))
        {
            if (UObject* LoadedObject = LoadObject<UObject>(nullptr, *ObjectPath))
            {
                return LoadedObject;
            }
        }

        return LoadObjectByAssetPath(ObjectPath);
    }

    return nullptr;
}

void AppendResolvedGraphRefsFromObject(
    UObject* Object,
    TArray<TSharedPtr<FJsonValue>>& OutRefs,
    TSet<FString>& SeenKeys)
{
    if (Object == nullptr)
    {
        return;
    }

    if (AActor* Actor = Cast<AActor>(Object))
    {
        AppendActorResolvedGraphRefs(Actor, OutRefs, SeenKeys);
        return;
    }

    if (UActorComponent* Component = Cast<UActorComponent>(Object))
    {
        if (UPCGComponent* PcgComponent = Cast<UPCGComponent>(Component))
        {
            AppendPcgComponentGraphRefs(PcgComponent, TEXT("component_source"), OutRefs, SeenKeys);
        }
        AppendSupportedGraphRefsFromObjectProperties(Component, TEXT("component_source"), OutRefs, SeenKeys);
        return;
    }

    if (UBlueprint* Blueprint = Cast<UBlueprint>(Object))
    {
        AppendBlueprintRootGraphRefs(Blueprint, TEXT("direct_asset"), OutRefs, SeenKeys);
        return;
    }

    if (UEdGraph* Graph = Cast<UEdGraph>(Object))
    {
        if (UBlueprint* Blueprint = Graph->GetTypedOuter<UBlueprint>())
        {
            FString BlueprintAssetPath;
            if (TryGetAssetPathFromObject(Blueprint, BlueprintAssetPath))
            {
                AddResolvedGraphRefEntry(
                    OutRefs,
                    SeenKeys,
                    TEXT("blueprint"),
                    MakeAssetGraphRefJson(BlueprintAssetPath, Graph->GetName()),
                    TEXT("selected_graph"),
                    TEXT("loaded"));
            }
        }
        return;
    }

    if (UEdGraphNode* GraphNode = Cast<UEdGraphNode>(Object))
    {
        if (UMaterialGraphNode* MaterialGraphNode = Cast<UMaterialGraphNode>(GraphNode))
        {
            if (MaterialGraphNode->MaterialExpression != nullptr)
            {
                AppendResolvedGraphRefsFromObject(MaterialGraphNode->MaterialExpression, OutRefs, SeenKeys);
                return;
            }
        }

        if (UPCGNode* PcgNode = ResolvePcgNodeFromEditorNode(GraphNode))
        {
            AppendResolvedGraphRefsFromObject(PcgNode, OutRefs, SeenKeys);
            return;
        }

        if (UBlueprint* Blueprint = GraphNode->GetTypedOuter<UBlueprint>())
        {
            FString BlueprintAssetPath;
            if (TryGetAssetPathFromObject(Blueprint, BlueprintAssetPath))
            {
                if (UEdGraph* Graph = GraphNode->GetGraph())
                {
                    AddResolvedGraphRefEntry(
                        OutRefs,
                        SeenKeys,
                        TEXT("blueprint"),
                        MakeAssetGraphRefJson(BlueprintAssetPath, Graph->GetName()),
                        TEXT("selected_graph"),
                        TEXT("loaded"));
                }

                FObjectPropertyBase* BoundGraphProp = FindFProperty<FObjectPropertyBase>(GraphNode->GetClass(), TEXT("BoundGraph"));
                if (BoundGraphProp != nullptr)
                {
                    UEdGraph* BoundGraph = Cast<UEdGraph>(BoundGraphProp->GetObjectPropertyValue_InContainer(GraphNode));
                    if (BoundGraph != nullptr)
                    {
                        AddResolvedGraphRefEntry(
                            OutRefs,
                            SeenKeys,
                            TEXT("blueprint"),
                            MakeInlineGraphRefJson(
                                BlueprintAssetPath,
                                GraphNode->NodeGuid.ToString(EGuidFormats::DigitsWithHyphensLower)),
                            TEXT("child"),
                            TEXT("loaded"));
                    }
                }
            }
        }
        return;
    }

    if (UMaterialExpression* Expression = Cast<UMaterialExpression>(Object))
    {
        UObject* MaterialOwner = Expression->GetTypedOuter<UMaterial>();
        FString MaterialOwnerAssetPath;
        if (MaterialOwner != nullptr
            && (!TryGetAssetPathFromObject(MaterialOwner, MaterialOwnerAssetPath)
                || IsTransientAssetPath(MaterialOwnerAssetPath)))
        {
            MaterialOwner = nullptr;
        }
        if (MaterialOwner == nullptr)
        {
            MaterialOwner = Expression->GetTypedOuter<UMaterialFunction>();
            if (MaterialOwner != nullptr
                && (!TryGetAssetPathFromObject(MaterialOwner, MaterialOwnerAssetPath)
                    || IsTransientAssetPath(MaterialOwnerAssetPath)))
            {
                MaterialOwner = nullptr;
            }
        }
        if (MaterialOwner == nullptr)
        {
            MaterialOwner = FindEditedMaterialAsset();
        }
        AppendMaterialGraphRefs(MaterialOwner, TEXT("selected_graph"), OutRefs, SeenKeys);

        if (UMaterialExpressionMaterialFunctionCall* FuncCall = Cast<UMaterialExpressionMaterialFunctionCall>(Expression))
        {
            AppendMaterialGraphRefs(FuncCall->MaterialFunction, TEXT("child"), OutRefs, SeenKeys);
        }
        return;
    }

    if (UPCGNode* PcgNode = Cast<UPCGNode>(Object))
    {
        UObject* PcgAsset = nullptr;
        if (UPCGGraph* Graph = PcgNode->GetTypedOuter<UPCGGraph>())
        {
            PcgAsset = ResolvePcgGraphFromAsset(Graph);
            FString PcgAssetPath;
            if (PcgAsset == nullptr
                || !TryGetAssetPathFromObject(PcgAsset, PcgAssetPath)
                || IsTransientAssetPath(PcgAssetPath))
            {
                PcgAsset = nullptr;
            }
        }
        if (PcgAsset == nullptr)
        {
            PcgAsset = FindEditedPcgAsset();
        }
        if (PcgAsset != nullptr)
        {
            AppendPcgGraphRefs(PcgAsset, TEXT("selected_graph"), OutRefs, SeenKeys);
        }
        AppendPcgSubgraphRefsFromNode(PcgNode, OutRefs, SeenKeys);
        return;
    }

    AppendSupportedGraphRefsFromAsset(Object, TEXT("direct_asset"), OutRefs, SeenKeys);
    AppendSupportedGraphRefsFromObjectProperties(Object, TEXT("attached"), OutRefs, SeenKeys);
}

void FilterResolvedGraphRefsByType(
    const TArray<TSharedPtr<FJsonValue>>& InRefs,
    const FString& GraphTypeFilter,
    TArray<TSharedPtr<FJsonValue>>& OutRefs)
{
    OutRefs.Reset();
    const FString NormalizedFilter = GraphTypeFilter.TrimStartAndEnd().ToLower();
    if (NormalizedFilter.IsEmpty())
    {
        OutRefs = InRefs;
        return;
    }

    for (const TSharedPtr<FJsonValue>& Value : InRefs)
    {
        const TSharedPtr<FJsonObject>* EntryObj = nullptr;
        if (!Value.IsValid() || !Value->TryGetObject(EntryObj) || EntryObj == nullptr || !(*EntryObj).IsValid())
        {
            continue;
        }

        FString EntryGraphType;
        (*EntryObj)->TryGetStringField(TEXT("graphType"), EntryGraphType);
        if (EntryGraphType.Equals(NormalizedFilter))
        {
            OutRefs.Add(Value);
        }
    }
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

    UMaterialGraph* MaterialGraph = Material->MaterialGraph;
    if (MaterialGraph != nullptr)
    {
        for (UEdGraphNode* Node : MaterialGraph->Nodes)
        {
            UMaterialGraphNode* MaterialGraphNode = Cast<UMaterialGraphNode>(Node);
            UMaterialExpression* Expression = MaterialGraphNode ? MaterialGraphNode->MaterialExpression : nullptr;
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

TSharedPtr<SWindow> ResolveActiveTopLevelWindow()
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

FString ResolveScreenshotOutputPath(const FString& RequestedPath)
{
    FString OutputPath = RequestedPath.TrimStartAndEnd();
    if (OutputPath.IsEmpty())
    {
        const FString Timestamp = FDateTime::UtcNow().ToString(TEXT("%Y%m%d-%H%M%S"));
        OutputPath = FPaths::Combine(
            FPaths::ProjectDir(),
            TEXT("Loomle"),
            TEXT("runtime"),
            TEXT("captures"),
            FString::Printf(TEXT("capture-%s.png"), *Timestamp));
    }
    else if (FPaths::IsRelative(OutputPath))
    {
        OutputPath = FPaths::Combine(FPaths::ProjectDir(), OutputPath);
    }

    if (!OutputPath.EndsWith(TEXT(".png"), ESearchCase::IgnoreCase))
    {
        OutputPath += TEXT(".png");
    }

    return FPaths::ConvertRelativePathToFull(OutputPath);
}

TSharedPtr<FJsonObject> BuildActiveWindowJson()
{
    TSharedPtr<FJsonObject> Window = MakeShared<FJsonObject>();
    Window->SetBoolField(TEXT("isValid"), false);
    Window->SetStringField(TEXT("title"), TEXT(""));

    TSharedPtr<SWindow> ActiveWindow = ResolveActiveTopLevelWindow();

    if (!ActiveWindow.IsValid())
    {
        return Window;
    }

    Window->SetBoolField(TEXT("isValid"), true);
    Window->SetStringField(TEXT("title"), ActiveWindow->GetTitle().ToString());
    return Window;
}

const FName MaterialEditorPreviewTabId(TEXT("MaterialEditor_Preview"));
const FName MaterialEditorPropertiesTabId(TEXT("MaterialEditor_MaterialProperties"));
const FName MaterialEditorPaletteTabId(TEXT("MaterialEditor_Palette"));
const FName MaterialEditorFindTabId(TEXT("MaterialEditor_Find"));
const FName MaterialEditorGraphTabId(TEXT("Document"));

FString NormalizeEditorPanel(FString Panel)
{
    Panel = Panel.TrimStartAndEnd().ToLower();
    return Panel;
}

bool IsMaterialLikeAsset(const UObject* Asset)
{
    return Asset != nullptr
        && (Asset->IsA<UMaterial>()
            || Asset->IsA<UMaterialFunctionInterface>());
}

FString GetActiveWindowTitle()
{
    TSharedPtr<SWindow> ActiveWindow = ResolveActiveTopLevelWindow();

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

    TArray<TSharedPtr<FJsonValue>> ResolvedGraphRefs;
    TSet<FString> SeenGraphRefs;
    AppendBlueprintRootGraphRefs(Blueprint, TEXT("context"), ResolvedGraphRefs, SeenGraphRefs);
    SetResolvedGraphRefsFieldIfAny(OutContext, ResolvedGraphRefs);
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
    TArray<TSharedPtr<FJsonValue>> ResolvedGraphRefs;
    TSet<FString> SelectionSeenGraphRefs;
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

        TArray<TSharedPtr<FJsonValue>> ItemResolvedGraphRefs;
        TSet<FString> ItemSeenGraphRefs;
        FString BlueprintAssetPath;
        if (TryGetAssetPathFromObject(Blueprint, BlueprintAssetPath))
        {
            if (UEdGraph* Graph = Node->GetGraph())
            {
                AddResolvedGraphRefEntry(
                    ItemResolvedGraphRefs,
                    ItemSeenGraphRefs,
                    TEXT("blueprint"),
                    MakeAssetGraphRefJson(BlueprintAssetPath, Graph->GetName()),
                    TEXT("selected_graph"),
                    TEXT("loaded"));
            }

            FObjectPropertyBase* BoundGraphProp = FindFProperty<FObjectPropertyBase>(Node->GetClass(), TEXT("BoundGraph"));
            if (BoundGraphProp != nullptr)
            {
                UEdGraph* BoundGraph = Cast<UEdGraph>(BoundGraphProp->GetObjectPropertyValue_InContainer(Node));
                if (BoundGraph != nullptr)
                {
                    AddResolvedGraphRefEntry(
                        ItemResolvedGraphRefs,
                        ItemSeenGraphRefs,
                        TEXT("blueprint"),
                        MakeInlineGraphRefJson(
                            BlueprintAssetPath,
                            Node->NodeGuid.ToString(EGuidFormats::DigitsWithHyphensLower)),
                        TEXT("child"),
                        TEXT("loaded"));
                }
            }
        }

        SetResolvedGraphRefsFieldIfAny(Item, ItemResolvedGraphRefs);
        CopyResolvedGraphRefEntries(ItemResolvedGraphRefs, ResolvedGraphRefs, SelectionSeenGraphRefs);
        Items.Add(MakeShared<FJsonValueObject>(Item));
    }

    OutSelection->SetArrayField(TEXT("items"), Items);
    OutSelection->SetNumberField(TEXT("count"), Items.Num());
    SetResolvedGraphRefsFieldIfAny(OutSelection, ResolvedGraphRefs);
    return true;
}

UObject* FindEditedMaterialAsset()
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
    UObject* FallbackMaterialAsset = nullptr;

    const TArray<UObject*> EditedAssets = AssetEditorSubsystem->GetAllEditedAssets();
    for (UObject* Asset : EditedAssets)
    {
        if (!Asset || (!Asset->IsA<UMaterial>() && !Asset->IsA<UMaterialFunction>()))
        {
            continue;
        }

        if (!FallbackMaterialAsset)
        {
            FallbackMaterialAsset = Asset;
        }

        if (!ActiveWindowTitle.IsEmpty()
            && ActiveWindowTitle.Contains(Asset->GetName(), ESearchCase::IgnoreCase))
        {
            return Asset;
        }
    }

    return ActiveWindowTitle.IsEmpty() ? FallbackMaterialAsset : nullptr;
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

bool CollectSelectedMaterialExpressions(TArray<UMaterialExpression*>& OutExpressions, UObject*& OutMaterialAsset)
{
    OutExpressions.Reset();
    OutMaterialAsset = nullptr;

    UObject* EditedMaterialAsset = FindEditedMaterialAsset();
    if (!EditedMaterialAsset)
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

    OutMaterialAsset = EditedMaterialAsset;
    return OutExpressions.Num() > 0 && OutMaterialAsset != nullptr;
}

bool BuildMaterialContextSnapshot(TSharedPtr<FJsonObject>& OutContext)
{
    OutContext.Reset();
    UObject* MaterialAsset = FindEditedMaterialAsset();
    if (!MaterialAsset)
    {
        return false;
    }

    OutContext = MakeShared<FJsonObject>();
    OutContext->SetBoolField(TEXT("isError"), false);
    OutContext->SetStringField(TEXT("editorType"), TEXT("material"));
    OutContext->SetStringField(TEXT("provider"), TEXT("material"));
    OutContext->SetStringField(TEXT("assetName"), MaterialAsset->GetName());
    OutContext->SetStringField(TEXT("assetPath"), MaterialAsset->GetPathName());
    OutContext->SetStringField(
        TEXT("assetClass"),
        MaterialAsset->GetClass() ? MaterialAsset->GetClass()->GetPathName() : TEXT(""));
    OutContext->SetStringField(TEXT("status"), TEXT("active"));

    TArray<TSharedPtr<FJsonValue>> ResolvedGraphRefs;
    TSet<FString> SeenGraphRefs;
    AppendMaterialGraphRefs(MaterialAsset, TEXT("context"), ResolvedGraphRefs, SeenGraphRefs);
    SetResolvedGraphRefsFieldIfAny(OutContext, ResolvedGraphRefs);
    return true;
}

bool BuildMaterialSelectionSnapshot(TSharedPtr<FJsonObject>& OutSelection)
{
    OutSelection.Reset();
    TArray<UMaterialExpression*> SelectedExpressions;
    UObject* MaterialAsset = nullptr;
    if (!CollectSelectedMaterialExpressions(SelectedExpressions, MaterialAsset))
    {
        return false;
    }

    OutSelection = MakeShared<FJsonObject>();
    OutSelection->SetBoolField(TEXT("isError"), false);
    OutSelection->SetStringField(TEXT("editorType"), TEXT("material"));
    OutSelection->SetStringField(TEXT("provider"), TEXT("material"));
    OutSelection->SetStringField(TEXT("assetPath"), MaterialAsset->GetPathName());
    OutSelection->SetStringField(TEXT("selectionKind"), TEXT("graph_node"));

    TArray<TSharedPtr<FJsonValue>> Items;
    TArray<TSharedPtr<FJsonValue>> ResolvedGraphRefs;
    TSet<FString> SelectionSeenGraphRefs;
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

        TArray<TSharedPtr<FJsonValue>> ItemResolvedGraphRefs;
        TSet<FString> ItemSeenGraphRefs;
        AppendMaterialGraphRefs(MaterialAsset, TEXT("selected_graph"), ItemResolvedGraphRefs, ItemSeenGraphRefs);
        if (UMaterialExpressionMaterialFunctionCall* FuncCall = Cast<UMaterialExpressionMaterialFunctionCall>(Expression))
        {
            AppendMaterialGraphRefs(FuncCall->MaterialFunction, TEXT("child"), ItemResolvedGraphRefs, ItemSeenGraphRefs);
        }
        SetResolvedGraphRefsFieldIfAny(Item, ItemResolvedGraphRefs);
        CopyResolvedGraphRefEntries(ItemResolvedGraphRefs, ResolvedGraphRefs, SelectionSeenGraphRefs);
        Items.Add(MakeShared<FJsonValueObject>(Item));
    }

    OutSelection->SetArrayField(TEXT("items"), Items);
    OutSelection->SetNumberField(TEXT("count"), Items.Num());
    SetResolvedGraphRefsFieldIfAny(OutSelection, ResolvedGraphRefs);
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

    TArray<TSharedPtr<FJsonValue>> ResolvedGraphRefs;
    TSet<FString> SeenGraphRefs;
    AppendPcgGraphRefs(PcgAsset, TEXT("context"), ResolvedGraphRefs, SeenGraphRefs);
    AppendSupportedGraphRefsFromObjectProperties(PcgAsset, TEXT("source"), ResolvedGraphRefs, SeenGraphRefs);
    SetResolvedGraphRefsFieldIfAny(OutContext, ResolvedGraphRefs);
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
    TArray<TSharedPtr<FJsonValue>> ResolvedGraphRefs;
    TSet<FString> SelectionSeenGraphRefs;
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

        TArray<TSharedPtr<FJsonValue>> ItemResolvedGraphRefs;
        TSet<FString> ItemSeenGraphRefs;
        AppendPcgGraphRefs(PcgAsset, TEXT("selected_graph"), ItemResolvedGraphRefs, ItemSeenGraphRefs);
        AppendPcgSubgraphRefsFromNode(PcgNode, ItemResolvedGraphRefs, ItemSeenGraphRefs);
        SetResolvedGraphRefsFieldIfAny(Item, ItemResolvedGraphRefs);
        CopyResolvedGraphRefEntries(ItemResolvedGraphRefs, ResolvedGraphRefs, SelectionSeenGraphRefs);
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
    SetResolvedGraphRefsFieldIfAny(OutSelection, ResolvedGraphRefs);
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
