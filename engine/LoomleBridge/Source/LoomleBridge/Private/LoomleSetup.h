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

FString MakeClientTarget(const FString& NodePlatform, const FString& Architecture);
FString GetCurrentClientTarget();
FString GetBundledClientPath(const FString& PluginBaseDir);
bool HasBundledClient(const FString& PluginBaseDir);
FString ResolveCodexConfigPath(
    const FString& LoomleHomeDirectory,
    const FString& CodexHomeEnvironment);

FString ClientEntryKindToString(EClientEntryKind Kind);

FConfigAssessment AssessCodexConfig(
    const FString& RawConfig,
    const FString& BundledClientPath,
    bool bBundledClientAvailable,
    const FString& LoomleHomeDirectory,
    FClientFileExists ClientFileExists);

FConfigAssessment AssessClaudeConfig(
    const FString& RawConfig,
    const FString& BundledClientPath,
    bool bBundledClientAvailable,
    const FString& LoomleHomeDirectory,
    FClientFileExists ClientFileExists);
}
