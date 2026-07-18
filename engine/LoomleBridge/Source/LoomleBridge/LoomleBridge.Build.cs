// Copyright 2026 Loomle contributors.

using UnrealBuildTool;

public class LoomleBridge : ModuleRules
{
    public LoomleBridge(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

        PublicDependencyModuleNames.AddRange(
            new[]
            {
                "Core",
                "CoreUObject",
                "Engine"
            });

        PrivateDependencyModuleNames.AddRange(
            new[]
            {
                "ApplicationCore",
                "AssetDefinition",
                "AssetRegistry",
                "AssetTools",
                "BlueprintGraph",
                "ContentBrowser",
                "ContentBrowserData",
                "GraphEditor",
                "Json",
                "JsonUtilities",
                "Kismet",
                "KismetCompiler",
                "LevelEditor",
                "MainFrame",
                "MaterialEditor",
                "MovieScene",
                "PCG",
                "PCGEditor",
                "PropertyEditor",
                "PythonScriptPlugin",
                "Projects",
                "RHI",
                "Slate",
                "SlateCore",
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
