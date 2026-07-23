// Copyright 2026 Loomle contributors.

#if WITH_DEV_AUTOMATION_TESTS

#include "Sal/StateTree/SalStateTreeInterface.h"
#include "SalStateTreeRobustTestTypes.h"
#include "Tests/LoomleTestEditorState.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Editor.h"
#include "HAL/FileManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/PackageName.h"
#include "Sal/SalModel.h"
#include "StateTree.h"
#include "StateTreeEditingSubsystem.h"
#include "StateTreeEditorData.h"
#include "StateTreeEditorModule.h"
#include "StateTreeEditorNode.h"
#include "StateTreeNodeClassCache.h"
#include "StateTreeState.h"
#include "StructUtils/PropertyBag.h"
#include "UObject/GarbageCollection.h"
#include "UObject/Package.h"
#include "UObject/UObjectHash.h"

namespace
{
using namespace Loomle::Sal;

FString RobustStateTreeGuid(const FGuid& Guid)
{
    return Guid.ToString(EGuidFormats::DigitsWithHyphensLower);
}

TSharedRef<FJsonObject> RobustStateTreeRef(
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

TSharedRef<FJsonObject> RobustStateTreeMember(
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

TSharedRef<FJsonObject> RobustStateTreeOperation(
    const FString& Kind)
{
    TSharedRef<FJsonObject> Operation = MakeShared<FJsonObject>();
    Operation->SetStringField(TEXT("kind"), Kind);
    return Operation;
}

TSharedPtr<FJsonValue> RobustStateTreeDeclaration(
    const FString& Alias,
    const FString& Palette)
{
    TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
    Args->SetStringField(TEXT("palette"), Palette);
    TSharedRef<FJsonObject> Call = MakeShared<FJsonObject>();
    Call->SetStringField(TEXT("kind"), TEXT("call"));
    Call->SetStringField(TEXT("callee"), TEXT("node"));
    Call->SetObjectField(TEXT("args"), Args);
    TSharedRef<FJsonObject> Binding = MakeShared<FJsonObject>();
    Binding->SetObjectField(
        TEXT("target"),
        RobustStateTreeRef(TEXT("local"), Alias));
    Binding->SetObjectField(TEXT("value"), Call);
    return MakeShared<FJsonValueObject>(Binding);
}

FSalPatch RobustStateTreePatch(
    TArray<TSharedPtr<FJsonValue>> Statements,
    const bool bDryRun = false)
{
    FSalPatch Patch;
    Patch.Alias = TEXT("tree");
    Patch.bDryRun = bDryRun;
    Patch.Statements = MoveTemp(Statements);
    return Patch;
}

FSalQuery RobustStateTreeQuery(const FString& Kind)
{
    FSalQuery Query;
    Query.Alias = TEXT("tree");
    Query.Operation = MakeShared<FJsonObject>();
    Query.Operation->SetStringField(TEXT("kind"), Kind);
    return Query;
}

bool RobustStateTreeHasError(
    const TSharedPtr<FJsonObject>& Result)
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

bool RobustStateTreeBool(
    const TSharedPtr<FJsonObject>& Result,
    const TCHAR* Field,
    const bool Default = false)
{
    bool Value = Default;
    return Result.IsValid() && Result->TryGetBoolField(Field, Value)
        ? Value
        : Default;
}

bool RobustStateTreeHasComment(
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

TArray<TSharedPtr<FJsonObject>> RobustStateTreeCalls(
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

class FRobustStateTreeFixture
{
public:
    ~FRobustStateTreeFixture()
    {
        FString Ignored;
        Cleanup(Ignored);
    }

    bool Initialize(FString& OutError)
    {
        const FString Token =
            FGuid::NewGuid().ToString(EGuidFormats::Digits);
        PackageName = FString::Printf(
            TEXT("/Game/LoomleTests/StateTreeRobust/%s/ST_Robust"),
            *Token);
        ObjectPath = PackageName + TEXT(".ST_Robust");
        Filename = FPackageName::LongPackageNameToFilename(
            PackageName,
            FPackageName::GetAssetPackageExtension());
        IFileManager::Get().MakeDirectory(
            *FPackageName::LongPackageNameToFilename(
                FPackageName::GetLongPackagePath(PackageName)),
            true);

        Package = CreatePackage(*PackageName);
        Asset = Package != nullptr
            ? NewObject<UStateTree>(
                Package,
                FName(TEXT("ST_Robust")),
                RF_Public | RF_Standalone | RF_Transactional)
            : nullptr;
        Data = Asset != nullptr
            ? NewObject<UStateTreeEditorData>(
                Asset,
                NAME_None,
                RF_Transactional)
            : nullptr;
        Schema = Data != nullptr
            ? NewObject<USalStateTreeRobustSchema>(
                Data,
                NAME_None,
                RF_Transactional)
            : nullptr;
        if (Asset == nullptr || Data == nullptr || Schema == nullptr)
        {
            OutError = TEXT("UE could not create robust StateTree fixture.");
            return false;
        }
        Asset->EditorData = Data;
        Data->Schema = Schema;
        Root = &Data->AddSubTree(FName(TEXT("Root")));
        Root->ID = FGuid::NewGuid();
        Root->Type = EStateTreeStateType::State;

        ParameterId = FGuid::NewGuid();
        FPropertyBagPropertyDesc Parameter(
            FName(TEXT("SourceValue")),
            EPropertyBagPropertyType::Int32);
        Parameter.ID = ParameterId;
        Parameter.PropertyFlags =
            static_cast<uint64>(
                CPF_Edit | CPF_BlueprintVisible);
        FInstancedPropertyBag& RootBag =
            const_cast<FInstancedPropertyBag&>(
                Data->GetRootParametersPropertyBag());
        RootBag.AddProperties({Parameter});
        ParameterContainerId = Data->GetRootParametersGuid();
        if (!ParameterContainerId.IsValid())
        {
            OutError = TEXT("Robust StateTree root Parameter container has no stable id.");
            return false;
        }
        FAssetRegistryModule::AssetCreated(Asset);
        bRegistered = true;
        UStateTreeEditingSubsystem::ValidateStateTree(Asset);
        Package->SetDirtyFlag(false);
        if (const TSharedPtr<FStateTreeNodeClassCache> Cache =
                FStateTreeEditorModule::GetModule().GetNodeClassCache())
        {
            Cache->InvalidateCache();
        }
        return true;
    }

    FSalResolvedTarget Target() const
    {
        FSalResolvedTarget Target;
        Target.Kind = ESalTargetKind::Asset;
        Target.Alias = TEXT("tree");
        Target.AssetPath =
            Asset != nullptr ? Asset->GetPathName() : ObjectPath;
        Target.Object = Asset;
        Target.Package =
            Asset != nullptr ? Asset->GetOutermost() : nullptr;
        Target.Interfaces = {
            FName(TEXT("asset")),
            FName(TEXT("state_tree"))};
        return Target;
    }

    TSharedRef<FJsonObject> TaskDestination() const
    {
        return RobustStateTreeMember(
            RobustStateTreeRef(
                TEXT("state"),
                RobustStateTreeGuid(Root->ID)),
            {TEXT("Tasks")});
    }

    FString ParameterIdentity() const
    {
        return RobustStateTreeGuid(ParameterContainerId)
            + TEXT("/")
            + RobustStateTreeGuid(ParameterId);
    }

    bool UnloadForReload(FString& OutError)
    {
        if (Package == nullptr)
        {
            OutError = TEXT("StateTree fixture has no Package to unload.");
            return false;
        }
        Package->SetDirtyFlag(false);
        Package->ClearFlags(RF_Public | RF_Standalone);
        ForEachObjectWithPackage(
            Package,
            [](UObject* Object)
            {
                Object->ClearFlags(RF_Public | RF_Standalone);
                return true;
            },
            true);
        Asset = nullptr;
        Data = nullptr;
        Schema = nullptr;
        Root = nullptr;
        Package = nullptr;
        CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);
        if (FindPackage(nullptr, *PackageName) != nullptr)
        {
            OutError = TEXT("Saved robust StateTree Package remained loaded.");
            return false;
        }
        return true;
    }

    bool AdoptReloaded(UStateTree* Reloaded)
    {
        Asset = Reloaded;
        Package = Reloaded != nullptr
            ? Reloaded->GetOutermost()
            : nullptr;
        Data = Reloaded != nullptr
            ? Cast<UStateTreeEditorData>(Reloaded->EditorData)
            : nullptr;
        Schema = Data != nullptr
            ? Cast<USalStateTreeRobustSchema>(Data->Schema)
            : nullptr;
        Root = Data != nullptr && Data->SubTrees.Num() == 1
            ? Data->SubTrees[0]
            : nullptr;
        return Asset != nullptr
            && Data != nullptr
            && Schema != nullptr
            && Root != nullptr;
    }

    bool Cleanup(FString& OutError)
    {
        OutError.Reset();
        if (bCleaned)
        {
            return true;
        }
        bCleaned = true;
        if (Asset == nullptr && !ObjectPath.IsEmpty())
        {
            Asset = FindObject<UStateTree>(nullptr, *ObjectPath);
        }
        if (bRegistered && Asset != nullptr)
        {
            FAssetRegistryModule::AssetDeleted(Asset);
            bRegistered = false;
        }
        UPackage* LoadedPackage = Asset != nullptr
            ? Asset->GetOutermost()
            : (!PackageName.IsEmpty()
                ? FindPackage(nullptr, *PackageName)
                : nullptr);
        if (LoadedPackage != nullptr)
        {
            LoadedPackage->SetDirtyFlag(false);
            LoadedPackage->ClearFlags(RF_Public | RF_Standalone);
            ForEachObjectWithPackage(
                LoadedPackage,
                [](UObject* Object)
                {
                    Object->ClearFlags(RF_Public | RF_Standalone);
                    return true;
                },
                true);
        }
        Asset = nullptr;
        Data = nullptr;
        Schema = nullptr;
        Root = nullptr;
        Package = nullptr;
        CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);
        if (!Filename.IsEmpty()
            && IFileManager::Get().FileExists(*Filename)
            && !IFileManager::Get().Delete(*Filename, false, true, true))
        {
            OutError = TEXT("StateTree fixture file could not be deleted.");
        }
        return OutError.IsEmpty();
    }

    FString PackageName;
    FString ObjectPath;
    FString Filename;
    UPackage* Package = nullptr;
    UStateTree* Asset = nullptr;
    UStateTreeEditorData* Data = nullptr;
    USalStateTreeRobustSchema* Schema = nullptr;
    UStateTreeState* Root = nullptr;
    FGuid ParameterContainerId;
    FGuid ParameterId;

private:
    bool bRegistered = false;
    bool bCleaned = false;
};

bool RobustStateTreeFindPalette(
    const FRobustStateTreeFixture& Fixture,
    FString& OutPalette,
    TSharedPtr<FJsonObject>& OutResult)
{
    FSalQuery Query = RobustStateTreeQuery(TEXT("palette_entries"));
    Query.Operation->SetStringField(
        TEXT("text"),
        TEXT("SalStateTreeRobustTask"));
    Query.Operation->SetObjectField(
        TEXT("to"),
        Fixture.TaskDestination());
    OutResult =
        FSalStateTreeInterface::Query(Query, Fixture.Target());
    for (const TSharedPtr<FJsonObject>& Args :
         RobustStateTreeCalls(OutResult, TEXT("node")))
    {
        FString Palette;
        if (Args.IsValid()
            && Args->TryGetStringField(TEXT("palette"), Palette))
        {
            OutPalette = Palette;
            return true;
        }
    }
    return false;
}

FSalPatch RobustStateTreeAddAndBindPatch(
    const FRobustStateTreeFixture& Fixture,
    const FString& Palette,
    const bool bDryRun)
{
    TSharedRef<FJsonObject> Add =
        RobustStateTreeOperation(TEXT("add"));
    Add->SetObjectField(
        TEXT("target"),
        RobustStateTreeRef(TEXT("local"), TEXT("classTask")));
    Add->SetObjectField(TEXT("to"), Fixture.TaskDestination());

    TSharedRef<FJsonObject> Bind =
        RobustStateTreeOperation(TEXT("bind"));
    Bind->SetObjectField(
        TEXT("from"),
        RobustStateTreeRef(
            TEXT("parameter"),
            Fixture.ParameterIdentity()));
    Bind->SetObjectField(
        TEXT("to"),
        RobustStateTreeMember(
            RobustStateTreeRef(TEXT("local"), TEXT("classTask")),
            {TEXT("Instance"), TEXT("InputValue")}));
    return RobustStateTreePatch(
        {
            RobustStateTreeDeclaration(TEXT("classTask"), Palette),
            MakeShared<FJsonValueObject>(Add),
            MakeShared<FJsonValueObject>(Bind)
        },
        bDryRun);
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FSalStateTreeRobustClassBackedLifecycleTest,
    "Loomle.Sal.StateTree.Robust.ClassBackedPaletteSchemaLifecycle",
    EAutomationTestFlags::EditorContext
        | EAutomationTestFlags::EngineFilter)

bool FSalStateTreeRobustClassBackedLifecycleTest::RunTest(
    const FString& Parameters)
{
    (void)Parameters;
    if (GEditor == nullptr)
    {
        AddError(TEXT("Class-backed StateTree lifecycle requires GEditor."));
        return false;
    }
    Loomle::Tests::FScopedIsolatedTransactor Transactions;
    if (!Transactions.Initialize())
    {
        return false;
    }
    FRobustStateTreeFixture Fixture;
    FString Error;
    if (!Fixture.Initialize(Error))
    {
        AddError(Error);
        Transactions.Restore();
        return false;
    }

    FString Palette;
    TSharedPtr<FJsonObject> SearchResult;
    TestTrue(
        TEXT("Destination-bound Palette discovers the class-backed Task"),
        RobustStateTreeFindPalette(
            Fixture,
            Palette,
            SearchResult));
    TestFalse(
        TEXT("Class-backed Palette search is diagnostic-clean"),
        RobustStateTreeHasError(SearchResult));

    FSalQuery ExactPalette =
        RobustStateTreeQuery(TEXT("palette"));
    ExactPalette.Operation->SetStringField(TEXT("id"), Palette);
    ExactPalette.Operation->SetObjectField(
        TEXT("to"),
        Fixture.TaskDestination());
    ExactPalette.With = {TEXT("schema")};
    const TSharedPtr<FJsonObject> PaletteSchema =
        FSalStateTreeInterface::Query(
            ExactPalette,
            Fixture.Target());
    TestFalse(
        TEXT("Exact class-backed Palette schema succeeds"),
        RobustStateTreeHasError(PaletteSchema));
    TestTrue(
        TEXT("Class-backed Palette schema exposes the native UObject property"),
        RobustStateTreeHasComment(
            PaletteSchema,
            TEXT("InputValue")));

    const TSharedPtr<FJsonObject> DryRun =
        FSalStateTreeInterface::Patch(
            RobustStateTreeAddAndBindPatch(
                Fixture,
                Palette,
                true),
            Fixture.Target());
    TestTrue(
        TEXT("Class-backed add+bind dry-run validates"),
        RobustStateTreeBool(DryRun, TEXT("valid")));
    TestEqual(
        TEXT("Class-backed add+bind dry-run preserves live Tasks"),
        Fixture.Root->Tasks.Num(),
        0);

    const TSharedPtr<FJsonObject> Applied =
        FSalStateTreeInterface::Patch(
            RobustStateTreeAddAndBindPatch(
                Fixture,
                Palette,
                false),
            Fixture.Target());
    TestTrue(
        TEXT("Class-backed add+bind applies"),
        RobustStateTreeBool(Applied, TEXT("applied")));
    TestEqual(
        TEXT("Class-backed Task is authored once"),
        Fixture.Root->Tasks.Num(),
        1);
    if (Fixture.Root->Tasks.Num() != 1)
    {
        return false;
    }
    FStateTreeEditorNode& Node = Fixture.Root->Tasks[0];
    TestTrue(
        TEXT("StateTree uses UE's class-backed InstanceObject path"),
        Node.InstanceObject != nullptr
            && Node.InstanceObject->IsA<USalStateTreeRobustTask>());
    TestEqual(
        TEXT("Class-backed binding is authored once"),
        Fixture.Data->EditorBindings.GetBindings().Num(),
        1);
    const FGuid NodeId = Node.ID;

    FSalQuery ExactNode = RobustStateTreeQuery(TEXT("node"));
    ExactNode.Operation->SetStringField(
        TEXT("id"),
        RobustStateTreeGuid(NodeId));
    ExactNode.With = {TEXT("schema")};
    const TSharedPtr<FJsonObject> ExactNodeResult =
        FSalStateTreeInterface::Query(
            ExactNode,
            Fixture.Target());
    TestFalse(
        TEXT("Exact class-backed Node schema succeeds"),
        RobustStateTreeHasError(ExactNodeResult));
    const TArray<TSharedPtr<FJsonObject>> NodeCalls =
        RobustStateTreeCalls(ExactNodeResult, TEXT("node"));
    FString NativeType;
    TestTrue(
        TEXT("Exact Node preserves the concrete class-backed UE type"),
        NodeCalls.Num() == 1
            && NodeCalls[0]->TryGetStringField(
                TEXT("type"),
                NativeType)
            && NativeType
                == USalStateTreeRobustTask::StaticClass()
                       ->GetPathName());

    TSharedRef<FJsonObject> Remove =
        RobustStateTreeOperation(TEXT("remove"));
    Remove->SetObjectField(
        TEXT("target"),
        RobustStateTreeRef(
            TEXT("node"),
            RobustStateTreeGuid(NodeId)));
    const TSharedPtr<FJsonObject> Removed =
        FSalStateTreeInterface::Patch(
            RobustStateTreePatch(
                {MakeShared<FJsonValueObject>(Remove)}),
            Fixture.Target());
    TestTrue(
        TEXT("Class-backed Node remove applies"),
        RobustStateTreeBool(Removed, TEXT("applied")));
    TestEqual(
        TEXT("Class-backed Node remove clears its native Tasks entry"),
        Fixture.Root->Tasks.Num(),
        0);
    TestEqual(
        TEXT("Class-backed Node remove cascades its authored Binding"),
        Fixture.Data->EditorBindings.GetBindings().Num(),
        0);
    TestTrue(
        TEXT("Class-backed Node remove is one undoable UE transaction"),
        GEditor->UndoTransaction(false));
    TestEqual(
        TEXT("Undo restores class-backed Node and Binding together"),
        Fixture.Root->Tasks.Num(),
        1);
    TestEqual(
        TEXT("Undo restores the class-backed Binding"),
        Fixture.Data->EditorBindings.GetBindings().Num(),
        1);

    Transactions.Restore();
    FString CleanupError;
    TestTrue(
        TEXT("Class-backed StateTree lifecycle fixture cleans up"),
        Fixture.Cleanup(CleanupError));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FSalStateTreeRobustPersistenceTest,
    "Loomle.Sal.StateTree.Robust.CompileSaveReloadStableIdentity",
    EAutomationTestFlags::EditorContext
        | EAutomationTestFlags::EngineFilter)

bool FSalStateTreeRobustPersistenceTest::RunTest(
    const FString& Parameters)
{
    (void)Parameters;
    if (GEditor == nullptr || GEditor->IsPlaySessionInProgress())
    {
        AddError(TEXT("StateTree persistence requires an Editor outside PIE."));
        return false;
    }
    Loomle::Tests::FScopedIsolatedTransactor Transactions;
    if (!Transactions.Initialize())
    {
        return false;
    }
    FRobustStateTreeFixture Fixture;
    FString Error;
    if (!Fixture.Initialize(Error))
    {
        AddError(Error);
        Transactions.Restore();
        return false;
    }
    FString Palette;
    TSharedPtr<FJsonObject> SearchResult;
    if (!RobustStateTreeFindPalette(
            Fixture,
            Palette,
            SearchResult))
    {
        AddError(TEXT("Persistent StateTree could not discover robust class-backed Task."));
        return false;
    }
    const TSharedPtr<FJsonObject> Added =
        FSalStateTreeInterface::Patch(
            RobustStateTreeAddAndBindPatch(
                Fixture,
                Palette,
                false),
            Fixture.Target());
    if (!TestTrue(
            TEXT("Persistent class-backed Task and Binding apply"),
            RobustStateTreeBool(Added, TEXT("applied")))
        || Fixture.Root->Tasks.Num() != 1)
    {
        return false;
    }
    const FGuid StateId = Fixture.Root->ID;
    const FGuid NodeId = Fixture.Root->Tasks[0].ID;
    const FGuid ContainerId = Fixture.ParameterContainerId;
    const FGuid PropertyId = Fixture.ParameterId;

    FSalPatch Finalize = RobustStateTreePatch(
        {
            MakeShared<FJsonValueObject>(
                RobustStateTreeOperation(TEXT("compile"))),
            MakeShared<FJsonValueObject>(
                RobustStateTreeOperation(TEXT("save")))
        });
    const TSharedPtr<FJsonObject> Finalized =
        FSalStateTreeInterface::Patch(
            Finalize,
            Fixture.Target());
    TestTrue(
        TEXT("StateTree compile+save validates"),
        RobustStateTreeBool(Finalized, TEXT("valid")));
    TestTrue(
        TEXT("StateTree compile+save applies"),
        RobustStateTreeBool(Finalized, TEXT("applied")));
    TestTrue(
        TEXT("StateTree save writes its exact Package"),
        IFileManager::Get().FileExists(*Fixture.Filename));

    Transactions.Restore();
    if (!Fixture.UnloadForReload(Error))
    {
        AddError(Error);
        return false;
    }
    UStateTree* Reloaded =
        LoadObject<UStateTree>(nullptr, *Fixture.ObjectPath);
    TestNotNull(
        TEXT("Saved StateTree reloads from disk"),
        Reloaded);
    if (Reloaded == nullptr || !Fixture.AdoptReloaded(Reloaded))
    {
        return false;
    }
    TestEqual(
        TEXT("State identity survives StateTree compile/save/reload"),
        Fixture.Root->ID,
        StateId);
    TestEqual(
        TEXT("Class-backed Node identity survives StateTree compile/save/reload"),
        Fixture.Root->Tasks.Num() == 1
            ? Fixture.Root->Tasks[0].ID
            : FGuid(),
        NodeId);
    TestTrue(
        TEXT("Class-backed InstanceObject survives StateTree compile/save/reload"),
        Fixture.Root->Tasks.Num() == 1
            && Fixture.Root->Tasks[0].InstanceObject != nullptr
            && Fixture.Root->Tasks[0].InstanceObject
                   ->IsA<USalStateTreeRobustTask>());
    TestEqual(
        TEXT("Root Parameter container identity survives reload"),
        Fixture.Data->GetRootParametersGuid(),
        ContainerId);
    const UPropertyBag* ReloadedBag =
        Fixture.Data->GetRootParametersPropertyBag()
            .GetPropertyBagStruct();
    bool bFoundProperty = false;
    if (ReloadedBag != nullptr)
    {
        for (const FPropertyBagPropertyDesc& Desc :
             ReloadedBag->GetPropertyDescs())
        {
            bFoundProperty |= Desc.ID == PropertyId;
        }
    }
    TestTrue(
        TEXT("Bound Parameter descriptor identity survives reload"),
        bFoundProperty);
    TestEqual(
        TEXT("Authored Binding survives reload"),
        Fixture.Data->EditorBindings.GetBindings().Num(),
        1);

    FSalQuery ExactNode = RobustStateTreeQuery(TEXT("node"));
    ExactNode.Operation->SetStringField(
        TEXT("id"),
        RobustStateTreeGuid(NodeId));
    const TSharedPtr<FJsonObject> Readback =
        FSalStateTreeInterface::Query(
            ExactNode,
            Fixture.Target());
    TestFalse(
        TEXT("SAL exact class-backed Node readback succeeds after reload"),
        RobustStateTreeHasError(Readback));
    TestEqual(
        TEXT("SAL readback returns the persisted Node exactly once"),
        RobustStateTreeCalls(Readback, TEXT("node")).Num(),
        1);

    FString CleanupError;
    TestTrue(
        TEXT("Persistent StateTree fixture cleans up"),
        Fixture.Cleanup(CleanupError));
    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
