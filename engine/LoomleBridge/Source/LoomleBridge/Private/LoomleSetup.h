// Copyright 2026 Loomle contributors.

#pragma once

#include "CoreMinimal.h"

namespace LoomleSetup
{
enum class EClientEntryKind
{
    None,
    Bundled,
    StaleBundled,
    LegacyPython,
    LegacyGlobal,
    Manual
};

struct FConfigAssessment
{
    EClientEntryKind ExistingKind = EClientEntryKind::None;
    FString SuggestedText;
    FString Message;
    bool bNeedsConfiguration = false;
    bool bNeedsMigration = false;
    bool bBlocked = false;
    bool bSyntaxUnverified = false;
};

using FClientFileExists = TFunctionRef<bool(const FString&)>;

LOOMLEBRIDGE_API FString MakeClientTarget(const FString& NodePlatform, const FString& Architecture);
LOOMLEBRIDGE_API FString GetCurrentClientTarget();
LOOMLEBRIDGE_API FString GetBundledClientPath(const FString& PluginBaseDir);
LOOMLEBRIDGE_API bool HasBundledClient(const FString& PluginBaseDir);
LOOMLEBRIDGE_API FString ResolveCodexConfigPath(
    const FString& LoomleHomeDirectory,
    const FString& CodexHomeEnvironment);

LOOMLEBRIDGE_API FString ClientEntryKindToString(EClientEntryKind Kind);

LOOMLEBRIDGE_API FConfigAssessment AssessCodexConfig(
    const FString& RawConfig,
    const FString& BundledClientPath,
    bool bBundledClientAvailable,
    const FString& LoomleHomeDirectory,
    FClientFileExists ClientFileExists);

LOOMLEBRIDGE_API FConfigAssessment AssessClaudeConfig(
    const FString& RawConfig,
    const FString& BundledClientPath,
    bool bBundledClientAvailable,
    const FString& LoomleHomeDirectory,
    FClientFileExists ClientFileExists);
}
