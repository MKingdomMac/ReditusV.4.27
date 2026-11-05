using UnrealBuildTool;

public class ReditusRollingHPEditorBridge : ModuleRules
{
    public ReditusRollingHPEditorBridge(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage=PCHUsageMode.UseExplicitOrSharedPCHs;
        PrivateDependencyModuleNames.AddRange(new string[] {
            "Core", "CoreUObject", "Engine", "UnrealEd", "BlueprintGraph", "KismetCompiler"
        });
    }
}
