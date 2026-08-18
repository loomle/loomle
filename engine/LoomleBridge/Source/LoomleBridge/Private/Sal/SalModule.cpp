// Copyright 2026 Loomle contributors.

#include "SalModule.h"

#include "Asset/SalAssetInterface.h"
#include "Blueprint/SalBlueprintInterface.h"
#include "Class/SalClassInterface.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Graph/SalGraphInterface.h"
#include "Level/SalLevelInterface.h"
#include "Misc/Base64.h"
#include "Misc/SecureHash.h"
#include "PCG/SalPCGInterface.h"
#include "PCG/SalPCGComponentInterface.h"
#include "Reference/SalReferenceInterface.h"
#include "Serialization/JsonSerializer.h"
#include "SalDiagnostics.h"
#include "SalJson.h"
#include "SalModel.h"
#include "SalObjectBuilder.h"
#include "SalResultTargets.h"
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
            FString::Printf(TEXT("The active Target Domain does not accept operation %s."), *Operation))
            .Operation(Operation)
            .Supported(Interfaces)
            .Suggestion(TEXT("Query the exact object with schema or call sal_schema({})."))
            .Build());
}

TSharedPtr<FJsonObject> ReferenceUnavailable(
    const FSalResolvedTarget& Target,
    const FString& Message)
{
    FSalDiagnosticBuilder Diagnostic = FSalDiagnostics::Error(
            TEXT("capability.reference_unavailable"),
            Message)
        .Operation(TEXT("references"));
    if (!Target.Interfaces.IsEmpty())
    {
        Diagnostic.Interface(Target.Interfaces[0].ToString());
    }
    return FSalDiagnostics::Result(Diagnostic.Build());
}

TSharedPtr<FJsonObject> ValidateOutgoing(const TSharedPtr<FJsonObject>& Result)
{
    TSharedPtr<FJsonObject> ValidationError;
    if (!FSalJson::ValidateResult(Result, ValidationError))
    {
        if (ValidationError.IsValid())
        {
            FString Context;
            if (Result.IsValid()
                && Result->TryGetStringField(TEXT("targetContext"), Context))
            {
                ValidationError->SetStringField(TEXT("targetContext"), Context);
                for (const TCHAR* Field : {
                         TEXT("target"),
                         TEXT("relatedTargets"),
                         TEXT("handoffs")})
                {
                    if (const TSharedPtr<FJsonValue> Value = Result->TryGetField(Field);
                        Value.IsValid())
                    {
                        ValidationError->SetField(Field, Value);
                    }
                }
            }
            else
            {
                ValidationError->SetStringField(
                    TEXT("targetContext"),
                    TEXT("unresolved_target"));
            }
        }
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

bool IsValidQueryResultWithinSizeLimit(
    const TSharedPtr<FJsonObject>& Result)
{
    const TOptional<int64> SizeBytes = CondensedJsonUtf8Size(Result);
    if (!SizeBytes.IsSet()
        || SizeBytes.GetValue() > MaxQueryResultUtf8Bytes)
    {
        return false;
    }

    TSharedPtr<FJsonObject> ValidationError;
    return FSalJson::ValidateResult(Result, ValidationError);
}

TSharedPtr<FJsonObject> BoundQueryErrorContext(
    const TSharedPtr<FJsonObject>& Error,
    const TSharedPtr<FJsonObject>& SourceResult)
{
    Error->SetStringField(TEXT("targetContext"), TEXT("unresolved_target"));
    if (SourceResult.IsValid())
    {
        for (const TCHAR* Field : {
                 TEXT("targetContext"),
                 TEXT("target"),
                 TEXT("relatedTargets"),
                 TEXT("handoffs")})
        {
            if (const TSharedPtr<FJsonValue> Value = SourceResult->TryGetField(Field);
                Value.IsValid())
            {
                Error->SetField(Field, Value);
            }
        }
    }

    if (IsValidQueryResultWithinSizeLimit(Error))
    {
        return Error;
    }

    // Related Targets and handoffs are useful context only when the complete
    // table is both contract-valid and bounded. Keep the primary Target when
    // possible, but never let retained navigation context defeat the hard
    // result-size gate.
    Error->RemoveField(TEXT("relatedTargets"));
    Error->RemoveField(TEXT("handoffs"));
    if (IsValidQueryResultWithinSizeLimit(Error))
    {
        return Error;
    }

    // A malformed or pathologically large primary Target must not escape the
    // same limit. An unresolved diagnostic is the smallest valid Query result.
    Error->RemoveField(TEXT("target"));
    Error->SetStringField(TEXT("targetContext"), TEXT("unresolved_target"));
    if (IsValidQueryResultWithinSizeLimit(Error))
    {
        return Error;
    }

    // This branch is defensive: current diagnostics are fixed and small, but
    // the hard limit must remain true even if a future caller supplies an
    // unexpectedly large diagnostic payload.
    TSharedPtr<FJsonObject> Minimal = FSalDiagnostics::Result(
        FSalDiagnostics::Error(
            TEXT("validation.result_too_large"),
            TEXT("Query result exceeded the 131072-byte safety limit."))
        .Suggestion(TEXT("Narrow the Query and retry."))
        .Build());
    Minimal->SetStringField(TEXT("targetContext"), TEXT("unresolved_target"));
    return Minimal;
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
        return BoundQueryErrorContext(
            FSalDiagnostics::Result(Diagnostic.Build()),
            Result);
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
    return BoundQueryErrorContext(
        FSalDiagnostics::Result(Diagnostic.Build()),
        Result);
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

bool ReadIdentityPath(
    const TSharedPtr<FJsonObject>& Ref,
    TArray<FString>& OutPath)
{
    OutPath.Reset();
    FString Kind;
    const TArray<TSharedPtr<FJsonValue>>* Segments = nullptr;
    if (!Ref.IsValid()
        || !Ref->TryGetStringField(TEXT("kind"), Kind)
        || Kind != TEXT("stable_ref")
        || !Ref->TryGetArrayField(TEXT("identityPath"), Segments)
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
            return false;
        }
        OutPath.Add(Segment);
    }
    return true;
}

TSharedPtr<FJsonObject> StableReferenceError(
    const FString& Code,
    const FString& Message,
    const FSalResolvedTarget& Target)
{
    return FSalDiagnostics::Result(
        FSalDiagnostics::Error(Code, Message)
            .Interface(Target.Interfaces.IsEmpty()
                ? FString()
                : Target.Interfaces[0].ToString())
            .Build());
}

bool LowerStableReference(
    const TSharedPtr<FJsonObject>& Ref,
    const FSalResolvedTarget& Target,
    TSharedPtr<FJsonObject>& OutError)
{
    TArray<FString> IdentityPath;
    if (!ReadIdentityPath(Ref, IdentityPath))
    {
        OutError = StableReferenceError(
            TEXT("validation.invalid_reference"),
            TEXT("Stable reference has an invalid identityPath."),
            Target);
        return false;
    }
    if (Target.Domain == ESalDomain::Pcg
        || Target.Domain == ESalDomain::Level
        || Target.Domain == ESalDomain::PcgComponent)
    {
        FString Code;
        FString Message;
        bool bResolved = false;
        if (Target.Domain == ESalDomain::Pcg)
        {
            bResolved = FSalPCGInterface::LowerStableReference(
                Target, IdentityPath, Ref, Code, Message);
        }
        else if (Target.Domain == ESalDomain::Level)
        {
            bResolved = FSalLevelInterface::LowerStableReference(
                Target, IdentityPath, Ref, Code, Message);
        }
        else
        {
            bResolved = FSalPCGComponentInterface::LowerStableReference(
                Target, IdentityPath, Ref, Code, Message);
        }
        if (bResolved)
        {
            return true;
        }
        OutError = StableReferenceError(
            Code.IsEmpty() ? TEXT("resolution.object_not_found") : Code,
            Message.IsEmpty()
                ? TEXT("Stable reference could not be resolved in the active structured-identity Target.")
                : Message,
            Target);
        return false;
    }
    FString LegacyKind;
    FString LegacyId;
    FString Code;
    FString Message;
    bool bResolved = false;
    switch (Target.Domain)
    {
    case ESalDomain::Blueprint:
        bResolved = FSalBlueprintInterface::LowerStableReference(
            Target,
            IdentityPath,
            LegacyKind,
            LegacyId,
            Code,
            Message);
        break;
    case ESalDomain::Graph:
        bResolved = FSalGraphInterface::LowerStableReference(
            Target,
            IdentityPath,
            LegacyKind,
            LegacyId,
            Code,
            Message);
        break;
    case ESalDomain::StateTree:
        bResolved = FSalStateTreeInterface::LowerStableReference(
            Target,
            IdentityPath,
            LegacyKind,
            LegacyId,
            Code,
            Message);
        break;
    case ESalDomain::Widget:
        bResolved = FSalWidgetInterface::LowerStableReference(
            Target,
            IdentityPath,
            LegacyKind,
            LegacyId,
            Code,
            Message);
        break;
    case ESalDomain::Asset:
        bResolved = FSalAssetInterface::LowerStableReference(
            Target,
            IdentityPath,
            LegacyKind,
            LegacyId,
            Code,
            Message);
        break;
    case ESalDomain::Class:
        bResolved = FSalClassInterface::LowerStableReference(
            Target,
            IdentityPath,
            LegacyKind,
            LegacyId,
            Code,
            Message);
        break;
    case ESalDomain::PcgComponent:
        // Structured-identity Domains are handled before the legacy switch.
        checkNoEntry();
        break;
    default:
        Code = TEXT("capability.interface_unavailable");
        Message = TEXT("The active Domain has no registered StableRef identity contract.");
        break;
    }
    if (!bResolved)
    {
        OutError = StableReferenceError(
            Code.IsEmpty() ? TEXT("resolution.object_not_found") : Code,
            Message.IsEmpty()
                ? TEXT("Stable reference could not be resolved in the active Target.")
                : Message,
            Target);
        return false;
    }
    Ref->Values.Reset();
    Ref->SetStringField(TEXT("kind"), LegacyKind);
    Ref->SetStringField(TEXT("id"), LegacyId);
    return true;
}

bool LowerRelationshipSubject(
    const TSharedPtr<FJsonObject>& Subject,
    const FSalResolvedTarget& Target,
    TSharedPtr<FJsonObject>& OutError)
{
    FString Kind;
    if (!Subject.IsValid()
        || !Subject->TryGetStringField(TEXT("kind"), Kind))
    {
        OutError = StableReferenceError(
            TEXT("validation.invalid_reference"),
            TEXT("Relationship subject has no normalized kind."),
            Target);
        return false;
    }
    if (Kind == TEXT("target_self"))
    {
        // TargetSelf is structural and remains intact for the already selected
        // Domain provider. It must never be fabricated as a contained
        // StableRef.
        return true;
    }
    if (Kind == TEXT("stable_ref"))
    {
        return LowerStableReference(Subject, Target, OutError);
    }
    if (Kind == TEXT("member"))
    {
        const TSharedPtr<FJsonObject>* Owner = nullptr;
        return Subject->TryGetObjectField(TEXT("object"), Owner)
            && Owner != nullptr
            && LowerRelationshipSubject(*Owner, Target, OutError);
    }
    OutError = StableReferenceError(
        TEXT("validation.invalid_reference"),
        TEXT("Relationship subject must be TargetSelf, a StableRef, or one of their members."),
        Target);
    return false;
}

bool LowerExpression(
    const TSharedPtr<FJsonValue>& Value,
    const FSalResolvedTarget& Target,
    const bool bCreationBinding,
    TSharedPtr<FJsonObject>& OutError)
{
    if (!Value.IsValid() || Value->IsNull())
    {
        return true;
    }
    FString String;
    double Number = 0.0;
    bool Boolean = false;
    if (Value->TryGetString(String)
        || Value->TryGetNumber(Number)
        || Value->TryGetBool(Boolean))
    {
        return true;
    }
    const TArray<TSharedPtr<FJsonValue>>* Array = nullptr;
    if (Value->TryGetArray(Array) && Array != nullptr)
    {
        for (const TSharedPtr<FJsonValue>& Item : *Array)
        {
            if (!LowerExpression(Item, Target, false, OutError))
            {
                return false;
            }
        }
        return true;
    }
    const TSharedPtr<FJsonObject>* ObjectPointer = nullptr;
    if (!Value->TryGetObject(ObjectPointer)
        || ObjectPointer == nullptr
        || !(*ObjectPointer).IsValid())
    {
        return true;
    }
    const TSharedPtr<FJsonObject> Object = *ObjectPointer;
    FString Kind;
    Object->TryGetStringField(TEXT("kind"), Kind);
    if (Kind == TEXT("stable_ref"))
    {
        return LowerStableReference(Object, Target, OutError);
    }
    if (Kind == TEXT("member"))
    {
        const TSharedPtr<FJsonObject>* Owner = nullptr;
        return Object->TryGetObjectField(TEXT("object"), Owner)
            && Owner != nullptr
            && LowerExpression(
                MakeShared<FJsonValueObject>(*Owner),
                Target,
                false,
                OutError);
    }
    if (Kind == TEXT("object"))
    {
        const TSharedPtr<FJsonObject>* FieldsPointer = nullptr;
        if (!Object->TryGetObjectField(TEXT("fields"), FieldsPointer)
            || FieldsPointer == nullptr
            || !(*FieldsPointer).IsValid())
        {
            OutError = StableReferenceError(
                TEXT("language.invalid_object_shape"),
                TEXT("Object expression has no fields object."),
                Target);
            return false;
        }
        const TSharedPtr<FJsonObject> Fields = *FieldsPointer;
        for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : Fields->Values)
        {
            if (!LowerExpression(Pair.Value, Target, false, OutError))
            {
                return false;
            }
        }
        if (bCreationBinding
            && Fields->HasField(TEXT("palette")))
        {
            FString Palette;
            FString LegacyCreationKind;
            bool bMapped = Fields->TryGetStringField(
                    TEXT("palette"),
                    Palette)
                && !Palette.IsEmpty();
            if (bMapped)
            {
                switch (Target.Domain)
                {
                case ESalDomain::Blueprint:
                    bMapped =
                        FSalBlueprintInterface::ResolveCreationKind(
                            Palette,
                            LegacyCreationKind);
                    break;
                case ESalDomain::Graph:
                    LegacyCreationKind = TEXT("node");
                    break;
                case ESalDomain::StateTree:
                    bMapped =
                        FSalStateTreeInterface::ResolveCreationKind(
                            Palette,
                            LegacyCreationKind);
                    break;
                case ESalDomain::Widget:
                    LegacyCreationKind = TEXT("widget");
                    break;
                case ESalDomain::Level:
                    bMapped =
                        FSalLevelInterface::ResolveCreationKind(
                            Palette,
                            LegacyCreationKind);
                    break;
                case ESalDomain::Pcg:
                    LegacyCreationKind =
                        Palette.StartsWith(TEXT("pcg.node."))
                            ? TEXT("node")
                            : FString();
                    bMapped = !LegacyCreationKind.IsEmpty();
                    break;
                default:
                    bMapped = false;
                    break;
                }
            }
            if (!bMapped || LegacyCreationKind.IsEmpty())
            {
                OutError = StableReferenceError(
                    TEXT("validation.creation_invalid"),
                    FString::Printf(
                        TEXT("Palette identity is not owned by the active %s Domain: %s."),
                        Target.Interfaces.IsEmpty()
                            ? TEXT("unknown")
                            : *Target.Interfaces[0].ToString(),
                        *Palette),
                    Target);
                return false;
            }
            Object->Values.Reset();
            Object->SetStringField(TEXT("kind"), TEXT("call"));
            Object->SetStringField(TEXT("callee"), LegacyCreationKind);
            Object->SetObjectField(TEXT("args"), Fields);
        }
        // An ObjectExpr is a literal data boundary. Keep its normalized
        // wrapper unless this exact binding was authorized as a Domain
        // creation through a validated Palette identity above. Unwrapping
        // arbitrary fields here would let ordinary data such as
        // {kind: "call", callee: ..., args: ...} or {kind: ..., id: ...}
        // masquerade as the private executor AST.
        return true;
    }
    for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : Object->Values)
    {
        if (!LowerExpression(Pair.Value, Target, false, OutError))
        {
            return false;
        }
    }
    return true;
}

bool LowerQueryForDomain(
    FSalQuery& Query,
    const FSalResolvedTarget& Target,
    TSharedPtr<FJsonObject>& OutError)
{
    FString Kind = OperationKind(Query.Operation);
    if (Kind == TEXT("target"))
    {
        if (Target.Domain == ESalDomain::Blueprint
            || Target.Domain == ESalDomain::Graph)
        {
            Query.Operation->SetStringField(
                TEXT("kind"),
                Target.Domain == ESalDomain::Blueprint
                    ? TEXT("blueprint")
                    : TEXT("graph"));
            Query.Operation->SetStringField(TEXT("id"), Target.Id);
        }
        else if (Target.Domain == ESalDomain::Class
            || Target.Domain == ESalDomain::Widget)
        {
            Query.Operation->SetStringField(TEXT("kind"), TEXT("summary"));
        }
    }
    else if (Kind == TEXT("references"))
    {
        const TSharedPtr<FJsonObject>* Subject = nullptr;
        if (!Query.Operation->TryGetObjectField(
                TEXT("target"),
                Subject)
            || Subject == nullptr
            || !LowerRelationshipSubject(
                *Subject,
                Target,
                OutError))
        {
            return false;
        }
    }
    else if (Kind == TEXT("object"))
    {
        const TSharedPtr<FJsonObject>* RefPointer = nullptr;
        if (!Query.Operation->TryGetObjectField(TEXT("target"), RefPointer)
            || RefPointer == nullptr
            || !LowerStableReference(*RefPointer, Target, OutError))
        {
            return false;
        }
        // The Domain lowerer owns the private exact-operation shape. Legacy
        // Domains still produce {kind,id}; structured identity Domains such as
        // PCG retain owner, direction, and label as independent fields.
        Query.Operation = *RefPointer;
    }
    else if (!LowerExpression(
        MakeShared<FJsonValueObject>(Query.Operation),
        Target,
        false,
        OutError))
    {
        return false;
    }
    if (Query.Where.IsValid()
        && !LowerExpression(
            MakeShared<FJsonValueObject>(Query.Where),
            Target,
            false,
            OutError))
    {
        return false;
    }
    return true;
}

bool LowerPatchForDomain(
    FSalPatch& Patch,
    const FSalResolvedTarget& Target,
    TSharedPtr<FJsonObject>& OutError)
{
    for (const TSharedPtr<FJsonValue>& StatementValue : Patch.Statements)
    {
        const TSharedPtr<FJsonObject>* Statement = nullptr;
        if (!StatementValue.IsValid()
            || !StatementValue->TryGetObject(Statement)
            || Statement == nullptr)
        {
            continue;
        }
        const bool bBinding =
            (*Statement)->HasField(TEXT("target"))
            && (*Statement)->HasField(TEXT("value"))
            && !(*Statement)->HasField(TEXT("kind"));
        if (bBinding)
        {
            if (!LowerExpression(
                    (*Statement)->TryGetField(TEXT("value")),
                    Target,
                    true,
                    OutError))
            {
                return false;
            }
            continue;
        }
        if (!LowerExpression(
                StatementValue,
                Target,
                false,
                OutError))
        {
            return false;
        }
    }
    return true;
}

bool HasExactFields(
    const TSharedPtr<FJsonObject>& Object,
    std::initializer_list<const TCHAR*> Fields)
{
    if (!Object.IsValid()
        || Object->Values.Num() != static_cast<int32>(Fields.size()))
    {
        return false;
    }
    for (const TCHAR* Field : Fields)
    {
        if (!Object->HasField(Field))
        {
            return false;
        }
    }
    return true;
}

bool ConvertOutputReference(
    const TSharedPtr<FJsonObject>& Ref,
    const FSalResolvedTarget& Target)
{
    if (!Ref.IsValid())
    {
        return false;
    }
    FString Kind;
    Ref->TryGetStringField(TEXT("kind"), Kind);
    if (Kind == TEXT("local"))
    {
        FString Name;
        return HasExactFields(Ref, {TEXT("kind"), TEXT("name")})
            && Ref->TryGetStringField(TEXT("name"), Name)
            && !Name.IsEmpty();
    }
    if (Kind == TEXT("member"))
    {
        const TSharedPtr<FJsonObject>* Owner = nullptr;
        return Ref->TryGetObjectField(TEXT("object"), Owner)
            && Owner != nullptr
            && ConvertOutputReference(*Owner, Target);
    }
    if (Kind == TEXT("scoped_stable_ref"))
    {
        const TSharedPtr<FJsonObject>* Scope = nullptr;
        const TSharedPtr<FJsonObject>* Reference = nullptr;
        return Ref->TryGetObjectField(TEXT("target"), Scope)
            && Scope != nullptr
            && Ref->TryGetObjectField(TEXT("reference"), Reference)
            && Reference != nullptr
            && ConvertOutputReference(*Scope, Target)
            && ConvertOutputReference(*Reference, Target);
    }
    if (Kind == TEXT("stable_ref"))
    {
        return HasExactFields(
            Ref,
            {TEXT("kind"), TEXT("identityPath")})
            || HasExactFields(
                Ref,
                {TEXT("kind"), TEXT("identityPath"), TEXT("semanticTag")});
    }
    return false;
}

bool ConvertOutputExpression(
    const TSharedPtr<FJsonValue>& Value,
    const FSalResolvedTarget& Target)
{
    if (!Value.IsValid() || Value->IsNull())
    {
        return true;
    }
    FString String;
    double Number = 0.0;
    bool Boolean = false;
    if (Value->TryGetString(String)
        || Value->TryGetNumber(Number)
        || Value->TryGetBool(Boolean))
    {
        return true;
    }
    const TArray<TSharedPtr<FJsonValue>>* Array = nullptr;
    if (Value->TryGetArray(Array) && Array != nullptr)
    {
        for (const TSharedPtr<FJsonValue>& Item : *Array)
        {
            if (!ConvertOutputExpression(Item, Target))
            {
                return false;
            }
        }
        return true;
    }
    const TSharedPtr<FJsonObject>* ObjectPointer = nullptr;
    if (!Value->TryGetObject(ObjectPointer)
        || ObjectPointer == nullptr
        || !(*ObjectPointer).IsValid())
    {
        return false;
    }
    const TSharedPtr<FJsonObject> Object = *ObjectPointer;
    const bool bExplicitExpression =
        Value::IsExplicitExpression(Value);
    FString Kind;
    Object->TryGetStringField(TEXT("kind"), Kind);
    if (bExplicitExpression
        && Kind == TEXT("object")
        && (HasExactFields(Object, {TEXT("kind"), TEXT("fields")})
            || HasExactFields(
                Object,
                {TEXT("kind"), TEXT("fields"), TEXT("semanticTag")})))
    {
        const TSharedPtr<FJsonObject>* Fields = nullptr;
        if (Object->TryGetObjectField(TEXT("fields"), Fields)
            && Fields != nullptr)
        {
            for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : (*Fields)->Values)
            {
                if (!ConvertOutputExpression(Pair.Value, Target))
                {
                    return false;
                }
            }
            return true;
        }
    }
    if (bExplicitExpression
        && ((Kind == TEXT("local")
            && HasExactFields(Object, {TEXT("kind"), TEXT("name")}))
        || (Kind == TEXT("member")
            && HasExactFields(Object, {TEXT("kind"), TEXT("object"), TEXT("path")}))
        || (Kind == TEXT("stable_ref")
            && (HasExactFields(Object, {TEXT("kind"), TEXT("identityPath")})
                || HasExactFields(Object, {TEXT("kind"), TEXT("identityPath"), TEXT("semanticTag")})))
        || (Kind == TEXT("scoped_stable_ref")
            && HasExactFields(Object, {TEXT("kind"), TEXT("target"), TEXT("reference")}))))
    {
        return ConvertOutputReference(Object, Target);
    }
    if (bExplicitExpression
        && Kind == TEXT("name")
        && HasExactFields(Object, {TEXT("kind"), TEXT("name")}))
    {
        return true;
    }
    if (bExplicitExpression)
    {
        return false;
    }

    TSharedPtr<FJsonObject> Fields = MakeShared<FJsonObject>();
    Fields->Values = Object->Values;
    for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : Fields->Values)
    {
        if (!ConvertOutputExpression(Pair.Value, Target))
        {
            return false;
        }
    }
    Object->Values.Reset();
    Object->SetStringField(TEXT("kind"), TEXT("object"));
    Object->SetObjectField(TEXT("fields"), Fields);
    return true;
}

bool ConvertOutputObjectText(
    const TSharedPtr<FJsonObject>& Result,
    const FSalResolvedTarget& Target)
{
    const TSharedPtr<FJsonObject>* Object = nullptr;
    const TArray<TSharedPtr<FJsonValue>>* Statements = nullptr;
    if (!Result.IsValid()
        || !Result->TryGetObjectField(TEXT("object"), Object)
        || Object == nullptr)
    {
        return true;
    }
    if (!(*Object)->TryGetArrayField(TEXT("statements"), Statements)
        || Statements == nullptr)
    {
        return false;
    }
    for (const TSharedPtr<FJsonValue>& StatementValue : *Statements)
    {
        const TSharedPtr<FJsonObject>* Statement = nullptr;
        if (!StatementValue.IsValid()
            || !StatementValue->TryGetObject(Statement)
            || Statement == nullptr)
        {
            return false;
        }
        if ((*Statement)->HasField(TEXT("target"))
            && (*Statement)->HasField(TEXT("value"))
            && !(*Statement)->HasField(TEXT("kind")))
        {
            if (!ConvertOutputExpression(
                    (*Statement)->TryGetField(TEXT("value")),
                    Target))
            {
                return false;
            }
            continue;
        }
        if ((*Statement)->HasField(TEXT("from"))
            && (*Statement)->HasField(TEXT("to"))
            && !(*Statement)->HasField(TEXT("kind")))
        {
            const TSharedPtr<FJsonObject>* From = nullptr;
            const TSharedPtr<FJsonObject>* To = nullptr;
            if (!(*Statement)->TryGetObjectField(TEXT("from"), From)
                || !(*Statement)->TryGetObjectField(TEXT("to"), To)
                || From == nullptr
                || To == nullptr
                || !ConvertOutputReference(*From, Target)
                || !ConvertOutputReference(*To, Target))
            {
                return false;
            }
        }
    }
    return true;
}

bool ConvertResolvedRefs(
    const TSharedPtr<FJsonObject>& Result,
    const FSalResolvedTarget& Target)
{
    const TSharedPtr<FJsonObject>* Refs = nullptr;
    if (!Result.IsValid() || !Result->HasField(TEXT("resolvedRefs")))
    {
        return true;
    }
    if (!Result->TryGetObjectField(TEXT("resolvedRefs"), Refs)
        || Refs == nullptr)
    {
        return false;
    }
    for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : (*Refs)->Values)
    {
        const TSharedPtr<FJsonObject>* Ref = nullptr;
        if (!Pair.Value.IsValid()
            || !Pair.Value->TryGetObject(Ref)
            || Ref == nullptr
            || !ConvertOutputReference(*Ref, Target))
        {
            return false;
        }
    }
    return true;
}

TSharedPtr<FJsonObject> TargetBinding(
    const FString& Alias,
    const TSharedPtr<FJsonObject>& Target)
{
    TSharedPtr<FJsonObject> Binding = MakeShared<FJsonObject>();
    Binding->SetStringField(TEXT("alias"), Alias);
    Binding->SetObjectField(TEXT("target"), Target);
    return Binding;
}

UEdGraph* FindBlueprintGraphById(
    UBlueprint* Blueprint,
    const FString& Id)
{
    if (Blueprint == nullptr || Id.IsEmpty())
    {
        return nullptr;
    }
    UEdGraph* Match = nullptr;
    TArray<UEdGraph*> Graphs;
    Blueprint->GetAllGraphs(Graphs);
    for (UEdGraph* Graph : Graphs)
    {
        if (Graph != nullptr
            && Graph->GraphGuid.IsValid()
            && Graph->GraphGuid.ToString(
                EGuidFormats::DigitsWithHyphensLower)
                .Equals(Id, ESearchCase::IgnoreCase))
        {
            if (Match != nullptr)
            {
                return nullptr;
            }
            Match = Graph;
        }
    }
    return Match;
}

UEdGraph* FindBlueprintGraphByName(
    UBlueprint* Blueprint,
    const FString& Name)
{
    if (Blueprint == nullptr || Name.IsEmpty())
    {
        return nullptr;
    }
    UEdGraph* Match = nullptr;
    TArray<UEdGraph*> Graphs;
    Blueprint->GetAllGraphs(Graphs);
    for (UEdGraph* Graph : Graphs)
    {
        if (Graph != nullptr
            && Graph->GetName().Equals(Name, ESearchCase::CaseSensitive))
        {
            if (Match != nullptr)
            {
                return nullptr;
            }
            Match = Graph;
        }
    }
    return Match;
}

UEdGraph* FindDispatcherSignatureGraph(
    UBlueprint* Blueprint,
    const FString& Id,
    const FString& Name = FString())
{
    if (Blueprint == nullptr)
    {
        return nullptr;
    }
    FName DispatcherName =
        Name.IsEmpty() ? NAME_None : FName(*Name);
    if (DispatcherName.IsNone() && !Id.IsEmpty())
    {
        for (const FBPVariableDescription& Variable :
             Blueprint->NewVariables)
        {
            if (Variable.VarGuid.IsValid()
                && Variable.VarGuid.ToString(
                    EGuidFormats::DigitsWithHyphensLower)
                    .Equals(Id, ESearchCase::IgnoreCase))
            {
                if (!DispatcherName.IsNone())
                {
                    return nullptr;
                }
                DispatcherName = Variable.VarName;
            }
        }
    }
    return DispatcherName.IsNone()
        ? nullptr
        : FindBlueprintGraphByName(
            Blueprint,
            DispatcherName.ToString());
}

UEdGraph* GraphForBlueprintReference(
    UBlueprint* Blueprint,
    const TSharedPtr<FJsonObject>& Ref)
{
    if (Blueprint == nullptr || !Ref.IsValid())
    {
        return nullptr;
    }
    FString Kind;
    FString Id;
    if (Ref->TryGetStringField(TEXT("kind"), Kind)
        && Ref->TryGetStringField(TEXT("id"), Id))
    {
        if (Kind == TEXT("graph"))
        {
            return FindBlueprintGraphById(Blueprint, Id);
        }
        if (Kind == TEXT("dispatcher"))
        {
            return FindDispatcherSignatureGraph(
                Blueprint,
                Id);
        }
        return nullptr;
    }
    if (Kind == TEXT("member"))
    {
        const TSharedPtr<FJsonObject>* Owner = nullptr;
        return Ref->TryGetObjectField(TEXT("object"), Owner)
                && Owner != nullptr
            ? GraphForBlueprintReference(Blueprint, *Owner)
            : nullptr;
    }
    return nullptr;
}

void CollectBlueprintPatchGraphs(
    const FSalPatch& Patch,
    UBlueprint* Blueprint,
    TSet<UEdGraph*>& OutGraphs)
{
    if (Blueprint == nullptr)
    {
        return;
    }
    for (const TSharedPtr<FJsonValue>& StatementValue :
         Patch.Statements)
    {
        const TSharedPtr<FJsonObject>* Statement = nullptr;
        if (!StatementValue.IsValid()
            || !StatementValue->TryGetObject(Statement)
            || Statement == nullptr
            || !(*Statement).IsValid())
        {
            continue;
        }

        if (!(*Statement)->HasField(TEXT("kind")))
        {
            const TSharedPtr<FJsonObject>* TargetRef = nullptr;
            const TSharedPtr<FJsonObject>* ValueObject = nullptr;
            const TSharedPtr<FJsonObject>* Args = nullptr;
            FString TargetKind;
            FString Alias;
            FString ValueKind;
            FString Callee;
            if ((*Statement)->TryGetObjectField(
                    TEXT("target"),
                    TargetRef)
                && TargetRef != nullptr
                && (*TargetRef)->TryGetStringField(
                    TEXT("kind"),
                    TargetKind)
                && TargetKind == TEXT("local")
                && (*TargetRef)->TryGetStringField(
                    TEXT("name"),
                    Alias)
                && (*Statement)->TryGetObjectField(
                    TEXT("value"),
                    ValueObject)
                && ValueObject != nullptr
                && (*ValueObject)->TryGetStringField(
                    TEXT("kind"),
                    ValueKind)
                && ValueKind == TEXT("call")
                && (*ValueObject)->TryGetStringField(
                    TEXT("callee"),
                    Callee)
                && (*ValueObject)->TryGetObjectField(
                    TEXT("args"),
                    Args)
                && Args != nullptr)
            {
                if (Callee == TEXT("graph"))
                {
                    if (UEdGraph* Graph =
                            FindBlueprintGraphByName(
                                Blueprint,
                                Alias))
                    {
                        OutGraphs.Add(Graph);
                    }
                }
                else if (Callee == TEXT("dispatcher"))
                {
                    if (UEdGraph* Graph =
                            FindDispatcherSignatureGraph(
                                Blueprint,
                                FString(),
                                Alias))
                    {
                        OutGraphs.Add(Graph);
                    }
                }
            }
            continue;
        }

        const TSharedPtr<FJsonObject>* TargetRef = nullptr;
        if ((*Statement)->TryGetObjectField(
                TEXT("target"),
                TargetRef)
            && TargetRef != nullptr)
        {
            if (UEdGraph* Graph = GraphForBlueprintReference(
                    Blueprint,
                    *TargetRef))
            {
                OutGraphs.Add(Graph);
            }
        }
    }
}

void AddGraphHandoff(
    const TSharedPtr<FJsonObject>& Result,
    const FSalResolvedTarget& ActiveTarget,
    UBlueprint* Blueprint,
    UEdGraph* Graph,
    const FString& Purpose)
{
    if (!Result.IsValid()
        || Blueprint == nullptr
        || Graph == nullptr
        || !Blueprint->GetBlueprintGuid().IsValid()
        || !Graph->GraphGuid.IsValid())
    {
        return;
    }
    const FString AssetPath =
        Blueprint == ActiveTarget.Blueprint
            ? ActiveTarget.AssetPath
            : Blueprint->GetPathName();
    ResultTargets::AddHandoff(
        Result,
        ResultTargets::Graph(
            AssetPath,
            Blueprint->GetBlueprintGuid().ToString(
                EGuidFormats::DigitsWithHyphensLower),
            Graph->GraphGuid.ToString(
                EGuidFormats::DigitsWithHyphensLower)),
        Graph->GetName() + TEXT("_graph_target"),
        Purpose);
}

bool PatchContainsOperation(
    const FSalPatch& Patch,
    const FString& ExpectedKind)
{
    for (const TSharedPtr<FJsonValue>& Value : Patch.Statements)
    {
        const TSharedPtr<FJsonObject>* Statement = nullptr;
        FString Kind;
        if (Value.IsValid()
            && Value->TryGetObject(Statement)
            && Statement != nullptr
            && (*Statement)->TryGetStringField(TEXT("kind"), Kind)
            && Kind == ExpectedKind)
        {
            return true;
        }
    }
    return false;
}

void AddCrossDomainHandoffs(
    const TSharedPtr<FJsonObject>& Result,
    const FSalResolvedTarget& Target,
    const FSalQuery* Query,
    const FSalPatch* Patch)
{
    if (!Result.IsValid())
    {
        return;
    }
    if (Target.Domain == ESalDomain::Blueprint
        && Target.Blueprint != nullptr)
    {
        if (Query != nullptr)
        {
            FString Operation;
            Query->Operation->TryGetStringField(
                TEXT("kind"),
                Operation);
            FString Id;
            FString Name;
            Query->Operation->TryGetStringField(
                TEXT("id"),
                Id);
            Query->Operation->TryGetStringField(
                TEXT("name"),
                Name);
            UEdGraph* Graph = nullptr;
            if (Operation == TEXT("graph"))
            {
                Graph = !Id.IsEmpty()
                    ? FindBlueprintGraphById(
                        Target.Blueprint,
                        Id)
                    : FindBlueprintGraphByName(
                        Target.Blueprint,
                        Name);
            }
            else if (Operation == TEXT("dispatcher"))
            {
                Graph = FindDispatcherSignatureGraph(
                    Target.Blueprint,
                    Id,
                    Name);
            }
            if (Graph != nullptr)
            {
                AddGraphHandoff(
                    Result,
                    Target,
                    Target.Blueprint,
                    Graph,
                    TEXT("edit_graph"));
            }
        }
        if (Patch != nullptr)
        {
            TSet<UEdGraph*> Graphs;
            CollectBlueprintPatchGraphs(
                *Patch,
                Target.Blueprint,
                Graphs);
            for (UEdGraph* Graph : Graphs)
            {
                AddGraphHandoff(
                    Result,
                    Target,
                    Target.Blueprint,
                    Graph,
                    PatchContainsOperation(
                        *Patch,
                        TEXT("compile"))
                        ? TEXT("inspect_compile_source")
                        : TEXT("edit_graph"));
            }
        }
    }

    if (Target.Domain == ESalDomain::Widget
        && Query != nullptr
        && Target.Blueprint != nullptr
        && Query->With.Contains(TEXT("schema")))
    {
        FString Operation;
        Query->Operation->TryGetStringField(
            TEXT("kind"),
            Operation);
        if (Operation == TEXT("widget"))
        {
            for (UEdGraph* Graph :
                 Target.Blueprint->UbergraphPages)
            {
                if (Graph != nullptr && Graph->GraphGuid.IsValid())
                {
                    AddGraphHandoff(
                        Result,
                        Target,
                        Target.Blueprint,
                        Graph,
                        TEXT("graph_event"));
                    break;
                }
            }
        }
    }
}

void AddBlueprintHandoff(
    const TSharedPtr<FJsonObject>& Result,
    const FSalResolvedTarget& Target)
{
    if (!Result.IsValid()
        || (Target.Domain != ESalDomain::Class
            && Target.Domain != ESalDomain::Graph
            && Target.Domain != ESalDomain::Widget)
        || !Result->HasField(TEXT("isError")))
    {
        return;
    }
    UBlueprint* Blueprint = Target.Blueprint;
    if (Blueprint == nullptr)
    {
        if (const UBlueprintGeneratedClass* GeneratedClass =
                Cast<UBlueprintGeneratedClass>(Target.Class))
        {
            Blueprint = Cast<UBlueprint>(
                GeneratedClass->ClassGeneratedBy);
        }
    }
    if (Blueprint == nullptr
        || !Blueprint->GetBlueprintGuid().IsValid()
        || Target.AssetPath.IsEmpty())
    {
        return;
    }
    ResultTargets::AddHandoff(
        Result,
        ResultTargets::Blueprint(
            Target.AssetPath,
            Blueprint->GetBlueprintGuid().ToString(
                EGuidFormats::DigitsWithHyphensLower)),
        TEXT("blueprint_target"),
        TEXT("compile"));
}

void RenameScopedTargetAlias(
    const TSharedPtr<FJsonValue>& Value,
    const FString& OldAlias,
    const FString& NewAlias)
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
            RenameScopedTargetAlias(
                Item,
                OldAlias,
                NewAlias);
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
    FString Kind;
    (*Object)->TryGetStringField(TEXT("kind"), Kind);
    if (Kind == TEXT("scoped_stable_ref"))
    {
        const TSharedPtr<FJsonObject>* Scope = nullptr;
        FString ScopeKind;
        FString ScopeName;
        if ((*Object)->TryGetObjectField(TEXT("target"), Scope)
            && Scope != nullptr
            && (*Scope)->TryGetStringField(TEXT("kind"), ScopeKind)
            && ScopeKind == TEXT("local")
            && (*Scope)->TryGetStringField(TEXT("name"), ScopeName)
            && ScopeName == OldAlias)
        {
            (*Scope)->SetStringField(TEXT("name"), NewAlias);
        }
    }
    for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair :
         (*Object)->Values)
    {
        RenameScopedTargetAlias(
            Pair.Value,
            OldAlias,
            NewAlias);
    }
}

void AvoidMainTargetAliasCollision(
    const TSharedPtr<FJsonObject>& Result,
    const FString& MainAlias)
{
    const TArray<TSharedPtr<FJsonValue>>* Related = nullptr;
    if (!Result.IsValid()
        || MainAlias.IsEmpty()
        || !Result->TryGetArrayField(
            TEXT("relatedTargets"),
            Related)
        || Related == nullptr)
    {
        return;
    }
    TSet<FString> UsedAliases;
    UsedAliases.Add(MainAlias);
    for (const TSharedPtr<FJsonValue>& Value : *Related)
    {
        const TSharedPtr<FJsonObject>* Binding = nullptr;
        FString Alias;
        if (Value.IsValid()
            && Value->TryGetObject(Binding)
            && Binding != nullptr
            && (*Binding)->TryGetStringField(TEXT("alias"), Alias))
        {
            UsedAliases.Add(Alias);
        }
    }
    for (const TSharedPtr<FJsonValue>& Value : *Related)
    {
        const TSharedPtr<FJsonObject>* Binding = nullptr;
        FString Alias;
        if (!Value.IsValid()
            || !Value->TryGetObject(Binding)
            || Binding == nullptr
            || !(*Binding)->TryGetStringField(TEXT("alias"), Alias)
            || Alias != MainAlias)
        {
            continue;
        }
        const FString Base =
            FSalObjectBuilder::SanitizeIdentifier(
                Alias + TEXT("_related"),
                TEXT("related_target"));
        FString Replacement = Base;
        for (int32 Suffix = 2;
             UsedAliases.Contains(Replacement);
             ++Suffix)
        {
            Replacement = FString::Printf(
                TEXT("%s_%d"),
                *Base,
                Suffix);
        }
        UsedAliases.Add(Replacement);
        (*Binding)->SetStringField(
            TEXT("alias"),
            Replacement);

        const TArray<TSharedPtr<FJsonValue>>* Handoffs = nullptr;
        if (Result->TryGetArrayField(TEXT("handoffs"), Handoffs)
            && Handoffs != nullptr)
        {
            for (const TSharedPtr<FJsonValue>& HandoffValue :
                 *Handoffs)
            {
                const TSharedPtr<FJsonObject>* Handoff = nullptr;
                const TSharedPtr<FJsonObject>* Ref = nullptr;
                FString Name;
                if (HandoffValue.IsValid()
                    && HandoffValue->TryGetObject(Handoff)
                    && Handoff != nullptr
                    && (*Handoff)->TryGetObjectField(
                        TEXT("target"),
                        Ref)
                    && Ref != nullptr
                    && (*Ref)->TryGetStringField(TEXT("name"), Name)
                    && Name == Alias)
                {
                    (*Ref)->SetStringField(
                        TEXT("name"),
                        Replacement);
                }
            }
        }
        RenameScopedTargetAlias(
            MakeShared<FJsonValueObject>(Result),
            Alias,
            Replacement);
    }
}

void RewriteMainTargetBindingAsMembers(
    const TSharedPtr<FJsonObject>& Result,
    const FString& MainAlias)
{
    const TSharedPtr<FJsonObject>* Object = nullptr;
    const TArray<TSharedPtr<FJsonValue>>* Statements = nullptr;
    if (!Result.IsValid()
        || MainAlias.IsEmpty()
        || !Result->TryGetObjectField(TEXT("object"), Object)
        || Object == nullptr
        || !(*Object)->TryGetArrayField(TEXT("statements"), Statements)
        || Statements == nullptr)
    {
        return;
    }

    TArray<TSharedPtr<FJsonValue>> Filtered;
    Filtered.Reserve(Statements->Num());
    bool bRewritten = false;
    for (const TSharedPtr<FJsonValue>& StatementValue : *Statements)
    {
        const TSharedPtr<FJsonObject>* Statement = nullptr;
        const TSharedPtr<FJsonObject>* BindingTarget = nullptr;
        const TSharedPtr<FJsonObject>* BindingValue = nullptr;
        const TSharedPtr<FJsonObject>* Fields = nullptr;
        FString Kind;
        FString Name;
        FString ValueKind;
        const bool bMainTargetObjectBinding =
            StatementValue.IsValid()
            && StatementValue->TryGetObject(Statement)
            && Statement != nullptr
            && !(*Statement)->HasField(TEXT("kind"))
            && (*Statement)->TryGetObjectField(
                TEXT("target"),
                BindingTarget)
            && BindingTarget != nullptr
            && (*BindingTarget)->TryGetStringField(
                TEXT("kind"),
                Kind)
            && Kind == TEXT("local")
            && (*BindingTarget)->TryGetStringField(
                TEXT("name"),
                Name)
            && Name == MainAlias
            && (*Statement)->TryGetObjectField(
                TEXT("value"),
                BindingValue)
            && BindingValue != nullptr
            && (*BindingValue)->TryGetStringField(
                TEXT("kind"),
                ValueKind)
            && ValueKind == TEXT("object")
            && (*BindingValue)->TryGetObjectField(
                TEXT("fields"),
                Fields)
            && Fields != nullptr;
        if (!bMainTargetObjectBinding)
        {
            Filtered.Add(StatementValue);
            continue;
        }
        bRewritten = true;

        // A Result Target binding declares MainAlias. Domain encoders written
        // before that table existed also bound the complete owner ObjectExpr
        // to the same local alias. Preserve every data field by projecting it
        // onto the declared Target instead of silently discarding the object.
        // semanticTag is presentation-only metadata, so it does not become a
        // stored member.
        for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair :
             (*Fields)->Values)
        {
            TSharedPtr<FJsonObject> MemberBinding =
                MakeShared<FJsonObject>();
            MemberBinding->SetObjectField(
                TEXT("target"),
                Value::MemberObject(
                    Value::LocalObject(MainAlias),
                    {Pair.Key}));
            MemberBinding->SetField(TEXT("value"), Pair.Value);
            Filtered.Add(
                MakeShared<FJsonValueObject>(MemberBinding));
        }
    }
    if (bRewritten)
    {
        (*Object)->SetArrayField(
            TEXT("statements"),
            Filtered);
    }
}

TSharedPtr<FJsonObject> DecorateResolvedResult(
    const TSharedPtr<FJsonObject>& Result,
    const FSalResolvedTarget& Target,
    const FSalQuery* Query = nullptr,
    const FSalPatch* Patch = nullptr)
{
    if (!Result.IsValid())
    {
        return Result;
    }
    Result->SetStringField(
        TEXT("targetContext"),
        Target.bDomainRoot ? TEXT("domain_root") : TEXT("exact_target"));
    // The Result Target table already declares Target.Alias. Older Domain
    // encoders also emitted the owner object under that same local name.
    // Project that ObjectExpr's fields onto the Target so the local no longer
    // shadows the Target alias without losing the returned owner data.
    RewriteMainTargetBindingAsMembers(
        Result,
        Target.Alias);
    AvoidMainTargetAliasCollision(Result, Target.Alias);
    Result->SetObjectField(
        TEXT("target"),
        TargetBinding(Target.Alias, Target.CanonicalTarget));
    AddCrossDomainHandoffs(
        Result,
        Target,
        Query,
        Patch);
    AddBlueprintHandoff(Result, Target);
    if (!ConvertOutputObjectText(Result, Target)
        || !ConvertResolvedRefs(Result, Target))
    {
        TSharedPtr<FJsonObject> Error = FSalDiagnostics::Result(
            FSalDiagnostics::Error(
                TEXT("language.invalid_result_shape"),
                TEXT("Domain output could not be normalized to ObjectExpr and Target-relative StableRef."))
                .Build());
        Error->SetStringField(
            TEXT("targetContext"),
            Target.bDomainRoot ? TEXT("domain_root") : TEXT("exact_target"));
        Error->SetObjectField(
            TEXT("target"),
            TargetBinding(Target.Alias, Target.CanonicalTarget));
        for (const TCHAR* Field : {
                 TEXT("relatedTargets"),
                 TEXT("handoffs")})
        {
            if (const TSharedPtr<FJsonValue> Value =
                    Result->TryGetField(Field);
                Value.IsValid())
            {
                Error->SetField(Field, Value);
            }
        }
        return Error;
    }
    return Result;
}

TSharedPtr<FJsonObject> DecorateUnresolvedResult(
    const TSharedPtr<FJsonObject>& Result)
{
    if (Result.IsValid())
    {
        Result->SetStringField(
            TEXT("targetContext"),
            TEXT("unresolved_target"));
    }
    return Result;
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
        // Values keys are FString before UE 5.8 and UE::FSharedString from
        // 5.8 on. operator* yields const TCHAR* for both, so this copy
        // compiles against either engine version.
        for (const auto& Pair : (*Object)->Values) Keys.Add(FString(*Pair.Key));
        Keys.Sort();
        Out += TEXT("o{");
        for (const FString& Key : Keys)
        {
            AppendCursorToken(Out, TEXT('k'), Key);
            // TryGetField takes an FString on both engine versions;
            // Values.FindRef cannot from UE 5.8 on.
            AppendCanonicalJson(Out, (*Object)->TryGetField(Key));
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
    const FString Operation = OperationKind(Query.Operation);
    switch (Target.Domain)
    {
    case ESalDomain::Asset:
        if (Operation == TEXT("target"))
        {
            return FSalObjectBuilder().BuildResult();
        }
        if (Operation == TEXT("references"))
        {
            return ReferenceUnavailable(
                Target,
                TEXT("Asset Domain does not currently expose a complete authored reference provider."));
        }
        return FSalAssetInterface::Query(Query, Target);
    case ESalDomain::Blueprint:
        return Operation == TEXT("references")
            ? FSalReferenceInterface::Query(Query, Target)
            : FSalBlueprintInterface::Query(Query, Target);
    case ESalDomain::Class:
        return Operation == TEXT("references")
            ? FSalReferenceInterface::Query(Query, Target)
            : FSalClassInterface::Query(Query, Target);
    case ESalDomain::Graph:
        return Operation == TEXT("references")
            ? FSalReferenceInterface::Query(Query, Target)
            : FSalGraphInterface::Query(Query, Target);
    case ESalDomain::StateTree:
        return FSalStateTreeInterface::Query(Query, Target);
    case ESalDomain::Widget:
        return Operation == TEXT("references")
            ? FSalReferenceInterface::Query(Query, Target)
            : FSalWidgetInterface::Query(Query, Target);
    case ESalDomain::Pcg:
        return FSalPCGInterface::Query(Query, Target);
    case ESalDomain::Level:
        return FSalLevelInterface::Query(Query, Target);
    case ESalDomain::PcgComponent:
        return FSalPCGComponentInterface::Query(Query, Target);
    default:
        return InterfaceError(Operation, Target);
    }
}

TSharedPtr<FJsonObject> DispatchPatch(const FSalPatch& Patch, const FSalResolvedTarget& Target)
{
    switch (Target.Domain)
    {
    case ESalDomain::Asset:
        return FSalAssetInterface::Patch(Patch, Target);
    case ESalDomain::Blueprint:
        return FSalBlueprintInterface::Patch(Patch, Target);
    case ESalDomain::Class:
        return FSalClassInterface::Patch(Patch, Target);
    case ESalDomain::Graph:
        return FSalGraphInterface::Patch(Patch, Target);
    case ESalDomain::StateTree:
        return FSalStateTreeInterface::Patch(Patch, Target);
    case ESalDomain::Widget:
        return FSalWidgetInterface::Patch(Patch, Target);
    case ESalDomain::Level:
        return FSalLevelInterface::Patch(Patch, Target);
    case ESalDomain::Pcg:
        return FSalPCGInterface::Patch(Patch, Target);
    case ESalDomain::PcgComponent:
        return InterfaceError(TEXT("patch"), Target);
    default:
        return InterfaceError(TEXT("patch"), Target);
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
        return FinalizeQueryResult(DecorateUnresolvedResult(Error));
    }
    FSalResolvedTarget Target;
    if (!FSalTargetResolver().Resolve(Query.Alias, Query.TargetValue, false, Target, Error))
    {
        return FinalizeQueryResult(DecorateUnresolvedResult(Error));
    }
    if (!LowerQueryForDomain(Query, Target, Error))
    {
        return FinalizeQueryResult(
            DecorateResolvedResult(
                Error,
                Target,
                &Query));
    }
    return FinalizeQueryResult(
        DecorateResolvedResult(
            DispatchQuery(Query, Target),
            Target,
            &Query));
}

TSharedPtr<FJsonObject> FSalModule::BuildPatchResult(const TSharedPtr<FJsonObject>& Arguments)
{
    FSalPatch Patch;
    TSharedPtr<FJsonObject> Error;
    if (!FSalJson::DecodePatch(Arguments, Patch, Error))
    {
        return ValidateOutgoing(
            DecorateUnresolvedResult(
                MutationFailure(Error, RequestedDryRun(Arguments))));
    }
    FSalResolvedTarget Target;
    if (!FSalTargetResolver().Resolve(Patch.Alias, Patch.TargetValue, true, Target, Error))
    {
        return ValidateOutgoing(
            DecorateUnresolvedResult(
                MutationFailure(Error, Patch.bDryRun)));
    }
    if (!LowerPatchForDomain(Patch, Target, Error))
    {
        return ValidateOutgoing(
            DecorateResolvedResult(
                MutationFailure(Error, Patch.bDryRun, Target.AssetPath),
                Target,
                nullptr,
                &Patch));
    }
    TSharedPtr<FJsonObject> Result = DispatchPatch(Patch, Target);
    if (!Result.IsValid() || !Result->HasField(TEXT("isError")))
    {
        Result = MutationFailure(Result, Patch.bDryRun, Target.AssetPath);
    }
    Result = DecorateResolvedResult(
        Result,
        Target,
        nullptr,
        &Patch);
    TSharedPtr<FJsonObject> Validated = ValidateOutgoing(Result);
    if (!Validated.IsValid() || !Validated->HasField(TEXT("isError")))
    {
        Validated = ValidateOutgoing(
            DecorateResolvedResult(
                MutationFailure(Validated, Patch.bDryRun, Target.AssetPath),
                Target,
                nullptr,
                &Patch));
    }
    return Validated;
}

#if WITH_DEV_AUTOMATION_TESTS
TSharedPtr<FJsonObject> FSalModule::EnforceQueryResultSizeForTesting(
    const TSharedPtr<FJsonObject>& Result)
{
    return EnforceQueryResultSize(Result, FSalQuery());
}

bool FSalModule::NormalizeOutputExpressionForTesting(
    const TSharedPtr<FJsonValue>& Value)
{
    FSalResolvedTarget Target;
    Target.Domain = ESalDomain::Asset;
    return ConvertOutputExpression(Value, Target);
}
#endif
}
