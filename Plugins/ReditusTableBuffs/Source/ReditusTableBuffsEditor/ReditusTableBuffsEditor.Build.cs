using UnrealBuildTool;

public class ReditusTableBuffsEditor : ModuleRules
{
    public ReditusTableBuffsEditor(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        PrivateDependencyModuleNames.AddRange(new[] {
            "Core", "CoreUObject", "Engine", "ReditusTableBuffs", "ReditusBuffVisuals",
            "UnrealEd", "AssetRegistry", "AssetTools", "BlueprintGraph", "KismetCompiler"
        });
    }
}
