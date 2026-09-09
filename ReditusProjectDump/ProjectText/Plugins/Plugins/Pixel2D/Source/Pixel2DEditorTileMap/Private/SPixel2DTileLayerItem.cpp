// Copyright Epic Games, Inc. All Rights Reserved.

#include "SPixel2DTileLayerItem.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "EditorStyleSet.h"
#include "PaperTileLayer.h"
#include "ScopedTransaction.h"
#include "Widgets/Text/SInlineEditableTextBlock.h"
#include "Pixel2DMapStyle.h"

#define LOCTEXT_NAMESPACE "Pixel2D"

//////////////////////////////////////////////////////////////////////////
// SPixel2DTileLayerItem

void SPixel2DTileLayerItem::Construct(const FArguments& InArgs, int32 Index, class UPixel2DTileMap* InMap, FIsSelected InIsSelectedDelegate)
{
	MyMap = InMap;
	MyIndex = Index;

	static const FName EyeClosedBrushName("TileMapEditor.LayerEyeClosed");
	static const FName EyeOpenedBrushName("TileMapEditor.LayerEyeOpened");
	EyeClosed = FPixel2DMapStyle::Get()->GetBrush(EyeClosedBrushName);
	EyeOpened = FPixel2DMapStyle::Get()->GetBrush(EyeOpenedBrushName);

	LayerNameWidget = SNew(SInlineEditableTextBlock)
		.Text(this, &SPixel2DTileLayerItem::GetLayerDisplayName)
		.ToolTipText(this, &SPixel2DTileLayerItem::GetLayerDisplayName)
		.OnTextCommitted(this, &SPixel2DTileLayerItem::OnLayerNameCommitted)
		.IsSelected(InIsSelectedDelegate);

	ChildSlot
	[
		SNew(SHorizontalBox)
		+SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		[
			SAssignNew( VisibilityButton, SButton )
			.ContentPadding(FMargin(4.0f, 4.0f, 4.0f, 4.0f))
			.ButtonStyle( FEditorStyle::Get(), "NoBorder" )
			.OnClicked( this, &SPixel2DTileLayerItem::OnToggleVisibility )
			.ToolTipText( LOCTEXT("LayerVisibilityButtonToolTip", "Toggle Layer Visibility") )
			.ForegroundColor( FSlateColor::UseForeground() )
			.HAlign( HAlign_Center )
			.VAlign( VAlign_Center )
			.Content()
			[
				SNew(SImage)
				.Image(this, &SPixel2DTileLayerItem::GetVisibilityBrushForLayer)
				.ColorAndOpacity(this, &SPixel2DTileLayerItem::GetForegroundColorForVisibilityButton)
			]
		]
		+SHorizontalBox::Slot()
		.VAlign(VAlign_Center)
		.Padding(FMargin(4.0f, 4.0f, 4.0f, 4.0f))
		[
			LayerNameWidget.ToSharedRef()
		]
	];
}

void SPixel2DTileLayerItem::BeginEditingName()
{
	LayerNameWidget->EnterEditingMode();
}

FText SPixel2DTileLayerItem::GetLayerDisplayName() const
{
	const FText UnnamedText = LOCTEXT("NoLayerName", "(unnamed)");

	return GetMyLayer()->LayerName.IsEmpty() ? UnnamedText : GetMyLayer()->LayerName;
}

void SPixel2DTileLayerItem::OnLayerNameCommitted(const FText& NewText, ETextCommit::Type CommitInfo)
{
	const FScopedTransaction Transaction( LOCTEXT("TileMapRenameLayerTransaction", "Rename Layer") );
	UPaperTileLayer* MyLayer = GetMyLayer();
	MyLayer->SetFlags(RF_Transactional);
	MyLayer->Modify();
	MyLayer->LayerName = NewText;
}

FReply SPixel2DTileLayerItem::OnToggleVisibility()
{
	const FScopedTransaction Transaction( LOCTEXT("ToggleVisibilityTransaction", "Toggle Layer Visibility") );
	UPaperTileLayer* MyLayer = GetMyLayer();
	MyLayer->SetFlags(RF_Transactional);
	MyLayer->Modify();
	MyLayer->SetShouldRenderInEditor(!MyLayer->ShouldRenderInEditor());
	MyLayer->PostEditChange();
	return FReply::Handled();
}

const FSlateBrush* SPixel2DTileLayerItem::GetVisibilityBrushForLayer() const
{
	return GetMyLayer()->ShouldRenderInEditor() ? EyeOpened : EyeClosed;
}

FSlateColor SPixel2DTileLayerItem::GetForegroundColorForVisibilityButton() const
{
	static const FName InvertedForeground("InvertedForeground");
	return FEditorStyle::GetSlateColor(InvertedForeground);
}

//////////////////////////////////////////////////////////////////////////

#undef LOCTEXT_NAMESPACE
