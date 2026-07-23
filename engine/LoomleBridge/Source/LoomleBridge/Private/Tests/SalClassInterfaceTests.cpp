// Copyright 2026 Loomle contributors.

#if WITH_DEV_AUTOMATION_TESTS

#include "Sal/Class/SalClassInterface.h"
#include "SalClassSparseTestTypes.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Editor.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Character.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/AutomationTest.h"
#include "UObject/GarbageCollection.h"
#include "UObject/Package.h"
#include "UObject/UObjectHash.h"
#include "UObject/UnrealType.h"

namespace
{
using namespace Loomle::Sal;

FSalResolvedTarget ClassTarget(
    UClass* Class,
    const FString& Alias = TEXT("actorClass"))
{
    FSalResolvedTarget Target;
    Target.Kind = ESalTargetKind::Class;
    Target.Alias = Alias;
    Target.Class = Class;
    Target.Object = Class;
    Target.Name = Class != nullptr ? Class->GetName() : FString();
    Target.Package = Class != nullptr ? Class->GetOutermost() : nullptr;
    Target.Interfaces = {FName(TEXT("class"))};
    if (const UBlueprintGeneratedClass* Generated =
            Cast<UBlueprintGeneratedClass>(Class))
    {
        if (const UBlueprint* Blueprint =
                Cast<UBlueprint>(Generated->ClassGeneratedBy))
        {
            Target.AssetPath = Blueprint->GetPathName();
            Target.Package = Blueprint->GetOutermost();
        }
    }
    return Target;
}

FSalQuery ClassQuery(
    const FString& Kind,
    const FString& Name = FString(),
    const FString& Text = FString())
{
    FSalQuery Query;
    Query.Alias = TEXT("actorClass");
    Query.Operation = MakeShared<FJsonObject>();
    Query.Operation->SetStringField(TEXT("kind"), Kind);
    if (!Name.IsEmpty())
    {
        Query.Operation->SetStringField(TEXT("name"), Name);
    }
    if (!Text.IsEmpty())
    {
        Query.Operation->SetStringField(TEXT("text"), Text);
    }
    return Query;
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

bool HasCallPath(
    const TSharedPtr<FJsonObject>& Result,
    const FString& Callee,
    const FString& ExpectedPath)
{
    for (const TSharedPtr<FJsonObject>& Args : CallArgs(Result, Callee))
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

bool ResultBool(
    const TSharedPtr<FJsonObject>& Result,
    const TCHAR* Field,
    const bool Default = false)
{
    bool Value = Default;
    return Result.IsValid() && Result->TryGetBoolField(Field, Value)
        ? Value
        : Default;
}

TSharedRef<FJsonObject> LocalReference(const FString& Alias)
{
    TSharedRef<FJsonObject> Reference = MakeShared<FJsonObject>();
    Reference->SetStringField(TEXT("kind"), TEXT("local"));
    Reference->SetStringField(TEXT("name"), Alias);
    return Reference;
}

TSharedRef<FJsonObject> DefaultReference(
    const FString& Alias,
    const FString& Property)
{
    TSharedRef<FJsonObject> Reference = MakeShared<FJsonObject>();
    Reference->SetStringField(TEXT("kind"), TEXT("member"));
    Reference->SetObjectField(TEXT("object"), LocalReference(Alias));
    Reference->SetArrayField(
        TEXT("path"),
        {MakeShared<FJsonValueString>(Property)});
    return Reference;
}

FSalPatch DefaultPatch(
    const FString& Kind,
    const FString& Property,
    const FString& Value,
    const bool bDryRun)
{
    static const FString Alias = TEXT("actorClass");
    TSharedRef<FJsonObject> Statement = MakeShared<FJsonObject>();
    Statement->SetStringField(TEXT("kind"), Kind);
    Statement->SetObjectField(
        TEXT("target"),
        DefaultReference(Alias, Property));
    if (Kind == TEXT("set"))
    {
        Statement->SetStringField(TEXT("value"), Value);
    }

    FSalPatch Patch;
    Patch.Alias = Alias;
    Patch.bDryRun = bDryRun;
    Patch.Statements = {MakeShared<FJsonValueObject>(Statement)};
    return Patch;
}

enum class EClassDefaultsFixtureKind : uint8
{
    Actor,
    Sparse
};

bool UnloadClassDefaultsTestPackage(
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
        OutError = TEXT("Fixture package remained loaded: ")
            + PackageName;
        return false;
    }
    return true;
}

class FClassDefaultsFixture
{
public:
    explicit FClassDefaultsFixture(
        const EClassDefaultsFixtureKind Kind)
    {
        const bool bSparse =
            Kind == EClassDefaultsFixtureKind::Sparse;
        const FString Token =
            FGuid::NewGuid().ToString(EGuidFormats::Digits);
        const FString PackageName =
            (bSparse
                ? TEXT("/Game/LoomleTests/SalSparseClassDefaults_")
                : TEXT("/Game/LoomleTests/SalClassDefaults_"))
            + Token;
        const FString BlueprintName =
            (bSparse
                ? TEXT("BP_SparseClassDefaults_")
                : TEXT("BP_ClassDefaults_"))
            + Token;
        Package = CreatePackage(*PackageName);
        Blueprint = FKismetEditorUtilities::CreateBlueprint(
            bSparse
                ? USalClassSparseTestObject::StaticClass()
                : AActor::StaticClass(),
            Package,
            FName(*BlueprintName),
            BPTYPE_Normal,
            UBlueprint::StaticClass(),
            UBlueprintGeneratedClass::StaticClass(),
            NAME_None);
        Class = Blueprint != nullptr
            ? Cast<UBlueprintGeneratedClass>(
                Blueprint->GeneratedClass)
            : nullptr;
        CDO = Class != nullptr
            ? Class->GetDefaultObject()
            : nullptr;
        if (Package != nullptr)
        {
            Package->SetDirtyFlag(false);
        }
    }

    ~FClassDefaultsFixture()
    {
        FString Ignored;
        Cleanup(Ignored);
    }

    FClassDefaultsFixture(const FClassDefaultsFixture&) = delete;
    FClassDefaultsFixture& operator=(const FClassDefaultsFixture&) = delete;

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
        CDO = nullptr;
        Class = nullptr;
        Blueprint = nullptr;
        Package = nullptr;
        return UnloadClassDefaultsTestPackage(
            PackageToUnload,
            OutError);
    }

    UPackage* Package = nullptr;
    UBlueprint* Blueprint = nullptr;
    UBlueprintGeneratedClass* Class = nullptr;
    UObject* CDO = nullptr;

private:
    bool bCleaned = false;
};

int32 SparseValue(UClass* Class)
{
    UScriptStruct* SparseStruct =
        Class != nullptr ? Class->GetSparseClassDataStruct() : nullptr;
    const void* SparseData =
        Class != nullptr
            ? Class->GetSparseClassData(
                EGetSparseClassDataMethod::ReturnIfNull)
            : nullptr;
    const FIntProperty* SparseProperty =
        SparseStruct != nullptr
            ? FindFProperty<FIntProperty>(
                SparseStruct,
                TEXT("SparseValue"))
            : nullptr;
    if (SparseData == nullptr || SparseProperty == nullptr)
    {
        return INDEX_NONE;
    }
    return SparseProperty->GetPropertyValue_InContainer(
        SparseData);
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FSalClassReflectionQuerySmokeTest,
    "Loomle.Sal.Class.Query.ReflectionAndInheritance",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSalClassReflectionQuerySmokeTest::RunTest(const FString& Parameters)
{
    UClass* Class = ACharacter::StaticClass();
    const FSalResolvedTarget Target = ClassTarget(Class);
    const TSharedPtr<FJsonObject> Summary =
        FSalClassInterface::Query(ClassQuery(TEXT("summary")), Target);
    const TArray<TSharedPtr<FJsonObject>> Classes =
        CallArgs(Summary, TEXT("class"));
    TestEqual(TEXT("Summary returns one exact Class"), Classes.Num(), 1);
    if (Classes.Num() == 1)
    {
        FString Path;
        FString SuperClass;
        const TSharedPtr<FJsonObject>* MetaData = nullptr;
        TestTrue(
            TEXT("Summary preserves native Class Path"),
            Classes[0]->TryGetStringField(TEXT("path"), Path)
                && Path == Class->GetPathName());
        TestTrue(
            TEXT("Summary preserves exact SuperClass Path"),
            Classes[0]->TryGetStringField(TEXT("SuperClass"), SuperClass)
                && SuperClass == Class->GetSuperClass()->GetPathName());
        TestTrue(
            TEXT("Summary exposes native Class Metadata"),
            Classes[0]->TryGetObjectField(TEXT("MetaData"), MetaData)
                && MetaData != nullptr
                && !(*MetaData)->Values.IsEmpty());
    }
    TestTrue(
        TEXT("Summary reports effective Property count"),
        HasCommentContaining(Summary, TEXT("properties:")));
    TestTrue(
        TEXT("Summary reports effective Function count"),
        HasCommentContaining(Summary, TEXT("functions:")));

    FProperty* ExpectedProperty =
        FindFProperty<FProperty>(Class, FName(TEXT("Tags")));
    UFunction* ExpectedFunction =
        Class->FindFunctionByName(FName(TEXT("K2_GetActorLocation")));
    TestNotNull(TEXT("Representative inherited Actor Property exists"), ExpectedProperty);
    TestNotNull(TEXT("Representative inherited Actor Function exists"), ExpectedFunction);
    if (ExpectedProperty == nullptr || ExpectedFunction == nullptr)
    {
        return false;
    }
    TestTrue(
        TEXT("Representative Property is inherited by Character"),
        ExpectedProperty->GetOwnerClass() != Class);
    TestTrue(
        TEXT("Representative Function is inherited by Character"),
        ExpectedFunction->GetOuterUClass() != Class);

    const TSharedPtr<FJsonObject> Properties =
        FSalClassInterface::Query(
            ClassQuery(TEXT("properties"), FString(), TEXT("Tags")),
            Target);
    TestTrue(
        TEXT("Effective Properties collection includes inherited declarations"),
        HasCallPath(
            Properties,
            TEXT("property"),
            ExpectedProperty->GetPathName()));

    const TSharedPtr<FJsonObject> Functions =
        FSalClassInterface::Query(
            ClassQuery(
                TEXT("functions"),
                FString(),
                TEXT("K2_GetActorLocation")),
            Target);
    TestTrue(
        TEXT("Effective Functions collection includes inherited declarations"),
        HasCallPath(
            Functions,
            TEXT("function"),
            ExpectedFunction->GetPathName()));

    FSalQuery ExactProperty =
        ClassQuery(TEXT("property"), TEXT("Tags"));
    ExactProperty.With.Add(TEXT("schema"));
    const TSharedPtr<FJsonObject> PropertyResult =
        FSalClassInterface::Query(ExactProperty, Target);
    const TArray<TSharedPtr<FJsonObject>> PropertyArgs =
        CallArgs(PropertyResult, TEXT("property"));
    TestEqual(
        TEXT("Exact Property query returns one result"),
        PropertyArgs.Num(),
        1);
    TestTrue(
        TEXT("Exact inherited Property resolves to its declaring native Path"),
        HasCallPath(
            PropertyResult,
            TEXT("property"),
            ExpectedProperty->GetPathName()));
    if (PropertyArgs.Num() == 1)
    {
        const TSharedPtr<FJsonObject>* MetaData = nullptr;
        TestTrue(
            TEXT("Exact Property exposes native Metadata"),
            PropertyArgs[0]->TryGetObjectField(TEXT("MetaData"), MetaData)
                && MetaData != nullptr
                && !(*MetaData)->Values.IsEmpty());
    }
    TestTrue(
        TEXT("Property schema records its declaring source"),
        HasCommentContaining(
            PropertyResult,
            ExpectedProperty->GetMetaData(TEXT("ModuleRelativePath")).IsEmpty()
                ? TEXT("source: ")
                    + ExpectedProperty->GetOwnerStruct()->GetPathName()
                : TEXT("source: native C++ ")
                    + ExpectedProperty->GetMetaData(TEXT("ModuleRelativePath"))));

    FSalQuery ExactFunction =
        ClassQuery(TEXT("function"), TEXT("K2_GetActorLocation"));
    ExactFunction.With.Add(TEXT("schema"));
    const TSharedPtr<FJsonObject> FunctionResult =
        FSalClassInterface::Query(ExactFunction, Target);
    const TArray<TSharedPtr<FJsonObject>> FunctionArgs =
        CallArgs(FunctionResult, TEXT("function"));
    TestEqual(
        TEXT("Exact Function query returns one result"),
        FunctionArgs.Num(),
        1);
    TestTrue(
        TEXT("Exact inherited Function resolves to its declaring native Path"),
        HasCallPath(
            FunctionResult,
            TEXT("function"),
            ExpectedFunction->GetPathName()));
    if (FunctionArgs.Num() == 1)
    {
        const TSharedPtr<FJsonObject>* MetaData = nullptr;
        TestTrue(
            TEXT("Exact Function exposes effective native Metadata"),
            FunctionArgs[0]->TryGetObjectField(TEXT("MetaData"), MetaData)
                && MetaData != nullptr
                && !(*MetaData)->Values.IsEmpty());
    }
    TestTrue(
        TEXT("Function schema records its declaring source"),
        HasCommentContaining(
            FunctionResult,
            ExpectedFunction->GetMetaData(TEXT("ModuleRelativePath")).IsEmpty()
                ? TEXT("source: ")
                    + ExpectedFunction->GetOuterUClass()->GetPathName()
                : TEXT("source: native C++ ")
                    + ExpectedFunction->GetMetaData(TEXT("ModuleRelativePath"))));

    FSalQuery RedundantInherited =
        ClassQuery(TEXT("property"), TEXT("Tags"));
    RedundantInherited.With.Add(TEXT("inherited"));
    TestTrue(
        TEXT("with inherited is rejected because Class reads are already effective"),
        HasDiagnosticCode(
            FSalClassInterface::Query(RedundantInherited, Target),
            TEXT("capability.unsupported_query")));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FSalClassDefaultsMutationSmokeTest,
    "Loomle.Sal.Class.Mutation.DefaultSetResetUndo",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSalClassDefaultsMutationSmokeTest::RunTest(const FString& Parameters)
{
    if (GEditor == nullptr)
    {
        AddError(TEXT("Class Defaults mutation test requires GEditor."));
        return false;
    }

    FClassDefaultsFixture Fixture(
        EClassDefaultsFixtureKind::Actor);
    TestNotNull(TEXT("Blueprint Generated Class fixture is created"), Fixture.Class);
    TestNotNull(TEXT("Blueprint Generated Class owns an Actor CDO"), Fixture.CDO);
    if (Fixture.Class == nullptr || Fixture.CDO == nullptr)
    {
        return false;
    }
    AActor* ActorCDO = CastChecked<AActor>(Fixture.CDO);

    const FSalResolvedTarget Target = ClassTarget(Fixture.Class);
    const float Before = ActorCDO->InitialLifeSpan;
    const float Desired = Before + 12.5f;
    const FString DesiredText = FString::SanitizeFloat(Desired);

    const TSharedPtr<FJsonObject> DryRun =
        FSalClassInterface::Patch(
            DefaultPatch(
                TEXT("set"),
                TEXT("InitialLifeSpan"),
                DesiredText,
                true),
            Target);
    TestTrue(TEXT("Default set dry-run validates"), ResultBool(DryRun, TEXT("valid")));
    TestTrue(TEXT("Default set reports dry-run"), ResultBool(DryRun, TEXT("dryRun")));
    TestFalse(
        TEXT("Default set dry-run does not apply"),
        ResultBool(DryRun, TEXT("applied"), true));
    TestTrue(
        TEXT("Default set dry-run preserves the live CDO"),
        FMath::IsNearlyEqual(ActorCDO->InitialLifeSpan, Before));
    TestFalse(
        TEXT("Default set dry-run preserves Package dirty state"),
        Fixture.Package->IsDirty());

    const TSharedPtr<FJsonObject> Applied =
        FSalClassInterface::Patch(
            DefaultPatch(
                TEXT("set"),
                TEXT("InitialLifeSpan"),
                DesiredText,
                false),
            Target);
    TestTrue(TEXT("Default set validates"), ResultBool(Applied, TEXT("valid")));
    TestTrue(TEXT("Default set applies"), ResultBool(Applied, TEXT("applied")));
    TestTrue(
        TEXT("Default set changes the exact generated CDO"),
        FMath::IsNearlyEqual(ActorCDO->InitialLifeSpan, Desired));

    const TSharedPtr<FJsonObject> Reset =
        FSalClassInterface::Patch(
            DefaultPatch(
                TEXT("reset"),
                TEXT("InitialLifeSpan"),
                FString(),
                false),
            Target);
    TestTrue(TEXT("Default reset validates"), ResultBool(Reset, TEXT("valid")));
    TestTrue(TEXT("Default reset applies"), ResultBool(Reset, TEXT("applied")));
    TestTrue(
        TEXT("Default reset restores the inherited value"),
        FMath::IsNearlyEqual(ActorCDO->InitialLifeSpan, Before));

    const bool bResetApplied =
        ResultBool(Reset, TEXT("applied"));
    TestTrue(
        TEXT("Reset is one UE transaction"),
        bResetApplied
            && GEditor->UndoTransaction(false));
    TestTrue(
        TEXT("Undo reset restores the local value"),
        FMath::IsNearlyEqual(ActorCDO->InitialLifeSpan, Desired));
    const bool bSetApplied =
        ResultBool(Applied, TEXT("applied"));
    TestTrue(
        TEXT("Set is one UE transaction"),
        bSetApplied
            && GEditor->UndoTransaction(false));
    TestTrue(
        TEXT("Undo set restores the original inherited value"),
        FMath::IsNearlyEqual(ActorCDO->InitialLifeSpan, Before));
    CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);
    TestTrue(
        TEXT("Class Default undo leaves the generated Class valid across GC"),
        IsValid(Fixture.Class));
    TestTrue(
        TEXT("Class Default undo leaves the CDO valid across GC"),
        IsValid(ActorCDO));

    Fixture.Package->SetDirtyFlag(false);
    UBlueprintGeneratedClass* DerivedWithoutCDO =
        NewObject<UBlueprintGeneratedClass>(
            GetTransientPackage(),
            MakeUniqueObjectName(
                GetTransientPackage(),
                UBlueprintGeneratedClass::StaticClass(),
                TEXT("SALClassDefaultsDerivedWithoutCDO")),
            RF_Transient);
    DerivedWithoutCDO->SetSuperStruct(Fixture.Class);
    TestNull(
        TEXT("Synthetic loaded derived BPGC starts without a CDO"),
        DerivedWithoutCDO->GetDefaultObject(false));
    const TSharedPtr<FJsonObject> UnsafeClassState =
        FSalClassInterface::Patch(
            DefaultPatch(
                TEXT("set"),
                TEXT("InitialLifeSpan"),
                DesiredText,
                false),
            Target);
    TestTrue(
        TEXT("Class Defaults rejects unsafe loaded derived Class state"),
        HasDiagnosticCode(
            UnsafeClassState,
            TEXT("validation.preflight_failed")));
    TestTrue(
        TEXT("Unsafe derived Class rejection preserves the CDO"),
        FMath::IsNearlyEqual(ActorCDO->InitialLifeSpan, Before));
    TestFalse(
        TEXT("Unsafe derived Class rejection preserves Package dirty state"),
        Fixture.Package->IsDirty());
    DerivedWithoutCDO->SetSuperStruct(nullptr);
    DerivedWithoutCDO->MarkAsGarbage();

    const TSharedPtr<FJsonObject> NativeMutation =
        FSalClassInterface::Patch(
            DefaultPatch(
                TEXT("set"),
                TEXT("InitialLifeSpan"),
                DesiredText,
                true),
            ClassTarget(AActor::StaticClass()));
    TestTrue(
        TEXT("Native Class Defaults remain read-only"),
        HasDiagnosticCode(
            NativeMutation,
            TEXT("validation.class_defaults_read_only")));
    FString CleanupError;
    const bool bCleaned = Fixture.Cleanup(CleanupError);
    TestTrue(
        *FString::Printf(
            TEXT("Class Defaults fixture unloads without resetting editor transactions: %s"),
            *CleanupError),
        bCleaned);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FSalSparseClassDefaultsUndoTest,
    "Loomle.Sal.Class.Mutation.SparseDefaultFirstSetUndo",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSalSparseClassDefaultsUndoTest::RunTest(const FString& Parameters)
{
    if (GEditor == nullptr)
    {
        AddError(TEXT("Sparse Class Defaults mutation test requires GEditor."));
        return false;
    }

    FClassDefaultsFixture Fixture(
        EClassDefaultsFixtureKind::Sparse);
    TestNotNull(
        TEXT("Sparse Class Defaults Blueprint fixture is created"),
        Fixture.Blueprint);
    TestNotNull(
        TEXT("Sparse Class Defaults generated Class is created"),
        Fixture.Class);
    if (Fixture.Blueprint == nullptr || Fixture.Class == nullptr)
    {
        return false;
    }

    UScriptStruct* SparseStruct =
        Fixture.Class->GetSparseClassDataStruct();
    TestNotNull(
        TEXT("Fixture inherits a native Sparse Class Data struct"),
        SparseStruct);
    TestNotNull(
        TEXT("Fixture exposes the test sparse property"),
        SparseStruct != nullptr
            ? FindFProperty<FIntProperty>(
                SparseStruct,
                TEXT("SparseValue"))
            : nullptr);
    if (SparseStruct == nullptr)
    {
        return false;
    }

    // Simulate a loaded generated Class that has the native sparse schema but
    // has not materialized a local sidecar yet.
    Fixture.Class->SetSparseClassDataStruct(nullptr);
    Fixture.Class->SetSparseClassDataStruct(SparseStruct);
    Fixture.Class->bIsSparseClassDataSerializable = false;
    TestNull(
        TEXT("Sparse fixture starts without local sidecar data"),
        Fixture.Class->GetSparseClassData(
            EGetSparseClassDataMethod::ReturnIfNull));

    const TSharedPtr<FJsonObject> Applied =
        FSalClassInterface::Patch(
            DefaultPatch(
                TEXT("set"),
                TEXT("SparseValue"),
                TEXT("41"),
                false),
            ClassTarget(Fixture.Class));
    TestTrue(
        TEXT("First sparse Default set validates"),
        ResultBool(Applied, TEXT("valid")));
    TestTrue(
        TEXT("First sparse Default set applies"),
        ResultBool(Applied, TEXT("applied")));
    TestEqual(
        TEXT("First sparse Default set materializes the requested value"),
        SparseValue(Fixture.Class),
        41);

    const bool bApplied =
        ResultBool(Applied, TEXT("applied"));
    TestTrue(
        TEXT("First sparse Default set is one UE transaction"),
        bApplied
            && GEditor->UndoTransaction(false));
    TestEqual(
        TEXT("Undo restores the inherited sparse value"),
        SparseValue(Fixture.Class),
        17);
    TestTrue(
        TEXT("Undo keeps the generated Class sparse sidecar serializable"),
        Fixture.Class->bIsSparseClassDataSerializable);

    CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);
    TestTrue(
        TEXT("Sparse Default undo leaves the generated Class valid across GC"),
        IsValid(Fixture.Class));
    Fixture.Package->SetDirtyFlag(false);
    FString CleanupError;
    const bool bCleaned = Fixture.Cleanup(CleanupError);
    TestTrue(
        *FString::Printf(
            TEXT("Sparse Class Defaults fixture unloads without resetting editor transactions: %s"),
            *CleanupError),
        bCleaned);
    return true;
}

#endif
