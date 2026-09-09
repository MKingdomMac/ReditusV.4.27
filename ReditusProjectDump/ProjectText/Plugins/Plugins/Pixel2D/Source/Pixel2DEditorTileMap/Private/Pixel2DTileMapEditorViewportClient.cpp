// Copyright Epic Games, Inc. All Rights Reserved.

#include "Pixel2DTileMapEditorViewportClient.h"
#include "Components/PrimitiveComponent.h"
#include "Pixel2DTileMapComponent.h"
#include "ScopedTransaction.h"
#include "CanvasItem.h"
#include "Engine/Selection.h"
#include "AssetEditorModeManager.h"
#include "Pixel2DEdModeTileMap.h"
#include "Pixel2DTileMap.h"

#include "ComponentReregisterContext.h"
#include "CanvasTypes.h"
#include "Pixel2DTileMapEditorSettings.h"

#define LOCTEXT_NAMESPACE "Pixel2DTileMapEditor"

//////////////////////////////////////////////////////////////////////////
// FPixel2DTileMapEditorViewportClient

FPixel2DTileMapEditorViewportClient::FPixel2DTileMapEditorViewportClient(TWeakPtr<FPixel2DTileMapEditor> InTileMapEditor, TWeakPtr<class SPixel2DTileMapEditorViewport> InTileMapEditorViewportPtr)
	: TileMapEditorPtr(InTileMapEditor)
	, TileMapEditorViewportPtr(InTileMapEditorViewportPtr)
{
	// The tile map editor fully supports mode tools and isn't doing any incompatible stuff with the Widget
	Widget->SetUsesEditorModeTools(ModeTools);

	check(TileMapEditorPtr.IsValid() && TileMapEditorViewportPtr.IsValid());

	PreviewScene = &OwnedPreviewScene;
	((FAssetEditorModeManager*)ModeTools)->SetPreviewScene(PreviewScene);

	SetRealtime(true);

	WidgetMode = FWidget::WM_Translate;
	bManipulating = false;
	bManipulationDirtiedSomething = false;
	bShowTileMapStats = true;
	ScopedTransaction = nullptr;

	DrawHelper.bDrawGrid = GetDefault<UPixel2DTileMapEditorSettings>()->bShowGridByDefault;
	DrawHelper.bDrawPivot = false;
	bShowPivot = false;

	EngineShowFlags.DisableAdvancedFeatures();
	EngineShowFlags.SetCompositeEditorPrimitives(true);

	// Create a render component for the tile map being edited
	{
		RenderTileMapComponent = NewObject<UPixel2DTileMapComponent>();
		UPixel2DTileMap* TileMap = GetTileMapBeingEdited();
		RenderTileMapComponent->TileMap = TileMap;

		PreviewScene->AddComponent(RenderTileMapComponent, FTransform::Identity);
	}

	// Select the render component
	ModeTools->GetSelectedObjects()->Select(RenderTileMapComponent);
}

void FPixel2DTileMapEditorViewportClient::ActivateEditMode()
{
	// Activate the tile map edit mode
	ModeTools->SetToolkitHost(TileMapEditorPtr.Pin()->GetToolkitHost());
	ModeTools->SetDefaultMode(FPixel2DEdModeTileMap::EM_TileMap);
	ModeTools->ActivateDefaultMode();
	
	//@TODO: Need to be able to register the widget in the toolbox panel with ToolkitHost, so it can instance the ed mode widgets into it
}

void FPixel2DTileMapEditorViewportClient::DrawBoundsAsText(FViewport& InViewport, FSceneView& View, FCanvas& Canvas, int32& YPos)
{
	FNumberFormattingOptions NoDigitGroupingFormat;
	NoDigitGroupingFormat.UseGrouping = false;

	UPixel2DTileMap* TileMap = GetTileMapBeingEdited();
	FBoxSphereBounds Bounds = TileMap->GetRenderBounds();

	const FText DisplaySizeText = FText::Format(LOCTEXT("BoundsSize", "Approx. Size: {0}x{1}x{2}"),
		FText::AsNumber((int32)(Bounds.BoxExtent.X * 2.0f), &NoDigitGroupingFormat),
		FText::AsNumber((int32)(Bounds.BoxExtent.Y * 2.0f), &NoDigitGroupingFormat),
		FText::AsNumber((int32)(Bounds.BoxExtent.Z * 2.0f), &NoDigitGroupingFormat));

	Canvas.DrawShadowedString(
		6,
		YPos,
		*DisplaySizeText.ToString(),
		GEngine->GetSmallFont(),
		FLinearColor::White);
	YPos += 18;
}

void FPixel2DTileMapEditorViewportClient::Draw(const FSceneView* View, FPrimitiveDrawInterface* PDI)
{
	FPixel2DEditorViewportClient::Draw(View, PDI);

	if (bShowPivot)
	{
		FColor PivotColor = FColor::Red;
		float PivotSize = 0.02f;


		//@TODO: Merge this with FEditorCommonDrawHelper::DrawPivot, which needs to take the pivot position as an argument
		const FMatrix CameraToWorld = View->ViewMatrices.GetInvViewMatrix();

		const FVector PivLoc = FVector::ZeroVector;

		const float ZoomFactor = FMath::Min<float>(View->ViewMatrices.GetProjectionMatrix().M[0][0], View->ViewMatrices.GetProjectionMatrix().M[1][1]);
		const float WidgetRadius = View->ViewMatrices.GetViewProjectionMatrix().TransformPosition(PivLoc).W * (PivotSize / ZoomFactor);

		const FVector CamX = CameraToWorld.TransformVector(FVector(1, 0, 0));
		const FVector CamY = CameraToWorld.TransformVector(FVector(0, 1, 0));

		PDI->DrawLine(PivLoc - (WidgetRadius*CamX), PivLoc + (WidgetRadius*CamX), PivotColor, SDPG_Foreground);
		PDI->DrawLine(PivLoc - (WidgetRadius*CamY), PivLoc + (WidgetRadius*CamY), PivotColor, SDPG_Foreground);
	}
}

void FPixel2DTileMapEditorViewportClient::DrawCanvas(FViewport& InViewport, FSceneView& View, FCanvas& Canvas)
{
	FEditorViewportClient::DrawCanvas(InViewport, View, Canvas);

	const bool bIsHitTesting = Canvas.IsHitTesting();
	if (!bIsHitTesting)
	{
		Canvas.SetHitProxy(nullptr);
	}

	if (!TileMapEditorPtr.IsValid())
	{
		return;
	}

	int32 YPos = 42;

	// Draw the stats (if enabled)
	if (bShowTileMapStats)
	{
		if (UPixel2DTileMap* TileMap = GetTileMapBeingEdited())
		{
			// Show baked collision stats
			if (TileMap->BodySetup != nullptr)
			{
				//FSpriteGeometryEditMode::DrawCollisionStats(InViewport, View, Canvas, TileMap->BodySetup, /*inout*/ YPos);
				auto DrawCollisionStats = [](FViewport & InViewport, FSceneView & View, FCanvas & Canvas, class UBodySetup* BodySetup, int32 & YPos)
				{
					FCanvasTextItem TextItem(FVector2D(6, YPos), LOCTEXT("CollisionGeomBaked", "Collision Geometry (baked)"), GEngine->GetSmallFont(), FLinearColor::White);
					TextItem.EnableShadow(FLinearColor::Black);

					TextItem.Draw(&Canvas);
					TextItem.Position += FVector2D(6.0f, 18.0f);

					// Collect stats
					const FKAggregateGeom& AggGeom3D = BodySetup->AggGeom;

					int32 NumSpheres = AggGeom3D.SphereElems.Num();
					int32 NumBoxes = AggGeom3D.BoxElems.Num();
					int32 NumCapsules = AggGeom3D.SphylElems.Num();
					int32 NumConvexElems = AggGeom3D.ConvexElems.Num();
					int32 NumConvexVerts = 0;
					bool bIs2D = false;

					for (const FKConvexElem& ConvexElement : AggGeom3D.ConvexElems)
					{
						NumConvexVerts += ConvexElement.VertexData.Num();
					}

					if (NumSpheres > 0)
					{
						static const FText SpherePrompt = LOCTEXT("SphereCount", "Spheres: {0}");
						static const FText CirclePrompt = LOCTEXT("CircleCount", "Circles: {0}");

						TextItem.Text = FText::Format(bIs2D ? CirclePrompt : SpherePrompt, FText::AsNumber(NumSpheres));
						TextItem.Draw(&Canvas);
						TextItem.Position.Y += 18.0f;
					}

					if (NumBoxes > 0)
					{
						static const FText BoxPrompt = LOCTEXT("BoxCount", "Boxes: {0}");
						TextItem.Text = FText::Format(BoxPrompt, FText::AsNumber(NumBoxes));
						TextItem.Draw(&Canvas);
						TextItem.Position.Y += 18.0f;
					}

					if (NumCapsules > 0)
					{
						static const FText CapsulePrompt = LOCTEXT("CapsuleCount", "Capsules: {0}");
						TextItem.Text = FText::Format(CapsulePrompt, FText::AsNumber(NumCapsules));
						TextItem.Draw(&Canvas);
						TextItem.Position.Y += 18.0f;
					}

					if (NumConvexElems > 0)
					{
						static const FText ConvexPrompt = LOCTEXT("ConvexCount", "Convex Shapes: {0} ({1} verts)");
						TextItem.Text = FText::Format(ConvexPrompt, FText::AsNumber(NumConvexElems), FText::AsNumber(NumConvexVerts));
						TextItem.Draw(&Canvas);
						TextItem.Position.Y += 18.0f;
					}

					if ((NumConvexElems + NumCapsules + NumBoxes + NumSpheres) == 0)
					{
						static const FText NoShapesPrompt = LOCTEXT("NoCollisionDataWarning", "Warning: Collision is enabled but there are no shapes");
						TextItem.Text = NoShapesPrompt;
						TextItem.SetColor(FLinearColor::Yellow);
						TextItem.Draw(&Canvas);
						TextItem.Position.Y += 18.0f;
					}

					YPos = (int32)TextItem.Position.Y;
				};
				//FSpriteGeometryEditMode::DrawCollisionStats(InViewport, View, Canvas, TileMap->BodySetup, /*inout*/ YPos);
				DrawCollisionStats(InViewport, View, Canvas, TileMap->BodySetup, /*inout*/ YPos);
			}
			else
			{
				FCanvasTextItem TextItem(FVector2D(6, YPos), LOCTEXT("NoCollisionDataMainScreen", "No collision data"), GEngine->GetSmallFont(), FLinearColor::White);
				TextItem.EnableShadow(FLinearColor::Black);
				TextItem.Draw(&Canvas);
				YPos += 18.0f;
			}


			// Show baked rendering stats
			int32 NumTriangles = 0;
			int32 NumBatches = 0;
			RenderTileMapComponent->GetRenderingStats(/*out*/ NumTriangles, /*out*/ NumBatches);

			{

				FCanvasTextItem TextItem(FVector2D(6, YPos), LOCTEXT("RenderGeomBaked", "Render Geometry (baked)"), GEngine->GetSmallFont(), FLinearColor::White);
				TextItem.EnableShadow(FLinearColor::Black);

				TextItem.Draw(&Canvas);
				TextItem.Position += FVector2D(6.0f, 18.0f);

				// Draw the number of batches
				TextItem.Text = FText::Format(LOCTEXT("SectionCount", "Sections: {0}"), FText::AsNumber(NumBatches));
				TextItem.Draw(&Canvas);
				TextItem.Position.Y += 18.0f;

				// Determine the material type
				//@TODO: Similar code happens in the sprite editor and sprite details panel, and should be consolidated if possible
				static const FText Opaque = LOCTEXT("OpaqueMaterial", "Opaque");
				static const FText Translucent = LOCTEXT("TranslucentMaterial", "Translucent");
				static const FText Masked = LOCTEXT("MaskedMaterial", "Masked");

				FText MaterialType = LOCTEXT("NoMaterial", "No material set!");
				if (TileMap->Material != nullptr)
				{
					switch (TileMap->Material->GetBlendMode())
					{
					case EBlendMode::BLEND_Opaque:
						MaterialType = Opaque;
						break;
					case EBlendMode::BLEND_Translucent:
					case EBlendMode::BLEND_Additive:
					case EBlendMode::BLEND_Modulate:
					case EBlendMode::BLEND_AlphaComposite:
					case EBlendMode::BLEND_AlphaHoldout:
						MaterialType = Translucent;
						break;
					case EBlendMode::BLEND_Masked:
						MaterialType = Masked;
						break;
					}
				}

				// Draw the number of triangles
				TextItem.Text = FText::Format(LOCTEXT("TriangleCountAndMaterialBlendMode", "Triangles: {0} ({1})"), FText::AsNumber(NumTriangles), MaterialType);
				TextItem.Draw(&Canvas);
				TextItem.Position.Y += 18.0f;

				YPos = (int32)TextItem.Position.Y;
			}
		}

		// Draw the render bounds
		DrawBoundsAsText(InViewport, View, Canvas, /*inout*/ YPos);
	}
}

void FPixel2DTileMapEditorViewportClient::Tick(float DeltaSeconds)
{
	FPixel2DEditorViewportClient::Tick(DeltaSeconds);

	if (!GIntraFrameDebuggingGameThread)
	{
		OwnedPreviewScene.GetWorld()->Tick(LEVELTICK_All, DeltaSeconds);
	}
}

FLinearColor FPixel2DTileMapEditorViewportClient::GetBackgroundColor() const
{
	if (UPixel2DTileMap* TileMap = RenderTileMapComponent->GetTileMap())
	{
		return TileMap->BackgroundColor;
	}
	else
	{
		return GetDefault<UPixel2DTileMapEditorSettings>()->DefaultBackgroundColor;
	}
}

void FPixel2DTileMapEditorViewportClient::ToggleShowTileGrid()
{
	FComponentReregisterContext RefreshComponentHelper(RenderTileMapComponent);
	RenderTileMapComponent->bShowPerTileGridWhenUnselected = !RenderTileMapComponent->bShowPerTileGridWhenUnselected;
	Invalidate();
}

bool FPixel2DTileMapEditorViewportClient::IsShowTileGridChecked() const
{
	return RenderTileMapComponent->bShowPerTileGridWhenUnselected;
}

void FPixel2DTileMapEditorViewportClient::ToggleShowLayerGrid()
{
	FComponentReregisterContext RefreshComponentHelper(RenderTileMapComponent);
	RenderTileMapComponent->bShowPerLayerGridWhenUnselected = !RenderTileMapComponent->bShowPerLayerGridWhenUnselected;
	Invalidate();
}

bool FPixel2DTileMapEditorViewportClient::IsShowLayerGridChecked() const
{
	return RenderTileMapComponent->bShowPerLayerGridWhenUnselected;
}

void FPixel2DTileMapEditorViewportClient::ToggleShowMeshEdges()
{
	EngineShowFlags.SetMeshEdges(!EngineShowFlags.MeshEdges);
	Invalidate();
}

bool FPixel2DTileMapEditorViewportClient::IsShowMeshEdgesChecked() const
{
	return EngineShowFlags.MeshEdges;
}

void FPixel2DTileMapEditorViewportClient::ToggleShowTileMapStats()
{
	bShowTileMapStats = !bShowTileMapStats;
	Invalidate();
}

bool FPixel2DTileMapEditorViewportClient::IsShowTileMapStatsChecked() const
{
	return bShowTileMapStats;
}

void FPixel2DTileMapEditorViewportClient::EndTransaction()
{
	if (bManipulationDirtiedSomething)
	{
		RenderTileMapComponent->TileMap->PostEditChange();
	}
	
	bManipulationDirtiedSomething = false;

	if (ScopedTransaction != nullptr)
	{
		delete ScopedTransaction;
		ScopedTransaction = nullptr;
	}
}

void FPixel2DTileMapEditorViewportClient::NotifyTileMapBeingEditedHasChanged()
{
	//@TODO: Ideally we do this before switching
	EndTransaction();

	// Update components to know about the new tile map being edited
	UPixel2DTileMap* TileMap = GetTileMapBeingEdited();
	RenderTileMapComponent->TileMap = TileMap;

	RequestFocusOnSelection(/*bInstant=*/ true);
}

FBox FPixel2DTileMapEditorViewportClient::GetDesiredFocusBounds() const
{
	return RenderTileMapComponent->Bounds.GetBox();
}

//////////////////////////////////////////////////////////////////////////

#undef LOCTEXT_NAMESPACE
