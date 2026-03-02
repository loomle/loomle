#pragma once

#include "CoreMinimal.h"

class FJsonObject;
class IEditorContextProvider;

class FEditorContextProviderRegistry
{
public:
    void RegisterProvider(TSharedRef<IEditorContextProvider> Provider);
    void ClearProviders();

    bool BuildActiveContextSnapshot(TSharedPtr<FJsonObject>& OutContext, FName& OutProviderId) const;
    bool BuildActiveSelectionSnapshot(TSharedPtr<FJsonObject>& OutSelection, FName& OutProviderId) const;

private:
    TArray<TSharedRef<IEditorContextProvider>> Providers;
};

