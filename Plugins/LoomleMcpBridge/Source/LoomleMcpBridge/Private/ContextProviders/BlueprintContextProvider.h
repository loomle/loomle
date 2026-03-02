#pragma once

#include "IEditorContextProvider.h"

class FBlueprintContextProvider : public IEditorContextProvider
{
public:
    virtual FName GetProviderId() const override;
    virtual int32 GetPriority() const override;
    virtual bool IsAvailable() const override;
    virtual bool BuildContextSnapshot(TSharedPtr<FJsonObject>& OutContext) const override;
    virtual bool BuildSelectionSnapshot(TSharedPtr<FJsonObject>& OutSelection) const override;
};

