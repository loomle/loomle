// Copyright 2026 Loomle contributors.

#if WITH_DEV_AUTOMATION_TESTS

#include "Sal/Reference/SalReferenceIndex.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "FindInBlueprintManager.h"
#include "Internationalization/StringTableCore.h"
#include "Misc/AutomationTest.h"
#include "Serialization/MemoryWriter.h"

namespace
{
class FLookupBuilder
{
public:
    FString Token(const FString& Source)
    {
        if (const int32* Existing = Indices.Find(Source))
        {
            return LexToString(*Existing);
        }
        const int32 Index = Values.Num();
        Indices.Add(Source, Index);
        Values.Add(Index, FText::FromString(Source));
        return LexToString(Index);
    }

    TSharedPtr<FJsonValue> String(const FString& Source)
    {
        return MakeShared<FJsonValueString>(Token(Source));
    }

    void Field(
        const TSharedPtr<FJsonObject>& Object,
        const FString& Name,
        const TSharedPtr<FJsonValue>& Value)
    {
        Object->SetField(Token(Name), Value);
    }

    TMap<int32, FText> Values;

private:
    TMap<FString, int32> Indices;
};

TSharedPtr<FJsonObject> MakeVariableReference(
    FLookupBuilder& Lookup,
    const FGuid& Guid,
    const FString& Name,
    const bool bSelfContext,
    const bool bIncludeSelfContext = true)
{
    TSharedPtr<FJsonObject> GuidObject = MakeShared<FJsonObject>();
    Lookup.Field(GuidObject, TEXT("A"), MakeShared<FJsonValueNumber>(Guid.A));
    Lookup.Field(GuidObject, TEXT("B"), MakeShared<FJsonValueNumber>(Guid.B));
    Lookup.Field(GuidObject, TEXT("C"), MakeShared<FJsonValueNumber>(Guid.C));
    Lookup.Field(GuidObject, TEXT("D"), MakeShared<FJsonValueNumber>(Guid.D));

    TSharedPtr<FJsonObject> Reference = MakeShared<FJsonObject>();
    Lookup.Field(Reference, TEXT("MemberScope"), Lookup.String(TEXT("")));
    Lookup.Field(Reference, TEXT("MemberGuid"), MakeShared<FJsonValueObject>(GuidObject));
    Lookup.Field(Reference, TEXT("MemberName"), Lookup.String(Name));
    if (bIncludeSelfContext)
    {
        Lookup.Field(Reference, TEXT("bSelfContext"), MakeShared<FJsonValueBoolean>(bSelfContext));
    }
    return Reference;
}

TSharedPtr<FJsonObject> MakeNode(
    FLookupBuilder& Lookup,
    const FString& NodeGuid,
    const TSharedPtr<FJsonObject>& VariableReference)
{
    TSharedPtr<FJsonObject> Node = MakeShared<FJsonObject>();
    Lookup.Field(Node, TEXT("Name"), Lookup.String(TEXT("Get Health")));
    Lookup.Field(Node, TEXT("ClassName"), Lookup.String(TEXT("K2Node_VariableGet")));
    Lookup.Field(Node, TEXT("NodeGuid"), Lookup.String(NodeGuid));
    Lookup.Field(Node, TEXT("VariableReference"), MakeShared<FJsonValueObject>(VariableReference));
    return Node;
}

TSharedPtr<FJsonObject> MakeGraph(
    FLookupBuilder& Lookup,
    const FString& Name,
    const TArray<TSharedPtr<FJsonValue>>& Nodes)
{
    TSharedPtr<FJsonObject> Graph = MakeShared<FJsonObject>();
    Lookup.Field(Graph, TEXT("Name"), Lookup.String(Name));
    Lookup.Field(Graph, TEXT("Nodes"), MakeShared<FJsonValueArray>(Nodes));
    return Graph;
}

TSharedPtr<FJsonObject> MakeRoot(
    FLookupBuilder& Lookup,
    const TArray<TSharedPtr<FJsonValue>>& Graphs)
{
    TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
    Lookup.Field(Root, TEXT("Uber"), MakeShared<FJsonValueArray>(Graphs));
    return Root;
}

FString EncodedInt32(const int32 Value)
{
    TArray<uint8> Bytes;
    FMemoryWriter Writer(Bytes);
    int32 Copy = Value;
    Writer << Copy;
    return BytesToString(Bytes.GetData(), Bytes.Num());
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FSalReferenceIndexExactVariableTest,
    "Loomle.Sal.ReferenceIndex.ExactVariable",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSalReferenceIndexExactVariableTest::RunTest(const FString& Parameters)
{
    const FGuid VariableGuid(0x12345678, 0x9abcdef0, 0x13572468, 0x24681357);
    const FString NodeGuid = TEXT("00112233445566778899aabbccddeeff");
    FLookupBuilder Lookup;
    const TSharedPtr<FJsonObject> Reference = MakeVariableReference(Lookup, VariableGuid, TEXT("Health"), true);
    const TSharedPtr<FJsonObject> Node = MakeNode(Lookup, NodeGuid, Reference);
    const TSharedPtr<FJsonObject> Graph = MakeGraph(
        Lookup,
        TEXT("EventGraph"),
        {MakeShared<FJsonValueObject>(Node)});

    Loomle::Sal::FReferenceIndexTarget Target;
    Target.Identity.Kind = Loomle::Sal::EReferenceDeclarationKind::BlueprintMemberVariable;
    Target.Identity.OwnerPath = TEXT("/Game/BP_Target.BP_Target");
    Target.Identity.Guid = VariableGuid;
    Target.Identity.Name = TEXT("Health");
    Target.OwnerClassPath = FTopLevelAssetPath(TEXT("/Game/BP_Target.BP_Target_C"));
    const TSet<FTopLevelAssetPath> CandidateLineage = {Target.OwnerClassPath};

    const Loomle::Sal::FReferenceIndexScanResult Result =
        Loomle::Sal::FSalReferenceIndex::ScanDecodedForTesting(
            MakeRoot(Lookup, {MakeShared<FJsonValueObject>(Graph)}),
            Lookup.Values,
            Target.Identity.OwnerPath,
            CandidateLineage,
            Target);

    TestTrue(TEXT("Index parses"), Result.Status == Loomle::Sal::EReferenceIndexScanStatus::Parsed);
    TestEqual(TEXT("One exact use-site"), Result.Sites.Num(), 1);
    if (Result.Sites.Num() == 1)
    {
        TestEqual(
            TEXT("Graph display provenance retained"),
            Result.Sites[0].GraphDisplayName,
            FString(TEXT("EventGraph")));
        TestEqual(
            TEXT("Node GUID normalized"),
            Result.Sites[0].NodeId,
            FString(TEXT("00112233-4455-6677-8899-aabbccddeeff")));
        TestEqual(TEXT("Native field provenance retained"), Result.Sites[0].MatchedPath, FString(TEXT("VariableReference")));
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FSalReferenceIndexAmbiguousNodeTest,
    "Loomle.Sal.ReferenceIndex.AmbiguousNodeLocator",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSalReferenceIndexAmbiguousNodeTest::RunTest(const FString& Parameters)
{
    const FGuid VariableGuid(1, 2, 3, 4);
    FLookupBuilder Lookup;
    const TSharedPtr<FJsonObject> Node = MakeNode(
        Lookup,
        TEXT("00112233445566778899aabbccddeeff"),
        MakeVariableReference(Lookup, VariableGuid, TEXT("Health"), true));
    const TSharedPtr<FJsonObject> First = MakeGraph(
        Lookup,
        TEXT("SharedName"),
        {MakeShared<FJsonValueObject>(Node)});
    const TSharedPtr<FJsonObject> Second = MakeGraph(
        Lookup,
        TEXT("OtherDisplayName"),
        {MakeShared<FJsonValueObject>(MakeNode(
            Lookup,
            TEXT("00112233445566778899aabbccddeeff"),
            MakeVariableReference(Lookup, FGuid(5, 6, 7, 8), TEXT("Other"), true))) });

    Loomle::Sal::FReferenceIndexTarget Target;
    Target.Identity.Kind = Loomle::Sal::EReferenceDeclarationKind::BlueprintMemberVariable;
    Target.Identity.OwnerPath = TEXT("/Game/BP_Target.BP_Target");
    Target.Identity.Guid = VariableGuid;
    Target.OwnerClassPath = FTopLevelAssetPath(TEXT("/Game/BP_Target.BP_Target_C"));

    const Loomle::Sal::FReferenceIndexScanResult Result =
        Loomle::Sal::FSalReferenceIndex::ScanDecodedForTesting(
            MakeRoot(Lookup, {
                MakeShared<FJsonValueObject>(First),
                MakeShared<FJsonValueObject>(Second)}),
            Lookup.Values,
            Target.Identity.OwnerPath,
            {Target.OwnerClassPath},
            Target);

    TestTrue(
        TEXT("Ambiguous NodeGuid fails closed"),
        Result.Status == Loomle::Sal::EReferenceIndexScanStatus::Corrupt);
    TestTrue(TEXT("No ambiguous site is emitted"), Result.Sites.IsEmpty());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FSalReferenceIndexIncompleteAncestryTest,
    "Loomle.Sal.ReferenceIndex.IncompleteAncestryFailsClosed",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSalReferenceIndexIncompleteAncestryTest::RunTest(const FString& Parameters)
{
    const FGuid VariableGuid(1, 2, 3, 4);
    FLookupBuilder Lookup;
    const TSharedPtr<FJsonObject> Node = MakeNode(
        Lookup,
        TEXT("00112233445566778899aabbccddeeff"),
        MakeVariableReference(Lookup, VariableGuid, TEXT("Health"), true));

    Loomle::Sal::FReferenceIndexTarget Target;
    Target.Identity.Kind = Loomle::Sal::EReferenceDeclarationKind::BlueprintMemberVariable;
    Target.Identity.OwnerPath = TEXT("/Game/BP_Target.BP_Target");
    Target.Identity.Guid = VariableGuid;
    Target.OwnerClassPath = FTopLevelAssetPath(TEXT("/Game/BP_Target.BP_Target_C"));

    const Loomle::Sal::FReferenceIndexScanResult Result =
        Loomle::Sal::FSalReferenceIndex::ScanDecodedForTesting(
            MakeRoot(Lookup, {MakeShared<FJsonValueObject>(MakeGraph(
                Lookup,
                TEXT("EventGraph"),
                {MakeShared<FJsonValueObject>(Node)}))}),
            Lookup.Values,
            TEXT("/Game/BP_UnknownChild.BP_UnknownChild"),
            {},
            Target,
            false);

    TestTrue(
        TEXT("Unknown class ancestry fails closed"),
        Result.Status == Loomle::Sal::EReferenceIndexScanStatus::Corrupt);
    TestTrue(TEXT("No unverified inherited site is emitted"), Result.Sites.IsEmpty());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FSalReferenceIndexMissingOwnerContextTest,
    "Loomle.Sal.ReferenceIndex.MissingOwnerContextFailsClosed",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSalReferenceIndexMissingOwnerContextTest::RunTest(const FString& Parameters)
{
    const FGuid VariableGuid(1, 2, 3, 4);
    FLookupBuilder Lookup;
    const TSharedPtr<FJsonObject> Node = MakeNode(
        Lookup,
        TEXT("00112233445566778899aabbccddeeff"),
        MakeVariableReference(Lookup, VariableGuid, TEXT("Health"), true, false));

    Loomle::Sal::FReferenceIndexTarget Target;
    Target.Identity.Kind = Loomle::Sal::EReferenceDeclarationKind::BlueprintMemberVariable;
    Target.Identity.OwnerPath = TEXT("/Game/BP_Target.BP_Target");
    Target.Identity.Guid = VariableGuid;
    Target.OwnerClassPath = FTopLevelAssetPath(TEXT("/Game/BP_Target.BP_Target_C"));

    const Loomle::Sal::FReferenceIndexScanResult Result =
        Loomle::Sal::FSalReferenceIndex::ScanDecodedForTesting(
            MakeRoot(Lookup, {MakeShared<FJsonValueObject>(MakeGraph(
                Lookup,
                TEXT("EventGraph"),
                {MakeShared<FJsonValueObject>(Node)}))}),
            Lookup.Values,
            Target.Identity.OwnerPath,
            {Target.OwnerClassPath},
            Target);

    TestTrue(
        TEXT("A matching identity without owner context fails closed"),
        Result.Status == Loomle::Sal::EReferenceIndexScanStatus::Corrupt);
    TestTrue(TEXT("No unverified site is emitted"), Result.Sites.IsEmpty());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FSalReferenceIndexLayoutValidationTest,
    "Loomle.Sal.ReferenceIndex.EncodedLayoutValidation",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSalReferenceIndexLayoutValidationTest::RunTest(const FString& Parameters)
{
    FString Error;
    const FString Valid = EncodedInt32(EFiBVersion::FIB_VER_LATEST)
        + EncodedInt32(sizeof(int32))
        + EncodedInt32(0)
        + TEXT("{}");
    TestTrue(
        TEXT("Bounded FiB header is accepted"),
        Loomle::Sal::FSalReferenceIndex::ValidateEncodedLayoutForTesting(Valid, true, Error));

    Error.Reset();
    const FString Corrupt = EncodedInt32(EFiBVersion::FIB_VER_LATEST)
        + EncodedInt32(1024)
        + TEXT("{}");
    TestFalse(
        TEXT("Out-of-bounds FiB lookup table is rejected before UE decoding"),
        Loomle::Sal::FSalReferenceIndex::ValidateEncodedLayoutForTesting(Corrupt, true, Error));
    TestFalse(TEXT("Corrupt layout explains the failure"), Error.IsEmpty());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FSalReferenceIndexStringTableFailClosedTest,
    "Loomle.Sal.ReferenceIndex.StringTableLookupFailsClosed",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSalReferenceIndexStringTableFailClosedTest::RunTest(const FString& Parameters)
{
    TMap<int32, FText> Lookup;
    Lookup.Add(
        0,
        FText::FromStringTable(
            TEXT("/Game/LoomleMissingStringTable.LoomleMissingStringTable"),
            FTextKey(TEXT("Key")),
            EStringTableLoadingPolicy::Find));

    TestTrue(
        TEXT("String-Table-backed FiB text requires asset resolution"),
        Loomle::Sal::FSalReferenceIndex::LookupRequiresAssetResolutionForTesting(Lookup));
    return true;
}

#endif
