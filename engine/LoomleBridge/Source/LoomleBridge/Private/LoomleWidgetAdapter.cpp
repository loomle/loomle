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
// Public API
// ---------------------------------------------------------------------------

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

    FProperty* Prop = FindFProperty<FProperty>(Target->GetClass(), *PropertyName);
    if (!Prop)
    {
        OutError = FString::Printf(
            TEXT("INVALID_ARGUMENT: Property '%s' not found on widget class '%s'."),
            *PropertyName, *Target->GetClass()->GetName());
        return false;
    }

    void* PropPtr = Prop->ContainerPtrToValuePtr<void>(Target);
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
