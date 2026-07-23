// Copyright 2026 Loomle contributors.

#if WITH_DEV_AUTOMATION_TESTS

#include "Sal/Class/SalClassInterface.h"
#include "SalClassSparseTestTypes.h"
#include "Tests/LoomleTestEditorState.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Editor.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Character.h"
#include "HAL/FileManager.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/AutomationTest.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "UObject/GarbageCollection.h"
#include "UObject/Package.h"
#include "UObject/UObjectGlobals.h"
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

FSalPatch DefaultValuePatch(
    const FString& Property,
    const TSharedPtr<FJsonValue>& Value,
    const bool bDryRun)
{
    static const FString Alias = TEXT("actorClass");
    TSharedRef<FJsonObject> Statement = MakeShared<FJsonObject>();
    Statement->SetStringField(TEXT("kind"), TEXT("set"));
    Statement->SetObjectField(
        TEXT("target"),
        DefaultReference(Alias, Property));
    Statement->SetField(TEXT("value"), Value);

    FSalPatch Patch;
    Patch.Alias = Alias;
    Patch.bDryRun = bDryRun;
    Patch.Statements = {MakeShared<FJsonValueObject>(Statement)};
    return Patch;
}

FSalPatch ClassSavePatch(const bool bDryRun)
{
    TSharedRef<FJsonObject> Statement = MakeShared<FJsonObject>();
    Statement->SetStringField(TEXT("kind"), TEXT("save"));
    FSalPatch Patch;
    Patch.Alias = TEXT("actorClass");
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

TSharedPtr<FJsonObject> OverriddenCondition(const bool bValue)
{
    TSharedPtr<FJsonObject> Field = MakeShared<FJsonObject>();
    Field->SetArrayField(
        TEXT("path"),
        {MakeShared<FJsonValueString>(TEXT("overridden"))});
    TSharedPtr<FJsonObject> Condition = MakeShared<FJsonObject>();
    Condition->SetStringField(TEXT("kind"), TEXT("eq"));
    Condition->SetObjectField(TEXT("field"), Field);
    Condition->SetBoolField(TEXT("value"), bValue);
    return Condition;
}

TSharedPtr<FJsonObject> ClassOrderBy(const FString& Key)
{
    TSharedPtr<FJsonObject> Order = MakeShared<FJsonObject>();
    Order->SetStringField(TEXT("key"), Key);
    Order->SetStringField(TEXT("direction"), TEXT("asc"));
    return Order;
}

FString ClassNextCursor(const TSharedPtr<FJsonObject>& Result)
{
    const TSharedPtr<FJsonObject>* Page = nullptr;
    FString Next;
    if (Result.IsValid()
        && Result->TryGetObjectField(TEXT("page"), Page)
        && Page != nullptr)
    {
        (*Page)->TryGetStringField(TEXT("next"), Next);
    }
    return Next;
}

bool HasMemberBinding(
    const TSharedPtr<FJsonObject>& Result,
    const FString& Field,
    const FString& Expected)
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
        const TSharedPtr<FJsonObject>* Target = nullptr;
        const TArray<TSharedPtr<FJsonValue>>* Path = nullptr;
        FString Kind;
        FString ActualField;
        FString ActualValue;
        if (StatementValue.IsValid()
            && StatementValue->TryGetObject(Statement)
            && Statement != nullptr
            && (*Statement)->TryGetObjectField(TEXT("target"), Target)
            && Target != nullptr
            && (*Target)->TryGetStringField(TEXT("kind"), Kind)
            && Kind == TEXT("member")
            && (*Target)->TryGetArrayField(TEXT("path"), Path)
            && Path != nullptr
            && Path->Num() == 1
            && (*Path)[0]->TryGetString(ActualField)
            && ActualField == Field
            && (*Statement)->TryGetStringField(TEXT("value"), ActualValue)
            && (Expected.IsEmpty() || ActualValue == Expected))
        {
            return true;
        }
    }
    return false;
}

bool JsonContainsStringArray(
    const TSharedPtr<FJsonValue>& Value,
    const TArray<FString>& Expected)
{
    if (!Value.IsValid())
    {
        return false;
    }
    const TArray<TSharedPtr<FJsonValue>>* Array = nullptr;
    if (Value->TryGetArray(Array)
        && Array != nullptr
        && Array->Num() == Expected.Num())
    {
        bool bMatches = true;
        for (int32 Index = 0; Index < Expected.Num(); ++Index)
        {
            FString Actual;
            bMatches = bMatches
                && (*Array)[Index].IsValid()
                && (*Array)[Index]->TryGetString(Actual)
                && Actual == Expected[Index];
        }
        if (bMatches)
        {
            return true;
        }
    }
    const TSharedPtr<FJsonObject>* Object = nullptr;
    if (Value->TryGetObject(Object)
        && Object != nullptr
        && (*Object).IsValid())
    {
        for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair :
             (*Object)->Values)
        {
            if (JsonContainsStringArray(Pair.Value, Expected))
            {
                return true;
            }
        }
    }
    if (Value->TryGetArray(Array) && Array != nullptr)
    {
        for (const TSharedPtr<FJsonValue>& Item : *Array)
        {
            if (JsonContainsStringArray(Item, Expected))
            {
                return true;
            }
        }
    }
    return false;
}

TSharedPtr<FJsonValue> NativeStringArray(
    const TArray<FString>& Values)
{
    TArray<TSharedPtr<FJsonValue>> JsonValues;
    JsonValues.Reserve(Values.Num());
    for (const FString& Value : Values)
    {
        JsonValues.Add(MakeShared<FJsonValueString>(Value));
    }
    return MakeShared<FJsonValueArray>(MoveTemp(JsonValues));
}

void PrepareClassPackageForCollection(UPackage* Package)
{
    if (Package == nullptr)
    {
        return;
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
}

class FClassPersistenceFixture
{
public:
    ~FClassPersistenceFixture()
    {
        FString Ignored;
        Cleanup(Ignored);
    }

    bool Create(FString& OutError)
    {
        const FString Token =
            FGuid::NewGuid().ToString(EGuidFormats::Digits);
        PackageName = FString::Printf(
            TEXT("/Game/LoomleTests/ClassPersistence/%s/BP_Class"),
            *Token);
        ObjectPath = PackageName + TEXT(".BP_Class");
        Filename = FPackageName::LongPackageNameToFilename(
            PackageName,
            FPackageName::GetAssetPackageExtension());
        IFileManager::Get().MakeDirectory(
            *FPaths::GetPath(Filename),
            true);
        Package = CreatePackage(*PackageName);
        Blueprint =
            Package != nullptr
                ? FKismetEditorUtilities::CreateBlueprint(
                    USalClassSparseTestObject::StaticClass(),
                    Package,
                    FName(TEXT("BP_Class")),
                    BPTYPE_Normal,
                    UBlueprint::StaticClass(),
                    UBlueprintGeneratedClass::StaticClass(),
                    NAME_None)
                : nullptr;
        if (Blueprint == nullptr)
        {
            OutError =
                TEXT("UE failed to create the persistent Class fixture.");
            return false;
        }
        FAssetRegistryModule::AssetCreated(Blueprint);
        bRegistered = true;
        FKismetEditorUtilities::CompileBlueprint(Blueprint);
        return Refresh(OutError);
    }

    bool Unload(FString& OutError)
    {
        PrepareClassPackageForCollection(Package);
        CDO = nullptr;
        Class = nullptr;
        Blueprint = nullptr;
        Package = nullptr;
        CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);
        if (FindPackage(nullptr, *PackageName) != nullptr
            || FindObject<UObject>(nullptr, *ObjectPath) != nullptr)
        {
            OutError =
                TEXT("Persistent Class fixture remained loaded after GC.");
            return false;
        }
        return true;
    }

    bool Reload(FString& OutError)
    {
        Blueprint = LoadObject<UBlueprint>(nullptr, *ObjectPath);
        Package =
            Blueprint != nullptr ? Blueprint->GetOutermost() : nullptr;
        return Refresh(OutError);
    }

    bool Cleanup(FString& OutError)
    {
        if (bCleaned)
        {
            return OutError.IsEmpty();
        }
        bCleaned = true;
        UObject* Loaded =
            !ObjectPath.IsEmpty()
                ? FindObject<UObject>(nullptr, *ObjectPath)
                : nullptr;
        if (Loaded != nullptr && bRegistered)
        {
            FAssetRegistryModule::AssetDeleted(Loaded);
            bRegistered = false;
        }
        UPackage* LoadedPackage =
            !PackageName.IsEmpty()
                ? FindPackage(nullptr, *PackageName)
                : nullptr;
        PrepareClassPackageForCollection(LoadedPackage);
        CDO = nullptr;
        Class = nullptr;
        Blueprint = nullptr;
        Package = nullptr;
        CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);
        if (!PackageName.IsEmpty()
            && FindPackage(nullptr, *PackageName) != nullptr)
        {
            OutError =
                TEXT("Persistent Class package remained loaded during cleanup.");
        }
        if (!Filename.IsEmpty()
            && IFileManager::Get().FileExists(*Filename)
            && !IFileManager::Get().Delete(*Filename, false, true, true))
        {
            if (!OutError.IsEmpty())
            {
                OutError += TEXT(" ");
            }
            OutError += TEXT("Persistent Class file could not be deleted.");
        }
        if (!Filename.IsEmpty())
        {
            IFileManager::Get().DeleteDirectory(
                *FPaths::GetPath(Filename),
                false,
                true);
        }
        return OutError.IsEmpty();
    }

    bool Refresh(FString& OutError)
    {
        Class =
            Blueprint != nullptr
                ? Cast<UBlueprintGeneratedClass>(
                    Blueprint->GeneratedClass)
                : nullptr;
        CDO =
            Class != nullptr
                ? Cast<USalClassSparseTestObject>(
                    Class->GetDefaultObject())
                : nullptr;
        if (Blueprint == nullptr
            || Package == nullptr
            || Class == nullptr
            || CDO == nullptr
            || Blueprint->Status == BS_Error)
        {
            OutError =
                TEXT("Persistent Class fixture has no valid generated Class/CDO.");
            return false;
        }
        return true;
    }

    UPackage* Package = nullptr;
    UBlueprint* Blueprint = nullptr;
    UBlueprintGeneratedClass* Class = nullptr;
    USalClassSparseTestObject* CDO = nullptr;
    FString PackageName;
    FString ObjectPath;
    FString Filename;

private:
    bool bRegistered = false;
    bool bCleaned = false;
};
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FSalClassDefaultsQueryContractTest,
    "Loomle.Sal.Class.Query.DefaultSchemaClausesAndPagination",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSalClassDefaultsQueryContractTest::RunTest(
    const FString& Parameters)
{
    if (GEditor == nullptr)
    {
        AddError(TEXT("Class Defaults Query test requires GEditor."));
        return false;
    }
    Loomle::Tests::FScopedIsolatedTransactor Transactions;
    if (!TestTrue(
            TEXT("Class Defaults Query test isolates transaction history"),
            Transactions.Initialize()))
    {
        return false;
    }

    FClassDefaultsFixture Fixture(
        EClassDefaultsFixtureKind::Actor);
    TestNotNull(
        TEXT("Class Query fixture creates a generated Class"),
        Fixture.Class);
    if (Fixture.Class == nullptr)
    {
        return false;
    }
    const FSalResolvedTarget Target = ClassTarget(Fixture.Class);

    FSalQuery Exact =
        ClassQuery(TEXT("default"), TEXT("InitialLifeSpan"));
    Exact.With.Add(TEXT("schema"));
    const TSharedPtr<FJsonObject> ExactResult =
        FSalClassInterface::Query(Exact, Target);
    TestFalse(
        TEXT("Exact Default with schema succeeds"),
        HasDiagnosticCode(
            ExactResult,
            TEXT("capability.unsupported_query")));
    TestTrue(
        TEXT("Exact Default schema exposes native UE type"),
        HasCommentContaining(
            ExactResult,
            TEXT("subject: default")));
    TestTrue(
        TEXT("Exact Default schema declares write/reset capability"),
        HasCommentContaining(
            ExactResult,
            TEXT("writable: true")));

    const float Desired =
        CastChecked<AActor>(Fixture.CDO)->InitialLifeSpan + 31.0f;
    const TSharedPtr<FJsonObject> Applied =
        FSalClassInterface::Patch(
            DefaultPatch(
                TEXT("set"),
                TEXT("InitialLifeSpan"),
                FString::SanitizeFloat(Desired),
                false),
            Target);
    TestTrue(
        TEXT("Query setup Default mutation applies"),
        ResultBool(Applied, TEXT("applied")));
    TestTrue(
        TEXT("Query setup mutation changes the native CDO"),
        FMath::IsNearlyEqual(
            CastChecked<AActor>(Fixture.CDO)->InitialLifeSpan,
            Desired));

    FSalQuery Overridden =
        ClassQuery(
            TEXT("defaults"),
            FString(),
            TEXT("InitialLifeSpan"));
    Overridden.Where = OverriddenCondition(true);
    Overridden.PageLimit = 1;
    const TSharedPtr<FJsonObject> OverriddenResult =
        FSalClassInterface::Query(Overridden, Target);
    TestTrue(
        TEXT("where overridden=true returns the local override"),
        HasMemberBinding(
            OverriddenResult,
            TEXT("InitialLifeSpan"),
            FString()));

    FSalQuery Page = ClassQuery(TEXT("properties"));
    Page.PageLimit = 1;
    const TSharedPtr<FJsonObject> FirstPage =
        FSalClassInterface::Query(Page, Target);
    const FString Cursor = ClassNextCursor(FirstPage);
    TestFalse(
        TEXT("Plural Class query emits a continuation cursor"),
        Cursor.IsEmpty());
    Page.PageAfter = Cursor;
    TestEqual(
        TEXT("Class continuation page preserves its page bound"),
        CallArgs(
            FSalClassInterface::Query(Page, Target),
            TEXT("property")).Num(),
        1);

    FSalQuery ChangedPage =
        ClassQuery(TEXT("properties"), FString(), TEXT("Actor"));
    ChangedPage.PageLimit = 1;
    ChangedPage.PageAfter = Cursor;
    TestTrue(
        TEXT("Class cursor cannot be reused after changing search"),
        HasDiagnosticCode(
            FSalClassInterface::Query(ChangedPage, Target),
            TEXT("validation.invalid_cursor")));

    FSalQuery SingularPage =
        ClassQuery(TEXT("default"), TEXT("InitialLifeSpan"));
    SingularPage.PageLimit = 1;
    TestTrue(
        TEXT("Singular Class query rejects pagination"),
        HasDiagnosticCode(
            FSalClassInterface::Query(SingularPage, Target),
            TEXT("capability.unsupported_query")));

    FSalQuery Ordered = ClassQuery(TEXT("defaults"));
    Ordered.OrderBy = {ClassOrderBy(TEXT("name"))};
    TestTrue(
        TEXT("Class rejects undeclared order by clauses"),
        HasDiagnosticCode(
            FSalClassInterface::Query(Ordered, Target),
            TEXT("capability.unsupported_query")));

    FSalQuery InvalidWhere = ClassQuery(TEXT("defaults"));
    InvalidWhere.Where = OverriddenCondition(false);
    TestTrue(
        TEXT("Class rejects where overridden=false"),
        HasDiagnosticCode(
            FSalClassInterface::Query(InvalidWhere, Target),
            TEXT("capability.unsupported_query")));

    FSalQuery PluralSchema = ClassQuery(TEXT("defaults"));
    PluralSchema.With.Add(TEXT("schema"));
    TestTrue(
        TEXT("Class schema discovery stays exact-object only"),
        HasDiagnosticCode(
            FSalClassInterface::Query(PluralSchema, Target),
            TEXT("capability.unsupported_query")));

    TestTrue(
        TEXT("Query setup mutation remains one native Undo step"),
        GEditor->UndoTransaction(false));
    Transactions.Restore();
    Fixture.Package->SetDirtyFlag(false);
    FString CleanupError;
    const bool bCleaned = Fixture.Cleanup(CleanupError);
    TestTrue(
        *FString::Printf(
            TEXT("Class Query fixture unloads cleanly: %s"),
            *CleanupError),
        bCleaned);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FSalClassFixedArrayPersistenceTest,
    "Loomle.Sal.Class.Mutation.FixedArraySaveUnloadReload",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSalClassFixedArrayPersistenceTest::RunTest(
    const FString& Parameters)
{
    Loomle::Tests::FScopedIsolatedTransactor Transactions;
    if (!TestTrue(
            TEXT("Fixed-array persistence test isolates transaction history"),
            Transactions.Initialize()))
    {
        return false;
    }

    FClassPersistenceFixture Fixture;
    FString Error;
    const bool bCreated = Fixture.Create(Error);
    TestTrue(
        *FString::Printf(
            TEXT("Persistent fixed-array Class fixture is created: %s"),
            *Error),
        bCreated);
    if (!bCreated)
    {
        return false;
    }

    FSalQuery Exact =
        ClassQuery(TEXT("default"), TEXT("FixedValues"));
    Exact.With.Add(TEXT("schema"));
    const TSharedPtr<FJsonObject> Initial =
        FSalClassInterface::Query(
            Exact,
            ClassTarget(Fixture.Class));
    TestTrue(
        TEXT("Fixed-array schema preserves native ArrayDim"),
        HasCommentContaining(
            Initial,
            TEXT("value shape: fixed array of 3 native UE strings")));
    TestTrue(
        TEXT("Fixed-array query returns the native initial values"),
        JsonContainsStringArray(
            MakeShared<FJsonValueObject>(Initial),
            {TEXT("1"), TEXT("2"), TEXT("3")}));

    const TSharedPtr<FJsonObject> WrongArity =
        FSalClassInterface::Patch(
            DefaultValuePatch(
                TEXT("FixedValues"),
                NativeStringArray({TEXT("10"), TEXT("20")}),
                true),
            ClassTarget(Fixture.Class));
    TestTrue(
        TEXT("Fixed-array Default rejects the wrong element count"),
        HasDiagnosticCode(
            WrongArity,
            TEXT("validation.default_import_failed")));

    const TArray<FString> Desired = {
        TEXT("10"), TEXT("20"), TEXT("30")};
    const TSharedPtr<FJsonObject> DryRun =
        FSalClassInterface::Patch(
            DefaultValuePatch(
                TEXT("FixedValues"),
                NativeStringArray(Desired),
                true),
            ClassTarget(Fixture.Class));
    TestTrue(
        TEXT("Fixed-array Default dry-run validates"),
        ResultBool(DryRun, TEXT("valid")));
    TestFalse(
        TEXT("Fixed-array Default dry-run does not apply"),
        ResultBool(DryRun, TEXT("applied"), true));
    TestTrue(
        TEXT("Fixed-array dry-run preserves every native element"),
        Fixture.CDO->FixedValues[0] == 1
            && Fixture.CDO->FixedValues[1] == 2
            && Fixture.CDO->FixedValues[2] == 3);

    const TSharedPtr<FJsonObject> Applied =
        FSalClassInterface::Patch(
            DefaultValuePatch(
                TEXT("FixedValues"),
                NativeStringArray(Desired),
                false),
            ClassTarget(Fixture.Class));
    TestTrue(
        TEXT("Fixed-array Default set applies"),
        ResultBool(Applied, TEXT("applied")));
    TestTrue(
        TEXT("Fixed-array Default set updates every native element"),
        Fixture.CDO->FixedValues[0] == 10
            && Fixture.CDO->FixedValues[1] == 20
            && Fixture.CDO->FixedValues[2] == 30);

    const TSharedPtr<FJsonObject> SaveDryRun =
        FSalClassInterface::Patch(
            ClassSavePatch(true),
            ClassTarget(Fixture.Class));
    TestTrue(
        TEXT("Class source save dry-run validates"),
        ResultBool(SaveDryRun, TEXT("valid")));
    TestFalse(
        TEXT("Class source save dry-run does not write"),
        ResultBool(SaveDryRun, TEXT("applied"), true));
    TestTrue(
        TEXT("Class source save dry-run preserves dirty state"),
        Fixture.Package->IsDirty());
    TestFalse(
        TEXT("Class source save dry-run creates no file"),
        IFileManager::Get().FileExists(*Fixture.Filename));

    const TSharedPtr<FJsonObject> Saved =
        FSalClassInterface::Patch(
            ClassSavePatch(false),
            ClassTarget(Fixture.Class));
    TestTrue(
        TEXT("Class source save validates"),
        ResultBool(Saved, TEXT("valid")));
    TestTrue(
        TEXT("Class source save writes the dirty Blueprint package"),
        ResultBool(Saved, TEXT("applied")));
    TestFalse(
        TEXT("Class source package is clean after save"),
        Fixture.Package->IsDirty());

    const TSharedPtr<FJsonObject> NativeSave =
        FSalClassInterface::Patch(
            ClassSavePatch(true),
            ClassTarget(AActor::StaticClass()));
    TestTrue(
        TEXT("Native Class has no saveable Blueprint source"),
        HasDiagnosticCode(
            NativeSave,
            TEXT("validation.class_source_required")));

    Transactions.Restore();
    Error.Reset();
    const bool bUnloaded = Fixture.Unload(Error);
    TestTrue(
        *FString::Printf(
            TEXT("Saved Class fixture unloads: %s"),
            *Error),
        bUnloaded);
    Error.Reset();
    const bool bReloaded = Fixture.Reload(Error);
    TestTrue(
        *FString::Printf(
            TEXT("Saved Class fixture reloads: %s"),
            *Error),
        bReloaded);
    if (Fixture.CDO != nullptr)
    {
        TestTrue(
            TEXT("Reload preserves every fixed-array Default element"),
            Fixture.CDO->FixedValues[0] == 10
                && Fixture.CDO->FixedValues[1] == 20
                && Fixture.CDO->FixedValues[2] == 30);
        const TSharedPtr<FJsonObject> Readback =
            FSalClassInterface::Query(
                Exact,
                ClassTarget(Fixture.Class));
        TestTrue(
            TEXT("SAL readback preserves the fixed-array value shape"),
            JsonContainsStringArray(
                MakeShared<FJsonValueObject>(Readback),
                Desired));

        const TSharedPtr<FJsonObject> CleanSave =
            FSalClassInterface::Patch(
                ClassSavePatch(false),
                ClassTarget(Fixture.Class));
        TestTrue(
            TEXT("Reloaded clean Class source remains save-valid"),
            ResultBool(CleanSave, TEXT("valid")));
        TestFalse(
            TEXT("Reloaded clean Class source save is a no-op"),
            ResultBool(CleanSave, TEXT("applied"), true));
    }

    Error.Reset();
    const bool bCleaned = Fixture.Cleanup(Error);
    TestTrue(
        *FString::Printf(
            TEXT("Persistent Class fixture is removed: %s"),
            *Error),
        bCleaned);
    return true;
}

#endif
