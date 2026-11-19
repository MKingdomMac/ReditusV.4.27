#include "ReditusSequenceSkip.h"
#include "ReditusQuestNPCReflection.h"
#include "LevelSequenceActor.h"
#include "LevelSequencePlayer.h"
#include "MovieSceneSequencePlayer.h"
#include "Engine/GameInstance.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Framework/Application/IInputProcessor.h"
#include "Framework/Application/SlateApplication.h"
#include "InputCoreTypes.h"
#include "HAL/PlatformTime.h"
#include "Blueprint/WidgetTree.h"
#include "Brushes/SlateColorBrush.h"
#include "Components/Border.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/TextBlock.h"
#include "Components/ProgressBar.h"
#include "Components/SizeBox.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/AudioComponent.h"
#include "UObject/UnrealType.h"
#include "Styling/CoreStyle.h"
#include "UnrealClient.h"

namespace
{
    ALevelSequenceActor* ControllerSequenceActor(const APlayerController* Controller)
    {
        if (!IsValid(Controller)) return nullptr;
        auto* Actor = Cast<ALevelSequenceActor>(ReditusQuestNPCReflection::ReadObject(
            const_cast<APlayerController*>(Controller), TEXT("Current Sequence ")));
        return IsValid(Actor) && Actor->GetWorld() == Controller->GetWorld() ? Actor : nullptr;
    }

    bool SkipKey(const FKey& Key)
    {
        return Key == EKeys::LeftShift || Key == EKeys::RightShift || Key == EKeys::Gamepad_RightTrigger;
    }

    class FSequenceSkipInput : public IInputProcessor
    {
        TWeakObjectPtr<UReditusSequenceSkipSubsystem> Owner;
    public:
        explicit FSequenceSkipInput(UReditusSequenceSkipSubsystem* System) : Owner(System) {}
        virtual void Tick(const float DeltaTime, FSlateApplication& App, TSharedRef<ICursor> Cursor) override
        { (void)DeltaTime; (void)App; (void)Cursor; }
        virtual bool HandleKeyDownEvent(FSlateApplication& App, const FKeyEvent& Event) override
        { (void)App; return Owner.IsValid() && Owner->HandleKey(Event.GetKey(), true, Event.IsRepeat()); }
        virtual bool HandleKeyUpEvent(FSlateApplication& App, const FKeyEvent& Event) override
        { (void)App; return Owner.IsValid() && Owner->HandleKey(Event.GetKey(), false); }
        virtual bool HandleAnalogInputEvent(FSlateApplication& App, const FAnalogInputEvent& Event) override
        {
            (void)App;
            return Owner.IsValid() && Event.GetKey() == EKeys::Gamepad_RightTriggerAxis
                && Owner->HandleTrigger(Event.GetAnalogValue());
        }
    };
}

void UReditusSequenceSkipNotice::SetMessage(const FText& Message)
{
    Notice = Message;
    HoldProgress = -1.f;
    if (Caption) Caption->SetText(Notice);
    if (ProgressBar) ProgressBar->SetVisibility(ESlateVisibility::Collapsed);
}

void UReditusSequenceSkipNotice::SetProgress(float Progress)
{
    HoldProgress = FMath::Clamp(Progress, 0.f, 1.f);
    Notice = FText::Format(NSLOCTEXT("ReditusSequence", "HoldingSkip", "Skipping... {0}%"),
        FText::AsNumber(FMath::RoundToInt(HoldProgress * 100.f)));
    if (Caption) Caption->SetText(Notice);
    if (ProgressBar)
    {
        ProgressBar->SetPercent(HoldProgress);
        ProgressBar->SetVisibility(ESlateVisibility::HitTestInvisible);
    }
}

TSharedRef<SWidget> UReditusSequenceSkipNotice::RebuildWidget()
{
    if (!WidgetTree) WidgetTree = NewObject<UWidgetTree>(this);
    auto* Canvas = WidgetTree->ConstructWidget<UCanvasPanel>();
    WidgetTree->RootWidget = Canvas;
    auto* Border = WidgetTree->ConstructWidget<UBorder>();
    Border->SetBrush(FSlateColorBrush(FLinearColor(.87f, .64f, .22f, .75f)));
    Border->SetPadding(FMargin(1));
    auto* Glass = WidgetTree->ConstructWidget<UBorder>();
    Glass->SetBrush(FSlateColorBrush(FLinearColor(.014f, .025f, .042f, .86f)));
    Glass->SetPadding(FMargin(24, 14));
    Border->SetContent(Glass);
    auto* Rows = WidgetTree->ConstructWidget<UVerticalBox>();
    Glass->SetContent(Rows);
    Caption = WidgetTree->ConstructWidget<UTextBlock>();
    Caption->SetText(Notice);
    Caption->SetFont(FCoreStyle::GetDefaultFontStyle(TEXT("Bold"), 19));
    Caption->SetColorAndOpacity(FSlateColor(FLinearColor(1.f, .95f, .82f, 1.f)));
    Caption->SetJustification(ETextJustify::Center);
    Rows->AddChildToVerticalBox(Caption)->SetPadding(FMargin(0, 0, 0, 6));
    auto* BarBounds = WidgetTree->ConstructWidget<USizeBox>();
    BarBounds->SetWidthOverride(320);
    BarBounds->SetHeightOverride(6);
    ProgressBar = WidgetTree->ConstructWidget<UProgressBar>();
    FProgressBarStyle BarStyle;
    BarStyle.SetBackgroundImage(FSlateColorBrush(FLinearColor(.08f, .14f, .18f, .65f)));
    BarStyle.SetFillImage(FSlateColorBrush(FLinearColor(1.f, .78f, .32f, 1.f)));
    ProgressBar->WidgetStyle = BarStyle;
    ProgressBar->SetFillColorAndOpacity(FLinearColor::White);
    ProgressBar->SetPercent(FMath::Max(0.f, HoldProgress));
    ProgressBar->SetVisibility(HoldProgress >= 0 ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
    BarBounds->SetContent(ProgressBar);
    Rows->AddChildToVerticalBox(BarBounds);
    auto* NoticeSlot = Canvas->AddChildToCanvas(Border);
    NoticeSlot->SetAnchors(FAnchors(.5f, .66f));
    NoticeSlot->SetAlignment(FVector2D(.5f, .5f));
    NoticeSlot->SetAutoSize(true);
    SetVisibility(ESlateVisibility::HitTestInvisible);
    return Super::RebuildWidget();
}

void UReditusSequenceSkipSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);
    bStopped = false;
    LastTime = FPlatformTime::Seconds();
    RegisterInput();
}

void UReditusSequenceSkipSubsystem::RegisterInput()
{
    if (InputProcessor.IsValid() || !FSlateApplication::IsInitialized()) return;
    InputProcessor = MakeShared<FSequenceSkipInput>(this);
    FSlateApplication::Get().RegisterInputPreProcessor(InputProcessor, 0);
}

void UReditusSequenceSkipSubsystem::Deinitialize()
{
    bStopped = true;
    ClearSequence();
    if (NoticeWidget) NoticeWidget->RemoveFromParent();
    NoticeWidget = nullptr;
    if (InputProcessor.IsValid() && FSlateApplication::IsInitialized())
        FSlateApplication::Get().UnregisterInputPreProcessor(InputProcessor);
    InputProcessor.Reset();
    Super::Deinitialize();
}

bool UReditusSequenceSkipSubsystem::IsTickable() const
{
    return !bStopped && !HasAnyFlags(RF_ClassDefaultObject);
}

UWorld* UReditusSequenceSkipSubsystem::GetTickableGameObjectWorld() const
{
    return GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
}

TStatId UReditusSequenceSkipSubsystem::GetStatId() const
{
    RETURN_QUICK_DECLARE_CYCLE_STAT(UReditusSequenceSkipSubsystem, STATGROUP_Tickables);
}

bool UReditusSequenceSkipSubsystem::Configure(ALevelSequenceActor* Actor, bool AllowSkip, FReditusSequenceSkipHandoff Handoff)
{
    if (!IsValid(Actor) || Actor->GetWorld() != GetTickableGameObjectWorld() || !Actor->GetSequencePlayer())
    {
        UE_LOG(LogTemp, Warning, TEXT("Configure Level Sequence Skipping needs a valid Sequence Actor in this level."));
        return false;
    }
    auto* Controller = UGameplayStatics::GetPlayerController(GetTickableGameObjectWorld(), 0);
    if (HasActiveSequence() && SequenceActor != Actor && ControllerSequenceActor(Controller) != Actor)
        return false;
    ClearSequence();
    SequenceActor = Actor;
    SequencePlayer = Actor->GetSequencePlayer();
    OnSkipped = Handoff;
    bAllowSkip = AllowSkip && OnSkipped.IsBound();
    bNeedTriggerRelease = LastTriggerValue > .2f || (IsValid(Controller)
        && Controller->GetInputAnalogKeyState(EKeys::Gamepad_RightTriggerAxis) > .2f);
    if (IsValid(Controller) && Controller->IsLocalController()) Controller->bShowMouseCursor = false;
    SequencePlayer->OnFinished.AddUniqueDynamic(this, &UReditusSequenceSkipSubsystem::SequenceEnded);
    SequencePlayer->OnStop.AddUniqueDynamic(this, &UReditusSequenceSkipSubsystem::SequenceEnded);
    if (AllowSkip && !OnSkipped.IsBound())
        UE_LOG(LogTemp, Warning, TEXT("Configure Level Sequence Skipping needs an On Skipped handoff before Allow Skip can be enabled."));
    RegisterInput();
    return !AllowSkip || bAllowSkip;
}

bool UReditusSequenceSkipSubsystem::HasActiveSequence() const
{
    return IsValid(SequenceActor) && SequenceActor->GetWorld() == GetTickableGameObjectWorld()
        && IsValid(SequencePlayer) && (SequencePlayer->IsPlaying() || SequencePlayer->IsPaused());
}

bool UReditusSequenceSkipSubsystem::CanCapture() const
{
    if (!HasActiveSequence() || !FSlateApplication::IsInitialized() || !FSlateApplication::Get().IsActive()) return false;
    auto* Player = UGameplayStatics::GetPlayerController(GetTickableGameObjectWorld(), 0);
    if (FindSequencePlayer(Player) != SequencePlayer) return false;
    auto* Viewport = GetGameInstance()->GetGameViewportClient();
    if (Viewport && Viewport->Viewport && Viewport->Viewport->HasFocus()) return true;
    auto* Dialogue = Cast<UUserWidget>(ReditusQuestNPCReflection::ReadObject(Player, TEXT("DialogueWidget")));
    return IsValid(Dialogue) && Dialogue->IsInViewport()
        && (Dialogue->HasUserFocus(Player) || Dialogue->HasUserFocusedDescendants(Player));
}

void UReditusSequenceSkipSubsystem::ClearSequence()
{
    ClearDialogueObservation();
    if (NoticeWidget) NoticeWidget->RemoveFromParent();
    NoticeWidget = nullptr;
    NoticeSeconds = 0;
    bShowingProgress = false;
    if (IsValid(SequencePlayer))
    {
        SequencePlayer->OnFinished.RemoveDynamic(this, &UReditusSequenceSkipSubsystem::SequenceEnded);
        SequencePlayer->OnStop.RemoveDynamic(this, &UReditusSequenceSkipSubsystem::SequenceEnded);
    }
    SequenceActor = nullptr;
    SequencePlayer = nullptr;
    OnSkipped.Unbind();
    SetAutoAdvance(nullptr, false);
    bAllowSkip = false;
    bKeyHeld = bTriggerHeld = bMouseHeld = false;
    bNeedTriggerRelease = LastTriggerValue > .2f;
    HeldSeconds = 0;
}

void UReditusSequenceSkipSubsystem::SequenceEnded()
{
    ClearSequence();
}

void UReditusSequenceSkipSubsystem::ShowBlockedNotice()
{
    if (!NoticeWidget)
    {
        auto* Player = UGameplayStatics::GetPlayerController(GetTickableGameObjectWorld(), 0);
        NoticeWidget = Player ? CreateWidget<UReditusSequenceSkipNotice>(Player) : nullptr;
        if (NoticeWidget)
        {
            NoticeWidget->AddToViewport(1000);
        }
    }
    if (NoticeWidget) NoticeWidget->SetMessage(NSLOCTEXT("ReditusSequence", "SkipBlocked", "This scene can't be skipped."));
    bShowingProgress = false;
    NoticeSeconds = 2;
}

void UReditusSequenceSkipSubsystem::ShowHoldProgress(float Progress)
{
    if (!NoticeWidget)
    {
        auto* Player = UGameplayStatics::GetPlayerController(GetTickableGameObjectWorld(), 0);
        NoticeWidget = Player ? CreateWidget<UReditusSequenceSkipNotice>(Player) : nullptr;
        if (NoticeWidget) NoticeWidget->AddToViewport(1000);
    }
    if (NoticeWidget) NoticeWidget->SetProgress(Progress);
    bShowingProgress = true;
}

void UReditusSequenceSkipSubsystem::HideHoldProgress()
{
    if (!bShowingProgress) return;
    if (NoticeWidget) NoticeWidget->RemoveFromParent();
    NoticeWidget = nullptr;
    bShowingProgress = false;
}

void UReditusSequenceSkipSubsystem::SetAutoAdvance(UMovieSceneSequencePlayer* Player, bool Enabled)
{
    bAutoAdvance = Enabled && IsValid(Player);
    AutoSequence = bAutoAdvance ? Player : nullptr;
    AutoWorld = bAutoAdvance ? GetTickableGameObjectWorld() : nullptr;
}

bool UReditusSequenceSkipSubsystem::IsAutoAdvanceEnabled(UMovieSceneSequencePlayer* Player) const
{
    return bAutoAdvance && IsValid(Player) && AutoSequence.Get() == Player
        && AutoWorld.IsValid() && AutoWorld.Get() == GetTickableGameObjectWorld()
        && (Player->IsPlaying() || Player->IsPaused());
}

void UReditusSequenceSkipSubsystem::DismissSequenceDialogue()
{
    auto* Controller = UGameplayStatics::GetPlayerController(GetTickableGameObjectWorld(), 0);
    auto* Dialogue = Cast<UUserWidget>(ReditusQuestNPCReflection::ReadObject(Controller, TEXT("DialogueWidget")));
    if (!IsValid(Dialogue)) return;
    // A scene skip closes the current presentation directly. Advancing its
    // dialogue graph here would fire reply events or reopen another line.
    if (auto* Voice = Cast<UAudioComponent>(ReditusQuestNPCReflection::ReadObject(Dialogue, TEXT("Sound2D"))))
        if (IsValid(Voice)) Voice->Stop();
    Dialogue->RemoveFromParent();
    if (auto* Field = FindFProperty<FObjectPropertyBase>(Controller->GetClass(), TEXT("DialogueWidget")))
        if (Field->GetObjectPropertyValue_InContainer(Controller) == Dialogue)
            Field->SetObjectPropertyValue_InContainer(Controller, nullptr);
}

bool UReditusSequenceSkipSubsystem::BeginAttempt()
{
    HeldSeconds = 0;
    if (bAllowSkip && OnSkipped.IsBound()) { ShowHoldProgress(0); return true; }
    ShowBlockedNotice();
    return false;
}

bool UReditusSequenceSkipSubsystem::HandleKey(const FKey& Key, bool Pressed, bool Repeat)
{
    if (!SkipKey(Key)) return false;
    if (!Pressed)
    {
        const bool Owned = bConsumeKeyUntilRelease;
        bConsumeKeyUntilRelease = false;
        bKeyHeld = false;
        if (!bTriggerHeld && !bMouseHeld) { HeldSeconds = 0; HideHoldProgress(); }
        return Owned;
    }
    if (Repeat && bConsumeKeyUntilRelease) return true;
    if (!CanCapture()) return false;
    bConsumeKeyUntilRelease = true;
    if (Key == EKeys::Gamepad_RightTrigger && bNeedTriggerRelease) return true;
    if (!Repeat) bKeyHeld = BeginAttempt();
    return true;
}

bool UReditusSequenceSkipSubsystem::HandleTrigger(float Value)
{
    LastTriggerValue = Value;
    if (Value < .2f)
    {
        const bool Owned = bTriggerHeld;
        bTriggerHeld = false;
        bNeedTriggerRelease = false;
        if (!bKeyHeld && !bMouseHeld) { HeldSeconds = 0; HideHoldProgress(); }
        return CanCapture() && Owned;
    }
    if (!CanCapture()) return false;
    if (bNeedTriggerRelease) return Value > .2f;
    if (Value > .55f && !bTriggerHeld)
    {
        // Latch even for a blocked attempt so analog polling shows one notice.
        BeginAttempt();
        bTriggerHeld = true;
    }
    return Value > .2f;
}

void UReditusSequenceSkipSubsystem::SetMouseHeld(bool Held)
{
    if (!Held)
    {
        bMouseHeld = false;
        if (!bKeyHeld && !bTriggerHeld) { HeldSeconds = 0; HideHoldProgress(); }
        return;
    }
    bMouseHeld = CanCapture() && BeginAttempt();
}

void UReditusSequenceSkipSubsystem::Tick(float DeltaTime)
{
    (void)DeltaTime;
    RegisterInput();
    const double Now = FPlatformTime::Seconds();
    const double Delta = FMath::Clamp(Now - LastTime, 0.0, .25);
    LastTime = Now;
    if (NoticeWidget && !bShowingProgress)
    {
        NoticeSeconds -= Delta;
        if (NoticeSeconds <= 0 || NoticeWidget->GetWorld() != GetTickableGameObjectWorld())
        {
            NoticeWidget->RemoveFromParent();
            NoticeWidget = nullptr;
        }
    }
    if (bAutoAdvance && !IsAutoAdvanceEnabled(AutoSequence.Get()))
        SetAutoAdvance(nullptr, false);
    if (SequenceActor && !HasActiveSequence()) ClearSequence();
    UpdateDialogueContinuation();
    if (!CanCapture())
    {
        bKeyHeld = bTriggerHeld = bMouseHeld = false;
        bNeedTriggerRelease = LastTriggerValue > .2f;
        HeldSeconds = 0;
        HideHoldProgress();
        return;
    }
    if (!bAllowSkip || !(bKeyHeld || bTriggerHeld || bMouseHeld))
    {
        HeldSeconds = 0;
        HideHoldProgress();
        return;
    }
    HeldSeconds += Delta;
    ShowHoldProgress(FMath::Clamp(static_cast<float>(HeldSeconds), 0.f, 1.f));
    if (HeldSeconds < 1.0) return;
    if (!OnSkipped.IsBound())
    {
        bKeyHeld = bTriggerHeld = bMouseHeld = false;
        HeldSeconds = 0;
        ShowBlockedNotice();
        return;
    }
    // Jump avoids replaying every intervening dialogue cue. Stop alone never
    // invokes OnFinished, so use the scene's explicitly supplied handoff once.
    auto Handoff = OnSkipped;
    auto* Player = SequencePlayer;
    DismissSequenceDialogue();
    ClearSequence();
    FMovieSceneSequencePlaybackParams EndPosition;
    // The upper bound is exclusive. Stay on the last visible frame so a
    // natural-finish callback cannot also execute the authored skip handoff.
    EndPosition.Frame = FMath::Max(Player->GetStartTime().Time,
        Player->GetEndTime().Time - FFrameTime(FFrameNumber(1)));
    EndPosition.PositionType = EMovieScenePositionType::Frame;
    EndPosition.UpdateMethod = EUpdatePositionMethod::Jump;
    Player->SetPlaybackPosition(EndPosition);
    if (IsValid(Player)) Player->StopAtCurrentTime();
    Handoff.ExecuteIfBound();
}

bool UReditusSequenceSkipLibrary::ConfigureLevelSequenceSkipping(const UObject* WorldContextObject,
    ALevelSequenceActor* SequenceActor, FReditusSequenceSkipHandoff OnSkipped, bool AllowSkip)
{
    if (!SequenceActor)
        SequenceActor = ControllerSequenceActor(Cast<APlayerController>(WorldContextObject));
    auto* Instance = UGameplayStatics::GetGameInstance(WorldContextObject);
    auto* System = Instance ? Instance->GetSubsystem<UReditusSequenceSkipSubsystem>() : nullptr;
    return System && System->Configure(SequenceActor, AllowSkip, OnSkipped);
}

UMovieSceneSequencePlayer* UReditusSequenceSkipSubsystem::FindSequencePlayer(APlayerController* Controller) const
{
    UWorld* World = GetTickableGameObjectWorld();
    if (!IsValid(Controller) || !Controller->IsLocalController() || Controller->GetWorld() != World)
        return nullptr;

    // An explicitly configured player also works when a future caller doesn't
    // use the JRPG controller's sequence member.
    if (auto* Actor = ControllerSequenceActor(Controller))
    {
        auto* Player = Actor->GetSequencePlayer();
        if (IsValid(Player) && (Player->IsPaused() || Player->IsPlaying()))
            return Player;
    }
    return HasActiveSequence() && UGameplayStatics::GetPlayerController(World, 0) == Controller
        ? SequencePlayer : nullptr;
}

void UReditusSequenceSkipSubsystem::ObserveDialogue(UUserWidget* Dialogue, bool PlaySequenceCue)
{
    if (!IsValid(Dialogue) || !Dialogue->IsInViewport() || !Dialogue->IsVisible()) return;
    auto* Controller = Dialogue->GetOwningPlayer();
    auto* Player = FindSequencePlayer(Controller);
    if (!Player || !Player->IsPaused()
        || ReditusQuestNPCReflection::ReadObject(Controller, TEXT("DialogueWidget")) != Dialogue)
        return;

    if (ObservedDialogue.Get() != Dialogue || DialogueSequence.Get() != Player)
    {
        ClearDialogueObservation();
        ObservedDialogue = Dialogue;
        DialogueController = Controller;
        DialogueSequence = Player;
        // The last voiced reply can close via the Blueprint's timer without
        // calling the presentation again. Keep this binding beyond widget removal.
        ReditusQuestNPCReflection::BindDialogueRemoved(Dialogue, this, true);
    }
    bPlaySequenceCue = PlaySequenceCue;
}

void UReditusSequenceSkipSubsystem::DialogueContinueRequested(UUserWidget* Dialogue)
{
    if (ObservedDialogue.Get() != Dialogue) ObserveDialogue(Dialogue, false);
    if (IsValid(Dialogue) && ObservedDialogue.Get() == Dialogue) bContinueRequested = true;
}

void UReditusSequenceSkipSubsystem::HandleDialogueRemoved()
{
    bContinueRequested = true;
}

void UReditusSequenceSkipSubsystem::HandleDialogueRemovedWithResult(uint8 RecruitmentEvents)
{
    (void)RecruitmentEvents;
    HandleDialogueRemoved();
}

void UReditusSequenceSkipSubsystem::ClearDialogueObservation()
{
    if (auto* Dialogue = ObservedDialogue.Get())
        ReditusQuestNPCReflection::BindDialogueRemoved(Dialogue, this, false);
    ObservedDialogue.Reset();
    DialogueController.Reset();
    DialogueSequence.Reset();
    bContinueRequested = false;
    bPlaySequenceCue = false;
}

void UReditusSequenceSkipSubsystem::UpdateDialogueContinuation()
{
    auto* Player = DialogueSequence.Get();
    auto* Controller = DialogueController.Get();
    if (!Player || !Player->IsPaused() || FindSequencePlayer(Controller) != Player)
    {
        if (ObservedDialogue.IsValid() || bContinueRequested || bPlaySequenceCue) ClearDialogueObservation();
        return;
    }
    auto* Dialogue = ObservedDialogue.Get();
    auto* Current = Cast<UUserWidget>(ReditusQuestNPCReflection::ReadObject(Controller, TEXT("DialogueWidget")));
    if (IsValid(Current) && Current != Dialogue && Current->IsInViewport())
    {
        ClearDialogueObservation();
        return;
    }
    const bool Visible = IsValid(Dialogue) && Dialogue->IsInViewport() && Dialogue->IsVisible();
    // Let the original graph choose/display its next line first. A different
    // visible line can still be speaking or waiting for a response.
    if (Visible && !bPlaySequenceCue) return;
    if (IsHoldingSkip()) return;
    const bool Continue = bContinueRequested || (Visible && bPlaySequenceCue);
    const bool HideForPlayCue = Visible && bPlaySequenceCue;
    ClearDialogueObservation();
    if (!Continue) return;

    if (HideForPlayCue)
    {
        // Match ToNpcReply's authored play cue for the exploration player.
        if (auto* Blocked = FindFProperty<FBoolProperty>(Dialogue->GetClass(), TEXT("isUIInputBlocked")))
            Blocked->SetPropertyValue_InContainer(Dialogue, true);
        Dialogue->SetVisibility(ESlateVisibility::Hidden);
    }
    Player->Play();
}
