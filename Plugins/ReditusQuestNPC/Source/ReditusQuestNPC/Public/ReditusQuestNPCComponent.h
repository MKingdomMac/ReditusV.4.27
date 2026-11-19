#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "ReditusQuestNPCTypes.h"
#include "ReditusQuestSystemTypes.h"
#include "TimerManager.h"
#include "ReditusNPCFollowUpPolicy.h"
#include "ReditusFetchQuestFlow.h"
#include "ReditusQuestNPCComponent.generated.h"

class APlayerController;
class UDialogue;
class UDialogueUserWidget;
class UMapIconComponent;
class UPaperFlipbookComponent;
class UTexture2D;
class UWidgetComponent;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FReditusNPCDialogueOpened, UDialogue*, Dialogue);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FReditusNPCDialogueClosed);

UCLASS(BlueprintType, Blueprintable, ClassGroup=(Reditus), meta=(BlueprintSpawnableComponent, DisplayName="Reditus Quest NPC"))
class REDITUSQUESTNPC_API UReditusQuestNPCComponent : public UActorComponent
{
    GENERATED_BODY()

public:
    UReditusQuestNPCComponent();

    // Unique across the game. Set separately on each placed/spawned NPC.
    // Never use a generated runtime actor name or a shared DataTable row as identity.
    UPROPERTY(EditInstanceOnly, BlueprintReadWrite, Category="Quest NPC|Persistence", meta=(DisplayName="Persistent NPC ID"))
    FName PersistentNPCID;

    UFUNCTION(BlueprintCallable, Category="Reditus|Quest NPC|Persistence", meta=(BlueprintInternalUseOnly="true", DeprecatedFunction, DeprecationMessage="Use Get Quest System -> Remove NPC Permanently with this component's Persistent NPC ID."))
    bool RemoveNPCPermanently(EReditusNPCRemovalReason Reason = EReditusNPCRemovalReason::Removed);
    UFUNCTION(BlueprintCallable, Category="Reditus|Quest NPC|Recruitment")
    bool RecruitNPCToParty(APlayerController* Player);
    UFUNCTION(BlueprintCallable, Category="Reditus|Quest NPC|Battle")
    bool StartNPCBattle(APlayerController* Player, FName EncounterIDOverride = NAME_None);

    // Authored terminal dialogue event; closure by itself is not story success.
    UFUNCTION(BlueprintCallable, Category="Reditus|Quest NPC|Battle")
    bool CompletePostBattleDialogue();

    UFUNCTION(BlueprintPure, Category="Reditus|Quest NPC|Battle")
    bool HasPendingBattleFollowUp() const { return FollowUpState.Busy(); }

    // Used by the generated shared fetch flow, not authored per-node graphs.
    ReditusFetchQuestFlow::Phase GetFetchQuestPhase(APlayerController* Player) const;
    bool ApplyFetchQuestAction(APlayerController* Player, ReditusFetchQuestFlow::Action Action);
    // Supplies localized fetch-NPC name only to this component's active widget.
    bool ReadFetchSpeakerName(const UDialogueUserWidget* Widget, FText& Name) const;

    // Assign a DT_QuestNPCs row. An unselected row uses InlineDefinition.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Quest NPC", meta=(RowType="ReditusQuestNPCDefinition"))
    FDataTableRowHandle DefinitionRow;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Quest NPC", meta=(DisplayName="Fallback Definition", EditCondition="!bHasDefinitionRow", EditConditionHides, ToolTip="Used only when Definition Row is empty. A selected DT_QuestNPCs row is the source of settings."))
    FReditusQuestNPCDefinition InlineDefinition;

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Quest NPC|Integration")
    TSubclassOf<UDialogueUserWidget> DefaultDialogueWidgetClass;

    UPROPERTY(BlueprintReadOnly, Category="Retired Marker Settings", meta=(DeprecatedProperty, DeprecationMessage="Quest type comes from DT_Quests; appearance comes from MinimapPlugin."))
    UTexture2D* MainQuestIcon = nullptr;

    UPROPERTY(BlueprintReadOnly, Category="Retired Marker Settings", meta=(DeprecatedProperty, DeprecationMessage="Quest type comes from DT_Quests; appearance comes from MinimapPlugin."))
    UTexture2D* SideQuestIcon = nullptr;

    UFUNCTION(BlueprintCallable, Category="Reditus|Quest NPC")
    void ApplyDefinition();

    UFUNCTION(BlueprintCallable, Category="Reditus|Quest NPC")
    bool InteractWithPlayer(APlayerController* Player);

    UFUNCTION(BlueprintCallable, Category="Reditus|Quest NPC")
    void RefreshQuestNPC();

    UFUNCTION(BlueprintPure, Category="Reditus|Quest NPC", meta=(DeprecatedFunction, DeprecationMessage="Use GetNPCQuestRow. Old quest classes are disabled."))
    TSubclassOf<AActor> GetNPCQuest() const;

    UFUNCTION(BlueprintPure, Category="Reditus|Quest NPC")
    FDataTableRowHandle GetNPCQuestRow() const;

    UFUNCTION(BlueprintPure, Category="Reditus|Quest NPC")
    bool IsTalking() const { return bTalking; }

    UPROPERTY(BlueprintAssignable, Category="Reditus|Quest NPC")
    FReditusNPCDialogueOpened OnDialogueOpened;

    // Presentation closed; this is NOT confirmation of a quest, item or recruitment choice.
    UPROPERTY(BlueprintAssignable, Category="Reditus|Quest NPC")
    FReditusNPCDialogueClosed OnDialogueClosed;

protected:
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
    virtual void OnRegister() override;
#if WITH_EDITOR
    virtual void PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

private:
    friend class UReditusQuestSubsystem;
    UDialogue* BuildFetchQuestDialogue(APlayerController* Player, const FReditusQuestNPCDefinition& Definition);
    bool BeginBattleFollowUp(const FReditusNPCBattleRequest& Request, EReditusNPCBattleResult Result);
    bool OpenDialogue(APlayerController* Player, UDialogue* Dialogue, FDataTableRowHandle Quest, bool PostBattle);
    void PumpBattleFollowUp();
    void FinishBattleFollowUp(bool Completed, bool Notify = true);
    void HoldEncounterSource(FName EncounterID);
    void ReleaseEncounterSource();
    UFUNCTION() void HandleFollowUpSequenceFinished(FReditusQuestSequence Sequence, bool Completed);
    UPROPERTY(Transient)
    bool bHasDefinitionRow = false;
    UFUNCTION() void HandleInteracted();
    UFUNCTION() void HandleLoaded(bool ShouldLoadEnemyPawns);
    UFUNCTION() void HandleDialogueRemoved();
    UFUNCTION() void HandleDialogueRemovedWithResult(uint8 RecruitmentEvents);
    UFUNCTION() void HandlePersistentStateRestored();
    UFUNCTION() void HandleQuestMarkerChanged(FDataTableRowHandle Quest);
    UFUNCTION() void HandleNPCRemoved(FName NPCID);
    UFUNCTION() void DestroyRememberedNPC();
    UFUNCTION() void SubmitPendingNPCBattle();
    bool ApplyPersistentRemoval();
    bool ValidatePersistentID();

    const FReditusQuestNPCDefinition* ResolveDefinition() const;
    APlayerController* ResolvePlayer() const;
    UDialogue* ChooseDialogue(APlayerController* Player, const FReditusQuestNPCDefinition& Definition, FDataTableRowHandle& SelectedQuest) const;
    void FindComponents();
    void FinishConversation(bool Notify, bool NormalClose = false);
    void FacePlayer(APlayerController* Player, const FReditusQuestNPCDefinition& Definition);

    TWeakObjectPtr<UPaperFlipbookComponent> Sprite;
    TWeakObjectPtr<UMapIconComponent> MapIcon;
    TWeakObjectPtr<APlayerController> ConversationPlayer;

    UPROPERTY(Transient)
    UDialogueUserWidget* ActiveDialogueWidget = nullptr;

    UPROPERTY(Transient)
    UDialogue* ActiveFetchDialogue = nullptr;
    bool bFetchActionFailed = false;

    UPROPERTY(Transient)
    UWidgetComponent* OverheadQuestMarker = nullptr;

    UPROPERTY(Transient)
    FDataTableRowHandle ConversationQuest;

    FTimerHandle RefreshTimer;
    FTimerHandle RemovalTimer;
    FTimerHandle BattleSubmitTimer;
    TWeakObjectPtr<APlayerController> PendingBattlePlayer;
    FName PendingEncounterID;
    bool bRemoveAfterPendingBattle = false;
    bool bRemovalScheduled = false;
    bool bIdentityWarningLogged = false;
    bool bHiddenBeforeRemoval = false;
    bool bCollisionBeforeRemoval = true;
    bool bTalking = false;
    bool bPostBattleDialogue = false;
    ReditusNPCFollowUpPolicy::State FollowUpState;
    UPROPERTY(Transient) FReditusNPCBattleFollowUp ActiveFollowUp;
    UPROPERTY(Transient) FReditusNPCBattleRequest FollowUpRequest;
    EReditusNPCBattleResult FollowUpResult = EReditusNPCBattleResult::Won;
    UPROPERTY(Transient) FDataTableRowHandle FollowUpQuest;
    UPROPERTY(Transient) FDataTableRowHandle PendingBattleQuest;
    UPROPERTY(Transient) FDataTableRowHandle LastBattleQuest;
    FGuid LastBattleRequestID;
    TWeakObjectPtr<AActor> HeldEncounterSource;
    bool bSourcePreviouslyStarting = false;
    bool bFinishingFollowUp = false;
    bool bClosing = false;
    bool bEndingPlay = false;
};
