#include "ReditusTutorialPrivate.h"

#include "../ATB/ReditusATBComponent.h"
#include "ReditusBattleHUDPresenterComponent.h"
#include "ReditusBattleHUDWidget.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "GameFramework/InputSettings.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerInput.h"
#include "GameFramework/SaveGame.h"
#include "HAL/PlatformTime.h"
#include "Kismet/GameplayStatics.h"
#include "UObject/UnrealType.h"

namespace ReditusFirstBattleTutorial
{
    constexpr double TargetResponseSeconds = 1.5;
    constexpr int32 GuideZOrder = 65;

    UObject* ReadObject(const UObject& Object, FName Name)
    {
        const auto* Field = FindFProperty<FObjectPropertyBase>(Object.GetClass(), Name);
        return Field ? Field->GetObjectPropertyValue_InContainer(&Object) : nullptr;
    }

    bool ReadBool(const UObject& Object, FName Name)
    {
        const auto* Field = FindFProperty<FBoolProperty>(Object.GetClass(), Name);
        return Field && Field->GetPropertyValue_InContainer(&Object);
    }

    int32 ReadInt(const UObject& Object, FName Name)
    {
        const auto* Field = FindFProperty<FIntProperty>(Object.GetClass(), Name);
        return Field ? Field->GetPropertyValue_InContainer(&Object) : 0;
    }

    bool IsSaved(const UWorld& World, EReditusTutorialLesson Lesson)
    {
        const auto* Save = ReditusMovementPrompt::FindStorySave(World);
        const auto* Flag = ReditusMovementPrompt::FindTutorialCompletionFlag(Save, Lesson);
        return Flag && Flag->GetPropertyValue_InContainer(Save);
    }

    TArray<FInputActionKeyMapping> Mappings(const APlayerController& Player, FName Action)
    {
        TArray<FInputActionKeyMapping> Keys;
        if (Player.PlayerInput)
        {
            Keys = Player.PlayerInput->GetKeysForAction(Action);
        }
        if (Keys.Num() == 0)
        {
            GetDefault<UInputSettings>()->GetActionMappingByName(Action, Keys);
        }
        return Keys;
    }

    FKey AmpKey(const UReditusBattleHUDWidget& HUD, const APlayerController& Player, bool bGamepad)
    {
        if (bGamepad)
        {
            return HUD.GamepadKeys.IncreaseAmp;
        }
        for (const auto& Mapping : Mappings(Player, HUD.InputActions.IncreaseAmp))
        {
            if (!Mapping.Key.IsGamepadKey())
            {
                return Mapping.Key;
            }
        }
        return FKey();
    }

    FKey AttackKey(const UReditusBattleHUDWidget& HUD, bool bGamepad)
    {
        if (!bGamepad)
        {
            if (const FKey* Shortcut = HUD.KeyboardShortcuts.Find(EReditusHUDCommand::Attack))
            {
                return *Shortcut;
            }
        }
        return ReditusMovementPrompt::ConfirmKey(HUD, bGamepad);
    }

    FKey OverdriveKey(const UReditusBattleHUDWidget& HUD, bool bGamepad)
    {
        const FKey* Shortcut = bGamepad ? &HUD.GamepadKeys.Overdrive : HUD.KeyboardShortcuts.Find(EReditusHUDCommand::Overdrive);
        return Shortcut && Shortcut->IsValid() ? *Shortcut : ReditusMovementPrompt::ConfirmKey(HUD, bGamepad);
    }

    FName TargetMove(const FKey& Key)
    {
        if (Key == EKeys::Left || Key == EKeys::A || Key == EKeys::Gamepad_DPad_Left || Key == EKeys::Gamepad_LeftStick_Left) return TEXT("MenuLeft");
        if (Key == EKeys::Right || Key == EKeys::D || Key == EKeys::Gamepad_DPad_Right || Key == EKeys::Gamepad_LeftStick_Right) return TEXT("MenuRight");
        if (Key == EKeys::Up || Key == EKeys::W || Key == EKeys::Gamepad_DPad_Up || Key == EKeys::Gamepad_LeftStick_Up) return TEXT("MenuUp");
        if (Key == EKeys::Down || Key == EKeys::S || Key == EKeys::Gamepad_DPad_Down || Key == EKeys::Gamepad_LeftStick_Down) return TEXT("MenuDown");
        return NAME_None;
    }
}

void UReditusMovementPromptSubsystem::ArmFirstBattleTutorial(AActor& BattleController, APlayerController& OwningController)
{
    using namespace ReditusFirstBattleTutorial;
    if (IsSaved(*BattleController.GetWorld(), EReditusTutorialLesson::AmpAttack) ||
        (GuidedBattleController.Get() == &BattleController &&
            (GuidedTurn != EGuidedTurn::Inactive || (CompletedGuidedHUD.IsValid() && CompletedGuidedHUD->IsInViewport()))))
    {
        return;
    }
    ResetFirstBattleTutorial();
    GuidedBattleController = &BattleController;
    GuidedPlayer = &OwningController;
    GuidedTurn = EGuidedTurn::WaitForTurn;
}

void UReditusMovementPromptSubsystem::TickFirstBattleTutorial()
{
    using namespace ReditusFirstBattleTutorial;
    const auto* LocalPlayer = GetLocalPlayer();
    UWorld* World = LocalPlayer ? LocalPlayer->GetWorld() : nullptr;
    if (!World)
    {
        return;
    }
    auto* Controller = GuidedBattleController.Get();
    auto* OwningController = GuidedPlayer.Get();
    if (GuidedTurn == EGuidedTurn::Inactive)
    {
        if (auto* LocalController = UGameplayStatics::GetPlayerController(World, 0))
        {
            TickOverdriveTutorial(*LocalController);
        }
        return;
    }
    if (!IsValid(Controller) || !IsValid(OwningController) || Controller->GetWorld() != World)
    {
        ResetFirstBattleTutorial();
        return;
    }
    auto* HUD = ReditusMovementPrompt::FindLiveBattleHUD(*Controller, *OwningController);
    const bool bBattleOver = ReadBool(*Controller, TEXT("isBattleOver"));
    if (GuidedHUD.IsValid() && (GuidedHUD.Get() != HUD || bBattleOver))
    {
        ResetFirstBattleTutorial();
        return;
    }
    if (!HUD || bBattleOver || TutorialDialogue || bShowingBlockPrompt)
    {
        return;
    }

    auto* ATB = Controller->FindComponentByClass<UReditusATBComponent>();
    if (!ATB || !ATB->IsATBRunning())
    {
        if (GuidedHUD.IsValid() && GuidedTurn != EGuidedTurn::WaitForTurn)
        {
            ResetFirstBattleTutorial();
        }
        return;
    }
    auto* Unit = GuidedUnit.Get();
    if (GuidedTurn == EGuidedTurn::WaitForTurn)
    {
        Unit = HUD->GetSelectedUnit();
        if (!IsValid(Unit) || HUD->IsTargetSelectionActive() ||
            !HUD->CanExecuteCommand(EReditusHUDCommand::Attack) || !HUD->CanExecuteCommand(EReditusHUDCommand::IncreaseAmp))
        {
            return;
        }
        GuidedHUD = HUD;
        GuidedUnit = Unit;
        bGuidedHUDWasSuspended = HUD->bInputSuspended;
        HUD->bInputSuspended = true;
        GuidedTurn = EGuidedTurn::Amp;
    }
    else if (GuidedTurn == EGuidedTurn::OpeningTargets)
    {
        if (HUD->IsTargetSelectionActive())
        {
            GuidedTurn = EGuidedTurn::Target;
        }
        else if (FPlatformTime::Seconds() >= GuidedTargetsDeadline)
        {
            GuidedTurn = EGuidedTurn::Attack;
        }
        else
        {
            return;
        }
    }
    else if (GuidedTurn == EGuidedTurn::ConfirmingTarget)
    {
        if (IsValid(Unit) && CaptureGuidedAttack(*ATB, *Unit))
        {
            HUD->bInputSuspended = bGuidedHUDWasSuspended;
            return;
        }
        if (FPlatformTime::Seconds() < GuidedTargetsDeadline)
        {
            return;
        }
        GuidedTurn = HUD->IsTargetSelectionActive() ? EGuidedTurn::Target : EGuidedTurn::Attack;
    }
    else if (GuidedTurn == EGuidedTurn::Resolving)
    {
        auto* Target = GuidedTarget.Get();
        FReditusATBAction Pending;
        const bool bActionPending = IsValid(Unit) && ATB->GetPendingAction(Unit, Pending) &&
            Pending.ActionToken == GuidedActionToken;
        const bool bTargetIsStaggered = IsValid(Target) && ATB->IsUnitStaggered(Target);
        if (bActionPending && !bGuidedAttackStarted)
        {
            const auto Phase = ATB->GetUnitState(Unit).Phase;
            bGuidedAttackStarted = Phase == EReditusATBPhase::Executing || Phase == EReditusATBPhase::CycleEnd;
        }
        if (bGuidedAttackStarted && bTargetIsStaggered && !bGuidedTargetWasStaggered)
        {
            const int32 MaximumStamina = ReadInt(*Target, TEXT("MaxStamina"));
            if (ShowBattleTutorialDialogue(*Controller, *OwningController,
                NSLOCTEXT("ReditusBattleTutorial", "StaggerTitle", "Stagger"),
                NSLOCTEXT("ReditusBattleTutorial", "StaggerBody",
                    "Enemy Stamina shows how close an enemy is to being staggered. Hit its weakness or land a critical hit to drain Stamina. When the bar empties, the enemy staggers.\n\nFor 10 seconds, it can't attack and every hit against it is critical."),
                FReditusBattleTutorialContinued(), false, MaximumStamina > 0 ? MaximumStamina : 3))
            {
                RecordTutorialCompletion(*World, EReditusTutorialLesson::AmpAttack);
                CompletedGuidedHUD = HUD;
                GuidedTurn = EGuidedTurn::Inactive;
                return;
            }
        }
        bGuidedTargetWasStaggered = bTargetIsStaggered;
        if (bActionPending)
        {
            return;
        }
        GuidedTarget.Reset();
        GuidedActionToken = INDEX_NONE;
        if (bGuidedAttackStarted)
        {
            // A resolved attack can miss or defeat its target. The fight can carry on.
            CompletedGuidedHUD = HUD;
            GuidedTurn = EGuidedTurn::Inactive;
        }
        else
        {
            GuidedTurn = EGuidedTurn::WaitForTurn;
        }
        return;
    }

    if (GuidedTurn == EGuidedTurn::Target && !HUD->IsTargetSelectionActive())
    {
        GuidedTurn = EGuidedTurn::Attack;
    }

    if (!IsValid(Unit) || HUD->GetSelectedUnit() != Unit || !ATB->CanIssueCommands(Unit))
    {
        if (GuidedTurn == EGuidedTurn::Overdrive)
        {
            ResetFirstBattleTutorial();
            return;
        }
        HideGuidedPrompt();
        HUD->bInputSuspended = bGuidedHUDWasSuspended;
        GuidedTurn = EGuidedTurn::WaitForTurn;
        return;
    }
    if (!ShowGuidedPrompt() && !UGameplayStatics::IsGamePaused(World))
    {
        ResetFirstBattleTutorial();
    }
}

bool UReditusMovementPromptSubsystem::ShowGuidedPrompt()
{
    using namespace ReditusFirstBattleTutorial;
    auto* HUD = GuidedHUD.Get();
    auto* OwningController = GuidedPlayer.Get();
    if (!IsValid(HUD) || !IsValid(OwningController) || !OwningController->GetWorld())
    {
        return false;
    }
    const bool bGamepad = bHasObservedDevice ? bLastInputWasGamepad : HUD->GetInputDevice() == EReditusHUDInputDevice::Xbox;
    const bool bTarget = GuidedTurn == EGuidedTurn::Target;
    const bool bOverdrive = GuidedTurn == EGuidedTurn::Overdrive;
    FKey Binding = AttackKey(*HUD, bGamepad);
    if (GuidedTurn == EGuidedTurn::Amp)
    {
        Binding = AmpKey(*HUD, *OwningController, bGamepad);
    }
    else if (bTarget)
    {
        Binding = ReditusMovementPrompt::ConfirmKey(*HUD, bGamepad);
    }
    else if (bOverdrive)
    {
        Binding = OverdriveKey(*HUD, bGamepad);
    }
    if (!GuidedPrompt)
    {
        GuidedPrompt = CreateWidget<UReditusMovementPromptWidget>(OwningController);
        if (!IsValid(GuidedPrompt))
        {
            return false;
        }
        GuidedPrompt->ContinueRequested.BindUObject(this, &UReditusMovementPromptSubsystem::ConfirmGuidedPrompt);
        GuidedPrompt->PreviousTargetRequested.BindUObject(this, &UReditusMovementPromptSubsystem::MoveGuidedTarget, -1);
        GuidedPrompt->NextTargetRequested.BindUObject(this, &UReditusMovementPromptSubsystem::MoveGuidedTarget, 1);
    }
    if (DisplayedGuidedTurn != GuidedTurn || Binding != GuidedBinding)
    {
        auto* Texture = HUD->GlyphTextures.FindRef(Binding.GetFName());
        const FText KeyLabel = ReditusMovementPrompt::BindingCaption(Binding);
        if (bOverdrive)
        {
            GuidedPrompt->SetDialogue(
                NSLOCTEXT("ReditusBattleTutorial", "OverdriveTitle", "Overdrive ready"),
                NSLOCTEXT("ReditusBattleTutorial", "OverdriveBody",
                    "Taking damage charges Overdrive, even when you block. When the gauge fills, choose Overdrive to use a special attack. Some Overdrives require other party members to be in Overdrive too.\n\nSpending AMP on any skill or Overdrive automatically increases its power."),
                false, NSLOCTEXT("ReditusBattleTutorial", "ChooseOverdrive", "Select Overdrive"));
            GuidedPrompt->SetContinueGlyph(Texture, KeyLabel);
        }
        else
        {
            FText Caption = NSLOCTEXT("ReditusBattleTutorial", "ChooseAttack", "Attack");
            if (GuidedTurn == EGuidedTurn::Amp)
            {
                Caption = NSLOCTEXT("ReditusBattleTutorial", "IncreaseAmp", "Increase AMP");
            }
            else if (bTarget)
            {
                Caption = NSLOCTEXT("ReditusBattleTutorial", "ChooseTarget", "Select a target");
            }
            GuidedPrompt->SetGuidedPrompt(Caption, Texture, KeyLabel, bTarget);
        }
        DisplayedGuidedTurn = GuidedTurn;
        GuidedBinding = Binding;
    }
    if (!GuidedPrompt->IsInViewport())
    {
        if (!GuidedPrompt->AddToPlayerScreen(GuideZOrder))
        {
            return false;
        }
        GuidedPrompt->SetUserFocus(OwningController);
    }
    UWorld* World = OwningController->GetWorld();
    if (!UGameplayStatics::IsGamePaused(World))
    {
        if (!UGameplayStatics::SetGamePaused(World, true))
        {
            return false;
        }
        GuidedPauseWorld = World;
    }
    return true;
}

void UReditusMovementPromptSubsystem::ReleaseGuidedPause()
{
    if (UWorld* World = GuidedPauseWorld.Get())
    {
        GuidedPauseWorld.Reset();
        UGameplayStatics::SetGamePaused(World, false);
    }
}

void UReditusMovementPromptSubsystem::HideGuidedPrompt()
{
    if (IsValid(GuidedPrompt))
    {
        GuidedPrompt->RemoveFromParent();
    }
    GuidedPrompt = nullptr;
    DisplayedGuidedTurn = EGuidedTurn::Inactive;
    GuidedBinding = FKey();
    ReleaseGuidedPause();
}

void UReditusMovementPromptSubsystem::ResetFirstBattleTutorial()
{
    HideGuidedPrompt();
    if (IsGuidedInputLocked())
    {
        GuidedHUD->bInputSuspended = bGuidedHUDWasSuspended;
    }
    GuidedTurn = EGuidedTurn::Inactive;
    GuidedHUD.Reset();
    GuidedUnit.Reset();
    GuidedTarget.Reset();
    GuidedBattleController.Reset();
    GuidedPlayer.Reset();
    GuidedHeldKeys.Reset();
    GuidedActionToken = INDEX_NONE;
    bGuidedAttackStarted = false;
    bGuidedTargetWasStaggered = false;
}

bool UReditusMovementPromptSubsystem::IsGuidedInputLocked() const
{
    return GuidedHUD.IsValid() && GuidedTurn != EGuidedTurn::Inactive &&
        GuidedTurn != EGuidedTurn::WaitForTurn && GuidedTurn != EGuidedTurn::Resolving;
}

bool UReditusMovementPromptSubsystem::HandleGuidedKey(const FKey& Key, bool bPressed, bool bRepeat)
{
    using namespace ReditusFirstBattleTutorial;
    if (!bPressed)
    {
        return GuidedHeldKeys.Remove(Key) > 0;
    }
    const bool bWasInactive = GuidedTurn == EGuidedTurn::Inactive;
    if (GuidedTurn == EGuidedTurn::WaitForTurn || bWasInactive)
    {
        TickFirstBattleTutorial();
    }
    if (GuidedHeldKeys.Contains(Key))
    {
        return true;
    }
    if (!IsGuidedInputLocked())
    {
        return false;
    }
    if (bRepeat)
    {
        return true;
    }
    GuidedHeldKeys.Add(Key);
    if (bWasInactive)
    {
        // A key that encounters a newly ready Overdrive opens the card first.
        return true;
    }
    auto* HUD = GuidedHUD.Get();
    const auto* OwningController = GuidedPlayer.Get();
    if (!IsValid(GuidedPrompt) || !IsValid(OwningController))
    {
        return true;
    }

    const bool bGamepad = Key.IsGamepadKey();
    HUD->ObserveKey(Key);
    const bool bConfirm = Key == ReditusMovementPrompt::ConfirmKey(*HUD, bGamepad) ||
        (!bGamepad && Key == HUD->KeyboardAlternateConfirmKey);
    if (GuidedTurn == EGuidedTurn::Amp && (Key == AmpKey(*HUD, *OwningController, bGamepad) ||
        (!bGamepad && Mappings(*OwningController, HUD->InputActions.IncreaseAmp).ContainsByPredicate(
            [Key](const FInputActionKeyMapping& Mapping) { return Key == Mapping.Key; }))))
    {
        ExecuteGuidedInput(NAME_None, Key);
    }
    else if (GuidedTurn == EGuidedTurn::Attack && (Key == AttackKey(*HUD, bGamepad) || bConfirm))
    {
        ExecuteGuidedInput(NAME_None, Key);
    }
    else if (GuidedTurn == EGuidedTurn::Overdrive && Key == OverdriveKey(*HUD, bGamepad))
    {
        ExecuteGuidedInput(NAME_None, Key);
    }
    else if (GuidedTurn == EGuidedTurn::Target)
    {
        const FName Movement = TargetMove(Key);
        if (bConfirm || !Movement.IsNone())
        {
            ExecuteGuidedInput(Movement, Key);
        }
    }
    return true;
}

void UReditusMovementPromptSubsystem::ConfirmGuidedPrompt()
{
    ExecuteGuidedInput(NAME_None, GuidedBinding);
}

void UReditusMovementPromptSubsystem::MoveGuidedTarget(int32 Direction)
{
    if (GuidedTurn == EGuidedTurn::Target)
    {
        ExecuteGuidedInput(Direction < 0 ? FName(TEXT("MenuLeft")) : FName(TEXT("MenuRight")),
            Direction < 0 ? EKeys::Left : EKeys::Right);
    }
}

void UReditusMovementPromptSubsystem::ExecuteGuidedInput(FName Movement, FKey Key)
{
    using namespace ReditusFirstBattleTutorial;
    auto* HUD = GuidedHUD.Get();
    auto* Controller = GuidedBattleController.Get();
    auto* Unit = GuidedUnit.Get();
    UWorld* World = GuidedPauseWorld.Get();
    if (!World || !IsValid(HUD) || !IsValid(Controller) || !IsValid(Unit) || bHoldingBite || TutorialDialogue)
    {
        return;
    }
    auto* Presenter = Controller->FindComponentByClass<UReditusBattleHUDPresenterComponent>();
    auto* ATB = Controller->FindComponentByClass<UReditusATBComponent>();
    if (!Presenter || !ATB || HUD->GetSelectedUnit() != Unit || !ATB->CanIssueCommands(Unit))
    {
        return;
    }

    // Run exactly one existing route while the world is unpaused. No world tick
    // passes between unpausing and re-pausing for the next prompt.
    ReleaseGuidedPause();
    HUD->bInputSuspended = bGuidedHUDWasSuspended;
    if (GuidedTurn == EGuidedTurn::Amp)
    {
        const int32 Before = ReadInt(*Unit, TEXT("CurrentAmpPointsInPlay"));
        HUD->RequestCommand(EReditusHUDCommand::IncreaseAmp);
        if (IsValid(Unit) && ReadInt(*Unit, TEXT("CurrentAmpPointsInPlay")) > Before)
        {
            GuidedTurn = EGuidedTurn::Attack;
        }
    }
    else if (GuidedTurn == EGuidedTurn::Attack)
    {
        HUD->SetAlternateCommands(false);
        if (!HUD->IsTargetSelectionActive() && HUD->RequestCommand(EReditusHUDCommand::Attack))
        {
            GuidedTurn = HUD->IsTargetSelectionActive() ? EGuidedTurn::Target : EGuidedTurn::OpeningTargets;
            GuidedTargetsDeadline = FPlatformTime::Seconds() + TargetResponseSeconds;
        }
    }
    else if (GuidedTurn == EGuidedTurn::Target && HUD->IsTargetSelectionActive())
    {
        if (!Movement.IsNone())
        {
            Presenter->ForwardGameplayInput(Movement, Key);
        }
        else if (HUD->RequestCommand(EReditusHUDCommand::Attack))
        {
            if (!CaptureGuidedAttack(*ATB, *Unit))
            {
                GuidedTurn = EGuidedTurn::ConfirmingTarget;
                GuidedTargetsDeadline = FPlatformTime::Seconds() + TargetResponseSeconds;
            }
        }
    }
    else if (GuidedTurn == EGuidedTurn::Overdrive)
    {
        // Release the tutorial's focused widget before the command opens its
        // own menu. The command can then assign focus to its final destination.
        HideGuidedPrompt();
        HUD->SetUserFocus(GuidedPlayer.Get());
        const bool bSelected = HUD->RequestCommand(EReditusHUDCommand::Overdrive);
        if (bSelected && IsValid(World))
        {
            RecordTutorialCompletion(*World, EReditusTutorialLesson::Overdrive);
        }
        if (bSelected || !IsValid(HUD) || !HUD->CanExecuteCommand(EReditusHUDCommand::Overdrive))
        {
            GuidedTurn = EGuidedTurn::Inactive;
        }
    }

    if (!IsValid(HUD) || !HUD->IsInViewport())
    {
        ResetFirstBattleTutorial();
        return;
    }
    HUD->bInputSuspended = bGuidedHUDWasSuspended || IsGuidedInputLocked();
    if (GuidedTurn == EGuidedTurn::Inactive || GuidedTurn == EGuidedTurn::Resolving || GuidedTurn == EGuidedTurn::OpeningTargets ||
        GuidedTurn == EGuidedTurn::ConfirmingTarget)
    {
        HideGuidedPrompt();
        if (!HUD->IsMenuOpen())
        {
            HUD->SetUserFocus(GuidedPlayer.Get());
        }
    }
    else
    {
        ShowGuidedPrompt();
    }
}

bool UReditusMovementPromptSubsystem::CaptureGuidedAttack(const UReditusATBComponent& ATB, AActor& Unit)
{
    FReditusATBAction Pending;
    if (!ATB.GetPendingAction(&Unit, Pending) || Pending.Kind != EReditusATBActionKind::Attack ||
        Pending.Targets.Num() != 1 || !IsValid(Pending.Targets[0]))
    {
        return false;
    }
    GuidedActionToken = Pending.ActionToken;
    GuidedTarget = Pending.Targets[0];
    bGuidedAttackStarted = false;
    bGuidedTargetWasStaggered = ATB.IsUnitStaggered(Pending.Targets[0]);
    GuidedTurn = EGuidedTurn::Resolving;
    return true;
}

void UReditusMovementPromptSubsystem::TickOverdriveTutorial(APlayerController& OwningController)
{
    using namespace ReditusFirstBattleTutorial;
    UWorld* World = OwningController.GetWorld();
    if (!World || TutorialDialogue || bShowingBlockPrompt || UGameplayStatics::IsGamePaused(World) ||
        IsSaved(*World, EReditusTutorialLesson::Overdrive))
    {
        return;
    }
    const auto& Completion = Completions[static_cast<uint8>(EReditusTutorialLesson::Overdrive)];
    const auto* StorySave = ReditusMovementPrompt::FindStorySave(*World);
    if (Completion.bHadSave ? Completion.Save.IsValid() && Completion.Save.Get() == StorySave : Completion.World.Get() == World)
    {
        return;
    }
    auto* Controller = Cast<AActor>(ReadObject(OwningController, TEXT("battleController")));
    if (!IsValid(Controller) || ReadBool(*Controller, TEXT("isBattleOver")))
    {
        return;
    }
    auto* HUD = ReditusMovementPrompt::FindLiveBattleHUD(*Controller, OwningController);
    auto* Unit = HUD ? HUD->GetSelectedUnit() : nullptr;
    auto* ATB = Controller->FindComponentByClass<UReditusATBComponent>();
    if (!IsValid(Unit) || !ATB || !ATB->IsATBRunning() || !ATB->CanIssueCommands(Unit) ||
        !HUD->CanExecuteCommand(EReditusHUDCommand::Overdrive))
    {
        return;
    }

    GuidedBattleController = Controller;
    GuidedPlayer = &OwningController;
    GuidedHUD = HUD;
    GuidedUnit = Unit;
    GuidedTurn = EGuidedTurn::Overdrive;
    bGuidedHUDWasSuspended = HUD->bInputSuspended;
    HUD->bInputSuspended = true;
    if (!ShowGuidedPrompt())
    {
        ResetFirstBattleTutorial();
    }
}
