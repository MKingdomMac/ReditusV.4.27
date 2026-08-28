// Copyright Epic Games, Inc. All Rights Reserved.

#include "TileMapEdModeToolkit.h"
#include "TileMapEdMode.h"
#include "TileMapTerrainActor.h"

#include "AssetRegistryModule.h"
#include "AssetToolsModule.h"
#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Editor.h"
#include "EditorModeManager.h"

#include "Engine/Level.h"
#include "Engine/Selection.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "EngineUtils.h"

#include "GameFramework/Actor.h"

#include "IAssetTools.h"
#include "Materials/Material.h"
#include "Materials/MaterialInterface.h"

#include "Misc/MessageDialog.h"
#include "Modules/ModuleManager.h"
#include "PhysicsEngine/BodySetup.h"
#include "ProceduralMeshComponent.h"
#include "RawMesh.h"
#include "ScopedTransaction.h"
#include "UObject/Package.h"

#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SSpinBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "FTileMapEdModeToolkit"

namespace TileMapEdModeToolkitLocal
{
	struct FExposedFace
	{
		FIntVector NeighborOffset;
		FVector Normal;
		FVector Corners[4];
	};

	ATileMapTerrainActor* FindTerrainActor(UWorld* EditorWorld)
	{
		if (!EditorWorld)
		{
			return nullptr;
		}

		if (GEditor)
		{
			USelection* SelectedActors =
				GEditor->GetSelectedActors();

			if (SelectedActors)
			{
				for (
					FSelectionIterator It(*SelectedActors);
					It;
					++It
					)
				{
					ATileMapTerrainActor* TerrainActor =
						Cast<ATileMapTerrainActor>(*It);

					if (
						TerrainActor &&
						TerrainActor->GetWorld() == EditorWorld
						)
					{
						return TerrainActor;
					}
				}
			}
		}

		for (
			TActorIterator<ATileMapTerrainActor> It(EditorWorld);
			It;
			++It
			)
		{
			return *It;
		}

		return nullptr;
	}

	void GetSelectedTerrainActors(
		UWorld* EditorWorld,
		TArray<ATileMapTerrainActor*>& OutTerrainActors
	)
	{
		OutTerrainActors.Reset();

		if (!EditorWorld || !GEditor)
		{
			return;
		}

		USelection* SelectedActors = GEditor->GetSelectedActors();

		if (!SelectedActors)
		{
			return;
		}

		for (FSelectionIterator It(*SelectedActors); It; ++It)
		{
			ATileMapTerrainActor* TerrainActor =
				Cast<ATileMapTerrainActor>(*It);

			if (
				TerrainActor &&
				TerrainActor->GetWorld() == EditorWorld
				)
			{
				OutTerrainActors.AddUnique(TerrainActor);
			}
		}

		OutTerrainActors.Sort(
			[](const ATileMapTerrainActor& Left,
				const ATileMapTerrainActor& Right)
			{
				return Left.GetPathName() < Right.GetPathName();
			}
		);
	}

	bool AreTileDefinitionsEquivalent(
		const FTileMapTileDefinition& Left,
		const FTileMapTileDefinition& Right
	)
	{
		return
			Left.TileName == Right.TileName &&
			Left.AutoTileSet == Right.AutoTileSet &&
			Left.AutoShape == Right.AutoShape &&
			Left.Mesh == Right.Mesh &&
			Left.MaterialOverride == Right.MaterialOverride &&
			Left.MeshScale.Equals(Right.MeshScale) &&
			Left.PivotOffset.Equals(Right.PivotOffset);
	}

	bool AreTerrainPalettesCompatible(
		const ATileMapTerrainActor* Target,
		const ATileMapTerrainActor* Source,
		FString& OutReason
	)
	{
		if (!Target || !Source)
		{
			OutReason = TEXT("The merge target or source is no longer valid.");
			return false;
		}

		if (!FMath::IsNearlyEqual(Target->GridSize, Source->GridSize))
		{
			OutReason = FString::Printf(
				TEXT("%s uses a different Grid Size."),
				*Source->GetActorLabel()
			);
			return false;
		}

		if (
			Target->BlockMesh != Source->BlockMesh ||
			Target->TilePalette.Num() != Source->TilePalette.Num()
			)
		{
			OutReason = FString::Printf(
				TEXT("%s does not use the same block mesh and tile palette as the target."),
				*Source->GetActorLabel()
			);
			return false;
		}

		for (int32 Index = 0; Index < Target->TilePalette.Num(); ++Index)
		{
			if (!AreTileDefinitionsEquivalent(
				Target->TilePalette[Index],
				Source->TilePalette[Index]
				))
			{
				OutReason = FString::Printf(
					TEXT("%s has a different tile definition at palette index %d."),
					*Source->GetActorLabel(),
					Index + 1
				);
				return false;
			}
		}

		return true;
	}

	bool GetRelativeGridRotation(
		const ATileMapTerrainActor* Target,
		const ATileMapTerrainActor* Source,
		int32& OutQuarterTurns,
		FString& OutReason
	)
	{
		const FVector TargetScale = Target->GetActorScale3D();
		const FVector SourceScale = Source->GetActorScale3D();

		if (
			!TargetScale.Equals(SourceScale) ||
			TargetScale.X <= 0.0f ||
			TargetScale.Y <= 0.0f ||
			TargetScale.Z <= 0.0f
			)
		{
			OutReason = FString::Printf(
				TEXT("%s must use the same positive actor scale as the target."),
				*Source->GetActorLabel()
			);
			return false;
		}

		const FQuat RelativeRotation =
			Target->GetActorQuat().Inverse() *
			Source->GetActorQuat();
		const FRotator RelativeRotator =
			RelativeRotation.Rotator().GetNormalized();

		if (
			FMath::Abs(RelativeRotator.Pitch) > 0.01f ||
			FMath::Abs(RelativeRotator.Roll) > 0.01f
			)
		{
			OutReason = FString::Printf(
				TEXT("%s is tilted. Copied terrain may only rotate around Z."),
				*Source->GetActorLabel()
			);
			return false;
		}

		const int32 SignedQuarterTurns =
			FMath::RoundToInt(RelativeRotator.Yaw / 90.0f);
		const float SnappedYaw =
			static_cast<float>(SignedQuarterTurns) * 90.0f;

		if (
			FMath::Abs(
				FMath::FindDeltaAngleDegrees(
					RelativeRotator.Yaw,
					SnappedYaw
				)
			) > 0.01f
			)
		{
			OutReason = FString::Printf(
				TEXT("%s must be rotated in exact 90-degree steps."),
				*Source->GetActorLabel()
			);
			return false;
		}

		OutQuarterTurns =
			((SignedQuarterTurns % 4) + 4) % 4;
		return true;
	}

	FIntVector SnapWorldPositionToTargetGrid(
		const ATileMapTerrainActor* Target,
		const FVector& WorldPosition
	)
	{
		const float SafeGridSize = FMath::Max(
			Target->GridSize,
			1.0f
		);
		const FVector TargetLocalPosition =
			Target->GetActorTransform().InverseTransformPosition(
				WorldPosition
			);
		const FVector GridCoordinates =
			(TargetLocalPosition / SafeGridSize) -
			FVector(0.5f, 0.5f, 0.5f);

		return FIntVector(
			FMath::RoundToInt(GridCoordinates.X),
			FMath::RoundToInt(GridCoordinates.Y),
			FMath::RoundToInt(GridCoordinates.Z)
		);
	}

	bool BuildSnappedTerrainMergeBatch(
		ATileMapTerrainActor* Target,
		const TArray<ATileMapTerrainActor*>& Sources,
		TArray<FIntVector>& OutGridPositions,
		TArray<int32>& OutTileTypes,
		TArray<uint8>& OutRotations,
		TArray<FIntVector>& OutPaintedPathPositions,
		int32& OutTargetOverlapCount,
		int32& OutSourceOverlapCount,
		float& OutMaximumSnapDistance,
		FString& OutReason
	)
	{
		OutGridPositions.Reset();
		OutTileTypes.Reset();
		OutRotations.Reset();
		OutPaintedPathPositions.Reset();
		OutTargetOverlapCount = 0;
		OutSourceOverlapCount = 0;
		OutMaximumSnapDistance = 0.0f;
		OutReason.Reset();

		TMap<FIntVector, int32> PendingBlockIndices;

		for (ATileMapTerrainActor* Source : Sources)
		{
			if (!AreTerrainPalettesCompatible(Target, Source, OutReason))
			{
				return false;
			}

			int32 RelativeQuarterTurns = 0;

			if (!GetRelativeGridRotation(
				Target,
				Source,
				RelativeQuarterTurns,
				OutReason
				))
			{
				return false;
			}

			for (const FIntVector& SourcePosition : Source->OccupiedBlocks)
			{
				const FVector WorldPosition =
					Source->GridToWorld(SourcePosition);
				const FIntVector TargetPosition =
					SnapWorldPositionToTargetGrid(
						Target,
						WorldPosition
					);
				const FVector SnappedWorldPosition =
					Target->GridToWorld(TargetPosition);

				OutMaximumSnapDistance = FMath::Max(
					OutMaximumSnapDistance,
					FVector::Dist(
						WorldPosition,
						SnappedWorldPosition
					)
				);

				if (Target->HasBlock(TargetPosition))
				{
					++OutTargetOverlapCount;
					continue;
				}

				if (PendingBlockIndices.Contains(TargetPosition))
				{
					++OutSourceOverlapCount;
					continue;
				}

				const int32 TileType =
					Source->GetBlockTileType(SourcePosition);
				const uint8 Rotation = static_cast<uint8>(
					(Source->GetBlockRotation(SourcePosition) +
					RelativeQuarterTurns) % 4
				);
				const int32 NewIndex =
					OutGridPositions.Add(TargetPosition);

				OutTileTypes.Add(TileType);
				OutRotations.Add(Rotation);
				PendingBlockIndices.Add(TargetPosition, NewIndex);

				if (Source->HasPaintedPath(SourcePosition))
				{
					OutPaintedPathPositions.Add(TargetPosition);
				}
			}
		}

		return true;
	}

	void AddRawMeshFace(
		FRawMesh& RawMesh,
		const FVector* Corners,
		const FVector& Normal,
		int32 MaterialIndex
	)
	{
		uint32 VertexIndices[4];

		for (int32 CornerIndex = 0; CornerIndex < 4; ++CornerIndex)
		{
			VertexIndices[CornerIndex] =
				RawMesh.VertexPositions.Add(
					Corners[CornerIndex]
				);
		}

		static const int32 TriangleCorners[6] =
		{
			0, 2, 1,
			0, 3, 2
		};

		static const FVector2D CornerUVs[4] =
		{
			FVector2D(0.0f, 0.0f),
			FVector2D(1.0f, 0.0f),
			FVector2D(1.0f, 1.0f),
			FVector2D(0.0f, 1.0f)
		};

		const FVector TangentX =
			(Corners[1] - Corners[0]).GetSafeNormal();

		const FVector TangentY =
			FVector::CrossProduct(
				Normal,
				TangentX
			).GetSafeNormal();

		for (int32 WedgeIndex = 0; WedgeIndex < 6; ++WedgeIndex)
		{
			const int32 CornerIndex =
				TriangleCorners[WedgeIndex];

			RawMesh.WedgeIndices.Add(
				VertexIndices[CornerIndex]
			);

			RawMesh.WedgeTangentX.Add(TangentX);
			RawMesh.WedgeTangentY.Add(TangentY);
			RawMesh.WedgeTangentZ.Add(Normal);
			RawMesh.WedgeTexCoords[0].Add(
				CornerUVs[CornerIndex]
			);
			RawMesh.WedgeColors.Add(FColor::White);
		}

		for (int32 TriangleIndex = 0; TriangleIndex < 2; ++TriangleIndex)
		{
			RawMesh.FaceMaterialIndices.Add(MaterialIndex);
			RawMesh.FaceSmoothingMasks.Add(0);
		}
	}

	void AddRawMeshTriangle(
		FRawMesh& RawMesh,
		const FVector& Corner0,
		const FVector& Corner1,
		const FVector& Corner2,
		const FVector& Normal,
		int32 MaterialIndex
	)
	{
		const FVector Corners[3] =
		{
			Corner0,
			Corner1,
			Corner2
		};

		uint32 VertexIndices[3];

		for (int32 CornerIndex = 0; CornerIndex < 3; ++CornerIndex)
		{
			VertexIndices[CornerIndex] =
				RawMesh.VertexPositions.Add(Corners[CornerIndex]);
		}

		static const int32 TriangleCorners[3] = { 0, 2, 1 };
		static const FVector2D CornerUVs[3] =
		{
			FVector2D(0.0f, 0.0f),
			FVector2D(1.0f, 0.0f),
			FVector2D(1.0f, 1.0f)
		};

		const FVector TangentX =
			(Corner1 - Corner0).GetSafeNormal();

		const FVector TangentY =
			FVector::CrossProduct(
				Normal,
				TangentX
			).GetSafeNormal();

		for (int32 WedgeIndex = 0; WedgeIndex < 3; ++WedgeIndex)
		{
			const int32 CornerIndex = TriangleCorners[WedgeIndex];

			RawMesh.WedgeIndices.Add(VertexIndices[CornerIndex]);
			RawMesh.WedgeTangentX.Add(TangentX);
			RawMesh.WedgeTangentY.Add(TangentY);
			RawMesh.WedgeTangentZ.Add(Normal);
			RawMesh.WedgeTexCoords[0].Add(CornerUVs[CornerIndex]);
			RawMesh.WedgeColors.Add(FColor::White);
		}

		RawMesh.FaceMaterialIndices.Add(MaterialIndex);
		RawMesh.FaceSmoothingMasks.Add(0);
	}

	int32 FindOrAddMaterial(
		TArray<UMaterialInterface*>& Materials,
		UMaterialInterface* Material
	)
	{
		if (!Material)
		{
			Material = UMaterial::GetDefaultMaterial(MD_Surface);
		}

		const int32 ExistingIndex =
			Materials.IndexOfByKey(Material);

		return ExistingIndex != INDEX_NONE
			? ExistingIndex
			: Materials.Add(Material);
	}

	bool AppendStaticMeshGeometry(
		UStaticMesh* SourceMesh,
		UMaterialInterface* SlotZeroOverride,
		const FTransform& LocalTransform,
		FRawMesh& OutRawMesh,
		TArray<UMaterialInterface*>& OutMaterials
	)
	{
		if (
			!SourceMesh ||
			SourceMesh->GetNumSourceModels() <= 0
			)
		{
			return false;
		}

		const FStaticMeshSourceModel& SourceModel =
			SourceMesh->GetSourceModel(0);

		if (!SourceModel.RawMeshBulkData)
		{
			return false;
		}

		FRawMesh SourceRawMesh;
		SourceModel.RawMeshBulkData->LoadRawMesh(
			SourceRawMesh
		);

		if (
			SourceRawMesh.VertexPositions.Num() == 0 ||
			SourceRawMesh.WedgeIndices.Num() == 0
			)
		{
			return false;
		}

		const uint32 VertexOffset =
			static_cast<uint32>(
				OutRawMesh.VertexPositions.Num()
			);

		for (const FVector& Position : SourceRawMesh.VertexPositions)
		{
			OutRawMesh.VertexPositions.Add(
				LocalTransform.TransformPosition(Position)
			);
		}

		const int32 WedgeCount =
			SourceRawMesh.WedgeIndices.Num();

		for (int32 WedgeIndex = 0; WedgeIndex < WedgeCount; ++WedgeIndex)
		{
			OutRawMesh.WedgeIndices.Add(
				VertexOffset +
				SourceRawMesh.WedgeIndices[WedgeIndex]
			);

			const FVector SourceTangentX =
				SourceRawMesh.WedgeTangentX.IsValidIndex(WedgeIndex)
				? SourceRawMesh.WedgeTangentX[WedgeIndex]
				: FVector::ForwardVector;

			const FVector SourceTangentY =
				SourceRawMesh.WedgeTangentY.IsValidIndex(WedgeIndex)
				? SourceRawMesh.WedgeTangentY[WedgeIndex]
				: FVector::RightVector;

			const FVector SourceTangentZ =
				SourceRawMesh.WedgeTangentZ.IsValidIndex(WedgeIndex)
				? SourceRawMesh.WedgeTangentZ[WedgeIndex]
				: FVector::UpVector;

			OutRawMesh.WedgeTangentX.Add(
				LocalTransform
				.TransformVectorNoScale(SourceTangentX)
				.GetSafeNormal()
			);

			OutRawMesh.WedgeTangentY.Add(
				LocalTransform
				.TransformVectorNoScale(SourceTangentY)
				.GetSafeNormal()
			);

			OutRawMesh.WedgeTangentZ.Add(
				LocalTransform
				.TransformVectorNoScale(SourceTangentZ)
				.GetSafeNormal()
			);

			OutRawMesh.WedgeTexCoords[0].Add(
				SourceRawMesh.WedgeTexCoords[0]
				.IsValidIndex(WedgeIndex)
				? SourceRawMesh.WedgeTexCoords[0][WedgeIndex]
				: FVector2D::ZeroVector
			);

			OutRawMesh.WedgeColors.Add(
				SourceRawMesh.WedgeColors.IsValidIndex(WedgeIndex)
				? SourceRawMesh.WedgeColors[WedgeIndex]
				: FColor::White
			);
		}

		const int32 TriangleCount = WedgeCount / 3;

		for (
			int32 TriangleIndex = 0;
			TriangleIndex < TriangleCount;
			++TriangleIndex
			)
		{
			const int32 SourceMaterialIndex =
				SourceRawMesh.FaceMaterialIndices
				.IsValidIndex(TriangleIndex)
				? SourceRawMesh.FaceMaterialIndices[TriangleIndex]
				: 0;

			UMaterialInterface* Material =
				SourceMaterialIndex == 0 && SlotZeroOverride
				? SlotZeroOverride
				: SourceMesh->GetMaterial(SourceMaterialIndex);

			OutRawMesh.FaceMaterialIndices.Add(
				FindOrAddMaterial(
					OutMaterials,
					Material
				)
			);

			OutRawMesh.FaceSmoothingMasks.Add(
				SourceRawMesh.FaceSmoothingMasks
				.IsValidIndex(TriangleIndex)
				? SourceRawMesh.FaceSmoothingMasks[TriangleIndex]
				: 0
			);
		}

		return true;
	}

	bool AppendProceduralMeshGeometry(
		UProceduralMeshComponent* SourceComponent,
		FRawMesh& OutRawMesh,
		TArray<UMaterialInterface*>& OutMaterials
	)
	{
		if (!SourceComponent)
		{
			return false;
		}

		bool bAppendedGeometry = false;
		const FTransform LocalTransform =
			SourceComponent->GetRelativeTransform();

		for (
			int32 SectionIndex = 0;
			SectionIndex < SourceComponent->GetNumSections();
			++SectionIndex
			)
		{
			const FProcMeshSection* Section =
				SourceComponent->GetProcMeshSection(SectionIndex);

			if (
				!Section ||
				Section->ProcVertexBuffer.Num() == 0 ||
				Section->ProcIndexBuffer.Num() == 0
				)
			{
				continue;
			}

			const uint32 VertexOffset =
				static_cast<uint32>(
					OutRawMesh.VertexPositions.Num()
				);

			for (
				const FProcMeshVertex& Vertex :
				Section->ProcVertexBuffer
				)
			{
				OutRawMesh.VertexPositions.Add(
					LocalTransform.TransformPosition(
						Vertex.Position
					)
				);
			}

			for (
				const uint32 SourceVertexIndex :
				Section->ProcIndexBuffer
				)
			{
				if (
					!Section->ProcVertexBuffer.IsValidIndex(
						static_cast<int32>(SourceVertexIndex)
					)
					)
				{
					return false;
				}

				const FProcMeshVertex& Vertex =
					Section->ProcVertexBuffer[
						static_cast<int32>(SourceVertexIndex)
					];

				const FVector TangentX =
					LocalTransform
					.TransformVectorNoScale(
						Vertex.Tangent.TangentX
					)
					.GetSafeNormal();
				const FVector TangentZ =
					LocalTransform
					.TransformVectorNoScale(Vertex.Normal)
					.GetSafeNormal();
				const FVector TangentY =
					FVector::CrossProduct(
						TangentZ,
						TangentX
					).GetSafeNormal() *
					(Vertex.Tangent.bFlipTangentY ? -1.0f : 1.0f);

				OutRawMesh.WedgeIndices.Add(
					VertexOffset + SourceVertexIndex
				);
				OutRawMesh.WedgeTangentX.Add(TangentX);
				OutRawMesh.WedgeTangentY.Add(TangentY);
				OutRawMesh.WedgeTangentZ.Add(TangentZ);
				OutRawMesh.WedgeTexCoords[0].Add(Vertex.UV0);
				OutRawMesh.WedgeColors.Add(Vertex.Color);
			}

			const int32 MaterialIndex = FindOrAddMaterial(
				OutMaterials,
				SourceComponent->GetMaterial(SectionIndex)
			);
			const int32 TriangleCount =
				Section->ProcIndexBuffer.Num() / 3;

			for (
				int32 TriangleIndex = 0;
				TriangleIndex < TriangleCount;
				++TriangleIndex
				)
			{
				OutRawMesh.FaceMaterialIndices.Add(MaterialIndex);
				OutRawMesh.FaceSmoothingMasks.Add(0);
			}

			bAppendedGeometry = true;
		}

		return bAppendedGeometry;
	}

	struct FBakePositionKey
	{
		int32 X;
		int32 Y;
		int32 Z;

		FBakePositionKey()
			: X(0)
			, Y(0)
			, Z(0)
		{
		}

		explicit FBakePositionKey(const FVector& Position)
			: X(FMath::RoundToInt(Position.X * 1000.0f))
			, Y(FMath::RoundToInt(Position.Y * 1000.0f))
			, Z(FMath::RoundToInt(Position.Z * 1000.0f))
		{
		}

		bool operator==(const FBakePositionKey& Other) const
		{
			return X == Other.X && Y == Other.Y && Z == Other.Z;
		}

		uint32 GetHash() const
		{
			uint32 Hash = 2166136261u;
			Hash = (Hash ^ static_cast<uint32>(X)) * 16777619u;
			Hash = (Hash ^ static_cast<uint32>(Y)) * 16777619u;
			Hash = (Hash ^ static_cast<uint32>(Z)) * 16777619u;
			return Hash;
		}

		friend uint32 GetTypeHash(const FBakePositionKey& Key)
		{
			return Key.GetHash();
		}
	};

	bool BakePositionKeyLess(
		const FBakePositionKey& A,
		const FBakePositionKey& B
	)
	{
		if (A.X != B.X)
		{
			return A.X < B.X;
		}

		if (A.Y != B.Y)
		{
			return A.Y < B.Y;
		}

		return A.Z < B.Z;
	}

	struct FBakeEdgeKey
	{
		FBakePositionKey A;
		FBakePositionKey B;

		FBakeEdgeKey()
		{
		}

		FBakeEdgeKey(
			const FBakePositionKey& InA,
			const FBakePositionKey& InB
		)
		{
			if (BakePositionKeyLess(InB, InA))
			{
				A = InB;
				B = InA;
			}
			else
			{
				A = InA;
				B = InB;
			}
		}

		bool operator==(const FBakeEdgeKey& Other) const
		{
			return A == Other.A && B == Other.B;
		}

		friend uint32 GetTypeHash(const FBakeEdgeKey& Key)
		{
			return HashCombine(Key.A.GetHash(), Key.B.GetHash());
		}
	};

	struct FBakeVertexData
	{
		FVector Position;
		FVector TangentX;
		FVector TangentY;
		FVector TangentZ;
		FVector2D UV;
		FColor Color;
	};

	struct FBakeTriangleData
	{
		int32 WedgeIndices[3];
		FBakePositionKey PositionKeys[3];
		FVector GeometricNormal;
		float PlaneDistance;
		int32 ProjectionAxis;
		int32 MaterialIndex;
		uint32 SmoothingMask;
		FVector2D ProjectedOrigin;
		FVector2D ProjectedCentroid;
		FVector2D UvGradientX;
		FVector2D UvGradientY;
		FVector2D UvOrigin;
		FLinearColor ColorGradientX;
		FLinearColor ColorGradientY;
		FLinearColor ColorOrigin;
		FVector ConstantTangentX;
		FVector ConstantTangentY;
		FVector ConstantTangentZ;
		bool bCanSimplify;
	};

	struct FBakeBoundaryEdge
	{
		FBakePositionKey Start;
		FBakePositionKey End;
	};

	struct FBakeBoundaryAccumulator
	{
		int32 Count;
		FBakePositionKey Start;
		FBakePositionKey End;

		FBakeBoundaryAccumulator()
			: Count(0)
		{
		}
	};

	struct FBakeGeneratedTriangle
	{
		FBakeVertexData Vertices[3];
		int32 MaterialIndex;
		uint32 SmoothingMask;
	};

	int32 GetBakeProjectionAxis(const FVector& Normal)
	{
		const FVector AbsoluteNormal(
			FMath::Abs(Normal.X),
			FMath::Abs(Normal.Y),
			FMath::Abs(Normal.Z)
		);

		if (
			AbsoluteNormal.X >= AbsoluteNormal.Y &&
			AbsoluteNormal.X >= AbsoluteNormal.Z
			)
		{
			return 0;
		}

		if (AbsoluteNormal.Y >= AbsoluteNormal.Z)
		{
			return 1;
		}

		return 2;
	}

	FVector2D ProjectBakePosition(
		const FVector& Position,
		const int32 ProjectionAxis
	)
	{
		if (ProjectionAxis == 0)
		{
			return FVector2D(Position.Y, Position.Z);
		}

		if (ProjectionAxis == 1)
		{
			return FVector2D(Position.X, Position.Z);
		}

		return FVector2D(Position.X, Position.Y);
	}

	float BakeCross2D(
		const FVector2D& A,
		const FVector2D& B,
		const FVector2D& C
	)
	{
		return
			(B.X - A.X) * (C.Y - A.Y) -
			(B.Y - A.Y) * (C.X - A.X);
	}

	bool NearlySameBakeDirection(
		const FVector& A,
		const FVector& B,
		const float MinimumDot = 0.9999f
	)
	{
		return
			!A.IsNearlyZero() &&
			!B.IsNearlyZero() &&
			FVector::DotProduct(A, B) >= MinimumDot;
	}

	bool ReadBakeWedge(
		const FRawMesh& RawMesh,
		const int32 WedgeIndex,
		FBakeVertexData& OutVertex
	)
	{
		if (
			!RawMesh.WedgeIndices.IsValidIndex(WedgeIndex) ||
			!RawMesh.VertexPositions.IsValidIndex(
				static_cast<int32>(RawMesh.WedgeIndices[WedgeIndex])
			) ||
			!RawMesh.WedgeTangentX.IsValidIndex(WedgeIndex) ||
			!RawMesh.WedgeTangentY.IsValidIndex(WedgeIndex) ||
			!RawMesh.WedgeTangentZ.IsValidIndex(WedgeIndex) ||
			!RawMesh.WedgeTexCoords[0].IsValidIndex(WedgeIndex) ||
			!RawMesh.WedgeColors.IsValidIndex(WedgeIndex)
			)
		{
			return false;
		}

		OutVertex.Position =
			RawMesh.VertexPositions[RawMesh.WedgeIndices[WedgeIndex]];
		OutVertex.TangentX = RawMesh.WedgeTangentX[WedgeIndex];
		OutVertex.TangentY = RawMesh.WedgeTangentY[WedgeIndex];
		OutVertex.TangentZ = RawMesh.WedgeTangentZ[WedgeIndex];
		OutVertex.UV = RawMesh.WedgeTexCoords[0][WedgeIndex];
		OutVertex.Color = RawMesh.WedgeColors[WedgeIndex];
		return true;
	}

	bool BakeVertexDataMatches(
		const FBakeVertexData& A,
		const FBakeVertexData& B
	)
	{
		return
			FVector::DistSquared(A.Position, B.Position) <= 0.000001f &&
			NearlySameBakeDirection(A.TangentX, B.TangentX) &&
			NearlySameBakeDirection(A.TangentY, B.TangentY) &&
			NearlySameBakeDirection(A.TangentZ, B.TangentZ) &&
			(A.UV - B.UV).SizeSquared() <= 0.000001f &&
			A.Color == B.Color;
	}

	FLinearColor BakeColorToAttribute(const FColor& Color)
	{
		return FLinearColor(
			static_cast<float>(Color.R),
			static_cast<float>(Color.G),
			static_cast<float>(Color.B),
			static_cast<float>(Color.A)
		);
	}

	FLinearColor AddBakeColors(
		const FLinearColor& A,
		const FLinearColor& B
	)
	{
		return FLinearColor(
			A.R + B.R,
			A.G + B.G,
			A.B + B.B,
			A.A + B.A
		);
	}

	FLinearColor SubtractBakeColors(
		const FLinearColor& A,
		const FLinearColor& B
	)
	{
		return FLinearColor(
			A.R - B.R,
			A.G - B.G,
			A.B - B.B,
			A.A - B.A
		);
	}

	FLinearColor ScaleBakeColor(
		const FLinearColor& Color,
		const float Scale
	)
	{
		return FLinearColor(
			Color.R * Scale,
			Color.G * Scale,
			Color.B * Scale,
			Color.A * Scale
		);
	}

	float BakeColorMaxAbsDifference(
		const FLinearColor& A,
		const FLinearColor& B
	)
	{
		return FMath::Max(
			FMath::Max(
				FMath::Abs(A.R - B.R),
				FMath::Abs(A.G - B.G)
			),
			FMath::Max(
				FMath::Abs(A.B - B.B),
				FMath::Abs(A.A - B.A)
			)
		);
	}

	FLinearColor EvaluateBakeColor(
		const FBakeTriangleData& Triangle,
		const FVector2D& ProjectedPosition
	)
	{
		const FVector2D PositionDelta =
			ProjectedPosition - Triangle.ProjectedOrigin;
		return AddBakeColors(
			Triangle.ColorOrigin,
			AddBakeColors(
				ScaleBakeColor(
					Triangle.ColorGradientX,
					PositionDelta.X
				),
				ScaleBakeColor(
					Triangle.ColorGradientY,
					PositionDelta.Y
				)
			)
		);
	}

	FVector2D EvaluateBakeUv(
		const FBakeTriangleData& Triangle,
		const FVector2D& ProjectedPosition
	)
	{
		const FVector2D PositionDelta =
			ProjectedPosition - Triangle.ProjectedOrigin;
		return
			Triangle.UvOrigin +
			Triangle.UvGradientX * PositionDelta.X +
			Triangle.UvGradientY * PositionDelta.Y;
	}

	bool InitializeBakeTriangleAffineData(
		FBakeTriangleData& Triangle,
		const FBakeVertexData WedgeVertices[3]
	)
	{
		Triangle.ProjectionAxis =
			GetBakeProjectionAxis(Triangle.GeometricNormal);
		const FVector2D ProjectedPositions[3] =
		{
			ProjectBakePosition(
				WedgeVertices[0].Position,
				Triangle.ProjectionAxis
			),
			ProjectBakePosition(
				WedgeVertices[1].Position,
				Triangle.ProjectionAxis
			),
			ProjectBakePosition(
				WedgeVertices[2].Position,
				Triangle.ProjectionAxis
			)
		};
		const FVector2D EdgeA =
			ProjectedPositions[1] - ProjectedPositions[0];
		const FVector2D EdgeB =
			ProjectedPositions[2] - ProjectedPositions[0];
		const float Determinant =
			EdgeA.X * EdgeB.Y - EdgeA.Y * EdgeB.X;

		if (FMath::Abs(Determinant) <= SMALL_NUMBER)
		{
			return false;
		}

		Triangle.ProjectedOrigin = ProjectedPositions[0];
		Triangle.ProjectedCentroid =
			(ProjectedPositions[0] +
			 ProjectedPositions[1] +
			 ProjectedPositions[2]) / 3.0f;

		const FVector2D UvEdgeA =
			WedgeVertices[1].UV - WedgeVertices[0].UV;
		const FVector2D UvEdgeB =
			WedgeVertices[2].UV - WedgeVertices[0].UV;
		Triangle.UvGradientX =
			(UvEdgeA * EdgeB.Y - UvEdgeB * EdgeA.Y) /
			Determinant;
		Triangle.UvGradientY =
			(UvEdgeB * EdgeA.X - UvEdgeA * EdgeB.X) /
			Determinant;
		Triangle.UvOrigin = WedgeVertices[0].UV;

		const FLinearColor Colors[3] =
		{
			BakeColorToAttribute(WedgeVertices[0].Color),
			BakeColorToAttribute(WedgeVertices[1].Color),
			BakeColorToAttribute(WedgeVertices[2].Color)
		};
		const FLinearColor ColorEdgeA =
			SubtractBakeColors(Colors[1], Colors[0]);
		const FLinearColor ColorEdgeB =
			SubtractBakeColors(Colors[2], Colors[0]);
		Triangle.ColorGradientX = ScaleBakeColor(
			SubtractBakeColors(
				ScaleBakeColor(ColorEdgeA, EdgeB.Y),
				ScaleBakeColor(ColorEdgeB, EdgeA.Y)
			),
			1.0f / Determinant
		);
		Triangle.ColorGradientY = ScaleBakeColor(
			SubtractBakeColors(
				ScaleBakeColor(ColorEdgeB, EdgeA.X),
				ScaleBakeColor(ColorEdgeA, EdgeB.X)
			),
			1.0f / Determinant
		);
		Triangle.ColorOrigin = Colors[0];
		return true;
	}

	bool BakeAffineAttributesMatch(
		const FBakeTriangleData& A,
		const FBakeTriangleData& B
	)
	{
		if (A.ProjectionAxis != B.ProjectionAxis)
		{
			return false;
		}

		const float UvGradientToleranceSquared = 0.0000000004f;
		// FColor quantization can move a short mask segment by one channel
		// value. The whole component is still rejected later unless every
		// source vertex agrees with the reference affine field to one value.
		const float ColorGradientTolerance = 0.5f;
		const FVector2D TestPosition =
			(A.ProjectedCentroid + B.ProjectedCentroid) * 0.5f;

		return
			(A.UvGradientX - B.UvGradientX).SizeSquared() <=
				UvGradientToleranceSquared &&
			(A.UvGradientY - B.UvGradientY).SizeSquared() <=
				UvGradientToleranceSquared &&
			(EvaluateBakeUv(A, TestPosition) -
			 EvaluateBakeUv(B, TestPosition)).SizeSquared() <= 0.000001f &&
			BakeColorMaxAbsDifference(
				A.ColorGradientX,
				B.ColorGradientX
			) <= ColorGradientTolerance &&
			BakeColorMaxAbsDifference(
				A.ColorGradientY,
				B.ColorGradientY
			) <= ColorGradientTolerance &&
			BakeColorMaxAbsDifference(
				EvaluateBakeColor(A, TestPosition),
				EvaluateBakeColor(B, TestPosition)
			) <= 1.01f;
	}

	bool BakeTrianglesCanConnect(
		const FBakeTriangleData& A,
		const FBakeTriangleData& B
	)
	{
		return
			A.bCanSimplify &&
			B.bCanSimplify &&
			A.MaterialIndex == B.MaterialIndex &&
			A.SmoothingMask == B.SmoothingMask &&
			BakeAffineAttributesMatch(A, B) &&
			NearlySameBakeDirection(
				A.GeometricNormal,
				B.GeometricNormal,
				0.99999f
			) &&
			NearlySameBakeDirection(
				A.ConstantTangentX,
				B.ConstantTangentX
			) &&
			NearlySameBakeDirection(
				A.ConstantTangentY,
				B.ConstantTangentY
			) &&
			NearlySameBakeDirection(
				A.ConstantTangentZ,
				B.ConstantTangentZ
			) &&
			FMath::Abs(A.PlaneDistance - B.PlaneDistance) <= 0.001f;
	}

	bool IsBakePointStrictlyInsideTriangle(
		const FVector2D& Point,
		const FVector2D& A,
		const FVector2D& B,
		const FVector2D& C,
		const float WindingSign
	)
	{
		const float Epsilon = 0.0001f;
		return
			BakeCross2D(A, B, Point) * WindingSign > Epsilon &&
			BakeCross2D(B, C, Point) * WindingSign > Epsilon &&
			BakeCross2D(C, A, Point) * WindingSign > Epsilon;
	}

	void AppendBakeTriangleToRawMesh(
		const FBakeVertexData Vertices[3],
		const int32 MaterialIndex,
		const uint32 SmoothingMask,
		FRawMesh& OutRawMesh,
		TMap<FBakePositionKey, uint32>& PositionIndices
	)
	{
		for (int32 CornerIndex = 0; CornerIndex < 3; ++CornerIndex)
		{
			const FBakeVertexData& Vertex = Vertices[CornerIndex];
			const FBakePositionKey PositionKey(Vertex.Position);
			uint32* ExistingPositionIndex =
				PositionIndices.Find(PositionKey);
			uint32 PositionIndex;

			if (ExistingPositionIndex)
			{
				PositionIndex = *ExistingPositionIndex;
			}
			else
			{
				PositionIndex = static_cast<uint32>(
					OutRawMesh.VertexPositions.Add(Vertex.Position)
				);
				PositionIndices.Add(PositionKey, PositionIndex);
			}

			OutRawMesh.WedgeIndices.Add(PositionIndex);
			OutRawMesh.WedgeTangentX.Add(Vertex.TangentX);
			OutRawMesh.WedgeTangentY.Add(Vertex.TangentY);
			OutRawMesh.WedgeTangentZ.Add(Vertex.TangentZ);
			OutRawMesh.WedgeTexCoords[0].Add(Vertex.UV);
			OutRawMesh.WedgeColors.Add(Vertex.Color);
		}

		OutRawMesh.FaceMaterialIndices.Add(MaterialIndex);
		OutRawMesh.FaceSmoothingMasks.Add(SmoothingMask);
	}

	bool WeldContinuousBakeRawMeshPositions(FRawMesh& InOutRawMesh)
	{
		const int32 OriginalPositionCount =
			InOutRawMesh.VertexPositions.Num();

		if (
			OriginalPositionCount == 0 ||
			InOutRawMesh.WedgeIndices.Num() == 0
			)
		{
			return false;
		}

		TArray<FVector> WeldedPositions;
		WeldedPositions.Reserve(OriginalPositionCount);
		TMap<FBakePositionKey, uint32> PositionIndices;

		for (uint32& WedgeIndex : InOutRawMesh.WedgeIndices)
		{
			if (
				!InOutRawMesh.VertexPositions.IsValidIndex(
					static_cast<int32>(WedgeIndex)
				)
				)
			{
				return false;
			}

			const FVector& Position =
				InOutRawMesh.VertexPositions[WedgeIndex];
			const FBakePositionKey PositionKey(Position);
			uint32* ExistingIndex = PositionIndices.Find(PositionKey);

			if (ExistingIndex)
			{
				WedgeIndex = *ExistingIndex;
				continue;
			}

			const uint32 WeldedIndex = static_cast<uint32>(
				WeldedPositions.Add(Position)
			);
			PositionIndices.Add(PositionKey, WeldedIndex);
			WedgeIndex = WeldedIndex;
		}

		InOutRawMesh.VertexPositions = MoveTemp(WeldedPositions);

		UE_LOG(
			LogTemp,
			Display,
			TEXT("TileMap bake position weld: %d -> %d positions."),
			OriginalPositionCount,
			InOutRawMesh.VertexPositions.Num()
		);

		return InOutRawMesh.IsValidOrFixable();
	}

	bool SimplifyContinuousBakeRawMesh(FRawMesh& InOutRawMesh)
	{
		const int32 OriginalTriangleCount =
			InOutRawMesh.WedgeIndices.Num() / 3;

		if (OriginalTriangleCount < 4)
		{
			return true;
		}

		TArray<FBakeTriangleData> Triangles;
		Triangles.SetNum(OriginalTriangleCount);
		TMap<FBakeEdgeKey, TArray<int32> > EdgeTriangles;
		TMap<FBakePositionKey, TArray<int32> > PositionTriangles;

		for (
			int32 TriangleIndex = 0;
			TriangleIndex < OriginalTriangleCount;
			++TriangleIndex
			)
		{
			FBakeTriangleData& Triangle = Triangles[TriangleIndex];
			Triangle.bCanSimplify = false;
			Triangle.MaterialIndex =
				InOutRawMesh.FaceMaterialIndices.IsValidIndex(TriangleIndex)
					? InOutRawMesh.FaceMaterialIndices[TriangleIndex]
					: 0;
			Triangle.SmoothingMask =
				InOutRawMesh.FaceSmoothingMasks.IsValidIndex(TriangleIndex)
					? InOutRawMesh.FaceSmoothingMasks[TriangleIndex]
					: 0;

			FBakeVertexData WedgeVertices[3];
			bool bValidTriangle = true;

			for (int32 CornerIndex = 0; CornerIndex < 3; ++CornerIndex)
			{
				const int32 WedgeIndex = TriangleIndex * 3 + CornerIndex;
				Triangle.WedgeIndices[CornerIndex] = WedgeIndex;

				if (!ReadBakeWedge(
					InOutRawMesh,
					WedgeIndex,
					WedgeVertices[CornerIndex]
				))
				{
					bValidTriangle = false;
					break;
				}

				Triangle.PositionKeys[CornerIndex] =
					FBakePositionKey(WedgeVertices[CornerIndex].Position);
				PositionTriangles.FindOrAdd(
					Triangle.PositionKeys[CornerIndex]
				).AddUnique(TriangleIndex);
			}

			if (!bValidTriangle)
			{
				continue;
			}

			const FVector EdgeA =
				WedgeVertices[1].Position - WedgeVertices[0].Position;
			const FVector EdgeB =
				WedgeVertices[2].Position - WedgeVertices[0].Position;
			Triangle.GeometricNormal =
				FVector::CrossProduct(EdgeA, EdgeB).GetSafeNormal();

			if (Triangle.GeometricNormal.IsNearlyZero())
			{
				continue;
			}

			Triangle.PlaneDistance = FVector::DotProduct(
				Triangle.GeometricNormal,
				WedgeVertices[0].Position
			);
			Triangle.ConstantTangentX = WedgeVertices[0].TangentX;
			Triangle.ConstantTangentY = WedgeVertices[0].TangentY;
			Triangle.ConstantTangentZ = WedgeVertices[0].TangentZ;
			const bool bAffineDataValid =
				InitializeBakeTriangleAffineData(
					Triangle,
					WedgeVertices
				);

			Triangle.bCanSimplify =
				bAffineDataValid &&
				NearlySameBakeDirection(
					WedgeVertices[0].TangentX,
					WedgeVertices[1].TangentX
				) &&
				NearlySameBakeDirection(
					WedgeVertices[0].TangentX,
					WedgeVertices[2].TangentX
				) &&
				NearlySameBakeDirection(
					WedgeVertices[0].TangentY,
					WedgeVertices[1].TangentY
				) &&
				NearlySameBakeDirection(
					WedgeVertices[0].TangentY,
					WedgeVertices[2].TangentY
				) &&
				NearlySameBakeDirection(
					WedgeVertices[0].TangentZ,
					WedgeVertices[1].TangentZ
				) &&
				NearlySameBakeDirection(
					WedgeVertices[0].TangentZ,
					WedgeVertices[2].TangentZ
				);

			if (!Triangle.bCanSimplify)
			{
				continue;
			}

			for (int32 EdgeIndex = 0; EdgeIndex < 3; ++EdgeIndex)
			{
				EdgeTriangles.FindOrAdd(
					FBakeEdgeKey(
						Triangle.PositionKeys[EdgeIndex],
						Triangle.PositionKeys[(EdgeIndex + 1) % 3]
					)
				).Add(TriangleIndex);
			}
		}

		TArray<int32> ComponentIndices;
		ComponentIndices.Init(-1, OriginalTriangleCount);
		TArray<TArray<int32> > Components;

		for (
			int32 SeedTriangleIndex = 0;
			SeedTriangleIndex < OriginalTriangleCount;
			++SeedTriangleIndex
			)
		{
			if (
				!Triangles[SeedTriangleIndex].bCanSimplify ||
				ComponentIndices[SeedTriangleIndex] >= 0
				)
			{
				continue;
			}

			const int32 ComponentIndex = Components.AddDefaulted();
			TArray<int32>& Component = Components[ComponentIndex];
			TArray<int32> PendingTriangles;
			PendingTriangles.Add(SeedTriangleIndex);
			ComponentIndices[SeedTriangleIndex] = ComponentIndex;

			while (PendingTriangles.Num() > 0)
			{
				const int32 TriangleIndex = PendingTriangles.Pop(false);
				Component.Add(TriangleIndex);
				const FBakeTriangleData& Triangle = Triangles[TriangleIndex];

				for (int32 EdgeIndex = 0; EdgeIndex < 3; ++EdgeIndex)
				{
					const TArray<int32>* NeighborTriangles =
						EdgeTriangles.Find(
							FBakeEdgeKey(
								Triangle.PositionKeys[EdgeIndex],
								Triangle.PositionKeys[(EdgeIndex + 1) % 3]
							)
						);

					if (!NeighborTriangles)
					{
						continue;
					}

					for (const int32 NeighborTriangleIndex : *NeighborTriangles)
					{
						if (
							ComponentIndices[NeighborTriangleIndex] < 0 &&
							BakeTrianglesCanConnect(
								Triangles[SeedTriangleIndex],
								Triangles[NeighborTriangleIndex]
							)
							)
						{
							ComponentIndices[NeighborTriangleIndex] =
								ComponentIndex;
							PendingTriangles.Add(NeighborTriangleIndex);
						}
					}
				}
			}
		}

		TArray<bool> bReplaceTriangle;
		bReplaceTriangle.Init(false, OriginalTriangleCount);
		TArray<FBakeGeneratedTriangle> GeneratedTriangles;

		for (const TArray<int32>& Component : Components)
		{
			if (Component.Num() < 4)
			{
				continue;
			}

			TSet<int32> ComponentSet;
			TMap<FBakeEdgeKey, FBakeBoundaryAccumulator> BoundaryEdges;
			TMap<FBakePositionKey, FBakeVertexData> VertexData;
			bool bAttributesMatch = true;

			for (const int32 TriangleIndex : Component)
			{
				ComponentSet.Add(TriangleIndex);
				const FBakeTriangleData& Triangle = Triangles[TriangleIndex];

				for (int32 CornerIndex = 0; CornerIndex < 3; ++CornerIndex)
				{
					FBakeVertexData WedgeVertex;

					if (!ReadBakeWedge(
						InOutRawMesh,
						Triangle.WedgeIndices[CornerIndex],
						WedgeVertex
					))
					{
						bAttributesMatch = false;
						break;
					}

					FBakeVertexData* ExistingVertex =
						VertexData.Find(Triangle.PositionKeys[CornerIndex]);

					if (ExistingVertex)
					{
						if (!BakeVertexDataMatches(*ExistingVertex, WedgeVertex))
						{
							bAttributesMatch = false;
							break;
						}
					}
					else
					{
						VertexData.Add(
							Triangle.PositionKeys[CornerIndex],
							WedgeVertex
						);
					}
				}

				if (!bAttributesMatch)
				{
					break;
				}

				for (int32 EdgeIndex = 0; EdgeIndex < 3; ++EdgeIndex)
				{
					const FBakePositionKey Start =
						Triangle.PositionKeys[EdgeIndex];
					const FBakePositionKey End =
						Triangle.PositionKeys[(EdgeIndex + 1) % 3];
					FBakeBoundaryAccumulator& Accumulator =
						BoundaryEdges.FindOrAdd(FBakeEdgeKey(Start, End));

					if (Accumulator.Count == 0)
					{
						Accumulator.Start = Start;
						Accumulator.End = End;
					}

					++Accumulator.Count;
				}
			}

			if (!bAttributesMatch)
			{
				continue;
			}

			TArray<FBakeBoundaryEdge> DirectedBoundaryEdges;

			for (
				const TPair<FBakeEdgeKey, FBakeBoundaryAccumulator>& Pair :
				BoundaryEdges
				)
			{
				if (Pair.Value.Count == 1)
				{
					FBakeBoundaryEdge Edge;
					Edge.Start = Pair.Value.Start;
					Edge.End = Pair.Value.End;
					DirectedBoundaryEdges.Add(Edge);
				}
				else if (Pair.Value.Count != 2)
				{
					bAttributesMatch = false;
					break;
				}
			}

			if (
				!bAttributesMatch ||
				DirectedBoundaryEdges.Num() < 3
				)
			{
				continue;
			}

			TMap<FBakePositionKey, FBakePositionKey> NextBoundaryVertex;
			TSet<FBakePositionKey> BoundaryEndVertices;

			for (const FBakeBoundaryEdge& Edge : DirectedBoundaryEdges)
			{
				if (
					NextBoundaryVertex.Contains(Edge.Start) ||
					BoundaryEndVertices.Contains(Edge.End)
					)
				{
					bAttributesMatch = false;
					break;
				}

				NextBoundaryVertex.Add(Edge.Start, Edge.End);
				BoundaryEndVertices.Add(Edge.End);
			}

			if (!bAttributesMatch)
			{
				continue;
			}

			TArray<FBakePositionKey> BoundaryLoop;
			const FBakePositionKey FirstBoundaryVertex =
				DirectedBoundaryEdges[0].Start;
			FBakePositionKey CurrentBoundaryVertex = FirstBoundaryVertex;

			for (
				int32 BoundaryStep = 0;
				BoundaryStep <= DirectedBoundaryEdges.Num();
				++BoundaryStep
				)
			{
				if (
					BoundaryStep > 0 &&
					CurrentBoundaryVertex == FirstBoundaryVertex
					)
				{
					break;
				}

				if (BoundaryLoop.Contains(CurrentBoundaryVertex))
				{
					bAttributesMatch = false;
					break;
				}

				BoundaryLoop.Add(CurrentBoundaryVertex);
				const FBakePositionKey* NextVertex =
					NextBoundaryVertex.Find(CurrentBoundaryVertex);

				if (!NextVertex)
				{
					bAttributesMatch = false;
					break;
				}

				CurrentBoundaryVertex = *NextVertex;
			}

			if (
				!bAttributesMatch ||
				!(CurrentBoundaryVertex == FirstBoundaryVertex) ||
				BoundaryLoop.Num() != DirectedBoundaryEdges.Num()
				)
			{
				continue;
			}

			const FBakeTriangleData& ReferenceTriangle =
				Triangles[Component[0]];
			FBakeVertexData ReferenceVertices[3];

			if (
				!ReadBakeWedge(
					InOutRawMesh,
					ReferenceTriangle.WedgeIndices[0],
					ReferenceVertices[0]
				) ||
				!ReadBakeWedge(
					InOutRawMesh,
					ReferenceTriangle.WedgeIndices[1],
					ReferenceVertices[1]
				) ||
				!ReadBakeWedge(
					InOutRawMesh,
					ReferenceTriangle.WedgeIndices[2],
					ReferenceVertices[2]
				)
				)
			{
				continue;
			}

			const FVector2D ReferencePositions[3] =
			{
				ProjectBakePosition(
					ReferenceVertices[0].Position,
					ReferenceTriangle.ProjectionAxis
				),
				ProjectBakePosition(
					ReferenceVertices[1].Position,
					ReferenceTriangle.ProjectionAxis
				),
				ProjectBakePosition(
					ReferenceVertices[2].Position,
					ReferenceTriangle.ProjectionAxis
				)
			};
			const FVector2D ReferenceEdgeA =
				ReferencePositions[1] - ReferencePositions[0];
			const FVector2D ReferenceEdgeB =
				ReferencePositions[2] - ReferencePositions[0];
			const float UvDeterminant =
				ReferenceEdgeA.X * ReferenceEdgeB.Y -
				ReferenceEdgeA.Y * ReferenceEdgeB.X;

			if (FMath::Abs(UvDeterminant) <= SMALL_NUMBER)
			{
				continue;
			}

			const FVector2D UvEdgeA =
				ReferenceVertices[1].UV - ReferenceVertices[0].UV;
			const FVector2D UvEdgeB =
				ReferenceVertices[2].UV - ReferenceVertices[0].UV;
			const FVector2D UvGradientX =
				(UvEdgeA * ReferenceEdgeB.Y -
				 UvEdgeB * ReferenceEdgeA.Y) / UvDeterminant;
			const FVector2D UvGradientY =
				(UvEdgeB * ReferenceEdgeA.X -
				 UvEdgeA * ReferenceEdgeB.X) / UvDeterminant;

			for (
				const TPair<FBakePositionKey, FBakeVertexData>& Pair :
				VertexData
				)
			{
				const FVector2D ProjectedPosition = ProjectBakePosition(
					Pair.Value.Position,
					ReferenceTriangle.ProjectionAxis
				);
				const FVector2D PositionDelta =
					ProjectedPosition - ReferencePositions[0];
				const FVector2D ExpectedUv =
					ReferenceVertices[0].UV +
					UvGradientX * PositionDelta.X +
					UvGradientY * PositionDelta.Y;
				const FLinearColor ExpectedColor = EvaluateBakeColor(
					ReferenceTriangle,
					ProjectedPosition
				);

				if (
					(ExpectedUv - Pair.Value.UV).SizeSquared() >
					0.000004f ||
					BakeColorMaxAbsDifference(
						ExpectedColor,
						BakeColorToAttribute(Pair.Value.Color)
					) > 1.01f ||
					!NearlySameBakeDirection(
						ReferenceVertices[0].TangentX,
						Pair.Value.TangentX
					) ||
					!NearlySameBakeDirection(
						ReferenceVertices[0].TangentY,
						Pair.Value.TangentY
					) ||
					!NearlySameBakeDirection(
						ReferenceVertices[0].TangentZ,
						Pair.Value.TangentZ
					)
					)
				{
					bAttributesMatch = false;
					break;
				}
			}

			if (!bAttributesMatch)
			{
				continue;
			}

			bool bRemovedBoundaryVertex = true;

			while (
				bRemovedBoundaryVertex &&
				BoundaryLoop.Num() > 3
				)
			{
				bRemovedBoundaryVertex = false;

				for (
					int32 BoundaryIndex = 0;
					BoundaryIndex < BoundaryLoop.Num();
					++BoundaryIndex
					)
				{
					const FBakePositionKey& CurrentKey =
						BoundaryLoop[BoundaryIndex];
					bool bReferencedOutsideComponent = false;
					const TArray<int32>* ReferencingTriangles =
						PositionTriangles.Find(CurrentKey);

					if (ReferencingTriangles)
					{
						for (const int32 ReferencingTriangle : *ReferencingTriangles)
						{
							if (!ComponentSet.Contains(ReferencingTriangle))
							{
								bReferencedOutsideComponent = true;
								break;
							}
						}
					}

					if (bReferencedOutsideComponent)
					{
						continue;
					}

					const int32 PreviousIndex =
						(BoundaryIndex + BoundaryLoop.Num() - 1) %
						BoundaryLoop.Num();
					const int32 NextIndex =
						(BoundaryIndex + 1) % BoundaryLoop.Num();
					const FBakeVertexData& PreviousVertex =
						VertexData.FindChecked(BoundaryLoop[PreviousIndex]);
					const FBakeVertexData& CurrentVertex =
						VertexData.FindChecked(CurrentKey);
					const FBakeVertexData& NextVertex =
						VertexData.FindChecked(BoundaryLoop[NextIndex]);
					const FVector2D PreviousPosition = ProjectBakePosition(
						PreviousVertex.Position,
						ReferenceTriangle.ProjectionAxis
					);
					const FVector2D CurrentPosition = ProjectBakePosition(
						CurrentVertex.Position,
						ReferenceTriangle.ProjectionAxis
					);
					const FVector2D NextPosition = ProjectBakePosition(
						NextVertex.Position,
						ReferenceTriangle.ProjectionAxis
					);
					const FVector2D Segment = NextPosition - PreviousPosition;
					const float SegmentLengthSquared = Segment.SizeSquared();

					if (SegmentLengthSquared <= SMALL_NUMBER)
					{
						continue;
					}

					const float Cross = FMath::Abs(
						BakeCross2D(
							PreviousPosition,
							CurrentPosition,
							NextPosition
						)
					);
					const float CrossTolerance =
						FMath::Max(0.0001f, SegmentLengthSquared * 0.000001f);

					if (Cross > CrossTolerance)
					{
						continue;
					}

					const FVector2D PreviousToCurrent =
						CurrentPosition - PreviousPosition;
					const float InterpolationAlpha =
						(
							PreviousToCurrent.X * Segment.X +
							PreviousToCurrent.Y * Segment.Y
						) / SegmentLengthSquared;

					if (
						InterpolationAlpha <= 0.0f ||
						InterpolationAlpha >= 1.0f
						)
					{
						continue;
					}

					const FVector2D ExpectedUv = FMath::Lerp(
						PreviousVertex.UV,
						NextVertex.UV,
						InterpolationAlpha
					);
					const FLinearColor ExpectedColor = AddBakeColors(
						BakeColorToAttribute(PreviousVertex.Color),
						ScaleBakeColor(
							SubtractBakeColors(
								BakeColorToAttribute(NextVertex.Color),
								BakeColorToAttribute(PreviousVertex.Color)
							),
							InterpolationAlpha
						)
					);

					if (
						(ExpectedUv - CurrentVertex.UV).SizeSquared() >
						0.000004f ||
						BakeColorMaxAbsDifference(
							ExpectedColor,
							BakeColorToAttribute(CurrentVertex.Color)
						) > 1.01f
						)
					{
						continue;
					}

					BoundaryLoop.RemoveAt(BoundaryIndex);
					bRemovedBoundaryVertex = true;
					break;
				}
			}

			if (BoundaryLoop.Num() < 3)
			{
				continue;
			}

			TArray<FVector2D> ProjectedBoundary;
			ProjectedBoundary.Reserve(BoundaryLoop.Num());
			float SignedArea = 0.0f;

			for (int32 BoundaryIndex = 0; BoundaryIndex < BoundaryLoop.Num(); ++BoundaryIndex)
			{
				const FBakeVertexData& Vertex =
					VertexData.FindChecked(BoundaryLoop[BoundaryIndex]);
				ProjectedBoundary.Add(
					ProjectBakePosition(
						Vertex.Position,
						ReferenceTriangle.ProjectionAxis
					)
				);
			}

			for (int32 BoundaryIndex = 0; BoundaryIndex < ProjectedBoundary.Num(); ++BoundaryIndex)
			{
				const FVector2D& A = ProjectedBoundary[BoundaryIndex];
				const FVector2D& B =
					ProjectedBoundary[(BoundaryIndex + 1) % ProjectedBoundary.Num()];
				SignedArea += A.X * B.Y - B.X * A.Y;
			}

			if (FMath::Abs(SignedArea) <= SMALL_NUMBER)
			{
				continue;
			}

			const float WindingSign = SignedArea > 0.0f ? 1.0f : -1.0f;
			TArray<int32> RemainingBoundaryIndices;
			RemainingBoundaryIndices.Reserve(BoundaryLoop.Num());

			for (int32 BoundaryIndex = 0; BoundaryIndex < BoundaryLoop.Num(); ++BoundaryIndex)
			{
				RemainingBoundaryIndices.Add(BoundaryIndex);
			}

			TArray<FBakeGeneratedTriangle> ComponentTriangles;
			int32 EarClipGuard = BoundaryLoop.Num() * BoundaryLoop.Num();

			while (
				RemainingBoundaryIndices.Num() > 3 &&
				EarClipGuard-- > 0
				)
			{
				bool bClippedEar = false;

				for (
					int32 RemainingIndex = 0;
					RemainingIndex < RemainingBoundaryIndices.Num();
					++RemainingIndex
					)
				{
					const int32 PreviousRemainingIndex =
						(RemainingIndex + RemainingBoundaryIndices.Num() - 1) %
						RemainingBoundaryIndices.Num();
					const int32 NextRemainingIndex =
						(RemainingIndex + 1) % RemainingBoundaryIndices.Num();
					const int32 AIndex =
						RemainingBoundaryIndices[PreviousRemainingIndex];
					const int32 BIndex =
						RemainingBoundaryIndices[RemainingIndex];
					const int32 CIndex =
						RemainingBoundaryIndices[NextRemainingIndex];

					if (
						BakeCross2D(
							ProjectedBoundary[AIndex],
							ProjectedBoundary[BIndex],
							ProjectedBoundary[CIndex]
						) * WindingSign <= 0.0001f
						)
					{
						continue;
					}

					bool bContainsBoundaryPoint = false;

					for (const int32 TestIndex : RemainingBoundaryIndices)
					{
						if (
							TestIndex == AIndex ||
							TestIndex == BIndex ||
							TestIndex == CIndex
							)
						{
							continue;
						}

						if (IsBakePointStrictlyInsideTriangle(
							ProjectedBoundary[TestIndex],
							ProjectedBoundary[AIndex],
							ProjectedBoundary[BIndex],
							ProjectedBoundary[CIndex],
							WindingSign
						))
						{
							bContainsBoundaryPoint = true;
							break;
						}
					}

					if (bContainsBoundaryPoint)
					{
						continue;
					}

					FBakeGeneratedTriangle NewTriangle;
					NewTriangle.Vertices[0] =
						VertexData.FindChecked(BoundaryLoop[AIndex]);
					NewTriangle.Vertices[1] =
						VertexData.FindChecked(BoundaryLoop[BIndex]);
					NewTriangle.Vertices[2] =
						VertexData.FindChecked(BoundaryLoop[CIndex]);
					NewTriangle.MaterialIndex =
						ReferenceTriangle.MaterialIndex;
					NewTriangle.SmoothingMask =
						ReferenceTriangle.SmoothingMask;
					ComponentTriangles.Add(NewTriangle);
					RemainingBoundaryIndices.RemoveAt(RemainingIndex);
					bClippedEar = true;
					break;
				}

				if (!bClippedEar)
				{
					break;
				}
			}

			if (RemainingBoundaryIndices.Num() == 3)
			{
				FBakeGeneratedTriangle FinalTriangle;

				for (int32 CornerIndex = 0; CornerIndex < 3; ++CornerIndex)
				{
					FinalTriangle.Vertices[CornerIndex] =
						VertexData.FindChecked(
							BoundaryLoop[
								RemainingBoundaryIndices[CornerIndex]
							]
						);
				}

				FinalTriangle.MaterialIndex =
					ReferenceTriangle.MaterialIndex;
				FinalTriangle.SmoothingMask =
					ReferenceTriangle.SmoothingMask;
				ComponentTriangles.Add(FinalTriangle);
			}

			if (
				RemainingBoundaryIndices.Num() != 3 ||
				ComponentTriangles.Num() >= Component.Num()
				)
			{
				continue;
			}

			bool bWindingMatches = true;

			for (const FBakeGeneratedTriangle& Triangle : ComponentTriangles)
			{
				const FVector NewNormal = FVector::CrossProduct(
					Triangle.Vertices[1].Position - Triangle.Vertices[0].Position,
					Triangle.Vertices[2].Position - Triangle.Vertices[0].Position
				).GetSafeNormal();

				if (
					NewNormal.IsNearlyZero() ||
					FVector::DotProduct(
						NewNormal,
						ReferenceTriangle.GeometricNormal
					) < 0.999f
					)
				{
					bWindingMatches = false;
					break;
				}
			}

			if (!bWindingMatches)
			{
				continue;
			}

			for (const int32 TriangleIndex : Component)
			{
				bReplaceTriangle[TriangleIndex] = true;
			}

			GeneratedTriangles.Append(ComponentTriangles);
		}

		if (GeneratedTriangles.Num() == 0)
		{
			UE_LOG(
				LogTemp,
				Display,
				TEXT("TileMap bake optimizer: no safe coplanar regions found; source topology preserved.")
			);
			return true;
		}

		FRawMesh SimplifiedRawMesh;
		TMap<FBakePositionKey, uint32> PositionIndices;
		int32 ReplacedTriangleCount = 0;

		for (
			int32 TriangleIndex = 0;
			TriangleIndex < OriginalTriangleCount;
			++TriangleIndex
			)
		{
			if (bReplaceTriangle[TriangleIndex])
			{
				++ReplacedTriangleCount;
				continue;
			}

			FBakeVertexData Vertices[3];
			bool bValidTriangle = true;

			for (int32 CornerIndex = 0; CornerIndex < 3; ++CornerIndex)
			{
				if (!ReadBakeWedge(
					InOutRawMesh,
					TriangleIndex * 3 + CornerIndex,
					Vertices[CornerIndex]
				))
				{
					bValidTriangle = false;
					break;
				}
			}

			if (!bValidTriangle)
			{
				return false;
			}

			AppendBakeTriangleToRawMesh(
				Vertices,
				InOutRawMesh.FaceMaterialIndices.IsValidIndex(TriangleIndex)
					? InOutRawMesh.FaceMaterialIndices[TriangleIndex]
					: 0,
				InOutRawMesh.FaceSmoothingMasks.IsValidIndex(TriangleIndex)
					? InOutRawMesh.FaceSmoothingMasks[TriangleIndex]
					: 0,
				SimplifiedRawMesh,
				PositionIndices
			);
		}

		for (const FBakeGeneratedTriangle& Triangle : GeneratedTriangles)
		{
			AppendBakeTriangleToRawMesh(
				Triangle.Vertices,
				Triangle.MaterialIndex,
				Triangle.SmoothingMask,
				SimplifiedRawMesh,
				PositionIndices
			);
		}

		if (!SimplifiedRawMesh.IsValidOrFixable())
		{
			return false;
		}

		const int32 SimplifiedTriangleCount =
			SimplifiedRawMesh.WedgeIndices.Num() / 3;
		UE_LOG(
			LogTemp,
			Display,
			TEXT("TileMap affine cliff-blend bake optimizer: %d -> %d triangles; %d dense coplanar triangles replaced by %d boundary triangles."),
			OriginalTriangleCount,
			SimplifiedTriangleCount,
			ReplacedTriangleCount,
			GeneratedTriangles.Num()
		);

		InOutRawMesh = MoveTemp(SimplifiedRawMesh);
		return true;
	}

	bool BuildOptimizedRawMesh(
		ATileMapTerrainActor* TerrainActor,
		FRawMesh& OutRawMesh,
		TArray<UMaterialInterface*>& OutMaterials
	)
	{
		if (
			!TerrainActor ||
			TerrainActor->OccupiedBlocks.Num() == 0
			)
		{
			return false;
		}

		if (TerrainActor->bUseContinuousTerrainPrototype)
		{
			bool bAppendedGeometry = false;

			TInlineComponentArray<UProceduralMeshComponent*>
				ContinuousComponents(TerrainActor);

			for (
				UProceduralMeshComponent* ContinuousComponent :
				ContinuousComponents
				)
			{
				if (
					!IsValid(ContinuousComponent) ||
					!ContinuousComponent->GetName().StartsWith(
						TEXT("TileContinuousChunk_")
					)
					)
				{
					continue;
				}

				bAppendedGeometry =
					AppendProceduralMeshGeometry(
						ContinuousComponent,
						OutRawMesh,
						OutMaterials
					) ||
					bAppendedGeometry;
			}

			if (bAppendedGeometry)
			{
				if (
					!SimplifyContinuousBakeRawMesh(OutRawMesh) ||
					!WeldContinuousBakeRawMeshPositions(OutRawMesh)
					)
				{
					return false;
				}
			}

			for (
				const FIntVector& GridPosition :
				TerrainActor->OccupiedBlocks
				)
			{
				if (TerrainActor->IsContinuousSurfaceBlock(GridPosition))
				{
					continue;
				}

				const int32 TileType =
					TerrainActor->GetBlockTileType(GridPosition);

				bAppendedGeometry =
					AppendStaticMeshGeometry(
						TerrainActor->GetTileMesh(TileType),
						TerrainActor->GetTileMaterialOverride(
							TileType
						),
						TerrainActor->GetBlockLocalTransform(
							GridPosition
						),
						OutRawMesh,
						OutMaterials
					) ||
					bAppendedGeometry;
			}

			return
				bAppendedGeometry &&
				OutRawMesh.WedgeIndices.Num() > 0 &&
				OutMaterials.Num() > 0 &&
				OutRawMesh.IsValidOrFixable();
		}

		const float Size =
			FMath::Max(TerrainActor->GridSize, 1.0f);

		const bool bDefaultTileIsEngineCube =
			TerrainActor->BlockMesh &&
			TerrainActor->BlockMesh->GetPathName() ==
			TEXT("/Engine/BasicShapes/Cube.Cube");

		const FExposedFace Faces[6] =
		{
			{
				FIntVector(1, 0, 0),
				FVector(1.0f, 0.0f, 0.0f),
				{
					FVector(1.0f, 0.0f, 0.0f),
					FVector(1.0f, 1.0f, 0.0f),
					FVector(1.0f, 1.0f, 1.0f),
					FVector(1.0f, 0.0f, 1.0f)
				}
			},
			{
				FIntVector(-1, 0, 0),
				FVector(-1.0f, 0.0f, 0.0f),
				{
					FVector(0.0f, 1.0f, 0.0f),
					FVector(0.0f, 0.0f, 0.0f),
					FVector(0.0f, 0.0f, 1.0f),
					FVector(0.0f, 1.0f, 1.0f)
				}
			},
			{
				FIntVector(0, 1, 0),
				FVector(0.0f, 1.0f, 0.0f),
				{
					FVector(1.0f, 1.0f, 0.0f),
					FVector(0.0f, 1.0f, 0.0f),
					FVector(0.0f, 1.0f, 1.0f),
					FVector(1.0f, 1.0f, 1.0f)
				}
			},
			{
				FIntVector(0, -1, 0),
				FVector(0.0f, -1.0f, 0.0f),
				{
					FVector(0.0f, 0.0f, 0.0f),
					FVector(1.0f, 0.0f, 0.0f),
					FVector(1.0f, 0.0f, 1.0f),
					FVector(0.0f, 0.0f, 1.0f)
				}
			},
			{
				FIntVector(0, 0, 1),
				FVector(0.0f, 0.0f, 1.0f),
				{
					FVector(0.0f, 0.0f, 1.0f),
					FVector(1.0f, 0.0f, 1.0f),
					FVector(1.0f, 1.0f, 1.0f),
					FVector(0.0f, 1.0f, 1.0f)
				}
			},
			{
				FIntVector(0, 0, -1),
				FVector(0.0f, 0.0f, -1.0f),
				{
					FVector(0.0f, 1.0f, 0.0f),
					FVector(1.0f, 1.0f, 0.0f),
					FVector(1.0f, 0.0f, 0.0f),
					FVector(0.0f, 0.0f, 0.0f)
				}
			}
		};

		TSet<FIntVector> BakedBlocks;

		for (
			const FIntVector& GridPosition :
			TerrainActor->OccupiedBlocks
			)
		{
			if (BakedBlocks.Contains(GridPosition))
			{
				continue;
			}

			BakedBlocks.Add(GridPosition);

			const int32 TileType =
				TerrainActor->GetBlockTileType(GridPosition);

			UStaticMesh* TileMesh =
				TerrainActor->GetTileMesh(TileType);

			UMaterialInterface* MaterialOverride =
				TerrainActor->GetTileMaterialOverride(TileType);

			// Preserve the existing exposed-face optimization for the default
			// engine cube. Modular palette meshes keep their complete geometry.
			if (!(TileType == 0 && bDefaultTileIsEngineCube))
			{
				if (
					!AppendStaticMeshGeometry(
						TileMesh,
						MaterialOverride,
						TerrainActor->GetBlockLocalTransform(
							GridPosition
						),
						OutRawMesh,
						OutMaterials
					)
					)
				{
					return false;
				}

				continue;
			}

			UMaterialInterface* CubeMaterial =
				MaterialOverride
				? MaterialOverride
				: TileMesh->GetMaterial(0);

			const int32 CubeMaterialIndex =
				FindOrAddMaterial(
					OutMaterials,
					CubeMaterial
				);

			const FVector BlockMinimum(
				GridPosition.X * Size,
				GridPosition.Y * Size,
				GridPosition.Z * Size
			);

			for (const FExposedFace& Face : Faces)
			{
				const FIntVector NeighborPosition =
					GridPosition + Face.NeighborOffset;

				if (
					TerrainActor->HasBlock(NeighborPosition) &&
					TerrainActor->GetBlockTileType(
						NeighborPosition
					) == 0
					)
				{
					continue;
				}

				FVector FaceCorners[4];

				for (
					int32 CornerIndex = 0;
					CornerIndex < 4;
					++CornerIndex
					)
				{
					FaceCorners[CornerIndex] =
						BlockMinimum +
						(Face.Corners[CornerIndex] * Size);
				}

					AddRawMeshFace(
						OutRawMesh,
						FaceCorners,
						Face.Normal,
						CubeMaterialIndex
					);
			}
		}

		TInlineComponentArray<UProceduralMeshComponent*>
			PathOverlayComponents(TerrainActor);

		for (
			UProceduralMeshComponent* PathOverlayComponent :
			PathOverlayComponents
			)
		{
			if (
				!IsValid(PathOverlayComponent) ||
				!PathOverlayComponent->GetName().StartsWith(
					TEXT("TilePathOverlayChunk_")
				)
				)
			{
				continue;
			}

			if (!AppendProceduralMeshGeometry(
				PathOverlayComponent,
				OutRawMesh,
				OutMaterials
			))
			{
				return false;
			}
		}

		return
			OutRawMesh.WedgeIndices.Num() > 0 &&
			OutMaterials.Num() > 0 &&
			OutRawMesh.IsValidOrFixable();
	}

	struct FTerrainDetailCandidate
	{
		FIntVector SurfaceBlock;
		ETileMapTerrainDetailPlacement Placement;
		FIntVector Direction;

		FTerrainDetailCandidate()
			:
			SurfaceBlock(FIntVector::ZeroValue),
			Placement(ETileMapTerrainDetailPlacement::Ground),
			Direction(FIntVector::ZeroValue)
		{
		}
	};

	uint32 MakeTerrainDetailSeed(
		int32 BaseSeed,
		const FIntVector& GridPosition,
		ETileMapTerrainDetailPlacement Placement,
		uint32 Salt
	)
	{
		uint32 Result = static_cast<uint32>(BaseSeed) ^ Salt;
		Result ^= static_cast<uint32>(GridPosition.X) * 73856093u;
		Result ^= static_cast<uint32>(GridPosition.Y) * 19349663u;
		Result ^= static_cast<uint32>(GridPosition.Z) * 83492791u;
		Result ^= static_cast<uint32>(Placement) * 2654435761u;
		Result ^= Result >> 16;
		Result *= 2246822519u;
		Result ^= Result >> 13;
		return Result;
	}

	bool IsTerrainDetailPathExcluded(
		ATileMapTerrainActor* TerrainActor,
		const FIntVector& SurfaceBlock
	)
	{
		if (!TerrainActor)
		{
			return true;
		}

		// Keep automatic structural pieces off the path itself and one cell away
		// from it so the readable path silhouette is never narrowed.
		for (int32 XOffset = -1; XOffset <= 1; ++XOffset)
		{
			for (int32 YOffset = -1; YOffset <= 1; ++YOffset)
			{
				if (TerrainActor->HasPaintedPath(
					FIntVector(
						SurfaceBlock.X + XOffset,
						SurfaceBlock.Y + YOffset,
						SurfaceBlock.Z
					)
				))
				{
					return true;
				}
			}
		}

		return false;
	}

	int32 ChooseTerrainDetailDefinition(
		ATileMapTerrainActor* TerrainActor,
		ETileMapTerrainDetailPlacement Placement,
		FRandomStream& RandomStream
	)
	{
		if (!TerrainActor)
		{
			return INDEX_NONE;
		}

		float TotalWeight = 0.0f;

		for (
			const FTileMapTerrainDetailDefinition& Definition :
				TerrainActor->TerrainDetailPalette
			)
		{
			if (
				Definition.Placement == Placement &&
				Definition.Mesh &&
				Definition.Weight > 0.0f
				)
			{
				TotalWeight += Definition.Weight;
			}
		}

		if (TotalWeight <= SMALL_NUMBER)
		{
			return INDEX_NONE;
		}

		float RemainingWeight =
			RandomStream.FRandRange(0.0f, TotalWeight);

		for (
			int32 DefinitionIndex = 0;
			DefinitionIndex < TerrainActor->TerrainDetailPalette.Num();
			++DefinitionIndex
			)
		{
			const FTileMapTerrainDetailDefinition& Definition =
				TerrainActor->TerrainDetailPalette[DefinitionIndex];

			if (
				Definition.Placement != Placement ||
				!Definition.Mesh ||
				Definition.Weight <= 0.0f
				)
			{
				continue;
			}

			RemainingWeight -= Definition.Weight;

			if (RemainingWeight <= 0.0f)
			{
				return DefinitionIndex;
			}
		}

		return INDEX_NONE;
	}

	bool IsTerrainDetailTooClose(
		const FVector& LocalLocation,
		const TArray<FVector>& AcceptedLocations,
		float MinimumSpacing,
		float GridSize
	)
	{
		if (MinimumSpacing <= 0.0f)
		{
			return false;
		}

		const float MinimumSpacingSquared =
			MinimumSpacing * MinimumSpacing;

		for (const FVector& AcceptedLocation : AcceptedLocations)
		{
			const FVector Delta = LocalLocation - AcceptedLocation;

			// Details on independently stacked platforms remain eligible when they
			// are separated by at least one complete grid level.
			if (
				FMath::Abs(Delta.Z) < GridSize * 0.75f &&
				FVector2D(Delta.X, Delta.Y).SizeSquared() <
					MinimumSpacingSquared
				)
			{
				return true;
			}
		}

		return false;
	}

	int32 GenerateBakedTerrainDetails(
		ATileMapTerrainActor* TerrainActor,
		UWorld* EditorWorld,
		ULevel* ActorLevel,
		AActor*& OutDetailActor
	)
	{
		OutDetailActor = nullptr;

		if (
			!TerrainActor ||
			!EditorWorld ||
			!TerrainActor->bGenerateTerrainDetailsOnBake ||
			TerrainActor->TerrainDetailPalette.Num() == 0 ||
			TerrainActor->TerrainDetailMaximumInstances <= 0
			)
		{
			return 0;
		}

		const float Density = FMath::Clamp(
			TerrainActor->TerrainDetailDensity,
			0.0f,
			1.0f
		);

		if (Density <= 0.0f)
		{
			return 0;
		}

		const FIntVector CardinalDirections[4] =
		{
			FIntVector(1, 0, 0),
			FIntVector(0, 1, 0),
			FIntVector(-1, 0, 0),
			FIntVector(0, -1, 0)
		};

		TArray<FIntVector> SurfaceBlocks;

		for (
			const FIntVector& GridPosition :
				TerrainActor->OccupiedBlocks
			)
		{
			if (
				TerrainActor->IsTerrainDetailSurfaceBlock(GridPosition) &&
				!IsTerrainDetailPathExcluded(TerrainActor, GridPosition)
				)
			{
				SurfaceBlocks.Add(GridPosition);
			}
		}

		SurfaceBlocks.Sort(
			[](const FIntVector& A, const FIntVector& B)
			{
				if (A.Z != B.Z)
				{
					return A.Z < B.Z;
				}

				if (A.X != B.X)
				{
					return A.X < B.X;
				}

				return A.Y < B.Y;
			}
		);

		TArray<FTerrainDetailCandidate> Candidates;

		for (const FIntVector& SurfaceBlock : SurfaceBlocks)
		{
			TArray<FIntVector> CliffTopDirections;
			TArray<FIntVector> CliffBaseDirections;

			for (const FIntVector& Direction : CardinalDirections)
			{
				if (TerrainActor->HasBlock(
					SurfaceBlock + Direction + FIntVector(0, 0, 1)
				))
				{
					CliffBaseDirections.Add(Direction);
				}
				else if (!TerrainActor->HasBlock(SurfaceBlock + Direction))
				{
					CliffTopDirections.Add(Direction);
				}
			}

			FTerrainDetailCandidate Candidate;
			Candidate.SurfaceBlock = SurfaceBlock;

			if (CliffBaseDirections.Num() > 0)
			{
				Candidate.Placement =
					ETileMapTerrainDetailPlacement::CliffBase;

				FRandomStream DirectionStream(
					static_cast<int32>(
						MakeTerrainDetailSeed(
							TerrainActor->TerrainDetailSeed,
							SurfaceBlock,
							Candidate.Placement,
							0x12f4a9bdu
						)
					)
				);

				Candidate.Direction = CliffBaseDirections[
					DirectionStream.RandRange(
						0,
						CliffBaseDirections.Num() - 1
					)
				];
			}
			else if (CliffTopDirections.Num() > 0)
			{
				Candidate.Placement =
					ETileMapTerrainDetailPlacement::CliffTop;

				FRandomStream DirectionStream(
					static_cast<int32>(
						MakeTerrainDetailSeed(
							TerrainActor->TerrainDetailSeed,
							SurfaceBlock,
							Candidate.Placement,
							0x88c7e35bu
						)
					)
				);

				Candidate.Direction = CliffTopDirections[
					DirectionStream.RandRange(
						0,
						CliffTopDirections.Num() - 1
					)
				];
			}
			else
			{
				Candidate.Placement =
					ETileMapTerrainDetailPlacement::Ground;
			}

			Candidates.Add(Candidate);
		}

		const float GridSize =
			FMath::Max(TerrainActor->GridSize, 1.0f);
		const float GridScale = GridSize / 100.0f;
		const float MinimumSpacing =
			FMath::Max(TerrainActor->TerrainDetailMinimumSpacingCells, 0) *
			GridSize;
		const float EdgeInset = FMath::Clamp(
			TerrainActor->TerrainDetailEdgeInset * GridScale,
			0.0f,
			GridSize * 0.5f
		);
		const int32 MaximumInstances = FMath::Clamp(
			TerrainActor->TerrainDetailMaximumInstances,
			0,
			4096
		);

		TMap<int32, TArray<FTransform> > InstancesByDefinition;
		TArray<FVector> AcceptedLocations;

		for (const FTerrainDetailCandidate& Candidate : Candidates)
		{
			if (AcceptedLocations.Num() >= MaximumInstances)
			{
				break;
			}

			FRandomStream RandomStream(
				static_cast<int32>(
					MakeTerrainDetailSeed(
						TerrainActor->TerrainDetailSeed,
						Candidate.SurfaceBlock,
						Candidate.Placement,
						0x6a09e667u
					)
				)
			);

			if (RandomStream.FRand() > Density)
			{
				continue;
			}

			const int32 DefinitionIndex =
				ChooseTerrainDetailDefinition(
					TerrainActor,
					Candidate.Placement,
					RandomStream
				);

			if (
				!TerrainActor->TerrainDetailPalette.IsValidIndex(
					DefinitionIndex
				)
				)
			{
				continue;
			}

			const FTileMapTerrainDetailDefinition& Definition =
				TerrainActor->TerrainDetailPalette[DefinitionIndex];

			FVector LocalLocation =
				TerrainActor->GridToLocal(Candidate.SurfaceBlock) +
				FVector(
					0.0f,
					0.0f,
					(GridSize * 0.5f) -
						(FMath::Max(Definition.SinkDepth, 0.0f) * GridScale)
				);

			if (Candidate.Placement == ETileMapTerrainDetailPlacement::Ground)
			{
				const float JitterLimit = GridSize * 0.25f;
				LocalLocation.X += RandomStream.FRandRange(
					-JitterLimit,
					JitterLimit
				);
				LocalLocation.Y += RandomStream.FRandRange(
					-JitterLimit,
					JitterLimit
				);
			}
			else
			{
				const FVector Direction(
					static_cast<float>(Candidate.Direction.X),
					static_cast<float>(Candidate.Direction.Y),
					0.0f
				);
				const FVector Tangent(-Direction.Y, Direction.X, 0.0f);
				LocalLocation += Direction * ((GridSize * 0.5f) - EdgeInset);
				LocalLocation += Tangent * RandomStream.FRandRange(
					-GridSize * 0.2f,
					GridSize * 0.2f
				);
			}

			if (IsTerrainDetailTooClose(
				LocalLocation,
				AcceptedLocations,
				MinimumSpacing,
				GridSize
			))
			{
				continue;
			}

			float YawDegrees = 0.0f;

			if (Definition.bRandomYaw)
			{
				YawDegrees = RandomStream.FRandRange(0.0f, 360.0f);
			}
			else if (Candidate.Direction != FIntVector::ZeroValue)
			{
				YawDegrees = FMath::RadiansToDegrees(
					FMath::Atan2(
						static_cast<float>(Candidate.Direction.Y),
						static_cast<float>(Candidate.Direction.X)
					)
				);
			}

			const float MinimumUniformScale = FMath::Max(
				FMath::Min(
					Definition.UniformScaleRange.X,
					Definition.UniformScaleRange.Y
				),
				0.01f
			);
			const float MaximumUniformScale = FMath::Max(
				FMath::Max(
					Definition.UniformScaleRange.X,
					Definition.UniformScaleRange.Y
				),
				MinimumUniformScale
			);
			const float UniformScale = RandomStream.FRandRange(
				MinimumUniformScale,
				MaximumUniformScale
			);

			InstancesByDefinition.FindOrAdd(DefinitionIndex).Add(
				FTransform(
					FRotator(0.0f, YawDegrees, 0.0f),
					LocalLocation,
					Definition.MeshScale * (GridScale * UniformScale)
				)
			);
			AcceptedLocations.Add(LocalLocation);
		}

		if (AcceptedLocations.Num() == 0)
		{
			return 0;
		}

		FActorSpawnParameters SpawnParameters;
		SpawnParameters.ObjectFlags |= RF_Transactional;
		SpawnParameters.OverrideLevel = ActorLevel;

		AActor* DetailActor = EditorWorld->SpawnActor<AActor>(
			TerrainActor->GetActorLocation(),
			TerrainActor->GetActorRotation(),
			SpawnParameters
		);

		if (!DetailActor)
		{
			return 0;
		}

		DetailActor->SetActorLabel(TEXT("TileMap_Terrain_Details_Baked"));
		DetailActor->Tags.AddUnique(FName(TEXT("TileMapTerrainDetails")));

		USceneComponent* DetailRoot = NewObject<USceneComponent>(
			DetailActor,
			FName(TEXT("TerrainDetailRoot")),
			RF_Transactional
		);

		if (!DetailRoot)
		{
			DetailActor->Destroy();
			return 0;
		}

		DetailRoot->CreationMethod = EComponentCreationMethod::Instance;
		DetailActor->AddInstanceComponent(DetailRoot);
		DetailActor->SetRootComponent(DetailRoot);
		DetailRoot->RegisterComponent();

		// A native base AActor has no root at spawn time, so its requested spawn
		// transform is not reliable until this instance root exists. Apply the
		// complete terrain transform now; every stored detail transform remains
		// local to this actor and therefore lines up with the baked terrain.
		DetailActor->SetActorTransform(TerrainActor->GetActorTransform());

		for (
			const TPair<int32, TArray<FTransform> >& Pair :
				InstancesByDefinition
			)
		{
			if (
				!TerrainActor->TerrainDetailPalette.IsValidIndex(Pair.Key) ||
				Pair.Value.Num() == 0
				)
			{
				continue;
			}

			const FTileMapTerrainDetailDefinition& Definition =
				TerrainActor->TerrainDetailPalette[Pair.Key];

			if (!Definition.Mesh)
			{
				continue;
			}

			const FName ComponentName = MakeUniqueObjectName(
				DetailActor,
				UHierarchicalInstancedStaticMeshComponent::StaticClass(),
				FName(
					*FString::Printf(
						TEXT("TerrainDetail_%d"),
						Pair.Key
					)
				)
			);

			UHierarchicalInstancedStaticMeshComponent* DetailComponent =
				NewObject<UHierarchicalInstancedStaticMeshComponent>(
					DetailActor,
					ComponentName,
					RF_Transactional
				);

			if (!DetailComponent)
			{
				continue;
			}

			DetailComponent->CreationMethod =
				EComponentCreationMethod::Instance;
			DetailComponent->SetupAttachment(DetailRoot);
			DetailComponent->SetRelativeTransform(FTransform::Identity);
			DetailComponent->SetMobility(EComponentMobility::Movable);
			DetailComponent->SetStaticMesh(Definition.Mesh);
			DetailComponent->SetGenerateOverlapEvents(false);
			DetailComponent->SetCastShadow(true);
			DetailComponent->bCastStaticShadow = false;
			DetailComponent->bCastDynamicShadow = true;
			const int32 StartCullDistance = FMath::Max(
				TerrainActor->TerrainDetailStartCullDistance,
				0
			);
			const int32 EndCullDistance = FMath::Max(
				TerrainActor->TerrainDetailEndCullDistance,
				StartCullDistance
			);
			DetailComponent->SetCullDistances(
				StartCullDistance,
				EndCullDistance
			);

			if (Definition.bEnableCollision)
			{
				DetailComponent->SetCollisionProfileName(TEXT("BlockAll"));
				DetailComponent->SetCollisionEnabled(
					ECollisionEnabled::QueryAndPhysics
				);
			}
			else
			{
				DetailComponent->SetCollisionEnabled(
					ECollisionEnabled::NoCollision
				);
			}

			if (Definition.MaterialOverride)
			{
				DetailComponent->SetMaterial(
					0,
					Definition.MaterialOverride
				);
			}

			DetailActor->AddInstanceComponent(DetailComponent);
			DetailComponent->RegisterComponent();

			for (const FTransform& InstanceTransform : Pair.Value)
			{
				DetailComponent->AddInstance(InstanceTransform);
			}

			DetailComponent->MarkRenderStateDirty();
		}

		OutDetailActor = DetailActor;
		return AcceptedLocations.Num();
	}

	UStaticMesh* BakeTerrainToStaticMesh(
		ATileMapTerrainActor* TerrainActor,
		int32* OutTerrainDetailCount = nullptr
	)
	{
		if (OutTerrainDetailCount)
		{
			*OutTerrainDetailCount = 0;
		}

		FRawMesh RawMesh;
		TArray<UMaterialInterface*> Materials;

		if (
			!BuildOptimizedRawMesh(
				TerrainActor,
				RawMesh,
				Materials
			)
			)
		{
			return nullptr;
		}

		FString PackageName;
		FString AssetName;

		FAssetToolsModule& AssetToolsModule =
			FModuleManager::LoadModuleChecked<
				FAssetToolsModule
			>(TEXT("AssetTools"));

		AssetToolsModule.Get().CreateUniqueAssetName(
			TEXT("/Game/TileMapBakes/SM_TileMapTerrain"),
			TEXT(""),
			PackageName,
			AssetName
		);

		UPackage* Package =
			CreatePackage(nullptr, *PackageName);

		if (!Package)
		{
			return nullptr;
		}

		UStaticMesh* StaticMesh =
			NewObject<UStaticMesh>(
				Package,
				FName(*AssetName),
				RF_Public | RF_Standalone
			);

		if (!StaticMesh)
		{
			return nullptr;
		}

		StaticMesh->InitResources();

		FStaticMeshSourceModel& SourceModel =
			StaticMesh->AddSourceModel();

		// The continuous terrain already supplies its authored normal and tangent
		// frames. Recomputing them with per-face smoothing masks needlessly splits
		// the optimized mesh back into many render vertices.
		SourceModel.BuildSettings.bRecomputeNormals = false;
		SourceModel.BuildSettings.bRecomputeTangents = false;
		SourceModel.BuildSettings.bRemoveDegenerates = true;
		SourceModel.BuildSettings.bUseMikkTSpace = true;
		SourceModel.BuildSettings.bGenerateLightmapUVs = true;
		SourceModel.BuildSettings.SrcLightmapIndex = 0;
		SourceModel.BuildSettings.DstLightmapIndex = 1;

		SourceModel.RawMeshBulkData->SaveRawMesh(RawMesh);

		for (UMaterialInterface* Material : Materials)
		{
			StaticMesh->StaticMaterials.Add(
				FStaticMaterial(Material)
			);
		}

		for (
			int32 MaterialIndex = 0;
			MaterialIndex < Materials.Num();
			++MaterialIndex
			)
		{
			FMeshSectionInfo SectionInfo =
				StaticMesh->SectionInfoMap.Get(
					0,
					MaterialIndex
				);

			SectionInfo.MaterialIndex = MaterialIndex;
			SectionInfo.bEnableCollision = true;
			StaticMesh->SectionInfoMap.Set(
				0,
				MaterialIndex,
				SectionInfo
			);
		}

		StaticMesh->LightMapCoordinateIndex = 1;
		StaticMesh->LightMapResolution = 64;
		StaticMesh->CreateBodySetup();

		if (StaticMesh->BodySetup)
		{
			StaticMesh->BodySetup->CollisionTraceFlag =
				CTF_UseComplexAsSimple;
		}

		StaticMesh->Build(false);
		StaticMesh->MarkPackageDirty();
		StaticMesh->PostEditChange();
		Package->MarkPackageDirty();

		FAssetRegistryModule::AssetCreated(StaticMesh);

		UWorld* EditorWorld = TerrainActor->GetWorld();

		if (EditorWorld)
		{
			const FScopedTransaction Transaction(
				LOCTEXT(
					"PlaceBakedTerrainTransaction",
					"Place Baked Tile Map Terrain"
				)
			);

			EditorWorld->Modify();

			ULevel* ActorLevel = TerrainActor->GetLevel();

			if (ActorLevel)
			{
				ActorLevel->Modify();
			}

			FActorSpawnParameters SpawnParameters;
			SpawnParameters.ObjectFlags |= RF_Transactional;
			SpawnParameters.OverrideLevel = ActorLevel;

			AStaticMeshActor* BakedActor =
				EditorWorld->SpawnActor<AStaticMeshActor>(
					TerrainActor->GetActorLocation(),
					TerrainActor->GetActorRotation(),
					SpawnParameters
				);

			if (BakedActor)
			{
				BakedActor->SetActorScale3D(
					TerrainActor->GetActorScale3D()
				);

				BakedActor->SetActorLabel(
					TEXT("TileMap_Terrain_Baked")
				);

				UStaticMeshComponent* BakedComponent =
					BakedActor->GetStaticMeshComponent();

				if (BakedComponent)
				{
					BakedComponent->SetMobility(
						EComponentMobility::Movable
					);

					BakedComponent->SetStaticMesh(StaticMesh);
				}

				AActor* BakedDetailActor = nullptr;
				const int32 BakedDetailCount =
					GenerateBakedTerrainDetails(
						TerrainActor,
						EditorWorld,
						ActorLevel,
						BakedDetailActor
					);

				if (OutTerrainDetailCount)
				{
					*OutTerrainDetailCount = BakedDetailCount;
				}

				if (GEditor)
				{
					GEditor->SelectNone(false, true, false);
					GEditor->SelectActor(
						BakedActor,
						true,
						true,
						true
					);
				}
			}
		}

		if (GEditor)
		{
			TArray<UObject*> ObjectsToSync;
			ObjectsToSync.Add(StaticMesh);
			GEditor->SyncBrowserToObjects(ObjectsToSync);
		}

		return StaticMesh;
	}
}

int32 FTileMapEdModeToolkit::EnsureSlantTile(
	ATileMapTerrainActor* TerrainActor,
	ETileMapSlantMode SlantMode,
	float SlantAngle,
	int32 MaterialTileType,
	int32 RampSegmentIndex,
	int32 RampSegmentCount
)
{
	if (!TerrainActor)
	{
		return INDEX_NONE;
	}

	const float SafeAngle =
		SlantMode == ETileMapSlantMode::DiagonalEdge
			? 45.0f
			: (
				SlantMode == ETileMapSlantMode::Stairs
					? FMath::RadiansToDegrees(FMath::Atan(2.0f / 3.0f))
					: FMath::Clamp(SlantAngle, 5.0f, 45.0f)
			);

	const int32 SafeSegmentCount =
		SlantMode == ETileMapSlantMode::Stairs
			? 2
			: (
				SlantMode == ETileMapSlantMode::Ramp
					? FMath::Clamp(RampSegmentCount, 1, 8)
					: 1
			);

	const int32 SafeSegmentIndex =
		SlantMode != ETileMapSlantMode::DiagonalEdge
		? FMath::Clamp(
			RampSegmentIndex,
			0,
			SafeSegmentCount - 1
		)
		: 0;

	const float EffectiveAngle =
		SlantMode == ETileMapSlantMode::Ramp
		? FMath::RadiansToDegrees(
			FMath::Atan(1.0f / SafeSegmentCount)
		)
		: SafeAngle;

	const int32 AngleTenths =
		FMath::RoundToInt(EffectiveAngle * 10.0f);

	UMaterialInterface* TileMaterial =
		TerrainActor->GetTileMaterialOverride(MaterialTileType);

	if (!TileMaterial)
	{
		UStaticMesh* MaterialSourceMesh =
			TerrainActor->GetTileMesh(MaterialTileType);

		if (MaterialSourceMesh)
		{
			TileMaterial = MaterialSourceMesh->GetMaterial(0);
		}
	}

	if (!TileMaterial)
	{
		TileMaterial = UMaterial::GetDefaultMaterial(MD_Surface);
	}

	const uint32 MaterialHash =
		GetTypeHash(TileMaterial->GetPathName());

	const TCHAR* ShapePrefix =
		SlantMode == ETileMapSlantMode::Ramp
		? TEXT("Ramp")
		: (
			SlantMode == ETileMapSlantMode::Stairs
				? TEXT("Stair")
				: TEXT("DiagonalEdge")
		);

	const FString AssetName =
		SlantMode != ETileMapSlantMode::DiagonalEdge
		? FString::Printf(
			TEXT("SM_TileMapV4%s_N%02d_S%02d_A%03d_M%08X"),
			ShapePrefix,
			SafeSegmentCount,
			SafeSegmentIndex,
			AngleTenths,
			MaterialHash
		)
		: FString::Printf(
			TEXT("SM_TileMapV4%s_A%03d_M%08X"),
			ShapePrefix,
			AngleTenths,
			MaterialHash
		);

	const FName TileName(*AssetName);

	for (int32 PaletteIndex = 0;
		PaletteIndex < TerrainActor->TilePalette.Num();
		++PaletteIndex)
	{
		const FTileMapTileDefinition& ExistingDefinition =
			TerrainActor->TilePalette[PaletteIndex];

		if (
			ExistingDefinition.TileName == TileName &&
			ExistingDefinition.Mesh
			)
		{
			return PaletteIndex + 1;
		}
	}

	const FString PackageName =
		FString::Printf(
			TEXT("/Game/TileMapGenerated/%s"),
			*AssetName
		);

	const FString ObjectPath = FString::Printf(
		TEXT("%s.%s"),
		*PackageName,
		*AssetName
	);

	UStaticMesh* StaticMesh =
		LoadObject<UStaticMesh>(
			nullptr,
			*ObjectPath
		);

	if (!StaticMesh)
	{
		FRawMesh RawMesh;
		const int32 MaterialIndex = 0;

		if (SlantMode == ETileMapSlantMode::Ramp)
		{
			// The complete run occupies the selected cells themselves. It
			// rises from the bottom of the first block to the top of the last
			// block instead of placing another ramp volume above full cubes.
			const float SegmentRise =
				100.0f / SafeSegmentCount;

			const float LowTopZ =
				-50.0f + SafeSegmentIndex * SegmentRise;

			const float HighTopZ =
				LowTopZ + SegmentRise;

			const FVector BottomLowFront(-50.0f, -50.0f, -50.0f);
			const FVector BottomHighFront(50.0f, -50.0f, -50.0f);
			const FVector BottomLowBack(-50.0f, 50.0f, -50.0f);
			const FVector BottomHighBack(50.0f, 50.0f, -50.0f);

			const FVector TopLowFront(-50.0f, -50.0f, LowTopZ);
			const FVector TopHighFront(50.0f, -50.0f, HighTopZ);
			const FVector TopLowBack(-50.0f, 50.0f, LowTopZ);
			const FVector TopHighBack(50.0f, 50.0f, HighTopZ);

			const FVector BottomFace[4] =
			{
				BottomLowBack,
				BottomHighBack,
				BottomHighFront,
				BottomLowFront
			};

			const FVector LowEndFace[4] =
			{
				BottomLowBack,
				BottomLowFront,
				TopLowFront,
				TopLowBack
			};

			const FVector HighEndFace[4] =
			{
				BottomHighFront,
				BottomHighBack,
				TopHighBack,
				TopHighFront
			};

			const FVector FrontFace[4] =
			{
				BottomLowFront,
				BottomHighFront,
				TopHighFront,
				TopLowFront
			};

			const FVector BackFace[4] =
			{
				BottomHighBack,
				BottomLowBack,
				TopLowBack,
				TopHighBack
			};

			const FVector SlopeFace[4] =
			{
				TopLowFront,
				TopHighFront,
				TopHighBack,
				TopLowBack
			};

			const FVector SlopeNormal =
				FVector(-SegmentRise, 0.0f, 100.0f)
				.GetSafeNormal();

			TileMapEdModeToolkitLocal::AddRawMeshFace(
				RawMesh,
				BottomFace,
				-FVector::UpVector,
				MaterialIndex
			);

			if (SafeSegmentIndex > 0)
			{
				TileMapEdModeToolkitLocal::AddRawMeshFace(
					RawMesh,
					LowEndFace,
					-FVector::ForwardVector,
					MaterialIndex
				);
			}

			TileMapEdModeToolkitLocal::AddRawMeshFace(
				RawMesh,
				HighEndFace,
				FVector::ForwardVector,
				MaterialIndex
			);

			if (SafeSegmentIndex == 0)
			{
				TileMapEdModeToolkitLocal::AddRawMeshTriangle(
					RawMesh,
					BottomLowFront,
					BottomHighFront,
					TopHighFront,
					-FVector::RightVector,
					MaterialIndex
				);

				TileMapEdModeToolkitLocal::AddRawMeshTriangle(
					RawMesh,
					BottomLowBack,
					TopHighBack,
					BottomHighBack,
					FVector::RightVector,
					MaterialIndex
				);
			}
			else
			{
				TileMapEdModeToolkitLocal::AddRawMeshFace(
					RawMesh,
					FrontFace,
					-FVector::RightVector,
					MaterialIndex
				);

				TileMapEdModeToolkitLocal::AddRawMeshFace(
					RawMesh,
					BackFace,
					FVector::RightVector,
					MaterialIndex
				);
			}

			TileMapEdModeToolkitLocal::AddRawMeshFace(
				RawMesh,
				SlopeFace,
				SlopeNormal,
				MaterialIndex
			);
		}
		else if (SlantMode == ETileMapSlantMode::Stairs)
		{
			const int32 StepCount = 12;
			const float LowLandingLength = 25.0f;
			const float StepDepth = 150.0f / StepCount;
			const float StepRise = 100.0f / StepCount;
			const float SegmentDistanceStart =
				SafeSegmentIndex * 100.0f;
			const float SegmentDistanceEnd =
				SegmentDistanceStart + 100.0f;

			auto ToLocalX =
				[&](float Distance)
				{
					return
						Distance -
						SegmentDistanceStart -
						50.0f;
				};

			auto GetLocalHeight =
				[&](float Distance)
				{
					const int32 CompletedSteps = FMath::Clamp(
						FMath::FloorToInt(
							(Distance - LowLandingLength) / StepDepth
						) + 1,
						0,
						StepCount
					);

					return -50.0f + (CompletedSteps * StepRise);
				};

			TArray<float> ProfileDistances;
			ProfileDistances.Add(SegmentDistanceStart);
			ProfileDistances.Add(SegmentDistanceEnd);

			for (int32 StepIndex = 1;
				StepIndex <= StepCount;
				++StepIndex)
			{
				const float RiserDistance =
					LowLandingLength + ((StepIndex - 1) * StepDepth);

				if (
					RiserDistance >
						SegmentDistanceStart + KINDA_SMALL_NUMBER &&
					RiserDistance <
						SegmentDistanceEnd - KINDA_SMALL_NUMBER
					)
				{
					ProfileDistances.Add(RiserDistance);
				}
			}

			ProfileDistances.Sort();

			for (int32 IntervalIndex = 0;
				IntervalIndex < ProfileDistances.Num() - 1;
				++IntervalIndex)
			{
				const float DistanceA = ProfileDistances[IntervalIndex];
				const float DistanceB = ProfileDistances[IntervalIndex + 1];
				const float LocalA = ToLocalX(DistanceA);
				const float LocalB = ToLocalX(DistanceB);
				const float Height = GetLocalHeight(
					(DistanceA + DistanceB) * 0.5f
				);
				const FVector Tread[4] =
				{
					FVector(LocalA, -50.0f, Height),
					FVector(LocalB, -50.0f, Height),
					FVector(LocalB, 50.0f, Height),
					FVector(LocalA, 50.0f, Height)
				};

				TileMapEdModeToolkitLocal::AddRawMeshFace(
					RawMesh,
					Tread,
					FVector::UpVector,
					MaterialIndex
				);

				if (Height > -50.0f + KINDA_SMALL_NUMBER)
				{
					const FVector NegativeSide[4] =
					{
						FVector(LocalB, -50.0f, -50.0f),
						FVector(LocalA, -50.0f, -50.0f),
						FVector(LocalA, -50.0f, Height),
						FVector(LocalB, -50.0f, Height)
					};
					const FVector PositiveSide[4] =
					{
						FVector(LocalA, 50.0f, -50.0f),
						FVector(LocalB, 50.0f, -50.0f),
						FVector(LocalB, 50.0f, Height),
						FVector(LocalA, 50.0f, Height)
					};

					TileMapEdModeToolkitLocal::AddRawMeshFace(
						RawMesh,
						NegativeSide,
						-FVector::RightVector,
						MaterialIndex
					);
					TileMapEdModeToolkitLocal::AddRawMeshFace(
						RawMesh,
						PositiveSide,
						FVector::RightVector,
						MaterialIndex
					);
				}
			}

			for (int32 StepIndex = 1;
				StepIndex <= StepCount;
				++StepIndex)
			{
				const float RiserDistance =
					LowLandingLength + ((StepIndex - 1) * StepDepth);

				if (
					RiserDistance <
						SegmentDistanceStart - KINDA_SMALL_NUMBER ||
					RiserDistance >=
						SegmentDistanceEnd - KINDA_SMALL_NUMBER
					)
				{
					continue;
				}

				const float LocalX = ToLocalX(RiserDistance);
				const float LowHeight =
					-50.0f + ((StepIndex - 1) * StepRise);
				const float HighHeight =
					-50.0f + (StepIndex * StepRise);
				const FVector Riser[4] =
				{
					FVector(LocalX, 50.0f, LowHeight),
					FVector(LocalX, -50.0f, LowHeight),
					FVector(LocalX, -50.0f, HighHeight),
					FVector(LocalX, 50.0f, HighHeight)
				};

				TileMapEdModeToolkitLocal::AddRawMeshFace(
					RawMesh,
					Riser,
					-FVector::ForwardVector,
					MaterialIndex
				);
			}

			const FVector BottomFace[4] =
			{
				FVector(-50.0f, 50.0f, -50.0f),
				FVector(50.0f, 50.0f, -50.0f),
				FVector(50.0f, -50.0f, -50.0f),
				FVector(-50.0f, -50.0f, -50.0f)
			};

			TileMapEdModeToolkitLocal::AddRawMeshFace(
				RawMesh,
				BottomFace,
				-FVector::UpVector,
				MaterialIndex
			);

			if (SafeSegmentIndex == SafeSegmentCount - 1)
			{
				const float HighHeight = GetLocalHeight(
					SegmentDistanceEnd - KINDA_SMALL_NUMBER
				);
				const FVector HighEnd[4] =
				{
					FVector(50.0f, -50.0f, -50.0f),
					FVector(50.0f, 50.0f, -50.0f),
					FVector(50.0f, 50.0f, HighHeight),
					FVector(50.0f, -50.0f, HighHeight)
				};

				TileMapEdModeToolkitLocal::AddRawMeshFace(
					RawMesh,
					HighEnd,
					FVector::ForwardVector,
					MaterialIndex
				);
			}
		}
		else
		{
			const float DiagonalRise = 100.0f;

			const float CutY = -50.0f + DiagonalRise;

			const FVector Bottom0(-50.0f, -50.0f, -50.0f);
			const FVector Bottom1(50.0f, -50.0f, -50.0f);
			const FVector Bottom2(50.0f, CutY, -50.0f);
			const FVector Top0(-50.0f, -50.0f, 50.0f);
			const FVector Top1(50.0f, -50.0f, 50.0f);
			const FVector Top2(50.0f, CutY, 50.0f);

			TileMapEdModeToolkitLocal::AddRawMeshTriangle(
				RawMesh,
				Bottom0,
				Bottom1,
				Bottom2,
				-FVector::UpVector,
				MaterialIndex
			);

			TileMapEdModeToolkitLocal::AddRawMeshTriangle(
				RawMesh,
				Top0,
				Top1,
				Top2,
				FVector::UpVector,
				MaterialIndex
			);

			const FVector Side0[4] =
			{
				Bottom0,
				Bottom1,
				Top1,
				Top0
			};

			const FVector Side1[4] =
			{
				Bottom1,
				Bottom2,
				Top2,
				Top1
			};

			const FVector Side2[4] =
			{
				Bottom2,
				Bottom0,
				Top0,
				Top2
			};

			TileMapEdModeToolkitLocal::AddRawMeshFace(
				RawMesh,
				Side0,
				-FVector::RightVector,
				MaterialIndex
			);

			TileMapEdModeToolkitLocal::AddRawMeshFace(
				RawMesh,
				Side1,
				FVector::ForwardVector,
				MaterialIndex
			);

			TileMapEdModeToolkitLocal::AddRawMeshFace(
				RawMesh,
				Side2,
				FVector(-DiagonalRise, 100.0f, 0.0f)
				.GetSafeNormal(),
				MaterialIndex
			);
		}

		if (!RawMesh.IsValidOrFixable())
		{
			return INDEX_NONE;
		}

		UPackage* Package = CreatePackage(nullptr, *PackageName);

		if (!Package)
		{
			return INDEX_NONE;
		}

		StaticMesh = NewObject<UStaticMesh>(
			Package,
			FName(*AssetName),
			RF_Public | RF_Standalone
		);

		if (!StaticMesh)
		{
			return INDEX_NONE;
		}

		StaticMesh->InitResources();

		FStaticMeshSourceModel& SourceModel =
			StaticMesh->AddSourceModel();

		SourceModel.BuildSettings.bRecomputeNormals = true;
		SourceModel.BuildSettings.bRecomputeTangents = true;
		SourceModel.BuildSettings.bRemoveDegenerates = true;
		SourceModel.BuildSettings.bUseMikkTSpace = true;
		SourceModel.BuildSettings.bGenerateLightmapUVs = true;
		SourceModel.BuildSettings.SrcLightmapIndex = 0;
		SourceModel.BuildSettings.DstLightmapIndex = 1;
		SourceModel.RawMeshBulkData->SaveRawMesh(RawMesh);

		StaticMesh->StaticMaterials.Add(FStaticMaterial(TileMaterial));
		StaticMesh->LightMapCoordinateIndex = 1;
		StaticMesh->LightMapResolution = 64;
		StaticMesh->CreateBodySetup();

		if (StaticMesh->BodySetup)
		{
			StaticMesh->BodySetup->CollisionTraceFlag =
				CTF_UseComplexAsSimple;
		}

		StaticMesh->Build(false);
		StaticMesh->MarkPackageDirty();
		StaticMesh->PostEditChange();
		Package->MarkPackageDirty();
		FAssetRegistryModule::AssetCreated(StaticMesh);
	}

	FTileMapTileDefinition NewDefinition;
	NewDefinition.TileName = TileName;
	NewDefinition.Mesh = StaticMesh;
	NewDefinition.MaterialOverride = TileMaterial;
	NewDefinition.AutoShape =
		SlantMode == ETileMapSlantMode::Ramp
		? ETileMapAutoShape::Ramp
		: (
			SlantMode == ETileMapSlantMode::Stairs
				? ETileMapAutoShape::Stair
				: ETileMapAutoShape::DiagonalEdge
		);

	const int32 NewPaletteIndex =
		TerrainActor->TilePalette.Add(NewDefinition);

	return NewPaletteIndex + 1;
}

void FTileMapEdModeToolkit::Init(
	const TSharedPtr<IToolkitHost>& InitToolkitHost
)
{
	const auto MakeToolCheckBox =
		[this](
			ETileMapCursorTool Tool,
			const FText& Label
			) -> TSharedRef<SWidget>
		{
			return
				SNew(SCheckBox)
				.IsChecked_Lambda(
					[this, Tool]()
					{
						FTileMapEdMode* TileMapMode =
							static_cast<
							FTileMapEdMode*
							>(
								GetEditorMode()
								);

						if (
							TileMapMode &&
							TileMapMode
							->GetActiveTool() ==
							Tool
							)
						{
							return
								ECheckBoxState::Checked;
						}

						return
							ECheckBoxState::Unchecked;
					}
				)
				.OnCheckStateChanged_Lambda(
					[this, Tool](
						ECheckBoxState NewState
						)
					{
						FTileMapEdMode* TileMapMode =
							static_cast<
							FTileMapEdMode*
							>(
								GetEditorMode()
								);

						if (!TileMapMode)
						{
							return;
						}

						TileMapMode->SetActiveTool(
							NewState ==
							ECheckBoxState::Checked
							? Tool
							: ETileMapCursorTool::
							None
						);
					}
				)
				[
					SNew(STextBlock)
						.Text(Label)
				];
		};

	const auto MakeSlantModeCheckBox =
		[this](
			ETileMapSlantMode SlantMode,
			const FText& Label
			) -> TSharedRef<SWidget>
		{
			return
				SNew(SCheckBox)
				.IsChecked_Lambda(
					[this, SlantMode]()
					{
						FTileMapEdMode* TileMapMode =
							static_cast<FTileMapEdMode*>(
								GetEditorMode()
							);

						return
							TileMapMode &&
							TileMapMode->GetSlantMode() == SlantMode
							? ECheckBoxState::Checked
							: ECheckBoxState::Unchecked;
					}
				)
				.OnCheckStateChanged_Lambda(
					[this, SlantMode](ECheckBoxState NewState)
					{
						if (NewState != ECheckBoxState::Checked)
						{
							return;
						}

						FTileMapEdMode* TileMapMode =
							static_cast<FTileMapEdMode*>(
								GetEditorMode()
							);

						if (TileMapMode)
						{
							TileMapMode->SetSlantMode(SlantMode);
						}
					}
				)
				[
					SNew(STextBlock)
					.Text(Label)
				];
		};

	const auto MakeSlantDirectionCheckBox =
		[this](
			ETileMapSlantDirection Direction,
			const FText& Label
			) -> TSharedRef<SWidget>
		{
			return
				SNew(SCheckBox)
				.IsChecked_Lambda(
					[this, Direction]()
					{
						FTileMapEdMode* TileMapMode =
							static_cast<FTileMapEdMode*>(
								GetEditorMode()
							);

						return
							TileMapMode &&
							TileMapMode->GetSlantDirection() == Direction
							? ECheckBoxState::Checked
							: ECheckBoxState::Unchecked;
					}
				)
				.OnCheckStateChanged_Lambda(
					[this, Direction](ECheckBoxState NewState)
					{
						if (NewState != ECheckBoxState::Checked)
						{
							return;
						}

						FTileMapEdMode* TileMapMode =
							static_cast<FTileMapEdMode*>(
								GetEditorMode()
							);

						if (TileMapMode)
						{
							TileMapMode->SetSlantDirection(Direction);
						}
					}
				)
				[
					SNew(STextBlock)
					.Text(Label)
				];
		};

	InlineWidget =
		SNew(SScrollBox)
		+ SScrollBox::Slot()
		[
		SNew(SVerticalBox)

		// TITLE
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(10.0f)
		[
			SNew(STextBlock)
				.Text(
					LOCTEXT(
						"TerrainEditorTitle",
						"Reditus Terrain Editor"
					)
				)
		]

	// ACTIVE TILE LABEL
	+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(10.0f, 5.0f)
		[
			SNew(STextBlock)
				.AutoWrapText(true)
				.Text_Lambda([this]()
					{
						FTileMapEdMode* TileMapMode =
							static_cast<FTileMapEdMode*>(
								GetEditorMode()
							);

						const int32 TileType =
							TileMapMode
							? TileMapMode->GetActiveTileType()
							: 0;

						ATileMapTerrainActor* TerrainActor =
							nullptr;

						if (GEditor)
						{
							TerrainActor =
								TileMapEdModeToolkitLocal::
								FindTerrainActor(
									GEditor
									->GetEditorWorldContext()
									.World()
								);
						}

						const FText TileName =
							TerrainActor
							? TerrainActor->GetTileDisplayName(
								TileType
							)
							: LOCTEXT(
								"DefaultTileName",
								"Default Block"
							);

						return FText::FromString(
							FString::Printf(
								TEXT("Active Tile %d: %s"),
								TileType,
								*TileName.ToString()
							)
						);
					})
		]

	// ACTIVE TILE NUMBER
	+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(10.0f, 0.0f, 10.0f, 5.0f)
		[
			SNew(SSpinBox<int32>)
				.MinValue(0)
				.MaxValue(255)
				.MinSliderValue(0)
				.MaxSliderValue(32)
				.Value_Lambda([this]()
					{
						FTileMapEdMode* TileMapMode =
							static_cast<FTileMapEdMode*>(
								GetEditorMode()
							);

						return TileMapMode
							? TileMapMode->GetActiveTileType()
							: 0;
					})
				.OnValueChanged_Lambda(
					[this](int32 NewTileType)
					{
						FTileMapEdMode* TileMapMode =
							static_cast<FTileMapEdMode*>(
								GetEditorMode()
							);

						if (!TileMapMode)
						{
							return;
						}

						int32 SafeTileType =
							FMath::Max(NewTileType, 0);

						if (GEditor)
						{
							ATileMapTerrainActor* TerrainActor =
								TileMapEdModeToolkitLocal::
								FindTerrainActor(
									GEditor
									->GetEditorWorldContext()
									.World()
								);

							if (TerrainActor)
							{
								SafeTileType = FMath::Clamp(
									SafeTileType,
									0,
									TerrainActor
									->GetTileTypeCount() - 1
								);
							}
						}

						TileMapMode->SetActiveTileType(
							SafeTileType
						);
					}
				)
		]

	// CREATE TEST GRID
	+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(10.0f)
		[
			SNew(SButton)
				.Text(
					LOCTEXT(
						"CreateGridButton",
						"Create Test Grid"
					)
				)
				.OnClicked_Lambda([]()
					{
						if (!GEditor)
						{
							FMessageDialog::Open(
								EAppMsgType::Ok,
								LOCTEXT(
									"NoEditorMessage",
									"Could not access the Unreal Editor."
								)
							);

							return FReply::Handled();
						}

						UWorld* EditorWorld =
							GEditor
							->GetEditorWorldContext()
							.World();

						if (!EditorWorld)
						{
							FMessageDialog::Open(
								EAppMsgType::Ok,
								LOCTEXT(
									"NoWorldMessage",
									"Could not find the current editor world."
								)
							);

							return FReply::Handled();
						}

						const FScopedTransaction
							Transaction(
								LOCTEXT(
									"CreateTerrainTransaction",
									"Create Tile Map Terrain"
								)
							);

						ATileMapTerrainActor*
							TerrainActor =
							nullptr;

						// Reuse the current terrain actor.
						for (
							TActorIterator<
							ATileMapTerrainActor
							> It(EditorWorld);
							It;
							++It
							)
						{
							TerrainActor = *It;
							break;
						}

						if (!TerrainActor)
						{
							EditorWorld->Modify();

							ULevel* CurrentLevel =
								EditorWorld
								->GetCurrentLevel();

							if (CurrentLevel)
							{
								CurrentLevel->Modify();
							}

							FActorSpawnParameters
								SpawnParameters;

							SpawnParameters
								.SpawnCollisionHandlingOverride =
								ESpawnActorCollisionHandlingMethod::
								AlwaysSpawn;

							SpawnParameters.ObjectFlags |=
								RF_Transactional;

							SpawnParameters.OverrideLevel =
								CurrentLevel;

							TerrainActor =
								EditorWorld->SpawnActor<
								ATileMapTerrainActor
								>(
									FVector::ZeroVector,
									FRotator::ZeroRotator,
									SpawnParameters
								);

							if (!TerrainActor)
							{
								FMessageDialog::Open(
									EAppMsgType::Ok,
									LOCTEXT(
										"TerrainSpawnFailure",
										"Could not create the tile map terrain actor."
									)
								);

								return
									FReply::Handled();
							}

							TerrainActor->SetActorLabel(
								TEXT(
									"TileMap_Terrain"
								)
							);
						}

						TerrainActor->Modify();

						TerrainActor->CreateTestGrid(
							10,
							10
						);

						GEditor->SelectNone(
							false,
							true,
							false
						);

						GEditor->SelectActor(
							TerrainActor,
							true,
							true,
							true
						);

						return FReply::Handled();
					})
		]

	// SEPARATOR
	+ SVerticalBox::Slot()
	.AutoHeight()
	.Padding(10.0f, 5.0f)
	[
		SNew(SSeparator)
	]

	// SNAPPED TERRAIN MERGE TITLE
	+ SVerticalBox::Slot()
	.AutoHeight()
	.Padding(10.0f, 5.0f)
	[
		SNew(STextBlock)
			.Text(
				LOCTEXT(
					"SnappedTerrainMergeTitle",
					"Snapped Terrain Merge"
				)
			)
	]

	// MERGE TARGET STATUS
	+ SVerticalBox::Slot()
	.AutoHeight()
	.Padding(10.0f, 0.0f, 10.0f, 5.0f)
	[
		SNew(STextBlock)
			.AutoWrapText(true)
			.Text_Lambda([this]()
				{
					ATileMapTerrainActor* Target = MergeTarget.Get();

					return Target
						? FText::FromString(
							FString::Printf(
								TEXT("Target: %s"),
								*Target->GetActorLabel()
							)
						)
						: LOCTEXT(
							"NoMergeTargetStatus",
							"Target: not set"
						);
				})
	]

	// SET MERGE TARGET
	+ SVerticalBox::Slot()
	.AutoHeight()
	.Padding(10.0f, 3.0f)
	[
		SNew(SButton)
			.Text(
				LOCTEXT(
					"SetMergeTargetButton",
					"Set Selected as Merge Target"
				)
			)
			.OnClicked_Lambda([this]()
				{
					if (!GEditor)
					{
						return FReply::Handled();
					}

					UWorld* EditorWorld =
						GEditor->GetEditorWorldContext().World();
					TArray<ATileMapTerrainActor*> SelectedTerrain;
					TileMapEdModeToolkitLocal::GetSelectedTerrainActors(
						EditorWorld,
						SelectedTerrain
					);

					if (SelectedTerrain.Num() != 1)
					{
						FMessageDialog::Open(
							EAppMsgType::Ok,
							LOCTEXT(
								"SelectOneMergeTarget",
								"Select exactly one Tile Map terrain actor, then set it as the merge target."
							)
						);
						return FReply::Handled();
					}

					MergeTarget = SelectedTerrain[0];
					return FReply::Handled();
				})
	]

	// SNAP AND MERGE SELECTED SOURCES
	+ SVerticalBox::Slot()
	.AutoHeight()
	.Padding(10.0f, 3.0f)
	[
		SNew(SButton)
			.Text(
				LOCTEXT(
					"SnapMergeSelectedTerrainButton",
					"Snap + Merge Selected"
				)
			)
			.OnClicked_Lambda([this]()
				{
					if (!GEditor)
					{
						return FReply::Handled();
					}

					UWorld* EditorWorld =
						GEditor->GetEditorWorldContext().World();
					ATileMapTerrainActor* Target = MergeTarget.Get();

					if (
						!Target ||
						Target->GetWorld() != EditorWorld
						)
					{
						MergeTarget.Reset();
						FMessageDialog::Open(
							EAppMsgType::Ok,
							LOCTEXT(
								"MissingMergeTarget",
								"Set one selected terrain actor as the merge target first."
							)
						);
						return FReply::Handled();
					}

					TArray<ATileMapTerrainActor*> Sources;
					TileMapEdModeToolkitLocal::GetSelectedTerrainActors(
						EditorWorld,
						Sources
					);
					Sources.Remove(Target);

					if (Sources.Num() == 0)
					{
						FMessageDialog::Open(
							EAppMsgType::Ok,
							LOCTEXT(
								"MissingMergeSources",
								"Select one or more copied terrain actors in the World Outliner. The stored target may remain selected; it is never used as a source."
							)
						);
						return FReply::Handled();
					}

					TArray<FIntVector> GridPositions;
					TArray<int32> TileTypes;
					TArray<uint8> Rotations;
					TArray<FIntVector> PaintedPathPositions;
					int32 TargetOverlapCount = 0;
					int32 SourceOverlapCount = 0;
					float MaximumSnapDistance = 0.0f;
					FString FailureReason;

					if (!TileMapEdModeToolkitLocal::
						BuildSnappedTerrainMergeBatch(
							Target,
							Sources,
							GridPositions,
							TileTypes,
							Rotations,
							PaintedPathPositions,
							TargetOverlapCount,
							SourceOverlapCount,
							MaximumSnapDistance,
							FailureReason
							))
					{
						FMessageDialog::Open(
							EAppMsgType::Ok,
							FText::FromString(FailureReason)
						);
						return FReply::Handled();
					}

					const FScopedTransaction Transaction(
						LOCTEXT(
							"SnapMergeCopiedTerrainTransaction",
							"Snap and Merge Copied Tile Map Terrain"
						)
					);
					Target->Modify();

					if (
						GridPositions.Num() > 0 &&
						!Target->AddBlocks(
							GridPositions,
							TileTypes,
							Rotations
						)
						)
					{
						FMessageDialog::Open(
							EAppMsgType::Ok,
							LOCTEXT(
								"MergeChangedDuringCommit",
								"The target changed after merge validation, so no copied blocks were added. Try again."
							)
						);
						return FReply::Handled();
					}

					Target->SetPathsPainted(
						PaintedPathPositions,
						true
					);

					int32 RemovedSourceCount = 0;

					for (ATileMapTerrainActor* Source : Sources)
					{
						if (!IsValid(Source) || Source == Target)
						{
							continue;
						}

						Source->Modify();

						if (EditorWorld->EditorDestroyActor(Source, true))
						{
							++RemovedSourceCount;
						}
					}

					Target->MarkPackageDirty();
					FMessageDialog::Open(
						EAppMsgType::Ok,
						FText::FromString(
							FString::Printf(
								TEXT("Snapped and merged %d blocks into %s, preserved %d painted path cells, and removed %d source actor(s). The target kept %d occupied cells; %d duplicate source cells were skipped. Maximum snap correction: %.1f units. One Undo restores both target and sources."),
								GridPositions.Num(),
								*Target->GetActorLabel(),
								PaintedPathPositions.Num(),
								RemovedSourceCount,
								TargetOverlapCount,
								SourceOverlapCount,
								MaximumSnapDistance
							)
						)
					);

					return FReply::Handled();
				})
	]

	// MERGE HELP
	+ SVerticalBox::Slot()
	.AutoHeight()
	.Padding(10.0f, 3.0f, 10.0f, 5.0f)
	[
		SNew(STextBlock)
			.AutoWrapText(true)
			.Text(
				LOCTEXT(
					"SnappedTerrainMergeHelp",
					"Move copied terrain approximately into place, even far outside the target's occupied area or at another elevation. The merge snaps every source cell to the target's unlimited 3D grid. Existing target cells win. Successful merges remove the source actors in the same Undo transaction."
				)
			)
	]

	// SEPARATOR
	+ SVerticalBox::Slot()
	.AutoHeight()
	.Padding(10.0f, 5.0f)
	[
		SNew(SSeparator)
	]

		// CURSOR TOOLS TITLE
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(10.0f)
		[
			SNew(STextBlock)
				.Text(
					LOCTEXT(
						"CursorToolsTitle",
						"Cursor Tools"
					)
				)
		]

	// ADD / STACK
	+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(10.0f, 5.0f)
		[
			MakeToolCheckBox(
				ETileMapCursorTool::AddBlock,
				LOCTEXT(
					"AddBlockTool",
					"Add / Stack Block"
				)
			)
		]

	// REPLACE BLOCK TYPE
	+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(10.0f, 5.0f)
		[
			MakeToolCheckBox(
				ETileMapCursorTool::PaintTile,
				LOCTEXT(
					"PaintTileTool",
					"Replace Block Type"
				)
			)
		]

	// PAINT PATH MASK
	+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(10.0f, 5.0f)
		[
			MakeToolCheckBox(
				ETileMapCursorTool::PaintPath,
				LOCTEXT(
					"PaintPathTool",
					"Paint Path"
				)
			)
		]

	// ERASE PATH MASK
	+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(10.0f, 5.0f)
		[
			MakeToolCheckBox(
				ETileMapCursorTool::ErasePath,
				LOCTEXT(
					"ErasePathTool",
					"Erase Path"
				)
			)
		]

	// ROTATE TILE
	+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(10.0f, 5.0f)
		[
			MakeToolCheckBox(
				ETileMapCursorTool::RotateTile,
				LOCTEXT(
					"RotateTileTool",
					"Rotate Tile 90 Degrees"
				)
			)
		]

	// SLANT SETTINGS TITLE
	+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(10.0f, 8.0f, 10.0f, 3.0f)
		[
			SNew(STextBlock)
			.Text(
				LOCTEXT(
					"SlantSettingsTitle",
					"Slant Shape"
				)
			)
		]

	// RAMP MODE
	+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(10.0f, 3.0f)
		[
			MakeSlantModeCheckBox(
				ETileMapSlantMode::Ramp,
				LOCTEXT(
					"RampSlantMode",
					"Ramp (vertical rise)"
				)
			)
		]

	// DIAGONAL EDGE MODE
	+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(10.0f, 3.0f)
		[
			MakeSlantModeCheckBox(
				ETileMapSlantMode::DiagonalEdge,
				LOCTEXT(
					"DiagonalEdgeSlantMode",
					"Diagonal Edge (horizontal cut)"
				)
			)
		]

	// FIXED STAIR MODE
	+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(10.0f, 3.0f)
		[
			MakeSlantModeCheckBox(
				ETileMapSlantMode::Stairs,
				LOCTEXT(
					"FixedStairMode",
					"Stairs (fixed 33.69 degrees)"
				)
			)
		]

	// MODE-SPECIFIC ANGLE LABEL
	+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(10.0f, 6.0f, 10.0f, 2.0f)
		[
			SNew(STextBlock)
			.Text_Lambda([this]()
				{
					FTileMapEdMode* TileMapMode =
						static_cast<FTileMapEdMode*>(
							GetEditorMode()
						);

					if (!TileMapMode)
					{
						return FText::GetEmpty();
					}

					if (
						TileMapMode->GetSlantMode() ==
						ETileMapSlantMode::DiagonalEdge
						)
					{
						return LOCTEXT(
							"DiagonalAngleFixedLabel",
							"Horizontal cut angle (fixed at 45 degrees)"
						);
					}

					return
						TileMapMode->GetSlantMode() ==
						ETileMapSlantMode::Stairs
						? LOCTEXT(
							"StairAngleFixedLabel",
							"Stair angle (fixed at 33.69 degrees)"
						)
						: LOCTEXT(
							"RampAngleLabel",
							"Ramp angle (5-45 degrees)"
						);
				})
		]

	// SLANT DIRECTION LABEL
	+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(10.0f, 6.0f, 10.0f, 2.0f)
		[
			SNew(STextBlock)
			.Text(
				LOCTEXT(
					"SlantDirectionLabel",
					"Rise / cut direction"
				)
			)
		]

	// SLANT DIRECTION BUTTONS
	+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(10.0f, 0.0f, 10.0f, 5.0f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(0.0f, 0.0f, 8.0f, 0.0f)
			[
				MakeSlantDirectionCheckBox(
					ETileMapSlantDirection::PositiveX,
					LOCTEXT("SlantPositiveX", "+X")
				)
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(0.0f, 0.0f, 8.0f, 0.0f)
			[
				MakeSlantDirectionCheckBox(
					ETileMapSlantDirection::PositiveY,
					LOCTEXT("SlantPositiveY", "+Y")
				)
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(0.0f, 0.0f, 8.0f, 0.0f)
			[
				MakeSlantDirectionCheckBox(
					ETileMapSlantDirection::NegativeX,
					LOCTEXT("SlantNegativeX", "-X")
				)
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			[
				MakeSlantDirectionCheckBox(
					ETileMapSlantDirection::NegativeY,
					LOCTEXT("SlantNegativeY", "-Y")
				)
			]
		]

	// SLANT ANGLE
	+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(10.0f, 0.0f, 10.0f, 5.0f)
		[
			SNew(SSpinBox<float>)
			.IsEnabled_Lambda([this]()
				{
					FTileMapEdMode* TileMapMode =
						static_cast<FTileMapEdMode*>(
							GetEditorMode()
						);

					return
						TileMapMode &&
						TileMapMode->GetSlantMode() ==
							ETileMapSlantMode::Ramp;
				})
			.MinValue(5.0f)
			.MaxValue(45.0f)
			.MinSliderValue(5.0f)
			.MaxSliderValue(45.0f)
			.Delta(1.0f)
			.Value_Lambda([this]()
				{
					FTileMapEdMode* TileMapMode =
						static_cast<FTileMapEdMode*>(
							GetEditorMode()
						);

					if (!TileMapMode)
					{
						return 26.565f;
					}

					if (
						TileMapMode->GetSlantMode() ==
						ETileMapSlantMode::DiagonalEdge
						)
					{
						return 45.0f;
					}

					return
						TileMapMode->GetSlantMode() ==
						ETileMapSlantMode::Stairs
						? TileMapMode->GetFixedStairAngle()
						: TileMapMode->GetSlantAngle();
				})
			.OnValueChanged_Lambda(
				[this](float NewAngle)
				{
					FTileMapEdMode* TileMapMode =
						static_cast<FTileMapEdMode*>(
							GetEditorMode()
						);

					if (
						TileMapMode &&
						TileMapMode->GetSlantMode() ==
							ETileMapSlantMode::Ramp
						)
					{
						TileMapMode->SetSlantAngle(NewAngle);
					}
				}
			)
		]

	// EFFECTIVE GRID-COMPATIBLE RAMP
	+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(10.0f, 0.0f, 10.0f, 5.0f)
		[
			SNew(STextBlock)
			.AutoWrapText(true)
			.Text_Lambda([this]()
				{
					FTileMapEdMode* TileMapMode =
						static_cast<FTileMapEdMode*>(
							GetEditorMode()
						);

					if (!TileMapMode)
					{
						return FText::GetEmpty();
					}

					if (
						TileMapMode->GetSlantMode() ==
						ETileMapSlantMode::DiagonalEdge
						)
					{
						return LOCTEXT(
							"FixedHorizontalCutStatus",
							"Horizontal cut: fixed 45.0 degrees"
						);
					}

					if (
						TileMapMode->GetSlantMode() ==
						ETileMapSlantMode::Stairs
						)
					{
						return FText::FromString(
							FString::Printf(
								TEXT("Stairs: 2 tiles, 12 steps, actual %.2f degrees"),
								TileMapMode->GetFixedStairAngle()
							)
						);
					}

					return FText::FromString(
						FString::Printf(
							TEXT("Continuous ramp: %d tile(s), actual %.1f degrees"),
							TileMapMode->GetRampSegmentCount(),
							TileMapMode->GetEffectiveRampAngle()
						)
					);
				})
		]

	// APPLY SLANT
	+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(10.0f, 5.0f)
		[
			MakeToolCheckBox(
				ETileMapCursorTool::SetSlant,
				LOCTEXT(
					"SetSlantTool",
					"Apply Slant"
				)
			)
		]

	// DELETE
	+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(10.0f, 5.0f)
		[
			MakeToolCheckBox(
				ETileMapCursorTool::DeleteBlock,
				LOCTEXT(
					"DeleteBlockTool",
					"Delete Block"
				)
			)
		]

	// SEPARATOR
	+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(10.0f, 5.0f)
		[
			SNew(SSeparator)
		]

	// BAKE STATIC MESH
	+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(10.0f, 5.0f)
		[
			SNew(SButton)
				.Text(
					LOCTEXT(
						"BakeStaticMeshButton",
						"Bake Optimized Static Mesh"
					)
				)
				.OnClicked_Lambda([]()
					{
						if (!GEditor)
						{
							return FReply::Handled();
						}

						UWorld* EditorWorld =
							GEditor
							->GetEditorWorldContext()
							.World();

						ATileMapTerrainActor* TerrainActor =
							TileMapEdModeToolkitLocal::
							FindTerrainActor(EditorWorld);

						if (!TerrainActor)
						{
							FMessageDialog::Open(
								EAppMsgType::Ok,
								LOCTEXT(
									"NoTerrainToBake",
									"Create or select a tile map terrain actor first."
								)
							);

							return FReply::Handled();
						}

						int32 BakedTerrainDetailCount = 0;
						UStaticMesh* BakedMesh =
							TileMapEdModeToolkitLocal::
							BakeTerrainToStaticMesh(
								TerrainActor,
								&BakedTerrainDetailCount
							);

						FText BakeMessage;

						if (!BakedMesh)
						{
							BakeMessage = LOCTEXT(
								"BakeFailed",
								"The terrain could not be baked. Make sure it contains blocks and every palette mesh has valid LOD 0 source geometry."
							);
						}
						else if (
							TerrainActor->bGenerateTerrainDetailsOnBake &&
							BakedTerrainDetailCount > 0
							)
						{
							BakeMessage = FText::Format(
								LOCTEXT(
									"BakeSucceededWithTerrainDetails",
									"The optimized terrain mesh was created in /Game/TileMapBakes. A separate HISM detail actor with {0} structural terrain instances was also placed; those instances were not merged into the terrain asset. Use Save All when ready."
								),
								FText::AsNumber(BakedTerrainDetailCount)
							);
						}
						else if (TerrainActor->bGenerateTerrainDetailsOnBake)
						{
							BakeMessage = LOCTEXT(
								"BakeSucceededWithoutEligibleTerrainDetails",
								"The optimized terrain mesh was created in /Game/TileMapBakes. Terrain Detail Pass was enabled, but no structural detail instances were selected. Check that the palette has a valid mesh for the needed Ground, Cliff Top, or Cliff Base region and that Density and Maximum Instances are above zero."
							);
						}
						else
						{
							BakeMessage = LOCTEXT(
								"BakeSucceeded",
								"The optimized mesh was created in /Game/TileMapBakes and a movable baked actor was placed in the level. Terrain Detail Pass remained disabled. Use Save All, then export the mesh asset from the Content Browser when needed."
							);
						}

						FMessageDialog::Open(
							EAppMsgType::Ok,
							BakeMessage
						);

						return FReply::Handled();
					})
		]

	// HELP
	+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(10.0f)
		[
			SNew(STextBlock)
				.AutoWrapText(true)
				.Text(
					LOCTEXT(
						"CursorToolHelp",
						"Stairs use one fixed 33.69-degree standard: two occupied cells, twelve physical steps, and 25-unit low/high landings. Paint Path and Erase Path work on upward ground surfaces in both continuous and modular/HISM modes, including ramp and stair surfaces. The path is OneMinus(VertexColor.G); connect that mask to the alpha of the ground/path texture blend in the terrain material. Terrain Detail Pass is configured in the selected terrain actor's Details panel, is disabled by default, and creates separate bake-time HISM rock/mound pieces without adding triangles to the optimized terrain asset. Vegetation remains a foliage-tool job. Replace Block Type changes palette metadata, while Delete removes occupied cells. Every mouse stroke is one Undo/Redo history step."
					)
				)
		]
		];

	FModeToolkit::Init(InitToolkitHost);
}

FName FTileMapEdModeToolkit::GetToolkitFName() const
{
	return FName(TEXT("TileMapEdMode"));
}

FText FTileMapEdModeToolkit::GetBaseToolkitName() const
{
	return LOCTEXT(
		"ToolkitName",
		"Tile Map"
	);
}

FEdMode* FTileMapEdModeToolkit::GetEditorMode() const
{
	return GLevelEditorModeTools().GetActiveMode(
		FTileMapEdMode::EM_TileMapEdModeId
	);
}

TSharedPtr<SWidget>
FTileMapEdModeToolkit::GetInlineContent() const
{
	return InlineWidget;
}

#undef LOCTEXT_NAMESPACE
