#include "ReditusEnemyCombatAIComponent.h"

#include "GameFramework/Actor.h"
#include "ReditusTableBuffQueries.h"
#include "ReditusTableBuffState.h"
#include "UObject/EnumProperty.h"
#include "UObject/StructOnScope.h"

#include <limits>

using namespace ReditusTableBuffQueries;

namespace
{
    using EComparison = EReditusEnemyAIComparison;
    using ETest = EReditusEnemyAITestType;
    using ESubject = EReditusEnemyAISubject;

    bool IsStaggeredForEnemyTargeting(UActorComponent* Clock, AActor* Unit)
    {
        if (!IsValid(Unit)) return false;
        // Use the same state query that holds/restarts the affected unit's ATB
        // gauge. The plugin cannot link against the game module directly.
        UFunction* Function = IsValid(Clock) ? Clock->FindFunction(TEXT("IsUnitStaggered")) : nullptr;
        const auto* UnitParam = Function ? InputProperty<FObjectPropertyBase>(Function, TEXT("Unit")) : nullptr;
        const auto* Return = Function ? CastField<FBoolProperty>(Function->GetReturnProperty()) : nullptr;
        if (Function && Function->NumParms == 2 && UnitParameter(UnitParam, Unit) &&
            !UnitParam->HasAnyPropertyFlags(CPF_OutParm) && Return)
        {
            FStructOnScope Params(Function);
            void* Memory = Params.GetStructMemory();
            UnitParam->SetObjectPropertyValue_InContainer(Memory, Unit);
            Clock->ProcessEvent(Function, Memory);
            return Return->GetPropertyValue_InContainer(Memory);
        }

        // Legacy/non-ATB battles: distinguish stamina break from generic stun.
        if (ReadBool(Unit, TEXT("Staggered"))) return true;
        if (!ReadBool(Unit, TEXT("isStun"))) return false;
        const auto* Property = FindFProperty<FMapProperty>(Unit->GetClass(), TEXT("activeBuffs"));
        const auto* Value = Property ? CastField<FObjectPropertyBase>(Property->ValueProp) : nullptr;
        if (!Value) return false;
        FScriptMapHelper Buffs(Property, Property->ContainerPtrToValuePtr<void>(Unit));
        for (int32 Index = 0; Index < Buffs.GetMaxIndex(); ++Index)
        {
            if (!Buffs.IsValidIndex(Index)) continue;
            auto* Buff = Cast<AActor>(Value->GetObjectPropertyValue(Buffs.GetValuePtr(Index)));
            if (!IsValid(Buff) || ReadObject(Buff, TEXT("targetUnit")) != Unit) continue;
            bool bStagger = false;
            for (auto* Type = Buff->GetClass(); Type; Type = Type->GetSuperClass())
                if (Type->GetPathName() == TEXT("/Game/TurnBasedJRPGTemplate/Blueprints/Skills/Buffs/BP_Staggered.BP_Staggered_C"))
                { bStagger = true; break; }
            if (bStagger && (ReadBool(Buff, TEXT("Permenant State")) || ReadInt(Buff, TEXT("turnLeft")) > 0)) return true;
        }
        return false;
    }

    bool MatchesNumber(double Observed, EComparison Comparison, double Threshold)
    {
        if (!FMath::IsFinite(Observed) || !FMath::IsFinite(Threshold)) return false;
        switch (Comparison)
        {
        case EComparison::AtLeast: return Observed >= Threshold;
        case EComparison::AtMost: return Observed <= Threshold;
        case EComparison::Exactly: return Observed == Threshold;
        }
        return false;
    }

    bool TryReadNumber(const UStruct* Type, const void* Object, FName Name, double& OutValue)
    {
        if (Name.IsNone()) return false;
        const auto* Property = FindFProperty<FNumericProperty>(Type, Name);
        if (!Property) return false;
        const void* Value = Property->ContainerPtrToValuePtr<void>(Object);
        if (Property->IsInteger()) OutValue = static_cast<double>(Property->GetSignedIntPropertyValue(Value));
        else if (Property->IsFloatingPoint()) OutValue = Property->GetFloatingPointPropertyValue(Value);
        else return false;
        return FMath::IsFinite(OutValue);
    }

    bool TryReadPercent(const AActor* Unit, FName CurrentName, FName MaxName, double& OutPercent)
    {
        double Current = 0.0;
        double Maximum = 0.0;
        if (!TryReadNumber(Unit->GetClass(), Unit, CurrentName, Current) ||
            !TryReadNumber(Unit->GetClass(), Unit, MaxName, Maximum) || Maximum <= 0.0) return false;
        OutPercent = FMath::Clamp(100.0 * Current / Maximum, 0.0, 100.0);
        return true;
    }

    bool TryReadStatus(AActor* Unit, FName StatusFlag, FName StatusBuffID, bool& bActive)
    {
        const auto* Flag = StatusFlag.IsNone() ? nullptr : FindFProperty<FBoolProperty>(Unit->GetClass(), StatusFlag);
        bActive = Flag && Flag->GetPropertyValue_InContainer(Unit);
        if (!bActive && !StatusBuffID.IsNone())
        {
            TInlineComponentArray<UReditusTableBuffState*> Buffs;
            Unit->GetComponents(Buffs);
            for (const auto* Buff : Buffs)
            {
                if (IsValid(Buff) && Buff->HasActiveBuff(StatusBuffID))
                {
                    bActive = true;
                    break;
                }
            }
        }
        return Flag || !StatusBuffID.IsNone();
    }

    template <class ReadState>
    bool WithActiveATBState(AActor* Unit, AActor* Controller, ReadState Read)
    {
        UActorComponent* Clock = FindClock(Controller);
        UFunction* Function = IsValid(Clock) ? Clock->FindFunction(TEXT("GetUnitState")) : nullptr;
        if (!Function || Function->NumParms != 2) return false;
        const auto* UnitParam = InputProperty<FObjectPropertyBase>(Function, TEXT("Unit"));
        const auto* Result = CastField<FStructProperty>(Function->GetReturnProperty());
        if (!UnitParameter(UnitParam, Unit) || UnitParam->HasAnyPropertyFlags(CPF_OutParm) ||
            !Result || !Result->Struct || Result->Struct->GetPathName() != TEXT("/Script/Chapter1Reditus.ReditusATBUnitView")) return false;
        const auto* Phase = FindFProperty<FEnumProperty>(Result->Struct, TEXT("Phase"));
        if (!Phase || !Phase->GetEnum()) return false;

        FStructOnScope Params(Function);
        void* Memory = Params.GetStructMemory();
        UnitParam->SetObjectPropertyValue_InContainer(Memory, Unit);
        Clock->ProcessEvent(Function, Memory);
        const void* State = Result->ContainerPtrToValuePtr<void>(Memory);
        const auto PhaseValue = Phase->GetUnderlyingProperty()->GetSignedIntPropertyValue(Phase->ContainerPtrToValuePtr<void>(State));
        const FString PhaseName = Phase->GetEnum()->GetNameStringByValue(PhaseValue);
        return !PhaseName.IsEmpty() && PhaseName != TEXT("Inactive") && Read(Result->Struct, State, PhaseName);
    }

    bool TryReadATBMetric(AActor* Unit, AActor* Controller, FName Name, double& OutValue)
    {
        return WithActiveATBState(Unit, Controller, [&](const UStruct* Type, const void* State, const FString&)
        {
            return TryReadNumber(Type, State, Name, OutValue);
        });
    }

    bool TryReadMetric(AActor* Unit, AActor* Controller, const FReditusEnemyAIUnitTest& Test, double& OutValue)
    {
        FName PropertyName = NAME_None;
        switch (Test.Type)
        {
        case ETest::HPPercent: return TryReadPercent(Unit, TEXT("currentHP"), TEXT("maxHP"), OutValue);
        case ETest::MPPercent: return TryReadPercent(Unit, TEXT("currentMP"), TEXT("maxMP"), OutValue);
        case ETest::StaminaPercent: return TryReadPercent(Unit, TEXT("CurrentStamina"), TEXT("MaxStamina"), OutValue);
        case ETest::OverdrivePercent: return TryReadPercent(Unit, TEXT("CurrentOverDrivePoints"), TEXT("MaxOverdrivePoints"), OutValue);
        case ETest::ATBGaugePercent:
            if (!TryReadATBMetric(Unit, Controller, TEXT("Gauge"), OutValue)) return false;
            OutValue = FMath::Clamp(OutValue, 0.0, 1.0) * 100.0;
            return true;
        case ETest::CompletedATBCycles: return TryReadATBMetric(Unit, Controller, TEXT("CompletedCycles"), OutValue);
        case ETest::MPPoints: PropertyName = TEXT("currentMP"); break;
        case ETest::StaminaPoints: PropertyName = TEXT("CurrentStamina"); break;
        case ETest::AmpPoints: PropertyName = TEXT("TotalAmpPoints"); break;
        case ETest::NumericProperty: PropertyName = Test.PropertyName; break;
        default: return false;
        }
        return TryReadNumber(Unit->GetClass(), Unit, PropertyName, OutValue);
    }

    bool PassesUnitTests(AActor* Unit, AActor* Controller, const TArray<FReditusEnemyAIUnitTest>& Tests)
    {
        for (const auto& Test : Tests)
        {
            if (Test.Type == ETest::StatusPresent || Test.Type == ETest::StatusMissing)
            {
                bool bActive = false;
                if (!TryReadStatus(Unit, Test.StatusFlag, Test.StatusBuffID, bActive) ||
                    bActive != (Test.Type == ETest::StatusPresent)) return false;
            }
            else if (Test.Type == ETest::Staggered || Test.Type == ETest::NotStaggered)
            {
                if (IsStaggeredForEnemyTargeting(FindClock(Controller), Unit) != (Test.Type == ETest::Staggered)) return false;
            }
            else if (Test.Type == ETest::ATBReady || Test.Type == ETest::ATBActionPending)
            {
                const bool bMatches = WithActiveATBState(Unit, Controller, [&](const UStruct*, const void*, const FString& Phase)
                {
                    return Test.Type == ETest::ATBReady ? Phase == TEXT("Ready")
                        : (Phase == TEXT("Preparing") || Phase == TEXT("Queued"));
                });
                if (!bMatches) return false;
            }
            else
            {
                double Observed = 0.0;
                if (!TryReadMetric(Unit, Controller, Test, Observed) || !MatchesNumber(Observed, Test.Comparison, Test.Value)) return false;
            }
        }
        return true;
    }

    bool PassesUnitCheck(AActor* Caster, AActor* Controller, const FReditusEnemyAIUnitCheck& Check)
    {
        if (Check.Count < 0) return false;
        if (Check.Subject == ESubject::Self)
        {
            const int32 Count = PassesUnitTests(Caster, Controller, Check.AllTests) ? 1 : 0;
            return MatchesNumber(Count, Check.CountComparison, Check.Count);
        }

        const bool bKO = Check.Subject == ESubject::KnockedOutAllies || Check.Subject == ESubject::KnockedOutOpponents;
        const bool bSwallowed = Check.Subject == ESubject::SwallowedAllies || Check.Subject == ESubject::SwallowedOpponents;
        const bool bOpposing = Check.Subject == ESubject::LivingOpponents || Check.Subject == ESubject::KnockedOutOpponents ||
            Check.Subject == ESubject::SwallowedOpponents;
        if (!IsValid(Controller) || ReadBool(Controller, TEXT("isBattleOver"))) return false;
        UObject* Battle = ReadObject(Controller, TEXT("currentBattle"));
        const auto* Property = IsValid(Battle) ? FindFProperty<FArrayProperty>(Battle->GetClass(),
            bOpposing ? TEXT("playerUnits") : TEXT("enemyUnits")) : nullptr;
        const auto* Item = Property ? CastField<FObjectPropertyBase>(Property->Inner) : nullptr;
        if (!Item) return false;

        FScriptArrayHelper Roster(Property, Property->ContainerPtrToValuePtr<void>(Battle));
        TSet<AActor*> Seen;
        int32 Count = 0;
        for (int32 Index = 0; Index < Roster.Num(); ++Index)
        {
            auto* Unit = Cast<AActor>(Item->GetObjectPropertyValue(Roster.GetRawPtr(Index)));
            if (!IsValid(Unit) || Seen.Contains(Unit) || ReadObject(Unit, TEXT("battleController")) != Controller ||
                ReadBool(Unit, TEXT("Swallowed")) != bSwallowed || (Unit == Caster && Check.Subject == ESubject::OtherLivingAllies)) continue;
            Seen.Add(Unit);

            double HP = 0.0;
            if (!TryReadNumber(Unit->GetClass(), Unit, TEXT("currentHP"), HP)) continue;
            const bool bKnockedOut = HP <= 0.0 || ReadBool(Unit, TEXT("IsDead?"));
            if (bKnockedOut == bKO && PassesUnitTests(Unit, Controller, Check.AllTests)) ++Count;
        }
        return MatchesNumber(Count, Check.CountComparison, Check.Count);
    }

    bool MatchesAnyConditionGroup(AActor* Caster, const TArray<FReditusEnemyAIConditionGroup>& Groups)
    {
        if (!IsValid(Caster)) return false;
        AActor* Controller = Cast<AActor>(ReadObject(Caster, TEXT("battleController")));
        for (const auto& Group : Groups)
        {
            if (Group.AllChecks.Num() == 0) continue;
            bool bMatches = true;
            for (const auto& Check : Group.AllChecks)
            {
                if (PassesUnitCheck(Caster, Controller, Check)) continue;
                bMatches = false;
                break;
            }
            if (bMatches) return true;
        }
        return false;
    }

    bool MatchesSkill(FName ActionID, const UClass* ActionClass, FName RuleID, const UClass* RuleClass)
    {
        return (!ActionID.IsNone() && ActionID == RuleID) || (ActionClass && RuleClass && ActionClass->IsChildOf(RuleClass));
    }
}

UReditusEnemyCombatAIComponent::UReditusEnemyCombatAIComponent()
{
    PrimaryComponentTick.bCanEverTick = false;
}

const uint8* UReditusEnemyCombatAIComponent::FindEnemyDefinition() const
{
    const AActor* Enemy = GetOwner();
    if (!IsValid(Enemy) || !IsValid(EnemyDefinitions) || !EnemyDefinitions->GetRowStruct()) return nullptr;
    const auto* IDProperty = FindFProperty<FNameProperty>(Enemy->GetClass(), TEXT("EnemyID"));
    const FName ID = IDProperty ? IDProperty->GetPropertyValue_InContainer(Enemy) : NAME_None;
    return ID.IsNone() ? nullptr : EnemyDefinitions->GetRowMap().FindRef(ID);
}

const FReditusEnemyAIProfile* UReditusEnemyCombatAIComponent::FindProfile() const
{
    const AActor* Enemy = GetOwner();
    if (!IsValid(Enemy)) return nullptr;
    const uint8* RowData = FindEnemyDefinition();
    const UScriptStruct* RowType = RowData ? EnemyDefinitions->GetRowStruct() : nullptr;
    const auto* ProfileProperty = RowData ? FindFProperty<FStructProperty>(RowType, TEXT("AIProfile")) : nullptr;
    if (ProfileProperty && ProfileProperty->Struct == FDataTableRowHandle::StaticStruct())
    {
        const auto& Handle = *ProfileProperty->ContainerPtrToValuePtr<FDataTableRowHandle>(RowData);
        if (IsValid(Handle.DataTable) && !Handle.RowName.IsNone() &&
            Handle.DataTable->GetRowStruct() == FReditusEnemyAIProfile::StaticStruct())
        {
            if (const auto* Profile = Handle.DataTable->FindRow<FReditusEnemyAIProfile>(
                Handle.RowName, TEXT("Enemy definition AI profile"), false))
            {
                return Profile;
            }
        }
    }

    if (IsValid(AIProfile.DataTable) && !AIProfile.RowName.IsNone() &&
        AIProfile.DataTable->GetRowStruct() == FReditusEnemyAIProfile::StaticStruct())
    {
        if (const auto* Profile = AIProfile.DataTable->FindRow<FReditusEnemyAIProfile>(
            AIProfile.RowName, TEXT("Enemy class AI profile"), false))
        {
            return Profile;
        }
    }

    const auto* ProfileIDProperty = RowData ? FindFProperty<FNameProperty>(RowType, TEXT("AIProfileID")) : nullptr;
    if (!ProfileIDProperty || !IsValid(ProfileTable) ||
        ProfileTable->GetRowStruct() != FReditusEnemyAIProfile::StaticStruct()) return nullptr;
    const FName ProfileID = ProfileIDProperty->GetPropertyValue_InContainer(RowData);
    return ProfileID.IsNone() ? nullptr : ProfileTable->FindRow<FReditusEnemyAIProfile>(
        ProfileID, TEXT("Enemy definition AI profile"), false);
}

int32 UReditusEnemyCombatAIComponent::ResolveEnemyAmpForAction(bool bRegularAttack, FName SkillID, TSubclassOf<AActor> SkillClass) const
{
    AActor* Enemy = GetOwner();
    if (!IsValid(Enemy) || AmpPolicy == EReditusEnemyAIAmpPolicy::Disabled || ReadBool(Enemy, TEXT("Easy Difficulty "))) return 0;

    const FReditusEnemyAIProfile* Profile = FindProfile();
    if (AmpPolicy == EReditusEnemyAIAmpPolicy::UseProfile && (!Profile || !Profile->bEnableAMP)) return 0;
    if (const uint8* Definition = FindEnemyDefinition())
    {
        const auto* CanUseAMP = FindFProperty<FBoolProperty>(EnemyDefinitions->GetRowStruct(), TEXT("bCanUseAMP"));
        if (CanUseAMP && !CanUseAMP->GetPropertyValue_InContainer(Definition)) return 0;
    }

    const int32 Available = ReadInt(Enemy, TEXT("TotalAmpPoints"));
    if (Available <= 0) return 0;
    const UClass* ChosenClass = SkillClass.Get();
    const auto SelectAmount = [&](const TArray<FReditusEnemyAIAmpRule>& Rules, bool& bMatchesMove)
    {
        for (const auto& Rule : Rules)
        {
            if ((Rule.Action == EReditusEnemyAIAmpAction::RegularAttack) != bRegularAttack || Rule.Points <= 0) continue;
            if (!bRegularAttack)
            {
                const UClass* RuleClass = Rule.SkillClass.Get();
                const bool bAnySkill = Rule.SkillID.IsNone() && !RuleClass;
                if (!bAnySkill && !MatchesSkill(SkillID, ChosenClass, Rule.SkillID, RuleClass)) continue;
            }
            bMatchesMove = true;
            if (!Rule.bSpendUpToAvailable && Rule.Points > Available) continue;
            if (Rule.AnyOf.Num() > 0 && !MatchesAnyConditionGroup(Enemy, Rule.AnyOf)) continue;
            return FMath::Min(Available, Rule.Points);
        }
        return 0;
    };

    bool bMatchesMove = false;
    const int32 LocalAmount = SelectAmount(LocalAmpRules, bMatchesMove);
    return bMatchesMove || !Profile ? LocalAmount : SelectAmount(Profile->AmpRules, bMatchesMove);
}

bool UReditusEnemyCombatAIComponent::MeetsSkillCondition(const FReditusEnemyAISkillRule& Rule) const
{
    AActor* Enemy = GetOwner();
    if (!IsValid(Enemy)) return false;

    if (Rule.Condition == EReditusEnemyAISkillCondition::RequireLowHP)
    {
        const int32 MaxHP = ReadInt(Enemy, TEXT("maxHP"));
        const int32 CurrentHP = ReadInt(Enemy, TEXT("currentHP"));
        return MaxHP > 0 && CurrentHP > 0 && FMath::IsFinite(Rule.HPPercent) &&
            static_cast<double>(CurrentHP) * 100.0 <= static_cast<double>(MaxHP) * FMath::Clamp(Rule.HPPercent, 0.f, 100.f);
    }

    bool bActive = false;
    TryReadStatus(Enemy, Rule.StatusFlag, Rule.StatusBuffID, bActive);
    return Rule.Condition == EReditusEnemyAISkillCondition::SkipWhileStatusActive ? !bActive : bActive;
}

float UReditusEnemyCombatAIComponent::GetRuleAdjustedWeight(const FReditusEnemyAIProfile* Profile, FName SkillID,
    const UClass* SkillClass, float OriginalWeight, bool& bMatchedRule) const
{
    bMatchedRule = false;
    double Weight = OriginalWeight;
    // The ID selector sums floats. Bound any boosted term for even the largest possible map.
    constexpr double MaximumScaledWeight = static_cast<double>((std::numeric_limits<float>::max)()) /
        (std::numeric_limits<int32>::max)();
    const double WeightLimit = FMath::Max(MaximumScaledWeight, Weight);
    const auto ApplyMatchingRules = [&](const TArray<FReditusEnemyAISkillRule>& Rules)
    {
        for (const auto& Rule : Rules)
        {
            if (!MatchesSkill(SkillID, SkillClass, Rule.SkillID, Rule.SkillClass.Get())) continue;
            bMatchedRule = true;
            if (Weight <= 0.0 || !FMath::IsFinite(Weight)) return false;

            if (Rule.AnyOf.Num() == 0)
            {
                if (!MeetsSkillCondition(Rule)) return false;
                continue;
            }
            const bool bMatches = MatchesAnyConditionGroup(GetOwner(), Rule.AnyOf);
            switch (Rule.ConditionEffect)
            {
            case EReditusEnemyAIRuleEffect::RequireMatch:
                if (!bMatches) return false;
                break;
            case EReditusEnemyAIRuleEffect::SkipOnMatch:
                if (bMatches) return false;
                break;
            case EReditusEnemyAIRuleEffect::AdjustWeight:
                if (bMatches && FMath::IsFinite(Rule.WeightMultiplier))
                {
                    const float Multiplier = FMath::Clamp(Rule.WeightMultiplier, 0.f, 100.f);
                    Weight = FMath::Min(Weight * Multiplier, WeightLimit);
                }
                break;
            }
        }
        return true;
    };

    if (!ApplyMatchingRules(LocalSkillRules) || (Profile && !ApplyMatchingRules(Profile->SkillRules))) return 0.f;
    return static_cast<float>(Weight);
}

float UReditusEnemyCombatAIComponent::FilterEnemySkillIDWeight(AActor* Enemy, FName SkillID, float OriginalWeight)
{
    if (!IsValid(Enemy) || SkillID.IsNone() || OriginalWeight <= 0.f) return OriginalWeight;
    const auto* Component = Enemy->FindComponentByClass<UReditusEnemyCombatAIComponent>();
    if (!Component) return OriginalWeight;

    bool bMatchedRule = false;
    return Component->GetRuleAdjustedWeight(Component->FindProfile(), SkillID, nullptr, OriginalWeight, bMatchedRule);
}

bool UReditusEnemyCombatAIComponent::TryChooseProfiledClassSkill(AActor* Enemy)
{
    if (!IsValid(Enemy)) return false;
    const auto* CurrentSkill = FindFProperty<FClassProperty>(Enemy->GetClass(), TEXT("currentSkill"));
    if (!CurrentSkill || !CurrentSkill->MetaClass) return false;

    TSubclassOf<AActor> SelectedSkill;
    if (!SelectClassSkill(Enemy, CurrentSkill->MetaClass, SelectedSkill)) return false;
    CurrentSkill->SetObjectPropertyValue_InContainer(Enemy, SelectedSkill.Get());
    return true;
}

bool UReditusEnemyCombatAIComponent::TrySelectProfiledClassSkill(AActor* Enemy, TSubclassOf<AActor>& SelectedSkill)
{
    SelectedSkill = nullptr;
    return IsValid(Enemy) && SelectClassSkill(Enemy, AActor::StaticClass(), SelectedSkill);
}

bool UReditusEnemyCombatAIComponent::SelectClassSkill(AActor* Enemy, const UClass* SkillType, TSubclassOf<AActor>& SelectedSkill)
{
    const auto* Component = Enemy->FindComponentByClass<UReditusEnemyCombatAIComponent>();
    if (!Component) return false;
    const FReditusEnemyAIProfile* Profile = Component->FindProfile();
    if (Component->LocalSkillRules.Num() == 0 && (!Profile || Profile->SkillRules.Num() == 0)) return false;

    const auto* SkillMap = FindFProperty<FMapProperty>(Enemy->GetClass(), TEXT("skills"));
    const auto* Key = SkillMap ? CastField<FClassProperty>(SkillMap->KeyProp) : nullptr;
    const auto* Weight = SkillMap ? CastField<FFloatProperty>(SkillMap->ValueProp) : nullptr;
    const auto* CurrentMP = FindFProperty<FIntProperty>(Enemy->GetClass(), TEXT("currentMP"));
    if (!Key || !Key->MetaClass || !Weight || !CurrentMP || !Key->MetaClass->IsChildOf(SkillType)) return false;

    struct FWeightedSkill
    {
        UClass* Class;
        float Weight;
    };

    const int32 MP = CurrentMP->GetPropertyValue_InContainer(Enemy);
    FScriptMapHelper Entries(SkillMap, SkillMap->ContainerPtrToValuePtr<void>(Enemy));
    TArray<FWeightedSkill> Candidates;
    bool bAppliedRule = false;
    double TotalWeight = 0.0;
    for (int32 Index = 0; Index < Entries.GetMaxIndex(); ++Index)
    {
        if (!Entries.IsValidIndex(Index)) continue;
        UClass* Skill = Cast<UClass>(Key->GetObjectPropertyValue(Entries.GetKeyPtr(Index)));
        if (Skill && !Skill->IsChildOf(SkillType)) return false;
        bool bMatchedRule = false;
        const float CandidateWeight = Component->GetRuleAdjustedWeight(Profile, NAME_None, Skill,
            Weight->GetPropertyValue(Entries.GetValuePtr(Index)), bMatchedRule);
        bAppliedRule |= bMatchedRule;

        if (!FMath::IsFinite(CandidateWeight) || CandidateWeight <= 0.f) continue;
        if (Skill && MP < FMath::Max(0, ReadInt(Skill->GetDefaultObject(), TEXT("manaRequired")))) continue;

        Candidates.Add({Skill, CandidateWeight});
        TotalWeight += CandidateWeight;
    }
    if (!bAppliedRule) return false;

    if (Candidates.Num() > 0)
    {
        SelectedSkill = Candidates.Last().Class;
        double Roll = FMath::FRand() * TotalWeight;
        for (const FWeightedSkill& Candidate : Candidates)
        {
            Roll -= Candidate.Weight;
            if (Roll < 0.0)
            {
                SelectedSkill = Candidate.Class;
                break;
            }
        }
    }
    return true;
}

namespace
{
    int32 FindStaggeredPlayerIndex(AActor* BattleController, int32 LegacyIndex)
    {
        // Preserve playerUnits indices: the existing Blueprint GET consumes this.
        UObject* Battle = ReadObject(BattleController, TEXT("currentBattle"));
        const auto* Property = IsValid(Battle) ? FindFProperty<FArrayProperty>(Battle->GetClass(), TEXT("playerUnits")) : nullptr;
        const auto* Item = Property ? CastField<FObjectPropertyBase>(Property->Inner) : nullptr;
        if (!Item) return LegacyIndex;
        FScriptArrayHelper Players(Property, Property->ContainerPtrToValuePtr<void>(Battle));
        UActorComponent* Clock = FindClock(BattleController);
        TArray<int32> Candidates;
        for (int32 Index = 0; Index < Players.Num(); ++Index)
        {
            auto* Unit = Cast<AActor>(Item->GetObjectPropertyValue(Players.GetRawPtr(Index)));
            if (AliveInBattle(Unit, BattleController) && !ReadBool(Unit, TEXT("Swallowed")) &&
                IsStaggeredForEnemyTargeting(Clock, Unit)) Candidates.Add(Index);
        }
        // Aggro has no influence within the staggered pool. Do not write any
        // target, threat or action state; an existing attack keeps its snapshot.
        return Candidates.Num() == 0 ? LegacyIndex
            : Candidates[Candidates.Num() == 1 ? 0 : FMath::RandRange(0, Candidates.Num() - 1)];
    }
}

int32 UReditusEnemyCombatAIComponent::ResolveTargetIndex(AActor* BattleController, AActor* Selector, int32 LegacyIndex)
{
    if (!IsValid(BattleController) || !IsValid(Selector)) return LegacyIndex;
    const FName Enemy(TEXT("Enemy"));
    const bool bEnemyUnitSelector = MatchesCatalogueSide(Selector, Enemy);
    // The skill selector also runs for some multi-target skills. Keep those unchanged.
    if (!bEnemyUnitSelector && !ReadBool(Selector, TEXT("OneUnitAttack?"))) return LegacyIndex;
    const AActor* Attacker = bEnemyUnitSelector ? Selector
        : Cast<AActor>(ReadObject(BattleController, TEXT("currentAttackingUnit")));
    if (!AliveInBattle(Attacker, BattleController) ||
        !MatchesCatalogueSide(Attacker, Enemy)) return LegacyIndex;
    // Those statuses have their own either-side selection. Do not steer them
    // back toward the party or override a deliberate friendly-fire choice.
    if (ReadBool(Attacker, TEXT("Is Confused")) || ReadBool(Attacker, TEXT("NewConfused")) ||
        ReadBool(Attacker, TEXT("IsCharmed??"))) return LegacyIndex;

    AActor* Target = nullptr;
    const int32 FocusIndex = FindAggroFocus(BattleController, Target);
    if (FocusIndex != INDEX_NONE) return FocusIndex; // Rage Bait protects a staggered ally.
    return FindStaggeredPlayerIndex(BattleController, LegacyIndex);
}
