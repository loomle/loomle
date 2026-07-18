// Copyright 2026 Loomle contributors.

#pragma once

#include "CoreMinimal.h"

class FJsonObject;
class IAssetEditorInstance;
class IDetailsView;
class SDockTab;
class SWidget;
class FWidgetPath;

namespace Loomle::EditorContext
{
/**
 * Structural input captured from one real Slate interaction.
 *
 * Providers must use the focus path, DockTab identity, editor identity, or
 * native editor state. Visible labels and window titles are intentionally not
 * part of this contract.
 */
struct FRecognitionInput
{
    const FWidgetPath* FocusPath = nullptr;
    TSharedPtr<SDockTab> ActiveTab;
    IAssetEditorInstance* AssetEditor = nullptr;
    TSharedPtr<IDetailsView> DetailsView;
    TArray<FName> Tags;
    TArray<FName> WidgetTypes;
    FName TabId;
    FName EditorName;
    bool bModal = false;
    bool bRecoveredHostFromWindow = false;

    bool HasTag(FName Tag) const;
    bool HasWidgetType(FName Type) const;
};

/**
 * The tracker retains only weak or externally owned native references and
 * surface identity. Object state and selections are always read again when
 * BuildResult is called. A raw AssetEditor pointer is never dereferenced until
 * its current DockTab association and EditorName have been revalidated.
 */
struct FInteractionRecord
{
    FName Provider;
    FName Surface;
    FName TabId;
    FName EditorName;
    FName LeafWidgetType;
    TWeakPtr<SDockTab> Tab;
    TWeakPtr<IDetailsView> DetailsView;
    TWeakPtr<SWidget> SurfaceWidget;
    IAssetEditorInstance* AssetEditor = nullptr;
    bool bHadTab = false;
    bool bHadSurfaceWidget = false;
    bool bHadFocusPath = false;
    bool bRecoveredHostFromWindow = false;
};

/** Bridge-internal extraction unit ordered by Priority (larger values win). */
class IEditorContextProvider
{
public:
    virtual ~IEditorContextProvider() = default;

    virtual FName Name() const = 0;
    virtual int32 Priority() const = 0;
    virtual bool Recognize(const FRecognitionInput& Input, FInteractionRecord& OutRecord) const = 0;
    virtual TSharedPtr<FJsonObject> Build(const FInteractionRecord& Record) const = 0;
};

/**
 * Process-wide Editor Context tracker and Provider registry.
 *
 * Startup and Shutdown are idempotent and must run on the Editor game thread.
 * BuildResult returns normalized SAL ObjectResult and introduces no Context
 * constructor or other SAL grammar.
 */
class FEditorContextService
{
public:
    static FEditorContextService& Get();

    void Startup();
    void Shutdown();
    bool IsStarted() const;

    void RegisterProvider(TSharedRef<IEditorContextProvider> Provider);
    void UnregisterProvider(FName ProviderName);

    TSharedPtr<FJsonObject> BuildResult();

private:
    FEditorContextService();
    ~FEditorContextService();
    FEditorContextService(const FEditorContextService&) = delete;
    FEditorContextService& operator=(const FEditorContextService&) = delete;

    class FImpl;
    TUniquePtr<FImpl> Impl;
};
}
