// Copyright Epic Games, Inc. All Rights Reserved.

#include "SPixel2DEditorViewport.h"
#include "Rendering/DrawElements.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SViewport.h"
#include "Application/ThrottleManager.h"
#include "Framework/Application/SlateApplication.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Text/STextBlock.h"
#include "EditorStyleSet.h"
#include "Editor.h"
#include "Pixel2DMapStyle.h"

const int32 DefaultZoomLevel = 7;
const int32 NumZoomLevels = 17;

struct FZoomLevelEntry
{
	FZoomLevelEntry(float InZoomAmount, const FText& InDisplayText)
		: DisplayText(FText::Format(NSLOCTEXT("Pixel2DEditor", "Zoom", "Zoom {0}"), InDisplayText))
		, ZoomAmount(InZoomAmount)
	{
	}

	FText DisplayText;
	float ZoomAmount;
};

static const FZoomLevelEntry& GetZoomLevel(int Index)
{
	static const FZoomLevelEntry ZoomLevels[NumZoomLevels] =
	{
		FZoomLevelEntry(0.03125f, NSLOCTEXT("Pixel2DEditor", "ZoomLevel_1_32", "1:32")),
		FZoomLevelEntry(0.0625f, NSLOCTEXT("Pixel2DEditor", "ZoomLevel_1_16", "1:16")),
		FZoomLevelEntry(0.125f, NSLOCTEXT("Pixel2DEditor", "ZoomLevel_1_8", "1:8")),
		FZoomLevelEntry(0.250f, NSLOCTEXT("Pixel2DEditor", "ZoomLevel_1_4", "1:4")),
		FZoomLevelEntry(0.500f, NSLOCTEXT("Pixel2DEditor", "ZoomLevel_1_2", "1:2")),
		FZoomLevelEntry(0.750f, NSLOCTEXT("Pixel2DEditor", "ZoomLevel_3_4", "3:4")),
		FZoomLevelEntry(0.875f, NSLOCTEXT("Pixel2DEditor", "ZoomLevel_7_8", "7:8")),
		FZoomLevelEntry(1.000f, NSLOCTEXT("Pixel2DEditor", "ZoomLevel_1_1", "1:1")),
		FZoomLevelEntry(2.000f, NSLOCTEXT("Pixel2DEditor", "ZoomLevel_2x", "2x")),
		FZoomLevelEntry(3.000f, NSLOCTEXT("Pixel2DEditor", "ZoomLevel_3x", "3x")),
		FZoomLevelEntry(4.500f, NSLOCTEXT("Pixel2DEditor", "ZoomLevel_4x", "4x")),
		FZoomLevelEntry(5.000f, NSLOCTEXT("Pixel2DEditor", "ZoomLevel_5x", "5x")),
		FZoomLevelEntry(6.000f, NSLOCTEXT("Pixel2DEditor", "ZoomLevel_6x", "6x")),
		FZoomLevelEntry(7.000f, NSLOCTEXT("Pixel2DEditor", "ZoomLevel_7x", "7x")),
		FZoomLevelEntry(8.000f, NSLOCTEXT("Pixel2DEditor", "ZoomLevel_8x", "8x")),
		FZoomLevelEntry(16.000f, NSLOCTEXT("Pixel2DEditor", "ZoomLevel_16x", "16x")),
		FZoomLevelEntry(32.000f, NSLOCTEXT("Pixel2DEditor", "ZoomLevel_32x", "32x"))
	};
	return ZoomLevels[Index];
}

//////////////////////////////////////////////////////////////////////////
// SPixel2DEditorViewport

SPixel2DEditorViewport::~SPixel2DEditorViewport()
{
	Pixel2DViewportClient.Reset();
}

TSharedRef<FEditorViewportClient> SPixel2DEditorViewport::MakeEditorViewportClient()
{
	return Pixel2DViewportClient.ToSharedRef();
}

void SPixel2DEditorViewport::Construct(const FArguments& InArgs, TSharedRef<FPixel2DEditorViewportClient> InViewportClient)
{
	OnSelectionChanged = InArgs._OnSelectionChanged;

	Pixel2DViewportClient = InViewportClient;
	Pixel2DViewportClient->SetRealtime(false);

	SEditorViewport::Construct(SEditorViewport::FArguments());

	// Indicator of current zoom level
	ViewportOverlay->AddSlot()
		.Padding(5)
		.VAlign(VAlign_Top)
		[
			SNew(STextBlock)
			.TextStyle(FPixel2DMapStyle::Get(), "Pixel2D.Common.ViewportZoomTextStyle")
			.Text(this, &SPixel2DEditorViewport::GetZoomText)
			.ColorAndOpacity(this, &SPixel2DEditorViewport::GetZoomTextColorAndOpacity)
		];

	// Title for the viewport
	ViewportOverlay->AddSlot()
		.VAlign(VAlign_Top)
		[
			SNew(SBorder)
			.BorderImage(FPixel2DMapStyle::Get()->GetBrush("Paper2D.Common.ViewportTitleBackground"))
			.HAlign(HAlign_Fill)
			.Visibility(EVisibility::HitTestInvisible)
			[
				SNew(SVerticalBox)
				// Title text/icon
				+SVerticalBox::Slot()
				.AutoHeight()
				[
					SNew(SHorizontalBox)
					+SHorizontalBox::Slot()
					.HAlign(HAlign_Center)
					.FillWidth(1.f)
					[
						SNew(STextBlock)
						.TextStyle(FPixel2DMapStyle::Get(), "Paper2D.Common.ViewportTitleTextStyle")
						.Text(this, &SPixel2DEditorViewport::GetTitleText)
					]
				]
			]
		];

	ZoomLevel = DefaultZoomLevel;
	PreviousZoomLevel = DefaultZoomLevel;
	ViewOffset = FVector2D::ZeroVector;
	TotalMouseDelta = 0;

	bIsPanning = false;

	ZoomLevelFade = FCurveSequence( 0.0f, 0.75f );
	ZoomLevelFade.Play(this->AsShared());

	DeferredPanPosition = FVector2D::ZeroVector;
	bRequestDeferredPan = false;
}

void SPixel2DEditorViewport::Tick( const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime )
{
	// Handle any deferred panning
	if (bRequestDeferredPan)
	{
		bRequestDeferredPan = false;
		UpdateViewOffset(AllottedGeometry, DeferredPanPosition);
	}

	if (!HasMouseCapture())
	{
		bShowSoftwareCursor = false;
		bIsPanning = false;
	}

	Pixel2DViewportClient->SetZoomPos( ViewOffset, GetZoomAmount() );
	Pixel2DViewportClient->bNeedsRedraw = true;

	bool bSelectionModified = false;
	if (Marquee.IsValid())
	{
		bSelectionModified = true;

		OnSelectionChanged.ExecuteIfBound(Marquee, true);
	}

	if (bSelectionModified || bIsPanning || FSlateThrottleManager::Get().IsAllowingExpensiveTasks())
	{
		// Setup the selection set for the viewport
		Pixel2DViewportClient->SelectionRectangles.Empty();

		if (Marquee.IsValid())
		{
			FViewportSelectionRectangle& Rect = *(new (Pixel2DViewportClient->SelectionRectangles) FViewportSelectionRectangle);
			Rect.Color = FColorList::Yellow;
			Rect.Color.A = 0.45f;
			Rect.TopLeft = Marquee.Rect.GetUpperLeft();
			Rect.Dimensions = Marquee.Rect.GetSize();
		}

		// Tick and render the viewport
		Pixel2DViewportClient->Tick(InDeltaTime);
		GEditor->UpdateSingleViewportClient(Pixel2DViewportClient.Get(), /*bInAllowNonRealtimeViewportToDraw=*/ true, /*bLinkedOrthoMovement=*/ false);
	}
}

FReply SPixel2DEditorViewport::OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	TotalMouseDelta = 0;

	if (MouseEvent.GetEffectingButton() == EKeys::RightMouseButton)
	{
		// RIGHT BUTTON is for dragging and Context Menu.
		FReply ReplyState = FReply::Handled();
		ReplyState.CaptureMouse( SharedThis(this) );
		ReplyState.UseHighPrecisionMouseMovement( SharedThis(this) );

		SoftwareCursorPosition = PanelCoordToGraphCoord( MyGeometry.AbsoluteToLocal( MouseEvent.GetScreenSpacePosition() ) * MyGeometry.Scale );

		// clear any interpolation when you manually pan
		//DeferredMovementTargetObject = nullptr;

		return ReplyState;
	}
	else if (MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
	{
		// START MARQUEE SELECTION.
		const FVector2D GraphMousePos = PanelCoordToGraphCoord( MyGeometry.AbsoluteToLocal( MouseEvent.GetScreenSpacePosition() ) * MyGeometry.Scale );
		Marquee.Start( GraphMousePos, FMarqueeOperation::OperationTypeFromMouseEvent(MouseEvent) );

		// Trigger a selection update now so that single-clicks without a drag still select something
		OnSelectionChanged.ExecuteIfBound(Marquee, true);
		Pixel2DViewportClient->Invalidate();

		return FReply::Handled().CaptureMouse( SharedThis(this) );
	}
	else
	{
		return FReply::Unhandled();
	}
}

FReply SPixel2DEditorViewport::OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	// Did the user move the cursor sufficiently far, or is it in a dead zone?
	// In Dead zone     - implies actions like summoning context menus and general clicking.
	// Out of Dead Zone - implies dragging actions like moving nodes and marquee selection.
	const bool bCursorInDeadZone = TotalMouseDelta <= FSlateApplication::Get().GetDragTriggerDistance();

	if (MouseEvent.GetEffectingButton() == EKeys::RightMouseButton)
	{
		FReply ReplyState = FReply::Handled();

		if (HasMouseCapture())
		{
			FSlateRect ThisPanelScreenSpaceRect = MyGeometry.GetLayoutBoundingRect();
			const FVector2D ScreenSpaceCursorPos = MyGeometry.LocalToAbsolute( GraphCoordToPanelCoord( SoftwareCursorPosition ) / MyGeometry.Scale );

			FIntPoint BestPositionInViewport(
				FMath::RoundToInt( FMath::Clamp( ScreenSpaceCursorPos.X, ThisPanelScreenSpaceRect.Left, ThisPanelScreenSpaceRect.Right ) ),
				FMath::RoundToInt( FMath::Clamp( ScreenSpaceCursorPos.Y, ThisPanelScreenSpaceRect.Top, ThisPanelScreenSpaceRect.Bottom ) )
				);

			if (!bCursorInDeadZone)
			{
				ReplyState.SetMousePos(BestPositionInViewport);
			}
		}

		TSharedPtr<SWidget> WidgetToFocus;
		if (bCursorInDeadZone)
		{
			//WidgetToFocus = OnSummonContextMenu(MyGeometry, MouseEvent);
		}

		bShowSoftwareCursor = false;

		bIsPanning = false;
		return (WidgetToFocus.IsValid())
			? ReplyState.ReleaseMouseCapture().SetUserFocus(WidgetToFocus.ToSharedRef(), EFocusCause::SetDirectly)
			: ReplyState.ReleaseMouseCapture();
	}
	else if (MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
	{
		if (OnHandleLeftMouseRelease(MyGeometry, MouseEvent))
		{

		}
		else if (bCursorInDeadZone)
		{
			//@TODO: Move to selection manager
// 			if ( NodeUnderMousePtr.IsValid() )
// 			{
// 				// We clicked on a node!
// 				TSharedRef<SNode> NodeWidgetUnderMouse = NodeUnderMousePtr.Pin().ToSharedRef();
// 
// 				SelectionManager.ClickedOnNode(NodeWidgetUnderMouse->GetObjectBeingDisplayed(), MouseEvent);
// 
// 				// We're done interacting with this node.
// 				NodeUnderMousePtr.Reset();
// 			}
// 			else 
			if (HasMouseCapture())
 			{
 				// We clicked on the panel background

				//@TODO: There isn't a marquee operation for clear, need to signal to remove existing sets too!
				//Marquee.End();
 				//OnSelectionChanged.ExecuteIfBound(Marquee, false);
 			}
		}
		else if (Marquee.IsValid())
		{
			//ApplyMarqueeSelection(Marquee, SelectionManager.SelectedNodes, SelectionManager.SelectedNodes);
			//SelectionManager.OnSelectionChanged.ExecuteIfBound(SelectionManager.SelectedNodes);
			OnSelectionChanged.ExecuteIfBound(Marquee, true);
		}

		// The existing marquee operation ended; reset it.
		Marquee = FMarqueeOperation();

		if (bIsPanning)
		{
			// We have released the left mouse button. But we're still panning
			// (i.e. RIGHT MOUSE is down), so we want to hold on to capturing mouse input.
			return FReply::Handled();
		}
		else
		{
			// We aren't panning, so we can release the mouse capture.
			return FReply::Handled().ReleaseMouseCapture();
		}	
	}

	return FReply::Unhandled();	
}

FReply SPixel2DEditorViewport::OnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	const bool bIsRightMouseButtonDown = MouseEvent.IsMouseButtonDown(EKeys::RightMouseButton);
	const bool bIsLeftMouseButtonDown = MouseEvent.IsMouseButtonDown(EKeys::LeftMouseButton);
	
	if (HasMouseCapture())
	{
		// Track how much the mouse moved since the mouse down.
		const FVector2D CursorDelta = MouseEvent.GetCursorDelta();
		TotalMouseDelta += CursorDelta.Size();

		if (bIsRightMouseButtonDown)
		{
			FReply ReplyState = FReply::Handled();

			if (!CursorDelta.IsZero())
			{
				bShowSoftwareCursor = true;
			}

			bIsPanning = true;
			ViewOffset -= CursorDelta / GetZoomAmount();

			return ReplyState;
		}
		else if (bIsLeftMouseButtonDown)
		{
//			TSharedPtr<SNode> NodeBeingDragged = NodeUnderMousePtr.Pin();

			// Update the amount to pan panel
			UpdateViewOffset(MyGeometry, MouseEvent.GetScreenSpacePosition());

			const bool bCursorInDeadZone = TotalMouseDelta <= FSlateApplication::Get().GetDragTriggerDistance();

			{
				// We are marquee selecting
				const FVector2D GraphMousePos = PanelCoordToGraphCoord( MyGeometry.AbsoluteToLocal( MouseEvent.GetScreenSpacePosition() ) * MyGeometry.Scale );
				Marquee.Rect.UpdateEndPoint(GraphMousePos);

				return FReply::Handled();
			}
		}
	}

	return FReply::Unhandled();
}

FReply SPixel2DEditorViewport::OnMouseWheel(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	// We want to zoom into this point; i.e. keep it the same fraction offset into the panel
	const FVector2D WidgetSpaceCursorPos = MyGeometry.AbsoluteToLocal( MouseEvent.GetScreenSpacePosition() ) * MyGeometry.Scale;
	FVector2D PointToMaintainGraphSpace = PanelCoordToGraphCoord( WidgetSpaceCursorPos );


	const int32 ZoomLevelDelta = FMath::FloorToInt(MouseEvent.GetWheelDelta());

	const bool bAllowFullZoomRange = true;
	const float OldZoomLevel = ZoomLevel;

	if (bAllowFullZoomRange)
	{
		ZoomLevel = FMath::Clamp( ZoomLevel + ZoomLevelDelta, 0, NumZoomLevels-1 );
	}
	else
	{
		// Without control, we do not allow zooming out past 1:1.
		ZoomLevel = FMath::Clamp( ZoomLevel + ZoomLevelDelta, 0, DefaultZoomLevel );
	}

	ZoomLevelFade.Play(this->AsShared());


	// Re-center the screen so that it feels like zooming around the cursor.
	ViewOffset = PointToMaintainGraphSpace - WidgetSpaceCursorPos / GetZoomAmount();

	return FReply::Handled();
}

FCursorReply SPixel2DEditorViewport::OnCursorQuery( const FGeometry& MyGeometry, const FPointerEvent& CursorEvent ) const
{
	return bShowSoftwareCursor ? 
		FCursorReply::Cursor( EMouseCursor::None ) :
		FCursorReply::Cursor( EMouseCursor::Default );
}

int32 SPixel2DEditorViewport::FindNearestZoomLevel(float InZoomAmount, bool bRoundDown) const
{
	bool bFoundItem = false;
	int32 Result = 0;

	for (int32 ZoomLevelIndex=0; ZoomLevelIndex < NumZoomLevels; ++ZoomLevelIndex)
	{
		if (InZoomAmount <= GetZoomLevel(ZoomLevelIndex).ZoomAmount)
		{
			Result = ZoomLevelIndex;
			bFoundItem = true;
			break;
		}
	}

	if (!bFoundItem)
	{
		return DefaultZoomLevel;
	}
	else
	{
		if (bFoundItem && bRoundDown && (Result > 0))
		{
			return Result - 1;
		}
		else
		{
			return Result;
		}
	}
}

float SPixel2DEditorViewport::GetZoomAmount() const
{
	return GetZoomLevel(ZoomLevel).ZoomAmount;
}

FText SPixel2DEditorViewport::GetZoomText() const
{
	return GetZoomLevel(ZoomLevel).DisplayText;
}

FSlateColor SPixel2DEditorViewport::GetZoomTextColorAndOpacity() const
{
	return FLinearColor( 1, 1, 1, 1.25f - ZoomLevelFade.GetLerp() * 0.75f );
}

FVector2D SPixel2DEditorViewport::GetViewOffset() const
{
	return ViewOffset;
}

FVector2D SPixel2DEditorViewport::ComputeEdgePanAmount(const FGeometry& MyGeometry, const FVector2D& TargetPosition)
{
	// How quickly to ramp up the pan speed as the user moves the mouse further past the edge of the graph panel.
	static const float EdgePanSpeedCoefficient = 0.1f;

	// Never pan slower than this, it's just unpleasant.
	static const float MinPanSpeed = 5.0f;

	// Start panning before we reach the edge of the graph panel.
	static const float EdgePanForgivenessZone = 30.0f;

	const FVector2D LocalCursorPos = MyGeometry.AbsoluteToLocal( TargetPosition );

	// If the mouse is outside of the graph area, then we want to pan in that direction.
	// The farther out the mouse is, the more we want to pan.

	FVector2D EdgePanThisTick(0,0);
	if ( LocalCursorPos.X <= EdgePanForgivenessZone )
	{
		EdgePanThisTick.X += FMath::Min( -MinPanSpeed, EdgePanSpeedCoefficient * (EdgePanForgivenessZone - LocalCursorPos.X) );
	}
	else if( LocalCursorPos.X >= MyGeometry.Size.X - EdgePanForgivenessZone )
	{
		EdgePanThisTick.X = FMath::Max( MinPanSpeed, EdgePanSpeedCoefficient * (MyGeometry.Size.X - EdgePanForgivenessZone - LocalCursorPos.X) );
	}

	if ( LocalCursorPos.Y <= EdgePanForgivenessZone )
	{
		EdgePanThisTick.Y += FMath::Min( -MinPanSpeed, EdgePanSpeedCoefficient * (EdgePanForgivenessZone - LocalCursorPos.Y) );
	}
	else if( LocalCursorPos.Y >= MyGeometry.Size.Y - EdgePanForgivenessZone )
	{
		EdgePanThisTick.Y = FMath::Max( MinPanSpeed, EdgePanSpeedCoefficient * (MyGeometry.Size.Y - EdgePanForgivenessZone - LocalCursorPos.Y) );
	}

	return EdgePanThisTick;
}

void SPixel2DEditorViewport::UpdateViewOffset(const FGeometry& MyGeometry, const FVector2D& TargetPosition)
{
	const FVector2D PanAmount = ComputeEdgePanAmount( MyGeometry, TargetPosition ) / GetZoomAmount();
	ViewOffset += PanAmount;
}

void SPixel2DEditorViewport::RequestDeferredPan(const FVector2D& UpdatePosition)
{
	bRequestDeferredPan = true;
	DeferredPanPosition = UpdatePosition;
}

FVector2D SPixel2DEditorViewport::GraphCoordToPanelCoord( const FVector2D& GraphSpaceCoordinate ) const
{
	return (GraphSpaceCoordinate - GetViewOffset()) * GetZoomAmount();
}

FVector2D SPixel2DEditorViewport::PanelCoordToGraphCoord( const FVector2D& PanelSpaceCoordinate ) const
{
	return PanelSpaceCoordinate / GetZoomAmount() + GetViewOffset();
}

FSlateRect SPixel2DEditorViewport::PanelRectToGraphRect( const FSlateRect& PanelSpaceRect ) const
{
	FVector2D UpperLeft = PanelCoordToGraphCoord( FVector2D(PanelSpaceRect.Left, PanelSpaceRect.Top) );
	FVector2D LowerRight = PanelCoordToGraphCoord(  FVector2D(PanelSpaceRect.Right, PanelSpaceRect.Bottom) );

	return FSlateRect(
		UpperLeft.X, UpperLeft.Y,
		LowerRight.X, LowerRight.Y );
}

void SPixel2DEditorViewport::PaintSoftwareCursor(const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 DrawLayerId) const
{
	if (bShowSoftwareCursor)
	{
		const FSlateBrush* Brush = FEditorStyle::GetBrush(TEXT("SoftwareCursor_Grab"));

		FSlateDrawElement::MakeBox(
			OutDrawElements,
			DrawLayerId,
			AllottedGeometry.ToPaintGeometry( GraphCoordToPanelCoord(SoftwareCursorPosition) / AllottedGeometry.Scale - ( Brush->ImageSize / 2 ), Brush->ImageSize ),
			Brush);
	}
}

int32 SPixel2DEditorViewport::OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	int32 MaxLayerId = SCompoundWidget::OnPaint(Args, AllottedGeometry, MyCullingRect, OutDrawElements, LayerId, InWidgetStyle, bParentEnabled);

	++MaxLayerId;
	PaintSoftwareCursor(AllottedGeometry, MyCullingRect, OutDrawElements, MaxLayerId);

	return MaxLayerId;
}

FText SPixel2DEditorViewport::GetTitleText() const
{
	return NSLOCTEXT("Pixel2DEditor", "TileSetPaletteTitle", "tile set palette");
}
