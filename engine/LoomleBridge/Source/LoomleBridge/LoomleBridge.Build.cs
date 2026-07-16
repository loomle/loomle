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
                "PythonScriptPlugin",
                "Projects",
                "RHI",
                "Slate",
                "SlateCore",
                "SubobjectDataInterface",
                "ToolMenus",
                "UMG",
                "UMGEditor",
                "UnrealEd"
            });
    }
}
