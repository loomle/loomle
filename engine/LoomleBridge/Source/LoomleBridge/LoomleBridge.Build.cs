// Copyright 2026 Loomle contributors.

using UnrealBuildTool;

public class LoomleBridge : ModuleRules
{
    public LoomleBridge(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        bUseUnity = false;

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
