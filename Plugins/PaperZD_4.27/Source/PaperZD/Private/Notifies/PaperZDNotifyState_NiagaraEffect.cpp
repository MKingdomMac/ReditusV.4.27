// Copyright 2017 ~ 2023 Critical Failure Studio Ltd. All rights reserved.

#include "Notifies/PaperZDNotifyState_NiagaraEffect.h"
#include "NiagaraComponent.h"
#include "NiagaraSystem.h"
#include "NiagaraFunctionLibrary.h"

UPaperZDNotifyState_NiagaraEffect::UPaperZDNotifyState_NiagaraEffect()
{
	bDestroyAtEnd = false;
	Scale = FVector(1.f);

#if WITH_EDITORONLY_DATA
	Color = FColor(192, 255, 99, 255);
#endif // WITH_EDITORONLY_DATA
}

void UPaperZDNotifyState_NiagaraEffect::PostLoad()
{
	Super::PostLoad();

	RotationOffsetQuat = FQuat(RotationOffset);
}

#if WITH_EDITOR
void UPaperZDNotifyState_NiagaraEffect::PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	if (PropertyChangedEvent.MemberProperty && PropertyChangedEvent.MemberProperty->GetFName() == GET_MEMBER_NAME_CHECKED(UPaperZDNotifyState_NiagaraEffect, RotationOffset))
	{
		RotationOffsetQuat = FQuat(RotationOffset);
	}
}
#endif

FName UPaperZDNotifyState_NiagaraEffect::GetDisplayName_Implementation() const
{
	if (PSTemplate)
	{
		return FName(*PSTemplate->GetName());
	}
	else
	{
		return Super::GetDisplayName_Implementation();
	}
}

void UPaperZDNotifyState_NiagaraEffect::OnNotifyBegin_Implementation(UPaperZDAnimInstance* OwningInstance /* = nullptr */) 
{
	if (PSTemplate && SequenceRenderComponent)
	{
		UNiagaraComponent* Component = UNiagaraFunctionLibrary::SpawnSystemAttached(
			PSTemplate,
			SequenceRenderComponent,
			SocketName,
			LocationOffset,
			RotationOffset,
			EAttachLocation::KeepRelativeOffset,
			true
		);
		if (Component)
		{
			Component->ComponentTags.AddUnique(GetSpawnedComponentTag());
		}
	}
	else if (!PSTemplate)
	{
		UObject* AnimSequencePkg = GetContainingAsset();
		UE_LOG(LogTemp, Warning, TEXT("Particle Notify: Particle system is null for particle notify '%s' in anim: '%s'"), *(GetDisplayName().ToString()), *GetPathNameSafe(AnimSequencePkg));
	}
}

void UPaperZDNotifyState_NiagaraEffect::OnNotifyEnd_Implementation(UPaperZDAnimInstance* OwningInstance) 
{
	if (PSTemplate && SequenceRenderComponent)
	{
		TArray<USceneComponent*> Children;
		SequenceRenderComponent->GetChildrenComponents(false, Children);
		for (USceneComponent* Component : Children)
		{
			if (UNiagaraComponent* ParticleComponent = Cast<UNiagaraComponent>(Component))
			{
				if (ParticleComponent->ComponentHasTag(GetSpawnedComponentTag()))
				{
					// untag the component
					ParticleComponent->ComponentTags.Remove(GetSpawnedComponentTag());

					// Either destroy the component or deactivate it to have it's active particles finish.
					// The component will auto destroy once all particle are gone.
					if (bDestroyAtEnd)
					{
						ParticleComponent->DestroyComponent();
					}
					else
					{
						ParticleComponent->Deactivate();
					}
				}
			}
		}	
	}
}
