// Copyright 2026 Loomle contributors.

#include "LglAssetRegistry.h"

#include "AssetRegistry/AssetData.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Modules/ModuleManager.h"

namespace Loomle::Lgl
{
namespace
{
constexpr int32 DefaultLimit = 50;
constexpr int32 MaxLimit = 200;

FString MakeAliasBase(const FString& Name)
{
    FString Alias;
    Alias.Reserve(Name.Len());

    for (const TCHAR Character : Name)
    {
        if (FChar::IsAlnum(Character) || Character == TEXT('_'))
        {
            Alias.AppendChar(Character);
        }
        else
        {
            Alias.AppendChar(TEXT('_'));
        }
    }

    if (Alias.IsEmpty())
    {
        return TEXT("asset");
    }

    if (FChar::IsDigit(Alias[0]))
    {
        Alias.InsertAt(0, TEXT('_'));
    }

    return Alias;
}

FString MakeUniqueAlias(const FString& Name, TSet<FString>& UsedAliases)
{
    const FString Base = MakeAliasBase(Name);
    FString Alias = Base;
    int32 Suffix = 2;

    while (UsedAliases.Contains(Alias))
    {
        Alias = FString::Printf(TEXT("%s_%d"), *Base, Suffix++);
    }

    UsedAliases.Add(Alias);
    return Alias;
}

FString MakeClassLeaf(const FString& ClassPath)
{
    FString Left;
    FString Right;
    if (ClassPath.Split(TEXT("."), &Left, &Right, ESearchCase::CaseSensitive, ESearchDir::FromEnd))
    {
        return Right;
    }
    if (ClassPath.Split(TEXT("/"), &Left, &Right, ESearchCase::CaseSensitive, ESearchDir::FromEnd))
    {
        return Right;
    }
    return ClassPath;
}

FString InferType(const FString& ClassPath)
{
    if (ClassPath.Contains(TEXT("WidgetBlueprint")))
    {
        return TEXT("widget");
    }
    if (ClassPath.Contains(TEXT("Blueprint")))
    {
        return TEXT("blueprint");
    }
    if (ClassPath.Contains(TEXT("Material")))
    {
        return TEXT("material");
    }
    if (ClassPath.Contains(TEXT("PCGGraph")))
    {
        return TEXT("pcg");
    }

    return MakeClassLeaf(ClassPath).ToLower();
}

void AddStringArray(TSharedPtr<FJsonObject> Object, const FString& Field, const TArray<FString>& Values)
{
    TArray<TSharedPtr<FJsonValue>> JsonValues;
    for (const FString& Value : Values)
    {
        JsonValues.Add(MakeShared<FJsonValueString>(Value));
    }
    Object->SetArrayField(Field, JsonValues);
}

bool MatchesText(const FAssetData& AssetData, const FString& Text)
{
    if (Text.IsEmpty())
    {
        return true;
    }

    const FString AssetName = AssetData.AssetName.ToString();
    const FString ObjectPath = AssetData.GetSoftObjectPath().ToString();
    const FString ClassPath = AssetData.AssetClassPath.ToString();

    return AssetName.Contains(Text, ESearchCase::IgnoreCase)
        || ObjectPath.Contains(Text, ESearchCase::IgnoreCase)
        || ClassPath.Contains(Text, ESearchCase::IgnoreCase);
}

FString RootFromPath(const FString& ObjectPath)
{
    if (ObjectPath.StartsWith(TEXT("/")))
    {
        const int32 NextSlashIndex = ObjectPath.Find(TEXT("/"), ESearchCase::CaseSensitive, ESearchDir::FromStart, 1);
        if (NextSlashIndex != INDEX_NONE)
        {
            return ObjectPath.Left(NextSlashIndex);
        }
    }
    return ObjectPath;
}

FString ReadAssetField(const FAssetData& AssetData, const FString& Field)
{
    const FString ObjectPath = AssetData.GetSoftObjectPath().ToString();
    if (Field == TEXT("root"))
    {
        return RootFromPath(ObjectPath);
    }
    if (Field == TEXT("type"))
    {
        return InferType(AssetData.AssetClassPath.ToString());
    }
    if (Field == TEXT("class"))
    {
        return AssetData.AssetClassPath.ToString();
    }
    if (Field == TEXT("name"))
    {
        return AssetData.AssetName.ToString();
    }
    if (Field == TEXT("path"))
    {
        return ObjectPath;
    }
    return FString();
}

FString ExprToString(const TSharedPtr<FJsonValue>& Value)
{
    if (!Value.IsValid() || Value->IsNull())
    {
        return FString();
    }

    FString StringValue;
    if (Value->TryGetString(StringValue))
    {
        return StringValue;
    }

    double NumberValue = 0.0;
    if (Value->TryGetNumber(NumberValue))
    {
        return FString::SanitizeFloat(NumberValue);
    }

    bool BoolValue = false;
    if (Value->TryGetBool(BoolValue))
    {
        return BoolValue ? TEXT("true") : TEXT("false");
    }

    const TSharedPtr<FJsonObject>* ObjectValue = nullptr;
    if (Value->TryGetObject(ObjectValue) && ObjectValue != nullptr && (*ObjectValue).IsValid())
    {
        FString Kind;
        if ((*ObjectValue)->TryGetStringField(TEXT("kind"), Kind))
        {
            if (Kind == TEXT("name"))
            {
                FString Name;
                (*ObjectValue)->TryGetStringField(TEXT("name"), Name);
                return Name;
            }
            if (Kind == TEXT("local"))
            {
                FString Name;
                (*ObjectValue)->TryGetStringField(TEXT("name"), Name);
                return Name;
            }
            if (Kind == TEXT("id"))
            {
                FString Id;
                (*ObjectValue)->TryGetStringField(TEXT("id"), Id);
                return Id;
            }
            if (Kind == TEXT("member"))
            {
                FString Object;
                FString Member;
                (*ObjectValue)->TryGetStringField(TEXT("object"), Object);
                (*ObjectValue)->TryGetStringField(TEXT("member"), Member);
                return FString::Printf(TEXT("%s.%s"), *Object, *Member);
            }
        }
    }

    return FString();
}

FString ReadConditionField(const TSharedPtr<FJsonObject>& Condition)
{
    const TSharedPtr<FJsonObject>* Field = nullptr;
    if (!Condition.IsValid()
        || !Condition->TryGetObjectField(TEXT("field"), Field)
        || Field == nullptr
        || !(*Field).IsValid())
    {
        return FString();
    }

    const TArray<TSharedPtr<FJsonValue>>* Path = nullptr;
    if (!(*Field)->TryGetArrayField(TEXT("path"), Path) || Path == nullptr || Path->Num() != 1)
    {
        return FString();
    }

    FString FieldName;
    (*Path)[0]->TryGetString(FieldName);
    return FieldName;
}

bool MatchesCondition(const FAssetData& AssetData, const TSharedPtr<FJsonObject>& Condition)
{
    if (!Condition.IsValid())
    {
        return true;
    }

    FString Kind;
    if (!Condition->TryGetStringField(TEXT("kind"), Kind))
    {
        return true;
    }

    if (Kind == TEXT("not"))
    {
        const TSharedPtr<FJsonObject>* Inner = nullptr;
        if (Condition->TryGetObjectField(TEXT("condition"), Inner) && Inner != nullptr)
        {
            return !MatchesCondition(AssetData, *Inner);
        }
        return true;
    }

    if (Kind == TEXT("and") || Kind == TEXT("or"))
    {
        const TArray<TSharedPtr<FJsonValue>>* Conditions = nullptr;
        if (!Condition->TryGetArrayField(TEXT("conditions"), Conditions) || Conditions == nullptr)
        {
            return true;
        }

        const bool bAnd = Kind == TEXT("and");
        for (const TSharedPtr<FJsonValue>& Item : *Conditions)
        {
            const TSharedPtr<FJsonObject>* ItemObject = nullptr;
            const bool bMatches = Item.IsValid()
                && Item->TryGetObject(ItemObject)
                && ItemObject != nullptr
                && MatchesCondition(AssetData, *ItemObject);

            if (bAnd && !bMatches)
            {
                return false;
            }
            if (!bAnd && bMatches)
            {
                return true;
            }
        }
        return bAnd;
    }

    const FString Field = ReadConditionField(Condition);
    const FString Left = ReadAssetField(AssetData, Field);
    const TSharedPtr<FJsonValue> RightValue = Condition->TryGetField(TEXT("value"));
    const FString Right = ExprToString(RightValue);

    if (Kind == TEXT("eq"))
    {
        return Left == Right;
    }
    if (Kind == TEXT("ne"))
    {
        return Left != Right;
    }
    if (Kind == TEXT("contains"))
    {
        return Left.Contains(Right, ESearchCase::IgnoreCase);
    }

    return true;
}

double ScoreAsset(const FAssetData& AssetData, const FString& Text)
{
    if (Text.IsEmpty())
    {
        return 0.0;
    }

    const FString AssetName = AssetData.AssetName.ToString();
    const FString ObjectPath = AssetData.GetSoftObjectPath().ToString();

    if (AssetName.Equals(Text, ESearchCase::IgnoreCase))
    {
        return 100.0;
    }
    if (ObjectPath.Equals(Text, ESearchCase::IgnoreCase))
    {
        return 95.0;
    }
    if (AssetName.StartsWith(Text, ESearchCase::IgnoreCase))
    {
        return 80.0;
    }
    if (ObjectPath.Contains(Text, ESearchCase::IgnoreCase))
    {
        return 60.0;
    }
    return 50.0;
}

TSharedPtr<FJsonObject> MakeAssetObject(
    const FAssetData& AssetData,
    const FString& Text,
    TSet<FString>& UsedAliases)
{
    const FString AssetName = AssetData.AssetName.ToString();
    const FString ObjectPath = AssetData.GetSoftObjectPath().ToString();
    const FString ClassPath = AssetData.AssetClassPath.ToString();
    const FString Type = InferType(ClassPath);

    TArray<FString> Domains;
    Domains.Add(TEXT("asset"));
    if (!Type.IsEmpty() && Type != TEXT("asset"))
    {
        Domains.Add(Type);
    }

    TSharedPtr<FJsonObject> Object = MakeShared<FJsonObject>();
    Object->SetStringField(TEXT("kind"), TEXT("asset"));
    Object->SetStringField(TEXT("alias"), MakeUniqueAlias(AssetName, UsedAliases));
    Object->SetStringField(TEXT("path"), ObjectPath);
    Object->SetStringField(TEXT("name"), AssetName);
    Object->SetStringField(TEXT("type"), Type);
    Object->SetStringField(TEXT("class"), ClassPath);
    Object->SetBoolField(TEXT("loaded"), AssetData.IsAssetLoaded());
    Object->SetNumberField(TEXT("score"), ScoreAsset(AssetData, Text));
    AddStringArray(Object, TEXT("domains"), Domains);
    return Object;
}
}

bool FLglAssetRegistry::IsAvailable() const
{
    return FModuleManager::Get().ModuleExists(TEXT("AssetRegistry"));
}

void FLglAssetRegistry::SearchAssets(
    const FString& Text,
    int32 Limit,
    const TSharedPtr<FJsonObject>& Where,
    TArray<TSharedPtr<FJsonObject>>& OutAssets) const
{
    OutAssets.Reset();

    const int32 ClampedLimit = FMath::Clamp(Limit > 0 ? Limit : DefaultLimit, 1, MaxLimit);
    FAssetRegistryModule& AssetRegistryModule =
        FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
    IAssetRegistry& Registry = AssetRegistryModule.Get();

    if (!Registry.IsSearchAllAssets())
    {
        Registry.SearchAllAssets(true);
    }
    Registry.WaitForCompletion();

    TArray<FAssetData> Assets;
    Registry.GetAllAssets(Assets, false);
    Assets.Sort([](const FAssetData& Left, const FAssetData& Right)
    {
        return Left.GetSoftObjectPath().ToString() < Right.GetSoftObjectPath().ToString();
    });

    TSet<FString> UsedAliases;
    for (const FAssetData& AssetData : Assets)
    {
        if (!AssetData.IsValid() || !MatchesText(AssetData, Text) || !MatchesCondition(AssetData, Where))
        {
            continue;
        }

        OutAssets.Add(MakeAssetObject(AssetData, Text, UsedAliases));
        if (OutAssets.Num() >= ClampedLimit)
        {
            break;
        }
    }
}
}
