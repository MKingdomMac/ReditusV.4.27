#pragma once
#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "Components/ActorComponent.h"
#include "GameFramework/Actor.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "MatineeCameraShake.h"
#include "ReditusEnemyDeathVisuals.generated.h"

class USceneComponent;
class UPaperSprite;
class UPaperSpriteComponent;
class UPaperFlipbookComponent;
class UMaterialInterface;
class UMaterialInstanceDynamic;
class APlayerCameraManager;

UENUM(BlueprintType)
enum class EReditusEnemyDeathStyle : uint8
{
    LegacyAnimation UMETA(DisplayName="Existing Death Animation"),
    CrimsonDissolve UMETA(DisplayName="Crimson Dissolve"),
    BossDissolve UMETA(DisplayName="Boss Crimson Dissolve"),
    BossCameraShake UMETA(DisplayName="Boss Camera Shake Only")
};

/** Normal appearance and normal/boss timing inherited from the base component. */
USTRUCT(BlueprintType)
struct REDITUSENEMYDEATH_API FReditusEnemyDeathSettings
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Death")
    EReditusEnemyDeathStyle Style = EReditusEnemyDeathStyle::LegacyAnimation;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Death", meta=(ClampMin="0.25", ClampMax="10.0", Units="s"))
    float DissolveSeconds = 1.25f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Death", meta=(ClampMin="0.5", ClampMax="20.0", Units="s"))
    float BossDissolveSeconds = 4.5f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Death")
    FLinearColor CrimsonTint = FLinearColor(.48f, .006f, .018f, 1.f);
    /** Texture-space facing, before the sprite component's rotation/mirroring. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Death", meta=(DisplayName="Front Is Right Side Of Sprite"))
    bool bFrontOnRight = true;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Death", meta=(ClampMin="0.0", ClampMax="4.0"))
    float FlashStrength = 1.4f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Death", meta=(ClampMin="2", ClampMax="6"))
    int32 FragmentColumns = 4;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Death", meta=(ClampMin="2", ClampMax="6"))
    int32 FragmentRows = 5;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Death", meta=(ClampMin="0.2", ClampMax="3.0", Units="s"))
    float FragmentLifeSeconds = .75f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Death", meta=(ClampMin="0.0", ClampMax="300.0", Units="cm"))
    float DriftDistance = 50.f;
    /** Serialized compatibility; Is Boss now enables camera shake automatically. */
    UPROPERTY()
    bool bBossCameraShake = true;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Death", meta=(ClampMin="0.0", ClampMax="2.0"))
    float BossShakeStrength = 1.f;
};

/** One per-enemy decision. Is Boss enables both the boss dissolve and its shake. */
USTRUCT(BlueprintType)
struct REDITUSENEMYDEATH_API FReditusEnemyBossDeathOptions
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Boss Death", meta=(DisplayName="Is Boss"))
    bool bIsBoss = false;
    /** Old assets retain their serialized fields; these switches no longer affect behaviour. */
    UPROPERTY()
    bool bDissolve = true;
    UPROPERTY()
    bool bCameraShake = true;
};

/** Inherit once from BP_EnemyUnitBase. Uses the existing visual, not a new animation system. */
UCLASS(ClassGroup=(Reditus), meta=(BlueprintSpawnableComponent))
class REDITUSENEMYDEATH_API UReditusEnemyDeathComponent : public UActorComponent
{
    GENERATED_BODY()
public:
    UReditusEnemyDeathComponent();
    virtual void PostLoad() override;

    /** Shared defaults. Normal table rows do not replace these values. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Enemy Death")
    FReditusEnemyDeathSettings Settings;

    /** Class enemy options; a matching EnemyID row's BossDeath supplies table enemy options. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Enemy Death")
    FReditusEnemyBossDeathOptions BossDeath;

    /** Serialized compatibility only. No longer editable or used to select row settings. */
    UPROPERTY()
    bool bOverrideTableSettings = false;

    /** The existing actor component VARIABLE name. Pixel2D is the reviewed project default. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Enemy Death", AdvancedDisplay)
    FName SpriteComponentProperty = TEXT("Pixel2D");

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Enemy Death", AdvancedDisplay)
    UDataTable* EnemyTable = nullptr;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Enemy Death", AdvancedDisplay)
    UMaterialInterface* DissolveMaterial = nullptr;

    /** Optional custom death entry point. Always continue the original death callbacks afterward. */
    UFUNCTION(BlueprintCallable, Category="Reditus|Enemies|Death")
    bool PlayDeathVisual();

    /** Effective style, including row boss options. Read before the dying enemy is destroyed. */
    UFUNCTION(BlueprintPure, Category="Reditus|Enemies|Death", meta=(DisplayName="Get Death Style"))
    EReditusEnemyDeathStyle GetDeathStyle() const;
};

UCLASS()
class REDITUSENEMYDEATH_API UReditusCrimsonDeathShake : public UMatineeCameraShake
{
    GENERATED_BODY()
public:
    UReditusCrimsonDeathShake(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());
};

/** A visual snapshot, never a combat participant. Not attached/owned by the dying unit. */
UCLASS(NotBlueprintable, Transient)
class REDITUSENEMYDEATH_API AReditusEnemyDeathVisual : public AActor
{
    GENERATED_BODY()
public:
    AReditusEnemyDeathVisual();
    bool Initialize(AActor* Enemy, UPaperFlipbookComponent* Source, UPaperSprite* Frame,
                    UMaterialInterface* Material, const FReditusEnemyDeathSettings& InSettings);
    bool InitializeShakeOnly(AActor* Enemy, const FReditusEnemyDeathSettings& InSettings);
    void ClaimSource();
    virtual void Tick(float DeltaSeconds) override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
private:
    UPROPERTY() USceneComponent* VisualRoot = nullptr;
    UPROPERTY() UPaperSpriteComponent* Body = nullptr;
    UPROPERTY() UMaterialInstanceDynamic* BodyMaterial = nullptr;
    UPROPERTY() TArray<UPaperSpriteComponent*> Fragments;
    UPROPERTY() TArray<UMaterialInstanceDynamic*> FragmentMaterials;
    TWeakObjectPtr<AActor> SourceEnemy;
    TWeakObjectPtr<UPaperFlipbookComponent> OriginalVisual;
    TWeakObjectPtr<AActor> BattleController;
    TWeakObjectPtr<UObject> Encounter;
    TWeakObjectPtr<APlayerCameraManager> ShakeManager;
    TWeakObjectPtr<UCameraShakeBase> Shake;
    FReditusEnemyDeathSettings Settings;
    FTransform OriginalTransform;
    FVector AxisX = FVector::ForwardVector;
    FVector AxisZ = FVector::UpVector;
    FVector DepthOffset = FVector::ZeroVector;
    FVector2D SpritePixels = FVector2D(128.f, 128.f);
    float Age = 0.f, Duration = 1.25f;
    int32 Seed = 0;
    bool bClaimed = false, bHadController = false, bShakeStopping = false, bShakeOnly = false;
    bool bWasVisible = true, bWasTicking = true, bWasPlaying = false;
    bool SameEncounter() const;
    void RestoreSource();
    void StopOwnedShake(bool bImmediate);
};

UCLASS()
class REDITUSENEMYDEATH_API UReditusEnemyDeathLibrary : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()
public:
    /** Call from the confirmed death path before hiding/destroying the sprite. Always continue existing death callbacks. */
    UFUNCTION(BlueprintCallable, Category="Reditus|Enemies|Death", meta=(DefaultToSelf="Enemy"))
    static bool TryStartEnemyDeathVisual(AActor* Enemy, UDataTable* EnemyTable, UMaterialInterface* DissolveMaterial);
};
