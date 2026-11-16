#include "ReditusQuestSubsystem.h"
#include "ReditusQuestPartyFacade.h"
#include "ReditusQuestNPCComponent.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"

namespace
{
    std::string PersistentID(FName ID) { return std::string(TCHAR_TO_UTF8(*ID.ToString().ToLower())); }
}

bool UReditusQuestSubsystem::WasNPCRemoved(FName NPCID) const
{ return !NPCID.IsNone() && WorldProgress.IsRemoved(PersistentID(NPCID)); }

bool UReditusQuestSubsystem::IsNPCConversationActive(FName NPCID) const
{
    UWorld* World = GetTickableGameObjectWorld();
    if (!World) return false;
    // Removal must also wait between dialogue close, battle submission and the real result.
    for (const auto& Pair : PendingNPCBattles)
        if ((NPCID.IsNone() || Pair.Value.NPCID == NPCID) && Pair.Value.RequestWorld.Get() == World) return true;
    for (TActorIterator<AActor> It(World); It; ++It)
        if (auto* NPC = It->FindComponentByClass<UReditusQuestNPCComponent>())
            if ((NPCID.IsNone() || NPC->PersistentNPCID == NPCID)
                && (NPC->IsTalking() || NPC->HasPendingBattleFollowUp() || !NPC->PendingEncounterID.IsNone())) return true;
    return false;
}

bool UReditusQuestSubsystem::RemoveNPCPermanently(FName NPCID, EReditusNPCRemovalReason Reason)
{
    if (bImporting || NPCID.IsNone() || uint8(Reason) > uint8(EReditusNPCRemovalReason::Recruited)) return false;
    if (!WorldProgress.MarkRemoved(PersistentID(NPCID), static_cast<ReditusQuestWorld::Removal>(Reason))) return false;
    OnNPCRemoved.Broadcast(NPCID); return true;
}

bool UReditusQuestSubsystem::RememberNPCRemoved(FName NPCID, EReditusNPCRemovalReason Reason)
{ return RemoveNPCPermanently(NPCID, Reason); }

bool UReditusQuestSubsystem::CommitRecruitment(FName CharacterID, FName NPCID, bool RemoveNPC)
{
    if (!WorldProgress.RecordRecruitment(PersistentID(CharacterID))) return false;
    // Commit removal BEFORE progress/notification callbacks. Save writes remain
    // gated by bApplyingNPCAction until this entire operation has completed.
    if (RemoveNPC) RemoveNPCPermanently(NPCID, EReditusNPCRemovalReason::Recruited);
    ReportQuestEvent(EReditusQuestObjectiveKind::RecruitUnit, CharacterID,
        FName(*(TEXT("Recruit:") + CharacterID.ToString())), 1);
    return true;
}

bool UReditusQuestSubsystem::RecruitQuestNPC(APlayerController* Player, FDataTableRowHandle Character,
    int32 StartingLevel, bool AddToPartyIfSpace, FName NPCID, bool RemoveNPC)
{
    if (bImporting || bApplyingNPCAction || !IsValid(Player) || !Player->IsLocalController()
        || !Character.DataTable || Character.RowName.IsNone() || StartingLevel < 1
        || !Character.DataTable->GetRowMap().Contains(Character.RowName)
        || (RemoveNPC && NPCID.IsNone()) || WasNPCRemoved(NPCID)
        || WorldProgress.HasRecruitment(PersistentID(Character.RowName))) return false;
    // The exported GetPartyMemberDefinition function used by UnlockCharacterAtLevel
    // reads this exact existing catalog. Reject unrelated item/enemy rows.
    if (!Character.DataTable->GetPathName().Equals(TEXT("/Game/TurnBasedJRPGTemplate/Blueprints/DataTable/DT_PartyMembers.DT_PartyMembers"), ESearchCase::IgnoreCase))
    { UE_LOG(LogTemp, Warning, TEXT("Recruitment must select a row from the existing DT_PartyMembers catalog.")); return false; }
    TGuardValue<bool> Guard(bApplyingNPCAction, true);
    if (!ReditusQuestPartyFacade::Recruit(Player, Character.RowName, StartingLevel, AddToPartyIfSpace, ActivePartyLimit))
    { UE_LOG(LogTemp, Warning, TEXT("NPC recruitment failed. Check the existing UnlockCharacterAtLevel/party facade, row and current roster. No quest success/removal was recorded.")); return false; }
    return CommitRecruitment(Character.RowName, NPCID, RemoveNPC);
}

bool UReditusQuestSubsystem::ConfirmQuestRecruitment(APlayerController* Player, FName CharacterID, FName NPCID, bool RemoveNPC)
{
    if (bImporting || bApplyingNPCAction || CharacterID.IsNone() || (RemoveNPC && NPCID.IsNone())
        || WasNPCRemoved(NPCID) || !ReditusQuestPartyFacade::Owned(Player, CharacterID)
        || WorldProgress.HasRecruitment(PersistentID(CharacterID))) return false;
    TGuardValue<bool> Guard(bApplyingNPCAction, true);
    return CommitRecruitment(CharacterID, NPCID, RemoveNPC);
}

bool UReditusQuestSubsystem::HasPendingNPCBattle(FName NPCID) const
{
    for (const auto& Pair : PendingNPCBattles) if (Pair.Value.NPCID == NPCID) return true;
    return false;
}

bool UReditusQuestSubsystem::RequestNPCBattle(APlayerController* Player, FName NPCID, FName EncounterID,
    bool RemoveNPCOnVictory, FGuid& RequestID)
{
    RequestID.Invalidate();
    if (bImporting || bApplyingNPCAction || !IsValid(Player) || !Player->IsLocalController() || NPCID.IsNone() || EncounterID.IsNone()
        || Player->GetWorld() != GetTickableGameObjectWorld()
        || WasNPCRemoved(NPCID) || PendingNPCBattles.Num() > 0 || HasNPCFollowUps() || HasNPCDialogueOpen()
        || !OnNPCBattleRequested.IsBound()) return false;
    FReditusNPCBattleRequest Request;
    Request.RequestID = FGuid::NewGuid(); Request.NPCID = NPCID; Request.EncounterID = EncounterID;
    Request.Player = Player; Request.RemoveNPCOnVictory = RemoveNPCOnVictory;
    Request.RequestWorld = GetTickableGameObjectWorld();
    RequestID = Request.RequestID; PendingNPCBattles.Add(Request.RequestID, Request);
    OnNPCBattleRequested.Broadcast(Request);
    return true;
}

bool UReditusQuestSubsystem::ResolveNPCBattle(FGuid RequestID, EReditusNPCBattleResult Result)
{
    const auto* Found = PendingNPCBattles.Find(RequestID);
    if (bImporting || bApplyingNPCAction || !Found || uint8(Result) > uint8(EReditusNPCBattleResult::Cancelled))
    {
        UE_LOG(LogTemp, Warning, TEXT("NPC battle result rejected: Request ID must be the pending request from On NPC Battle Requested, and resolve must run once after the real result."));
        return false;
    }
    if (Found->RequestWorld.Get() != GetTickableGameObjectWorld())
    {
        PendingNPCBattles.Remove(RequestID);
        UE_LOG(LogTemp, Warning, TEXT("NPC battle result belongs to a previous world; no story progression/removal was applied."));
        return false;
    }
    const auto Request = *Found;
    TGuardValue<bool> Guard(bApplyingNPCAction, true);
    PendingNPCBattles.Remove(RequestID); // Duplicate finish callbacks cannot resolve it twice.
    auto* NPC = Request.RequestWorld.Get() == GetTickableGameObjectWorld() ? FindNPCComponent(Request.NPCID) : nullptr;
    const bool FollowUpOwnsRemoval = NPC && NPC->BeginBattleFollowUp(Request, Result);
    if (Result == EReditusNPCBattleResult::Won)
    {
        if (Request.RemoveNPCOnVictory && !FollowUpOwnsRemoval) RemoveNPCPermanently(Request.NPCID, EReditusNPCRemovalReason::Defeated);
        ReportQuestEvent(EReditusQuestObjectiveKind::WinBattle, Request.EncounterID, FName(*RequestID.ToString()), 1);
    }
    OnNPCBattleResolved.Broadcast(Request, Result);
    return true;
}
