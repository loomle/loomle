// Copyright 2026 Loomle contributors.

#include "SalAssetInterface.h"

#include "../SalDiagnostics.h"
#include "../SalObjectBuilder.h"
#include "../SalRuntime.h"
#include "AssetRegistry/AssetData.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Engine/Blueprint.h"
#include "FileHelpers.h"
#include "Misc/Crc.h"
#include "Modules/ModuleManager.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/Package.h"
#include "UObject/UObjectGlobals.h"
#include "WidgetBlueprint.h"

namespace Loomle::Sal
{
namespace
{
constexpr int32 DefaultPageLimit = 50;

struct FAssetMatch
{
    FAssetData Data;
    double Score = 0.0;
};

TSharedPtr<FJsonObject> ErrorResult(
    const FString& Code,
    const FString& Message,
    const FString& Operation = FString(),
    const TArray<FString>& Supported = {})
{
    FSalDiagnosticBuilder Diagnostic = FSalDiagnostics::Error(Code, Message).Interface(TEXT("asset"));
    if (!Operation.IsEmpty())
    {
        Diagnostic.Operation(Operation);
    }
    if (!Supported.IsEmpty())
    {
        Diagnostic.Supported(Supported);
    }
    return FSalDiagnostics::Result(Diagnostic.Build());
}

FString AssetPath(const FAssetData& Data)
{
    return Data.GetSoftObjectPath().ToString();
}

FString AssetRoot(const FAssetData& Data)
{
    const FString PackageName = Data.PackageName.ToString();
    if (!PackageName.StartsWith(TEXT("/")))
    {
        return PackageName;
    }
    const int32 Slash = PackageName.Find(TEXT("/"), ESearchCase::CaseSensitive, ESearchDir::FromStart, 1);
    return Slash == INDEX_NONE ? PackageName : PackageName.Left(Slash);
}

FString TagValue(const FAssetData& Data, const FString& Key)
{
    const FAssetTagValueRef Value = Data.TagsAndValues.FindTag(FName(*Key));
    return Value.IsSet() ? Value.AsString() : FString();
}

bool HasTag(const FAssetData& Data, const FString& Key)
{
    return Data.TagsAndValues.Contains(FName(*Key));
}

bool TryReadBool(const TSharedPtr<FJsonValue>& Value, bool& OutValue)
{
    if (Value.IsValid() && Value->TryGetBool(OutValue))
    {
        return true;
    }
    const FString Text = ExprString(Value);
    if (Text.Equals(TEXT("true"), ESearchCase::IgnoreCase))
    {
        OutValue = true;
        return true;
    }
    if (Text.Equals(TEXT("false"), ESearchCase::IgnoreCase))
    {
        OutValue = false;
        return true;
    }
    return false;
}

bool ValidateCondition(const TSharedPtr<FJsonObject>& Condition, FString& OutError)
{
    if (!Condition.IsValid())
    {
        return true;
    }
    FString Kind;
    Condition->TryGetStringField(TEXT("kind"), Kind);
    if (Kind == TEXT("not"))
    {
        const TSharedPtr<FJsonObject>* Inner = nullptr;
        return Condition->TryGetObjectField(TEXT("condition"), Inner)
            && Inner != nullptr
            && ValidateCondition(*Inner, OutError);
    }
    if (Kind == TEXT("and") || Kind == TEXT("or"))
    {
        const TArray<TSharedPtr<FJsonValue>>* Conditions = nullptr;
        if (!Condition->TryGetArrayField(TEXT("conditions"), Conditions) || Conditions == nullptr)
        {
            OutError = TEXT("Asset condition is missing its nested conditions.");
            return false;
        }
        for (const TSharedPtr<FJsonValue>& Value : *Conditions)
        {
            const TSharedPtr<FJsonObject>* Inner = nullptr;
            if (!Value.IsValid()
                || !Value->TryGetObject(Inner)
                || Inner == nullptr
                || !ValidateCondition(*Inner, OutError))
            {
                return false;
            }
        }
        return true;
    }

    if (!(Kind == TEXT("eq") || Kind == TEXT("ne") || Kind == TEXT("contains")))
    {
        OutError = TEXT("Asset filters support only =, !=, and ~= for their declared fields.");
        return false;
    }

    const FString Field = ConditionField(Condition);
    const bool bRegistryTag = Field.StartsWith(TEXT("registryTag.")) && Field.Len() > 12;
    const bool bEqualityOnly = Field == TEXT("root") || Field == TEXT("type") || Field == TEXT("loaded");
    const bool bTextField = Field == TEXT("name") || Field == TEXT("path") || bRegistryTag;
    if (!bEqualityOnly && !bTextField)
    {
        OutError = FString::Printf(TEXT("Asset filter field is unsupported: %s."), *Field);
        return false;
    }
    if (Kind == TEXT("contains") && bEqualityOnly)
    {
        OutError = FString::Printf(TEXT("Asset field %s does not support ~=."), *Field);
        return false;
    }
    if (Field == TEXT("loaded"))
    {
        bool bValue = false;
        if (!TryReadBool(Condition->TryGetField(TEXT("value")), bValue))
        {
            OutError = TEXT("Asset loaded filter requires a Boolean value.");
            return false;
        }
    }
    return true;
}

bool MatchString(const FString& Left, const FString& Right, const FString& Kind)
{
    if (Kind == TEXT("eq"))
    {
        return Left.Equals(Right, ESearchCase::CaseSensitive);
    }
    if (Kind == TEXT("ne"))
    {
        return !Left.Equals(Right, ESearchCase::CaseSensitive);
    }
    return Left.Contains(Right, ESearchCase::IgnoreCase);
}

bool MatchesCondition(const FAssetData& Data, const TSharedPtr<FJsonObject>& Condition)
{
    if (!Condition.IsValid())
    {
        return true;
    }
    FString Kind;
    Condition->TryGetStringField(TEXT("kind"), Kind);
    if (Kind == TEXT("not"))
    {
        const TSharedPtr<FJsonObject>* Inner = nullptr;
        return Condition->TryGetObjectField(TEXT("condition"), Inner)
            && Inner != nullptr
            && !MatchesCondition(Data, *Inner);
    }
    if (Kind == TEXT("and") || Kind == TEXT("or"))
    {
        const TArray<TSharedPtr<FJsonValue>>* Conditions = nullptr;
        if (!Condition->TryGetArrayField(TEXT("conditions"), Conditions) || Conditions == nullptr)
        {
            return false;
        }
        const bool bAnd = Kind == TEXT("and");
        for (const TSharedPtr<FJsonValue>& Value : *Conditions)
        {
            const TSharedPtr<FJsonObject>* Inner = nullptr;
            const bool bMatches = Value.IsValid()
                && Value->TryGetObject(Inner)
                && Inner != nullptr
                && MatchesCondition(Data, *Inner);
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

    const FString Field = ConditionField(Condition);
    if (Field == TEXT("loaded"))
    {
        bool bExpected = false;
        TryReadBool(Condition->TryGetField(TEXT("value")), bExpected);
        const bool bEqual = Data.IsAssetLoaded() == bExpected;
        return Kind == TEXT("ne") ? !bEqual : bEqual;
    }

    FString Left;
    if (Field == TEXT("root"))
    {
        Left = AssetRoot(Data);
    }
    else if (Field == TEXT("type"))
    {
        Left = Data.AssetClassPath.ToString();
    }
    else if (Field == TEXT("name"))
    {
        Left = Data.AssetName.ToString();
    }
    else if (Field == TEXT("path"))
    {
        Left = AssetPath(Data);
    }
    else
    {
        const FString Key = Field.Mid(12);
        const bool bPresent = HasTag(Data, Key);
        const FString Right = ExprString(Condition->TryGetField(TEXT("value")));
        if (!bPresent)
        {
            return Kind == TEXT("ne");
        }
        return MatchString(TagValue(Data, Key), Right, Kind);
    }
    return MatchString(Left, ExprString(Condition->TryGetField(TEXT("value"))), Kind);
}

bool FindText(const FAssetData& Data, const FString& Text, bool& bTagMatch)
{
    bTagMatch = false;
    if (Text.IsEmpty())
    {
        return true;
    }
    if (Data.AssetName.ToString().Contains(Text, ESearchCase::IgnoreCase)
        || AssetPath(Data).Contains(Text, ESearchCase::IgnoreCase)
        || Data.PackageName.ToString().Contains(Text, ESearchCase::IgnoreCase)
        || Data.AssetClassPath.ToString().Contains(Text, ESearchCase::IgnoreCase))
    {
        return true;
    }
    Data.TagsAndValues.ForEach([&](const TPair<FName, FAssetTagValueRef>& Pair)
    {
        bTagMatch |= Pair.Key.ToString().Contains(Text, ESearchCase::IgnoreCase)
            || Pair.Value.AsString().Contains(Text, ESearchCase::IgnoreCase);
    });
    return bTagMatch;
}

double ScoreAsset(const FAssetData& Data, const FString& Text, const bool bTagMatch)
{
    if (Text.IsEmpty())
    {
        return 0.0;
    }
    const FString Name = Data.AssetName.ToString();
    const FString Path = AssetPath(Data);
    const FString Package = Data.PackageName.ToString();
    if (Path.Equals(Text, ESearchCase::IgnoreCase) || Package.Equals(Text, ESearchCase::IgnoreCase))
    {
        return 100.0;
    }
    if (Name.Equals(Text, ESearchCase::IgnoreCase))
    {
        return 98.0;
    }
    if (Name.StartsWith(Text, ESearchCase::IgnoreCase))
    {
        return 90.0;
    }
    if (Name.Contains(Text, ESearchCase::IgnoreCase))
    {
        return 80.0;
    }
    if (Path.Contains(Text, ESearchCase::IgnoreCase) || Package.Contains(Text, ESearchCase::IgnoreCase))
    {
        return 70.0;
    }
    return bTagMatch ? 60.0 : 50.0;
}

TArray<FString> DomainsFor(const FAssetData& Data)
{
    TArray<FString> Domains = {TEXT("asset")};
    const FString ClassPath = Data.AssetClassPath.ToString();
    const UClass* AssetClass = FindObject<UClass>(nullptr, *ClassPath);
    if (AssetClass != nullptr && AssetClass->IsChildOf(UBlueprint::StaticClass()))
    {
        Domains.Add(TEXT("blueprint"));
    }
    if (AssetClass != nullptr && AssetClass->IsChildOf(UWidgetBlueprint::StaticClass()))
    {
        Domains.Add(TEXT("widget"));
    }
    return Domains;
}

TSharedPtr<FJsonObject> RegistryTags(
    const FAssetData& Data,
    TSharedPtr<FJsonObject>& OutUnrepresentableTags)
{
    TArray<TPair<FString, FString>> Tags;
    Tags.Reserve(Data.TagsAndValues.Num());
    Data.TagsAndValues.ForEach([&](const TPair<FName, FAssetTagValueRef>& Pair)
    {
        Tags.Emplace(Pair.Key.ToString(), Pair.Value.AsString());
    });
    Tags.Sort([](const TPair<FString, FString>& Left, const TPair<FString, FString>& Right)
    {
        return Left.Key < Right.Key;
    });
    TSharedPtr<FJsonObject> Object = MakeShared<FJsonObject>();
    for (const TPair<FString, FString>& Pair : Tags)
    {
        // Inline SAL objects require identifier keys. `kind` is also reserved
        // because its value can make the object indistinguishable from a SAL
        // reference or Call in the normalized object contract.
        if (FSalObjectBuilder::IsIdentifier(Pair.Key) && Pair.Key != TEXT("kind"))
        {
            Object->SetStringField(Pair.Key, Pair.Value);
        }
        else
        {
            if (!OutUnrepresentableTags.IsValid())
            {
                OutUnrepresentableTags = MakeShared<FJsonObject>();
            }
            OutUnrepresentableTags->SetStringField(Pair.Key, Pair.Value);
        }
    }
    return Object;
}

FString RegistryTagFallbackComment(const TSharedPtr<FJsonObject>& Tags)
{
    if (!Tags.IsValid() || Tags->Values.IsEmpty())
    {
        return FString();
    }
    FString Json;
    const TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
        TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Json);
    FJsonSerializer::Serialize(Tags.ToSharedRef(), Writer);
    return TEXT("registryTags not representable as SAL inline fields; exact native key/value JSON:\n") + Json;
}

TSharedPtr<FJsonValue> AssetValue(
    const FAssetData& Data,
    const double Score,
    const bool bIncludeScore,
    const bool bRegistryTags,
    TSharedPtr<FJsonObject>* OutUnrepresentableTags = nullptr)
{
    TSharedPtr<FJsonObject> Args = MakeShared<FJsonObject>();
    Args->SetStringField(TEXT("path"), AssetPath(Data));
    Args->SetStringField(TEXT("type"), Data.AssetClassPath.ToString());
    TArray<TSharedPtr<FJsonValue>> Domains;
    for (const FString& Domain : DomainsFor(Data))
    {
        Domains.Add(Value::Name(Domain));
    }
    Args->SetArrayField(TEXT("domains"), Domains);
    Args->SetBoolField(TEXT("loaded"), Data.IsAssetLoaded());
    if (bIncludeScore)
    {
        Args->SetNumberField(TEXT("score"), Score);
    }
    if (bRegistryTags)
    {
        TSharedPtr<FJsonObject> UnrepresentableTags;
        Args->SetObjectField(TEXT("registryTags"), RegistryTags(Data, UnrepresentableTags));
        if (OutUnrepresentableTags != nullptr)
        {
            *OutUnrepresentableTags = MoveTemp(UnrepresentableTags);
        }
    }
    return Value::Call(TEXT("asset"), Args);
}

int32 CompareString(const FString& Left, const FString& Right)
{
    return Left.Compare(Right, ESearchCase::IgnoreCase);
}

int32 CompareMatch(const FAssetMatch& Left, const FAssetMatch& Right, const FSalQuery& Query)
{
    for (const TSharedPtr<FJsonObject>& Entry : Query.OrderBy)
    {
        FString Key;
        FString Direction;
        Entry->TryGetStringField(TEXT("key"), Key);
        Entry->TryGetStringField(TEXT("direction"), Direction);
        int32 Result = 0;
        if (Key == TEXT("score"))
        {
            Result = Left.Score < Right.Score ? -1 : Left.Score > Right.Score ? 1 : 0;
        }
        else if (Key == TEXT("name"))
        {
            Result = CompareString(Left.Data.AssetName.ToString(), Right.Data.AssetName.ToString());
        }
        else if (Key == TEXT("path"))
        {
            Result = CompareString(AssetPath(Left.Data), AssetPath(Right.Data));
        }
        else if (Key == TEXT("type"))
        {
            Result = CompareString(Left.Data.AssetClassPath.ToString(), Right.Data.AssetClassPath.ToString());
        }
        if (Result != 0)
        {
            return Direction == TEXT("desc") ? -Result : Result;
        }
    }
    if (Query.OrderBy.IsEmpty() && Left.Score != Right.Score)
    {
        return Left.Score > Right.Score ? -1 : 1;
    }
    return AssetPath(Left.Data).Compare(AssetPath(Right.Data), ESearchCase::CaseSensitive);
}

bool ValidateQuery(const FSalQuery& Query, const FSalResolvedTarget& Target, FString& OutOperation, FString& OutError)
{
    if (Target.Kind != ESalTargetKind::AssetRoot)
    {
        OutError = TEXT("Asset Query requires the asset collection root.");
        return false;
    }
    if (!Query.Operation.IsValid() || !Query.Operation->TryGetStringField(TEXT("kind"), OutOperation) || OutOperation != TEXT("assets"))
    {
        OutError = TEXT("Asset supports only the assets collection Query.");
        return false;
    }
    for (const FString& Detail : Query.With)
    {
        if (Detail != TEXT("registryTags"))
        {
            OutError = FString::Printf(TEXT("Asset Query does not support with %s."), *Detail);
            return false;
        }
    }
    for (const TSharedPtr<FJsonObject>& Entry : Query.OrderBy)
    {
        FString Key;
        Entry->TryGetStringField(TEXT("key"), Key);
        if (!(Key == TEXT("score") || Key == TEXT("name") || Key == TEXT("path") || Key == TEXT("type")))
        {
            OutError = FString::Printf(TEXT("Asset order key is unsupported: %s."), *Key);
            return false;
        }
    }
    return ValidateCondition(Query.Where, OutError);
}

FString QuerySignature(const FSalQuery& Query)
{
    FString Signature;
    FString Text;
    Query.Operation->TryGetStringField(TEXT("text"), Text);
    Signature += Text + TEXT("|");
    if (Query.Where.IsValid())
    {
        FString Where;
        const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Where);
        FJsonSerializer::Serialize(Query.Where.ToSharedRef(), Writer);
        Signature += Where;
    }
    Signature += TEXT("|") + FString::Join(Query.With, TEXT(","));
    for (const TSharedPtr<FJsonObject>& Entry : Query.OrderBy)
    {
        FString Key;
        FString Direction;
        Entry->TryGetStringField(TEXT("key"), Key);
        Entry->TryGetStringField(TEXT("direction"), Direction);
        Signature += FString::Printf(TEXT("|%s:%s"), *Key, *Direction);
    }
    return FString::Printf(TEXT("%08x"), FCrc::StrCrc32(*Signature));
}

FString EncodeAssetCursor(const FSalQuery& Query, const int32 Offset)
{
    return FString::Printf(TEXT("asset:%s:%d"), *QuerySignature(Query), Offset);
}

bool DecodeAssetCursor(const FSalQuery& Query, int32& OutOffset)
{
    OutOffset = 0;
    if (Query.PageAfter.IsEmpty())
    {
        return true;
    }
    TArray<FString> Parts;
    Query.PageAfter.ParseIntoArray(Parts, TEXT(":"), false);
    if (Parts.Num() != 3
        || Parts[0] != TEXT("asset")
        || Parts[1] != QuerySignature(Query)
        || !ParseNonNegativeInt32(Parts[2], OutOffset))
    {
        return false;
    }
    return true;
}

TSharedPtr<FJsonObject> CurrentAssetObject(const FSalResolvedTarget& Target)
{
    FSalObjectBuilder Builder;
    if (Target.Object == nullptr)
    {
        return Builder.BuildObject();
    }
    const FAssetData Data(Target.Object);
    const FString Alias = Builder.UniqueAlias(Target.Object->GetName());
    Builder.AddLocalBinding(Alias, AssetValue(Data, 0.0, false, false));
    return Builder.BuildObject();
}

TSharedPtr<FJsonObject> SavePlan(const FSalResolvedTarget& Target, const bool bDirty)
{
    TSharedPtr<FJsonObject> Plan = MakeShared<FJsonObject>();
    Plan->SetStringField(TEXT("operation"), TEXT("save"));
    Plan->SetStringField(TEXT("assetPath"), Target.AssetPath);
    Plan->SetBoolField(TEXT("dirty"), bDirty);
    return Plan;
}

TSharedPtr<FJsonObject> ResolvedSaveRefs(const FSalResolvedTarget& Target)
{
    TSharedPtr<FJsonObject> Asset = MakeShared<FJsonObject>();
    Asset->SetStringField(TEXT("path"), Target.AssetPath);
    TSharedPtr<FJsonObject> Package = MakeShared<FJsonObject>();
    Package->SetStringField(TEXT("name"), Target.Package != nullptr ? Target.Package->GetName() : FString());
    TSharedPtr<FJsonObject> Refs = MakeShared<FJsonObject>();
    Refs->SetObjectField(TEXT("asset"), Asset);
    Refs->SetObjectField(TEXT("package"), Package);
    return Refs;
}
}

TSharedPtr<FJsonObject> FSalAssetInterface::Query(const FSalQuery& Query, const FSalResolvedTarget& Target)
{
    FString Operation;
    FString ValidationError;
    if (!ValidateQuery(Query, Target, Operation, ValidationError))
    {
        return ErrorResult(
            TEXT("capability.unsupported_query"),
            ValidationError,
            Operation,
            {TEXT("assets")});
    }

    int32 Offset = 0;
    if (!DecodeAssetCursor(Query, Offset))
    {
        return ErrorResult(
            TEXT("validation.invalid_cursor"),
            TEXT("Asset cursor does not belong to this Query. Re-run the first page."),
            Operation);
    }

    if (!FModuleManager::Get().ModuleExists(TEXT("AssetRegistry")))
    {
        return ErrorResult(TEXT("capability.asset_registry_unavailable"), TEXT("UE Asset Registry is unavailable."), Operation);
    }

    FString SearchText;
    Query.Operation->TryGetStringField(TEXT("text"), SearchText);
    FAssetRegistryModule& Module = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
    IAssetRegistry& Registry = Module.Get();
    if (!Registry.IsSearchAllAssets())
    {
        Registry.SearchAllAssets(true);
    }
    Registry.WaitForCompletion();

    TArray<FAssetData> Assets;
    Registry.GetAllAssets(Assets, false);
    TArray<FAssetMatch> Matches;
    Matches.Reserve(Assets.Num());
    for (const FAssetData& Data : Assets)
    {
        bool bTagMatch = false;
        if (!Data.IsValid() || !FindText(Data, SearchText, bTagMatch) || !MatchesCondition(Data, Query.Where))
        {
            continue;
        }
        Matches.Add({Data, ScoreAsset(Data, SearchText, bTagMatch)});
    }
    Matches.Sort([&Query](const FAssetMatch& Left, const FAssetMatch& Right)
    {
        return CompareMatch(Left, Right, Query) < 0;
    });

    const int32 Limit = FMath::Max(1, Query.PageLimit > 0 ? Query.PageLimit : DefaultPageLimit);
    const int32 End = static_cast<int32>(FMath::Min<int64>(static_cast<int64>(Offset) + Limit, Matches.Num()));
    FSalObjectBuilder Builder;
    const bool bRegistryTags = HasDetail(Query, TEXT("registryTags"));
    for (int32 Index = FMath::Min(Offset, Matches.Num()); Index < End; ++Index)
    {
        const FAssetMatch& Match = Matches[Index];
        const FString Alias = Builder.UniqueAlias(Match.Data.AssetName.ToString());
        TSharedPtr<FJsonObject> UnrepresentableTags;
        Builder.AddLocalBinding(
            Alias,
            AssetValue(Match.Data, Match.Score, true, bRegistryTags, &UnrepresentableTags));
        const FString Fallback = RegistryTagFallbackComment(UnrepresentableTags);
        if (!Fallback.IsEmpty())
        {
            Builder.AddComment(Fallback);
        }
    }
    if (Builder.BuildObject()->GetArrayField(TEXT("statements")).IsEmpty())
    {
        Builder.AddComment(TEXT("no matches"));
    }
    TSharedPtr<FJsonObject> Result = Builder.BuildResult();
    if (End < Matches.Num())
    {
        TSharedPtr<FJsonObject> Page = MakeShared<FJsonObject>();
        Page->SetStringField(TEXT("next"), EncodeAssetCursor(Query, End));
        Result->SetObjectField(TEXT("page"), Page);
    }
    return Result;
}

TSharedPtr<FJsonObject> FSalAssetInterface::Patch(const FSalPatch& Patch, const FSalResolvedTarget& Target)
{
    if (Target.Kind != ESalTargetKind::Asset || Target.Object == nullptr || Target.Package == nullptr)
    {
        return MakeMutationResult(
            nullptr,
            {FSalDiagnostics::Error(TEXT("validation.exact_asset_required"), TEXT("Asset save requires one exact asset(path: ...) target."))
                .Interface(TEXT("asset"))
                .Build()},
            Patch.bDryRun,
            false,
            false,
            Target.AssetPath,
            TEXT("save"));
    }
    if (Patch.Statements.Num() != 1)
    {
        return MakeMutationResult(
            CurrentAssetObject(Target),
            {FSalDiagnostics::Error(TEXT("capability.unsupported_patch"), TEXT("Asset Patch accepts exactly one independent save operation."))
                .Interface(TEXT("asset"))
                .Operation(TEXT("save"))
                .Build()},
            Patch.bDryRun,
            false,
            false,
            Target.AssetPath,
            TEXT("save"));
    }
    const TSharedPtr<FJsonObject>* Statement = nullptr;
    FString Kind;
    if (!Patch.Statements[0].IsValid()
        || !Patch.Statements[0]->TryGetObject(Statement)
        || Statement == nullptr
        || !(*Statement)->TryGetStringField(TEXT("kind"), Kind)
        || Kind != TEXT("save"))
    {
        return MakeMutationResult(
            CurrentAssetObject(Target),
            {FSalDiagnostics::Error(TEXT("capability.unsupported_patch_operation"), TEXT("Asset defines no Patch operation other than Core save."))
                .Interface(TEXT("asset"))
                .Supported({TEXT("save")})
                .Build()},
            Patch.bDryRun,
            false,
            false,
            Target.AssetPath,
            TEXT("save"));
    }

    const bool bDirty = Target.Package->IsDirty();
    const TSharedPtr<FJsonObject> Planned = SavePlan(Target, bDirty);
    const TSharedPtr<FJsonObject> ResolvedRefs = ResolvedSaveRefs(Target);
    if (Patch.bDryRun || !bDirty)
    {
        return MakeMutationResult(
            CurrentAssetObject(Target),
            {},
            Patch.bDryRun,
            true,
            false,
            Target.AssetPath,
            TEXT("save"),
            Planned,
            ResolvedRefs);
    }

    const TArray<UPackage*> Packages = {Target.Package};
    if (!UEditorLoadingAndSavingUtils::SavePackages(Packages, true))
    {
        return MakeMutationResult(
            CurrentAssetObject(Target),
            {FSalDiagnostics::Error(TEXT("validation.save_failed"), TEXT("UE failed to save the asset Package."))
                .Interface(TEXT("asset"))
                .Operation(TEXT("save"))
                .Ref(Target.AssetPath)
                .Build()},
            false,
            false,
            false,
            Target.AssetPath,
            TEXT("save"),
            Planned,
            ResolvedRefs);
    }
    return MakeMutationResult(
        CurrentAssetObject(Target),
        {},
        false,
        true,
        true,
        Target.AssetPath,
        TEXT("save"),
        Planned,
        ResolvedRefs);
}
}
