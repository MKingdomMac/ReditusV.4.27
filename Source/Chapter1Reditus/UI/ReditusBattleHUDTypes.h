#pragma once

#include "CoreMinimal.h"
#include "InputCoreTypes.h"
#include "Styling/SlateBrush.h"
#include "ReditusBattleHUDTypes.generated.h"

class AActor;

UENUM(BlueprintType)
enum class EReditusHUDCommand : uint8
{
    Attack, Tech, Items, Defend, Overdrive, Hack, ChangeParty, Run,
    IncreaseAmp, DecreaseAmp, Block, Cancel, CycleBattleSpeed,
    SelectPartyCard UMETA(Hidden), SelectMenuEntry UMETA(Hidden),
    ReadyAlly, ToggleAlternate UMETA(Hidden)
};

UENUM(BlueprintType)
enum class EReditusHUDInputDevice : uint8
{
    KeyboardMouse, Xbox
};

/** Combat-only controller profile. Existing keyboard action mappings remain. */
USTRUCT(BlueprintType)
struct CHAPTER1REDITUS_API FReditusBattleHUDGamepadKeys
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="HUD") FKey Bottom = EKeys::Gamepad_FaceButton_Bottom;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="HUD") FKey Left = EKeys::Gamepad_FaceButton_Left;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="HUD") FKey Right = EKeys::Gamepad_FaceButton_Right;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="HUD") FKey Top = EKeys::Gamepad_FaceButton_Top;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="HUD") FKey More = EKeys::Gamepad_RightShoulder;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="HUD") FKey Block = EKeys::Gamepad_LeftShoulder;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="HUD") FKey DecreaseAmp = EKeys::Gamepad_LeftTrigger;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="HUD") FKey IncreaseAmp = EKeys::Gamepad_RightTrigger;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="HUD") FKey Overdrive = EKeys::Gamepad_RightThumbstick;
};

USTRUCT(BlueprintType)
struct CHAPTER1REDITUS_API FReditusBattleHUDUnit
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="HUD") AActor* Unit = nullptr;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="HUD") FName CharacterID;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="HUD") FText DisplayName;
    /** Existing UTexture2D or UPaperSprite from battleImage/S_Thumbnail. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="HUD") UObject* Portrait = nullptr;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="HUD") float HP = 0.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="HUD") float MaxHP = 0.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="HUD") float MP = 0.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="HUD") float MaxMP = 0.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="HUD") float ATBPercent = 0.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="HUD") bool bReadyForCommands = false;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="HUD") bool bCanAct = true;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="HUD") bool bQueued = false;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="HUD") float Overdrive = 0.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="HUD") float MaxOverdrive = 0.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="HUD") bool bCanOverdrive = true;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="HUD") bool bOverdriveEntered = false;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="HUD") bool bCanHack = false;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="HUD") int32 TotalAmp = 0;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="HUD") int32 CommittedAmp = 0;
    /** Live BP_UnitBase.CurrentStamina; the HUD does not spend or restore it. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="HUD") int32 CurrentStamina = 0;

    /** Cached KO cards may outlive their actor. A default snapshot is empty. */
    bool HasCharacter() const
    {
        return Unit!=nullptr || !CharacterID.IsNone() || !DisplayName.IsEmpty() || Portrait!=nullptr;
    }
};

USTRUCT(BlueprintType)
struct CHAPTER1REDITUS_API FReditusBattleHUDMenuEntry
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="HUD") FName ID;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="HUD") FText Label;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="HUD") FText Detail;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="HUD") FText Description;
    /** Copies the existing row's texture, sprite or material brush. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="HUD") FSlateBrush Icon;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="HUD") bool bEnabled = true;
};

/** Names are configurable; defaults match the uploaded DefaultInput.ini. */
USTRUCT(BlueprintType)
struct CHAPTER1REDITUS_API FReditusBattleHUDInputActions
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="HUD") FName Block = TEXT("Block");
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="HUD") FName IncreaseAmp = TEXT("IncreaseAmp");
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="HUD") FName DecreaseAmp = TEXT("DecreaseAmp");
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="HUD") FName ChangeParty = TEXT("SwapPartyMember");
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="HUD") FName Speed = TEXT("TimeSpeedUp");
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FReditusHUDCommandEvent, AActor*, Unit, EReditusHUDCommand, Command);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FReditusHUDSelectionEvent, AActor*, Unit);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FReditusHUDMenuEvent, AActor*, Unit, FName, EntryID);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FReditusHUDGameplayInputEvent, FName, ActionName, FKey, Key);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FReditusHUDDeviceEvent, EReditusHUDInputDevice, Device);

DECLARE_DELEGATE_RetVal_TwoParams(bool, FReditusHUDCommandRouter, AActor*, EReditusHUDCommand);
DECLARE_DELEGATE_RetVal_TwoParams(bool, FReditusHUDGameplayInputRouter, FName, FKey);
