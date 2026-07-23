// Copyright 2026 Loomle contributors.

#if WITH_DEV_AUTOMATION_TESTS

#include "Sal/Asset/SalAssetInterface.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "GameFramework/Actor.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/AutomationTest.h"
#include "Modules/ModuleManager.h"
#include "UObject/Package.h"

namespace
{
using namespace Loomle::Sal;

class FScopedAssetQueryFixture
{
public:
    FScopedAssetQueryFixture()
    {
        const FString Token = FGuid::NewGuid().ToString(EGuidFormats::Digits);
        Package = CreatePackage(*FString::Printf(
            TEXT("/Game/LoomleTests/SalAssetQuery_%s"),
            *Token));
        const FName BlueprintName(*FString::Printf(
            TEXT("BP_AssetQuery_%s"),
            *Token));
        Blueprint = FKismetEditorUtilities::CreateBlueprint(
            AActor::StaticClass(),
            Package,
            BlueprintName,
            BPTYPE_Normal,
            UBlueprint::StaticClass(),
            UBlueprintGeneratedClass::StaticClass(),
            NAME_None);
        if (Blueprint != nullptr)
        {
            Blueprint->BlueprintDescription =
                FString::ChrN(9000, TEXT('x'));
            FModuleManager::LoadModuleChecked<FAssetRegistryModule>(
                TEXT("AssetRegistry"));
            FAssetRegistryModule::AssetCreated(Blueprint);
            bRegistered = true;
        }
        if (Package != nullptr)
        {
            Package->SetDirtyFlag(false);
        }
    }

    ~FScopedAssetQueryFixture()
    {
        if (bRegistered && Blueprint != nullptr)
        {
            FAssetRegistryModule::AssetDeleted(Blueprint);
        }
        if (Blueprint != nullptr)
        {
            Blueprint->ClearFlags(RF_Public | RF_Standalone);
        }
        if (Package != nullptr)
        {
            Package->SetDirtyFlag(false);
        }
    }

    FScopedAssetQueryFixture(const FScopedAssetQueryFixture&) = delete;
    FScopedAssetQueryFixture& operator=(const FScopedAssetQueryFixture&) = delete;

    UPackage* Package = nullptr;
    UBlueprint* Blueprint = nullptr;

private:
    bool bRegistered = false;
};

FSalResolvedTarget AssetRootTarget()
{
    FSalResolvedTarget Target;
    Target.Kind = ESalTargetKind::AssetRoot;
    Target.Alias = TEXT("asset");
    Target.Interfaces = {FName(TEXT("asset"))};
    return Target;
}

FSalQuery AssetQuery(const FString& Text = FString())
{
    FSalQuery Query;
    Query.Alias = TEXT("asset");
    Query.Operation = MakeShared<FJsonObject>();
    Query.Operation->SetStringField(TEXT("kind"), TEXT("assets"));
    if (!Text.IsEmpty())
    {
        Query.Operation->SetStringField(TEXT("text"), Text);
    }
    return Query;
}

TSharedPtr<FJsonObject> FieldCondition(
    const FString& Kind,
    const TArray<FString>& Path,
    const FString& Value)
{
    TSharedPtr<FJsonObject> Field = MakeShared<FJsonObject>();
    TArray<TSharedPtr<FJsonValue>> Segments;
    for (const FString& Segment : Path)
    {
        Segments.Add(MakeShared<FJsonValueString>(Segment));
    }
    Field->SetArrayField(TEXT("path"), MoveTemp(Segments));

    TSharedPtr<FJsonObject> Condition = MakeShared<FJsonObject>();
    Condition->SetStringField(TEXT("kind"), Kind);
    Condition->SetObjectField(TEXT("field"), Field);
    Condition->SetStringField(TEXT("value"), Value);
    return Condition;
}

TArray<TSharedPtr<FJsonObject>> CallArgs(
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
    for (const TSharedPtr<FJsonValue>& StatementValue : *Statements)
    {
        const TSharedPtr<FJsonObject>* Statement = nullptr;
        const TSharedPtr<FJsonObject>* Call = nullptr;
        const TSharedPtr<FJsonObject>* CallArgsObject = nullptr;
        FString ActualCallee;
        if (StatementValue.IsValid()
            && StatementValue->TryGetObject(Statement)
            && Statement != nullptr
            && (*Statement)->TryGetObjectField(TEXT("value"), Call)
            && Call != nullptr
            && (*Call)->TryGetStringField(TEXT("callee"), ActualCallee)
            && ActualCallee == Callee
            && (*Call)->TryGetObjectField(TEXT("args"), CallArgsObject)
            && CallArgsObject != nullptr)
        {
            Args.Add(*CallArgsObject);
        }
    }
    return Args;
}

bool HasCommentContaining(
    const TSharedPtr<FJsonObject>& Result,
    const FString& Text)
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
    for (const TSharedPtr<FJsonValue>& StatementValue : *Statements)
    {
        const TSharedPtr<FJsonObject>* Statement = nullptr;
        FString Kind;
        FString Comment;
        if (StatementValue.IsValid()
            && StatementValue->TryGetObject(Statement)
            && Statement != nullptr
            && (*Statement)->TryGetStringField(TEXT("kind"), Kind)
            && Kind == TEXT("comment")
            && (*Statement)->TryGetStringField(TEXT("text"), Comment)
            && Comment.Contains(Text))
        {
            return true;
        }
    }
    return false;
}

bool HasDiagnosticCode(
    const TSharedPtr<FJsonObject>& Result,
    const FString& ExpectedCode)
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
        if (DiagnosticValue.IsValid()
            && DiagnosticValue->TryGetObject(Diagnostic)
            && Diagnostic != nullptr
            && (*Diagnostic)->TryGetStringField(TEXT("code"), Code)
            && Code == ExpectedCode)
        {
            return true;
        }
    }
    return false;
}

bool ContainsAssetPath(
    const TSharedPtr<FJsonObject>& Result,
    const FString& ExpectedPath)
{
    for (const TSharedPtr<FJsonObject>& Args : CallArgs(Result, TEXT("asset")))
    {
        FString Path;
        if (Args.IsValid()
            && Args->TryGetStringField(TEXT("path"), Path)
            && Path == ExpectedPath)
        {
            return true;
        }
    }
    return false;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FSalAssetQuerySmokeTest,
    "Loomle.Sal.Asset.Query.ExactAndCollection",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSalAssetQuerySmokeTest::RunTest(const FString& Parameters)
{
    FScopedAssetQueryFixture Fixture;
    TestNotNull(TEXT("In-memory Blueprint fixture is created"), Fixture.Blueprint);
    if (Fixture.Blueprint == nullptr)
    {
        return false;
    }

    const FString AssetPath = Fixture.Blueprint->GetPathName();
    const FString AssetName = Fixture.Blueprint->GetName();

    FSalQuery Search = AssetQuery(AssetName);
    Search.PageLimit = 10;
    const TSharedPtr<FJsonObject> SearchResult =
        FSalAssetInterface::Query(Search, AssetRootTarget());
    TestTrue(
        TEXT("Collection search returns the registered in-memory asset"),
        ContainsAssetPath(SearchResult, AssetPath));

    FSalQuery Exact = AssetQuery();
    Exact.PageLimit = 2;
    Exact.Where = FieldCondition(TEXT("eq"), {TEXT("path")}, AssetPath);
    const TSharedPtr<FJsonObject> ExactResult =
        FSalAssetInterface::Query(Exact, AssetRootTarget());
    const TArray<TSharedPtr<FJsonObject>> ExactAssets =
        CallArgs(ExactResult, TEXT("asset"));
    TestEqual(TEXT("Exact path filter returns one asset"), ExactAssets.Num(), 1);
    if (ExactAssets.Num() == 1)
    {
        FString Type;
        bool bLoaded = false;
        const TArray<TSharedPtr<FJsonValue>>* Domains = nullptr;
        TestTrue(
            TEXT("Exact asset preserves its native Blueprint type"),
            ExactAssets[0]->TryGetStringField(TEXT("type"), Type)
                && Type == UBlueprint::StaticClass()->GetPathName());
        TestTrue(
            TEXT("Exact in-memory asset reports loaded state"),
            ExactAssets[0]->TryGetBoolField(TEXT("loaded"), bLoaded)
                && bLoaded);
        TestTrue(
            TEXT("Exact Blueprint asset exposes composed domains"),
            ExactAssets[0]->TryGetArrayField(TEXT("domains"), Domains)
                && Domains != nullptr
                && Domains->Num() >= 2);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FSalAssetRegistryTagSafetyTest,
    "Loomle.Sal.Asset.Query.RegistryTagSafety",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSalAssetRegistryTagSafetyTest::RunTest(const FString& Parameters)
{
    FScopedAssetQueryFixture Fixture;
    TestNotNull(TEXT("Blueprint Registry Tag fixture is created"), Fixture.Blueprint);
    if (Fixture.Blueprint == nullptr)
    {
        return false;
    }

    FSalQuery Query = AssetQuery();
    Query.PageLimit = 2;
    Query.Where = FieldCondition(
        TEXT("eq"),
        {TEXT("path")},
        Fixture.Blueprint->GetPathName());
    Query.With.Add(TEXT("registryTags"));
    const TSharedPtr<FJsonObject> Result =
        FSalAssetInterface::Query(Query, AssetRootTarget());
    const TArray<TSharedPtr<FJsonObject>> Assets =
        CallArgs(Result, TEXT("asset"));
    TestEqual(TEXT("Registry Tag query returns the exact fixture"), Assets.Num(), 1);
    if (Assets.Num() == 1)
    {
        const TSharedPtr<FJsonObject>* Tags = nullptr;
        TestTrue(
            TEXT("Registry Tags remain an inline object"),
            Assets[0]->TryGetObjectField(TEXT("registryTags"), Tags)
                && Tags != nullptr);
        if (Tags != nullptr)
        {
            TestFalse(
                TEXT("FiBData is never returned as an inline Registry Tag"),
                (*Tags)->HasField(TEXT("FiBData")));
            TestFalse(
                TEXT("Legacy FiB is never returned as an inline Registry Tag"),
                (*Tags)->HasField(TEXT("FiB")));
            TestFalse(
                TEXT("Oversized ordinary Registry Tags are not returned inline"),
                (*Tags)->HasField(TEXT("BlueprintDescription")));
        }
    }
    TestTrue(
        TEXT("Omitted oversized Registry Tags report only size and reason"),
        HasCommentContaining(
            Result,
            TEXT("BlueprintDescription: reason=value_too_large")));

    FSalQuery UnsafeComparison = AssetQuery();
    UnsafeComparison.Where = FieldCondition(
        TEXT("eq"),
        {TEXT("registryTag"), TEXT("FiBData")},
        TEXT(""));
    const TSharedPtr<FJsonObject> UnsafeResult =
        FSalAssetInterface::Query(UnsafeComparison, AssetRootTarget());
    TestTrue(
        TEXT("Opaque FiBData comparison is rejected before registry scanning"),
        HasDiagnosticCode(UnsafeResult, TEXT("capability.unsupported_query")));
    return true;
}

#endif
