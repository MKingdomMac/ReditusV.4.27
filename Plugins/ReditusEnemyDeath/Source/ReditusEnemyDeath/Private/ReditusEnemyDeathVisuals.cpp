#include "ReditusEnemyDeathVisuals.h"
#include "ReditusEnemyDeathRules.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/SceneComponent.h"
#include "Engine/World.h"
#include "Engine/Texture2D.h"
#include "GameFramework/PlayerController.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Modules/ModuleManager.h"
#include "PaperFlipbook.h"
#include "PaperFlipbookComponent.h"
#include "PaperSprite.h"
#include "PaperSpriteComponent.h"
#include "UObject/UnrealType.h"

DEFINE_LOG_CATEGORY_STATIC(LogReditusEnemyDeath, Log, All);
namespace ReditusEnemyDeathRuntime
{
    const FName StartedTag(TEXT("Reditus.EnemyDeath.Started"));
    UObject* Object(const UObject* Owner, const TCHAR* Name)
    {
        const auto* P = IsValid(Owner) ? FindFProperty<FObjectPropertyBase>(Owner->GetClass(), Name) : nullptr;
        return P ? P->GetObjectPropertyValue_InContainer(Owner) : nullptr;
    }
    bool Boolean(const UObject* Owner, const TCHAR* Name)
    {
        const auto* P = IsValid(Owner) ? FindFProperty<FBoolProperty>(Owner->GetClass(), Name) : nullptr;
        return P && P->GetPropertyValue_InContainer(Owner);
    }
    int32 Integer(const UObject* Owner, const TCHAR* Name)
    {
        const auto* P = IsValid(Owner) ? FindFProperty<FIntProperty>(Owner->GetClass(), Name) : nullptr;
        return P ? P->GetPropertyValue_InContainer(Owner) : 0;
    }
    bool IsEnemy(const AActor* Enemy)
    {
        for (auto* C = IsValid(Enemy) ? Enemy->GetClass() : nullptr; C; C = C->GetSuperClass())
            if (C->GetPathName() == TEXT("/Game/TurnBasedJRPGTemplate/Blueprints/Units/EnemyUnits/BP_EnemyUnitBase.BP_EnemyUnitBase_C")) return true;
        return false;
    }
    bool ResolveSettings(AActor* Enemy, UDataTable* Table, const UReditusEnemyDeathComponent* Component,
                         FReditusEnemyDeathSettings& Out)
    {
        Out = Component ? Component->Settings : FReditusEnemyDeathSettings();
        FReditusEnemyBossDeathOptions Boss = Component ? Component->BossDeath : FReditusEnemyBossDeathOptions();
        bool bFound = Component != nullptr;
        bool bLegacyBoss = false;
        const auto* ID = IsValid(Enemy) ? FindFProperty<FNameProperty>(Enemy->GetClass(), TEXT("EnemyID")) : nullptr;
        const auto* RowType = IsValid(Table) ? Table->GetRowStruct() : nullptr;
        const FName RowID = ID ? ID->GetPropertyValue_InContainer(Enemy) : NAME_None;
        const uint8* Row = RowType && !RowID.IsNone() ? Table->GetRowMap().FindRef(RowID) : nullptr;
        const auto* BossField = RowType ? FindFProperty<FStructProperty>(RowType, TEXT("BossDeath")) : nullptr;
        if (Row && BossField && BossField->Struct == FReditusEnemyBossDeathOptions::StaticStruct())
        {
            Boss = *BossField->ContainerPtrToValuePtr<FReditusEnemyBossDeathOptions>(Row);
            bFound = true;
        }
        else if (Row && !Component)
        {
            // Compatibility for old table-only library calls. Installed components
            // always keep their shared appearance/timing, even for an old animated row.
            const auto* Legacy = FindFProperty<FStructProperty>(RowType, TEXT("DeathVisual"));
            if (Legacy && Legacy->Struct == FReditusEnemyDeathSettings::StaticStruct())
            {
                Out = *Legacy->ContainerPtrToValuePtr<FReditusEnemyDeathSettings>(Row);
                bLegacyBoss = Out.Style == EReditusEnemyDeathStyle::BossDissolve || Out.Style == EReditusEnemyDeathStyle::BossCameraShake;
                bFound = true;
            }
        }
        if (Boss.bIsBoss || bLegacyBoss)
        {
            Out.Style = EReditusEnemyDeathStyle::BossDissolve;
            Out.bBossCameraShake = true;
            if (!FMath::IsFinite(Out.BossShakeStrength) || Out.BossShakeStrength <= 0.f) Out.BossShakeStrength = 1.f;
        }
        else
        {
            Out.bBossCameraShake = false;
            if (Out.Style == EReditusEnemyDeathStyle::BossDissolve || Out.Style == EReditusEnemyDeathStyle::BossCameraShake)
                Out.Style = EReditusEnemyDeathStyle::CrimsonDissolve;
        }
        return bFound;
    }

    FReditusEnemyDeathSettings Sanitize(FReditusEnemyDeathSettings S)
    {
        using ReditusEnemyDeathRules::Clamp;
        S.DissolveSeconds = Clamp(S.DissolveSeconds, .25f, 10.f, 1.25f);
        S.BossDissolveSeconds = Clamp(S.BossDissolveSeconds, .5f, 20.f, 4.5f);
        S.FragmentLifeSeconds = Clamp(S.FragmentLifeSeconds, .2f, 3.f, .75f);
        S.DriftDistance = Clamp(S.DriftDistance, 0.f, 300.f, 50.f);
        S.FlashStrength = Clamp(S.FlashStrength, 0.f, 4.f, 1.4f);
        S.BossShakeStrength = Clamp(S.BossShakeStrength, 0.f, 2.f, 1.f);
        S.FragmentColumns = FMath::Clamp(S.FragmentColumns, 2, 6);
        S.FragmentRows = FMath::Clamp(S.FragmentRows, 2, 6);
        S.CrimsonTint.R = Clamp(S.CrimsonTint.R, 0.f, 4.f, .48f);
        S.CrimsonTint.G = Clamp(S.CrimsonTint.G, 0.f, 4.f, .006f);
        S.CrimsonTint.B = Clamp(S.CrimsonTint.B, 0.f, 4.f, .018f);
        S.CrimsonTint.A = 1.f;
        return S;
    }
}

UReditusEnemyDeathComponent::UReditusEnemyDeathComponent()
{
    PrimaryComponentTick.bCanEverTick = false;
    bEditableWhenInherited = true;
}

void UReditusEnemyDeathComponent::PostLoad()
{
    Super::PostLoad();
    // Preserve class presets made before boss decisions were separated from defaults.
    if (Settings.Style == EReditusEnemyDeathStyle::BossDissolve || Settings.Style == EReditusEnemyDeathStyle::BossCameraShake)
    {
        BossDeath.bIsBoss = true;
        BossDeath.bDissolve = Settings.Style == EReditusEnemyDeathStyle::BossDissolve;
        BossDeath.bCameraShake = Settings.bBossCameraShake;
        Settings.Style = EReditusEnemyDeathStyle::CrimsonDissolve;
    }
}

EReditusEnemyDeathStyle UReditusEnemyDeathComponent::GetDeathStyle() const
{
    FReditusEnemyDeathSettings Effective;
    return ReditusEnemyDeathRuntime::ResolveSettings(GetOwner(), EnemyTable, this, Effective)
        ? Effective.Style : EReditusEnemyDeathStyle::LegacyAnimation;
}

bool UReditusEnemyDeathComponent::PlayDeathVisual()
{
    return UReditusEnemyDeathLibrary::TryStartEnemyDeathVisual(GetOwner(), EnemyTable, DissolveMaterial);
}

UReditusCrimsonDeathShake::UReditusCrimsonDeathShake(const FObjectInitializer& ObjectInitializer)
    : Super(ObjectInitializer)
{
    bSingleInstance = false;
    OscillationDuration = -1.f; // The owning visual stops just this instance.
    OscillationBlendInTime = .15f;
    OscillationBlendOutTime = .25f;
    RotOscillation.Pitch.Amplitude = 1.2f; RotOscillation.Pitch.Frequency = 18.f;
    RotOscillation.Yaw.Amplitude = .7f; RotOscillation.Yaw.Frequency = 15.f;
    LocOscillation.Y.Amplitude = 3.f; LocOscillation.Y.Frequency = 15.f;
    LocOscillation.Z.Amplitude = 4.f; LocOscillation.Z.Frequency = 21.f;
}

AReditusEnemyDeathVisual::AReditusEnemyDeathVisual()
{
    PrimaryActorTick.bCanEverTick = true;
    SetActorEnableCollision(false);
    VisualRoot = CreateDefaultSubobject<USceneComponent>(TEXT("VisualRoot"));
    SetRootComponent(VisualRoot);
    VisualRoot->SetMobility(EComponentMobility::Movable);
    Body = CreateDefaultSubobject<UPaperSpriteComponent>(TEXT("FrozenBody"));
    Body->SetupAttachment(VisualRoot);
    Body->SetMobility(EComponentMobility::Movable);
    Body->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Body->SetGenerateOverlapEvents(false);
    Body->SetCastShadow(false);
}

bool AReditusEnemyDeathVisual::Initialize(AActor* Enemy, UPaperFlipbookComponent* Source, UPaperSprite* Frame,
                                        UMaterialInterface* Material, const FReditusEnemyDeathSettings& InSettings)
{
    if (!IsValid(Enemy) || !IsValid(Source) || !IsValid(Frame) || !IsValid(Material) ||
        !IsValid(Frame->GetBakedTexture())) return false;
    const TArray<FVector4>& Vertices = Frame->BakedRenderData;
    if (Vertices.Num() == 0) return false;
    FVector2D MinUV(FLT_MAX, FLT_MAX), MaxUV(-FLT_MAX, -FLT_MAX);
    for (const FVector4& V : Vertices)
    {
        MinUV.X = FMath::Min(MinUV.X, V.Z); MinUV.Y = FMath::Min(MinUV.Y, V.W);
        MaxUV.X = FMath::Max(MaxUV.X, V.Z); MaxUV.Y = FMath::Max(MaxUV.Y, V.W);
    }
    if (MaxUV.X <= MinUV.X || MaxUV.Y <= MinUV.Y) return false;
    SpritePixels = FVector2D(FMath::Max(1.f, Frame->GetBakedTexture()->GetSizeX() * (MaxUV.X-MinUV.X)),
                             FMath::Max(1.f, Frame->GetBakedTexture()->GetSizeY() * (MaxUV.Y-MinUV.Y)));
    Settings = ReditusEnemyDeathRuntime::Sanitize(InSettings);
    Duration = Settings.Style == EReditusEnemyDeathStyle::BossDissolve ? Settings.BossDissolveSeconds : Settings.DissolveSeconds;
    Seed = FMath::RandRange(1, 65535);
    SourceEnemy = Enemy; OriginalVisual = Source;
    BattleController = Cast<AActor>(ReditusEnemyDeathRuntime::Object(Enemy, TEXT("battleController")));
    bHadController = BattleController.IsValid();
    Encounter = ReditusEnemyDeathRuntime::Object(BattleController.Get(), TEXT("currentBattle"));
    OriginalTransform = Source->GetComponentTransform();
    SetActorTransform(OriginalTransform);
    const FVector SourceScale = OriginalTransform.GetScale3D();
    AxisX = OriginalTransform.GetRotation().GetAxisX() * (SourceScale.X < 0.f ? -1.f : 1.f);
    AxisZ = OriginalTransform.GetRotation().GetAxisZ() * (SourceScale.Z < 0.f ? -1.f : 1.f);
    DepthOffset = OriginalTransform.GetRotation().GetAxisY() * .5f;
    if (APlayerController* PC = GetWorld()->GetFirstPlayerController())
        if (PC->PlayerCameraManager && FVector::DotProduct(PC->PlayerCameraManager->GetCameraLocation() - OriginalTransform.GetLocation(), DepthOffset) < 0.f)
            DepthOffset *= -1.f;
    bWasVisible = Source->IsVisible(); bWasTicking = Source->IsComponentTickEnabled(); bWasPlaying = Source->IsPlaying();

    auto MakeMaterial = [&](UPaperSpriteComponent* Component, int32 Tile) -> UMaterialInstanceDynamic*
    {
        Component->SetSprite(Frame);
        Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        Component->SetGenerateOverlapEvents(false);
        Component->SetCastShadow(false);
        Component->TranslucencySortPriority = Source->TranslucencySortPriority + (Tile < 0 ? 1 : 2);
        UMaterialInstanceDynamic* MID = UMaterialInstanceDynamic::Create(Material, this);
        if (!MID) return nullptr;
        for (int32 I = 0; I < FMath::Max(1, Component->GetNumMaterials()); ++I) Component->SetMaterial(I, MID);
        MID->SetTextureParameterValue(TEXT("SpriteTexture"), Frame->GetBakedTexture());
        MID->SetVectorParameterValue(TEXT("UVBounds"), FLinearColor(MinUV.X, MinUV.Y, MaxUV.X-MinUV.X, MaxUV.Y-MinUV.Y));
        MID->SetVectorParameterValue(TEXT("CrimsonTint"), Settings.CrimsonTint);
        MID->SetVectorParameterValue(TEXT("SourceTint"), Source->GetSpriteColor());
        MID->SetVectorParameterValue(TEXT("PixelGrid"), FLinearColor(SpritePixels.X, SpritePixels.Y, 0.f, 0.f));
        MID->SetScalarParameterValue(TEXT("FrontOnRight"), Settings.bFrontOnRight ? 1.f : 0.f);
        MID->SetScalarParameterValue(TEXT("Columns"), Settings.FragmentColumns);
        MID->SetScalarParameterValue(TEXT("Rows"), Settings.FragmentRows);
        MID->SetScalarParameterValue(TEXT("Seed"), Seed);
        MID->SetScalarParameterValue(TEXT("TileIndex"), Tile);
        MID->SetScalarParameterValue(TEXT("Progress"), 0.f);
        MID->SetScalarParameterValue(TEXT("Flash"), Settings.FlashStrength);
        MID->SetScalarParameterValue(TEXT("Opacity"), Tile < 0 ? 1.f : 0.f);
        return MID;
    };
    BodyMaterial = MakeMaterial(Body, INDEX_NONE);
    if (!BodyMaterial) return false;
    for (int32 I = 0; I < Settings.FragmentColumns * Settings.FragmentRows; ++I)
    {
        auto* Fragment = NewObject<UPaperSpriteComponent>(this);
        Fragment->SetupAttachment(VisualRoot);
        Fragment->SetMobility(EComponentMobility::Movable);
        auto* MID = MakeMaterial(Fragment, I);
        if (!MID) return false;
        Fragments.Add(Fragment); FragmentMaterials.Add(MID);
        AddInstanceComponent(Fragment);
        Fragment->RegisterComponent();
        Fragment->SetVisibility(false);
    }
    SetLifeSpan(Duration + Settings.FragmentLifeSeconds + .25f);
    return true;
}

bool AReditusEnemyDeathVisual::InitializeShakeOnly(AActor* Enemy, const FReditusEnemyDeathSettings& InSettings)
{
    if (!IsValid(Enemy) || !GetWorld()) return false;
    bShakeOnly = true;
    Settings = ReditusEnemyDeathRuntime::Sanitize(InSettings);
    Duration = Settings.BossDissolveSeconds;
    SourceEnemy = Enemy;
    BattleController = Cast<AActor>(ReditusEnemyDeathRuntime::Object(Enemy, TEXT("battleController")));
    bHadController = BattleController.IsValid();
    Encounter = ReditusEnemyDeathRuntime::Object(BattleController.Get(), TEXT("currentBattle"));
    Body->SetVisibility(false);
    SetLifeSpan(Duration + .5f);
    return true;
}

void AReditusEnemyDeathVisual::ClaimSource()
{
    if (bClaimed || !SourceEnemy.IsValid() || (!bShakeOnly && !OriginalVisual.IsValid())) return;
    bClaimed = true;
    SourceEnemy->Tags.AddUnique(ReditusEnemyDeathRuntime::StartedTag);
    if (!bShakeOnly)
    {
        OriginalVisual->Stop();
        OriginalVisual->SetComponentTickEnabled(false);
        OriginalVisual->SetVisibility(false, false);
    }
    if ((Settings.Style == EReditusEnemyDeathStyle::BossDissolve || bShakeOnly) && Settings.bBossCameraShake && Settings.BossShakeStrength > 0.f)
        if (APlayerController* PC = GetWorld()->GetFirstPlayerController())
            if (PC->PlayerCameraManager)
            {
                ShakeManager = PC->PlayerCameraManager;
                Shake = PC->PlayerCameraManager->StartCameraShake(UReditusCrimsonDeathShake::StaticClass(), Settings.BossShakeStrength);
            }
}

bool AReditusEnemyDeathVisual::SameEncounter() const
{
    return !bHadController || (BattleController.IsValid() &&
        ReditusEnemyDeathRuntime::Object(BattleController.Get(), TEXT("currentBattle")) == Encounter.Get());
}

void AReditusEnemyDeathVisual::RestoreSource()
{
    if (!OriginalVisual.IsValid()) return;
    OriginalVisual->SetVisibility(bWasVisible, false);
    OriginalVisual->SetComponentTickEnabled(bWasTicking);
    if (bWasPlaying) OriginalVisual->Play(); else OriginalVisual->Stop();
}

void AReditusEnemyDeathVisual::StopOwnedShake(bool bImmediate)
{
    if (ShakeManager.IsValid() && Shake.IsValid()) ShakeManager->StopCameraShake(Shake.Get(), bImmediate);
    if (bImmediate) { Shake.Reset(); ShakeManager.Reset(); }
}

void AReditusEnemyDeathVisual::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    if (!bClaimed) return;
    if (!SameEncounter()) { Destroy(); return; }
    if (SourceEnemy.IsValid() && ReditusEnemyDeathRuntime::Integer(SourceEnemy.Get(), TEXT("currentHP")) > 0 &&
        !ReditusEnemyDeathRuntime::Boolean(SourceEnemy.Get(), TEXT("IsDead?")))
    {
        Destroy(); return; // EndPlay restores a revived unit; never destroys it.
    }
    Age += FMath::IsFinite(DeltaSeconds) ? FMath::Max(0.f, DeltaSeconds) : 0.f;
    if (bShakeOnly)
    {
        if (Age >= Duration && !bShakeStopping) { bShakeStopping = true; StopOwnedShake(false); }
        if (Age >= Duration + .25f) Destroy();
        return; // Existing animation/destruction owns the source; no sprite capture.
    }
    const float Progress = FMath::Clamp(Age / Duration, 0.f, 1.f);
    BodyMaterial->SetScalarParameterValue(TEXT("Progress"), Progress);
    BodyMaterial->SetScalarParameterValue(TEXT("Flash"), ReditusEnemyDeathRules::Flash(Age) * Settings.FlashStrength);
    if (Age >= Duration && !bShakeStopping) { bShakeStopping = true; StopOwnedShake(false); }
    for (int32 I = 0; I < Fragments.Num(); ++I)
    {
        const float Birth = ReditusEnemyDeathRules::ParticleBirth(Age, I, Fragments.Num(), Duration, Settings.FragmentLifeSeconds);
        const float ParticleAge = Birth >= 0.f ? Age - Birth : -1.f;
        const float Alpha = ReditusEnemyDeathRules::FragmentAlpha(ParticleAge, Settings.FragmentLifeSeconds);
        Fragments[I]->SetVisibility(Alpha > 0.f);
        if (Alpha <= 0.f) continue;
        const float T = ParticleAge / Settings.FragmentLifeSeconds;
        const int32 Cycle = static_cast<int32>(Birth / Settings.FragmentLifeSeconds);
        const float Side = ReditusEnemyDeathRules::Noise(I+17, Cycle+23, Seed) * 2.f - 1.f;
        const float U = ReditusEnemyDeathRules::FrontPosition(Birth / Duration, Settings.bFrontOnRight);
        const float V = .02f + ReditusEnemyDeathRules::Noise(I+71, Cycle+43, Seed) * .96f;
        FragmentMaterials[I]->SetVectorParameterValue(TEXT("ParticlePatch"), FLinearColor(U, V,
            FMath::Clamp(2.5f / SpritePixels.X, .008f, .06f), FMath::Clamp(5.f / SpritePixels.Y, .01f, .09f)));
        const float Outward = Settings.bFrontOnRight ? 1.f : -1.f;
        const FVector Drift = AxisX * ((Outward*.45f + Side*.2f) * T * Settings.DriftDistance) +
            AxisZ * ((T + T*T*.35f) * Settings.DriftDistance);
        Fragments[I]->SetWorldLocation(OriginalTransform.GetLocation() + Drift + DepthOffset);
        Fragments[I]->SetWorldScale3D(OriginalTransform.GetScale3D());
        FragmentMaterials[I]->SetScalarParameterValue(TEXT("Opacity"), Alpha);
        FragmentMaterials[I]->SetScalarParameterValue(TEXT("Flash"), 0.f);
    }
    if (Age >= Duration + Settings.FragmentLifeSeconds) Destroy();
}

void AReditusEnemyDeathVisual::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    StopOwnedShake(true);
    AActor* Enemy = SourceEnemy.Get();
    const bool bSame = SameEncounter();
    const bool bShouldDestroy = !bShakeOnly && ReditusEnemyDeathRules::ShouldDestroySource(bClaimed, bSame, IsValid(Enemy),
        ReditusEnemyDeathRuntime::Integer(Enemy, TEXT("currentHP")), ReditusEnemyDeathRuntime::Boolean(Enemy, TEXT("IsDead?")));
    if (bClaimed && bSame && IsValid(Enemy) && ReditusEnemyDeathRuntime::Integer(Enemy, TEXT("currentHP")) > 0 &&
        !ReditusEnemyDeathRuntime::Boolean(Enemy, TEXT("IsDead?"))) RestoreSource();
    if (bClaimed && IsValid(Enemy)) Enemy->Tags.Remove(ReditusEnemyDeathRuntime::StartedTag);
    bClaimed = false;
    // EndPlay in editor/level teardown is not an extra gameplay callback.
    if (EndPlayReason == EEndPlayReason::Destroyed && bShouldDestroy) Enemy->Destroy();
    Super::EndPlay(EndPlayReason);
}

bool UReditusEnemyDeathLibrary::TryStartEnemyDeathVisual(AActor* Enemy, UDataTable* EnemyTable, UMaterialInterface* DissolveMaterial)
{
    using namespace ReditusEnemyDeathRuntime;
    if (!IsEnemy(Enemy) || Enemy->IsTemplate() || !Enemy->GetWorld() ||
        Integer(Enemy, TEXT("currentHP")) > 0 || !Boolean(Enemy, TEXT("IsDead?"))) return false;
    if (Enemy->ActorHasTag(StartedTag)) return true;
    TInlineComponentArray<UReditusEnemyDeathComponent*> Components;
    Enemy->GetComponents(Components);
    UReditusEnemyDeathComponent* Component = nullptr;
    for (auto* Candidate : Components)
        if (IsValid(Candidate))
        {
            if (Component)
            {
                UE_LOG(LogReditusEnemyDeath, Warning, TEXT("%s has multiple Enemy Death components; keeping existing death."), *Enemy->GetName());
                return false;
            }
            Component = Candidate;
        }
    if (Component)
    {
        if (IsValid(Component->EnemyTable)) EnemyTable = Component->EnemyTable;
        if (IsValid(Component->DissolveMaterial)) DissolveMaterial = Component->DissolveMaterial;
    }
    FReditusEnemyDeathSettings Settings;
    if (!ResolveSettings(Enemy, EnemyTable, Component, Settings)) return false;
    if (Settings.Style == EReditusEnemyDeathStyle::BossCameraShake)
    {
        if (!Settings.bBossCameraShake || Settings.BossShakeStrength <= 0.f) return false;
        FActorSpawnParameters Params;
        Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        auto* Visual = Enemy->GetWorld()->SpawnActor<AReditusEnemyDeathVisual>(AReditusEnemyDeathVisual::StaticClass(), Enemy->GetActorTransform(), Params);
        if (!Visual) return false;
        if (!Visual->InitializeShakeOnly(Enemy, Settings)) { Visual->Destroy(); return false; }
        Visual->ClaimSource();
        return true;
    }
    if (Settings.Style == EReditusEnemyDeathStyle::LegacyAnimation || !IsValid(DissolveMaterial)) return false;
    if (Settings.Style != EReditusEnemyDeathStyle::CrimsonDissolve && Settings.Style != EReditusEnemyDeathStyle::BossDissolve) return false;
    const FName SpriteProperty = Component ? Component->SpriteComponentProperty : FName(TEXT("Pixel2D"));
    auto* Source = SpriteProperty.IsNone() ? nullptr
        : Cast<UPaperFlipbookComponent>(Object(Enemy, *SpriteProperty.ToString()));
    if (!Source || Source->GetOwner() != Enemy || !Source->GetFlipbook())
    {
        UE_LOG(LogReditusEnemyDeath, Warning, TEXT("%s: no supported owned flipbook in the configured visual property; keeping existing death."), *Enemy->GetName());
        return false;
    }
    UPaperSprite* Frame = Source->GetFlipbook()->GetSpriteAtTime(Source->GetPlaybackPosition(), true);
    if (!IsValid(Frame)) return false;
    FActorSpawnParameters Params;
    Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    auto* Visual = Enemy->GetWorld()->SpawnActor<AReditusEnemyDeathVisual>(AReditusEnemyDeathVisual::StaticClass(), Source->GetComponentTransform(), Params);
    if (!Visual) return false;
    if (!Visual->Initialize(Enemy, Source, Frame, DissolveMaterial, Settings))
    {
        Visual->Destroy();
        UE_LOG(LogReditusEnemyDeath, Warning, TEXT("%s: could not capture death visual; keeping existing death."), *Enemy->GetName());
        return false;
    }
    Visual->ClaimSource();
    return true;
}

IMPLEMENT_MODULE(FDefaultModuleImpl, ReditusEnemyDeath)
