// Graph runtime handlers for Loomle Bridge.
namespace
{
// Graph reference helpers shared across query/list/mutate paths.
TSharedPtr<FJsonObject> MakeGraphAssetRef(const FString& RefAssetPath, const FString& RefGraphName)
{
    TSharedPtr<FJsonObject> Ref = MakeShared<FJsonObject>();
    Ref->SetStringField(TEXT("kind"), TEXT("asset"));
    Ref->SetStringField(TEXT("assetPath"), RefAssetPath);
    if (!RefGraphName.IsEmpty())
    {
        Ref->SetStringField(TEXT("graphName"), RefGraphName);
    }
    return Ref;
}

TSharedPtr<FJsonObject> MakeGraphInlineRef(const FString& RefNodeGuid, const FString& RefAssetPath)
{
    TSharedPtr<FJsonObject> Ref = MakeShared<FJsonObject>();
    Ref->SetStringField(TEXT("kind"), TEXT("inline"));
    Ref->SetStringField(TEXT("nodeGuid"), RefNodeGuid);
    Ref->SetStringField(TEXT("assetPath"), RefAssetPath);
    return Ref;
}

bool TryGetRequiredObjectField(
    const TSharedPtr<FJsonObject>& Source,
    const TCHAR* FieldName,
    const TSharedPtr<FJsonObject>*& OutObject)
{
    OutObject = nullptr;
    return Source.IsValid()
        && Source->TryGetObjectField(FieldName, OutObject)
        && OutObject != nullptr
        && (*OutObject).IsValid();
}

bool TryResolveNodeRef(
    const TSharedPtr<FJsonObject>& Source,
    const TMap<FString, FString>& NodeRefs,
    FString& OutNodeId)
{
    FString NodeRef;
    if (Source.IsValid()
        && Source->TryGetStringField(TEXT("nodeRef"), NodeRef)
        && !NodeRef.IsEmpty()
        && NodeRefs.Contains(NodeRef))
    {
        OutNodeId = NodeRefs[NodeRef];
        return !OutNodeId.IsEmpty();
    }
    return false;
}

bool ResolveNodeTokenWithRefs(
    const TSharedPtr<FJsonObject>& Source,
    const TMap<FString, FString>& NodeRefs,
    FString& OutNodeId,
    bool bAllowNameAliases)
{
    OutNodeId.Empty();
    if (!Source.IsValid())
    {
        return false;
    }
    if (Source->TryGetStringField(TEXT("nodeId"), OutNodeId) && !OutNodeId.IsEmpty())
    {
        return true;
    }
    if (TryResolveNodeRef(Source, NodeRefs, OutNodeId))
    {
        return true;
    }
    if (Source->TryGetStringField(TEXT("nodePath"), OutNodeId) && !OutNodeId.IsEmpty())
    {
        return true;
    }
    if (Source->TryGetStringField(TEXT("path"), OutNodeId) && !OutNodeId.IsEmpty())
    {
        return true;
    }
    if (bAllowNameAliases)
    {
        if (Source->TryGetStringField(TEXT("nodeName"), OutNodeId) && !OutNodeId.IsEmpty())
        {
            return true;
        }
        if (Source->TryGetStringField(TEXT("name"), OutNodeId) && !OutNodeId.IsEmpty())
        {
            return true;
        }
    }
    return false;
}

bool ResolveNodeTokenFromArgsWithRefs(
    const TSharedPtr<FJsonObject>& ArgsObj,
    const TMap<FString, FString>& NodeRefs,
    FString& OutNodeId,
    bool bAllowNameAliases)
{
    OutNodeId.Empty();
    if (!ArgsObj.IsValid())
    {
        return false;
    }

    const TSharedPtr<FJsonObject>* TargetObj = nullptr;
    if (ArgsObj->TryGetObjectField(TEXT("target"), TargetObj)
        && TargetObj != nullptr
        && (*TargetObj).IsValid()
        && ResolveNodeTokenWithRefs(*TargetObj, NodeRefs, OutNodeId, bAllowNameAliases))
    {
        return true;
    }

    return ResolveNodeTokenWithRefs(ArgsObj, NodeRefs, OutNodeId, bAllowNameAliases);
}

TSharedPtr<FJsonObject> MakeNodeRefsObject(const TMap<FString, FString>& NodeRefs)
{
    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    for (const TPair<FString, FString>& Pair : NodeRefs)
    {
        Result->SetStringField(Pair.Key, Pair.Value);
    }
    return Result;
}

// graph.mutate execution DTOs and result builders.
struct FMutateResolvedPinEndpoint
{
    FString NodeId;
    FString PinName;

    bool IsValid() const
    {
        return !NodeId.IsEmpty() && !PinName.IsEmpty();
    }
};

struct FMutateResolvedNodeTarget
{
    FString NodeId;
    FString PinName;

    bool IsValid() const
    {
        return !NodeId.IsEmpty();
    }
};

struct FBlueprintMutateOpExecution
{
    bool bOk = true;
    bool bChanged = false;
    bool bSkipped = false;
    FString Error;
    FString SkipReason;
    FString NodeId;
    TArray<FString> NodesTouchedForLayout;
    FString GraphEventName;
    TSharedPtr<FJsonObject> GraphEventData = MakeShared<FJsonObject>();
    TSharedPtr<FJsonObject> ErrorDetailsForOp;
    TSharedPtr<FJsonObject> ScriptResultForOp;
};

struct FAssetMutateOpExecution
{
    bool bOk = true;
    bool bChanged = false;
    FString Error;
    FString NodeId;
    TArray<FString> NodesTouchedForLayout;
    FString GraphEventName;
    TSharedPtr<FJsonObject> GraphEventData = MakeShared<FJsonObject>();
    TSharedPtr<FJsonObject> ErrorDetailsForOp;
};

TArray<TSharedPtr<FJsonValue>> MakeJsonStringArray(const TArray<FString>& Values)
{
    TArray<TSharedPtr<FJsonValue>> Result;
    for (const FString& Value : Values)
    {
        if (!Value.IsEmpty())
        {
            Result.Add(MakeShared<FJsonValueString>(Value));
        }
    }
    return Result;
}

TSharedPtr<FJsonObject> BuildMutateOpResultObject(
    int32 Index,
    const FString& Op,
    bool bOk,
    bool bChanged,
    const FString& NodeId,
    const FString& Error,
    const FString& ErrorCode,
    bool bIncludeSkippedField,
    bool bSkipped,
    const FString& SkipReason,
    bool bAlwaysEmitErrorField,
    const TSharedPtr<FJsonObject>& ErrorDetails,
    const TSharedPtr<FJsonObject>& ScriptResult,
    const TArray<FString>* MovedNodeIds = nullptr)
{
    TSharedPtr<FJsonObject> OpResult = MakeShared<FJsonObject>();
    OpResult->SetNumberField(TEXT("index"), Index);
    OpResult->SetStringField(TEXT("op"), Op);
    OpResult->SetBoolField(TEXT("ok"), bOk);
    if (bIncludeSkippedField)
    {
        OpResult->SetBoolField(TEXT("skipped"), bSkipped);
    }
    if (!NodeId.IsEmpty())
    {
        OpResult->SetStringField(TEXT("nodeId"), NodeId);
    }
    OpResult->SetBoolField(TEXT("changed"), bChanged);
    if (bAlwaysEmitErrorField || !Error.IsEmpty())
    {
        OpResult->SetStringField(TEXT("error"), bOk ? TEXT("") : Error);
    }
    OpResult->SetStringField(TEXT("errorCode"), ErrorCode);
    OpResult->SetStringField(TEXT("errorMessage"), bOk ? TEXT("") : Error);
    if (bIncludeSkippedField && bSkipped && !SkipReason.IsEmpty())
    {
        OpResult->SetStringField(TEXT("skipReason"), SkipReason);
    }
    if (MovedNodeIds != nullptr)
    {
        OpResult->SetArrayField(TEXT("movedNodeIds"), MakeJsonStringArray(*MovedNodeIds));
    }
    if (ErrorDetails.IsValid())
    {
        OpResult->SetObjectField(TEXT("details"), ErrorDetails);
    }
    if (ScriptResult.IsValid())
    {
        OpResult->SetObjectField(TEXT("scriptResult"), ScriptResult);
    }
    return OpResult;
}

TSharedPtr<FJsonObject> BuildMutateDiagnosticObject(
    const FString& ErrorCode,
    const FString& Op,
    const FString& Error,
    const FString& NodeId,
    const FString& AssetPath,
    const FString& GraphName,
    const TSharedPtr<FJsonObject>& ErrorDetails)
{
    TSharedPtr<FJsonObject> Diagnostic = MakeShared<FJsonObject>();
    Diagnostic->SetStringField(TEXT("code"), ErrorCode.IsEmpty() ? TEXT("INTERNAL_ERROR") : ErrorCode);
    Diagnostic->SetStringField(TEXT("severity"), TEXT("error"));
    Diagnostic->SetStringField(TEXT("message"), Error.IsEmpty() ? FString::Printf(TEXT("%s failed."), *Op) : Error);
    Diagnostic->SetStringField(TEXT("sourceKind"), TEXT("mutate"));
    Diagnostic->SetStringField(TEXT("op"), Op);
    if (!NodeId.IsEmpty())
    {
        Diagnostic->SetStringField(TEXT("nodeId"), NodeId);
    }
    if (!AssetPath.IsEmpty())
    {
        Diagnostic->SetStringField(TEXT("assetPath"), AssetPath);
    }
    if (!GraphName.IsEmpty())
    {
        Diagnostic->SetStringField(TEXT("graphName"), GraphName);
    }
    if (ErrorDetails.IsValid())
    {
        Diagnostic->SetObjectField(TEXT("details"), ErrorDetails);
    }
    return Diagnostic;
}

// Shared JSON coercion and endpoint contracts for graph.mutate.
bool TryResolvePinNameField(const TSharedPtr<FJsonObject>& Obj, FString& OutPinName)
{
    OutPinName.Empty();
    if (!Obj.IsValid())
    {
        return false;
    }

    if (Obj->TryGetStringField(TEXT("pin"), OutPinName) && !OutPinName.IsEmpty())
    {
        return true;
    }
    return Obj->TryGetStringField(TEXT("pinName"), OutPinName) && !OutPinName.IsEmpty();
}

bool ResolveNodeTokenArrayWithRefs(
    const TSharedPtr<FJsonObject>& ArgsObj,
    const TMap<FString, FString>& NodeRefs,
    TArray<FString>& OutNodeIds,
    bool bAllowNameAliases)
{
    OutNodeIds.Reset();
    if (!ArgsObj.IsValid())
    {
        return false;
    }

    const TArray<TSharedPtr<FJsonValue>>* NodeIds = nullptr;
    if (ArgsObj->TryGetArrayField(TEXT("nodeIds"), NodeIds) && NodeIds)
    {
        for (const TSharedPtr<FJsonValue>& NodeIdValue : *NodeIds)
        {
            FString NodeId;
            if (NodeIdValue.IsValid() && NodeIdValue->TryGetString(NodeId) && !NodeId.IsEmpty())
            {
                OutNodeIds.Add(NodeId);
            }
        }
    }

    const TArray<TSharedPtr<FJsonValue>>* NodesArray = nullptr;
    if (ArgsObj->TryGetArrayField(TEXT("nodes"), NodesArray) && NodesArray)
    {
        for (const TSharedPtr<FJsonValue>& NodeValue : *NodesArray)
        {
            FString NodeId;
            if (!NodeValue.IsValid())
            {
                continue;
            }
            if (NodeValue->TryGetString(NodeId) && !NodeId.IsEmpty())
            {
                OutNodeIds.Add(NodeId);
                continue;
            }

            const TSharedPtr<FJsonObject>* NodeObj = nullptr;
            if (NodeValue->TryGetObject(NodeObj) && NodeObj && (*NodeObj).IsValid()
                && ResolveNodeTokenWithRefs(*NodeObj, NodeRefs, NodeId, bAllowNameAliases) && !NodeId.IsEmpty())
            {
                OutNodeIds.Add(NodeId);
            }
        }
    }

    return OutNodeIds.Num() > 0;
}

void GetPointFromJsonObject(const TSharedPtr<FJsonObject>& Obj, int32& OutX, int32& OutY)
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
}

bool HasExplicitPositionField(const TSharedPtr<FJsonObject>& Obj)
{
    return Obj.IsValid() && Obj->HasTypedField<EJson::Object>(TEXT("position"));
}

void GetDeltaFromJsonObject(const TSharedPtr<FJsonObject>& Obj, int32& OutDx, int32& OutDy)
{
    OutDx = 0;
    OutDy = 0;
    if (!Obj.IsValid())
    {
        return;
    }

    double Dx = 0.0;
    double Dy = 0.0;
    Obj->TryGetNumberField(TEXT("dx"), Dx);
    Obj->TryGetNumberField(TEXT("dy"), Dy);

    if (const TSharedPtr<FJsonObject>* Delta = nullptr;
        Obj->TryGetObjectField(TEXT("delta"), Delta) && Delta && (*Delta).IsValid())
    {
        (*Delta)->TryGetNumberField(TEXT("x"), Dx);
        (*Delta)->TryGetNumberField(TEXT("y"), Dy);
    }

    OutDx = static_cast<int32>(Dx);
    OutDy = static_cast<int32>(Dy);
}

FString SerializeJsonObjectForMutate(const TSharedPtr<FJsonObject>& Obj)
{
    if (!Obj.IsValid())
    {
        return TEXT("{}");
    }

    FString Out;
    TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Out);
    FJsonSerializer::Serialize(Obj.ToSharedRef(), Writer);
    return Out;
}

FString EscapePythonSingleQuotedString(const FString& In)
{
    FString Out = In;
    Out.ReplaceInline(TEXT("\\"), TEXT("\\\\"));
    Out.ReplaceInline(TEXT("'"), TEXT("\\'"));
    Out.ReplaceInline(TEXT("\r"), TEXT("\\r"));
    Out.ReplaceInline(TEXT("\n"), TEXT("\\n"));
    return Out;
}

UEdGraphNode* ResolveBlueprintGraphNodeByToken(UEdGraph* Graph, const FString& NodeToken)
{
    if (Graph == nullptr || NodeToken.IsEmpty())
    {
        return nullptr;
    }

    if (UEdGraphNode* Node = FindNodeByGuid(Graph, NodeToken))
    {
        return Node;
    }

    for (UEdGraphNode* Node : Graph->Nodes)
    {
        if (Node == nullptr)
        {
            continue;
        }

        if (Node->GetPathName().Equals(NodeToken, ESearchCase::IgnoreCase)
            || Node->GetName().Equals(NodeToken, ESearchCase::IgnoreCase))
        {
            return Node;
        }
    }

    return nullptr;
}

bool TryResolveMutatePinEndpoint(
    const TSharedPtr<FJsonObject>& Obj,
    const TMap<FString, FString>& NodeRefs,
    FMutateResolvedPinEndpoint& OutEndpoint,
    bool bAllowNameAliases = true)
{
    OutEndpoint = FMutateResolvedPinEndpoint();
    return Obj.IsValid()
        && ResolveNodeTokenWithRefs(Obj, NodeRefs, OutEndpoint.NodeId, bAllowNameAliases)
        && TryResolvePinNameField(Obj, OutEndpoint.PinName);
}

bool TryResolveMutateNodeTarget(
    const TSharedPtr<FJsonObject>& Obj,
    const TMap<FString, FString>& NodeRefs,
    FMutateResolvedNodeTarget& OutTarget,
    bool bRequirePin,
    bool bAllowNameAliases = true)
{
    OutTarget = FMutateResolvedNodeTarget();
    if (!Obj.IsValid() || !ResolveNodeTokenWithRefs(Obj, NodeRefs, OutTarget.NodeId, bAllowNameAliases))
    {
        return false;
    }

    if (bRequirePin)
    {
        return TryResolvePinNameField(Obj, OutTarget.PinName);
    }

    TryResolvePinNameField(Obj, OutTarget.PinName);
    return true;
}

bool TryResolveMutatePinEndpointField(
    const TSharedPtr<FJsonObject>& ArgsObj,
    const TCHAR* FieldName,
    const TMap<FString, FString>& NodeRefs,
    FMutateResolvedPinEndpoint& OutEndpoint,
    bool bAllowNameAliases = true)
{
    const TSharedPtr<FJsonObject>* EndpointObj = nullptr;
    return TryGetRequiredObjectField(ArgsObj, FieldName, EndpointObj)
        && TryResolveMutatePinEndpoint(*EndpointObj, NodeRefs, OutEndpoint, bAllowNameAliases);
}

bool TryResolveMutateNodeTargetField(
    const TSharedPtr<FJsonObject>& ArgsObj,
    const TCHAR* FieldName,
    const TMap<FString, FString>& NodeRefs,
    FMutateResolvedNodeTarget& OutTarget,
    bool bRequirePin,
    bool bAllowNameAliases = true)
{
    const TSharedPtr<FJsonObject>* TargetObj = nullptr;
    return TryGetRequiredObjectField(ArgsObj, FieldName, TargetObj)
        && TryResolveMutateNodeTarget(*TargetObj, NodeRefs, OutTarget, bRequirePin, bAllowNameAliases);
}

bool TryResolveOptionalMutateNodeTargetField(
    const TSharedPtr<FJsonObject>& ArgsObj,
    const TCHAR* FieldName,
    const TMap<FString, FString>& NodeRefs,
    FMutateResolvedNodeTarget& OutTarget,
    bool& bHasField,
    bool bRequirePin,
    bool bAllowNameAliases = true)
{
    bHasField = false;
    OutTarget = FMutateResolvedNodeTarget();

    const TSharedPtr<FJsonObject>* TargetObj = nullptr;
    if (!TryGetRequiredObjectField(ArgsObj, FieldName, TargetObj))
    {
        return true;
    }

    bHasField = true;
    return TryResolveMutateNodeTarget(*TargetObj, NodeRefs, OutTarget, bRequirePin, bAllowNameAliases);
}

bool TryResolveValueStringField(const TSharedPtr<FJsonObject>& Obj, FString& OutValue)
{
    OutValue.Empty();
    if (!Obj.IsValid())
    {
        return false;
    }

    TSharedPtr<FJsonValue> ValueField = Obj->TryGetField(TEXT("value"));
    if (!ValueField.IsValid())
    {
        return false;
    }

    switch (ValueField->Type)
    {
    case EJson::String:
        OutValue = ValueField->AsString();
        return true;
    case EJson::Boolean:
        OutValue = ValueField->AsBool() ? TEXT("true") : TEXT("false");
        return true;
    case EJson::Number:
    {
        const double NumericValue = ValueField->AsNumber();
        const double RoundedValue = FMath::RoundToDouble(NumericValue);
        if (FMath::IsNearlyEqual(NumericValue, RoundedValue, KINDA_SMALL_NUMBER))
        {
            OutValue = LexToString(static_cast<int64>(RoundedValue));
        }
        else
        {
            OutValue = LexToString(NumericValue);
        }
        return true;
    }
    default:
        return false;
    }
}

// Shared graph.mutate layout and post-change helpers.
template <typename TExecution, typename TResolveRequestedNodeIds, typename TResolvePendingNodes, typename TApplyLayout>
void ExecuteMutateLayoutGraphOp(
    const FString& Op,
    const FString& GraphName,
    const TSharedPtr<FJsonObject>& ArgsObj,
    TExecution& Execution,
    TResolveRequestedNodeIds&& ResolveRequestedNodeIds,
    TResolvePendingNodes&& ResolvePendingNodes,
    TApplyLayout&& ApplyLayout)
{
    FString LayoutScope = TEXT("touched");
    ArgsObj->TryGetStringField(TEXT("scope"), LayoutScope);
    LayoutScope = LayoutScope.ToLower();

    TArray<FString> LayoutNodeIds;
    ResolveRequestedNodeIds(ArgsObj, LayoutNodeIds);
    if (LayoutScope.Equals(TEXT("touched")))
    {
        TArray<FString> PendingNodeIds;
        ResolvePendingNodes(GraphName, PendingNodeIds, false);
        for (const FString& PendingNodeId : PendingNodeIds)
        {
            LayoutNodeIds.AddUnique(PendingNodeId);
        }
    }

    TArray<FString> MovedNodeIds;
    Execution.bOk = ApplyLayout(GraphName, LayoutScope, LayoutNodeIds, MovedNodeIds, Execution.Error);
    if (!Execution.bOk)
    {
        if (Execution.Error.IsEmpty())
        {
            Execution.Error = TEXT("layoutGraph failed.");
        }
        return;
    }

    Execution.bChanged = MovedNodeIds.Num() > 0;
    Execution.NodesTouchedForLayout.Append(MovedNodeIds);
    Execution.GraphEventName = TEXT("graph.layout_applied");
    Execution.GraphEventData->SetStringField(TEXT("scope"), LayoutScope);
    Execution.GraphEventData->SetArrayField(TEXT("nodeIds"), MakeJsonStringArray(MovedNodeIds));
    Execution.GraphEventData->SetStringField(TEXT("op"), Op);

    if (LayoutScope.Equals(TEXT("touched")))
    {
        TArray<FString> IgnoredNodeIds;
        ResolvePendingNodes(GraphName, IgnoredNodeIds, true);
    }
}

void ApplyAssetMutateSideEffects(
    UObject* MutableAsset,
    UMaterial* MaterialAsset,
    UPCGGraph* PcgGraph,
    bool bChanged)
{
    if (!bChanged)
    {
        return;
    }

    if (MutableAsset != nullptr)
    {
        MutableAsset->MarkPackageDirty();
    }

    if (MaterialAsset != nullptr)
    {
        MaterialAsset->PostEditChange();
        RefreshMaterialEditorVisuals(MaterialAsset);
    }

    if (PcgGraph != nullptr)
    {
        RefreshPcgEditorVisuals(PcgGraph);
    }
}

template <typename TRecordPendingNodes>
void RecordDeferredLayoutTargetsFromMutate(
    const FString& GraphType,
    const FString& AssetPath,
    const FString& GraphName,
    const FString& Op,
    const TArray<FString>& NodesTouchedForLayout,
    TRecordPendingNodes&& RecordPendingNodes)
{
    if (!Op.Equals(TEXT("layoutgraph")))
    {
        TSet<FString> UniqueTouchedNodeIds;
        for (const FString& TouchedNodeId : NodesTouchedForLayout)
        {
            if (!TouchedNodeId.IsEmpty())
            {
                UniqueTouchedNodeIds.Add(TouchedNodeId);
            }
        }

        TArray<FString> SortedTouchedNodeIds;
        for (const FString& TouchedNodeId : UniqueTouchedNodeIds)
        {
            SortedTouchedNodeIds.Add(TouchedNodeId);
        }
        SortedTouchedNodeIds.Sort();
        RecordPendingNodes(GraphType, AssetPath, GraphName, SortedTouchedNodeIds);
    }
}

// Blueprint-specific mutate execution helpers.
bool ResolveBlueprintInsertionPointForMutate(
    const FString& BlueprintAssetPath,
    const FString& CurrentGraphName,
    const TSharedPtr<FJsonObject>& ArgsObj,
    const TMap<FString, FString>& NodeRefs,
    const FString& PreferredAnchorNodeId,
    const FString& PreferredAnchorPinName,
    int32& OutX,
    int32& OutY)
{
    OutX = 0;
    OutY = 0;

    if (HasExplicitPositionField(ArgsObj))
    {
        GetPointFromJsonObject(ArgsObj, OutX, OutY);
        return true;
    }

    UBlueprint* Blueprint = LoadBlueprintByAssetPath(BlueprintAssetPath);
    UEdGraph* TargetGraph = ResolveBlueprintGraph(Blueprint, CurrentGraphName);
    if (Blueprint == nullptr || TargetGraph == nullptr)
    {
        return false;
    }

    auto EstimateNodeSize = [](const UEdGraphNode* Node) -> FVector2D
    {
        if (const UEdGraphNode_Comment* CommentNode = Cast<UEdGraphNode_Comment>(Node))
        {
            return FVector2D(
                FMath::Max(160.0f, static_cast<float>(CommentNode->NodeWidth)),
                FMath::Max(120.0f, static_cast<float>(CommentNode->NodeHeight)));
        }
        return FVector2D(280.0f, 160.0f);
    };

    UEdGraphNode* AnchorNode = ResolveBlueprintGraphNodeByToken(TargetGraph, PreferredAnchorNodeId);
    FString AnchorPinName = PreferredAnchorPinName;
    if (AnchorNode == nullptr && ArgsObj.IsValid())
    {
        auto TryResolveAnchorField =
            [&NodeRefs, TargetGraph, &AnchorNode, &AnchorPinName](
                const TSharedPtr<FJsonObject>& SourceObj,
                const TCHAR* FieldName) -> bool
        {
            const TSharedPtr<FJsonObject>* AnchorObj = nullptr;
            if (!TryGetRequiredObjectField(SourceObj, FieldName, AnchorObj))
            {
                return false;
            }

            FString AnchorToken;
            if (!ResolveNodeTokenWithRefs(*AnchorObj, NodeRefs, AnchorToken, true) || AnchorToken.IsEmpty())
            {
                return false;
            }

            AnchorNode = ResolveBlueprintGraphNodeByToken(TargetGraph, AnchorToken);
            if (AnchorNode == nullptr)
            {
                return false;
            }

            FString ResolvedPinName;
            if (TryResolvePinNameField(*AnchorObj, ResolvedPinName))
            {
                AnchorPinName = ResolvedPinName;
            }
            return true;
        };

        if (!TryResolveAnchorField(ArgsObj, TEXT("anchor")))
        {
            if (!TryResolveAnchorField(ArgsObj, TEXT("near")))
            {
                if (!TryResolveAnchorField(ArgsObj, TEXT("from")))
                {
                    TryResolveAnchorField(ArgsObj, TEXT("target"));
                }
            }
        }
    }

    if (AnchorNode != nullptr)
    {
        const FVector2D AnchorSize = EstimateNodeSize(AnchorNode);
        OutX = AnchorNode->NodePosX + static_cast<int32>(AnchorSize.X) + 96;
        OutY = AnchorNode->NodePosY;

        if (!AnchorPinName.IsEmpty())
        {
            if (UEdGraphPin* AnchorPin = FindPinByName(AnchorNode, AnchorPinName))
            {
                if (AnchorPin->Direction == EGPD_Input)
                {
                    OutX = AnchorNode->NodePosX - 384;
                }
            }
        }
    }
    else
    {
        UEdGraphNode* RightmostNode = nullptr;
        for (UEdGraphNode* Node : TargetGraph->Nodes)
        {
            if (Node == nullptr)
            {
                continue;
            }
            if (RightmostNode == nullptr || Node->NodePosX > RightmostNode->NodePosX)
            {
                RightmostNode = Node;
            }
        }

        if (RightmostNode != nullptr)
        {
            const FVector2D AnchorSize = EstimateNodeSize(RightmostNode);
            OutX = RightmostNode->NodePosX + static_cast<int32>(AnchorSize.X) + 96;
            OutY = RightmostNode->NodePosY;
        }
    }

    const int32 GridSize = 16;
    const int32 VerticalStep = 192;
    const FVector2D CandidateSize(280.0f, 160.0f);
    OutX = FMath::GridSnap(OutX, GridSize);
    OutY = FMath::GridSnap(OutY, GridSize);

    bool bCollides = true;
    while (bCollides)
    {
        bCollides = false;
        const FBox2D CandidateRect(
            FVector2D(OutX, OutY),
            FVector2D(OutX + CandidateSize.X, OutY + CandidateSize.Y));

        for (UEdGraphNode* ExistingNode : TargetGraph->Nodes)
        {
            if (ExistingNode == nullptr || ExistingNode == AnchorNode)
            {
                continue;
            }

            const FVector2D ExistingSize = EstimateNodeSize(ExistingNode);
            const FBox2D ExistingRect(
                FVector2D(ExistingNode->NodePosX, ExistingNode->NodePosY),
                FVector2D(ExistingNode->NodePosX + ExistingSize.X, ExistingNode->NodePosY + ExistingSize.Y));
            if (CandidateRect.Intersect(ExistingRect))
            {
                OutY = FMath::GridSnap(OutY + VerticalStep, GridSize);
                bCollides = true;
                break;
            }
        }
    }

    return true;
}

FBlueprintMutateOpExecution ExecuteBlueprintMutateOp(
    const FString& Op,
    const FString& AssetPath,
    const FString& GraphName,
    const TSharedPtr<FJsonObject>& ArgsObj,
    const TMap<FString, FString>& NodeRefs,
    bool bDryRun,
    int32 Index,
    TFunctionRef<bool(const FString&, TArray<FString>&, bool)> ResolvePendingLayoutNodes,
    TFunctionRef<bool(const FString&, const FString&, const TArray<FString>&, TArray<FString>&, FString&)> ApplyLayout)
{
    FBlueprintMutateOpExecution Execution;

    if (Op.Equals(TEXT("addnode.byclass")))
    {
        FString NodeClassPath;
        ArgsObj->TryGetStringField(TEXT("nodeClassPath"), NodeClassPath);
        int32 X = 0;
        int32 Y = 0;
        ResolveBlueprintInsertionPointForMutate(AssetPath, GraphName, ArgsObj, NodeRefs, TEXT(""), TEXT(""), X, Y);
        Execution.bOk = FLoomleBlueprintAdapter::AddNodeByClass(
            AssetPath,
            GraphName,
            NodeClassPath,
            SerializeJsonObjectForMutate(ArgsObj),
            X,
            Y,
            Execution.NodeId,
            Execution.Error);
        if (Execution.bOk)
        {
            Execution.NodesTouchedForLayout.Add(Execution.NodeId);
            Execution.GraphEventName = TEXT("graph.node_added");
            Execution.GraphEventData->SetStringField(TEXT("nodeId"), Execution.NodeId);
            Execution.GraphEventData->SetStringField(TEXT("nodeType"), TEXT("by_class"));
            Execution.GraphEventData->SetStringField(TEXT("nodeClassPath"), NodeClassPath);
            Execution.GraphEventData->SetStringField(TEXT("op"), Op);
        }
        return Execution;
    }

    if (Op.Equals(TEXT("connectpins")) || Op.Equals(TEXT("disconnectpins")))
    {
        FMutateResolvedPinEndpoint FromEndpoint;
        FMutateResolvedPinEndpoint ToEndpoint;
        Execution.bOk = TryResolveMutatePinEndpointField(ArgsObj, TEXT("from"), NodeRefs, FromEndpoint)
            && TryResolveMutatePinEndpointField(ArgsObj, TEXT("to"), NodeRefs, ToEndpoint);
        if (Execution.bOk)
        {
            Execution.bOk = Op.Equals(TEXT("connectpins"))
                ? FLoomleBlueprintAdapter::ConnectPins(
                    AssetPath,
                    GraphName,
                    FromEndpoint.NodeId,
                    FromEndpoint.PinName,
                    ToEndpoint.NodeId,
                    ToEndpoint.PinName,
                    Execution.Error)
                : FLoomleBlueprintAdapter::DisconnectPins(
                    AssetPath,
                    GraphName,
                    FromEndpoint.NodeId,
                    FromEndpoint.PinName,
                    ToEndpoint.NodeId,
                    ToEndpoint.PinName,
                    Execution.Error);
        }
        if (!Execution.bOk && Execution.Error.IsEmpty())
        {
            Execution.Error = Op.Equals(TEXT("connectpins"))
                ? TEXT("Failed to resolve connectPins node ids/pins.")
                : TEXT("Failed to resolve disconnectPins node ids/pins.");
        }
        if (Execution.bOk)
        {
            Execution.NodesTouchedForLayout.Add(FromEndpoint.NodeId);
            Execution.NodesTouchedForLayout.Add(ToEndpoint.NodeId);
            Execution.GraphEventName = Op.Equals(TEXT("connectpins"))
                ? TEXT("graph.node_connected")
                : TEXT("graph.links_changed");
            if (Op.Equals(TEXT("disconnectpins")))
            {
                Execution.GraphEventData->SetStringField(TEXT("change"), TEXT("disconnected"));
            }
            Execution.GraphEventData->SetStringField(TEXT("fromNodeId"), FromEndpoint.NodeId);
            Execution.GraphEventData->SetStringField(TEXT("fromPin"), FromEndpoint.PinName);
            Execution.GraphEventData->SetStringField(TEXT("toNodeId"), ToEndpoint.NodeId);
            Execution.GraphEventData->SetStringField(TEXT("toPin"), ToEndpoint.PinName);
            Execution.GraphEventData->SetStringField(TEXT("op"), Op);
        }
        return Execution;
    }

    if (Op.Equals(TEXT("breakpinlinks")))
    {
        FMutateResolvedPinEndpoint TargetEndpoint;
        Execution.bOk = TryResolveMutatePinEndpointField(ArgsObj, TEXT("target"), NodeRefs, TargetEndpoint);
        if (Execution.bOk)
        {
            Execution.bOk = FLoomleBlueprintAdapter::BreakPinLinks(
                AssetPath,
                GraphName,
                TargetEndpoint.NodeId,
                TargetEndpoint.PinName,
                Execution.Error);
        }
        if (!Execution.bOk && Execution.Error.IsEmpty())
        {
            Execution.Error = TEXT("Failed to resolve breakPinLinks target.");
        }
        if (Execution.bOk)
        {
            Execution.NodesTouchedForLayout.Add(TargetEndpoint.NodeId);
            Execution.GraphEventName = TEXT("graph.links_changed");
            Execution.GraphEventData->SetStringField(TEXT("change"), TEXT("break_all"));
            Execution.GraphEventData->SetStringField(TEXT("nodeId"), TargetEndpoint.NodeId);
            Execution.GraphEventData->SetStringField(TEXT("pin"), TargetEndpoint.PinName);
            Execution.GraphEventData->SetStringField(TEXT("op"), Op);
        }
        return Execution;
    }

    if (Op.Equals(TEXT("setpindefault")))
    {
        FMutateResolvedPinEndpoint TargetEndpoint;
        FString Value;
        Execution.bOk = TryResolveMutatePinEndpointField(ArgsObj, TEXT("target"), NodeRefs, TargetEndpoint);
        if (Execution.bOk)
        {
            ArgsObj->TryGetStringField(TEXT("value"), Value);
            Execution.bOk = FLoomleBlueprintAdapter::SetPinDefaultValue(
                AssetPath,
                GraphName,
                TargetEndpoint.NodeId,
                TargetEndpoint.PinName,
                Value,
                Execution.Error);
        }
        if (!Execution.bOk && Execution.Error.IsEmpty())
        {
            Execution.Error = TEXT("Failed to resolve setPinDefault target.");
        }
        if (!Execution.bOk)
        {
            FString DetailsJson;
            FString DetailsError;
            if (FLoomleBlueprintAdapter::DescribePinTarget(
                    AssetPath,
                    GraphName,
                    TargetEndpoint.NodeId,
                    TargetEndpoint.PinName,
                    DetailsJson,
                    DetailsError))
            {
                TSharedPtr<FJsonObject> DetailsObject;
                TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(DetailsJson);
                if (FJsonSerializer::Deserialize(Reader, DetailsObject) && DetailsObject.IsValid())
                {
                    Execution.ErrorDetailsForOp = DetailsObject;
                }
            }
        }
        if (Execution.bOk)
        {
            Execution.NodesTouchedForLayout.Add(TargetEndpoint.NodeId);
            Execution.GraphEventName = TEXT("graph.pin_default_changed");
            Execution.GraphEventData->SetStringField(TEXT("nodeId"), TargetEndpoint.NodeId);
            Execution.GraphEventData->SetStringField(TEXT("pin"), TargetEndpoint.PinName);
            Execution.GraphEventData->SetStringField(TEXT("value"), Value);
            Execution.GraphEventData->SetStringField(TEXT("op"), Op);
        }
        return Execution;
    }

    if (Op.Equals(TEXT("removenode")))
    {
        Execution.bOk = ResolveNodeTokenFromArgsWithRefs(ArgsObj, NodeRefs, Execution.NodeId, true)
            && FLoomleBlueprintAdapter::RemoveNode(AssetPath, GraphName, Execution.NodeId, Execution.Error);
        if (!Execution.bOk && Execution.Error.IsEmpty())
        {
            Execution.Error = TEXT("Failed to resolve removeNode target. Provide nodeId, nodeRef, nodePath, path, nodeName, or name.");
        }
        if (Execution.bOk)
        {
            Execution.NodesTouchedForLayout.Add(Execution.NodeId);
            Execution.GraphEventName = TEXT("graph.node_removed");
            Execution.GraphEventData->SetStringField(TEXT("nodeId"), Execution.NodeId);
            Execution.GraphEventData->SetStringField(TEXT("op"), Op);
        }
        return Execution;
    }

    if (Op.Equals(TEXT("movenode")) || Op.Equals(TEXT("movenodeby")))
    {
        FString NodeToken;
        Execution.bOk = ResolveNodeTokenFromArgsWithRefs(ArgsObj, NodeRefs, NodeToken, true);
        int32 X = 0;
        int32 Y = 0;
        if (Execution.bOk && Op.Equals(TEXT("movenodeby")))
        {
            UBlueprint* Blueprint = LoadBlueprintByAssetPath(AssetPath);
            UEdGraph* TargetGraph = ResolveBlueprintGraph(Blueprint, GraphName);
            UEdGraphNode* TargetNode = ResolveBlueprintGraphNodeByToken(TargetGraph, NodeToken);
            if (TargetNode == nullptr)
            {
                Execution.bOk = false;
                Execution.Error = TEXT("Failed to resolve moveNodeBy target.");
            }
            else
            {
                int32 Dx = 0;
                int32 Dy = 0;
                GetDeltaFromJsonObject(ArgsObj, Dx, Dy);
                X = TargetNode->NodePosX + Dx;
                Y = TargetNode->NodePosY + Dy;
            }
        }
        else
        {
            GetPointFromJsonObject(ArgsObj, X, Y);
        }

        Execution.bOk = Execution.bOk
            && FLoomleBlueprintAdapter::MoveNode(AssetPath, GraphName, NodeToken, X, Y, Execution.Error);
        if (!Execution.bOk && Execution.Error.IsEmpty())
        {
            Execution.Error = FString::Printf(TEXT("Failed to resolve %s target."), *Op);
        }
        if (Execution.bOk)
        {
            Execution.NodesTouchedForLayout.Add(NodeToken);
            Execution.GraphEventName = TEXT("graph.node_moved");
            Execution.GraphEventData->SetStringField(TEXT("nodeId"), NodeToken);
            Execution.GraphEventData->SetNumberField(TEXT("x"), X);
            Execution.GraphEventData->SetNumberField(TEXT("y"), Y);
            Execution.GraphEventData->SetStringField(TEXT("op"), Op);
        }
        return Execution;
    }

    if (Op.Equals(TEXT("movenodes")))
    {
        TArray<FString> NodeTokens;
        Execution.bOk = ResolveNodeTokenArrayWithRefs(ArgsObj, NodeRefs, NodeTokens, true);
        if (!Execution.bOk)
        {
            Execution.Error = TEXT("Missing nodeIds/nodes for moveNodes.");
            return Execution;
        }

        UBlueprint* Blueprint = LoadBlueprintByAssetPath(AssetPath);
        UEdGraph* TargetGraph = ResolveBlueprintGraph(Blueprint, GraphName);
        if (TargetGraph == nullptr)
        {
            Execution.bOk = false;
            Execution.Error = TEXT("Failed to resolve target graph for moveNodes.");
            return Execution;
        }

        int32 Dx = 0;
        int32 Dy = 0;
        GetDeltaFromJsonObject(ArgsObj, Dx, Dy);

        TArray<TSharedPtr<FJsonValue>> MovedNodeIds;
        for (const FString& NodeToken : NodeTokens)
        {
            UEdGraphNode* TargetNode = ResolveBlueprintGraphNodeByToken(TargetGraph, NodeToken);
            if (TargetNode == nullptr)
            {
                Execution.bOk = false;
                Execution.Error = FString::Printf(TEXT("Failed to resolve moveNodes target: %s"), *NodeToken);
                break;
            }

            const int32 NewX = TargetNode->NodePosX + Dx;
            const int32 NewY = TargetNode->NodePosY + Dy;
            if (!FLoomleBlueprintAdapter::MoveNode(AssetPath, GraphName, NodeToken, NewX, NewY, Execution.Error))
            {
                Execution.bOk = false;
                break;
            }

            MovedNodeIds.Add(MakeShared<FJsonValueString>(
                TargetNode->NodeGuid.ToString(EGuidFormats::DigitsWithHyphensLower)));
        }

        if (Execution.bOk)
        {
            for (const TSharedPtr<FJsonValue>& MovedNodeId : MovedNodeIds)
            {
                FString MovedNodeIdString;
                if (MovedNodeId.IsValid() && MovedNodeId->TryGetString(MovedNodeIdString))
                {
                    Execution.NodesTouchedForLayout.Add(MovedNodeIdString);
                }
            }
            Execution.bChanged = MovedNodeIds.Num() > 0;
            Execution.GraphEventName = TEXT("graph.node_moved");
            Execution.GraphEventData->SetArrayField(TEXT("nodeIds"), MovedNodeIds);
            Execution.GraphEventData->SetNumberField(TEXT("dx"), Dx);
            Execution.GraphEventData->SetNumberField(TEXT("dy"), Dy);
            Execution.GraphEventData->SetStringField(TEXT("op"), Op);
        }
        return Execution;
    }

    if (Op.Equals(TEXT("layoutgraph")))
    {
        ExecuteMutateLayoutGraphOp(
            Op,
            GraphName,
            ArgsObj,
            Execution,
            [&NodeRefs](const TSharedPtr<FJsonObject>& SourceArgs, TArray<FString>& OutNodeIds)
            {
                ResolveNodeTokenArrayWithRefs(SourceArgs, NodeRefs, OutNodeIds, true);
            },
            [&ResolvePendingLayoutNodes](const FString& LayoutGraphName, TArray<FString>& OutNodeIds, bool bConsume)
            {
                ResolvePendingLayoutNodes(LayoutGraphName, OutNodeIds, bConsume);
            },
            [&ApplyLayout](const FString& LayoutGraphName, const FString& LayoutScope, const TArray<FString>& RequestedNodeIds, TArray<FString>& OutMovedNodeIds, FString& OutError)
            {
                return ApplyLayout(LayoutGraphName, LayoutScope, RequestedNodeIds, OutMovedNodeIds, OutError);
            });
        return Execution;
    }

    if (Op.Equals(TEXT("compile")))
    {
        Execution.bOk = FLoomleBlueprintAdapter::CompileBlueprint(AssetPath, GraphName, Execution.Error);
        if (Execution.bOk)
        {
            Execution.GraphEventName = TEXT("graph.compiled");
            Execution.GraphEventData->SetStringField(TEXT("op"), Op);
        }
        return Execution;
    }

    if (Op.Equals(TEXT("runscript")))
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
            Execution.bOk = false;
            Execution.Error = TEXT("runScript requires args.code when mode=inlineCode.");
        }
        else if (ModeNormalized.Equals(TEXT("scriptid")) && ScriptId.IsEmpty())
        {
            Execution.bOk = false;
            Execution.Error = TEXT("runScript requires args.scriptId when mode=scriptId.");
        }
        else if (bDryRun)
        {
            Execution.bSkipped = true;
            Execution.SkipReason = TEXT("dryRun");
        }
        else
        {
            TSharedPtr<FJsonObject> ScriptContext = MakeShared<FJsonObject>();
            ScriptContext->SetStringField(TEXT("assetPath"), AssetPath);
            ScriptContext->SetStringField(TEXT("graphName"), GraphName);
            ScriptContext->SetNumberField(TEXT("opIndex"), Index);
            ScriptContext->SetBoolField(TEXT("dryRun"), bDryRun);
            ScriptContext->SetObjectField(TEXT("input"), ScriptInput);
            ScriptContext->SetObjectField(TEXT("nodeRefs"), MakeNodeRefsObject(NodeRefs));

            const FString ContextJson = SerializeJsonObjectForMutate(ScriptContext);
            const FString ContextB64 = FBase64::Encode(ContextJson);
            const FString CodeB64 = FBase64::Encode(ScriptCode);
            const FString ScriptIdB64 = FBase64::Encode(ScriptId);
            const FString EntryEscaped = EscapePythonSingleQuotedString(Entry);
            const FString ModeEscaped = EscapePythonSingleQuotedString(Mode);

            static constexpr int32 ScriptTimeoutSeconds = 30;

            FString PythonSource;
            PythonSource += TEXT("import base64, json, importlib, signal, sys, platform\n");
            PythonSource += FString::Printf(TEXT("_LOOMLE_TIMEOUT = %d\n"), ScriptTimeoutSeconds);
            PythonSource += TEXT("def _loomle_timeout_handler(signum, frame):\n");
            PythonSource += TEXT("    raise TimeoutError(f'runScript exceeded {_LOOMLE_TIMEOUT}s timeout')\n");
            PythonSource += TEXT("if platform.system() != 'Windows' and hasattr(signal, 'SIGALRM'):\n");
            PythonSource += TEXT("    signal.signal(signal.SIGALRM, _loomle_timeout_handler)\n");
            PythonSource += TEXT("    signal.alarm(_LOOMLE_TIMEOUT)\n");
            PythonSource += TEXT("try:\n");
            PythonSource += FString::Printf(TEXT("    _ctx = json.loads(base64.b64decode('%s').decode('utf-8'))\n"), *ContextB64);
            PythonSource += FString::Printf(TEXT("    _mode = '%s'\n"), *ModeEscaped);
            PythonSource += FString::Printf(TEXT("    _entry = '%s'\n"), *EntryEscaped);
            PythonSource += TEXT("    _fn = None\n");
            PythonSource += TEXT("    if _mode.lower() == 'inlinecode':\n");
            PythonSource += FString::Printf(TEXT("        _src = base64.b64decode('%s').decode('utf-8')\n"), *CodeB64);
            PythonSource += TEXT("        _ns = {}\n");
            PythonSource += TEXT("        exec(_src, _ns)\n");
            PythonSource += TEXT("        _fn = _ns.get(_entry)\n");
            PythonSource += TEXT("    else:\n");
            PythonSource += FString::Printf(TEXT("        _mod_name = base64.b64decode('%s').decode('utf-8')\n"), *ScriptIdB64);
            PythonSource += TEXT("        _mod = importlib.import_module(_mod_name)\n");
            PythonSource += TEXT("        _fn = getattr(_mod, _entry)\n");
            PythonSource += TEXT("    if _fn is None:\n");
            PythonSource += TEXT("        raise RuntimeError(f'Entry not found: {_entry}')\n");
            PythonSource += TEXT("    _out = _fn(_ctx)\n");
            PythonSource += TEXT("    if _out is None:\n");
            PythonSource += TEXT("        _out = {}\n");
            PythonSource += TEXT("    print('__LOOMLE_SCRIPT_RESULT__' + json.dumps(_out, ensure_ascii=False))\n");
            PythonSource += TEXT("finally:\n");
            PythonSource += TEXT("    if platform.system() != 'Windows' and hasattr(signal, 'SIGALRM'):\n");
            PythonSource += TEXT("        signal.alarm(0)\n");

            IPythonScriptPlugin* PythonScriptPlugin = IPythonScriptPlugin::Get();
            if (PythonScriptPlugin == nullptr)
            {
                Execution.bOk = false;
                Execution.Error = TEXT("PythonScriptPlugin module is not loaded.");
            }
            else
            {
                if (!PythonScriptPlugin->IsPythonInitialized())
                {
                    PythonScriptPlugin->ForceEnablePythonAtRuntime();
                }
                if (!PythonScriptPlugin->IsPythonInitialized())
                {
                    Execution.bOk = false;
                    Execution.Error = TEXT("Python runtime is not initialized.");
                }
                else
                {
                    FPythonCommandEx PythonCommand;
                    PythonCommand.ExecutionMode = EPythonCommandExecutionMode::ExecuteFile;
                    PythonCommand.Command = PythonSource;
                    Execution.bOk = PythonScriptPlugin->ExecPythonCommandEx(PythonCommand);
                    if (!Execution.bOk)
                    {
                        Execution.Error = PythonCommand.CommandResult.IsEmpty() ? TEXT("runScript execution failed.") : PythonCommand.CommandResult;
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
                            Execution.bOk = false;
                            Execution.Error = TEXT("runScript did not emit structured result.");
                        }
                        else
                        {
                            TSharedPtr<FJsonObject> ScriptResultObj;
                            TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(ScriptResultJson);
                            if (!FJsonSerializer::Deserialize(Reader, ScriptResultObj) || !ScriptResultObj.IsValid())
                            {
                                Execution.bOk = false;
                                Execution.Error = TEXT("runScript returned invalid JSON result.");
                            }
                            else
                            {
                                Execution.ScriptResultForOp = ScriptResultObj;
                                Execution.GraphEventName = TEXT("graph.script_executed");
                                Execution.GraphEventData->SetStringField(TEXT("mode"), Mode);
                                Execution.GraphEventData->SetStringField(TEXT("entry"), Entry);
                                if (!ScriptId.IsEmpty())
                                {
                                    Execution.GraphEventData->SetStringField(TEXT("scriptId"), ScriptId);
                                }
                                Execution.GraphEventData->SetStringField(TEXT("op"), Op);
                            }
                        }
                    }
                }
            }
        }
        return Execution;
    }

    Execution.bOk = false;
    Execution.Error = FString::Printf(TEXT("Unsupported mutate op: %s"), *Op);
    return Execution;
}

TSharedPtr<FJsonObject> MakeGraphCatalogEntry(
    const FString& GraphName,
    const FString& GraphKind,
    const FString& GraphClassPath,
    const TSharedPtr<FJsonObject>& GraphRef,
    const TSharedPtr<FJsonObject>& ParentGraphRef,
    const FString& OwnerNodeId,
    const FString& LoadStatus)
{
    TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
    Entry->SetStringField(TEXT("graphName"), GraphName);
    Entry->SetStringField(TEXT("graphKind"), GraphKind);
    Entry->SetStringField(TEXT("graphClassPath"), GraphClassPath);
    if (GraphRef.IsValid())
    {
        Entry->SetObjectField(TEXT("graphRef"), GraphRef);
    }
    else
    {
        Entry->SetField(TEXT("graphRef"), MakeShared<FJsonValueNull>());
    }
    if (ParentGraphRef.IsValid())
    {
        Entry->SetObjectField(TEXT("parentGraphRef"), ParentGraphRef);
    }
    else
    {
        Entry->SetField(TEXT("parentGraphRef"), MakeShared<FJsonValueNull>());
    }
    if (OwnerNodeId.IsEmpty())
    {
        Entry->SetField(TEXT("ownerNodeId"), MakeShared<FJsonValueNull>());
    }
    else
    {
        Entry->SetStringField(TEXT("ownerNodeId"), OwnerNodeId);
    }
    Entry->SetStringField(TEXT("loadStatus"), LoadStatus);
    return Entry;
}

void SetNodeChildGraphRef(
    const TSharedPtr<FJsonObject>& Node,
    const TSharedPtr<FJsonObject>& ChildGraphRef,
    const FString& ChildLoadStatus = TEXT(""))
{
    if (!Node.IsValid() || !ChildGraphRef.IsValid())
    {
        return;
    }

    Node->SetObjectField(TEXT("childGraphRef"), ChildGraphRef);
    if (!ChildLoadStatus.IsEmpty())
    {
        Node->SetStringField(TEXT("childLoadStatus"), ChildLoadStatus);
    }
}

void SetNodeAssetChildGraphRef(
    const TSharedPtr<FJsonObject>& Node,
    const FString& ChildAssetPath,
    const FString& ChildLoadStatus)
{
    SetNodeChildGraphRef(Node, MakeGraphAssetRef(ChildAssetPath, TEXT("")), ChildLoadStatus);
}

void SetNodeInlineChildGraphRef(
    const TSharedPtr<FJsonObject>& Node,
    const FString& OwnerNodeGuid,
    const FString& AssetPath)
{
    SetNodeChildGraphRef(Node, MakeGraphInlineRef(OwnerNodeGuid, AssetPath));
}

FString NormalizeReferencedAssetPath(UObject* AssetObject)
{
    if (AssetObject == nullptr)
    {
        return TEXT("");
    }

    UPackage* AssetPackage = AssetObject->GetPackage();
    FString AssetPath = AssetPackage ? AssetPackage->GetPathName() : AssetObject->GetPathName();
    if (!AssetPackage)
    {
        int32 DotIdx;
        if (AssetPath.FindLastChar(TEXT('.'), DotIdx))
        {
            AssetPath = AssetPath.Left(DotIdx);
        }
    }
    return AssetPath;
}

bool ResolveMaterialFunctionAssetPath(UMaterialFunctionInterface* MaterialFunction, FString& OutAssetPath)
{
    OutAssetPath = NormalizeReferencedAssetPath(MaterialFunction);
    return !OutAssetPath.IsEmpty();
}

bool ResolvePcgSubgraphAssetReference(UPCGSettings* NodeSettings, FString& OutAssetPath, FString& OutLoadStatus, FString& OutGraphName, FString& OutGraphClassPath)
{
    OutAssetPath.Empty();
    OutLoadStatus.Empty();
    OutGraphName.Empty();
    OutGraphClassPath.Empty();

    if (NodeSettings == nullptr || NodeSettings->GetClass() == nullptr)
    {
        return false;
    }

    UObject* SubgraphAsset = nullptr;
    FString SubgraphSoftPath;
    for (TFieldIterator<FObjectPropertyBase> PropIt(NodeSettings->GetClass()); PropIt; ++PropIt)
    {
        FObjectPropertyBase* Prop = *PropIt;
        UObject* PropVal = Prop->GetObjectPropertyValue_InContainer(NodeSettings);
        if (PropVal && PropVal->GetClass() && PropVal->GetClass()->GetPathName().Contains(TEXT("PCGGraph")))
        {
            SubgraphAsset = PropVal;
            break;
        }
    }
    if (SubgraphAsset == nullptr)
    {
        for (TFieldIterator<FSoftObjectProperty> PropIt(NodeSettings->GetClass()); PropIt; ++PropIt)
        {
            FSoftObjectProperty* SoftProp = *PropIt;
            if (!SoftProp->PropertyClass || !SoftProp->PropertyClass->GetPathName().Contains(TEXT("PCGGraph")))
            {
                continue;
            }
            const FSoftObjectPtr SoftPtr = SoftProp->GetPropertyValue_InContainer(NodeSettings);
            const FSoftObjectPath SoftPath = SoftPtr.ToSoftObjectPath();
            if (SoftPath.IsNull())
            {
                continue;
            }
            UObject* Resolved = SoftPath.ResolveObject();
            if (Resolved)
            {
                SubgraphAsset = Resolved;
            }
            else
            {
                SubgraphSoftPath = SoftPath.GetAssetPathString();
                int32 DotIdx;
                if (SubgraphSoftPath.FindLastChar(TEXT('.'), DotIdx))
                {
                    SubgraphSoftPath = SubgraphSoftPath.Left(DotIdx);
                }
            }
            break;
        }
    }

    if (SubgraphAsset == nullptr && SubgraphSoftPath.IsEmpty())
    {
        return false;
    }

    if (SubgraphAsset != nullptr)
    {
        OutAssetPath = NormalizeReferencedAssetPath(SubgraphAsset);
        OutLoadStatus = TEXT("loaded");
        OutGraphName = SubgraphAsset->GetName();
        OutGraphClassPath = SubgraphAsset->GetClass() ? SubgraphAsset->GetClass()->GetPathName() : TEXT("");
        return !OutAssetPath.IsEmpty();
    }

    OutAssetPath = SubgraphSoftPath;
    OutLoadStatus = TEXT("not_found");
    OutGraphName = FPaths::GetBaseFilename(SubgraphSoftPath);
    OutGraphClassPath = TEXT("");
    return !OutAssetPath.IsEmpty();
}

FString NormalizeBlueprintGraphKind(FString GraphKind)
{
    if (GraphKind.Equals(TEXT("Event"), ESearchCase::IgnoreCase))
    {
        return TEXT("root");
    }
    if (GraphKind.Equals(TEXT("Function"), ESearchCase::IgnoreCase))
    {
        return TEXT("function");
    }
    if (GraphKind.Equals(TEXT("Macro"), ESearchCase::IgnoreCase))
    {
        return TEXT("macro");
    }
    if (GraphKind.Equals(TEXT("Interface"), ESearchCase::IgnoreCase))
    {
        return TEXT("function");
    }
    return GraphKind;
}

struct FResolvedGraphAddress
{
    FString AssetPath;
    FString GraphName;
    FString InlineNodeGuid;
    bool bUsedGraphRef = false;
};

struct FGraphAddressResolutionOptions
{
    FString BlueprintDefaultGraphName;
    bool bRequireBlueprintGraphNameInModeA = false;
    bool bUseModeLabelsInMessages = false;
    bool bUseKindSpecificGraphRefAssetErrors = false;
    bool bRejectNonBlueprintInlineGraphRefs = false;
    bool bResolveInlineBlueprintGraphName = false;
};

bool SetGraphAddressError(
    const TSharedPtr<FJsonObject>& Result,
    const FString& Code,
    const FString& Message)
{
    if (Result.IsValid())
    {
        Result->SetBoolField(TEXT("isError"), true);
        Result->SetStringField(TEXT("code"), Code);
        Result->SetStringField(TEXT("message"), Message);
    }
    return false;
}

FString GetDefaultGraphNameForType(
    const FString& GraphType,
    const FString& BlueprintDefaultGraphName)
{
    if (GraphType.Equals(TEXT("pcg")))
    {
        return TEXT("PCGGraph");
    }
    if (GraphType.Equals(TEXT("material")))
    {
        return TEXT("MaterialGraph");
    }
    if (GraphType.Equals(TEXT("blueprint")))
    {
        return BlueprintDefaultGraphName;
    }
    return TEXT("");
}

bool ResolveInlineBlueprintGraphName(
    const FString& InAssetPath,
    const FString& InNodeGuid,
    FString& OutGraphName,
    FString& OutError)
{
    FString NodesJson;
    OutGraphName.Empty();
    OutError.Empty();
    return FLoomleBlueprintAdapter::ListCompositeSubgraphNodes(
               InAssetPath,
               InNodeGuid,
               OutGraphName,
               NodesJson,
               OutError)
        && !OutGraphName.IsEmpty();
}

bool ResolveGraphRefObject(
    const TSharedPtr<FJsonObject>& GraphRefObj,
    const FString& GraphType,
    const FString& DefaultGraphName,
    const FGraphAddressResolutionOptions& Options,
    FResolvedGraphAddress& OutAddress,
    FString& OutCode,
    FString& OutMessage)
{
    OutAddress = FResolvedGraphAddress();
    OutAddress.bUsedGraphRef = true;
    OutCode.Empty();
    OutMessage.Empty();

    if (!GraphRefObj.IsValid())
    {
        OutCode = TEXT("GRAPH_REF_INVALID");
        OutMessage = TEXT("graphRef is required.");
        return false;
    }

    FString Kind;
    if (!GraphRefObj->TryGetStringField(TEXT("kind"), Kind) || Kind.IsEmpty())
    {
        OutCode = TEXT("GRAPH_REF_INVALID");
        OutMessage = TEXT("graphRef.kind is required.");
        return false;
    }
    Kind = Kind.ToLower();

    FString RefAssetPath;
    if (!GraphRefObj->TryGetStringField(TEXT("assetPath"), RefAssetPath) || RefAssetPath.IsEmpty())
    {
        OutCode = TEXT("GRAPH_REF_INVALID");
        OutMessage = Options.bUseKindSpecificGraphRefAssetErrors
            ? FString::Printf(TEXT("graphRef.assetPath is required for kind=%s."), *Kind)
            : TEXT("graphRef.assetPath is required.");
        return false;
    }
    OutAddress.AssetPath = NormalizeAssetPath(RefAssetPath);

    if (Kind.Equals(TEXT("asset")))
    {
        if (!GraphRefObj->TryGetStringField(TEXT("graphName"), OutAddress.GraphName) || OutAddress.GraphName.IsEmpty())
        {
            OutAddress.GraphName = DefaultGraphName;
        }
        return true;
    }

    if (Kind.Equals(TEXT("inline")))
    {
        if (Options.bRejectNonBlueprintInlineGraphRefs && !GraphType.Equals(TEXT("blueprint")))
        {
            OutCode = TEXT("GRAPH_REF_INVALID");
            OutMessage = TEXT("graphRef.kind=inline is only supported for blueprint graphType.");
            return false;
        }

        if (!GraphRefObj->TryGetStringField(TEXT("nodeGuid"), OutAddress.InlineNodeGuid) || OutAddress.InlineNodeGuid.IsEmpty())
        {
            OutCode = TEXT("GRAPH_REF_INVALID");
            OutMessage = TEXT("graphRef.nodeGuid is required for kind=inline.");
            return false;
        }

        if (Options.bResolveInlineBlueprintGraphName && GraphType.Equals(TEXT("blueprint")))
        {
            FString ResolveError;
            if (!ResolveInlineBlueprintGraphName(OutAddress.AssetPath, OutAddress.InlineNodeGuid, OutAddress.GraphName, ResolveError))
            {
                OutCode = TEXT("GRAPH_NOT_FOUND");
                OutMessage = ResolveError.IsEmpty() ? TEXT("Failed to resolve inline graphRef.") : ResolveError;
                return false;
            }
        }
        return true;
    }

    OutCode = TEXT("GRAPH_REF_INVALID");
    OutMessage = FString::Printf(TEXT("Unsupported graphRef.kind: %s"), *Kind);
    return false;
}

FString RewriteGraphRefErrorPrefix(
    const FString& Message,
    const FString& OldPrefix,
    const FString& NewPrefix)
{
    if (Message.IsEmpty())
    {
        return Message;
    }
    return Message.Replace(*OldPrefix, *NewPrefix, ESearchCase::CaseSensitive);
}

bool ResolveTargetGraphRefObject(
    const TSharedPtr<FJsonObject>& TargetGraphRefObj,
    const FString& GraphType,
    const FString& RequestAssetPath,
    const FString& DefaultGraphName,
    FResolvedGraphAddress& OutAddress,
    FString& OutError)
{
    FString IgnoredCode;
    FString ErrorMessage;
    FGraphAddressResolutionOptions Options;
    Options.BlueprintDefaultGraphName = DefaultGraphName;
    Options.bRejectNonBlueprintInlineGraphRefs = true;
    Options.bResolveInlineBlueprintGraphName = true;
    if (!ResolveGraphRefObject(TargetGraphRefObj, GraphType, DefaultGraphName, Options, OutAddress, IgnoredCode, ErrorMessage))
    {
        OutError = RewriteGraphRefErrorPrefix(ErrorMessage, TEXT("graphRef"), TEXT("targetGraphRef"));
        return false;
    }

    if (!OutAddress.AssetPath.Equals(RequestAssetPath, ESearchCase::CaseSensitive))
    {
        OutError = TEXT("targetGraphRef.assetPath must match request assetPath for this mutate call.");
        return false;
    }

    OutError.Empty();
    return true;
}

bool ResolveGraphRequestAddress(
    const TSharedPtr<FJsonObject>& Arguments,
    const FString& GraphType,
    const FGraphAddressResolutionOptions& Options,
    FResolvedGraphAddress& OutAddress,
    const TSharedPtr<FJsonObject>& Result)
{
    OutAddress = FResolvedGraphAddress();
    const FString DefaultGraphName = GetDefaultGraphNameForType(GraphType, Options.BlueprintDefaultGraphName);

    const TSharedPtr<FJsonObject>* GraphRefObj = nullptr;
    const bool bHasGraphRef = Arguments.IsValid()
        && Arguments->TryGetObjectField(TEXT("graphRef"), GraphRefObj)
        && GraphRefObj != nullptr
        && (*GraphRefObj).IsValid();

    FString ProvidedGraphName;
    const bool bHasGraphName = Arguments.IsValid()
        && Arguments->TryGetStringField(TEXT("graphName"), ProvidedGraphName)
        && !ProvidedGraphName.IsEmpty();

    if (bHasGraphRef && bHasGraphName)
    {
        const FString ConflictMessage = Options.bUseModeLabelsInMessages
            ? TEXT("Supply either graphRef (Mode B) or graphName (Mode A), not both.")
            : TEXT("Supply either graphRef or graphName, not both.");
        return SetGraphAddressError(Result, TEXT("INVALID_ARGUMENT"), ConflictMessage);
    }

    if (bHasGraphRef)
    {
        FString ErrorCode;
        FString ErrorMessage;
        if (!ResolveGraphRefObject(*GraphRefObj, GraphType, DefaultGraphName, Options, OutAddress, ErrorCode, ErrorMessage))
        {
            return SetGraphAddressError(Result, ErrorCode, ErrorMessage);
        }
        return true;
    }

    if (!Arguments.IsValid()
        || !Arguments->TryGetStringField(TEXT("assetPath"), OutAddress.AssetPath)
        || OutAddress.AssetPath.IsEmpty())
    {
        const FString MissingAssetMessage = Options.bUseModeLabelsInMessages
            ? TEXT("arguments.assetPath is required (Mode A) or supply graphRef (Mode B).")
            : TEXT("arguments.assetPath is required (or supply graphRef).");
        return SetGraphAddressError(Result, TEXT("INVALID_ARGUMENT"), MissingAssetMessage);
    }
    OutAddress.AssetPath = NormalizeAssetPath(OutAddress.AssetPath);

    if (bHasGraphName)
    {
        OutAddress.GraphName = ProvidedGraphName;
        return true;
    }

    if (GraphType.Equals(TEXT("blueprint")) && Options.bRequireBlueprintGraphNameInModeA)
    {
        const FString MissingGraphNameMessage = Options.bUseModeLabelsInMessages
            ? TEXT("arguments.graphName is required (Mode A) or supply graphRef (Mode B).")
            : TEXT("arguments.graphName is required (or supply graphRef).");
        return SetGraphAddressError(Result, TEXT("INVALID_ARGUMENT"), MissingGraphNameMessage);
    }

    OutAddress.GraphName = DefaultGraphName;
    return true;
}

TSharedPtr<FJsonObject> MakeEffectiveGraphRef(const FResolvedGraphAddress& Address)
{
    if (!Address.InlineNodeGuid.IsEmpty())
    {
        return MakeGraphInlineRef(Address.InlineNodeGuid, Address.AssetPath);
    }
    return MakeGraphAssetRef(Address.AssetPath, Address.GraphName);
}

template <typename TEnum>
FString GetPcgEnumName(TEnum Value)
{
    const UEnum* Enum = StaticEnum<TEnum>();
    return Enum ? Enum->GetNameStringByValue(static_cast<int64>(Value)) : FString::Printf(TEXT("%d"), static_cast<int32>(Value));
}

TSharedPtr<FJsonObject> MakePcgSelectorObject(const FPCGAttributePropertySelector& Selector)
{
    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetStringField(TEXT("text"), Selector.ToString());
    Result->SetStringField(TEXT("selection"), GetPcgEnumName(Selector.GetSelection()));
    Result->SetStringField(TEXT("name"), Selector.GetName().ToString());
    Result->SetStringField(TEXT("domain"), Selector.GetDomainString(false));
    Result->SetStringField(TEXT("attributeOrProperty"), Selector.GetAttributePropertyString(false));

    TArray<TSharedPtr<FJsonValue>> Accessors;
    for (const FString& ExtraName : Selector.GetExtraNames())
    {
        Accessors.Add(MakeShared<FJsonValueString>(ExtraName));
    }
    Result->SetArrayField(TEXT("accessors"), Accessors);
    return Result;
}

bool HasPcgPinNamed(const TArray<TSharedPtr<FJsonValue>>& Pins, const FString& PinName)
{
    for (const TSharedPtr<FJsonValue>& PinValue : Pins)
    {
        const TSharedPtr<FJsonObject>* PinObject = nullptr;
        if (!PinValue.IsValid() || !PinValue->TryGetObject(PinObject) || PinObject == nullptr || !(*PinObject).IsValid())
        {
            continue;
        }

        FString ExistingPinName;
        if ((*PinObject)->TryGetStringField(TEXT("name"), ExistingPinName)
            && ExistingPinName.Equals(PinName, ESearchCase::IgnoreCase))
        {
            return true;
        }
    }

    return false;
}

void AppendPcgSyntheticSettingPin(
    TArray<TSharedPtr<FJsonValue>>& Pins,
    const FString& PinName,
    const FString& DefaultValue,
    const FString& DefaultText = FString())
{
    if (PinName.IsEmpty() || HasPcgPinNamed(Pins, PinName))
    {
        return;
    }

    const FString EffectiveDefaultText = DefaultText.IsEmpty() ? DefaultValue : DefaultText;

    TSharedPtr<FJsonObject> PinObj = MakeShared<FJsonObject>();
    PinObj->SetStringField(TEXT("name"), PinName);
    PinObj->SetStringField(TEXT("direction"), TEXT("input"));
    PinObj->SetStringField(TEXT("category"), TEXT("pcg"));
    PinObj->SetStringField(TEXT("subCategory"), TEXT("setting"));
    PinObj->SetStringField(TEXT("subCategoryObject"), TEXT(""));
    PinObj->SetBoolField(TEXT("isReference"), false);
    PinObj->SetBoolField(TEXT("isConst"), true);
    PinObj->SetBoolField(TEXT("isArray"), false);
    PinObj->SetBoolField(TEXT("isSyntheticDefaultTarget"), true);
    PinObj->SetStringField(TEXT("defaultValue"), DefaultValue);
    PinObj->SetStringField(TEXT("defaultObject"), TEXT(""));
    PinObj->SetStringField(TEXT("defaultText"), EffectiveDefaultText);

    TSharedPtr<FJsonObject> PinTypeObject = MakeShared<FJsonObject>();
    PinTypeObject->SetStringField(TEXT("category"), TEXT("pcg"));
    PinTypeObject->SetStringField(TEXT("subCategory"), TEXT("setting"));
    PinTypeObject->SetStringField(TEXT("object"), TEXT(""));
    PinTypeObject->SetStringField(TEXT("container"), TEXT("none"));
    PinObj->SetObjectField(TEXT("type"), PinTypeObject);

    TSharedPtr<FJsonObject> PinDefaultObject = MakeShared<FJsonObject>();
    PinDefaultObject->SetStringField(TEXT("value"), DefaultValue);
    PinDefaultObject->SetStringField(TEXT("object"), TEXT(""));
    PinDefaultObject->SetStringField(TEXT("text"), EffectiveDefaultText);
    PinObj->SetObjectField(TEXT("default"), PinDefaultObject);
    PinObj->SetArrayField(TEXT("links"), TArray<TSharedPtr<FJsonValue>>{});
    PinObj->SetArrayField(TEXT("linkedTo"), TArray<TSharedPtr<FJsonValue>>{});
    Pins.Add(MakeShared<FJsonValueObject>(PinObj));
}

FString MakePcgConstantValueString(const FPCGMetadataTypesConstantStruct& ConstantValue, FString& OutLeafPropertyName)
{
    OutLeafPropertyName.Empty();

    switch (ConstantValue.Type)
    {
    case EPCGMetadataTypes::Float:
        OutLeafPropertyName = TEXT("FloatValue");
        return FString::SanitizeFloat(ConstantValue.FloatValue);
    case EPCGMetadataTypes::Integer32:
        OutLeafPropertyName = TEXT("Int32Value");
        return FString::FromInt(ConstantValue.Int32Value);
    case EPCGMetadataTypes::Double:
        OutLeafPropertyName = TEXT("DoubleValue");
        return FString::SanitizeFloat(ConstantValue.DoubleValue);
    case EPCGMetadataTypes::Integer64:
        OutLeafPropertyName = TEXT("IntValue");
        return LexToString(ConstantValue.IntValue);
    case EPCGMetadataTypes::String:
        OutLeafPropertyName = TEXT("StringValue");
        return ConstantValue.StringValue;
    case EPCGMetadataTypes::Boolean:
        OutLeafPropertyName = TEXT("BoolValue");
        return ConstantValue.BoolValue ? TEXT("true") : TEXT("false");
    case EPCGMetadataTypes::Name:
        OutLeafPropertyName = TEXT("NameValue");
        return ConstantValue.NameValue.ToString();
    case EPCGMetadataTypes::SoftObjectPath:
        OutLeafPropertyName = TEXT("SoftObjectPathValue");
        return ConstantValue.SoftObjectPathValue.ToString();
    case EPCGMetadataTypes::SoftClassPath:
        OutLeafPropertyName = TEXT("SoftClassPathValue");
        return ConstantValue.SoftClassPathValue.ToString();
    default:
        break;
    }

    return ConstantValue.ToString();
}

void AppendPcgConstantStructPins(
    TArray<TSharedPtr<FJsonValue>>& Pins,
    const FString& Prefix,
    const FPCGMetadataTypesConstantStruct& ConstantValue)
{
    if (Prefix.IsEmpty())
    {
        return;
    }

    AppendPcgSyntheticSettingPin(
        Pins,
        Prefix + TEXT("/Type"),
        GetPcgEnumName(ConstantValue.Type));

    FString LeafPropertyName;
    const FString LeafValue = MakePcgConstantValueString(ConstantValue, LeafPropertyName);
    if (!LeafPropertyName.IsEmpty())
    {
        AppendPcgSyntheticSettingPin(Pins, Prefix + TEXT("/") + LeafPropertyName, LeafValue);
    }
}

void AppendPcgSyntheticWritablePins(UPCGSettings* Settings, TArray<TSharedPtr<FJsonValue>>& Pins)
{
    if (Settings == nullptr)
    {
        return;
    }

    if (const UPCGFilterByAttributeSettings* FilterByAttributeSettings = Cast<UPCGFilterByAttributeSettings>(Settings))
    {
        AppendPcgConstantStructPins(Pins, TEXT("Threshold/AttributeTypes"), FilterByAttributeSettings->Threshold.AttributeTypes);
        AppendPcgConstantStructPins(Pins, TEXT("MinThreshold/AttributeTypes"), FilterByAttributeSettings->MinThreshold.AttributeTypes);
        AppendPcgConstantStructPins(Pins, TEXT("MaxThreshold/AttributeTypes"), FilterByAttributeSettings->MaxThreshold.AttributeTypes);
    }
    else if (const UPCGAttributeFilteringSettings* AttributeFilteringSettings = Cast<UPCGAttributeFilteringSettings>(Settings))
    {
        AppendPcgConstantStructPins(Pins, TEXT("AttributeTypes"), AttributeFilteringSettings->AttributeTypes);
    }
    else if (const UPCGAttributeFilteringRangeSettings* AttributeFilteringRangeSettings = Cast<UPCGAttributeFilteringRangeSettings>(Settings))
    {
        AppendPcgConstantStructPins(Pins, TEXT("MinThreshold/AttributeTypes"), AttributeFilteringRangeSettings->MinThreshold.AttributeTypes);
        AppendPcgConstantStructPins(Pins, TEXT("MaxThreshold/AttributeTypes"), AttributeFilteringRangeSettings->MaxThreshold.AttributeTypes);
    }

    TArray<FPcgQuerySyntheticTarget> SyntheticTargets;
    GatherPcgSyntheticPropertyTargetsForQuery(Settings, SyntheticTargets);
    for (const FPcgQuerySyntheticTarget& SyntheticTarget : SyntheticTargets)
    {
        AppendPcgSyntheticSettingPin(
            Pins,
            SyntheticTarget.PinName,
            SyntheticTarget.DefaultValue,
            SyntheticTarget.DefaultText);
    }
}

TSharedPtr<FJsonObject> MakePcgActorSelectorObject(const FPCGActorSelectorSettings& Selector)
{
    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetStringField(TEXT("actorFilter"), GetPcgEnumName(Selector.ActorFilter));
    Result->SetBoolField(TEXT("mustOverlapSelf"), Selector.bMustOverlapSelf);
    Result->SetBoolField(TEXT("includeChildren"), Selector.bIncludeChildren);
    Result->SetBoolField(TEXT("disableFilter"), Selector.bDisableFilter);
    Result->SetStringField(TEXT("actorSelection"), GetPcgEnumName(Selector.ActorSelection));
    Result->SetStringField(TEXT("actorSelectionTag"), Selector.ActorSelectionTag.ToString());
    Result->SetStringField(
        TEXT("actorSelectionClassPath"),
        Selector.ActorSelectionClass ? Selector.ActorSelectionClass->GetPathName() : TEXT(""));
    Result->SetObjectField(TEXT("actorReferenceSelector"), MakePcgSelectorObject(Selector.ActorReferenceSelector));
    Result->SetBoolField(TEXT("selectMultiple"), Selector.bSelectMultiple);
    Result->SetBoolField(TEXT("ignoreSelfAndChildren"), Selector.bIgnoreSelfAndChildren);
    return Result;
}

TSharedPtr<FJsonObject> MakePcgComponentSelectorObject(const FPCGComponentSelectorSettings& Selector)
{
    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetStringField(TEXT("componentSelection"), GetPcgEnumName(Selector.ComponentSelection));
    Result->SetStringField(TEXT("componentSelectionTag"), Selector.ComponentSelectionTag.ToString());
    Result->SetStringField(
        TEXT("componentSelectionClassPath"),
        Selector.ComponentSelectionClass ? Selector.ComponentSelectionClass->GetPathName() : TEXT(""));
    return Result;
}

void AppendPcgGraphQueryDiagnostic(
    TArray<TSharedPtr<FJsonValue>>& NodeDiagnostics,
    TArray<TSharedPtr<FJsonValue>>& RootDiagnostics,
    const FString& NodeId,
    const FString& Code,
    const FString& Message,
    const FString& Severity = TEXT("info"))
{
    TSharedPtr<FJsonObject> NodeDiagnostic = MakeShared<FJsonObject>();
    NodeDiagnostic->SetStringField(TEXT("code"), Code);
    NodeDiagnostic->SetStringField(TEXT("message"), Message);
    NodeDiagnostic->SetStringField(TEXT("severity"), Severity);
    NodeDiagnostic->SetStringField(TEXT("nodeId"), NodeId);
    NodeDiagnostics.Add(MakeShared<FJsonValueObject>(NodeDiagnostic));
    RootDiagnostics.Add(MakeShared<FJsonValueObject>(NodeDiagnostic));
}

void AppendPcgGraphRootDiagnostic(
    TArray<TSharedPtr<FJsonValue>>& RootDiagnostics,
    const FString& Code,
    const FString& Message,
    const FString& Severity = TEXT("warning"),
    const TArray<FString>* NodeIds = nullptr)
{
    TSharedPtr<FJsonObject> Diagnostic = MakeShared<FJsonObject>();
    Diagnostic->SetStringField(TEXT("code"), Code);
    Diagnostic->SetStringField(TEXT("message"), Message);
    Diagnostic->SetStringField(TEXT("severity"), Severity);
    if (NodeIds != nullptr && NodeIds->Num() > 0)
    {
        TArray<TSharedPtr<FJsonValue>> NodeIdValues;
        for (const FString& NodeId : *NodeIds)
        {
            NodeIdValues.Add(MakeShared<FJsonValueString>(NodeId));
        }
        Diagnostic->SetArrayField(TEXT("nodeIds"), NodeIdValues);
    }
    RootDiagnostics.Add(MakeShared<FJsonValueObject>(Diagnostic));
}

TSharedPtr<FJsonObject> BuildPcgNodeSettingsObject(
    UPCGNode* NodeObj,
    TArray<TSharedPtr<FJsonValue>>& NodeDiagnostics,
    TArray<TSharedPtr<FJsonValue>>& RootDiagnostics)
{
    if (NodeObj == nullptr)
    {
        return nullptr;
    }

    UPCGSettings* NodeSettings = NodeObj->GetSettings();
    if (NodeSettings == nullptr)
    {
        return nullptr;
    }

    const FString NodeId = NodeObj->GetPathName();

    if (const UPCGGetActorPropertySettings* GetActorPropertySettings = Cast<UPCGGetActorPropertySettings>(NodeSettings))
    {
        TSharedPtr<FJsonObject> Settings = MakeShared<FJsonObject>();
        Settings->SetStringField(TEXT("settingsClassPath"), GetActorPropertySettings->GetClass()->GetPathName());
        Settings->SetObjectField(TEXT("actorSelector"), MakePcgActorSelectorObject(GetActorPropertySettings->ActorSelector));
        Settings->SetBoolField(TEXT("selectComponent"), GetActorPropertySettings->bSelectComponent);
        Settings->SetStringField(
            TEXT("componentClassPath"),
            GetActorPropertySettings->ComponentClass ? GetActorPropertySettings->ComponentClass->GetPathName() : TEXT(""));
        Settings->SetBoolField(TEXT("processAllComponents"), GetActorPropertySettings->bProcessAllComponents);
        Settings->SetBoolField(TEXT("outputComponentReference"), GetActorPropertySettings->bOutputComponentReference);
        Settings->SetStringField(TEXT("propertyName"), GetActorPropertySettings->PropertyName.ToString());
        Settings->SetBoolField(TEXT("forceObjectAndStructExtraction"), GetActorPropertySettings->bForceObjectAndStructExtraction);
        Settings->SetObjectField(TEXT("outputAttributeName"), MakePcgSelectorObject(GetActorPropertySettings->OutputAttributeName));
        Settings->SetBoolField(TEXT("sanitizeOutputAttributeName"), GetActorPropertySettings->bSanitizeOutputAttributeName);
        Settings->SetBoolField(TEXT("outputActorReference"), GetActorPropertySettings->bOutputActorReference);
        Settings->SetBoolField(TEXT("alwaysRequeryActors"), GetActorPropertySettings->bAlwaysRequeryActors);

        AppendPcgGraphQueryDiagnostic(
            NodeDiagnostics,
            RootDiagnostics,
            NodeId,
            TEXT("PCG_SELECTOR_EMPTY_INPUT_HINT"),
            TEXT("If no actors match actorSelector, this node yields no output data."));
        if (GetActorPropertySettings->bSelectComponent)
        {
            AppendPcgGraphQueryDiagnostic(
                NodeDiagnostics,
                RootDiagnostics,
                NodeId,
                TEXT("PCG_COMPONENT_SELECTOR_EMPTY_INPUT_HINT"),
                TEXT("If no attached components match componentClass, this node yields no output data."));
        }

        return Settings;
    }

    if (const UPCGGetSplineSettings* GetSplineSettings = Cast<UPCGGetSplineSettings>(NodeSettings))
    {
        TSharedPtr<FJsonObject> Settings = MakeShared<FJsonObject>();
        Settings->SetStringField(TEXT("settingsClassPath"), GetSplineSettings->GetClass()->GetPathName());
        Settings->SetStringField(TEXT("dataFilter"), TEXT("PolyLine"));
        Settings->SetStringField(TEXT("mode"), GetPcgEnumName(GetSplineSettings->Mode));
        Settings->SetObjectField(TEXT("actorSelector"), MakePcgActorSelectorObject(GetSplineSettings->ActorSelector));
        Settings->SetObjectField(TEXT("componentSelector"), MakePcgComponentSelectorObject(GetSplineSettings->ComponentSelector));
        Settings->SetBoolField(TEXT("ignorePCGGeneratedComponents"), GetSplineSettings->bIgnorePCGGeneratedComponents);
        Settings->SetBoolField(TEXT("alwaysRequeryActors"), GetSplineSettings->bAlwaysRequeryActors);

        AppendPcgGraphQueryDiagnostic(
            NodeDiagnostics,
            RootDiagnostics,
            NodeId,
            TEXT("PCG_SELECTOR_EMPTY_INPUT_HINT"),
            TEXT("If no actors match actorSelector, this node yields no spline data."));
        AppendPcgGraphQueryDiagnostic(
            NodeDiagnostics,
            RootDiagnostics,
            NodeId,
            TEXT("PCG_COMPONENT_SELECTOR_EMPTY_INPUT_HINT"),
            TEXT("If no spline components match componentSelector, this node yields no spline data."));

        return Settings;
    }

    if (const UPCGStaticMeshSpawnerSettings* StaticMeshSpawnerSettings = Cast<UPCGStaticMeshSpawnerSettings>(NodeSettings))
    {
        TSharedPtr<FJsonObject> Settings = MakeShared<FJsonObject>();
        Settings->SetStringField(TEXT("settingsClassPath"), StaticMeshSpawnerSettings->GetClass()->GetPathName());
        Settings->SetStringField(
            TEXT("meshSelectorTypeClassPath"),
            StaticMeshSpawnerSettings->MeshSelectorType ? StaticMeshSpawnerSettings->MeshSelectorType->GetPathName() : TEXT(""));
        Settings->SetStringField(
            TEXT("meshSelectorParametersClassPath"),
            StaticMeshSpawnerSettings->MeshSelectorParameters ? StaticMeshSpawnerSettings->MeshSelectorParameters->GetClass()->GetPathName() : TEXT(""));
        Settings->SetBoolField(TEXT("allowDescriptorChanges"), StaticMeshSpawnerSettings->bAllowDescriptorChanges);
        Settings->SetStringField(TEXT("outAttributeName"), StaticMeshSpawnerSettings->OutAttributeName.ToString());
        Settings->SetBoolField(TEXT("applyMeshBoundsToPoints"), StaticMeshSpawnerSettings->bApplyMeshBoundsToPoints);
        Settings->SetBoolField(TEXT("synchronousLoad"), StaticMeshSpawnerSettings->bSynchronousLoad);
        Settings->SetBoolField(TEXT("allowMergeDifferentDataInSameInstancedComponents"), StaticMeshSpawnerSettings->bAllowMergeDifferentDataInSameInstancedComponents);
        Settings->SetBoolField(TEXT("silenceOverrideAttributeNotFoundErrors"), StaticMeshSpawnerSettings->bSilenceOverrideAttributeNotFoundErrors);
        Settings->SetBoolField(TEXT("warnOnIdenticalSpawn"), StaticMeshSpawnerSettings->bWarnOnIdenticalSpawn);

        if (const UPCGMeshSelectorByAttribute* ByAttributeSelector = Cast<UPCGMeshSelectorByAttribute>(StaticMeshSpawnerSettings->MeshSelectorParameters))
        {
            TSharedPtr<FJsonObject> MeshSelector = MakeShared<FJsonObject>();
            MeshSelector->SetStringField(TEXT("kind"), TEXT("byAttribute"));
            MeshSelector->SetStringField(TEXT("attributeName"), ByAttributeSelector->AttributeName.ToString());
            MeshSelector->SetBoolField(TEXT("useAttributeMaterialOverrides"), ByAttributeSelector->bUseAttributeMaterialOverrides);
            TArray<TSharedPtr<FJsonValue>> OverrideAttributes;
            for (const FName& AttributeName : ByAttributeSelector->MaterialOverrideAttributes)
            {
                OverrideAttributes.Add(MakeShared<FJsonValueString>(AttributeName.ToString()));
            }
            MeshSelector->SetArrayField(TEXT("materialOverrideAttributes"), OverrideAttributes);
            Settings->SetObjectField(TEXT("meshSelector"), MeshSelector);

            if (ByAttributeSelector->AttributeName.IsNone())
            {
                AppendPcgGraphQueryDiagnostic(
                    NodeDiagnostics,
                    RootDiagnostics,
                    NodeId,
                    TEXT("PCG_EMPTY_INPUT_HINT"),
                    TEXT("Static Mesh Spawner uses attribute-based mesh selection but no attributeName is configured."),
                    TEXT("warning"));
            }
        }
        else if (const UPCGMeshSelectorWeighted* WeightedSelector = Cast<UPCGMeshSelectorWeighted>(StaticMeshSpawnerSettings->MeshSelectorParameters))
        {
            TSharedPtr<FJsonObject> MeshSelector = MakeShared<FJsonObject>();
            MeshSelector->SetStringField(TEXT("kind"), TEXT("weighted"));
            MeshSelector->SetBoolField(TEXT("useAttributeMaterialOverrides"), WeightedSelector->bUseAttributeMaterialOverrides);

            TArray<TSharedPtr<FJsonValue>> Entries;
            for (const FPCGMeshSelectorWeightedEntry& Entry : WeightedSelector->MeshEntries)
            {
                TSharedPtr<FJsonObject> EntryObject = MakeShared<FJsonObject>();
                EntryObject->SetNumberField(TEXT("weight"), Entry.Weight);
                EntryObject->SetStringField(TEXT("meshPath"), Entry.Descriptor.StaticMesh.ToSoftObjectPath().GetAssetPathString());
                Entries.Add(MakeShared<FJsonValueObject>(EntryObject));
            }
            MeshSelector->SetArrayField(TEXT("meshEntries"), Entries);
            Settings->SetObjectField(TEXT("meshSelector"), MeshSelector);

            if (WeightedSelector->MeshEntries.Num() == 0)
            {
                AppendPcgGraphQueryDiagnostic(
                    NodeDiagnostics,
                    RootDiagnostics,
                    NodeId,
                    TEXT("PCG_EMPTY_INPUT_HINT"),
                    TEXT("Static Mesh Spawner has no weighted mesh entries configured."),
                    TEXT("warning"));
            }
        }
        else if (const UPCGMeshSelectorWeightedByCategory* WeightedByCategorySelector = Cast<UPCGMeshSelectorWeightedByCategory>(StaticMeshSpawnerSettings->MeshSelectorParameters))
        {
            TSharedPtr<FJsonObject> MeshSelector = MakeShared<FJsonObject>();
            MeshSelector->SetStringField(TEXT("kind"), TEXT("weightedByCategory"));
            MeshSelector->SetStringField(TEXT("categoryAttribute"), WeightedByCategorySelector->CategoryAttribute.ToString());

            TArray<TSharedPtr<FJsonValue>> Entries;
            for (const FPCGWeightedByCategoryEntryList& Entry : WeightedByCategorySelector->Entries)
            {
                TSharedPtr<FJsonObject> EntryObject = MakeShared<FJsonObject>();
                EntryObject->SetStringField(TEXT("categoryEntry"), Entry.CategoryEntry);
                EntryObject->SetBoolField(TEXT("isDefault"), Entry.IsDefault);
                EntryObject->SetNumberField(TEXT("meshEntryCount"), Entry.WeightedMeshEntries.Num());
                Entries.Add(MakeShared<FJsonValueObject>(EntryObject));
            }
            MeshSelector->SetArrayField(TEXT("entries"), Entries);
            Settings->SetObjectField(TEXT("meshSelector"), MeshSelector);

            if (WeightedByCategorySelector->Entries.Num() == 0)
            {
                AppendPcgGraphQueryDiagnostic(
                    NodeDiagnostics,
                    RootDiagnostics,
                    NodeId,
                    TEXT("PCG_EMPTY_INPUT_HINT"),
                    TEXT("Static Mesh Spawner has no weighted category entries configured."),
                    TEXT("warning"));
            }
        }

        return Settings;
    }

    return nullptr;
}

bool TryReadPinEndpoint(const TSharedPtr<FJsonObject>& EndpointObj, FString& OutNodeId, FString& OutPinName)
{
    if (!EndpointObj.IsValid())
    {
        return false;
    }

    EndpointObj->TryGetStringField(TEXT("nodeId"), OutNodeId);
    EndpointObj->TryGetStringField(TEXT("pinName"), OutPinName);
    return !OutNodeId.IsEmpty() && !OutPinName.IsEmpty();
}

const TCHAR* GetDefaultPcgInputPinName(const FString& OpId)
{
    if (OpId.Equals(TEXT("pcg.meta.add_tag"))
        || OpId.Equals(TEXT("pcg.filter.elements_compare"))
        || OpId.Equals(TEXT("pcg.filter.elements_in_range"))
        || OpId.Equals(TEXT("pcg.filter.points_by_density"))
        || OpId.Equals(TEXT("pcg.route.data_by_tag"))
        || OpId.Equals(TEXT("pcg.route.data_if_attribute_exists"))
        || OpId.Equals(TEXT("pcg.route.data_if_attribute_value"))
        || OpId.Equals(TEXT("pcg.sample.points_ratio"))
        || OpId.Equals(TEXT("pcg.route.data_if_attribute_in_range")))
    {
        return TEXT("In");
    }

    return TEXT("");
}

const TCHAR* GetDefaultPcgOutputPinName(const FString& OpId)
{
    if (OpId.Equals(TEXT("pcg.filter.elements_compare"))
        || OpId.Equals(TEXT("pcg.filter.elements_in_range"))
        || OpId.Equals(TEXT("pcg.route.data_by_tag"))
        || OpId.Equals(TEXT("pcg.route.data_if_attribute_exists"))
        || OpId.Equals(TEXT("pcg.route.data_if_attribute_value"))
        || OpId.Equals(TEXT("pcg.route.data_if_attribute_in_range")))
    {
        return TEXT("InsideFilter");
    }
    if (OpId.Equals(TEXT("pcg.meta.add_tag")))
    {
        return TEXT("Out");
    }
    if (OpId.Equals(TEXT("pcg.filter.points_by_density"))
        || OpId.Equals(TEXT("pcg.sample.points_ratio")))
    {
        return TEXT("Out");
    }

    return TEXT("");
}

bool IsComposablePcgSemanticOp(const FString& OpId)
{
    return OpId.Equals(TEXT("pcg.meta.add_tag"))
        || OpId.Equals(TEXT("pcg.filter.elements_compare"))
        || OpId.Equals(TEXT("pcg.filter.elements_in_range"))
        || OpId.Equals(TEXT("pcg.filter.points_by_density"))
        || OpId.Equals(TEXT("pcg.route.data_by_tag"))
        || OpId.Equals(TEXT("pcg.route.data_if_attribute_exists"))
        || OpId.Equals(TEXT("pcg.route.data_if_attribute_value"))
        || OpId.Equals(TEXT("pcg.sample.points_ratio"))
        || OpId.Equals(TEXT("pcg.route.data_if_attribute_in_range"));
}

FString ResolvePlanStepNodeRef(const FString& ClientRef, int32 Index)
{
    return !ClientRef.IsEmpty()
        ? ClientRef
        : FString::Printf(TEXT("resolved_%d"), Index);
}
}

TSharedPtr<FJsonObject> FLoomleBridgeModule::BuildGraphListToolResult(const TSharedPtr<FJsonObject>& Arguments) const
{
    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetBoolField(TEXT("isError"), false);

    FString AssetPath;
    if (!Arguments->TryGetStringField(TEXT("assetPath"), AssetPath) || AssetPath.IsEmpty())
    {
        Result->SetBoolField(TEXT("isError"), true);
        Result->SetStringField(TEXT("code"), TEXT("INVALID_ARGUMENT"));
        Result->SetStringField(TEXT("message"), TEXT("arguments.assetPath is required."));
        return Result;
    }
    AssetPath = NormalizeAssetPath(AssetPath);

    FString GraphType;
    const bool bHasExplicitGraphType =
        Arguments.IsValid() && Arguments->TryGetStringField(TEXT("graphType"), GraphType) && !GraphType.IsEmpty();
    if (bHasExplicitGraphType)
    {
        GraphType = NormalizeGraphType(GraphType);
        if (!IsSupportedGraphType(GraphType))
        {
            Result->SetBoolField(TEXT("isError"), true);
            Result->SetStringField(TEXT("code"), TEXT("UNSUPPORTED_GRAPH_TYPE"));
            Result->SetStringField(TEXT("message"), TEXT("Supported graphType values: blueprint, material, pcg."));
            return Result;
        }
    }
    else
    {
        UObject* AssetObject = LoadObjectByAssetPath(AssetPath);
        if (AssetObject != nullptr
            && (AssetObject->IsA<UMaterial>() || AssetObject->IsA<UMaterialFunction>()))
        {
            GraphType = TEXT("material");
        }
        else if (IsLikelyPcgAsset(AssetObject))
        {
            GraphType = TEXT("pcg");
        }
        else
        {
            GraphType = TEXT("blueprint");
        }
    }

    bool bIncludeSubgraphs = false;
    Arguments->TryGetBoolField(TEXT("includeSubgraphs"), bIncludeSubgraphs);

    int32 MaxDepth = 1;
    double MaxDepthNumber = 1.0;
    if (Arguments->TryGetNumberField(TEXT("maxDepth"), MaxDepthNumber))
    {
        MaxDepth = FMath::Clamp(static_cast<int32>(MaxDepthNumber), 0, 8);
    }

    if (GraphType.Equals(TEXT("material")))
    {
        UMaterial* Material = LoadMaterialByAssetPath(AssetPath);
        UMaterialFunction* MaterialFunction = nullptr;
        if (Material == nullptr)
        {
            MaterialFunction = Cast<UMaterialFunction>(LoadObjectByAssetPath(AssetPath));
        }
        if (Material == nullptr && MaterialFunction == nullptr)
        {
            Result->SetBoolField(TEXT("isError"), true);
            Result->SetStringField(TEXT("code"), TEXT("ASSET_NOT_FOUND"));
            Result->SetStringField(TEXT("message"), TEXT("Material asset not found."));
            return Result;
        }

        TArray<TSharedPtr<FJsonValue>> Graphs;
        Graphs.Add(MakeShared<FJsonValueObject>(MakeGraphCatalogEntry(
            TEXT("MaterialGraph"),
            TEXT("root"),
            TEXT("/Script/Engine.MaterialGraph"),
            MakeGraphAssetRef(AssetPath, TEXT("")),
            nullptr,
            TEXT(""),
            TEXT("loaded"))));

        if (bIncludeSubgraphs && MaxDepth > 0)
        {
            TArray<UMaterialExpression*> Expressions;
            if (Material != nullptr)
            {
                for (UMaterialExpression* Expression : Material->GetExpressions())
                {
                    if (Expression != nullptr)
                    {
                        Expressions.Add(Expression);
                    }
                }
            }
            else if (MaterialFunction != nullptr)
            {
                for (const TObjectPtr<UMaterialExpression>& Expression : MaterialFunction->GetExpressions())
                {
                    if (Expression != nullptr)
                    {
                        Expressions.Add(Expression.Get());
                    }
                }
            }

            if (Expressions.Num() > 0)
            {
                TSharedPtr<FJsonObject> ParentRef = MakeGraphAssetRef(AssetPath, TEXT(""));
                for (UMaterialExpression* Expression : Expressions)
                {
                    UMaterialExpressionMaterialFunctionCall* FuncCall = Cast<UMaterialExpressionMaterialFunctionCall>(Expression);
                    if (!FuncCall || !FuncCall->MaterialFunction) { continue; }

                    FString FuncAssetPath;
                    if (!ResolveMaterialFunctionAssetPath(FuncCall->MaterialFunction, FuncAssetPath))
                    {
                        continue;
                    }

                    Graphs.Add(MakeShared<FJsonValueObject>(MakeGraphCatalogEntry(
                        FuncCall->MaterialFunction->GetName(),
                        TEXT("subgraph"),
                        FuncCall->MaterialFunction->GetClass() ? FuncCall->MaterialFunction->GetClass()->GetPathName() : TEXT(""),
                        MakeGraphAssetRef(FuncAssetPath, TEXT("")),
                        ParentRef,
                        MaterialExpressionId(Expression),
                        TEXT("loaded"))));
                }
            }
        }

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
        Graphs.Add(MakeShared<FJsonValueObject>(MakeGraphCatalogEntry(
            TEXT("PCGGraph"),
            TEXT("root"),
            Asset->GetClass() ? Asset->GetClass()->GetPathName() : TEXT(""),
            MakeGraphAssetRef(AssetPath, TEXT("")),
            nullptr,
            TEXT(""),
            TEXT("loaded"))));

        if (bIncludeSubgraphs && MaxDepth > 0)
        {
            UPCGGraph* PcgGraph = LoadPcgGraphByAssetPath(AssetPath);
            if (PcgGraph)
            {
                TSharedPtr<FJsonObject> ParentRef = MakeGraphAssetRef(AssetPath, TEXT(""));
                for (UPCGNode* NodeObj : PcgGraph->GetNodes())
                {
                    if (NodeObj == nullptr) { continue; }
                    UPCGSettings* NodeSettings = NodeObj->GetSettings();
                    if (!NodeSettings || !NodeSettings->GetClass()) { continue; }
                    const FString NodeClassPath = NodeSettings->GetClass()->GetPathName();
                    if (!NodeClassPath.Contains(TEXT("Subgraph"))) { continue; }

                    FString SubAssetPath;
                    FString SubLoadStatus;
                    FString SubgraphName;
                    FString SubgraphClassPath;
                    if (!ResolvePcgSubgraphAssetReference(NodeSettings, SubAssetPath, SubLoadStatus, SubgraphName, SubgraphClassPath))
                    {
                        continue;
                    }

                    Graphs.Add(MakeShared<FJsonValueObject>(MakeGraphCatalogEntry(
                        SubgraphName,
                        TEXT("subgraph"),
                        SubgraphClassPath,
                        MakeGraphAssetRef(SubAssetPath, TEXT("")),
                        ParentRef,
                        NodeObj->GetPathName(),
                        SubLoadStatus)));
                }
            }
        }

        Result->SetStringField(TEXT("graphType"), GraphType);
        Result->SetStringField(TEXT("assetPath"), AssetPath);
        Result->SetArrayField(TEXT("graphs"), Graphs);
        Result->SetArrayField(TEXT("diagnostics"), TArray<TSharedPtr<FJsonValue>>{});
        return Result;
    }

    // Blueprint
    FString GraphsJson;
    FString Error;
    if (!FLoomleBlueprintAdapter::ListBlueprintGraphs(AssetPath, GraphsJson, Error))
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

    // Annotate each root graph entry with graphRef and metadata.
    for (TSharedPtr<FJsonValue>& GraphValue : Graphs)
    {
        const TSharedPtr<FJsonObject>* GraphObj = nullptr;
        if (!GraphValue.IsValid() || !GraphValue->TryGetObject(GraphObj) || !GraphObj || !(*GraphObj).IsValid())
        {
            continue;
        }

        FString GraphName;
        (*GraphObj)->TryGetStringField(TEXT("graphName"), GraphName);

        // Normalize graphKind to lowercase enum values.
        FString GraphKind;
        (*GraphObj)->TryGetStringField(TEXT("graphKind"), GraphKind);
        GraphKind = NormalizeBlueprintGraphKind(GraphKind);
        (*GraphObj)->SetStringField(TEXT("graphKind"), GraphKind);

        (*GraphObj)->SetObjectField(TEXT("graphRef"), MakeGraphAssetRef(AssetPath, GraphName));
        (*GraphObj)->SetField(TEXT("parentGraphRef"), MakeShared<FJsonValueNull>());
        (*GraphObj)->SetField(TEXT("ownerNodeId"), MakeShared<FJsonValueNull>());
        (*GraphObj)->SetStringField(TEXT("loadStatus"), TEXT("loaded"));
    }

    // When includeSubgraphs is requested, enumerate K2Node_Composite nodes in each root graph.
    if (bIncludeSubgraphs && MaxDepth > 0)
    {
        FString SubgraphJson;
        FString SubgraphError;
        if (FLoomleBlueprintAdapter::ListCompositeSubgraphs(AssetPath, SubgraphJson, SubgraphError))
        {
            TArray<TSharedPtr<FJsonValue>> SubgraphEntries;
            const TSharedRef<TJsonReader<>> SubgraphReader = TJsonReaderFactory<>::Create(SubgraphJson);
            if (FJsonSerializer::Deserialize(SubgraphReader, SubgraphEntries))
            {
                for (const TSharedPtr<FJsonValue>& SubgraphValue : SubgraphEntries)
                {
                    const TSharedPtr<FJsonObject>* SubgraphObj = nullptr;
                    if (!SubgraphValue.IsValid()
                        || !SubgraphValue->TryGetObject(SubgraphObj)
                        || SubgraphObj == nullptr
                        || !(*SubgraphObj).IsValid())
                    {
                        continue;
                    }

                    FString ParentGraphName;
                    (*SubgraphObj)->TryGetStringField(TEXT("parentGraphName"), ParentGraphName);

                    FString OwnerNodeId;
                    (*SubgraphObj)->TryGetStringField(TEXT("ownerNodeId"), OwnerNodeId);
                    if (OwnerNodeId.IsEmpty())
                    {
                        continue;
                    }

                    TSharedPtr<FJsonObject> SubEntry = CloneJsonObject(*SubgraphObj);
                    if (!SubEntry.IsValid())
                    {
                        continue;
                    }

                    SubEntry->SetObjectField(TEXT("graphRef"), MakeGraphInlineRef(OwnerNodeId, AssetPath));
                    SubEntry->SetObjectField(TEXT("parentGraphRef"), MakeGraphAssetRef(AssetPath, ParentGraphName));
                    SubEntry->SetStringField(TEXT("loadStatus"), TEXT("loaded"));
                    Graphs.Add(MakeShared<FJsonValueObject>(SubEntry));
                }
            }
        }
    }

    Result->SetStringField(TEXT("graphType"), GraphType);
    Result->SetStringField(TEXT("assetPath"), AssetPath);
    Result->SetArrayField(TEXT("graphs"), Graphs);
    Result->SetArrayField(TEXT("diagnostics"), TArray<TSharedPtr<FJsonValue>>{});
    return Result;
}

TSharedPtr<FJsonObject> FLoomleBridgeModule::BuildGraphResolveToolResult(const TSharedPtr<FJsonObject>& Arguments) const
{
    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetBoolField(TEXT("isError"), false);

    FString GraphTypeFilter;
    if (Arguments.IsValid())
    {
        Arguments->TryGetStringField(TEXT("graphType"), GraphTypeFilter);
    }
    GraphTypeFilter = GraphTypeFilter.TrimStartAndEnd().ToLower();
    if (!GraphTypeFilter.IsEmpty() && !IsSupportedGraphType(GraphTypeFilter))
    {
        Result->SetBoolField(TEXT("isError"), true);
        Result->SetStringField(TEXT("code"), TEXT("UNSUPPORTED_GRAPH_TYPE"));
        Result->SetStringField(TEXT("message"), TEXT("Supported graphType values: blueprint, material, pcg."));
        return Result;
    }

    FString InputPath;
    FString InputKind;
    if (Arguments.IsValid())
    {
        if (Arguments->TryGetStringField(TEXT("path"), InputPath) && !InputPath.IsEmpty())
        {
            InputKind = TEXT("path");
        }
        else if (Arguments->TryGetStringField(TEXT("objectPath"), InputPath) && !InputPath.IsEmpty())
        {
            InputKind = TEXT("objectPath");
        }
        else if (Arguments->TryGetStringField(TEXT("componentPath"), InputPath) && !InputPath.IsEmpty())
        {
            InputKind = TEXT("componentPath");
        }
        else if (Arguments->TryGetStringField(TEXT("actorPath"), InputPath) && !InputPath.IsEmpty())
        {
            InputKind = TEXT("actorPath");
        }
        else if (Arguments->TryGetStringField(TEXT("assetPath"), InputPath) && !InputPath.IsEmpty())
        {
            InputKind = TEXT("assetPath");
        }
    }

    if (InputPath.IsEmpty())
    {
        Result->SetBoolField(TEXT("isError"), true);
        Result->SetStringField(TEXT("code"), TEXT("INVALID_ARGUMENT"));
        Result->SetStringField(TEXT("message"), TEXT("Supply one of path, objectPath, componentPath, actorPath, or assetPath."));
        return Result;
    }

    UObject* TargetObject = nullptr;
    if (InputKind.Equals(TEXT("assetPath")))
    {
        TargetObject = LoadObjectByAssetPath(InputPath);
        if (TargetObject == nullptr)
        {
            Result->SetBoolField(TEXT("isError"), true);
            Result->SetStringField(TEXT("code"), TEXT("ASSET_NOT_FOUND"));
            Result->SetStringField(TEXT("message"), TEXT("Failed to resolve assetPath."));
            return Result;
        }
    }
    else
    {
        TargetObject = ResolveRuntimeObjectFromPath(InputPath);
        if (TargetObject == nullptr)
        {
            Result->SetBoolField(TEXT("isError"), true);
            Result->SetStringField(TEXT("code"), TEXT("OBJECT_NOT_FOUND"));
            Result->SetStringField(TEXT("message"), TEXT("Failed to resolve object path."));
            return Result;
        }
    }

    TArray<TSharedPtr<FJsonValue>> ResolvedGraphRefs;
    TSet<FString> SeenGraphRefs;
    AppendResolvedGraphRefsFromObject(TargetObject, ResolvedGraphRefs, SeenGraphRefs);

    TArray<TSharedPtr<FJsonValue>> FilteredGraphRefs;
    FilterResolvedGraphRefsByType(ResolvedGraphRefs, GraphTypeFilter, FilteredGraphRefs);

    TArray<TSharedPtr<FJsonValue>> Diagnostics;
    if (FilteredGraphRefs.Num() == 0)
    {
        TSharedPtr<FJsonObject> Diagnostic = MakeShared<FJsonObject>();
        Diagnostic->SetStringField(TEXT("code"), TEXT("GRAPH_RESOLVE_EMPTY"));
        Diagnostic->SetStringField(TEXT("message"), TEXT("No resolvable graph references were found for the supplied target."));
        Diagnostics.Add(MakeShared<FJsonValueObject>(Diagnostic));
    }

    TSharedPtr<FJsonObject> InputEcho = MakeShared<FJsonObject>();
    InputEcho->SetStringField(InputKind, InputPath);
    if (!GraphTypeFilter.IsEmpty())
    {
        InputEcho->SetStringField(TEXT("graphType"), GraphTypeFilter);
    }

    Result->SetObjectField(TEXT("inputEcho"), InputEcho);
    Result->SetArrayField(TEXT("resolvedGraphRefs"), FilteredGraphRefs);
    Result->SetArrayField(TEXT("diagnostics"), Diagnostics);
    return Result;
}

namespace
{
struct FGraphQueryShapeOptions
{
    TArray<FString> NodeClasses;
    TSet<FString> NodeIds;
    FString Text;
    int32 Limit = 200;
    int32 Offset = 0;
    bool bLimitExplicit = false;
    bool bCursorValid = true;
    FString CursorError;
};

bool ParseGraphQueryCursor(const FString& Cursor, int32& OutOffset, FString& OutError)
{
    OutOffset = 0;
    OutError.Empty();

    const FString TrimmedCursor = Cursor.TrimStartAndEnd();
    if (TrimmedCursor.IsEmpty())
    {
        return true;
    }

    FString Prefix;
    FString OffsetText;
    if (!TrimmedCursor.Split(TEXT(":"), &Prefix, &OffsetText) || !Prefix.Equals(TEXT("offset")))
    {
        OutError = TEXT("cursor must use the format offset:<non-negative integer>.");
        return false;
    }

    OffsetText = OffsetText.TrimStartAndEnd();
    if (OffsetText.IsEmpty())
    {
        OutError = TEXT("cursor offset is missing.");
        return false;
    }

    for (TCHAR Character : OffsetText)
    {
        if (!FChar::IsDigit(Character))
        {
            OutError = TEXT("cursor offset must be a non-negative integer.");
            return false;
        }
    }

    const int64 ParsedOffset = FCString::Atoi64(*OffsetText);
    if (ParsedOffset < 0 || ParsedOffset > TNumericLimits<int32>::Max())
    {
        OutError = TEXT("cursor offset is out of range.");
        return false;
    }

    OutOffset = static_cast<int32>(ParsedOffset);
    return true;
}

FString BuildGraphQueryCursor(const int32 Offset)
{
    return FString::Printf(TEXT("offset:%d"), FMath::Max(Offset, 0));
}

FGraphQueryShapeOptions ParseGraphQueryShapeOptions(const TSharedPtr<FJsonObject>& Arguments)
{
    FGraphQueryShapeOptions Options;

    if (Arguments.IsValid())
    {
        double LimitNumber = 0.0;
        if (Arguments->TryGetNumberField(TEXT("limit"), LimitNumber))
        {
            Options.Limit = FMath::Clamp(static_cast<int32>(LimitNumber), 1, 1000);
            Options.bLimitExplicit = true;
        }

        FString Cursor;
        if (Arguments->TryGetStringField(TEXT("cursor"), Cursor) && !Cursor.TrimStartAndEnd().IsEmpty())
        {
            Options.bCursorValid = ParseGraphQueryCursor(Cursor, Options.Offset, Options.CursorError);
        }

        const TSharedPtr<FJsonObject>* FilterObject = nullptr;
        if (Arguments->TryGetObjectField(TEXT("filter"), FilterObject) && FilterObject && (*FilterObject).IsValid())
        {
            const TArray<TSharedPtr<FJsonValue>>* NodeClasses = nullptr;
            if ((*FilterObject)->TryGetArrayField(TEXT("nodeClasses"), NodeClasses) && NodeClasses)
            {
                for (const TSharedPtr<FJsonValue>& NodeClassValue : *NodeClasses)
                {
                    FString NodeClass;
                    if (NodeClassValue.IsValid() && NodeClassValue->TryGetString(NodeClass) && !NodeClass.IsEmpty())
                    {
                        Options.NodeClasses.Add(NodeClass);
                    }
                }
            }

            const TArray<TSharedPtr<FJsonValue>>* NodeIds = nullptr;
            if ((*FilterObject)->TryGetArrayField(TEXT("nodeIds"), NodeIds) && NodeIds)
            {
                for (const TSharedPtr<FJsonValue>& NodeIdValue : *NodeIds)
                {
                    FString NodeId;
                    if (NodeIdValue.IsValid() && NodeIdValue->TryGetString(NodeId) && !NodeId.IsEmpty())
                    {
                        Options.NodeIds.Add(NodeId);
                    }
                }
            }

            (*FilterObject)->TryGetStringField(TEXT("text"), Options.Text);
            Options.Text = Options.Text.TrimStartAndEnd();
        }
    }

    return Options;
}

bool GraphQueryNodeMatchesShapeOptions(const TSharedPtr<FJsonObject>& NodeObject, const FGraphQueryShapeOptions& Options)
{
    if (!NodeObject.IsValid())
    {
        return false;
    }

    FString NodeId;
    NodeObject->TryGetStringField(TEXT("id"), NodeId);
    FString NodeGuid;
    NodeObject->TryGetStringField(TEXT("guid"), NodeGuid);

    if (Options.NodeIds.Num() > 0 && !Options.NodeIds.Contains(NodeId) && !Options.NodeIds.Contains(NodeGuid))
    {
        return false;
    }

    FString NodeClassPath;
    NodeObject->TryGetStringField(TEXT("nodeClassPath"), NodeClassPath);
    if (NodeClassPath.IsEmpty())
    {
        NodeObject->TryGetStringField(TEXT("classPath"), NodeClassPath);
    }

    if (Options.NodeClasses.Num() > 0)
    {
        bool bClassMatched = false;
        for (const FString& FilterClass : Options.NodeClasses)
        {
            if (NodeClassPath.Equals(FilterClass))
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

    if (!Options.Text.IsEmpty())
    {
        FString Title;
        NodeObject->TryGetStringField(TEXT("title"), Title);
        if (!Title.Contains(Options.Text, ESearchCase::IgnoreCase)
            && !NodeId.Contains(Options.Text, ESearchCase::IgnoreCase)
            && !NodeGuid.Contains(Options.Text, ESearchCase::IgnoreCase)
            && !NodeClassPath.Contains(Options.Text, ESearchCase::IgnoreCase))
        {
            return false;
        }
    }

    return true;
}

void PruneGraphQueryNodeLinks(const TSharedPtr<FJsonObject>& NodeObject, const TSet<FString>& AllowedNodeIds)
{
    if (!NodeObject.IsValid())
    {
        return;
    }

    const TArray<TSharedPtr<FJsonValue>>* Pins = nullptr;
    if (!NodeObject->TryGetArrayField(TEXT("pins"), Pins) || Pins == nullptr)
    {
        return;
    }

    for (const TSharedPtr<FJsonValue>& PinValue : *Pins)
    {
        const TSharedPtr<FJsonObject>* PinObject = nullptr;
        if (!PinValue.IsValid() || !PinValue->TryGetObject(PinObject) || PinObject == nullptr || !(*PinObject).IsValid())
        {
            continue;
        }

        auto FilterLinks = [&AllowedNodeIds](const TArray<TSharedPtr<FJsonValue>>* Links) -> TArray<TSharedPtr<FJsonValue>>
        {
            TArray<TSharedPtr<FJsonValue>> FilteredLinks;
            if (Links == nullptr)
            {
                return FilteredLinks;
            }

            FilteredLinks.Reserve(Links->Num());
            for (const TSharedPtr<FJsonValue>& LinkValue : *Links)
            {
                const TSharedPtr<FJsonObject>* LinkObject = nullptr;
                if (!LinkValue.IsValid() || !LinkValue->TryGetObject(LinkObject) || LinkObject == nullptr || !(*LinkObject).IsValid())
                {
                    continue;
                }

                FString LinkedNodeId;
                (*LinkObject)->TryGetStringField(TEXT("toNodeId"), LinkedNodeId);
                if (LinkedNodeId.IsEmpty())
                {
                    (*LinkObject)->TryGetStringField(TEXT("nodeGuid"), LinkedNodeId);
                }

                if (AllowedNodeIds.Contains(LinkedNodeId))
                {
                    FilteredLinks.Add(LinkValue);
                }
            }
            return FilteredLinks;
        };

        const TArray<TSharedPtr<FJsonValue>>* Links = nullptr;
        if ((*PinObject)->TryGetArrayField(TEXT("links"), Links))
        {
            (*PinObject)->SetArrayField(TEXT("links"), FilterLinks(Links));
        }

        const TArray<TSharedPtr<FJsonValue>>* LinkedTo = nullptr;
        if ((*PinObject)->TryGetArrayField(TEXT("linkedTo"), LinkedTo))
        {
            (*PinObject)->SetArrayField(TEXT("linkedTo"), FilterLinks(LinkedTo));
        }
    }
}

FString BuildGraphQueryRevision(const TSharedPtr<FJsonObject>& Result, const FString& Signature)
{
    if (!Result.IsValid())
    {
        return TEXT("");
    }

    FString RevisionPrefix = TEXT("graph");
    FString ExistingRevision;
    Result->TryGetStringField(TEXT("revision"), ExistingRevision);
    int32 SeparatorIndex = INDEX_NONE;
    if (ExistingRevision.FindChar(TEXT(':'), SeparatorIndex) && SeparatorIndex > 0)
    {
        RevisionPrefix = ExistingRevision.Left(SeparatorIndex);
    }

    FString AssetPath;
    Result->TryGetStringField(TEXT("assetPath"), AssetPath);
    FString GraphName;
    Result->TryGetStringField(TEXT("graphName"), GraphName);
    return FString::Printf(TEXT("%s:%08x"), *RevisionPrefix, GetTypeHash(AssetPath + TEXT("|") + GraphName + TEXT("|") + Signature));
}
}

TSharedPtr<FJsonObject> FLoomleBridgeModule::BuildGraphQueryToolResult(const TSharedPtr<FJsonObject>& Arguments) const
{
    FString RequestedGraphType;
    const bool bHasExplicitGraphType =
        Arguments.IsValid()
        && Arguments->TryGetStringField(TEXT("graphType"), RequestedGraphType)
        && !RequestedGraphType.IsEmpty();
    if (bHasExplicitGraphType && NormalizeGraphType(RequestedGraphType).Equals(TEXT("blueprint")))
    {
        if (!IsInGameThread())
        {
            TPromise<TSharedPtr<FJsonObject>> ResponsePromise;
            TFuture<TSharedPtr<FJsonObject>> ResponseFuture = ResponsePromise.GetFuture();
            AsyncTask(ENamedThreads::GameThread, [this, Arguments, Promise = MoveTemp(ResponsePromise)]() mutable
            {
                Promise.SetValue(BuildGraphQueryBaseResult(Arguments));
            });

            static constexpr uint32 GameThreadTimeoutMs = 30000;
            if (!ResponseFuture.WaitFor(FTimespan::FromMilliseconds(GameThreadTimeoutMs)))
            {
                TSharedPtr<FJsonObject> TimeoutResult = MakeShared<FJsonObject>();
                TimeoutResult->SetBoolField(TEXT("isError"), true);
                TimeoutResult->SetStringField(TEXT("code"), TEXT("EXECUTION_TIMEOUT"));
                TimeoutResult->SetStringField(TEXT("message"), TEXT("EXECUTION_TIMEOUT"));
                return TimeoutResult;
            }

            return ResponseFuture.Get();
        }

        return BuildGraphQueryBaseResult(Arguments);
    }

    TSharedPtr<FJsonObject> BaseArguments = CloneJsonObject(Arguments);
    if (!BaseArguments.IsValid())
    {
        BaseArguments = MakeShared<FJsonObject>();
        if (Arguments.IsValid())
        {
            for (const TPair<FString, TSharedPtr<FJsonValue>>& Field : Arguments->Values)
            {
                BaseArguments->SetField(Field.Key, Field.Value);
            }
        }
    }
    BaseArguments->SetBoolField(TEXT("_loomleBaseSnapshot"), true);

    if (!IsInGameThread())
    {
        TPromise<TSharedPtr<FJsonObject>> ResponsePromise;
        TFuture<TSharedPtr<FJsonObject>> ResponseFuture = ResponsePromise.GetFuture();
        AsyncTask(ENamedThreads::GameThread, [this, BaseArguments, Promise = MoveTemp(ResponsePromise)]() mutable
        {
            Promise.SetValue(BuildGraphQueryBaseResult(BaseArguments));
        });

        static constexpr uint32 GameThreadTimeoutMs = 30000;
        if (!ResponseFuture.WaitFor(FTimespan::FromMilliseconds(GameThreadTimeoutMs)))
        {
            TSharedPtr<FJsonObject> TimeoutResult = MakeShared<FJsonObject>();
            TimeoutResult->SetBoolField(TEXT("isError"), true);
            TimeoutResult->SetStringField(TEXT("code"), TEXT("EXECUTION_TIMEOUT"));
            TimeoutResult->SetStringField(TEXT("message"), TEXT("EXECUTION_TIMEOUT"));
            return TimeoutResult;
        }

        return BuildShapedGraphQueryResult(ResponseFuture.Get(), Arguments);
    }

    return BuildShapedGraphQueryResult(BuildGraphQueryBaseResult(BaseArguments), Arguments);
}

TSharedPtr<FJsonObject> FLoomleBridgeModule::BuildShapedGraphQueryResult(const TSharedPtr<FJsonObject>& BaseResult, const TSharedPtr<FJsonObject>& Arguments) const
{
    if (!BaseResult.IsValid())
    {
        TSharedPtr<FJsonObject> ErrorResult = MakeShared<FJsonObject>();
        ErrorResult->SetBoolField(TEXT("isError"), true);
        ErrorResult->SetStringField(TEXT("code"), TEXT("INTERNAL_ERROR"));
        ErrorResult->SetStringField(TEXT("message"), TEXT("graph.query produced an invalid result."));
        return ErrorResult;
    }

    bool bIsError = false;
    if (BaseResult->TryGetBoolField(TEXT("isError"), bIsError) && bIsError)
    {
        return CloneJsonObject(BaseResult);
    }

    TSharedPtr<FJsonObject> Result = CloneJsonObject(BaseResult);
    if (!Result.IsValid())
    {
        return BaseResult;
    }

    const FGraphQueryShapeOptions ShapeOptions = ParseGraphQueryShapeOptions(Arguments);
    if (!ShapeOptions.bCursorValid)
    {
        TSharedPtr<FJsonObject> ErrorResult = MakeShared<FJsonObject>();
        ErrorResult->SetBoolField(TEXT("isError"), true);
        ErrorResult->SetStringField(TEXT("code"), TEXT("INVALID_CURSOR"));
        ErrorResult->SetStringField(TEXT("message"), ShapeOptions.CursorError.IsEmpty()
            ? TEXT("graph.query cursor is invalid.")
            : ShapeOptions.CursorError);
        return ErrorResult;
    }

    const TSharedPtr<FJsonObject>* SnapshotObject = nullptr;
    if (!Result->TryGetObjectField(TEXT("semanticSnapshot"), SnapshotObject) || SnapshotObject == nullptr || !(*SnapshotObject).IsValid())
    {
        return Result;
    }

    const TArray<TSharedPtr<FJsonValue>>* SnapshotNodes = nullptr;
    const TArray<TSharedPtr<FJsonValue>>* SnapshotEdges = nullptr;
    (*SnapshotObject)->TryGetArrayField(TEXT("nodes"), SnapshotNodes);
    (*SnapshotObject)->TryGetArrayField(TEXT("edges"), SnapshotEdges);
    const int32 OriginalNodeCount = SnapshotNodes ? SnapshotNodes->Num() : 0;
    const int32 OriginalEdgeCount = SnapshotEdges ? SnapshotEdges->Num() : 0;

    TArray<TSharedPtr<FJsonValue>> ShapedNodes;
    TArray<TSharedPtr<FJsonValue>> ShapedEdges;
    TSet<FString> IncludedNodeIds;
    TArray<FString> SignatureNodeTokens;
    TArray<FString> SignatureEdgeTokens;
    int32 MatchingNodeCount = 0;

    if (SnapshotNodes != nullptr)
    {
        ShapedNodes.Reserve(FMath::Min(ShapeOptions.Limit, SnapshotNodes->Num()));
        for (const TSharedPtr<FJsonValue>& NodeValue : *SnapshotNodes)
        {
            const TSharedPtr<FJsonObject>* NodeObject = nullptr;
            if (!NodeValue.IsValid() || !NodeValue->TryGetObject(NodeObject) || NodeObject == nullptr || !(*NodeObject).IsValid())
            {
                continue;
            }

            if (!GraphQueryNodeMatchesShapeOptions(*NodeObject, ShapeOptions))
            {
                continue;
            }

            ++MatchingNodeCount;
            if (MatchingNodeCount <= ShapeOptions.Offset)
            {
                continue;
            }
            if (ShapedNodes.Num() >= ShapeOptions.Limit)
            {
                continue;
            }

            FString NodeId;
            (*NodeObject)->TryGetStringField(TEXT("id"), NodeId);
            FString NodeGuid;
            (*NodeObject)->TryGetStringField(TEXT("guid"), NodeGuid);
            if (NodeId.IsEmpty())
            {
                NodeId = NodeGuid;
            }

            IncludedNodeIds.Add(NodeId);
            SignatureNodeTokens.Add(NodeId);
            ShapedNodes.Add(NodeValue);
        }
    }

    for (const TSharedPtr<FJsonValue>& NodeValue : ShapedNodes)
    {
        const TSharedPtr<FJsonObject>* NodeObject = nullptr;
        if (!NodeValue.IsValid() || !NodeValue->TryGetObject(NodeObject) || NodeObject == nullptr || !(*NodeObject).IsValid())
        {
            continue;
        }
        PruneGraphQueryNodeLinks(*NodeObject, IncludedNodeIds);
    }

    if (SnapshotEdges != nullptr)
    {
        ShapedEdges.Reserve(SnapshotEdges->Num());
        for (const TSharedPtr<FJsonValue>& EdgeValue : *SnapshotEdges)
        {
            const TSharedPtr<FJsonObject>* EdgeObject = nullptr;
            if (!EdgeValue.IsValid() || !EdgeValue->TryGetObject(EdgeObject) || EdgeObject == nullptr || !(*EdgeObject).IsValid())
            {
                continue;
            }

            FString FromNodeId;
            FString ToNodeId;
            FString FromPin;
            FString ToPin;
            (*EdgeObject)->TryGetStringField(TEXT("fromNodeId"), FromNodeId);
            (*EdgeObject)->TryGetStringField(TEXT("toNodeId"), ToNodeId);
            if (!IncludedNodeIds.Contains(FromNodeId) || !IncludedNodeIds.Contains(ToNodeId))
            {
                continue;
            }

            (*EdgeObject)->TryGetStringField(TEXT("fromPin"), FromPin);
            (*EdgeObject)->TryGetStringField(TEXT("toPin"), ToPin);
            SignatureEdgeTokens.Add(FromNodeId + TEXT("|") + FromPin + TEXT("->") + ToNodeId + TEXT("|") + ToPin);
            ShapedEdges.Add(EdgeValue);
        }
    }

    Algo::Sort(SignatureNodeTokens);
    Algo::Sort(SignatureEdgeTokens);
    const FString Signature = FString::Join(SignatureNodeTokens, TEXT(";")) + TEXT("#") + FString::Join(SignatureEdgeTokens, TEXT(";"));
    const bool bTruncated = (ShapeOptions.Offset + ShapedNodes.Num()) < MatchingNodeCount;

    (*SnapshotObject)->SetStringField(TEXT("signature"), Signature);
    (*SnapshotObject)->SetArrayField(TEXT("nodes"), ShapedNodes);
    (*SnapshotObject)->SetArrayField(TEXT("edges"), ShapedEdges);
    Result->SetStringField(TEXT("revision"), BuildGraphQueryRevision(Result, Signature));
    Result->SetStringField(TEXT("nextCursor"), bTruncated ? BuildGraphQueryCursor(ShapeOptions.Offset + ShapedNodes.Num()) : TEXT(""));

    const TSharedPtr<FJsonObject>* MetaObject = nullptr;
    if (!Result->TryGetObjectField(TEXT("meta"), MetaObject) || MetaObject == nullptr || !(*MetaObject).IsValid())
    {
        TSharedPtr<FJsonObject> NewMeta = MakeShared<FJsonObject>();
        Result->SetObjectField(TEXT("meta"), NewMeta);
        Result->TryGetObjectField(TEXT("meta"), MetaObject);
    }

    (*MetaObject)->SetNumberField(TEXT("totalNodes"), OriginalNodeCount);
    (*MetaObject)->SetNumberField(TEXT("returnedNodes"), ShapedNodes.Num());
    (*MetaObject)->SetNumberField(TEXT("totalEdges"), OriginalEdgeCount);
    (*MetaObject)->SetNumberField(TEXT("returnedEdges"), ShapedEdges.Num());
    (*MetaObject)->SetBoolField(TEXT("truncated"), bTruncated);

    return Result;
}

TSharedPtr<FJsonObject> FLoomleBridgeModule::BuildGraphQueryBaseResult(const TSharedPtr<FJsonObject>& Arguments) const
{
    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetBoolField(TEXT("isError"), false);
    bool bBaseSnapshotRequest = false;
    if (Arguments.IsValid())
    {
        Arguments->TryGetBoolField(TEXT("_loomleBaseSnapshot"), bBaseSnapshotRequest);
    }

    FString GraphType;
    const bool bHasExplicitGraphType =
        Arguments.IsValid() && Arguments->TryGetStringField(TEXT("graphType"), GraphType) && !GraphType.IsEmpty();
    if (bHasExplicitGraphType)
    {
        GraphType = NormalizeGraphType(GraphType);
    }
    else
    {
        GraphType = TEXT("blueprint");
        FString InferredAssetPath;
        const TSharedPtr<FJsonObject>* QueryGraphRefObj = nullptr;
        const bool bHasQueryGraphRef =
            Arguments.IsValid()
            && Arguments->TryGetObjectField(TEXT("graphRef"), QueryGraphRefObj)
            && QueryGraphRefObj
            && (*QueryGraphRefObj).IsValid();
        if (bHasQueryGraphRef)
        {
            FString QueryGraphRefKind;
            (*QueryGraphRefObj)->TryGetStringField(TEXT("kind"), QueryGraphRefKind);
            if (!QueryGraphRefKind.Equals(TEXT("inline")))
            {
                (*QueryGraphRefObj)->TryGetStringField(TEXT("assetPath"), InferredAssetPath);
            }
        }
        if (InferredAssetPath.IsEmpty() && Arguments.IsValid())
        {
            Arguments->TryGetStringField(TEXT("assetPath"), InferredAssetPath);
        }
        if (!InferredAssetPath.IsEmpty())
        {
            const FString NormalizedInferredAssetPath = NormalizeAssetPath(InferredAssetPath);
            UObject* AssetObject = LoadObjectByAssetPath(NormalizedInferredAssetPath);
            if (AssetObject != nullptr
                && (AssetObject->IsA<UMaterial>() || AssetObject->IsA<UMaterialFunction>()))
            {
                GraphType = TEXT("material");
            }
            else if (IsLikelyPcgAsset(AssetObject))
            {
                GraphType = TEXT("pcg");
            }
        }
    }
    if (!IsSupportedGraphType(GraphType))
    {
        Result->SetBoolField(TEXT("isError"), true);
        Result->SetStringField(TEXT("code"), TEXT("UNSUPPORTED_GRAPH_TYPE"));
        Result->SetStringField(TEXT("message"), TEXT("Supported graphType values: blueprint, material, pcg."));
        return Result;
    }

    // Resolve addressing mode: Mode A (assetPath + optional graphName for single-graph assets)
    // or Mode B (graphRef).
    FResolvedGraphAddress GraphAddress;
    FGraphAddressResolutionOptions GraphAddressOptions;
    GraphAddressOptions.bRequireBlueprintGraphNameInModeA = true;
    GraphAddressOptions.bUseModeLabelsInMessages = true;
    GraphAddressOptions.bUseKindSpecificGraphRefAssetErrors = true;
    if (!ResolveGraphRequestAddress(Arguments, GraphType, GraphAddressOptions, GraphAddress, Result))
    {
        return Result;
    }

    FString AssetPath = GraphAddress.AssetPath;
    FString GraphName = GraphAddress.GraphName;
    FString InlineNodeGuid = GraphAddress.InlineNodeGuid; // set when graphRef.kind == "inline"

    // Path traversal (Blueprint only): an ordered array of composite node GUIDs to drill into.
    // Because ListCompositeSubgraphNodes searches all Blueprint graphs by GUID, only the final
    // element is needed — no manual step-by-step resolution required.
    if (GraphType.Equals(TEXT("blueprint")))
    {
        const TArray<TSharedPtr<FJsonValue>>* PathArray = nullptr;
        if (Arguments->TryGetArrayField(TEXT("path"), PathArray) && PathArray && PathArray->Num() > 0)
        {
            FString LastGuid;
            for (const TSharedPtr<FJsonValue>& PathVal : *PathArray)
            {
                FString Guid;
                if (PathVal.IsValid() && PathVal->TryGetString(Guid) && !Guid.IsEmpty())
                {
                    LastGuid = Guid;
                }
            }
            if (!LastGuid.IsEmpty())
            {
                InlineNodeGuid = LastGuid;
            }
        }
    }

    FString RequestedLayoutDetail = TEXT("basic");
    Arguments->TryGetStringField(TEXT("layoutDetail"), RequestedLayoutDetail);
    RequestedLayoutDetail = RequestedLayoutDetail.ToLower();
    if (!RequestedLayoutDetail.Equals(TEXT("measured")))
    {
        RequestedLayoutDetail = TEXT("basic");
    }
    const FString AppliedLayoutDetail = RequestedLayoutDetail.Equals(TEXT("measured"))
        ? TEXT("basic")
        : RequestedLayoutDetail;

    auto MakeLayoutObject = [](int32 PositionX, int32 PositionY, const FString& Source, bool bReliable) -> TSharedPtr<FJsonObject>
    {
        TSharedPtr<FJsonObject> Layout = MakeShared<FJsonObject>();
        TSharedPtr<FJsonObject> Position = MakeShared<FJsonObject>();
        Position->SetNumberField(TEXT("x"), PositionX);
        Position->SetNumberField(TEXT("y"), PositionY);
        Layout->SetObjectField(TEXT("position"), Position);
        Layout->SetStringField(TEXT("source"), Source);
        Layout->SetBoolField(TEXT("reliable"), bReliable);
        Layout->SetStringField(TEXT("sizeSource"), TEXT("unsupported"));
        Layout->SetStringField(TEXT("boundsSource"), TEXT("unsupported"));
        return Layout;
    };

    auto MakeLayoutCapabilitiesObject = [&GraphType]() -> TSharedPtr<FJsonObject>
    {
        TSharedPtr<FJsonObject> Capabilities = MakeShared<FJsonObject>();
        const bool bCanMoveNode = GraphType.Equals(TEXT("blueprint"))
            || GraphType.Equals(TEXT("material"))
            || GraphType.Equals(TEXT("pcg"));
        Capabilities->SetBoolField(TEXT("canReadPosition"), true);
        Capabilities->SetBoolField(TEXT("canReadSize"), false);
        Capabilities->SetBoolField(TEXT("canReadBounds"), false);
        Capabilities->SetBoolField(TEXT("canMoveNode"), bCanMoveNode);
        Capabilities->SetBoolField(TEXT("canBatchMove"), bCanMoveNode);
        Capabilities->SetBoolField(TEXT("supportsMeasuredGeometry"), false);
        Capabilities->SetStringField(TEXT("positionSource"), TEXT("model"));
        Capabilities->SetStringField(TEXT("sizeSource"), GraphType.Equals(TEXT("blueprint")) ? TEXT("partial") : TEXT("unsupported"));
        return Capabilities;
    };

    auto BuildMinimalSnapshotResult = [&Result, &GraphType, &AssetPath, &GraphName, &MakeLayoutCapabilitiesObject, &RequestedLayoutDetail, &AppliedLayoutDetail](const FString& RevisionPrefix, const TArray<TSharedPtr<FJsonValue>>& Nodes, const TArray<TSharedPtr<FJsonValue>>& Edges, const TArray<TSharedPtr<FJsonValue>>& Diagnostics)
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
        Meta->SetObjectField(TEXT("layoutCapabilities"), MakeLayoutCapabilitiesObject());
        Meta->SetStringField(TEXT("layoutDetailRequested"), RequestedLayoutDetail);
        Meta->SetStringField(TEXT("layoutDetailApplied"), AppliedLayoutDetail);

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
        // Try UMaterial first; fall back to UMaterialFunction (for asset-kind graphRef pointing to a function asset).
        UMaterial* Material = LoadMaterialByAssetPath(AssetPath);
        UMaterialFunction* MatFunc = nullptr;
        if (Material == nullptr)
        {
            MatFunc = Cast<UMaterialFunction>(LoadObjectByAssetPath(AssetPath));
        }
        if (Material == nullptr && MatFunc == nullptr)
        {
            Result->SetBoolField(TEXT("isError"), true);
            Result->SetStringField(TEXT("code"), TEXT("ASSET_NOT_FOUND"));
            Result->SetStringField(TEXT("message"), TEXT("Material or MaterialFunction asset not found."));
            return Result;
        }
        if (Material != nullptr)
        {
            if (Material->MaterialGraph == nullptr)
            {
                Material->MaterialGraph = CastChecked<UMaterialGraph>(
                    FBlueprintEditorUtils::CreateNewGraph(
                        Material,
                        NAME_None,
                        UMaterialGraph::StaticClass(),
                        UMaterialGraphSchema::StaticClass()));
            }
            if (Material->MaterialGraph != nullptr)
            {
                Material->MaterialGraph->Material = Material;
                Material->MaterialGraph->MaterialFunction = nullptr;
                Material->MaterialGraph->RebuildGraph();
            }
        }

        // Collect all expressions from whichever asset was loaded.
        TArray<UMaterialExpression*> AllExpressions;
        if (Material)
        {
            for (UMaterialExpression* E : Material->GetExpressions()) { if (E) AllExpressions.Add(E); }
        }
        else
        {
            for (const TObjectPtr<UMaterialExpression>& E : MatFunc->GetExpressions()) { if (E) AllExpressions.Add(E.Get()); }
        }

        TArray<TSharedPtr<FJsonValue>> Nodes;
        TArray<TSharedPtr<FJsonValue>> Diagnostics;
        TArray<TSharedPtr<FJsonValue>> Edges;
        const FString MaterialRootNodeId = TEXT("__material_root__");
        int32 MaterialChildGraphRefCount = 0;
        for (UMaterialExpression* Expression : AllExpressions)
        {
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
            Node->SetObjectField(
                TEXT("layout"),
                MakeLayoutObject(Expression->MaterialExpressionEditorX, Expression->MaterialExpressionEditorY, TEXT("model"), true));
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

            // Annotate MaterialFunctionCall nodes with childGraphRef.
            if (UMaterialExpressionMaterialFunctionCall* FuncCall = Cast<UMaterialExpressionMaterialFunctionCall>(Expression))
            {
                if (FuncCall->MaterialFunction)
                {
                    FString FuncAssetPath;
                    if (ResolveMaterialFunctionAssetPath(FuncCall->MaterialFunction, FuncAssetPath))
                    {
                        SetNodeAssetChildGraphRef(Node, FuncAssetPath, TEXT("loaded"));
                        ++MaterialChildGraphRefCount;
                    }
                }
            }

            Node->SetArrayField(TEXT("pins"), Pins);
            Nodes.Add(MakeShared<FJsonValueObject>(Node));
        }

        if (Material != nullptr && Material->MaterialGraph != nullptr && Material->MaterialGraph->RootNode != nullptr)
        {
            UMaterialGraphNode_Root* RootNode = Material->MaterialGraph->RootNode;
            TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
            Root->SetStringField(TEXT("id"), MaterialRootNodeId);
            Root->SetStringField(TEXT("guid"), MaterialRootNodeId);
            Root->SetStringField(TEXT("nodeClassPath"), RootNode->GetClass() ? RootNode->GetClass()->GetPathName() : TEXT(""));
            Root->SetStringField(TEXT("title"), Material->GetName());
            Root->SetStringField(TEXT("graphName"), GraphName);
            TSharedPtr<FJsonObject> RootPosition = MakeShared<FJsonObject>();
            RootPosition->SetNumberField(TEXT("x"), RootNode->NodePosX);
            RootPosition->SetNumberField(TEXT("y"), RootNode->NodePosY);
            Root->SetObjectField(TEXT("position"), RootPosition);
            Root->SetObjectField(TEXT("layout"), MakeLayoutObject(RootNode->NodePosX, RootNode->NodePosY, TEXT("model"), true));
            Root->SetBoolField(TEXT("enabled"), true);
            Root->SetStringField(TEXT("nodeRole"), TEXT("materialRoot"));

            TArray<TSharedPtr<FJsonValue>> RootPins;
            for (UEdGraphPin* Pin : RootNode->Pins)
            {
                if (Pin == nullptr || Pin->Direction != EGPD_Input)
                {
                    continue;
                }

                TSharedPtr<FJsonObject> PinObj = MakeShared<FJsonObject>();
                const FString PinName = Pin->PinName.ToString();
                PinObj->SetStringField(TEXT("name"), PinName);
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
                for (UEdGraphPin* LinkedPin : Pin->LinkedTo)
                {
                    UEdGraphNode* LinkedNode = LinkedPin ? LinkedPin->GetOwningNodeUnchecked() : nullptr;
                    UMaterialGraphNode* MaterialGraphNode = Cast<UMaterialGraphNode>(LinkedNode);
                    UMaterialExpression* LinkedExpression = MaterialGraphNode ? MaterialGraphNode->MaterialExpression : nullptr;
                    if (LinkedExpression == nullptr)
                    {
                        continue;
                    }

                    const FString FromNodeId = MaterialExpressionId(LinkedExpression);
                    const FString FromPinName = LinkedPin->PinName.ToString();

                    TSharedPtr<FJsonObject> LinkObj = MakeShared<FJsonObject>();
                    LinkObj->SetStringField(TEXT("toNodeId"), FromNodeId);
                    LinkObj->SetStringField(TEXT("toPin"), FromPinName);
                    LinkObj->SetStringField(TEXT("nodeName"), LinkedExpression->GetName());
                    LinkObj->SetStringField(TEXT("nodeGuid"), FromNodeId);
                    LinkObj->SetStringField(TEXT("direction"), TEXT("input"));
                    Links.Add(MakeShared<FJsonValueObject>(LinkObj));

                    TSharedPtr<FJsonObject> EdgeObj = MakeShared<FJsonObject>();
                    EdgeObj->SetStringField(TEXT("fromNodeId"), FromNodeId);
                    EdgeObj->SetStringField(TEXT("fromPin"), FromPinName);
                    EdgeObj->SetStringField(TEXT("toNodeId"), MaterialRootNodeId);
                    EdgeObj->SetStringField(TEXT("toPin"), PinName);
                    Edges.Add(MakeShared<FJsonValueObject>(EdgeObj));
                }

                PinObj->SetArrayField(TEXT("links"), Links);
                PinObj->SetArrayField(TEXT("linkedTo"), Links);
                RootPins.Add(MakeShared<FJsonValueObject>(PinObj));
            }

            Root->SetArrayField(TEXT("pins"), RootPins);
            Nodes.Add(MakeShared<FJsonValueObject>(Root));
        }

        if (Nodes.Num() == 0)
        {
            TSharedPtr<FJsonObject> Diagnostic = MakeShared<FJsonObject>();
            Diagnostic->SetStringField(TEXT("code"), TEXT("QUERY_DEGRADED"));
            Diagnostic->SetStringField(TEXT("message"), TEXT("Material graph has no expressions."));
            Diagnostics.Add(MakeShared<FJsonValueObject>(Diagnostic));
        }
        else if (MaterialChildGraphRefCount > 0)
        {
            TSharedPtr<FJsonObject> Diagnostic = MakeShared<FJsonObject>();
            Diagnostic->SetStringField(TEXT("code"), TEXT("MATERIAL_SUBGRAPH_REFS_PRESENT"));
            Diagnostic->SetStringField(
                TEXT("message"),
                TEXT("This material query covers the current asset only. Follow childGraphRef entries or use graph.list(includeSubgraphs=true) to inspect referenced MaterialFunction subgraphs."));
            Diagnostic->SetNumberField(TEXT("childGraphRefCount"), MaterialChildGraphRefCount);
            Diagnostics.Add(MakeShared<FJsonValueObject>(Diagnostic));
        }

        BuildMinimalSnapshotResult(TEXT("mat"), Nodes, Edges, Diagnostics);
        // Echo effective graphRef at response root.
        Result->SetObjectField(TEXT("graphRef"), MakeGraphAssetRef(AssetPath, TEXT("")));
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
        if (!bBaseSnapshotRequest && Arguments->TryGetObjectField(TEXT("filter"), FilterObj) && FilterObj && (*FilterObj).IsValid())
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

        int32 Limit = TNumericLimits<int32>::Max();
        if (!bBaseSnapshotRequest)
        {
            Limit = 200;
            double LimitNumber = 0.0;
            if (Arguments->TryGetNumberField(TEXT("limit"), LimitNumber))
            {
                Limit = FMath::Clamp(static_cast<int32>(LimitNumber), 1, 1000);
            }
        }

        TArray<TSharedPtr<FJsonValue>> Nodes;
        TArray<TSharedPtr<FJsonValue>> Edges;
        TArray<TSharedPtr<FJsonValue>> Diagnostics;
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
            Node->SetObjectField(
                TEXT("layout"),
                MakeLayoutObject(NodePosX, NodePosY, TEXT("model"), true));
            Node->SetBoolField(TEXT("enabled"), true);
            TArray<TSharedPtr<FJsonValue>> NodeDiagnostics;

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
                FString SurfacedDefaultValue;
                FString SurfacedDefaultText;
                const bool bHasSurfacedDefault =
                    Direction.Equals(TEXT("input"))
                    && !Pin->IsConnected()
                    && TryReadPcgPinDefaultValueForQuery(NodeObj, PinLabel, SurfacedDefaultValue, SurfacedDefaultText);
                PinObj->SetStringField(TEXT("defaultValue"), bHasSurfacedDefault ? SurfacedDefaultValue : TEXT(""));
                PinObj->SetStringField(TEXT("defaultObject"), TEXT(""));
                PinObj->SetStringField(TEXT("defaultText"), bHasSurfacedDefault ? SurfacedDefaultText : TEXT(""));

                TSharedPtr<FJsonObject> PinTypeObject = MakeShared<FJsonObject>();
                PinTypeObject->SetStringField(TEXT("category"), TEXT("pcg"));
                PinTypeObject->SetStringField(TEXT("subCategory"), TEXT(""));
                PinTypeObject->SetStringField(TEXT("object"), TEXT(""));
                PinTypeObject->SetStringField(TEXT("container"), Pin->Properties.bAllowMultipleData ? TEXT("array") : TEXT("none"));
                PinObj->SetObjectField(TEXT("type"), PinTypeObject);

                TSharedPtr<FJsonObject> PinDefaultObject = MakeShared<FJsonObject>();
                PinDefaultObject->SetStringField(TEXT("value"), bHasSurfacedDefault ? SurfacedDefaultValue : TEXT(""));
                PinDefaultObject->SetStringField(TEXT("object"), TEXT(""));
                PinDefaultObject->SetStringField(TEXT("text"), bHasSurfacedDefault ? SurfacedDefaultText : TEXT(""));
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
            AppendPcgSyntheticWritablePins(NodeObj->GetSettings(), Pins);

            // Annotate PCG subgraph nodes with childGraphRef via reflection.
            if (NodeClassPath.Contains(TEXT("Subgraph")))
            {
                UPCGSettings* NodeSettings = NodeObj->GetSettings();
                if (NodeSettings)
                {
                    FString SubAssetPath;
                    FString ChildLoadStatus;
                    FString SubgraphName;
                    FString SubgraphClassPath;
                    if (ResolvePcgSubgraphAssetReference(NodeSettings, SubAssetPath, ChildLoadStatus, SubgraphName, SubgraphClassPath))
                    {
                        SetNodeAssetChildGraphRef(Node, SubAssetPath, ChildLoadStatus);
                    }
                }
            }

            if (TSharedPtr<FJsonObject> Settings = BuildPcgNodeSettingsObject(NodeObj, NodeDiagnostics, Diagnostics))
            {
                Node->SetObjectField(TEXT("settings"), Settings);
                Node->SetObjectField(TEXT("effectiveSettings"), Settings);
            }

            Node->SetArrayField(TEXT("pins"), Pins);
            Node->SetArrayField(TEXT("diagnostics"), NodeDiagnostics);
            Nodes.Add(MakeShared<FJsonValueObject>(Node));
        }

        if (Nodes.Num() == 0)
        {
            TSharedPtr<FJsonObject> Diagnostic = MakeShared<FJsonObject>();
            Diagnostic->SetStringField(TEXT("code"), TEXT("QUERY_EMPTY"));
            Diagnostic->SetStringField(TEXT("message"), TEXT("PCG graph has no nodes."));
            Diagnostics.Add(MakeShared<FJsonValueObject>(Diagnostic));
        }

        BuildMinimalSnapshotResult(TEXT("pcg"), Nodes, Edges, Diagnostics);
        // Echo effective graphRef at response root.
        Result->SetObjectField(TEXT("graphRef"), MakeGraphAssetRef(AssetPath, TEXT("")));
        return Result;
    }

    const FGraphQueryShapeOptions QueryShapeOptions = ParseGraphQueryShapeOptions(Arguments);
    if (!bBaseSnapshotRequest && !QueryShapeOptions.bCursorValid)
    {
        Result->SetBoolField(TEXT("isError"), true);
        Result->SetStringField(TEXT("code"), TEXT("INVALID_CURSOR"));
        Result->SetStringField(TEXT("message"), QueryShapeOptions.CursorError.IsEmpty()
            ? TEXT("graph.query cursor is invalid.")
            : QueryShapeOptions.CursorError);
        return Result;
    }

    FString NodesJson;
    FString Error;
    bool bOk = false;
    FLoomleBlueprintNodeListOptions BlueprintListOptions;
    FLoomleBlueprintNodeListStats BlueprintListStats;
    if (!bBaseSnapshotRequest && GraphType.Equals(TEXT("blueprint")))
    {
        BlueprintListOptions.NodeClasses = QueryShapeOptions.NodeClasses;
        BlueprintListOptions.NodeIds = QueryShapeOptions.NodeIds.Array();
        BlueprintListOptions.Text = QueryShapeOptions.Text;
        BlueprintListOptions.Offset = QueryShapeOptions.Offset;
        BlueprintListOptions.Limit = QueryShapeOptions.bLimitExplicit
            ? QueryShapeOptions.Limit
            : 50;
    }

    if (!InlineNodeGuid.IsEmpty())
    {
        // Mode B inline: query the composite subgraph by node guid.
        FString SubgraphNameOut;
        bOk = FLoomleBlueprintAdapter::ListCompositeSubgraphNodes(
            AssetPath,
            InlineNodeGuid,
            SubgraphNameOut,
            NodesJson,
            Error,
            (!bBaseSnapshotRequest && GraphType.Equals(TEXT("blueprint"))) ? &BlueprintListOptions : nullptr,
            (!bBaseSnapshotRequest && GraphType.Equals(TEXT("blueprint"))) ? &BlueprintListStats : nullptr);
        if (bOk && GraphName.IsEmpty())
        {
            GraphName = SubgraphNameOut;
        }
        if (!bOk)
        {
            const FString DomainCode = Error.Contains(TEXT("not a composite"))
                ? TEXT("GRAPH_REF_NOT_COMPOSITE")
                : (Error.Contains(TEXT("not found")) ? TEXT("NODE_NOT_FOUND") : TEXT("GRAPH_REF_INVALID"));
            Result->SetBoolField(TEXT("isError"), true);
            Result->SetStringField(TEXT("code"), DomainCode);
            Result->SetStringField(TEXT("message"), Error.IsEmpty() ? TEXT("graph.query (inline ref) failed") : Error);
            return Result;
        }
    }
    else
    {
        bOk = FLoomleBlueprintAdapter::ListGraphNodes(
            AssetPath,
            GraphName,
            NodesJson,
            Error,
            (!bBaseSnapshotRequest && GraphType.Equals(TEXT("blueprint"))) ? &BlueprintListOptions : nullptr,
            (!bBaseSnapshotRequest && GraphType.Equals(TEXT("blueprint"))) ? &BlueprintListStats : nullptr);
        if (!bOk)
        {
            Result->SetBoolField(TEXT("isError"), true);
            Result->SetStringField(TEXT("code"), Error.Contains(TEXT("Graph not found")) ? TEXT("GRAPH_NOT_FOUND") : TEXT("INTERNAL_ERROR"));
            Result->SetStringField(TEXT("message"), Error.IsEmpty() ? TEXT("graph.query failed") : Error);
            return Result;
        }
    }

    TArray<TSharedPtr<FJsonValue>> Nodes;
    {
        TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(NodesJson);
        FJsonSerializer::Deserialize(Reader, Nodes);
    }

    int32 Limit = TNumericLimits<int32>::Max();
    if (!bBaseSnapshotRequest)
    {
        Limit = GraphType.Equals(TEXT("blueprint"))
            ? BlueprintListOptions.Limit
            : QueryShapeOptions.Limit;
    }

    TArray<TSharedPtr<FJsonValue>> SnapshotNodes;
    TArray<TSharedPtr<FJsonValue>> Edges;
    TArray<TSharedPtr<FJsonValue>> Diagnostics;
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

        if (QueryShapeOptions.NodeClasses.Num() > 0)
        {
            FString NodeClassPath;
            (*NodeObj)->TryGetStringField(TEXT("nodeClassPath"), NodeClassPath);
            if (NodeClassPath.IsEmpty())
            {
                (*NodeObj)->TryGetStringField(TEXT("classPath"), NodeClassPath);
            }
            bool bClassMatched = false;
            for (const FString& FilterClass : QueryShapeOptions.NodeClasses)
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

    // Annotate K2Node_Composite nodes with childGraphRef by looking up BoundGraph via reflection.
    {
        UBlueprint* Blueprint = LoadBlueprintByAssetPath(AssetPath);
        if (Blueprint)
        {
            // Build a guid -> UEdGraphNode* lookup across all graphs for fast access.
            TMap<FGuid, UEdGraphNode*> GuidToNode;
            TArray<UEdGraph*> AllGraphs;
            AllGraphs.Append(Blueprint->UbergraphPages);
            AllGraphs.Append(Blueprint->FunctionGraphs);
            AllGraphs.Append(Blueprint->MacroGraphs);
            for (UEdGraph* G : AllGraphs)
            {
                if (!G) { continue; }
                for (UEdGraphNode* N : G->Nodes)
                {
                    if (N) { GuidToNode.Add(N->NodeGuid, N); }
                }
            }

            for (TSharedPtr<FJsonValue>& SnapshotNodeValue : SnapshotNodes)
            {
                const TSharedPtr<FJsonObject>* SnapshotNodeObj = nullptr;
                if (!SnapshotNodeValue.IsValid() || !SnapshotNodeValue->TryGetObject(SnapshotNodeObj)) { continue; }

                FString NodeClassPath;
                (*SnapshotNodeObj)->TryGetStringField(TEXT("nodeClassPath"), NodeClassPath);
                if (!NodeClassPath.Contains(TEXT("K2Node_Composite"))) { continue; }

                FString GuidText;
                (*SnapshotNodeObj)->TryGetStringField(TEXT("guid"), GuidText);
                FGuid NodeGuid;
                if (!FGuid::Parse(GuidText, NodeGuid)) { continue; }

                UEdGraphNode** FoundNode = GuidToNode.Find(NodeGuid);
                if (!FoundNode || !*FoundNode) { continue; }

                FObjectPropertyBase* BoundGraphProp = FindFProperty<FObjectPropertyBase>((*FoundNode)->GetClass(), TEXT("BoundGraph"));
                if (!BoundGraphProp) { continue; }

                UEdGraph* SubGraph = Cast<UEdGraph>(BoundGraphProp->GetObjectPropertyValue_InContainer(*FoundNode));
                if (!SubGraph) { continue; }

                SetNodeInlineChildGraphRef(*SnapshotNodeObj, GuidText, AssetPath);
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

    // Build the effective graphRef for the response root.
    GraphAddress.GraphName = GraphName;
    GraphAddress.InlineNodeGuid = InlineNodeGuid;
    TSharedPtr<FJsonObject> ResponseGraphRef = MakeEffectiveGraphRef(GraphAddress);

    Result->SetStringField(TEXT("graphType"), GraphType);
    Result->SetStringField(TEXT("assetPath"), AssetPath);
    Result->SetStringField(TEXT("graphName"), GraphName);
    Result->SetObjectField(TEXT("graphRef"), ResponseGraphRef);
    Result->SetStringField(TEXT("revision"), Revision);
    Result->SetObjectField(TEXT("semanticSnapshot"), Snapshot);
    Result->SetStringField(TEXT("nextCursor"), TEXT(""));

    TSharedPtr<FJsonObject> Meta = MakeShared<FJsonObject>();
    const int32 EffectiveTotalNodes =
        (!bBaseSnapshotRequest && GraphType.Equals(TEXT("blueprint")) && BlueprintListStats.TotalNodes > 0)
            ? BlueprintListStats.TotalNodes
            : Nodes.Num();
    const int32 EffectiveMatchingNodes =
        (!bBaseSnapshotRequest && GraphType.Equals(TEXT("blueprint")) && BlueprintListStats.MatchingNodes > 0)
            ? BlueprintListStats.MatchingNodes
            : AddedCount;

    Meta->SetNumberField(TEXT("totalNodes"), EffectiveTotalNodes);
    Meta->SetNumberField(TEXT("returnedNodes"), SnapshotNodes.Num());
    Meta->SetNumberField(TEXT("totalEdges"), Edges.Num());
    Meta->SetNumberField(TEXT("returnedEdges"), Edges.Num());
    Meta->SetBoolField(TEXT("truncated"), EffectiveMatchingNodes > (QueryShapeOptions.Offset + SnapshotNodes.Num()));
    Meta->SetObjectField(TEXT("layoutCapabilities"), MakeLayoutCapabilitiesObject());
    Meta->SetStringField(TEXT("layoutDetailRequested"), RequestedLayoutDetail);
    Meta->SetStringField(TEXT("layoutDetailApplied"), AppliedLayoutDetail);
    if (RequestedLayoutDetail.Equals(TEXT("measured")) && !AppliedLayoutDetail.Equals(TEXT("measured")))
    {
        TSharedPtr<FJsonObject> Diagnostic = MakeShared<FJsonObject>();
        Diagnostic->SetStringField(TEXT("code"), TEXT("LAYOUT_DETAIL_DOWNGRADED"));
        Diagnostic->SetStringField(TEXT("message"), TEXT("Requested measured layout detail is not yet supported for this graph query; returned basic layout data."));
        Diagnostic->SetStringField(TEXT("requestedDetail"), RequestedLayoutDetail);
        Diagnostic->SetStringField(TEXT("appliedDetail"), AppliedLayoutDetail);
        Diagnostics.Add(MakeShared<FJsonValueObject>(Diagnostic));
    }
    Result->SetObjectField(TEXT("meta"), Meta);
    Result->SetArrayField(TEXT("diagnostics"), Diagnostics);
    if (!bBaseSnapshotRequest && GraphType.Equals(TEXT("blueprint")))
    {
        const bool bTruncated = EffectiveMatchingNodes > (QueryShapeOptions.Offset + SnapshotNodes.Num());
        Result->SetStringField(TEXT("nextCursor"), bTruncated ? BuildGraphQueryCursor(QueryShapeOptions.Offset + SnapshotNodes.Num()) : TEXT(""));
    }
    return Result;
}

FString FLoomleBridgeModule::MakePendingGraphLayoutKey(const FString& GraphType, const FString& AssetPath, const FString& GraphName) const
{
    return FString::Printf(TEXT("%s|%s|%s"), *GraphType.ToLower(), *AssetPath, *GraphName);
}

void FLoomleBridgeModule::RecordPendingGraphLayoutNodes(
    const FString& GraphType,
    const FString& AssetPath,
    const FString& GraphName,
    const TArray<FString>& NodeIds)
{
    if (NodeIds.Num() == 0 || GraphType.IsEmpty() || AssetPath.IsEmpty())
    {
        return;
    }

    const FString Key = MakePendingGraphLayoutKey(GraphType, AssetPath, GraphName);
    FScopeLock ScopeLock(&PendingGraphLayoutStatesMutex);
    FPendingGraphLayoutState& State = PendingGraphLayoutStates.FindOrAdd(Key);
    State.GraphType = GraphType;
    State.AssetPath = AssetPath;
    State.GraphName = GraphName;
    State.UpdatedAtSeconds = FPlatformTime::Seconds();
    for (const FString& NodeId : NodeIds)
    {
        if (!NodeId.IsEmpty())
        {
            State.TouchedNodeIds.Add(NodeId);
        }
    }
}

bool FLoomleBridgeModule::ResolvePendingGraphLayoutNodes(
    const FString& GraphType,
    const FString& AssetPath,
    const FString& GraphName,
    TArray<FString>& OutNodeIds,
    bool bConsume)
{
    OutNodeIds.Reset();
    const FString Key = MakePendingGraphLayoutKey(GraphType, AssetPath, GraphName);
    FScopeLock ScopeLock(&PendingGraphLayoutStatesMutex);
    FPendingGraphLayoutState* State = PendingGraphLayoutStates.Find(Key);
    if (State == nullptr || State->TouchedNodeIds.Num() == 0)
    {
        return false;
    }

    for (const FString& NodeId : State->TouchedNodeIds)
    {
        OutNodeIds.Add(NodeId);
    }
    OutNodeIds.Sort();
    if (bConsume)
    {
        PendingGraphLayoutStates.Remove(Key);
    }
    return OutNodeIds.Num() > 0;
}

bool FLoomleBridgeModule::ApplyBlueprintLayout(
    const FString& AssetPath,
    const FString& GraphName,
    const FString& Scope,
    const TArray<FString>& RequestedNodeIds,
    TArray<FString>& OutMovedNodeIds,
    FString& OutError)
{
    OutMovedNodeIds.Reset();
    OutError.Empty();
    (void)GraphName;

    UBlueprint* Blueprint = LoadBlueprintByAssetPath(AssetPath);
    UEdGraph* TargetGraph = ResolveBlueprintGraph(Blueprint, GraphName);
    if (Blueprint == nullptr || TargetGraph == nullptr)
    {
        OutError = TEXT("Failed to resolve blueprint/target graph for layout.");
        return false;
    }

    auto ResolveNodeToken = [](UEdGraph* Graph, const FString& NodeToken) -> UEdGraphNode*
    {
        if (Graph == nullptr || NodeToken.IsEmpty())
        {
            return nullptr;
        }
        if (UEdGraphNode* Node = FindNodeByGuid(Graph, NodeToken))
        {
            return Node;
        }
        for (UEdGraphNode* Node : Graph->Nodes)
        {
            if (Node == nullptr)
            {
                continue;
            }
            if (Node->GetPathName().Equals(NodeToken, ESearchCase::IgnoreCase)
                || Node->GetName().Equals(NodeToken, ESearchCase::IgnoreCase))
            {
                return Node;
            }
        }
        return nullptr;
    };

    auto IsExecPin = [](const UEdGraphPin* Pin) -> bool
    {
        return Pin != nullptr && Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec;
    };

    auto EstimateNodeSize = [](const UEdGraphNode* Node) -> FVector2D
    {
        if (const UEdGraphNode_Comment* CommentNode = Cast<UEdGraphNode_Comment>(Node))
        {
            return FVector2D(
                FMath::Max(160.0f, static_cast<float>(CommentNode->NodeWidth)),
                FMath::Max(120.0f, static_cast<float>(CommentNode->NodeHeight)));
        }
        return FVector2D(280.0f, 160.0f);
    };

    TArray<UEdGraphNode*> MovableNodes;
    if (Scope.Equals(TEXT("all")))
    {
        for (UEdGraphNode* Node : TargetGraph->Nodes)
        {
            if (Node != nullptr)
            {
                MovableNodes.Add(Node);
            }
        }
    }
    else if (Scope.Equals(TEXT("touched")))
    {
        for (const FString& NodeId : RequestedNodeIds)
        {
            if (UEdGraphNode* Node = ResolveNodeToken(TargetGraph, NodeId))
            {
                MovableNodes.AddUnique(Node);
            }
        }
    }
    else
    {
        OutError = FString::Printf(TEXT("Unsupported layout scope for Blueprint: %s"), *Scope);
        return false;
    }

    if (MovableNodes.Num() == 0)
    {
        OutError = Scope.Equals(TEXT("touched"))
            ? TEXT("No touched nodes are pending for layout.")
            : TEXT("No nodes available for layout.");
        return false;
    }

    TSet<FString> MovableNodeIds;
    for (UEdGraphNode* Node : MovableNodes)
    {
        if (Node != nullptr)
        {
            MovableNodeIds.Add(Node->NodeGuid.ToString(EGuidFormats::DigitsWithHyphensLower));
        }
    }

    TArray<FBox2D> OccupiedRects;
    OccupiedRects.Reserve(TargetGraph->Nodes.Num());
    for (UEdGraphNode* Node : TargetGraph->Nodes)
    {
        if (Node == nullptr)
        {
            continue;
        }
        const FString NodeId = Node->NodeGuid.ToString(EGuidFormats::DigitsWithHyphensLower);
        if (MovableNodeIds.Contains(NodeId))
        {
            continue;
        }

        const FVector2D Size = EstimateNodeSize(Node);
        OccupiedRects.Add(FBox2D(
            FVector2D(Node->NodePosX, Node->NodePosY),
            FVector2D(Node->NodePosX + Size.X, Node->NodePosY + Size.Y)));
    }

    TMap<UEdGraphNode*, int32> DepthByNode;
    TMap<UEdGraphNode*, TArray<UEdGraphNode*>> ExecSuccessors;
    TMap<UEdGraphNode*, float> PreferredYByNode;
    TMap<UEdGraphNode*, int32> ExecBranchBiasByNode;
    TSet<UEdGraphNode*> ExecNodes;
    TSet<UEdGraphNode*> CommentNodes;
    for (UEdGraphNode* Node : MovableNodes)
    {
        DepthByNode.Add(Node, TNumericLimits<int32>::Min());
        ExecSuccessors.Add(Node, TArray<UEdGraphNode*>());
        PreferredYByNode.Add(Node, static_cast<float>(Node->NodePosY));
        ExecBranchBiasByNode.Add(Node, 0);
        if (Cast<UEdGraphNode_Comment>(Node) != nullptr)
        {
            CommentNodes.Add(Node);
        }
    }

    for (UEdGraphNode* Node : MovableNodes)
    {
        bool bHasIncomingExecFromMovable = false;
        bool bHasAnyExecPin = false;
        float PreferredExecY = 0.0f;
        int32 PreferredExecYCount = 0;
        for (UEdGraphPin* Pin : Node->Pins)
        {
            if (!IsExecPin(Pin))
            {
                continue;
            }

             bHasAnyExecPin = true;

            if (Pin->Direction == EGPD_Input)
            {
                for (UEdGraphPin* LinkedPin : Pin->LinkedTo)
                {
                    UEdGraphNode* LinkedNode = LinkedPin ? LinkedPin->GetOwningNodeUnchecked() : nullptr;
                    if (LinkedNode != nullptr && DepthByNode.Contains(LinkedNode))
                    {
                        bHasIncomingExecFromMovable = true;
                        PreferredExecY += static_cast<float>(LinkedNode->NodePosY);
                        ++PreferredExecYCount;
                    }
                }
            }
            else if (Pin->Direction == EGPD_Output)
            {
                TArray<UEdGraphNode*>& Successors = ExecSuccessors.FindChecked(Node);
                for (UEdGraphPin* LinkedPin : Pin->LinkedTo)
                {
                    UEdGraphNode* LinkedNode = LinkedPin ? LinkedPin->GetOwningNodeUnchecked() : nullptr;
                    if (LinkedNode != nullptr && DepthByNode.Contains(LinkedNode))
                    {
                        Successors.AddUnique(LinkedNode);
                        const FString NormalizedPinName = Pin->PinName.ToString().ToLower();
                        if (NormalizedPinName.Equals(TEXT("then")) || NormalizedPinName.Equals(TEXT("true")))
                        {
                            ExecBranchBiasByNode.FindOrAdd(LinkedNode) = 0;
                        }
                        else if (NormalizedPinName.Equals(TEXT("else")) || NormalizedPinName.Equals(TEXT("false")))
                        {
                            ExecBranchBiasByNode.FindOrAdd(LinkedNode) = 1;
                        }
                    }
                }
            }
        }

        if (bHasAnyExecPin)
        {
            ExecNodes.Add(Node);
        }

        if (PreferredExecYCount > 0)
        {
            PreferredYByNode[Node] = PreferredExecY / static_cast<float>(PreferredExecYCount);
        }

        if (!bHasIncomingExecFromMovable)
        {
            DepthByNode[Node] = 0;
        }
    }

    bool bProgress = true;
    while (bProgress)
    {
        bProgress = false;
        for (const TPair<UEdGraphNode*, TArray<UEdGraphNode*>>& Pair : ExecSuccessors)
        {
            const int32* SourceDepth = DepthByNode.Find(Pair.Key);
            if (SourceDepth == nullptr || *SourceDepth == TNumericLimits<int32>::Min())
            {
                continue;
            }

            for (UEdGraphNode* Successor : Pair.Value)
            {
                int32& CurrentDepth = DepthByNode.FindChecked(Successor);
                if (CurrentDepth < (*SourceDepth + 1))
                {
                    CurrentDepth = *SourceDepth + 1;
                    bProgress = true;
                }
            }
        }
    }

    for (int32 Pass = 0; Pass < 4; ++Pass)
    {
        bool bPassProgress = false;
        for (UEdGraphNode* Node : MovableNodes)
        {
            int32& CurrentDepth = DepthByNode.FindChecked(Node);
            if (CurrentDepth != TNumericLimits<int32>::Min())
            {
                continue;
            }

            TOptional<int32> CandidateDepth;
            for (UEdGraphPin* Pin : Node->Pins)
            {
                if (Pin == nullptr || IsExecPin(Pin))
                {
                    continue;
                }

                for (UEdGraphPin* LinkedPin : Pin->LinkedTo)
                {
                    UEdGraphNode* LinkedNode = LinkedPin ? LinkedPin->GetOwningNodeUnchecked() : nullptr;
                    if (LinkedNode == nullptr || !DepthByNode.Contains(LinkedNode))
                    {
                        continue;
                    }

                    const int32 LinkedDepth = DepthByNode.FindChecked(LinkedNode);
                    if (LinkedDepth == TNumericLimits<int32>::Min())
                    {
                        continue;
                    }

                    int32 ProposedDepth = LinkedDepth;
                    if (Pin->Direction == EGPD_Output)
                    {
                        ProposedDepth = LinkedDepth - 1;
                    }
                    else if (Pin->Direction == EGPD_Input)
                    {
                        ProposedDepth = LinkedDepth + 1;
                    }

                    CandidateDepth = CandidateDepth.IsSet()
                        ? TOptional<int32>(FMath::Min(CandidateDepth.GetValue(), ProposedDepth))
                        : TOptional<int32>(ProposedDepth);
                }
            }

            if (CandidateDepth.IsSet())
            {
                CurrentDepth = CandidateDepth.GetValue();
                bPassProgress = true;
            }
        }

        if (!bPassProgress)
        {
            break;
        }
    }

    for (UEdGraphNode* Node : MovableNodes)
    {
        float WeightedY = static_cast<float>(Node->NodePosY);
        float WeightTotal = 1.0f;

        for (UEdGraphPin* Pin : Node->Pins)
        {
            if (Pin == nullptr)
            {
                continue;
            }

            for (UEdGraphPin* LinkedPin : Pin->LinkedTo)
            {
                UEdGraphNode* LinkedNode = LinkedPin ? LinkedPin->GetOwningNodeUnchecked() : nullptr;
                if (LinkedNode == nullptr || !PreferredYByNode.Contains(LinkedNode))
                {
                    continue;
                }

                const float LinkedY = PreferredYByNode.FindChecked(LinkedNode);
                const bool bExecAffinity = IsExecPin(Pin) || IsExecPin(LinkedPin);
                const float Weight = bExecAffinity ? 2.0f : 1.0f;
                WeightedY += LinkedY * Weight;
                WeightTotal += Weight;
            }
        }

        PreferredYByNode[Node] = WeightedY / WeightTotal;
        if (ExecNodes.Contains(Node))
        {
            const int32 BranchBias = ExecBranchBiasByNode.Contains(Node) ? ExecBranchBiasByNode.FindChecked(Node) : 0;
            PreferredYByNode[Node] += static_cast<float>(BranchBias * 96);
        }
    }

    int32 MinDepth = 0;
    bool bFoundAssignedDepth = false;
    for (const TPair<UEdGraphNode*, int32>& Pair : DepthByNode)
    {
        if (Pair.Value == TNumericLimits<int32>::Min())
        {
            continue;
        }
        MinDepth = bFoundAssignedDepth ? FMath::Min(MinDepth, Pair.Value) : Pair.Value;
        bFoundAssignedDepth = true;
    }

    TMap<UEdGraphNode*, int32> NormalizedDepthByNode;
    TMap<int32, TArray<UEdGraphNode*>> NodesByDepth;
    int32 BaseX = TNumericLimits<int32>::Max();
    int32 BaseY = TNumericLimits<int32>::Max();
    for (UEdGraphNode* Node : MovableNodes)
    {
        int32 Depth = DepthByNode.FindChecked(Node);
        if (Depth == TNumericLimits<int32>::Min())
        {
            Depth = 0;
        }
        Depth -= MinDepth;
        NormalizedDepthByNode.Add(Node, Depth);
        NodesByDepth.FindOrAdd(Depth).Add(Node);
        BaseX = FMath::Min(BaseX, Node->NodePosX);
        BaseY = FMath::Min(BaseY, Node->NodePosY);
    }

    if (BaseX == TNumericLimits<int32>::Max())
    {
        BaseX = 0;
    }
    if (BaseY == TNumericLimits<int32>::Max())
    {
        BaseY = 0;
    }

    const int32 ColumnSpacing = 320;
    const int32 RowSpacing = 220;
    const int32 ExecSubtreeGap = 64;
    const int32 RootGap = 96;

    TMap<UEdGraphNode*, TArray<UEdGraphNode*>> OrderedExecChildren;
    TMap<UEdGraphNode*, int32> IncomingExecCount;
    for (UEdGraphNode* Node : ExecNodes)
    {
        OrderedExecChildren.Add(Node, TArray<UEdGraphNode*>());
        IncomingExecCount.Add(Node, 0);
    }

    for (UEdGraphNode* Node : ExecNodes)
    {
        TArray<UEdGraphNode*>& Children = OrderedExecChildren.FindChecked(Node);
        for (UEdGraphPin* Pin : Node->Pins)
        {
            if (!IsExecPin(Pin) || Pin->Direction != EGPD_Output)
            {
                continue;
            }

            for (UEdGraphPin* LinkedPin : Pin->LinkedTo)
                {
                    UEdGraphNode* ChildNode = LinkedPin ? LinkedPin->GetOwningNodeUnchecked() : nullptr;
                    if (ChildNode == nullptr || !ExecNodes.Contains(ChildNode))
                    {
                        continue;
                }

                if (!Children.Contains(ChildNode))
                {
                    Children.Add(ChildNode);
                    int32& IncomingCount = IncomingExecCount.FindOrAdd(ChildNode);
                    ++IncomingCount;
                }
            }
        }

        Children.Sort([&ExecBranchBiasByNode, &PreferredYByNode](const UEdGraphNode& A, const UEdGraphNode& B)
        {
            const int32 BiasA = ExecBranchBiasByNode.Contains(&A) ? ExecBranchBiasByNode.FindChecked(&A) : 0;
            const int32 BiasB = ExecBranchBiasByNode.Contains(&B) ? ExecBranchBiasByNode.FindChecked(&B) : 0;
            if (BiasA != BiasB)
            {
                return BiasA < BiasB;
            }

            const float YA = PreferredYByNode.Contains(&A) ? PreferredYByNode.FindChecked(&A) : static_cast<float>(A.NodePosY);
            const float YB = PreferredYByNode.Contains(&B) ? PreferredYByNode.FindChecked(&B) : static_cast<float>(B.NodePosY);
            if (!FMath::IsNearlyEqual(YA, YB, 1.0f))
            {
                return YA < YB;
            }
            return A.NodePosX < B.NodePosX;
        });
    }

    TMap<UEdGraphNode*, float> ExecSubtreeHeightByNode;
    TSet<UEdGraphNode*> ExecHeightVisiting;
    TFunction<float(UEdGraphNode*)> ComputeExecSubtreeHeight = [&](UEdGraphNode* Node) -> float
    {
        if (Node == nullptr)
        {
            return 0.0f;
        }
        if (const float* CachedHeight = ExecSubtreeHeightByNode.Find(Node))
        {
            return *CachedHeight;
        }
        if (ExecHeightVisiting.Contains(Node))
        {
            return EstimateNodeSize(Node).Y;
        }

        ExecHeightVisiting.Add(Node);
        const float NodeHeight = EstimateNodeSize(Node).Y;
        const TArray<UEdGraphNode*>* Children = OrderedExecChildren.Find(Node);
        float ResultHeight = NodeHeight;
        if (Children != nullptr && Children->Num() > 0)
        {
            float ChildrenHeight = 0.0f;
            for (int32 Index = 0; Index < Children->Num(); ++Index)
            {
                if (Index > 0)
                {
                    ChildrenHeight += static_cast<float>(ExecSubtreeGap);
                }
                ChildrenHeight += ComputeExecSubtreeHeight((*Children)[Index]);
            }
            ResultHeight = FMath::Max(NodeHeight, ChildrenHeight);
        }

        ExecHeightVisiting.Remove(Node);
        ExecSubtreeHeightByNode.Add(Node, ResultHeight);
        return ResultHeight;
    };

    TMap<UEdGraphNode*, FVector2D> PlannedExecPositions;
    TSet<UEdGraphNode*> ExecLayoutVisited;
    TFunction<void(UEdGraphNode*, float)> LayoutExecSubtree = [&](UEdGraphNode* Node, float TopY)
    {
        if (Node == nullptr || ExecLayoutVisited.Contains(Node))
        {
            return;
        }

        ExecLayoutVisited.Add(Node);
        const float SubtreeHeight = ComputeExecSubtreeHeight(Node);
        const FVector2D NodeSize = EstimateNodeSize(Node);
        const int32 Depth = NormalizedDepthByNode.Contains(Node) ? NormalizedDepthByNode.FindChecked(Node) : 0;
        const float NodeX = static_cast<float>(BaseX + (Depth * ColumnSpacing));
        const float NodeY = TopY;
        PlannedExecPositions.Add(Node, FVector2D(NodeX, NodeY));

        const TArray<UEdGraphNode*>* Children = OrderedExecChildren.Find(Node);
        if (Children == nullptr || Children->Num() == 0)
        {
            return;
        }

        float ChildTopY = TopY;
        for (UEdGraphNode* ChildNode : *Children)
        {
            LayoutExecSubtree(ChildNode, ChildTopY);
            ChildTopY += ComputeExecSubtreeHeight(ChildNode) + static_cast<float>(ExecSubtreeGap);
        }
    };

    TArray<UEdGraphNode*> ExecRoots;
    for (UEdGraphNode* Node : ExecNodes)
    {
        const int32 IncomingCount = IncomingExecCount.Contains(Node) ? IncomingExecCount.FindChecked(Node) : 0;
        if (IncomingCount == 0)
        {
            ExecRoots.Add(Node);
        }
    }
    ExecRoots.Sort([&PreferredYByNode](const UEdGraphNode& A, const UEdGraphNode& B)
    {
        const float YA = PreferredYByNode.Contains(&A) ? PreferredYByNode.FindChecked(&A) : static_cast<float>(A.NodePosY);
        const float YB = PreferredYByNode.Contains(&B) ? PreferredYByNode.FindChecked(&B) : static_cast<float>(B.NodePosY);
        if (!FMath::IsNearlyEqual(YA, YB, 1.0f))
        {
            return YA < YB;
        }
        return A.NodePosX < B.NodePosX;
    });

    float RootTopY = static_cast<float>(BaseY);
    for (UEdGraphNode* RootNode : ExecRoots)
    {
        LayoutExecSubtree(RootNode, RootTopY);
        RootTopY += ComputeExecSubtreeHeight(RootNode) + static_cast<float>(RootGap);
    }

    for (UEdGraphNode* ExecNode : ExecNodes)
    {
        if (!ExecLayoutVisited.Contains(ExecNode))
        {
            LayoutExecSubtree(ExecNode, RootTopY);
            RootTopY += ComputeExecSubtreeHeight(ExecNode) + static_cast<float>(RootGap);
        }
    }

    for (const TPair<UEdGraphNode*, FVector2D>& Pair : PlannedExecPositions)
    {
        UEdGraphNode* Node = Pair.Key;
        const FVector2D Size = EstimateNodeSize(Node);
        const int32 TargetX = FMath::RoundToInt(Pair.Value.X);
        int32 TargetY = FMath::RoundToInt(Pair.Value.Y);
        FBox2D CandidateRect(
            FVector2D(TargetX, TargetY),
            FVector2D(TargetX + Size.X, TargetY + Size.Y));

        bool bOverlaps = true;
        while (bOverlaps)
        {
            bOverlaps = false;
            for (const FBox2D& OccupiedRect : OccupiedRects)
            {
                if (CandidateRect.Intersect(OccupiedRect))
                {
                    TargetY += RowSpacing;
                    CandidateRect = FBox2D(
                        FVector2D(TargetX, TargetY),
                        FVector2D(TargetX + Size.X, TargetY + Size.Y));
                    bOverlaps = true;
                    break;
                }
            }
        }

        if (Node->NodePosX != TargetX || Node->NodePosY != TargetY)
        {
            FString MoveError;
            const FString NodeId = Node->NodeGuid.ToString(EGuidFormats::DigitsWithHyphensLower);
            if (!FLoomleBlueprintAdapter::MoveNode(AssetPath, GraphName, NodeId, TargetX, TargetY, MoveError))
            {
                OutError = MoveError.IsEmpty() ? TEXT("Failed to move exec node during layout.") : MoveError;
                return false;
            }
            OutMovedNodeIds.Add(NodeId);
        }

        OccupiedRects.Add(CandidateRect);
    }

    TArray<int32> SortedDepths;
    NodesByDepth.GetKeys(SortedDepths);
    SortedDepths.Sort();

    for (int32 Depth : SortedDepths)
    {
        TArray<UEdGraphNode*>& ColumnNodes = NodesByDepth.FindChecked(Depth);
        ColumnNodes.Sort([&PreferredYByNode, &ExecNodes, &CommentNodes](const UEdGraphNode& A, const UEdGraphNode& B)
        {
            const bool bAIsComment = CommentNodes.Contains(&A);
            const bool bBIsComment = CommentNodes.Contains(&B);
            if (bAIsComment != bBIsComment)
            {
                return !bAIsComment;
            }

            const bool bAIsExec = ExecNodes.Contains(&A);
            const bool bBIsExec = ExecNodes.Contains(&B);
            if (bAIsExec != bBIsExec)
            {
                return bAIsExec;
            }

            const float PreferredYA = PreferredYByNode.Contains(&A)
                ? PreferredYByNode.FindChecked(&A)
                : static_cast<float>(A.NodePosY);
            const float PreferredYB = PreferredYByNode.Contains(&B)
                ? PreferredYByNode.FindChecked(&B)
                : static_cast<float>(B.NodePosY);
            if (!FMath::IsNearlyEqual(PreferredYA, PreferredYB, 1.0f))
            {
                return PreferredYA < PreferredYB;
            }

            if (A.NodePosY != B.NodePosY)
            {
                return A.NodePosY < B.NodePosY;
            }
            return A.NodePosX < B.NodePosX;
        });

        int32 RowIndex = 0;
        for (UEdGraphNode* Node : ColumnNodes)
        {
            if (ExecNodes.Contains(Node))
            {
                continue;
            }

            const FVector2D Size = EstimateNodeSize(Node);
            const int32 TargetX = BaseX + (Depth * ColumnSpacing);
            int32 TargetY = BaseY + (RowIndex * RowSpacing);
            FBox2D CandidateRect(
                FVector2D(TargetX, TargetY),
                FVector2D(TargetX + Size.X, TargetY + Size.Y));

            bool bOverlaps = true;
            while (bOverlaps)
            {
                bOverlaps = false;
                for (const FBox2D& OccupiedRect : OccupiedRects)
                {
                    if (CandidateRect.Intersect(OccupiedRect))
                    {
                        TargetY += RowSpacing;
                        CandidateRect = FBox2D(
                            FVector2D(TargetX, TargetY),
                            FVector2D(TargetX + Size.X, TargetY + Size.Y));
                        bOverlaps = true;
                        break;
                    }
                }
            }

            if (Node->NodePosX != TargetX || Node->NodePosY != TargetY)
            {
                FString MoveError;
                const FString NodeId = Node->NodeGuid.ToString(EGuidFormats::DigitsWithHyphensLower);
                if (!FLoomleBlueprintAdapter::MoveNode(AssetPath, GraphName, NodeId, TargetX, TargetY, MoveError))
                {
                    OutError = MoveError.IsEmpty() ? TEXT("Failed to move node during layout.") : MoveError;
                    return false;
                }
                OutMovedNodeIds.Add(NodeId);
            }

            OccupiedRects.Add(CandidateRect);
            ++RowIndex;
        }
    }

    return true;
}

bool FLoomleBridgeModule::ApplyMaterialLayout(
    const FString& AssetPath,
    const FString& GraphName,
    const FString& Scope,
    const TArray<FString>& RequestedNodeIds,
    TArray<FString>& OutMovedNodeIds,
    FString& OutError)
{
    OutMovedNodeIds.Reset();
    OutError.Empty();
    (void)GraphName;

    UMaterial* MaterialAsset = LoadMaterialByAssetPath(AssetPath);
    if (MaterialAsset == nullptr)
    {
        OutError = TEXT("Material asset not found.");
        return false;
    }
    if (MaterialAsset->MaterialGraph == nullptr)
    {
        MaterialAsset->MaterialGraph = CastChecked<UMaterialGraph>(
            FBlueprintEditorUtils::CreateNewGraph(
                MaterialAsset,
                NAME_None,
                UMaterialGraph::StaticClass(),
                UMaterialGraphSchema::StaticClass()));
    }
    if (MaterialAsset->MaterialGraph != nullptr)
    {
        MaterialAsset->MaterialGraph->Material = MaterialAsset;
        MaterialAsset->MaterialGraph->MaterialFunction = nullptr;
        MaterialAsset->MaterialGraph->RebuildGraph();
    }
    UMaterialGraph* MaterialGraph = MaterialAsset->MaterialGraph;
    UMaterialGraphNode_Root* RootNode = MaterialGraph ? MaterialGraph->RootNode : nullptr;

    TArray<UMaterialExpression*> AllExpressions;
    for (UMaterialExpression* Expression : MaterialAsset->GetExpressions())
    {
        if (Expression != nullptr)
        {
            AllExpressions.Add(Expression);
        }
    }

    auto ResolveExpressionToken = [&AllExpressions](const FString& NodeToken) -> UMaterialExpression*
    {
        if (NodeToken.IsEmpty())
        {
            return nullptr;
        }

        for (UMaterialExpression* Expression : AllExpressions)
        {
            if (Expression == nullptr)
            {
                continue;
            }

            const FString ExpressionId = MaterialExpressionId(Expression);
            if (ExpressionId.Equals(NodeToken, ESearchCase::IgnoreCase)
                || Expression->GetPathName().Equals(NodeToken, ESearchCase::IgnoreCase)
                || Expression->GetName().Equals(NodeToken, ESearchCase::IgnoreCase))
            {
                return Expression;
            }
        }

        return nullptr;
    };

    auto EstimateNodeSize = [](const UMaterialExpression* Expression) -> FVector2D
    {
        if (Expression == nullptr)
        {
            return FVector2D(240.0f, 160.0f);
        }

        const FString ClassName = Expression->GetClass() ? Expression->GetClass()->GetName() : TEXT("");
        if (ClassName.Contains(TEXT("Texture"), ESearchCase::IgnoreCase))
        {
            return FVector2D(320.0f, 220.0f);
        }
        if (ClassName.Contains(TEXT("Parameter"), ESearchCase::IgnoreCase))
        {
            return FVector2D(260.0f, 180.0f);
        }
        return FVector2D(240.0f, 160.0f);
    };

    TMap<UMaterialExpression*, TArray<UMaterialExpression*>> Predecessors;
    TMap<UMaterialExpression*, TArray<UMaterialExpression*>> Successors;
    TSet<UMaterialExpression*> RootSinkExpressions;
    for (UMaterialExpression* Expression : AllExpressions)
    {
        if (Expression == nullptr)
        {
            continue;
        }
        Predecessors.Add(Expression, TArray<UMaterialExpression*>());
        Successors.Add(Expression, TArray<UMaterialExpression*>());
    }

    for (UMaterialExpression* Expression : AllExpressions)
    {
        if (Expression == nullptr)
        {
            continue;
        }

        const int32 MaxInputs = 128;
        for (int32 InputIndex = 0; InputIndex < MaxInputs; ++InputIndex)
        {
            FExpressionInput* Input = Expression->GetInput(InputIndex);
            if (Input == nullptr)
            {
                break;
            }

            if (UMaterialExpression* SourceExpression = Input->Expression)
            {
                Predecessors.FindChecked(Expression).AddUnique(SourceExpression);
                if (Successors.Contains(SourceExpression))
                {
                    Successors.FindChecked(SourceExpression).AddUnique(Expression);
                }
            }
        }
    }

    if (RootNode != nullptr)
    {
        for (UEdGraphPin* Pin : RootNode->Pins)
        {
            if (Pin == nullptr || Pin->Direction != EGPD_Input)
            {
                continue;
            }

            for (UEdGraphPin* LinkedPin : Pin->LinkedTo)
            {
                UEdGraphNode* LinkedNode = LinkedPin ? LinkedPin->GetOwningNodeUnchecked() : nullptr;
                UMaterialGraphNode* MaterialGraphNode = Cast<UMaterialGraphNode>(LinkedNode);
                UMaterialExpression* LinkedExpression = MaterialGraphNode ? MaterialGraphNode->MaterialExpression : nullptr;
                if (LinkedExpression != nullptr)
                {
                    RootSinkExpressions.Add(LinkedExpression);
                }
            }
        }
    }

    TSet<UMaterialExpression*> MovableExpressions;
    if (Scope.Equals(TEXT("all")))
    {
        for (UMaterialExpression* Expression : AllExpressions)
        {
            if (Expression != nullptr)
            {
                MovableExpressions.Add(Expression);
            }
        }
    }
    else if (Scope.Equals(TEXT("touched")))
    {
        for (const FString& NodeId : RequestedNodeIds)
        {
            if (UMaterialExpression* Expression = ResolveExpressionToken(NodeId))
            {
                MovableExpressions.Add(Expression);
            }
        }

        TArray<UMaterialExpression*> SeedExpressions = MovableExpressions.Array();
        for (UMaterialExpression* Expression : SeedExpressions)
        {
            if (Expression == nullptr)
            {
                continue;
            }

            if (const TArray<UMaterialExpression*>* Inputs = Predecessors.Find(Expression))
            {
                for (UMaterialExpression* Neighbor : *Inputs)
                {
                    if (Neighbor != nullptr)
                    {
                        MovableExpressions.Add(Neighbor);
                    }
                }
            }
            if (const TArray<UMaterialExpression*>* Outputs = Successors.Find(Expression))
            {
                for (UMaterialExpression* Neighbor : *Outputs)
                {
                    if (Neighbor != nullptr)
                    {
                        MovableExpressions.Add(Neighbor);
                    }
                }
            }
        }
    }
    else
    {
        OutError = FString::Printf(TEXT("Unsupported layout scope for material: %s"), *Scope);
        return false;
    }

    if (MovableExpressions.Num() == 0)
    {
        OutError = Scope.Equals(TEXT("touched"))
            ? TEXT("No touched nodes are pending for layout.")
            : TEXT("No material expressions available for layout.");
        return false;
    }

    TSet<FString> MovableNodeIds;
    for (UMaterialExpression* Expression : MovableExpressions)
    {
        if (Expression != nullptr)
        {
            MovableNodeIds.Add(MaterialExpressionId(Expression));
        }
    }

    TArray<FBox2D> OccupiedRects;
    int32 BaseX = TNumericLimits<int32>::Max();
    int32 BaseY = TNumericLimits<int32>::Max();
    for (UMaterialExpression* Expression : AllExpressions)
    {
        if (Expression == nullptr)
        {
            continue;
        }

        if (MovableExpressions.Contains(Expression))
        {
            BaseX = FMath::Min(BaseX, Expression->MaterialExpressionEditorX);
            BaseY = FMath::Min(BaseY, Expression->MaterialExpressionEditorY);
            continue;
        }

        const FVector2D Size = EstimateNodeSize(Expression);
        OccupiedRects.Add(FBox2D(
            FVector2D(Expression->MaterialExpressionEditorX, Expression->MaterialExpressionEditorY),
            FVector2D(Expression->MaterialExpressionEditorX + Size.X, Expression->MaterialExpressionEditorY + Size.Y)));
    }

    int32 RootOriginalX = 0;
    int32 RootOriginalY = 0;
    if (RootNode != nullptr)
    {
        RootOriginalX = RootNode->NodePosX;
        RootOriginalY = RootNode->NodePosY;
        BaseX = FMath::Min(BaseX, RootOriginalX - 720);
        BaseY = FMath::Min(BaseY, RootOriginalY);
        const FVector2D RootSize(420.0f, 1040.0f);
        OccupiedRects.Add(FBox2D(
            FVector2D(RootOriginalX, RootOriginalY),
            FVector2D(RootOriginalX + RootSize.X, RootOriginalY + RootSize.Y)));
    }

    if (BaseX == TNumericLimits<int32>::Max())
    {
        BaseX = 0;
    }
    if (BaseY == TNumericLimits<int32>::Max())
    {
        BaseY = 0;
    }

    TMap<UMaterialExpression*, int32> ReverseDepthByNode;
    TMap<UMaterialExpression*, float> PreferredYByNode;
    for (UMaterialExpression* Expression : MovableExpressions)
    {
        ReverseDepthByNode.Add(Expression, TNumericLimits<int32>::Min());
        PreferredYByNode.Add(Expression, static_cast<float>(Expression->MaterialExpressionEditorY));
    }

    for (UMaterialExpression* Expression : MovableExpressions)
    {
        const TArray<UMaterialExpression*>* Outputs = Successors.Find(Expression);
        bool bHasMovableSuccessor = false;
        if (RootSinkExpressions.Contains(Expression))
        {
            ReverseDepthByNode[Expression] = 0;
            continue;
        }
        if (Outputs != nullptr)
        {
            for (UMaterialExpression* Successor : *Outputs)
            {
                if (MovableExpressions.Contains(Successor))
                {
                    bHasMovableSuccessor = true;
                    break;
                }
            }
        }

        if (!bHasMovableSuccessor)
        {
            ReverseDepthByNode[Expression] = 0;
        }
    }

    bool bProgress = true;
    while (bProgress)
    {
        bProgress = false;
        for (UMaterialExpression* Expression : MovableExpressions)
        {
            const TArray<UMaterialExpression*>* Inputs = Predecessors.Find(Expression);
            if (Inputs == nullptr)
            {
                continue;
            }

            const int32 CurrentDepth = ReverseDepthByNode.FindChecked(Expression);
            if (CurrentDepth == TNumericLimits<int32>::Min())
            {
                continue;
            }

            for (UMaterialExpression* InputExpression : *Inputs)
            {
                if (InputExpression == nullptr || !MovableExpressions.Contains(InputExpression))
                {
                    continue;
                }

                int32& InputDepth = ReverseDepthByNode.FindChecked(InputExpression);
                if (InputDepth < CurrentDepth + 1)
                {
                    InputDepth = CurrentDepth + 1;
                    bProgress = true;
                }
            }
        }
    }

    int32 MaxDepth = 0;
    for (UMaterialExpression* Expression : MovableExpressions)
    {
        int32& Depth = ReverseDepthByNode.FindChecked(Expression);
        if (Depth == TNumericLimits<int32>::Min())
        {
            Depth = 0;
        }
        MaxDepth = FMath::Max(MaxDepth, Depth);

        float WeightedY = static_cast<float>(Expression->MaterialExpressionEditorY);
        float WeightTotal = 1.0f;
        if (const TArray<UMaterialExpression*>* Inputs = Predecessors.Find(Expression))
        {
            for (UMaterialExpression* Neighbor : *Inputs)
            {
                if (Neighbor != nullptr && MovableExpressions.Contains(Neighbor))
                {
                    WeightedY += static_cast<float>(Neighbor->MaterialExpressionEditorY);
                    WeightTotal += 1.0f;
                }
            }
        }
        if (const TArray<UMaterialExpression*>* Outputs = Successors.Find(Expression))
        {
            for (UMaterialExpression* Neighbor : *Outputs)
            {
                if (Neighbor != nullptr && MovableExpressions.Contains(Neighbor))
                {
                    WeightedY += static_cast<float>(Neighbor->MaterialExpressionEditorY);
                    WeightTotal += 1.5f;
                }
            }
        }
        PreferredYByNode[Expression] = WeightedY / WeightTotal;
    }

    TMap<int32, TArray<UMaterialExpression*>> NodesByColumn;
    for (UMaterialExpression* Expression : MovableExpressions)
    {
        const int32 Column = MaxDepth - ReverseDepthByNode.FindChecked(Expression);
        NodesByColumn.FindOrAdd(Column).Add(Expression);
    }

    auto MaterialCategoryRank = [](const UMaterialExpression* Expression) -> int32
    {
        const FString ClassName = Expression && Expression->GetClass() ? Expression->GetClass()->GetName() : TEXT("");
        if (ClassName.Contains(TEXT("Parameter"), ESearchCase::IgnoreCase))
        {
            return 0;
        }
        if (ClassName.Contains(TEXT("Constant"), ESearchCase::IgnoreCase))
        {
            return 1;
        }
        if (ClassName.Contains(TEXT("Texture"), ESearchCase::IgnoreCase))
        {
            return 2;
        }
        return 3;
    };

    TArray<int32> SortedColumns;
    NodesByColumn.GetKeys(SortedColumns);
    SortedColumns.Sort();

    const int32 ColumnSpacing = 360;
    const int32 RowSpacing = 224;
    const int32 GridSize = 16;
    const int32 RootReservedGap = 352;
    const int32 MaxColumnIndex = SortedColumns.Num() > 0 ? SortedColumns.Last() : 0;

    for (int32 Column : SortedColumns)
    {
        TArray<UMaterialExpression*>& ColumnNodes = NodesByColumn.FindChecked(Column);
        ColumnNodes.Sort([&PreferredYByNode, &MaterialCategoryRank](const UMaterialExpression& A, const UMaterialExpression& B)
        {
            const int32 RankA = MaterialCategoryRank(&A);
            const int32 RankB = MaterialCategoryRank(&B);
            if (RankA != RankB)
            {
                return RankA < RankB;
            }

            const float YA = PreferredYByNode.Contains(&A) ? PreferredYByNode.FindChecked(&A) : static_cast<float>(A.MaterialExpressionEditorY);
            const float YB = PreferredYByNode.Contains(&B) ? PreferredYByNode.FindChecked(&B) : static_cast<float>(B.MaterialExpressionEditorY);
            if (!FMath::IsNearlyEqual(YA, YB, 1.0f))
            {
                return YA < YB;
            }
            return A.MaterialExpressionEditorX < B.MaterialExpressionEditorX;
        });

        for (int32 Index = 0; Index < ColumnNodes.Num(); ++Index)
        {
            UMaterialExpression* Expression = ColumnNodes[Index];
            const FVector2D Size = EstimateNodeSize(Expression);
            int32 TargetX = BaseX + (Column * ColumnSpacing);
            if (RootNode != nullptr)
            {
                const int32 ColumnsFromSink = MaxColumnIndex - Column;
                TargetX = RootOriginalX - RootReservedGap - (ColumnsFromSink * ColumnSpacing);
            }
            TargetX = FMath::GridSnap(TargetX, GridSize);
            int32 TargetY = FMath::GridSnap(
                FMath::Max(BaseY + (Index * RowSpacing), FMath::RoundToInt(PreferredYByNode.FindChecked(Expression))),
                GridSize);

            FBox2D CandidateRect(
                FVector2D(TargetX, TargetY),
                FVector2D(TargetX + Size.X, TargetY + Size.Y));

            bool bOverlaps = true;
            while (bOverlaps)
            {
                bOverlaps = false;
                for (const FBox2D& OccupiedRect : OccupiedRects)
                {
                    if (CandidateRect.Intersect(OccupiedRect))
                    {
                        TargetY = FMath::GridSnap(TargetY + RowSpacing, GridSize);
                        CandidateRect = FBox2D(
                            FVector2D(TargetX, TargetY),
                            FVector2D(TargetX + Size.X, TargetY + Size.Y));
                        bOverlaps = true;
                        break;
                    }
                }
            }

            if (Expression->MaterialExpressionEditorX != TargetX || Expression->MaterialExpressionEditorY != TargetY)
            {
                Expression->MaterialExpressionEditorX = TargetX;
                Expression->MaterialExpressionEditorY = TargetY;
                OutMovedNodeIds.Add(MaterialExpressionId(Expression));
            }

            OccupiedRects.Add(CandidateRect);
        }
    }

    return true;
}

bool FLoomleBridgeModule::ApplyPcgLayout(
    const FString& AssetPath,
    const FString& GraphName,
    const FString& Scope,
    const TArray<FString>& RequestedNodeIds,
    TArray<FString>& OutMovedNodeIds,
    FString& OutError)
{
    OutMovedNodeIds.Reset();
    OutError.Empty();

    UPCGGraph* PcgGraph = LoadPcgGraphByAssetPath(AssetPath);
    if (PcgGraph == nullptr)
    {
        OutError = TEXT("PCG asset not found.");
        return false;
    }

    TArray<UPCGNode*> AllNodes;
    for (UPCGNode* Node : PcgGraph->GetNodes())
    {
        if (Node != nullptr)
        {
            AllNodes.Add(Node);
        }
    }

    auto ResolveNodeToken = [&AllNodes](const FString& NodeToken) -> UPCGNode*
    {
        if (NodeToken.IsEmpty())
        {
            return nullptr;
        }

        for (UPCGNode* Node : AllNodes)
        {
            if (Node == nullptr)
            {
                continue;
            }

            if (Node->GetPathName().Equals(NodeToken, ESearchCase::IgnoreCase)
                || Node->GetName().Equals(NodeToken, ESearchCase::IgnoreCase))
            {
                return Node;
            }
        }
        return nullptr;
    };

    auto EstimateNodeSize = [](const UPCGNode* Node) -> FVector2D
    {
        const FString ClassName = Node && Node->GetSettings() && Node->GetSettings()->GetClass()
            ? Node->GetSettings()->GetClass()->GetName()
            : TEXT("");
        if (ClassName.Contains(TEXT("Sampler"), ESearchCase::IgnoreCase))
        {
            return FVector2D(320.0f, 180.0f);
        }
        return FVector2D(280.0f, 160.0f);
    };

    TMap<UPCGNode*, TArray<UPCGNode*>> Predecessors;
    TMap<UPCGNode*, TArray<UPCGNode*>> Successors;
    for (UPCGNode* Node : AllNodes)
    {
        if (Node != nullptr)
        {
            Predecessors.Add(Node, TArray<UPCGNode*>());
            Successors.Add(Node, TArray<UPCGNode*>());
        }
    }

    for (UPCGNode* Node : AllNodes)
    {
        if (Node == nullptr)
        {
            continue;
        }

        for (UPCGPin* OutputPin : Node->GetOutputPins())
        {
            if (OutputPin == nullptr)
            {
                continue;
            }

            for (UPCGEdge* Edge : OutputPin->Edges)
            {
                const UPCGPin* OtherPin = Edge ? Edge->GetOtherPin(OutputPin) : nullptr;
                UPCGNode* OtherNode = OtherPin ? OtherPin->Node : nullptr;
                if (OtherNode == nullptr || OtherNode == Node)
                {
                    continue;
                }
                Successors.FindChecked(Node).AddUnique(OtherNode);
                if (Predecessors.Contains(OtherNode))
                {
                    Predecessors.FindChecked(OtherNode).AddUnique(Node);
                }
            }
        }
    }

    TSet<UPCGNode*> MovableNodes;
    if (Scope.Equals(TEXT("all")))
    {
        for (UPCGNode* Node : AllNodes)
        {
            if (Node != nullptr)
            {
                MovableNodes.Add(Node);
            }
        }
    }
    else if (Scope.Equals(TEXT("touched")))
    {
        for (const FString& NodeId : RequestedNodeIds)
        {
            if (UPCGNode* Node = ResolveNodeToken(NodeId))
            {
                MovableNodes.Add(Node);
            }
        }

        TArray<UPCGNode*> SeedNodes = MovableNodes.Array();
        for (UPCGNode* Node : SeedNodes)
        {
            if (Node == nullptr)
            {
                continue;
            }
            if (const TArray<UPCGNode*>* Inputs = Predecessors.Find(Node))
            {
                for (UPCGNode* Neighbor : *Inputs)
                {
                    if (Neighbor != nullptr)
                    {
                        MovableNodes.Add(Neighbor);
                    }
                }
            }
            if (const TArray<UPCGNode*>* Outputs = Successors.Find(Node))
            {
                for (UPCGNode* Neighbor : *Outputs)
                {
                    if (Neighbor != nullptr)
                    {
                        MovableNodes.Add(Neighbor);
                    }
                }
            }
        }
    }
    else
    {
        OutError = FString::Printf(TEXT("Unsupported layout scope for PCG: %s"), *Scope);
        return false;
    }

    if (MovableNodes.Num() == 0)
    {
        OutError = Scope.Equals(TEXT("touched"))
            ? TEXT("No touched nodes are pending for layout.")
            : TEXT("No PCG nodes available for layout.");
        return false;
    }

    TArray<FBox2D> OccupiedRects;
    int32 BaseX = TNumericLimits<int32>::Max();
    int32 BaseY = TNumericLimits<int32>::Max();
    for (UPCGNode* Node : AllNodes)
    {
        if (Node == nullptr)
        {
            continue;
        }

        int32 NodeX = 0;
        int32 NodeY = 0;
        Node->GetNodePosition(NodeX, NodeY);
        if (MovableNodes.Contains(Node))
        {
            BaseX = FMath::Min(BaseX, NodeX);
            BaseY = FMath::Min(BaseY, NodeY);
            continue;
        }

        const FVector2D Size = EstimateNodeSize(Node);
        OccupiedRects.Add(FBox2D(
            FVector2D(NodeX, NodeY),
            FVector2D(NodeX + Size.X, NodeY + Size.Y)));
    }

    if (BaseX == TNumericLimits<int32>::Max())
    {
        BaseX = 0;
    }
    if (BaseY == TNumericLimits<int32>::Max())
    {
        BaseY = 0;
    }

    TMap<UPCGNode*, int32> DepthByNode;
    TMap<UPCGNode*, float> PreferredYByNode;
    for (UPCGNode* Node : MovableNodes)
    {
        DepthByNode.Add(Node, TNumericLimits<int32>::Min());
        int32 NodeX = 0;
        int32 NodeY = 0;
        Node->GetNodePosition(NodeX, NodeY);
        PreferredYByNode.Add(Node, static_cast<float>(NodeY));
    }

    for (UPCGNode* Node : MovableNodes)
    {
        bool bHasMovableInput = false;
        if (const TArray<UPCGNode*>* Inputs = Predecessors.Find(Node))
        {
            for (UPCGNode* InputNode : *Inputs)
            {
                if (MovableNodes.Contains(InputNode))
                {
                    bHasMovableInput = true;
                    break;
                }
            }
        }
        if (!bHasMovableInput)
        {
            DepthByNode[Node] = 0;
        }
    }

    bool bProgress = true;
    while (bProgress)
    {
        bProgress = false;
        for (UPCGNode* Node : MovableNodes)
        {
            const int32 CurrentDepth = DepthByNode.FindChecked(Node);
            if (CurrentDepth == TNumericLimits<int32>::Min())
            {
                continue;
            }

            if (const TArray<UPCGNode*>* Outputs = Successors.Find(Node))
            {
                for (UPCGNode* OutputNode : *Outputs)
                {
                    if (OutputNode == nullptr || !MovableNodes.Contains(OutputNode))
                    {
                        continue;
                    }

                    int32& OutputDepth = DepthByNode.FindChecked(OutputNode);
                    if (OutputDepth < CurrentDepth + 1)
                    {
                        OutputDepth = CurrentDepth + 1;
                        bProgress = true;
                    }
                }
            }
        }
    }

    TMap<int32, TArray<UPCGNode*>> NodesByDepth;
    for (UPCGNode* Node : MovableNodes)
    {
        int32& Depth = DepthByNode.FindChecked(Node);
        if (Depth == TNumericLimits<int32>::Min())
        {
            Depth = 0;
        }

        float WeightedY = PreferredYByNode.FindChecked(Node);
        float WeightTotal = 1.0f;
        if (const TArray<UPCGNode*>* Inputs = Predecessors.Find(Node))
        {
            for (UPCGNode* InputNode : *Inputs)
            {
                if (InputNode != nullptr && MovableNodes.Contains(InputNode))
                {
                    WeightedY += PreferredYByNode.FindChecked(InputNode) * 1.5f;
                    WeightTotal += 1.5f;
                }
            }
        }
        PreferredYByNode[Node] = WeightedY / WeightTotal;
        NodesByDepth.FindOrAdd(Depth).Add(Node);
    }

    TArray<int32> SortedDepths;
    NodesByDepth.GetKeys(SortedDepths);
    SortedDepths.Sort();

    const int32 ColumnSpacing = 360;
    const int32 RowSpacing = 208;
    const int32 GridSize = 16;

    for (int32 Depth : SortedDepths)
    {
        TArray<UPCGNode*>& ColumnNodes = NodesByDepth.FindChecked(Depth);
        ColumnNodes.Sort([&PreferredYByNode](const UPCGNode& A, const UPCGNode& B)
        {
            const float YA = PreferredYByNode.Contains(&A) ? PreferredYByNode.FindChecked(&A) : 0.0f;
            const float YB = PreferredYByNode.Contains(&B) ? PreferredYByNode.FindChecked(&B) : 0.0f;
            if (!FMath::IsNearlyEqual(YA, YB, 1.0f))
            {
                return YA < YB;
            }
            return A.GetName() < B.GetName();
        });

        for (int32 Index = 0; Index < ColumnNodes.Num(); ++Index)
        {
            UPCGNode* Node = ColumnNodes[Index];
            const FVector2D Size = EstimateNodeSize(Node);
            const int32 TargetX = FMath::GridSnap(BaseX + (Depth * ColumnSpacing), GridSize);
            int32 TargetY = FMath::GridSnap(
                FMath::Max(BaseY + (Index * RowSpacing), FMath::RoundToInt(PreferredYByNode.FindChecked(Node))),
                GridSize);

            FBox2D CandidateRect(
                FVector2D(TargetX, TargetY),
                FVector2D(TargetX + Size.X, TargetY + Size.Y));

            bool bOverlaps = true;
            while (bOverlaps)
            {
                bOverlaps = false;
                for (const FBox2D& OccupiedRect : OccupiedRects)
                {
                    if (CandidateRect.Intersect(OccupiedRect))
                    {
                        TargetY = FMath::GridSnap(TargetY + RowSpacing, GridSize);
                        CandidateRect = FBox2D(
                            FVector2D(TargetX, TargetY),
                            FVector2D(TargetX + Size.X, TargetY + Size.Y));
                        bOverlaps = true;
                        break;
                    }
                }
            }

            int32 CurrentX = 0;
            int32 CurrentY = 0;
            Node->GetNodePosition(CurrentX, CurrentY);
            if (CurrentX != TargetX || CurrentY != TargetY)
            {
                Node->SetNodePosition(TargetX, TargetY);
                OutMovedNodeIds.Add(Node->GetPathName());
            }

            OccupiedRects.Add(CandidateRect);
        }
    }

    return true;
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
        Result->SetStringField(TEXT("message"), TEXT("Supported graphType values: blueprint, material, pcg."));
        return Result;
    }

    FResolvedGraphAddress GraphAddress;
    FGraphAddressResolutionOptions GraphAddressOptions;
    GraphAddressOptions.BlueprintDefaultGraphName = TEXT("EventGraph");
    GraphAddressOptions.bRejectNonBlueprintInlineGraphRefs = true;
    GraphAddressOptions.bResolveInlineBlueprintGraphName = true;
    if (!ResolveGraphRequestAddress(Arguments, GraphType, GraphAddressOptions, GraphAddress, Result))
    {
        return Result;
    }

    FString AssetPath = GraphAddress.AssetPath;
    FString GraphName = GraphAddress.GraphName;
    FString InlineNodeGuid = GraphAddress.InlineNodeGuid;
    const bool bUsedGraphRef = GraphAddress.bUsedGraphRef;

    bool bDryRun = false;
    Arguments->TryGetBoolField(TEXT("dryRun"), bDryRun);

    auto BuildMutateRevisionQueryArgs = [&]() -> TSharedPtr<FJsonObject>
    {
        TSharedPtr<FJsonObject> QueryArgs = MakeShared<FJsonObject>();
        QueryArgs->SetStringField(TEXT("graphType"), GraphType);
        if (bUsedGraphRef)
        {
            QueryArgs->SetObjectField(TEXT("graphRef"), MakeEffectiveGraphRef(GraphAddress));
        }
        else
        {
            QueryArgs->SetStringField(TEXT("assetPath"), AssetPath);
            if (!GraphName.IsEmpty())
            {
                QueryArgs->SetStringField(TEXT("graphName"), GraphName);
            }
        }
        return QueryArgs;
    };

    auto ResolveActualRevision = [&](FString& OutRevision, FString& OutCode, FString& OutMessage) -> bool
    {
        OutRevision.Empty();
        OutCode.Empty();
        OutMessage.Empty();

        const TSharedPtr<FJsonObject> RevisionResult = BuildGraphQueryToolResult(BuildMutateRevisionQueryArgs());
        if (!RevisionResult.IsValid())
        {
            OutCode = TEXT("INTERNAL_ERROR");
            OutMessage = TEXT("Failed to resolve current graph revision.");
            return false;
        }

        bool bRevisionQueryError = false;
        RevisionResult->TryGetBoolField(TEXT("isError"), bRevisionQueryError);
        if (bRevisionQueryError)
        {
            RevisionResult->TryGetStringField(TEXT("code"), OutCode);
            RevisionResult->TryGetStringField(TEXT("message"), OutMessage);
            if (OutCode.IsEmpty())
            {
                OutCode = TEXT("INTERNAL_ERROR");
            }
            if (OutMessage.IsEmpty())
            {
                OutMessage = TEXT("Failed to resolve current graph revision.");
            }
            return false;
        }

        if (!RevisionResult->TryGetStringField(TEXT("revision"), OutRevision) || OutRevision.IsEmpty())
        {
            OutCode = TEXT("INTERNAL_ERROR");
            OutMessage = TEXT("graph.query did not return a revision for graph.mutate.");
            return false;
        }

        return true;
    };

    FString PreviousRevision;
    FString RevisionCode;
    FString RevisionMessage;
    if (!ResolveActualRevision(PreviousRevision, RevisionCode, RevisionMessage))
    {
        Result->SetBoolField(TEXT("isError"), true);
        Result->SetStringField(TEXT("code"), RevisionCode);
        Result->SetStringField(TEXT("message"), RevisionMessage);
        return Result;
    }

    FString IdempotencyKey;
    Arguments->TryGetStringField(TEXT("idempotencyKey"), IdempotencyKey);
    IdempotencyKey = IdempotencyKey.TrimStartAndEnd();

    const FString IdempotencyRegistryKey = IdempotencyKey.IsEmpty()
        ? FString()
        : FString::Printf(
            TEXT("%s|%s|%s|%s|%s"),
            *GraphType,
            *AssetPath,
            *GraphName,
            *InlineNodeGuid,
            *IdempotencyKey);
    FString RequestFingerprint;
    if (!IdempotencyRegistryKey.IsEmpty())
    {
        TSharedPtr<FJsonObject> FingerprintSource = CloneJsonObject(Arguments);
        if (!FingerprintSource.IsValid())
        {
            FingerprintSource = MakeShared<FJsonObject>();
            for (const TPair<FString, TSharedPtr<FJsonValue>>& Field : Arguments->Values)
            {
                FingerprintSource->SetField(Field.Key, Field.Value);
            }
        }
        FingerprintSource->RemoveField(TEXT("idempotencyKey"));
        RequestFingerprint = SerializeJsonObjectCondensed(FingerprintSource);

        {
            const double NowSeconds = FPlatformTime::Seconds();
            FScopeLock Lock(&MutateIdempotencyRegistryMutex);

            TArray<FString> ExpiredKeys;
            for (const TPair<FString, FMutateIdempotencyEntry>& Entry : MutateIdempotencyRegistry)
            {
                if ((NowSeconds - Entry.Value.CreatedAtSeconds) > LoomleBridgeConstants::MutateIdempotencyTtlSeconds)
                {
                    ExpiredKeys.Add(Entry.Key);
                }
            }
            for (const FString& ExpiredKey : ExpiredKeys)
            {
                MutateIdempotencyRegistry.Remove(ExpiredKey);
            }

            if (const FMutateIdempotencyEntry* Existing = MutateIdempotencyRegistry.Find(IdempotencyRegistryKey))
            {
                if (!Existing->RequestFingerprint.Equals(RequestFingerprint, ESearchCase::CaseSensitive))
                {
                    Result->SetBoolField(TEXT("isError"), true);
                    Result->SetStringField(TEXT("code"), TEXT("INVALID_ARGUMENT"));
                    Result->SetStringField(
                        TEXT("message"),
                        TEXT("idempotencyKey was already used for a different graph.mutate request in this graph scope."));
                    return Result;
                }

                if (Existing->Result.IsValid())
                {
                    TSharedPtr<FJsonObject> ReplayResult = CloneJsonObject(Existing->Result);
                    if (ReplayResult.IsValid())
                    {
                        return ReplayResult;
                    }
                }
            }
        }
    }

    FString ExpectedRevision;
    if (Arguments->TryGetStringField(TEXT("expectedRevision"), ExpectedRevision)
        && !ExpectedRevision.IsEmpty()
        && !ExpectedRevision.Equals(PreviousRevision, ESearchCase::CaseSensitive))
    {
        Result->SetBoolField(TEXT("isError"), true);
        Result->SetStringField(TEXT("code"), TEXT("REVISION_CONFLICT"));
        Result->SetStringField(
            TEXT("message"),
            FString::Printf(
                TEXT("expectedRevision mismatch: expected %s but current revision is %s."),
                *ExpectedRevision,
                *PreviousRevision));
        Result->SetBoolField(TEXT("applied"), false);
        Result->SetStringField(TEXT("graphType"), GraphType);
        Result->SetStringField(TEXT("assetPath"), AssetPath);
        Result->SetStringField(TEXT("graphName"), GraphName);
        Result->SetStringField(TEXT("previousRevision"), PreviousRevision);
        Result->SetStringField(TEXT("newRevision"), PreviousRevision);
        Result->SetArrayField(TEXT("opResults"), TArray<TSharedPtr<FJsonValue>>{});
        Result->SetArrayField(TEXT("diagnostics"), TArray<TSharedPtr<FJsonValue>>{});
        return Result;
    }

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

    auto InferMutateErrorCode = [](const FString& Op, const FString& Error) -> FString
    {
        const FString OpLower = Op.ToLower();
        const FString ErrorLower = Error.ToLower();

        if (Error.StartsWith(TEXT("ACTION_TOKEN_INVALID:")))
        {
            return TEXT("ACTION_TOKEN_INVALID");
        }
        if (Error.StartsWith(TEXT("GRAPH_REF_INVALID:")))
        {
            return TEXT("GRAPH_REF_INVALID");
        }
        if (ErrorLower.Contains(TEXT("unsupported")) && ErrorLower.Contains(TEXT("op")))
        {
            return TEXT("UNSUPPORTED_OP");
        }
        if (ErrorLower.Contains(TEXT("invalid")))
        {
            return TEXT("INVALID_ARGUMENT");
        }
        if (ErrorLower.Contains(TEXT("duplicate clientref")))
        {
            return TEXT("INVALID_ARGUMENT");
        }
        if (ErrorLower.Contains(TEXT("requires")) || ErrorLower.Contains(TEXT("missing")))
        {
            return TEXT("INVALID_ARGUMENT");
        }
        if (ErrorLower.Contains(TEXT("graph not found")))
        {
            return TEXT("GRAPH_NOT_FOUND");
        }
        if (ErrorLower.Contains(TEXT("node not found")))
        {
            return TEXT("NODE_NOT_FOUND");
        }
        if (ErrorLower.Contains(TEXT("pin not found")))
        {
            return TEXT("PIN_NOT_FOUND");
        }
        if (ErrorLower.Contains(TEXT("failed to resolve")))
        {
            return OpLower.Equals(TEXT("removenode")) || OpLower.Equals(TEXT("movenode"))
                || OpLower.Equals(TEXT("connectpins")) || OpLower.Equals(TEXT("disconnectpins"))
                || OpLower.Equals(TEXT("breakpinlinks")) || OpLower.Equals(TEXT("setpindefault"))
                ? TEXT("TARGET_NOT_FOUND")
                : TEXT("INTERNAL_ERROR");
        }
        if (ErrorLower.Contains(TEXT("timeout")))
        {
            return TEXT("TIMEOUT");
        }
        if (ErrorLower.Contains(TEXT("python runtime is not initialized")) || ErrorLower.Contains(TEXT("module is not loaded")))
        {
            return TEXT("RUNTIME_UNAVAILABLE");
        }
        return TEXT("INTERNAL_ERROR");
    };

    if (!GraphType.Equals(TEXT("blueprint")))
    {
        TMap<FString, FString> LocalNodeRefs;
        TArray<TSharedPtr<FJsonValue>> LocalOpResults;
        TArray<TSharedPtr<FJsonValue>> LocalDiagnostics;
        bool bAnyErrorLocal = false;
        bool bAnyChangedLocal = false;
        FString FirstErrorLocal;
        FString FirstErrorCodeLocal;

        auto ResolveNodeTokenLocal = [&LocalNodeRefs](const TSharedPtr<FJsonObject>& Obj, FString& OutNodeId) -> bool
        {
            return ResolveNodeTokenWithRefs(Obj, LocalNodeRefs, OutNodeId, true);
        };

        auto ResolveNodeTokenFromArgsLocal = [&LocalNodeRefs](const TSharedPtr<FJsonObject>& ArgsObj, FString& OutNodeId) -> bool
        {
            return ResolveNodeTokenFromArgsWithRefs(ArgsObj, LocalNodeRefs, OutNodeId, true);
        };

        auto ResolveStableNodeTokenLocal = [&LocalNodeRefs](const TSharedPtr<FJsonObject>& Obj, FString& OutNodeId) -> bool
        {
            return ResolveNodeTokenWithRefs(Obj, LocalNodeRefs, OutNodeId, false);
        };

        auto ResolveStableNodeTokenFromArgsLocal = [&LocalNodeRefs](const TSharedPtr<FJsonObject>& ArgsObj, FString& OutNodeId) -> bool
        {
            return ResolveNodeTokenFromArgsWithRefs(ArgsObj, LocalNodeRefs, OutNodeId, false);
        };

        auto ResolveNodeTokenArrayFromArgsLocal = [&LocalNodeRefs](const TSharedPtr<FJsonObject>& ArgsObj, TArray<FString>& OutNodeIds) -> bool
        {
            return ResolveNodeTokenArrayWithRefs(ArgsObj, LocalNodeRefs, OutNodeIds, true);
        };

        auto ResolvePinName = [](const TSharedPtr<FJsonObject>& Obj, FString& OutPinName) -> bool
        {
            return TryResolvePinNameField(Obj, OutPinName);
        };

        auto CollectPcgAdjacentNodeIdsLocal = [](UPCGNode* Node, TArray<FString>& OutNodeIds)
        {
            if (Node == nullptr)
            {
                return;
            }

            TSet<FString> UniqueNodeIds;
            auto AddNeighbor = [&UniqueNodeIds, Node](UPCGPin* Pin)
            {
                if (Pin == nullptr)
                {
                    return;
                }

                for (UPCGEdge* Edge : Pin->Edges)
                {
                    const UPCGPin* OtherPin = Edge ? Edge->GetOtherPin(Pin) : nullptr;
                    UPCGNode* OtherNode = OtherPin ? OtherPin->Node : nullptr;
                    if (OtherNode == nullptr || OtherNode == Node)
                    {
                        continue;
                    }

                    const FString OtherNodeId = OtherNode->GetPathName();
                    if (!OtherNodeId.IsEmpty())
                    {
                        UniqueNodeIds.Add(OtherNodeId);
                    }
                }
            };

            for (UPCGPin* InputPin : Node->GetInputPins())
            {
                AddNeighbor(InputPin);
            }
            for (UPCGPin* OutputPin : Node->GetOutputPins())
            {
                AddNeighbor(OutputPin);
            }

            for (const FString& NodeId : UniqueNodeIds)
            {
                OutNodeIds.Add(NodeId);
            }
        };

        auto GetPointFromObjectLocal = [](const TSharedPtr<FJsonObject>& Obj, int32& OutX, int32& OutY)
        {
            GetPointFromJsonObject(Obj, OutX, OutY);
        };

        auto HasExplicitPositionLocal = [](const TSharedPtr<FJsonObject>& Obj) -> bool
        {
            return HasExplicitPositionField(Obj);
        };

        auto GetDeltaFromObjectLocal = [](const TSharedPtr<FJsonObject>& Obj, int32& OutDx, int32& OutDy)
        {
            GetDeltaFromJsonObject(Obj, OutDx, OutDy);
        };

        UObject* MutableAsset = nullptr;
        UMaterial* MaterialAsset = nullptr;
        UPCGGraph* PcgGraph = nullptr;

        auto EstimateMaterialNodeSize = [](const UMaterialExpression* Expression) -> FVector2D
        {
            if (Expression == nullptr)
            {
                return FVector2D(240.0f, 160.0f);
            }

            const FString ClassName = Expression->GetClass() ? Expression->GetClass()->GetName() : TEXT("");
            if (ClassName.Contains(TEXT("Texture"), ESearchCase::IgnoreCase))
            {
                return FVector2D(320.0f, 220.0f);
            }
            if (ClassName.Contains(TEXT("Parameter"), ESearchCase::IgnoreCase))
            {
                return FVector2D(260.0f, 180.0f);
            }
            return FVector2D(240.0f, 160.0f);
        };

        auto EstimatePcgNodeSize = [](const UPCGNode* Node) -> FVector2D
        {
            const FString ClassName = Node && Node->GetSettings() && Node->GetSettings()->GetClass()
                ? Node->GetSettings()->GetClass()->GetName()
                : TEXT("");
            if (ClassName.Contains(TEXT("Sampler"), ESearchCase::IgnoreCase))
            {
                return FVector2D(320.0f, 180.0f);
            }
            return FVector2D(280.0f, 160.0f);
        };

        auto ResolveAutoPlacementLocal = [&](const TSharedPtr<FJsonObject>& ArgsObj, int32& OutX, int32& OutY) -> bool
        {
            OutX = 0;
            OutY = 0;

            auto ResolveAnchorPoint = [&](const TSharedPtr<FJsonObject>& SourceObj, const TCHAR* FieldName, int32& AnchorX, int32& AnchorY, FVector2D& AnchorSize) -> bool
            {
                const TSharedPtr<FJsonObject>* AnchorObj = nullptr;
                if (!SourceObj.IsValid()
                    || !SourceObj->TryGetObjectField(FieldName, AnchorObj)
                    || !AnchorObj
                    || !(*AnchorObj).IsValid())
                {
                    return false;
                }

                FString AnchorNodeId;
                if (!ResolveNodeTokenLocal(*AnchorObj, AnchorNodeId) || AnchorNodeId.IsEmpty())
                {
                    return false;
                }

                if (GraphType.Equals(TEXT("material")))
                {
                    if (UMaterialExpression* AnchorExpression = FindMaterialExpressionById(MaterialAsset, AnchorNodeId))
                    {
                        AnchorX = AnchorExpression->MaterialExpressionEditorX;
                        AnchorY = AnchorExpression->MaterialExpressionEditorY;
                        AnchorSize = EstimateMaterialNodeSize(AnchorExpression);
                        return true;
                    }
                }
                else if (GraphType.Equals(TEXT("pcg")))
                {
                    if (UPCGNode* AnchorNode = FindPcgNodeById(PcgGraph, AnchorNodeId))
                    {
                        AnchorNode->GetNodePosition(AnchorX, AnchorY);
                        AnchorSize = EstimatePcgNodeSize(AnchorNode);
                        return true;
                    }
                }

                return false;
            };

            int32 AnchorX = 0;
            int32 AnchorY = 0;
            FVector2D AnchorSize(280.0f, 160.0f);
            if (ResolveAnchorPoint(ArgsObj, TEXT("anchor"), AnchorX, AnchorY, AnchorSize)
                || ResolveAnchorPoint(ArgsObj, TEXT("near"), AnchorX, AnchorY, AnchorSize)
                || ResolveAnchorPoint(ArgsObj, TEXT("from"), AnchorX, AnchorY, AnchorSize)
                || ResolveAnchorPoint(ArgsObj, TEXT("target"), AnchorX, AnchorY, AnchorSize))
            {
                OutX = AnchorX + static_cast<int32>(AnchorSize.X) + 96;
                OutY = AnchorY;
            }
            else if (GraphType.Equals(TEXT("material")) && MaterialAsset != nullptr)
            {
                if (MaterialAsset->MaterialGraph != nullptr && MaterialAsset->MaterialGraph->RootNode != nullptr)
                {
                    OutX = MaterialAsset->MaterialGraph->RootNode->NodePosX - 384;
                    OutY = MaterialAsset->MaterialGraph->RootNode->NodePosY;
                }
                else
                {
                    UMaterialExpression* RightmostExpression = nullptr;
                    for (UMaterialExpression* Expression : MaterialAsset->GetExpressions())
                    {
                        if (Expression == nullptr)
                        {
                            continue;
                        }
                        if (RightmostExpression == nullptr || Expression->MaterialExpressionEditorX > RightmostExpression->MaterialExpressionEditorX)
                        {
                            RightmostExpression = Expression;
                        }
                    }

                    if (RightmostExpression != nullptr)
                    {
                        const FVector2D Size = EstimateMaterialNodeSize(RightmostExpression);
                        OutX = RightmostExpression->MaterialExpressionEditorX + static_cast<int32>(Size.X) + 96;
                        OutY = RightmostExpression->MaterialExpressionEditorY;
                    }
                }
            }
            else if (GraphType.Equals(TEXT("pcg")) && PcgGraph != nullptr)
            {
                UPCGNode* RightmostNode = nullptr;
                int32 RightmostX = TNumericLimits<int32>::Min();
                for (UPCGNode* Node : PcgGraph->GetNodes())
                {
                    if (Node == nullptr)
                    {
                        continue;
                    }
                    int32 NodeX = 0;
                    int32 NodeY = 0;
                    Node->GetNodePosition(NodeX, NodeY);
                    if (RightmostNode == nullptr || NodeX > RightmostX)
                    {
                        RightmostNode = Node;
                        RightmostX = NodeX;
                        OutY = NodeY;
                    }
                }

                if (RightmostNode != nullptr)
                {
                    const FVector2D Size = EstimatePcgNodeSize(RightmostNode);
                    OutX = RightmostX + static_cast<int32>(Size.X) + 96;
                }
            }

            const int32 GridSize = 16;
            const int32 VerticalStep = 208;
            const FVector2D CandidateSize = GraphType.Equals(TEXT("material"))
                ? FVector2D(260.0f, 180.0f)
                : FVector2D(280.0f, 160.0f);

            OutX = FMath::GridSnap(OutX, GridSize);
            OutY = FMath::GridSnap(OutY, GridSize);

            bool bCollides = true;
            while (bCollides)
            {
                bCollides = false;
                const FBox2D CandidateRect(
                    FVector2D(OutX, OutY),
                    FVector2D(OutX + CandidateSize.X, OutY + CandidateSize.Y));

                if (GraphType.Equals(TEXT("material")) && MaterialAsset != nullptr)
                {
                    for (UMaterialExpression* Expression : MaterialAsset->GetExpressions())
                    {
                        if (Expression == nullptr)
                        {
                            continue;
                        }

                        const FVector2D ExistingSize = EstimateMaterialNodeSize(Expression);
                        const FBox2D ExistingRect(
                            FVector2D(Expression->MaterialExpressionEditorX, Expression->MaterialExpressionEditorY),
                            FVector2D(Expression->MaterialExpressionEditorX + ExistingSize.X, Expression->MaterialExpressionEditorY + ExistingSize.Y));
                        if (CandidateRect.Intersect(ExistingRect))
                        {
                            OutY = FMath::GridSnap(OutY + VerticalStep, GridSize);
                            bCollides = true;
                            break;
                        }
                    }
                }
                else if (GraphType.Equals(TEXT("pcg")) && PcgGraph != nullptr)
                {
                    for (UPCGNode* ExistingNode : PcgGraph->GetNodes())
                    {
                        if (ExistingNode == nullptr)
                        {
                            continue;
                        }

                        int32 ExistingX = 0;
                        int32 ExistingY = 0;
                        ExistingNode->GetNodePosition(ExistingX, ExistingY);
                        const FVector2D ExistingSize = EstimatePcgNodeSize(ExistingNode);
                        const FBox2D ExistingRect(
                            FVector2D(ExistingX, ExistingY),
                            FVector2D(ExistingX + ExistingSize.X, ExistingY + ExistingSize.Y));
                        if (CandidateRect.Intersect(ExistingRect))
                        {
                            OutY = FMath::GridSnap(OutY + VerticalStep, GridSize);
                            bCollides = true;
                            break;
                        }
                    }
                }
            }

            return true;
        };

        auto ResolveMaterialPropertyByRootPinName = [&](const FString& PinName, EMaterialProperty& OutProperty) -> bool
        {
            if (MaterialAsset == nullptr || MaterialAsset->MaterialGraph == nullptr || MaterialAsset->MaterialGraph->RootNode == nullptr)
            {
                return false;
            }

            for (UEdGraphPin* Pin : MaterialAsset->MaterialGraph->RootNode->Pins)
            {
                if (Pin == nullptr || Pin->Direction != EGPD_Input)
                {
                    continue;
                }
                if (!Pin->PinName.ToString().Equals(PinName, ESearchCase::IgnoreCase))
                {
                    continue;
                }

                const int32 SourceIndex = Pin->SourceIndex;
                if (!MaterialAsset->MaterialGraph->MaterialInputs.IsValidIndex(SourceIndex))
                {
                    return false;
                }

                OutProperty = MaterialAsset->MaterialGraph->MaterialInputs[SourceIndex].GetProperty();
                return true;
            }

            return false;
        };
        auto DisconnectMaterialInput = [](FExpressionInput* Input) -> bool
        {
            if (Input == nullptr || Input->Expression == nullptr)
            {
                return false;
            }

            Input->Expression = nullptr;
            Input->OutputIndex = 0;
            return true;
        };
        auto BuildMaterialPinDiagnostics = [&](const FString& Direction, const FString& NodeId, const FString& RequestedPinName, const TArray<FString>& AvailablePins) -> TSharedPtr<FJsonObject>
        {
            TSharedPtr<FJsonObject> DetailsObject = MakeShared<FJsonObject>();
            DetailsObject->SetStringField(TEXT("graphType"), TEXT("material"));
            DetailsObject->SetStringField(TEXT("pinDirection"), Direction);
            DetailsObject->SetStringField(TEXT("requestedPin"), RequestedPinName);
            if (!NodeId.IsEmpty())
            {
                DetailsObject->SetStringField(TEXT("nodeId"), NodeId);
            }

            TArray<TSharedPtr<FJsonValue>> AvailablePinValues;
            for (const FString& AvailablePin : AvailablePins)
            {
                if (!AvailablePin.IsEmpty())
                {
                    AvailablePinValues.Add(MakeShared<FJsonValueString>(AvailablePin));
                }
            }
            DetailsObject->SetArrayField(TEXT("availablePins"), AvailablePinValues);
            return DetailsObject;
        };
        auto ResolveMaterialSourceOutputPin = [&](const FString& SourceNodeId, UMaterialExpression* Expression, const FString& RequestedPinName, FString& OutResolvedPinName, FString& OutErrorMessage, TSharedPtr<FJsonObject>& OutErrorDetails) -> bool
        {
            TArray<FString> AvailablePins;
            if (TryResolveMaterialOutputPinName(MaterialAsset, Expression, RequestedPinName, OutResolvedPinName, &AvailablePins))
            {
                return true;
            }

            OutErrorDetails = BuildMaterialPinDiagnostics(TEXT("output"), SourceNodeId, RequestedPinName, AvailablePins);
            OutErrorMessage = TEXT("Material output pin not found.");
            return false;
        };
        auto ResolveMaterialTargetInputPin = [&](const FString& TargetNodeId, UMaterialExpression* Expression, const FString& RequestedPinName, int32& OutInputIndex, FString& OutResolvedPinName, FString& OutErrorMessage, TSharedPtr<FJsonObject>& OutErrorDetails) -> bool
        {
            TArray<FString> AvailablePins;
            if (TryResolveMaterialInputPinName(Expression, RequestedPinName, OutInputIndex, OutResolvedPinName, &AvailablePins))
            {
                return true;
            }

            OutErrorDetails = BuildMaterialPinDiagnostics(TEXT("input"), TargetNodeId, RequestedPinName, AvailablePins);
            OutErrorMessage = TEXT("Material input pin not found.");
            return false;
        };
        auto DisconnectMaterialInputIfMatchesSource = [&](FExpressionInput* Input, UMaterialExpression* SourceExpr, const FString& SourcePinName) -> bool
        {
            return Input != nullptr
                && Input->Expression == SourceExpr
                && AreMaterialOutputPinNamesEquivalent(Input->InputName.ToString(), SourcePinName)
                && DisconnectMaterialInput(Input);
        };
        auto BreakMaterialLinksBySourcePin = [&](UMaterialExpression* SourceExpr, const FString& SourcePinName, TArray<FString>& OutTouchedNodeIds) -> bool
        {
            if (SourceExpr == nullptr || MaterialAsset == nullptr)
            {
                return false;
            }

            auto InputMatchesSourcePin = [&SourcePinName](const FExpressionInput* Input) -> bool
            {
                if (Input == nullptr)
                {
                    return false;
                }

                if (SourcePinName.IsEmpty())
                {
                    return true;
                }

                return AreMaterialOutputPinNamesEquivalent(Input->InputName.ToString(), SourcePinName);
            };

            bool bDisconnected = false;

            if (MaterialAsset->MaterialGraph != nullptr && MaterialAsset->MaterialGraph->RootNode != nullptr)
            {
                TSet<EMaterialProperty> SeenRootProperties;
                for (UEdGraphPin* RootPin : MaterialAsset->MaterialGraph->RootNode->Pins)
                {
                    if (RootPin == nullptr || RootPin->Direction != EGPD_Input)
                    {
                        continue;
                    }

                    const int32 SourceIndex = RootPin->SourceIndex;
                    if (!MaterialAsset->MaterialGraph->MaterialInputs.IsValidIndex(SourceIndex))
                    {
                        continue;
                    }

                    const EMaterialProperty Property = MaterialAsset->MaterialGraph->MaterialInputs[SourceIndex].GetProperty();
                    if (SeenRootProperties.Contains(Property))
                    {
                        continue;
                    }
                    SeenRootProperties.Add(Property);

                    if (FExpressionInput* Input = MaterialAsset->GetExpressionInputForProperty(Property))
                    {
                        if (Input->Expression == SourceExpr && InputMatchesSourcePin(Input) && DisconnectMaterialInput(Input))
                        {
                            bDisconnected = true;
                            OutTouchedNodeIds.Add(TEXT("__material_root__"));
                        }
                    }
                }
            }

            for (UMaterialExpression* Candidate : MaterialAsset->GetExpressions())
            {
                if (Candidate == nullptr)
                {
                    continue;
                }

                bool bCandidateChanged = false;
                const int32 MaxInputs = 128;
                for (int32 InputIndex = 0; InputIndex < MaxInputs; ++InputIndex)
                {
                    FExpressionInput* Input = Candidate->GetInput(InputIndex);
                    if (Input == nullptr)
                    {
                        break;
                    }

                    if (Input->Expression == SourceExpr && InputMatchesSourcePin(Input) && DisconnectMaterialInput(Input))
                    {
                        bDisconnected = true;
                        bCandidateChanged = true;
                    }
                }

                if (bCandidateChanged)
                {
                    OutTouchedNodeIds.Add(MaterialExpressionId(Candidate));
                }
            }

            return bDisconnected;
        };
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
            if (MaterialAsset->MaterialGraph == nullptr)
            {
                MaterialAsset->MaterialGraph = CastChecked<UMaterialGraph>(
                    FBlueprintEditorUtils::CreateNewGraph(
                        MaterialAsset,
                        NAME_None,
                        UMaterialGraph::StaticClass(),
                        UMaterialGraphSchema::StaticClass()));
            }
            if (MaterialAsset->MaterialGraph != nullptr)
            {
                MaterialAsset->MaterialGraph->Material = MaterialAsset;
                MaterialAsset->MaterialGraph->MaterialFunction = nullptr;
                MaterialAsset->MaterialGraph->RebuildGraph();
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

        auto ExecuteMaterialMutateOp = [&](const FString& Op, const TSharedPtr<FJsonObject>& ArgsObj) -> FAssetMutateOpExecution
        {
            FAssetMutateOpExecution Execution;

            if (Op.Equals(TEXT("addnode.byclass")))
            {
                FString NodeClassPath;
                ArgsObj->TryGetStringField(TEXT("nodeClassPath"), NodeClassPath);

                UClass* ExpressionClass = LoadObject<UClass>(nullptr, *NodeClassPath);
                if (ExpressionClass == nullptr || !ExpressionClass->IsChildOf(UMaterialExpression::StaticClass()))
                {
                    Execution.bOk = false;
                    Execution.Error = TEXT("Invalid material expression class.");
                    return Execution;
                }

                int32 X = 0;
                int32 Y = 0;
                if (HasExplicitPositionLocal(ArgsObj))
                {
                    GetPointFromObjectLocal(ArgsObj, X, Y);
                }
                else
                {
                    ResolveAutoPlacementLocal(ArgsObj, X, Y);
                }

                UMaterialExpression* NewExpression = UMaterialEditingLibrary::CreateMaterialExpression(MaterialAsset, ExpressionClass, X, Y);
                if (NewExpression == nullptr)
                {
                    Execution.bOk = false;
                    Execution.Error = TEXT("Failed to create material expression.");
                    return Execution;
                }

                FString ParameterName;
                ArgsObj->TryGetStringField(TEXT("parameterName"), ParameterName);
                if (!ParameterName.IsEmpty())
                {
                    if (FNameProperty* ParameterNameProperty = FindFProperty<FNameProperty>(NewExpression->GetClass(), TEXT("ParameterName")))
                    {
                        ParameterNameProperty->SetPropertyValue_InContainer(NewExpression, FName(*ParameterName));
                    }
                }

                if (UMaterialExpressionMaterialFunctionCall* FunctionCall = Cast<UMaterialExpressionMaterialFunctionCall>(NewExpression))
                {
                    FString FunctionAssetPath;
                    ArgsObj->TryGetStringField(TEXT("functionAssetPath"), FunctionAssetPath);
                    if (!FunctionAssetPath.IsEmpty())
                    {
                        UMaterialFunctionInterface* MaterialFunction = LoadObject<UMaterialFunctionInterface>(nullptr, *FunctionAssetPath);
                        if (MaterialFunction == nullptr || !FunctionCall->SetMaterialFunction(MaterialFunction))
                        {
                            UMaterialEditingLibrary::DeleteMaterialExpression(MaterialAsset, NewExpression);
                            Execution.bOk = false;
                            Execution.Error = TEXT("Failed to resolve material function asset.");
                            return Execution;
                        }

                        FunctionCall->UpdateFromFunctionResource();
                    }
                }

                Execution.NodeId = MaterialExpressionId(NewExpression);
                Execution.bChanged = true;
                Execution.GraphEventName = TEXT("graph.node_added");
                Execution.GraphEventData->SetStringField(TEXT("nodeId"), Execution.NodeId);
                Execution.GraphEventData->SetStringField(TEXT("nodeType"), TEXT("by_class"));
                Execution.GraphEventData->SetStringField(TEXT("nodeClassPath"), NodeClassPath);
                Execution.GraphEventData->SetStringField(TEXT("op"), Op);
                Execution.NodesTouchedForLayout.Add(Execution.NodeId);
                return Execution;
            }

            if (Op.Equals(TEXT("removenode")))
            {
                FString TargetNodeId;
                Execution.bOk = ResolveNodeTokenFromArgsLocal(ArgsObj, TargetNodeId);
                if (!Execution.bOk)
                {
                    Execution.Error = TEXT("Missing target nodeId/path/name.");
                    return Execution;
                }

                if (UMaterialExpression* Expression = FindMaterialExpressionById(MaterialAsset, TargetNodeId))
                {
                    UMaterialEditingLibrary::DeleteMaterialExpression(MaterialAsset, Expression);
                    Execution.bChanged = true;
                    Execution.GraphEventName = TEXT("graph.node_removed");
                    Execution.GraphEventData->SetStringField(TEXT("nodeId"), TargetNodeId);
                    Execution.GraphEventData->SetStringField(TEXT("op"), Op);
                    Execution.NodesTouchedForLayout.Add(TargetNodeId);
                    return Execution;
                }

                Execution.bOk = false;
                Execution.Error = TEXT("Material expression not found.");
                return Execution;
            }

            if (Op.Equals(TEXT("movenode")) || Op.Equals(TEXT("movenodeby")))
            {
                FString TargetNodeId;
                Execution.bOk = ResolveNodeTokenFromArgsLocal(ArgsObj, TargetNodeId);
                if (!Execution.bOk)
                {
                    Execution.Error = FString::Printf(TEXT("Missing target nodeId/path/name for %s."), *Op);
                    return Execution;
                }

                if (UMaterialExpression* Expression = FindMaterialExpressionById(MaterialAsset, TargetNodeId))
                {
                    int32 X = 0;
                    int32 Y = 0;
                    if (Op.Equals(TEXT("movenodeby")))
                    {
                        int32 Dx = 0;
                        int32 Dy = 0;
                        GetDeltaFromObjectLocal(ArgsObj, Dx, Dy);
                        X = Expression->MaterialExpressionEditorX + Dx;
                        Y = Expression->MaterialExpressionEditorY + Dy;
                    }
                    else
                    {
                        GetPointFromObjectLocal(ArgsObj, X, Y);
                    }
                    Expression->MaterialExpressionEditorX = X;
                    Expression->MaterialExpressionEditorY = Y;
                    Execution.bChanged = true;
                    Execution.GraphEventName = TEXT("graph.node_moved");
                    Execution.GraphEventData->SetStringField(TEXT("nodeId"), TargetNodeId);
                    Execution.GraphEventData->SetNumberField(TEXT("x"), X);
                    Execution.GraphEventData->SetNumberField(TEXT("y"), Y);
                    Execution.GraphEventData->SetStringField(TEXT("op"), Op);
                    Execution.NodesTouchedForLayout.Add(TargetNodeId);
                    return Execution;
                }

                Execution.bOk = false;
                Execution.Error = TEXT("Material expression not found.");
                return Execution;
            }

            if (Op.Equals(TEXT("movenodes")))
            {
                TArray<FString> TargetNodeIds;
                Execution.bOk = ResolveNodeTokenArrayFromArgsLocal(ArgsObj, TargetNodeIds);
                if (!Execution.bOk)
                {
                    Execution.Error = TEXT("Missing nodeIds/nodes for moveNodes.");
                    return Execution;
                }

                int32 Dx = 0;
                int32 Dy = 0;
                GetDeltaFromObjectLocal(ArgsObj, Dx, Dy);

                TArray<TSharedPtr<FJsonValue>> MovedNodeIds;
                for (const FString& TargetNodeId : TargetNodeIds)
                {
                    UMaterialExpression* Expression = FindMaterialExpressionById(MaterialAsset, TargetNodeId);
                    if (Expression == nullptr)
                    {
                        Execution.bOk = false;
                        Execution.Error = FString::Printf(TEXT("Material expression not found: %s"), *TargetNodeId);
                        return Execution;
                    }

                    Expression->MaterialExpressionEditorX += Dx;
                    Expression->MaterialExpressionEditorY += Dy;
                    MovedNodeIds.Add(MakeShared<FJsonValueString>(TargetNodeId));
                }

                Execution.bChanged = MovedNodeIds.Num() > 0;
                Execution.GraphEventName = TEXT("graph.node_moved");
                Execution.GraphEventData->SetArrayField(TEXT("nodeIds"), MovedNodeIds);
                Execution.GraphEventData->SetNumberField(TEXT("dx"), Dx);
                Execution.GraphEventData->SetNumberField(TEXT("dy"), Dy);
                Execution.GraphEventData->SetStringField(TEXT("op"), Op);
                for (const TSharedPtr<FJsonValue>& MovedNodeId : MovedNodeIds)
                {
                    FString MovedNodeIdString;
                    if (MovedNodeId.IsValid() && MovedNodeId->TryGetString(MovedNodeIdString))
                    {
                        Execution.NodesTouchedForLayout.Add(MovedNodeIdString);
                    }
                }
                return Execution;
            }

            if (Op.Equals(TEXT("connectpins")))
            {
                FMutateResolvedNodeTarget FromTarget;
                FMutateResolvedNodeTarget ToTarget;
                if (!TryResolveMutateNodeTargetField(ArgsObj, TEXT("from"), LocalNodeRefs, FromTarget, false)
                    || !TryResolveMutateNodeTargetField(ArgsObj, TEXT("to"), LocalNodeRefs, ToTarget, false))
                {
                    Execution.bOk = false;
                    Execution.Error = TEXT("connectPins requires from/to node references.");
                    return Execution;
                }

                const FString& FromNodeId = FromTarget.NodeId;
                const FString& ToNodeId = ToTarget.NodeId;
                const FString& FromPinName = FromTarget.PinName;
                const FString& ToPinName = ToTarget.PinName;
                UMaterialExpression* FromExpr = FindMaterialExpressionById(MaterialAsset, FromNodeId);
                UMaterialExpression* ToExpr = FindMaterialExpressionById(MaterialAsset, ToNodeId);
                if (FromExpr == nullptr)
                {
                    Execution.bOk = false;
                    Execution.Error = TEXT("Material expression not found.");
                    return Execution;
                }

                FString ResolvedFromPinName;
                Execution.bOk = ResolveMaterialSourceOutputPin(
                    FromNodeId,
                    FromExpr,
                    FromPinName,
                    ResolvedFromPinName,
                    Execution.Error,
                    Execution.ErrorDetailsForOp);
                if (!Execution.bOk)
                {
                    return Execution;
                }

                if (ToNodeId.Equals(TEXT("__material_root__")))
                {
                    EMaterialProperty Property = MP_MAX;
                    if (!ResolveMaterialPropertyByRootPinName(ToPinName, Property))
                    {
                        Execution.bOk = false;
                        Execution.Error = TEXT("Material root pin not found.");
                        return Execution;
                    }
                    if (!UMaterialEditingLibrary::ConnectMaterialProperty(FromExpr, ResolvedFromPinName, Property))
                    {
                        Execution.bOk = false;
                        Execution.Error = TEXT("Failed to connect material expression to material root.");
                        return Execution;
                    }

                    Execution.bChanged = true;
                    Execution.GraphEventName = TEXT("graph.node_connected");
                    Execution.GraphEventData->SetStringField(TEXT("fromNodeId"), FromNodeId);
                    Execution.GraphEventData->SetStringField(TEXT("fromPin"), ResolvedFromPinName);
                    Execution.GraphEventData->SetStringField(TEXT("toNodeId"), ToNodeId);
                    Execution.GraphEventData->SetStringField(TEXT("toPin"), ToPinName);
                    Execution.GraphEventData->SetStringField(TEXT("op"), Op);
                    Execution.NodesTouchedForLayout.Add(FromNodeId);
                    return Execution;
                }

                if (ToExpr == nullptr)
                {
                    Execution.bOk = false;
                    Execution.Error = TEXT("Material expression not found.");
                    return Execution;
                }

                int32 ResolvedToInputIndex = INDEX_NONE;
                FString ResolvedToPinName;
                Execution.bOk = ResolveMaterialTargetInputPin(
                    ToNodeId,
                    ToExpr,
                    ToPinName,
                    ResolvedToInputIndex,
                    ResolvedToPinName,
                    Execution.Error,
                    Execution.ErrorDetailsForOp);
                if (!Execution.bOk)
                {
                    return Execution;
                }

                TArray<FString> TargetPinCallCandidates;
                AddUniqueMaterialPinCandidate(TargetPinCallCandidates, ResolvedToPinName);
                if (ResolvedToInputIndex == 0 && GetMaterialExpressionInputCount(ToExpr) == 1)
                {
                    TargetPinCallCandidates.Add(TEXT(""));
                }

                bool bConnected = false;
                for (const FString& TargetPinCallCandidate : TargetPinCallCandidates)
                {
                    if (UMaterialEditingLibrary::ConnectMaterialExpressions(FromExpr, ResolvedFromPinName, ToExpr, TargetPinCallCandidate))
                    {
                        if (TargetPinCallCandidate.IsEmpty())
                        {
                            ResolvedToPinName = TEXT("");
                        }
                        bConnected = true;
                        break;
                    }
                }

                if (!bConnected)
                {
                    Execution.bOk = false;
                    Execution.Error = TEXT("Failed to connect material expressions.");
                    return Execution;
                }

                Execution.bChanged = true;
                Execution.GraphEventName = TEXT("graph.node_connected");
                Execution.GraphEventData->SetStringField(TEXT("fromNodeId"), FromNodeId);
                Execution.GraphEventData->SetStringField(TEXT("fromPin"), ResolvedFromPinName);
                Execution.GraphEventData->SetStringField(TEXT("toNodeId"), ToNodeId);
                Execution.GraphEventData->SetStringField(TEXT("toPin"), ResolvedToPinName);
                Execution.GraphEventData->SetStringField(TEXT("op"), Op);
                Execution.NodesTouchedForLayout.Add(FromNodeId);
                Execution.NodesTouchedForLayout.Add(ToNodeId);
                return Execution;
            }

            if (Op.Equals(TEXT("disconnectpins")) || Op.Equals(TEXT("breakpinlinks")))
            {
                FMutateResolvedNodeTarget SourceTarget;
                bool bHasSourceFilter = false;
                FMutateResolvedNodeTarget TargetTarget;
                if (Op.Equals(TEXT("disconnectpins")))
                {
                    if (!TryResolveOptionalMutateNodeTargetField(ArgsObj, TEXT("from"), LocalNodeRefs, SourceTarget, bHasSourceFilter, false))
                    {
                        Execution.bOk = false;
                        Execution.Error = TEXT("disconnectPins requires args.from node reference.");
                        return Execution;
                    }

                    if (!TryResolveMutateNodeTargetField(ArgsObj, TEXT("to"), LocalNodeRefs, TargetTarget, false))
                    {
                        Execution.bOk = false;
                        Execution.Error = TEXT("disconnectPins requires args.to.");
                        return Execution;
                    }
                }
                else
                {
                    if (!TryResolveMutateNodeTargetField(ArgsObj, TEXT("target"), LocalNodeRefs, TargetTarget, false))
                    {
                        Execution.bOk = false;
                        Execution.Error = TEXT("breakPinLinks requires args.target.");
                        return Execution;
                    }
                }

                const FString& SourceNodeId = SourceTarget.NodeId;
                const FString& SourcePinName = SourceTarget.PinName;
                const FString& TargetNodeId = TargetTarget.NodeId;
                const FString& TargetPinName = TargetTarget.PinName;
                if (TargetNodeId.Equals(TEXT("__material_root__")))
                {
                    EMaterialProperty Property = MP_MAX;
                    if (TargetPinName.IsEmpty() || !ResolveMaterialPropertyByRootPinName(TargetPinName, Property))
                    {
                        Execution.bOk = false;
                        Execution.Error = TEXT("Material root pin not found.");
                        return Execution;
                    }

                    if (FExpressionInput* Input = MaterialAsset->GetExpressionInputForProperty(Property))
                    {
                        if (Input->Expression == nullptr)
                        {
                            Execution.bOk = false;
                            Execution.Error = TEXT("No links were removed.");
                            return Execution;
                        }

                        if (bHasSourceFilter)
                        {
                            UMaterialExpression* SourceExpr = FindMaterialExpressionById(MaterialAsset, SourceNodeId);
                            if (SourceExpr == nullptr)
                            {
                                Execution.bOk = false;
                                Execution.Error = TEXT("Material expression not found.");
                                return Execution;
                            }

                            FString ResolvedSourcePinName;
                            Execution.bOk = ResolveMaterialSourceOutputPin(
                                SourceNodeId,
                                SourceExpr,
                                SourcePinName,
                                ResolvedSourcePinName,
                                Execution.Error,
                                Execution.ErrorDetailsForOp);
                            if (!Execution.bOk)
                            {
                                return Execution;
                            }
                            if (!DisconnectMaterialInputIfMatchesSource(Input, SourceExpr, ResolvedSourcePinName))
                            {
                                Execution.bOk = false;
                                Execution.Error = TEXT("No links were removed.");
                                return Execution;
                            }
                        }
                        else
                        {
                            DisconnectMaterialInput(Input);
                        }

                        Execution.bChanged = true;
                        Execution.GraphEventName = TEXT("graph.links_changed");
                        Execution.GraphEventData->SetStringField(TEXT("nodeId"), TargetNodeId);
                        Execution.GraphEventData->SetStringField(TEXT("pinName"), TargetPinName);
                        Execution.GraphEventData->SetStringField(TEXT("op"), Op);
                        return Execution;
                    }

                    Execution.bOk = false;
                    Execution.Error = TEXT("Material property input not found.");
                    return Execution;
                }

                UMaterialExpression* Expr = FindMaterialExpressionById(MaterialAsset, TargetNodeId);
                if (Expr == nullptr)
                {
                    Execution.bOk = false;
                    Execution.Error = TEXT("Material expression not found.");
                    return Execution;
                }

                UMaterialExpression* SourceExpr = nullptr;
                FString ResolvedSourcePinName;
                if (bHasSourceFilter)
                {
                    SourceExpr = FindMaterialExpressionById(MaterialAsset, SourceNodeId);
                    if (SourceExpr == nullptr)
                    {
                        Execution.bOk = false;
                        Execution.Error = TEXT("Material expression not found.");
                        return Execution;
                    }

                    Execution.bOk = ResolveMaterialSourceOutputPin(
                        SourceNodeId,
                        SourceExpr,
                        SourcePinName,
                        ResolvedSourcePinName,
                        Execution.Error,
                        Execution.ErrorDetailsForOp);
                    if (!Execution.bOk)
                    {
                        return Execution;
                    }
                }

                bool bDisconnected = false;
                if (TargetPinName.IsEmpty())
                {
                    const int32 MaxInputs = 128;
                    for (int32 InputIndex = 0; InputIndex < MaxInputs; ++InputIndex)
                    {
                        if (FExpressionInput* Input = Expr->GetInput(InputIndex))
                        {
                            if (!bHasSourceFilter)
                            {
                                if (DisconnectMaterialInput(Input))
                                {
                                    bDisconnected = true;
                                }
                            }
                            else if (DisconnectMaterialInputIfMatchesSource(Input, SourceExpr, ResolvedSourcePinName))
                            {
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
                    int32 InputIndex = INDEX_NONE;
                    FString ResolvedTargetPinName;
                    if (ResolveMaterialTargetInputPin(
                            TargetNodeId,
                            Expr,
                            TargetPinName,
                            InputIndex,
                            ResolvedTargetPinName,
                            Execution.Error,
                            Execution.ErrorDetailsForOp))
                    {
                        if (FExpressionInput* Input = Expr->GetInput(InputIndex))
                        {
                            if (!bHasSourceFilter)
                            {
                                if (DisconnectMaterialInput(Input))
                                {
                                    bDisconnected = true;
                                }
                            }
                            else if (DisconnectMaterialInputIfMatchesSource(Input, SourceExpr, ResolvedSourcePinName))
                            {
                                bDisconnected = true;
                            }
                        }
                    }
                    else if (Op.Equals(TEXT("breakpinlinks")) && BreakMaterialLinksBySourcePin(Expr, TargetPinName, Execution.NodesTouchedForLayout))
                    {
                        bDisconnected = true;
                        Execution.Error.Reset();
                        Execution.ErrorDetailsForOp.Reset();
                    }
                    else
                    {
                        Execution.bOk = false;
                        return Execution;
                    }
                }

                if (!bDisconnected)
                {
                    Execution.bOk = false;
                    Execution.Error = TEXT("No links were removed.");
                    return Execution;
                }

                Execution.bChanged = true;
                Execution.GraphEventName = TEXT("graph.links_changed");
                Execution.GraphEventData->SetStringField(TEXT("nodeId"), TargetNodeId);
                Execution.GraphEventData->SetStringField(TEXT("pinName"), TargetPinName);
                Execution.GraphEventData->SetStringField(TEXT("op"), Op);
                Execution.NodesTouchedForLayout.Add(TargetNodeId);
                return Execution;
            }

            if (Op.Equals(TEXT("layoutgraph")))
            {
                ExecuteMutateLayoutGraphOp(
                    Op,
                    GraphName,
                    ArgsObj,
                    Execution,
                    [&ResolveNodeTokenArrayFromArgsLocal](const TSharedPtr<FJsonObject>& SourceArgs, TArray<FString>& OutNodeIds)
                    {
                        ResolveNodeTokenArrayFromArgsLocal(SourceArgs, OutNodeIds);
                    },
                    [this, &GraphType, &AssetPath](const FString& LayoutGraphName, TArray<FString>& OutNodeIds, bool bConsume)
                    {
                        ResolvePendingGraphLayoutNodes(GraphType, AssetPath, LayoutGraphName, OutNodeIds, bConsume);
                    },
                    [this, &AssetPath](const FString& LayoutGraphName, const FString& LayoutScope, const TArray<FString>& RequestedNodeIds, TArray<FString>& OutMovedNodeIds, FString& OutError)
                    {
                        return ApplyMaterialLayout(AssetPath, LayoutGraphName, LayoutScope, RequestedNodeIds, OutMovedNodeIds, OutError);
                    });
                return Execution;
            }

            if (Op.Equals(TEXT("compile")))
            {
                UMaterialEditingLibrary::RecompileMaterial(MaterialAsset);
                Execution.GraphEventName = TEXT("graph.compiled");
                Execution.GraphEventData->SetStringField(TEXT("op"), Op);
                return Execution;
            }

            Execution.bOk = false;
            Execution.Error = FString::Printf(TEXT("Unsupported op for material: %s"), *Op);
            return Execution;
        };

        auto ExecutePcgMutateOp = [&](const FString& Op, const TSharedPtr<FJsonObject>& ArgsObj) -> FAssetMutateOpExecution
        {
            FAssetMutateOpExecution Execution;

            if (Op.Equals(TEXT("addnode.byclass")))
            {
                FString SettingsClassPath;
                ArgsObj->TryGetStringField(TEXT("nodeClassPath"), SettingsClassPath);

                UClass* SettingsClass = LoadObject<UClass>(nullptr, *SettingsClassPath);
                if (SettingsClass == nullptr || !SettingsClass->IsChildOf(UPCGSettings::StaticClass()))
                {
                    Execution.bOk = false;
                    Execution.Error = TEXT("Invalid PCG settings class.");
                    return Execution;
                }

                UPCGSettings* DefaultSettings = nullptr;
                UPCGNode* NewNode = PcgGraph->AddNodeOfType(SettingsClass, DefaultSettings);
                if (NewNode == nullptr)
                {
                    Execution.bOk = false;
                    Execution.Error = TEXT("Failed to create PCG node.");
                    return Execution;
                }

                int32 X = 0;
                int32 Y = 0;
                if (HasExplicitPositionLocal(ArgsObj))
                {
                    GetPointFromObjectLocal(ArgsObj, X, Y);
                }
                else
                {
                    ResolveAutoPlacementLocal(ArgsObj, X, Y);
                }
                NewNode->SetNodePosition(X, Y);
                Execution.NodeId = NewNode->GetPathName();
                Execution.bChanged = true;
                Execution.GraphEventName = TEXT("graph.node_added");
                Execution.GraphEventData->SetStringField(TEXT("nodeId"), Execution.NodeId);
                Execution.GraphEventData->SetStringField(TEXT("nodeType"), TEXT("by_class"));
                Execution.GraphEventData->SetStringField(TEXT("nodeClassPath"), SettingsClassPath);
                Execution.GraphEventData->SetStringField(TEXT("op"), Op);
                Execution.NodesTouchedForLayout.Add(Execution.NodeId);
                return Execution;
            }

            if (Op.Equals(TEXT("removenode")))
            {
                FString TargetNodeId;
                Execution.bOk = ResolveStableNodeTokenFromArgsLocal(ArgsObj, TargetNodeId);
                if (!Execution.bOk)
                {
                    Execution.Error = TEXT("PCG removeNode requires a stable target: provide nodeId, nodeRef, nodePath, or path.");
                    return Execution;
                }

                if (UPCGNode* Node = FindPcgNodeById(PcgGraph, TargetNodeId))
                {
                    TArray<FString> AdjacentNodeIds;
                    CollectPcgAdjacentNodeIdsLocal(Node, AdjacentNodeIds);
                    const FString RemovedNodePath = Node->GetPathName();
                    PcgGraph->RemoveNode(Node);
                    for (UPCGNode* RemainingNode : PcgGraph->GetNodes())
                    {
                        if (RemainingNode != nullptr && RemainingNode->GetPathName().Equals(RemovedNodePath, ESearchCase::IgnoreCase))
                        {
                            Execution.bOk = false;
                            Execution.Error = TEXT("PCG node remained in graph after removal attempt.");
                            return Execution;
                        }
                    }

                    Execution.bChanged = true;
                    Execution.GraphEventName = TEXT("graph.node_removed");
                    Execution.GraphEventData->SetStringField(TEXT("nodeId"), RemovedNodePath);
                    Execution.GraphEventData->SetStringField(TEXT("op"), Op);
                    for (const FString& AdjacentNodeId : AdjacentNodeIds)
                    {
                        if (!AdjacentNodeId.IsEmpty())
                        {
                            Execution.NodesTouchedForLayout.Add(AdjacentNodeId);
                        }
                    }
                    return Execution;
                }

                Execution.bOk = false;
                Execution.Error = TEXT("PCG node not found.");
                return Execution;
            }

            if (Op.Equals(TEXT("movenode")) || Op.Equals(TEXT("movenodeby")))
            {
                FString TargetNodeId;
                Execution.bOk = ResolveNodeTokenFromArgsLocal(ArgsObj, TargetNodeId);
                if (!Execution.bOk)
                {
                    Execution.Error = FString::Printf(TEXT("Missing target nodeId/path/name for %s."), *Op);
                    return Execution;
                }

                if (UPCGNode* Node = FindPcgNodeById(PcgGraph, TargetNodeId))
                {
                    int32 X = 0;
                    int32 Y = 0;
                    if (Op.Equals(TEXT("movenodeby")))
                    {
                        int32 CurrentX = 0;
                        int32 CurrentY = 0;
                        Node->GetNodePosition(CurrentX, CurrentY);
                        int32 Dx = 0;
                        int32 Dy = 0;
                        GetDeltaFromObjectLocal(ArgsObj, Dx, Dy);
                        X = CurrentX + Dx;
                        Y = CurrentY + Dy;
                    }
                    else
                    {
                        GetPointFromObjectLocal(ArgsObj, X, Y);
                    }
                    Node->SetNodePosition(X, Y);
                    Execution.bChanged = true;
                    Execution.GraphEventName = TEXT("graph.node_moved");
                    Execution.GraphEventData->SetStringField(TEXT("nodeId"), TargetNodeId);
                    Execution.GraphEventData->SetNumberField(TEXT("x"), X);
                    Execution.GraphEventData->SetNumberField(TEXT("y"), Y);
                    Execution.GraphEventData->SetStringField(TEXT("op"), Op);
                    Execution.NodesTouchedForLayout.Add(TargetNodeId);
                    return Execution;
                }

                Execution.bOk = false;
                Execution.Error = TEXT("PCG node not found.");
                return Execution;
            }

            if (Op.Equals(TEXT("movenodes")))
            {
                TArray<FString> TargetNodeIds;
                Execution.bOk = ResolveNodeTokenArrayFromArgsLocal(ArgsObj, TargetNodeIds);
                if (!Execution.bOk)
                {
                    Execution.Error = TEXT("Missing nodeIds/nodes for moveNodes.");
                    return Execution;
                }

                int32 Dx = 0;
                int32 Dy = 0;
                GetDeltaFromObjectLocal(ArgsObj, Dx, Dy);

                TArray<FString> MovedNodeIds;
                for (const FString& TargetNodeId : TargetNodeIds)
                {
                    UPCGNode* Node = FindPcgNodeById(PcgGraph, TargetNodeId);
                    if (Node == nullptr)
                    {
                        Execution.bOk = false;
                        Execution.Error = FString::Printf(TEXT("PCG node not found: %s"), *TargetNodeId);
                        return Execution;
                    }

                    int32 CurrentX = 0;
                    int32 CurrentY = 0;
                    Node->GetNodePosition(CurrentX, CurrentY);
                    Node->SetNodePosition(CurrentX + Dx, CurrentY + Dy);
                    MovedNodeIds.Add(TargetNodeId);
                }

                Execution.bChanged = MovedNodeIds.Num() > 0;
                Execution.GraphEventName = TEXT("graph.node_moved");
                Execution.GraphEventData->SetArrayField(TEXT("nodeIds"), MakeJsonStringArray(MovedNodeIds));
                Execution.GraphEventData->SetNumberField(TEXT("dx"), Dx);
                Execution.GraphEventData->SetNumberField(TEXT("dy"), Dy);
                Execution.GraphEventData->SetStringField(TEXT("op"), Op);
                Execution.NodesTouchedForLayout.Append(MovedNodeIds);
                return Execution;
            }

            if (Op.Equals(TEXT("connectpins")) || Op.Equals(TEXT("disconnectpins")))
            {
                FMutateResolvedNodeTarget FromTarget;
                FMutateResolvedNodeTarget ToTarget;
                if (!TryResolveMutateNodeTargetField(ArgsObj, TEXT("from"), LocalNodeRefs, FromTarget, true)
                    || !TryResolveMutateNodeTargetField(ArgsObj, TEXT("to"), LocalNodeRefs, ToTarget, true))
                {
                    Execution.bOk = false;
                    Execution.Error = FString::Printf(TEXT("%s requires from/to node references."), *Op);
                    return Execution;
                }

                const FString& FromNodeId = FromTarget.NodeId;
                const FString& ToNodeId = ToTarget.NodeId;
                const FString& FromPinName = FromTarget.PinName;
                const FString& ToPinName = ToTarget.PinName;
                UPCGNode* FromNode = FindPcgNodeById(PcgGraph, FromNodeId);
                UPCGNode* ToNode = FindPcgNodeById(PcgGraph, ToNodeId);
                if (FromNode == nullptr || ToNode == nullptr)
                {
                    Execution.bOk = false;
                    Execution.Error = TEXT("PCG node not found.");
                    return Execution;
                }
                if (FindPcgPin(FromNode, FromPinName, true) == nullptr)
                {
                    Execution.bOk = false;
                    Execution.Error = TEXT("PCG output pin not found.");
                    return Execution;
                }
                if (FindPcgPin(ToNode, ToPinName, false) == nullptr)
                {
                    Execution.bOk = false;
                    Execution.Error = TEXT("PCG input pin not found.");
                    return Execution;
                }

                if (Op.Equals(TEXT("connectpins")))
                {
                    Execution.bOk = (PcgGraph->AddEdge(FromNode, FName(*FromPinName), ToNode, FName(*ToPinName)) != nullptr);
                    if (!Execution.bOk)
                    {
                        Execution.Error = TEXT("Failed to add PCG edge.");
                        return Execution;
                    }

                    Execution.bChanged = true;
                    Execution.GraphEventName = TEXT("graph.node_connected");
                }
                else
                {
                    Execution.bOk = PcgGraph->RemoveEdge(FromNode, FName(*FromPinName), ToNode, FName(*ToPinName));
                    if (!Execution.bOk)
                    {
                        Execution.Error = TEXT("Failed to remove PCG edge.");
                        return Execution;
                    }

                    Execution.bChanged = true;
                    Execution.GraphEventName = TEXT("graph.links_changed");
                }

                Execution.GraphEventData->SetStringField(TEXT("fromNodeId"), FromNodeId);
                Execution.GraphEventData->SetStringField(TEXT("fromPin"), FromPinName);
                Execution.GraphEventData->SetStringField(TEXT("toNodeId"), ToNodeId);
                Execution.GraphEventData->SetStringField(TEXT("toPin"), ToPinName);
                Execution.GraphEventData->SetStringField(TEXT("op"), Op);
                Execution.NodesTouchedForLayout.Add(FromNodeId);
                Execution.NodesTouchedForLayout.Add(ToNodeId);
                return Execution;
            }

            if (Op.Equals(TEXT("breakpinlinks")))
            {
                FMutateResolvedNodeTarget TargetTarget;
                if (!TryResolveMutateNodeTargetField(ArgsObj, TEXT("target"), LocalNodeRefs, TargetTarget, false))
                {
                    Execution.bOk = false;
                    Execution.Error = TEXT("breakPinLinks requires args.target.");
                    return Execution;
                }

                const FString& TargetNodeId = TargetTarget.NodeId;
                const FString& TargetPinName = TargetTarget.PinName;
                UPCGNode* Node = FindPcgNodeById(PcgGraph, TargetNodeId);
                if (Node == nullptr)
                {
                    Execution.bOk = false;
                    Execution.Error = TEXT("PCG node not found.");
                    return Execution;
                }

                UPCGPin* Pin = FindPcgPin(Node, TargetPinName, false);
                if (Pin == nullptr)
                {
                    Pin = FindPcgPin(Node, TargetPinName, true);
                }
                if (Pin == nullptr)
                {
                    Execution.bOk = false;
                    Execution.Error = TEXT("PCG pin not found.");
                    return Execution;
                }

                Execution.bOk = Pin->BreakAllEdges();
                if (!Execution.bOk)
                {
                    Execution.Error = TEXT("No links were removed.");
                    return Execution;
                }

                Execution.bChanged = true;
                Execution.GraphEventName = TEXT("graph.links_changed");
                Execution.GraphEventData->SetStringField(TEXT("nodeId"), TargetNodeId);
                Execution.GraphEventData->SetStringField(TEXT("pinName"), TargetPinName);
                Execution.GraphEventData->SetStringField(TEXT("op"), Op);
                Execution.NodesTouchedForLayout.Add(TargetNodeId);
                return Execution;
            }

            if (Op.Equals(TEXT("setpindefault")))
            {
                FMutateResolvedNodeTarget TargetTarget;
                FString DefaultValue;
                if (!TryResolveMutateNodeTargetField(ArgsObj, TEXT("target"), LocalNodeRefs, TargetTarget, false))
                {
                    Execution.bOk = false;
                    Execution.Error = TEXT("setPinDefault requires args.target.");
                    Execution.ErrorDetailsForOp = BuildPcgSetPinDefaultDiagnostics(nullptr, FString());
                    return Execution;
                }
                if (TargetTarget.PinName.IsEmpty())
                {
                    Execution.bOk = false;
                    Execution.Error = TEXT("setPinDefault requires args.target.pin.");
                    Execution.ErrorDetailsForOp = BuildPcgSetPinDefaultDiagnostics(nullptr, FString());
                    return Execution;
                }
                if (!TryResolveValueStringField(ArgsObj, DefaultValue))
                {
                    Execution.bOk = false;
                    Execution.Error = TEXT("setPinDefault requires args.value as a string, number, or boolean.");
                    return Execution;
                }

                const FString& TargetNodeId = TargetTarget.NodeId;
                const FString& TargetPinName = TargetTarget.PinName;
                if (UPCGNode* Node = FindPcgNodeById(PcgGraph, TargetNodeId))
                {
                    Execution.bOk = SetPcgPinDefaultValue(Node, TargetPinName, DefaultValue, Execution.Error, !bDryRun);
                    if (!Execution.bOk)
                    {
                        if (Execution.Error.IsEmpty())
                        {
                            Execution.Error = TEXT("Failed to set PCG pin default value.");
                        }
                        Execution.ErrorDetailsForOp = BuildPcgSetPinDefaultDiagnostics(Node, TargetPinName);
                        return Execution;
                    }

                    Execution.NodeId = TargetNodeId;
                    if (!bDryRun)
                    {
                        Execution.bChanged = true;
                        Execution.GraphEventName = TEXT("graph.pin_default_changed");
                        Execution.GraphEventData->SetStringField(TEXT("nodeId"), TargetNodeId);
                        Execution.GraphEventData->SetStringField(TEXT("pin"), TargetPinName);
                        Execution.GraphEventData->SetStringField(TEXT("value"), DefaultValue);
                        Execution.GraphEventData->SetStringField(TEXT("op"), Op);
                        Execution.NodesTouchedForLayout.Add(TargetNodeId);
                    }
                    return Execution;
                }

                Execution.bOk = false;
                Execution.Error = TEXT("PCG node not found.");
                Execution.ErrorDetailsForOp = BuildPcgSetPinDefaultDiagnostics(nullptr, TargetPinName);
                return Execution;
            }

            if (Op.Equals(TEXT("layoutgraph")))
            {
                ExecuteMutateLayoutGraphOp(
                    Op,
                    GraphName,
                    ArgsObj,
                    Execution,
                    [&ResolveNodeTokenArrayFromArgsLocal](const TSharedPtr<FJsonObject>& SourceArgs, TArray<FString>& OutNodeIds)
                    {
                        ResolveNodeTokenArrayFromArgsLocal(SourceArgs, OutNodeIds);
                    },
                    [this, &GraphType, &AssetPath](const FString& LayoutGraphName, TArray<FString>& OutNodeIds, bool bConsume)
                    {
                        ResolvePendingGraphLayoutNodes(GraphType, AssetPath, LayoutGraphName, OutNodeIds, bConsume);
                    },
                    [this, &AssetPath](const FString& LayoutGraphName, const FString& LayoutScope, const TArray<FString>& RequestedNodeIds, TArray<FString>& OutMovedNodeIds, FString& OutError)
                    {
                        return ApplyPcgLayout(AssetPath, LayoutGraphName, LayoutScope, RequestedNodeIds, OutMovedNodeIds, OutError);
                    });
                return Execution;
            }

            if (Op.Equals(TEXT("compile")))
            {
                const bool bCompilationChanged = PcgGraph->Recompile();
                Execution.bChanged = bCompilationChanged;
                Execution.GraphEventName = TEXT("graph.compiled");
                Execution.GraphEventData->SetStringField(TEXT("op"), Op);
                Execution.GraphEventData->SetBoolField(TEXT("compilationChanged"), bCompilationChanged);
                return Execution;
            }

            Execution.bOk = false;
            Execution.Error = FString::Printf(TEXT("Unsupported op for pcg: %s"), *Op);
            return Execution;
        };

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
            TArray<FString> NodesTouchedForLayout;
            FString GraphEventName;
            TSharedPtr<FJsonObject> GraphEventData = MakeShared<FJsonObject>();
            TSharedPtr<FJsonObject> ErrorDetailsForOp;
            (*OpObj)->TryGetStringField(TEXT("clientRef"), ClientRef);

            const TSharedPtr<FJsonObject>* ArgsObjPtr = nullptr;
            const TSharedPtr<FJsonObject> ArgsObj =
                ((*OpObj)->TryGetObjectField(TEXT("args"), ArgsObjPtr) && ArgsObjPtr && (*ArgsObjPtr).IsValid())
                    ? *ArgsObjPtr
                    : MakeShared<FJsonObject>();

            if (Op.Equals(TEXT("runscript")))
            {
                bOk = false;
                Error = TEXT("Unsupported mutate op: runScript");
            }
            else if (!bDryRun || (GraphType.Equals(TEXT("pcg")) && Op.Equals(TEXT("setpindefault"))))
            {
                const FAssetMutateOpExecution Execution = GraphType.Equals(TEXT("material"))
                    ? ExecuteMaterialMutateOp(Op, ArgsObj)
                    : ExecutePcgMutateOp(Op, ArgsObj);
                bOk = Execution.bOk;
                bChanged = Execution.bChanged;
                Error = Execution.Error;
                NodeId = Execution.NodeId;
                NodesTouchedForLayout = Execution.NodesTouchedForLayout;
                GraphEventName = Execution.GraphEventName;
                GraphEventData = Execution.GraphEventData.IsValid() ? Execution.GraphEventData : MakeShared<FJsonObject>();
                ErrorDetailsForOp = Execution.ErrorDetailsForOp;
            }

            ApplyAssetMutateSideEffects(MutableAsset, MaterialAsset, PcgGraph, bChanged);

            if (bOk)
            {
                RecordDeferredLayoutTargetsFromMutate(
                    GraphType,
                    AssetPath,
                    GraphName,
                    Op,
                    NodesTouchedForLayout,
                    [this](const FString& PendingGraphType, const FString& PendingAssetPath, const FString& PendingGraphName, const TArray<FString>& PendingNodeIds)
                    {
                        RecordPendingGraphLayoutNodes(PendingGraphType, PendingAssetPath, PendingGraphName, PendingNodeIds);
                    });
            }

            if (bOk && !ClientRef.IsEmpty() && !NodeId.IsEmpty())
            {
                if (LocalNodeRefs.Contains(ClientRef))
                {
                    bOk = false;
                    bChanged = false;
                    Error = FString::Printf(TEXT("Duplicate clientRef in mutate batch: %s"), *ClientRef);
                }
                else
                {
                    LocalNodeRefs.Add(ClientRef, NodeId);
                }
            }

            const FString OpErrorCode = bOk ? TEXT("") : InferMutateErrorCode(Op, Error);
            const TArray<FString>* MovedNodeIds = Op.Equals(TEXT("layoutgraph")) ? &NodesTouchedForLayout : nullptr;
            LocalOpResults.Add(MakeShared<FJsonValueObject>(BuildMutateOpResultObject(
                Index,
                Op,
                bOk,
                bChanged,
                NodeId,
                Error,
                OpErrorCode,
                false,
                false,
                TEXT(""),
                true,
                ErrorDetailsForOp,
                nullptr,
                MovedNodeIds)));
            if (bOk && bChanged && !bDryRun)
            {
                bAnyChangedLocal = true;
            }

            if (!bOk)
            {
                bAnyErrorLocal = true;
                if (FirstErrorLocal.IsEmpty())
                {
                    FirstErrorLocal = Error;
                }
                if (FirstErrorCodeLocal.IsEmpty())
                {
                    FirstErrorCodeLocal = OpErrorCode;
                }
                LocalDiagnostics.Add(MakeShared<FJsonValueObject>(BuildMutateDiagnosticObject(
                    OpErrorCode,
                    Op,
                    Error,
                    NodeId,
                    AssetPath,
                    GraphName,
                    ErrorDetailsForOp)));
                if (bStopOnError)
                {
                    break;
                }
            }
        }

        Result->SetBoolField(TEXT("isError"), bAnyErrorLocal);
        if (bAnyErrorLocal)
        {
            Result->SetStringField(TEXT("code"), FirstErrorCodeLocal.IsEmpty() ? TEXT("INTERNAL_ERROR") : FirstErrorCodeLocal);
            Result->SetStringField(TEXT("message"), FirstErrorLocal.IsEmpty() ? TEXT("graph.mutate failed") : FirstErrorLocal);
        }
        Result->SetBoolField(TEXT("applied"), !bAnyErrorLocal);
        Result->SetBoolField(TEXT("partialApplied"), bAnyErrorLocal && bAnyChangedLocal);
        Result->SetStringField(TEXT("graphType"), GraphType);
        Result->SetStringField(TEXT("assetPath"), AssetPath);
        Result->SetStringField(TEXT("graphName"), GraphName);
        {
            GraphAddress.GraphName = GraphName;
            GraphAddress.InlineNodeGuid = bUsedGraphRef ? InlineNodeGuid : TEXT("");
            Result->SetObjectField(TEXT("graphRef"), MakeEffectiveGraphRef(GraphAddress));
        }
        FString NewRevision = PreviousRevision;
        if (!bDryRun && !bAnyErrorLocal)
        {
            FString PostRevisionCode;
            FString PostRevisionMessage;
            if (ResolveActualRevision(NewRevision, PostRevisionCode, PostRevisionMessage))
            {
                Result->SetStringField(TEXT("previousRevision"), PreviousRevision);
                Result->SetStringField(TEXT("newRevision"), NewRevision);
            }
            else
            {
                Result->SetStringField(TEXT("previousRevision"), PreviousRevision);
                Result->SetStringField(TEXT("newRevision"), PreviousRevision);
            }
        }
        else
        {
            Result->SetStringField(TEXT("previousRevision"), PreviousRevision);
            Result->SetStringField(TEXT("newRevision"), PreviousRevision);
        }
        Result->SetArrayField(TEXT("opResults"), LocalOpResults);
        Result->SetArrayField(TEXT("diagnostics"), LocalDiagnostics);
        if (!IdempotencyRegistryKey.IsEmpty() && !bAnyErrorLocal)
        {
            FScopeLock Lock(&MutateIdempotencyRegistryMutex);
            MutateIdempotencyRegistry.FindOrAdd(IdempotencyRegistryKey) = FMutateIdempotencyEntry{
                RequestFingerprint,
                CloneJsonObject(Result),
                FPlatformTime::Seconds(),
            };
            while (MutateIdempotencyRegistry.Num() > LoomleBridgeConstants::MaxMutateIdempotencyEntries)
            {
                FString OldestKey;
                double OldestTime = TNumericLimits<double>::Max();
                for (const TPair<FString, FMutateIdempotencyEntry>& Entry : MutateIdempotencyRegistry)
                {
                    if (Entry.Value.CreatedAtSeconds < OldestTime)
                    {
                        OldestTime = Entry.Value.CreatedAtSeconds;
                        OldestKey = Entry.Key;
                    }
                }
                if (OldestKey.IsEmpty())
                {
                    break;
                }
                MutateIdempotencyRegistry.Remove(OldestKey);
            }
        }
        return Result;
    }

    TMap<FString, FString> NodeRefs;
    TArray<TSharedPtr<FJsonValue>> OpResults;
    bool bAnyError = false;
    bool bAnyChanged = false;
    FString FirstError;
    FString FirstErrorCode;

    TUniquePtr<FScopedTransaction> Transaction;
    if (!bDryRun && GraphType.Equals(TEXT("blueprint")))
    {
        Transaction = MakeUnique<FScopedTransaction>(FText::FromString(FString::Printf(TEXT("Loomle graph.mutate %s"), *GraphName)));
    }

    bGraphMutateInProgress.Store(!bDryRun);
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

        bool bOk = true;
        bool bChanged = false;
        bool bSkipped = false;
        FString Error;
        FString SkipReason;
        FString NodeId;
        TArray<FString> NodesTouchedForLayout;
        FString ClientRef;
        FString GraphEventName;
        TSharedPtr<FJsonObject> GraphEventData = MakeShared<FJsonObject>();
        TSharedPtr<FJsonObject> ErrorDetailsForOp;
        TSharedPtr<FJsonObject> ScriptResultForOp;
        (*OpObj)->TryGetStringField(TEXT("clientRef"), ClientRef);

        const TSharedPtr<FJsonObject>* ArgsObjPtr = nullptr;
        const TSharedPtr<FJsonObject> ArgsObj =
            ((*OpObj)->TryGetObjectField(TEXT("args"), ArgsObjPtr) && ArgsObjPtr && (*ArgsObjPtr).IsValid())
                ? *ArgsObjPtr
                : MakeShared<FJsonObject>();

        const bool bHasTargetGraphName = (*OpObj)->TryGetStringField(TEXT("targetGraphName"), OpGraphName) && !OpGraphName.IsEmpty();
        const TSharedPtr<FJsonObject>* TargetGraphRefObj = nullptr;
        bool bHasTargetGraphRef = (*OpObj)->TryGetObjectField(TEXT("targetGraphRef"), TargetGraphRefObj)
            && TargetGraphRefObj && (*TargetGraphRefObj).IsValid();
        if (!bHasTargetGraphRef)
        {
            bHasTargetGraphRef = ArgsObj->TryGetObjectField(TEXT("graphRef"), TargetGraphRefObj)
                && TargetGraphRefObj && (*TargetGraphRefObj).IsValid();
        }

        if (bHasTargetGraphRef && bHasTargetGraphName)
        {
            bOk = false;
            Error = TEXT("Supply either targetGraphRef/args.graphRef or targetGraphName, not both.");
        }
        else if (bHasTargetGraphRef)
        {
            FResolvedGraphAddress TargetGraphAddress;
            FString GraphRefErrorMessage;
            if (!ResolveTargetGraphRefObject(*TargetGraphRefObj, GraphType, AssetPath, GraphName, TargetGraphAddress, GraphRefErrorMessage))
            {
                bOk = false;
                Error = GraphRefErrorMessage;
            }
            else
            {
                OpGraphName = TargetGraphAddress.GraphName;
            }
        }

        const bool bRunScriptOp = Op.Equals(TEXT("runscript"));
        if (bOk && (!bDryRun || bRunScriptOp))
        {
            const FBlueprintMutateOpExecution Execution = ExecuteBlueprintMutateOp(
                Op,
                AssetPath,
                OpGraphName,
                ArgsObj,
                NodeRefs,
                bDryRun,
                Index,
                [this, &GraphType, &AssetPath](const FString& LayoutGraphName, TArray<FString>& OutNodeIds, bool bConsume) -> bool
                {
                    return ResolvePendingGraphLayoutNodes(GraphType, AssetPath, LayoutGraphName, OutNodeIds, bConsume);
                },
                [this, &AssetPath](const FString& LayoutGraphName, const FString& LayoutScope, const TArray<FString>& RequestedNodeIds, TArray<FString>& OutMovedNodeIds, FString& OutError) -> bool
                {
                    return ApplyBlueprintLayout(AssetPath, LayoutGraphName, LayoutScope, RequestedNodeIds, OutMovedNodeIds, OutError);
                });
            bOk = Execution.bOk;
            bChanged = Execution.bChanged;
            bSkipped = Execution.bSkipped;
            Error = Execution.Error;
            SkipReason = Execution.SkipReason;
            NodeId = Execution.NodeId;
            NodesTouchedForLayout = Execution.NodesTouchedForLayout;
            GraphEventName = Execution.GraphEventName;
            GraphEventData = Execution.GraphEventData.IsValid() ? Execution.GraphEventData : MakeShared<FJsonObject>();
            ErrorDetailsForOp = Execution.ErrorDetailsForOp;
            ScriptResultForOp = Execution.ScriptResultForOp;
        }

        if (!bDryRun && bOk && !Op.Equals(TEXT("runscript")) && !Op.Equals(TEXT("compile")))
        {
            bChanged = true;
        }

        if (!ClientRef.IsEmpty() && !NodeId.IsEmpty())
        {
            if (NodeRefs.Contains(ClientRef))
            {
                bOk = false;
                bChanged = false;
                Error = FString::Printf(TEXT("Duplicate clientRef in mutate batch: %s"), *ClientRef);
            }
            else
            {
                NodeRefs.Add(ClientRef, NodeId);
            }
        }

        if (bOk)
        {
            RecordDeferredLayoutTargetsFromMutate(
                GraphType,
                AssetPath,
                OpGraphName,
                Op,
                NodesTouchedForLayout,
                [this](const FString& PendingGraphType, const FString& PendingAssetPath, const FString& PendingGraphName, const TArray<FString>& PendingNodeIds)
                {
                    RecordPendingGraphLayoutNodes(PendingGraphType, PendingAssetPath, PendingGraphName, PendingNodeIds);
                });
        }

        const FString OpErrorCode = bOk ? TEXT("") : InferMutateErrorCode(Op, Error);
        const TArray<FString>* MovedNodeIds = Op.Equals(TEXT("layoutgraph")) ? &NodesTouchedForLayout : nullptr;
        OpResults.Add(MakeShared<FJsonValueObject>(BuildMutateOpResultObject(
            Index,
            Op,
            bOk,
            bChanged,
            NodeId,
            Error,
            OpErrorCode,
            true,
            bSkipped,
            SkipReason,
            false,
            ErrorDetailsForOp,
            ScriptResultForOp,
            MovedNodeIds)));
        if (bOk && bChanged && !bDryRun)
        {
            bAnyChanged = true;
        }

        if (!bOk)
        {
            bAnyError = true;
            if (FirstError.IsEmpty())
            {
                FirstError = Error;
            }
            if (FirstErrorCode.IsEmpty())
            {
                FirstErrorCode = OpErrorCode;
            }
            if (bStopOnError)
            {
                break;
            }
        }

    }
    bGraphMutateInProgress.Store(false);

    Result->SetBoolField(TEXT("isError"), bAnyError);
    if (bAnyError)
    {
        Result->SetStringField(TEXT("code"), FirstErrorCode.IsEmpty() ? TEXT("INTERNAL_ERROR") : FirstErrorCode);
        Result->SetStringField(TEXT("message"), FirstError.IsEmpty() ? TEXT("graph.mutate failed") : FirstError);
    }

    Result->SetBoolField(TEXT("applied"), !bAnyError);
    Result->SetBoolField(TEXT("partialApplied"), bAnyError && bAnyChanged);
    Result->SetStringField(TEXT("graphType"), GraphType);
    Result->SetStringField(TEXT("assetPath"), AssetPath);
    Result->SetStringField(TEXT("graphName"), GraphName);
    {
        GraphAddress.GraphName = GraphName;
        GraphAddress.InlineNodeGuid = bUsedGraphRef ? InlineNodeGuid : TEXT("");
        Result->SetObjectField(TEXT("graphRef"), MakeEffectiveGraphRef(GraphAddress));
    }
    FString NewRevision = PreviousRevision;
    if (!bDryRun && !bAnyError)
    {
        FString PostRevisionCode;
        FString PostRevisionMessage;
        if (ResolveActualRevision(NewRevision, PostRevisionCode, PostRevisionMessage))
        {
            Result->SetStringField(TEXT("previousRevision"), PreviousRevision);
            Result->SetStringField(TEXT("newRevision"), NewRevision);
        }
        else
        {
            Result->SetStringField(TEXT("previousRevision"), PreviousRevision);
            Result->SetStringField(TEXT("newRevision"), PreviousRevision);
        }
    }
    else
    {
        Result->SetStringField(TEXT("previousRevision"), PreviousRevision);
        Result->SetStringField(TEXT("newRevision"), PreviousRevision);
    }
    Result->SetArrayField(TEXT("opResults"), OpResults);
    Result->SetArrayField(TEXT("diagnostics"), TArray<TSharedPtr<FJsonValue>>{});
    if (!IdempotencyRegistryKey.IsEmpty() && !bAnyError)
    {
        FScopeLock Lock(&MutateIdempotencyRegistryMutex);
        MutateIdempotencyRegistry.FindOrAdd(IdempotencyRegistryKey) = FMutateIdempotencyEntry{
            RequestFingerprint,
            CloneJsonObject(Result),
            FPlatformTime::Seconds(),
        };
        while (MutateIdempotencyRegistry.Num() > LoomleBridgeConstants::MaxMutateIdempotencyEntries)
        {
            FString OldestKey;
            double OldestTime = TNumericLimits<double>::Max();
            for (const TPair<FString, FMutateIdempotencyEntry>& Entry : MutateIdempotencyRegistry)
            {
                if (Entry.Value.CreatedAtSeconds < OldestTime)
                {
                    OldestTime = Entry.Value.CreatedAtSeconds;
                    OldestKey = Entry.Key;
                }
            }
            if (OldestKey.IsEmpty())
            {
                break;
            }
            MutateIdempotencyRegistry.Remove(OldestKey);
        }
    }
    return Result;
}
