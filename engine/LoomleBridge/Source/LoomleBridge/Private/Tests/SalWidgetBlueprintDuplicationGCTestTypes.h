// Copyright 2026 Loomle contributors.

#pragma once

#include "WidgetBlueprint.h"

#include "SalWidgetBlueprintDuplicationGCTestTypes.generated.h"

UCLASS()
class USalWidgetBlueprintDuplicationGCProbe final
    : public UObject
{
    GENERATED_BODY()
};

/**
 * Widget Blueprint fixture that can release one source-owned object immediately
 * before native duplicate compilation. This deterministically exercises UE's
 * non-auto-removing duplication annotation map across nested compile-time GC.
 */
UCLASS()
class USalWidgetBlueprintDuplicationGCTest final
    : public UWidgetBlueprint
{
    GENERATED_BODY()

public:
    UPROPERTY()
    TObjectPtr<USalWidgetBlueprintDuplicationGCProbe>
        CollectionProbe;

    static void ArmSourceRelease(
        USalWidgetBlueprintDuplicationGCTest* Source);
    static void DisarmSourceRelease();
    static bool WasSourceReleased();

    virtual void PostDuplicate(bool bDuplicateForPIE) override;

private:
    static TWeakObjectPtr<USalWidgetBlueprintDuplicationGCTest>
        ArmedSource;
    static bool bSourceReleased;
};
