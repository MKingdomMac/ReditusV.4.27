#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "ReditusStaggerFeedbackComponent.generated.h"

class UParticleSystem;
class USoundBase;
class USceneComponent;
class UCameraShakeBase;

DECLARE_DYNAMIC_MULTICAST_DELEGATE(
	FReditusStaggerFeedbackPlayedSignature
);

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(
	FReditusStaggerTextRequestedSignature,
	FText, Text
);

/**
 * Presentation-only stagger feedback.
 *
 * This is deliberately separate from damage/stamina logic.
 * The gameplay system decides WHEN an enemy becomes staggered.
 * This component decides HOW that moment looks/sounds/feels.
 *
 * Put this on BP_EnemyUnitBase and call PlayStaggerFeedback exactly once
 * when Staggered changes from false -> true.
 */
UCLASS(ClassGroup=(Reditus), meta=(BlueprintSpawnableComponent))
class CHAPTER1REDITUS_API UReditusStaggerFeedbackComponent
	: public UActorComponent
{
	GENERATED_BODY()

public:

	UReditusStaggerFeedbackComponent();

	/**
	 * Assign your CURRENT stagger particle here first.
	 * This preserves the effect you already have.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stagger|VFX")
	UParticleSystem* PrimaryStaggerParticle = nullptr;

	/**
	 * Optional second "break burst" particle on top of the old effect.
	 * Good for shards/radial burst/ring.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stagger|VFX")
	UParticleSystem* BreakBurstParticle = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stagger|VFX")
	FName AttachSocketName = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stagger|VFX")
	FVector ParticleScale = FVector(1.0f);

	/**
	 * Optional break/stagger sound.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stagger|Audio")
	USoundBase* StaggerSound = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stagger|Audio",
		meta = (ClampMin = "0.0"))
	float SoundVolumeMultiplier = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stagger|Audio",
		meta = (ClampMin = "0.01"))
	float SoundPitchMultiplier = 1.0f;

	/**
	 * Optional camera shake.
	 * Create/choose a CameraShake Blueprint in UE4.27 and assign it here.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stagger|Camera")
	TSubclassOf<UCameraShakeBase> StaggerCameraShake;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stagger|Camera",
		meta = (ClampMin = "0.0"))
	float CameraShakeScale = 1.0f;

	// ============================================================
	// STAGGERED TEXT
	// ============================================================

	/**
	 * Text requested when the stagger happens.
	 * BP_EnemyUnitBase can bind to OnStaggerTextRequested and animate
	 * whatever UMG widget/style you want.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stagger|Text")
	FText StaggerText;

	UPROPERTY(BlueprintAssignable, Category = "Stagger|Text")
	FReditusStaggerTextRequestedSignature OnStaggerTextRequested;

	/**
	 * Broadcast after the built-in particle/sound/shake has fired.
	 */
	UPROPERTY(BlueprintAssignable, Category = "Stagger")
	FReditusStaggerFeedbackPlayedSignature OnStaggerFeedbackPlayed;

	/**
	 * Play the configured stagger presentation.
	 *
	 * AttachComponent should normally be the enemy's sprite/root/impact component.
	 * If it is null, particles spawn at the owning actor's world transform.
	 */
	UFUNCTION(BlueprintCallable, Category = "Reditus|Combat|Stagger")
	void PlayStaggerFeedback(
		USceneComponent* AttachComponent
	);

	/**
	 * Blueprint extension point.
	 *
	 * Use this for anything we do NOT want hard-coded in C++:
	 * - Niagara shatter effect
	 * - white flash
	 * - "BREAK!" UI burst
	 * - brief camera zoom
	 * - material flash
	 * - screen-space distortion
	 * - custom per-boss presentation
	 */
	UFUNCTION(BlueprintImplementableEvent,
		Category = "Reditus|Combat|Stagger",
		DisplayName = "Additional Stagger Feedback")
	void BP_AdditionalStaggerFeedback();
};
