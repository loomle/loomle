// Copyright 2026 Loomle contributors.

using UnrealBuildTool;

public class LoomleBridge : ModuleRules
{
    public LoomleBridge(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

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
                "Json",
                "Kismet",
                "KismetCompiler",
                "LevelEditor",
                "MovieScene",
                "PropertyBindingUtils",
                "PropertyEditor",
                "Projects",
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
