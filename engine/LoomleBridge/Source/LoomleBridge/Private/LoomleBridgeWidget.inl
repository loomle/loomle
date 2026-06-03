// Copyright 2026 Loomle contributors.

// Widget tool handlers for Loomle Bridge.
// Included by LoomleBridgeModule.cpp after shared graph-domain helpers.

#include "Blueprint/WidgetTree.h"
#include "Components/PanelWidget.h"

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

void SplitWidgetError(const FString& Error, FString& OutCode, FString& OutMessage)
{
    OutCode = TEXT("INVALID_ARGUMENT");
    OutMessage = Error;
    int32 ColonIdx = INDEX_NONE;
    if (Error.FindChar(TEXT(':'), ColonIdx) && ColonIdx > 0)
    {
        const FString Prefix = Error.Left(ColonIdx);
        if (!Prefix.Contains(TEXT(" ")))
        {
            OutCode = Prefix;
            OutMessage = Error.Mid(ColonIdx + 1).TrimStart();
        }
    }
}

TSharedPtr<FJsonObject> MakeWidgetRef(const FString& Name)
{
    TSharedPtr<FJsonObject> Ref = MakeShared<FJsonObject>();
    Ref->SetStringField(TEXT("name"), Name);
    return Ref;
}

TSharedPtr<FJsonObject> MakeWidgetTreeChange(
    const FString& Kind,
    const FString& TargetName,
    const TSharedPtr<FJsonValue>& Before = nullptr,
    const TSharedPtr<FJsonValue>& After = nullptr)
{
    return LoomleMutation::MakeChange(
        Kind,
        LoomleMutation::MakeTarget(TEXT("widget"), TargetName),
        Before,
        After);
}

bool TryFindWidgetTemplate(UWidgetBlueprint* WBP, const FString& Name, UWidget*& OutWidget)
{
    OutWidget = nullptr;
    if (!WBP || !WBP->WidgetTree || Name.IsEmpty())
    {
        return false;
    }
    OutWidget = WBP->WidgetTree->FindWidget(FName(*Name));
    return OutWidget != nullptr;
}

bool ValidateWidgetPropertyImport(UObject* Owner, FProperty* Prop, const FString& Value, FString& OutError)
{
    if (!Owner || !Prop)
    {
        OutError = TEXT("PROPERTY_NOT_FOUND: Property target is unavailable.");
        return false;
    }

    void* TempValue = FMemory::Malloc(Prop->GetSize(), Prop->GetMinAlignment());
    Prop->InitializeValue(TempValue);
    Prop->CopyCompleteValue(TempValue, Prop->ContainerPtrToValuePtr<void>(Owner));
    const TCHAR* ImportResult = Prop->ImportText_Direct(*Value, TempValue, nullptr, PPF_None);
    Prop->DestroyValue(TempValue);
    FMemory::Free(TempValue);
    if (!ImportResult)
    {
        OutError = FString::Printf(
            TEXT("PROPERTY_SET_FAILED: Could not import value '%s' for property '%s'."),
            *Value,
            *Prop->GetName());
        return false;
    }
    return true;
}

bool ValidateWidgetTreeOp(
    UWidgetBlueprint* WBP,
    const FString& OpName,
    const TSharedPtr<FJsonObject>& Args,
    TMap<FString, UClass*>& PlannedAdds,
    FString& OutError)
{
    if (OpName.Equals(TEXT("addWidget")))
    {
        FString WidgetClassPath;
        FString Name;
        FString Parent;
        Args->TryGetStringField(TEXT("widgetClass"), WidgetClassPath);
        Args->TryGetStringField(TEXT("name"), Name);
        Args->TryGetStringField(TEXT("parentName"), Parent);
        if (Parent.IsEmpty())
        {
            Args->TryGetStringField(TEXT("parent"), Parent);
        }
        if (WidgetClassPath.IsEmpty() || Name.IsEmpty())
        {
            OutError = TEXT("INVALID_ARGUMENT: addWidget requires widgetClass and name.");
            return false;
        }
        UClass* WidgetClass = LoadClass<UWidget>(nullptr, *WidgetClassPath);
        if (!WidgetClass)
        {
            OutError = FString::Printf(TEXT("WIDGET_CLASS_NOT_FOUND: Could not load widget class '%s'."), *WidgetClassPath);
            return false;
        }
        UWidget* ExistingWidget = nullptr;
        if (TryFindWidgetTemplate(WBP, Name, ExistingWidget) || PlannedAdds.Contains(Name))
        {
            OutError = FString::Printf(TEXT("WIDGET_ALREADY_EXISTS: A widget named '%s' already exists in the WidgetTree or current batch."), *Name);
            return false;
        }
        if (!Parent.IsEmpty() && !Parent.Equals(TEXT("root"), ESearchCase::IgnoreCase))
        {
            UWidget* ParentWidget = nullptr;
            if (TryFindWidgetTemplate(WBP, Parent, ParentWidget))
            {
                if (!ParentWidget->IsA(UPanelWidget::StaticClass()))
                {
                    OutError = FString::Printf(TEXT("WIDGET_PARENT_NOT_PANEL: Parent widget '%s' is not a UPanelWidget and cannot have children."), *Parent);
                    return false;
                }
            }
            else if (UClass** PlannedParentClass = PlannedAdds.Find(Parent))
            {
                if (!(*PlannedParentClass)->IsChildOf(UPanelWidget::StaticClass()))
                {
                    OutError = FString::Printf(TEXT("WIDGET_PARENT_NOT_PANEL: Planned parent widget '%s' is not a UPanelWidget and cannot have children."), *Parent);
                    return false;
                }
            }
            else
            {
                OutError = FString::Printf(TEXT("WIDGET_NOT_FOUND: Parent widget '%s' not found."), *Parent);
                return false;
            }
        }
        PlannedAdds.Add(Name, WidgetClass);
        return true;
    }

    if (OpName.Equals(TEXT("removeWidget")))
    {
        FString Name;
        if (!ResolveWidgetName(Args, Name))
        {
            OutError = TEXT("INVALID_ARGUMENT: removeWidget requires args.name or args.target.name.");
            return false;
        }
        UWidget* Target = nullptr;
        if (!TryFindWidgetTemplate(WBP, Name, Target) && !PlannedAdds.Contains(Name))
        {
            OutError = FString::Printf(TEXT("WIDGET_NOT_FOUND: Widget '%s' not found."), *Name);
            return false;
        }
        PlannedAdds.Remove(Name);
        return true;
    }

    if (OpName.Equals(TEXT("renameWidget")))
    {
        FString OldName;
        FString NewName;
        if (!ResolveWidgetName(Args, OldName))
        {
            OutError = TEXT("INVALID_ARGUMENT: renameWidget requires args.name or args.target.name.");
            return false;
        }
        if (!Args->TryGetStringField(TEXT("newName"), NewName) || NewName.IsEmpty())
        {
            OutError = TEXT("INVALID_ARGUMENT: renameWidget requires args.newName.");
            return false;
        }
        UWidget* Target = nullptr;
        if (!TryFindWidgetTemplate(WBP, OldName, Target) && !PlannedAdds.Contains(OldName))
        {
            OutError = FString::Printf(TEXT("WIDGET_NOT_FOUND: Widget '%s' not found."), *OldName);
            return false;
        }
        UWidget* ExistingNew = nullptr;
        if (TryFindWidgetTemplate(WBP, NewName, ExistingNew) || PlannedAdds.Contains(NewName))
        {
            OutError = FString::Printf(TEXT("WIDGET_ALREADY_EXISTS: A widget named '%s' already exists in the WidgetTree or current batch."), *NewName);
            return false;
        }
        if (UClass** PlannedClass = PlannedAdds.Find(OldName))
        {
            PlannedAdds.Add(NewName, *PlannedClass);
            PlannedAdds.Remove(OldName);
        }
        return true;
    }

    if (OpName.Equals(TEXT("reparentWidget")))
    {
        FString Name;
        FString NewParent;
        if (!ResolveWidgetName(Args, Name))
        {
            OutError = TEXT("INVALID_ARGUMENT: reparentWidget requires args.name or args.target.name.");
            return false;
        }
        if (!Args->TryGetStringField(TEXT("newParent"), NewParent) || NewParent.IsEmpty())
        {
            OutError = TEXT("INVALID_ARGUMENT: reparentWidget requires args.newParent.");
            return false;
        }
        UWidget* Target = nullptr;
        if (!TryFindWidgetTemplate(WBP, Name, Target) && !PlannedAdds.Contains(Name))
        {
            OutError = FString::Printf(TEXT("WIDGET_NOT_FOUND: Widget '%s' not found."), *Name);
            return false;
        }
        UWidget* NewParentWidget = nullptr;
        if (TryFindWidgetTemplate(WBP, NewParent, NewParentWidget))
        {
            if (!NewParentWidget->IsA(UPanelWidget::StaticClass()))
            {
                OutError = FString::Printf(TEXT("WIDGET_PARENT_NOT_PANEL: Widget '%s' is not a UPanelWidget and cannot have children."), *NewParent);
                return false;
            }
        }
        else if (UClass** PlannedParentClass = PlannedAdds.Find(NewParent))
        {
            if (!(*PlannedParentClass)->IsChildOf(UPanelWidget::StaticClass()))
            {
                OutError = FString::Printf(TEXT("WIDGET_PARENT_NOT_PANEL: Planned widget '%s' is not a UPanelWidget and cannot have children."), *NewParent);
                return false;
            }
        }
        else
        {
            OutError = FString::Printf(TEXT("WIDGET_NOT_FOUND: New parent widget '%s' not found."), *NewParent);
            return false;
        }
        return true;
    }

    OutError = FString::Printf(TEXT("UNSUPPORTED_OP: Unknown widget op '%s'."), *OpName);
    return false;
}

bool ValidateWidgetEditOp(
    UWidgetBlueprint* WBP,
    const FString& OpName,
    const TSharedPtr<FJsonObject>& Args,
    FString& OutError)
{
    const bool bSlotProperty = OpName.Equals(TEXT("setSlotProperty"));
    if (!OpName.Equals(TEXT("setProperty")) && !bSlotProperty)
    {
        OutError = FString::Printf(TEXT("UNSUPPORTED_OP: Unknown widget edit op '%s'."), *OpName);
        return false;
    }

    FString Name;
    FString PropertyName;
    FString Value;
    if (!ResolveWidgetName(Args, Name))
    {
        OutError = FString::Printf(TEXT("INVALID_ARGUMENT: %s requires args.name or args.target.name."), *OpName);
        return false;
    }
    if (!Args->TryGetStringField(TEXT("property"), PropertyName) || PropertyName.IsEmpty())
    {
        OutError = FString::Printf(TEXT("INVALID_ARGUMENT: %s requires args.property."), *OpName);
        return false;
    }
    if (!Args->TryGetStringField(TEXT("value"), Value))
    {
        OutError = FString::Printf(TEXT("INVALID_ARGUMENT: %s requires args.value."), *OpName);
        return false;
    }

    UWidget* Target = nullptr;
    if (!TryFindWidgetTemplate(WBP, Name, Target))
    {
        OutError = FString::Printf(TEXT("WIDGET_NOT_FOUND: Widget '%s' not found."), *Name);
        return false;
    }

    UObject* PropOwner = Target;
    FProperty* Prop = nullptr;
    if (bSlotProperty)
    {
        if (!Target->Slot)
        {
            OutError = FString::Printf(TEXT("SLOT_NOT_FOUND: Widget '%s' has no slot."), *Name);
            return false;
        }
        PropOwner = Target->Slot;
        Prop = FindFProperty<FProperty>(Target->Slot->GetClass(), *PropertyName);
    }
    else
    {
        Prop = FindFProperty<FProperty>(Target->GetClass(), *PropertyName);
    }
    if (!Prop)
    {
        OutError = FString::Printf(TEXT("PROPERTY_NOT_FOUND: Property '%s' not found for widget edit op '%s'."), *PropertyName, *OpName);
        return false;
    }
    return ValidateWidgetPropertyImport(PropOwner, Prop, Value, OutError);
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
// widget.tree.inspect
// ---------------------------------------------------------------------------

TSharedPtr<FJsonObject> FLoomleBridgeModule::BuildWidgetTreeInspectToolResult(
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

    Payload->SetBoolField(TEXT("isError"), false);
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
// widget.tree.edit
// ---------------------------------------------------------------------------

TSharedPtr<FJsonObject> FLoomleBridgeModule::BuildWidgetTreeEditToolResult(
    const TSharedPtr<FJsonObject>& Arguments)
{
    TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();

    FString AssetPath;
    if (!Arguments.IsValid() || !Arguments->TryGetStringField(TEXT("assetPath"), AssetPath) || AssetPath.IsEmpty())
    {
        LoomleMutation::SetFailure(Payload, TEXT("INVALID_ARGUMENT"), TEXT("widget.tree.edit requires assetPath."));
        return Payload;
    }
    AssetPath = NormalizeAssetPath(AssetPath);

    const TArray<TSharedPtr<FJsonValue>>* OpsArray = nullptr;
    if (!Arguments->TryGetArrayField(TEXT("ops"), OpsArray) || !OpsArray || OpsArray->IsEmpty())
    {
        LoomleMutation::SetFailure(Payload, TEXT("INVALID_ARGUMENT"), TEXT("widget.tree.edit requires ops."));
        return Payload;
    }

    bool bDryRun = false;
    Arguments->TryGetBoolField(TEXT("dryRun"), bDryRun);

    if (const TSharedPtr<FJsonObject> Blocked = BuildEditorMutationLifecycleBlockResult(
            TEXT("widget.tree.edit"),
            Arguments,
            AssetPath,
            TEXT("")))
    {
        return Blocked;
    }

    FString ObjectPath = AssetPath;
    if (!ObjectPath.Contains(TEXT(".")))
    {
        ObjectPath = AssetPath + TEXT(".") + FPackageName::GetShortName(AssetPath);
    }
    UObject* Loaded = StaticLoadObject(UWidgetBlueprint::StaticClass(), nullptr, *ObjectPath);
    UWidgetBlueprint* WBP = Cast<UWidgetBlueprint>(Loaded);
    if (!WBP || !WBP->WidgetTree)
    {
        LoomleMutation::SetFailure(
            Payload,
            TEXT("WIDGET_TREE_UNAVAILABLE"),
            FString::Printf(TEXT("Asset '%s' is not a WidgetBlueprint or has a null WidgetTree."), *AssetPath));
        return Payload;
    }

    const FString PreviousRevision = FLoomleWidgetAdapter::ComputeRevision_Public(WBP);
    FString ExpectedRevision;
    if (Arguments->TryGetStringField(TEXT("expectedRevision"), ExpectedRevision) && !ExpectedRevision.IsEmpty())
    {
        if (PreviousRevision != ExpectedRevision)
        {
            LoomleMutation::SetMutationEnvelope(Payload, TEXT("widget.tree.edit"), AssetPath, TEXT("widget.tree.edit"), bDryRun, false, false);
            LoomleMutation::SetRevisionConflict(Payload, ExpectedRevision, PreviousRevision);
            return Payload;
        }
    }

    TArray<TSharedPtr<FJsonValue>> OpResults;
    TArray<TSharedPtr<FJsonValue>> Changes;
    TMap<FString, UClass*> PlannedAdds;

    for (int32 i = 0; i < OpsArray->Num(); ++i)
    {
        const TSharedPtr<FJsonObject>* OpObjPtr = nullptr;
        if (!(*OpsArray)[i]->TryGetObject(OpObjPtr) || !OpObjPtr || !(*OpObjPtr).IsValid())
        {
            OpResults.Add(MakeShared<FJsonValueObject>(LoomleMutation::MakeOpResult(
                i,
                TEXT(""),
                false,
                false,
                TEXT("INVALID_ARGUMENT"),
                TEXT("Op entry is not a valid object."))));
            LoomleMutation::SetMutationEnvelope(Payload, TEXT("widget.tree.edit"), AssetPath, TEXT("widget.tree.edit"), bDryRun, false, false);
            Payload->SetArrayField(TEXT("opResults"), OpResults);
            LoomleMutation::SetFailure(Payload, TEXT("INVALID_ARGUMENT"), TEXT("Op entry is not a valid object."));
            LoomleMutation::SetUnchangedRevision(Payload, PreviousRevision);
            return Payload;
        }

        const TSharedPtr<FJsonObject>& OpObj = *OpObjPtr;
        FString OpName;
        if (!OpObj->TryGetStringField(TEXT("op"), OpName) || OpName.IsEmpty())
        {
            OpResults.Add(MakeShared<FJsonValueObject>(LoomleMutation::MakeOpResult(
                i,
                TEXT(""),
                false,
                false,
                TEXT("INVALID_ARGUMENT"),
                TEXT("Op entry requires op."))));
            LoomleMutation::SetMutationEnvelope(Payload, TEXT("widget.tree.edit"), AssetPath, TEXT("widget.tree.edit"), bDryRun, false, false);
            Payload->SetArrayField(TEXT("opResults"), OpResults);
            LoomleMutation::SetFailure(Payload, TEXT("INVALID_ARGUMENT"), TEXT("Op entry requires op."));
            LoomleMutation::SetUnchangedRevision(Payload, PreviousRevision);
            return Payload;
        }

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

        FString ValidationError;
        if (!ValidateWidgetTreeOp(WBP, OpName, Args, PlannedAdds, ValidationError))
        {
            FString ErrorCode;
            FString ErrorMessage;
            SplitWidgetError(ValidationError, ErrorCode, ErrorMessage);
            OpResults.Add(MakeShared<FJsonValueObject>(LoomleMutation::MakeOpResult(
                i,
                OpName,
                false,
                false,
                ErrorCode,
                ErrorMessage)));
            LoomleMutation::SetMutationEnvelope(Payload, TEXT("widget.tree.edit"), AssetPath, TEXT("widget.tree.edit"), bDryRun, false, false);
            Payload->SetArrayField(TEXT("opResults"), OpResults);
            LoomleMutation::SetFailure(Payload, ErrorCode, ErrorMessage);
            LoomleMutation::SetUnchangedRevision(Payload, PreviousRevision);
            return Payload;
        }

        OpResults.Add(MakeShared<FJsonValueObject>(LoomleMutation::MakeOpResult(i, OpName, true, false)));

        FString TargetName;
        ResolveWidgetName(Args, TargetName);
        if (OpName.Equals(TEXT("addWidget")))
        {
            Args->TryGetStringField(TEXT("name"), TargetName);
            Changes.Add(MakeShared<FJsonValueObject>(
                MakeWidgetTreeChange(TEXT("create"), TargetName, nullptr, MakeShared<FJsonValueObject>(Args))));
        }
        else if (OpName.Equals(TEXT("renameWidget")))
        {
            FString NewName;
            Args->TryGetStringField(TEXT("newName"), NewName);
            TSharedPtr<FJsonObject> Before = MakeWidgetRef(TargetName);
            TSharedPtr<FJsonObject> After = MakeWidgetRef(NewName);
            Changes.Add(MakeShared<FJsonValueObject>(
                MakeWidgetTreeChange(TEXT("rename"), TargetName, MakeShared<FJsonValueObject>(Before), MakeShared<FJsonValueObject>(After))));
        }
        else if (OpName.Equals(TEXT("removeWidget")))
        {
            Changes.Add(MakeShared<FJsonValueObject>(
                MakeWidgetTreeChange(TEXT("delete"), TargetName, MakeShared<FJsonValueObject>(MakeWidgetRef(TargetName)), nullptr)));
        }
        else if (OpName.Equals(TEXT("reparentWidget")))
        {
            Changes.Add(MakeShared<FJsonValueObject>(
                MakeWidgetTreeChange(TEXT("reparent"), TargetName, nullptr, MakeShared<FJsonValueObject>(Args))));
        }
    }

    TSharedPtr<FJsonObject> Planned = LoomleMutation::BuildBatchPlanFromOpResults(
        TEXT("widget.tree.edit"),
        AssetPath,
        TEXT("widget.tree.edit"),
        OpsArray->Num(),
        OpResults);

    Payload->SetObjectField(TEXT("planned"), Planned);
    const TSharedPtr<FJsonObject>* ResolvedRefs = nullptr;
    if (Planned->TryGetObjectField(TEXT("resolvedRefs"), ResolvedRefs) && ResolvedRefs != nullptr && ResolvedRefs->IsValid())
    {
        Payload->SetObjectField(TEXT("resolvedRefs"), *ResolvedRefs);
    }
    Payload->SetArrayField(TEXT("opResults"), OpResults);
    LoomleMutation::SetDiff(Payload, TEXT("widget.tree"), Changes);
    LoomleMutation::SetMutationEnvelope(Payload, TEXT("widget.tree.edit"), AssetPath, TEXT("widget.tree.edit"), bDryRun, false, true);
    LoomleMutation::SetUnchangedRevision(Payload, PreviousRevision);

    if (bDryRun)
    {
        return Payload;
    }

    for (int32 i = 0; i < OpsArray->Num(); ++i)
    {
        const TSharedPtr<FJsonObject>* OpObjPtr = nullptr;
        (*OpsArray)[i]->TryGetObject(OpObjPtr);
        const TSharedPtr<FJsonObject>& OpObj = *OpObjPtr;
        FString OpName;
        OpObj->TryGetStringField(TEXT("op"), OpName);
        const TSharedPtr<FJsonObject>* ArgsObjPtr = nullptr;
        TSharedPtr<FJsonObject> Args = MakeShared<FJsonObject>();
        if (OpObj->TryGetObjectField(TEXT("args"), ArgsObjPtr) && ArgsObjPtr)
        {
            Args = *ArgsObjPtr;
        }

        FString OpError;
        bool bOpOk = false;
        if (OpName.Equals(TEXT("addWidget")))
        {
            FString WidgetClass, Name, Parent;
            Args->TryGetStringField(TEXT("widgetClass"), WidgetClass);
            Args->TryGetStringField(TEXT("name"), Name);
            Args->TryGetStringField(TEXT("parentName"), Parent);
            if (Parent.IsEmpty())
            {
                Args->TryGetStringField(TEXT("parent"), Parent);
            }
            const TSharedPtr<FJsonObject>* SlotObj = nullptr;
            TSharedPtr<FJsonObject> SlotArgs;
            if (Args->TryGetObjectField(TEXT("slot"), SlotObj) && SlotObj)
            {
                SlotArgs = *SlotObj;
            }
            bOpOk = FLoomleWidgetAdapter::AddWidget(WBP, WidgetClass, Name, Parent, SlotArgs, OpError);
        }
        else if (OpName.Equals(TEXT("removeWidget")))
        {
            FString Name;
            ResolveWidgetName(Args, Name);
            bOpOk = FLoomleWidgetAdapter::RemoveWidget(WBP, Name, OpError);
        }
        else if (OpName.Equals(TEXT("renameWidget")))
        {
            FString OldName;
            FString NewName;
            ResolveWidgetName(Args, OldName);
            Args->TryGetStringField(TEXT("newName"), NewName);
            bOpOk = FLoomleWidgetAdapter::RenameWidget(WBP, OldName, NewName, OpError);
        }
        else if (OpName.Equals(TEXT("reparentWidget")))
        {
            FString Name, NewParent;
            ResolveWidgetName(Args, Name);
            Args->TryGetStringField(TEXT("newParent"), NewParent);
            const TSharedPtr<FJsonObject>* SlotObj = nullptr;
            TSharedPtr<FJsonObject> SlotArgs;
            if (Args->TryGetObjectField(TEXT("slot"), SlotObj) && SlotObj)
            {
                SlotArgs = *SlotObj;
            }
            bOpOk = FLoomleWidgetAdapter::ReparentWidget(WBP, Name, NewParent, SlotArgs, OpError);
        }

        if (!bOpOk)
        {
            FString ErrorCode;
            FString ErrorMessage;
            SplitWidgetError(OpError, ErrorCode, ErrorMessage);
            TArray<TSharedPtr<FJsonValue>> FailedOpResults = OpResults;
            FailedOpResults[i] = MakeShared<FJsonValueObject>(LoomleMutation::MakeOpResult(i, OpName, false, false, ErrorCode, ErrorMessage));
            Payload->SetArrayField(TEXT("opResults"), FailedOpResults);
            LoomleMutation::SetFailure(Payload, ErrorCode, ErrorMessage);
            LoomleMutation::SetRevision(Payload, PreviousRevision, FLoomleWidgetAdapter::ComputeRevision_Public(WBP));
            return Payload;
        }
    }

    for (const TSharedPtr<FJsonValue>& OpResultValue : OpResults)
    {
        const TSharedPtr<FJsonObject>* OpResultObject = nullptr;
        if (OpResultValue.IsValid()
            && OpResultValue->TryGetObject(OpResultObject)
            && OpResultObject != nullptr
            && (*OpResultObject).IsValid())
        {
            (*OpResultObject)->SetBoolField(TEXT("changed"), true);
        }
    }
    Payload->SetArrayField(TEXT("opResults"), OpResults);
    const FString NewRevision = FLoomleWidgetAdapter::ComputeRevision_Public(WBP);
    LoomleMutation::SetMutationEnvelope(Payload, TEXT("widget.tree.edit"), AssetPath, TEXT("widget.tree.edit"), false, true, true);
    LoomleMutation::SetRevision(Payload, PreviousRevision, NewRevision);
    return Payload;
}

// ---------------------------------------------------------------------------
// widget.edit
// ---------------------------------------------------------------------------

TSharedPtr<FJsonObject> FLoomleBridgeModule::BuildWidgetEditToolResult(
    const TSharedPtr<FJsonObject>& Arguments)
{
    TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();

    FString AssetPath;
    if (!Arguments.IsValid() || !Arguments->TryGetStringField(TEXT("assetPath"), AssetPath) || AssetPath.IsEmpty())
    {
        LoomleMutation::SetFailure(Payload, TEXT("INVALID_ARGUMENT"), TEXT("widget.edit requires assetPath."));
        return Payload;
    }
    AssetPath = NormalizeAssetPath(AssetPath);

    const TArray<TSharedPtr<FJsonValue>>* OpsArray = nullptr;
    if (!Arguments->TryGetArrayField(TEXT("ops"), OpsArray) || !OpsArray || OpsArray->IsEmpty())
    {
        LoomleMutation::SetFailure(Payload, TEXT("INVALID_ARGUMENT"), TEXT("widget.edit requires ops."));
        return Payload;
    }

    bool bDryRun = false;
    Arguments->TryGetBoolField(TEXT("dryRun"), bDryRun);

    if (const TSharedPtr<FJsonObject> Blocked = BuildEditorMutationLifecycleBlockResult(
            TEXT("widget.edit"),
            Arguments,
            AssetPath,
            TEXT("")))
    {
        return Blocked;
    }

    FString ObjectPath = AssetPath;
    if (!ObjectPath.Contains(TEXT(".")))
    {
        ObjectPath = AssetPath + TEXT(".") + FPackageName::GetShortName(AssetPath);
    }
    UObject* Loaded = StaticLoadObject(UWidgetBlueprint::StaticClass(), nullptr, *ObjectPath);
    UWidgetBlueprint* WBP = Cast<UWidgetBlueprint>(Loaded);
    if (!WBP || !WBP->WidgetTree)
    {
        LoomleMutation::SetFailure(
            Payload,
            TEXT("WIDGET_TREE_UNAVAILABLE"),
            FString::Printf(TEXT("Asset '%s' is not a WidgetBlueprint or has a null WidgetTree."), *AssetPath));
        return Payload;
    }

    const FString PreviousRevision = FLoomleWidgetAdapter::ComputeRevision_Public(WBP);
    FString ExpectedRevision;
    if (Arguments->TryGetStringField(TEXT("expectedRevision"), ExpectedRevision) && !ExpectedRevision.IsEmpty())
    {
        if (PreviousRevision != ExpectedRevision)
        {
            LoomleMutation::SetMutationEnvelope(Payload, TEXT("widget.edit"), AssetPath, TEXT("widget.edit"), bDryRun, false, false);
            LoomleMutation::SetRevisionConflict(Payload, ExpectedRevision, PreviousRevision);
            return Payload;
        }
    }

    TArray<TSharedPtr<FJsonValue>> OpResults;
    TArray<TSharedPtr<FJsonValue>> Changes;
    for (int32 i = 0; i < OpsArray->Num(); ++i)
    {
        const TSharedPtr<FJsonObject>* OpObjPtr = nullptr;
        if (!(*OpsArray)[i]->TryGetObject(OpObjPtr) || !OpObjPtr || !(*OpObjPtr).IsValid())
        {
            OpResults.Add(MakeShared<FJsonValueObject>(LoomleMutation::MakeOpResult(i, TEXT(""), false, false, TEXT("INVALID_ARGUMENT"), TEXT("Op entry is not a valid object."))));
            LoomleMutation::SetMutationEnvelope(Payload, TEXT("widget.edit"), AssetPath, TEXT("widget.edit"), bDryRun, false, false);
            Payload->SetArrayField(TEXT("opResults"), OpResults);
            LoomleMutation::SetFailure(Payload, TEXT("INVALID_ARGUMENT"), TEXT("Op entry is not a valid object."));
            LoomleMutation::SetUnchangedRevision(Payload, PreviousRevision);
            return Payload;
        }

        const TSharedPtr<FJsonObject>& OpObj = *OpObjPtr;
        FString OpName;
        if (!OpObj->TryGetStringField(TEXT("op"), OpName) || OpName.IsEmpty())
        {
            OpResults.Add(MakeShared<FJsonValueObject>(LoomleMutation::MakeOpResult(i, TEXT(""), false, false, TEXT("INVALID_ARGUMENT"), TEXT("Op entry requires op."))));
            LoomleMutation::SetMutationEnvelope(Payload, TEXT("widget.edit"), AssetPath, TEXT("widget.edit"), bDryRun, false, false);
            Payload->SetArrayField(TEXT("opResults"), OpResults);
            LoomleMutation::SetFailure(Payload, TEXT("INVALID_ARGUMENT"), TEXT("Op entry requires op."));
            LoomleMutation::SetUnchangedRevision(Payload, PreviousRevision);
            return Payload;
        }

        const TSharedPtr<FJsonObject>* ArgsObjPtr = nullptr;
        TSharedPtr<FJsonObject> Args = MakeShared<FJsonObject>();
        if (OpObj->TryGetObjectField(TEXT("args"), ArgsObjPtr) && ArgsObjPtr)
        {
            Args = *ArgsObjPtr;
        }

        FString ValidationError;
        if (!ValidateWidgetEditOp(WBP, OpName, Args, ValidationError))
        {
            FString ErrorCode;
            FString ErrorMessage;
            SplitWidgetError(ValidationError, ErrorCode, ErrorMessage);
            OpResults.Add(MakeShared<FJsonValueObject>(LoomleMutation::MakeOpResult(i, OpName, false, false, ErrorCode, ErrorMessage)));
            LoomleMutation::SetMutationEnvelope(Payload, TEXT("widget.edit"), AssetPath, TEXT("widget.edit"), bDryRun, false, false);
            Payload->SetArrayField(TEXT("opResults"), OpResults);
            LoomleMutation::SetFailure(Payload, ErrorCode, ErrorMessage);
            LoomleMutation::SetUnchangedRevision(Payload, PreviousRevision);
            return Payload;
        }

        OpResults.Add(MakeShared<FJsonValueObject>(LoomleMutation::MakeOpResult(i, OpName, true, false)));
        FString TargetName;
        ResolveWidgetName(Args, TargetName);
        Changes.Add(MakeShared<FJsonValueObject>(
            MakeWidgetTreeChange(TEXT("update"), TargetName, nullptr, MakeShared<FJsonValueObject>(Args))));
    }

    TSharedPtr<FJsonObject> Planned = LoomleMutation::BuildBatchPlanFromOpResults(
        TEXT("widget.edit"),
        AssetPath,
        TEXT("widget.edit"),
        OpsArray->Num(),
        OpResults);
    Payload->SetObjectField(TEXT("planned"), Planned);
    const TSharedPtr<FJsonObject>* ResolvedRefs = nullptr;
    if (Planned->TryGetObjectField(TEXT("resolvedRefs"), ResolvedRefs) && ResolvedRefs != nullptr && ResolvedRefs->IsValid())
    {
        Payload->SetObjectField(TEXT("resolvedRefs"), *ResolvedRefs);
    }
    Payload->SetArrayField(TEXT("opResults"), OpResults);
    LoomleMutation::SetDiff(Payload, TEXT("widget"), Changes);
    LoomleMutation::SetMutationEnvelope(Payload, TEXT("widget.edit"), AssetPath, TEXT("widget.edit"), bDryRun, false, true);
    LoomleMutation::SetUnchangedRevision(Payload, PreviousRevision);
    if (bDryRun)
    {
        return Payload;
    }

    for (int32 i = 0; i < OpsArray->Num(); ++i)
    {
        const TSharedPtr<FJsonObject>* OpObjPtr = nullptr;
        (*OpsArray)[i]->TryGetObject(OpObjPtr);
        const TSharedPtr<FJsonObject>& OpObj = *OpObjPtr;
        FString OpName;
        OpObj->TryGetStringField(TEXT("op"), OpName);
        const TSharedPtr<FJsonObject>* ArgsObjPtr = nullptr;
        TSharedPtr<FJsonObject> Args = MakeShared<FJsonObject>();
        if (OpObj->TryGetObjectField(TEXT("args"), ArgsObjPtr) && ArgsObjPtr)
        {
            Args = *ArgsObjPtr;
        }

        FString Name, PropertyName, Value;
        ResolveWidgetName(Args, Name);
        Args->TryGetStringField(TEXT("property"), PropertyName);
        Args->TryGetStringField(TEXT("value"), Value);

        FString OpError;
        const bool bSlotProperty = OpName.Equals(TEXT("setSlotProperty"));
        const bool bOpOk = FLoomleWidgetAdapter::SetWidgetProperty(WBP, Name, PropertyName, Value, bSlotProperty, OpError);
        if (!bOpOk)
        {
            FString ErrorCode;
            FString ErrorMessage;
            SplitWidgetError(OpError, ErrorCode, ErrorMessage);
            TArray<TSharedPtr<FJsonValue>> FailedOpResults = OpResults;
            FailedOpResults[i] = MakeShared<FJsonValueObject>(LoomleMutation::MakeOpResult(i, OpName, false, false, ErrorCode, ErrorMessage));
            Payload->SetArrayField(TEXT("opResults"), FailedOpResults);
            LoomleMutation::SetFailure(Payload, ErrorCode, ErrorMessage);
            LoomleMutation::SetRevision(Payload, PreviousRevision, FLoomleWidgetAdapter::ComputeRevision_Public(WBP));
            return Payload;
        }
    }

    for (const TSharedPtr<FJsonValue>& OpResultValue : OpResults)
    {
        const TSharedPtr<FJsonObject>* OpResultObject = nullptr;
        if (OpResultValue.IsValid()
            && OpResultValue->TryGetObject(OpResultObject)
            && OpResultObject != nullptr
            && (*OpResultObject).IsValid())
        {
            (*OpResultObject)->SetBoolField(TEXT("changed"), true);
        }
    }
    Payload->SetArrayField(TEXT("opResults"), OpResults);
    LoomleMutation::SetMutationEnvelope(Payload, TEXT("widget.edit"), AssetPath, TEXT("widget.edit"), false, true, true);
    LoomleMutation::SetRevision(Payload, PreviousRevision, FLoomleWidgetAdapter::ComputeRevision_Public(WBP));
    return Payload;
}

// ---------------------------------------------------------------------------
// widget.inspect
// ---------------------------------------------------------------------------

TSharedPtr<FJsonObject> FLoomleBridgeModule::BuildWidgetInspectToolResult(
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
        else if (Error.StartsWith(TEXT("WIDGET_CLASS_MISMATCH")))
        {
            DomainCode = TEXT("WIDGET_CLASS_MISMATCH");
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
    Payload->SetBoolField(TEXT("isError"), false);
    for (const auto& Pair : DescribeObj->Values)
    {
        Payload->SetField(Pair.Key, Pair.Value);
    }
    return Payload;
}

// ---------------------------------------------------------------------------
// widget.event.create
// ---------------------------------------------------------------------------

TSharedPtr<FJsonObject> FLoomleBridgeModule::BuildWidgetEventCreateToolResult(
    const TSharedPtr<FJsonObject>& Arguments)
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

    FString WidgetName;
    const TSharedPtr<FJsonObject>* WidgetObj = nullptr;
    if (Arguments->TryGetObjectField(TEXT("widget"), WidgetObj) && WidgetObj && WidgetObj->IsValid())
    {
        (*WidgetObj)->TryGetStringField(TEXT("name"), WidgetName);
    }
    if (WidgetName.IsEmpty())
    {
        Arguments->TryGetStringField(TEXT("widgetName"), WidgetName);
    }
    if (WidgetName.IsEmpty())
    {
        Payload->SetBoolField(TEXT("isError"), true);
        Payload->SetStringField(TEXT("code"), TEXT("INVALID_ARGUMENT"));
        Payload->SetStringField(TEXT("message"), TEXT("INVALID_ARGUMENT"));
        Payload->SetStringField(TEXT("detail"), TEXT("widget.event.create requires widget.name."));
        return Payload;
    }

    FString EventName;
    if (!Arguments->TryGetStringField(TEXT("event"), EventName) || EventName.IsEmpty())
    {
        Payload->SetBoolField(TEXT("isError"), true);
        Payload->SetStringField(TEXT("code"), TEXT("INVALID_ARGUMENT"));
        Payload->SetStringField(TEXT("message"), TEXT("INVALID_ARGUMENT"));
        Payload->SetStringField(TEXT("detail"), TEXT("widget.event.create requires event."));
        return Payload;
    }

    bool bDryRun = false;
    Arguments->TryGetBoolField(TEXT("dryRun"), bDryRun);

    FString ResultJson;
    FString Error;
    if (!FLoomleWidgetAdapter::CreateWidgetEvent(AssetPath, WidgetName, EventName, bDryRun, ResultJson, Error))
    {
        FString DomainCode = TEXT("INTERNAL_ERROR");
        if (Error.Contains(TEXT(":")))
        {
            Error.Split(TEXT(":"), &DomainCode, nullptr);
        }
        Payload->SetBoolField(TEXT("isError"), true);
        Payload->SetStringField(TEXT("code"), DomainCode);
        Payload->SetStringField(TEXT("message"), DomainCode);
        Payload->SetStringField(TEXT("detail"), Error);
        return Payload;
    }

    TSharedPtr<FJsonObject> ResultObj;
    TSharedRef<TJsonReader<TCHAR>> Reader = TJsonReaderFactory<TCHAR>::Create(ResultJson);
    if (!FJsonSerializer::Deserialize(Reader, ResultObj) || !ResultObj.IsValid())
    {
        Payload->SetBoolField(TEXT("isError"), true);
        Payload->SetStringField(TEXT("code"), TEXT("INTERNAL_ERROR"));
        Payload->SetStringField(TEXT("message"), TEXT("INTERNAL_ERROR"));
        Payload->SetStringField(TEXT("detail"), TEXT("Failed to parse widget event result."));
        return Payload;
    }

    Payload->SetBoolField(TEXT("isError"), false);
    for (const auto& Pair : ResultObj->Values)
    {
        Payload->SetField(Pair.Key, Pair.Value);
    }
    return Payload;
}

// ---------------------------------------------------------------------------
// widget.compile
// ---------------------------------------------------------------------------

TSharedPtr<FJsonObject> FLoomleBridgeModule::BuildWidgetCompileToolResult(
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
