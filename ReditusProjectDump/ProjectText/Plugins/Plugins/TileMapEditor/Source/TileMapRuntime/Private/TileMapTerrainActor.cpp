// Copyright Epic Games, Inc. All Rights Reserved.

#include "TileMapTerrainActor.h"

#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "Components/SceneComponent.h"
#include "DrawDebugHelpers.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"
#include "ProceduralMeshComponent.h"
#include "UObject/ConstructorHelpers.h"
#include "UObject/UnrealType.h"

namespace
{
	struct FTileMapContinuousSection
	{
		TArray<FVector> Vertices;
		TArray<int32> Triangles;
		TArray<FVector> Normals;
		TArray<FVector2D> UV0;
		TArray<FColor> VertexColors;
		TArray<FProcMeshTangent> Tangents;
	};

	bool BuildPathOverlaySection(
		const FTileMapContinuousSection& Source,
		FTileMapContinuousSection& OutOverlay
	)
	{
		OutOverlay = FTileMapContinuousSection();
		TMap<int32, int32> RemappedVertices;

		auto RemapVertex =
			[&](int32 SourceVertexIndex) -> int32
			{
				if (const int32* ExistingIndex =
					RemappedVertices.Find(SourceVertexIndex))
				{
					return *ExistingIndex;
				}

				if (
					!Source.Vertices.IsValidIndex(SourceVertexIndex) ||
					!Source.Normals.IsValidIndex(SourceVertexIndex) ||
					!Source.UV0.IsValidIndex(SourceVertexIndex) ||
					!Source.VertexColors.IsValidIndex(SourceVertexIndex) ||
					!Source.Tangents.IsValidIndex(SourceVertexIndex)
					)
				{
					return INDEX_NONE;
				}

				const int32 OverlayVertexIndex =
					OutOverlay.Vertices.Add(
						Source.Vertices[SourceVertexIndex]
					);
				OutOverlay.Normals.Add(Source.Normals[SourceVertexIndex]);
				OutOverlay.UV0.Add(Source.UV0[SourceVertexIndex]);
				OutOverlay.VertexColors.Add(
					Source.VertexColors[SourceVertexIndex]
				);
				OutOverlay.Tangents.Add(Source.Tangents[SourceVertexIndex]);
				RemappedVertices.Add(
					SourceVertexIndex,
					OverlayVertexIndex
				);
				return OverlayVertexIndex;
			};

		for (
			int32 TriangleIndex = 0;
			TriangleIndex + 2 < Source.Triangles.Num();
			TriangleIndex += 3
			)
		{
			const int32 SourceIndices[3] =
			{
				Source.Triangles[TriangleIndex],
				Source.Triangles[TriangleIndex + 1],
				Source.Triangles[TriangleIndex + 2]
			};
			bool bTouchesPathMask = false;

			for (int32 CornerIndex = 0; CornerIndex < 3; ++CornerIndex)
			{
				if (
					Source.VertexColors.IsValidIndex(
						SourceIndices[CornerIndex]
					) &&
					Source.VertexColors[SourceIndices[CornerIndex]].G < 255
					)
				{
					bTouchesPathMask = true;
					break;
				}
			}

			if (!bTouchesPathMask)
			{
				continue;
			}

			int32 OverlayIndices[3];

			for (int32 CornerIndex = 0; CornerIndex < 3; ++CornerIndex)
			{
				OverlayIndices[CornerIndex] =
					RemapVertex(SourceIndices[CornerIndex]);

				if (OverlayIndices[CornerIndex] == INDEX_NONE)
				{
					return false;
				}
			}

			OutOverlay.Triangles.Append(OverlayIndices, 3);
		}

		return OutOverlay.Triangles.Num() > 0;
	}

	// Continuous terrain owns its material classification explicitly. Blue is
	// zero for cliff/support geometry. Alpha zero marks ordinary authored data,
	// while alpha 0.5 identifies the physical upper cliff-edge strip. Ordinary
	// modular meshes keep their default white vertex color and therefore retain
	// the material's normal-based fallback.
	const FColor ContinuousCliffSurfaceColor(255, 255, 0, 0);
	const FColor ContinuousCliffEdgeSurfaceColor(255, 255, 0, 128);

	FVector2D MakeContinuousUV(
		const FVector& Position,
		const FVector& Normal,
		float GridSize
	)
	{
		const float SafeGridSize = FMath::Max(GridSize, 1.0f);
		const FVector AbsoluteNormal = Normal.GetAbs();

		if (AbsoluteNormal.Z >= AbsoluteNormal.X &&
			AbsoluteNormal.Z >= AbsoluteNormal.Y)
		{
			return FVector2D(
				Position.X / SafeGridSize,
				Position.Y / SafeGridSize
			);
		}

		if (AbsoluteNormal.X >= AbsoluteNormal.Y)
		{
			return FVector2D(
				Position.Y / SafeGridSize,
				Position.Z / SafeGridSize
			);
		}

		return FVector2D(
			Position.X / SafeGridSize,
			Position.Z / SafeGridSize
		);
	}

	FVector MakeContinuousTangent(
		const FVector& Normal
	)
	{
		return FMath::Abs(Normal.Z) >= 0.5f
			? FVector::ForwardVector
			: (
				FMath::Abs(Normal.X) >= 0.5f
				? FVector::RightVector
				: FVector::ForwardVector
			);
	}

	FVector MakeCliffTopTangent(
		const FVector& Direction,
		const FVector& Normal
	)
	{
		FVector SafeDirection = Direction.GetSafeNormal();

		if (SafeDirection.IsNearlyZero())
		{
			const FVector2D OutwardNormal(Normal.X, Normal.Y);

			if (OutwardNormal.SizeSquared() > SMALL_NUMBER)
			{
				const FVector2D Tangent2D(
					-OutwardNormal.Y,
					OutwardNormal.X
				);
				SafeDirection = FVector(
					Tangent2D.X,
					Tangent2D.Y,
					0.0f
				).GetSafeNormal();
			}
		}

		return SafeDirection.IsNearlyZero()
			? MakeContinuousTangent(Normal)
			: SafeDirection;
	}

	FVector2D MakeCliffTopUVFromCoordinate(
		float AlongBoundary,
		float AcrossLip
	)
	{
		// U is actor-local boundary distance in grid units. V is the authored
		// cross-lip coordinate: 0 = flat ground, 0.5 = outer crest, and
		// 1 = lower edge of the narrow upper-wall strip.
		return FVector2D(
			AlongBoundary,
			FMath::Clamp(AcrossLip, 0.0f, 1.0f)
		);
	}

	FColor MakeCliffFootVertexColor(float BlendMask)
	{
		const float SafeMask = FMath::Clamp(
			BlendMask,
			0.0f,
			1.0f
		);
		const uint8 Red = static_cast<uint8>(
			FMath::RoundToInt((1.0f - SafeMask) * 255.0f)
		);

		// R remains the inverse cliff-foot mask. B=0 identifies cliff-owned
		// geometry, and A=0 marks the continuous ownership mask as valid.
		return FColor(Red, 255, 0, 0);
	}

	FColor MakeGroundSurfaceVertexColor(
		float CliffFootMask,
		float PathMask,
		float GroundOwnershipMask = 1.0f
	)
	{
		const float SafeCliffFootMask = FMath::Clamp(
			CliffFootMask,
			0.0f,
			1.0f
		);
		const float SafePathMask = FMath::Clamp(
			PathMask,
			0.0f,
			1.0f
		);
		const uint8 Red = static_cast<uint8>(
			FMath::RoundToInt((1.0f - SafeCliffFootMask) * 255.0f)
		);
		const uint8 Green = static_cast<uint8>(
			FMath::RoundToInt((1.0f - SafePathMask) * 255.0f)
		);
		const uint8 Blue = static_cast<uint8>(
			FMath::RoundToInt(
				FMath::Clamp(GroundOwnershipMask, 0.0f, 1.0f) *
				255.0f
			)
		);

		// R remains the inverse cliff-foot mask. G remains the independent
		// inverse path mask. B is explicit ground ownership, and A=0 identifies
		// the authored continuous-terrain material data.
		return FColor(Red, Green, Blue, 0);
	}

	FColor MakeCliffEdgeSurfaceVertexColor(
		float CliffEdgeMask,
		float PathMask = 0.0f
	)
	{
		const float SafeCliffEdgeMask = FMath::Clamp(
			CliffEdgeMask,
			0.0f,
			1.0f
		);
		const float SafePathMask = FMath::Clamp(
			PathMask,
			0.0f,
			1.0f
		);
		const uint8 Green = static_cast<uint8>(
			FMath::RoundToInt((1.0f - SafePathMask) * 255.0f)
		);
		const uint8 Blue = static_cast<uint8>(
			FMath::RoundToInt((1.0f - SafeCliffEdgeMask) * 255.0f)
		);
		const uint8 Alpha = static_cast<uint8>(
			FMath::RoundToInt(SafeCliffEdgeMask * 128.0f)
		);

		// The edge mask simultaneously fades ground ownership out through B and
		// fades the dedicated cliff-edge material in through the lower half of A.
		// A=1 remains reserved for unowned modular fallback geometry.
		return FColor(255, Green, Blue, Alpha);
	}

	int32 AddContinuousVertex(
		FTileMapContinuousSection& Section,
		const FVector& Position,
		const FVector& Normal,
		float GridSize,
		const FColor& VertexColor = ContinuousCliffSurfaceColor
	)
	{
		const int32 VertexIndex = Section.Vertices.Add(Position);
		Section.Normals.Add(Normal);
		Section.UV0.Add(
			MakeContinuousUV(Position, Normal, GridSize)
		);
		Section.VertexColors.Add(VertexColor);
		Section.Tangents.Add(
			FProcMeshTangent(
				MakeContinuousTangent(Normal),
				false
			)
		);
		return VertexIndex;
	}

	int32 AddContinuousVertexWithUV(
		FTileMapContinuousSection& Section,
		const FVector& Position,
		const FVector& Normal,
		float GridSize,
		const FVector2D& UV,
		const FVector& Tangent,
		const FColor& VertexColor = ContinuousCliffSurfaceColor
	)
	{
		const int32 VertexIndex = Section.Vertices.Add(Position);
		Section.Normals.Add(Normal);
		Section.UV0.Add(UV);
		Section.VertexColors.Add(VertexColor);
		Section.Tangents.Add(
			FProcMeshTangent(
				MakeCliffTopTangent(Tangent, Normal),
				false
			)
		);
		return VertexIndex;
	}

	void AddContinuousPolygon(
		FTileMapContinuousSection& Section,
		const TArray<FVector>& Boundary,
		const FVector& Normal,
		float GridSize,
		bool bReverseWinding,
		const FColor& VertexColor = ContinuousCliffSurfaceColor
	)
	{
		if (Boundary.Num() < 3)
		{
			return;
		}

		FVector Center = FVector::ZeroVector;

		for (const FVector& Point : Boundary)
		{
			Center += Point;
		}

		Center /= static_cast<float>(Boundary.Num());

		const int32 CenterIndex =
			AddContinuousVertex(
				Section,
				Center,
				Normal,
				GridSize,
				VertexColor
			);

		TArray<int32> BoundaryIndices;
		BoundaryIndices.Reserve(Boundary.Num());

		for (const FVector& Point : Boundary)
		{
			BoundaryIndices.Add(
				AddContinuousVertex(
					Section,
					Point,
					Normal,
					GridSize,
					VertexColor
				)
			);
		}

		for (int32 Index = 0; Index < BoundaryIndices.Num(); ++Index)
		{
			const int32 NextIndex =
				(Index + 1) % BoundaryIndices.Num();

			Section.Triangles.Add(CenterIndex);

			if (bReverseWinding)
			{
				Section.Triangles.Add(BoundaryIndices[Index]);
				Section.Triangles.Add(BoundaryIndices[NextIndex]);
			}
			else
			{
				Section.Triangles.Add(BoundaryIndices[NextIndex]);
				Section.Triangles.Add(BoundaryIndices[Index]);
			}
		}
	}

	// Triangulate a simple horizontal polygon without introducing an averaged
	// center vertex. Rounded diagonal-support complements can be concave at the
	// two tangent junctions, where a center fan crosses the polygon boundary and
	// leaves visible wedges. Ear clipping preserves the authored boundary and
	// handles all four horizontal-cut rotations.
	void AddContinuousHorizontalPolygonEarClipped(
		FTileMapContinuousSection& Section,
		const TArray<FVector>& Boundary,
		const FVector& Normal,
		float GridSize,
		bool bReverseWinding,
		const FColor& VertexColor = ContinuousCliffSurfaceColor
	)
	{
		if (Boundary.Num() < 3)
		{
			return;
		}

		TArray<int32> BoundaryIndices;
		BoundaryIndices.Reserve(Boundary.Num());

		for (const FVector& Point : Boundary)
		{
			BoundaryIndices.Add(
				AddContinuousVertex(
					Section,
					Point,
					Normal,
					GridSize,
					VertexColor
				)
			);
		}

		float SignedDoubleArea = 0.0f;

		for (int32 Index = 0; Index < Boundary.Num(); ++Index)
		{
			const FVector& A = Boundary[Index];
			const FVector& B = Boundary[(Index + 1) % Boundary.Num()];
			SignedDoubleArea += (A.X * B.Y) - (B.X * A.Y);
		}

		if (FMath::Abs(SignedDoubleArea) <= KINDA_SMALL_NUMBER)
		{
			return;
		}

		const float OrientationSign = SignedDoubleArea > 0.0f ? 1.0f : -1.0f;
		TArray<int32> Remaining;
		Remaining.Reserve(Boundary.Num());

		for (int32 Index = 0; Index < Boundary.Num(); ++Index)
		{
			Remaining.Add(Index);
		}

		auto Cross2D =
			[](const FVector& A, const FVector& B, const FVector& C)
			{
				return
					((B.X - A.X) * (C.Y - A.Y)) -
					((B.Y - A.Y) * (C.X - A.X));
			};

		auto IsPointInsideTriangle =
			[&Cross2D](
				const FVector& Point,
				const FVector& A,
				const FVector& B,
				const FVector& C
			)
			{
				const float AB = Cross2D(A, B, Point);
				const float BC = Cross2D(B, C, Point);
				const float CA = Cross2D(C, A, Point);
				const bool bHasNegative =
					AB < -KINDA_SMALL_NUMBER ||
					BC < -KINDA_SMALL_NUMBER ||
					CA < -KINDA_SMALL_NUMBER;
				const bool bHasPositive =
					AB > KINDA_SMALL_NUMBER ||
					BC > KINDA_SMALL_NUMBER ||
					CA > KINDA_SMALL_NUMBER;

				return !(bHasNegative && bHasPositive);
			};

		int32 SafetyCounter = Boundary.Num() * Boundary.Num();

		while (Remaining.Num() > 3 && SafetyCounter-- > 0)
		{
			bool bRemovedEar = false;

			for (int32 EarIndex = 0; EarIndex < Remaining.Num(); ++EarIndex)
			{
				const int32 PreviousListIndex =
					(EarIndex + Remaining.Num() - 1) % Remaining.Num();
				const int32 NextListIndex =
					(EarIndex + 1) % Remaining.Num();
				const int32 Previous = Remaining[PreviousListIndex];
				const int32 Current = Remaining[EarIndex];
				const int32 Next = Remaining[NextListIndex];
				const float ConvexCross = Cross2D(
					Boundary[Previous],
					Boundary[Current],
					Boundary[Next]
				);

				if (ConvexCross * OrientationSign <= KINDA_SMALL_NUMBER)
				{
					continue;
				}

				bool bContainsOtherPoint = false;

				for (int32 TestListIndex = 0;
					TestListIndex < Remaining.Num();
					++TestListIndex)
				{
					const int32 TestIndex = Remaining[TestListIndex];

					if (
						TestIndex == Previous ||
						TestIndex == Current ||
						TestIndex == Next
						)
					{
						continue;
					}

					if (IsPointInsideTriangle(
						Boundary[TestIndex],
						Boundary[Previous],
						Boundary[Current],
						Boundary[Next]
						))
					{
						bContainsOtherPoint = true;
						break;
					}
				}

				if (bContainsOtherPoint)
				{
					continue;
				}

				if (bReverseWinding)
				{
					Section.Triangles.Add(BoundaryIndices[Previous]);
					Section.Triangles.Add(BoundaryIndices[Current]);
					Section.Triangles.Add(BoundaryIndices[Next]);
				}
				else
				{
					Section.Triangles.Add(BoundaryIndices[Previous]);
					Section.Triangles.Add(BoundaryIndices[Next]);
					Section.Triangles.Add(BoundaryIndices[Current]);
				}

				Remaining.RemoveAt(EarIndex, 1, false);
				bRemovedEar = true;
				break;
			}

			if (!bRemovedEar)
			{
				break;
			}
		}

		if (Remaining.Num() == 3)
		{
			if (bReverseWinding)
			{
				Section.Triangles.Add(BoundaryIndices[Remaining[0]]);
				Section.Triangles.Add(BoundaryIndices[Remaining[1]]);
				Section.Triangles.Add(BoundaryIndices[Remaining[2]]);
			}
			else
			{
				Section.Triangles.Add(BoundaryIndices[Remaining[0]]);
				Section.Triangles.Add(BoundaryIndices[Remaining[2]]);
				Section.Triangles.Add(BoundaryIndices[Remaining[1]]);
			}
		}
	}

	void AddContinuousWallQuad(
		FTileMapContinuousSection& Section,
		const FVector& BottomA,
		const FVector& BottomB,
		const FVector& TopB,
		const FVector& TopA,
		const FVector& NormalA,
		const FVector& NormalB,
		float GridSize,
		const FColor& BottomAColor = ContinuousCliffSurfaceColor,
		const FColor& BottomBColor = ContinuousCliffSurfaceColor,
		const FColor& TopBColor = ContinuousCliffSurfaceColor,
		const FColor& TopAColor = ContinuousCliffSurfaceColor
	)
	{
		const int32 A = AddContinuousVertex(
			Section, BottomA, NormalA, GridSize, BottomAColor);
		const int32 B = AddContinuousVertex(
			Section, BottomB, NormalB, GridSize, BottomBColor);
		const int32 C = AddContinuousVertex(
			Section, TopB, NormalB, GridSize, TopBColor);
		const int32 D = AddContinuousVertex(
			Section, TopA, NormalA, GridSize, TopAColor);
		const FVector DesiredNormal =
			(NormalA + NormalB).GetSafeNormal();

		const FVector WindingNormal =
			FVector::CrossProduct(
				BottomB - BottomA,
				TopB - BottomA
			);

		if (FVector::DotProduct(WindingNormal, DesiredNormal) >= 0.0f)
		{
			Section.Triangles.Add(A);
			Section.Triangles.Add(C);
			Section.Triangles.Add(B);
			Section.Triangles.Add(A);
			Section.Triangles.Add(D);
			Section.Triangles.Add(C);
		}
		else
		{
			Section.Triangles.Add(A);
			Section.Triangles.Add(C);
			Section.Triangles.Add(D);
			Section.Triangles.Add(A);
			Section.Triangles.Add(B);
			Section.Triangles.Add(C);
		}
	}

	void AddContinuousCliffTopQuad(
		FTileMapContinuousSection& Section,
		const FVector& LowerA,
		const FVector& LowerB,
		const FVector& UpperB,
		const FVector& UpperA,
		const FVector& NormalA,
		const FVector& NormalB,
		float GridSize,
		float LowerV,
		float UpperV,
		float AlongA,
		float AlongB,
		const FColor& LowerAColor = ContinuousCliffEdgeSurfaceColor,
		const FColor& LowerBColor = ContinuousCliffEdgeSurfaceColor,
		const FColor& UpperBColor = ContinuousCliffEdgeSurfaceColor,
		const FColor& UpperAColor = ContinuousCliffEdgeSurfaceColor
	)
	{
		const FVector Tangent = MakeCliffTopTangent(
			((LowerB + UpperB) * 0.5f) -
			((LowerA + UpperA) * 0.5f),
			(NormalA + NormalB).GetSafeNormal()
		);
		const int32 A = AddContinuousVertexWithUV(
			Section,
			LowerA,
			NormalA,
			GridSize,
			MakeCliffTopUVFromCoordinate(AlongA, LowerV),
			Tangent,
			LowerAColor
		);
		const int32 B = AddContinuousVertexWithUV(
			Section,
			LowerB,
			NormalB,
			GridSize,
			MakeCliffTopUVFromCoordinate(AlongB, LowerV),
			Tangent,
			LowerBColor
		);
		const int32 C = AddContinuousVertexWithUV(
			Section,
			UpperB,
			NormalB,
			GridSize,
			MakeCliffTopUVFromCoordinate(AlongB, UpperV),
			Tangent,
			UpperBColor
		);
		const int32 D = AddContinuousVertexWithUV(
			Section,
			UpperA,
			NormalA,
			GridSize,
			MakeCliffTopUVFromCoordinate(AlongA, UpperV),
			Tangent,
			UpperAColor
		);
		const FVector DesiredNormal =
			(NormalA + NormalB).GetSafeNormal();
		const FVector WindingNormal = FVector::CrossProduct(
			LowerB - LowerA,
			UpperB - LowerA
		);

		if (FVector::DotProduct(WindingNormal, DesiredNormal) >= 0.0f)
		{
			Section.Triangles.Add(A);
			Section.Triangles.Add(C);
			Section.Triangles.Add(B);
			Section.Triangles.Add(A);
			Section.Triangles.Add(D);
			Section.Triangles.Add(C);
		}
		else
		{
			Section.Triangles.Add(A);
			Section.Triangles.Add(C);
			Section.Triangles.Add(D);
			Section.Triangles.Add(A);
			Section.Triangles.Add(B);
			Section.Triangles.Add(C);
		}
	}

	void AddContinuousTriangle(
		FTileMapContinuousSection& Section,
		const FVector& PositionA,
		const FVector& PositionB,
		const FVector& PositionC,
		const FVector& NormalA,
		const FVector& NormalB,
		const FVector& NormalC,
		const FVector& DesiredNormal,
		float GridSize,
		const FColor& ColorA = ContinuousCliffSurfaceColor,
		const FColor& ColorB = ContinuousCliffSurfaceColor,
		const FColor& ColorC = ContinuousCliffSurfaceColor
	)
	{
		const int32 A = AddContinuousVertex(
			Section, PositionA, NormalA, GridSize, ColorA);
		const int32 B = AddContinuousVertex(
			Section, PositionB, NormalB, GridSize, ColorB);
		const int32 C = AddContinuousVertex(
			Section, PositionC, NormalC, GridSize, ColorC);
		const FVector WindingNormal =
			FVector::CrossProduct(
				PositionB - PositionA,
				PositionC - PositionA
			);

		Section.Triangles.Add(A);

		// Match the original v1.2.6 RawMesh 0,2,1 front-face convention.
		if (FVector::DotProduct(WindingNormal, DesiredNormal) >= 0.0f)
		{
			Section.Triangles.Add(C);
			Section.Triangles.Add(B);
		}
		else
		{
			Section.Triangles.Add(B);
			Section.Triangles.Add(C);
		}
	}

	void AddContinuousCliffTopTriangle(
		FTileMapContinuousSection& Section,
		const FVector& PositionA,
		const FVector& PositionB,
		const FVector& PositionC,
		const FVector& NormalA,
		const FVector& NormalB,
		const FVector& NormalC,
		const FVector& DesiredNormal,
		float GridSize,
		const FVector& Tangent,
		float UA,
		float UB,
		float UC,
		float VA,
		float VB,
		float VC,
		const FColor& ColorA = ContinuousCliffEdgeSurfaceColor,
		const FColor& ColorB = ContinuousCliffEdgeSurfaceColor,
		const FColor& ColorC = ContinuousCliffEdgeSurfaceColor
	)
	{
		const FVector SafeTangent = MakeCliffTopTangent(
			Tangent,
			DesiredNormal
		);
		const int32 A = AddContinuousVertexWithUV(
			Section,
			PositionA,
			NormalA,
			GridSize,
			MakeCliffTopUVFromCoordinate(UA, VA),
			SafeTangent,
			ColorA
		);
		const int32 B = AddContinuousVertexWithUV(
			Section,
			PositionB,
			NormalB,
			GridSize,
			MakeCliffTopUVFromCoordinate(UB, VB),
			SafeTangent,
			ColorB
		);
		const int32 C = AddContinuousVertexWithUV(
			Section,
			PositionC,
			NormalC,
			GridSize,
			MakeCliffTopUVFromCoordinate(UC, VC),
			SafeTangent,
			ColorC
		);
		const FVector WindingNormal = FVector::CrossProduct(
			PositionB - PositionA,
			PositionC - PositionA
		);

		Section.Triangles.Add(A);

		if (FVector::DotProduct(WindingNormal, DesiredNormal) >= 0.0f)
		{
			Section.Triangles.Add(C);
			Section.Triangles.Add(B);
		}
		else
		{
			Section.Triangles.Add(B);
			Section.Triangles.Add(C);
		}
	}
}

ATileMapTerrainActor::ATileMapTerrainActor()
{
	PrimaryActorTick.bCanEverTick = false;

	SceneRoot = CreateDefaultSubobject<USceneComponent>(
		TEXT("SceneRoot")
	);

	SceneRoot->SetMobility(EComponentMobility::Movable);
	SetRootComponent(SceneRoot);

	GridSize = 100.0f;
	ChunkSize = 16;
	bGenerateCollision = true;
	DefaultTerrainMaterial = nullptr;
	bUseContinuousTerrainPrototype = true;
	bDebugContinuousSlantSupports = false;
	ContinuousChamferWidth = 12.0f;
	ContinuousChamferDepth = 5.0f;
	ContinuousEdgeIrregularity = 10.0f;
	ContinuousEdgeWavelength = 144.0f;
	ContinuousCliffCornerRadius = 20.0f;
	ContinuousCliffFootBlendWidth = 12.0f;
	ContinuousCliffFootBlendHeight = 20.0f;
	ContinuousPathBlendWidth = 20.0f;
	bGenerateTerrainDetailsOnBake = false;
	TerrainDetailSeed = 1337;
	TerrainDetailDensity = 0.08f;
	TerrainDetailCoveragePercent = 8.0f;
	TerrainDetailMaximumInstances = 128;
	TerrainDetailMinimumSpacingCells = 2;
	TerrainDetailEdgeInset = 15.0f;
	TerrainDetailStartCullDistance = 3000;
	TerrainDetailEndCullDistance = 8000;

	static ConstructorHelpers::FObjectFinder<UStaticMesh>
		DefaultCubeMesh(
			TEXT("/Engine/BasicShapes/Cube.Cube")
		);

	if (DefaultCubeMesh.Succeeded())
	{
		BlockMesh = DefaultCubeMesh.Object;
	}
}

void ATileMapTerrainActor::BeginPlay()
{
	Super::BeginPlay();

	// Chunk components are intentionally transient derived caches. PIE and a
	// packaged level do not retain those editor-world component instances, so
	// rebuild them from the serialized block arrays in the game world.
	RebuildAllChunks();
}

void ATileMapTerrainActor::OnConstruction(
	const FTransform& Transform
)
{
	Super::OnConstruction(Transform);
	RebuildAllChunks();
}

#if WITH_EDITOR

void ATileMapTerrainActor::PostEditUndo()
{
	Super::PostEditUndo();

	// The serialized block arrays are the transactional source of truth.
	// HISM components, collision, and lookup tables are derived caches.
	RebuildAllChunks();
}

void ATileMapTerrainActor::PostEditChangeProperty(
	FPropertyChangedEvent& PropertyChangedEvent
)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	const FName PropertyName = PropertyChangedEvent.GetPropertyName();
	const FName MemberPropertyName =
		PropertyChangedEvent.MemberProperty
			? PropertyChangedEvent.MemberProperty->GetFName()
			: NAME_None;

	auto IsChangedProperty =
		[PropertyName, MemberPropertyName](const FName CandidateName)
		{
			return
				PropertyName == CandidateName ||
				MemberPropertyName == CandidateName;
		};

	const bool bTerrainDetailOnlyChange =
		IsChangedProperty(GET_MEMBER_NAME_CHECKED(
			ATileMapTerrainActor,
			bGenerateTerrainDetailsOnBake
		)) ||
		IsChangedProperty(GET_MEMBER_NAME_CHECKED(
			ATileMapTerrainActor,
			TerrainDetailSeed
		)) ||
		IsChangedProperty(GET_MEMBER_NAME_CHECKED(
			ATileMapTerrainActor,
			TerrainDetailDensity
		)) ||
		IsChangedProperty(GET_MEMBER_NAME_CHECKED(
			ATileMapTerrainActor,
			TerrainDetailCoveragePercent
		)) ||
		IsChangedProperty(GET_MEMBER_NAME_CHECKED(
			ATileMapTerrainActor,
			TerrainDetailMaximumInstances
		)) ||
		IsChangedProperty(GET_MEMBER_NAME_CHECKED(
			ATileMapTerrainActor,
			TerrainDetailMinimumSpacingCells
		)) ||
		IsChangedProperty(GET_MEMBER_NAME_CHECKED(
			ATileMapTerrainActor,
			TerrainDetailEdgeInset
		)) ||
		IsChangedProperty(GET_MEMBER_NAME_CHECKED(
			ATileMapTerrainActor,
			TerrainDetailStartCullDistance
		)) ||
		IsChangedProperty(GET_MEMBER_NAME_CHECKED(
			ATileMapTerrainActor,
			TerrainDetailEndCullDistance
		)) ||
		IsChangedProperty(GET_MEMBER_NAME_CHECKED(
			ATileMapTerrainActor,
			TerrainDetailPalette
		));

	// These properties affect only the optional actor created by Bake. Editing
	// them must not regenerate the live continuous/HISM terrain.
	if (bTerrainDetailOnlyChange)
	{
		return;
	}

	RebuildAllChunks();
}

#endif

int32 ATileMapTerrainActor::FloorDivide(
	int32 Value,
	int32 Divisor
)
{
	if (Divisor <= 0)
	{
		return 0;
	}

	int32 Quotient = Value / Divisor;
	const int32 Remainder = Value % Divisor;

	if (Remainder < 0)
	{
		--Quotient;
	}

	return Quotient;
}

FIntVector ATileMapTerrainActor::GetChunkCoordinate(
	const FIntVector& GridPosition
) const
{
	const int32 SafeChunkSize =
		FMath::Max(ChunkSize, 1);

	return FIntVector(
		FloorDivide(GridPosition.X, SafeChunkSize),
		FloorDivide(GridPosition.Y, SafeChunkSize),
		FloorDivide(GridPosition.Z, SafeChunkSize)
	);
}

FVector ATileMapTerrainActor::GridToLocal(
	const FIntVector& GridPosition
) const
{
	const float SafeGridSize =
		FMath::Max(GridSize, 1.0f);

	return FVector(
		(GridPosition.X + 0.5f) * SafeGridSize,
		(GridPosition.Y + 0.5f) * SafeGridSize,
		(GridPosition.Z + 0.5f) * SafeGridSize
	);
}

FVector ATileMapTerrainActor::GridToWorld(
	const FIntVector& GridPosition
) const
{
	return GetActorTransform().TransformPosition(
		GridToLocal(GridPosition)
	);
}

FIntVector ATileMapTerrainActor::WorldToGrid(
	const FVector& WorldPosition
) const
{
	const float SafeGridSize =
		FMath::Max(GridSize, 1.0f);

	const FVector LocalPosition =
		GetActorTransform().InverseTransformPosition(
			WorldPosition
		);

	return FIntVector(
		FMath::FloorToInt(LocalPosition.X / SafeGridSize),
		FMath::FloorToInt(LocalPosition.Y / SafeGridSize),
		FMath::FloorToInt(LocalPosition.Z / SafeGridSize)
	);
}

int32 ATileMapTerrainActor::FindBlockIndex(
	const FIntVector& GridPosition
) const
{
	const int32* FoundIndex =
		BlockIndexLookup.Find(GridPosition);

	return FoundIndex ? *FoundIndex : INDEX_NONE;
}

bool ATileMapTerrainActor::HasBlock(
	const FIntVector& GridPosition
) const
{
	return OccupancyLookup.Contains(GridPosition);
}

bool ATileMapTerrainActor::HasPaintedPath(
	const FIntVector& GridPosition
) const
{
	return PaintedPathLookup.Contains(GridPosition);
}

bool ATileMapTerrainActor::SetPathPainted(
	const FIntVector& GridPosition,
	bool bPainted
)
{
	TArray<FIntVector> GridPositions;
	GridPositions.Add(GridPosition);
	return SetPathsPainted(GridPositions, bPainted);
}

bool ATileMapTerrainActor::SetPathsPainted(
	const TArray<FIntVector>& GridPositions,
	bool bPainted
)
{
	if (GridPositions.Num() == 0)
	{
		return false;
	}

	TSet<FIntVector> ChangedPositions;

	for (const FIntVector& GridPosition : GridPositions)
	{
		if (!HasBlock(GridPosition))
		{
			continue;
		}

		const bool bWasPainted = HasPaintedPath(GridPosition);

		if (bWasPainted == bPainted)
		{
			continue;
		}

		if (bPainted)
		{
			PaintedPathBlocks.Add(GridPosition);
			PaintedPathLookup.Add(GridPosition);
		}
		else
		{
			PaintedPathBlocks.RemoveSingle(GridPosition);
			PaintedPathLookup.Remove(GridPosition);
		}

		for (int32 XOffset = -1; XOffset <= 1; ++XOffset)
		{
			for (int32 YOffset = -1; YOffset <= 1; ++YOffset)
			{
				ChangedPositions.Add(
					GridPosition + FIntVector(XOffset, YOffset, 0)
				);
			}
		}
	}

	if (ChangedPositions.Num() == 0)
	{
		return false;
	}

	RebuildChunksForPositions(ChangedPositions);
	return true;
}

bool ATileMapTerrainActor::IsBlockExcludedFromContinuousTerrain(
	const FIntVector& GridPosition
) const
{
	return ModularTerrainBlockLookup.Contains(GridPosition);
}

bool ATileMapTerrainActor::SetBlockContinuousTerrain(
	const FIntVector& GridPosition,
	bool bUseContinuousTerrain
)
{
	TArray<FIntVector> GridPositions;
	GridPositions.Add(GridPosition);
	return SetBlocksContinuousTerrain(
		GridPositions,
		bUseContinuousTerrain
	);
}

bool ATileMapTerrainActor::SetBlocksContinuousTerrain(
	const TArray<FIntVector>& GridPositions,
	bool bUseContinuousTerrain
)
{
	if (GridPositions.Num() == 0)
	{
		return false;
	}

	TSet<FIntVector> ChangedPositions;
	const bool bShouldExclude = !bUseContinuousTerrain;

	for (const FIntVector& GridPosition : GridPositions)
	{
		if (!HasBlock(GridPosition))
		{
			continue;
		}

		const bool bWasExcluded =
			IsBlockExcludedFromContinuousTerrain(GridPosition);

		if (bWasExcluded == bShouldExclude)
		{
			continue;
		}

		if (bShouldExclude)
		{
			ModularTerrainBlocks.Add(GridPosition);
			ModularTerrainBlockLookup.Add(GridPosition);
		}
		else
		{
			ModularTerrainBlocks.RemoveSingle(GridPosition);
			ModularTerrainBlockLookup.Remove(GridPosition);
		}

		// Continuous edge construction, path overlays, and HISM ownership can
		// involve neighboring cells or a neighboring chunk.
		for (int32 ZOffset = -1; ZOffset <= 1; ++ZOffset)
		{
			for (int32 XOffset = -1; XOffset <= 1; ++XOffset)
			{
				for (int32 YOffset = -1; YOffset <= 1; ++YOffset)
				{
					ChangedPositions.Add(
						GridPosition +
						FIntVector(XOffset, YOffset, ZOffset)
					);
				}
			}
		}
	}

	if (ChangedPositions.Num() == 0)
	{
		return false;
	}

	RebuildChunksForPositions(ChangedPositions);
	return true;
}

int32 ATileMapTerrainActor::GetTileTypeCount() const
{
	// Type 0 is always the backward-compatible BlockMesh.
	return TilePalette.Num() + 1;
}

int32 ATileMapTerrainActor::ClampTileType(
	int32 TileType
) const
{
	return FMath::Clamp(
		TileType,
		0,
		FMath::Max(GetTileTypeCount() - 1, 0)
	);
}

const FTileMapTileDefinition*
ATileMapTerrainActor::GetTileDefinition(
	int32 TileType
) const
{
	const int32 PaletteIndex = TileType - 1;

	return TilePalette.IsValidIndex(PaletteIndex)
		? &TilePalette[PaletteIndex]
		: nullptr;
}

int32 ATileMapTerrainActor::FindAutoTileVariant(
	FName AutoTileSet,
	ETileMapAutoShape AutoShape
) const
{
	if (AutoTileSet.IsNone())
	{
		return INDEX_NONE;
	}

	for (int32 PaletteIndex = 0;
		PaletteIndex < TilePalette.Num();
		++PaletteIndex)
	{
		const FTileMapTileDefinition& Definition =
			TilePalette[PaletteIndex];

		if (
			Definition.AutoTileSet == AutoTileSet &&
			Definition.AutoShape == AutoShape &&
			Definition.Mesh
			)
		{
			return PaletteIndex + 1;
		}
	}

	return INDEX_NONE;
}

ETileMapAutoShape ATileMapTerrainActor::DetermineAutoShape(
	const FIntVector& GridPosition,
	uint8& OutQuarterTurns
) const
{
	OutQuarterTurns = 0;

	static const FIntVector CardinalOffsets[4] =
	{
		FIntVector(1, 0, 0),
		FIntVector(0, 1, 0),
		FIntVector(-1, 0, 0),
		FIntVector(0, -1, 0)
	};

	bool bOpen[4] = { false, false, false, false };
	int32 OpenCount = 0;

	for (int32 DirectionIndex = 0;
		DirectionIndex < 4;
		++DirectionIndex)
	{
		bOpen[DirectionIndex] =
			!HasBlock(
				GridPosition + CardinalOffsets[DirectionIndex]
			);

		OpenCount += bOpen[DirectionIndex] ? 1 : 0;
	}

	const bool bHasBlockAbove =
		HasBlock(GridPosition + FIntVector(0, 0, 1));

	if (bHasBlockAbove)
	{
		if (OpenCount == 0)
		{
			return ETileMapAutoShape::Flat;
		}

		for (int32 DirectionIndex = 0;
			DirectionIndex < 4;
			++DirectionIndex)
		{
			if (bOpen[DirectionIndex])
			{
				OutQuarterTurns =
					static_cast<uint8>(DirectionIndex);
				break;
			}
		}

		return ETileMapAutoShape::Cliff;
	}

	if (OpenCount == 0)
	{
		static const FIntVector DiagonalOffsets[4] =
		{
			FIntVector(1, 1, 0),
			FIntVector(-1, 1, 0),
			FIntVector(-1, -1, 0),
			FIntVector(1, -1, 0)
		};

		for (int32 CornerIndex = 0;
			CornerIndex < 4;
			++CornerIndex)
		{
			if (!HasBlock(GridPosition + DiagonalOffsets[CornerIndex]))
			{
				OutQuarterTurns =
					static_cast<uint8>(CornerIndex);
				return ETileMapAutoShape::InnerCorner;
			}
		}

		return ETileMapAutoShape::Flat;
	}

	if (OpenCount == 1)
	{
		for (int32 DirectionIndex = 0;
			DirectionIndex < 4;
			++DirectionIndex)
		{
			if (bOpen[DirectionIndex])
			{
				OutQuarterTurns =
					static_cast<uint8>(DirectionIndex);
				break;
			}
		}

		return ETileMapAutoShape::Edge;
	}

	if (OpenCount == 2)
	{
		if (bOpen[0] && bOpen[2])
		{
			OutQuarterTurns = 0;
			return ETileMapAutoShape::Pass;
		}

		if (bOpen[1] && bOpen[3])
		{
			OutQuarterTurns = 1;
			return ETileMapAutoShape::Pass;
		}

		if (bOpen[0] && bOpen[1])
		{
			OutQuarterTurns = 0;
		}
		else if (bOpen[1] && bOpen[2])
		{
			OutQuarterTurns = 1;
		}
		else if (bOpen[2] && bOpen[3])
		{
			OutQuarterTurns = 2;
		}
		else
		{
			OutQuarterTurns = 3;
		}

		return ETileMapAutoShape::OuterCorner;
	}

	if (OpenCount == 3)
	{
		for (int32 DirectionIndex = 0;
			DirectionIndex < 4;
			++DirectionIndex)
		{
			if (!bOpen[DirectionIndex])
			{
				OutQuarterTurns =
					static_cast<uint8>((DirectionIndex + 1) % 4);
				break;
			}
		}

		return ETileMapAutoShape::Peninsula;
	}

	return ETileMapAutoShape::Island;
}

bool ATileMapTerrainActor::ResolveAutoTileAt(
	const FIntVector& GridPosition,
	TSet<FIntVector>& OutChangedPositions
)
{
	const int32 BlockIndex = FindBlockIndex(GridPosition);

	if (
		!BlockTileTypes.IsValidIndex(BlockIndex) ||
		!BlockRotations.IsValidIndex(BlockIndex)
		)
	{
		return false;
	}

	const int32 CurrentTileType =
		ClampTileType(BlockTileTypes[BlockIndex]);

	const FTileMapTileDefinition* CurrentDefinition =
		GetTileDefinition(CurrentTileType);

	if (
		!CurrentDefinition ||
		CurrentDefinition->AutoTileSet.IsNone() ||
		CurrentDefinition->AutoShape == ETileMapAutoShape::Ramp ||
		CurrentDefinition->AutoShape == ETileMapAutoShape::Stair ||
		CurrentDefinition->AutoShape == ETileMapAutoShape::DiagonalEdge
		)
	{
		return false;
	}

	uint8 ResolvedRotation = 0;
	const ETileMapAutoShape ResolvedShape =
		DetermineAutoShape(
			GridPosition,
			ResolvedRotation
		);

	int32 ResolvedTileType =
		FindAutoTileVariant(
			CurrentDefinition->AutoTileSet,
			ResolvedShape
		);

	if (ResolvedTileType == INDEX_NONE)
	{
		ResolvedTileType =
			FindAutoTileVariant(
				CurrentDefinition->AutoTileSet,
				ETileMapAutoShape::Manual
			);
	}

	if (ResolvedTileType == INDEX_NONE)
	{
		return false;
	}

	const bool bChanged =
		BlockTileTypes[BlockIndex] != ResolvedTileType ||
		BlockRotations[BlockIndex] != ResolvedRotation;

	if (!bChanged)
	{
		return false;
	}

	BlockTileTypes[BlockIndex] = ResolvedTileType;
	BlockRotations[BlockIndex] = ResolvedRotation;
	OutChangedPositions.Add(GridPosition);
	return true;
}

void ATileMapTerrainActor::RebuildChunksForPositions(
	const TSet<FIntVector>& GridPositions
)
{
	TSet<FIntVector> ChunksToRebuild;

	for (const FIntVector& GridPosition : GridPositions)
	{
		ChunksToRebuild.Add(
			GetChunkCoordinate(GridPosition)
		);
	}

	for (const FIntVector& ChunkCoordinate : ChunksToRebuild)
	{
		RebuildChunk(ChunkCoordinate);
	}
}

void ATileMapTerrainActor::RefreshAutoTilesAround(
	const FIntVector& GridPosition
)
{
	TSet<FIntVector> PositionsToCheck;

	for (int32 ZOffset = -1; ZOffset <= 1; ++ZOffset)
	{
		for (int32 XOffset = -1; XOffset <= 1; ++XOffset)
		{
			for (int32 YOffset = -1; YOffset <= 1; ++YOffset)
			{
				PositionsToCheck.Add(
					GridPosition +
					FIntVector(XOffset, YOffset, ZOffset)
				);
			}
		}
	}

	TSet<FIntVector> ChangedPositions;

	for (const FIntVector& PositionToCheck : PositionsToCheck)
	{
		ResolveAutoTileAt(
			PositionToCheck,
			ChangedPositions
		);
	}

	// Include the complete local neighborhood so removed and newly created
	// instances are rebuilt even when no auto variant changed.
	for (const FIntVector& PositionToCheck : PositionsToCheck)
	{
		ChangedPositions.Add(PositionToCheck);
	}
	RebuildChunksForPositions(ChangedPositions);
}

void ATileMapTerrainActor::ResolveAllAutoTiles()
{
	TSet<FIntVector> ChangedPositions;

	for (const FIntVector& GridPosition : OccupiedBlocks)
	{
		ResolveAutoTileAt(
			GridPosition,
			ChangedPositions
		);
	}
}

FText ATileMapTerrainActor::GetTileDisplayName(
	int32 TileType
) const
{
	const int32 SafeTileType = ClampTileType(TileType);

	if (SafeTileType == 0)
	{
		return FText::FromString(TEXT("Default Block"));
	}

	const FTileMapTileDefinition* Definition =
		GetTileDefinition(SafeTileType);

	if (
		Definition &&
		!Definition->TileName.IsNone()
		)
	{
		return FText::FromString(
			Definition->TileName.ToString()
		);
	}

	return FText::FromString(
		FString::Printf(
			TEXT("Palette Tile %d"),
			SafeTileType
		)
	);
}

UStaticMesh* ATileMapTerrainActor::GetTileMesh(
	int32 TileType
) const
{
	const FTileMapTileDefinition* Definition =
		GetTileDefinition(ClampTileType(TileType));

	if (Definition && Definition->Mesh)
	{
		return Definition->Mesh;
	}

	return BlockMesh;
}

UMaterialInterface*
ATileMapTerrainActor::GetTileMaterialOverride(
	int32 TileType
) const
{
	// A shared terrain material deliberately wins over palette and source-mesh
	// materials. This keeps generated slants, rebuilt HISM chunks, and baked
	// output visually identical, including on terrain created by older builds.
	if (DefaultTerrainMaterial)
	{
		return DefaultTerrainMaterial;
	}

	const FTileMapTileDefinition* Definition =
		GetTileDefinition(ClampTileType(TileType));

	return Definition
		? Definition->MaterialOverride
		: nullptr;
}

bool ATileMapTerrainActor::UsesLegacyMeshInContinuousMode(
	int32 TileType
) const
{
	const FTileMapTileDefinition* Definition =
		GetTileDefinition(ClampTileType(TileType));

	if (!Definition)
	{
		return false;
	}

	if (Definition->AutoShape == ETileMapAutoShape::Ramp)
	{
		int32 IgnoredSegmentCount = 0;
		int32 IgnoredSegmentIndex = 0;
		return !GetContinuousRampMetadata(
			TileType,
			IgnoredSegmentCount,
			IgnoredSegmentIndex
		);
	}

	if (Definition->AutoShape == ETileMapAutoShape::Stair)
	{
		int32 IgnoredSegmentCount = 0;
		int32 IgnoredSegmentIndex = 0;
		return !GetContinuousStairMetadata(
			TileType,
			IgnoredSegmentCount,
			IgnoredSegmentIndex
		);
	}

	return
		Definition->AutoShape == ETileMapAutoShape::DiagonalEdge &&
		!IsContinuousDiagonalTileType(TileType);
}

bool ATileMapTerrainActor::IsContinuousDiagonalTileType(
	int32 TileType
) const
{
	float IgnoredFraction = 0.0f;
	return GetContinuousDiagonalFraction(
		TileType,
		IgnoredFraction
	);
}

bool ATileMapTerrainActor::GetContinuousDiagonalFraction(
	int32 TileType,
	float& OutFraction
) const
{
	OutFraction = 0.0f;
	const FTileMapTileDefinition* Definition =
		GetTileDefinition(ClampTileType(TileType));

	if (
		!Definition ||
		Definition->AutoShape != ETileMapAutoShape::DiagonalEdge ||
		Definition->TileName.IsNone()
		)
	{
		return false;
	}

	// Only the full-cell 45-degree V4 diagonal is continuous terrain. Earlier
	// shallow assets remain loadable through modular HISM fallback, but are not
	// interpreted as fractional continuous boundaries.
	const FString StableName = Definition->TileName.ToString();
	const FString AngleToken(TEXT("TileMapV4DiagonalEdge_A"));
	const int32 TokenIndex = StableName.Find(
		*AngleToken,
		ESearchCase::CaseSensitive,
		ESearchDir::FromStart
	);

	if (TokenIndex == INDEX_NONE)
	{
		return false;
	}

	const int32 AngleIndex = TokenIndex + AngleToken.Len();

	if (StableName.Len() < AngleIndex + 4)
	{
		return false;
	}

	for (int32 DigitIndex = 0; DigitIndex < 3; ++DigitIndex)
	{
		if (!FChar::IsDigit(StableName[AngleIndex + DigitIndex]))
		{
			return false;
		}
	}

	if (StableName[AngleIndex + 3] != TCHAR('_'))
	{
		return false;
	}

	const int32 AngleTenths = FCString::Atoi(
		*StableName.Mid(AngleIndex, 3)
	);

	if (AngleTenths != 450)
	{
		return false;
	}

	OutFraction = 1.0f;
	return true;
}

bool ATileMapTerrainActor::GetContinuousRampMetadata(
	int32 TileType,
	int32& OutSegmentCount,
	int32& OutSegmentIndex
) const
{
	OutSegmentCount = 0;
	OutSegmentIndex = 0;
	const FTileMapTileDefinition* Definition =
		GetTileDefinition(ClampTileType(TileType));

	if (
		!Definition ||
		Definition->AutoShape != ETileMapAutoShape::Ramp ||
		Definition->TileName.IsNone()
		)
	{
		return false;
	}

	// V4 ramp palette names are persistent map metadata. N is the complete
	// run length, S is this cell's segment, and A is the effective angle in
	// tenths: TileMapV4Ramp_N01_S00_A450_. Reconstructing from that stable name
	// keeps existing v1.2.6 maps transactional without adding another array.
	const FString StableName = Definition->TileName.ToString();
	const FString RampToken(TEXT("TileMapV4Ramp_N"));
	const int32 TokenIndex = StableName.Find(
		*RampToken,
		ESearchCase::CaseSensitive,
		ESearchDir::FromStart
	);

	if (TokenIndex == INDEX_NONE)
	{
		return false;
	}

	const int32 CountIndex = TokenIndex + RampToken.Len();

	if (StableName.Len() < CountIndex + 12)
	{
		return false;
	}

	if (
		!FChar::IsDigit(StableName[CountIndex]) ||
		!FChar::IsDigit(StableName[CountIndex + 1]) ||
		StableName.Mid(CountIndex + 2, 2) != TEXT("_S") ||
		!FChar::IsDigit(StableName[CountIndex + 4]) ||
		!FChar::IsDigit(StableName[CountIndex + 5]) ||
		StableName.Mid(CountIndex + 6, 2) != TEXT("_A") ||
		!FChar::IsDigit(StableName[CountIndex + 8]) ||
		!FChar::IsDigit(StableName[CountIndex + 9]) ||
		!FChar::IsDigit(StableName[CountIndex + 10]) ||
		StableName[CountIndex + 11] != TCHAR('_')
		)
	{
		return false;
	}

	const int32 SegmentCount = FCString::Atoi(
		*StableName.Mid(CountIndex, 2)
	);
	const int32 SegmentIndex = FCString::Atoi(
		*StableName.Mid(CountIndex + 4, 2)
	);
	const int32 AngleTenths = FCString::Atoi(
		*StableName.Mid(CountIndex + 8, 3)
	);

	if (
		SegmentCount < 1 ||
		SegmentCount > 8 ||
		SegmentIndex < 0 ||
		SegmentIndex >= SegmentCount ||
		AngleTenths < 50 ||
		AngleTenths > 450
		)
	{
		return false;
	}

	const int32 ExpectedAngleTenths = FMath::RoundToInt(
		FMath::RadiansToDegrees(
			FMath::Atan(1.0f / static_cast<float>(SegmentCount))
		) * 10.0f
	);

	if (FMath::Abs(AngleTenths - ExpectedAngleTenths) > 1)
	{
		return false;
	}

	OutSegmentCount = SegmentCount;
	OutSegmentIndex = SegmentIndex;
	return true;
}

bool ATileMapTerrainActor::IsContinuousRampBlock(
	const FIntVector& GridPosition
) const
{
	if (!HasBlock(GridPosition))
	{
		return false;
	}

	int32 SegmentCount = 0;
	int32 SegmentIndex = 0;

	if (
		!GetContinuousRampMetadata(
			GetBlockTileType(GridPosition),
			SegmentCount,
			SegmentIndex
		)
		)
	{
		return false;
	}

	const uint8 QuarterTurns = GetBlockRotation(GridPosition) % 4;
	FIntVector RampStep(1, 0, 0);

	switch (QuarterTurns)
	{
	case 1:
		RampStep = FIntVector(0, 1, 0);
		break;

	case 2:
		RampStep = FIntVector(-1, 0, 0);
		break;

	case 3:
		RampStep = FIntVector(0, -1, 0);
		break;

	default:
		break;
	}

	const FIntVector RunStart =
		GridPosition - (RampStep * SegmentIndex);
	const FIntVector HorizontalDirections[4] =
	{
		FIntVector(1, 0, 0),
		FIntVector(-1, 0, 0),
		FIntVector(0, 1, 0),
		FIntVector(0, -1, 0)
	};

	for (int32 RunSegment = 0;
		RunSegment < SegmentCount;
		++RunSegment)
	{
		const FIntVector SegmentPosition =
			RunStart + (RampStep * RunSegment);
		int32 OtherCount = 0;
		int32 OtherIndex = 0;

		if (
			!HasBlock(SegmentPosition) ||
			GetBlockRotation(SegmentPosition) % 4 != QuarterTurns ||
			!GetContinuousRampMetadata(
				GetBlockTileType(SegmentPosition),
				OtherCount,
				OtherIndex
			) ||
			OtherCount != SegmentCount ||
			OtherIndex != RunSegment ||
			HasBlock(SegmentPosition + FIntVector(0, 0, 1))
			)
		{
			return false;
		}

		for (const FIntVector& Direction : HorizontalDirections)
		{
			const FIntVector NeighborPosition =
				SegmentPosition + Direction;

			if (!HasBlock(NeighborPosition))
			{
				continue;
			}

			const FTileMapTileDefinition* NeighborDefinition =
				GetTileDefinition(
					GetBlockTileType(NeighborPosition)
				);

			if (!NeighborDefinition)
			{
				continue;
			}

			// Mixed slants retain their authored modular meshes. A ramp may join
			// only its own next segment or a parallel run with the same profile.
			if (
				NeighborDefinition->AutoShape ==
				ETileMapAutoShape::DiagonalEdge
				)
			{
				return false;
			}

			if (NeighborDefinition->AutoShape != ETileMapAutoShape::Ramp)
			{
				continue;
			}

			int32 NeighborCount = 0;
			int32 NeighborIndex = 0;
			const int32 AlongRun =
				(Direction.X * RampStep.X) +
				(Direction.Y * RampStep.Y);
			const int32 ExpectedNeighborIndex =
				RunSegment + AlongRun;

			if (
				GetBlockRotation(NeighborPosition) % 4 != QuarterTurns ||
				!GetContinuousRampMetadata(
					GetBlockTileType(NeighborPosition),
					NeighborCount,
					NeighborIndex
				) ||
				NeighborCount != SegmentCount ||
				NeighborIndex != ExpectedNeighborIndex
				)
			{
				return false;
			}
		}
	}

	return true;
}

bool ATileMapTerrainActor::GetContinuousRampBoundaryChamferZ(
	const FIntVector& RampPosition,
	const FVector2D& LocalPoint,
	float FlatTopZ,
	float MaximumChamferDepth,
	float& OutChamferZ
) const
{
	OutChamferZ = FlatTopZ;

	// Callers establish full continuous-ramp validity once before registering
	// the shared edge. Avoid repeating that run-wide validation for every top
	// vertex and finite-difference sample along the same boundary.
	if (!HasBlock(RampPosition))
	{
		return false;
	}

	int32 SegmentCount = 0;
	int32 SegmentIndex = 0;

	if (
		!GetContinuousRampMetadata(
			GetBlockTileType(RampPosition),
			SegmentCount,
			SegmentIndex
		)
		)
	{
		return false;
	}

	const float SafeGridSize = FMath::Max(GridSize, 1.0f);
	const uint8 QuarterTurns = GetBlockRotation(RampPosition) % 4;
	FIntVector RampStep(1, 0, 0);
	FVector2D RiseAxis(1.0f, 0.0f);

	switch (QuarterTurns)
	{
	case 1:
		RampStep = FIntVector(0, 1, 0);
		RiseAxis = FVector2D(0.0f, 1.0f);
		break;

	case 2:
		RampStep = FIntVector(-1, 0, 0);
		RiseAxis = FVector2D(-1.0f, 0.0f);
		break;

	case 3:
		RampStep = FIntVector(0, -1, 0);
		RiseAxis = FVector2D(0.0f, -1.0f);
		break;

	default:
		break;
	}

	auto IsCompatibleRampNeighbor =
		[&](
			const FIntVector& NeighborPosition,
			int32 ExpectedSegmentIndex
		)
		{
			int32 NeighborCount = 0;
			int32 NeighborIndex = 0;

			return
				HasBlock(NeighborPosition) &&
				GetBlockRotation(NeighborPosition) % 4 ==
					QuarterTurns &&
				GetContinuousRampMetadata(
					GetBlockTileType(NeighborPosition),
					NeighborCount,
					NeighborIndex
				) &&
				NeighborCount == SegmentCount &&
				NeighborIndex == ExpectedSegmentIndex;
		};

	const FIntVector RampRunStart =
		RampPosition - (RampStep * SegmentIndex);
	const FIntVector RampRunEnd =
		RampPosition +
			(
				RampStep *
					(SegmentCount - 1 - SegmentIndex)
			);
	const bool bContinuesFromLowerRamp =
		IsCompatibleRampNeighbor(
			RampRunStart -
				RampStep +
				FIntVector(0, 0, -1),
			SegmentCount - 1
		);
	const bool bContinuesIntoHigherRamp =
		IsCompatibleRampNeighbor(
			RampRunEnd +
				RampStep +
				FIntVector(0, 0, 1),
			0
		);
	const float RunLength = SegmentCount * SafeGridSize;
	const float RampEndTransitionWidth = FMath::Clamp(
		ContinuousChamferWidth * 2.0f,
		4.0f,
		SafeGridSize * 0.3f
	);
	const float BlendAlpha = FMath::Clamp(
		RampEndTransitionWidth / FMath::Max(RunLength, 1.0f),
		0.001f,
		0.3f
	);

	auto SmoothRampEndUnit =
		[](float Value)
		{
			const float SafeValue = FMath::Clamp(
				Value,
				0.0f,
				1.0f
			);
			const float SafeValueSquared =
				SafeValue * SafeValue;
			const float SafeValueCubed =
				SafeValueSquared * SafeValue;

			return
				SafeValueCubed *
				(
					6.0f -
					(8.0f * SafeValue) +
					(3.0f * SafeValueSquared)
				);
		};

	auto EvaluateHeightAtLocalU =
		[&](float LocalU)
		{
			const float GlobalAlpha = FMath::Clamp(
				(
					SegmentIndex +
					FMath::Clamp(LocalU, 0.0f, 1.0f)
				) / static_cast<float>(SegmentCount),
				0.0f,
				1.0f
			);
			float HeightFraction = GlobalAlpha;

			if (
				!bContinuesFromLowerRamp &&
				GlobalAlpha < BlendAlpha
				)
			{
				HeightFraction =
					BlendAlpha *
					SmoothRampEndUnit(GlobalAlpha / BlendAlpha);
			}
			else if (
				!bContinuesIntoHigherRamp &&
				GlobalAlpha > 1.0f - BlendAlpha
				)
			{
				const float Alpha =
					(GlobalAlpha - (1.0f - BlendAlpha)) /
					BlendAlpha;
				HeightFraction =
					1.0f -
					(
						BlendAlpha *
						SmoothRampEndUnit(1.0f - Alpha)
					);
			}

			return
				(RampPosition.Z * SafeGridSize) +
				(HeightFraction * SafeGridSize);
		};
	auto EvaluateChamferAtLocalU =
		[&](float LocalU)
		{
			return FMath::Max(
				EvaluateHeightAtLocalU(LocalU),
				FlatTopZ - FMath::Max(MaximumChamferDepth, 0.0f)
			);
		};

	// The ramp mesh uses these exact longitudinal samples. Interpolate between
	// the same samples here so a neighboring flat top cannot trace a different
	// curve and reopen a sub-triangle seam along the shared boundary.
	constexpr int32 RampLongitudinalSubdivisions = 6;
	TArray<float, TInlineAllocator<24>> RampAlphas;
	auto AddRampAlpha =
		[&RampAlphas](float Alpha)
		{
			const float SafeAlpha = FMath::Clamp(Alpha, 0.0f, 1.0f);

			for (const float ExistingAlpha : RampAlphas)
			{
				if (FMath::IsNearlyEqual(ExistingAlpha, SafeAlpha))
				{
					return;
				}
			}

			RampAlphas.Add(SafeAlpha);
		};

	for (int32 Step = 0;
		Step <= RampLongitudinalSubdivisions;
		++Step)
	{
		AddRampAlpha(
			static_cast<float>(Step) /
			static_cast<float>(RampLongitudinalSubdivisions)
		);
	}

	const float RampEndTransitionAlpha =
		RampEndTransitionWidth / SafeGridSize;
	const float RampDetailFractions[] =
	{
		0.025f,
		0.05f,
		0.1f,
		0.2f,
		0.4f,
		0.7f,
		1.0f
	};

	for (const float DetailFraction : RampDetailFractions)
	{
		const float EndDetailAlpha =
			RampEndTransitionAlpha * DetailFraction;

		if (SegmentIndex == 0 && !bContinuesFromLowerRamp)
		{
			AddRampAlpha(EndDetailAlpha);
		}

		if (
			SegmentIndex == SegmentCount - 1 &&
			!bContinuesIntoHigherRamp
			)
		{
			AddRampAlpha(1.0f - EndDetailAlpha);
		}
	}

	RampAlphas.Sort();
	const FVector2D BlockCenter(
		(RampPosition.X + 0.5f) * SafeGridSize,
		(RampPosition.Y + 0.5f) * SafeGridSize
	);
	const float LocalU = FMath::Clamp(
		FVector2D::DotProduct(
			LocalPoint - BlockCenter,
			RiseAxis
		) / SafeGridSize + 0.5f,
		0.0f,
		1.0f
	);

	for (int32 AlphaIndex = 0;
		AlphaIndex < RampAlphas.Num() - 1;
		++AlphaIndex)
	{
		const float AlphaA = RampAlphas[AlphaIndex];
		const float AlphaB = RampAlphas[AlphaIndex + 1];

		if (LocalU > AlphaB + KINDA_SMALL_NUMBER)
		{
			continue;
		}

		const float IntervalAlpha = FMath::IsNearlyEqual(AlphaA, AlphaB)
			? 0.0f
			: FMath::Clamp(
				(LocalU - AlphaA) / (AlphaB - AlphaA),
				0.0f,
				1.0f
			);
		OutChamferZ = FMath::Lerp(
			EvaluateChamferAtLocalU(AlphaA),
			EvaluateChamferAtLocalU(AlphaB),
			IntervalAlpha
		);
		return true;
	}

	OutChamferZ = EvaluateChamferAtLocalU(1.0f);
	return true;
}

bool ATileMapTerrainActor::GetContinuousStairMetadata(
	int32 TileType,
	int32& OutSegmentCount,
	int32& OutSegmentIndex
) const
{
	OutSegmentCount = 0;
	OutSegmentIndex = 0;
	const FTileMapTileDefinition* Definition =
		GetTileDefinition(ClampTileType(TileType));

	if (
		!Definition ||
		Definition->AutoShape != ETileMapAutoShape::Stair ||
		Definition->TileName.IsNone()
		)
	{
		return false;
	}

	// Stairs deliberately have one persistent standard: two grid cells,
	// 100 units of rise, and 150 units of actual stepped run. The stable name
	// records both run ownership and the fixed 33.69-degree design angle.
	const FString StableName = Definition->TileName.ToString();
	const FString StairToken(TEXT("TileMapV4Stair_N"));
	const int32 TokenIndex = StableName.Find(
		*StairToken,
		ESearchCase::CaseSensitive,
		ESearchDir::FromStart
	);

	if (TokenIndex == INDEX_NONE)
	{
		return false;
	}

	const int32 CountIndex = TokenIndex + StairToken.Len();

	if (
		StableName.Len() < CountIndex + 12 ||
		!FChar::IsDigit(StableName[CountIndex]) ||
		!FChar::IsDigit(StableName[CountIndex + 1]) ||
		StableName.Mid(CountIndex + 2, 2) != TEXT("_S") ||
		!FChar::IsDigit(StableName[CountIndex + 4]) ||
		!FChar::IsDigit(StableName[CountIndex + 5]) ||
		StableName.Mid(CountIndex + 6, 2) != TEXT("_A") ||
		!FChar::IsDigit(StableName[CountIndex + 8]) ||
		!FChar::IsDigit(StableName[CountIndex + 9]) ||
		!FChar::IsDigit(StableName[CountIndex + 10]) ||
		StableName[CountIndex + 11] != TCHAR('_')
		)
	{
		return false;
	}

	const int32 SegmentCount = FCString::Atoi(
		*StableName.Mid(CountIndex, 2)
	);
	const int32 SegmentIndex = FCString::Atoi(
		*StableName.Mid(CountIndex + 4, 2)
	);
	const int32 AngleTenths = FCString::Atoi(
		*StableName.Mid(CountIndex + 8, 3)
	);

	if (
		SegmentCount != 2 ||
		SegmentIndex < 0 ||
		SegmentIndex >= SegmentCount ||
		AngleTenths != 337
		)
	{
		return false;
	}

	OutSegmentCount = SegmentCount;
	OutSegmentIndex = SegmentIndex;
	return true;
}

bool ATileMapTerrainActor::IsContinuousStairBlock(
	const FIntVector& GridPosition
) const
{
	if (!HasBlock(GridPosition))
	{
		return false;
	}

	int32 SegmentCount = 0;
	int32 SegmentIndex = 0;

	if (
		!GetContinuousStairMetadata(
			GetBlockTileType(GridPosition),
			SegmentCount,
			SegmentIndex
		)
		)
	{
		return false;
	}

	const uint8 QuarterTurns = GetBlockRotation(GridPosition) % 4;
	FIntVector StairStep(1, 0, 0);

	switch (QuarterTurns)
	{
	case 1:
		StairStep = FIntVector(0, 1, 0);
		break;
	case 2:
		StairStep = FIntVector(-1, 0, 0);
		break;
	case 3:
		StairStep = FIntVector(0, -1, 0);
		break;
	default:
		break;
	}

	const FIntVector RunStart =
		GridPosition - (StairStep * SegmentIndex);
	const FIntVector HorizontalDirections[4] =
	{
		FIntVector(1, 0, 0),
		FIntVector(-1, 0, 0),
		FIntVector(0, 1, 0),
		FIntVector(0, -1, 0)
	};

	for (int32 RunSegment = 0;
		RunSegment < SegmentCount;
		++RunSegment)
	{
		const FIntVector SegmentPosition =
			RunStart + (StairStep * RunSegment);
		int32 OtherCount = 0;
		int32 OtherIndex = 0;

		if (
			!HasBlock(SegmentPosition) ||
			GetBlockRotation(SegmentPosition) % 4 != QuarterTurns ||
			!GetContinuousStairMetadata(
				GetBlockTileType(SegmentPosition),
				OtherCount,
				OtherIndex
			) ||
			OtherCount != SegmentCount ||
			OtherIndex != RunSegment ||
			HasBlock(SegmentPosition + FIntVector(0, 0, 1))
			)
		{
			return false;
		}

		for (const FIntVector& Direction : HorizontalDirections)
		{
			const FIntVector NeighborPosition =
				SegmentPosition + Direction;

			if (!HasBlock(NeighborPosition))
			{
				continue;
			}

			const FTileMapTileDefinition* NeighborDefinition =
				GetTileDefinition(GetBlockTileType(NeighborPosition));

			if (!NeighborDefinition)
			{
				continue;
			}

			if (
				NeighborDefinition->AutoShape == ETileMapAutoShape::Ramp ||
				NeighborDefinition->AutoShape == ETileMapAutoShape::DiagonalEdge
				)
			{
				return false;
			}

			if (NeighborDefinition->AutoShape != ETileMapAutoShape::Stair)
			{
				continue;
			}

			int32 NeighborCount = 0;
			int32 NeighborIndex = 0;
			const int32 AlongRun =
				(Direction.X * StairStep.X) +
				(Direction.Y * StairStep.Y);
			const int32 ExpectedNeighborIndex = RunSegment + AlongRun;

			if (
				GetBlockRotation(NeighborPosition) % 4 != QuarterTurns ||
				!GetContinuousStairMetadata(
					GetBlockTileType(NeighborPosition),
					NeighborCount,
					NeighborIndex
				) ||
				NeighborCount != SegmentCount ||
				NeighborIndex != ExpectedNeighborIndex
				)
			{
				return false;
			}
		}
	}

	return true;
}

bool ATileMapTerrainActor::IsContinuousSurfaceBlock(
	const FIntVector& GridPosition
) const
{
	if (
		!HasBlock(GridPosition)
		)
	{
		return false;
	}

	const int32 TileType = GetBlockTileType(GridPosition);

	if (UsesLegacyMeshInContinuousMode(TileType))
	{
		return false;
	}

	int32 IgnoredRampCount = 0;
	int32 IgnoredRampIndex = 0;

	if (
		GetContinuousRampMetadata(
			TileType,
			IgnoredRampCount,
			IgnoredRampIndex
		)
		)
	{
		return IsContinuousRampBlock(GridPosition);
	}

	int32 IgnoredStairCount = 0;
	int32 IgnoredStairIndex = 0;

	if (
		GetContinuousStairMetadata(
			TileType,
			IgnoredStairCount,
			IgnoredStairIndex
		)
		)
	{
		return IsContinuousStairBlock(GridPosition);
	}

	if (!IsContinuousDiagonalTileType(TileType))
	{
		return true;
	}

	// A directly covered cut still needs the authored modular topology. A block
	// underneath is ordinary support, however, and must not force the cut back
	// to HISM: its buried top is already suppressed by the occupied cell above.
	return !HasBlock(GridPosition + FIntVector(0, 0, 1));
}

bool ATileMapTerrainActor::ShouldUseContinuousTerrainForBlock(
	const FIntVector& GridPosition
) const
{
	return
		bUseContinuousTerrainPrototype &&
		IsContinuousSurfaceBlock(GridPosition) &&
		!IsBlockExcludedFromContinuousTerrain(GridPosition);
}

bool ATileMapTerrainActor::IsTerrainDetailSurfaceBlock(
	const FIntVector& GridPosition
) const
{
	if (
		!HasBlock(GridPosition) ||
		HasBlock(GridPosition + FIntVector(0, 0, 1))
		)
	{
		return false;
	}

	const FTileMapTileDefinition* Definition =
		GetTileDefinition(GetBlockTileType(GridPosition));

	if (!Definition)
	{
		return true;
	}

	// The first detail pass is deliberately restricted to flat upward cells.
	// Slants, stairs, horizontal cuts, and bridges retain their accepted
	// authored silhouettes without automatic structural clutter.
	return
		Definition->AutoShape != ETileMapAutoShape::Ramp &&
		Definition->AutoShape != ETileMapAutoShape::DiagonalEdge &&
		Definition->AutoShape != ETileMapAutoShape::Stair &&
		Definition->AutoShape != ETileMapAutoShape::Pass;
}

bool ATileMapTerrainActor::GetContinuousCellEdgeCoverage(
	const FIntVector& GridPosition,
	const FIntVector& EdgeDirection,
	float& OutCoverageStart,
	float& OutCoverageEnd
) const
{
	OutCoverageStart = 0.0f;
	OutCoverageEnd = 0.0f;

	if (!IsContinuousSurfaceBlock(GridPosition))
	{
		return false;
	}

	float DiagonalFraction = 0.0f;

	if (
		!GetContinuousDiagonalFraction(
			GetBlockTileType(GridPosition),
			DiagonalFraction
		)
		)
	{
		OutCoverageEnd = 1.0f;
		return true;
	}

	const uint8 QuarterTurns = GetBlockRotation(GridPosition) % 4;

	if (
		(QuarterTurns == 0 && EdgeDirection == FIntVector(0, -1, 0)) ||
		(QuarterTurns == 1 && EdgeDirection == FIntVector(1, 0, 0)) ||
		(QuarterTurns == 2 && EdgeDirection == FIntVector(0, 1, 0)) ||
		(QuarterTurns == 3 && EdgeDirection == FIntVector(-1, 0, 0))
		)
	{
		OutCoverageEnd = 1.0f;
		return true;
	}

	if (
		(QuarterTurns == 0 && EdgeDirection == FIntVector(1, 0, 0)) ||
		(QuarterTurns == 3 && EdgeDirection == FIntVector(0, -1, 0))
		)
	{
		OutCoverageEnd = DiagonalFraction;
		return true;
	}

	if (
		(QuarterTurns == 1 && EdgeDirection == FIntVector(0, 1, 0)) ||
		(QuarterTurns == 2 && EdgeDirection == FIntVector(-1, 0, 0))
		)
	{
		OutCoverageStart = 1.0f - DiagonalFraction;
		OutCoverageEnd = 1.0f;
		return true;
	}

	return false;
}

bool ATileMapTerrainActor::GetContinuousTerrainCoverageAcrossEdge(
	const FIntVector& GridPosition,
	const FIntVector& EdgeDirection,
	float& OutCoverageStart,
	float& OutCoverageEnd
) const
{
	OutCoverageStart = 0.0f;
	OutCoverageEnd = 0.0f;
	const FIntVector NeighborPosition =
		GridPosition + EdgeDirection;

	if (!HasBlock(NeighborPosition))
	{
		return false;
	}

	// Full legacy blocks own their complete authored cell boundary. A fallback
	// slant does not: its wedge can cover only part of that boundary vertically
	// or horizontally. Keep the continuous neighbor's closing face in that
	// mixed case so a covered, malformed, or otherwise unsupported slant can
	// never cut a transparent hole into the surrounding terrain.
	if (!IsContinuousSurfaceBlock(NeighborPosition))
	{
		const FTileMapTileDefinition* NeighborDefinition =
			GetTileDefinition(GetBlockTileType(NeighborPosition));

		if (
			NeighborDefinition &&
			(
				NeighborDefinition->AutoShape ==
					ETileMapAutoShape::Ramp ||
				NeighborDefinition->AutoShape ==
					ETileMapAutoShape::Stair ||
				NeighborDefinition->AutoShape ==
					ETileMapAutoShape::DiagonalEdge
			)
			)
		{
			return false;
		}

		OutCoverageEnd = 1.0f;
		return true;
	}

	return GetContinuousCellEdgeCoverage(
		NeighborPosition,
		FIntVector(
			-EdgeDirection.X,
			-EdgeDirection.Y,
			-EdgeDirection.Z
		),
		OutCoverageStart,
		OutCoverageEnd
	);
}

float ATileMapTerrainActor::GetContinuousTopSurfaceZ(
	float LocalX,
	float LocalY,
	int32 TopGridZ,
	FVector& OutNormal
) const
{
	const float SafeGridSize = FMath::Max(GridSize, 1.0f);
	const float ChamferWidth = FMath::Clamp(
		ContinuousChamferWidth,
		1.0f,
		SafeGridSize * 0.3f
	);
	const float ChamferDepth = FMath::Clamp(
		ContinuousChamferDepth,
		1.0f,
		SafeGridSize * 0.25f
	);
	const float FlatTopZ = TopGridZ * SafeGridSize;
	const int32 BlockLayer = TopGridZ - 1;
	const FVector2D SamplePoint(LocalX, LocalY);
	const int32 CenterGridX =
		FMath::FloorToInt(LocalX / SafeGridSize);
	const int32 CenterGridY =
		FMath::FloorToInt(LocalY / SafeGridSize);

	float GreatestSurfaceDrop = 0.0f;
	FVector2D WinningSurfaceGradient = FVector2D::ZeroVector;

	struct FDropEdge
	{
		FDropEdge(
			const FVector2D& InStart,
			const FVector2D& InEnd,
			const FVector2D& InInwardNormal,
			bool bInRampAdjacent = false,
			const FIntVector& InRampPosition = FIntVector::ZeroValue
		)
			: Start(InStart)
			, End(InEnd)
			, InwardNormal(InInwardNormal)
			, bRampAdjacent(bInRampAdjacent)
			, RampPosition(InRampPosition)
		{
		}

		FVector2D Start;
		FVector2D End;
		FVector2D InwardNormal;
		bool bRampAdjacent;
		FIntVector RampPosition;
	};

	for (int32 XOffset = -2; XOffset <= 2; ++XOffset)
	{
		for (int32 YOffset = -2; YOffset <= 2; ++YOffset)
		{
			const FIntVector SurfaceBlock(
				CenterGridX + XOffset,
				CenterGridY + YOffset,
				BlockLayer
			);

			if (!IsContinuousSurfaceBlock(SurfaceBlock))
			{
				continue;
			}

			const float MinimumX =
				SurfaceBlock.X * SafeGridSize;
			const float MinimumY =
				SurfaceBlock.Y * SafeGridSize;
			const float MaximumX = MinimumX + SafeGridSize;
			const float MaximumY = MinimumY + SafeGridSize;
			const FIntVector AboveSurfaceBlock =
				SurfaceBlock + FIntVector(0, 0, 1);

			if (HasBlock(AboveSurfaceBlock))
			{
				bool bSampleCoveredFromAbove = true;

				// A continuous horizontal cut owns only half of its cell. The
				// complementary half of the support block below remains a real top
				// surface and must still participate in the cliff-lip height query.
				// Treating the upper cell as a full square is what suppressed the
				// chamfer on lower ledges beneath stacked diagonal cuts.
				if (
					IsContinuousSurfaceBlock(AboveSurfaceBlock) &&
					IsContinuousDiagonalTileType(
						GetBlockTileType(AboveSurfaceBlock)
					)
					)
				{
					const float UnitX = FMath::Clamp(
						(LocalX - MinimumX) / SafeGridSize,
						0.0f,
						1.0f
					);
					const float UnitY = FMath::Clamp(
						(LocalY - MinimumY) / SafeGridSize,
						0.0f,
						1.0f
					);
					const float CoverageTolerance = 0.0001f;

					switch (GetBlockRotation(AboveSurfaceBlock) % 4)
					{
					case 0:
						bSampleCoveredFromAbove =
							UnitY <= UnitX + CoverageTolerance;
						break;

					case 1:
						bSampleCoveredFromAbove =
							UnitX + UnitY >= 1.0f - CoverageTolerance;
						break;

					case 2:
						bSampleCoveredFromAbove =
							UnitY >= UnitX - CoverageTolerance;
						break;

					default:
						bSampleCoveredFromAbove =
							UnitX + UnitY <= 1.0f + CoverageTolerance;
						break;
					}
				}

				if (bSampleCoveredFromAbove)
				{
					continue;
				}
			}

			TArray<FDropEdge, TInlineAllocator<8>> DropEdges;

			auto AddExposedAxisDropEdge =
				[&](
					const FIntVector& Direction,
					const FVector2D& Start,
					const FVector2D& End,
					const FVector2D& InwardNormal
				)
				{
					float AboveCoverageStart = 0.0f;
					float AboveCoverageEnd = 0.0f;

					if (
						IsContinuousSurfaceBlock(AboveSurfaceBlock) &&
						IsContinuousDiagonalTileType(
							GetBlockTileType(AboveSurfaceBlock)
						) &&
						GetContinuousCellEdgeCoverage(
							AboveSurfaceBlock,
							Direction,
							AboveCoverageStart,
							AboveCoverageEnd
						) &&
						AboveCoverageStart <= KINDA_SMALL_NUMBER &&
						AboveCoverageEnd >= 1.0f - KINDA_SMALL_NUMBER
						)
					{
						return;
					}

					float CoverageStart = 0.0f;
					float CoverageEnd = 0.0f;
					const FIntVector NeighborPosition =
						SurfaceBlock + Direction;

					if (IsContinuousRampBlock(NeighborPosition))
					{
						float RampStartZ = FlatTopZ;
						float RampEndZ = FlatTopZ;

						if (
							GetContinuousRampBoundaryChamferZ(
								NeighborPosition,
								Start,
								FlatTopZ,
								ChamferDepth,
								RampStartZ
							) &&
							GetContinuousRampBoundaryChamferZ(
								NeighborPosition,
								End,
								FlatTopZ,
								ChamferDepth,
								RampEndZ
							) &&
							FMath::Min(RampStartZ, RampEndZ) <
								FlatTopZ - KINDA_SMALL_NUMBER
							)
						{
							DropEdges.Emplace(
								Start,
								End,
								InwardNormal,
								true,
								NeighborPosition
							);
							return;
						}
					}

					if (
						!GetContinuousTerrainCoverageAcrossEdge(
							SurfaceBlock,
							Direction,
							CoverageStart,
							CoverageEnd
						)
						)
					{
						DropEdges.Emplace(Start, End, InwardNormal);
						return;
					}

					const FVector2D EdgeVector = End - Start;
					const bool bVertical = FMath::IsNearlyZero(EdgeVector.X);
					const float CanonicalStart =
						bVertical
							? (Start.Y - MinimumY) / SafeGridSize
							: (Start.X - MinimumX) / SafeGridSize;
					const float CanonicalEnd =
						bVertical
							? (End.Y - MinimumY) / SafeGridSize
							: (End.X - MinimumX) / SafeGridSize;
					const float CanonicalDelta =
						CanonicalEnd - CanonicalStart;

					if (FMath::Abs(CanonicalDelta) <= SMALL_NUMBER)
					{
						return;
					}

					const float CoveredAlphaA =
						(CoverageStart - CanonicalStart) / CanonicalDelta;
					const float CoveredAlphaB =
						(CoverageEnd - CanonicalStart) / CanonicalDelta;
					const float CoveredStart = FMath::Clamp(
						FMath::Min(CoveredAlphaA, CoveredAlphaB),
						0.0f,
						1.0f
					);
					const float CoveredEnd = FMath::Clamp(
						FMath::Max(CoveredAlphaA, CoveredAlphaB),
						0.0f,
						1.0f
					);

					if (
						CoveredEnd - CoveredStart <=
							KINDA_SMALL_NUMBER
						)
					{
						DropEdges.Emplace(Start, End, InwardNormal);
						return;
					}

					if (CoveredStart > KINDA_SMALL_NUMBER)
					{
						DropEdges.Emplace(
							Start,
							FMath::Lerp(Start, End, CoveredStart),
							InwardNormal
						);
					}

					if (CoveredEnd < 1.0f - KINDA_SMALL_NUMBER)
					{
						DropEdges.Emplace(
							FMath::Lerp(Start, End, CoveredEnd),
							End,
							InwardNormal
						);
					}
				};

			if (
				IsContinuousDiagonalTileType(
					GetBlockTileType(SurfaceBlock)
				)
				)
			{
				FVector2D UnitTriangle[3];
				float DiagonalFraction = 1.0f;
				GetContinuousDiagonalFraction(
					GetBlockTileType(SurfaceBlock),
					DiagonalFraction
				);

				switch (GetBlockRotation(SurfaceBlock) % 4)
				{
				case 0:
					UnitTriangle[0] = FVector2D(0, 0);
					UnitTriangle[1] = FVector2D(1, 0);
					UnitTriangle[2] = FVector2D(1, DiagonalFraction);
					break;

				case 1:
					UnitTriangle[0] = FVector2D(1, 0);
					UnitTriangle[1] = FVector2D(1, 1);
					UnitTriangle[2] = FVector2D(
						1.0f - DiagonalFraction,
						1
					);
					break;

				case 2:
					UnitTriangle[0] = FVector2D(1, 1);
					UnitTriangle[1] = FVector2D(0, 1);
					UnitTriangle[2] = FVector2D(
						0,
						1.0f - DiagonalFraction
					);
					break;

				default:
					UnitTriangle[0] = FVector2D(0, 1);
					UnitTriangle[1] = FVector2D(0, 0);
					UnitTriangle[2] = FVector2D(DiagonalFraction, 0);
					break;
				}

				for (int32 EdgeIndex = 0; EdgeIndex < 3; ++EdgeIndex)
				{
					const FVector2D Start =
						FVector2D(MinimumX, MinimumY) +
						(UnitTriangle[EdgeIndex] * SafeGridSize);
					const FVector2D End =
						FVector2D(MinimumX, MinimumY) +
						(
							UnitTriangle[(EdgeIndex + 1) % 3] *
							SafeGridSize
						);
					const FVector2D EdgeVector = End - Start;
					const FVector2D OutwardNormal =
						FVector2D(EdgeVector.Y, -EdgeVector.X)
						.GetSafeNormal();
					const bool bAxisAligned =
						FMath::IsNearlyZero(EdgeVector.X) ||
						FMath::IsNearlyZero(EdgeVector.Y);

					if (bAxisAligned)
					{
						const FIntVector EdgeDirection(
							FMath::RoundToInt(OutwardNormal.X),
							FMath::RoundToInt(OutwardNormal.Y),
							0
						);
						AddExposedAxisDropEdge(
							EdgeDirection,
							Start,
							End,
							-OutwardNormal
						);
					}
					else
					{
						DropEdges.Emplace(
							Start,
							End,
							-OutwardNormal
						);
					}
				}
			}
			else
			{
				const struct
				{
					FIntVector Direction;
					FVector2D Start;
					FVector2D End;
					FVector2D InwardNormal;
				} SquareEdges[4] =
				{
					{
						FIntVector(1, 0, 0),
						FVector2D(MaximumX, MinimumY),
						FVector2D(MaximumX, MaximumY),
						FVector2D(-1.0f, 0.0f)
					},
					{
						FIntVector(-1, 0, 0),
						FVector2D(MinimumX, MinimumY),
						FVector2D(MinimumX, MaximumY),
						FVector2D(1.0f, 0.0f)
					},
					{
						FIntVector(0, 1, 0),
						FVector2D(MinimumX, MaximumY),
						FVector2D(MaximumX, MaximumY),
						FVector2D(0.0f, -1.0f)
					},
					{
						FIntVector(0, -1, 0),
						FVector2D(MinimumX, MinimumY),
						FVector2D(MaximumX, MinimumY),
						FVector2D(0.0f, 1.0f)
					}
				};

				for (const auto& SquareEdge : SquareEdges)
				{
					AddExposedAxisDropEdge(
						SquareEdge.Direction,
						SquareEdge.Start,
						SquareEdge.End,
						SquareEdge.InwardNormal
					);
				}
			}

			for (const FDropEdge& DropEdge : DropEdges)
			{
				const FVector2D EdgeVector =
					DropEdge.End - DropEdge.Start;
				const float EdgeLengthSquared =
					EdgeVector.SizeSquared();
				const float EdgeAlpha =
					EdgeLengthSquared > SMALL_NUMBER
					? FMath::Clamp(
						FVector2D::DotProduct(
							SamplePoint - DropEdge.Start,
							EdgeVector
						) / EdgeLengthSquared,
						0.0f,
						1.0f
					)
					: 0.0f;
				const FVector2D ClosestPoint =
					DropEdge.Start + (EdgeVector * EdgeAlpha);
				const FVector2D FromEdge =
					SamplePoint - ClosestPoint;
				const float DistanceSquared =
					FromEdge.SizeSquared();
				const float Distance = FMath::Sqrt(DistanceSquared);

				if (Distance >= ChamferWidth)
				{
					continue;
				}

				float EffectiveChamferDepth = ChamferDepth;
				float DepthDerivativeAlongEdge = 0.0f;

				if (DropEdge.bRampAdjacent)
				{
					float RampSurfaceZ = FlatTopZ;

					if (
						!GetContinuousRampBoundaryChamferZ(
							DropEdge.RampPosition,
							ClosestPoint,
							FlatTopZ,
							ChamferDepth,
							RampSurfaceZ
						)
						)
					{
						continue;
					}

					EffectiveChamferDepth = FMath::Clamp(
						FlatTopZ - RampSurfaceZ,
						0.0f,
						ChamferDepth
					);

					const float EdgeLength = FMath::Sqrt(EdgeLengthSquared);
					const float DerivativeAlpha = EdgeLength > SMALL_NUMBER
						? FMath::Min(1.0f / EdgeLength, 0.01f)
						: 0.0f;
					const float AlphaBefore = FMath::Max(
						0.0f,
						EdgeAlpha - DerivativeAlpha
					);
					const float AlphaAfter = FMath::Min(
						1.0f,
						EdgeAlpha + DerivativeAlpha
					);
					const float DerivativeDistance =
						(AlphaAfter - AlphaBefore) * EdgeLength;

					if (DerivativeDistance > SMALL_NUMBER)
					{
						float RampBeforeZ = RampSurfaceZ;
						float RampAfterZ = RampSurfaceZ;
						GetContinuousRampBoundaryChamferZ(
							DropEdge.RampPosition,
							DropEdge.Start +
								(EdgeVector * AlphaBefore),
							FlatTopZ,
							ChamferDepth,
							RampBeforeZ
						);
						GetContinuousRampBoundaryChamferZ(
							DropEdge.RampPosition,
							DropEdge.Start +
								(EdgeVector * AlphaAfter),
							FlatTopZ,
							ChamferDepth,
							RampAfterZ
						);
						const float DepthBefore = FMath::Clamp(
							FlatTopZ - RampBeforeZ,
							0.0f,
							ChamferDepth
						);
						const float DepthAfter = FMath::Clamp(
							FlatTopZ - RampAfterZ,
							0.0f,
							ChamferDepth
						);
						DepthDerivativeAlongEdge =
							(DepthAfter - DepthBefore) /
							DerivativeDistance;
					}
				}

				const float SurfaceAlpha = FMath::Clamp(
					Distance / ChamferWidth,
					0.0f,
					1.0f
				);
				const float SurfaceDrop =
					EffectiveChamferDepth * (1.0f - SurfaceAlpha);

				if (SurfaceDrop <= GreatestSurfaceDrop + SMALL_NUMBER)
				{
					continue;
				}

				const FVector2D InwardDirection =
					DistanceSquared > KINDA_SMALL_NUMBER
						? FromEdge.GetSafeNormal()
						: DropEdge.InwardNormal;
				const FVector2D EdgeTangent =
					EdgeVector.GetSafeNormal();

				GreatestSurfaceDrop = SurfaceDrop;
				WinningSurfaceGradient =
					(InwardDirection *
						(EffectiveChamferDepth / ChamferWidth)) -
					(EdgeTangent *
						DepthDerivativeAlongEdge *
						(1.0f - SurfaceAlpha));
			}
		}
	}

	OutNormal = FVector::UpVector;

	if (GreatestSurfaceDrop <= SMALL_NUMBER)
	{
		return FlatTopZ;
	}

	OutNormal = FVector(
		-WinningSurfaceGradient.X,
		-WinningSurfaceGradient.Y,
		1.0f
	).GetSafeNormal();

	return FlatTopZ - GreatestSurfaceDrop;
}

bool ATileMapTerrainActor::IsVisiblePaintedPathBlock(
	const FIntVector& GridPosition
) const
{
	return
		PaintedPathLookup.Contains(GridPosition) &&
		IsContinuousSurfaceBlock(GridPosition) &&
		!HasBlock(GridPosition + FIntVector(0, 0, 1));
}

float ATileMapTerrainActor::GetContinuousPathMask(
	float LocalX,
	float LocalY,
	int32 GridZ,
	float BlendWidth
) const
{
	if (PaintedPathLookup.Num() == 0)
	{
		return 0.0f;
	}

	const float SafeGridSize = FMath::Max(GridSize, 1.0f);
	const float SafeBlendWidth = FMath::Clamp(
		BlendWidth,
		0.0f,
		SafeGridSize * 0.5f
	);
	const FVector2D SamplePoint(LocalX, LocalY);
	const int32 CenterX = FMath::FloorToInt(LocalX / SafeGridSize);
	const int32 CenterY = FMath::FloorToInt(LocalY / SafeGridSize);
	const int32 SearchRadius = FMath::Max(
		1,
		FMath::CeilToInt(SafeBlendWidth / SafeGridSize) + 1
	);
	bool bInsidePaintedArea = false;
	float ClosestBoundaryDistanceSquared =
		TNumericLimits<float>::Max();

	auto AccumulateBoundaryDistance =
		[&](const FVector2D& Start, const FVector2D& End)
		{
			const FVector2D Edge = End - Start;
			const float EdgeLengthSquared = Edge.SizeSquared();
			const float EdgeAlpha =
				EdgeLengthSquared > SMALL_NUMBER
				? FMath::Clamp(
					FVector2D::DotProduct(SamplePoint - Start, Edge) /
						EdgeLengthSquared,
					0.0f,
					1.0f
				)
				: 0.0f;
			const FVector2D ClosestPoint = Start + (Edge * EdgeAlpha);

			ClosestBoundaryDistanceSquared = FMath::Min(
				ClosestBoundaryDistanceSquared,
				(SamplePoint - ClosestPoint).SizeSquared()
			);
		};

	for (int32 XOffset = -SearchRadius;
		XOffset <= SearchRadius;
		++XOffset)
	{
		for (int32 YOffset = -SearchRadius;
			YOffset <= SearchRadius;
			++YOffset)
		{
			const FIntVector PathPosition(
				CenterX + XOffset,
				CenterY + YOffset,
				GridZ
			);

			if (!IsVisiblePaintedPathBlock(PathPosition))
			{
				continue;
			}

			const float MinimumX = PathPosition.X * SafeGridSize;
			const float MinimumY = PathPosition.Y * SafeGridSize;
			const float MaximumX = MinimumX + SafeGridSize;
			const float MaximumY = MinimumY + SafeGridSize;

			bInsidePaintedArea =
				bInsidePaintedArea ||
				(
					LocalX >= MinimumX - KINDA_SMALL_NUMBER &&
					LocalX <= MaximumX + KINDA_SMALL_NUMBER &&
					LocalY >= MinimumY - KINDA_SMALL_NUMBER &&
					LocalY <= MaximumY + KINDA_SMALL_NUMBER
				);

			if (!IsVisiblePaintedPathBlock(
				PathPosition + FIntVector(-1, 0, 0)))
			{
				AccumulateBoundaryDistance(
					FVector2D(MinimumX, MinimumY),
					FVector2D(MinimumX, MaximumY)
				);
			}

			if (!IsVisiblePaintedPathBlock(
				PathPosition + FIntVector(1, 0, 0)))
			{
				AccumulateBoundaryDistance(
					FVector2D(MaximumX, MinimumY),
					FVector2D(MaximumX, MaximumY)
				);
			}

			if (!IsVisiblePaintedPathBlock(
				PathPosition + FIntVector(0, -1, 0)))
			{
				AccumulateBoundaryDistance(
					FVector2D(MinimumX, MinimumY),
					FVector2D(MaximumX, MinimumY)
				);
			}

			if (!IsVisiblePaintedPathBlock(
				PathPosition + FIntVector(0, 1, 0)))
			{
				AccumulateBoundaryDistance(
					FVector2D(MinimumX, MaximumY),
					FVector2D(MaximumX, MaximumY)
				);
			}
		}
	}

	if (SafeBlendWidth <= KINDA_SMALL_NUMBER)
	{
		return bInsidePaintedArea ? 1.0f : 0.0f;
	}

	if (ClosestBoundaryDistanceSquared == TNumericLimits<float>::Max())
	{
		return bInsidePaintedArea ? 1.0f : 0.0f;
	}

	const float Distance = FMath::Sqrt(ClosestBoundaryDistanceSquared);
	const float SignedDistance = bInsidePaintedArea ? Distance : -Distance;
	const float Alpha = FMath::Clamp(
		(SignedDistance + SafeBlendWidth) /
			(2.0f * SafeBlendWidth),
		0.0f,
		1.0f
	);

	return Alpha * Alpha * (3.0f - (2.0f * Alpha));
}

int32 ATileMapTerrainActor::GetBlockTileType(
	const FIntVector& GridPosition
) const
{
	const int32 BlockIndex =
		FindBlockIndex(GridPosition);

	if (!BlockTileTypes.IsValidIndex(BlockIndex))
	{
		return 0;
	}

	return ClampTileType(BlockTileTypes[BlockIndex]);
}

uint8 ATileMapTerrainActor::GetBlockRotation(
	const FIntVector& GridPosition
) const
{
	const int32 BlockIndex =
		FindBlockIndex(GridPosition);

	if (!BlockRotations.IsValidIndex(BlockIndex))
	{
		return 0;
	}

	return BlockRotations[BlockIndex] % 4;
}

FTransform ATileMapTerrainActor::GetBlockLocalTransform(
	const FIntVector& GridPosition
) const
{
	const int32 TileType =
		GetBlockTileType(GridPosition);

	const uint8 QuarterTurns =
		GetBlockRotation(GridPosition);

	const FTileMapTileDefinition* Definition =
		GetTileDefinition(TileType);

	const float GridScale =
		FMath::Max(GridSize, 1.0f) / 100.0f;

	const FVector DefinitionScale =
		Definition
		? Definition->MeshScale
		: FVector::OneVector;

	const FVector TilePivotOffset =
		Definition
		? Definition->PivotOffset
		: FVector::ZeroVector;

	const FQuat Rotation(
		FVector::UpVector,
		FMath::DegreesToRadians(
			static_cast<float>(QuarterTurns) * 90.0f
		)
	);

	const FVector Location =
		GridToLocal(GridPosition) +
		Rotation.RotateVector(
			TilePivotOffset * GridScale
		);

	return FTransform(
		Rotation,
		Location,
		DefinitionScale * GridScale
	);
}

bool ATileMapTerrainActor::AddBlock(
	const FIntVector& GridPosition,
	int32 TileType,
	uint8 QuarterTurns
)
{
	if (HasBlock(GridPosition))
	{
		return false;
	}

	NormalizeBlockMetadata();

	const int32 NewIndex =
		OccupiedBlocks.Add(GridPosition);

	BlockTileTypes.Add(
		ClampTileType(TileType)
	);

	BlockRotations.Add(QuarterTurns % 4);

	OccupancyLookup.Add(GridPosition);
	BlockIndexLookup.Add(GridPosition, NewIndex);

	const FIntVector ChunkCoordinate =
		GetChunkCoordinate(GridPosition);

	ChunkBlocksLookup.FindOrAdd(
		ChunkCoordinate
	).Add(GridPosition);

	RefreshAutoTilesAround(GridPosition);
	return true;
}

bool ATileMapTerrainActor::AddBlocks(
	const TArray<FIntVector>& GridPositions,
	const TArray<int32>& TileTypes,
	const TArray<uint8>& QuarterTurns
)
{
	if (
		GridPositions.Num() == 0 ||
		GridPositions.Num() != TileTypes.Num() ||
		GridPositions.Num() != QuarterTurns.Num()
		)
	{
		return false;
	}

	TSet<FIntVector> UniquePositions;

	for (const FIntVector& GridPosition : GridPositions)
	{
		if (
			HasBlock(GridPosition) ||
			UniquePositions.Contains(GridPosition)
			)
		{
			return false;
		}

		UniquePositions.Add(GridPosition);
	}

	NormalizeBlockMetadata();
	OccupiedBlocks.Reserve(
		OccupiedBlocks.Num() + GridPositions.Num()
	);
	BlockTileTypes.Reserve(
		BlockTileTypes.Num() + TileTypes.Num()
	);
	BlockRotations.Reserve(
		BlockRotations.Num() + QuarterTurns.Num()
	);

	for (int32 Index = 0; Index < GridPositions.Num(); ++Index)
	{
		OccupiedBlocks.Add(GridPositions[Index]);
		BlockTileTypes.Add(
			ClampTileType(TileTypes[Index])
		);
		BlockRotations.Add(QuarterTurns[Index] % 4);
	}

	RebuildDerivedLookups();

	TSet<FIntVector> PositionsToRefresh;

	for (const FIntVector& AddedPosition : GridPositions)
	{
		for (int32 ZOffset = -1; ZOffset <= 1; ++ZOffset)
		{
			for (int32 XOffset = -1; XOffset <= 1; ++XOffset)
			{
				for (int32 YOffset = -1; YOffset <= 1; ++YOffset)
				{
					PositionsToRefresh.Add(
						AddedPosition +
						FIntVector(XOffset, YOffset, ZOffset)
					);
				}
			}
		}
	}

	TSet<FIntVector> ChangedPositions = PositionsToRefresh;

	for (const FIntVector& Position : PositionsToRefresh)
	{
		ResolveAutoTileAt(Position, ChangedPositions);
	}

	RebuildChunksForPositions(ChangedPositions);
	return true;
}

bool ATileMapTerrainActor::RemoveBlock(
	const FIntVector& GridPosition
)
{
	const int32 BlockIndex =
		FindBlockIndex(GridPosition);

	if (BlockIndex == INDEX_NONE)
	{
		return false;
	}

	const FIntVector ChunkCoordinate =
		GetChunkCoordinate(GridPosition);

	OccupiedBlocks.RemoveAt(BlockIndex);

	if (BlockTileTypes.IsValidIndex(BlockIndex))
	{
		BlockTileTypes.RemoveAt(BlockIndex);
	}

	if (BlockRotations.IsValidIndex(BlockIndex))
	{
		BlockRotations.RemoveAt(BlockIndex);
	}

	PaintedPathBlocks.RemoveSingle(GridPosition);
	PaintedPathLookup.Remove(GridPosition);
	ModularTerrainBlocks.RemoveSingle(GridPosition);
	ModularTerrainBlockLookup.Remove(GridPosition);

	OccupancyLookup.Remove(GridPosition);
	BlockIndexLookup.Remove(GridPosition);

	for (
		int32 Index = BlockIndex;
		Index < OccupiedBlocks.Num();
		++Index
		)
	{
		BlockIndexLookup.Add(
			OccupiedBlocks[Index],
			Index
		);
	}

	TArray<FIntVector>* ChunkBlocks =
		ChunkBlocksLookup.Find(ChunkCoordinate);

	if (ChunkBlocks)
	{
		ChunkBlocks->RemoveSingle(GridPosition);

		if (ChunkBlocks->Num() == 0)
		{
			ChunkBlocksLookup.Remove(ChunkCoordinate);
		}
	}

	RefreshAutoTilesAround(GridPosition);
	return true;
}

bool ATileMapTerrainActor::RemoveBlocks(
	const TArray<FIntVector>& GridPositions
)
{
	if (GridPositions.Num() == 0)
	{
		return false;
	}

	TSet<FIntVector> UniquePositions;
	TArray<int32> BlockIndices;

	for (const FIntVector& GridPosition : GridPositions)
	{
		if (UniquePositions.Contains(GridPosition))
		{
			continue;
		}

		const int32 BlockIndex = FindBlockIndex(GridPosition);

		if (BlockIndex == INDEX_NONE)
		{
			continue;
		}

		UniquePositions.Add(GridPosition);
		BlockIndices.Add(BlockIndex);
	}

	if (BlockIndices.Num() == 0)
	{
		return false;
	}

	// Removing from the highest index down preserves all remaining indices.
	BlockIndices.Sort(
		[](int32 Left, int32 Right)
		{
			return Left > Right;
		}
	);

	for (const int32 BlockIndex : BlockIndices)
	{
		OccupiedBlocks.RemoveAt(BlockIndex);

		if (BlockTileTypes.IsValidIndex(BlockIndex))
		{
			BlockTileTypes.RemoveAt(BlockIndex);
		}

		if (BlockRotations.IsValidIndex(BlockIndex))
		{
			BlockRotations.RemoveAt(BlockIndex);
		}
	}

	for (const FIntVector& RemovedPosition : UniquePositions)
	{
		PaintedPathBlocks.RemoveSingle(RemovedPosition);
	}

	RebuildDerivedLookups();

	TSet<FIntVector> PositionsToRefresh;

	for (const FIntVector& RemovedPosition : UniquePositions)
	{
		for (int32 ZOffset = -1; ZOffset <= 1; ++ZOffset)
		{
			for (int32 XOffset = -1; XOffset <= 1; ++XOffset)
			{
				for (int32 YOffset = -1; YOffset <= 1; ++YOffset)
				{
					PositionsToRefresh.Add(
						RemovedPosition +
						FIntVector(XOffset, YOffset, ZOffset)
					);
				}
			}
		}
	}

	TSet<FIntVector> ChangedPositions = PositionsToRefresh;

	for (const FIntVector& Position : PositionsToRefresh)
	{
		ResolveAutoTileAt(Position, ChangedPositions);
	}

	RebuildChunksForPositions(ChangedPositions);
	return true;
}

bool ATileMapTerrainActor::MoveBlock(
	const FIntVector& FromGridPosition,
	const FIntVector& ToGridPosition
)
{
	if (FromGridPosition == ToGridPosition)
	{
		return HasBlock(FromGridPosition);
	}

	if (
		!HasBlock(FromGridPosition) ||
		HasBlock(ToGridPosition)
		)
	{
		return false;
	}

	const int32 BlockIndex =
		FindBlockIndex(FromGridPosition);

	if (BlockIndex == INDEX_NONE)
	{
		return false;
	}

	OccupiedBlocks[BlockIndex] = ToGridPosition;

	if (HasPaintedPath(FromGridPosition))
	{
		PaintedPathBlocks.RemoveSingle(FromGridPosition);
		PaintedPathBlocks.Add(ToGridPosition);
		PaintedPathLookup.Remove(FromGridPosition);
		PaintedPathLookup.Add(ToGridPosition);
	}

	if (IsBlockExcludedFromContinuousTerrain(FromGridPosition))
	{
		ModularTerrainBlocks.RemoveSingle(FromGridPosition);
		ModularTerrainBlocks.Add(ToGridPosition);
		ModularTerrainBlockLookup.Remove(FromGridPosition);
		ModularTerrainBlockLookup.Add(ToGridPosition);
	}

	OccupancyLookup.Remove(FromGridPosition);
	OccupancyLookup.Add(ToGridPosition);

	BlockIndexLookup.Remove(FromGridPosition);
	BlockIndexLookup.Add(ToGridPosition, BlockIndex);

	const FIntVector OldChunk =
		GetChunkCoordinate(FromGridPosition);

	const FIntVector NewChunk =
		GetChunkCoordinate(ToGridPosition);

	TArray<FIntVector>* OldChunkBlocks =
		ChunkBlocksLookup.Find(OldChunk);

	if (OldChunkBlocks)
	{
		OldChunkBlocks->RemoveSingle(FromGridPosition);

		if (OldChunkBlocks->Num() == 0)
		{
			ChunkBlocksLookup.Remove(OldChunk);
		}
	}

	ChunkBlocksLookup.FindOrAdd(
		NewChunk
	).Add(ToGridPosition);

	RefreshAutoTilesAround(FromGridPosition);
	RefreshAutoTilesAround(ToGridPosition);

	return true;
}

bool ATileMapTerrainActor::SetBlockTileType(
	const FIntVector& GridPosition,
	int32 TileType
)
{
	const int32 BlockIndex =
		FindBlockIndex(GridPosition);

	if (!BlockTileTypes.IsValidIndex(BlockIndex))
	{
		return false;
	}

	const int32 SafeTileType =
		ClampTileType(TileType);

	if (BlockTileTypes[BlockIndex] == SafeTileType)
	{
		return false;
	}

	BlockTileTypes[BlockIndex] = SafeTileType;
	RefreshAutoTilesAround(GridPosition);
	return true;
}

bool ATileMapTerrainActor::RotateBlock(
	const FIntVector& GridPosition,
	int32 QuarterTurnDelta
)
{
	const int32 BlockIndex =
		FindBlockIndex(GridPosition);

	if (!BlockRotations.IsValidIndex(BlockIndex))
	{
		return false;
	}

	const int32 CurrentRotation =
		static_cast<int32>(BlockRotations[BlockIndex]);

	BlockRotations[BlockIndex] =
		static_cast<uint8>(
			(CurrentRotation + QuarterTurnDelta % 4 + 4) % 4
		);

	TSet<FIntVector> PositionsToRebuild;

	for (int32 XOffset = -1; XOffset <= 1; ++XOffset)
	{
		for (int32 YOffset = -1; YOffset <= 1; ++YOffset)
		{
			PositionsToRebuild.Add(
				GridPosition + FIntVector(XOffset, YOffset, 0)
			);
		}
	}

	RebuildChunksForPositions(PositionsToRebuild);
	return true;
}

bool ATileMapTerrainActor::SetBlocksVisual(
	const TArray<FIntVector>& GridPositions,
	const TArray<int32>& TileTypes,
	uint8 QuarterTurns
)
{
	if (
		GridPositions.Num() == 0 ||
		GridPositions.Num() != TileTypes.Num()
		)
	{
		return false;
	}

	TArray<int32> BlockIndices;
	BlockIndices.Reserve(GridPositions.Num());

	// Validate the complete run before changing anything.
	for (const FIntVector& GridPosition : GridPositions)
	{
		const int32 BlockIndex = FindBlockIndex(GridPosition);

		if (
			!BlockTileTypes.IsValidIndex(BlockIndex) ||
			!BlockRotations.IsValidIndex(BlockIndex)
			)
		{
			return false;
		}

		BlockIndices.Add(BlockIndex);
	}

	const uint8 SafeRotation = QuarterTurns % 4;
	TSet<FIntVector> PositionsToRebuild;
	bool bChanged = false;

	for (int32 ItemIndex = 0;
		ItemIndex < GridPositions.Num();
		++ItemIndex)
	{
		const int32 BlockIndex = BlockIndices[ItemIndex];
		const int32 SafeTileType = ClampTileType(TileTypes[ItemIndex]);

		if (
			BlockTileTypes[BlockIndex] == SafeTileType &&
			BlockRotations[BlockIndex] == SafeRotation
			)
		{
			continue;
		}

		BlockTileTypes[BlockIndex] = SafeTileType;
		BlockRotations[BlockIndex] = SafeRotation;
		for (int32 ZOffset = -1; ZOffset <= 1; ++ZOffset)
		{
			for (int32 XOffset = -1; XOffset <= 1; ++XOffset)
			{
				for (int32 YOffset = -1; YOffset <= 1; ++YOffset)
				{
					PositionsToRebuild.Add(
						GridPositions[ItemIndex] +
						FIntVector(XOffset, YOffset, ZOffset)
					);
				}
			}
		}
		bChanged = true;
	}

	if (bChanged)
	{
		RebuildChunksForPositions(PositionsToRebuild);
	}

	return bChanged;
}

void ATileMapTerrainActor::ClearBlocks()
{
	OccupiedBlocks.Reset();
	BlockTileTypes.Reset();
	BlockRotations.Reset();
	PaintedPathBlocks.Reset();
	ModularTerrainBlocks.Reset();
	OccupancyLookup.Reset();
	PaintedPathLookup.Reset();
	ModularTerrainBlockLookup.Reset();
	BlockIndexLookup.Reset();
	ChunkBlocksLookup.Reset();
	DestroyAllChunkComponents();
}

void ATileMapTerrainActor::CreateTestGrid(
	int32 GridWidth,
	int32 GridHeight
)
{
	OccupiedBlocks.Reset();
	BlockTileTypes.Reset();
	BlockRotations.Reset();
	PaintedPathBlocks.Reset();
	ModularTerrainBlocks.Reset();

	GridWidth = FMath::Max(GridWidth, 1);
	GridHeight = FMath::Max(GridHeight, 1);

	for (int32 X = 0; X < GridWidth; ++X)
	{
		for (int32 Y = 0; Y < GridHeight; ++Y)
		{
			int32 HighestLayer = 0;

			if (
				X >= 3 && X <= 6 &&
				Y >= 3 && Y <= 6
				)
			{
				HighestLayer = 1;
			}

			if (
				X >= 4 && X <= 5 &&
				Y >= 4 && Y <= 5
				)
			{
				HighestLayer = 2;
			}

			for (
				int32 Layer = 0;
				Layer <= HighestLayer;
				++Layer
				)
			{
				OccupiedBlocks.Add(
					FIntVector(X, Y, Layer)
				);

				BlockTileTypes.Add(0);
				BlockRotations.Add(0);
			}
		}
	}

	RebuildAllChunks();
}

bool ATileMapTerrainActor::GetGridPositionFromHit(
	const FHitResult& HitResult,
	FIntVector& OutGridPosition
) const
{
	if (HitResult.GetActor() != this)
	{
		return false;
	}

	const FVector SafeNormal =
		HitResult.ImpactNormal.GetSafeNormal();

	const float NudgeDistance =
		FMath::Max(GridSize * 0.01f, 0.1f);

	const FVector InsidePoint =
		HitResult.ImpactPoint -
		(SafeNormal * NudgeDistance);

	OutGridPosition = WorldToGrid(InsidePoint);

	if (HasBlock(OutGridPosition))
	{
		return true;
	}

	if (!bUseContinuousTerrainPrototype)
	{
		return false;
	}

	// The continuous top-edge chamfer can place a collision triangle directly
	// on a logical cell boundary. If the normal nudge lands in the neighboring
	// empty cell, recover the closest occupied source block for cursor tools.
	const FVector LocalImpactPoint =
		GetActorTransform().InverseTransformPosition(
			HitResult.ImpactPoint
		);
	const float SafeGridSize = FMath::Max(GridSize, 1.0f);
	float BestDistanceSquared = TNumericLimits<float>::Max();
	FIntVector BestPosition = OutGridPosition;

	for (int32 ZOffset = -1; ZOffset <= 1; ++ZOffset)
	{
		for (int32 XOffset = -1; XOffset <= 1; ++XOffset)
		{
			for (int32 YOffset = -1; YOffset <= 1; ++YOffset)
			{
				const FIntVector Candidate =
					OutGridPosition +
					FIntVector(XOffset, YOffset, ZOffset);

				if (!HasBlock(Candidate))
				{
					continue;
				}

				const FVector Minimum(
					Candidate.X * SafeGridSize,
					Candidate.Y * SafeGridSize,
					Candidate.Z * SafeGridSize
				);
				const FVector Maximum =
					Minimum + FVector(SafeGridSize);
				const FVector ClosestPoint(
					FMath::Clamp(
						LocalImpactPoint.X,
						Minimum.X,
						Maximum.X
					),
					FMath::Clamp(
						LocalImpactPoint.Y,
						Minimum.Y,
						Maximum.Y
					),
					FMath::Clamp(
						LocalImpactPoint.Z,
						Minimum.Z,
						Maximum.Z
					)
				);
				const float DistanceSquared =
					FVector::DistSquared(
						LocalImpactPoint,
						ClosestPoint
					);

				if (DistanceSquared < BestDistanceSquared)
				{
					BestDistanceSquared = DistanceSquared;
					BestPosition = Candidate;
				}
			}
		}
	}

	if (BestDistanceSquared < TNumericLimits<float>::Max())
	{
		OutGridPosition = BestPosition;
		return true;
	}

	return false;
}

bool ATileMapTerrainActor::GetAdjacentGridPositionFromHit(
	const FHitResult& HitResult,
	FIntVector& OutGridPosition
) const
{
	if (HitResult.GetActor() != this)
	{
		return false;
	}

	FIntVector HitGridPosition;

	if (
		!GetGridPositionFromHit(
			HitResult,
			HitGridPosition
		)
		)
	{
		return false;
	}

	const FVector LocalNormal =
		GetActorTransform()
		.InverseTransformVectorNoScale(
			HitResult.ImpactNormal
		)
		.GetSafeNormal();

	const FVector AbsoluteNormal =
		LocalNormal.GetAbs();

	FIntVector NeighborOffset(0, 0, 0);

	if (
		AbsoluteNormal.X >= AbsoluteNormal.Y &&
		AbsoluteNormal.X >= AbsoluteNormal.Z
		)
	{
		NeighborOffset.X = LocalNormal.X >= 0.0f ? 1 : -1;
	}
	else if (AbsoluteNormal.Y >= AbsoluteNormal.Z)
	{
		NeighborOffset.Y = LocalNormal.Y >= 0.0f ? 1 : -1;
	}
	else
	{
		NeighborOffset.Z = LocalNormal.Z >= 0.0f ? 1 : -1;
	}

	OutGridPosition = HitGridPosition + NeighborOffset;
	return true;
}

void ATileMapTerrainActor::NormalizeBlockMetadata()
{
	BlockTileTypes.SetNum(OccupiedBlocks.Num());
	BlockRotations.SetNum(OccupiedBlocks.Num());

	for (int32 Index = 0; Index < OccupiedBlocks.Num(); ++Index)
	{
		BlockTileTypes[Index] =
			ClampTileType(BlockTileTypes[Index]);

		BlockRotations[Index] %= 4;
	}

	TSet<FIntVector> OccupiedSourceBlocks;

	for (const FIntVector& GridPosition : OccupiedBlocks)
	{
		OccupiedSourceBlocks.Add(GridPosition);
	}

	TSet<FIntVector> UniquePathBlocks;

	for (int32 Index = PaintedPathBlocks.Num() - 1; Index >= 0; --Index)
	{
		const FIntVector& GridPosition = PaintedPathBlocks[Index];

		if (
			!OccupiedSourceBlocks.Contains(GridPosition) ||
			UniquePathBlocks.Contains(GridPosition)
			)
		{
			PaintedPathBlocks.RemoveAt(Index);
			continue;
		}

		UniquePathBlocks.Add(GridPosition);
	}

	TSet<FIntVector> UniqueModularTerrainBlocks;

	for (
		int32 Index = ModularTerrainBlocks.Num() - 1;
		Index >= 0;
		--Index
		)
	{
		const FIntVector& GridPosition =
			ModularTerrainBlocks[Index];

		if (
			!OccupiedSourceBlocks.Contains(GridPosition) ||
			UniqueModularTerrainBlocks.Contains(GridPosition)
			)
		{
			ModularTerrainBlocks.RemoveAt(Index);
			continue;
		}

		UniqueModularTerrainBlocks.Add(GridPosition);
	}
}

void ATileMapTerrainActor::RebuildDerivedLookups()
{
	NormalizeBlockMetadata();

	OccupancyLookup.Reset();
	PaintedPathLookup.Reset();
	ModularTerrainBlockLookup.Reset();
	BlockIndexLookup.Reset();
	ChunkBlocksLookup.Reset();

	for (
		int32 Index = 0;
		Index < OccupiedBlocks.Num();
		++Index
		)
	{
		const FIntVector& GridPosition =
			OccupiedBlocks[Index];

		if (OccupancyLookup.Contains(GridPosition))
		{
			continue;
		}

		OccupancyLookup.Add(GridPosition);
		BlockIndexLookup.Add(GridPosition, Index);

		ChunkBlocksLookup.FindOrAdd(
			GetChunkCoordinate(GridPosition)
		).Add(GridPosition);
	}

	for (const FIntVector& GridPosition : PaintedPathBlocks)
	{
		PaintedPathLookup.Add(GridPosition);
	}

	for (const FIntVector& GridPosition : ModularTerrainBlocks)
	{
		ModularTerrainBlockLookup.Add(GridPosition);
	}
}

UProceduralMeshComponent*
ATileMapTerrainActor::FindOrCreateContinuousChunkComponent(
	const FIntVector& ChunkCoordinate,
	bool bPathOverlayOnly
)
{
	TMap<FIntVector, UProceduralMeshComponent*>& ComponentLookup =
		bPathOverlayOnly
		? PathOverlayChunkComponentLookup
		: ContinuousChunkComponentLookup;
	UProceduralMeshComponent** ExistingComponent =
		ComponentLookup.Find(ChunkCoordinate);

	if (ExistingComponent && IsValid(*ExistingComponent))
	{
		return *ExistingComponent;
	}

	const FString ComponentNameString =
		bPathOverlayOnly
		? FString::Printf(
			TEXT("TilePathOverlayChunk_%d_%d_%d"),
			ChunkCoordinate.X,
			ChunkCoordinate.Y,
			ChunkCoordinate.Z
		)
		: FString::Printf(
			TEXT("TileContinuousChunk_%d_%d_%d"),
			ChunkCoordinate.X,
			ChunkCoordinate.Y,
			ChunkCoordinate.Z
		);

	const FName ComponentName =
		MakeUniqueObjectName(
			this,
			UProceduralMeshComponent::StaticClass(),
			FName(*ComponentNameString)
		);

	UProceduralMeshComponent* NewComponent =
		NewObject<UProceduralMeshComponent>(
			this,
			ComponentName,
			RF_Transient |
			RF_DuplicateTransient |
			RF_TextExportTransient
		);

	if (!NewComponent)
	{
		return nullptr;
	}

	// Like the HISM components, this is a derived render/collision cache. The
	// serialized block arrays remain the only transactional terrain data.
	NewComponent->ClearFlags(RF_Transactional);
	NewComponent->CreationMethod = EComponentCreationMethod::Instance;
	NewComponent->SetupAttachment(SceneRoot);
	NewComponent->SetRelativeTransform(
		bPathOverlayOnly
			? FTransform(
				FQuat::Identity,
				FVector(0.0f, 0.0f, 0.25f),
				FVector::OneVector
			)
			: FTransform::Identity
	);
	NewComponent->SetMobility(EComponentMobility::Movable);
	NewComponent->SetGenerateOverlapEvents(false);
	NewComponent->SetCollisionProfileName(TEXT("BlockAll"));
	NewComponent->SetCastShadow(!bPathOverlayOnly);
	NewComponent->bCastStaticShadow = false;
	NewComponent->bCastDynamicShadow = !bPathOverlayOnly;
	NewComponent->bUseComplexAsSimpleCollision = !bPathOverlayOnly;
	NewComponent->SetCollisionEnabled(
		bGenerateCollision && !bPathOverlayOnly
		? ECollisionEnabled::QueryAndPhysics
		: ECollisionEnabled::NoCollision
	);

	AddOwnedComponent(NewComponent);
	NewComponent->RegisterComponent();

	ContinuousChunkComponents.Add(NewComponent);
	ComponentLookup.Add(
		ChunkCoordinate,
		NewComponent
	);

	return NewComponent;
}

void ATileMapTerrainActor::BuildContinuousChunk(
	const FIntVector& ChunkCoordinate,
	const TArray<FIntVector>& ChunkBlocks,
	bool bPathOverlayOnly
)
{
	if (bPathOverlayOnly)
	{
		bool bChunkTouchesVisiblePath = false;

		for (const FIntVector& GridPosition : ChunkBlocks)
		{
			for (int32 XOffset = -1; XOffset <= 1; ++XOffset)
			{
				for (int32 YOffset = -1; YOffset <= 1; ++YOffset)
				{
					if (IsVisiblePaintedPathBlock(
						GridPosition + FIntVector(XOffset, YOffset, 0)))
					{
						bChunkTouchesVisiblePath = true;
						break;
					}
				}

				if (bChunkTouchesVisiblePath)
				{
					break;
				}
			}

			if (bChunkTouchesVisiblePath)
			{
				break;
			}
		}

		if (!bChunkTouchesVisiblePath)
		{
			return;
		}
	}

	TMap<int32, FTileMapContinuousSection> SectionsByTileType;
	// Organic-edge density is intentionally fixed and no longer exposed as an
	// actor setting. The old default of eight multiplied into several separate
	// 32/24-sample paths, while their hardcoded floors prevented low values from
	// reducing topology consistently. Two base samples preserve the accepted
	// chamfer shape, with derived densities shared by every detailed branch.
	constexpr int32 ChamferSubdivisions = 2;
	constexpr int32 CornerSubdivisions = 2;
	constexpr int32 DetailedEdgeSubdivisions =
		ChamferSubdivisions * 4;
	constexpr int32 RampLongitudinalSubdivisions =
		ChamferSubdivisions * 3;

	struct FContinuousWall
	{
		FIntVector NeighborOffset;
		FVector Normal;
		FVector2D BottomAOffset;
		FVector2D BottomBOffset;
	};

	const FContinuousWall Walls[4] =
	{
		{
			FIntVector(1, 0, 0),
			FVector(1, 0, 0),
			FVector2D(1, 0),
			FVector2D(1, 1)
		},
		{
			FIntVector(-1, 0, 0),
			FVector(-1, 0, 0),
			FVector2D(0, 1),
			FVector2D(0, 0)
		},
		{
			FIntVector(0, 1, 0),
			FVector(0, 1, 0),
			FVector2D(1, 1),
			FVector2D(0, 1)
		},
		{
			FIntVector(0, -1, 0),
			FVector(0, -1, 0),
			FVector2D(0, 0),
			FVector2D(1, 0)
		}
	};

	const float SafeGridSize = FMath::Max(GridSize, 1.0f);
	const float CornerRadius = FMath::Clamp(
		ContinuousCliffCornerRadius,
		1.0f,
		SafeGridSize * 0.2f
	);
	const float ChamferWidth = FMath::Clamp(
		ContinuousChamferWidth,
		1.0f,
		SafeGridSize * 0.3f
	);
	const float ChamferDepth = FMath::Clamp(
		ContinuousChamferDepth,
		1.0f,
		SafeGridSize * 0.25f
	);
	const float CliffFootBlendWidth = FMath::Clamp(
		ContinuousCliffFootBlendWidth,
		0.0f,
		SafeGridSize * 0.3f
	);
	const float CliffFootBlendHeight = FMath::Clamp(
		ContinuousCliffFootBlendHeight,
		0.0f,
		SafeGridSize * 0.3f
	);
	const float PathBlendWidth = FMath::Clamp(
		ContinuousPathBlendWidth,
		0.0f,
		SafeGridSize * 0.5f
	);
	const float EdgeIrregularity = FMath::Clamp(
		ContinuousEdgeIrregularity,
		0.0f,
		FMath::Min(
			SafeGridSize * 0.1f,
			ChamferWidth * 0.75f
		)
	);
	const float EdgeWavelength = FMath::Clamp(
		ContinuousEdgeWavelength,
		SafeGridSize * 0.25f,
		SafeGridSize * 2.0f
	);
	const float CornerAlpha = CornerRadius / SafeGridSize;
	const float ChamferAlpha = ChamferWidth / SafeGridSize;
	const float CliffFootBlendAlpha =
		CliffFootBlendWidth / SafeGridSize;
	const float PathBlendAlpha =
		PathBlendWidth / SafeGridSize;

	TArray<float> RoundedSurfaceAlphas;
	RoundedSurfaceAlphas.Reserve(
		(CornerSubdivisions * 2) + 7
	);

	auto AddSurfaceAlpha =
		[&RoundedSurfaceAlphas](float Alpha)
		{
			const float SafeAlpha = FMath::Clamp(
				Alpha,
				0.0f,
				1.0f
			);

			for (const float ExistingAlpha : RoundedSurfaceAlphas)
			{
				if (FMath::IsNearlyEqual(ExistingAlpha, SafeAlpha))
				{
					return;
				}
			}

			RoundedSurfaceAlphas.Add(SafeAlpha);
		};

	AddSurfaceAlpha(0.0f);
	AddSurfaceAlpha(1.0f);
	AddSurfaceAlpha(ChamferAlpha);
	AddSurfaceAlpha(1.0f - ChamferAlpha);
	AddSurfaceAlpha(0.5f);

	if (EdgeIrregularity > KINDA_SMALL_NUMBER)
	{
		AddSurfaceAlpha(0.25f);
		AddSurfaceAlpha(0.75f);
	}

	for (
		int32 CornerStep = 1;
		CornerStep <= CornerSubdivisions;
		++CornerStep
		)
	{
		const float CornerStepAlpha =
			CornerAlpha *
			(
				static_cast<float>(CornerStep) /
				static_cast<float>(CornerSubdivisions)
			);

		AddSurfaceAlpha(CornerStepAlpha);
		AddSurfaceAlpha(1.0f - CornerStepAlpha);
	}

	RoundedSurfaceAlphas.Sort();

	// Straight exposed runs do not need the extra tangent samples reserved for
	// rounded junctions. Build a second boundary-compatible alpha set from a
	// physical target segment length; both the top and its wall use this exact
	// set, so lowering density cannot introduce a T-junction at the cliff lip.
	TArray<float> StraightEdgeSurfaceAlphas;
	StraightEdgeSurfaceAlphas.Reserve(DetailedEdgeSubdivisions + 5);
	auto AddStraightEdgeAlpha =
		[&StraightEdgeSurfaceAlphas](float Alpha)
		{
			const float SafeAlpha = FMath::Clamp(Alpha, 0.0f, 1.0f);

			for (const float ExistingAlpha : StraightEdgeSurfaceAlphas)
			{
				if (FMath::IsNearlyEqual(ExistingAlpha, SafeAlpha))
				{
					return;
				}
			}

			StraightEdgeSurfaceAlphas.Add(SafeAlpha);
		};

	AddStraightEdgeAlpha(0.0f);
	AddStraightEdgeAlpha(1.0f);
	AddStraightEdgeAlpha(ChamferAlpha);
	AddStraightEdgeAlpha(1.0f - ChamferAlpha);
	AddStraightEdgeAlpha(0.5f);

	if (EdgeIrregularity > KINDA_SMALL_NUMBER)
	{
		const float TargetEdgeSegmentLength = FMath::Clamp(
			EdgeWavelength / 4.0f,
			SafeGridSize * 0.125f,
			SafeGridSize * 0.5f
		);
		const int32 StraightEdgeSubdivisions = FMath::Clamp(
			FMath::CeilToInt(
				SafeGridSize / TargetEdgeSegmentLength
			),
			2,
			DetailedEdgeSubdivisions
		);

		for (
			int32 EdgeStep = 1;
			EdgeStep < StraightEdgeSubdivisions;
			++EdgeStep
			)
		{
			AddStraightEdgeAlpha(
				static_cast<float>(EdgeStep) /
					static_cast<float>(StraightEdgeSubdivisions)
			);
		}
	}

	StraightEdgeSurfaceAlphas.Sort();
	TArray<float> RoundedCliffFootSurfaceAlphas =
		RoundedSurfaceAlphas;
	TArray<float> StraightCliffFootSurfaceAlphas =
		StraightEdgeSurfaceAlphas;

	TArray<float> FlatSurfaceAlphas;
	FlatSurfaceAlphas.Add(0.0f);
	FlatSurfaceAlphas.Add(1.0f);
	TArray<float> CliffFootSurfaceAlphas = FlatSurfaceAlphas;

	if (CliffFootBlendWidth > KINDA_SMALL_NUMBER)
	{
		const float FootAlphas[2] =
		{
			CliffFootBlendAlpha,
			1.0f - CliffFootBlendAlpha
		};

		for (const float FootAlpha : FootAlphas)
		{
			CliffFootSurfaceAlphas.Add(FootAlpha);
			bool bAlreadyRoundedAlpha = false;

			for (const float RoundedAlpha : RoundedSurfaceAlphas)
			{
				if (FMath::IsNearlyEqual(RoundedAlpha, FootAlpha))
				{
					bAlreadyRoundedAlpha = true;
					break;
				}
			}

			if (!bAlreadyRoundedAlpha)
			{
				RoundedCliffFootSurfaceAlphas.Add(FootAlpha);
			}

			bool bAlreadyStraightAlpha = false;

			for (const float StraightAlpha : StraightEdgeSurfaceAlphas)
			{
				if (FMath::IsNearlyEqual(StraightAlpha, FootAlpha))
				{
					bAlreadyStraightAlpha = true;
					break;
				}
			}

			if (!bAlreadyStraightAlpha)
			{
				StraightCliffFootSurfaceAlphas.Add(FootAlpha);
			}
		}

		CliffFootSurfaceAlphas.Sort();
		RoundedCliffFootSurfaceAlphas.Sort();
		StraightCliffFootSurfaceAlphas.Sort();
	}

	struct FDiagonalTangentJunction
	{
		FVector2D CornerPoint;
		FVector2D FirstRay;
		FVector2D SecondRay;
		FVector2D CircleCenter;
		float Radius;
		float TangentDistance;
		float RadialNormalSign;
	};

	auto BuildContinuousFootprint =
		[this](
			const FIntVector& Cell,
			TArray<FVector2D, TInlineAllocator<4>>& OutPoints
		)
		{
			OutPoints.Reset();

			if (!IsContinuousSurfaceBlock(Cell))
			{
				return false;
			}

			if (
				!IsContinuousDiagonalTileType(
					GetBlockTileType(Cell)
				)
				)
			{
				OutPoints.Add(FVector2D(0, 0));
				OutPoints.Add(FVector2D(1, 0));
				OutPoints.Add(FVector2D(1, 1));
				OutPoints.Add(FVector2D(0, 1));
				return true;
			}

			float DiagonalFraction = 1.0f;
			GetContinuousDiagonalFraction(
				GetBlockTileType(Cell),
				DiagonalFraction
			);

			switch (GetBlockRotation(Cell) % 4)
			{
			case 0:
				OutPoints.Add(FVector2D(0, 0));
				OutPoints.Add(FVector2D(1, 0));
				OutPoints.Add(FVector2D(1, DiagonalFraction));
				break;

			case 1:
				OutPoints.Add(FVector2D(1, 0));
				OutPoints.Add(FVector2D(1, 1));
				OutPoints.Add(
					FVector2D(1.0f - DiagonalFraction, 1)
				);
				break;

			case 2:
				OutPoints.Add(FVector2D(1, 1));
				OutPoints.Add(FVector2D(0, 1));
				OutPoints.Add(
					FVector2D(0, 1.0f - DiagonalFraction)
				);
				break;

			default:
				OutPoints.Add(FVector2D(0, 1));
				OutPoints.Add(FVector2D(0, 0));
				OutPoints.Add(FVector2D(DiagonalFraction, 0));
				break;
			}

			return true;
		};

	struct FContinuousBoundarySegment
	{
		FVector2D Start;
		FVector2D End;
		FVector2D OutwardNormal;
		bool bDiagonal;
	};

	// Return only the portions of a cell boundary that are physically exposed.
	// A shallow diagonal can cover only part of a shared grid edge, so treating
	// that edge as a single boolean is what produced the missing wall in v1.2.19.
	auto BuildExposedBoundarySegments =
		[this, &BuildContinuousFootprint, SafeGridSize](
			const FIntVector& Cell,
			TArray<FContinuousBoundarySegment>& OutSegments
		)
		{
			OutSegments.Reset();
			TArray<FVector2D, TInlineAllocator<4>> UnitFootprint;

			if (!BuildContinuousFootprint(Cell, UnitFootprint))
			{
				return false;
			}

			const FVector2D CellMinimum(
				Cell.X * SafeGridSize,
				Cell.Y * SafeGridSize
			);

			for (
				int32 EdgeIndex = 0;
				EdgeIndex < UnitFootprint.Num();
				++EdgeIndex
				)
			{
				const FVector2D UnitStart = UnitFootprint[EdgeIndex];
				const FVector2D UnitEnd =
					UnitFootprint[(EdgeIndex + 1) % UnitFootprint.Num()];
				const FVector2D Start =
					CellMinimum + (UnitStart * SafeGridSize);
				const FVector2D End =
					CellMinimum + (UnitEnd * SafeGridSize);
				const FVector2D EdgeVector = End - Start;

				if (EdgeVector.SizeSquared() <= SMALL_NUMBER)
				{
					continue;
				}

				const FVector2D OutwardNormal =
					FVector2D(EdgeVector.Y, -EdgeVector.X)
					.GetSafeNormal();
				const bool bAxisAligned =
					FMath::IsNearlyZero(EdgeVector.X) ||
					FMath::IsNearlyZero(EdgeVector.Y);

				if (!bAxisAligned)
				{
					OutSegments.Add(
						{ Start, End, OutwardNormal, true }
					);
					continue;
				}

				const FIntVector EdgeDirection(
					FMath::RoundToInt(OutwardNormal.X),
					FMath::RoundToInt(OutwardNormal.Y),
					0
				);
				float CoverageStart = 0.0f;
				float CoverageEnd = 0.0f;
				const bool bHasCoverage =
					GetContinuousTerrainCoverageAcrossEdge(
						Cell,
						EdgeDirection,
						CoverageStart,
						CoverageEnd
					);

				if (!bHasCoverage)
				{
					OutSegments.Add(
						{ Start, End, OutwardNormal, false }
					);
					continue;
				}

				// Coverage is canonical world-edge alpha: increasing Y on a
				// vertical edge and increasing X on a horizontal edge. Convert it
				// into this polygon edge's own Start-to-End alpha.
				const float CanonicalStart =
					FMath::IsNearlyZero(EdgeVector.X)
						? UnitStart.Y
						: UnitStart.X;
				const float CanonicalEnd =
					FMath::IsNearlyZero(EdgeVector.X)
						? UnitEnd.Y
						: UnitEnd.X;
				const float CanonicalDelta =
					CanonicalEnd - CanonicalStart;

				if (FMath::Abs(CanonicalDelta) <= SMALL_NUMBER)
				{
					continue;
				}

				float CoveredAlphaA =
					(CoverageStart - CanonicalStart) / CanonicalDelta;
				float CoveredAlphaB =
					(CoverageEnd - CanonicalStart) / CanonicalDelta;
				const float CoveredStart = FMath::Clamp(
					FMath::Min(CoveredAlphaA, CoveredAlphaB),
					0.0f,
					1.0f
				);
				const float CoveredEnd = FMath::Clamp(
					FMath::Max(CoveredAlphaA, CoveredAlphaB),
					0.0f,
					1.0f
				);

				if (
					CoveredEnd - CoveredStart <=
						KINDA_SMALL_NUMBER
					)
				{
					OutSegments.Add(
						{ Start, End, OutwardNormal, false }
					);
					continue;
				}

				if (CoveredStart > KINDA_SMALL_NUMBER)
				{
					OutSegments.Add(
						{
							Start,
							FMath::Lerp(Start, End, CoveredStart),
							OutwardNormal,
							false
						}
					);
				}

				if (CoveredEnd < 1.0f - KINDA_SMALL_NUMBER)
				{
					OutSegments.Add(
						{
							FMath::Lerp(Start, End, CoveredEnd),
							End,
							OutwardNormal,
							false
						}
					);
				}
			}

			return true;
		};

	auto TryBuildDiagonalTangentJunction =
		[&](
			const FVector2D& CornerPoint,
			int32 Layer,
			FDiagonalTangentJunction& OutJunction
		)
		{
			const int32 BaseGridX =
				FMath::FloorToInt(CornerPoint.X / SafeGridSize);
			const int32 BaseGridY =
				FMath::FloorToInt(CornerPoint.Y / SafeGridSize);
			struct FBoundaryRay
			{
				FVector2D Direction;
				FVector2D OutwardNormal;
				float Length;
			};

			TArray<FBoundaryRay, TInlineAllocator<2>> DiagonalRays;
			TArray<FBoundaryRay, TInlineAllocator<4>> StraightRays;

			auto AddUniqueRay =
				[](
					auto& Rays,
					const FVector2D& Ray,
					const FVector2D& OutwardNormal,
					float RayLength
				)
				{
					for (FBoundaryRay& ExistingRay : Rays)
					{
						if (
							ExistingRay.Direction.Equals(
								Ray,
								KINDA_SMALL_NUMBER
							)
							)
						{
							ExistingRay.Length = FMath::Max(
								ExistingRay.Length,
								RayLength
							);
							return;
						}
					}

					FBoundaryRay NewRay;
					NewRay.Direction = Ray;
					NewRay.OutwardNormal = OutwardNormal;
					NewRay.Length = RayLength;
					Rays.Add(NewRay);
				};

			for (int32 XOffset = -1; XOffset <= 1; ++XOffset)
			{
				for (int32 YOffset = -1; YOffset <= 1; ++YOffset)
				{
					const FIntVector Cell(
						BaseGridX + XOffset,
						BaseGridY + YOffset,
						Layer
					);
					TArray<FContinuousBoundarySegment> Segments;

					if (!BuildExposedBoundarySegments(Cell, Segments))
					{
						continue;
					}

					for (const FContinuousBoundarySegment& Segment : Segments)
					{
						const FVector2D& Start = Segment.Start;
						const FVector2D& End = Segment.End;
					const bool bStartsAtCorner =
							Start.Equals(CornerPoint, 0.01f);
					const bool bEndsAtCorner =
							End.Equals(CornerPoint, 0.01f);

					if (!bStartsAtCorner && !bEndsAtCorner)
					{
						continue;
					}

					const FVector2D OtherPoint =
						bStartsAtCorner ? End : Start;
					const float RayLength =
						(OtherPoint - CornerPoint).Size();
					const FVector2D Ray =
						(OtherPoint - CornerPoint).GetSafeNormal();

					if (Segment.bDiagonal)
					{
						AddUniqueRay(
							DiagonalRays,
							Ray,
							Segment.OutwardNormal,
							RayLength
						);
						continue;
					}

					AddUniqueRay(
						StraightRays,
						Ray,
						Segment.OutwardNormal,
						RayLength
					);
					}
				}
			}

			const bool bDiagonalToStraight =
				DiagonalRays.Num() == 1 &&
				StraightRays.Num() == 1;
			const bool bDiagonalToDiagonal =
				DiagonalRays.Num() == 2 &&
				StraightRays.Num() == 0;

			if (!bDiagonalToStraight && !bDiagonalToDiagonal)
			{
				return false;
			}

			const FBoundaryRay& FirstBoundaryRay = DiagonalRays[0];
			const FBoundaryRay& SecondBoundaryRay =
				bDiagonalToDiagonal
					? DiagonalRays[1]
					: StraightRays[0];
			const FVector2D FirstRay = FirstBoundaryRay.Direction;
			const FVector2D SecondRay = SecondBoundaryRay.Direction;
			const float RayDot = FMath::Clamp(
				FVector2D::DotProduct(FirstRay, SecondRay),
				-1.0f,
				1.0f
			);
			const float InteriorAngle = FMath::Acos(RayDot);

			// A fixed 45-degree cut has exactly two legitimate
			// diagonal-to-straight junctions: one acute 45-degree endpoint and
			// one obtuse 135-degree endpoint. Both must use the same tangent
			// ownership path. Excluding the exact acute endpoint leaves its
			// transition face unowned after the obtuse endpoint is corrected.
			const float JunctionAngleTolerance =
				FMath::DegreesToRadians(0.1f);
			const bool bAcuteDiagonalJunction =
				FMath::Abs(
					InteriorAngle - FMath::DegreesToRadians(45.0f)
				) <= JunctionAngleTolerance;
			const bool bObtuseDiagonalJunction =
				FMath::Abs(
					InteriorAngle - FMath::DegreesToRadians(135.0f)
				) <= JunctionAngleTolerance;
			const bool bRightAngleTwoSlantJunction =
				bDiagonalToDiagonal &&
				FMath::Abs(
					InteriorAngle - FMath::DegreesToRadians(90.0f)
				) <= JunctionAngleTolerance;

			// Two horizontal 45-degree cuts may meet at a right-angle V. Give
			// only that shared diagonal-to-diagonal point the same physical
			// tangent fillet used by established diagonal-to-straight corners.
			// Collinear seams and any junction that also owns a straight edge
			// deliberately remain outside this path.

			if (
				bDiagonalToStraight &&
				!bAcuteDiagonalJunction &&
				!bObtuseDiagonalJunction
				)
			{
				return false;
			}

			if (
				bDiagonalToDiagonal &&
				!bRightAngleTwoSlantJunction
				)
			{
				return false;
			}

			const float HalfAngle = InteriorAngle * 0.5f;
			// Scale the diagonal transition from the established cliff-corner
			// setting so shallow and 45-degree cuts have comparable visible relief.
			const float DesiredRadius = FMath::Clamp(
				CornerRadius * 2.0f,
				2.0f,
				SafeGridSize * 0.4f
			);
			const float TangentScale = FMath::Max(
				FMath::Tan(HalfAngle),
				KINDA_SMALL_NUMBER
			);
			const float AvailableRayLength = FMath::Min(
				FirstBoundaryRay.Length,
				SecondBoundaryRay.Length
			);
			const float TangentDistance = FMath::Min(
				FMath::Min(
					DesiredRadius / TangentScale,
					SafeGridSize * 0.25f
				),
				AvailableRayLength * 0.8f
			);

			if (TangentDistance <= KINDA_SMALL_NUMBER)
			{
				return false;
			}

			const float Radius = TangentDistance * TangentScale;
			const FVector2D Bisector =
				(FirstRay + SecondRay).GetSafeNormal();

			if (Bisector.SizeSquared() <= SMALL_NUMBER)
			{
				return false;
			}

			const FVector2D CircleCenter =
				CornerPoint +
				(
					Bisector *
					(
						Radius /
						FMath::Max(FMath::Sin(HalfAngle), KINDA_SMALL_NUMBER)
					)
				);
			const FVector2D CenterFromCorner =
				CircleCenter - CornerPoint;
			const FVector2D FirstRadialNormal =
				(
					(FirstRay * TangentDistance) -
					CenterFromCorner
				).GetSafeNormal();
			const FVector2D SecondRadialNormal =
				(
					(SecondRay * TangentDistance) -
					CenterFromCorner
				).GetSafeNormal();
			const float FirstOutwardAlignment =
				FVector2D::DotProduct(
					FirstRadialNormal,
					FirstBoundaryRay.OutwardNormal
				);
			const float SecondOutwardAlignment =
				FVector2D::DotProduct(
					SecondRadialNormal,
					SecondBoundaryRay.OutwardNormal
				);

			// Both tangent radii must agree on a single normal sign. This is
			// the ownership test that the older corner code omitted.
			if (
				FMath::Abs(FirstOutwardAlignment) < 0.9f ||
				FMath::Abs(SecondOutwardAlignment) < 0.9f ||
				(
					FirstOutwardAlignment *
					SecondOutwardAlignment
				) <= 0.0f
				)
			{
				return false;
			}

			OutJunction.CornerPoint = CornerPoint;
			OutJunction.FirstRay = FirstRay;
			OutJunction.SecondRay = SecondRay;
			OutJunction.CircleCenter = CircleCenter;
			OutJunction.Radius = Radius;
			OutJunction.TangentDistance = TangentDistance;
			OutJunction.RadialNormalSign =
				FirstOutwardAlignment >= 0.0f ? 1.0f : -1.0f;
			return true;
		};

	auto RoundDiagonalTangentPoint =
		[&](
			const FVector2D& OriginalPoint,
			int32 Layer,
			FVector2D* OutWallNormal
		)
		{
			if (OutWallNormal)
			{
				*OutWallNormal = FVector2D::ZeroVector;
			}

			const int32 BaseGridX =
				FMath::FloorToInt(OriginalPoint.X / SafeGridSize);
			const int32 BaseGridY =
				FMath::FloorToInt(OriginalPoint.Y / SafeGridSize);
			TArray<FVector2D, TInlineAllocator<16>> JunctionPoints;

			auto AddUniqueJunctionPoint =
				[&JunctionPoints](const FVector2D& Point)
				{
					for (const FVector2D& ExistingPoint : JunctionPoints)
					{
						if (ExistingPoint.Equals(Point, 0.01f))
						{
							return;
						}
					}

					JunctionPoints.Add(Point);
				};

			for (int32 XOffset = -1; XOffset <= 1; ++XOffset)
			{
				for (int32 YOffset = -1; YOffset <= 1; ++YOffset)
				{
					TArray<FContinuousBoundarySegment> Segments;
					BuildExposedBoundarySegments(
						FIntVector(
							BaseGridX + XOffset,
							BaseGridY + YOffset,
							Layer
						),
						Segments
					);

					for (const FContinuousBoundarySegment& Segment : Segments)
					{
						if (Segment.bDiagonal)
						{
							AddUniqueJunctionPoint(Segment.Start);
							AddUniqueJunctionPoint(Segment.End);
						}
					}
				}
			}

			for (const FVector2D& JunctionPoint : JunctionPoints)
			{
				FDiagonalTangentJunction Junction;

				if (
					!TryBuildDiagonalTangentJunction(
						JunctionPoint,
						Layer,
						Junction
					)
					)
				{
					continue;
				}

				const FVector2D FromCorner =
					OriginalPoint - Junction.CornerPoint;
					const float BasisCross =
						(
							Junction.FirstRay.X *
							Junction.SecondRay.Y
						) -
						(
							Junction.FirstRay.Y *
							Junction.SecondRay.X
						);

					if (FMath::Abs(BasisCross) <= SMALL_NUMBER)
					{
						continue;
					}

					const float FirstAmount =
						(
							FromCorner.X * Junction.SecondRay.Y -
							FromCorner.Y * Junction.SecondRay.X
						) / BasisCross;
					const float SecondAmount =
						(
							Junction.FirstRay.X * FromCorner.Y -
							Junction.FirstRay.Y * FromCorner.X
						) / BasisCross;
					const FVector2D CenterFromCorner =
						Junction.CircleCenter - Junction.CornerPoint;
					const float CenterFirstAmount =
						(
							CenterFromCorner.X * Junction.SecondRay.Y -
							CenterFromCorner.Y * Junction.SecondRay.X
						) / BasisCross;
					const float CenterSecondAmount =
						(
							Junction.FirstRay.X * CenterFromCorner.Y -
							Junction.FirstRay.Y * CenterFromCorner.X
						) / BasisCross;
					const float DeltaFirst =
						FirstAmount - CenterFirstAmount;
					const float DeltaSecond =
						SecondAmount - CenterSecondAmount;
					float BoundaryScale =
						TNumericLimits<float>::Max();

					if (DeltaFirst < -KINDA_SMALL_NUMBER)
					{
						BoundaryScale = FMath::Min(
							BoundaryScale,
							-CenterFirstAmount / DeltaFirst
						);
					}

					if (DeltaSecond < -KINDA_SMALL_NUMBER)
					{
						BoundaryScale = FMath::Min(
							BoundaryScale,
							-CenterSecondAmount / DeltaSecond
						);
					}

					if (
						BoundaryScale == TNumericLimits<float>::Max() ||
						BoundaryScale < 1.0f - KINDA_SMALL_NUMBER
						)
					{
						continue;
					}

					const float BoundaryFirstAmount =
						CenterFirstAmount +
						(DeltaFirst * BoundaryScale);
					const float BoundarySecondAmount =
						CenterSecondAmount +
						(DeltaSecond * BoundaryScale);

					if (
						BoundaryFirstAmount < -KINDA_SMALL_NUMBER ||
						BoundarySecondAmount < -KINDA_SMALL_NUMBER ||
						BoundaryFirstAmount >
							Junction.TangentDistance + KINDA_SMALL_NUMBER ||
						BoundarySecondAmount >
							Junction.TangentDistance + KINDA_SMALL_NUMBER
						)
					{
						continue;
					}

					const FVector2D CircleOffset =
						OriginalPoint - Junction.CircleCenter;
					const float Distance = CircleOffset.Size();
					const float BoundaryDistance =
						Distance * BoundaryScale;

					if (
						Distance <= SMALL_NUMBER ||
						BoundaryDistance <=
							Junction.Radius + KINDA_SMALL_NUMBER
						)
					{
						continue;
					}

					// Radially redistribute the surface between the circle center
					// and the original two-edge boundary. Moving only boundary
					// vertices would leave a hole; collapsing the whole cutout onto
					// the arc would create zero-area or flipped top triangles.
					const FVector2D RoundedPoint =
						Junction.CircleCenter +
						(
							CircleOffset *
							(Junction.Radius / BoundaryDistance)
						);

					if (
						OutWallNormal &&
						(
							FMath::Abs(FirstAmount) <=
								KINDA_SMALL_NUMBER ||
							FMath::Abs(SecondAmount) <=
								KINDA_SMALL_NUMBER
						)
						)
					{
						const FVector2D RoundedWallNormal =
							(RoundedPoint - Junction.CircleCenter)
							.GetSafeNormal() *
							Junction.RadialNormalSign;

						// RadialNormalSign was resolved once from both owning
						// boundary normals. It stays consistent across the whole
						// curved strip, including convex and concave junctions.
						*OutWallNormal = RoundedWallNormal;
					}

					return RoundedPoint;
			}

			return OriginalPoint;
		};

	auto IsConvexCorner =
		[this, &TryBuildDiagonalTangentJunction, SafeGridSize](
			const FIntVector& BlockPosition,
			int32 CornerX,
			int32 CornerY
		)
		{
			if (!IsContinuousSurfaceBlock(BlockPosition))
			{
				return false;
			}

			const int32 XDirection = CornerX == 0 ? -1 : 1;
			const int32 YDirection = CornerY == 0 ? -1 : 1;
			const FVector2D CornerPoint(
				(BlockPosition.X + CornerX) * SafeGridSize,
				(BlockPosition.Y + CornerY) * SafeGridSize
			);
			FDiagonalTangentJunction DiagonalJunction;

			if (
				TryBuildDiagonalTangentJunction(
					CornerPoint,
					BlockPosition.Z,
					DiagonalJunction
				)
				)
			{
				return false;
			}

			auto HasCoverageAt =
				[this, &BlockPosition](
					const FIntVector& Direction,
					float CanonicalAlpha
				)
				{
					float CoverageStart = 0.0f;
					float CoverageEnd = 0.0f;

					return
						GetContinuousTerrainCoverageAcrossEdge(
							BlockPosition,
							Direction,
							CoverageStart,
							CoverageEnd
						) &&
						CanonicalAlpha >=
							CoverageStart - KINDA_SMALL_NUMBER &&
						CanonicalAlpha <=
							CoverageEnd + KINDA_SMALL_NUMBER;
				};

			return
				!HasCoverageAt(
					FIntVector(XDirection, 0, 0),
					static_cast<float>(CornerY)
				) &&
				!HasCoverageAt(
					FIntVector(0, YDirection, 0),
					static_cast<float>(CornerX)
				);
		};

	auto IsRoundableConcaveCorner =
		[this](
			int32 CornerGridX,
			int32 CornerGridY,
			int32 Layer
		)
		{
			const FIntVector Cells[4] =
			{
				FIntVector(CornerGridX - 1, CornerGridY - 1, Layer),
				FIntVector(CornerGridX, CornerGridY - 1, Layer),
				FIntVector(CornerGridX - 1, CornerGridY, Layer),
				FIntVector(CornerGridX, CornerGridY, Layer)
			};

			TArray<FIntVector, TInlineAllocator<4>> SurfaceCells;
			int32 OccupiedCellCount = 0;

			for (const FIntVector& Cell : Cells)
			{
				int32 RampCount = 0;
				int32 RampIndex = 0;
				int32 StairCount = 0;
				int32 StairIndex = 0;

				if (
					IsContinuousSurfaceBlock(Cell) &&
					(
						IsContinuousDiagonalTileType(
							GetBlockTileType(Cell)
						) ||
						GetContinuousRampMetadata(
							GetBlockTileType(Cell),
							RampCount,
							RampIndex
						) ||
						GetContinuousStairMetadata(
							GetBlockTileType(Cell),
							StairCount,
							StairIndex
						)
					)
					)
				{
					return false;
				}

				if (HasBlock(Cell))
				{
					++OccupiedCellCount;
				}

				if (IsContinuousSurfaceBlock(Cell))
				{
					SurfaceCells.Add(Cell);
				}
			}

			if (OccupiedCellCount != 3 || SurfaceCells.Num() != 3)
			{
				return false;
			}

			auto HasUniformLayerState =
				[this, &SurfaceCells](const FIntVector& LayerOffset)
				{
					const bool bFirstOccupied =
						HasBlock(SurfaceCells[0] + LayerOffset);
					const bool bFirstContinuous =
						IsContinuousSurfaceBlock(
							SurfaceCells[0] + LayerOffset
						);

					for (
						int32 CellIndex = 1;
						CellIndex < SurfaceCells.Num();
						++CellIndex
						)
					{
						if (
							HasBlock(
								SurfaceCells[CellIndex] + LayerOffset
							) != bFirstOccupied ||
							IsContinuousSurfaceBlock(
								SurfaceCells[CellIndex] + LayerOffset
							) != bFirstContinuous
							)
						{
							return false;
						}
					}

					return true;
				};

			if (!HasUniformLayerState(FIntVector(0, 0, 1)))
			{
				return false;
			}

			return
				Layer == 0 ||
				HasUniformLayerState(FIntVector(0, 0, -1));
		};

	// A visible lip endpoint beside a covered support cell must return to the
	// shared grid corner. Otherwise its horizontal wave pulls away from the
	// stacked transition patch and exposes a narrow crack.
	auto IsStackTransitionCorner =
		[this](
			int32 CornerGridX,
			int32 CornerGridY,
			int32 Layer
		)
		{
			const FIntVector Cells[4] =
			{
				FIntVector(CornerGridX - 1, CornerGridY - 1, Layer),
				FIntVector(CornerGridX, CornerGridY - 1, Layer),
				FIntVector(CornerGridX - 1, CornerGridY, Layer),
				FIntVector(CornerGridX, CornerGridY, Layer)
			};
			bool bHasCoveredSurface = false;
			bool bHasVisibleSurface = false;

			for (const FIntVector& Cell : Cells)
			{
				if (!IsContinuousSurfaceBlock(Cell))
				{
					continue;
				}

				if (HasBlock(Cell + FIntVector(0, 0, 1)))
				{
					bHasCoveredSurface = true;
				}
				else
				{
					bHasVisibleSurface = true;
				}
			}

			return bHasCoveredSurface && bHasVisibleSurface;
		};

	for (const FIntVector& GridPosition : ChunkBlocks)
	{
		const bool bShouldBuildBlock = bPathOverlayOnly
			? (
				IsContinuousSurfaceBlock(GridPosition) &&
				!ShouldUseContinuousTerrainForBlock(GridPosition)
			)
			: ShouldUseContinuousTerrainForBlock(GridPosition);

		if (!bShouldBuildBlock)
		{
			continue;
		}

		const int32 TileType = GetBlockTileType(GridPosition);
		FTileMapContinuousSection& Section =
			SectionsByTileType.FindOrAdd(TileType);
		const FVector BlockMinimum(
			GridPosition.X * SafeGridSize,
			GridPosition.Y * SafeGridSize,
			GridPosition.Z * SafeGridSize
		);
		const FVector2D BlockMinimum2D(
			BlockMinimum.X,
			BlockMinimum.Y
		);

		auto GetCliffBaseBlendHeight =
			[&](
				const FVector2D& OriginalPoint,
				const FVector2D& OutwardNormal,
				float MaximumHeight
			)
			{
				const float SafeMaximumHeight = FMath::Max(
					0.0f,
					MaximumHeight
				);
				const FVector2D SafeNormal =
					OutwardNormal.GetSafeNormal();
				const float WaveAmplitude = FMath::Min(
					CliffFootBlendHeight * 0.45f,
					FMath::Min(
						EdgeIrregularity * 0.5f,
						CliffFootBlendWidth * 0.5f
					)
				);

				if (
					WaveAmplitude <= KINDA_SMALL_NUMBER ||
					SafeNormal.SizeSquared() <= SMALL_NUMBER
					)
				{
					return FMath::Clamp(
						CliffFootBlendHeight,
						0.0f,
						SafeMaximumHeight
					);
				}

				const FVector2D Tangent(-SafeNormal.Y, SafeNormal.X);
				const float TangentCoordinate = FVector2D::DotProduct(
					OriginalPoint,
					Tangent
				);
				const float EdgeLineCoordinate = FVector2D::DotProduct(
					OriginalPoint,
					SafeNormal
				);
				const float EdgePhase =
					(EdgeLineCoordinate / SafeGridSize) * 1.6180339f +
					(SafeNormal.X * 1.2345f) +
					(SafeNormal.Y * 2.3456f) +
					(GridPosition.Z * 0.731f);
				const float PrimaryAngle =
					(2.0f * PI * TangentCoordinate / EdgeWavelength) +
					EdgePhase;
				const float SecondaryAngle =
					(
						2.0f * PI * TangentCoordinate /
						(EdgeWavelength * 0.61f)
					) +
					(EdgePhase * 1.37f) +
					1.234f;
				const float Noise =
					(0.75f * FMath::Sin(PrimaryAngle)) +
					(0.25f * FMath::Sin(SecondaryAngle));

				return FMath::Clamp(
					CliffFootBlendHeight + (Noise * WaveAmplitude),
					0.0f,
					SafeMaximumHeight
				);
			};

		int32 StairSegmentCount = 0;
		int32 StairSegmentIndex = 0;

		if (
			GetContinuousStairMetadata(
				TileType,
				StairSegmentCount,
				StairSegmentIndex
			)
			)
		{
			enum class EStairBoundaryMode : uint8
			{
				Seam,
				NeighborWall,
				Exposed
			};

			const uint8 QuarterTurns =
				GetBlockRotation(GridPosition) % 4;
			FIntVector StairStep(1, 0, 0);
			FVector2D RiseAxis(1.0f, 0.0f);

			switch (QuarterTurns)
			{
			case 1:
				StairStep = FIntVector(0, 1, 0);
				RiseAxis = FVector2D(0.0f, 1.0f);
				break;
			case 2:
				StairStep = FIntVector(-1, 0, 0);
				RiseAxis = FVector2D(-1.0f, 0.0f);
				break;
			case 3:
				StairStep = FIntVector(0, -1, 0);
				RiseAxis = FVector2D(0.0f, -1.0f);
				break;
			default:
				break;
			}

			const FIntVector SideStep(
				-StairStep.Y,
				StairStep.X,
				0
			);
			const FVector2D SideAxis(-RiseAxis.Y, RiseAxis.X);
			const FVector2D BlockCenter =
				BlockMinimum2D +
				FVector2D(SafeGridSize * 0.5f, SafeGridSize * 0.5f);
			const FVector2D RunStartCenter =
				BlockCenter -
				(
					RiseAxis *
					((StairSegmentIndex + 0.5f) * SafeGridSize)
				);
			const float LowLandingLength = SafeGridSize * 0.25f;
			const float SteppedRunLength = SafeGridSize * 1.5f;
			const int32 StepCount = 12;
			const float StepDepth =
				SteppedRunLength / static_cast<float>(StepCount);
			const float StepRise =
				SafeGridSize / static_cast<float>(StepCount);
			const float SegmentDistanceStart =
				StairSegmentIndex * SafeGridSize;
			const float SegmentDistanceEnd =
				SegmentDistanceStart + SafeGridSize;

			auto GetStairPlanPoint =
				[&](float Distance, float SideAlpha)
				{
					return
						RunStartCenter +
						(RiseAxis * Distance) +
						(
							SideAxis *
							((SideAlpha - 0.5f) * SafeGridSize)
						);
				};

			auto GetStairHeight =
				[&](float Distance)
				{
					const int32 CompletedSteps = FMath::Clamp(
						FMath::FloorToInt(
							(Distance - LowLandingLength) / StepDepth
						) + 1,
						0,
						StepCount
					);

					return
						BlockMinimum.Z +
						(CompletedSteps * StepRise);
				};

			auto IsCompatibleStairNeighbor =
				[&](
					const FIntVector& NeighborPosition,
					int32 ExpectedSegmentIndex
				)
				{
					int32 NeighborCount = 0;
					int32 NeighborIndex = 0;

					return
						HasBlock(NeighborPosition) &&
						GetBlockRotation(NeighborPosition) % 4 ==
							QuarterTurns &&
						GetContinuousStairMetadata(
							GetBlockTileType(NeighborPosition),
							NeighborCount,
							NeighborIndex
						) &&
						NeighborCount == StairSegmentCount &&
						NeighborIndex == ExpectedSegmentIndex;
				};

			auto ResolveStairBoundary =
				[&](
					const FIntVector& Direction,
					int32 ExpectedSegmentIndex,
					bool bFlatHeightMatches
				)
				{
					const FIntVector NeighborPosition =
						GridPosition + Direction;

					if (!HasBlock(NeighborPosition))
					{
						return EStairBoundaryMode::Exposed;
					}

					if (
						IsCompatibleStairNeighbor(
							NeighborPosition,
							ExpectedSegmentIndex
						) ||
						bFlatHeightMatches
						)
					{
						return EStairBoundaryMode::Seam;
					}

					int32 IgnoredStairCount = 0;
					int32 IgnoredStairIndex = 0;
					int32 IgnoredRampCount = 0;
					int32 IgnoredRampIndex = 0;
					const int32 NeighborTileType =
						GetBlockTileType(NeighborPosition);
					const bool bOrdinaryContinuousNeighbor =
						IsContinuousSurfaceBlock(NeighborPosition) &&
						!GetContinuousStairMetadata(
							NeighborTileType,
							IgnoredStairCount,
							IgnoredStairIndex
						) &&
						!GetContinuousRampMetadata(
							NeighborTileType,
							IgnoredRampCount,
							IgnoredRampIndex
						) &&
						!IsContinuousDiagonalTileType(NeighborTileType);

					return bOrdinaryContinuousNeighbor
						? EStairBoundaryMode::NeighborWall
						: EStairBoundaryMode::Exposed;
				};

			const FIntVector LowDirection(
				-StairStep.X,
				-StairStep.Y,
				0
			);
			const FIntVector NegativeSideDirection(
				-SideStep.X,
				-SideStep.Y,
				0
			);
			const bool bHasLowerFootSurface =
				StairSegmentIndex == 0 &&
				GridPosition.Z > 0 &&
				HasBlock(
					GridPosition +
					LowDirection +
					FIntVector(0, 0, -1)
				);
			const EStairBoundaryMode BoundaryModes[4] =
			{
				bHasLowerFootSurface
					? EStairBoundaryMode::Seam
					: ResolveStairBoundary(
						LowDirection,
						StairSegmentIndex - 1,
						false
					),
				ResolveStairBoundary(
					StairStep,
					StairSegmentIndex + 1,
					StairSegmentIndex == StairSegmentCount - 1
				),
				ResolveStairBoundary(
					NegativeSideDirection,
					StairSegmentIndex,
					false
				),
				ResolveStairBoundary(
					SideStep,
					StairSegmentIndex,
					false
				)
			};
			const FVector2D BoundaryNormals[4] =
			{
				-RiseAxis,
				RiseAxis,
				-SideAxis,
				SideAxis
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

			// Horizontal treads carry the same persistent path mask as flat ground.
			for (int32 IntervalIndex = 0;
				IntervalIndex < ProfileDistances.Num() - 1;
				++IntervalIndex)
			{
				const float DistanceA = ProfileDistances[IntervalIndex];
				const float DistanceB = ProfileDistances[IntervalIndex + 1];
				const float Height = GetStairHeight(
					(DistanceA + DistanceB) * 0.5f
				);
				const FVector2D PlanA0 = GetStairPlanPoint(DistanceA, 0.0f);
				const FVector2D PlanA1 = GetStairPlanPoint(DistanceA, 1.0f);
				const FVector2D PlanB0 = GetStairPlanPoint(DistanceB, 0.0f);
				const FVector2D PlanB1 = GetStairPlanPoint(DistanceB, 1.0f);
				const FVector A0(PlanA0.X, PlanA0.Y, Height);
				const FVector A1(PlanA1.X, PlanA1.Y, Height);
				const FVector B0(PlanB0.X, PlanB0.Y, Height);
				const FVector B1(PlanB1.X, PlanB1.Y, Height);
				const FColor A0Color = MakeGroundSurfaceVertexColor(
					0.0f,
					GetContinuousPathMask(
						PlanA0.X,
						PlanA0.Y,
						GridPosition.Z,
						PathBlendWidth
					)
				);
				const FColor A1Color = MakeGroundSurfaceVertexColor(
					0.0f,
					GetContinuousPathMask(
						PlanA1.X,
						PlanA1.Y,
						GridPosition.Z,
						PathBlendWidth
					)
				);
				const FColor B0Color = MakeGroundSurfaceVertexColor(
					0.0f,
					GetContinuousPathMask(
						PlanB0.X,
						PlanB0.Y,
						GridPosition.Z,
						PathBlendWidth
					)
				);
				const FColor B1Color = MakeGroundSurfaceVertexColor(
					0.0f,
					GetContinuousPathMask(
						PlanB1.X,
						PlanB1.Y,
						GridPosition.Z,
						PathBlendWidth
					)
				);

				AddContinuousTriangle(
					Section,
					A0,
					B0,
					B1,
					FVector::UpVector,
					FVector::UpVector,
					FVector::UpVector,
					FVector::UpVector,
					SafeGridSize,
					A0Color,
					B0Color,
					B1Color
				);
				AddContinuousTriangle(
					Section,
					A0,
					B1,
					A1,
					FVector::UpVector,
					FVector::UpVector,
					FVector::UpVector,
					FVector::UpVector,
					SafeGridSize,
					A0Color,
					B1Color,
					A1Color
				);
			}

			// A riser exactly on the shared cell boundary belongs to the higher
			// segment. This makes every physical vertical transition single-owner.
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

				const float LowHeight =
					BlockMinimum.Z + ((StepIndex - 1) * StepRise);
				const float HighHeight =
					BlockMinimum.Z + (StepIndex * StepRise);
				const FVector2D Plan0 =
					GetStairPlanPoint(RiserDistance, 0.0f);
				const FVector2D Plan1 =
					GetStairPlanPoint(RiserDistance, 1.0f);
				const FVector RiserNormal(
					-RiseAxis.X,
					-RiseAxis.Y,
					0.0f
				);

				AddContinuousWallQuad(
					Section,
					FVector(Plan0.X, Plan0.Y, LowHeight),
					FVector(Plan1.X, Plan1.Y, LowHeight),
					FVector(Plan1.X, Plan1.Y, HighHeight),
					FVector(Plan0.X, Plan0.Y, HighHeight),
					RiserNormal,
					RiserNormal,
					SafeGridSize
				);
			}

			if (
				GridPosition.Z > 0 &&
				!HasBlock(GridPosition + FIntVector(0, 0, -1))
				)
			{
				const FVector2D Bottom00 =
					GetStairPlanPoint(SegmentDistanceStart, 0.0f);
				const FVector2D Bottom10 =
					GetStairPlanPoint(SegmentDistanceEnd, 0.0f);
				const FVector2D Bottom11 =
					GetStairPlanPoint(SegmentDistanceEnd, 1.0f);
				const FVector2D Bottom01 =
					GetStairPlanPoint(SegmentDistanceStart, 1.0f);
				TArray<FVector> BottomBoundary;
				BottomBoundary.Add(
					FVector(Bottom00.X, Bottom00.Y, BlockMinimum.Z)
				);
				BottomBoundary.Add(
					FVector(Bottom10.X, Bottom10.Y, BlockMinimum.Z)
				);
				BottomBoundary.Add(
					FVector(Bottom11.X, Bottom11.Y, BlockMinimum.Z)
				);
				BottomBoundary.Add(
					FVector(Bottom01.X, Bottom01.Y, BlockMinimum.Z)
				);
				AddContinuousPolygon(
					Section,
					BottomBoundary,
					-FVector::UpVector,
					SafeGridSize,
					true
				);
			}

			for (int32 BoundaryIndex = 0;
				BoundaryIndex < 4;
				++BoundaryIndex)
			{
				const EStairBoundaryMode BoundaryMode =
					BoundaryModes[BoundaryIndex];

				if (BoundaryMode == EStairBoundaryMode::Seam)
				{
					continue;
				}

				const bool bLongSide = BoundaryIndex >= 2;
				const FVector OutwardNormal(
					BoundaryNormals[BoundaryIndex].X,
					BoundaryNormals[BoundaryIndex].Y,
					0.0f
				);
				const FVector WallNormal =
					BoundaryMode == EStairBoundaryMode::NeighborWall
						? -OutwardNormal
						: OutwardNormal;

				if (!bLongSide)
				{
					const float Distance =
						BoundaryIndex == 0
							? SegmentDistanceStart
							: SegmentDistanceEnd;
					const float SurfaceHeight =
						BoundaryIndex == 0
							? GetStairHeight(Distance + KINDA_SMALL_NUMBER)
							: GetStairHeight(Distance - KINDA_SMALL_NUMBER);
					const float OtherHeight =
						BoundaryMode == EStairBoundaryMode::NeighborWall
							? BlockMinimum.Z + SafeGridSize
							: BlockMinimum.Z;

					if (FMath::IsNearlyEqual(SurfaceHeight, OtherHeight))
					{
						continue;
					}

					const FVector2D Plan0 =
						GetStairPlanPoint(Distance, 0.0f);
					const FVector2D Plan1 =
						GetStairPlanPoint(Distance, 1.0f);
					const float BottomZ =
						FMath::Min(SurfaceHeight, OtherHeight);
					const float TopZ =
						FMath::Max(SurfaceHeight, OtherHeight);

					AddContinuousWallQuad(
						Section,
						FVector(Plan0.X, Plan0.Y, BottomZ),
						FVector(Plan1.X, Plan1.Y, BottomZ),
						FVector(Plan1.X, Plan1.Y, TopZ),
						FVector(Plan0.X, Plan0.Y, TopZ),
						WallNormal,
						WallNormal,
						SafeGridSize
					);
					continue;
				}

				const float SideAlpha =
					BoundaryIndex == 2 ? 0.0f : 1.0f;

				for (int32 IntervalIndex = 0;
					IntervalIndex < ProfileDistances.Num() - 1;
					++IntervalIndex)
				{
					const float DistanceA = ProfileDistances[IntervalIndex];
					const float DistanceB = ProfileDistances[IntervalIndex + 1];
					const float SurfaceHeight = GetStairHeight(
						(DistanceA + DistanceB) * 0.5f
					);
					const float OtherHeight =
						BoundaryMode == EStairBoundaryMode::NeighborWall
							? BlockMinimum.Z + SafeGridSize
							: BlockMinimum.Z;

					if (FMath::IsNearlyEqual(SurfaceHeight, OtherHeight))
					{
						continue;
					}

					const FVector2D PlanA =
						GetStairPlanPoint(DistanceA, SideAlpha);
					const FVector2D PlanB =
						GetStairPlanPoint(DistanceB, SideAlpha);
					const float BottomZ =
						FMath::Min(SurfaceHeight, OtherHeight);
					const float TopZ =
						FMath::Max(SurfaceHeight, OtherHeight);

					AddContinuousWallQuad(
						Section,
						FVector(PlanA.X, PlanA.Y, BottomZ),
						FVector(PlanB.X, PlanB.Y, BottomZ),
						FVector(PlanB.X, PlanB.Y, TopZ),
						FVector(PlanA.X, PlanA.Y, TopZ),
						WallNormal,
						WallNormal,
						SafeGridSize
					);
				}
			}

			continue;
		}

		if (IsContinuousDiagonalTileType(TileType))
		{
			FVector2D UnitTriangle[3];
			float DiagonalFraction = 1.0f;
			GetContinuousDiagonalFraction(
				TileType,
				DiagonalFraction
			);

			switch (GetBlockRotation(GridPosition) % 4)
			{
			case 0:
				UnitTriangle[0] = FVector2D(0, 0);
				UnitTriangle[1] = FVector2D(1, 0);
				UnitTriangle[2] = FVector2D(1, DiagonalFraction);
				break;

			case 1:
				UnitTriangle[0] = FVector2D(1, 0);
				UnitTriangle[1] = FVector2D(1, 1);
				UnitTriangle[2] = FVector2D(
					1.0f - DiagonalFraction,
					1
				);
				break;

			case 2:
				UnitTriangle[0] = FVector2D(1, 1);
				UnitTriangle[1] = FVector2D(0, 1);
				UnitTriangle[2] = FVector2D(
					0,
					1.0f - DiagonalFraction
				);
				break;

			default:
				UnitTriangle[0] = FVector2D(0, 1);
				UnitTriangle[1] = FVector2D(0, 0);
				UnitTriangle[2] = FVector2D(DiagonalFraction, 0);
				break;
			}

			FVector2D Triangle[3];

			for (int32 PointIndex = 0; PointIndex < 3; ++PointIndex)
			{
				Triangle[PointIndex] =
					BlockMinimum2D +
					(UnitTriangle[PointIndex] * SafeGridSize);
			}

			TArray<FContinuousBoundarySegment> DiagonalEdges;
			BuildExposedBoundarySegments(
				GridPosition,
				DiagonalEdges
			);

			auto SmoothDiagonalUnit =
				[](float Value)
				{
					const float SafeValue = FMath::Clamp(
						Value,
						0.0f,
						1.0f
					);

					return
						SafeValue *
						SafeValue *
						(3.0f - (2.0f * SafeValue));
				};

			auto EvaluateDiagonalTop =
				[&](
					const FVector2D& OriginalPoint,
					FVector& OutPosition,
					FVector& OutSurfaceNormal
				)
				{
					const int32 TopGridZ = GridPosition.Z + 1;
					FVector2D TangentWallNormal;
					const FVector2D RoundedPoint =
						RoundDiagonalTangentPoint(
							OriginalPoint,
							GridPosition.Z,
							&TangentWallNormal
						);
					OutPosition = FVector(
						RoundedPoint.X,
						RoundedPoint.Y,
						GetContinuousTopSurfaceZ(
							OriginalPoint.X,
							OriginalPoint.Y,
							TopGridZ,
							OutSurfaceNormal
						)
					);

					if (TangentWallNormal.SizeSquared() > SMALL_NUMBER)
					{
						const float LipSlope =
							ChamferDepth /
							FMath::Max(ChamferWidth, 1.0f);
						OutSurfaceNormal = FVector(
							TangentWallNormal.X * LipSlope,
							TangentWallNormal.Y * LipSlope,
							1.0f
						).GetSafeNormal();

						// Keep the tangent arc geometric and quiet. Waviness starts
						// only after both joined edges have left the fillet.
						return;
					}

					if (EdgeIrregularity <= KINDA_SMALL_NUMBER)
					{
						return;
					}

					int32 ClosestEdgeIndex = INDEX_NONE;
					float ClosestDistanceSquared =
						TNumericLimits<float>::Max();
					float ClosestEdgeAlpha = 0.0f;

					for (
						int32 EdgeIndex = 0;
						EdgeIndex < DiagonalEdges.Num();
						++EdgeIndex
						)
					{
						const FContinuousBoundarySegment& Edge =
							DiagonalEdges[EdgeIndex];

						const FVector2D EdgeVector =
							Edge.End - Edge.Start;
						const float EdgeLengthSquared =
							EdgeVector.SizeSquared();
						const float EdgeAlpha =
							EdgeLengthSquared > SMALL_NUMBER
							? FMath::Clamp(
								FVector2D::DotProduct(
									OriginalPoint - Edge.Start,
									EdgeVector
								) / EdgeLengthSquared,
								0.0f,
								1.0f
							)
							: 0.0f;
						const FVector2D ClosestPoint =
							Edge.Start + (EdgeVector * EdgeAlpha);
						const float DistanceSquared =
							(OriginalPoint - ClosestPoint).SizeSquared();

						if (DistanceSquared < ClosestDistanceSquared)
						{
							ClosestDistanceSquared = DistanceSquared;
							ClosestEdgeIndex = EdgeIndex;
							ClosestEdgeAlpha = EdgeAlpha;
						}
					}

					if (ClosestEdgeIndex == INDEX_NONE)
					{
						return;
					}

					const float DistanceFromEdge =
						FMath::Sqrt(ClosestDistanceSquared);

					if (DistanceFromEdge >= ChamferWidth)
					{
						return;
					}

					const FContinuousBoundarySegment& ClosestEdge =
						DiagonalEdges[ClosestEdgeIndex];
					const FVector2D EdgeVector =
						ClosestEdge.End - ClosestEdge.Start;
					const float EdgeLength = EdgeVector.Size();
					const FVector2D EdgeTangent =
						EdgeVector.GetSafeNormal();
					const float EndpointDistance =
						FMath::Min(
							ClosestEdgeAlpha,
							1.0f - ClosestEdgeAlpha
						) * EdgeLength;
					const float EndpointBlend = SmoothDiagonalUnit(
						(EndpointDistance - CornerRadius) /
						FMath::Max(CornerRadius, 1.0f)
					);
					const float EdgeBlend = SmoothDiagonalUnit(
						1.0f - (DistanceFromEdge / ChamferWidth)
					);
					const float TangentCoordinate =
						FVector2D::DotProduct(
							OriginalPoint,
							EdgeTangent
						);
					const float EdgeLineCoordinate =
						FVector2D::DotProduct(
							ClosestEdge.Start,
							ClosestEdge.OutwardNormal
						);
					const float EdgePhase =
						(EdgeLineCoordinate / SafeGridSize) *
						1.6180339f +
						(ClosestEdge.OutwardNormal.X * 1.2345f) +
						(ClosestEdge.OutwardNormal.Y * 2.3456f) +
						(GridPosition.Z * 0.731f);
					const float PrimaryAngle =
						(2.0f * PI * TangentCoordinate / EdgeWavelength) +
						EdgePhase;
					const float SecondaryAngle =
						(
							2.0f * PI * TangentCoordinate /
							(EdgeWavelength * 0.61f)
						) +
						(EdgePhase * 1.37f) +
						1.234f;
					const float Noise =
						(0.75f * FMath::Sin(PrimaryAngle)) +
						(0.25f * FMath::Sin(SecondaryAngle));
					const FVector2D Offset =
						ClosestEdge.OutwardNormal *
						Noise *
						EdgeIrregularity *
						EdgeBlend *
						EndpointBlend;

					OutPosition.X += Offset.X;
					OutPosition.Y += Offset.Y;
				};

			const int32 SurfaceSubdivisions =
				DetailedEdgeSubdivisions;
			const int32 SurfaceRowSize = SurfaceSubdivisions + 1;
			TArray<int32> SurfaceVertexIndices;
			SurfaceVertexIndices.Init(
				INDEX_NONE,
				SurfaceRowSize * SurfaceRowSize
			);
			TArray<int32> SurfaceCliffVertexIndices;
			SurfaceCliffVertexIndices.Init(
				INDEX_NONE,
				SurfaceRowSize * SurfaceRowSize
			);
			TArray<uint8> SurfaceGroundOwnership;
			SurfaceGroundOwnership.Init(
				0,
				SurfaceRowSize * SurfaceRowSize
			);
			TArray<FVector> SurfacePositions;
			SurfacePositions.SetNum(
				SurfaceRowSize * SurfaceRowSize
			);
			TArray<FVector> SurfaceNormals;
			SurfaceNormals.SetNum(
				SurfaceRowSize * SurfaceRowSize
			);
			TArray<FVector2D> SurfaceCliffUVs;
			SurfaceCliffUVs.SetNum(
				SurfaceRowSize * SurfaceRowSize
			);
			TArray<FVector> SurfaceCliffTangents;
			SurfaceCliffTangents.SetNum(
				SurfaceRowSize * SurfaceRowSize
			);
			TArray<FColor> SurfaceColors;
			SurfaceColors.SetNum(
				SurfaceRowSize * SurfaceRowSize
			);

			for (int32 UIndex = 0; UIndex <= SurfaceSubdivisions; ++UIndex)
			{
				for (
					int32 VIndex = 0;
					VIndex <= SurfaceSubdivisions - UIndex;
					++VIndex
					)
				{
					const float U =
						static_cast<float>(UIndex) /
						static_cast<float>(SurfaceSubdivisions);
					const float V =
						static_cast<float>(VIndex) /
						static_cast<float>(SurfaceSubdivisions);
					const FVector2D OriginalPoint =
						Triangle[0] +
						((Triangle[1] - Triangle[0]) * U) +
						((Triangle[2] - Triangle[0]) * V);
					FVector SurfacePosition;
					FVector SurfaceNormal;
					EvaluateDiagonalTop(
						OriginalPoint,
						SurfacePosition,
						SurfaceNormal
					);
					const float PathMask = 0.0f;
					const bool bGroundOwned =
						SurfacePosition.Z >=
						(
							(GridPosition.Z + 1) * SafeGridSize -
							KINDA_SMALL_NUMBER
						);
					FVector CliffTopTangent(
						-SurfaceNormal.Y,
						SurfaceNormal.X,
						0.0f
					);
					float CliffTopU = FVector::DotProduct(
						SurfacePosition,
						MakeCliffTopTangent(
							CliffTopTangent,
							SurfaceNormal
						)
					) / SafeGridSize;
					float ClosestBoundaryDistanceSquared =
						TNumericLimits<float>::Max();

					for (const FContinuousBoundarySegment& Edge : DiagonalEdges)
					{
						const FVector2D EdgeVector = Edge.End - Edge.Start;
						const float EdgeLengthSquared = EdgeVector.SizeSquared();
						const float EdgeAlpha =
							EdgeLengthSquared > SMALL_NUMBER
								? FMath::Clamp(
									FVector2D::DotProduct(
										OriginalPoint - Edge.Start,
										EdgeVector
									) / EdgeLengthSquared,
									0.0f,
									1.0f
								)
								: 0.0f;
						const FVector2D ClosestPoint =
							Edge.Start + (EdgeVector * EdgeAlpha);
						const float DistanceSquared =
							(OriginalPoint - ClosestPoint).SizeSquared();

						if (DistanceSquared < ClosestBoundaryDistanceSquared)
						{
							const FVector2D Tangent2D =
								EdgeVector.GetSafeNormal();
							ClosestBoundaryDistanceSquared = DistanceSquared;
							CliffTopTangent = FVector(
								Tangent2D.X,
								Tangent2D.Y,
								0.0f
							);
							CliffTopU = FVector2D::DotProduct(
								OriginalPoint,
								Tangent2D
							) / SafeGridSize;
						}
					}
					const float CliffTopV = FMath::Clamp(
						(
							((GridPosition.Z + 1) * SafeGridSize) -
							SurfacePosition.Z
						) /
						FMath::Max(ChamferDepth * 2.0f, 1.0f),
						0.0f,
						0.5f
					);
					const int32 SurfaceArrayIndex =
						(UIndex * SurfaceRowSize) + VIndex;
					const FColor SurfaceColor = bGroundOwned
						? MakeGroundSurfaceVertexColor(0.0f, PathMask)
						: ContinuousCliffEdgeSurfaceColor;
					const FVector2D SurfaceCliffUV =
						MakeCliffTopUVFromCoordinate(
							CliffTopU,
							CliffTopV
						);

					SurfaceGroundOwnership[SurfaceArrayIndex] =
						bGroundOwned ? 1 : 0;
					SurfacePositions[SurfaceArrayIndex] =
						SurfacePosition;
					SurfaceNormals[SurfaceArrayIndex] = SurfaceNormal;
					SurfaceCliffUVs[SurfaceArrayIndex] = SurfaceCliffUV;
					SurfaceCliffTangents[SurfaceArrayIndex] =
						CliffTopTangent;
					SurfaceColors[SurfaceArrayIndex] = SurfaceColor;
					SurfaceVertexIndices[SurfaceArrayIndex] = bGroundOwned
						? AddContinuousVertex(
							Section,
							SurfacePosition,
							SurfaceNormal,
							SafeGridSize,
							SurfaceColor
						)
						: AddContinuousVertexWithUV(
							Section,
							SurfacePosition,
							SurfaceNormal,
							SafeGridSize,
							SurfaceCliffUV,
							CliffTopTangent,
							SurfaceColor
						);
					SurfaceCliffVertexIndices[SurfaceArrayIndex] =
						bGroundOwned
							? INDEX_NONE
							: SurfaceVertexIndices[SurfaceArrayIndex];
				}
			}

			auto ResolveSurfaceVertex =
				[&](int32 SurfaceArrayIndex, bool bUseCliffUV)
				{
					if (
						!bUseCliffUV ||
						SurfaceGroundOwnership[SurfaceArrayIndex] == 0
						)
					{
						return SurfaceVertexIndices[SurfaceArrayIndex];
					}

					int32& CliffVertexIndex =
						SurfaceCliffVertexIndices[SurfaceArrayIndex];

					if (CliffVertexIndex == INDEX_NONE)
					{
						// A ground/lip seam needs two render vertices at the same
						// position. Ground triangles retain planar ground UV0 while
						// lip triangles receive the directional cliff-strip UV0.
						CliffVertexIndex = AddContinuousVertexWithUV(
							Section,
							SurfacePositions[SurfaceArrayIndex],
							SurfaceNormals[SurfaceArrayIndex],
							SafeGridSize,
							SurfaceCliffUVs[SurfaceArrayIndex],
							SurfaceCliffTangents[SurfaceArrayIndex],
							SurfaceColors[SurfaceArrayIndex]
						);
					}

					return CliffVertexIndex;
				};

			auto AddSurfaceTriangle =
				[&](int32 A, int32 B, int32 C)
				{
					const bool bUseCliffUV =
						SurfaceGroundOwnership[A] == 0 ||
						SurfaceGroundOwnership[B] == 0 ||
						SurfaceGroundOwnership[C] == 0;

					Section.Triangles.Add(
						ResolveSurfaceVertex(A, bUseCliffUV)
					);
					Section.Triangles.Add(
						ResolveSurfaceVertex(B, bUseCliffUV)
					);
					Section.Triangles.Add(
						ResolveSurfaceVertex(C, bUseCliffUV)
					);
				};

			for (int32 UIndex = 0; UIndex < SurfaceSubdivisions; ++UIndex)
			{
				for (
					int32 VIndex = 0;
					VIndex < SurfaceSubdivisions - UIndex;
					++VIndex
					)
				{
					const int32 A =
						(UIndex * SurfaceRowSize) + VIndex;
					const int32 B =
						((UIndex + 1) * SurfaceRowSize) + VIndex;
					const int32 C =
						(UIndex * SurfaceRowSize) + VIndex + 1;

					AddSurfaceTriangle(A, C, B);

					if (
						UIndex + VIndex <
						SurfaceSubdivisions - 1
						)
					{
						const int32 D =
							((UIndex + 1) * SurfaceRowSize) +
							VIndex + 1;

						AddSurfaceTriangle(B, C, D);
					}
				}
			}

			if (
				GridPosition.Z > 0 &&
				!HasBlock(GridPosition + FIntVector(0, 0, -1))
				)
			{
				TArray<FVector> BottomBoundary;

				for (int32 EdgeIndex = 0; EdgeIndex < 3; ++EdgeIndex)
				{
					for (
						int32 EdgeStep = 0;
						EdgeStep < SurfaceSubdivisions;
						++EdgeStep
						)
					{
						const float EdgeAlpha =
							static_cast<float>(EdgeStep) /
							static_cast<float>(SurfaceSubdivisions);
						const FVector2D OriginalPoint = FMath::Lerp(
							Triangle[EdgeIndex],
							Triangle[(EdgeIndex + 1) % 3],
							EdgeAlpha
						);
						const FVector2D RoundedPoint =
							RoundDiagonalTangentPoint(
								OriginalPoint,
								GridPosition.Z,
								nullptr
							);

						if (
							BottomBoundary.Num() > 0 &&
							FVector2D(
								BottomBoundary.Last().X,
								BottomBoundary.Last().Y
							).Equals(RoundedPoint, KINDA_SMALL_NUMBER)
							)
						{
							continue;
						}

						BottomBoundary.Add(
							FVector(
								RoundedPoint.X,
								RoundedPoint.Y,
								BlockMinimum.Z
							)
						);
					}
				}

				AddContinuousPolygon(
					Section,
					BottomBoundary,
					-FVector::UpVector,
					SafeGridSize,
					true
				);
			}

			for (const FContinuousBoundarySegment& Edge : DiagonalEdges)
			{
				const FVector WallNormal(
					Edge.OutwardNormal.X,
					Edge.OutwardNormal.Y,
					0.0f
				);
				TArray<FVector> WallBottomPoints;
				TArray<FVector> WallFootPoints;
				TArray<FVector> WallShoulderPoints;
				TArray<FVector> WallTopPoints;
				TArray<FVector> WallNormals;
				TArray<float> WallAlongCoordinates;
				const FVector2D EdgeTangent =
					(Edge.End - Edge.Start).GetSafeNormal();
				const FIntVector LowerDiagonalPosition =
					GridPosition + FIntVector(0, 0, -1);
				const bool bHasDiagonalCliffFoot =
					GridPosition.Z > 0 &&
					CliffFootBlendHeight > KINDA_SMALL_NUMBER &&
					IsContinuousSurfaceBlock(LowerDiagonalPosition) &&
					!IsContinuousRampBlock(LowerDiagonalPosition) &&
					!IsContinuousStairBlock(LowerDiagonalPosition);

				for (
					int32 EdgeStep = 0;
					EdgeStep <= SurfaceSubdivisions;
					++EdgeStep
					)
				{
					const float EdgeAlpha =
						static_cast<float>(EdgeStep) /
						static_cast<float>(SurfaceSubdivisions);
					const FVector2D OriginalPoint = FMath::Lerp(
						Edge.Start,
						Edge.End,
						EdgeAlpha
					);
					FVector2D TangentWallNormal;
					const FVector2D RoundedPoint =
						RoundDiagonalTangentPoint(
							OriginalPoint,
							GridPosition.Z,
							&TangentWallNormal
						);
					const FVector PointWallNormal =
						TangentWallNormal.SizeSquared() > SMALL_NUMBER
						? FVector(
							TangentWallNormal.X,
							TangentWallNormal.Y,
							0.0f
						)
						: WallNormal;
					FVector TopPoint;
					FVector IgnoredTopNormal;
					EvaluateDiagonalTop(
						OriginalPoint,
						TopPoint,
						IgnoredTopNormal
					);
					const float ShoulderZ = FMath::Max(
						BlockMinimum.Z,
						TopPoint.Z - ChamferDepth
					);

					WallBottomPoints.Add(
						FVector(
							RoundedPoint.X,
							RoundedPoint.Y,
							BlockMinimum.Z
						)
					);
					WallFootPoints.Add(
						FVector(
							RoundedPoint.X,
							RoundedPoint.Y,
							BlockMinimum.Z +
							GetCliffBaseBlendHeight(
								OriginalPoint,
								FVector2D(
									PointWallNormal.X,
									PointWallNormal.Y
								),
								ShoulderZ - BlockMinimum.Z
							)
						)
					);
					WallShoulderPoints.Add(
						FVector(
							RoundedPoint.X,
							RoundedPoint.Y,
							ShoulderZ
						)
					);
					WallTopPoints.Add(TopPoint);
					WallNormals.Add(PointWallNormal);
					WallAlongCoordinates.Add(
						FVector2D::DotProduct(
							OriginalPoint,
							EdgeTangent
						) / SafeGridSize
					);
				}

				for (
					int32 PointIndex = 0;
					PointIndex < WallBottomPoints.Num() - 1;
					++PointIndex
				)
				{
					if (bHasDiagonalCliffFoot)
					{
						const FColor ContactColor =
							MakeCliffFootVertexColor(1.0f);

						AddContinuousWallQuad(
							Section,
							WallBottomPoints[PointIndex],
							WallBottomPoints[PointIndex + 1],
							WallFootPoints[PointIndex + 1],
							WallFootPoints[PointIndex],
							WallNormals[PointIndex],
							WallNormals[PointIndex + 1],
							SafeGridSize,
							ContactColor,
							ContactColor,
							ContinuousCliffSurfaceColor,
							ContinuousCliffSurfaceColor
						);
					}

					const TArray<FVector>& LowerWallPoints =
						bHasDiagonalCliffFoot
							? WallFootPoints
							: WallBottomPoints;

					AddContinuousWallQuad(
						Section,
						LowerWallPoints[PointIndex],
						LowerWallPoints[PointIndex + 1],
						WallShoulderPoints[PointIndex + 1],
						WallShoulderPoints[PointIndex],
						WallNormals[PointIndex],
						WallNormals[PointIndex + 1],
						SafeGridSize
					);
					AddContinuousCliffTopQuad(
						Section,
						WallShoulderPoints[PointIndex],
						WallShoulderPoints[PointIndex + 1],
						WallTopPoints[PointIndex + 1],
						WallTopPoints[PointIndex],
						WallNormals[PointIndex],
						WallNormals[PointIndex + 1],
						SafeGridSize,
						1.0f,
						0.5f,
						WallAlongCoordinates[PointIndex],
						WallAlongCoordinates[PointIndex + 1],
						ContinuousCliffEdgeSurfaceColor,
						ContinuousCliffEdgeSurfaceColor,
						ContinuousCliffEdgeSurfaceColor,
						ContinuousCliffEdgeSurfaceColor
					);
				}
			}

			continue;
		}

		int32 RampSegmentCount = 0;
		int32 RampSegmentIndex = 0;

		if (
			GetContinuousRampMetadata(
				TileType,
				RampSegmentCount,
				RampSegmentIndex
			)
			)
		{
			enum class ERampBoundaryMode : uint8
			{
				Seam,
				Bank,
				NeighborWall,
				Exposed
			};

			const uint8 QuarterTurns =
				GetBlockRotation(GridPosition) % 4;
			FIntVector RampStep(1, 0, 0);
			FVector2D RiseAxis(1.0f, 0.0f);

			switch (QuarterTurns)
			{
			case 1:
				RampStep = FIntVector(0, 1, 0);
				RiseAxis = FVector2D(0.0f, 1.0f);
				break;

			case 2:
				RampStep = FIntVector(-1, 0, 0);
				RiseAxis = FVector2D(-1.0f, 0.0f);
				break;

			case 3:
				RampStep = FIntVector(0, -1, 0);
				RiseAxis = FVector2D(0.0f, -1.0f);
				break;

			default:
				break;
			}

			const FIntVector SideStep(
				-RampStep.Y,
				RampStep.X,
				0
			);
			const FVector2D SideAxis(
				-RiseAxis.Y,
				RiseAxis.X
			);
			const FVector2D BlockCenter =
				BlockMinimum2D +
				FVector2D(
					SafeGridSize * 0.5f,
					SafeGridSize * 0.5f
				);
			const FVector2D RunStartCenter =
				BlockCenter -
				(
					RiseAxis *
					(
						(RampSegmentIndex + 0.5f) *
						SafeGridSize
					)
				);
			const float RunLength =
				RampSegmentCount * SafeGridSize;
			const float RampTransitionWidth = FMath::Clamp(
				ChamferWidth,
				2.0f,
				SafeGridSize * 0.25f
			);
			const float RampEndTransitionWidth = FMath::Clamp(
				ChamferWidth * 2.0f,
				4.0f,
				SafeGridSize * 0.3f
			);
			const float RampTransitionAlpha =
				RampTransitionWidth / SafeGridSize;
			const float RampEndTransitionAlpha =
				RampEndTransitionWidth / SafeGridSize;

			auto SmoothRampUnit =
				[](float Value)
				{
					const float SafeValue = FMath::Clamp(
						Value,
						0.0f,
						1.0f
					);

					return
						SafeValue *
						SafeValue *
						(3.0f - (2.0f * SafeValue));
				};

			auto SmoothRampBankUnit =
				[](float Value)
				{
					const float SafeValue = FMath::Clamp(
						Value,
						0.0f,
						1.0f
					);

					// Quintic easing holds a visibly flatter tangent immediately
					// beside the neighboring top before the bank turns vertical.
					return
						SafeValue *
						SafeValue *
						SafeValue *
						(
							(SafeValue * ((SafeValue * 6.0f) - 15.0f)) +
							10.0f
						);
				};

			auto SmoothRampEndUnit =
				[](float Value)
				{
					const float SafeValue = FMath::Clamp(
						Value,
						0.0f,
						1.0f
					);
					const float SafeValueSquared =
						SafeValue * SafeValue;
					const float SafeValueCubed =
						SafeValueSquared * SafeValue;

					// Quintic Hermite transition: flat tangent and zero curvature
					// at the terrain endpoint, then the exact ramp tangent and zero
					// curvature where the straight middle begins.
					return
						SafeValueCubed *
						(
							6.0f -
							(8.0f * SafeValue) +
							(3.0f * SafeValueSquared)
						);
				};

			auto IsCompatibleRampNeighbor =
				[&](
					const FIntVector& NeighborPosition,
					int32 ExpectedSegmentIndex
				)
				{
					int32 NeighborCount = 0;
					int32 NeighborIndex = 0;

					return
						HasBlock(NeighborPosition) &&
						GetBlockRotation(NeighborPosition) % 4 ==
							QuarterTurns &&
						GetContinuousRampMetadata(
							GetBlockTileType(NeighborPosition),
							NeighborCount,
							NeighborIndex
						) &&
						NeighborCount == RampSegmentCount &&
						NeighborIndex == ExpectedSegmentIndex;
				};

			// Separately painted ramp runs can continue one full grid layer
			// above or below each other. Treat those joins as part of one
			// uninterrupted slope: the endpoint Hermite easing and exposed-edge
			// chamfer belong only at the true transition to flat terrain.
			const FIntVector RampRunStart =
				GridPosition - (RampStep * RampSegmentIndex);
			const FIntVector RampRunEnd =
				GridPosition +
					(
						RampStep *
							(RampSegmentCount - 1 - RampSegmentIndex)
					);
			const bool bContinuesFromLowerRamp =
				IsCompatibleRampNeighbor(
					RampRunStart -
						RampStep +
						FIntVector(0, 0, -1),
					RampSegmentCount - 1
				);
			const bool bContinuesIntoHigherRamp =
				IsCompatibleRampNeighbor(
					RampRunEnd +
						RampStep +
						FIntVector(0, 0, 1),
					0
				);

			auto ResolveBoundaryMode =
				[&](
					const FIntVector& Direction,
					int32 ExpectedRampIndex,
					bool bFlatHeightMatches
				)
				{
					const FIntVector NeighborPosition =
						GridPosition + Direction;

					if (!HasBlock(NeighborPosition))
					{
						return ERampBoundaryMode::Exposed;
					}

					if (
						IsCompatibleRampNeighbor(
							NeighborPosition,
							ExpectedRampIndex
						)
						)
					{
						return ERampBoundaryMode::Seam;
					}

					if (bFlatHeightMatches)
					{
						return ERampBoundaryMode::Seam;
					}

					// A continuous ordinary block beside a lower ramp surface needs
					// the vertical height difference closed. Keep the ramp on its
					// natural profile and emit that neighbor-facing wall explicitly
					// instead of bending the ramp edge up into a bank.
					int32 NeighborRampCount = 0;
					int32 NeighborRampIndex = 0;
					const int32 NeighborTileType =
						GetBlockTileType(NeighborPosition);
					const bool bOrdinaryContinuousNeighbor =
						IsContinuousSurfaceBlock(NeighborPosition) &&
						!GetContinuousRampMetadata(
							NeighborTileType,
							NeighborRampCount,
							NeighborRampIndex
						) &&
						!IsContinuousDiagonalTileType(NeighborTileType);

					return bOrdinaryContinuousNeighbor
						? ERampBoundaryMode::NeighborWall
						: ERampBoundaryMode::Bank;
				};

			// Low, high, negative-side, positive-side. The last ramp edge is
			// already at the neighboring block's top elevation, so it is a true
			// seam. A first segment touching terrain one layer down is also an
			// exact foot seam; treating that edge as exposed would wave it away
			// from the lower top and open a crack. A continuous ordinary neighbor
			// receives an explicit wall down to the natural ramp edge.
			const FIntVector LowDirection(
				-RampStep.X,
				-RampStep.Y,
				-RampStep.Z
			);
			const bool bHasLowerFootSurface =
				RampSegmentIndex == 0 &&
				GridPosition.Z > 0 &&
				HasBlock(
					GridPosition +
					LowDirection +
					FIntVector(0, 0, -1)
				);
			const FIntVector NegativeSideDirection(
				-SideStep.X,
				-SideStep.Y,
				-SideStep.Z
			);
			const ERampBoundaryMode BoundaryModes[4] =
			{
				bContinuesFromLowerRamp || bHasLowerFootSurface
					? ERampBoundaryMode::Seam
					: ResolveBoundaryMode(
						LowDirection,
						RampSegmentIndex - 1,
						false
					),
				bContinuesIntoHigherRamp
					? ERampBoundaryMode::Seam
					: ResolveBoundaryMode(
						RampStep,
						RampSegmentIndex + 1,
						RampSegmentIndex == RampSegmentCount - 1
					),
				ResolveBoundaryMode(
					NegativeSideDirection,
					RampSegmentIndex,
					false
				),
				ResolveBoundaryMode(
					SideStep,
					RampSegmentIndex,
					false
				)
			};
			const FVector2D BoundaryNormals[4] =
			{
				-RiseAxis,
				RiseAxis,
				-SideAxis,
				SideAxis
			};

			auto EvaluateRampBaseHeight =
				[&](float GlobalAlpha)
				{
					const float SafeAlpha = FMath::Clamp(
						GlobalAlpha,
						0.0f,
						1.0f
					);
					const float BlendAlpha = FMath::Clamp(
						RampEndTransitionWidth /
						FMath::Max(RunLength, 1.0f),
						0.001f,
						0.3f
					);
					float HeightFraction = SafeAlpha;

					if (
						!bContinuesFromLowerRamp &&
						SafeAlpha < BlendAlpha
						)
					{
						const float Alpha = SafeAlpha / BlendAlpha;
						HeightFraction =
							BlendAlpha * SmoothRampEndUnit(Alpha);
					}
					else if (
						!bContinuesIntoHigherRamp &&
						SafeAlpha > 1.0f - BlendAlpha
						)
					{
						const float Alpha =
							(SafeAlpha - (1.0f - BlendAlpha)) /
							BlendAlpha;
						HeightFraction =
							1.0f -
							(
								BlendAlpha *
								SmoothRampEndUnit(1.0f - Alpha)
							);
					}

					return
						BlockMinimum.Z +
						(HeightFraction * SafeGridSize);
				};

			auto GetRampOriginalPlan =
				[&](float U, float V)
				{
					const float GlobalDistance =
						(RampSegmentIndex + U) * SafeGridSize;

					return
						RunStartCenter +
						(RiseAxis * GlobalDistance) +
						(
							SideAxis *
							((V - 0.5f) * SafeGridSize)
						);
				};

			auto GetRampLongitudinalCliffTopU =
				[&](float U)
				{
					const float TargetDistance = FMath::Clamp(
						(RampSegmentIndex + U) * SafeGridSize,
						0.0f,
						RunLength
					);
					const int32 SampleCount = FMath::Clamp(
						FMath::CeilToInt(
							TargetDistance /
							FMath::Max(SafeGridSize / 16.0f, 1.0f)
						),
						1,
						128
					);
					float PreviousDistance = 0.0f;
					float PreviousHeight = EvaluateRampBaseHeight(0.0f);
					float SurfaceDistance = 0.0f;

					for (int32 SampleIndex = 1;
						SampleIndex <= SampleCount;
						++SampleIndex)
					{
						const float Distance =
							TargetDistance *
							(
								static_cast<float>(SampleIndex) /
								static_cast<float>(SampleCount)
							);
						const float Height = EvaluateRampBaseHeight(
							Distance / FMath::Max(RunLength, 1.0f)
						);
						SurfaceDistance += FVector2D(
							Distance - PreviousDistance,
							Height - PreviousHeight
						).Size();
						PreviousDistance = Distance;
						PreviousHeight = Height;
					}

					return SurfaceDistance / SafeGridSize;
				};

			auto GetRampWave =
				[&](
					const FVector2D& OriginalPoint,
					const FVector2D& OutwardNormal,
					const FVector2D& Tangent
				)
				{
					const float TangentCoordinate =
						FVector2D::DotProduct(OriginalPoint, Tangent);
					const float EdgeLineCoordinate =
						FVector2D::DotProduct(
							OriginalPoint,
							OutwardNormal
						);
					const float EdgePhase =
						(EdgeLineCoordinate / SafeGridSize) * 1.6180339f +
						(OutwardNormal.X * 1.2345f) +
						(OutwardNormal.Y * 2.3456f) +
						(GridPosition.Z * 0.731f);
					const float PrimaryAngle =
						(2.0f * PI * TangentCoordinate / EdgeWavelength) +
						EdgePhase;
					const float SecondaryAngle =
						(
							2.0f * PI * TangentCoordinate /
							(EdgeWavelength * 0.61f)
						) +
						(EdgePhase * 1.37f) +
						1.234f;

					return
						(0.75f * FMath::Sin(PrimaryAngle)) +
						(0.25f * FMath::Sin(SecondaryAngle));
				};

			auto EvaluateRampPosition =
				[&](float U, float V)
				{
					const float SafeU = FMath::Clamp(U, 0.0f, 1.0f);
					const float SafeV = FMath::Clamp(V, 0.0f, 1.0f);
					const float GlobalDistance =
						(RampSegmentIndex + SafeU) * SafeGridSize;
					const float GlobalAlpha =
						GlobalDistance / FMath::Max(RunLength, 1.0f);
					const FVector2D OriginalPoint =
						GetRampOriginalPlan(SafeU, SafeV);
					float Height = EvaluateRampBaseHeight(GlobalAlpha);
					const float EdgeDistances[4] =
					{
						SafeU * SafeGridSize,
						(1.0f - SafeU) * SafeGridSize,
						SafeV * SafeGridSize,
						(1.0f - SafeV) * SafeGridSize
					};
					FVector2D PlanOffset = FVector2D::ZeroVector;

					for (int32 BoundaryIndex = 0;
						BoundaryIndex < 4;
						++BoundaryIndex)
					{
						float EffectiveEdgeDistance =
							EdgeDistances[BoundaryIndex];

						if (
							BoundaryModes[BoundaryIndex] ==
								ERampBoundaryMode::Bank &&
							EdgeIrregularity > KINDA_SMALL_NUMBER
							)
						{
							const bool bLongitudinalBoundary =
								BoundaryIndex >= 2;
							const float EndpointDistance =
								bLongitudinalBoundary
								? FMath::Min(
									GlobalDistance,
									RunLength - GlobalDistance
								)
								: FMath::Min(
									SafeV,
									1.0f - SafeV
								) * SafeGridSize;
							const float EndpointBlend = SmoothRampUnit(
								EndpointDistance /
								FMath::Max(CornerRadius, 1.0f)
							);
							const FVector2D Tangent =
								bLongitudinalBoundary
								? RiseAxis
								: SideAxis;
							const float DistanceBlend = SmoothRampUnit(
								EffectiveEdgeDistance /
								RampTransitionWidth
							);
							const float BankWave = GetRampWave(
								OriginalPoint,
								BoundaryNormals[BoundaryIndex],
								Tangent
							);

							// Keep the shared grid line exact, then vary where the
							// narrow bank leaves the flat top. That creates a physical
							// wavy lip without opening a seam against the neighbor.
							EffectiveEdgeDistance = FMath::Max(
								0.0f,
								EffectiveEdgeDistance -
								(
									BankWave *
									EdgeIrregularity *
									DistanceBlend *
									EndpointBlend
								)
							);
						}

						const float BoundaryUnit =
							EffectiveEdgeDistance /
							RampTransitionWidth;
						const float BoundaryBlend =
							1.0f -
							(
								BoundaryModes[BoundaryIndex] ==
									ERampBoundaryMode::Bank
								? SmoothRampBankUnit(BoundaryUnit)
								: SmoothRampUnit(BoundaryUnit)
							);

						if (BoundaryBlend <= KINDA_SMALL_NUMBER)
						{
							continue;
						}

						if (
							BoundaryModes[BoundaryIndex] ==
							ERampBoundaryMode::Bank
							)
						{
							Height = FMath::Lerp(
								Height,
								BlockMinimum.Z + SafeGridSize,
								BoundaryBlend
							);
							continue;
						}

						if (
							BoundaryModes[BoundaryIndex] !=
							ERampBoundaryMode::Exposed
							)
						{
							continue;
						}

						const bool bLongitudinalBoundary =
							BoundaryIndex >= 2;
						const float EndpointDistance =
							bLongitudinalBoundary
							? FMath::Min(
								GlobalDistance,
								RunLength - GlobalDistance
							)
							: FMath::Min(
								SafeV,
								1.0f - SafeV
							) * SafeGridSize;
						const float EndpointBlend = SmoothRampUnit(
							EndpointDistance /
							FMath::Max(CornerRadius, 1.0f)
						);
						const float ExposedBlend =
							BoundaryBlend * EndpointBlend;

						Height = FMath::Max(
							BlockMinimum.Z,
							Height - (ChamferDepth * ExposedBlend)
						);

						if (EdgeIrregularity <= KINDA_SMALL_NUMBER)
						{
							continue;
						}

						const FVector2D Tangent =
							bLongitudinalBoundary
							? RiseAxis
							: SideAxis;
						const float Noise = GetRampWave(
							OriginalPoint,
							BoundaryNormals[BoundaryIndex],
							Tangent
						);

						PlanOffset +=
							BoundaryNormals[BoundaryIndex] *
							Noise *
							EdgeIrregularity *
							ExposedBlend;
					}

					if (
						PlanOffset.SizeSquared() >
						(EdgeIrregularity * EdgeIrregularity)
						)
					{
						PlanOffset =
							PlanOffset.GetSafeNormal() *
							EdgeIrregularity;
					}

					return FVector(
						OriginalPoint.X + PlanOffset.X,
						OriginalPoint.Y + PlanOffset.Y,
						Height
					);
				};

			auto EvaluateRampNormal =
				[&](float U, float V)
				{
					const float SampleStep = 0.0025f;
					const FVector UBefore = EvaluateRampPosition(
						FMath::Max(0.0f, U - SampleStep),
						V
					);
					const FVector UAfter = EvaluateRampPosition(
						FMath::Min(1.0f, U + SampleStep),
						V
					);
					const FVector VBefore = EvaluateRampPosition(
						U,
						FMath::Max(0.0f, V - SampleStep)
					);
					const FVector VAfter = EvaluateRampPosition(
						U,
						FMath::Min(1.0f, V + SampleStep)
					);
					FVector Normal = FVector::CrossProduct(
						UAfter - UBefore,
						VAfter - VBefore
					).GetSafeNormal();

					if (Normal.Z < 0.0f)
					{
						Normal *= -1.0f;
					}

					return Normal.IsNearlyZero()
						? FVector::UpVector
						: Normal;
				};

			auto GetRampCliffEdgeMask =
				[&](float U, float V)
				{
					const float SafeU = FMath::Clamp(U, 0.0f, 1.0f);
					const float SafeV = FMath::Clamp(V, 0.0f, 1.0f);
					const float GlobalDistance =
						(RampSegmentIndex + SafeU) * SafeGridSize;
					const float EdgeDistances[4] =
					{
						SafeU * SafeGridSize,
						(1.0f - SafeU) * SafeGridSize,
						SafeV * SafeGridSize,
						(1.0f - SafeV) * SafeGridSize
					};
					float EdgeMask = 0.0f;

					for (int32 BoundaryIndex = 0;
						BoundaryIndex < 4;
						++BoundaryIndex)
					{
						if (
							BoundaryModes[BoundaryIndex] !=
							ERampBoundaryMode::Exposed
							)
						{
							continue;
						}

						const float BoundaryBlend = 1.0f - SmoothRampUnit(
							EdgeDistances[BoundaryIndex] /
							RampTransitionWidth
						);
						const bool bLongitudinalBoundary =
							BoundaryIndex >= 2;
						const float EndpointDistance =
							bLongitudinalBoundary
								? FMath::Min(
									GlobalDistance,
									RunLength - GlobalDistance
								)
								: FMath::Min(
									SafeV,
									1.0f - SafeV
								) * SafeGridSize;
						const float EndpointBlend = SmoothRampUnit(
							EndpointDistance /
							FMath::Max(CornerRadius, 1.0f)
						);

						EdgeMask = FMath::Max(
							EdgeMask,
							BoundaryBlend * EndpointBlend
						);
					}

					return FMath::Clamp(EdgeMask, 0.0f, 1.0f);
				};

			TArray<float> RampUAlphas;
			TArray<float> RampVAlphas = RoundedSurfaceAlphas;
			for (int32 Step = 0;
				Step <= RampLongitudinalSubdivisions;
				++Step)
			{
				RampUAlphas.Add(
					static_cast<float>(Step) /
					static_cast<float>(RampLongitudinalSubdivisions)
				);
			}

			auto AddRampAlpha =
				[](TArray<float>& Alphas, float Alpha)
				{
					const float SafeAlpha = FMath::Clamp(
						Alpha,
						0.0f,
						1.0f
					);

					for (const float ExistingAlpha : Alphas)
					{
						if (FMath::IsNearlyEqual(ExistingAlpha, SafeAlpha))
						{
							return;
						}
					}

					Alphas.Add(SafeAlpha);
				};

			const float RampDetailFractions[] =
			{
				0.025f,
				0.05f,
				0.1f,
				0.2f,
				0.4f,
				0.7f,
				1.0f
			};

			for (const float DetailFraction : RampDetailFractions)
			{
				const float SideDetailAlpha =
					RampTransitionAlpha *
					DetailFraction;
				const float EndDetailAlpha =
					RampEndTransitionAlpha *
					DetailFraction;

				AddRampAlpha(RampVAlphas, SideDetailAlpha);
				AddRampAlpha(RampVAlphas, 1.0f - SideDetailAlpha);

				if (
					RampSegmentIndex == 0 &&
					!bContinuesFromLowerRamp
					)
				{
					AddRampAlpha(RampUAlphas, EndDetailAlpha);
				}

				if (
					RampSegmentIndex == RampSegmentCount - 1 &&
					!bContinuesIntoHigherRamp
					)
				{
					AddRampAlpha(RampUAlphas, 1.0f - EndDetailAlpha);
				}
			}

			RampUAlphas.Sort();
			RampVAlphas.Sort();
			const int32 RampPointCountU = RampUAlphas.Num();
			const int32 RampPointCountV = RampVAlphas.Num();
			TArray<int32> RampVertexIndices;
			RampVertexIndices.SetNum(
				RampPointCountU * RampPointCountV
			);

			for (int32 VIndex = 0;
				VIndex < RampPointCountV;
				++VIndex)
			{
				for (int32 UIndex = 0;
					UIndex < RampPointCountU;
					++UIndex)
				{
					const float U = RampUAlphas[UIndex];
					const float V = RampVAlphas[VIndex];
					const FVector2D OriginalPlan =
						GetRampOriginalPlan(U, V);
					const float PathMask = GetContinuousPathMask(
						OriginalPlan.X,
						OriginalPlan.Y,
						GridPosition.Z,
						PathBlendWidth
					);
					const float CliffEdgeMask =
						GetRampCliffEdgeMask(U, V);
					const FVector RampPosition =
						EvaluateRampPosition(U, V);
					const FVector RampNormal =
						EvaluateRampNormal(U, V);
					int32 ClosestExposedBoundary = INDEX_NONE;
					float ClosestExposedDistance =
						TNumericLimits<float>::Max();
					const float EdgeDistances[4] =
					{
						U * SafeGridSize,
						(1.0f - U) * SafeGridSize,
						V * SafeGridSize,
						(1.0f - V) * SafeGridSize
					};

					for (int32 BoundaryIndex = 0;
						BoundaryIndex < 4;
						++BoundaryIndex)
					{
						if (
							BoundaryModes[BoundaryIndex] ==
								ERampBoundaryMode::Exposed &&
							EdgeDistances[BoundaryIndex] <
								ClosestExposedDistance
							)
						{
							ClosestExposedBoundary = BoundaryIndex;
							ClosestExposedDistance =
								EdgeDistances[BoundaryIndex];
						}
					}

					FVector CliffTopTangent(
						SideAxis.X,
						SideAxis.Y,
						0.0f
					);

					if (ClosestExposedBoundary >= 2)
					{
						const float SampleStep = 0.0025f;
						const FVector Before = EvaluateRampPosition(
							FMath::Max(0.0f, U - SampleStep),
							ClosestExposedBoundary == 2 ? 0.0f : 1.0f
						);
						const FVector After = EvaluateRampPosition(
							FMath::Min(1.0f, U + SampleStep),
							ClosestExposedBoundary == 2 ? 0.0f : 1.0f
						);
						CliffTopTangent = (After - Before).GetSafeNormal();
					}

					const float GlobalDistance =
						(RampSegmentIndex + U) * SafeGridSize;
					const float GlobalAlpha =
						GlobalDistance / FMath::Max(RunLength, 1.0f);
					const float UnchamferedHeight =
						EvaluateRampBaseHeight(GlobalAlpha);
					const float CliffTopV = FMath::Clamp(
						(UnchamferedHeight - RampPosition.Z) /
							FMath::Max(ChamferDepth * 2.0f, 1.0f),
						0.0f,
						0.5f
					);
					const float CliffTopU =
						ClosestExposedBoundary >= 2
							? GetRampLongitudinalCliffTopU(U)
							: V;
					RampVertexIndices[
						(VIndex * RampPointCountU) + UIndex
					] = CliffEdgeMask > KINDA_SMALL_NUMBER
						? AddContinuousVertexWithUV(
							Section,
							RampPosition,
							RampNormal,
							SafeGridSize,
							MakeCliffTopUVFromCoordinate(
								CliffTopU,
								CliffTopV
							),
							CliffTopTangent,
							MakeCliffEdgeSurfaceVertexColor(
								CliffEdgeMask,
								PathMask
							)
						)
						: AddContinuousVertex(
							Section,
							RampPosition,
							RampNormal,
							SafeGridSize,
							MakeGroundSurfaceVertexColor(
								0.0f,
								PathMask
							)
						);
				}
			}

			for (int32 VIndex = 0;
				VIndex < RampPointCountV - 1;
				++VIndex)
			{
				for (int32 UIndex = 0;
					UIndex < RampPointCountU - 1;
					++UIndex)
				{
					const int32 BottomLeft =
						RampVertexIndices[
							(VIndex * RampPointCountU) + UIndex
						];
					const int32 BottomRight =
						RampVertexIndices[
							(VIndex * RampPointCountU) + UIndex + 1
						];
					const int32 TopLeft =
						RampVertexIndices[
							((VIndex + 1) * RampPointCountU) + UIndex
						];
					const int32 TopRight =
						RampVertexIndices[
							((VIndex + 1) * RampPointCountU) + UIndex + 1
						];

					Section.Triangles.Add(BottomLeft);
					Section.Triangles.Add(TopRight);
					Section.Triangles.Add(BottomRight);
					Section.Triangles.Add(BottomLeft);
					Section.Triangles.Add(TopLeft);
					Section.Triangles.Add(TopRight);
				}
			}

			if (
				GridPosition.Z > 0 &&
				!HasBlock(GridPosition + FIntVector(0, 0, -1))
				)
			{
				TArray<FVector> BottomBoundary;
				const FVector2D Bottom00 =
					GetRampOriginalPlan(0.0f, 0.0f);
				const FVector2D Bottom10 =
					GetRampOriginalPlan(1.0f, 0.0f);
				const FVector2D Bottom11 =
					GetRampOriginalPlan(1.0f, 1.0f);
				const FVector2D Bottom01 =
					GetRampOriginalPlan(0.0f, 1.0f);
				BottomBoundary.Add(
					FVector(Bottom00.X, Bottom00.Y, BlockMinimum.Z)
				);
				BottomBoundary.Add(
					FVector(Bottom10.X, Bottom10.Y, BlockMinimum.Z)
				);
				BottomBoundary.Add(
					FVector(Bottom11.X, Bottom11.Y, BlockMinimum.Z)
				);
				BottomBoundary.Add(
					FVector(Bottom01.X, Bottom01.Y, BlockMinimum.Z)
				);
				AddContinuousPolygon(
					Section,
					BottomBoundary,
					-FVector::UpVector,
					SafeGridSize,
					true
				);
			}

			for (int32 BoundaryIndex = 0;
				BoundaryIndex < 4;
				++BoundaryIndex)
			{
				const ERampBoundaryMode BoundaryMode =
					BoundaryModes[BoundaryIndex];

				if (BoundaryMode == ERampBoundaryMode::NeighborWall)
				{
					const bool bLongitudinalBoundary =
						BoundaryIndex >= 2;
					const TArray<float>& BoundaryAlphas =
						bLongitudinalBoundary
						? RampUAlphas
						: RampVAlphas;
					const FVector NeighborWallNormal(
						-BoundaryNormals[BoundaryIndex].X,
						-BoundaryNormals[BoundaryIndex].Y,
						0.0f
					);
					TArray<FVector> RampEdgePoints;
					TArray<FVector> NeighborTopPoints;

					for (const float BoundaryAlpha : BoundaryAlphas)
					{
						float U = BoundaryAlpha;
						float V = BoundaryIndex == 2 ? 0.0f : 1.0f;

						if (!bLongitudinalBoundary)
						{
							U = BoundaryIndex == 0 ? 0.0f : 1.0f;
							V = BoundaryAlpha;
						}

						const FVector2D OriginalPoint =
							GetRampOriginalPlan(U, V);
						FVector IgnoredNeighborTopNormal;
						const float NeighborTopZ =
							GetContinuousTopSurfaceZ(
								OriginalPoint.X,
								OriginalPoint.Y,
								GridPosition.Z + 1,
								IgnoredNeighborTopNormal
							);
						RampEdgePoints.Add(
							EvaluateRampPosition(U, V)
						);
						NeighborTopPoints.Add(
							FVector(
								OriginalPoint.X,
								OriginalPoint.Y,
								NeighborTopZ
							)
						);
					}

					for (
						int32 PointIndex = 0;
						PointIndex < RampEdgePoints.Num() - 1;
						++PointIndex
						)
					{
						const float MaximumHeightDifference = FMath::Max(
							NeighborTopPoints[PointIndex].Z -
								RampEdgePoints[PointIndex].Z,
							NeighborTopPoints[PointIndex + 1].Z -
								RampEdgePoints[PointIndex + 1].Z
						);
						const float HeightDifferenceA =
							NeighborTopPoints[PointIndex].Z -
							RampEdgePoints[PointIndex].Z;
						const float HeightDifferenceB =
							NeighborTopPoints[PointIndex + 1].Z -
							RampEdgePoints[PointIndex + 1].Z;

						if (MaximumHeightDifference <= KINDA_SMALL_NUMBER)
						{
							continue;
						}

						// At the high endpoint the closure tapers to one point. A
						// quad would contain a zero-area triangle, and its winding
						// probe would use the collapsed edge. Emit the one genuine
						// triangle explicitly instead.
						if (
							HeightDifferenceA <= KINDA_SMALL_NUMBER ||
							HeightDifferenceB <= KINDA_SMALL_NUMBER
							)
						{
							if (HeightDifferenceA > KINDA_SMALL_NUMBER)
							{
								AddContinuousTriangle(
									Section,
									RampEdgePoints[PointIndex],
									NeighborTopPoints[PointIndex],
									RampEdgePoints[PointIndex + 1],
									NeighborWallNormal,
									NeighborWallNormal,
									NeighborWallNormal,
									NeighborWallNormal,
									SafeGridSize
								);
							}
							else
							{
								AddContinuousTriangle(
									Section,
									RampEdgePoints[PointIndex],
									RampEdgePoints[PointIndex + 1],
									NeighborTopPoints[PointIndex + 1],
									NeighborWallNormal,
									NeighborWallNormal,
									NeighborWallNormal,
									NeighborWallNormal,
									SafeGridSize
								);
							}

							continue;
						}

						// This closure remains ordinary cliff wall. The neighboring
						// flat top owns the physical upper chamfer; keeping the closure
						// un-split prevents lower ramp layers from receiving a false
						// cliff-edge band.
						AddContinuousWallQuad(
							Section,
							RampEdgePoints[PointIndex],
							RampEdgePoints[PointIndex + 1],
							NeighborTopPoints[PointIndex + 1],
							NeighborTopPoints[PointIndex],
							NeighborWallNormal,
							NeighborWallNormal,
							SafeGridSize
						);
					}

					continue;
				}

				if (
					BoundaryMode != ERampBoundaryMode::Exposed
					)
				{
					continue;
				}

				const bool bLongitudinalBoundary = BoundaryIndex >= 2;
				const TArray<float>& BoundaryAlphas =
					bLongitudinalBoundary
					? RampUAlphas
					: RampVAlphas;
				TArray<FVector> WallBottomPoints;
				TArray<FVector> WallShoulderPoints;
				TArray<FVector> WallTopPoints;
				TArray<float> WallAlongCoordinates;
				const FVector WallNormal(
					BoundaryNormals[BoundaryIndex].X,
					BoundaryNormals[BoundaryIndex].Y,
					0.0f
				);

				for (const float BoundaryAlpha : BoundaryAlphas)
				{
					float U = BoundaryAlpha;
					float V = BoundaryIndex == 2 ? 0.0f : 1.0f;

					if (!bLongitudinalBoundary)
					{
						U = BoundaryIndex == 0 ? 0.0f : 1.0f;
						V = BoundaryAlpha;
					}

					const FVector2D OriginalPoint =
						GetRampOriginalPlan(U, V);
					const FVector TopPoint =
						EvaluateRampPosition(U, V);
					const float ShoulderZ = FMath::Max(
						BlockMinimum.Z,
						TopPoint.Z - ChamferDepth
					);

					WallBottomPoints.Add(
						FVector(
							OriginalPoint.X,
							OriginalPoint.Y,
							BlockMinimum.Z
						)
					);
					WallShoulderPoints.Add(
						FVector(
							OriginalPoint.X,
							OriginalPoint.Y,
							ShoulderZ
						)
					);
					WallTopPoints.Add(TopPoint);
					WallAlongCoordinates.Add(
						bLongitudinalBoundary
							? GetRampLongitudinalCliffTopU(BoundaryAlpha)
							: BoundaryAlpha
					);
				}

				for (int32 PointIndex = 0;
					PointIndex < WallTopPoints.Num() - 1;
					++PointIndex)
				{
					if (
						FMath::Max(
							WallTopPoints[PointIndex].Z,
							WallTopPoints[PointIndex + 1].Z
						) <= BlockMinimum.Z + KINDA_SMALL_NUMBER
						)
					{
						continue;
					}

					if (
						FMath::Max(
							WallShoulderPoints[PointIndex].Z,
							WallShoulderPoints[PointIndex + 1].Z
						) > BlockMinimum.Z + KINDA_SMALL_NUMBER
						)
					{
						AddContinuousWallQuad(
							Section,
							WallBottomPoints[PointIndex],
							WallBottomPoints[PointIndex + 1],
							WallShoulderPoints[PointIndex + 1],
							WallShoulderPoints[PointIndex],
							WallNormal,
							WallNormal,
							SafeGridSize
						);
					}

					AddContinuousCliffTopQuad(
						Section,
						WallShoulderPoints[PointIndex],
						WallShoulderPoints[PointIndex + 1],
						WallTopPoints[PointIndex + 1],
						WallTopPoints[PointIndex],
						WallNormal,
						WallNormal,
						SafeGridSize,
						1.0f,
						0.5f,
						WallAlongCoordinates[PointIndex],
						WallAlongCoordinates[PointIndex + 1],
						ContinuousCliffEdgeSurfaceColor,
						ContinuousCliffEdgeSurfaceColor,
						ContinuousCliffEdgeSurfaceColor,
						ContinuousCliffEdgeSurfaceColor
					);
				}
			}

			continue;
		}

		bool bConvexCorners[2][2];
		bool bDiagonalTangentCorners[2][2];
		bool bEdgeBlendCorners[2][2];
		bool bHasConvexCorner = false;
		bool bHasDiagonalTangentCorner = false;
		bool bHasEdgeBlendCorner = false;

		for (int32 CornerX = 0; CornerX <= 1; ++CornerX)
		{
			for (int32 CornerY = 0; CornerY <= 1; ++CornerY)
			{
				bConvexCorners[CornerX][CornerY] =
					IsConvexCorner(
						GridPosition,
						CornerX,
						CornerY
					);
				bHasConvexCorner =
					bHasConvexCorner ||
					bConvexCorners[CornerX][CornerY];
				FDiagonalTangentJunction DiagonalJunction;
				bDiagonalTangentCorners[CornerX][CornerY] =
					TryBuildDiagonalTangentJunction(
						FVector2D(
							(GridPosition.X + CornerX) * SafeGridSize,
							(GridPosition.Y + CornerY) * SafeGridSize
						),
						GridPosition.Z,
						DiagonalJunction
					);
				bHasDiagonalTangentCorner =
					bHasDiagonalTangentCorner ||
					bDiagonalTangentCorners[CornerX][CornerY];
				bEdgeBlendCorners[CornerX][CornerY] =
					bConvexCorners[CornerX][CornerY] ||
					bDiagonalTangentCorners[CornerX][CornerY] ||
					IsRoundableConcaveCorner(
						GridPosition.X + CornerX,
						GridPosition.Y + CornerY,
						GridPosition.Z
					) ||
					IsStackTransitionCorner(
						GridPosition.X + CornerX,
						GridPosition.Y + CornerY,
						GridPosition.Z
					);
				bHasEdgeBlendCorner =
					bHasEdgeBlendCorner ||
					bEdgeBlendCorners[CornerX][CornerY];
			}
		}

		auto RoundConvexPoint =
			[&](
				const FVector2D& OriginalPoint,
				FVector2D* OutWallNormal
			)
			{
				if (OutWallNormal)
				{
					*OutWallNormal = FVector2D::ZeroVector;
				}

				for (int32 CornerX = 0; CornerX <= 1; ++CornerX)
				{
					for (int32 CornerY = 0; CornerY <= 1; ++CornerY)
					{
						if (!bConvexCorners[CornerX][CornerY])
						{
							continue;
						}

						const float XDirection =
							CornerX == 0 ? -1.0f : 1.0f;
						const float YDirection =
							CornerY == 0 ? -1.0f : 1.0f;
						const FVector2D CornerPoint(
							BlockMinimum2D.X +
								(CornerX * SafeGridSize),
							BlockMinimum2D.Y +
								(CornerY * SafeGridSize)
						);
						const FVector2D CircleCenter =
							CornerPoint -
							FVector2D(
								XDirection * CornerRadius,
								YDirection * CornerRadius
							);
						const float SignedX =
							(OriginalPoint.X - CircleCenter.X) *
							XDirection;
						const float SignedY =
							(OriginalPoint.Y - CircleCenter.Y) *
							YDirection;

						if (
							SignedX < -KINDA_SMALL_NUMBER ||
							SignedY < -KINDA_SMALL_NUMBER ||
							SignedX > CornerRadius + KINDA_SMALL_NUMBER ||
							SignedY > CornerRadius + KINDA_SMALL_NUMBER
							)
						{
							continue;
						}

						const FVector2D CircleOffset =
							OriginalPoint - CircleCenter;
						const float SquareRadius = FMath::Max(
							FMath::Abs(CircleOffset.X),
							FMath::Abs(CircleOffset.Y)
						);

						if (CircleOffset.SizeSquared() <= SMALL_NUMBER)
						{
							return OriginalPoint;
						}

						const FVector2D RoundedPoint =
							CircleCenter +
							(
								CircleOffset.GetSafeNormal() *
								SquareRadius
							);

						if (OutWallNormal)
						{
							*OutWallNormal =
								(RoundedPoint - CircleCenter)
								.GetSafeNormal();
						}

						return RoundedPoint;
					}
				}

				return RoundDiagonalTangentPoint(
					OriginalPoint,
					GridPosition.Z,
					OutWallNormal
				);
			};

		auto BuildConvexCutoutBoundary =
			[&](
				int32 CornerX,
				int32 CornerY,
				float LocalZ,
				TArray<FVector>& OutBoundary
			)
			{
				OutBoundary.Reset();
				const float XDirection =
					CornerX == 0 ? -1.0f : 1.0f;
				const float YDirection =
					CornerY == 0 ? -1.0f : 1.0f;
				const FVector2D CornerPoint(
					BlockMinimum2D.X +
						(CornerX * SafeGridSize),
					BlockMinimum2D.Y +
						(CornerY * SafeGridSize)
				);
				const FVector2D CircleCenter =
					CornerPoint -
					FVector2D(
						XDirection * CornerRadius,
						YDirection * CornerRadius
					);
				TArray<FVector2D> Boundary2D;
				Boundary2D.Add(CornerPoint);

				for (
					int32 CornerStep = 0;
					CornerStep <= CornerSubdivisions;
					++CornerStep
					)
				{
					const float Alpha =
						static_cast<float>(CornerStep) /
						static_cast<float>(CornerSubdivisions);
					const FVector2D Direction =
						FVector2D(
							XDirection * Alpha,
							YDirection
						).GetSafeNormal();

					Boundary2D.Add(
						CircleCenter +
						(Direction * CornerRadius)
					);
				}

				for (
					int32 CornerStep = CornerSubdivisions - 1;
					CornerStep >= 0;
					--CornerStep
					)
				{
					const float Alpha =
						static_cast<float>(CornerStep) /
						static_cast<float>(CornerSubdivisions);
					const FVector2D Direction =
						FVector2D(
							XDirection,
							YDirection * Alpha
						).GetSafeNormal();

					Boundary2D.Add(
						CircleCenter +
						(Direction * CornerRadius)
					);
				}

				for (const FVector2D& BoundaryPoint : Boundary2D)
				{
					OutBoundary.Add(
						FVector(
							BoundaryPoint.X,
							BoundaryPoint.Y,
							LocalZ
						)
					);
				}
			};

		auto AddConvexCutoutPatch =
			[&](
				const TArray<FVector>& Boundary,
				const FVector& SurfaceNormal,
				bool bUseCliffFootMask
			)
			{
				if (Boundary.Num() < 3)
				{
					return;
				}

				for (
					int32 BoundaryIndex = 1;
					BoundaryIndex < Boundary.Num() - 1;
					++BoundaryIndex
					)
				{
					auto MakePatchColor =
						[&](const FVector& Position)
						{
							if (SurfaceNormal.Z <= 0.25f)
							{
								return ContinuousCliffSurfaceColor;
							}

							const float PathMask =
								GetContinuousPathMask(
									Position.X,
									Position.Y,
									GridPosition.Z,
									PathBlendWidth
								);

							return MakeGroundSurfaceVertexColor(
								bUseCliffFootMask ? 1.0f : 0.0f,
								PathMask
							);
						};
					const FColor ColorA = MakePatchColor(Boundary[0]);
					const FColor ColorB =
						MakePatchColor(Boundary[BoundaryIndex]);
					const FColor ColorC =
						MakePatchColor(Boundary[BoundaryIndex + 1]);

					AddContinuousTriangle(
						Section,
						Boundary[0],
						Boundary[BoundaryIndex],
						Boundary[BoundaryIndex + 1],
						SurfaceNormal,
						SurfaceNormal,
						SurfaceNormal,
						SurfaceNormal,
						SafeGridSize,
						ColorA,
						ColorB,
						ColorC
					);
				}
			};

		auto SmoothUnit =
			[](float Value)
			{
				const float ClampedValue = FMath::Clamp(
					Value,
					0.0f,
					1.0f
				);

				return
					ClampedValue *
					ClampedValue *
					(3.0f - (2.0f * ClampedValue));
			};

		auto GetCliffEdgeOffset =
			[&](const FVector2D& OriginalPoint)
			{
				FVector2D EdgeOffset = FVector2D::ZeroVector;

				if (EdgeIrregularity <= KINDA_SMALL_NUMBER)
				{
					return EdgeOffset;
				}

				for (const FContinuousWall& Wall : Walls)
				{
					const FIntVector AboveEdgePosition =
						GridPosition + FIntVector(0, 0, 1);
					float AboveCoverageStart = 0.0f;
					float AboveCoverageEnd = 0.0f;

					// A stacked horizontal cut covers two lower cliff edges and
					// leaves the other two exposed. Do not apply the lower lip wave
					// beneath an edge actually owned by the upper cut.
					if (
						IsContinuousSurfaceBlock(AboveEdgePosition) &&
						IsContinuousDiagonalTileType(
							GetBlockTileType(AboveEdgePosition)
						) &&
						GetContinuousCellEdgeCoverage(
							AboveEdgePosition,
							Wall.NeighborOffset,
							AboveCoverageStart,
							AboveCoverageEnd
						) &&
						AboveCoverageStart <= KINDA_SMALL_NUMBER &&
						AboveCoverageEnd >= 1.0f - KINDA_SMALL_NUMBER
						)
					{
						continue;
					}

					float CoverageStart = 0.0f;
					float CoverageEnd = 0.0f;
					const bool bHasCoverage =
						GetContinuousTerrainCoverageAcrossEdge(
							GridPosition,
							Wall.NeighborOffset,
							CoverageStart,
							CoverageEnd
						);

					const FVector2D EdgeStart =
						BlockMinimum2D +
						(Wall.BottomAOffset * SafeGridSize);
					const FVector2D EdgeEnd =
						BlockMinimum2D +
						(Wall.BottomBOffset * SafeGridSize);
					const FVector2D EdgeVector = EdgeEnd - EdgeStart;
					const float EdgeLengthSquared =
						EdgeVector.SizeSquared();

					if (EdgeLengthSquared <= SMALL_NUMBER)
					{
						continue;
					}

					const float EdgeAlpha = FMath::Clamp(
						FVector2D::DotProduct(
							OriginalPoint - EdgeStart,
							EdgeVector
						) / EdgeLengthSquared,
						0.0f,
						1.0f
					);
					float CoveredWallStart = 0.0f;
					float CoveredWallEnd = 0.0f;
					bool bHasCoveredWallInterval = false;

					if (bHasCoverage)
					{
						const bool bVerticalWall =
							FMath::IsNearlyZero(EdgeVector.X);
						const float CanonicalStart =
							bVerticalWall
								? Wall.BottomAOffset.Y
								: Wall.BottomAOffset.X;
						const float CanonicalEnd =
							bVerticalWall
								? Wall.BottomBOffset.Y
								: Wall.BottomBOffset.X;
						const float CanonicalDelta =
							CanonicalEnd - CanonicalStart;

						if (FMath::Abs(CanonicalDelta) > SMALL_NUMBER)
						{
							const float CoveredAlphaA =
								(CoverageStart - CanonicalStart) /
								CanonicalDelta;
							const float CoveredAlphaB =
								(CoverageEnd - CanonicalStart) /
								CanonicalDelta;
							CoveredWallStart = FMath::Clamp(
								FMath::Min(CoveredAlphaA, CoveredAlphaB),
								0.0f,
								1.0f
							);
							CoveredWallEnd = FMath::Clamp(
								FMath::Max(CoveredAlphaA, CoveredAlphaB),
								0.0f,
								1.0f
							);
							bHasCoveredWallInterval =
								CoveredWallEnd - CoveredWallStart >
								KINDA_SMALL_NUMBER;
						}
					}

					if (
						bHasCoveredWallInterval &&
						EdgeAlpha >=
							CoveredWallStart - KINDA_SMALL_NUMBER &&
						EdgeAlpha <=
							CoveredWallEnd + KINDA_SMALL_NUMBER
						)
					{
						continue;
					}
					const FVector2D ClosestPoint =
						EdgeStart + (EdgeVector * EdgeAlpha);
					const FVector2D FromEdge =
						OriginalPoint - ClosestPoint;
					const FVector2D OutwardNormal(
						Wall.Normal.X,
						Wall.Normal.Y
					);

					if (
						FVector2D::DotProduct(
							FromEdge,
							OutwardNormal
						) > KINDA_SMALL_NUMBER
						)
					{
						continue;
					}

					const float DistanceFromEdge = FromEdge.Size();

					// Keep the sampled inner chamfer row eligible for a reduced
					// share of the existing edge wave. Previously the offset faded
					// to exactly zero here, exposing a ruler-straight material and
					// geometry boundary around otherwise irregular cliffs.
					if (
						DistanceFromEdge >
							ChamferWidth + KINDA_SMALL_NUMBER
						)
					{
						continue;
					}

					const int32 CornerAX =
						FMath::RoundToInt(Wall.BottomAOffset.X);
					const int32 CornerAY =
						FMath::RoundToInt(Wall.BottomAOffset.Y);
					const int32 CornerBX =
						FMath::RoundToInt(Wall.BottomBOffset.X);
					const int32 CornerBY =
						FMath::RoundToInt(Wall.BottomBOffset.Y);
					const bool bCornerAtA =
						bEdgeBlendCorners[CornerAX][CornerAY];
					const bool bCornerAtB =
						bEdgeBlendCorners[CornerBX][CornerBY];
					float EndpointBlend = 1.0f;

					if (bCornerAtA)
					{
						EndpointBlend *= SmoothUnit(
							(EdgeAlpha - CornerAlpha) /
							FMath::Max(
								CornerAlpha,
								KINDA_SMALL_NUMBER
							)
						);
					}

					if (bCornerAtB)
					{
						EndpointBlend *= SmoothUnit(
							(
								(1.0f - CornerAlpha) -
								EdgeAlpha
							) /
							FMath::Max(
								CornerAlpha,
								KINDA_SMALL_NUMBER
							)
						);
					}

					if (bHasCoveredWallInterval)
					{
						const float DistanceFromCoveredInterval =
							EdgeAlpha < CoveredWallStart
								? CoveredWallStart - EdgeAlpha
								: EdgeAlpha - CoveredWallEnd;
						EndpointBlend *= SmoothUnit(
							DistanceFromCoveredInterval /
							FMath::Max(
								CornerAlpha,
								KINDA_SMALL_NUMBER
							)
						);
					}

					const float OuterEdgeBlend = SmoothUnit(
						1.0f - FMath::Clamp(
							DistanceFromEdge / ChamferWidth,
							0.0f,
							1.0f
						)
					);
					const float InnerLipBlend = FMath::Lerp(
						0.5f,
						1.0f,
						OuterEdgeBlend
					);
					const float EdgeBlend =
						InnerLipBlend * EndpointBlend;

					if (EdgeBlend <= KINDA_SMALL_NUMBER)
					{
						continue;
					}

					const FVector2D EdgeTangent =
						EdgeVector.GetSafeNormal();
					const float TangentCoordinate =
						FVector2D::DotProduct(
							OriginalPoint,
							EdgeTangent
						);
					const float EdgeLineCoordinate =
						FVector2D::DotProduct(
							EdgeStart,
							OutwardNormal
						);
					const float EdgePhase =
						(EdgeLineCoordinate / SafeGridSize) *
						1.6180339f +
						(Wall.Normal.X * 1.2345f) +
						(Wall.Normal.Y * 2.3456f) +
						(GridPosition.Z * 0.731f);
					const float PrimaryAngle =
						(
							2.0f * PI * TangentCoordinate /
							EdgeWavelength
						) + EdgePhase;
					const float SecondaryAngle =
						(
							2.0f * PI * TangentCoordinate /
							(EdgeWavelength * 0.61f)
						) +
						(EdgePhase * 1.37f) +
						1.234f;
					const float Noise =
						(0.75f * FMath::Sin(PrimaryAngle)) +
						(0.25f * FMath::Sin(SecondaryAngle));

					EdgeOffset +=
						OutwardNormal *
						Noise *
						EdgeIrregularity *
						EdgeBlend;
				}

				if (
					EdgeOffset.SizeSquared() >
					(EdgeIrregularity * EdgeIrregularity)
					)
				{
					EdgeOffset =
						EdgeOffset.GetSafeNormal() *
						EdgeIrregularity;
				}

				return EdgeOffset;
			};

		const FIntVector AbovePosition =
			GridPosition + FIntVector(0, 0, 1);
		const bool bHasAboveBlock = HasBlock(AbovePosition);

		// A supported horizontal cut occupies only its triangular footprint.
		// The support block's ordinary top is otherwise suppressed by the mere
		// presence of that upper cell, leaving the triangle complement open at
		// the bottom of the diagonal wall. Emit only that visible complement,
		// with its diagonal sampled through the same tangent-rounding function
		// as the upper wall bottom and its outside edges sampled through the same
		// convex/tangent rounding function as this lower block's wall tops. No
		// buried full-square face is introduced.
		if (
			bHasAboveBlock &&
			IsContinuousSurfaceBlock(AbovePosition) &&
			IsContinuousDiagonalTileType(
				GetBlockTileType(AbovePosition)
			)
			)
		{
			float DiagonalFraction = 1.0f;
			GetContinuousDiagonalFraction(
				GetBlockTileType(AbovePosition),
				DiagonalFraction
			);
			const FVector2D C00(BlockMinimum.X, BlockMinimum.Y);
			const FVector2D C10(
				BlockMinimum.X + SafeGridSize,
				BlockMinimum.Y
			);
			const FVector2D C11(
				BlockMinimum.X + SafeGridSize,
				BlockMinimum.Y + SafeGridSize
			);
			const FVector2D C01(
				BlockMinimum.X,
				BlockMinimum.Y + SafeGridSize
			);
			const float SupportTopZ =
				(GridPosition.Z + 1) * SafeGridSize;
			const int32 SupportDiagonalSubdivisions =
				DetailedEdgeSubdivisions;
			TArray<FVector> SupportBoundary;

			auto AddSupportPoint =
				[&](const FVector2D& Point)
				{
					const FVector Position(Point.X, Point.Y, SupportTopZ);

					if (
						SupportBoundary.Num() == 0 ||
						!SupportBoundary.Last().Equals(
							Position,
							KINDA_SMALL_NUMBER
						)
						)
					{
						SupportBoundary.Add(Position);
					}
				};

			auto AddRoundedSupportDiagonal =
				[&](const FVector2D& Start, const FVector2D& End)
				{
					for (int32 Step = 0;
						Step <= SupportDiagonalSubdivisions;
						++Step)
					{
						const float Alpha =
							static_cast<float>(Step) /
							static_cast<float>(SupportDiagonalSubdivisions);
						const FVector2D OriginalPoint = FMath::Lerp(
							Start,
							End,
							Alpha
						);
						AddSupportPoint(
							RoundDiagonalTangentPoint(
								OriginalPoint,
								AbovePosition.Z,
								nullptr
							)
						);
					}
				};

			const TArray<float>& SupportOuterEdgeAlphas =
				bHasEdgeBlendCorner
					? RoundedSurfaceAlphas
					: StraightEdgeSurfaceAlphas;

			auto AddRoundedSupportOuterEdge =
				[&](const FVector2D& Start, const FVector2D& End)
				{
					if (Start.Equals(End, KINDA_SMALL_NUMBER))
					{
						AddSupportPoint(
							RoundConvexPoint(Start, nullptr)
						);
						return;
					}

					for (const float Alpha : SupportOuterEdgeAlphas)
					{
						const FVector2D OriginalPoint = FMath::Lerp(
							Start,
							End,
							Alpha
						);
						FVector2D SupportPoint =
							RoundConvexPoint(OriginalPoint, nullptr);
						SupportPoint += GetCliffEdgeOffset(OriginalPoint);
						AddSupportPoint(SupportPoint);
					}
				};

			const int32 SupportRotation =
				GetBlockRotation(AbovePosition) % 4;
			FVector2D SupportDiagonalStart = C00;
			FVector2D SupportDiagonalEnd = C11;
			FVector2D SupportOuterCorner = C01;

			switch (SupportRotation)
			{
			case 0:
			{
				const FVector2D DiagonalEnd(
					BlockMinimum.X + SafeGridSize,
					BlockMinimum.Y + (DiagonalFraction * SafeGridSize)
				);
				SupportDiagonalStart = C00;
				SupportDiagonalEnd = DiagonalEnd;
				SupportOuterCorner = C01;
				AddRoundedSupportDiagonal(C00, DiagonalEnd);
				AddRoundedSupportOuterEdge(DiagonalEnd, C11);
				AddRoundedSupportOuterEdge(C11, C01);
				AddRoundedSupportOuterEdge(C01, C00);
				break;
			}

			case 1:
			{
				const FVector2D DiagonalEnd(
					BlockMinimum.X +
						((1.0f - DiagonalFraction) * SafeGridSize),
					BlockMinimum.Y + SafeGridSize
				);
				SupportDiagonalStart = C10;
				SupportDiagonalEnd = DiagonalEnd;
				SupportOuterCorner = C00;
				AddRoundedSupportOuterEdge(C00, C10);
				AddRoundedSupportDiagonal(C10, DiagonalEnd);
				AddRoundedSupportOuterEdge(DiagonalEnd, C01);
				AddRoundedSupportOuterEdge(C01, C00);
				break;
			}

			case 2:
			{
				const FVector2D DiagonalEnd(
					BlockMinimum.X,
					BlockMinimum.Y +
						((1.0f - DiagonalFraction) * SafeGridSize)
				);
				SupportDiagonalStart = C11;
				SupportDiagonalEnd = DiagonalEnd;
				SupportOuterCorner = C10;
				AddRoundedSupportOuterEdge(C00, C10);
				AddRoundedSupportOuterEdge(C10, C11);
				AddRoundedSupportDiagonal(C11, DiagonalEnd);
				AddRoundedSupportOuterEdge(DiagonalEnd, C00);
				break;
			}

			default:
			{
				const FVector2D DiagonalEnd(
					BlockMinimum.X +
						(DiagonalFraction * SafeGridSize),
					BlockMinimum.Y
				);
				SupportDiagonalStart = C01;
				SupportDiagonalEnd = DiagonalEnd;
				SupportOuterCorner = C11;
				AddRoundedSupportOuterEdge(C10, C11);
				AddRoundedSupportOuterEdge(C11, C01);
				AddRoundedSupportDiagonal(C01, DiagonalEnd);
				AddRoundedSupportOuterEdge(DiagonalEnd, C10);
				break;
			}
			}

			if (
				SupportBoundary.Num() > 1 &&
				SupportBoundary[0].Equals(
					SupportBoundary.Last(),
					KINDA_SMALL_NUMBER
				)
				)
			{
				SupportBoundary.Pop();
			}

			const int32 DiagnosticFirstVertex = Section.Vertices.Num();
			const int32 DiagnosticFirstTriangleIndex = Section.Triangles.Num();
			const int32 SupportPointCount =
				SupportOuterEdgeAlphas.Num();
			const int32 SupportArraySize =
				SupportPointCount * SupportPointCount;
			TArray<int32> SupportVertexIndices;
			SupportVertexIndices.Init(INDEX_NONE, SupportArraySize);
			TArray<int32> SupportCliffVertexIndices;
			SupportCliffVertexIndices.Init(INDEX_NONE, SupportArraySize);
			TArray<uint8> SupportGroundOwnership;
			SupportGroundOwnership.Init(0, SupportArraySize);
			TArray<FVector> SupportPositions;
			SupportPositions.SetNum(SupportArraySize);
			TArray<FVector> SupportNormals;
			SupportNormals.SetNum(SupportArraySize);
			TArray<FVector2D> SupportCliffUVs;
			SupportCliffUVs.SetNum(SupportArraySize);
			TArray<FVector> SupportCliffTangents;
			SupportCliffTangents.SetNum(SupportArraySize);
			TArray<FColor> SupportColors;
			SupportColors.SetNum(SupportArraySize);
			TArray<FContinuousBoundarySegment> SupportExposedEdges;
			BuildExposedBoundarySegments(
				GridPosition,
				SupportExposedEdges
			);

			const FVector2D SupportOuterEdgeA =
				SupportOuterCorner - SupportDiagonalEnd;
			const FVector2D SupportOuterEdgeB =
				SupportDiagonalStart - SupportOuterCorner;
			const FVector2D SupportOuterNormalA(
				SupportOuterEdgeA.Y,
				-SupportOuterEdgeA.X
			);
			const FVector2D SupportOuterNormalB(
				SupportOuterEdgeB.Y,
				-SupportOuterEdgeB.X
			);
			const FVector2D SafeSupportOuterNormalA =
				SupportOuterNormalA.GetSafeNormal();
			const FVector2D SafeSupportOuterNormalB =
				SupportOuterNormalB.GetSafeNormal();

			// The lower complement touches only two cell edges. Discard the
			// other two lower-wall boundaries because those lie beneath the
			// occupied half of the upper diagonal and must remain buried.
			for (
				int32 EdgeIndex = SupportExposedEdges.Num() - 1;
				EdgeIndex >= 0;
				--EdgeIndex
				)
			{
				const FContinuousBoundarySegment& Edge =
					SupportExposedEdges[EdgeIndex];
				const bool bOwnsSupportOuterEdge =
					!Edge.bDiagonal &&
					(
						FVector2D::DotProduct(
							Edge.OutwardNormal,
							SafeSupportOuterNormalA
						) > 0.99f ||
						FVector2D::DotProduct(
							Edge.OutwardNormal,
							SafeSupportOuterNormalB
						) > 0.99f
					);

				if (!bOwnsSupportOuterEdge)
				{
					SupportExposedEdges.RemoveAt(EdgeIndex);
				}
			}

			auto IsInsideSupportComplement =
				[SupportRotation](float UnitX, float UnitY)
				{
					const float Tolerance = 0.0001f;

					switch (SupportRotation)
					{
					case 0:
						return UnitY >= UnitX - Tolerance;
					case 1:
						return UnitX + UnitY <= 1.0f + Tolerance;
					case 2:
						return UnitY <= UnitX + Tolerance;
					default:
						return UnitX + UnitY >= 1.0f - Tolerance;
					}
				};

			const FVector2D SupportDiagonalVector =
				SupportDiagonalEnd - SupportDiagonalStart;
			const float SupportDiagonalLengthSquared =
				SupportDiagonalVector.SizeSquared();

			auto EvaluateSupportDiagonalPoint =
				[&](const FVector2D& OriginalPoint)
				{
					const float DiagonalAlpha =
						SupportDiagonalLengthSquared > SMALL_NUMBER
							? FMath::Clamp(
								FVector2D::DotProduct(
									OriginalPoint - SupportDiagonalStart,
									SupportDiagonalVector
								) / SupportDiagonalLengthSquared,
								0.0f,
								1.0f
							)
							: 0.0f;
					const float ScaledAlpha =
						DiagonalAlpha * SupportDiagonalSubdivisions;
					const int32 SegmentIndex = FMath::Clamp(
						FMath::FloorToInt(ScaledAlpha),
						0,
						SupportDiagonalSubdivisions - 1
					);
					const float SegmentAlpha = FMath::Clamp(
						ScaledAlpha - SegmentIndex,
						0.0f,
						1.0f
					);
					const float AlphaA =
						static_cast<float>(SegmentIndex) /
						static_cast<float>(SupportDiagonalSubdivisions);
					const float AlphaB =
						static_cast<float>(SegmentIndex + 1) /
						static_cast<float>(SupportDiagonalSubdivisions);
					const FVector2D RoundedA =
						RoundDiagonalTangentPoint(
							FMath::Lerp(
								SupportDiagonalStart,
								SupportDiagonalEnd,
								AlphaA
							),
							AbovePosition.Z,
							nullptr
						);
					const FVector2D RoundedB =
						RoundDiagonalTangentPoint(
							FMath::Lerp(
								SupportDiagonalStart,
								SupportDiagonalEnd,
								AlphaB
							),
							AbovePosition.Z,
							nullptr
						);

					// Extra grid samples lie on the exact piecewise-linear boundary
					// already used by the upper wall, so adding lip density cannot
					// reintroduce a diagonal T-junction.
					return FMath::Lerp(RoundedA, RoundedB, SegmentAlpha);
				};

			for (int32 SupportY = 0;
				SupportY < SupportPointCount;
				++SupportY)
			{
				for (int32 SupportX = 0;
					SupportX < SupportPointCount;
					++SupportX)
				{
					const float UnitX = SupportOuterEdgeAlphas[SupportX];
					const float UnitY = SupportOuterEdgeAlphas[SupportY];

					if (!IsInsideSupportComplement(UnitX, UnitY))
					{
						continue;
					}

					const FVector2D OriginalPoint(
						BlockMinimum.X + (UnitX * SafeGridSize),
						BlockMinimum.Y + (UnitY * SafeGridSize)
					);
					const float DiagonalAlpha =
						SupportDiagonalLengthSquared > SMALL_NUMBER
							? FMath::Clamp(
								FVector2D::DotProduct(
									OriginalPoint - SupportDiagonalStart,
									SupportDiagonalVector
								) / SupportDiagonalLengthSquared,
								0.0f,
								1.0f
							)
							: 0.0f;
					const FVector2D ClosestDiagonalPoint =
						SupportDiagonalStart +
						(SupportDiagonalVector * DiagonalAlpha);
					const bool bOnDiagonalBoundary =
						(OriginalPoint - ClosestDiagonalPoint).SizeSquared() <=
							0.0001f;
					FVector2D SurfacePoint = bOnDiagonalBoundary
						? EvaluateSupportDiagonalPoint(OriginalPoint)
						: RoundConvexPoint(OriginalPoint, nullptr);

					if (!bOnDiagonalBoundary)
					{
						SurfacePoint += GetCliffEdgeOffset(OriginalPoint);
					}

					FVector SurfaceNormal;
					const float LocalZ = GetContinuousTopSurfaceZ(
						OriginalPoint.X,
						OriginalPoint.Y,
						GridPosition.Z + 1,
						SurfaceNormal
					);
					const bool bGroundOwned =
						LocalZ >= SupportTopZ - KINDA_SMALL_NUMBER;
					const FVector SurfacePosition(
						SurfacePoint.X,
						SurfacePoint.Y,
						LocalZ
					);
					FVector CliffTopTangent(
						-SurfaceNormal.Y,
						SurfaceNormal.X,
						0.0f
					);
					float CliffTopU = FVector::DotProduct(
						SurfacePosition,
						MakeCliffTopTangent(
							CliffTopTangent,
							SurfaceNormal
						)
					) / SafeGridSize;
					float ClosestBoundaryDistanceSquared =
						TNumericLimits<float>::Max();

					for (const FContinuousBoundarySegment& Edge :
						SupportExposedEdges)
					{
						const FVector2D EdgeVector = Edge.End - Edge.Start;
						const float EdgeLengthSquared =
							EdgeVector.SizeSquared();
						const float EdgeAlpha =
							EdgeLengthSquared > SMALL_NUMBER
								? FMath::Clamp(
									FVector2D::DotProduct(
										OriginalPoint - Edge.Start,
										EdgeVector
									) / EdgeLengthSquared,
									0.0f,
									1.0f
								)
								: 0.0f;
						const FVector2D ClosestPoint =
							Edge.Start + (EdgeVector * EdgeAlpha);
						const float DistanceSquared =
							(OriginalPoint - ClosestPoint).SizeSquared();

						if (DistanceSquared < ClosestBoundaryDistanceSquared)
						{
							const FVector2D Tangent2D =
								EdgeVector.GetSafeNormal();
							ClosestBoundaryDistanceSquared = DistanceSquared;
							CliffTopTangent = FVector(
								Tangent2D.X,
								Tangent2D.Y,
								0.0f
							);
							CliffTopU = FVector2D::DotProduct(
								OriginalPoint,
								Tangent2D
							) / SafeGridSize;
						}
					}

					const float CliffTopV = FMath::Clamp(
						(SupportTopZ - LocalZ) /
							FMath::Max(ChamferDepth * 2.0f, 1.0f),
						0.0f,
						0.5f
					);
					const float PathMask = GetContinuousPathMask(
						OriginalPoint.X,
						OriginalPoint.Y,
						GridPosition.Z,
						PathBlendWidth
					);
					const FColor SurfaceColor = bGroundOwned
						? MakeGroundSurfaceVertexColor(0.0f, PathMask)
						: ContinuousCliffEdgeSurfaceColor;
					const FVector2D SurfaceCliffUV =
						MakeCliffTopUVFromCoordinate(
							CliffTopU,
							CliffTopV
						);
					const int32 ArrayIndex =
						(SupportY * SupportPointCount) + SupportX;

					SupportGroundOwnership[ArrayIndex] =
						bGroundOwned ? 1 : 0;
					SupportPositions[ArrayIndex] = SurfacePosition;
					SupportNormals[ArrayIndex] = SurfaceNormal;
					SupportCliffUVs[ArrayIndex] = SurfaceCliffUV;
					SupportCliffTangents[ArrayIndex] = CliffTopTangent;
					SupportColors[ArrayIndex] = SurfaceColor;
					SupportVertexIndices[ArrayIndex] = bGroundOwned
						? AddContinuousVertex(
							Section,
							SurfacePosition,
							SurfaceNormal,
							SafeGridSize,
							SurfaceColor
						)
						: AddContinuousVertexWithUV(
							Section,
							SurfacePosition,
							SurfaceNormal,
							SafeGridSize,
							SurfaceCliffUV,
							CliffTopTangent,
							SurfaceColor
						);
					SupportCliffVertexIndices[ArrayIndex] = bGroundOwned
						? INDEX_NONE
						: SupportVertexIndices[ArrayIndex];
				}
			}

			auto ResolveSupportVertex =
				[&](int32 ArrayIndex, bool bUseCliffUV)
				{
					if (
						!bUseCliffUV ||
						SupportGroundOwnership[ArrayIndex] == 0
						)
					{
						return SupportVertexIndices[ArrayIndex];
					}

					int32& CliffVertexIndex =
						SupportCliffVertexIndices[ArrayIndex];

					if (CliffVertexIndex == INDEX_NONE)
					{
						CliffVertexIndex = AddContinuousVertexWithUV(
							Section,
							SupportPositions[ArrayIndex],
							SupportNormals[ArrayIndex],
							SafeGridSize,
							SupportCliffUVs[ArrayIndex],
							SupportCliffTangents[ArrayIndex],
							SupportColors[ArrayIndex]
						);
					}

					return CliffVertexIndex;
				};

			const int32 ExpectedSupportTriangleCount =
				(SupportPointCount - 1) * (SupportPointCount - 1);
			auto AddSupportTriangle =
				[&](int32 A, int32 B, int32 C)
				{
					if (
						A == INDEX_NONE ||
						B == INDEX_NONE ||
						C == INDEX_NONE
						)
					{
						return;
					}

					const bool bUseCliffUV =
						SupportGroundOwnership[A] == 0 ||
						SupportGroundOwnership[B] == 0 ||
						SupportGroundOwnership[C] == 0;
					Section.Triangles.Add(
						ResolveSupportVertex(A, bUseCliffUV)
					);
					Section.Triangles.Add(
						ResolveSupportVertex(B, bUseCliffUV)
					);
					Section.Triangles.Add(
						ResolveSupportVertex(C, bUseCliffUV)
					);
				};

			for (int32 SupportY = 0;
				SupportY < SupportPointCount - 1;
				++SupportY)
			{
				for (int32 SupportX = 0;
					SupportX < SupportPointCount - 1;
					++SupportX)
				{
					const int32 BottomLeft =
						(SupportY * SupportPointCount) + SupportX;
					const int32 BottomRight = BottomLeft + 1;
					const int32 TopLeft =
						((SupportY + 1) * SupportPointCount) + SupportX;
					const int32 TopRight = TopLeft + 1;

					if ((SupportRotation % 2) == 0)
					{
						AddSupportTriangle(
							SupportVertexIndices[BottomLeft] == INDEX_NONE
								? INDEX_NONE : BottomLeft,
							SupportVertexIndices[TopRight] == INDEX_NONE
								? INDEX_NONE : TopRight,
							SupportVertexIndices[BottomRight] == INDEX_NONE
								? INDEX_NONE : BottomRight
						);
						AddSupportTriangle(
							SupportVertexIndices[BottomLeft] == INDEX_NONE
								? INDEX_NONE : BottomLeft,
							SupportVertexIndices[TopLeft] == INDEX_NONE
								? INDEX_NONE : TopLeft,
							SupportVertexIndices[TopRight] == INDEX_NONE
								? INDEX_NONE : TopRight
						);
					}
					else
					{
						AddSupportTriangle(
							SupportVertexIndices[BottomLeft] == INDEX_NONE
								? INDEX_NONE : BottomLeft,
							SupportVertexIndices[TopLeft] == INDEX_NONE
								? INDEX_NONE : TopLeft,
							SupportVertexIndices[BottomRight] == INDEX_NONE
								? INDEX_NONE : BottomRight
						);
						AddSupportTriangle(
							SupportVertexIndices[BottomRight] == INDEX_NONE
								? INDEX_NONE : BottomRight,
							SupportVertexIndices[TopLeft] == INDEX_NONE
								? INDEX_NONE : TopLeft,
							SupportVertexIndices[TopRight] == INDEX_NONE
								? INDEX_NONE : TopRight
						);
					}
				}
			}

			if (bDebugContinuousSlantSupports)
			{
				const int32 Rotation = GetBlockRotation(AbovePosition) % 4;
				const int32 AddedVertexCount =
					Section.Vertices.Num() - DiagnosticFirstVertex;
				const int32 AddedTriangleCount =
					(Section.Triangles.Num() - DiagnosticFirstTriangleIndex) / 3;
				const int32 ExpectedTriangleCount =
					ExpectedSupportTriangleCount;

				auto Cross2D =
					[](const FVector& A, const FVector& B, const FVector& C)
					{
						return
							((B.X - A.X) * (C.Y - A.Y)) -
							((B.Y - A.Y) * (C.X - A.X));
					};

				auto PointInsideBoundary =
					[&](const FVector& Point)
					{
						bool bInside = false;
						for (int32 I = 0, J = SupportBoundary.Num() - 1;
							I < SupportBoundary.Num(); J = I++)
						{
							const FVector& A = SupportBoundary[I];
							const FVector& B = SupportBoundary[J];
							const bool bCrosses =
								((A.Y > Point.Y) != (B.Y > Point.Y)) &&
								(Point.X <
									((B.X - A.X) * (Point.Y - A.Y) /
										(B.Y - A.Y)) + A.X);
							if (bCrosses)
							{
								bInside = !bInside;
							}
						}
						return bInside;
					};

				auto SegmentsIntersect =
					[&](
						const FVector& A,
						const FVector& B,
						const FVector& C,
						const FVector& D)
					{
						const float AB_C = Cross2D(A, B, C);
						const float AB_D = Cross2D(A, B, D);
						const float CD_A = Cross2D(C, D, A);
						const float CD_B = Cross2D(C, D, B);
						return
							((AB_C > KINDA_SMALL_NUMBER && AB_D < -KINDA_SMALL_NUMBER) ||
							 (AB_C < -KINDA_SMALL_NUMBER && AB_D > KINDA_SMALL_NUMBER)) &&
							((CD_A > KINDA_SMALL_NUMBER && CD_B < -KINDA_SMALL_NUMBER) ||
							 (CD_A < -KINDA_SMALL_NUMBER && CD_B > KINDA_SMALL_NUMBER));
					};

				float BoundaryDoubleArea = 0.0f;
				int32 DuplicatePointCount = 0;
				int32 SelfIntersectionCount = 0;
				for (int32 I = 0; I < SupportBoundary.Num(); ++I)
				{
					const int32 NextI = (I + 1) % SupportBoundary.Num();
					BoundaryDoubleArea +=
						(SupportBoundary[I].X * SupportBoundary[NextI].Y) -
						(SupportBoundary[NextI].X * SupportBoundary[I].Y);

					for (int32 J = I + 1; J < SupportBoundary.Num(); ++J)
					{
						if (SupportBoundary[I].Equals(
							SupportBoundary[J], KINDA_SMALL_NUMBER))
						{
							++DuplicatePointCount;
						}
					}

					for (int32 J = I + 1; J < SupportBoundary.Num(); ++J)
					{
						const int32 NextJ = (J + 1) % SupportBoundary.Num();
						if (J == I || NextI == J || NextJ == I)
						{
							continue;
						}
						if (SegmentsIntersect(
							SupportBoundary[I], SupportBoundary[NextI],
							SupportBoundary[J], SupportBoundary[NextJ]))
						{
							++SelfIntersectionCount;
						}
					}
				}

				int32 DegenerateTriangleCount = 0;
				int32 OutsideTriangleCount = 0;
				float EmittedDoubleArea = 0.0f;
				for (int32 TriangleIndex = DiagnosticFirstTriangleIndex;
					TriangleIndex + 2 < Section.Triangles.Num();
					TriangleIndex += 3)
				{
					const int32 IA = Section.Triangles[TriangleIndex];
					const int32 IB = Section.Triangles[TriangleIndex + 1];
					const int32 IC = Section.Triangles[TriangleIndex + 2];
					if (!Section.Vertices.IsValidIndex(IA) ||
						!Section.Vertices.IsValidIndex(IB) ||
						!Section.Vertices.IsValidIndex(IC))
					{
						++DegenerateTriangleCount;
						continue;
					}

					const FVector& A = Section.Vertices[IA];
					const FVector& B = Section.Vertices[IB];
					const FVector& C = Section.Vertices[IC];
					const float TriangleDoubleArea = FMath::Abs(Cross2D(A, B, C));
					EmittedDoubleArea += TriangleDoubleArea;
					const bool bDegenerate = TriangleDoubleArea <= KINDA_SMALL_NUMBER;
					const bool bOutside = !PointInsideBoundary((A + B + C) / 3.0f);
					DegenerateTriangleCount += bDegenerate ? 1 : 0;
					OutsideTriangleCount += bOutside ? 1 : 0;

					const FColor TriangleColor =
						(bDegenerate || bOutside) ? FColor::Red : FColor::Green;
					DrawDebugLine(GetWorld(), GetActorTransform().TransformPosition(A),
						GetActorTransform().TransformPosition(B), TriangleColor, false, 60.0f, 0, 0.8f);
					DrawDebugLine(GetWorld(), GetActorTransform().TransformPosition(B),
						GetActorTransform().TransformPosition(C), TriangleColor, false, 60.0f, 0, 0.8f);
					DrawDebugLine(GetWorld(), GetActorTransform().TransformPosition(C),
						GetActorTransform().TransformPosition(A), TriangleColor, false, 60.0f, 0, 0.8f);
				}

				const float AreaError = FMath::Abs(
					FMath::Abs(BoundaryDoubleArea) - EmittedDoubleArea);
				const bool bFailed =
					AddedTriangleCount != ExpectedTriangleCount ||
					DuplicatePointCount > 0 ||
					SelfIntersectionCount > 0 ||
					DegenerateTriangleCount > 0 ||
					OutsideTriangleCount > 0 ||
					AreaError > 0.1f;

				UE_LOG(LogTemp, Warning, TEXT(
					"TileMap slant diagnostic: lower=(%d,%d,%d) upper=(%d,%d,%d) "
					"chunk=(%d,%d,%d) rotation=%d fraction=%.4f boundary=%d "
					"vertices=%d triangles=%d expected=%d duplicate=%d "
					"intersections=%d degenerate=%d outside=%d area_error=%.6f result=%s"),
					GridPosition.X, GridPosition.Y, GridPosition.Z,
					AbovePosition.X, AbovePosition.Y, AbovePosition.Z,
					ChunkCoordinate.X, ChunkCoordinate.Y, ChunkCoordinate.Z,
					Rotation, DiagonalFraction, SupportBoundary.Num(),
					AddedVertexCount, AddedTriangleCount, ExpectedTriangleCount,
					DuplicatePointCount, SelfIntersectionCount,
					DegenerateTriangleCount, OutsideTriangleCount,
					AreaError, bFailed ? TEXT("FAIL") : TEXT("PASS"));

				for (int32 I = 0; I < SupportBoundary.Num(); ++I)
				{
					const FVector& A = SupportBoundary[I];
					const FVector& B = SupportBoundary[(I + 1) % SupportBoundary.Num()];
					UE_LOG(LogTemp, Warning, TEXT(
						"TileMap slant diagnostic point: lower=(%d,%d,%d) i=%d p=(%.6f,%.6f,%.6f)"),
						GridPosition.X, GridPosition.Y, GridPosition.Z,
						I, A.X, A.Y, A.Z);
					DrawDebugLine(GetWorld(), GetActorTransform().TransformPosition(A),
						GetActorTransform().TransformPosition(B), FColor::Yellow, false, 60.0f, 0, 2.0f);
					DrawDebugPoint(GetWorld(), GetActorTransform().TransformPosition(A),
						7.0f, bFailed ? FColor::Red : FColor::Cyan, false, 60.0f, 0);
				}

				for (int32 XOffset = -1; XOffset <= 1; ++XOffset)
				{
					for (int32 YOffset = -1; YOffset <= 1; ++YOffset)
					{
						for (int32 ZOffset = 0; ZOffset <= 1; ++ZOffset)
						{
							const FIntVector Nearby = GridPosition +
								FIntVector(XOffset, YOffset, ZOffset);
							if (!HasBlock(Nearby))
							{
								continue;
							}
							UE_LOG(LogTemp, Warning, TEXT(
								"TileMap slant diagnostic neighbor: origin=(%d,%d,%d) "
								"cell=(%d,%d,%d) tile=%d rotation=%d diagonal=%d"),
								GridPosition.X, GridPosition.Y, GridPosition.Z,
								Nearby.X, Nearby.Y, Nearby.Z,
								static_cast<int32>(GetBlockTileType(Nearby)),
								GetBlockRotation(Nearby) % 4,
								IsContinuousDiagonalTileType(GetBlockTileType(Nearby)) ? 1 : 0);
						}
					}
				}
			}
		}

		bool bNeedsPathDetail = false;
		const bool bCurrentCellHasVisiblePath =
			IsVisiblePaintedPathBlock(GridPosition);

		for (int32 XOffset = -1;
			XOffset <= 1 && !bNeedsPathDetail;
			++XOffset)
		{
			for (int32 YOffset = -1; YOffset <= 1; ++YOffset)
			{
				if (
					IsVisiblePaintedPathBlock(
						GridPosition +
						FIntVector(XOffset, YOffset, 0)
					) != bCurrentCellHasVisiblePath
					)
				{
					bNeedsPathDetail = true;
					break;
				}
			}
		}

		if (!bHasAboveBlock)
		{
			const int32 TopGridZ = GridPosition.Z + 1;
			const float FlatTopZ = TopGridZ * SafeGridSize;
			bool bTouchesChamfer = false;
			bool bCliffFootEdges[4] =
			{
				false,
				false,
				false,
				false
			};
			bool bTouchesCliffFoot = false;

			if (CliffFootBlendWidth > KINDA_SMALL_NUMBER)
			{
				for (int32 WallIndex = 0; WallIndex < 4; ++WallIndex)
				{
					const FIntVector SupportPosition =
						GridPosition + Walls[WallIndex].NeighborOffset;
					const FIntVector StackedPosition =
						SupportPosition + FIntVector(0, 0, 1);

					bCliffFootEdges[WallIndex] =
						IsContinuousSurfaceBlock(SupportPosition) &&
						IsContinuousSurfaceBlock(StackedPosition) &&
						!IsContinuousRampBlock(SupportPosition) &&
						!IsContinuousRampBlock(StackedPosition) &&
						!IsContinuousStairBlock(SupportPosition) &&
						!IsContinuousStairBlock(StackedPosition);
					bTouchesCliffFoot =
						bTouchesCliffFoot ||
						bCliffFootEdges[WallIndex];
				}
			}

			for (int32 CornerX = 0; CornerX <= 1; ++CornerX)
			{
				for (int32 CornerY = 0; CornerY <= 1; ++CornerY)
				{
					FVector CornerNormal;
					const float CornerZ = GetContinuousTopSurfaceZ(
						(GridPosition.X + CornerX) * SafeGridSize,
						(GridPosition.Y + CornerY) * SafeGridSize,
						TopGridZ,
						CornerNormal
					);

					if (CornerZ < FlatTopZ - KINDA_SMALL_NUMBER)
					{
						bTouchesChamfer = true;
					}
				}
			}

			bool bTouchesPartialDiagonal = false;
			bool bNeedsDetailedX = false;
			bool bNeedsDetailedY = false;
			TArray<float> PartialXAlphas;
			TArray<float> PartialYAlphas;

			for (const FContinuousWall& Wall : Walls)
			{
				float CoverageStart = 0.0f;
				float CoverageEnd = 0.0f;

				if (
					!GetContinuousTerrainCoverageAcrossEdge(
						GridPosition,
						Wall.NeighborOffset,
						CoverageStart,
						CoverageEnd
					) ||
					(
						CoverageStart <= KINDA_SMALL_NUMBER &&
						CoverageEnd >= 1.0f - KINDA_SMALL_NUMBER
					)
					)
				{
					continue;
				}

				bTouchesPartialDiagonal = true;
				const bool bVerticalWall = FMath::IsNearlyEqual(
					Wall.BottomAOffset.X,
					Wall.BottomBOffset.X
				);
				TArray<float>& PartialAlphas =
					bVerticalWall ? PartialYAlphas : PartialXAlphas;
				bNeedsDetailedY = bNeedsDetailedY || bVerticalWall;
				bNeedsDetailedX = bNeedsDetailedX || !bVerticalWall;

				if (
					CoverageStart > KINDA_SMALL_NUMBER &&
					CoverageStart < 1.0f - KINDA_SMALL_NUMBER
					)
				{
					PartialAlphas.Add(CoverageStart);
				}

				if (
					CoverageEnd > KINDA_SMALL_NUMBER &&
					CoverageEnd < 1.0f - KINDA_SMALL_NUMBER
					)
				{
					PartialAlphas.Add(CoverageEnd);
				}
			}

			const bool bUseRoundedSurface =
				bTouchesChamfer ||
				bHasConvexCorner ||
				bHasDiagonalTangentCorner ||
				bTouchesPartialDiagonal;
			const bool bTouchesCliffFootX =
				bCliffFootEdges[0] || bCliffFootEdges[1];
			const bool bTouchesCliffFootY =
				bCliffFootEdges[2] || bCliffFootEdges[3];
			const TArray<float>& ActiveRoundedSurfaceAlphas =
				bHasEdgeBlendCorner
					? RoundedSurfaceAlphas
					: StraightEdgeSurfaceAlphas;
			const TArray<float>& ActiveRoundedCliffFootAlphas =
				bHasEdgeBlendCorner
					? RoundedCliffFootSurfaceAlphas
					: StraightCliffFootSurfaceAlphas;
			const TArray<float>& BaseSurfaceXAlphas =
				bUseRoundedSurface
					? (
						bTouchesCliffFootX
							? ActiveRoundedCliffFootAlphas
							: ActiveRoundedSurfaceAlphas
					)
					: (
						bTouchesCliffFootX
							? CliffFootSurfaceAlphas
							: FlatSurfaceAlphas
					);
			const TArray<float>& BaseSurfaceYAlphas =
				bUseRoundedSurface
					? (
						bTouchesCliffFootY
							? ActiveRoundedCliffFootAlphas
							: ActiveRoundedSurfaceAlphas
					)
					: (
						bTouchesCliffFootY
							? CliffFootSurfaceAlphas
						: FlatSurfaceAlphas
					);
			TArray<float> DetailedSurfaceXAlphas = BaseSurfaceXAlphas;
			TArray<float> DetailedSurfaceYAlphas = BaseSurfaceYAlphas;
			auto AddDetailedAlpha =
				[](TArray<float>& Alphas, float Alpha)
				{
					const float SafeAlpha = FMath::Clamp(
						Alpha,
						0.0f,
						1.0f
					);

					for (const float ExistingAlpha : Alphas)
					{
						if (FMath::IsNearlyEqual(ExistingAlpha, SafeAlpha))
						{
							return;
						}
					}

					Alphas.Add(SafeAlpha);
				};

			if (bNeedsDetailedX)
			{
				for (
					int32 DetailStep = 1;
					DetailStep < DetailedEdgeSubdivisions;
					++DetailStep
					)
				{
					AddDetailedAlpha(
						DetailedSurfaceXAlphas,
						static_cast<float>(DetailStep) /
							static_cast<float>(DetailedEdgeSubdivisions)
					);
				}

				for (const float Alpha : PartialXAlphas)
				{
					AddDetailedAlpha(DetailedSurfaceXAlphas, Alpha);
				}

				DetailedSurfaceXAlphas.Sort();
			}

			if (bNeedsDetailedY)
			{
				for (
					int32 DetailStep = 1;
					DetailStep < DetailedEdgeSubdivisions;
					++DetailStep
					)
				{
					AddDetailedAlpha(
						DetailedSurfaceYAlphas,
						static_cast<float>(DetailStep) /
							static_cast<float>(DetailedEdgeSubdivisions)
					);
				}

				for (const float Alpha : PartialYAlphas)
				{
					AddDetailedAlpha(DetailedSurfaceYAlphas, Alpha);
				}

				DetailedSurfaceYAlphas.Sort();
			}

			if (
				bNeedsPathDetail &&
				PathBlendAlpha > KINDA_SMALL_NUMBER
				)
			{
				const float PathDetailFractions[] =
				{
					0.5f,
					1.0f
				};

				for (const float DetailFraction : PathDetailFractions)
				{
					const float DetailAlpha =
						PathBlendAlpha * DetailFraction;

					AddDetailedAlpha(
						DetailedSurfaceXAlphas,
						DetailAlpha
					);
					AddDetailedAlpha(
						DetailedSurfaceXAlphas,
						1.0f - DetailAlpha
					);
					AddDetailedAlpha(
						DetailedSurfaceYAlphas,
						DetailAlpha
					);
					AddDetailedAlpha(
						DetailedSurfaceYAlphas,
						1.0f - DetailAlpha
					);
				}

				DetailedSurfaceXAlphas.Sort();
				DetailedSurfaceYAlphas.Sort();
			}

			const bool bUseDetailedX =
				bNeedsDetailedX || bNeedsPathDetail;
			const bool bUseDetailedY =
				bNeedsDetailedY || bNeedsPathDetail;

			const TArray<float>& SurfaceXAlphas =
				bUseDetailedX
					? DetailedSurfaceXAlphas
					: BaseSurfaceXAlphas;
			const TArray<float>& SurfaceYAlphas =
				bUseDetailedY
					? DetailedSurfaceYAlphas
					: BaseSurfaceYAlphas;
			const int32 SurfacePointCountX = SurfaceXAlphas.Num();
			const int32 SurfacePointCountY = SurfaceYAlphas.Num();
			TArray<int32> SurfaceVertexIndices;
			SurfaceVertexIndices.SetNum(
				SurfacePointCountX * SurfacePointCountY
			);
			TArray<int32> SurfaceCliffVertexIndices;
			SurfaceCliffVertexIndices.Init(
				INDEX_NONE,
				SurfacePointCountX * SurfacePointCountY
			);
			TArray<uint8> SurfaceGroundOwnership;
			SurfaceGroundOwnership.Init(
				0,
				SurfacePointCountX * SurfacePointCountY
			);
			TArray<FVector> SurfacePositions;
			SurfacePositions.SetNum(
				SurfacePointCountX * SurfacePointCountY
			);
			TArray<FVector> SurfaceNormals;
			SurfaceNormals.SetNum(
				SurfacePointCountX * SurfacePointCountY
			);
			TArray<FVector2D> SurfaceCliffUVs;
			SurfaceCliffUVs.SetNum(
				SurfacePointCountX * SurfacePointCountY
			);
			TArray<FVector> SurfaceCliffTangents;
			SurfaceCliffTangents.SetNum(
				SurfacePointCountX * SurfacePointCountY
			);
			TArray<FColor> SurfaceColors;
			SurfaceColors.SetNum(
				SurfacePointCountX * SurfacePointCountY
			);
			TArray<FContinuousBoundarySegment> CliffTopBoundarySegments;
			BuildExposedBoundarySegments(
				GridPosition,
				CliffTopBoundarySegments
			);

			// A valid ramp occupies the neighboring XY cell, so the generic
			// exposure query correctly suppresses a full-height wall. Its surface
			// can still sit below this flat top, however. Register only that shared
			// upper ledge for cliff-top UV/tangent ownership; the ramp branch keeps
			// the vertical remainder as ordinary wall.
			for (const FContinuousWall& Wall : Walls)
			{
				const FIntVector RampPosition =
					GridPosition + Wall.NeighborOffset;

				if (!IsContinuousRampBlock(RampPosition))
				{
					continue;
				}

				const FVector2D Start =
					BlockMinimum2D +
					(Wall.BottomAOffset * SafeGridSize);
				const FVector2D End =
					BlockMinimum2D +
					(Wall.BottomBOffset * SafeGridSize);
				float RampStartZ = FlatTopZ;
				float RampEndZ = FlatTopZ;

				if (
					GetContinuousRampBoundaryChamferZ(
						RampPosition,
						Start,
						FlatTopZ,
						ChamferDepth,
						RampStartZ
					) &&
					GetContinuousRampBoundaryChamferZ(
						RampPosition,
						End,
						FlatTopZ,
						ChamferDepth,
						RampEndZ
					) &&
					FMath::Min(RampStartZ, RampEndZ) <
						FlatTopZ - KINDA_SMALL_NUMBER
					)
				{
					CliffTopBoundarySegments.Add(
						{
							Start,
							End,
							FVector2D(Wall.Normal.X, Wall.Normal.Y),
							false
						}
					);
				}
			}

			for (
				int32 SurfaceY = 0;
				SurfaceY < SurfacePointCountY;
				++SurfaceY
				)
			{
				for (
					int32 SurfaceX = 0;
					SurfaceX < SurfacePointCountX;
					++SurfaceX
					)
				{
					const float AlphaX = SurfaceXAlphas[SurfaceX];
					const float AlphaY = SurfaceYAlphas[SurfaceY];
					const FVector2D OriginalPoint(
						(GridPosition.X + AlphaX) * SafeGridSize,
						(GridPosition.Y + AlphaY) * SafeGridSize
					);
					FVector2D SurfacePoint =
						RoundConvexPoint(OriginalPoint, nullptr);
					SurfacePoint +=
						GetCliffEdgeOffset(OriginalPoint);
					FVector SurfaceNormal;
					const float LocalZ = GetContinuousTopSurfaceZ(
						OriginalPoint.X,
						OriginalPoint.Y,
						TopGridZ,
						SurfaceNormal
					);
					FVector2D DiagonalTangentNormal;
					RoundDiagonalTangentPoint(
						OriginalPoint,
						GridPosition.Z,
						&DiagonalTangentNormal
					);

					if (
						DiagonalTangentNormal.SizeSquared() >
						SMALL_NUMBER
						)
					{
						const float LipSlope =
							ChamferDepth /
							FMath::Max(ChamferWidth, 1.0f);
						SurfaceNormal = FVector(
							DiagonalTangentNormal.X * LipSlope,
							DiagonalTangentNormal.Y * LipSlope,
							1.0f
						).GetSafeNormal();
					}
					const int32 VertexArrayIndex =
						SurfaceY * SurfacePointCountX +
						SurfaceX;
					const float PathMask = GetContinuousPathMask(
						OriginalPoint.X,
						OriginalPoint.Y,
						GridPosition.Z,
						PathBlendWidth
					);

					const bool bGroundOwned =
						LocalZ >= FlatTopZ - KINDA_SMALL_NUMBER
							? true
							: false;
					const FVector SurfacePosition(
						SurfacePoint.X,
						SurfacePoint.Y,
						LocalZ
					);
					FVector CliffTopTangent(
						-SurfaceNormal.Y,
						SurfaceNormal.X,
						0.0f
					);
					float CliffTopU = FVector::DotProduct(
						SurfacePosition,
						MakeCliffTopTangent(
							CliffTopTangent,
							SurfaceNormal
						)
					) / SafeGridSize;
					float ClosestBoundaryDistanceSquared =
						TNumericLimits<float>::Max();

					for (
						const FContinuousBoundarySegment& Edge :
							CliffTopBoundarySegments
						)
					{
						const FVector2D EdgeVector = Edge.End - Edge.Start;
						const float EdgeLengthSquared = EdgeVector.SizeSquared();
						const float EdgeAlpha =
							EdgeLengthSquared > SMALL_NUMBER
								? FMath::Clamp(
									FVector2D::DotProduct(
										OriginalPoint - Edge.Start,
										EdgeVector
									) / EdgeLengthSquared,
									0.0f,
									1.0f
								)
								: 0.0f;
						const FVector2D ClosestPoint =
							Edge.Start + (EdgeVector * EdgeAlpha);
						const float DistanceSquared =
							(OriginalPoint - ClosestPoint).SizeSquared();

						if (DistanceSquared < ClosestBoundaryDistanceSquared)
						{
							const FVector2D Tangent2D =
								EdgeVector.GetSafeNormal();
							ClosestBoundaryDistanceSquared = DistanceSquared;
							CliffTopTangent = FVector(
								Tangent2D.X,
								Tangent2D.Y,
								0.0f
							);
							CliffTopU = FVector2D::DotProduct(
								OriginalPoint,
								Tangent2D
							) / SafeGridSize;
						}
					}
					const float CliffTopV = FMath::Clamp(
						(FlatTopZ - LocalZ) /
							FMath::Max(ChamferDepth * 2.0f, 1.0f),
						0.0f,
						0.5f
					);
					const FColor SurfaceColor = bGroundOwned
						? MakeGroundSurfaceVertexColor(0.0f, PathMask)
						: ContinuousCliffEdgeSurfaceColor;
					const FVector2D SurfaceCliffUV =
						MakeCliffTopUVFromCoordinate(
							CliffTopU,
							CliffTopV
						);

					SurfaceGroundOwnership[VertexArrayIndex] =
						bGroundOwned ? 1 : 0;
					SurfacePositions[VertexArrayIndex] = SurfacePosition;
					SurfaceNormals[VertexArrayIndex] = SurfaceNormal;
					SurfaceCliffUVs[VertexArrayIndex] = SurfaceCliffUV;
					SurfaceCliffTangents[VertexArrayIndex] =
						CliffTopTangent;
					SurfaceColors[VertexArrayIndex] = SurfaceColor;
					SurfaceVertexIndices[VertexArrayIndex] = bGroundOwned
						? AddContinuousVertex(
							Section,
							SurfacePosition,
							SurfaceNormal,
							SafeGridSize,
							SurfaceColor
						)
						: AddContinuousVertexWithUV(
							Section,
							SurfacePosition,
							SurfaceNormal,
							SafeGridSize,
							SurfaceCliffUV,
							CliffTopTangent,
							SurfaceColor
						);
					SurfaceCliffVertexIndices[VertexArrayIndex] =
						bGroundOwned
							? INDEX_NONE
							: SurfaceVertexIndices[VertexArrayIndex];
				}
			}

			auto ResolveSurfaceVertex =
				[&](int32 VertexArrayIndex, bool bUseCliffUV)
				{
					if (
						!bUseCliffUV ||
						SurfaceGroundOwnership[VertexArrayIndex] == 0
						)
					{
						return SurfaceVertexIndices[VertexArrayIndex];
					}

					int32& CliffVertexIndex =
						SurfaceCliffVertexIndices[VertexArrayIndex];

					if (CliffVertexIndex == INDEX_NONE)
					{
						// Split UV ownership only when a lip triangle actually
						// touches this ground vertex. Position, normal, color, and
						// therefore every blend mask remain exactly unchanged.
						CliffVertexIndex = AddContinuousVertexWithUV(
							Section,
							SurfacePositions[VertexArrayIndex],
							SurfaceNormals[VertexArrayIndex],
							SafeGridSize,
							SurfaceCliffUVs[VertexArrayIndex],
							SurfaceCliffTangents[VertexArrayIndex],
							SurfaceColors[VertexArrayIndex]
						);
					}

					return CliffVertexIndex;
				};

			auto AddSurfaceTriangle =
				[&](int32 A, int32 B, int32 C)
				{
					const bool bUseCliffUV =
						SurfaceGroundOwnership[A] == 0 ||
						SurfaceGroundOwnership[B] == 0 ||
						SurfaceGroundOwnership[C] == 0;

					Section.Triangles.Add(
						ResolveSurfaceVertex(A, bUseCliffUV)
					);
					Section.Triangles.Add(
						ResolveSurfaceVertex(B, bUseCliffUV)
					);
					Section.Triangles.Add(
						ResolveSurfaceVertex(C, bUseCliffUV)
					);
				};

			for (
				int32 SurfaceY = 0;
				SurfaceY < SurfacePointCountY - 1;
				++SurfaceY
				)
			{
				for (
					int32 SurfaceX = 0;
					SurfaceX < SurfacePointCountX - 1;
					++SurfaceX
					)
				{
					const int32 BottomLeft =
						SurfaceY * SurfacePointCountX +
						SurfaceX;
					const int32 BottomRight =
						SurfaceY * SurfacePointCountX +
						SurfaceX + 1;
					const int32 TopLeft =
						(SurfaceY + 1) *
						SurfacePointCountX +
						SurfaceX;
					const int32 TopRight =
						(SurfaceY + 1) *
						SurfacePointCountX +
						SurfaceX + 1;

					AddSurfaceTriangle(
						BottomLeft,
						TopRight,
						BottomRight
					);
					AddSurfaceTriangle(
						BottomLeft,
						TopLeft,
						TopRight
					);
				}
			}
		}

		if (
			GridPosition.Z > 0 &&
			!HasBlock(GridPosition + FIntVector(0, 0, -1))
			)
		{
			const float BottomZ =
				GridPosition.Z * SafeGridSize;
			TArray<FVector> BottomBoundary;
			const FVector2D BoundaryCorners[4] =
			{
				FVector2D(BlockMinimum.X, BlockMinimum.Y),
				FVector2D(
					BlockMinimum.X + SafeGridSize,
					BlockMinimum.Y
				),
				FVector2D(
					BlockMinimum.X + SafeGridSize,
					BlockMinimum.Y + SafeGridSize
				),
				FVector2D(
					BlockMinimum.X,
					BlockMinimum.Y + SafeGridSize
				)
			};
			const TArray<float>& BottomAlphas =
				(bHasConvexCorner || bHasDiagonalTangentCorner)
					? RoundedSurfaceAlphas
					: FlatSurfaceAlphas;

			for (int32 EdgeIndex = 0; EdgeIndex < 4; ++EdgeIndex)
			{
				const FVector2D& EdgeStart =
					BoundaryCorners[EdgeIndex];
				const FVector2D& EdgeEnd =
					BoundaryCorners[(EdgeIndex + 1) % 4];

				for (const float EdgeAlpha : BottomAlphas)
				{
					const FVector2D OriginalPoint = FMath::Lerp(
						EdgeStart,
						EdgeEnd,
						EdgeAlpha
					);
					const FVector2D RoundedPoint =
						RoundConvexPoint(OriginalPoint, nullptr);

					if (
						BottomBoundary.Num() > 0 &&
						FVector2D(
							BottomBoundary.Last().X,
							BottomBoundary.Last().Y
						).Equals(RoundedPoint, KINDA_SMALL_NUMBER)
						)
					{
						continue;
					}

					BottomBoundary.Add(
						FVector(
							RoundedPoint.X,
							RoundedPoint.Y,
							BottomZ
						)
					);
				}
			}

			if (
				BottomBoundary.Num() > 1 &&
				FVector2D(
					BottomBoundary[0].X,
					BottomBoundary[0].Y
				).Equals(
					FVector2D(
						BottomBoundary.Last().X,
						BottomBoundary.Last().Y
					),
					KINDA_SMALL_NUMBER
				)
				)
			{
				BottomBoundary.Pop();
			}

			AddContinuousPolygon(
				Section,
				BottomBoundary,
				-FVector::UpVector,
				SafeGridSize,
				true
			);
		}

		for (int32 CornerX = 0; CornerX <= 1; ++CornerX)
		{
			for (int32 CornerY = 0; CornerY <= 1; ++CornerY)
			{
				if (!bConvexCorners[CornerX][CornerY])
				{
					continue;
				}

				TArray<FVector> TransitionBoundary;
				const FIntVector BelowPosition =
					GridPosition + FIntVector(0, 0, -1);
				const FIntVector CornerAbovePosition =
					GridPosition + FIntVector(0, 0, 1);

				if (
					GridPosition.Z > 0 &&
					HasBlock(BelowPosition) &&
					!IsConvexCorner(
						BelowPosition,
						CornerX,
						CornerY
					)
					)
				{
					BuildConvexCutoutBoundary(
						CornerX,
						CornerY,
						BlockMinimum.Z,
						TransitionBoundary
					);

					if (TransitionBoundary.Num() > 0)
					{
						const FVector2D SupportingCorner(
							(GridPosition.X + CornerX) *
							SafeGridSize,
							(GridPosition.Y + CornerY) *
							SafeGridSize
						);
						FVector SupportNormal;
						const float SupportingSurfaceZ =
							GetContinuousTopSurfaceZ(
								SupportingCorner.X,
								SupportingCorner.Y,
								GridPosition.Z,
								SupportNormal
							);

						// Keep the rounded arc on the upper block, but lower the
						// outer fan vertex onto the chamfered supporting lip.
						TransitionBoundary[0].Z = FMath::Min(
							BlockMinimum.Z,
							SupportingSurfaceZ
						);
					}

					AddConvexCutoutPatch(
						TransitionBoundary,
						FVector::UpVector,
						false
					);
				}

				if (
					HasBlock(CornerAbovePosition) &&
					!IsConvexCorner(
						CornerAbovePosition,
						CornerX,
						CornerY
					)
					)
				{
					BuildConvexCutoutBoundary(
						CornerX,
						CornerY,
						BlockMinimum.Z + SafeGridSize,
						TransitionBoundary
					);
					AddConvexCutoutPatch(
						TransitionBoundary,
						-FVector::UpVector,
						false
					);
				}
			}
		}

		for (const FContinuousWall& Wall : Walls)
		{
			float CoverageStart = 0.0f;
			float CoverageEnd = 0.0f;
			const bool bHasCoverage =
				GetContinuousTerrainCoverageAcrossEdge(
					GridPosition,
					Wall.NeighborOffset,
					CoverageStart,
					CoverageEnd
				);
			const bool bVerticalWall = FMath::IsNearlyEqual(
				Wall.BottomAOffset.X,
				Wall.BottomBOffset.X
			);
			const float CanonicalStart =
				bVerticalWall
					? Wall.BottomAOffset.Y
					: Wall.BottomAOffset.X;
			const float CanonicalEnd =
				bVerticalWall
					? Wall.BottomBOffset.Y
					: Wall.BottomBOffset.X;
			const float CanonicalDelta =
				CanonicalEnd - CanonicalStart;
			TArray<FVector2D, TInlineAllocator<2>> VisibleRanges;
			bool bPartialCoverage = false;

			if (!bHasCoverage)
			{
				VisibleRanges.Add(FVector2D(0.0f, 1.0f));
			}
			else if (FMath::Abs(CanonicalDelta) > SMALL_NUMBER)
			{
				const float CoveredAlphaA =
					(CoverageStart - CanonicalStart) / CanonicalDelta;
				const float CoveredAlphaB =
					(CoverageEnd - CanonicalStart) / CanonicalDelta;
				const float CoveredStart = FMath::Clamp(
					FMath::Min(CoveredAlphaA, CoveredAlphaB),
					0.0f,
					1.0f
				);
				const float CoveredEnd = FMath::Clamp(
					FMath::Max(CoveredAlphaA, CoveredAlphaB),
					0.0f,
					1.0f
				);

				if (
					CoveredEnd - CoveredStart <=
						KINDA_SMALL_NUMBER
					)
				{
					VisibleRanges.Add(FVector2D(0.0f, 1.0f));
				}
				else
				{
					bPartialCoverage =
						CoveredStart > KINDA_SMALL_NUMBER ||
						CoveredEnd < 1.0f - KINDA_SMALL_NUMBER;

					if (CoveredStart > KINDA_SMALL_NUMBER)
					{
						VisibleRanges.Add(
							FVector2D(0.0f, CoveredStart)
						);
					}

					if (CoveredEnd < 1.0f - KINDA_SMALL_NUMBER)
					{
						VisibleRanges.Add(
							FVector2D(CoveredEnd, 1.0f)
						);
					}
				}
			}

			if (VisibleRanges.Num() == 0)
			{
				continue;
			}

			const FIntVector LowerCurrentPosition =
				GridPosition + FIntVector(0, 0, -1);
			const FIntVector LowerNeighborPosition =
				GridPosition +
				Wall.NeighborOffset +
				FIntVector(0, 0, -1);
			const bool bHasCliffFoot =
				GridPosition.Z > 0 &&
				CliffFootBlendHeight > KINDA_SMALL_NUMBER &&
				IsContinuousSurfaceBlock(LowerCurrentPosition) &&
				IsContinuousSurfaceBlock(LowerNeighborPosition) &&
				!IsContinuousRampBlock(LowerCurrentPosition) &&
				!IsContinuousRampBlock(LowerNeighborPosition) &&
				!IsContinuousStairBlock(LowerCurrentPosition) &&
				!IsContinuousStairBlock(LowerNeighborPosition);

			const FVector2D OriginalBottomA(
				BlockMinimum.X +
					(Wall.BottomAOffset.X * SafeGridSize),
				BlockMinimum.Y +
					(Wall.BottomAOffset.Y * SafeGridSize)
			);
			const FVector2D OriginalBottomB(
				BlockMinimum.X +
					(Wall.BottomBOffset.X * SafeGridSize),
				BlockMinimum.Y +
					(Wall.BottomBOffset.Y * SafeGridSize)
			);
			const int32 CornerAGridX =
				GridPosition.X +
				FMath::RoundToInt(Wall.BottomAOffset.X);
			const int32 CornerAGridY =
				GridPosition.Y +
				FMath::RoundToInt(Wall.BottomAOffset.Y);
			const int32 CornerBGridX =
				GridPosition.X +
				FMath::RoundToInt(Wall.BottomBOffset.X);
			const int32 CornerBGridY =
				GridPosition.Y +
				FMath::RoundToInt(Wall.BottomBOffset.Y);
			const int32 CornerAX =
				FMath::RoundToInt(Wall.BottomAOffset.X);
			const int32 CornerAY =
				FMath::RoundToInt(Wall.BottomAOffset.Y);
			const int32 CornerBX =
				FMath::RoundToInt(Wall.BottomBOffset.X);
			const int32 CornerBY =
				FMath::RoundToInt(Wall.BottomBOffset.Y);
			const bool bConvexAtA =
				bConvexCorners[CornerAX][CornerAY];
			const bool bConvexAtB =
				bConvexCorners[CornerBX][CornerBY];
			const bool bDiagonalTangentAtA =
				bDiagonalTangentCorners[CornerAX][CornerAY];
			const bool bDiagonalTangentAtB =
				bDiagonalTangentCorners[CornerBX][CornerBY];
			const bool bConcaveAtA =
				IsRoundableConcaveCorner(
					CornerAGridX,
					CornerAGridY,
					GridPosition.Z
				);
			const bool bConcaveAtB =
				IsRoundableConcaveCorner(
					CornerBGridX,
					CornerBGridY,
					GridPosition.Z
				);

			for (const FVector2D& VisibleRange : VisibleRanges)
			{
			const float StartAlpha = FMath::Max(
				VisibleRange.X,
				bConcaveAtA ? CornerAlpha : 0.0f
			);
			const float EndAlpha = FMath::Min(
				VisibleRange.Y,
				bConcaveAtB ? 1.0f - CornerAlpha : 1.0f
			);

			if (EndAlpha - StartAlpha <= KINDA_SMALL_NUMBER)
			{
				continue;
			}
			const FIntVector WallAbovePosition =
				GridPosition + FIntVector(0, 0, 1);
			bool bCoveredAbove = HasBlock(WallAbovePosition);

			if (
				bCoveredAbove &&
				IsContinuousSurfaceBlock(WallAbovePosition) &&
				IsContinuousDiagonalTileType(
					GetBlockTileType(WallAbovePosition)
				)
				)
			{
				float AboveCoverageStart = 0.0f;
				float AboveCoverageEnd = 0.0f;
				bCoveredAbove =
					GetContinuousCellEdgeCoverage(
						WallAbovePosition,
						Wall.NeighborOffset,
						AboveCoverageStart,
						AboveCoverageEnd
					) &&
					AboveCoverageStart <= KINDA_SMALL_NUMBER &&
					AboveCoverageEnd >= 1.0f - KINDA_SMALL_NUMBER;
			}
			const bool bUseWavyLip =
				!bCoveredAbove &&
				EdgeIrregularity > KINDA_SMALL_NUMBER;
			TArray<float> WallAlphas;
			WallAlphas.Add(StartAlpha);
			WallAlphas.Add(EndAlpha);
			// The top grid selects one alpha set for both axes. If any corner in
			// this cell needs junction detail, every exposed wall must retain that
			// same set so opposite edges cannot acquire a T-junction.
			const bool bNeedsRoundedWallSampling =
				bHasEdgeBlendCorner;
			const TArray<float>& CandidateWallAlphas =
				bNeedsRoundedWallSampling
				? RoundedSurfaceAlphas
				: (
					bUseWavyLip
						? StraightEdgeSurfaceAlphas
						: FlatSurfaceAlphas
				);

			for (const float SurfaceAlpha : CandidateWallAlphas)
			{
				if (
					SurfaceAlpha > StartAlpha + KINDA_SMALL_NUMBER &&
					SurfaceAlpha < EndAlpha - KINDA_SMALL_NUMBER
					)
				{
					WallAlphas.Add(SurfaceAlpha);
				}
			}

			if (
				bNeedsPathDetail &&
				PathBlendAlpha > KINDA_SMALL_NUMBER
				)
			{
				const float PathDetailFractions[] =
				{
					0.5f,
					1.0f
				};

				for (const float DetailFraction : PathDetailFractions)
				{
					const float DetailAlpha =
						PathBlendAlpha * DetailFraction;
					const float CandidateAlphas[] =
					{
						DetailAlpha,
						1.0f - DetailAlpha
					};

					for (const float CandidateAlpha : CandidateAlphas)
					{
						if (
							CandidateAlpha >
								StartAlpha + KINDA_SMALL_NUMBER &&
							CandidateAlpha <
								EndAlpha - KINDA_SMALL_NUMBER
							)
						{
							WallAlphas.Add(CandidateAlpha);
						}
					}
				}
			}

			// Partial cuts use the same global alpha positions as the detailed
			// top grid. Matching those samples prevents a top-to-wall T-junction.
			if (bPartialCoverage)
			{
				for (
					int32 DetailStep = 1;
					DetailStep < DetailedEdgeSubdivisions;
					++DetailStep
					)
				{
					const float DetailAlpha =
						static_cast<float>(DetailStep) /
							static_cast<float>(DetailedEdgeSubdivisions);

					if (
						DetailAlpha > StartAlpha + KINDA_SMALL_NUMBER &&
						DetailAlpha < EndAlpha - KINDA_SMALL_NUMBER
						)
					{
						WallAlphas.Add(DetailAlpha);
					}
				}
			}

			WallAlphas.Sort();

			for (
				int32 AlphaIndex = WallAlphas.Num() - 1;
				AlphaIndex > 0;
				--AlphaIndex
				)
			{
				if (
					FMath::IsNearlyEqual(
						WallAlphas[AlphaIndex],
						WallAlphas[AlphaIndex - 1]
					)
					)
				{
					WallAlphas.RemoveAt(AlphaIndex);
				}
			}

			TArray<FVector> WallBottomPoints;
			TArray<FVector> WallFootPoints;
			TArray<FVector> WallShoulderPoints;
			TArray<FVector> WallTopPoints;
			TArray<FVector> WallNormals;
			TArray<float> WallAlongCoordinates;
			const FVector2D WallTangent =
				(OriginalBottomB - OriginalBottomA).GetSafeNormal();

			for (const float WallAlpha : WallAlphas)
			{
				const FVector2D OriginalPoint = FMath::Lerp(
					OriginalBottomA,
					OriginalBottomB,
					WallAlpha
				);
				FVector2D RoundedNormal2D;
				const FVector2D RoundedPoint =
					RoundConvexPoint(
						OriginalPoint,
						&RoundedNormal2D
					);
				FVector2D TopPoint = RoundedPoint;

				if (bUseWavyLip)
				{
					TopPoint += GetCliffEdgeOffset(OriginalPoint);
				}

				const FVector WallNormal =
					RoundedNormal2D.SizeSquared() <= SMALL_NUMBER
					? Wall.Normal
					: FVector(
						RoundedNormal2D.X,
						RoundedNormal2D.Y,
						0.0f
					);
				float WallTopZ =
					(GridPosition.Z + 1) * SafeGridSize;

				if (!bCoveredAbove)
				{
					FVector IgnoredTopNormal;
					WallTopZ = GetContinuousTopSurfaceZ(
						OriginalPoint.X,
						OriginalPoint.Y,
						GridPosition.Z + 1,
						IgnoredTopNormal
					);
				}

				const float WallShoulderZ =
					bUseWavyLip
					? FMath::Max(
						BlockMinimum.Z,
						WallTopZ - ChamferDepth
					)
					: WallTopZ;

				WallBottomPoints.Add(
					FVector(
						RoundedPoint.X,
						RoundedPoint.Y,
						BlockMinimum.Z
					)
				);
				WallFootPoints.Add(
					FVector(
						RoundedPoint.X,
						RoundedPoint.Y,
						BlockMinimum.Z +
						GetCliffBaseBlendHeight(
							OriginalPoint,
							FVector2D(WallNormal.X, WallNormal.Y),
							WallShoulderZ - BlockMinimum.Z
						)
					)
				);
				WallShoulderPoints.Add(
					FVector(
						RoundedPoint.X,
						RoundedPoint.Y,
						WallShoulderZ
					)
				);
				WallTopPoints.Add(
					FVector(
						TopPoint.X,
						TopPoint.Y,
						WallTopZ
					)
				);
				WallNormals.Add(WallNormal);
				WallAlongCoordinates.Add(
					FVector2D::DotProduct(
						OriginalPoint,
						WallTangent
					) / SafeGridSize
				);
			}

			for (
				int32 WallPointIndex = 0;
				WallPointIndex < WallBottomPoints.Num() - 1;
				++WallPointIndex
				)
			{
				if (bHasCliffFoot)
				{
					const FColor ContactColor =
						MakeCliffFootVertexColor(1.0f);

					AddContinuousWallQuad(
						Section,
						WallBottomPoints[WallPointIndex],
						WallBottomPoints[WallPointIndex + 1],
						WallFootPoints[WallPointIndex + 1],
						WallFootPoints[WallPointIndex],
						WallNormals[WallPointIndex],
						WallNormals[WallPointIndex + 1],
						SafeGridSize,
						ContactColor,
						ContactColor,
						ContinuousCliffSurfaceColor,
						ContinuousCliffSurfaceColor
					);
				}

				const TArray<FVector>& LowerWallPoints =
					bHasCliffFoot
						? WallFootPoints
						: WallBottomPoints;

				if (bUseWavyLip)
				{
					AddContinuousWallQuad(
						Section,
						LowerWallPoints[WallPointIndex],
						LowerWallPoints[WallPointIndex + 1],
						WallShoulderPoints[WallPointIndex + 1],
						WallShoulderPoints[WallPointIndex],
						WallNormals[WallPointIndex],
						WallNormals[WallPointIndex + 1],
						SafeGridSize
					);
					AddContinuousCliffTopQuad(
						Section,
						WallShoulderPoints[WallPointIndex],
						WallShoulderPoints[WallPointIndex + 1],
						WallTopPoints[WallPointIndex + 1],
						WallTopPoints[WallPointIndex],
						WallNormals[WallPointIndex],
						WallNormals[WallPointIndex + 1],
						SafeGridSize,
						1.0f,
						0.5f,
						WallAlongCoordinates[WallPointIndex],
						WallAlongCoordinates[WallPointIndex + 1],
						ContinuousCliffEdgeSurfaceColor,
						ContinuousCliffEdgeSurfaceColor,
						ContinuousCliffEdgeSurfaceColor,
						ContinuousCliffEdgeSurfaceColor
					);
				}
				else
				{
					AddContinuousWallQuad(
						Section,
						LowerWallPoints[WallPointIndex],
						LowerWallPoints[WallPointIndex + 1],
						WallTopPoints[WallPointIndex + 1],
						WallTopPoints[WallPointIndex],
						WallNormals[WallPointIndex],
						WallNormals[WallPointIndex + 1],
						SafeGridSize
					);
				}
			}
			}
		}

		// A three-block L around one missing cell is a concave cliff corner.
		// The two planar walls stop at tangent points and this curved strip fills
		// the notch without changing the occupied-block source data.
		for (int32 CornerX = 0; CornerX <= 1; ++CornerX)
		{
			for (int32 CornerY = 0; CornerY <= 1; ++CornerY)
			{
				const int32 XDirection = CornerX == 0 ? -1 : 1;
				const int32 YDirection = CornerY == 0 ? -1 : 1;
				const FIntVector SideXPosition =
					GridPosition + FIntVector(XDirection, 0, 0);
				const FIntVector SideYPosition =
					GridPosition + FIntVector(0, YDirection, 0);
				const FIntVector MissingDiagonalPosition =
					GridPosition +
					FIntVector(XDirection, YDirection, 0);

				if (
					!IsContinuousSurfaceBlock(SideXPosition) ||
					!IsContinuousSurfaceBlock(SideYPosition) ||
					HasBlock(MissingDiagonalPosition)
					)
				{
					continue;
				}

				const int32 CornerGridX =
					GridPosition.X + CornerX;
				const int32 CornerGridY =
					GridPosition.Y + CornerY;

				if (
					!IsRoundableConcaveCorner(
						CornerGridX,
						CornerGridY,
						GridPosition.Z
					)
					)
				{
					continue;
				}

				const FVector2D CornerPoint(
					CornerGridX * SafeGridSize,
					CornerGridY * SafeGridSize
				);
				const FVector2D CircleCenter =
					CornerPoint +
					FVector2D(
						XDirection * CornerRadius,
						YDirection * CornerRadius
					);
				const bool bCoveredAbove =
					HasBlock(GridPosition + FIntVector(0, 0, 1));
				const bool bHasConcaveCliffFoot =
					GridPosition.Z > 0 &&
					CliffFootBlendHeight > KINDA_SMALL_NUMBER &&
					IsContinuousSurfaceBlock(
						GridPosition + FIntVector(0, 0, -1)
					) &&
					IsContinuousSurfaceBlock(
						MissingDiagonalPosition +
						FIntVector(0, 0, -1)
					);
				TArray<FVector> ArcBottomPoints;
				TArray<FVector> ArcFootPoints;
				TArray<FVector> ArcTopPoints;
				TArray<FVector> ArcWallNormals;
				TArray<FVector> ArcTopNormals;

				for (
					int32 CornerStep = 0;
					CornerStep <= CornerSubdivisions;
					++CornerStep
					)
				{
					const float Alpha =
						static_cast<float>(CornerStep) /
						static_cast<float>(CornerSubdivisions);
					const FVector2D CircleDirection =
						FVector2D(
							-XDirection * (1.0f - Alpha),
							-YDirection * Alpha
						).GetSafeNormal();
					const FVector2D ArcPoint =
						CircleCenter +
						(CircleDirection * CornerRadius);
					const FVector WallNormal(
						-CircleDirection.X,
						-CircleDirection.Y,
						0.0f
					);
					float ArcTopZ =
						(GridPosition.Z + 1) * SafeGridSize;
					FVector ArcTopNormal = FVector::UpVector;

					if (!bCoveredAbove)
					{
						ArcTopZ -= ChamferDepth;
						GetContinuousTopSurfaceZ(
							ArcPoint.X,
							ArcPoint.Y,
							GridPosition.Z + 1,
							ArcTopNormal
						);
					}

					ArcBottomPoints.Add(
						FVector(
							ArcPoint.X,
							ArcPoint.Y,
							BlockMinimum.Z
						)
					);
					ArcFootPoints.Add(
						FVector(
							ArcPoint.X,
							ArcPoint.Y,
							BlockMinimum.Z +
							GetCliffBaseBlendHeight(
								ArcPoint,
								FVector2D(WallNormal.X, WallNormal.Y),
								ArcTopZ - BlockMinimum.Z
							)
						)
					);
					ArcTopPoints.Add(
						FVector(
							ArcPoint.X,
							ArcPoint.Y,
							ArcTopZ
						)
					);
					ArcWallNormals.Add(WallNormal);
					ArcTopNormals.Add(ArcTopNormal);
				}

				TArray<float> ArcAlongCoordinates;
				const FVector FirstArcTangent =
					ArcTopPoints.Num() > 1
						? (
							ArcTopPoints[1] - ArcTopPoints[0]
						).GetSafeNormal()
						: FVector::ForwardVector;
				ArcAlongCoordinates.Add(
					FVector::DotProduct(
						ArcTopPoints[0],
						FirstArcTangent
					) / SafeGridSize
				);

				for (int32 ArcIndex = 1;
					ArcIndex < ArcTopPoints.Num();
					++ArcIndex)
				{
					ArcAlongCoordinates.Add(
						ArcAlongCoordinates.Last() +
						(
							FVector::Dist(
								ArcTopPoints[ArcIndex - 1],
								ArcTopPoints[ArcIndex]
							) / SafeGridSize
						)
					);
				}

				for (
					int32 ArcIndex = 0;
					ArcIndex < ArcBottomPoints.Num() - 1;
					++ArcIndex
					)
				{
					if (bHasConcaveCliffFoot)
					{
						const FColor ContactColor =
							MakeCliffFootVertexColor(1.0f);

						AddContinuousWallQuad(
							Section,
							ArcBottomPoints[ArcIndex],
							ArcBottomPoints[ArcIndex + 1],
							ArcFootPoints[ArcIndex + 1],
							ArcFootPoints[ArcIndex],
							ArcWallNormals[ArcIndex],
							ArcWallNormals[ArcIndex + 1],
							SafeGridSize,
							ContactColor,
							ContactColor,
							ContinuousCliffSurfaceColor,
							ContinuousCliffSurfaceColor
						);
					}

					AddContinuousWallQuad(
						Section,
						bHasConcaveCliffFoot
							? ArcFootPoints[ArcIndex]
							: ArcBottomPoints[ArcIndex],
						bHasConcaveCliffFoot
							? ArcFootPoints[ArcIndex + 1]
							: ArcBottomPoints[ArcIndex + 1],
						ArcTopPoints[ArcIndex + 1],
						ArcTopPoints[ArcIndex],
						ArcWallNormals[ArcIndex],
						ArcWallNormals[ArcIndex + 1],
						SafeGridSize
					);
				}

				if (!bCoveredAbove)
				{
					FVector CornerTopNormal;
					GetContinuousTopSurfaceZ(
						CornerPoint.X,
						CornerPoint.Y,
						GridPosition.Z + 1,
						CornerTopNormal
					);
					const float CornerTopZ =
						(GridPosition.Z + 1) * SafeGridSize -
						ChamferDepth;
					const FVector CornerTopPoint(
						CornerPoint.X,
						CornerPoint.Y,
						CornerTopZ
					);

					for (
						int32 ArcIndex = 0;
						ArcIndex < ArcTopPoints.Num() - 1;
						++ArcIndex
						)
					{
						const FVector ArcTangent =
							ArcTopPoints[ArcIndex + 1] -
							ArcTopPoints[ArcIndex];
						AddContinuousCliffTopTriangle(
							Section,
							CornerTopPoint,
							ArcTopPoints[ArcIndex],
							ArcTopPoints[ArcIndex + 1],
							CornerTopNormal,
							ArcTopNormals[ArcIndex],
							ArcTopNormals[ArcIndex + 1],
							FVector::UpVector,
							SafeGridSize,
							ArcTangent,
							(
								ArcAlongCoordinates[ArcIndex] +
								ArcAlongCoordinates[ArcIndex + 1]
							) * 0.5f,
							ArcAlongCoordinates[ArcIndex],
							ArcAlongCoordinates[ArcIndex + 1],
							0.0f,
							0.5f,
							0.5f,
							ContinuousCliffEdgeSurfaceColor,
							ContinuousCliffEdgeSurfaceColor,
							ContinuousCliffEdgeSurfaceColor
						);
					}
				}

				if (
					GridPosition.Z > 0 &&
					!HasBlock(
						GridPosition + FIntVector(0, 0, -1)
					) &&
					!HasBlock(
						MissingDiagonalPosition +
						FIntVector(0, 0, -1)
					)
					)
				{
					const FVector CornerBottomPoint(
						CornerPoint.X,
						CornerPoint.Y,
						BlockMinimum.Z
					);

					for (
						int32 ArcIndex = 0;
						ArcIndex < ArcBottomPoints.Num() - 1;
						++ArcIndex
						)
					{
						AddContinuousTriangle(
							Section,
							CornerBottomPoint,
							ArcBottomPoints[ArcIndex],
							ArcBottomPoints[ArcIndex + 1],
							-FVector::UpVector,
							-FVector::UpVector,
							-FVector::UpVector,
							-FVector::UpVector,
							SafeGridSize
						);
					}
				}
			}
		}
	}

	if (SectionsByTileType.Num() == 0)
	{
		return;
	}

	// Diagnose the completed procedural mesh rather than any one source
	// polygon. Continuous helpers intentionally duplicate vertices, so edge
	// incidence must be determined from quantized world-space endpoints rather
	// than raw vertex indices. A horizontal edge used by only one completed
	// triangle is a real open boundary (or one side of a T-junction). Restrict
	// the visualization to the support planes beneath stacked horizontal cuts
	// so ordinary exposed terrain silhouettes do not obscure the fault.
	if (bDebugContinuousSlantSupports && !bPathOverlayOnly)
	{
		struct FCompletedDiagnosticEdge
		{
			FVector A;
			FVector B;
			int32 IncidenceCount = 0;
			int32 FirstTileType = INDEX_NONE;
		};

		TArray<FIntVector> DiagnosticSupportCells;

		for (const FIntVector& GridPosition : ChunkBlocks)
		{
			const FIntVector AbovePosition =
				GridPosition + FIntVector(0, 0, 1);

			if (
				IsContinuousSurfaceBlock(GridPosition) &&
				IsContinuousSurfaceBlock(AbovePosition) &&
				IsContinuousDiagonalTileType(
					GetBlockTileType(AbovePosition)
				)
				)
			{
				DiagnosticSupportCells.Add(GridPosition);
			}
		}

		if (DiagnosticSupportCells.Num() > 0)
		{
			TMap<FString, FCompletedDiagnosticEdge> CompletedEdges;
			constexpr float DiagnosticPositionScale = 100.0f;

			auto MakeDiagnosticPointKey =
				[&](const FVector& Point)
				{
					return FString::Printf(
						TEXT("%d,%d,%d"),
						FMath::RoundToInt(
							Point.X * DiagnosticPositionScale
						),
						FMath::RoundToInt(
							Point.Y * DiagnosticPositionScale
						),
						FMath::RoundToInt(
							Point.Z * DiagnosticPositionScale
						)
					);
				};

			auto AddCompletedDiagnosticEdge =
				[&](
					const FVector& A,
					const FVector& B,
					int32 TileType
				)
				{
					if (A.Equals(B, KINDA_SMALL_NUMBER))
					{
						return;
					}

					const FString AKey = MakeDiagnosticPointKey(A);
					const FString BKey = MakeDiagnosticPointKey(B);
					const bool bKeepOrder =
						AKey.Compare(
							*BKey,
							ESearchCase::CaseSensitive
						) <= 0;
					const FString EdgeKey = bKeepOrder
						? AKey + TEXT("|") + BKey
						: BKey + TEXT("|") + AKey;
					FCompletedDiagnosticEdge& Edge =
						CompletedEdges.FindOrAdd(EdgeKey);

					if (Edge.IncidenceCount == 0)
					{
						Edge.A = bKeepOrder ? A : B;
						Edge.B = bKeepOrder ? B : A;
						Edge.FirstTileType = TileType;
					}

					++Edge.IncidenceCount;
				};

			for (
				const TPair<int32, FTileMapContinuousSection>& SectionPair :
				SectionsByTileType
				)
			{
				const FTileMapContinuousSection& Section =
					SectionPair.Value;

				for (
					int32 TriangleIndex = 0;
					TriangleIndex + 2 < Section.Triangles.Num();
					TriangleIndex += 3
					)
				{
					const int32 IndexA =
						Section.Triangles[TriangleIndex];
					const int32 IndexB =
						Section.Triangles[TriangleIndex + 1];
					const int32 IndexC =
						Section.Triangles[TriangleIndex + 2];

					if (
						!Section.Vertices.IsValidIndex(IndexA) ||
						!Section.Vertices.IsValidIndex(IndexB) ||
						!Section.Vertices.IsValidIndex(IndexC)
						)
					{
						continue;
					}

					const FVector& A = Section.Vertices[IndexA];
					const FVector& B = Section.Vertices[IndexB];
					const FVector& C = Section.Vertices[IndexC];

					AddCompletedDiagnosticEdge(
						A,
						B,
						SectionPair.Key
					);
					AddCompletedDiagnosticEdge(
						B,
						C,
						SectionPair.Key
					);
					AddCompletedDiagnosticEdge(
						C,
						A,
						SectionPair.Key
					);
				}
			}

			int32 SupportHorizontalEdgeCount = 0;
			int32 UnmatchedHorizontalEdgeCount = 0;
			int32 NonManifoldHorizontalEdgeCount = 0;
			int32 LoggedEdgeCount = 0;

			for (
				const TPair<FString, FCompletedDiagnosticEdge>& EdgePair :
				CompletedEdges
				)
			{
				const FCompletedDiagnosticEdge& Edge = EdgePair.Value;

				if (FMath::Abs(Edge.A.Z - Edge.B.Z) > 0.02f)
				{
					continue;
				}

				const FVector Midpoint = (Edge.A + Edge.B) * 0.5f;
				bool bNearStackedSupportPlane = false;

				for (
					const FIntVector& SupportCell :
					DiagnosticSupportCells
					)
				{
					const float SupportZ =
						(SupportCell.Z + 1) * SafeGridSize;
					const float MinimumX =
						(SupportCell.X - 0.35f) * SafeGridSize;
					const float MaximumX =
						(SupportCell.X + 1.35f) * SafeGridSize;
					const float MinimumY =
						(SupportCell.Y - 0.35f) * SafeGridSize;
					const float MaximumY =
						(SupportCell.Y + 1.35f) * SafeGridSize;

					if (
						FMath::Abs(Midpoint.Z - SupportZ) <= 0.02f &&
						Midpoint.X >= MinimumX &&
						Midpoint.X <= MaximumX &&
						Midpoint.Y >= MinimumY &&
						Midpoint.Y <= MaximumY
						)
					{
						bNearStackedSupportPlane = true;
						break;
					}
				}

				if (!bNearStackedSupportPlane)
				{
					continue;
				}

				++SupportHorizontalEdgeCount;

				FColor EdgeColor = FColor::Transparent;

				if (Edge.IncidenceCount == 1)
				{
					++UnmatchedHorizontalEdgeCount;
					EdgeColor = FColor::Red;
				}
				else if (Edge.IncidenceCount > 2)
				{
					++NonManifoldHorizontalEdgeCount;
					EdgeColor = FColor(255, 128, 0);
				}

				if (EdgeColor == FColor::Transparent)
				{
					continue;
				}

				const FVector WorldA =
					GetActorTransform().TransformPosition(Edge.A);
				const FVector WorldB =
					GetActorTransform().TransformPosition(Edge.B);

				DrawDebugLine(
					GetWorld(),
					WorldA,
					WorldB,
					EdgeColor,
					false,
					60.0f,
					0,
					4.0f
				);
				DrawDebugPoint(
					GetWorld(),
					WorldA,
					8.0f,
					EdgeColor,
					false,
					60.0f,
					0
				);

				if (LoggedEdgeCount < 256)
				{
					UE_LOG(
						LogTemp,
						Warning,
						TEXT(
							"TileMap completed edge diagnostic: "
							"chunk=(%d,%d,%d) incidence=%d tile=%d "
							"a=(%.4f,%.4f,%.4f) b=(%.4f,%.4f,%.4f) "
							"mid=(%.4f,%.4f,%.4f) length=%.4f"
						),
						ChunkCoordinate.X,
						ChunkCoordinate.Y,
						ChunkCoordinate.Z,
						Edge.IncidenceCount,
						Edge.FirstTileType,
						Edge.A.X,
						Edge.A.Y,
						Edge.A.Z,
						Edge.B.X,
						Edge.B.Y,
						Edge.B.Z,
						Midpoint.X,
						Midpoint.Y,
						Midpoint.Z,
						FVector::Distance(Edge.A, Edge.B)
					);
					++LoggedEdgeCount;
				}
			}

			UE_LOG(
				LogTemp,
				Warning,
				TEXT(
					"TileMap completed edge diagnostic summary: "
					"chunk=(%d,%d,%d) support_cells=%d "
					"geometric_edges=%d support_horizontal=%d "
					"unmatched=%d nonmanifold=%d"
				),
				ChunkCoordinate.X,
				ChunkCoordinate.Y,
				ChunkCoordinate.Z,
				DiagnosticSupportCells.Num(),
				CompletedEdges.Num(),
				SupportHorizontalEdgeCount,
				UnmatchedHorizontalEdgeCount,
				NonManifoldHorizontalEdgeCount
			);
		}
	}

	UProceduralMeshComponent* ContinuousComponent =
		FindOrCreateContinuousChunkComponent(
			ChunkCoordinate,
			bPathOverlayOnly
		);

	if (!ContinuousComponent)
	{
		return;
	}

	TArray<int32> TileTypes;
	SectionsByTileType.GetKeys(TileTypes);
	TileTypes.Sort();

	for (int32 SectionIndex = 0; SectionIndex < TileTypes.Num(); ++SectionIndex)
	{
		const int32 TileType = TileTypes[SectionIndex];
		FTileMapContinuousSection* Section =
			SectionsByTileType.Find(TileType);
		FTileMapContinuousSection PathOverlaySection;

		if (
			bPathOverlayOnly &&
			Section &&
			!BuildPathOverlaySection(*Section, PathOverlaySection)
			)
		{
			continue;
		}

		FTileMapContinuousSection* RenderSection =
			bPathOverlayOnly ? &PathOverlaySection : Section;

		if (
			!RenderSection ||
			RenderSection->Vertices.Num() == 0 ||
			RenderSection->Triangles.Num() == 0
			)
		{
			continue;
		}

		ContinuousComponent->CreateMeshSection(
			SectionIndex,
			RenderSection->Vertices,
			RenderSection->Triangles,
			RenderSection->Normals,
			RenderSection->UV0,
			RenderSection->VertexColors,
			RenderSection->Tangents,
			bGenerateCollision && !bPathOverlayOnly
		);

		UMaterialInterface* Material =
			GetTileMaterialOverride(TileType);

		if (!Material)
		{
			UStaticMesh* TileMesh = GetTileMesh(TileType);
			Material = TileMesh ? TileMesh->GetMaterial(0) : nullptr;
		}

		if (Material)
		{
			ContinuousComponent->SetMaterial(
				SectionIndex,
				Material
			);
		}
	}

	ContinuousComponent->SetCollisionEnabled(
		bGenerateCollision && !bPathOverlayOnly
		? ECollisionEnabled::QueryAndPhysics
		: ECollisionEnabled::NoCollision
	);
	ContinuousComponent->MarkRenderStateDirty();
}

UHierarchicalInstancedStaticMeshComponent*
ATileMapTerrainActor::FindOrCreateChunkComponent(
	const FIntVector& ChunkCoordinate,
	int32 TileType
)
{
	const int32 SafeTileType =
		ClampTileType(TileType);

	const FTileMapChunkMeshKey Key(
		ChunkCoordinate,
		SafeTileType
	);

	UHierarchicalInstancedStaticMeshComponent**
		ExistingComponent =
		ChunkComponentLookup.Find(Key);

	if (
		ExistingComponent &&
		IsValid(*ExistingComponent)
		)
	{
		return *ExistingComponent;
	}

	UStaticMesh* TileMesh =
		GetTileMesh(SafeTileType);

	if (!TileMesh)
	{
		return nullptr;
	}

	const FName ComponentName =
		MakeUniqueObjectName(
			this,
			UHierarchicalInstancedStaticMeshComponent::StaticClass(),
			FName(
				*FString::Printf(
					TEXT("TileChunk_%d_%d_%d_Type_%d"),
					ChunkCoordinate.X,
					ChunkCoordinate.Y,
					ChunkCoordinate.Z,
					SafeTileType
				)
			)
		);

	UHierarchicalInstancedStaticMeshComponent* NewComponent =
		NewObject<UHierarchicalInstancedStaticMeshComponent>(
			this,
			ComponentName,
			RF_Transient |
			RF_DuplicateTransient |
			RF_TextExportTransient
		);

	if (!NewComponent)
	{
		return nullptr;
	}

	// These components are only a render/collision cache. Persistent edits and
	// Undo/Redo live in the serialized block arrays above.
	NewComponent->ClearFlags(RF_Transactional);
	NewComponent->CreationMethod =
		EComponentCreationMethod::Instance;

	NewComponent->SetupAttachment(SceneRoot);
	NewComponent->SetRelativeTransform(FTransform::Identity);
	NewComponent->SetMobility(EComponentMobility::Movable);
	NewComponent->SetStaticMesh(TileMesh);
	NewComponent->SetGenerateOverlapEvents(false);
	NewComponent->SetCollisionProfileName(TEXT("BlockAll"));
	NewComponent->SetCastShadow(true);
	NewComponent->bCastStaticShadow = false;
	NewComponent->bCastDynamicShadow = true;

	UMaterialInterface* MaterialOverride =
		GetTileMaterialOverride(SafeTileType);

	if (MaterialOverride)
	{
		NewComponent->SetMaterial(0, MaterialOverride);
	}

	NewComponent->SetCollisionEnabled(
		bGenerateCollision
		? ECollisionEnabled::QueryAndPhysics
		: ECollisionEnabled::NoCollision
	);

	AddOwnedComponent(NewComponent);
	NewComponent->RegisterComponent();

	ChunkComponents.Add(NewComponent);
	ChunkComponentLookup.Add(Key, NewComponent);

	return NewComponent;
}

void ATileMapTerrainActor::DestroyChunkComponents(
	const FIntVector& ChunkCoordinate
)
{
	TArray<FTileMapChunkMeshKey> KeysToRemove;

	for (
		const TPair<
			FTileMapChunkMeshKey,
			UHierarchicalInstancedStaticMeshComponent*
		>& Pair : ChunkComponentLookup
		)
	{
		if (
			Pair.Key.ChunkCoordinate ==
			ChunkCoordinate
			)
		{
			KeysToRemove.Add(Pair.Key);
		}
	}

	for (const FTileMapChunkMeshKey& Key : KeysToRemove)
	{
		UHierarchicalInstancedStaticMeshComponent**
			ExistingComponent =
			ChunkComponentLookup.Find(Key);

		UHierarchicalInstancedStaticMeshComponent*
			ComponentToRemove =
			ExistingComponent
			? *ExistingComponent
			: nullptr;

		ChunkComponentLookup.Remove(Key);
		ChunkComponents.RemoveSingleSwap(ComponentToRemove);

		if (IsValid(ComponentToRemove))
		{
			ComponentToRemove->ClearInstances();
			RemoveInstanceComponent(ComponentToRemove);
			RemoveOwnedComponent(ComponentToRemove);
			ComponentToRemove->DestroyComponent();
		}
	}

	auto DestroyProceduralChunkComponent =
		[&](
			TMap<FIntVector, UProceduralMeshComponent*>& ComponentLookup
		)
		{
			UProceduralMeshComponent** ExistingComponent =
				ComponentLookup.Find(ChunkCoordinate);
			UProceduralMeshComponent* ComponentToRemove =
				ExistingComponent ? *ExistingComponent : nullptr;

			ComponentLookup.Remove(ChunkCoordinate);
			ContinuousChunkComponents.RemoveSingleSwap(
				ComponentToRemove
			);

			if (IsValid(ComponentToRemove))
			{
				ComponentToRemove->ClearAllMeshSections();
				RemoveInstanceComponent(ComponentToRemove);
				RemoveOwnedComponent(ComponentToRemove);
				ComponentToRemove->DestroyComponent();
			}
		};

	DestroyProceduralChunkComponent(
		ContinuousChunkComponentLookup
	);
	DestroyProceduralChunkComponent(
		PathOverlayChunkComponentLookup
	);
}

void ATileMapTerrainActor::RebuildChunk(
	const FIntVector& ChunkCoordinate
)
{
	DestroyChunkComponents(ChunkCoordinate);

	const TArray<FIntVector>* ChunkBlocks =
		ChunkBlocksLookup.Find(ChunkCoordinate);

	if (!ChunkBlocks || ChunkBlocks->Num() == 0)
	{
		return;
	}

	if (bUseContinuousTerrainPrototype)
	{
		BuildContinuousChunk(
			ChunkCoordinate,
			*ChunkBlocks
		);
	}

	if (PaintedPathLookup.Num() > 0)
	{
		BuildContinuousChunk(
			ChunkCoordinate,
			*ChunkBlocks,
			true
		);
	}

	TMap<int32, TArray<FIntVector>> BlocksByTileType;

	for (const FIntVector& GridPosition : *ChunkBlocks)
	{
		const int32 TileType = GetBlockTileType(GridPosition);

		if (
			ShouldUseContinuousTerrainForBlock(GridPosition)
			)
		{
			continue;
		}

		BlocksByTileType.FindOrAdd(
			TileType
		).Add(GridPosition);
	}

	for (
		const TPair<int32, TArray<FIntVector>>& Pair :
		BlocksByTileType
		)
	{
		UHierarchicalInstancedStaticMeshComponent*
			ChunkComponent =
			FindOrCreateChunkComponent(
				ChunkCoordinate,
				Pair.Key
			);

		if (!ChunkComponent)
		{
			continue;
		}

		ChunkComponent->SetMobility(
			EComponentMobility::Movable
		);

		ChunkComponent->SetCollisionEnabled(
			bGenerateCollision
			? ECollisionEnabled::QueryAndPhysics
			: ECollisionEnabled::NoCollision
		);

		// One synchronous HISM tree build per affected tile group.
		ChunkComponent->bAutoRebuildTreeOnInstanceChanges = false;
		ChunkComponent->ClearInstances();

		for (const FIntVector& GridPosition : Pair.Value)
		{
			ChunkComponent->AddInstance(
				GetBlockLocalTransform(GridPosition)
			);
		}

		ChunkComponent->BuildTreeIfOutdated(false, true);
		ChunkComponent->bAutoRebuildTreeOnInstanceChanges = true;
		ChunkComponent->MarkRenderStateDirty();
	}
}

void ATileMapTerrainActor::DestroyAllChunkComponents()
{
	// Query the actor too, removing caches left by older plugin revisions.
	TInlineComponentArray<
		UHierarchicalInstancedStaticMeshComponent*
	> ExistingComponents(this);

	for (
		UHierarchicalInstancedStaticMeshComponent* ChunkComponent :
		ExistingComponents
		)
	{
		if (
			!IsValid(ChunkComponent) ||
			!ChunkComponent->GetName().StartsWith(TEXT("TileChunk_"))
			)
		{
			continue;
		}

		ChunkComponent->ClearInstances();
		RemoveInstanceComponent(ChunkComponent);
		RemoveOwnedComponent(ChunkComponent);
		ChunkComponent->DestroyComponent();
	}

	ChunkComponents.Reset();
	ChunkComponentLookup.Reset();

	TInlineComponentArray<UProceduralMeshComponent*>
		ExistingContinuousComponents(this);

	for (
		UProceduralMeshComponent* ContinuousComponent :
		ExistingContinuousComponents
		)
	{
		if (
			!IsValid(ContinuousComponent) ||
			(
				!ContinuousComponent->GetName().StartsWith(
					TEXT("TileContinuousChunk_")
				) &&
				!ContinuousComponent->GetName().StartsWith(
					TEXT("TilePathOverlayChunk_")
				)
			)
			)
		{
			continue;
		}

		ContinuousComponent->ClearAllMeshSections();
		RemoveInstanceComponent(ContinuousComponent);
		RemoveOwnedComponent(ContinuousComponent);
		ContinuousComponent->DestroyComponent();
	}

	ContinuousChunkComponents.Reset();
	ContinuousChunkComponentLookup.Reset();
	PathOverlayChunkComponentLookup.Reset();
}

void ATileMapTerrainActor::RebuildAllChunks()
{
	DestroyAllChunkComponents();
	RebuildDerivedLookups();
	ResolveAllAutoTiles();

	TArray<FIntVector> RequiredChunks;
	ChunkBlocksLookup.GetKeys(RequiredChunks);

	for (const FIntVector& ChunkCoordinate : RequiredChunks)
	{
		RebuildChunk(ChunkCoordinate);
	}
}
