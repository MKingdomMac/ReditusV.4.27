#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "ReditusQuestSystemTypes.h"
#include "ReditusQuestRuntime.h"
#include "ReditusQuestWorldRuntime.h"
#include "Tickable.h"
#include "ReditusQuestSubsystem.generated.h"

class APlayerController;
class AActor;
class USaveGame;
class UReditusQuestNotificationsWidget;
class ULevelSequencePlayer;
class ALevelSequenceActor;
class UReditusQuestJournalBinding;
class UReditusQuestNPCComponent;
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FReditusQuestChanged, FDataTableRowHandle, Quest);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FReditusQuestDeliveryRequested, FReditusQuestDelivery, Delivery);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FReditusQuestStateRestored);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FReditusQuestNotificationEvent, FReditusQuestNotification, Notification);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FReditusQuestSequenceEvent, FReditusQuestSequence, Sequence, bool, Completed);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FReditusNPCRemovedEvent, FName, NPCID);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FReditusNPCBattleEvent, FReditusNPCBattleRequest, Request);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FReditusNPCBattleResolvedEvent, FReditusNPCBattleRequest, Request, EReditusNPCBattleResult, Result);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FReditusNPCFollowUpFinishedEvent, FName, NPCID, FName, EncounterID, bool, Completed);

UCLASS()
class REDITUSQUESTNPC_API UReditusQuestSubsystem : public UGameInstanceSubsystem, public FTickableGameObject
{
    GENERATED_BODY()
public:
    virtual void Tick(float DeltaTime) override;
    virtual bool IsTickable() const override;
    virtual UWorld* GetTickableGameObjectWorld() const override;
    virtual TStatId GetStatId() const override;
    virtual void Deinitialize() override;
    UFUNCTION(BlueprintPure, Category="Reditus|New Quests", meta=(WorldContext="WorldContextObject"))
    static UReditusQuestSubsystem* GetQuestSystem(const UObject* WorldContextObject);

    UFUNCTION(BlueprintCallable, Category="Reditus|New Quests")
    bool StartQuest(FDataTableRowHandle Quest, bool IgnorePrerequisites = false);
    UFUNCTION(BlueprintPure, Category="Reditus|Quest NPC|Persistence") bool WasNPCRemoved(FName NPCID) const;
    // Internal lifetime check shared by linked visuals; not another removal API.
    bool IsNPCConversationActive(FName NPCID) const;
    bool HasNPCDialogueOpen() const;
    bool HasNPCFollowUps() const;
    bool HasNPCStoryWork() const;
    bool HasQueuedQuestSequences() const { return SequencePlayer || SequenceQueue.Num() > 0; }
    bool CanPresentNPCFollowUp() const { return !bImporting && !bFlushing && !bApplyingNPCAction && !bEndingSequence && !bSubsystemStopped && PendingNPCBattles.Num() == 0; }
    void CancelNPCFollowUpSequences(FGuid RequestID);
    void CancelNPCStoryWork();
    // One public removal command for dialogue, battle outcomes and sequence events.
    UFUNCTION(BlueprintCallable, Category="Reditus|Quest NPC|Persistence")
    bool RemoveNPCPermanently(FName NPCID, EReditusNPCRemovalReason Reason = EReditusNPCRemovalReason::Removed);
    // Retained for existing Blueprint references; new graphs use RemoveNPCPermanently.
    UFUNCTION(BlueprintCallable, Category="Reditus|Quest NPC|Persistence", meta=(BlueprintInternalUseOnly="true", DeprecatedFunction, DeprecationMessage="Use Remove NPC Permanently on Get Quest System."))
    bool RememberNPCRemoved(FName NPCID, EReditusNPCRemovalReason Reason = EReditusNPCRemovalReason::Removed);
    UFUNCTION(BlueprintCallable, Category="Reditus|Quest NPC|Recruitment")
    bool RecruitQuestNPC(APlayerController* Player, FDataTableRowHandle Character, int32 StartingLevel,
        bool AddToPartyIfSpace, FName NPCID, bool RemoveNPC);
    // For an existing recruitment success branch that already added the unit.
    UFUNCTION(BlueprintCallable, Category="Reditus|Quest NPC|Recruitment")
    bool ConfirmQuestRecruitment(APlayerController* Player, FName CharacterID, FName NPCID, bool RemoveNPC);
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Reditus|Quest NPC|Recruitment", meta=(ClampMin="1"))
    int32 ActivePartyLimit = 4;
    UFUNCTION(BlueprintCallable, Category="Reditus|Quest NPC|Battle")
    bool RequestNPCBattle(APlayerController* Player, FName NPCID, FName EncounterID, bool RemoveNPCOnVictory, FGuid& RequestID);
    UFUNCTION(BlueprintCallable, Category="Reditus|Quest NPC|Battle")
    bool ResolveNPCBattle(FGuid RequestID, EReditusNPCBattleResult Result);
    UFUNCTION(BlueprintPure, Category="Reditus|Quest NPC|Battle") bool HasPendingNPCBattle(FName NPCID) const;
    UPROPERTY(BlueprintAssignable, Category="Reditus|Quest NPC|Persistence") FReditusNPCRemovedEvent OnNPCRemoved;
    UPROPERTY(BlueprintAssignable, Category="Reditus|Quest NPC|Battle") FReditusNPCBattleEvent OnNPCBattleRequested;
    // Reports the actual outcome once. Queue story work here; cinematics wait for exploration.
    UPROPERTY(BlueprintAssignable, Category="Reditus|Quest NPC|Battle") FReditusNPCBattleResolvedEvent OnNPCBattleResolved;
    // After the configured scene/dialogue and before any queued next battle.
    UPROPERTY(BlueprintAssignable, Category="Reditus|Quest NPC|Battle") FReditusNPCFollowUpFinishedEvent OnNPCFollowUpFinished;
    UFUNCTION(BlueprintCallable, Category="Reditus|New Quests") bool RetryFailedQuest(FDataTableRowHandle Quest);
    // Call from confirmed On Unit Die, before actor destruction; never from Destroyed/cleanup.
    UFUNCTION(BlueprintCallable, Category="Reditus|New Quests") bool ReportEnemyDefeated(AActor* Enemy, FName EnemyID);
    UFUNCTION(BlueprintPure, Category="Reditus|New Quests|Timers") TArray<FReditusQuestTimerStatus> GetQuestTimers() const;
    UPROPERTY(BlueprintReadWrite, Category="Reditus|New Quests|Timers") bool PauseQuestTimers = false;
    // Requests queue in order. No second sequence starts over an active one.
    UFUNCTION(BlueprintCallable, Category="Reditus|New Quests|Sequences") bool PlayQuestLevelSequence(FReditusQuestSequence Sequence);
    UPROPERTY(BlueprintAssignable, Category="Reditus|New Quests|Sequences") FReditusQuestSequenceEvent OnQuestSequenceFinished;

    // Only accepts an authored option in the quest's current stage. Immutable once chosen.
    UFUNCTION(BlueprintCallable, Category="Reditus|New Quests|Steps")
    bool RecordQuestChoice(FDataTableRowHandle Quest, FName ChoiceID, FName OptionID);
    UFUNCTION(BlueprintCallable, Category="Reditus|New Quests|Steps")
    bool GetQuestChoice(FDataTableRowHandle Quest, FName ChoiceID, FName& OptionID, int32& Value) const;
    UFUNCTION(BlueprintCallable, Category="Reditus|New Quests|Steps")
    TArray<FReditusQuestSavedChoice> GetQuestChoices(FDataTableRowHandle Quest) const;
    // Uses the SAME delivery handler/confirmation nodes as final turn-in.
    UFUNCTION(BlueprintCallable, Category="Reditus|New Quests|Steps")
    bool RequestQuestStepAction(FDataTableRowHandle Quest, FName ActionID, APlayerController* Player);
    UFUNCTION(BlueprintPure, Category="Reditus|New Quests|Steps")
    bool WasQuestStepActionApplied(FDataTableRowHandle Quest, FName ActionID) const;

    // Same event ID must be reused for duplicate notifications of ONE event.
    // Use a new ID for each distinct kill, interaction, etc.
    UFUNCTION(BlueprintCallable, Category="Reditus|New Quests")
    bool ReportQuestEvent(EReditusQuestObjectiveKind Kind, FName TargetID, FName UniqueEventID, int32 Amount = 1);

    // Call after inventory changes. Reads the existing per-category inventory
    // maps using the item row name; its table chooses the map. Never calls old quest APIs.
    UFUNCTION(BlueprintCallable, Category="Reditus|New Quests")
    bool RefreshQuestInventory(APlayerController* Player);
    // Kind is a compatibility input and is ignored; ItemRow determines inventory routing.
    UFUNCTION(BlueprintCallable, Category="Reditus|New Quests|Items")
    bool GetQuestItemCount(APlayerController* Player, FDataTableRowHandle ItemRow,
        EReditusQuestItemKind Kind, int32& Count) const;

    // Preview only. Rewards are received only through confirmed Turn In Quest delivery.
    UFUNCTION(BlueprintPure, Category="Reditus|New Quests|UI")
    FText GetQuestRewardSummary(FDataTableRowHandle Quest);

    UFUNCTION(BlueprintCallable, Category="Reditus|New Quests")
    bool ReadQuest(FDataTableRowHandle Quest, FReditusQuestSnapshot& Snapshot);
    UFUNCTION(BlueprintCallable, Category="Reditus|New Quests|Markers")
    bool GetQuestNPCMarkerState(FDataTableRowHandle Quest, FReditusQuestMarkerState& Marker);

    UFUNCTION(BlueprintCallable, Category="Reditus|New Quests")
    bool GetQuestDetails(FDataTableRowHandle Quest, FReditusQuestDefinition& Definition,
        FReditusQuestSnapshot& Snapshot, TArray<FReditusQuestObjectiveStatus>& Objectives);

    UFUNCTION(BlueprintPure, Category="Reditus|New Quests")
    TArray<FDataTableRowHandle> GetStartedQuests() const;

    // Empty deliveries finish directly. Costs/rewards request the one central BP
    // inventory/reward handler; they are never silently discarded.
    UFUNCTION(BlueprintCallable, Category="Reditus|New Quests")
    bool TurnInQuest(FDataTableRowHandle Quest, APlayerController* Player);
    // Fetch text mode may use the built-in item/gold exchange if no custom handler is bound.
    bool TurnInFetchQuest(FDataTableRowHandle Quest, APlayerController* Player);
    UFUNCTION(BlueprintCallable, Category="Reditus|New Quests")
    bool ConfirmQuestDelivery(FDataTableRowHandle Quest, FGuid ReceiptID);
    UFUNCTION(BlueprintCallable, Category="Reditus|New Quests")
    bool CancelQuestDelivery(FDataTableRowHandle Quest, FGuid ReceiptID);
    UFUNCTION(BlueprintPure, Category="Reditus|New Quests")
    TArray<FReditusQuestDelivery> GetPendingDeliveries() const;

    UFUNCTION(BlueprintPure, Category="Reditus|New Quests|Save")
    FReditusQuestSaveData ExportQuestState() const;
    UFUNCTION(BlueprintCallable, Category="Reditus|New Quests|Save")
    bool ImportQuestState(const FReditusQuestSaveData& Data, FText& Error);
    // Convenience nodes for the existing Blueprint save object. QuestState is a
    // variable of type Reditus Quest Save Data on BP_JRPGSaveGame.
    UFUNCTION(BlueprintCallable, Category="Reditus|New Quests|Save")
    bool WriteQuestStateToSave(USaveGame* SaveObject);
    UFUNCTION(BlueprintCallable, Category="Reditus|New Quests|Save")
    bool ReadQuestStateFromSave(USaveGame* SaveObject, FText& Error);
    UFUNCTION(BlueprintPure, Category="Reditus|New Quests|Save")
    bool CanSaveQuestState() const;
    UFUNCTION(BlueprintCallable, Category="Reditus|New Quests|Save")
    void ResetQuestStateForNewGame();

    UPROPERTY(BlueprintAssignable, Category="Reditus|New Quests") FReditusQuestChanged OnQuestChanged;
    UPROPERTY(BlueprintAssignable, Category="Reditus|New Quests") FReditusQuestDeliveryRequested OnQuestDeliveryRequested;
    UPROPERTY(BlueprintAssignable, Category="Reditus|New Quests") FReditusQuestStateRestored OnQuestStateRestored;
    UPROPERTY(BlueprintAssignable, Category="Reditus|New Quests|UI") FReditusQuestNotificationEvent OnQuestNotification;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Reditus|New Quests|UI") bool ShowQuestNotifications = true;

private:
    bool ApplyBuiltInFetchDelivery(APlayerController* Player, const FReditusQuestDelivery& Delivery, FString& Error);
    bool bAllowBuiltInFetchDelivery = false;
    UReditusQuestNPCComponent* FindNPCComponent(FName NPCID) const;
    friend class UReditusQuestUILibrary;
    bool CommitRecruitment(FName CharacterID, FName NPCID, bool RemoveNPC);
    bool EnsureDefinition(FDataTableRowHandle Quest, FString& Error, TSet<FString>& Visiting);
    bool Prepare(FDataTableRowHandle Quest);
    bool StartInternal(FDataTableRowHandle Quest, bool IgnorePrerequisites);
    void FlushChanges();
    void FinishInternal(FDataTableRowHandle Quest);
    bool BuildStepDelivery(FDataTableRowHandle Quest, FName ActionID, FReditusQuestDelivery& Delivery) const;
    void NotifyQuest(FDataTableRowHandle Quest, EReditusQuestNoticeKind Kind);
    void PumpSequences();
    void EndSequence(bool Completed);
    UFUNCTION() void SequenceFinished();
    UFUNCTION() void SequenceStopped();
    static FString Key(FDataTableRowHandle Quest);
    ReditusQuestRuntime::Engine Runtime;
    ReditusQuestWorld::State WorldProgress;
    UPROPERTY(Transient) TMap<FGuid, FReditusNPCBattleRequest> PendingNPCBattles;
    UPROPERTY(Transient) TMap<FString, FDataTableRowHandle> QuestRows;
    UPROPERTY(Transient) TMap<FString, FReditusQuestDelivery> Deliveries;
    UPROPERTY(Transient) TArray<FDataTableRowHandle> AddedNotices;
    UPROPERTY(Transient) TArray<FDataTableRowHandle> CompletedNotices;
    TWeakObjectPtr<UReditusQuestNotificationsWidget> NotificationView;
    UPROPERTY(Transient) TArray<UReditusQuestJournalBinding*> JournalBindings;
    UPROPERTY(Transient) TArray<FReditusQuestSequence> SequenceQueue;
    UPROPERTY(Transient) FReditusQuestSequence ActiveSequence;
    UPROPERTY(Transient) ULevelSequencePlayer* SequencePlayer = nullptr;
    UPROPERTY(Transient) ALevelSequenceActor* SequenceActor = nullptr;
    TWeakObjectPtr<UWorld> SequenceWorld;
    TWeakObjectPtr<UWorld> QueueWorld;
    TWeakObjectPtr<APlayerController> SequenceContextController;
    UPROPERTY(Transient) UObject* PreviousSequenceContext = nullptr;
    TMap<TWeakObjectPtr<AActor>, FName> DefeatEventIDs;
    bool bOwnSequenceActor = false;
    bool bEndingSequence = false;
    float InventoryRefreshSeconds = 0.f;
    bool bSubsystemStopped = false;
    bool bFlushing = false;
    bool bImporting = false;
    bool bApplyingNPCAction = false;
};
