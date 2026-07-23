// Copyright 2026 Loomle contributors.

#if WITH_DEV_AUTOMATION_TESTS

#include "Sal/Blueprint/SalBlueprintInterface.h"
#include "Sal/Widget/SalWidgetInterface.h"
#include "Tests/LoomleTestEditorState.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetBlueprintGeneratedClass.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Overlay.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Editor.h"
#include "HAL/FileManager.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/AutomationTest.h"
#include "Misc/PackageName.h"
#include "PackageTools.h"
#include "Sal/SalModel.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"
#include "WidgetBlueprint.h"

namespace
{
using namespace Loomle::Sal;

FString RobustWidgetGuidText(const FGuid& Guid)
{
    return Guid.ToString(EGuidFormats::DigitsWithHyphensLower);
}

TSharedRef<FJsonObject> RobustWidgetRef(
    const FString& Kind,
    const FString& Identity)
{
    TSharedRef<FJsonObject> Ref = MakeShared<FJsonObject>();
    Ref->SetStringField(TEXT("kind"), Kind);
    if (Kind == TEXT("local"))
    {
        Ref->SetStringField(TEXT("name"), Identity);
    }
    else
    {
        Ref->SetStringField(TEXT("id"), Identity);
    }
    return Ref;
}

TSharedRef<FJsonObject> RobustWidgetMember(
    const TSharedRef<FJsonObject>& Owner,
    std::initializer_list<const TCHAR*> Path)
{
    TSharedRef<FJsonObject> Member = MakeShared<FJsonObject>();
    Member->SetStringField(TEXT("kind"), TEXT("member"));
    Member->SetObjectField(TEXT("object"), Owner);
    TArray<TSharedPtr<FJsonValue>> Segments;
    for (const TCHAR* Segment : Path)
    {
        Segments.Add(MakeShared<FJsonValueString>(Segment));
    }
    Member->SetArrayField(TEXT("path"), MoveTemp(Segments));
    return Member;
}

TSharedPtr<FJsonValue> RobustWidgetDeclaration(
    const FString& Alias,
    const FString& PaletteId)
{
    TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
    Args->SetStringField(TEXT("palette"), PaletteId);
    TSharedRef<FJsonObject> Call = MakeShared<FJsonObject>();
    Call->SetStringField(TEXT("kind"), TEXT("call"));
    Call->SetStringField(TEXT("callee"), TEXT("widget"));
    Call->SetObjectField(TEXT("args"), Args);
    TSharedRef<FJsonObject> Binding = MakeShared<FJsonObject>();
    Binding->SetObjectField(
        TEXT("target"),
        RobustWidgetRef(TEXT("local"), Alias));
    Binding->SetObjectField(TEXT("value"), Call);
    return MakeShared<FJsonValueObject>(Binding);
}

TSharedRef<FJsonObject> RobustWidgetOperation(const FString& Kind)
{
    TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetStringField(TEXT("kind"), Kind);
    return Result;
}

FSalPatch RobustWidgetPatch(
    TArray<TSharedPtr<FJsonValue>> Statements,
    const bool bDryRun = false)
{
    FSalPatch Patch;
    Patch.Alias = TEXT("menu");
    Patch.bDryRun = bDryRun;
    Patch.Statements = MoveTemp(Statements);
    return Patch;
}

FSalQuery RobustWidgetQuery(const FString& Kind)
{
    FSalQuery Query;
    Query.Alias = TEXT("menu");
    Query.Operation = MakeShared<FJsonObject>();
    Query.Operation->SetStringField(TEXT("kind"), Kind);
    return Query;
}

bool RobustWidgetResultBool(
    const TSharedPtr<FJsonObject>& Result,
    const TCHAR* Field,
    const bool Default = false)
{
    bool Value = Default;
    return Result.IsValid() && Result->TryGetBoolField(Field, Value)
        ? Value
        : Default;
}

bool RobustWidgetHasError(const TSharedPtr<FJsonObject>& Result)
{
    const TArray<TSharedPtr<FJsonValue>>* Diagnostics = nullptr;
    if (!Result.IsValid()
        || !Result->TryGetArrayField(TEXT("diagnostics"), Diagnostics)
        || Diagnostics == nullptr)
    {
        return true;
    }
    for (const TSharedPtr<FJsonValue>& Value : *Diagnostics)
    {
        const TSharedPtr<FJsonObject>* Diagnostic = nullptr;
        FString Severity;
        if (Value.IsValid()
            && Value->TryGetObject(Diagnostic)
            && Diagnostic != nullptr
            && (*Diagnostic)->TryGetStringField(TEXT("severity"), Severity)
            && Severity == TEXT("error"))
        {
            return true;
        }
    }
    return false;
}

TArray<TSharedPtr<FJsonObject>> RobustWidgetCalls(
    const TSharedPtr<FJsonObject>& Result,
    const FString& Callee)
{
    TArray<TSharedPtr<FJsonObject>> Args;
    const TSharedPtr<FJsonObject>* Object = nullptr;
    const TArray<TSharedPtr<FJsonValue>>* Statements = nullptr;
    if (!Result.IsValid()
        || !Result->TryGetObjectField(TEXT("object"), Object)
        || Object == nullptr
        || !(*Object)->TryGetArrayField(TEXT("statements"), Statements)
        || Statements == nullptr)
    {
        return Args;
    }
    for (const TSharedPtr<FJsonValue>& Value : *Statements)
    {
        const TSharedPtr<FJsonObject>* Statement = nullptr;
        const TSharedPtr<FJsonObject>* Call = nullptr;
        const TSharedPtr<FJsonObject>* CallArgs = nullptr;
        FString Actual;
        if (Value.IsValid()
            && Value->TryGetObject(Statement)
            && Statement != nullptr
            && (*Statement)->TryGetObjectField(TEXT("value"), Call)
            && Call != nullptr
            && (*Call)->TryGetStringField(TEXT("callee"), Actual)
            && Actual == Callee
            && (*Call)->TryGetObjectField(TEXT("args"), CallArgs)
            && CallArgs != nullptr)
        {
            Args.Add(*CallArgs);
        }
    }
    return Args;
}

int32 RobustWidgetCallCountById(
    const TSharedPtr<FJsonObject>& Result,
    const FString& Callee,
    const FString& Id)
{
    int32 Count = 0;
    for (const TSharedPtr<FJsonObject>& Args :
         RobustWidgetCalls(Result, Callee))
    {
        FString ActualId;
        if (Args.IsValid()
            && Args->TryGetStringField(TEXT("id"), ActualId)
            && ActualId.Equals(Id, ESearchCase::IgnoreCase))
        {
            ++Count;
        }
    }
    return Count;
}

bool RobustWidgetHasComment(
    const TSharedPtr<FJsonObject>& Result,
    const FString& Needle)
{
    const TSharedPtr<FJsonObject>* Object = nullptr;
    const TArray<TSharedPtr<FJsonValue>>* Statements = nullptr;
    if (!Result.IsValid()
        || !Result->TryGetObjectField(TEXT("object"), Object)
        || Object == nullptr
        || !(*Object)->TryGetArrayField(TEXT("statements"), Statements)
        || Statements == nullptr)
    {
        return false;
    }
    for (const TSharedPtr<FJsonValue>& Value : *Statements)
    {
        const TSharedPtr<FJsonObject>* Statement = nullptr;
        FString Kind;
        FString Text;
        if (Value.IsValid()
            && Value->TryGetObject(Statement)
            && Statement != nullptr
            && (*Statement)->TryGetStringField(TEXT("kind"), Kind)
            && Kind == TEXT("comment")
            && (*Statement)->TryGetStringField(TEXT("text"), Text)
            && Text.Contains(Needle))
        {
            return true;
        }
    }
    return false;
}

bool RobustWidgetUnloadPackage(
    UPackage* Package,
    FString& OutError)
{
    OutError.Reset();
    if (Package == nullptr)
    {
        return true;
    }

    Package->SetDirtyFlag(false);
    TArray<UPackage*> Packages = {Package};
    UPackageTools::FUnloadPackageParams Params(Packages);
    Params.bUnloadDirtyPackages = true;
    // Fixture mutations use a disposable transactor that has already been
    // restored. Preserve the Editor's real Undo/Redo history.
    Params.bResetTransBuffer = false;
    const bool bUnloaded = UPackageTools::UnloadPackages(Params);
    if (!bUnloaded)
    {
        OutError = Params.OutErrorMessage.IsEmpty()
            ? TEXT("UE package unload did not unload the Widget fixture.")
            : Params.OutErrorMessage.ToString();
    }
    return bUnloaded;
}

class FRobustWidgetFixture
{
public:
    ~FRobustWidgetFixture()
    {
        FString Ignored;
        Cleanup(Ignored);
    }

    bool Initialize(FString& OutError, const bool bIncludeDetached = false)
    {
        const FString Token =
            FGuid::NewGuid().ToString(EGuidFormats::Digits);
        PackageName = FString::Printf(
            TEXT("/Game/LoomleTests/WidgetRobust/%s/WBP_Robust"),
            *Token);
        ObjectPath = PackageName + TEXT(".WBP_Robust");
        Filename = FPackageName::LongPackageNameToFilename(
            PackageName,
            FPackageName::GetAssetPackageExtension());
        IFileManager::Get().MakeDirectory(
            *FPackageName::LongPackageNameToFilename(
                FPackageName::GetLongPackagePath(PackageName)),
            true);

        Package = CreatePackage(*PackageName);
        Blueprint = Package != nullptr
            ? Cast<UWidgetBlueprint>(
                FKismetEditorUtilities::CreateBlueprint(
                    UUserWidget::StaticClass(),
                    Package,
                    FName(TEXT("WBP_Robust")),
                    BPTYPE_Normal,
                    UWidgetBlueprint::StaticClass(),
                    UWidgetBlueprintGeneratedClass::StaticClass(),
                    NAME_None))
            : nullptr;
        if (Blueprint == nullptr || Blueprint->WidgetTree == nullptr)
        {
            OutError = TEXT("UE could not create the robust Widget fixture.");
            return false;
        }
        FAssetRegistryModule::AssetCreated(Blueprint);
        bRegistered = true;
        Blueprint->WidgetTree->SetFlags(RF_Transactional);

        Root = Blueprint->WidgetTree->ConstructWidget<UCanvasPanel>(
            UCanvasPanel::StaticClass(),
            RootName);
        Primary = Blueprint->WidgetTree->ConstructWidget<UButton>(
            UButton::StaticClass(),
            PrimaryName);
        Secondary = Blueprint->WidgetTree->ConstructWidget<UButton>(
            UButton::StaticClass(),
            SecondaryName);
        Nested = Blueprint->WidgetTree->ConstructWidget<UVerticalBox>(
            UVerticalBox::StaticClass(),
            NestedName);
        Deeper = Blueprint->WidgetTree->ConstructWidget<UOverlay>(
            UOverlay::StaticClass(),
            DeeperName);
        DeepLeaf = Blueprint->WidgetTree->ConstructWidget<UButton>(
            UButton::StaticClass(),
            DeepLeafName);
        if (Root == nullptr
            || Primary == nullptr
            || Secondary == nullptr
            || Nested == nullptr
            || Deeper == nullptr
            || DeepLeaf == nullptr)
        {
            OutError = TEXT("UE could not construct robust Widget objects.");
            return false;
        }
        Blueprint->WidgetTree->RootWidget = Root;
        if (Root->AddChild(Primary) == nullptr
            || Root->AddChild(Secondary) == nullptr
            || Root->AddChild(Nested) == nullptr
            || Nested->AddChild(Deeper) == nullptr
            || Deeper->AddChild(DeepLeaf) == nullptr)
        {
            OutError = TEXT("UE could not author the robust Widget hierarchy.");
            return false;
        }
        for (const FName Name : {
                 RootName,
                 PrimaryName,
                 SecondaryName,
                 NestedName,
                 DeeperName,
                 DeepLeafName})
        {
            Blueprint->OnVariableAdded(Name);
        }
        FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
        FKismetEditorUtilities::CompileBlueprint(Blueprint);
        if (Blueprint->Status == BS_Error
            || Blueprint->GeneratedClass == nullptr)
        {
            OutError = TEXT("Robust Widget fixture did not compile.");
            return false;
        }
        RefreshPointers();
        if (bIncludeDetached)
        {
            // UE compilation removes source Widgets that are not enumerated by
            // WidgetTree::GetAllWidgets. Model the detached editing state only
            // after compiling the valid authored hierarchy.
            Detached = Blueprint->WidgetTree->ConstructWidget<UTextBlock>(
                UTextBlock::StaticClass(),
                DetachedName);
            if (Detached == nullptr)
            {
                OutError = TEXT("UE could not construct the detached Widget.");
                return false;
            }
            DetachedGuard.Reset(Detached);
            Blueprint->OnVariableAdded(DetachedName);
        }
        PrimaryId = GuidForName(PrimaryName);
        SecondaryId = GuidForName(SecondaryName);
        NestedId = GuidForName(NestedName);
        DetachedId = GuidForName(DetachedName);
        if (!PrimaryId.IsValid()
            || !SecondaryId.IsValid()
            || !NestedId.IsValid()
            || (bIncludeDetached && !DetachedId.IsValid()))
        {
            OutError = TEXT("Robust Widget fixture has incomplete stable ids.");
            return false;
        }
        Package->SetDirtyFlag(false);
        return true;
    }

    FSalResolvedTarget Target() const
    {
        FSalResolvedTarget Target;
        Target.Kind = ESalTargetKind::Blueprint;
        Target.Alias = TEXT("menu");
        Target.AssetPath = Blueprint != nullptr
            ? Blueprint->GetPathName()
            : ObjectPath;
        Target.Id = Blueprint != nullptr
            ? RobustWidgetGuidText(Blueprint->GetBlueprintGuid())
            : FString();
        Target.Object = Blueprint;
        Target.Package = Blueprint != nullptr
            ? Blueprint->GetOutermost()
            : nullptr;
        Target.Blueprint = Blueprint;
        Target.Class = Blueprint != nullptr
            ? Blueprint->GeneratedClass.Get()
            : nullptr;
        Target.Interfaces = {
            FName(TEXT("asset")),
            FName(TEXT("blueprint")),
            FName(TEXT("widget"))};
        return Target;
    }

    FGuid GuidForName(const FName Name) const
    {
        return Blueprint != nullptr
            ? Blueprint->WidgetVariableNameToGuidMap.FindRef(Name)
            : FGuid();
    }

    UWidget* WidgetForId(const FGuid& Id) const
    {
        if (Blueprint == nullptr || Blueprint->WidgetTree == nullptr)
        {
            return nullptr;
        }
        for (const TPair<FName, FGuid>& Pair :
             Blueprint->WidgetVariableNameToGuidMap)
        {
            if (Pair.Value == Id)
            {
                return Blueprint->WidgetTree->FindWidget(Pair.Key);
            }
        }
        return nullptr;
    }

    bool UnloadForReload(FString& OutError)
    {
        if (Package == nullptr)
        {
            OutError = TEXT("Robust Widget fixture has no Package to unload.");
            return false;
        }
        UPackage* PackageToUnload = Package;
        Blueprint = nullptr;
        Package = nullptr;
        Root = nullptr;
        Primary = nullptr;
        Secondary = nullptr;
        Nested = nullptr;
        Deeper = nullptr;
        DeepLeaf = nullptr;
        Detached = nullptr;
        DetachedGuard.Reset();
        if (!RobustWidgetUnloadPackage(PackageToUnload, OutError))
        {
            return false;
        }
        if (FindPackage(nullptr, *PackageName) != nullptr
            || FindObject<UObject>(nullptr, *ObjectPath) != nullptr)
        {
            OutError = TEXT("Saved robust Widget Package remained loaded.");
            return false;
        }
        OutError.Reset();
        return true;
    }

    bool AdoptReloaded(UWidgetBlueprint* Reloaded)
    {
        Blueprint = Reloaded;
        Package = Reloaded != nullptr
            ? Reloaded->GetOutermost()
            : nullptr;
        RefreshPointers();
        return Blueprint != nullptr && Blueprint->WidgetTree != nullptr;
    }

    bool Cleanup(FString& OutError)
    {
        OutError.Reset();
        if (bCleaned)
        {
            return true;
        }
        bCleaned = true;
        if (Blueprint == nullptr && !ObjectPath.IsEmpty())
        {
            Blueprint = FindObject<UWidgetBlueprint>(nullptr, *ObjectPath);
        }
        if (bRegistered && Blueprint != nullptr)
        {
            FAssetRegistryModule::AssetDeleted(Blueprint);
            bRegistered = false;
        }
        UPackage* LoadedPackage = Blueprint != nullptr
            ? Blueprint->GetOutermost()
            : (!PackageName.IsEmpty()
                ? FindPackage(nullptr, *PackageName)
                : nullptr);
        Blueprint = nullptr;
        Package = nullptr;
        Root = nullptr;
        Primary = nullptr;
        Secondary = nullptr;
        Nested = nullptr;
        Deeper = nullptr;
        DeepLeaf = nullptr;
        Detached = nullptr;
        DetachedGuard.Reset();
        FString UnloadError;
        if (!RobustWidgetUnloadPackage(LoadedPackage, UnloadError))
        {
            OutError = UnloadError;
        }
        if (!PackageName.IsEmpty()
            && FindPackage(nullptr, *PackageName) != nullptr)
        {
            if (!OutError.IsEmpty())
            {
                OutError += TEXT(" ");
            }
            OutError += TEXT("Widget fixture Package remained loaded.");
        }
        if (!Filename.IsEmpty()
            && IFileManager::Get().FileExists(*Filename)
            && !IFileManager::Get().Delete(*Filename, false, true, true))
        {
            OutError += TEXT("Widget fixture file could not be deleted.");
        }
        return OutError.IsEmpty();
    }

    void RefreshPointers()
    {
        if (Blueprint == nullptr || Blueprint->WidgetTree == nullptr)
        {
            return;
        }
        Root = Cast<UCanvasPanel>(
            Blueprint->WidgetTree->FindWidget(RootName));
        Primary = Cast<UButton>(
            Blueprint->WidgetTree->FindWidget(PrimaryName));
        Secondary = Cast<UButton>(
            Blueprint->WidgetTree->FindWidget(SecondaryName));
        Nested = Cast<UVerticalBox>(
            Blueprint->WidgetTree->FindWidget(NestedName));
        Deeper = Cast<UOverlay>(
            Blueprint->WidgetTree->FindWidget(DeeperName));
        DeepLeaf = Cast<UButton>(
            Blueprint->WidgetTree->FindWidget(DeepLeafName));
        Detached = Cast<UTextBlock>(
            Blueprint->WidgetTree->FindWidget(DetachedName));
        DetachedGuard.Reset(Detached);
    }

    static const FName RootName;
    static const FName PrimaryName;
    static const FName SecondaryName;
    static const FName NestedName;
    static const FName DeeperName;
    static const FName DeepLeafName;
    static const FName DetachedName;

    FString PackageName;
    FString ObjectPath;
    FString Filename;
    UPackage* Package = nullptr;
    UWidgetBlueprint* Blueprint = nullptr;
    UCanvasPanel* Root = nullptr;
    UButton* Primary = nullptr;
    UButton* Secondary = nullptr;
    UVerticalBox* Nested = nullptr;
    UOverlay* Deeper = nullptr;
    UButton* DeepLeaf = nullptr;
    UTextBlock* Detached = nullptr;
    TStrongObjectPtr<UTextBlock> DetachedGuard;
    FGuid PrimaryId;
    FGuid SecondaryId;
    FGuid NestedId;
    FGuid DetachedId;

private:
    bool bRegistered = false;
    bool bCleaned = false;
};

const FName FRobustWidgetFixture::RootName(TEXT("RootCanvas"));
const FName FRobustWidgetFixture::PrimaryName(TEXT("PrimaryAction"));
const FName FRobustWidgetFixture::SecondaryName(TEXT("SecondaryAction"));
const FName FRobustWidgetFixture::NestedName(TEXT("NestedStack"));
const FName FRobustWidgetFixture::DeeperName(TEXT("NestedOverlay"));
const FName FRobustWidgetFixture::DeepLeafName(TEXT("DeepAction"));
const FName FRobustWidgetFixture::DetachedName(TEXT("DetachedLabel"));

FString RobustWidgetClassPalette(const UClass* Class)
{
    return Class != nullptr
        ? TEXT("widget.class:") + Class->GetPathName()
        : FString();
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FSalWidgetRobustQueryPaletteTest,
    "Loomle.Sal.Widget.Robust.QueryPaletteDepthDetached",
    EAutomationTestFlags::EditorContext
        | EAutomationTestFlags::EngineFilter)

bool FSalWidgetRobustQueryPaletteTest::RunTest(
    const FString& Parameters)
{
    (void)Parameters;
    FRobustWidgetFixture Fixture;
    FString Error;
    if (!TestTrue(TEXT("Robust Widget fixture initializes"), Fixture.Initialize(Error, true)))
    {
        AddError(Error);
        return false;
    }
    const FSalResolvedTarget Target = Fixture.Target();

    FSalQuery Shallow = RobustWidgetQuery(TEXT("tree"));
    Shallow.Operation->SetNumberField(TEXT("depth"), 1);
    const TSharedPtr<FJsonObject> ShallowResult =
        FSalWidgetInterface::Query(Shallow, Target);
    TestFalse(TEXT("Shallow Widget tree succeeds"), RobustWidgetHasError(ShallowResult));
    TestTrue(
        TEXT("Shallow Widget tree reports its truncation boundary"),
        RobustWidgetHasComment(ShallowResult, TEXT("truncated: widget@")));

    FSalQuery Widgets = RobustWidgetQuery(TEXT("widgets"));
    Widgets.Operation->SetStringField(TEXT("text"), TEXT("Detached"));
    const TSharedPtr<FJsonObject> DetachedResult =
        FSalWidgetInterface::Query(Widgets, Target);
    TestFalse(TEXT("Detached Widget discovery succeeds"), RobustWidgetHasError(DetachedResult));
    TestEqual(
        TEXT("Detached Widget search returns one exact source object"),
        RobustWidgetCalls(DetachedResult, TEXT("widget")).Num(),
        1);
    TestTrue(
        TEXT("Detached Widget result explains that it is outside the reachable tree"),
        RobustWidgetHasComment(DetachedResult, TEXT("detached source Widget")));

    FSalQuery Palette = RobustWidgetQuery(TEXT("palette"));
    Palette.Operation->SetStringField(
        TEXT("id"),
        RobustWidgetClassPalette(UButton::StaticClass()));
    Palette.With = {TEXT("schema")};
    const TSharedPtr<FJsonObject> PaletteResult =
        FSalWidgetInterface::Query(Palette, Target);
    TestFalse(TEXT("Exact Widget Palette schema succeeds"), RobustWidgetHasError(PaletteResult));
    TestEqual(
        TEXT("Exact Widget Palette returns one copyable constructor"),
        RobustWidgetCalls(PaletteResult, TEXT("widget")).Num(),
        1);
    TestTrue(
        TEXT("Exact Widget Palette schema explains materialization operations"),
        RobustWidgetHasComment(PaletteResult, TEXT("materialization operations:")));

    FSalQuery Exact = RobustWidgetQuery(TEXT("widget"));
    Exact.Operation->SetStringField(
        TEXT("id"),
        RobustWidgetGuidText(Fixture.PrimaryId));
    Exact.With = {TEXT("schema")};
    const TSharedPtr<FJsonObject> ExactResult =
        FSalWidgetInterface::Query(Exact, Target);
    TestFalse(TEXT("Exact Widget schema succeeds"), RobustWidgetHasError(ExactResult));
    TestTrue(
        TEXT("Exact Widget schema exposes native Canvas Slot fields"),
        RobustWidgetHasComment(ExactResult, TEXT("Slot.ZOrder")));

    FString CleanupError;
    TestTrue(TEXT("Robust Widget Query fixture cleans up"), Fixture.Cleanup(CleanupError));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FSalWidgetRobustPanelLifecycleTest,
    "Loomle.Sal.Widget.Robust.PanelPlacementSlotLifecycle",
    EAutomationTestFlags::EditorContext
        | EAutomationTestFlags::EngineFilter)

bool FSalWidgetRobustPanelLifecycleTest::RunTest(
    const FString& Parameters)
{
    (void)Parameters;
    if (GEditor == nullptr)
    {
        AddError(TEXT("Robust Widget lifecycle requires GEditor."));
        return false;
    }
    Loomle::Tests::FScopedIsolatedTransactor Transactions;
    if (!Transactions.Initialize())
    {
        AddError(TEXT("Robust Widget lifecycle could not isolate transactions."));
        return false;
    }
    FRobustWidgetFixture Fixture;
    FString Error;
    if (!Fixture.Initialize(Error))
    {
        AddError(Error);
        Transactions.Restore();
        return false;
    }
    const FSalResolvedTarget Target = Fixture.Target();

    const auto AddBeforeSecondary = [&](const bool bDryRun)
    {
        TSharedRef<FJsonObject> Add = RobustWidgetOperation(TEXT("add"));
        Add->SetObjectField(
            TEXT("target"),
            RobustWidgetRef(TEXT("local"), TEXT("AddedAction")));
        Add->SetObjectField(
            TEXT("before"),
            RobustWidgetRef(
                TEXT("widget"),
                RobustWidgetGuidText(Fixture.SecondaryId)));
        return RobustWidgetPatch(
            {
                RobustWidgetDeclaration(
                    TEXT("AddedAction"),
                    RobustWidgetClassPalette(UButton::StaticClass())),
                MakeShared<FJsonValueObject>(Add)
            },
            bDryRun);
    };

    const int32 BeforeCount = Fixture.Root->GetChildrenCount();
    const TSharedPtr<FJsonObject> DryRun =
        FSalWidgetInterface::Patch(
            AddBeforeSecondary(true),
            Target);
    TestTrue(TEXT("Widget add dry-run validates"), RobustWidgetResultBool(DryRun, TEXT("valid")));
    TestFalse(TEXT("Widget add dry-run does not apply"), RobustWidgetResultBool(DryRun, TEXT("applied")));
    TestEqual(TEXT("Widget add dry-run preserves native child count"), Fixture.Root->GetChildrenCount(), BeforeCount);

    const TSharedPtr<FJsonObject> Added =
        FSalWidgetInterface::Patch(
            AddBeforeSecondary(false),
            Target);
    TestTrue(TEXT("Widget add validates"), RobustWidgetResultBool(Added, TEXT("valid")));
    TestTrue(TEXT("Widget add applies"), RobustWidgetResultBool(Added, TEXT("applied")));
    UWidget* AddedWidget = Fixture.Blueprint->WidgetTree->FindWidget(
        FName(TEXT("AddedAction")));
    const FGuid AddedId = Fixture.GuidForName(FName(TEXT("AddedAction")));
    TestNotNull(TEXT("Widget add materializes its exact alias"), AddedWidget);
    TestTrue(TEXT("Widget add registers a stable Widget id"), AddedId.IsValid());
    TestEqual(
        TEXT("Widget add inserts before the selected sibling"),
        Fixture.Root->GetChildIndex(AddedWidget),
        Fixture.Root->GetChildIndex(Fixture.Secondary) - 1);

    TSharedRef<FJsonObject> Set = RobustWidgetOperation(TEXT("set"));
    Set->SetObjectField(
        TEXT("target"),
        RobustWidgetMember(
            RobustWidgetRef(
                TEXT("widget"),
                RobustWidgetGuidText(AddedId)),
            {TEXT("Slot"), TEXT("ZOrder")}));
    Set->SetStringField(TEXT("value"), TEXT("7"));
    const TSharedPtr<FJsonObject> SetResult =
        FSalWidgetInterface::Patch(
            RobustWidgetPatch({MakeShared<FJsonValueObject>(Set)}),
            Target);
    UCanvasPanelSlot* OriginalSlot =
        Cast<UCanvasPanelSlot>(AddedWidget != nullptr ? AddedWidget->Slot : nullptr);
    TestTrue(TEXT("Widget Slot set applies"), RobustWidgetResultBool(SetResult, TEXT("applied")));
    TestEqual(TEXT("Widget Slot set reaches native ZOrder"), OriginalSlot != nullptr ? OriginalSlot->GetZOrder() : INDEX_NONE, 7);

    TSharedRef<FJsonObject> Move = RobustWidgetOperation(TEXT("move"));
    Move->SetObjectField(
        TEXT("target"),
        RobustWidgetRef(TEXT("widget"), RobustWidgetGuidText(AddedId)));
    Move->SetObjectField(
        TEXT("after"),
        RobustWidgetRef(
            TEXT("widget"),
            RobustWidgetGuidText(Fixture.SecondaryId)));
    const TSharedPtr<FJsonObject> MoveResult =
        FSalWidgetInterface::Patch(
            RobustWidgetPatch({MakeShared<FJsonValueObject>(Move)}),
            Target);
    AddedWidget = Fixture.WidgetForId(AddedId);
    TestTrue(TEXT("Widget sibling move applies"), RobustWidgetResultBool(MoveResult, TEXT("applied")));
    UCanvasPanelSlot* MovedSlot = AddedWidget != nullptr
        ? Cast<UCanvasPanelSlot>(AddedWidget->Slot.Get())
        : nullptr;
    TestEqual(
        TEXT("Same-Panel move preserves the native Slot object"),
        AddedWidget != nullptr ? AddedWidget->Slot.Get() : nullptr,
        static_cast<UPanelSlot*>(OriginalSlot));
    TestEqual(
        TEXT("Same-Panel move preserves native Slot state"),
        MovedSlot != nullptr ? MovedSlot->GetZOrder() : INDEX_NONE,
        7);

    TSharedRef<FJsonObject> Reset = RobustWidgetOperation(TEXT("reset"));
    Reset->SetObjectField(
        TEXT("target"),
        RobustWidgetMember(
            RobustWidgetRef(
                TEXT("widget"),
                RobustWidgetGuidText(AddedId)),
            {TEXT("Slot"), TEXT("ZOrder")}));
    const TSharedPtr<FJsonObject> ResetResult =
        FSalWidgetInterface::Patch(
            RobustWidgetPatch({MakeShared<FJsonValueObject>(Reset)}),
            Target);
    TestTrue(TEXT("Widget Slot reset applies"), RobustWidgetResultBool(ResetResult, TEXT("applied")));
    TestEqual(
        TEXT("Widget Slot reset restores the native CDO value"),
        MovedSlot != nullptr ? MovedSlot->GetZOrder() : INDEX_NONE,
        GetDefault<UCanvasPanelSlot>()->GetZOrder());

    TSharedRef<FJsonObject> Remove = RobustWidgetOperation(TEXT("remove"));
    Remove->SetObjectField(
        TEXT("target"),
        RobustWidgetRef(TEXT("widget"), RobustWidgetGuidText(AddedId)));
    const TSharedPtr<FJsonObject> RemoveResult =
        FSalWidgetInterface::Patch(
            RobustWidgetPatch({MakeShared<FJsonValueObject>(Remove)}),
            Target);
    TestTrue(TEXT("Widget remove applies"), RobustWidgetResultBool(RemoveResult, TEXT("applied")));
    TestNull(TEXT("Widget remove deletes the exact source object"), Fixture.WidgetForId(AddedId));
    TestFalse(
        TEXT("Widget remove clears the stable GUID record"),
        Fixture.Blueprint->WidgetVariableNameToGuidMap.Contains(
            FName(TEXT("AddedAction"))));
    TestTrue(TEXT("Widget remove is one undoable native transaction"), GEditor->UndoTransaction(false));
    TestNotNull(TEXT("Undo restores the removed Widget"), Fixture.WidgetForId(AddedId));

    Transactions.Restore();
    FString CleanupError;
    TestTrue(TEXT("Robust Widget lifecycle fixture cleans up"), Fixture.Cleanup(CleanupError));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FSalWidgetRobustCompoundOperationsTest,
    "Loomle.Sal.Widget.Robust.WrapRenameDuplicateReplace",
    EAutomationTestFlags::EditorContext
        | EAutomationTestFlags::EngineFilter)

bool FSalWidgetRobustCompoundOperationsTest::RunTest(
    const FString& Parameters)
{
    (void)Parameters;
    if (GEditor == nullptr)
    {
        AddError(TEXT("Robust Widget compound operations require GEditor."));
        return false;
    }
    Loomle::Tests::FScopedIsolatedTransactor Transactions;
    if (!Transactions.Initialize())
    {
        return false;
    }
    FRobustWidgetFixture Fixture;
    FString Error;
    if (!Fixture.Initialize(Error))
    {
        AddError(Error);
        Transactions.Restore();
        return false;
    }
    const FSalResolvedTarget Target = Fixture.Target();

    TSharedRef<FJsonObject> Wrap = RobustWidgetOperation(TEXT("wrap"));
    Wrap->SetArrayField(
        TEXT("targets"),
        {
            MakeShared<FJsonValueObject>(
                RobustWidgetRef(
                    TEXT("widget"),
                    RobustWidgetGuidText(Fixture.PrimaryId))),
            MakeShared<FJsonValueObject>(
                RobustWidgetRef(
                    TEXT("widget"),
                    RobustWidgetGuidText(Fixture.SecondaryId)))
        });
    Wrap->SetObjectField(
        TEXT("with"),
        RobustWidgetRef(TEXT("local"), TEXT("ActionGroup")));
    const TSharedPtr<FJsonObject> Wrapped =
        FSalWidgetInterface::Patch(
            RobustWidgetPatch(
                {
                    RobustWidgetDeclaration(
                        TEXT("ActionGroup"),
                        RobustWidgetClassPalette(UVerticalBox::StaticClass())),
                    MakeShared<FJsonValueObject>(Wrap)
                }),
            Target);
    UVerticalBox* Wrapper = Cast<UVerticalBox>(
        Fixture.Blueprint->WidgetTree->FindWidget(
            FName(TEXT("ActionGroup"))));
    TestTrue(TEXT("Widget wrap applies"), RobustWidgetResultBool(Wrapped, TEXT("applied")));
    TestNotNull(TEXT("Widget wrap materializes one Panel"), Wrapper);
    TestEqual(TEXT("Widget wrap preserves explicit target count"), Wrapper != nullptr ? Wrapper->GetChildrenCount() : INDEX_NONE, 2);
    TestEqual(TEXT("Widget wrap preserves first child identity"), Fixture.WidgetForId(Fixture.PrimaryId), Wrapper != nullptr ? Wrapper->GetChildAt(0) : nullptr);
    TestEqual(TEXT("Widget wrap preserves second child identity"), Fixture.WidgetForId(Fixture.SecondaryId), Wrapper != nullptr ? Wrapper->GetChildAt(1) : nullptr);
    TestTrue(TEXT("Widget wrap is one undoable native transaction"), GEditor->UndoTransaction(false));
    Fixture.RefreshPointers();
    TestEqual(
        TEXT("Undo restores direct sibling placement"),
        Fixture.Primary != nullptr
            ? Fixture.Primary->GetParent()
            : nullptr,
        static_cast<UPanelWidget*>(Fixture.Root));

    TSharedRef<FJsonObject> Rename = RobustWidgetOperation(TEXT("invoke"));
    Rename->SetObjectField(
        TEXT("target"),
        RobustWidgetRef(
            TEXT("widget"),
            RobustWidgetGuidText(Fixture.PrimaryId)));
    Rename->SetStringField(TEXT("operation"), TEXT("Rename"));
    TSharedRef<FJsonObject> RenameArgs = MakeShared<FJsonObject>();
    RenameArgs->SetStringField(TEXT("displayName"), TEXT("Primary Action Renamed"));
    Rename->SetObjectField(TEXT("args"), RenameArgs);
    Rename->SetArrayField(TEXT("outputs"), {});
    const TSharedPtr<FJsonObject> Renamed =
        FSalWidgetInterface::Patch(
            RobustWidgetPatch({MakeShared<FJsonValueObject>(Rename)}),
            Target);
    UWidget* RenamedWidget = Fixture.WidgetForId(Fixture.PrimaryId);
    TestTrue(TEXT("Widget Rename applies"), RobustWidgetResultBool(Renamed, TEXT("applied")));
    TestNotNull(TEXT("Widget Rename preserves stable identity"), RenamedWidget);
    TestTrue(
        TEXT("Widget Rename changes the authored object name"),
        RenamedWidget != nullptr
            && RenamedWidget->GetFName() != FRobustWidgetFixture::PrimaryName);

    TSet<UWidget*> BeforeDuplicate;
    for (int32 Index = 0; Index < Fixture.Root->GetChildrenCount(); ++Index)
    {
        BeforeDuplicate.Add(Fixture.Root->GetChildAt(Index));
    }
    TSharedRef<FJsonObject> Duplicate = RobustWidgetOperation(TEXT("invoke"));
    Duplicate->SetObjectField(
        TEXT("target"),
        RobustWidgetRef(
            TEXT("widget"),
            RobustWidgetGuidText(Fixture.PrimaryId)));
    Duplicate->SetStringField(TEXT("operation"), TEXT("Duplicate"));
    Duplicate->SetObjectField(TEXT("args"), MakeShared<FJsonObject>());
    TSharedRef<FJsonObject> Output = MakeShared<FJsonObject>();
    Output->SetStringField(TEXT("alias"), TEXT("ActionCopy"));
    Duplicate->SetArrayField(
        TEXT("outputs"),
        {MakeShared<FJsonValueObject>(Output)});
    const TSharedPtr<FJsonObject> Duplicated =
        FSalWidgetInterface::Patch(
            RobustWidgetPatch({MakeShared<FJsonValueObject>(Duplicate)}),
            Target);
    UWidget* Copy = nullptr;
    for (int32 Index = 0; Index < Fixture.Root->GetChildrenCount(); ++Index)
    {
        UWidget* Candidate = Fixture.Root->GetChildAt(Index);
        if (!BeforeDuplicate.Contains(Candidate))
        {
            Copy = Candidate;
            break;
        }
    }
    const FGuid CopyId = Copy != nullptr
        ? Fixture.GuidForName(Copy->GetFName())
        : FGuid();
    TestTrue(TEXT("Widget Duplicate applies"), RobustWidgetResultBool(Duplicated, TEXT("applied")));
    TestNotNull(TEXT("Widget Duplicate creates one new sibling root"), Copy);
    TestTrue(TEXT("Widget Duplicate registers a distinct stable id"), CopyId.IsValid() && CopyId != Fixture.PrimaryId);

    TSharedRef<FJsonObject> Replace = RobustWidgetOperation(TEXT("replace"));
    Replace->SetObjectField(
        TEXT("target"),
        RobustWidgetRef(TEXT("widget"), RobustWidgetGuidText(CopyId)));
    Replace->SetObjectField(
        TEXT("with"),
        RobustWidgetRef(TEXT("local"), TEXT("BorderReplacement")));
    const TSharedPtr<FJsonObject> Replaced =
        FSalWidgetInterface::Patch(
            RobustWidgetPatch(
                {
                    RobustWidgetDeclaration(
                        TEXT("BorderReplacement"),
                        RobustWidgetClassPalette(UBorder::StaticClass())),
                    MakeShared<FJsonValueObject>(Replace)
                }),
            Target);
    UWidget* Replacement = Fixture.WidgetForId(CopyId);
    TestTrue(TEXT("Widget replace applies"), RobustWidgetResultBool(Replaced, TEXT("applied")));
    TestTrue(
        TEXT("Palette replacement preserves target stable identity while changing native type"),
        Replacement != nullptr
            && Replacement->IsA<UBorder>());
    TestNotNull(
        TEXT("Original renamed Widget remains addressable after neighboring compound edits"),
        Fixture.WidgetForId(Fixture.PrimaryId));

    Transactions.Restore();
    FString CleanupError;
    TestTrue(TEXT("Robust Widget compound fixture cleans up"), Fixture.Cleanup(CleanupError));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FSalWidgetRobustPersistenceTest,
    "Loomle.Sal.Widget.Robust.CompileSaveReloadIdentity",
    EAutomationTestFlags::EditorContext
        | EAutomationTestFlags::EngineFilter)

bool FSalWidgetRobustPersistenceTest::RunTest(
    const FString& Parameters)
{
    (void)Parameters;
    if (GEditor == nullptr || GEditor->IsPlaySessionInProgress())
    {
        AddError(TEXT("Widget persistence requires an Editor outside PIE."));
        return false;
    }
    Loomle::Tests::FScopedIsolatedTransactor Transactions;
    if (!Transactions.Initialize())
    {
        return false;
    }
    FRobustWidgetFixture Fixture;
    FString Error;
    if (!Fixture.Initialize(Error))
    {
        AddError(Error);
        Transactions.Restore();
        return false;
    }
    const FGuid BlueprintId = Fixture.Blueprint->GetBlueprintGuid();
    const FGuid WidgetId = Fixture.PrimaryId;

    TSharedRef<FJsonObject> Rename = RobustWidgetOperation(TEXT("invoke"));
    Rename->SetObjectField(
        TEXT("target"),
        RobustWidgetRef(TEXT("widget"), RobustWidgetGuidText(WidgetId)));
    Rename->SetStringField(TEXT("operation"), TEXT("Rename"));
    TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
    Args->SetStringField(TEXT("displayName"), TEXT("Persisted Primary"));
    Rename->SetObjectField(TEXT("args"), Args);
    Rename->SetArrayField(TEXT("outputs"), {});
    const TSharedPtr<FJsonObject> Mutation =
        FSalWidgetInterface::Patch(
            RobustWidgetPatch({MakeShared<FJsonValueObject>(Rename)}),
            Fixture.Target());
    TestTrue(TEXT("Persisted Widget mutation applies"), RobustWidgetResultBool(Mutation, TEXT("applied")));
    UWidget* PersistedWidget = Fixture.WidgetForId(WidgetId);
    if (!TestNotNull(
            TEXT("Persisted Widget remains addressable after Rename"),
            PersistedWidget))
    {
        Transactions.Restore();
        FString CleanupError;
        Fixture.Cleanup(CleanupError);
        return false;
    }
    const FName PersistedName = PersistedWidget->GetFName();

    FSalPatch Finalize;
    Finalize.Alias = TEXT("menu");
    Finalize.Statements = {
        MakeShared<FJsonValueObject>(
            RobustWidgetOperation(TEXT("compile"))),
        MakeShared<FJsonValueObject>(
            RobustWidgetOperation(TEXT("save")))};
    const TSharedPtr<FJsonObject> Finalized =
        FSalBlueprintInterface::Patch(
            Finalize,
            Fixture.Target());
    TestTrue(TEXT("Widget Blueprint compile+save validates"), RobustWidgetResultBool(Finalized, TEXT("valid")));
    TestTrue(TEXT("Widget Blueprint compile+save applies"), RobustWidgetResultBool(Finalized, TEXT("applied")));
    TestTrue(TEXT("Widget Blueprint package is persisted to disk"), IFileManager::Get().FileExists(*Fixture.Filename));

    Transactions.Restore();
    if (!Fixture.UnloadForReload(Error))
    {
        AddError(Error);
        return false;
    }
    UWidgetBlueprint* Reloaded =
        LoadObject<UWidgetBlueprint>(nullptr, *Fixture.ObjectPath);
    TestNotNull(TEXT("Saved Widget Blueprint reloads from disk"), Reloaded);
    if (Reloaded == nullptr || !Fixture.AdoptReloaded(Reloaded))
    {
        return false;
    }
    TestEqual(TEXT("Widget Blueprint Guid survives reload"), Reloaded->GetBlueprintGuid(), BlueprintId);
    TestEqual(
        TEXT("Widget stable identity survives compile/save/reload"),
        Reloaded->WidgetVariableNameToGuidMap.FindRef(PersistedName),
        WidgetId);
    TestNotNull(
        TEXT("Renamed Widget source object survives compile/save/reload"),
        Reloaded->WidgetTree->FindWidget(PersistedName));

    FSalQuery Exact = RobustWidgetQuery(TEXT("widget"));
    Exact.Operation->SetStringField(TEXT("id"), RobustWidgetGuidText(WidgetId));
    const TSharedPtr<FJsonObject> Readback =
        FSalWidgetInterface::Query(Exact, Fixture.Target());
    TestFalse(TEXT("SAL exact Widget readback succeeds after reload"), RobustWidgetHasError(Readback));
    TestEqual(
        TEXT("SAL exact Widget readback returns the requested persisted identity once"),
        RobustWidgetCallCountById(
            Readback,
            TEXT("widget"),
            RobustWidgetGuidText(WidgetId)),
        1);

    FString CleanupError;
    TestTrue(TEXT("Robust Widget persistence fixture cleans up"), Fixture.Cleanup(CleanupError));
    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
