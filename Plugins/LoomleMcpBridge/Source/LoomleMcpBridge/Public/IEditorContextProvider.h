#pragma once

#include "CoreMinimal.h"

class FJsonObject;

class IEditorContextProvider
{
public:
    virtual ~IEditorContextProvider() = default;

    virtual FName GetProviderId() const = 0;
    virtual int32 GetPriority() const = 0;
    virtual bool IsAvailable() const = 0;
    virtual bool BuildContextSnapshot(TSharedPtr<FJsonObject>& OutContext) const = 0;
    virtual bool BuildSelectionSnapshot(TSharedPtr<FJsonObject>& OutSelection) const = 0;
};

