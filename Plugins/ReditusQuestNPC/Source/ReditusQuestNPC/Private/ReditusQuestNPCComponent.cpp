#include "ReditusQuestNPCComponent.h"
#include "ReditusQuestSubsystem.h"
#include "ReditusQuestNPCPolicy.h"
#include "ReditusQuestNPCReflection.h"
#include "ReditusQuestSequenceGate.h"
#include "Dialogue.h"
#include "DialogueUserWidget.h"
#include "MapIconComponent.h"
#include "MapQuestMarkerLibrary.h"
#include "Components/WidgetComponent.h"
#include "PaperFlipbookComponent.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "TimerManager.h"
#include "Engine/World.h"
#include "EngineUtils.h"

DEFINE_LOG_CATEGORY_STATIC(LogReditusQuestNPC, Log, All);

UReditusQuestNPCComponent::UReditusQuestNPCComponent()
{
    PrimaryComponentTick.bCanEverTick = false;
}

const FReditusQuestNPCDefinition* UReditusQuestNPCComponent::ResolveDefinition() const
{
    if (!DefinitionRow.DataTable || DefinitionRow.RowName.IsNone()) return &InlineDefinition;
    if (DefinitionRow.DataTable->GetRowStruct() != FReditusQuestNPCDefinition::StaticStruct()) return nullptr;
    return DefinitionRow.DataTable->FindRow<FReditusQuestNPCDefinition>(DefinitionRow.RowName, TEXT("Quest NPC"), false);
}

void UReditusQuestNPCComponent::FindComponents()
{
    AActor* Owner = GetOwner();
    if (!Owner) return;
    Sprite = Owner->FindComponentByClass<UPaperFlipbookComponent>();
    MapIcon = Owner->FindComponentByClass<UMapIconComponent>();
    if (!MapIcon.IsValid() && HasBegunPlay() && Owner->GetRootComponent())
    {
        auto* Created = NewObject<UMapIconComponent>(Owner);
        Created->SetIconVisible(false);
        Created->SetupAttachment(Owner->GetRootComponent());
        Owner->AddInstanceComponent(Created); Created->RegisterComponent();
        MapIcon = Created;
    }
}

void UReditusQuestNPCComponent::OnRegister()
{
    Super::OnRegister();
    bHasDefinitionRow = DefinitionRow.DataTable && !DefinitionRow.RowName.IsNone();
    FindComponents();
    ApplyDefinition();
}

#if WITH_EDITOR
void UReditusQuestNPCComponent::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
    Super::PostEditChangeProperty(PropertyChangedEvent);
    bHasDefinitionRow = DefinitionRow.DataTable && !DefinitionRow.RowName.IsNone();
    ApplyDefinition();
}
#endif

void UReditusQuestNPCComponent::ApplyDefinition()
{
    if (!GetOwner()) return;
    FindComponents();
    const auto* Definition = ResolveDefinition();
    if (!Definition) return;
    if (Sprite.IsValid() && !bTalking)
    {
        Sprite->SetFlipbook(Definition->IdleFlipbook);
        Sprite->SetLooping(true);
        Sprite->Play();
    }
    if (auto* Text = FindFProperty<FTextProperty>(GetOwner()->GetClass(), TEXT("interactionText")))
        Text->SetPropertyValue_InContainer(GetOwner(), Definition->InteractionText);
    // No quest actions in construction, preview, loading, or refreshing markers.
    if (GetWorld() && GetWorld()->IsGameWorld() && HasBegunPlay()) RefreshQuestNPC();
}

APlayerController* UReditusQuestNPCComponent::ResolvePlayer() const
{
    auto* Player = Cast<APlayerController>(ReditusQuestNPCReflection::ReadObject(GetOwner(), TEXT("jRPGPlayerController")));
    return IsValid(Player) ? Player : UGameplayStatics::GetPlayerController(this, 0);
}

void UReditusQuestNPCComponent::BeginPlay()
{
    Super::BeginPlay();
    if (auto* System = UReditusQuestSubsystem::GetQuestSystem(this))
    {
        System->OnQuestStateRestored.AddUniqueDynamic(this, &UReditusQuestNPCComponent::HandlePersistentStateRestored);
        System->OnQuestChanged.AddUniqueDynamic(this, &UReditusQuestNPCComponent::HandleQuestMarkerChanged);
        System->OnNPCRemoved.AddUniqueDynamic(this, &UReditusQuestNPCComponent::HandleNPCRemoved);
        System->OnQuestSequenceFinished.AddUniqueDynamic(this, &UReditusQuestNPCComponent::HandleFollowUpSequenceFinished);
    }
    if (ApplyPersistentRemoval()) return;
    ApplyDefinition();
    auto* Player = ResolvePlayer();
    if (Player) ReditusQuestNPCReflection::WriteObject(GetOwner(), TEXT("jRPGPlayerController"), Player);
    const bool Bound = ReditusQuestNPCReflection::Bind(GetOwner(), TEXT("OnInteracted"), this,
                                                     TEXT("HandleInteracted"), true);
    ReditusQuestNPCReflection::Bind(GetOwner(), TEXT("OnLoaded"), this, TEXT("HandleLoaded"), true, true);
    if (!Bound) UE_LOG(LogReditusQuestNPC, Error, TEXT("%s needs BP_InteractionDetector as parent; OnInteracted could not be bound."), *GetOwner()->GetName());
    if (MapIcon.IsValid()) MapIcon->SetIconVisible(false);
    GetWorld()->GetTimerManager().SetTimer(RefreshTimer, this, &UReditusQuestNPCComponent::RefreshQuestNPC, .5f, true);
    RefreshQuestNPC();
}

TSubclassOf<AActor> UReditusQuestNPCComponent::GetNPCQuest() const
{
    return TSubclassOf<AActor>(); // Retained only for old Blueprint pin compatibility.
}

FDataTableRowHandle UReditusQuestNPCComponent::GetNPCQuestRow() const
{
    if (bTalking) return ConversationQuest;
    if (HasPendingBattleFollowUp()) return FollowUpQuest;
    const auto* Definition = ResolveDefinition();
    return Definition ? Definition->QuestRow : FDataTableRowHandle();
}

UDialogue* UReditusQuestNPCComponent::ChooseDialogue(APlayerController* Player, const FReditusQuestNPCDefinition& Definition, FDataTableRowHandle& SelectedQuest) const
{
    SelectedQuest = Definition.QuestRow;
    auto* System = UReditusQuestSubsystem::GetQuestSystem(Player);
    using namespace ReditusQuestNPCPolicy;
    for (const auto& Rule : Definition.ExtraRules)
    {
        FReditusQuestSnapshot Snapshot;
        const FDataTableRowHandle Quest = Rule.QuestRow.DataTable ? Rule.QuestRow : Definition.QuestRow;
        FName SelectedOption; int32 SelectedValue = 0;
        const bool ChoiceMatches = Rule.RequiredChoiceID.IsNone() || (System
            && System->GetQuestChoice(Quest, Rule.RequiredChoiceID, SelectedOption, SelectedValue)
            && (Rule.RequiredOptionID.IsNone() || Rule.RequiredOptionID == SelectedOption));
        if (Rule.Dialogue && ChoiceMatches && Quest.DataTable && System && System->ReadQuest(Quest, Snapshot)
            && RuleMatches(static_cast<State>(Snapshot.State), static_cast<State>(Rule.RequiredState), Rule.AnyKnownState,
                           Snapshot.Progress, Rule.MinimumProgress, Rule.MaximumProgressExclusive)
            && (Rule.RequiredStageID.IsNone() || Snapshot.StageID == Rule.RequiredStageID))
        {
            SelectedQuest = Quest;
            return Rule.Dialogue;
        }
    }
    FReditusQuestSnapshot Snapshot;
    UDialogue* Selected = nullptr;
    if (Definition.QuestRow.DataTable && System && System->ReadQuest(Definition.QuestRow, Snapshot))
    {
        switch (Snapshot.State)
        {
        case EReditusQuestState::Unavailable: Selected = Definition.UnavailableDialogue; break;
        case EReditusQuestState::Available: Selected = Definition.AvailableDialogue; break;
        case EReditusQuestState::Active: Selected = Definition.ActiveDialogue; break;
        case EReditusQuestState::ReadyToDeliver: Selected = Definition.ReadyToDeliverDialogue; break;
        case EReditusQuestState::Completed: Selected = Definition.CompletedDialogue; break;
        case EReditusQuestState::Failed: Selected = Definition.FailedDialogue; break;
        default: break;
        }
    }
    return Selected ? Selected : Definition.FallbackDialogue;
}

void UReditusQuestNPCComponent::HandleInteracted()
{
    InteractWithPlayer(ResolvePlayer());
}

void UReditusQuestNPCComponent::HandleLoaded(bool ShouldLoadEnemyPawns)
{
    (void)ShouldLoadEnemyPawns;
    if (ApplyPersistentRemoval()) return;
    // Ordinary talking NPCs persist. Quest state belongs to the new game-instance subsystem and exported save data.
    if (!bTalking) ReditusQuestNPCReflection::CallNoArguments(GetOwner(), TEXT("EnableInteraction"));
    RefreshQuestNPC();
}

void UReditusQuestNPCComponent::FacePlayer(APlayerController* Player, const FReditusQuestNPCDefinition& Definition)
{
    if (!Definition.FacePlayerWhenTalking || !Sprite.IsValid() || !Player || !Player->GetPawn()) return;
    const FVector Delta = Player->GetPawn()->GetActorLocation() - GetOwner()->GetActorLocation();
    UPaperFlipbook* Facing = nullptr;
    if (FMath::Abs(Delta.X) > FMath::Abs(Delta.Y)) Facing = Delta.X >= 0 ? Definition.EastFlipbook : Definition.WestFlipbook;
    else Facing = Delta.Y >= 0 ? Definition.IdleFlipbook : Definition.NorthFlipbook;
    if (Facing) { Sprite->SetFlipbook(Facing); Sprite->Play(); }
}

bool UReditusQuestNPCComponent::InteractWithPlayer(APlayerController* Player)
{
    if (bTalking || bClosing || bEndingPlay || bRemovalScheduled || ApplyPersistentRemoval() || !IsValid(Player) || !Player->IsLocalController()) return false;
    auto* QuestSystem = UReditusQuestSubsystem::GetQuestSystem(this);
    if (QuestSystem && QuestSystem->HasNPCFollowUps()) return false;
    if (!PendingEncounterID.IsNone() || (QuestSystem && QuestSystem->HasPendingNPCBattle(PersistentNPCID))) return false;
    const auto* Definition = ResolveDefinition();
    FDataTableRowHandle SelectedQuest;
    if (auto* System = UReditusQuestSubsystem::GetQuestSystem(Player)) System->RefreshQuestInventory(Player);
    UDialogue* Dialogue = nullptr;
    if (Definition && Definition->DialogueMode == EReditusQuestNPCDialogueMode::FetchQuestText)
    {
        SelectedQuest = Definition->QuestRow;
        Dialogue = BuildFetchQuestDialogue(Player, *Definition);
    }
    else if (Definition) Dialogue = ChooseDialogue(Player, *Definition, SelectedQuest);
    return OpenDialogue(Player, Dialogue, SelectedQuest, false);
}

bool UReditusQuestNPCComponent::OpenDialogue(APlayerController* Player, UDialogue* Dialogue, FDataTableRowHandle Quest, bool PostBattle)
{
    if (bEndingPlay || bTalking || !IsValid(Player) || !Player->IsLocalController()) return false;
    const auto* Definition = ResolveDefinition();
    const TSubclassOf<UDialogueUserWidget> WidgetClass = Definition && Definition->DialogueWidgetClass
        ? Definition->DialogueWidgetClass : DefaultDialogueWidgetClass;
    if (!Dialogue || !WidgetClass)
    {
        UE_LOG(LogReditusQuestNPC, Warning, TEXT("%s has no matching dialogue or dialogue widget. Check its NPC row."), *GetOwner()->GetName());
        ReditusQuestNPCReflection::CallNoArguments(GetOwner(), TEXT("EnableInteraction"));
        return false;
    }

    UDialogueUserWidget* Widget = CreateWidget<UDialogueUserWidget>(Player, WidgetClass);
    if (!Widget || !ReditusQuestNPCReflection::WriteObject(Widget, TEXT("InDialogue"), Dialogue)
        || !ReditusQuestNPCReflection::BindDialogueRemoved(Widget, this, true))
    {
        UE_LOG(LogReditusQuestNPC, Error, TEXT("%s widget must expose InDialogue and Removed with no arguments or one RecruitmentEvents enum."), *GetOwner()->GetName());
        ReditusQuestNPCReflection::CallNoArguments(GetOwner(), TEXT("EnableInteraction"));
        return false;
    }
    Widget->NPCActor = GetOwner();
    ActiveDialogueWidget = Widget;
    ConversationPlayer = Player;
    ConversationQuest = Quest;
    bPostBattleDialogue = PostBattle;
    bTalking = true; // Set before Construct, which may itself close or start a scene.
    if (Definition) FacePlayer(Player, *Definition);
    // The existing widget is responsible for executing the chosen dialogue's conditions/events.
    Widget->AddToViewport(50);
    if (bTalking && IsValid(ActiveDialogueWidget)) OnDialogueOpened.Broadcast(Dialogue);
    return true;
}

void UReditusQuestNPCComponent::HandleDialogueRemoved()
{
    FinishConversation(true, true);
}

void UReditusQuestNPCComponent::HandleDialogueRemovedWithResult(uint8 RecruitmentEvents)
{
    // Recruitment is committed by an explicit NPC action or the existing
    // success handler. Closing is cleanup; the enum is not another join action.
    (void)RecruitmentEvents;
    FinishConversation(true, true);
}

void UReditusQuestNPCComponent::FinishConversation(bool Notify, bool NormalClose)
{
    if (!bTalking || bClosing) return;
    bClosing = true;
    if (bPostBattleDialogue) FollowUpState.DialogueClosed(Notify && NormalClose);
    bPostBattleDialogue = false;
    if (IsValid(ActiveDialogueWidget))
        ReditusQuestNPCReflection::BindDialogueRemoved(ActiveDialogueWidget, this, false);
    ActiveDialogueWidget = nullptr;
    ActiveFetchDialogue = nullptr;
    ConversationPlayer.Reset();
    ConversationQuest = FDataTableRowHandle();
    bTalking = false;
    if (!bEndingPlay)
    {
        ReditusQuestNPCReflection::CallNoArguments(GetOwner(), TEXT("EnableInteraction"));
        ApplyDefinition();
        if (Notify) OnDialogueClosed.Broadcast();
    }
    bClosing = false;
    if (Notify && !bEndingPlay && !PendingEncounterID.IsNone() && GetWorld())
    {
        // Removed may still be dispatching other listeners' input/UI cleanup.
        // Submit on the next tick so those listeners finish before battle starts.
        BattleSubmitTimer = GetWorld()->GetTimerManager().SetTimerForNextTick(this, &UReditusQuestNPCComponent::SubmitPendingNPCBattle);
    }
    else if (!Notify || bEndingPlay)
    { PendingEncounterID = NAME_None; PendingBattlePlayer.Reset(); PendingBattleQuest = FDataTableRowHandle(); bRemoveAfterPendingBattle = false; }
}

void UReditusQuestNPCComponent::RefreshQuestNPC()
{
    if (bEndingPlay) return;
    if (bTalking && (!IsValid(ActiveDialogueWidget) || !ActiveDialogueWidget->IsInViewport())) FinishConversation(true);
    PumpBattleFollowUp();
    if (ApplyPersistentRemoval()) return;
    FindComponents();
    const auto* Definition = ResolveDefinition();
    auto* System = UReditusQuestSubsystem::GetQuestSystem(this);
    FReditusQuestMarkerState Marker;
    // Markers belong to the attached quest, independently of dialogue selection.
    if (Definition && System) System->GetQuestNPCMarkerState(Definition->QuestRow, Marker);
    UMapQuestMarkerLibrary::ApplyQuestMapIcon(MapIcon.Get(), Marker.MinimapVisible, Marker.IsMainQuest, Marker.Title);
    if (!IsValid(GetOwner())) return;
    FVector Top = GetOwner()->GetActorLocation() + FVector(0, 0, 96);
    if (Sprite.IsValid() && Sprite->GetFlipbook())
        Top = Sprite->Bounds.Origin + FVector(0, 0, Sprite->Bounds.BoxExtent.Z);
    OverheadQuestMarker = UMapQuestMarkerLibrary::UpdateQuestHead(GetOwner(), OverheadQuestMarker,
        Top, Marker.OverheadVisible, Marker.IsMainQuest);
}

void UReditusQuestNPCComponent::HandleQuestMarkerChanged(FDataTableRowHandle Quest)
{
    (void)Quest; // Other quests may unlock this quest's prerequisites.
    RefreshQuestNPC();
}

void UReditusQuestNPCComponent::EndPlay(const EEndPlayReason::Type Reason)
{
    const auto* QuestSystem = UReditusQuestSubsystem::GetQuestSystem(this);
    if (Reason == EEndPlayReason::Destroyed && (HasPendingBattleFollowUp()
        || (QuestSystem && QuestSystem->HasPendingNPCBattle(PersistentNPCID))))
        UE_LOG(LogReditusQuestNPC, Warning, TEXT("QuestNPC %s was destroyed before its post-battle follow-up finished. Direct Destroy Actor/Set Life Span bypasses deferred removal; keep No Destroy enabled on the encounter and use the quest removal command."), *PersistentNPCID.ToString());
    bEndingPlay = true;
    FinishBattleFollowUp(false, false);
    if (GetWorld())
    {
        GetWorld()->GetTimerManager().ClearTimer(RefreshTimer);
        GetWorld()->GetTimerManager().ClearTimer(RemovalTimer);
        GetWorld()->GetTimerManager().ClearTimer(BattleSubmitTimer);
    }
    if (auto* System = UReditusQuestSubsystem::GetQuestSystem(this))
    {
        System->OnQuestStateRestored.RemoveDynamic(this, &UReditusQuestNPCComponent::HandlePersistentStateRestored);
        System->OnQuestChanged.RemoveDynamic(this, &UReditusQuestNPCComponent::HandleQuestMarkerChanged);
        System->OnNPCRemoved.RemoveDynamic(this, &UReditusQuestNPCComponent::HandleNPCRemoved);
        System->OnQuestSequenceFinished.RemoveDynamic(this, &UReditusQuestNPCComponent::HandleFollowUpSequenceFinished);
    }
    ReditusQuestNPCReflection::Bind(GetOwner(), TEXT("OnInteracted"), this, TEXT("HandleInteracted"), false);
    ReditusQuestNPCReflection::Bind(GetOwner(), TEXT("OnLoaded"), this, TEXT("HandleLoaded"), false, true);
    UDialogueUserWidget* Widget = ActiveDialogueWidget;
    FinishConversation(false);
    if (IsValid(Widget)) Widget->RemoveFromParent();
    if (IsValid(OverheadQuestMarker)) OverheadQuestMarker->DestroyComponent();
    OverheadQuestMarker = nullptr;
    Super::EndPlay(Reason);
}


bool UReditusQuestNPCComponent::ApplyPersistentRemoval()
{
    auto* System = UReditusQuestSubsystem::GetQuestSystem(this);
    if (!System || !System->WasNPCRemoved(PersistentNPCID) || !IsValid(GetOwner())) return false;
    if (!ValidatePersistentID()) return false;
    if (MapIcon.IsValid()) MapIcon->SetIconVisible(false);
    if (IsValid(OverheadQuestMarker)) OverheadQuestMarker->SetVisibility(false);
    // Recheck even when a previous event already scheduled destruction. A battle
    // result can establish its follow-up later in that same frame.
    if (bTalking || HasPendingBattleFollowUp() || System->IsNPCConversationActive(PersistentNPCID))
    {
        if (bRemovalScheduled)
        {
            if (GetWorld()) GetWorld()->GetTimerManager().ClearTimer(RemovalTimer);
            bRemovalScheduled = false;
            GetOwner()->SetActorHiddenInGame(bHiddenBeforeRemoval);
            GetOwner()->SetActorEnableCollision(bCollisionBeforeRemoval);
        }
        return true;
    }
    if (!bRemovalScheduled && GetWorld())
    {
        bHiddenBeforeRemoval = GetOwner()->IsHidden();
        bCollisionBeforeRemoval = GetOwner()->GetActorEnableCollision();
        GetOwner()->SetActorHiddenInGame(true); GetOwner()->SetActorEnableCollision(false);
        bRemovalScheduled = true;
        RemovalTimer = GetWorld()->GetTimerManager().SetTimerForNextTick(this, &UReditusQuestNPCComponent::DestroyRememberedNPC);
    }
    return true;
}
void UReditusQuestNPCComponent::DestroyRememberedNPC()
{
    if (!IsValid(GetOwner()) || bEndingPlay) return;
    auto* System = UReditusQuestSubsystem::GetQuestSystem(this);
    if (System && System->WasNPCRemoved(PersistentNPCID))
    {
        if (bTalking || HasPendingBattleFollowUp() || System->IsNPCConversationActive(PersistentNPCID))
        { ApplyPersistentRemoval(); return; }
        GetOwner()->Destroy();
    }
    else
    {
        bRemovalScheduled = false;
        GetOwner()->SetActorHiddenInGame(bHiddenBeforeRemoval);
        GetOwner()->SetActorEnableCollision(bCollisionBeforeRemoval);
        RefreshQuestNPC();
    }
}
void UReditusQuestNPCComponent::HandlePersistentStateRestored()
{
    if (ApplyPersistentRemoval() || !IsValid(GetOwner())) return;
    if (bRemovalScheduled)
    {
        if (GetWorld()) GetWorld()->GetTimerManager().ClearTimer(RemovalTimer);
        bRemovalScheduled = false;
        GetOwner()->SetActorHiddenInGame(bHiddenBeforeRemoval);
        GetOwner()->SetActorEnableCollision(bCollisionBeforeRemoval);
    }
    RefreshQuestNPC();
}
void UReditusQuestNPCComponent::HandleNPCRemoved(FName NPCID)
{ if (!PersistentNPCID.IsNone() && PersistentNPCID == NPCID) ApplyPersistentRemoval(); }

bool UReditusQuestNPCComponent::RemoveNPCPermanently(EReditusNPCRemovalReason Reason)
{
    auto* System = UReditusQuestSubsystem::GetQuestSystem(this);
    if (bEndingPlay || !System || !ValidatePersistentID())
    { UE_LOG(LogReditusQuestNPC, Warning, TEXT("Assign a unique Persistent NPC ID before removing an NPC permanently.")); return false; }
    return System->RemoveNPCPermanently(PersistentNPCID, Reason);
}

bool UReditusQuestNPCComponent::RecruitNPCToParty(APlayerController* Player)
{
    const auto* Definition = ResolveDefinition();
    auto* System = UReditusQuestSubsystem::GetQuestSystem(this);
    if (bEndingPlay || !Definition || !System) return false;
    if (Definition->RemoveNPCOnRecruitment && !ValidatePersistentID()) return false;
    return System->RecruitQuestNPC(Player ? Player : ResolvePlayer(), Definition->RecruitedCharacter,
        Definition->RecruitmentStartingLevel, Definition->AddRecruitToActivePartyIfSpace,
        PersistentNPCID, Definition->RemoveNPCOnRecruitment);
}

bool UReditusQuestNPCComponent::StartNPCBattle(APlayerController* Player, FName EncounterIDOverride)
{
    if (PersistentNPCID.IsNone())
    {
        UE_LOG(LogReditusQuestNPC, Error, TEXT("%s cannot start an NPC battle: set a unique Persistent NPC ID on its QuestNPC component."), *GetNameSafe(GetOwner()));
        return false;
    }
    const auto* Definition = ResolveDefinition();
    auto* System = UReditusQuestSubsystem::GetQuestSystem(this);
    Player = Player ? Player : ResolvePlayer();
    const FName Encounter = !EncounterIDOverride.IsNone() ? EncounterIDOverride : (Definition ? Definition->EncounterID : NAME_None);
    if (bEndingPlay || !Definition || !System || !IsValid(Player) || !ValidatePersistentID()
        || Encounter.IsNone() || !PendingEncounterID.IsNone()
        || System->WasNPCRemoved(PersistentNPCID) || System->HasPendingNPCBattle(PersistentNPCID)
        || !System->OnNPCBattleRequested.IsBound())
    { UE_LOG(LogReditusQuestNPC, Warning, TEXT("Check Persistent NPC ID, Encounter ID and the central On NPC Battle Requested binding.")); return false; }
    PendingBattlePlayer = Player; PendingEncounterID = Encounter;
    PendingBattleQuest = GetNPCQuestRow();
    bRemoveAfterPendingBattle = Definition->RemoveNPCOnBattleVictory;
    if (bPostBattleDialogue) FollowUpState.ApproveDialogue(); // An explicit next-battle node is a committed branch.
    if (!bTalking && !HasPendingBattleFollowUp() && GetWorld())
        BattleSubmitTimer = GetWorld()->GetTimerManager().SetTimerForNextTick(this, &UReditusQuestNPCComponent::SubmitPendingNPCBattle);
    return true;
}

void UReditusQuestNPCComponent::SubmitPendingNPCBattle()
{
    if (bEndingPlay || PendingEncounterID.IsNone()) return;
    if (bTalking || HasPendingBattleFollowUp()) return;
    if (!GetWorld() || !PendingBattlePlayer.IsValid() || PendingBattlePlayer->GetWorld() != GetWorld())
    {
        PendingEncounterID = NAME_None; PendingBattlePlayer.Reset(); PendingBattleQuest = FDataTableRowHandle();
        bRemoveAfterPendingBattle = false; return;
    }
    auto* System = UReditusQuestSubsystem::GetQuestSystem(this);
    if (System && (System->HasNPCDialogueOpen() || System->HasNPCFollowUps() || System->HasQueuedQuestSequences()
        || !ReditusQuestSequenceGate::IsExploring(PendingBattlePlayer.Get())))
    {
        GetWorld()->GetTimerManager().SetTimer(BattleSubmitTimer, this, &UReditusQuestNPCComponent::SubmitPendingNPCBattle, .1f, false);
        return;
    }
    auto* Player = PendingBattlePlayer.Get(); const FName Encounter = PendingEncounterID;
    const bool RemoveAfterVictory = bRemoveAfterPendingBattle;
    LastBattleQuest = PendingBattleQuest; PendingBattleQuest = FDataTableRowHandle();
    PendingEncounterID = NAME_None; PendingBattlePlayer.Reset(); bRemoveAfterPendingBattle = false;
    if (!System || !System->RequestNPCBattle(Player, PersistentNPCID, Encounter, RemoveAfterVictory, LastBattleRequestID))
        UE_LOG(LogReditusQuestNPC, Warning, TEXT("NPC battle request did not start. Bind the central battle handler and check the NPC/encounter IDs."));
}

bool UReditusQuestNPCComponent::ValidatePersistentID()
{
    if (PersistentNPCID.IsNone() || !GetWorld()) return false;
    int32 Count = 0;
    for (TActorIterator<AActor> It(GetWorld()); It; ++It)
    {
        TArray<UReditusQuestNPCComponent*> Components;
        It->GetComponents<UReditusQuestNPCComponent>(Components);
        for (auto* Component : Components)
            if (Component && Component->PersistentNPCID == PersistentNPCID) ++Count;
    }
    if (Count == 1) return true;
    if (!bIdentityWarningLogged)
    {
        bIdentityWarningLogged = true;
        UE_LOG(LogReditusQuestNPC, Error, TEXT("Duplicate Persistent NPC ID %s in this world. Give each placed NPC its own ID; removal/recruitment/battle was refused."), *PersistentNPCID.ToString());
    }
    return false;
}
