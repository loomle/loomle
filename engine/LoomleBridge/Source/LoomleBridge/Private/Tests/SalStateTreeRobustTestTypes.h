// Copyright 2026 Loomle contributors.

#pragma once

#include "Blueprint/StateTreeTaskBlueprintBase.h"
#include "StateTreeSchema.h"

#include "SalStateTreeRobustTestTypes.generated.h"

/**
 * Native UObject-backed task used to exercise the same wrapper and InstanceObject
 * path that UE uses for Blueprint-authored StateTree node Classes.
 */
UCLASS()
class USalStateTreeRobustTask final
    : public UStateTreeTaskBlueprintBase
{
    GENERATED_BODY()

public:
    UPROPERTY(EditAnywhere, Category = "Input", meta = (Optional))
    int32 InputValue = 0;

    UPROPERTY(EditAnywhere, Category = "Output")
    int32 OutputValue = 0;
};

/** Restrictive Schema that admits exactly the class-backed Automation fixture. */
UCLASS()
class USalStateTreeRobustSchema final
    : public UStateTreeSchema
{
    GENERATED_BODY()

public:
    virtual bool IsStructAllowed(
        const UScriptStruct*) const override
    {
        return false;
    }

    virtual bool IsClassAllowed(
        const UClass* InClass) const override
    {
        return InClass == USalStateTreeRobustTask::StaticClass();
    }

    virtual bool AllowMultipleTasks() const override
    {
        return true;
    }
};
