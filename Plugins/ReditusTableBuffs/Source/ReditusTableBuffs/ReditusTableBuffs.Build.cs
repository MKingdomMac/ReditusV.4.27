using UnrealBuildTool;

public class ReditusTableBuffs : ModuleRules
{
    public ReditusTableBuffs(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        PublicDependencyModuleNames.AddRange(new[] { "Core", "CoreUObject", "Engine", "ReditusBuffVisuals" });
        PrivateDependencyModuleNames.Add("Niagara");
    }
}
