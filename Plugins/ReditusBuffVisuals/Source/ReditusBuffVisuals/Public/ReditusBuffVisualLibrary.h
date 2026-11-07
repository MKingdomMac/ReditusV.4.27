#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "ReditusBuffVisualLibrary.generated.h"

class AActor;
class UNiagaraComponent;
class UNiagaraSystem;
class USceneComponent;

USTRUCT(BlueprintType)
struct REDITUSBUFFVISUALS_API FReditusBuffNiagaraEffect
{
    GENERATED_BODY()

    /** None means this phase has no visual. Use finite systems for one-shots. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Effect")
    UNiagaraSystem* Niagara = nullptr;

    /** Use a socket on the supplied component, or None for the component origin. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Effect")
    FName Socket = FName(TEXT("StatusSocket"));

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Effect")
    FVector Offset = FVector::ZeroVector;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Effect")
    FRotator Rotation = FRotator::ZeroRotator;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Effect")
    FVector Scale = FVector(1.0f);
};

USTRUCT(BlueprintType)
struct REDITUSBUFFVISUALS_API FReditusBuffVisualRow : public FTableRowBase
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Buff Visuals")
    FReditusBuffNiagaraEffect OnApply;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Buff Visuals")
    FReditusBuffNiagaraEffect WhileActive;

    /** Optional; call Play Buff Tick Visual after an actual gameplay tick. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Buff Visuals")
    FReditusBuffNiagaraEffect OnTick;

    /** Optional; played on normal Stop, never on forced actor teardown. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Buff Visuals")
    FReditusBuffNiagaraEffect OnEnd;
};

UCLASS()
class REDITUSBUFFVISUALS_API UReditusBuffVisualLibrary : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()

public:
    /** Call on successful application. Use the buff actor as the owner, not the caster or unit. */
    UFUNCTION(BlueprintCallable, Category="Reditus|Buff Visuals")
    static UNiagaraComponent* StartBuffVisuals(AActor* BuffOwner, USceneComponent* AttachToComponent,
        UDataTable* VisualTable, FName BuffID);

    /** Plays the configured one-shot for one accepted status tick. Does not tick gameplay. */
    UFUNCTION(BlueprintCallable, Category="Reditus|Buff Visuals")
    static void PlayBuffTickVisual(AActor* BuffOwner);

    /** Removes only this buff actor's loop. Safe for missing or already stopped visuals. */
    UFUNCTION(BlueprintCallable, Category="Reditus|Buff Visuals")
    static void StopBuffVisuals(AActor* BuffOwner);
};
