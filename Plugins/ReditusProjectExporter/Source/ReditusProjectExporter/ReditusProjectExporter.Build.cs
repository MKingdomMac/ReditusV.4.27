using UnrealBuildTool;

public class ReditusProjectExporter : ModuleRules
{
    public ReditusProjectExporter(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

        PublicDependencyModuleNames.AddRange(
            new string[]
            {
                "Core"
            }
        );

        PrivateDependencyModuleNames.AddRange(
            new string[]
            {
                "CoreUObject",
                "Engine",
                "UnrealEd",
                "LevelEditor",
                "ToolMenus",
                "AssetRegistry",
                "Json",
                "Slate",
                "SlateCore",
                "Projects"
            }
        );
    }
}
