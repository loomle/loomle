// Copyright 2026 Loomle contributors.

#pragma once

#include "StateTreeSchema.h"
#include "StateTreeTypes.h"

#include "SalStateTreePaletteTestSchema.generated.h"

/** Palette fixture that can switch between mixed State/Linked and Linked-only modes. */
UCLASS()
class USalStateTreePaletteTestSchema final : public UStateTreeSchema
{
    GENERATED_BODY()

public:
    virtual bool IsStateTypeAllowed(const EStateTreeStateType InType) const override
    {
        return (bAllowOrdinaryState && InType == EStateTreeStateType::State)
            || (bAllowLinkedState && InType == EStateTreeStateType::Linked)
            || (bAllowLinkedAssetState && InType == EStateTreeStateType::LinkedAsset);
    }

    bool bAllowOrdinaryState = false;
    bool bAllowLinkedState = true;
    bool bAllowLinkedAssetState = false;
};
