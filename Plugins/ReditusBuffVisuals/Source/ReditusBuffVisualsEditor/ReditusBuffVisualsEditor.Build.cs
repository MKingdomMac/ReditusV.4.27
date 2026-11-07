using UnrealBuildTool;

public class ReditusBuffVisualsEditor : ModuleRules
{
    public ReditusBuffVisualsEditor(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        PrivateDependencyModuleNames.AddRange(new[] {
            "Core", "CoreUObject", "Engine", "Niagara", "UnrealEd", "AssetRegistry",
            "BlueprintGraph", "KismetCompiler", "ReditusBuffVisuals"
        });
    }
}
