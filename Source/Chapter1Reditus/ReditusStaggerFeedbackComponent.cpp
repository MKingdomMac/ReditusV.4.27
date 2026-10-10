#include "ReditusStaggerFeedbackComponent.h"

#include "Camera/CameraShakeBase.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/SceneComponent.h"
#include "GameFramework/Actor.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Particles/ParticleSystem.h"
#include "Sound/SoundBase.h"

UReditusStaggerFeedbackComponent::UReditusStaggerFeedbackComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	StaggerText = FText::FromString(TEXT("STAGGERED"));
}

void UReditusStaggerFeedbackComponent::PlayStaggerFeedback(
	USceneComponent* AttachComponent)
{
	AActor* OwnerActor = GetOwner();

	if (!OwnerActor)
	{
		return;
	}

	auto SpawnParticle =
		[this, OwnerActor, AttachComponent](UParticleSystem* Particle)
	{
		if (!Particle)
		{
			return;
		}

		if (AttachComponent)
		{
			UGameplayStatics::SpawnEmitterAttached(
				Particle,
				AttachComponent,
				AttachSocketName,
				FVector::ZeroVector,
				FRotator::ZeroRotator,
				ParticleScale,
				EAttachLocation::SnapToTarget,
				true
			);
		}
		else
		{
			FTransform SpawnTransform =
				OwnerActor->GetActorTransform();

			SpawnTransform.SetScale3D(ParticleScale);

			UGameplayStatics::SpawnEmitterAtLocation(
				GetWorld(),
				Particle,
				SpawnTransform,
				true
			);
		}
	};

	// Preserve the old stagger effect if assigned.
	SpawnParticle(PrimaryStaggerParticle);

	// Optional new "break" burst layered on top.
	SpawnParticle(BreakBurstParticle);

	if (StaggerSound)
	{
		UGameplayStatics::PlaySoundAtLocation(
			this,
			StaggerSound,
			OwnerActor->GetActorLocation(),
			SoundVolumeMultiplier,
			SoundPitchMultiplier
		);
	}

	if (StaggerCameraShake)
	{
		if (APlayerController* PlayerController =
			UGameplayStatics::GetPlayerController(this, 0))
		{
			if (APlayerCameraManager* CameraManager =
				PlayerController->PlayerCameraManager)
			{
				CameraManager->StartCameraShake(
					StaggerCameraShake,
					CameraShakeScale
				);
			}
		}
	}

	// Tell Blueprint/UI to play the dedicated STAGGERED text animation.
	OnStaggerTextRequested.Broadcast(StaggerText);

	BP_AdditionalStaggerFeedback();
	OnStaggerFeedbackPlayed.Broadcast();
}
