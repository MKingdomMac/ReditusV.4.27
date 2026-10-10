#include "ReditusEnemyPhaseComponent.h"

UReditusEnemyPhaseComponent::UReditusEnemyPhaseComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UReditusEnemyPhaseComponent::InitializeFromEnemyDefinition(
	const FReditusEnemyDefinition& EnemyDefinition)
{
	CurrentPhaseIndex = -1;
	TotalStaggerCount = 0;
	CurrentTurnNumber = 0;

	PhaseDefinitions = EnemyDefinition.Phases;
	CurrentElementAffinity = EnemyDefinition.ElementAffinity;
	CurrentSkills = EnemyDefinition.Skills;
	CurrentAIProfileID = EnemyDefinition.AIProfileID;
	CurrentSpecialBehaviorClass = EnemyDefinition.SpecialBehaviorClass;
}

bool UReditusEnemyPhaseComponent::HasNextPhase() const
{
	return PhaseDefinitions.IsValidIndex(CurrentPhaseIndex + 1);
}

bool UReditusEnemyPhaseComponent::GetCurrentPhase(
	FReditusEnemyPhase& OutPhase) const
{
	if (!PhaseDefinitions.IsValidIndex(CurrentPhaseIndex))
	{
		OutPhase = FReditusEnemyPhase();
		return false;
	}

	OutPhase = PhaseDefinitions[CurrentPhaseIndex];
	return true;
}

bool UReditusEnemyPhaseComponent::DoesNextPhaseMatchStagger() const
{
	if (!HasNextPhase())
	{
		return false;
	}

	const FReditusEnemyPhase& NextPhase =
		PhaseDefinitions[CurrentPhaseIndex + 1];

	switch (NextPhase.Trigger)
	{
	case EReditusEnemyPhaseTrigger::AfterStagger:
		return true;

	case EReditusEnemyPhaseTrigger::StaggerCountAtLeast:
		return static_cast<float>(TotalStaggerCount) >= NextPhase.TriggerValue;

	default:
		return false;
	}
}

bool UReditusEnemyPhaseComponent::DoesNextPhaseMatchHP(
	float CurrentHP,
	float MaxHP) const
{
	if (!HasNextPhase() || MaxHP <= 0.0f)
	{
		return false;
	}

	const FReditusEnemyPhase& NextPhase =
		PhaseDefinitions[CurrentPhaseIndex + 1];

	if (NextPhase.Trigger != EReditusEnemyPhaseTrigger::HPBelowPercent)
	{
		return false;
	}

	const float HPPercent =
		FMath::Clamp((CurrentHP / MaxHP) * 100.0f, 0.0f, 100.0f);

	return HPPercent <= NextPhase.TriggerValue;
}

bool UReditusEnemyPhaseComponent::DoesNextPhaseMatchTurn(
	int32 TurnNumber) const
{
	if (!HasNextPhase())
	{
		return false;
	}

	const FReditusEnemyPhase& NextPhase =
		PhaseDefinitions[CurrentPhaseIndex + 1];

	if (NextPhase.Trigger != EReditusEnemyPhaseTrigger::TurnNumberAtLeast)
	{
		return false;
	}

	return static_cast<float>(TurnNumber) >= NextPhase.TriggerValue;
}

bool UReditusEnemyPhaseComponent::EnterNextPhase()
{
	if (!HasNextPhase())
	{
		return false;
	}

	++CurrentPhaseIndex;

	const FReditusEnemyPhase& NewPhase =
		PhaseDefinitions[CurrentPhaseIndex];

	if (NewPhase.ElementAffinityOverride.Num() > 0)
	{
		CurrentElementAffinity = NewPhase.ElementAffinityOverride;
	}

	switch (NewPhase.SkillMode)
	{
	case EReditusEnemyPhaseSkillMode::Replace:
		CurrentSkills = NewPhase.Skills;
		break;

	case EReditusEnemyPhaseSkillMode::Add:
		CurrentSkills.Append(NewPhase.Skills);
		break;

	case EReditusEnemyPhaseSkillMode::KeepCurrent:
	default:
		break;
	}

	if (!NewPhase.AIProfileID.IsNone())
	{
		CurrentAIProfileID = NewPhase.AIProfileID;
	}

	if (!NewPhase.SpecialBehaviorClass.ToString().IsEmpty())
	{
		CurrentSpecialBehaviorClass = NewPhase.SpecialBehaviorClass;
	}

	OnPhaseChanged.Broadcast(CurrentPhaseIndex, NewPhase);
	return true;
}

bool UReditusEnemyPhaseComponent::NotifyEnemyStaggered()
{
	++TotalStaggerCount;

	// One phase per stagger event.
	if (DoesNextPhaseMatchStagger())
	{
		return EnterNextPhase();
	}

	return false;
}

bool UReditusEnemyPhaseComponent::NotifyHPChanged(
	float CurrentHP,
	float MaxHP)
{
	bool bChanged = false;

	while (DoesNextPhaseMatchHP(CurrentHP, MaxHP))
	{
		if (!EnterNextPhase())
		{
			break;
		}

		bChanged = true;
	}

	return bChanged;
}

bool UReditusEnemyPhaseComponent::NotifyTurnStarted(
	int32 TurnNumber)
{
	CurrentTurnNumber = TurnNumber;

	bool bChanged = false;

	while (DoesNextPhaseMatchTurn(TurnNumber))
	{
		if (!EnterNextPhase())
		{
			break;
		}

		bChanged = true;
	}

	return bChanged;
}

bool UReditusEnemyPhaseComponent::TriggerCustomPhase(
	FName PhaseID)
{
	if (!HasNextPhase())
	{
		return false;
	}

	const FReditusEnemyPhase& NextPhase =
		PhaseDefinitions[CurrentPhaseIndex + 1];

	if (NextPhase.Trigger != EReditusEnemyPhaseTrigger::Custom)
	{
		return false;
	}

	if (!PhaseID.IsNone() && NextPhase.PhaseID != PhaseID)
	{
		return false;
	}

	return EnterNextPhase();
}

bool UReditusEnemyPhaseComponent::ForceNextPhase()
{
	return EnterNextPhase();
}
