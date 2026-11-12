using UnrealBuildTool;
public class ReditusEnemyDeath : ModuleRules
{
    public ReditusEnemyDeath(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        PublicDependencyModuleNames.AddRange(new[] { "Core", "CoreUObject", "Engine", "Paper2D", "GameplayCameras" });
    }
}
