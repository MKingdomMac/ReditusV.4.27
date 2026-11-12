using UnrealBuildTool;
public class ReditusEnemyDeathEditor : ModuleRules
{
    public ReditusEnemyDeathEditor(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        PrivateDependencyModuleNames.AddRange(new[] { "Core", "CoreUObject", "Engine", "Paper2D", "ReditusEnemyDeath", "UnrealEd", "AssetRegistry", "BlueprintGraph", "KismetCompiler" });
    }
}
