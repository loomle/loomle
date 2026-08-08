// Copyright 2026 Loomle contributors.

#include "SalWidgetBlueprintDuplicationGCTestTypes.h"

TWeakObjectPtr<USalWidgetBlueprintDuplicationGCTest>
    USalWidgetBlueprintDuplicationGCTest::ArmedSource;
bool USalWidgetBlueprintDuplicationGCTest::bSourceReleased = false;

void USalWidgetBlueprintDuplicationGCTest::ArmSourceRelease(
    USalWidgetBlueprintDuplicationGCTest* Source)
{
    ArmedSource = Source;
    bSourceReleased = false;
}

void USalWidgetBlueprintDuplicationGCTest::DisarmSourceRelease()
{
    ArmedSource.Reset();
}

bool USalWidgetBlueprintDuplicationGCTest::WasSourceReleased()
{
    return bSourceReleased;
}

void USalWidgetBlueprintDuplicationGCTest::PostDuplicate(
    const bool bDuplicateForPIE)
{
    USalWidgetBlueprintDuplicationGCTest* Source = ArmedSource.Get();
    if (Source != nullptr
        && Source != this
        && Source->CollectionProbe != nullptr)
    {
        Source->CollectionProbe = nullptr;
        bSourceReleased = true;
    }
    Super::PostDuplicate(bDuplicateForPIE);
}
