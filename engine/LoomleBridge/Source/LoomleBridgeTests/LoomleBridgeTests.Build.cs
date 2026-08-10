// Copyright 2026 Loomle contributors.

using System.IO;
using UnrealBuildTool;

public class LoomleBridgeTests : ModuleRules
{
    public LoomleBridgeTests(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        bUseUnity = false;

        PrivateIncludePaths.Add(
            Path.Combine(ModuleDirectory, "..", "LoomleBridge", "Private"));

        PrivateDependencyModuleNames.AddRange(
            new[]
            {
                "ApplicationCore",
                "AssetRegistry",
                "BlueprintGraph",
                "ContentBrowser",
                "ContentBrowserData",
                "Core",
                "CoreUObject",
                "Engine",
                "GraphEditor",
                "Json",
                "Kismet",
                "KismetCompiler",
                "LevelEditor",
                "LoomleBridge",
                "MovieScene",
                "PCG",
                "PropertyBindingUtils",
                "PropertyEditor",
                "Projects",
                "PythonScriptPlugin",
                "Slate",
                "SlateCore",
                "StateTreeEditorModule",
                "StateTreeModule",
                "SubobjectDataInterface",
                "SubobjectEditor",
                "ToolMenus",
                "TypedElementRuntime",
                "UMG",
                "UMGEditor",
                "UnrealEd"
            });
    }
}
