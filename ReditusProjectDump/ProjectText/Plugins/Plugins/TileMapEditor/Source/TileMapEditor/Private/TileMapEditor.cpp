// Copyright Epic Games, Inc. All Rights Reserved.

#include "TileMapEditor.h"
#include "TileMapEditorStyle.h"
#include "TileMapEditorCommands.h"
#include "ToolMenus.h"

#include "TileMapEdMode.h"
#include "EditorModeRegistry.h"
#include "EditorModeManager.h"

#define LOCTEXT_NAMESPACE "FTileMapEditorModule"

void FTileMapEditorModule::StartupModule()
{
	FTileMapEditorStyle::Initialize();
	FTileMapEditorStyle::ReloadTextures();

	FTileMapEditorCommands::Register();

	FEditorModeRegistry::Get().RegisterMode<FTileMapEdMode>(
		FTileMapEdMode::EM_TileMapEdModeId,
		LOCTEXT("TileMapEdModeName", "Tile Map"),
		FSlateIcon(),
		true
	);

	PluginCommands = MakeShareable(new FUICommandList);

	PluginCommands->MapAction(
		FTileMapEditorCommands::Get().PluginAction,
		FExecuteAction::CreateRaw(
			this,
			&FTileMapEditorModule::PluginButtonClicked
		),
		FCanExecuteAction()
	);

	UToolMenus::RegisterStartupCallback(
		FSimpleMulticastDelegate::FDelegate::CreateRaw(
			this,
			&FTileMapEditorModule::RegisterMenus
		)
	);
}

void FTileMapEditorModule::ShutdownModule()
{
	FEditorModeRegistry::Get().UnregisterMode(
		FTileMapEdMode::EM_TileMapEdModeId
	);

	UToolMenus::UnRegisterStartupCallback(this);
	UToolMenus::UnregisterOwner(this);

	FTileMapEditorStyle::Shutdown();
	FTileMapEditorCommands::Unregister();
}

void FTileMapEditorModule::PluginButtonClicked()
{
	GLevelEditorModeTools().ActivateMode(
		FTileMapEdMode::EM_TileMapEdModeId
	);
}

void FTileMapEditorModule::RegisterMenus()
{
	FToolMenuOwnerScoped OwnerScoped(this);

	{
		UToolMenu* Menu =
			UToolMenus::Get()->ExtendMenu(
				"LevelEditor.MainMenu.Window"
			);

		FToolMenuSection& Section =
			Menu->FindOrAddSection("WindowLayout");

		Section.AddMenuEntryWithCommandList(
			FTileMapEditorCommands::Get().PluginAction,
			PluginCommands
		);
	}

	{
		UToolMenu* ToolbarMenu =
			UToolMenus::Get()->ExtendMenu(
				"LevelEditor.LevelEditorToolBar"
			);

		FToolMenuSection& Section =
			ToolbarMenu->FindOrAddSection("Settings");

		FToolMenuEntry& Entry =
			Section.AddEntry(
				FToolMenuEntry::InitToolBarButton(
					FTileMapEditorCommands::Get().PluginAction
				)
			);

		Entry.SetCommandList(PluginCommands);
	}
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FTileMapEditorModule, TileMapEditor)