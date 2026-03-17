// Graph runtime handlers for Loomle Bridge.
namespace
{
struct FGraphSemanticOpSpec
{
    const TCHAR* OpId;
    const TCHAR* Scope;
    const TCHAR* Summary;
    const TCHAR* NodeClassPath;
    bool bRequiresPinContext = false;
    const TCHAR* Coverage = TEXT("curated");
    const TCHAR* Determinism = TEXT("stable");
};

static const TCHAR* BlueprintStandardMacrosAssetPath = TEXT("/Engine/EditorBlueprintResources/StandardMacros");

void AppendGraphSemanticOpSpecs(const FString& GraphType, TArray<FGraphSemanticOpSpec>& OutSpecs)
{
    if (GraphType.Equals(TEXT("blueprint")))
    {
        OutSpecs.Add({ TEXT("core.comment"), TEXT("cross-graph"), TEXT("Add a comment node or comment region."), TEXT("/Script/UnrealEd.EdGraphNode_Comment"), false, TEXT("curated"), TEXT("stable") });
        OutSpecs.Add({ TEXT("core.reroute"), TEXT("cross-graph"), TEXT("Add a reroute node in a pin context."), TEXT("/Script/BlueprintGraph.K2Node_Knot"), true, TEXT("contextual"), TEXT("context_sensitive") });
        OutSpecs.Add({ TEXT("bp.flow.branch"), TEXT("blueprint"), TEXT("Add a branch flow-control node."), TEXT("/Script/BlueprintGraph.K2Node_IfThenElse"), false, TEXT("curated"), TEXT("stable") });
        OutSpecs.Add({ TEXT("bp.flow.sequence"), TEXT("blueprint"), TEXT("Add an execution sequence node."), TEXT("/Script/BlueprintGraph.K2Node_ExecutionSequence"), false, TEXT("curated"), TEXT("stable") });
        OutSpecs.Add({ TEXT("bp.flow.delay"), TEXT("blueprint"), TEXT("Add a latent delay node."), TEXT("/Script/BlueprintGraph.K2Node_CallFunction"), false, TEXT("curated"), TEXT("stable") });
        OutSpecs.Add({ TEXT("bp.flow.do_once"), TEXT("blueprint"), TEXT("Add a DoOnce macro node."), TEXT("/Script/BlueprintGraph.K2Node_MacroInstance"), false, TEXT("curated"), TEXT("stable") });
        OutSpecs.Add({ TEXT("bp.debug.print_string"), TEXT("blueprint"), TEXT("Add a Print String debug node."), TEXT("/Script/BlueprintGraph.K2Node_CallFunction"), false, TEXT("curated"), TEXT("stable") });
        OutSpecs.Add({ TEXT("bp.var.get"), TEXT("blueprint"), TEXT("Add a variable getter node."), TEXT("/Script/BlueprintGraph.K2Node_VariableGet"), false, TEXT("contextual"), TEXT("context_sensitive") });
        OutSpecs.Add({ TEXT("bp.var.set"), TEXT("blueprint"), TEXT("Add a variable setter node."), TEXT("/Script/BlueprintGraph.K2Node_VariableSet"), false, TEXT("contextual"), TEXT("context_sensitive") });
    }
    else if (GraphType.Equals(TEXT("material")))
    {
        OutSpecs.Add({ TEXT("mat.constant.scalar"), TEXT("material"), TEXT("Add a scalar constant expression."), TEXT("/Script/Engine.MaterialExpressionConstant") });
        OutSpecs.Add({ TEXT("mat.constant.vector3"), TEXT("material"), TEXT("Add a Constant3Vector expression."), TEXT("/Script/Engine.MaterialExpressionConstant3Vector") });
        OutSpecs.Add({ TEXT("mat.math.add"), TEXT("material"), TEXT("Add an add expression."), TEXT("/Script/Engine.MaterialExpressionAdd") });
        OutSpecs.Add({ TEXT("mat.math.lerp"), TEXT("material"), TEXT("Add a linear interpolate expression."), TEXT("/Script/Engine.MaterialExpressionLinearInterpolate") });
        OutSpecs.Add({ TEXT("mat.math.multiply"), TEXT("material"), TEXT("Add a multiply expression."), TEXT("/Script/Engine.MaterialExpressionMultiply") });
        OutSpecs.Add({ TEXT("mat.math.one_minus"), TEXT("material"), TEXT("Add a one-minus expression."), TEXT("/Script/Engine.MaterialExpressionOneMinus") });
        OutSpecs.Add({ TEXT("mat.math.saturate"), TEXT("material"), TEXT("Add a saturate expression."), TEXT("/Script/Engine.MaterialExpressionSaturate") });
        OutSpecs.Add({ TEXT("mat.param.scalar"), TEXT("material"), TEXT("Add a scalar parameter expression."), TEXT("/Script/Engine.MaterialExpressionScalarParameter") });
        OutSpecs.Add({ TEXT("mat.param.texture"), TEXT("material"), TEXT("Add a texture parameter expression."), TEXT("/Script/Engine.MaterialExpressionTextureSampleParameter2D") });
        OutSpecs.Add({ TEXT("mat.param.vector"), TEXT("material"), TEXT("Add a vector parameter expression."), TEXT("/Script/Engine.MaterialExpressionVectorParameter") });
        OutSpecs.Add({ TEXT("mat.func.call"), TEXT("material"), TEXT("Add a material function call expression."), TEXT("/Script/Engine.MaterialExpressionMaterialFunctionCall") });
        OutSpecs.Add({ TEXT("mat.texture.sample"), TEXT("material"), TEXT("Add a texture sample expression."), TEXT("/Script/Engine.MaterialExpressionTextureSample") });
    }
    else if (GraphType.Equals(TEXT("pcg")))
    {
        OutSpecs.Add({ TEXT("pcg.create.points"), TEXT("pcg"), TEXT("Add a Create Points PCG node."), TEXT("/Script/PCG.PCGCreatePointsSettings") });
        OutSpecs.Add({ TEXT("pcg.meta.add_tag"), TEXT("pcg"), TEXT("Add an Add Tag PCG node."), TEXT("/Script/PCG.PCGAddTagSettings") });
        OutSpecs.Add({ TEXT("pcg.filter.by_tag"), TEXT("pcg"), TEXT("Add a Filter By Tag PCG node."), TEXT("/Script/PCG.PCGFilterByTagSettings") });
        OutSpecs.Add({ TEXT("pcg.sample.surface"), TEXT("pcg"), TEXT("Add a Surface Sampler PCG node."), TEXT("/Script/PCG.PCGSurfaceSamplerSettings") });
        OutSpecs.Add({ TEXT("pcg.transform.points"), TEXT("pcg"), TEXT("Add a Transform Points PCG node."), TEXT("/Script/PCG.PCGTransformPointsSettings") });
        OutSpecs.Add({ TEXT("pcg.sample.spline"), TEXT("pcg"), TEXT("Add a Spline Sampler PCG node."), TEXT("/Script/PCG.PCGSplineSamplerSettings") });
        OutSpecs.Add({ TEXT("pcg.source.actor_data"), TEXT("pcg"), TEXT("Add a Get Actor Data PCG node."), TEXT("/Script/PCG.PCGDataFromActorSettings") });
        OutSpecs.Add({ TEXT("pcg.project.surface"), TEXT("pcg"), TEXT("Add a Projection PCG node."), TEXT("/Script/PCG.PCGProjectionSettings") });
        OutSpecs.Add({ TEXT("pcg.spawn.static_mesh"), TEXT("pcg"), TEXT("Add a Static Mesh Spawner PCG node."), TEXT("/Script/PCG.PCGStaticMeshSpawnerSettings") });
        OutSpecs.Add({ TEXT("pcg.spawn.actor"), TEXT("pcg"), TEXT("Add a Spawn Actor PCG node."), TEXT("/Script/PCG.PCGSpawnActorSettings") });
    }
}

bool TryFindGraphSemanticOpSpec(const FString& GraphType, const FString& OpId, FGraphSemanticOpSpec& OutSpec)
{
    TArray<FGraphSemanticOpSpec> Specs;
    AppendGraphSemanticOpSpecs(GraphType, Specs);
    for (const FGraphSemanticOpSpec& Spec : Specs)
    {
        if (OpId.Equals(Spec.OpId))
        {
            OutSpec = Spec;
            return true;
        }
    }
    return false;
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

void AppendPcgGraphHealthDiagnostics(
    const UPCGGraph* Graph,
    const TSet<FString>& SpawnerNodeIds,
    const TArray<TSharedPtr<FJsonValue>>& Edges,
    TArray<TSharedPtr<FJsonValue>>& Diagnostics)
{
    if (Graph == nullptr)
    {
        return;
    }

    const UPCGNode* OutputNode = Graph->GetOutputNode();
    if (OutputNode == nullptr)
    {
        return;
    }

    const FString OutputNodeId = OutputNode->GetPathName();
    TMap<FString, TArray<FString>> ReverseAdjacency;
    int32 OutputIncomingEdgeCount = 0;

    for (const TSharedPtr<FJsonValue>& EdgeValue : Edges)
    {
        const TSharedPtr<FJsonObject>* EdgeObject = nullptr;
        if (!EdgeValue.IsValid() || !EdgeValue->TryGetObject(EdgeObject) || EdgeObject == nullptr || !(*EdgeObject).IsValid())
        {
            continue;
        }

        FString FromNodeId;
        FString ToNodeId;
        if (!(*EdgeObject)->TryGetStringField(TEXT("fromNodeId"), FromNodeId) ||
            !(*EdgeObject)->TryGetStringField(TEXT("toNodeId"), ToNodeId) ||
            FromNodeId.IsEmpty() ||
            ToNodeId.IsEmpty())
        {
            continue;
        }

        ReverseAdjacency.FindOrAdd(ToNodeId).Add(FromNodeId);
        if (ToNodeId.Equals(OutputNodeId))
        {
            ++OutputIncomingEdgeCount;
        }
    }

    TSet<FString> ReachableToOutput;
    TArray<FString> Pending;
    ReachableToOutput.Add(OutputNodeId);
    Pending.Add(OutputNodeId);

    while (Pending.Num() > 0)
    {
        const FString CurrentNodeId = Pending.Pop(EAllowShrinking::No);
        const TArray<FString>* UpstreamNodes = ReverseAdjacency.Find(CurrentNodeId);
        if (UpstreamNodes == nullptr)
        {
            continue;
        }

        for (const FString& UpstreamNodeId : *UpstreamNodes)
        {
            if (!UpstreamNodeId.IsEmpty() && !ReachableToOutput.Contains(UpstreamNodeId))
            {
                ReachableToOutput.Add(UpstreamNodeId);
                Pending.Add(UpstreamNodeId);
            }
        }
    }

    const bool bOutputNodeMissingInputs = OutputIncomingEdgeCount == 0;
    if (bOutputNodeMissingInputs)
    {
        AppendPcgGraphRootDiagnostic(
            Diagnostics,
            TEXT("PCG_OUTPUT_NODE_MISSING_INPUTS"),
            TEXT("The PCG output node has no incoming edges, so no data can reach the graph output."),
            TEXT("error"));
        AppendPcgGraphRootDiagnostic(
            Diagnostics,
            TEXT("PCG_NO_TERMINAL_OUTPUT_PATH"),
            TEXT("No terminal output path reaches the PCG output node from any authored node."),
            TEXT("error"));
        AppendPcgGraphRootDiagnostic(
            Diagnostics,
            TEXT("PCG_GRAPH_CAN_GENERATE_NO_OUTPUT"),
            TEXT("This PCG graph currently has no authored path into the output node and can generate no output."),
            TEXT("warning"));
    }

    TArray<FString> DisconnectedSpawnerNodeIds;
    for (const FString& SpawnerNodeId : SpawnerNodeIds)
    {
        if (!ReachableToOutput.Contains(SpawnerNodeId))
        {
            DisconnectedSpawnerNodeIds.Add(SpawnerNodeId);
        }
    }
    if (DisconnectedSpawnerNodeIds.Num() > 0)
    {
        DisconnectedSpawnerNodeIds.Sort();
        AppendPcgGraphRootDiagnostic(
            Diagnostics,
            TEXT("PCG_SPAWNER_NOT_CONNECTED_TO_OUTPUT"),
            FString::Printf(
                TEXT("%d PCG spawner node(s) are not connected to any path that reaches the output node."),
                DisconnectedSpawnerNodeIds.Num()),
            TEXT("warning"),
            &DisconnectedSpawnerNodeIds);
    }
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
    if (OpId.Equals(TEXT("pcg.meta.add_tag")) || OpId.Equals(TEXT("pcg.filter.by_tag")))
    {
        return TEXT("In");
    }

    return TEXT("");
}

const TCHAR* GetDefaultPcgOutputPinName(const FString& OpId)
{
    if (OpId.Equals(TEXT("pcg.filter.by_tag")))
    {
        return TEXT("InsideFilter");
    }
    if (OpId.Equals(TEXT("pcg.meta.add_tag")))
    {
        return TEXT("Out");
    }

    return TEXT("");
}

bool IsComposablePcgSemanticOp(const FString& OpId)
{
    return OpId.Equals(TEXT("pcg.meta.add_tag")) || OpId.Equals(TEXT("pcg.filter.by_tag"));
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

    // Helper: build an asset-kind GraphRef JSON object.
    auto MakeAssetGraphRef = [](const FString& RefAssetPath, const FString& RefGraphName) -> TSharedPtr<FJsonObject>
    {
        TSharedPtr<FJsonObject> Ref = MakeShared<FJsonObject>();
        Ref->SetStringField(TEXT("kind"), TEXT("asset"));
        Ref->SetStringField(TEXT("assetPath"), RefAssetPath);
        if (!RefGraphName.IsEmpty())
        {
            Ref->SetStringField(TEXT("graphName"), RefGraphName);
        }
        return Ref;
    };

    // Helper: build an inline-kind GraphRef JSON object.
    auto MakeInlineGraphRef = [](const FString& RefNodeGuid, const FString& RefAssetPath) -> TSharedPtr<FJsonObject>
    {
        TSharedPtr<FJsonObject> Ref = MakeShared<FJsonObject>();
        Ref->SetStringField(TEXT("kind"), TEXT("inline"));
        Ref->SetStringField(TEXT("nodeGuid"), RefNodeGuid);
        Ref->SetStringField(TEXT("assetPath"), RefAssetPath);
        return Ref;
    };

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
        TSharedPtr<FJsonObject> GraphInfo = MakeShared<FJsonObject>();
        GraphInfo->SetStringField(TEXT("graphName"), TEXT("MaterialGraph"));
        GraphInfo->SetStringField(TEXT("graphKind"), TEXT("root"));
        GraphInfo->SetStringField(TEXT("graphClassPath"), TEXT("/Script/Engine.MaterialGraph"));
        GraphInfo->SetObjectField(TEXT("graphRef"), MakeAssetGraphRef(AssetPath, TEXT("")));
        GraphInfo->SetField(TEXT("parentGraphRef"), MakeShared<FJsonValueNull>());
        GraphInfo->SetField(TEXT("ownerNodeId"), MakeShared<FJsonValueNull>());
        GraphInfo->SetStringField(TEXT("loadStatus"), TEXT("loaded"));
        Graphs.Add(MakeShared<FJsonValueObject>(GraphInfo));

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
                TSharedPtr<FJsonObject> ParentRef = MakeAssetGraphRef(AssetPath, TEXT(""));
                for (UMaterialExpression* Expression : Expressions)
                {
                    UMaterialExpressionMaterialFunctionCall* FuncCall = Cast<UMaterialExpressionMaterialFunctionCall>(Expression);
                    if (!FuncCall || !FuncCall->MaterialFunction) { continue; }

                    UPackage* FuncPkg = FuncCall->MaterialFunction->GetPackage();
                    FString FuncAssetPath = FuncPkg ? FuncPkg->GetPathName() : FuncCall->MaterialFunction->GetPathName();
                    if (!FuncPkg)
                    {
                        int32 DotIdx;
                        if (FuncAssetPath.FindLastChar(TEXT('.'), DotIdx)) { FuncAssetPath = FuncAssetPath.Left(DotIdx); }
                    }

                    TSharedPtr<FJsonObject> SubEntry = MakeShared<FJsonObject>();
                    SubEntry->SetStringField(TEXT("graphName"), FuncCall->MaterialFunction->GetName());
                    SubEntry->SetStringField(TEXT("graphKind"), TEXT("subgraph"));
                    SubEntry->SetStringField(TEXT("graphClassPath"), FuncCall->MaterialFunction->GetClass() ? FuncCall->MaterialFunction->GetClass()->GetPathName() : TEXT(""));
                    SubEntry->SetObjectField(TEXT("graphRef"), MakeAssetGraphRef(FuncAssetPath, TEXT("")));
                    SubEntry->SetObjectField(TEXT("parentGraphRef"), ParentRef);
                    SubEntry->SetStringField(TEXT("ownerNodeId"), MaterialExpressionId(Expression));
                    SubEntry->SetStringField(TEXT("loadStatus"), TEXT("loaded"));
                    Graphs.Add(MakeShared<FJsonValueObject>(SubEntry));
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
        TSharedPtr<FJsonObject> GraphInfo = MakeShared<FJsonObject>();
        GraphInfo->SetStringField(TEXT("graphName"), TEXT("PCGGraph"));
        GraphInfo->SetStringField(TEXT("graphKind"), TEXT("root"));
        GraphInfo->SetStringField(TEXT("graphClassPath"), Asset->GetClass() ? Asset->GetClass()->GetPathName() : TEXT(""));
        GraphInfo->SetObjectField(TEXT("graphRef"), MakeAssetGraphRef(AssetPath, TEXT("")));
        GraphInfo->SetField(TEXT("parentGraphRef"), MakeShared<FJsonValueNull>());
        GraphInfo->SetField(TEXT("ownerNodeId"), MakeShared<FJsonValueNull>());
        GraphInfo->SetStringField(TEXT("loadStatus"), TEXT("loaded"));
        Graphs.Add(MakeShared<FJsonValueObject>(GraphInfo));

        if (bIncludeSubgraphs && MaxDepth > 0)
        {
            UPCGGraph* PcgGraph = LoadPcgGraphByAssetPath(AssetPath);
            if (PcgGraph)
            {
                TSharedPtr<FJsonObject> ParentRef = MakeAssetGraphRef(AssetPath, TEXT(""));
                for (UPCGNode* NodeObj : PcgGraph->GetNodes())
                {
                    if (NodeObj == nullptr) { continue; }
                    UPCGSettings* NodeSettings = NodeObj->GetSettings();
                    if (!NodeSettings || !NodeSettings->GetClass()) { continue; }
                    const FString NodeClassPath = NodeSettings->GetClass()->GetPathName();
                    if (!NodeClassPath.Contains(TEXT("Subgraph"))) { continue; }

                    // Resolve the referenced PCG subgraph: try hard ref first, then soft ref.
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
                            if (!SoftProp->PropertyClass || !SoftProp->PropertyClass->GetPathName().Contains(TEXT("PCGGraph"))) { continue; }
                            const FSoftObjectPtr SoftPtr = SoftProp->GetPropertyValue_InContainer(NodeSettings);
                            const FSoftObjectPath SoftPath = SoftPtr.ToSoftObjectPath();
                            if (SoftPath.IsNull()) { continue; }
                            UObject* Resolved = SoftPath.ResolveObject();
                            if (Resolved) { SubgraphAsset = Resolved; }
                            else
                            {
                                SubgraphSoftPath = SoftPath.GetAssetPathString();
                                int32 DotIdx;
                                if (SubgraphSoftPath.FindLastChar(TEXT('.'), DotIdx)) { SubgraphSoftPath = SubgraphSoftPath.Left(DotIdx); }
                            }
                            break;
                        }
                    }
                    if (!SubgraphAsset && SubgraphSoftPath.IsEmpty()) { continue; }

                    FString SubAssetPath;
                    FString SubLoadStatus;
                    if (SubgraphAsset)
                    {
                        UPackage* SubPkg = SubgraphAsset->GetPackage();
                        SubAssetPath = SubPkg ? SubPkg->GetPathName() : SubgraphAsset->GetPathName();
                        if (!SubPkg) { int32 DotIdx; if (SubAssetPath.FindLastChar(TEXT('.'), DotIdx)) { SubAssetPath = SubAssetPath.Left(DotIdx); } }
                        SubLoadStatus = TEXT("loaded");
                    }
                    else
                    {
                        SubAssetPath = SubgraphSoftPath;
                        SubLoadStatus = TEXT("not_found");
                    }

                    TSharedPtr<FJsonObject> SubEntry = MakeShared<FJsonObject>();
                    SubEntry->SetStringField(TEXT("graphName"), SubgraphAsset ? SubgraphAsset->GetName() : FPaths::GetBaseFilename(SubAssetPath));
                    SubEntry->SetStringField(TEXT("graphKind"), TEXT("subgraph"));
                    SubEntry->SetStringField(TEXT("graphClassPath"), (SubgraphAsset && SubgraphAsset->GetClass()) ? SubgraphAsset->GetClass()->GetPathName() : TEXT(""));
                    SubEntry->SetObjectField(TEXT("graphRef"), MakeAssetGraphRef(SubAssetPath, TEXT("")));
                    SubEntry->SetObjectField(TEXT("parentGraphRef"), ParentRef);
                    SubEntry->SetStringField(TEXT("ownerNodeId"), NodeObj->GetPathName());
                    SubEntry->SetStringField(TEXT("loadStatus"), SubLoadStatus);
                    Graphs.Add(MakeShared<FJsonValueObject>(SubEntry));
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
        if (GraphKind.Equals(TEXT("Event"), ESearchCase::IgnoreCase))
        {
            GraphKind = TEXT("root");
        }
        else if (GraphKind.Equals(TEXT("Function"), ESearchCase::IgnoreCase))
        {
            GraphKind = TEXT("function");
        }
        else if (GraphKind.Equals(TEXT("Macro"), ESearchCase::IgnoreCase))
        {
            GraphKind = TEXT("macro");
        }
        else if (GraphKind.Equals(TEXT("Interface"), ESearchCase::IgnoreCase))
        {
            GraphKind = TEXT("function");
        }
        (*GraphObj)->SetStringField(TEXT("graphKind"), GraphKind);

        (*GraphObj)->SetObjectField(TEXT("graphRef"), MakeAssetGraphRef(AssetPath, GraphName));
        (*GraphObj)->SetField(TEXT("parentGraphRef"), MakeShared<FJsonValueNull>());
        (*GraphObj)->SetField(TEXT("ownerNodeId"), MakeShared<FJsonValueNull>());
        (*GraphObj)->SetStringField(TEXT("loadStatus"), TEXT("loaded"));
    }

    // When includeSubgraphs is requested, enumerate K2Node_Composite nodes in each root graph.
    if (bIncludeSubgraphs && MaxDepth > 0)
    {
        UBlueprint* Blueprint = LoadBlueprintByAssetPath(AssetPath);
        if (Blueprint)
        {
            // Collect all root graphs to scan for composite nodes.
            TArray<UEdGraph*> RootGraphs;
            RootGraphs.Append(Blueprint->UbergraphPages);
            RootGraphs.Append(Blueprint->FunctionGraphs);
            RootGraphs.Append(Blueprint->MacroGraphs);

            for (UEdGraph* RootGraph : RootGraphs)
            {
                if (!RootGraph)
                {
                    continue;
                }

                const FString ParentGraphName = RootGraph->GetName();
                TSharedPtr<FJsonObject> ParentRef = MakeAssetGraphRef(AssetPath, ParentGraphName);

                for (UEdGraphNode* Node : RootGraph->Nodes)
                {
                    if (!Node)
                    {
                        continue;
                    }

                    FObjectPropertyBase* BoundGraphProp = FindFProperty<FObjectPropertyBase>(Node->GetClass(), TEXT("BoundGraph"));
                    if (!BoundGraphProp)
                    {
                        continue;
                    }

                    UEdGraph* SubGraph = Cast<UEdGraph>(BoundGraphProp->GetObjectPropertyValue_InContainer(Node));
                    if (!SubGraph)
                    {
                        continue;
                    }

                    const FString NodeGuidText = Node->NodeGuid.ToString(EGuidFormats::DigitsWithHyphensLower);
                    const FString SubgraphName = SubGraph->GetName();
                    const FString SubgraphClassPath = SubGraph->GetClass() ? SubGraph->GetClass()->GetPathName() : TEXT("");

                    TSharedPtr<FJsonObject> SubEntry = MakeShared<FJsonObject>();
                    SubEntry->SetStringField(TEXT("graphName"), SubgraphName);
                    SubEntry->SetStringField(TEXT("graphKind"), TEXT("subgraph"));
                    SubEntry->SetStringField(TEXT("graphClassPath"), SubgraphClassPath);
                    SubEntry->SetObjectField(TEXT("graphRef"), MakeInlineGraphRef(NodeGuidText, AssetPath));
                    SubEntry->SetObjectField(TEXT("parentGraphRef"), ParentRef);
                    SubEntry->SetStringField(TEXT("ownerNodeId"), NodeGuidText);
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
    FString AssetPath;
    FString GraphName;
    bool bUsedGraphRef = false;
    FString InlineNodeGuid; // set when graphRef.kind == "inline"
    const FString DefaultSingleGraphName = GraphType.Equals(TEXT("pcg"))
        ? TEXT("PCGGraph")
        : (GraphType.Equals(TEXT("material")) ? TEXT("MaterialGraph") : TEXT(""));

    const TSharedPtr<FJsonObject>* GraphRefObj = nullptr;
    const bool bHasGraphRef = Arguments->TryGetObjectField(TEXT("graphRef"), GraphRefObj) && GraphRefObj && (*GraphRefObj).IsValid();
    const bool bHasGraphName = Arguments->TryGetStringField(TEXT("graphName"), GraphName) && !GraphName.IsEmpty();

    if (bHasGraphRef && bHasGraphName)
    {
        Result->SetBoolField(TEXT("isError"), true);
        Result->SetStringField(TEXT("code"), TEXT("INVALID_ARGUMENT"));
        Result->SetStringField(TEXT("message"), TEXT("Supply either graphRef (Mode B) or graphName (Mode A), not both."));
        return Result;
    }

    if (bHasGraphRef)
    {
        // Mode B: resolve via GraphRef.
        bUsedGraphRef = true;
        FString Kind;
        if (!(*GraphRefObj)->TryGetStringField(TEXT("kind"), Kind) || Kind.IsEmpty())
        {
            Result->SetBoolField(TEXT("isError"), true);
            Result->SetStringField(TEXT("code"), TEXT("GRAPH_REF_INVALID"));
            Result->SetStringField(TEXT("message"), TEXT("graphRef.kind is required."));
            return Result;
        }

        if (Kind.Equals(TEXT("inline")))
        {
            if (!(*GraphRefObj)->TryGetStringField(TEXT("nodeGuid"), InlineNodeGuid) || InlineNodeGuid.IsEmpty())
            {
                Result->SetBoolField(TEXT("isError"), true);
                Result->SetStringField(TEXT("code"), TEXT("GRAPH_REF_INVALID"));
                Result->SetStringField(TEXT("message"), TEXT("graphRef.nodeGuid is required for kind=inline."));
                return Result;
            }

            FString RefAssetPath;
            if (!(*GraphRefObj)->TryGetStringField(TEXT("assetPath"), RefAssetPath) || RefAssetPath.IsEmpty())
            {
                Result->SetBoolField(TEXT("isError"), true);
                Result->SetStringField(TEXT("code"), TEXT("GRAPH_REF_INVALID"));
                Result->SetStringField(TEXT("message"), TEXT("graphRef.assetPath is required for kind=inline."));
                return Result;
            }
            AssetPath = NormalizeAssetPath(RefAssetPath);
        }
        else if (Kind.Equals(TEXT("asset")))
        {
            FString RefAssetPath;
            if (!(*GraphRefObj)->TryGetStringField(TEXT("assetPath"), RefAssetPath) || RefAssetPath.IsEmpty())
            {
                Result->SetBoolField(TEXT("isError"), true);
                Result->SetStringField(TEXT("code"), TEXT("GRAPH_REF_INVALID"));
                Result->SetStringField(TEXT("message"), TEXT("graphRef.assetPath is required for kind=asset."));
                return Result;
            }
            AssetPath = NormalizeAssetPath(RefAssetPath);
            if (!(*GraphRefObj)->TryGetStringField(TEXT("graphName"), GraphName) || GraphName.IsEmpty())
            {
                GraphName = DefaultSingleGraphName;
            }
        }
        else
        {
            Result->SetBoolField(TEXT("isError"), true);
            Result->SetStringField(TEXT("code"), TEXT("GRAPH_REF_INVALID"));
            Result->SetStringField(TEXT("message"), FString::Printf(TEXT("Unsupported graphRef.kind: %s"), *Kind));
            return Result;
        }
    }
    else
    {
        // Mode A: assetPath is always required; graphName is only required for multi-graph assets.
        if (!Arguments->TryGetStringField(TEXT("assetPath"), AssetPath) || AssetPath.IsEmpty())
        {
            Result->SetBoolField(TEXT("isError"), true);
            Result->SetStringField(TEXT("code"), TEXT("INVALID_ARGUMENT"));
            Result->SetStringField(TEXT("message"), TEXT("arguments.assetPath is required (Mode A) or supply graphRef (Mode B)."));
            return Result;
        }
        if (!Arguments->TryGetStringField(TEXT("graphName"), GraphName) || GraphName.IsEmpty())
        {
            if (GraphType.Equals(TEXT("blueprint")))
            {
                Result->SetBoolField(TEXT("isError"), true);
                Result->SetStringField(TEXT("code"), TEXT("INVALID_ARGUMENT"));
                Result->SetStringField(TEXT("message"), TEXT("arguments.graphName is required (Mode A) or supply graphRef (Mode B)."));
                return Result;
            }
            GraphName = DefaultSingleGraphName;
        }
        AssetPath = NormalizeAssetPath(AssetPath);
    }

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
                    UPackage* FuncPkg = FuncCall->MaterialFunction->GetPackage();
                    FString FuncAssetPath = FuncPkg ? FuncPkg->GetPathName() : FuncCall->MaterialFunction->GetPathName();
                    if (!FuncPkg)
                    {
                        int32 DotIdx;
                        if (FuncAssetPath.FindLastChar(TEXT('.'), DotIdx)) { FuncAssetPath = FuncAssetPath.Left(DotIdx); }
                    }
                    TSharedPtr<FJsonObject> ChildRef = MakeShared<FJsonObject>();
                    ChildRef->SetStringField(TEXT("kind"), TEXT("asset"));
                    ChildRef->SetStringField(TEXT("assetPath"), FuncAssetPath);
                    Node->SetObjectField(TEXT("childGraphRef"), ChildRef);
                    Node->SetStringField(TEXT("childLoadStatus"), TEXT("loaded"));
                    ++MaterialChildGraphRefCount;
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
        {
            TSharedPtr<FJsonObject> ResponseGraphRef = MakeShared<FJsonObject>();
            ResponseGraphRef->SetStringField(TEXT("kind"), TEXT("asset"));
            ResponseGraphRef->SetStringField(TEXT("assetPath"), AssetPath);
            Result->SetObjectField(TEXT("graphRef"), ResponseGraphRef);
        }
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
        TSet<FString> SpawnerNodeIds;
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
            const bool bIsSpawnerNode =
                NodeClassPath.Equals(TEXT("/Script/PCG.PCGStaticMeshSpawnerSettings")) ||
                NodeClassPath.Equals(TEXT("/Script/PCG.PCGSpawnActorSettings"));

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
            if (bIsSpawnerNode)
            {
                SpawnerNodeIds.Add(NodeId);
            }
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

            // Annotate PCG subgraph nodes with childGraphRef via reflection.
            if (NodeClassPath.Contains(TEXT("Subgraph")))
            {
                UPCGSettings* NodeSettings = NodeObj->GetSettings();
                if (NodeSettings)
                {
                    // Resolve the referenced PCG subgraph: try hard ref first, then soft ref.
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
                            if (!SoftProp->PropertyClass || !SoftProp->PropertyClass->GetPathName().Contains(TEXT("PCGGraph"))) { continue; }
                            const FSoftObjectPtr SoftPtr = SoftProp->GetPropertyValue_InContainer(NodeSettings);
                            const FSoftObjectPath SoftPath = SoftPtr.ToSoftObjectPath();
                            if (SoftPath.IsNull()) { continue; }
                            UObject* Resolved = SoftPath.ResolveObject();
                            if (Resolved) { SubgraphAsset = Resolved; }
                            else
                            {
                                SubgraphSoftPath = SoftPath.GetAssetPathString();
                                int32 DotIdx;
                                if (SubgraphSoftPath.FindLastChar(TEXT('.'), DotIdx)) { SubgraphSoftPath = SubgraphSoftPath.Left(DotIdx); }
                            }
                            break;
                        }
                    }
                    if (SubgraphAsset || !SubgraphSoftPath.IsEmpty())
                    {
                        FString SubAssetPath;
                        FString ChildLoadStatus;
                        if (SubgraphAsset)
                        {
                            UPackage* SubPkg = SubgraphAsset->GetPackage();
                            SubAssetPath = SubPkg ? SubPkg->GetPathName() : SubgraphAsset->GetPathName();
                            if (!SubPkg) { int32 DotIdx; if (SubAssetPath.FindLastChar(TEXT('.'), DotIdx)) { SubAssetPath = SubAssetPath.Left(DotIdx); } }
                            ChildLoadStatus = TEXT("loaded");
                        }
                        else
                        {
                            SubAssetPath = SubgraphSoftPath;
                            ChildLoadStatus = TEXT("not_found");
                        }
                        TSharedPtr<FJsonObject> ChildRef = MakeShared<FJsonObject>();
                        ChildRef->SetStringField(TEXT("kind"), TEXT("asset"));
                        ChildRef->SetStringField(TEXT("assetPath"), SubAssetPath);
                        Node->SetObjectField(TEXT("childGraphRef"), ChildRef);
                        Node->SetStringField(TEXT("childLoadStatus"), ChildLoadStatus);
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
        AppendPcgGraphHealthDiagnostics(PcgGraph, SpawnerNodeIds, Edges, Diagnostics);

        BuildMinimalSnapshotResult(TEXT("pcg"), Nodes, Edges, Diagnostics);
        // Echo effective graphRef at response root.
        {
            TSharedPtr<FJsonObject> ResponseGraphRef = MakeShared<FJsonObject>();
            ResponseGraphRef->SetStringField(TEXT("kind"), TEXT("asset"));
            ResponseGraphRef->SetStringField(TEXT("assetPath"), AssetPath);
            Result->SetObjectField(TEXT("graphRef"), ResponseGraphRef);
        }
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

    FString NodesJson;
    FString Error;
    bool bOk = false;

    if (!InlineNodeGuid.IsEmpty())
    {
        // Mode B inline: query the composite subgraph by node guid.
        FString SubgraphNameOut;
        bOk = FLoomleBlueprintAdapter::ListCompositeSubgraphNodes(AssetPath, InlineNodeGuid, SubgraphNameOut, NodesJson, Error);
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
        bOk = FLoomleBlueprintAdapter::ListGraphNodes(AssetPath, GraphName, NodesJson, Error);
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
        Limit = 200;
        double LimitNumber = 0.0;
        if (Arguments->TryGetNumberField(TEXT("limit"), LimitNumber))
        {
            Limit = FMath::Clamp(static_cast<int32>(LimitNumber), 1, 1000);
        }
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

                TSharedPtr<FJsonObject> ChildRef = MakeShared<FJsonObject>();
                ChildRef->SetStringField(TEXT("kind"), TEXT("inline"));
                ChildRef->SetStringField(TEXT("nodeGuid"), GuidText);
                ChildRef->SetStringField(TEXT("assetPath"), AssetPath);
                (*SnapshotNodeObj)->SetObjectField(TEXT("childGraphRef"), ChildRef);
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
    TSharedPtr<FJsonObject> ResponseGraphRef = MakeShared<FJsonObject>();
    if (!InlineNodeGuid.IsEmpty())
    {
        ResponseGraphRef->SetStringField(TEXT("kind"), TEXT("inline"));
        ResponseGraphRef->SetStringField(TEXT("nodeGuid"), InlineNodeGuid);
        ResponseGraphRef->SetStringField(TEXT("assetPath"), AssetPath);
    }
    else
    {
        ResponseGraphRef->SetStringField(TEXT("kind"), TEXT("asset"));
        ResponseGraphRef->SetStringField(TEXT("assetPath"), AssetPath);
        ResponseGraphRef->SetStringField(TEXT("graphName"), GraphName);
    }

    Result->SetStringField(TEXT("graphType"), GraphType);
    Result->SetStringField(TEXT("assetPath"), AssetPath);
    Result->SetStringField(TEXT("graphName"), GraphName);
    Result->SetObjectField(TEXT("graphRef"), ResponseGraphRef);
    Result->SetStringField(TEXT("revision"), Revision);
    Result->SetObjectField(TEXT("semanticSnapshot"), Snapshot);
    Result->SetStringField(TEXT("nextCursor"), TEXT(""));

    TSharedPtr<FJsonObject> Meta = MakeShared<FJsonObject>();
    Meta->SetNumberField(TEXT("totalNodes"), Nodes.Num());
    Meta->SetNumberField(TEXT("returnedNodes"), SnapshotNodes.Num());
    Meta->SetNumberField(TEXT("totalEdges"), Edges.Num());
    Meta->SetNumberField(TEXT("returnedEdges"), Edges.Num());
    Meta->SetBoolField(TEXT("truncated"), AddedCount < Nodes.Num() && SnapshotNodes.Num() >= Limit);
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

TSharedPtr<FJsonObject> FLoomleBridgeModule::BuildGraphOpsToolResult(const TSharedPtr<FJsonObject>& Arguments) const
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

    FString StabilityFilter;
    if (Arguments.IsValid())
    {
        Arguments->TryGetStringField(TEXT("stability"), StabilityFilter);
    }
    StabilityFilter = StabilityFilter.TrimStartAndEnd().ToLower();
    if (!StabilityFilter.IsEmpty()
        && !StabilityFilter.Equals(TEXT("stable"))
        && !StabilityFilter.Equals(TEXT("experimental")))
    {
        Result->SetBoolField(TEXT("isError"), true);
        Result->SetStringField(TEXT("code"), TEXT("INVALID_ARGUMENT"));
        Result->SetStringField(TEXT("message"), TEXT("stability must be stable or experimental when provided."));
        return Result;
    }

    FString Query;
    if (Arguments.IsValid())
    {
        Arguments->TryGetStringField(TEXT("query"), Query);
    }
    const FString QueryLower = Query.TrimStartAndEnd().ToLower();

    int32 Limit = 1000;
    double LimitNumber = 0.0;
    if (Arguments.IsValid() && Arguments->TryGetNumberField(TEXT("limit"), LimitNumber))
    {
        Limit = FMath::Clamp(static_cast<int32>(LimitNumber), 1, 1000);
    }

    TArray<FGraphSemanticOpSpec> Specs;
    AppendGraphSemanticOpSpecs(GraphType, Specs);

    TArray<TSharedPtr<FJsonValue>> Ops;
    TArray<TSharedPtr<FJsonValue>> Diagnostics;
    for (const FGraphSemanticOpSpec& Spec : Specs)
    {
        if (Ops.Num() >= Limit)
        {
            break;
        }

        if (!StabilityFilter.IsEmpty() && !StabilityFilter.Equals(TEXT("stable")))
        {
            continue;
        }

        const FString SearchBlob = (FString(Spec.OpId) + TEXT("|") + Spec.Scope + TEXT("|") + Spec.Summary).ToLower();
        if (!QueryLower.IsEmpty() && !SearchBlob.Contains(QueryLower))
        {
            continue;
        }

        UClass* NodeClass = LoadObject<UClass>(nullptr, Spec.NodeClassPath);
        if (NodeClass == nullptr)
        {
            TSharedPtr<FJsonObject> Diagnostic = MakeShared<FJsonObject>();
            Diagnostic->SetStringField(TEXT("code"), TEXT("SEMANTIC_OP_CLASS_UNAVAILABLE"));
            Diagnostic->SetStringField(TEXT("opId"), Spec.OpId);
            Diagnostic->SetStringField(TEXT("message"), FString::Printf(TEXT("Semantic op class is unavailable in this editor build: %s"), Spec.NodeClassPath));
            Diagnostics.Add(MakeShared<FJsonValueObject>(Diagnostic));
            continue;
        }

        TSharedPtr<FJsonObject> OpObject = MakeShared<FJsonObject>();
        OpObject->SetStringField(TEXT("opId"), Spec.OpId);
        OpObject->SetStringField(TEXT("stability"), TEXT("stable"));
        OpObject->SetStringField(TEXT("scope"), Spec.Scope);
        OpObject->SetStringField(TEXT("summary"), Spec.Summary);
        Ops.Add(MakeShared<FJsonValueObject>(OpObject));
    }

    TSharedPtr<FJsonObject> Meta = MakeShared<FJsonObject>();
    Meta->SetStringField(TEXT("source"), TEXT("loomle_catalog"));
    Meta->SetStringField(TEXT("coverage"), TEXT("curated"));

    Result->SetStringField(TEXT("graphType"), GraphType);
    Result->SetArrayField(TEXT("ops"), Ops);
    Result->SetObjectField(TEXT("meta"), Meta);
    Result->SetArrayField(TEXT("diagnostics"), Diagnostics);
    return Result;
}

TSharedPtr<FJsonObject> FLoomleBridgeModule::BuildGraphOpsResolveToolResult(const TSharedPtr<FJsonObject>& Arguments) const
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

    const TSharedPtr<FJsonObject>* GraphRefObj = nullptr;
    if (!Arguments.IsValid()
        || !Arguments->TryGetObjectField(TEXT("graphRef"), GraphRefObj)
        || GraphRefObj == nullptr
        || !(*GraphRefObj).IsValid())
    {
        Result->SetBoolField(TEXT("isError"), true);
        Result->SetStringField(TEXT("code"), TEXT("INVALID_ARGUMENT"));
        Result->SetStringField(TEXT("message"), TEXT("graphRef is required."));
        return Result;
    }

    const TArray<TSharedPtr<FJsonValue>>* Items = nullptr;
    if (!Arguments->TryGetArrayField(TEXT("items"), Items) || Items == nullptr || Items->Num() == 0)
    {
        Result->SetBoolField(TEXT("isError"), true);
        Result->SetStringField(TEXT("code"), TEXT("INVALID_ARGUMENT"));
        Result->SetStringField(TEXT("message"), TEXT("items[] is required."));
        return Result;
    }

    TSharedPtr<FJsonObject> QueryArgs = MakeShared<FJsonObject>();
    QueryArgs->SetStringField(TEXT("graphType"), GraphType);
    QueryArgs->SetObjectField(TEXT("graphRef"), CloneJsonObject(*GraphRefObj));
    const TSharedPtr<FJsonObject> QueryResult = BuildGraphQueryToolResult(QueryArgs);
    bool bQueryIsError = false;
    if (QueryResult.IsValid() && QueryResult->TryGetBoolField(TEXT("isError"), bQueryIsError) && bQueryIsError)
    {
        return QueryResult;
    }

    FString FromNodeId;
    FString FromPinName;
    FString ToNodeId;
    FString ToPinName;
    const TSharedPtr<FJsonObject>* ContextObj = nullptr;
    if (Arguments->TryGetObjectField(TEXT("context"), ContextObj) && ContextObj != nullptr && (*ContextObj).IsValid())
    {
        const TSharedPtr<FJsonObject>* FromPinObj = nullptr;
        if ((*ContextObj)->TryGetObjectField(TEXT("fromPin"), FromPinObj) && FromPinObj != nullptr && (*FromPinObj).IsValid())
        {
            TryReadPinEndpoint(*FromPinObj, FromNodeId, FromPinName);
        }

        const TSharedPtr<FJsonObject>* ToPinObj = nullptr;
        if ((*ContextObj)->TryGetObjectField(TEXT("toPin"), ToPinObj) && ToPinObj != nullptr && (*ToPinObj).IsValid())
        {
            TryReadPinEndpoint(*ToPinObj, ToNodeId, ToPinName);
        }

        const TSharedPtr<FJsonObject>* EdgeObj = nullptr;
        if ((*ContextObj)->TryGetObjectField(TEXT("edge"), EdgeObj) && EdgeObj != nullptr && (*EdgeObj).IsValid())
        {
            const TSharedPtr<FJsonObject>* EdgeFromPinObj = nullptr;
            if ((*EdgeObj)->TryGetObjectField(TEXT("fromPin"), EdgeFromPinObj) && EdgeFromPinObj != nullptr && (*EdgeFromPinObj).IsValid())
            {
                TryReadPinEndpoint(*EdgeFromPinObj, FromNodeId, FromPinName);
            }

            const TSharedPtr<FJsonObject>* EdgeToPinObj = nullptr;
            if ((*EdgeObj)->TryGetObjectField(TEXT("toPin"), EdgeToPinObj) && EdgeToPinObj != nullptr && (*EdgeToPinObj).IsValid())
            {
                TryReadPinEndpoint(*EdgeToPinObj, ToNodeId, ToPinName);
            }
        }
    }

    bool bEnablePcgCompose = false;
    if (GraphType.Equals(TEXT("pcg")) && Items->Num() > 1 && !FromPinName.IsEmpty())
    {
        bEnablePcgCompose = true;
        for (const TSharedPtr<FJsonValue>& ItemValue : *Items)
        {
            const TSharedPtr<FJsonObject>* ItemObj = nullptr;
            if (!ItemValue.IsValid() || !ItemValue->TryGetObject(ItemObj) || ItemObj == nullptr || !(*ItemObj).IsValid())
            {
                bEnablePcgCompose = false;
                break;
            }

            FString OpId;
            if (!(*ItemObj)->TryGetStringField(TEXT("opId"), OpId) || !IsComposablePcgSemanticOp(OpId))
            {
                bEnablePcgCompose = false;
                break;
            }
        }
    }

    FString ComposeSourceNodeId = FromNodeId;
    FString ComposeSourcePinName = FromPinName;
    FString ComposeSourceNodeRef;
    FString ComposeSourceKind = TEXT("nodeId");

    TArray<TSharedPtr<FJsonValue>> Results;
    for (int32 Index = 0; Index < Items->Num(); ++Index)
    {
        const TSharedPtr<FJsonValue>& ItemValue = (*Items)[Index];
        const TSharedPtr<FJsonObject>* ItemObj = nullptr;
        if (!ItemValue.IsValid() || !ItemValue->TryGetObject(ItemObj) || ItemObj == nullptr || !(*ItemObj).IsValid())
        {
            Result->SetBoolField(TEXT("isError"), true);
            Result->SetStringField(TEXT("code"), TEXT("INVALID_ARGUMENT"));
            Result->SetStringField(TEXT("message"), FString::Printf(TEXT("items[%d] must be an object."), Index));
            return Result;
        }

        FString OpId;
        if (!(*ItemObj)->TryGetStringField(TEXT("opId"), OpId) || OpId.IsEmpty())
        {
            Result->SetBoolField(TEXT("isError"), true);
            Result->SetStringField(TEXT("code"), TEXT("INVALID_ARGUMENT"));
            Result->SetStringField(TEXT("message"), FString::Printf(TEXT("items[%d].opId is required."), Index));
            return Result;
        }

        FString ClientRef;
        (*ItemObj)->TryGetStringField(TEXT("clientRef"), ClientRef);
        const TSharedPtr<FJsonObject>* HintsObj = nullptr;
        const bool bHasHints = (*ItemObj)->TryGetObjectField(TEXT("hints"), HintsObj)
            && HintsObj != nullptr
            && (*HintsObj).IsValid();
        FString HintVariableName;
        FString HintVariableClassPath;
        FString HintTargetRootPin;
        FString HintFunctionAssetPath;
        if (bHasHints)
        {
            (*HintsObj)->TryGetStringField(TEXT("variableName"), HintVariableName);
            (*HintsObj)->TryGetStringField(TEXT("variableClassPath"), HintVariableClassPath);
            (*HintsObj)->TryGetStringField(TEXT("targetRootPin"), HintTargetRootPin);
            (*HintsObj)->TryGetStringField(TEXT("functionAssetPath"), HintFunctionAssetPath);
        }

        TSharedPtr<FJsonObject> ResultItem = MakeShared<FJsonObject>();
        ResultItem->SetStringField(TEXT("opId"), OpId);
        if (!ClientRef.IsEmpty())
        {
            ResultItem->SetStringField(TEXT("clientRef"), ClientRef);
        }

        FGraphSemanticOpSpec Spec;
        if (!TryFindGraphSemanticOpSpec(GraphType, OpId, Spec))
        {
            TSharedPtr<FJsonObject> Compatibility = MakeShared<FJsonObject>();
            Compatibility->SetBoolField(TEXT("isCompatible"), false);
            Compatibility->SetArrayField(TEXT("reasons"), TArray<TSharedPtr<FJsonValue>>{
                MakeShared<FJsonValueString>(TEXT("unsupported_opid"))
            });

            ResultItem->SetBoolField(TEXT("resolved"), false);
            ResultItem->SetObjectField(TEXT("compatibility"), Compatibility);
            ResultItem->SetStringField(TEXT("reason"), TEXT("unsupported_opid"));
            Results.Add(MakeShared<FJsonValueObject>(ResultItem));
            continue;
        }

        UClass* NodeClass = LoadObject<UClass>(nullptr, Spec.NodeClassPath);
        if (NodeClass == nullptr)
        {
            TSharedPtr<FJsonObject> Compatibility = MakeShared<FJsonObject>();
            Compatibility->SetBoolField(TEXT("isCompatible"), false);
            Compatibility->SetArrayField(TEXT("reasons"), TArray<TSharedPtr<FJsonValue>>{
                MakeShared<FJsonValueString>(TEXT("class_unavailable"))
            });

            ResultItem->SetBoolField(TEXT("resolved"), false);
            ResultItem->SetObjectField(TEXT("compatibility"), Compatibility);
            ResultItem->SetStringField(TEXT("reason"), TEXT("class_unavailable"));
            Results.Add(MakeShared<FJsonValueObject>(ResultItem));
            continue;
        }

        if (Spec.bRequiresPinContext && (FromNodeId.IsEmpty() || FromPinName.IsEmpty()))
        {
            TSharedPtr<FJsonObject> Compatibility = MakeShared<FJsonObject>();
            Compatibility->SetBoolField(TEXT("isCompatible"), false);
            Compatibility->SetArrayField(TEXT("reasons"), TArray<TSharedPtr<FJsonValue>>{
                MakeShared<FJsonValueString>(TEXT("requires_pin_context"))
            });

            TSharedPtr<FJsonObject> Remediation = MakeShared<FJsonObject>();
            Remediation->SetArrayField(TEXT("requiredContext"), TArray<TSharedPtr<FJsonValue>>{
                MakeShared<FJsonValueString>(TEXT("fromPin"))
            });
            Remediation->SetArrayField(TEXT("missingFields"), TArray<TSharedPtr<FJsonValue>>{
                MakeShared<FJsonValueString>(TEXT("context.fromPin"))
            });
            Remediation->SetStringField(TEXT("nextAction"), TEXT("retry_resolve_with_fromPin"));
            Remediation->SetStringField(TEXT("fallbackKind"), TEXT("manual_readback"));

            ResultItem->SetBoolField(TEXT("resolved"), false);
            ResultItem->SetObjectField(TEXT("compatibility"), Compatibility);
            ResultItem->SetObjectField(TEXT("remediation"), Remediation);
            ResultItem->SetStringField(TEXT("reason"), TEXT("incompatible_context"));
            Results.Add(MakeShared<FJsonValueObject>(ResultItem));
            continue;
        }

        if (GraphType.Equals(TEXT("blueprint"))
            && (OpId.Equals(TEXT("bp.var.get")) || OpId.Equals(TEXT("bp.var.set")))
            && HintVariableName.IsEmpty())
        {
            TSharedPtr<FJsonObject> Compatibility = MakeShared<FJsonObject>();
            Compatibility->SetBoolField(TEXT("isCompatible"), false);
            Compatibility->SetArrayField(TEXT("reasons"), TArray<TSharedPtr<FJsonValue>>{
                MakeShared<FJsonValueString>(TEXT("requires_variable_context"))
            });

            TSharedPtr<FJsonObject> Remediation = MakeShared<FJsonObject>();
            Remediation->SetArrayField(TEXT("requiredContext"), TArray<TSharedPtr<FJsonValue>>{
                MakeShared<FJsonValueString>(TEXT("variableName"))
            });
            Remediation->SetArrayField(TEXT("missingFields"), TArray<TSharedPtr<FJsonValue>>{
                MakeShared<FJsonValueString>(TEXT("items[].hints.variableName"))
            });
            Remediation->SetStringField(TEXT("nextAction"), TEXT("retry_resolve_with_variableName"));
            Remediation->SetStringField(TEXT("fallbackKind"), TEXT("manual_readback"));

            ResultItem->SetBoolField(TEXT("resolved"), false);
            ResultItem->SetObjectField(TEXT("compatibility"), Compatibility);
            ResultItem->SetObjectField(TEXT("remediation"), Remediation);
            ResultItem->SetStringField(TEXT("reason"), TEXT("incompatible_context"));
            Results.Add(MakeShared<FJsonValueObject>(ResultItem));
            continue;
        }

        TSharedPtr<FJsonObject> Compatibility = MakeShared<FJsonObject>();
        Compatibility->SetBoolField(TEXT("isCompatible"), true);
        Compatibility->SetArrayField(TEXT("reasons"), TArray<TSharedPtr<FJsonValue>>{});

        TSharedPtr<FJsonObject> PreferredPlan = MakeShared<FJsonObject>();
        PreferredPlan->SetStringField(TEXT("realizationKind"), TEXT("spawn_node"));
        PreferredPlan->SetStringField(TEXT("preferredMutateOp"), TEXT("addNode.byClass"));
        TSharedPtr<FJsonObject> ArgsObj = MakeShared<FJsonObject>();
        ArgsObj->SetStringField(TEXT("nodeClassPath"), Spec.NodeClassPath);
        PreferredPlan->SetObjectField(TEXT("args"), ArgsObj);
        PreferredPlan->SetStringField(TEXT("source"), TEXT("loomle_catalog"));
        PreferredPlan->SetStringField(TEXT("coverage"), Spec.Coverage);
        PreferredPlan->SetStringField(TEXT("determinism"), Spec.Determinism);

        if (GraphType.Equals(TEXT("blueprint")))
        {
            TArray<TSharedPtr<FJsonValue>> PinHints;
            if (OpId.Equals(TEXT("core.reroute")))
            {
                TSharedPtr<FJsonObject> InHint = MakeShared<FJsonObject>();
                InHint->SetStringField(TEXT("role"), TEXT("input"));
                InHint->SetStringField(TEXT("pinName"), TEXT("Input"));
                PinHints.Add(MakeShared<FJsonValueObject>(InHint));

                TSharedPtr<FJsonObject> OutHint = MakeShared<FJsonObject>();
                OutHint->SetStringField(TEXT("role"), TEXT("output"));
                OutHint->SetStringField(TEXT("pinName"), TEXT("Output"));
                PinHints.Add(MakeShared<FJsonValueObject>(OutHint));
            }
            else if (OpId.Equals(TEXT("bp.flow.branch")))
            {
                TSharedPtr<FJsonObject> ExecInHint = MakeShared<FJsonObject>();
                ExecInHint->SetStringField(TEXT("role"), TEXT("exec_input"));
                ExecInHint->SetStringField(TEXT("pinName"), TEXT("execute"));
                PinHints.Add(MakeShared<FJsonValueObject>(ExecInHint));

                TSharedPtr<FJsonObject> ConditionHint = MakeShared<FJsonObject>();
                ConditionHint->SetStringField(TEXT("role"), TEXT("condition"));
                ConditionHint->SetStringField(TEXT("pinName"), TEXT("Condition"));
                PinHints.Add(MakeShared<FJsonValueObject>(ConditionHint));

                TSharedPtr<FJsonObject> TrueHint = MakeShared<FJsonObject>();
                TrueHint->SetStringField(TEXT("role"), TEXT("true_branch"));
                TrueHint->SetStringField(TEXT("pinName"), TEXT("Then"));
                PinHints.Add(MakeShared<FJsonValueObject>(TrueHint));

                TSharedPtr<FJsonObject> FalseHint = MakeShared<FJsonObject>();
                FalseHint->SetStringField(TEXT("role"), TEXT("false_branch"));
                FalseHint->SetStringField(TEXT("pinName"), TEXT("Else"));
                PinHints.Add(MakeShared<FJsonValueObject>(FalseHint));
            }
            else if (OpId.Equals(TEXT("bp.flow.sequence")))
            {
                TSharedPtr<FJsonObject> ExecInHint = MakeShared<FJsonObject>();
                ExecInHint->SetStringField(TEXT("role"), TEXT("exec_input"));
                ExecInHint->SetStringField(TEXT("pinName"), TEXT("execute"));
                PinHints.Add(MakeShared<FJsonValueObject>(ExecInHint));

                TSharedPtr<FJsonObject> Then0Hint = MakeShared<FJsonObject>();
                Then0Hint->SetStringField(TEXT("role"), TEXT("exec_output_primary"));
                Then0Hint->SetStringField(TEXT("pinName"), TEXT("Then_0"));
                PinHints.Add(MakeShared<FJsonValueObject>(Then0Hint));

                TSharedPtr<FJsonObject> Then1Hint = MakeShared<FJsonObject>();
                Then1Hint->SetStringField(TEXT("role"), TEXT("exec_output_secondary"));
                Then1Hint->SetStringField(TEXT("pinName"), TEXT("Then_1"));
                PinHints.Add(MakeShared<FJsonValueObject>(Then1Hint));
            }
            else if (OpId.Equals(TEXT("bp.flow.delay")))
            {
                ArgsObj->SetStringField(TEXT("functionClassPath"), TEXT("/Script/Engine.KismetSystemLibrary"));
                ArgsObj->SetStringField(TEXT("functionName"), TEXT("Delay"));

                TSharedPtr<FJsonObject> ExecInHint = MakeShared<FJsonObject>();
                ExecInHint->SetStringField(TEXT("role"), TEXT("exec_input"));
                ExecInHint->SetStringField(TEXT("pinName"), TEXT("execute"));
                PinHints.Add(MakeShared<FJsonValueObject>(ExecInHint));

                TSharedPtr<FJsonObject> DurationHint = MakeShared<FJsonObject>();
                DurationHint->SetStringField(TEXT("role"), TEXT("duration"));
                DurationHint->SetStringField(TEXT("pinName"), TEXT("Duration"));
                PinHints.Add(MakeShared<FJsonValueObject>(DurationHint));

                TSharedPtr<FJsonObject> ThenHint = MakeShared<FJsonObject>();
                ThenHint->SetStringField(TEXT("role"), TEXT("exec_output"));
                ThenHint->SetStringField(TEXT("pinName"), TEXT("then"));
                PinHints.Add(MakeShared<FJsonValueObject>(ThenHint));

                TSharedPtr<FJsonObject> SettingsTemplate = MakeShared<FJsonObject>();
                SettingsTemplate->SetNumberField(TEXT("duration"), 0.2);
                PreferredPlan->SetObjectField(TEXT("settingsTemplate"), SettingsTemplate);
            }
            else if (OpId.Equals(TEXT("bp.flow.do_once")))
            {
                ArgsObj->SetStringField(TEXT("macroLibraryAssetPath"), BlueprintStandardMacrosAssetPath);
                ArgsObj->SetStringField(TEXT("macroGraphName"), TEXT("DoOnce"));

                TSharedPtr<FJsonObject> ExecInHint = MakeShared<FJsonObject>();
                ExecInHint->SetStringField(TEXT("role"), TEXT("exec_input"));
                ExecInHint->SetStringField(TEXT("pinName"), TEXT("Execute"));
                PinHints.Add(MakeShared<FJsonValueObject>(ExecInHint));

                TSharedPtr<FJsonObject> CompletedHint = MakeShared<FJsonObject>();
                CompletedHint->SetStringField(TEXT("role"), TEXT("exec_output"));
                CompletedHint->SetStringField(TEXT("pinName"), TEXT("Completed"));
                PinHints.Add(MakeShared<FJsonValueObject>(CompletedHint));

                TSharedPtr<FJsonObject> ResetHint = MakeShared<FJsonObject>();
                ResetHint->SetStringField(TEXT("role"), TEXT("reset"));
                ResetHint->SetStringField(TEXT("pinName"), TEXT("Reset"));
                PinHints.Add(MakeShared<FJsonValueObject>(ResetHint));
            }
            else if (OpId.Equals(TEXT("bp.debug.print_string")))
            {
                ArgsObj->SetStringField(TEXT("functionClassPath"), TEXT("/Script/Engine.KismetSystemLibrary"));
                ArgsObj->SetStringField(TEXT("functionName"), TEXT("PrintString"));

                TSharedPtr<FJsonObject> ExecInHint = MakeShared<FJsonObject>();
                ExecInHint->SetStringField(TEXT("role"), TEXT("exec_input"));
                ExecInHint->SetStringField(TEXT("pinName"), TEXT("execute"));
                PinHints.Add(MakeShared<FJsonValueObject>(ExecInHint));

                TSharedPtr<FJsonObject> StringHint = MakeShared<FJsonObject>();
                StringHint->SetStringField(TEXT("role"), TEXT("message"));
                StringHint->SetStringField(TEXT("pinName"), TEXT("In String"));
                PinHints.Add(MakeShared<FJsonValueObject>(StringHint));

                TSharedPtr<FJsonObject> ThenHint = MakeShared<FJsonObject>();
                ThenHint->SetStringField(TEXT("role"), TEXT("exec_output"));
                ThenHint->SetStringField(TEXT("pinName"), TEXT("then"));
                PinHints.Add(MakeShared<FJsonValueObject>(ThenHint));
            }
            else if (OpId.Equals(TEXT("bp.var.get")))
            {
                ArgsObj->SetStringField(TEXT("variableName"), HintVariableName);
                if (!HintVariableClassPath.IsEmpty())
                {
                    ArgsObj->SetStringField(TEXT("variableClassPath"), HintVariableClassPath);
                }

                TSharedPtr<FJsonObject> ValueHint = MakeShared<FJsonObject>();
                ValueHint->SetStringField(TEXT("role"), TEXT("value_output"));
                ValueHint->SetStringField(TEXT("pinName"), HintVariableName);
                PinHints.Add(MakeShared<FJsonValueObject>(ValueHint));
            }
            else if (OpId.Equals(TEXT("bp.var.set")))
            {
                ArgsObj->SetStringField(TEXT("variableName"), HintVariableName);
                if (!HintVariableClassPath.IsEmpty())
                {
                    ArgsObj->SetStringField(TEXT("variableClassPath"), HintVariableClassPath);
                }

                TSharedPtr<FJsonObject> ExecInHint = MakeShared<FJsonObject>();
                ExecInHint->SetStringField(TEXT("role"), TEXT("exec_input"));
                ExecInHint->SetStringField(TEXT("pinName"), TEXT("execute"));
                PinHints.Add(MakeShared<FJsonValueObject>(ExecInHint));

                TSharedPtr<FJsonObject> ValueHint = MakeShared<FJsonObject>();
                ValueHint->SetStringField(TEXT("role"), TEXT("value_input"));
                ValueHint->SetStringField(TEXT("pinName"), HintVariableName);
                PinHints.Add(MakeShared<FJsonValueObject>(ValueHint));

                TSharedPtr<FJsonObject> ThenHint = MakeShared<FJsonObject>();
                ThenHint->SetStringField(TEXT("role"), TEXT("exec_output"));
                ThenHint->SetStringField(TEXT("pinName"), TEXT("then"));
                PinHints.Add(MakeShared<FJsonValueObject>(ThenHint));
            }

            if (PinHints.Num() > 0)
            {
                PreferredPlan->SetArrayField(TEXT("pinHints"), PinHints);
            }

            if (!FromNodeId.IsEmpty() && !FromPinName.IsEmpty())
            {
                const FString StepNodeRef = !ClientRef.IsEmpty()
                    ? ClientRef
                    : FString::Printf(TEXT("resolved_%d"), Index);
                TArray<TSharedPtr<FJsonValue>> Steps;
                FString TargetPinName;

                if (OpId.Equals(TEXT("core.reroute")))
                {
                    TargetPinName = TEXT("Input");
                }
                else if (OpId.Equals(TEXT("bp.flow.branch")))
                {
                    TargetPinName = TEXT("execute");
                }
                else if (OpId.Equals(TEXT("bp.flow.sequence")))
                {
                    TargetPinName = TEXT("execute");
                }
                else if (OpId.Equals(TEXT("bp.flow.delay")))
                {
                    TargetPinName = TEXT("execute");
                }
                else if (OpId.Equals(TEXT("bp.debug.print_string")))
                {
                    TargetPinName = TEXT("execute");
                }
                else if (OpId.Equals(TEXT("bp.var.set")))
                {
                    TargetPinName = TEXT("execute");
                }

                if (!TargetPinName.IsEmpty())
                {
                    TSharedPtr<FJsonObject> AddStep = MakeShared<FJsonObject>();
                    AddStep->SetStringField(TEXT("op"), TEXT("addNode.byClass"));
                    AddStep->SetStringField(TEXT("clientRef"), StepNodeRef);
                    TSharedPtr<FJsonObject> AddStepArgs = MakeShared<FJsonObject>();
                    AddStepArgs->SetStringField(TEXT("nodeClassPath"), Spec.NodeClassPath);
                    AddStep->SetObjectField(TEXT("args"), AddStepArgs);
                    Steps.Add(MakeShared<FJsonValueObject>(AddStep));

                    TSharedPtr<FJsonObject> ConnectStep = MakeShared<FJsonObject>();
                    ConnectStep->SetStringField(TEXT("op"), TEXT("connectPins"));
                    TSharedPtr<FJsonObject> ConnectArgs = MakeShared<FJsonObject>();
                    TSharedPtr<FJsonObject> FromObj = MakeShared<FJsonObject>();
                    FromObj->SetStringField(TEXT("nodeId"), FromNodeId);
                    FromObj->SetStringField(TEXT("pinName"), FromPinName);
                    TSharedPtr<FJsonObject> ToObj = MakeShared<FJsonObject>();
                    ToObj->SetStringField(TEXT("nodeRef"), StepNodeRef);
                    ToObj->SetStringField(TEXT("pinName"), TargetPinName);
                    ConnectArgs->SetObjectField(TEXT("from"), FromObj);
                    ConnectArgs->SetObjectField(TEXT("to"), ToObj);
                    ConnectStep->SetObjectField(TEXT("args"), ConnectArgs);
                    Steps.Add(MakeShared<FJsonValueObject>(ConnectStep));

                    PreferredPlan->SetStringField(TEXT("realizationKind"), TEXT("graph_insert"));
                    PreferredPlan->SetStringField(TEXT("coverage"), TEXT("contextual"));
                    PreferredPlan->SetArrayField(TEXT("steps"), Steps);
                }
            }
        }
        else if (GraphType.Equals(TEXT("material")))
        {
            TArray<TSharedPtr<FJsonValue>> PinHints;
            if (OpId.Equals(TEXT("mat.math.add")) || OpId.Equals(TEXT("mat.math.multiply")))
            {
                TSharedPtr<FJsonObject> AHint = MakeShared<FJsonObject>();
                AHint->SetStringField(TEXT("role"), TEXT("input_a"));
                AHint->SetStringField(TEXT("pinName"), TEXT("A"));
                PinHints.Add(MakeShared<FJsonValueObject>(AHint));

                TSharedPtr<FJsonObject> BHint = MakeShared<FJsonObject>();
                BHint->SetStringField(TEXT("role"), TEXT("input_b"));
                BHint->SetStringField(TEXT("pinName"), TEXT("B"));
                PinHints.Add(MakeShared<FJsonValueObject>(BHint));

                TSharedPtr<FJsonObject> OutHint = MakeShared<FJsonObject>();
                OutHint->SetStringField(TEXT("role"), TEXT("output"));
                OutHint->SetStringField(TEXT("pinName"), TEXT(""));
                PinHints.Add(MakeShared<FJsonValueObject>(OutHint));
            }
            else if (OpId.Equals(TEXT("mat.math.lerp")))
            {
                TSharedPtr<FJsonObject> AHint = MakeShared<FJsonObject>();
                AHint->SetStringField(TEXT("role"), TEXT("input_a"));
                AHint->SetStringField(TEXT("pinName"), TEXT("A"));
                PinHints.Add(MakeShared<FJsonValueObject>(AHint));

                TSharedPtr<FJsonObject> BHint = MakeShared<FJsonObject>();
                BHint->SetStringField(TEXT("role"), TEXT("input_b"));
                BHint->SetStringField(TEXT("pinName"), TEXT("B"));
                PinHints.Add(MakeShared<FJsonValueObject>(BHint));

                TSharedPtr<FJsonObject> AlphaHint = MakeShared<FJsonObject>();
                AlphaHint->SetStringField(TEXT("role"), TEXT("alpha"));
                AlphaHint->SetStringField(TEXT("pinName"), TEXT("Alpha"));
                PinHints.Add(MakeShared<FJsonValueObject>(AlphaHint));

                TSharedPtr<FJsonObject> OutHint = MakeShared<FJsonObject>();
                OutHint->SetStringField(TEXT("role"), TEXT("output"));
                OutHint->SetStringField(TEXT("pinName"), TEXT(""));
                PinHints.Add(MakeShared<FJsonValueObject>(OutHint));
            }
            else if (OpId.Equals(TEXT("mat.math.one_minus")) || OpId.Equals(TEXT("mat.math.saturate")))
            {
                TSharedPtr<FJsonObject> InputHint = MakeShared<FJsonObject>();
                InputHint->SetStringField(TEXT("role"), TEXT("input"));
                InputHint->SetStringField(TEXT("pinName"), TEXT("Input"));
                PinHints.Add(MakeShared<FJsonValueObject>(InputHint));

                TSharedPtr<FJsonObject> OutHint = MakeShared<FJsonObject>();
                OutHint->SetStringField(TEXT("role"), TEXT("output"));
                OutHint->SetStringField(TEXT("pinName"), TEXT(""));
                PinHints.Add(MakeShared<FJsonValueObject>(OutHint));
            }
            else if (OpId.Equals(TEXT("mat.texture.sample")))
            {
                TSharedPtr<FJsonObject> UvHint = MakeShared<FJsonObject>();
                UvHint->SetStringField(TEXT("role"), TEXT("uv_input"));
                UvHint->SetStringField(TEXT("pinName"), TEXT("UVs"));
                PinHints.Add(MakeShared<FJsonValueObject>(UvHint));

                TSharedPtr<FJsonObject> RgbHint = MakeShared<FJsonObject>();
                RgbHint->SetStringField(TEXT("role"), TEXT("rgb_output"));
                RgbHint->SetStringField(TEXT("pinName"), TEXT("RGB"));
                PinHints.Add(MakeShared<FJsonValueObject>(RgbHint));
            }
            else if (OpId.Equals(TEXT("mat.param.texture")))
            {
                ArgsObj->SetStringField(TEXT("parameterName"), TEXT("TextureParam"));

                TSharedPtr<FJsonObject> UvHint = MakeShared<FJsonObject>();
                UvHint->SetStringField(TEXT("role"), TEXT("uv_input"));
                UvHint->SetStringField(TEXT("pinName"), TEXT("UVs"));
                PinHints.Add(MakeShared<FJsonValueObject>(UvHint));

                TSharedPtr<FJsonObject> RgbHint = MakeShared<FJsonObject>();
                RgbHint->SetStringField(TEXT("role"), TEXT("rgb_output"));
                RgbHint->SetStringField(TEXT("pinName"), TEXT("RGB"));
                PinHints.Add(MakeShared<FJsonValueObject>(RgbHint));

                TSharedPtr<FJsonObject> SettingsTemplate = MakeShared<FJsonObject>();
                SettingsTemplate->SetStringField(TEXT("parameterName"), TEXT("TextureParam"));
                PreferredPlan->SetObjectField(TEXT("settingsTemplate"), SettingsTemplate);
            }
            else if (OpId.Equals(TEXT("mat.func.call")))
            {
                TSharedPtr<FJsonObject> SettingsTemplate = MakeShared<FJsonObject>();
                SettingsTemplate->SetStringField(TEXT("functionAssetPath"), TEXT("/Game/Functions/MF_Example"));
                PreferredPlan->SetObjectField(TEXT("settingsTemplate"), SettingsTemplate);
                if (!HintFunctionAssetPath.IsEmpty())
                {
                    ArgsObj->SetStringField(TEXT("functionAssetPath"), HintFunctionAssetPath);
                }
            }

            if (PinHints.Num() > 0)
            {
                PreferredPlan->SetArrayField(TEXT("pinHints"), PinHints);
            }

            if ((OpId.Equals(TEXT("mat.math.add"))
                    || OpId.Equals(TEXT("mat.math.lerp"))
                    || OpId.Equals(TEXT("mat.math.multiply"))
                    || OpId.Equals(TEXT("mat.math.one_minus"))
                    || OpId.Equals(TEXT("mat.math.saturate")))
                && !HintTargetRootPin.IsEmpty())
            {
                const FString StepNodeRef = !ClientRef.IsEmpty()
                    ? ClientRef
                    : FString::Printf(TEXT("resolved_%d"), Index);

                TArray<TSharedPtr<FJsonValue>> Steps;

                TSharedPtr<FJsonObject> AddStep = MakeShared<FJsonObject>();
                AddStep->SetStringField(TEXT("op"), TEXT("addNode.byClass"));
                AddStep->SetStringField(TEXT("clientRef"), StepNodeRef);
                TSharedPtr<FJsonObject> AddStepArgs = MakeShared<FJsonObject>();
                AddStepArgs->SetStringField(TEXT("nodeClassPath"), Spec.NodeClassPath);
                AddStep->SetObjectField(TEXT("args"), AddStepArgs);
                Steps.Add(MakeShared<FJsonValueObject>(AddStep));

                TSharedPtr<FJsonObject> ConnectStep = MakeShared<FJsonObject>();
                ConnectStep->SetStringField(TEXT("op"), TEXT("connectPins"));
                TSharedPtr<FJsonObject> ConnectArgs = MakeShared<FJsonObject>();
                TSharedPtr<FJsonObject> FromObj = MakeShared<FJsonObject>();
                FromObj->SetStringField(TEXT("nodeRef"), StepNodeRef);
                FromObj->SetStringField(TEXT("pinName"), TEXT(""));
                TSharedPtr<FJsonObject> ToObj = MakeShared<FJsonObject>();
                ToObj->SetStringField(TEXT("nodeId"), TEXT("__material_root__"));
                ToObj->SetStringField(TEXT("pinName"), HintTargetRootPin);
                ConnectArgs->SetObjectField(TEXT("from"), FromObj);
                ConnectArgs->SetObjectField(TEXT("to"), ToObj);
                ConnectStep->SetObjectField(TEXT("args"), ConnectArgs);
                Steps.Add(MakeShared<FJsonValueObject>(ConnectStep));

                PreferredPlan->SetStringField(TEXT("realizationKind"), TEXT("expression_insert"));
                PreferredPlan->SetStringField(TEXT("coverage"), TEXT("contextual"));
                PreferredPlan->SetArrayField(TEXT("steps"), Steps);
            }
            else if ((OpId.Equals(TEXT("mat.texture.sample")) || OpId.Equals(TEXT("mat.param.texture"))) && !HintTargetRootPin.IsEmpty())
            {
                const FString StepNodeRef = !ClientRef.IsEmpty()
                    ? ClientRef
                    : FString::Printf(TEXT("resolved_%d"), Index);

                TArray<TSharedPtr<FJsonValue>> Steps;

                TSharedPtr<FJsonObject> AddStep = MakeShared<FJsonObject>();
                AddStep->SetStringField(TEXT("op"), TEXT("addNode.byClass"));
                AddStep->SetStringField(TEXT("clientRef"), StepNodeRef);
                TSharedPtr<FJsonObject> AddStepArgs = MakeShared<FJsonObject>();
                AddStepArgs->SetStringField(TEXT("nodeClassPath"), Spec.NodeClassPath);
                if (OpId.Equals(TEXT("mat.param.texture")))
                {
                    AddStepArgs->SetStringField(TEXT("parameterName"), TEXT("TextureParam"));
                }
                AddStep->SetObjectField(TEXT("args"), AddStepArgs);
                Steps.Add(MakeShared<FJsonValueObject>(AddStep));

                TSharedPtr<FJsonObject> ConnectStep = MakeShared<FJsonObject>();
                ConnectStep->SetStringField(TEXT("op"), TEXT("connectPins"));
                TSharedPtr<FJsonObject> ConnectArgs = MakeShared<FJsonObject>();
                TSharedPtr<FJsonObject> FromObj = MakeShared<FJsonObject>();
                FromObj->SetStringField(TEXT("nodeRef"), StepNodeRef);
                FromObj->SetStringField(TEXT("pinName"), TEXT("RGB"));
                TSharedPtr<FJsonObject> ToObj = MakeShared<FJsonObject>();
                ToObj->SetStringField(TEXT("nodeId"), TEXT("__material_root__"));
                ToObj->SetStringField(TEXT("pinName"), HintTargetRootPin);
                ConnectArgs->SetObjectField(TEXT("from"), FromObj);
                ConnectArgs->SetObjectField(TEXT("to"), ToObj);
                ConnectStep->SetObjectField(TEXT("args"), ConnectArgs);
                Steps.Add(MakeShared<FJsonValueObject>(ConnectStep));

                if (!FromNodeId.IsEmpty() && !FromPinName.IsEmpty())
                {
                    TSharedPtr<FJsonObject> UvStep = MakeShared<FJsonObject>();
                    UvStep->SetStringField(TEXT("op"), TEXT("connectPins"));
                    TSharedPtr<FJsonObject> UvArgs = MakeShared<FJsonObject>();
                    TSharedPtr<FJsonObject> UvFromObj = MakeShared<FJsonObject>();
                    UvFromObj->SetStringField(TEXT("nodeId"), FromNodeId);
                    UvFromObj->SetStringField(TEXT("pinName"), FromPinName);
                    TSharedPtr<FJsonObject> UvToObj = MakeShared<FJsonObject>();
                    UvToObj->SetStringField(TEXT("nodeRef"), StepNodeRef);
                    UvToObj->SetStringField(TEXT("pinName"), TEXT("UVs"));
                    UvArgs->SetObjectField(TEXT("from"), UvFromObj);
                    UvArgs->SetObjectField(TEXT("to"), UvToObj);
                    UvStep->SetObjectField(TEXT("args"), UvArgs);
                    Steps.Add(MakeShared<FJsonValueObject>(UvStep));
                }

                PreferredPlan->SetStringField(TEXT("realizationKind"), TEXT("expression_insert"));
                PreferredPlan->SetStringField(TEXT("coverage"), TEXT("contextual"));
                PreferredPlan->SetArrayField(TEXT("steps"), Steps);
            }
        }
        if (GraphType.Equals(TEXT("pcg")))
        {
            TArray<TSharedPtr<FJsonValue>> PinHints;
            TSharedPtr<FJsonObject> SettingsTemplate = MakeShared<FJsonObject>();
            bool bHasSettingsTemplate = false;

            if (OpId.Equals(TEXT("pcg.meta.add_tag"))
                || OpId.Equals(TEXT("pcg.transform.points"))
                || OpId.Equals(TEXT("pcg.spawn.actor")))
            {
                TSharedPtr<FJsonObject> InHint = MakeShared<FJsonObject>();
                InHint->SetStringField(TEXT("role"), TEXT("input"));
                InHint->SetStringField(TEXT("pinName"), TEXT("In"));
                PinHints.Add(MakeShared<FJsonValueObject>(InHint));

                TSharedPtr<FJsonObject> OutHint = MakeShared<FJsonObject>();
                OutHint->SetStringField(TEXT("role"), TEXT("output"));
                OutHint->SetStringField(TEXT("pinName"), TEXT("Out"));
                PinHints.Add(MakeShared<FJsonValueObject>(OutHint));
            }
            else if (OpId.Equals(TEXT("pcg.create.points")))
            {
                TSharedPtr<FJsonObject> OutHint = MakeShared<FJsonObject>();
                OutHint->SetStringField(TEXT("role"), TEXT("output"));
                OutHint->SetStringField(TEXT("kind"), TEXT("pin"));
                OutHint->SetStringField(TEXT("pinName"), TEXT("Out"));
                OutHint->SetStringField(TEXT("semanticRole"), TEXT("generated_points"));
                OutHint->SetBoolField(TEXT("isDefaultPath"), true);
                PinHints.Add(MakeShared<FJsonValueObject>(OutHint));
            }
            else if (OpId.Equals(TEXT("pcg.sample.surface")))
            {
                TSharedPtr<FJsonObject> SurfaceHint = MakeShared<FJsonObject>();
                SurfaceHint->SetStringField(TEXT("role"), TEXT("input_surface"));
                SurfaceHint->SetStringField(TEXT("kind"), TEXT("pin"));
                SurfaceHint->SetStringField(TEXT("pinName"), TEXT("Surface"));
                SurfaceHint->SetStringField(TEXT("semanticRole"), TEXT("surface_input"));
                PinHints.Add(MakeShared<FJsonValueObject>(SurfaceHint));

                TSharedPtr<FJsonObject> ShapeHint = MakeShared<FJsonObject>();
                ShapeHint->SetStringField(TEXT("role"), TEXT("bounding_shape"));
                ShapeHint->SetStringField(TEXT("kind"), TEXT("pin"));
                ShapeHint->SetStringField(TEXT("pinName"), TEXT("Bounding Shape"));
                ShapeHint->SetStringField(TEXT("semanticRole"), TEXT("bounds_input"));
                PinHints.Add(MakeShared<FJsonValueObject>(ShapeHint));

                TSharedPtr<FJsonObject> OutHint = MakeShared<FJsonObject>();
                OutHint->SetStringField(TEXT("role"), TEXT("output"));
                OutHint->SetStringField(TEXT("kind"), TEXT("pin"));
                OutHint->SetStringField(TEXT("pinName"), TEXT("Out"));
                OutHint->SetStringField(TEXT("semanticRole"), TEXT("sampled_points"));
                OutHint->SetBoolField(TEXT("isDefaultPath"), true);
                PinHints.Add(MakeShared<FJsonValueObject>(OutHint));
            }
            else if (OpId.Equals(TEXT("pcg.filter.by_tag")))
            {
                TSharedPtr<FJsonObject> InHint = MakeShared<FJsonObject>();
                InHint->SetStringField(TEXT("role"), TEXT("input"));
                InHint->SetStringField(TEXT("kind"), TEXT("pin"));
                InHint->SetStringField(TEXT("pinName"), TEXT("In"));
                InHint->SetStringField(TEXT("semanticRole"), TEXT("input_stage"));
                PinHints.Add(MakeShared<FJsonValueObject>(InHint));

                TSharedPtr<FJsonObject> InsideHint = MakeShared<FJsonObject>();
                InsideHint->SetStringField(TEXT("role"), TEXT("output"));
                InsideHint->SetStringField(TEXT("kind"), TEXT("pin"));
                InsideHint->SetStringField(TEXT("pinName"), TEXT("InsideFilter"));
                InsideHint->SetStringField(TEXT("semanticRole"), TEXT("primary_output"));
                InsideHint->SetBoolField(TEXT("isDefaultPath"), true);
                PinHints.Add(MakeShared<FJsonValueObject>(InsideHint));

                TSharedPtr<FJsonObject> OutsideHint = MakeShared<FJsonObject>();
                OutsideHint->SetStringField(TEXT("role"), TEXT("output"));
                OutsideHint->SetStringField(TEXT("kind"), TEXT("pin"));
                OutsideHint->SetStringField(TEXT("pinName"), TEXT("OutsideFilter"));
                OutsideHint->SetStringField(TEXT("semanticRole"), TEXT("secondary_output"));
                OutsideHint->SetBoolField(TEXT("isDefaultPath"), false);
                PinHints.Add(MakeShared<FJsonValueObject>(OutsideHint));
            }
            else if (OpId.Equals(TEXT("pcg.sample.spline")))
            {
                TSharedPtr<FJsonObject> SplineHint = MakeShared<FJsonObject>();
                SplineHint->SetStringField(TEXT("role"), TEXT("input_spline"));
                SplineHint->SetStringField(TEXT("kind"), TEXT("pin"));
                SplineHint->SetStringField(TEXT("pinName"), TEXT("Spline"));
                SplineHint->SetStringField(TEXT("semanticRole"), TEXT("spline_input"));
                PinHints.Add(MakeShared<FJsonValueObject>(SplineHint));

                TSharedPtr<FJsonObject> ShapeHint = MakeShared<FJsonObject>();
                ShapeHint->SetStringField(TEXT("role"), TEXT("bounding_shape"));
                ShapeHint->SetStringField(TEXT("kind"), TEXT("pin"));
                ShapeHint->SetStringField(TEXT("pinName"), TEXT("Bounding Shape"));
                ShapeHint->SetStringField(TEXT("semanticRole"), TEXT("bounds_input"));
                PinHints.Add(MakeShared<FJsonValueObject>(ShapeHint));

                TSharedPtr<FJsonObject> OutHint = MakeShared<FJsonObject>();
                OutHint->SetStringField(TEXT("role"), TEXT("output"));
                OutHint->SetStringField(TEXT("kind"), TEXT("pin"));
                OutHint->SetStringField(TEXT("pinName"), TEXT("Out"));
                OutHint->SetStringField(TEXT("semanticRole"), TEXT("sampled_points"));
                OutHint->SetBoolField(TEXT("isDefaultPath"), true);
                PinHints.Add(MakeShared<FJsonValueObject>(OutHint));
            }
            else if (OpId.Equals(TEXT("pcg.source.actor_data")))
            {
                TSharedPtr<FJsonObject> OutHint = MakeShared<FJsonObject>();
                OutHint->SetStringField(TEXT("role"), TEXT("output"));
                OutHint->SetStringField(TEXT("kind"), TEXT("pin"));
                OutHint->SetStringField(TEXT("pinName"), TEXT("Out"));
                OutHint->SetStringField(TEXT("semanticRole"), TEXT("actor_data"));
                OutHint->SetBoolField(TEXT("isDefaultPath"), true);
                PinHints.Add(MakeShared<FJsonValueObject>(OutHint));
            }
            else if (OpId.Equals(TEXT("pcg.project.surface")))
            {
                TSharedPtr<FJsonObject> InHint = MakeShared<FJsonObject>();
                InHint->SetStringField(TEXT("role"), TEXT("input"));
                InHint->SetStringField(TEXT("kind"), TEXT("pin"));
                InHint->SetStringField(TEXT("pinName"), TEXT("In"));
                InHint->SetStringField(TEXT("semanticRole"), TEXT("source_input"));
                PinHints.Add(MakeShared<FJsonValueObject>(InHint));

                TSharedPtr<FJsonObject> ProjectionTargetHint = MakeShared<FJsonObject>();
                ProjectionTargetHint->SetStringField(TEXT("role"), TEXT("projection_target"));
                ProjectionTargetHint->SetStringField(TEXT("kind"), TEXT("pin"));
                ProjectionTargetHint->SetStringField(TEXT("pinName"), TEXT("Projection Target"));
                ProjectionTargetHint->SetStringField(TEXT("semanticRole"), TEXT("projection_target"));
                PinHints.Add(MakeShared<FJsonValueObject>(ProjectionTargetHint));

                TSharedPtr<FJsonObject> OutHint = MakeShared<FJsonObject>();
                OutHint->SetStringField(TEXT("role"), TEXT("output"));
                OutHint->SetStringField(TEXT("kind"), TEXT("pin"));
                OutHint->SetStringField(TEXT("pinName"), TEXT("Out"));
                OutHint->SetStringField(TEXT("semanticRole"), TEXT("projected_output"));
                OutHint->SetBoolField(TEXT("isDefaultPath"), true);
                PinHints.Add(MakeShared<FJsonValueObject>(OutHint));
            }
            else if (OpId.Equals(TEXT("pcg.spawn.static_mesh")))
            {
                TSharedPtr<FJsonObject> InHint = MakeShared<FJsonObject>();
                InHint->SetStringField(TEXT("role"), TEXT("input"));
                InHint->SetStringField(TEXT("kind"), TEXT("pin"));
                InHint->SetStringField(TEXT("pinName"), TEXT("In"));
                InHint->SetStringField(TEXT("semanticRole"), TEXT("points_input"));
                PinHints.Add(MakeShared<FJsonValueObject>(InHint));

                TSharedPtr<FJsonObject> OutHint = MakeShared<FJsonObject>();
                OutHint->SetStringField(TEXT("role"), TEXT("output"));
                OutHint->SetStringField(TEXT("kind"), TEXT("pin"));
                OutHint->SetStringField(TEXT("pinName"), TEXT("Out"));
                OutHint->SetStringField(TEXT("semanticRole"), TEXT("spawned_output"));
                OutHint->SetBoolField(TEXT("isDefaultPath"), true);
                PinHints.Add(MakeShared<FJsonValueObject>(OutHint));
            }

            if (OpId.Equals(TEXT("pcg.meta.add_tag")))
            {
                SettingsTemplate->SetStringField(TEXT("tag"), TEXT("<required-tag>"));
                bHasSettingsTemplate = true;
            }
            else if (OpId.Equals(TEXT("pcg.filter.by_tag")))
            {
                SettingsTemplate->SetStringField(TEXT("tag"), TEXT("<required-tag>"));
                SettingsTemplate->SetStringField(TEXT("mode"), TEXT("include"));
                bHasSettingsTemplate = true;
            }
            else if (OpId.Equals(TEXT("pcg.transform.points")))
            {
                TSharedPtr<FJsonObject> OffsetMin = MakeShared<FJsonObject>();
                OffsetMin->SetNumberField(TEXT("x"), 0.0);
                OffsetMin->SetNumberField(TEXT("y"), 0.0);
                OffsetMin->SetNumberField(TEXT("z"), 0.0);
                TSharedPtr<FJsonObject> OffsetMax = CloneJsonObject(OffsetMin);
                TSharedPtr<FJsonObject> RotationMin = MakeShared<FJsonObject>();
                RotationMin->SetNumberField(TEXT("pitch"), 0.0);
                RotationMin->SetNumberField(TEXT("yaw"), 0.0);
                RotationMin->SetNumberField(TEXT("roll"), 0.0);
                TSharedPtr<FJsonObject> RotationMax = CloneJsonObject(RotationMin);
                TSharedPtr<FJsonObject> ScaleMin = MakeShared<FJsonObject>();
                ScaleMin->SetNumberField(TEXT("x"), 1.0);
                ScaleMin->SetNumberField(TEXT("y"), 1.0);
                ScaleMin->SetNumberField(TEXT("z"), 1.0);
                TSharedPtr<FJsonObject> ScaleMax = CloneJsonObject(ScaleMin);
                SettingsTemplate->SetObjectField(TEXT("offsetMin"), OffsetMin);
                SettingsTemplate->SetObjectField(TEXT("offsetMax"), OffsetMax);
                SettingsTemplate->SetObjectField(TEXT("rotationMin"), RotationMin);
                SettingsTemplate->SetObjectField(TEXT("rotationMax"), RotationMax);
                SettingsTemplate->SetObjectField(TEXT("scaleMin"), ScaleMin);
                SettingsTemplate->SetObjectField(TEXT("scaleMax"), ScaleMax);
                bHasSettingsTemplate = true;
            }
            else if (OpId.Equals(TEXT("pcg.sample.spline")))
            {
                TSharedPtr<FJsonObject> SamplerParams = MakeShared<FJsonObject>();
                SamplerParams->SetStringField(TEXT("dimension"), TEXT("OnSpline"));
                SamplerParams->SetStringField(TEXT("mode"), TEXT("Distance"));
                SamplerParams->SetNumberField(TEXT("distanceIncrement"), 100.0);
                SettingsTemplate->SetObjectField(TEXT("samplerParams"), SamplerParams);
                bHasSettingsTemplate = true;
            }
            else if (OpId.Equals(TEXT("pcg.source.actor_data")))
            {
                SettingsTemplate->SetStringField(TEXT("mode"), TEXT("ParseActorComponents"));
                bHasSettingsTemplate = true;
            }
            else if (OpId.Equals(TEXT("pcg.project.surface")))
            {
                TSharedPtr<FJsonObject> ProjectionParams = MakeShared<FJsonObject>();
                ProjectionParams->SetBoolField(TEXT("bProjectPositions"), true);
                ProjectionParams->SetBoolField(TEXT("bProjectRotations"), true);
                ProjectionParams->SetBoolField(TEXT("bProjectScales"), false);
                ProjectionParams->SetStringField(TEXT("colorBlendMode"), TEXT("SourceValue"));
                ProjectionParams->SetStringField(TEXT("attributeList"), TEXT(""));
                ProjectionParams->SetStringField(TEXT("attributeMode"), TEXT("ExcludeAttributes"));
                ProjectionParams->SetStringField(TEXT("attributeMergeOperation"), TEXT("TargetValue"));
                ProjectionParams->SetStringField(TEXT("tagMergeOperation"), TEXT("Source"));
                SettingsTemplate->SetObjectField(TEXT("projectionParams"), ProjectionParams);
                SettingsTemplate->SetBoolField(TEXT("bForceCollapseToPoint"), false);
                SettingsTemplate->SetBoolField(TEXT("bKeepZeroDensityPoints"), false);
                bHasSettingsTemplate = true;
            }
            else if (OpId.Equals(TEXT("pcg.spawn.static_mesh")))
            {
                SettingsTemplate->SetStringField(TEXT("meshSelectorType"), TEXT("<selector-class-or-strategy>"));
                SettingsTemplate->SetStringField(TEXT("outAttributeName"), TEXT("<optional-attribute-name>"));
                SettingsTemplate->SetBoolField(TEXT("bApplyMeshBoundsToPoints"), true);
                SettingsTemplate->SetBoolField(TEXT("bAllowDescriptorChanges"), true);
                SettingsTemplate->SetStringField(TEXT("targetActor"), TEXT("<optional-target-actor-path>"));
                SettingsTemplate->SetBoolField(TEXT("bSynchronousLoad"), false);
                SettingsTemplate->SetBoolField(TEXT("bWarnOnIdenticalSpawn"), true);
                bHasSettingsTemplate = true;
            }
            else if (OpId.Equals(TEXT("pcg.spawn.actor")))
            {
                SettingsTemplate->SetBoolField(TEXT("bSpawnByAttribute"), false);
                SettingsTemplate->SetStringField(TEXT("spawnAttribute"), TEXT("<optional-actor-class-attribute>"));
                bHasSettingsTemplate = true;
            }

            if (PinHints.Num() > 0)
            {
                PreferredPlan->SetArrayField(TEXT("pinHints"), PinHints);
            }
            if (bHasSettingsTemplate)
            {
                PreferredPlan->SetObjectField(TEXT("settingsTemplate"), SettingsTemplate);
            }

            const FString PrimaryOutputPin = GetDefaultPcgOutputPinName(OpId);
            TArray<TSharedPtr<FJsonValue>> VerificationHints;
            TSharedPtr<FJsonObject> VerificationHint = MakeShared<FJsonObject>();
            VerificationHint->SetStringField(TEXT("kind"), TEXT("readback"));
            VerificationHint->SetStringField(TEXT("message"), TEXT("Re-query the new PCG node and confirm actual pin names before wiring downstream stages."));
            VerificationHint->SetBoolField(TEXT("requiredBeforeNextStep"), true);
            VerificationHints.Add(MakeShared<FJsonValueObject>(VerificationHint));
            PreferredPlan->SetArrayField(TEXT("verificationHints"), VerificationHints);

            if ((OpId.Equals(TEXT("pcg.filter.by_tag")) || OpId.Equals(TEXT("pcg.meta.add_tag")))
                && !FromNodeId.IsEmpty() && !FromPinName.IsEmpty())
            {
                const FString StepNodeRef = ResolvePlanStepNodeRef(ClientRef, Index);

                TArray<TSharedPtr<FJsonValue>> Steps;
                TArray<TSharedPtr<FJsonValue>> ExecutionHints;
                FString EffectiveFromNodeId = FromNodeId;
                FString EffectiveFromPinName = FromPinName;
                FString EffectiveFromNodeRef;
                FString EffectiveFromKind = TEXT("nodeId");
                const bool bComposeItem = bEnablePcgCompose && IsComposablePcgSemanticOp(OpId);
                if (bComposeItem)
                {
                    EffectiveFromNodeId = ComposeSourceNodeId;
                    EffectiveFromPinName = ComposeSourcePinName;
                    EffectiveFromNodeRef = ComposeSourceNodeRef;
                    EffectiveFromKind = ComposeSourceKind;
                }

                TSharedPtr<FJsonObject> AddStep = MakeShared<FJsonObject>();
                AddStep->SetStringField(TEXT("op"), TEXT("addNode.byClass"));
                AddStep->SetStringField(TEXT("clientRef"), StepNodeRef);
                TSharedPtr<FJsonObject> AddStepArgs = MakeShared<FJsonObject>();
                AddStepArgs->SetStringField(TEXT("nodeClassPath"), Spec.NodeClassPath);
                AddStep->SetObjectField(TEXT("args"), AddStepArgs);
                Steps.Add(MakeShared<FJsonValueObject>(AddStep));

                const TCHAR* InputPinName = GetDefaultPcgInputPinName(OpId);
                if (!FString(InputPinName).IsEmpty())
                {
                    TSharedPtr<FJsonObject> ConnectStep = MakeShared<FJsonObject>();
                    ConnectStep->SetStringField(TEXT("op"), TEXT("connectPins"));
                    TSharedPtr<FJsonObject> ConnectArgs = MakeShared<FJsonObject>();
                    TSharedPtr<FJsonObject> FromObj = MakeShared<FJsonObject>();
                    if (EffectiveFromKind.Equals(TEXT("nodeRef")) && !EffectiveFromNodeRef.IsEmpty())
                    {
                        FromObj->SetStringField(TEXT("nodeRef"), EffectiveFromNodeRef);
                    }
                    else
                    {
                        FromObj->SetStringField(TEXT("nodeId"), EffectiveFromNodeId);
                    }
                    FromObj->SetStringField(TEXT("pinName"), EffectiveFromPinName);
                    TSharedPtr<FJsonObject> ToObj = MakeShared<FJsonObject>();
                    ToObj->SetStringField(TEXT("nodeRef"), StepNodeRef);
                    ToObj->SetStringField(TEXT("pinName"), InputPinName);
                    ConnectArgs->SetObjectField(TEXT("from"), FromObj);
                    ConnectArgs->SetObjectField(TEXT("to"), ToObj);
                    ConnectStep->SetObjectField(TEXT("args"), ConnectArgs);
                    Steps.Add(MakeShared<FJsonValueObject>(ConnectStep));
                }

                const bool bShouldReconnectDownstream = !ToNodeId.IsEmpty()
                    && !ToPinName.IsEmpty()
                    && !PrimaryOutputPin.IsEmpty()
                    && (!bComposeItem || Index == Items->Num() - 1);
                if (bShouldReconnectDownstream)
                {
                    TSharedPtr<FJsonObject> DisconnectStep = MakeShared<FJsonObject>();
                    DisconnectStep->SetStringField(TEXT("op"), TEXT("disconnectPins"));
                    TSharedPtr<FJsonObject> DisconnectArgs = MakeShared<FJsonObject>();
                    TSharedPtr<FJsonObject> DisconnectFromObj = MakeShared<FJsonObject>();
                    DisconnectFromObj->SetStringField(TEXT("nodeId"), FromNodeId);
                    DisconnectFromObj->SetStringField(TEXT("pinName"), FromPinName);
                    TSharedPtr<FJsonObject> DisconnectToObj = MakeShared<FJsonObject>();
                    DisconnectToObj->SetStringField(TEXT("nodeId"), ToNodeId);
                    DisconnectToObj->SetStringField(TEXT("pinName"), ToPinName);
                    DisconnectArgs->SetObjectField(TEXT("from"), DisconnectFromObj);
                    DisconnectArgs->SetObjectField(TEXT("to"), DisconnectToObj);
                    DisconnectStep->SetObjectField(TEXT("args"), DisconnectArgs);
                    Steps.Add(MakeShared<FJsonValueObject>(DisconnectStep));

                    TSharedPtr<FJsonObject> ReconnectStep = MakeShared<FJsonObject>();
                    ReconnectStep->SetStringField(TEXT("op"), TEXT("connectPins"));
                    TSharedPtr<FJsonObject> ReconnectArgs = MakeShared<FJsonObject>();
                    TSharedPtr<FJsonObject> ReconnectFromObj = MakeShared<FJsonObject>();
                    ReconnectFromObj->SetStringField(TEXT("nodeRef"), StepNodeRef);
                    ReconnectFromObj->SetStringField(TEXT("pinName"), PrimaryOutputPin);
                    TSharedPtr<FJsonObject> ReconnectToObj = MakeShared<FJsonObject>();
                    ReconnectToObj->SetStringField(TEXT("nodeId"), ToNodeId);
                    ReconnectToObj->SetStringField(TEXT("pinName"), ToPinName);
                    ReconnectArgs->SetObjectField(TEXT("from"), ReconnectFromObj);
                    ReconnectArgs->SetObjectField(TEXT("to"), ReconnectToObj);
                    ReconnectStep->SetObjectField(TEXT("args"), ReconnectArgs);
                    Steps.Add(MakeShared<FJsonValueObject>(ReconnectStep));

                    TSharedPtr<FJsonObject> ExecutionHint = MakeShared<FJsonObject>();
                    ExecutionHint->SetStringField(TEXT("kind"), TEXT("pipeline_insert"));
                    ExecutionHint->SetBoolField(TEXT("preserveUpstream"), true);
                    ExecutionHint->SetBoolField(TEXT("preserveDownstream"), true);
                    ExecutionHint->SetStringField(TEXT("composeMode"), bComposeItem ? TEXT("pipeline_segment") : TEXT("independent"));
                    if (bComposeItem)
                    {
                        ExecutionHint->SetNumberField(TEXT("segmentIndex"), Index);
                        ExecutionHint->SetNumberField(TEXT("segmentLength"), Items->Num());
                    }
                    ExecutionHints.Add(MakeShared<FJsonValueObject>(ExecutionHint));

                    VerificationHint->SetStringField(TEXT("targetClientRef"), StepNodeRef);
                    PreferredPlan->SetStringField(TEXT("realizationKind"), TEXT("pipeline_insert"));
                }
                else
                {
                    TSharedPtr<FJsonObject> ExecutionHint = MakeShared<FJsonObject>();
                    ExecutionHint->SetStringField(TEXT("kind"), bComposeItem ? TEXT("pipeline_compose") : TEXT("attach_to_upstream"));
                    ExecutionHint->SetBoolField(TEXT("preserveUpstream"), true);
                    ExecutionHint->SetBoolField(TEXT("preserveDownstream"), false);
                    ExecutionHint->SetStringField(TEXT("composeMode"), bComposeItem ? TEXT("pipeline_segment") : TEXT("independent"));
                    if (bComposeItem)
                    {
                        ExecutionHint->SetNumberField(TEXT("segmentIndex"), Index);
                        ExecutionHint->SetNumberField(TEXT("segmentLength"), Items->Num());
                    }
                    ExecutionHints.Add(MakeShared<FJsonValueObject>(ExecutionHint));

                    PreferredPlan->SetStringField(TEXT("realizationKind"), bComposeItem ? TEXT("pipeline_compose") : TEXT("pipeline_extend"));
                }

                PreferredPlan->SetStringField(TEXT("coverage"), TEXT("contextual"));
                PreferredPlan->SetArrayField(TEXT("steps"), Steps);
                PreferredPlan->SetArrayField(TEXT("executionHints"), ExecutionHints);

                if (bComposeItem && !PrimaryOutputPin.IsEmpty())
                {
                    ComposeSourceNodeId.Empty();
                    ComposeSourceNodeRef = StepNodeRef;
                    ComposeSourcePinName = PrimaryOutputPin;
                    ComposeSourceKind = TEXT("nodeRef");
                }
            }
        }

        ResultItem->SetBoolField(TEXT("resolved"), true);
        ResultItem->SetObjectField(TEXT("compatibility"), Compatibility);
        ResultItem->SetObjectField(TEXT("preferredPlan"), PreferredPlan);
        ResultItem->SetArrayField(TEXT("alternatives"), TArray<TSharedPtr<FJsonValue>>{});
        Results.Add(MakeShared<FJsonValueObject>(ResultItem));
    }

    Result->SetStringField(TEXT("graphType"), QueryResult->GetStringField(TEXT("graphType")));
    const TSharedPtr<FJsonObject>* NormalizedGraphRef = nullptr;
    if (QueryResult->TryGetObjectField(TEXT("graphRef"), NormalizedGraphRef) && NormalizedGraphRef != nullptr && (*NormalizedGraphRef).IsValid())
    {
        Result->SetObjectField(TEXT("graphRef"), CloneJsonObject(*NormalizedGraphRef));
    }
    else
    {
        Result->SetObjectField(TEXT("graphRef"), CloneJsonObject(*GraphRefObj));
    }

    const TArray<TSharedPtr<FJsonValue>>* QueryDiagnostics = nullptr;
    if (QueryResult->TryGetArrayField(TEXT("diagnostics"), QueryDiagnostics) && QueryDiagnostics != nullptr)
    {
        Result->SetArrayField(TEXT("diagnostics"), *QueryDiagnostics);
    }
    else
    {
        Result->SetArrayField(TEXT("diagnostics"), TArray<TSharedPtr<FJsonValue>>{});
    }
    Result->SetArrayField(TEXT("results"), Results);
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
        Result->SetStringField(TEXT("message"), TEXT("Supported graphType values: blueprint, material, pcg."));
        return Result;
    }

    auto ResolveInlineBlueprintGraphName = [](const FString& InAssetPath, const FString& InNodeGuid, FString& OutGraphName, FString& OutError) -> bool
    {
        FString NodesJson;
        OutGraphName.Empty();
        OutError.Empty();
        return FLoomleBlueprintAdapter::ListCompositeSubgraphNodes(InAssetPath, InNodeGuid, OutGraphName, NodesJson, OutError)
            && !OutGraphName.IsEmpty();
    };

    FString AssetPath;
    FString GraphName = GraphType.Equals(TEXT("blueprint"))
        ? TEXT("EventGraph")
        : (GraphType.Equals(TEXT("pcg")) ? TEXT("PCGGraph") : TEXT("MaterialGraph"));
    FString InlineNodeGuid;
    bool bUsedGraphRef = false;

    const TSharedPtr<FJsonObject>* GraphRefObj = nullptr;
    const bool bHasGraphRef = Arguments->TryGetObjectField(TEXT("graphRef"), GraphRefObj) && GraphRefObj && (*GraphRefObj).IsValid();
    const bool bHasGraphName = Arguments->TryGetStringField(TEXT("graphName"), GraphName) && !GraphName.IsEmpty();

    if (bHasGraphRef && bHasGraphName)
    {
        Result->SetBoolField(TEXT("isError"), true);
        Result->SetStringField(TEXT("code"), TEXT("INVALID_ARGUMENT"));
        Result->SetStringField(TEXT("message"), TEXT("Supply either graphRef or graphName, not both."));
        return Result;
    }

    if (bHasGraphRef)
    {
        bUsedGraphRef = true;
        FString Kind;
        if (!(*GraphRefObj)->TryGetStringField(TEXT("kind"), Kind) || Kind.IsEmpty())
        {
            Result->SetBoolField(TEXT("isError"), true);
            Result->SetStringField(TEXT("code"), TEXT("GRAPH_REF_INVALID"));
            Result->SetStringField(TEXT("message"), TEXT("graphRef.kind is required."));
            return Result;
        }
        Kind = Kind.ToLower();

        FString RefAssetPath;
        if (!(*GraphRefObj)->TryGetStringField(TEXT("assetPath"), RefAssetPath) || RefAssetPath.IsEmpty())
        {
            Result->SetBoolField(TEXT("isError"), true);
            Result->SetStringField(TEXT("code"), TEXT("GRAPH_REF_INVALID"));
            Result->SetStringField(TEXT("message"), TEXT("graphRef.assetPath is required."));
            return Result;
        }
        AssetPath = NormalizeAssetPath(RefAssetPath);

        if (Kind.Equals(TEXT("asset")))
        {
            (*GraphRefObj)->TryGetStringField(TEXT("graphName"), GraphName);
        }
        else if (Kind.Equals(TEXT("inline")))
        {
            if (!GraphType.Equals(TEXT("blueprint")))
            {
                Result->SetBoolField(TEXT("isError"), true);
                Result->SetStringField(TEXT("code"), TEXT("GRAPH_REF_INVALID"));
                Result->SetStringField(TEXT("message"), TEXT("graphRef.kind=inline is only supported for blueprint graphType."));
                return Result;
            }
            if (!(*GraphRefObj)->TryGetStringField(TEXT("nodeGuid"), InlineNodeGuid) || InlineNodeGuid.IsEmpty())
            {
                Result->SetBoolField(TEXT("isError"), true);
                Result->SetStringField(TEXT("code"), TEXT("GRAPH_REF_INVALID"));
                Result->SetStringField(TEXT("message"), TEXT("graphRef.nodeGuid is required for kind=inline."));
                return Result;
            }

            FString ResolvedGraphName;
            FString ResolveError;
            if (!ResolveInlineBlueprintGraphName(AssetPath, InlineNodeGuid, ResolvedGraphName, ResolveError))
            {
                Result->SetBoolField(TEXT("isError"), true);
                Result->SetStringField(TEXT("code"), TEXT("GRAPH_NOT_FOUND"));
                Result->SetStringField(TEXT("message"), ResolveError.IsEmpty() ? TEXT("Failed to resolve inline graphRef.") : ResolveError);
                return Result;
            }
            GraphName = ResolvedGraphName;
        }
        else
        {
            Result->SetBoolField(TEXT("isError"), true);
            Result->SetStringField(TEXT("code"), TEXT("GRAPH_REF_INVALID"));
            Result->SetStringField(TEXT("message"), FString::Printf(TEXT("Unsupported graphRef.kind: %s"), *Kind));
            return Result;
        }
    }
    else
    {
        if (!Arguments->TryGetStringField(TEXT("assetPath"), AssetPath) || AssetPath.IsEmpty())
        {
            Result->SetBoolField(TEXT("isError"), true);
            Result->SetStringField(TEXT("code"), TEXT("INVALID_ARGUMENT"));
            Result->SetStringField(TEXT("message"), TEXT("arguments.assetPath is required (or supply graphRef)."));
            return Result;
        }
        AssetPath = NormalizeAssetPath(AssetPath);
    }

    bool bDryRun = false;
    Arguments->TryGetBoolField(TEXT("dryRun"), bDryRun);

    auto BuildMutateRevisionQueryArgs = [&]() -> TSharedPtr<FJsonObject>
    {
        TSharedPtr<FJsonObject> QueryArgs = MakeShared<FJsonObject>();
        QueryArgs->SetStringField(TEXT("graphType"), GraphType);
        if (bUsedGraphRef)
        {
            TSharedPtr<FJsonObject> QueryGraphRef = MakeShared<FJsonObject>();
            QueryGraphRef->SetStringField(TEXT("assetPath"), AssetPath);
            if (!InlineNodeGuid.IsEmpty())
            {
                QueryGraphRef->SetStringField(TEXT("kind"), TEXT("inline"));
                QueryGraphRef->SetStringField(TEXT("nodeGuid"), InlineNodeGuid);
            }
            else
            {
                QueryGraphRef->SetStringField(TEXT("kind"), TEXT("asset"));
                if (!GraphName.IsEmpty())
                {
                    QueryGraphRef->SetStringField(TEXT("graphName"), GraphName);
                }
            }
            QueryArgs->SetObjectField(TEXT("graphRef"), QueryGraphRef);
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
        if (ErrorLower.Contains(TEXT("node not found")) || ErrorLower.Contains(TEXT("pin not found")))
        {
            return TEXT("TARGET_NOT_FOUND");
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
        bool bAnyErrorLocal = false;
        bool bAnyChangedLocal = false;
        FString FirstErrorLocal;
        FString FirstErrorCodeLocal;

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
            if (Obj->TryGetStringField(TEXT("nodePath"), OutNodeId) && !OutNodeId.IsEmpty())
            {
                return true;
            }
            if (Obj->TryGetStringField(TEXT("path"), OutNodeId) && !OutNodeId.IsEmpty())
            {
                return true;
            }
            if (Obj->TryGetStringField(TEXT("nodeName"), OutNodeId) && !OutNodeId.IsEmpty())
            {
                return true;
            }
            if (Obj->TryGetStringField(TEXT("name"), OutNodeId) && !OutNodeId.IsEmpty())
            {
                return true;
            }
            return false;
        };

        auto ResolveNodeTokenFromArgsLocal = [&ResolveNodeTokenLocal](const TSharedPtr<FJsonObject>& ArgsObj, FString& OutNodeId) -> bool
        {
            OutNodeId.Empty();
            if (!ArgsObj.IsValid())
            {
                return false;
            }

            const TSharedPtr<FJsonObject>* TargetObj = nullptr;
            if (ArgsObj->TryGetObjectField(TEXT("target"), TargetObj) && TargetObj && (*TargetObj).IsValid()
                && ResolveNodeTokenLocal(*TargetObj, OutNodeId))
            {
                return true;
            }

            return ResolveNodeTokenLocal(ArgsObj, OutNodeId);
        };

        auto ResolveNodeTokenArrayFromArgsLocal = [&ResolveNodeTokenLocal](const TSharedPtr<FJsonObject>& ArgsObj, TArray<FString>& OutNodeIds) -> bool
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
                        && ResolveNodeTokenLocal(*NodeObj, NodeId) && !NodeId.IsEmpty())
                    {
                        OutNodeIds.Add(NodeId);
                    }
                }
            }

            return OutNodeIds.Num() > 0;
        };

        auto ResolvePinName = [](const TSharedPtr<FJsonObject>& Obj, FString& OutPinName) -> bool
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
        };

        auto ResolveValueStringLocal = [](const TSharedPtr<FJsonObject>& Obj, FString& OutValue) -> bool
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

        auto HasExplicitPositionLocal = [](const TSharedPtr<FJsonObject>& Obj) -> bool
        {
            return Obj.IsValid() && Obj->HasTypedField<EJson::Object>(TEXT("position"));
        };

        auto GetDeltaFromObjectLocal = [](const TSharedPtr<FJsonObject>& Obj, int32& OutDx, int32& OutDy)
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
        auto AddUniqueMaterialPinCandidate = [](TArray<FString>& Candidates, const FString& Candidate)
        {
            for (const FString& Existing : Candidates)
            {
                if (Existing.Equals(Candidate, ESearchCase::IgnoreCase))
                {
                    return;
                }
            }
            Candidates.Add(Candidate);
        };
        auto TrimMaterialPinName = [](const FString& PinName) -> FString
        {
            return PinName.TrimStartAndEnd();
        };
        auto StripMaterialPinDisplaySuffix = [&](const FString& PinName) -> FString
        {
            const FString Trimmed = TrimMaterialPinName(PinName);
            int32 OpenParenIndex = INDEX_NONE;
            if (Trimmed.EndsWith(TEXT(")")) && Trimmed.FindLastChar(TEXT('('), OpenParenIndex) && OpenParenIndex > 0)
            {
                const FString Prefix = Trimmed.Left(OpenParenIndex).TrimEnd();
                if (!Prefix.IsEmpty())
                {
                    return Prefix;
                }
            }
            return Trimmed;
        };
        auto CanonicalizeMaterialOutputPinName = [&](const FString& PinName) -> FString
        {
            const FString Trimmed = TrimMaterialPinName(PinName);
            if (Trimmed.IsEmpty()
                || Trimmed.Equals(TEXT("None"), ESearchCase::IgnoreCase)
                || Trimmed.Equals(TEXT("Output"), ESearchCase::IgnoreCase))
            {
                return TEXT("");
            }
            return StripMaterialPinDisplaySuffix(Trimmed);
        };
        auto BuildMaterialOutputPinCandidates = [&](const FString& PinName) -> TArray<FString>
        {
            TArray<FString> Candidates;
            AddUniqueMaterialPinCandidate(Candidates, CanonicalizeMaterialOutputPinName(PinName));
            AddUniqueMaterialPinCandidate(Candidates, TEXT(""));
            return Candidates;
        };
        auto BuildMaterialInputPinCandidates = [&](UMaterialExpression* Expression, const FString& PinName) -> TArray<FString>
        {
            TArray<FString> Candidates;
            const FString Trimmed = TrimMaterialPinName(PinName);
            const FString Stripped = StripMaterialPinDisplaySuffix(Trimmed);
            AddUniqueMaterialPinCandidate(Candidates, Trimmed);
            AddUniqueMaterialPinCandidate(Candidates, Stripped);

            int32 InputCount = 0;
            if (Expression != nullptr)
            {
                const int32 MaxInputs = 128;
                for (int32 InputIndex = 0; InputIndex < MaxInputs; ++InputIndex)
                {
                    FExpressionInput* Input = Expression->GetInput(InputIndex);
                    if (Input == nullptr)
                    {
                        break;
                    }

                    ++InputCount;
                    const FString DisplayName = TrimMaterialPinName(Expression->GetInputName(InputIndex).ToString());
                    const FString DisplayNameStripped = StripMaterialPinDisplaySuffix(DisplayName);
                    const FString RawName = TrimMaterialPinName(Input->InputName.ToString());
                    const FString RawNameStripped = StripMaterialPinDisplaySuffix(RawName);

                    const bool bMatchesDisplay =
                        (!Trimmed.IsEmpty() && DisplayName.Equals(Trimmed, ESearchCase::IgnoreCase))
                        || (!Stripped.IsEmpty() && DisplayName.Equals(Stripped, ESearchCase::IgnoreCase))
                        || (!Trimmed.IsEmpty() && DisplayNameStripped.Equals(Trimmed, ESearchCase::IgnoreCase))
                        || (!Stripped.IsEmpty() && DisplayNameStripped.Equals(Stripped, ESearchCase::IgnoreCase));

                    const bool bMatchesRaw =
                        (!Trimmed.IsEmpty() && RawName.Equals(Trimmed, ESearchCase::IgnoreCase))
                        || (!Stripped.IsEmpty() && RawName.Equals(Stripped, ESearchCase::IgnoreCase))
                        || (!Trimmed.IsEmpty() && RawNameStripped.Equals(Trimmed, ESearchCase::IgnoreCase))
                        || (!Stripped.IsEmpty() && RawNameStripped.Equals(Stripped, ESearchCase::IgnoreCase));

                    if (bMatchesDisplay || bMatchesRaw)
                    {
                        AddUniqueMaterialPinCandidate(Candidates, RawName);
                        AddUniqueMaterialPinCandidate(Candidates, RawNameStripped);
                        AddUniqueMaterialPinCandidate(Candidates, DisplayName);
                        AddUniqueMaterialPinCandidate(Candidates, DisplayNameStripped);
                    }
                }
            }

            if (InputCount == 1)
            {
                AddUniqueMaterialPinCandidate(Candidates, TEXT(""));
            }

            return Candidates;
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

                const auto CanonicalizeMaterialSourcePinNameLocal = [](const FString& PinName) -> FString
                {
                    FString Normalized = PinName.TrimStartAndEnd();
                    if (Normalized.IsEmpty()
                        || Normalized.Equals(TEXT("None"), ESearchCase::IgnoreCase)
                        || Normalized.Equals(TEXT("Output"), ESearchCase::IgnoreCase))
                    {
                        return TEXT("__default__");
                    }
                    return Normalized.ToLower();
                };

                return CanonicalizeMaterialSourcePinNameLocal(Input->InputName.ToString())
                    == CanonicalizeMaterialSourcePinNameLocal(SourcePinName);
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
            else if (!bDryRun)
            {
                if (GraphType.Equals(TEXT("material")))
                {
                    if (Op.Equals(TEXT("addnode.byclass")))
                    {
                        FString NodeClassPath;
                        ArgsObj->TryGetStringField(TEXT("nodeClassPath"), NodeClassPath);

                        UClass* ExpressionClass = LoadObject<UClass>(nullptr, *NodeClassPath);
                        if (bOk && (ExpressionClass == nullptr || !ExpressionClass->IsChildOf(UMaterialExpression::StaticClass())))
                        {
                            bOk = false;
                            Error = TEXT("Invalid material expression class.");
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
                                            bOk = false;
                                            Error = TEXT("Failed to resolve material function asset.");
                                        }
                                        else
                                        {
                                            FunctionCall->UpdateFromFunctionResource();
                                        }
                                    }
                                }
                            }

                            if (bOk)
                            {
                                NodeId = MaterialExpressionId(NewExpression);
                                bChanged = true;
                                GraphEventName = TEXT("graph.node_added");
                                GraphEventData->SetStringField(TEXT("nodeId"), NodeId);
                                GraphEventData->SetStringField(TEXT("nodeType"), TEXT("by_class"));
                                GraphEventData->SetStringField(TEXT("nodeClassPath"), NodeClassPath);
                                GraphEventData->SetStringField(TEXT("op"), Op);
                                NodesTouchedForLayout.Add(NodeId);
                            }
                        }
                    }
                    else if (Op.Equals(TEXT("removenode")))
                    {
                        FString TargetNodeId;
                        bOk = ResolveNodeTokenFromArgsLocal(ArgsObj, TargetNodeId);
                        if (!bOk)
                        {
                            Error = TEXT("Missing target nodeId/path/name.");
                        }
                        else if (UMaterialExpression* Expression = FindMaterialExpressionById(MaterialAsset, TargetNodeId))
                        {
                            UMaterialEditingLibrary::DeleteMaterialExpression(MaterialAsset, Expression);
                            bChanged = true;
                            GraphEventName = TEXT("graph.node_removed");
                            GraphEventData->SetStringField(TEXT("nodeId"), TargetNodeId);
                            GraphEventData->SetStringField(TEXT("op"), Op);
                            NodesTouchedForLayout.Add(TargetNodeId);
                        }
                        else
                        {
                            bOk = false;
                            Error = TEXT("Material expression not found.");
                        }
                    }
                    else if (Op.Equals(TEXT("movenode")) || Op.Equals(TEXT("movenodeby")))
                    {
                        FString TargetNodeId;
                        bOk = ResolveNodeTokenFromArgsLocal(ArgsObj, TargetNodeId);
                        if (!bOk)
                        {
                            Error = FString::Printf(TEXT("Missing target nodeId/path/name for %s."), *Op);
                        }
                        else if (UMaterialExpression* Expression = FindMaterialExpressionById(MaterialAsset, TargetNodeId))
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
                            bChanged = true;
                            GraphEventName = TEXT("graph.node_moved");
                            GraphEventData->SetStringField(TEXT("nodeId"), TargetNodeId);
                            GraphEventData->SetNumberField(TEXT("x"), X);
                            GraphEventData->SetNumberField(TEXT("y"), Y);
                            GraphEventData->SetStringField(TEXT("op"), Op);
                            NodesTouchedForLayout.Add(TargetNodeId);
                        }
                        else
                        {
                            bOk = false;
                            Error = TEXT("Material expression not found.");
                        }
                    }
                    else if (Op.Equals(TEXT("movenodes")))
                    {
                        TArray<FString> TargetNodeIds;
                        bOk = ResolveNodeTokenArrayFromArgsLocal(ArgsObj, TargetNodeIds);
                        if (!bOk)
                        {
                            Error = TEXT("Missing nodeIds/nodes for moveNodes.");
                        }
                        else
                        {
                            int32 Dx = 0;
                            int32 Dy = 0;
                            GetDeltaFromObjectLocal(ArgsObj, Dx, Dy);

                            TArray<TSharedPtr<FJsonValue>> MovedNodeIds;
                            for (const FString& TargetNodeId : TargetNodeIds)
                            {
                                UMaterialExpression* Expression = FindMaterialExpressionById(MaterialAsset, TargetNodeId);
                                if (Expression == nullptr)
                                {
                                    bOk = false;
                                    Error = FString::Printf(TEXT("Material expression not found: %s"), *TargetNodeId);
                                    break;
                                }

                                Expression->MaterialExpressionEditorX += Dx;
                                Expression->MaterialExpressionEditorY += Dy;
                                MovedNodeIds.Add(MakeShared<FJsonValueString>(TargetNodeId));
                            }

                            if (bOk)
                            {
                                bChanged = MovedNodeIds.Num() > 0;
                                GraphEventName = TEXT("graph.node_moved");
                                GraphEventData->SetArrayField(TEXT("nodeIds"), MovedNodeIds);
                                GraphEventData->SetNumberField(TEXT("dx"), Dx);
                                GraphEventData->SetNumberField(TEXT("dy"), Dy);
                                GraphEventData->SetStringField(TEXT("op"), Op);
                                for (const TSharedPtr<FJsonValue>& MovedNodeId : MovedNodeIds)
                                {
                                    FString MovedNodeIdString;
                                    if (MovedNodeId.IsValid() && MovedNodeId->TryGetString(MovedNodeIdString))
                                    {
                                        NodesTouchedForLayout.Add(MovedNodeIdString);
                                    }
                                }
                            }
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
                            ResolvePinName(*FromObj, FromPinName);
                            ResolvePinName(*ToObj, ToPinName);
                            UMaterialExpression* FromExpr = FindMaterialExpressionById(MaterialAsset, FromNodeId);
                            UMaterialExpression* ToExpr = FindMaterialExpressionById(MaterialAsset, ToNodeId);
                            if (FromExpr == nullptr)
                            {
                                bOk = false;
                                Error = TEXT("Material expression not found.");
                            }
                            else if (ToNodeId.Equals(TEXT("__material_root__")))
                            {
                                EMaterialProperty Property = MP_MAX;
                                if (!ResolveMaterialPropertyByRootPinName(ToPinName, Property))
                                {
                                    bOk = false;
                                    Error = TEXT("Material root pin not found.");
                                }
                                else
                                {
                                    const TArray<FString> SourcePinCandidates = BuildMaterialOutputPinCandidates(FromPinName);
                                    FString EffectiveFromPinName = FromPinName;
                                    bOk = false;
                                    for (const FString& SourcePinCandidate : SourcePinCandidates)
                                    {
                                        if (UMaterialEditingLibrary::ConnectMaterialProperty(FromExpr, SourcePinCandidate, Property))
                                        {
                                            EffectiveFromPinName = SourcePinCandidate;
                                            bOk = true;
                                            break;
                                        }
                                    }

                                    if (!bOk)
                                    {
                                        Error = TEXT("Failed to connect material expression to material root.");
                                    }
                                    else
                                    {
                                        bChanged = true;
                                        GraphEventName = TEXT("graph.node_connected");
                                        GraphEventData->SetStringField(TEXT("fromNodeId"), FromNodeId);
                                        GraphEventData->SetStringField(TEXT("fromPin"), EffectiveFromPinName);
                                        GraphEventData->SetStringField(TEXT("toNodeId"), ToNodeId);
                                        GraphEventData->SetStringField(TEXT("toPin"), ToPinName);
                                        GraphEventData->SetStringField(TEXT("op"), Op);
                                        NodesTouchedForLayout.Add(FromNodeId);
                                    }
                                }
                            }
                            else if (ToExpr == nullptr)
                            {
                                bOk = false;
                                Error = TEXT("Material expression not found.");
                            }
                            else
                            {
                                const TArray<FString> SourcePinCandidates = BuildMaterialOutputPinCandidates(FromPinName);
                                const TArray<FString> TargetPinCandidates = BuildMaterialInputPinCandidates(ToExpr, ToPinName);
                                FString EffectiveFromPinName = FromPinName;
                                FString EffectiveToPinName = ToPinName;
                                bOk = false;
                                for (const FString& SourcePinCandidate : SourcePinCandidates)
                                {
                                    for (const FString& TargetPinCandidate : TargetPinCandidates)
                                    {
                                        if (UMaterialEditingLibrary::ConnectMaterialExpressions(FromExpr, SourcePinCandidate, ToExpr, TargetPinCandidate))
                                        {
                                            EffectiveFromPinName = SourcePinCandidate;
                                            EffectiveToPinName = TargetPinCandidate;
                                            bOk = true;
                                            break;
                                        }
                                    }
                                    if (bOk)
                                    {
                                        break;
                                    }
                                }

                                if (!bOk)
                                {
                                    Error = TEXT("Failed to connect material expressions.");
                                }
                                else
                                {
                                    bChanged = true;
                                    GraphEventName = TEXT("graph.node_connected");
                                    GraphEventData->SetStringField(TEXT("fromNodeId"), FromNodeId);
                                    GraphEventData->SetStringField(TEXT("fromPin"), EffectiveFromPinName);
                                    GraphEventData->SetStringField(TEXT("toNodeId"), ToNodeId);
                                    GraphEventData->SetStringField(TEXT("toPin"), EffectiveToPinName);
                                    GraphEventData->SetStringField(TEXT("op"), Op);
                                    NodesTouchedForLayout.Add(FromNodeId);
                                    NodesTouchedForLayout.Add(ToNodeId);
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
                                ResolvePinName(*ToObj, TargetPinName);
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
                                ResolvePinName(*TargetObj, TargetPinName);
                            }
                        }

                        if (bOk)
                        {
                            if (TargetNodeId.Equals(TEXT("__material_root__")))
                            {
                                EMaterialProperty Property = MP_MAX;
                                if (TargetPinName.IsEmpty() || !ResolveMaterialPropertyByRootPinName(TargetPinName, Property))
                                {
                                    bOk = false;
                                    Error = TEXT("Material root pin not found.");
                                }
                                else if (FExpressionInput* Input = MaterialAsset->GetExpressionInputForProperty(Property))
                                {
                                    if (Input->Expression == nullptr)
                                    {
                                        bOk = false;
                                        Error = TEXT("No links were removed.");
                                    }
                                    else
                                    {
                                        Input->Expression = nullptr;
                                        Input->OutputIndex = 0;
                                        bChanged = true;
                                        GraphEventName = TEXT("graph.links_changed");
                                        GraphEventData->SetStringField(TEXT("nodeId"), TargetNodeId);
                                        GraphEventData->SetStringField(TEXT("pinName"), TargetPinName);
                                        GraphEventData->SetStringField(TEXT("op"), Op);
                                    }
                                }
                                else
                                {
                                    bOk = false;
                                    Error = TEXT("Material property input not found.");
                                }
                            }
                            else
                            {
                                UMaterialExpression* Expr = FindMaterialExpressionById(MaterialAsset, TargetNodeId);
                                if (Expr == nullptr)
                                {
                                    bOk = false;
                                    Error = TEXT("Material expression not found.");
                                    continue;
                                }
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
                                            if (DisconnectMaterialInput(Input))
                                            {
                                                bDisconnected = true;
                                            }
                                        }
                                    }
                                    else if (BreakMaterialLinksBySourcePin(Expr, TargetPinName, NodesTouchedForLayout))
                                    {
                                        bDisconnected = true;
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
                                    NodesTouchedForLayout.Add(TargetNodeId);
                                }
                            }
                        }
                    }
                    else if (Op.Equals(TEXT("layoutgraph")))
                    {
                        FString LayoutScope = TEXT("touched");
                        ArgsObj->TryGetStringField(TEXT("scope"), LayoutScope);
                        LayoutScope = LayoutScope.ToLower();

                        TArray<FString> LayoutNodeIds;
                        ResolveNodeTokenArrayFromArgsLocal(ArgsObj, LayoutNodeIds);
                        if (LayoutScope.Equals(TEXT("touched")))
                        {
                            TArray<FString> PendingNodeIds;
                            ResolvePendingGraphLayoutNodes(GraphType, AssetPath, GraphName, PendingNodeIds, false);
                            for (const FString& PendingNodeId : PendingNodeIds)
                            {
                                LayoutNodeIds.AddUnique(PendingNodeId);
                            }
                        }

                        TArray<FString> MovedNodeIds;
                        bOk = ApplyMaterialLayout(AssetPath, GraphName, LayoutScope, LayoutNodeIds, MovedNodeIds, Error);
                        if (bOk)
                        {
                            bChanged = MovedNodeIds.Num() > 0;
                            NodesTouchedForLayout.Append(MovedNodeIds);
                            GraphEventName = TEXT("graph.layout_applied");
                            TArray<TSharedPtr<FJsonValue>> MovedNodeValues;
                            for (const FString& MovedNodeId : MovedNodeIds)
                            {
                                MovedNodeValues.Add(MakeShared<FJsonValueString>(MovedNodeId));
                            }
                            GraphEventData->SetStringField(TEXT("scope"), LayoutScope);
                            GraphEventData->SetArrayField(TEXT("nodeIds"), MovedNodeValues);
                            GraphEventData->SetStringField(TEXT("op"), Op);

                            if (LayoutScope.Equals(TEXT("touched")))
                            {
                                TArray<FString> IgnoredNodeIds;
                                ResolvePendingGraphLayoutNodes(GraphType, AssetPath, GraphName, IgnoredNodeIds, true);
                            }
                        }
                        else if (Error.IsEmpty())
                        {
                            Error = TEXT("layoutGraph failed.");
                        }
                    }
                    else if (Op.Equals(TEXT("compile")))
                    {
                        UMaterialEditingLibrary::RecompileMaterial(MaterialAsset);
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
                    if (Op.Equals(TEXT("addnode.byclass")))
                    {
                        FString SettingsClassPath;
                        ArgsObj->TryGetStringField(TEXT("nodeClassPath"), SettingsClassPath);

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
                                if (HasExplicitPositionLocal(ArgsObj))
                                {
                                    GetPointFromObjectLocal(ArgsObj, X, Y);
                                }
                                else
                                {
                                    ResolveAutoPlacementLocal(ArgsObj, X, Y);
                                }
                                NewNode->SetNodePosition(X, Y);
                                NodeId = NewNode->GetPathName();
                                bChanged = true;
                                GraphEventName = TEXT("graph.node_added");
                                GraphEventData->SetStringField(TEXT("nodeId"), NodeId);
                                GraphEventData->SetStringField(TEXT("nodeType"), TEXT("by_class"));
                                GraphEventData->SetStringField(TEXT("nodeClassPath"), SettingsClassPath);
                                GraphEventData->SetStringField(TEXT("op"), Op);
                                NodesTouchedForLayout.Add(NodeId);
                            }
                        }
                    }
                    else if (Op.Equals(TEXT("removenode")))
                    {
                        FString TargetNodeId;
                        bOk = ResolveNodeTokenFromArgsLocal(ArgsObj, TargetNodeId);
                        if (!bOk)
                        {
                            Error = TEXT("Missing target nodeId/path/name.");
                        }
                        else if (UPCGNode* Node = FindPcgNodeById(PcgGraph, TargetNodeId))
                        {
                            PcgGraph->RemoveNode(Node);
                            bChanged = true;
                            GraphEventName = TEXT("graph.node_removed");
                            GraphEventData->SetStringField(TEXT("nodeId"), TargetNodeId);
                            GraphEventData->SetStringField(TEXT("op"), Op);
                            NodesTouchedForLayout.Add(TargetNodeId);
                        }
                        else
                        {
                            bOk = false;
                            Error = TEXT("PCG node not found.");
                        }
                    }
                    else if (Op.Equals(TEXT("movenode")) || Op.Equals(TEXT("movenodeby")))
                    {
                        FString TargetNodeId;
                        bOk = ResolveNodeTokenFromArgsLocal(ArgsObj, TargetNodeId);
                        if (!bOk)
                        {
                            Error = FString::Printf(TEXT("Missing target nodeId/path/name for %s."), *Op);
                        }
                        else if (UPCGNode* Node = FindPcgNodeById(PcgGraph, TargetNodeId))
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
                            bChanged = true;
                            GraphEventName = TEXT("graph.node_moved");
                            GraphEventData->SetStringField(TEXT("nodeId"), TargetNodeId);
                            GraphEventData->SetNumberField(TEXT("x"), X);
                            GraphEventData->SetNumberField(TEXT("y"), Y);
                            GraphEventData->SetStringField(TEXT("op"), Op);
                            NodesTouchedForLayout.Add(TargetNodeId);
                        }
                        else
                        {
                            bOk = false;
                            Error = TEXT("PCG node not found.");
                        }
                    }
                    else if (Op.Equals(TEXT("movenodes")))
                    {
                        TArray<FString> TargetNodeIds;
                        bOk = ResolveNodeTokenArrayFromArgsLocal(ArgsObj, TargetNodeIds);
                        if (!bOk)
                        {
                            Error = TEXT("Missing nodeIds/nodes for moveNodes.");
                        }
                        else
                        {
                            int32 Dx = 0;
                            int32 Dy = 0;
                            GetDeltaFromObjectLocal(ArgsObj, Dx, Dy);

                            TArray<TSharedPtr<FJsonValue>> MovedNodeIds;
                            for (const FString& TargetNodeId : TargetNodeIds)
                            {
                                UPCGNode* Node = FindPcgNodeById(PcgGraph, TargetNodeId);
                                if (Node == nullptr)
                                {
                                    bOk = false;
                                    Error = FString::Printf(TEXT("PCG node not found: %s"), *TargetNodeId);
                                    break;
                                }

                                int32 CurrentX = 0;
                                int32 CurrentY = 0;
                                Node->GetNodePosition(CurrentX, CurrentY);
                                Node->SetNodePosition(CurrentX + Dx, CurrentY + Dy);
                                MovedNodeIds.Add(MakeShared<FJsonValueString>(TargetNodeId));
                            }

                            if (bOk)
                            {
                                bChanged = MovedNodeIds.Num() > 0;
                                GraphEventName = TEXT("graph.node_moved");
                                GraphEventData->SetArrayField(TEXT("nodeIds"), MovedNodeIds);
                                GraphEventData->SetNumberField(TEXT("dx"), Dx);
                                GraphEventData->SetNumberField(TEXT("dy"), Dy);
                                GraphEventData->SetStringField(TEXT("op"), Op);
                                for (const TSharedPtr<FJsonValue>& MovedNodeId : MovedNodeIds)
                                {
                                    FString MovedNodeIdString;
                                    if (MovedNodeId.IsValid() && MovedNodeId->TryGetString(MovedNodeIdString))
                                    {
                                        NodesTouchedForLayout.Add(MovedNodeIdString);
                                    }
                                }
                            }
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
                            ResolvePinName(*FromObj, FromPinName);
                            ResolvePinName(*ToObj, ToPinName);
                            UPCGNode* FromNode = FindPcgNodeById(PcgGraph, FromNodeId);
                            UPCGNode* ToNode = FindPcgNodeById(PcgGraph, ToNodeId);
                            if (FromNode == nullptr || ToNode == nullptr)
                            {
                                bOk = false;
                                Error = TEXT("PCG node not found.");
                            }
                            else if (FindPcgPin(FromNode, FromPinName, true) == nullptr)
                            {
                                bOk = false;
                                Error = TEXT("PCG output pin not found.");
                            }
                            else if (FindPcgPin(ToNode, ToPinName, false) == nullptr)
                            {
                                bOk = false;
                                Error = TEXT("PCG input pin not found.");
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
                                    NodesTouchedForLayout.Add(FromNodeId);
                                    NodesTouchedForLayout.Add(ToNodeId);
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
                                    NodesTouchedForLayout.Add(FromNodeId);
                                    NodesTouchedForLayout.Add(ToNodeId);
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
                            ResolvePinName(*TargetObj, TargetPinName);
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
                                        NodesTouchedForLayout.Add(TargetNodeId);
                                    }
                                    else
                                    {
                                        Error = TEXT("No links were removed.");
                                    }
                                }
                            }
                        }
                    }
                    else if (Op.Equals(TEXT("setpindefault")))
                    {
                        const TSharedPtr<FJsonObject>* TargetObj = nullptr;
                        FString TargetNodeId;
                        FString TargetPinName;
                        FString DefaultValue;
                        if (!ArgsObj->TryGetObjectField(TEXT("target"), TargetObj) || !TargetObj || !(*TargetObj).IsValid() || !ResolveNodeTokenLocal(*TargetObj, TargetNodeId))
                        {
                            bOk = false;
                            Error = TEXT("setPinDefault requires args.target.");
                        }
                        else if (!ResolvePinName(*TargetObj, TargetPinName))
                        {
                            bOk = false;
                            Error = TEXT("setPinDefault requires args.target.pin.");
                        }
                        else if (!ResolveValueStringLocal(ArgsObj, DefaultValue))
                        {
                            bOk = false;
                            Error = TEXT("setPinDefault requires args.value as a string, number, or boolean.");
                        }
                        else if (UPCGNode* Node = FindPcgNodeById(PcgGraph, TargetNodeId))
                        {
                            bOk = SetPcgPinDefaultValue(Node, TargetPinName, DefaultValue, Error);
                            if (bOk)
                            {
                                NodeId = TargetNodeId;
                                bChanged = true;
                                GraphEventName = TEXT("graph.pin_default_changed");
                                GraphEventData->SetStringField(TEXT("nodeId"), TargetNodeId);
                                GraphEventData->SetStringField(TEXT("pin"), TargetPinName);
                                GraphEventData->SetStringField(TEXT("value"), DefaultValue);
                                GraphEventData->SetStringField(TEXT("op"), Op);
                                NodesTouchedForLayout.Add(TargetNodeId);
                            }
                            else if (Error.IsEmpty())
                            {
                                Error = TEXT("Failed to set PCG pin default value.");
                            }
                        }
                        else
                        {
                            bOk = false;
                            Error = TEXT("PCG node not found.");
                        }
                    }
                    else if (Op.Equals(TEXT("layoutgraph")))
                    {
                        FString LayoutScope = TEXT("touched");
                        ArgsObj->TryGetStringField(TEXT("scope"), LayoutScope);
                        LayoutScope = LayoutScope.ToLower();

                        TArray<FString> LayoutNodeIds;
                        ResolveNodeTokenArrayFromArgsLocal(ArgsObj, LayoutNodeIds);
                        if (LayoutScope.Equals(TEXT("touched")))
                        {
                            TArray<FString> PendingNodeIds;
                            ResolvePendingGraphLayoutNodes(GraphType, AssetPath, GraphName, PendingNodeIds, false);
                            for (const FString& PendingNodeId : PendingNodeIds)
                            {
                                LayoutNodeIds.AddUnique(PendingNodeId);
                            }
                        }

                        TArray<FString> MovedNodeIds;
                        bOk = ApplyPcgLayout(AssetPath, GraphName, LayoutScope, LayoutNodeIds, MovedNodeIds, Error);
                        if (bOk)
                        {
                            bChanged = MovedNodeIds.Num() > 0;
                            NodesTouchedForLayout.Append(MovedNodeIds);
                            GraphEventName = TEXT("graph.layout_applied");
                            TArray<TSharedPtr<FJsonValue>> MovedNodeValues;
                            for (const FString& MovedNodeId : MovedNodeIds)
                            {
                                MovedNodeValues.Add(MakeShared<FJsonValueString>(MovedNodeId));
                            }
                            GraphEventData->SetStringField(TEXT("scope"), LayoutScope);
                            GraphEventData->SetArrayField(TEXT("nodeIds"), MovedNodeValues);
                            GraphEventData->SetStringField(TEXT("op"), Op);

                            if (LayoutScope.Equals(TEXT("touched")))
                            {
                                TArray<FString> IgnoredNodeIds;
                                ResolvePendingGraphLayoutNodes(GraphType, AssetPath, GraphName, IgnoredNodeIds, true);
                            }
                        }
                        else if (Error.IsEmpty())
                        {
                            Error = TEXT("layoutGraph failed.");
                        }
                    }
                    else if (Op.Equals(TEXT("compile")))
                    {
                        bOk = PcgGraph->Recompile();
                        if (bOk)
                        {
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
                RefreshMaterialEditorVisuals(MaterialAsset);
            }

            if (bChanged && PcgGraph != nullptr)
            {
                RefreshPcgEditorVisuals(PcgGraph);
            }

            if (bOk && !Op.Equals(TEXT("layoutgraph")))
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
                RecordPendingGraphLayoutNodes(GraphType, AssetPath, GraphName, SortedTouchedNodeIds);
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

            TSharedPtr<FJsonObject> OpResult = MakeShared<FJsonObject>();
            const FString OpErrorCode = bOk ? TEXT("") : InferMutateErrorCode(Op, Error);
            OpResult->SetNumberField(TEXT("index"), Index);
            OpResult->SetStringField(TEXT("op"), Op);
            OpResult->SetBoolField(TEXT("ok"), bOk);
            if (!NodeId.IsEmpty())
            {
                OpResult->SetStringField(TEXT("nodeId"), NodeId);
            }
            OpResult->SetBoolField(TEXT("changed"), bChanged);
            OpResult->SetStringField(TEXT("error"), bOk ? TEXT("") : Error);
            OpResult->SetStringField(TEXT("errorCode"), OpErrorCode);
            OpResult->SetStringField(TEXT("errorMessage"), bOk ? TEXT("") : Error);
            if (Op.Equals(TEXT("layoutgraph")))
            {
                TArray<TSharedPtr<FJsonValue>> MovedNodeValues;
                for (const FString& MovedNodeId : NodesTouchedForLayout)
                {
                    if (!MovedNodeId.IsEmpty())
                    {
                        MovedNodeValues.Add(MakeShared<FJsonValueString>(MovedNodeId));
                    }
                }
                OpResult->SetArrayField(TEXT("movedNodeIds"), MovedNodeValues);
            }
            LocalOpResults.Add(MakeShared<FJsonValueObject>(OpResult));
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
            TSharedPtr<FJsonObject> ResponseGraphRef = MakeShared<FJsonObject>();
            if (bUsedGraphRef && !InlineNodeGuid.IsEmpty())
            {
                ResponseGraphRef->SetStringField(TEXT("kind"), TEXT("inline"));
                ResponseGraphRef->SetStringField(TEXT("nodeGuid"), InlineNodeGuid);
                ResponseGraphRef->SetStringField(TEXT("assetPath"), AssetPath);
            }
            else
            {
                ResponseGraphRef->SetStringField(TEXT("kind"), TEXT("asset"));
                ResponseGraphRef->SetStringField(TEXT("assetPath"), AssetPath);
                if (!GraphName.IsEmpty())
                {
                    ResponseGraphRef->SetStringField(TEXT("graphName"), GraphName);
                }
            }
            Result->SetObjectField(TEXT("graphRef"), ResponseGraphRef);
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
        Result->SetArrayField(TEXT("diagnostics"), TArray<TSharedPtr<FJsonValue>>{});
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
        if (Obj->TryGetStringField(TEXT("nodePath"), OutNodeId) && !OutNodeId.IsEmpty())
        {
            return true;
        }
        if (Obj->TryGetStringField(TEXT("path"), OutNodeId) && !OutNodeId.IsEmpty())
        {
            return true;
        }
        if (Obj->TryGetStringField(TEXT("nodeName"), OutNodeId) && !OutNodeId.IsEmpty())
        {
            return true;
        }
        if (Obj->TryGetStringField(TEXT("name"), OutNodeId) && !OutNodeId.IsEmpty())
        {
            return true;
        }
        return false;
    };

    auto ResolveGraphNodeToken = [](UEdGraph* Graph, const FString& NodeToken) -> UEdGraphNode*
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

    auto ResolveNodeTokenFromArgs = [&ResolveNodeToken](const TSharedPtr<FJsonObject>& ArgsObj, FString& OutNodeId) -> bool
    {
        OutNodeId.Empty();
        if (!ArgsObj.IsValid())
        {
            return false;
        }

        const TSharedPtr<FJsonObject>* TargetObj = nullptr;
        if (ArgsObj->TryGetObjectField(TEXT("target"), TargetObj) && TargetObj && (*TargetObj).IsValid()
            && ResolveNodeToken(*TargetObj, OutNodeId))
        {
            return true;
        }

        return ResolveNodeToken(ArgsObj, OutNodeId);
    };

    auto ResolveNodeTokenArrayFromArgs = [&ResolveNodeToken](const TSharedPtr<FJsonObject>& ArgsObj, TArray<FString>& OutNodeIds) -> bool
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
                    && ResolveNodeToken(*NodeObj, NodeId) && !NodeId.IsEmpty())
                {
                    OutNodeIds.Add(NodeId);
                }
            }
        }

        return OutNodeIds.Num() > 0;
    };

    auto ResolvePinName = [](const TSharedPtr<FJsonObject>& Obj, FString& OutPinName) -> bool
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

    auto HasExplicitPoint = [](const TSharedPtr<FJsonObject>& Obj) -> bool
    {
        if (!Obj.IsValid())
        {
            return false;
        }

        const TSharedPtr<FJsonObject>* Position = nullptr;
        return Obj->TryGetObjectField(TEXT("position"), Position) && Position && (*Position).IsValid();
    };

    auto GetDeltaFromObject = [](const TSharedPtr<FJsonObject>& Obj, int32& OutDx, int32& OutDy)
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

    auto ResolveBlueprintInsertionPoint =
        [&ResolveNodeToken, &ResolvePinName, &HasExplicitPoint, &GetPointFromObject](
            const FString& BlueprintAssetPath,
            const FString& CurrentGraphName,
            const TSharedPtr<FJsonObject>& ArgsObj,
            const FString& PreferredAnchorNodeId,
            const FString& PreferredAnchorPinName,
            int32& OutX,
            int32& OutY)
    {
        OutX = 0;
        OutY = 0;

        if (HasExplicitPoint(ArgsObj))
        {
            GetPointFromObject(ArgsObj, OutX, OutY);
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

        auto FindAnchorNodeByToken = [](UEdGraph* Graph, const FString& NodeToken) -> UEdGraphNode*
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

        UEdGraphNode* AnchorNode = FindAnchorNodeByToken(TargetGraph, PreferredAnchorNodeId);
        FString AnchorPinName = PreferredAnchorPinName;
        if (AnchorNode == nullptr && ArgsObj.IsValid())
        {
            auto TryResolveAnchorField =
                [&ResolveNodeToken, &ResolvePinName, TargetGraph, &AnchorNode, &AnchorPinName, &FindAnchorNodeByToken](
                    const TSharedPtr<FJsonObject>& SourceObj,
                    const TCHAR* FieldName) -> bool
            {
                const TSharedPtr<FJsonObject>* AnchorObj = nullptr;
                if (!SourceObj.IsValid()
                    || !SourceObj->TryGetObjectField(FieldName, AnchorObj)
                    || !AnchorObj
                    || !(*AnchorObj).IsValid())
                {
                    return false;
                }

                FString AnchorToken;
                if (!ResolveNodeToken(*AnchorObj, AnchorToken) || AnchorToken.IsEmpty())
                {
                    return false;
                }

                AnchorNode = FindAnchorNodeByToken(TargetGraph, AnchorToken);
                if (AnchorNode == nullptr)
                {
                    return false;
                }

                FString ResolvedPinName;
                if (ResolvePinName(*AnchorObj, ResolvedPinName))
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
    };

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
            FString TargetKind;
            if (!(*TargetGraphRefObj)->TryGetStringField(TEXT("kind"), TargetKind) || TargetKind.IsEmpty())
            {
                bOk = false;
                Error = TEXT("targetGraphRef.kind is required.");
            }
            else
            {
                TargetKind = TargetKind.ToLower();
                FString TargetAssetPath;
                if (!(*TargetGraphRefObj)->TryGetStringField(TEXT("assetPath"), TargetAssetPath) || TargetAssetPath.IsEmpty())
                {
                    bOk = false;
                    Error = TEXT("targetGraphRef.assetPath is required.");
                }
                else if (!NormalizeAssetPath(TargetAssetPath).Equals(AssetPath, ESearchCase::CaseSensitive))
                {
                    bOk = false;
                    Error = TEXT("targetGraphRef.assetPath must match request assetPath for this mutate call.");
                }
                else if (TargetKind.Equals(TEXT("asset")))
                {
                    if (!(*TargetGraphRefObj)->TryGetStringField(TEXT("graphName"), OpGraphName) || OpGraphName.IsEmpty())
                    {
                        OpGraphName = GraphName;
                    }
                }
                else if (TargetKind.Equals(TEXT("inline")))
                {
                    FString TargetNodeGuid;
                    if (!(*TargetGraphRefObj)->TryGetStringField(TEXT("nodeGuid"), TargetNodeGuid) || TargetNodeGuid.IsEmpty())
                    {
                        bOk = false;
                        Error = TEXT("targetGraphRef.nodeGuid is required for kind=inline.");
                    }
                    else
                    {
                        FString ResolvedOpGraphName;
                        FString ResolveError;
                        if (!ResolveInlineBlueprintGraphName(AssetPath, TargetNodeGuid, ResolvedOpGraphName, ResolveError))
                        {
                            bOk = false;
                            Error = ResolveError.IsEmpty() ? TEXT("Failed to resolve targetGraphRef inline node.") : ResolveError;
                        }
                        else
                        {
                            OpGraphName = ResolvedOpGraphName;
                        }
                    }
                }
                else
                {
                    bOk = false;
                    Error = FString::Printf(TEXT("Unsupported targetGraphRef.kind: %s"), *TargetKind);
                }
            }
        }

        const bool bRunScriptOp = Op.Equals(TEXT("runscript"));
        if (bOk && (!bDryRun || bRunScriptOp))
        {
            if (Op.Equals(TEXT("addnode.byclass")))
            {
                FString NodeClassPath;
                ArgsObj->TryGetStringField(TEXT("nodeClassPath"), NodeClassPath);
                int32 X = 0;
                int32 Y = 0;
                ResolveBlueprintInsertionPoint(AssetPath, OpGraphName, ArgsObj, TEXT(""), TEXT(""), X, Y);
                bOk = FLoomleBlueprintAdapter::AddNodeByClass(AssetPath, OpGraphName, NodeClassPath, SerializeJsonObject(ArgsObj), X, Y, NodeId, Error);
                if (bOk)
                {
                    NodesTouchedForLayout.Add(NodeId);
                    GraphEventName = TEXT("graph.node_added");
                    GraphEventData->SetStringField(TEXT("nodeId"), NodeId);
                    GraphEventData->SetStringField(TEXT("nodeType"), TEXT("by_class"));
                    GraphEventData->SetStringField(TEXT("nodeClassPath"), NodeClassPath);
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
                    ResolvePinName(*FromObj, FromPin);
                }
                if (ArgsObj->TryGetObjectField(TEXT("to"), ToObj) && ToObj && (*ToObj).IsValid())
                {
                    ResolveNodeToken(*ToObj, ToNodeId);
                    ResolvePinName(*ToObj, ToPin);
                }
                bOk = !FromNodeId.IsEmpty() && !ToNodeId.IsEmpty() && !FromPin.IsEmpty() && !ToPin.IsEmpty()
                    && FLoomleBlueprintAdapter::ConnectPins(AssetPath, OpGraphName, FromNodeId, FromPin, ToNodeId, ToPin, Error);
                if (!bOk && Error.IsEmpty())
                {
                    Error = TEXT("Failed to resolve connectPins node ids/pins.");
                }
                if (bOk)
                {
                    NodesTouchedForLayout.Add(FromNodeId);
                    NodesTouchedForLayout.Add(ToNodeId);
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
                    ResolvePinName(*FromObj, FromPin);
                }
                if (ArgsObj->TryGetObjectField(TEXT("to"), ToObj) && ToObj && (*ToObj).IsValid())
                {
                    ResolveNodeToken(*ToObj, ToNodeId);
                    ResolvePinName(*ToObj, ToPin);
                }
                bOk = !FromNodeId.IsEmpty() && !ToNodeId.IsEmpty() && !FromPin.IsEmpty() && !ToPin.IsEmpty()
                    && FLoomleBlueprintAdapter::DisconnectPins(AssetPath, OpGraphName, FromNodeId, FromPin, ToNodeId, ToPin, Error);
                if (!bOk && Error.IsEmpty())
                {
                    Error = TEXT("Failed to resolve disconnectPins node ids/pins.");
                }
                if (bOk)
                {
                    NodesTouchedForLayout.Add(FromNodeId);
                    NodesTouchedForLayout.Add(ToNodeId);
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
                    ResolvePinName(*TargetObj, Pin);
                }
                bOk = !NodeToken.IsEmpty() && !Pin.IsEmpty()
                    && FLoomleBlueprintAdapter::BreakPinLinks(AssetPath, OpGraphName, NodeToken, Pin, Error);
                if (!bOk && Error.IsEmpty())
                {
                    Error = TEXT("Failed to resolve breakPinLinks target.");
                }
                if (bOk)
                {
                    NodesTouchedForLayout.Add(NodeToken);
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
                    ResolvePinName(*TargetObj, Pin);
                }
                ArgsObj->TryGetStringField(TEXT("value"), Value);
                bOk = !NodeToken.IsEmpty() && !Pin.IsEmpty()
                    && FLoomleBlueprintAdapter::SetPinDefaultValue(AssetPath, OpGraphName, NodeToken, Pin, Value, Error);
                if (!bOk && Error.IsEmpty())
                {
                    Error = TEXT("Failed to resolve setPinDefault target.");
                }
                if (!bOk)
                {
                    FString DetailsJson;
                    FString DetailsError;
                    if (FLoomleBlueprintAdapter::DescribePinTarget(AssetPath, OpGraphName, NodeToken, Pin, DetailsJson, DetailsError))
                    {
                        TSharedPtr<FJsonObject> DetailsObject;
                        TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(DetailsJson);
                        if (FJsonSerializer::Deserialize(Reader, DetailsObject) && DetailsObject.IsValid())
                        {
                            ErrorDetailsForOp = DetailsObject;
                        }
                    }
                }
                if (bOk)
                {
                    NodesTouchedForLayout.Add(NodeToken);
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
                ResolveNodeTokenFromArgs(ArgsObj, NodeToken);
                bOk = !NodeToken.IsEmpty() && FLoomleBlueprintAdapter::RemoveNode(AssetPath, OpGraphName, NodeToken, Error);
                if (!bOk && Error.IsEmpty())
                {
                    Error = TEXT("Failed to resolve removeNode target. Provide nodeId, nodeRef, nodePath, path, nodeName, or name.");
                }
                if (bOk)
                {
                    NodesTouchedForLayout.Add(NodeToken);
                    GraphEventName = TEXT("graph.node_removed");
                    GraphEventData->SetStringField(TEXT("nodeId"), NodeToken);
                    GraphEventData->SetStringField(TEXT("op"), Op);
                }
            }
            else if (Op.Equals(TEXT("movenode")) || Op.Equals(TEXT("movenodeby")))
            {
                FString NodeToken;
                ResolveNodeTokenFromArgs(ArgsObj, NodeToken);
                int32 X = 0;
                int32 Y = 0;
                if (Op.Equals(TEXT("movenodeby")))
                {
                    UBlueprint* Blueprint = LoadBlueprintByAssetPath(AssetPath);
                    UEdGraph* TargetGraph = ResolveBlueprintGraph(Blueprint, OpGraphName);
                    UEdGraphNode* TargetNode = TargetGraph ? ResolveGraphNodeToken(TargetGraph, NodeToken) : nullptr;
                    if (TargetNode == nullptr)
                    {
                        bOk = false;
                        Error = TEXT("Failed to resolve moveNodeBy target.");
                    }
                    else
                    {
                        int32 Dx = 0;
                        int32 Dy = 0;
                        GetDeltaFromObject(ArgsObj, Dx, Dy);
                        X = TargetNode->NodePosX + Dx;
                        Y = TargetNode->NodePosY + Dy;
                    }
                }
                else
                {
                    GetPointFromObject(ArgsObj, X, Y);
                }
                bOk = bOk && !NodeToken.IsEmpty() && FLoomleBlueprintAdapter::MoveNode(AssetPath, OpGraphName, NodeToken, X, Y, Error);
                if (!bOk && Error.IsEmpty())
                {
                    Error = FString::Printf(TEXT("Failed to resolve %s target."), *Op);
                }
                if (bOk)
                {
                    NodesTouchedForLayout.Add(NodeToken);
                    GraphEventName = TEXT("graph.node_moved");
                    GraphEventData->SetStringField(TEXT("nodeId"), NodeToken);
                    GraphEventData->SetNumberField(TEXT("x"), X);
                    GraphEventData->SetNumberField(TEXT("y"), Y);
                    GraphEventData->SetStringField(TEXT("op"), Op);
                }
            }
            else if (Op.Equals(TEXT("movenodes")))
            {
                TArray<FString> NodeTokens;
                bOk = ResolveNodeTokenArrayFromArgs(ArgsObj, NodeTokens);
                if (!bOk)
                {
                    Error = TEXT("Missing nodeIds/nodes for moveNodes.");
                }
                else
                {
                    UBlueprint* Blueprint = LoadBlueprintByAssetPath(AssetPath);
                    UEdGraph* TargetGraph = ResolveBlueprintGraph(Blueprint, OpGraphName);
                    if (TargetGraph == nullptr)
                    {
                        bOk = false;
                        Error = TEXT("Failed to resolve target graph for moveNodes.");
                    }
                    else
                    {
                        int32 Dx = 0;
                        int32 Dy = 0;
                        GetDeltaFromObject(ArgsObj, Dx, Dy);

                        TArray<TSharedPtr<FJsonValue>> MovedNodeIds;
                        for (const FString& NodeToken : NodeTokens)
                        {
                            UEdGraphNode* TargetNode = ResolveGraphNodeToken(TargetGraph, NodeToken);
                            if (TargetNode == nullptr)
                            {
                                bOk = false;
                                Error = FString::Printf(TEXT("Failed to resolve moveNodes target: %s"), *NodeToken);
                                break;
                            }

                            const int32 NewX = TargetNode->NodePosX + Dx;
                            const int32 NewY = TargetNode->NodePosY + Dy;
                            if (!FLoomleBlueprintAdapter::MoveNode(AssetPath, OpGraphName, NodeToken, NewX, NewY, Error))
                            {
                                bOk = false;
                                break;
                            }

                            MovedNodeIds.Add(MakeShared<FJsonValueString>(
                                TargetNode->NodeGuid.ToString(EGuidFormats::DigitsWithHyphensLower)));
                        }

                        if (bOk)
                        {
                            for (const TSharedPtr<FJsonValue>& MovedNodeId : MovedNodeIds)
                            {
                                FString MovedNodeIdString;
                                if (MovedNodeId.IsValid() && MovedNodeId->TryGetString(MovedNodeIdString))
                                {
                                    NodesTouchedForLayout.Add(MovedNodeIdString);
                                }
                            }
                            bChanged = MovedNodeIds.Num() > 0;
                            GraphEventName = TEXT("graph.node_moved");
                            GraphEventData->SetArrayField(TEXT("nodeIds"), MovedNodeIds);
                            GraphEventData->SetNumberField(TEXT("dx"), Dx);
                            GraphEventData->SetNumberField(TEXT("dy"), Dy);
                            GraphEventData->SetStringField(TEXT("op"), Op);
                        }
                    }
                }
            }
            else if (Op.Equals(TEXT("layoutgraph")))
            {
                FString LayoutScope = TEXT("touched");
                ArgsObj->TryGetStringField(TEXT("scope"), LayoutScope);
                LayoutScope = LayoutScope.ToLower();

                TArray<FString> LayoutNodeIds;
                ResolveNodeTokenArrayFromArgs(ArgsObj, LayoutNodeIds);
                if (LayoutScope.Equals(TEXT("touched")))
                {
                    TArray<FString> PendingNodeIds;
                    ResolvePendingGraphLayoutNodes(GraphType, AssetPath, OpGraphName, PendingNodeIds, false);
                    for (const FString& PendingNodeId : PendingNodeIds)
                    {
                        LayoutNodeIds.AddUnique(PendingNodeId);
                    }
                }

                TArray<FString> MovedNodeIds;
                bOk = ApplyBlueprintLayout(AssetPath, OpGraphName, LayoutScope, LayoutNodeIds, MovedNodeIds, Error);
                if (bOk)
                {
                    NodesTouchedForLayout.Append(MovedNodeIds);
                    GraphEventName = TEXT("graph.layout_applied");
                    TArray<TSharedPtr<FJsonValue>> MovedNodeValues;
                    for (const FString& MovedNodeId : MovedNodeIds)
                    {
                        MovedNodeValues.Add(MakeShared<FJsonValueString>(MovedNodeId));
                    }
                    GraphEventData->SetStringField(TEXT("scope"), LayoutScope);
                    GraphEventData->SetArrayField(TEXT("nodeIds"), MovedNodeValues);
                    GraphEventData->SetStringField(TEXT("op"), Op);

                    if (LayoutScope.Equals(TEXT("touched")))
                    {
                        TArray<FString> IgnoredNodeIds;
                        ResolvePendingGraphLayoutNodes(GraphType, AssetPath, OpGraphName, IgnoredNodeIds, true);
                    }
                }
                else if (Error.IsEmpty())
                {
                    Error = TEXT("layoutGraph failed.");
                }
            }
            else if (Op.Equals(TEXT("compile")))
            {
                bOk = FLoomleBlueprintAdapter::CompileBlueprint(AssetPath, OpGraphName, Error);
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
                else if (bDryRun)
                {
                    bSkipped = true;
                    SkipReason = TEXT("dryRun");
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

        if (bOk && !Op.Equals(TEXT("layoutgraph")))
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
            RecordPendingGraphLayoutNodes(GraphType, AssetPath, OpGraphName, SortedTouchedNodeIds);
        }

        TSharedPtr<FJsonObject> OpResult = MakeShared<FJsonObject>();
        const FString OpErrorCode = bOk ? TEXT("") : InferMutateErrorCode(Op, Error);
        OpResult->SetNumberField(TEXT("index"), Index);
        OpResult->SetStringField(TEXT("op"), Op);
        OpResult->SetBoolField(TEXT("ok"), bOk);
        OpResult->SetBoolField(TEXT("skipped"), bSkipped);
        if (!NodeId.IsEmpty())
        {
            OpResult->SetStringField(TEXT("nodeId"), NodeId);
        }
        OpResult->SetBoolField(TEXT("changed"), bChanged);
        if (!Error.IsEmpty())
        {
            OpResult->SetStringField(TEXT("error"), Error);
        }
        OpResult->SetStringField(TEXT("errorCode"), OpErrorCode);
        OpResult->SetStringField(TEXT("errorMessage"), bOk ? TEXT("") : Error);
        if (bSkipped && !SkipReason.IsEmpty())
        {
            OpResult->SetStringField(TEXT("skipReason"), SkipReason);
        }
        if (Op.Equals(TEXT("layoutgraph")))
        {
            TArray<TSharedPtr<FJsonValue>> MovedNodeValues;
            for (const FString& MovedNodeId : NodesTouchedForLayout)
            {
                if (!MovedNodeId.IsEmpty())
                {
                    MovedNodeValues.Add(MakeShared<FJsonValueString>(MovedNodeId));
                }
            }
            OpResult->SetArrayField(TEXT("movedNodeIds"), MovedNodeValues);
        }
        if (ErrorDetailsForOp.IsValid())
        {
            OpResult->SetObjectField(TEXT("details"), ErrorDetailsForOp);
        }
        if (ScriptResultForOp.IsValid())
        {
            OpResult->SetObjectField(TEXT("scriptResult"), ScriptResultForOp);
        }
        OpResults.Add(MakeShared<FJsonValueObject>(OpResult));
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
        TSharedPtr<FJsonObject> ResponseGraphRef = MakeShared<FJsonObject>();
        if (bUsedGraphRef && !InlineNodeGuid.IsEmpty())
        {
            ResponseGraphRef->SetStringField(TEXT("kind"), TEXT("inline"));
            ResponseGraphRef->SetStringField(TEXT("nodeGuid"), InlineNodeGuid);
            ResponseGraphRef->SetStringField(TEXT("assetPath"), AssetPath);
        }
        else
        {
            ResponseGraphRef->SetStringField(TEXT("kind"), TEXT("asset"));
            ResponseGraphRef->SetStringField(TEXT("assetPath"), AssetPath);
            if (!GraphName.IsEmpty())
            {
                ResponseGraphRef->SetStringField(TEXT("graphName"), GraphName);
            }
        }
        Result->SetObjectField(TEXT("graphRef"), ResponseGraphRef);
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
