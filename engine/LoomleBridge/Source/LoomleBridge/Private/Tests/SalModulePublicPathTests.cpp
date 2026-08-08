// Copyright 2026 Loomle contributors.

#if WITH_DEV_AUTOMATION_TESTS

#include "Sal/SalJson.h"
#include "Sal/SalModule.h"
#include "Sal/Graph/SalGraphInterface.h"
#include "LoomleTestObjectIteration.h"
#include "SalStateTreeTestSchema.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "BlueprintActionDatabase.h"
#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetBlueprintGeneratedClass.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Button.h"
#include "Components/CanvasPanel.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraphSchema_K2.h"
#include "Editor.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "GameFramework/Actor.h"
#include "K2Node_CallFunction.h"
#include "K2Node_CustomEvent.h"
#include "K2Node_FunctionEntry.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/AutomationTest.h"
#include "StateTree.h"
#include "StateTreeEditingSubsystem.h"
#include "StateTreeEditorData.h"
#include "StateTreeState.h"
#include "UObject/GarbageCollection.h"
#include "UObject/Package.h"
#include "UObject/UObjectHash.h"
#include "WidgetBlueprint.h"

namespace
{
using namespace Loomle::Sal;

TSharedRef<FJsonObject> PublicPathDomainTarget(const FString& Domain)
{
    TSharedRef<FJsonObject> Target = MakeShared<FJsonObject>();
    Target->SetStringField(TEXT("kind"), TEXT("target"));
    Target->SetStringField(TEXT("domain"), Domain);
    return Target;
}

TSharedRef<FJsonObject> PublicPathClassCall(const FString& Path)
{
    TSharedRef<FJsonObject> Target =
        PublicPathDomainTarget(TEXT("class"));
    Target->SetStringField(TEXT("path"), Path);
    return Target;
}

TSharedRef<FJsonObject> PublicPathTarget(
    const FString& Alias,
    const TSharedRef<FJsonObject>& Value)
{
    TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetStringField(TEXT("alias"), Alias);
    Result->SetObjectField(TEXT("target"), Value);
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

TSharedRef<FJsonObject> PublicPathQueryArguments(
    const FString& Alias,
    const TSharedRef<FJsonObject>& Value,
    const TSharedRef<FJsonObject>& Operation)
{
    TSharedRef<FJsonObject> Query = MakeShared<FJsonObject>();
    Query->SetStringField(TEXT("kind"), TEXT("query"));
    Query->SetObjectField(TEXT("target"), PublicPathTarget(Alias, Value));
    Query->SetObjectField(TEXT("operation"), Operation);

    TSharedRef<FJsonObject> Arguments = MakeShared<FJsonObject>();
    Arguments->SetObjectField(TEXT("object"), Query);
    return Arguments;
}

TSharedRef<FJsonObject> PublicPathOperation(const FString& Kind)
{
    TSharedRef<FJsonObject> Operation = MakeShared<FJsonObject>();
    Operation->SetStringField(TEXT("kind"), Kind);
    return Operation;
}

TSharedRef<FJsonObject> PublicPathAssetRoot()
{
    return PublicPathDomainTarget(TEXT("asset"));
}

TSharedRef<FJsonObject> PublicPathAssetCall(
    const FString& Path,
    const FString& Type = FString())
{
    TSharedRef<FJsonObject> Target =
        PublicPathDomainTarget(TEXT("asset"));
    Target->SetStringField(TEXT("path"), Path);
    if (!Type.IsEmpty())
    {
        Target->SetStringField(TEXT("type"), Type);
    }
    return Target;
}

TSharedRef<FJsonObject> PublicPathBlueprintCall(
    const FString& Path,
    const FGuid& Id)
{
    TSharedRef<FJsonObject> Target =
        PublicPathDomainTarget(TEXT("blueprint"));
    Target->SetStringField(TEXT("asset"), Path);
    Target->SetStringField(
        TEXT("id"),
        Id.ToString(EGuidFormats::DigitsWithHyphensLower));
    return Target;
}

TSharedRef<FJsonObject> PublicPathGraphCall(
    const FString& BlueprintPath,
    const FGuid& BlueprintId,
    const UEdGraph* Graph)
{
    TSharedRef<FJsonObject> Target =
        PublicPathDomainTarget(TEXT("graph"));
    Target->SetStringField(TEXT("asset"), BlueprintPath);
    Target->SetStringField(
        TEXT("blueprintId"),
        BlueprintId.ToString(EGuidFormats::DigitsWithHyphensLower));
    Target->SetStringField(
        TEXT("id"),
        Graph != nullptr
            ? Graph->GraphGuid.ToString(
                EGuidFormats::DigitsWithHyphensLower)
            : FString());
    return Target;
}

TSharedRef<FJsonObject> PublicPathWidgetTarget(
    const FString& Path,
    const FGuid& Id)
{
    TSharedRef<FJsonObject> Target =
        PublicPathDomainTarget(TEXT("widget"));
    Target->SetStringField(TEXT("asset"), Path);
    Target->SetStringField(
        TEXT("id"),
        Id.ToString(EGuidFormats::DigitsWithHyphensLower));
    return Target;
}

TSharedRef<FJsonObject> PublicPathStateTreeTarget(
    const FString& Path,
    const FString& Type)
{
    TSharedRef<FJsonObject> Target =
        PublicPathDomainTarget(TEXT("state_tree"));
    Target->SetStringField(TEXT("asset"), Path);
    Target->SetStringField(TEXT("type"), Type);
    return Target;
}

TSharedRef<FJsonObject> PublicPathPatchArguments(
    const FString& Alias,
    const TSharedRef<FJsonObject>& Value,
    const TArray<TSharedPtr<FJsonValue>>& Statements)
{
    TSharedRef<FJsonObject> Patch = MakeShared<FJsonObject>();
    Patch->SetStringField(TEXT("kind"), TEXT("patch"));
    Patch->SetObjectField(TEXT("target"), PublicPathTarget(Alias, Value));
    Patch->SetBoolField(TEXT("dryRun"), true);
    Patch->SetArrayField(TEXT("statements"), Statements);

    TSharedRef<FJsonObject> Arguments = MakeShared<FJsonObject>();
    Arguments->SetObjectField(TEXT("object"), Patch);
    return Arguments;
}

TSharedPtr<FJsonValue> PublicPathStatement(
    const TSharedRef<FJsonObject>& Statement)
{
    return MakeShared<FJsonValueObject>(Statement);
}

TSharedRef<FJsonObject> PublicPathSaveStatement()
{
    TSharedRef<FJsonObject> Save = MakeShared<FJsonObject>();
    Save->SetStringField(TEXT("kind"), TEXT("save"));
    return Save;
}

TSharedRef<FJsonObject> PublicPathSetMemberStatement(
    const TSharedRef<FJsonObject>& Owner,
    const FString& Property,
    const TSharedPtr<FJsonValue>& Value)
{
    TSharedRef<FJsonObject> Member = MakeShared<FJsonObject>();
    Member->SetStringField(TEXT("kind"), TEXT("member"));
    Member->SetObjectField(TEXT("object"), Owner);
    Member->SetArrayField(
        TEXT("path"),
        {MakeShared<FJsonValueString>(Property)});

    TSharedRef<FJsonObject> Set = MakeShared<FJsonObject>();
    Set->SetStringField(TEXT("kind"), TEXT("set"));
    Set->SetObjectField(TEXT("target"), Member);
    Set->SetField(TEXT("value"), Value);
    return Set;
}

TSharedRef<FJsonObject> PublicPathStableReference(
    const FString& SemanticTag,
    const FGuid& Id)
{
    TSharedRef<FJsonObject> Reference = MakeShared<FJsonObject>();
    Reference->SetStringField(TEXT("kind"), TEXT("stable_ref"));
    Reference->SetArrayField(
        TEXT("identityPath"),
        {MakeShared<FJsonValueString>(
            Id.ToString(EGuidFormats::DigitsWithHyphensLower))});
    if (!SemanticTag.IsEmpty())
    {
        Reference->SetStringField(TEXT("semanticTag"), SemanticTag);
    }
    return Reference;
}

TSharedRef<FJsonObject> PublicPathStableReference(
    const FString& SemanticTag,
    const FGuid& OwnerId,
    const FGuid& LocalId)
{
    TSharedRef<FJsonObject> Reference = MakeShared<FJsonObject>();
    Reference->SetStringField(TEXT("kind"), TEXT("stable_ref"));
    Reference->SetArrayField(
        TEXT("identityPath"),
        {
            MakeShared<FJsonValueString>(
                OwnerId.ToString(EGuidFormats::DigitsWithHyphensLower)),
            MakeShared<FJsonValueString>(
                LocalId.ToString(EGuidFormats::DigitsWithHyphensLower))
        });
    if (!SemanticTag.IsEmpty())
    {
        Reference->SetStringField(TEXT("semanticTag"), SemanticTag);
    }
    return Reference;
}

TSharedRef<FJsonObject> PublicPathExactObjectOperation(
    const TSharedRef<FJsonObject>& Reference)
{
    TSharedRef<FJsonObject> Operation = MakeShared<FJsonObject>();
    Operation->SetStringField(TEXT("kind"), TEXT("object"));
    Operation->SetObjectField(TEXT("target"), Reference);
    return Operation;
}

TSharedRef<FJsonObject> PublicPathTargetSelfReference()
{
    TSharedRef<FJsonObject> Reference = MakeShared<FJsonObject>();
    Reference->SetStringField(TEXT("kind"), TEXT("target_self"));
    return Reference;
}

TSharedRef<FJsonObject> PublicPathTargetSelfMemberReference(
    const FString& Member)
{
    TSharedRef<FJsonObject> Reference = MakeShared<FJsonObject>();
    Reference->SetStringField(TEXT("kind"), TEXT("member"));
    Reference->SetObjectField(
        TEXT("object"),
        PublicPathTargetSelfReference());
    Reference->SetArrayField(
        TEXT("path"),
        {MakeShared<FJsonValueString>(Member)});
    return Reference;
}

TSharedRef<FJsonObject> PublicPathReferencesOperation(
    const TSharedRef<FJsonObject>& Subject)
{
    TSharedRef<FJsonObject> Operation = MakeShared<FJsonObject>();
    Operation->SetStringField(TEXT("kind"), TEXT("references"));
    Operation->SetObjectField(TEXT("target"), Subject);
    return Operation;
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

TSharedRef<FJsonObject> PublicPathObjectExpression(
    const TSharedRef<FJsonObject>& Fields,
    const FString& SemanticTag = FString())
{
    TSharedRef<FJsonObject> Expression = MakeShared<FJsonObject>();
    Expression->SetStringField(TEXT("kind"), TEXT("object"));
    Expression->SetObjectField(TEXT("fields"), Fields);
    if (!SemanticTag.IsEmpty())
    {
        Expression->SetStringField(
            TEXT("semanticTag"),
            SemanticTag);
    }
    return Expression;
}

TSharedRef<FJsonObject> PublicPathCreationBinding(
    const TSharedRef<FJsonObject>& Target,
    const TSharedRef<FJsonObject>& Fields)
{
    TSharedRef<FJsonObject> Binding = MakeShared<FJsonObject>();
    Binding->SetObjectField(TEXT("target"), Target);
    Binding->SetObjectField(
        TEXT("value"),
        PublicPathObjectExpression(Fields));
    return Binding;
}

TSharedRef<FJsonObject> PublicPathAddStatement(
    const TSharedRef<FJsonObject>& Target,
    const TSharedPtr<FJsonObject>& Destination = nullptr)
{
    TSharedRef<FJsonObject> Add = MakeShared<FJsonObject>();
    Add->SetStringField(TEXT("kind"), TEXT("add"));
    Add->SetObjectField(TEXT("target"), Target);
    if (Destination.IsValid())
    {
        Add->SetObjectField(TEXT("to"), Destination);
    }
    return Add;
}

FString PublicPathPinTypeText(const FName Category)
{
    FEdGraphPinType Type;
    Type.PinCategory = Category;
    FString Text;
    FEdGraphPinType::StaticStruct()->ExportText(
        Text,
        &Type,
        nullptr,
        nullptr,
        PPF_None,
        nullptr);
    return Text;
}

FString PublicPathFirstPaletteId(
    const TSharedPtr<FJsonObject>& Result)
{
    const TSharedPtr<FJsonObject>* Object = nullptr;
    const TArray<TSharedPtr<FJsonValue>>* Statements = nullptr;
    if (!Result.IsValid()
        || !Result->TryGetObjectField(TEXT("object"), Object)
        || Object == nullptr
        || !(*Object)->TryGetArrayField(TEXT("statements"), Statements)
        || Statements == nullptr)
    {
        return FString();
    }
    for (const TSharedPtr<FJsonValue>& Value : *Statements)
    {
        const TSharedPtr<FJsonObject>* Statement = nullptr;
        const TSharedPtr<FJsonObject>* Expression = nullptr;
        const TSharedPtr<FJsonObject>* Fields = nullptr;
        FString Palette;
        if (Value.IsValid()
            && Value->TryGetObject(Statement)
            && Statement != nullptr
            && (*Statement)->TryGetObjectField(
                TEXT("value"),
                Expression)
            && Expression != nullptr
            && (*Expression)->TryGetObjectField(
                TEXT("fields"),
                Fields)
            && Fields != nullptr
            && (*Fields)->TryGetStringField(
                TEXT("palette"),
                Palette)
            && !Palette.IsEmpty())
        {
            return Palette;
        }
    }
    return FString();
}

FString PublicPathPaletteIdForType(
    const TSharedPtr<FJsonObject>& Result,
    const FString& ExpectedType)
{
    const TSharedPtr<FJsonObject>* Object = nullptr;
    const TArray<TSharedPtr<FJsonValue>>* Statements = nullptr;
    if (!Result.IsValid()
        || !Result->TryGetObjectField(TEXT("object"), Object)
        || Object == nullptr
        || !(*Object)->TryGetArrayField(TEXT("statements"), Statements)
        || Statements == nullptr)
    {
        return FString();
    }
    for (const TSharedPtr<FJsonValue>& Value : *Statements)
    {
        const TSharedPtr<FJsonObject>* Statement = nullptr;
        const TSharedPtr<FJsonObject>* Expression = nullptr;
        const TSharedPtr<FJsonObject>* Fields = nullptr;
        FString Type;
        FString Palette;
        if (Value.IsValid()
            && Value->TryGetObject(Statement)
            && Statement != nullptr
            && (*Statement)->TryGetObjectField(
                TEXT("value"),
                Expression)
            && Expression != nullptr
            && (*Expression)->TryGetObjectField(
                TEXT("fields"),
                Fields)
            && Fields != nullptr
            && (*Fields)->TryGetStringField(TEXT("type"), Type)
            && Type == ExpectedType
            && (*Fields)->TryGetStringField(
                TEXT("palette"),
                Palette)
            && !Palette.IsEmpty())
        {
            return Palette;
        }
    }
    return FString();
}

FString PublicPathPaletteEntriesSummary(
    const TSharedPtr<FJsonObject>& Result)
{
    const TSharedPtr<FJsonObject>* Object = nullptr;
    const TArray<TSharedPtr<FJsonValue>>* Statements = nullptr;
    if (!Result.IsValid()
        || !Result->TryGetObjectField(TEXT("object"), Object)
        || Object == nullptr
        || !(*Object)->TryGetArrayField(TEXT("statements"), Statements)
        || Statements == nullptr)
    {
        return TEXT("<missing statements>");
    }
    TArray<FString> Entries;
    for (const TSharedPtr<FJsonValue>& Value : *Statements)
    {
        const TSharedPtr<FJsonObject>* Statement = nullptr;
        const TSharedPtr<FJsonObject>* Expression = nullptr;
        const TSharedPtr<FJsonObject>* Fields = nullptr;
        FString Type;
        FString Palette;
        if (Value.IsValid()
            && Value->TryGetObject(Statement)
            && Statement != nullptr
            && (*Statement)->TryGetObjectField(
                TEXT("value"),
                Expression)
            && Expression != nullptr
            && (*Expression)->TryGetObjectField(
                TEXT("fields"),
                Fields)
            && Fields != nullptr
            && (*Fields)->TryGetStringField(TEXT("palette"), Palette))
        {
            (*Fields)->TryGetStringField(TEXT("type"), Type);
            Entries.Add(Type + TEXT(" | ") + Palette);
        }
    }
    return Entries.IsEmpty()
        ? TEXT("<none>")
        : FString::Join(Entries, TEXT(" || "));
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

bool PublicPathHasDiagnosticSuggestion(
    const TSharedPtr<FJsonObject>& Result,
    const FString& ExpectedCode,
    const FString& ExpectedText)
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
        FString Suggestion;
        if (DiagnosticValue.IsValid()
            && DiagnosticValue->TryGetObject(Diagnostic)
            && Diagnostic != nullptr
            && (*Diagnostic)->TryGetStringField(TEXT("code"), Code)
            && Code == ExpectedCode
            && (*Diagnostic)->TryGetStringField(
                TEXT("suggestion"),
                Suggestion)
            && Suggestion.Contains(ExpectedText))
        {
            return true;
        }
    }
    return false;
}

bool PublicPathHasError(
    const TSharedPtr<FJsonObject>& Result)
{
    const TArray<TSharedPtr<FJsonValue>>* Diagnostics = nullptr;
    if (!Result.IsValid()
        || !Result->TryGetArrayField(TEXT("diagnostics"), Diagnostics)
        || Diagnostics == nullptr)
    {
        return true;
    }
    for (const TSharedPtr<FJsonValue>& DiagnosticValue : *Diagnostics)
    {
        const TSharedPtr<FJsonObject>* Diagnostic = nullptr;
        FString Severity;
        if (DiagnosticValue.IsValid()
            && DiagnosticValue->TryGetObject(Diagnostic)
            && Diagnostic != nullptr
            && (*Diagnostic)->TryGetStringField(
                TEXT("severity"),
                Severity)
            && Severity == TEXT("error"))
        {
            return true;
        }
    }
    return false;
}

FString PublicPathDiagnosticSummary(
    const TSharedPtr<FJsonObject>& Result)
{
    const TArray<TSharedPtr<FJsonValue>>* Diagnostics = nullptr;
    if (!Result.IsValid()
        || !Result->TryGetArrayField(TEXT("diagnostics"), Diagnostics)
        || Diagnostics == nullptr)
    {
        return TEXT("<missing diagnostics>");
    }
    TArray<FString> Lines;
    for (const TSharedPtr<FJsonValue>& DiagnosticValue : *Diagnostics)
    {
        const TSharedPtr<FJsonObject>* Diagnostic = nullptr;
        FString Code;
        FString Message;
        if (DiagnosticValue.IsValid()
            && DiagnosticValue->TryGetObject(Diagnostic)
            && Diagnostic != nullptr)
        {
            (*Diagnostic)->TryGetStringField(TEXT("code"), Code);
            (*Diagnostic)->TryGetStringField(TEXT("message"), Message);
        }
        Lines.Add(Code.IsEmpty()
            ? Message
            : Code + TEXT(": ") + Message);
    }
    return Lines.IsEmpty()
        ? TEXT("<none>")
        : FString::Join(Lines, TEXT(" | "));
}

bool PublicPathHasComment(
    const TSharedPtr<FJsonObject>& Result,
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
        FString Kind;
        FString Text;
        if (StatementValue.IsValid()
            && StatementValue->TryGetObject(Statement)
            && Statement != nullptr
            && (*Statement)->TryGetStringField(TEXT("kind"), Kind)
            && Kind == TEXT("comment")
            && (*Statement)->TryGetStringField(TEXT("text"), Text)
            && Text.Contains(Expected))
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
    const TSharedPtr<FJsonObject>* Binding = nullptr;
    const TSharedPtr<FJsonObject>* Target = nullptr;
    FString Domain;
    FString Path;
    if (Result.IsValid()
        && Result->TryGetObjectField(TEXT("target"), Binding)
        && Binding != nullptr
        && (*Binding)->TryGetObjectField(TEXT("target"), Target)
        && Target != nullptr
        && (*Target)->TryGetStringField(TEXT("domain"), Domain)
        && Domain == Callee
        && ((*Target)->TryGetStringField(TEXT("path"), Path)
            || (*Target)->TryGetStringField(TEXT("asset"), Path))
        && Path == ExpectedPath)
    {
        return true;
    }
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
        FString Kind;
        FString ActualCallee;
        FString ActualPath;
        const bool bReservedDomainTag =
            Callee == TEXT("asset")
            || Callee == TEXT("blueprint")
            || Callee == TEXT("class")
            || Callee == TEXT("graph")
            || Callee == TEXT("state_tree")
            || Callee == TEXT("widget");
        if (StatementValue.IsValid()
            && StatementValue->TryGetObject(Statement)
            && Statement != nullptr
            && (*Statement)->TryGetObjectField(TEXT("value"), Value)
            && Value != nullptr
            && (*Value)->TryGetStringField(TEXT("kind"), Kind)
            && Kind == TEXT("object")
            && ((!(*Value)->HasField(TEXT("semanticTag"))
                    && bReservedDomainTag)
                || ((*Value)->TryGetStringField(
                        TEXT("semanticTag"),
                        ActualCallee)
                    && ActualCallee == Callee))
            && (*Value)->TryGetObjectField(TEXT("fields"), Args)
            && Args != nullptr
            && (*Args)->TryGetStringField(TEXT("path"), ActualPath)
            && ActualPath == ExpectedPath)
        {
            return true;
        }
    }
    return false;
}

bool PublicPathHasUntaggedObjectField(
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
        const TSharedPtr<FJsonObject>* Value = nullptr;
        const TSharedPtr<FJsonObject>* Fields = nullptr;
        FString Kind;
        FString Actual;
        if (StatementValue.IsValid()
            && StatementValue->TryGetObject(Statement)
            && Statement != nullptr
            && (*Statement)->TryGetObjectField(TEXT("value"), Value)
            && Value != nullptr
            && (*Value)->TryGetStringField(TEXT("kind"), Kind)
            && Kind == TEXT("object")
            && !(*Value)->HasField(TEXT("semanticTag"))
            && (*Value)->TryGetObjectField(TEXT("fields"), Fields)
            && Fields != nullptr
            && (*Fields)->TryGetStringField(Field, Actual)
            && Actual == Expected)
        {
            return true;
        }
    }
    return false;
}

bool PublicPathHasCallId(
    const TSharedPtr<FJsonObject>& Result,
    const FString& Callee,
    const FString& ExpectedId)
{
    const TSharedPtr<FJsonObject>* Binding = nullptr;
    const TSharedPtr<FJsonObject>* Target = nullptr;
    FString Domain;
    FString Id;
    if (Result.IsValid()
        && Result->TryGetObjectField(TEXT("target"), Binding)
        && Binding != nullptr
        && (*Binding)->TryGetObjectField(TEXT("target"), Target)
        && Target != nullptr
        && (*Target)->TryGetStringField(TEXT("domain"), Domain)
        && Domain == Callee
        && (*Target)->TryGetStringField(TEXT("id"), Id)
        && Id == ExpectedId)
    {
        return true;
    }
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
        FString Kind;
        FString ActualCallee;
        FString ActualId;
        if (StatementValue.IsValid()
            && StatementValue->TryGetObject(Statement)
            && Statement != nullptr
            && (*Statement)->TryGetObjectField(TEXT("value"), Value)
            && Value != nullptr
            && (*Value)->TryGetStringField(TEXT("kind"), Kind)
            && Kind == TEXT("object")
            && (*Value)->TryGetStringField(
                TEXT("semanticTag"),
                ActualCallee)
            && ActualCallee == Callee
            && (*Value)->TryGetObjectField(TEXT("fields"), Args)
            && Args != nullptr
            && (*Args)->TryGetStringField(TEXT("id"), ActualId)
            && ActualId == ExpectedId)
        {
            return true;
        }
    }
    return false;
}

bool PublicPathHasTargetMember(
    const TSharedPtr<FJsonObject>& Result,
    const FString& TargetAlias,
    const FString& Member)
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
        const TSharedPtr<FJsonObject>* Owner = nullptr;
        const TArray<TSharedPtr<FJsonValue>>* Path = nullptr;
        FString Kind;
        FString OwnerKind;
        FString OwnerName;
        FString Segment;
        if (StatementValue.IsValid()
            && StatementValue->TryGetObject(Statement)
            && Statement != nullptr
            && (*Statement)->HasField(TEXT("value"))
            && (*Statement)->TryGetObjectField(TEXT("target"), Target)
            && Target != nullptr
            && (*Target)->TryGetStringField(TEXT("kind"), Kind)
            && Kind == TEXT("member")
            && (*Target)->TryGetObjectField(TEXT("object"), Owner)
            && Owner != nullptr
            && (*Owner)->TryGetStringField(TEXT("kind"), OwnerKind)
            && OwnerKind == TEXT("local")
            && (*Owner)->TryGetStringField(TEXT("name"), OwnerName)
            && OwnerName == TargetAlias
            && (*Target)->TryGetArrayField(TEXT("path"), Path)
            && Path != nullptr
            && Path->Num() == 1
            && (*Path)[0].IsValid()
            && (*Path)[0]->TryGetString(Segment)
            && Segment == Member)
        {
            return true;
        }
    }
    return false;
}

bool PublicPathHasHandoff(
    const TSharedPtr<FJsonObject>& Result,
    const FString& Purpose,
    const FString& Domain,
    const FString& Id)
{
    const TArray<TSharedPtr<FJsonValue>>* Related = nullptr;
    const TArray<TSharedPtr<FJsonValue>>* Handoffs = nullptr;
    if (!Result.IsValid()
        || !Result->TryGetArrayField(TEXT("relatedTargets"), Related)
        || Related == nullptr
        || !Result->TryGetArrayField(TEXT("handoffs"), Handoffs)
        || Handoffs == nullptr)
    {
        return false;
    }
    TSet<FString> MatchingAliases;
    for (const TSharedPtr<FJsonValue>& Value : *Related)
    {
        const TSharedPtr<FJsonObject>* Binding = nullptr;
        const TSharedPtr<FJsonObject>* Target = nullptr;
        FString Alias;
        FString ActualDomain;
        FString ActualId;
        if (Value.IsValid()
            && Value->TryGetObject(Binding)
            && Binding != nullptr
            && (*Binding)->TryGetStringField(TEXT("alias"), Alias)
            && (*Binding)->TryGetObjectField(TEXT("target"), Target)
            && Target != nullptr
            && (*Target)->TryGetStringField(
                TEXT("domain"),
                ActualDomain)
            && ActualDomain == Domain
            && (Id.IsEmpty()
                || ((*Target)->TryGetStringField(
                        TEXT("id"),
                        ActualId)
                    && ActualId == Id)))
        {
            MatchingAliases.Add(Alias);
        }
    }
    for (const TSharedPtr<FJsonValue>& Value : *Handoffs)
    {
        const TSharedPtr<FJsonObject>* Handoff = nullptr;
        const TSharedPtr<FJsonObject>* Ref = nullptr;
        FString ActualPurpose;
        FString Alias;
        if (Value.IsValid()
            && Value->TryGetObject(Handoff)
            && Handoff != nullptr
            && (*Handoff)->TryGetStringField(
                TEXT("purpose"),
                ActualPurpose)
            && ActualPurpose == Purpose
            && (*Handoff)->TryGetObjectField(TEXT("target"), Ref)
            && Ref != nullptr
            && (*Ref)->TryGetStringField(TEXT("name"), Alias)
            && MatchingAliases.Contains(Alias))
        {
            return true;
        }
    }
    return false;
}

void PublicPathEraseSemanticTags(
    const TSharedPtr<FJsonValue>& Value)
{
    if (!Value.IsValid() || Value->IsNull())
    {
        return;
    }
    const TArray<TSharedPtr<FJsonValue>>* Array = nullptr;
    if (Value->TryGetArray(Array) && Array != nullptr)
    {
        for (const TSharedPtr<FJsonValue>& Item : *Array)
        {
            PublicPathEraseSemanticTags(Item);
        }
        return;
    }
    const TSharedPtr<FJsonObject>* Object = nullptr;
    if (!Value->TryGetObject(Object)
        || Object == nullptr
        || !(*Object).IsValid())
    {
        return;
    }
    (*Object)->RemoveField(TEXT("semanticTag"));
    for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair :
         (*Object)->Values)
    {
        PublicPathEraseSemanticTags(Pair.Value);
    }
}

void PublicPathEraseSemanticTags(
    const TSharedPtr<FJsonObject>& Object)
{
    if (Object.IsValid())
    {
        PublicPathEraseSemanticTags(
            MakeShared<FJsonValueObject>(Object));
    }
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
        Loomle::Tests::IncludeNestedObjects);

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
        if (Blueprint != nullptr)
        {
            const FName VariableName(TEXT("PublicPathValue"));
            const FEdGraphPinType VariableType(
                UEdGraphSchema_K2::PC_Int,
                NAME_None,
                nullptr,
                EPinContainerType::None,
                false,
                FEdGraphTerminalType());
            FBlueprintEditorUtils::AddMemberVariable(
                Blueprint,
                VariableName,
                VariableType);
            VariableId =
                FBlueprintEditorUtils::FindMemberVariableGuidByName(
                    Blueprint,
                    VariableName);
            const FName DispatcherName(TEXT("PublicPathSignal"));
            FEdGraphPinType DispatcherType;
            DispatcherType.PinCategory =
                UEdGraphSchema_K2::PC_MCDelegate;
            if (FBlueprintEditorUtils::AddMemberVariable(
                    Blueprint,
                    DispatcherName,
                    DispatcherType))
            {
                DispatcherId =
                    FBlueprintEditorUtils::FindMemberVariableGuidByName(
                        Blueprint,
                        DispatcherName);
                DispatcherGraph =
                    FBlueprintEditorUtils::CreateNewGraph(
                        Blueprint,
                        DispatcherName,
                        UEdGraph::StaticClass(),
                        UEdGraphSchema_K2::StaticClass());
                const UEdGraphSchema_K2* Schema =
                    GetDefault<UEdGraphSchema_K2>();
                if (DispatcherGraph != nullptr
                    && Schema != nullptr)
                {
                    Schema->CreateDefaultNodesForGraph(
                        *DispatcherGraph);
                    Schema->CreateFunctionGraphTerminators(
                        *DispatcherGraph,
                        static_cast<UClass*>(nullptr));
                    Schema->AddExtraFunctionFlags(
                        DispatcherGraph,
                        FUNC_BlueprintCallable
                            | FUNC_BlueprintEvent
                            | FUNC_Public);
                    Schema->MarkFunctionEntryAsEditable(
                        DispatcherGraph,
                        true);
                    Blueprint->DelegateSignatureGraphs.Add(
                        DispatcherGraph);
                }
            }
            Graph =
                FBlueprintEditorUtils::FindEventGraph(Blueprint);
            if (Graph != nullptr)
            {
                FGraphNodeCreator<UK2Node_CustomEvent> NodeCreator(
                    *Graph);
                Node = NodeCreator.CreateNode(false);
                Node->CustomFunctionName =
                    TEXT("PublicPathRoutingEvent");
                Node->NodePosX = 100;
                Node->NodePosY = 200;
                NodeCreator.Finalize();
            }
            FunctionGraph = FBlueprintEditorUtils::CreateNewGraph(
                Blueprint,
                TEXT("PublicPathFunction"),
                UEdGraph::StaticClass(),
                UEdGraphSchema_K2::StaticClass());
            if (FunctionGraph != nullptr)
            {
                FBlueprintEditorUtils::AddFunctionGraph<UClass>(
                    Blueprint,
                    FunctionGraph,
                    true,
                    nullptr);
                TArray<UK2Node_FunctionEntry*> Entries;
                FunctionGraph->GetNodesOfClass(Entries);
                if (Entries.Num() == 1 && Entries[0] != nullptr)
                {
                    FBPVariableDescription Local;
                    Local.VarName = TEXT("PublicPathLocal");
                    Local.VarGuid = FGuid::NewGuid();
                    Local.VarType = VariableType;
                    Entries[0]->LocalVariables.Add(Local);
                    LocalVariableId = Local.VarGuid;
                }
            }
            FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(
                Blueprint);
            FKismetEditorUtilities::CompileBlueprint(Blueprint);
            FAssetRegistryModule::AssetCreated(Blueprint);
            bRegistered = true;
        }
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
        if (Blueprint != nullptr && bRegistered)
        {
            FAssetRegistryModule::AssetDeleted(Blueprint);
            bRegistered = false;
        }
        if (Blueprint != nullptr)
        {
            Blueprint->ClearFlags(RF_Public | RF_Standalone);
        }
        ActorCDO = nullptr;
        Node = nullptr;
        Graph = nullptr;
        FunctionGraph = nullptr;
        DispatcherGraph = nullptr;
        Class = nullptr;
        Blueprint = nullptr;
        Package = nullptr;
        return UnloadPublicPathFixturePackage(PackageToUnload, OutError);
    }

    UPackage* Package = nullptr;
    UBlueprint* Blueprint = nullptr;
    UBlueprintGeneratedClass* Class = nullptr;
    AActor* ActorCDO = nullptr;
    UEdGraph* Graph = nullptr;
    UEdGraph* FunctionGraph = nullptr;
    UEdGraph* DispatcherGraph = nullptr;
    UK2Node_CustomEvent* Node = nullptr;
    FGuid VariableId;
    FGuid DispatcherId;
    FGuid LocalVariableId;

private:
    bool bRegistered = false;
    bool bCleaned = false;
};

class FPublicPathWidgetFixture
{
public:
    FPublicPathWidgetFixture()
    {
        const FString Token =
            FGuid::NewGuid().ToString(EGuidFormats::Digits);
        Package = CreatePackage(*FString::Printf(
            TEXT("/Game/LoomleTests/PublicPathWidget_%s"),
            *Token));
        Blueprint = Cast<UWidgetBlueprint>(
            FKismetEditorUtilities::CreateBlueprint(
                UUserWidget::StaticClass(),
                Package,
                *FString::Printf(
                    TEXT("WBP_PublicPathWidget_%s"),
                    *Token),
                BPTYPE_Normal,
                UWidgetBlueprint::StaticClass(),
                UWidgetBlueprintGeneratedClass::StaticClass(),
                NAME_None));
        if (Blueprint != nullptr
            && Blueprint->WidgetTree != nullptr)
        {
            for (UEdGraph* Candidate : Blueprint->UbergraphPages)
            {
                if (Candidate != nullptr
                    && Candidate->GraphGuid.IsValid())
                {
                    EventGraph = Candidate;
                    break;
                }
            }
            if (EventGraph == nullptr)
            {
                EventGraph = FBlueprintEditorUtils::CreateNewGraph(
                    Blueprint,
                    TEXT("EventGraph"),
                    UEdGraph::StaticClass(),
                    UEdGraphSchema_K2::StaticClass());
                if (EventGraph != nullptr)
                {
                    FBlueprintEditorUtils::AddUbergraphPage(
                        Blueprint,
                        EventGraph);
                }
            }
            Root = Blueprint->WidgetTree
                ->ConstructWidget<UCanvasPanel>(
                    UCanvasPanel::StaticClass(),
                    RootName);
            if (Root != nullptr)
            {
                Root->bIsVariable = false;
                Blueprint->WidgetTree->RootWidget = Root;
                Blueprint->OnVariableAdded(RootName);
                EventButton = Blueprint->WidgetTree
                    ->ConstructWidget<UButton>(
                        UButton::StaticClass(),
                        EventButtonName);
                if (EventButton != nullptr)
                {
                    EventButton->bIsVariable = true;
                    Root->AddChild(EventButton);
                    Blueprint->OnVariableAdded(EventButtonName);
                }
                FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(
                    Blueprint);
                FKismetEditorUtilities::CompileBlueprint(Blueprint);
                Root = Cast<UCanvasPanel>(
                    Blueprint->WidgetTree->FindWidget(RootName));
                RootId =
                    Blueprint->WidgetVariableNameToGuidMap.FindRef(
                        RootName);
                EventButton = Cast<UButton>(
                    Blueprint->WidgetTree->FindWidget(
                        EventButtonName));
                EventButtonId =
                    Blueprint->WidgetVariableNameToGuidMap.FindRef(
                        EventButtonName);
                FBlueprintActionDatabase::Get().RefreshAssetActions(
                    Blueprint);
            }
        }
        if (Package != nullptr)
        {
            Package->SetDirtyFlag(false);
        }
    }

    ~FPublicPathWidgetFixture()
    {
        FString Ignored;
        Cleanup(Ignored);
    }

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
            FBlueprintActionDatabase::Get().ClearAssetActions(
                Blueprint);
        }
        if (Blueprint != nullptr)
        {
            Blueprint->ClearFlags(RF_Public | RF_Standalone);
        }
        EventButton = nullptr;
        Root = nullptr;
        EventGraph = nullptr;
        Blueprint = nullptr;
        Package = nullptr;
        return UnloadPublicPathFixturePackage(
            PackageToUnload,
            OutError);
    }

    bool IsValid() const
    {
        return Blueprint != nullptr
            && Root != nullptr
            && RootId.IsValid()
            && EventButton != nullptr
            && EventButtonId.IsValid()
            && EventGraph != nullptr
            && EventGraph->GraphGuid.IsValid()
            && Blueprint->GeneratedClass != nullptr
            && Blueprint->Status != BS_Error;
    }

    static const FName RootName;
    static const FName EventButtonName;
    UPackage* Package = nullptr;
    UWidgetBlueprint* Blueprint = nullptr;
    UCanvasPanel* Root = nullptr;
    UButton* EventButton = nullptr;
    UEdGraph* EventGraph = nullptr;
    FGuid RootId;
    FGuid EventButtonId;

private:
    bool bCleaned = false;
};

const FName FPublicPathWidgetFixture::RootName(
    TEXT("PublicPathRoot"));
const FName FPublicPathWidgetFixture::EventButtonName(
    TEXT("PublicPathEventButton"));

class FPublicPathStateTreeFixture
{
public:
    FPublicPathStateTreeFixture()
    {
        const FString Token =
            FGuid::NewGuid().ToString(EGuidFormats::Digits);
        Package = CreatePackage(*FString::Printf(
            TEXT("/Game/LoomleTests/PublicPathState_%s"),
            *Token));
        Asset = NewObject<UStateTree>(
            Package,
            FName(TEXT("ST_PublicPath")),
            RF_Public | RF_Standalone | RF_Transactional);
        if (Asset != nullptr)
        {
            EditorData = NewObject<UStateTreeEditorData>(
                Asset,
                NAME_None,
                RF_Transactional);
            Asset->EditorData = EditorData;
            if (EditorData != nullptr)
            {
                Schema = NewObject<USalStateTreeTestSchema>(
                    EditorData);
                EditorData->Schema = Schema;
                Root = &EditorData->AddSubTree(
                    FName(TEXT("Root")));
                Root->ID = FGuid::NewGuid();

                // Match the authored state produced by UE's StateTree editor.
                // Patch preflight deliberately rejects native validation repairs,
                // so the fixture must not leave EditorSchema initialization (or
                // any other ordinary validation repair) pending.
                const FGuid RootId = Root->ID;
                UStateTreeEditingSubsystem::ValidateStateTree(Asset);
                EditorData =
                    Cast<UStateTreeEditorData>(Asset->EditorData);
                Schema = EditorData != nullptr
                    ? Cast<USalStateTreeTestSchema>(
                        EditorData->Schema)
                    : nullptr;
                Root = nullptr;
                if (EditorData != nullptr)
                {
                    for (UStateTreeState* Candidate :
                         EditorData->SubTrees)
                    {
                        if (Candidate != nullptr
                            && Candidate->ID == RootId)
                        {
                            Root = Candidate;
                            break;
                        }
                    }
                }
            }
        }
        if (Package != nullptr)
        {
            Package->SetDirtyFlag(false);
        }
    }

    ~FPublicPathStateTreeFixture()
    {
        FString Ignored;
        Cleanup(Ignored);
    }

    bool Cleanup(FString& OutError)
    {
        if (bCleaned)
        {
            OutError.Reset();
            return true;
        }
        bCleaned = true;
        UPackage* PackageToUnload = Package;
        Root = nullptr;
        Schema = nullptr;
        EditorData = nullptr;
        if (Asset != nullptr)
        {
            Asset->ClearFlags(RF_Public | RF_Standalone);
        }
        Asset = nullptr;
        Package = nullptr;
        return UnloadPublicPathFixturePackage(
            PackageToUnload,
            OutError);
    }

    bool IsValid() const
    {
        return Asset != nullptr
            && EditorData != nullptr
            && Schema != nullptr
            && Root != nullptr
            && Root->ID.IsValid();
    }

    UPackage* Package = nullptr;
    UStateTree* Asset = nullptr;
    UStateTreeEditorData* EditorData = nullptr;
    USalStateTreeTestSchema* Schema = nullptr;
    UStateTreeState* Root = nullptr;

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
    TestTrue(
        TEXT("Class mutation returns an explicit Blueprint compile handoff"),
        PublicPathHasHandoff(
            Result,
            TEXT("compile"),
            TEXT("blueprint"),
            Fixture.Blueprint->GetBlueprintGuid().ToString(
                EGuidFormats::DigitsWithHyphensLower)));

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
    FSalModuleInvalidTargetPublicPathTest,
    "Loomle.Sal.PublicPath.Query.InvalidTarget",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSalModuleInvalidTargetPublicPathTest::RunTest(const FString& Parameters)
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
        TEXT("Empty normalized Target field is rejected before resolution"),
        PublicPathHasDiagnosticCode(
            Result,
            TEXT("language.invalid_object_shape")));
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
    FString Context;
    TestTrue(
        TEXT("Decode failure returns unresolved_target context"),
        Result.IsValid()
            && Result->TryGetStringField(TEXT("targetContext"), Context)
            && Context == TEXT("unresolved_target"));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FSalModuleQueryRoutingMatrixTest,
    "Loomle.Sal.PublicPath.Query.NormalizedRoutingMatrix",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSalModuleQueryRoutingMatrixTest::RunTest(
    const FString& Parameters)
{
    FPublicPathClassFixture BlueprintFixture;
    FPublicPathWidgetFixture WidgetFixture;
    FPublicPathStateTreeFixture StateTreeFixture;
    UK2Node_CallFunction* FunctionCallNode = nullptr;
    if (BlueprintFixture.Graph != nullptr
        && BlueprintFixture.FunctionGraph != nullptr)
    {
        FunctionCallNode =
            NewObject<UK2Node_CallFunction>(
                BlueprintFixture.Graph,
                NAME_None,
                RF_Transactional);
        FunctionCallNode->CreateNewGuid();
        FunctionCallNode->FunctionReference.SetSelfMember(
            BlueprintFixture.FunctionGraph->GetFName(),
            BlueprintFixture.FunctionGraph->GraphGuid);
        BlueprintFixture.Graph->AddNode(
            FunctionCallNode,
            false,
            false);
        FunctionCallNode->AllocateDefaultPins();
    }
    TestNotNull(
        TEXT("Routing fixture creates an Actor Blueprint"),
        BlueprintFixture.Blueprint);
    TestNotNull(
        TEXT("Routing fixture creates an Event Graph"),
        BlueprintFixture.Graph);
    TestTrue(
        TEXT("Routing fixture creates a stable variable declaration"),
        BlueprintFixture.VariableId.IsValid());
    TestNotNull(
        TEXT("Routing fixture creates a cross-Graph function call"),
        FunctionCallNode);
    TestTrue(
        TEXT("Routing fixture creates a Dispatcher Signature Graph"),
        BlueprintFixture.DispatcherId.IsValid()
            && BlueprintFixture.DispatcherGraph != nullptr);
    TestTrue(
        TEXT("Routing fixture creates a valid WidgetBlueprint"),
        WidgetFixture.IsValid());
    TestTrue(
        TEXT("Routing fixture creates a valid StateTree"),
        StateTreeFixture.IsValid());
    if (BlueprintFixture.Blueprint == nullptr
        || BlueprintFixture.Graph == nullptr
        || FunctionCallNode == nullptr
        || !BlueprintFixture.VariableId.IsValid()
        || !BlueprintFixture.DispatcherId.IsValid()
        || BlueprintFixture.DispatcherGraph == nullptr
        || !WidgetFixture.IsValid()
        || !StateTreeFixture.IsValid())
    {
        return false;
    }

    TSharedRef<FJsonObject> AssetCollectionOperation =
        PublicPathOperation(TEXT("assets"));
    AssetCollectionOperation->SetStringField(
        TEXT("text"),
        BlueprintFixture.Blueprint->GetName());
    const TSharedPtr<FJsonObject> AssetRoot =
        FSalModule::BuildQueryResult(
            PublicPathQueryArguments(
                TEXT("asset_scope"),
                PublicPathAssetRoot(),
                AssetCollectionOperation));
    TestFalse(
        TEXT("Normalized Asset root Query dispatches successfully"),
        PublicPathHasError(AssetRoot));
    TestTrue(
        TEXT("Asset root Query returns the exact fixture identity"),
        PublicPathHasCallPath(
            AssetRoot,
            TEXT("asset"),
            BlueprintFixture.Blueprint->GetPathName()));
    TestTrue(
        TEXT("Reserved Domain word asset is erased rather than emitted as a semanticTag"),
        PublicPathHasUntaggedObjectField(
            AssetRoot,
            TEXT("path"),
            BlueprintFixture.Blueprint->GetPathName()));
    FString AssetRootContext;
    TestTrue(
        TEXT("Asset collection Query returns domain_root context"),
        AssetRoot.IsValid()
            && AssetRoot->TryGetStringField(
                TEXT("targetContext"),
                AssetRootContext)
            && AssetRootContext == TEXT("domain_root"));

    const TSharedPtr<FJsonObject> ExactAsset =
        FSalModule::BuildQueryResult(
            PublicPathQueryArguments(
                TEXT("asset_scope"),
                PublicPathAssetCall(
                    BlueprintFixture.Blueprint->GetPathName()),
                TEXT("assets")));
    TestTrue(
        TEXT("Exact Asset reaches the Asset interface and rejects collection-only Query"),
        PublicPathHasDiagnosticCode(
            ExactAsset,
            TEXT("capability.unsupported_query")));

    const TSharedPtr<FJsonObject> Blueprint =
        FSalModule::BuildQueryResult(
            PublicPathQueryArguments(
                TEXT("blueprint_scope"),
                PublicPathBlueprintCall(
                    BlueprintFixture.Blueprint->GetPathName(),
                    BlueprintFixture.Blueprint->GetBlueprintGuid()),
                TEXT("summary")));
    TestFalse(
        TEXT("Normalized Blueprint Query dispatches successfully"),
        PublicPathHasError(Blueprint));
    TestTrue(
        TEXT("Blueprint Query returns its exact Blueprint Target"),
        PublicPathHasCallId(
            Blueprint,
            TEXT("blueprint"),
            BlueprintFixture.Blueprint->GetBlueprintGuid().ToString(
                EGuidFormats::DigitsWithHyphensLower)));
    TestTrue(
        TEXT("Blueprint owner fields survive promotion of its alias into the Result Target table"),
        PublicPathHasTargetMember(
            Blueprint,
            TEXT("blueprint_scope"),
            TEXT("Status")));

    const TSharedPtr<FJsonObject> BlueprintGraph =
        FSalModule::BuildQueryResult(
            PublicPathQueryArguments(
                TEXT("blueprint_scope"),
                PublicPathBlueprintCall(
                    BlueprintFixture.Blueprint->GetPathName(),
                    BlueprintFixture.Blueprint->GetBlueprintGuid()),
                PublicPathExactObjectOperation(
                    PublicPathStableReference(
                        FString(),
                        BlueprintFixture.Graph->GraphGuid))));
    TestFalse(
        TEXT("Blueprint exact Graph Query dispatches successfully"),
        PublicPathHasError(BlueprintGraph));
    TestTrue(
        TEXT("Blueprint exact Graph Query returns an explicit edit handoff"),
        PublicPathHasHandoff(
            BlueprintGraph,
            TEXT("edit_graph"),
            TEXT("graph"),
            BlueprintFixture.Graph->GraphGuid.ToString(
                EGuidFormats::DigitsWithHyphensLower)));

    const TSharedPtr<FJsonObject> BlueprintDispatcher =
        FSalModule::BuildQueryResult(
            PublicPathQueryArguments(
                TEXT("blueprint_scope"),
                PublicPathBlueprintCall(
                    BlueprintFixture.Blueprint->GetPathName(),
                    BlueprintFixture.Blueprint->GetBlueprintGuid()),
                PublicPathExactObjectOperation(
                    PublicPathStableReference(
                        FString(),
                        BlueprintFixture.DispatcherId))));
    TestFalse(
        TEXT("Blueprint exact Dispatcher Query dispatches successfully"),
        PublicPathHasError(BlueprintDispatcher));
    TestTrue(
        TEXT("Blueprint exact Dispatcher Query returns its Signature Graph handoff"),
        PublicPathHasHandoff(
            BlueprintDispatcher,
            TEXT("edit_graph"),
            TEXT("graph"),
            BlueprintFixture.DispatcherGraph->GraphGuid.ToString(
                EGuidFormats::DigitsWithHyphensLower)));

    const TSharedPtr<FJsonObject> Graph =
        FSalModule::BuildQueryResult(
            PublicPathQueryArguments(
                TEXT("graph_scope"),
                PublicPathGraphCall(
                    BlueprintFixture.Blueprint->GetPathName(),
                    BlueprintFixture.Blueprint->GetBlueprintGuid(),
                    BlueprintFixture.Graph),
                TEXT("summary")));
    TestFalse(
        TEXT("Normalized Graph Query dispatches successfully"),
        PublicPathHasError(Graph));
    TestTrue(
        TEXT("Graph Query returns the exact Graph id"),
        PublicPathHasCallId(
            Graph,
            TEXT("graph"),
            BlueprintFixture.Graph->GraphGuid.ToString(
                EGuidFormats::DigitsWithHyphensLower)));
    FString GraphContext;
    TestTrue(
        TEXT("Exact Graph Query returns canonical exact_target context"),
        Graph.IsValid()
            && Graph->TryGetStringField(TEXT("targetContext"), GraphContext)
            && GraphContext == TEXT("exact_target"));

    FSalResolvedTarget DirectGraphTarget;
    DirectGraphTarget.Kind = ESalTargetKind::Graph;
    DirectGraphTarget.Domain = ESalDomain::Graph;
    DirectGraphTarget.Alias = TEXT("graph_scope");
    DirectGraphTarget.AssetPath =
        BlueprintFixture.Blueprint->GetPathName();
    DirectGraphTarget.Object = BlueprintFixture.Graph;
    DirectGraphTarget.Package =
        BlueprintFixture.Blueprint->GetOutermost();
    DirectGraphTarget.Blueprint = BlueprintFixture.Blueprint;
    DirectGraphTarget.Graph = BlueprintFixture.Graph;
    DirectGraphTarget.Interfaces = {FName(TEXT("graph"))};
    FSalQuery DirectNodeQuery;
    DirectNodeQuery.Alias = DirectGraphTarget.Alias;
    DirectNodeQuery.Operation = PublicPathOperation(TEXT("node"));
    DirectNodeQuery.Operation->SetStringField(
        TEXT("id"),
        FunctionCallNode->NodeGuid.ToString(
            EGuidFormats::DigitsWithHyphensLower));
    const TSharedPtr<FJsonObject> DirectNode =
        FSalGraphInterface::Query(
            DirectNodeQuery,
            DirectGraphTarget);
    TestTrue(
        TEXT("Graph adapter derives navigation from the native Node rather than output tags"),
        PublicPathHasHandoff(
            DirectNode,
            TEXT("navigate_graph"),
            TEXT("graph"),
            BlueprintFixture.FunctionGraph->GraphGuid.ToString(
                EGuidFormats::DigitsWithHyphensLower)));
    PublicPathEraseSemanticTags(DirectNode);
    TestTrue(
        TEXT("Erasing every presentation tag preserves the Graph navigation handoff"),
        PublicPathHasHandoff(
            DirectNode,
            TEXT("navigate_graph"),
            TEXT("graph"),
            BlueprintFixture.FunctionGraph->GraphGuid.ToString(
                EGuidFormats::DigitsWithHyphensLower)));

    const TSharedRef<FJsonObject> ExactCallOperation =
        PublicPathExactObjectOperation(
            PublicPathStableReference(
                FString(),
                FunctionCallNode->NodeGuid));
    const TSharedPtr<FJsonObject> GraphCall =
        FSalModule::BuildQueryResult(
            PublicPathQueryArguments(
                TEXT("graph_scope"),
                PublicPathGraphCall(
                    BlueprintFixture.Blueprint->GetPathName(),
                    BlueprintFixture.Blueprint->GetBlueprintGuid(),
                    BlueprintFixture.Graph),
                ExactCallOperation));
    TestTrue(
        TEXT("Public Graph Query preserves the adapter navigation handoff"),
        PublicPathHasHandoff(
            GraphCall,
            TEXT("navigate_graph"),
            TEXT("graph"),
            BlueprintFixture.FunctionGraph->GraphGuid.ToString(
                EGuidFormats::DigitsWithHyphensLower)));

    const TSharedPtr<FJsonObject> Widget =
        FSalModule::BuildQueryResult(
            PublicPathQueryArguments(
                TEXT("widget_blueprint"),
                PublicPathWidgetTarget(
                    WidgetFixture.Blueprint->GetPathName(),
                    WidgetFixture.Blueprint->GetBlueprintGuid()),
                TEXT("summary")));
    TestFalse(
        TEXT("Composed WidgetBlueprint summary routes to Widget"),
        PublicPathHasError(Widget));
    TestTrue(
        TEXT("Composed Widget Query reports authored Widget counts"),
        PublicPathHasComment(Widget, TEXT("widgets: 2")));

    TSharedRef<FJsonObject> WidgetSchemaArguments =
        PublicPathQueryArguments(
            TEXT("widget_blueprint"),
            PublicPathWidgetTarget(
                WidgetFixture.Blueprint->GetPathName(),
                WidgetFixture.Blueprint->GetBlueprintGuid()),
            PublicPathExactObjectOperation(
                PublicPathStableReference(
                    FString(),
                    WidgetFixture.RootId)));
    WidgetSchemaArguments
        ->GetObjectField(TEXT("object"))
        ->SetArrayField(
            TEXT("with"),
            {MakeShared<FJsonValueString>(TEXT("schema"))});
    const TSharedPtr<FJsonObject> WidgetSchema =
        FSalModule::BuildQueryResult(WidgetSchemaArguments);
    TestFalse(
        TEXT("Widget exact schema Query dispatches successfully"),
        PublicPathHasError(WidgetSchema));
    TestTrue(
        TEXT("Widget event schema returns an explicit Graph handoff"),
        PublicPathHasHandoff(
            WidgetSchema,
            TEXT("graph_event"),
            TEXT("graph"),
            WidgetFixture.EventGraph->GraphGuid.ToString(
                EGuidFormats::DigitsWithHyphensLower)));

    const TSharedPtr<FJsonObject> StateTree =
        FSalModule::BuildQueryResult(
            PublicPathQueryArguments(
                TEXT("tree_scope"),
                PublicPathStateTreeTarget(
                    StateTreeFixture.Asset->GetPathName(),
                    StateTreeFixture.Asset->GetClass()->GetPathName()),
                TEXT("summary")));
    TestFalse(
        TEXT("Composed StateTree Asset summary routes to StateTree"),
        PublicPathHasError(StateTree));
    TestTrue(
        TEXT("StateTree Query reports authored State counts"),
        PublicPathHasComment(
            StateTree,
            TEXT("states: 1")));
    TestTrue(
        TEXT("StateTree owner fields survive promotion of its alias into the Result Target table"),
        PublicPathHasTargetMember(
            StateTree,
            TEXT("tree_scope"),
            TEXT("loaded")));

    const TSharedPtr<FJsonObject> WidgetAsBlueprint =
        FSalModule::BuildQueryResult(
            PublicPathQueryArguments(
                TEXT("widget_blueprint"),
                PublicPathBlueprintCall(
                    WidgetFixture.Blueprint->GetPathName(),
                    WidgetFixture.Blueprint->GetBlueprintGuid()),
                TEXT("summary")));
    TestFalse(
        TEXT("Widget native asset remains queryable through explicit Blueprint Domain"),
        PublicPathHasError(WidgetAsBlueprint));
    TestFalse(
        TEXT("Blueprint Domain does not implicitly compose Widget summary"),
        PublicPathHasComment(WidgetAsBlueprint, TEXT("widgets:")));

    const TSharedPtr<FJsonObject> StateTreeAsAsset =
        FSalModule::BuildQueryResult(
            PublicPathQueryArguments(
                TEXT("tree_asset"),
                PublicPathAssetCall(
                    StateTreeFixture.Asset->GetPathName(),
                    StateTreeFixture.Asset->GetClass()->GetPathName()),
                TEXT("summary")));
    TestTrue(
        TEXT("StateTree native asset follows Asset Domain capability surface"),
        PublicPathHasDiagnosticCode(
            StateTreeAsAsset,
            TEXT("capability.unsupported_query")));
    TestFalse(
        TEXT("Asset Domain does not implicitly compose StateTree summary"),
        PublicPathHasComment(StateTreeAsAsset, TEXT("states: 1")));

    TSharedRef<FJsonObject> References =
        PublicPathOperation(TEXT("references"));
    References->SetObjectField(
        TEXT("target"),
        PublicPathStableReference(
            TEXT("variable"),
            BlueprintFixture.VariableId));
    const TSharedPtr<FJsonObject> ReferenceResult =
        FSalModule::BuildQueryResult(
            PublicPathQueryArguments(
                TEXT("blueprint_scope"),
                PublicPathBlueprintCall(
                    BlueprintFixture.Blueprint->GetPathName(),
                    BlueprintFixture.Blueprint->GetBlueprintGuid()),
                References));
    TestFalse(
        TEXT("references bypasses the owner interface and routes factually"),
        PublicPathHasError(ReferenceResult));

    const TArray<TSharedPtr<FJsonObject>> Results = {
        AssetRoot,
        ExactAsset,
        Blueprint,
        BlueprintGraph,
        BlueprintDispatcher,
        Graph,
        GraphCall,
        Widget,
        WidgetSchema,
        StateTree,
        WidgetAsBlueprint,
        StateTreeAsAsset,
        ReferenceResult};
    for (int32 Index = 0; Index < Results.Num(); ++Index)
    {
        FString ValidationError;
        const bool bValid =
            IsValidPublicPathOutgoingResult(
                Results[Index],
                ValidationError);
        TestTrue(
            *FString::Printf(
                TEXT("Routing result %d satisfies the outgoing SAL contract: %s"),
                Index,
                *ValidationError),
            bValid);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FSalModuleWidgetEventPaletteStableReferencePublicPathTest,
    "Loomle.Sal.PublicPath.Query.WidgetEventPaletteStableReference",
    EAutomationTestFlags::EditorContext
        | EAutomationTestFlags::EngineFilter)

bool FSalModuleWidgetEventPaletteStableReferencePublicPathTest::RunTest(
    const FString& Parameters)
{
    if (GEditor == nullptr
        || GEditor->IsPlaySessionInProgress()
        || GEditor->IsTransactionActive())
    {
        AddError(
            TEXT("Widget event Palette regression requires an idle Editor outside PIE and transactions."));
        return false;
    }

    FPublicPathWidgetFixture Fixture;
    if (!TestTrue(
            TEXT("Widget event Palette fixture is valid"),
            Fixture.IsValid()))
    {
        return false;
    }

    TSharedRef<FJsonObject> Operation =
        PublicPathOperation(TEXT("palette_entries"));
    Operation->SetStringField(TEXT("text"), TEXT("OnClicked"));
    TSharedRef<FJsonObject> Where = MakeShared<FJsonObject>();
    Where->SetStringField(TEXT("kind"), TEXT("eq"));
    TSharedRef<FJsonObject> WidgetField = MakeShared<FJsonObject>();
    WidgetField->SetArrayField(
        TEXT("path"),
        {MakeShared<FJsonValueString>(TEXT("widget"))});
    Where->SetObjectField(TEXT("field"), WidgetField);
    Where->SetObjectField(
        TEXT("value"),
        PublicPathStableReference(
            FString(),
            Fixture.EventButtonId));
    TSharedRef<FJsonObject> QueryArguments =
        PublicPathQueryArguments(
            TEXT("event_graph"),
            PublicPathGraphCall(
                Fixture.Blueprint->GetPathName(),
                Fixture.Blueprint->GetBlueprintGuid(),
                Fixture.EventGraph),
            Operation);
    QueryArguments->GetObjectField(TEXT("object"))
        ->SetObjectField(TEXT("where"), Where);

    const TSharedPtr<FJsonObject> Palette =
        FSalModule::BuildQueryResult(QueryArguments);
    TestFalse(
        *FString::Printf(
            TEXT("Graph accepts the owning WidgetBlueprint WidgetGuid as Palette context: %s"),
            *PublicPathDiagnosticSummary(Palette)),
        PublicPathHasError(Palette));
    const FString EventPaletteId =
        PublicPathPaletteIdForType(
            Palette,
            TEXT("/Script/BlueprintGraph.K2Node_ComponentBoundEvent"));
    TestFalse(
        *FString::Printf(
            TEXT("Contextual OnClicked discovery returns a ComponentBoundEvent Palette entry: %s"),
            *PublicPathPaletteEntriesSummary(Palette)),
        EventPaletteId.IsEmpty());
    TestTrue(
        TEXT("OnClicked Palette identity preserves its Widget context descriptor"),
        EventPaletteId.Contains(
            TEXT(";widget.")
            + Fixture.EventButtonId.ToString(
                EGuidFormats::DigitsWithHyphensLower)));
    if (EventPaletteId.IsEmpty())
    {
        return false;
    }

    TSharedRef<FJsonObject> CreationFields =
        MakeShared<FJsonObject>();
    CreationFields->SetStringField(
        TEXT("palette"),
        EventPaletteId);
    const TSharedRef<FJsonObject> CreationTarget =
        PublicPathLocalReference(TEXT("button_clicked"));
    const int32 OriginalNodeCount =
        Fixture.EventGraph->Nodes.Num();
    const bool bDirtyBefore = Fixture.Package->IsDirty();
    const TSharedPtr<FJsonObject> DryRun =
        FSalModule::BuildPatchResult(
            PublicPathPatchArguments(
                TEXT("event_graph"),
                PublicPathGraphCall(
                    Fixture.Blueprint->GetPathName(),
                    Fixture.Blueprint->GetBlueprintGuid(),
                    Fixture.EventGraph),
                {
                    PublicPathStatement(
                        PublicPathCreationBinding(
                            CreationTarget,
                            CreationFields)),
                    PublicPathStatement(
                        PublicPathAddStatement(
                            CreationTarget))
                }));
    TestTrue(
        *FString::Printf(
            TEXT("Schema-advertised OnClicked Palette entry supports creation-only dry run: %s"),
            *PublicPathDiagnosticSummary(DryRun)),
        PublicPathResultBool(DryRun, TEXT("valid"))
            && PublicPathResultBool(DryRun, TEXT("dryRun"))
            && !PublicPathResultBool(
                DryRun,
                TEXT("applied"),
                true));
    TestEqual(
        TEXT("OnClicked dry run preserves the live EventGraph"),
        Fixture.EventGraph->Nodes.Num(),
        OriginalNodeCount);
    TestEqual(
        TEXT("OnClicked dry run preserves Package dirty state"),
        Fixture.Package->IsDirty(),
        bDirtyBefore);

    FString PaletteValidation;
    FString DryRunValidation;
    TestTrue(
        *FString::Printf(
            TEXT("Widget event Palette result satisfies the outgoing contract: %s"),
            *PaletteValidation),
        IsValidPublicPathOutgoingResult(
            Palette,
            PaletteValidation));
    TestTrue(
        *FString::Printf(
            TEXT("Widget event dry-run result satisfies the outgoing contract: %s"),
            *DryRunValidation),
        IsValidPublicPathOutgoingResult(
            DryRun,
            DryRunValidation));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FSalModulePatchRoutingMatrixTest,
    "Loomle.Sal.PublicPath.Patch.NormalizedDryRunRoutingMatrix",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSalModulePatchRoutingMatrixTest::RunTest(
    const FString& Parameters)
{
    if (GEditor == nullptr
        || GEditor->IsPlaySessionInProgress())
    {
        AddError(
            TEXT("Public Patch routing requires an idle Editor outside PIE."));
        return false;
    }

    FPublicPathClassFixture BlueprintFixture;
    FPublicPathWidgetFixture WidgetFixture;
    FPublicPathStateTreeFixture StateTreeFixture;
    if (BlueprintFixture.Blueprint == nullptr
        || BlueprintFixture.Graph == nullptr
        || BlueprintFixture.Node == nullptr
        || !BlueprintFixture.Node->NodeGuid.IsValid()
        || !WidgetFixture.IsValid()
        || !StateTreeFixture.IsValid())
    {
        AddError(TEXT("Public Patch routing fixtures are incomplete."));
        return false;
    }

    const FString DescriptionBefore =
        BlueprintFixture.Blueprint->BlueprintDescription;
    const FIntPoint NodeBefore(
        BlueprintFixture.Node->NodePosX,
        BlueprintFixture.Node->NodePosY);
    const bool bBlueprintPackageDirtyBefore =
        BlueprintFixture.Blueprint->GetOutermost()->IsDirty();
    const bool bWidgetVariableBefore =
        WidgetFixture.Root->bIsVariable;

    const TSharedPtr<FJsonObject> Asset =
        FSalModule::BuildPatchResult(
            PublicPathPatchArguments(
                TEXT("asset_scope"),
                PublicPathAssetCall(
                    BlueprintFixture.Blueprint->GetPathName(),
                    BlueprintFixture.Blueprint->GetClass()->GetPathName()),
                {PublicPathStatement(
                    PublicPathSaveStatement())}));

    const TSharedPtr<FJsonObject> Blueprint =
        FSalModule::BuildPatchResult(
            PublicPathPatchArguments(
                TEXT("blueprint_scope"),
                PublicPathBlueprintCall(
                    BlueprintFixture.Blueprint->GetPathName(),
                    BlueprintFixture.Blueprint->GetBlueprintGuid()),
                {PublicPathStatement(
                    PublicPathSetMemberStatement(
                        PublicPathLocalReference(TEXT("blueprint_scope")),
                        TEXT("BlueprintDescription"),
                        MakeShared<FJsonValueString>(
                            TEXT("dry-run description"))))}));

    TSharedRef<FJsonObject> Move = MakeShared<FJsonObject>();
    Move->SetStringField(TEXT("kind"), TEXT("move"));
    Move->SetObjectField(
        TEXT("target"),
        PublicPathStableReference(
            TEXT("node"),
            BlueprintFixture.Node->NodeGuid));
    Move->SetArrayField(
        TEXT("to"),
        {
            MakeShared<FJsonValueNumber>(NodeBefore.X + 17),
            MakeShared<FJsonValueNumber>(NodeBefore.Y + 23)
        });
    const TSharedPtr<FJsonObject> Graph =
        FSalModule::BuildPatchResult(
            PublicPathPatchArguments(
                TEXT("graph_scope"),
                PublicPathGraphCall(
                    BlueprintFixture.Blueprint->GetPathName(),
                    BlueprintFixture.Blueprint->GetBlueprintGuid(),
                    BlueprintFixture.Graph),
                {PublicPathStatement(Move)}));

    const FIntPoint NodeBeforeRejectedMove(
        BlueprintFixture.Node->NodePosX,
        BlueprintFixture.Node->NodePosY);
    const bool bPackageDirtyBeforeRejectedMove =
        BlueprintFixture.Blueprint->GetOutermost()->IsDirty();
    TSharedRef<FJsonObject> MoveBy = MakeShared<FJsonObject>();
    MoveBy->SetStringField(TEXT("kind"), TEXT("move"));
    MoveBy->SetObjectField(
        TEXT("target"),
        PublicPathStableReference(
            TEXT("node"),
            BlueprintFixture.Node->NodeGuid));
    MoveBy->SetArrayField(
        TEXT("by"),
        {
            MakeShared<FJsonValueNumber>(17),
            MakeShared<FJsonValueNumber>(23)
        });
    const TSharedPtr<FJsonObject> GraphMoveBy =
        FSalModule::BuildPatchResult(
            PublicPathPatchArguments(
                TEXT("graph_scope"),
                PublicPathGraphCall(
                    BlueprintFixture.Blueprint->GetPathName(),
                    BlueprintFixture.Blueprint->GetBlueprintGuid(),
                    BlueprintFixture.Graph),
                {PublicPathStatement(MoveBy)}));
    TestTrue(
        TEXT("Rejected relative Graph move preserves native Node layout"),
        BlueprintFixture.Node->NodePosX
                == NodeBeforeRejectedMove.X
            && BlueprintFixture.Node->NodePosY
                == NodeBeforeRejectedMove.Y);
    TestEqual(
        TEXT("Rejected relative Graph move preserves immediate dirty state"),
        BlueprintFixture.Blueprint->GetOutermost()->IsDirty(),
        bPackageDirtyBeforeRejectedMove);

    const TSharedPtr<FJsonObject> Widget =
        FSalModule::BuildPatchResult(
            PublicPathPatchArguments(
                TEXT("widget_blueprint"),
                PublicPathWidgetTarget(
                    WidgetFixture.Blueprint->GetPathName(),
                    WidgetFixture.Blueprint->GetBlueprintGuid()),
                {PublicPathStatement(
                    PublicPathSetMemberStatement(
                        PublicPathStableReference(
                            FString(),
                            WidgetFixture.RootId),
                        TEXT("bIsVariable"),
                        MakeShared<FJsonValueBoolean>(
                            !bWidgetVariableBefore)))}));

    const TSharedPtr<FJsonObject> StateTree =
        FSalModule::BuildPatchResult(
            PublicPathPatchArguments(
                TEXT("tree_scope"),
                PublicPathStateTreeTarget(
                    StateTreeFixture.Asset->GetPathName(),
                    StateTreeFixture.Asset->GetClass()->GetPathName()),
                {PublicPathStatement(
                    PublicPathSaveStatement())}));

    TSharedRef<FJsonObject> BlueprintCreationFields =
        MakeShared<FJsonObject>();
    BlueprintCreationFields->SetStringField(
        TEXT("palette"),
        TEXT("blueprint.variable"));
    BlueprintCreationFields->SetStringField(
        TEXT("type"),
        PublicPathPinTypeText(
            UEdGraphSchema_K2::PC_Boolean));
    const TSharedRef<FJsonObject> BlueprintCreationTarget =
        PublicPathMemberReference(
            TEXT("blueprint_scope"),
            TEXT("TagErasedValue"));
    const TSharedPtr<FJsonObject> BlueprintCreation =
        FSalModule::BuildPatchResult(
            PublicPathPatchArguments(
                TEXT("blueprint_scope"),
                PublicPathBlueprintCall(
                    BlueprintFixture.Blueprint->GetPathName(),
                    BlueprintFixture.Blueprint->GetBlueprintGuid()),
                {
                    PublicPathStatement(
                        PublicPathCreationBinding(
                            BlueprintCreationTarget,
                            BlueprintCreationFields)),
                    PublicPathStatement(
                        PublicPathAddStatement(
                            BlueprintCreationTarget))
                }));

    TSharedRef<FJsonObject> GraphPaletteOperation =
        PublicPathOperation(TEXT("palette_entries"));
    GraphPaletteOperation->SetStringField(
        TEXT("text"),
        TEXT("Branch"));
    const TSharedPtr<FJsonObject> GraphPalette =
        FSalModule::BuildQueryResult(
            PublicPathQueryArguments(
                TEXT("graph_scope"),
                PublicPathGraphCall(
                    BlueprintFixture.Blueprint->GetPathName(),
                    BlueprintFixture.Blueprint->GetBlueprintGuid(),
                    BlueprintFixture.Graph),
                GraphPaletteOperation));
    const FString GraphPaletteId =
        PublicPathFirstPaletteId(GraphPalette);
    TestFalse(
        TEXT("Graph creation regression discovers a current Palette entry"),
        GraphPaletteId.IsEmpty());
    if (GraphPaletteId.IsEmpty())
    {
        return false;
    }
    TSharedRef<FJsonObject> GraphCreationFields =
        MakeShared<FJsonObject>();
    GraphCreationFields->SetStringField(
        TEXT("palette"),
        GraphPaletteId);
    const TSharedRef<FJsonObject> GraphCreationTarget =
        PublicPathLocalReference(TEXT("tag_erased_node"));
    const TSharedPtr<FJsonObject> GraphCreation =
        FSalModule::BuildPatchResult(
            PublicPathPatchArguments(
                TEXT("graph_scope"),
                PublicPathGraphCall(
                    BlueprintFixture.Blueprint->GetPathName(),
                    BlueprintFixture.Blueprint->GetBlueprintGuid(),
                    BlueprintFixture.Graph),
                {
                    PublicPathStatement(
                        PublicPathCreationBinding(
                            GraphCreationTarget,
                            GraphCreationFields)),
                    PublicPathStatement(
                        PublicPathAddStatement(
                            GraphCreationTarget))
                }));

    TSharedRef<FJsonObject> NestedLegacyCallArgs =
        MakeShared<FJsonObject>();
    NestedLegacyCallArgs->SetStringField(
        TEXT("palette"),
        GraphPaletteId);
    TSharedRef<FJsonObject> WrappedLegacyCallFields =
        MakeShared<FJsonObject>();
    WrappedLegacyCallFields->SetStringField(
        TEXT("kind"),
        TEXT("call"));
    WrappedLegacyCallFields->SetStringField(
        TEXT("callee"),
        TEXT("node"));
    WrappedLegacyCallFields->SetObjectField(
        TEXT("args"),
        PublicPathObjectExpression(NestedLegacyCallArgs));
    const TSharedPtr<FJsonObject> WrappedLegacyCall =
        FSalModule::BuildPatchResult(
            PublicPathPatchArguments(
                TEXT("graph_scope"),
                PublicPathGraphCall(
                    BlueprintFixture.Blueprint->GetPathName(),
                    BlueprintFixture.Blueprint->GetBlueprintGuid(),
                    BlueprintFixture.Graph),
                {PublicPathStatement(
                    PublicPathCreationBinding(
                        PublicPathLocalReference(
                            TEXT("wrapped_call_data")),
                        WrappedLegacyCallFields))}));

    TSharedRef<FJsonObject> WrappedLegacyRefFields =
        MakeShared<FJsonObject>();
    WrappedLegacyRefFields->SetStringField(
        TEXT("kind"),
        TEXT("node"));
    WrappedLegacyRefFields->SetStringField(
        TEXT("id"),
        BlueprintFixture.Node->NodeGuid.ToString(
            EGuidFormats::DigitsWithHyphensLower));
    const TSharedPtr<FJsonObject> WrappedLegacyRef =
        FSalModule::BuildPatchResult(
            PublicPathPatchArguments(
                TEXT("graph_scope"),
                PublicPathGraphCall(
                    BlueprintFixture.Blueprint->GetPathName(),
                    BlueprintFixture.Blueprint->GetBlueprintGuid(),
                    BlueprintFixture.Graph),
                {PublicPathStatement(
                    PublicPathCreationBinding(
                        PublicPathLocalReference(
                            TEXT("wrapped_reference_data")),
                        WrappedLegacyRefFields))}));

    TSharedRef<FJsonObject> WidgetCreationFields =
        MakeShared<FJsonObject>();
    WidgetCreationFields->SetStringField(
        TEXT("palette"),
        FString(TEXT("widget.class:"))
            + UButton::StaticClass()->GetPathName());
    const TSharedRef<FJsonObject> WidgetCreationTarget =
        PublicPathLocalReference(TEXT("tag_erased_widget"));
    const TSharedPtr<FJsonObject> WidgetCreation =
        FSalModule::BuildPatchResult(
            PublicPathPatchArguments(
                TEXT("widget_blueprint"),
                PublicPathWidgetTarget(
                    WidgetFixture.Blueprint->GetPathName(),
                    WidgetFixture.Blueprint->GetBlueprintGuid()),
                {
                    PublicPathStatement(
                        PublicPathCreationBinding(
                            WidgetCreationTarget,
                            WidgetCreationFields)),
                    PublicPathStatement(
                        PublicPathAddStatement(
                            WidgetCreationTarget,
                            PublicPathStableReference(
                                FString(),
                                WidgetFixture.RootId)))
                }));

    TSharedRef<FJsonObject> StateTreeCreationFields =
        MakeShared<FJsonObject>();
    StateTreeCreationFields->SetStringField(
        TEXT("palette"),
        TEXT("state_tree.parameter"));
    StateTreeCreationFields->SetStringField(
        TEXT("Name"),
        TEXT("TagErasedCount"));
    StateTreeCreationFields->SetStringField(
        TEXT("type"),
        TEXT("IntProperty"));
    const TSharedRef<FJsonObject> StateTreeCreationTarget =
        PublicPathLocalReference(TEXT("tag_erased_parameter"));
    const TSharedPtr<FJsonObject> StateTreeCreation =
        FSalModule::BuildPatchResult(
            PublicPathPatchArguments(
                TEXT("tree_scope"),
                PublicPathStateTreeTarget(
                    StateTreeFixture.Asset->GetPathName(),
                    StateTreeFixture.Asset->GetClass()->GetPathName()),
                {
                    PublicPathStatement(
                        PublicPathCreationBinding(
                            StateTreeCreationTarget,
                            StateTreeCreationFields)),
                    PublicPathStatement(
                        PublicPathAddStatement(
                            StateTreeCreationTarget,
                            PublicPathMemberReference(
                                TEXT("tree_scope"),
                                TEXT("RootParameters"))))
                }));

    const TArray<TPair<FString, TSharedPtr<FJsonObject>>> Results = {
        {TEXT("Asset"), Asset},
        {TEXT("Blueprint"), Blueprint},
        {TEXT("Graph"), Graph},
        {TEXT("Widget"), Widget},
        {TEXT("StateTree"), StateTree},
        {TEXT("Blueprint tag-erased creation"), BlueprintCreation},
        {TEXT("Graph tag-erased creation"), GraphCreation},
        {TEXT("Widget tag-erased creation"), WidgetCreation},
        {TEXT("StateTree tag-erased creation"), StateTreeCreation}};
    for (const TPair<FString, TSharedPtr<FJsonObject>>& Entry : Results)
    {
        FString ValidationError;
        const bool bOutgoingValid =
            IsValidPublicPathOutgoingResult(
                Entry.Value,
                ValidationError);
        const FString Diagnostics =
            PublicPathDiagnosticSummary(Entry.Value);
        TestTrue(
            *FString::Printf(
                TEXT("%s Patch result satisfies outgoing SAL: %s"),
                *Entry.Key,
                *ValidationError),
            bOutgoingValid);
        TestFalse(
            *FString::Printf(
                TEXT("%s normalized Patch reaches its interface; diagnostics: %s"),
                *Entry.Key,
                *Diagnostics),
            PublicPathResultBool(
                Entry.Value,
                TEXT("isError"),
                true));
        TestTrue(
            *FString::Printf(
                TEXT("%s normalized Patch validates; diagnostics: %s"),
                *Entry.Key,
                *Diagnostics),
            PublicPathResultBool(
                Entry.Value,
                TEXT("valid")));
        TestTrue(
            *FString::Printf(
                TEXT("%s normalized Patch remains a dry-run"),
                *Entry.Key),
            PublicPathResultBool(
                Entry.Value,
                TEXT("dryRun")));
        TestFalse(
            *FString::Printf(
                TEXT("%s normalized Patch does not apply"),
                *Entry.Key),
            PublicPathResultBool(
                Entry.Value,
                TEXT("applied"),
                true));
    }

    {
        FString ValidationError;
        const bool bOutgoingValid =
            IsValidPublicPathOutgoingResult(
                GraphMoveBy,
                ValidationError);
        TestTrue(
            *FString::Printf(
                TEXT("Relative Graph move rejection satisfies outgoing SAL: %s"),
                *ValidationError),
            bOutgoingValid);
        TestTrue(
            TEXT("Normalized public Patch reaches Graph before rejecting by"),
            PublicPathResultBool(
                GraphMoveBy,
                TEXT("isError")));
        TestFalse(
            TEXT("Relative Graph move does not validate"),
            PublicPathResultBool(
                GraphMoveBy,
                TEXT("valid"),
                true));
        TestTrue(
            TEXT("Relative Graph move rejection retains dry-run semantics"),
            PublicPathResultBool(
                GraphMoveBy,
                TEXT("dryRun")));
        TestFalse(
            TEXT("Relative Graph move rejection does not apply"),
            PublicPathResultBool(
                GraphMoveBy,
                TEXT("applied"),
                true));
        TestTrue(
            TEXT("Relative Graph move reports the unavailable clause"),
            PublicPathHasDiagnosticCode(
                GraphMoveBy,
                TEXT("capability.clause_unavailable")));
        TestTrue(
            TEXT("Relative Graph move suggests querying exact layout"),
            PublicPathHasDiagnosticSuggestion(
                GraphMoveBy,
                TEXT("capability.clause_unavailable"),
                TEXT("with layout")));
        TestTrue(
            TEXT("Relative Graph move suggests retrying with absolute to"),
            PublicPathHasDiagnosticSuggestion(
                GraphMoveBy,
                TEXT("capability.clause_unavailable"),
                TEXT("to (x, y)")));
        FString TargetContext;
        TestTrue(
            TEXT("Relative Graph move rejection retains exact Target context"),
            GraphMoveBy.IsValid()
                && GraphMoveBy->TryGetStringField(
                    TEXT("targetContext"),
                    TargetContext)
                && TargetContext == TEXT("exact_target"));
    }

    for (const TPair<FString, TSharedPtr<FJsonObject>>& Entry : {
             TPair<FString, TSharedPtr<FJsonObject>>(
                 TEXT("wrapped call-like data"),
                 WrappedLegacyCall),
             TPair<FString, TSharedPtr<FJsonObject>>(
                 TEXT("wrapped kind/id data"),
                 WrappedLegacyRef)})
    {
        FString ValidationError;
        const bool bOutgoingValid =
            IsValidPublicPathOutgoingResult(
                Entry.Value,
                ValidationError);
        TestTrue(
            *FString::Printf(
                TEXT("%s rejection remains a valid contextual result: %s"),
                *Entry.Key,
                *ValidationError),
            bOutgoingValid);
        TestTrue(
            *FString::Printf(
                TEXT("%s cannot tunnel into the private executor AST"),
                *Entry.Key),
            PublicPathHasDiagnosticCode(
                Entry.Value,
                TEXT("capability.unsupported_constructor")));
        TestFalse(
            *FString::Printf(
                TEXT("%s is not accepted as a creation definition"),
                *Entry.Key),
            PublicPathResultBool(
                Entry.Value,
                TEXT("valid"),
                true));
    }

    TestEqual(
        TEXT("Blueprint dry-run preserves its authored field"),
        BlueprintFixture.Blueprint->BlueprintDescription,
        DescriptionBefore);
    TestTrue(
        TEXT("Graph dry-run preserves native node layout"),
        BlueprintFixture.Node->NodePosX == NodeBefore.X
            && BlueprintFixture.Node->NodePosY == NodeBefore.Y);
    TestEqual(
        TEXT("Rejected relative Graph move preserves package dirty state"),
        BlueprintFixture.Blueprint->GetOutermost()->IsDirty(),
        bBlueprintPackageDirtyBefore);
    TestEqual(
        TEXT("Widget dry-run preserves native Widget state"),
        WidgetFixture.Root->bIsVariable,
        bWidgetVariableBefore);
    TestTrue(
        TEXT("Graph mutation returns an explicit Blueprint compile handoff"),
        PublicPathHasHandoff(
            Graph,
            TEXT("compile"),
            TEXT("blueprint"),
            BlueprintFixture.Blueprint->GetBlueprintGuid().ToString(
                EGuidFormats::DigitsWithHyphensLower)));
    TestTrue(
        TEXT("Widget mutation returns an explicit Blueprint compile handoff"),
        PublicPathHasHandoff(
            Widget,
            TEXT("compile"),
            TEXT("blueprint"),
            WidgetFixture.Blueprint->GetBlueprintGuid().ToString(
                EGuidFormats::DigitsWithHyphensLower)));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FSalModuleGraphStableReferencePublicPathTest,
    "Loomle.Sal.PublicPath.Query.GraphStableReferenceOwnerScope",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSalModuleGraphStableReferencePublicPathTest::RunTest(
    const FString& Parameters)
{
    FPublicPathClassFixture Fixture;
    if (Fixture.Blueprint == nullptr
        || Fixture.Graph == nullptr
        || Fixture.Node == nullptr
        || Fixture.Node->Pins.IsEmpty()
        || Fixture.Node->Pins[0] == nullptr)
    {
        AddError(TEXT("Graph StableRef fixture has no exact Node and Pin."));
        return false;
    }
    UEdGraphPin* Pin = Fixture.Node->Pins[0];
    const TSharedRef<FJsonObject> GraphTarget =
        PublicPathGraphCall(
            Fixture.Blueprint->GetPathName(),
            Fixture.Blueprint->GetBlueprintGuid(),
            Fixture.Graph);
    const TSharedPtr<FJsonObject> Tagged =
        FSalModule::BuildQueryResult(
            PublicPathQueryArguments(
                TEXT("graph_scope"),
                GraphTarget,
                PublicPathExactObjectOperation(
                    PublicPathStableReference(
                        TEXT("pin"),
                        Fixture.Node->NodeGuid,
                        Pin->PinId))));
    const TSharedPtr<FJsonObject> TagFree =
        FSalModule::BuildQueryResult(
            PublicPathQueryArguments(
                TEXT("graph_scope"),
                GraphTarget,
                PublicPathExactObjectOperation(
                    PublicPathStableReference(
                        FString(),
                        Fixture.Node->NodeGuid,
                        Pin->PinId))));
    TestFalse(TEXT("Tagged NodeGuid/PinId resolves"), PublicPathHasError(Tagged));
    TestFalse(TEXT("Tag-free NodeGuid/PinId resolves"), PublicPathHasError(TagFree));

    FString TaggedValidation;
    FString TagFreeValidation;
    TestTrue(
        *FString::Printf(TEXT("Tagged result validates: %s"), *TaggedValidation),
        IsValidPublicPathOutgoingResult(Tagged, TaggedValidation));
    TestTrue(
        *FString::Printf(TEXT("Tag-free result validates: %s"), *TagFreeValidation),
        IsValidPublicPathOutgoingResult(TagFree, TagFreeValidation));

    const TSharedPtr<FJsonObject> OwnerVariable =
        FSalModule::BuildQueryResult(
            PublicPathQueryArguments(
                TEXT("graph_scope"),
                GraphTarget,
                PublicPathExactObjectOperation(
                    PublicPathStableReference(
                        TEXT("variable"),
                        Fixture.VariableId))));
    TestFalse(
        TEXT("Graph Domain reads an explicitly declared owning Blueprint Variable"),
        PublicPathHasError(OwnerVariable));
    TestTrue(
        TEXT("Owning Blueprint Variable keeps its Target-relative identity"),
        PublicPathHasCallId(
            OwnerVariable,
            TEXT("variable"),
            Fixture.VariableId.ToString(
                EGuidFormats::DigitsWithHyphensLower)));

    if (Fixture.FunctionGraph == nullptr
        || !Fixture.LocalVariableId.IsValid())
    {
        AddError(TEXT("StableRef fixture has no Function-local Variable."));
        return false;
    }
    const FString LocalVariableIdentity =
        Fixture.FunctionGraph->GraphGuid.ToString(
            EGuidFormats::DigitsWithHyphensLower)
        + TEXT("/")
        + Fixture.LocalVariableId.ToString(
            EGuidFormats::DigitsWithHyphensLower);
    const TSharedPtr<FJsonObject> BlueprintLocalVariable =
        FSalModule::BuildQueryResult(
            PublicPathQueryArguments(
                TEXT("blueprint_scope"),
                PublicPathBlueprintCall(
                    Fixture.Blueprint->GetPathName(),
                    Fixture.Blueprint->GetBlueprintGuid()),
                PublicPathExactObjectOperation(
                    PublicPathStableReference(
                        TEXT("variable"),
                        Fixture.FunctionGraph->GraphGuid,
                        Fixture.LocalVariableId))));
    TestFalse(
        TEXT("Blueprint Domain resolves GraphGuid/VarGuid"),
        PublicPathHasError(BlueprintLocalVariable));
    TestTrue(
        TEXT("Blueprint Function-local Variable preserves its composite identity"),
        PublicPathHasCallId(
            BlueprintLocalVariable,
            TEXT("variable"),
            LocalVariableIdentity));

    const TSharedPtr<FJsonObject> GraphLocalVariable =
        FSalModule::BuildQueryResult(
            PublicPathQueryArguments(
                TEXT("function_graph"),
                PublicPathGraphCall(
                    Fixture.Blueprint->GetPathName(),
                    Fixture.Blueprint->GetBlueprintGuid(),
                    Fixture.FunctionGraph),
                PublicPathExactObjectOperation(
                    PublicPathStableReference(
                        FString(),
                        Fixture.LocalVariableId))));
    TestFalse(
        TEXT("Top-level Function Graph resolves its local Variable by one segment"),
        PublicPathHasError(GraphLocalVariable));
    TestTrue(
        TEXT("Graph-local Variable output retains GraphGuid/VarGuid"),
        PublicPathHasCallId(
            GraphLocalVariable,
            TEXT("variable"),
            LocalVariableIdentity));

    const TSharedPtr<FJsonObject> WrongOwner =
        FSalModule::BuildQueryResult(
            PublicPathQueryArguments(
                TEXT("graph_scope"),
                GraphTarget,
                PublicPathExactObjectOperation(
                    PublicPathStableReference(
                        FString(),
                        FGuid::NewGuid(),
                        Pin->PinId))));
    TestTrue(
        TEXT("PinId under the wrong NodeGuid does not fall back to global PinId lookup"),
        PublicPathHasDiagnosticCode(
            WrongOwner,
            TEXT("resolution.object_not_found")));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FSalModuleTargetSelfReferencePublicPathTest,
    "Loomle.Sal.PublicPath.Query.TargetSelfReferences",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSalModuleTargetSelfReferencePublicPathTest::RunTest(
    const FString& Parameters)
{
    FPublicPathClassFixture BlueprintFixture;
    FPublicPathWidgetFixture WidgetFixture;
    FPublicPathStateTreeFixture StateTreeFixture;
    if (BlueprintFixture.Blueprint == nullptr
        || BlueprintFixture.Class == nullptr
        || BlueprintFixture.Graph == nullptr
        || BlueprintFixture.FunctionGraph == nullptr
        || !WidgetFixture.IsValid()
        || !StateTreeFixture.IsValid())
    {
        AddError(TEXT("TargetSelf reference fixture is incomplete."));
        return false;
    }

    const TSharedPtr<FJsonObject> CallableGraph =
        FSalModule::BuildQueryResult(
            PublicPathQueryArguments(
                TEXT("function_graph"),
                PublicPathGraphCall(
                    BlueprintFixture.Blueprint->GetPathName(),
                    BlueprintFixture.Blueprint->GetBlueprintGuid(),
                    BlueprintFixture.FunctionGraph),
                PublicPathReferencesOperation(
                    PublicPathTargetSelfReference())));
    TestFalse(
        TEXT("Function Graph TargetSelf resolves as its declaration without a fabricated StableRef"),
        PublicPathHasError(CallableGraph));

    TArray<TSharedPtr<FJsonObject>> Unsupported;
    Unsupported.Add(
        FSalModule::BuildQueryResult(
            PublicPathQueryArguments(
                TEXT("event_graph"),
                PublicPathGraphCall(
                    BlueprintFixture.Blueprint->GetPathName(),
                    BlueprintFixture.Blueprint->GetBlueprintGuid(),
                    BlueprintFixture.Graph),
                PublicPathReferencesOperation(
                    PublicPathTargetSelfReference()))));
    Unsupported.Add(
        FSalModule::BuildQueryResult(
            PublicPathQueryArguments(
                TEXT("function_graph"),
                PublicPathGraphCall(
                    BlueprintFixture.Blueprint->GetPathName(),
                    BlueprintFixture.Blueprint->GetBlueprintGuid(),
                    BlueprintFixture.FunctionGraph),
                PublicPathReferencesOperation(
                    PublicPathTargetSelfMemberReference(
                        TEXT("UnsupportedField"))))));
    Unsupported.Add(
        FSalModule::BuildQueryResult(
            PublicPathQueryArguments(
                TEXT("blueprint_scope"),
                PublicPathBlueprintCall(
                    BlueprintFixture.Blueprint->GetPathName(),
                    BlueprintFixture.Blueprint->GetBlueprintGuid()),
                PublicPathReferencesOperation(
                    PublicPathTargetSelfReference()))));
    Unsupported.Add(
        FSalModule::BuildQueryResult(
            PublicPathQueryArguments(
                TEXT("actor_class"),
                PublicPathClassCall(AActor::StaticClass()->GetPathName()),
                PublicPathReferencesOperation(
                    PublicPathTargetSelfReference()))));
    Unsupported.Add(
        FSalModule::BuildQueryResult(
            PublicPathQueryArguments(
                TEXT("asset_scope"),
                PublicPathAssetCall(
                    BlueprintFixture.Blueprint->GetPathName()),
                PublicPathReferencesOperation(
                    PublicPathTargetSelfReference()))));
    Unsupported.Add(
        FSalModule::BuildQueryResult(
            PublicPathQueryArguments(
                TEXT("tree_scope"),
                PublicPathStateTreeTarget(
                    StateTreeFixture.Asset->GetPathName(),
                    StateTreeFixture.Asset->GetClass()->GetPathName()),
                PublicPathReferencesOperation(
                    PublicPathTargetSelfReference()))));
    Unsupported.Add(
        FSalModule::BuildQueryResult(
            PublicPathQueryArguments(
                TEXT("widget_scope"),
                PublicPathWidgetTarget(
                    WidgetFixture.Blueprint->GetPathName(),
                    WidgetFixture.Blueprint->GetBlueprintGuid()),
                PublicPathReferencesOperation(
                    PublicPathTargetSelfReference()))));

    for (int32 Index = 0; Index < Unsupported.Num(); ++Index)
    {
        FString Context;
        FString ValidationError;
        TestTrue(
            *FString::Printf(
                TEXT("Unsupported TargetSelf role %d returns capability.reference_unavailable"),
                Index),
            PublicPathHasDiagnosticCode(
                Unsupported[Index],
                TEXT("capability.reference_unavailable")));
        TestTrue(
            *FString::Printf(
                TEXT("Unsupported TargetSelf role %d retains exact Target context"),
                Index),
            Unsupported[Index].IsValid()
                && Unsupported[Index]->TryGetStringField(
                    TEXT("targetContext"),
                    Context)
                && Context == TEXT("exact_target"));
        TestTrue(
            *FString::Printf(
                TEXT("Unsupported TargetSelf role %d remains a valid outgoing result: %s"),
                Index,
                *ValidationError),
            IsValidPublicPathOutgoingResult(
                Unsupported[Index],
                ValidationError));
    }

    FString CallableContext;
    FString CallableValidationError;
    TestTrue(
        TEXT("Callable Graph TargetSelf result retains exact Target context"),
        CallableGraph.IsValid()
            && CallableGraph->TryGetStringField(
                TEXT("targetContext"),
                CallableContext)
            && CallableContext == TEXT("exact_target"));
    TestTrue(
        *FString::Printf(
            TEXT("Callable Graph TargetSelf result satisfies outgoing validation: %s"),
            *CallableValidationError),
        IsValidPublicPathOutgoingResult(
            CallableGraph,
            CallableValidationError));

    TSharedRef<FJsonObject> MalformedTargetSelf =
        PublicPathTargetSelfReference();
    MalformedTargetSelf->SetStringField(
        TEXT("unexpected"),
        TEXT("field"));
    const TSharedPtr<FJsonObject> Malformed =
        FSalModule::BuildQueryResult(
            PublicPathQueryArguments(
                TEXT("function_graph"),
                PublicPathGraphCall(
                    BlueprintFixture.Blueprint->GetPathName(),
                    BlueprintFixture.Blueprint->GetBlueprintGuid(),
                    BlueprintFixture.FunctionGraph),
                PublicPathReferencesOperation(
                    MalformedTargetSelf)));
    TestTrue(
        TEXT("TargetSelf accepts no ad-hoc fields"),
        PublicPathHasDiagnosticCode(
            Malformed,
            TEXT("language.invalid_object_shape")));

    TSharedRef<FJsonObject> InvalidSet =
        PublicPathSetMemberStatement(
            PublicPathLocalReference(TEXT("actor_class")),
            TEXT("InitialLifeSpan"),
            MakeShared<FJsonValueObject>(
                PublicPathTargetSelfReference()));
    const TSharedPtr<FJsonObject> InvalidExpression =
        FSalModule::BuildPatchResult(
            PublicPathPatchArguments(
                TEXT("actor_class"),
                PublicPathClassCall(
                    BlueprintFixture.Class->GetPathName()),
                {PublicPathStatement(InvalidSet)}));
    TestTrue(
        TEXT("TargetSelf remains invalid as an ordinary expression"),
        PublicPathHasDiagnosticCode(
            InvalidExpression,
            TEXT("language.invalid_object_shape")));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FSalModuleLegacyNormalizedShapeRejectionTest,
    "Loomle.Sal.PublicPath.Query.LegacyShapesRejected",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSalModuleLegacyNormalizedShapeRejectionTest::RunTest(
    const FString& Parameters)
{
    TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
    Args->SetStringField(TEXT("path"), AActor::StaticClass()->GetPathName());
    TSharedRef<FJsonObject> LegacyCall = MakeShared<FJsonObject>();
    LegacyCall->SetStringField(TEXT("kind"), TEXT("call"));
    LegacyCall->SetStringField(TEXT("callee"), TEXT("class"));
    LegacyCall->SetObjectField(TEXT("args"), Args);
    TSharedRef<FJsonObject> LegacyBinding = MakeShared<FJsonObject>();
    LegacyBinding->SetStringField(TEXT("alias"), TEXT("actorClass"));
    LegacyBinding->SetObjectField(TEXT("value"), LegacyCall);
    TSharedRef<FJsonObject> Query = MakeShared<FJsonObject>();
    Query->SetStringField(TEXT("kind"), TEXT("query"));
    Query->SetObjectField(TEXT("target"), LegacyBinding);
    Query->SetObjectField(TEXT("operation"), PublicPathOperation(TEXT("summary")));
    TSharedRef<FJsonObject> Envelope = MakeShared<FJsonObject>();
    Envelope->SetObjectField(TEXT("object"), Query);
    const TSharedPtr<FJsonObject> LegacyTargetResult =
        FSalModule::BuildQueryResult(Envelope);
    TestTrue(
        TEXT("Legacy constructor Target is rejected at the Bridge boundary"),
        PublicPathHasDiagnosticCode(
            LegacyTargetResult,
            TEXT("language.invalid_object_shape")));

    FPublicPathClassFixture Fixture;
    if (Fixture.Blueprint == nullptr || Fixture.Graph == nullptr || Fixture.Node == nullptr)
    {
        AddError(TEXT("Legacy reference rejection fixture is incomplete."));
        return false;
    }
    TSharedRef<FJsonObject> LegacyExact = MakeShared<FJsonObject>();
    LegacyExact->SetStringField(TEXT("kind"), TEXT("node"));
    LegacyExact->SetStringField(
        TEXT("id"),
        Fixture.Node->NodeGuid.ToString(EGuidFormats::DigitsWithHyphensLower));
    const TSharedPtr<FJsonObject> LegacyRefResult =
        FSalModule::BuildQueryResult(
            PublicPathQueryArguments(
                TEXT("graph_scope"),
                PublicPathGraphCall(
                    Fixture.Blueprint->GetPathName(),
                    Fixture.Blueprint->GetBlueprintGuid(),
                    Fixture.Graph),
                LegacyExact));
    TestTrue(
        TEXT("Legacy kind@id selector is rejected at the Bridge boundary"),
        PublicPathHasDiagnosticCode(
            LegacyRefResult,
            TEXT("language.invalid_object_shape")));

    const TSharedPtr<FJsonObject> ReservedTagResult =
        FSalModule::BuildQueryResult(
            PublicPathQueryArguments(
                TEXT("graph_scope"),
                PublicPathGraphCall(
                    Fixture.Blueprint->GetPathName(),
                    Fixture.Blueprint->GetBlueprintGuid(),
                    Fixture.Graph),
                PublicPathExactObjectOperation(
                    PublicPathStableReference(
                        TEXT("palette"),
                        Fixture.Node->NodeGuid))));
    TestTrue(
        TEXT("Reserved Core/Domain word cannot be used as a semanticTag"),
        PublicPathHasDiagnosticCode(
            ReservedTagResult,
            TEXT("language.invalid_object_shape")));

    const TSharedPtr<FJsonObject> ReservedObjectTagResult =
        FSalModule::BuildQueryResult(
            PublicPathQueryArguments(
                TEXT("graph_scope"),
                PublicPathGraphCall(
                    Fixture.Blueprint->GetPathName(),
                    Fixture.Blueprint->GetBlueprintGuid(),
                    Fixture.Graph),
                PublicPathExactObjectOperation(
                    PublicPathStableReference(
                        TEXT("object"),
                        Fixture.Node->NodeGuid))));
    TestTrue(
        TEXT("Universal ObjectExpr keyword cannot be used as a semanticTag"),
        PublicPathHasDiagnosticCode(
            ReservedObjectTagResult,
            TEXT("language.invalid_object_shape")));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FSalModuleQueryResultSizeGateTest,
    "Loomle.Sal.PublicPath.Query.FinalResultSizeGate",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSalModuleQueryResultSizeGateTest::RunTest(
    const FString& Parameters)
{
    FPublicPathClassFixture Fixture;
    TestNotNull(
        TEXT("Result-size fixture creates a generated Class"),
        Fixture.Class);
    if (Fixture.Class == nullptr)
    {
        return false;
    }

    const FName MetadataKey(TEXT("LoomleOversizedResult"));
    Fixture.Class->SetMetaData(
        MetadataKey,
        *FString::ChrN(140 * 1024, TEXT('x')));
    const TSharedPtr<FJsonObject> Result =
        FSalModule::BuildQueryResult(
            PublicPathQueryArguments(
                TEXT("actorClass"),
                PublicPathClassCall(
                    Fixture.Class->GetPathName()),
                TEXT("summary")));
    Fixture.Class->RemoveMetaData(MetadataKey);

    FString ValidationError;
    TestTrue(
        *FString::Printf(
            TEXT("Oversized replacement result remains contract-valid: %s"),
            *ValidationError),
        IsValidPublicPathOutgoingResult(
            Result,
            ValidationError));
    TestTrue(
        TEXT("Final public Query rejects output beyond 128 KiB"),
        PublicPathHasDiagnosticCode(
            Result,
            TEXT("validation.result_too_large")));
    TestFalse(
        TEXT("Oversized public Query returns no partial object"),
        Result.IsValid() && Result->HasField(TEXT("object")));
    return true;
}

#endif
