// Fill out your copyright notice in the Description page of Project Settings.




UPaperZDAnimNotify_ParticleEffect::UPaperZDAnimNotify_ParticleEffect()
{
    bAttached = true;
    Scale = FVector(1.f);
#if WITH_EDITORONLY_DATA
    Color = FColor(192, 255, 99, 255);
#endif // WITH_EDITORONLY_DATA
}

void UPaperZDAnimNotify_ParticleEffect::PostLoad()
{
    Super::PostLoad();
    RotationOffsetQuat = FQuat(RotationOffset);
}

#if WITH_EDITOR
void UPaperZDAnimNotify_ParticleEffect::PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent)
{
    Super::PostEditChangeProperty(PropertyChangedEvent);
    if (PropertyChangedEvent.MemberProperty && PropertyChangedEvent.MemberProperty->GetFName() == GET_MEMBER_NAME_CHECKED(UPaperZDAnimNotify_ParticleEffect, RotationOffset))
    {
        RotationOffsetQuat = FQuat(RotationOffset);
    }
}
#endif

FName UPaperZDAnimNotify_ParticleEffect::GetDisplayName_Implementation() const
{
    // Swap PSTemplate reference to NiagaraSystem
    if (NiagaraTemplate)
    {
        return FName(*NiagaraTemplate->GetName());
    }
    else
    {
        return Super::GetDisplayName_Implementation();
    }
}

void UPaperZDAnimNotify_ParticleEffect::OnReceiveNotify_Implementation(UPaperZDAnimInstance* OwningInstance /* = nullptr */)
{
    if (NiagaraTemplate && SequenceRenderComponent)
    {
        // Niagara systems don't have IsLooping() — check via UNiagaraSystem properties
        // If you need looping suppression, add a bIsLooping UPROPERTY flag manually
        // and check it here instead

        if (bAttached)
        {
            UNiagaraFunctionLibrary::SpawnSystemAttached(
                NiagaraTemplate,
                SequenceRenderComponent,
                SocketName,
                LocationOffset,
                RotationOffset,
                EAttachLocation::KeepRelativeOffset,
                true  // bAutoDestroy
            );
        }
        else
        {
            const FTransform Transform = SequenceRenderComponent->GetSocketTransform(SocketName);
            FTransform SpawnTransform;
            SpawnTransform.SetLocation(Transform.TransformPosition(LocationOffset));
            SpawnTransform.SetRotation(Transform.GetRotation() * RotationOffsetQuat);
            SpawnTransform.SetScale3D(Scale);

            UNiagaraFunctionLibrary::SpawnSystemAtLocation(
                GetWorld(),
                NiagaraTemplate,
                SpawnTransform.GetLocation(),
                SpawnTransform.GetRotation().Rotator(),
                SpawnTransform.GetScale3D(),
                true,  // bAutoDestroy
                true,  // bAutoActivate
                ENCPoolMethod::None
            );
        }
    }
    else
    {
        UObject* AnimSequencePkg = GetContainingAsset();
        UE_LOG(LogTemp, Warning, TEXT("Niagara Notify: Niagara system is null for notify '%s' in anim: '%s'"),
            *(GetDisplayName().ToString()), *GetPathNameSafe(AnimSequencePkg));
    }
}

