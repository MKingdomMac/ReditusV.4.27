#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "GameFramework/Actor.h"
#include "InputCoreTypes.h"
#include "ReditusFocusShotRules.h"
#include "ReditusFocusShotSession.generated.h"

class ACameraActor;
class APlayerController;
class IInputProcessor;
class SWidget;
class UGameViewportClient;
class ULocalPlayer;
class UReditusBattleHUDWidget;
class USceneComponent;

UENUM(BlueprintType)
enum class EReditusFocusShotResult : uint8
{
    NormalShot,
    WeakSpotHit,
    NoShot
};

USTRUCT(BlueprintType)
struct CHAPTER1REDITUS_API FReditusFocusShotTargetRow : public FTableRowBase
{
    GENERATED_BODY()

    /** Optional socket. The local offset is relative to the socket when it exists. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Focus Shot")
    FName SocketName = NAME_None;

    /** Relative to the visual component if no socket is specified or found. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Focus Shot")
    FVector LocalOffset = FVector::ZeroVector;

    /** In viewport layout units; this is also the radius of the amber ring. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Focus Shot", meta=(ClampMin="6.0", ClampMax="60.0"))
    float HitRadius = 19.f;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FReditusFocusShotResolved, EReditusFocusShotResult, Result, AActor*, ShotTarget);

UCLASS(BlueprintType, Blueprintable)
class CHAPTER1REDITUS_API AReditusFocusShotSession : public AActor
{
    GENERATED_BODY()
public:
    AReditusFocusShotSession();

    UPROPERTY(BlueprintAssignable, Category="Focus Shot")
    FReditusFocusShotResolved OnResolved;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Focus Shot", meta=(ExposeOnSpawn="true"))
    UDataTable* WeakSpotTable = nullptr;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Focus Shot", meta=(ExposeOnSpawn="true", ClampMin="0.5", ClampMax="15.0"))
    float AimSeconds = 4.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Focus Shot", meta=(ExposeOnSpawn="true"))
    bool bUseFocusCamera = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Focus Shot", meta=(ClampMin="100.0"))
    float CameraDistance = 560.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Focus Shot", meta=(ClampMin="15.0", ClampMax="100.0"))
    float CameraFOV = 48.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Focus Shot", meta=(ClampMin="0.0"))
    float ReticleSway = 7.f;

    /** Minimum starting distance from the weak spot, in viewport layout units. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Focus Shot", meta=(ExposeOnSpawn="true", ClampMin="0.0"))
    float ReticleSpawnMinRadius = 64.f;

    /** Maximum starting distance from the weak spot, in viewport layout units. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Focus Shot", meta=(ExposeOnSpawn="true", ClampMin="24.0"))
    float ReticleSpawnMaxRadius = 120.f;

    UFUNCTION(BlueprintPure, Category="Focus Shot")
    static bool GetFocusShotRifleResult(EReditusFocusShotResult Result, float& CriticalChance, bool& bWeakSpotHit);

    UFUNCTION(BlueprintPure, Category="Focus Shot")
    static bool CanHitFocusShotTarget(AActor* BattleController, AActor* Target);

    UFUNCTION(BlueprintPure, Category="Focus Shot")
    static float GetFocusShotRifleFlightTime(FTransform MuzzleTransform, AActor* Target, float ProjectileSpeed);

    /** Bind On Resolved first. Visual Anchor may be left empty to use the enemy's Pixel2D component. */
    UFUNCTION(BlueprintCallable, Category="Focus Shot")
    void StartFocusShot(APlayerController* PlayerController, AActor* BattleController,
        AActor* Target, USceneComponent* VisualAnchor = nullptr);

    UFUNCTION(BlueprintCallable, Category="Focus Shot")
    void CancelFocusShot();

    virtual void Tick(float DeltaSeconds) override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
    friend class SReditusFocusShotOverlay;
    friend class FReditusFocusShotInput;

    bool OwnsInput(uint32 UserIndex, FKey Key) const;
    bool GameWindowAcceptsInput() const;
    void CaptureKeyboardFocus();
    void FireRifle();
    bool AcceptsGameInput() const;
    bool KeyDown(FKey Key, bool bRepeat);
    bool KeyUp(FKey Key);
    bool Analog(FKey Key, float Value);
    void UpdateAim(const FVector2D& Size, float Step);
    void LockShot(ReditusFocusShotRules::EResult Result);
    void Finish();
    void ReleasePresentation();
    bool HasLiveTarget() const;
    bool OwnsBattleAction() const;

    TWeakObjectPtr<APlayerController> Player;
    TWeakObjectPtr<AActor> Battle;
    TWeakObjectPtr<AActor> AimTarget;
    TWeakObjectPtr<AActor> FiringUnit;
    TWeakObjectPtr<AActor> PreviousViewTarget;
    TWeakObjectPtr<ACameraActor> FocusCamera;
    TWeakObjectPtr<USceneComponent> AimAnchor;
    TWeakObjectPtr<UReditusBattleHUDWidget> SuspendedHUD;
    TWeakObjectPtr<UGameViewportClient> ViewportClient;
    TWeakObjectPtr<ULocalPlayer> LocalPlayer;
    TWeakPtr<SWidget> PreviousKeyboardFocus;
    TSharedPtr<SWidget> Overlay;
    TSharedPtr<IInputProcessor> Input;
    ReditusFocusShotRules::FRun Run;
    FReditusFocusShotTargetRow WeakSpot;

    TSet<FKey> HeldKeys;
    FVector2D Stick = FVector2D::ZeroVector;
    FVector2D Aim = FVector2D::ZeroVector;
    FVector2D Reticle = FVector2D::ZeroVector;
    FVector2D WeakSpotScreen = FVector2D::ZeroVector;
    FVector2D TargetScreen = FVector2D::ZeroVector;
    FVector2D LastSize = FVector2D::ZeroVector;
    float Elapsed = 0.f;
    float FeedbackTime = 0.f;
    float ViewportWaitTime = 0.f;
    float TotalAimTime = 4.f;
    double LastTickReal = 0.0;
    bool bTracksFiringUnit = false;
    bool bHadSuspendedHUD = false;
    bool bHUDWasSuspended = false;
    bool bWeakSpotConfigured = false;
    bool bWeakSpotVisible = false;
    bool bAimInitialized = false;
    bool bFireRequested = false;
    bool bCapturedKeyboardFocus = false;
    bool bUsedGamepad = false;
};
