// Copyright 2026 Loomle contributors.

#include "LglCapabilityValidator.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "LglDiagnostics.h"
#include "LglResult.h"

namespace Loomle::Lgl
{
namespace
{
FString JoinFieldPath(const TArray<FString>& Path)
{
    FString Result;
    for (int32 Index = 0; Index < Path.Num(); ++Index)
    {
        if (Index > 0)
        {
            Result += TEXT(".");
        }
        Result += Path[Index];
    }
    return Result;
}

bool ReadFieldPath(const TSharedPtr<FJsonObject>& Condition, TArray<FString>& OutPath)
{
    OutPath.Reset();

    const TSharedPtr<FJsonObject>* Field = nullptr;
    if (!Condition.IsValid() || !Condition->TryGetObjectField(TEXT("field"), Field) || Field == nullptr || !(*Field).IsValid())
    {
        return false;
    }

    const TArray<TSharedPtr<FJsonValue>>* Path = nullptr;
    if (!(*Field)->TryGetArrayField(TEXT("path"), Path) || Path == nullptr || Path->Num() == 0)
    {
        return false;
    }

    for (const TSharedPtr<FJsonValue>& Segment : *Path)
    {
        FString SegmentText;
        if (!Segment.IsValid() || !Segment->TryGetString(SegmentText) || SegmentText.IsEmpty())
        {
            return false;
        }
        OutPath.Add(SegmentText);
    }
    return true;
}

bool MatchesFieldPath(const TArray<FString>& Path, const TArray<FString>& Supported)
{
    const FString Field = JoinFieldPath(Path);
    for (const FString& Candidate : Supported)
    {
        if (Candidate == TEXT("*"))
        {
            return true;
        }
        if (Candidate.EndsWith(TEXT(".*")))
        {
            const FString Prefix = Candidate.LeftChop(2);
            if (Path.Num() > 0 && Path[0] == Prefix)
            {
                return true;
            }
        }
        if (Candidate == Field)
        {
            return true;
        }
    }
    return false;
}

bool ContainsString(const TArray<FString>& Values, const FString& Value)
{
    return Values.Contains(Value);
}

FLglObjectResult CapabilityError(
    const FString& Code,
    const FString& Message,
    const FLglQueryCapabilities& Capabilities,
    const TArray<FString>& Path,
    const FString& Actual,
    const TArray<FString>& Supported)
{
    return FLglResult::FromDiagnostic(
        FLglDiagnostics::Error(Code, Message)
            .Domain(Capabilities.Domain)
            .Path(Path)
            .Actual(Actual)
            .Supported(Supported)
            .Build());
}

bool ValidateFind(
    const TSharedPtr<FJsonObject>& Object,
    const FLglQueryCapabilities& Capabilities,
    FLglObjectResult& OutError)
{
    if (!Capabilities.bValidateFindKinds)
    {
        return true;
    }

    const TSharedPtr<FJsonObject>* Find = nullptr;
    if (!Object->TryGetObjectField(TEXT("find"), Find) || Find == nullptr || !(*Find).IsValid())
    {
        return true;
    }

    FString Kind;
    (*Find)->TryGetStringField(TEXT("kind"), Kind);
    if (!ContainsString(Capabilities.FindKinds, Kind))
    {
        OutError = CapabilityError(
            TEXT("capability.unsupported_find"),
            FString::Printf(TEXT("Domain %s does not support find %s."), *Capabilities.Domain, *Kind),
            Capabilities,
            {TEXT("find"), TEXT("kind")},
            Kind,
            Capabilities.FindKinds);
        return false;
    }

    return true;
}

bool ValidateDetails(
    const TSharedPtr<FJsonObject>& Object,
    const FLglQueryCapabilities& Capabilities,
    FLglObjectResult& OutError)
{
    if (!Capabilities.bValidateDetails)
    {
        return true;
    }

    const TArray<TSharedPtr<FJsonValue>>* Details = nullptr;
    if (!Object->TryGetArrayField(TEXT("with"), Details))
    {
        return true;
    }

    for (const TSharedPtr<FJsonValue>& DetailValue : *Details)
    {
        FString Detail;
        DetailValue->TryGetString(Detail);
        if (!ContainsString(Capabilities.Details, Detail))
        {
            OutError = CapabilityError(
                TEXT("capability.unsupported_detail"),
                FString::Printf(TEXT("Domain %s does not support detail %s."), *Capabilities.Domain, *Detail),
                Capabilities,
                {TEXT("with")},
                Detail,
                Capabilities.Details);
            return false;
        }
    }

    return true;
}

bool ValidateOrderBy(
    const TSharedPtr<FJsonObject>& Object,
    const FLglQueryCapabilities& Capabilities,
    FLglObjectResult& OutError)
{
    if (!Capabilities.bValidateOrderKeys)
    {
        return true;
    }

    const TArray<TSharedPtr<FJsonValue>>* Orders = nullptr;
    if (!Object->TryGetArrayField(TEXT("orderBy"), Orders))
    {
        return true;
    }

    for (const TSharedPtr<FJsonValue>& OrderValue : *Orders)
    {
        const TSharedPtr<FJsonObject>* Order = nullptr;
        if (!OrderValue.IsValid() || !OrderValue->TryGetObject(Order) || Order == nullptr || !(*Order).IsValid())
        {
            continue;
        }

        FString Key;
        (*Order)->TryGetStringField(TEXT("key"), Key);
        TArray<FString> KeyPath;
        Key.ParseIntoArray(KeyPath, TEXT("."));
        if (!MatchesFieldPath(KeyPath, Capabilities.OrderKeys))
        {
            OutError = CapabilityError(
                TEXT("capability.unsupported_order_key"),
                FString::Printf(TEXT("Domain %s does not support order key %s."), *Capabilities.Domain, *Key),
                Capabilities,
                {TEXT("orderBy")},
                Key,
                Capabilities.OrderKeys);
            return false;
        }
    }

    return true;
}

bool ValidatePage(
    const TSharedPtr<FJsonObject>& Object,
    const FLglQueryCapabilities& Capabilities,
    FLglObjectResult& OutError)
{
    const TSharedPtr<FJsonObject>* Page = nullptr;
    if (!Object->TryGetObjectField(TEXT("page"), Page) || Page == nullptr || !(*Page).IsValid())
    {
        return true;
    }

    if ((*Page)->HasField(TEXT("after")) && !Capabilities.bSupportsPageAfter)
    {
        OutError = CapabilityError(
            TEXT("capability.unsupported_pagination"),
            FString::Printf(TEXT("Domain %s does not support page after cursors."), *Capabilities.Domain),
            Capabilities,
            {TEXT("page"), TEXT("after")},
            TEXT("after"),
            {TEXT("limit")});
        return false;
    }

    return true;
}

bool ValidateCondition(
    const TSharedPtr<FJsonObject>& Condition,
    const FLglQueryCapabilities& Capabilities,
    const TArray<FString>& Path,
    FLglObjectResult& OutError)
{
    if (!Condition.IsValid())
    {
        return true;
    }

    FString Kind;
    Condition->TryGetStringField(TEXT("kind"), Kind);
    if (Kind == TEXT("eq") || Kind == TEXT("ne") || Kind == TEXT("contains") || Kind == TEXT("compare"))
    {
        if (Kind == TEXT("compare") && !Capabilities.bSupportsCompare)
        {
            TArray<FString> ComparePath = Path;
            ComparePath.Add(TEXT("kind"));
            OutError = CapabilityError(
                TEXT("capability.unsupported_compare"),
                FString::Printf(TEXT("Domain %s does not support ordered comparison filters."), *Capabilities.Domain),
                Capabilities,
                ComparePath,
                TEXT("compare"),
                {TEXT("eq"), TEXT("ne"), TEXT("contains")});
            return false;
        }

        if (Capabilities.bValidateWhereFields)
        {
            TArray<FString> FieldPath;
            if (ReadFieldPath(Condition, FieldPath) && !MatchesFieldPath(FieldPath, Capabilities.WhereFields))
            {
                TArray<FString> DiagnosticPath = Path;
                DiagnosticPath.Add(TEXT("field"));
                const FString Field = JoinFieldPath(FieldPath);
                OutError = CapabilityError(
                    TEXT("capability.unsupported_where_field"),
                    FString::Printf(TEXT("Domain %s does not support where field %s."), *Capabilities.Domain, *Field),
                    Capabilities,
                    DiagnosticPath,
                    Field,
                    Capabilities.WhereFields);
                return false;
            }
        }
        return true;
    }

    if (Kind == TEXT("not"))
    {
        const TSharedPtr<FJsonObject>* Inner = nullptr;
        if (Condition->TryGetObjectField(TEXT("condition"), Inner) && Inner != nullptr)
        {
            TArray<FString> InnerPath = Path;
            InnerPath.Add(TEXT("condition"));
            return ValidateCondition(*Inner, Capabilities, InnerPath, OutError);
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

        for (int32 Index = 0; Index < Conditions->Num(); ++Index)
        {
            const TSharedPtr<FJsonValue>& Item = (*Conditions)[Index];
            const TSharedPtr<FJsonObject>* ItemObject = nullptr;
            if (!Item.IsValid() || !Item->TryGetObject(ItemObject) || ItemObject == nullptr)
            {
                continue;
            }

            TArray<FString> ItemPath = Path;
            ItemPath.Add(TEXT("conditions"));
            ItemPath.Add(FString::FromInt(Index));
            if (!ValidateCondition(*ItemObject, Capabilities, ItemPath, OutError))
            {
                return false;
            }
        }
    }

    return true;
}

bool ValidateWhere(
    const TSharedPtr<FJsonObject>& Object,
    const FLglQueryCapabilities& Capabilities,
    FLglObjectResult& OutError)
{
    const TSharedPtr<FJsonObject>* Where = nullptr;
    if (!Object->TryGetObjectField(TEXT("where"), Where) || Where == nullptr || !(*Where).IsValid())
    {
        return true;
    }

    return ValidateCondition(*Where, Capabilities, {TEXT("where")}, OutError);
}
}

bool FLglCapabilityValidator::ValidateQuery(
    const FLglObjectRequest& Request,
    const FLglQueryCapabilities& Capabilities,
    FLglObjectResult& OutError)
{
    if (!Request.Object.IsValid())
    {
        return true;
    }

    return ValidateFind(Request.Object, Capabilities, OutError)
        && ValidateDetails(Request.Object, Capabilities, OutError)
        && ValidateOrderBy(Request.Object, Capabilities, OutError)
        && ValidatePage(Request.Object, Capabilities, OutError)
        && ValidateWhere(Request.Object, Capabilities, OutError);
}
}
