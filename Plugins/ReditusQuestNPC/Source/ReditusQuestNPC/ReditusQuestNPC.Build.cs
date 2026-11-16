using UnrealBuildTool;

public class ReditusQuestNPC : ModuleRules
{
    public ReditusQuestNPC(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        PublicDependencyModuleNames.AddRange(new string[] {
            "Core", "CoreUObject", "Engine", "UMG", "Paper2D", "DialoguePlugin", "MinimapPlugin", "LevelSequence"
        });
        PrivateDependencyModuleNames.AddRange(new string[] { "Slate", "SlateCore", "InputCore", "MovieScene" });
    }
}
