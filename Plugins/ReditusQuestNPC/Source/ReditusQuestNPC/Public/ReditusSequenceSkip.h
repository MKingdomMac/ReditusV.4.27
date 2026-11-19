#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Tickable.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Blueprint/UserWidget.h"
#include "ReditusSequenceSkip.generated.h"

class ALevelSequenceActor;
class APlayerController;
class UMovieSceneSequencePlayer;
class UTextBlock;
class UProgressBar;
class IInputProcessor;
struct FKey;

// The same no-argument handoff can be used for natural finish and authored skip.
DECLARE_DYNAMIC_DELEGATE(FReditusSequenceSkipHandoff);

UCLASS()
class REDITUSQUESTNPC_API UReditusSequenceSkipNotice : public UUserWidget
{
    GENERATED_BODY()
public:
    void SetMessage(const FText& Message);
    void SetProgress(float Progress);
protected:
    virtual TSharedRef<SWidget> RebuildWidget() override;
private:
    FText Notice;
    UPROPERTY(Transient) UTextBlock* Caption = nullptr;
    UPROPERTY(Transient) UProgressBar* ProgressBar = nullptr;
    float HoldProgress = -1.f;
};

UCLASS()
class REDITUSQUESTNPC_API UReditusSequenceSkipSubsystem : public UGameInstanceSubsystem, public FTickableGameObject
{
    GENERATED_BODY()
public:
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;
    virtual void Tick(float DeltaTime) override;
    virtual bool IsTickable() const override;
    virtual UWorld* GetTickableGameObjectWorld() const override;
    virtual TStatId GetStatId() const override;

    bool Configure(ALevelSequenceActor* Actor, bool AllowSkip, FReditusSequenceSkipHandoff Handoff);
    bool HasActiveSequence() const;
    UMovieSceneSequencePlayer* FindSequencePlayer(APlayerController* Controller) const;
    void ObserveDialogue(UUserWidget* Dialogue, bool PlaySequenceCue);
    void DialogueContinueRequested(UUserWidget* Dialogue);
    bool IsHoldingSkip() const { return bAllowSkip && (bKeyHeld || bTriggerHeld || bMouseHeld); }
    bool HandleKey(const FKey& Key, bool Pressed, bool Repeat = false);
    bool HandleTrigger(float Value);
    void SetMouseHeld(bool Held);
    void ShowBlockedNotice();
    void SetAutoAdvance(UMovieSceneSequencePlayer* Player, bool Enabled);
    bool IsAutoAdvanceEnabled(UMovieSceneSequencePlayer* Player) const;

private:
    bool CanCapture() const;
    bool BeginAttempt();
    void ClearSequence();
    void ShowHoldProgress(float Progress);
    void HideHoldProgress();
    void DismissSequenceDialogue();
    void RegisterInput();
    void UpdateDialogueContinuation();
    void ClearDialogueObservation();
    UFUNCTION() void SequenceEnded();
    UFUNCTION() void HandleDialogueRemoved();
    UFUNCTION() void HandleDialogueRemovedWithResult(uint8 RecruitmentEvents);
    UPROPERTY(Transient) ALevelSequenceActor* SequenceActor = nullptr;
    UPROPERTY(Transient) UMovieSceneSequencePlayer* SequencePlayer = nullptr;
    UPROPERTY(Transient) FReditusSequenceSkipHandoff OnSkipped;
    UPROPERTY(Transient) UReditusSequenceSkipNotice* NoticeWidget = nullptr;
    TSharedPtr<IInputProcessor> InputProcessor;
    TWeakObjectPtr<UMovieSceneSequencePlayer> AutoSequence;
    TWeakObjectPtr<UWorld> AutoWorld;
    TWeakObjectPtr<UUserWidget> ObservedDialogue;
    TWeakObjectPtr<APlayerController> DialogueController;
    TWeakObjectPtr<UMovieSceneSequencePlayer> DialogueSequence;
    bool bContinueRequested = false;
    bool bPlaySequenceCue = false;
    bool bAutoAdvance = false;
    bool bShowingProgress = false;
    bool bConsumeKeyUntilRelease = false;
    bool bAllowSkip = false;
    bool bKeyHeld = false;
    bool bTriggerHeld = false;
    bool bMouseHeld = false;
    bool bNeedTriggerRelease = false;
    bool bStopped = false;
    float LastTriggerValue = 0;
    double HeldSeconds = 0;
    double NoticeSeconds = 0;
    double LastTime = 0;
};

UCLASS()
class REDITUSQUESTNPC_API UReditusSequenceSkipLibrary : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()
public:
    // Configure just before Play. The controller's Current Sequence can
    // supply the actor when this node is called in the controller itself.
    UFUNCTION(BlueprintCallable, Category="Reditus|Cinematics", meta=(WorldContext="WorldContextObject", DefaultToSelf="WorldContextObject", DisplayName="Configure Level Sequence Skipping"))
    static bool ConfigureLevelSequenceSkipping(const UObject* WorldContextObject, ALevelSequenceActor* SequenceActor,
        FReditusSequenceSkipHandoff OnSkipped, bool AllowSkip = false);
};
