// PCG-domain tool adapters.
namespace
{
void SetNodeAssetChildGraphRef(
    const TSharedPtr<FJsonObject>& Node,
    const FString& ChildAssetPath,
    const FString& ChildLoadStatus);

bool ResolvePcgChildGraphReference(
    UPCGSettings* NodeSettings,
    FString& OutAssetPath,
    FString& OutLoadStatus,
    FString& OutGraphName,
    FString& OutGraphClassPath);

void AppendPcgSyntheticWritablePins(UPCGSettings* Settings, TArray<TSharedPtr<FJsonValue>>& Pins);

TSharedPtr<FJsonObject> BuildPcgNodeSettingsObject(
    UPCGNode* NodeObj,
    TArray<TSharedPtr<FJsonValue>>& NodeDiagnostics,
    TArray<TSharedPtr<FJsonValue>>& RootDiagnostics);

UClass* ResolvePcgSettingsDescribeClass(const FString& ClassToken)
{
    const FString TrimmedToken = ClassToken.TrimStartAndEnd();
    if (TrimmedToken.IsEmpty())
    {
        return nullptr;
    }

    auto AcceptClass = [](UClass* Candidate) -> UClass*
    {
        return Candidate != nullptr && Candidate->IsChildOf(UPCGSettings::StaticClass()) ? Candidate : nullptr;
    };

    if (UClass* DirectClass = AcceptClass(LoadObject<UClass>(nullptr, *TrimmedToken)))
    {
        return DirectClass;
    }

    if (UClass* NamedClass = AcceptClass(FindFirstObject<UClass>(*TrimmedToken, EFindFirstObjectOptions::None)))
    {
        return NamedClass;
    }

    const FString NormalizedToken = TrimmedToken.ToLower();
    for (TObjectIterator<UClass> It; It; ++It)
    {
        UClass* Candidate = *It;
        if (!Candidate->IsChildOf(UPCGSettings::StaticClass()))
        {
            continue;
        }

        const FString ClassName = Candidate->GetName();
        const FString ClassPath = Candidate->GetPathName();
        if (ClassName.Equals(TrimmedToken, ESearchCase::IgnoreCase)
            || ClassPath.Equals(TrimmedToken, ESearchCase::IgnoreCase)
            || ClassName.ToLower().Equals(NormalizedToken))
        {
            return Candidate;
        }
    }

    return nullptr;
}

TSharedPtr<FJsonObject> MakePcgDescribePinEntry(const FPCGPinProperties& Pin)
{
    TSharedPtr<FJsonObject> PinObject = MakeShared<FJsonObject>();
    PinObject->SetStringField(TEXT("name"), Pin.Label.ToString());
    PinObject->SetBoolField(TEXT("allowMultipleData"), Pin.bAllowMultipleData);
    PinObject->SetBoolField(TEXT("allowMultipleConnections"), Pin.AllowsMultipleConnections());
    PinObject->SetBoolField(TEXT("advanced"), Pin.IsAdvancedPin());
    PinObject->SetBoolField(TEXT("required"), Pin.IsRequiredPin());
    PinObject->SetBoolField(TEXT("invisible"), Pin.bInvisiblePin);

    if (const UEnum* UsageEnum = StaticEnum<EPCGPinUsage>())
    {
        PinObject->SetStringField(TEXT("usage"), UsageEnum->GetNameStringByValue(static_cast<int64>(Pin.Usage)));
    }
    PinObject->SetStringField(TEXT("allowedTypes"), Pin.AllowedTypes.ToString());
    return PinObject;
}

TSharedPtr<FJsonObject> MakePcgDescribePropertyType(const FProperty* Property)
{
    TSharedPtr<FJsonObject> TypeObject = MakeShared<FJsonObject>();
    if (Property == nullptr)
    {
        return TypeObject;
    }

    TypeObject->SetStringField(TEXT("cppType"), Property->GetCPPType());
    TypeObject->SetStringField(TEXT("propertyClass"), Property->GetClass() ? Property->GetClass()->GetName() : TEXT(""));
    if (const FStructProperty* StructProperty = CastField<FStructProperty>(Property))
    {
        TypeObject->SetStringField(TEXT("kind"), TEXT("struct"));
        TypeObject->SetStringField(TEXT("objectClassPath"), StructProperty->Struct ? StructProperty->Struct->GetPathName() : TEXT(""));
    }
    else if (const FObjectPropertyBase* ObjectProperty = CastField<FObjectPropertyBase>(Property))
    {
        TypeObject->SetStringField(TEXT("kind"), TEXT("object"));
        TypeObject->SetStringField(TEXT("objectClassPath"), ObjectProperty->PropertyClass ? ObjectProperty->PropertyClass->GetPathName() : TEXT(""));
    }
    else if (const FEnumProperty* EnumProperty = CastField<FEnumProperty>(Property))
    {
        TypeObject->SetStringField(TEXT("kind"), TEXT("enum"));
        TypeObject->SetStringField(TEXT("objectClassPath"), EnumProperty->GetEnum() ? EnumProperty->GetEnum()->GetPathName() : TEXT(""));
    }
    else if (Property->IsA<FBoolProperty>())
    {
        TypeObject->SetStringField(TEXT("kind"), TEXT("bool"));
    }
    else if (Property->IsA<FNumericProperty>())
    {
        TypeObject->SetStringField(TEXT("kind"), TEXT("number"));
    }
    else
    {
        TypeObject->SetStringField(TEXT("kind"), TEXT("value"));
    }

    return TypeObject;
}

TSharedPtr<FJsonObject> BuildPcgClassDescribeResult(const FString& NodeClass)
{
    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

    UClass* SettingsClass = ResolvePcgSettingsDescribeClass(NodeClass);
    if (SettingsClass == nullptr)
    {
        Result->SetBoolField(TEXT("isError"), true);
        Result->SetStringField(TEXT("code"), TEXT("NODE_CLASS_NOT_FOUND"));
        Result->SetStringField(TEXT("message"), TEXT("PCG settings class not found."));
        return Result;
    }

    UPCGSettings* Settings = Cast<UPCGSettings>(SettingsClass->GetDefaultObject());
    if (Settings == nullptr)
    {
        Result->SetBoolField(TEXT("isError"), true);
        Result->SetStringField(TEXT("code"), TEXT("INTERNAL_ERROR"));
        Result->SetStringField(TEXT("message"), TEXT("PCG settings default object is unavailable."));
        return Result;
    }

    Result->SetBoolField(TEXT("isError"), false);
    Result->SetStringField(TEXT("mode"), TEXT("class"));
    Result->SetStringField(TEXT("nodeClass"), SettingsClass->GetPathName());
    Result->SetStringField(TEXT("title"), Settings->GetDefaultNodeTitle().ToString());
    Result->SetStringField(TEXT("tooltip"), Settings->GetNodeTooltipText().ToString());
    if (const UEnum* SettingsTypeEnum = StaticEnum<EPCGSettingsType>())
    {
        Result->SetStringField(TEXT("settingsType"), SettingsTypeEnum->GetNameStringByValue(static_cast<int64>(Settings->GetType())));
    }

    TArray<TSharedPtr<FJsonValue>> InputPins;
    for (const FPCGPinProperties& Pin : Settings->AllInputPinProperties())
    {
        InputPins.Add(MakeShared<FJsonValueObject>(MakePcgDescribePinEntry(Pin)));
    }
    Result->SetArrayField(TEXT("inputPins"), InputPins);

    TArray<TSharedPtr<FJsonValue>> OutputPins;
    for (const FPCGPinProperties& Pin : Settings->AllOutputPinProperties())
    {
        OutputPins.Add(MakeShared<FJsonValueObject>(MakePcgDescribePinEntry(Pin)));
    }
    Result->SetArrayField(TEXT("outputPins"), OutputPins);

    TArray<TSharedPtr<FJsonValue>> Properties;
    for (UStruct* CurrentStruct = Settings->GetClass();
         CurrentStruct != nullptr && CurrentStruct != UPCGSettings::StaticClass();
         CurrentStruct = CurrentStruct->GetSuperStruct())
    {
        for (TFieldIterator<FProperty> It(CurrentStruct, EFieldIteratorFlags::ExcludeSuper); It; ++It)
        {
            FProperty* Property = *It;
            if (!ShouldSurfacePcgSyntheticLeafProperty(Property))
            {
                continue;
            }

            TSharedPtr<FJsonObject> PropertyObject = MakeShared<FJsonObject>();
            PropertyObject->SetStringField(TEXT("name"), Property->GetName());
            PropertyObject->SetStringField(TEXT("displayName"), Property->GetDisplayNameText().ToString());
            PropertyObject->SetObjectField(TEXT("type"), MakePcgDescribePropertyType(Property));

            FString DefaultValue;
            FString DefaultText;
            if (TryReadPcgPropertyValueForQuery(Settings, Property, DefaultValue, DefaultText))
            {
                PropertyObject->SetStringField(TEXT("defaultValue"), DefaultValue);
                PropertyObject->SetStringField(TEXT("defaultText"), DefaultText);
            }

            Properties.Add(MakeShared<FJsonValueObject>(PropertyObject));
        }
    }
    Result->SetArrayField(TEXT("properties"), Properties);

    return Result;
}

TSharedPtr<FJsonObject> MakePcgGraphAssetRef(const FString& AssetPath)
{
    TSharedPtr<FJsonObject> Ref = MakeShared<FJsonObject>();
    Ref->SetStringField(TEXT("kind"), TEXT("asset"));
    Ref->SetStringField(TEXT("assetPath"), AssetPath);
    return Ref;
}

}

TSharedPtr<FJsonObject> FLoomleBridgeModule::BuildPcgListToolResult(const TSharedPtr<FJsonObject>& Arguments) const
{
    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetBoolField(TEXT("isError"), false);

    FString AssetPath;
    if (!Arguments.IsValid()
        || !Arguments->TryGetStringField(TEXT("assetPath"), AssetPath)
        || AssetPath.TrimStartAndEnd().IsEmpty())
    {
        Result->SetBoolField(TEXT("isError"), true);
        Result->SetStringField(TEXT("code"), TEXT("INVALID_ARGUMENT"));
        Result->SetStringField(TEXT("message"), TEXT("pcg.list requires assetPath."));
        return Result;
    }

    AssetPath = NormalizeAssetPath(AssetPath);
    UPCGGraph* PcgGraph = LoadPcgGraphByAssetPath(AssetPath);
    if (PcgGraph == nullptr)
    {
        Result->SetBoolField(TEXT("isError"), true);
        Result->SetStringField(TEXT("code"), TEXT("ASSET_NOT_FOUND"));
        Result->SetStringField(TEXT("message"), TEXT("PCG asset not found."));
        return Result;
    }

    TArray<TSharedPtr<FJsonValue>> Nodes;
    for (UPCGNode* NodeObj : PcgGraph->GetNodes())
    {
        if (NodeObj == nullptr)
        {
            continue;
        }

        int32 NodePosX = 0;
        int32 NodePosY = 0;
        NodeObj->GetNodePosition(NodePosX, NodePosY);

        TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
        Entry->SetStringField(TEXT("nodeId"), NodeObj->GetPathName());
        Entry->SetStringField(
            TEXT("class"),
            NodeObj->GetSettings() && NodeObj->GetSettings()->GetClass()
                ? NodeObj->GetSettings()->GetClass()->GetPathName()
                : (NodeObj->GetClass() ? NodeObj->GetClass()->GetPathName() : TEXT("")));
        Entry->SetNumberField(TEXT("x"), NodePosX);
        Entry->SetNumberField(TEXT("y"), NodePosY);
        Entry->SetStringField(TEXT("label"), NodeObj->NodeTitle.IsNone() ? NodeObj->GetName() : NodeObj->NodeTitle.ToString());
        Nodes.Add(MakeShared<FJsonValueObject>(Entry));
    }

    Result->SetStringField(TEXT("assetPath"), AssetPath);
    Result->SetArrayField(TEXT("nodes"), Nodes);
    return Result;
}

TSharedPtr<FJsonObject> FLoomleBridgeModule::BuildPcgQueryToolResult(const TSharedPtr<FJsonObject>& Arguments) const
{
    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetBoolField(TEXT("isError"), false);

    FString AssetPath;
    if (!Arguments.IsValid()
        || !Arguments->TryGetStringField(TEXT("assetPath"), AssetPath)
        || AssetPath.TrimStartAndEnd().IsEmpty())
    {
        Result->SetBoolField(TEXT("isError"), true);
        Result->SetStringField(TEXT("code"), TEXT("INVALID_ARGUMENT"));
        Result->SetStringField(TEXT("message"), TEXT("pcg.query requires assetPath."));
        return Result;
    }

    AssetPath = NormalizeAssetPath(AssetPath);
    UPCGGraph* PcgGraph = LoadPcgGraphByAssetPath(AssetPath);
    if (PcgGraph == nullptr)
    {
        Result->SetBoolField(TEXT("isError"), true);
        Result->SetStringField(TEXT("code"), TEXT("ASSET_NOT_FOUND"));
        Result->SetStringField(TEXT("message"), TEXT("PCG asset not found."));
        return Result;
    }

    TSet<FString> RequestedNodeIds;
    const TArray<TSharedPtr<FJsonValue>>* NodeIds = nullptr;
    if (Arguments->TryGetArrayField(TEXT("nodeIds"), NodeIds) && NodeIds != nullptr)
    {
        for (const TSharedPtr<FJsonValue>& Value : *NodeIds)
        {
            FString NodeId;
            if (Value.IsValid() && Value->TryGetString(NodeId) && !NodeId.IsEmpty())
            {
                RequestedNodeIds.Add(NodeId);
            }
        }
    }

    TArray<FString> RequestedNodeClasses;
    const TArray<TSharedPtr<FJsonValue>>* NodeClasses = nullptr;
    if (Arguments->TryGetArrayField(TEXT("nodeClasses"), NodeClasses) && NodeClasses != nullptr)
    {
        for (const TSharedPtr<FJsonValue>& Value : *NodeClasses)
        {
            FString NodeClass;
            if (Value.IsValid() && Value->TryGetString(NodeClass) && !NodeClass.IsEmpty())
            {
                RequestedNodeClasses.Add(NodeClass);
            }
        }
    }

    auto MakeLayoutObject = [](int32 PositionX, int32 PositionY) -> TSharedPtr<FJsonObject>
    {
        TSharedPtr<FJsonObject> Layout = MakeShared<FJsonObject>();
        TSharedPtr<FJsonObject> Position = MakeShared<FJsonObject>();
        Position->SetNumberField(TEXT("x"), PositionX);
        Position->SetNumberField(TEXT("y"), PositionY);
        Layout->SetObjectField(TEXT("position"), Position);
        Layout->SetStringField(TEXT("source"), TEXT("model"));
        Layout->SetBoolField(TEXT("reliable"), true);
        Layout->SetStringField(TEXT("sizeSource"), TEXT("unsupported"));
        Layout->SetStringField(TEXT("boundsSource"), TEXT("unsupported"));
        return Layout;
    };

    TArray<TSharedPtr<FJsonValue>> Nodes;
    TArray<TSharedPtr<FJsonValue>> Edges;
    TArray<TSharedPtr<FJsonValue>> Diagnostics;
    TSet<FString> EmittedEdgeKeys;
    int32 ChildGraphRefCount = 0;

    for (UPCGNode* NodeObj : PcgGraph->GetNodes())
    {
        if (NodeObj == nullptr)
        {
            continue;
        }

        const FString NodeId = NodeObj->GetPathName();
        const FString NodeClassPath = (NodeObj->GetSettings() && NodeObj->GetSettings()->GetClass())
            ? NodeObj->GetSettings()->GetClass()->GetPathName()
            : (NodeObj->GetClass() ? NodeObj->GetClass()->GetPathName() : TEXT(""));

        if (RequestedNodeIds.Num() > 0 && !RequestedNodeIds.Contains(NodeId))
        {
            continue;
        }

        if (RequestedNodeClasses.Num() > 0)
        {
            bool bMatched = false;
            for (const FString& RequestedClass : RequestedNodeClasses)
            {
                if (NodeClassPath.Equals(RequestedClass))
                {
                    bMatched = true;
                    break;
                }
            }
            if (!bMatched)
            {
                continue;
            }
        }

        int32 NodePosX = 0;
        int32 NodePosY = 0;
        NodeObj->GetNodePosition(NodePosX, NodePosY);

        TSharedPtr<FJsonObject> Node = MakeShared<FJsonObject>();
        Node->SetStringField(TEXT("id"), NodeId);
        Node->SetStringField(TEXT("guid"), NodeId);
        Node->SetStringField(TEXT("nodeClassPath"), NodeClassPath);
        Node->SetStringField(TEXT("title"), NodeObj->NodeTitle.IsNone() ? NodeObj->GetName() : NodeObj->NodeTitle.ToString());
        Node->SetStringField(TEXT("graphName"), TEXT(""));

        TSharedPtr<FJsonObject> Position = MakeShared<FJsonObject>();
        Position->SetNumberField(TEXT("x"), NodePosX);
        Position->SetNumberField(TEXT("y"), NodePosY);
        Node->SetObjectField(TEXT("position"), Position);
        Node->SetObjectField(TEXT("layout"), MakeLayoutObject(NodePosX, NodePosY));
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

        if (UPCGSettings* NodeSettings = NodeObj->GetSettings())
        {
            FString SubAssetPath;
            FString ChildLoadStatus;
            FString SubgraphName;
            FString SubgraphClassPath;
            if (ResolvePcgChildGraphReference(NodeSettings, SubAssetPath, ChildLoadStatus, SubgraphName, SubgraphClassPath))
            {
                SetNodeAssetChildGraphRef(Node, SubAssetPath, ChildLoadStatus);
                ++ChildGraphRefCount;
            }

            if (TSharedPtr<FJsonObject> Settings = BuildPcgNodeSettingsObject(NodeObj, NodeDiagnostics, Diagnostics))
            {
                Node->SetObjectField(TEXT("settings"), Settings);
                Node->SetObjectField(TEXT("effectiveSettings"), Settings);
            }
        }

        Node->SetArrayField(TEXT("pins"), Pins);
        Node->SetArrayField(TEXT("diagnostics"), NodeDiagnostics);
        Nodes.Add(MakeShared<FJsonValueObject>(Node));
    }

    if (Nodes.Num() == 0)
    {
        TSharedPtr<FJsonObject> Diagnostic = MakeShared<FJsonObject>();
        Diagnostic->SetStringField(TEXT("code"), TEXT("QUERY_DEGRADED"));
        Diagnostic->SetStringField(TEXT("message"), TEXT("PCG graph has no nodes."));
        Diagnostics.Add(MakeShared<FJsonValueObject>(Diagnostic));
    }
    else if (ChildGraphRefCount > 0)
    {
        TSharedPtr<FJsonObject> Diagnostic = MakeShared<FJsonObject>();
        Diagnostic->SetStringField(TEXT("code"), TEXT("PCG_SUBGRAPH_REFS_PRESENT"));
        Diagnostic->SetStringField(TEXT("message"), TEXT("This PCG query covers the current asset only. Follow childGraphRef entries to inspect referenced subgraphs."));
        Diagnostic->SetNumberField(TEXT("childGraphRefCount"), ChildGraphRefCount);
        Diagnostics.Add(MakeShared<FJsonValueObject>(Diagnostic));
    }

    TArray<FString> SignatureNodeTokens;
    TArray<FString> SignatureEdgeTokens;
    for (const TSharedPtr<FJsonValue>& NodeValue : Nodes)
    {
        const TSharedPtr<FJsonObject>* NodeObj = nullptr;
        if (NodeValue.IsValid() && NodeValue->TryGetObject(NodeObj) && NodeObj != nullptr && (*NodeObj).IsValid())
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
        if (EdgeValue.IsValid() && EdgeValue->TryGetObject(EdgeObj) && EdgeObj != nullptr && (*EdgeObj).IsValid())
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

    TSharedPtr<FJsonObject> Snapshot = MakeShared<FJsonObject>();
    Snapshot->SetStringField(TEXT("signature"), Signature);
    Snapshot->SetArrayField(TEXT("nodes"), Nodes);
    Snapshot->SetArrayField(TEXT("edges"), Edges);

    TSharedPtr<FJsonObject> LayoutCapabilities = MakeShared<FJsonObject>();
    LayoutCapabilities->SetBoolField(TEXT("canReadPosition"), true);
    LayoutCapabilities->SetBoolField(TEXT("canReadSize"), false);
    LayoutCapabilities->SetBoolField(TEXT("canReadBounds"), false);
    LayoutCapabilities->SetBoolField(TEXT("canMoveNode"), true);
    LayoutCapabilities->SetBoolField(TEXT("canBatchMove"), true);
    LayoutCapabilities->SetBoolField(TEXT("supportsMeasuredGeometry"), false);
    LayoutCapabilities->SetStringField(TEXT("positionSource"), TEXT("model"));
    LayoutCapabilities->SetStringField(TEXT("sizeSource"), TEXT("unsupported"));

    TSharedPtr<FJsonObject> Meta = MakeShared<FJsonObject>();
    Meta->SetNumberField(TEXT("totalNodes"), Nodes.Num());
    Meta->SetNumberField(TEXT("returnedNodes"), Nodes.Num());
    Meta->SetNumberField(TEXT("totalEdges"), Edges.Num());
    Meta->SetNumberField(TEXT("returnedEdges"), Edges.Num());
    Meta->SetBoolField(TEXT("truncated"), false);
    Meta->SetObjectField(TEXT("layoutCapabilities"), LayoutCapabilities);
    Meta->SetStringField(TEXT("layoutDetailRequested"), TEXT("basic"));
    Meta->SetStringField(TEXT("layoutDetailApplied"), TEXT("basic"));

    Result->SetStringField(TEXT("assetPath"), AssetPath);
    Result->SetStringField(TEXT("graphName"), TEXT(""));
    Result->SetStringField(TEXT("revision"), FString::Printf(TEXT("pcg:%08x"), GetTypeHash(AssetPath + TEXT("||") + Signature)));
    Result->SetObjectField(TEXT("graphRef"), MakePcgGraphAssetRef(AssetPath));
    Result->SetObjectField(TEXT("semanticSnapshot"), Snapshot);
    Result->SetObjectField(TEXT("meta"), Meta);
    Result->SetArrayField(TEXT("diagnostics"), Diagnostics);
    Result->SetStringField(TEXT("nextCursor"), TEXT(""));
    return Result;
}

TSharedPtr<FJsonObject> FLoomleBridgeModule::BuildPcgMutateToolResult(const TSharedPtr<FJsonObject>& Arguments)
{
    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetBoolField(TEXT("isError"), false);

    FString AssetPath;
    if (!Arguments.IsValid()
        || !Arguments->TryGetStringField(TEXT("assetPath"), AssetPath)
        || AssetPath.TrimStartAndEnd().IsEmpty())
    {
        Result->SetBoolField(TEXT("isError"), true);
        Result->SetStringField(TEXT("code"), TEXT("INVALID_ARGUMENT"));
        Result->SetStringField(TEXT("message"), TEXT("pcg.mutate requires assetPath."));
        return Result;
    }
    AssetPath = NormalizeAssetPath(AssetPath);

    const TArray<TSharedPtr<FJsonValue>>* Ops = nullptr;
    if (!Arguments->TryGetArrayField(TEXT("ops"), Ops) || Ops == nullptr)
    {
        Result->SetBoolField(TEXT("isError"), true);
        Result->SetStringField(TEXT("code"), TEXT("INVALID_ARGUMENT"));
        Result->SetStringField(TEXT("message"), TEXT("arguments.ops must be an array."));
        return Result;
    }

    for (int32 Index = 0; Index < Ops->Num(); ++Index)
    {
        const TSharedPtr<FJsonObject>* OpObj = nullptr;
        if (!(*Ops)[Index].IsValid() || !(*Ops)[Index]->TryGetObject(OpObj) || OpObj == nullptr || !(*OpObj).IsValid())
        {
            continue;
        }

        FString Op;
        (*OpObj)->TryGetStringField(TEXT("op"), Op);
        if (Op.Equals(TEXT("runscript"), ESearchCase::IgnoreCase))
        {
            Result->SetBoolField(TEXT("isError"), true);
            Result->SetStringField(TEXT("code"), TEXT("UNSUPPORTED_OP"));
            Result->SetStringField(TEXT("message"), TEXT("pcg.mutate no longer supports runScript."));
            Result->SetBoolField(TEXT("applied"), false);
            Result->SetBoolField(TEXT("partialApplied"), false);
            Result->SetStringField(TEXT("graphType"), TEXT("pcg"));
            Result->SetStringField(TEXT("assetPath"), AssetPath);
            Result->SetStringField(TEXT("graphName"), TEXT(""));
            Result->SetObjectField(TEXT("graphRef"), MakePcgGraphAssetRef(AssetPath));
            Result->SetStringField(TEXT("previousRevision"), TEXT(""));
            Result->SetStringField(TEXT("newRevision"), TEXT(""));

            TSharedPtr<FJsonObject> OpResult = MakeShared<FJsonObject>();
            OpResult->SetNumberField(TEXT("index"), Index);
            OpResult->SetStringField(TEXT("op"), Op);
            OpResult->SetBoolField(TEXT("ok"), false);
            OpResult->SetBoolField(TEXT("skipped"), false);
            OpResult->SetBoolField(TEXT("changed"), false);
            OpResult->SetStringField(TEXT("errorCode"), TEXT("UNSUPPORTED_OP"));
            OpResult->SetStringField(TEXT("errorMessage"), TEXT("pcg.mutate no longer supports runScript."));
            Result->SetArrayField(TEXT("opResults"), {MakeShared<FJsonValueObject>(OpResult)});

            TSharedPtr<FJsonObject> Diagnostic = MakeShared<FJsonObject>();
            Diagnostic->SetStringField(TEXT("code"), TEXT("UNSUPPORTED_OP"));
            Diagnostic->SetStringField(TEXT("severity"), TEXT("error"));
            Diagnostic->SetStringField(TEXT("message"), TEXT("pcg.mutate no longer supports runScript."));
            Diagnostic->SetStringField(TEXT("sourceKind"), TEXT("mutate"));
            Diagnostic->SetStringField(TEXT("op"), Op);
            Result->SetArrayField(TEXT("diagnostics"), {MakeShared<FJsonValueObject>(Diagnostic)});
            return Result;
        }
    }

    auto BuildRevisionQueryArgs = [&]() -> TSharedPtr<FJsonObject>
    {
        TSharedPtr<FJsonObject> QueryArgs = MakeShared<FJsonObject>();
        QueryArgs->SetStringField(TEXT("assetPath"), AssetPath);
        return QueryArgs;
    };

    auto ResolveRevision = [&](FString& OutRevision, FString& OutCode, FString& OutMessage) -> bool
    {
        OutRevision.Empty();
        OutCode.Empty();
        OutMessage.Empty();

        const TSharedPtr<FJsonObject> QueryResult = BuildPcgQueryToolResult(BuildRevisionQueryArgs());
        if (!QueryResult.IsValid())
        {
            OutCode = TEXT("INTERNAL_ERROR");
            OutMessage = TEXT("Failed to resolve current PCG revision.");
            return false;
        }

        bool bQueryError = false;
        QueryResult->TryGetBoolField(TEXT("isError"), bQueryError);
        if (bQueryError)
        {
            QueryResult->TryGetStringField(TEXT("code"), OutCode);
            QueryResult->TryGetStringField(TEXT("message"), OutMessage);
            if (OutCode.IsEmpty())
            {
                OutCode = TEXT("INTERNAL_ERROR");
            }
            if (OutMessage.IsEmpty())
            {
                OutMessage = TEXT("Failed to resolve current PCG revision.");
            }
            return false;
        }

        if (!QueryResult->TryGetStringField(TEXT("revision"), OutRevision) || OutRevision.IsEmpty())
        {
            OutCode = TEXT("INTERNAL_ERROR");
            OutMessage = TEXT("pcg.query did not return a revision for pcg.mutate.");
            return false;
        }

        return true;
    };

    FString PreviousRevision;
    FString PreviousRevisionCode;
    FString PreviousRevisionMessage;
    if (!ResolveRevision(PreviousRevision, PreviousRevisionCode, PreviousRevisionMessage))
    {
        Result->SetBoolField(TEXT("isError"), true);
        Result->SetStringField(TEXT("code"), PreviousRevisionCode);
        Result->SetStringField(TEXT("message"), PreviousRevisionMessage);
        return Result;
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
            FString::Printf(TEXT("expectedRevision mismatch: expected %s but current revision is %s."), *ExpectedRevision, *PreviousRevision));
        Result->SetBoolField(TEXT("applied"), false);
        Result->SetBoolField(TEXT("partialApplied"), false);
        Result->SetStringField(TEXT("graphType"), TEXT("pcg"));
        Result->SetStringField(TEXT("assetPath"), AssetPath);
        Result->SetStringField(TEXT("graphName"), TEXT(""));
        Result->SetObjectField(TEXT("graphRef"), MakePcgGraphAssetRef(AssetPath));
        Result->SetStringField(TEXT("previousRevision"), PreviousRevision);
        Result->SetStringField(TEXT("newRevision"), PreviousRevision);
        Result->SetArrayField(TEXT("opResults"), {});
        Result->SetArrayField(TEXT("diagnostics"), {});
        return Result;
    }

    FString IdempotencyKey;
    Arguments->TryGetStringField(TEXT("idempotencyKey"), IdempotencyKey);
    IdempotencyKey = IdempotencyKey.TrimStartAndEnd();
    const FString IdempotencyRegistryKey = IdempotencyKey.IsEmpty()
        ? FString()
        : FString::Printf(TEXT("pcg|%s|%s"), *AssetPath, *IdempotencyKey);
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
        RequestFingerprint = SerializeBlueprintJsonObjectCondensed(FingerprintSource);

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
                Result->SetStringField(TEXT("message"), TEXT("idempotencyKey was already used for a different pcg.mutate request in this graph scope."));
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

    bool bDryRun = false;
    Arguments->TryGetBoolField(TEXT("dryRun"), bDryRun);

    bool bStopOnError = true;
    bool bContinueOnError = false;
    Arguments->TryGetBoolField(TEXT("continueOnError"), bContinueOnError);
    if (bContinueOnError)
    {
        bStopOnError = false;
    }

    TArray<TSharedPtr<FJsonValue>> OpResults;
    TArray<TSharedPtr<FJsonValue>> Diagnostics;
    bool bAnyError = false;
    bool bAnyChanged = false;
    FString FirstErrorCode;
    FString FirstErrorMessage;
    TMap<FString, FString> NodeRefs;
    TArray<FString> PendingLayoutNodeIds;

    auto ResolveSingleNodeToken = [&NodeRefs](const TSharedPtr<FJsonObject>& Obj, FString& OutNodeId) -> bool
    {
        OutNodeId.Empty();
        if (!Obj.IsValid())
        {
            return false;
        }

        const TSharedPtr<FJsonObject>* SourceObj = &Obj;
        const TSharedPtr<FJsonObject>* TargetObj = nullptr;
        if (Obj->TryGetObjectField(TEXT("target"), TargetObj) && TargetObj != nullptr && (*TargetObj).IsValid())
        {
            SourceObj = TargetObj;
        }

        if ((*SourceObj)->TryGetStringField(TEXT("nodeId"), OutNodeId) && !OutNodeId.IsEmpty())
        {
            return true;
        }

        FString NodeRef;
        if ((*SourceObj)->TryGetStringField(TEXT("nodeRef"), NodeRef) && !NodeRef.IsEmpty() && NodeRefs.Contains(NodeRef))
        {
            OutNodeId = NodeRefs[NodeRef];
            return !OutNodeId.IsEmpty();
        }

        if ((*SourceObj)->TryGetStringField(TEXT("nodePath"), OutNodeId) && !OutNodeId.IsEmpty())
        {
            return true;
        }
        if ((*SourceObj)->TryGetStringField(TEXT("path"), OutNodeId) && !OutNodeId.IsEmpty())
        {
            return true;
        }
        return false;
    };

    auto ReadIntField = [](const TSharedPtr<FJsonObject>& Obj, const TCHAR* FieldName, int32 DefaultValue) -> int32
    {
        double Number = static_cast<double>(DefaultValue);
        if (Obj.IsValid() && Obj->TryGetNumberField(FieldName, Number))
        {
            return static_cast<int32>(Number);
        }
        return DefaultValue;
    };

    auto ReadJsonFieldAsString = [](const TSharedPtr<FJsonObject>& Obj, const TCHAR* FieldName, FString& OutValue) -> bool
    {
        OutValue.Empty();
        if (!Obj.IsValid() || !Obj->HasField(FieldName))
        {
            return false;
        }

        if (Obj->TryGetStringField(FieldName, OutValue))
        {
            return true;
        }

        double Number = 0.0;
        if (Obj->TryGetNumberField(FieldName, Number))
        {
            if (FMath::IsNearlyEqual(Number, FMath::RoundToDouble(Number)))
            {
                OutValue = FString::Printf(TEXT("%.0f"), Number);
            }
            else
            {
                OutValue = FString::SanitizeFloat(Number);
            }
            return true;
        }

        bool bBool = false;
        if (Obj->TryGetBoolField(FieldName, bBool))
        {
            OutValue = bBool ? TEXT("true") : TEXT("false");
            return true;
        }

        return false;
    };

    auto ResolvePlacement = [&AssetPath, &ResolveSingleNodeToken, &ReadIntField](const TSharedPtr<FJsonObject>& Obj, int32& OutX, int32& OutY) -> bool
    {
        OutX = ReadIntField(Obj, TEXT("x"), 0);
        OutY = ReadIntField(Obj, TEXT("y"), 0);
        if (Obj.IsValid() && Obj->HasField(TEXT("x")) && Obj->HasField(TEXT("y")))
        {
            return true;
        }

        UPCGGraph* PcgGraph = LoadPcgGraphByAssetPath(AssetPath);
        if (PcgGraph == nullptr)
        {
            return false;
        }

        FString AnchorNodeId;
        auto TryAnchor = [&](const TCHAR* FieldName) -> bool
        {
            const TSharedPtr<FJsonObject>* AnchorObj = nullptr;
            if (!Obj.IsValid()
                || !Obj->TryGetObjectField(FieldName, AnchorObj)
                || AnchorObj == nullptr
                || !(*AnchorObj).IsValid()
                || !ResolveSingleNodeToken(*AnchorObj, AnchorNodeId))
            {
                return false;
            }

            if (UPCGNode* AnchorNode = FindPcgNodeById(PcgGraph, AnchorNodeId))
            {
                AnchorNode->GetNodePosition(OutX, OutY);
                OutX += 376;
                return true;
            }
            return false;
        };

        if (TryAnchor(TEXT("anchor")) || TryAnchor(TEXT("near")) || TryAnchor(TEXT("from")) || TryAnchor(TEXT("target")))
        {
            return true;
        }

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
            OutX = RightmostX + 376;
        }
        return true;
    };

    for (int32 Index = 0; Index < Ops->Num(); ++Index)
    {
        const TSharedPtr<FJsonObject>* OpObj = nullptr;
        if (!(*Ops)[Index].IsValid() || !(*Ops)[Index]->TryGetObject(OpObj) || OpObj == nullptr || !(*OpObj).IsValid())
        {
            continue;
        }

        TSharedPtr<FJsonObject> SingleOp = CloneJsonObject(*OpObj);
        if (!SingleOp.IsValid())
        {
            SingleOp = *OpObj;
        }

        FString ClientRef;
        SingleOp->TryGetStringField(TEXT("clientRef"), ClientRef);

        auto RewriteNodeRefField = [&NodeRefs](TSharedPtr<FJsonObject>& Obj, const TCHAR* FieldName)
        {
            const TSharedPtr<FJsonObject>* TargetObj = nullptr;
            if (Obj.IsValid()
                && Obj->TryGetObjectField(FieldName, TargetObj)
                && TargetObj != nullptr
                && (*TargetObj).IsValid())
            {
                FString NodeRef;
                if ((*TargetObj)->TryGetStringField(TEXT("nodeRef"), NodeRef) && !NodeRef.IsEmpty() && NodeRefs.Contains(NodeRef))
                {
                    TSharedPtr<FJsonObject> Rewritten = CloneJsonObject(*TargetObj);
                    if (!Rewritten.IsValid())
                    {
                        Rewritten = *TargetObj;
                    }
                    Rewritten->RemoveField(TEXT("nodeRef"));
                    Rewritten->SetStringField(TEXT("nodeId"), NodeRefs[NodeRef]);
                    Obj->SetObjectField(FieldName, Rewritten);
                }
            }
        };

        RewriteNodeRefField(SingleOp, TEXT("from"));
        RewriteNodeRefField(SingleOp, TEXT("to"));
        RewriteNodeRefField(SingleOp, TEXT("target"));

        FString DirectNodeRef;
        if (SingleOp->TryGetStringField(TEXT("nodeRef"), DirectNodeRef) && !DirectNodeRef.IsEmpty() && NodeRefs.Contains(DirectNodeRef))
        {
            SingleOp->RemoveField(TEXT("nodeRef"));
            SingleOp->SetStringField(TEXT("nodeId"), NodeRefs[DirectNodeRef]);
        }

        FString OpName;
        SingleOp->TryGetStringField(TEXT("op"), OpName);
        OpName = OpName.ToLower();

        auto BuildDirectSingleResult = [&](bool bOk, bool bChanged, const FString& ErrorCode, const FString& ErrorMessage, const FString& NodeId = TEXT("")) -> TSharedPtr<FJsonObject>
        {
            TSharedPtr<FJsonObject> DirectResult = MakeShared<FJsonObject>();
            DirectResult->SetBoolField(TEXT("isError"), !bOk);
            if (!bOk)
            {
                DirectResult->SetStringField(TEXT("code"), ErrorCode.IsEmpty() ? TEXT("INTERNAL_ERROR") : ErrorCode);
                DirectResult->SetStringField(TEXT("message"), ErrorMessage.IsEmpty() ? TEXT("pcg.mutate failed") : ErrorMessage);
            }

            TSharedPtr<FJsonObject> DirectOpResult = MakeShared<FJsonObject>();
            DirectOpResult->SetNumberField(TEXT("index"), 0);
            DirectOpResult->SetStringField(TEXT("op"), OpName);
            DirectOpResult->SetBoolField(TEXT("ok"), bOk);
            DirectOpResult->SetBoolField(TEXT("skipped"), false);
            DirectOpResult->SetBoolField(TEXT("changed"), bChanged);
            DirectOpResult->SetStringField(TEXT("errorCode"), bOk ? TEXT("") : (ErrorCode.IsEmpty() ? TEXT("INTERNAL_ERROR") : ErrorCode));
            DirectOpResult->SetStringField(TEXT("errorMessage"), bOk ? TEXT("") : ErrorMessage);
            if (!NodeId.IsEmpty())
            {
                DirectOpResult->SetStringField(TEXT("nodeId"), NodeId);
            }
            DirectResult->SetArrayField(TEXT("opResults"), {MakeShared<FJsonValueObject>(DirectOpResult)});

            TArray<TSharedPtr<FJsonValue>> DirectDiagnostics;
            if (!bOk)
            {
                TSharedPtr<FJsonObject> Diagnostic = MakeShared<FJsonObject>();
                Diagnostic->SetStringField(TEXT("code"), ErrorCode.IsEmpty() ? TEXT("INTERNAL_ERROR") : ErrorCode);
                Diagnostic->SetStringField(TEXT("severity"), TEXT("error"));
                Diagnostic->SetStringField(TEXT("message"), ErrorMessage);
                Diagnostic->SetStringField(TEXT("sourceKind"), TEXT("mutate"));
                Diagnostic->SetStringField(TEXT("op"), OpName);
                if (!NodeId.IsEmpty())
                {
                    Diagnostic->SetStringField(TEXT("nodeId"), NodeId);
                }
                DirectDiagnostics.Add(MakeShared<FJsonValueObject>(Diagnostic));
            }
            DirectResult->SetArrayField(TEXT("diagnostics"), DirectDiagnostics);
            return DirectResult;
        };

        TSharedPtr<FJsonObject> SingleResult;
        if (!ClientRef.IsEmpty() && NodeRefs.Contains(ClientRef))
        {
            SingleResult = BuildDirectSingleResult(
                false,
                false,
                TEXT("INVALID_ARGUMENT"),
                FString::Printf(TEXT("Duplicate clientRef: %s"), *ClientRef));
        }
        else if (OpName.Equals(TEXT("addnode.byclass")))
        {
            FString SettingsClassPath;
            if (!SingleOp->TryGetStringField(TEXT("nodeClassPath"), SettingsClassPath) || SettingsClassPath.IsEmpty())
            {
                SingleOp->TryGetStringField(TEXT("nodeClass"), SettingsClassPath);
            }

            if (SettingsClassPath.IsEmpty())
            {
                SingleResult = BuildDirectSingleResult(false, false, TEXT("INVALID_ARGUMENT"), TEXT("addNode.byClass requires nodeClassPath."));
            }
            else if (bDryRun)
            {
                SingleResult = BuildDirectSingleResult(true, false, TEXT(""), TEXT(""));
            }
            else
            {
                UPCGGraph* PcgGraph = LoadPcgGraphByAssetPath(AssetPath);
                UClass* SettingsClass = LoadObject<UClass>(nullptr, *SettingsClassPath);
                if (PcgGraph == nullptr)
                {
                    SingleResult = BuildDirectSingleResult(false, false, TEXT("ASSET_NOT_FOUND"), TEXT("PCG asset not found."));
                }
                else if (SettingsClass == nullptr || !SettingsClass->IsChildOf(UPCGSettings::StaticClass()))
                {
                    SingleResult = BuildDirectSingleResult(false, false, TEXT("INVALID_ARGUMENT"), TEXT("Invalid PCG settings class."));
                }
                else
                {
                    UPCGSettings* DefaultSettings = nullptr;
                    UPCGNode* NewNode = PcgGraph->AddNodeOfType(SettingsClass, DefaultSettings);
                    if (NewNode == nullptr)
                    {
                        SingleResult = BuildDirectSingleResult(false, false, TEXT("INTERNAL_ERROR"), TEXT("Failed to create PCG node."));
                    }
                    else
                    {
                        int32 X = 0;
                        int32 Y = 0;
                        ResolvePlacement(SingleOp, X, Y);
                        NewNode->SetNodePosition(X, Y);
                        PendingLayoutNodeIds.AddUnique(NewNode->GetPathName());
                        SingleResult = BuildDirectSingleResult(true, true, TEXT(""), TEXT(""), NewNode->GetPathName());
                    }
                }
            }
        }
        else if (OpName.Equals(TEXT("removenode")))
        {
            FString TargetNodeId;
            if (!ResolveSingleNodeToken(SingleOp, TargetNodeId))
            {
                SingleResult = BuildDirectSingleResult(false, false, TEXT("TARGET_NOT_FOUND"), TEXT("removeNode requires a stable target node reference."));
            }
            else if (bDryRun)
            {
                SingleResult = BuildDirectSingleResult(true, false, TEXT(""), TEXT(""), TargetNodeId);
            }
            else
            {
                UPCGGraph* PcgGraph = LoadPcgGraphByAssetPath(AssetPath);
                UPCGNode* Node = PcgGraph ? FindPcgNodeById(PcgGraph, TargetNodeId) : nullptr;
                if (Node == nullptr)
                {
                    SingleResult = BuildDirectSingleResult(false, false, TEXT("NODE_NOT_FOUND"), TEXT("PCG node not found."), TargetNodeId);
                }
                else
                {
                    if (PcgGraph != nullptr)
                    {
                        for (UPCGPin* InputPin : Node->GetInputPins())
                        {
                            if (InputPin == nullptr)
                            {
                                continue;
                            }
                            for (UPCGEdge* Edge : InputPin->Edges)
                            {
                                const UPCGPin* OtherPin = Edge ? Edge->GetOtherPin(InputPin) : nullptr;
                                if (OtherPin != nullptr && OtherPin->Node != nullptr)
                                {
                                    PendingLayoutNodeIds.AddUnique(OtherPin->Node->GetPathName());
                                }
                            }
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
                                if (OtherPin != nullptr && OtherPin->Node != nullptr)
                                {
                                    PendingLayoutNodeIds.AddUnique(OtherPin->Node->GetPathName());
                                }
                            }
                        }
                    }
                    PcgGraph->RemoveNode(Node);
                    SingleResult = BuildDirectSingleResult(true, true, TEXT(""), TEXT(""), TargetNodeId);
                }
            }
        }
        else if (OpName.Equals(TEXT("movenode")) || OpName.Equals(TEXT("movenodeby")))
        {
            FString TargetNodeId;
            if (!ResolveSingleNodeToken(SingleOp, TargetNodeId))
            {
                SingleResult = BuildDirectSingleResult(false, false, TEXT("TARGET_NOT_FOUND"), FString::Printf(TEXT("%s requires a target node reference."), *OpName));
            }
            else
            {
                UPCGGraph* PcgGraph = LoadPcgGraphByAssetPath(AssetPath);
                UPCGNode* Node = PcgGraph ? FindPcgNodeById(PcgGraph, TargetNodeId) : nullptr;
                if (Node == nullptr)
                {
                    SingleResult = BuildDirectSingleResult(false, false, TEXT("NODE_NOT_FOUND"), TEXT("PCG node not found."), TargetNodeId);
                }
                else if (bDryRun)
                {
                    SingleResult = BuildDirectSingleResult(true, false, TEXT(""), TEXT(""), TargetNodeId);
                }
                else
                {
                    int32 X = 0;
                    int32 Y = 0;
                    if (OpName.Equals(TEXT("movenodeby")))
                    {
                        int32 CurrentX = 0;
                        int32 CurrentY = 0;
                        Node->GetNodePosition(CurrentX, CurrentY);
                        X = CurrentX + ReadIntField(SingleOp, TEXT("dx"), ReadIntField(SingleOp, TEXT("deltaX"), 0));
                        Y = CurrentY + ReadIntField(SingleOp, TEXT("dy"), ReadIntField(SingleOp, TEXT("deltaY"), 0));
                    }
                    else
                    {
                        X = ReadIntField(SingleOp, TEXT("x"), 0);
                        Y = ReadIntField(SingleOp, TEXT("y"), 0);
                    }
                    Node->SetNodePosition(X, Y);
                    PendingLayoutNodeIds.AddUnique(TargetNodeId);
                    SingleResult = BuildDirectSingleResult(true, true, TEXT(""), TEXT(""), TargetNodeId);
                }
            }
        }
        else if (OpName.Equals(TEXT("movenodes")))
        {
            const TArray<TSharedPtr<FJsonValue>>* NodeIdsArray = nullptr;
            if (!SingleOp->TryGetArrayField(TEXT("nodeIds"), NodeIdsArray) || NodeIdsArray == nullptr || NodeIdsArray->Num() == 0)
            {
                SingleResult = BuildDirectSingleResult(false, false, TEXT("INVALID_ARGUMENT"), TEXT("moveNodes requires nodeIds."));
            }
            else if (bDryRun)
            {
                SingleResult = BuildDirectSingleResult(true, false, TEXT(""), TEXT(""));
            }
            else
            {
                UPCGGraph* PcgGraph = LoadPcgGraphByAssetPath(AssetPath);
                if (PcgGraph == nullptr)
                {
                    SingleResult = BuildDirectSingleResult(false, false, TEXT("ASSET_NOT_FOUND"), TEXT("PCG asset not found."));
                }
                else
                {
                    const int32 Dx = ReadIntField(SingleOp, TEXT("dx"), ReadIntField(SingleOp, TEXT("deltaX"), 0));
                    const int32 Dy = ReadIntField(SingleOp, TEXT("dy"), ReadIntField(SingleOp, TEXT("deltaY"), 0));
                    FString Error;
                    bool bOk = true;
                    for (const TSharedPtr<FJsonValue>& NodeIdValue : *NodeIdsArray)
                    {
                        FString NodeId;
                        if (!NodeIdValue.IsValid() || !NodeIdValue->TryGetString(NodeId) || NodeId.IsEmpty())
                        {
                            continue;
                        }
                        UPCGNode* Node = FindPcgNodeById(PcgGraph, NodeId);
                        if (Node == nullptr)
                        {
                            bOk = false;
                            Error = FString::Printf(TEXT("PCG node not found: %s"), *NodeId);
                            break;
                        }
                        int32 CurrentX = 0;
                        int32 CurrentY = 0;
                        Node->GetNodePosition(CurrentX, CurrentY);
                        Node->SetNodePosition(CurrentX + Dx, CurrentY + Dy);
                        PendingLayoutNodeIds.AddUnique(NodeId);
                    }
                    SingleResult = BuildDirectSingleResult(bOk, bOk, TEXT(""), Error);
                }
            }
        }
        else if (OpName.Equals(TEXT("setpindefault")))
        {
            const TSharedPtr<FJsonObject>* TargetObj = nullptr;
            if (!SingleOp->TryGetObjectField(TEXT("target"), TargetObj) || TargetObj == nullptr || !(*TargetObj).IsValid())
            {
                SingleResult = BuildDirectSingleResult(false, false, TEXT("INVALID_ARGUMENT"), TEXT("setPinDefault requires target."));
            }
            else
            {
                FString TargetNodeId;
                FString PinName;
                FString Value;
                ResolveSingleNodeToken(*TargetObj, TargetNodeId);
                (*TargetObj)->TryGetStringField(TEXT("pin"), PinName);
                if (PinName.IsEmpty())
                {
                    (*TargetObj)->TryGetStringField(TEXT("pinName"), PinName);
                }

                if (TargetNodeId.IsEmpty())
                {
                    SingleResult = BuildDirectSingleResult(false, false, TEXT("TARGET_NOT_FOUND"), TEXT("setPinDefault requires a stable target node reference."));
                }
                else if (PinName.IsEmpty())
                {
                    SingleResult = BuildDirectSingleResult(false, false, TEXT("PIN_NOT_FOUND"), TEXT("setPinDefault requires target.pin."));
                }
                else if (!ReadJsonFieldAsString(SingleOp, TEXT("value"), Value))
                {
                    SingleResult = BuildDirectSingleResult(false, false, TEXT("INVALID_ARGUMENT"), TEXT("setPinDefault requires value."), TargetNodeId);
                }
                else if (bDryRun)
                {
                    SingleResult = BuildDirectSingleResult(true, false, TEXT(""), TEXT(""), TargetNodeId);
                }
                else
                {
                    UPCGGraph* PcgGraph = LoadPcgGraphByAssetPath(AssetPath);
                    UPCGNode* Node = PcgGraph ? FindPcgNodeById(PcgGraph, TargetNodeId) : nullptr;
                    if (PcgGraph == nullptr)
                    {
                        SingleResult = BuildDirectSingleResult(false, false, TEXT("ASSET_NOT_FOUND"), TEXT("PCG asset not found."));
                    }
                    else if (Node == nullptr)
                    {
                        SingleResult = BuildDirectSingleResult(false, false, TEXT("NODE_NOT_FOUND"), TEXT("PCG node not found."), TargetNodeId);
                    }
                    else
                    {
                        FString Error;
                        const bool bOk = SetPcgPinDefaultValue(Node, PinName, Value, Error);
                        if (bOk)
                        {
                            PendingLayoutNodeIds.AddUnique(TargetNodeId);
                        }
                        SingleResult = BuildDirectSingleResult(
                            bOk,
                            bOk,
                            bOk ? TEXT("") : TEXT("PIN_NOT_FOUND"),
                            Error.IsEmpty() ? TEXT("Failed to set PCG pin default value.") : Error,
                            TargetNodeId);
                    }
                }
            }
        }
        else if (OpName.Equals(TEXT("connectpins")) || OpName.Equals(TEXT("disconnectpins")))
        {
            const TSharedPtr<FJsonObject>* FromObj = nullptr;
            const TSharedPtr<FJsonObject>* ToObj = nullptr;
            if (!SingleOp->TryGetObjectField(TEXT("from"), FromObj) || FromObj == nullptr || !(*FromObj).IsValid()
                || !SingleOp->TryGetObjectField(TEXT("to"), ToObj) || ToObj == nullptr || !(*ToObj).IsValid())
            {
                SingleResult = BuildDirectSingleResult(false, false, TEXT("INVALID_ARGUMENT"), FString::Printf(TEXT("%s requires from and to pin references."), *OpName));
            }
            else
            {
                FString FromNodeId;
                FString ToNodeId;
                FString FromPinName;
                FString ToPinName;
                ResolveSingleNodeToken(*FromObj, FromNodeId);
                ResolveSingleNodeToken(*ToObj, ToNodeId);
                (*FromObj)->TryGetStringField(TEXT("pin"), FromPinName);
                if (FromPinName.IsEmpty())
                {
                    (*FromObj)->TryGetStringField(TEXT("pinName"), FromPinName);
                }
                (*ToObj)->TryGetStringField(TEXT("pin"), ToPinName);
                if (ToPinName.IsEmpty())
                {
                    (*ToObj)->TryGetStringField(TEXT("pinName"), ToPinName);
                }

                if (FromNodeId.IsEmpty() || ToNodeId.IsEmpty())
                {
                    SingleResult = BuildDirectSingleResult(false, false, TEXT("TARGET_NOT_FOUND"), FString::Printf(TEXT("%s requires resolvable from/to node references."), *OpName));
                }
                else if (FromPinName.IsEmpty() || ToPinName.IsEmpty())
                {
                    SingleResult = BuildDirectSingleResult(false, false, TEXT("INVALID_ARGUMENT"), FString::Printf(TEXT("%s requires from/to pin names."), *OpName));
                }
                else if (bDryRun)
                {
                    SingleResult = BuildDirectSingleResult(true, false, TEXT(""), TEXT(""));
                }
                else
                {
                    UPCGGraph* PcgGraph = LoadPcgGraphByAssetPath(AssetPath);
                    UPCGNode* FromNode = PcgGraph ? FindPcgNodeById(PcgGraph, FromNodeId) : nullptr;
                    UPCGNode* ToNode = PcgGraph ? FindPcgNodeById(PcgGraph, ToNodeId) : nullptr;
                    UPCGPin* FromPin = FindPcgPin(FromNode, FromPinName, true);
                    UPCGPin* ToPin = FindPcgPin(ToNode, ToPinName, false);
                    if (PcgGraph == nullptr)
                    {
                        SingleResult = BuildDirectSingleResult(false, false, TEXT("ASSET_NOT_FOUND"), TEXT("PCG asset not found."));
                    }
                    else if (FromNode == nullptr)
                    {
                        SingleResult = BuildDirectSingleResult(false, false, TEXT("NODE_NOT_FOUND"), TEXT("Source PCG node not found."), FromNodeId);
                    }
                    else if (ToNode == nullptr)
                    {
                        SingleResult = BuildDirectSingleResult(false, false, TEXT("NODE_NOT_FOUND"), TEXT("Target PCG node not found."), ToNodeId);
                    }
                    else if (FromPin == nullptr)
                    {
                        SingleResult = BuildDirectSingleResult(false, false, TEXT("PIN_NOT_FOUND"), TEXT("Source PCG output pin not found."), FromNodeId);
                    }
                    else if (ToPin == nullptr)
                    {
                        SingleResult = BuildDirectSingleResult(false, false, TEXT("PIN_NOT_FOUND"), TEXT("Target PCG input pin not found."), ToNodeId);
                    }
                    else if (OpName.Equals(TEXT("connectpins")))
                    {
                        const UPCGNode* ConnectedNode = PcgGraph->AddEdge(
                            FromNode,
                            FromPin->Properties.Label,
                            ToNode,
                            ToPin->Properties.Label);
                        if (ConnectedNode == ToNode)
                        {
                            PendingLayoutNodeIds.AddUnique(FromNodeId);
                            PendingLayoutNodeIds.AddUnique(ToNodeId);
                            SingleResult = BuildDirectSingleResult(true, true, TEXT(""), TEXT(""), ToNodeId);
                        }
                        else
                        {
                            SingleResult = BuildDirectSingleResult(false, false, TEXT("INTERNAL_ERROR"), TEXT("Failed to connect PCG pins."), ToNodeId);
                        }
                    }
                    else
                    {
                        const bool bRemoved = PcgGraph->RemoveEdge(
                            FromNode,
                            FromPin->Properties.Label,
                            ToNode,
                            ToPin->Properties.Label);
                        if (bRemoved)
                        {
                            PendingLayoutNodeIds.AddUnique(FromNodeId);
                            PendingLayoutNodeIds.AddUnique(ToNodeId);
                            SingleResult = BuildDirectSingleResult(true, true, TEXT(""), TEXT(""), ToNodeId);
                        }
                        else
                        {
                            SingleResult = BuildDirectSingleResult(false, false, TEXT("TARGET_NOT_FOUND"), TEXT("Requested PCG connection was not found."), ToNodeId);
                        }
                    }
                }
            }
        }
        else if (OpName.Equals(TEXT("layoutgraph")))
        {
            if (bDryRun)
            {
                SingleResult = BuildDirectSingleResult(true, false, TEXT(""), TEXT(""));
            }
            else
            {
                FString Scope;
                SingleOp->TryGetStringField(TEXT("scope"), Scope);
                if (Scope.IsEmpty())
                {
                    Scope = TEXT("all");
                }

                TArray<FString> RequestedNodeIds;
                const TArray<TSharedPtr<FJsonValue>>* NodeIdsArray = nullptr;
                if (SingleOp->TryGetArrayField(TEXT("nodeIds"), NodeIdsArray) && NodeIdsArray != nullptr)
                {
                    for (const TSharedPtr<FJsonValue>& NodeIdValue : *NodeIdsArray)
                    {
                        FString NodeId;
                        if (NodeIdValue.IsValid() && NodeIdValue->TryGetString(NodeId) && !NodeId.IsEmpty())
                        {
                            RequestedNodeIds.Add(NodeId);
                        }
                    }
                }
                else if (Scope.Equals(TEXT("touched")))
                {
                    RequestedNodeIds = PendingLayoutNodeIds;
                }

                TArray<FString> MovedNodeIds;
                FString Error;
                const bool bOk = ApplyPcgLayout(AssetPath, TEXT(""), Scope, RequestedNodeIds, MovedNodeIds, Error);
                SingleResult = BuildDirectSingleResult(bOk, bOk && MovedNodeIds.Num() > 0, TEXT(""), Error);
                if (SingleResult.IsValid())
                {
                    const TArray<TSharedPtr<FJsonValue>>* SingleOpResults = nullptr;
                    if (SingleResult->TryGetArrayField(TEXT("opResults"), SingleOpResults) && SingleOpResults != nullptr && SingleOpResults->Num() > 0)
                    {
                        const TSharedPtr<FJsonObject>* SingleOpResult = nullptr;
                        if ((*SingleOpResults)[0].IsValid() && (*SingleOpResults)[0]->TryGetObject(SingleOpResult) && SingleOpResult != nullptr && (*SingleOpResult).IsValid())
                        {
                            TArray<TSharedPtr<FJsonValue>> MovedNodeValues;
                            for (const FString& MovedNodeId : MovedNodeIds)
                            {
                                MovedNodeValues.Add(MakeShared<FJsonValueString>(MovedNodeId));
                            }
                            (*SingleOpResult)->SetArrayField(TEXT("movedNodeIds"), MovedNodeValues);
                        }
                    }
                    if (bOk)
                    {
                        PendingLayoutNodeIds.Reset();
                    }
                }
            }
        }
        else if (OpName.Equals(TEXT("compile")))
        {
            if (bDryRun)
            {
                SingleResult = BuildDirectSingleResult(true, false, TEXT(""), TEXT(""));
            }
            else
            {
                UPCGGraph* PcgGraph = LoadPcgGraphByAssetPath(AssetPath);
                if (PcgGraph == nullptr)
                {
                    SingleResult = BuildDirectSingleResult(false, false, TEXT("ASSET_NOT_FOUND"), TEXT("PCG asset not found."));
                }
                else
                {
                    const bool bCompilationChanged = PcgGraph->Recompile();
                    SingleResult = BuildDirectSingleResult(true, bCompilationChanged, TEXT(""), TEXT(""));
                }
            }
        }

        if (!SingleResult.IsValid())
        {
            TSharedPtr<FJsonObject> UnsupportedOpResult = MakeShared<FJsonObject>();
            UnsupportedOpResult->SetNumberField(TEXT("index"), Index);
            UnsupportedOpResult->SetStringField(TEXT("op"), OpName);
            UnsupportedOpResult->SetBoolField(TEXT("ok"), false);
            UnsupportedOpResult->SetBoolField(TEXT("skipped"), false);
            UnsupportedOpResult->SetBoolField(TEXT("changed"), false);
            UnsupportedOpResult->SetStringField(TEXT("errorCode"), TEXT("UNSUPPORTED_OP"));
            UnsupportedOpResult->SetStringField(TEXT("errorMessage"), FString::Printf(TEXT("pcg.mutate does not support op '%s'."), *OpName));
            OpResults.Add(MakeShared<FJsonValueObject>(UnsupportedOpResult));

            TSharedPtr<FJsonObject> Diagnostic = MakeShared<FJsonObject>();
            Diagnostic->SetStringField(TEXT("code"), TEXT("UNSUPPORTED_OP"));
            Diagnostic->SetStringField(TEXT("severity"), TEXT("error"));
            Diagnostic->SetStringField(TEXT("message"), FString::Printf(TEXT("pcg.mutate does not support op '%s'."), *OpName));
            Diagnostic->SetStringField(TEXT("sourceKind"), TEXT("mutate"));
            Diagnostic->SetStringField(TEXT("op"), OpName);
            Diagnostics.Add(MakeShared<FJsonValueObject>(Diagnostic));

            bAnyError = true;
            if (FirstErrorCode.IsEmpty())
            {
                FirstErrorCode = TEXT("UNSUPPORTED_OP");
                FirstErrorMessage = FString::Printf(TEXT("pcg.mutate does not support op '%s'."), *OpName);
            }
            if (bStopOnError)
            {
                break;
            }
            continue;
        }

        Diagnostics.Append(CloneBlueprintJsonArrayField(SingleResult, TEXT("diagnostics")));

        bool bSingleError = false;
        SingleResult->TryGetBoolField(TEXT("isError"), bSingleError);
        const TArray<TSharedPtr<FJsonValue>> SingleOpResults = CloneBlueprintJsonArrayField(SingleResult, TEXT("opResults"));
        const TSharedPtr<FJsonObject>* FirstSingleOpResult = nullptr;
        if (SingleOpResults.Num() > 0
            && SingleOpResults[0].IsValid()
            && SingleOpResults[0]->TryGetObject(FirstSingleOpResult)
            && FirstSingleOpResult != nullptr
            && (*FirstSingleOpResult).IsValid())
        {
            TSharedPtr<FJsonObject> RewrittenOpResult = CloneJsonObject(*FirstSingleOpResult);
            if (!RewrittenOpResult.IsValid())
            {
                RewrittenOpResult = *FirstSingleOpResult;
            }
            RewrittenOpResult->SetNumberField(TEXT("index"), Index);
            OpResults.Add(MakeShared<FJsonValueObject>(RewrittenOpResult));

            FString CreatedNodeId;
            if (RewrittenOpResult->TryGetStringField(TEXT("nodeId"), CreatedNodeId) && !CreatedNodeId.IsEmpty() && !ClientRef.IsEmpty())
            {
                NodeRefs.FindOrAdd(ClientRef) = CreatedNodeId;
            }

            bool bChanged = false;
            RewrittenOpResult->TryGetBoolField(TEXT("changed"), bChanged);
            if (bChanged && !bDryRun)
            {
                bAnyChanged = true;
            }
        }

        if (bSingleError)
        {
            bAnyError = true;
            if (FirstErrorCode.IsEmpty())
            {
                SingleResult->TryGetStringField(TEXT("code"), FirstErrorCode);
            }
            if (FirstErrorMessage.IsEmpty())
            {
                SingleResult->TryGetStringField(TEXT("message"), FirstErrorMessage);
            }
            if (bStopOnError)
            {
                break;
            }
        }
    }

    Result->SetBoolField(TEXT("isError"), bAnyError);
    if (bAnyError)
    {
        Result->SetStringField(TEXT("code"), FirstErrorCode.IsEmpty() ? TEXT("INTERNAL_ERROR") : FirstErrorCode);
        Result->SetStringField(TEXT("message"), FirstErrorMessage.IsEmpty() ? TEXT("pcg.mutate failed") : FirstErrorMessage);
    }
    Result->SetBoolField(TEXT("applied"), !bAnyError);
    Result->SetBoolField(TEXT("partialApplied"), bAnyError && bAnyChanged);
    Result->SetStringField(TEXT("graphType"), TEXT("pcg"));
    Result->SetStringField(TEXT("assetPath"), AssetPath);
    Result->SetStringField(TEXT("graphName"), TEXT(""));
    Result->SetObjectField(TEXT("graphRef"), MakePcgGraphAssetRef(AssetPath));
    Result->SetStringField(TEXT("previousRevision"), PreviousRevision);

    FString NewRevision = PreviousRevision;
    if (!bDryRun && !bAnyError)
    {
        FString NewRevisionCode;
        FString NewRevisionMessage;
        if (ResolveRevision(NewRevision, NewRevisionCode, NewRevisionMessage))
        {
            Result->SetStringField(TEXT("newRevision"), NewRevision);
        }
        else
        {
            Result->SetStringField(TEXT("newRevision"), PreviousRevision);
        }
    }
    else
    {
        Result->SetStringField(TEXT("newRevision"), PreviousRevision);
    }

    Result->SetArrayField(TEXT("opResults"), OpResults);
    Result->SetArrayField(TEXT("diagnostics"), Diagnostics);

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

TSharedPtr<FJsonObject> FLoomleBridgeModule::BuildPcgVerifyToolResult(const TSharedPtr<FJsonObject>& Arguments)
{
    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

    FString AssetPath;
    if (!Arguments.IsValid()
        || !Arguments->TryGetStringField(TEXT("assetPath"), AssetPath)
        || AssetPath.TrimStartAndEnd().IsEmpty())
    {
        Result->SetBoolField(TEXT("isError"), true);
        Result->SetStringField(TEXT("code"), TEXT("INVALID_ARGUMENT"));
        Result->SetStringField(TEXT("message"), TEXT("pcg.verify requires assetPath."));
        return Result;
    }

    AssetPath = NormalizeAssetPath(AssetPath);

    const TSharedPtr<FJsonObject> QueryArgs = MakeShared<FJsonObject>();
    QueryArgs->SetStringField(TEXT("assetPath"), AssetPath);
    const TSharedPtr<FJsonObject> QueryResult = BuildPcgQueryToolResult(QueryArgs);
    bool bQueryError = false;
    QueryResult->TryGetBoolField(TEXT("isError"), bQueryError);
    if (bQueryError)
    {
        return QueryResult;
    }

    TSharedPtr<FJsonObject> MutateArgs = MakeShared<FJsonObject>();
    MutateArgs->SetStringField(TEXT("assetPath"), AssetPath);
    TArray<TSharedPtr<FJsonValue>> Ops;
    TSharedPtr<FJsonObject> CompileOp = MakeShared<FJsonObject>();
    CompileOp->SetStringField(TEXT("op"), TEXT("compile"));
    Ops.Add(MakeShared<FJsonValueObject>(CompileOp));
    MutateArgs->SetArrayField(TEXT("ops"), Ops);

    const TSharedPtr<FJsonObject> MutateResult = BuildPcgMutateToolResult(MutateArgs);
    bool bMutateError = false;
    MutateResult->TryGetBoolField(TEXT("isError"), bMutateError);

    Result->SetBoolField(TEXT("isError"), false);
    Result->SetStringField(TEXT("assetPath"), AssetPath);
    Result->SetStringField(TEXT("status"), bMutateError ? TEXT("error") : TEXT("ok"));
    Result->SetStringField(
        TEXT("summary"),
        bMutateError
            ? TEXT("PCG verification failed during compile-backed confirmation.")
            : TEXT("PCG verification succeeded with compile-backed confirmation."));

    TSharedPtr<FJsonObject> QueryReport = MakeShared<FJsonObject>();
    CopyBlueprintOptionalStringField(QueryResult, QueryReport, TEXT("revision"));
    CopyBlueprintOptionalObjectField(QueryResult, QueryReport, TEXT("meta"));
    QueryReport->SetArrayField(TEXT("diagnostics"), CloneBlueprintJsonArrayField(QueryResult, TEXT("diagnostics")));
    Result->SetObjectField(TEXT("queryReport"), QueryReport);

    TSharedPtr<FJsonObject> CompileReport = MakeShared<FJsonObject>();
    bool bApplied = false;
    bool bPartialApplied = false;
    MutateResult->TryGetBoolField(TEXT("applied"), bApplied);
    MutateResult->TryGetBoolField(TEXT("partialApplied"), bPartialApplied);
    CompileReport->SetBoolField(TEXT("compiled"), !bMutateError);
    CompileReport->SetBoolField(TEXT("compilationChanged"), bApplied);
    CompileReport->SetBoolField(TEXT("applied"), bApplied);
    CompileReport->SetBoolField(TEXT("partialApplied"), bPartialApplied);
    CompileReport->SetArrayField(TEXT("opResults"), CloneBlueprintJsonArrayField(MutateResult, TEXT("opResults")));
    CompileReport->SetArrayField(TEXT("diagnostics"), CloneBlueprintJsonArrayField(MutateResult, TEXT("diagnostics")));
    Result->SetObjectField(TEXT("compileReport"), CompileReport);

    TArray<TSharedPtr<FJsonValue>> Diagnostics = CloneBlueprintJsonArrayField(QueryResult, TEXT("diagnostics"));
    Diagnostics.Append(CloneBlueprintJsonArrayField(MutateResult, TEXT("diagnostics")));
    Result->SetArrayField(TEXT("diagnostics"), Diagnostics);

    if (bMutateError)
    {
        Result->SetStringField(TEXT("code"), TEXT("PCG_COMPILE_FAILED"));
        Result->SetStringField(TEXT("message"), TEXT("PCG graph compile failed."));
    }

    return Result;
}

TSharedPtr<FJsonObject> FLoomleBridgeModule::BuildPcgDescribeToolResult(const TSharedPtr<FJsonObject>& Arguments) const
{
    FString AssetPath;
    FString NodeId;
    FString NodeClass;
    if (Arguments.IsValid())
    {
        Arguments->TryGetStringField(TEXT("assetPath"), AssetPath);
        Arguments->TryGetStringField(TEXT("nodeId"), NodeId);
        Arguments->TryGetStringField(TEXT("nodeClass"), NodeClass);
    }

    AssetPath = NormalizeAssetPath(AssetPath);

    if (!AssetPath.IsEmpty() && !NodeId.IsEmpty())
    {
        TSharedPtr<FJsonObject> QueryArgs = MakeShared<FJsonObject>();
        QueryArgs->SetStringField(TEXT("assetPath"), AssetPath);
        QueryArgs->SetBoolField(TEXT("includeConnections"), true);
        TArray<TSharedPtr<FJsonValue>> NodeIds;
        NodeIds.Add(MakeShared<FJsonValueString>(NodeId));
        QueryArgs->SetArrayField(TEXT("nodeIds"), NodeIds);

        const TSharedPtr<FJsonObject> QueryResult = BuildPcgQueryToolResult(QueryArgs);
        bool bQueryError = false;
        QueryResult->TryGetBoolField(TEXT("isError"), bQueryError);
        if (bQueryError)
        {
            return QueryResult;
        }

        const TSharedPtr<FJsonObject>* Snapshot = nullptr;
        const TArray<TSharedPtr<FJsonValue>>* Nodes = nullptr;
        if (!QueryResult->TryGetObjectField(TEXT("semanticSnapshot"), Snapshot)
            || Snapshot == nullptr
            || !(*Snapshot).IsValid()
            || !(*Snapshot)->TryGetArrayField(TEXT("nodes"), Nodes)
            || Nodes == nullptr
            || Nodes->Num() == 0)
        {
            TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
            Result->SetBoolField(TEXT("isError"), true);
            Result->SetStringField(TEXT("code"), TEXT("NODE_NOT_FOUND"));
            Result->SetStringField(TEXT("message"), TEXT("PCG node not found."));
            return Result;
        }

        const TSharedPtr<FJsonObject>* NodeObject = nullptr;
        if (!(*Nodes)[0].IsValid() || !(*Nodes)[0]->TryGetObject(NodeObject) || NodeObject == nullptr || !(*NodeObject).IsValid())
        {
            TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
            Result->SetBoolField(TEXT("isError"), true);
            Result->SetStringField(TEXT("code"), TEXT("INTERNAL_ERROR"));
            Result->SetStringField(TEXT("message"), TEXT("PCG describe failed to read node snapshot."));
            return Result;
        }

        TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
        Result->SetBoolField(TEXT("isError"), false);
        Result->SetStringField(TEXT("mode"), TEXT("instance"));
        Result->SetStringField(TEXT("assetPath"), AssetPath);
        Result->SetStringField(TEXT("nodeId"), NodeId);
        Result->SetObjectField(TEXT("node"), CloneJsonObject(*NodeObject));
        return Result;
    }

    if (!NodeClass.IsEmpty())
    {
        return BuildPcgClassDescribeResult(NodeClass);
    }

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetBoolField(TEXT("isError"), true);
    Result->SetStringField(TEXT("code"), TEXT("INVALID_ARGUMENT"));
    Result->SetStringField(TEXT("message"), TEXT("pcg.describe requires assetPath+nodeId for instance mode or nodeClass for class mode."));
    return Result;
}
