#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Components/SlateWrapperTypes.h"
#include "GameFramework/PlayerInput.h"
#include "ReditusBattleHUDTypes.h"
#include "ReditusBattleHUDPresenterComponent.generated.h"

class APlayerController;
class UUserWidget;
class UWidget;
class UDataTable;
class UTexture2D;
class USceneComponent;
class UReditusBattleHUDWidget;

USTRUCT()
struct FReditusHUDReadinessOverride
{
    GENERATED_BODY()
    UPROPERTY() float Percent = 0.f;
    UPROPERTY() bool bReady = false;
    UPROPERTY() bool bCanAct = true;
    UPROPERTY() bool bQueued = false;
};

/** Add to BP_BattleController. Existing combat/targeting functions remain authoritative. */
UCLASS(ClassGroup=(Reditus), meta=(BlueprintSpawnableComponent))
class CHAPTER1REDITUS_API UReditusBattleHUDPresenterComponent : public UActorComponent
{
    GENERATED_BODY()
public:
    UReditusBattleHUDPresenterComponent();

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Battle HUD") TSubclassOf<UReditusBattleHUDWidget> WidgetClass;
    UPROPERTY(BlueprintReadOnly, Transient, Category="Battle HUD") UReditusBattleHUDWidget* HUD = nullptr;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Battle HUD") bool bReadLegacyBattle = true;
    /** True uses the existing unit events. False opts into OnCommandRequested. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Battle HUD") bool bRouteLegacyCommands = true;
    /** Disable when a future multi-ready backend owns the player's command focus. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Battle HUD|Legacy") bool bFollowLegacyTurnSelection = true;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Battle HUD") bool bSetGameAndUIInputMode = true;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Battle HUD") bool bHideLegacyActionsWidget = false;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Battle HUD") bool bSyncLegacyDeviceFlag = true;
    /** Copy the old attack notice's text/icon/visibility out of its transparent backing root. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Battle HUD|Legacy") bool bMirrorLegacyActionAnnouncement = true;
    /** Complete a recorded EnemyWon result after the last live HP counter reaches 0. Disable for custom scripted exits. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Battle HUD|Legacy") bool bCompleteRecordedDefeat = true;
    /** Serialized compatibility only. Victory handling stays in the original Blueprint flow. */
    UPROPERTY() bool bCompleteRecordedVictory = false;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Battle HUD|Enemies") bool bShowEnemyStatusBadges = true;
    /** Apply battle-specific keyboard and gamepad mappings, then restore them when the HUD closes. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Battle HUD|Input") bool bApplyCombatGamepadProfile = true;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Battle HUD|Menus") bool bUseNativeCommandLists = true;
    /** Slows enemy ATB fill and preparation in battle subwindows and target selection. 50 = half speed, 100 = pause. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Battle HUD|Menus", meta=(ClampMin="50.0", ClampMax="100.0", UIMin="50.0", UIMax="100.0"))
    float EnemyMenuSlowdownPercent = 50.f;
    /** Reuse the project's menu builder when its old visible HUD was removed. Runtime only. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Battle HUD|Menus") bool bCreateMissingMenuSource = true;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Battle HUD|Menus") UDataTable* SkillDescriptions = nullptr;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Battle HUD|Menus") UDataTable* ItemDescriptions = nullptr;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Battle HUD|Input") bool bLogCommandRouting = false;
    UPROPERTY(BlueprintReadOnly, Transient, Category="Battle HUD|Input") FString LastRouteStatus;
    /** The supplied project uses Critical as its action-time threshold. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Battle HUD|Legacy") FName LegacyATBThresholdProperty = TEXT("Critical");
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Battle HUD|Legacy") FName HackCharacterID = TEXT("Airloft");
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Battle HUD|Legacy") int32 CommandMenuModeValue = 1;

    /** Custom root-command route, used only when Route Legacy Commands is false. */
    UPROPERTY(BlueprintAssignable, Category="Battle HUD") FReditusHUDCommandEvent OnCommandRequested;
    UPROPERTY(BlueprintAssignable, Category="Battle HUD") FReditusHUDSelectionEvent OnSelectedUnitChanged;
    UPROPERTY(BlueprintAssignable, Category="Battle HUD") FReditusHUDGameplayInputEvent OnUnroutedGameplayInput;

    UFUNCTION(BlueprintCallable, Category="Battle HUD") UReditusBattleHUDWidget* CreateBattleHUD(APlayerController* PlayerController, int32 ZOrder = 30);
    UFUNCTION(BlueprintCallable, Category="Battle HUD") void DestroyBattleHUD();
    UFUNCTION(BlueprintCallable, Category="Battle HUD") void RefreshBattleHUD();
    UFUNCTION(BlueprintCallable, Category="Battle HUD") void SetPartyUnits(const TArray<AActor*>& Units);
    /** ATB backends can supply actual readiness without changing the display or combat structs. */
    UFUNCTION(BlueprintCallable, Category="Battle HUD") void SetUnitReadiness(AActor* Unit, float Percent, bool bReady, bool bCanAct = true, bool bQueued = false);
    UFUNCTION(BlueprintCallable, Category="Battle HUD") void ClearReadinessOverrides();
    UFUNCTION(BlueprintCallable, Category="Battle HUD") void SetCommandWindowEnabled(bool bEnabled);
    UFUNCTION(BlueprintCallable, Category="Battle HUD") void FollowLegacyCommandWindow();
    UFUNCTION(BlueprintCallable, Category="Battle HUD") void SetTargetedEnemy(AActor* Enemy);
    UFUNCTION(BlueprintCallable, Category="Battle HUD") void SetTargetGroupLabel(FText Label);
    UFUNCTION(BlueprintCallable, Category="Battle HUD") void FollowLegacyTarget();
    UFUNCTION(BlueprintCallable, Category="Battle HUD") void SetDisplayedBattleSpeed(float Multiplier);
    /** Hide only specified old presentation widgets. Never hides targeting/dialogue roots automatically. */
    UFUNCTION(BlueprintCallable, Category="Battle HUD") void SetLegacyWidgetsToHide(const TArray<UWidget*>& Widgets);
    UFUNCTION(BlueprintCallable, Category="Battle HUD") bool ExecuteLegacyCommand(AActor* Unit, EReditusHUDCommand Command);
    UFUNCTION(BlueprintCallable, Category="Battle HUD") bool ForwardGameplayInput(FName ActionName, FKey Key);

    /** Compatibility pass-through for old installed nodes; RestoreVictoryFlow removes them. */
    UFUNCTION(BlueprintCallable, Category="Battle HUD|Legacy")
    static bool TryBeginRecordedBattleResult(AActor* BattleController);

    virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
    UPROPERTY(Transient) APlayerController* Player = nullptr;
    UPROPERTY(Transient) TArray<AActor*> PartyUnits;
    /** Display-only snapshots, retained across removal/destruction on KO. */
    UPROPERTY(Transient) TArray<FReditusBattleHUDUnit> DisplaySlots;
    UPROPERTY(Transient) TMap<AActor*,FReditusHUDReadinessOverride> Readiness;
    UPROPERTY(Transient) TArray<UWidget*> HiddenLegacyWidgets;
    UPROPERTY(Transient) TArray<ESlateVisibility> PreviousLegacyVisibility;
    UPROPERTY(Transient) UUserWidget* NativeMenuSource = nullptr;
    UPROPERTY(Transient) AActor* NativeMenuOwner = nullptr;
    UPROPERTY(Transient) AActor* PendingMenuOwner = nullptr;
    UPROPERTY(Transient) TArray<UUserWidget*> NativeMenuRows;
    UPROPERTY(Transient) TArray<FName> NativeMenuIDs;
    UPROPERTY(Transient) TArray<FName> NativeMenuDataIDs;
    UPROPERTY(Transient) UDataTable* PartyMemberSkillDefinitions = nullptr;
    UPROPERTY(Transient) UDataTable* EquipmentSkillDefinitions = nullptr;
    UPROPERTY(Transient) TMap<int32,UTexture2D*> ActionElementTextures;
    TMap<TWeakObjectPtr<USceneComponent>,bool> HiddenLegacyEnemyComponents;
    struct FAggroIndicatorVisibility
    {
        bool bVisible;
        bool bHiddenInGame;
    };
    TMap<TWeakObjectPtr<USceneComponent>,FAggroIndicatorVisibility> OwnedAggroIndicators;
    TWeakObjectPtr<AActor> ExplicitEnemyTarget;
    UPROPERTY(Transient) TArray<UWidget*> SuppressedMenuWidgets;
    UPROPERTY(Transient) TArray<ESlateVisibility> SuppressedMenuVisibility;
    UPROPERTY(Transient) TArray<float> SuppressedMenuOpacity;
    UPROPERTY(Transient) UUserWidget* HostedLegacyRoot = nullptr;
    UPROPERTY(Transient) UUserWidget* AssignedLegacyRoot = nullptr;
    UPROPERTY(Transient) TArray<AActor*> LinkedLegacyRootUnits;
    ESlateVisibility HostedRootVisibility = ESlateVisibility::Visible;
    float HostedRootOpacity = 1.f;
    bool bAddedLegacyRootToScreen = false;
    bool bLinkedControllerRoot = false;
    bool bAttemptedLegacyRootCreation = false;
    struct FInputOverride
    {
        FName Action;
        TArray<FInputActionKeyMapping> Before;
        TArray<FInputActionKeyMapping> Applied;
    };
    TArray<FInputOverride> InputOverrides;
    EReditusHUDCommand NativeMenuCommand = EReditusHUDCommand::Tech;
    EReditusHUDCommand PendingMenuCommand = EReditusHUDCommand::Tech;
    TWeakObjectPtr<AActor> DraftOwner;
    TWeakObjectPtr<AActor> DraftTarget;
    TArray<TWeakObjectPtr<AActor>> DraftIcons;
    FName DraftActionID;
    EReditusHUDCommand DraftCommand = EReditusHUDCommand::Tech;
    int32 DraftCycle = INDEX_NONE;
    bool bDraftActive = false;
    bool bDraftGroup = false;
    TSet<TWeakObjectPtr<AActor>> MenuSlowedEnemies;
    TSet<TWeakObjectPtr<UUserWidget>> OpenLegacyMenuWindows;
    bool bAwaitingNativeMenu = false;
    int32 PendingOriginalSelection = -1;
    double PendingMenuDeadline = 0.0;
    TWeakObjectPtr<AActor> PreviousLegacyUnit;
    TMap<TWeakObjectPtr<AActor>,int32> LastTurnCueCycles;
    TWeakObjectPtr<AActor> TrackedEncounter;
    TSet<TWeakObjectPtr<AActor>> TrackedEncounterUnits;
    bool bTrackingEncounter = false;
    bool bStoppedClockForExit = false;
    bool bRetriedOnLevelFleeExit = false;
    bool bDefeatFlowHandled = false;
    bool bExplicitParty = false;
    bool bExplicitCommandWindow = false;
    bool bCommandWindowEnabled = false;
    bool bExplicitTarget = false;
    bool bExplicitSpeed = false;
    bool bRefreshing = false;
    bool bRouting = false;
    bool bWarnedPartyOverflow = false;
    bool bShowingEnemyATBAnnouncement = false;

    FReditusBattleHUDUnit ReadUnit(AActor* Unit, AActor* CurrentActor, bool bCommandWindow) const;
    void UpdateDisplaySlots(UObject* Battle, AActor* CurrentActor, bool bCommandWindow);
    void ApplyCombatInputProfile();
    void RestoreCombatInputProfile();
    void BeginNativeMenu(AActor* Unit, EReditusHUDCommand Command);
    bool EnsureLegacyMenuRoot(AActor* Unit);
    void ReleaseLegacyMenuRoot();
    UUserWidget* FindLegacyMenu(AActor* Unit, EReditusHUDCommand Command) const;
    void RecoverMenuPreparation(AActor* Unit, EReditusHUDCommand Command);
    void TryCaptureNativeMenu();
    void RefreshNativeMenuEntries(bool bOpen);
    bool IsBattleMoveUnlocked(AActor* Unit,FName MoveID,bool bOverdrive) const;
    void ClearNativeMenu();
    bool BeginATBListTargeting(AActor* Unit,FName ID,EReditusHUDCommand Command);
    void AdoptLegacyAllyTargeting(AActor* Unit);
    bool RouteATBListTargetInput(FName ActionName);
    AActor* FindTimedAttackOwner() const;
    void ClearATBListTargeting();
    void UpdateEnemyMenuSlowdown(UObject* Battle,AActor* CommandUnit);
    void ClearEnemyMenuSlowdown();
    bool ShowExecutingEnemyAction(UObject* Battle);
    void UpdateEnemyBadges(UObject* Battle,AActor* Target,bool bGroup);
    void RestoreLegacyEnemyComponents();
    void UpdateAggroIndicator(UObject* Battle);
    void ReleaseAggroIndicators();
    void TrackLegacyEncounter(AActor* Encounter);
    void FinishConfirmedDefeat();
    void FinishConfirmedBattleExit();
    void ResetTrackedEncounter();
    UFUNCTION() void HandleNativeMenuChosen(AActor* Unit, FName EntryID);
    UFUNCTION() void HandleNativeMenuCancelled(AActor* Unit);
    bool HandleCommand(AActor* Unit, EReditusHUDCommand Command);
    bool SelectCommandUnit(AActor* Unit);
    void ReconcileATBCommandContext();
    void SynchronizeCommandSelectionEffects(AActor* CommandUnit);
    UFUNCTION() void HandleSelection(AActor* Unit);
    UFUNCTION() void HandleDevice(EReditusHUDInputDevice Device);
};
