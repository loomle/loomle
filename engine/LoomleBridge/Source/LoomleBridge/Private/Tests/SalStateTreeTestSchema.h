// Copyright 2026 Loomle contributors.

#pragma once

#include "Conditions/StateTreeCommonConditions.h"
#include "StateTreeExecutionTypes.h"
#include "StateTreeConditionBase.h"
#include "StateTreePropertyFunctionBase.h"
#include "StateTreePropertyBindings.h"
#include "StateTreeSchema.h"
#include "StateTreeTaskBase.h"
#include "StateTreeTypes.h"
#include "StructUtils/InstancedStruct.h"

#include "SalStateTreeTestSchema.generated.h"

/** Reflected authored surfaces used to exercise native StateTree Binding paths. */
USTRUCT()
struct FSalStateTreeBindingTaskInstanceData
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, Category = "Input", meta = (Optional))
    int32 InputValue = 0;

    UPROPERTY(EditAnywhere, Category = "Input", meta = (Optional))
    int32 SecondaryInputValue = 0;

    UPROPERTY(EditAnywhere, Category = "Input", meta = (Optional))
    float FloatInput = 0.0f;

    UPROPERTY(EditAnywhere, Category = "Input", meta = (Optional))
    TArray<int32> InputValues;

    UPROPERTY(EditAnywhere, Category = "Output")
    int32 OutputValue = 0;

    UPROPERTY(EditAnywhere, Category = "Output")
    TArray<int32> OutputValues;

    UPROPERTY(EditAnywhere, Category = "Input", meta = (Optional))
    FInstancedStruct InstancedInput;
};

/** Distinct native instance types whose identical member names intentionally collapse in SAL text. */
USTRUCT()
struct FSalStateTreeInstancedInputA
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, Category = "Input")
    int32 SharedValue = 0;
};

USTRUCT()
struct FSalStateTreeInstancedInputB
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, Category = "Input")
    int32 SharedValue = 0;
};

USTRUCT()
struct FSalStateTreeBindingTask : public FStateTreeTaskBase
{
    GENERATED_BODY()

    using FInstanceDataType = FSalStateTreeBindingTaskInstanceData;

    virtual const UStruct* GetInstanceDataType() const override
    {
        return FInstanceDataType::StaticStruct();
    }
};

USTRUCT()
struct FSalStateTreePostEditCascadeTaskInstanceData
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, Category = "Input", meta = (Optional))
    int32 TriggerValue = 0;

    UPROPERTY(EditAnywhere, Category = "Input", meta = (Optional))
    int32 SynchronizedValue = 0;
};

/** Controllable native PostEdit cascade used to prove Patch effect reporting. */
USTRUCT()
struct FSalStateTreePostEditCascadeTask : public FStateTreeTaskBase
{
    GENERATED_BODY()

    using FInstanceDataType = FSalStateTreePostEditCascadeTaskInstanceData;

    virtual const UStruct* GetInstanceDataType() const override
    {
        return FInstanceDataType::StaticStruct();
    }

#if WITH_EDITOR
    virtual void PostEditInstanceDataChangeChainProperty(
        const FPropertyChangedChainEvent& PropertyChangedEvent,
        FStateTreeDataView InstanceDataView) override
    {
        if (PropertyChangedEvent.Property != nullptr
            && PropertyChangedEvent.Property->GetFName()
                == GET_MEMBER_NAME_CHECKED(FInstanceDataType, TriggerValue))
        {
            FInstanceDataType& Instance = InstanceDataView.GetMutable<FInstanceDataType>();
            Instance.SynchronizedValue = Instance.TriggerValue * 2;
        }
    }
#endif
};

USTRUCT()
struct FSalStateTreeBindingCondition : public FStateTreeConditionBase
{
    GENERATED_BODY()

    using FInstanceDataType = FSalStateTreeBindingTaskInstanceData;

    virtual const UStruct* GetInstanceDataType() const override
    {
        return FInstanceDataType::StaticStruct();
    }
};

USTRUCT()
struct FSalStateTreeOptionalEnumConditionInstanceData
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, Category = "Input", meta = (AllowAnyBinding, Optional))
    FStateTreeAnyEnum Left;

    UPROPERTY(EditAnywhere, Category = "Parameter")
    FStateTreeAnyEnum Right;
};

/**
 * Mirrors UE's native Enum Compare binding cascade while allowing an unbound
 * baseline, so the test exercises bind and unbind without compiling invalid
 * fixture data before either operation.
 */
USTRUCT()
struct FSalStateTreeOptionalEnumCondition : public FStateTreeConditionBase
{
    GENERATED_BODY()

    using FInstanceDataType = FSalStateTreeOptionalEnumConditionInstanceData;

    virtual const UStruct* GetInstanceDataType() const override
    {
        return FInstanceDataType::StaticStruct();
    }

#if WITH_EDITOR
    virtual void OnBindingChanged(
        const FGuid& ID,
        FStateTreeDataView InstanceData,
        const FPropertyBindingPath& SourcePath,
        const FPropertyBindingPath& TargetPath,
        const IStateTreeBindingLookup& BindingLookup) override
    {
        if (!TargetPath.GetStructID().IsValid()
            || TargetPath.IsPathEmpty()
            || TargetPath.GetSegments().Last().GetName()
                != GET_MEMBER_NAME_CHECKED(FInstanceDataType, Left))
        {
            return;
        }

        FInstanceDataType& Instance = InstanceData.GetMutable<FInstanceDataType>();
        UEnum* NewEnum = nullptr;
        if (const FProperty* Leaf = BindingLookup.GetPropertyPathLeafProperty(SourcePath))
        {
            if (const FByteProperty* ByteProperty = CastField<FByteProperty>(Leaf))
            {
                NewEnum = ByteProperty->GetIntPropertyEnum();
            }
            else if (const FEnumProperty* EnumProperty = CastField<FEnumProperty>(Leaf))
            {
                NewEnum = EnumProperty->GetEnum();
            }
        }
        Instance.Left.Initialize(NewEnum);
        if (Instance.Right.Enum != Instance.Left.Enum)
        {
            Instance.Right.Initialize(Instance.Left.Enum);
        }
    }
#endif
};

USTRUCT()
struct FSalStateTreeIntPropertyFunctionInstanceData
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, Category = "Input")
    int32 Operand = 0;

    UPROPERTY(EditAnywhere, Category = "Output")
    int32 IntResult = 0;
};

USTRUCT(meta = (DisplayName = "SAL Integer Property Function"))
struct FSalStateTreeIntPropertyFunction : public FStateTreePropertyFunctionBase
{
    GENERATED_BODY()

    using FInstanceDataType = FSalStateTreeIntPropertyFunctionInstanceData;

    virtual const UStruct* GetInstanceDataType() const override
    {
        return FInstanceDataType::StaticStruct();
    }
};

USTRUCT()
struct FSalStateTreeFloatPropertyFunctionInstanceData
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, Category = "Output")
    float FloatResult = 0.0f;
};

USTRUCT(meta = (DisplayName = "SAL Float Property Function"))
struct FSalStateTreeFloatPropertyFunction : public FStateTreePropertyFunctionBase
{
    GENERATED_BODY()

    using FInstanceDataType = FSalStateTreeFloatPropertyFunctionInstanceData;

    virtual const UStruct* GetInstanceDataType() const override
    {
        return FInstanceDataType::StaticStruct();
    }
};

USTRUCT()
struct FSalStateTreeContextTaskInstanceData
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, Category = "Context")
    TObjectPtr<UObject> ContextObject = nullptr;
};

USTRUCT()
struct FSalStateTreeContextTask : public FStateTreeTaskBase
{
    GENERATED_BODY()

    using FInstanceDataType = FSalStateTreeContextTaskInstanceData;

    virtual const UStruct* GetInstanceDataType() const override
    {
        return FInstanceDataType::StaticStruct();
    }
};

/** Node-template Context property must be ignored when UE does not expose the Node struct for Binding. */
USTRUCT()
struct FSalStateTreeIneligibleNodeContextTask : public FStateTreeTaskBase
{
    GENERATED_BODY()

    using FInstanceDataType = FSalStateTreeContextTaskInstanceData;

    UPROPERTY(EditAnywhere, Category = "Context")
    TObjectPtr<UObject> NodeContextObject = nullptr;

    virtual const UStruct* GetInstanceDataType() const override
    {
        return FInstanceDataType::StaticStruct();
    }
};

USTRUCT()
struct FSalStateTreeBindingEventPayload
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, Category = "Payload")
    int32 Value = 0;
};

/** Mutable schema fixture used only by the StateTree interface Automation tests. */
UCLASS()
class USalStateTreeTestSchema final : public UStateTreeSchema
{
    GENERATED_BODY()

public:
    virtual bool IsStructAllowed(const UScriptStruct* InScriptStruct) const override
    {
        return (bAllowBindingTask
                && (InScriptStruct == FSalStateTreeBindingTask::StaticStruct()
                    || InScriptStruct == FSalStateTreePostEditCascadeTask::StaticStruct()))
            || (bAllowCommonEnumCondition
                && (InScriptStruct == FStateTreeCompareEnumCondition::StaticStruct()
                    || InScriptStruct == FSalStateTreeOptionalEnumCondition::StaticStruct()))
            || (bAllowPropertyFunctions
                && (InScriptStruct == FSalStateTreeIntPropertyFunction::StaticStruct()
                    || InScriptStruct == FSalStateTreeFloatPropertyFunction::StaticStruct()));
    }

    virtual bool IsClassAllowed(const UClass*) const override
    {
        return false;
    }

    virtual bool IsScheduledTickAllowed() const override
    {
        return bAllowScheduledTick;
    }

    virtual bool AllowEvaluators() const override
    {
        return bAllowEvaluators;
    }

    virtual bool AllowEnterConditions() const override
    {
        return bAllowEnterConditions;
    }

    virtual bool AllowUtilityConsiderations() const override
    {
        return bAllowUtilityConsiderations;
    }

    virtual bool AllowMultipleTasks() const override
    {
        return bAllowMultipleTasks;
    }

    virtual bool IsStateSelectionAllowed(
        const EStateTreeStateSelectionBehavior InBehavior) const override
    {
        return !bRestrictStateSelection
            || InBehavior == EStateTreeStateSelectionBehavior::TryEnterState;
    }

    virtual bool IsStateTypeAllowed(const EStateTreeStateType InType) const override
    {
        return !bRestrictStateTypes || InType == EStateTreeStateType::State;
    }

    virtual TConstArrayView<FStateTreeExternalDataDesc> GetContextDataDescs() const override
    {
        return ContextData;
    }

#if WITH_EDITOR
    virtual bool AllowTasksCompletion() const override
    {
        return bAllowTaskCompletionEdits;
    }
#endif

    /** Opt-in used by Palette tests; the default preserves the restrictive native base Schema. */
    UPROPERTY()
    bool bAllowBindingTask = false;

    /** Opt-in for the native enum Condition used to exercise OnBindingChanged cascades. */
    UPROPERTY()
    bool bAllowCommonEnumCondition = false;

    /** Opt-in for destination-bound Property Function Palette compatibility tests. */
    UPROPERTY()
    bool bAllowPropertyFunctions = false;

    UPROPERTY()
    bool bAllowTaskCompletionEdits = true;

    UPROPERTY()
    bool bAllowScheduledTick = false;

    UPROPERTY()
    bool bAllowEvaluators = true;

    UPROPERTY()
    bool bAllowEnterConditions = true;

    UPROPERTY()
    bool bAllowUtilityConsiderations = true;

    UPROPERTY()
    bool bAllowMultipleTasks = true;

    UPROPERTY()
    bool bRestrictStateTypes = false;

    UPROPERTY()
    bool bRestrictStateSelection = false;

    UPROPERTY()
    TArray<FStateTreeExternalDataDesc> ContextData;
};
