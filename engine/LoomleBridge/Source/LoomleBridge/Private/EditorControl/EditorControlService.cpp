// Copyright 2026 Loomle contributors.

#include "EditorControlService.h"

#include "BlueprintEditor.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "EdGraph/EdGraph.h"
#include "Editor.h"
#include "Engine/Blueprint.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Docking/TabManager.h"
#include "GraphEditor.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Sal/SalDiagnostics.h"
#include "Sal/SalJson.h"
#include "Sal/SalModel.h"
#include "Sal/SalTargetResolver.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "Toolkits/AssetEditorToolkit.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/SWidget.h"
#include "Widgets/SWindow.h"

namespace Loomle::EditorControl
{
using namespace Loomle::Sal;

namespace
{
constexpr const TCHAR* EditorTargetAlias = TEXT("editorTarget");

TSharedPtr<FJsonObject> DispatchError(
    const FString& Code,
    const FString& Message)
{
    TSharedPtr<FJsonObject> Error = MakeShared<FJsonObject>();
    Error->SetBoolField(TEXT("isError"), true);
    Error->SetStringField(TEXT("code"), Code);
    Error->SetStringField(TEXT("message"), Message);
    return Error;
}

bool HasOnlyTargetField(const TSharedPtr<FJsonObject>& Arguments)
{
    return Arguments.IsValid()
        && Arguments->Values.Num() == 1
        && Arguments->HasField(TEXT("target"));
}

TSharedPtr<FJsonObject> TargetBinding(
    const TSharedPtr<FJsonObject>& Target)
{
    TSharedPtr<FJsonObject> Binding = MakeShared<FJsonObject>();
    Binding->SetStringField(TEXT("alias"), EditorTargetAlias);
    Binding->SetObjectField(TEXT("target"), Target);
    return Binding;
}

TSharedPtr<FJsonObject> ExactSubject(
    const TSharedPtr<FJsonObject>& Target)
{
    TSharedPtr<FJsonObject> Subject = MakeShared<FJsonObject>();
    Subject->SetStringField(TEXT("targetContext"), TEXT("exact_target"));
    Subject->SetObjectField(TEXT("target"), TargetBinding(Target));
    Subject->SetArrayField(
        TEXT("diagnostics"),
        TArray<TSharedPtr<FJsonValue>>());
    return Subject;
}

void AddDiagnostic(
    const TSharedPtr<FJsonObject>& Subject,
    const TSharedPtr<FJsonObject>& Diagnostic)
{
    TArray<TSharedPtr<FJsonValue>> Diagnostics;
    const TArray<TSharedPtr<FJsonValue>>* Existing = nullptr;
    if (Subject.IsValid()
        && Subject->TryGetArrayField(TEXT("diagnostics"), Existing)
        && Existing != nullptr)
    {
        Diagnostics = *Existing;
    }
    if (Diagnostic.IsValid())
    {
        Diagnostics.Add(MakeShared<FJsonValueObject>(Diagnostic));
    }
    Subject->SetArrayField(TEXT("diagnostics"), Diagnostics);
}

TSharedPtr<FJsonObject> Wrapper(
    const TSharedPtr<FJsonObject>& Subject,
    const FString& Operation,
    const FString& Status)
{
    TSharedPtr<FJsonObject> Outcome = MakeShared<FJsonObject>();
    Outcome->SetStringField(TEXT("operation"), Operation);
    Outcome->SetStringField(TEXT("status"), Status);

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetObjectField(TEXT("subject"), Subject);
    Result->SetObjectField(TEXT("outcome"), Outcome);
    return Result;
}

TSharedPtr<FJsonObject> Failed(
    const FString& Operation,
    const FSalResolvedTarget& Target,
    const FString& Code,
    const FString& Message,
    const FString& Suggestion = FString())
{
    TSharedPtr<FJsonObject> Subject = ExactSubject(Target.CanonicalTarget);
    FSalDiagnosticBuilder Diagnostic =
        FSalDiagnostics::Error(Code, Message)
            .Operation(Operation);
    if (!Target.Id.IsEmpty())
    {
        Diagnostic.Ref(Target.Id);
    }
    else if (!Target.AssetPath.IsEmpty())
    {
        Diagnostic.Ref(Target.AssetPath);
    }
    if (!Suggestion.IsEmpty())
    {
        Diagnostic.Suggestion(Suggestion);
    }
    AddDiagnostic(Subject, Diagnostic.Build());
    return Wrapper(Subject, Operation, TEXT("failed"));
}

TSharedPtr<FJsonObject> Unresolved(
    const FString& Operation,
    TSharedPtr<FJsonObject> Subject)
{
    if (!Subject.IsValid())
    {
        Subject = FSalDiagnostics::Result(
            FSalDiagnostics::Error(
                TEXT("resolution.target_not_found"),
                TEXT("The Editor Target could not be resolved."))
                .Operation(Operation)
                .Build());
    }
    const TArray<TSharedPtr<FJsonValue>>* Diagnostics = nullptr;
    if (Subject->TryGetArrayField(
            TEXT("diagnostics"),
            Diagnostics)
        && Diagnostics != nullptr)
    {
        for (const TSharedPtr<FJsonValue>& Value : *Diagnostics)
        {
            const TSharedPtr<FJsonObject>* Diagnostic = nullptr;
            if (Value.IsValid()
                && Value->TryGetObject(Diagnostic)
                && Diagnostic != nullptr
                && (*Diagnostic).IsValid())
            {
                TArray<TSharedPtr<FJsonValue>> Path;
                Path.Add(MakeShared<FJsonValueString>(TEXT("target")));
                (*Diagnostic)->SetArrayField(
                    TEXT("path"),
                    Path);
            }
        }
    }
    Subject->SetStringField(
        TEXT("targetContext"),
        TEXT("unresolved_target"));
    return Wrapper(Subject, Operation, TEXT("failed"));
}

bool HasActiveModal()
{
    return FSlateApplication::IsInitialized()
        && FSlateApplication::Get().GetActiveModalWindow().IsValid();
}

UAssetEditorSubsystem* AssetEditors()
{
    return GEditor != nullptr
        ? GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()
        : nullptr;
}

bool IsLiveDockTab(const TSharedPtr<SDockTab>& Tab)
{
    return Tab.IsValid()
        && (Tab->GetParentDockTabStack().IsValid()
            || Tab->GetDockArea().IsValid());
}

bool IsLivePresentation(IAssetEditorInstance* Editor)
{
    if (Editor == nullptr)
    {
        return false;
    }
    const TSharedPtr<FTabManager> Manager =
        Editor->GetAssociatedTabManager();
    return Manager.IsValid()
        && IsLiveDockTab(Manager->GetOwnerTab());
}

TArray<IAssetEditorInstance*> FindBlueprintPresentations(
    UBlueprint* Blueprint)
{
    TArray<IAssetEditorInstance*> Presentations;
    UAssetEditorSubsystem* Editors = AssetEditors();
    if (Editors == nullptr || Blueprint == nullptr)
    {
        return Presentations;
    }
    for (IAssetEditorInstance* Editor
        : Editors->FindEditorsForAsset(Blueprint))
    {
        if (IsLivePresentation(Editor))
        {
            Presentations.AddUnique(Editor);
        }
    }
    return Presentations;
}

bool IsNormalInteractiveWindow(const TSharedPtr<SWindow>& Window)
{
    return Window.IsValid()
        && Window->GetType() == EWindowType::Normal
        && Window->IsVisible()
        && !Window->IsWindowMinimized();
}

bool FindForegroundOwner(
    IAssetEditorInstance* Editor,
    TSharedPtr<SDockTab>& OutOwnerTab)
{
    OutOwnerTab.Reset();
    if (Editor == nullptr || !FSlateApplication::IsInitialized())
    {
        return false;
    }
    const TSharedPtr<FTabManager> Manager =
        Editor->GetAssociatedTabManager();
    if (!Manager.IsValid())
    {
        return false;
    }
    OutOwnerTab = Manager->GetOwnerTab();
    const TSharedPtr<SDockTab> RegisteredMajorTab =
        FGlobalTabmanager::Get()->GetMajorTabForTabManager(
            Manager.ToSharedRef());
    return OutOwnerTab.IsValid()
        && OutOwnerTab == RegisteredMajorTab
        && OutOwnerTab->IsForeground()
        && IsNormalInteractiveWindow(
            OutOwnerTab->GetParentWindow());
}

bool IsBlueprintPresentationFocused(IAssetEditorInstance* Editor)
{
    if (HasActiveModal())
    {
        return false;
    }
    TSharedPtr<SDockTab> OwnerTab;
    return FindForegroundOwner(Editor, OwnerTab)
        && FSlateApplication::Get()
                .GetActiveTopLevelRegularWindow()
            == OwnerTab->GetParentWindow();
}

TSharedPtr<FBlueprintEditor> NativeBlueprintGraphEditor(
    UBlueprint* Blueprint,
    IAssetEditorInstance* Presentation)
{
    if (Blueprint == nullptr || Presentation == nullptr)
    {
        return nullptr;
    }
    // UE's typed lookup first resolves the asset through FToolkitManager and
    // checks IToolkit::IsBlueprintEditor(). This avoids casting arbitrary
    // IAssetEditorInstance implementations such as UAssetEditor.
    const TSharedPtr<IBlueprintEditor> Editor =
        FKismetEditorUtilities::GetIBlueprintEditorForObject(
            Blueprint,
            false);
    if (!Editor.IsValid()
        || Editor->GetAssociatedTabManager()
            != Presentation->GetAssociatedTabManager())
    {
        return nullptr;
    }
    return StaticCastSharedPtr<FBlueprintEditor>(Editor);
}

TArray<TSharedPtr<SDockTab>> FindGraphPresentations(
    FBlueprintEditor* Editor,
    UEdGraph* Graph)
{
    TArray<TSharedPtr<SDockTab>> Tabs;
    if (Editor == nullptr || Graph == nullptr)
    {
        return Tabs;
    }
    TArray<TSharedPtr<SDockTab>> Matches;
    Editor->FindOpenTabsContainingDocument(Graph, Matches);
    for (const TSharedPtr<SDockTab>& Tab : Matches)
    {
        if (IsLiveDockTab(Tab))
        {
            Tabs.AddUnique(Tab);
        }
    }
    return Tabs;
}

TSharedPtr<SGraphEditor> GraphWidget(
    const TSharedPtr<SDockTab>& Tab)
{
    if (!Tab.IsValid()
        || Tab->GetContent()->GetType()
            != FName(TEXT("SGraphEditor")))
    {
        return nullptr;
    }
    return StaticCastSharedRef<SGraphEditor>(
        Tab->GetContent()).ToSharedPtr();
}

bool IsGraphPresentationFocused(
    FBlueprintEditor* Editor,
    UEdGraph* Graph,
    const TSharedPtr<SDockTab>& Tab,
    const TSharedPtr<SGraphEditor>& ExpectedGraphEditor = nullptr)
{
    if (HasActiveModal()
        || Editor == nullptr
        || Graph == nullptr
        || !Tab.IsValid()
        || !Tab->IsForeground()
        || Editor->GetFocusedGraph() != Graph)
    {
        return false;
    }

    TSharedPtr<SDockTab> OwnerTab;
    if (!FindForegroundOwner(Editor, OwnerTab))
    {
        return false;
    }

    const TSharedPtr<SGraphEditor> ActualGraphEditor =
        GraphWidget(Tab);
    if (!ActualGraphEditor.IsValid()
        || (ExpectedGraphEditor.IsValid()
            && ActualGraphEditor != ExpectedGraphEditor)
        || ActualGraphEditor->GetCurrentGraph() != Graph)
    {
        return false;
    }

    const TSharedPtr<SWindow> GraphWindow =
        FSlateApplication::Get().FindWidgetWindow(
            ActualGraphEditor.ToSharedRef());
    if (!IsNormalInteractiveWindow(GraphWindow)
        || FSlateApplication::Get()
                .GetActiveTopLevelRegularWindow()
            != GraphWindow)
    {
        return false;
    }

    const TSharedPtr<SWidget> FocusedWidget =
        FSlateApplication::Get().GetKeyboardFocusedWidget();
    FWidgetPath FocusPath;
    return FocusedWidget.IsValid()
        && FSlateApplication::Get()
            .GeneratePathToWidgetUnchecked(
                FocusedWidget.ToSharedRef(),
                FocusPath)
        && FocusPath.IsValid()
        && FocusPath.ContainsWidget(ActualGraphEditor.Get());
}

TSharedPtr<FJsonObject> ModalFailure(
    const FString& Operation,
    const FSalResolvedTarget& Target)
{
    return Failed(
        Operation,
        Target,
        TEXT("runtime.editor_blocked_by_modal"),
        TEXT("A native Modal window blocks the requested Editor presentation change."),
        TEXT("Finish or dismiss the Modal in Unreal Editor, then retry the operation."));
}

TSharedPtr<FJsonObject> AmbiguousFailure(
    const FString& Operation,
    const FSalResolvedTarget& Target,
    const FString& Presentation)
{
    return Failed(
        Operation,
        Target,
        TEXT("resolution.editor_presentation_ambiguous"),
        FString::Printf(
            TEXT("More than one live %s presents the requested Target."),
            *Presentation),
        TEXT("Close duplicate presentations in Unreal Editor, then retry with the same canonical Target."));
}

TSharedPtr<FJsonObject> UnsupportedGraphEditorFailure(
    const FString& Operation,
    const FSalResolvedTarget& Target,
    IAssetEditorInstance* Editor)
{
    const FString EditorName = Editor != nullptr
        ? Editor->GetEditorName().ToString()
        : TEXT("unavailable");
    return Failed(
        Operation,
        Target,
        TEXT("capability.interface_unavailable"),
        FString::Printf(
            TEXT("Editor %s does not expose the native Blueprint Graph document interface supported by editor.%s."),
            *EditorName,
            *Operation),
        TEXT("Open the Target with an FBlueprintEditor-derived native editor, then retry."));
}

TSharedPtr<FJsonObject> OpenBlueprint(
    const FSalResolvedTarget& Target)
{
    UBlueprint* Blueprint = Target.Blueprint;
    UAssetEditorSubsystem* Editors = AssetEditors();
    if (Blueprint == nullptr || Editors == nullptr)
    {
        return Failed(
            TEXT("open"),
            Target,
            TEXT("validation.editor_open_failed"),
            TEXT("The native Asset Editor subsystem is unavailable for this Blueprint."));
    }

    TArray<IAssetEditorInstance*> Presentations =
        FindBlueprintPresentations(Blueprint);
    if (Presentations.Num() > 1)
    {
        return AmbiguousFailure(
            TEXT("open"),
            Target,
            TEXT("Blueprint Editor presentations"));
    }
    if (Presentations.Num() == 1
        && IsBlueprintPresentationFocused(Presentations[0]))
    {
        return Wrapper(
            ExactSubject(Target.CanonicalTarget),
            TEXT("open"),
            TEXT("already_focused"));
    }
    if (HasActiveModal())
    {
        return ModalFailure(TEXT("open"), Target);
    }

    const bool bWasOpen = Presentations.Num() == 1;
    if (bWasOpen)
    {
        Presentations[0]->FocusWindow(Blueprint);
    }
    else
    {
        FText NativeReason;
        if (!Editors->CanOpenEditorForAsset(
                Blueprint,
                EAssetTypeActivationOpenedMethod::Edit,
                &NativeReason)
            || !Editors->OpenEditorForAsset(
                Blueprint,
                EToolkitMode::Standalone,
                TSharedPtr<IToolkitHost>(),
                false,
                EAssetTypeActivationOpenedMethod::Edit))
        {
            return Failed(
                TEXT("open"),
                Target,
                TEXT("validation.editor_open_failed"),
                NativeReason.IsEmpty()
                    ? TEXT("UE did not open the requested Blueprint Editor.")
                    : NativeReason.ToString(),
                TEXT("Confirm the Blueprint is editable and retry in an idle Unreal Editor."));
        }
    }

    Presentations = FindBlueprintPresentations(Blueprint);
    if (Presentations.Num() > 1)
    {
        return AmbiguousFailure(
            TEXT("open"),
            Target,
            TEXT("Blueprint Editor presentations"));
    }
    if (Presentations.Num() != 1
        || !IsBlueprintPresentationFocused(Presentations[0]))
    {
        return Failed(
            TEXT("open"),
            Target,
            TEXT("validation.editor_verification_failed"),
            TEXT("UE did not establish one focused Blueprint Editor for the requested Target."),
            TEXT("Inspect Unreal Editor for a blocked or background editor, then retry."));
    }
    return Wrapper(
        ExactSubject(Target.CanonicalTarget),
        TEXT("open"),
        bWasOpen ? TEXT("focused") : TEXT("opened"));
}

TSharedPtr<FJsonObject> OpenGraph(
    const FSalResolvedTarget& Target)
{
    UBlueprint* Blueprint = Target.Blueprint;
    UEdGraph* Graph = Target.Graph;
    UAssetEditorSubsystem* Editors = AssetEditors();
    if (Blueprint == nullptr || Graph == nullptr || Editors == nullptr)
    {
        return Failed(
            TEXT("open"),
            Target,
            TEXT("validation.editor_open_failed"),
            TEXT("The native Blueprint Graph editor is unavailable for this Target."));
    }

    TArray<IAssetEditorInstance*> Presentations =
        FindBlueprintPresentations(Blueprint);
    if (Presentations.Num() > 1)
    {
        return AmbiguousFailure(
            TEXT("open"),
            Target,
            TEXT("owning Blueprint Editor presentations"));
    }

    bool bDocumentWasOpen = false;
    if (Presentations.Num() == 1)
    {
        const TSharedPtr<FBlueprintEditor> BlueprintEditor =
            NativeBlueprintGraphEditor(
                Blueprint,
                Presentations[0]);
        if (!BlueprintEditor.IsValid()
            || BlueprintEditor->GetBlueprintObj() != Blueprint)
        {
            return UnsupportedGraphEditorFailure(
                TEXT("open"),
                Target,
                Presentations[0]);
        }
        const TArray<TSharedPtr<SDockTab>> Tabs =
            FindGraphPresentations(BlueprintEditor.Get(), Graph);
        if (Tabs.Num() > 1)
        {
            return AmbiguousFailure(
                TEXT("open"),
                Target,
                TEXT("Graph document tabs"));
        }
        bDocumentWasOpen = Tabs.Num() == 1;
        if (bDocumentWasOpen
            && IsGraphPresentationFocused(
                BlueprintEditor.Get(),
                Graph,
                Tabs[0]))
        {
            return Wrapper(
                ExactSubject(Target.CanonicalTarget),
                TEXT("open"),
                TEXT("already_focused"));
        }
    }

    if (HasActiveModal())
    {
        return ModalFailure(TEXT("open"), Target);
    }

    if (Presentations.IsEmpty())
    {
        FText NativeReason;
        if (!Editors->CanOpenEditorForAsset(
                Blueprint,
                EAssetTypeActivationOpenedMethod::Edit,
                &NativeReason)
            || !Editors->OpenEditorForAsset(
                Blueprint,
                EToolkitMode::Standalone,
                TSharedPtr<IToolkitHost>(),
                false,
                EAssetTypeActivationOpenedMethod::Edit))
        {
            return Failed(
                TEXT("open"),
                Target,
                TEXT("validation.editor_open_failed"),
                NativeReason.IsEmpty()
                    ? TEXT("UE did not open the Graph's owning Blueprint Editor.")
                    : NativeReason.ToString(),
                TEXT("Confirm the Blueprint is editable and retry in an idle Unreal Editor."));
        }
        Presentations = FindBlueprintPresentations(Blueprint);
    }

    if (Presentations.Num() > 1)
    {
        return AmbiguousFailure(
            TEXT("open"),
            Target,
            TEXT("owning Blueprint Editor presentations"));
    }
    if (Presentations.Num() != 1)
    {
        return Failed(
            TEXT("open"),
            Target,
            TEXT("validation.editor_verification_failed"),
            TEXT("UE did not establish one owning Blueprint Editor for the requested Graph."));
    }

    IAssetEditorInstance* Presentation = Presentations[0];
    const TSharedPtr<FBlueprintEditor> BlueprintEditor =
        NativeBlueprintGraphEditor(Blueprint, Presentation);
    if (!BlueprintEditor.IsValid()
        || BlueprintEditor->GetBlueprintObj() != Blueprint)
    {
        return UnsupportedGraphEditorFailure(
            TEXT("open"),
            Target,
            Presentation);
    }

    Presentation->FocusWindow(Blueprint);
    const TSharedPtr<SGraphEditor> GraphEditor =
        BlueprintEditor->OpenGraphAndBringToFront(Graph, true);
    const TArray<TSharedPtr<SDockTab>> Tabs =
        FindGraphPresentations(BlueprintEditor.Get(), Graph);
    if (Tabs.Num() > 1)
    {
        return AmbiguousFailure(
            TEXT("open"),
            Target,
            TEXT("Graph document tabs"));
    }
    if (Tabs.Num() != 1
        || !GraphEditor.IsValid()
        || !IsGraphPresentationFocused(
            BlueprintEditor.Get(),
            Graph,
            Tabs[0],
            GraphEditor))
    {
        return Failed(
            TEXT("open"),
            Target,
            TEXT("validation.editor_verification_failed"),
            TEXT("UE did not establish the exact focused Graph document postcondition."),
            TEXT("Inspect the Blueprint Editor for a blocked, background, or unsupported Graph document, then retry."));
    }
    return Wrapper(
        ExactSubject(Target.CanonicalTarget),
        TEXT("open"),
        bDocumentWasOpen ? TEXT("focused") : TEXT("opened"));
}

TSharedPtr<FJsonObject> CloseBlueprint(
    const FSalResolvedTarget& Target)
{
    UBlueprint* Blueprint = Target.Blueprint;
    TArray<IAssetEditorInstance*> Presentations =
        FindBlueprintPresentations(Blueprint);
    if (Presentations.IsEmpty())
    {
        return Wrapper(
            ExactSubject(Target.CanonicalTarget),
            TEXT("close"),
            TEXT("already_closed"));
    }
    if (Presentations.Num() > 1)
    {
        return AmbiguousFailure(
            TEXT("close"),
            Target,
            TEXT("Blueprint Editor presentations"));
    }
    if (HasActiveModal())
    {
        return ModalFailure(TEXT("close"), Target);
    }

    const TSharedPtr<FBlueprintEditor> BlueprintEditor =
        NativeBlueprintGraphEditor(
            Blueprint,
            Presentations[0]);
    Presentations[0]->CloseWindow(
        EAssetEditorCloseReason::AssetEditorHostClosed);
    // FBlueprintEditor marks itself closing synchronously in OnClose after
    // the native veto path has accepted the request. The Asset Editor
    // subsystem removes its bookkeeping entry later, when the toolkit's last
    // shared reference is destroyed, so its immediate query can be stale.
    if (BlueprintEditor.IsValid()
        && BlueprintEditor->IsEditorClosing())
    {
        return Wrapper(
            ExactSubject(Target.CanonicalTarget),
            TEXT("close"),
            TEXT("closed"));
    }
    Presentations = FindBlueprintPresentations(Blueprint);
    if (Presentations.IsEmpty())
    {
        return Wrapper(
            ExactSubject(Target.CanonicalTarget),
            TEXT("close"),
            TEXT("closed"));
    }
    if (HasActiveModal())
    {
        return ModalFailure(TEXT("close"), Target);
    }
    if (Presentations.Num() > 1)
    {
        return Failed(
            TEXT("close"),
            Target,
            TEXT("validation.editor_verification_failed"),
            TEXT("The Blueprint close request left multiple live presentations."));
    }
    return Failed(
        TEXT("close"),
        Target,
        TEXT("validation.editor_close_vetoed"),
        TEXT("The native Blueprint Editor remained open after its close request."),
        TEXT("Resolve any unsaved-change prompt or native close veto in Unreal Editor, then retry."));
}

TSharedPtr<FJsonObject> CloseGraph(
    const FSalResolvedTarget& Target)
{
    UBlueprint* Blueprint = Target.Blueprint;
    UEdGraph* Graph = Target.Graph;
    TArray<IAssetEditorInstance*> Presentations =
        FindBlueprintPresentations(Blueprint);
    if (Presentations.IsEmpty())
    {
        return Wrapper(
            ExactSubject(Target.CanonicalTarget),
            TEXT("close"),
            TEXT("already_closed"));
    }
    if (Presentations.Num() > 1)
    {
        return AmbiguousFailure(
            TEXT("close"),
            Target,
            TEXT("owning Blueprint Editor presentations"));
    }

    TSharedPtr<FBlueprintEditor> BlueprintEditor =
        NativeBlueprintGraphEditor(
            Blueprint,
            Presentations[0]);
    if (!BlueprintEditor.IsValid()
        || BlueprintEditor->GetBlueprintObj() != Blueprint)
    {
        return UnsupportedGraphEditorFailure(
            TEXT("close"),
            Target,
            Presentations[0]);
    }
    TArray<TSharedPtr<SDockTab>> Tabs =
        FindGraphPresentations(BlueprintEditor.Get(), Graph);
    if (Tabs.IsEmpty())
    {
        return Wrapper(
            ExactSubject(Target.CanonicalTarget),
            TEXT("close"),
            TEXT("already_closed"));
    }
    if (Tabs.Num() > 1)
    {
        return AmbiguousFailure(
            TEXT("close"),
            Target,
            TEXT("Graph document tabs"));
    }
    if (HasActiveModal())
    {
        return ModalFailure(TEXT("close"), Target);
    }

    TSharedPtr<SDockTab> ClosingTab = Tabs[0];
    BlueprintEditor->CloseDocumentTab(Graph);
    const bool bTabDetached =
        !IsLiveDockTab(ClosingTab);
    // FDocumentTracker keeps only a weak tab reference, but these local strong
    // references would otherwise make its immediate matching query return the
    // already-detached document as a ghost presentation.
    ClosingTab.Reset();
    Tabs.Reset();
    if (bTabDetached)
    {
        return Wrapper(
            ExactSubject(Target.CanonicalTarget),
            TEXT("close"),
            TEXT("closed"));
    }
    Presentations = FindBlueprintPresentations(Blueprint);
    if (Presentations.IsEmpty())
    {
        return Wrapper(
            ExactSubject(Target.CanonicalTarget),
            TEXT("close"),
            TEXT("closed"));
    }
    if (Presentations.Num() > 1)
    {
        return Failed(
            TEXT("close"),
            Target,
            TEXT("validation.editor_verification_failed"),
            TEXT("The Graph close request left multiple owning Blueprint Editor presentations."));
    }
    BlueprintEditor = NativeBlueprintGraphEditor(
        Blueprint,
        Presentations[0]);
    if (!BlueprintEditor.IsValid())
    {
        return UnsupportedGraphEditorFailure(
            TEXT("close"),
            Target,
            Presentations[0]);
    }
    Tabs = FindGraphPresentations(BlueprintEditor.Get(), Graph);
    if (Tabs.IsEmpty())
    {
        return Wrapper(
            ExactSubject(Target.CanonicalTarget),
            TEXT("close"),
            TEXT("closed"));
    }
    if (HasActiveModal())
    {
        return ModalFailure(TEXT("close"), Target);
    }
    if (Tabs.Num() > 1)
    {
        return Failed(
            TEXT("close"),
            Target,
            TEXT("validation.editor_verification_failed"),
            TEXT("The Graph close request left multiple live document tabs."));
    }
    return Failed(
        TEXT("close"),
        Target,
        TEXT("validation.editor_close_vetoed"),
        TEXT("The native Graph document remained open after its close request."),
        TEXT("Resolve any native close veto in the Blueprint Editor, then retry."));
}

bool IsValidSubject(const TSharedPtr<FJsonObject>& WrapperResult)
{
    const TSharedPtr<FJsonObject>* Subject = nullptr;
    TSharedPtr<FJsonObject> ValidationError;
    return WrapperResult.IsValid()
        && WrapperResult->TryGetObjectField(
            TEXT("subject"),
            Subject)
        && Subject != nullptr
        && FSalJson::ValidateResult(
            *Subject,
            ValidationError);
}
}

TSharedPtr<FJsonObject> FEditorControlService::Execute(
    const FString& Operation,
    const TSharedPtr<FJsonObject>& Arguments,
    TSharedPtr<FJsonObject>& OutDispatchError)
{
    OutDispatchError.Reset();
    if (!(Operation == TEXT("open")
            || Operation == TEXT("close")))
    {
        OutDispatchError = DispatchError(
            TEXT("tool.invalid_arguments"),
            TEXT("Editor control operation must be open or close."));
        return nullptr;
    }

    const TSharedPtr<FJsonObject>* TargetValue = nullptr;
    if (!HasOnlyTargetField(Arguments)
        || !Arguments->TryGetObjectField(
            TEXT("target"),
            TargetValue)
        || TargetValue == nullptr
        || !(*TargetValue).IsValid())
    {
        OutDispatchError = DispatchError(
            TEXT("tool.invalid_arguments"),
            TEXT("editor.open and editor.close require exactly one normalized target object."));
        return nullptr;
    }

    FString ValidationMessage;
    FString Domain;
    if (!FSalJson::ValidateCanonicalTarget(
            *TargetValue,
            ValidationMessage)
        || !(*TargetValue)->TryGetStringField(
            TEXT("domain"),
            Domain)
        || !(Domain == TEXT("blueprint")
            || Domain == TEXT("graph")))
    {
        OutDispatchError = DispatchError(
            TEXT("tool.invalid_arguments"),
            ValidationMessage.IsEmpty()
                ? TEXT("Editor control accepts only canonical Blueprint and Graph Targets.")
                : ValidationMessage);
        return nullptr;
    }

    FSalResolvedTarget Target;
    TSharedPtr<FJsonObject> ResolutionError;
    FSalTargetResolver Resolver;
    if (!Resolver.Resolve(
            EditorTargetAlias,
            *TargetValue,
            true,
            Target,
            ResolutionError))
    {
        TSharedPtr<FJsonObject> Result = Unresolved(
            Operation,
            ResolutionError);
        if (!IsValidSubject(Result))
        {
            OutDispatchError = DispatchError(
                TEXT("runtime.internal_error"),
                TEXT("Editor Target resolution produced an invalid SAL subject."));
            return nullptr;
        }
        return Result;
    }

    TSharedPtr<FJsonObject> Result;
    if (Operation == TEXT("open"))
    {
        Result = Target.Domain == ESalDomain::Graph
            ? OpenGraph(Target)
            : OpenBlueprint(Target);
    }
    else
    {
        Result = Target.Domain == ESalDomain::Graph
            ? CloseGraph(Target)
            : CloseBlueprint(Target);
    }

    if (!IsValidSubject(Result))
    {
        OutDispatchError = DispatchError(
            TEXT("runtime.internal_error"),
            TEXT("Editor control produced an invalid SAL subject."));
        return nullptr;
    }
    return Result;
}
}
