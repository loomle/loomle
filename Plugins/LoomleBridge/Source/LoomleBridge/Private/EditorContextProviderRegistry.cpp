#include "EditorContextProviderRegistry.h"

#include "Dom/JsonObject.h"
#include "IEditorContextProvider.h"

void FEditorContextProviderRegistry::RegisterProvider(TSharedRef<IEditorContextProvider> Provider)
{
    Providers.Add(Provider);
    Providers.Sort([](const TSharedRef<IEditorContextProvider>& A, const TSharedRef<IEditorContextProvider>& B)
    {
        return A->GetPriority() > B->GetPriority();
    });
}

void FEditorContextProviderRegistry::ClearProviders()
{
    Providers.Empty();
}

bool FEditorContextProviderRegistry::BuildActiveContextSnapshot(TSharedPtr<FJsonObject>& OutContext, FName& OutProviderId) const
{
    OutContext.Reset();
    OutProviderId = NAME_None;

    for (const TSharedRef<IEditorContextProvider>& Provider : Providers)
    {
        if (!Provider->IsAvailable())
        {
            continue;
        }

        TSharedPtr<FJsonObject> Snapshot;
        if (!Provider->BuildContextSnapshot(Snapshot) || !Snapshot.IsValid())
        {
            continue;
        }

        OutContext = Snapshot;
        OutProviderId = Provider->GetProviderId();
        return true;
    }

    return false;
}

bool FEditorContextProviderRegistry::BuildActiveSelectionSnapshot(TSharedPtr<FJsonObject>& OutSelection, FName& OutProviderId) const
{
    OutSelection.Reset();
    OutProviderId = NAME_None;

    for (const TSharedRef<IEditorContextProvider>& Provider : Providers)
    {
        if (!Provider->IsAvailable())
        {
            continue;
        }

        TSharedPtr<FJsonObject> Snapshot;
        if (!Provider->BuildSelectionSnapshot(Snapshot) || !Snapshot.IsValid())
        {
            continue;
        }

        OutSelection = Snapshot;
        OutProviderId = Provider->GetProviderId();
        return true;
    }

    return false;
}

