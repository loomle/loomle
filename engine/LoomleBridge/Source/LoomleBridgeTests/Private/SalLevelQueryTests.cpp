// Copyright 2026 Loomle contributors.

#if WITH_DEV_AUTOMATION_TESTS

#include "LoomleTestObjectIteration.h"
#include "EditorContext/EditorContextService.h"
#include "Sal/Level/SalLevelInterface.h"
#include "Sal/SalJson.h"
#include "Sal/SalModule.h"
#include "Sal/SalTargetResolver.h"

#include "AssetRegistry/AssetData.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Components/ActorComponent.h"
#include "Components/SceneComponent.h"
#include "ComponentInstanceDataCache.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Editor.h"
#include "EditorWorldUtils.h"
#include "Editor/Transactor.h"
#include "Elements/Framework/EngineElementsLibrary.h"
#include "Elements/Framework/TypedElementSelectionSet.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Engine/Level.h"
#include "Engine/SCS_Node.h"
#include "Engine/Selection.h"
#include "Engine/SimpleConstructionScript.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformTime.h"
#include "Helpers/PCGHelpers.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "LevelInstance/LevelInstanceActor.h"
#include "LevelInstance/LevelInstanceSubsystem.h"
#include "Misc/AutomationTest.h"
#include "Misc/CoreDelegates.h"
#include "Misc/EngineVersionComparison.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "PCGComponent.h"
#include "PCGGraph.h"
#include "PCGVolume.h"
#include "StructUtils/PropertyBag.h"
#include "UObject/GarbageCollection.h"
#include "UObject/ObjectSaveContext.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/UObjectHash.h"
#include "UObject/UObjectIterator.h"
#include "UObject/UnrealType.h"
#include "WorldPartition/ActorDescContainerInstance.h"
#include "WorldPartition/IWorldPartitionEditorModule.h"
#include "WorldPartition/WorldPartition.h"
#include "WorldPartition/WorldPartitionActorDesc.h"
#include "WorldPartition/WorldPartitionActorDescInstance.h"
#include "WorldPartition/WorldPartitionHandle.h"

namespace
{
using namespace Loomle::Sal;

FString LevelGuidText(const FGuid& Guid)
{
    return Guid.ToString(EGuidFormats::DigitsWithHyphensLower);
}

TArray<TSharedPtr<FJsonValue>> LevelStringValues(
    const TArray<FString>& Strings)
{
    TArray<TSharedPtr<FJsonValue>> Values;
    Values.Reserve(Strings.Num());
    for (const FString& String : Strings)
    {
        Values.Add(MakeShared<FJsonValueString>(String));
    }
    return Values;
}

TSharedRef<FJsonObject> LevelTarget(
    const FString& Asset,
    const FString& Type = FString())
{
    TSharedRef<FJsonObject> Target = MakeShared<FJsonObject>();
    Target->SetStringField(TEXT("kind"), TEXT("target"));
    Target->SetStringField(TEXT("domain"), TEXT("level"));
    Target->SetStringField(TEXT("asset"), Asset);
    if (!Type.IsEmpty())
    {
        Target->SetStringField(TEXT("type"), Type);
    }
    return Target;
}

TSharedRef<FJsonObject> PCGComponentTarget(
    const FString& Asset,
    const FGuid& ActorId,
    const FString& Source,
    const FString& Id,
    const FString& Type = UPCGComponent::StaticClass()->GetPathName())
{
    TSharedRef<FJsonObject> Target = MakeShared<FJsonObject>();
    Target->SetStringField(TEXT("kind"), TEXT("target"));
    Target->SetStringField(TEXT("domain"), TEXT("pcg_component"));
    Target->SetStringField(TEXT("asset"), Asset);
    Target->SetStringField(TEXT("actorId"), LevelGuidText(ActorId));
    Target->SetStringField(TEXT("source"), Source);
    Target->SetStringField(TEXT("id"), Id);
    Target->SetStringField(TEXT("type"), Type);
    return Target;
}

TSharedRef<FJsonObject> LevelTargetBinding(
    const TSharedRef<FJsonObject>& Target)
{
    TSharedRef<FJsonObject> Binding = MakeShared<FJsonObject>();
    Binding->SetStringField(TEXT("alias"), TEXT("level_scope"));
    Binding->SetObjectField(TEXT("target"), Target);
    return Binding;
}

TSharedRef<FJsonObject> LevelOperation(
    const FString& Kind,
    const FString& SearchText = FString())
{
    TSharedRef<FJsonObject> Operation = MakeShared<FJsonObject>();
    Operation->SetStringField(TEXT("kind"), Kind);
    if (!SearchText.IsEmpty())
    {
        Operation->SetStringField(TEXT("text"), SearchText);
    }
    return Operation;
}

TSharedRef<FJsonObject> LevelStableRef(const FString& ActorId)
{
    TSharedRef<FJsonObject> Ref = MakeShared<FJsonObject>();
    Ref->SetStringField(TEXT("kind"), TEXT("stable_ref"));
    Ref->SetArrayField(
        TEXT("identityPath"),
        LevelStringValues({ActorId}));
    return Ref;
}

TSharedRef<FJsonObject> LevelComponentStableRef(
    const FString& ActorId,
    const FString& Source,
    const FString& Id)
{
    TSharedRef<FJsonObject> Ref = MakeShared<FJsonObject>();
    Ref->SetStringField(TEXT("kind"), TEXT("stable_ref"));
    Ref->SetArrayField(
        TEXT("identityPath"),
        LevelStringValues({ActorId, Source, Id}));
    return Ref;
}

TSharedRef<FJsonObject> LevelExactOperation(const FString& ActorId)
{
    TSharedRef<FJsonObject> Operation = LevelOperation(TEXT("object"));
    Operation->SetObjectField(TEXT("target"), LevelStableRef(ActorId));
    return Operation;
}

TSharedRef<FJsonObject> LevelExactComponentOperation(
    const FString& ActorId,
    const FString& Source,
    const FString& Id)
{
    TSharedRef<FJsonObject> Operation = LevelOperation(TEXT("object"));
    Operation->SetObjectField(
        TEXT("target"),
        LevelComponentStableRef(ActorId, Source, Id));
    return Operation;
}

TSharedRef<FJsonObject> LevelQueryArguments(
    const TSharedRef<FJsonObject>& Target,
    const TSharedRef<FJsonObject>& Operation,
    const int32 PageLimit = 0,
    const FString& PageAfter = FString())
{
    TSharedRef<FJsonObject> Query = MakeShared<FJsonObject>();
    Query->SetStringField(TEXT("kind"), TEXT("query"));
    Query->SetObjectField(TEXT("target"), LevelTargetBinding(Target));
    Query->SetObjectField(TEXT("operation"), Operation);
    if (PageLimit > 0 || !PageAfter.IsEmpty())
    {
        TSharedRef<FJsonObject> Page = MakeShared<FJsonObject>();
        if (PageLimit > 0)
        {
            Page->SetNumberField(TEXT("limit"), PageLimit);
        }
        if (!PageAfter.IsEmpty())
        {
            Page->SetStringField(TEXT("after"), PageAfter);
        }
        Query->SetObjectField(TEXT("page"), Page);
    }

    TSharedRef<FJsonObject> Arguments = MakeShared<FJsonObject>();
    Arguments->SetObjectField(TEXT("object"), Query);
    return Arguments;
}

TSharedRef<FJsonObject> PCGComponentQueryArguments(
    const TSharedRef<FJsonObject>& Target,
    const TSharedRef<FJsonObject>& Operation,
    const bool bWithSchema = false,
    const int32 PageLimit = 0,
    const FString& PageAfter = FString())
{
    TSharedRef<FJsonObject> Binding = MakeShared<FJsonObject>();
    Binding->SetStringField(TEXT("alias"), TEXT("pcg_component_scope"));
    Binding->SetObjectField(TEXT("target"), Target);

    TSharedRef<FJsonObject> Query = MakeShared<FJsonObject>();
    Query->SetStringField(TEXT("kind"), TEXT("query"));
    Query->SetObjectField(TEXT("target"), Binding);
    Query->SetObjectField(TEXT("operation"), Operation);
    if (bWithSchema)
    {
        Query->SetArrayField(
            TEXT("with"),
            {MakeShared<FJsonValueString>(TEXT("schema"))});
    }
    if (PageLimit > 0 || !PageAfter.IsEmpty())
    {
        TSharedRef<FJsonObject> Page = MakeShared<FJsonObject>();
        if (PageLimit > 0)
        {
            Page->SetNumberField(TEXT("limit"), PageLimit);
        }
        if (!PageAfter.IsEmpty())
        {
            Page->SetStringField(TEXT("after"), PageAfter);
        }
        Query->SetObjectField(TEXT("page"), Page);
    }

    TSharedRef<FJsonObject> Arguments = MakeShared<FJsonObject>();
    Arguments->SetObjectField(TEXT("object"), Query);
    return Arguments;
}

TSharedRef<FJsonObject> PCGComponentParameterStableRef(
    const FGuid& ParameterId)
{
    TSharedRef<FJsonObject> Ref = MakeShared<FJsonObject>();
    Ref->SetStringField(TEXT("kind"), TEXT("stable_ref"));
    Ref->SetArrayField(
        TEXT("identityPath"),
        LevelStringValues({LevelGuidText(ParameterId)}));
    return Ref;
}

TSharedRef<FJsonObject> PCGComponentExactParameterOperation(
    const FGuid& ParameterId)
{
    TSharedRef<FJsonObject> Operation = LevelOperation(TEXT("object"));
    Operation->SetObjectField(
        TEXT("target"),
        PCGComponentParameterStableRef(ParameterId));
    return Operation;
}

TSharedRef<FJsonObject> PCGComponentPatchArguments(
    const TSharedRef<FJsonObject>& Target)
{
    TSharedRef<FJsonObject> Binding = MakeShared<FJsonObject>();
    Binding->SetStringField(TEXT("alias"), TEXT("pcg_component_scope"));
    Binding->SetObjectField(TEXT("target"), Target);

    TSharedRef<FJsonObject> Save = MakeShared<FJsonObject>();
    Save->SetStringField(TEXT("kind"), TEXT("save"));

    TSharedRef<FJsonObject> Patch = MakeShared<FJsonObject>();
    Patch->SetStringField(TEXT("kind"), TEXT("patch"));
    Patch->SetObjectField(TEXT("target"), Binding);
    Patch->SetBoolField(TEXT("dryRun"), true);
    Patch->SetArrayField(
        TEXT("statements"),
        {MakeShared<FJsonValueObject>(Save)});

    TSharedRef<FJsonObject> Arguments = MakeShared<FJsonObject>();
    Arguments->SetObjectField(TEXT("object"), Patch);
    return Arguments;
}

bool LevelHasDiagnostic(
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
    for (const TSharedPtr<FJsonValue>& Value : *Diagnostics)
    {
        const TSharedPtr<FJsonObject>* Diagnostic = nullptr;
        FString Code;
        if (Value.IsValid()
            && Value->TryGetObject(Diagnostic)
            && Diagnostic != nullptr
            && (*Diagnostic).IsValid()
            && (*Diagnostic)->TryGetStringField(TEXT("code"), Code)
            && Code == ExpectedCode)
        {
            return true;
        }
    }
    return false;
}

bool LevelHasError(const TSharedPtr<FJsonObject>& Result)
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
            && (*Diagnostic).IsValid()
            && (*Diagnostic)->TryGetStringField(TEXT("severity"), Severity)
            && Severity == TEXT("error"))
        {
            return true;
        }
    }
    return false;
}

bool LevelHasTargetContext(
    const TSharedPtr<FJsonObject>& Result,
    const FString& Expected)
{
    FString Context;
    return Result.IsValid()
        && Result->TryGetStringField(TEXT("targetContext"), Context)
        && Context == Expected;
}

bool HasCanonicalLevelTarget(
    const TSharedPtr<FJsonObject>& Result,
    const FString& ExpectedAsset)
{
    const TSharedPtr<FJsonObject>* Binding = nullptr;
    const TSharedPtr<FJsonObject>* Target = nullptr;
    FString Domain;
    FString Asset;
    FString Type;
    return Result.IsValid()
        && Result->TryGetObjectField(TEXT("target"), Binding)
        && Binding != nullptr
        && (*Binding).IsValid()
        && (*Binding)->TryGetObjectField(TEXT("target"), Target)
        && Target != nullptr
        && (*Target).IsValid()
        && (*Target)->TryGetStringField(TEXT("domain"), Domain)
        && Domain == TEXT("level")
        && (*Target)->TryGetStringField(TEXT("asset"), Asset)
        && Asset == ExpectedAsset
        && (*Target)->TryGetStringField(TEXT("type"), Type)
        && Type == UWorld::StaticClass()->GetPathName();
}

bool HasCanonicalPCGComponentTarget(
    const TSharedPtr<FJsonObject>& Result,
    const FString& ExpectedAsset,
    const FGuid& ExpectedActorId,
    const FString& ExpectedSource,
    const FString& ExpectedId,
    const FString& ExpectedType = UPCGComponent::StaticClass()->GetPathName())
{
    const TSharedPtr<FJsonObject>* Binding = nullptr;
    const TSharedPtr<FJsonObject>* Target = nullptr;
    FString Kind;
    FString Domain;
    FString Asset;
    FString ActorId;
    FString Source;
    FString Id;
    FString Type;
    return Result.IsValid()
        && Result->TryGetObjectField(TEXT("target"), Binding)
        && Binding != nullptr
        && (*Binding).IsValid()
        && (*Binding)->TryGetObjectField(TEXT("target"), Target)
        && Target != nullptr
        && (*Target).IsValid()
        && (*Target)->TryGetStringField(TEXT("kind"), Kind)
        && Kind == TEXT("target")
        && (*Target)->TryGetStringField(TEXT("domain"), Domain)
        && Domain == TEXT("pcg_component")
        && (*Target)->TryGetStringField(TEXT("asset"), Asset)
        && Asset == ExpectedAsset
        && (*Target)->TryGetStringField(TEXT("actorId"), ActorId)
        && ActorId == LevelGuidText(ExpectedActorId)
        && (*Target)->TryGetStringField(TEXT("source"), Source)
        && Source == ExpectedSource
        && (*Target)->TryGetStringField(TEXT("id"), Id)
        && Id == ExpectedId
        && (*Target)->TryGetStringField(TEXT("type"), Type)
        && Type == ExpectedType;
}

bool TryReadLevelObjectFields(
    const TSharedPtr<FJsonValue>& Value,
    const TSharedPtr<FJsonObject>*& OutFields,
    FString* OutSemanticTag = nullptr)
{
    OutFields = nullptr;
    const TSharedPtr<FJsonObject>* Object = nullptr;
    FString Kind;
    if (!Value.IsValid()
        || !Value->TryGetObject(Object)
        || Object == nullptr
        || !(*Object).IsValid()
        || !(*Object)->TryGetStringField(TEXT("kind"), Kind)
        || Kind != TEXT("object")
        || !(*Object)->TryGetObjectField(TEXT("fields"), OutFields)
        || OutFields == nullptr)
    {
        return false;
    }
    if (OutSemanticTag != nullptr)
    {
        OutSemanticTag->Reset();
        (*Object)->TryGetStringField(TEXT("semanticTag"), *OutSemanticTag);
    }
    return true;
}

TArray<TSharedPtr<FJsonObject>> LevelTaggedObjectFields(
    const TSharedPtr<FJsonObject>& Result,
    const FString& SemanticTag)
{
    TArray<TSharedPtr<FJsonObject>> Matches;
    const TSharedPtr<FJsonObject>* Object = nullptr;
    const TArray<TSharedPtr<FJsonValue>>* Statements = nullptr;
    if (!Result.IsValid()
        || !Result->TryGetObjectField(TEXT("object"), Object)
        || Object == nullptr
        || !(*Object).IsValid()
        || !(*Object)->TryGetArrayField(TEXT("statements"), Statements)
        || Statements == nullptr)
    {
        return Matches;
    }
    for (const TSharedPtr<FJsonValue>& StatementValue : *Statements)
    {
        const TSharedPtr<FJsonObject>* Statement = nullptr;
        const TSharedPtr<FJsonObject>* Fields = nullptr;
        FString ActualTag;
        if (StatementValue.IsValid()
            && StatementValue->TryGetObject(Statement)
            && Statement != nullptr
            && (*Statement).IsValid()
            && TryReadLevelObjectFields(
                (*Statement)->TryGetField(TEXT("value")),
                Fields,
                &ActualTag)
            && Fields != nullptr
            && ActualTag == SemanticTag)
        {
            Matches.Add(*Fields);
        }
    }
    return Matches;
}

bool LevelHasCommentContaining(
    const TSharedPtr<FJsonObject>& Result,
    const FString& Fragment)
{
    const TSharedPtr<FJsonObject>* Object = nullptr;
    const TArray<TSharedPtr<FJsonValue>>* Statements = nullptr;
    if (!Result.IsValid()
        || !Result->TryGetObjectField(TEXT("object"), Object)
        || Object == nullptr
        || !(*Object).IsValid()
        || !(*Object)->TryGetArrayField(TEXT("statements"), Statements)
        || Statements == nullptr)
    {
        return false;
    }
    for (const TSharedPtr<FJsonValue>& StatementValue : *Statements)
    {
        const TSharedPtr<FJsonObject>* Statement = nullptr;
        FString Kind;
        FString Text;
        if (StatementValue.IsValid()
            && StatementValue->TryGetObject(Statement)
            && Statement != nullptr
            && (*Statement).IsValid()
            && (*Statement)->TryGetStringField(TEXT("kind"), Kind)
            && Kind == TEXT("comment")
            && (*Statement)->TryGetStringField(TEXT("text"), Text)
            && Text.Contains(Fragment))
        {
            return true;
        }
    }
    return false;
}

TSharedPtr<FJsonObject> FindLevelFieldsById(
    const TArray<TSharedPtr<FJsonObject>>& Objects,
    const FString& ExpectedId)
{
    for (const TSharedPtr<FJsonObject>& Fields : Objects)
    {
        FString Id;
        if (Fields.IsValid()
            && Fields->TryGetStringField(TEXT("id"), Id)
            && Id == ExpectedId)
        {
            return Fields;
        }
    }
    return nullptr;
}

int32 CountLevelFieldsById(
    const TArray<TSharedPtr<FJsonObject>>& Objects,
    const FString& ExpectedId)
{
    int32 Count = 0;
    for (const TSharedPtr<FJsonObject>& Fields : Objects)
    {
        FString Id;
        if (Fields.IsValid()
            && Fields->TryGetStringField(TEXT("id"), Id)
            && Id == ExpectedId)
        {
            ++Count;
        }
    }
    return Count;
}

TSharedPtr<FJsonObject> FirstLevelAssetFields(
    const TSharedPtr<FJsonObject>& Result)
{
    const TSharedPtr<FJsonObject>* Object = nullptr;
    const TArray<TSharedPtr<FJsonValue>>* Statements = nullptr;
    if (!Result.IsValid()
        || !Result->TryGetObjectField(TEXT("object"), Object)
        || Object == nullptr
        || !(*Object).IsValid()
        || !(*Object)->TryGetArrayField(TEXT("statements"), Statements)
        || Statements == nullptr)
    {
        return nullptr;
    }

    TSharedPtr<FJsonObject> Fields = MakeShared<FJsonObject>();
    for (const TSharedPtr<FJsonValue>& StatementValue : *Statements)
    {
        const TSharedPtr<FJsonObject>* Statement = nullptr;
        const TSharedPtr<FJsonObject>* Target = nullptr;
        const TSharedPtr<FJsonObject>* Owner = nullptr;
        const TArray<TSharedPtr<FJsonValue>>* Path = nullptr;
        FString TargetKind;
        FString OwnerKind;
        FString OwnerName;
        FString FieldName;
        if (StatementValue.IsValid()
            && StatementValue->TryGetObject(Statement)
            && Statement != nullptr
            && (*Statement).IsValid()
            && (*Statement)->TryGetObjectField(TEXT("target"), Target)
            && Target != nullptr
            && (*Target).IsValid()
            && (*Target)->TryGetStringField(TEXT("kind"), TargetKind)
            && TargetKind == TEXT("member")
            && (*Target)->TryGetObjectField(TEXT("object"), Owner)
            && Owner != nullptr
            && (*Owner).IsValid()
            && (*Owner)->TryGetStringField(TEXT("kind"), OwnerKind)
            && OwnerKind == TEXT("local")
            && (*Owner)->TryGetStringField(TEXT("name"), OwnerName)
            && OwnerName == TEXT("level_scope")
            && (*Target)->TryGetArrayField(TEXT("path"), Path)
            && Path != nullptr
            && Path->Num() == 1
            && (*Path)[0].IsValid()
            && (*Path)[0]->TryGetString(FieldName)
            && !FieldName.IsEmpty())
        {
            Fields->SetField(
                FieldName,
                (*Statement)->TryGetField(TEXT("value")));
        }
    }
    return Fields->Values.IsEmpty() ? nullptr : Fields;
}

TSharedPtr<FJsonObject> CollectLocalMemberFields(
    const TSharedPtr<FJsonObject>& Result,
    const FString& ExpectedAlias)
{
    const TSharedPtr<FJsonObject>* Object = nullptr;
    const TArray<TSharedPtr<FJsonValue>>* Statements = nullptr;
    if (!Result.IsValid()
        || !Result->TryGetObjectField(TEXT("object"), Object)
        || Object == nullptr
        || !(*Object).IsValid()
        || !(*Object)->TryGetArrayField(TEXT("statements"), Statements)
        || Statements == nullptr)
    {
        return nullptr;
    }

    TSharedPtr<FJsonObject> Fields = MakeShared<FJsonObject>();
    for (const TSharedPtr<FJsonValue>& StatementValue : *Statements)
    {
        const TSharedPtr<FJsonObject>* Statement = nullptr;
        const TSharedPtr<FJsonObject>* Target = nullptr;
        const TSharedPtr<FJsonObject>* Owner = nullptr;
        const TArray<TSharedPtr<FJsonValue>>* Path = nullptr;
        FString TargetKind;
        FString OwnerKind;
        FString OwnerName;
        FString FieldName;
        if (StatementValue.IsValid()
            && StatementValue->TryGetObject(Statement)
            && Statement != nullptr
            && (*Statement).IsValid()
            && (*Statement)->TryGetObjectField(TEXT("target"), Target)
            && Target != nullptr
            && (*Target).IsValid()
            && (*Target)->TryGetStringField(TEXT("kind"), TargetKind)
            && TargetKind == TEXT("member")
            && (*Target)->TryGetObjectField(TEXT("object"), Owner)
            && Owner != nullptr
            && (*Owner).IsValid()
            && (*Owner)->TryGetStringField(TEXT("kind"), OwnerKind)
            && OwnerKind == TEXT("local")
            && (*Owner)->TryGetStringField(TEXT("name"), OwnerName)
            && OwnerName == ExpectedAlias
            && (*Target)->TryGetArrayField(TEXT("path"), Path)
            && Path != nullptr
            && Path->Num() == 1
            && (*Path)[0].IsValid()
            && (*Path)[0]->TryGetString(FieldName)
            && !FieldName.IsEmpty())
        {
            Fields->SetField(
                FieldName,
                (*Statement)->TryGetField(TEXT("value")));
        }
    }
    return Fields->Values.IsEmpty() ? nullptr : Fields;
}

bool ReadLevelNextCursor(
    const TSharedPtr<FJsonObject>& Result,
    FString& OutCursor)
{
    OutCursor.Reset();
    const TSharedPtr<FJsonObject>* Page = nullptr;
    return Result.IsValid()
        && Result->TryGetObjectField(TEXT("page"), Page)
        && Page != nullptr
        && (*Page).IsValid()
        && (*Page)->TryGetStringField(TEXT("next"), OutCursor)
        && !OutCursor.IsEmpty();
}

bool ReadLevelLocalField(
    const TSharedPtr<FJsonObject>& Fields,
    const FString& FieldName,
    FString& OutAlias)
{
    OutAlias.Reset();
    const TSharedPtr<FJsonObject>* Ref = nullptr;
    FString Kind;
    return Fields.IsValid()
        && Fields->TryGetObjectField(FieldName, Ref)
        && Ref != nullptr
        && (*Ref).IsValid()
        && (*Ref)->TryGetStringField(TEXT("kind"), Kind)
        && Kind == TEXT("local")
        && (*Ref)->TryGetStringField(TEXT("name"), OutAlias)
        && !OutAlias.IsEmpty();
}

bool ReadNestedObjectField(
    const TSharedPtr<FJsonObject>& Fields,
    const FString& FieldName,
    TSharedPtr<FJsonObject>& OutFields)
{
    OutFields.Reset();
    const TSharedPtr<FJsonObject>* NestedFields = nullptr;
    return Fields.IsValid()
        && TryReadLevelObjectFields(
            Fields->TryGetField(FieldName),
            NestedFields)
        && NestedFields != nullptr
        && (OutFields = *NestedFields).IsValid();
}

bool LevelFieldIsNull(
    const TSharedPtr<FJsonObject>& Fields,
    const FString& FieldName)
{
    const TSharedPtr<FJsonValue> Value = Fields.IsValid()
        ? Fields->TryGetField(FieldName)
        : nullptr;
    return Value.IsValid() && Value->IsNull();
}

bool ReadLevelNameField(
    const TSharedPtr<FJsonObject>& Fields,
    const FString& FieldName,
    FString& OutName)
{
    OutName.Reset();
    const TSharedPtr<FJsonObject>* Value = nullptr;
    FString Kind;
    return Fields.IsValid()
        && Fields->TryGetObjectField(FieldName, Value)
        && Value != nullptr
        && (*Value).IsValid()
        && (*Value)->TryGetStringField(TEXT("kind"), Kind)
        && Kind == TEXT("name")
        && (*Value)->TryGetStringField(TEXT("name"), OutName)
        && !OutName.IsEmpty();
}

bool ReadLevelNameArrayField(
    const TSharedPtr<FJsonObject>& Fields,
    const FString& FieldName,
    TArray<FString>& OutNames)
{
    OutNames.Reset();
    const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
    if (!Fields.IsValid()
        || !Fields->TryGetArrayField(FieldName, Values)
        || Values == nullptr)
    {
        return false;
    }
    for (const TSharedPtr<FJsonValue>& Value : *Values)
    {
        const TSharedPtr<FJsonObject>* NameObject = nullptr;
        FString Kind;
        FString Name;
        if (!Value.IsValid()
            || !Value->TryGetObject(NameObject)
            || NameObject == nullptr
            || !(*NameObject).IsValid()
            || !(*NameObject)->TryGetStringField(TEXT("kind"), Kind)
            || Kind != TEXT("name")
            || !(*NameObject)->TryGetStringField(TEXT("name"), Name)
            || Name.IsEmpty())
        {
            OutNames.Reset();
            return false;
        }
        OutNames.Add(MoveTemp(Name));
    }
    return true;
}

bool ReadLevelStableRefField(
    const TSharedPtr<FJsonObject>& Fields,
    const FString& FieldName,
    const FString& ExpectedSemanticTag,
    TArray<FString>& OutIdentityPath)
{
    OutIdentityPath.Reset();
    const TSharedPtr<FJsonObject>* Ref = nullptr;
    const TArray<TSharedPtr<FJsonValue>>* Segments = nullptr;
    FString Kind;
    FString SemanticTag;
    if (!Fields.IsValid()
        || !Fields->TryGetObjectField(FieldName, Ref)
        || Ref == nullptr
        || !(*Ref).IsValid()
        || !(*Ref)->TryGetStringField(TEXT("kind"), Kind)
        || Kind != TEXT("stable_ref")
        || !(*Ref)->TryGetStringField(
            TEXT("semanticTag"),
            SemanticTag)
        || SemanticTag != ExpectedSemanticTag
        || !(*Ref)->TryGetArrayField(
            TEXT("identityPath"),
            Segments)
        || Segments == nullptr
        || Segments->IsEmpty())
    {
        return false;
    }
    for (const TSharedPtr<FJsonValue>& SegmentValue : *Segments)
    {
        FString Segment;
        if (!SegmentValue.IsValid()
            || !SegmentValue->TryGetString(Segment)
            || Segment.IsEmpty())
        {
            OutIdentityPath.Reset();
            return false;
        }
        OutIdentityPath.Add(Segment);
    }
    return true;
}

TSharedPtr<FJsonObject> FindLevelComponentFieldsByIdentity(
    const TArray<TSharedPtr<FJsonObject>>& Objects,
    const FString& ActorId,
    const FString& Source,
    const FString& Id)
{
    const TArray<FString> Expected = {ActorId, Source, Id};
    for (const TSharedPtr<FJsonObject>& Fields : Objects)
    {
        TArray<FString> Identity;
        if (ReadLevelStableRefField(
                Fields,
                TEXT("ref"),
                TEXT("component"),
                Identity)
            && Identity == Expected)
        {
            return Fields;
        }
    }
    return nullptr;
}

int32 CountLevelComponentFieldsByIdentity(
    const TArray<TSharedPtr<FJsonObject>>& Objects,
    const FString& ActorId,
    const FString& Source,
    const FString& Id)
{
    const TArray<FString> Expected = {ActorId, Source, Id};
    int32 Count = 0;
    for (const TSharedPtr<FJsonObject>& Fields : Objects)
    {
        TArray<FString> Identity;
        if (ReadLevelStableRefField(
                Fields,
                TEXT("ref"),
                TEXT("component"),
                Identity)
            && Identity == Expected)
        {
            ++Count;
        }
    }
    return Count;
}

bool ReadSingleRelatedLevelTarget(
    const TSharedPtr<FJsonObject>& Result,
    FString& OutAlias,
    FString& OutAsset)
{
    OutAlias.Reset();
    OutAsset.Reset();
    const TArray<TSharedPtr<FJsonValue>>* Related = nullptr;
    if (!Result.IsValid()
        || !Result->TryGetArrayField(TEXT("relatedTargets"), Related)
        || Related == nullptr)
    {
        return false;
    }
    int32 Matches = 0;
    for (const TSharedPtr<FJsonValue>& Value : *Related)
    {
        const TSharedPtr<FJsonObject>* Binding = nullptr;
        const TSharedPtr<FJsonObject>* Target = nullptr;
        FString Kind;
        FString Domain;
        FString Alias;
        FString Asset;
        FString Type;
        if (!Value.IsValid()
            || !Value->TryGetObject(Binding)
            || Binding == nullptr
            || !(*Binding).IsValid()
            || !(*Binding)->TryGetObjectField(TEXT("target"), Target)
            || Target == nullptr
            || !(*Target).IsValid()
            || !(*Target)->TryGetStringField(TEXT("domain"), Domain))
        {
            return false;
        }
        if (Domain != TEXT("level"))
        {
            continue;
        }
        if (!(*Binding)->TryGetStringField(TEXT("alias"), Alias)
            || Alias.IsEmpty()
            || !(*Target)->TryGetStringField(TEXT("kind"), Kind)
            || Kind != TEXT("target")
            || !(*Target)->TryGetStringField(TEXT("asset"), Asset)
            || Asset.IsEmpty()
            || !(*Target)->TryGetStringField(TEXT("type"), Type)
            || Type != UWorld::StaticClass()->GetPathName())
        {
            return false;
        }
        ++Matches;
        OutAlias = MoveTemp(Alias);
        OutAsset = MoveTemp(Asset);
    }
    return Matches == 1;
}

bool ReadRelatedLevelTargets(
    const TSharedPtr<FJsonObject>& Result,
    TMap<FString, FString>& OutAliasesByAsset)
{
    OutAliasesByAsset.Reset();
    const TArray<TSharedPtr<FJsonValue>>* Related = nullptr;
    if (!Result.IsValid()
        || !Result->TryGetArrayField(TEXT("relatedTargets"), Related)
        || Related == nullptr
        || Related->IsEmpty())
    {
        return false;
    }
    for (const TSharedPtr<FJsonValue>& Value : *Related)
    {
        const TSharedPtr<FJsonObject>* Binding = nullptr;
        const TSharedPtr<FJsonObject>* Target = nullptr;
        FString Alias;
        FString Asset;
        FString Kind;
        FString Domain;
        FString Type;
        if (!Value.IsValid()
            || !Value->TryGetObject(Binding)
            || Binding == nullptr
            || !(*Binding).IsValid()
            || !(*Binding)->TryGetStringField(TEXT("alias"), Alias)
            || Alias.IsEmpty()
            || !(*Binding)->TryGetObjectField(TEXT("target"), Target)
            || Target == nullptr
            || !(*Target).IsValid()
            || !(*Target)->TryGetStringField(TEXT("kind"), Kind)
            || Kind != TEXT("target")
            || !(*Target)->TryGetStringField(TEXT("domain"), Domain))
        {
            OutAliasesByAsset.Reset();
            return false;
        }
        if (Domain != TEXT("level"))
        {
            continue;
        }
        if (!(*Target)->TryGetStringField(TEXT("asset"), Asset)
            || Asset.IsEmpty()
            || !(*Target)->TryGetStringField(TEXT("type"), Type)
            || Type != UWorld::StaticClass()->GetPathName()
            || OutAliasesByAsset.Contains(Asset)
            || OutAliasesByAsset.FindKey(Alias) != nullptr)
        {
            OutAliasesByAsset.Reset();
            return false;
        }
        OutAliasesByAsset.Add(Asset, Alias);
    }
    return true;
}

FString PCGComponentIdentityKey(
    const FString& ActorId,
    const FString& Source,
    const FString& Id)
{
    return FString::Printf(
        TEXT("%d:%s|%d:%s|%d:%s"),
        ActorId.Len(),
        *ActorId,
        Source.Len(),
        *Source,
        Id.Len(),
        *Id);
}

bool ReadRelatedPCGComponentTargets(
    const TSharedPtr<FJsonObject>& Result,
    const FString& ExpectedAsset,
    TMap<FString, FString>& OutAliasesByIdentity)
{
    OutAliasesByIdentity.Reset();
    const TArray<TSharedPtr<FJsonValue>>* Related = nullptr;
    if (!Result.IsValid()
        || !Result->TryGetArrayField(TEXT("relatedTargets"), Related)
        || Related == nullptr
        || Related->IsEmpty())
    {
        return false;
    }
    for (const TSharedPtr<FJsonValue>& Value : *Related)
    {
        const TSharedPtr<FJsonObject>* Binding = nullptr;
        const TSharedPtr<FJsonObject>* Target = nullptr;
        FString Alias;
        FString Kind;
        FString Domain;
        FString Asset;
        FString ActorId;
        FString Source;
        FString Id;
        FString Type;
        if (!Value.IsValid()
            || !Value->TryGetObject(Binding)
            || Binding == nullptr
            || !(*Binding).IsValid()
            || !(*Binding)->TryGetStringField(TEXT("alias"), Alias)
            || Alias.IsEmpty()
            || !(*Binding)->TryGetObjectField(TEXT("target"), Target)
            || Target == nullptr
            || !(*Target).IsValid()
            || !(*Target)->TryGetStringField(TEXT("kind"), Kind)
            || Kind != TEXT("target")
            || !(*Target)->TryGetStringField(TEXT("domain"), Domain))
        {
            OutAliasesByIdentity.Reset();
            return false;
        }
        if (Domain != TEXT("pcg_component"))
        {
            continue;
        }
        if (!(*Target)->TryGetStringField(TEXT("asset"), Asset)
            || Asset != ExpectedAsset
            || !(*Target)->TryGetStringField(TEXT("actorId"), ActorId)
            || ActorId.IsEmpty()
            || !(*Target)->TryGetStringField(TEXT("source"), Source)
            || (Source != TEXT("native")
                && Source != TEXT("scs")
                && Source != TEXT("instance"))
            || !(*Target)->TryGetStringField(TEXT("id"), Id)
            || Id.IsEmpty()
            || !(*Target)->TryGetStringField(TEXT("type"), Type)
            || Type != UPCGComponent::StaticClass()->GetPathName())
        {
            OutAliasesByIdentity.Reset();
            return false;
        }
        const FString Key = PCGComponentIdentityKey(
            ActorId,
            Source,
            Id);
        if (OutAliasesByIdentity.Contains(Key)
            || OutAliasesByIdentity.FindKey(Alias) != nullptr)
        {
            OutAliasesByIdentity.Reset();
            return false;
        }
        OutAliasesByIdentity.Add(Key, Alias);
    }
    return true;
}

bool HasSingleLevelHandoff(
    const TSharedPtr<FJsonObject>& Result,
    const FString& ExpectedPurpose,
    const FString& ExpectedAlias)
{
    const TArray<TSharedPtr<FJsonValue>>* Handoffs = nullptr;
    if (!Result.IsValid()
        || !Result->TryGetArrayField(TEXT("handoffs"), Handoffs)
        || Handoffs == nullptr)
    {
        return false;
    }
    int32 Matches = 0;
    for (const TSharedPtr<FJsonValue>& Value : *Handoffs)
    {
        const TSharedPtr<FJsonObject>* Handoff = nullptr;
        const TSharedPtr<FJsonObject>* Target = nullptr;
        FString Kind;
        FString Purpose;
        FString RefKind;
        FString Alias;
        if (!Value.IsValid()
            || !Value->TryGetObject(Handoff)
            || Handoff == nullptr
            || !(*Handoff).IsValid()
            || !(*Handoff)->TryGetStringField(TEXT("kind"), Kind)
            || Kind != TEXT("target_handoff")
            || !(*Handoff)->TryGetStringField(TEXT("purpose"), Purpose))
        {
            return false;
        }
        if (Purpose != ExpectedPurpose)
        {
            continue;
        }
        if (!(*Handoff)->TryGetObjectField(TEXT("target"), Target)
            || Target == nullptr
            || !(*Target).IsValid()
            || !(*Target)->TryGetStringField(TEXT("kind"), RefKind)
            || RefKind != TEXT("local")
            || !(*Target)->TryGetStringField(TEXT("name"), Alias)
            || Alias != ExpectedAlias)
        {
            return false;
        }
        ++Matches;
    }
    return Matches == 1;
}

bool ReadUniqueHandoffAlias(
    const TSharedPtr<FJsonObject>& Result,
    const FString& ExpectedPurpose,
    FString& OutAlias)
{
    OutAlias.Reset();
    const TArray<TSharedPtr<FJsonValue>>* Handoffs = nullptr;
    if (!Result.IsValid()
        || !Result->TryGetArrayField(TEXT("handoffs"), Handoffs)
        || Handoffs == nullptr)
    {
        return false;
    }
    int32 Matches = 0;
    for (const TSharedPtr<FJsonValue>& Value : *Handoffs)
    {
        const TSharedPtr<FJsonObject>* Handoff = nullptr;
        const TSharedPtr<FJsonObject>* Target = nullptr;
        FString Kind;
        FString Purpose;
        FString RefKind;
        FString Alias;
        if (!Value.IsValid()
            || !Value->TryGetObject(Handoff)
            || Handoff == nullptr
            || !(*Handoff).IsValid()
            || !(*Handoff)->TryGetStringField(TEXT("kind"), Kind)
            || Kind != TEXT("target_handoff")
            || !(*Handoff)->TryGetStringField(TEXT("purpose"), Purpose)
            || !(*Handoff)->TryGetObjectField(TEXT("target"), Target)
            || Target == nullptr
            || !(*Target).IsValid()
            || !(*Target)->TryGetStringField(TEXT("kind"), RefKind)
            || RefKind != TEXT("local")
            || !(*Target)->TryGetStringField(TEXT("name"), Alias)
            || Alias.IsEmpty())
        {
            return false;
        }
        if (Purpose == ExpectedPurpose)
        {
            ++Matches;
            OutAlias = Alias;
        }
    }
    return Matches == 1;
}

int32 CountHandoffPurpose(
    const TSharedPtr<FJsonObject>& Result,
    const FString& ExpectedPurpose)
{
    const TArray<TSharedPtr<FJsonValue>>* Handoffs = nullptr;
    if (!Result.IsValid()
        || !Result->TryGetArrayField(TEXT("handoffs"), Handoffs)
        || Handoffs == nullptr)
    {
        return 0;
    }
    int32 Matches = 0;
    for (const TSharedPtr<FJsonValue>& Value : *Handoffs)
    {
        const TSharedPtr<FJsonObject>* Handoff = nullptr;
        FString Purpose;
        if (Value.IsValid()
            && Value->TryGetObject(Handoff)
            && Handoff != nullptr
            && (*Handoff).IsValid()
            && (*Handoff)->TryGetStringField(TEXT("purpose"), Purpose)
            && Purpose == ExpectedPurpose)
        {
            ++Matches;
        }
    }
    return Matches;
}

int32 CountRelatedTargetDomain(
    const TSharedPtr<FJsonObject>& Result,
    const FString& ExpectedDomain)
{
    const TArray<TSharedPtr<FJsonValue>>* Related = nullptr;
    if (!Result.IsValid()
        || !Result->TryGetArrayField(TEXT("relatedTargets"), Related)
        || Related == nullptr)
    {
        return 0;
    }
    int32 Matches = 0;
    for (const TSharedPtr<FJsonValue>& Value : *Related)
    {
        const TSharedPtr<FJsonObject>* Binding = nullptr;
        const TSharedPtr<FJsonObject>* Target = nullptr;
        FString Domain;
        if (Value.IsValid()
            && Value->TryGetObject(Binding)
            && Binding != nullptr
            && (*Binding).IsValid()
            && (*Binding)->TryGetObjectField(TEXT("target"), Target)
            && Target != nullptr
            && (*Target).IsValid()
            && (*Target)->TryGetStringField(TEXT("domain"), Domain)
            && Domain == ExpectedDomain)
        {
            ++Matches;
        }
    }
    return Matches;
}

TSharedPtr<FJsonObject> ReadUniqueHandoffTarget(
    const TSharedPtr<FJsonObject>& Result,
    const FString& ExpectedPurpose)
{
    FString Alias;
    if (!ReadUniqueHandoffAlias(
            Result,
            ExpectedPurpose,
            Alias))
    {
        return nullptr;
    }

    const TArray<TSharedPtr<FJsonValue>>* Related = nullptr;
    if (!Result->TryGetArrayField(TEXT("relatedTargets"), Related)
        || Related == nullptr)
    {
        return nullptr;
    }
    TSharedPtr<FJsonObject> Match;
    int32 Matches = 0;
    for (const TSharedPtr<FJsonValue>& Value : *Related)
    {
        const TSharedPtr<FJsonObject>* Binding = nullptr;
        const TSharedPtr<FJsonObject>* Target = nullptr;
        FString CandidateAlias;
        if (Value.IsValid()
            && Value->TryGetObject(Binding)
            && Binding != nullptr
            && (*Binding).IsValid()
            && (*Binding)->TryGetStringField(
                TEXT("alias"),
                CandidateAlias)
            && CandidateAlias == Alias
            && (*Binding)->TryGetObjectField(
                TEXT("target"),
                Target)
            && Target != nullptr
            && (*Target).IsValid())
        {
            Match = *Target;
            ++Matches;
        }
    }
    return Matches == 1 ? Match : nullptr;
}

bool IsCanonicalRelatedPathTarget(
    const TSharedPtr<FJsonObject>& Target,
    const FString& ExpectedDomain,
    const FString& PathField,
    const FString& ExpectedPath,
    const FString& ExpectedType = FString())
{
    FString Kind;
    FString Domain;
    FString Path;
    FString Type;
    return Target.IsValid()
        && Target->TryGetStringField(TEXT("kind"), Kind)
        && Kind == TEXT("target")
        && Target->TryGetStringField(TEXT("domain"), Domain)
        && Domain == ExpectedDomain
        && Target->TryGetStringField(PathField, Path)
        && Path == ExpectedPath
        && (ExpectedType.IsEmpty()
            || (Target->TryGetStringField(TEXT("type"), Type)
                && Type == ExpectedType));
}

bool IsCanonicalBlueprintTarget(
    const TSharedPtr<FJsonObject>& Target,
    const FString& ExpectedAsset,
    const FGuid& ExpectedId)
{
    FString Id;
    return ExpectedId.IsValid()
        && IsCanonicalRelatedPathTarget(
            Target,
            TEXT("blueprint"),
            TEXT("asset"),
            ExpectedAsset)
        && Target->TryGetStringField(TEXT("id"), Id)
        && Id == LevelGuidText(ExpectedId);
}

bool HasSingleLocalStableRef(
    const TSharedPtr<FJsonObject>& Result,
    const FString& ExpectedSemanticTag,
    const FString& ExpectedId)
{
    const TSharedPtr<FJsonObject>* Object = nullptr;
    const TArray<TSharedPtr<FJsonValue>>* Statements = nullptr;
    if (!Result.IsValid()
        || !Result->TryGetObjectField(TEXT("object"), Object)
        || Object == nullptr
        || !(*Object).IsValid()
        || !(*Object)->TryGetArrayField(
            TEXT("statements"),
            Statements)
        || Statements == nullptr)
    {
        return false;
    }

    int32 Matches = 0;
    for (const TSharedPtr<FJsonValue>& StatementValue : *Statements)
    {
        const TSharedPtr<FJsonObject>* Statement = nullptr;
        const TSharedPtr<FJsonObject>* Value = nullptr;
        const TArray<TSharedPtr<FJsonValue>>* IdentityPath = nullptr;
        FString Kind;
        FString SemanticTag;
        FString Id;
        if (StatementValue.IsValid()
            && StatementValue->TryGetObject(Statement)
            && Statement != nullptr
            && (*Statement).IsValid()
            && (*Statement)->TryGetObjectField(TEXT("value"), Value)
            && Value != nullptr
            && (*Value).IsValid()
            && (*Value)->TryGetStringField(TEXT("kind"), Kind)
            && Kind == TEXT("stable_ref")
            && (*Value)->TryGetStringField(
                TEXT("semanticTag"),
                SemanticTag)
            && SemanticTag == ExpectedSemanticTag
            && (*Value)->TryGetArrayField(
                TEXT("identityPath"),
                IdentityPath)
            && IdentityPath != nullptr
            && IdentityPath->Num() == 1
            && (*IdentityPath)[0].IsValid()
            && (*IdentityPath)[0]->TryGetString(Id)
            && Id == ExpectedId)
        {
            ++Matches;
        }
    }
    return Matches == 1;
}

bool HasRelatedTarget(
    const TSharedPtr<FJsonObject>& Result,
    const FString& ExpectedAlias,
    const FString& ExpectedDomain,
    const FString& ExpectedAsset,
    const FString& ExpectedType)
{
    const TArray<TSharedPtr<FJsonValue>>* Related = nullptr;
    if (!Result.IsValid()
        || !Result->TryGetArrayField(TEXT("relatedTargets"), Related)
        || Related == nullptr)
    {
        return false;
    }
    int32 Matches = 0;
    for (const TSharedPtr<FJsonValue>& Value : *Related)
    {
        const TSharedPtr<FJsonObject>* Binding = nullptr;
        const TSharedPtr<FJsonObject>* Target = nullptr;
        FString Alias;
        FString Kind;
        FString Domain;
        FString Asset;
        FString Type;
        if (Value.IsValid()
            && Value->TryGetObject(Binding)
            && Binding != nullptr
            && (*Binding).IsValid()
            && (*Binding)->TryGetStringField(TEXT("alias"), Alias)
            && Alias == ExpectedAlias
            && (*Binding)->TryGetObjectField(TEXT("target"), Target)
            && Target != nullptr
            && (*Target).IsValid()
            && (*Target)->TryGetStringField(TEXT("kind"), Kind)
            && Kind == TEXT("target")
            && (*Target)->TryGetStringField(TEXT("domain"), Domain)
            && Domain == ExpectedDomain
            && (*Target)->TryGetStringField(TEXT("asset"), Asset)
            && Asset == ExpectedAsset
            && (*Target)->TryGetStringField(TEXT("type"), Type)
            && Type == ExpectedType)
        {
            ++Matches;
        }
    }
    return Matches == 1;
}

bool ReadLevelHandoffAliases(
    const TSharedPtr<FJsonObject>& Result,
    const FString& ExpectedPurpose,
    TSet<FString>& OutAliases)
{
    OutAliases.Reset();
    const TArray<TSharedPtr<FJsonValue>>* Handoffs = nullptr;
    if (!Result.IsValid()
        || !Result->TryGetArrayField(TEXT("handoffs"), Handoffs)
        || Handoffs == nullptr
        || Handoffs->IsEmpty())
    {
        return false;
    }
    for (const TSharedPtr<FJsonValue>& Value : *Handoffs)
    {
        const TSharedPtr<FJsonObject>* Handoff = nullptr;
        const TSharedPtr<FJsonObject>* Target = nullptr;
        FString Kind;
        FString Purpose;
        FString RefKind;
        FString Alias;
        if (!Value.IsValid()
            || !Value->TryGetObject(Handoff)
            || Handoff == nullptr
            || !(*Handoff).IsValid()
            || !(*Handoff)->TryGetStringField(TEXT("kind"), Kind)
            || Kind != TEXT("target_handoff")
            || !(*Handoff)->TryGetStringField(TEXT("purpose"), Purpose))
        {
            OutAliases.Reset();
            return false;
        }
        if (Purpose != ExpectedPurpose)
        {
            continue;
        }
        if (!(*Handoff)->TryGetObjectField(TEXT("target"), Target)
            || Target == nullptr
            || !(*Target).IsValid()
            || !(*Target)->TryGetStringField(TEXT("kind"), RefKind)
            || RefKind != TEXT("local")
            || !(*Target)->TryGetStringField(TEXT("name"), Alias)
            || Alias.IsEmpty()
            || OutAliases.Contains(Alias))
        {
            OutAliases.Reset();
            return false;
        }
        OutAliases.Add(Alias);
    }
    return !OutAliases.IsEmpty();
}

bool HasNoLevelRelatedContext(
    const TSharedPtr<FJsonObject>& Result)
{
    if (!Result.IsValid())
    {
        return false;
    }
    const TArray<TSharedPtr<FJsonValue>>* Related = nullptr;
    if (Result->TryGetArrayField(TEXT("relatedTargets"), Related)
        && Related != nullptr)
    {
        for (const TSharedPtr<FJsonValue>& Value : *Related)
        {
            const TSharedPtr<FJsonObject>* Binding = nullptr;
            const TSharedPtr<FJsonObject>* Target = nullptr;
            FString Domain;
            if (!Value.IsValid()
                || !Value->TryGetObject(Binding)
                || Binding == nullptr
                || !(*Binding).IsValid()
                || !(*Binding)->TryGetObjectField(TEXT("target"), Target)
                || Target == nullptr
                || !(*Target).IsValid()
                || !(*Target)->TryGetStringField(TEXT("domain"), Domain))
            {
                return false;
            }
            if (Domain == TEXT("level")
                || Domain == TEXT("pcg_component"))
            {
                return false;
            }
        }
    }
    const TArray<TSharedPtr<FJsonValue>>* Handoffs = nullptr;
    if (Result->TryGetArrayField(TEXT("handoffs"), Handoffs)
        && Handoffs != nullptr)
    {
        for (const TSharedPtr<FJsonValue>& Value : *Handoffs)
        {
            const TSharedPtr<FJsonObject>* Handoff = nullptr;
            FString Purpose;
            if (!Value.IsValid()
                || !Value->TryGetObject(Handoff)
                || Handoff == nullptr
                || !(*Handoff).IsValid()
                || !(*Handoff)->TryGetStringField(TEXT("purpose"), Purpose))
            {
                return false;
            }
            if (Purpose == TEXT("inspect_source_level")
                || Purpose == TEXT("inspect_pcg_component"))
            {
                return false;
            }
        }
    }
    return true;
}

void PrepareLevelPackageForCollection(UPackage* Package)
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
        Loomle::Tests::IncludeNestedObjects);
}

bool SetFixtureGuidProperty(
    AActor* Actor,
    const FName PropertyName,
    const FGuid& Guid)
{
    FStructProperty* GuidProperty =
        FindFProperty<FStructProperty>(
            AActor::StaticClass(),
            PropertyName);
    if (Actor == nullptr
        || GuidProperty == nullptr
        || GuidProperty->Struct != TBaseStructure<FGuid>::Get())
    {
        return false;
    }
    *GuidProperty->ContainerPtrToValuePtr<FGuid>(Actor) = Guid;
    return true;
}

bool SetFixtureActorGuid(AActor* Actor, const FGuid& Guid)
{
    return SetFixtureGuidProperty(
            Actor,
            TEXT("ActorGuid"),
            Guid)
        && Actor->GetActorGuid() == Guid;
}

struct FLevelMapRecord
{
    FString PackageName;
    FString ObjectPath;
    FString Filename;
    UWorld* World = nullptr;
    bool bRegisteredInMemory = false;
};

class FScopedLevelQueryFixture
{
public:
    ~FScopedLevelQueryFixture()
    {
        FString Ignored;
        Cleanup(Ignored);
    }

    FScopedLevelQueryFixture(const FScopedLevelQueryFixture&) = delete;
    FScopedLevelQueryFixture& operator=(const FScopedLevelQueryFixture&) = delete;
    FScopedLevelQueryFixture() = default;

    bool Build(FString& OutError)
    {
        Token = FGuid::NewGuid().ToString(EGuidFormats::Digits);
        RootPackagePath = FString::Printf(
            TEXT("/Game/LoomleTests/Level/%s"),
            *Token);

        if (!CreateMap(TEXT("L_10_Loaded"), Loaded, OutError))
        {
            return false;
        }
        Alpha = SpawnActor(
            Loaded.World,
            TEXT("Actor_Alpha"),
            TEXT("Loomle Level Alpha"),
            FTransform(FVector(100.0, 20.0, 5.0)),
            OutError);
        Beta = SpawnActor(
            Loaded.World,
            TEXT("Actor_Beta"),
            TEXT("Loomle Level Beta"),
            FTransform(FVector(-25.0, 50.0, 10.0)),
            OutError);
        Gamma = SpawnActor(
            Loaded.World,
            TEXT("Actor_Gamma"),
            TEXT("Loomle Level Gamma"),
            FTransform(FVector(0.0, 0.0, 125.0)),
            OutError);
        InstanceProjection = SpawnActor(
            Loaded.World,
            TEXT("Actor_InstanceProjection"),
            TEXT("Loomle Instance Projection"),
            FTransform::Identity,
            OutError);
        ExternalContent = SpawnActor(
            Loaded.World,
            TEXT("Actor_ExternalContent"),
            TEXT("Loomle External Content"),
            FTransform::Identity,
            OutError);
        PcgGenerated = SpawnActor(
            Loaded.World,
            TEXT("Actor_PCGGenerated"),
            TEXT("Loomle PCG Generated"),
            FTransform::Identity,
            OutError);
        if (Alpha == nullptr
            || Beta == nullptr
            || Gamma == nullptr
            || InstanceProjection == nullptr
            || ExternalContent == nullptr
            || PcgGenerated == nullptr)
        {
            return false;
        }
        const FGuid InstanceGuid = FGuid::NewGuid();
        const FGuid ContentBundleGuid = FGuid::NewGuid();
        if (!SetFixtureGuidProperty(
            InstanceProjection,
            TEXT("ActorInstanceGuid"),
            InstanceGuid)
            || InstanceProjection->GetActorInstanceGuid() != InstanceGuid
            || !SetFixtureGuidProperty(
            ExternalContent,
            TEXT("ContentBundleGuid"),
            ContentBundleGuid)
            || !ExternalContent->HasExternalContent())
        {
            OutError = TEXT("The Level fixture could not author projection and external-content evidence.");
            return false;
        }
        PcgGenerated->Tags.AddUnique(
            PCGHelpers::DefaultPCGActorTag);
        AlphaId = Alpha->GetActorGuid();
        BetaId = Beta->GetActorGuid();
        GammaId = Gamma->GetActorGuid();
        InstanceProjectionId = InstanceProjection->GetActorGuid();
        ExternalContentId = ExternalContent->GetActorGuid();
        PcgGeneratedId = PcgGenerated->GetActorGuid();
        if (!AlphaId.IsValid()
            || !BetaId.IsValid()
            || !GammaId.IsValid()
            || !InstanceProjectionId.IsValid()
            || !ExternalContentId.IsValid()
            || !PcgGeneratedId.IsValid())
        {
            OutError = TEXT("UE did not assign persistent ActorGuids to the loaded fixture Actors.");
            return false;
        }
        if (!SaveMap(Loaded, OutError))
        {
            return false;
        }

        if (!CreateMap(TEXT("L_20_Unloaded"), Unloaded, OutError))
        {
            return false;
        }
        FActorSpawnParameters UnloadedPCGParams;
        UnloadedPCGParams.Name = FName(TEXT("PCG_UnloadedOwner"));
        UnloadedPCGParams.OverrideLevel = Unloaded.World->PersistentLevel;
        UnloadedPCGParams.ObjectFlags = RF_Transactional;
        APCGVolume* UnloadedPCGOwner =
            Unloaded.World->SpawnActor<APCGVolume>(
                APCGVolume::StaticClass(),
                FTransform::Identity,
                UnloadedPCGParams);
        UPCGComponent* UnloadedPCGComponent = UnloadedPCGOwner != nullptr
            ? UnloadedPCGOwner->PCGComponent.Get()
            : nullptr;
        UnloadedPCGActorId = UnloadedPCGOwner != nullptr
            ? UnloadedPCGOwner->GetActorGuid()
            : FGuid();
        UnloadedPCGId = UnloadedPCGComponent != nullptr
            ? UnloadedPCGComponent->GetFName().ToString()
            : FString();
        if (!UnloadedPCGActorId.IsValid()
            || UnloadedPCGId.IsEmpty()
            || UnloadedPCGComponent->CreationMethod
                != EComponentCreationMethod::Native
            || UnloadedPCGComponent->GetConstOriginalComponent()
                != UnloadedPCGComponent)
        {
            OutError = TEXT(
                "The unloaded Level fixture could not create a durable native PCG Component locator.");
            return false;
        }
        if (!SaveMap(Unloaded, OutError)
            || !UnloadMap(Unloaded, OutError))
        {
            return false;
        }

        if (!CreateMap(TEXT("L_30_Corrupt"), Corrupt, OutError))
        {
            return false;
        }
        CorruptStable = SpawnActor(
            Corrupt.World,
            TEXT("Actor_Stable"),
            TEXT("Loomle Stable Evidence"),
            FTransform::Identity,
            OutError);
        if (CorruptStable == nullptr || !SaveMap(Corrupt, OutError))
        {
            return false;
        }
        InvalidGuidActor = SpawnActor(
            Corrupt.World,
            TEXT("Actor_InvalidGuid"),
            TEXT("Loomle Invalid Guid"),
            FTransform::Identity,
            OutError);
        DuplicateFirst = SpawnActor(
            Corrupt.World,
            TEXT("Actor_DuplicateA"),
            TEXT("Loomle Duplicate A"),
            FTransform::Identity,
            OutError);
        DuplicateSecond = SpawnActor(
            Corrupt.World,
            TEXT("Actor_DuplicateB"),
            TEXT("Loomle Duplicate B"),
            FTransform::Identity,
            OutError);
        DuplicateId = DuplicateFirst != nullptr
            ? DuplicateFirst->GetActorGuid()
            : FGuid();
        if (InvalidGuidActor == nullptr
            || DuplicateFirst == nullptr
            || DuplicateSecond == nullptr
            || !DuplicateId.IsValid()
            || !SetFixtureActorGuid(InvalidGuidActor, FGuid())
            || !SetFixtureActorGuid(DuplicateSecond, DuplicateId))
        {
            OutError = TEXT("The corrupt Level fixture could not author invalid and duplicate ActorGuid evidence.");
            return false;
        }
        Corrupt.World->GetOutermost()->SetDirtyFlag(false);
        return ValidateOnDiskEvidence(Loaded, false, OutError)
            && ValidateOnDiskEvidence(Unloaded, true, OutError)
            && ValidateOnDiskEvidence(Corrupt, false, OutError);
    }

    bool Cleanup(FString& OutError)
    {
        OutError.Reset();
        if (bCleaned)
        {
            return true;
        }

        bCleaned = true;

        if (bEditorContextChanged && GEditor != nullptr)
        {
            GEditor->GetEditorWorldContext().SetCurrentWorld(
                OriginalEditorWorld);
            bEditorContextChanged = false;
        }

        IAssetRegistry& Registry =
            FModuleManager::LoadModuleChecked<FAssetRegistryModule>(
                TEXT("AssetRegistry"))
                .Get();
        for (FLevelMapRecord* Record : {&Loaded, &Unloaded, &Corrupt})
        {
            if (Record->World != nullptr)
            {
                if (Record->bRegisteredInMemory)
                {
                    FAssetRegistryModule::AssetDeleted(Record->World);
                    Record->bRegisteredInMemory = false;
                }
                UPackage* Package = Record->World->GetOutermost();
                Record->World->DestroyWorld(false);
                PrepareLevelPackageForCollection(Package);
                Record->World = nullptr;
            }
            else if (!Record->PackageName.IsEmpty())
            {
                if (UPackage* Package = FindPackage(
                        nullptr,
                        *Record->PackageName))
                {
                    PrepareLevelPackageForCollection(Package);
                }
            }
        }

        Alpha = nullptr;
        Beta = nullptr;
        Gamma = nullptr;
        InstanceProjection = nullptr;
        ExternalContent = nullptr;
        PcgGenerated = nullptr;
        CorruptStable = nullptr;
        InvalidGuidActor = nullptr;
        DuplicateFirst = nullptr;
        DuplicateSecond = nullptr;
        CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);

        TArray<FString> Filenames;
        for (const FLevelMapRecord* Record : {&Loaded, &Unloaded, &Corrupt})
        {
            if (!Record->Filename.IsEmpty())
            {
                IFileManager::Get().Delete(
                    *Record->Filename,
                    false,
                    true,
                    true);
                Filenames.Add(Record->Filename);
            }
            if (!Record->PackageName.IsEmpty()
                && FindPackage(nullptr, *Record->PackageName) != nullptr
                && OutError.IsEmpty())
            {
                OutError = TEXT("Level fixture package remained loaded during cleanup: ")
                    + Record->PackageName;
            }
        }
        if (!Filenames.IsEmpty())
        {
            Registry.ScanModifiedAssetFiles(Filenames);
        }
        return OutError.IsEmpty();
    }

    bool IsUnloadedMapStillUnloaded() const
    {
        return FindPackage(nullptr, *Unloaded.PackageName) == nullptr
            && FindObject<UWorld>(nullptr, *Unloaded.ObjectPath) == nullptr;
    }

    bool Activate(
        const FLevelMapRecord& Record,
        FString& OutError)
    {
        if (GEditor == nullptr || Record.World == nullptr)
        {
            OutError = TEXT("Cannot activate a null Level fixture in the Editor World context.");
            return false;
        }
        FWorldContext& Context = GEditor->GetEditorWorldContext();
        if (!bEditorContextChanged)
        {
            OriginalEditorWorld = Context.World();
            bEditorContextChanged = true;
        }
        Context.SetCurrentWorld(Record.World);
        if (Context.World() != Record.World)
        {
            OutError = TEXT("UE failed to bind the saved Level fixture to the Editor World context.");
            return false;
        }
        return true;
    }

    bool SaveLoadedMapForDurability(FString& OutError)
    {
        return SaveMap(Loaded, OutError);
    }

    bool UnloadLoadedMapForDurability(FString& OutError)
    {
        if (GEditor != nullptr
            && GEditor->GetEditorWorldContext().World() == Loaded.World)
        {
            GEditor->GetEditorWorldContext().SetCurrentWorld(
                OriginalEditorWorld);
        }
        return UnloadMap(Loaded, OutError);
    }

    bool ReloadLoadedMapForDurability(FString& OutError)
    {
        OutError.Reset();
        if (Loaded.World != nullptr
            || FindPackage(nullptr, *Loaded.PackageName) != nullptr)
        {
            OutError = TEXT(
                "Cannot reload a Level durability fixture that is already loaded.");
            return false;
        }

        UPackage* Package = LoadWorldPackageForEditor(
            Loaded.PackageName,
            EWorldType::Editor,
            LOAD_None);
        const FString ObjectName =
            FPackageName::ObjectPathToObjectName(Loaded.ObjectPath);
        UWorld* World = Package != nullptr
            ? FindObject<UWorld>(Package, *ObjectName)
            : nullptr;
        if (World == nullptr
            || World->PersistentLevel == nullptr
            || World->GetPathName() != Loaded.ObjectPath)
        {
            OutError = TEXT(
                "UE failed to reload the exact saved Level durability fixture: ")
                + Loaded.ObjectPath;
            return false;
        }

        World->SetFlags(RF_Transactional);
        Loaded.World = World;
        return ValidateOnDiskEvidence(Loaded, false, OutError);
    }

    FLevelMapRecord Loaded;
    FLevelMapRecord Unloaded;
    FLevelMapRecord Corrupt;
    AActor* Alpha = nullptr;
    AActor* Beta = nullptr;
    AActor* Gamma = nullptr;
    AActor* InstanceProjection = nullptr;
    AActor* ExternalContent = nullptr;
    AActor* PcgGenerated = nullptr;
    AActor* CorruptStable = nullptr;
    AActor* InvalidGuidActor = nullptr;
    AActor* DuplicateFirst = nullptr;
    AActor* DuplicateSecond = nullptr;
    FGuid AlphaId;
    FGuid BetaId;
    FGuid GammaId;
    FGuid InstanceProjectionId;
    FGuid ExternalContentId;
    FGuid PcgGeneratedId;
    FGuid DuplicateId;
    FGuid UnloadedPCGActorId;
    FString UnloadedPCGId;

private:
    static UWorld::InitializationValues FixtureInitializationValues()
    {
        return UWorld::InitializationValues()
            .RequiresHitProxies(false)
            .ShouldSimulatePhysics(false)
            .EnableTraceCollision(false)
            .CreateNavigation(false)
            .CreateAISystem(false)
            .AllowAudioPlayback(false)
            .CreatePhysicsScene(false);
    }

    bool CreateMap(
        const FString& AssetName,
        FLevelMapRecord& OutRecord,
        FString& OutError)
    {
        OutRecord.PackageName = RootPackagePath + TEXT("/") + AssetName;
        OutRecord.ObjectPath = OutRecord.PackageName
            + TEXT(".")
            + AssetName;
        OutRecord.Filename = FPackageName::LongPackageNameToFilename(
            OutRecord.PackageName,
            FPackageName::GetMapPackageExtension());
        IFileManager::Get().MakeDirectory(
            *FPaths::GetPath(OutRecord.Filename),
            true);

        UPackage* Package = CreatePackage(*OutRecord.PackageName);
        const UWorld::InitializationValues InitValues =
            FixtureInitializationValues();
        OutRecord.World = UWorld::CreateWorld(
            EWorldType::Editor,
            false,
            FName(*AssetName),
            Package,
            false,
            ERHIFeatureLevel::Num,
            &InitValues);
        if (OutRecord.World == nullptr
            || OutRecord.World->PersistentLevel == nullptr
            || OutRecord.World->GetPathName() != OutRecord.ObjectPath)
        {
            OutError = TEXT("UE failed to create an exact Editor UWorld fixture at ")
                + OutRecord.ObjectPath;
            return false;
        }
        OutRecord.World->SetFlags(
            RF_Public | RF_Standalone | RF_Transactional);
        // SaveMap immediately establishes authoritative on-disk Registry
        // evidence. Avoid a transient AssetCreated registration that would be
        // reported as a deletion when the source map is deliberately unloaded.
        OutRecord.bRegisteredInMemory = false;
        OutRecord.World->GetOutermost()->SetDirtyFlag(false);
        return true;
    }

    static AActor* SpawnActor(
        UWorld* World,
        const FString& Name,
        const FString& Label,
        const FTransform& Transform,
        FString& OutError)
    {
        FActorSpawnParameters Params;
        Params.Name = FName(*Name);
        Params.OverrideLevel = World != nullptr
            ? World->PersistentLevel
            : nullptr;
        Params.ObjectFlags = RF_Transactional;
        AActor* Actor = World != nullptr
            ? World->SpawnActor<AActor>(
                AActor::StaticClass(),
                Transform,
                Params)
            : nullptr;
        if (Actor == nullptr)
        {
            OutError = TEXT("UE failed to spawn Level fixture Actor ") + Name;
            return nullptr;
        }
        Actor->SetActorLabel(Label, false);
        return Actor;
    }

    static bool SaveMap(
        FLevelMapRecord& Record,
        FString& OutError)
    {
        if (Record.World == nullptr)
        {
            OutError = TEXT("Cannot save a null Level fixture World.");
            return false;
        }
        UPackage* Package = Record.World->GetOutermost();
        Package->SetDirtyFlag(true);
        Package->FullyLoad();
        FSavePackageArgs SaveArgs;
        SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
        SaveArgs.Error = GLog;
        if (!UPackage::SavePackage(
                Package,
                Record.World,
                *Record.Filename,
                SaveArgs))
        {
            OutError = TEXT("UE failed to save Level fixture ")
                + Record.ObjectPath;
            return false;
        }
        Package->SetDirtyFlag(false);
        FModuleManager::LoadModuleChecked<FAssetRegistryModule>(
            TEXT("AssetRegistry"))
            .Get()
            .ScanModifiedAssetFiles({Record.Filename});
        return true;
    }

    static bool UnloadMap(
        FLevelMapRecord& Record,
        FString& OutError)
    {
        if (Record.World == nullptr)
        {
            OutError = TEXT("Cannot unload a null Level fixture World.");
            return false;
        }
        UPackage* Package = Record.World->GetOutermost();
        if (Record.bRegisteredInMemory)
        {
            // The saved map remains a valid on-disk Asset. Do not announce a
            // deletion immediately before rescanning that same file; the scan
            // replaces the transient registration with disk evidence.
            Record.bRegisteredInMemory = false;
        }
        Record.World->DestroyWorld(false);
        PrepareLevelPackageForCollection(Package);
        Record.World = nullptr;
        CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);

        FModuleManager::LoadModuleChecked<FAssetRegistryModule>(
            TEXT("AssetRegistry"))
            .Get()
            .ScanModifiedAssetFiles({Record.Filename});
        if (FindPackage(nullptr, *Record.PackageName) != nullptr
            || FindObject<UWorld>(nullptr, *Record.ObjectPath) != nullptr)
        {
            OutError = TEXT("Saved Level fixture remained loaded: ")
                + Record.ObjectPath;
            return false;
        }
        return true;
    }

    static bool ValidateOnDiskEvidence(
        const FLevelMapRecord& Record,
        const bool bExpectUnloaded,
        FString& OutError)
    {
        IAssetRegistry& Registry =
            FModuleManager::LoadModuleChecked<FAssetRegistryModule>(
                TEXT("AssetRegistry"))
                .Get();
        const FAssetData Data = Registry.GetAssetByObjectPath(
            FSoftObjectPath(Record.ObjectPath),
            true);
        if (!Data.IsValid()
            || Data.AssetClassPath != UWorld::StaticClass()->GetClassPathName())
        {
            OutError = TEXT("Asset Registry lacks on-disk UWorld evidence for ")
                + Record.ObjectPath;
            return false;
        }
        if (bExpectUnloaded
            && (Data.FastGetAsset(false) != nullptr
                || FindPackage(nullptr, *Record.PackageName) != nullptr))
        {
            OutError = TEXT("On-disk Level fixture unexpectedly remained loaded: ")
                + Record.ObjectPath;
            return false;
        }
        return true;
    }

    FString Token;
    FString RootPackagePath;
    bool bCleaned = false;
    UWorld* OriginalEditorWorld = nullptr;
    bool bEditorContextChanged = false;
};

class FScopedLevelComponentQueryFixture
{
public:
    FScopedLevelComponentQueryFixture() = default;
    FScopedLevelComponentQueryFixture(
        const FScopedLevelComponentQueryFixture&) = delete;
    FScopedLevelComponentQueryFixture& operator=(
        const FScopedLevelComponentQueryFixture&) = delete;

    ~FScopedLevelComponentQueryFixture()
    {
        FString Ignored;
        Cleanup(Ignored);
    }

    bool Build(
        FScopedLevelQueryFixture& LevelFixture,
        FString& OutError)
    {
        OutError.Reset();
        UWorld* World = LevelFixture.Loaded.World;
        if (World == nullptr || World->PersistentLevel == nullptr)
        {
            OutError = TEXT(
                "Component fixture requires the loaded Level fixture World.");
            return false;
        }

        const FString Token =
            FGuid::NewGuid().ToString(EGuidFormats::Digits);
        GraphPackageName = FString::Printf(
            TEXT("/Game/LoomleTests/LevelComponent/%s/PCG_LevelComponent"),
            *Token);
        GraphPackage = CreatePackage(*GraphPackageName);
        Graph = GraphPackage != nullptr
            ? NewObject<UPCGGraph>(
                GraphPackage,
                FName(TEXT("PCG_LevelComponent")),
                RF_Public | RF_Standalone | RF_Transactional)
            : nullptr;
        if (Graph == nullptr)
        {
            OutError = TEXT(
                "UE failed to create the Component fixture PCG Graph asset.");
            return false;
        }
        GraphRoot.Reset(Graph);
        FAssetRegistryModule::AssetCreated(Graph);
        bGraphRegistered = true;
        GraphPath = Graph->GetPathName();
        GraphFilename = FPackageName::LongPackageNameToFilename(
            GraphPackageName,
            FPackageName::GetAssetPackageExtension());
        IFileManager::Get().MakeDirectory(
            *FPaths::GetPath(GraphFilename),
            true);
        GraphPackage->SetDirtyFlag(true);
        FSavePackageArgs GraphSaveArgs;
        GraphSaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
        GraphSaveArgs.Error = GLog;
        if (!UPackage::SavePackage(
                GraphPackage,
                Graph,
                *GraphFilename,
                GraphSaveArgs))
        {
            OutError = TEXT(
                "UE failed to save the Component fixture PCG Graph asset.");
            return false;
        }
        GraphPackage->SetDirtyFlag(false);
        FModuleManager::LoadModuleChecked<FAssetRegistryModule>(
            TEXT("AssetRegistry"))
            .Get()
            .ScanModifiedAssetFiles({GraphFilename});
        bGraphRegistered = false;

        UnsavedGraphPackageName = FString::Printf(
            TEXT("/Game/LoomleTests/LevelComponent/%s/PCG_Unsaved"),
            *Token);
        UnsavedGraphPackage = CreatePackage(*UnsavedGraphPackageName);
        UnsavedGraph = UnsavedGraphPackage != nullptr
            ? NewObject<UPCGGraph>(
                UnsavedGraphPackage,
                FName(TEXT("PCG_Unsaved")),
                RF_Public | RF_Standalone | RF_Transactional)
            : nullptr;
        if (UnsavedGraph == nullptr)
        {
            OutError = TEXT(
                "UE failed to create the unsaved Graph evidence fixture.");
            return false;
        }
        UnsavedGraphRoot.Reset(UnsavedGraph);
        FAssetRegistryModule::AssetCreated(UnsavedGraph);
        bUnsavedGraphRegistered = true;
        UnsavedGraphPath = UnsavedGraph->GetPathName();

        BlueprintPackageName = FString::Printf(
            TEXT("/Game/LoomleTests/LevelComponent/%s/BP_LevelComponent"),
            *Token);
        BlueprintFilename = FPackageName::LongPackageNameToFilename(
            BlueprintPackageName,
            FPackageName::GetAssetPackageExtension());
        IFileManager::Get().MakeDirectory(
            *FPaths::GetPath(BlueprintFilename),
            true);
        BlueprintPackage = CreatePackage(*BlueprintPackageName);
        Blueprint = BlueprintPackage != nullptr
            ? FKismetEditorUtilities::CreateBlueprint(
                AStaticMeshActor::StaticClass(),
                BlueprintPackage,
                FName(TEXT("BP_LevelComponent")),
                BPTYPE_Normal,
                UBlueprint::StaticClass(),
                UBlueprintGeneratedClass::StaticClass(),
                NAME_None)
            : nullptr;
        if (Blueprint == nullptr
            || Blueprint->SimpleConstructionScript == nullptr)
        {
            OutError = TEXT(
                "UE failed to create the Component fixture Blueprint and SCS.");
            return false;
        }
        BlueprintPath = Blueprint->GetPathName();
        BlueprintRoot.Reset(Blueprint);
        FAssetRegistryModule::AssetCreated(Blueprint);
        bBlueprintRegistered = true;

        SCSNode = Blueprint->SimpleConstructionScript->CreateNode(
            USceneComponent::StaticClass(),
            FName(TEXT("LoomleSCSComponent")));
        SCSCollisionProbeNode =
            Blueprint->SimpleConstructionScript->CreateNode(
                USceneComponent::StaticClass(),
                FName(TEXT("LoomleSCSCollisionProbe")));
        if (SCSNode == nullptr
            || !SCSNode->VariableGuid.IsValid()
            || SCSCollisionProbeNode == nullptr
            || !SCSCollisionProbeNode->VariableGuid.IsValid()
            || SCSCollisionProbeNode->VariableGuid == SCSNode->VariableGuid)
        {
            OutError = TEXT(
                "UE failed to create the Component fixture SCS declaration.");
            return false;
        }
        Blueprint->SimpleConstructionScript->AddNode(SCSNode);
        Blueprint->SimpleConstructionScript->AddNode(
            SCSCollisionProbeNode);
        SCSGuid = SCSNode->VariableGuid;
        FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
        FKismetEditorUtilities::CompileBlueprint(Blueprint);
        GeneratedClass = Cast<UBlueprintGeneratedClass>(
            Blueprint->GeneratedClass);
        if (Blueprint->Status == BS_Error || GeneratedClass == nullptr)
        {
            OutError = TEXT(
                "The Component fixture Blueprint failed to compile.");
            return false;
        }

        FActorSpawnParameters Params;
        Params.Name = FName(TEXT("Actor_LevelComponents"));
        Params.OverrideLevel = World->PersistentLevel;
        Params.ObjectFlags = RF_Transactional;
        Actor = World->SpawnActor<AActor>(
            GeneratedClass,
            FTransform::Identity,
            Params);
        if (Actor == nullptr)
        {
            OutError = TEXT(
                "UE failed to spawn the Component fixture Blueprint Actor.");
            return false;
        }
        Actor->SetActorLabel(TEXT("Loomle Component Owner"), false);
        ActorId = Actor->GetActorGuid();
        NativeComponent = CastChecked<AStaticMeshActor>(Actor)
            ->GetStaticMeshComponent();

        FActorSpawnParameters NativePCGParams;
        NativePCGParams.Name = FName(TEXT("Actor_NativePCGComponent"));
        NativePCGParams.OverrideLevel = World->PersistentLevel;
        NativePCGParams.ObjectFlags = RF_Transactional;
        NativePCGActor = World->SpawnActor<APCGVolume>(
            APCGVolume::StaticClass(),
            FTransform::Identity,
            NativePCGParams);
        NativePCGComponent = NativePCGActor != nullptr
            ? NativePCGActor->PCGComponent.Get()
            : nullptr;
        if (NativePCGActor != nullptr)
        {
            NativePCGActor->SetActorLabel(
                TEXT("Loomle Native PCG Component Owner"),
                false);
            NativePCGActorId = NativePCGActor->GetActorGuid();
        }

        TArray<UActorComponent*> SpawnedComponents;
        Actor->GetComponents(SpawnedComponents, false);
        for (UActorComponent* Component : SpawnedComponents)
        {
            if (Component != nullptr
                && Component->CreationMethod
                    == EComponentCreationMethod::SimpleConstructionScript
                && Component->GetFName() == SCSNode->GetVariableName())
            {
                SCSComponent = Component;
            }
        }
        const FObjectPropertyBase* SCSProperty =
            FindFProperty<FObjectPropertyBase>(
                GeneratedClass,
                SCSNode->GetVariableName());

        InstanceComponent = NewObject<USceneComponent>(
            Actor,
            USceneComponent::StaticClass(),
            FName(TEXT("LoomleInstanceComponent")),
            RF_Transactional);
        if (InstanceComponent != nullptr)
        {
            Actor->AddInstanceComponent(InstanceComponent);
            InstanceComponent->OnComponentCreated();
            InstanceComponent->RegisterComponent();
        }

        PCGComponent = NewObject<UPCGComponent>(
            Actor,
            UPCGComponent::StaticClass(),
            FName(TEXT("LoomlePCGComponent")),
            RF_Transactional);
        if (PCGComponent != nullptr)
        {
            Actor->AddInstanceComponent(PCGComponent);
            PCGComponent->OnComponentCreated();
        }
        if (NativePCGComponent != nullptr)
        {
            NativePCGComponent->SetGraphLocal(Graph);
        }
        if (PCGComponent != nullptr)
        {
            PCGComponent->SetGraphLocal(UnsavedGraph);
        }

        const TArray<TPair<FName, FName>> GeneratedPCGSpecs = {
            {
                FName(TEXT("LoomlePCGGeneratedComponent")),
                PCGHelpers::DefaultPCGTag
            },
            {
                FName(TEXT("LoomlePCGDebugComponent")),
                PCGHelpers::DefaultPCGDebugTag
            },
            {
                FName(TEXT("LoomlePCGCleanupComponent")),
                PCGHelpers::MarkedForCleanupPCGTag
            }
        };
        for (const TPair<FName, FName>& Spec : GeneratedPCGSpecs)
        {
            USceneComponent* Generated = NewObject<USceneComponent>(
                Actor,
                USceneComponent::StaticClass(),
                Spec.Key,
                RF_Transactional);
            if (Generated != nullptr)
            {
                Actor->AddInstanceComponent(Generated);
                Generated->ComponentTags.Add(Spec.Value);
                GeneratedPCGComponents.Add(Generated);
            }
        }

        UCSComponent = Actor->AddComponentByClass(
            USceneComponent::StaticClass(),
            true,
            FTransform::Identity,
            false);

        if (!ActorId.IsValid()
            || NativeComponent == nullptr
            || NativeComponent->CreationMethod
                != EComponentCreationMethod::Native
            || SCSComponent == nullptr
            || InstanceComponent == nullptr
            || !InstanceComponent->IsRegistered()
            || PCGComponent == nullptr
            || PCGComponent->IsRegistered()
            || PCGComponent->GetConstOriginalComponent() != PCGComponent
            || NativePCGActor == nullptr
            || !NativePCGActorId.IsValid()
            || NativePCGComponent == nullptr
            || NativePCGComponent->CreationMethod
                != EComponentCreationMethod::Native
            || NativePCGComponent->GetConstOriginalComponent()
                != NativePCGComponent
            || NativePCGComponent->GetGraph() != Graph
            || PCGComponent->GetGraph() != UnsavedGraph
            || GeneratedPCGComponents.Num() != GeneratedPCGSpecs.Num()
            || UCSComponent == nullptr
            || UCSComponent->CreationMethod
                != EComponentCreationMethod::UserConstructionScript)
        {
            OutError = TEXT(
                "The Component fixture did not produce exact Native, SCS, "
                "Instance, PCG, and UCS source evidence.");
            return false;
        }

        NativeId = NativeComponent->GetFName().ToString();
        InstanceId = InstanceComponent->GetFName().ToString();
        PCGId = PCGComponent->GetFName().ToString();
        NativePCGId = NativePCGComponent->GetFName().ToString();
        SCSDeclaringClass = GeneratedClass->GetPathName();
        SCSId = SCSDeclaringClass
            + TEXT("#")
            + LevelGuidText(SCSNode->VariableGuid);
        if (NativeId.IsEmpty()
            || InstanceId.IsEmpty()
            || PCGId.IsEmpty()
            || NativePCGId.IsEmpty()
            || GraphPath.IsEmpty()
            || UnsavedGraphPath.IsEmpty()
            || SCSDeclaringClass.IsEmpty()
            || SCSId.IsEmpty()
            || SCSProperty == nullptr
            || SCSProperty->GetObjectPropertyValue_InContainer(Actor)
                != SCSComponent)
        {
            OutError = TEXT(
                "The Component fixture could not prove its stable source locators.");
            return false;
        }

        World->GetOutermost()->SetDirtyFlag(false);
        BlueprintPackage->SetDirtyFlag(false);
        GraphPackage->SetDirtyFlag(false);
        UnsavedGraphPackage->SetDirtyFlag(false);
        return true;
    }

    bool Cleanup(FString& OutError)
    {
        OutError.Reset();
        if (bCleaned)
        {
            return true;
        }
        bCleaned = true;

        UBlueprint* RootedBlueprint = BlueprintRoot.Get();
        const bool bBlueprintExistsOnDisk =
            !BlueprintFilename.IsEmpty()
            && IFileManager::Get().FileExists(*BlueprintFilename);
        if (bBlueprintRegistered
            && RootedBlueprint != nullptr
            && !bBlueprintExistsOnDisk)
        {
            FAssetRegistryModule::AssetDeleted(RootedBlueprint);
        }
        bBlueprintRegistered = false;
        UPackage* Package = !BlueprintPackageName.IsEmpty()
            ? FindPackage(nullptr, *BlueprintPackageName)
            : nullptr;
        PrepareLevelPackageForCollection(Package);

        UPCGGraph* RootedGraph = GraphRoot.Get();
        const bool bGraphExistsOnDisk =
            !GraphFilename.IsEmpty()
            && IFileManager::Get().FileExists(*GraphFilename);
        if (bGraphRegistered
            && RootedGraph != nullptr
            && !bGraphExistsOnDisk)
        {
            FAssetRegistryModule::AssetDeleted(RootedGraph);
        }
        bGraphRegistered = false;
        UPackage* ExistingGraphPackage = !GraphPackageName.IsEmpty()
            ? FindPackage(nullptr, *GraphPackageName)
            : nullptr;
        PrepareLevelPackageForCollection(ExistingGraphPackage);
        UPCGGraph* RootedUnsavedGraph = UnsavedGraphRoot.Get();
        if (bUnsavedGraphRegistered && RootedUnsavedGraph != nullptr)
        {
            FAssetRegistryModule::AssetDeleted(RootedUnsavedGraph);
            bUnsavedGraphRegistered = false;
        }
        UPackage* ExistingUnsavedGraphPackage =
            !UnsavedGraphPackageName.IsEmpty()
            ? FindPackage(nullptr, *UnsavedGraphPackageName)
            : nullptr;
        PrepareLevelPackageForCollection(ExistingUnsavedGraphPackage);

        Actor = nullptr;
        NativeComponent = nullptr;
        SCSComponent = nullptr;
        InstanceComponent = nullptr;
        PCGComponent = nullptr;
        NativePCGActor = nullptr;
        NativePCGComponent = nullptr;
        TwinActor = nullptr;
        TwinNativeComponent = nullptr;
        TwinSCSComponent = nullptr;
        TwinInstanceComponent = nullptr;
        TwinPCGComponent = nullptr;
        GeneratedPCGComponents.Reset();
        UCSComponent = nullptr;
        SCSNode = nullptr;
        SCSCollisionProbeNode = nullptr;
        GeneratedClass = nullptr;
        Blueprint = nullptr;
        BlueprintPackage = nullptr;
        BlueprintRoot.Reset();
        Graph = nullptr;
        GraphPackage = nullptr;
        GraphRoot.Reset();
        UnsavedGraph = nullptr;
        UnsavedGraphPackage = nullptr;
        UnsavedGraphRoot.Reset();
        CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);
        if (!BlueprintPackageName.IsEmpty()
            && FindPackage(nullptr, *BlueprintPackageName) != nullptr)
        {
            OutError = TEXT(
                "Component fixture Blueprint package remained loaded during cleanup: ")
                + BlueprintPackageName;
        }
        if (!GraphPackageName.IsEmpty()
            && FindPackage(nullptr, *GraphPackageName) != nullptr
            && OutError.IsEmpty())
        {
            OutError = TEXT(
                "Component fixture PCG Graph package remained loaded during cleanup: ")
                + GraphPackageName;
        }
        if (!UnsavedGraphPackageName.IsEmpty()
            && FindPackage(nullptr, *UnsavedGraphPackageName) != nullptr
            && OutError.IsEmpty())
        {
            OutError = TEXT(
                "Component fixture unsaved PCG Graph package remained loaded during cleanup: ")
                + UnsavedGraphPackageName;
        }
        if (!GraphFilename.IsEmpty())
        {
            IFileManager::Get().Delete(
                *GraphFilename,
                false,
                true,
                true);
            FModuleManager::LoadModuleChecked<FAssetRegistryModule>(
                TEXT("AssetRegistry"))
                .Get()
                .ScanModifiedAssetFiles({GraphFilename});
        }
        if (!BlueprintFilename.IsEmpty())
        {
            IFileManager::Get().Delete(
                *BlueprintFilename,
                false,
                true,
                true);
            FModuleManager::LoadModuleChecked<FAssetRegistryModule>(
                TEXT("AssetRegistry"))
                .Get()
                .ScanModifiedAssetFiles({BlueprintFilename});
        }
        return OutError.IsEmpty();
    }

    AActor* Actor = nullptr;
    UActorComponent* NativeComponent = nullptr;
    UActorComponent* SCSComponent = nullptr;
    USceneComponent* InstanceComponent = nullptr;
    UPCGComponent* PCGComponent = nullptr;
    APCGVolume* NativePCGActor = nullptr;
    UPCGComponent* NativePCGComponent = nullptr;
    AActor* TwinActor = nullptr;
    UActorComponent* TwinNativeComponent = nullptr;
    UActorComponent* TwinSCSComponent = nullptr;
    USceneComponent* TwinInstanceComponent = nullptr;
    UPCGComponent* TwinPCGComponent = nullptr;
    TArray<USceneComponent*> GeneratedPCGComponents;
    UActorComponent* UCSComponent = nullptr;
    FGuid ActorId;
    FGuid NativePCGActorId;
    FGuid TwinActorId;
    FString NativeId;
    FString SCSId;
    FString SCSDeclaringClass;
    FString InstanceId;
    FString PCGId;
    FString NativePCGId;
    FString GraphPath;
    FString UnsavedGraphPath;

    UPackage* GetBlueprintPackage() const
    {
        return BlueprintPackage;
    }

    UBlueprint* GetBlueprint() const
    {
        return Blueprint;
    }

    UBlueprintGeneratedClass* GetGeneratedClass() const
    {
        return GeneratedClass;
    }

    bool IsBlueprintUnloadedForDurability() const
    {
        return FindPackage(nullptr, *BlueprintPackageName) == nullptr
            && FindObject<UBlueprint>(nullptr, *BlueprintPath) == nullptr
            && FindObject<UClass>(nullptr, *SCSDeclaringClass) == nullptr;
    }

    FName GetSCSVariableName() const
    {
        return SCSNode != nullptr
            ? SCSNode->GetVariableName()
            : NAME_None;
    }

    bool AddSameSlotTwin(UWorld* World, FString& OutError)
    {
        OutError.Reset();
        if (World == nullptr
            || World->PersistentLevel == nullptr
            || GeneratedClass == nullptr
            || Actor == nullptr
            || Graph == nullptr)
        {
            OutError = TEXT(
                "Same-slot Component fixture requires its loaded World, Blueprint Class, owner, and saved Graph.");
            return false;
        }

        FActorSpawnParameters Params;
        Params.Name = FName(TEXT("Actor_LevelComponentsTwin"));
        Params.OverrideLevel = World->PersistentLevel;
        Params.ObjectFlags = RF_Transactional;
        TwinActor = World->SpawnActor<AActor>(
            GeneratedClass,
            FTransform(FVector(250.0, 0.0, 0.0)),
            Params);
        if (TwinActor == nullptr)
        {
            OutError = TEXT(
                "UE failed to spawn the same-Class Component identity twin.");
            return false;
        }
        TwinActor->SetActorLabel(
            TEXT("Loomle Component Owner Twin"),
            false);
        TwinActorId = TwinActor->GetActorGuid();

        TwinInstanceComponent = NewObject<USceneComponent>(
            TwinActor,
            USceneComponent::StaticClass(),
            FName(*InstanceId),
            RF_Transactional);
        if (TwinInstanceComponent != nullptr)
        {
            TwinActor->AddInstanceComponent(TwinInstanceComponent);
            TwinInstanceComponent->OnComponentCreated();
            TwinInstanceComponent->RegisterComponent();
        }
        TwinPCGComponent = NewObject<UPCGComponent>(
            TwinActor,
            UPCGComponent::StaticClass(),
            FName(*PCGId),
            RF_Transactional);
        if (TwinPCGComponent != nullptr)
        {
            TwinActor->AddInstanceComponent(TwinPCGComponent);
            TwinPCGComponent->OnComponentCreated();
            TwinPCGComponent->SetGraphLocal(Graph);
        }
        PCGComponent->SetGraphLocal(Graph);

        if (!TwinActorId.IsValid()
            || TwinInstanceComponent == nullptr
            || TwinPCGComponent == nullptr
            || !RefreshAfterExternalLifecycle(World, OutError))
        {
            if (OutError.IsEmpty())
            {
                OutError = TEXT(
                    "The same-slot Component identity twin did not retain exact authored slots.");
            }
            return false;
        }
        return true;
    }

    bool RerunConstructionScriptsAndRefresh(
        UWorld* World,
        FString& OutError)
    {
        OutError.Reset();
        if (Actor == nullptr || TwinActor == nullptr)
        {
            OutError = TEXT(
                "Component reconstruction requires both loaded Blueprint Actors.");
            return false;
        }
        Actor->RerunConstructionScripts();
        TwinActor->RerunConstructionScripts();
        CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);
        return RefreshAfterExternalLifecycle(World, OutError);
    }

    bool RenameSCSRecompileAndRefresh(
        UWorld* World,
        FString& OutError)
    {
        OutError.Reset();
        if (Blueprint == nullptr
            || SCSNode == nullptr
            || !SCSGuid.IsValid())
        {
            OutError = TEXT(
                "Blueprint recompile requires the exact SCS declaration.");
            return false;
        }

        const FString StableSCSId = SCSId;
        SCSNode->SetVariableName(
            FName(TEXT("LoomleRenamedSCSComponent")));
        if (SCSNode->VariableGuid != SCSGuid)
        {
            OutError = TEXT(
                "Renaming the SCS variable unexpectedly changed its VariableGuid.");
            return false;
        }
        FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
        FKismetEditorUtilities::CompileBlueprint(Blueprint);
        const bool bCompileFailed = Blueprint->Status == BS_Error;
        CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);
        if (bCompileFailed
            || !RefreshAfterExternalLifecycle(World, OutError))
        {
            if (OutError.IsEmpty())
            {
                OutError = TEXT(
                    "The Component durability Blueprint failed to recompile.");
            }
            return false;
        }
        if (SCSId != StableSCSId)
        {
            OutError = TEXT(
                "The qualified SCS StableRef changed across variable rename and Blueprint recompile.");
            return false;
        }
        return true;
    }

    bool SaveBlueprintForDurability(FString& OutError)
    {
        OutError.Reset();
        if (Blueprint == nullptr
            || BlueprintPackage == nullptr
            || BlueprintFilename.IsEmpty())
        {
            OutError = TEXT(
                "Cannot save an incomplete Component durability Blueprint.");
            return false;
        }
        BlueprintPackage->SetDirtyFlag(true);
        BlueprintPackage->FullyLoad();
        FSavePackageArgs SaveArgs;
        SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
        SaveArgs.Error = GLog;
        if (!UPackage::SavePackage(
                BlueprintPackage,
                Blueprint,
                *BlueprintFilename,
                SaveArgs))
        {
            OutError = TEXT(
                "UE failed to save the Component durability Blueprint: ")
                + BlueprintPath;
            return false;
        }
        BlueprintPackage->SetDirtyFlag(false);
        FModuleManager::LoadModuleChecked<FAssetRegistryModule>(
            TEXT("AssetRegistry"))
            .Get()
            .ScanModifiedAssetFiles({BlueprintFilename});
        bBlueprintRegistered = false;
        return true;
    }

    void ReleaseWorldObjectsForReload()
    {
        Actor = nullptr;
        NativeComponent = nullptr;
        SCSComponent = nullptr;
        InstanceComponent = nullptr;
        PCGComponent = nullptr;
        NativePCGActor = nullptr;
        NativePCGComponent = nullptr;
        TwinActor = nullptr;
        TwinNativeComponent = nullptr;
        TwinSCSComponent = nullptr;
        TwinInstanceComponent = nullptr;
        TwinPCGComponent = nullptr;
        GeneratedPCGComponents.Reset();
        UCSComponent = nullptr;
    }

    bool UnloadBlueprintForDurability(FString& OutError)
    {
        OutError.Reset();
        UPackage* Package = BlueprintPackage;
        BlueprintRoot.Reset();
        Blueprint = nullptr;
        GeneratedClass = nullptr;
        SCSNode = nullptr;
        SCSCollisionProbeNode = nullptr;
        BlueprintPackage = nullptr;
        PrepareLevelPackageForCollection(Package);
        CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);
        if (!IsBlueprintUnloadedForDurability())
        {
            OutError = TEXT(
                "Saved Component durability Blueprint or generated Class remained loaded: ")
                + BlueprintPath;
            return false;
        }
        return true;
    }

    bool RefreshAfterExternalLifecycle(
        UWorld* World,
        FString& OutError)
    {
        OutError.Reset();
        if (World == nullptr || World->PersistentLevel == nullptr)
        {
            OutError = TEXT(
                "Cannot refresh Component identity without its exact loaded World.");
            return false;
        }
        if (Blueprint == nullptr)
        {
            UPackage* Package = FindPackage(nullptr, *BlueprintPackageName);
            const FString BlueprintName =
                FPackageName::ObjectPathToObjectName(BlueprintPath);
            Blueprint = Package != nullptr
                ? FindObject<UBlueprint>(Package, *BlueprintName)
                : nullptr;
            if (Blueprint == nullptr)
            {
                OutError = TEXT(
                    "The reloaded map did not reload its saved Component Blueprint dependency: ")
                    + BlueprintPath;
                return false;
            }
            BlueprintPackage = Package;
            BlueprintRoot.Reset(Blueprint);
        }
        GeneratedClass = Cast<UBlueprintGeneratedClass>(
            Blueprint->GeneratedClass);
        if (GeneratedClass == nullptr
            || Blueprint->SimpleConstructionScript == nullptr)
        {
            OutError = TEXT(
                "The loaded Component Blueprint has no valid generated Class or SCS.");
            return false;
        }

        SCSNode = FindSCSNodeByGuid(
            Blueprint->SimpleConstructionScript,
            SCSGuid);
        if (SCSNode == nullptr)
        {
            OutError = TEXT(
                "The loaded Component Blueprint lost its persistent SCS VariableGuid.");
            return false;
        }
        const FString RefreshedDeclaringClass =
            GeneratedClass->GetPathName();
        const FString RefreshedSCSId = RefreshedDeclaringClass
            + TEXT("#")
            + LevelGuidText(SCSGuid);
        if (!SCSId.IsEmpty() && RefreshedSCSId != SCSId)
        {
            OutError = TEXT(
                "The loaded Component Blueprint changed its qualified SCS identity.");
            return false;
        }
        SCSDeclaringClass = RefreshedDeclaringClass;
        SCSId = RefreshedSCSId;

        Actor = FindActorByGuid(World, ActorId, OutError);
        if (Actor == nullptr
            || !RefreshActorComponents(
                Actor,
                NativeComponent,
                SCSComponent,
                InstanceComponent,
                PCGComponent,
                OutError))
        {
            return false;
        }
        if (TwinActorId.IsValid())
        {
            TwinActor = FindActorByGuid(World, TwinActorId, OutError);
            if (TwinActor == nullptr
                || !RefreshActorComponents(
                    TwinActor,
                    TwinNativeComponent,
                    TwinSCSComponent,
                    TwinInstanceComponent,
                    TwinPCGComponent,
                    OutError))
            {
                return false;
            }
        }

        NativePCGActor = Cast<APCGVolume>(
            FindActorByGuid(World, NativePCGActorId, OutError));
        NativePCGComponent = NativePCGActor != nullptr
            ? NativePCGActor->PCGComponent.Get()
            : nullptr;
        if (NativePCGActor == nullptr
            || NativePCGComponent == nullptr
            || NativePCGComponent->GetFName().ToString()
                != NativePCGId
            || NativePCGComponent->CreationMethod
                != EComponentCreationMethod::Native
            || NativePCGComponent->GetConstOriginalComponent()
                != NativePCGComponent)
        {
            if (OutError.IsEmpty())
            {
                OutError = TEXT(
                    "The native original PCG Component did not survive lifecycle refresh.");
            }
            return false;
        }
        return true;
    }

    bool BeginSCSGuidCollision(FGuid& OutOriginalGuid)
    {
        OutOriginalGuid.Invalidate();
        if (SCSNode == nullptr
            || SCSCollisionProbeNode == nullptr
            || !SCSNode->VariableGuid.IsValid()
            || !SCSCollisionProbeNode->VariableGuid.IsValid())
        {
            return false;
        }
        OutOriginalGuid = SCSCollisionProbeNode->VariableGuid;
        SCSCollisionProbeNode->VariableGuid = SCSNode->VariableGuid;
        return true;
    }

    void EndSCSGuidCollision(const FGuid& OriginalGuid)
    {
        if (SCSCollisionProbeNode != nullptr && OriginalGuid.IsValid())
        {
            SCSCollisionProbeNode->VariableGuid = OriginalGuid;
        }
    }

private:
    static USCS_Node* FindSCSNodeByGuid(
        USimpleConstructionScript* SCS,
        const FGuid& Guid)
    {
        if (SCS == nullptr || !Guid.IsValid())
        {
            return nullptr;
        }
        TArray<USCS_Node*> Stack = SCS->GetRootNodes();
        TSet<const USCS_Node*> Seen;
        while (!Stack.IsEmpty() && Seen.Num() < 4096)
        {
            USCS_Node* Node = Stack.Pop(EAllowShrinking::No);
            if (Node == nullptr || Seen.Contains(Node))
            {
                continue;
            }
            Seen.Add(Node);
            if (Node->VariableGuid == Guid)
            {
                return Node;
            }
            for (USCS_Node* Child : Node->GetChildNodes())
            {
                Stack.Add(Child);
            }
        }
        return nullptr;
    }

    static AActor* FindActorByGuid(
        UWorld* World,
        const FGuid& Guid,
        FString& OutError)
    {
        AActor* Match = nullptr;
        int32 Matches = 0;
        if (World != nullptr && World->PersistentLevel != nullptr)
        {
            for (AActor* Candidate : World->PersistentLevel->Actors)
            {
                if (Candidate != nullptr
                    && Candidate->GetActorGuid() == Guid)
                {
                    Match = Candidate;
                    ++Matches;
                }
            }
        }
        if (Matches != 1)
        {
            OutError = FString::Printf(
                TEXT("Expected one reloaded Actor for Component owner %s, found %d."),
                *LevelGuidText(Guid),
                Matches);
            return nullptr;
        }
        return Match;
    }

    bool RefreshActorComponents(
        AActor* Owner,
        UActorComponent*& OutNative,
        UActorComponent*& OutSCS,
        USceneComponent*& OutInstance,
        UPCGComponent*& OutPCG,
        FString& OutError) const
    {
        OutNative = nullptr;
        OutSCS = nullptr;
        OutInstance = nullptr;
        OutPCG = nullptr;
        AStaticMeshActor* StaticMeshOwner =
            Cast<AStaticMeshActor>(Owner);
        OutNative = StaticMeshOwner != nullptr
            ? StaticMeshOwner->GetStaticMeshComponent()
            : nullptr;
        const FObjectPropertyBase* SCSProperty =
            Owner != nullptr && SCSNode != nullptr
            ? FindFProperty<FObjectPropertyBase>(
                Owner->GetClass(),
                SCSNode->GetVariableName())
            : nullptr;
        OutSCS = SCSProperty != nullptr
            ? Cast<UActorComponent>(
                SCSProperty->GetObjectPropertyValue_InContainer(Owner))
            : nullptr;
        if (Owner != nullptr)
        {
            for (UActorComponent* Component : Owner->GetInstanceComponents())
            {
                if (Component == nullptr)
                {
                    continue;
                }
                if (Component->GetFName() == FName(*InstanceId))
                {
                    OutInstance = Cast<USceneComponent>(Component);
                }
                else if (Component->GetFName() == FName(*PCGId))
                {
                    OutPCG = Cast<UPCGComponent>(Component);
                }
            }
        }
        if (OutNative == nullptr
            || OutNative->CreationMethod
                != EComponentCreationMethod::Native
            || OutNative->GetFName().ToString() != NativeId
            || OutSCS == nullptr
            || OutSCS->CreationMethod
                != EComponentCreationMethod::SimpleConstructionScript
            || OutInstance == nullptr
            || OutInstance->CreationMethod
                != EComponentCreationMethod::Instance
            || OutPCG == nullptr
            || OutPCG->CreationMethod
                != EComponentCreationMethod::Instance
            || OutPCG->GetConstOriginalComponent() != OutPCG
            || OutPCG->GetGraph() != Graph)
        {
            OutError = TEXT(
                "A Blueprint Actor did not refresh the exact native, SCS, instance, and original PCG slots.");
            return false;
        }
        return true;
    }

    FString BlueprintPackageName;
    FString BlueprintPath;
    FString BlueprintFilename;
    UPackage* BlueprintPackage = nullptr;
    UBlueprint* Blueprint = nullptr;
    UBlueprintGeneratedClass* GeneratedClass = nullptr;
    USCS_Node* SCSNode = nullptr;
    USCS_Node* SCSCollisionProbeNode = nullptr;
    FGuid SCSGuid;
    TStrongObjectPtr<UBlueprint> BlueprintRoot;
    FString GraphPackageName;
    FString GraphFilename;
    UPackage* GraphPackage = nullptr;
    UPCGGraph* Graph = nullptr;
    TStrongObjectPtr<UPCGGraph> GraphRoot;
    FString UnsavedGraphPackageName;
    UPackage* UnsavedGraphPackage = nullptr;
    UPCGGraph* UnsavedGraph = nullptr;
    TStrongObjectPtr<UPCGGraph> UnsavedGraphRoot;
    bool bBlueprintRegistered = false;
    bool bGraphRegistered = false;
    bool bUnsavedGraphRegistered = false;
    bool bCleaned = false;
};

struct FLevelInstanceLoadState
{
    TWeakObjectPtr<ALevelInstance> Actor;
    FString SourcePath;
    FGuid ActorInstanceGuid;
    bool bLoaded = false;
    bool bLoading = false;
};

class FScopedLevelInstanceQueryFixture
{
public:
    FScopedLevelInstanceQueryFixture() = default;
    FScopedLevelInstanceQueryFixture(
        const FScopedLevelInstanceQueryFixture&) = delete;
    FScopedLevelInstanceQueryFixture& operator=(
        const FScopedLevelInstanceQueryFixture&) = delete;

    ~FScopedLevelInstanceQueryFixture()
    {
        FString Ignored;
        Cleanup(Ignored);
    }

    bool Build(FString& OutError)
    {
        Token = FGuid::NewGuid().ToString(EGuidFormats::Digits);
        RootPackagePath = FString::Printf(
            TEXT("/Game/LoomleTests/LevelInstance/%s"),
            *Token);

        if (!CreateMap(TEXT("L_10_SourceA"), SourceA, OutError))
        {
            return false;
        }
        AActor* EditActorOne = SpawnActor(
            SourceA.World,
            EditActorOneName.ToString(),
            TEXT("Loomle Edit Actor One"),
            OutError);
        AActor* EditActorTwo = SpawnActor(
            SourceA.World,
            EditActorTwoName.ToString(),
            TEXT("Loomle Edit Actor Two"),
            OutError);
        if (EditActorOne == nullptr
            || EditActorTwo == nullptr
            || !SaveMap(SourceA, OutError)
            || !UnloadMap(SourceA, OutError)
            || !CreateMap(TEXT("L_20_SourceB"), SourceB, OutError)
            || !SaveMap(SourceB, OutError)
            || !UnloadMap(SourceB, OutError)
            || !CreateMap(
                TEXT("L_30_Containing"),
                Containing,
                OutError))
        {
            return false;
        }

        Ordinary = SpawnActor(
            Containing.World,
            TEXT("Actor_Ordinary"),
            TEXT("Loomle Ordinary Actor"),
            OutError);
        SharedOne = SpawnLevelInstance(
            Containing.World,
            TEXT("LI_SharedA"),
            TEXT("Loomle Resolved Instance Shared Source A"),
            OutError);
        SharedTwo = SpawnLevelInstance(
            Containing.World,
            TEXT("LI_SharedB"),
            TEXT("Loomle Resolved Instance Shared Source B"),
            OutError);
        OtherSource = SpawnLevelInstance(
            Containing.World,
            TEXT("LI_OtherSource"),
            TEXT("Loomle Resolved Instance Other Source"),
            OutError);
        MissingSource = SpawnLevelInstance(
            Containing.World,
            TEXT("LI_MissingSource"),
            TEXT("Loomle Missing Source Instance"),
            OutError);
        if (Ordinary == nullptr
            || SharedOne == nullptr
            || SharedTwo == nullptr
            || OtherSource == nullptr
            || MissingSource == nullptr
            || !SetSource(SharedOne, SourceA.ObjectPath, OutError)
            || !SetSource(SharedTwo, SourceA.ObjectPath, OutError)
            || !SetSource(OtherSource, SourceB.ObjectPath, OutError))
        {
            return false;
        }

        OrdinaryId = Ordinary->GetActorGuid();
        SharedOneId = SharedOne->GetActorGuid();
        SharedTwoId = SharedTwo->GetActorGuid();
        OtherSourceId = OtherSource->GetActorGuid();
        MissingSourceId = MissingSource->GetActorGuid();
        if (!OrdinaryId.IsValid()
            || !SharedOneId.IsValid()
            || !SharedTwoId.IsValid()
            || !OtherSourceId.IsValid()
            || !MissingSourceId.IsValid())
        {
            OutError = TEXT(
                "UE did not assign persistent ActorGuids to the Level Instance fixture Actors.");
            return false;
        }

        if (!SaveMap(Containing, OutError)
            || !ValidateOnDiskEvidence(SourceA, true, OutError)
            || !ValidateOnDiskEvidence(SourceB, true, OutError)
            || !ValidateOnDiskEvidence(Containing, false, OutError)
            || !SourcesRemainUnloaded())
        {
            if (OutError.IsEmpty())
            {
                OutError = TEXT(
                    "Creating the containing Level loaded a Level Instance source map.");
            }
            return false;
        }
        return true;
    }

    bool Activate(FString& OutError)
    {
        if (GEditor == nullptr || Containing.World == nullptr)
        {
            OutError = TEXT(
                "Cannot activate a null containing Level Instance fixture World.");
            return false;
        }
        FWorldContext& Context = GEditor->GetEditorWorldContext();
        if (!bEditorContextChanged)
        {
            OriginalEditorWorld = Context.World();
            OriginalGWorld = GWorld.GetReference();
            bEditorContextChanged = true;
        }
        Context.SetCurrentWorld(Containing.World);
        GWorld = Containing.World;
        if (Context.World() != Containing.World
            || GWorld.GetReference() != Containing.World)
        {
            OutError = TEXT(
                "UE failed to bind the containing Level Instance fixture to the Editor World context.");
            return false;
        }
        return true;
    }

    bool CaptureNoLoadState(FString& OutError)
    {
        OutError.Reset();
        if (Containing.World == nullptr
            || !SourcesRemainUnloaded()
            || HasLoadedSourceInstancePackage())
        {
            OutError = TEXT(
                "The Level Instance no-load fixture already contained loaded source or temporary instance packages.");
            return false;
        }
        LevelInstanceSubsystem =
            Containing.World->GetSubsystem<ULevelInstanceSubsystem>();
        if (LevelInstanceSubsystem == nullptr)
        {
            OutError = TEXT(
                "The containing World has no Level Instance subsystem.");
            return false;
        }
        LevelsBefore = Containing.World->GetLevels();
        LoadStates.Reset();
        for (ALevelInstance* Actor : {
                 SharedOne,
                 SharedTwo,
                 OtherSource,
                 MissingSource})
        {
            if (Actor == nullptr)
            {
                OutError = TEXT(
                    "The Level Instance read invariant received a null placement Actor.");
                return false;
            }
            FLevelInstanceLoadState& State =
                LoadStates.AddDefaulted_GetRef();
            State.Actor = Actor;
            State.SourcePath =
                Actor->GetWorldAsset().ToSoftObjectPath().ToString();
            State.ActorInstanceGuid = Actor->GetActorInstanceGuid();
            State.bLoaded = LevelInstanceSubsystem->IsLoaded(Actor);
            State.bLoading = LevelInstanceSubsystem->IsLoading(Actor);
            if (State.bLoaded
                || State.bLoading
                || State.ActorInstanceGuid != Actor->GetActorGuid())
            {
                OutError = TEXT(
                    "A Level Instance fixture placement was already loaded, loading, or projected before Query.");
                return false;
            }
        }
        bNoLoadStateCaptured = true;
        return true;
    }

    bool VerifyNoLoadState(FAutomationTestBase& Test) const
    {
        bool bOk = Test.TestTrue(
            TEXT("Level Instance Query keeps both source maps unloaded"),
            SourcesRemainUnloaded());
        bOk &= Test.TestFalse(
            TEXT("Level Instance Query creates no temporary source instance package"),
            HasLoadedSourceInstancePackage());
        bOk &= Test.TestTrue(
            TEXT("Level Instance no-load state was captured"),
            bNoLoadStateCaptured);
        if (!bNoLoadStateCaptured || Containing.World == nullptr)
        {
            return false;
        }
        bOk &= Test.TestEqual(
            TEXT("Level Instance Query preserves the World's Level list"),
            Containing.World->GetLevels(),
            LevelsBefore);
        bOk &= Test.TestEqual(
            TEXT("Level Instance Query preserves the native subsystem"),
            Containing.World->GetSubsystem<ULevelInstanceSubsystem>(),
            LevelInstanceSubsystem);
        for (const FLevelInstanceLoadState& State : LoadStates)
        {
            ALevelInstance* Actor = State.Actor.Get();
            bOk &= Test.TestNotNull(
                TEXT("Level Instance Query preserves each placement Actor"),
                Actor);
            if (Actor == nullptr || LevelInstanceSubsystem == nullptr)
            {
                continue;
            }
            bOk &= Test.TestEqual(
                TEXT("Level Instance Query preserves the saved source path"),
                Actor->GetWorldAsset().ToSoftObjectPath().ToString(),
                State.SourcePath);
            bOk &= Test.TestEqual(
                TEXT("Level Instance Query does not project ActorInstanceGuid"),
                Actor->GetActorInstanceGuid(),
                State.ActorInstanceGuid);
            bOk &= Test.TestEqual(
                TEXT("Level Instance Query does not load a placement"),
                LevelInstanceSubsystem->IsLoaded(Actor),
                State.bLoaded);
            bOk &= Test.TestEqual(
                TEXT("Level Instance Query does not queue a placement load"),
                LevelInstanceSubsystem->IsLoading(Actor),
                State.bLoading);
            bOk &= Test.TestNull(
                TEXT("Level Instance source SoftObjectPtr remains unresolved"),
                Actor->GetWorldAsset().Get());
        }
        return bOk;
    }

    bool SetSharedOneSource(
        const bool bUseSourceB,
        FString& OutError)
    {
        return SetSource(
            SharedOne,
            bUseSourceB ? SourceB.ObjectPath : SourceA.ObjectPath,
            OutError);
    }

    bool SourcesRemainUnloaded() const
    {
        return IsMapUnloaded(SourceA) && IsMapUnloaded(SourceB);
    }

    bool ExitActiveEdit(FString& OutError)
    {
        OutError.Reset();
        ULevelInstanceSubsystem* Subsystem = Containing.World != nullptr
            ? Containing.World->GetSubsystem<ULevelInstanceSubsystem>()
            : nullptr;
        ILevelInstanceInterface* Editing = Subsystem != nullptr
            ? Subsystem->GetEditingLevelInstance()
            : nullptr;
        if (Editing == nullptr)
        {
            return true;
        }

        if (GEditor != nullptr)
        {
            GEditor->SelectNone(false, true, false);
        }
        FText Reason;
        if (!Editing->CanExitEdit(true, &Reason)
            || !Editing->ExitEdit(true))
        {
            OutError = TEXT(
                "Level Instance fixture could not discard and exit its active edit: ")
                + Reason.ToString();
            return false;
        }
        if (GEditor != nullptr)
        {
            // CommitLevelInstanceInternal reselects the placement Actor.
            GEditor->SelectNone(false, true, false);
        }
        return true;
    }

    void AbandonDestructiveCleanup()
    {
        // A failed native edit exit leaves the Editor holding the composed
        // World. Preserve it until process shutdown instead of dereferencing
        // stale editor state by trying to destroy it from the fixture.
        bCleaned = true;
    }

    bool Cleanup(FString& OutError)
    {
        OutError.Reset();
        if (bCleaned)
        {
            return true;
        }

        if (!ExitActiveEdit(OutError))
        {
            return false;
        }
        bCleaned = true;

        if (bEditorContextChanged && GEditor != nullptr)
        {
            GEditor->GetEditorWorldContext().SetCurrentWorld(
                OriginalEditorWorld);
            GWorld = OriginalGWorld;
            bEditorContextChanged = false;
        }

        if (GEditor != nullptr)
        {
            // ExitEdit reselects the placement Actor. Never leave a selected
            // element pointing into a fixture World that is about to go away.
            GEditor->SelectNone(false, true, false);
        }

        IAssetRegistry& Registry =
            FModuleManager::LoadModuleChecked<FAssetRegistryModule>(
                TEXT("AssetRegistry"))
                .Get();
        for (FLevelMapRecord* Record : {
                 &SourceA,
                 &SourceB,
                 &Containing})
        {
            UWorld* World = Record->World;
            if (World == nullptr && !Record->ObjectPath.IsEmpty())
            {
                World = FindObject<UWorld>(
                    nullptr,
                    *Record->ObjectPath);
            }
            if (World != nullptr)
            {
                if (Record->bRegisteredInMemory)
                {
                    FAssetRegistryModule::AssetDeleted(World);
                    Record->bRegisteredInMemory = false;
                }
                UPackage* Package = World->GetOutermost();
                World->DestroyWorld(false);
                PrepareLevelPackageForCollection(Package);
                Record->World = nullptr;
            }
            else if (!Record->PackageName.IsEmpty())
            {
                if (UPackage* Package = FindPackage(
                        nullptr,
                        *Record->PackageName))
                {
                    PrepareLevelPackageForCollection(Package);
                }
            }
        }

        Ordinary = nullptr;
        SharedOne = nullptr;
        SharedTwo = nullptr;
        OtherSource = nullptr;
        MissingSource = nullptr;
        LevelInstanceSubsystem = nullptr;
        LoadStates.Reset();
        LevelsBefore.Reset();
        CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);

        TArray<FString> Filenames;
        for (const FLevelMapRecord* Record : {
                 &SourceA,
                 &SourceB,
                 &Containing})
        {
            if (!Record->Filename.IsEmpty())
            {
                IFileManager::Get().Delete(
                    *Record->Filename,
                    false,
                    true,
                    true);
                Filenames.Add(Record->Filename);
            }
            if (!Record->PackageName.IsEmpty()
                && FindPackage(nullptr, *Record->PackageName) != nullptr
                && OutError.IsEmpty())
            {
                OutError = TEXT(
                    "Level Instance fixture package remained loaded during cleanup: ")
                    + Record->PackageName;
            }
        }
        if (!Filenames.IsEmpty())
        {
            Registry.ScanModifiedAssetFiles(Filenames);
        }
        return OutError.IsEmpty();
    }

    FLevelMapRecord SourceA;
    FLevelMapRecord SourceB;
    FLevelMapRecord Containing;
    AActor* Ordinary = nullptr;
    ALevelInstance* SharedOne = nullptr;
    ALevelInstance* SharedTwo = nullptr;
    ALevelInstance* OtherSource = nullptr;
    ALevelInstance* MissingSource = nullptr;
    FGuid OrdinaryId;
    FGuid SharedOneId;
    FGuid SharedTwoId;
    FGuid OtherSourceId;
    FGuid MissingSourceId;
    const FName EditActorOneName{TEXT("Actor_EditOne")};
    const FName EditActorTwoName{TEXT("Actor_EditTwo")};

private:
    static UWorld::InitializationValues FixtureInitializationValues()
    {
        return UWorld::InitializationValues()
            .RequiresHitProxies(false)
            .ShouldSimulatePhysics(false)
            .EnableTraceCollision(false)
            .CreateNavigation(false)
            .CreateAISystem(false)
            .AllowAudioPlayback(false)
            .CreatePhysicsScene(false);
    }

    bool CreateMap(
        const FString& AssetName,
        FLevelMapRecord& OutRecord,
        FString& OutError)
    {
        OutRecord.PackageName = RootPackagePath + TEXT("/") + AssetName;
        OutRecord.ObjectPath = OutRecord.PackageName
            + TEXT(".")
            + AssetName;
        OutRecord.Filename = FPackageName::LongPackageNameToFilename(
            OutRecord.PackageName,
            FPackageName::GetMapPackageExtension());
        IFileManager::Get().MakeDirectory(
            *FPaths::GetPath(OutRecord.Filename),
            true);

        UPackage* Package = CreatePackage(*OutRecord.PackageName);
        const UWorld::InitializationValues InitValues =
            FixtureInitializationValues();
        OutRecord.World = UWorld::CreateWorld(
            EWorldType::Editor,
            false,
            FName(*AssetName),
            Package,
            false,
            ERHIFeatureLevel::Num,
            &InitValues);
        if (OutRecord.World == nullptr
            || OutRecord.World->PersistentLevel == nullptr
            || OutRecord.World->GetPathName() != OutRecord.ObjectPath)
        {
            OutError = TEXT(
                "UE failed to create an exact Level Instance fixture World at ")
                + OutRecord.ObjectPath;
            return false;
        }
        OutRecord.World->SetFlags(
            RF_Public | RF_Standalone | RF_Transactional);
        FAssetRegistryModule::AssetCreated(OutRecord.World);
        OutRecord.bRegisteredInMemory = true;
        OutRecord.World->GetOutermost()->SetDirtyFlag(false);
        return true;
    }

    static AActor* SpawnActor(
        UWorld* World,
        const FString& Name,
        const FString& Label,
        FString& OutError)
    {
        FActorSpawnParameters Params;
        Params.Name = FName(*Name);
        Params.OverrideLevel = World != nullptr
            ? World->PersistentLevel
            : nullptr;
        Params.ObjectFlags = RF_Transactional;
        AActor* Actor = World != nullptr
            ? World->SpawnActor<AActor>(
                AActor::StaticClass(),
                FTransform::Identity,
                Params)
            : nullptr;
        if (Actor == nullptr)
        {
            OutError = TEXT(
                "UE failed to spawn Level Instance fixture Actor ")
                + Name;
            return nullptr;
        }
        Actor->SetActorLabel(Label, false);
        return Actor;
    }

    static ALevelInstance* SpawnLevelInstance(
        UWorld* World,
        const FString& Name,
        const FString& Label,
        FString& OutError)
    {
        FActorSpawnParameters Params;
        Params.Name = FName(*Name);
        Params.OverrideLevel = World != nullptr
            ? World->PersistentLevel
            : nullptr;
        Params.ObjectFlags = RF_Transactional;
        ALevelInstance* Actor = World != nullptr
            ? World->SpawnActor<ALevelInstance>(
                ALevelInstance::StaticClass(),
                FTransform::Identity,
                Params)
            : nullptr;
        if (Actor == nullptr)
        {
            OutError = TEXT(
                "UE failed to spawn Level Instance placement ")
                + Name;
            return nullptr;
        }
        Actor->SetActorLabel(Label, false);
        return Actor;
    }

    static bool SetSource(
        ALevelInstance* Actor,
        const FString& SourceObjectPath,
        FString& OutError)
    {
        const TSoftObjectPtr<UWorld> Source{
            FSoftObjectPath(SourceObjectPath)};
        if (Actor == nullptr
            || SourceObjectPath.IsEmpty()
            || !Actor->SetWorldAsset(Source)
            || Actor->GetWorldAsset().ToSoftObjectPath().ToString()
                != SourceObjectPath
            || Actor->GetWorldAsset().Get() != nullptr)
        {
            OutError = TEXT(
                "UE failed to retain an unloaded saved source on a Level Instance placement: ")
                + SourceObjectPath;
            return false;
        }
        return true;
    }

    static bool SaveMap(
        FLevelMapRecord& Record,
        FString& OutError)
    {
        if (Record.World == nullptr)
        {
            OutError = TEXT(
                "Cannot save a null Level Instance fixture World.");
            return false;
        }
        UPackage* Package = Record.World->GetOutermost();
        Package->SetDirtyFlag(true);
        Package->FullyLoad();
        FSavePackageArgs SaveArgs;
        SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
        SaveArgs.Error = GLog;
        if (!UPackage::SavePackage(
                Package,
                Record.World,
                *Record.Filename,
                SaveArgs))
        {
            OutError = TEXT(
                "UE failed to save Level Instance fixture ")
                + Record.ObjectPath;
            return false;
        }
        Package->SetDirtyFlag(false);
        FModuleManager::LoadModuleChecked<FAssetRegistryModule>(
            TEXT("AssetRegistry"))
            .Get()
            .ScanModifiedAssetFiles({Record.Filename});
        return true;
    }

    static bool UnloadMap(
        FLevelMapRecord& Record,
        FString& OutError)
    {
        if (Record.World == nullptr)
        {
            OutError = TEXT(
                "Cannot unload a null Level Instance fixture World.");
            return false;
        }
        UPackage* Package = Record.World->GetOutermost();
        if (Record.bRegisteredInMemory)
        {
            FAssetRegistryModule::AssetDeleted(Record.World);
            Record.bRegisteredInMemory = false;
        }
        Record.World->DestroyWorld(false);
        PrepareLevelPackageForCollection(Package);
        Record.World = nullptr;
        CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);
        FModuleManager::LoadModuleChecked<FAssetRegistryModule>(
            TEXT("AssetRegistry"))
            .Get()
            .ScanModifiedAssetFiles({Record.Filename});
        if (!IsMapUnloaded(Record))
        {
            OutError = TEXT(
                "Saved Level Instance source fixture remained loaded: ")
                + Record.ObjectPath;
            return false;
        }
        return true;
    }

    static bool IsMapUnloaded(const FLevelMapRecord& Record)
    {
        return !Record.PackageName.IsEmpty()
            && !Record.ObjectPath.IsEmpty()
            && FindPackage(nullptr, *Record.PackageName) == nullptr
            && FindObject<UWorld>(nullptr, *Record.ObjectPath) == nullptr;
    }

    static bool ValidateOnDiskEvidence(
        const FLevelMapRecord& Record,
        const bool bExpectUnloaded,
        FString& OutError)
    {
        IAssetRegistry& Registry =
            FModuleManager::LoadModuleChecked<FAssetRegistryModule>(
                TEXT("AssetRegistry"))
                .Get();
        const FAssetData Data = Registry.GetAssetByObjectPath(
            FSoftObjectPath(Record.ObjectPath),
            true);
        if (!Data.IsValid()
            || Data.AssetClassPath
                != UWorld::StaticClass()->GetClassPathName())
        {
            OutError = TEXT(
                "Asset Registry lacks on-disk UWorld evidence for Level Instance fixture ")
                + Record.ObjectPath;
            return false;
        }
        if (bExpectUnloaded
            && (Data.FastGetAsset(false) != nullptr
                || !IsMapUnloaded(Record)))
        {
            OutError = TEXT(
                "On-disk Level Instance source fixture unexpectedly remained loaded: ")
                + Record.ObjectPath;
            return false;
        }
        return true;
    }

    bool HasLoadedSourceInstancePackage() const
    {
        TArray<FString> Prefixes;
        for (const FLevelMapRecord* Record : {&SourceA, &SourceB})
        {
            Prefixes.Add(
                TEXT("/Temp")
                + FPackageName::GetLongPackagePath(Record->PackageName)
                + TEXT("/")
                + FPackageName::GetShortName(Record->PackageName)
                + TEXT("_LevelInstance_"));
        }
        for (TObjectIterator<UPackage> It; It; ++It)
        {
            const FString PackageName = It->GetName();
            for (const FString& Prefix : Prefixes)
            {
                if (PackageName.StartsWith(Prefix))
                {
                    return true;
                }
            }
        }
        return false;
    }

    FString Token;
    FString RootPackagePath;
    bool bCleaned = false;
    UWorld* OriginalEditorWorld = nullptr;
    UWorld* OriginalGWorld = nullptr;
    bool bEditorContextChanged = false;
    bool bNoLoadStateCaptured = false;
    ULevelInstanceSubsystem* LevelInstanceSubsystem = nullptr;
    TArray<ULevel*> LevelsBefore;
    TArray<FLevelInstanceLoadState> LoadStates;
};

struct FFixtureActorState
{
    TWeakObjectPtr<AActor> Actor;
    FGuid Guid;
    FString Label;
    FTransform Transform;
};

class FLevelReadInvariant
{
public:
    explicit FLevelReadInvariant(UWorld* InWorld)
        : World(InWorld)
        , GWorldBefore(GWorld.GetReference())
        , EditorWorldBefore(
            GEditor != nullptr
                ? GEditor->GetEditorWorldContext().World()
                : nullptr)
        , CurrentLevelBefore(
            InWorld != nullptr ? InWorld->GetCurrentLevel() : nullptr)
        , PackageDirtyBefore(
            InWorld != nullptr && InWorld->GetOutermost()->IsDirty())
        , SelectedActorCountBefore(
            GEditor != nullptr ? GEditor->GetSelectedActorCount() : -1)
        , SelectedComponentCountBefore(
            GEditor != nullptr ? GEditor->GetSelectedComponentCount() : -1)
        , UndoCountBefore(
            GEditor != nullptr && GEditor->Trans != nullptr
                ? GEditor->Trans->GetUndoCount()
                : -1)
        , QueueLengthBefore(
            GEditor != nullptr && GEditor->Trans != nullptr
                ? GEditor->Trans->GetQueueLength()
                : -1)
    {
        if (World != nullptr && World->PersistentLevel != nullptr)
        {
            for (AActor* Actor : World->PersistentLevel->Actors)
            {
                if (Actor == nullptr)
                {
                    continue;
                }
                FFixtureActorState& State = ActorStates.AddDefaulted_GetRef();
                State.Actor = Actor;
                State.Guid = Actor->GetActorGuid();
                State.Label = Actor->GetActorLabel();
                State.Transform = Actor->GetActorTransform();
            }
        }
        TransactionHandle = FCoreUObjectDelegates::OnObjectTransacted.AddLambda(
            [this](UObject*, const FTransactionObjectEvent&)
            {
                ++TransactionEventCount;
            });
    }

    ~FLevelReadInvariant()
    {
        FCoreUObjectDelegates::OnObjectTransacted.Remove(TransactionHandle);
    }

    bool Verify(FAutomationTestBase& Test) const
    {
        bool bOk = true;
        bOk &= Test.TestEqual(
            TEXT("Level Query preserves GWorld"),
            GWorld.GetReference(),
            GWorldBefore);
        if (GEditor != nullptr)
        {
            bOk &= Test.TestEqual(
                TEXT("Level Query preserves the Editor World context"),
                GEditor->GetEditorWorldContext().World(),
                EditorWorldBefore);
            bOk &= Test.TestEqual(
                TEXT("Level Query preserves Actor selection"),
                GEditor->GetSelectedActorCount(),
                SelectedActorCountBefore);
            bOk &= Test.TestEqual(
                TEXT("Level Query preserves Component selection"),
                GEditor->GetSelectedComponentCount(),
                SelectedComponentCountBefore);
            if (GEditor->Trans != nullptr)
            {
                bOk &= Test.TestEqual(
                    TEXT("Level Query creates no Undo entry"),
                    GEditor->Trans->GetUndoCount(),
                    UndoCountBefore);
                bOk &= Test.TestEqual(
                    TEXT("Level Query preserves the transaction queue"),
                    GEditor->Trans->GetQueueLength(),
                    QueueLengthBefore);
            }
        }
        bOk &= Test.TestEqual(
            TEXT("Level Query emits no UObject transaction"),
            TransactionEventCount,
            0);
        if (World != nullptr)
        {
            bOk &= Test.TestEqual(
                TEXT("Level Query preserves the World's current Level"),
                World->GetCurrentLevel(),
                CurrentLevelBefore);
            bOk &= Test.TestEqual(
                TEXT("Level Query preserves the map package dirty state"),
                World->GetOutermost()->IsDirty(),
                PackageDirtyBefore);
        }
        for (const FFixtureActorState& State : ActorStates)
        {
            AActor* Actor = State.Actor.Get();
            bOk &= Test.TestNotNull(
                TEXT("Level Query preserves each source Actor"),
                Actor);
            if (Actor != nullptr)
            {
                bOk &= Test.TestEqual(
                    TEXT("Level Query preserves ActorGuid"),
                    Actor->GetActorGuid(),
                    State.Guid);
                bOk &= Test.TestEqual(
                    TEXT("Level Query preserves ActorLabel"),
                    Actor->GetActorLabel(),
                    State.Label);
                bOk &= Test.TestTrue(
                    TEXT("Level Query preserves Actor transform"),
                    Actor->GetActorTransform().Equals(State.Transform));
            }
        }
        return bOk;
    }

private:
    UWorld* World = nullptr;
    UWorld* GWorldBefore = nullptr;
    UWorld* EditorWorldBefore = nullptr;
    ULevel* CurrentLevelBefore = nullptr;
    bool PackageDirtyBefore = false;
    int32 SelectedActorCountBefore = -1;
    int32 SelectedComponentCountBefore = -1;
    int32 UndoCountBefore = -1;
    int32 QueueLengthBefore = -1;
    TArray<FFixtureActorState> ActorStates;
    FDelegateHandle TransactionHandle;
    int32 TransactionEventCount = 0;
};

struct FFixtureComponentState
{
    TWeakObjectPtr<UActorComponent> Component;
    FName Name;
    TWeakObjectPtr<UObject> Outer;
    TWeakObjectPtr<AActor> Owner;
    EComponentCreationMethod CreationMethod =
        EComponentCreationMethod::Native;
    bool bRegistered = false;
    bool bHasBeenCreated = false;
};

class FLevelComponentReadInvariant
{
public:
    FLevelComponentReadInvariant(
        AActor* InActor,
        UPackage* InBlueprintPackage)
        : Actor(InActor)
        , BlueprintPackage(InBlueprintPackage)
        , ActorPackageDirtyBefore(
            InActor != nullptr
                && InActor->GetOutermost()->IsDirty())
        , BlueprintPackageDirtyBefore(
            InBlueprintPackage != nullptr
                && InBlueprintPackage->IsDirty())
    {
        if (Actor == nullptr)
        {
            return;
        }
        TArray<UActorComponent*> Components;
        Actor->GetComponents(Components, false);
        for (UActorComponent* Component : Components)
        {
            if (Component == nullptr)
            {
                continue;
            }
            FFixtureComponentState& State =
                ComponentStates.AddDefaulted_GetRef();
            State.Component = Component;
            State.Name = Component->GetFName();
            State.Outer = Component->GetOuter();
            State.Owner = Component->GetOwner();
            State.CreationMethod = Component->CreationMethod;
            State.bRegistered = Component->IsRegistered();
            State.bHasBeenCreated = Component->HasBeenCreated();
        }
        for (UActorComponent* Component : Actor->GetInstanceComponents())
        {
            InstanceComponentsBefore.Add(Component);
        }
        for (UActorComponent* Component : Actor->BlueprintCreatedComponents)
        {
            BlueprintCreatedComponentsBefore.Add(Component);
        }
    }

    bool Verify(FAutomationTestBase& Test) const
    {
        bool bOk = true;
        bOk &= Test.TestNotNull(
            TEXT("Component Query preserves its owner Actor"),
            Actor);
        if (Actor == nullptr)
        {
            return false;
        }

        TArray<UActorComponent*> ComponentsAfter;
        Actor->GetComponents(ComponentsAfter, false);
        bOk &= Test.TestEqual(
            TEXT("Component Query preserves the exact owned Component count"),
            ComponentsAfter.Num(),
            ComponentStates.Num());
        for (const FFixtureComponentState& State : ComponentStates)
        {
            UActorComponent* Component = State.Component.Get();
            bOk &= Test.TestNotNull(
                TEXT("Component Query preserves each Component incarnation"),
                Component);
            if (Component == nullptr)
            {
                continue;
            }
            bOk &= Test.TestTrue(
                TEXT("Component Query preserves owned Component membership"),
                ComponentsAfter.Contains(Component));
            bOk &= Test.TestEqual(
                TEXT("Component Query preserves Component FName"),
                Component->GetFName(),
                State.Name);
            bOk &= Test.TestEqual(
                TEXT("Component Query preserves Component Outer"),
                Component->GetOuter(),
                State.Outer.Get());
            bOk &= Test.TestEqual(
                TEXT("Component Query preserves Component owner"),
                Component->GetOwner(),
                State.Owner.Get());
            bOk &= Test.TestTrue(
                TEXT("Component Query preserves CreationMethod"),
                Component->CreationMethod == State.CreationMethod);
            bOk &= Test.TestEqual(
                TEXT("Component Query preserves registration state"),
                Component->IsRegistered(),
                State.bRegistered);
            bOk &= Test.TestEqual(
                TEXT("Component Query preserves creation lifecycle state"),
                Component->HasBeenCreated(),
                State.bHasBeenCreated);
        }

        const TArray<UActorComponent*>& InstanceComponentsAfter =
            Actor->GetInstanceComponents();
        bOk &= Test.TestEqual(
            TEXT("Component Query preserves InstanceComponents count"),
            InstanceComponentsAfter.Num(),
            InstanceComponentsBefore.Num());
        for (int32 Index = 0;
             Index < FMath::Min(
                 InstanceComponentsAfter.Num(),
                 InstanceComponentsBefore.Num());
             ++Index)
        {
            bOk &= Test.TestEqual(
                TEXT("Component Query preserves InstanceComponents order"),
                InstanceComponentsAfter[Index],
                InstanceComponentsBefore[Index].Get());
        }

        bOk &= Test.TestEqual(
            TEXT("Component Query preserves BlueprintCreatedComponents count"),
            Actor->BlueprintCreatedComponents.Num(),
            BlueprintCreatedComponentsBefore.Num());
        for (int32 Index = 0;
             Index < FMath::Min(
                 Actor->BlueprintCreatedComponents.Num(),
                 BlueprintCreatedComponentsBefore.Num());
             ++Index)
        {
            bOk &= Test.TestEqual(
                TEXT("Component Query preserves Blueprint-created Component order"),
                Actor->BlueprintCreatedComponents[Index].Get(),
                BlueprintCreatedComponentsBefore[Index].Get());
        }

        bOk &= Test.TestEqual(
            TEXT("Component Query preserves the map package dirty state"),
            Actor->GetOutermost()->IsDirty(),
            ActorPackageDirtyBefore);
        if (BlueprintPackage != nullptr)
        {
            bOk &= Test.TestEqual(
                TEXT("Component Query preserves the declaring Blueprint package dirty state"),
                BlueprintPackage->IsDirty(),
                BlueprintPackageDirtyBefore);
        }
        return bOk;
    }

private:
    AActor* Actor = nullptr;
    UPackage* BlueprintPackage = nullptr;
    bool ActorPackageDirtyBefore = false;
    bool BlueprintPackageDirtyBefore = false;
    TArray<FFixtureComponentState> ComponentStates;
    TArray<TWeakObjectPtr<UActorComponent>> InstanceComponentsBefore;
    TArray<TWeakObjectPtr<UActorComponent>>
        BlueprintCreatedComponentsBefore;
};

struct FFixturePCGComponentState
{
    TWeakObjectPtr<UPCGComponent> Component;
    TWeakObjectPtr<UPCGGraphInstance> GraphInstance;
    TWeakObjectPtr<UPCGGraphInterface> GraphInterface;
    TWeakObjectPtr<UPCGGraph> Graph;
    bool bRegistered = false;
    bool bGenerating = false;
    bool bCleaningUp = false;
    FPCGTaskId GenerationTask = InvalidPCGTaskId;
    int32 ManagedResourceCount = 0;
};

struct FFixturePCGGraphLinkState
{
    TWeakObjectPtr<UPCGGraphInstance> Instance;
    TWeakObjectPtr<UPCGGraphInterface> Parent;
};

int32 CountPCGManagedResources(UPCGComponent* Component)
{
    int32 Count = 0;
    if (Component != nullptr)
    {
        Component->ForEachManagedResource(
            [&Count](UPCGManagedResource*)
            {
                ++Count;
            });
    }
    return Count;
}

class FPCGComponentReadInvariant
{
public:
    explicit FPCGComponentReadInvariant(
        const TArray<UPCGComponent*>& Components)
    {
        for (UPCGComponent* Component : Components)
        {
            if (Component == nullptr)
            {
                continue;
            }
            FFixturePCGComponentState& State =
                States.AddDefaulted_GetRef();
            State.Component = Component;
            State.GraphInstance = Component->GetGraphInstance();
            State.GraphInterface = Component->GetGraphInstance() != nullptr
                ? Component->GetGraphInstance()->Graph.Get()
                : nullptr;
            State.Graph = Component->GetGraph();
            State.bRegistered = Component->IsRegistered();
            State.bGenerating = Component->IsGenerating();
            State.bCleaningUp = Component->IsCleaningUp();
            State.GenerationTask = Component->GetGenerationTaskId();
            State.ManagedResourceCount =
                CountPCGManagedResources(Component);
            RelevantObjects.Add(Component);
            if (UPCGGraphInstance* GraphInstance =
                    Component->GetGraphInstance())
            {
                RelevantObjects.Add(GraphInstance);
            }
            TSet<UPCGGraphInstance*> SeenGraphInstances;
            UPCGGraphInterface* CurrentInterface =
                State.GraphInterface.Get();
            while (UPCGGraphInstance* CurrentInstance =
                       Cast<UPCGGraphInstance>(CurrentInterface))
            {
                if (SeenGraphInstances.Contains(CurrentInstance))
                {
                    break;
                }
                SeenGraphInstances.Add(CurrentInstance);
                FFixturePCGGraphLinkState& Link =
                    GraphLinks.AddDefaulted_GetRef();
                Link.Instance = CurrentInstance;
                Link.Parent = CurrentInstance->Graph.Get();
                RelevantObjects.Add(CurrentInstance);
                CurrentInterface = Link.Parent.Get();
            }
            if (UPCGGraph* Graph = Component->GetGraph())
            {
                RelevantObjects.Add(Graph);
            }
            UPackage* Package = Component->GetOutermost();
            if (Package != nullptr && !PackageDirtyBefore.Contains(Package))
            {
                PackageDirtyBefore.Add(Package, Package->IsDirty());
            }
            UPackage* GraphPackage = Component->GetGraph() != nullptr
                ? Component->GetGraph()->GetOutermost()
                : nullptr;
            if (GraphPackage != nullptr
                && !PackageDirtyBefore.Contains(GraphPackage))
            {
                PackageDirtyBefore.Add(
                    GraphPackage,
                    GraphPackage->IsDirty());
            }
        }
        ObjectModifiedHandle =
            FCoreUObjectDelegates::OnObjectModified.AddLambda(
                [this](UObject* Object)
                {
                    if (RelevantObjects.Contains(Object))
                    {
                        ++RelevantObjectModifiedCount;
                    }
                });
    }

    ~FPCGComponentReadInvariant()
    {
        FCoreUObjectDelegates::OnObjectModified.Remove(
            ObjectModifiedHandle);
    }

    bool Verify(FAutomationTestBase& Test) const
    {
        bool bOk = true;
        bOk &= Test.TestEqual(
            TEXT("PCG Component Query emits no relevant UObject modification"),
            RelevantObjectModifiedCount,
            0);
        for (const FFixturePCGComponentState& State : States)
        {
            UPCGComponent* Component = State.Component.Get();
            bOk &= Test.TestNotNull(
                TEXT("PCG Component Query preserves the source Component"),
                Component);
            if (Component == nullptr)
            {
                continue;
            }
            bOk &= Test.TestEqual(
                TEXT("PCG Component Query preserves the owned GraphInstance"),
                Component->GetGraphInstance(),
                State.GraphInstance.Get());
            bOk &= Test.TestEqual(
                TEXT("PCG Component Query preserves the direct Graph interface"),
                Component->GetGraphInstance() != nullptr
                    ? Component->GetGraphInstance()->Graph.Get()
                    : nullptr,
                State.GraphInterface.Get());
            bOk &= Test.TestEqual(
                TEXT("PCG Component Query preserves the top Graph"),
                Component->GetGraph(),
                State.Graph.Get());
            bOk &= Test.TestEqual(
                TEXT("PCG Component Query preserves registration"),
                Component->IsRegistered(),
                State.bRegistered);
            bOk &= Test.TestEqual(
                TEXT("PCG Component Query does not start generation"),
                Component->IsGenerating(),
                State.bGenerating);
            bOk &= Test.TestEqual(
                TEXT("PCG Component Query does not start cleanup"),
                Component->IsCleaningUp(),
                State.bCleaningUp);
            bOk &= Test.TestEqual(
                TEXT("PCG Component Query preserves the generation task id"),
                Component->GetGenerationTaskId(),
                State.GenerationTask);
            bOk &= Test.TestEqual(
                TEXT("PCG Component Query preserves managed resources"),
                CountPCGManagedResources(Component),
                State.ManagedResourceCount);
        }
        for (const FFixturePCGGraphLinkState& Link : GraphLinks)
        {
            UPCGGraphInstance* Instance = Link.Instance.Get();
            bOk &= Test.TestNotNull(
                TEXT("PCG Component Query preserves every loaded GraphInstance link"),
                Instance);
            if (Instance != nullptr)
            {
                bOk &= Test.TestEqual(
                    TEXT("PCG Component Query preserves every loaded GraphInterface parent"),
                    Instance->Graph.Get(),
                    Link.Parent.Get());
            }
        }
        for (const TPair<UPackage*, bool>& Entry : PackageDirtyBefore)
        {
            bOk &= Test.TestEqual(
                TEXT("PCG Component Query preserves every persistence package dirty flag"),
                Entry.Key->IsDirty(),
                Entry.Value);
        }
        return bOk;
    }

private:
    TArray<FFixturePCGComponentState> States;
    TArray<FFixturePCGGraphLinkState> GraphLinks;
    TSet<UObject*> RelevantObjects;
    TMap<UPackage*, bool> PackageDirtyBefore;
    FDelegateHandle ObjectModifiedHandle;
    int32 RelevantObjectModifiedCount = 0;
};

class FScopedPCGComponentParameterFixture
{
public:
    bool Build(
        FScopedLevelQueryFixture& LevelFixture,
        FScopedLevelComponentQueryFixture& ComponentFixture,
        FString& OutError)
    {
        OutError.Reset();
        Graph = ComponentFixture.PCGComponent != nullptr
            ? ComponentFixture.PCGComponent->GetGraph()
            : nullptr;
        if (Graph == nullptr
            || ComponentFixture.Actor == nullptr
            || ComponentFixture.PCGComponent == nullptr)
        {
            OutError = TEXT(
                "PCG Parameter fixture requires the unregistered instance "
                "Component and its already-loaded Graph.");
            return false;
        }

        TArray<FPropertyBagPropertyDesc> Descriptors;
        Descriptors.Reserve(11);
        const auto AddDescriptor = [&Descriptors](
            const FName Name,
            const FGuid& Id,
            const EPropertyBagPropertyType ValueType,
            const UObject* ValueTypeObject = nullptr)
        {
            FPropertyBagPropertyDesc Desc(
                Name,
                ValueType,
                ValueTypeObject);
            Desc.ID = Id;
            Descriptors.Add(MoveTemp(Desc));
        };
        AddDescriptor(
            GraphDefaultName,
            GraphDefaultId,
            EPropertyBagPropertyType::Double);
        AddDescriptor(
            ParentOverrideName,
            ParentOverrideId,
            EPropertyBagPropertyType::Int64);
        AddDescriptor(
            RenamedFromName,
            ComponentOverrideId,
            EPropertyBagPropertyType::String);
        AddDescriptor(
            SoftObjectName,
            SoftObjectId,
            EPropertyBagPropertyType::SoftObject,
            UWorld::StaticClass());
        AddDescriptor(
            UnsupportedStructName,
            UnsupportedStructId,
            EPropertyBagPropertyType::Struct,
            TBaseStructure<FVector>::Get());
        FPropertyBagPropertyDesc ArrayDesc(
            UnsupportedArrayName,
            EPropertyBagContainerType::Array,
            EPropertyBagPropertyType::Double);
        ArrayDesc.ID = UnsupportedArrayId;
        Descriptors.Add(MoveTemp(ArrayDesc));
#if UE_VERSION_NEWER_THAN_OR_EQUAL(5, 8, 0)
        AddDescriptor(
            UnsupportedInt8Name,
            UnsupportedInt8Id,
            EPropertyBagPropertyType::Int8);
        AddDescriptor(
            UnsupportedInt16Name,
            UnsupportedInt16Id,
            EPropertyBagPropertyType::Int16);
        AddDescriptor(
            UnsupportedUInt16Name,
            UnsupportedUInt16Id,
            EPropertyBagPropertyType::UInt16);
#endif
        AddDescriptor(
            EnumName,
            EnumId,
            EPropertyBagPropertyType::Enum,
            StaticEnum<EPropertyBagPropertyType>());
        AddDescriptor(
            RemovedName,
            RemovedId,
            EPropertyBagPropertyType::Double);

        if (Graph->AddUserParameters(Descriptors)
                != EPropertyBagAlterationResult::Success
            || Graph->SetGraphParameter<double>(
                    GraphDefaultName,
                    GraphDefaultValue)
                != EPropertyBagResult::Success
            || Graph->SetGraphParameter<int64>(
                    ParentOverrideName,
                    GraphParentDefaultValue)
                != EPropertyBagResult::Success
            || Graph->SetGraphParameter<FString>(
                    RenamedFromName,
                    GraphComponentDefaultValue)
                != EPropertyBagResult::Success
            || Graph->SetGraphParameter<FSoftObjectPath>(
                    SoftObjectName,
                    FSoftObjectPath(LevelFixture.Unloaded.ObjectPath))
                != EPropertyBagResult::Success
            || Graph->SetGraphParameter<FVector>(
                    UnsupportedStructName,
                    FVector(1.0, 2.0, 3.0))
                != EPropertyBagResult::Success
            || Graph->SetGraphParameter(
                    EnumName,
                    static_cast<uint64>(EPropertyBagPropertyType::Double),
                    StaticEnum<EPropertyBagPropertyType>())
                != EPropertyBagResult::Success
            || Graph->SetGraphParameter<double>(
                    RemovedName,
                    RemovedValue)
                != EPropertyBagResult::Success)
        {
            OutError = TEXT(
                "UE failed to create the fixed-Guid PCG Parameter declarations "
                "and Graph defaults.");
            return false;
        }

        ParentGraphInstance = NewObject<UPCGGraphInstance>(
            ComponentFixture.Actor,
            UPCGGraphInstance::StaticClass(),
            FName(TEXT("LoomleParameterParentGraphInstance")),
            RF_Transactional);
        if (ParentGraphInstance == nullptr)
        {
            OutError = TEXT(
                "UE failed to create the external parent GraphInstance.");
            return false;
        }
        ParentGraphInstance->SetGraph(Graph);
        if (ParentGraphInstance->SetGraphParameter<int64>(
                ParentOverrideName,
                ParentOverrideValue)
            != EPropertyBagResult::Success)
        {
            OutError = TEXT(
                "UE failed to author the parent GraphInstance override.");
            return false;
        }

        Component = ComponentFixture.PCGComponent;
        Component->SetGraphLocal(ParentGraphInstance);
        OwnedGraphInstance = Component->GetGraphInstance();
        if (OwnedGraphInstance == nullptr
            || OwnedGraphInstance->SetGraphParameter<FString>(
                    RenamedFromName,
                    ComponentOverrideValue)
                != EPropertyBagResult::Success)
        {
            OutError = TEXT(
                "UE failed to author the Component-owned GraphInstance override.");
            return false;
        }

        if (Graph->RenameUserParameter(
                RenamedFromName,
                ComponentOverrideName)
            != EPropertyBagAlterationResult::Success)
        {
            OutError = TEXT(
                "UE failed to propagate a same-Guid Parameter rename.");
            return false;
        }
        EPropertyBagAlterationResult RemoveResult =
            EPropertyBagAlterationResult::InternalError;
        Graph->UpdateUserParametersStruct(
            [this, &RemoveResult](FInstancedPropertyBag& Bag)
            {
                RemoveResult = Bag.RemovePropertyByName(RemovedName);
            });
        if (RemoveResult != EPropertyBagAlterationResult::Success)
        {
            OutError = TEXT(
                "UE failed to propagate a removed Parameter declaration.");
            return false;
        }

        DeepComponent = NewObject<UPCGComponent>(
            ComponentFixture.Actor,
            UPCGComponent::StaticClass(),
            FName(TEXT("LoomleDeepParameterPCGComponent")),
            RF_Transactional);
        if (DeepComponent == nullptr)
        {
            OutError = TEXT(
                "UE failed to create the bounded Parameter-chain Component.");
            return false;
        }
        ComponentFixture.Actor->AddInstanceComponent(DeepComponent);
        DeepComponent->OnComponentCreated();
        constexpr int32 DeepGraphInstanceCount = 32;
        DeepGraphInstances.Reserve(DeepGraphInstanceCount);
        for (int32 Index = 0; Index < DeepGraphInstanceCount; ++Index)
        {
            DeepGraphInstances.Add(NewObject<UPCGGraphInstance>(
                ComponentFixture.Actor,
                UPCGGraphInstance::StaticClass(),
                FName(*FString::Printf(
                    TEXT("LoomleParameterDeepGraphInstance_%02d"),
                    Index)),
                RF_Transactional));
        }
        if (DeepGraphInstances.Contains(nullptr))
        {
            OutError = TEXT(
                "UE failed to create the bounded GraphInstance chain.");
            return false;
        }
        for (int32 Index = DeepGraphInstances.Num() - 1;
             Index >= 0;
             --Index)
        {
            UPCGGraphInterface* Parent = Graph;
            if (Index + 1 < DeepGraphInstances.Num())
            {
                Parent = DeepGraphInstances[Index + 1];
            }
            DeepGraphInstances[Index]->SetGraph(Parent);
        }
        DeepComponent->SetGraphLocal(DeepGraphInstances[0]);

        const FInstancedPropertyBag* GraphBag =
            Graph->GetUserParametersStruct();
        const FInstancedPropertyBag* ParentBag =
            ParentGraphInstance->GetUserParametersStruct();
        const FInstancedPropertyBag* OwnedBag =
            OwnedGraphInstance->GetUserParametersStruct();
        const FPropertyBagPropertyDesc* GraphDefaultOwnedDesc =
            OwnedBag != nullptr
            ? OwnedBag->FindPropertyDescByID(GraphDefaultId)
            : nullptr;
        const FPropertyBagPropertyDesc* ParentDesc =
            ParentBag != nullptr
            ? ParentBag->FindPropertyDescByID(ParentOverrideId)
            : nullptr;
        const FPropertyBagPropertyDesc* OwnedDesc =
            OwnedBag != nullptr
            ? OwnedBag->FindPropertyDescByID(ComponentOverrideId)
            : nullptr;
        const FPropertyBagPropertyDesc* RenamedGraphDesc =
            GraphBag != nullptr
            ? GraphBag->FindPropertyDescByID(ComponentOverrideId)
            : nullptr;
#if UE_VERSION_NEWER_THAN_OR_EQUAL(5, 8, 0)
        constexpr int32 ExpectedLiveParameterCount = 10;
#else
        constexpr int32 ExpectedLiveParameterCount = 7;
#endif
        if (GraphBag == nullptr
            || ParentBag == nullptr
            || OwnedBag == nullptr
            || GraphBag->GetPropertyBagStruct() == nullptr
            || GraphBag->GetPropertyBagStruct()->GetPropertyDescs().Num()
                != ExpectedLiveParameterCount
            || GraphDefaultOwnedDesc == nullptr
            || GraphDefaultOwnedDesc->CachedProperty == nullptr
            || ParentDesc == nullptr
            || ParentDesc->CachedProperty == nullptr
            || OwnedDesc == nullptr
            || OwnedDesc->CachedProperty == nullptr
            || RenamedGraphDesc == nullptr
            || RenamedGraphDesc->ID != ComponentOverrideId
            || RenamedGraphDesc->Name != ComponentOverrideName
            || GraphBag->FindPropertyDescByName(RenamedFromName) != nullptr
            || GraphBag->FindPropertyDescByID(RemovedId) != nullptr
            || ParentBag->FindPropertyDescByID(RemovedId) != nullptr
            || OwnedBag->FindPropertyDescByID(RemovedId) != nullptr
            || OwnedGraphInstance->IsPropertyOverridden(
                GraphDefaultOwnedDesc->CachedProperty)
            || !OwnedGraphInstance->IsGraphParameterOverridden(
                GraphDefaultName)
            || !ParentGraphInstance->IsPropertyOverridden(
                ParentDesc->CachedProperty)
            || !OwnedGraphInstance->IsPropertyOverridden(
                OwnedDesc->CachedProperty))
        {
            OutError = TEXT(
                "The PCG Parameter fixture could not prove Guid alignment, "
                "rename/removal propagation, or native override bits.");
            return false;
        }

        World = LevelFixture.Loaded.World;
        WorldPackage = World != nullptr ? World->GetOutermost() : nullptr;
        GraphPackage = Graph->GetOutermost();
        if (WorldPackage != nullptr)
        {
            WorldPackage->SetDirtyFlag(false);
        }
        if (GraphPackage != nullptr)
        {
            GraphPackage->SetDirtyFlag(false);
        }
        return true;
    }

    TArray<UPCGGraphInterface*> GetParameterInterfaces() const
    {
        TArray<UPCGGraphInterface*> Interfaces = {
            Graph,
            ParentGraphInstance,
            OwnedGraphInstance,
            DeepComponent != nullptr
                ? DeepComponent->GetGraphInstance()
                : nullptr};
        for (UPCGGraphInstance* Instance : DeepGraphInstances)
        {
            Interfaces.Add(Instance);
        }
        Interfaces.Remove(nullptr);
        return Interfaces;
    }

    void ClearSetupDirtyFlags() const
    {
        if (WorldPackage != nullptr)
        {
            WorldPackage->SetDirtyFlag(false);
        }
        if (GraphPackage != nullptr)
        {
            GraphPackage->SetDirtyFlag(false);
        }
    }

    static inline const FName GraphDefaultName =
        FName(TEXT("GraphDefaultDensity"));
    static inline const FName ParentOverrideName =
        FName(TEXT("ParentOverrideSeed"));
    static inline const FName RenamedFromName =
        FName(TEXT("OldComponentOverrideLabel"));
    static inline const FName ComponentOverrideName =
        FName(TEXT("ComponentOverrideLabel"));
    static inline const FName SoftObjectName =
        FName(TEXT("SoftObjectDescriptor"));
    static inline const FName UnsupportedStructName =
        FName(TEXT("UnsupportedStructVector"));
    static inline const FName UnsupportedArrayName =
        FName(TEXT("UnsupportedDoubleArray"));
#if UE_VERSION_NEWER_THAN_OR_EQUAL(5, 8, 0)
    static inline const FName UnsupportedInt8Name =
        FName(TEXT("UnsupportedInt8"));
    static inline const FName UnsupportedInt16Name =
        FName(TEXT("UnsupportedInt16"));
    static inline const FName UnsupportedUInt16Name =
        FName(TEXT("UnsupportedUInt16"));
#endif
    static inline const FName EnumName =
        FName(TEXT("SupportedEnum"));
    static inline const FName RemovedName =
        FName(TEXT("RemovedParameter"));

    static inline const FGuid GraphDefaultId =
        FGuid(0x10000001, 0x10000002, 0x10000003, 0x10000004);
    static inline const FGuid ParentOverrideId =
        FGuid(0x20000001, 0x20000002, 0x20000003, 0x20000004);
    static inline const FGuid ComponentOverrideId =
        FGuid(0x30000001, 0x30000002, 0x30000003, 0x30000004);
    static inline const FGuid SoftObjectId =
        FGuid(0x40000001, 0x40000002, 0x40000003, 0x40000004);
    static inline const FGuid UnsupportedStructId =
        FGuid(0x50000001, 0x50000002, 0x50000003, 0x50000004);
    static inline const FGuid UnsupportedArrayId =
        FGuid(0x60000001, 0x60000002, 0x60000003, 0x60000004);
#if UE_VERSION_NEWER_THAN_OR_EQUAL(5, 8, 0)
    static inline const FGuid UnsupportedInt8Id =
        FGuid(0x65000001, 0x65000002, 0x65000003, 0x65000004);
    static inline const FGuid UnsupportedInt16Id =
        FGuid(0x66000001, 0x66000002, 0x66000003, 0x66000004);
    static inline const FGuid UnsupportedUInt16Id =
        FGuid(0x67000001, 0x67000002, 0x67000003, 0x67000004);
#endif
    static inline const FGuid EnumId =
        FGuid(0x68000001, 0x68000002, 0x68000003, 0x68000004);
    static inline const FGuid RemovedId =
        FGuid(0x70000001, 0x70000002, 0x70000003, 0x70000004);

    static constexpr double GraphDefaultValue = 1.25;
    static constexpr int64 GraphParentDefaultValue = 9007199254740993LL;
    static constexpr int64 ParentOverrideValue = -9007199254740993LL;
    static constexpr double RemovedValue = 9.25;
    static inline const FString GraphComponentDefaultValue =
        TEXT("graph-default");
    static inline const FString ComponentOverrideValue =
        TEXT("component-local");
    static inline const FString StaleComponentOverrideValue =
        TEXT("component-local-after-cursor");

    UPCGGraph* Graph = nullptr;
    UPCGGraphInstance* ParentGraphInstance = nullptr;
    UPCGComponent* Component = nullptr;
    UPCGGraphInstance* OwnedGraphInstance = nullptr;
    UPCGComponent* DeepComponent = nullptr;
    TArray<UPCGGraphInstance*> DeepGraphInstances;

private:
    UWorld* World = nullptr;
    UPackage* WorldPackage = nullptr;
    UPackage* GraphPackage = nullptr;
};

struct FFixturePCGParameterBagState
{
    TWeakObjectPtr<UPCGGraphInterface> Interface;
    FInstancedPropertyBag Parameters;
    TSet<FGuid> OverrideIds;
    bool bGraphInstance = false;
#if WITH_EDITOR
    FDelegateHandle GraphChangedHandle;
    FDelegateHandle ParametersChangedHandle;
#endif
};

class FPCGParameterReadInvariant
{
public:
    explicit FPCGParameterReadInvariant(
        const TArray<UPCGGraphInterface*>& Interfaces)
    {
        for (UPCGGraphInterface* Interface : Interfaces)
        {
            const FInstancedPropertyBag* Parameters = Interface != nullptr
                ? Interface->GetUserParametersStruct()
                : nullptr;
            if (Interface == nullptr || Parameters == nullptr)
            {
                continue;
            }
            FFixturePCGParameterBagState& State =
                States.AddDefaulted_GetRef();
            State.Interface = Interface;
            State.Parameters = *Parameters;
            if (UPCGGraphInstance* Instance =
                    Cast<UPCGGraphInstance>(Interface))
            {
                State.bGraphInstance = true;
                State.OverrideIds =
                    Instance->ParametersOverrides.PropertiesIDsOverridden;
            }
#if WITH_EDITOR
            State.GraphChangedHandle =
                Interface->OnGraphChangedDelegate.AddLambda(
                    [this](UPCGGraphInterface*, EPCGChangeType)
                    {
                        ++GraphChangedCount;
                    });
            State.ParametersChangedHandle =
                Interface->OnGraphParametersChangedDelegate.AddLambda(
                    [this](
                        UPCGGraphInterface*,
                        EPCGGraphParameterEvent,
                        FName)
                    {
                        ++ParametersChangedCount;
                    });
#endif
        }
    }

    ~FPCGParameterReadInvariant()
    {
#if WITH_EDITOR
        for (const FFixturePCGParameterBagState& State : States)
        {
            if (UPCGGraphInterface* Interface = State.Interface.Get())
            {
                Interface->OnGraphChangedDelegate.Remove(
                    State.GraphChangedHandle);
                Interface->OnGraphParametersChangedDelegate.Remove(
                    State.ParametersChangedHandle);
            }
        }
#endif
    }

    bool Verify(FAutomationTestBase& Test) const
    {
        bool bOk = true;
#if WITH_EDITOR
        bOk &= Test.TestEqual(
            TEXT("PCG Parameter Query broadcasts no Graph change"),
            GraphChangedCount,
            0);
        bOk &= Test.TestEqual(
            TEXT("PCG Parameter Query broadcasts no Parameter change"),
            ParametersChangedCount,
            0);
#endif
        for (const FFixturePCGParameterBagState& State : States)
        {
            UPCGGraphInterface* Interface = State.Interface.Get();
            bOk &= Test.TestNotNull(
                TEXT("PCG Parameter Query preserves every Graph interface"),
                Interface);
            const FInstancedPropertyBag* Current = Interface != nullptr
                ? Interface->GetUserParametersStruct()
                : nullptr;
            bOk &= Test.TestTrue(
                TEXT("PCG Parameter Query preserves every Property Bag exactly"),
                Current != nullptr
                    && Current->GetPropertyBagStruct()
                        == State.Parameters.GetPropertyBagStruct()
                    && Current->Identical(&State.Parameters, 0));
            if (State.bGraphInstance)
            {
                const UPCGGraphInstance* Instance =
                    Cast<UPCGGraphInstance>(Interface);
                bool bOverridesMatch = Instance != nullptr
                    && Instance->ParametersOverrides.PropertiesIDsOverridden.Num()
                        == State.OverrideIds.Num();
                if (bOverridesMatch)
                {
                    for (const FGuid& Id : State.OverrideIds)
                    {
                        bOverridesMatch &= Instance->ParametersOverrides
                            .PropertiesIDsOverridden.Contains(Id);
                    }
                }
                bOk &= Test.TestTrue(
                    TEXT("PCG Parameter Query preserves every native override bit"),
                    bOverridesMatch);
            }
        }
        return bOk;
    }

private:
    TArray<FFixturePCGParameterBagState> States;
#if WITH_EDITOR
    int32 GraphChangedCount = 0;
    int32 ParametersChangedCount = 0;
#endif
};

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FSalLevelNoLoadTargetTest,
    "Loomle.Sal.Level.Query.NoLoadCanonicalTarget",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSalLevelNoLoadTargetTest::RunTest(const FString& Parameters)
{
    FScopedLevelQueryFixture Fixture;
    FString Error;
    if (!TestTrue(TEXT("Level no-load fixture builds"), Fixture.Build(Error)))
    {
        AddError(Error);
        return false;
    }
    TestTrue(
        TEXT("No-load fixture starts with no UPackage or UWorld"),
        Fixture.IsUnloadedMapStillUnloaded());

    UWorld* SourceWorld = Fixture.Loaded.World;
    ULevel* SourceLevel = SourceWorld != nullptr
        ? SourceWorld->PersistentLevel
        : nullptr;
    UWorld* ActiveEditorWorld = GEditor != nullptr
        ? GEditor->GetEditorWorldContext().World()
        : nullptr;
    if (!TestNotNull(TEXT("Inactive source fixture has a PersistentLevel"), SourceLevel)
        || !TestNotNull(TEXT("Streamed source fixture has an active Editor World"), ActiveEditorWorld))
    {
        return false;
    }
    const EWorldType::Type OriginalSourceType = SourceWorld->WorldType;
    UWorld* OriginalOwningWorld = SourceLevel->OwningWorld;
    SourceWorld->WorldType = EWorldType::Inactive;
    SourceLevel->OwningWorld = SourceWorld;

    const TSharedRef<FJsonObject> InactiveTarget =
        LevelTarget(Fixture.Loaded.ObjectPath);
    const TSharedPtr<FJsonObject> InactiveTargetResult =
        FSalModule::BuildQueryResult(
            LevelQueryArguments(
                InactiveTarget,
                LevelOperation(TEXT("target"))));
    const TSharedPtr<FJsonObject> InactiveTargetFields =
        FirstLevelAssetFields(InactiveTargetResult);
    bool bInactiveLoaded = true;
    TestTrue(
        TEXT("A saved inactive Editor World canonicalizes without becoming Level content authority"),
        !LevelHasError(InactiveTargetResult)
            && InactiveTargetFields.IsValid()
            && InactiveTargetFields->TryGetBoolField(
                TEXT("loaded"),
                bInactiveLoaded)
            && !bInactiveLoaded);
    const TSharedPtr<FJsonObject> InactiveSummary =
        FSalModule::BuildQueryResult(
            LevelQueryArguments(
                InactiveTarget,
                LevelOperation(TEXT("summary"))));
    TestTrue(
        TEXT("An inactive Editor World cannot serve Level content Query"),
        LevelHasDiagnostic(
            InactiveSummary,
            TEXT("capability.level_not_loaded")));

    SourceLevel->OwningWorld = ActiveEditorWorld;
    const TSharedPtr<FJsonObject> StreamedSummary =
        FSalModule::BuildQueryResult(
            LevelQueryArguments(
                InactiveTarget,
                LevelOperation(TEXT("summary"))));
    const bool bStreamedShapePreserved =
        SourceWorld->WorldType == EWorldType::Inactive
        && SourceLevel->OwningWorld == ActiveEditorWorld;
    SourceLevel->OwningWorld = OriginalOwningWorld;
    SourceWorld->WorldType = OriginalSourceType;
    TestTrue(
        TEXT("An inactive exact source World is readable when its PersistentLevel belongs to the active Editor World"),
        !LevelHasError(StreamedSummary)
            && bStreamedShapePreserved
            && FirstLevelAssetFields(StreamedSummary).IsValid());

    const TSharedRef<FJsonObject> Target =
        LevelTarget(Fixture.Unloaded.ObjectPath);
    const TSharedPtr<FJsonObject> TargetResult =
        FSalModule::BuildQueryResult(
            LevelQueryArguments(Target, LevelOperation(TEXT("target"))));
    TestTrue(
        TEXT("Level target canonicalizes from on-disk Asset Registry evidence"),
        !LevelHasError(TargetResult)
            && LevelHasTargetContext(TargetResult, TEXT("exact_target"))
            && HasCanonicalLevelTarget(
                TargetResult,
                Fixture.Unloaded.ObjectPath));
    const TSharedPtr<FJsonObject> TargetFields =
        FirstLevelAssetFields(TargetResult);
    bool bLoaded = true;
    FString Path;
    TestTrue(
        TEXT("No-load target truthfully reports the canonical unloaded map"),
        TargetFields.IsValid()
            && TargetFields->TryGetStringField(TEXT("path"), Path)
            && Path == Fixture.Unloaded.ObjectPath
            && TargetFields->TryGetBoolField(TEXT("loaded"), bLoaded)
            && !bLoaded);
    TestTrue(
        TEXT("Canonical target Query does not load the map"),
        Fixture.IsUnloadedMapStillUnloaded());

    for (const FString& Operation : {TEXT("summary"), TEXT("actors")})
    {
        const TSharedPtr<FJsonObject> Result =
            FSalModule::BuildQueryResult(
                LevelQueryArguments(Target, LevelOperation(Operation)));
        TestTrue(
            *FString::Printf(
                TEXT("Unloaded Level %s fails with exact loading-state context"),
                *Operation),
            LevelHasDiagnostic(
                Result,
                TEXT("capability.level_not_loaded"))
                && LevelHasTargetContext(Result, TEXT("exact_target"))
                && HasCanonicalLevelTarget(
                    Result,
                    Fixture.Unloaded.ObjectPath));
        TestTrue(
            *FString::Printf(
                TEXT("Unloaded Level %s does not load the map"),
                *Operation),
            Fixture.IsUnloadedMapStillUnloaded());
    }

    const TSharedPtr<FJsonObject> Exact =
        FSalModule::BuildQueryResult(
            LevelQueryArguments(
                Target,
                LevelExactOperation(LevelGuidText(FGuid::NewGuid()))));
    TestTrue(
        TEXT("StableRef resolution on an unloaded exact target reports level_not_loaded"),
        LevelHasDiagnostic(
            Exact,
            TEXT("capability.level_not_loaded"))
            && LevelHasTargetContext(Exact, TEXT("exact_target"))
            && Fixture.IsUnloadedMapStillUnloaded());

    if (!Fixture.Cleanup(Error))
    {
        AddError(Error);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FSalLevelLoadedActorQueryTest,
    "Loomle.Sal.Level.Query.LoadedActorsSummaryStableRefSearchAndPage",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSalLevelLoadedActorQueryTest::RunTest(const FString& Parameters)
{
    FScopedLevelQueryFixture Fixture;
    FString Error;
    if (!TestTrue(TEXT("Loaded Level fixture builds"), Fixture.Build(Error)))
    {
        AddError(Error);
        return false;
    }
    const TSharedRef<FJsonObject> Target =
        LevelTarget(
            Fixture.Loaded.ObjectPath,
            UWorld::StaticClass()->GetPathName());
    if (!TestTrue(
            TEXT("Loaded Level fixture binds to the Editor World context"),
            Fixture.Activate(Fixture.Loaded, Error)))
    {
        AddError(Error);
        return false;
    }
    FLevelReadInvariant Invariant(Fixture.Loaded.World);

    const TSharedPtr<FJsonObject> TargetResult =
        FSalModule::BuildQueryResult(
            LevelQueryArguments(Target, LevelOperation(TEXT("target"))));
    const TSharedPtr<FJsonObject> TargetFields =
        FirstLevelAssetFields(TargetResult);
    bool bLoaded = false;
    FString WorldType;
    TestTrue(
        TEXT("Loaded Level target reports the exact Editor source World"),
        !LevelHasError(TargetResult)
            && HasCanonicalLevelTarget(
                TargetResult,
                Fixture.Loaded.ObjectPath)
            && TargetFields.IsValid()
            && TargetFields->TryGetBoolField(TEXT("loaded"), bLoaded)
            && bLoaded
            && TargetFields->TryGetStringField(TEXT("worldType"), WorldType)
            && WorldType == TEXT("Editor"));

    const TSharedPtr<FJsonObject> Summary =
        FSalModule::BuildQueryResult(
            LevelQueryArguments(Target, LevelOperation(TEXT("summary"))));
    const TSharedPtr<FJsonObject> SummaryFields =
        FirstLevelAssetFields(Summary);
    double ActorCount = 0.0;
    double LoadedActorCount = 0.0;
    double UnloadedDescriptorCount = -1.0;
    double StableActorCount = 0.0;
    bool bIdentityComplete = false;
    TestTrue(
        TEXT("Level summary reports a complete non-WP loaded Actor identity set"),
        !LevelHasError(Summary)
            && SummaryFields.IsValid()
            && SummaryFields->TryGetNumberField(TEXT("actorCount"), ActorCount)
            && ActorCount >= 3.0
            && SummaryFields->TryGetNumberField(
                TEXT("loadedActorCount"),
                LoadedActorCount)
            && LoadedActorCount == ActorCount
            && SummaryFields->TryGetNumberField(
                TEXT("unloadedDescriptorCount"),
                UnloadedDescriptorCount)
            && UnloadedDescriptorCount == 0.0
            && SummaryFields->TryGetNumberField(
                TEXT("stableActorCount"),
                StableActorCount)
            && StableActorCount == ActorCount
            && SummaryFields->TryGetBoolField(
                TEXT("identityComplete"),
                bIdentityComplete)
            && bIdentityComplete);

    const TSharedPtr<FJsonObject> ActorsResult =
        FSalModule::BuildQueryResult(
            LevelQueryArguments(Target, LevelOperation(TEXT("actors"))));
    const TArray<TSharedPtr<FJsonObject>> Actors =
        LevelTaggedObjectFields(ActorsResult, TEXT("actor"));
    const FString AlphaId = LevelGuidText(Fixture.AlphaId);
    const FString BetaId = LevelGuidText(Fixture.BetaId);
    const FString GammaId = LevelGuidText(Fixture.GammaId);
    TestTrue(
        TEXT("Loaded Level actors returns every authored fixture Actor"),
        !LevelHasError(ActorsResult)
            && FindLevelFieldsById(Actors, AlphaId).IsValid()
            && FindLevelFieldsById(Actors, BetaId).IsValid()
            && FindLevelFieldsById(Actors, GammaId).IsValid());
    TestTrue(
        TEXT("Level actors excludes instance projections, external-content Actors, and PCG-generated output"),
        !FindLevelFieldsById(
            Actors,
            LevelGuidText(Fixture.InstanceProjectionId)).IsValid()
            && !FindLevelFieldsById(
                Actors,
                LevelGuidText(Fixture.ExternalContentId)).IsValid()
            && !FindLevelFieldsById(
                Actors,
                LevelGuidText(Fixture.PcgGeneratedId)).IsValid());

    const TSharedPtr<FJsonObject> ExactResult =
        FSalModule::BuildQueryResult(
            LevelQueryArguments(Target, LevelExactOperation(BetaId)));
    const TSharedPtr<FJsonObject> ExactFields =
        FindLevelFieldsById(
            LevelTaggedObjectFields(ExactResult, TEXT("actor")),
            BetaId);
    FString ExactLabel;
    bool bExactLoaded = false;
    bool bStableRefAvailable = false;
    TestTrue(
        TEXT("ActorGuid StableRef resolves the exact loaded Actor"),
        !LevelHasError(ExactResult)
            && ExactFields.IsValid()
            && ExactFields->TryGetStringField(
                TEXT("ActorLabel"),
                ExactLabel)
            && ExactLabel == TEXT("Loomle Level Beta")
            && ExactFields->TryGetBoolField(
                TEXT("loaded"),
                bExactLoaded)
            && bExactLoaded
            && ExactFields->TryGetBoolField(
                TEXT("stableRefAvailable"),
                bStableRefAvailable)
            && bStableRefAvailable);

    for (const FString& NonCanonicalId : {
             BetaId.ToUpper(),
             Fixture.BetaId.ToString(EGuidFormats::Digits)})
    {
        const TSharedPtr<FJsonObject> NonCanonicalExact =
            FSalModule::BuildQueryResult(
                LevelQueryArguments(
                    Target,
                    LevelExactOperation(NonCanonicalId)));
        TestTrue(
            TEXT("Actor StableRef rejects non-canonical ActorGuid text"),
            LevelHasDiagnostic(
                NonCanonicalExact,
                TEXT("validation.invalid_reference")));
    }

    const TSharedPtr<FJsonObject> SearchByLabel =
        FSalModule::BuildQueryResult(
            LevelQueryArguments(
                Target,
                LevelOperation(
                    TEXT("actors"),
                    TEXT("Loomle Level Alpha"))));
    const TArray<TSharedPtr<FJsonObject>> SearchActors =
        LevelTaggedObjectFields(SearchByLabel, TEXT("actor"));
    TestTrue(
        TEXT("Actor search matches the current ActorLabel without using it as identity"),
        !LevelHasError(SearchByLabel)
            && SearchActors.Num() == 1
            && FindLevelFieldsById(SearchActors, AlphaId).IsValid());

    TArray<FString> FullOrder;
    for (const TSharedPtr<FJsonObject>& Fields : Actors)
    {
        FString Id;
        if (Fields.IsValid() && Fields->TryGetStringField(TEXT("id"), Id))
        {
            FullOrder.Add(Id);
        }
    }
    TArray<FString> PagedOrder;
    FString Cursor;
    for (int32 PageIndex = 0; PageIndex < 64; ++PageIndex)
    {
        const TSharedPtr<FJsonObject> Page =
            FSalModule::BuildQueryResult(
                LevelQueryArguments(
                    Target,
                    LevelOperation(TEXT("actors")),
                    1,
                    Cursor));
        if (LevelHasError(Page))
        {
            AddError(TEXT("A deterministic Level Actor continuation page failed."));
            break;
        }
        const TArray<TSharedPtr<FJsonObject>> PageActors =
            LevelTaggedObjectFields(Page, TEXT("actor"));
        if (PageActors.Num() != 1)
        {
            AddError(TEXT("A non-final Level Actor page did not contain exactly one Actor."));
            break;
        }
        FString Id;
        if (!PageActors[0]->TryGetStringField(TEXT("id"), Id))
        {
            AddError(TEXT("A Level Actor page omitted ActorGuid identity."));
            break;
        }
        PagedOrder.Add(Id);
        FString Next;
        if (!ReadLevelNextCursor(Page, Next))
        {
            break;
        }
        Cursor = Next;
    }
    TestEqual(
        TEXT("Cursor pagination reproduces the deterministic full Actor order"),
        PagedOrder,
        FullOrder);

    const TSharedPtr<FJsonObject> FirstSearchPage =
        FSalModule::BuildQueryResult(
            LevelQueryArguments(
                Target,
                LevelOperation(TEXT("actors")),
                1));
    FString SearchBoundCursor;
    if (ReadLevelNextCursor(FirstSearchPage, SearchBoundCursor))
    {
        const TSharedPtr<FJsonObject> ReusedForOtherSearch =
            FSalModule::BuildQueryResult(
                LevelQueryArguments(
                    Target,
                    LevelOperation(TEXT("actors"), TEXT("Alpha")),
                    1,
                    SearchBoundCursor));
        TestTrue(
            TEXT("Actor cursor is bound to its search and snapshot"),
            LevelHasDiagnostic(
                ReusedForOtherSearch,
                TEXT("validation.invalid_cursor")));

        const FString OriginalLabel = Fixture.Alpha->GetActorLabel();
        const bool bOriginalDirty =
            Fixture.Loaded.World->GetOutermost()->IsDirty();
        Fixture.Alpha->SetActorLabel(
            TEXT("Loomle Level Alpha Changed"),
            false);
        const TSharedPtr<FJsonObject> ReusedAfterSnapshotChange =
            FSalModule::BuildQueryResult(
                LevelQueryArguments(
                    Target,
                    LevelOperation(TEXT("actors")),
                    1,
                    SearchBoundCursor));
        TestTrue(
            TEXT("Actor cursor is invalid after any emitted snapshot field changes"),
            LevelHasDiagnostic(
                ReusedAfterSnapshotChange,
                TEXT("validation.invalid_cursor")));
        Fixture.Alpha->SetActorLabel(OriginalLabel, false);
        Fixture.Loaded.World->GetOutermost()->SetDirtyFlag(
            bOriginalDirty);
    }

    const FString OriginalGammaLabel = Fixture.Gamma->GetActorLabel(false);
    const bool bDirtyBeforeLabelRead =
        Fixture.Loaded.World->GetOutermost()->IsDirty();
    Fixture.Gamma->ClearActorLabel();
    int32 ActorLabelChangedEvents = 0;
    const FDelegateHandle LabelChangedHandle =
        FCoreDelegates::OnActorLabelChanged.AddLambda(
            [&ActorLabelChangedEvents](AActor*)
            {
                ++ActorLabelChangedEvents;
            });
    const TSharedPtr<FJsonObject> UnlabeledExact =
        FSalModule::BuildQueryResult(
            LevelQueryArguments(Target, LevelExactOperation(GammaId)));
    const TSharedPtr<FJsonObject> UnlabeledFields =
        FindLevelFieldsById(
            LevelTaggedObjectFields(UnlabeledExact, TEXT("actor")),
            GammaId);
    const bool bLabelRemainedEmpty =
        Fixture.Gamma->GetActorLabel(false).IsEmpty();
    FCoreDelegates::OnActorLabelChanged.Remove(LabelChangedHandle);
    Fixture.Gamma->SetActorLabel(OriginalGammaLabel, false);
    Fixture.Loaded.World->GetOutermost()->SetDirtyFlag(
        bDirtyBeforeLabelRead);
    TestTrue(
        TEXT("Exact Actor Query observes an absent ActorLabel without creating one"),
        !LevelHasError(UnlabeledExact)
            && UnlabeledFields.IsValid()
            && !UnlabeledFields->HasField(TEXT("ActorLabel"))
            && bLabelRemainedEmpty
            && ActorLabelChangedEvents == 0);

    TestTrue(
        TEXT("All loaded Level reads preserve Editor and authored state"),
        Invariant.Verify(*this));

    Fixture.Loaded.World->GetOutermost()->SetDirtyFlag(true);
    const TSharedPtr<FJsonObject> DirtySummary =
        FSalModule::BuildQueryResult(
            LevelQueryArguments(Target, LevelOperation(TEXT("summary"))));
    TestTrue(
        TEXT("Read Query preserves a pre-existing dirty map package"),
        !LevelHasError(DirtySummary)
            && Fixture.Loaded.World->GetOutermost()->IsDirty());
    Fixture.Loaded.World->GetOutermost()->SetDirtyFlag(false);

    if (!Fixture.Cleanup(Error))
    {
        AddError(Error);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FSalLevelActorGuidCorruptionTest,
    "Loomle.Sal.Level.Query.InvalidAndDuplicateActorGuid",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSalLevelActorGuidCorruptionTest::RunTest(const FString& Parameters)
{
    FScopedLevelQueryFixture Fixture;
    FString Error;
    if (!TestTrue(TEXT("Corrupt Level fixture builds"), Fixture.Build(Error)))
    {
        AddError(Error);
        return false;
    }
    const TSharedRef<FJsonObject> Target =
        LevelTarget(
            Fixture.Corrupt.ObjectPath,
            UWorld::StaticClass()->GetPathName());
    if (!TestTrue(
            TEXT("Corrupt Level fixture binds to the Editor World context"),
            Fixture.Activate(Fixture.Corrupt, Error)))
    {
        AddError(Error);
        return false;
    }
    FLevelReadInvariant Invariant(Fixture.Corrupt.World);

    const TSharedPtr<FJsonObject> Summary =
        FSalModule::BuildQueryResult(
            LevelQueryArguments(Target, LevelOperation(TEXT("summary"))));
    const TSharedPtr<FJsonObject> SummaryFields =
        FirstLevelAssetFields(Summary);
    double InvalidCount = 0.0;
    double DuplicateCount = 0.0;
    TestTrue(
        TEXT("Summary exposes bounded invalid and duplicate ActorGuid corruption evidence"),
        !LevelHasError(Summary)
            && LevelHasDiagnostic(
                Summary,
                TEXT("resolution.identity_conflict"))
            && SummaryFields.IsValid()
            && SummaryFields->TryGetNumberField(
                TEXT("invalidActorGuidCount"),
                InvalidCount)
            && InvalidCount == 1.0
            && SummaryFields->TryGetNumberField(
                TEXT("duplicateActorGuidCount"),
                DuplicateCount)
            && DuplicateCount == 1.0);

    const TSharedPtr<FJsonObject> ActorsResult =
        FSalModule::BuildQueryResult(
            LevelQueryArguments(Target, LevelOperation(TEXT("actors"))));
    const TArray<TSharedPtr<FJsonObject>> Actors =
        LevelTaggedObjectFields(ActorsResult, TEXT("actor"));
    const FString InvalidId = LevelGuidText(FGuid());
    const FString DuplicateId = LevelGuidText(Fixture.DuplicateId);
    const TSharedPtr<FJsonObject> InvalidFields =
        FindLevelFieldsById(Actors, InvalidId);
    bool bIdentityValid = true;
    bool bStableRefAvailable = true;
    TestTrue(
        TEXT("Invalid ActorGuid remains readable but receives no StableRef"),
        !LevelHasError(ActorsResult)
            && InvalidFields.IsValid()
            && InvalidFields->TryGetBoolField(
                TEXT("identityValid"),
                bIdentityValid)
            && !bIdentityValid
            && InvalidFields->TryGetBoolField(
                TEXT("stableRefAvailable"),
                bStableRefAvailable)
            && !bStableRefAvailable);
    TestEqual(
        TEXT("Duplicate ActorGuid publishes both corruption records"),
        CountLevelFieldsById(Actors, DuplicateId),
        2);
    for (const TSharedPtr<FJsonObject>& Fields : Actors)
    {
        FString Id;
        if (!Fields.IsValid()
            || !Fields->TryGetStringField(TEXT("id"), Id)
            || Id != DuplicateId)
        {
            continue;
        }
        bool bUnique = true;
        bool bAvailable = true;
        TestTrue(
            TEXT("Each duplicate record denies unique StableRef identity"),
            Fields->TryGetBoolField(TEXT("identityUnique"), bUnique)
                && !bUnique
                && Fields->TryGetBoolField(
                    TEXT("stableRefAvailable"),
                    bAvailable)
                && !bAvailable);
    }

    const TSharedPtr<FJsonObject> DuplicateExact =
        FSalModule::BuildQueryResult(
            LevelQueryArguments(
                Target,
                LevelExactOperation(DuplicateId)));
    TestTrue(
        TEXT("Duplicate ActorGuid StableRef resolution fails closed"),
        LevelHasDiagnostic(
            DuplicateExact,
            TEXT("resolution.identity_conflict")));

    const TSharedPtr<FJsonObject> InvalidExact =
        FSalModule::BuildQueryResult(
            LevelQueryArguments(
                Target,
                LevelExactOperation(InvalidId)));
    TestTrue(
        TEXT("Invalid ActorGuid text cannot become a StableRef"),
        LevelHasDiagnostic(
            InvalidExact,
            TEXT("validation.invalid_reference")));
    TestTrue(
        TEXT("Corruption reads preserve the corrupt source evidence exactly"),
        Invariant.Verify(*this));

    if (!Fixture.Cleanup(Error))
    {
        AddError(Error);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FSalLevelInstanceSourceOwnershipTest,
    "Loomle.Sal.Level.Query.LevelInstanceSourceOwnershipNoLoadAndDedup",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSalLevelInstanceSourceOwnershipTest::RunTest(
    const FString& Parameters)
{
    FScopedLevelInstanceQueryFixture Fixture;
    FString Error;
    if (!TestTrue(
            TEXT("Level Instance source-ownership fixture builds"),
            Fixture.Build(Error)))
    {
        AddError(Error);
        return false;
    }
    if (!TestTrue(
            TEXT("Containing Level Instance fixture becomes the active Editor World"),
            Fixture.Activate(Error)))
    {
        AddError(Error);
        return false;
    }
    if (!TestTrue(
            TEXT("Level Instance fixture captures an unloaded-source baseline"),
            Fixture.CaptureNoLoadState(Error)))
    {
        AddError(Error);
        return false;
    }
    FLevelReadInvariant Invariant(Fixture.Containing.World);

    const TSharedRef<FJsonObject> Target = LevelTarget(
        Fixture.Containing.ObjectPath,
        UWorld::StaticClass()->GetPathName());
    const FString SharedOneId = LevelGuidText(Fixture.SharedOneId);
    const FString SharedTwoId = LevelGuidText(Fixture.SharedTwoId);
    const FString OtherSourceId = LevelGuidText(Fixture.OtherSourceId);
    const FString OrdinaryId = LevelGuidText(Fixture.OrdinaryId);
    const FString MissingSourceId = LevelGuidText(
        Fixture.MissingSourceId);

    const TSharedPtr<FJsonObject> TargetOnlyResult =
        FSalModule::BuildQueryResult(
            LevelQueryArguments(
                Target,
                LevelOperation(TEXT("target"))));
    const TSharedPtr<FJsonObject> SummaryResult =
        FSalModule::BuildQueryResult(
            LevelQueryArguments(
                Target,
                LevelOperation(TEXT("summary"))));
    TestTrue(
        TEXT("Level target and summary reads never enumerate placement source Targets"),
        !LevelHasError(TargetOnlyResult)
            && !LevelHasError(SummaryResult)
            && HasNoLevelRelatedContext(TargetOnlyResult)
            && HasNoLevelRelatedContext(SummaryResult));

    const TSharedPtr<FJsonObject> SharedResult =
        FSalModule::BuildQueryResult(
            LevelQueryArguments(
                Target,
                LevelOperation(
                    TEXT("actors"),
                    TEXT("Shared Source"))));
    const TArray<TSharedPtr<FJsonObject>> SharedActors =
        LevelTaggedObjectFields(SharedResult, TEXT("actor"));
    const TSharedPtr<FJsonObject> SharedOneFields =
        FindLevelFieldsById(SharedActors, SharedOneId);
    const TSharedPtr<FJsonObject> SharedTwoFields =
        FindLevelFieldsById(SharedActors, SharedTwoId);
    FString SharedRelatedAlias;
    FString SharedRelatedAsset;
    FString SharedOneSourceAlias;
    FString SharedTwoSourceAlias;
    bool bSharedOneLevelInstance = false;
    bool bSharedTwoLevelInstance = false;
    TestTrue(
        TEXT("Two placements of one source remain two containing-Level Actors"),
        !LevelHasError(SharedResult)
            && LevelHasTargetContext(
                SharedResult,
                TEXT("exact_target"))
            && HasCanonicalLevelTarget(
                SharedResult,
                Fixture.Containing.ObjectPath)
            && SharedActors.Num() == 2
            && SharedOneFields.IsValid()
            && SharedTwoFields.IsValid()
            && SharedOneFields->TryGetBoolField(
                TEXT("levelInstance"),
                bSharedOneLevelInstance)
            && bSharedOneLevelInstance
            && SharedTwoFields->TryGetBoolField(
                TEXT("levelInstance"),
                bSharedTwoLevelInstance)
            && bSharedTwoLevelInstance);
    TestTrue(
        TEXT("Same-source placements share one canonical related Level and handoff"),
        ReadSingleRelatedLevelTarget(
            SharedResult,
            SharedRelatedAlias,
            SharedRelatedAsset)
            && SharedRelatedAsset == Fixture.SourceA.ObjectPath
            && HasSingleLevelHandoff(
                SharedResult,
                TEXT("inspect_source_level"),
                SharedRelatedAlias)
            && ReadLevelLocalField(
                SharedOneFields,
                TEXT("sourceLevel"),
                SharedOneSourceAlias)
            && ReadLevelLocalField(
                SharedTwoFields,
                TEXT("sourceLevel"),
                SharedTwoSourceAlias)
            && SharedOneSourceAlias == SharedRelatedAlias
            && SharedTwoSourceAlias == SharedRelatedAlias);
    TestTrue(
        TEXT("Level Instance source availability is represented only by sourceLevel presence"),
        SharedOneFields.IsValid()
            && SharedTwoFields.IsValid()
            && !SharedOneFields->HasField(
                TEXT("sourceLevelAvailable"))
            && !SharedTwoFields->HasField(
                TEXT("sourceLevelAvailable")));

    const TSharedPtr<FJsonObject> AllResolvedResult =
        FSalModule::BuildQueryResult(
            LevelQueryArguments(
                Target,
                LevelOperation(
                    TEXT("actors"),
                    TEXT("Loomle Resolved Instance"))));
    const TArray<TSharedPtr<FJsonObject>> AllResolvedActors =
        LevelTaggedObjectFields(AllResolvedResult, TEXT("actor"));
    const TSharedPtr<FJsonObject> AllSharedOneFields =
        FindLevelFieldsById(AllResolvedActors, SharedOneId);
    const TSharedPtr<FJsonObject> AllSharedTwoFields =
        FindLevelFieldsById(AllResolvedActors, SharedTwoId);
    const TSharedPtr<FJsonObject> AllOtherSourceFields =
        FindLevelFieldsById(AllResolvedActors, OtherSourceId);
    TMap<FString, FString> FullAliasesByAsset;
    TSet<FString> FullHandoffAliases;
    const bool bFullTargetsValid = ReadRelatedLevelTargets(
        AllResolvedResult,
        FullAliasesByAsset);
    const bool bFullHandoffsValid = ReadLevelHandoffAliases(
        AllResolvedResult,
        TEXT("inspect_source_level"),
        FullHandoffAliases);
    const FString SourceAAlias = FullAliasesByAsset.FindRef(
        Fixture.SourceA.ObjectPath);
    const FString SourceBAlias = FullAliasesByAsset.FindRef(
        Fixture.SourceB.ObjectPath);
    FString AllSharedOneSourceAlias;
    FString AllSharedTwoSourceAlias;
    FString AllOtherSourceAlias;
    TestTrue(
        TEXT("Full Actor collection deduplicates each distinct source and maps every placement to it"),
        !LevelHasError(AllResolvedResult)
            && AllResolvedActors.Num() == 3
            && AllSharedOneFields.IsValid()
            && AllSharedTwoFields.IsValid()
            && AllOtherSourceFields.IsValid()
            && bFullTargetsValid
            && FullAliasesByAsset.Num() == 2
            && !SourceAAlias.IsEmpty()
            && !SourceBAlias.IsEmpty()
            && SourceAAlias != SourceBAlias
            && bFullHandoffsValid
            && FullHandoffAliases.Num() == 2
            && FullHandoffAliases.Contains(SourceAAlias)
            && FullHandoffAliases.Contains(SourceBAlias)
            && ReadLevelLocalField(
                AllSharedOneFields,
                TEXT("sourceLevel"),
                AllSharedOneSourceAlias)
            && ReadLevelLocalField(
                AllSharedTwoFields,
                TEXT("sourceLevel"),
                AllSharedTwoSourceAlias)
            && ReadLevelLocalField(
                AllOtherSourceFields,
                TEXT("sourceLevel"),
                AllOtherSourceAlias)
            && AllSharedOneSourceAlias == SourceAAlias
            && AllSharedTwoSourceAlias == SourceAAlias
            && AllOtherSourceAlias == SourceBAlias);

    const TSharedPtr<FJsonObject> ExactInstance =
        FSalModule::BuildQueryResult(
            LevelQueryArguments(
                Target,
                LevelExactOperation(SharedOneId)));
    const TSharedPtr<FJsonObject> ExactInstanceFields =
        FindLevelFieldsById(
            LevelTaggedObjectFields(ExactInstance, TEXT("actor")),
            SharedOneId);
    FString ExactRelatedAlias;
    FString ExactRelatedAsset;
    FString ExactSourceAlias;
    TestTrue(
        TEXT("Exact Level Instance Query keeps the containing Level as its main exact Target"),
        !LevelHasError(ExactInstance)
            && LevelHasTargetContext(
                ExactInstance,
                TEXT("exact_target"))
            && HasCanonicalLevelTarget(
                ExactInstance,
                Fixture.Containing.ObjectPath)
            && ExactInstanceFields.IsValid()
            && ReadSingleRelatedLevelTarget(
                ExactInstance,
                ExactRelatedAlias,
                ExactRelatedAsset)
            && ExactRelatedAsset == Fixture.SourceA.ObjectPath
            && HasSingleLevelHandoff(
                ExactInstance,
                TEXT("inspect_source_level"),
                ExactRelatedAlias)
            && ReadLevelLocalField(
                ExactInstanceFields,
                TEXT("sourceLevel"),
                ExactSourceAlias)
            && ExactSourceAlias == ExactRelatedAlias);

    const TSharedPtr<FJsonObject> ExactOrdinary =
        FSalModule::BuildQueryResult(
            LevelQueryArguments(
                Target,
                LevelExactOperation(OrdinaryId)));
    const TSharedPtr<FJsonObject> OrdinaryFields =
        FindLevelFieldsById(
            LevelTaggedObjectFields(ExactOrdinary, TEXT("actor")),
            OrdinaryId);
    TestTrue(
        TEXT("An ordinary Actor exposes no Level Instance source context"),
        !LevelHasError(ExactOrdinary)
            && LevelHasTargetContext(
                ExactOrdinary,
                TEXT("exact_target"))
            && HasCanonicalLevelTarget(
                ExactOrdinary,
                Fixture.Containing.ObjectPath)
            && OrdinaryFields.IsValid()
            && !OrdinaryFields->HasField(TEXT("levelInstance"))
            && !OrdinaryFields->HasField(TEXT("sourceLevel"))
            && !OrdinaryFields->HasField(
                TEXT("sourceLevelAvailable"))
            && HasNoLevelRelatedContext(ExactOrdinary));

    const TSharedPtr<FJsonObject> ExactMissingSource =
        FSalModule::BuildQueryResult(
            LevelQueryArguments(
                Target,
                LevelExactOperation(MissingSourceId)));
    const TSharedPtr<FJsonObject> MissingSourceFields =
        FindLevelFieldsById(
            LevelTaggedObjectFields(
                ExactMissingSource,
                TEXT("actor")),
            MissingSourceId);
    bool bMissingLevelInstance = false;
    TestTrue(
        TEXT("A placement with no saved source remains readable without inventing a Target"),
        !LevelHasError(ExactMissingSource)
            && LevelHasTargetContext(
                ExactMissingSource,
                TEXT("exact_target"))
            && HasCanonicalLevelTarget(
                ExactMissingSource,
                Fixture.Containing.ObjectPath)
            && LevelHasDiagnostic(
                ExactMissingSource,
                TEXT("resolution.level_instance_source_unavailable"))
            && MissingSourceFields.IsValid()
            && MissingSourceFields->TryGetBoolField(
                TEXT("levelInstance"),
                bMissingLevelInstance)
            && bMissingLevelInstance
            && !MissingSourceFields->HasField(TEXT("sourceLevel"))
            && !MissingSourceFields->HasField(
                TEXT("sourceLevelAvailable"))
            && HasNoLevelRelatedContext(ExactMissingSource));

    const TSharedPtr<FJsonObject> FirstResolvedPage =
        FSalModule::BuildQueryResult(
            LevelQueryArguments(
                Target,
                LevelOperation(
                    TEXT("actors"),
                    TEXT("Loomle Resolved Instance")),
                1));
    const TArray<TSharedPtr<FJsonObject>> PageActors =
        LevelTaggedObjectFields(FirstResolvedPage, TEXT("actor"));
    FString Cursor;
    FString PageActorId;
    FString PageSourceAlias;
    FString PageRelatedAlias;
    FString PageRelatedAsset;
    bool bPageShapeValid = !LevelHasError(FirstResolvedPage)
        && PageActors.Num() == 1
        && PageActors[0].IsValid()
        && PageActors[0]->TryGetStringField(
            TEXT("id"),
            PageActorId)
        && ReadLevelNextCursor(FirstResolvedPage, Cursor)
        && ReadLevelLocalField(
            PageActors[0],
            TEXT("sourceLevel"),
            PageSourceAlias)
        && ReadSingleRelatedLevelTarget(
            FirstResolvedPage,
            PageRelatedAlias,
            PageRelatedAsset)
        && HasSingleLevelHandoff(
            FirstResolvedPage,
            TEXT("inspect_source_level"),
            PageRelatedAlias)
        && PageSourceAlias == PageRelatedAlias;
    FString ExpectedPageSource;
    if (PageActorId == SharedOneId || PageActorId == SharedTwoId)
    {
        ExpectedPageSource = Fixture.SourceA.ObjectPath;
    }
    else if (PageActorId == OtherSourceId)
    {
        ExpectedPageSource = Fixture.SourceB.ObjectPath;
    }
    TestTrue(
        TEXT("Actor pagination retains related source context only for the emitted page"),
        bPageShapeValid
            && !ExpectedPageSource.IsEmpty()
            && PageRelatedAsset == ExpectedPageSource);

    FString SourceChangeError;
    if (TestTrue(
            TEXT("Fixture can change one soft source without loading either map"),
            Fixture.SetSharedOneSource(true, SourceChangeError)
                && Fixture.SourcesRemainUnloaded()))
    {
        const TSharedPtr<FJsonObject> StaleContinuation =
            FSalModule::BuildQueryResult(
                LevelQueryArguments(
                    Target,
                    LevelOperation(
                        TEXT("actors"),
                        TEXT("Loomle Resolved Instance")),
                    1,
                    Cursor));
        TestTrue(
            TEXT("A source Target change invalidates an existing Actor cursor"),
            LevelHasDiagnostic(
                StaleContinuation,
                TEXT("validation.invalid_cursor")));
    }
    else if (!SourceChangeError.IsEmpty())
    {
        AddError(SourceChangeError);
    }
    SourceChangeError.Reset();
    if (!Fixture.SetSharedOneSource(false, SourceChangeError))
    {
        AddError(SourceChangeError);
    }

    TestTrue(
        TEXT("All Level Instance ownership reads preserve Editor and authored state"),
        Invariant.Verify(*this));
    TestTrue(
        TEXT("All Level Instance ownership reads preserve no-load state"),
        Fixture.VerifyNoLoadState(*this));

    if (!Fixture.Cleanup(Error))
    {
        AddError(Error);
    }
    return true;
}

class FRunLevelInstanceEditModeEditorContextCommand final
    : public IAutomationLatentCommand
{
public:
    FRunLevelInstanceEditModeEditorContextCommand(
        FAutomationTestBase* InTest,
        TSharedRef<FScopedLevelInstanceQueryFixture> InFixture)
        : Test(InTest)
        , Fixture(MoveTemp(InFixture))
        , StartSeconds(FPlatformTime::Seconds())
    {
    }

    virtual bool Update() override
    {
        if (Stage == EStage::AwaitEditReady)
        {
            ULevelInstanceSubsystem* Subsystem = nullptr;
            ULevel* EditLevel = nullptr;
            if (TryGetReadyEdit(Subsystem, EditLevel))
            {
                // EnterEdit completes over multiple engine frames and can
                // queue a WorldFolders rebuild. Observe a fully ready native
                // edit first, then leave one complete frame before asserting.
                Stage = EStage::RunAssertions;
                return false;
            }
            if (FPlatformTime::Seconds() - StartSeconds
                <= ReadinessTimeoutSeconds)
            {
                return false;
            }

            ReportReadinessTimeout();
            if (Subsystem == nullptr
                || Subsystem->GetEditingLevelInstance()
                    != Fixture->SharedOne)
            {
                // EnterEdit has no return value. A null editing instance can
                // still mean that UE queued its fallback load, so it is not
                // proof that the fixture World is safe to destroy. Fail closed
                // and preserve the fixture until process shutdown.
                Fixture->AbandonDestructiveCleanup();
                return true;
            }
            Stage = EStage::ExitEdit;
            return false;
        }

        if (Stage == EStage::RunAssertions)
        {
            ULevelInstanceSubsystem* Subsystem = nullptr;
            ULevel* EditLevel = nullptr;
            if (!TryGetReadyEdit(Subsystem, EditLevel))
            {
                // If readiness changed during the barrier frame, keep waiting
                // within the original bounded deadline instead of inspecting
                // a partially composed edit World.
                Stage = EStage::AwaitEditReady;
                return false;
            }
            RunAssertions(Subsystem, EditLevel);
            Stage = EStage::ExitEdit;
            return false;
        }

        if (Stage == EStage::ExitEdit)
        {
            FString ExitError;
            const bool bExited = Fixture->ExitActiveEdit(ExitError);
            Test->TestTrue(
                *FString::Printf(
                    TEXT("Deferred Level Instance edit exits cleanly: %s"),
                    ExitError.IsEmpty()
                        ? TEXT("no native error")
                        : *ExitError),
                bExited);
            if (!bExited)
            {
                Fixture->AbandonDestructiveCleanup();
                return true;
            }
            Stage = EStage::PostExitBarrier;
            return false;
        }

        if (Stage == EStage::PostExitBarrier)
        {
            // ExitEdit may queue another WorldFolders rebuild. Preserve the
            // composed World for a complete intervening engine frame.
            Stage = EStage::Cleanup;
            return false;
        }

        FString CleanupError;
        const bool bCleaned = Fixture->Cleanup(CleanupError);
        Test->TestTrue(
            *FString::Printf(
                TEXT("Deferred Level Instance fixture cleanup succeeds: %s"),
                CleanupError.IsEmpty()
                    ? TEXT("no cleanup error")
                    : *CleanupError),
            bCleaned);
        return true;
    }

private:
    enum class EStage : uint8
    {
        AwaitEditReady,
        RunAssertions,
        ExitEdit,
        PostExitBarrier,
        Cleanup,
    };

    bool TryGetReadyEdit(
        ULevelInstanceSubsystem*& OutSubsystem,
        ULevel*& OutEditLevel) const
    {
        OutSubsystem = Fixture->Containing.World != nullptr
            ? Fixture->Containing.World
                ->GetSubsystem<ULevelInstanceSubsystem>()
            : nullptr;
        OutEditLevel = OutSubsystem != nullptr
            && Fixture->SharedOne != nullptr
            ? OutSubsystem->GetLevelInstanceLevel(Fixture->SharedOne)
            : nullptr;
        return Fixture->SharedOne != nullptr
            && Fixture->Containing.World != nullptr
            && OutSubsystem != nullptr
            && Fixture->SharedOne->IsEditing()
            && OutSubsystem->GetEditingLevelInstance()
                == Fixture->SharedOne
            && OutEditLevel != nullptr
            && Fixture->Containing.World->GetCurrentLevel()
                == OutEditLevel;
    }

    void ReportReadinessTimeout() const
    {
        ULevelInstanceSubsystem* Subsystem =
            Fixture->Containing.World != nullptr
            ? Fixture->Containing.World
                ->GetSubsystem<ULevelInstanceSubsystem>()
            : nullptr;
        ULevel* EditLevel = Subsystem != nullptr
            && Fixture->SharedOne != nullptr
            ? Subsystem->GetLevelInstanceLevel(Fixture->SharedOne)
            : nullptr;
        Test->AddError(FString::Printf(
            TEXT("Timed out after %.1f seconds waiting for native Level Instance edit readiness (editing=%s, exactInstance=%s, editLevel=%s, currentLevel=%s)."),
            ReadinessTimeoutSeconds,
            Fixture->SharedOne != nullptr
                    && Fixture->SharedOne->IsEditing()
                ? TEXT("true")
                : TEXT("false"),
            Subsystem != nullptr
                    && Subsystem->GetEditingLevelInstance()
                        == Fixture->SharedOne
                ? TEXT("true")
                : TEXT("false"),
            EditLevel != nullptr ? TEXT("ready") : TEXT("null"),
            EditLevel != nullptr
                    && Fixture->Containing.World != nullptr
                    && Fixture->Containing.World->GetCurrentLevel()
                        == EditLevel
                ? TEXT("ready")
                : TEXT("not_ready")));
    }

    void RunAssertions(
        ULevelInstanceSubsystem* Subsystem,
        ULevel* EditLevel)
    {
        UWorld* EditSourceWorld = EditLevel->GetTypedOuter<UWorld>();
        const bool bEditing = Fixture->SharedOne->IsEditing();
        const bool bEditingExactInstance =
            Subsystem->GetEditingLevelInstance() == Fixture->SharedOne;
        const bool bHasComposedLevel = EditLevel
            != Fixture->Containing.World->PersistentLevel;
        const bool bComposedLevelOwnedByContainingWorld =
            EditLevel->GetWorld() == Fixture->Containing.World;
        const bool bComposedLevelIsCurrent =
            Fixture->Containing.World->GetCurrentLevel() == EditLevel;
        const bool bHasComposedSourceWorld = EditSourceWorld != nullptr;
        const bool bAuthoredSourcePreserved =
            Fixture->SharedOne->GetWorldAsset()
                .ToSoftObjectPath().ToString()
            == Fixture->SourceA.ObjectPath;
        Test->TestTrue(
            TEXT("Native Level Instance edit state is active"),
            bEditing);
        Test->TestTrue(
            TEXT("Native edit state retains the exact placement instance"),
            bEditingExactInstance);
        Test->TestTrue(
            TEXT("Native edit exposes a composed source Level"),
            bHasComposedLevel);
        Test->TestTrue(
            TEXT("The composed edit Level belongs to the containing Editor World"),
            bComposedLevelOwnedByContainingWorld);
        Test->TestTrue(
            TEXT("The composed edit Level is current"),
            bComposedLevelIsCurrent);
        Test->TestTrue(
            TEXT("Native edit retains a loaded source World behind the composed Level"),
            bHasComposedSourceWorld);
        Test->TestTrue(
            TEXT("Native edit preserves the canonical authored source path"),
            bAuthoredSourcePreserved);
        Test->TestTrue(
            TEXT("Public Level Instance edit flow composes the saved source as the current Editor Level"),
            bEditing
                && bEditingExactInstance
                && bHasComposedLevel
                && bComposedLevelOwnedByContainingWorld
                && bComposedLevelIsCurrent
                && bHasComposedSourceWorld
                && bAuthoredSourcePreserved);

        AActor* EditActorOne = nullptr;
        AActor* EditActorTwo = nullptr;
        for (AActor* Actor : EditLevel->Actors)
        {
            if (Actor != nullptr
                && Actor->GetFName() == Fixture->EditActorOneName)
            {
                EditActorOne = Actor;
            }
            else if (Actor != nullptr
                && Actor->GetFName() == Fixture->EditActorTwoName)
            {
                EditActorTwo = Actor;
            }
        }
        UTypedElementSelectionSet* Selection = GEditor != nullptr
            && GEditor->GetSelectedActors() != nullptr
            ? GEditor->GetSelectedActors()->GetElementSelectionSet()
            : nullptr;
        bool bCanInspectContext = Test->TestNotNull(
            TEXT("Edit source retains the first saved Actor"),
            EditActorOne);
        bCanInspectContext &= Test->TestNotNull(
            TEXT("Edit source retains the second saved Actor"),
            EditActorTwo);
        bCanInspectContext &= Test->TestNotNull(
            TEXT("Level Editor exposes its typed selection set"),
            Selection);
        if (!bCanInspectContext)
        {
            if (GEditor != nullptr)
            {
                GEditor->SelectNone(false, true, false);
            }
            return;
        }

        Test->TestTrue(
            TEXT("Composed source Actors carry UE's native edit-Level marker"),
            EditActorOne->IsInEditLevelInstance()
                && EditActorTwo->IsInEditLevelInstance());

        const TArray<ULevel*> LevelsBefore =
            Fixture->Containing.World->GetLevels();
        UPackage* SourcePackage = EditLevel->GetOutermost();
        UPackage* ContainingPackage =
            Fixture->Containing.World->GetOutermost();
        const bool bSourceDirtyBefore = SourcePackage->IsDirty();
        const bool bContainingDirtyBefore =
            ContainingPackage->IsDirty();
        const bool bEditDirtyBefore =
            Subsystem->IsEditingLevelInstanceDirty(Fixture->SharedOne);
        auto BuildContext = [&](const FString& Case)
        {
            int32 AssetLoads = 0;
            int32 PackageSaves = 0;
            const FDelegateHandle LoadHandle =
                FCoreUObjectDelegates::OnAssetLoaded.AddLambda(
                    [&AssetLoads](UObject*)
                    {
                        ++AssetLoads;
                    });
            const FDelegateHandle SaveHandle =
                FCoreUObjectDelegates::OnObjectPreSave.AddLambda(
                    [SourcePackage, ContainingPackage, &PackageSaves](
                        UObject* Object,
                        FObjectPreSaveContext)
                    {
                        if (Object != nullptr
                            && (Object->GetOutermost() == SourcePackage
                                || Object->GetOutermost()
                                    == ContainingPackage))
                        {
                            ++PackageSaves;
                        }
                    });
            const TSharedPtr<FJsonObject> Result =
                Loomle::EditorContext::FEditorContextService::Get()
                    .BuildProviderForTesting(
                        FName(TEXT("level_editor")),
                        Loomle::EditorContext::FInteractionRecord());
            FCoreUObjectDelegates::OnAssetLoaded.Remove(LoadHandle);
            FCoreUObjectDelegates::OnObjectPreSave.Remove(SaveHandle);

            TSharedPtr<FJsonObject> ValidationError;
            Test->TestTrue(
                *FString::Printf(
                    TEXT("%s edit-mode Context is valid SAL Result data"),
                    *Case),
                FSalJson::ValidateResult(Result, ValidationError));
            Test->TestEqual(
                *FString::Printf(
                    TEXT("%s edit-mode Context loads no Asset"),
                    *Case),
                AssetLoads,
                0);
            Test->TestEqual(
                *FString::Printf(
                    TEXT("%s edit-mode Context saves no source or containing package"),
                    *Case),
                PackageSaves,
                0);
            Test->TestTrue(
                *FString::Printf(
                    TEXT("%s edit-mode Context preserves the native edit state"),
                    *Case),
                Fixture->SharedOne->IsEditing()
                    && Subsystem->GetEditingLevelInstance()
                        == Fixture->SharedOne
                    && Subsystem->GetLevelInstanceLevel(
                        Fixture->SharedOne) == EditLevel
                    && Fixture->Containing.World->GetCurrentLevel()
                        == EditLevel
                    && Fixture->Containing.World->GetLevels()
                        == LevelsBefore
                    && SourcePackage->IsDirty()
                        == bSourceDirtyBefore
                    && ContainingPackage->IsDirty()
                        == bContainingDirtyBefore
                    && Subsystem->IsEditingLevelInstanceDirty(
                        Fixture->SharedOne) == bEditDirtyBefore);
            return Result;
        };

        GEditor->SelectNone(false, true, false);
        const TSharedPtr<FJsonObject> ZeroSelection =
            BuildContext(TEXT("Zero-selection"));
        Test->TestTrue(
            TEXT("Zero-selection Context retains the edited source Level Target"),
            !LevelHasError(ZeroSelection)
                && HasCanonicalLevelTarget(
                    ZeroSelection,
                    Fixture->SourceA.ObjectPath)
                && LevelHasCommentContaining(
                    ZeroSelection,
                    TEXT("selected: none")));

        GEditor->SelectActor(EditActorOne, true, false, true);
        GEditor->SelectActor(EditActorTwo, true, false, true);
        const TSharedPtr<FJsonObject> MultiSelection =
            BuildContext(TEXT("Multi-selection"));
        Test->TestTrue(
            TEXT("Multi-selection Context retains the edited source Level Target"),
            !LevelHasError(MultiSelection)
                && HasCanonicalLevelTarget(
                    MultiSelection,
                    Fixture->SourceA.ObjectPath)
                && LevelHasCommentContaining(
                    MultiSelection,
                    TEXT("selected: multiple (2)")));

        GEditor->SelectNone(false, true, false);
        UObject* UnsupportedObject = UBlueprint::StaticClass();
        const FTypedElementHandle ObjectHandle =
            UEngineElementsLibrary::AcquireEditorObjectElementHandle(
                UnsupportedObject);
        const bool bObjectSelected = ObjectHandle
            && Selection->SelectElement(
                ObjectHandle,
                FTypedElementSelectionOptions()
                    .SetAllowHidden(true)
                    .SetAllowLegacyNotifications(false));
        const TArray<UObject*> SelectedObjects =
            Selection->GetSelectedObjects();
        const TSharedPtr<FJsonObject> UnsupportedSelection =
            BuildContext(TEXT("Unsupported UObject selection"));
        Test->TestTrue(
            TEXT("Unsupported UObject selection is observed exactly"),
            bObjectSelected
                && SelectedObjects.Num() == 1
                && SelectedObjects[0] == UnsupportedObject);
        Test->TestTrue(
            TEXT("Unsupported UObject Context retains the edited source Level Target"),
            !LevelHasError(UnsupportedSelection)
                && HasCanonicalLevelTarget(
                    UnsupportedSelection,
                    Fixture->SourceA.ObjectPath)
                && LevelHasCommentContaining(
                    UnsupportedSelection,
                    TEXT("interface: unavailable")));

        Selection->DeselectElement(
            ObjectHandle,
            FTypedElementSelectionOptions()
                .SetAllowHidden(true)
                .SetAllowLegacyNotifications(false));
        Selection->ClearSelection(
            FTypedElementSelectionOptions()
                .SetAllowHidden(true)
                .SetAllowLegacyNotifications(false));
        GEditor->SelectNone(false, true, false);
    }

    FAutomationTestBase* Test = nullptr;
    TSharedRef<FScopedLevelInstanceQueryFixture> Fixture;
    double StartSeconds = 0.0;
    EStage Stage = EStage::AwaitEditReady;
    static constexpr double ReadinessTimeoutSeconds = 5.0;
};

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FSalLevelInstanceEditModeEditorContextTest,
    "Loomle.Sal.Level.Query.LevelInstanceEditModeEditorContext",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSalLevelInstanceEditModeEditorContextTest::RunTest(
    const FString& Parameters)
{
    const TSharedRef<FScopedLevelInstanceQueryFixture> FixtureOwner =
        MakeShared<FScopedLevelInstanceQueryFixture>();
    FScopedLevelInstanceQueryFixture& Fixture = *FixtureOwner;
    FString Error;
    if (!TestTrue(
            TEXT("Level Instance edit-mode Context fixture builds"),
            Fixture.Build(Error))
        || !TestTrue(
            TEXT("Level Instance edit-mode Context fixture becomes active"),
            Fixture.Activate(Error)))
    {
        AddError(Error);
        return false;
    }

    ULevelInstanceSubsystem* Subsystem =
        Fixture.Containing.World->GetSubsystem<ULevelInstanceSubsystem>();
    FText EnterReason;
    if (!TestNotNull(
            TEXT("Edit-mode Context fixture has a Level Instance subsystem"),
            Subsystem)
        || !TestTrue(
            TEXT("Saved Level Instance source can enter native edit mode"),
            Fixture.SharedOne->CanEnterEdit(&EnterReason)))
    {
        AddError(EnterReason.ToString());
        return false;
    }
    Fixture.SharedOne->EnterEdit();

    ADD_LATENT_AUTOMATION_COMMAND(
        FRunLevelInstanceEditModeEditorContextCommand(
            this,
            FixtureOwner));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FSalLevelComponentIdentityQueryTest,
    "Loomle.Sal.Level.Query.ComponentIdentity",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSalLevelComponentIdentityQueryTest::RunTest(
    const FString& Parameters)
{
    // Declaration order is intentional: the Level fixture must destroy its
    // Actor World before the Component fixture releases the Blueprint Class.
    FScopedLevelComponentQueryFixture ComponentFixture;
    FScopedLevelQueryFixture LevelFixture;
    FString Error;
    if (!TestTrue(
            TEXT("Level fixture for Component Query builds"),
            LevelFixture.Build(Error)))
    {
        AddError(Error);
        return false;
    }
    if (!TestTrue(
            TEXT("Component Query Level becomes the active Editor World"),
            LevelFixture.Activate(LevelFixture.Loaded, Error)))
    {
        AddError(Error);
        return false;
    }
    if (!TestTrue(
            TEXT("Native, SCS, Instance, PCG, and UCS fixture builds"),
            ComponentFixture.Build(LevelFixture, Error)))
    {
        AddError(Error);
        return false;
    }

    FLevelReadInvariant LevelInvariant(LevelFixture.Loaded.World);
    FLevelComponentReadInvariant ComponentInvariant(
        ComponentFixture.Actor,
        ComponentFixture.GetBlueprintPackage());
    const TSharedRef<FJsonObject> Target = LevelTarget(
        LevelFixture.Loaded.ObjectPath,
        UWorld::StaticClass()->GetPathName());
    const FString ActorId = LevelGuidText(ComponentFixture.ActorId);

    struct FExpectedComponent
    {
        UActorComponent* Component = nullptr;
        FGuid ActorId;
        FString Source;
        FString Id;
        FString CreationMethod;
        FString DeclaringClass;
        bool bPCG = false;
    };
    const TArray<FExpectedComponent> Expected = {
        {
            ComponentFixture.NativeComponent,
            ComponentFixture.ActorId,
            TEXT("native"),
            ComponentFixture.NativeId,
            TEXT("Native"),
            FString(),
            false
        },
        {
            ComponentFixture.SCSComponent,
            ComponentFixture.ActorId,
            TEXT("scs"),
            ComponentFixture.SCSId,
            TEXT("SimpleConstructionScript"),
            ComponentFixture.SCSDeclaringClass,
            false
        },
        {
            ComponentFixture.InstanceComponent,
            ComponentFixture.ActorId,
            TEXT("instance"),
            ComponentFixture.InstanceId,
            TEXT("Instance"),
            FString(),
            false
        },
        {
            ComponentFixture.PCGComponent,
            ComponentFixture.ActorId,
            TEXT("instance"),
            ComponentFixture.PCGId,
            TEXT("Instance"),
            FString(),
            true
        },
        {
            ComponentFixture.NativePCGComponent,
            ComponentFixture.NativePCGActorId,
            TEXT("native"),
            ComponentFixture.NativePCGId,
            TEXT("Native"),
            FString(),
            true
        }
    };

    const TSharedPtr<FJsonObject> ComponentsResult =
        FSalModule::BuildQueryResult(
            LevelQueryArguments(
                Target,
                LevelOperation(TEXT("components"))));
    const TArray<TSharedPtr<FJsonObject>> Components =
        LevelTaggedObjectFields(ComponentsResult, TEXT("component"));
    TestTrue(
        TEXT("Component collection remains on the canonical exact Level Target"),
        !LevelHasError(ComponentsResult)
            && LevelHasTargetContext(
                ComponentsResult,
                TEXT("exact_target"))
            && HasCanonicalLevelTarget(
                ComponentsResult,
                LevelFixture.Loaded.ObjectPath));
    TestTrue(
        TEXT("Component collection emits every explicitly expected persistent slot"),
        Components.Num() >= Expected.Num());

    TMap<FString, FString> ExpectedPCGAliases;
    for (const FExpectedComponent& Entry : Expected)
    {
        const TSharedPtr<FJsonObject> Fields =
            FindLevelFieldsById(Components, Entry.Id);
        FString Name;
        FString Type;
        FString Source;
        FString CreationMethod;
        FString DeclaringClass;
        bool bRegistered = false;
        bool bStableRefAvailable = false;
        TArray<FString> ActorRefPath;
        TArray<FString> ComponentRefPath;
        const FString EntryActorId = LevelGuidText(Entry.ActorId);
        const TArray<FString> ExpectedActorRefPath = {EntryActorId};
        const TArray<FString> ExpectedComponentRefPath = {
            EntryActorId,
            Entry.Source,
            Entry.Id
        };
        const bool bHasDeclaringClass = Fields.IsValid()
            && Fields->TryGetStringField(
                TEXT("declaringClass"),
                DeclaringClass);
        TestTrue(
            *FString::Printf(
                TEXT("%s Component exposes the frozen 1C-A identity fields"),
                *Entry.Source),
            Fields.IsValid()
                && Fields->TryGetStringField(TEXT("id"), Name)
                && Name == Entry.Id
                && Fields->TryGetStringField(TEXT("name"), Name)
                && Name == Entry.Component->GetFName().ToString()
                && Fields->TryGetStringField(TEXT("type"), Type)
                && Type == Entry.Component->GetClass()->GetPathName()
                && ReadLevelNameField(
                    Fields,
                    TEXT("source"),
                    Source)
                && Source == Entry.Source
                && ReadLevelNameField(
                    Fields,
                    TEXT("CreationMethod"),
                    CreationMethod)
                && CreationMethod == Entry.CreationMethod
                && Fields->TryGetBoolField(
                    TEXT("registered"),
                    bRegistered)
                && bRegistered == Entry.Component->IsRegistered()
                && Fields->TryGetBoolField(
                    TEXT("stableRefAvailable"),
                    bStableRefAvailable)
                && bStableRefAvailable
                && ReadLevelStableRefField(
                    Fields,
                    TEXT("actor"),
                    TEXT("actor"),
                    ActorRefPath)
                && ActorRefPath == ExpectedActorRefPath
                && ReadLevelStableRefField(
                    Fields,
                    TEXT("ref"),
                    TEXT("component"),
                    ComponentRefPath)
                && ComponentRefPath == ExpectedComponentRefPath
                && (Entry.DeclaringClass.IsEmpty()
                    ? !bHasDeclaringClass
                    : bHasDeclaringClass
                        && DeclaringClass == Entry.DeclaringClass));
        if (Entry.bPCG)
        {
            FString Alias;
            const FString Key = PCGComponentIdentityKey(
                EntryActorId,
                Entry.Source,
                Entry.Id);
            TestTrue(
                TEXT("Authored original PCG Component retains one specialized Target LocalRef"),
                ReadLevelLocalField(
                    Fields,
                    TEXT("pcgComponent"),
                    Alias));
            if (!Alias.IsEmpty())
            {
                ExpectedPCGAliases.Add(Key, Alias);
            }
        }
        else
        {
            TestTrue(
                TEXT("Ordinary Component exposes no PCG-specific Target LocalRef"),
                Fields.IsValid()
                    && !Fields->HasField(TEXT("pcgComponent")));
        }
    }

    const FString UCSName =
        ComponentFixture.UCSComponent->GetFName().ToString();
    bool bUCSReturned = false;
    for (const TSharedPtr<FJsonObject>& Fields : Components)
    {
        FString Name;
        if (Fields.IsValid()
            && Fields->TryGetStringField(TEXT("name"), Name)
            && Name == UCSName)
        {
            bUCSReturned = true;
        }
    }
    TestFalse(
        TEXT("UserConstructionScript Component receives no persistent Level identity"),
        bUCSReturned);

    for (const USceneComponent* Generated :
         ComponentFixture.GeneratedPCGComponents)
    {
        bool bGeneratedReturned = false;
        for (const TSharedPtr<FJsonObject>& Fields : Components)
        {
            FString Name;
            if (Fields.IsValid()
                && Fields->TryGetStringField(TEXT("name"), Name)
                && Generated != nullptr
                && Name == Generated->GetFName().ToString())
            {
                bGeneratedReturned = true;
            }
        }
        TestFalse(
            TEXT("PCG generated, debug, and cleanup Components receive no persistent Level identity"),
            bGeneratedReturned);
    }

    TMap<FString, FString> RelatedPCGAliases;
    TSet<FString> PCGHandoffAliases;
    TestTrue(
        TEXT("Level Component collection structurally deduplicates exact PCG Component Targets and handoffs"),
        ReadRelatedPCGComponentTargets(
            ComponentsResult,
            LevelFixture.Loaded.ObjectPath,
            RelatedPCGAliases)
            && RelatedPCGAliases.Num() == ExpectedPCGAliases.Num()
            && ReadLevelHandoffAliases(
                ComponentsResult,
                TEXT("inspect_pcg_component"),
                PCGHandoffAliases)
            && PCGHandoffAliases.Num() == ExpectedPCGAliases.Num());
    for (const TPair<FString, FString>& ExpectedAlias : ExpectedPCGAliases)
    {
        TestTrue(
            TEXT("Every PCG Component LocalRef names its canonical related Target and handoff"),
            RelatedPCGAliases.FindRef(ExpectedAlias.Key)
                    == ExpectedAlias.Value
                && PCGHandoffAliases.Contains(ExpectedAlias.Value));
    }

    const TSharedPtr<FJsonObject> SummaryResult =
        FSalModule::BuildQueryResult(
            LevelQueryArguments(
                Target,
                LevelOperation(TEXT("summary"))));
    const TSharedPtr<FJsonObject> SummaryFields =
        FirstLevelAssetFields(SummaryResult);
    double ComponentCount = 0.0;
    double NativeCount = 0.0;
    double SCSCount = 0.0;
    double InstanceCount = 0.0;
    double PCGCount = 0.0;
    bool bComponentIdentityComplete = false;
    TestTrue(
        TEXT("Level summary reports the closed Component identity counts"),
        !LevelHasError(SummaryResult)
            && SummaryFields.IsValid()
            && SummaryFields->TryGetNumberField(
                TEXT("componentCount"),
                ComponentCount)
            && SummaryFields->TryGetNumberField(
                TEXT("nativeComponentCount"),
                NativeCount)
            && SummaryFields->TryGetNumberField(
                TEXT("scsComponentCount"),
                SCSCount)
            && SummaryFields->TryGetNumberField(
                TEXT("instanceComponentCount"),
                InstanceCount)
            && SummaryFields->TryGetNumberField(
                TEXT("pcgComponentCount"),
                PCGCount)
            && SummaryFields->TryGetBoolField(
                TEXT("componentIdentityComplete"),
                bComponentIdentityComplete)
            && bComponentIdentityComplete
            && ComponentCount == NativeCount + SCSCount + InstanceCount
            && NativeCount >= 1.0
            && SCSCount >= 1.0
            && InstanceCount >= 2.0
            && PCGCount >= 1.0);

    auto ComponentRefKey = [](const TSharedPtr<FJsonObject>& Fields)
    {
        TArray<FString> Identity;
        return ReadLevelStableRefField(
                   Fields,
                   TEXT("ref"),
                   TEXT("component"),
                   Identity)
            ? FString::Join(Identity, TEXT("\x1f"))
            : FString();
    };
    TArray<FString> FullComponentOrder;
    for (const TSharedPtr<FJsonObject>& Fields : Components)
    {
        FullComponentOrder.Add(ComponentRefKey(Fields));
    }
    TestFalse(
        TEXT("Every Component collection item contributes one complete cursor identity"),
        FullComponentOrder.Contains(FString()));

    TArray<FString> PagedComponentOrder;
    FString ComponentCursor;
    FString FirstComponentCursor;
    for (int32 PageIndex = 0; PageIndex < 512; ++PageIndex)
    {
        const TSharedPtr<FJsonObject> PageResult =
            FSalModule::BuildQueryResult(
                LevelQueryArguments(
                    Target,
                    LevelOperation(TEXT("components")),
                    1,
                    ComponentCursor));
        if (LevelHasError(PageResult))
        {
            AddError(TEXT("A deterministic Level Component continuation page failed."));
            break;
        }
        const TArray<TSharedPtr<FJsonObject>> PageComponents =
            LevelTaggedObjectFields(PageResult, TEXT("component"));
        if (PageComponents.Num() != 1)
        {
            AddError(TEXT("A non-final Level Component page did not contain exactly one Component."));
            break;
        }
        const FString Key = ComponentRefKey(PageComponents[0]);
        if (Key.IsEmpty())
        {
            AddError(TEXT("A Level Component page omitted its structured StableRef."));
            break;
        }
        PagedComponentOrder.Add(Key);
        FString PageType;
        PageComponents[0]->TryGetStringField(TEXT("type"), PageType);
        if (PageType == UPCGComponent::StaticClass()->GetPathName())
        {
            FString PageAlias;
            TMap<FString, FString> PageRelated;
            TestTrue(
                TEXT("A PCG Component page enriches only its emitted item with one exact handoff"),
                ReadLevelLocalField(
                    PageComponents[0],
                    TEXT("pcgComponent"),
                    PageAlias)
                    && ReadRelatedPCGComponentTargets(
                        PageResult,
                        LevelFixture.Loaded.ObjectPath,
                        PageRelated)
                    && PageRelated.Num() == 1
                    && PageRelated.FindKey(PageAlias) != nullptr
                    && HasSingleLevelHandoff(
                        PageResult,
                        TEXT("inspect_pcg_component"),
                        PageAlias));
        }
        else
        {
            TestTrue(
                TEXT("A non-PCG Component page carries no unrelated PCG Target context"),
                !PageComponents[0]->HasField(TEXT("pcgComponent"))
                    && HasNoLevelRelatedContext(PageResult));
        }
        FString Next;
        if (!ReadLevelNextCursor(PageResult, Next))
        {
            break;
        }
        if (FirstComponentCursor.IsEmpty())
        {
            FirstComponentCursor = Next;
        }
        ComponentCursor = Next;
    }
    TestEqual(
        TEXT("Component cursor pagination reproduces deterministic collection order"),
        PagedComponentOrder,
        FullComponentOrder);

    if (!FirstComponentCursor.IsEmpty())
    {
        const TSharedPtr<FJsonObject> ReusedForOtherSearch =
            FSalModule::BuildQueryResult(
                LevelQueryArguments(
                    Target,
                    LevelOperation(
                        TEXT("components"),
                        TEXT("LoomleSCSComponent")),
                    1,
                    FirstComponentCursor));
        TestTrue(
            TEXT("Component cursor is bound to its search"),
            LevelHasDiagnostic(
                ReusedForOtherSearch,
                TEXT("validation.invalid_cursor")));

        const bool bMapDirtyBeforeRegistrationChange =
            LevelFixture.Loaded.World->GetOutermost()->IsDirty();
        ComponentFixture.InstanceComponent->UnregisterComponent();
        const TSharedPtr<FJsonObject> ReusedAfterSnapshotChange =
            FSalModule::BuildQueryResult(
                LevelQueryArguments(
                    Target,
                    LevelOperation(TEXT("components")),
                    1,
                    FirstComponentCursor));
        ComponentFixture.InstanceComponent->RegisterComponent();
        LevelFixture.Loaded.World->GetOutermost()->SetDirtyFlag(
            bMapDirtyBeforeRegistrationChange);
        TestTrue(
            TEXT("Component cursor is invalid after an emitted registration field changes"),
            LevelHasDiagnostic(
                ReusedAfterSnapshotChange,
                TEXT("validation.invalid_cursor")));
    }

    for (const FExpectedComponent& Entry : Expected)
    {
        const TSharedPtr<FJsonObject> ExactResult =
            FSalModule::BuildQueryResult(
                LevelQueryArguments(
                    Target,
                    LevelExactComponentOperation(
                        LevelGuidText(Entry.ActorId),
                        Entry.Source,
                        Entry.Id)));
        const TArray<TSharedPtr<FJsonObject>> ExactComponents =
            LevelTaggedObjectFields(ExactResult, TEXT("component"));
        TestTrue(
            *FString::Printf(
                TEXT("Structured exact Component ref resolves %s/%s"),
                *Entry.Source,
                *Entry.Id),
            !LevelHasError(ExactResult)
                && LevelHasTargetContext(
                    ExactResult,
                    TEXT("exact_target"))
                && ExactComponents.Num() == 1
                && FindLevelFieldsById(
                    ExactComponents,
                    Entry.Id).IsValid());
        const TSharedPtr<FJsonObject> ExactFields =
            FindLevelFieldsById(ExactComponents, Entry.Id);
        if (Entry.bPCG)
        {
            FString ExactAlias;
            TMap<FString, FString> ExactRelated;
            const FString ExactKey = PCGComponentIdentityKey(
                LevelGuidText(Entry.ActorId),
                Entry.Source,
                Entry.Id);
            TestTrue(
                TEXT("Exact authored PCG Component retains one canonical inspect handoff"),
                ReadLevelLocalField(
                    ExactFields,
                    TEXT("pcgComponent"),
                    ExactAlias)
                    && ReadRelatedPCGComponentTargets(
                        ExactResult,
                        LevelFixture.Loaded.ObjectPath,
                        ExactRelated)
                    && ExactRelated.Num() == 1
                    && ExactRelated.FindRef(ExactKey) == ExactAlias
                    && HasSingleLevelHandoff(
                        ExactResult,
                        TEXT("inspect_pcg_component"),
                        ExactAlias));
        }
        else
        {
            TestTrue(
                TEXT("Exact ordinary Component has no PCG-specific Target context"),
                ExactFields.IsValid()
                    && !ExactFields->HasField(TEXT("pcgComponent"))
                    && HasNoLevelRelatedContext(ExactResult));
        }
    }

    for (const USceneComponent* Generated :
         ComponentFixture.GeneratedPCGComponents)
    {
        if (Generated == nullptr)
        {
            continue;
        }
        const TSharedPtr<FJsonObject> GeneratedExact =
            FSalModule::BuildQueryResult(
                LevelQueryArguments(
                    Target,
                    LevelExactComponentOperation(
                        ActorId,
                        TEXT("instance"),
                        Generated->GetFName().ToString())));
        TestTrue(
            TEXT("PCG projection tags fail closed in exact Component resolution"),
            LevelHasDiagnostic(
                GeneratedExact,
                TEXT("resolution.object_not_found")));
    }

    FGuid OriginalProbeGuid;
    if (TestTrue(
            TEXT("SCS collision probe can create bounded duplicate declaration evidence"),
            ComponentFixture.BeginSCSGuidCollision(OriginalProbeGuid)))
    {
        const TSharedPtr<FJsonObject> AmbiguousSCS =
            FSalModule::BuildQueryResult(
                LevelQueryArguments(
                    Target,
                    LevelOperation(TEXT("components"))));
        ComponentFixture.EndSCSGuidCollision(OriginalProbeGuid);
        TestTrue(
            TEXT("Duplicate declaring-Class SCS VariableGuid fails Component identity closed"),
            LevelHasDiagnostic(
                AmbiguousSCS,
                TEXT("validation.reference_scan_incomplete")));
    }

    TestTrue(
        TEXT("All Component reads preserve Level and Editor state"),
        LevelInvariant.Verify(*this));
    TestTrue(
        TEXT("All Component reads preserve Component lifecycle and packages"),
        ComponentInvariant.Verify(*this));

    const FString StableSCSId = ComponentFixture.SCSId;
    const FName InitialSCSVariableName =
        ComponentFixture.GetSCSVariableName();
    auto VerifyDurabilityQueries =
        [&](const FString& Stage) -> bool
    {
        UWorld* World = LevelFixture.Loaded.World;
        if (World == nullptr
            || ComponentFixture.Actor == nullptr
            || ComponentFixture.TwinActor == nullptr)
        {
            AddError(FString::Printf(
                TEXT("%s durability verification has no exact loaded World or owners."),
                *Stage));
            return false;
        }

        FLevelReadInvariant StageLevelInvariant(World);
        FLevelComponentReadInvariant StageOwnerInvariant(
            ComponentFixture.Actor,
            ComponentFixture.GetBlueprintPackage());
        FLevelComponentReadInvariant StageTwinInvariant(
            ComponentFixture.TwinActor,
            ComponentFixture.GetBlueprintPackage());

        struct FDurableIdentity
        {
            FGuid ActorGuid;
            FString Source;
            FString Id;
            AActor* ExpectedOwner = nullptr;
            UActorComponent* ExpectedComponent = nullptr;
        };
        const TArray<FDurableIdentity> DurableIdentities = {
            {
                ComponentFixture.ActorId,
                TEXT("native"),
                ComponentFixture.NativeId,
                ComponentFixture.Actor,
                ComponentFixture.NativeComponent
            },
            {
                ComponentFixture.ActorId,
                TEXT("scs"),
                ComponentFixture.SCSId,
                ComponentFixture.Actor,
                ComponentFixture.SCSComponent
            },
            {
                ComponentFixture.ActorId,
                TEXT("instance"),
                ComponentFixture.InstanceId,
                ComponentFixture.Actor,
                ComponentFixture.InstanceComponent
            },
            {
                ComponentFixture.ActorId,
                TEXT("instance"),
                ComponentFixture.PCGId,
                ComponentFixture.Actor,
                ComponentFixture.PCGComponent
            },
            {
                ComponentFixture.TwinActorId,
                TEXT("native"),
                ComponentFixture.NativeId,
                ComponentFixture.TwinActor,
                ComponentFixture.TwinNativeComponent
            },
            {
                ComponentFixture.TwinActorId,
                TEXT("scs"),
                ComponentFixture.SCSId,
                ComponentFixture.TwinActor,
                ComponentFixture.TwinSCSComponent
            },
            {
                ComponentFixture.TwinActorId,
                TEXT("instance"),
                ComponentFixture.InstanceId,
                ComponentFixture.TwinActor,
                ComponentFixture.TwinInstanceComponent
            },
            {
                ComponentFixture.TwinActorId,
                TEXT("instance"),
                ComponentFixture.PCGId,
                ComponentFixture.TwinActor,
                ComponentFixture.TwinPCGComponent
            },
            {
                ComponentFixture.NativePCGActorId,
                TEXT("native"),
                ComponentFixture.NativePCGId,
                ComponentFixture.NativePCGActor,
                ComponentFixture.NativePCGComponent
            }
        };

        int32 AssetLoads = 0;
        int32 PackageSaves = 0;
        UPackage* WorldPackage = World->GetOutermost();
        UPackage* BlueprintPackage =
            ComponentFixture.GetBlueprintPackage();
        const FDelegateHandle LoadHandle =
            FCoreUObjectDelegates::OnAssetLoaded.AddLambda(
                [&AssetLoads](UObject*)
                {
                    ++AssetLoads;
                });
        const FDelegateHandle SaveHandle =
            FCoreUObjectDelegates::OnObjectPreSave.AddLambda(
                [WorldPackage, BlueprintPackage, &PackageSaves](
                    UObject* Object,
                    FObjectPreSaveContext)
                {
                    if (Object != nullptr
                        && (Object->GetOutermost() == WorldPackage
                            || Object->GetOutermost()
                                == BlueprintPackage))
                    {
                        ++PackageSaves;
                    }
                });

        const TSharedPtr<FJsonObject> CollectionResult =
            FSalModule::BuildQueryResult(
                LevelQueryArguments(
                    Target,
                    LevelOperation(TEXT("components"))));
        const TArray<TSharedPtr<FJsonObject>> Collection =
            LevelTaggedObjectFields(
                CollectionResult,
                TEXT("component"));
        TArray<TSharedPtr<FJsonObject>> ExactResults;
        ExactResults.Reserve(DurableIdentities.Num());
        for (const FDurableIdentity& Identity : DurableIdentities)
        {
            ExactResults.Add(FSalModule::BuildQueryResult(
                LevelQueryArguments(
                    Target,
                    LevelExactComponentOperation(
                        LevelGuidText(Identity.ActorGuid),
                        Identity.Source,
                        Identity.Id))));
        }
        FSalResolvedTarget DirectLevelTarget;
        TSharedPtr<FJsonObject> DirectTargetError;
        const bool bDirectTargetResolved =
            FSalTargetResolver().Resolve(
                TEXT("component_durability"),
                Target,
                false,
                DirectLevelTarget,
                DirectTargetError);
        struct FDirectComponentResolution
        {
            bool bResolved = false;
            UActorComponent* Component = nullptr;
            FString ActorId;
            FString Source;
            FString Id;
            FString Code;
        };
        TArray<FDirectComponentResolution> DirectResolutions;
        DirectResolutions.Reserve(DurableIdentities.Num());
        for (const FDurableIdentity& Identity : DurableIdentities)
        {
            FDirectComponentResolution& Direct =
                DirectResolutions.AddDefaulted_GetRef();
            FString Name;
            FString Type;
            FString CreationMethod;
            FString DeclaringClass;
            FString Message;
            Direct.bResolved = bDirectTargetResolved
                && FSalLevelInterface::ResolveExactComponent(
                    DirectLevelTarget,
                    LevelGuidText(Identity.ActorGuid),
                    Identity.Source,
                    Identity.Id,
                    Direct.Component,
                    Direct.ActorId,
                    Direct.Source,
                    Direct.Id,
                    Name,
                    Type,
                    CreationMethod,
                    DeclaringClass,
                    Direct.Code,
                    Message);
        }
        const TSharedPtr<FJsonObject> PCGTargetResult =
            FSalModule::BuildQueryResult(
                PCGComponentQueryArguments(
                    PCGComponentTarget(
                        LevelFixture.Loaded.ObjectPath,
                        ComponentFixture.ActorId,
                        TEXT("instance"),
                        ComponentFixture.PCGId),
                    LevelOperation(TEXT("target"))));

        FCoreUObjectDelegates::OnAssetLoaded.Remove(LoadHandle);
        FCoreUObjectDelegates::OnObjectPreSave.Remove(SaveHandle);

        bool bOk = TestTrue(
            *FString::Printf(
                TEXT("%s Component collection succeeds"),
                *Stage),
            !LevelHasError(CollectionResult));
        bOk &= TestTrue(
            *FString::Printf(
                TEXT("%s direct Level Target resolves to the current loaded World"),
                *Stage),
            bDirectTargetResolved
                && DirectLevelTarget.Object == World);
        for (int32 Index = 0;
             Index < DurableIdentities.Num();
             ++Index)
        {
            const FDurableIdentity& Identity =
                DurableIdentities[Index];
            const FString IdentityActorId =
                LevelGuidText(Identity.ActorGuid);
            const TSharedPtr<FJsonObject> CollectionFields =
                FindLevelComponentFieldsByIdentity(
                    Collection,
                    IdentityActorId,
                    Identity.Source,
                    Identity.Id);
            bOk &= TestEqual(
                *FString::Printf(
                    TEXT("%s collection contains one exact %s/%s owner-qualified slot"),
                    *Stage,
                    *Identity.Source,
                    *Identity.Id),
                CountLevelComponentFieldsByIdentity(
                    Collection,
                    IdentityActorId,
                    Identity.Source,
                    Identity.Id),
                1);
            bOk &= TestNotNull(
                *FString::Printf(
                    TEXT("%s collection exposes the full Component StableRef"),
                    *Stage),
                CollectionFields.Get());

            const TSharedPtr<FJsonObject>& ExactResult =
                ExactResults[Index];
            const TArray<TSharedPtr<FJsonObject>> ExactComponents =
                LevelTaggedObjectFields(
                    ExactResult,
                    TEXT("component"));
            const TSharedPtr<FJsonObject> ExactFields =
                FindLevelComponentFieldsByIdentity(
                    ExactComponents,
                    IdentityActorId,
                    Identity.Source,
                    Identity.Id);
            TArray<FString> OwnerRef;
            bOk &= TestTrue(
                *FString::Printf(
                    TEXT("%s exact %s/%s resolves only inside Actor %s"),
                    *Stage,
                    *Identity.Source,
                    *Identity.Id,
                    *IdentityActorId),
                !LevelHasError(ExactResult)
                    && ExactComponents.Num() == 1
                    && ExactFields.IsValid()
                    && ReadLevelStableRefField(
                        ExactFields,
                        TEXT("actor"),
                        TEXT("actor"),
                        OwnerRef)
                    && OwnerRef
                        == TArray<FString>{IdentityActorId});
            const FDirectComponentResolution& Direct =
                DirectResolutions[Index];
            bOk &= TestTrue(
                *FString::Printf(
                    TEXT("%s direct resolver returns the current %s/%s UObject for Actor %s"),
                    *Stage,
                    *Identity.Source,
                    *Identity.Id,
                    *IdentityActorId),
                Direct.bResolved
                    && Direct.Code.IsEmpty()
                    && Direct.Component
                        == Identity.ExpectedComponent
                    && Direct.Component != nullptr
                    && Direct.Component->GetOwner()
                        == Identity.ExpectedOwner
                    && Direct.ActorId == IdentityActorId
                    && Direct.Source == Identity.Source
                    && Direct.Id == Identity.Id);
        }

        bOk &= TestEqual(
            *FString::Printf(
                TEXT("%s same native slot appears once per Blueprint Actor"),
                *Stage),
            CountLevelFieldsById(Collection, ComponentFixture.NativeId),
            2);
        bOk &= TestEqual(
            *FString::Printf(
                TEXT("%s same SCS slot appears once per Blueprint Actor"),
                *Stage),
            CountLevelFieldsById(Collection, ComponentFixture.SCSId),
            2);
        bOk &= TestEqual(
            *FString::Printf(
                TEXT("%s same instance slot appears once per Blueprint Actor"),
                *Stage),
            CountLevelFieldsById(Collection, ComponentFixture.InstanceId),
            2);
        bOk &= TestEqual(
            *FString::Printf(
                TEXT("%s same original PCG slot appears once per Blueprint Actor"),
                *Stage),
            CountLevelFieldsById(Collection, ComponentFixture.PCGId),
            2);

        TMap<FString, FString> RelatedPCGTargets;
        const FString OriginalPCGKey = PCGComponentIdentityKey(
            LevelGuidText(ComponentFixture.ActorId),
            TEXT("instance"),
            ComponentFixture.PCGId);
        const FString TwinPCGKey = PCGComponentIdentityKey(
            LevelGuidText(ComponentFixture.TwinActorId),
            TEXT("instance"),
            ComponentFixture.PCGId);
        bOk &= TestTrue(
            *FString::Printf(
                TEXT("%s same-slot PCG handoffs remain isolated by ActorGuid"),
                *Stage),
            ReadRelatedPCGComponentTargets(
                CollectionResult,
                LevelFixture.Loaded.ObjectPath,
                RelatedPCGTargets)
                && RelatedPCGTargets.Contains(OriginalPCGKey)
                && RelatedPCGTargets.Contains(TwinPCGKey)
                && RelatedPCGTargets.FindRef(OriginalPCGKey)
                    != RelatedPCGTargets.FindRef(TwinPCGKey));
        bOk &= TestTrue(
            *FString::Printf(
                TEXT("%s reloaded original PCG Component retains its specialized Target"),
                *Stage),
            !LevelHasError(PCGTargetResult)
                && LevelHasTargetContext(
                    PCGTargetResult,
                    TEXT("exact_target")));
        bOk &= TestEqual(
            *FString::Printf(
                TEXT("%s Query loads no Asset"),
                *Stage),
            AssetLoads,
            0);
        bOk &= TestEqual(
            *FString::Printf(
                TEXT("%s Query saves no Level or Blueprint package"),
                *Stage),
            PackageSaves,
            0);
        bOk &= TestTrue(
            *FString::Printf(
                TEXT("%s Query preserves Level and Editor state"),
                *Stage),
            StageLevelInvariant.Verify(*this));
        bOk &= TestTrue(
            *FString::Printf(
                TEXT("%s Query preserves the first owner Component incarnation"),
                *Stage),
            StageOwnerInvariant.Verify(*this));
        bOk &= TestTrue(
            *FString::Printf(
                TEXT("%s Query preserves the twin owner Component incarnation"),
                *Stage),
            StageTwinInvariant.Verify(*this));
        return bOk;
    };

    bool bDurabilityReady = TestTrue(
        TEXT("Same-Class Blueprint Actors can author identical native, SCS, instance, and PCG slots"),
        ComponentFixture.AddSameSlotTwin(
            LevelFixture.Loaded.World,
            Error));
    if (!bDurabilityReady && !Error.IsEmpty())
    {
        AddError(Error);
    }
    if (bDurabilityReady)
    {
        bDurabilityReady = VerifyDurabilityQueries(
            TEXT("Same-slot/different-Actor"));
    }

    if (bDurabilityReady)
    {
        const TWeakObjectPtr<UActorComponent> SCSBeforeRerun =
            ComponentFixture.SCSComponent;
        Error.Reset();
        bDurabilityReady = TestTrue(
            TEXT("RerunConstructionScripts rebuilds the Blueprint Actors and allows identity refresh"),
            ComponentFixture.RerunConstructionScriptsAndRefresh(
                LevelFixture.Loaded.World,
                Error));
        if (!bDurabilityReady && !Error.IsEmpty())
        {
            AddError(Error);
        }
        if (bDurabilityReady)
        {
            TestEqual(
                TEXT("RerunConstructionScripts preserves the qualified SCS StableRef"),
                ComponentFixture.SCSId,
                StableSCSId);
            TestTrue(
                TEXT("RerunConstructionScripts resolves the SCS StableRef to a current live Component"),
                ComponentFixture.SCSComponent != nullptr
                    && IsValid(ComponentFixture.SCSComponent)
                    && !SCSBeforeRerun.IsValid()
                    && SCSBeforeRerun
                        != TWeakObjectPtr<UActorComponent>(
                            ComponentFixture.SCSComponent));
            TestTrue(
                TEXT("RerunConstructionScripts retains the exact ActorGuid owner"),
                ComponentFixture.Actor != nullptr
                    && ComponentFixture.Actor->GetActorGuid()
                        == ComponentFixture.ActorId);
            bDurabilityReady = VerifyDurabilityQueries(
                TEXT("RerunConstructionScripts"));
        }
    }

    if (bDurabilityReady)
    {
        const TWeakObjectPtr<AActor> ActorBeforeRecompile =
            ComponentFixture.Actor;
        const TWeakObjectPtr<UActorComponent> SCSBeforeRecompile =
            ComponentFixture.SCSComponent;
        Error.Reset();
        bDurabilityReady = TestTrue(
            TEXT("SCS variable rename and Blueprint recompile refresh every owner by ActorGuid"),
            ComponentFixture.RenameSCSRecompileAndRefresh(
                LevelFixture.Loaded.World,
                Error));
        if (!bDurabilityReady && !Error.IsEmpty())
        {
            AddError(Error);
        }
        if (bDurabilityReady)
        {
            TestEqual(
                TEXT("Blueprint recompile preserves the qualified SCS Guid StableRef"),
                ComponentFixture.SCSId,
                StableSCSId);
            TestTrue(
                TEXT("Blueprint recompile changes only SCS presentation identity"),
                ComponentFixture.GetSCSVariableName()
                    != InitialSCSVariableName);
            TestTrue(
                TEXT("Blueprint recompile does not reuse the superseded Actor incarnation"),
                !ActorBeforeRecompile.IsValid()
                    && ActorBeforeRecompile
                        != TWeakObjectPtr<AActor>(
                            ComponentFixture.Actor));
            TestTrue(
                TEXT("Blueprint recompile does not reuse the superseded SCS Component incarnation"),
                !SCSBeforeRecompile.IsValid()
                    && SCSBeforeRecompile
                        != TWeakObjectPtr<UActorComponent>(
                            ComponentFixture.SCSComponent));
            bDurabilityReady = VerifyDurabilityQueries(
                TEXT("Blueprint recompile"));
        }
    }

    if (bDurabilityReady)
    {
        Error.Reset();
        bDurabilityReady = TestTrue(
            TEXT("Durability fixture saves its final recompiled Blueprint"),
            ComponentFixture.SaveBlueprintForDurability(Error));
        if (!bDurabilityReady && !Error.IsEmpty())
        {
            AddError(Error);
        }
    }
    if (bDurabilityReady)
    {
        Error.Reset();
        bDurabilityReady = TestTrue(
            TEXT("Durability fixture saves the Component-bearing Level after the Blueprint"),
            LevelFixture.SaveLoadedMapForDurability(Error));
        if (!bDurabilityReady && !Error.IsEmpty())
        {
            AddError(Error);
        }
    }

    if (bDurabilityReady)
    {
        const TWeakObjectPtr<AActor> OldActor =
            ComponentFixture.Actor;
        const TWeakObjectPtr<UActorComponent> OldNative =
            ComponentFixture.NativeComponent;
        const TWeakObjectPtr<UActorComponent> OldSCS =
            ComponentFixture.SCSComponent;
        const TWeakObjectPtr<UActorComponent> OldInstance =
            ComponentFixture.InstanceComponent;
        const TWeakObjectPtr<UActorComponent> OldPCG =
            ComponentFixture.PCGComponent;
        const TWeakObjectPtr<UBlueprint> OldBlueprint =
            ComponentFixture.GetBlueprint();
        const TWeakObjectPtr<UClass> OldGeneratedClass =
            ComponentFixture.GetGeneratedClass();

        ComponentFixture.ReleaseWorldObjectsForReload();
        Error.Reset();
        bDurabilityReady = TestTrue(
            TEXT("Saved Component-bearing Level unloads completely"),
            LevelFixture.UnloadLoadedMapForDurability(Error));
        if (!bDurabilityReady && !Error.IsEmpty())
        {
            AddError(Error);
        }
        if (bDurabilityReady)
        {
            TestTrue(
                TEXT("Map unload releases every captured Actor and Component incarnation"),
                !OldActor.IsValid()
                    && !OldNative.IsValid()
                    && !OldSCS.IsValid()
                    && !OldInstance.IsValid()
                    && !OldPCG.IsValid());
            Error.Reset();
            bDurabilityReady = TestTrue(
                TEXT("Saved durability Blueprint and generated Class unload completely"),
                ComponentFixture.UnloadBlueprintForDurability(Error));
            if (!bDurabilityReady && !Error.IsEmpty())
            {
                AddError(Error);
            }
        }
        if (bDurabilityReady)
        {
            TestTrue(
                TEXT("Blueprint unload releases the captured Blueprint and generated Class incarnations"),
                !OldBlueprint.IsValid()
                    && !OldGeneratedClass.IsValid());

            int32 UnloadedQueryLoads = 0;
            int32 UnloadedQuerySaves = 0;
            const FDelegateHandle LoadHandle =
                FCoreUObjectDelegates::OnAssetLoaded.AddLambda(
                    [&UnloadedQueryLoads](UObject*)
                    {
                        ++UnloadedQueryLoads;
                    });
            const FDelegateHandle SaveHandle =
                FCoreUObjectDelegates::OnObjectPreSave.AddLambda(
                    [&UnloadedQuerySaves](
                        UObject*,
                        FObjectPreSaveContext)
                    {
                        ++UnloadedQuerySaves;
                    });
            const TSharedPtr<FJsonObject> UnloadedExact =
                FSalModule::BuildQueryResult(
                    LevelQueryArguments(
                        Target,
                        LevelExactComponentOperation(
                            LevelGuidText(ComponentFixture.ActorId),
                            TEXT("scs"),
                            StableSCSId)));
            FCoreUObjectDelegates::OnAssetLoaded.Remove(LoadHandle);
            FCoreUObjectDelegates::OnObjectPreSave.Remove(SaveHandle);
            TestTrue(
                TEXT("Exact Component Query fails closed while its saved Level is unloaded"),
                LevelHasDiagnostic(
                    UnloadedExact,
                    TEXT("capability.level_not_loaded")));
            TestEqual(
                TEXT("Unloaded exact Component Query loads no map, Blueprint, or Class"),
                UnloadedQueryLoads,
                0);
            TestEqual(
                TEXT("Unloaded exact Component Query saves no package"),
                UnloadedQuerySaves,
                0);
            TestTrue(
                TEXT("Unloaded exact Component Query leaves the map, Blueprint, and generated Class packages absent"),
                FindPackage(
                    nullptr,
                    *LevelFixture.Loaded.PackageName) == nullptr
                    && FindObject<UWorld>(
                        nullptr,
                        *LevelFixture.Loaded.ObjectPath) == nullptr
                    && ComponentFixture
                        .IsBlueprintUnloadedForDurability());

            Error.Reset();
            bDurabilityReady = TestTrue(
                TEXT("Durability fixture explicitly reloads the saved Component-bearing Level from disk"),
                LevelFixture.ReloadLoadedMapForDurability(Error));
            if (!bDurabilityReady && !Error.IsEmpty())
            {
                AddError(Error);
            }
        }
        if (bDurabilityReady)
        {
            Error.Reset();
            bDurabilityReady = TestTrue(
                TEXT("Reloaded durability Level becomes the active exact Editor source"),
                LevelFixture.Activate(
                    LevelFixture.Loaded,
                    Error));
            if (!bDurabilityReady && !Error.IsEmpty())
            {
                AddError(Error);
            }
        }
        if (bDurabilityReady)
        {
            Error.Reset();
            bDurabilityReady = TestTrue(
                TEXT("Reloaded durability Level reacquires every Component from ActorGuid and source slot"),
                ComponentFixture.RefreshAfterExternalLifecycle(
                    LevelFixture.Loaded.World,
                    Error));
            if (!bDurabilityReady && !Error.IsEmpty())
            {
                AddError(Error);
            }
        }
        if (bDurabilityReady)
        {
            const TWeakObjectPtr<AActor> ReloadedActor =
                ComponentFixture.Actor;
            const TWeakObjectPtr<UActorComponent> ReloadedNative =
                ComponentFixture.NativeComponent;
            const TWeakObjectPtr<UActorComponent> ReloadedSCS =
                ComponentFixture.SCSComponent;
            const TWeakObjectPtr<UActorComponent> ReloadedInstance =
                ComponentFixture.InstanceComponent;
            const TWeakObjectPtr<UActorComponent> ReloadedPCG =
                ComponentFixture.PCGComponent;
            const TWeakObjectPtr<UBlueprint> ReloadedBlueprint =
                ComponentFixture.GetBlueprint();
            const TWeakObjectPtr<UClass> ReloadedGeneratedClass =
                ComponentFixture.GetGeneratedClass();
            TestTrue(
                TEXT("Reload creates new Actor, Component, Blueprint, and Class incarnations"),
                !OldActor.IsValid()
                    && !OldNative.IsValid()
                    && !OldSCS.IsValid()
                    && !OldInstance.IsValid()
                    && !OldPCG.IsValid()
                    && !OldBlueprint.IsValid()
                    && !OldGeneratedClass.IsValid()
                    && ReloadedActor.IsValid()
                    && ReloadedNative.IsValid()
                    && ReloadedSCS.IsValid()
                    && ReloadedInstance.IsValid()
                    && ReloadedPCG.IsValid()
                    && ReloadedBlueprint.IsValid()
                    && ReloadedGeneratedClass.IsValid()
                    && ReloadedActor != OldActor
                    && ReloadedNative != OldNative
                    && ReloadedSCS != OldSCS
                    && ReloadedInstance != OldInstance
                    && ReloadedPCG != OldPCG
                    && ReloadedBlueprint != OldBlueprint
                    && ReloadedGeneratedClass
                        != OldGeneratedClass);
            bDurabilityReady = VerifyDurabilityQueries(
                TEXT("Save/unload/reload"));
        }
    }

    Error.Reset();
    if (!LevelFixture.Cleanup(Error))
    {
        AddError(Error);
    }
    Error.Reset();
    if (!ComponentFixture.Cleanup(Error))
    {
        AddError(Error);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FSalLevelEditorContextAndDeclarationHandoffTest,
    "Loomle.Sal.Level.Query.EditorContextAndDeclarationHandoffs",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSalLevelEditorContextAndDeclarationHandoffTest::RunTest(
    const FString& Parameters)
{
    // The Level fixture must destroy its Actor World before the Component
    // fixture releases the generated Blueprint Class.
    FScopedLevelComponentQueryFixture ComponentFixture;
    FScopedLevelQueryFixture LevelFixture;
    FString Error;
    if (!TestTrue(
            TEXT("Level handoff fixture builds"),
            LevelFixture.Build(Error)))
    {
        AddError(Error);
        return false;
    }
    if (!TestTrue(
            TEXT("Level handoff fixture becomes the active Editor World"),
            LevelFixture.Activate(LevelFixture.Loaded, Error)))
    {
        AddError(Error);
        return false;
    }
    if (!TestTrue(
            TEXT("Level declaration fixture builds"),
            ComponentFixture.Build(LevelFixture, Error)))
    {
        AddError(Error);
        return false;
    }

    FLevelReadInvariant LevelInvariant(LevelFixture.Loaded.World);
    FLevelComponentReadInvariant ComponentInvariant(
        ComponentFixture.Actor,
        ComponentFixture.GetBlueprintPackage());
    const TSharedRef<FJsonObject> Target = LevelTarget(
        LevelFixture.Loaded.ObjectPath,
        UWorld::StaticClass()->GetPathName());

    for (const FString& Operation : {
             TEXT("target"),
             TEXT("summary"),
             TEXT("actors"),
             TEXT("components")})
    {
        const TSharedPtr<FJsonObject> Result =
            FSalModule::BuildQueryResult(
                LevelQueryArguments(
                    Target,
                    LevelOperation(Operation)));
        const TSharedPtr<FJsonObject> AssetTarget =
            ReadUniqueHandoffTarget(
                Result,
                TEXT("inspect_asset"));
        TestTrue(
            *FString::Printf(
                TEXT("Successful Level %s retains one canonical source Asset Target"),
                *Operation),
            !LevelHasError(Result)
                && IsCanonicalRelatedPathTarget(
                    AssetTarget,
                    TEXT("asset"),
                    TEXT("path"),
                    LevelFixture.Loaded.ObjectPath,
                    UWorld::StaticClass()->GetPathName()));
        TestTrue(
            *FString::Printf(
                TEXT("Level %s emits no per-row declaration navigation"),
                *Operation),
            CountHandoffPurpose(Result, TEXT("inspect_class")) == 0
                && CountRelatedTargetDomain(
                    Result,
                    TEXT("class")) == 0
                && CountHandoffPurpose(
                    Result,
                    TEXT("inspect_blueprint")) == 0
                && CountRelatedTargetDomain(
                    Result,
                    TEXT("blueprint")) == 0);
    }

    const TSharedPtr<FJsonObject> UnloadedTargetResult =
        FSalModule::BuildQueryResult(
            LevelQueryArguments(
                LevelTarget(
                    LevelFixture.Unloaded.ObjectPath,
                    UWorld::StaticClass()->GetPathName()),
                LevelOperation(TEXT("target"))));
    TestTrue(
        TEXT("Source Asset navigation for an unloaded map stays zero-load"),
        IsCanonicalRelatedPathTarget(
            ReadUniqueHandoffTarget(
                UnloadedTargetResult,
                TEXT("inspect_asset")),
            TEXT("asset"),
            TEXT("path"),
            LevelFixture.Unloaded.ObjectPath,
            UWorld::StaticClass()->GetPathName())
            && LevelFixture.IsUnloadedMapStillUnloaded());

    const TSharedPtr<FJsonObject> InvalidExactActor =
        FSalModule::BuildQueryResult(
            LevelQueryArguments(
                Target,
                LevelExactOperation(TEXT("not-an-actor-guid"))));
    TestTrue(
        TEXT("Failed exact Level Query emits no source Asset handoff"),
        LevelHasError(InvalidExactActor)
            && CountHandoffPurpose(
                InvalidExactActor,
                TEXT("inspect_asset")) == 0
            && CountRelatedTargetDomain(
                InvalidExactActor,
                TEXT("asset")) == 0);

    UBlueprintGeneratedClass* ActorClass =
        Cast<UBlueprintGeneratedClass>(
            ComponentFixture.Actor->GetClass());
    UBlueprint* Blueprint = ActorClass != nullptr
        ? Cast<UBlueprint>(ActorClass->ClassGeneratedBy)
        : nullptr;
    if (!TestNotNull(
            TEXT("Declaration fixture retains its generated Blueprint"),
            Blueprint))
    {
        return false;
    }
    const TSharedPtr<FJsonObject> ExactActor =
        FSalModule::BuildQueryResult(
            LevelQueryArguments(
                Target,
                LevelExactOperation(
                    LevelGuidText(ComponentFixture.ActorId))));
    TestTrue(
        TEXT("Exact Actor exposes its actual Class declaration Target"),
        !LevelHasError(ExactActor)
            && IsCanonicalRelatedPathTarget(
                ReadUniqueHandoffTarget(
                    ExactActor,
                    TEXT("inspect_class")),
                TEXT("class"),
                TEXT("path"),
                ActorClass->GetPathName()));
    TestTrue(
        TEXT("Exact Blueprint Actor exposes its proven generating Blueprint Target"),
        IsCanonicalBlueprintTarget(
            ReadUniqueHandoffTarget(
                ExactActor,
                TEXT("inspect_blueprint")),
            Blueprint->GetPathName(),
            Blueprint->GetBlueprintGuid()));
    TestTrue(
        TEXT("Exact Actor also retains the source map Asset Target"),
        IsCanonicalRelatedPathTarget(
            ReadUniqueHandoffTarget(
                ExactActor,
                TEXT("inspect_asset")),
            TEXT("asset"),
            TEXT("path"),
            LevelFixture.Loaded.ObjectPath,
            UWorld::StaticClass()->GetPathName()));

    const TSharedPtr<FJsonObject> ExactNativeComponent =
        FSalModule::BuildQueryResult(
            LevelQueryArguments(
                Target,
                LevelExactComponentOperation(
                    LevelGuidText(ComponentFixture.ActorId),
                    TEXT("native"),
                    ComponentFixture.NativeId)));
    TestTrue(
        TEXT("Exact native Component exposes only its actual Class declaration"),
        !LevelHasError(ExactNativeComponent)
            && IsCanonicalRelatedPathTarget(
                ReadUniqueHandoffTarget(
                    ExactNativeComponent,
                    TEXT("inspect_class")),
                TEXT("class"),
                TEXT("path"),
                ComponentFixture.NativeComponent->GetClass()->GetPathName())
            && CountHandoffPurpose(
                ExactNativeComponent,
                TEXT("inspect_blueprint")) == 0
            && CountRelatedTargetDomain(
                ExactNativeComponent,
                TEXT("blueprint")) == 0);

    const TSharedPtr<FJsonObject> ExactSCSComponent =
        FSalModule::BuildQueryResult(
            LevelQueryArguments(
                Target,
                LevelExactComponentOperation(
                    LevelGuidText(ComponentFixture.ActorId),
                    TEXT("scs"),
                    ComponentFixture.SCSId)));
    TestTrue(
        TEXT("Exact SCS Component exposes its actual runtime Class Target"),
        !LevelHasError(ExactSCSComponent)
            && IsCanonicalRelatedPathTarget(
                ReadUniqueHandoffTarget(
                    ExactSCSComponent,
                    TEXT("inspect_class")),
                TEXT("class"),
                TEXT("path"),
                ComponentFixture.SCSComponent->GetClass()->GetPathName()));
    TestTrue(
        TEXT("Exact SCS Component uses its locator-proven declaring Blueprint"),
        IsCanonicalBlueprintTarget(
            ReadUniqueHandoffTarget(
                ExactSCSComponent,
                TEXT("inspect_blueprint")),
            Blueprint->GetPathName(),
            Blueprint->GetBlueprintGuid()));

    for (const TSharedPtr<FJsonObject>& Result : {
             ExactActor,
             ExactNativeComponent,
             ExactSCSComponent})
    {
        TestTrue(
            TEXT("Every successful exact object Query retains the source Asset Target"),
            IsCanonicalRelatedPathTarget(
                ReadUniqueHandoffTarget(
                    Result,
                    TEXT("inspect_asset")),
                TEXT("asset"),
                TEXT("path"),
                LevelFixture.Loaded.ObjectPath,
                UWorld::StaticClass()->GetPathName()));
        TestTrue(
            TEXT("Read-only declaration navigation emits no save or compile handoff"),
            CountHandoffPurpose(Result, TEXT("save")) == 0
                && CountHandoffPurpose(Result, TEXT("compile")) == 0);
    }

    const TSharedPtr<FJsonObject> ContextResult =
        Loomle::EditorContext::FEditorContextService::Get()
            .BuildLevelActorForTesting(
                LevelFixture.Loaded.World,
                ComponentFixture.Actor);
    TSharedPtr<FJsonObject> ValidationError;
    TestTrue(
        TEXT("Selected Level Actor Editor Context is valid SAL Result data"),
        FSalJson::ValidateResult(
            ContextResult,
            ValidationError));
    TestTrue(
        TEXT("Selected Level Actor Editor Context returns the canonical Level Target"),
        !LevelHasError(ContextResult)
            && LevelHasTargetContext(
                ContextResult,
                TEXT("exact_target"))
            && HasCanonicalLevelTarget(
                ContextResult,
                LevelFixture.Loaded.ObjectPath));
    TestTrue(
        TEXT("Selected source Actor projects exactly one ActorGuid StableRef"),
        HasSingleLocalStableRef(
            ContextResult,
            TEXT("actor"),
            LevelGuidText(ComponentFixture.ActorId)));
    TestTrue(
        TEXT("Editor Context does not fabricate result handoffs"),
        CountHandoffPurpose(ContextResult, TEXT("inspect_asset")) == 0
            && CountRelatedTargetDomain(
                ContextResult,
                TEXT("asset")) == 0
            && CountHandoffPurpose(ContextResult, TEXT("inspect_class")) == 0
            && CountRelatedTargetDomain(
                ContextResult,
                TEXT("class")) == 0
            && CountHandoffPurpose(
                ContextResult,
                TEXT("inspect_blueprint")) == 0
            && CountRelatedTargetDomain(
                ContextResult,
                TEXT("blueprint")) == 0);

    TestTrue(
        TEXT("Handoff and Editor Context reads preserve Level and Editor state"),
        LevelInvariant.Verify(*this));
    TestTrue(
        TEXT("Declaration handoffs preserve Component lifecycle and packages"),
        ComponentInvariant.Verify(*this));

    if (TestTrue(
            TEXT("Corrupt Level fixture becomes active for fail-closed Context"),
            LevelFixture.Activate(LevelFixture.Corrupt, Error)))
    {
        const TSharedPtr<FJsonObject> DuplicateContext =
            Loomle::EditorContext::FEditorContextService::Get()
                .BuildLevelActorForTesting(
                    LevelFixture.Corrupt.World,
                    LevelFixture.DuplicateFirst);
        TestTrue(
            TEXT("Duplicate ActorGuid Editor Context fails closed on StableRef projection"),
            HasCanonicalLevelTarget(
                DuplicateContext,
                LevelFixture.Corrupt.ObjectPath)
                && LevelHasDiagnostic(
                    DuplicateContext,
                    TEXT("context.identity_duplicate"))
                && !HasSingleLocalStableRef(
                    DuplicateContext,
                    TEXT("actor"),
                    LevelGuidText(LevelFixture.DuplicateId)));
    }
    else if (!Error.IsEmpty())
    {
        AddError(Error);
    }

    Error.Reset();
    if (!LevelFixture.Cleanup(Error))
    {
        AddError(Error);
    }
    Error.Reset();
    if (!ComponentFixture.Cleanup(Error))
    {
        AddError(Error);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FSalPCGComponentAuthoredQueryTest,
    "Loomle.Sal.PCGComponent.Query.AuthoredTargetSummaryAndBoundaries",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSalPCGComponentAuthoredQueryTest::RunTest(
    const FString& Parameters)
{
    // Keep this declaration order: the Level World must release Component and
    // Graph references before the authored fixture removes its asset records.
    FScopedLevelComponentQueryFixture ComponentFixture;
    FScopedLevelQueryFixture LevelFixture;
    FString Error;
    if (!TestTrue(
            TEXT("PCG Component Level fixture builds"),
            LevelFixture.Build(Error)))
    {
        AddError(Error);
        return false;
    }
    if (!TestTrue(
            TEXT("PCG Component source Level becomes the active Editor World"),
            LevelFixture.Activate(LevelFixture.Loaded, Error)))
    {
        AddError(Error);
        return false;
    }
    if (!TestTrue(
            TEXT("Native and instance PCG Component authored fixture builds"),
            ComponentFixture.Build(LevelFixture, Error)))
    {
        AddError(Error);
        return false;
    }

    const auto AddMatrixComponent =
        [&ComponentFixture](const FName Name)
        {
            UPCGComponent* Component = NewObject<UPCGComponent>(
                ComponentFixture.Actor,
                UPCGComponent::StaticClass(),
                Name,
                RF_Transactional);
            if (Component != nullptr)
            {
                ComponentFixture.Actor->AddInstanceComponent(Component);
                Component->OnComponentCreated();
            }
            return Component;
        };
    UPCGComponent* UnboundPCGComponent = AddMatrixComponent(
        FName(TEXT("LoomleUnboundPCGComponent")));
    UPCGComponent* ParentlessPCGComponent = AddMatrixComponent(
        FName(TEXT("LoomleParentlessPCGComponent")));
    UPCGGraphInstance* ParentlessGraphInstance =
        NewObject<UPCGGraphInstance>(
            ComponentFixture.Actor,
            UPCGGraphInstance::StaticClass(),
            FName(TEXT("LoomleParentlessGraphInstance")),
            RF_Transactional);
    if (ParentlessPCGComponent != nullptr
        && ParentlessGraphInstance != nullptr)
    {
        ParentlessPCGComponent->SetGraphLocal(
            ParentlessGraphInstance);
    }

    UPCGComponent* BoundedPCGComponent = AddMatrixComponent(
        FName(TEXT("LoomleBoundedPCGComponent")));
    constexpr int32 BoundedGraphInstanceCount = 32;
    TArray<UPCGGraphInstance*> BoundedGraphInstances;
    BoundedGraphInstances.Reserve(BoundedGraphInstanceCount);
    for (int32 Index = 0;
         Index < BoundedGraphInstanceCount;
         ++Index)
    {
        const FName InstanceName(*FString::Printf(
            TEXT("LoomleBoundedGraphInstance_%02d"),
            Index));
        BoundedGraphInstances.Add(NewObject<UPCGGraphInstance>(
            ComponentFixture.Actor,
            UPCGGraphInstance::StaticClass(),
            InstanceName,
            RF_Transactional));
    }
    UPCGGraph* SavedGraph =
        ComponentFixture.NativePCGComponent != nullptr
        ? ComponentFixture.NativePCGComponent->GetGraph()
        : nullptr;
    bool bBoundedChainBuilt = SavedGraph != nullptr
        && BoundedGraphInstances.Num() == BoundedGraphInstanceCount
        && !BoundedGraphInstances.Contains(nullptr);
    if (bBoundedChainBuilt)
    {
        for (int32 Index = BoundedGraphInstances.Num() - 1;
             Index >= 0;
             --Index)
        {
            UPCGGraphInterface* Parent = SavedGraph;
            if (Index + 1 < BoundedGraphInstances.Num())
            {
                Parent = BoundedGraphInstances[Index + 1];
            }
            BoundedGraphInstances[Index]->SetGraph(Parent);
        }
        if (BoundedPCGComponent != nullptr)
        {
            BoundedPCGComponent->SetGraphLocal(
                BoundedGraphInstances[0]);
        }
    }

    const auto IsOriginalInstance = [](const UPCGComponent* Component)
    {
        return Component != nullptr
            && Component->CreationMethod
                == EComponentCreationMethod::Instance
            && Component->GetConstOriginalComponent() == Component;
    };
    if (!TestTrue(
            TEXT("The frozen Graph binding matrix fixture builds from public UE APIs"),
            IsOriginalInstance(UnboundPCGComponent)
                && UnboundPCGComponent->GetGraphInstance() != nullptr
                && UnboundPCGComponent->GetGraphInstance()->Graph.Get()
                    == nullptr
                && IsOriginalInstance(ParentlessPCGComponent)
                && ParentlessGraphInstance != nullptr
                && ParentlessGraphInstance->Graph.Get() == nullptr
                && ParentlessPCGComponent->GetGraphInstance() != nullptr
                && ParentlessPCGComponent->GetGraphInstance()->Graph.Get()
                    == ParentlessGraphInstance
                && IsOriginalInstance(BoundedPCGComponent)
                && bBoundedChainBuilt
                && BoundedPCGComponent->GetGraphInstance() != nullptr
                && BoundedPCGComponent->GetGraphInstance()->Graph.Get()
                    == BoundedGraphInstances[0]
                && BoundedPCGComponent->GetGraph() == SavedGraph))
    {
        return false;
    }
    LevelFixture.Loaded.World->GetOutermost()->SetDirtyFlag(false);

    FLevelReadInvariant LevelInvariant(LevelFixture.Loaded.World);
    FLevelComponentReadInvariant InstanceOwnerInvariant(
        ComponentFixture.Actor,
        ComponentFixture.GetBlueprintPackage());
    FLevelComponentReadInvariant NativeOwnerInvariant(
        ComponentFixture.NativePCGActor,
        nullptr);
    const TArray<UPCGComponent*> PCGComponents = {
        ComponentFixture.NativePCGComponent,
        ComponentFixture.PCGComponent,
        UnboundPCGComponent,
        ParentlessPCGComponent,
        BoundedPCGComponent};
    FPCGComponentReadInvariant PCGInvariant(PCGComponents);

    struct FExpectedPCGComponent
    {
        UPCGComponent* Component = nullptr;
        FGuid ActorId;
        FString Source;
        FString Id;
        FString CreationMethod;
        FString GraphBindingKind;
        FString GraphInterfacePath;
        FString GraphInterfaceType;
        FString GraphPath;
        FString GraphType;
        bool bGraphBindingComplete = true;
        bool bSavedGraph = false;
        bool bExpectReferenceWarning = false;
    };
    const TArray<FExpectedPCGComponent> Expected = {
        {
            ComponentFixture.NativePCGComponent,
            ComponentFixture.NativePCGActorId,
            TEXT("native"),
            ComponentFixture.NativePCGId,
            TEXT("Native"),
            TEXT("graph"),
            ComponentFixture.GraphPath,
            UPCGGraph::StaticClass()->GetPathName(),
            ComponentFixture.GraphPath,
            UPCGGraph::StaticClass()->GetPathName(),
            true,
            true
        },
        {
            ComponentFixture.PCGComponent,
            ComponentFixture.ActorId,
            TEXT("instance"),
            ComponentFixture.PCGId,
            TEXT("Instance"),
            TEXT("graph"),
            ComponentFixture.UnsavedGraphPath,
            UPCGGraph::StaticClass()->GetPathName(),
            ComponentFixture.UnsavedGraphPath,
            UPCGGraph::StaticClass()->GetPathName(),
            true,
            false
        },
        {
            UnboundPCGComponent,
            ComponentFixture.ActorId,
            TEXT("instance"),
            UnboundPCGComponent->GetFName().ToString(),
            TEXT("Instance"),
            TEXT("none"),
            FString(),
            FString(),
            FString(),
            FString(),
            true,
            false,
            false
        },
        {
            ParentlessPCGComponent,
            ComponentFixture.ActorId,
            TEXT("instance"),
            ParentlessPCGComponent->GetFName().ToString(),
            TEXT("Instance"),
            TEXT("graph_instance"),
            ParentlessGraphInstance->GetPathName(),
            UPCGGraphInstance::StaticClass()->GetPathName(),
            FString(),
            FString(),
            true,
            false,
            false
        },
        {
            BoundedPCGComponent,
            ComponentFixture.ActorId,
            TEXT("instance"),
            BoundedPCGComponent->GetFName().ToString(),
            TEXT("Instance"),
            TEXT("graph_instance"),
            BoundedGraphInstances[0]->GetPathName(),
            UPCGGraphInstance::StaticClass()->GetPathName(),
            FString(),
            FString(),
            false,
            false,
            true
        }
    };

    for (const FExpectedPCGComponent& Entry : Expected)
    {
        const TSharedRef<FJsonObject> Target = PCGComponentTarget(
            LevelFixture.Loaded.ObjectPath,
            Entry.ActorId,
            Entry.Source,
            Entry.Id);
        const TSharedPtr<FJsonObject> TargetResult =
            FSalModule::BuildQueryResult(
                PCGComponentQueryArguments(
                    Target,
                    LevelOperation(TEXT("target"))));
        const TSharedPtr<FJsonObject> TargetFields =
            CollectLocalMemberFields(
                TargetResult,
                TEXT("pcg_component_scope"));
        FString Source;
        FString CreationMethod;
        FString Asset;
        FString ActorId;
        FString Id;
        FString Name;
        FString Type;
        bool bLoaded = false;
        TestTrue(
            TEXT("Canonical PCG Component target resolves its closed authored fields"),
            !LevelHasError(TargetResult)
                && LevelHasTargetContext(
                    TargetResult,
                    TEXT("exact_target"))
                && HasCanonicalPCGComponentTarget(
                    TargetResult,
                    LevelFixture.Loaded.ObjectPath,
                    Entry.ActorId,
                    Entry.Source,
                    Entry.Id)
                && TargetFields.IsValid()
                && TargetFields->Values.Num() == 8
                && TargetFields->TryGetStringField(TEXT("asset"), Asset)
                && Asset == LevelFixture.Loaded.ObjectPath
                && TargetFields->TryGetStringField(
                    TEXT("actorId"),
                    ActorId)
                && ActorId == LevelGuidText(Entry.ActorId)
                && ReadLevelNameField(
                    TargetFields,
                    TEXT("source"),
                    Source)
                && Source == Entry.Source
                && TargetFields->TryGetStringField(TEXT("id"), Id)
                && Id == Entry.Id
                && TargetFields->TryGetStringField(TEXT("name"), Name)
                && Name == Entry.Component->GetFName().ToString()
                && TargetFields->TryGetStringField(TEXT("type"), Type)
                && Type == UPCGComponent::StaticClass()->GetPathName()
                && ReadLevelNameField(
                    TargetFields,
                    TEXT("CreationMethod"),
                    CreationMethod)
                && CreationMethod == Entry.CreationMethod
                && TargetFields->TryGetBoolField(
                    TEXT("loaded"),
                    bLoaded)
                && bLoaded
                && !TargetFields->HasField(TEXT("registered"))
                && !TargetFields->HasField(TEXT("graph"))
                && !TargetFields->HasField(TEXT("graphInterface")));

        FString OwnerAlias;
        TestTrue(
            TEXT("PCG Component target retains exactly one owning-Level navigation"),
            ReadUniqueHandoffAlias(
                TargetResult,
                TEXT("inspect_level"),
                OwnerAlias)
                && HasRelatedTarget(
                    TargetResult,
                    OwnerAlias,
                    TEXT("level"),
                    LevelFixture.Loaded.ObjectPath,
                    UWorld::StaticClass()->GetPathName())
                && CountHandoffPurpose(TargetResult, TEXT("save")) == 0);

        const TSharedPtr<FJsonObject> SchemaResult =
            FSalModule::BuildQueryResult(
                PCGComponentQueryArguments(
                    Target,
                    LevelOperation(TEXT("target")),
                    true));
        TestTrue(
            TEXT("PCG Component exact schema remains explicitly read-only"),
            !LevelHasError(SchemaResult)
                && LevelHasCommentContaining(
                    SchemaResult,
                    TEXT("read-only"))
                && CollectLocalMemberFields(
                    SchemaResult,
                    TEXT("pcg_component_scope")).IsValid()
                && CountHandoffPurpose(SchemaResult, TEXT("save")) == 0);

        const TSharedPtr<FJsonObject> SummaryResult =
            FSalModule::BuildQueryResult(
                PCGComponentQueryArguments(
                    Target,
                    LevelOperation(TEXT("summary"))));
        const TSharedPtr<FJsonObject> SummaryFields =
            CollectLocalMemberFields(
                SummaryResult,
                TEXT("pcg_component_scope"));
        TSharedPtr<FJsonObject> GraphInterfaceFields;
        TSharedPtr<FJsonObject> GraphFields;
        FString GraphInterfaceKind;
        FString GraphInterfacePath;
        FString GraphInterfaceType;
        FString GraphPath;
        FString GraphType;
        FString GraphBindingKind;
        bool bGraphBindingComplete = false;
        const bool bGraphInterfaceMatches =
            Entry.GraphInterfacePath.IsEmpty()
            ? LevelFieldIsNull(
                SummaryFields,
                TEXT("graphInterface"))
            : ReadNestedObjectField(
                    SummaryFields,
                    TEXT("graphInterface"),
                    GraphInterfaceFields)
                && GraphInterfaceFields->Values.Num() == 3
                && GraphInterfaceFields->TryGetStringField(
                    TEXT("kind"),
                    GraphInterfaceKind)
                && GraphInterfaceKind == Entry.GraphBindingKind
                && GraphInterfaceFields->TryGetStringField(
                    TEXT("path"),
                    GraphInterfacePath)
                && GraphInterfacePath == Entry.GraphInterfacePath
                && GraphInterfaceFields->TryGetStringField(
                    TEXT("type"),
                    GraphInterfaceType)
                && GraphInterfaceType == Entry.GraphInterfaceType;
        const bool bTopGraphMatches = Entry.GraphPath.IsEmpty()
            ? LevelFieldIsNull(SummaryFields, TEXT("graph"))
            : ReadNestedObjectField(
                    SummaryFields,
                    TEXT("graph"),
                    GraphFields)
                && GraphFields->Values.Num() == 2
                && GraphFields->TryGetStringField(
                    TEXT("path"),
                    GraphPath)
                && GraphPath == Entry.GraphPath
                && GraphFields->TryGetStringField(
                    TEXT("type"),
                    GraphType)
                && GraphType == Entry.GraphType;
        TestTrue(
            TEXT("PCG Component summary adds only the frozen bounded Graph binding evidence"),
            !LevelHasError(SummaryResult)
                && HasCanonicalPCGComponentTarget(
                    SummaryResult,
                    LevelFixture.Loaded.ObjectPath,
                    Entry.ActorId,
                    Entry.Source,
                    Entry.Id)
                && SummaryFields.IsValid()
                && SummaryFields->Values.Num() == 12
                && bGraphInterfaceMatches
                && bTopGraphMatches
                && SummaryFields->TryGetStringField(
                    TEXT("graphBindingKind"),
                    GraphBindingKind)
                && GraphBindingKind == Entry.GraphBindingKind
                && SummaryFields->TryGetBoolField(
                    TEXT("graphBindingComplete"),
                    bGraphBindingComplete)
                && bGraphBindingComplete
                    == Entry.bGraphBindingComplete
                && LevelHasDiagnostic(
                    SummaryResult,
                    TEXT("validation.reference_scan_incomplete"))
                    == Entry.bExpectReferenceWarning
                && !SummaryFields->HasField(TEXT("parameters"))
                && !SummaryFields->HasField(TEXT("registered")));

        OwnerAlias.Reset();
        TestTrue(
            TEXT("PCG Component summary always retains its owning Level and never save authority"),
            ReadUniqueHandoffAlias(
                SummaryResult,
                TEXT("inspect_level"),
                OwnerAlias)
                && HasRelatedTarget(
                    SummaryResult,
                    OwnerAlias,
                    TEXT("level"),
                    LevelFixture.Loaded.ObjectPath,
                    UWorld::StaticClass()->GetPathName())
                && CountHandoffPurpose(SummaryResult, TEXT("save")) == 0);
        FString GraphAlias;
        if (Entry.bSavedGraph)
        {
            TestTrue(
                TEXT("A saved top Graph produces one exact inspect_graph handoff"),
                ReadUniqueHandoffAlias(
                    SummaryResult,
                    TEXT("inspect_graph"),
                    GraphAlias)
                    && HasRelatedTarget(
                        SummaryResult,
                        GraphAlias,
                        TEXT("pcg"),
                        Entry.GraphPath,
                        UPCGGraph::StaticClass()->GetPathName()));
        }
        else
        {
            TestEqual(
                TEXT("A non-persistent, absent, or incomplete top Graph gains no Graph handoff"),
                CountHandoffPurpose(
                    SummaryResult,
                    TEXT("inspect_graph")),
                0);
        }
    }

    const TSharedRef<FJsonObject> ParameterTarget = PCGComponentTarget(
        LevelFixture.Loaded.ObjectPath,
        ComponentFixture.NativePCGActorId,
        TEXT("native"),
        ComponentFixture.NativePCGId);
    const TSharedPtr<FJsonObject> ParametersResult =
        FSalModule::BuildQueryResult(
            PCGComponentQueryArguments(
                ParameterTarget,
                LevelOperation(TEXT("parameters"))));
    TestTrue(
        TEXT("A bound Graph with no declarations returns a complete empty Parameter collection"),
        !LevelHasError(ParametersResult)
            && LevelTaggedObjectFields(
                ParametersResult,
                TEXT("parameter")).IsEmpty()
            && LevelHasTargetContext(
                ParametersResult,
                TEXT("exact_target"))
            && HasCanonicalPCGComponentTarget(
                ParametersResult,
                LevelFixture.Loaded.ObjectPath,
                ComponentFixture.NativePCGActorId,
                TEXT("native"),
                ComponentFixture.NativePCGId));

    UPCGGraphInstance* EmptyOwnedGraphInstance =
        ComponentFixture.NativePCGComponent != nullptr
        ? ComponentFixture.NativePCGComponent->GetGraphInstance()
        : nullptr;
    TestNotNull(
        TEXT("Empty-Parameter corruption fixture has a Component-owned GraphInstance"),
        EmptyOwnedGraphInstance);
    if (EmptyOwnedGraphInstance != nullptr)
    {
        const FInstancedPropertyBag EmptyBagBefore =
            EmptyOwnedGraphInstance->ParametersOverrides.Parameters;
        EmptyOwnedGraphInstance->ParametersOverrides.Parameters.Reset();
        const TSharedPtr<FJsonObject> InvalidEmptyBagResult =
            FSalModule::BuildQueryResult(
                PCGComponentQueryArguments(
                    ParameterTarget,
                    LevelOperation(TEXT("parameters"))));
        TestTrue(
            TEXT("A bound zero-declaration GraphInstance with invalid storage fails closed"),
            LevelHasDiagnostic(
                InvalidEmptyBagResult,
                TEXT("validation.reference_scan_incomplete"))
                && LevelTaggedObjectFields(
                    InvalidEmptyBagResult,
                    TEXT("parameter")).IsEmpty());
        const TSharedPtr<FJsonObject> InvalidEmptyBagExactResult =
            FSalModule::BuildQueryResult(
                PCGComponentQueryArguments(
                    ParameterTarget,
                    PCGComponentExactParameterOperation(
                        FGuid(
                            0x7f000001,
                            0x7f000002,
                            0x7f000003,
                            0x7f000004)),
                    true));
        TestTrue(
            TEXT("Invalid zero-declaration storage also fails exact Parameter schema Query closed"),
            LevelHasDiagnostic(
                InvalidEmptyBagExactResult,
                TEXT("validation.reference_scan_incomplete"))
                && LevelTaggedObjectFields(
                    InvalidEmptyBagExactResult,
                    TEXT("parameter")).IsEmpty());
        EmptyOwnedGraphInstance->ParametersOverrides.Parameters =
            EmptyBagBefore;
    }
    if (EmptyOwnedGraphInstance != nullptr)
    {
        const TSet<FGuid> OverrideIdsBefore =
            EmptyOwnedGraphInstance
                ->ParametersOverrides.PropertiesIDsOverridden;
        const FGuid StaleOverrideId(
            0x7e000001,
            0x7e000002,
            0x7e000003,
            0x7e000004);
        TestFalse(
            TEXT("Zero-declaration fixture begins without the hostile override Guid"),
            OverrideIdsBefore.Contains(StaleOverrideId));
        EmptyOwnedGraphInstance
            ->ParametersOverrides.PropertiesIDsOverridden.Add(
                StaleOverrideId);

        FPCGParameterReadInvariant StaleOverrideInvariant({
            EmptyOwnedGraphInstance});
        const TSharedPtr<FJsonObject> StaleOverrideCollection =
            FSalModule::BuildQueryResult(
                PCGComponentQueryArguments(
                    ParameterTarget,
                    LevelOperation(TEXT("parameters"))));
        TestTrue(
            TEXT("A zero-declaration GraphInstance with a stale override bit fails collection readback closed"),
            LevelHasDiagnostic(
                StaleOverrideCollection,
                TEXT("validation.reference_scan_incomplete"))
                && LevelTaggedObjectFields(
                    StaleOverrideCollection,
                    TEXT("parameter")).IsEmpty());

        const TSharedPtr<FJsonObject> StaleOverrideExact =
            FSalModule::BuildQueryResult(
                PCGComponentQueryArguments(
                    ParameterTarget,
                    PCGComponentExactParameterOperation(
                        StaleOverrideId),
                    true));
        TestTrue(
            TEXT("A stale override bit cannot leak through exact Parameter schema Query"),
            LevelHasDiagnostic(
                StaleOverrideExact,
                TEXT("validation.reference_scan_incomplete"))
                && LevelTaggedObjectFields(
                    StaleOverrideExact,
                    TEXT("parameter")).IsEmpty());
        TestTrue(
            TEXT("Stale-override rejection preserves the corrupt evidence for deterministic diagnosis"),
            StaleOverrideInvariant.Verify(*this));

        EmptyOwnedGraphInstance
            ->ParametersOverrides.PropertiesIDsOverridden =
                OverrideIdsBefore;
        const TSharedPtr<FJsonObject> RestoredEmptyParameters =
            FSalModule::BuildQueryResult(
                PCGComponentQueryArguments(
                    ParameterTarget,
                    LevelOperation(TEXT("parameters"))));
        TestTrue(
            TEXT("Restoring the hostile override set restores complete empty Parameter readback"),
            !LevelHasError(RestoredEmptyParameters)
                && LevelTaggedObjectFields(
                    RestoredEmptyParameters,
                    TEXT("parameter")).IsEmpty());
    }

    const TSharedRef<FJsonObject> WrongTypeTarget = PCGComponentTarget(
        LevelFixture.Loaded.ObjectPath,
        ComponentFixture.NativePCGActorId,
        TEXT("native"),
        ComponentFixture.NativePCGId,
        USceneComponent::StaticClass()->GetPathName());
    const TSharedPtr<FJsonObject> WrongTypeResult =
        FSalModule::BuildQueryResult(
            PCGComponentQueryArguments(
                WrongTypeTarget,
                LevelOperation(TEXT("target"))));
    TestTrue(
        TEXT("A PCG Component Target with the wrong exact native Class fails closed"),
        LevelHasDiagnostic(
            WrongTypeResult,
            TEXT("validation.invalid_target"))
            && LevelHasTargetContext(
                WrongTypeResult,
                TEXT("unresolved_target")));

    const TSharedRef<FJsonObject> NonPCGTarget = PCGComponentTarget(
        LevelFixture.Loaded.ObjectPath,
        ComponentFixture.ActorId,
        TEXT("instance"),
        ComponentFixture.InstanceId,
        USceneComponent::StaticClass()->GetPathName());
    const TSharedPtr<FJsonObject> NonPCGResult =
        FSalModule::BuildQueryResult(
            PCGComponentQueryArguments(
                NonPCGTarget,
                LevelOperation(TEXT("target"))));
    TestTrue(
        TEXT("A durable non-PCG Component slot cannot enter the specialized Domain"),
        LevelHasError(NonPCGResult)
            && LevelHasTargetContext(
                NonPCGResult,
                TEXT("unresolved_target"))
            && !LevelHasDiagnostic(
                NonPCGResult,
                TEXT("capability.component_owner_not_loaded")));

    UWorld* EditorWorldBefore = GEditor != nullptr
        ? GEditor->GetEditorWorldContext().World()
        : nullptr;
    const int32 SelectedActorsBefore = GEditor != nullptr
        ? GEditor->GetSelectedActorCount()
        : -1;
    const int32 SelectedComponentsBefore = GEditor != nullptr
        ? GEditor->GetSelectedComponentCount()
        : -1;
    const int32 UndoBefore = GEditor != nullptr && GEditor->Trans != nullptr
        ? GEditor->Trans->GetUndoCount()
        : -1;
    TestTrue(
        TEXT("Unloaded PCG Component fixture begins with no loaded source map"),
        LevelFixture.IsUnloadedMapStillUnloaded());
    const TSharedRef<FJsonObject> UnloadedTarget = PCGComponentTarget(
        LevelFixture.Unloaded.ObjectPath,
        LevelFixture.UnloadedPCGActorId,
        TEXT("native"),
        LevelFixture.UnloadedPCGId);
    const TSharedPtr<FJsonObject> UnloadedResult =
        FSalModule::BuildQueryResult(
            PCGComponentQueryArguments(
                UnloadedTarget,
                LevelOperation(TEXT("target"))));
    TestTrue(
        TEXT("An unloaded source Level fails before Component lookup without loading or pinning its map"),
        LevelHasDiagnostic(
            UnloadedResult,
            TEXT("capability.level_not_loaded"))
            && LevelHasTargetContext(
                UnloadedResult,
                TEXT("unresolved_target"))
            && LevelFixture.IsUnloadedMapStillUnloaded()
            && (GEditor == nullptr
                || (GEditor->GetEditorWorldContext().World()
                        == EditorWorldBefore
                    && GEditor->GetSelectedActorCount()
                        == SelectedActorsBefore
                    && GEditor->GetSelectedComponentCount()
                        == SelectedComponentsBefore
                    && (GEditor->Trans == nullptr
                        || GEditor->Trans->GetUndoCount()
                            == UndoBefore))));

    const TSharedPtr<FJsonObject> PatchResult =
        FSalModule::BuildPatchResult(
            PCGComponentPatchArguments(
                PCGComponentTarget(
                    LevelFixture.Loaded.ObjectPath,
                    ComponentFixture.NativePCGActorId,
                    TEXT("native"),
                    ComponentFixture.NativePCGId)));
    TestTrue(
        TEXT("PCG Component Patch is rejected before native target resolution"),
        LevelHasDiagnostic(
            PatchResult,
            TEXT("language.invalid_object_shape"))
            && !LevelHasDiagnostic(
                PatchResult,
                TEXT("capability.component_owner_not_loaded"))
            && !LevelHasDiagnostic(
                PatchResult,
                TEXT("resolution.object_not_found")));

    TestTrue(
        TEXT("All specialized PCG Component reads preserve Level and Editor state"),
        LevelInvariant.Verify(*this));
    TestTrue(
        TEXT("All specialized reads preserve the instance owner lifecycle"),
        InstanceOwnerInvariant.Verify(*this));
    TestTrue(
        TEXT("All specialized reads preserve the native owner lifecycle"),
        NativeOwnerInvariant.Verify(*this));
    TestTrue(
        TEXT("All specialized reads preserve PCG authored and runtime state"),
        PCGInvariant.Verify(*this));

    Error.Reset();
    if (!LevelFixture.Cleanup(Error))
    {
        AddError(Error);
    }
    Error.Reset();
    if (!ComponentFixture.Cleanup(Error))
    {
        AddError(Error);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FSalPCGComponentParameterReadbackTest,
    "Loomle.Sal.PCGComponent.Query.ParameterReadbackAndBoundaries",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSalPCGComponentParameterReadbackTest::RunTest(
    const FString& Parameters)
{
    // Keep this declaration order: the Level releases Component and Graph
    // references before the authored fixture removes its asset records.
    FScopedLevelComponentQueryFixture ComponentFixture;
    FScopedLevelQueryFixture LevelFixture;
    FString Error;
    if (!TestTrue(
            TEXT("PCG Parameter Level fixture builds"),
            LevelFixture.Build(Error)))
    {
        AddError(Error);
        return false;
    }
    if (!TestTrue(
            TEXT("PCG Parameter source Level becomes the active Editor World"),
            LevelFixture.Activate(LevelFixture.Loaded, Error)))
    {
        AddError(Error);
        return false;
    }
    if (!TestTrue(
            TEXT("PCG Parameter Component fixture builds"),
            ComponentFixture.Build(LevelFixture, Error)))
    {
        AddError(Error);
        return false;
    }

    FScopedPCGComponentParameterFixture ParameterFixture;
    if (!TestTrue(
            TEXT("PCG Parameter GraphInstance fixture builds through public UE APIs"),
            ParameterFixture.Build(
                LevelFixture,
                ComponentFixture,
                Error)))
    {
        AddError(Error);
        return false;
    }

    const TSharedRef<FJsonObject> Target = PCGComponentTarget(
        LevelFixture.Loaded.ObjectPath,
        ComponentFixture.ActorId,
        TEXT("instance"),
        ComponentFixture.PCGId);
    const TSharedRef<FJsonObject> DeepTarget = PCGComponentTarget(
        LevelFixture.Loaded.ObjectPath,
        ComponentFixture.ActorId,
        TEXT("instance"),
        ParameterFixture.DeepComponent->GetFName().ToString());

    FLevelReadInvariant LevelInvariant(LevelFixture.Loaded.World);
    FLevelComponentReadInvariant OwnerInvariant(
        ComponentFixture.Actor,
        ComponentFixture.GetBlueprintPackage());
    FPCGComponentReadInvariant ComponentInvariant({
        ParameterFixture.Component,
        ParameterFixture.DeepComponent});
    FPCGParameterReadInvariant ParameterInvariant(
        ParameterFixture.GetParameterInterfaces());

    const TSharedPtr<FJsonObject> CollectionResult =
        FSalModule::BuildQueryResult(
            PCGComponentQueryArguments(
                Target,
                LevelOperation(TEXT("parameters"))));
    const TArray<TSharedPtr<FJsonObject>> ParameterFields =
        LevelTaggedObjectFields(CollectionResult, TEXT("parameter"));
    TestTrue(
        TEXT("Parameter collection resolves the exact authored PCG Component"),
        !LevelHasError(CollectionResult)
            && LevelHasTargetContext(
                CollectionResult,
                TEXT("exact_target"))
            && HasCanonicalPCGComponentTarget(
                CollectionResult,
                LevelFixture.Loaded.ObjectPath,
                ComponentFixture.ActorId,
                TEXT("instance"),
                ComponentFixture.PCGId));

    struct FExpectedParameter
    {
        FGuid Id;
        FString Name;
        FString ValueType;
        TArray<FString> ContainerTypes;
        FString ValueTypeObject;
        bool bOverridden = false;
        FString EffectiveSource;
        FString ValueStatus;
    };
    const TArray<FExpectedParameter> ExpectedParameters = {
        {
            FScopedPCGComponentParameterFixture::GraphDefaultId,
            FScopedPCGComponentParameterFixture::GraphDefaultName.ToString(),
            TEXT("double"),
            {},
            FString(),
            false,
            TEXT("graph_default"),
            TEXT("available")
        },
        {
            FScopedPCGComponentParameterFixture::ParentOverrideId,
            FScopedPCGComponentParameterFixture::ParentOverrideName.ToString(),
            TEXT("int64"),
            {},
            FString(),
            false,
            TEXT("parent_instance"),
            TEXT("available")
        },
        {
            FScopedPCGComponentParameterFixture::ComponentOverrideId,
            FScopedPCGComponentParameterFixture::ComponentOverrideName.ToString(),
            TEXT("string"),
            {},
            FString(),
            true,
            TEXT("component_override"),
            TEXT("available")
        },
        {
            FScopedPCGComponentParameterFixture::SoftObjectId,
            FScopedPCGComponentParameterFixture::SoftObjectName.ToString(),
            TEXT("soft_object"),
            {},
            UWorld::StaticClass()->GetPathName(),
            false,
            TEXT("graph_default"),
            TEXT("unsupported")
        },
        {
            FScopedPCGComponentParameterFixture::UnsupportedStructId,
            FScopedPCGComponentParameterFixture::UnsupportedStructName.ToString(),
            TEXT("struct"),
            {},
            TBaseStructure<FVector>::Get()->GetPathName(),
            false,
            TEXT("graph_default"),
            TEXT("unsupported")
        },
        {
            FScopedPCGComponentParameterFixture::UnsupportedArrayId,
            FScopedPCGComponentParameterFixture::UnsupportedArrayName.ToString(),
            TEXT("double"),
            {TEXT("array")},
            FString(),
            false,
            TEXT("graph_default"),
            TEXT("unsupported")
        },
#if UE_VERSION_NEWER_THAN_OR_EQUAL(5, 8, 0)
        {
            FScopedPCGComponentParameterFixture::UnsupportedInt8Id,
            FScopedPCGComponentParameterFixture::UnsupportedInt8Name.ToString(),
            TEXT("int8"),
            {},
            FString(),
            false,
            TEXT("graph_default"),
            TEXT("unsupported")
        },
        {
            FScopedPCGComponentParameterFixture::UnsupportedInt16Id,
            FScopedPCGComponentParameterFixture::UnsupportedInt16Name.ToString(),
            TEXT("int16"),
            {},
            FString(),
            false,
            TEXT("graph_default"),
            TEXT("unsupported")
        },
        {
            FScopedPCGComponentParameterFixture::UnsupportedUInt16Id,
            FScopedPCGComponentParameterFixture::UnsupportedUInt16Name.ToString(),
            TEXT("uint16"),
            {},
            FString(),
            false,
            TEXT("graph_default"),
            TEXT("unsupported")
        },
#endif
        {
            FScopedPCGComponentParameterFixture::EnumId,
            FScopedPCGComponentParameterFixture::EnumName.ToString(),
            TEXT("enum"),
            {},
            StaticEnum<EPropertyBagPropertyType>()->GetPathName(),
            false,
            TEXT("graph_default"),
            TEXT("available")
        }
    };
    TestEqual(
        TEXT("Parameter collection returns every canonical Graph descriptor"),
        ParameterFields.Num(),
        ExpectedParameters.Num());
    for (int32 Index = 0;
         Index < FMath::Min(
             ParameterFields.Num(),
             ExpectedParameters.Num());
         ++Index)
    {
        const FExpectedParameter& Expected = ExpectedParameters[Index];
        const TSharedPtr<FJsonObject>& Fields = ParameterFields[Index];
        FString Id;
        FString Name;
        TSharedPtr<FJsonObject> TypeFields;
        FString ValueType;
        TArray<FString> ContainerTypes;
        FString ValueTypeObject;
        FString EffectiveSource;
        FString ValueStatus;
        bool bOverridden = false;
        bool bStableRefAvailable = false;
        TArray<FString> IdentityPath;
        TestTrue(
            *FString::Printf(
                TEXT("Parameter %d preserves the closed descriptor, value-status, source, and StableRef fields"),
                Index),
            Fields.IsValid()
                && Fields->Values.Num()
                    == (Expected.bOverridden
                        && Expected.ValueStatus == TEXT("available")
                        ? 10
                        : 9)
                && Fields->TryGetStringField(TEXT("id"), Id)
                && Id == LevelGuidText(Expected.Id)
                && Fields->TryGetStringField(TEXT("name"), Name)
                && Name == Expected.Name
                && ReadNestedObjectField(
                    Fields,
                    TEXT("type"),
                    TypeFields)
                && TypeFields->Values.Num() == 3
                && ReadLevelNameField(
                    TypeFields,
                    TEXT("valueType"),
                    ValueType)
                && ValueType == Expected.ValueType
                && ReadLevelNameArrayField(
                    TypeFields,
                    TEXT("containerTypes"),
                    ContainerTypes)
                && ContainerTypes == Expected.ContainerTypes
                && (Expected.ValueTypeObject.IsEmpty()
                    ? LevelFieldIsNull(
                        TypeFields,
                        TEXT("valueTypeObject"))
                    : TypeFields->TryGetStringField(
                            TEXT("valueTypeObject"),
                            ValueTypeObject)
                        && ValueTypeObject == Expected.ValueTypeObject)
                && ReadLevelNameField(
                    Fields,
                    TEXT("valueStatus"),
                    ValueStatus)
                && ValueStatus == Expected.ValueStatus
                && Fields->TryGetBoolField(TEXT("overridden"), bOverridden)
                && bOverridden == Expected.bOverridden
                && ReadLevelNameField(
                    Fields,
                    TEXT("effectiveSource"),
                    EffectiveSource)
                && EffectiveSource == Expected.EffectiveSource
                && Fields->TryGetBoolField(
                    TEXT("stableRefAvailable"),
                    bStableRefAvailable)
                && bStableRefAvailable
                && ReadLevelStableRefField(
                    Fields,
                    TEXT("ref"),
                    TEXT("parameter"),
                    IdentityPath)
                && IdentityPath.Num() == 1
                && IdentityPath[0] == LevelGuidText(Expected.Id));
    }
    TArray<FGuid> UnsupportedIds = {
        FScopedPCGComponentParameterFixture::SoftObjectId,
        FScopedPCGComponentParameterFixture::UnsupportedStructId,
        FScopedPCGComponentParameterFixture::UnsupportedArrayId};
#if UE_VERSION_NEWER_THAN_OR_EQUAL(5, 8, 0)
    UnsupportedIds.Add(
        FScopedPCGComponentParameterFixture::UnsupportedInt8Id);
    UnsupportedIds.Add(
        FScopedPCGComponentParameterFixture::UnsupportedInt16Id);
    UnsupportedIds.Add(
        FScopedPCGComponentParameterFixture::UnsupportedUInt16Id);
#endif
    for (const FGuid& UnsupportedId : UnsupportedIds)
    {
        const TSharedPtr<FJsonObject> UnsupportedFields =
            FindLevelFieldsById(
                ParameterFields,
                LevelGuidText(UnsupportedId));
        FString ValueStatus;
        TestTrue(
            TEXT("Descriptor-only Parameter remains readable without projecting a value"),
            UnsupportedFields.IsValid()
                && ReadLevelNameField(
                    UnsupportedFields,
                    TEXT("valueStatus"),
                    ValueStatus)
                && ValueStatus == TEXT("unsupported")
                && LevelFieldIsNull(
                    UnsupportedFields,
                    TEXT("effectiveValue"))
                && !UnsupportedFields->HasField(TEXT("localValue")));
    }

    const TSharedPtr<FJsonObject> GraphDefaultFields =
        FindLevelFieldsById(
            ParameterFields,
            LevelGuidText(
                FScopedPCGComponentParameterFixture::GraphDefaultId));
    const TSharedPtr<FJsonObject> ParentOverrideFields =
        FindLevelFieldsById(
            ParameterFields,
            LevelGuidText(
                FScopedPCGComponentParameterFixture::ParentOverrideId));
    const TSharedPtr<FJsonObject> ComponentOverrideFields =
        FindLevelFieldsById(
            ParameterFields,
            LevelGuidText(
                FScopedPCGComponentParameterFixture::ComponentOverrideId));
    const TSharedPtr<FJsonObject> EnumFields =
        FindLevelFieldsById(
            ParameterFields,
            LevelGuidText(
                FScopedPCGComponentParameterFixture::EnumId));
    double GraphDefaultValue = 0.0;
    FString ParentValue;
    FString LocalValue;
    FString EffectiveLocalValue;
    TestTrue(
        TEXT("Graph default Double remains a lossless JSON number without a local value"),
        GraphDefaultFields.IsValid()
            && GraphDefaultFields->TryGetNumberField(
                TEXT("effectiveValue"),
                GraphDefaultValue)
            && FMath::IsNearlyEqual(
                GraphDefaultValue,
                FScopedPCGComponentParameterFixture::GraphDefaultValue)
            && !GraphDefaultFields->HasField(TEXT("localValue")));
    TestTrue(
        TEXT("Inherited Int64 outside JSON's exact range remains decimal text"),
        ParentOverrideFields.IsValid()
            && ParentOverrideFields->TryGetStringField(
                TEXT("effectiveValue"),
                ParentValue)
            && ParentValue
                == LexToString(
                    FScopedPCGComponentParameterFixture::ParentOverrideValue)
            && !ParentOverrideFields->HasField(TEXT("localValue")));
    TestTrue(
        TEXT("Component override exposes matching local and effective String values"),
        ComponentOverrideFields.IsValid()
            && ComponentOverrideFields->TryGetStringField(
                TEXT("localValue"),
                LocalValue)
            && LocalValue
                == FScopedPCGComponentParameterFixture::ComponentOverrideValue
            && ComponentOverrideFields->TryGetStringField(
                TEXT("effectiveValue"),
                EffectiveLocalValue)
            && EffectiveLocalValue == LocalValue);
    TSharedPtr<FJsonObject> EnumValueFields;
    FString EnumType;
    FString EnumName;
    FString EnumNumber;
    TestTrue(
        TEXT("Enum readback preserves its resolved type, authored name, and exact decimal value"),
        EnumFields.IsValid()
            && ReadNestedObjectField(
                EnumFields,
                TEXT("effectiveValue"),
                EnumValueFields)
            && EnumValueFields->Values.Num() == 3
            && EnumValueFields->TryGetStringField(TEXT("type"), EnumType)
            && EnumType
                == StaticEnum<EPropertyBagPropertyType>()->GetPathName()
            && EnumValueFields->TryGetStringField(TEXT("name"), EnumName)
            && EnumName == TEXT("Double")
            && EnumValueFields->TryGetStringField(TEXT("value"), EnumNumber)
            && EnumNumber == LexToString(
                static_cast<int64>(EPropertyBagPropertyType::Double)));

    for (const FExpectedParameter& Expected : ExpectedParameters)
    {
        const TSharedPtr<FJsonObject> ExactResult =
            FSalModule::BuildQueryResult(
                PCGComponentQueryArguments(
                    Target,
                    PCGComponentExactParameterOperation(Expected.Id)));
        const TArray<TSharedPtr<FJsonObject>> ExactFields =
            LevelTaggedObjectFields(ExactResult, TEXT("parameter"));
        TestTrue(
            TEXT("Exact Parameter StableRef resolves by descriptor Guid"),
            !LevelHasError(ExactResult)
                && ExactFields.Num() == 1
                && FindLevelFieldsById(
                    ExactFields,
                    LevelGuidText(Expected.Id)).IsValid());
    }
    const TSharedPtr<FJsonObject> ExactSchemaResult =
        FSalModule::BuildQueryResult(
            PCGComponentQueryArguments(
                Target,
                PCGComponentExactParameterOperation(
                    FScopedPCGComponentParameterFixture::GraphDefaultId),
                true));
    TestTrue(
        TEXT("Exact Parameter Query accepts the frozen read-only schema detail"),
        !LevelHasError(ExactSchemaResult)
            && LevelTaggedObjectFields(
                ExactSchemaResult,
                TEXT("parameter")).Num() == 1);

    const TSharedPtr<FJsonObject> RemovedResult =
        FSalModule::BuildQueryResult(
            PCGComponentQueryArguments(
                Target,
                PCGComponentExactParameterOperation(
                    FScopedPCGComponentParameterFixture::RemovedId)));
    TestTrue(
        TEXT("A removed descriptor Guid fails exact Parameter resolution closed"),
        LevelHasDiagnostic(
            RemovedResult,
            TEXT("resolution.object_not_found"))
            && LevelTaggedObjectFields(
                RemovedResult,
                TEXT("parameter")).IsEmpty());
    const TSharedPtr<FJsonObject> UnsupportedExactResult =
        FSalModule::BuildQueryResult(
            PCGComponentQueryArguments(
                Target,
                PCGComponentExactParameterOperation(
                    FScopedPCGComponentParameterFixture::UnsupportedStructId)));
    const TArray<TSharedPtr<FJsonObject>> UnsupportedExactFields =
        LevelTaggedObjectFields(
            UnsupportedExactResult,
            TEXT("parameter"));
    FString UnsupportedExactStatus;
    TestTrue(
        TEXT("Exact unsupported Parameter returns its full descriptor-only projection"),
        !LevelHasError(UnsupportedExactResult)
            && UnsupportedExactFields.Num() == 1
            && ReadLevelNameField(
                UnsupportedExactFields[0],
                TEXT("valueStatus"),
                UnsupportedExactStatus)
            && UnsupportedExactStatus == TEXT("unsupported")
            && LevelFieldIsNull(
                UnsupportedExactFields[0],
                TEXT("effectiveValue"))
            && !UnsupportedExactFields[0]->HasField(TEXT("localValue")));

    const TSharedPtr<FJsonObject> RenamedSearchResult =
        FSalModule::BuildQueryResult(
            PCGComponentQueryArguments(
                Target,
                LevelOperation(
                    TEXT("parameters"),
                    TEXT("ComponentOverride"))));
    const TArray<TSharedPtr<FJsonObject>> RenamedSearchFields =
        LevelTaggedObjectFields(
            RenamedSearchResult,
            TEXT("parameter"));
    TestTrue(
        TEXT("Parameter search uses the current Graph declaration name"),
        !LevelHasError(RenamedSearchResult)
            && RenamedSearchFields.Num() == 1
            && FindLevelFieldsById(
                RenamedSearchFields,
                LevelGuidText(
                    FScopedPCGComponentParameterFixture::ComponentOverrideId))
                    .IsValid());
    const TSharedPtr<FJsonObject> OldNameSearchResult =
        FSalModule::BuildQueryResult(
            PCGComponentQueryArguments(
                Target,
                LevelOperation(
                    TEXT("parameters"),
                    FScopedPCGComponentParameterFixture::RenamedFromName
                        .ToString())));
    TestTrue(
        TEXT("Parameter search does not retain a stale pre-rename name"),
        !LevelHasError(OldNameSearchResult)
            && LevelTaggedObjectFields(
                OldNameSearchResult,
                TEXT("parameter")).IsEmpty());

    const TSharedPtr<FJsonObject> FirstPageResult =
        FSalModule::BuildQueryResult(
            PCGComponentQueryArguments(
                Target,
                LevelOperation(TEXT("parameters")),
                false,
                2));
    const TArray<TSharedPtr<FJsonObject>> FirstPageFields =
        LevelTaggedObjectFields(
            FirstPageResult,
            TEXT("parameter"));
    FString FirstPageFirstId;
    FString FirstPageSecondId;
    FString Cursor;
    TestTrue(
        TEXT("First Parameter page preserves descriptor order and returns a cursor"),
        !LevelHasError(FirstPageResult)
            && FirstPageFields.Num() == 2
            && FirstPageFields[0]->TryGetStringField(
                TEXT("id"),
                FirstPageFirstId)
            && FirstPageFirstId == LevelGuidText(
                FScopedPCGComponentParameterFixture::GraphDefaultId)
            && FirstPageFields[1]->TryGetStringField(
                TEXT("id"),
                FirstPageSecondId)
            && FirstPageSecondId == LevelGuidText(
                FScopedPCGComponentParameterFixture::ParentOverrideId)
            && ReadLevelNextCursor(FirstPageResult, Cursor));
    if (!Cursor.IsEmpty())
    {
        const TSharedPtr<FJsonObject> SecondPageResult =
            FSalModule::BuildQueryResult(
                PCGComponentQueryArguments(
                    Target,
                    LevelOperation(TEXT("parameters")),
                    false,
                    2,
                    Cursor));
        const TArray<TSharedPtr<FJsonObject>> SecondPageFields =
            LevelTaggedObjectFields(
                SecondPageResult,
                TEXT("parameter"));
        FString SecondPageFirstId;
        FString SecondPageSecondId;
        TestTrue(
            TEXT("Second Parameter page resumes without duplicates or skips"),
            !LevelHasError(SecondPageResult)
                && SecondPageFields.Num() == 2
                && SecondPageFields[0]->TryGetStringField(
                    TEXT("id"),
                    SecondPageFirstId)
                && SecondPageFirstId == LevelGuidText(
                    FScopedPCGComponentParameterFixture::ComponentOverrideId)
                && SecondPageFields[1]->TryGetStringField(
                    TEXT("id"),
                    SecondPageSecondId)
                && SecondPageSecondId == LevelGuidText(
                    FScopedPCGComponentParameterFixture::SoftObjectId));

        const TSharedPtr<FJsonObject> ChangedSearchCursorResult =
            FSalModule::BuildQueryResult(
                PCGComponentQueryArguments(
                    Target,
                    LevelOperation(TEXT("parameters"), TEXT("Override")),
                    false,
                    2,
                    Cursor));
        TestTrue(
            TEXT("A Parameter cursor is bound to its exact search"),
            LevelHasDiagnostic(
                ChangedSearchCursorResult,
                TEXT("validation.invalid_cursor")));
    }

    const TSharedPtr<FJsonObject> DeepCollectionResult =
        FSalModule::BuildQueryResult(
            PCGComponentQueryArguments(
                DeepTarget,
                LevelOperation(TEXT("parameters"))));
    TestTrue(
        TEXT("An incomplete GraphInstance chain fails Parameter collection closed"),
        LevelHasDiagnostic(
            DeepCollectionResult,
            TEXT("validation.reference_scan_incomplete"))
            && LevelTaggedObjectFields(
                DeepCollectionResult,
                TEXT("parameter")).IsEmpty());
    const TSharedPtr<FJsonObject> DeepExactResult =
        FSalModule::BuildQueryResult(
            PCGComponentQueryArguments(
                DeepTarget,
                PCGComponentExactParameterOperation(
                    FScopedPCGComponentParameterFixture::GraphDefaultId)));
    TestTrue(
        TEXT("An incomplete GraphInstance chain fails exact Parameter readback closed"),
        LevelHasDiagnostic(
            DeepExactResult,
            TEXT("validation.reference_scan_incomplete"))
            && LevelTaggedObjectFields(
                DeepExactResult,
                TEXT("parameter")).IsEmpty());

    UWorld* EditorWorldBefore = GEditor != nullptr
        ? GEditor->GetEditorWorldContext().World()
        : nullptr;
    const int32 UndoBefore = GEditor != nullptr && GEditor->Trans != nullptr
        ? GEditor->Trans->GetUndoCount()
        : -1;
    TestTrue(
        TEXT("Unloaded Parameter evidence begins with no loaded source map"),
        LevelFixture.IsUnloadedMapStillUnloaded());
    const TSharedPtr<FJsonObject> UnloadedParametersResult =
        FSalModule::BuildQueryResult(
            PCGComponentQueryArguments(
                PCGComponentTarget(
                    LevelFixture.Unloaded.ObjectPath,
                    LevelFixture.UnloadedPCGActorId,
                    TEXT("native"),
                    LevelFixture.UnloadedPCGId),
                LevelOperation(TEXT("parameters"))));
    TestTrue(
        TEXT("Parameter readback never loads its source Level or changes Editor state"),
        LevelHasDiagnostic(
            UnloadedParametersResult,
            TEXT("capability.level_not_loaded"))
            && LevelFixture.IsUnloadedMapStillUnloaded()
            && (GEditor == nullptr
                || (GEditor->GetEditorWorldContext().World()
                        == EditorWorldBefore
                    && (GEditor->Trans == nullptr
                        || GEditor->Trans->GetUndoCount()
                            == UndoBefore))));

    TestTrue(
        TEXT("All PCG Parameter reads preserve Level and Editor state"),
        LevelInvariant.Verify(*this));
    TestTrue(
        TEXT("All PCG Parameter reads preserve Component owner state"),
        OwnerInvariant.Verify(*this));
    TestTrue(
        TEXT("All PCG Parameter reads preserve PCG runtime state"),
        ComponentInvariant.Verify(*this));
    TestTrue(
        TEXT("All PCG Parameter reads preserve bags, override bits, and delegates"),
        ParameterInvariant.Verify(*this));

    // Cursor staleness is a separate read-invariant scope because this native
    // authored change is deliberate fixture setup, not Query behavior.
    if (!Cursor.IsEmpty())
    {
        const FPropertyBagPropertyDesc* OwnedDesc =
            ParameterFixture.OwnedGraphInstance
                ->GetUserParametersStruct()
                ->FindPropertyDescByID(
                    FScopedPCGComponentParameterFixture::ComponentOverrideId);
        TestNotNull(
            TEXT("Cursor-stale fixture reacquires the owned descriptor by Guid"),
            OwnedDesc);
        if (OwnedDesc != nullptr)
        {
            TestEqual(
                TEXT("Cursor-stale fixture changes one local override through native PCG API"),
                ParameterFixture.OwnedGraphInstance
                    ->SetGraphParameter<FString>(
                        OwnedDesc->Name,
                        FScopedPCGComponentParameterFixture::
                            StaleComponentOverrideValue),
                EPropertyBagResult::Success);
            ParameterFixture.ClearSetupDirtyFlags();
            FPCGComponentReadInvariant StaleComponentInvariant({
                ParameterFixture.Component});
            FPCGParameterReadInvariant StaleParameterInvariant(
                ParameterFixture.GetParameterInterfaces());
            const TSharedPtr<FJsonObject> StaleCursorResult =
                FSalModule::BuildQueryResult(
                    PCGComponentQueryArguments(
                        Target,
                        LevelOperation(TEXT("parameters")),
                        false,
                        2,
                        Cursor));
            TestTrue(
                TEXT("A Parameter cursor expires after an effective value changes"),
                LevelHasDiagnostic(
                    StaleCursorResult,
                    TEXT("validation.invalid_cursor")));
            TestTrue(
                TEXT("Stale-cursor rejection preserves PCG Component runtime state"),
                StaleComponentInvariant.Verify(*this));
            TestTrue(
                TEXT("Stale-cursor rejection preserves current bags and override bits"),
                StaleParameterInvariant.Verify(*this));

            TestEqual(
                TEXT("Floating cursor fixture restores the original String override"),
                ParameterFixture.OwnedGraphInstance
                    ->SetGraphParameter<FString>(
                        OwnedDesc->Name,
                        FScopedPCGComponentParameterFixture::
                            ComponentOverrideValue),
                EPropertyBagResult::Success);
            constexpr double SubDisplayPrecisionDelta = 0.0000001;
            TestEqual(
                TEXT("Floating cursor fixture changes a Double below six-decimal text precision"),
                ParameterFixture.Graph->SetGraphParameter<double>(
                    FScopedPCGComponentParameterFixture::GraphDefaultName,
                    FScopedPCGComponentParameterFixture::GraphDefaultValue
                        + SubDisplayPrecisionDelta),
                EPropertyBagResult::Success);
            ParameterFixture.ClearSetupDirtyFlags();
            FPCGComponentReadInvariant FloatingComponentInvariant({
                ParameterFixture.Component});
            FPCGParameterReadInvariant FloatingParameterInvariant(
                ParameterFixture.GetParameterInterfaces());
            const TSharedPtr<FJsonObject> FloatingStaleCursorResult =
                FSalModule::BuildQueryResult(
                    PCGComponentQueryArguments(
                        Target,
                        LevelOperation(TEXT("parameters")),
                        false,
                        2,
                        Cursor));
            TestTrue(
                TEXT("A Parameter cursor expires after a sub-six-decimal Double change"),
                LevelHasDiagnostic(
                    FloatingStaleCursorResult,
                    TEXT("validation.invalid_cursor")));
            TestTrue(
                TEXT("Floating cursor rejection preserves PCG Component runtime state"),
                FloatingComponentInvariant.Verify(*this));
            TestTrue(
                TEXT("Floating cursor rejection preserves current bags and override bits"),
                FloatingParameterInvariant.Verify(*this));
        }
    }

    Error.Reset();
    if (!LevelFixture.Cleanup(Error))
    {
        AddError(Error);
    }
    Error.Reset();
    if (!ComponentFixture.Cleanup(Error))
    {
        AddError(Error);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FSalPCGComponentParameterHostileBoundsTest,
    "Loomle.Sal.PCGComponent.Query.ParameterHostileSizeAndNativeShapeBoundaries",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSalPCGComponentParameterHostileBoundsTest::RunTest(
    const FString& Parameters)
{
    // Keep this declaration order: the Level releases Component and Graph
    // references before the authored fixture removes its asset records.
    FScopedLevelComponentQueryFixture ComponentFixture;
    FScopedLevelQueryFixture LevelFixture;
    FString Error;
    if (!TestTrue(
            TEXT("Hostile PCG Parameter Level fixture builds"),
            LevelFixture.Build(Error)))
    {
        AddError(Error);
        return false;
    }
    if (!TestTrue(
            TEXT("Hostile PCG Parameter source Level becomes active"),
            LevelFixture.Activate(LevelFixture.Loaded, Error)))
    {
        AddError(Error);
        return false;
    }
    if (!TestTrue(
            TEXT("Hostile PCG Parameter Component fixture builds"),
            ComponentFixture.Build(LevelFixture, Error)))
    {
        AddError(Error);
        return false;
    }

    FScopedPCGComponentParameterFixture ParameterFixture;
    if (!TestTrue(
            TEXT("Hostile PCG Parameter GraphInstance fixture builds through public UE APIs"),
            ParameterFixture.Build(
                LevelFixture,
                ComponentFixture,
                Error)))
    {
        AddError(Error);
        return false;
    }

    const TSharedRef<FJsonObject> Target = PCGComponentTarget(
        LevelFixture.Loaded.ObjectPath,
        ComponentFixture.ActorId,
        TEXT("instance"),
        ComponentFixture.PCGId);
    const FPropertyBagPropertyDesc* OwnedStringDesc =
        ParameterFixture.OwnedGraphInstance
            ->GetUserParametersStruct()
            ->FindPropertyDescByID(
                FScopedPCGComponentParameterFixture::ComponentOverrideId);
    if (!TestNotNull(
            TEXT("Hostile bounds fixture resolves the owned String descriptor"),
            OwnedStringDesc))
    {
        return false;
    }

    constexpr int32 PerValueLimit = 8 * 1024;
    const FString PerValueAtLimit =
        FString::ChrN(PerValueLimit, TEXT('v'));
    TestEqual(
        TEXT("UE accepts a String at the exact 8 Ki code-unit boundary"),
        ParameterFixture.OwnedGraphInstance->SetGraphParameter<FString>(
            OwnedStringDesc->Name,
            PerValueAtLimit),
        EPropertyBagResult::Success);
    ParameterFixture.ClearSetupDirtyFlags();
    const TSharedPtr<FJsonObject> PerValueAtLimitResult =
        FSalModule::BuildQueryResult(
            PCGComponentQueryArguments(
                Target,
                PCGComponentExactParameterOperation(
                    FScopedPCGComponentParameterFixture::ComponentOverrideId),
                true));
    const TArray<TSharedPtr<FJsonObject>> PerValueAtLimitFields =
        LevelTaggedObjectFields(
            PerValueAtLimitResult,
            TEXT("parameter"));
    FString PerValueLocal;
    FString PerValueEffective;
    TestTrue(
        TEXT("Exact Parameter with schema preserves the frozen read-only shape at 8 Ki"),
        !LevelHasError(PerValueAtLimitResult)
            && PerValueAtLimitFields.Num() == 1
            && PerValueAtLimitFields[0]->Values.Num() == 10
            && PerValueAtLimitFields[0]->TryGetStringField(
                TEXT("localValue"),
                PerValueLocal)
            && PerValueLocal == PerValueAtLimit
            && PerValueAtLimitFields[0]->TryGetStringField(
                TEXT("effectiveValue"),
                PerValueEffective)
            && PerValueEffective == PerValueAtLimit
            && LevelHasCommentContaining(
                PerValueAtLimitResult,
                TEXT("read-only"))
            && !PerValueAtLimitFields[0]->HasField(TEXT("writable"))
            && !PerValueAtLimitFields[0]->HasField(TEXT("operations"))
            && CountHandoffPurpose(
                PerValueAtLimitResult,
                TEXT("save")) == 0);

    TestEqual(
        TEXT("UE accepts a String one code unit beyond the SAL readback limit"),
        ParameterFixture.OwnedGraphInstance->SetGraphParameter<FString>(
            OwnedStringDesc->Name,
            FString::ChrN(PerValueLimit + 1, TEXT('v'))),
        EPropertyBagResult::Success);
    ParameterFixture.ClearSetupDirtyFlags();
    const TSharedPtr<FJsonObject> PerValueOverflowResult =
        FSalModule::BuildQueryResult(
            PCGComponentQueryArguments(
                Target,
                PCGComponentExactParameterOperation(
                    FScopedPCGComponentParameterFixture::ComponentOverrideId)));
    TestTrue(
        TEXT("A String one code unit over 8 Ki fails exact readback atomically"),
        LevelHasDiagnostic(
            PerValueOverflowResult,
            TEXT("validation.result_too_large"))
            && LevelTaggedObjectFields(
                PerValueOverflowResult,
                TEXT("parameter")).IsEmpty());
    TestEqual(
        TEXT("Hostile fixture restores the ordinary Component override"),
        ParameterFixture.OwnedGraphInstance->SetGraphParameter<FString>(
            OwnedStringDesc->Name,
            FScopedPCGComponentParameterFixture::ComponentOverrideValue),
        EPropertyBagResult::Success);

    constexpr int32 AggregateLimit = 64 * 1024;
    constexpr int32 AggregateParameterCount = 8;
    TArray<FName> AggregateNames;
    TArray<FPropertyBagPropertyDesc> AggregateDescriptors;
    AggregateNames.Reserve(AggregateParameterCount);
    AggregateDescriptors.Reserve(AggregateParameterCount);
    for (int32 Index = 0; Index < AggregateParameterCount; ++Index)
    {
        const FName Name(*FString::Printf(
            TEXT("AggregateString_%02d"),
            Index));
        const FGuid Id(
            0x71000000u,
            0x71000001u,
            0x71000002u,
            static_cast<uint32>(Index + 1));
        FPropertyBagPropertyDesc Desc(
            Name,
            EPropertyBagPropertyType::String);
        Desc.ID = Id;
        AggregateNames.Add(Name);
        AggregateDescriptors.Add(MoveTemp(Desc));
    }
    if (!TestEqual(
            TEXT("UE adds aggregate-boundary String descriptors through the Graph API"),
            ParameterFixture.Graph->AddUserParameters(
                AggregateDescriptors),
            EPropertyBagAlterationResult::Success))
    {
        return false;
    }
    const int32 ExistingStringEvidenceChars =
        FScopedPCGComponentParameterFixture::ComponentOverrideValue.Len()
        + FString(TEXT("Double")).Len();
    const int32 LastAggregateValueChars =
        AggregateLimit
        - ExistingStringEvidenceChars
        - (AggregateParameterCount - 1) * PerValueLimit;
    TestTrue(
        TEXT("Aggregate-boundary fixture keeps every individual value within 8 Ki"),
        LastAggregateValueChars > 0
            && LastAggregateValueChars <= PerValueLimit);
    bool bAggregateValuesSet = true;
    for (int32 Index = 0; Index < AggregateParameterCount; ++Index)
    {
        const int32 Length = Index + 1 < AggregateParameterCount
            ? PerValueLimit
            : LastAggregateValueChars;
        bAggregateValuesSet &=
            ParameterFixture.Graph->SetGraphParameter<FString>(
                AggregateNames[Index],
                FString::ChrN(
                    Length,
                    static_cast<TCHAR>(TEXT('a') + Index)))
            == EPropertyBagResult::Success;
    }
    if (!TestTrue(
            TEXT("UE authors every aggregate-boundary String default"),
            bAggregateValuesSet))
    {
        return false;
    }
    ParameterFixture.ClearSetupDirtyFlags();

    const TSharedPtr<FJsonObject> AggregateAtLimitResult =
        FSalModule::BuildQueryResult(
            PCGComponentQueryArguments(
                Target,
                PCGComponentExactParameterOperation(
                    FScopedPCGComponentParameterFixture::GraphDefaultId)));
    const int32 AuthoredAggregateChars = ExistingStringEvidenceChars
        + (AggregateParameterCount - 1) * PerValueLimit
        + LastAggregateValueChars;
    TestTrue(
        TEXT("A complete 64 Ki aggregate String-value snapshot remains readable"),
        AuthoredAggregateChars == AggregateLimit
            && !LevelHasError(AggregateAtLimitResult)
            && LevelTaggedObjectFields(
                AggregateAtLimitResult,
                TEXT("parameter")).Num() == 1);

    TestEqual(
        TEXT("UE accepts an aggregate String snapshot one code unit over the SAL limit"),
        ParameterFixture.Graph->SetGraphParameter<FString>(
            AggregateNames.Last(),
            FString::ChrN(
                LastAggregateValueChars + 1,
                TEXT('z'))),
        EPropertyBagResult::Success);
    ParameterFixture.ClearSetupDirtyFlags();
    const TSharedPtr<FJsonObject> AggregateOverflowResult =
        FSalModule::BuildQueryResult(
            PCGComponentQueryArguments(
                Target,
                PCGComponentExactParameterOperation(
                    FScopedPCGComponentParameterFixture::GraphDefaultId)));
    TestTrue(
        TEXT("A 64 Ki plus one aggregate String-value snapshot fails atomically"),
        LevelHasDiagnostic(
            AggregateOverflowResult,
            TEXT("validation.result_too_large"))
            && LevelTaggedObjectFields(
                AggregateOverflowResult,
                TEXT("parameter")).IsEmpty());
    TestEqual(
        TEXT("Hostile fixture restores the exact aggregate boundary"),
        ParameterFixture.Graph->SetGraphParameter<FString>(
            AggregateNames.Last(),
            FString::ChrN(
                LastAggregateValueChars,
                TEXT('z'))),
        EPropertyBagResult::Success);

#if UE_VERSION_NEWER_THAN_OR_EQUAL(5, 8, 0)
    const FName UnsupportedMapName(TEXT("UnsupportedNativeMap"));
    const FGuid UnsupportedMapId(
        0x78000001,
        0x78000002,
        0x78000003,
        0x78000004);
    FPropertyBagPropertyDesc UnsupportedMapDesc(
        UnsupportedMapName,
        EPropertyBagContainerType::Map,
        EPropertyBagPropertyType::Double,
        nullptr,
        CPF_Edit,
        EPropertyBagPropertyType::Name);
    UnsupportedMapDesc.ID = UnsupportedMapId;
    if (TestEqual(
            TEXT("UE 5.8 creates a native Name-to-Double Map descriptor"),
            ParameterFixture.Graph->AddUserParameters(
                {UnsupportedMapDesc}),
            EPropertyBagAlterationResult::Success))
    {
        ParameterFixture.ClearSetupDirtyFlags();
        const TSharedPtr<FJsonObject> MapCollectionResult =
            FSalModule::BuildQueryResult(
                PCGComponentQueryArguments(
                    Target,
                    LevelOperation(TEXT("parameters"))));
        TestTrue(
            TEXT("A native Map descriptor fails the closed public Parameter type environment"),
            LevelHasDiagnostic(
                MapCollectionResult,
                TEXT("validation.reference_scan_incomplete"))
                && LevelTaggedObjectFields(
                    MapCollectionResult,
                    TEXT("parameter")).IsEmpty());
        const TSharedPtr<FJsonObject> MapExactResult =
            FSalModule::BuildQueryResult(
                PCGComponentQueryArguments(
                    Target,
                    PCGComponentExactParameterOperation(
                        UnsupportedMapId),
                    true));
        TestTrue(
            TEXT("A native Map cannot leak a partial descriptor through exact schema Query"),
            LevelHasDiagnostic(
                MapExactResult,
                TEXT("validation.reference_scan_incomplete"))
                && LevelTaggedObjectFields(
                    MapExactResult,
                    TEXT("parameter")).IsEmpty());

        EPropertyBagAlterationResult RemoveMapResult =
            EPropertyBagAlterationResult::InternalError;
        ParameterFixture.Graph->UpdateUserParametersStruct(
            [&RemoveMapResult, UnsupportedMapName](
                FInstancedPropertyBag& Bag)
            {
                RemoveMapResult =
                    Bag.RemovePropertyByName(UnsupportedMapName);
            });
        TestEqual(
            TEXT("UE removes the Map through the public Graph Parameter update path"),
            RemoveMapResult,
            EPropertyBagAlterationResult::Success);
        ParameterFixture.ClearSetupDirtyFlags();
        const TSharedPtr<FJsonObject> AfterMapRemovalResult =
            FSalModule::BuildQueryResult(
                PCGComponentQueryArguments(
                    Target,
                    PCGComponentExactParameterOperation(
                        FScopedPCGComponentParameterFixture::GraphDefaultId)));
        TestFalse(
            TEXT("Removing the Map restores the complete Parameter environment"),
            LevelHasError(AfterMapRemovalResult));
    }
#endif

    // Descriptor evidence is counted once for the Graph and once for each of
    // the two aligned GraphInstances. Fifty-six 400-character names alone
    // therefore contribute 67,200 code units, independently proving that the
    // shared 64 Ki evidence budget has been crossed without mirroring the
    // implementation's full accounting in this fixture.
    constexpr int32 EvidenceOverflowDescriptorCount = 56;
    constexpr int32 EvidenceOverflowNameChars = 400;
    static_assert(
        EvidenceOverflowDescriptorCount
            * EvidenceOverflowNameChars
            * 3
            > 64 * 1024);
    TArray<FPropertyBagPropertyDesc> EvidenceOverflowDescriptors;
    EvidenceOverflowDescriptors.Reserve(
        EvidenceOverflowDescriptorCount);
    for (int32 Index = 0;
         Index < EvidenceOverflowDescriptorCount;
         ++Index)
    {
        const FString Prefix = FString::Printf(
            TEXT("LoomleEvidenceOverflow_%02d_"),
            Index);
        const FName Name(*(
            Prefix
            + FString::ChrN(
                EvidenceOverflowNameChars - Prefix.Len(),
                TEXT('e'))));
        FPropertyBagPropertyDesc Desc(
            Name,
            EPropertyBagPropertyType::String);
        Desc.ID = FGuid(
            0xb0000000u,
            0xb0000001u,
            0xb0000002u,
            static_cast<uint32>(Index + 1));
        EvidenceOverflowDescriptors.Add(MoveTemp(Desc));
    }
    if (TestEqual(
            TEXT("UE adds an independently over-budget descriptor environment"),
            ParameterFixture.Graph->AddUserParameters(
                EvidenceOverflowDescriptors),
            EPropertyBagAlterationResult::Success))
    {
        ParameterFixture.ClearSetupDirtyFlags();
        const TSharedPtr<FJsonObject> EvidenceOverflowResult =
            FSalModule::BuildQueryResult(
                PCGComponentQueryArguments(
                    Target,
                    LevelOperation(TEXT("parameters"))));
        TestTrue(
            TEXT("Descriptor evidence above 64 Ki fails the complete Parameter environment atomically"),
            LevelHasDiagnostic(
                EvidenceOverflowResult,
                TEXT("validation.reference_scan_incomplete"))
                && LevelTaggedObjectFields(
                    EvidenceOverflowResult,
                    TEXT("parameter")).IsEmpty());
    }

    Error.Reset();
    if (!LevelFixture.Cleanup(Error))
    {
        AddError(Error);
    }
    Error.Reset();
    if (!ComponentFixture.Cleanup(Error))
    {
        AddError(Error);
    }
    return true;
}

class FScopedLevelWorldPartitionQueryFixture
{
public:
    FScopedLevelWorldPartitionQueryFixture() = default;
    FScopedLevelWorldPartitionQueryFixture(
        const FScopedLevelWorldPartitionQueryFixture&) = delete;
    FScopedLevelWorldPartitionQueryFixture& operator=(
        const FScopedLevelWorldPartitionQueryFixture&) = delete;

    ~FScopedLevelWorldPartitionQueryFixture()
    {
        FString Ignored;
        Cleanup(Ignored);
    }

    bool Build(FString& OutError)
    {
        OutError.Reset();
        const FString Token =
            FGuid::NewGuid().ToString(EGuidFormats::Digits);
        PackageName = FString::Printf(
            TEXT("/Game/LoomleTests/LevelWorldPartition/%s/L_WP_UnloadedActor"),
            *Token);
        ObjectPath = PackageName + TEXT(".L_WP_UnloadedActor");
        SourcePackageName = FString::Printf(
            TEXT("/Game/LoomleTests/LevelWorldPartition/%s/L_WP_LevelInstanceSource"),
            *Token);
        SourceObjectPath = SourcePackageName
            + TEXT(".L_WP_LevelInstanceSource");
        MapFilename = FPackageName::LongPackageNameToFilename(
            PackageName,
            FPackageName::GetMapPackageExtension());
        SourceMapFilename = FPackageName::LongPackageNameToFilename(
            SourcePackageName,
            FPackageName::GetMapPackageExtension());
        IFileManager::Get().MakeDirectory(
            *FPaths::GetPath(MapFilename),
            true);
        IFileManager::Get().MakeDirectory(
            *FPaths::GetPath(SourceMapFilename),
            true);

        IWorldPartitionEditorModule& WorldPartitionEditor =
            IWorldPartitionEditorModule::Get();
        const bool bOriginalLoadingInEditor =
            WorldPartitionEditor.GetEnableLoadingInEditor();
        bExpectedLoadingInEditor = bOriginalLoadingInEditor;
        const auto SetLoadingInEditorWithoutRetrofittingActiveWorld =
            [&WorldPartitionEditor](const bool bEnabled)
            {
                // The setting callback otherwise rebuilds the loader adapter
                // on GWorld. This fixture only needs to control how its new
                // isolated partition is initialized.
                UWorld* GlobalWorldBefore = GWorld.GetReference();
                GWorld = nullptr;
                WorldPartitionEditor.SetEnableLoadingInEditor(bEnabled);
                GWorld = GlobalWorldBefore;
            };
        const UWorld::InitializationValues InitValues =
            UWorld::InitializationValues()
                .RequiresHitProxies(false)
                .ShouldSimulatePhysics(false)
                .EnableTraceCollision(false)
                .CreateNavigation(false)
                .CreateAISystem(false)
                .AllowAudioPlayback(false)
                .CreatePhysicsScene(false)
                .CreateWorldPartition(true)
                .EnableWorldPartitionStreaming(true);
        FSavePackageArgs SaveArgs;
        SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
        SaveArgs.Error = GLog;

        // Author and fully release a real World Partition source before the
        // containing placement exists. The later Level Instance descriptor
        // must recover this source from durable descriptor/Asset Registry
        // evidence rather than a loaded UWorld.
        if (!bOriginalLoadingInEditor)
        {
            SetLoadingInEditorWithoutRetrofittingActiveWorld(true);
        }
        UPackage* SourcePackage = CreatePackage(*SourcePackageName);
        SourceWorld = UWorld::CreateWorld(
            EWorldType::Editor,
            false,
            FName(TEXT("L_WP_LevelInstanceSource")),
            SourcePackage,
            false,
            ERHIFeatureLevel::Num,
            &InitValues);
        if (!bOriginalLoadingInEditor)
        {
            SetLoadingInEditorWithoutRetrofittingActiveWorld(false);
        }
        if (SourceWorld == nullptr
            || SourceWorld->GetPathName() != SourceObjectPath
            || SourceWorld->PersistentLevel == nullptr
            || !SourceWorld->PersistentLevel->IsUsingExternalActors())
        {
            OutError = TEXT(
                "UE failed to create the external-Actor World Partition Level Instance source map.");
            return false;
        }
        SourceWorldRoot.Reset(SourceWorld);
        SourceWorld->SetFlags(
            RF_Public | RF_Standalone | RF_Transactional);
        FAssetRegistryModule::AssetCreated(SourceWorld);

        SourceWorldPartition = SourceWorld->GetWorldPartition();
        SourceRootContainer = SourceWorldPartition != nullptr
            ? SourceWorldPartition->GetActorDescContainerInstance()
            : nullptr;
        if (!IsValid(SourceWorldPartition)
            || !IsValid(SourceRootContainer)
            || !SourceRootContainer->IsInitialized()
            || SourceRootContainer->GetContainerPackage()
                != SourceWorld->GetOutermost()->GetFName()
            || SourceRootContainer->GetParentContainerInstance() != nullptr)
        {
            OutError = TEXT(
                "UE did not initialize the Level Instance source descriptor index.");
            return false;
        }

        FActorSpawnParameters SourceSpawnParams;
        SourceSpawnParams.Name = FName(TEXT("Actor_ChildOnly"));
        SourceSpawnParams.OverrideLevel = SourceWorld->PersistentLevel;
        SourceSpawnParams.ObjectFlags = RF_Transactional;
        AActor* SourceActor = SourceWorld->SpawnActor<AActor>(
            AActor::StaticClass(),
            FTransform(FVector(128.0, 256.0, 64.0)),
            SourceSpawnParams);
        if (SourceActor == nullptr
            || !SourceActor->GetActorGuid().IsValid()
            || !SourceActor->IsPackageExternal()
            || !SourceActor->GetIsSpatiallyLoaded())
        {
            OutError = TEXT(
                "UE failed to create the source map's external child-only Actor.");
            return false;
        }
        SourceActor->SetActorLabel(
            TEXT("Loomle Child Container Only Actor"),
            false);
        SourceActorId = SourceActor->GetActorGuid();
        SourceActorObjectPath = SourceActor->GetPathName();
        UPackage* SourceExternalPackage =
            SourceActor->GetExternalPackage();
        if (SourceExternalPackage == nullptr
            || SourceExternalPackage == SourceWorld->GetOutermost())
        {
            OutError = TEXT(
                "The source map Actor was not assigned a genuine external package.");
            return false;
        }
        SourceActorPackageName = SourceExternalPackage->GetName();
        SourceActorFilename =
            FPackageName::LongPackageNameToFilename(
                SourceActorPackageName,
                FPackageName::GetAssetPackageExtension());
        IFileManager::Get().MakeDirectory(
            *FPaths::GetPath(SourceActorFilename),
            true);
        if (!SourceActorPackageName.StartsWith(
                ULevel::GetExternalActorsPath(SourcePackageName)
                    + TEXT("/")))
        {
            OutError = TEXT(
                "The source Actor package is not under its map's native ExternalActors path.");
            return false;
        }

        SourceWorld->GetOutermost()->SetDirtyFlag(true);
        SourceWorld->GetOutermost()->FullyLoad();
        if (!UPackage::SavePackage(
                SourceWorld->GetOutermost(),
                SourceWorld,
                *SourceMapFilename,
                SaveArgs))
        {
            OutError = TEXT(
                "UE failed to save the World Partition Level Instance source map.");
            return false;
        }
        SourceWorld->GetOutermost()->SetDirtyFlag(false);
        SourceExternalPackage->SetDirtyFlag(true);
        {
            UWorldPartition::FDisableNonDirtyActorTrackingScope NoAutoPin(
                SourceWorldPartition,
                true);
            if (!UPackage::SavePackage(
                    SourceExternalPackage,
                    SourceActor,
                    *SourceActorFilename,
                    SaveArgs))
            {
                OutError = TEXT(
                    "UE failed to save the source map's external Actor package.");
                return false;
            }
            SourceExternalPackage->SetDirtyFlag(false);
        }

        FWorldPartitionActorDescInstance* SourceDescriptor =
            SourceRootContainer->GetActorDescInstance(SourceActorId);
        if (SourceDescriptor == nullptr
            || SourceDescriptor->GetActorDesc() == nullptr
            || SourceDescriptor->GetContainerInstance()
                != SourceRootContainer
            || SourceDescriptor->IsChildContainerInstance()
            || SourceDescriptor->GetActorDesc()->GetActorPackage()
                != FName(*SourceActorPackageName))
        {
            OutError = TEXT(
                "Saving the source map did not publish its child-only root descriptor.");
            return false;
        }
        FModuleManager::LoadModuleChecked<FAssetRegistryModule>(
            TEXT("AssetRegistry"))
            .Get()
            .ScanModifiedAssetFiles(
                {SourceMapFilename, SourceActorFilename});

        SourceWorldPartition->UnpinActors({SourceActorId});
        if (SourceDescriptor->IsLoaded())
        {
            FWorldPartitionReference ReleaseSourceActor(
                SourceRootContainer,
                SourceActorId);
            if (!ReleaseSourceActor.IsValid())
            {
                OutError = TEXT(
                    "UE could not bind the saved source Actor through its public descriptor reference API.");
                return false;
            }
            ReleaseSourceActor.Reset();
        }
        // FWorldPartitionReference::Reset marks the Actor descriptor unloaded,
        // but UE 5.7 does not clear retention flags from every object in the
        // external package. Prepare that exact fixture package before GC so
        // descriptor-only state also means the package itself is absent.
        PrepareLevelPackageForCollection(SourceExternalPackage);
        SourceActor = nullptr;
        SourceExternalPackage = nullptr;
        SourcePackage = SourceWorld->GetOutermost();
        SourceWorld->DestroyWorld(false);
        PrepareLevelPackageForCollection(SourcePackage);
        SourceWorld = nullptr;
        SourceDescriptor = nullptr;
        SourceRootContainer = nullptr;
        SourceWorldPartition = nullptr;
        SourceWorldRoot.Reset();
        CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);
        FModuleManager::LoadModuleChecked<FAssetRegistryModule>(
            TEXT("AssetRegistry"))
            .Get()
            .ScanModifiedAssetFiles(
                {SourceMapFilename, SourceActorFilename});
        const FAssetData SourceWorldAsset =
            FModuleManager::LoadModuleChecked<FAssetRegistryModule>(
                TEXT("AssetRegistry"))
                .Get()
                .GetAssetByObjectPath(
                    FSoftObjectPath(SourceObjectPath),
                    true);
        const bool bSourceMapPackageLoaded =
            FindPackage(nullptr, *SourcePackageName) != nullptr;
        const bool bSourceWorldLoaded =
            FindObject<UWorld>(nullptr, *SourceObjectPath) != nullptr;
        const bool bSourceActorPackageLoaded =
            FindPackage(nullptr, *SourceActorPackageName) != nullptr;
        const bool bSourceUsesExternalActors =
            ULevel::GetIsLevelUsingExternalActorsFromPackage(
                FName(*SourcePackageName));
        if (bSourceMapPackageLoaded
            || bSourceWorldLoaded
            || bSourceActorPackageLoaded
            || !SourceWorldAsset.IsValid()
            || !SourceWorldAsset.IsTopLevelAsset()
            || SourceWorldAsset.PackageName
                != FName(*SourcePackageName)
            || SourceWorldAsset.AssetClassPath
                != UWorld::StaticClass()->GetClassPathName()
            || !bSourceUsesExternalActors)
        {
            OutError = FString::Printf(
                TEXT("The saved World Partition source did not reach a durable unloaded Asset Registry state (mapPackageLoaded=%s, worldLoaded=%s, actorPackageLoaded=%s, assetValid=%s, topLevel=%s, assetPackage='%s', assetClass='%s', externalActorsTag=%s)."),
                bSourceMapPackageLoaded ? TEXT("true") : TEXT("false"),
                bSourceWorldLoaded ? TEXT("true") : TEXT("false"),
                bSourceActorPackageLoaded ? TEXT("true") : TEXT("false"),
                SourceWorldAsset.IsValid() ? TEXT("true") : TEXT("false"),
                SourceWorldAsset.IsTopLevelAsset()
                    ? TEXT("true")
                    : TEXT("false"),
                *SourceWorldAsset.PackageName.ToString(),
                *SourceWorldAsset.AssetClassPath.ToString(),
                bSourceUsesExternalActors ? TEXT("true") : TEXT("false"));
            return false;
        }

        if (!bOriginalLoadingInEditor)
        {
            // UWorldPartition creates its force-load adapter only during
            // initialization. Restore the user's global setting immediately
            // after creating this isolated World.
            SetLoadingInEditorWithoutRetrofittingActiveWorld(true);
        }

        UPackage* Package = CreatePackage(*PackageName);
        World = UWorld::CreateWorld(
            EWorldType::Editor,
            false,
            FName(TEXT("L_WP_UnloadedActor")),
            Package,
            false,
            ERHIFeatureLevel::Num,
            &InitValues);

        if (!bOriginalLoadingInEditor)
        {
            SetLoadingInEditorWithoutRetrofittingActiveWorld(false);
        }

        if (World == nullptr
            || World->GetPathName() != ObjectPath
            || World->PersistentLevel == nullptr
            || !World->PersistentLevel->IsUsingExternalActors())
        {
            OutError = TEXT(
                "UE failed to create an exact external-Actor World Partition test map.");
            return false;
        }
        WorldRoot.Reset(World);
        World->SetFlags(RF_Public | RF_Standalone | RF_Transactional);
        FAssetRegistryModule::AssetCreated(World);
        bWorldRegistered = true;

        WorldPartition = World->GetWorldPartition();
        RootContainer = WorldPartition != nullptr
            ? WorldPartition->GetActorDescContainerInstance()
            : nullptr;
        if (!IsValid(WorldPartition)
            || !IsValid(RootContainer)
            || !RootContainer->IsInitialized()
            || RootContainer->GetContainerPackage()
                != World->GetOutermost()->GetFName()
            || RootContainer->GetParentContainerInstance() != nullptr)
        {
            OutError = TEXT(
                "UE did not initialize the saved map's root World Partition descriptor index.");
            return false;
        }

        FActorSpawnParameters SpawnParams;
        SpawnParams.Name = FName(TEXT("PCG_UnloadedRootOwner"));
        SpawnParams.OverrideLevel = World->PersistentLevel;
        SpawnParams.ObjectFlags = RF_Transactional;
        APCGVolume* Owner = World->SpawnActor<APCGVolume>(
            APCGVolume::StaticClass(),
            FTransform(FVector(3200.0, 1600.0, 100.0)),
            SpawnParams);
        UPCGComponent* Component = Owner != nullptr
            ? Owner->PCGComponent.Get()
            : nullptr;
        if (Owner == nullptr
            || Component == nullptr
            || !Owner->GetActorGuid().IsValid()
            || !Owner->IsPackageExternal()
            || !Owner->GetIsSpatiallyLoaded()
            || Component->CreationMethod
                != EComponentCreationMethod::Native
            || Component->GetConstOriginalComponent() != Component)
        {
            OutError = TEXT(
                "UE failed to create a spatial external root Actor with a durable native PCG Component.");
            return false;
        }
        Owner->SetActorLabel(
            TEXT("Loomle Unloaded World Partition Root"),
            false);
        ActorId = Owner->GetActorGuid();
        ActorObjectPath = Owner->GetPathName();
        PCGComponentId = Component->GetFName().ToString();

        UPackage* ExternalPackage = Owner->GetExternalPackage();
        if (ExternalPackage == nullptr
            || ExternalPackage == World->GetOutermost())
        {
            OutError = TEXT(
                "The World Partition Actor was not assigned a genuine external package.");
            return false;
        }
        ActorPackageName = ExternalPackage->GetName();
        ActorFilename = FPackageName::LongPackageNameToFilename(
            ActorPackageName,
            FPackageName::GetAssetPackageExtension());
        IFileManager::Get().MakeDirectory(
            *FPaths::GetPath(ActorFilename),
            true);
        if (!ActorPackageName.StartsWith(
                ULevel::GetExternalActorsPath(PackageName)
                    + TEXT("/")))
        {
            OutError = TEXT(
                "The Actor package is not under the map's native ExternalActors path.");
            return false;
        }

        FActorSpawnParameters LevelInstanceSpawnParams;
        LevelInstanceSpawnParams.Name =
            FName(TEXT("LI_UnloadedRootPlacement"));
        LevelInstanceSpawnParams.OverrideLevel = World->PersistentLevel;
        LevelInstanceSpawnParams.ObjectFlags = RF_Transactional;
        ALevelInstance* LevelInstance =
            World->SpawnActor<ALevelInstance>(
                ALevelInstance::StaticClass(),
                FTransform(FVector(-2400.0, 800.0, 0.0)),
                LevelInstanceSpawnParams);
        const TSoftObjectPtr<UWorld> SourceAsset{
            FSoftObjectPath(SourceObjectPath)};
        if (LevelInstance == nullptr
            || !LevelInstance->SetWorldAsset(SourceAsset)
            || LevelInstance->GetWorldAsset().ToSoftObjectPath().ToString()
                != SourceObjectPath
            || LevelInstance->GetWorldAsset().Get() != nullptr
            || !LevelInstance->GetActorGuid().IsValid()
            || !LevelInstance->IsPackageExternal()
            || !LevelInstance->GetIsSpatiallyLoaded())
        {
            OutError = TEXT(
                "UE failed to create an unloaded-source external Level Instance root placement.");
            return false;
        }
        LevelInstance->SetActorLabel(
            TEXT("Loomle Unloaded Root Level Instance"),
            false);
        LevelInstanceActorId = LevelInstance->GetActorGuid();
        LevelInstanceActorObjectPath = LevelInstance->GetPathName();
        UPackage* LevelInstanceExternalPackage =
            LevelInstance->GetExternalPackage();
        if (LevelInstanceExternalPackage == nullptr
            || LevelInstanceExternalPackage == World->GetOutermost())
        {
            OutError = TEXT(
                "The Level Instance placement was not assigned a genuine external package.");
            return false;
        }
        LevelInstanceActorPackageName =
            LevelInstanceExternalPackage->GetName();
        LevelInstanceActorFilename =
            FPackageName::LongPackageNameToFilename(
                LevelInstanceActorPackageName,
                FPackageName::GetAssetPackageExtension());
        IFileManager::Get().MakeDirectory(
            *FPaths::GetPath(LevelInstanceActorFilename),
            true);
        if (!LevelInstanceActorPackageName.StartsWith(
                ULevel::GetExternalActorsPath(PackageName)
                    + TEXT("/")))
        {
            OutError = TEXT(
                "The Level Instance placement package is not under the containing map's native ExternalActors path.");
            return false;
        }

        // Match UE's native map-save ordering: persist the map while its
        // external Actor is loaded, then persist and release external
        // packages. This lets UWorldPartition::PreSave gather the authored
        // external reference before the descriptor-only state is established.
        World->GetOutermost()->SetDirtyFlag(true);
        World->GetOutermost()->FullyLoad();
        if (!UPackage::SavePackage(
                World->GetOutermost(),
                World,
                *MapFilename,
                SaveArgs))
        {
            OutError = TEXT(
                "UE failed to save the World Partition map package.");
            return false;
        }
        World->GetOutermost()->SetDirtyFlag(false);

        ExternalPackage->SetDirtyFlag(true);
        LevelInstanceExternalPackage->SetDirtyFlag(true);
        {
            // This is UE's public save-time scope for ensuring a newly saved
            // spatial Actor (including a Level Instance placement) is
            // released instead of silently transferred to the editor pin
            // adapter.
            UWorldPartition::FDisableNonDirtyActorTrackingScope NoAutoPin(
                WorldPartition,
                true);
            if (!UPackage::SavePackage(
                    ExternalPackage,
                    Owner,
                    *ActorFilename,
                    SaveArgs))
            {
                OutError = TEXT(
                    "UE failed to save the genuine external Actor package.");
                return false;
            }
            ExternalPackage->SetDirtyFlag(false);
            if (!UPackage::SavePackage(
                    LevelInstanceExternalPackage,
                    LevelInstance,
                    *LevelInstanceActorFilename,
                    SaveArgs))
            {
                OutError = TEXT(
                    "UE failed to save the genuine external Level Instance placement package.");
                return false;
            }
            LevelInstanceExternalPackage->SetDirtyFlag(false);
        }

        Descriptor = RootContainer->GetActorDescInstance(ActorId);
        if (Descriptor == nullptr
            || Descriptor->GetActorDesc() == nullptr
            || Descriptor->GetContainerInstance() != RootContainer
            || Descriptor->IsChildContainerInstance()
            || Descriptor->GetActorDesc()->GetActorPackage()
                != FName(*ActorPackageName))
        {
            OutError = TEXT(
                "Saving the external Actor did not publish one genuine root Actor descriptor.");
            return false;
        }

        LevelInstanceDescriptor =
            RootContainer->GetActorDescInstance(LevelInstanceActorId);
        if (LevelInstanceDescriptor == nullptr
            || LevelInstanceDescriptor->GetActorDesc() == nullptr
            || LevelInstanceDescriptor->GetContainerInstance()
                != RootContainer
            || !LevelInstanceDescriptor->IsChildContainerInstance()
            || LevelInstanceDescriptor->GetActorDesc()
                ->GetActorPackage()
                != FName(*LevelInstanceActorPackageName)
            || LevelInstanceDescriptor->GetActorDesc()
                ->GetChildContainerPackage()
                != FName(*SourcePackageName)
            || LevelInstanceDescriptor->GetActorDesc()
                ->GetActorNativeClass() == nullptr
            || !LevelInstanceDescriptor->GetActorDesc()
                ->GetActorNativeClass()
                ->IsChildOf(ALevelInstance::StaticClass()))
        {
            OutError = TEXT(
                "Saving the Level Instance did not publish a genuine root descriptor with its source child-container package.");
            return false;
        }
        const TObjectPtr<UActorDescContainerInstance>* ChildContainerPtr =
            RootContainer->GetChildContainerInstances().Find(
                LevelInstanceActorId);
        ChildContainer = ChildContainerPtr != nullptr
            ? ChildContainerPtr->Get()
            : nullptr;
        SourceChildDescriptor = ChildContainer != nullptr
            ? ChildContainer->GetActorDescInstance(SourceActorId)
            : nullptr;
        if (RootContainer->GetChildContainerInstances().Num() != 1
            || !IsValid(ChildContainer)
            || !ChildContainer->IsInitialized()
            || ChildContainer->GetContainerPackage()
                != FName(*SourcePackageName)
            || ChildContainer->GetParentContainerInstance()
                != RootContainer
            || SourceChildDescriptor == nullptr
            || SourceChildDescriptor->GetActorDesc() == nullptr
            || SourceChildDescriptor->GetContainerInstance()
                != ChildContainer
            || SourceChildDescriptor->IsLoaded()
            || SourceChildDescriptor->GetActor() != nullptr
            || SourceChildDescriptor->GetActorDesc()->GetActorPackage()
                != FName(*SourceActorPackageName)
            || RootContainer->GetActorDescInstance(SourceActorId)
                != nullptr
            || !ChildContainer->GetChildContainerInstances().IsEmpty()
            || FindPackage(nullptr, *SourcePackageName) != nullptr
            || FindPackage(nullptr, *SourceActorPackageName) != nullptr
            || HasLoadedSourceInstancePackage())
        {
            OutError = TEXT(
                "UE failed to retain a descriptor-only child-container hierarchy for the unloaded Level Instance source.");
            return false;
        }

        FModuleManager::LoadModuleChecked<FAssetRegistryModule>(
            TEXT("AssetRegistry"))
            .Get()
            .ScanModifiedAssetFiles(
                {MapFilename,
                 ActorFilename,
                 LevelInstanceActorFilename});

        WorldPartition->UnpinActors(
            {ActorId, LevelInstanceActorId});
        if (Descriptor->IsLoaded())
        {
            FWorldPartitionReference ReleaseLoadedActor(
                RootContainer,
                ActorId);
            if (!ReleaseLoadedActor.IsValid())
            {
                OutError = TEXT(
                    "UE could not bind the saved Actor descriptor through its public reference API.");
                return false;
            }
            ReleaseLoadedActor.Reset();
        }
        if (LevelInstanceDescriptor->IsLoaded())
        {
            FWorldPartitionReference ReleaseLevelInstance(
                RootContainer,
                LevelInstanceActorId);
            if (!ReleaseLevelInstance.IsValid())
            {
                OutError = TEXT(
                    "UE could not bind the saved Level Instance descriptor through its public reference API.");
                return false;
            }
            ReleaseLevelInstance.Reset();
        }
        Owner = nullptr;
        Component = nullptr;
        ExternalPackage = nullptr;
        LevelInstance = nullptr;
        LevelInstanceExternalPackage = nullptr;
        CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);

        Descriptor = RootContainer->GetActorDescInstance(ActorId);
        LevelInstanceDescriptor =
            RootContainer->GetActorDescInstance(LevelInstanceActorId);
        ChildContainerPtr = RootContainer->GetChildContainerInstances()
            .Find(LevelInstanceActorId);
        ChildContainer = ChildContainerPtr != nullptr
            ? ChildContainerPtr->Get()
            : nullptr;
        SourceChildDescriptor = ChildContainer != nullptr
            ? ChildContainer->GetActorDescInstance(SourceActorId)
            : nullptr;
        if (Descriptor == nullptr
            || Descriptor->IsLoaded()
            || Descriptor->GetActor() != nullptr
            || LevelInstanceDescriptor == nullptr
            || LevelInstanceDescriptor->IsLoaded()
            || LevelInstanceDescriptor->GetActor() != nullptr
            || LevelInstanceDescriptor->GetActorDesc() == nullptr
            || LevelInstanceDescriptor->GetActorDesc()
                ->GetChildContainerPackage()
                != FName(*SourcePackageName)
            || SourceChildDescriptor == nullptr
            || SourceChildDescriptor->IsLoaded()
            || SourceChildDescriptor->GetActor() != nullptr
            || WorldPartition->IsActorPinned(ActorId)
            || WorldPartition->IsActorPinned(LevelInstanceActorId)
            || FindPackage(nullptr, *ActorPackageName) != nullptr
            || FindPackage(
                nullptr,
                *LevelInstanceActorPackageName) != nullptr
            || FindPackage(nullptr, *SourcePackageName) != nullptr
            || FindPackage(nullptr, *SourceActorPackageName) != nullptr
            || HasLoadedSourceInstancePackage()
            || !IFileManager::Get().FileExists(*MapFilename)
            || !IFileManager::Get().FileExists(*ActorFilename)
            || !IFileManager::Get().FileExists(
                *LevelInstanceActorFilename)
            || !IFileManager::Get().FileExists(*SourceMapFilename)
            || !IFileManager::Get().FileExists(*SourceActorFilename))
        {
            OutError = TEXT(
                "The saved external Actors did not reach a genuine root-and-child descriptor-only unloaded state.");
            return false;
        }
        RootDescriptorCount = RootContainer->GetActorDescInstanceCount();
        ChildDescriptorCount =
            ChildContainer->GetActorDescInstanceCount();
        PersistentActorSlotCount = World->PersistentLevel->Actors.Num();
        return true;
    }

    bool Activate(FString& OutError)
    {
        if (GEditor == nullptr || World == nullptr)
        {
            OutError = TEXT(
                "Cannot activate a null World Partition fixture in the Editor context.");
            return false;
        }
        FWorldContext& Context = GEditor->GetEditorWorldContext();
        if (!bEditorContextChanged)
        {
            OriginalEditorWorld = Context.World();
            bEditorContextChanged = true;
        }
        Context.SetCurrentWorld(World);
        if (Context.World() != World)
        {
            OutError = TEXT(
                "UE failed to bind the World Partition fixture to the Editor context.");
            return false;
        }
        return true;
    }

    TArray<FString> SavedFiles() const
    {
        return {
            SourceMapFilename,
            SourceActorFilename,
            MapFilename,
            ActorFilename,
            LevelInstanceActorFilename};
    }

    bool VerifyDescriptorOnlyState() const
    {
        if (World == nullptr
            || WorldPartition == nullptr
            || RootContainer == nullptr)
        {
            return false;
        }
        FWorldPartitionActorDescInstance* Current =
            RootContainer->GetActorDescInstance(ActorId);
        FWorldPartitionActorDescInstance* CurrentLevelInstance =
            RootContainer->GetActorDescInstance(LevelInstanceActorId);
        const TObjectPtr<UActorDescContainerInstance>* ChildContainerPtr =
            RootContainer->GetChildContainerInstances().Find(
                LevelInstanceActorId);
        UActorDescContainerInstance* CurrentChildContainer =
            ChildContainerPtr != nullptr
            ? ChildContainerPtr->Get()
            : nullptr;
        FWorldPartitionActorDescInstance* CurrentSourceChild =
            CurrentChildContainer != nullptr
            ? CurrentChildContainer->GetActorDescInstance(SourceActorId)
            : nullptr;
        const bool bLoadingSettingPreserved =
            IWorldPartitionEditorModule::Get().GetEnableLoadingInEditor()
                == bExpectedLoadingInEditor;
        return Current == Descriptor
            && Current != nullptr
            && Current->GetActorDesc() != nullptr
            && Current->GetContainerInstance() == RootContainer
            && !Current->IsChildContainerInstance()
            && !Current->IsLoaded()
            && Current->GetActor() == nullptr
            && CurrentLevelInstance == LevelInstanceDescriptor
            && CurrentLevelInstance != nullptr
            && CurrentLevelInstance->GetActorDesc() != nullptr
            && CurrentLevelInstance->GetContainerInstance()
                == RootContainer
            && CurrentLevelInstance->IsChildContainerInstance()
            && !CurrentLevelInstance->IsLoaded()
            && CurrentLevelInstance->GetActor() == nullptr
            && CurrentLevelInstance->GetActorDesc()
                ->GetChildContainerPackage()
                == FName(*SourcePackageName)
            && RootContainer->GetActorDescInstanceCount()
                == RootDescriptorCount
            && RootContainer->GetChildContainerInstances().Num() == 1
            && CurrentChildContainer == ChildContainer
            && IsValid(CurrentChildContainer)
            && CurrentChildContainer->IsInitialized()
            && CurrentChildContainer->GetContainerPackage()
                == FName(*SourcePackageName)
            && CurrentChildContainer->GetParentContainerInstance()
                == RootContainer
            && CurrentChildContainer->GetActorDescInstanceCount()
                == ChildDescriptorCount
            && CurrentChildContainer->GetChildContainerInstances()
                .IsEmpty()
            && CurrentSourceChild == SourceChildDescriptor
            && CurrentSourceChild != nullptr
            && CurrentSourceChild->GetActorDesc() != nullptr
            && CurrentSourceChild->GetContainerInstance()
                == CurrentChildContainer
            && !CurrentSourceChild->IsLoaded()
            && CurrentSourceChild->GetActor() == nullptr
            && CurrentSourceChild->GetActorDesc()->GetActorPackage()
                == FName(*SourceActorPackageName)
            && RootContainer->GetActorDescInstance(SourceActorId)
                == nullptr
            && !WorldPartition->IsActorPinned(ActorId)
            && !WorldPartition->IsActorPinned(LevelInstanceActorId)
            && bLoadingSettingPreserved
            && !World->GetOutermost()->IsDirty()
            && FindPackage(nullptr, *ActorPackageName) == nullptr
            && FindPackage(
                nullptr,
                *LevelInstanceActorPackageName) == nullptr
            && FindPackage(nullptr, *SourcePackageName) == nullptr
            && FindPackage(nullptr, *SourceActorPackageName) == nullptr
            && !HasLoadedSourceInstancePackage()
            && World->PersistentLevel != nullptr
            && World->PersistentLevel->Actors.Num()
                == PersistentActorSlotCount;
    }

    bool Cleanup(FString& OutError)
    {
        OutError.Reset();
        if (bCleaned)
        {
            return true;
        }
        bCleaned = true;

        if (bEditorContextChanged && GEditor != nullptr)
        {
            GEditor->GetEditorWorldContext().SetCurrentWorld(
                OriginalEditorWorld);
            bEditorContextChanged = false;
        }

        if (World != nullptr)
        {
            if (bWorldRegistered)
            {
                FAssetRegistryModule::AssetDeleted(World);
                bWorldRegistered = false;
            }
            UPackage* Package = World->GetOutermost();
            World->DestroyWorld(false);
            PrepareLevelPackageForCollection(Package);
            World = nullptr;
        }
        if (SourceWorld != nullptr)
        {
            UPackage* Package = SourceWorld->GetOutermost();
            SourceWorld->DestroyWorld(false);
            PrepareLevelPackageForCollection(Package);
            SourceWorld = nullptr;
        }
        for (const FString& FixturePackageName : {
                 PackageName,
                 ActorPackageName,
                 LevelInstanceActorPackageName,
                 SourcePackageName,
                 SourceActorPackageName})
        {
            if (!FixturePackageName.IsEmpty())
            {
                PrepareLevelPackageForCollection(
                    FindPackage(nullptr, *FixturePackageName));
            }
        }
        Descriptor = nullptr;
        LevelInstanceDescriptor = nullptr;
        SourceChildDescriptor = nullptr;
        ChildContainer = nullptr;
        RootContainer = nullptr;
        WorldPartition = nullptr;
        SourceRootContainer = nullptr;
        SourceWorldPartition = nullptr;
        WorldRoot.Reset();
        SourceWorldRoot.Reset();
        CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);

        TArray<FString> DeletedFiles;
        for (const FString& Filename : {
                 ActorFilename,
                 LevelInstanceActorFilename,
                 MapFilename,
                 SourceActorFilename,
                 SourceMapFilename})
        {
            if (!Filename.IsEmpty())
            {
                IFileManager::Get().Delete(
                    *Filename,
                    false,
                    true,
                    true);
                DeletedFiles.Add(Filename);
            }
        }
        if (!DeletedFiles.IsEmpty())
        {
            FModuleManager::LoadModuleChecked<FAssetRegistryModule>(
                TEXT("AssetRegistry"))
                .Get()
                .ScanModifiedAssetFiles(DeletedFiles);
        }
        for (const FString& LoadedPackageName : {
                 PackageName,
                 ActorPackageName,
                 LevelInstanceActorPackageName,
                 SourcePackageName,
                 SourceActorPackageName})
        {
            if (!LoadedPackageName.IsEmpty()
                && FindPackage(
                    nullptr,
                    *LoadedPackageName) != nullptr
                && OutError.IsEmpty())
            {
                OutError = TEXT(
                    "World Partition fixture package remained loaded during cleanup: ")
                    + LoadedPackageName;
            }
        }
        if (HasLoadedSourceInstancePackage()
            && OutError.IsEmpty())
        {
            OutError = TEXT(
                "World Partition fixture retained a temporary Level Instance source package during cleanup: ")
                + SourcePackageName;
        }
        return OutError.IsEmpty();
    }

    FString PackageName;
    FString ObjectPath;
    FString MapFilename;
    FString SourcePackageName;
    FString SourceObjectPath;
    FString SourceMapFilename;
    FString SourceActorPackageName;
    FString SourceActorFilename;
    FString SourceActorObjectPath;
    FString ActorPackageName;
    FString ActorFilename;
    FString ActorObjectPath;
    FString LevelInstanceActorPackageName;
    FString LevelInstanceActorFilename;
    FString LevelInstanceActorObjectPath;
    FString PCGComponentId;
    FGuid ActorId;
    FGuid LevelInstanceActorId;
    FGuid SourceActorId;
    UWorld* World = nullptr;
    UWorldPartition* WorldPartition = nullptr;
    UActorDescContainerInstance* RootContainer = nullptr;
    FWorldPartitionActorDescInstance* Descriptor = nullptr;

private:
    bool HasLoadedSourceInstancePackage() const
    {
        if (SourcePackageName.IsEmpty())
        {
            return false;
        }
        const FString Prefix = TEXT("/Temp")
            + FPackageName::GetLongPackagePath(SourcePackageName)
            + TEXT("/")
            + FPackageName::GetShortName(SourcePackageName)
            + TEXT("_LevelInstance_");
        for (TObjectIterator<UPackage> It; It; ++It)
        {
            if (It->GetName().StartsWith(Prefix))
            {
                return true;
            }
        }
        return false;
    }

    TStrongObjectPtr<UWorld> WorldRoot;
    TStrongObjectPtr<UWorld> SourceWorldRoot;
    UWorld* SourceWorld = nullptr;
    UWorldPartition* SourceWorldPartition = nullptr;
    UActorDescContainerInstance* SourceRootContainer = nullptr;
    UActorDescContainerInstance* ChildContainer = nullptr;
    FWorldPartitionActorDescInstance* LevelInstanceDescriptor = nullptr;
    FWorldPartitionActorDescInstance* SourceChildDescriptor = nullptr;
    UWorld* OriginalEditorWorld = nullptr;
    uint32 RootDescriptorCount = 0;
    uint32 ChildDescriptorCount = 0;
    int32 PersistentActorSlotCount = 0;
    bool bWorldRegistered = false;
    bool bEditorContextChanged = false;
    bool bExpectedLoadingInEditor = true;
    bool bCleaned = false;
};

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FSalLevelWorldPartitionUnloadedRootActorTest,
    "Loomle.Sal.Level.Query.WorldPartitionUnloadedRootActor",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSalLevelWorldPartitionUnloadedRootActorTest::RunTest(
    const FString& Parameters)
{
    FScopedLevelWorldPartitionQueryFixture Fixture;
    FString Error;
    if (!TestTrue(
            TEXT("A genuine saved World Partition fixture builds"),
            Fixture.Build(Error)))
    {
        AddError(Error);
        return false;
    }
    if (!TestTrue(
            TEXT("The World Partition source becomes the active Editor World"),
            Fixture.Activate(Error)))
    {
        AddError(Error);
        return false;
    }
    if (!TestTrue(
            TEXT("The fixture begins with descriptor-only unloaded root and child-container Actors"),
            Fixture.VerifyDescriptorOnlyState()))
    {
        return false;
    }

    const TSharedRef<FJsonObject> Target = LevelTarget(
        Fixture.ObjectPath,
        UWorld::StaticClass()->GetPathName());
    const FString ActorId = LevelGuidText(Fixture.ActorId);
    const FString LevelInstanceActorId =
        LevelGuidText(Fixture.LevelInstanceActorId);
    const FString SourceActorId =
        LevelGuidText(Fixture.SourceActorId);
    const TArray<FString> SavedFiles = Fixture.SavedFiles();
    TArray<FDateTime> SavedFileTimestampsBefore;
    TArray<int64> SavedFileSizesBefore;
    for (const FString& Filename : SavedFiles)
    {
        SavedFileTimestampsBefore.Add(
            IFileManager::Get().GetTimeStamp(*Filename));
        SavedFileSizesBefore.Add(
            IFileManager::Get().FileSize(*Filename));
    }
    const int32 UndoBefore =
        GEditor != nullptr && GEditor->Trans != nullptr
        ? GEditor->Trans->GetUndoCount()
        : -1;
    const int32 QueueBefore =
        GEditor != nullptr && GEditor->Trans != nullptr
        ? GEditor->Trans->GetQueueLength()
        : -1;
    TArray<AActor*> SelectedActorsBefore;
    TArray<UActorComponent*> SelectedComponentsBefore;
    if (GEditor != nullptr)
    {
        GEditor->GetSelectedActors()->GetSelectedObjects<AActor>(
            SelectedActorsBefore);
        GEditor->GetSelectedComponents()->GetSelectedObjects<UActorComponent>(
            SelectedComponentsBefore);
    }
    FLevelReadInvariant Invariant(Fixture.World);
    int32 TransactionEvents = 0;
    const FDelegateHandle TransactionHandle =
        FCoreUObjectDelegates::OnObjectTransacted.AddLambda(
            [&TransactionEvents](UObject*, const FTransactionObjectEvent&)
            {
                ++TransactionEvents;
            });

    const TSharedPtr<FJsonObject> SubobjectTargetResult =
        FSalModule::BuildQueryResult(
            LevelQueryArguments(
                LevelTarget(
                    Fixture.ObjectPath + TEXT(":PersistentLevel"),
                    UWorld::StaticClass()->GetPathName()),
                LevelOperation(TEXT("target"))));
    TestTrue(
        TEXT("A Level Target cannot use a saved map subobject path"),
        LevelHasDiagnostic(
            SubobjectTargetResult,
            TEXT("validation.invalid_target"))
            && LevelHasTargetContext(
                SubobjectTargetResult,
                TEXT("unresolved_target"))
            && !Fixture.World->GetOutermost()->IsDirty()
            && Fixture.VerifyDescriptorOnlyState());

    const TSharedPtr<FJsonObject> SummaryResult =
        FSalModule::BuildQueryResult(
            LevelQueryArguments(
                Target,
                LevelOperation(TEXT("summary"))));
    const TSharedPtr<FJsonObject> SummaryFields =
        FirstLevelAssetFields(SummaryResult);
    double UnloadedDescriptorCount = 0.0;
    bool bIdentityComplete = false;
    TestTrue(
        TEXT("Level summary counts both unloaded root descriptors but not the child container in a complete identity audit"),
        !LevelHasError(SummaryResult)
            && SummaryFields.IsValid()
            && SummaryFields->TryGetNumberField(
                TEXT("unloadedDescriptorCount"),
                UnloadedDescriptorCount)
            && UnloadedDescriptorCount >= 2.0
            && SummaryFields->TryGetBoolField(
                TEXT("identityComplete"),
                bIdentityComplete)
            && bIdentityComplete);

    const TSharedPtr<FJsonObject> ActorsResult =
        FSalModule::BuildQueryResult(
            LevelQueryArguments(
                Target,
                LevelOperation(TEXT("actors"))));
    const TArray<TSharedPtr<FJsonObject>> ActorFields =
        LevelTaggedObjectFields(ActorsResult, TEXT("actor"));
    const TSharedPtr<FJsonObject> CollectionMatch =
        FindLevelFieldsById(ActorFields, ActorId);
    FString CollectionPath;
    FString CollectionPackage;
    bool bCollectionLoaded = true;
    bool bCollectionExternal = false;
    bool bCollectionDescriptor = false;
    bool bCollectionStable = false;
    TestTrue(
        TEXT("Level actors projects the genuine unloaded root descriptor exactly once"),
        !LevelHasError(ActorsResult)
            && CountLevelFieldsById(ActorFields, ActorId) == 1
            && CollectionMatch.IsValid()
            && CollectionMatch->TryGetStringField(
                TEXT("path"),
                CollectionPath)
            && CollectionPath == Fixture.ActorObjectPath
            && CollectionMatch->TryGetStringField(
                TEXT("package"),
                CollectionPackage)
            && CollectionPackage == Fixture.ActorPackageName
            && CollectionMatch->TryGetBoolField(
                TEXT("loaded"),
                bCollectionLoaded)
            && !bCollectionLoaded
            && CollectionMatch->TryGetBoolField(
                TEXT("external"),
                bCollectionExternal)
            && bCollectionExternal
            && CollectionMatch->TryGetBoolField(
                TEXT("descriptor"),
                bCollectionDescriptor)
            && bCollectionDescriptor
            && CollectionMatch->TryGetBoolField(
                TEXT("stableRefAvailable"),
                bCollectionStable)
            && bCollectionStable);

    const TSharedPtr<FJsonObject> LevelInstanceCollectionMatch =
        FindLevelFieldsById(ActorFields, LevelInstanceActorId);
    FString LevelInstanceCollectionPath;
    FString LevelInstanceCollectionPackage;
    FString CollectionRelatedAlias;
    FString CollectionRelatedAsset;
    FString CollectionSourceAlias;
    bool bLevelInstanceCollectionLoaded = true;
    bool bLevelInstanceCollectionExternal = false;
    bool bLevelInstanceCollectionDescriptor = false;
    bool bLevelInstanceCollectionStable = false;
    bool bLevelInstanceCollectionType = false;
    TestTrue(
        TEXT("Level actors resolves sourceLevel and its handoff from the unloaded root Level Instance descriptor without leaking child Actors"),
        !LevelHasError(ActorsResult)
            && CountLevelFieldsById(
                ActorFields,
                LevelInstanceActorId) == 1
            && CountLevelFieldsById(ActorFields, SourceActorId) == 0
            && LevelInstanceCollectionMatch.IsValid()
            && LevelInstanceCollectionMatch->TryGetStringField(
                TEXT("path"),
                LevelInstanceCollectionPath)
            && LevelInstanceCollectionPath
                == Fixture.LevelInstanceActorObjectPath
            && LevelInstanceCollectionMatch->TryGetStringField(
                TEXT("package"),
                LevelInstanceCollectionPackage)
            && LevelInstanceCollectionPackage
                == Fixture.LevelInstanceActorPackageName
            && LevelInstanceCollectionMatch->TryGetBoolField(
                TEXT("levelInstance"),
                bLevelInstanceCollectionType)
            && bLevelInstanceCollectionType
            && LevelInstanceCollectionMatch->TryGetBoolField(
                TEXT("loaded"),
                bLevelInstanceCollectionLoaded)
            && !bLevelInstanceCollectionLoaded
            && LevelInstanceCollectionMatch->TryGetBoolField(
                TEXT("external"),
                bLevelInstanceCollectionExternal)
            && bLevelInstanceCollectionExternal
            && LevelInstanceCollectionMatch->TryGetBoolField(
                TEXT("descriptor"),
                bLevelInstanceCollectionDescriptor)
            && bLevelInstanceCollectionDescriptor
            && LevelInstanceCollectionMatch->TryGetBoolField(
                TEXT("stableRefAvailable"),
                bLevelInstanceCollectionStable)
            && bLevelInstanceCollectionStable
            && ReadSingleRelatedLevelTarget(
                ActorsResult,
                CollectionRelatedAlias,
                CollectionRelatedAsset)
            && CollectionRelatedAsset == Fixture.SourceObjectPath
            && HasSingleLevelHandoff(
                ActorsResult,
                TEXT("inspect_source_level"),
                CollectionRelatedAlias)
            && ReadLevelLocalField(
                LevelInstanceCollectionMatch,
                TEXT("sourceLevel"),
                CollectionSourceAlias)
            && CollectionSourceAlias == CollectionRelatedAlias);

    const TSharedPtr<FJsonObject> ChildContainerSearchResult =
        FSalModule::BuildQueryResult(
            LevelQueryArguments(
                Target,
                LevelOperation(
                    TEXT("actors"),
                    TEXT("Child Container Only"))));
    TestTrue(
        TEXT("Root Level Query does not traverse or project the child container"),
        !LevelHasError(ChildContainerSearchResult)
            && LevelTaggedObjectFields(
                ChildContainerSearchResult,
                TEXT("actor")).IsEmpty()
            && HasNoLevelRelatedContext(
                ChildContainerSearchResult));

    const TSharedPtr<FJsonObject> ExactActorResult =
        FSalModule::BuildQueryResult(
            LevelQueryArguments(
                Target,
                LevelExactOperation(ActorId)));
    const TSharedPtr<FJsonObject> ExactActorFields =
        FindLevelFieldsById(
            LevelTaggedObjectFields(
                ExactActorResult,
                TEXT("actor")),
            ActorId);
    bool bExactLoaded = true;
    bool bExactDescriptor = false;
    TestTrue(
        TEXT("Exact Actor StableRef resolves the same ActorGuid from descriptor evidence only"),
        !LevelHasError(ExactActorResult)
            && ExactActorFields.IsValid()
            && ExactActorFields->TryGetBoolField(
                TEXT("loaded"),
                bExactLoaded)
            && !bExactLoaded
            && ExactActorFields->TryGetBoolField(
                TEXT("descriptor"),
                bExactDescriptor)
            && bExactDescriptor);

    const TSharedPtr<FJsonObject> ExactLevelInstanceResult =
        FSalModule::BuildQueryResult(
            LevelQueryArguments(
                Target,
                LevelExactOperation(LevelInstanceActorId)));
    const TSharedPtr<FJsonObject> ExactLevelInstanceFields =
        FindLevelFieldsById(
            LevelTaggedObjectFields(
                ExactLevelInstanceResult,
                TEXT("actor")),
            LevelInstanceActorId);
    FString ExactLevelInstanceRelatedAlias;
    FString ExactLevelInstanceRelatedAsset;
    FString ExactLevelInstanceSourceAlias;
    bool bExactLevelInstanceLoaded = true;
    bool bExactLevelInstanceDescriptor = false;
    bool bExactLevelInstanceType = false;
    TestTrue(
        TEXT("Exact Level Instance StableRef resolves sourceLevel and its handoff from root descriptor evidence only"),
        !LevelHasError(ExactLevelInstanceResult)
            && ExactLevelInstanceFields.IsValid()
            && ExactLevelInstanceFields->TryGetBoolField(
                TEXT("levelInstance"),
                bExactLevelInstanceType)
            && bExactLevelInstanceType
            && ExactLevelInstanceFields->TryGetBoolField(
                TEXT("loaded"),
                bExactLevelInstanceLoaded)
            && !bExactLevelInstanceLoaded
            && ExactLevelInstanceFields->TryGetBoolField(
                TEXT("descriptor"),
                bExactLevelInstanceDescriptor)
            && bExactLevelInstanceDescriptor
            && ReadSingleRelatedLevelTarget(
                ExactLevelInstanceResult,
                ExactLevelInstanceRelatedAlias,
                ExactLevelInstanceRelatedAsset)
            && ExactLevelInstanceRelatedAsset
                == Fixture.SourceObjectPath
            && HasSingleLevelHandoff(
                ExactLevelInstanceResult,
                TEXT("inspect_source_level"),
                ExactLevelInstanceRelatedAlias)
            && ReadLevelLocalField(
                ExactLevelInstanceFields,
                TEXT("sourceLevel"),
                ExactLevelInstanceSourceAlias)
            && ExactLevelInstanceSourceAlias
                == ExactLevelInstanceRelatedAlias);

    const TSharedPtr<FJsonObject> ExactChildActorResult =
        FSalModule::BuildQueryResult(
            LevelQueryArguments(
                Target,
                LevelExactOperation(SourceActorId)));
    TestTrue(
        TEXT("A child-container ActorGuid is not an exact Actor of the containing Level Target"),
        LevelHasDiagnostic(
            ExactChildActorResult,
            TEXT("resolution.object_not_found"))
            && HasNoLevelRelatedContext(ExactChildActorResult));

    const TSharedPtr<FJsonObject> ExactComponentResult =
        FSalModule::BuildQueryResult(
            LevelQueryArguments(
                Target,
                LevelExactComponentOperation(
                    ActorId,
                    TEXT("native"),
                    Fixture.PCGComponentId)));
    TestTrue(
        TEXT("Exact Level Component read refuses to load its descriptor-only owner"),
        LevelHasDiagnostic(
            ExactComponentResult,
            TEXT("capability.component_owner_not_loaded")));

    const TSharedPtr<FJsonObject> ExactPCGComponentResult =
        FSalModule::BuildQueryResult(
            PCGComponentQueryArguments(
                PCGComponentTarget(
                    Fixture.ObjectPath,
                    Fixture.ActorId,
                    TEXT("native"),
                    Fixture.PCGComponentId),
                LevelOperation(TEXT("target"))));
    TestTrue(
        TEXT("Exact PCG Component read refuses to load its descriptor-only owner"),
        LevelHasDiagnostic(
            ExactPCGComponentResult,
            TEXT("capability.component_owner_not_loaded")));

    FCoreUObjectDelegates::OnObjectTransacted.Remove(TransactionHandle);
    TArray<AActor*> SelectedActorsAfter;
    TArray<UActorComponent*> SelectedComponentsAfter;
    if (GEditor != nullptr)
    {
        GEditor->GetSelectedActors()->GetSelectedObjects<AActor>(
            SelectedActorsAfter);
        GEditor->GetSelectedComponents()->GetSelectedObjects<UActorComponent>(
            SelectedComponentsAfter);
    }
    TestTrue(
        TEXT("Every World Partition Query preserves the descriptor-only unloaded and unpinned state"),
        Fixture.VerifyDescriptorOnlyState());
    bool bSavedFilesPreserved =
        SavedFiles.Num() == SavedFileTimestampsBefore.Num()
        && SavedFiles.Num() == SavedFileSizesBefore.Num();
    for (int32 Index = 0;
         bSavedFilesPreserved && Index < SavedFiles.Num();
         ++Index)
    {
        bSavedFilesPreserved =
            IFileManager::Get().GetTimeStamp(*SavedFiles[Index])
                == SavedFileTimestampsBefore[Index]
            && IFileManager::Get().FileSize(*SavedFiles[Index])
                == SavedFileSizesBefore[Index];
    }
    TestTrue(
        TEXT("World Partition Query does not rewrite any containing, placement, source, or child Actor package"),
        bSavedFilesPreserved);
    TestTrue(
        TEXT("World Partition Query preserves the exact Actor selection"),
        SelectedActorsAfter == SelectedActorsBefore);
    TestTrue(
        TEXT("World Partition Query preserves the exact Component selection"),
        SelectedComponentsAfter == SelectedComponentsBefore);
    TestEqual(
        TEXT("World Partition Query emits no UObject transaction"),
        TransactionEvents,
        0);
    if (GEditor != nullptr && GEditor->Trans != nullptr)
    {
        TestEqual(
            TEXT("World Partition Query creates no Undo entry"),
            GEditor->Trans->GetUndoCount(),
            UndoBefore);
        TestEqual(
            TEXT("World Partition Query preserves the transaction queue"),
            GEditor->Trans->GetQueueLength(),
            QueueBefore);
    }
    TestTrue(
        TEXT("All World Partition reads preserve the active World and authored state"),
        Invariant.Verify(*this));

    if (!Fixture.Cleanup(Error))
    {
        AddError(Error);
    }
    return true;
}

// ============================================================================
// Level Palette discovery (Slice 2) tests
// ============================================================================

TSharedRef<FJsonObject> LevelPaletteLocalRef(const FString& Alias)
{
    TSharedRef<FJsonObject> Ref = MakeShared<FJsonObject>();
    Ref->SetStringField(TEXT("kind"), TEXT("local"));
    Ref->SetStringField(TEXT("name"), Alias);
    return Ref;
}

TSharedRef<FJsonObject> LevelPaletteMemberRef(
    const TSharedRef<FJsonObject>& ObjectRef,
    const FString& Member)
{
    TSharedRef<FJsonObject> Ref = MakeShared<FJsonObject>();
    Ref->SetStringField(TEXT("kind"), TEXT("member"));
    Ref->SetObjectField(TEXT("object"), ObjectRef);
    Ref->SetArrayField(TEXT("path"), LevelStringValues({Member}));
    return Ref;
}

TSharedRef<FJsonObject> LevelPaletteEntriesOperation(
    const TSharedRef<FJsonObject>& Destination,
    const FString& SearchText = FString())
{
    TSharedRef<FJsonObject> Operation =
        LevelOperation(TEXT("palette_entries"), SearchText);
    Operation->SetObjectField(TEXT("to"), Destination);
    return Operation;
}

TSharedRef<FJsonObject> LevelPaletteOperation(
    const FString& Id,
    const TSharedRef<FJsonObject>& Destination)
{
    TSharedRef<FJsonObject> Operation = LevelOperation(TEXT("palette"));
    Operation->SetStringField(TEXT("id"), Id);
    Operation->SetObjectField(TEXT("to"), Destination);
    return Operation;
}

struct FLevelPaletteEntryView
{
    FString Alias;
    FString PaletteId;
    FString Name;
    FString Category;
    FString Type;
    FString Creation;
    FString Reason;
};

TArray<FLevelPaletteEntryView> CollectLevelPaletteEntries(
    const TSharedPtr<FJsonObject>& Result)
{
    TArray<FLevelPaletteEntryView> Out;
    const TSharedPtr<FJsonObject>* Object = nullptr;
    const TArray<TSharedPtr<FJsonValue>>* Statements = nullptr;
    if (!Result.IsValid()
        || !Result->TryGetObjectField(TEXT("object"), Object)
        || Object == nullptr
        || !(*Object).IsValid()
        || !(*Object)->TryGetArrayField(TEXT("statements"), Statements)
        || Statements == nullptr)
    {
        return Out;
    }
    for (const TSharedPtr<FJsonValue>& StatementValue : *Statements)
    {
        const TSharedPtr<FJsonObject>* Statement = nullptr;
        const TSharedPtr<FJsonObject>* Target = nullptr;
        const TSharedPtr<FJsonObject>* Value = nullptr;
        FString TargetKind;
        FString Alias;
        if (StatementValue.IsValid()
            && StatementValue->TryGetObject(Statement)
            && Statement != nullptr
            && (*Statement).IsValid()
            && (*Statement)->TryGetObjectField(TEXT("target"), Target)
            && Target != nullptr
            && (*Target).IsValid()
            && (*Target)->TryGetStringField(TEXT("kind"), TargetKind)
            && TargetKind == TEXT("local")
            && (*Target)->TryGetStringField(TEXT("name"), Alias)
            && (*Statement)->TryGetObjectField(TEXT("value"), Value)
            && Value != nullptr
            && (*Value).IsValid())
        {
            FString ValueKind;
            const TSharedPtr<FJsonObject>* Args = nullptr;
            if ((*Value)->TryGetStringField(TEXT("kind"), ValueKind)
                && ValueKind == TEXT("call")
                && (*Value)->TryGetObjectField(TEXT("args"), Args)
                && Args != nullptr)
            {
                FLevelPaletteEntryView Entry;
                Entry.Alias = Alias;
                (*Args)->TryGetStringField(TEXT("palette"), Entry.PaletteId);
                (*Args)->TryGetStringField(TEXT("name"), Entry.Name);
                (*Args)->TryGetStringField(TEXT("category"), Entry.Category);
                (*Args)->TryGetStringField(TEXT("type"), Entry.Type);
                (*Args)->TryGetStringField(TEXT("creation"), Entry.Creation);
                (*Args)->TryGetStringField(TEXT("reason"), Entry.Reason);
                Out.Add(MoveTemp(Entry));
            }
        }
    }
    return Out;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FSalLevelPaletteDestinationValidationTest,
    "Loomle.Sal.Level.Query.PaletteDestinationValidation",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSalLevelPaletteDestinationValidationTest::RunTest(const FString& Parameters)
{
    FScopedLevelQueryFixture Fixture;
    FString Error;
    if (!TestTrue(TEXT("Level palette fixture builds"), Fixture.Build(Error)))
    {
        AddError(Error);
        return false;
    }
    if (!TestTrue(
            TEXT("Level palette fixture activates the loaded source"),
            Fixture.Activate(Fixture.Loaded, Error)))
    {
        AddError(Error);
        return false;
    }
    const TSharedRef<FJsonObject> Target =
        LevelTarget(Fixture.Loaded.ObjectPath);

    const TSharedPtr<FJsonObject> NoToResult = FSalModule::BuildQueryResult(
        LevelQueryArguments(Target, LevelOperation(TEXT("palette_entries"))));
    TestTrue(
        TEXT("Level palette entries requires one destination"),
        LevelHasDiagnostic(
            NoToResult,
            TEXT("validation.palette_context_invalid")));

    const TSharedPtr<FJsonObject> WrongAliasResult =
        FSalModule::BuildQueryResult(
            LevelQueryArguments(
                Target,
                LevelPaletteEntriesOperation(
                    LevelPaletteMemberRef(
                        LevelPaletteLocalRef(TEXT("other")),
                        TEXT("Actors")))));
    TestTrue(
        TEXT("Level Actor Palette destination must name the bound alias"),
        LevelHasDiagnostic(
            WrongAliasResult,
            TEXT("language.invalid_object_shape")));

    const TSharedPtr<FJsonObject> BadPathResult =
        FSalModule::BuildQueryResult(
            LevelQueryArguments(
                Target,
                LevelPaletteEntriesOperation(
                    LevelPaletteMemberRef(
                        LevelPaletteLocalRef(TEXT("level_scope")),
                        TEXT("Levels")))));
    TestTrue(
        TEXT("Level Palette destination path is closed to Actors and Components"),
        LevelHasDiagnostic(
            BadPathResult,
            TEXT("validation.palette_context_invalid")));

    const TSharedPtr<FJsonObject> AliasComponentsResult =
        FSalModule::BuildQueryResult(
            LevelQueryArguments(
                Target,
                LevelPaletteEntriesOperation(
                    LevelPaletteMemberRef(
                        LevelPaletteLocalRef(TEXT("level_scope")),
                        TEXT("Components")))));
    TestTrue(
        TEXT("Level Component Palette destination requires an exact Actor"),
        LevelHasDiagnostic(
            AliasComponentsResult,
            TEXT("validation.palette_context_invalid")));

    const TSharedPtr<FJsonObject> InvalidGuidResult =
        FSalModule::BuildQueryResult(
            LevelQueryArguments(
                Target,
                LevelPaletteEntriesOperation(
                    LevelPaletteMemberRef(
                        LevelStableRef(TEXT("not-a-guid")),
                        TEXT("Components")))));
    TestTrue(
        TEXT("Level Component Palette destination rejects a malformed ActorGuid"),
        LevelHasDiagnostic(
            InvalidGuidResult,
            TEXT("validation.invalid_reference")));

    const TSharedPtr<FJsonObject> MissingActorResult =
        FSalModule::BuildQueryResult(
            LevelQueryArguments(
                Target,
                LevelPaletteEntriesOperation(
                    LevelPaletteMemberRef(
                        LevelStableRef(
                            FGuid::NewGuid().ToString(
                                EGuidFormats::DigitsWithHyphensLower)),
                        TEXT("Components")))));
    TestTrue(
        TEXT("Level Component Palette destination fails closed on an unknown Actor"),
        LevelHasDiagnostic(
            MissingActorResult,
            TEXT("resolution.object_not_found")));

    if (!Fixture.Cleanup(Error))
    {
        AddError(Error);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FSalLevelPaletteActorEntriesTest,
    "Loomle.Sal.Level.Query.PaletteActorEntries",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSalLevelPaletteActorEntriesTest::RunTest(const FString& Parameters)
{
    FScopedLevelQueryFixture Fixture;
    FString Error;
    if (!TestTrue(TEXT("Level palette fixture builds"), Fixture.Build(Error)))
    {
        AddError(Error);
        return false;
    }
    if (!TestTrue(
            TEXT("Level palette fixture activates the loaded source"),
            Fixture.Activate(Fixture.Loaded, Error)))
    {
        AddError(Error);
        return false;
    }
    const TSharedRef<FJsonObject> Target =
        LevelTarget(Fixture.Loaded.ObjectPath);
    const TSharedRef<FJsonObject> Destination = LevelPaletteMemberRef(
        LevelPaletteLocalRef(TEXT("level_scope")),
        TEXT("Actors"));

    const TSharedPtr<FJsonObject> EntriesResult = FSalModule::BuildQueryResult(
        LevelQueryArguments(
            Target,
            LevelPaletteEntriesOperation(Destination)));
    if (!TestFalse(TEXT("Actor Palette discovery has no error"), LevelHasError(EntriesResult)))
    {
        return false;
    }
    const TArray<FLevelPaletteEntryView> Entries =
        CollectLevelPaletteEntries(EntriesResult);
    if (!TestTrue(TEXT("Actor Palette discovery returns entries"), Entries.Num() > 0))
    {
        return false;
    }
    bool bAllWellFormed = true;
    for (const FLevelPaletteEntryView& Entry : Entries)
    {
        bAllWellFormed = bAllWellFormed
            && Entry.PaletteId.StartsWith(TEXT("level.actor."))
            && !Entry.Name.IsEmpty()
            && !Entry.Type.IsEmpty()
            && Entry.Creation == TEXT("unavailable")
            && !Entry.Reason.IsEmpty();
    }
    TestTrue(
        TEXT("Actor Palette entries carry opaque ids, names, types, and honest unavailability"),
        bAllWellFormed);

    const FString FirstId = Entries[0].PaletteId;
    const TSharedPtr<FJsonObject> ExactResult = FSalModule::BuildQueryResult(
        LevelQueryArguments(
            Target,
            LevelPaletteOperation(FirstId, Destination)));
    if (!TestFalse(TEXT("Exact Actor Palette replay has no error"), LevelHasError(ExactResult)))
    {
        return false;
    }
    const TArray<FLevelPaletteEntryView> ExactEntries =
        CollectLevelPaletteEntries(ExactResult);
    TestTrue(
        TEXT("Exact Actor Palette replay returns exactly one entry with the same id"),
        ExactEntries.Num() == 1
            && ExactEntries[0].PaletteId == FirstId
            && ExactEntries[0].Name == Entries[0].Name
            && ExactEntries[0].Type == Entries[0].Type);

    const TSharedPtr<FJsonObject> StaleResult = FSalModule::BuildQueryResult(
        LevelQueryArguments(
            Target,
            LevelPaletteOperation(
                TEXT("level.actor.0000000000000000000000000000000000000000"),
                Destination)));
    TestTrue(
        TEXT("Exact Actor Palette replay rejects a stale id"),
        LevelHasDiagnostic(StaleResult, TEXT("resolution.palette_not_found")));

    const TSharedPtr<FJsonObject> PagedResult = FSalModule::BuildQueryResult(
        LevelQueryArguments(
            Target,
            LevelPaletteEntriesOperation(Destination),
            5));
    const TArray<FLevelPaletteEntryView> PagedEntries =
        CollectLevelPaletteEntries(PagedResult);
    TestTrue(
        TEXT("Actor Palette discovery honors the page limit"),
        PagedEntries.Num() > 0 && PagedEntries.Num() <= 5);

    if (!Fixture.Cleanup(Error))
    {
        AddError(Error);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FSalLevelPaletteComponentEntriesTest,
    "Loomle.Sal.Level.Query.PaletteComponentEntries",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSalLevelPaletteComponentEntriesTest::RunTest(const FString& Parameters)
{
    FScopedLevelQueryFixture Fixture;
    FString Error;
    if (!TestTrue(TEXT("Level palette fixture builds"), Fixture.Build(Error)))
    {
        AddError(Error);
        return false;
    }
    if (!TestTrue(
            TEXT("Level palette fixture activates the loaded source"),
            Fixture.Activate(Fixture.Loaded, Error)))
    {
        AddError(Error);
        return false;
    }
    const TSharedRef<FJsonObject> Target =
        LevelTarget(Fixture.Loaded.ObjectPath);
    const TSharedRef<FJsonObject> Destination = LevelPaletteMemberRef(
        LevelStableRef(LevelGuidText(Fixture.AlphaId)),
        TEXT("Components"));

    const TSharedPtr<FJsonObject> EntriesResult = FSalModule::BuildQueryResult(
        LevelQueryArguments(
            Target,
            LevelPaletteEntriesOperation(Destination)));
    if (!TestFalse(TEXT("Component Palette discovery has no error"), LevelHasError(EntriesResult)))
    {
        return false;
    }
    const TArray<FLevelPaletteEntryView> Entries =
        CollectLevelPaletteEntries(EntriesResult);
    if (!TestTrue(TEXT("Component Palette discovery returns entries"), Entries.Num() > 0))
    {
        return false;
    }
    bool bAllWellFormed = true;
    for (const FLevelPaletteEntryView& Entry : Entries)
    {
        bAllWellFormed = bAllWellFormed
            && Entry.PaletteId.StartsWith(TEXT("level.component."))
            && !Entry.Name.IsEmpty()
            && !Entry.Type.IsEmpty()
            && Entry.Creation == TEXT("unavailable")
            && !Entry.Reason.IsEmpty();
    }
    TestTrue(
        TEXT("Component Palette entries carry opaque ids, names, types, and honest unavailability"),
        bAllWellFormed);

    const TSharedPtr<FJsonObject> ExactResult = FSalModule::BuildQueryResult(
        LevelQueryArguments(
            Target,
            LevelPaletteOperation(Entries[0].PaletteId, Destination)));
    if (!TestFalse(TEXT("Exact Component Palette replay has no error"), LevelHasError(ExactResult)))
    {
        return false;
    }
    const TArray<FLevelPaletteEntryView> ExactEntries =
        CollectLevelPaletteEntries(ExactResult);
    TestTrue(
        TEXT("Exact Component Palette replay returns the same entry"),
        ExactEntries.Num() == 1
            && ExactEntries[0].PaletteId == Entries[0].PaletteId);

    if (!Fixture.Cleanup(Error))
    {
        AddError(Error);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FSalLevelPaletteUnloadedLevelTest,
    "Loomle.Sal.Level.Query.PaletteUnloadedLevel",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSalLevelPaletteUnloadedLevelTest::RunTest(const FString& Parameters)
{
    FScopedLevelQueryFixture Fixture;
    FString Error;
    if (!TestTrue(TEXT("Level palette fixture builds"), Fixture.Build(Error)))
    {
        AddError(Error);
        return false;
    }
    // The fixture's Unloaded map is saved on disk but never loaded into any
    // Editor World; its object path must fail closed for Palette discovery.
    const TSharedRef<FJsonObject> Target =
        LevelTarget(Fixture.Unloaded.ObjectPath);
    const TSharedRef<FJsonObject> Destination = LevelPaletteMemberRef(
        LevelPaletteLocalRef(TEXT("level_scope")),
        TEXT("Actors"));

    const TSharedPtr<FJsonObject> Result = FSalModule::BuildQueryResult(
        LevelQueryArguments(
            Target,
            LevelPaletteEntriesOperation(Destination)));
    TestTrue(
        TEXT("Actor Palette discovery fails closed on an unloaded source map"),
        LevelHasDiagnostic(Result, TEXT("capability.level_not_loaded")));

    if (!Fixture.Cleanup(Error))
    {
        AddError(Error);
    }
    return true;
}

}

#endif
