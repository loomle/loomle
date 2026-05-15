// Widget tool handlers for Loomle Bridge.
// Included by LoomleBridgeModule.cpp after shared graph-domain helpers.

namespace
{

// Resolve a widget name from an args object that may carry { "name": "..." }
// or { "target": { "name": "..." } }, mirroring the graph nodeId/target pattern.
bool ResolveWidgetName(const TSharedPtr<FJsonObject>& Args, FString& OutName)
{
    if (!Args.IsValid())
    {
        return false;
    }
    if (Args->TryGetStringField(TEXT("name"), OutName) && !OutName.IsEmpty())
    {
        return true;
    }
    const TSharedPtr<FJsonObject>* TargetObj = nullptr;
    if (Args->TryGetObjectField(TEXT("target"), TargetObj)
        && TargetObj && (*TargetObj).IsValid()
        && (*TargetObj)->TryGetStringField(TEXT("name"), OutName)
        && !OutName.IsEmpty())
    {
        return true;
    }
    return false;
}

FString WidgetPaletteTextToString(const FText& Text)
{
    return Text.IsEmpty() ? FString() : Text.ToString();
}

TArray<TSharedPtr<FJsonValue>> WidgetPaletteStringArrayToJson(const TArray<FString>& Values)
{
    TArray<TSharedPtr<FJsonValue>> Out;
    for (const FString& Value : Values)
    {
        if (!Value.IsEmpty())
        {
            Out.Add(MakeShared<FJsonValueString>(Value));
        }
    }
    return Out;
}

bool IsWidgetPaletteClassAllowed(const UClass* Class)
{
    if (Class == nullptr || !Class->IsChildOf(UWidget::StaticClass()))
    {
        return false;
    }
    if (Class == UWidget::StaticClass() || Class == UUserWidget::StaticClass())
    {
        return false;
    }
    if (Class->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated | CLASS_Hidden | CLASS_HideDropDown))
    {
        return false;
    }
    return true;
}

TSharedPtr<FJsonObject> MakeWidgetOpResult(
    int32 Index,
    const FString& Op,
    bool bOk,
    bool bChanged,
    const FString& ErrorCode = FString(),
    const FString& ErrorMessage = FString())
{
    TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
    Obj->SetNumberField(TEXT("index"), Index);
    Obj->SetStringField(TEXT("op"), Op);
    Obj->SetBoolField(TEXT("ok"), bOk);
    Obj->SetBoolField(TEXT("changed"), bChanged);
    Obj->SetStringField(TEXT("errorCode"), ErrorCode);
    Obj->SetStringField(TEXT("errorMessage"), ErrorMessage);
    return Obj;
}

} // namespace

// ---------------------------------------------------------------------------
// widget.palette
// ---------------------------------------------------------------------------

TSharedPtr<FJsonObject> FLoomleBridgeModule::BuildWidgetPaletteToolResult(
    const TSharedPtr<FJsonObject>& Arguments) const
{
    FString AssetPath;
    FString Query;
    int32 Limit = 50;
    int32 Offset = 0;
    if (Arguments.IsValid())
    {
        Arguments->TryGetStringField(TEXT("assetPath"), AssetPath);
        Arguments->TryGetStringField(TEXT("query"), Query);
        double LimitNumber = 0.0;
        if (Arguments->TryGetNumberField(TEXT("limit"), LimitNumber))
        {
            Limit = FMath::Clamp(static_cast<int32>(LimitNumber), 1, 500);
        }
        double OffsetNumber = 0.0;
        if (Arguments->TryGetNumberField(TEXT("offset"), OffsetNumber))
        {
            Offset = FMath::Max(0, static_cast<int32>(OffsetNumber));
        }
    }

    TSet<FString> ElementTypes;
    const TArray<TSharedPtr<FJsonValue>>* ElementTypeValues = nullptr;
    if (Arguments.IsValid() && Arguments->TryGetArrayField(TEXT("elementTypes"), ElementTypeValues) && ElementTypeValues != nullptr)
    {
        for (const TSharedPtr<FJsonValue>& Value : *ElementTypeValues)
        {
            FString Text;
            if (Value.IsValid() && Value->TryGetString(Text))
            {
                ElementTypes.Add(Text.ToLower());
            }
        }
    }
    auto IncludesElementType = [&ElementTypes](const TCHAR* Type)
    {
        return ElementTypes.IsEmpty() || ElementTypes.Contains(FString(Type).ToLower());
    };

    TArray<UClass*> WidgetClasses;
    for (TObjectIterator<UClass> It; It; ++It)
    {
        UClass* Class = *It;
        if (IsWidgetPaletteClassAllowed(Class))
        {
            WidgetClasses.Add(Class);
        }
    }
    WidgetClasses.Sort([](const UClass& Left, const UClass& Right)
    {
        return Left.GetPathName() < Right.GetPathName();
    });

    TArray<TSharedPtr<FJsonObject>> AllEntries;
    int32 EntryIndex = 0;
    auto AddEntry = [&AllEntries, &EntryIndex](const FString& Kind, const FString& Label, const FString& Category, const FString& Tooltip, const TSharedPtr<FJsonObject>& Payload, const TArray<FString>& Keywords)
    {
        const FString StableText = FString::Printf(
            TEXT("%s|%s|%s|%s|%s|%d"),
            *Kind,
            *Category,
            *Label,
            *Tooltip,
            *SerializeBlueprintJsonObjectCondensed(Payload),
            EntryIndex);
        TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
        Entry->SetStringField(TEXT("id"), FString::Printf(TEXT("widget.palette:%s"), *FMD5::HashAnsiString(*StableText)));
        Entry->SetStringField(TEXT("kind"), Kind);
        Entry->SetStringField(TEXT("label"), Label);
        Entry->SetStringField(TEXT("category"), Category);
        Entry->SetStringField(TEXT("tooltip"), Tooltip);
        Entry->SetBoolField(TEXT("requiresContext"), false);
        Entry->SetBoolField(TEXT("executable"), true);
        Entry->SetArrayField(TEXT("keywords"), WidgetPaletteStringArrayToJson(Keywords));
        Entry->SetObjectField(TEXT("payload"), Payload);
        AllEntries.Add(Entry);
        ++EntryIndex;
    };

    for (UClass* Class : WidgetClasses)
    {
        UWidget* DefaultWidget = Class ? Class->GetDefaultObject<UWidget>() : nullptr;
        if (DefaultWidget == nullptr)
        {
            continue;
        }

        const bool bIsUserWidget = Class->IsChildOf(UUserWidget::StaticClass());
        const FString Kind = bIsUserWidget ? TEXT("user") : TEXT("native");
        if (!IncludesElementType(*Kind))
        {
            continue;
        }

        FString Label = WidgetPaletteTextToString(Class->GetDisplayNameText());
        if (Label.IsEmpty())
        {
            Label = Class->GetName();
        }
        FString Category = WidgetPaletteTextToString(DefaultWidget->GetPaletteCategory());
        if (Category.IsEmpty())
        {
            Category = bIsUserWidget ? TEXT("User Created") : TEXT("Common");
        }

        TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
        Payload->SetStringField(TEXT("widgetClass"), Class->GetPathName());
        Payload->SetStringField(TEXT("className"), Class->GetName());

        TArray<FString> Keywords;
        Keywords.Add(Class->GetName());
        Keywords.Add(Class->GetPathName());
        Keywords.Add(Category);

        const FString Tooltip = FString::Printf(TEXT("Adds a %s widget to the WidgetTree."), *Label);
        AddEntry(Kind, Label, Category, Tooltip, Payload, Keywords);
    }

    auto EntryMatchesQuery = [&Query](const TSharedPtr<FJsonObject>& Entry)
    {
        if (Query.IsEmpty())
        {
            return true;
        }
        const FString QueryLower = Query.ToLower();
        for (const TCHAR* Field : { TEXT("label"), TEXT("category"), TEXT("tooltip"), TEXT("kind") })
        {
            FString Value;
            if (Entry->TryGetStringField(Field, Value) && Value.ToLower().Contains(QueryLower))
            {
                return true;
            }
        }
        return SerializeBlueprintJsonObjectCondensed(Entry).ToLower().Contains(QueryLower);
    };

    auto EntryScore = [&Query](const TSharedPtr<FJsonObject>& Entry)
    {
        if (Query.IsEmpty())
        {
            return 100;
        }
        const FString QueryLower = Query.ToLower();
        FString Label;
        Entry->TryGetStringField(TEXT("label"), Label);
        const FString LabelLower = Label.ToLower();
        if (LabelLower.Equals(QueryLower))
        {
            return 0;
        }
        if (LabelLower.StartsWith(QueryLower))
        {
            return 10;
        }
        if (LabelLower.Contains(QueryLower))
        {
            return 20;
        }
        FString Category;
        Entry->TryGetStringField(TEXT("category"), Category);
        if (Category.ToLower().Contains(QueryLower))
        {
            return 30;
        }
        return 50;
    };

    TArray<TSharedPtr<FJsonObject>> Filtered;
    for (const TSharedPtr<FJsonObject>& Entry : AllEntries)
    {
        if (Entry.IsValid() && EntryMatchesQuery(Entry))
        {
            Filtered.Add(Entry);
        }
    }
    Filtered.Sort([&EntryScore](const TSharedPtr<FJsonObject>& Left, const TSharedPtr<FJsonObject>& Right)
    {
        const int32 LeftScore = EntryScore(Left);
        const int32 RightScore = EntryScore(Right);
        if (LeftScore != RightScore)
        {
            return LeftScore < RightScore;
        }
        FString LeftLabel;
        FString RightLabel;
        Left->TryGetStringField(TEXT("label"), LeftLabel);
        Right->TryGetStringField(TEXT("label"), RightLabel);
        return LeftLabel < RightLabel;
    });

    TArray<TSharedPtr<FJsonValue>> EntriesJson;
    const int32 Start = FMath::Min(Offset, Filtered.Num());
    const int32 End = FMath::Min(Start + Limit, Filtered.Num());
    for (int32 Index = Start; Index < End; ++Index)
    {
        EntriesJson.Add(MakeShared<FJsonValueObject>(Filtered[Index]));
    }

    TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
    Payload->SetStringField(TEXT("assetPath"), NormalizeAssetPath(AssetPath));
    Payload->SetStringField(TEXT("query"), Query);
    Payload->SetNumberField(TEXT("total"), Filtered.Num());
    Payload->SetNumberField(TEXT("limit"), Limit);
    Payload->SetNumberField(TEXT("offset"), Offset);
    Payload->SetArrayField(TEXT("entries"), EntriesJson);
    Payload->SetArrayField(TEXT("diagnostics"), TArray<TSharedPtr<FJsonValue>>{});
    return Payload;
}

// ---------------------------------------------------------------------------
// widget.query
// ---------------------------------------------------------------------------

TSharedPtr<FJsonObject> FLoomleBridgeModule::BuildWidgetQueryToolResult(
    const TSharedPtr<FJsonObject>& Arguments) const
{
    TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();

    FString AssetPath;
    if (!Arguments.IsValid() || !Arguments->TryGetStringField(TEXT("assetPath"), AssetPath) || AssetPath.IsEmpty())
    {
        Payload->SetBoolField(TEXT("isError"), true);
        Payload->SetStringField(TEXT("code"), TEXT("INVALID_ARGUMENT"));
        Payload->SetStringField(TEXT("message"), TEXT("INVALID_ARGUMENT"));
        Payload->SetStringField(TEXT("detail"), TEXT("field assetPath is required"));
        return Payload;
    }

    bool bIncludeSlot = false;
    if (Arguments.IsValid())
    {
        Arguments->TryGetBoolField(TEXT("includeSlotProperties"), bIncludeSlot);
    }

    FString TreeJson;
    FString Revision;
    FString Error;
    if (!FLoomleWidgetAdapter::QueryWidgetTree(AssetPath, bIncludeSlot, TreeJson, Revision, Error))
    {
        FString DomainCode = TEXT("INTERNAL_ERROR");
        if (Error.StartsWith(TEXT("WIDGET_TREE_UNAVAILABLE")))
        {
            DomainCode = TEXT("WIDGET_TREE_UNAVAILABLE");
        }
        Payload->SetBoolField(TEXT("isError"), true);
        Payload->SetStringField(TEXT("code"), DomainCode);
        Payload->SetStringField(TEXT("message"), DomainCode);
        Payload->SetStringField(TEXT("detail"), Error);
        return Payload;
    }

    // Parse the tree JSON back to embed it as a structured object in the response
    TSharedPtr<FJsonObject> TreeObj;
    TSharedRef<TJsonReader<TCHAR>> Reader = TJsonReaderFactory<TCHAR>::Create(TreeJson);
    if (!FJsonSerializer::Deserialize(Reader, TreeObj) || !TreeObj.IsValid())
    {
        Payload->SetBoolField(TEXT("isError"), true);
        Payload->SetStringField(TEXT("code"), TEXT("INTERNAL_ERROR"));
        Payload->SetStringField(TEXT("message"), TEXT("INTERNAL_ERROR"));
        Payload->SetStringField(TEXT("detail"), TEXT("Failed to deserialize widget tree JSON."));
        return Payload;
    }

    Payload->SetStringField(TEXT("assetPath"), AssetPath);
    Payload->SetStringField(TEXT("revision"), Revision);

    const TSharedPtr<FJsonObject>* RootWidgetObj = nullptr;
    if (TreeObj->TryGetObjectField(TEXT("rootWidget"), RootWidgetObj) && RootWidgetObj)
    {
        Payload->SetObjectField(TEXT("rootWidget"), *RootWidgetObj);
    }
    else
    {
        Payload->SetField(TEXT("rootWidget"), MakeShared<FJsonValueNull>());
    }

    Payload->SetArrayField(TEXT("diagnostics"), TArray<TSharedPtr<FJsonValue>>{});
    return Payload;
}

// ---------------------------------------------------------------------------
// widget.mutate
// ---------------------------------------------------------------------------

TSharedPtr<FJsonObject> FLoomleBridgeModule::BuildWidgetMutateToolResult(
    const TSharedPtr<FJsonObject>& Arguments)
{
    TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();

    // --- Validate required fields ---
    FString AssetPath;
    if (!Arguments.IsValid() || !Arguments->TryGetStringField(TEXT("assetPath"), AssetPath) || AssetPath.IsEmpty())
    {
        Payload->SetBoolField(TEXT("isError"), true);
        Payload->SetStringField(TEXT("code"), TEXT("INVALID_ARGUMENT"));
        Payload->SetStringField(TEXT("message"), TEXT("INVALID_ARGUMENT"));
        Payload->SetStringField(TEXT("detail"), TEXT("field assetPath is required"));
        return Payload;
    }

    const TArray<TSharedPtr<FJsonValue>>* OpsArray = nullptr;
    if (!Arguments->TryGetArrayField(TEXT("ops"), OpsArray) || !OpsArray || OpsArray->IsEmpty())
    {
        Payload->SetBoolField(TEXT("isError"), true);
        Payload->SetStringField(TEXT("code"), TEXT("INVALID_ARGUMENT"));
        Payload->SetStringField(TEXT("message"), TEXT("INVALID_ARGUMENT"));
        Payload->SetStringField(TEXT("detail"), TEXT("field ops is required and must be non-empty"));
        return Payload;
    }

    bool bDryRun = false;
    Arguments->TryGetBoolField(TEXT("dryRun"), bDryRun);

    bool bContinueOnError = false;
    Arguments->TryGetBoolField(TEXT("continueOnError"), bContinueOnError);

    // --- Load asset ---
    FString LoadError;
    FString ObjectPath = AssetPath;
    if (!ObjectPath.Contains(TEXT(".")))
    {
        ObjectPath = AssetPath + TEXT(".") + FPackageName::GetShortName(AssetPath);
    }
    UObject* Loaded = StaticLoadObject(UWidgetBlueprint::StaticClass(), nullptr, *ObjectPath);
    UWidgetBlueprint* WBP = Cast<UWidgetBlueprint>(Loaded);
    if (!WBP || !WBP->WidgetTree)
    {
        Payload->SetBoolField(TEXT("isError"), true);
        Payload->SetStringField(TEXT("code"), TEXT("WIDGET_TREE_UNAVAILABLE"));
        Payload->SetStringField(TEXT("message"), TEXT("WIDGET_TREE_UNAVAILABLE"));
        Payload->SetStringField(TEXT("detail"), FString::Printf(
            TEXT("Asset '%s' is not a WidgetBlueprint or has a null WidgetTree."), *AssetPath));
        return Payload;
    }

    // --- Optimistic concurrency check ---
    FString ExpectedRevision;
    if (Arguments->TryGetStringField(TEXT("expectedRevision"), ExpectedRevision) && !ExpectedRevision.IsEmpty())
    {
        const FString CurrentRevision = FLoomleWidgetAdapter::ComputeRevision_Public(WBP);
        if (CurrentRevision != ExpectedRevision)
        {
            Payload->SetBoolField(TEXT("isError"), true);
            Payload->SetStringField(TEXT("code"), TEXT("REVISION_CONFLICT"));
            Payload->SetStringField(TEXT("message"), TEXT("REVISION_CONFLICT"));
            Payload->SetStringField(TEXT("detail"), FString::Printf(
                TEXT("Expected revision '%s' but current is '%s'."), *ExpectedRevision, *CurrentRevision));
            return Payload;
        }
    }

    const FString PreviousRevision = FLoomleWidgetAdapter::ComputeRevision_Public(WBP);

    // --- Execute ops ---
    TArray<TSharedPtr<FJsonValue>> OpResults;
    bool bAnyChanged = false;
    bool bAnyFailed = false;

    for (int32 i = 0; i < OpsArray->Num(); ++i)
    {
        const TSharedPtr<FJsonObject>* OpObjPtr = nullptr;
        if (!(*OpsArray)[i]->TryGetObject(OpObjPtr) || !OpObjPtr || !(*OpObjPtr).IsValid())
        {
            OpResults.Add(MakeShared<FJsonValueObject>(
                MakeWidgetOpResult(i, TEXT(""), false, false, TEXT("INVALID_ARGUMENT"), TEXT("Op entry is not a valid object."))));
            bAnyFailed = true;
            if (!bContinueOnError) break;
            continue;
        }

        const TSharedPtr<FJsonObject>& OpObj = *OpObjPtr;
        FString OpName;
        OpObj->TryGetStringField(TEXT("op"), OpName);

        const TSharedPtr<FJsonObject>* ArgsObjPtr = nullptr;
        TSharedPtr<FJsonObject> Args;
        if (OpObj->TryGetObjectField(TEXT("args"), ArgsObjPtr) && ArgsObjPtr)
        {
            Args = *ArgsObjPtr;
        }
        else
        {
            Args = MakeShared<FJsonObject>();
        }

        FString OpError;
        bool bOpOk = false;

        if (bDryRun)
        {
            // Dry-run: validate op name only, do not touch UE objects
            const TArray<FString> KnownOps = {
                TEXT("addWidget"), TEXT("removeWidget"), TEXT("setProperty"), TEXT("reparentWidget")
            };
            bOpOk = KnownOps.Contains(OpName);
            if (!bOpOk)
            {
                OpError = FString::Printf(TEXT("Unknown op '%s'."), *OpName);
            }
            OpResults.Add(MakeShared<FJsonValueObject>(
                MakeWidgetOpResult(i, OpName, bOpOk, false,
                    bOpOk ? FString() : TEXT("INVALID_ARGUMENT"), OpError)));
            if (!bOpOk)
            {
                bAnyFailed = true;
                if (!bContinueOnError) break;
            }
            continue;
        }

        if (OpName.Equals(TEXT("addWidget")))
        {
            FString WidgetClass, Name, Parent;
            Args->TryGetStringField(TEXT("widgetClass"), WidgetClass);
            Args->TryGetStringField(TEXT("name"), Name);
            Args->TryGetStringField(TEXT("parentName"), Parent);
            if (Parent.IsEmpty())
            {
                Args->TryGetStringField(TEXT("parent"), Parent); // legacy alias
            }
            const TSharedPtr<FJsonObject>* SlotObj = nullptr;
            TSharedPtr<FJsonObject> SlotArgs;
            if (Args->TryGetObjectField(TEXT("slot"), SlotObj) && SlotObj)
            {
                SlotArgs = *SlotObj;
            }
            if (WidgetClass.IsEmpty() || Name.IsEmpty())
            {
                bOpOk = false;
                OpError = TEXT("addWidget requires widgetClass and name.");
            }
            else
            {
                bOpOk = FLoomleWidgetAdapter::AddWidget(WBP, WidgetClass, Name, Parent, SlotArgs, OpError);
            }
        }
        else if (OpName.Equals(TEXT("removeWidget")))
        {
            FString Name;
            if (!ResolveWidgetName(Args, Name))
            {
                bOpOk = false;
                OpError = TEXT("removeWidget requires args.name or args.target.name.");
            }
            else
            {
                bOpOk = FLoomleWidgetAdapter::RemoveWidget(WBP, Name, OpError);
            }
        }
        else if (OpName.Equals(TEXT("setProperty")))
        {
            FString Name, PropertyName, Value;
            if (!ResolveWidgetName(Args, Name))
            {
                bOpOk = false;
                OpError = TEXT("setProperty requires args.name or args.target.name.");
            }
            else if (!Args->TryGetStringField(TEXT("property"), PropertyName) || PropertyName.IsEmpty())
            {
                bOpOk = false;
                OpError = TEXT("setProperty requires args.property.");
            }
            else if (!Args->TryGetStringField(TEXT("value"), Value))
            {
                bOpOk = false;
                OpError = TEXT("setProperty requires args.value.");
            }
            else
            {
                bOpOk = FLoomleWidgetAdapter::SetWidgetProperty(WBP, Name, PropertyName, Value, OpError);
            }
        }
        else if (OpName.Equals(TEXT("reparentWidget")))
        {
            FString Name, NewParent;
            if (!ResolveWidgetName(Args, Name))
            {
                bOpOk = false;
                OpError = TEXT("reparentWidget requires args.name or args.target.name.");
            }
            else if (!Args->TryGetStringField(TEXT("newParent"), NewParent) || NewParent.IsEmpty())
            {
                bOpOk = false;
                OpError = TEXT("reparentWidget requires args.newParent.");
            }
            else
            {
                const TSharedPtr<FJsonObject>* SlotObj = nullptr;
                TSharedPtr<FJsonObject> SlotArgs;
                if (Args->TryGetObjectField(TEXT("slot"), SlotObj) && SlotObj)
                {
                    SlotArgs = *SlotObj;
                }
                bOpOk = FLoomleWidgetAdapter::ReparentWidget(WBP, Name, NewParent, SlotArgs, OpError);
            }
        }
        else
        {
            bOpOk = false;
            OpError = FString::Printf(TEXT("Unknown widget op '%s'."), *OpName);
        }

        if (bOpOk)
        {
            bAnyChanged = true;
        }
        else
        {
            bAnyFailed = true;
        }

        // Extract domain code from error string prefix if present
        FString DomainCode;
        FString ErrorDetail = OpError;
        int32 ColonIdx = INDEX_NONE;
        if (OpError.FindChar(TEXT(':'), ColonIdx) && ColonIdx > 0)
        {
            FString Prefix = OpError.Left(ColonIdx);
            if (!Prefix.Contains(TEXT(" ")))
            {
                DomainCode = Prefix;
                ErrorDetail = OpError.Mid(ColonIdx + 1).TrimStart();
            }
        }

        OpResults.Add(MakeShared<FJsonValueObject>(
            MakeWidgetOpResult(i, OpName, bOpOk, bOpOk, DomainCode, ErrorDetail)));

        if (!bOpOk && !bContinueOnError)
        {
            break;
        }
    }

    const FString NewRevision = bAnyChanged
        ? FLoomleWidgetAdapter::ComputeRevision_Public(WBP)
        : PreviousRevision;

    const bool bFullyApplied = !bDryRun && !bAnyFailed;
    const bool bPartialApplied = !bDryRun && bAnyFailed && bAnyChanged;

    Payload->SetBoolField(TEXT("applied"), bFullyApplied);
    Payload->SetBoolField(TEXT("partialApplied"), bPartialApplied);
    Payload->SetStringField(TEXT("assetPath"), AssetPath);
    Payload->SetStringField(TEXT("previousRevision"), PreviousRevision);
    Payload->SetStringField(TEXT("newRevision"), NewRevision);
    Payload->SetArrayField(TEXT("opResults"), OpResults);
    Payload->SetArrayField(TEXT("diagnostics"), TArray<TSharedPtr<FJsonValue>>{});
    return Payload;
}

// ---------------------------------------------------------------------------
// widget.describe
// ---------------------------------------------------------------------------

TSharedPtr<FJsonObject> FLoomleBridgeModule::BuildWidgetDescribeToolResult(
    const TSharedPtr<FJsonObject>& Arguments) const
{
    TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();

    if (!Arguments.IsValid())
    {
        Payload->SetBoolField(TEXT("isError"), true);
        Payload->SetStringField(TEXT("code"), TEXT("INVALID_ARGUMENT"));
        Payload->SetStringField(TEXT("message"), TEXT("INVALID_ARGUMENT"));
        Payload->SetStringField(TEXT("detail"), TEXT("No arguments provided."));
        return Payload;
    }

    FString WidgetClass;
    FString AssetPath;
    FString WidgetName;
    Arguments->TryGetStringField(TEXT("widgetClass"), WidgetClass);
    Arguments->TryGetStringField(TEXT("assetPath"), AssetPath);
    Arguments->TryGetStringField(TEXT("widgetName"), WidgetName);

    // Need at least widgetClass OR (assetPath + widgetName)
    const bool bHasClass = !WidgetClass.IsEmpty();
    const bool bHasInstance = !AssetPath.IsEmpty() && !WidgetName.IsEmpty();
    if (!bHasClass && !bHasInstance)
    {
        Payload->SetBoolField(TEXT("isError"), true);
        Payload->SetStringField(TEXT("code"), TEXT("INVALID_ARGUMENT"));
        Payload->SetStringField(TEXT("message"), TEXT("INVALID_ARGUMENT"));
        Payload->SetStringField(TEXT("detail"),
            TEXT("Provide widgetClass, or both assetPath and widgetName."));
        return Payload;
    }

    FString DescribeJson;
    FString Error;
    if (!FLoomleWidgetAdapter::DescribeWidgetClass(WidgetClass, AssetPath, WidgetName, DescribeJson, Error))
    {
        FString DomainCode = TEXT("INTERNAL_ERROR");
        if (Error.StartsWith(TEXT("WIDGET_CLASS_NOT_FOUND")))
        {
            DomainCode = TEXT("WIDGET_CLASS_NOT_FOUND");
        }
        else if (Error.StartsWith(TEXT("WIDGET_NOT_FOUND")))
        {
            DomainCode = TEXT("WIDGET_NOT_FOUND");
        }
        else if (Error.StartsWith(TEXT("WIDGET_TREE_UNAVAILABLE")))
        {
            DomainCode = TEXT("WIDGET_TREE_UNAVAILABLE");
        }
        Payload->SetBoolField(TEXT("isError"), true);
        Payload->SetStringField(TEXT("code"), DomainCode);
        Payload->SetStringField(TEXT("message"), DomainCode);
        Payload->SetStringField(TEXT("detail"), Error);
        return Payload;
    }

    TSharedPtr<FJsonObject> DescribeObj;
    TSharedRef<TJsonReader<TCHAR>> Reader = TJsonReaderFactory<TCHAR>::Create(DescribeJson);
    if (!FJsonSerializer::Deserialize(Reader, DescribeObj) || !DescribeObj.IsValid())
    {
        Payload->SetBoolField(TEXT("isError"), true);
        Payload->SetStringField(TEXT("code"), TEXT("INTERNAL_ERROR"));
        Payload->SetStringField(TEXT("message"), TEXT("INTERNAL_ERROR"));
        Payload->SetStringField(TEXT("detail"), TEXT("Failed to deserialize describe result JSON."));
        return Payload;
    }

    // Merge describe object fields into payload
    for (const auto& Pair : DescribeObj->Values)
    {
        Payload->SetField(Pair.Key, Pair.Value);
    }
    return Payload;
}

// ---------------------------------------------------------------------------
// widget.verify
// ---------------------------------------------------------------------------

TSharedPtr<FJsonObject> FLoomleBridgeModule::BuildWidgetVerifyToolResult(
    const TSharedPtr<FJsonObject>& Arguments) const
{
    TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();

    FString AssetPath;
    if (!Arguments.IsValid() || !Arguments->TryGetStringField(TEXT("assetPath"), AssetPath) || AssetPath.IsEmpty())
    {
        Payload->SetBoolField(TEXT("isError"), true);
        Payload->SetStringField(TEXT("code"), TEXT("INVALID_ARGUMENT"));
        Payload->SetStringField(TEXT("message"), TEXT("INVALID_ARGUMENT"));
        Payload->SetStringField(TEXT("detail"), TEXT("field assetPath is required"));
        return Payload;
    }

    FString DiagsJson;
    FString Error;
    if (!FLoomleWidgetAdapter::CompileWidgetBlueprint(AssetPath, DiagsJson, Error))
    {
        FString DomainCode = TEXT("INTERNAL_ERROR");
        if (Error.StartsWith(TEXT("WIDGET_TREE_UNAVAILABLE")))
        {
            DomainCode = TEXT("WIDGET_TREE_UNAVAILABLE");
        }
        Payload->SetBoolField(TEXT("isError"), true);
        Payload->SetStringField(TEXT("code"), DomainCode);
        Payload->SetStringField(TEXT("message"), DomainCode);
        Payload->SetStringField(TEXT("detail"), Error);
        return Payload;
    }

    TSharedPtr<FJsonObject> DiagsObj;
    TSharedRef<TJsonReader<TCHAR>> Reader = TJsonReaderFactory<TCHAR>::Create(DiagsJson);
    if (!FJsonSerializer::Deserialize(Reader, DiagsObj) || !DiagsObj.IsValid())
    {
        Payload->SetBoolField(TEXT("isError"), true);
        Payload->SetStringField(TEXT("code"), TEXT("INTERNAL_ERROR"));
        Payload->SetStringField(TEXT("message"), TEXT("INTERNAL_ERROR"));
        Payload->SetStringField(TEXT("detail"), TEXT("Failed to parse compile diagnostics."));
        return Payload;
    }

    const TArray<TSharedPtr<FJsonValue>>* Diags = nullptr;
    DiagsObj->TryGetArrayField(TEXT("diagnostics"), Diags);

    bool bHasErrors = false;
    if (Diags)
    {
        for (const TSharedPtr<FJsonValue>& D : *Diags)
        {
            const TSharedPtr<FJsonObject>* DObj = nullptr;
            FString Severity;
            if (D->TryGetObject(DObj) && DObj && (*DObj)->TryGetStringField(TEXT("severity"), Severity)
                && Severity.Equals(TEXT("error")))
            {
                bHasErrors = true;
                break;
            }
        }
    }

    Payload->SetStringField(TEXT("status"), bHasErrors ? TEXT("error") : TEXT("ok"));
    Payload->SetStringField(TEXT("assetPath"), AssetPath);
    Payload->SetArrayField(TEXT("diagnostics"), Diags ? *Diags : TArray<TSharedPtr<FJsonValue>>{});
    return Payload;
}
