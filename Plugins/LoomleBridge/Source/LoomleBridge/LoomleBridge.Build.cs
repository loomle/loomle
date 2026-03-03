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
                "AssetRegistry",
                "BlueprintGraph",
                "Json",
                "JsonUtilities",
                "Kismet",
                "MaterialEditor",
                "PythonScriptPlugin",
                "Projects",
                "Slate",
                "SlateCore",
                "UnrealEd"
            });
    }
}
