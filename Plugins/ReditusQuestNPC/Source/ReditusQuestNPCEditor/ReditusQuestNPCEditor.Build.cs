using UnrealBuildTool;

public class ReditusQuestNPCEditor : ModuleRules
{
    public ReditusQuestNPCEditor(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        PrivateDependencyModuleNames.AddRange(new string[] {
            "Core", "CoreUObject", "Engine", "UnrealEd", "AssetRegistry", "BlueprintGraph",
            "KismetCompiler", "Paper2D", "UMG", "UMGEditor", "DialoguePlugin", "MinimapPlugin", "ReditusQuestNPC", "Json",
            "AssetTools", "GraphEditor", "PropertyEditor", "Slate", "SlateCore", "InputCore"
        });
    }
}
