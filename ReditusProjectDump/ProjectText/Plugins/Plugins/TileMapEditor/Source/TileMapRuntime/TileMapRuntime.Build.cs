// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class TileMapRuntime : ModuleRules
{
    public TileMapRuntime(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage =
            ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

        PublicDependencyModuleNames.AddRange(
            new string[]
            {
                "Core",
                "CoreUObject",
                "Engine",
                "ProceduralMeshComponent"
            }
        );
    }
}
