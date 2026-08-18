// Copyright 2026 Loomle contributors.

#if WITH_DEV_AUTOMATION_TESTS

#include "Sal/SalJson.h"
#include "Sal/SalModule.h"
#include "Sal/Asset/SalAssetInterface.h"
#include "Sal/Blueprint/SalBlueprintInterface.h"
#include "Sal/Class/SalClassInterface.h"
#include "Sal/Graph/SalGraphInterface.h"
#include "Sal/StateTree/SalStateTreeInterface.h"
#include "Sal/Widget/SalWidgetInterface.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Misc/AutomationTest.h"

namespace
{
using namespace Loomle::Sal;

constexpr const TCHAR* ActorId =
    TEXT("11111111-1111-1111-1111-111111111111");

TSharedRef<FJsonObject> DomainTarget(const FString& Domain)
{
    TSharedRef<FJsonObject> Target = MakeShared<FJsonObject>();
    Target->SetStringField(TEXT("kind"), TEXT("target"));
    Target->SetStringField(TEXT("domain"), Domain);
    return Target;
}

TSharedRef<FJsonObject> AssetDomainTarget(
    const FString& Domain,
    const FString& Type = FString())
{
    TSharedRef<FJsonObject> Target = DomainTarget(Domain);
    Target->SetStringField(
        TEXT("asset"),
        Domain == TEXT("level")
            ? TEXT("/Game/Maps/Forest.Forest")
            : TEXT("/Game/PCG/PCG_Forest.PCG_Forest"));
    if (!Type.IsEmpty())
    {
        Target->SetStringField(TEXT("type"), Type);
    }
    return Target;
}

TSharedRef<FJsonObject> PcgComponentTarget(
    const FString& Source = TEXT("native"),
    const FString& Id = TEXT("PCGComponent"))
{
    TSharedRef<FJsonObject> Target = DomainTarget(TEXT("pcg_component"));
    Target->SetStringField(TEXT("asset"), TEXT("/Game/Maps/Forest.Forest"));
    Target->SetStringField(TEXT("actorId"), ActorId);
    Target->SetStringField(TEXT("source"), Source);
    Target->SetStringField(TEXT("id"), Id);
    Target->SetStringField(TEXT("type"), TEXT("/Script/PCG.PCGComponent"));
    return Target;
}

TSharedRef<FJsonObject> TargetBinding(
    const FString& Alias,
    const TSharedRef<FJsonObject>& Target)
{
    TSharedRef<FJsonObject> Binding = MakeShared<FJsonObject>();
    Binding->SetStringField(TEXT("alias"), Alias);
    Binding->SetObjectField(TEXT("target"), Target);
    return Binding;
}

TSharedRef<FJsonObject> Handoff(
    const FString& Purpose,
    const FString& Alias)
{
    TSharedRef<FJsonObject> Ref = MakeShared<FJsonObject>();
    Ref->SetStringField(TEXT("kind"), TEXT("local"));
    Ref->SetStringField(TEXT("name"), Alias);

    TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetStringField(TEXT("kind"), TEXT("target_handoff"));
    Result->SetStringField(TEXT("purpose"), Purpose);
    Result->SetObjectField(TEXT("target"), Ref);
    return Result;
}

TSharedRef<FJsonObject> QueryArguments(
    const TSharedRef<FJsonObject>& Target,
    const FString& OperationKind = TEXT("target"),
    const FString& SearchText = FString())
{
    TSharedRef<FJsonObject> Operation = MakeShared<FJsonObject>();
    Operation->SetStringField(TEXT("kind"), OperationKind);
    if (!SearchText.IsEmpty())
    {
        Operation->SetStringField(TEXT("text"), SearchText);
    }

    TSharedRef<FJsonObject> Query = MakeShared<FJsonObject>();
    Query->SetStringField(TEXT("kind"), TEXT("query"));
    Query->SetObjectField(
        TEXT("target"),
        TargetBinding(TEXT("scene_target"), Target));
    Query->SetObjectField(TEXT("operation"), Operation);

    TSharedRef<FJsonObject> Arguments = MakeShared<FJsonObject>();
    Arguments->SetObjectField(TEXT("object"), Query);
    return Arguments;
}

TSharedRef<FJsonObject> ObjectQueryArguments(
    const TSharedRef<FJsonObject>& Target,
    const FString& IdentitySegment)
{
    TSharedRef<FJsonObject> StableRef = MakeShared<FJsonObject>();
    StableRef->SetStringField(TEXT("kind"), TEXT("stable_ref"));
    StableRef->SetArrayField(
        TEXT("identityPath"),
        {MakeShared<FJsonValueString>(IdentitySegment)});

    TSharedRef<FJsonObject> Operation = MakeShared<FJsonObject>();
    Operation->SetStringField(TEXT("kind"), TEXT("object"));
    Operation->SetObjectField(TEXT("target"), StableRef);

    TSharedRef<FJsonObject> Query = MakeShared<FJsonObject>();
    Query->SetStringField(TEXT("kind"), TEXT("query"));
    Query->SetObjectField(
        TEXT("target"),
        TargetBinding(TEXT("scene_target"), Target));
    Query->SetObjectField(TEXT("operation"), Operation);

    TSharedRef<FJsonObject> Arguments = MakeShared<FJsonObject>();
    Arguments->SetObjectField(TEXT("object"), Query);
    return Arguments;
}

TSharedRef<FJsonObject> PatchArguments(
    const TSharedRef<FJsonObject>& Target)
{
    TSharedRef<FJsonObject> Save = MakeShared<FJsonObject>();
    Save->SetStringField(TEXT("kind"), TEXT("save"));

    TSharedRef<FJsonObject> Patch = MakeShared<FJsonObject>();
    Patch->SetStringField(TEXT("kind"), TEXT("patch"));
    Patch->SetObjectField(
        TEXT("target"),
        TargetBinding(TEXT("scene_target"), Target));
    Patch->SetBoolField(TEXT("dryRun"), true);
    Patch->SetArrayField(
        TEXT("statements"),
        {MakeShared<FJsonValueObject>(Save)});

    TSharedRef<FJsonObject> Arguments = MakeShared<FJsonObject>();
    Arguments->SetObjectField(TEXT("object"), Patch);
    return Arguments;
}

bool DecodeQueryTarget(const TSharedRef<FJsonObject>& Target)
{
    FSalQuery Query;
    TSharedPtr<FJsonObject> Error;
    return FSalJson::DecodeQuery(QueryArguments(Target), Query, Error);
}

bool DecodePatchTarget(const TSharedRef<FJsonObject>& Target)
{
    FSalPatch Patch;
    TSharedPtr<FJsonObject> Error;
    return FSalJson::DecodePatch(PatchArguments(Target), Patch, Error);
}

bool HasDiagnostic(
    const TSharedPtr<FJsonObject>& Result,
    const FString& ExpectedCode,
    const FString& MessageFragment = FString())
{
    const TArray<TSharedPtr<FJsonValue>>* Diagnostics = nullptr;
    if (!Result.IsValid()
        || !Result->TryGetArrayField(TEXT("diagnostics"), Diagnostics)
        || Diagnostics == nullptr)
    {
        return false;
    }
    for (const TSharedPtr<FJsonValue>& DiagnosticValue : *Diagnostics)
    {
        const TSharedPtr<FJsonObject>* Diagnostic = nullptr;
        FString Code;
        FString Message;
        if (DiagnosticValue.IsValid()
            && DiagnosticValue->TryGetObject(Diagnostic)
            && Diagnostic != nullptr
            && (*Diagnostic)->TryGetStringField(TEXT("code"), Code)
            && Code == ExpectedCode
            && (MessageFragment.IsEmpty()
                || ((*Diagnostic)->TryGetStringField(TEXT("message"), Message)
                    && Message.Contains(MessageFragment))))
        {
            return true;
        }
    }
    return false;
}

bool HasTargetContext(
    const TSharedPtr<FJsonObject>& Result,
    const FString& ExpectedContext)
{
    FString Context;
    return Result.IsValid()
        && Result->TryGetStringField(TEXT("targetContext"), Context)
        && Context == ExpectedContext;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FSalTargetAdmissionModesTest,
    "Loomle.Sal.Phase0.Protocol.TargetAdmissionModes",
    EAutomationTestFlags::EditorContext
        | EAutomationTestFlags::EngineFilter)

bool FSalTargetAdmissionModesTest::RunTest(const FString& Parameters)
{
    using FStableLowerer = bool (*)(
        const FSalResolvedTarget&,
        const TArray<FString>&,
        FString&,
        FString&,
        FString&,
        FString&);
    struct FLegacyDomainCase
    {
        const TCHAR* Name;
        ESalDomain Domain;
        FStableLowerer Lowerer;
    };
    const FLegacyDomainCase LegacyDomains[] = {
        {TEXT("asset"), ESalDomain::Asset, &FSalAssetInterface::LowerStableReference},
        {TEXT("blueprint"), ESalDomain::Blueprint, &FSalBlueprintInterface::LowerStableReference},
        {TEXT("class"), ESalDomain::Class, &FSalClassInterface::LowerStableReference},
        {TEXT("graph"), ESalDomain::Graph, &FSalGraphInterface::LowerStableReference},
        {TEXT("state_tree"), ESalDomain::StateTree, &FSalStateTreeInterface::LowerStableReference},
        {TEXT("widget"), ESalDomain::Widget, &FSalWidgetInterface::LowerStableReference}};
    for (const FLegacyDomainCase& DomainCase : LegacyDomains)
    {
        FSalResolvedTarget Target;
        Target.Domain = DomainCase.Domain;
        FString LegacyKind;
        FString LegacyId;
        FString Code;
        FString LoweringMessage;
        TestFalse(
            *FString::Printf(
                TEXT("%s adapter rejects a non-Guid StableRef identity"),
                DomainCase.Name),
            DomainCase.Lowerer(
                Target,
                {TEXT("not-a-guid")},
                LegacyKind,
                LegacyId,
                Code,
                LoweringMessage));
        TestEqual(
            *FString::Printf(
                TEXT("%s adapter owns the legacy invalid-reference diagnostic"),
                DomainCase.Name),
            Code,
            FString(TEXT("validation.invalid_reference")));
    }

    const TSharedRef<FJsonObject> LevelDiscovery =
        AssetDomainTarget(TEXT("level"));
    const TSharedRef<FJsonObject> PcgDiscovery =
        AssetDomainTarget(TEXT("pcg"));
    FString Message;

    TestTrue(
        TEXT("Level Query admission accepts discovery without type"),
        DecodeQueryTarget(LevelDiscovery));
    FSalQuery ActorsQuery;
    TSharedPtr<FJsonObject> ActorsError;
    TestTrue(
        TEXT("Protocol v6 accepts the Level actors collection operation with search text"),
        FSalJson::DecodeQuery(
            QueryArguments(LevelDiscovery, TEXT("actors"), TEXT("light")),
            ActorsQuery,
            ActorsError));
    FString ActorsKind;
    FString ActorsSearch;
    TestTrue(
        TEXT("Decoded actors operation retains its normalized kind"),
        ActorsQuery.Operation.IsValid()
            && ActorsQuery.Operation->TryGetStringField(TEXT("kind"), ActorsKind));
    TestEqual(
        TEXT("Decoded actors operation kind remains actors"),
        ActorsKind,
        FString(TEXT("actors")));
    TestTrue(
        TEXT("Decoded actors operation retains optional search text"),
        ActorsQuery.Operation.IsValid()
            && ActorsQuery.Operation->TryGetStringField(TEXT("text"), ActorsSearch));
    TestEqual(
        TEXT("Decoded actors search text remains exact"),
        ActorsSearch,
        FString(TEXT("light")));
    TestFalse(
        TEXT("Canonical Result admission rejects a discovery-only Level Target"),
        FSalJson::ValidateCanonicalTarget(LevelDiscovery, Message));

    const TSharedRef<FJsonObject> CanonicalLevel =
        AssetDomainTarget(TEXT("level"), TEXT("/Script/Engine.World"));
    Message.Reset();
    TestTrue(
        TEXT("Canonical Result admission accepts an exact Level Target"),
        FSalJson::ValidateCanonicalTarget(CanonicalLevel, Message));
    TestTrue(
        TEXT("Patch admission accepts an exact Level Target after the "
            "authored-mutation capability bump"),
        DecodePatchTarget(CanonicalLevel));

    TestTrue(
        TEXT("PCG Query admission accepts discovery without type"),
        DecodeQueryTarget(PcgDiscovery));
    Message.Reset();
    TestFalse(
        TEXT("Canonical Result admission rejects a discovery-only PCG Target"),
        FSalJson::ValidateCanonicalTarget(PcgDiscovery, Message));
    const TSharedRef<FJsonObject> CanonicalPcg =
        AssetDomainTarget(TEXT("pcg"), TEXT("/Script/PCG.PCGGraph"));
    Message.Reset();
    TestTrue(
        TEXT("Canonical Result admission accepts an exact PCG Target"),
        FSalJson::ValidateCanonicalTarget(CanonicalPcg, Message));
    TestTrue(
        TEXT("Patch admission accepts an exact PCG Target after the "
            "authored-mutation capability bump"),
        DecodePatchTarget(CanonicalPcg));

    const TSharedRef<FJsonObject> Component = PcgComponentTarget();
    TestTrue(
        TEXT("pcg_component passes Query admission"),
        DecodeQueryTarget(Component));
    Message.Reset();
    TestTrue(
        TEXT("pcg_component passes canonical Result admission"),
        FSalJson::ValidateCanonicalTarget(Component, Message));
    TestTrue(
        TEXT("pcg_component passes Patch admission with the edit-guard bump"),
        DecodePatchTarget(Component));
    TestFalse(
        TEXT("pcg_component rejects an open-ended source kind"),
        DecodeQueryTarget(PcgComponentTarget(TEXT("runtime"))));

    TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetStringField(TEXT("targetContext"), TEXT("exact_target"));
    Result->SetObjectField(
        TEXT("target"),
        TargetBinding(TEXT("component_target"), Component));
    Result->SetArrayField(
        TEXT("diagnostics"),
        TArray<TSharedPtr<FJsonValue>>{});
    TSharedPtr<FJsonObject> ValidationError;
    TestTrue(
        TEXT("A Query-only pcg_component remains legal as a canonical Result Target"),
        FSalJson::ValidateResult(Result, ValidationError));

    TSharedRef<FJsonObject> RelatedResult = MakeShared<FJsonObject>();
    RelatedResult->SetStringField(TEXT("targetContext"), TEXT("exact_target"));
    RelatedResult->SetObjectField(
        TEXT("target"),
        TargetBinding(TEXT("level_target"), CanonicalLevel));
    RelatedResult->SetArrayField(
        TEXT("relatedTargets"),
        {MakeShared<FJsonValueObject>(
            TargetBinding(TEXT("component_target"), Component))});
    RelatedResult->SetArrayField(
        TEXT("handoffs"),
        {MakeShared<FJsonValueObject>(
            Handoff(TEXT("configure_pcg_component"), TEXT("component_target")))});
    RelatedResult->SetArrayField(
        TEXT("diagnostics"),
        TArray<TSharedPtr<FJsonValue>>{});
    ValidationError.Reset();
    TestTrue(
        TEXT("pcg_component is legal as a canonical related Target"),
        FSalJson::ValidateResult(RelatedResult, ValidationError));

    TSharedRef<FJsonObject> DuplicateRelated = MakeShared<FJsonObject>();
    DuplicateRelated->SetStringField(TEXT("targetContext"), TEXT("exact_target"));
    DuplicateRelated->SetObjectField(
        TEXT("target"),
        TargetBinding(TEXT("level_target"), CanonicalLevel));
    DuplicateRelated->SetArrayField(
        TEXT("relatedTargets"),
        {
            MakeShared<FJsonValueObject>(
                TargetBinding(TEXT("component_a"), Component)),
            MakeShared<FJsonValueObject>(
                TargetBinding(TEXT("component_b"), Component))});
    DuplicateRelated->SetArrayField(
        TEXT("handoffs"),
        {
            MakeShared<FJsonValueObject>(
                Handoff(TEXT("configure_a"), TEXT("component_a"))),
            MakeShared<FJsonValueObject>(
                Handoff(TEXT("configure_b"), TEXT("component_b")))});
    DuplicateRelated->SetArrayField(
        TEXT("diagnostics"),
        TArray<TSharedPtr<FJsonValue>>{});
    ValidationError.Reset();
    TestFalse(
        TEXT("Duplicate canonical pcg_component related Targets conflict"),
        FSalJson::ValidateResult(DuplicateRelated, ValidationError));

    const TSharedRef<FJsonObject> InstanceComponent =
        PcgComponentTarget(TEXT("instance"), TEXT("PCGComponentInstance"));
    TSharedRef<FJsonObject> DistinctRelated = MakeShared<FJsonObject>();
    DistinctRelated->SetStringField(TEXT("targetContext"), TEXT("exact_target"));
    DistinctRelated->SetObjectField(
        TEXT("target"),
        TargetBinding(TEXT("level_target"), CanonicalLevel));
    DistinctRelated->SetArrayField(
        TEXT("relatedTargets"),
        {
            MakeShared<FJsonValueObject>(
                TargetBinding(TEXT("native_component"), Component)),
            MakeShared<FJsonValueObject>(
                TargetBinding(TEXT("instance_component"), InstanceComponent))});
    DistinctRelated->SetArrayField(
        TEXT("handoffs"),
        {
            MakeShared<FJsonValueObject>(
                Handoff(TEXT("configure_native"), TEXT("native_component"))),
            MakeShared<FJsonValueObject>(
                Handoff(TEXT("configure_instance"), TEXT("instance_component")))});
    DistinctRelated->SetArrayField(
        TEXT("diagnostics"),
        TArray<TSharedPtr<FJsonValue>>{});
    ValidationError.Reset();
    TestTrue(
        TEXT("Different pcg_component source and id values remain distinct canonical Targets"),
        FSalJson::ValidateResult(DistinctRelated, ValidationError));

    TSharedRef<FJsonObject> BlueprintDiscovery = DomainTarget(TEXT("blueprint"));
    BlueprintDiscovery->SetStringField(TEXT("asset"), TEXT("/Game/BP_Test.BP_Test"));
    TestTrue(
        TEXT("Existing Blueprint Query discovery remains admitted"),
        DecodeQueryTarget(BlueprintDiscovery));
    Message.Reset();
    TestFalse(
        TEXT("Existing Blueprint canonical Result still requires an id"),
        FSalJson::ValidateCanonicalTarget(BlueprintDiscovery, Message));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FSalPhase0PublicPathTest,
    "Loomle.Sal.Phase0.PublicPath.DomainsFailClosed",
    EAutomationTestFlags::EditorContext
        | EAutomationTestFlags::EngineFilter)

bool FSalPhase0PublicPathTest::RunTest(const FString& Parameters)
{
    const TSharedPtr<FJsonObject> LegacyAssetReference =
        FSalModule::BuildQueryResult(
            ObjectQueryArguments(
                DomainTarget(TEXT("asset")),
                TEXT("not-a-guid")));
    TestTrue(
        TEXT("The old Asset Domain retains canonical Guid StableRef validation"),
        HasDiagnostic(
            LegacyAssetReference,
            TEXT("validation.invalid_reference")));

    const TSharedPtr<FJsonObject> MissingLevel =
        FSalModule::BuildQueryResult(
            QueryArguments(AssetDomainTarget(TEXT("level"))));
    TestTrue(
        TEXT("Level resolution requires a registered saved source map"),
        HasDiagnostic(MissingLevel, TEXT("resolution.target_not_found")));
    TestTrue(
        TEXT("A missing Level source map remains unresolved"),
        HasTargetContext(MissingLevel, TEXT("unresolved_target")));

    const TSharedPtr<FJsonObject> MissingComponent =
        FSalModule::BuildQueryResult(QueryArguments(PcgComponentTarget()));
    TestTrue(
        TEXT("pcg_component requires on-disk evidence for its owning Level"),
        HasDiagnostic(
            MissingComponent,
            TEXT("resolution.target_not_found")));
    TestTrue(
        TEXT("A pcg_component whose owning Level is missing remains unresolved"),
        HasTargetContext(MissingComponent, TEXT("unresolved_target")));

    const TSharedPtr<FJsonObject> LevelPatch =
        FSalModule::BuildPatchResult(
            PatchArguments(
                AssetDomainTarget(
                    TEXT("level"),
                    TEXT("/Script/Engine.World"))));
    TestTrue(
        TEXT("Exact Level Patch is admitted for adapter resolution after "
            "the authored-mutation capability bump"),
        !HasDiagnostic(
            LevelPatch,
            TEXT("language.invalid_object_shape")));

    const TSharedPtr<FJsonObject> PcgPatch =
        FSalModule::BuildPatchResult(
            PatchArguments(
                AssetDomainTarget(
                    TEXT("pcg"),
                    TEXT("/Script/PCG.PCGGraph"))));
    TestTrue(
        TEXT("Exact PCG Patch is admitted for adapter resolution after the "
            "authored-mutation capability bump"),
        !HasDiagnostic(
            PcgPatch,
            TEXT("language.invalid_object_shape")));

    const TSharedPtr<FJsonObject> ComponentPatch =
        FSalModule::BuildPatchResult(
            PatchArguments(PcgComponentTarget()));
    TestTrue(
        TEXT("pcg_component Patch is rejected before adapter resolution"),
        HasDiagnostic(
            ComponentPatch,
            TEXT("language.invalid_object_shape"),
            TEXT("Query-only")));
    TestFalse(
        TEXT("pcg_component Patch never reaches the adapter stub"),
        HasDiagnostic(
            ComponentPatch,
            TEXT("capability.interface_unavailable")));
    return true;
}
}

#endif
