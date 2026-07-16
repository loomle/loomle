// Copyright 2026 Loomle contributors.

#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

namespace LoomleMutation
{
    static TSharedPtr<FJsonValue> MakeDiagnostic(const FString& Severity, const FString& Code, const FString& Message)
    {
        TSharedPtr<FJsonObject> Diagnostic = MakeShared<FJsonObject>();
        Diagnostic->SetStringField(TEXT("severity"), Severity);
        Diagnostic->SetStringField(TEXT("code"), Code);
        Diagnostic->SetStringField(TEXT("message"), Message);
        return MakeShared<FJsonValueObject>(Diagnostic);
    }

    static void SetDiagnostics(const TSharedPtr<FJsonObject>& Result, const TArray<TSharedPtr<FJsonValue>>& Diagnostics)
    {
        if (Result.IsValid())
        {
            Result->SetArrayField(TEXT("diagnostics"), Diagnostics);
        }
    }

    /**
     * Builds the shared normalized mutation envelope used by SAL and any other
     * ordered-object surface. Domain code supplies only its object, diagnostics,
     * plan, resolved references, and diff; envelope fields stay centralized.
     */
    static TSharedPtr<FJsonObject> BuildMutationResult(
        const TSharedPtr<FJsonObject>& Object,
        const TArray<TSharedPtr<FJsonValue>>& Diagnostics,
        const bool bDryRun,
        const bool bValid,
        const bool bApplied,
        const FString& AssetPath,
        const FString& Operation,
        const TSharedPtr<FJsonObject>& Planned = nullptr,
        const TSharedPtr<FJsonObject>& ResolvedRefs = nullptr,
        const TSharedPtr<FJsonObject>& Diff = nullptr)
    {
        TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
        if (Object.IsValid())
        {
            Result->SetObjectField(TEXT("object"), Object);
        }
        SetDiagnostics(Result, Diagnostics);
        Result->SetBoolField(TEXT("isError"), !bValid);
        Result->SetBoolField(TEXT("dryRun"), bDryRun);
        Result->SetBoolField(TEXT("valid"), bValid);
        Result->SetBoolField(TEXT("applied"), bApplied);
        Result->SetStringField(TEXT("operation"), Operation);
        if (!AssetPath.IsEmpty())
        {
            Result->SetStringField(TEXT("assetPath"), AssetPath);
        }
        if (Planned.IsValid())
        {
            Result->SetObjectField(TEXT("planned"), Planned);
        }
        if (ResolvedRefs.IsValid())
        {
            Result->SetObjectField(TEXT("resolvedRefs"), ResolvedRefs);
        }
        if (Diff.IsValid())
        {
            Result->SetObjectField(TEXT("diff"), Diff);
        }
        return Result;
    }

    static void SetRevision(const TSharedPtr<FJsonObject>& Result, const FString& PreviousRevision, const FString& NewRevision)
    {
        if (!Result.IsValid())
        {
            return;
        }
        Result->SetStringField(TEXT("previousRevision"), PreviousRevision);
        Result->SetStringField(TEXT("newRevision"), NewRevision);
    }

    static void SetUnchangedRevision(const TSharedPtr<FJsonObject>& Result, const FString& Revision)
    {
        SetRevision(Result, Revision, Revision);
    }

    static void SetFailure(const TSharedPtr<FJsonObject>& Result, const FString& Code, const FString& Message)
    {
        if (!Result.IsValid())
        {
            return;
        }
        Result->SetBoolField(TEXT("isError"), true);
        Result->SetBoolField(TEXT("valid"), false);
        Result->SetBoolField(TEXT("applied"), false);
        Result->SetStringField(TEXT("code"), Code);
        Result->SetStringField(TEXT("message"), Message);
        TArray<TSharedPtr<FJsonValue>> Diagnostics;
        Diagnostics.Add(MakeDiagnostic(TEXT("error"), Code, Message));
        SetDiagnostics(Result, Diagnostics);
    }

    static void SetRevisionConflict(const TSharedPtr<FJsonObject>& Result, const FString& ExpectedRevision, const FString& CurrentRevision)
    {
        SetFailure(
            Result,
            TEXT("REVISION_CONFLICT"),
            FString::Printf(TEXT("expectedRevision mismatch: expected %s but current revision is %s."), *ExpectedRevision, *CurrentRevision));
        SetUnchangedRevision(Result, CurrentRevision);
    }

    static TSharedPtr<FJsonObject> BuildBasicPlan(
        const FString& Tool,
        const FString& AssetPath,
        const FString& Operation,
        const TSharedPtr<FJsonObject>& Args,
        const TSharedPtr<FJsonObject>& ExtraResolvedRefs = nullptr)
    {
        TSharedPtr<FJsonObject> Plan = MakeShared<FJsonObject>();
        Plan->SetStringField(TEXT("tool"), Tool);
        Plan->SetStringField(TEXT("action"), Operation);
        Plan->SetStringField(TEXT("operation"), Operation);
        Plan->SetObjectField(TEXT("args"), Args.IsValid() ? Args : MakeShared<FJsonObject>());

        TSharedPtr<FJsonObject> ResolvedRefs = ExtraResolvedRefs.IsValid() ? ExtraResolvedRefs : MakeShared<FJsonObject>();
        if (!ResolvedRefs->HasField(TEXT("asset")))
        {
            TSharedPtr<FJsonObject> AssetRef = MakeShared<FJsonObject>();
            AssetRef->SetStringField(TEXT("path"), AssetPath);
            ResolvedRefs->SetObjectField(TEXT("asset"), AssetRef);
        }
        Plan->SetObjectField(TEXT("resolvedRefs"), ResolvedRefs);
        return Plan;
    }

    static void SetValidatedPlan(
        const TSharedPtr<FJsonObject>& Result,
        const FString& Tool,
        const FString& AssetPath,
        const FString& Operation,
        const TSharedPtr<FJsonObject>& Args,
        const TSharedPtr<FJsonObject>& ExtraResolvedRefs = nullptr)
    {
        if (!Result.IsValid())
        {
            return;
        }
        TSharedPtr<FJsonObject> Plan = BuildBasicPlan(Tool, AssetPath, Operation, Args, ExtraResolvedRefs);
        const TSharedPtr<FJsonObject>* ResolvedRefs = nullptr;
        if (Plan->TryGetObjectField(TEXT("resolvedRefs"), ResolvedRefs) && ResolvedRefs != nullptr && ResolvedRefs->IsValid())
        {
            Result->SetObjectField(TEXT("resolvedRefs"), *ResolvedRefs);
        }
        Result->SetObjectField(TEXT("planned"), Plan);
        Result->SetBoolField(TEXT("valid"), true);
        SetDiagnostics(Result, TArray<TSharedPtr<FJsonValue>>{});
    }

    static TSharedPtr<FJsonObject> BuildBatchPlan(
        const FString& Tool,
        const FString& AssetPath,
        const FString& Operation,
        const TArray<TSharedPtr<FJsonValue>>& Commands,
        const TSharedPtr<FJsonObject>& ExtraResolvedRefs = nullptr)
    {
        TSharedPtr<FJsonObject> Args = MakeShared<FJsonObject>();
        Args->SetArrayField(TEXT("commands"), Commands);
        TSharedPtr<FJsonObject> Plan = BuildBasicPlan(Tool, AssetPath, Operation, Args, ExtraResolvedRefs);
        Plan->SetNumberField(TEXT("commandCount"), Commands.Num());
        Plan->SetArrayField(TEXT("commands"), Commands);
        return Plan;
    }

    static TSharedPtr<FJsonObject> BuildBatchPlanFromOpResults(
        const FString& Tool,
        const FString& AssetPath,
        const FString& Operation,
        const int32 CommandCount,
        const TArray<TSharedPtr<FJsonValue>>& OpResults,
        const TSharedPtr<FJsonObject>& ExtraResolvedRefs = nullptr)
    {
        TArray<TSharedPtr<FJsonValue>> PlannedCommands;
        PlannedCommands.Reserve(OpResults.Num());
        for (const TSharedPtr<FJsonValue>& OpResultValue : OpResults)
        {
            const TSharedPtr<FJsonObject>* OpResultObject = nullptr;
            if (!OpResultValue.IsValid()
                || !OpResultValue->TryGetObject(OpResultObject)
                || OpResultObject == nullptr
                || !(*OpResultObject).IsValid())
            {
                continue;
            }

            TSharedPtr<FJsonObject> PlannedCommand = MakeShared<FJsonObject>();
            double CommandIndex = 0.0;
            if ((*OpResultObject)->TryGetNumberField(TEXT("index"), CommandIndex))
            {
                PlannedCommand->SetNumberField(TEXT("index"), CommandIndex);
            }
            FString PlannedOp;
            if ((*OpResultObject)->TryGetStringField(TEXT("op"), PlannedOp))
            {
                PlannedCommand->SetStringField(TEXT("op"), PlannedOp);
            }
            bool bOpOk = false;
            if ((*OpResultObject)->TryGetBoolField(TEXT("ok"), bOpOk))
            {
                PlannedCommand->SetBoolField(TEXT("valid"), bOpOk);
            }
            bool bOpChanged = false;
            if ((*OpResultObject)->TryGetBoolField(TEXT("changed"), bOpChanged))
            {
                PlannedCommand->SetBoolField(TEXT("changed"), bOpChanged);
            }
            FString PlannedNodeId;
            if ((*OpResultObject)->TryGetStringField(TEXT("nodeId"), PlannedNodeId) && !PlannedNodeId.IsEmpty())
            {
                PlannedCommand->SetStringField(TEXT("nodeId"), PlannedNodeId);
            }
            FString PlannedErrorCode;
            if ((*OpResultObject)->TryGetStringField(TEXT("errorCode"), PlannedErrorCode) && !PlannedErrorCode.IsEmpty())
            {
                PlannedCommand->SetStringField(TEXT("errorCode"), PlannedErrorCode);
            }
            PlannedCommands.Add(MakeShared<FJsonValueObject>(PlannedCommand));
        }

        TSharedPtr<FJsonObject> Plan = BuildBatchPlan(Tool, AssetPath, Operation, PlannedCommands, ExtraResolvedRefs);
        Plan->SetNumberField(TEXT("commandCount"), CommandCount);
        return Plan;
    }

    static TSharedPtr<FJsonObject> MakeOpResult(
        const int32 Index,
        const FString& Operation,
        const bool bOk,
        const bool bChanged,
        const FString& ErrorCode = FString(),
        const FString& ErrorMessage = FString(),
        const FString& NodeId = FString(),
        const TSharedPtr<FJsonObject>& Details = nullptr)
    {
        TSharedPtr<FJsonObject> OpResult = MakeShared<FJsonObject>();
        OpResult->SetNumberField(TEXT("index"), Index);
        OpResult->SetStringField(TEXT("op"), Operation);
        OpResult->SetBoolField(TEXT("ok"), bOk);
        OpResult->SetBoolField(TEXT("skipped"), false);
        OpResult->SetBoolField(TEXT("changed"), bChanged);
        OpResult->SetStringField(TEXT("errorCode"), bOk ? TEXT("") : ErrorCode);
        OpResult->SetStringField(TEXT("errorMessage"), bOk ? TEXT("") : ErrorMessage);
        if (!NodeId.IsEmpty())
        {
            OpResult->SetStringField(TEXT("nodeId"), NodeId);
        }
        if (!bOk && Details.IsValid())
        {
            OpResult->SetObjectField(TEXT("details"), Details);
        }
        return OpResult;
    }

    static void SetMutationEnvelope(
        const TSharedPtr<FJsonObject>& Result,
        const FString& Tool,
        const FString& AssetPath,
        const FString& Operation,
        const bool bDryRun,
        const bool bApplied,
        const bool bValid = true)
    {
        if (!Result.IsValid())
        {
            return;
        }
        Result->SetBoolField(TEXT("isError"), !bValid);
        Result->SetBoolField(TEXT("valid"), bValid);
        Result->SetBoolField(TEXT("dryRun"), bDryRun);
        Result->SetBoolField(TEXT("applied"), bApplied);
        Result->SetStringField(TEXT("tool"), Tool);
        Result->SetStringField(TEXT("assetPath"), AssetPath);
        Result->SetStringField(TEXT("operation"), Operation);
        if (!Result->HasField(TEXT("diagnostics")))
        {
            SetDiagnostics(Result, TArray<TSharedPtr<FJsonValue>>{});
        }
    }

    static TSharedPtr<FJsonObject> MakeTarget(const FString& Type, const FString& Name = FString(), const FString& Path = FString())
    {
        TSharedPtr<FJsonObject> Target = MakeShared<FJsonObject>();
        Target->SetStringField(TEXT("type"), Type);
        if (!Name.IsEmpty())
        {
            Target->SetStringField(TEXT("name"), Name);
        }
        if (!Path.IsEmpty())
        {
            Target->SetStringField(TEXT("path"), Path);
        }
        return Target;
    }

    static TSharedPtr<FJsonObject> MakeChange(
        const FString& Kind,
        const TSharedPtr<FJsonObject>& Target,
        const TSharedPtr<FJsonValue>& Before = nullptr,
        const TSharedPtr<FJsonValue>& After = nullptr)
    {
        TSharedPtr<FJsonObject> Change = MakeShared<FJsonObject>();
        Change->SetStringField(TEXT("kind"), Kind);
        if (Target.IsValid())
        {
            Change->SetObjectField(TEXT("target"), Target);
        }
        if (Before.IsValid())
        {
            Change->SetField(TEXT("before"), Before);
        }
        if (After.IsValid())
        {
            Change->SetField(TEXT("after"), After);
        }
        return Change;
    }

    static TSharedPtr<FJsonObject> MakeDiff(const FString& Scope, const TArray<TSharedPtr<FJsonValue>>& Changes)
    {
        TSharedPtr<FJsonObject> Diff = MakeShared<FJsonObject>();
        Diff->SetStringField(TEXT("scope"), Scope);
        Diff->SetArrayField(TEXT("changes"), Changes);
        return Diff;
    }

    static void SetDiff(const TSharedPtr<FJsonObject>& Result, const FString& Scope, const TArray<TSharedPtr<FJsonValue>>& Changes)
    {
        if (Result.IsValid())
        {
            Result->SetObjectField(TEXT("diff"), MakeDiff(Scope, Changes));
        }
    }
}
