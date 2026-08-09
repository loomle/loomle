// Copyright 2026 Loomle contributors.

#if WITH_DEV_AUTOMATION_TESTS

#include "Sal/SalJson.h"
#include "Sal/SalModule.h"
#include "Sal/SalObjectBuilder.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Misc/AutomationTest.h"

namespace
{
using namespace Loomle::Sal;

constexpr const TCHAR* BlueprintId =
    TEXT("11111111-1111-1111-1111-111111111111");
constexpr const TCHAR* OtherBlueprintId =
    TEXT("22222222-2222-2222-2222-222222222222");

TSharedRef<FJsonObject> LocalRef(const FString& Name)
{
    TSharedRef<FJsonObject> Ref = MakeShared<FJsonObject>();
    Ref->SetStringField(TEXT("kind"), TEXT("local"));
    Ref->SetStringField(TEXT("name"), Name);
    return Ref;
}

TSharedRef<FJsonObject> StableRef(const FString& Identity)
{
    TSharedRef<FJsonObject> Ref = MakeShared<FJsonObject>();
    Ref->SetStringField(TEXT("kind"), TEXT("stable_ref"));
    Ref->SetArrayField(
        TEXT("identityPath"),
        {MakeShared<FJsonValueString>(Identity)});
    return Ref;
}

TSharedRef<FJsonObject> ScopedStableRef(
    const FString& TargetAlias,
    const FString& Identity)
{
    TSharedRef<FJsonObject> Ref = MakeShared<FJsonObject>();
    Ref->SetStringField(TEXT("kind"), TEXT("scoped_stable_ref"));
    Ref->SetObjectField(TEXT("target"), LocalRef(TargetAlias));
    Ref->SetObjectField(TEXT("reference"), StableRef(Identity));
    return Ref;
}

TSharedRef<FJsonObject> BlueprintTarget(
    const FString& Asset,
    const FString& Id)
{
    TSharedRef<FJsonObject> Target = MakeShared<FJsonObject>();
    Target->SetStringField(TEXT("kind"), TEXT("target"));
    Target->SetStringField(TEXT("domain"), TEXT("blueprint"));
    Target->SetStringField(TEXT("asset"), Asset);
    Target->SetStringField(TEXT("id"), Id);
    return Target;
}

TSharedRef<FJsonObject> AssetRootTarget()
{
    TSharedRef<FJsonObject> Target = MakeShared<FJsonObject>();
    Target->SetStringField(TEXT("kind"), TEXT("target"));
    Target->SetStringField(TEXT("domain"), TEXT("asset"));
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

TSharedRef<FJsonObject> ObjectTextBinding(
    const FString& Alias,
    const TSharedPtr<FJsonValue>& Value)
{
    TSharedRef<FJsonObject> Statement = MakeShared<FJsonObject>();
    Statement->SetObjectField(TEXT("target"), LocalRef(Alias));
    Statement->SetField(TEXT("value"), Value);
    TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();
    Object->SetArrayField(
        TEXT("statements"),
        {MakeShared<FJsonValueObject>(Statement)});
    return Object;
}

TSharedRef<FJsonObject> QueryResult(
    const FString& Context,
    const TSharedPtr<FJsonObject>& MainTarget = nullptr)
{
    TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetStringField(TEXT("targetContext"), Context);
    if (MainTarget.IsValid())
    {
        Result->SetObjectField(TEXT("target"), MainTarget);
    }
    Result->SetArrayField(
        TEXT("diagnostics"),
        TArray<TSharedPtr<FJsonValue>>{});
    return Result;
}

TSharedRef<FJsonObject> Handoff(
    const FString& Purpose,
    const FString& Alias)
{
    TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetStringField(TEXT("kind"), TEXT("target_handoff"));
    Result->SetStringField(TEXT("purpose"), Purpose);
    Result->SetObjectField(TEXT("target"), LocalRef(Alias));
    return Result;
}

bool IsValidResult(const TSharedPtr<FJsonObject>& Result)
{
    TSharedPtr<FJsonObject> Error;
    return FSalJson::ValidateResult(Result, Error);
}

TSharedRef<FJsonObject> ErrorDiagnostic()
{
    TSharedRef<FJsonObject> Diagnostic = MakeShared<FJsonObject>();
    Diagnostic->SetStringField(TEXT("severity"), TEXT("error"));
    Diagnostic->SetStringField(TEXT("code"), TEXT("language.invalid_result_shape"));
    Diagnostic->SetStringField(TEXT("message"), TEXT("fixture"));
    return Diagnostic;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FSalResultContextSafetyTest,
    "Loomle.Sal.Result.ContextSafety",
    EAutomationTestFlags::EditorContext
        | EAutomationTestFlags::EngineFilter)

bool FSalResultContextSafetyTest::RunTest(const FString& Parameters)
{
    const TSharedRef<FJsonObject> Main = TargetBinding(
        TEXT("main_scope"),
        BlueprintTarget(
            TEXT("/Game/Main.Main"),
            BlueprintId));
    const TSharedRef<FJsonObject> Related = TargetBinding(
        TEXT("other_scope"),
        BlueprintTarget(
            TEXT("/Game/Other.Other"),
            OtherBlueprintId));

    TSharedRef<FJsonObject> ScopedUse = QueryResult(
        TEXT("exact_target"),
        Main);
    ScopedUse->SetArrayField(
        TEXT("relatedTargets"),
        {MakeShared<FJsonValueObject>(Related)});
    ScopedUse->SetObjectField(
        TEXT("object"),
        ObjectTextBinding(
            TEXT("entry"),
            MakeShared<FJsonValueObject>(
                ScopedStableRef(
                    TEXT("other_scope"),
                    TEXT("aaaaaaaa-aaaa-aaaa-aaaa-aaaaaaaaaaaa")))));
    TestTrue(
        TEXT("A scoped result StableRef retains its related Target"),
        IsValidResult(ScopedUse));

    TSharedRef<FJsonObject> HandoffUse = QueryResult(
        TEXT("exact_target"),
        Main);
    HandoffUse->SetArrayField(
        TEXT("relatedTargets"),
        {MakeShared<FJsonValueObject>(Related)});
    HandoffUse->SetArrayField(
        TEXT("handoffs"),
        {MakeShared<FJsonValueObject>(
            Handoff(TEXT("edit"), TEXT("other_scope")))});
    TestTrue(
        TEXT("A handoff retains its related Target"),
        IsValidResult(HandoffUse));

    TSharedRef<FJsonObject> UnusedRelated = QueryResult(
        TEXT("exact_target"),
        Main);
    UnusedRelated->SetArrayField(
        TEXT("relatedTargets"),
        {MakeShared<FJsonValueObject>(Related)});
    TestFalse(
        TEXT("A diagnostic-only or unused related Target is rejected"),
        IsValidResult(UnusedRelated));

    TSharedRef<FJsonObject> DuplicateAlias = QueryResult(
        TEXT("exact_target"),
        Main);
    DuplicateAlias->SetArrayField(
        TEXT("relatedTargets"),
        {MakeShared<FJsonValueObject>(
            TargetBinding(
                TEXT("main_scope"),
                BlueprintTarget(
                    TEXT("/Game/Other.Other"),
                    OtherBlueprintId)))});
    DuplicateAlias->SetArrayField(
        TEXT("handoffs"),
        {MakeShared<FJsonValueObject>(
            Handoff(TEXT("edit"), TEXT("main_scope")))});
    TestFalse(
        TEXT("Target table aliases are unique"),
        IsValidResult(DuplicateAlias));

    TSharedRef<FJsonObject> DuplicateTarget = QueryResult(
        TEXT("exact_target"),
        Main);
    DuplicateTarget->SetArrayField(
        TEXT("relatedTargets"),
        {MakeShared<FJsonValueObject>(
            TargetBinding(
                TEXT("duplicate_scope"),
                BlueprintTarget(
                    TEXT("/Game/Main.Main"),
                    BlueprintId)))});
    DuplicateTarget->SetArrayField(
        TEXT("handoffs"),
        {MakeShared<FJsonValueObject>(
            Handoff(TEXT("edit"), TEXT("duplicate_scope")))});
    TestFalse(
        TEXT("Canonical Targets are structurally deduplicated"),
        IsValidResult(DuplicateTarget));

    TSharedRef<FJsonObject> AliasCollision = QueryResult(
        TEXT("exact_target"),
        Main);
    AliasCollision->SetObjectField(
        TEXT("object"),
        ObjectTextBinding(
            TEXT("main_scope"),
            MakeShared<FJsonValueString>(TEXT("collision"))));
    TestFalse(
        TEXT("Object Text bindings cannot shadow Target aliases"),
        IsValidResult(AliasCollision));

    TSharedRef<FJsonObject> DomainRootScoped = QueryResult(
        TEXT("domain_root"),
        TargetBinding(TEXT("registry_scope"), AssetRootTarget()));
    DomainRootScoped->SetArrayField(
        TEXT("relatedTargets"),
        {MakeShared<FJsonValueObject>(Related)});
    DomainRootScoped->SetObjectField(
        TEXT("object"),
        ObjectTextBinding(
            TEXT("entry"),
            MakeShared<FJsonValueObject>(
                ScopedStableRef(
                    TEXT("other_scope"),
                    TEXT("aaaaaaaa-aaaa-aaaa-aaaa-aaaaaaaaaaaa")))));
    TestTrue(
        TEXT("Domain-root results may retain related exact Targets through scoped refs"),
        IsValidResult(DomainRootScoped));

    TSharedRef<FJsonObject> DomainRootUnqualified = QueryResult(
        TEXT("domain_root"),
        TargetBinding(TEXT("registry_scope"), AssetRootTarget()));
    DomainRootUnqualified->SetObjectField(
        TEXT("object"),
        ObjectTextBinding(
            TEXT("entry"),
            MakeShared<FJsonValueObject>(
                StableRef(
                    TEXT("aaaaaaaa-aaaa-aaaa-aaaa-aaaaaaaaaaaa")))));
    TestFalse(
        TEXT("Domain-root ObjectText cannot seed an unqualified StableRef"),
        IsValidResult(DomainRootUnqualified));

    TSharedRef<FJsonObject> ExactUnqualified = QueryResult(
        TEXT("exact_target"),
        Main);
    ExactUnqualified->SetObjectField(
        TEXT("object"),
        ObjectTextBinding(
            TEXT("entry"),
            MakeShared<FJsonValueObject>(
                StableRef(
                    TEXT("aaaaaaaa-aaaa-aaaa-aaaa-aaaaaaaaaaaa")))));
    TestTrue(
        TEXT("Exact-target ObjectText may use an unqualified StableRef"),
        IsValidResult(ExactUnqualified));

    TSharedRef<FJsonObject> UnresolvedStable = QueryResult(
        TEXT("unresolved_target"));
    UnresolvedStable->SetArrayField(
        TEXT("diagnostics"),
        {MakeShared<FJsonValueObject>(ErrorDiagnostic())});
    UnresolvedStable->SetObjectField(
        TEXT("object"),
        ObjectTextBinding(
            TEXT("entry"),
            MakeShared<FJsonValueObject>(
                StableRef(
                    TEXT("aaaaaaaa-aaaa-aaaa-aaaa-aaaaaaaaaaaa")))));
    TestFalse(
        TEXT("Unresolved ObjectText cannot use a StableRef"),
        IsValidResult(UnresolvedStable));

    TSharedRef<FJsonObject> UnresolvedScoped = QueryResult(
        TEXT("unresolved_target"));
    UnresolvedScoped->SetArrayField(
        TEXT("diagnostics"),
        {MakeShared<FJsonValueObject>(ErrorDiagnostic())});
    UnresolvedScoped->SetObjectField(
        TEXT("object"),
        ObjectTextBinding(
            TEXT("entry"),
            MakeShared<FJsonValueObject>(
                ScopedStableRef(
                    TEXT("missing_scope"),
                    TEXT("aaaaaaaa-aaaa-aaaa-aaaa-aaaaaaaaaaaa")))));
    TestFalse(
        TEXT("Unresolved ObjectText cannot use a ScopedStableRef"),
        IsValidResult(UnresolvedScoped));

    TSharedRef<FJsonObject> UnresolvedLocal = QueryResult(
        TEXT("unresolved_target"));
    UnresolvedLocal->SetArrayField(
        TEXT("diagnostics"),
        {MakeShared<FJsonValueObject>(ErrorDiagnostic())});
    UnresolvedLocal->SetObjectField(
        TEXT("object"),
        ObjectTextBinding(
            TEXT("entry"),
            MakeShared<FJsonValueObject>(
                LocalRef(TEXT("missing_local")))));
    TestFalse(
        TEXT("Unresolved ObjectText still enforces local declaration order"),
        IsValidResult(UnresolvedLocal));

    TSharedRef<FJsonObject> ReferenceCollision =
        MakeShared<FJsonObject>();
    ReferenceCollision->SetStringField(TEXT("kind"), TEXT("node"));
    ReferenceCollision->SetStringField(TEXT("id"), TEXT("inner-id"));
    TSharedRef<FJsonObject> CallArgs = MakeShared<FJsonObject>();
    CallArgs->SetObjectField(
        TEXT("nested"),
        ReferenceCollision);
    TSharedRef<FJsonObject> CallCollision =
        MakeShared<FJsonObject>();
    CallCollision->SetStringField(TEXT("kind"), TEXT("call"));
    CallCollision->SetStringField(
        TEXT("callee"),
        TEXT("outer-callee"));
    CallCollision->SetObjectField(TEXT("args"), CallArgs);
    const TSharedPtr<FJsonValue> RawValue =
        MakeShared<FJsonValueObject>(CallCollision);
    TestTrue(
        TEXT("Output normalization accepts an ordinary object whose fields resemble legacy protocol shapes"),
        FSalModule::NormalizeOutputExpressionForTesting(RawValue));
    const TSharedPtr<FJsonObject>* Normalized = nullptr;
    const TSharedPtr<FJsonObject>* Fields = nullptr;
    const TSharedPtr<FJsonObject>* NestedNormalized = nullptr;
    const TSharedPtr<FJsonObject>* NestedFields = nullptr;
    FString NormalizedKind;
    FString OuterCallee;
    FString NestedId;
    FString NestedKind;
    TestTrue(
        TEXT("Legacy-looking ordinary object fields round-trip through nested ObjectExpr wrappers"),
        RawValue->TryGetObject(Normalized)
            && Normalized != nullptr
            && (*Normalized)->TryGetStringField(
                TEXT("kind"),
                NormalizedKind)
            && NormalizedKind == TEXT("object")
            && (*Normalized)->TryGetObjectField(TEXT("fields"), Fields)
            && Fields != nullptr
            && (*Fields)->TryGetStringField(TEXT("callee"), OuterCallee)
            && OuterCallee == TEXT("outer-callee")
            && (*Fields)->TryGetObjectField(
                TEXT("args"),
                NestedNormalized)
            && NestedNormalized != nullptr
            && (*NestedNormalized)->TryGetObjectField(
                TEXT("fields"),
                NestedFields)
            && NestedFields != nullptr
            && (*NestedFields)->TryGetObjectField(
                TEXT("nested"),
                NestedNormalized)
            && NestedNormalized != nullptr
            && (*NestedNormalized)->TryGetObjectField(
                TEXT("fields"),
                NestedFields)
            && NestedFields != nullptr
            && (*NestedFields)->TryGetStringField(TEXT("id"), NestedId)
            && NestedId == TEXT("inner-id")
            && (*NestedFields)->TryGetStringField(TEXT("kind"), NestedKind)
            && NestedKind == TEXT("node"));

    for (const FString& CollisionKind : {
             FString(TEXT("name")),
             FString(TEXT("local")),
             FString(TEXT("stable_ref")),
             FString(TEXT("object"))})
    {
        TSharedPtr<FJsonObject> Collision = MakeShared<FJsonObject>();
        Collision->SetStringField(TEXT("kind"), CollisionKind);
        if (CollisionKind == TEXT("name")
            || CollisionKind == TEXT("local"))
        {
            Collision->SetStringField(TEXT("name"), TEXT("ordinary"));
        }
        else if (CollisionKind == TEXT("stable_ref"))
        {
            Collision->SetArrayField(
                TEXT("identityPath"),
                {MakeShared<FJsonValueString>(
                    TEXT("aaaaaaaa-aaaa-aaaa-aaaa-aaaaaaaaaaaa"))});
        }
        else
        {
            TSharedPtr<FJsonObject> OrdinaryFields =
                MakeShared<FJsonObject>();
            OrdinaryFields->SetStringField(
                TEXT("payload"),
                TEXT("ordinary"));
            Collision->SetObjectField(
                TEXT("fields"),
                OrdinaryFields);
        }
        const TSharedPtr<FJsonValue> CollisionValue =
            MakeShared<FJsonValueObject>(Collision);
        const TSharedPtr<FJsonObject>* CollisionNormalized = nullptr;
        const TSharedPtr<FJsonObject>* CollisionFields = nullptr;
        FString PreservedKind;
        TestTrue(
            *FString::Printf(
                TEXT("Raw %s-shaped object remains ordinary object data"),
                *CollisionKind),
            FSalModule::NormalizeOutputExpressionForTesting(
                CollisionValue)
                && CollisionValue->TryGetObject(
                    CollisionNormalized)
                && CollisionNormalized != nullptr
                && (*CollisionNormalized)->TryGetObjectField(
                    TEXT("fields"),
                    CollisionFields)
                && CollisionFields != nullptr
                && (*CollisionFields)->TryGetStringField(
                    TEXT("kind"),
                    PreservedKind)
                && PreservedKind == CollisionKind);
    }

    const TSharedPtr<FJsonValue> ExplicitName =
        Value::Name(TEXT("ordinary"));
    const TSharedPtr<FJsonObject>* ExplicitNameObject = nullptr;
    FString ExplicitNameKind;
    TestTrue(
        TEXT("Explicit Name factory remains a Name expression"),
        FSalModule::NormalizeOutputExpressionForTesting(
            ExplicitName)
            && ExplicitName->TryGetObject(ExplicitNameObject)
            && ExplicitNameObject != nullptr
            && (*ExplicitNameObject)->TryGetStringField(
                TEXT("kind"),
                ExplicitNameKind)
            && ExplicitNameKind == TEXT("name"));

    TSharedPtr<FJsonObject> ExplicitFields =
        MakeShared<FJsonObject>();
    ExplicitFields->SetField(
        TEXT("nestedName"),
        Value::Name(TEXT("ordinary")));
    const TSharedPtr<FJsonValue> ExplicitObject =
        Value::Call(TEXT("node"), ExplicitFields);
    const TSharedPtr<FJsonObject>* ExplicitObjectValue = nullptr;
    const TSharedPtr<FJsonObject>* ExplicitObjectFields = nullptr;
    const TSharedPtr<FJsonObject>* ExplicitNestedName = nullptr;
    FString ExplicitObjectKind;
    FString ExplicitNestedKind;
    TestTrue(
        TEXT("Explicit ObjectExpr factory and nested Name remain structural expressions"),
        FSalModule::NormalizeOutputExpressionForTesting(
            ExplicitObject)
            && ExplicitObject->TryGetObject(
                ExplicitObjectValue)
            && ExplicitObjectValue != nullptr
            && (*ExplicitObjectValue)->TryGetStringField(
                TEXT("kind"),
                ExplicitObjectKind)
            && ExplicitObjectKind == TEXT("object")
            && (*ExplicitObjectValue)->TryGetObjectField(
                TEXT("fields"),
                ExplicitObjectFields)
            && ExplicitObjectFields != nullptr
            && (*ExplicitObjectFields)->TryGetObjectField(
                TEXT("nestedName"),
                ExplicitNestedName)
            && ExplicitNestedName != nullptr
            && (*ExplicitNestedName)->TryGetStringField(
                TEXT("kind"),
                ExplicitNestedKind)
            && ExplicitNestedKind == TEXT("name"));
    return true;
}

#endif
