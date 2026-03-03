#include "ContextProviders/MaterialContextProvider.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Editor.h"
#include "Engine/Selection.h"
#include "Framework/Application/SlateApplication.h"
#include "IMaterialEditor.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpression.h"
#include "MaterialGraph/MaterialGraphNode.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "Widgets/SWindow.h"

namespace
{
FString GetActiveWindowTitle()
{
    if (!FSlateApplication::IsInitialized())
    {
        return FString();
    }

    TSharedPtr<SWindow> ActiveWindow = FSlateApplication::Get().GetActiveTopLevelWindow();
    if (!ActiveWindow.IsValid())
    {
        const TSharedPtr<SWidget> FocusedWidget = FSlateApplication::Get().GetKeyboardFocusedWidget();
        if (FocusedWidget.IsValid())
        {
            ActiveWindow = FSlateApplication::Get().FindWidgetWindow(FocusedWidget.ToSharedRef());
        }
    }

    return ActiveWindow.IsValid() ? ActiveWindow->GetTitle().ToString() : FString();
}

UMaterial* FindEditedMaterial()
{
    if (!GEditor)
    {
        return nullptr;
    }

    UAssetEditorSubsystem* AssetEditorSubsystem = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>();
    if (!AssetEditorSubsystem)
    {
        return nullptr;
    }

    const FString ActiveWindowTitle = GetActiveWindowTitle();
    UMaterial* FallbackMaterial = nullptr;

    const TArray<UObject*> EditedAssets = AssetEditorSubsystem->GetAllEditedAssets();
    for (UObject* Asset : EditedAssets)
    {
        if (UMaterial* Material = Cast<UMaterial>(Asset))
        {
            if (!FallbackMaterial)
            {
                FallbackMaterial = Material;
            }

            if (!ActiveWindowTitle.IsEmpty()
                && ActiveWindowTitle.Contains(Material->GetName(), ESearchCase::IgnoreCase))
            {
                return Material;
            }
        }
    }

    return ActiveWindowTitle.IsEmpty() ? FallbackMaterial : nullptr;
}

bool CollectSelectedMaterialExpressions(TArray<UMaterialExpression*>& OutExpressions, UMaterial*& OutMaterial)
{
    OutExpressions.Reset();
    OutMaterial = nullptr;

    if (!GEditor)
    {
        return false;
    }

    UMaterial* EditedMaterial = FindEditedMaterial();
    if (!EditedMaterial)
    {
        return false;
    }

    UAssetEditorSubsystem* AssetEditorSubsystem = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>();
    if (!AssetEditorSubsystem)
    {
        return false;
    }

    IAssetEditorInstance* AssetEditorInstance = AssetEditorSubsystem->FindEditorForAsset(EditedMaterial, false);
    if (!AssetEditorInstance)
    {
        return false;
    }

    IMaterialEditor* MaterialEditor = static_cast<IMaterialEditor*>(AssetEditorInstance);
    if (!MaterialEditor)
    {
        return false;
    }

    const TSet<UObject*> SelectedNodes = MaterialEditor->GetSelectedNodes();
    for (UObject* SelectedObject : SelectedNodes)
    {
        UMaterialExpression* Expression = Cast<UMaterialExpression>(SelectedObject);
        if (!Expression)
        {
            if (UMaterialGraphNode* GraphNode = Cast<UMaterialGraphNode>(SelectedObject))
            {
                Expression = GraphNode->MaterialExpression;
            }
        }

        if (Expression)
        {
            OutExpressions.Add(Expression);
        }
    }

    OutMaterial = EditedMaterial;
    return OutExpressions.Num() > 0 && OutMaterial != nullptr;
}
}

FName FMaterialContextProvider::GetProviderId() const
{
    return TEXT("material");
}

int32 FMaterialContextProvider::GetPriority() const
{
    return 90;
}

bool FMaterialContextProvider::IsAvailable() const
{
    return FindEditedMaterial() != nullptr;
}

bool FMaterialContextProvider::BuildContextSnapshot(TSharedPtr<FJsonObject>& OutContext) const
{
    OutContext.Reset();
    UMaterial* Material = FindEditedMaterial();
    if (!Material)
    {
        return false;
    }

    OutContext = MakeShared<FJsonObject>();
    OutContext->SetBoolField(TEXT("isError"), false);
    OutContext->SetStringField(TEXT("editorType"), TEXT("material"));
    OutContext->SetStringField(TEXT("provider"), GetProviderId().ToString());
    OutContext->SetStringField(TEXT("assetName"), Material->GetName());
    OutContext->SetStringField(TEXT("assetPath"), Material->GetPathName());
    OutContext->SetStringField(TEXT("assetClass"), Material->GetClass()->GetPathName());
    OutContext->SetStringField(TEXT("status"), TEXT("active"));
    return true;
}

bool FMaterialContextProvider::BuildSelectionSnapshot(TSharedPtr<FJsonObject>& OutSelection) const
{
    OutSelection.Reset();
    TArray<UMaterialExpression*> SelectedExpressions;
    UMaterial* Material = nullptr;
    if (!CollectSelectedMaterialExpressions(SelectedExpressions, Material))
    {
        return false;
    }

    OutSelection = MakeShared<FJsonObject>();
    OutSelection->SetBoolField(TEXT("isError"), false);
    OutSelection->SetStringField(TEXT("editorType"), TEXT("material"));
    OutSelection->SetStringField(TEXT("provider"), GetProviderId().ToString());
    OutSelection->SetStringField(TEXT("assetPath"), Material->GetPathName());
    OutSelection->SetStringField(TEXT("selectionKind"), TEXT("graph_node"));
    TArray<TSharedPtr<FJsonValue>> Items;
    Items.Reserve(SelectedExpressions.Num());
    for (UMaterialExpression* Expression : SelectedExpressions)
    {
        if (!Expression)
        {
            continue;
        }

        TSharedPtr<FJsonObject> Item = MakeShared<FJsonObject>();
        Item->SetStringField(TEXT("id"), Expression->GetPathName());
        Item->SetStringField(TEXT("name"), Expression->GetName());
        Item->SetStringField(TEXT("class"), Expression->GetClass() ? Expression->GetClass()->GetPathName() : TEXT(""));
        Item->SetStringField(TEXT("path"), Expression->GetPathName());
        Item->SetNumberField(TEXT("nodePosX"), Expression->MaterialExpressionEditorX);
        Item->SetNumberField(TEXT("nodePosY"), Expression->MaterialExpressionEditorY);
        Items.Add(MakeShared<FJsonValueObject>(Item));
    }
    OutSelection->SetArrayField(TEXT("items"), Items);
    OutSelection->SetNumberField(TEXT("count"), Items.Num());
    return true;
}
