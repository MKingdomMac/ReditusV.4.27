using UnrealBuildTool;

public class ReditusBuffVisuals : ModuleRules
{
    public ReditusBuffVisuals(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        PublicDependencyModuleNames.AddRange(new[] { "Core", "CoreUObject", "Engine", "Niagara" });
    }
}
