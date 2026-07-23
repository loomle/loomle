// Copyright 2026 Loomle contributors.

#if WITH_DEV_AUTOMATION_TESTS

#include "Sal/StateTree/SalStateTreePalette.h"
#include "SalStateTreePaletteTestSchema.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Misc/AutomationTest.h"
#include "StateTree.h"
#include "StateTreeEditorData.h"
#include "StateTreeState.h"
#include "StructUtils/PropertyBag.h"
#include "UObject/Package.h"
#include "UObject/UObjectGlobals.h"

namespace
{
using namespace Loomle::Sal::StateTreePalette;

struct FPaletteFixture
{
    UStateTree* Asset = nullptr;
    UStateTreeEditorData* Data = nullptr;
    USalStateTreePaletteTestSchema* Schema = nullptr;
};

FPaletteFixture MakePaletteFixture()
{
    FPaletteFixture Result;
    UPackage* Package = CreatePackage(*FString::Printf(
        TEXT("/LoomleTests/StateTreePalette_%s"),
        *FGuid::NewGuid().ToString(EGuidFormats::Digits)));
    Result.Asset = NewObject<UStateTree>(Package, NAME_None, RF_Transient | RF_Transactional);
    Result.Data = NewObject<UStateTreeEditorData>(
        Result.Asset,
        NAME_None,
        RF_Transient | RF_Transactional);
    Result.Asset->EditorData = Result.Data;
    Result.Schema = NewObject<USalStateTreePaletteTestSchema>(Result.Data);
    Result.Data->Schema = Result.Schema;
    return Result;
}

FPropertyBagPropertyDesc FloatParameter(const FName Name)
{
    FPropertyBagPropertyDesc Desc(Name, EPropertyBagPropertyType::Float);
    Desc.ID = FGuid::NewGuid();
    return Desc;
}

TSharedPtr<FJsonObject> ConstructorArgs(const FEntry& Entry)
{
    const TSharedPtr<FJsonValue> Value = MakeConstructor(Entry);
    const TSharedPtr<FJsonObject>* Call = nullptr;
    const TSharedPtr<FJsonObject>* Args = nullptr;
    if (!Value.IsValid()
        || !Value->TryGetObject(Call)
        || Call == nullptr
        || !(*Call)->TryGetObjectField(TEXT("args"), Args)
        || Args == nullptr)
    {
        return nullptr;
    }
    return *Args;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FSalStateTreePaletteConstructorTest,
    "Loomle.Sal.StateTree.Palette.CopyableConstructors",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSalStateTreePaletteConstructorTest::RunTest(const FString& Parameters)
{
    const FPaletteFixture Fixture = MakePaletteFixture();

    UStateTreeState& Ancestor = Fixture.Data->AddSubTree(FName(TEXT("Ancestor")));
    Ancestor.ID = FGuid(0xA1000001, 0xA1000002, 0xA1000003, 0xA1000004);
    Ancestor.Type = EStateTreeStateType::Subtree;
    UStateTreeState& Parent = Ancestor.AddChildState(FName(TEXT("Parent")));
    Parent.ID = FGuid(0xA2000001, 0xA2000002, 0xA2000003, 0xA2000004);
    Parent.Type = EStateTreeStateType::Subtree;
    UStateTreeState& ValidTarget = Fixture.Data->AddSubTree(FName(TEXT("Reusable")));
    ValidTarget.ID = FGuid(0xA3000001, 0xA3000002, 0xA3000003, 0xA3000004);
    ValidTarget.Type = EStateTreeStateType::Subtree;
    UStateTreeState& OrdinaryState = Fixture.Data->AddSubTree(FName(TEXT("Ordinary")));
    OrdinaryState.ID = FGuid(0xA4000001, 0xA4000002, 0xA4000003, 0xA4000004);
    OrdinaryState.Type = EStateTreeStateType::State;

    FDestination StateDestination;
    StateDestination.Role = EDestinationRole::ChildState;
    StateDestination.OwnerId = Parent.ID;
    Fixture.Schema->bAllowOrdinaryState = true;
    Fixture.Schema->bAllowLinkedAssetState = true;
    FPage StatePage;
    FString Message;
    TestTrue(
        TEXT("Mixed State Palette discovery succeeds"),
        DiscoverEntries(
            *Fixture.Asset,
            *Fixture.Data,
            StateDestination,
            FString(),
            0,
            50,
            StatePage,
            Message));
    TestEqual(
        TEXT("Mixed Schema returns ordinary, LinkedAsset, and legal Linked capabilities"),
        StatePage.Entries.Num(),
        3);
    const FEntry* OrdinaryEntry = StatePage.Entries.FindByPredicate(
        [](const FEntry& Entry)
        {
            return Entry.Id == TEXT("state_tree.state");
        });
    const FEntry* LinkedAssetEntry = StatePage.Entries.FindByPredicate(
        [](const FEntry& Entry)
        {
            return Entry.Id == TEXT("state_tree.state.linked_asset");
        });
    const FEntry* LinkedEntry = StatePage.Entries.FindByPredicate(
        [](const FEntry& Entry)
        {
            return Entry.LinkedSubtreeId.IsValid();
        });
    TestNotNull(TEXT("Mixed Schema keeps the generic State capability"), OrdinaryEntry);
    TestNotNull(TEXT("Mixed Schema adds the opaque LinkedAsset capability"), LinkedAssetEntry);
    TestNotNull(TEXT("Mixed Schema adds a destination-bound Linked capability"), LinkedEntry);
    if (OrdinaryEntry != nullptr)
    {
        TestEqual(
            TEXT("Ordinary State capability keeps its stable generic id"),
            OrdinaryEntry->Id,
            FString(TEXT("state_tree.state")));
        TestEqual(
            TEXT("Ordinary State capability seeds one allowed native non-Linked Type"),
            OrdinaryEntry->StateType,
            FString(TEXT("State")));
        TestFalse(
            TEXT("Ordinary State seed leaves State, Group, and Subtree editable"),
            OrdinaryEntry->bFixedStateType);
        const TSharedPtr<FJsonObject> OrdinaryArgs = ConstructorArgs(*OrdinaryEntry);
        TestTrue(TEXT("Ordinary State exposes a copyable constructor"), OrdinaryArgs.IsValid());
        TestFalse(
            TEXT("Ordinary State seed does not carry a locked Linked target"),
            OrdinaryArgs.IsValid() && OrdinaryArgs->HasField(TEXT("LinkedSubtree")));
    }
    if (LinkedAssetEntry != nullptr)
    {
        TestTrue(
            TEXT("LinkedAsset capability fixes its native State Type"),
            LinkedAssetEntry->bFixedStateType);
        TestEqual(
            TEXT("LinkedAsset capability uses the native LinkedAsset Type"),
            LinkedAssetEntry->StateType,
            FString(TEXT("LinkedAsset")));
        const TSharedPtr<FJsonObject> LinkedAssetArgs = ConstructorArgs(*LinkedAssetEntry);
        FString LinkedAssetType;
        TestTrue(
            TEXT("LinkedAsset constructor is copyable with its exact native Type"),
            LinkedAssetArgs.IsValid()
                && LinkedAssetArgs->TryGetStringField(TEXT("Type"), LinkedAssetType)
                && LinkedAssetType == TEXT("LinkedAsset"));
        TestFalse(
            TEXT("LinkedAsset constructor omits UE's predefined SelectionBehavior"),
            LinkedAssetArgs.IsValid()
                && LinkedAssetArgs->HasField(TEXT("SelectionBehavior")));
        TestFalse(
            TEXT("LinkedAsset constructor does not invent or scan an asset path"),
            LinkedAssetArgs.IsValid()
                && LinkedAssetArgs->HasField(TEXT("LinkedAsset")));
    }
    FString LinkedPaletteId;
    if (LinkedEntry != nullptr)
    {
        LinkedPaletteId = LinkedEntry->Id;
        TestTrue(
            TEXT("Destination-bound Linked capability fixes its native State Type"),
            LinkedEntry->bFixedStateType);
        TestEqual(
            TEXT("Ancestor Subtrees are excluded and the Linked entry carries the legal target"),
            LinkedEntry->LinkedSubtreeId,
            ValidTarget.ID);
        const TSharedPtr<FJsonObject> Args = ConstructorArgs(*LinkedEntry);
        const TSharedPtr<FJsonObject>* Link = nullptr;
        FString LinkKind;
        FString LinkId;
        TestTrue(
            TEXT("Copyable Linked State constructor contains a typed state@id"),
            Args.IsValid()
                && Args->TryGetObjectField(TEXT("LinkedSubtree"), Link)
                && Link != nullptr
                && (*Link)->TryGetStringField(TEXT("kind"), LinkKind)
                && LinkKind == TEXT("state")
                && (*Link)->TryGetStringField(TEXT("id"), LinkId)
                && LinkId == ValidTarget.ID.ToString(EGuidFormats::DigitsWithHyphensLower));

        FEntry ExactState;
        TestTrue(
            TEXT("Exact Linked State capability revalidates at the same destination"),
            ResolveEntry(
                *Fixture.Asset,
                *Fixture.Data,
                StateDestination,
                LinkedEntry->Id,
                ExactState,
                Message));
        TestEqual(
            TEXT("Exact Linked State capability preserves the target"),
            ExactState.LinkedSubtreeId,
            ValidTarget.ID);
    }

    ValidTarget.Type = EStateTreeStateType::State;
    if (!LinkedPaletteId.IsEmpty())
    {
        FEntry ExactState;
        TestFalse(
            TEXT("Exact Linked State capability becomes stale when its target is no longer a Subtree"),
            ResolveEntry(
                *Fixture.Asset,
                *Fixture.Data,
                StateDestination,
                LinkedPaletteId,
                ExactState,
                Message));
    }
    FPage OrdinaryOnlyPage;
    TestTrue(
        TEXT("Mixed Palette remains available after a Linked target becomes stale"),
        DiscoverEntries(
            *Fixture.Asset,
            *Fixture.Data,
            StateDestination,
            FString(),
            0,
            50,
            OrdinaryOnlyPage,
            Message));
    TestEqual(
        TEXT("Stale Linked target leaves ordinary and LinkedAsset capabilities"),
        OrdinaryOnlyPage.Entries.Num(),
        2);

    Fixture.Schema->bAllowOrdinaryState = false;
    Fixture.Schema->bAllowLinkedState = false;
    FPage LinkedAssetOnlyPage;
    TestTrue(
        TEXT("LinkedAsset-only Palette discovery succeeds without asset scanning"),
        DiscoverEntries(
            *Fixture.Asset,
            *Fixture.Data,
            StateDestination,
            FString(),
            0,
            50,
            LinkedAssetOnlyPage,
            Message));
    TestEqual(
        TEXT("LinkedAsset-only Schema returns exactly one opaque capability"),
        LinkedAssetOnlyPage.Entries.Num(),
        1);
    if (LinkedAssetOnlyPage.Entries.Num() == 1)
    {
        TestEqual(
            TEXT("LinkedAsset-only capability keeps its stable opaque id"),
            LinkedAssetOnlyPage.Entries[0].Id,
            FString(TEXT("state_tree.state.linked_asset")));
        const TSharedPtr<FJsonObject> Args = ConstructorArgs(LinkedAssetOnlyPage.Entries[0]);
        TestTrue(TEXT("LinkedAsset-only constructor remains copyable"), Args.IsValid());
        TestFalse(
            TEXT("LinkedAsset-only constructor omits SelectionBehavior"),
            Args.IsValid() && Args->HasField(TEXT("SelectionBehavior")));

        FEntry ExactLinkedAsset;
        TestTrue(
            TEXT("Exact LinkedAsset capability revalidates while Schema allows it"),
            ResolveEntry(
                *Fixture.Asset,
                *Fixture.Data,
                StateDestination,
                LinkedAssetOnlyPage.Entries[0].Id,
                ExactLinkedAsset,
                Message));
        TestTrue(
            TEXT("Exact LinkedAsset capability preserves fixed State Type semantics"),
            ExactLinkedAsset.bFixedStateType);
    }

    Fixture.Schema->bAllowLinkedAssetState = false;
    FEntry StaleLinkedAsset;
    TestFalse(
        TEXT("Exact LinkedAsset capability becomes stale when Schema disables it"),
        ResolveEntry(
            *Fixture.Asset,
            *Fixture.Data,
            StateDestination,
            TEXT("state_tree.state.linked_asset"),
            StaleLinkedAsset,
            Message));

    Fixture.Schema->bAllowLinkedState = true;
    FPage EmptyStatePage;
    TestTrue(
        TEXT("Linked-only Palette remains a valid empty result without a legal target"),
        DiscoverEntries(
            *Fixture.Asset,
            *Fixture.Data,
            StateDestination,
            FString(),
            0,
            50,
            EmptyStatePage,
            Message));
    TestEqual(
        TEXT("No invalid bare Linked constructor is advertised"),
        EmptyStatePage.Entries.Num(),
        0);

    ValidTarget.Type = EStateTreeStateType::Subtree;
    FPage LinkedOnlyPage;
    TestTrue(
        TEXT("Linked-only Palette discovers a restored legal target"),
        DiscoverEntries(
            *Fixture.Asset,
            *Fixture.Data,
            StateDestination,
            FString(),
            0,
            50,
            LinkedOnlyPage,
            Message));
    TestEqual(
        TEXT("Linked-only Schema returns exactly the target-bound capability"),
        LinkedOnlyPage.Entries.Num(),
        1);
    if (LinkedOnlyPage.Entries.Num() == 1)
    {
        TestEqual(
            TEXT("Linked-only capability remains locked to its exact target"),
            LinkedOnlyPage.Entries[0].LinkedSubtreeId,
            ValidTarget.ID);
    }

    Parent.Parameters.Parameters.AddProperties({FloatParameter(TEXT("NewParameter"))});
    FDestination ParameterDestination;
    ParameterDestination.Role = EDestinationRole::Parameter;
    ParameterDestination.OwnerId = Parent.ID;
    FPage ParameterPage;
    TestTrue(
        TEXT("Parameter Palette discovery succeeds for the exact Property Bag"),
        DiscoverEntries(
            *Fixture.Asset,
            *Fixture.Data,
            ParameterDestination,
            FString(),
            0,
            50,
            ParameterPage,
            Message));
    TestEqual(TEXT("Parameter Palette returns one constructor"), ParameterPage.Entries.Num(), 1);
    if (ParameterPage.Entries.Num() == 1)
    {
        const FEntry& ParameterEntry = ParameterPage.Entries[0];
        TestEqual(
            TEXT("Parameter constructor avoids an existing native Name"),
            ParameterEntry.ParameterName,
            FString(TEXT("NewParameter_1")));
        const TSharedPtr<FJsonObject> Args = ConstructorArgs(ParameterEntry);
        FString ConstructorName;
        TestTrue(
            TEXT("Copyable Parameter constructor carries the exact unique Name"),
            Args.IsValid()
                && Args->TryGetStringField(TEXT("Name"), ConstructorName)
                && ConstructorName == TEXT("NewParameter_1"));

        Parent.Parameters.Parameters.AddProperties({FloatParameter(TEXT("NewParameter_1"))});
        FEntry Recomputed;
        TestTrue(
            TEXT("Exact Parameter capability revalidates after its destination changes"),
            ResolveEntry(
                *Fixture.Asset,
                *Fixture.Data,
                ParameterDestination,
                ParameterEntry.Id,
                Recomputed,
                Message));
        TestEqual(
            TEXT("Exact Parameter capability recomputes the next deterministic Name"),
            Recomputed.ParameterName,
            FString(TEXT("NewParameter_2")));
    }
    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
