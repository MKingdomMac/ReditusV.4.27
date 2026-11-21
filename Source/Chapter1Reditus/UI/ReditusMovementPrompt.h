#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "InputCoreTypes.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Subsystems/LocalPlayerSubsystem.h"
#include "ReditusMovementPrompt.generated.h"

class AActor;
class APlayerController;
class FReditusMovementPromptInputObserver;
class IInputProcessor;
class UActorComponent;
class UBorder;
class UCanvasPanelSlot;
class UFont;
class UImage;
class UReditusATBComponent;
class UReditusBattleHUDWidget;
class USaveGame;
class USizeBox;
class UTextBlock;
class UTexture2D;
class UTimelineComponent;
class UVerticalBox;
class UWidget;
class UWorld;

DECLARE_DYNAMIC_DELEGATE(FReditusHeldTutorialBite);
DECLARE_DYNAMIC_DELEGATE(FReditusBattleTutorialContinued);

enum class EReditusTutorialLesson : uint8 { Block, AmpAttack, Overdrive, Count };

UCLASS(NotBlueprintable, Transient)
class CHAPTER1REDITUS_API UReditusMovementPromptWidget : public UUserWidget
{
    GENERATED_BODY()

public:
    explicit UReditusMovementPromptWidget(const FObjectInitializer& Initializer);

    void SetGlyph(UTexture2D* Texture, bool bGamepad);
    void SetBlockPrompt(UTexture2D* Texture, const FText& KeyLabel);
    void SetGuidedPrompt(const FText& Caption, UTexture2D* Texture, const FText& KeyLabel, bool bSelectTarget);
    void SetDialogue(const FText& Title, const FText& Message, bool bShowShield,
        const FText& ActionCaption = FText(), int32 StaminaExampleMax = 0);
    void SetContinueGlyph(UTexture2D* Texture, const FText& KeyLabel);
    FSimpleDelegate ContinueRequested;
    FSimpleDelegate PreviousTargetRequested;
    FSimpleDelegate NextTargetRequested;

protected:
    virtual TSharedRef<SWidget> RebuildWidget() override;

private:
    enum class ECard : uint8 { Movement, Block, Guided, Dialogue };

    void RefreshCard();
    void RefreshStaminaExample();
    UFUNCTION() void OnContinueClicked();
    UFUNCTION() void OnPreviousTargetClicked();
    UFUNCTION() void OnNextTargetClicked();

    UPROPERTY(Transient)
    UFont* HeadingFont = nullptr;

    UPROPERTY(Transient)
    UTexture2D* CurrentGlyph = nullptr;

    UPROPERTY(Transient)
    UImage* GlyphImage = nullptr;

    UPROPERTY(Transient)
    USizeBox* GlyphBounds = nullptr;

    UPROPERTY(Transient)
    UTextBlock* PromptCaption = nullptr;

    UPROPERTY(Transient)
    UTextBlock* KeyCaption = nullptr;

    UPROPERTY(Transient)
    UBorder* KeyCap = nullptr;

    UPROPERTY(Transient)
    UCanvasPanelSlot* PromptPlacement = nullptr;

    UPROPERTY(Transient)
    UWidget* Shield = nullptr;

    UPROPERTY(Transient)
    UWidget* PromptRow = nullptr;

    UPROPERTY(Transient)
    UWidget* PromptButton = nullptr;

    UPROPERTY(Transient)
    UWidget* PreviousTargetButton = nullptr;

    UPROPERTY(Transient)
    UWidget* NextTargetButton = nullptr;

    UPROPERTY(Transient)
    UWidget* DialogueLayout = nullptr;

    UPROPERTY(Transient)
    UWidget* DialogueShield = nullptr;

    UPROPERTY(Transient)
    UWidget* Scrim = nullptr;

    UPROPERTY(Transient)
    UTextBlock* DialogueHeading = nullptr;

    UPROPERTY(Transient)
    UTextBlock* DialogueBody = nullptr;

    UPROPERTY(Transient)
    UVerticalBox* StaminaExample = nullptr;

    UPROPERTY(Transient)
    UTextBlock* DialogueAction = nullptr;

    UPROPERTY(Transient)
    UImage* ContinueGlyph = nullptr;

    UPROPERTY(Transient)
    USizeBox* ContinueGlyphBox = nullptr;

    UPROPERTY(Transient)
    UBorder* ContinueKeyCap = nullptr;

    UPROPERTY(Transient)
    UTextBlock* ContinueKeyText = nullptr;

    UPROPERTY(Transient)
    UTexture2D* ContinueTexture = nullptr;

    FText KeyLabel;
    FText ContinueLabel;
    FText TitleText;
    FText BodyText;
    FText DialogueActionCaption;
    FText GuidedCaption;
    ECard Card = ECard::Movement;
    int32 EnemyStaminaExampleMax = 0;
    bool bGamepadGlyph = false;
    bool bDialogueShield = false;
    bool bSelectTarget = false;
};

UCLASS()
class CHAPTER1REDITUS_API UReditusMovementPromptSubsystem : public ULocalPlayerSubsystem
{
    GENERATED_BODY()

public:
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;

    void ShowPrompt(APlayerController& OwningController);
    void ArmBlockTutorial(AActor& BattleController, APlayerController& OwningController);
    void ShowBlockTutorialPrompt(AActor& Attacker, APlayerController& OwningController);
    bool HoldBlockTutorialBite(AActor& Attacker, AActor& Defender, UActorComponent& AttackAnimation,
        bool bDefenderIsDefending, const FReditusHeldTutorialBite& ApplyBite);
    void NotifyBlockTutorialDefense(AActor& Defender);
    bool ShowBattleTutorialDialogue(AActor& BattleController, APlayerController& OwningController,
        const FText& Title, const FText& Message, const FReditusBattleTutorialContinued& OnContinued,
        bool bShowShield = false, int32 StaminaExampleMax = 0);
    bool TrackBattleDialogue(AActor& BattleController, APlayerController& OwningController, UUserWidget& Dialogue);
    bool ShouldSaveTutorialCompletion(const UWorld& World, const USaveGame& SaveGame, EReditusTutorialLesson Lesson) const;

private:
    friend class FReditusMovementPromptInputObserver;

    void RegisterInputObserver();
    void RecordDevice(bool bGamepad);
    bool HandleHeldBlockKey(const FKey& Key, bool bRepeat);
    bool HandleDialogueKey(const FKey& Key, bool bPressed, bool bRepeat);
    bool EnsurePrompt(APlayerController& OwningController);
    bool DisplayPrompt(APlayerController& OwningController, int32 ZOrder);
    void TickPrompt();
    void DismissPrompt();
    bool AttachBlockBattleHUD();
    bool IsBlockBattleActive() const;
    bool IsAttackerInBlockBattle(const AActor& Attacker) const;
    void RefreshBlockGlyph();
    void ResolveBlockTutorialBite(AActor& Defender, FReditusHeldTutorialBite ReleaseBite);
    void ReleaseBlockHold();
    void CancelBlockTutorial();
    void RefreshDialogueGlyph();
    bool SyncBattleDialoguePause();
    void ReleaseDialoguePause();
    void CloseTutorialDialogue(bool bContinued);
    void ContinueTutorialDialogue();
    void ArmFirstBattleTutorial(AActor& BattleController, APlayerController& OwningController);
    void TickFirstBattleTutorial();
    void ResetFirstBattleTutorial();
    void RecordTutorialCompletion(UWorld& World, EReditusTutorialLesson Lesson);
    void TickOverdriveTutorial(APlayerController& OwningController);
    bool ShowGuidedPrompt();
    void HideGuidedPrompt();
    void ReleaseGuidedPause();
    bool IsGuidedInputLocked() const;
    bool HandleGuidedKey(const FKey& Key, bool bPressed, bool bRepeat);
    void ConfirmGuidedPrompt();
    void MoveGuidedTarget(int32 Direction);
    void ExecuteGuidedInput(FName Movement, FKey Key);
    bool CaptureGuidedAttack(const UReditusATBComponent& ATB, AActor& Unit);

    enum class EGuidedTurn : uint8 { Inactive, WaitForTurn, Amp, Attack, OpeningTargets, Target, ConfirmingTarget, Resolving, Overdrive };

    struct FTutorialCompletion
    {
        TWeakObjectPtr<USaveGame> Save;
        TWeakObjectPtr<UWorld> World;
        bool bHadSave = false;
    };

    UPROPERTY(Transient)
    UReditusMovementPromptWidget* ActivePrompt = nullptr;

    UPROPERTY(Transient)
    UReditusMovementPromptWidget* TutorialDialogue = nullptr;

    UPROPERTY(Transient)
    UReditusMovementPromptWidget* GuidedPrompt = nullptr;

    UPROPERTY(Transient)
    UTexture2D* KeyboardGlyph = nullptr;

    UPROPERTY(Transient)
    UTexture2D* GamepadGlyph = nullptr;

    TSharedPtr<IInputProcessor> InputObserver;
    TWeakObjectPtr<UWorld> PromptWorld;
    TWeakObjectPtr<AActor> BlockBattleController;
    TWeakObjectPtr<APlayerController> BlockPlayer;
    TWeakObjectPtr<UReditusBattleHUDWidget> BlockBattleHUD;
    TWeakObjectPtr<UReditusBattleHUDWidget> CompletedBlockBattleHUD;
    FTutorialCompletion Completions[static_cast<uint8>(EReditusTutorialLesson::Count)];
    TWeakObjectPtr<AActor> BlockAttacker;
    TWeakObjectPtr<AActor> EarlyBlockDefender;
    TWeakObjectPtr<AActor> HeldDefender;
    TWeakObjectPtr<UActorComponent> HeldAnimation;
    TArray<TWeakObjectPtr<UTimelineComponent>> HeldAttackerTimelines;
    TWeakObjectPtr<UReditusATBComponent> HeldATB;
    TWeakObjectPtr<UWorld> HeldPauseWorld;
    FReditusHeldTutorialBite HeldBite;
    TArray<TWeakObjectPtr<UUserWidget>> BattleDialogues;
    TWeakObjectPtr<AActor> DialogueBattleController;
    TWeakObjectPtr<APlayerController> DialoguePlayer;
    TWeakObjectPtr<UReditusBattleHUDWidget> DialogueHUD;
    TWeakObjectPtr<UWorld> PausedWorld;
    TWeakObjectPtr<AActor> GuidedBattleController;
    TWeakObjectPtr<APlayerController> GuidedPlayer;
    TWeakObjectPtr<UReditusBattleHUDWidget> GuidedHUD;
    TWeakObjectPtr<UReditusBattleHUDWidget> CompletedGuidedHUD;
    TWeakObjectPtr<AActor> GuidedUnit;
    TWeakObjectPtr<AActor> GuidedTarget;
    TWeakObjectPtr<UWorld> GuidedPauseWorld;
    TWeakPtr<SWidget> PreviousDialogueFocus;
    FReditusBattleTutorialContinued DialogueContinued;
    TSet<FKey> GuidedHeldKeys;
    FKey GuidedBinding;
    EGuidedTurn GuidedTurn = EGuidedTurn::Inactive;
    EGuidedTurn DisplayedGuidedTurn = EGuidedTurn::Inactive;
    int32 GuidedActionToken = INDEX_NONE;
    double GuidedTargetsDeadline = 0.0;
    bool bGuidedHUDWasSuspended = false;
    bool bGuidedAttackStarted = false;
    bool bGuidedTargetWasStaggered = false;
    FKey DisplayedBlockKey;
    FKey DisplayedContinueKey;
    FKey PressedContinueKey;
    double DismissAt = 0.0;
    bool bHasObservedDevice = false;
    bool bLastInputWasGamepad = false;
    bool bShowingBlockPrompt = false;
    bool bBlockBattleAttached = false;
    bool bHoldingBite = false;
    bool bResolvingBite = false;
    bool bAttackerWasTicking = false;
    bool bAnimationWasTicking = false;
    bool bATBWasPaused = false;
};

UCLASS()
class CHAPTER1REDITUS_API UReditusMovementPromptLibrary : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()

public:
    /** Show the movement hint for the local player for 4.5 seconds. */
    UFUNCTION(BlueprintCallable, Category = "Reditus|Tutorial",
        meta = (WorldContext = "WorldContextObject", DisplayName = "Show Movement Tutorial Prompt"))
    static void ShowMovementTutorialPrompt(const UObject* WorldContextObject);

    /** Call from the first encounter before Start Battle or after its battle HUD opens. */
    UFUNCTION(BlueprintCallable, Category = "Reditus|Tutorial")
    static void ArmBlockTutorial(AActor* BattleController);

    /** Call from the lizard's warning notify. */
    UFUNCTION(BlueprintCallable, Category = "Reditus|Tutorial")
    static void ShowBlockTutorialPrompt(AActor* Attacker);

    /** True means this node handled the bite. If false, apply the bite normally. */
    UFUNCTION(BlueprintCallable, Category = "Reditus|Tutorial")
    static bool HoldBlockTutorialBite(AActor* Attacker, AActor* Defender, UActorComponent* AttackAnimation,
        bool bDefenderIsDefending, FReditusHeldTutorialBite ApplyBite);

    /** Call immediately after the block input sets the target's IsDefending flag. */
    UFUNCTION(BlueprintCallable, Category = "Reditus|Tutorial")
    static void NotifyBlockTutorialDefense(AActor* Defender);

    /** The regular story save operation, including battle lessons completed in this playthrough. */
    UFUNCTION(BlueprintCallable, Category = "Reditus|Tutorial",
        meta = (WorldContext = "WorldContextObject", DisplayName = "Save Game to Slot With Tutorial Progress"))
    static bool SaveGameToSlotWithTutorialProgress(const UObject* WorldContextObject, USaveGame* SaveGameObject,
        const FString& SlotName, int32 UserIndex = 0);

    /** Passes through the damage and weakness inputs for graphs made with the previous tutorial. */
    UFUNCTION(BlueprintCallable, Category = "Reditus|Tutorial", meta = (DisplayName = "Prepare First Battle AMP Hit"))
    static int32 PrepareFirstBattleAmpHit(AActor* BattleController, int32 CalculatedDamage,
        bool bWeakness, bool& bReduceStamina);

    /** Passes through the existing chance result for graphs made with the previous tutorial. */
    UFUNCTION(BlueprintPure, Category = "Reditus|Tutorial", meta = (DisplayName = "Allow First Battle Tutorial Stagger"))
    static bool AllowFirstBattleTutorialStagger(AActor* BattleController, AActor* Target, bool bPassedChanceRoll);

    /** Show a modal tutorial explanation. The battle resumes when the player continues. */
    UFUNCTION(BlueprintCallable, Category = "Reditus|Tutorial")
    static bool ShowBattleTutorialDialogue(AActor* BattleController, const FText& Title, const FText& Message,
        FReditusBattleTutorialContinued OnContinued);

    /** Call after adding a battle dialogue that accepts input while the game is paused. */
    UFUNCTION(BlueprintCallable, Category = "Reditus|Tutorial", meta = (DisplayName = "Pause Battle While Dialogue Is Open"))
    static bool PauseBattleWhileDialogueIsOpen(AActor* BattleController, UUserWidget* Dialogue);
};
