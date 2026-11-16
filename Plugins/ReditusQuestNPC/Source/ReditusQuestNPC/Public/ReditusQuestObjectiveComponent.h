#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "ReditusQuestNPCTypes.h"
#include "TimerManager.h"
#include "ReditusQuestObjectiveComponent.generated.h"

class UMapIconComponent;
class UTexture2D;

// Put this beside a MapIcon on a destination actor. Reporting is explicit; the
// marker never completes an objective merely because it becomes visible.
UCLASS(BlueprintType, ClassGroup=(Reditus), meta=(BlueprintSpawnableComponent, DisplayName="Reditus Quest Objective"))
class REDITUSQUESTNPC_API UReditusQuestObjectiveComponent : public UActorComponent
{
    GENERATED_BODY()

public:
    UReditusQuestObjectiveComponent();

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Quest Objective", meta=(RowType="ReditusQuestObjectiveDefinition"))
    FDataTableRowHandle DefinitionRow;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Quest Objective")
    FReditusQuestObjectiveDefinition InlineDefinition;

    UPROPERTY(BlueprintReadOnly, Category="Retired Marker Settings", meta=(DeprecatedProperty, DeprecationMessage="Ignored: use DT_Quests and MinimapPlugin."))
    UTexture2D* MainQuestIcon = nullptr;

    UPROPERTY(BlueprintReadOnly, Category="Retired Marker Settings", meta=(DeprecatedProperty, DeprecationMessage="Ignored: use DT_Quests and MinimapPlugin."))
    UTexture2D* SideQuestIcon = nullptr;

    UFUNCTION(BlueprintCallable, Category="Reditus|Quest")
    void RefreshObjective();

    UFUNCTION(BlueprintCallable, Category="Reditus|New Quests")
    bool ReportObjectiveReached(FName UniqueEventID);

protected:
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;

private:
    UFUNCTION() void HandleQuestChanged(FDataTableRowHandle Quest);
    UFUNCTION() void HandleStateRestored();
    void FindMapIcon();
    const FReditusQuestObjectiveDefinition* ResolveDefinition() const;
    TWeakObjectPtr<UMapIconComponent> MapIcon;
    FTimerHandle RefreshTimer;
    bool bEndingPlay = false;
};
