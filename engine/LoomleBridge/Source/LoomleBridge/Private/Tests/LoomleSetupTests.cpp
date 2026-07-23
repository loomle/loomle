// Copyright 2026 Loomle contributors.

#if WITH_DEV_AUTOMATION_TESTS

#include "LoomleSetup.h"

#include "Misc/AutomationTest.h"

namespace
{
const FString CurrentClientTarget = LoomleSetup::GetCurrentClientTarget();
const FString ClientFileName = CurrentClientTarget.StartsWith(TEXT("win32-")) ? TEXT("loomle.exe") : TEXT("loomle");
const FString BundledPath = FPaths::Combine(
    TEXT("/Engine/Plugins/Marketplace/LoomleBridge/Resources/Loomle"),
    CurrentClientTarget,
    ClientFileName);
const FString OtherBundledPath = FPaths::Combine(
    TEXT("/OtherEngine/Plugins/Marketplace/LoomleBridge/Resources/Loomle"),
    CurrentClientTarget,
    ClientFileName);
const FString LoomleHome = TEXT("/Users/test");

bool NoClientFiles(const FString&)
{
    return false;
}

bool OtherClientExists(const FString& Path)
{
    return Path.Equals(OtherBundledPath, ESearchCase::CaseSensitive);
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FLoomleSetupTargetTest,
    "Loomle.Setup.ClientTarget",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLoomleSetupTargetTest::RunTest(const FString& Parameters)
{
    TestEqual(TEXT("Mac ARM target"), LoomleSetup::MakeClientTarget(TEXT("darwin"), TEXT("arm64")), TEXT("darwin-arm64"));
    TestEqual(TEXT("Windows x64 target"), LoomleSetup::MakeClientTarget(TEXT("win32"), TEXT("x64")), TEXT("win32-x64"));
    TestEqual(TEXT("Linux x64 target"), LoomleSetup::MakeClientTarget(TEXT("linux"), TEXT("x64")), TEXT("linux-x64"));
    TestTrue(TEXT("UE platform spelling is not a Client target"), LoomleSetup::MakeClientTarget(TEXT("windows"), TEXT("x64")).IsEmpty());
    TestFalse(TEXT("Current supported build has a target"), CurrentClientTarget.IsEmpty());

    TestEqual(
        TEXT("Default Codex config uses the user home"),
        LoomleSetup::ResolveCodexConfigPath(TEXT("/Users/test"), FString()),
        FString(TEXT("/Users/test/.codex/config.toml")));
    TestEqual(
        TEXT("Absolute CODEX_HOME overrides the default"),
        LoomleSetup::ResolveCodexConfigPath(TEXT("/Users/test"), TEXT("/Users/test/custom/../codex")),
        FString(TEXT("/Users/test/codex/config.toml")));
    TestTrue(
        TEXT("Relative CODEX_HOME fails closed"),
        LoomleSetup::ResolveCodexConfigPath(TEXT("/Users/test"), TEXT("relative/codex")).IsEmpty());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FLoomleSetupCodexConfigTest,
    "Loomle.Setup.CodexConfigAssessment",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLoomleSetupCodexConfigTest::RunTest(const FString& Parameters)
{
    const LoomleSetup::FConfigAssessment Missing = LoomleSetup::AssessCodexConfig(
        TEXT("model = \"gpt\"\n"),
        BundledPath,
        true,
        LoomleHome,
        NoClientFiles);
    TestTrue(TEXT("Missing entry needs configuration"), Missing.bNeedsConfiguration);
    TestTrue(TEXT("Suggested Codex entry uses bundled command"), Missing.SuggestedText.Contains(BundledPath));
    TestFalse(TEXT("Assessment does not report a migration"), Missing.bNeedsMigration);

    const FString LegacyPython = TEXT(
        "model = \"gpt\"\n"
        "[mcp_servers.loomle]\n"
        "command = \"uv\"\n"
        "args = [\"--directory\", \"/Engine/Plugins/LoomleBridge/Resources/MCP\", \"run\", \"loomle_mcp_server.py\"]\n"
        "enabled = true\n"
        "[mcp_servers.loomle.env]\n"
        "LOOMLE_PROJECT_ROOT = \"/Project\"\n");
    const LoomleSetup::FConfigAssessment PythonMigration = LoomleSetup::AssessCodexConfig(
        LegacyPython,
        BundledPath,
        true,
        LoomleHome,
        NoClientFiles);
    TestTrue(TEXT("Exact Loomle 0.6 Python entry needs migration"), PythonMigration.bNeedsMigration);
    TestTrue(TEXT("Legacy Python is classified"), PythonMigration.ExistingKind == LoomleSetup::EClientEntryKind::LegacyPython);

    const FString QuotedCurrent = FString::Printf(
        TEXT("[mcp_servers.\"loomle\"] # generated\ncommand = \"%s\" # client\nargs = [\"mcp\"]\n"),
        *BundledPath);
    const LoomleSetup::FConfigAssessment KeptQuoted = LoomleSetup::AssessCodexConfig(
        QuotedCurrent,
        BundledPath,
        true,
        LoomleHome,
        NoClientFiles);
    TestTrue(TEXT("Quoted Loomle table is recognized as current"), KeptQuoted.ExistingKind == LoomleSetup::EClientEntryKind::Bundled);
    TestFalse(TEXT("Current entry needs no setup"), KeptQuoted.bNeedsConfiguration || KeptQuoted.bNeedsMigration || KeptQuoted.bBlocked);

    const FString QuotedNamespaceCurrent = FString::Printf(
        TEXT("[\"mcp_servers\".loomle]\ncommand = \"%s\"\nargs = [\"mcp\"]\n"),
        *BundledPath);
    const LoomleSetup::FConfigAssessment KeptQuotedNamespace = LoomleSetup::AssessCodexConfig(
        QuotedNamespaceCurrent,
        BundledPath,
        true,
        LoomleHome,
        NoClientFiles);
    TestTrue(TEXT("Quoted namespace table is recognized as current"), KeptQuotedNamespace.ExistingKind == LoomleSetup::EClientEntryKind::Bundled);
    TestFalse(TEXT("Quoted namespace table needs no setup"), KeptQuotedNamespace.bNeedsConfiguration || KeptQuotedNamespace.bNeedsMigration || KeptQuotedNamespace.bBlocked);

    const FString LiteralQuotedTableCurrent = FString::Printf(
        TEXT("['mcp_servers'.'loomle']\ncommand = \"%s\"\nargs = [\"mcp\"]\n"),
        *BundledPath);
    const LoomleSetup::FConfigAssessment KeptLiteralQuotedTable = LoomleSetup::AssessCodexConfig(
        LiteralQuotedTableCurrent,
        BundledPath,
        true,
        LoomleHome,
        NoClientFiles);
    TestTrue(TEXT("Literal-quoted table is recognized as current"), KeptLiteralQuotedTable.ExistingKind == LoomleSetup::EClientEntryKind::Bundled);
    TestFalse(TEXT("Literal-quoted table needs no setup"), KeptLiteralQuotedTable.bNeedsConfiguration || KeptLiteralQuotedTable.bNeedsMigration || KeptLiteralQuotedTable.bBlocked);

    const FString MultilineArgsCurrent = FString::Printf(
        TEXT("[mcp_servers.loomle]\ncommand = \"%s\"\nargs = [\n  \"mcp\", # trailing comma is valid\n]\n"),
        *BundledPath);
    const LoomleSetup::FConfigAssessment KeptMultilineArgs = LoomleSetup::AssessCodexConfig(
        MultilineArgsCurrent,
        BundledPath,
        true,
        LoomleHome,
        NoClientFiles);
    TestTrue(TEXT("Multiline args array is parsed as current"), KeptMultilineArgs.ExistingKind == LoomleSetup::EClientEntryKind::Bundled);
    TestFalse(TEXT("Valid multiline args are not blocked"), KeptMultilineArgs.bBlocked);

    const FString LiteralAndQuotedFields = FString::Printf(
        TEXT("[mcp_servers.loomle]\n\"command\" = '%s'\n'args' = ['mcp',]\n"),
        *BundledPath);
    const LoomleSetup::FConfigAssessment KeptLiteralAndQuoted = LoomleSetup::AssessCodexConfig(
        LiteralAndQuotedFields,
        BundledPath,
        true,
        LoomleHome,
        NoClientFiles);
    TestTrue(TEXT("Literal values and quoted field keys are current"), KeptLiteralAndQuoted.ExistingKind == LoomleSetup::EClientEntryKind::Bundled);
    TestFalse(TEXT("Literal values and inline trailing comma are not blocked"), KeptLiteralAndQuoted.bBlocked);

    const FString DescendantOnly = TEXT(
        "[mcp_servers.\"loomle\".env]\n"
        "LOOMLE_PROJECT_ROOT = \"/Project\"\n");
    const LoomleSetup::FConfigAssessment DescendantBlocked = LoomleSetup::AssessCodexConfig(
        DescendantOnly,
        BundledPath,
        true,
        LoomleHome,
        NoClientFiles);
    TestTrue(TEXT("Loomle descendant without a root entry fails closed"), DescendantBlocked.bBlocked);

    const FString MalformedTable = TEXT("[mcp_servers.\"loomle\"\ncommand = \"custom\"\n");
    const LoomleSetup::FConfigAssessment MalformedBlocked = LoomleSetup::AssessCodexConfig(
        MalformedTable,
        BundledPath,
        true,
        LoomleHome,
        NoClientFiles);
    TestTrue(TEXT("Malformed possible Loomle table fails closed"), MalformedBlocked.bBlocked);

    const FString MultilineBasicFakeCurrent = FString::Printf(
        TEXT("notes = \"\"\"\n[mcp_servers.loomle]\ncommand = \"%s\"\nargs = [\"mcp\"]\n\"\"\"\n"),
        *BundledPath);
    const LoomleSetup::FConfigAssessment BasicFakeCurrent = LoomleSetup::AssessCodexConfig(
        MultilineBasicFakeCurrent,
        BundledPath,
        true,
        LoomleHome,
        NoClientFiles);
    TestTrue(TEXT("Multiline basic string does not create a fake current entry"), BasicFakeCurrent.ExistingKind == LoomleSetup::EClientEntryKind::None);
    TestTrue(TEXT("Multiline basic string remains an unverified unrelated value"), BasicFakeCurrent.bSyntaxUnverified);

    const FString MultilineLiteralFakeMigration = TEXT(
        "notes = '''\n"
        "[mcp_servers.loomle]\n"
        "command = \"uv\"\n"
        "args = [\"--directory\", \"/Engine/Plugins/LoomleBridge/Resources/MCP\", \"run\", \"loomle_mcp_server.py\"]\n"
        "'''\n");
    const LoomleSetup::FConfigAssessment LiteralFakeMigration = LoomleSetup::AssessCodexConfig(
        MultilineLiteralFakeMigration,
        BundledPath,
        true,
        LoomleHome,
        NoClientFiles);
    TestTrue(TEXT("Multiline literal string does not create a fake migration"), LiteralFakeMigration.ExistingKind == LoomleSetup::EClientEntryKind::None);
    TestFalse(TEXT("Fake legacy text does not request migration"), LiteralFakeMigration.bNeedsMigration);

    const FString FourQuoteBasic = FString::Printf(
        TEXT("notes = \"\"\"\n[mcp_servers.loomle]\ncommand = \"%s\"\nargs = [\"mcp\"]\n\"\"\"\"\n"),
        *BundledPath);
    const LoomleSetup::FConfigAssessment FourQuoteBasicAssessment = LoomleSetup::AssessCodexConfig(
        FourQuoteBasic,
        BundledPath,
        true,
        LoomleHome,
        NoClientFiles);
    TestFalse(TEXT("Four-quote multiline basic closing is valid"), FourQuoteBasicAssessment.bBlocked);
    TestTrue(TEXT("Four-quote basic content does not create a fake entry"), FourQuoteBasicAssessment.ExistingKind == LoomleSetup::EClientEntryKind::None);

    const FString FourQuoteLiteral = TEXT(
        "notes = '''\n"
        "[mcp_servers.loomle]\n"
        "command = \"uv\"\n"
        "args = [\"--directory\", \"/Engine/Plugins/LoomleBridge/Resources/MCP\", \"run\", \"loomle_mcp_server.py\"]\n"
        "''''\n");
    const LoomleSetup::FConfigAssessment FourQuoteLiteralAssessment = LoomleSetup::AssessCodexConfig(
        FourQuoteLiteral,
        BundledPath,
        true,
        LoomleHome,
        NoClientFiles);
    TestFalse(TEXT("Four-quote multiline literal closing is valid"), FourQuoteLiteralAssessment.bBlocked);
    TestFalse(TEXT("Four-quote literal content does not request migration"), FourQuoteLiteralAssessment.bNeedsMigration);

    const TArray<FString> AlternativeShapes = {
        TEXT("mcp_servers.loomle.command = \"/custom/loomle\"\n"),
        TEXT("mcp_servers.loomle.command = \"/custom/loomle\"\nmcp_servers.loomle.args = [\"mcp\"]\n"),
        TEXT("[mcp_servers]\n\"loomle\".command = \"/custom/loomle\"\n"),
        TEXT("mcp_servers.loomle = { command = \"/custom/loomle\", args = [\"mcp\"] }\n"),
        TEXT("mcp_servers = { loomle = { command = \"/custom/loomle\", args = [\"mcp\"] } }\n"),
        TEXT("[mcp_servers]\nloomle = { command = \"/custom/loomle\", args = [\"mcp\"] }\n"),
        TEXT("[[mcp_servers]]\nname = \"loomle\"\n")
    };
    for (const FString& AlternativeShape : AlternativeShapes)
    {
        const LoomleSetup::FConfigAssessment AlternativeBlocked = LoomleSetup::AssessCodexConfig(
            AlternativeShape,
            BundledPath,
            true,
            LoomleHome,
            NoClientFiles);
        TestTrue(TEXT("Alternative Loomle TOML shape fails closed"), AlternativeBlocked.bBlocked);
        TestFalse(TEXT("Alternative Loomle TOML shape is not reported missing"), AlternativeBlocked.bNeedsConfiguration);
        TestTrue(TEXT("Blocked alternative receives no append suggestion"), AlternativeBlocked.SuggestedText.IsEmpty());
    }

    const LoomleSetup::FConfigAssessment MalformedUnrelated = LoomleSetup::AssessCodexConfig(
        TEXT("model = [\n"),
        BundledPath,
        true,
        LoomleHome,
        NoClientFiles);
    TestTrue(TEXT("Structurally invalid unrelated TOML fails closed"), MalformedUnrelated.bBlocked);

    const TArray<FString> ExplicitlyInvalidToml = {
        TEXT("model == 1\n"),
        TEXT("value = [{]}\n"),
        TEXT("model = 1\nmodel = 2\n"),
        TEXT("[features]\nflag = true\n[features]\nflag = false\n")
    };
    for (const FString& InvalidToml : ExplicitlyInvalidToml)
    {
        const LoomleSetup::FConfigAssessment InvalidAssessment = LoomleSetup::AssessCodexConfig(
            InvalidToml,
            BundledPath,
            true,
            LoomleHome,
            NoClientFiles);
        TestTrue(TEXT("Explicitly invalid TOML fails closed"), InvalidAssessment.bBlocked);
    }

    const LoomleSetup::FConfigAssessment UnverifiedUnrelated = LoomleSetup::AssessCodexConfig(
        TEXT("model = \"gpt\"\n"),
        BundledPath,
        true,
        LoomleHome,
        NoClientFiles);
    TestTrue(TEXT("Unsupported TOML is not reported as fully validated"), UnverifiedUnrelated.bSyntaxUnverified);
    TestTrue(TEXT("Unrecognized config still reports missing Loomle"), UnverifiedUnrelated.bNeedsConfiguration);

    const FString RepeatedArrayTableSubtables = TEXT(
        "[[profiles]]\n"
        "name = \"first\"\n"
        "[profiles.options]\n"
        "enabled = true\n"
        "[[profiles]]\n"
        "name = \"second\"\n"
        "[profiles.options]\n"
        "enabled = false\n");
    const LoomleSetup::FConfigAssessment ArrayTableAssessment = LoomleSetup::AssessCodexConfig(
        RepeatedArrayTableSubtables,
        BundledPath,
        true,
        LoomleHome,
        NoClientFiles);
    TestFalse(TEXT("Per-element AoT subtables are not falsely blocked"), ArrayTableAssessment.bBlocked);
    TestTrue(TEXT("Complex AoT scope remains conservatively unverified"), ArrayTableAssessment.bSyntaxUnverified);
    TestTrue(TEXT("AoT-only config has no recognized Loomle entry"), ArrayTableAssessment.bNeedsConfiguration);

    const FString LegacyGlobal = TEXT(
        "[mcp_servers.loomle]\n"
        "command = \"/Users/test/.loomle/bin/loomle\"\n"
        "args = [\"mcp\"]\n");
    const LoomleSetup::FConfigAssessment GlobalMigration = LoomleSetup::AssessCodexConfig(
        LegacyGlobal,
        BundledPath,
        true,
        LoomleHome,
        NoClientFiles);
    TestTrue(TEXT("Official global Client needs migration"), GlobalMigration.bNeedsMigration);
    TestTrue(TEXT("Official global Client is classified"), GlobalMigration.ExistingKind == LoomleSetup::EClientEntryKind::LegacyGlobal);

    const FString OtherCurrent = FString::Printf(
        TEXT("[mcp_servers.loomle]\ncommand = \"%s\"\nargs = [\"mcp\"]\n"),
        *OtherBundledPath);
    const LoomleSetup::FConfigAssessment KeptOther = LoomleSetup::AssessCodexConfig(
        OtherCurrent,
        BundledPath,
        true,
        LoomleHome,
        OtherClientExists);
    TestTrue(TEXT("Another installed engine Client is current"), KeptOther.ExistingKind == LoomleSetup::EClientEntryKind::Bundled);
    TestFalse(TEXT("Another installed engine Client needs no migration"), KeptOther.bNeedsMigration);

    const LoomleSetup::FConfigAssessment StaleOther = LoomleSetup::AssessCodexConfig(
        OtherCurrent,
        BundledPath,
        true,
        LoomleHome,
        NoClientFiles);
    TestTrue(TEXT("Missing exact other-engine path is stale"), StaleOther.ExistingKind == LoomleSetup::EClientEntryKind::StaleBundled);
    TestTrue(TEXT("Missing exact other-engine path needs migration"), StaleOther.bNeedsMigration);

    const FString RenamedPluginPath = FPaths::Combine(
        TEXT("/Engine/Plugins/Marketplace/RenamedPlugin/Resources/Loomle"),
        CurrentClientTarget,
        ClientFileName);
    const FString RenamedCurrent = FString::Printf(
        TEXT("[mcp_servers.loomle]\ncommand = \"%s\"\nargs = [\"mcp\"]\n"),
        *RenamedPluginPath);
    const LoomleSetup::FConfigAssessment ExactRenamedCurrent = LoomleSetup::AssessCodexConfig(
        RenamedCurrent,
        RenamedPluginPath,
        true,
        LoomleHome,
        NoClientFiles);
    TestTrue(TEXT("Exact current path wins over canonical layout checks"), ExactRenamedCurrent.ExistingKind == LoomleSetup::EClientEntryKind::Bundled);

    const FString NearLegacyPython = TEXT(
        "[mcp_servers.loomle]\n"
        "command = \"uv\"\n"
        "args = [\"--directory\", \"/Engine/Plugins/LoomleBridge/Resources/MCP\", \"run\", \"loomle_mcp_server.py\", \"--verbose\"]\n");
    const LoomleSetup::FConfigAssessment KeptNearLegacyPython = LoomleSetup::AssessCodexConfig(
        NearLegacyPython,
        BundledPath,
        true,
        LoomleHome,
        NoClientFiles);
    TestTrue(TEXT("A near-match Python command is manual"), KeptNearLegacyPython.ExistingKind == LoomleSetup::EClientEntryKind::Manual);
    TestFalse(TEXT("A near-match Python command is not claimed as 0.6"), KeptNearLegacyPython.bNeedsMigration);

    const FString RelativeBundled = FString::Printf(
        TEXT("[mcp_servers.loomle]\ncommand = \"Plugins/LoomleBridge/Resources/Loomle/%s/%s\"\nargs = [\"mcp\"]\n"),
        *CurrentClientTarget,
        *ClientFileName);
    const LoomleSetup::FConfigAssessment KeptRelativeBundled = LoomleSetup::AssessCodexConfig(
        RelativeBundled,
        BundledPath,
        true,
        LoomleHome,
        NoClientFiles);
    TestTrue(TEXT("Only absolute bundled commands are classified"), KeptRelativeBundled.ExistingKind == LoomleSetup::EClientEntryKind::Manual);

    const LoomleSetup::FConfigAssessment MissingPayload = LoomleSetup::AssessCodexConfig(
        LegacyPython,
        BundledPath,
        false,
        LoomleHome,
        NoClientFiles);
    TestTrue(TEXT("Missing new payload blocks migration guidance"), MissingPayload.bBlocked);

    const FString Duplicate = LegacyGlobal + LegacyGlobal;
    const LoomleSetup::FConfigAssessment Ambiguous = LoomleSetup::AssessCodexConfig(
        Duplicate,
        BundledPath,
        true,
        LoomleHome,
        NoClientFiles);
    TestTrue(TEXT("Duplicate Loomle sections are blocked"), Ambiguous.bBlocked);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FLoomleSetupClaudeConfigTest,
    "Loomle.Setup.ClaudeConfigAssessment",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLoomleSetupClaudeConfigTest::RunTest(const FString& Parameters)
{
    const FString LegacyPython = TEXT(
        "{\n"
        "  \"theme\": \"dark\",\n"
        "  \"mcpServers\": {\n"
        "    \"other\": { \"command\": \"other\" },\n"
        "    \"loomle\": {\n"
        "      \"command\": \"uv\",\n"
        "      \"args\": [\"--directory\", \"/Engine/Plugins/LoomleBridge/Resources/MCP\", \"run\", \"loomle_mcp_server.py\"]\n"
        "    }\n"
        "  }\n"
        "}\n");
    const LoomleSetup::FConfigAssessment Migration = LoomleSetup::AssessClaudeConfig(
        LegacyPython,
        BundledPath,
        true,
        LoomleHome,
        NoClientFiles);
    TestTrue(TEXT("Claude exact Loomle 0.6 entry needs migration"), Migration.bNeedsMigration);
    TestTrue(TEXT("Claude legacy Python is classified"), Migration.ExistingKind == LoomleSetup::EClientEntryKind::LegacyPython);
    TestTrue(TEXT("Suggested Claude entry uses bundled command"), Migration.SuggestedText.Contains(BundledPath));

    const FString OtherCurrent = FString::Printf(
        TEXT("{\"mcpServers\":{\"loomle\":{\"command\":\"%s\",\"args\":[\"mcp\"]}}}"),
        *OtherBundledPath);
    const LoomleSetup::FConfigAssessment KeptOther = LoomleSetup::AssessClaudeConfig(
        OtherCurrent,
        BundledPath,
        true,
        LoomleHome,
        OtherClientExists);
    TestTrue(TEXT("Claude keeps another installed engine Client"), KeptOther.ExistingKind == LoomleSetup::EClientEntryKind::Bundled);

    const LoomleSetup::FConfigAssessment Malformed = LoomleSetup::AssessClaudeConfig(
        TEXT("{not json"),
        BundledPath,
        true,
        LoomleHome,
        NoClientFiles);
    TestTrue(TEXT("Malformed Claude JSON is blocked"), Malformed.bBlocked);

    const LoomleSetup::FConfigAssessment Duplicate = LoomleSetup::AssessClaudeConfig(
        TEXT("{\"mcpServers\":{\"loomle\":{},\"loomle\":{}}}"),
        BundledPath,
        true,
        LoomleHome,
        NoClientFiles);
    TestTrue(TEXT("Duplicate Claude JSON keys are blocked"), Duplicate.bBlocked);

    const LoomleSetup::FConfigAssessment LargeInteger = LoomleSetup::AssessClaudeConfig(
        TEXT("{\"serial\":18446744073709551615}"),
        BundledPath,
        true,
        LoomleHome,
        NoClientFiles);
    TestFalse(TEXT("Unrelated large JSON numbers remain valid for read-only assessment"), LargeInteger.bBlocked);
    TestTrue(TEXT("Config without Loomle still needs setup"), LargeInteger.bNeedsConfiguration);
    return true;
}

#endif
