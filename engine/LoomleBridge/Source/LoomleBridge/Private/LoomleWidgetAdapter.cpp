#include "LoomleWidgetAdapter.h"

#include "Blueprint/WidgetBlueprintLibrary.h"
#include "Blueprint/WidgetTree.h"
#include "Components/PanelSlot.h"
#include "Components/PanelWidget.h"
#include "Components/Widget.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "EditorFramework/AssetImportData.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/CompilerResultsLog.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/SecureHash.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "UObject/Class.h"
#include "UObject/UObjectIterator.h"
#include "UObject/UObjectGlobals.h"
#include "WidgetBlueprint.h"

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

namespace
{

FString JsonObjectToString(const TSharedPtr<FJsonObject>& Obj)
{
    FString Out;
    TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
        TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Out);
    FJsonSerializer::Serialize(Obj.ToSharedRef(), Writer);
    return Out;
}

UWidgetBlueprint* LoadWidgetBlueprintAsset(const FString& AssetPath, FString& OutError)
{
    FString ObjectPath = AssetPath;
    if (!ObjectPath.Contains(TEXT(".")))
    {
        // Append short package name as the object name
        FString PkgName = FPackageName::GetShortName(AssetPath);
        ObjectPath = AssetPath + TEXT(".") + PkgName;
    }

    UObject* Loaded = StaticLoadObject(UWidgetBlueprint::StaticClass(), nullptr, *ObjectPath);
    UWidgetBlueprint* WBP = Cast<UWidgetBlueprint>(Loaded);
    if (!WBP)
    {
        OutError = FString::Printf(
            TEXT("WIDGET_TREE_UNAVAILABLE: Asset '%s' is not a WidgetBlueprint or could not be loaded."),
            *AssetPath);
        return nullptr;
    }
    if (!WBP->WidgetTree)
    {
        OutError = FString::Printf(
            TEXT("WIDGET_TREE_UNAVAILABLE: WidgetBlueprint '%s' has a null WidgetTree."),
            *AssetPath);
        return nullptr;
    }
    return WBP;
}

} // namespace

// ---------------------------------------------------------------------------
// Private helpers (static member implementations)
// ---------------------------------------------------------------------------

TSharedPtr<FJsonObject> FLoomleWidgetAdapter::SerializeWidget(UWidget* Widget, bool bIncludeSlotProperties)
{
    if (!Widget)
    {
        return nullptr;
    }

    TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
    Obj->SetStringField(TEXT("name"), Widget->GetName());
    Obj->SetStringField(TEXT("widgetClass"), Widget->GetClass()->GetPathName());

    // Slot properties (from the parent's perspective)
    if (bIncludeSlotProperties && Widget->Slot)
    {
        TSharedPtr<FJsonObject> SlotObj = MakeShared<FJsonObject>();
        UPanelSlot* PanelSlot = Widget->Slot;
        for (TFieldIterator<FProperty> PropIt(PanelSlot->GetClass()); PropIt; ++PropIt)
        {
            FProperty* Prop = *PropIt;
            if (Prop->HasAnyPropertyFlags(CPF_Transient | CPF_NativeAccessSpecifierPrivate))
            {
                continue;
            }
            FString ValueStr;
            const void* PropPtr = Prop->ContainerPtrToValuePtr<void>(PanelSlot);
            Prop->ExportTextItem_Direct(ValueStr, PropPtr, nullptr, nullptr, PPF_None);
            SlotObj->SetStringField(Prop->GetName(), ValueStr);
        }
        Obj->SetObjectField(TEXT("slot"), SlotObj);
    }

    // Children
    TArray<TSharedPtr<FJsonValue>> ChildArray;
    if (UPanelWidget* Panel = Cast<UPanelWidget>(Widget))
    {
        for (int32 i = 0; i < Panel->GetChildrenCount(); ++i)
        {
            TSharedPtr<FJsonObject> ChildObj = SerializeWidget(Panel->GetChildAt(i), bIncludeSlotProperties);
            if (ChildObj.IsValid())
            {
                ChildArray.Add(MakeShared<FJsonValueObject>(ChildObj));
            }
        }
    }
    Obj->SetArrayField(TEXT("children"), ChildArray);

    return Obj;
}

UWidget* FLoomleWidgetAdapter::FindWidgetByName(UWidgetBlueprint* WBP, const FString& Name)
{
    if (!WBP || !WBP->WidgetTree)
    {
        return nullptr;
    }
    UWidget* Found = nullptr;
    WBP->WidgetTree->ForEachWidget([&](UWidget* Widget)
    {
        if (!Found && Widget->GetName() == Name)
        {
            Found = Widget;
        }
    });
    return Found;
}

void FLoomleWidgetAdapter::ApplySlotProperties(UPanelSlot* Slot, const TSharedPtr<FJsonObject>& SlotArgs)
{
    if (!Slot || !SlotArgs.IsValid())
    {
        return;
    }
    for (TFieldIterator<FProperty> PropIt(Slot->GetClass()); PropIt; ++PropIt)
    {
        FProperty* Prop = *PropIt;
        FString Value;
        if (SlotArgs->TryGetStringField(Prop->GetName(), Value))
        {
            void* PropPtr = Prop->ContainerPtrToValuePtr<void>(Slot);
            Prop->ImportText_Direct(*Value, PropPtr, nullptr, PPF_None);
        }
    }
}

FString FLoomleWidgetAdapter::ComputeRevision(UWidgetBlueprint* WBP)
{
    if (!WBP || !WBP->WidgetTree)
    {
        return TEXT("");
    }
    // Build a canonical string from the tree and hash it.
    TSharedPtr<FJsonObject> TreeObj = SerializeWidget(WBP->WidgetTree->RootWidget, /*bIncludeSlotProperties=*/true);
    FString TreeStr = TreeObj.IsValid() ? JsonObjectToString(TreeObj) : TEXT("null");
    return FMD5::HashAnsiString(*TreeStr);
}

void FLoomleWidgetAdapter::MarkModified(UWidgetBlueprint* WBP)
{
    WBP->MarkPackageDirty();
    FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(WBP);
}

// ---------------------------------------------------------------------------
// Internal helpers — property description
// ---------------------------------------------------------------------------

namespace
{

// Return true if this property should be included in widget.describe output.
bool IsDescribableProperty(FProperty* Prop)
{
    if (!Prop)
    {
        return false;
    }
    // Must be editor-visible or Blueprint-accessible
    if (!Prop->HasAnyPropertyFlags(CPF_Edit | CPF_BlueprintVisible))
    {
        return false;
    }
    // Exclude transient and private-access properties
    if (Prop->HasAnyPropertyFlags(CPF_Transient | CPF_NativeAccessSpecifierPrivate))
    {
        return false;
    }
    // Must have a non-empty Category metadata — filters out internal C++ properties
    const FString Category = Prop->GetMetaData(TEXT("Category"));
    return !Category.IsEmpty();
}

// Resolve a UClass* from either a short name ("TextBlock") or a full path.
// Returns nullptr and sets OutError on failure.
UClass* ResolveWidgetClassByName(const FString& ClassInput, FString& OutError)
{
    if (ClassInput.IsEmpty())
    {
        OutError = TEXT("WIDGET_CLASS_NOT_FOUND: widgetClass is empty.");
        return nullptr;
    }

    // If it looks like an asset path (contains '/') try LoadClass directly first.
    if (ClassInput.Contains(TEXT("/")))
    {
        UClass* Cls = LoadClass<UWidget>(nullptr, *ClassInput);
        if (Cls)
        {
            return Cls;
        }
        OutError = FString::Printf(
            TEXT("WIDGET_CLASS_NOT_FOUND: Could not load widget class '%s'."), *ClassInput);
        return nullptr;
    }

    // Short name — search registered UClass objects.
    // We iterate UClass objects looking for a short name match.
    UClass* Found = nullptr;
    for (TObjectIterator<UClass> It; It; ++It)
    {
        UClass* Cls = *It;
        if (!Cls->IsChildOf(UWidget::StaticClass()))
        {
            continue;
        }
        if (Cls->GetName().Equals(ClassInput, ESearchCase::IgnoreCase))
        {
            Found = Cls;
            break;
        }
    }
    if (Found)
    {
        return Found;
    }

    // Fallback: try common UMG script path
    const FString FullPath = FString::Printf(TEXT("/Script/UMG.%s"), *ClassInput);
    UClass* Cls = LoadClass<UWidget>(nullptr, *FullPath);
    if (Cls)
    {
        return Cls;
    }

    OutError = FString::Printf(
        TEXT("WIDGET_CLASS_NOT_FOUND: Widget class '%s' not found. Provide a full asset path or a registered short name."),
        *ClassInput);
    return nullptr;
}

// Serialize one FProperty into a JSON descriptor object.
TSharedPtr<FJsonObject> SerializePropertyDescriptor(FProperty* Prop)
{
    TSharedPtr<FJsonObject> PObj = MakeShared<FJsonObject>();
    PObj->SetStringField(TEXT("name"), Prop->GetName());
    PObj->SetStringField(TEXT("type"), Prop->GetCPPType());
    PObj->SetStringField(TEXT("category"), Prop->GetMetaData(TEXT("Category")));
    const bool bWritable = !Prop->HasAnyPropertyFlags(CPF_BlueprintReadOnly);
    PObj->SetBoolField(TEXT("writable"), bWritable);
    return PObj;
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

bool FLoomleWidgetAdapter::DescribeWidgetClass(
    const FString& ClassInput,
    const FString& AssetPath,
    const FString& WidgetName,
    FString& OutJson,
    FString& OutError)
{
    // --- Resolve the UClass ---
    FString ResolvedClassInput = ClassInput;

    // If caller provided asset+widget but no explicit class, derive the class from the live instance.
    UWidget* LiveInstance = nullptr;
    if (ResolvedClassInput.IsEmpty() && !AssetPath.IsEmpty() && !WidgetName.IsEmpty())
    {
        UWidgetBlueprint* WBP = LoadWidgetBlueprintAsset(AssetPath, OutError);
        if (!WBP)
        {
            return false;
        }
        LiveInstance = FindWidgetByName(WBP, WidgetName);
        if (!LiveInstance)
        {
            OutError = FString::Printf(
                TEXT("WIDGET_NOT_FOUND: Widget '%s' not found in '%s'."), *WidgetName, *AssetPath);
            return false;
        }
        ResolvedClassInput = LiveInstance->GetClass()->GetPathName();
    }

    UClass* WidgetClass = ResolveWidgetClassByName(ResolvedClassInput, OutError);
    if (!WidgetClass)
    {
        return false;
    }

    // If we have an asset+widget but didn't load the instance yet (class was given explicitly),
    // try to find the live instance now so we can attach currentValues.
    if (!LiveInstance && !AssetPath.IsEmpty() && !WidgetName.IsEmpty())
    {
        UWidgetBlueprint* WBP = LoadWidgetBlueprintAsset(AssetPath, OutError);
        if (!WBP)
        {
            return false;
        }
        LiveInstance = FindWidgetByName(WBP, WidgetName);
        if (!LiveInstance)
        {
            OutError = FString::Printf(
                TEXT("WIDGET_NOT_FOUND: Widget '%s' not found in '%s'."), *WidgetName, *AssetPath);
            return false;
        }
    }

    // --- Enumerate widget properties ---
    TArray<TSharedPtr<FJsonValue>> Properties;
    TSharedPtr<FJsonObject> CurrentValues = MakeShared<FJsonObject>();
    bool bHasCurrentValues = (LiveInstance != nullptr);

    for (TFieldIterator<FProperty> PropIt(WidgetClass); PropIt; ++PropIt)
    {
        FProperty* Prop = *PropIt;
        if (!IsDescribableProperty(Prop))
        {
            continue;
        }
        Properties.Add(MakeShared<FJsonValueObject>(SerializePropertyDescriptor(Prop)));

        if (bHasCurrentValues)
        {
            FString ValueStr;
            const void* PropPtr = Prop->ContainerPtrToValuePtr<void>(LiveInstance);
            Prop->ExportTextItem_Direct(ValueStr, PropPtr, nullptr, nullptr, PPF_None);
            CurrentValues->SetStringField(Prop->GetName(), ValueStr);
        }
    }

    // --- Enumerate slot properties (from the instance's current slot class, if available) ---
    TArray<TSharedPtr<FJsonValue>> SlotProperties;
    if (LiveInstance && LiveInstance->Slot)
    {
        UPanelSlot* Slot = LiveInstance->Slot;
        for (TFieldIterator<FProperty> PropIt(Slot->GetClass()); PropIt; ++PropIt)
        {
            FProperty* Prop = *PropIt;
            if (Prop->HasAnyPropertyFlags(CPF_Transient | CPF_NativeAccessSpecifierPrivate))
            {
                continue;
            }
            TSharedPtr<FJsonObject> SPObj = MakeShared<FJsonObject>();
            SPObj->SetStringField(TEXT("name"), Prop->GetName());
            SPObj->SetStringField(TEXT("type"), Prop->GetCPPType());
            const bool bWritable = !Prop->HasAnyPropertyFlags(CPF_BlueprintReadOnly);
            SPObj->SetBoolField(TEXT("writable"), bWritable);
            SlotProperties.Add(MakeShared<FJsonValueObject>(SPObj));
        }
    }

    // --- Assemble output ---
    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetStringField(TEXT("widgetClass"), WidgetClass->GetPathName());
    Result->SetArrayField(TEXT("properties"), Properties);
    Result->SetArrayField(TEXT("slotProperties"), SlotProperties);
    if (bHasCurrentValues)
    {
        Result->SetObjectField(TEXT("currentValues"), CurrentValues);
    }

    OutJson = JsonObjectToString(Result);
    return true;
}

bool FLoomleWidgetAdapter::QueryWidgetTree(
    const FString& AssetPath,
    bool bIncludeSlotProperties,
    FString& OutTreeJson,
    FString& OutRevision,
    FString& OutError)
{
    UWidgetBlueprint* WBP = LoadWidgetBlueprintAsset(AssetPath, OutError);
    if (!WBP)
    {
        return false;
    }

    TSharedPtr<FJsonObject> RootObj = SerializeWidget(WBP->WidgetTree->RootWidget, bIncludeSlotProperties);

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetStringField(TEXT("assetPath"), AssetPath);
    if (RootObj.IsValid())
    {
        Result->SetObjectField(TEXT("rootWidget"), RootObj);
    }
    else
    {
        Result->SetField(TEXT("rootWidget"), MakeShared<FJsonValueNull>());
    }

    OutRevision = ComputeRevision(WBP);
    OutTreeJson = JsonObjectToString(Result);
    return true;
}

bool FLoomleWidgetAdapter::AddWidget(
    UWidgetBlueprint* WBP,
    const FString& WidgetClassPath,
    const FString& Name,
    const FString& ParentName,
    const TSharedPtr<FJsonObject>& SlotArgs,
    FString& OutError)
{
    // Resolve widget class
    UClass* WidgetClass = LoadClass<UWidget>(nullptr, *WidgetClassPath);
    if (!WidgetClass)
    {
        OutError = FString::Printf(
            TEXT("WIDGET_CLASS_NOT_FOUND: Could not load widget class '%s'."), *WidgetClassPath);
        return false;
    }

    // Duplicate-name guard
    if (FindWidgetByName(WBP, Name))
    {
        OutError = FString::Printf(
            TEXT("INVALID_ARGUMENT: A widget named '%s' already exists in the WidgetTree."), *Name);
        return false;
    }

    // Construct the new widget inside the WidgetTree
    UWidget* NewWidget = WBP->WidgetTree->ConstructWidget<UWidget>(WidgetClass, FName(*Name));
    if (!NewWidget)
    {
        OutError = FString::Printf(
            TEXT("INTERNAL_ERROR: ConstructWidget failed for class '%s'."), *WidgetClassPath);
        return false;
    }

    // Resolve parent and attach
    if (ParentName.IsEmpty() || ParentName.Equals(TEXT("root"), ESearchCase::IgnoreCase))
    {
        // Set as root
        WBP->WidgetTree->RootWidget = NewWidget;
    }
    else
    {
        UWidget* ParentWidget = FindWidgetByName(WBP, ParentName);
        if (!ParentWidget)
        {
            OutError = FString::Printf(
                TEXT("WIDGET_NOT_FOUND: Parent widget '%s' not found."), *ParentName);
            return false;
        }
        UPanelWidget* Panel = Cast<UPanelWidget>(ParentWidget);
        if (!Panel)
        {
            OutError = FString::Printf(
                TEXT("WIDGET_PARENT_NOT_PANEL: Parent widget '%s' is not a UPanelWidget and cannot have children."),
                *ParentName);
            return false;
        }
        UPanelSlot* Slot = Panel->AddChild(NewWidget);
        ApplySlotProperties(Slot, SlotArgs);
    }

    MarkModified(WBP);
    return true;
}

bool FLoomleWidgetAdapter::RemoveWidget(
    UWidgetBlueprint* WBP,
    const FString& Name,
    FString& OutError)
{
    UWidget* Target = FindWidgetByName(WBP, Name);
    if (!Target)
    {
        OutError = FString::Printf(TEXT("WIDGET_NOT_FOUND: Widget '%s' not found."), *Name);
        return false;
    }

    if (Target == WBP->WidgetTree->RootWidget)
    {
        WBP->WidgetTree->RootWidget = nullptr;
    }
    else if (UPanelWidget* ParentPanel = Cast<UPanelWidget>(Target->GetParent()))
    {
        ParentPanel->RemoveChild(Target);
    }

    MarkModified(WBP);
    return true;
}

bool FLoomleWidgetAdapter::SetWidgetProperty(
    UWidgetBlueprint* WBP,
    const FString& TargetName,
    const FString& PropertyName,
    const FString& ValueJson,
    FString& OutError)
{
    UWidget* Target = FindWidgetByName(WBP, TargetName);
    if (!Target)
    {
        OutError = FString::Printf(TEXT("WIDGET_NOT_FOUND: Widget '%s' not found."), *TargetName);
        return false;
    }

    // Try widget-own property first, then fall back to slot property.
    FProperty* Prop = FindFProperty<FProperty>(Target->GetClass(), *PropertyName);
    UObject* PropOwner = Target;

    if (!Prop && Target->Slot)
    {
        Prop = FindFProperty<FProperty>(Target->Slot->GetClass(), *PropertyName);
        PropOwner = Target->Slot;
    }

    if (!Prop)
    {
        OutError = FString::Printf(
            TEXT("INVALID_ARGUMENT: Property '%s' not found on widget class '%s' or its slot."),
            *PropertyName, *Target->GetClass()->GetName());
        return false;
    }

    void* PropPtr = Prop->ContainerPtrToValuePtr<void>(PropOwner);
    const TCHAR* ImportResult = Prop->ImportText_Direct(*ValueJson, PropPtr, nullptr, PPF_None);
    if (!ImportResult)
    {
        OutError = FString::Printf(
            TEXT("INVALID_ARGUMENT: Could not import value '%s' for property '%s'."),
            *ValueJson, *PropertyName);
        return false;
    }

    MarkModified(WBP);
    return true;
}

bool FLoomleWidgetAdapter::ReparentWidget(
    UWidgetBlueprint* WBP,
    const FString& Name,
    const FString& NewParentName,
    const TSharedPtr<FJsonObject>& SlotArgs,
    FString& OutError)
{
    UWidget* Target = FindWidgetByName(WBP, Name);
    if (!Target)
    {
        OutError = FString::Printf(TEXT("WIDGET_NOT_FOUND: Widget '%s' not found."), *Name);
        return false;
    }

    UWidget* NewParent = FindWidgetByName(WBP, NewParentName);
    if (!NewParent)
    {
        OutError = FString::Printf(TEXT("WIDGET_NOT_FOUND: New parent widget '%s' not found."), *NewParentName);
        return false;
    }

    UPanelWidget* NewPanel = Cast<UPanelWidget>(NewParent);
    if (!NewPanel)
    {
        OutError = FString::Printf(
            TEXT("WIDGET_PARENT_NOT_PANEL: Widget '%s' is not a UPanelWidget and cannot have children."),
            *NewParentName);
        return false;
    }

    // Detach from current parent
    if (UPanelWidget* OldPanel = Cast<UPanelWidget>(Target->GetParent()))
    {
        OldPanel->RemoveChild(Target);
    }

    UPanelSlot* Slot = NewPanel->AddChild(Target);
    ApplySlotProperties(Slot, SlotArgs);

    MarkModified(WBP);
    return true;
}

bool FLoomleWidgetAdapter::CompileWidgetBlueprint(
    const FString& AssetPath,
    FString& OutDiagsJson,
    FString& OutError)
{
    UWidgetBlueprint* WBP = LoadWidgetBlueprintAsset(AssetPath, OutError);
    if (!WBP)
    {
        return false;
    }

    FCompilerResultsLog ResultsLog;
    ResultsLog.bSilentMode = true;
    FKismetEditorUtilities::CompileBlueprint(WBP, EBlueprintCompileOptions::None, &ResultsLog);

    // Collect compiler results
    TArray<TSharedPtr<FJsonValue>> Diags;
    for (const TSharedRef<FTokenizedMessage>& Msg : ResultsLog.Messages)
    {
        const EMessageSeverity::Type Severity = Msg->GetSeverity();
        if (Severity != EMessageSeverity::Error && Severity != EMessageSeverity::Warning)
        {
            continue;
        }
        TSharedPtr<FJsonObject> DiagObj = MakeShared<FJsonObject>();
        DiagObj->SetStringField(TEXT("severity"),
            Severity == EMessageSeverity::Error ? TEXT("error") : TEXT("warning"));
        DiagObj->SetStringField(TEXT("message"), Msg->ToText().ToString());
        Diags.Add(MakeShared<FJsonValueObject>(DiagObj));
    }

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetArrayField(TEXT("diagnostics"), Diags);
    OutDiagsJson = JsonObjectToString(Result);
    return true;
}
