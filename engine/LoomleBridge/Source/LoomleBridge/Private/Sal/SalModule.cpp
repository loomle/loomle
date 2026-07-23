// Copyright 2026 Loomle contributors.

#include "SalModule.h"

#include "Asset/SalAssetInterface.h"
#include "Blueprint/SalBlueprintInterface.h"
#include "Class/SalClassInterface.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Graph/SalGraphInterface.h"
#include "Misc/Base64.h"
#include "Misc/SecureHash.h"
#include "Reference/SalReferenceInterface.h"
#include "Serialization/JsonSerializer.h"
#include "SalDiagnostics.h"
#include "SalJson.h"
#include "SalModel.h"
#include "SalRuntime.h"
#include "SalTargetResolver.h"
#include "StateTree/SalStateTreeInterface.h"
#include "Widget/SalWidgetInterface.h"

namespace Loomle::Sal
{
namespace
{
constexpr int64 MaxQueryResultUtf8Bytes = 128 * 1024;

TSharedPtr<FJsonObject> InterfaceError(const FString& Operation, const FSalResolvedTarget& Target)
{
    TArray<FString> Interfaces;
    for (const FName Interface : Target.Interfaces) Interfaces.Add(Interface.ToString());
    return FSalDiagnostics::Result(
        FSalDiagnostics::Error(
            TEXT("capability.operation_unavailable"),
            FString::Printf(TEXT("No resolved target interface accepts operation %s."), *Operation))
            .Operation(Operation)
            .Supported(Interfaces)
            .Suggestion(TEXT("Query the exact object with schema or call sal_schema({})."))
            .Build());
}

TSharedPtr<FJsonObject> ValidateOutgoing(const TSharedPtr<FJsonObject>& Result)
{
    TSharedPtr<FJsonObject> ValidationError;
    if (!FSalJson::ValidateResult(Result, ValidationError))
    {
        return ValidationError;
    }
    return Result;
}

TOptional<int64> CondensedJsonUtf8Size(const TSharedPtr<FJsonObject>& Result)
{
    if (!Result.IsValid())
    {
        return {};
    }
    FString Serialized;
    const TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
        TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Serialized);
    if (!FJsonSerializer::Serialize(Result.ToSharedRef(), Writer))
    {
        return {};
    }
    const FTCHARToUTF8 Utf8(*Serialized);
    return Utf8.Length();
}

TSharedPtr<FJsonObject> EnforceQueryResultSize(
    const TSharedPtr<FJsonObject>& Result,
    const FSalQuery& Query)
{
    const TOptional<int64> SizeBytes = CondensedJsonUtf8Size(Result);
    FString Operation;
    if (Query.Operation.IsValid())
    {
        Query.Operation->TryGetStringField(TEXT("kind"), Operation);
    }
    if (!SizeBytes.IsSet())
    {
        FSalDiagnosticBuilder Diagnostic = FSalDiagnostics::Error(
            TEXT("language.invalid_result_shape"),
            TEXT("Query result could not be serialized for output-size validation."))
            .Suggestion(TEXT("Retry with a narrower Query; report the failure if it persists."));
        if (!Operation.IsEmpty())
        {
            Diagnostic.Operation(Operation);
        }
        return FSalDiagnostics::Result(Diagnostic.Build());
    }
    if (SizeBytes.GetValue() <= MaxQueryResultUtf8Bytes)
    {
        return Result;
    }
    FSalDiagnosticBuilder Diagnostic = FSalDiagnostics::Error(
        TEXT("validation.result_too_large"),
        FString::Printf(
            TEXT("Query produced %lld bytes of condensed UTF-8 JSON, exceeding the %lld-byte safety limit."),
            static_cast<long long>(SizeBytes.GetValue()),
            static_cast<long long>(MaxQueryResultUtf8Bytes)))
        .Suggestion(TEXT("Narrow the Query with search, filters, pagination, depth, or an exact object reference."));
    if (!Operation.IsEmpty())
    {
        Diagnostic.Operation(Operation);
    }
    return FSalDiagnostics::Result(Diagnostic.Build());
}

TSharedPtr<FJsonObject> MutationFailure(
    const TSharedPtr<FJsonObject>& ErrorResult,
    const bool bDryRun,
    const FString& AssetPath = FString())
{
    TArray<TSharedPtr<FJsonObject>> Diagnostics;
    const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
    if (ErrorResult.IsValid()
        && ErrorResult->TryGetArrayField(TEXT("diagnostics"), Values)
        && Values != nullptr)
    {
        for (const TSharedPtr<FJsonValue>& Value : *Values)
        {
            const TSharedPtr<FJsonObject>* Diagnostic = nullptr;
            if (Value.IsValid() && Value->TryGetObject(Diagnostic) && Diagnostic != nullptr && (*Diagnostic).IsValid())
            {
                Diagnostics.Add(*Diagnostic);
            }
        }
    }
    if (Diagnostics.IsEmpty())
    {
        Diagnostics.Add(
            FSalDiagnostics::Error(TEXT("validation.invalid_mutation_result"), TEXT("Patch failed without a diagnostic."))
                .Build());
    }
    return MakeMutationResult(
        nullptr,
        Diagnostics,
        bDryRun,
        false,
        false,
        AssetPath,
        TEXT("patch"));
}

bool RequestedDryRun(const TSharedPtr<FJsonObject>& Arguments)
{
    const TSharedPtr<FJsonObject>* Object = nullptr;
    bool bDryRun = false;
    return Arguments.IsValid()
        && Arguments->TryGetObjectField(TEXT("object"), Object)
        && Object != nullptr
        && (*Object).IsValid()
        && (*Object)->TryGetBoolField(TEXT("dryRun"), bDryRun)
        && bDryRun;
}

FString OperationKind(const TSharedPtr<FJsonObject>& Operation)
{
    FString Kind;
    if (Operation.IsValid()) Operation->TryGetStringField(TEXT("kind"), Kind);
    return Kind;
}

bool ValueContainsStableRefKind(const TSharedPtr<FJsonValue>& Value, const FString& ExpectedKind)
{
    if (!Value.IsValid()) return false;
    const TSharedPtr<FJsonObject>* Object = nullptr;
    if (Value->TryGetObject(Object) && Object != nullptr && (*Object).IsValid())
    {
        FString Kind;
        if ((*Object)->TryGetStringField(TEXT("kind"), Kind)
            && Kind == ExpectedKind
            && (*Object)->HasField(TEXT("id")))
        {
            return true;
        }
        for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : (*Object)->Values)
        {
            if (ValueContainsStableRefKind(Pair.Value, ExpectedKind)) return true;
        }
        return false;
    }
    const TArray<TSharedPtr<FJsonValue>>* Array = nullptr;
    if (Value->TryGetArray(Array) && Array != nullptr)
    {
        for (const TSharedPtr<FJsonValue>& Item : *Array)
        {
            if (ValueContainsStableRefKind(Item, ExpectedKind)) return true;
        }
    }
    return false;
}

bool PatchMentionsKind(const FSalPatch& Patch, const FString& ExpectedKind)
{
    for (const TSharedPtr<FJsonValue>& StatementValue : Patch.Statements)
    {
        if (ValueContainsStableRefKind(StatementValue, ExpectedKind)) return true;
    }
    return false;
}

bool IsWidgetQuery(const FString& Kind)
{
    return Kind == TEXT("summary") || Kind == TEXT("tree") || Kind == TEXT("widgets") || Kind == TEXT("widget");
}

bool IsPaletteQuery(const FString& Kind)
{
    return Kind == TEXT("palette_entries") || Kind == TEXT("palette");
}

bool IsWidgetPaletteId(const FString& Id)
{
    return Id.StartsWith(TEXT("widget.class:"))
        || Id.StartsWith(TEXT("widget.blueprint:"))
        || Id.StartsWith(TEXT("widget.image:"));
}

bool ResultHasError(const TSharedPtr<FJsonObject>& Result)
{
    if (!Result.IsValid()) return true;
    const TArray<TSharedPtr<FJsonValue>>* Diagnostics = nullptr;
    if (!Result->TryGetArrayField(TEXT("diagnostics"), Diagnostics) || Diagnostics == nullptr) return false;
    for (const TSharedPtr<FJsonValue>& Value : *Diagnostics)
    {
        const TSharedPtr<FJsonObject>* Diagnostic = nullptr;
        FString Severity;
        if (Value.IsValid()
            && Value->TryGetObject(Diagnostic)
            && Diagnostic != nullptr
            && (*Diagnostic)->TryGetStringField(TEXT("severity"), Severity)
            && Severity == TEXT("error"))
        {
            return true;
        }
    }
    return false;
}

FString ResultNextCursor(const TSharedPtr<FJsonObject>& Result)
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

struct FCombinedPaletteCursor
{
    bool bBlueprintDone = false;
    bool bWidgetDone = false;
    bool bWidgetTurn = false;
    FString BlueprintAfter;
    FString WidgetAfter;
};

void AppendCursorToken(FString& Out, const TCHAR Prefix, const FString& Text)
{
    Out += FString::Printf(TEXT("%c%d:%s;"), Prefix, Text.Len(), *Text);
}

void AppendCanonicalJson(FString& Out, const TSharedPtr<FJsonValue>& Value)
{
    if (!Value.IsValid() || Value->IsNull())
    {
        Out += TEXT("n;");
        return;
    }
    FString String;
    if (Value->TryGetString(String))
    {
        AppendCursorToken(Out, TEXT('s'), String);
        return;
    }
    bool bBoolean = false;
    if (Value->TryGetBool(bBoolean))
    {
        Out += bBoolean ? TEXT("b1;") : TEXT("b0;");
        return;
    }
    double Number = 0.0;
    if (Value->TryGetNumber(Number))
    {
        AppendCursorToken(Out, TEXT('d'), FString::Printf(TEXT("%.17g"), Number));
        return;
    }
    const TArray<TSharedPtr<FJsonValue>>* Array = nullptr;
    if (Value->TryGetArray(Array) && Array != nullptr)
    {
        Out += TEXT("a[");
        for (const TSharedPtr<FJsonValue>& Item : *Array) AppendCanonicalJson(Out, Item);
        Out += TEXT("];");
        return;
    }
    const TSharedPtr<FJsonObject>* Object = nullptr;
    if (Value->TryGetObject(Object) && Object != nullptr && (*Object).IsValid())
    {
        TArray<FString> Keys;
        (*Object)->Values.GetKeys(Keys);
        Keys.Sort();
        Out += TEXT("o{");
        for (const FString& Key : Keys)
        {
            AppendCursorToken(Out, TEXT('k'), Key);
            AppendCanonicalJson(Out, (*Object)->Values.FindRef(Key));
        }
        Out += TEXT("};");
        return;
    }
    Out += TEXT("u;");
}

FString CombinedPaletteFingerprint(const FSalQuery& Query, const FSalResolvedTarget& Target)
{
    FString Canonical;
    AppendCursorToken(Canonical, TEXT('a'), Target.AssetPath);
    AppendCursorToken(Canonical, TEXT('i'), Target.Id);
    AppendCanonicalJson(Canonical, MakeShared<FJsonValueObject>(Query.Operation));
    if (Query.Where.IsValid())
    {
        AppendCanonicalJson(Canonical, MakeShared<FJsonValueObject>(Query.Where));
    }
    else
    {
        AppendCanonicalJson(Canonical, MakeShared<FJsonValueNull>());
    }
    TArray<TSharedPtr<FJsonValue>> Details;
    for (const FString& Detail : Query.With) Details.Add(MakeShared<FJsonValueString>(Detail));
    AppendCanonicalJson(Canonical, MakeShared<FJsonValueArray>(Details));
    TArray<TSharedPtr<FJsonValue>> Order;
    for (const TSharedPtr<FJsonObject>& Entry : Query.OrderBy)
    {
        Order.Add(MakeShared<FJsonValueObject>(Entry));
    }
    AppendCanonicalJson(Canonical, MakeShared<FJsonValueArray>(Order));
    uint8 Digest[FSHA1::DigestSize];
    FSHA1::HashBuffer(*Canonical, Canonical.Len() * sizeof(TCHAR), Digest);
    return BytesToHex(Digest, UE_ARRAY_COUNT(Digest)).ToLower();
}

FString EncodeCursorPart(const FString& Value)
{
    return Value.IsEmpty() ? TEXT("-") : FBase64::Encode(Value, EBase64Mode::UrlSafe);
}

bool DecodeCursorPart(const FString& Encoded, FString& Out)
{
    Out.Reset();
    return Encoded == TEXT("-") || FBase64::Decode(Encoded, Out, EBase64Mode::UrlSafe);
}

FString EncodeCombinedPaletteCursor(
    const FCombinedPaletteCursor& Cursor,
    const FSalQuery& Query,
    const FSalResolvedTarget& Target)
{
    return FString::Printf(
        TEXT("sal_palette:1:%s:%d:%d:%d:%s:%s"),
        *CombinedPaletteFingerprint(Query, Target),
        Cursor.bBlueprintDone ? 1 : 0,
        Cursor.bWidgetDone ? 1 : 0,
        Cursor.bWidgetTurn ? 1 : 0,
        *EncodeCursorPart(Cursor.BlueprintAfter),
        *EncodeCursorPart(Cursor.WidgetAfter));
}

bool DecodeCombinedPaletteCursor(
    const FString& Encoded,
    const FSalQuery& Query,
    const FSalResolvedTarget& Target,
    FCombinedPaletteCursor& Out)
{
    Out = FCombinedPaletteCursor();
    if (Encoded.IsEmpty()) return true;
    TArray<FString> Parts;
    Encoded.ParseIntoArray(Parts, TEXT(":"), false);
    if (Parts.Num() != 8
        || Parts[0] != TEXT("sal_palette")
        || Parts[1] != TEXT("1")
        || Parts[2] != CombinedPaletteFingerprint(Query, Target)
        || (Parts[3] != TEXT("0") && Parts[3] != TEXT("1"))
        || (Parts[4] != TEXT("0") && Parts[4] != TEXT("1"))
        || (Parts[5] != TEXT("0") && Parts[5] != TEXT("1"))
        || !DecodeCursorPart(Parts[6], Out.BlueprintAfter)
        || !DecodeCursorPart(Parts[7], Out.WidgetAfter))
    {
        return false;
    }
    Out.bBlueprintDone = Parts[3] == TEXT("1");
    Out.bWidgetDone = Parts[4] == TEXT("1");
    Out.bWidgetTurn = Parts[5] == TEXT("1");
    return !(Out.bBlueprintDone && !Out.BlueprintAfter.IsEmpty())
        && !(Out.bWidgetDone && !Out.WidgetAfter.IsEmpty())
        && !(Out.bBlueprintDone && Out.bWidgetDone);
}

TSharedPtr<FJsonObject> InvalidCombinedPaletteCursor(const FSalQuery& Query)
{
    return FSalDiagnostics::Result(
        FSalDiagnostics::Error(
            TEXT("validation.invalid_cursor"),
            TEXT("The combined Palette cursor is malformed or no longer belongs to an active page."))
            .Interface(TEXT("blueprint"))
            .Operation(TEXT("palette_entries"))
            .Ref(Query.PageAfter)
            .Suggestion(TEXT("Restart the same Palette query without page after."))
            .Build());
}

void RenameLocalRefs(const TSharedPtr<FJsonValue>& Value, const TMap<FString, FString>& Renames)
{
    if (!Value.IsValid()) return;
    const TSharedPtr<FJsonObject>* Object = nullptr;
    if (Value->TryGetObject(Object) && Object != nullptr && (*Object).IsValid())
    {
        FString Kind;
        FString Name;
        if ((*Object)->TryGetStringField(TEXT("kind"), Kind)
            && Kind == TEXT("local")
            && (*Object)->TryGetStringField(TEXT("name"), Name))
        {
            if (const FString* Replacement = Renames.Find(Name)) (*Object)->SetStringField(TEXT("name"), *Replacement);
        }
        for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : (*Object)->Values) RenameLocalRefs(Pair.Value, Renames);
        return;
    }
    const TArray<TSharedPtr<FJsonValue>>* Array = nullptr;
    if (Value->TryGetArray(Array) && Array != nullptr)
    {
        for (const TSharedPtr<FJsonValue>& Item : *Array) RenameLocalRefs(Item, Renames);
    }
}

bool IsBindingStatement(const TSharedPtr<FJsonValue>& Value)
{
    const TSharedPtr<FJsonObject>* Object = nullptr;
    return Value.IsValid()
        && Value->TryGetObject(Object)
        && Object != nullptr
        && (*Object)->HasField(TEXT("target"))
        && (*Object)->HasField(TEXT("value"))
        && !(*Object)->HasField(TEXT("kind"));
}

int32 ResultBindingCount(const TSharedPtr<FJsonObject>& Result)
{
    const TSharedPtr<FJsonObject>* Object = nullptr;
    const TArray<TSharedPtr<FJsonValue>>* Statements = nullptr;
    if (!Result.IsValid()
        || !Result->TryGetObjectField(TEXT("object"), Object)
        || Object == nullptr
        || !(*Object)->TryGetArrayField(TEXT("statements"), Statements)
        || Statements == nullptr)
    {
        return 0;
    }
    int32 Count = 0;
    for (const TSharedPtr<FJsonValue>& Statement : *Statements)
    {
        if (IsBindingStatement(Statement)) ++Count;
    }
    return Count;
}

bool IsNoMatchesComment(const TSharedPtr<FJsonValue>& Value)
{
    const TSharedPtr<FJsonObject>* Object = nullptr;
    FString Kind;
    FString Comment;
    return Value.IsValid()
        && Value->TryGetObject(Object)
        && Object != nullptr
        && (*Object)->TryGetStringField(TEXT("kind"), Kind)
        && Kind == TEXT("comment")
        && (*Object)->TryGetStringField(TEXT("text"), Comment)
        && Comment == TEXT("no matches");
}

TSharedPtr<FJsonObject> MergePaletteResults(const TSharedPtr<FJsonObject>& BlueprintResult, const TSharedPtr<FJsonObject>& WidgetResult)
{
    if (!BlueprintResult.IsValid()) return WidgetResult;
    if (!WidgetResult.IsValid()) return BlueprintResult;
    const TSharedPtr<FJsonObject>* BlueprintObject = nullptr;
    const TSharedPtr<FJsonObject>* WidgetObject = nullptr;
    const TArray<TSharedPtr<FJsonValue>>* BlueprintStatements = nullptr;
    const TArray<TSharedPtr<FJsonValue>>* WidgetStatements = nullptr;
    if (!BlueprintResult->TryGetObjectField(TEXT("object"), BlueprintObject)
        || BlueprintObject == nullptr
        || !(*BlueprintObject)->TryGetArrayField(TEXT("statements"), BlueprintStatements)
        || BlueprintStatements == nullptr)
    {
        return WidgetResult;
    }
    if (!WidgetResult->TryGetObjectField(TEXT("object"), WidgetObject)
        || WidgetObject == nullptr
        || !(*WidgetObject)->TryGetArrayField(TEXT("statements"), WidgetStatements)
        || WidgetStatements == nullptr)
    {
        return BlueprintResult;
    }

    TSet<FString> Used;
    for (const TSharedPtr<FJsonValue>& StatementValue : *BlueprintStatements)
    {
        const TSharedPtr<FJsonObject>* Statement = nullptr;
        const TSharedPtr<FJsonObject>* Target = nullptr;
        FString Kind;
        FString Name;
        if (StatementValue->TryGetObject(Statement) && Statement != nullptr
            && (*Statement)->TryGetObjectField(TEXT("target"), Target) && Target != nullptr
            && (*Target)->TryGetStringField(TEXT("kind"), Kind) && Kind == TEXT("local")
            && (*Target)->TryGetStringField(TEXT("name"), Name)) Used.Add(Name);
    }
    TMap<FString, FString> Renames;
    for (const TSharedPtr<FJsonValue>& StatementValue : *WidgetStatements)
    {
        const TSharedPtr<FJsonObject>* Statement = nullptr;
        const TSharedPtr<FJsonObject>* Target = nullptr;
        FString Kind;
        FString Name;
        if (!StatementValue->TryGetObject(Statement) || Statement == nullptr
            || !(*Statement)->TryGetObjectField(TEXT("target"), Target) || Target == nullptr
            || !(*Target)->TryGetStringField(TEXT("kind"), Kind) || Kind != TEXT("local")
            || !(*Target)->TryGetStringField(TEXT("name"), Name)) continue;
        FString Unique = Name;
        int32 Suffix = 2;
        while (Used.Contains(Unique)) Unique = FString::Printf(TEXT("widget_%s_%d"), *Name, Suffix++);
        Used.Add(Unique);
        if (Unique != Name) Renames.Add(Name, Unique);
    }
    for (const TSharedPtr<FJsonValue>& StatementValue : *WidgetStatements) RenameLocalRefs(StatementValue, Renames);

    bool bHasBinding = false;
    for (const TSharedPtr<FJsonValue>& Statement : *BlueprintStatements) bHasBinding |= IsBindingStatement(Statement);
    for (const TSharedPtr<FJsonValue>& Statement : *WidgetStatements) bHasBinding |= IsBindingStatement(Statement);
    TArray<TSharedPtr<FJsonValue>> Statements;
    Statements.Reserve(BlueprintStatements->Num() + WidgetStatements->Num());
    for (const TSharedPtr<FJsonValue>& Statement : *BlueprintStatements)
    {
        if (!bHasBinding || !IsNoMatchesComment(Statement)) Statements.Add(Statement);
    }
    for (const TSharedPtr<FJsonValue>& Statement : *WidgetStatements)
    {
        if (!bHasBinding || !IsNoMatchesComment(Statement)) Statements.Add(Statement);
    }
    TSharedPtr<FJsonObject> Object = MakeShared<FJsonObject>();
    Object->SetArrayField(TEXT("statements"), Statements);
    TArray<TSharedPtr<FJsonValue>> Diagnostics;
    const TArray<TSharedPtr<FJsonValue>>* SourceDiagnostics = nullptr;
    if (BlueprintResult->TryGetArrayField(TEXT("diagnostics"), SourceDiagnostics) && SourceDiagnostics != nullptr) Diagnostics.Append(*SourceDiagnostics);
    if (WidgetResult->TryGetArrayField(TEXT("diagnostics"), SourceDiagnostics) && SourceDiagnostics != nullptr) Diagnostics.Append(*SourceDiagnostics);
    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetObjectField(TEXT("object"), Object);
    Result->SetArrayField(TEXT("diagnostics"), Diagnostics);
    return Result;
}

TSharedPtr<FJsonObject> QueryWidgetBlueprintPalette(
    const FSalQuery& Query,
    const FSalResolvedTarget& Target)
{
    const FString Kind = OperationKind(Query.Operation);
    if (Kind == TEXT("palette"))
    {
        FString Id;
        Query.Operation->TryGetStringField(TEXT("id"), Id);
        return IsWidgetPaletteId(Id)
            ? FSalWidgetInterface::Query(Query, Target)
            : FSalBlueprintInterface::Query(Query, Target);
    }

    FCombinedPaletteCursor Cursor;
    if (!DecodeCombinedPaletteCursor(Query.PageAfter, Query, Target, Cursor))
    {
        return InvalidCombinedPaletteCursor(Query);
    }

    const int32 Limit = FMath::Clamp(Query.PageLimit > 0 ? Query.PageLimit : 50, 1, 200);
    int32 BlueprintLimit = 0;
    int32 WidgetLimit = 0;
    if (!Cursor.bBlueprintDone && !Cursor.bWidgetDone)
    {
        if (Limit == 1)
        {
            Cursor.bWidgetTurn ? WidgetLimit = 1 : BlueprintLimit = 1;
        }
        else
        {
            BlueprintLimit = (Limit + 1) / 2;
            WidgetLimit = Limit - BlueprintLimit;
        }
    }
    else if (!Cursor.bBlueprintDone)
    {
        BlueprintLimit = Limit;
    }
    else if (!Cursor.bWidgetDone)
    {
        WidgetLimit = Limit;
    }

    TSharedPtr<FJsonObject> BlueprintResult;
    TSharedPtr<FJsonObject> WidgetResult;
    if (BlueprintLimit > 0)
    {
        FSalQuery SideQuery = Query;
        SideQuery.PageLimit = BlueprintLimit;
        SideQuery.PageAfter = Cursor.BlueprintAfter;
        BlueprintResult = FSalBlueprintInterface::Query(SideQuery, Target);
        if (ResultHasError(BlueprintResult)) return BlueprintResult;
        Cursor.BlueprintAfter = ResultNextCursor(BlueprintResult);
        Cursor.bBlueprintDone = Cursor.BlueprintAfter.IsEmpty();
        if (Cursor.bBlueprintDone && !Cursor.bWidgetDone)
        {
            WidgetLimit += FMath::Max(0, BlueprintLimit - ResultBindingCount(BlueprintResult));
        }
    }
    if (WidgetLimit > 0)
    {
        FSalQuery SideQuery = Query;
        SideQuery.PageLimit = WidgetLimit;
        SideQuery.PageAfter = Cursor.WidgetAfter;
        WidgetResult = FSalWidgetInterface::Query(SideQuery, Target);
        if (ResultHasError(WidgetResult)) return WidgetResult;
        Cursor.WidgetAfter = ResultNextCursor(WidgetResult);
        Cursor.bWidgetDone = Cursor.WidgetAfter.IsEmpty();
    }
    if (Cursor.bWidgetDone
        && !Cursor.bBlueprintDone
        && !BlueprintResult.IsValid()
        && WidgetLimit > ResultBindingCount(WidgetResult))
    {
        FSalQuery SideQuery = Query;
        SideQuery.PageLimit = WidgetLimit - ResultBindingCount(WidgetResult);
        SideQuery.PageAfter = Cursor.BlueprintAfter;
        BlueprintResult = FSalBlueprintInterface::Query(SideQuery, Target);
        if (ResultHasError(BlueprintResult)) return BlueprintResult;
        Cursor.BlueprintAfter = ResultNextCursor(BlueprintResult);
        Cursor.bBlueprintDone = Cursor.BlueprintAfter.IsEmpty();
    }
    if (Limit == 1 && !Cursor.bBlueprintDone && !Cursor.bWidgetDone)
    {
        Cursor.bWidgetTurn = !Cursor.bWidgetTurn;
    }
    else if (Cursor.bBlueprintDone)
    {
        Cursor.bWidgetTurn = true;
    }
    else if (Cursor.bWidgetDone)
    {
        Cursor.bWidgetTurn = false;
    }

    TSharedPtr<FJsonObject> Result = MergePaletteResults(BlueprintResult, WidgetResult);
    if (!Result.IsValid())
    {
        Result = MakeShared<FJsonObject>();
        Result->SetArrayField(TEXT("diagnostics"), TArray<TSharedPtr<FJsonValue>>());
    }
    Result->RemoveField(TEXT("page"));
    if (!Cursor.bBlueprintDone || !Cursor.bWidgetDone)
    {
        TSharedPtr<FJsonObject> Page = MakeShared<FJsonObject>();
        Page->SetStringField(TEXT("next"), EncodeCombinedPaletteCursor(Cursor, Query, Target));
        Result->SetObjectField(TEXT("page"), Page);
    }
    return Result;
}

bool PatchUsesWidget(const FSalPatch& Patch)
{
    for (const TSharedPtr<FJsonValue>& StatementValue : Patch.Statements)
    {
        const TSharedPtr<FJsonObject>* Statement = nullptr;
        if (!StatementValue.IsValid() || !StatementValue->TryGetObject(Statement) || Statement == nullptr) continue;
        FString Kind;
        (*Statement)->TryGetStringField(TEXT("kind"), Kind);
        if (Kind == TEXT("wrap") || Kind == TEXT("replace")) return true;
        const TSharedPtr<FJsonObject>* Value = nullptr;
        FString Callee;
        if ((*Statement)->TryGetObjectField(TEXT("value"), Value) && Value != nullptr
            && (*Value)->TryGetStringField(TEXT("callee"), Callee) && Callee == TEXT("widget")) return true;
    }
    return PatchMentionsKind(Patch, TEXT("widget"));
}

TSharedPtr<FJsonObject> DispatchQuery(const FSalQuery& Query, const FSalResolvedTarget& Target)
{
    if (OperationKind(Query.Operation) == TEXT("references"))
    {
        if (Target.Kind == ESalTargetKind::Asset
            && Target.HasInterface(FName(TEXT("state_tree"))))
        {
            return FSalStateTreeInterface::Query(Query, Target);
        }
        return FSalReferenceInterface::Query(Query, Target);
    }
    switch (Target.Kind)
    {
    case ESalTargetKind::AssetRoot:
        return FSalAssetInterface::Query(Query, Target);
    case ESalTargetKind::Asset:
        if (Target.HasInterface(FName(TEXT("state_tree"))))
        {
            return FSalStateTreeInterface::Query(Query, Target);
        }
        return FSalAssetInterface::Query(Query, Target);
    case ESalTargetKind::Blueprint:
        if (Target.HasInterface(FName(TEXT("widget"))))
        {
            const FString Kind = OperationKind(Query.Operation);
            if (IsPaletteQuery(Kind))
            {
                return QueryWidgetBlueprintPalette(Query, Target);
            }
            if (IsWidgetQuery(Kind)) return FSalWidgetInterface::Query(Query, Target);
        }
        return FSalBlueprintInterface::Query(Query, Target);
    case ESalTargetKind::Class:
        return FSalClassInterface::Query(Query, Target);
    case ESalTargetKind::Graph:
        return FSalGraphInterface::Query(Query, Target);
    default:
        return InterfaceError(OperationKind(Query.Operation), Target);
    }
}

TSharedPtr<FJsonObject> DispatchPatch(const FSalPatch& Patch, const FSalResolvedTarget& Target)
{
    switch (Target.Kind)
    {
    case ESalTargetKind::Asset:
        if (Target.HasInterface(FName(TEXT("state_tree"))))
        {
            return FSalStateTreeInterface::Patch(Patch, Target);
        }
        return FSalAssetInterface::Patch(Patch, Target);
    case ESalTargetKind::Blueprint:
        if (Target.HasInterface(FName(TEXT("widget"))) && PatchUsesWidget(Patch))
        {
            return FSalWidgetInterface::Patch(Patch, Target);
        }
        return FSalBlueprintInterface::Patch(Patch, Target);
    case ESalTargetKind::Class:
        return FSalClassInterface::Patch(Patch, Target);
    case ESalTargetKind::Graph:
        return FSalGraphInterface::Patch(Patch, Target);
    default:
        return InterfaceError(PatchMentionsKind(Patch, TEXT("widget")) ? TEXT("widget patch") : TEXT("patch"), Target);
    }
}
}

TSharedPtr<FJsonObject> FSalModule::BuildQueryResult(const TSharedPtr<FJsonObject>& Arguments)
{
    FSalQuery Query;
    const auto FinalizeQueryResult = [&Query](const TSharedPtr<FJsonObject>& Result)
    {
        return ValidateOutgoing(EnforceQueryResultSize(ValidateOutgoing(Result), Query));
    };
    TSharedPtr<FJsonObject> Error;
    if (!FSalJson::DecodeQuery(Arguments, Query, Error))
    {
        return FinalizeQueryResult(Error);
    }
    FSalResolvedTarget Target;
    if (!FSalTargetResolver().Resolve(Query.Alias, Query.TargetValue, false, Target, Error))
    {
        return FinalizeQueryResult(Error);
    }
    return FinalizeQueryResult(DispatchQuery(Query, Target));
}

TSharedPtr<FJsonObject> FSalModule::BuildPatchResult(const TSharedPtr<FJsonObject>& Arguments)
{
    FSalPatch Patch;
    TSharedPtr<FJsonObject> Error;
    if (!FSalJson::DecodePatch(Arguments, Patch, Error))
    {
        return ValidateOutgoing(MutationFailure(Error, RequestedDryRun(Arguments)));
    }
    FSalResolvedTarget Target;
    if (!FSalTargetResolver().Resolve(Patch.Alias, Patch.TargetValue, true, Target, Error))
    {
        return ValidateOutgoing(MutationFailure(Error, Patch.bDryRun));
    }
    TSharedPtr<FJsonObject> Result = DispatchPatch(Patch, Target);
    if (!Result.IsValid() || !Result->HasField(TEXT("isError")))
    {
        Result = MutationFailure(Result, Patch.bDryRun, Target.AssetPath);
    }
    TSharedPtr<FJsonObject> Validated = ValidateOutgoing(Result);
    if (!Validated.IsValid() || !Validated->HasField(TEXT("isError")))
    {
        Validated = ValidateOutgoing(MutationFailure(Validated, Patch.bDryRun, Target.AssetPath));
    }
    return Validated;
}
}
