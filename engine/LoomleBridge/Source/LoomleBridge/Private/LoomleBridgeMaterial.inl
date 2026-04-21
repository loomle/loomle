// Material-domain tool adapters.
namespace
{
UClass* ResolveMaterialExpressionDescribeClass(const FString& ClassToken)
{
    const FString TrimmedToken = ClassToken.TrimStartAndEnd();
    if (TrimmedToken.IsEmpty())
    {
        return nullptr;
    }

    auto AcceptClass = [](UClass* Candidate) -> UClass*
    {
        return Candidate != nullptr && Candidate->IsChildOf(UMaterialExpression::StaticClass()) ? Candidate : nullptr;
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
        if (!Candidate->IsChildOf(UMaterialExpression::StaticClass()))
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

bool ShouldDescribeMaterialProperty(const FProperty* Property)
{
    return Property != nullptr
        && Property->HasAnyPropertyFlags(CPF_Edit)
        && !Property->HasAnyPropertyFlags(CPF_Transient | CPF_Deprecated | CPF_Parm)
        && !Property->GetName().EndsWith(TEXT("_DEPRECATED"))
        && CastField<FArrayProperty>(Property) == nullptr
        && CastField<FSetProperty>(Property) == nullptr
        && CastField<FMapProperty>(Property) == nullptr;
}

TSharedPtr<FJsonObject> MakeMaterialDescribePropertyType(const FProperty* Property)
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

TSharedPtr<FJsonObject> BuildMaterialClassDescribeResult(const FString& NodeClass)
{
    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

    UClass* ExpressionClass = ResolveMaterialExpressionDescribeClass(NodeClass);
    if (ExpressionClass == nullptr)
    {
        Result->SetBoolField(TEXT("isError"), true);
        Result->SetStringField(TEXT("code"), TEXT("NODE_CLASS_NOT_FOUND"));
        Result->SetStringField(TEXT("message"), TEXT("Material expression class not found."));
        return Result;
    }

    UMaterialExpression* Expression = Cast<UMaterialExpression>(ExpressionClass->GetDefaultObject());
    if (Expression == nullptr)
    {
        Result->SetBoolField(TEXT("isError"), true);
        Result->SetStringField(TEXT("code"), TEXT("INTERNAL_ERROR"));
        Result->SetStringField(TEXT("message"), TEXT("Material expression default object is unavailable."));
        return Result;
    }

    Result->SetBoolField(TEXT("isError"), false);
    Result->SetStringField(TEXT("mode"), TEXT("class"));
    Result->SetStringField(TEXT("nodeClass"), ExpressionClass->GetPathName());

    TArray<FString> Captions;
    Expression->GetCaption(Captions);
    Result->SetStringField(TEXT("title"), Captions.Num() > 0 ? FString::Join(Captions, TEXT(" / ")) : ExpressionClass->GetName());

    TArray<TSharedPtr<FJsonValue>> InputPins;
    const int32 InputCount = Expression->CountInputs();
    for (int32 InputIndex = 0; InputIndex < InputCount; ++InputIndex)
    {
        TSharedPtr<FJsonObject> PinObject = MakeShared<FJsonObject>();
        const FName InputName = Expression->GetInputName(InputIndex);
        PinObject->SetStringField(TEXT("name"), InputName.IsNone() ? FString::Printf(TEXT("Input%d"), InputIndex) : InputName.ToString());
        PinObject->SetBoolField(TEXT("required"), Expression->IsInputConnectionRequired(InputIndex));
        PinObject->SetNumberField(TEXT("valueType"), static_cast<int32>(Expression->GetInputValueType(InputIndex)));
        InputPins.Add(MakeShared<FJsonValueObject>(PinObject));
    }
    Result->SetArrayField(TEXT("inputPins"), InputPins);

    TArray<TSharedPtr<FJsonValue>> OutputPins;
    TArray<FExpressionOutput>& Outputs = Expression->GetOutputs();
    for (int32 OutputIndex = 0; OutputIndex < Outputs.Num(); ++OutputIndex)
    {
        TSharedPtr<FJsonObject> PinObject = MakeShared<FJsonObject>();
        PinObject->SetStringField(TEXT("name"), Outputs[OutputIndex].OutputName.IsNone() ? FString::Printf(TEXT("Output%d"), OutputIndex) : Outputs[OutputIndex].OutputName.ToString());
        PinObject->SetNumberField(TEXT("valueType"), static_cast<int32>(Expression->GetOutputValueType(OutputIndex)));
        OutputPins.Add(MakeShared<FJsonValueObject>(PinObject));
    }
    Result->SetArrayField(TEXT("outputPins"), OutputPins);

    TArray<TSharedPtr<FJsonValue>> Properties;
    for (TFieldIterator<FProperty> It(ExpressionClass, EFieldIteratorFlags::IncludeSuper); It; ++It)
    {
        FProperty* Property = *It;
        if (!ShouldDescribeMaterialProperty(Property))
        {
            continue;
        }

        TSharedPtr<FJsonObject> PropertyObject = MakeShared<FJsonObject>();
        PropertyObject->SetStringField(TEXT("name"), Property->GetName());
        PropertyObject->SetStringField(TEXT("displayName"), Property->GetDisplayNameText().ToString());
        PropertyObject->SetObjectField(TEXT("type"), MakeMaterialDescribePropertyType(Property));

        FString DefaultValue;
        Property->ExportTextItem_InContainer(DefaultValue, Expression, Expression, Expression, PPF_None);
        if (!DefaultValue.IsEmpty())
        {
            PropertyObject->SetStringField(TEXT("defaultValue"), DefaultValue);
        }

        Properties.Add(MakeShared<FJsonValueObject>(PropertyObject));
    }
    Result->SetArrayField(TEXT("properties"), Properties);

    return Result;
}

TSharedPtr<FJsonObject> MakeMaterialGraphAssetRef(const FString& AssetPath)
{
    TSharedPtr<FJsonObject> Ref = MakeShared<FJsonObject>();
    Ref->SetStringField(TEXT("kind"), TEXT("asset"));
    Ref->SetStringField(TEXT("assetPath"), AssetPath);
    return Ref;
}

FString NormalizeMaterialReferencedAssetPath(UObject* AssetObject)
{
    if (AssetObject == nullptr)
    {
        return TEXT("");
    }

    UPackage* AssetPackage = AssetObject->GetPackage();
    FString AssetPath = AssetPackage ? AssetPackage->GetPathName() : AssetObject->GetPathName();
    if (!AssetPackage)
    {
        int32 DotIdx = INDEX_NONE;
        if (AssetPath.FindLastChar(TEXT('.'), DotIdx))
        {
            AssetPath = AssetPath.Left(DotIdx);
        }
    }
    return AssetPath;
}

bool ResolveMaterialQueryFunctionAssetPath(UMaterialFunctionInterface* MaterialFunction, FString& OutAssetPath)
{
    OutAssetPath = NormalizeMaterialReferencedAssetPath(MaterialFunction);
    return !OutAssetPath.IsEmpty();
}

void EnsureMaterialGraphReady(UMaterial* Material)
{
    if (Material == nullptr)
    {
        return;
    }

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

void ResetMaterialExpressionInput(FExpressionInput& Input)
{
    Input.Expression = nullptr;
    Input.OutputIndex = INDEX_NONE;
    Input.Mask = 0;
    Input.MaskR = 0;
    Input.MaskG = 0;
    Input.MaskB = 0;
    Input.MaskA = 0;
}

bool TryResolveMaterialPropertyByRootPinName(
    UMaterial* Material,
    const FString& RequestedPinName,
    EMaterialProperty& OutProperty,
    FString& OutResolvedPinName,
    TArray<FString>* OutAvailablePinNames)
{
    OutResolvedPinName.Reset();
    if (OutAvailablePinNames != nullptr)
    {
        OutAvailablePinNames->Reset();
    }

    if (Material == nullptr)
    {
        return false;
    }

    EnsureMaterialGraphReady(Material);
    UMaterialGraph* MaterialGraph = Material->MaterialGraph;
    UMaterialGraphNode_Root* RootNode = MaterialGraph ? MaterialGraph->RootNode : nullptr;
    if (MaterialGraph == nullptr || RootNode == nullptr)
    {
        return false;
    }

    const FString RequestedTrimmed = TrimMaterialPinName(RequestedPinName);
    const FString RequestedStripped = StripMaterialPinDisplaySuffix(RequestedTrimmed);
    for (UEdGraphPin* Pin : RootNode->Pins)
    {
        if (Pin == nullptr || Pin->Direction != EGPD_Input)
        {
            continue;
        }

        const FString GraphPinName = TrimMaterialPinName(Pin->PinName.ToString());
        const FString GraphPinNameStripped = StripMaterialPinDisplaySuffix(GraphPinName);
        if (OutAvailablePinNames != nullptr)
        {
            AddUniqueMaterialPinCandidate(*OutAvailablePinNames, GraphPinName);
            AddUniqueMaterialPinCandidate(*OutAvailablePinNames, GraphPinNameStripped);
        }

        const bool bMatches =
            (!RequestedTrimmed.IsEmpty() && GraphPinName.Equals(RequestedTrimmed, ESearchCase::IgnoreCase))
            || (!RequestedStripped.IsEmpty() && GraphPinName.Equals(RequestedStripped, ESearchCase::IgnoreCase))
            || (!RequestedTrimmed.IsEmpty() && GraphPinNameStripped.Equals(RequestedTrimmed, ESearchCase::IgnoreCase))
            || (!RequestedStripped.IsEmpty() && GraphPinNameStripped.Equals(RequestedStripped, ESearchCase::IgnoreCase));
        if (!bMatches)
        {
            continue;
        }

        if (Pin->SourceIndex == INDEX_NONE || !MaterialGraph->MaterialInputs.IsValidIndex(Pin->SourceIndex))
        {
            return false;
        }

        OutProperty = MaterialGraph->MaterialInputs[Pin->SourceIndex].GetProperty();
        OutResolvedPinName = GraphPinNameStripped.IsEmpty() ? GraphPinName : GraphPinNameStripped;
        return true;
    }

    return false;
}

bool DisconnectMaterialRootInput(UMaterial* Material, EMaterialProperty Property)
{
    if (Material == nullptr)
    {
        return false;
    }

    if (FExpressionInput* Input = Material->GetExpressionInputForProperty(Property))
    {
        if (Input->Expression != nullptr || Input->OutputIndex != INDEX_NONE)
        {
            ResetMaterialExpressionInput(*Input);
            return true;
        }
    }

    return false;
}

bool DisconnectMaterialExpressionInputByName(
    UMaterialExpression* Expression,
    const FString& RequestedPinName,
    FString& OutResolvedPinName,
    TArray<FString>* OutAvailablePinNames)
{
    int32 InputIndex = INDEX_NONE;
    if (!TryResolveMaterialInputPinName(Expression, RequestedPinName, InputIndex, OutResolvedPinName, OutAvailablePinNames)
        || InputIndex == INDEX_NONE)
    {
        return false;
    }

    if (FExpressionInput* Input = Expression->GetInput(InputIndex))
    {
        const bool bHadConnection = Input->Expression != nullptr || Input->OutputIndex != INDEX_NONE;
        ResetMaterialExpressionInput(*Input);
        return bHadConnection;
    }

    return false;
}

bool DisconnectMaterialLinksFromSource(
    UMaterial* Material,
    UMaterialExpression* FromExpression,
    const FString& RequestedOutputPinName,
    int32* OutDisconnectedCount)
{
    if (OutDisconnectedCount != nullptr)
    {
        *OutDisconnectedCount = 0;
    }

    if (Material == nullptr || FromExpression == nullptr)
    {
        return false;
    }

    int32 ResolvedOutputIndex = INDEX_NONE;
    FString ResolvedOutputPinName;
    TArray<FString> AvailableOutputPins;
    if (!TryResolveMaterialOutputPin(Material, FromExpression, RequestedOutputPinName, ResolvedOutputIndex, ResolvedOutputPinName, &AvailableOutputPins))
    {
        return false;
    }

    int32 DisconnectedCount = 0;
    for (UMaterialExpression* Expression : Material->GetExpressions())
    {
        if (Expression == nullptr)
        {
            continue;
        }

        const int32 InputCount = Expression->CountInputs();
        for (int32 InputIndex = 0; InputIndex < InputCount; ++InputIndex)
        {
            FExpressionInput* Input = Expression->GetInput(InputIndex);
            if (Input == nullptr || Input->Expression != FromExpression)
            {
                continue;
            }

            if (Input->OutputIndex != ResolvedOutputIndex)
            {
                continue;
            }

            ResetMaterialExpressionInput(*Input);
            ++DisconnectedCount;
        }
    }

    EnsureMaterialGraphReady(Material);
    UMaterialGraph* MaterialGraph = Material->MaterialGraph;
    if (MaterialGraph != nullptr)
    {
        for (const FMaterialInputInfo& InputInfo : MaterialGraph->MaterialInputs)
        {
            FExpressionInput& MaterialInput = InputInfo.GetExpressionInput(Material);
            if (MaterialInput.Expression != FromExpression)
            {
                continue;
            }

            if (MaterialInput.OutputIndex != ResolvedOutputIndex)
            {
                continue;
            }

            ResetMaterialExpressionInput(MaterialInput);
            ++DisconnectedCount;
        }
    }

    if (OutDisconnectedCount != nullptr)
    {
        *OutDisconnectedCount = DisconnectedCount;
    }
    return DisconnectedCount > 0;
}

void SetMaterialQueryChildGraphRef(const TSharedPtr<FJsonObject>& Node, const FString& ChildAssetPath)
{
    if (!Node.IsValid() || ChildAssetPath.IsEmpty())
    {
        return;
    }

    Node->SetObjectField(TEXT("childGraphRef"), MakeMaterialGraphAssetRef(ChildAssetPath));
    Node->SetStringField(TEXT("childLoadStatus"), TEXT("loaded"));
}

}

TSharedPtr<FJsonObject> FLoomleBridgeModule::BuildMaterialListToolResult(const TSharedPtr<FJsonObject>& Arguments) const
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
        Result->SetStringField(TEXT("message"), TEXT("material.list requires assetPath."));
        return Result;
    }

    AssetPath = NormalizeAssetPath(AssetPath);
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

    TArray<TSharedPtr<FJsonValue>> Expressions;
    auto AppendExpression = [&Expressions](UMaterialExpression* Expression)
    {
        if (Expression == nullptr)
        {
            return;
        }

        TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
        Entry->SetStringField(TEXT("nodeId"), MaterialExpressionId(Expression));
        Entry->SetStringField(TEXT("class"), Expression->GetClass() ? Expression->GetClass()->GetPathName() : TEXT(""));
        Entry->SetNumberField(TEXT("x"), Expression->MaterialExpressionEditorX);
        Entry->SetNumberField(TEXT("y"), Expression->MaterialExpressionEditorY);
        Entry->SetStringField(TEXT("comment"), Expression->Desc);
        Expressions.Add(MakeShared<FJsonValueObject>(Entry));
    };

    if (Material != nullptr)
    {
        for (UMaterialExpression* Expression : Material->GetExpressions())
        {
            AppendExpression(Expression);
        }
    }
    else
    {
        for (const TObjectPtr<UMaterialExpression>& Expression : MaterialFunction->GetExpressions())
        {
            AppendExpression(Expression.Get());
        }
    }

    Result->SetStringField(TEXT("assetPath"), AssetPath);
    Result->SetArrayField(TEXT("expressions"), Expressions);
    Result->SetNumberField(TEXT("outputCount"), Material != nullptr ? 1 : 0);
    return Result;
}

TSharedPtr<FJsonObject> FLoomleBridgeModule::BuildMaterialQueryToolResult(const TSharedPtr<FJsonObject>& Arguments) const
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
        Result->SetStringField(TEXT("message"), TEXT("material.query requires assetPath."));
        return Result;
    }

    AssetPath = NormalizeAssetPath(AssetPath);
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
        Result->SetStringField(TEXT("message"), TEXT("Material or MaterialFunction asset not found."));
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

    TArray<UMaterialExpression*> AllExpressions;
    if (Material != nullptr)
    {
        for (UMaterialExpression* Expression : Material->GetExpressions())
        {
            if (Expression != nullptr)
            {
                AllExpressions.Add(Expression);
            }
        }
    }
    else
    {
        for (const TObjectPtr<UMaterialExpression>& Expression : MaterialFunction->GetExpressions())
        {
            if (Expression != nullptr)
            {
                AllExpressions.Add(Expression.Get());
            }
        }
    }

    TArray<TSharedPtr<FJsonValue>> Nodes;
    TArray<TSharedPtr<FJsonValue>> Edges;
    TArray<TSharedPtr<FJsonValue>> Diagnostics;
    int32 MaterialChildGraphRefCount = 0;
    const FString MaterialRootNodeId = TEXT("__material_root__");

    for (UMaterialExpression* Expression : AllExpressions)
    {
        const FString NodeId = MaterialExpressionId(Expression);
        const FString NodeClassPath = Expression->GetClass() ? Expression->GetClass()->GetPathName() : TEXT("");
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

        TSharedPtr<FJsonObject> Node = MakeShared<FJsonObject>();
        Node->SetStringField(TEXT("id"), NodeId);
        Node->SetStringField(TEXT("guid"), NodeId);
        Node->SetStringField(TEXT("nodeClassPath"), NodeClassPath);
        Node->SetStringField(TEXT("title"), Expression->GetClass() ? Expression->GetClass()->GetName() : Expression->GetName());
        Node->SetStringField(TEXT("graphName"), TEXT(""));
        TSharedPtr<FJsonObject> Position = MakeShared<FJsonObject>();
        Position->SetNumberField(TEXT("x"), Expression->MaterialExpressionEditorX);
        Position->SetNumberField(TEXT("y"), Expression->MaterialExpressionEditorY);
        Node->SetObjectField(TEXT("position"), Position);
        Node->SetObjectField(TEXT("layout"), MakeLayoutObject(Expression->MaterialExpressionEditorX, Expression->MaterialExpressionEditorY));
        Node->SetBoolField(TEXT("enabled"), true);

        TArray<TSharedPtr<FJsonValue>> Pins;
        const int32 InputCount = Expression->CountInputs();
        for (int32 InputIndex = 0; InputIndex < InputCount; ++InputIndex)
        {
            FExpressionInput* Input = Expression->GetInput(InputIndex);
            if (Input == nullptr)
            {
                continue;
            }

            const FString InputName = Expression->GetInputName(InputIndex).ToString();
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

        if (UMaterialExpressionMaterialFunctionCall* FuncCall = Cast<UMaterialExpressionMaterialFunctionCall>(Expression))
        {
            if (FuncCall->MaterialFunction)
            {
                FString FuncAssetPath;
                if (ResolveMaterialQueryFunctionAssetPath(FuncCall->MaterialFunction, FuncAssetPath))
                {
                    SetMaterialQueryChildGraphRef(Node, FuncAssetPath);
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
        Root->SetStringField(TEXT("graphName"), TEXT(""));
        TSharedPtr<FJsonObject> RootPosition = MakeShared<FJsonObject>();
        RootPosition->SetNumberField(TEXT("x"), RootNode->NodePosX);
        RootPosition->SetNumberField(TEXT("y"), RootNode->NodePosY);
        Root->SetObjectField(TEXT("position"), RootPosition);
        Root->SetObjectField(TEXT("layout"), MakeLayoutObject(RootNode->NodePosX, RootNode->NodePosY));
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
        Diagnostic->SetStringField(TEXT("message"), TEXT("This material query covers the current asset only. Follow childGraphRef entries to inspect referenced MaterialFunction subgraphs."));
        Diagnostic->SetNumberField(TEXT("childGraphRefCount"), MaterialChildGraphRefCount);
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
    Result->SetStringField(TEXT("revision"), FString::Printf(TEXT("mat:%08x"), GetTypeHash(AssetPath + TEXT("||") + Signature)));
    Result->SetObjectField(TEXT("graphRef"), MakeMaterialGraphAssetRef(AssetPath));
    Result->SetObjectField(TEXT("semanticSnapshot"), Snapshot);
    Result->SetObjectField(TEXT("meta"), Meta);
    Result->SetArrayField(TEXT("diagnostics"), Diagnostics);
    Result->SetStringField(TEXT("nextCursor"), TEXT(""));
    return Result;
}

TSharedPtr<FJsonObject> FLoomleBridgeModule::BuildMaterialMutateToolResult(const TSharedPtr<FJsonObject>& Arguments)
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
        Result->SetStringField(TEXT("message"), TEXT("material.mutate requires assetPath."));
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
            Result->SetStringField(TEXT("message"), TEXT("material.mutate no longer supports runScript."));
            Result->SetBoolField(TEXT("applied"), false);
            Result->SetBoolField(TEXT("partialApplied"), false);
            Result->SetStringField(TEXT("graphType"), TEXT("material"));
            Result->SetStringField(TEXT("assetPath"), AssetPath);
            Result->SetStringField(TEXT("graphName"), TEXT(""));
            Result->SetObjectField(TEXT("graphRef"), MakeMaterialGraphAssetRef(AssetPath));
            Result->SetStringField(TEXT("previousRevision"), TEXT(""));
            Result->SetStringField(TEXT("newRevision"), TEXT(""));

            TSharedPtr<FJsonObject> OpResult = MakeShared<FJsonObject>();
            OpResult->SetNumberField(TEXT("index"), Index);
            OpResult->SetStringField(TEXT("op"), Op);
            OpResult->SetBoolField(TEXT("ok"), false);
            OpResult->SetBoolField(TEXT("skipped"), false);
            OpResult->SetBoolField(TEXT("changed"), false);
            OpResult->SetStringField(TEXT("errorCode"), TEXT("UNSUPPORTED_OP"));
            OpResult->SetStringField(TEXT("errorMessage"), TEXT("material.mutate no longer supports runScript."));
            Result->SetArrayField(TEXT("opResults"), {MakeShared<FJsonValueObject>(OpResult)});

            TSharedPtr<FJsonObject> Diagnostic = MakeShared<FJsonObject>();
            Diagnostic->SetStringField(TEXT("code"), TEXT("UNSUPPORTED_OP"));
            Diagnostic->SetStringField(TEXT("severity"), TEXT("error"));
            Diagnostic->SetStringField(TEXT("message"), TEXT("material.mutate no longer supports runScript."));
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

        const TSharedPtr<FJsonObject> QueryResult = BuildMaterialQueryToolResult(BuildRevisionQueryArgs());
        if (!QueryResult.IsValid())
        {
            OutCode = TEXT("INTERNAL_ERROR");
            OutMessage = TEXT("Failed to resolve current material revision.");
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
                OutMessage = TEXT("Failed to resolve current material revision.");
            }
            return false;
        }

        if (!QueryResult->TryGetStringField(TEXT("revision"), OutRevision) || OutRevision.IsEmpty())
        {
            OutCode = TEXT("INTERNAL_ERROR");
            OutMessage = TEXT("material.query did not return a revision for material.mutate.");
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
        Result->SetStringField(TEXT("graphType"), TEXT("material"));
        Result->SetStringField(TEXT("assetPath"), AssetPath);
        Result->SetStringField(TEXT("graphName"), TEXT(""));
        Result->SetObjectField(TEXT("graphRef"), MakeMaterialGraphAssetRef(AssetPath));
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
        : FString::Printf(TEXT("material|%s|%s"), *AssetPath, *IdempotencyKey);
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
                Result->SetStringField(TEXT("message"), TEXT("idempotencyKey was already used for a different material.mutate request in this graph scope."));
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
        if ((*SourceObj)->TryGetStringField(TEXT("nodeName"), OutNodeId) && !OutNodeId.IsEmpty())
        {
            return true;
        }
        if ((*SourceObj)->TryGetStringField(TEXT("name"), OutNodeId) && !OutNodeId.IsEmpty())
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

    auto HasExplicitPosition = [](const TSharedPtr<FJsonObject>& Obj) -> bool
    {
        return Obj.IsValid() && Obj->HasField(TEXT("x")) && Obj->HasField(TEXT("y"));
    };

    auto ResolvePlacement = [&AssetPath, &ResolveSingleNodeToken, &ReadIntField, &HasExplicitPosition](const TSharedPtr<FJsonObject>& Obj, int32& OutX, int32& OutY) -> bool
    {
        OutX = 0;
        OutY = 0;
        if (HasExplicitPosition(Obj))
        {
            OutX = ReadIntField(Obj, TEXT("x"), 0);
            OutY = ReadIntField(Obj, TEXT("y"), 0);
            return true;
        }

        UMaterial* MaterialAsset = LoadMaterialByAssetPath(AssetPath);
        if (MaterialAsset == nullptr)
        {
            return false;
        }
        EnsureMaterialGraphReady(MaterialAsset);

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

            if (UMaterialExpression* AnchorExpr = FindMaterialExpressionById(MaterialAsset, AnchorNodeId))
            {
                OutX = AnchorExpr->MaterialExpressionEditorX + 336;
                OutY = AnchorExpr->MaterialExpressionEditorY;
                return true;
            }
            return false;
        };

        if (TryAnchor(TEXT("anchor")) || TryAnchor(TEXT("near")) || TryAnchor(TEXT("from")) || TryAnchor(TEXT("target")))
        {
            return true;
        }

        if (MaterialAsset->MaterialGraph != nullptr && MaterialAsset->MaterialGraph->RootNode != nullptr)
        {
            OutX = MaterialAsset->MaterialGraph->RootNode->NodePosX - 384;
            OutY = MaterialAsset->MaterialGraph->RootNode->NodePosY;
            return true;
        }

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
            OutX = RightmostExpression->MaterialExpressionEditorX + 336;
            OutY = RightmostExpression->MaterialExpressionEditorY;
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
                DirectResult->SetStringField(TEXT("message"), ErrorMessage.IsEmpty() ? TEXT("material.mutate failed") : ErrorMessage);
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
            FString NodeClassPath;
            if (!SingleOp->TryGetStringField(TEXT("nodeClassPath"), NodeClassPath) || NodeClassPath.IsEmpty())
            {
                SingleOp->TryGetStringField(TEXT("nodeClass"), NodeClassPath);
            }

            if (NodeClassPath.IsEmpty())
            {
                SingleResult = BuildDirectSingleResult(false, false, TEXT("INVALID_ARGUMENT"), TEXT("addNode.byClass requires nodeClassPath."));
            }
            else if (bDryRun)
            {
                SingleResult = BuildDirectSingleResult(true, false, TEXT(""), TEXT(""));
            }
            else
            {
                UMaterial* MaterialAsset = LoadMaterialByAssetPath(AssetPath);
                UClass* ExpressionClass = LoadObject<UClass>(nullptr, *NodeClassPath);
                if (MaterialAsset == nullptr)
                {
                    SingleResult = BuildDirectSingleResult(false, false, TEXT("ASSET_NOT_FOUND"), TEXT("Material asset not found."));
                }
                else if (ExpressionClass == nullptr || !ExpressionClass->IsChildOf(UMaterialExpression::StaticClass()))
                {
                    SingleResult = BuildDirectSingleResult(false, false, TEXT("INVALID_ARGUMENT"), TEXT("Invalid material expression class."));
                }
                else
                {
                    int32 X = 0;
                    int32 Y = 0;
                    ResolvePlacement(SingleOp, X, Y);
                    UMaterialExpression* NewExpression = UMaterialEditingLibrary::CreateMaterialExpression(MaterialAsset, ExpressionClass, X, Y);
                    if (NewExpression == nullptr)
                    {
                        SingleResult = BuildDirectSingleResult(false, false, TEXT("INTERNAL_ERROR"), TEXT("Failed to create material expression."));
                    }
                    else
                    {
                        FString ParameterName;
                        SingleOp->TryGetStringField(TEXT("parameterName"), ParameterName);
                        if (!ParameterName.IsEmpty())
                        {
                            if (FNameProperty* ParameterNameProperty = FindFProperty<FNameProperty>(NewExpression->GetClass(), TEXT("ParameterName")))
                            {
                                ParameterNameProperty->SetPropertyValue_InContainer(NewExpression, FName(*ParameterName));
                            }
                        }

                        FString NewNodeId = MaterialExpressionId(NewExpression);
                        SingleResult = BuildDirectSingleResult(true, true, TEXT(""), TEXT(""), NewNodeId);
                    }
                }
            }
        }
        else if (OpName.Equals(TEXT("removenode")))
        {
            FString TargetNodeId;
            if (!ResolveSingleNodeToken(SingleOp, TargetNodeId))
            {
                SingleResult = BuildDirectSingleResult(false, false, TEXT("TARGET_NOT_FOUND"), TEXT("removeNode requires a target node reference."));
            }
            else if (bDryRun)
            {
                SingleResult = BuildDirectSingleResult(true, false, TEXT(""), TEXT(""), TargetNodeId);
            }
            else
            {
                UMaterial* MaterialAsset = LoadMaterialByAssetPath(AssetPath);
                UMaterialExpression* Expression = MaterialAsset ? FindMaterialExpressionById(MaterialAsset, TargetNodeId) : nullptr;
                if (Expression == nullptr)
                {
                    SingleResult = BuildDirectSingleResult(false, false, TEXT("NODE_NOT_FOUND"), TEXT("Material expression not found."), TargetNodeId);
                }
                else
                {
                    UMaterialEditingLibrary::DeleteMaterialExpression(MaterialAsset, Expression);
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
                UMaterial* MaterialAsset = LoadMaterialByAssetPath(AssetPath);
                UMaterialExpression* Expression = MaterialAsset ? FindMaterialExpressionById(MaterialAsset, TargetNodeId) : nullptr;
                if (Expression == nullptr)
                {
                    SingleResult = BuildDirectSingleResult(false, false, TEXT("NODE_NOT_FOUND"), TEXT("Material expression not found."), TargetNodeId);
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
                        const int32 Dx = ReadIntField(SingleOp, TEXT("dx"), ReadIntField(SingleOp, TEXT("deltaX"), 0));
                        const int32 Dy = ReadIntField(SingleOp, TEXT("dy"), ReadIntField(SingleOp, TEXT("deltaY"), 0));
                        X = Expression->MaterialExpressionEditorX + Dx;
                        Y = Expression->MaterialExpressionEditorY + Dy;
                    }
                    else
                    {
                        X = ReadIntField(SingleOp, TEXT("x"), 0);
                        Y = ReadIntField(SingleOp, TEXT("y"), 0);
                    }
                    Expression->MaterialExpressionEditorX = X;
                    Expression->MaterialExpressionEditorY = Y;
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
                UMaterial* MaterialAsset = LoadMaterialByAssetPath(AssetPath);
                if (MaterialAsset == nullptr)
                {
                    SingleResult = BuildDirectSingleResult(false, false, TEXT("ASSET_NOT_FOUND"), TEXT("Material asset not found."));
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
                        UMaterialExpression* Expression = FindMaterialExpressionById(MaterialAsset, NodeId);
                        if (Expression == nullptr)
                        {
                            bOk = false;
                            Error = FString::Printf(TEXT("Material expression not found: %s"), *NodeId);
                            break;
                        }
                        Expression->MaterialExpressionEditorX += Dx;
                        Expression->MaterialExpressionEditorY += Dy;
                    }
                    SingleResult = BuildDirectSingleResult(bOk, bOk, TEXT(""), Error);
                }
            }
        }
        else if (OpName.Equals(TEXT("connectpins")))
        {
            const TSharedPtr<FJsonObject>* FromObj = nullptr;
            const TSharedPtr<FJsonObject>* ToObj = nullptr;
            if (!SingleOp->TryGetObjectField(TEXT("from"), FromObj) || FromObj == nullptr || !(*FromObj).IsValid()
                || !SingleOp->TryGetObjectField(TEXT("to"), ToObj) || ToObj == nullptr || !(*ToObj).IsValid())
            {
                SingleResult = BuildDirectSingleResult(false, false, TEXT("INVALID_ARGUMENT"), TEXT("connectPins requires from and to pin references."));
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
                    SingleResult = BuildDirectSingleResult(false, false, TEXT("TARGET_NOT_FOUND"), TEXT("connectPins requires resolvable from/to node references."));
                }
                else if (bDryRun)
                {
                    SingleResult = BuildDirectSingleResult(true, false, TEXT(""), TEXT(""));
                }
                else
                {
                    UMaterial* MaterialAsset = LoadMaterialByAssetPath(AssetPath);
                    if (MaterialAsset == nullptr)
                    {
                        SingleResult = BuildDirectSingleResult(false, false, TEXT("ASSET_NOT_FOUND"), TEXT("Material asset not found."));
                    }
                    else
                    {
                        EnsureMaterialGraphReady(MaterialAsset);
                        UMaterialExpression* FromExpression = FindMaterialExpressionById(MaterialAsset, FromNodeId);
                        int32 ResolvedOutputIndex = INDEX_NONE;
                        FString ResolvedOutputPinName;
                        TArray<FString> AvailableOutputPins;
                        UMaterialExpression* ToExpression = FindMaterialExpressionById(MaterialAsset, ToNodeId);
                        const bool bToRoot = ToNodeId.Equals(TEXT("__material_root__"), ESearchCase::IgnoreCase);
                        if (FromExpression == nullptr)
                        {
                            SingleResult = BuildDirectSingleResult(false, false, TEXT("NODE_NOT_FOUND"), TEXT("Source material expression not found."), FromNodeId);
                        }
                        else if (!TryResolveMaterialOutputPin(MaterialAsset, FromExpression, FromPinName, ResolvedOutputIndex, ResolvedOutputPinName, &AvailableOutputPins))
                        {
                            SingleResult = BuildDirectSingleResult(false, false, TEXT("INVALID_ARGUMENT"), TEXT("Invalid material output pin."), FromNodeId);
                        }
                        else if (bToRoot)
                        {
                            EMaterialProperty Property = MP_MAX;
                            FString ResolvedRootPinName;
                            TArray<FString> AvailableRootPins;
                            if (!TryResolveMaterialPropertyByRootPinName(MaterialAsset, ToPinName, Property, ResolvedRootPinName, &AvailableRootPins))
                            {
                                SingleResult = BuildDirectSingleResult(false, false, TEXT("INVALID_ARGUMENT"), TEXT("Invalid material root input pin."), ToNodeId);
                            }
                            else if (!UMaterialEditingLibrary::ConnectMaterialProperty(FromExpression, ResolvedOutputPinName, Property))
                            {
                                SingleResult = BuildDirectSingleResult(false, false, TEXT("INVALID_ARGUMENT"), TEXT("Failed to connect material root input."), ToNodeId);
                            }
                            else
                            {
                                EnsureMaterialGraphReady(MaterialAsset);
                                SingleResult = BuildDirectSingleResult(true, true, TEXT(""), TEXT(""), ToNodeId);
                            }
                        }
                        else if (ToExpression == nullptr)
                        {
                            SingleResult = BuildDirectSingleResult(false, false, TEXT("NODE_NOT_FOUND"), TEXT("Target material expression not found."), ToNodeId);
                        }
                        else
                        {
                            int32 InputIndex = INDEX_NONE;
                            FString ResolvedInputPinName;
                            TArray<FString> AvailableInputPins;
                            if (!TryResolveMaterialInputPinName(ToExpression, ToPinName, InputIndex, ResolvedInputPinName, &AvailableInputPins))
                            {
                                SingleResult = BuildDirectSingleResult(false, false, TEXT("INVALID_ARGUMENT"), TEXT("Invalid material input pin."), ToNodeId);
                            }
                            else
                            {
                                const FString EffectiveInputPinName = ToExpression->CountInputs() == 1 ? FString() : ResolvedInputPinName;
                                if (!UMaterialEditingLibrary::ConnectMaterialExpressions(FromExpression, ResolvedOutputPinName, ToExpression, EffectiveInputPinName))
                                {
                                    SingleResult = BuildDirectSingleResult(false, false, TEXT("INVALID_ARGUMENT"), TEXT("Failed to connect material expressions."), ToNodeId);
                                }
                                else
                                {
                                    EnsureMaterialGraphReady(MaterialAsset);
                                    SingleResult = BuildDirectSingleResult(true, true, TEXT(""), TEXT(""), ToNodeId);
                                }
                            }
                        }
                    }
                }
            }
        }
        else if (OpName.Equals(TEXT("disconnectpins")))
        {
            const TSharedPtr<FJsonObject>* FromObj = nullptr;
            const TSharedPtr<FJsonObject>* ToObj = nullptr;
            if (!SingleOp->TryGetObjectField(TEXT("from"), FromObj) || FromObj == nullptr || !(*FromObj).IsValid()
                || !SingleOp->TryGetObjectField(TEXT("to"), ToObj) || ToObj == nullptr || !(*ToObj).IsValid())
            {
                SingleResult = BuildDirectSingleResult(false, false, TEXT("INVALID_ARGUMENT"), TEXT("disconnectPins requires from and to pin references."));
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
                    SingleResult = BuildDirectSingleResult(false, false, TEXT("TARGET_NOT_FOUND"), TEXT("disconnectPins requires resolvable from/to node references."));
                }
                else if (bDryRun)
                {
                    SingleResult = BuildDirectSingleResult(true, false, TEXT(""), TEXT(""));
                }
                else
                {
                    UMaterial* MaterialAsset = LoadMaterialByAssetPath(AssetPath);
                    if (MaterialAsset == nullptr)
                    {
                        SingleResult = BuildDirectSingleResult(false, false, TEXT("ASSET_NOT_FOUND"), TEXT("Material asset not found."));
                    }
                    else
                    {
                        EnsureMaterialGraphReady(MaterialAsset);
                        UMaterialExpression* FromExpression = FindMaterialExpressionById(MaterialAsset, FromNodeId);
                        const bool bToRoot = ToNodeId.Equals(TEXT("__material_root__"), ESearchCase::IgnoreCase);
                        int32 ResolvedOutputIndex = INDEX_NONE;
                        FString ResolvedOutputPinName;
                        TArray<FString> AvailableOutputPins;
                        if (FromExpression == nullptr)
                        {
                            SingleResult = BuildDirectSingleResult(false, false, TEXT("NODE_NOT_FOUND"), TEXT("Source material expression not found."), FromNodeId);
                        }
                        else if (!TryResolveMaterialOutputPin(MaterialAsset, FromExpression, FromPinName, ResolvedOutputIndex, ResolvedOutputPinName, &AvailableOutputPins))
                        {
                            SingleResult = BuildDirectSingleResult(false, false, TEXT("INVALID_ARGUMENT"), TEXT("Invalid material output pin."), FromNodeId);
                        }
                        else if (bToRoot)
                        {
                            EMaterialProperty Property = MP_MAX;
                            FString ResolvedRootPinName;
                            TArray<FString> AvailableRootPins;
                            if (!TryResolveMaterialPropertyByRootPinName(MaterialAsset, ToPinName, Property, ResolvedRootPinName, &AvailableRootPins))
                            {
                                SingleResult = BuildDirectSingleResult(false, false, TEXT("INVALID_ARGUMENT"), TEXT("Invalid material root input pin."), ToNodeId);
                            }
                            else
                            {
                                FExpressionInput* RootInput = MaterialAsset->GetExpressionInputForProperty(Property);
                                if (RootInput == nullptr
                                    || RootInput->Expression != FromExpression
                                    || RootInput->OutputIndex != ResolvedOutputIndex)
                                {
                                    SingleResult = BuildDirectSingleResult(false, false, TEXT("TARGET_NOT_FOUND"), TEXT("Requested material root connection was not found."), ToNodeId);
                                }
                                else
                                {
                                    ResetMaterialExpressionInput(*RootInput);
                                    EnsureMaterialGraphReady(MaterialAsset);
                                    SingleResult = BuildDirectSingleResult(true, true, TEXT(""), TEXT(""), ToNodeId);
                                }
                            }
                        }
                        else
                        {
                            UMaterialExpression* ToExpression = FindMaterialExpressionById(MaterialAsset, ToNodeId);
                            if (ToExpression == nullptr)
                            {
                                SingleResult = BuildDirectSingleResult(false, false, TEXT("NODE_NOT_FOUND"), TEXT("Target material expression not found."), ToNodeId);
                            }
                            else
                            {
                                int32 InputIndex = INDEX_NONE;
                                FString ResolvedInputPinName;
                                TArray<FString> AvailableInputPins;
                                if (!TryResolveMaterialInputPinName(ToExpression, ToPinName, InputIndex, ResolvedInputPinName, &AvailableInputPins))
                                {
                                    SingleResult = BuildDirectSingleResult(false, false, TEXT("INVALID_ARGUMENT"), TEXT("Invalid material input pin."), ToNodeId);
                                }
                                else
                                {
                                    FExpressionInput* Input = ToExpression->GetInput(InputIndex);
                                    if (Input == nullptr
                                        || Input->Expression != FromExpression
                                        || Input->OutputIndex != ResolvedOutputIndex)
                                    {
                                        SingleResult = BuildDirectSingleResult(false, false, TEXT("TARGET_NOT_FOUND"), TEXT("Requested material connection was not found."), ToNodeId);
                                    }
                                    else
                                    {
                                        ResetMaterialExpressionInput(*Input);
                                        EnsureMaterialGraphReady(MaterialAsset);
                                        SingleResult = BuildDirectSingleResult(true, true, TEXT(""), TEXT(""), ToNodeId);
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
        else if (OpName.Equals(TEXT("breakpinlinks")))
        {
            FString TargetNodeId;
            if (!ResolveSingleNodeToken(SingleOp, TargetNodeId))
            {
                SingleResult = BuildDirectSingleResult(false, false, TEXT("TARGET_NOT_FOUND"), TEXT("breakPinLinks requires a target node reference."));
            }
            else
            {
                FString TargetPinName;
                SingleOp->TryGetStringField(TEXT("pinName"), TargetPinName);
                if (TargetPinName.IsEmpty())
                {
                    SingleOp->TryGetStringField(TEXT("pin"), TargetPinName);
                }
                if (TargetPinName.IsEmpty())
                {
                    const TSharedPtr<FJsonObject>* TargetObj = nullptr;
                    if (SingleOp->TryGetObjectField(TEXT("target"), TargetObj) && TargetObj != nullptr && (*TargetObj).IsValid())
                    {
                        (*TargetObj)->TryGetStringField(TEXT("pinName"), TargetPinName);
                        if (TargetPinName.IsEmpty())
                        {
                            (*TargetObj)->TryGetStringField(TEXT("pin"), TargetPinName);
                        }
                    }
                }

                if (TargetPinName.IsEmpty())
                {
                    SingleResult = BuildDirectSingleResult(false, false, TEXT("INVALID_ARGUMENT"), TEXT("breakPinLinks requires pinName."));
                }
                else if (bDryRun)
                {
                    SingleResult = BuildDirectSingleResult(true, false, TEXT(""), TEXT(""), TargetNodeId);
                }
                else
                {
                    UMaterial* MaterialAsset = LoadMaterialByAssetPath(AssetPath);
                    if (MaterialAsset == nullptr)
                    {
                        SingleResult = BuildDirectSingleResult(false, false, TEXT("ASSET_NOT_FOUND"), TEXT("Material asset not found."));
                    }
                    else
                    {
                        EnsureMaterialGraphReady(MaterialAsset);
                        if (TargetNodeId.Equals(TEXT("__material_root__"), ESearchCase::IgnoreCase))
                        {
                            EMaterialProperty Property = MP_MAX;
                            FString ResolvedRootPinName;
                            TArray<FString> AvailableRootPins;
                            if (!TryResolveMaterialPropertyByRootPinName(MaterialAsset, TargetPinName, Property, ResolvedRootPinName, &AvailableRootPins))
                            {
                                SingleResult = BuildDirectSingleResult(false, false, TEXT("INVALID_ARGUMENT"), TEXT("Invalid material root input pin."), TargetNodeId);
                            }
                            else
                            {
                                const bool bChanged = DisconnectMaterialRootInput(MaterialAsset, Property);
                                if (bChanged)
                                {
                                    EnsureMaterialGraphReady(MaterialAsset);
                                }
                                SingleResult = bChanged
                                    ? BuildDirectSingleResult(true, true, TEXT(""), TEXT(""), TargetNodeId)
                                    : BuildDirectSingleResult(false, false, TEXT("TARGET_NOT_FOUND"), TEXT("No material root links were found for the requested pin."), TargetNodeId);
                            }
                        }
                        else
                        {
                            UMaterialExpression* TargetExpression = FindMaterialExpressionById(MaterialAsset, TargetNodeId);
                            if (TargetExpression == nullptr)
                            {
                                SingleResult = BuildDirectSingleResult(false, false, TEXT("NODE_NOT_FOUND"), TEXT("Material expression not found."), TargetNodeId);
                            }
                            else
                            {
                                FString ResolvedInputPinName;
                                TArray<FString> AvailableInputPins;
                                const bool bChangedInput = DisconnectMaterialExpressionInputByName(TargetExpression, TargetPinName, ResolvedInputPinName, &AvailableInputPins);
                                if (bChangedInput)
                                {
                                    SingleResult = BuildDirectSingleResult(true, true, TEXT(""), TEXT(""), TargetNodeId);
                                }
                                else
                                {
                                    int32 DisconnectedCount = 0;
                                    const bool bChangedOutput = DisconnectMaterialLinksFromSource(MaterialAsset, TargetExpression, TargetPinName, &DisconnectedCount);
                                    if (bChangedOutput)
                                    {
                                        EnsureMaterialGraphReady(MaterialAsset);
                                    }
                                    SingleResult = bChangedOutput
                                        ? BuildDirectSingleResult(true, true, TEXT(""), TEXT(""), TargetNodeId)
                                        : BuildDirectSingleResult(false, false, TEXT("TARGET_NOT_FOUND"), TEXT("No material links were found for the requested pin."), TargetNodeId);
                                }
                            }
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

                TArray<FString> MovedNodeIds;
                FString Error;
                const bool bOk = ApplyMaterialLayout(AssetPath, TEXT(""), Scope, RequestedNodeIds, MovedNodeIds, Error);
                SingleResult = BuildDirectSingleResult(bOk, bOk && MovedNodeIds.Num() > 0, TEXT(""), Error);
                if (SingleResult.IsValid())
                {
                    const TArray<TSharedPtr<FJsonValue>>* SingleOpResults = nullptr;
                    if (SingleResult->TryGetArrayField(TEXT("opResults"), SingleOpResults)
                        && SingleOpResults != nullptr
                        && SingleOpResults->Num() > 0)
                    {
                        const TSharedPtr<FJsonObject>* SingleOpResult = nullptr;
                        if ((*SingleOpResults)[0].IsValid()
                            && (*SingleOpResults)[0]->TryGetObject(SingleOpResult)
                            && SingleOpResult != nullptr
                            && (*SingleOpResult).IsValid())
                        {
                            TArray<TSharedPtr<FJsonValue>> MovedNodeValues;
                            for (const FString& MovedNodeId : MovedNodeIds)
                            {
                                MovedNodeValues.Add(MakeShared<FJsonValueString>(MovedNodeId));
                            }
                            (*SingleOpResult)->SetArrayField(TEXT("movedNodeIds"), MovedNodeValues);
                        }
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
                UMaterial* MaterialAsset = LoadMaterialByAssetPath(AssetPath);
                if (MaterialAsset == nullptr)
                {
                    SingleResult = BuildDirectSingleResult(false, false, TEXT("ASSET_NOT_FOUND"), TEXT("Material asset not found."));
                }
                else
                {
                    UMaterialEditingLibrary::RecompileMaterial(MaterialAsset);
                    SingleResult = BuildDirectSingleResult(true, false, TEXT(""), TEXT(""));
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
            UnsupportedOpResult->SetStringField(TEXT("errorMessage"), FString::Printf(TEXT("material.mutate does not support op '%s'."), *OpName));
            OpResults.Add(MakeShared<FJsonValueObject>(UnsupportedOpResult));

            TSharedPtr<FJsonObject> Diagnostic = MakeShared<FJsonObject>();
            Diagnostic->SetStringField(TEXT("code"), TEXT("UNSUPPORTED_OP"));
            Diagnostic->SetStringField(TEXT("severity"), TEXT("error"));
            Diagnostic->SetStringField(TEXT("message"), FString::Printf(TEXT("material.mutate does not support op '%s'."), *OpName));
            Diagnostic->SetStringField(TEXT("sourceKind"), TEXT("mutate"));
            Diagnostic->SetStringField(TEXT("op"), OpName);
            Diagnostics.Add(MakeShared<FJsonValueObject>(Diagnostic));

            bAnyError = true;
            if (FirstErrorCode.IsEmpty())
            {
                FirstErrorCode = TEXT("UNSUPPORTED_OP");
                FirstErrorMessage = FString::Printf(TEXT("material.mutate does not support op '%s'."), *OpName);
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
        Result->SetStringField(TEXT("message"), FirstErrorMessage.IsEmpty() ? TEXT("material.mutate failed") : FirstErrorMessage);
    }
    Result->SetBoolField(TEXT("applied"), !bAnyError);
    Result->SetBoolField(TEXT("partialApplied"), bAnyError && bAnyChanged);
    Result->SetStringField(TEXT("graphType"), TEXT("material"));
    Result->SetStringField(TEXT("assetPath"), AssetPath);
    Result->SetStringField(TEXT("graphName"), TEXT(""));
    Result->SetObjectField(TEXT("graphRef"), MakeMaterialGraphAssetRef(AssetPath));
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

TSharedPtr<FJsonObject> FLoomleBridgeModule::BuildMaterialVerifyToolResult(const TSharedPtr<FJsonObject>& Arguments)
{
    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

    FString AssetPath;
    if (!Arguments.IsValid()
        || !Arguments->TryGetStringField(TEXT("assetPath"), AssetPath)
        || AssetPath.TrimStartAndEnd().IsEmpty())
    {
        Result->SetBoolField(TEXT("isError"), true);
        Result->SetStringField(TEXT("code"), TEXT("INVALID_ARGUMENT"));
        Result->SetStringField(TEXT("message"), TEXT("material.verify requires assetPath."));
        return Result;
    }

    AssetPath = NormalizeAssetPath(AssetPath);

    const TSharedPtr<FJsonObject> QueryArgs = MakeShared<FJsonObject>();
    QueryArgs->SetStringField(TEXT("assetPath"), AssetPath);
    const TSharedPtr<FJsonObject> QueryResult = BuildMaterialQueryToolResult(QueryArgs);
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

    const TSharedPtr<FJsonObject> MutateResult = BuildMaterialMutateToolResult(MutateArgs);
    bool bMutateError = false;
    MutateResult->TryGetBoolField(TEXT("isError"), bMutateError);

    Result->SetBoolField(TEXT("isError"), false);
    Result->SetStringField(TEXT("assetPath"), AssetPath);
    Result->SetStringField(TEXT("status"), bMutateError ? TEXT("error") : TEXT("ok"));
    Result->SetStringField(
        TEXT("summary"),
        bMutateError
            ? TEXT("Material verification failed during compile-backed confirmation.")
            : TEXT("Material verification succeeded with compile-backed confirmation."));

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
        Result->SetStringField(TEXT("code"), TEXT("COMPILE_FAILED"));
        Result->SetStringField(TEXT("message"), TEXT("Material compile failed."));
    }

    return Result;
}

TSharedPtr<FJsonObject> FLoomleBridgeModule::BuildMaterialDescribeToolResult(const TSharedPtr<FJsonObject>& Arguments) const
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

        const TSharedPtr<FJsonObject> QueryResult = BuildMaterialQueryToolResult(QueryArgs);
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
            Result->SetStringField(TEXT("message"), TEXT("Material node not found."));
            return Result;
        }

        const TSharedPtr<FJsonObject>* NodeObject = nullptr;
        if (!(*Nodes)[0].IsValid() || !(*Nodes)[0]->TryGetObject(NodeObject) || NodeObject == nullptr || !(*NodeObject).IsValid())
        {
            TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
            Result->SetBoolField(TEXT("isError"), true);
            Result->SetStringField(TEXT("code"), TEXT("INTERNAL_ERROR"));
            Result->SetStringField(TEXT("message"), TEXT("Material describe failed to read node snapshot."));
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
        return BuildMaterialClassDescribeResult(NodeClass);
    }

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetBoolField(TEXT("isError"), true);
    Result->SetStringField(TEXT("code"), TEXT("INVALID_ARGUMENT"));
    Result->SetStringField(TEXT("message"), TEXT("material.describe requires assetPath+nodeId for instance mode or nodeClass for class mode."));
    return Result;
}
