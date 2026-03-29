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
                "BlueprintGraph",
                "Json",
                "JsonUtilities",
                "Kismet",
                "LevelEditor",
                "MainFrame",
                "MaterialEditor",
                "PCG",
                "PCGEditor",
                "PythonScriptPlugin",
                "Projects",
                "RHI",
                "Slate",
                "SlateCore",
                "ToolMenus",
                "UMG",
                "UnrealEd"
            });
    }
}
