// Copyright 1998-2017 Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class Pixel2DEditorTileMap : ModuleRules
{
	public Pixel2DEditorTileMap(ReadOnlyTargetRules Target) : base(Target)
	{
        PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

        PrivateIncludePaths.Add("Pixel2DEditorTileMap/Private");
        PrivateIncludePaths.Add("Pixel2D/Classes");

        PrivateDependencyModuleNames.AddRange(
			new string[] {
				"Core",
				"CoreUObject",
				"ApplicationCore",
				"Json",
				"Slate",
				"SlateCore",
				"Engine",
				"EditorStyle",
				"EditorWidgets",
				"Kismet",
				"KismetWidgets",
				"InputCore",
				"UnrealEd", // for FAssetEditorManager
				"PropertyEditor",
				"NavigationSystem",
				"RenderCore",
				"Paper2D",
                "Paper2DEditor",
				"Projects",
                "Pixel2D"
			});

		PrivateIncludePathModuleNames.AddRange(
			new string[] {
				"Settings",
				"IntroTutorials",
				"AssetTools",
				"LevelEditor",
				"Paper2D"
            });

	}
}
