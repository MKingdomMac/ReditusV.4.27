using UnrealBuildTool;

public class ReditusBlueprintRepairEditor : ModuleRules
{
    public ReditusBlueprintRepairEditor(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        PrivateDependencyModuleNames.AddRange(new[]
        {
            "Core", "CoreUObject", "Engine", "UnrealEd", "BlueprintGraph", "KismetCompiler",
            "UMG", "UMGEditor", "SlateCore"
        });
    }
}
