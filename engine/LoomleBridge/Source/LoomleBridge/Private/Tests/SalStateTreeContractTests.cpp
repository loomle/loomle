// Copyright 2026 Loomle contributors.

#if WITH_DEV_AUTOMATION_TESTS

#include "Sal/SalJson.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Misc/AutomationTest.h"

namespace
{
using Loomle::Sal::FSalJson;
using Loomle::Sal::FSalPatch;
using Loomle::Sal::FSalQuery;

TSharedRef<FJsonObject> MakeCall(const FString& Callee, const TSharedRef<FJsonObject>& Args)
{
    const TSharedRef<FJsonObject> Call = MakeShared<FJsonObject>();
    Call->SetStringField(TEXT("kind"), TEXT("call"));
    Call->SetStringField(TEXT("callee"), Callee);
    Call->SetObjectField(TEXT("args"), Args);
    return Call;
}

TSharedRef<FJsonObject> MakeAssetTarget()
{
    const TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
    Args->SetStringField(TEXT("path"), TEXT("/Game/AI/ST_Test.ST_Test"));
    Args->SetStringField(TEXT("type"), TEXT("/Script/StateTreeModule.StateTree"));

    const TSharedRef<FJsonObject> Target = MakeShared<FJsonObject>();
    Target->SetStringField(TEXT("alias"), TEXT("tree"));
    Target->SetObjectField(TEXT("value"), MakeCall(TEXT("asset"), Args));
    return Target;
}

TSharedRef<FJsonObject> MakeStableRef(const FString& Kind, const FString& Id)
{
    const TSharedRef<FJsonObject> Ref = MakeShared<FJsonObject>();
    Ref->SetStringField(TEXT("kind"), Kind);
    Ref->SetStringField(TEXT("id"), Id);
    return Ref;
}

TSharedRef<FJsonObject> MakeLocalRef(const FString& Name)
{
    const TSharedRef<FJsonObject> Ref = MakeShared<FJsonObject>();
    Ref->SetStringField(TEXT("kind"), TEXT("local"));
    Ref->SetStringField(TEXT("name"), Name);
    return Ref;
}

TSharedRef<FJsonObject> MakeMemberRef(
    const TSharedRef<FJsonObject>& Owner,
    TArray<TSharedPtr<FJsonValue>> Path)
{
    const TSharedRef<FJsonObject> Ref = MakeShared<FJsonObject>();
    Ref->SetStringField(TEXT("kind"), TEXT("member"));
    Ref->SetObjectField(TEXT("object"), Owner);
    Ref->SetArrayField(TEXT("path"), MoveTemp(Path));
    return Ref;
}

TSharedPtr<FJsonValue> StringSegment(const FString& Value)
{
    return MakeShared<FJsonValueString>(Value);
}

TSharedPtr<FJsonValue> IndexSegment(const int32 Value)
{
    return MakeShared<FJsonValueNumber>(Value);
}

bool DecodeQuery(const TSharedRef<FJsonObject>& Operation, FSalQuery& OutQuery)
{
    const TSharedRef<FJsonObject> Query = MakeShared<FJsonObject>();
    Query->SetStringField(TEXT("kind"), TEXT("query"));
    Query->SetObjectField(TEXT("target"), MakeAssetTarget());
    Query->SetObjectField(TEXT("operation"), Operation);

    const TSharedRef<FJsonObject> Envelope = MakeShared<FJsonObject>();
    Envelope->SetObjectField(TEXT("object"), Query);
    TSharedPtr<FJsonObject> Error;
    return FSalJson::DecodeQuery(Envelope, OutQuery, Error);
}

bool DecodePatch(TArray<TSharedPtr<FJsonValue>> Statements, FSalPatch& OutPatch)
{
    const TSharedRef<FJsonObject> Patch = MakeShared<FJsonObject>();
    Patch->SetStringField(TEXT("kind"), TEXT("patch"));
    Patch->SetObjectField(TEXT("target"), MakeAssetTarget());
    Patch->SetBoolField(TEXT("dryRun"), true);
    Patch->SetArrayField(TEXT("statements"), MoveTemp(Statements));

    const TSharedRef<FJsonObject> Envelope = MakeShared<FJsonObject>();
    Envelope->SetObjectField(TEXT("object"), Patch);
    TSharedPtr<FJsonObject> Error;
    return FSalJson::DecodePatch(Envelope, OutPatch, Error);
}

TSharedRef<FJsonObject> MakeKindOperation(const FString& Kind)
{
    const TSharedRef<FJsonObject> Operation = MakeShared<FJsonObject>();
    Operation->SetStringField(TEXT("kind"), Kind);
    return Operation;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FSalStateTreeQueryContractTest,
    "Loomle.Sal.Contract.StateTree.Query",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSalStateTreeQueryContractTest::RunTest(const FString& Parameters)
{
    for (const FString& Kind : {TEXT("target"), TEXT("states"), TEXT("nodes"), TEXT("parameters")})
    {
        FSalQuery Query;
        TestTrue(*FString::Printf(TEXT("StateTree %s Query shape is accepted"), *Kind), DecodeQuery(MakeKindOperation(Kind), Query));
    }

    const TSharedRef<FJsonObject> ParameterSearch = MakeKindOperation(TEXT("parameters"));
    ParameterSearch->SetStringField(TEXT("text"), TEXT("Movement Speed"));
    FSalQuery ParameterSearchQuery;
    TestTrue(
        TEXT("StateTree Parameter collection accepts its optional search text"),
        DecodeQuery(ParameterSearch, ParameterSearchQuery));

    for (const FString& Kind : {TEXT("state"), TEXT("node"), TEXT("transition"), TEXT("parameter"), TEXT("object")})
    {
        const TSharedRef<FJsonObject> Operation = MakeKindOperation(Kind);
        Operation->SetStringField(
            TEXT("id"),
            Kind == TEXT("parameter") ? TEXT("container-guid/property-guid") : TEXT("authored-guid"));
        FSalQuery Query;
        TestTrue(*FString::Printf(TEXT("StateTree %s exact Query shape is accepted"), *Kind), DecodeQuery(Operation, Query));
    }


    const TSharedRef<FJsonObject> CompoundParameter = MakeKindOperation(TEXT("parameter"));
    CompoundParameter->SetStringField(
        TEXT("id"),
        TEXT("11111111-2222-3333-4444-555555555555/aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee"));
    FSalQuery CompoundParameterQuery;
    TestTrue(
        TEXT("StateTree exact Parameter preserves the canonical container/property compound id"),
        DecodeQuery(CompoundParameter, CompoundParameterQuery));
    FString CompoundId;
    TestTrue(
        TEXT("Decoded exact Parameter keeps the slash as identity rather than member syntax"),
        CompoundParameterQuery.Operation.IsValid()
            && CompoundParameterQuery.Operation->TryGetStringField(TEXT("id"), CompoundId)
            && CompoundId == TEXT("11111111-2222-3333-4444-555555555555/aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee"));

    const TSharedRef<FJsonObject> Tree = MakeKindOperation(TEXT("tree"));
    Tree->SetObjectField(TEXT("root"), MakeStableRef(TEXT("state"), TEXT("root-state-guid")));
    Tree->SetNumberField(TEXT("depth"), 20);
    FSalQuery TreeQuery;
    TestTrue(TEXT("StateTree tree accepts an optional stable State root"), DecodeQuery(Tree, TreeQuery));

    const TSharedRef<FJsonObject> Entries = MakeKindOperation(TEXT("palette_entries"));
    Entries->SetStringField(TEXT("text"), TEXT("Follow"));
    Entries->SetObjectField(
        TEXT("to"),
        MakeMemberRef(MakeStableRef(TEXT("state"), TEXT("companion-guid")), {StringSegment(TEXT("Tasks"))}));
    FSalQuery EntriesQuery;
    TestTrue(TEXT("StateTree Palette search accepts an exact destination"), DecodeQuery(Entries, EntriesQuery));

    const TSharedRef<FJsonObject> Palette = MakeKindOperation(TEXT("palette"));
    Palette->SetStringField(TEXT("id"), TEXT("P_State"));
    Palette->SetObjectField(
        TEXT("to"),
        MakeMemberRef(MakeLocalRef(TEXT("tree")), {StringSegment(TEXT("SubTrees"))}));
    FSalQuery PaletteQuery;
    TestTrue(TEXT("StateTree exact Palette read accepts the bound target destination"), DecodeQuery(Palette, PaletteQuery));

    const TSharedRef<FJsonObject> UnknownDestination = MakeKindOperation(TEXT("palette_entries"));
    UnknownDestination->SetObjectField(
        TEXT("to"),
        MakeMemberRef(MakeLocalRef(TEXT("other")), {StringSegment(TEXT("SubTrees"))}));
    FSalQuery InvalidQuery;
    TestFalse(TEXT("Palette destination cannot reference an undeclared local alias"), DecodeQuery(UnknownDestination, InvalidQuery));

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FSalStateTreeIndexedPathContractTest,
    "Loomle.Sal.Contract.StateTree.IndexedMemberPath",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSalStateTreeIndexedPathContractTest::RunTest(const FString& Parameters)
{
    const TSharedRef<FJsonObject> References = MakeKindOperation(TEXT("references"));
    References->SetObjectField(
        TEXT("target"),
        MakeMemberRef(
            MakeStableRef(TEXT("parameter"), TEXT("container-guid/property-guid")),
            {IndexSegment(0), StringSegment(TEXT("X"))}));
    FSalQuery Query;
    TestTrue(TEXT("Parameter array index normalizes into a numeric member path segment"), DecodeQuery(References, Query));

    const TSharedRef<FJsonObject> BracketReference = MakeKindOperation(TEXT("references"));
    BracketReference->SetObjectField(
        TEXT("target"),
        MakeStableRef(TEXT("parameter"), TEXT("container-guid/property-guid[0]")));
    FSalQuery InvalidQuery;
    TestFalse(TEXT("A StableRef id cannot absorb an indexed member suffix"), DecodeQuery(BracketReference, InvalidQuery));

    const TSharedRef<FJsonObject> BracketExact = MakeKindOperation(TEXT("parameter"));
    BracketExact->SetStringField(TEXT("id"), TEXT("container-guid/property-guid[0]"));
    TestFalse(TEXT("An exact Parameter id cannot absorb an indexed member suffix"), DecodeQuery(BracketExact, InvalidQuery));

    for (const FString& Kind : {TEXT("state"), TEXT("node"), TEXT("transition")})
    {
        const TSharedRef<FJsonObject> BracketObject = MakeKindOperation(Kind);
        BracketObject->SetStringField(TEXT("id"), TEXT("authored-guid[0]"));
        TestFalse(
            *FString::Printf(TEXT("An exact %s id rejects a bracket suffix"), *Kind),
            DecodeQuery(BracketObject, InvalidQuery));
    }

    const TSharedRef<FJsonObject> NegativeIndex = MakeKindOperation(TEXT("references"));
    NegativeIndex->SetObjectField(
        TEXT("target"),
        MakeMemberRef(
            MakeStableRef(TEXT("parameter"), TEXT("container-guid/property-guid")),
            {MakeShared<FJsonValueNumber>(-1), StringSegment(TEXT("X"))}));
    TestFalse(TEXT("Member path indexes must be non-negative"), DecodeQuery(NegativeIndex, InvalidQuery));

    const TSharedRef<FJsonObject> OverflowIndex = MakeKindOperation(TEXT("references"));
    OverflowIndex->SetObjectField(
        TEXT("target"),
        MakeMemberRef(
            MakeStableRef(TEXT("parameter"), TEXT("container-guid/property-guid")),
            {MakeShared<FJsonValueNumber>(static_cast<double>(MAX_int32) + 1.0), StringSegment(TEXT("X"))}));
    TestFalse(TEXT("Member path indexes cannot exceed int32"), DecodeQuery(OverflowIndex, InvalidQuery));

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FSalStateTreeBindingContractTest,
    "Loomle.Sal.Contract.StateTree.Binding",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSalStateTreeBindingContractTest::RunTest(const FString& Parameters)
{
    const TSharedRef<FJsonObject> ConstructorArgs = MakeShared<FJsonObject>();
    ConstructorArgs->SetStringField(TEXT("palette"), TEXT("P_ClampPropertyFunction"));
    const TSharedRef<FJsonObject> FunctionBinding = MakeShared<FJsonObject>();
    FunctionBinding->SetObjectField(TEXT("target"), MakeLocalRef(TEXT("clamp")));
    FunctionBinding->SetObjectField(TEXT("value"), MakeCall(TEXT("node"), ConstructorArgs));

    const TSharedRef<FJsonObject> BindFunction = MakeKindOperation(TEXT("bind"));
    BindFunction->SetObjectField(
        TEXT("from"),
        MakeMemberRef(
            MakeLocalRef(TEXT("clamp")),
            {StringSegment(TEXT("Instance")), StringSegment(TEXT("Output"))}));
    BindFunction->SetObjectField(
        TEXT("to"),
        MakeMemberRef(
            MakeStableRef(TEXT("node"), TEXT("task-guid")),
            {StringSegment(TEXT("Instance")), StringSegment(TEXT("Targets")), IndexSegment(1), StringSegment(TEXT("Value"))}));

    const TSharedRef<FJsonObject> Unbind = MakeKindOperation(TEXT("unbind"));
    Unbind->SetObjectField(
        TEXT("from"),
        MakeStableRef(TEXT("parameter"), TEXT("container-guid/old-threshold-guid")));
    Unbind->SetObjectField(
        TEXT("to"),
        MakeMemberRef(
            MakeStableRef(TEXT("node"), TEXT("guard-guid")),
            {StringSegment(TEXT("Instance")), StringSegment(TEXT("Threshold"))}));

    FSalPatch Patch;
    TestTrue(
        TEXT("StateTree bind and unbind preserve data-flow order and declared local aliases"),
        DecodePatch(
            {
                MakeShared<FJsonValueObject>(FunctionBinding),
                MakeShared<FJsonValueObject>(BindFunction),
                MakeShared<FJsonValueObject>(Unbind),
            },
            Patch));

    const TSharedRef<FJsonObject> UndeclaredBind = MakeKindOperation(TEXT("bind"));
    UndeclaredBind->SetObjectField(
        TEXT("from"),
        MakeMemberRef(MakeLocalRef(TEXT("missing")), {StringSegment(TEXT("Instance")), StringSegment(TEXT("Output"))}));
    UndeclaredBind->SetObjectField(
        TEXT("to"),
        MakeMemberRef(MakeStableRef(TEXT("node"), TEXT("task-guid")), {StringSegment(TEXT("Instance")), StringSegment(TEXT("Input"))}));
    FSalPatch InvalidPatch;
    TestFalse(
        TEXT("StateTree bind cannot use an undeclared local Property Function alias"),
        DecodePatch({MakeShared<FJsonValueObject>(UndeclaredBind)}, InvalidPatch));

    return true;
}

#endif
