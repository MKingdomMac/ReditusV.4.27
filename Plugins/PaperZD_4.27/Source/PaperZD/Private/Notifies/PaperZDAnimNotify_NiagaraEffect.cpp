// Copyright 2017 ~ 2023 Critical Failure Studio Ltd. All rights reserved.

#include "Notifies/PaperZDAnimNotify_NiagaraEffect.h"
#include "NiagaraComponent.h"
#include "NiagaraSystem.h"
#include "NiagaraFunctionLibrary.h"

UPaperZDAnimNotify_NiagaraEffect::UPaperZDAnimNotify_NiagaraEffect()
{
	bAttached = true;
	Scale = FVector(1.f);

#if WITH_EDITORONLY_DATA
	Color = FColor(192, 255, 99, 255);
#endif // WITH_EDITORONLY_DATA
}

void UPaperZDAnimNotify_NiagaraEffect::PostLoad()
{
	Super::PostLoad();

	RotationOffsetQuat = FQuat(RotationOffset);
}

#if WITH_EDITOR
void UPaperZDAnimNotify_NiagaraEffect::PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	if (PropertyChangedEvent.MemberProperty && PropertyChangedEvent.MemberProperty->GetFName() == GET_MEMBER_NAME_CHECKED(UPaperZDAnimNotify_NiagaraEffect, RotationOffset))
	{
		RotationOffsetQuat = FQuat(RotationOffset);
	}
}
#endif

FName UPaperZDAnimNotify_NiagaraEffect::GetDisplayName_Implementation() const
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

void UPaperZDAnimNotify_NiagaraEffect::OnReceiveNotify_Implementation(UPaperZDAnimInstance* OwningInstance /* = nullptr */)
{
	if (PSTemplate && SequenceRenderComponent)
	{
		if (bAttached)
		{
			UNiagaraFunctionLibrary::SpawnSystemAttached(PSTemplate, SequenceRenderComponent, SocketName, LocationOffset, RotationOffset, EAttachLocation::KeepRelativeOffset, true);
		}
		else
		{
			const FTransform Transform = SequenceRenderComponent->GetSocketTransform(SocketName);
			UNiagaraComponent* System = UNiagaraFunctionLibrary::SpawnSystemAtLocation(GetWorld(), PSTemplate, Transform.TransformPosition(LocationOffset), (Transform.GetRotation() * RotationOffsetQuat).Rotator(), Scale, true);
		}
	}
	else if (!PSTemplate)
	{
		UObject* AnimSequencePkg = this->GetContainingAsset();
		UE_LOG(LogTemp, Warning, TEXT("Particle Notify: Particle system is null for particle notify '%s' in anim: '%s'"), *(GetDisplayName().ToString()), *GetPathNameSafe(AnimSequencePkg));
	}
}

