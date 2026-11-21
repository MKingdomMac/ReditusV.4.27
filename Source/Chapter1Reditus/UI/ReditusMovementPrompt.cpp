#include "ReditusMovementPrompt.h"

#include "../ATB/ReditusATBComponent.h"
#include "ReditusBattleHUDPresenterComponent.h"
#include "ReditusBattleHUDInputPolicy.h"
#include "ReditusBattleHUDWidget.h"
#include "ReditusTutorialPrivate.h"
#include "Components/ActorComponent.h"
#include "Components/TimelineComponent.h"
#include "Engine/GameInstance.h"
#include "Engine/GameViewportClient.h"
#include "Engine/LocalPlayer.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "Framework/Application/IInputProcessor.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/Actor.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/SaveGame.h"
#include "HAL/PlatformTime.h"
#include "Input/Events.h"
#include "InputCoreTypes.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet/KismetRenderingLibrary.h"
#include "Layout/WidgetPath.h"
#include "UObject/UnrealType.h"
#include "Widgets/SViewport.h"
#include "Widgets/SWindow.h"

DEFINE_LOG_CATEGORY_STATIC(LogReditusTutorialPrompt, Log, All);

namespace ReditusMovementPrompt
{
    constexpr double DurationSeconds = 4.5;
    constexpr int32 ViewportZOrder = 30;
    constexpr int32 BlockZOrder = 60;
    constexpr int32 DialogueZOrder = 90;
    constexpr float StickDeadZone = 0.30f;
    constexpr float AnalogChangeThreshold = 0.12f;

    #include "ReditusMovementPromptGlyphs.inl"

    UTexture2D* LoadGlyph(UObject* WorldContext, const uint8* Png, int32 ByteCount)
    {
        const TArray<uint8> Buffer(Png, ByteCount);
        return UKismetRenderingLibrary::ImportBufferAsTexture2D(WorldContext, Buffer);
    }

    USaveGame* FindStorySave(const UWorld& World)
    {
        const UGameInstance* Instance = World.GetGameInstance();
        const auto* SaveField = IsValid(Instance) ?
            FindFProperty<FObjectPropertyBase>(Instance->GetClass(), TEXT("jRPGSaveGame")) : nullptr;
        return SaveField ? Cast<USaveGame>(SaveField->GetObjectPropertyValue_InContainer(Instance)) : nullptr;
    }

    const FBoolProperty* FindTutorialCompletionFlag(const USaveGame* SaveGame, EReditusTutorialLesson Lesson)
    {
        static const TCHAR* Fields[] = {
            TEXT("BlockTutorialCompleted"), TEXT("FirstAttackTutorialCompleted"), TEXT("OverdriveTutorialCompleted")
        };
        static_assert(UE_ARRAY_COUNT(Fields) == static_cast<int32>(EReditusTutorialLesson::Count), "Tutorial flags must match the lesson order.");
        return IsValid(SaveGame) ? FindFProperty<FBoolProperty>(SaveGame->GetClass(), Fields[static_cast<uint8>(Lesson)]) : nullptr;
    }

    UReditusMovementPromptSubsystem* FindSubsystem(const UObject* Context, APlayerController*& OutPlayer)
    {
        OutPlayer = IsValid(Context) ? UGameplayStatics::GetPlayerController(Context, 0) : nullptr;
        ULocalPlayer* LocalPlayer = IsValid(OutPlayer) ? OutPlayer->GetLocalPlayer() : nullptr;
        return LocalPlayer ? LocalPlayer->GetSubsystem<UReditusMovementPromptSubsystem>() : nullptr;
    }

    UReditusBattleHUDWidget* FindLiveBattleHUD(const AActor& BattleController, const APlayerController& Player)
    {
        const auto* Presenter = BattleController.FindComponentByClass<UReditusBattleHUDPresenterComponent>();
        auto* HUD = Presenter ? Presenter->HUD : nullptr;
        const UWorld* World = Player.GetWorld();
        return World && BattleController.GetWorld() == World && IsValid(HUD) &&
            HUD->IsInViewport() && HUD->GetOwningPlayer() == &Player ? HUD : nullptr;
    }

    bool IsDialogueVisible(const UUserWidget& Widget)
    {
        const ESlateVisibility Visibility = Widget.GetVisibility();
        return Visibility != ESlateVisibility::Collapsed && Visibility != ESlateVisibility::Hidden;
    }

    FText BindingCaption(const FKey& Key)
    {
        if (Key == EKeys::Gamepad_LeftShoulder)
        {
            return NSLOCTEXT("ReditusMovementPrompt", "LeftShoulder", "LB");
        }
        if (Key == EKeys::Gamepad_FaceButton_Bottom)
        {
            return NSLOCTEXT("ReditusMovementPrompt", "GamepadConfirm", "A");
        }
        if (Key == EKeys::Gamepad_RightThumbstick)
        {
            return NSLOCTEXT("ReditusMovementPrompt", "RightStickButton", "RS");
        }
        if (Key == EKeys::SpaceBar)
        {
            return NSLOCTEXT("ReditusMovementPrompt", "Space", "SPACE");
        }
        return Key.GetDisplayName();
    }

    FKey ConfirmKey(const UReditusBattleHUDWidget& HUD, bool bGamepad)
    {
        const FKey Binding = bGamepad ? HUD.GamepadKeys.Bottom : HUD.KeyboardConfirmKey;
        return Binding.IsValid() ? Binding : (bGamepad ? EKeys::Gamepad_FaceButton_Bottom : EKeys::Enter);
    }

    FKey BlockKey(const UReditusBattleHUDWidget& HUD, bool bGamepad)
    {
        const FKey Binding = bGamepad ? HUD.GamepadKeys.Block : HUD.KeyboardBlockKey;
        return Binding.IsValid() ? Binding : (bGamepad ? EKeys::Gamepad_LeftShoulder : EKeys::SpaceBar);
    }
}

class FReditusMovementPromptInputObserver final : public IInputProcessor
{
public:
    explicit FReditusMovementPromptInputObserver(UReditusMovementPromptSubsystem* Subsystem)
        : Owner(Subsystem)
    {
    }

    void Tick(float, FSlateApplication&, TSharedRef<ICursor>) override
    {
        if (UReditusMovementPromptSubsystem* Subsystem = Owner.Get())
        {
            Subsystem->TickPrompt();
        }
    }

    bool HandleKeyDownEvent(FSlateApplication& SlateApp, const FKeyEvent& Event) override
    {
        if (!CanObserve(SlateApp, Event.GetUserIndex()))
        {
            return false;
        }
        if (!Event.IsRepeat())
        {
            Owner->RecordDevice(Event.GetKey().IsGamepadKey());
        }
        return Owner->HandleDialogueKey(Event.GetKey(), true, Event.IsRepeat()) ||
            Owner->HandleHeldBlockKey(Event.GetKey(), Event.IsRepeat()) ||
            Owner->HandleGuidedKey(Event.GetKey(), true, Event.IsRepeat());
    }

    bool HandleKeyUpEvent(FSlateApplication& SlateApp, const FKeyEvent& Event) override
    {
        if (!CanObserve(SlateApp, Event.GetUserIndex()))
        {
            return false;
        }
        const bool bWasGuideKey = Owner->HandleGuidedKey(Event.GetKey(), false, false);
        return Owner->HandleDialogueKey(Event.GetKey(), false, false) || bWasGuideKey;
    }

    bool HandleMouseButtonDownEvent(FSlateApplication& SlateApp, const FPointerEvent& Event) override
    {
        if (CanObserve(SlateApp, Event.GetUserIndex()))
        {
            Owner->RecordDevice(false);
            return Owner->HandleHeldBlockKey(Event.GetEffectingButton(), false);
        }
        return false;
    }

    bool HandleAnalogInputEvent(FSlateApplication& SlateApp, const FAnalogInputEvent& Event) override
    {
        const FKey Key = Event.GetKey();
        const float Value = Event.GetAnalogValue();
        if (!Key.IsGamepadKey() || !FMath::IsFinite(Value) || !CanObserve(SlateApp, Event.GetUserIndex()))
        {
            return false;
        }

        float& PreviousValue = PreviousRawAnalogValues.FindOrAdd(Key);
        const float Previous = PreviousValue;
        PreviousValue = Value;
        float& LastSignificantValue = PreviousAnalogValues.FindOrAdd(Key);
        if (FMath::Abs(Value) < ReditusMovementPrompt::StickDeadZone)
        {
            LastSignificantValue = 0.f;
        }
        else if (FMath::Abs(Value - LastSignificantValue) >= ReditusMovementPrompt::AnalogChangeThreshold)
        {
            LastSignificantValue = Value;
            Owner->RecordDevice(true);
        }
        const bool bLockingCommands = Owner->IsGuidedInputLocked() && !Owner->TutorialDialogue && !Owner->bHoldingBite;
        const auto RouteEdge = [&](FKey Digital, float Before, float After)
        {
            const int32 Edge = ReditusHUDInputPolicy::TriggerEdge(Before, After);
            if (Edge < 0 || (Edge > 0 && bLockingCommands))
            {
                Owner->HandleGuidedKey(Digital, Edge > 0, false);
            }
        };
        if (Key == EKeys::Gamepad_LeftTriggerAxis || Key == EKeys::Gamepad_RightTriggerAxis)
        {
            RouteEdge(Key == EKeys::Gamepad_LeftTriggerAxis ? EKeys::Gamepad_LeftTrigger : EKeys::Gamepad_RightTrigger, Previous, Value);
        }
        else if (Key == EKeys::Gamepad_LeftX || Key == EKeys::Gamepad_LeftY)
        {
            const bool bHorizontal = Key == EKeys::Gamepad_LeftX;
            RouteEdge(bHorizontal ? EKeys::Gamepad_LeftStick_Right : EKeys::Gamepad_LeftStick_Up,
                FMath::Max(0.f, Previous), FMath::Max(0.f, Value));
            RouteEdge(bHorizontal ? EKeys::Gamepad_LeftStick_Left : EKeys::Gamepad_LeftStick_Down,
                FMath::Max(0.f, -Previous), FMath::Max(0.f, -Value));
        }
        return bLockingCommands;
    }

private:
    bool CanObserve(FSlateApplication& SlateApp, uint32 UserIndex) const
    {
        const UReditusMovementPromptSubsystem* Subsystem = Owner.Get();
        const ULocalPlayer* LocalPlayer = Subsystem ? Subsystem->GetLocalPlayer() : nullptr;
        if (!LocalPlayer || !LocalPlayer->ViewportClient || !SlateApp.IsActive() || SlateApp.GetActiveModalWindow().IsValid()
            || LocalPlayer->GetControllerId() != static_cast<int32>(UserIndex))
        {
            return false;
        }

        const TSharedPtr<SViewport> GameViewport = LocalPlayer->ViewportClient->GetGameViewportWidget();
        if (!GameViewport.IsValid())
        {
            return false;
        }

        const TSharedPtr<SWidget> FocusedWidget = SlateApp.GetUserFocusedWidget(static_cast<int32>(UserIndex));
        FWidgetPath FocusPath;
        if (!FocusedWidget.IsValid() || !SlateApp.GeneratePathToWidgetUnchecked(
            FocusedWidget.ToSharedRef(), FocusPath, EVisibility::All))
        {
            return false;
        }
        const TSharedPtr<SWindow> ActiveWindow = SlateApp.GetActiveTopLevelWindow();
        if (ActiveWindow.IsValid() && FocusPath.TopLevelWindow.IsValid() && ActiveWindow != FocusPath.TopLevelWindow)
        {
            return false;
        }

        const auto* Dialogue = Subsystem->TutorialDialogue ? Subsystem->TutorialDialogue : Subsystem->GuidedPrompt;
        const TSharedPtr<SWidget> DialogueSlate = IsValid(Dialogue) ? Dialogue->GetCachedWidget() : nullptr;
        const auto* GuideHUD = Subsystem->GuidedHUD.Get();
        const TSharedPtr<SWidget> BattleSlate = IsValid(GuideHUD) ? GuideHUD->GetCachedWidget() : nullptr;
        if (FocusPath.ContainsWidget(GameViewport.ToSharedRef()) ||
            (DialogueSlate.IsValid() && FocusPath.ContainsWidget(DialogueSlate.ToSharedRef())) ||
            (BattleSlate.IsValid() && FocusPath.ContainsWidget(BattleSlate.ToSharedRef())))
        {
            return true;
        }
        for (int32 Index = 0; Index < FocusPath.Widgets.Num(); ++Index)
        {
            const FString Type = FocusPath.Widgets[Index].Widget->GetTypeAsString();
            if (Type == TEXT("SGameLayerManager"))
            {
                return true;
            }
        }
        return false;
    }

    TWeakObjectPtr<UReditusMovementPromptSubsystem> Owner;
    TMap<FKey, float> PreviousAnalogValues;
    TMap<FKey, float> PreviousRawAnalogValues;
};

void UReditusMovementPromptSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);
    RegisterInputObserver();
}

void UReditusMovementPromptSubsystem::Deinitialize()
{
    CloseTutorialDialogue(false);
    ResetFirstBattleTutorial();
    BattleDialogues.Reset();
    ReleaseDialoguePause();
    CancelBlockTutorial();
    DismissPrompt();
    if (InputObserver.IsValid() && FSlateApplication::IsInitialized())
    {
        FSlateApplication::Get().UnregisterInputPreProcessor(InputObserver);
    }
    InputObserver.Reset();
    Super::Deinitialize();
}

void UReditusMovementPromptSubsystem::RegisterInputObserver()
{
    if (!InputObserver.IsValid() && FSlateApplication::IsInitialized())
    {
        const TSharedPtr<IInputProcessor> Observer = MakeShared<FReditusMovementPromptInputObserver>(this);
        if (FSlateApplication::Get().RegisterInputPreProcessor(Observer, 0))
        {
            InputObserver = Observer;
        }
    }
}

void UReditusMovementPromptSubsystem::ShowPrompt(APlayerController& OwningController)
{
    if (bShowingBlockPrompt || !EnsurePrompt(OwningController))
    {
        return;
    }

    if (!bHasObservedDevice)
    {
        const FBoolProperty* KeyboardState =
            FindFProperty<FBoolProperty>(OwningController.GetClass(), TEXT("IsUsingKeyboard"));
        bLastInputWasGamepad = KeyboardState && !KeyboardState->GetPropertyValue_InContainer(&OwningController);
    }

    if (!KeyboardGlyph)
    {
        KeyboardGlyph = ReditusMovementPrompt::LoadGlyph(&OwningController, ReditusMovementPrompt::KeyboardPng,
            UE_ARRAY_COUNT(ReditusMovementPrompt::KeyboardPng));
    }
    if (!GamepadGlyph)
    {
        GamepadGlyph = ReditusMovementPrompt::LoadGlyph(&OwningController, ReditusMovementPrompt::GamepadPng,
            UE_ARRAY_COUNT(ReditusMovementPrompt::GamepadPng));
    }

    ActivePrompt->SetGlyph(bLastInputWasGamepad ? GamepadGlyph : KeyboardGlyph, bLastInputWasGamepad);
    if (DisplayPrompt(OwningController, ReditusMovementPrompt::ViewportZOrder))
    {
        DismissAt = FPlatformTime::Seconds() + ReditusMovementPrompt::DurationSeconds;
    }
}

bool UReditusMovementPromptSubsystem::EnsurePrompt(APlayerController& OwningController)
{
    RegisterInputObserver();
    UWorld* World = OwningController.GetWorld();
    if (!InputObserver.IsValid() || !World)
    {
        return false;
    }
    if (IsValid(ActivePrompt) && ActivePrompt->IsInViewport()
        && PromptWorld.Get() == World && ActivePrompt->GetOwningPlayer() == &OwningController)
    {
        return true;
    }

    DismissPrompt();
    ActivePrompt = CreateWidget<UReditusMovementPromptWidget>(&OwningController);
    return IsValid(ActivePrompt);
}

bool UReditusMovementPromptSubsystem::DisplayPrompt(APlayerController& OwningController, int32 ZOrder)
{
    if (!ActivePrompt->IsInViewport() && !ActivePrompt->AddToPlayerScreen(ZOrder))
    {
        DismissPrompt();
        return false;
    }
    PromptWorld = OwningController.GetWorld();
    return true;
}

void UReditusMovementPromptSubsystem::ArmBlockTutorial(AActor& BattleController, APlayerController& OwningController)
{
    UWorld* World = BattleController.GetWorld();
    if (!World || World != OwningController.GetWorld())
    {
        return;
    }

    ArmFirstBattleTutorial(BattleController, OwningController);
    const USaveGame* StorySave = ReditusMovementPrompt::FindStorySave(*World);
    const auto* Completed = ReditusMovementPrompt::FindTutorialCompletionFlag(StorySave, EReditusTutorialLesson::Block);
    if (Completed && Completed->GetPropertyValue_InContainer(StorySave))
    {
        return;
    }

    if (BlockBattleController.Get() == &BattleController && BlockPlayer.Get() == &OwningController)
    {
        if (!bBlockBattleAttached || IsBlockBattleActive())
        {
            return;
        }
    }

    CancelBlockTutorial();
    BlockBattleController = &BattleController;
    BlockPlayer = &OwningController;
    RegisterInputObserver();
    AttachBlockBattleHUD();
}

bool UReditusMovementPromptSubsystem::AttachBlockBattleHUD()
{
    if (bBlockBattleAttached)
    {
        return IsBlockBattleActive();
    }

    const auto* BattleController = BlockBattleController.Get();
    const auto* OwningController = BlockPlayer.Get();
    if (!IsValid(BattleController) || !IsValid(OwningController))
    {
        return false;
    }
    if (auto* HUD = ReditusMovementPrompt::FindLiveBattleHUD(*BattleController, *OwningController))
    {
        if (CompletedBlockBattleHUD.Get() == HUD)
        {
            CancelBlockTutorial();
            return false;
        }
        BlockBattleHUD = HUD;
        bBlockBattleAttached = true;
        return true;
    }
    return false;
}

bool UReditusMovementPromptSubsystem::IsBlockBattleActive() const
{
    const auto* BattleController = BlockBattleController.Get();
    const auto* OwningController = BlockPlayer.Get();
    return bBlockBattleAttached && BlockBattleHUD.IsValid() &&
        IsValid(BattleController) && IsValid(OwningController) &&
        ReditusMovementPrompt::FindLiveBattleHUD(*BattleController, *OwningController) == BlockBattleHUD.Get();
}

bool UReditusMovementPromptSubsystem::IsAttackerInBlockBattle(const AActor& Attacker) const
{
    const auto* BattleController = BlockBattleController.Get();
    if (!IsValid(BattleController) || Attacker.GetWorld() != BattleController->GetWorld())
    {
        return false;
    }
    const FObjectPropertyBase* BattleOwner = FindFProperty<FObjectPropertyBase>(Attacker.GetClass(), TEXT("battleController"));
    const UObject* AttackerController = BattleOwner ? BattleOwner->GetObjectPropertyValue_InContainer(&Attacker) : nullptr;
    return !AttackerController || AttackerController == BattleController;
}

void UReditusMovementPromptSubsystem::ShowBlockTutorialPrompt(AActor& Attacker, APlayerController& OwningController)
{
    if (bShowingBlockPrompt || bResolvingBite || BlockPlayer.Get() != &OwningController ||
        !AttachBlockBattleHUD() || !IsAttackerInBlockBattle(Attacker))
    {
        return;
    }

    DismissPrompt();
    if (!EnsurePrompt(OwningController))
    {
        return;
    }

    BlockAttacker = &Attacker;
    bShowingBlockPrompt = true;
    RefreshBlockGlyph();
    if (!DisplayPrompt(OwningController, ReditusMovementPrompt::BlockZOrder))
    {
        CancelBlockTutorial();
    }
}

void UReditusMovementPromptSubsystem::RefreshBlockGlyph()
{
    const auto* BattleHUD = BlockBattleHUD.Get();
    if (!IsValid(BattleHUD) || !IsValid(ActivePrompt))
    {
        return;
    }

    const FKey Binding = ReditusMovementPrompt::BlockKey(*BattleHUD, BattleHUD->GetInputDevice() == EReditusHUDInputDevice::Xbox);
    if (DisplayedBlockKey == Binding)
    {
        return;
    }

    DisplayedBlockKey = Binding;
    UTexture2D* Glyph = BattleHUD->GlyphTextures.FindRef(Binding.GetFName());
    ActivePrompt->SetBlockPrompt(Glyph, ReditusMovementPrompt::BindingCaption(Binding));
}

bool UReditusMovementPromptSubsystem::HoldBlockTutorialBite(AActor& Attacker, AActor& Defender,
    UActorComponent& AttackAnimation, bool bDefenderIsDefending, const FReditusHeldTutorialBite& ApplyBite)
{
    if (!AttachBlockBattleHUD() || !IsAttackerInBlockBattle(Attacker))
    {
        return false;
    }
    if (bResolvingBite)
    {
        return BlockAttacker.Get() == &Attacker;
    }
    if (!bShowingBlockPrompt)
    {
        // The impact can still start the lesson if a warning notify was missed.
        ShowBlockTutorialPrompt(Attacker, *BlockPlayer.Get());
    }
    if (!bShowingBlockPrompt || BlockAttacker.Get() != &Attacker)
    {
        return false;
    }
    if (bHoldingBite)
    {
        return true;
    }
    UWorld* World = Attacker.GetWorld();
    if (!World || Defender.GetWorld() != World || AttackAnimation.GetOwner() != &Attacker || !ApplyBite.IsBound() ||
        !FindFProperty<FBoolProperty>(Defender.GetClass(), TEXT("IsDefending")))
    {
        UE_LOG(LogReditusTutorialPrompt, Warning, TEXT("Hold Block Tutorial Bite needs the current defender with IsDefending, the attacker's animation component and a bound bite event."));
        CancelBlockTutorial();
        return false;
    }
    if (bDefenderIsDefending && EarlyBlockDefender.Get() == &Defender)
    {
        ResolveBlockTutorialBite(Defender, ApplyBite);
        return true;
    }

    // The return movement is driven separately from the Pixel2D animation.
    // Pause world time so its timeline and combat timers wait at impact too.
    if (UGameplayStatics::IsGamePaused(World) || !UGameplayStatics::SetGamePaused(World, true))
    {
        UE_LOG(LogReditusTutorialPrompt, Warning, TEXT("Unable to pause the local battle for the Block tutorial."));
        CancelBlockTutorial();
        return false;
    }

    HeldPauseWorld = World;
    HeldDefender = &Defender;
    HeldBite = ApplyBite;
    HeldAnimation = &AttackAnimation;
    bAttackerWasTicking = Attacker.IsActorTickEnabled();
    bAnimationWasTicking = AttackAnimation.IsComponentTickEnabled();
    bHoldingBite = true;
    Attacker.SetActorTickEnabled(false);
    AttackAnimation.SetComponentTickEnabled(false);
    TArray<UTimelineComponent*> Timelines;
    Attacker.GetComponents<UTimelineComponent>(Timelines);
    for (auto* Timeline : Timelines)
    {
        if (IsValid(Timeline) && Timeline->IsComponentTickEnabled())
        {
            HeldAttackerTimelines.Add(TWeakObjectPtr<UTimelineComponent>(Timeline));
            Timeline->SetComponentTickEnabled(false);
        }
    }
    if (auto* ATB = BlockBattleController->FindComponentByClass<UReditusATBComponent>())
    {
        HeldATB = ATB;
        bATBWasPaused = ATB->bPaused;
        ATB->bPaused = true;
    }
    return true;
}

void UReditusMovementPromptSubsystem::NotifyBlockTutorialDefense(AActor& Defender)
{
    UWorld* World = Defender.GetWorld();
    if (!bShowingBlockPrompt || bResolvingBite || !IsBlockBattleActive() || !World || World != PromptWorld.Get())
    {
        return;
    }
    if (!bHoldingBite)
    {
        EarlyBlockDefender = &Defender;
        return;
    }
    if (HeldDefender.Get() != &Defender || !HeldBite.IsBound())
    {
        return;
    }

    // The Blueprint's existing IsDefending delay starts after this call returns.
    ResolveBlockTutorialBite(Defender, HeldBite);
}

void UReditusMovementPromptSubsystem::ResolveBlockTutorialBite(AActor& Defender, FReditusHeldTutorialBite ReleaseBite)
{
    const TWeakObjectPtr<AActor> Target(&Defender);
    const TWeakObjectPtr<UWorld> ImpactWorld(Defender.GetWorld());
    bResolvingBite = true;
    ReleaseBlockHold();
    ReleaseBite.ExecuteIfBound();
    bResolvingBite = false;
    EarlyBlockDefender.Reset();

    // Notify Do Damage runs the existing Is Target Player Unit Defending check
    // and reads its Blocked result before the bite delegate returns.
    const auto* Blocked = Target.IsValid() ? FindFProperty<FBoolProperty>(Target->GetClass(), TEXT("Blocked")) : nullptr;
    if (!Target.IsValid() || !ImpactWorld.IsValid() || (Blocked && !Blocked->GetPropertyValue_InContainer(Target.Get())))
    {
        BlockAttacker.Reset();
        DismissPrompt();
        return;
    }

    const TWeakObjectPtr<AActor> BattleController = BlockBattleController;
    const TWeakObjectPtr<APlayerController> OwningController = BlockPlayer;
    CompletedBlockBattleHUD = BlockBattleHUD;
    RecordTutorialCompletion(*ImpactWorld.Get(), EReditusTutorialLesson::Block);
    CancelBlockTutorial();

    if (BattleController.IsValid() && OwningController.IsValid())
    {
        ShowBattleTutorialDialogue(*BattleController.Get(), *OwningController.Get(),
            NSLOCTEXT("ReditusMovementPrompt", "BlockSucceeded", "Block successful"),
            NSLOCTEXT("ReditusMovementPrompt", "BlockLesson", "Blocking reduces damage by half and increases Overdrive."),
            FReditusBattleTutorialContinued(), true);
    }
}

void UReditusMovementPromptSubsystem::RecordTutorialCompletion(UWorld& World, EReditusTutorialLesson Lesson)
{
    auto& Entry = Completions[static_cast<uint8>(Lesson)];
    Entry.World = &World;
    Entry.Save = ReditusMovementPrompt::FindStorySave(World);
    Entry.bHadSave = Entry.Save.IsValid();
}

bool UReditusMovementPromptSubsystem::ShouldSaveTutorialCompletion(const UWorld& World, const USaveGame& SaveGame,
    EReditusTutorialLesson Lesson) const
{
    const auto& Entry = Completions[static_cast<uint8>(Lesson)];
    // Starting a map directly in PIE can leave the GameInstance without a
    // save object until the first save. Associate that completion with its world.
    return ReditusMovementPrompt::FindStorySave(World) == &SaveGame &&
        (Entry.bHadSave ? Entry.Save.Get() == &SaveGame : Entry.World.Get() == &World);
}

void UReditusMovementPromptSubsystem::ReleaseBlockHold()
{
    if (bHoldingBite)
    {
        if (auto* Attacker = BlockAttacker.Get())
        {
            Attacker->SetActorTickEnabled(bAttackerWasTicking);
        }
    }
    if (auto* Animation = HeldAnimation.Get())
    {
        Animation->SetComponentTickEnabled(bAnimationWasTicking);
    }
    for (const auto& Timeline : HeldAttackerTimelines)
    {
        if (auto* Component = Timeline.Get())
        {
            Component->SetComponentTickEnabled(true);
        }
    }
    HeldAttackerTimelines.Reset();
    if (auto* ATB = HeldATB.Get())
    {
        ATB->bPaused = bATBWasPaused;
    }
    HeldAnimation.Reset();
    HeldATB.Reset();
    HeldDefender.Reset();
    HeldBite.Unbind();
    bHoldingBite = false;
    if (UWorld* World = HeldPauseWorld.Get())
    {
        HeldPauseWorld.Reset();
        UGameplayStatics::SetGamePaused(World, false);
    }
}

void UReditusMovementPromptSubsystem::CancelBlockTutorial()
{
    ReleaseBlockHold();
    BlockAttacker.Reset();
    EarlyBlockDefender.Reset();
    BlockBattleController.Reset();
    BlockPlayer.Reset();
    BlockBattleHUD.Reset();
    bBlockBattleAttached = false;
    if (bShowingBlockPrompt)
    {
        DismissPrompt();
    }
}

bool UReditusMovementPromptSubsystem::ShowBattleTutorialDialogue(AActor& BattleController,
    APlayerController& OwningController, const FText& Title, const FText& Message,
    const FReditusBattleTutorialContinued& OnContinued, bool bShowShield, int32 StaminaExampleMax)
{
    auto* HUD = ReditusMovementPrompt::FindLiveBattleHUD(BattleController, OwningController);
    RegisterInputObserver();
    if (!HUD || !InputObserver.IsValid() || TutorialDialogue || Message.IsEmpty())
    {
        return false;
    }

    auto* Widget = CreateWidget<UReditusMovementPromptWidget>(&OwningController);
    if (!IsValid(Widget))
    {
        return false;
    }
    TutorialDialogue = Widget;
    Widget->SetDialogue(Title, Message, bShowShield, FText(), StaminaExampleMax);
    Widget->ContinueRequested.BindUObject(this, &UReditusMovementPromptSubsystem::ContinueTutorialDialogue);
    bLastInputWasGamepad = HUD->GetInputDevice() == EReditusHUDInputDevice::Xbox;
    const int32 UserIndex = GetLocalPlayer()->GetControllerId();
    PreviousDialogueFocus = FSlateApplication::Get().GetUserFocusedWidget(UserIndex);
    if (!Widget->AddToPlayerScreen(ReditusMovementPrompt::DialogueZOrder) ||
        !TrackBattleDialogue(BattleController, OwningController, *Widget))
    {
        CloseTutorialDialogue(false);
        return false;
    }

    DialogueContinued = OnContinued;
    RefreshDialogueGlyph();
    Widget->SetUserFocus(&OwningController);
    return true;
}

bool UReditusMovementPromptSubsystem::TrackBattleDialogue(AActor& BattleController,
    APlayerController& OwningController, UUserWidget& Dialogue)
{
    auto* HUD = ReditusMovementPrompt::FindLiveBattleHUD(BattleController, OwningController);
    RegisterInputObserver();
    if (!HUD || !InputObserver.IsValid() || !Dialogue.IsInViewport() ||
        Dialogue.GetWorld() != OwningController.GetWorld() ||
        (Dialogue.GetOwningPlayer() && Dialogue.GetOwningPlayer() != &OwningController))
    {
        return false;
    }
    if (DialogueHUD.Get() != HUD)
    {
        if (TutorialDialogue && TutorialDialogue != &Dialogue)
        {
            CloseTutorialDialogue(false);
        }
        BattleDialogues.Reset();
        ReleaseDialoguePause();
    }

    DialogueBattleController = &BattleController;
    DialoguePlayer = &OwningController;
    DialogueHUD = HUD;
    BattleDialogues.AddUnique(TWeakObjectPtr<UUserWidget>(&Dialogue));
    if (!SyncBattleDialoguePause())
    {
        BattleDialogues.Remove(TWeakObjectPtr<UUserWidget>(&Dialogue));
        UE_LOG(LogReditusTutorialPrompt, Warning, TEXT("Unable to pause the local battle for dialogue."));
        return false;
    }
    return true;
}

bool UReditusMovementPromptSubsystem::SyncBattleDialoguePause()
{
    const auto* BattleController = DialogueBattleController.Get();
    const auto* OwningController = DialoguePlayer.Get();
    if (!DialogueHUD.IsValid() || !IsValid(BattleController) || !IsValid(OwningController) ||
        ReditusMovementPrompt::FindLiveBattleHUD(*BattleController, *OwningController) != DialogueHUD.Get())
    {
        BattleDialogues.Reset();
        DialogueBattleController.Reset();
        DialoguePlayer.Reset();
        DialogueHUD.Reset();
        ReleaseDialoguePause();
        return false;
    }

    UWorld* World = OwningController->GetWorld();
    BattleDialogues.RemoveAll([World](const TWeakObjectPtr<UUserWidget>& Entry)
    {
        const auto* Widget = Entry.Get();
        return !IsValid(Widget) || !Widget->IsInViewport() || Widget->GetWorld() != World;
    });
    const bool bDialogueOpen = BattleDialogues.ContainsByPredicate([](const TWeakObjectPtr<UUserWidget>& Entry)
    {
        return ReditusMovementPrompt::IsDialogueVisible(*Entry.Get());
    });
    if (!bDialogueOpen)
    {
        ReleaseDialoguePause();
        return true;
    }
    if (!UGameplayStatics::IsGamePaused(World))
    {
        if (!UGameplayStatics::SetGamePaused(World, true))
        {
            return false;
        }
        PausedWorld = World;
    }
    return true;
}

void UReditusMovementPromptSubsystem::ReleaseDialoguePause()
{
    if (UWorld* World = PausedWorld.Get())
    {
        UGameplayStatics::SetGamePaused(World, false);
    }
    PausedWorld.Reset();
}

void UReditusMovementPromptSubsystem::RefreshDialogueGlyph()
{
    const auto* HUD = DialogueHUD.Get();
    if (!IsValid(TutorialDialogue) || !IsValid(HUD))
    {
        return;
    }
    const FKey Key = ReditusMovementPrompt::ConfirmKey(*HUD, bLastInputWasGamepad);
    if (DisplayedContinueKey != Key)
    {
        DisplayedContinueKey = Key;
        TutorialDialogue->SetContinueGlyph(HUD->GlyphTextures.FindRef(Key.GetFName()), ReditusMovementPrompt::BindingCaption(Key));
    }
}

bool UReditusMovementPromptSubsystem::HandleDialogueKey(const FKey& Key, bool bPressed, bool bRepeat)
{
    const auto* HUD = DialogueHUD.Get();
    if (!IsValid(TutorialDialogue) || !TutorialDialogue->IsInViewport() || !IsValid(HUD) ||
        !ReditusMovementPrompt::IsDialogueVisible(*TutorialDialogue))
    {
        return false;
    }
    if (bPressed)
    {
        if (!bRepeat && (Key == ReditusMovementPrompt::ConfirmKey(*HUD, Key.IsGamepadKey()) ||
            (!Key.IsGamepadKey() && Key == HUD->KeyboardAlternateConfirmKey)))
        {
            PressedContinueKey = Key;
        }
    }
    else if (Key == PressedContinueKey)
    {
        ContinueTutorialDialogue();
    }
    return true;
}

void UReditusMovementPromptSubsystem::ContinueTutorialDialogue()
{
    CloseTutorialDialogue(true);
}

void UReditusMovementPromptSubsystem::CloseTutorialDialogue(bool bContinued)
{
    if (!TutorialDialogue)
    {
        return;
    }

    const auto OnContinued = DialogueContinued;
    DialogueContinued.Unbind();
    auto* Widget = TutorialDialogue;
    TutorialDialogue = nullptr;
    BattleDialogues.Remove(TWeakObjectPtr<UUserWidget>(Widget));
    PressedContinueKey = FKey();
    DisplayedContinueKey = FKey();

    auto* OwningController = DialoguePlayer.Get();
    if (IsValid(Widget))
    {
        Widget->RemoveFromParent();
    }
    SyncBattleDialoguePause();

    if (IsValid(OwningController) && DialogueHUD.IsValid() && FSlateApplication::IsInitialized())
    {
        auto& Slate = FSlateApplication::Get();
        const auto Previous = PreviousDialogueFocus.Pin();
        FWidgetPath PreviousPath;
        if (Previous.IsValid() && Slate.GeneratePathToWidgetUnchecked(Previous.ToSharedRef(), PreviousPath, EVisibility::All))
        {
            Slate.SetUserFocus(GetLocalPlayer()->GetControllerId(), Previous.ToSharedRef(), EFocusCause::SetDirectly);
        }
        else
        {
            DialogueHUD->SetUserFocus(OwningController);
        }
    }
    PreviousDialogueFocus.Reset();

    if (bContinued)
    {
        OnContinued.ExecuteIfBound();
    }
}

void UReditusMovementPromptSubsystem::RecordDevice(bool bGamepad)
{
    bHasObservedDevice = true;
    if (bLastInputWasGamepad == bGamepad)
    {
        return;
    }

    bLastInputWasGamepad = bGamepad;
    RefreshDialogueGlyph();
    if (IsValid(ActivePrompt) && !bShowingBlockPrompt)
    {
        ActivePrompt->SetGlyph(bGamepad ? GamepadGlyph : KeyboardGlyph, bGamepad);
    }
}

bool UReditusMovementPromptSubsystem::HandleHeldBlockKey(const FKey& Key, bool bRepeat)
{
    auto* HUD = BlockBattleHUD.Get();
    if (!bHoldingBite || !IsValid(HUD) || !IsBlockBattleActive())
    {
        return false;
    }
    if (bRepeat || Key != ReditusMovementPrompt::BlockKey(*HUD, Key.IsGamepadKey()))
    {
        return true;
    }

    HUD->ObserveKey(Key);
    if (auto* Presenter = BlockBattleController->FindComponentByClass<UReditusBattleHUDPresenterComponent>())
    {
        Presenter->ForwardGameplayInput(HUD->InputActions.Block, Key);
    }
    if (!bHoldingBite)
    {
        return true;
    }

    // An early Block press may have left the Blueprint's cooldown running
    // when the world paused. Accept the bound key for this held hit only.
    const TWeakObjectPtr<AActor> Defender = HeldDefender;
    const auto* Defending = Defender.IsValid() ? FindFProperty<FBoolProperty>(Defender->GetClass(), TEXT("IsDefending")) : nullptr;
    if (Defending)
    {
        const bool bWasDefending = Defending->GetPropertyValue_InContainer(Defender.Get());
        Defending->SetPropertyValue_InContainer(Defender.Get(), true);
        NotifyBlockTutorialDefense(*Defender.Get());
        if (!bWasDefending && Defender.IsValid())
        {
            Defending->SetPropertyValue_InContainer(Defender.Get(), false);
        }
    }
    return true;
}

void UReditusMovementPromptSubsystem::TickPrompt()
{
    if (BlockBattleController.IsValid() && BlockPlayer.IsValid() &&
        BlockBattleController->GetWorld() == BlockPlayer->GetWorld() && !bBlockBattleAttached)
    {
        AttachBlockBattleHUD();
    }
    if (bBlockBattleAttached && !IsBlockBattleActive())
    {
        CancelBlockTutorial();
    }
    else if (bShowingBlockPrompt && (!BlockAttacker.IsValid() || !IsValid(ActivePrompt) || !ActivePrompt->IsInViewport() ||
        (bHoldingBite && (!HeldAnimation.IsValid() || !HeldDefender.IsValid() || !HeldBite.IsBound()))))
    {
        ReleaseBlockHold();
        BlockAttacker.Reset();
        EarlyBlockDefender.Reset();
        DismissPrompt();
    }

    if (BattleDialogues.Num() > 0 && !SyncBattleDialoguePause())
    {
        CloseTutorialDialogue(false);
    }
    if (TutorialDialogue && (!IsValid(TutorialDialogue) || !TutorialDialogue->IsInViewport() ||
        !ReditusMovementPrompt::IsDialogueVisible(*TutorialDialogue) || !DialogueHUD.IsValid()))
    {
        CloseTutorialDialogue(false);
    }
    TickFirstBattleTutorial();
    if (!ActivePrompt)
    {
        return;
    }
    const ULocalPlayer* LocalPlayer = GetLocalPlayer();
    if (!IsValid(ActivePrompt) || !ActivePrompt->IsInViewport() || !PromptWorld.IsValid()
        || !LocalPlayer || PromptWorld.Get() != LocalPlayer->GetWorld())
    {
        DismissPrompt();
        return;
    }
    if (bShowingBlockPrompt)
    {
        RefreshBlockGlyph();
    }
    else if (FPlatformTime::Seconds() >= DismissAt)
    {
        DismissPrompt();
    }
}

void UReditusMovementPromptSubsystem::DismissPrompt()
{
    if (IsValid(ActivePrompt))
    {
        ActivePrompt->RemoveFromParent();
    }
    ActivePrompt = nullptr;
    PromptWorld.Reset();
    bShowingBlockPrompt = false;
    DisplayedBlockKey = FKey();
}

void UReditusMovementPromptLibrary::ShowMovementTutorialPrompt(const UObject* WorldContextObject)
{
    APlayerController* Player = nullptr;
    if (auto* Subsystem = ReditusMovementPrompt::FindSubsystem(WorldContextObject, Player))
    {
        Subsystem->ShowPrompt(*Player);
    }
}

void UReditusMovementPromptLibrary::ArmBlockTutorial(AActor* BattleController)
{
    APlayerController* Player = nullptr;
    if (auto* Subsystem = ReditusMovementPrompt::FindSubsystem(BattleController, Player))
    {
        Subsystem->ArmBlockTutorial(*BattleController, *Player);
    }
}

void UReditusMovementPromptLibrary::ShowBlockTutorialPrompt(AActor* Attacker)
{
    APlayerController* Player = nullptr;
    if (auto* Subsystem = ReditusMovementPrompt::FindSubsystem(Attacker, Player))
    {
        Subsystem->ShowBlockTutorialPrompt(*Attacker, *Player);
    }
}

bool UReditusMovementPromptLibrary::HoldBlockTutorialBite(AActor* Attacker, AActor* Defender,
    UActorComponent* AttackAnimation, bool bDefenderIsDefending, FReditusHeldTutorialBite ApplyBite)
{
    APlayerController* Player = nullptr;
    auto* Subsystem = ReditusMovementPrompt::FindSubsystem(Attacker, Player);
    return Subsystem && IsValid(Defender) && IsValid(AttackAnimation)
        && Subsystem->HoldBlockTutorialBite(*Attacker, *Defender, *AttackAnimation, bDefenderIsDefending, ApplyBite);
}

void UReditusMovementPromptLibrary::NotifyBlockTutorialDefense(AActor* Defender)
{
    APlayerController* Player = nullptr;
    if (auto* Subsystem = ReditusMovementPrompt::FindSubsystem(Defender, Player))
    {
        Subsystem->NotifyBlockTutorialDefense(*Defender);
    }
}

bool UReditusMovementPromptLibrary::SaveGameToSlotWithTutorialProgress(const UObject* WorldContextObject,
    USaveGame* SaveGameObject, const FString& SlotName, int32 UserIndex)
{
    APlayerController* Player = nullptr;
    const auto* Subsystem = ReditusMovementPrompt::FindSubsystem(WorldContextObject, Player);
    const UWorld* World = IsValid(Player) ? Player->GetWorld() : nullptr;
    TArray<const FBoolProperty*> Updated;
    if (IsValid(SaveGameObject) && Subsystem && World)
    {
        for (uint8 Index = 0; Index < static_cast<uint8>(EReditusTutorialLesson::Count); ++Index)
        {
            const auto Lesson = static_cast<EReditusTutorialLesson>(Index);
            if (!Subsystem->ShouldSaveTutorialCompletion(*World, *SaveGameObject, Lesson))
            {
                continue;
            }
            const auto* Completed = ReditusMovementPrompt::FindTutorialCompletionFlag(SaveGameObject, Lesson);
            if (Completed && !Completed->GetPropertyValue_InContainer(SaveGameObject))
            {
                Completed->SetPropertyValue_InContainer(SaveGameObject, true);
                Updated.Add(Completed);
            }
        }
    }
    const bool bSaved = UGameplayStatics::SaveGameToSlot(SaveGameObject, SlotName, UserIndex);
    if (!bSaved)
    {
        for (const auto* Completed : Updated)
        {
            Completed->SetPropertyValue_InContainer(SaveGameObject, false);
        }
    }
    return bSaved;
}

int32 UReditusMovementPromptLibrary::PrepareFirstBattleAmpHit(AActor* /*BattleController*/, int32 CalculatedDamage,
    bool bWeakness, bool& bReduceStamina)
{
    bReduceStamina = bWeakness;
    return CalculatedDamage;
}

bool UReditusMovementPromptLibrary::AllowFirstBattleTutorialStagger(AActor* /*BattleController*/, AActor* /*Target*/,
    bool bPassedChanceRoll)
{
    return bPassedChanceRoll;
}

bool UReditusMovementPromptLibrary::ShowBattleTutorialDialogue(AActor* BattleController, const FText& Title,
    const FText& Message, FReditusBattleTutorialContinued OnContinued)
{
    APlayerController* Player = nullptr;
    auto* Subsystem = ReditusMovementPrompt::FindSubsystem(BattleController, Player);
    return Subsystem && Subsystem->ShowBattleTutorialDialogue(*BattleController, *Player, Title, Message, OnContinued);
}

bool UReditusMovementPromptLibrary::PauseBattleWhileDialogueIsOpen(AActor* BattleController, UUserWidget* Dialogue)
{
    APlayerController* Player = nullptr;
    auto* Subsystem = ReditusMovementPrompt::FindSubsystem(BattleController, Player);
    return Subsystem && IsValid(Dialogue) && Subsystem->TrackBattleDialogue(*BattleController, *Player, *Dialogue);
}
