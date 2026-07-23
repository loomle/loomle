// Copyright 2026 Loomle contributors.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"

#include "SalClassSparseTestTypes.generated.h"

USTRUCT()
struct FSalClassSparseTestData
{
    GENERATED_BODY()

    UPROPERTY(EditDefaultsOnly, Category = "Loomle Tests")
    int32 SparseValue = 17;
};

UCLASS(Blueprintable, SparseClassDataTypes = SalClassSparseTestData)
class USalClassSparseTestObject : public UObject
{
    GENERATED_BODY()
};
