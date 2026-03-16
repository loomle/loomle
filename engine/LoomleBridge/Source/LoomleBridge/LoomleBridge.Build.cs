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
                "MainFrame",
                "MaterialEditor",
                "PCG",
                "PCGEditor",
                "PythonScriptPlugin",
                "Projects",
                "Slate",
                "SlateCore",
                "UMG",
                "UnrealEd"
            });
    }
}
