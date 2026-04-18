#pragma once

#include "CoreMinimal.h"

class FJsonObject;
class UWidget;
class UWidgetBlueprint;
class UPanelSlot;

class LOOMLEBRIDGE_API FLoomleWidgetAdapter
{
public:
    // Read the full WidgetTree of a WidgetBlueprint asset as a JSON snapshot.
    // OutRevision is a content-hash token suitable for expectedRevision in mutate.
    static bool QueryWidgetTree(
        const FString& AssetPath,
        bool bIncludeSlotProperties,
        FString& OutTreeJson,
        FString& OutRevision,
        FString& OutError);

    // Structural mutate ops — callers load the UWidgetBlueprint once and pass it in.
    // Each method returns true on success. On failure OutError is set.
    static bool AddWidget(
        UWidgetBlueprint* WBP,
        const FString& WidgetClassPath,
        const FString& Name,
        const FString& ParentName,
        const TSharedPtr<FJsonObject>& SlotArgs,
        FString& OutError);

    static bool RemoveWidget(
        UWidgetBlueprint* WBP,
        const FString& Name,
        FString& OutError);

    static bool SetWidgetProperty(
        UWidgetBlueprint* WBP,
        const FString& TargetName,
        const FString& PropertyName,
        const FString& ValueJson,
        FString& OutError);

    static bool ReparentWidget(
        UWidgetBlueprint* WBP,
        const FString& Name,
        const FString& NewParentName,
        const TSharedPtr<FJsonObject>& SlotArgs,
        FString& OutError);

    // Trigger a Blueprint compile and return diagnostics JSON.
    static bool CompileWidgetBlueprint(
        const FString& AssetPath,
        FString& OutDiagsJson,
        FString& OutError);

    // Public wrapper so handler code in .inl files can call ComputeRevision
    // without reaching into private implementation details.
    static FString ComputeRevision_Public(UWidgetBlueprint* WBP) { return ComputeRevision(WBP); }

private:
    // Walk UWidget* recursively into a JsonObject tree.
    static TSharedPtr<FJsonObject> SerializeWidget(
        UWidget* Widget,
        bool bIncludeSlotProperties);

    // Find a widget by designer name inside the WidgetTree. Returns nullptr if not found.
    static UWidget* FindWidgetByName(
        UWidgetBlueprint* WBP,
        const FString& Name);

    // Apply slot properties from a JSON object onto a UPanelSlot via reflection.
    static void ApplySlotProperties(
        UPanelSlot* Slot,
        const TSharedPtr<FJsonObject>& SlotArgs);

    // Compute a lightweight revision token from the current WidgetTree state.
    static FString ComputeRevision(UWidgetBlueprint* WBP);

    // Mark the package dirty and notify Blueprint editors.
    static void MarkModified(UWidgetBlueprint* WBP);
};
