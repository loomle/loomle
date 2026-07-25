// Copyright 2026 Loomle contributors.

#include "SalResultTargets.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "SalObjectBuilder.h"

namespace Loomle::Sal::ResultTargets
{
namespace
{
FString KeyPart(
    const TSharedPtr<FJsonObject>& Target,
    const TCHAR* Field)
{
    FString Value;
    return Target.IsValid()
            && Target->TryGetStringField(Field, Value)
        ? FString::Printf(TEXT("%d:%s"), Value.Len(), *Value)
        : TEXT("#");
}

FString TargetKey(const TSharedPtr<FJsonObject>& Target)
{
    FString Domain;
    if (!Target.IsValid()
        || !Target->TryGetStringField(TEXT("domain"), Domain))
    {
        return FString();
    }
    if (Domain == TEXT("asset"))
    {
        return FString::Printf(
            TEXT("asset|%s|%s"),
            *KeyPart(Target, TEXT("path")),
            *KeyPart(Target, TEXT("type")));
    }
    if (Domain == TEXT("blueprint") || Domain == TEXT("widget"))
    {
        return FString::Printf(
            TEXT("%s|%s|%s"),
            *Domain,
            *KeyPart(Target, TEXT("asset")),
            *KeyPart(Target, TEXT("id")));
    }
    if (Domain == TEXT("class"))
    {
        return FString::Printf(
            TEXT("class|%s"),
            *KeyPart(Target, TEXT("path")));
    }
    if (Domain == TEXT("graph"))
    {
        return FString::Printf(
            TEXT("graph|%s|%s|%s"),
            *KeyPart(Target, TEXT("asset")),
            *KeyPart(Target, TEXT("blueprintId")),
            *KeyPart(Target, TEXT("id")));
    }
    if (Domain == TEXT("state_tree"))
    {
        return FString::Printf(
            TEXT("state_tree|%s|%s"),
            *KeyPart(Target, TEXT("asset")),
            *KeyPart(Target, TEXT("type")));
    }
    return FString();
}

void AddBindingAlias(
    const TSharedPtr<FJsonObject>& Binding,
    TSet<FString>& Out)
{
    FString Alias;
    if (Binding.IsValid()
        && Binding->TryGetStringField(TEXT("alias"), Alias))
    {
        Out.Add(Alias);
    }
}

TSet<FString> UsedAliases(const TSharedPtr<FJsonObject>& Result)
{
    TSet<FString> Aliases;
    const TSharedPtr<FJsonObject>* Main = nullptr;
    if (Result->TryGetObjectField(TEXT("target"), Main)
        && Main != nullptr)
    {
        AddBindingAlias(*Main, Aliases);
    }
    const TArray<TSharedPtr<FJsonValue>>* Related = nullptr;
    if (Result->TryGetArrayField(TEXT("relatedTargets"), Related)
        && Related != nullptr)
    {
        for (const TSharedPtr<FJsonValue>& Value : *Related)
        {
            const TSharedPtr<FJsonObject>* Binding = nullptr;
            if (Value.IsValid()
                && Value->TryGetObject(Binding)
                && Binding != nullptr)
            {
                AddBindingAlias(*Binding, Aliases);
            }
        }
    }
    const TSharedPtr<FJsonObject>* Object = nullptr;
    const TArray<TSharedPtr<FJsonValue>>* Statements = nullptr;
    if (Result->TryGetObjectField(TEXT("object"), Object)
        && Object != nullptr
        && (*Object)->TryGetArrayField(TEXT("statements"), Statements)
        && Statements != nullptr)
    {
        for (const TSharedPtr<FJsonValue>& StatementValue : *Statements)
        {
            const TSharedPtr<FJsonObject>* Statement = nullptr;
            const TSharedPtr<FJsonObject>* Ref = nullptr;
            FString Kind;
            FString Name;
            if (StatementValue.IsValid()
                && StatementValue->TryGetObject(Statement)
                && Statement != nullptr
                && (*Statement)->TryGetObjectField(TEXT("target"), Ref)
                && Ref != nullptr
                && (*Ref)->TryGetStringField(TEXT("kind"), Kind)
                && Kind == TEXT("local")
                && (*Ref)->TryGetStringField(TEXT("name"), Name))
            {
                Aliases.Add(Name);
            }
        }
    }
    return Aliases;
}

bool ReadTargetBinding(
    const TSharedPtr<FJsonObject>& Binding,
    FString& OutAlias,
    TSharedPtr<FJsonObject>& OutTarget)
{
    const TSharedPtr<FJsonObject>* Target = nullptr;
    return Binding.IsValid()
        && Binding->TryGetStringField(TEXT("alias"), OutAlias)
        && Binding->TryGetObjectField(TEXT("target"), Target)
        && Target != nullptr
        && (OutTarget = *Target).IsValid();
}
}

TSharedPtr<FJsonObject> Blueprint(
    const FString& AssetPath,
    const FString& BlueprintId)
{
    if (AssetPath.IsEmpty() || BlueprintId.IsEmpty())
    {
        return nullptr;
    }
    TSharedPtr<FJsonObject> Target = MakeShared<FJsonObject>();
    Target->SetStringField(TEXT("kind"), TEXT("target"));
    Target->SetStringField(TEXT("domain"), TEXT("blueprint"));
    Target->SetStringField(TEXT("asset"), AssetPath);
    Target->SetStringField(TEXT("id"), BlueprintId);
    return Target;
}

TSharedPtr<FJsonObject> Graph(
    const FString& AssetPath,
    const FString& BlueprintId,
    const FString& GraphId)
{
    if (AssetPath.IsEmpty()
        || BlueprintId.IsEmpty()
        || GraphId.IsEmpty())
    {
        return nullptr;
    }
    TSharedPtr<FJsonObject> Target = MakeShared<FJsonObject>();
    Target->SetStringField(TEXT("kind"), TEXT("target"));
    Target->SetStringField(TEXT("domain"), TEXT("graph"));
    Target->SetStringField(TEXT("asset"), AssetPath);
    Target->SetStringField(TEXT("blueprintId"), BlueprintId);
    Target->SetStringField(TEXT("id"), GraphId);
    return Target;
}

FString AddHandoff(
    const TSharedPtr<FJsonObject>& Result,
    const TSharedPtr<FJsonObject>& Target,
    const FString& PreferredAlias,
    const FString& Purpose)
{
    const FString Key = TargetKey(Target);
    if (!Result.IsValid()
        || Key.IsEmpty()
        || Purpose.IsEmpty())
    {
        return FString();
    }

    const TSharedPtr<FJsonObject>* MainBinding = nullptr;
    if (Result->TryGetObjectField(TEXT("target"), MainBinding)
        && MainBinding != nullptr)
    {
        FString MainAlias;
        TSharedPtr<FJsonObject> MainTarget;
        if (ReadTargetBinding(*MainBinding, MainAlias, MainTarget)
            && TargetKey(MainTarget) == Key)
        {
            return FString();
        }
    }

    TArray<TSharedPtr<FJsonValue>> Related;
    const TArray<TSharedPtr<FJsonValue>>* ExistingRelated = nullptr;
    if (Result->TryGetArrayField(
            TEXT("relatedTargets"),
            ExistingRelated)
        && ExistingRelated != nullptr)
    {
        Related = *ExistingRelated;
    }

    FString Alias;
    for (const TSharedPtr<FJsonValue>& Value : Related)
    {
        const TSharedPtr<FJsonObject>* Binding = nullptr;
        FString CandidateAlias;
        TSharedPtr<FJsonObject> CandidateTarget;
        if (Value.IsValid()
            && Value->TryGetObject(Binding)
            && Binding != nullptr
            && ReadTargetBinding(
                *Binding,
                CandidateAlias,
                CandidateTarget)
            && TargetKey(CandidateTarget) == Key)
        {
            Alias = CandidateAlias;
            break;
        }
    }

    if (Alias.IsEmpty())
    {
        TSet<FString> Aliases = UsedAliases(Result);
        const FString Base = FSalObjectBuilder::SanitizeIdentifier(
            PreferredAlias,
            TEXT("related_target"));
        Alias = Base;
        for (int32 Suffix = 2; Aliases.Contains(Alias); ++Suffix)
        {
            Alias = FString::Printf(TEXT("%s_%d"), *Base, Suffix);
        }
        TSharedPtr<FJsonObject> Binding = MakeShared<FJsonObject>();
        Binding->SetStringField(TEXT("alias"), Alias);
        Binding->SetObjectField(TEXT("target"), Target);
        Related.Add(MakeShared<FJsonValueObject>(Binding));
        Result->SetArrayField(TEXT("relatedTargets"), Related);
    }

    TArray<TSharedPtr<FJsonValue>> Handoffs;
    const TArray<TSharedPtr<FJsonValue>>* ExistingHandoffs = nullptr;
    if (Result->TryGetArrayField(TEXT("handoffs"), ExistingHandoffs)
        && ExistingHandoffs != nullptr)
    {
        Handoffs = *ExistingHandoffs;
        for (const TSharedPtr<FJsonValue>& Value : Handoffs)
        {
            const TSharedPtr<FJsonObject>* Handoff = nullptr;
            const TSharedPtr<FJsonObject>* Ref = nullptr;
            FString ExistingPurpose;
            FString ExistingAlias;
            if (Value.IsValid()
                && Value->TryGetObject(Handoff)
                && Handoff != nullptr
                && (*Handoff)->TryGetStringField(
                    TEXT("purpose"),
                    ExistingPurpose)
                && ExistingPurpose == Purpose
                && (*Handoff)->TryGetObjectField(TEXT("target"), Ref)
                && Ref != nullptr
                && (*Ref)->TryGetStringField(
                    TEXT("name"),
                    ExistingAlias)
                && ExistingAlias == Alias)
            {
                return Alias;
            }
        }
    }

    TSharedPtr<FJsonObject> Ref = MakeShared<FJsonObject>();
    Ref->SetStringField(TEXT("kind"), TEXT("local"));
    Ref->SetStringField(TEXT("name"), Alias);
    TSharedPtr<FJsonObject> Handoff = MakeShared<FJsonObject>();
    Handoff->SetStringField(TEXT("kind"), TEXT("target_handoff"));
    Handoff->SetStringField(TEXT("purpose"), Purpose);
    Handoff->SetObjectField(TEXT("target"), Ref);
    Handoffs.Add(MakeShared<FJsonValueObject>(Handoff));
    Result->SetArrayField(TEXT("handoffs"), Handoffs);
    return Alias;
}
}
