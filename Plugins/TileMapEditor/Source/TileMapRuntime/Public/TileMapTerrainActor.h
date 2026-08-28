// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "TileMapTerrainActor.generated.h"

class UHierarchicalInstancedStaticMeshComponent;
class UMaterialInterface;
class UProceduralMeshComponent;
class USceneComponent;
class UStaticMesh;
struct FHitResult;
struct FPropertyChangedEvent;

/**
 * Optional role used by an elevation-aware modular tile set.
 * Entries sharing AutoTileSet are treated as visual variants of one style.
 */
UENUM(BlueprintType)
enum class ETileMapAutoShape : uint8
{
	Manual UMETA(DisplayName = "Manual / Any"),
	Flat UMETA(DisplayName = "Flat"),
	Edge UMETA(DisplayName = "Edge"),
	OuterCorner UMETA(DisplayName = "Outer Corner"),
	InnerCorner UMETA(DisplayName = "Inner Corner"),
	Cliff UMETA(DisplayName = "Cliff"),
	Pass UMETA(DisplayName = "Pass / Bridge"),
	Peninsula UMETA(DisplayName = "Peninsula"),
	Island UMETA(DisplayName = "Island"),
	Ramp UMETA(DisplayName = "Ramp"),
	DiagonalEdge UMETA(DisplayName = "Diagonal Edge"),
	Stair UMETA(DisplayName = "Stair")
};

/**
 * One modular tile that can be selected from the Tile Map mode palette.
 * Meshes should be authored for a 100 x 100 x 100 Unreal-unit cell.
 */
USTRUCT(BlueprintType)
struct TILEMAPRUNTIME_API FTileMapTileDefinition
{
	GENERATED_BODY()

	FTileMapTileDefinition()
		:
		TileName(NAME_None),
		AutoTileSet(NAME_None),
		AutoShape(ETileMapAutoShape::Manual),
		Mesh(nullptr),
		MaterialOverride(nullptr),
		MeshScale(FVector::OneVector),
		PivotOffset(FVector::ZeroVector)
	{
	}

	UPROPERTY(
		EditAnywhere,
		BlueprintReadOnly,
		Category = "Tile"
	)
	FName TileName;

	/**
	 * Optional style/set name, for example Grass or Stone. Entries with the
	 * same non-empty value can automatically exchange shape and rotation as
	 * surrounding elevation changes.
	 */
	UPROPERTY(
		EditAnywhere,
		BlueprintReadOnly,
		Category = "Tile|Auto Tiling"
	)
	FName AutoTileSet;

	/** Geometry role this entry supplies inside its AutoTileSet. */
	UPROPERTY(
		EditAnywhere,
		BlueprintReadOnly,
		Category = "Tile|Auto Tiling"
	)
	ETileMapAutoShape AutoShape;

	UPROPERTY(
		EditAnywhere,
		BlueprintReadOnly,
		Category = "Tile"
	)
	UStaticMesh* Mesh;

	/** Optional material for slot 0. Leave empty to use the mesh material. */
	UPROPERTY(
		EditAnywhere,
		BlueprintReadOnly,
		Category = "Tile"
	)
	UMaterialInterface* MaterialOverride;

	/** Multiplied by the normal GridSize / 100 scale. */
	UPROPERTY(
		EditAnywhere,
		BlueprintReadOnly,
		Category = "Tile"
	)
	FVector MeshScale;

	/** Local offset at the 100-unit authoring size, rotated with the tile. */
	UPROPERTY(
		EditAnywhere,
		BlueprintReadOnly,
		Category = "Tile"
	)
	FVector PivotOffset;
};

/** Surface region where a bake-time structural terrain detail may appear. */
UENUM(BlueprintType)
enum class ETileMapTerrainDetailPlacement : uint8
{
	Ground UMETA(DisplayName = "Ground Bump / Mound"),
	CliffTop UMETA(DisplayName = "Cliff Top / Corner"),
	CliffBase UMETA(DisplayName = "Cliff Base")
};

/**
 * One optional low-poly structural detail used only by the bake-time terrain
 * detail pass. These are rocks, mounds, and short layered terrain pieces; the
 * ordinary foliage tool remains responsible for vegetation.
 */
USTRUCT(BlueprintType)
struct TILEMAPRUNTIME_API FTileMapTerrainDetailDefinition
{
	GENERATED_BODY()

	FTileMapTerrainDetailDefinition()
		:
		Placement(ETileMapTerrainDetailPlacement::Ground),
		Mesh(nullptr),
		MaterialOverride(nullptr),
		Weight(1.0f),
		MeshScale(FVector::OneVector),
		UniformScaleRange(FVector2D(0.9f, 1.1f)),
		SinkDepth(8.0f),
		bRandomYaw(true),
		bEnableCollision(false)
	{
	}

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Terrain Detail")
	ETileMapTerrainDetailPlacement Placement;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Terrain Detail")
	UStaticMesh* Mesh;

	/** Optional material for slot 0. Leave empty to keep the mesh material. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Terrain Detail")
	UMaterialInterface* MaterialOverride;

	/** Relative selection weight among valid entries in the same region. */
	UPROPERTY(
		EditAnywhere,
		BlueprintReadOnly,
		Category = "Terrain Detail",
		meta = (ClampMin = "0.0")
	)
	float Weight;

	/** Multiplied by the normal GridSize / 100 authoring scale. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Terrain Detail")
	FVector MeshScale;

	/** Random uniform multiplier applied after MeshScale. */
	UPROPERTY(
		EditAnywhere,
		BlueprintReadOnly,
		Category = "Terrain Detail",
		meta = (DisplayName = "Uniform Scale Range")
	)
	FVector2D UniformScaleRange;

	/** Distance sunk into the supporting surface at the 100-unit authoring size. */
	UPROPERTY(
		EditAnywhere,
		BlueprintReadOnly,
		Category = "Terrain Detail",
		meta = (ClampMin = "0.0", ClampMax = "100.0")
	)
	float SinkDepth;

	/**
	 * Randomizes yaw. When disabled, +X faces out from a cliff top or toward
	 * the wall at a cliff base. Ground entries use zero yaw.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Terrain Detail")
	bool bRandomYaw;

	/** Disabled by default so visual terrain details do not alter navigation. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Terrain Detail")
	bool bEnableCollision;
};

struct FTileMapChunkMeshKey
{
	FIntVector ChunkCoordinate;
	int32 TileType;

	FTileMapChunkMeshKey()
		:
		ChunkCoordinate(FIntVector::ZeroValue),
		TileType(0)
	{
	}

	FTileMapChunkMeshKey(
		const FIntVector& InChunkCoordinate,
		int32 InTileType
	)
		:
		ChunkCoordinate(InChunkCoordinate),
		TileType(InTileType)
	{
	}

	bool operator==(
		const FTileMapChunkMeshKey& Other
	) const
	{
		return
			ChunkCoordinate == Other.ChunkCoordinate &&
			TileType == Other.TileType;
	}
};

FORCEINLINE uint32 GetTypeHash(
	const FTileMapChunkMeshKey& Key
)
{
	return HashCombine(
		GetTypeHash(Key.ChunkCoordinate),
		GetTypeHash(Key.TileType)
	);
}

UCLASS(BlueprintType)
class TILEMAPRUNTIME_API ATileMapTerrainActor : public AActor
{
	GENERATED_BODY()

public:
	ATileMapTerrainActor();

	UPROPERTY(
		EditAnywhere,
		BlueprintReadOnly,
		Category = "Tile Map",
		meta = (ClampMin = "1.0")
	)
	float GridSize;

	UPROPERTY(
		EditAnywhere,
		BlueprintReadOnly,
		Category = "Tile Map",
		meta = (ClampMin = "1", ClampMax = "64")
	)
	int32 ChunkSize;

	/** Tile type 0. Existing terrain remains compatible with this mesh. */
	UPROPERTY(
		EditAnywhere,
		BlueprintReadOnly,
		Category = "Tile Map|Palette"
	)
	UStaticMesh* BlockMesh;

	/**
	 * Optional shared material for the complete terrain actor. When assigned,
	 * it takes precedence over mesh and palette materials so flat blocks,
	 * generated slants, rebuilt chunks, and baked meshes remain consistent.
	 */
	UPROPERTY(
		EditAnywhere,
		BlueprintReadOnly,
		Category = "Tile Map|Rendering",
		meta = (DisplayName = "Default Terrain Material")
	)
	UMaterialInterface* DefaultTerrainMaterial;

	/**
	 * Opt-in prototype that derives one exposed terrain surface per chunk from
	 * OccupiedBlocks. Ordinary unstacked 45-degree horizontal diagonal cuts are
	 * consumed by the same surface; ramps and unsupported diagonal cases keep
	 * their existing modular meshes. Clearing this restores the complete HISM
	 * path.
	 */
	UPROPERTY(
		EditAnywhere,
		BlueprintReadOnly,
		Category = "Tile Map|Continuous Terrain",
		meta = (DisplayName = "Use Continuous Terrain Prototype")
	)
	bool bUseContinuousTerrainPrototype;

	/** Horizontal width of the chamfer measured inward from a cliff edge. */
	UPROPERTY(
		EditAnywhere,
		BlueprintReadOnly,
		Category = "Tile Map|Continuous Terrain",
		meta = (
			ClampMin = "1.0",
			ClampMax = "30.0",
			DisplayName = "Chamfer Width",
			EditCondition = "bUseContinuousTerrainPrototype"
		)
	)
	float ContinuousChamferWidth;

	/** Vertical drop from the flat ground to the planar cliff wall. */
	UPROPERTY(
		EditAnywhere,
		BlueprintReadOnly,
		Category = "Tile Map|Continuous Terrain",
		meta = (
			ClampMin = "1.0",
			ClampMax = "25.0",
			DisplayName = "Chamfer Depth",
			EditCondition = "bUseContinuousTerrainPrototype"
		)
	)
	float ContinuousChamferDepth;

	/** Maximum horizontal displacement of only the exposed cliff lip. */
	UPROPERTY(
		EditAnywhere,
		BlueprintReadOnly,
		Category = "Tile Map|Continuous Terrain",
		meta = (
			ClampMin = "0.0",
			ClampMax = "10.0",
			DisplayName = "Cliff Edge Irregularity",
			EditCondition = "bUseContinuousTerrainPrototype"
		)
	)
	float ContinuousEdgeIrregularity;

	/** World-space wavelength used by the deterministic cliff-lip curve. */
	UPROPERTY(
		EditAnywhere,
		BlueprintReadOnly,
		Category = "Tile Map|Continuous Terrain",
		meta = (
			ClampMin = "25.0",
			ClampMax = "200.0",
			DisplayName = "Cliff Edge Wavelength",
			EditCondition = "bUseContinuousTerrainPrototype"
		)
	)
	float ContinuousEdgeWavelength;

	/** Radius used to physically round exposed vertical cliff corners. */
	UPROPERTY(
		EditAnywhere,
		BlueprintReadOnly,
		Category = "Tile Map|Continuous Terrain",
		meta = (
			ClampMin = "1.0",
			ClampMax = "20.0",
			DisplayName = "Cliff Corner Radius",
			EditCondition = "bUseContinuousTerrainPrototype"
		)
	)
	float ContinuousCliffCornerRadius;

	/** Ground distance over which the generated cliff-foot vertex mask fades. */
	UPROPERTY(
		EditAnywhere,
		BlueprintReadOnly,
		Category = "Tile Map|Continuous Terrain",
		meta = (
			ClampMin = "0.0",
			ClampMax = "30.0",
			DisplayName = "Cliff Foot Blend Width",
			EditCondition = "bUseContinuousTerrainPrototype"
		)
	)
	float ContinuousCliffFootBlendWidth;

	/** Cliff height over which the generated cliff-foot vertex mask fades. */
	UPROPERTY(
		EditAnywhere,
		BlueprintReadOnly,
		Category = "Tile Map|Continuous Terrain",
		meta = (
			ClampMin = "0.0",
			ClampMax = "30.0",
			DisplayName = "Cliff Foot Blend Height",
			EditCondition = "bUseContinuousTerrainPrototype"
		)
	)
	float ContinuousCliffFootBlendHeight;

	/**
	 * Half-width of the soft material transition at a painted path boundary.
	 * The generated path mask is stored as OneMinus(VertexColor.G), leaving
	 * VertexColor.R reserved for the existing cliff-foot blend.
	 */
	UPROPERTY(
		EditAnywhere,
		BlueprintReadOnly,
		Category = "Tile Map|Path Painting",
		meta = (
			ClampMin = "0.0",
			ClampMax = "50.0",
			DisplayName = "Path Edge Blend Width"
		)
	)
	float ContinuousPathBlendWidth;

	/**
	 * Opt-in bake-time structural detail pass. It creates a separate actor with
	 * HISM components and never inserts detail triangles into the optimized
	 * terrain static mesh.
	 */
	UPROPERTY(
		EditAnywhere,
		BlueprintReadOnly,
		Category = "Tile Map|Terrain Detail Pass",
		meta = (DisplayName = "Generate Terrain Details During Bake")
	)
	bool bGenerateTerrainDetailsOnBake;

	UPROPERTY(
		EditAnywhere,
		BlueprintReadOnly,
		Category = "Tile Map|Terrain Detail Pass",
		meta = (EditCondition = "bGenerateTerrainDetailsOnBake")
	)
	int32 TerrainDetailSeed;

	/** Probability that one eligible exposed surface cell receives a detail. */
	UPROPERTY(
		EditAnywhere,
		BlueprintReadOnly,
		Category = "Tile Map|Terrain Detail Pass",
		meta = (
			ClampMin = "0.0",
			ClampMax = "1.0",
			EditCondition = "bGenerateTerrainDetailsOnBake"
		)
	)
	float TerrainDetailDensity;

	UPROPERTY(
		EditAnywhere,
		BlueprintReadOnly,
		Category = "Tile Map|Terrain Detail Pass",
		meta = (
			ClampMin = "0",
			ClampMax = "4096",
			DisplayName = "Maximum Detail Instances",
			EditCondition = "bGenerateTerrainDetailsOnBake"
		)
	)
	int32 TerrainDetailMaximumInstances;

	UPROPERTY(
		EditAnywhere,
		BlueprintReadOnly,
		Category = "Tile Map|Terrain Detail Pass",
		meta = (
			ClampMin = "0",
			ClampMax = "16",
			DisplayName = "Minimum Spacing (Grid Cells)",
			EditCondition = "bGenerateTerrainDetailsOnBake"
		)
	)
	int32 TerrainDetailMinimumSpacingCells;

	/** Distance kept inside a cliff boundary at the 100-unit authoring size. */
	UPROPERTY(
		EditAnywhere,
		BlueprintReadOnly,
		Category = "Tile Map|Terrain Detail Pass",
		meta = (
			ClampMin = "0.0",
			ClampMax = "50.0",
			DisplayName = "Cliff Detail Edge Inset",
			EditCondition = "bGenerateTerrainDetailsOnBake"
		)
	)
	float TerrainDetailEdgeInset;

	UPROPERTY(
		EditAnywhere,
		BlueprintReadOnly,
		Category = "Tile Map|Terrain Detail Pass",
		meta = (
			ClampMin = "0",
			DisplayName = "Detail Start Cull Distance",
			EditCondition = "bGenerateTerrainDetailsOnBake"
		)
	)
	int32 TerrainDetailStartCullDistance;

	UPROPERTY(
		EditAnywhere,
		BlueprintReadOnly,
		Category = "Tile Map|Terrain Detail Pass",
		meta = (
			ClampMin = "0",
			DisplayName = "Detail End Cull Distance",
			EditCondition = "bGenerateTerrainDetailsOnBake"
		)
	)
	int32 TerrainDetailEndCullDistance;

	/** User-supplied low-poly structural meshes grouped by placement region. */
	UPROPERTY(
		EditAnywhere,
		BlueprintReadOnly,
		Category = "Tile Map|Terrain Detail Pass",
		meta = (EditCondition = "bGenerateTerrainDetailsOnBake")
	)
	TArray<FTileMapTerrainDetailDefinition> TerrainDetailPalette;

	/** Tile types 1 and above. Add modular meshes here in the Details panel. */
	UPROPERTY(
		EditAnywhere,
		BlueprintReadOnly,
		Category = "Tile Map|Palette"
	)
	TArray<FTileMapTileDefinition> TilePalette;

	UPROPERTY(
		EditAnywhere,
		BlueprintReadOnly,
		Category = "Tile Map"
	)
	bool bGenerateCollision;

	UPROPERTY(
		VisibleAnywhere,
		Category = "Tile Map"
	)
	TArray<FIntVector> OccupiedBlocks;

	/**
	 * Persistent path layer. Entries identify occupied source blocks whose
	 * exposed upward surface contributes to the continuous-terrain path mask.
	 */
	UPROPERTY(
		VisibleAnywhere,
		BlueprintReadOnly,
		Category = "Tile Map|Path Painting"
	)
	TArray<FIntVector> PaintedPathBlocks;

	UPROPERTY(
		VisibleAnywhere,
		BlueprintReadOnly,
		Category = "Tile Map"
	)
	USceneComponent* SceneRoot;

	bool HasBlock(
		const FIntVector& GridPosition
	) const;

	bool AddBlock(
		const FIntVector& GridPosition,
		int32 TileType = 0,
		uint8 QuarterTurns = 0
	);

	/**
	 * Adds a prevalidated group of blocks as one efficient batch. The arrays
	 * must be aligned, every position must be unique, and no destination cell
	 * may already be occupied. Auto tiles and dirty neighboring chunks are
	 * refreshed once after the complete group has been inserted.
	 */
	bool AddBlocks(
		const TArray<FIntVector>& GridPositions,
		const TArray<int32>& TileTypes,
		const TArray<uint8>& QuarterTurns
	);

	bool RemoveBlock(
		const FIntVector& GridPosition
	);

	/** Removes several blocks and refreshes each affected HISM chunk once. */
	bool RemoveBlocks(
		const TArray<FIntVector>& GridPositions
	);

	bool MoveBlock(
		const FIntVector& FromGridPosition,
		const FIntVector& ToGridPosition
	);

	bool SetBlockTileType(
		const FIntVector& GridPosition,
		int32 TileType
	);

	bool HasPaintedPath(
		const FIntVector& GridPosition
	) const;

	/** Paints or erases the independent path layer on one occupied block. */
	bool SetPathPainted(
		const FIntVector& GridPosition,
		bool bPainted
	);

	/** Efficient batch used by copied-terrain merging. */
	bool SetPathsPainted(
		const TArray<FIntVector>& GridPositions,
		bool bPainted
	);

	bool RotateBlock(
		const FIntVector& GridPosition,
		int32 QuarterTurnDelta = 1
	);

	/**
	 * Changes several existing blocks as one efficient visual batch. Tile type
	 * and absolute quarter-turn rotation are updated together, then each
	 * affected HISM chunk is rebuilt only once.
	 */
	bool SetBlocksVisual(
		const TArray<FIntVector>& GridPositions,
		const TArray<int32>& TileTypes,
		uint8 QuarterTurns
	);

	int32 GetBlockTileType(
		const FIntVector& GridPosition
	) const;

	uint8 GetBlockRotation(
		const FIntVector& GridPosition
	) const;

	int32 GetTileTypeCount() const;

	FText GetTileDisplayName(
		int32 TileType
	) const;

	UStaticMesh* GetTileMesh(
		int32 TileType
	) const;

	UMaterialInterface* GetTileMaterialOverride(
		int32 TileType
	) const;

	/** True for blocks compatible with generated continuous/path surfaces. */
	bool IsContinuousSurfaceBlock(
		const FIntVector& GridPosition
	) const;

	/** True for exposed flat cells accepted by the bake-time detail pass. */
	bool IsTerrainDetailSurfaceBlock(
		const FIntVector& GridPosition
	) const;

	FTransform GetBlockLocalTransform(
		const FIntVector& GridPosition
	) const;

	void ClearBlocks();

	void CreateTestGrid(
		int32 GridWidth = 10,
		int32 GridHeight = 10
	);

	void RebuildAllChunks();

	/** Re-evaluates only this cell and its immediate elevation neighbors. */
	void RefreshAutoTilesAround(
		const FIntVector& GridPosition
	);

	FVector GridToLocal(
		const FIntVector& GridPosition
	) const;

	FVector GridToWorld(
		const FIntVector& GridPosition
	) const;

	FIntVector WorldToGrid(
		const FVector& WorldPosition
	) const;

	bool GetGridPositionFromHit(
		const FHitResult& HitResult,
		FIntVector& OutGridPosition
	) const;

	bool GetAdjacentGridPositionFromHit(
		const FHitResult& HitResult,
		FIntVector& OutGridPosition
	) const;

protected:
	virtual void OnConstruction(
		const FTransform& Transform
	) override;

#if WITH_EDITOR
	virtual void PostEditUndo() override;

	virtual void PostEditChangeProperty(
		FPropertyChangedEvent& PropertyChangedEvent
	) override;
#endif

private:
	/** Persistent metadata aligned with OccupiedBlocks. */
	UPROPERTY()
	TArray<int32> BlockTileTypes;

	/** Yaw rotation stored as 0, 1, 2, or 3 quarter turns. */
	UPROPERTY()
	TArray<uint8> BlockRotations;

	TArray<UHierarchicalInstancedStaticMeshComponent*>
		ChunkComponents;

	TArray<UProceduralMeshComponent*>
		ContinuousChunkComponents;

	TMap<
		FTileMapChunkMeshKey,
		UHierarchicalInstancedStaticMeshComponent*
	> ChunkComponentLookup;

	TMap<FIntVector, UProceduralMeshComponent*>
		ContinuousChunkComponentLookup;

	TSet<FIntVector> OccupancyLookup;

	TSet<FIntVector> PaintedPathLookup;

	TMap<FIntVector, int32> BlockIndexLookup;

	TMap<
		FIntVector,
		TArray<FIntVector>
	> ChunkBlocksLookup;

	FIntVector GetChunkCoordinate(
		const FIntVector& GridPosition
	) const;

	int32 FindBlockIndex(
		const FIntVector& GridPosition
	) const;

	int32 ClampTileType(
		int32 TileType
	) const;

	const FTileMapTileDefinition* GetTileDefinition(
		int32 TileType
	) const;

	int32 FindAutoTileVariant(
		FName AutoTileSet,
		ETileMapAutoShape AutoShape
	) const;

	ETileMapAutoShape DetermineAutoShape(
		const FIntVector& GridPosition,
		uint8& OutQuarterTurns
	) const;

	bool ResolveAutoTileAt(
		const FIntVector& GridPosition,
		TSet<FIntVector>& OutChangedPositions
	);

	void ResolveAllAutoTiles();

	void RebuildChunksForPositions(
		const TSet<FIntVector>& GridPositions
	);

	UHierarchicalInstancedStaticMeshComponent*
		FindOrCreateChunkComponent(
			const FIntVector& ChunkCoordinate,
			int32 TileType
		);

	UProceduralMeshComponent*
		FindOrCreateContinuousChunkComponent(
			const FIntVector& ChunkCoordinate,
			bool bPathOverlayOnly
		);

	void BuildContinuousChunk(
		const FIntVector& ChunkCoordinate,
		const TArray<FIntVector>& ChunkBlocks,
		bool bPathOverlayOnly = false
	);

	bool UsesLegacyMeshInContinuousMode(
		int32 TileType
	) const;

	bool GetContinuousDiagonalFraction(
		int32 TileType,
		float& OutFraction
	) const;

	bool IsContinuousDiagonalTileType(
		int32 TileType
	) const;

	bool GetContinuousRampMetadata(
		int32 TileType,
		int32& OutSegmentCount,
		int32& OutSegmentIndex
	) const;

	bool IsContinuousRampBlock(
		const FIntVector& GridPosition
	) const;

	bool GetContinuousStairMetadata(
		int32 TileType,
		int32& OutSegmentCount,
		int32& OutSegmentIndex
	) const;

	bool IsContinuousStairBlock(
		const FIntVector& GridPosition
	) const;

	bool GetContinuousCellEdgeCoverage(
		const FIntVector& GridPosition,
		const FIntVector& EdgeDirection,
		float& OutCoverageStart,
		float& OutCoverageEnd
	) const;

	bool GetContinuousTerrainCoverageAcrossEdge(
		const FIntVector& GridPosition,
		const FIntVector& EdgeDirection,
		float& OutCoverageStart,
		float& OutCoverageEnd
	) const;

	float GetContinuousTopSurfaceZ(
		float LocalX,
		float LocalY,
		int32 TopGridZ,
		FVector& OutNormal
	) const;

	bool IsVisiblePaintedPathBlock(
		const FIntVector& GridPosition
	) const;

	float GetContinuousPathMask(
		float LocalX,
		float LocalY,
		int32 GridZ,
		float BlendWidth
	) const;

	void RebuildChunk(
		const FIntVector& ChunkCoordinate
	);

	void NormalizeBlockMetadata();

	void RebuildDerivedLookups();

	void DestroyChunkComponents(
		const FIntVector& ChunkCoordinate
	);

	void DestroyAllChunkComponents();

	static int32 FloorDivide(
		int32 Value,
		int32 Divisor
	);
};
