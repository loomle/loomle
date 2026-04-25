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

// Shared mutate execution DTOs and result builders.
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

// Shared JSON coercion and endpoint contracts for domain mutate handlers.
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

// Shared layout and post-change helpers for domain mutate handlers.
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
        FString ObjectPath;
        FString TextValue;
        Execution.bOk = TryResolveMutatePinEndpointField(ArgsObj, TEXT("target"), NodeRefs, TargetEndpoint);
        if (Execution.bOk)
        {
            ArgsObj->TryGetStringField(TEXT("value"), Value);
            ArgsObj->TryGetStringField(TEXT("object"), ObjectPath);
            ArgsObj->TryGetStringField(TEXT("defaultObject"), ObjectPath);
            ArgsObj->TryGetStringField(TEXT("defaultObjectPath"), ObjectPath);
            ArgsObj->TryGetStringField(TEXT("text"), TextValue);
            Execution.bOk = FLoomleBlueprintAdapter::SetPinDefaultValue(
                AssetPath,
                GraphName,
                TargetEndpoint.NodeId,
                TargetEndpoint.PinName,
                Value,
                ObjectPath,
                TextValue,
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

bool ResolvePcgChildGraphReference(
    UPCGSettings* NodeSettings,
    FString& OutAssetPath,
    FString& OutLoadStatus,
    FString& OutGraphName,
    FString& OutGraphClassPath)
{
    return ResolvePcgSubgraphAssetReference(
        NodeSettings,
        OutAssetPath,
        OutLoadStatus,
        OutGraphName,
        OutGraphClassPath);
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

template <typename TObjectType>
FString GetPcgSoftObjectAssetPath(const TSoftObjectPtr<TObjectType>& Object)
{
    return Object.ToSoftObjectPath().GetAssetPathString();
}

template <typename TObjectType>
FString GetPcgClassPath(const TSubclassOf<TObjectType>& ObjectClass)
{
    const TObjectPtr<UClass>& RawClass =
        const_cast<TSubclassOf<TObjectType>&>(ObjectClass).GetGCPtr();
    return RawClass ? RawClass->GetPathName() : TEXT("");
}

TArray<TSharedPtr<FJsonValue>> MakePcgStringValueArray(const TArray<FString>& Values)
{
    TArray<TSharedPtr<FJsonValue>> Result;
    Result.Reserve(Values.Num());
    for (const FString& Value : Values)
    {
        Result.Add(MakeShared<FJsonValueString>(Value));
    }
    return Result;
}

TArray<TSharedPtr<FJsonValue>> MakePcgNameValueArray(const TArray<FName>& Values)
{
    TArray<TSharedPtr<FJsonValue>> Result;
    Result.Reserve(Values.Num());
    for (const FName& Value : Values)
    {
        Result.Add(MakeShared<FJsonValueString>(Value.ToString()));
    }
    return Result;
}

void SetPcgNameArrayField(const TSharedPtr<FJsonObject>& Object, const TCHAR* FieldName, const TArray<FName>& Values)
{
    if (Object.IsValid())
    {
        Object->SetArrayField(FieldName, MakePcgNameValueArray(Values));
    }
}

template <typename TObjectType>
void SetPcgSoftObjectArrayField(
    const TSharedPtr<FJsonObject>& Object,
    const TCHAR* FieldName,
    const TArray<TSoftObjectPtr<TObjectType>>& Values)
{
    if (!Object.IsValid())
    {
        return;
    }

    TArray<FString> Paths;
    Paths.Reserve(Values.Num());
    for (const TSoftObjectPtr<TObjectType>& Value : Values)
    {
        Paths.Add(GetPcgSoftObjectAssetPath(Value));
    }
    Object->SetArrayField(FieldName, MakePcgStringValueArray(Paths));
}

TSharedPtr<FJsonObject> MakePcgPropertyOverrideObject(const FPCGObjectPropertyOverrideDescription& Override)
{
    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetObjectField(TEXT("inputSource"), MakePcgSelectorObject(Override.InputSource));
    Result->SetStringField(TEXT("propertyTarget"), Override.PropertyTarget);
    return Result;
}

TSharedPtr<FJsonObject> MakePcgPropertyOverridesObject(const TArray<FPCGObjectPropertyOverrideDescription>& Overrides)
{
    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetNumberField(TEXT("count"), Overrides.Num());

    TArray<TSharedPtr<FJsonValue>> Entries;
    Entries.Reserve(Overrides.Num());
    for (const FPCGObjectPropertyOverrideDescription& Override : Overrides)
    {
        Entries.Add(MakeShared<FJsonValueObject>(MakePcgPropertyOverrideObject(Override)));
    }
    Result->SetArrayField(TEXT("entries"), Entries);
    return Result;
}

TSharedPtr<FJsonObject> MakePcgDataLayerReferenceSelectorObject(const FPCGDataLayerReferenceSelector& Selector)
{
    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetBoolField(TEXT("asInput"), Selector.bAsInput);
    Result->SetObjectField(TEXT("attribute"), MakePcgSelectorObject(Selector.Attribute));
    SetPcgSoftObjectArrayField(Result, TEXT("dataLayers"), Selector.DataLayers);
    return Result;
}

TSharedPtr<FJsonObject> MakePcgDataLayerSettingsObject(const FPCGDataLayerSettings& Settings)
{
    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetStringField(TEXT("sourceType"), GetPcgEnumName(Settings.DataLayerSourceType));
    Result->SetObjectField(TEXT("dataLayerReferenceAttribute"), MakePcgSelectorObject(Settings.DataLayerReferenceAttribute));
    Result->SetObjectField(TEXT("includedDataLayers"), MakePcgDataLayerReferenceSelectorObject(Settings.IncludedDataLayers));
    Result->SetObjectField(TEXT("excludedDataLayers"), MakePcgDataLayerReferenceSelectorObject(Settings.ExcludedDataLayers));
    Result->SetObjectField(TEXT("addDataLayers"), MakePcgDataLayerReferenceSelectorObject(Settings.AddDataLayers));
    return Result;
}

TSharedPtr<FJsonObject> MakePcgHlodSettingsObject(const FPCGHLODSettings& Settings)
{
    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetStringField(TEXT("sourceType"), GetPcgEnumName(Settings.HLODSourceType));
    Result->SetStringField(TEXT("hlodLayerPath"), Settings.HLODLayer ? Settings.HLODLayer->GetPathName() : TEXT(""));
    return Result;
}

TSharedPtr<FJsonObject> MakePcgGetActorPropertyComponentSelectorObject(const UPCGGetActorPropertySettings& Settings)
{
    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetBoolField(TEXT("enabled"), Settings.bSelectComponent);
    Result->SetStringField(
        TEXT("componentClassPath"),
        Settings.ComponentClass ? Settings.ComponentClass->GetPathName() : TEXT(""));
    Result->SetBoolField(TEXT("processAllComponents"), Settings.bProcessAllComponents);
    Result->SetBoolField(TEXT("outputComponentReference"), Settings.bOutputComponentReference);
    return Result;
}

TSharedPtr<FJsonObject> MakePcgApplyActorTargetObject(const UPCGApplyOnActorSettings& Settings)
{
    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetStringField(TEXT("kind"), TEXT("objectReferenceAttribute"));
    Result->SetObjectField(TEXT("objectReferenceAttribute"), MakePcgSelectorObject(Settings.ObjectReferenceAttribute));
    return Result;
}

TSharedPtr<FJsonObject> MakePcgApplyBehaviorObject(const UPCGApplyOnActorSettings& Settings)
{
    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    SetPcgNameArrayField(Result, TEXT("postProcessFunctionNames"), Settings.PostProcessFunctionNames);
    Result->SetBoolField(TEXT("silenceErrorOnEmptyObjectPath"), Settings.bSilenceErrorOnEmptyObjectPath);
    Result->SetBoolField(TEXT("synchronousLoad"), Settings.bSynchronousLoad);
#if WITH_EDITORONLY_DATA
    Result->SetBoolField(TEXT("propagateObjectChangeEvent"), Settings.bPropagateObjectChangeEvent);
#endif
    return Result;
}

TSharedPtr<FJsonObject> MakePcgSpawnActorTemplateIdentityObject(const UPCGSpawnActorSettings& Settings)
{
    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetStringField(TEXT("templateActorPath"), Settings.TemplateActor ? Settings.TemplateActor->GetPathName() : TEXT(""));
    Result->SetStringField(TEXT("templateActorClassPath"), GetPcgClassPath(Settings.GetTemplateActorClass()));
    Result->SetStringField(TEXT("rootActorPath"), GetPcgSoftObjectAssetPath(Settings.RootActor));
    Result->SetBoolField(TEXT("allowTemplateActorEditing"), Settings.GetAllowTemplateActorEditing());
    return Result;
}

TSharedPtr<FJsonObject> MakePcgSpawnActorBehaviorObject(const UPCGSpawnActorSettings& Settings)
{
    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetStringField(TEXT("option"), GetPcgEnumName(Settings.Option));
    Result->SetBoolField(TEXT("forceDisableActorParsing"), Settings.bForceDisableActorParsing);
    Result->SetStringField(TEXT("generationTrigger"), GetPcgEnumName(Settings.GenerationTrigger));
    Result->SetStringField(TEXT("attachOptions"), GetPcgEnumName(Settings.AttachOptions));
    Result->SetBoolField(TEXT("spawnByAttribute"), Settings.bSpawnByAttribute);
    Result->SetStringField(TEXT("spawnAttribute"), Settings.SpawnAttribute.ToString());
    Result->SetBoolField(TEXT("inheritActorTags"), Settings.bInheritActorTags);
    SetPcgNameArrayField(Result, TEXT("tagsToAddOnActors"), Settings.TagsToAddOnActors);
    SetPcgNameArrayField(Result, TEXT("postSpawnFunctionNames"), Settings.PostSpawnFunctionNames);
    Result->SetBoolField(TEXT("warnOnIdenticalSpawn"), Settings.bWarnOnIdenticalSpawn);
    Result->SetBoolField(TEXT("deleteActorsBeforeGeneration"), Settings.bDeleteActorsBeforeGeneration);
    return Result;
}

TSharedPtr<FJsonObject> MakePcgSpawnSplineTemplateIdentityObject(const UPCGSpawnSplineSettings& Settings)
{
    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetStringField(TEXT("splineComponentClassPath"), GetPcgClassPath(Settings.SplineComponent));
    Result->SetStringField(TEXT("targetActorPath"), GetPcgSoftObjectAssetPath(Settings.TargetActor));
    return Result;
}

TSharedPtr<FJsonObject> MakePcgSpawnSplineBehaviorObject(const UPCGSpawnSplineSettings& Settings)
{
    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetBoolField(TEXT("spawnComponentFromAttribute"), Settings.bSpawnComponentFromAttribute);
    Result->SetObjectField(TEXT("spawnComponentFromAttributeName"), MakePcgSelectorObject(Settings.SpawnComponentFromAttributeName));
    Result->SetBoolField(TEXT("outputSplineComponentReference"), Settings.bOutputSplineComponentReference);
    Result->SetStringField(TEXT("componentReferenceAttributeName"), Settings.ComponentReferenceAttributeName.ToString());
    SetPcgNameArrayField(Result, TEXT("postProcessFunctionNames"), Settings.PostProcessFunctionNames);
    return Result;
}

TSharedPtr<FJsonObject> MakePcgSpawnSplineMeshTemplateIdentityObject(const UPCGSpawnSplineMeshSettings& Settings)
{
    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetStringField(TEXT("targetActorPath"), GetPcgSoftObjectAssetPath(Settings.TargetActor));
    Result->SetStringField(
        TEXT("componentClassPath"),
        GetPcgClassPath(Settings.SplineMeshDescriptor.ComponentClass));
    return Result;
}

TSharedPtr<FJsonObject> MakePcgSpawnSplineMeshBehaviorObject(const UPCGSpawnSplineMeshSettings& Settings)
{
    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    SetPcgNameArrayField(Result, TEXT("postProcessFunctionNames"), Settings.PostProcessFunctionNames);
    Result->SetBoolField(TEXT("synchronousLoad"), Settings.bSynchronousLoad);
    Result->SetStringField(TEXT("forwardAxis"), GetPcgEnumName(Settings.SplineMeshParams.ForwardAxis));
    Result->SetBoolField(TEXT("scaleMeshToBounds"), Settings.SplineMeshParams.bScaleMeshToBounds);
    Result->SetBoolField(
        TEXT("scaleMeshToLandscapeSplineFullWidth"),
        Settings.SplineMeshParams.bScaleMeshToLandscapeSplineFullWidth);
    Result->SetBoolField(TEXT("smoothInterpRollScale"), Settings.SplineMeshParams.bSmoothInterpRollScale);
    return Result;
}

TSharedPtr<FJsonObject> MakePcgSpawnSplineMeshSelectorObject(const UPCGSpawnSplineMeshSettings& Settings)
{
    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetStringField(TEXT("kind"), TEXT("splineMeshDescriptor"));
    Result->SetStringField(TEXT("staticMeshPath"), GetPcgSoftObjectAssetPath(Settings.SplineMeshDescriptor.StaticMesh));
    Result->SetStringField(
        TEXT("componentClassPath"),
        GetPcgClassPath(Settings.SplineMeshDescriptor.ComponentClass));
    Result->SetNumberField(TEXT("descriptorOverrideCount"), Settings.SplineMeshOverrideDescriptions.Num());
    Result->SetNumberField(TEXT("paramsOverrideCount"), Settings.SplineMeshParamsOverride.Num());
    Result->SetNumberField(TEXT("componentOverrideCount"), Settings.SplineMeshComponentOverride.Num());
    return Result;
}

TSharedPtr<FJsonObject> MakePcgSkinnedMeshTemplateIdentityObject(const UPCGSkinnedMeshSpawnerSettings& Settings)
{
    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetStringField(TEXT("targetActorPath"), GetPcgSoftObjectAssetPath(Settings.TargetActor));
    Result->SetStringField(
        TEXT("instanceDataPackerTypeClassPath"),
        GetPcgClassPath(Settings.InstanceDataPackerType));
    Result->SetStringField(
        TEXT("instanceDataPackerParametersClassPath"),
        Settings.InstanceDataPackerParameters ? Settings.InstanceDataPackerParameters->GetClass()->GetPathName() : TEXT(""));
    return Result;
}

TSharedPtr<FJsonObject> MakePcgSkinnedMeshBehaviorObject(const UPCGSkinnedMeshSpawnerSettings& Settings)
{
    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetBoolField(TEXT("applyMeshBoundsToPoints"), Settings.bApplyMeshBoundsToPoints);
    SetPcgNameArrayField(Result, TEXT("postProcessFunctionNames"), Settings.PostProcessFunctionNames);
    Result->SetBoolField(TEXT("synchronousLoad"), Settings.bSynchronousLoad);
    Result->SetBoolField(TEXT("silenceOverrideAttributeNotFoundErrors"), Settings.bSilenceOverrideAttributeNotFoundErrors);
    Result->SetBoolField(TEXT("warnOnIdenticalSpawn"), Settings.bWarnOnIdenticalSpawn);
    return Result;
}

TSharedPtr<FJsonObject> MakePcgSkinnedMeshSelectorObject(const UPCGSkinnedMeshSpawnerSettings& Settings)
{
    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetStringField(TEXT("kind"), TEXT("skinnedByAttribute"));

    const UPCGSkinnedMeshSelector* MeshSelector = Settings.MeshSelectorParameters;
    Result->SetStringField(
        TEXT("selectorClassPath"),
        MeshSelector ? MeshSelector->GetClass()->GetPathName() : TEXT(""));

    if (MeshSelector != nullptr)
    {
        Result->SetObjectField(TEXT("meshAttribute"), MakePcgSelectorObject(MeshSelector->MeshAttribute));
        Result->SetStringField(
            TEXT("componentClassPath"),
            GetPcgClassPath(MeshSelector->TemplateDescriptor.ComponentClass));
        SetPcgNameArrayField(Result, TEXT("componentTags"), MeshSelector->TemplateDescriptor.ComponentTags);
        Result->SetBoolField(TEXT("useAttributeMaterialOverrides"), MeshSelector->bUseAttributeMaterialOverrides);
        SetPcgNameArrayField(Result, TEXT("materialOverrideAttributes"), MeshSelector->MaterialOverrideAttributes);
    }

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
        Settings->SetObjectField(
            TEXT("componentSelector"),
            MakePcgGetActorPropertyComponentSelectorObject(*GetActorPropertySettings));
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

    if (const UPCGApplyOnActorSettings* ApplyOnActorSettings = Cast<UPCGApplyOnActorSettings>(NodeSettings))
    {
        TSharedPtr<FJsonObject> Settings = MakeShared<FJsonObject>();
        Settings->SetStringField(TEXT("settingsClassPath"), ApplyOnActorSettings->GetClass()->GetPathName());
        Settings->SetObjectField(TEXT("actorSelector"), MakePcgApplyActorTargetObject(*ApplyOnActorSettings));
        Settings->SetObjectField(
            TEXT("propertyOverrides"),
            MakePcgPropertyOverridesObject(ApplyOnActorSettings->PropertyOverrideDescriptions));
        Settings->SetObjectField(TEXT("applyBehavior"), MakePcgApplyBehaviorObject(*ApplyOnActorSettings));
        Settings->SetObjectField(
            TEXT("objectReferenceAttribute"),
            MakePcgSelectorObject(ApplyOnActorSettings->ObjectReferenceAttribute));
        SetPcgNameArrayField(Settings, TEXT("postProcessFunctionNames"), ApplyOnActorSettings->PostProcessFunctionNames);
        Settings->SetBoolField(TEXT("silenceErrorOnEmptyObjectPath"), ApplyOnActorSettings->bSilenceErrorOnEmptyObjectPath);
        Settings->SetBoolField(TEXT("synchronousLoad"), ApplyOnActorSettings->bSynchronousLoad);
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

    if (const UPCGDataFromActorSettings* DataFromActorSettings = Cast<UPCGDataFromActorSettings>(NodeSettings))
    {
        TSharedPtr<FJsonObject> Settings = MakeShared<FJsonObject>();
        Settings->SetStringField(TEXT("settingsClassPath"), DataFromActorSettings->GetClass()->GetPathName());
        Settings->SetObjectField(TEXT("actorSelector"), MakePcgActorSelectorObject(DataFromActorSettings->ActorSelector));
        Settings->SetObjectField(TEXT("componentSelector"), MakePcgComponentSelectorObject(DataFromActorSettings->ComponentSelector));
        Settings->SetStringField(TEXT("mode"), GetPcgEnumName(DataFromActorSettings->Mode));
        Settings->SetBoolField(TEXT("ignorePCGGeneratedComponents"), DataFromActorSettings->bIgnorePCGGeneratedComponents);
        Settings->SetBoolField(TEXT("alsoOutputSinglePointData"), DataFromActorSettings->bAlsoOutputSinglePointData);
        Settings->SetBoolField(TEXT("componentsMustOverlapSelf"), DataFromActorSettings->bComponentsMustOverlapSelf);
        Settings->SetBoolField(TEXT("getDataOnAllGrids"), DataFromActorSettings->bGetDataOnAllGrids);
        Settings->SetNumberField(TEXT("allowedGrids"), DataFromActorSettings->AllowedGrids);
        Settings->SetBoolField(TEXT("mergeSinglePointData"), DataFromActorSettings->bMergeSinglePointData);
        SetPcgNameArrayField(Settings, TEXT("expectedPins"), DataFromActorSettings->ExpectedPins);
        Settings->SetStringField(TEXT("propertyName"), DataFromActorSettings->PropertyName.ToString());
        Settings->SetBoolField(TEXT("alwaysRequeryActors"), DataFromActorSettings->bAlwaysRequeryActors);
        Settings->SetBoolField(
            TEXT("silenceSanitizedAttributeNameWarnings"),
            DataFromActorSettings->bSilenceSanitizedAttributeNameWarnings);
        Settings->SetBoolField(
            TEXT("silenceReservedAttributeNameWarnings"),
            DataFromActorSettings->bSilenceReservedAttributeNameWarnings);

        AppendPcgGraphQueryDiagnostic(
            NodeDiagnostics,
            RootDiagnostics,
            NodeId,
            TEXT("PCG_SELECTOR_EMPTY_INPUT_HINT"),
            TEXT("If no actors match actorSelector, this node yields no actor data."));

        return Settings;
    }

    if (const UPCGSpawnActorSettings* SpawnActorSettings = Cast<UPCGSpawnActorSettings>(NodeSettings))
    {
        TSharedPtr<FJsonObject> Settings = MakeShared<FJsonObject>();
        Settings->SetStringField(TEXT("settingsClassPath"), SpawnActorSettings->GetClass()->GetPathName());
        Settings->SetObjectField(TEXT("templateIdentity"), MakePcgSpawnActorTemplateIdentityObject(*SpawnActorSettings));
        Settings->SetObjectField(TEXT("spawnBehavior"), MakePcgSpawnActorBehaviorObject(*SpawnActorSettings));
        Settings->SetObjectField(
            TEXT("propertyOverrides"),
            MakePcgPropertyOverridesObject(SpawnActorSettings->SpawnedActorPropertyOverrideDescriptions));
        Settings->SetObjectField(TEXT("dataLayerSettings"), MakePcgDataLayerSettingsObject(SpawnActorSettings->DataLayerSettings));
        Settings->SetObjectField(TEXT("hlodSettings"), MakePcgHlodSettingsObject(SpawnActorSettings->HLODSettings));
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

    if (const UPCGSpawnSplineSettings* SpawnSplineSettings = Cast<UPCGSpawnSplineSettings>(NodeSettings))
    {
        TSharedPtr<FJsonObject> Settings = MakeShared<FJsonObject>();
        Settings->SetStringField(TEXT("settingsClassPath"), SpawnSplineSettings->GetClass()->GetPathName());
        Settings->SetObjectField(TEXT("templateIdentity"), MakePcgSpawnSplineTemplateIdentityObject(*SpawnSplineSettings));
        Settings->SetObjectField(TEXT("spawnBehavior"), MakePcgSpawnSplineBehaviorObject(*SpawnSplineSettings));
        Settings->SetObjectField(
            TEXT("propertyOverrides"),
            MakePcgPropertyOverridesObject(SpawnSplineSettings->PropertyOverrideDescriptions));
        return Settings;
    }

    if (const UPCGSpawnSplineMeshSettings* SpawnSplineMeshSettings = Cast<UPCGSpawnSplineMeshSettings>(NodeSettings))
    {
        TSharedPtr<FJsonObject> Settings = MakeShared<FJsonObject>();
        Settings->SetStringField(TEXT("settingsClassPath"), SpawnSplineMeshSettings->GetClass()->GetPathName());
        Settings->SetObjectField(TEXT("templateIdentity"), MakePcgSpawnSplineMeshTemplateIdentityObject(*SpawnSplineMeshSettings));
        Settings->SetObjectField(TEXT("spawnBehavior"), MakePcgSpawnSplineMeshBehaviorObject(*SpawnSplineMeshSettings));
        Settings->SetObjectField(TEXT("meshSelector"), MakePcgSpawnSplineMeshSelectorObject(*SpawnSplineMeshSettings));
        Settings->SetObjectField(
            TEXT("propertyOverrides"),
            MakePcgPropertyOverridesObject(SpawnSplineMeshSettings->SplineMeshOverrideDescriptions));
        return Settings;
    }

    if (const UPCGSkinnedMeshSpawnerSettings* SkinnedMeshSpawnerSettings = Cast<UPCGSkinnedMeshSpawnerSettings>(NodeSettings))
    {
        TSharedPtr<FJsonObject> Settings = MakeShared<FJsonObject>();
        Settings->SetStringField(TEXT("settingsClassPath"), SkinnedMeshSpawnerSettings->GetClass()->GetPathName());
        Settings->SetObjectField(TEXT("templateIdentity"), MakePcgSkinnedMeshTemplateIdentityObject(*SkinnedMeshSpawnerSettings));
        Settings->SetObjectField(TEXT("spawnBehavior"), MakePcgSkinnedMeshBehaviorObject(*SkinnedMeshSpawnerSettings));
        Settings->SetObjectField(TEXT("meshSelector"), MakePcgSkinnedMeshSelectorObject(*SkinnedMeshSpawnerSettings));
        Settings->SetObjectField(
            TEXT("propertyOverrides"),
            MakePcgPropertyOverridesObject(SkinnedMeshSpawnerSettings->SkinnedMeshComponentPropertyOverrides));
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
