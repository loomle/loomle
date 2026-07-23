// Copyright 2026 Loomle contributors.

#if WITH_DEV_AUTOMATION_TESTS

#include "Sal/SalJson.h"
#include "Sal/SalModule.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "GameFramework/Actor.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/AutomationTest.h"
#include "UObject/GarbageCollection.h"
#include "UObject/Package.h"
#include "UObject/UObjectHash.h"

namespace
{
using namespace Loomle::Sal;

TSharedRef<FJsonObject> PublicPathCall(
    const FString& Callee,
    const TSharedRef<FJsonObject>& Args)
{
    TSharedRef<FJsonObject> Value = MakeShared<FJsonObject>();
    Value->SetStringField(TEXT("kind"), TEXT("call"));
    Value->SetStringField(TEXT("callee"), Callee);
    Value->SetObjectField(TEXT("args"), Args);
    return Value;
}

TSharedRef<FJsonObject> PublicPathClassCall(const FString& Path)
{
    TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
    Args->SetStringField(TEXT("path"), Path);
    return PublicPathCall(TEXT("class"), Args);
}

TSharedRef<FJsonObject> PublicPathTarget(
    const FString& Alias,
    const TSharedRef<FJsonObject>& Value)
{
    TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetStringField(TEXT("alias"), Alias);
    Result->SetObjectField(TEXT("value"), Value);
    return Result;
}

TSharedRef<FJsonObject> PublicPathQueryArguments(
    const FString& Alias,
    const TSharedRef<FJsonObject>& Value,
    const FString& OperationKind)
{
    TSharedRef<FJsonObject> Operation = MakeShared<FJsonObject>();
    Operation->SetStringField(TEXT("kind"), OperationKind);

    TSharedRef<FJsonObject> Query = MakeShared<FJsonObject>();
    Query->SetStringField(TEXT("kind"), TEXT("query"));
    Query->SetObjectField(TEXT("target"), PublicPathTarget(Alias, Value));
    Query->SetObjectField(TEXT("operation"), Operation);

    TSharedRef<FJsonObject> Arguments = MakeShared<FJsonObject>();
    Arguments->SetObjectField(TEXT("object"), Query);
    return Arguments;
}

TSharedRef<FJsonObject> PublicPathLocalReference(const FString& Alias)
{
    TSharedRef<FJsonObject> Reference = MakeShared<FJsonObject>();
    Reference->SetStringField(TEXT("kind"), TEXT("local"));
    Reference->SetStringField(TEXT("name"), Alias);
    return Reference;
}

TSharedRef<FJsonObject> PublicPathMemberReference(
    const FString& Alias,
    const FString& Property)
{
    TSharedRef<FJsonObject> Reference = MakeShared<FJsonObject>();
    Reference->SetStringField(TEXT("kind"), TEXT("member"));
    Reference->SetObjectField(
        TEXT("object"),
        PublicPathLocalReference(Alias));
    Reference->SetArrayField(
        TEXT("path"),
        {MakeShared<FJsonValueString>(Property)});
    return Reference;
}

TSharedRef<FJsonObject> PublicPathClassDefaultDryRunArguments(
    const FString& ClassPath,
    const FString& DesiredValue)
{
    static const FString Alias = TEXT("actorClass");

    TSharedRef<FJsonObject> Set = MakeShared<FJsonObject>();
    Set->SetStringField(TEXT("kind"), TEXT("set"));
    Set->SetObjectField(
        TEXT("target"),
        PublicPathMemberReference(Alias, TEXT("InitialLifeSpan")));
    Set->SetStringField(TEXT("value"), DesiredValue);

    TSharedRef<FJsonObject> Patch = MakeShared<FJsonObject>();
    Patch->SetStringField(TEXT("kind"), TEXT("patch"));
    Patch->SetObjectField(
        TEXT("target"),
        PublicPathTarget(Alias, PublicPathClassCall(ClassPath)));
    Patch->SetBoolField(TEXT("dryRun"), true);
    Patch->SetArrayField(
        TEXT("statements"),
        {MakeShared<FJsonValueObject>(Set)});

    TSharedRef<FJsonObject> Arguments = MakeShared<FJsonObject>();
    Arguments->SetObjectField(TEXT("object"), Patch);
    return Arguments;
}

bool PublicPathResultBool(
    const TSharedPtr<FJsonObject>& Result,
    const TCHAR* Field,
    const bool Default = false)
{
    bool Value = Default;
    return Result.IsValid() && Result->TryGetBoolField(Field, Value)
        ? Value
        : Default;
}

bool PublicPathHasDiagnosticCode(
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

bool PublicPathHasCallPath(
    const TSharedPtr<FJsonObject>& Result,
    const FString& Callee,
    const FString& ExpectedPath)
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
        const TSharedPtr<FJsonObject>* Value = nullptr;
        const TSharedPtr<FJsonObject>* Args = nullptr;
        FString ActualCallee;
        FString ActualPath;
        if (StatementValue.IsValid()
            && StatementValue->TryGetObject(Statement)
            && Statement != nullptr
            && (*Statement)->TryGetObjectField(TEXT("value"), Value)
            && Value != nullptr
            && (*Value)->TryGetStringField(TEXT("callee"), ActualCallee)
            && ActualCallee == Callee
            && (*Value)->TryGetObjectField(TEXT("args"), Args)
            && Args != nullptr
            && (*Args)->TryGetStringField(TEXT("path"), ActualPath)
            && ActualPath == ExpectedPath)
        {
            return true;
        }
    }
    return false;
}

bool IsValidPublicPathOutgoingResult(
    const TSharedPtr<FJsonObject>& Result,
    FString& OutError)
{
    TSharedPtr<FJsonObject> ValidationError;
    if (FSalJson::ValidateResult(Result, ValidationError))
    {
        OutError.Reset();
        return true;
    }

    OutError = TEXT("Result failed SAL outgoing validation.");
    const TArray<TSharedPtr<FJsonValue>>* Diagnostics = nullptr;
    if (ValidationError.IsValid()
        && ValidationError->TryGetArrayField(TEXT("diagnostics"), Diagnostics)
        && Diagnostics != nullptr
        && !Diagnostics->IsEmpty())
    {
        const TSharedPtr<FJsonObject>* Diagnostic = nullptr;
        FString Message;
        if ((*Diagnostics)[0].IsValid()
            && (*Diagnostics)[0]->TryGetObject(Diagnostic)
            && Diagnostic != nullptr
            && (*Diagnostic)->TryGetStringField(TEXT("message"), Message))
        {
            OutError += TEXT(" ") + Message;
        }
    }
    return false;
}

bool UnloadPublicPathFixturePackage(
    UPackage* Package,
    FString& OutError)
{
    OutError.Reset();
    if (Package == nullptr)
    {
        return true;
    }

    const FString PackageName = Package->GetName();
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

    CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);
    if (FindPackage(nullptr, *PackageName) != nullptr)
    {
        OutError = TEXT("Fixture package remained loaded: ") + PackageName;
        return false;
    }
    return true;
}

class FPublicPathClassFixture
{
public:
    FPublicPathClassFixture()
    {
        const FString Token =
            FGuid::NewGuid().ToString(EGuidFormats::Digits);
        const FString PackageName =
            TEXT("/Game/LoomleTests/SalModulePublicPath_") + Token;
        const FString BlueprintName =
            TEXT("BP_SalModulePublicPath_") + Token;

        Package = CreatePackage(*PackageName);
        Blueprint = FKismetEditorUtilities::CreateBlueprint(
            AActor::StaticClass(),
            Package,
            FName(*BlueprintName),
            BPTYPE_Normal,
            UBlueprint::StaticClass(),
            UBlueprintGeneratedClass::StaticClass(),
            NAME_None);
        Class = Blueprint != nullptr
            ? Cast<UBlueprintGeneratedClass>(Blueprint->GeneratedClass)
            : nullptr;
        ActorCDO = Class != nullptr
            ? Cast<AActor>(Class->GetDefaultObject())
            : nullptr;
        if (Package != nullptr)
        {
            Package->SetDirtyFlag(false);
        }
    }

    ~FPublicPathClassFixture()
    {
        FString Ignored;
        Cleanup(Ignored);
    }

    FPublicPathClassFixture(const FPublicPathClassFixture&) = delete;
    FPublicPathClassFixture& operator=(const FPublicPathClassFixture&) = delete;

    bool Cleanup(FString& OutError)
    {
        if (bCleaned)
        {
            OutError.Reset();
            return true;
        }
        bCleaned = true;

        UPackage* PackageToUnload = Package;
        if (Blueprint != nullptr)
        {
            Blueprint->ClearFlags(RF_Public | RF_Standalone);
        }
        ActorCDO = nullptr;
        Class = nullptr;
        Blueprint = nullptr;
        Package = nullptr;
        return UnloadPublicPathFixturePackage(PackageToUnload, OutError);
    }

    UPackage* Package = nullptr;
    UBlueprint* Blueprint = nullptr;
    UBlueprintGeneratedClass* Class = nullptr;
    AActor* ActorCDO = nullptr;

private:
    bool bCleaned = false;
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FSalModuleQueryPublicPathTest,
    "Loomle.Sal.PublicPath.Query.DecodeResolveDispatchAndValidate",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSalModuleQueryPublicPathTest::RunTest(const FString& Parameters)
{
    UClass* Class = AActor::StaticClass();
    const TSharedPtr<FJsonObject> Result =
        FSalModule::BuildQueryResult(
            PublicPathQueryArguments(
                TEXT("actorClass"),
                PublicPathClassCall(Class->GetPathName()),
                TEXT("summary")));

    FString ValidationError;
    const bool bOutgoingValid =
        IsValidPublicPathOutgoingResult(Result, ValidationError);
    TestTrue(
        *FString::Printf(
            TEXT("Public Query result satisfies the outgoing SAL contract: %s"),
            *ValidationError),
        bOutgoingValid);
    TestFalse(
        TEXT("Successful Query does not return a MutationResult"),
        Result.IsValid() && Result->HasField(TEXT("isError")));
    TestTrue(
        TEXT("Decoded Query resolves and dispatches to the Class interface"),
        PublicPathHasCallPath(
            Result,
            TEXT("class"),
            Class->GetPathName()));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FSalModulePatchDryRunPublicPathTest,
    "Loomle.Sal.PublicPath.Patch.DryRunDecodeResolveDispatchAndValidate",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSalModulePatchDryRunPublicPathTest::RunTest(const FString& Parameters)
{
    FPublicPathClassFixture Fixture;
    TestNotNull(
        TEXT("Public Patch fixture creates a Blueprint Generated Class"),
        Fixture.Class);
    TestNotNull(
        TEXT("Public Patch fixture creates an Actor CDO"),
        Fixture.ActorCDO);
    if (Fixture.Class == nullptr || Fixture.ActorCDO == nullptr)
    {
        return false;
    }

    const float Before = Fixture.ActorCDO->InitialLifeSpan;
    const float Desired = Before + 23.5f;
    const TSharedPtr<FJsonObject> Result =
        FSalModule::BuildPatchResult(
            PublicPathClassDefaultDryRunArguments(
                Fixture.Class->GetPathName(),
                FString::SanitizeFloat(Desired)));

    FString ValidationError;
    const bool bOutgoingValid =
        IsValidPublicPathOutgoingResult(Result, ValidationError);
    TestTrue(
        *FString::Printf(
            TEXT("Public Patch result satisfies the outgoing SAL contract: %s"),
            *ValidationError),
        bOutgoingValid);
    TestFalse(
        TEXT("Successful public Patch is not an error"),
        PublicPathResultBool(Result, TEXT("isError"), true));
    TestTrue(
        TEXT("Public Patch validates after decode, resolution, and dispatch"),
        PublicPathResultBool(Result, TEXT("valid")));
    TestTrue(
        TEXT("Public Patch preserves dryRun=true"),
        PublicPathResultBool(Result, TEXT("dryRun")));
    TestFalse(
        TEXT("Public dry-run Patch reports applied=false"),
        PublicPathResultBool(Result, TEXT("applied"), true));
    TestTrue(
        TEXT("Public dry-run Patch preserves the live generated CDO"),
        FMath::IsNearlyEqual(Fixture.ActorCDO->InitialLifeSpan, Before));
    TestFalse(
        TEXT("Public dry-run Patch preserves Package dirty state"),
        Fixture.Package->IsDirty());

    FString CleanupError;
    const bool bCleaned = Fixture.Cleanup(CleanupError);
    TestTrue(
        *FString::Printf(
            TEXT("Public Patch fixture unloads cleanly: %s"),
            *CleanupError),
        bCleaned);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FSalModuleInvalidLocatorPublicPathTest,
    "Loomle.Sal.PublicPath.Query.InvalidLocator",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSalModuleInvalidLocatorPublicPathTest::RunTest(const FString& Parameters)
{
    const TSharedPtr<FJsonObject> Result =
        FSalModule::BuildQueryResult(
            PublicPathQueryArguments(
                TEXT("invalidClass"),
                PublicPathClassCall(FString()),
                TEXT("summary")));

    FString ValidationError;
    const bool bOutgoingValid =
        IsValidPublicPathOutgoingResult(Result, ValidationError);
    TestTrue(
        *FString::Printf(
            TEXT("Resolver failure still satisfies the outgoing SAL contract: %s"),
            *ValidationError),
        bOutgoingValid);
    TestTrue(
        TEXT("Decoded invalid locator reaches TargetResolver"),
        PublicPathHasDiagnosticCode(
            Result,
            TEXT("validation.invalid_target_locator")));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FSalModuleMalformedQueryPublicPathTest,
    "Loomle.Sal.PublicPath.Query.MalformedNormalizedObject",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSalModuleMalformedQueryPublicPathTest::RunTest(const FString& Parameters)
{
    TSharedRef<FJsonObject> MalformedQuery = MakeShared<FJsonObject>();
    MalformedQuery->SetStringField(TEXT("kind"), TEXT("query"));
    MalformedQuery->SetStringField(TEXT("unsupported"), TEXT("field"));
    TSharedRef<FJsonObject> Arguments = MakeShared<FJsonObject>();
    Arguments->SetObjectField(TEXT("object"), MalformedQuery);

    const TSharedPtr<FJsonObject> Result =
        FSalModule::BuildQueryResult(Arguments);
    FString ValidationError;
    const bool bOutgoingValid =
        IsValidPublicPathOutgoingResult(Result, ValidationError);
    TestTrue(
        *FString::Printf(
            TEXT("Decode failure still satisfies the outgoing SAL contract: %s"),
            *ValidationError),
        bOutgoingValid);
    TestTrue(
        TEXT("Malformed normalized Query is rejected by the SAL decoder"),
        PublicPathHasDiagnosticCode(
            Result,
            TEXT("language.invalid_object_shape")));
    return true;
}

#endif
