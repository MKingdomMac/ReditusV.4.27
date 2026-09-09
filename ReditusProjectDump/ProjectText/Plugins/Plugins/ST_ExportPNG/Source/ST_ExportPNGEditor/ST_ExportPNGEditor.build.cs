// Copyright (C) James Baxter. All Rights Reserved.

using UnrealBuildTool;
using System.IO;

public class ST_ExportPNGEditor : ModuleRules
{
    public ST_ExportPNGEditor(ReadOnlyTargetRules ROTargetRules) : base(ROTargetRules)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        bEnforceIWYU = true;

		PublicIncludePaths.Add(Path.Combine(ModuleDirectory, "Public"));
		PrivateIncludePaths.Add(Path.Combine(ModuleDirectory, "Private"));

		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "UnrealEd" });
    }
}