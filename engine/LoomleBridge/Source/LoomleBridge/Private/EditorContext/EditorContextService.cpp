// Copyright 2026 Loomle contributors.

#include "EditorContextService.h"

#include "Algo/Reverse.h"
#include "AssetRegistry/AssetData.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetViewTypes.h"
#include "BlueprintEditor.h"
#include "ContentBrowserItem.h"
#include "Dom/JsonObject.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphSchema.h"
#include "EdGraphSchema_K2.h"
#include "EdGraphSchema_K2_Actions.h"
#include "Editor.h"
#include "EditorModeManager.h"
#include "Elements/Framework/TypedElementSelectionSet.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Engine/Level.h"
#include "Engine/SCS_Node.h"
#include "Engine/SimpleConstructionScript.h"
#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Docking/TabManager.h"
#include "GameFramework/Actor.h"
#include "IDetailsView.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "K2Node.h"
#include "LevelEditor.h"
#include "LevelInstance/LevelInstanceInterface.h"
#include "LevelInstance/LevelInstanceSubsystem.h"
#include "Misc/PackageName.h"
#include "Modules/ModuleManager.h"
#include "Sal/SalDiagnostics.h"
#include "Sal/SalJson.h"
#include "Sal/SalObjectBuilder.h"
#include "SMyBlueprint.h"
#include "SAssetView.h"
#include "SSubobjectEditor.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "Types/ISlateMetaData.h"
#include "UObject/SoftObjectPath.h"
#include "WidgetBlueprint.h"
#include "WidgetBlueprintEditor.h"
#include "WidgetBlueprintEditorUtils.h"
#include "WidgetReference.h"
#include "Blueprint/WidgetTree.h"
#include "Components/ActorComponent.h"
#include "Components/PanelWidget.h"
#include "Components/Widget.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/SWidget.h"
#include "Widgets/SWindow.h"

namespace Loomle::EditorContext
{
using namespace Loomle::Sal;

namespace
{
extern const FName ProviderModal;
extern const FName ProviderBlueprint;
extern const FName ProviderWidget;
extern const FName ProviderContentBrowser;
extern const FName ProviderLevel;
extern const FName ProviderDetails;
extern const FName ProviderAssetEditor;
extern const FName ProviderUnknown;

extern const FName SurfaceModal;
extern const FName SurfaceBlueprintGraph;
extern const FName SurfaceMyBlueprint;
extern const FName SurfaceBlueprintComponents;
extern const FName SurfaceClassSettings;
extern const FName SurfaceClassDefaults;
extern const FName SurfaceWidgetDesigner;
extern const FName SurfaceContentBrowser;
extern const FName SurfaceLevelEditor;
extern const FName SurfaceDetails;
extern const FName SurfaceAssetEditor;
extern const FName SurfaceUnknown;

struct FContextOutput
{
    FSalObjectBuilder Builder;
    TArray<TSharedPtr<FJsonObject>> Diagnostics;

    void Error(const FString& Code, const FString& Message, const FString& Ref = FString())
    {
        FSalDiagnosticBuilder Diagnostic = FSalDiagnostics::Error(Code, Message).Interface(TEXT("editor_context"));
        if (!Ref.IsEmpty())
        {
            Diagnostic.Ref(Ref);
        }
        Diagnostics.Add(Diagnostic.Build());
    }

    TSharedPtr<FJsonObject> Finish() const
    {
        return Builder.BuildResult(Diagnostics);
    }
};

FString GuidText(const FGuid& Guid);
bool IsSalPathSegment(const FString& Text);
FString CommentScalar(FString Text);
FString CommentPath(const TArray<FString>& Segments);
TSharedPtr<FJsonObject> Args();
void AddSurface(FContextOutput& Out, const FString& Surface);
void AddRecoveredFocus(FContextOutput& Out, const FInteractionRecord& Record);
void AddNoSelection(FContextOutput& Out);
void AddMultipleSelection(FContextOutput& Out, int32 Count);
FString GraphTypeText(const UEdGraph* Graph);
FString EmitAsset(FContextOutput& Out, const FString& PreferredAlias, const FString& Path, const FString& Type);
FString EmitAsset(FContextOutput& Out, const FAssetData& Asset);
FString EmitAsset(FContextOutput& Out, UObject* Asset);
FString EmitBlueprint(FContextOutput& Out, UBlueprint* Blueprint);
FString EmitClass(FContextOutput& Out, UClass* Class, const FString& PreferredAlias = FString());
FString EmitGraph(FContextOutput& Out, UEdGraph* Graph, const FString& BlueprintAlias, bool bIncludeRecognition = true);
void EmitNode(FContextOutput& Out, UEdGraphNode* Node, const FString& GraphAlias);
UObject* FindNearestAsset(UObject* Object);
bool IsOpenAssetEditor(IAssetEditorInstance* Editor);
TArray<UObject*> AssetsEditedBy(IAssetEditorInstance* Editor);
IAssetEditorInstance* FindAssetEditorForTab(const TSharedPtr<SDockTab>& Tab);
IAssetEditorInstance* FindAssetEditorForWindow(
    const TSharedPtr<SWindow>& Window,
    TSharedPtr<SDockTab>& OutOwnerTab);
IAssetEditorInstance* ResolveTrackedAssetEditor(const FInteractionRecord& Record);
TSharedPtr<SDockTab> FindDockTab(const FWidgetPath& Path);
TSharedPtr<IDetailsView> FindDetailsView(const FWidgetPath& Path);
TSharedPtr<SWidget> FindWidgetOfType(const FWidgetPath& Path, FName Type);
void CollectStructuralPath(const FWidgetPath& Path, TArray<FName>& OutTags, TArray<FName>& OutTypes);
bool HasAnyPrefix(FName Value, std::initializer_list<const TCHAR*> Prefixes);
bool HasAnyType(const FRecognitionInput& Input, std::initializer_list<const TCHAR*> Types);
bool HasAnyTag(const FRecognitionInput& Input, std::initializer_list<const TCHAR*> Tags);
void CopyRecognition(const FRecognitionInput& Input, FInteractionRecord& Out);
TSharedPtr<FJsonObject> InvalidTrackedSurface(const FInteractionRecord& Record, const FString& Message);

void EmitVariable(
    FContextOutput& Out,
    UBlueprint* Blueprint,
    const FString& BlueprintAlias,
    const FBPVariableDescription& Variable,
    const bool bDispatcher)
{
    if (!Variable.VarGuid.IsValid())
    {
        Out.Error(
            TEXT("context.identity_missing"),
            TEXT("The selected Blueprint declaration has no valid variable GUID."),
            Variable.VarName.ToString());
        return;
    }
    bool bOwned = false;
    int32 IdentityMatches = 0;
    if (Blueprint != nullptr)
    {
        for (const FBPVariableDescription& Candidate : Blueprint->NewVariables)
        {
            bOwned |= &Candidate == &Variable;
            if (Candidate.VarGuid == Variable.VarGuid)
            {
                ++IdentityMatches;
            }
        }
    }
    if (!bOwned)
    {
        Out.Error(
            TEXT("context.native_inconsistent"),
            TEXT("The selected Blueprint declaration is not owned by the reported Blueprint."),
            Variable.VarName.ToString());
        return;
    }
    if (IdentityMatches != 1)
    {
        Out.Error(
            TEXT("context.identity_duplicate"),
            TEXT("The selected Blueprint declaration GUID is duplicated in its owner."),
            GuidText(Variable.VarGuid));
        return;
    }
    TSharedPtr<FJsonObject> ValueArgs = Args();
    ValueArgs->SetStringField(TEXT("id"), GuidText(Variable.VarGuid));
    const FString Name = Variable.VarName.ToString();
    if (IsSalPathSegment(Name))
    {
        Out.Builder.AddMemberBinding(
            BlueprintAlias,
            {Name},
            Value::Call(bDispatcher ? TEXT("dispatcher") : TEXT("variable"), ValueArgs));
    }
    else
    {
        Out.Builder.AddLocalBinding(
            Out.Builder.UniqueAlias(Name),
            Value::Call(bDispatcher ? TEXT("dispatcher") : TEXT("variable"), ValueArgs));
        Out.Builder.AddComment(FString::Printf(
            TEXT("member path: unavailable in SAL identifier syntax\nnative name: %s"),
            *CommentScalar(Name)));
    }
}

const FBPVariableDescription* FindBlueprintVariable(UBlueprint* Blueprint, const FName Name)
{
    if (Blueprint == nullptr)
    {
        return nullptr;
    }
    return Blueprint->NewVariables.FindByPredicate([Name](const FBPVariableDescription& Variable)
    {
        return Variable.VarName == Name;
    });
}

void EmitComponentChain(FContextOutput& Out, UBlueprint* Blueprint, USCS_Node* Selected)
{
    if (Blueprint == nullptr || Blueprint->SimpleConstructionScript == nullptr || Selected == nullptr)
    {
        return;
    }
    const FString BlueprintAlias = EmitBlueprint(Out, Blueprint);
    TArray<USCS_Node*> Chain;
    TSet<USCS_Node*> Seen;
    USCS_Node* Current = Selected;
    for (; Current != nullptr && !Seen.Contains(Current); )
    {
        Seen.Add(Current);
        Chain.Add(Current);
        Current = Blueprint->SimpleConstructionScript->FindParentNode(Current);
    }
    if (Current != nullptr)
    {
        Out.Error(
            TEXT("context.native_inconsistent"),
            TEXT("The selected Blueprint Component ancestry contains a cycle."),
            Current->GetPathName());
        return;
    }
    Algo::Reverse(Chain);

    TArray<FString> NativePath;
    bool bPathExpressible = true;
    const TArray<USCS_Node*> OwnedNodes = Blueprint->SimpleConstructionScript->GetAllNodes();
    for (USCS_Node* Node : Chain)
    {
        if (Node == nullptr)
        {
            continue;
        }
        const FName VariableName = Node->GetVariableName();
        if (VariableName.IsNone() || !Node->VariableGuid.IsValid())
        {
            Out.Error(
                TEXT("context.identity_missing"),
                TEXT("A selected Blueprint Component has no stable variable name or VariableGuid."),
                Node->GetPathName());
            return;
        }
        if (!OwnedNodes.Contains(Node))
        {
            Out.Error(
                TEXT("context.native_inconsistent"),
                TEXT("A selected Blueprint Component is not owned by the reported SCS."),
                Node->GetPathName());
            return;
        }
        int32 IdentityMatches = 0;
        for (const USCS_Node* Candidate : OwnedNodes)
        {
            if (Candidate != nullptr && Candidate->VariableGuid == Node->VariableGuid)
            {
                ++IdentityMatches;
            }
        }
        if (IdentityMatches != 1)
        {
            Out.Error(
                TEXT("context.identity_duplicate"),
                TEXT("A selected Blueprint Component VariableGuid is duplicated in its owner."),
                GuidText(Node->VariableGuid));
            return;
        }
        NativePath.Add(VariableName.ToString());
        bPathExpressible &= IsSalPathSegment(VariableName.ToString());
    }
    if (!bPathExpressible)
    {
        TSharedPtr<FJsonObject> ValueArgs = Args();
        ValueArgs->SetStringField(TEXT("id"), GuidText(Selected->VariableGuid));
        const UClass* ComponentClass = Selected->ComponentClass != nullptr
            ? Selected->ComponentClass.Get()
            : Selected->ComponentTemplate != nullptr ? Selected->ComponentTemplate->GetClass() : nullptr;
        if (ComponentClass != nullptr)
        {
            ValueArgs->SetStringField(TEXT("type"), ComponentClass->GetPathName());
        }
        Out.Builder.AddLocalBinding(
            Out.Builder.UniqueAlias(Selected->GetVariableName().ToString()),
            Value::Call(TEXT("component"), ValueArgs));
        Out.Builder.AddComment(
            TEXT("member path: unavailable in SAL identifier syntax\nnative path: ") + CommentPath(NativePath));
        return;
    }

    TArray<FString> Path;
    for (USCS_Node* Node : Chain)
    {
        if (Node == nullptr)
        {
            continue;
        }
        const FName VariableName = Node->GetVariableName();
        Path.Add(VariableName.ToString());
        TSharedPtr<FJsonObject> ValueArgs = Args();
        ValueArgs->SetStringField(TEXT("id"), GuidText(Node->VariableGuid));
        const UClass* ComponentClass = Node->ComponentClass != nullptr
            ? Node->ComponentClass.Get()
            : Node->ComponentTemplate != nullptr ? Node->ComponentTemplate->GetClass() : nullptr;
        if (ComponentClass != nullptr)
        {
            ValueArgs->SetStringField(TEXT("type"), ComponentClass->GetPathName());
        }
        Out.Builder.AddMemberBinding(BlueprintAlias, Path, Value::Call(TEXT("component"), ValueArgs));
    }
}

class FModalProvider final : public IEditorContextProvider
{
public:
    virtual FName Name() const override { return ProviderModal; }
    virtual int32 Priority() const override { return 1100; }

    virtual bool Recognize(const FRecognitionInput& Input, FInteractionRecord& OutRecord) const override
    {
        if (!Input.bModal)
        {
            return false;
        }
        CopyRecognition(Input, OutRecord);
        OutRecord.Provider = Name();
        OutRecord.Surface = SurfaceModal;
        return true;
    }

    virtual TSharedPtr<FJsonObject> Build(const FInteractionRecord&) const override
    {
        FContextOutput Out;
        Out.Builder.AddComment(TEXT(
            "surface: modal dialog\n"
            "selection: unavailable\n"
            "previous context: suppressed"));
        return Out.Finish();
    }
};

class FBlueprintProvider final : public IEditorContextProvider
{
public:
    virtual FName Name() const override { return ProviderBlueprint; }
    virtual int32 Priority() const override { return 1000; }

    virtual bool Recognize(const FRecognitionInput& Input, FInteractionRecord& OutRecord) const override
    {
        if (Input.AssetEditor == nullptr
            || !(Input.EditorName == FName(TEXT("BlueprintEditor"))
                || Input.EditorName == FName(TEXT("WidgetBlueprintEditor"))))
        {
            return false;
        }

        if (Input.EditorName == FName(TEXT("WidgetBlueprintEditor"))
            && static_cast<FWidgetBlueprintEditor*>(Input.AssetEditor)->IsModeCurrent(FName(TEXT("DesignerName"))))
        {
            // Designer mode has its own native selection model. The exact
            // editor association is already structural, so a low-level focus
            // leaf does not turn Designer selection into Graph selection.
            return false;
        }

        FBlueprintEditor* Editor = static_cast<FBlueprintEditor*>(Input.AssetEditor);
        const FName SelectionState = Editor->GetUISelectionState();
        FName Surface;
        if (SelectionState == FBlueprintEditor::SelectionState_Graph)
        {
            Surface = SurfaceBlueprintGraph;
        }
        else if (SelectionState == FBlueprintEditor::SelectionState_MyBlueprint)
        {
            Surface = SurfaceMyBlueprint;
        }
        else if (SelectionState == FBlueprintEditor::SelectionState_Components)
        {
            Surface = SurfaceBlueprintComponents;
        }
        else if (SelectionState == FBlueprintEditor::SelectionState_ClassSettings)
        {
            Surface = SurfaceClassSettings;
        }
        else if (SelectionState == FBlueprintEditor::SelectionState_ClassDefaults)
        {
            Surface = SurfaceClassDefaults;
        }
        else
        {
            return false;
        }

        CopyRecognition(Input, OutRecord);
        OutRecord.Provider = Name();
        OutRecord.Surface = Surface;
        return true;
    }

    virtual TSharedPtr<FJsonObject> Build(const FInteractionRecord& Record) const override
    {
        if (!IsOpenAssetEditor(Record.AssetEditor))
        {
            return InvalidTrackedSurface(Record, TEXT("The tracked Blueprint Editor was closed before Context was read."));
        }
        FBlueprintEditor* Editor = static_cast<FBlueprintEditor*>(Record.AssetEditor);
        UBlueprint* Blueprint = Editor->GetBlueprintObj();
        if (Blueprint == nullptr)
        {
            return InvalidTrackedSurface(Record, TEXT("The tracked Blueprint Editor no longer owns exactly one Blueprint."));
        }
        const TArray<UObject*> EditedAssets = AssetsEditedBy(Editor);
        if (!EditedAssets.Contains(Blueprint)
            || (Record.bRecoveredHostFromWindow && EditedAssets.Num() != 1))
        {
            return InvalidTrackedSurface(
                Record,
                TEXT("The structurally resolved Blueprint Editor does not uniquely own its reported Blueprint."));
        }
        if (!SurfaceMatchesSelectionState(Record.Surface, Editor->GetUISelectionState()))
        {
            return InvalidTrackedSurface(
                Record,
                TEXT("The Blueprint Editor changed surface before Context was read."));
        }

        if (Record.Surface == SurfaceBlueprintGraph)
        {
            return BuildGraph(Editor, Blueprint, Record);
        }
        if (Record.Surface == SurfaceMyBlueprint)
        {
            return BuildMyBlueprint(Editor, Blueprint, Record);
        }
        if (Record.Surface == SurfaceBlueprintComponents)
        {
            return BuildComponents(Editor, Blueprint, Record);
        }
        if (Record.Surface == SurfaceClassSettings)
        {
            FContextOutput Out;
            AddSurface(Out, TEXT("Blueprint Editor / Class Settings"));
            AddRecoveredFocus(Out, Record);
            EmitBlueprint(Out, Blueprint);
            return Out.Finish();
        }
        if (Record.Surface == SurfaceClassDefaults)
        {
            FContextOutput Out;
            AddSurface(Out, TEXT("Blueprint Editor / Class Defaults"));
            AddRecoveredFocus(Out, Record);
            if (Blueprint->GeneratedClass != nullptr)
            {
                EmitClass(Out, Blueprint->GeneratedClass, Blueprint->GetName() + TEXT("Class"));
                Out.Builder.AddComment(FString::Printf(TEXT("source: %s"), *Blueprint->GetPathName()));
            }
            else
            {
                EmitBlueprint(Out, Blueprint);
                Out.Builder.AddComment(TEXT("Class Defaults unavailable: Blueprint has no valid GeneratedClass"));
            }
            return Out.Finish();
        }
        return InvalidTrackedSurface(Record, TEXT("The tracked Blueprint surface is no longer recognized."));
    }

private:
    static bool SurfaceMatchesSelectionState(const FName Surface, const FName SelectionState)
    {
        if (Surface == SurfaceBlueprintGraph)
        {
            return SelectionState == FBlueprintEditor::SelectionState_Graph;
        }
        if (Surface == SurfaceMyBlueprint)
        {
            return SelectionState == FBlueprintEditor::SelectionState_MyBlueprint;
        }
        if (Surface == SurfaceBlueprintComponents)
        {
            return SelectionState == FBlueprintEditor::SelectionState_Components;
        }
        if (Surface == SurfaceClassSettings)
        {
            return SelectionState == FBlueprintEditor::SelectionState_ClassSettings;
        }
        if (Surface == SurfaceClassDefaults)
        {
            return SelectionState == FBlueprintEditor::SelectionState_ClassDefaults;
        }
        return false;
    }

    static TSharedPtr<FJsonObject> BuildGraph(
        FBlueprintEditor* Editor,
        UBlueprint* Blueprint,
        const FInteractionRecord& Record)
    {
        FContextOutput Out;
        AddSurface(Out, TEXT("Blueprint Editor / Graph"));
        AddRecoveredFocus(Out, Record);
        const FString BlueprintAlias = EmitBlueprint(Out, Blueprint);
        UEdGraph* Graph = Editor->GetFocusedGraph();
        if (Graph == nullptr || FBlueprintEditorUtils::FindBlueprintForGraph(Graph) != Blueprint)
        {
            Out.Error(
                TEXT("context.owner_invalid"),
                TEXT("The focused Graph is unavailable or is not owned by the tracked Blueprint."),
                Blueprint->GetPathName());
            return Out.Finish();
        }
        const FString GraphAlias = EmitGraph(Out, Graph, BlueprintAlias);

        const FGraphPanelSelectionSet Selected = Editor->GetSelectedNodes();
        TArray<UEdGraphNode*> Nodes;
        for (UObject* Object : Selected)
        {
            if (UEdGraphNode* Node = Cast<UEdGraphNode>(Object))
            {
                Nodes.Add(Node);
            }
        }
        if (Selected.Num() == 0)
        {
            AddNoSelection(Out);
        }
        else if (Selected.Num() > 1)
        {
            AddMultipleSelection(Out, Selected.Num());
        }
        else if (Nodes.Num() != 1)
        {
            Out.Builder.AddComment(TEXT("selected: unsupported Graph item\ninterface: unavailable"));
        }
        else if (Nodes[0]->GetGraph() != Graph)
        {
            Out.Error(
                TEXT("context.native_inconsistent"),
                TEXT("The selected Node is not owned by the focused Graph."),
                Nodes[0]->GetPathName());
        }
        else
        {
            EmitNode(Out, Nodes[0], GraphAlias);
        }
        return Out.Finish();
    }

    static TSharedPtr<FJsonObject> BuildMyBlueprint(
        FBlueprintEditor* Editor,
        UBlueprint* Blueprint,
        const FInteractionRecord& Record)
    {
        FContextOutput Out;
        AddSurface(Out, TEXT("Blueprint Editor / My Blueprint"));
        AddRecoveredFocus(Out, Record);
        const FString BlueprintAlias = EmitBlueprint(Out, Blueprint);
        const TSharedPtr<SMyBlueprint> MyBlueprint = Editor->GetMyBlueprintWidget();
        if (!MyBlueprint.IsValid())
        {
            Out.Error(TEXT("context.owner_invalid"), TEXT("My Blueprint panel was reconstructed before Context was read."));
            return Out.Finish();
        }

        if (FEdGraphSchemaAction_K2LocalVar* Local = MyBlueprint->SelectionAsLocalVar())
        {
            UStruct* Scope = Cast<UStruct>(Local->GetVariableScope());
            UEdGraph* ScopeGraph = Scope != nullptr
                ? FBlueprintEditorUtils::FindScopeGraph(Blueprint, Scope)
                : nullptr;
            if (ScopeGraph != nullptr
                && FBlueprintEditorUtils::FindBlueprintForGraph(ScopeGraph) == Blueprint)
            {
                EmitGraph(Out, ScopeGraph, BlueprintAlias);
            }
            Out.Builder.AddComment(FString::Printf(
                TEXT("selected: local variable\nname: %s\nscope: %s\nscopeGraph: %s\ninterface: unavailable"),
                *CommentScalar(Local->GetVariableName().ToString()),
                *CommentScalar(Scope != nullptr ? Scope->GetName() : TEXT("unknown")),
                *CommentScalar(ScopeGraph != nullptr ? ScopeGraph->GetName() : TEXT("unavailable"))));
            return Out.Finish();
        }
        if (FEdGraphSchemaAction_K2Delegate* Dispatcher = MyBlueprint->SelectionAsDelegate())
        {
            if (const FBPVariableDescription* Variable = FindBlueprintVariable(Blueprint, Dispatcher->GetDelegateName()))
            {
                EmitVariable(Out, Blueprint, BlueprintAlias, *Variable, true);
            }
            else
            {
                Out.Builder.AddComment(FString::Printf(
                    TEXT("selected: inherited dispatcher\nname: %s\ninterface: unavailable"),
                    *CommentScalar(Dispatcher->GetDelegateName().ToString())));
            }
            return Out.Finish();
        }
        if (FEdGraphSchemaAction_K2Var* VariableAction = MyBlueprint->SelectionAsVar())
        {
            if (const FBPVariableDescription* Variable = FindBlueprintVariable(Blueprint, VariableAction->GetVariableName()))
            {
                EmitVariable(Out, Blueprint, BlueprintAlias, *Variable, false);
            }
            else
            {
                Out.Builder.AddComment(FString::Printf(
                    TEXT("selected: inherited variable\nname: %s\ninterface: unavailable"),
                    *CommentScalar(VariableAction->GetVariableName().ToString())));
            }
            return Out.Finish();
        }
        if (FEdGraphSchemaAction_K2Graph* GraphAction = MyBlueprint->SelectionAsGraph())
        {
            UEdGraph* Graph = GraphAction->EdGraph;
            if (Graph != nullptr && FBlueprintEditorUtils::FindBlueprintForGraph(Graph) == Blueprint)
            {
                EmitGraph(Out, Graph, BlueprintAlias);
            }
            else
            {
                Out.Error(TEXT("context.native_inconsistent"), TEXT("The selected My Blueprint Graph is unavailable or has a different owner."));
            }
            return Out.Finish();
        }
        if (FEdGraphSchemaAction_K2Event* EventAction = MyBlueprint->SelectionAsEvent())
        {
            return BuildAuthoredActionNode(Out, Blueprint, BlueprintAlias, EventAction->NodeTemplate, TEXT("event"));
        }
        if (FEdGraphSchemaAction_K2InputAction* InputAction = MyBlueprint->SelectionAsInputAction())
        {
            return BuildAuthoredActionNode(Out, Blueprint, BlueprintAlias, InputAction->NodeTemplate, TEXT("input action"));
        }
        if (FEdGraphSchemaAction_K2Enum* EnumAction = MyBlueprint->SelectionAsEnum())
        {
            if (EnumAction->Enum != nullptr)
            {
                EmitAsset(Out, EnumAction->Enum);
            }
            else
            {
                Out.Error(TEXT("context.owner_invalid"), TEXT("The selected User Defined Enum is no longer valid."));
            }
            return Out.Finish();
        }
        if (FEdGraphSchemaAction_K2Struct* StructAction = MyBlueprint->SelectionAsStruct())
        {
            if (StructAction->Struct != nullptr)
            {
                EmitAsset(Out, StructAction->Struct);
            }
            else
            {
                Out.Error(TEXT("context.owner_invalid"), TEXT("The selected User Defined Struct is no longer valid."));
            }
            return Out.Finish();
        }
        if (!MyBlueprint->SelectionIsCategory())
        {
            Out.Builder.AddComment(TEXT("selected: unsupported My Blueprint action\ninterface: unavailable"));
        }
        else
        {
            // UE 5.7 implements SelectionIsCategory as the inverse of
            // SelectionHasContextMenu, so the public API cannot distinguish a
            // selected category from an empty action selection.
            Out.Builder.AddComment(TEXT(
                "selection: unavailable\n"
                "reason: My Blueprint category and empty selection are indistinguishable through the public API"));
        }
        return Out.Finish();
    }

    static TSharedPtr<FJsonObject> BuildAuthoredActionNode(
        FContextOutput& Out,
        UBlueprint* Blueprint,
        const FString& BlueprintAlias,
        UEdGraphNode* Node,
        const TCHAR* NativeKind)
    {
        UEdGraph* Graph = Node != nullptr ? Node->GetGraph() : nullptr;
        if (Node != nullptr
            && Graph != nullptr
            && Node->NodeGuid.IsValid()
            && FBlueprintEditorUtils::FindBlueprintForGraph(Graph) == Blueprint)
        {
            const FString GraphAlias = EmitGraph(Out, Graph, BlueprintAlias);
            EmitNode(Out, Node, GraphAlias);
        }
        else
        {
            Out.Builder.AddComment(FString::Printf(
                TEXT("selected: %s template\ninterface: unavailable"),
                NativeKind));
        }
        return Out.Finish();
    }

    static TSharedPtr<FJsonObject> BuildComponents(
        FBlueprintEditor* Editor,
        UBlueprint* Blueprint,
        const FInteractionRecord& Record)
    {
        FContextOutput Out;
        AddSurface(Out, TEXT("Blueprint Editor / Components"));
        AddRecoveredFocus(Out, Record);
        const TArray<TSharedPtr<FSubobjectEditorTreeNode>> Selected = Editor->GetSelectedSubobjectEditorTreeNodes();
        if (Selected.IsEmpty())
        {
            EmitBlueprint(Out, Blueprint);
            AddNoSelection(Out);
            return Out.Finish();
        }
        if (Selected.Num() > 1)
        {
            EmitBlueprint(Out, Blueprint);
            AddMultipleSelection(Out, Selected.Num());
            return Out.Finish();
        }

        const TSharedPtr<FSubobjectEditorTreeNode> TreeNode = Selected[0];
        if (!TreeNode.IsValid() || !TreeNode->IsValid())
        {
            EmitBlueprint(Out, Blueprint);
            Out.Error(TEXT("context.owner_invalid"), TEXT("The selected Component tree item was reconstructed before Context was read."));
            return Out.Finish();
        }
        if (TreeNode->IsRootActorNode())
        {
            EmitBlueprint(Out, Blueprint);
            Out.Builder.AddComment(TEXT("selected: actor root"));
            return Out.Finish();
        }

        const UActorComponent* Component = TreeNode->GetComponentTemplate();
        if (TreeNode->IsNativeComponent())
        {
            EmitBlueprint(Out, Blueprint);
            Out.Builder.AddComment(FString::Printf(
                TEXT("selected: native component\nname: %s\ntype: %s\ninterface: unavailable"),
                *CommentScalar(TreeNode->GetVariableName().ToString()),
                *CommentScalar(Component != nullptr ? Component->GetClass()->GetPathName() : TEXT("unknown"))));
            return Out.Finish();
        }

        // FSubobjectData intentionally keeps its SCS accessor private in UE
        // 5.7. Resolve only by exact template identity inside the current
        // Blueprint's own SCS; no visible-name inference is allowed here.
        USCS_Node* SCSNode = nullptr;
        if (Component != nullptr && Blueprint->SimpleConstructionScript != nullptr)
        {
            for (USCS_Node* Candidate : Blueprint->SimpleConstructionScript->GetAllNodes())
            {
                if (Candidate != nullptr && Candidate->ComponentTemplate == Component)
                {
                    if (SCSNode != nullptr)
                    {
                        EmitBlueprint(Out, Blueprint);
                        Out.Error(
                            TEXT("context.identity_ambiguous"),
                            TEXT("More than one local SCS Node owns the selected Component template."),
                            Component->GetPathName());
                        return Out.Finish();
                    }
                    SCSNode = Candidate;
                }
            }
        }
        if (SCSNode == nullptr)
        {
            EmitBlueprint(Out, Blueprint);
            Out.Builder.AddComment(FString::Printf(
                TEXT("selected: inherited component\nname: %s\ntype: %s\nownerClass: %s\ninterface: unavailable"),
                *CommentScalar(TreeNode->GetVariableName().ToString()),
                *CommentScalar(Component != nullptr ? Component->GetClass()->GetPathName() : TEXT("unknown")),
                *CommentScalar(Component != nullptr && Component->GetOuter() != nullptr
                    ? Component->GetOuter()->GetPathName()
                    : TEXT("unknown"))));
            return Out.Finish();
        }
        if (Blueprint->SimpleConstructionScript == nullptr
            || SCSNode->GetOuter() != Blueprint->SimpleConstructionScript
            || !Blueprint->SimpleConstructionScript->GetAllNodes().Contains(SCSNode))
        {
            EmitBlueprint(Out, Blueprint);
            Out.Builder.AddComment(FString::Printf(
                TEXT("selected: inherited component\nname: %s\ntype: %s\nownerClass: %s\ninterface: unavailable"),
                *CommentScalar(TreeNode->GetVariableName().ToString()),
                *CommentScalar(Component != nullptr ? Component->GetClass()->GetPathName() : TEXT("unknown")),
                *CommentScalar(Component != nullptr && Component->GetOuter() != nullptr
                    ? Component->GetOuter()->GetPathName()
                    : TEXT("unknown"))));
            return Out.Finish();
        }

        EmitComponentChain(Out, Blueprint, SCSNode);
        return Out.Finish();
    }
};

bool TryWidgetGuid(UWidgetBlueprint* Blueprint, UWidget* Widget, FGuid& OutGuid, bool& bOutDuplicate)
{
    OutGuid.Invalidate();
    bOutDuplicate = false;
    if (Blueprint == nullptr || Widget == nullptr)
    {
        return false;
    }

    int32 PointerMatches = 0;
    int32 NameMatches = 0;
    Blueprint->ForEachSourceWidget([Widget, &PointerMatches, &NameMatches](UWidget* Candidate)
    {
        if (Candidate == Widget)
        {
            ++PointerMatches;
        }
        if (Candidate != nullptr && Candidate->GetFName() == Widget->GetFName())
        {
            ++NameMatches;
        }
    });
    if (PointerMatches != 1 || NameMatches != 1)
    {
        bOutDuplicate = PointerMatches > 1 || NameMatches > 1;
        return false;
    }

    const FGuid* Guid = Blueprint->WidgetVariableNameToGuidMap.Find(Widget->GetFName());
    if (Guid == nullptr || !Guid->IsValid())
    {
        return false;
    }
    OutGuid = *Guid;
    int32 Matches = 0;
    for (const TPair<FName, FGuid>& Pair : Blueprint->WidgetVariableNameToGuidMap)
    {
        if (Pair.Value == OutGuid)
        {
            ++Matches;
        }
    }
    bOutDuplicate = Matches != 1;
    return true;
}

TSharedPtr<FJsonValue> WidgetValue(
    FContextOutput& Out,
    UWidgetBlueprint* Blueprint,
    UWidget* Widget)
{
    FGuid Guid;
    bool bDuplicate = false;
    if (!TryWidgetGuid(Blueprint, Widget, Guid, bDuplicate) || bDuplicate)
    {
        Out.Error(
            bDuplicate ? TEXT("context.identity_duplicate") : TEXT("context.identity_missing"),
            bDuplicate
                ? TEXT("The selected source Widget identity is duplicated in its Blueprint.")
                : TEXT("The selected Widget is not uniquely owned by the current source tree or has no stable WidgetVariableNameToGuidMap entry."),
            Widget != nullptr ? Widget->GetPathName() : FString());
        return nullptr;
    }
    TSharedPtr<FJsonObject> ValueArgs = Args();
    ValueArgs->SetStringField(TEXT("id"), GuidText(Guid));
    ValueArgs->SetStringField(TEXT("type"), Widget->GetClass()->GetPathName());
    return Value::Call(TEXT("widget"), ValueArgs);
}

bool EmitWidgetChain(
    FContextOutput& Out,
    UWidgetBlueprint* Blueprint,
    UWidget* Selected,
    FString* OutSelectedId = nullptr)
{
    if (Blueprint == nullptr || Blueprint->WidgetTree == nullptr || Selected == nullptr)
    {
        return false;
    }
    const FString BlueprintAlias = EmitBlueprint(Out, Blueprint);
    TArray<UWidget*> Chain;
    TSet<UWidget*> Seen;
    UWidget* Current = Selected;
    for (; Current != nullptr && !Seen.Contains(Current); )
    {
        Seen.Add(Current);
        Chain.Add(Current);
        UWidget* Parent = Current->GetParent();
        if (Parent == nullptr)
        {
            Parent = FWidgetBlueprintEditorUtils::FindNamedSlotHostWidgetForContent(Current, Blueprint->WidgetTree);
        }
        Current = Parent;
    }
    if (Current != nullptr)
    {
        Out.Error(
            TEXT("context.native_inconsistent"),
            TEXT("The selected Widget ancestry contains a cycle."),
            Current->GetPathName());
        return false;
    }
    Algo::Reverse(Chain);

    TArray<FString> NativePath;
    bool bPathExpressible = true;
    for (UWidget* Widget : Chain)
    {
        if (Widget != nullptr)
        {
            NativePath.Add(Widget->GetName());
            bPathExpressible &= IsSalPathSegment(Widget->GetName());
        }
    }
    if (!bPathExpressible)
    {
        TSharedPtr<FJsonValue> ValueObject = WidgetValue(Out, Blueprint, Selected);
        if (!ValueObject.IsValid())
        {
            return false;
        }
        Out.Builder.AddLocalBinding(Out.Builder.UniqueAlias(Selected->GetName()), ValueObject);
        if (OutSelectedId != nullptr)
        {
            FGuid Guid;
            bool bDuplicate = false;
            if (TryWidgetGuid(Blueprint, Selected, Guid, bDuplicate) && !bDuplicate)
            {
                *OutSelectedId = GuidText(Guid);
            }
        }
        Out.Builder.AddComment(
            TEXT("member path: unavailable in SAL identifier syntax\nnative path: ") + CommentPath(NativePath));
        if (!Chain.IsEmpty()
            && Chain[0] != Blueprint->WidgetTree->RootWidget
            && Blueprint->WidgetTree->NamedSlotBindings.FindKey(Chain[0]) == nullptr)
        {
            Out.Builder.AddComment(TEXT("detached source Widget subtree"));
        }
        return true;
    }

    TArray<FString> Path;
    for (UWidget* Widget : Chain)
    {
        TSharedPtr<FJsonValue> ValueObject = WidgetValue(Out, Blueprint, Widget);
        if (!ValueObject.IsValid())
        {
            return false;
        }
        Path.Add(Widget->GetName());
        Out.Builder.AddMemberBinding(BlueprintAlias, Path, ValueObject);
    }
    if (OutSelectedId != nullptr)
    {
        FGuid Guid;
        bool bDuplicate = false;
        if (TryWidgetGuid(Blueprint, Selected, Guid, bDuplicate) && !bDuplicate)
        {
            *OutSelectedId = GuidText(Guid);
        }
    }
    if (!Chain.IsEmpty()
        && Chain[0] != Blueprint->WidgetTree->RootWidget
        && Blueprint->WidgetTree->NamedSlotBindings.FindKey(Chain[0]) == nullptr)
    {
        Out.Builder.AddComment(TEXT("detached source Widget subtree"));
    }
    return true;
}

class FWidgetDesignerProvider final : public IEditorContextProvider
{
public:
    virtual FName Name() const override { return ProviderWidget; }
    virtual int32 Priority() const override { return 950; }

    virtual bool Recognize(const FRecognitionInput& Input, FInteractionRecord& OutRecord) const override
    {
        if (Input.AssetEditor == nullptr
            || Input.EditorName != FName(TEXT("WidgetBlueprintEditor"))
            || !static_cast<FWidgetBlueprintEditor*>(Input.AssetEditor)->IsModeCurrent(FName(TEXT("DesignerName"))))
        {
            return false;
        }
        CopyRecognition(Input, OutRecord);
        OutRecord.Provider = Name();
        OutRecord.Surface = SurfaceWidgetDesigner;
        return true;
    }

    virtual TSharedPtr<FJsonObject> Build(const FInteractionRecord& Record) const override
    {
        if (!IsOpenAssetEditor(Record.AssetEditor))
        {
            return InvalidTrackedSurface(Record, TEXT("The tracked Widget Blueprint Editor was closed before Context was read."));
        }
        FWidgetBlueprintEditor* Editor = static_cast<FWidgetBlueprintEditor*>(Record.AssetEditor);
        if (!Editor->IsModeCurrent(FName(TEXT("DesignerName"))))
        {
            return InvalidTrackedSurface(Record, TEXT("The Widget Blueprint Editor left Designer mode before Context was read."));
        }
        UWidgetBlueprint* Blueprint = Editor->GetWidgetBlueprintObj();
        if (Blueprint == nullptr)
        {
            return InvalidTrackedSurface(Record, TEXT("The tracked Widget Designer no longer owns a WidgetBlueprint."));
        }
        const TArray<UObject*> EditedAssets = AssetsEditedBy(Editor);
        if (!EditedAssets.Contains(Blueprint)
            || (Record.bRecoveredHostFromWindow && EditedAssets.Num() != 1))
        {
            return InvalidTrackedSurface(
                Record,
                TEXT("The structurally resolved Widget Blueprint Editor does not uniquely own its reported WidgetBlueprint."));
        }

        FContextOutput Out;
        AddSurface(Out, TEXT("Widget Designer"));
        AddRecoveredFocus(Out, Record);

        const TOptional<FNamedSlotSelection> NamedSlot = Editor->GetSelectedNamedSlot();
        if (NamedSlot.IsSet())
        {
            UWidget* Host = NamedSlot->NamedSlotHostWidget.GetTemplate();
            FString HostId;
            if (Host != nullptr && EmitWidgetChain(Out, Blueprint, Host, &HostId) && !HostId.IsEmpty())
            {
                const FString SlotName = NamedSlot->SlotName.ToString();
                if (IsSalPathSegment(SlotName))
                {
                    Out.Builder.AddComment(FString::Printf(
                        TEXT("selected: widget@%s.NamedSlots.%s"),
                        *HostId,
                        *SlotName));
                }
                else
                {
                    Out.Builder.AddComment(FString::Printf(
                        TEXT("selected: named slot\nhost: widget@%s\nmember path: unavailable in SAL identifier syntax\nnative name: %s"),
                        *HostId,
                        *CommentScalar(SlotName)));
                }
            }
            else if (Host == nullptr && !NamedSlot->NamedSlotHostWidget.GetWidgetEditor().IsValid())
            {
                EmitBlueprint(Out, Blueprint);
                Out.Builder.AddComment(FString::Printf(
                    TEXT("selected: inherited named slot\nname: %s\ninterface: unavailable"),
                    *CommentScalar(NamedSlot->SlotName.ToString())));
            }
            else if (Host == nullptr)
            {
                EmitBlueprint(Out, Blueprint);
                Out.Error(
                    TEXT("context.owner_invalid"),
                    TEXT("The selected Named Slot host was reconstructed before Context was read."),
                    NamedSlot->SlotName.ToString());
            }
            return Out.Finish();
        }

        const TSet<FWidgetReference>& Selected = Editor->GetSelectedWidgets();
        if (Selected.IsEmpty())
        {
            EmitBlueprint(Out, Blueprint);
            const TSet<TWeakObjectPtr<UObject>>& SelectedObjects = Editor->GetSelectedObjects();
            if (SelectedObjects.Num() > 1)
            {
                AddMultipleSelection(Out, SelectedObjects.Num());
                return Out.Finish();
            }
            if (SelectedObjects.IsEmpty())
            {
                AddNoSelection(Out);
                return Out.Finish();
            }
            UObject* SelectedObject = SelectedObjects.CreateConstIterator()->Get();
            if (SelectedObject == nullptr)
            {
                Out.Error(
                    TEXT("context.owner_invalid"),
                    TEXT("The selected Widget Designer object was reconstructed before Context was read."));
            }
            else if (SelectedObject == Editor->GetPreview())
            {
                Out.Builder.AddComment(TEXT("selected: preview root\ninterface: unavailable"));
            }
            else
            {
                Out.Builder.AddComment(FString::Printf(
                    TEXT("selected: unsupported Widget Designer object\npath: %s\ntype: %s\ninterface: unavailable"),
                    *CommentScalar(SelectedObject->GetPathName()),
                    *CommentScalar(SelectedObject->GetClass()->GetPathName())));
            }
            return Out.Finish();
        }
        if (Selected.Num() > 1)
        {
            EmitBlueprint(Out, Blueprint);
            AddMultipleSelection(Out, Selected.Num());
            return Out.Finish();
        }

        const FWidgetReference& Reference = *Selected.CreateConstIterator();
        UWidget* Widget = Reference.GetTemplate();
        if (Widget == nullptr)
        {
            EmitBlueprint(Out, Blueprint);
            Out.Error(
                TEXT("context.owner_invalid"),
                TEXT("The selected source Widget was reconstructed before Context was read."));
            return Out.Finish();
        }
        EmitWidgetChain(Out, Blueprint, Widget);
        return Out.Finish();
    }
};

class FContentBrowserProvider final : public IEditorContextProvider
{
public:
    virtual FName Name() const override { return ProviderContentBrowser; }
    virtual int32 Priority() const override { return 900; }

    virtual bool Recognize(const FRecognitionInput& Input, FInteractionRecord& OutRecord) const override
    {
        // SAssetView is also embedded by Asset Picker and Asset Dialog. Only
        // an actual SContentBrowser ancestor or one of the native Content
        // Browser tabs establishes this provider's ownership.
        if (!HasAnyType(Input, {TEXT("SContentBrowser")})
            && !HasAnyPrefix(Input.TabId, {TEXT("ContentBrowserTab"), TEXT("ContentBrowserDrawer")}))
        {
            return false;
        }
        CopyRecognition(Input, OutRecord);
        OutRecord.Provider = Name();
        OutRecord.Surface = SurfaceContentBrowser;
        if (Input.FocusPath != nullptr && Input.FocusPath->IsValid())
        {
            OutRecord.SurfaceWidget = FindWidgetOfType(*Input.FocusPath, FName(TEXT("SAssetView")));
            OutRecord.bHadSurfaceWidget = OutRecord.SurfaceWidget.IsValid();
        }
        return true;
    }

    virtual TSharedPtr<FJsonObject> Build(const FInteractionRecord& Record) const override
    {
        FContextOutput Out;
        AddSurface(Out, TEXT("Content Browser"));
        const TSharedPtr<SWidget> SurfaceWidget = Record.SurfaceWidget.Pin();
        if (Record.bHadSurfaceWidget && !SurfaceWidget.IsValid())
        {
            Out.Error(
                TEXT("context.owner_invalid"),
                TEXT("The tracked Content Browser Asset View was reconstructed before Context was read."));
            return Out.Finish();
        }
        if (!SurfaceWidget.IsValid())
        {
            Out.Builder.AddComment(TEXT(
                "selection: unavailable\n"
                "reason: this Content Browser subview has no public side-effect-free selection API\n"
                "interface: unavailable"));
            return Out.Finish();
        }

        const TSharedPtr<SAssetView> AssetView = StaticCastSharedPtr<SAssetView>(SurfaceWidget);
        const TArray<TSharedPtr<FAssetViewItem>> ViewItems = AssetView->GetSelectedViewItems();
        const int32 Count = ViewItems.Num();
        if (Count == 0)
        {
            AddNoSelection(Out);
            return Out.Finish();
        }
        if (Count > 1)
        {
            AddMultipleSelection(Out, Count);
            return Out.Finish();
        }
        const TSharedPtr<FAssetViewItem>& ViewItem = ViewItems[0];
        if (!ViewItem.IsValid())
        {
            Out.Error(TEXT("context.owner_invalid"), TEXT("The selected Content Browser view item is no longer valid."));
            return Out.Finish();
        }
        const FContentBrowserItem& Item = ViewItem->GetItem();
        if (!Item.IsValid())
        {
            Out.Error(TEXT("context.owner_invalid"), TEXT("The selected Content Browser item is no longer valid."));
            return Out.Finish();
        }
        if (Item.IsTemporary())
        {
            Out.Builder.AddComment(FString::Printf(
                TEXT("selected: temporary Content Browser item\nvirtualPath: %s\ninterface: unavailable\nreason: the item is still being created or renamed"),
                *CommentScalar(Item.GetVirtualPath().ToString())));
            return Out.Finish();
        }
        if (Item.IsFile())
        {
            FAssetData Asset;
            if (Item.Legacy_TryGetAssetData(Asset) && Asset.IsValid())
            {
                EmitAsset(Out, Asset);
                return Out.Finish();
            }
            Out.Builder.AddComment(FString::Printf(
                TEXT("selected: Content Browser file\nvirtualPath: \"%s\"\ninternalPath: \"%s\"\ninterface: unavailable"),
                *Item.GetVirtualPath().ToString(),
                *Item.GetInternalPath().ToString()));
            return Out.Finish();
        }
        if (Item.IsFolder())
        {
            Out.Builder.AddComment(FString::Printf(
                TEXT("selected: folder\nvirtualPath: \"%s\"\ninternalPath: \"%s\"\ninterface: unavailable"),
                *Item.GetVirtualPath().ToString(),
                *Item.GetInternalPath().ToString()));
            return Out.Finish();
        }
        Out.Builder.AddComment(FString::Printf(
            TEXT("selected: Content Browser item\nvirtualPath: \"%s\"\ninterface: unavailable"),
            *Item.GetVirtualPath().ToString()));
        return Out.Finish();
    }
};

FAssetData FindRegisteredWorldAsset(const FSoftObjectPath& Path)
{
    if (!Path.IsValid())
    {
        return FAssetData();
    }
    const FAssetRegistryModule& Module = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
    return Module.Get().GetAssetByObjectPath(Path);
}

FAssetData FindRegisteredWorldAsset(UWorld* World)
{
    if (World == nullptr)
    {
        return FAssetData();
    }
    return FindRegisteredWorldAsset(FSoftObjectPath(World));
}

void EmitWorldOwner(FContextOutput& Out, UWorld* World)
{
    const FAssetData WorldAsset = FindRegisteredWorldAsset(World);
    if (WorldAsset.IsValid())
    {
        EmitAsset(Out, WorldAsset);
    }
    else if (World != nullptr)
    {
        Out.Builder.AddComment(FString::Printf(
            TEXT("map asset: unavailable\npackagePath: \"%s\""),
            *World->GetOutermost()->GetName()));
    }
}

bool EmitAuthoredLevelOwner(FContextOutput& Out, UWorld* EditorWorld, const ULevel* Level)
{
    if (EditorWorld != nullptr && Level != nullptr)
    {
        if (ULevelInstanceSubsystem* LevelInstances = EditorWorld->GetSubsystem<ULevelInstanceSubsystem>())
        {
            if (ILevelInstanceInterface* LevelInstance = LevelInstances->GetOwningLevelInstance(Level))
            {
                const FSoftObjectPath SourcePath = LevelInstance->GetWorldAsset().ToSoftObjectPath();
                const FAssetData SourceAsset = FindRegisteredWorldAsset(SourcePath);
                if (SourceAsset.IsValid())
                {
                    EmitAsset(Out, SourceAsset);
                    return true;
                }
                Out.Builder.AddComment(FString::Printf(
                    TEXT("map asset: unavailable\nsourceWorld: %s"),
                    *CommentScalar(SourcePath.ToString())));
                return false;
            }
        }
    }
    UWorld* OwningWorld = Level != nullptr ? Level->GetTypedOuter<UWorld>() : nullptr;
    const FAssetData WorldAsset = FindRegisteredWorldAsset(OwningWorld);
    if (WorldAsset.IsValid())
    {
        EmitAsset(Out, WorldAsset);
        return true;
    }
    EmitWorldOwner(Out, OwningWorld != nullptr ? OwningWorld : EditorWorld);
    return false;
}

int32 CountActorGuidInLevel(const AActor* Actor)
{
    if (Actor == nullptr || Actor->GetLevel() == nullptr || !Actor->GetActorGuid().IsValid())
    {
        return 0;
    }
    int32 Count = 0;
    for (const AActor* Candidate : Actor->GetLevel()->Actors)
    {
        if (Candidate != nullptr && Candidate->GetActorGuid() == Actor->GetActorGuid())
        {
            ++Count;
        }
    }
    return Count;
}

void EmitActorDescription(FContextOutput& Out, AActor* Actor, const bool bOwnerScopeAvailable)
{
    if (Actor == nullptr)
    {
        return;
    }
    const FGuid ActorGuid = Actor->GetActorGuid();
    const FGuid InstanceGuid = Actor->GetActorInstanceGuid();
    const bool bLevelScopedRef = bOwnerScopeAvailable
        && ActorGuid.IsValid()
        && CountActorGuidInLevel(Actor) == 1;
    TArray<FString> Lines;
    Lines.Add(TEXT("selected: actor"));
    if (ActorGuid.IsValid())
    {
        Lines.Add(FString::Printf(TEXT("actorGuid: %s"), *GuidText(ActorGuid)));
    }
    if (InstanceGuid.IsValid() && InstanceGuid != ActorGuid)
    {
        Lines.Add(FString::Printf(TEXT("actorInstanceGuid: %s"), *GuidText(InstanceGuid)));
    }
    if (bLevelScopedRef)
    {
        Lines.Add(FString::Printf(TEXT("ref: actor@%s"), *GuidText(ActorGuid)));
        Lines.Add(TEXT("scope: owning Level only"));
    }
    Lines.Add(FString::Printf(TEXT("label: %s"), *CommentScalar(Actor->GetActorLabel())));
    Lines.Add(FString::Printf(TEXT("path: \"%s\""), *Actor->GetPathName()));
    Lines.Add(FString::Printf(TEXT("type: \"%s\""), *Actor->GetClass()->GetPathName()));
    Lines.Add(TEXT("interface: unavailable"));
    Lines.Add(bLevelScopedRef
        ? TEXT("graphPaletteUse: requires the exact owning Level Blueprint and Graph target")
        : TEXT("graph palette actor context: unavailable (ActorGuid is missing or not unique in the owning Level)"));
    Out.Builder.AddComment(FString::Join(Lines, TEXT("\n")));
}

void EmitActorComponentDescription(
    FContextOutput& Out,
    UActorComponent* Component,
    const bool bOwnerScopeAvailable)
{
    if (Component == nullptr)
    {
        return;
    }
    AActor* Owner = Component->GetOwner();
    TArray<FString> Lines;
    Lines.Add(TEXT("selected: actor component"));
    if (bOwnerScopeAvailable
        && Owner != nullptr
        && Owner->GetActorGuid().IsValid()
        && CountActorGuidInLevel(Owner) == 1)
    {
        Lines.Add(FString::Printf(TEXT("owner: actor@%s"), *GuidText(Owner->GetActorGuid())));
    }
    else if (Owner != nullptr)
    {
        Lines.Add(FString::Printf(TEXT("ownerPath: \"%s\""), *Owner->GetPathName()));
    }
    Lines.Add(FString::Printf(TEXT("name: %s"), *Component->GetName()));
    Lines.Add(FString::Printf(TEXT("path: \"%s\""), *Component->GetPathName()));
    Lines.Add(FString::Printf(TEXT("type: \"%s\""), *Component->GetClass()->GetPathName()));
    Lines.Add(TEXT("interface: unavailable"));
    Out.Builder.AddComment(FString::Join(Lines, TEXT("\n")));
}

class FLevelEditorProvider final : public IEditorContextProvider
{
public:
    virtual FName Name() const override { return ProviderLevel; }
    virtual int32 Priority() const override { return 800; }

    virtual bool Recognize(const FRecognitionInput& Input, FInteractionRecord& OutRecord) const override
    {
        if (Input.AssetEditor != nullptr)
        {
            return false;
        }
        const bool bLevelTab = Input.TabId == LevelEditorTabIds::LevelEditorViewport
            || Input.TabId == LevelEditorTabIds::LevelEditorViewport_Clone1
            || Input.TabId == LevelEditorTabIds::LevelEditorViewport_Clone2
            || Input.TabId == LevelEditorTabIds::LevelEditorViewport_Clone3
            || Input.TabId == LevelEditorTabIds::LevelEditorViewport_Clone4
            || Input.TabId == LevelEditorTabIds::LevelEditorSceneOutliner
            || Input.TabId == LevelEditorTabIds::LevelEditorSceneOutliner2
            || Input.TabId == LevelEditorTabIds::LevelEditorSceneOutliner3
            || Input.TabId == LevelEditorTabIds::LevelEditorSceneOutliner4
            || Input.TabId == LevelEditorTabIds::LevelEditorSelectionDetails
            || Input.TabId == LevelEditorTabIds::LevelEditorSelectionDetails2
            || Input.TabId == LevelEditorTabIds::LevelEditorSelectionDetails3
            || Input.TabId == LevelEditorTabIds::LevelEditorSelectionDetails4;
        // SSceneOutliner is also used by Actor/Component pickers and custom
        // Outliners. SEditorViewport also applies "LevelEditorViewport" as a
        // generic default tag, so only native Level Editor tabs establish
        // ownership of the global Level selection set.
        if (!bLevelTab)
        {
            return false;
        }
        CopyRecognition(Input, OutRecord);
        OutRecord.Provider = Name();
        OutRecord.Surface = SurfaceLevelEditor;
        return true;
    }

    virtual TSharedPtr<FJsonObject> Build(const FInteractionRecord&) const override
    {
        FContextOutput Out;
        AddSurface(Out, TEXT("Level Editor"));
        if (GEditor == nullptr)
        {
            Out.Error(TEXT("context.owner_invalid"), TEXT("The Level Editor mode manager is unavailable."));
            return Out.Finish();
        }
        UWorld* EditorWorld = GEditor->GetEditorWorldContext().World();
        if (EditorWorld == nullptr)
        {
            Out.Error(TEXT("context.owner_invalid"), TEXT("The Editor World is unavailable."));
            return Out.Finish();
        }
        UTypedElementSelectionSet* Selection = GLevelEditorModeTools().GetEditorSelectionSet();
        if (Selection == nullptr)
        {
            EmitWorldOwner(Out, EditorWorld);
            Out.Error(TEXT("context.owner_invalid"), TEXT("The Level Editor typed selection set is unavailable."));
            return Out.Finish();
        }
        const int32 Count = Selection->GetNumSelectedElements();
        if (Count == 0)
        {
            EmitWorldOwner(Out, EditorWorld);
            AddNoSelection(Out);
            return Out.Finish();
        }
        if (Count > 1)
        {
            EmitWorldOwner(Out, EditorWorld);
            AddMultipleSelection(Out, Count);
            return Out.Finish();
        }

        const TArray<UObject*> Objects = Selection->GetSelectedObjects();
        if (Objects.Num() != 1 || Objects[0] == nullptr)
        {
            EmitWorldOwner(Out, EditorWorld);
            Out.Builder.AddComment(TEXT(
                "selected: typed element\n"
                "native UObject: unavailable\n"
                "interface: unavailable"));
            return Out.Finish();
        }
        UObject* Object = Objects[0];
        if (AActor* Actor = Cast<AActor>(Object))
        {
            const bool bOwnerScopeAvailable = EmitAuthoredLevelOwner(Out, EditorWorld, Actor->GetLevel());
            if (Actor->GetWorld() != EditorWorld)
            {
                Out.Error(TEXT("context.native_inconsistent"), TEXT("The selected Actor is not owned by the Editor World."), Actor->GetPathName());
            }
            else
            {
                EmitActorDescription(Out, Actor, bOwnerScopeAvailable);
            }
            return Out.Finish();
        }
        if (UActorComponent* Component = Cast<UActorComponent>(Object))
        {
            AActor* Owner = Component->GetOwner();
            const bool bOwnerScopeAvailable = EmitAuthoredLevelOwner(
                Out,
                EditorWorld,
                Owner != nullptr ? Owner->GetLevel() : nullptr);
            if (Owner != nullptr && Owner->GetWorld() != EditorWorld)
            {
                Out.Error(TEXT("context.native_inconsistent"), TEXT("The selected Actor Component is not owned by the Editor World."), Component->GetPathName());
            }
            else
            {
                EmitActorComponentDescription(Out, Component, bOwnerScopeAvailable);
            }
            return Out.Finish();
        }

        EmitWorldOwner(Out, EditorWorld);
        Out.Builder.AddComment(FString::Printf(
            TEXT("selected: UObject\npath: \"%s\"\ntype: \"%s\"\ninterface: unavailable"),
            *Object->GetPathName(),
            *Object->GetClass()->GetPathName()));
        return Out.Finish();
    }
};

bool EmitSupportedObject(FContextOutput& Out, UObject* Object)
{
    if (Object == nullptr)
    {
        return false;
    }
    if (UBlueprint* Blueprint = Cast<UBlueprint>(Object))
    {
        EmitBlueprint(Out, Blueprint);
        return true;
    }
    if (UClass* Class = Cast<UClass>(Object))
    {
        EmitClass(Out, Class);
        return true;
    }
    if (UEdGraph* Graph = Cast<UEdGraph>(Object))
    {
        UBlueprint* Blueprint = FBlueprintEditorUtils::FindBlueprintForGraph(Graph);
        if (Blueprint != nullptr)
        {
            const FString BlueprintAlias = EmitBlueprint(Out, Blueprint);
            EmitGraph(Out, Graph, BlueprintAlias);
            return true;
        }
    }
    if (UEdGraphNode* Node = Cast<UEdGraphNode>(Object))
    {
        UEdGraph* Graph = Node->GetGraph();
        UBlueprint* Blueprint = Graph != nullptr ? FBlueprintEditorUtils::FindBlueprintForGraph(Graph) : nullptr;
        if (Blueprint != nullptr)
        {
            const FString BlueprintAlias = EmitBlueprint(Out, Blueprint);
            const FString GraphAlias = EmitGraph(Out, Graph, BlueprintAlias);
            EmitNode(Out, Node, GraphAlias);
            return true;
        }
    }
    if (USCS_Node* Component = Cast<USCS_Node>(Object))
    {
        UBlueprint* Blueprint = Component->GetTypedOuter<UBlueprint>();
        if (Blueprint != nullptr
            && Blueprint->SimpleConstructionScript != nullptr
            && Component->GetOuter() == Blueprint->SimpleConstructionScript)
        {
            EmitComponentChain(Out, Blueprint, Component);
            return true;
        }
    }
    if (UWidget* Widget = Cast<UWidget>(Object))
    {
        UWidgetBlueprint* Blueprint = Widget->GetTypedOuter<UWidgetBlueprint>();
        if (Blueprint != nullptr && EmitWidgetChain(Out, Blueprint, Widget))
        {
            return true;
        }
    }
    if (Object->IsAsset())
    {
        EmitAsset(Out, Object);
        return true;
    }
    return false;
}

void EmitUnsupportedObject(FContextOutput& Out, UObject* Object, UObject* FallbackOwner = nullptr)
{
    UObject* OwnerAsset = FindNearestAsset(Object);
    if (OwnerAsset == nullptr)
    {
        OwnerAsset = FallbackOwner;
    }
    if (OwnerAsset != nullptr && OwnerAsset != Object)
    {
        EmitSupportedObject(Out, OwnerAsset);
    }
    Out.Builder.AddComment(FString::Printf(
        TEXT("selected: unsupported UObject\npath: \"%s\"\ntype: \"%s\"\ninterface: unavailable"),
        Object != nullptr ? *Object->GetPathName() : TEXT("unavailable"),
        Object != nullptr && Object->GetClass() != nullptr ? *Object->GetClass()->GetPathName() : TEXT("unavailable")));
}

class FGenericDetailsProvider final : public IEditorContextProvider
{
public:
    virtual FName Name() const override { return ProviderDetails; }
    virtual int32 Priority() const override { return 700; }

    virtual bool Recognize(const FRecognitionInput& Input, FInteractionRecord& OutRecord) const override
    {
        if (!Input.DetailsView.IsValid())
        {
            return false;
        }
        CopyRecognition(Input, OutRecord);
        OutRecord.Provider = Name();
        OutRecord.Surface = SurfaceDetails;
        return true;
    }

    virtual TSharedPtr<FJsonObject> Build(const FInteractionRecord& Record) const override
    {
        const TSharedPtr<IDetailsView> Details = Record.DetailsView.Pin();
        if (!Details.IsValid())
        {
            return InvalidTrackedSurface(Record, TEXT("The tracked Details View was reconstructed before Context was read."));
        }
        FContextOutput Out;
        AddSurface(Out, TEXT("Details"));
        AddRecoveredFocus(Out, Record);
        const TArray<TWeakObjectPtr<UObject>>& Selected = Details->GetSelectedObjects();
        const auto EmitUniqueEditorOwner = [&Out, &Record]()
        {
            if (!IsOpenAssetEditor(Record.AssetEditor))
            {
                return;
            }
            const TArray<UObject*> Assets = AssetsEditedBy(Record.AssetEditor);
            if (Assets.Num() == 1 && !EmitSupportedObject(Out, Assets[0]))
            {
                EmitAsset(Out, Assets[0]);
            }
        };
        if (Selected.IsEmpty())
        {
            EmitUniqueEditorOwner();
            AddNoSelection(Out);
            return Out.Finish();
        }
        if (Selected.Num() > 1)
        {
            EmitUniqueEditorOwner();
            AddMultipleSelection(Out, Selected.Num());
            return Out.Finish();
        }
        UObject* Object = Selected[0].Get();
        if (Object == nullptr)
        {
            Out.Error(TEXT("context.owner_invalid"), TEXT("The object observed by the tracked Details View is no longer valid."));
        }
        else if (!EmitSupportedObject(Out, Object))
        {
            UObject* FallbackOwner = nullptr;
            if (IsOpenAssetEditor(Record.AssetEditor))
            {
                const TArray<UObject*> Assets = AssetsEditedBy(Record.AssetEditor);
                if (Assets.Num() == 1)
                {
                    FallbackOwner = Assets[0];
                }
            }
            EmitUnsupportedObject(Out, Object, FallbackOwner);
        }
        return Out.Finish();
    }
};

class FGenericAssetEditorProvider final : public IEditorContextProvider
{
public:
    virtual FName Name() const override { return ProviderAssetEditor; }
    virtual int32 Priority() const override { return 600; }

    virtual bool Recognize(const FRecognitionInput& Input, FInteractionRecord& OutRecord) const override
    {
        if (Input.AssetEditor == nullptr)
        {
            return false;
        }
        CopyRecognition(Input, OutRecord);
        OutRecord.Provider = Name();
        OutRecord.Surface = SurfaceAssetEditor;
        return true;
    }

    virtual TSharedPtr<FJsonObject> Build(const FInteractionRecord& Record) const override
    {
        if (!IsOpenAssetEditor(Record.AssetEditor))
        {
            return InvalidTrackedSurface(Record, TEXT("The tracked Asset Editor was closed before Context was read."));
        }
        FContextOutput Out;
        AddSurface(Out, Record.EditorName.IsNone() ? TEXT("Asset Editor") : Record.EditorName.ToString());
        AddRecoveredFocus(Out, Record);
        const TArray<UObject*> Assets = AssetsEditedBy(Record.AssetEditor);
        if (Assets.IsEmpty())
        {
            Out.Error(TEXT("context.owner_invalid"), TEXT("The tracked Asset Editor no longer owns an edited Asset."));
            return Out.Finish();
        }
        if (Assets.Num() > 1)
        {
            Out.Builder.AddComment(FString::Printf(
                TEXT("owner: ambiguous (%d)\nselection: unavailable\nreason: editor has no public active-document API"),
                Assets.Num()));
            return Out.Finish();
        }
        if (!EmitSupportedObject(Out, Assets[0]))
        {
            EmitAsset(Out, Assets[0]);
        }
        Out.Builder.AddComment(FString::Printf(
            TEXT("surface: %s\nselection: unavailable\nprovider: unavailable"),
            Record.TabId.IsNone() ? TEXT("unknown") : *Record.TabId.ToString()));
        return Out.Finish();
    }
};

class FUnknownProvider final : public IEditorContextProvider
{
public:
    virtual FName Name() const override { return ProviderUnknown; }
    virtual int32 Priority() const override { return -1000; }

    virtual bool Recognize(const FRecognitionInput& Input, FInteractionRecord& OutRecord) const override
    {
        CopyRecognition(Input, OutRecord);
        OutRecord.Provider = Name();
        OutRecord.Surface = SurfaceUnknown;
        return true;
    }

    virtual TSharedPtr<FJsonObject> Build(const FInteractionRecord& Record) const override
    {
        FContextOutput Out;
        FString Surface = !Record.TabId.IsNone()
            ? Record.TabId.ToString()
            : !Record.LeafWidgetType.IsNone() ? Record.LeafWidgetType.ToString() : TEXT("unknown");
        FString Comment = FString::Printf(TEXT("surface: %s"), *Surface);
        if (!Record.TabId.IsNone())
        {
            Comment += FString::Printf(TEXT("\ntab: %s"), *Record.TabId.ToString());
        }
        Comment += TEXT("\nselection: unavailable");
        Out.Builder.AddComment(Comment);
        return Out.Finish();
    }
};
const FName ProviderModal(TEXT("modal"));
const FName ProviderBlueprint(TEXT("blueprint"));
const FName ProviderWidget(TEXT("widget"));
const FName ProviderContentBrowser(TEXT("content_browser"));
const FName ProviderLevel(TEXT("level_editor"));
const FName ProviderDetails(TEXT("generic_details"));
const FName ProviderAssetEditor(TEXT("generic_asset_editor"));
const FName ProviderUnknown(TEXT("unknown"));

const FName SurfaceModal(TEXT("modal_dialog"));
const FName SurfaceBlueprintGraph(TEXT("blueprint_graph"));
const FName SurfaceMyBlueprint(TEXT("my_blueprint"));
const FName SurfaceBlueprintComponents(TEXT("blueprint_components"));
const FName SurfaceClassSettings(TEXT("class_settings"));
const FName SurfaceClassDefaults(TEXT("class_defaults"));
const FName SurfaceWidgetDesigner(TEXT("widget_designer"));
const FName SurfaceContentBrowser(TEXT("content_browser"));
const FName SurfaceLevelEditor(TEXT("level_editor"));
const FName SurfaceDetails(TEXT("details"));
const FName SurfaceAssetEditor(TEXT("asset_editor"));
const FName SurfaceUnknown(TEXT("unknown"));

FString GuidText(const FGuid& Guid)
{
    return Guid.ToString(EGuidFormats::DigitsWithHyphensLower);
}

bool IsSalPathSegment(const FString& Text)
{
    const auto IsAsciiAlpha = [](const TCHAR Character)
    {
        return (Character >= TEXT('A') && Character <= TEXT('Z'))
            || (Character >= TEXT('a') && Character <= TEXT('z'));
    };
    const auto IsAsciiDigit = [](const TCHAR Character)
    {
        return Character >= TEXT('0') && Character <= TEXT('9');
    };
    if (Text.IsEmpty() || !(IsAsciiAlpha(Text[0]) || Text[0] == TEXT('_')))
    {
        return false;
    }
    for (const TCHAR Character : Text)
    {
        if (!(IsAsciiAlpha(Character) || IsAsciiDigit(Character) || Character == TEXT('_')))
        {
            return false;
        }
    }
    return true;
}

FString CommentScalar(FString Text)
{
    Text.ReplaceInline(TEXT("\\"), TEXT("\\\\"));
    Text.ReplaceInline(TEXT("\r"), TEXT("\\r"));
    Text.ReplaceInline(TEXT("\n"), TEXT("\\n"));
    Text.ReplaceInline(TEXT("\""), TEXT("\\\""));
    return TEXT("\"") + Text + TEXT("\"");
}

FString CommentPath(const TArray<FString>& Segments)
{
    TArray<FString> Encoded;
    Encoded.Reserve(Segments.Num());
    for (const FString& Segment : Segments)
    {
        Encoded.Add(CommentScalar(Segment));
    }
    return TEXT("[") + FString::Join(Encoded, TEXT(", ")) + TEXT("]");
}

TSharedPtr<FJsonObject> Args()
{
    return MakeShared<FJsonObject>();
}

void AddSurface(FContextOutput& Out, const FString& Surface)
{
    Out.Builder.AddComment(Surface);
}

void AddRecoveredFocus(FContextOutput& Out, const FInteractionRecord& Record)
{
    if (Record.bRecoveredHostFromWindow && !Record.LeafWidgetType.IsNone())
    {
        Out.Builder.AddComment(TEXT("focus: ") + Record.LeafWidgetType.ToString());
    }
}

void AddNoSelection(FContextOutput& Out)
{
    Out.Builder.AddComment(TEXT("selected: none"));
}

void AddMultipleSelection(FContextOutput& Out, const int32 Count)
{
    Out.Builder.AddComment(FString::Printf(
        TEXT("selected: multiple (%d)\nselection target unavailable: multiple selection is not supported"),
        Count));
}

FString GraphTypeText(const UEdGraph* Graph)
{
    const UEdGraphSchema* Schema = Graph != nullptr ? Graph->GetSchema() : nullptr;
    const UEnum* Enum = StaticEnum<EGraphType>();
    return Schema != nullptr && Enum != nullptr
        ? Enum->GetNameStringByValue(Schema->GetGraphType(Graph))
        : FString();
}

FString EmitAsset(
    FContextOutput& Out,
    const FString& PreferredAlias,
    const FString& Path,
    const FString& Type)
{
    const FString Alias = Out.Builder.UniqueAlias(PreferredAlias.IsEmpty() ? TEXT("asset") : PreferredAlias);
    TSharedPtr<FJsonObject> ValueArgs = Args();
    ValueArgs->SetStringField(TEXT("path"), Path);
    if (!Type.IsEmpty())
    {
        ValueArgs->SetStringField(TEXT("type"), Type);
    }
    Out.Builder.AddLocalBinding(Alias, Value::Call(TEXT("asset"), ValueArgs));
    return Alias;
}

FString EmitAsset(FContextOutput& Out, const FAssetData& Asset)
{
    return EmitAsset(
        Out,
        Asset.AssetName.ToString(),
        Asset.GetSoftObjectPath().ToString(),
        Asset.AssetClassPath.ToString());
}

FString EmitAsset(FContextOutput& Out, UObject* Asset)
{
    return Asset != nullptr
        ? EmitAsset(Out, Asset->GetName(), Asset->GetPathName(), Asset->GetClass()->GetPathName())
        : FString();
}

FString EmitBlueprint(FContextOutput& Out, UBlueprint* Blueprint)
{
    if (Blueprint == nullptr)
    {
        return FString();
    }

    const FString Alias = Out.Builder.UniqueAlias(Blueprint->GetName());
    TSharedPtr<FJsonObject> ValueArgs = Args();
    ValueArgs->SetStringField(TEXT("asset"), Blueprint->GetPathName());
    if (Blueprint->GetBlueprintGuid().IsValid())
    {
        ValueArgs->SetStringField(TEXT("id"), GuidText(Blueprint->GetBlueprintGuid()));
    }
    else
    {
        Out.Error(
            TEXT("context.identity_missing"),
            TEXT("The selected Blueprint has no valid BlueprintGuid."),
            Blueprint->GetPathName());
    }
    Out.Builder.AddLocalBinding(Alias, Value::Call(TEXT("blueprint"), ValueArgs));
    return Alias;
}

FString EmitClass(FContextOutput& Out, UClass* Class, const FString& PreferredAlias)
{
    if (Class == nullptr)
    {
        return FString();
    }
    const FString Alias = Out.Builder.UniqueAlias(PreferredAlias.IsEmpty() ? Class->GetName() : PreferredAlias);
    TSharedPtr<FJsonObject> ValueArgs = Args();
    ValueArgs->SetStringField(TEXT("path"), Class->GetPathName());
    Out.Builder.AddLocalBinding(Alias, Value::Call(TEXT("class"), ValueArgs));
    return Alias;
}

FString EmitGraph(
    FContextOutput& Out,
    UEdGraph* Graph,
    const FString& BlueprintAlias,
    const bool bIncludeRecognition)
{
    if (Graph == nullptr || BlueprintAlias.IsEmpty())
    {
        return FString();
    }
    UBlueprint* Blueprint = FBlueprintEditorUtils::FindBlueprintForGraph(Graph);
    if (Blueprint == nullptr)
    {
        Out.Error(
            TEXT("context.native_inconsistent"),
            TEXT("The selected Graph has no owning Blueprint."),
            Graph->GetPathName());
        return FString();
    }
    TArray<UEdGraph*> OwnedGraphs;
    Blueprint->GetAllGraphs(OwnedGraphs);
    if (!OwnedGraphs.Contains(Graph))
    {
        Out.Error(
            TEXT("context.native_inconsistent"),
            TEXT("The selected Graph is not present in its reported Blueprint owner."),
            Graph->GetPathName());
        return FString();
    }
    int32 IdentityMatches = 0;
    for (const UEdGraph* Candidate : OwnedGraphs)
    {
        if (Candidate != nullptr && Candidate->GraphGuid == Graph->GraphGuid)
        {
            ++IdentityMatches;
        }
    }
    if (Graph->GraphGuid.IsValid() && IdentityMatches != 1)
    {
        Out.Error(
            TEXT("context.identity_duplicate"),
            TEXT("The selected GraphGuid is duplicated in its Blueprint owner."),
            GuidText(Graph->GraphGuid));
        return FString();
    }
    const FString Alias = Out.Builder.UniqueAlias(Graph->GetName());
    TSharedPtr<FJsonObject> ValueArgs = Args();
    ValueArgs->SetField(TEXT("asset"), Value::Local(BlueprintAlias));
    if (Graph->GraphGuid.IsValid())
    {
        ValueArgs->SetStringField(TEXT("id"), GuidText(Graph->GraphGuid));
    }
    else
    {
        Out.Error(
            TEXT("context.identity_missing"),
            TEXT("The selected Graph has no valid GraphGuid."),
            Graph->GetPathName());
    }
    if (bIncludeRecognition)
    {
        ValueArgs->SetStringField(TEXT("name"), Graph->GetName());
        const FString GraphType = GraphTypeText(Graph);
        if (!GraphType.IsEmpty())
        {
            ValueArgs->SetField(TEXT("type"), Value::Name(GraphType));
        }
    }
    Out.Builder.AddLocalBinding(Alias, Value::Call(TEXT("graph"), ValueArgs));
    return Alias;
}

void EmitNode(
    FContextOutput& Out,
    UEdGraphNode* Node,
    const FString& GraphAlias)
{
    if (Node == nullptr || GraphAlias.IsEmpty())
    {
        return;
    }
    if (!Node->NodeGuid.IsValid())
    {
        Out.Error(
            TEXT("context.identity_missing"),
            TEXT("The selected Node has no valid NodeGuid."),
            Node->GetPathName());
        return;
    }
    UEdGraph* Graph = Node->GetGraph();
    if (Graph == nullptr || !Graph->Nodes.Contains(Node))
    {
        Out.Error(
            TEXT("context.native_inconsistent"),
            TEXT("The selected Node is not owned by its reported Graph."),
            Node->GetPathName());
        return;
    }
    int32 IdentityMatches = 0;
    for (const UEdGraphNode* Candidate : Graph->Nodes)
    {
        if (Candidate != nullptr && Candidate->NodeGuid == Node->NodeGuid)
        {
            ++IdentityMatches;
        }
    }
    if (IdentityMatches != 1)
    {
        Out.Error(
            TEXT("context.identity_duplicate"),
            TEXT("The selected NodeGuid is duplicated in its owning Graph."),
            GuidText(Node->NodeGuid));
        return;
    }
    const FString Alias = Out.Builder.UniqueAlias(Node->GetName());
    TSharedPtr<FJsonObject> ValueArgs = Args();
    ValueArgs->SetField(TEXT("graph"), Value::Local(GraphAlias));
    ValueArgs->SetStringField(TEXT("id"), GuidText(Node->NodeGuid));
    ValueArgs->SetStringField(TEXT("type"), Node->GetClass()->GetPathName());
    Out.Builder.AddLocalBinding(Alias, Value::Call(TEXT("node"), ValueArgs));
}

UObject* FindNearestAsset(UObject* Object)
{
    for (UObject* Current = Object; Current != nullptr; Current = Current->GetOuter())
    {
        if (Current->IsAsset())
        {
            return Current;
        }
    }
    return nullptr;
}

bool IsOpenAssetEditor(IAssetEditorInstance* Editor)
{
    if (GEditor == nullptr || Editor == nullptr)
    {
        return false;
    }
    UAssetEditorSubsystem* Editors = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>();
    return Editors != nullptr && Editors->GetAllOpenEditors().Contains(Editor);
}

TArray<UObject*> AssetsEditedBy(IAssetEditorInstance* Editor)
{
    TArray<UObject*> Result;
    if (GEditor == nullptr || Editor == nullptr)
    {
        return Result;
    }
    UAssetEditorSubsystem* Editors = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>();
    if (Editors == nullptr)
    {
        return Result;
    }
    for (UObject* Asset : Editors->GetAllEditedAssets())
    {
        if (Asset == nullptr)
        {
            continue;
        }
        for (IAssetEditorInstance* Candidate : Editors->FindEditorsForAsset(Asset))
        {
            if (Candidate == Editor)
            {
                Result.AddUnique(Asset);
                break;
            }
        }
    }
    return Result;
}

IAssetEditorInstance* FindAssetEditorForTab(const TSharedPtr<SDockTab>& Tab)
{
    if (GEditor == nullptr || !Tab.IsValid())
    {
        return nullptr;
    }
    UAssetEditorSubsystem* Editors = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>();
    if (Editors == nullptr)
    {
        return nullptr;
    }
    const TSharedPtr<FTabManager> OwningManager = Tab->GetTabManagerPtr();
    TArray<IAssetEditorInstance*> OwnerMatches;
    TArray<IAssetEditorInstance*> ManagerMatches;
    for (IAssetEditorInstance* Editor : Editors->GetAllOpenEditors())
    {
        if (Editor == nullptr)
        {
            continue;
        }
        const TSharedPtr<FTabManager> EditorManager = Editor->GetAssociatedTabManager();
        if (!EditorManager.IsValid())
        {
            continue;
        }
        if (EditorManager->GetOwnerTab() == Tab)
        {
            OwnerMatches.AddUnique(Editor);
        }
        if (EditorManager == OwningManager)
        {
            ManagerMatches.AddUnique(Editor);
        }
    }
    if (OwnerMatches.Num() == 1)
    {
        return OwnerMatches[0];
    }
    if (!OwnerMatches.IsEmpty())
    {
        return nullptr;
    }
    if (ManagerMatches.Num() == 1)
    {
        return ManagerMatches[0];
    }
    // A world-centric Asset Editor shares the Level Editor TabManager with
    // native Level tabs. Manager identity, even with one hosted toolkit, does
    // not prove that this exact DockTab belongs to that toolkit. Without a
    // toolkit-specific structural owner signal, leave the association unknown.
    return nullptr;
}

IAssetEditorInstance* FindAssetEditorForWindow(
    const TSharedPtr<SWindow>& Window,
    TSharedPtr<SDockTab>& OutOwnerTab)
{
    OutOwnerTab.Reset();
    if (GEditor == nullptr
        || !Window.IsValid()
        || Window->GetType() != EWindowType::Normal)
    {
        return nullptr;
    }

    const TSharedRef<FGlobalTabmanager> GlobalManager = FGlobalTabmanager::Get();
    const TSharedPtr<FTabManager> WindowManager = GlobalManager->GetSubTabManagerForWindow(Window.ToSharedRef());
    if (!WindowManager.IsValid())
    {
        return nullptr;
    }
    const TSharedPtr<SDockTab> OwnerTab = WindowManager->GetOwnerTab();
    const TSharedPtr<SDockTab> RegisteredMajorTab = GlobalManager->GetMajorTabForTabManager(WindowManager.ToSharedRef());
    const TSharedPtr<SWindow> OwnerWindow = OwnerTab.IsValid() ? OwnerTab->GetParentWindow() : nullptr;
    // UE resolves either the owner/root window or a Docking Area owned by the
    // manager. Rejecting the owner window leaves only the exact auxiliary
    // Docking Area path; the root-window branch is not structurally unique.
    if (!OwnerTab.IsValid()
        || OwnerTab != RegisteredMajorTab
        || OwnerTab->GetTabRole() != ETabRole::MajorTab
        || !OwnerTab->IsForeground()
        || !OwnerWindow.IsValid()
        || OwnerWindow == Window)
    {
        return nullptr;
    }

    UAssetEditorSubsystem* Editors = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>();
    if (Editors == nullptr)
    {
        return nullptr;
    }
    TArray<IAssetEditorInstance*> Matches;
    for (IAssetEditorInstance* Editor : Editors->GetAllOpenEditors())
    {
        if (Editor != nullptr
            && Editor->GetAssociatedTabManager() == WindowManager)
        {
            Matches.AddUnique(Editor);
        }
    }
    if (Matches.Num() != 1 || AssetsEditedBy(Matches[0]).Num() != 1)
    {
        return nullptr;
    }

    OutOwnerTab = OwnerTab;
    return Matches[0];
}

IAssetEditorInstance* ResolveTrackedAssetEditor(const FInteractionRecord& Record)
{
    if (Record.AssetEditor == nullptr || !Record.bHadTab)
    {
        return nullptr;
    }
    const TSharedPtr<SDockTab> Tab = Record.Tab.Pin();
    if (!Tab.IsValid())
    {
        return nullptr;
    }
    IAssetEditorInstance* Resolved = FindAssetEditorForTab(Tab);
    if (Resolved == nullptr
        || Resolved != Record.AssetEditor
        || Resolved->GetEditorName() != Record.EditorName)
    {
        return nullptr;
    }
    if (Record.bRecoveredHostFromWindow)
    {
        const TSharedPtr<FTabManager> Manager = Resolved->GetAssociatedTabManager();
        if (!Manager.IsValid()
            || Manager->GetOwnerTab() != Tab
            || Tab->GetTabRole() != ETabRole::MajorTab
            || !Tab->IsForeground()
            || AssetsEditedBy(Resolved).Num() != 1)
        {
            return nullptr;
        }
    }
    return Resolved;
}

TSharedPtr<SDockTab> FindDockTab(const FWidgetPath& Path)
{
    for (int32 Index = Path.Widgets.Num() - 1; Index >= 0; --Index)
    {
        const TSharedRef<SWidget>& Widget = Path.Widgets[Index].Widget;
        if (Widget->GetType() == FName(TEXT("SDockTab")))
        {
            return StaticCastSharedRef<SDockTab>(Widget).ToSharedPtr();
        }
    }
    return nullptr;
}

TSharedPtr<IDetailsView> FindDetailsView(const FWidgetPath& Path)
{
    for (int32 Index = Path.Widgets.Num() - 1; Index >= 0; --Index)
    {
        const TSharedRef<SWidget>& Widget = Path.Widgets[Index].Widget;
        if (Widget->GetType() == FName(TEXT("SDetailsView")))
        {
            return StaticCastSharedRef<IDetailsView>(Widget).ToSharedPtr();
        }
    }
    return nullptr;
}

TSharedPtr<SWidget> FindWidgetOfType(const FWidgetPath& Path, const FName Type)
{
    for (int32 Index = Path.Widgets.Num() - 1; Index >= 0; --Index)
    {
        const TSharedRef<SWidget>& Widget = Path.Widgets[Index].Widget;
        if (Widget->GetType() == Type)
        {
            return Widget.ToSharedPtr();
        }
    }
    return nullptr;
}

void CollectStructuralPath(const FWidgetPath& Path, TArray<FName>& OutTags, TArray<FName>& OutTypes)
{
    for (int32 Index = 0; Index < Path.Widgets.Num(); ++Index)
    {
        const FArrangedWidget& Arranged = Path.Widgets[Index];
        const TSharedRef<SWidget>& Widget = Arranged.Widget;
        OutTypes.AddUnique(Widget->GetType());
        for (const TSharedRef<FTagMetaData>& Tag : Widget->GetAllMetaData<FTagMetaData>())
        {
            OutTags.AddUnique(Tag->Tag);
        }
    }
}

bool HasAnyPrefix(const FName Value, std::initializer_list<const TCHAR*> Prefixes)
{
    const FString Text = Value.ToString();
    for (const TCHAR* Prefix : Prefixes)
    {
        if (Text.StartsWith(Prefix, ESearchCase::CaseSensitive))
        {
            return true;
        }
    }
    return false;
}

bool HasAnyType(const FRecognitionInput& Input, std::initializer_list<const TCHAR*> Types)
{
    for (const TCHAR* Type : Types)
    {
        if (Input.HasWidgetType(FName(Type)))
        {
            return true;
        }
    }
    return false;
}

bool HasAnyTag(const FRecognitionInput& Input, std::initializer_list<const TCHAR*> Tags)
{
    for (const TCHAR* Tag : Tags)
    {
        if (Input.HasTag(FName(Tag)))
        {
            return true;
        }
    }
    return false;
}

void CopyRecognition(const FRecognitionInput& Input, FInteractionRecord& Out)
{
    Out.TabId = Input.TabId;
    Out.EditorName = Input.EditorName;
    Out.Tab = Input.ActiveTab;
    Out.DetailsView = Input.DetailsView;
    Out.AssetEditor = Input.AssetEditor;
    Out.bHadTab = Input.ActiveTab.IsValid();
    Out.bHadFocusPath = Input.FocusPath != nullptr && Input.FocusPath->IsValid();
    Out.bRecoveredHostFromWindow = Input.bRecoveredHostFromWindow;
    Out.LeafWidgetType = Input.FocusPath != nullptr && Input.FocusPath->IsValid()
        ? Input.FocusPath->Widgets[Input.FocusPath->Widgets.Num() - 1].Widget->GetType()
        : NAME_None;
}

TSharedPtr<FJsonObject> InvalidTrackedSurface(const FInteractionRecord& Record, const FString& Message)
{
    FContextOutput Out;
    Out.Builder.AddComment(FString::Printf(
        TEXT("surface: %s\nselection: unavailable"),
        Record.Surface.IsNone() ? TEXT("unknown") : *Record.Surface.ToString()));
    Out.Error(TEXT("context.owner_invalid"), Message, Record.TabId.ToString());
    return Out.Finish();
}
}

bool FRecognitionInput::HasTag(const FName Tag) const
{
    return Tags.Contains(Tag);
}

bool FRecognitionInput::HasWidgetType(const FName Type) const
{
    return WidgetTypes.Contains(Type);
}

class FEditorContextService::FImpl
{
public:
    FImpl()
    {
        RegisterProvider(MakeShared<FModalProvider>());
        RegisterProvider(MakeShared<FBlueprintProvider>());
        RegisterProvider(MakeShared<FWidgetDesignerProvider>());
        RegisterProvider(MakeShared<FContentBrowserProvider>());
        RegisterProvider(MakeShared<FLevelEditorProvider>());
        RegisterProvider(MakeShared<FGenericDetailsProvider>());
        RegisterProvider(MakeShared<FGenericAssetEditorProvider>());
        RegisterProvider(MakeShared<FUnknownProvider>());
    }

    void Startup()
    {
        if (bStarted || !FSlateApplication::IsInitialized())
        {
            return;
        }
        bStarted = true;
        FocusChangingHandle = FSlateApplication::Get().OnFocusChanging().AddRaw(
            this,
            &FImpl::HandleFocusChanging);
        ActiveTabChangedHandle = FGlobalTabmanager::Get()->OnActiveTabChanged_Subscribe(
            FOnActiveTabChanged::FDelegate::CreateRaw(this, &FImpl::HandleActiveTabChanged));
        TabForegroundedHandle = FGlobalTabmanager::Get()->OnTabForegrounded_Subscribe(
            FOnActiveTabChanged::FDelegate::CreateRaw(this, &FImpl::HandleTabForegrounded));

        if (!ObserveCurrentFocus())
        {
            Observe(nullptr, FGlobalTabmanager::Get()->GetActiveTab());
        }
    }

    void Shutdown()
    {
        if (!bStarted)
        {
            return;
        }
        if (FSlateApplication::IsInitialized())
        {
            FSlateApplication::Get().OnFocusChanging().Remove(FocusChangingHandle);
            FGlobalTabmanager::Get()->OnActiveTabChanged_Unsubscribe(ActiveTabChangedHandle);
            FGlobalTabmanager::Get()->OnTabForegrounded_Unsubscribe(TabForegroundedHandle);
        }
        FocusChangingHandle.Reset();
        ActiveTabChangedHandle.Reset();
        TabForegroundedHandle.Reset();
        bStarted = false;
        bHasRecord = false;
        Current = FInteractionRecord();
    }

    void RegisterProvider(TSharedRef<IEditorContextProvider> Provider)
    {
        Providers.RemoveAll([&Provider](const TSharedRef<IEditorContextProvider>& Existing)
        {
            return Existing->Name() == Provider->Name();
        });
        Providers.Add(Provider);
        Providers.Sort([](
            const TSharedRef<IEditorContextProvider>& Left,
            const TSharedRef<IEditorContextProvider>& Right)
        {
            return Left->Priority() > Right->Priority();
        });
    }

    void UnregisterProvider(const FName Name)
    {
        Providers.RemoveAll([Name](const TSharedRef<IEditorContextProvider>& Existing)
        {
            return Existing->Name() == Name;
        });
        if (bHasRecord && Current.Provider == Name)
        {
            bHasRecord = false;
            Current = FInteractionRecord();
        }
    }

    TSharedPtr<FJsonObject> BuildResult()
    {
        if (!bStarted)
        {
            Startup();
        }
        const bool bModalActive = FSlateApplication::IsInitialized()
            && FSlateApplication::Get().GetActiveModalWindow().IsValid();
        if (bModalActive)
        {
            FRecognitionInput Input;
            Input.bModal = true;
            for (const TSharedRef<IEditorContextProvider>& Provider : Providers)
            {
                FInteractionRecord ModalRecord;
                if (Provider->Recognize(Input, ModalRecord))
                {
                    return Validate(Provider->Build(ModalRecord));
                }
            }
        }
        const bool bObservedCurrentFocus = ObserveCurrentFocus();
        if (!bObservedCurrentFocus
            && FSlateApplication::IsInitialized()
            && bHasRecord
            && Current.bHadTab)
        {
            const TSharedPtr<SDockTab> TrackedTab = Current.Tab.Pin();
            const TSharedPtr<SDockTab> ActiveTab = FGlobalTabmanager::Get()->GetActiveTab();
            const bool bParticipatesInGlobalActiveTab = TrackedTab.IsValid()
                && TrackedTab->GetVisualTabRole() != ETabRole::MajorTab;
            if (bParticipatesInGlobalActiveTab && ActiveTab.IsValid() && TrackedTab != ActiveTab)
            {
                // Tab delegates are the primary signal. Re-checking exact
                // identity here closes any event-ordering gap without falling
                // back from a closed tracked Tab to an unrelated editor.
                // Major Tabs are intentionally excluded by UE's own global
                // active-tab tracker, so comparing one to the last minor Tab
                // would erase a real foreground interaction.
                Observe(nullptr, ActiveTab);
            }
        }
        if (!bHasRecord && FSlateApplication::IsInitialized())
        {
            Observe(nullptr, FGlobalTabmanager::Get()->GetActiveTab());
        }
        if (!bHasRecord)
        {
            FContextOutput Out;
            Out.Builder.AddComment(TEXT("surface: unknown\nselection: unavailable"));
            return Out.Finish();
        }
        if (Current.bHadTab && !Current.Tab.IsValid())
        {
            return InvalidTrackedSurface(Current, TEXT("The tracked DockTab was closed before Context was read."));
        }
        FInteractionRecord ValidatedRecord = Current;
        if (Current.AssetEditor != nullptr)
        {
            IAssetEditorInstance* ResolvedEditor = ResolveTrackedAssetEditor(Current);
            if (ResolvedEditor == nullptr)
            {
                return InvalidTrackedSurface(
                    Current,
                    TEXT("The tracked Asset Editor is no longer owned by the recorded DockTab."));
            }
            ValidatedRecord.AssetEditor = ResolvedEditor;
        }
        for (const TSharedRef<IEditorContextProvider>& Provider : Providers)
        {
            if (Provider->Name() == Current.Provider)
            {
                return Validate(Provider->Build(ValidatedRecord));
            }
        }
        return InvalidTrackedSurface(Current, TEXT("The Provider that recognized the tracked surface is no longer registered."));
    }

    bool IsStarted() const
    {
        return bStarted;
    }

private:
    bool ObserveCurrentFocus()
    {
        if (!FSlateApplication::IsInitialized()
            || FSlateApplication::Get().GetActiveModalWindow().IsValid())
        {
            return false;
        }
        const TSharedPtr<SWidget> FocusedWidget = FSlateApplication::Get().GetKeyboardFocusedWidget();
        if (!FocusedWidget.IsValid())
        {
            return false;
        }
        FWidgetPath FocusPath;
        if (!FSlateApplication::Get().GeneratePathToWidgetUnchecked(FocusedWidget.ToSharedRef(), FocusPath)
            || !FocusPath.IsValid())
        {
            return false;
        }
        const TSharedRef<SWindow> FocusWindow = FocusPath.GetDeepestWindow();
        if (FocusWindow->GetType() != EWindowType::Normal
            || !FocusWindow->IsVisible()
            || FocusWindow->IsWindowMinimized())
        {
            return false;
        }
        const TSharedPtr<SDockTab> FocusTab = FindDockTab(FocusPath);
        if (FocusTab.IsValid() && !FocusTab->IsForeground())
        {
            return false;
        }
        Observe(&FocusPath, nullptr);
        return true;
    }

    FRecognitionInput MakeInput(const FWidgetPath* Path, TSharedPtr<SDockTab> Tab) const
    {
        FRecognitionInput Input;
        Input.FocusPath = Path;
        Input.bModal = FSlateApplication::IsInitialized()
            && FSlateApplication::Get().GetActiveModalWindow().IsValid();
        if (Path != nullptr && Path->IsValid())
        {
            const TSharedPtr<SDockTab> PathTab = FindDockTab(*Path);
            if (PathTab.IsValid())
            {
                Tab = PathTab;
            }
            else
            {
                TSharedPtr<SDockTab> RecoveredOwnerTab;
                Input.AssetEditor = FindAssetEditorForWindow(Path->GetDeepestWindow(), RecoveredOwnerTab);
                if (Input.AssetEditor != nullptr)
                {
                    Tab = RecoveredOwnerTab;
                    Input.bRecoveredHostFromWindow = true;
                }
            }
            CollectStructuralPath(*Path, Input.Tags, Input.WidgetTypes);
            Input.DetailsView = FindDetailsView(*Path);
        }
        Input.ActiveTab = Tab;
        if (Tab.IsValid())
        {
            Input.TabId = Tab->GetLayoutIdentifier().TabType;
        }
        if (Input.AssetEditor == nullptr)
        {
            Input.AssetEditor = FindAssetEditorForTab(Tab);
        }
        if (Input.AssetEditor != nullptr)
        {
            Input.EditorName = Input.AssetEditor->GetEditorName();
        }
        return Input;
    }

    void Observe(const FWidgetPath* Path, const TSharedPtr<SDockTab>& Tab)
    {
        const FRecognitionInput Input = MakeInput(Path, Tab);
        if (Path == nullptr && Tab.IsValid())
        {
            if (!FSlateApplication::IsInitialized())
            {
                return;
            }
            const TSharedPtr<SWindow> ActiveWindow = FSlateApplication::Get().GetActiveTopLevelRegularWindow();
            const TSharedPtr<SWindow> TabWindow = Tab->GetParentWindow();
            if (!ActiveWindow.IsValid()
                || !TabWindow.IsValid()
                || TabWindow != ActiveWindow
                || !Tab->IsForeground()
                || !TabWindow->IsVisible()
                || TabWindow->IsWindowMinimized())
            {
                // Local tab wells can foreground tabs in a background window.
                // A pathless event is interaction evidence only in Slate's
                // active regular window.
                return;
            }
            if (Input.AssetEditor != nullptr)
            {
                const TSharedPtr<FTabManager> EditorManager = Input.AssetEditor->GetAssociatedTabManager();
                const TSharedPtr<SDockTab> OwnerTab = EditorManager.IsValid()
                    ? EditorManager->GetOwnerTab()
                    : nullptr;
                const TSharedPtr<SDockTab> RegisteredMajorTab = EditorManager.IsValid()
                    ? FGlobalTabmanager::Get()->GetMajorTabForTabManager(EditorManager.ToSharedRef())
                    : nullptr;
                if (!OwnerTab.IsValid()
                    || OwnerTab != RegisteredMajorTab
                    || !OwnerTab->IsForeground())
                {
                    return;
                }
            }
        }
        if (Path == nullptr && bHasRecord && Current.bHadFocusPath && Tab.IsValid())
        {
            const TSharedPtr<SDockTab> CurrentTab = Current.Tab.Pin();
            const bool bSameTab = CurrentTab == Tab && Current.AssetEditor == Input.AssetEditor;
            bool bOwnerMajorTabForSameEditor = false;
            if (Current.AssetEditor != nullptr
                && Current.AssetEditor == Input.AssetEditor
                && Tab->GetTabRole() == ETabRole::MajorTab
                && Tab->IsForeground())
            {
                const TSharedPtr<FTabManager> EditorManager = Input.AssetEditor->GetAssociatedTabManager();
                bOwnerMajorTabForSameEditor = EditorManager.IsValid() && EditorManager->GetOwnerTab() == Tab;
            }
            if (bSameTab || bOwnerMajorTabForSameEditor)
            {
                // A pathless activation broadcast is weaker than the current
                // Focus Path only when it repeats that exact Tab or foregrounds
                // the same Editor's owner Major Tab. A different minor Tab is
                // a real surface change even when its content takes no focus.
                return;
            }
        }
        for (const TSharedRef<IEditorContextProvider>& Provider : Providers)
        {
            FInteractionRecord Candidate;
            if (Provider->Recognize(Input, Candidate))
            {
                Current = MoveTemp(Candidate);
                bHasRecord = true;
                return;
            }
        }
    }

    void HandleFocusChanging(
        const FFocusEvent&,
        const FWeakWidgetPath&,
        const TSharedPtr<SWidget>&,
        const FWidgetPath& NewPath,
        const TSharedPtr<SWidget>&)
    {
        if (!NewPath.IsValid())
        {
            return;
        }
        if (FSlateApplication::Get().GetActiveModalWindow().IsValid())
        {
            // BuildResult suppresses the retained context dynamically while
            // the Modal is active. Keeping the pre-Modal record avoids losing
            // an exact SAssetView, Details View, or Major-Tab interaction when
            // the window closes.
            return;
        }
        const EWindowType WindowType = NewPath.GetDeepestWindow()->GetType();
        if (WindowType == EWindowType::Menu
            || WindowType == EWindowType::ToolTip
            || WindowType == EWindowType::Notification
            || WindowType == EWindowType::CursorDecorator)
        {
            // Popups and menus inherit the surface that opened them. Tooltips
            // and notifications never become an interaction source.
            return;
        }
        Observe(&NewPath, nullptr);
    }

    void HandleActiveTabChanged(
        TSharedPtr<SDockTab> PreviouslyActive,
        TSharedPtr<SDockTab> NewlyActivated)
    {
        // UE 5.7's FGlobalTabmanager::SetActiveTab broadcasts (previous, new),
        // despite the delegate's historical parameter comments.
        (void)PreviouslyActive;
        if (NewlyActivated.IsValid())
        {
            Observe(nullptr, NewlyActivated);
        }
    }

    void HandleTabForegrounded(
        TSharedPtr<SDockTab> NewlyForegrounded,
        TSharedPtr<SDockTab> PreviouslyForegrounded)
    {
        (void)PreviouslyForegrounded;
        if (NewlyForegrounded.IsValid())
        {
            Observe(nullptr, NewlyForegrounded);
        }
    }

    TSharedPtr<FJsonObject> Validate(const TSharedPtr<FJsonObject>& Result) const
    {
        TSharedPtr<FJsonObject> ValidationError;
        if (FSalJson::ValidateResult(Result, ValidationError))
        {
            return Result;
        }
        FContextOutput Out;
        Out.Builder.AddComment(TEXT("surface: unavailable\nselection: unavailable"));
        Out.Error(
            TEXT("context.invalid_result"),
            TEXT("Editor Context produced an invalid normalized SAL ObjectResult."));
        return Out.Finish();
    }

private:
    TArray<TSharedRef<IEditorContextProvider>> Providers;
    FInteractionRecord Current;
    FDelegateHandle FocusChangingHandle;
    FDelegateHandle ActiveTabChangedHandle;
    FDelegateHandle TabForegroundedHandle;
    bool bStarted = false;
    bool bHasRecord = false;
};

FEditorContextService& FEditorContextService::Get()
{
    static FEditorContextService Instance;
    return Instance;
}

FEditorContextService::FEditorContextService()
    : Impl(MakeUnique<FImpl>())
{
}

FEditorContextService::~FEditorContextService()
{
    Shutdown();
}

void FEditorContextService::Startup()
{
    Impl->Startup();
}

void FEditorContextService::Shutdown()
{
    Impl->Shutdown();
}

bool FEditorContextService::IsStarted() const
{
    return Impl->IsStarted();
}

void FEditorContextService::RegisterProvider(TSharedRef<IEditorContextProvider> Provider)
{
    Impl->RegisterProvider(MoveTemp(Provider));
}

void FEditorContextService::UnregisterProvider(const FName ProviderName)
{
    Impl->UnregisterProvider(ProviderName);
}

TSharedPtr<FJsonObject> FEditorContextService::BuildResult()
{
    return Impl->BuildResult();
}
}
