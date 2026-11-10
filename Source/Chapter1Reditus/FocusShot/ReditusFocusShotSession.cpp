#include "ReditusFocusShotSession.h"

#include "../UI/ReditusBattleHUDPresenterComponent.h"
#include "../UI/ReditusBattleHUDWidget.h"
#include "Blueprint/WidgetLayoutLibrary.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/PrimitiveComponent.h"
#include "Components/SceneComponent.h"
#include "Engine/GameViewportClient.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "Framework/Application/IInputProcessor.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/PlayerController.h"
#include "HAL/PlatformTime.h"
#include "Layout/WidgetPath.h"
#include "Rendering/DrawElements.h"
#include "Styling/CoreStyle.h"
#include "UObject/UnrealType.h"
#include "Widgets/SLeafWidget.h"
#include "Widgets/SViewport.h"
#include "Widgets/SWindow.h"

using ReditusFocusShotRules::EPhase;
using ReditusFocusShotRules::EResult;

namespace
{
    const FLinearColor Ink(0.008f, 0.016f, 0.035f, 1.f);
    const FLinearColor Cyan(0.22f, 0.90f, 1.f, 1.f);
    const FLinearColor Amber(1.f, 0.64f, 0.14f, 1.f);
    const FLinearColor White(0.95f, 0.98f, 1.f, 1.f);

    template<typename Field>
    const Field* FindMember(const UObject* Object, FName Name)
    {
        return IsValid(Object) ? FindFProperty<Field>(Object->GetClass(), Name) : nullptr;
    }

    template<typename T>
    T* ObjectMember(const UObject* Object, FName Name)
    {
        const auto* Field = FindMember<FObjectPropertyBase>(Object, Name);
        return Field ? Cast<T>(Field->GetObjectPropertyValue_InContainer(Object)) : nullptr;
    }

    bool Flag(const UObject* Object, FName Name)
    {
        const auto* Field = FindMember<FBoolProperty>(Object, Name);
        return Field && Field->GetPropertyValue_InContainer(Object);
    }

    bool IsAlive(const AActor* Target)
    {
        if (!IsValid(Target) || Flag(Target, TEXT("IsDead?"))) return false;
        const auto* HP = FindMember<FNumericProperty>(Target, TEXT("currentHP"));
        if (!HP) return true;
        const void* Value = HP->ContainerPtrToValuePtr<void>(Target);
        return HP->IsFloatingPoint() ? HP->GetFloatingPointPropertyValue(Value) > 0.0
            : HP->GetSignedIntPropertyValue(Value) > 0;
    }

    bool IsMoveKey(FKey K)
    {
        return K == EKeys::W || K == EKeys::A || K == EKeys::S || K == EKeys::D ||
            K == EKeys::Up || K == EKeys::Left || K == EKeys::Down || K == EKeys::Right ||
            K == EKeys::Gamepad_DPad_Up || K == EKeys::Gamepad_DPad_Left ||
            K == EKeys::Gamepad_DPad_Down || K == EKeys::Gamepad_DPad_Right ||
            K == EKeys::Gamepad_LeftStick_Up || K == EKeys::Gamepad_LeftStick_Left ||
            K == EKeys::Gamepad_LeftStick_Down || K == EKeys::Gamepad_LeftStick_Right;
    }

    bool IsFireKey(FKey K)
    {
        return K == EKeys::SpaceBar || K == EKeys::E || K == EKeys::Enter ||
            K == EKeys::LeftMouseButton || K == EKeys::Gamepad_FaceButton_Bottom;
    }

    bool IsCancelKey(FKey K)
    {
        return K == EKeys::Escape || K == EKeys::BackSpace || K == EKeys::Gamepad_FaceButton_Right;
    }

    FVector2D ClampAim(FVector2D P, const FVector2D& Size)
    {
        return FVector2D(FMath::Clamp(P.X, 26.f, Size.X - 26.f),
            FMath::Clamp(P.Y, 38.f, FMath::Max(38.f, Size.Y - 26.f)));
    }

    void Line(FSlateWindowElementList& Out, const FGeometry& G, int32 Layer,
        TArray<FVector2D> Points, const FLinearColor& Color, float Width)
    {
        FSlateDrawElement::MakeLines(Out, Layer, G.ToPaintGeometry(), MoveTemp(Points),
            ESlateDrawEffect::None, Color, true, Width);
    }

    void Text(FSlateWindowElementList& Out, const FGeometry& G, int32 Layer,
        FVector2D P, const FText& Label, int32 Size, const FLinearColor& Color)
    {
        const auto Font = FCoreStyle::GetDefaultFontStyle("Bold", Size);
        FSlateDrawElement::MakeText(Out, Layer, G.ToPaintGeometry(FVector2D(450.f, 36.f),
            FSlateLayoutTransform(P + FVector2D(1.f, 2.f))), Label, Font, ESlateDrawEffect::None, Ink);
        FSlateDrawElement::MakeText(Out, Layer + 1, G.ToPaintGeometry(FVector2D(450.f, 36.f),
            FSlateLayoutTransform(P)), Label, Font, ESlateDrawEffect::None, Color);
    }
}

class SReditusFocusShotOverlay final : public SLeafWidget
{
public:
    SLATE_BEGIN_ARGS(SReditusFocusShotOverlay) {}
    SLATE_END_ARGS()

    void Construct(const FArguments&, AReditusFocusShotSession* InSession)
    {
        Session = InSession;
        SetVisibility(EVisibility::HitTestInvisible);
    }

    virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D(1.f, 1.f); }

    virtual int32 OnPaint(const FPaintArgs&, const FGeometry& G, const FSlateRect&,
        FSlateWindowElementList& Out, int32 Layer, const FWidgetStyle&, bool) const override
    {
        const auto* S = Session.Get();
        if (!S || !S->bAimInitialized) return Layer;
        const bool bFeedback = S->Run.Phase == EPhase::Feedback;
        if (bFeedback && S->Run.Result == EResult::NoShot) return Layer;
        const FVector2D Size = G.GetLocalSize();
        const float Distance = FVector2D::DistSquared(S->Reticle, S->WeakSpotScreen);
        const bool bOnWeakSpot = ReditusFocusShotRules::Judge(true, S->bWeakSpotVisible,
            Distance, S->WeakSpot.HitRadius) == EResult::WeakSpot;
        if (S->bWeakSpotVisible)
        {
            TArray<FVector2D> Circle;
            for (int32 I = 0; I <= 36; ++I)
            {
                const float Angle = (2.f * PI * I) / 36.f;
                Circle.Add(S->WeakSpotScreen + FVector2D(FMath::Cos(Angle), FMath::Sin(Angle)) * S->WeakSpot.HitRadius);
            }
            Line(Out, G, Layer, Circle, Ink, 6.f);
            Line(Out, G, Layer + 1, MoveTemp(Circle), Amber, 2.5f);
        }

        const float Gap = 11.f;
        const float Edge = 24.f;
        const FLinearColor ReticleColor = bOnWeakSpot ? White : Cyan;
        for (const float X : {-1.f, 1.f})
        for (const float Y : {-1.f, 1.f})
        {
            TArray<FVector2D> Corner = { S->Reticle + FVector2D(X * Gap, Y * Edge),
                S->Reticle + FVector2D(X * Edge, Y * Edge), S->Reticle + FVector2D(X * Edge, Y * Gap) };
            Line(Out, G, Layer + 4, Corner, Ink, 7.f);
            Line(Out, G, Layer + 5, MoveTemp(Corner), ReticleColor, bOnWeakSpot ? 4.f : 3.f);
        }

        const float X = FMath::Max(20.f, Size.X * 0.5f - 124.f);
        const float Y = 30.f;
        const FVector2D BarStart(X, Y + 53.f);
        const float Fraction = FMath::Clamp(1.f - S->Elapsed / S->TotalAimTime, 0.f, 1.f);
        Line(Out, G, Layer + 6, {BarStart, BarStart + FVector2D(248.f, 0.f)}, Ink, 9.f);
        if (Fraction > 0.f)
            Line(Out, G, Layer + 7, {BarStart, BarStart + FVector2D(248.f * Fraction, 0.f)},
                Fraction < 0.25f ? Amber : Cyan, 4.f);
        Text(Out, G, Layer + 8, FVector2D(X, Y), NSLOCTEXT("ReditusFocusShot", "Title", "FOCUS SHOT"), 21, White);
        const FText Hint = bFeedback
            ? (S->Run.Result == EResult::WeakSpot ? NSLOCTEXT("ReditusFocusShot", "Hit", "WEAK SPOT") :
                NSLOCTEXT("ReditusFocusShot", "Shot", "SHOT"))
            : (S->bUsedGamepad ? NSLOCTEXT("ReditusFocusShot", "Pad", "LEFT STICK  Aim     A / Cross  Fire") :
                NSLOCTEXT("ReditusFocusShot", "Keys", "WASD  Aim     SPACE / E / CLICK  Fire"));
        Text(Out, G, Layer + 8, FVector2D(X, Y + 25.f), Hint, 13, bFeedback ? Amber : Cyan);
        return Layer + 9;
    }

private:
    TWeakObjectPtr<AReditusFocusShotSession> Session;
};

class FReditusFocusShotInput final : public IInputProcessor
{
public:
    explicit FReditusFocusShotInput(AReditusFocusShotSession* InSession) : Session(InSession) {}
    virtual void Tick(const float, FSlateApplication&, TSharedRef<ICursor>) override {}
    virtual bool HandleKeyDownEvent(FSlateApplication&, const FKeyEvent& Event) override
    {
        auto* S = Session.Get();
        return S && S->OwnsInput(Event.GetUserIndex(), Event.GetKey()) && S->KeyDown(Event.GetKey(), Event.IsRepeat());
    }
    virtual bool HandleKeyUpEvent(FSlateApplication&, const FKeyEvent& Event) override
    {
        auto* S = Session.Get();
        return S && S->OwnsInput(Event.GetUserIndex(), Event.GetKey()) && S->KeyUp(Event.GetKey());
    }
    virtual bool HandleAnalogInputEvent(FSlateApplication&, const FAnalogInputEvent& Event) override
    {
        auto* S = Session.Get();
        return S && S->OwnsInput(Event.GetUserIndex(), Event.GetKey()) && S->Analog(Event.GetKey(), Event.GetAnalogValue());
    }
    virtual bool HandleMouseButtonDownEvent(FSlateApplication&, const FPointerEvent& Event) override
    {
        auto* S = Session.Get();
        return S && S->OwnsInput(Event.GetUserIndex(), Event.GetEffectingButton()) && S->KeyDown(Event.GetEffectingButton(), false);
    }
    virtual bool HandleMouseButtonUpEvent(FSlateApplication&, const FPointerEvent& Event) override
    {
        auto* S = Session.Get();
        return S && S->OwnsInput(Event.GetUserIndex(), Event.GetEffectingButton()) && S->KeyUp(Event.GetEffectingButton());
    }
private:
    TWeakObjectPtr<AReditusFocusShotSession> Session;
};

AReditusFocusShotSession::AReditusFocusShotSession()
{
    PrimaryActorTick.bCanEverTick = true;
    PrimaryActorTick.bStartWithTickEnabled = false;
    PrimaryActorTick.TickGroup = TG_PostUpdateWork;
}

bool AReditusFocusShotSession::GetFocusShotRifleResult(EReditusFocusShotResult Result,
    float& CriticalChance, bool& bWeakSpotHit)
{
    bWeakSpotHit = Result == EReditusFocusShotResult::WeakSpotHit;
    CriticalChance = bWeakSpotHit ? 100.f : 0.f;
    return bWeakSpotHit || Result == EReditusFocusShotResult::NormalShot;
}

bool AReditusFocusShotSession::CanHitFocusShotTarget(AActor* BattleController, AActor* Target)
{
    return IsValid(BattleController) && !Flag(BattleController, TEXT("isBattleOver")) &&
        IsAlive(Target) && !Flag(Target, TEXT("Swallowed"));
}

float AReditusFocusShotSession::GetFocusShotRifleFlightTime(FTransform MuzzleTransform,
    AActor* Target, float ProjectileSpeed)
{
    if (!IsValid(Target) || !FMath::IsFinite(ProjectileSpeed) || ProjectileSpeed <= 0.f) return 0.08f;
    const auto* HitPoint = ObjectMember<USceneComponent>(Target, TEXT("ProjectileHitLocation"));
    const FVector Destination = HitPoint ? HitPoint->GetComponentLocation() : Target->GetActorLocation();
    const float Distance = FVector::Dist(MuzzleTransform.GetLocation(), Destination);
    return FMath::IsFinite(Distance) ? FMath::Clamp(Distance / ProjectileSpeed, 0.04f, 3.f) : 0.08f;
}

void AReditusFocusShotSession::StartFocusShot(APlayerController* PlayerController, AActor* BattleController,
    AActor* Target, USceneComponent* VisualAnchor)
{
    if (!Run.Begin()) return;
    Player = PlayerController;
    Battle = BattleController;
    AimTarget = Target;
    FiringUnit = ObjectMember<AActor>(BattleController, TEXT("currentAttackingUnit"));
    bTracksFiringUnit = FiringUnit.IsValid();
    if (IsValid(PlayerController)) LocalPlayer = PlayerController->GetLocalPlayer();
    if (ULocalPlayer* Local = LocalPlayer.Get()) ViewportClient = Local->ViewportClient;

    if (!HasLiveTarget() || !Player.IsValid() || !LocalPlayer.IsValid() || !ViewportClient.IsValid() ||
        !FSlateApplication::IsInitialized() || !ViewportClient->GetGameViewportWidget().IsValid())
    {
        LockShot(EResult::NoShot);
        Finish();
        return;
    }

    AimAnchor = IsValid(VisualAnchor) && VisualAnchor->GetOwner() == Target ? VisualAnchor
        : ObjectMember<USceneComponent>(Target, TEXT("Pixel2D"));
    if (!AimAnchor.IsValid()) AimAnchor = Target->GetRootComponent();
    const auto* IDField = FindMember<FNameProperty>(Target, TEXT("EnemyID"));
    const FName ID = IDField ? IDField->GetPropertyValue_InContainer(Target) : NAME_None;
    if (IsValid(WeakSpotTable) && !ID.IsNone() && WeakSpotTable->GetRowStruct() == FReditusFocusShotTargetRow::StaticStruct())
        if (const auto* Row = WeakSpotTable->FindRow<FReditusFocusShotTargetRow>(ID, TEXT("Focus Shot"), false))
        {
            WeakSpot = *Row;
            WeakSpot.HitRadius = FMath::IsFinite(Row->HitRadius) ? FMath::Clamp(Row->HitRadius, 6.f, 60.f) : 19.f;
            bWeakSpotConfigured = !WeakSpot.LocalOffset.ContainsNaN();
        }
    if (!bWeakSpotConfigured && AimAnchor.IsValid() && AimAnchor->DoesSocketExist(TEXT("Headshot")))
    {
        WeakSpot = FReditusFocusShotTargetRow();
        WeakSpot.SocketName = TEXT("Headshot");
        bWeakSpotConfigured = true;
    }

    TotalAimTime = FMath::IsFinite(AimSeconds) ? FMath::Clamp(AimSeconds, 0.5f, 15.f) : 4.f;
    if (auto* Presenter = BattleController->FindComponentByClass<UReditusBattleHUDPresenterComponent>())
        if (UReditusBattleHUDWidget* HUD = Presenter->HUD)
        {
            SuspendedHUD = HUD;
            bHadSuspendedHUD = true;
            bHUDWasSuspended = HUD->bInputSuspended;
            HUD->bInputSuspended = true;
        }

    PreviousViewTarget = PlayerController->GetViewTarget();
    if (bUseFocusCamera && IsValid(PlayerController->PlayerCameraManager) && PreviousViewTarget.IsValid())
    {
        const FRotator Facing = PlayerController->PlayerCameraManager->GetCameraRotation();
        FVector Center, Extent;
        Target->GetActorBounds(false, Center, Extent);
        if (auto* Visual = Cast<UPrimitiveComponent>(AimAnchor.Get()))
        {
            const auto Bounds = Visual->Bounds;
            if (!Bounds.Origin.ContainsNaN() && Bounds.SphereRadius > 1.f)
            {
                Center = Bounds.Origin;
                Extent = Bounds.BoxExtent;
            }
        }
        const float FOV = FMath::IsFinite(CameraFOV) ? FMath::Clamp(CameraFOV, 15.f, 100.f) : 48.f;
        const float BaseDistance = FMath::IsFinite(CameraDistance) ? FMath::Max(100.f, CameraDistance) : 560.f;
        const float Distance = FMath::Max(BaseDistance, Extent.GetMax() * 3.4f);
        FActorSpawnParameters Spawn;
        Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        if (auto* Camera = GetWorld()->SpawnActor<ACameraActor>(Center - Facing.Vector() * Distance, Facing, Spawn))
        {
            Camera->GetCameraComponent()->SetFieldOfView(FOV);
            Camera->GetCameraComponent()->bConstrainAspectRatio = false;
            FocusCamera = Camera;
            PlayerController->SetViewTarget(Camera);
        }
    }

    Overlay = SNew(SReditusFocusShotOverlay, this);
    ViewportClient->AddViewportWidgetForPlayer(LocalPlayer.Get(), Overlay.ToSharedRef(), 120);
    Input = MakeShared<FReditusFocusShotInput>(this);
    if (!FSlateApplication::Get().RegisterInputPreProcessor(Input, 0))
    {
        LockShot(EResult::NoShot);
        Finish();
        return;
    }
    CaptureKeyboardFocus();
    LastTickReal = FPlatformTime::Seconds();
    SetActorTickEnabled(true);
}

bool AReditusFocusShotSession::OwnsBattleAction() const
{
    return !Flag(Battle.Get(), TEXT("isBattleOver")) &&
        (!bTracksFiringUnit || (FiringUnit.IsValid() &&
            ObjectMember<AActor>(Battle.Get(), TEXT("currentAttackingUnit")) == FiringUnit.Get()));
}

bool AReditusFocusShotSession::HasLiveTarget() const
{
    return OwnsBattleAction() && CanHitFocusShotTarget(Battle.Get(), AimTarget.Get());
}

bool AReditusFocusShotSession::GameWindowAcceptsInput() const
{
    if (!LocalPlayer.IsValid() || !ViewportClient.IsValid() || !FSlateApplication::IsInitialized()) return false;
    const auto GameWidget = ViewportClient->GetGameViewportWidget();
    FSlateApplication& Slate = FSlateApplication::Get();
    if (!GameWidget.IsValid() || Slate.GetActiveModalWindow().IsValid()) return false;
    FWidgetPath GamePath;
    const auto Active = Slate.GetActiveTopLevelWindow();
    return Slate.GeneratePathToWidgetUnchecked(GameWidget.ToSharedRef(), GamePath,
        EVisibility::All) && (!Active.IsValid() || Active == GamePath.TopLevelWindow);
}

void AReditusFocusShotSession::CaptureKeyboardFocus()
{
    if (!GameWindowAcceptsInput()) return;
    FSlateApplication& Slate = FSlateApplication::Get();
    const auto GameWidget = ViewportClient->GetGameViewportWidget();
    const auto Focused = Slate.GetUserFocusedWidget(0);
    if (Focused == GameWidget) return;
    PreviousKeyboardFocus = Focused;
    Slate.SetUserFocus(0, GameWidget, EFocusCause::SetDirectly);
    bCapturedKeyboardFocus = Slate.GetUserFocusedWidget(0) == GameWidget;
}

bool AReditusFocusShotSession::AcceptsGameInput() const
{
    if (!GameWindowAcceptsInput()) return false;
    const auto GameWidget = ViewportClient->GetGameViewportWidget();
    FSlateApplication& Slate = FSlateApplication::Get();
    const auto Focused = Slate.GetUserFocusedWidget(0);
    if (!Focused.IsValid() || Focused == GameWidget) return true;
    FWidgetPath Path;
    if (!Slate.GeneratePathToWidgetUnchecked(Focused.ToSharedRef(), Path, EVisibility::All)) return true;
    if (Path.ContainsWidget(GameWidget.ToSharedRef())) return true;
    if (const auto* HUD = SuspendedHUD.Get())
    {
        const auto Cached = HUD->GetCachedWidget();
        if (Cached.IsValid() && Path.ContainsWidget(Cached.ToSharedRef())) return true;
    }
    for (int32 I = 0; I < Path.Widgets.Num(); ++I)
        if (Path.Widgets[I].Widget->GetTypeAsString() == TEXT("SGameLayerManager")) return true;
    return false;
}

bool AReditusFocusShotSession::OwnsInput(uint32 UserIndex, FKey Key) const
{
    if ((Run.Phase != EPhase::Aiming && Run.Phase != EPhase::Feedback) || !LocalPlayer.IsValid()) return false;
    // Keyboard and mouse use Slate's primary user; controller IDs can differ.
    return UserIndex == (Key.IsGamepadKey() ? static_cast<uint32>(FMath::Max(0, LocalPlayer->GetControllerId())) : 0u)
        && AcceptsGameInput();
}

void AReditusFocusShotSession::FireRifle()
{
    if (!bAimInitialized)
    {
        bFireRequested = true;
        return;
    }
    bFireRequested = false;
    LockShot(ReditusFocusShotRules::Judge(true, bWeakSpotVisible,
        FVector2D::DistSquared(Reticle, WeakSpotScreen), WeakSpot.HitRadius));
}

bool AReditusFocusShotSession::KeyDown(FKey Key, bool bRepeat)
{
    if (!IsMoveKey(Key) && !IsFireKey(Key) && !IsCancelKey(Key)) return false;
    if (Run.Phase != EPhase::Aiming) return true;
    bUsedGamepad = Key.IsGamepadKey();
    if (IsMoveKey(Key)) HeldKeys.Add(Key);
    if (!bRepeat && IsCancelKey(Key)) LockShot(EResult::NoShot);
    else if (!bRepeat && IsFireKey(Key)) FireRifle();
    return true;
}

bool AReditusFocusShotSession::KeyUp(FKey Key)
{
    HeldKeys.Remove(Key);
    return IsMoveKey(Key) || IsFireKey(Key) || IsCancelKey(Key);
}

bool AReditusFocusShotSession::Analog(FKey Key, float Value)
{
    if (Key != EKeys::Gamepad_LeftX && Key != EKeys::Gamepad_LeftY) return false;
    if (Key == EKeys::Gamepad_LeftX) Stick.X = Value;
    else Stick.Y = -Value;
    if (FMath::Abs(Value) > 0.25f) bUsedGamepad = true;
    return true;
}

void AReditusFocusShotSession::UpdateAim(const FVector2D& Size, float Step)
{
    bWeakSpotVisible = false;
    if (Size.X < 120.f || Size.Y < 100.f) return;
    FVector2D TargetPoint = FVector2D::ZeroVector;
    if (AActor* Target = AimTarget.Get())
    {
        FVector Center, Extent;
        Target->GetActorBounds(false, Center, Extent);
        if (auto* Anchor = AimAnchor.Get())
        {
            const auto* Visual = Cast<UPrimitiveComponent>(Anchor);
            if (Visual && Visual->Bounds.SphereRadius > 1.f) Center = Visual->Bounds.Origin;
            if (bWeakSpotConfigured)
            {
                const FTransform Basis = !WeakSpot.SocketName.IsNone() && Anchor->DoesSocketExist(WeakSpot.SocketName)
                    ? Anchor->GetSocketTransform(WeakSpot.SocketName) : Anchor->GetComponentTransform();
                const FVector Point = Basis.TransformPosition(WeakSpot.LocalOffset);
                bWeakSpotVisible = UWidgetLayoutLibrary::ProjectWorldLocationToWidgetPosition(
                    Player.Get(), Point, WeakSpotScreen, true) && FMath::IsFinite(WeakSpotScreen.X) &&
                    FMath::IsFinite(WeakSpotScreen.Y) && WeakSpotScreen.X >= 26.f && WeakSpotScreen.Y >= 38.f &&
                    WeakSpotScreen.X <= Size.X - 26.f && WeakSpotScreen.Y <= Size.Y - 26.f;
            }
        }
        if (!UWidgetLayoutLibrary::ProjectWorldLocationToWidgetPosition(Player.Get(), Center, TargetPoint, true) ||
            !FMath::IsFinite(TargetPoint.X) || !FMath::IsFinite(TargetPoint.Y))
            TargetPoint = Size * 0.5f;
    }

    TargetScreen = ClampAim(TargetPoint, Size);
    if (!bAimInitialized)
    {
        const FVector2D Center = bWeakSpotVisible ? WeakSpotScreen : TargetScreen;
        const float Outer = FMath::IsFinite(ReticleSpawnMaxRadius)
            ? FMath::Clamp(ReticleSpawnMaxRadius, 24.f, 1000.f) : 120.f;
        const float RequestedInner = FMath::IsFinite(ReticleSpawnMinRadius)
            ? FMath::Max(0.f, ReticleSpawnMinRadius) : 64.f;
        const float Inner = bWeakSpotVisible ? FMath::Max(RequestedInner, WeakSpot.HitRadius + 8.f) : RequestedInner;
        const auto Spawn = ReditusFocusShotRules::SampleAimStart({Center.X, Center.Y},
            {26.f, 38.f, Size.X - 26.f, Size.Y - 26.f}, Inner, Outer, [] { return FMath::FRand(); });
        Aim = Reticle = FVector2D(Spawn.X, Spawn.Y);
        LastSize = Size;
        bAimInitialized = true;
        return;
    }
    else if (LastSize.X > 0.f && LastSize.Y > 0.f && !LastSize.Equals(Size))
        Aim = FVector2D(Aim.X * Size.X / LastSize.X, Aim.Y * Size.Y / LastSize.Y);
    LastSize = Size;

    const auto Held = [this](FKey A, FKey B, FKey C)
    {
        return HeldKeys.Contains(A) || HeldKeys.Contains(B) || HeldKeys.Contains(C) ? 1.f : 0.f;
    };
    FVector2D Direction(Held(EKeys::D, EKeys::Right, EKeys::Gamepad_DPad_Right) -
        Held(EKeys::A, EKeys::Left, EKeys::Gamepad_DPad_Left),
        Held(EKeys::S, EKeys::Down, EKeys::Gamepad_DPad_Down) -
        Held(EKeys::W, EKeys::Up, EKeys::Gamepad_DPad_Up));
    const float StickMagnitude = FMath::Min(1.f, Stick.Size());
    if (StickMagnitude > 0.19f)
        Direction += Stick.GetSafeNormal() * ((StickMagnitude - 0.19f) / 0.81f);
    if (Direction.SizeSquared() > 1.f) Direction.Normalize();
    Aim = ClampAim(Aim + Direction * (340.f * Step), Size);
    const float Sway = FMath::IsFinite(ReticleSway) ? FMath::Clamp(ReticleSway, 0.f, 32.f) : 7.f;
    Reticle = ClampAim(Aim + Sway * FVector2D(FMath::Sin(Elapsed * 5.f), FMath::Sin(Elapsed * 3.7f + 0.7f)), Size);
}

void AReditusFocusShotSession::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    if (Run.Phase == EPhase::Finished) return;
    if (!HasLiveTarget() || !Player.IsValid() || !ViewportClient.IsValid())
    {
        Run.Result = EResult::NoShot;
        Finish();
        return;
    }

    const double Now = FPlatformTime::Seconds();
    const float Step = FMath::Clamp(static_cast<float>(Now - LastTickReal), 0.f, 0.1f);
    LastTickReal = Now;
    if (Run.Phase == EPhase::Aiming)
    {
        if (!AcceptsGameInput()) { HeldKeys.Reset(); Stick = FVector2D::ZeroVector; }
        const FVector2D Size = Overlay.IsValid() ? Overlay->GetCachedGeometry().GetLocalSize() : FVector2D::ZeroVector;
        UpdateAim(Size, Step);
        if (!bAimInitialized)
        {
            ViewportWaitTime += Step;
            if (ViewportWaitTime >= 1.f) LockShot(EResult::NoShot);
        }
        else if (bFireRequested)
            FireRifle();
        else
        {
            Elapsed += Step;
            if (Elapsed >= TotalAimTime) LockShot(EResult::Normal);
        }
    }
    else if (Run.Phase == EPhase::Feedback)
    {
        FeedbackTime -= Step;
        if (FeedbackTime <= 0.f) { Finish(); return; }
    }
    if (Overlay.IsValid()) Overlay->Invalidate(EInvalidateWidgetReason::Paint);
}

void AReditusFocusShotSession::LockShot(EResult Result)
{
    if (!Run.Lock(Result)) return;
    HeldKeys.Reset();
    Stick = FVector2D::ZeroVector;
    FeedbackTime = Result == EResult::NoShot ? 0.f : 0.16f;
}

void AReditusFocusShotSession::CancelFocusShot()
{
    LockShot(EResult::NoShot);
}

void AReditusFocusShotSession::Finish()
{
    if (!Run.Complete()) return;
    const EReditusFocusShotResult Result = Run.Result == EResult::WeakSpot ? EReditusFocusShotResult::WeakSpotHit
        : Run.Result == EResult::Normal ? EReditusFocusShotResult::NormalShot : EReditusFocusShotResult::NoShot;
    AActor* Target = Result == EReditusFocusShotResult::NoShot ? nullptr : AimTarget.Get();
    const bool bNotify = OwnsBattleAction();
    ReleasePresentation();
    if (bNotify) OnResolved.Broadcast(Result, Target);
    if (IsValid(this)) Destroy();
}

void AReditusFocusShotSession::ReleasePresentation()
{
    SetActorTickEnabled(false);
    if (Input.IsValid() && FSlateApplication::IsInitialized())
        FSlateApplication::Get().UnregisterInputPreProcessor(Input);
    Input.Reset();
    if (bCapturedKeyboardFocus && GameWindowAcceptsInput())
    {
        FSlateApplication& Slate = FSlateApplication::Get();
        const auto Previous = PreviousKeyboardFocus.Pin();
        if (Previous.IsValid() && Slate.GetUserFocusedWidget(0) == ViewportClient->GetGameViewportWidget())
            Slate.SetUserFocus(0, Previous, EFocusCause::SetDirectly);
    }
    bCapturedKeyboardFocus = false;
    PreviousKeyboardFocus.Reset();
    if (Overlay.IsValid() && ViewportClient.IsValid() && LocalPlayer.IsValid())
        ViewportClient->RemoveViewportWidgetForPlayer(LocalPlayer.Get(), Overlay.ToSharedRef());
    Overlay.Reset();
    if (auto* Camera = FocusCamera.Get())
    {
        if (Player.IsValid() && Player->GetViewTarget() == Camera && PreviousViewTarget.IsValid())
            Player->SetViewTarget(PreviousViewTarget.Get());
        Camera->Destroy();
    }
    FocusCamera.Reset();
    if (bHadSuspendedHUD)
        if (auto* HUD = SuspendedHUD.Get())
            HUD->bInputSuspended = bHUDWasSuspended;
    bHadSuspendedHUD = false;
}

void AReditusFocusShotSession::EndPlay(const EEndPlayReason::Type Reason)
{
    const bool bWasActive = Run.Phase == EPhase::Aiming || Run.Phase == EPhase::Feedback;
    Run.Complete();
    ReleasePresentation();
    if (bWasActive && Reason == EEndPlayReason::Destroyed && Battle.IsValid() && OwnsBattleAction())
        OnResolved.Broadcast(EReditusFocusShotResult::NoShot, nullptr);
    Super::EndPlay(Reason);
}
