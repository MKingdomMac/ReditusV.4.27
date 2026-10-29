#include "ReditusBattleHUDPresenterComponent.h"
#include "ReditusBattleHUDWidget.h"
#include "ReditusBattleHUDPolicy.h"
#include "ReditusBattleHUDInputPolicy.h"
#include "Components/InputComponent.h"
#include "Components/Button.h"
#include "Components/TextBlock.h"
#include "Components/Image.h"
#include "Kismet/GameplayStatics.h"
#include "Blueprint/WidgetTree.h"
#include "Engine/DataTable.h"
#include "Engine/World.h"
#include "Engine/Texture2D.h"
#include "GameFramework/Actor.h"
#include "GameFramework/PlayerController.h"
#include "PaperSprite.h"
#include "UObject/UnrealType.h"
#include "UObject/UObjectGlobals.h"

DEFINE_LOG_CATEGORY_STATIC(LogReditusBattleHUD, Log, All);

namespace
{
    FProperty* Property(UObject* Object, FName Name)
    {
        return IsValid(Object) ? FindFProperty<FProperty>(Object->GetClass(),Name) : nullptr;
    }

    UObject* ObjectValue(UObject* Object, FName Name)
    {
        FObjectPropertyBase* Field=CastField<FObjectPropertyBase>(Property(Object,Name));
        return Field ? Field->GetObjectPropertyValue_InContainer(Object) : nullptr;
    }

    bool SetObjectValue(UObject* Object,FName Name,UObject* Value)
    {
        FObjectPropertyBase* Field=CastField<FObjectPropertyBase>(Property(Object,Name));
        if (!Field || (Value && !Value->IsA(Field->PropertyClass))) return false;
        Field->SetObjectPropertyValue_InContainer(Object,Value);
        return true;
    }

    bool SetSelectionValue(UObject* Object,int32 Value)
    {
        FProperty* Field=Property(Object,TEXT("SelectType"));
        if (FEnumProperty* Enum=CastField<FEnumProperty>(Field))
        {
            if (!Enum->GetEnum() || Enum->GetEnum()->GetFName()!=TEXT("E_SelectType")) return false;
            Enum->GetUnderlyingProperty()->SetIntPropertyValue(Enum->ContainerPtrToValuePtr<void>(Object),static_cast<int64>(Value));
            return true;
        }
        if (FByteProperty* Byte=CastField<FByteProperty>(Field))
        {
            if (!Byte->Enum || Byte->Enum->GetFName()!=TEXT("E_SelectType")) return false;
            Byte->SetPropertyValue_InContainer(Object,static_cast<uint8>(Value));
            return true;
        }
        return false;
    }

    double Number(UObject* Object, FName Name, double Default=0.0)
    {
        FProperty* Field=Property(Object,Name);
        if (!Field) return Default;
        const void* Address=Field->ContainerPtrToValuePtr<void>(Object);
        if (FEnumProperty* Enum=CastField<FEnumProperty>(Field))
            return static_cast<double>(Enum->GetUnderlyingProperty()->GetSignedIntPropertyValue(Address));
        FNumericProperty* Numeric=CastField<FNumericProperty>(Field);
        if (!Numeric) return Default;
        const double Value=Numeric->IsFloatingPoint() ? Numeric->GetFloatingPointPropertyValue(Address) :
            static_cast<double>(Numeric->GetSignedIntPropertyValue(Address));
        return FMath::IsFinite(Value) ? Value : Default;
    }

    bool Boolean(UObject* Object, FName Name, bool Default=false)
    {
        FBoolProperty* Field=CastField<FBoolProperty>(Property(Object,Name));
        return Field ? Field->GetPropertyValue_InContainer(Object) : Default;
    }

    FName NameValue(UObject* Object, FName Name)
    {
        FNameProperty* Field=CastField<FNameProperty>(Property(Object,Name));
        return Field ? Field->GetPropertyValue_InContainer(Object) : NAME_None;
    }

    FText Title(UObject* Object)
    {
        FTextProperty* Field=CastField<FTextProperty>(Property(Object,TEXT("title")));
        return Field ? Field->GetPropertyValue_InContainer(Object) : FText::GetEmpty();
    }

    TArray<AActor*> Actors(UObject* Object,FName Name)
    {
        TArray<AActor*> Result;
        FArrayProperty* Array=CastField<FArrayProperty>(Property(Object,Name));
        FObjectPropertyBase* Inner=Array ? CastField<FObjectPropertyBase>(Array->Inner) : nullptr;
        if (!Inner) return Result;
        FScriptArrayHelper Helper(Array,Array->ContainerPtrToValuePtr<void>(Object));
        for (int32 I=0; I<Helper.Num(); ++I)
            if (AActor* Actor=Cast<AActor>(Inner->GetObjectPropertyValue(Helper.GetRawPtr(I))))
                if (IsValid(Actor)) Result.AddUnique(Actor);
        return Result;
    }

    TSet<FName> DeadCharacterIDs(UObject* Battle)
    {
        TSet<FName> Result;
        FMapProperty* Map=CastField<FMapProperty>(Property(Battle,TEXT("DeadCharactersByID")));
        FNameProperty* Key=Map ? CastField<FNameProperty>(Map->KeyProp) : nullptr;
        if (!Key) return Result;
        // The map stores spawn transforms as values, not actor references.
        FScriptMapHelper Helper(Map,Map->ContainerPtrToValuePtr<void>(Battle));
        for (int32 I=0; I<Helper.GetMaxIndex(); ++I)
            if (Helper.IsValidIndex(I)) Result.Add(Key->GetPropertyValue(Helper.GetKeyPtr(I)));
        return Result;
    }

    TArray<FName> Names(UObject* Object,FName Name)
    {
        TArray<FName> Result;
        FArrayProperty* Array=CastField<FArrayProperty>(Property(Object,Name));
        FNameProperty* Inner=Array ? CastField<FNameProperty>(Array->Inner) : nullptr;
        if (!Inner) return Result;
        FScriptArrayHelper Helper(Array,Array->ContainerPtrToValuePtr<void>(Object));
        for (int32 I=0; I<Helper.Num(); ++I) Result.AddUnique(Inner->GetPropertyValue(Helper.GetRawPtr(I)));
        return Result;
    }

    UObject* Thumbnail(UObject* Unit,FName StructName)
    {
        FStructProperty* Field=CastField<FStructProperty>(Property(Unit,StructName));
        if (!Field || !Field->Struct) return nullptr;
        const void* Data=Field->ContainerPtrToValuePtr<void>(Unit);
        UObject* Texture=nullptr;
        UObject* Sprite=nullptr;
        for (TFieldIterator<FProperty> It(Field->Struct); It; ++It)
        {
            FObjectPropertyBase* Asset=CastField<FObjectPropertyBase>(*It);
            if (!Asset) continue;
            UObject* Resource=Asset->GetObjectPropertyValue_InContainer(Data);
            const FString FieldName=Asset->GetName();
            if (FieldName.StartsWith(TEXT("Spritethumbnail_")) && Cast<UPaperSprite>(Resource)) Sprite=Resource;
            else if (FieldName.StartsWith(TEXT("thumbnail_")) && Cast<UTexture2D>(Resource)) Texture=Resource;
        }
        return Sprite ? Sprite : Texture;
    }

    bool CallNoArgumentEvent(UObject* Actor,FName Name)
    {
        if (!IsValid(Actor)) return false;
        UFunction* Function=Actor->FindFunction(Name);
        if (!Function || Function->NumParms != 0) return false;
        Actor->ProcessEvent(Function,nullptr);
        return true;
    }

    bool CallUnitArgumentEvent(UObject* Object,FName Name,AActor* Unit)
    {
        UFunction* Function=IsValid(Object) ? Object->FindFunction(Name) : nullptr;
        if (!Function || Function->NumParms!=1 || !IsValid(Unit)) return false;
        FObjectPropertyBase* Argument=nullptr;
        for (TFieldIterator<FProperty> It(Function); It; ++It)
            if (It->HasAnyPropertyFlags(CPF_Parm) && !It->HasAnyPropertyFlags(CPF_OutParm|CPF_ReturnParm))
                Argument=CastField<FObjectPropertyBase>(*It);
        if (!Argument || !Unit->IsA(Argument->PropertyClass)) return false;
        TArray<uint8> Parameters;
        Parameters.SetNumZeroed(Function->ParmsSize);
        Argument->SetObjectPropertyValue_InContainer(Parameters.GetData(),Unit);
        Object->ProcessEvent(Function,Parameters.GetData());
        return true;
    }

    TArray<UUserWidget*> WidgetArray(UObject* Object,FName Name)
    {
        TArray<UUserWidget*> Result;
        FArrayProperty* Array=CastField<FArrayProperty>(Property(Object,Name));
        FObjectPropertyBase* Inner=Array ? CastField<FObjectPropertyBase>(Array->Inner) : nullptr;
        if (!Inner) return Result;
        FScriptArrayHelper Helper(Array,Array->ContainerPtrToValuePtr<void>(Object));
        for (int32 I=0; I<Helper.Num(); ++I)
            Result.Add(Cast<UUserWidget>(Inner->GetObjectPropertyValue(Helper.GetRawPtr(I))));
        return Result;
    }

    UWidget* NamedWidget(UObject* Object,FName Name)
    {
        if (UWidget* Found=Cast<UWidget>(ObjectValue(Object,Name))) return Found;
        UUserWidget* OwnerWidget=Cast<UUserWidget>(Object);
        return OwnerWidget && OwnerWidget->WidgetTree ? OwnerWidget->WidgetTree->FindWidget(Name) : nullptr;
    }

    FText WidgetText(UObject* Object,FName Name)
    {
        UTextBlock* Label=Cast<UTextBlock>(NamedWidget(Object,Name));
        return Label ? Label->GetText() : FText::GetEmpty();
    }

    UUserWidget* ChildMenu(UObject* Root,FName Name)
    {
        if (UUserWidget* Result=Cast<UUserWidget>(ObjectValue(Root,Name))) return Result;
        UUserWidget* RootWidget=Cast<UUserWidget>(Root);
        return RootWidget && RootWidget->WidgetTree ? Cast<UUserWidget>(RootWidget->WidgetTree->FindWidget(Name)) : nullptr;
    }

    FText TableText(UDataTable* Table,FName ID,const FString& FieldName)
    {
        if (!Table || !Table->GetRowStruct()) return FText::GetEmpty();
        uint8* const* Row=Table->GetRowMap().Find(ID);
        if (!Row) return FText::GetEmpty();
        for (TFieldIterator<FProperty> It(Table->GetRowStruct()); It; ++It)
            if (FTextProperty* Field=CastField<FTextProperty>(*It))
                if (Field->GetName()==FieldName || Field->GetName().StartsWith(FieldName+TEXT("_")))
                    return Field->GetPropertyValue_InContainer(*Row);
        return FText::GetEmpty();
    }

    bool HasZeroMPCost(UDataTable* Table,FName ID)
    {
        if (!Table || !Table->GetRowStruct()) return false;
        uint8* const* Row=Table->GetRowMap().Find(ID);
        if (!Row) return false;
        for (TFieldIterator<FProperty> It(Table->GetRowStruct()); It; ++It)
            if (FIntProperty* Cost=CastField<FIntProperty>(*It))
                if (Cost->GetName()==TEXT("MPCost") || Cost->GetName().StartsWith(TEXT("MPCost_")))
                    return Cost->GetPropertyValue_InContainer(*Row)==0;
        return false;
    }

    bool ExecuteInputAction(AActor* Actor,FName Action,FKey Key)
    {
        if (!IsValid(Actor)) return false;
        TArray<UInputComponent*> Components;
        Actor->GetComponents<UInputComponent>(Components);
        // Copy delegates first: a Blueprint callback can change input components.
        TArray<FInputActionUnifiedDelegate> Delegates;
        for (UInputComponent* Input : Components)
        {
            if (!IsValid(Input)) continue;
            for (int32 I=0; I<Input->GetNumActionBindings(); ++I)
            {
                const FInputActionBinding& Binding=Input->GetActionBinding(I);
                if (Binding.GetActionName()==Action && Binding.KeyEvent==IE_Pressed && Binding.ActionDelegate.IsBound())
                    Delegates.Add(Binding.ActionDelegate);
            }
        }
        for (FInputActionUnifiedDelegate& Delegate : Delegates) Delegate.Execute(Key);
        return Delegates.Num()>0;
    }
}

UReditusBattleHUDPresenterComponent::UReditusBattleHUDPresenterComponent()
{
    PrimaryComponentTick.bCanEverTick=true;
    PrimaryComponentTick.TickInterval=0.05f;
    WidgetClass=UReditusBattleHUDWidget::StaticClass();
    // Description tables contain gameplay Blueprint class references. Do not
    // load them from the native class default object during module startup.
    // BeginNativeMenu resolves the relevant table during an actual battle.
}

UReditusBattleHUDWidget* UReditusBattleHUDPresenterComponent::CreateBattleHUD(APlayerController* PlayerController,int32 ZOrder)
{
    if (IsValid(HUD)) return HUD;
    if (!IsValid(PlayerController) || !PlayerController->IsLocalController() || !WidgetClass) return nullptr;
    Player=PlayerController;
    HUD=CreateWidget<UReditusBattleHUDWidget>(Player,WidgetClass);
    if (!HUD) return nullptr;
    HUD->CommandRouter.BindUObject(this,&UReditusBattleHUDPresenterComponent::HandleCommand);
    HUD->GameplayInputRouter.BindUObject(this,&UReditusBattleHUDPresenterComponent::ForwardGameplayInput);
    HUD->OnSelectedUnitChanged.AddUniqueDynamic(this,&UReditusBattleHUDPresenterComponent::HandleSelection);
    HUD->OnInputDeviceChanged.AddUniqueDynamic(this,&UReditusBattleHUDPresenterComponent::HandleDevice);
    HUD->OnMenuEntryChosen.AddUniqueDynamic(this,&UReditusBattleHUDPresenterComponent::HandleNativeMenuChosen);
    HUD->OnMenuCancelled.AddUniqueDynamic(this,&UReditusBattleHUDPresenterComponent::HandleNativeMenuCancelled);
    HUD->SetInputDevice(Boolean(Player,TEXT("IsUsingKeyboard"),true) ? EReditusHUDInputDevice::KeyboardMouse : EReditusHUDInputDevice::Xbox);
    if (!HUD->AddToPlayerScreen(ZOrder))
    {
        HUD=nullptr;
        return nullptr;
    }
    if (bSetGameAndUIInputMode)
    {
        FInputModeGameAndUI Mode;
        Mode.SetWidgetToFocus(HUD->TakeWidget());
        Mode.SetHideCursorDuringCapture(false);
        Player->SetInputMode(Mode);
    }
    ApplyCombatInputProfile();
    RefreshBattleHUD();
    return HUD;
}

void UReditusBattleHUDPresenterComponent::DestroyBattleHUD()
{
    RestoreCombatInputProfile();
    ClearNativeMenu();
    for (int32 I=0; I<SuppressedMenuWidgets.Num(); ++I)
        if (IsValid(SuppressedMenuWidgets[I]) && SuppressedMenuVisibility.IsValidIndex(I))
        {
            SuppressedMenuWidgets[I]->SetVisibility(SuppressedMenuVisibility[I]);
            if (SuppressedMenuOpacity.IsValidIndex(I)) SuppressedMenuWidgets[I]->SetRenderOpacity(SuppressedMenuOpacity[I]);
        }
    SuppressedMenuWidgets.Reset();
    SuppressedMenuVisibility.Reset();
    SuppressedMenuOpacity.Reset();
    ReleaseLegacyMenuRoot();
    if (IsValid(HUD))
    {
        HUD->CommandRouter.Unbind();
        HUD->GameplayInputRouter.Unbind();
        HUD->RemoveFromParent();
    }
    HUD=nullptr;
    DisplaySlots.Reset();
    PreviousLegacyUnit.Reset();
    for (int32 I=0; I<HiddenLegacyWidgets.Num(); ++I)
        if (IsValid(HiddenLegacyWidgets[I]) && PreviousLegacyVisibility.IsValidIndex(I))
            HiddenLegacyWidgets[I]->SetVisibility(PreviousLegacyVisibility[I]);
    HiddenLegacyWidgets.Reset();
    PreviousLegacyVisibility.Reset();
}

void UReditusBattleHUDPresenterComponent::ApplyCombatInputProfile()
{
    if (!bApplyCombatGamepadProfile || !HUD || !Player || !Player->PlayerInput || InputOverrides.Num()>0) return;
    UPlayerInput* Input=Player->PlayerInput;
    const auto Override=[this,Input](FName Action,FKey Key,bool AllDevices=false,FKey Keyboard=FKey()) {
        if (Action.IsNone()) return;
        FInputOverride Saved;
        Saved.Action=Action;
        Saved.Before=Input->GetKeysForAction(Action);
        Saved.Applied=Saved.Before;
        Saved.Applied.RemoveAll([AllDevices](const FInputActionKeyMapping& Mapping) { return AllDevices || Mapping.Key.IsGamepadKey(); });
        if (Key.IsValid()) Saved.Applied.AddUnique(FInputActionKeyMapping(Action,Key));
        if (Keyboard.IsValid()) Saved.Applied.AddUnique(FInputActionKeyMapping(Action,Keyboard));
        for (const auto& Mapping : Saved.Before) Input->RemoveActionMapping(Mapping);
        for (const auto& Mapping : Saved.Applied) Input->AddActionMapping(Mapping);
        InputOverrides.Add(Saved);
    };
    Override(HUD->InputActions.Block,HUD->GamepadKeys.Block);
    Override(HUD->InputActions.DecreaseAmp,HUD->GamepadKeys.DecreaseAmp);
    Override(HUD->InputActions.IncreaseAmp,HUD->GamepadKeys.IncreaseAmp);
    // Chords are owned by the HUD. Remove the old unmodified Y party action.
    Override(HUD->InputActions.ChangeParty,FKey());
    Override(TEXT("MenuConfirm"),HUD->GamepadKeys.Bottom);
    Override(TEXT("MenuCancel"),HUD->GamepadBackKey,true,HUD->KeyboardBackKey);
    Input->ForceRebuildingKeyMaps(false);
}

void UReditusBattleHUDPresenterComponent::RestoreCombatInputProfile()
{
    if (Player && Player->PlayerInput)
    {
        UPlayerInput* Input=Player->PlayerInput;
        for (const FInputOverride& Saved : InputOverrides)
        {
            const TArray<FInputActionKeyMapping> Current=Input->GetKeysForAction(Saved.Action);
            bool Unchanged=Current.Num()==Saved.Applied.Num();
            for (const auto& Mapping : Current) Unchanged=Unchanged && Saved.Applied.Contains(Mapping);
            // A settings screen may have rebound the action during the battle.
            // Preserve that newer mapping rather than restoring an old snapshot.
            if (!Unchanged) continue;
            for (const auto& Mapping : Current) Input->RemoveActionMapping(Mapping);
            for (const auto& Mapping : Saved.Before) Input->AddActionMapping(Mapping);
        }
        Input->ForceRebuildingKeyMaps(false);
    }
    InputOverrides.Reset();
}

UUserWidget* UReditusBattleHUDPresenterComponent::FindLegacyMenu(AActor* Unit,EReditusHUDCommand Command) const
{
    const FName Field=Command==EReditusHUDCommand::Items ? TEXT("BP_ItemUseDialogue") :
        Command==EReditusHUDCommand::Overdrive ? TEXT("BP_OverdriveDialogue") : TEXT("BP_SkillUseDialogue");
    UObject* Roots[]={ObjectValue(GetOwner(),TEXT("battleUI")),ObjectValue(Unit,TEXT("battleUI"))};
    UUserWidget* First=nullptr;
    for (UObject* Root : Roots)
        if (UUserWidget* Found=ChildMenu(Root,Field))
        {
            if (ObjectValue(Found,TEXT("Player Unit Base"))==Unit) return Found;
            if (!First) First=Found;
        }
    return First;
}

bool UReditusBattleHUDPresenterComponent::EnsureLegacyMenuRoot(AActor* Unit)
{
    if (!Player || !GetWorld() || !GetWorld()->IsGameWorld()) return false;
    UUserWidget* Root=Cast<UUserWidget>(ObjectValue(GetOwner(),TEXT("battleUI")));
    if (!Root) Root=Cast<UUserWidget>(ObjectValue(Unit,TEXT("battleUI")));
    bool Created=false;
    if (!Root && bCreateMissingMenuSource && !bAttemptedLegacyRootCreation)
    {
        bAttemptedLegacyRootCreation=true;
        UClass* SourceClass=LoadClass<UUserWidget>(nullptr,TEXT("/Game/TurnBasedJRPGTemplate/Blueprints/UI/BP_BattleUI.BP_BattleUI_C"));
        Root=SourceClass ? CreateWidget<UUserWidget>(Player,SourceClass) : nullptr;
        Created=Root!=nullptr;
    }
    if (!IsValid(Root)) return false;
    if (!AssignedLegacyRoot) AssignedLegacyRoot=Root;
    if (!ObjectValue(GetOwner(),TEXT("battleUI")))
    {
        if (!SetObjectValue(GetOwner(),TEXT("battleUI"),Root)) return false;
        bLinkedControllerRoot=true;
    }
    if (!ObjectValue(Root,TEXT("battleController"))) SetObjectValue(Root,TEXT("battleController"),GetOwner());
    if (!ObjectValue(Root,TEXT("currentBattle"))) SetObjectValue(Root,TEXT("currentBattle"),ObjectValue(GetOwner(),TEXT("currentBattle")));
    if (!ObjectValue(Root,TEXT("jRPGPlayerController"))) SetObjectValue(Root,TEXT("jRPGPlayerController"),Player);
    if (!ObjectValue(Unit,TEXT("battleUI")) && SetObjectValue(Unit,TEXT("battleUI"),Root)) LinkedLegacyRootUnits.AddUnique(Unit);
    const bool Hidden=Root->GetVisibility()==ESlateVisibility::Hidden || Root->GetVisibility()==ESlateVisibility::Collapsed;
    const bool Unattached=!Root->IsInViewport() && !Root->GetParent();
    if (!HostedLegacyRoot && (Created || Unattached || Hidden))
    {
        HostedLegacyRoot=Root;
        HostedRootVisibility=Root->GetVisibility();
        HostedRootOpacity=Root->GetRenderOpacity();
        bAddedLegacyRootToScreen=Unattached;
        Root->SetRenderOpacity(0.f);
        Root->SetVisibility(ESlateVisibility::HitTestInvisible);
        if (bAddedLegacyRootToScreen && !Root->AddToPlayerScreen(0)) { ReleaseLegacyMenuRoot(); return false; }
    }
    return true;
}

void UReditusBattleHUDPresenterComponent::ReleaseLegacyMenuRoot()
{
    if (IsValid(HostedLegacyRoot))
    {
        if (bAddedLegacyRootToScreen) HostedLegacyRoot->RemoveFromParent();
        HostedLegacyRoot->SetVisibility(HostedRootVisibility);
        HostedLegacyRoot->SetRenderOpacity(HostedRootOpacity);
    }
    if (bLinkedControllerRoot && ObjectValue(GetOwner(),TEXT("battleUI"))==AssignedLegacyRoot) SetObjectValue(GetOwner(),TEXT("battleUI"),nullptr);
    for (AActor* Unit : LinkedLegacyRootUnits)
        if (ObjectValue(Unit,TEXT("battleUI"))==AssignedLegacyRoot) SetObjectValue(Unit,TEXT("battleUI"),nullptr);
    HostedLegacyRoot=nullptr;
    AssignedLegacyRoot=nullptr;
    LinkedLegacyRootUnits.Reset();
    bAddedLegacyRootToScreen=false;
    bLinkedControllerRoot=false;
    bAttemptedLegacyRootCreation=false;
}

void UReditusBattleHUDPresenterComponent::RecoverMenuPreparation(AActor* Unit,EReditusHUDCommand Command)
{
    if (!bAwaitingNativeMenu || PendingMenuOwner!=Unit || PendingMenuCommand!=Command || !IsValid(Unit) || Boolean(Unit,TEXT("isInputBlocked"))) return;
    UUserWidget* Source=FindLegacyMenu(Unit,Command);
    const int32 Selection=static_cast<int32>(Number(Unit,TEXT("SelectType"),-1.0));
    if (Source && (ObjectValue(Source,TEXT("Player Unit Base"))!=Unit || Selection==3))
    {
        const FName Event=Command==EReditusHUDCommand::Items ? TEXT("ShowItemInventory") :
            Command==EReditusHUDCommand::Overdrive ? TEXT("ShowOverdrive") : TEXT("ShowSkills");
        if (CallUnitArgumentEvent(GetOwner(),Event,Unit))
        {
            UUserWidget* Prepared=FindLegacyMenu(Unit,Command);
            const int32 PreparedSelection=static_cast<int32>(Number(Unit,TEXT("SelectType"),-1.0));
            if (IsValid(Prepared) && ObjectValue(Prepared,TEXT("Player Unit Base"))==Unit &&
                ObjectValue(GetOwner(),TEXT("currentAttackingUnit"))==Unit && !Boolean(Unit,TEXT("isInputBlocked")) &&
                (PreparedSelection==0 || PreparedSelection==3))
            {
                SetSelectionValue(Unit,0);
                CallNoArgumentEvent(Unit,TEXT("ResetInput"));
            }
        }
    }
}

void UReditusBattleHUDPresenterComponent::BeginNativeMenu(AActor* Unit,EReditusHUDCommand Command)
{
    if (!HUD || !IsValid(Unit) || !bUseNativeCommandLists || (Command!=EReditusHUDCommand::Tech && Command!=EReditusHUDCommand::Items && Command!=EReditusHUDCommand::Overdrive)) return;
    UWorld* BattleWorld=GetWorld();
    if (!BattleWorld || !BattleWorld->IsGameWorld()) return;
    PendingOriginalSelection=static_cast<int32>(Number(Unit,TEXT("SelectType"),-1.0));
    PendingMenuOwner=Unit;
    PendingMenuCommand=Command;
    bAwaitingNativeMenu=true;
    const FText OpeningTitle=FText::FromString(Command==EReditusHUDCommand::Items ? TEXT("ITEMS") : Command==EReditusHUDCommand::Overdrive ? TEXT("OVERDRIVE") : TEXT("SKILLS"));
    HUD->ShowListStatus(OpeningTitle,NSLOCTEXT("ReditusHUD","LoadingChoices","Loading available choices..."));
    EnsureLegacyMenuRoot(Unit);
    // Keep explicit table assignments; resolve the default only when opening
    // a game-time list, after the editor/plugin startup phases have completed.
    UDataTable*& DescriptionTable=Command==EReditusHUDCommand::Items ? ItemDescriptions : SkillDescriptions;
    if (!IsValid(DescriptionTable))
    {
        const TCHAR* DescriptionTablePath=Command==EReditusHUDCommand::Items ?
            TEXT("/Game/TurnBasedJRPGTemplate/Blueprints/DataTable/DT_UsableItems.DT_UsableItems") :
            TEXT("/Game/TurnBasedJRPGTemplate/Blueprints/DataTable/DT_Skills.DT_Skills");
        DescriptionTable=LoadObject<UDataTable>(nullptr,DescriptionTablePath);
        if (!DescriptionTable)
            UE_LOG(LogReditusBattleHUD,Warning,TEXT("HUD description table unavailable: %s. The list will keep its existing labels and command continuation."),DescriptionTablePath);
    }
    PendingMenuDeadline=BattleWorld->GetRealTimeSeconds()+1.0;
}

void UReditusBattleHUDPresenterComponent::TryCaptureNativeMenu()
{
    if (!bAwaitingNativeMenu || !HUD) return;
    if (!IsValid(PendingMenuOwner)) { ClearNativeMenu(); HUD->CloseListMenu(); return; }
    if (ObjectValue(GetOwner(),TEXT("currentAttackingUnit"))!=PendingMenuOwner || Number(PendingMenuOwner,TEXT("currentHP"))<=0.0)
    {
        ClearNativeMenu();
        HUD->CloseListMenu();
        return;
    }
    const FName Field=PendingMenuCommand==EReditusHUDCommand::Items ? TEXT("BP_ItemUseDialogue") :
        PendingMenuCommand==EReditusHUDCommand::Overdrive ? TEXT("BP_OverdriveDialogue") : TEXT("BP_SkillUseDialogue");
    UObject* MenuRoots[]={ObjectValue(GetOwner(),TEXT("battleUI")),ObjectValue(PendingMenuOwner,TEXT("battleUI"))};
    UUserWidget* Source=nullptr;
    UUserWidget* Candidate=nullptr;
    for (UObject* MenuRoot : MenuRoots)
    {
        UUserWidget* Found=ChildMenu(MenuRoot,Field);
        if (!IsValid(Found)) continue;
        Candidate=Found;
        if (ObjectValue(Candidate,TEXT("Player Unit Base"))==PendingMenuOwner) { Source=Candidate; break; }
    }
    if (!Source)
    {
        if (GetWorld() && GetWorld()->GetRealTimeSeconds()>PendingMenuDeadline)
        {
            bAwaitingNativeMenu=false;
            LastRouteStatus=FString::Printf(TEXT("Native list source unavailable: %s; requested owner=%s; candidate=%s; candidate owner=%s; controller battleUI=%s; unit battleUI=%s"),
                *Field.ToString(),*GetNameSafe(PendingMenuOwner),*GetNameSafe(Candidate),*GetNameSafe(ObjectValue(Candidate,TEXT("Player Unit Base"))),
                *GetNameSafe(MenuRoots[0]),*GetNameSafe(MenuRoots[1]));
            UE_LOG(LogReditusBattleHUD,Warning,TEXT("%s. Existing dialogue remains authoritative."),*LastRouteStatus);
            HUD->ShowListStatus(FText::FromString(PendingMenuCommand==EReditusHUDCommand::Items ? TEXT("ITEMS") : PendingMenuCommand==EReditusHUDCommand::Overdrive ? TEXT("OVERDRIVE") : TEXT("SKILLS")),
                NSLOCTEXT("ReditusHUD","ChoicesUnavailable","Choices could not be prepared. Press Back to return."));
        }
        return;
    }
    NativeMenuSource=Source;
    NativeMenuOwner=PendingMenuOwner;
    NativeMenuCommand=PendingMenuCommand;
    PendingMenuOwner=nullptr;
    bAwaitingNativeMenu=false;
    const int32 Existing=SuppressedMenuWidgets.IndexOfByKey(Source);
    if (Existing==INDEX_NONE)
    {
        SuppressedMenuWidgets.Add(Source);
        SuppressedMenuVisibility.Add(Source->GetVisibility());
        SuppressedMenuOpacity.Add(Source->GetRenderOpacity());
    }
    Source->SetVisibility(ESlateVisibility::HitTestInvisible);
    Source->SetRenderOpacity(0.f);
    RefreshNativeMenuEntries(true);
}

void UReditusBattleHUDPresenterComponent::RefreshNativeMenuEntries(bool bOpen)
{
    if (!NativeMenuSource || !HUD) return;
    const bool Items=NativeMenuCommand==EReditusHUDCommand::Items;
    NativeMenuRows=WidgetArray(NativeMenuSource,Items ? TEXT("itemUIs") : TEXT("skillUIs"));
    NativeMenuIDs.Init(NAME_None,NativeMenuRows.Num());
    NativeMenuDataIDs.Init(NAME_None,NativeMenuRows.Num());
    TArray<FReditusBattleHUDMenuEntry> Entries;
    TSet<FName> Used;
    bool CopiedFonts=false;
    for (int32 I=0; I<NativeMenuRows.Num(); ++I)
    {
        UUserWidget* Row=NativeMenuRows[I];
        if (!IsValid(Row)) continue;
        if (!HUD->ListSelectionBrush.GetResourceObject())
            if (UImage* SelectedImage=Cast<UImage>(NamedWidget(Row,TEXT("SelectedImage"))))
                if (SelectedImage->Brush.GetResourceObject())
                {
                    HUD->ListSelectionBrush=SelectedImage->Brush;
                    HUD->ListSelectionBrush.DrawAs=ESlateBrushDrawType::Image;
                    HUD->ListSelectionBrush.TintColor=FSlateColor(FLinearColor::White);
                }
        const FName DataID=NameValue(Row,Items ? TEXT("item") : TEXT("skill"));
        if (DataID.IsNone()) continue;
        FReditusBattleHUDMenuEntry Entry;
        Entry.ID=DataID;
        if (Used.Contains(Entry.ID)) Entry.ID=FName(*FString::Printf(TEXT("HUDRow_%d"),I));
        Used.Add(Entry.ID);
        NativeMenuIDs[I]=Entry.ID;
        NativeMenuDataIDs[I]=DataID;
        Entry.Label=WidgetText(Row,Items ? TEXT("ItemName") : TEXT("SkillName"));
        UDataTable* DisplayTable=Items ? ItemDescriptions : SkillDescriptions;
        if (Entry.Label.IsEmpty()) Entry.Label=TableText(DisplayTable,DataID,TEXT("DisplayName"));
        if (Entry.Label.IsEmpty()) Entry.Label=FText::FromName(DataID);
        // Item quantity belongs to this row's item ID, never the inventory's entry count.
        Entry.Detail=Items ? FText::Format(NSLOCTEXT("ReditusHUD","ItemQuantity","x{0}"),
            FText::AsNumber(FMath::Max(0,static_cast<int32>(Number(Row,TEXT("amount")))))) : WidgetText(Row,TEXT("MPAmount"));
        if (!Items && HasZeroMPCost(DisplayTable,DataID)) Entry.Detail=FText::GetEmpty();
        Entry.Description=TableText(DisplayTable,DataID,TEXT("ShortDescription"));
        if (UImage* RowIcon=Cast<UImage>(NamedWidget(Row,Items ? TEXT("ItemImage") : TEXT("SkillImage"))))
            Entry.Icon=RowIcon->Brush;
        if (!CopiedFonts)
        {
            UTextBlock* NameText=Cast<UTextBlock>(NamedWidget(Row,Items ? TEXT("ItemName") : TEXT("SkillName")));
            if (NameText)
            {
                HUD->CopyLegacyListFonts(NameText,Cast<UTextBlock>(NamedWidget(Row,Items ? TEXT("AmountText") : TEXT("MPAmount"))),
                    Cast<UTextBlock>(NamedWidget(NativeMenuSource,Items ? TEXT("ItemDescriptionText") : TEXT("SkillDescriptionText"))));
                CopiedFonts=true;
            }
        }
        Entry.bEnabled=Row->GetIsEnabled() && (!Items || Number(Row,TEXT("amount"))>0.0);
        if (UWidget* Inner=Cast<UWidget>(ObjectValue(Row,Items ? TEXT("ItemButton") : TEXT("SkillButton"))))
            Entry.bEnabled=Entry.bEnabled && Inner->GetIsEnabled();
        Entries.Add(Entry);
    }
    NativeMenuSource->SetVisibility(ESlateVisibility::HitTestInvisible);
    NativeMenuSource->SetRenderOpacity(0.f);
    if (bOpen)
    {
        LastRouteStatus=FString::Printf(TEXT("Opened %s for %s: %d generated rows, %d displayed entries"),
            Items ? TEXT("Items") : NativeMenuCommand==EReditusHUDCommand::Overdrive ? TEXT("Overdrive") : TEXT("Skills"),
            *GetNameSafe(NativeMenuOwner),NativeMenuRows.Num(),Entries.Num());
        if (bLogCommandRouting || Entries.Num()==0) UE_LOG(LogReditusBattleHUD,Log,TEXT("%s"),*LastRouteStatus);
        HUD->ShowListMenu(FText::FromString(Items ? TEXT("ITEMS") : NativeMenuCommand==EReditusHUDCommand::Overdrive ? TEXT("OVERDRIVE") : TEXT("SKILLS")),Entries);
    }
    else HUD->UpdateListEntries(Entries);
}

void UReditusBattleHUDPresenterComponent::ClearNativeMenu()
{
    NativeMenuSource=nullptr;
    NativeMenuOwner=nullptr;
    PendingMenuOwner=nullptr;
    NativeMenuRows.Reset();
    NativeMenuIDs.Reset();
    NativeMenuDataIDs.Reset();
    bAwaitingNativeMenu=false;
    PendingOriginalSelection=-1;
}

void UReditusBattleHUDPresenterComponent::HandleNativeMenuChosen(AActor* Unit,FName EntryID)
{
    if (!NativeMenuSource || !HUD || Unit!=NativeMenuOwner || !IsValid(Unit) || Number(Unit,TEXT("currentHP"))<=0.0 ||
        ObjectValue(GetOwner(),TEXT("currentAttackingUnit"))!=Unit) return;
    const int32 Index=NativeMenuIDs.IndexOfByKey(EntryID);
    if (!NativeMenuRows.IsValidIndex(Index) || !IsValid(NativeMenuRows[Index]) || !NativeMenuRows[Index]->GetIsEnabled()) return;
    const bool Items=NativeMenuCommand==EReditusHUDCommand::Items;
    const TArray<UUserWidget*> CurrentRows=WidgetArray(NativeMenuSource,Items ? TEXT("itemUIs") : TEXT("skillUIs"));
    const int32 CurrentIndex=CurrentRows.IndexOfByKey(NativeMenuRows[Index]);
    UUserWidget* Row=NativeMenuRows[Index];
    if (CurrentIndex==INDEX_NONE || !NativeMenuDataIDs.IsValidIndex(Index) ||
        NameValue(Row,Items ? TEXT("item") : TEXT("skill"))!=NativeMenuDataIDs[Index] ||
        (Items && Number(Row,TEXT("amount"))<=0.0))
    {
        RefreshNativeMenuEntries(false);
        return;
    }
    FIntProperty* Selection=CastField<FIntProperty>(Property(NativeMenuSource,Items ? TEXT("selectedItemIndex") : TEXT("selectedSkillIndex")));
    if (!Selection) return;
    Selection->SetPropertyValue_InContainer(NativeMenuSource,CurrentIndex);
    if (!CallNoArgumentEvent(NativeMenuSource,Items ? TEXT("HighlightSelectedItem") : TEXT("HighlightSelectedSkill"))) return;
    UWidget* Confirm=Cast<UWidget>(ObjectValue(NativeMenuSource,TEXT("ConfirmButton")));
    if (Confirm && !Confirm->GetIsEnabled()) return;
    UUserWidget* Source=NativeMenuSource;
    UFunction* ConfirmEvent=Source->FindFunction(TEXT("Confirm"));
    if (!ConfirmEvent || ConfirmEvent->NumParms!=0) return;
    ClearNativeMenu();
    HUD->CloseListMenu();
    CallNoArgumentEvent(Source,TEXT("Confirm"));
    RefreshBattleHUD();
}

void UReditusBattleHUDPresenterComponent::HandleNativeMenuCancelled(AActor* Unit)
{
    if (!HUD) return;
    if (!NativeMenuSource)
    {
        if (!PendingMenuOwner || Unit!=PendingMenuOwner) return;
        const bool CanRestore=ReditusHUDInputPolicy::CanRollbackPendingMenu(ObjectValue(GetOwner(),TEXT("currentAttackingUnit"))==Unit,
            Number(Unit,TEXT("currentHP"))>0.0,Boolean(Unit,TEXT("isInputBlocked")),PendingOriginalSelection,
            static_cast<int32>(Number(Unit,TEXT("SelectType"),-1.0)));
        const int32 PreviousSelection=PendingOriginalSelection;
        ClearNativeMenu();
        if (CanRestore && SetSelectionValue(Unit,PreviousSelection))
        {
            CallNoArgumentEvent(Unit,TEXT("ResetInput"));
            CallNoArgumentEvent(Unit,TEXT("AttackCancelled"));
        }
        RefreshBattleHUD();
        return;
    }
    if (Unit!=NativeMenuOwner) return;
    UUserWidget* Source=NativeMenuSource;
    const bool CanCancel=ReditusHUDInputPolicy::CanCancelOwnedMenu(ObjectValue(GetOwner(),TEXT("currentAttackingUnit"))==Unit,
        IsValid(Unit) && Number(Unit,TEXT("currentHP"))>0.0,Boolean(Unit,TEXT("isInputBlocked")));
    ClearNativeMenu();
    if (CanCancel) CallNoArgumentEvent(Source,TEXT("Cancel"));
    RefreshBattleHUD();
}

void UReditusBattleHUDPresenterComponent::TickComponent(float DeltaTime,ELevelTick TickType,FActorComponentTickFunction* ThisTickFunction)
{
    Super::TickComponent(DeltaTime,TickType,ThisTickFunction);
    if (IsValid(HUD) && bReadLegacyBattle) RefreshBattleHUD();
}

void UReditusBattleHUDPresenterComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    DestroyBattleHUD();
    Super::EndPlay(EndPlayReason);
}

void UReditusBattleHUDPresenterComponent::SetPartyUnits(const TArray<AActor*>& Units)
{
    bExplicitParty=true;
    PartyUnits=Units;
    RefreshBattleHUD();
}

void UReditusBattleHUDPresenterComponent::SetUnitReadiness(AActor* Unit,float Percent,bool bReady,bool bCanAct,bool bQueued)
{
    if (!IsValid(Unit)) return;
    FReditusHUDReadinessOverride& Value=Readiness.FindOrAdd(Unit);
    Value.Percent=ReditusHUDPolicy::Ratio(Percent,1.f);
    Value.bReady=bReady;
    Value.bCanAct=bCanAct;
    Value.bQueued=bQueued;
    RefreshBattleHUD();
}

void UReditusBattleHUDPresenterComponent::ClearReadinessOverrides() { Readiness.Reset(); RefreshBattleHUD(); }
void UReditusBattleHUDPresenterComponent::SetCommandWindowEnabled(bool bEnabled)
{
    bExplicitCommandWindow=true;
    bCommandWindowEnabled=bEnabled;
    RefreshBattleHUD();
}
void UReditusBattleHUDPresenterComponent::FollowLegacyCommandWindow() { bExplicitCommandWindow=false; RefreshBattleHUD(); }

FReditusBattleHUDUnit UReditusBattleHUDPresenterComponent::ReadUnit(AActor* Unit,AActor* CurrentActor,bool bCommandWindow) const
{
    FReditusBattleHUDUnit Data;
    Data.Unit=Unit;
    if (!IsValid(Unit)) return Data;
    Data.CharacterID=NameValue(Unit,TEXT("CharacterID"));
    Data.DisplayName=Title(Unit);
    if (Data.DisplayName.IsEmpty()) Data.DisplayName=FText::FromName(Data.CharacterID);
    Data.Portrait=Thumbnail(Unit,TEXT("battleImage"));
    if (!Data.Portrait) Data.Portrait=Thumbnail(Unit,TEXT("thumbnail"));
    Data.HP=static_cast<float>(Number(Unit,TEXT("currentHP")));
    Data.MaxHP=static_cast<float>(Number(Unit,TEXT("maxHP")));
    Data.MP=static_cast<float>(Number(Unit,TEXT("currentMP")));
    Data.MaxMP=static_cast<float>(Number(Unit,TEXT("maxMP")));
    Data.Overdrive=static_cast<float>(Number(Unit,TEXT("CurrentOverDrivePoints")));
    Data.MaxOverdrive=static_cast<float>(Number(Unit,TEXT("MaxOverdrivePoints")));
    Data.bCanOverdrive=Boolean(Unit,TEXT("CanEnterOverdrive"));
    Data.bOverdriveEntered=Boolean(Unit,TEXT("OverdriveEntered"));
    Data.bCanHack=Data.CharacterID==HackCharacterID;
    Data.TotalAmp=static_cast<int32>(Number(Unit,TEXT("TotalAmpPoints")));
    Data.CommittedAmp=static_cast<int32>(Number(Unit,TEXT("CurrentAmpPointsInPlay")));
    Data.ATBPercent=ReditusHUDPolicy::Ratio(static_cast<float>(Number(Unit,TEXT("currentActionTime"))),
        static_cast<float>(Number(Unit,LegacyATBThresholdProperty)));
    Data.bReadyForCommands=Unit==CurrentActor && bCommandWindow;
    Data.bCanAct=Data.HP>0.f;
    if (Data.bReadyForCommands) Data.ATBPercent=1.f;
    if (const FReditusHUDReadinessOverride* Override=Readiness.Find(Unit))
    {
        Data.ATBPercent=Override->Percent;
        Data.bReadyForCommands=Override->bReady;
        Data.bCanAct=Override->bCanAct && Data.HP>0.f;
        Data.bQueued=Override->bQueued;
    }
    return Data;
}

void UReditusBattleHUDPresenterComponent::UpdateDisplaySlots(UObject* Battle,AActor* CurrentActor,bool bCommandWindow)
{
    DisplaySlots.SetNum(ReditusHUDPolicy::MaxActiveUnits);
    const TSet<FName> DeadIDs=DeadCharacterIDs(Battle);
    UObject* RosterOwner=ObjectValue(Battle,TEXT("jRPGPlayerController"));
    if (!RosterOwner) RosterOwner=Player;
    const TArray<FName> ActiveIDs=bExplicitParty ? TArray<FName>() : Names(RosterOwner,TEXT("ActivePartyCharacterIDs"));
    TArray<FReditusBattleHUDUnit> Incoming;
    for (AActor* Unit : PartyUnits)
    {
        FReditusBattleHUDUnit Data=ReadUnit(Unit,CurrentActor,bCommandWindow);
        if (!IsValid(Unit) || Incoming.ContainsByPredicate([&Data](const FReditusBattleHUDUnit& Other) {
            return Other.Unit==Data.Unit || (!Data.CharacterID.IsNone() && Other.CharacterID==Data.CharacterID);
        })) continue;
        Incoming.Add(Data);
        if (Incoming.Num()==ReditusHUDPolicy::MaxActiveUnits) break;
    }
    const auto SameCharacter=[](const FReditusBattleHUDUnit& First,const FReditusBattleHUDUnit& Second) {
        return (!First.CharacterID.IsNone() && First.CharacterID==Second.CharacterID) ||
            (First.Unit!=nullptr && First.Unit==Second.Unit);
    };
    const bool HasNewMember=Incoming.ContainsByPredicate([this,&SameCharacter](const FReditusBattleHUDUnit& Data) {
        return !DisplaySlots.ContainsByPredicate([&Data,&SameCharacter](const FReditusBattleHUDUnit& Old) { return SameCharacter(Old,Data); });
    });
    bool Matches[ReditusHUDPolicy::MaxActiveUnits][ReditusHUDPolicy::MaxActiveUnits] = {};
    bool Occupied[ReditusHUDPolicy::MaxActiveUnits] = {};
    bool Defeated[ReditusHUDPolicy::MaxActiveUnits] = {};
    for (int32 I=0; I<DisplaySlots.Num(); ++I)
    {
        const FReditusBattleHUDUnit& Old=DisplaySlots[I];
        Occupied[I]=Old.HasCharacter();
        const bool KnownDefeated=Old.HP<=0.f || (!Old.CharacterID.IsNone() && DeadIDs.Contains(Old.CharacterID)) ||
            (IsValid(Old.Unit) && (Number(Old.Unit,TEXT("currentHP"))<=0.0 || Boolean(Old.Unit,TEXT("IsDead?"))));
        const bool RemovedFromRoster=!Old.CharacterID.IsNone() && ActiveIDs.Num()>0 && !ActiveIDs.Contains(Old.CharacterID);
        // Permanent-death paths can destroy/remove an actor between polls,
        // without using the revivable-death map. Keep its cached KO card too.
        // A real new roster member can still replace the outgoing position.
        Defeated[I]=ReditusHUDPolicy::KeepMissingCard(Occupied[I],KnownDefeated,IsValid(Old.Unit),HasNewMember,RemovedFromRoster);
        for (int32 J=0; J<Incoming.Num(); ++J)
            Matches[I][J]=Occupied[I] && SameCharacter(Old,Incoming[J]);
    }
    const ReditusHUDPolicy::PartySlotPlan Plan=ReditusHUDPolicy::PlanPartySlots(Incoming.Num(),Matches,Occupied,Defeated);
    for (int32 I=0; I<DisplaySlots.Num(); ++I)
    {
        if (Incoming.IsValidIndex(Plan.Incoming[I])) DisplaySlots[I]=Incoming[Plan.Incoming[I]];
        else if (Plan.KeepDefeated[I])
        {
            FReditusBattleHUDUnit& Data=DisplaySlots[I];
            if (IsValid(Data.Unit)) Data=ReadUnit(Data.Unit,CurrentActor,false);
            else Data.Unit=nullptr;
            Data.HP=0.f;
            Data.ATBPercent=0.f;
            Data.bReadyForCommands=false;
            Data.bCanAct=false;
            Data.bQueued=false;
            Data.bOverdriveEntered=false;
        }
        else DisplaySlots[I]=FReditusBattleHUDUnit();
    }
}

void UReditusBattleHUDPresenterComponent::RefreshBattleHUD()
{
    if (!IsValid(HUD) || bRefreshing) return;
    TGuardValue<bool> Guard(bRefreshing,true);
    AActor* Controller=GetOwner();
    if (Boolean(Controller,TEXT("isBattleOver"))) { DestroyBattleHUD(); return; }
    UObject* Battle=ObjectValue(Controller,TEXT("currentBattle"));
    // Enemy skill preparation also calls battleUI before any list has opened.
    // Keep the backing root available from HUD creation's first game-world refresh.
    if (bReadLegacyBattle && bRouteLegacyCommands && IsValid(Battle))
        EnsureLegacyMenuRoot(Cast<AActor>(ObjectValue(Controller,TEXT("currentAttackingUnit"))));
    if (!bExplicitParty)
    {
        PartyUnits=Actors(Battle,TEXT("playerUnits"));
    }
    PartyUnits.RemoveAll([](AActor* Actor){return !IsValid(Actor);});
    if (PartyUnits.Num()>ReditusHUDPolicy::MaxActiveUnits)
    {
        if (!bWarnedPartyOverflow)
            UE_LOG(LogReditusBattleHUD,Warning,TEXT("Battle HUD received %d active player units; the supported maximum is four. Pass active combatants, with reserves in the Change Party list."),PartyUnits.Num());
        bWarnedPartyOverflow=true;
        PartyUnits.SetNum(ReditusHUDPolicy::MaxActiveUnits);
    }
    else bWarnedPartyOverflow=false;
    for (auto It=Readiness.CreateIterator(); It; ++It) if (!IsValid(It.Key())) It.RemoveCurrent();
    AActor* CurrentActor=Cast<AActor>(ObjectValue(Controller,TEXT("currentAttackingUnit")));
    const bool IsPartyActor=PartyUnits.Contains(CurrentActor);
    const bool OwnsLegacyCommand=!Property(Controller,TEXT("CurrentCommandUnit")) || ObjectValue(Controller,TEXT("CurrentCommandUnit"))==CurrentActor;
    const int32 LegacySelection=static_cast<int32>(Number(CurrentActor,TEXT("SelectType"),-1.0));
    const int32 BattleMode=static_cast<int32>(Number(Controller,TEXT("BattleInputMode"),-1.0));
    // Use the unit's finer selection state as well as the controller mode.
    // The legacy graph can remain in CommandMenu while selecting a target.
    HUD->SetReactionWindow(IsValid(CurrentActor) && !IsPartyActor);
    TryCaptureNativeMenu();
    if (NativeMenuSource && (!IsValid(NativeMenuOwner) || CurrentActor!=NativeMenuOwner || Number(NativeMenuOwner,TEXT("currentHP"))<=0.0))
    {
        // A stale list is presentation cleanup, not the player's Back action.
        // Its legacy Cancel handler can restart the new current unit's turn.
        ClearNativeMenu();
        HUD->CloseListMenu();
    }
    bool CommandWindow=false;
    if (bExplicitCommandWindow) CommandWindow=bCommandWindowEnabled;
    else
    {
        UObject* OldActions=ObjectValue(Controller,TEXT("BattleActionsUI"));
        const FProperty* Mode=Property(Controller,TEXT("BattleInputMode"));
        if (Mode) CommandWindow=Number(Controller,TEXT("BattleInputMode"))==CommandMenuModeValue;
        else
        {
            CommandWindow=Boolean(OldActions,TEXT("IsShown?"));
        }
        // Specialized Party/Hack/target menus block the original actions UI.
        // Keep their face-button input instead of reopening the native cross.
        CommandWindow=ReditusHUDInputPolicy::LegacyCommandSurface(CommandWindow,LegacySelection) &&
            IsPartyActor && !Boolean(OldActions,TEXT("isUIInputBlocked"));
    }
    if (NativeMenuSource && IsValid(NativeMenuOwner) && CurrentActor==NativeMenuOwner) CommandWindow=true;
    if (IsValid(PendingMenuOwner) && CurrentActor==PendingMenuOwner && HUD->IsNativeListOpen()) CommandWindow=true;
    if (bRouteLegacyCommands && !OwnsLegacyCommand) CommandWindow=false;
    const bool TargetWindow=(bRouteLegacyCommands || !bExplicitCommandWindow) && !NativeMenuSource && !PendingMenuOwner &&
        ReditusHUDInputPolicy::LegacyTargetSurface(IsPartyActor && OwnsLegacyCommand,Number(CurrentActor,TEXT("currentHP"))>0.0,
            Boolean(CurrentActor,TEXT("isInputBlocked")),LegacySelection,BattleMode);
    if (TargetWindow) CommandWindow=false;
    HUD->SetTargetSelectionActive(TargetWindow);
    UpdateDisplaySlots(Battle,CurrentActor,CommandWindow);
    HUD->SetPartySnapshot(DisplaySlots);
    if (bFollowLegacyTurnSelection)
        ReditusHUDPolicy::FollowChangedTurn(TWeakObjectPtr<AActor>(CurrentActor),PreviousLegacyUnit,IsPartyActor,
            [this](TWeakObjectPtr<AActor> Candidate) { return HUD->SelectReadyUnit(Candidate.Get()); });
    else PreviousLegacyUnit.Reset();
    HUD->SetCommandsEnabled(CommandWindow);
    if (NativeMenuSource) RefreshNativeMenuEntries(false);
    if (!bExplicitTarget)
    {
        AActor* Target=Cast<AActor>(ObjectValue(HUD->GetSelectedUnit(),TEXT("currentTarget")));
        if (IsPartyActor && IsValid(Target) && !PartyUnits.Contains(Target)) HUD->SetTargetedEnemy(Target);
        else HUD->ClearTargetLabel();
    }
    if (!bExplicitSpeed && IsValid(Player)) HUD->SetDisplayedBattleSpeed(static_cast<float>(Number(Player,TEXT("BattleSpeed"),1.0)));
    if (bReadLegacyBattle && bMirrorLegacyActionAnnouncement)
    {
        UUserWidget* Announcement=ChildMenu(ObjectValue(Controller,TEXT("battleUI")),TEXT("BP_ShowAttackElement"));
        if (IsValid(Announcement))
        {
            const ESlateVisibility SourceVisibility=Announcement->GetVisibility();
            const bool Shown=SourceVisibility!=ESlateVisibility::Hidden && SourceVisibility!=ESlateVisibility::Collapsed;
            FSlateBrush ElementIcon;
            if (UImage* SourceIcon=Cast<UImage>(NamedWidget(Announcement,TEXT("SkillElement"))))
                if (SourceIcon->GetVisibility()!=ESlateVisibility::Hidden && SourceIcon->GetVisibility()!=ESlateVisibility::Collapsed)
                    ElementIcon=SourceIcon->Brush;
            HUD->SetActionAnnouncement(WidgetText(Announcement,TEXT("SkillName")),ElementIcon,Shown);
        }
        else HUD->ClearActionAnnouncement();
    }
    if (bHideLegacyActionsWidget)
    {
        if (UWidget* Old=Cast<UWidget>(ObjectValue(Controller,TEXT("BattleActionsUI"))))
        {
            const int32 Index=HiddenLegacyWidgets.IndexOfByKey(Old);
            if (Index==INDEX_NONE)
            {
                HiddenLegacyWidgets.Add(Old);
                PreviousLegacyVisibility.Add(Old->GetVisibility());
            }
            Old->SetVisibility(ESlateVisibility::Hidden);
        }
    }
    for (UWidget* Widget : HiddenLegacyWidgets) if (IsValid(Widget)) Widget->SetVisibility(ESlateVisibility::Hidden);
    for (UWidget* Widget : SuppressedMenuWidgets) if (IsValid(Widget)) { Widget->SetVisibility(ESlateVisibility::HitTestInvisible); Widget->SetRenderOpacity(0.f); }
    if (IsValid(HostedLegacyRoot)) { HostedLegacyRoot->SetVisibility(ESlateVisibility::HitTestInvisible); HostedLegacyRoot->SetRenderOpacity(0.f); }
}

void UReditusBattleHUDPresenterComponent::SetTargetedEnemy(AActor* Enemy)
{
    bExplicitTarget=true;
    if (HUD) HUD->SetTargetedEnemy(Enemy);
}
void UReditusBattleHUDPresenterComponent::SetTargetGroupLabel(FText Label)
{
    bExplicitTarget=true;
    if (HUD) HUD->SetTargetLabel(Label);
}
void UReditusBattleHUDPresenterComponent::FollowLegacyTarget() { bExplicitTarget=false; RefreshBattleHUD(); }
void UReditusBattleHUDPresenterComponent::SetDisplayedBattleSpeed(float Multiplier)
{
    bExplicitSpeed=true;
    if (HUD) HUD->SetDisplayedBattleSpeed(Multiplier);
}

void UReditusBattleHUDPresenterComponent::SetLegacyWidgetsToHide(const TArray<UWidget*>& Widgets)
{
    for (int32 I=0; I<HiddenLegacyWidgets.Num(); ++I)
        if (IsValid(HiddenLegacyWidgets[I]) && PreviousLegacyVisibility.IsValidIndex(I))
            HiddenLegacyWidgets[I]->SetVisibility(PreviousLegacyVisibility[I]);
    HiddenLegacyWidgets.Reset(); PreviousLegacyVisibility.Reset();
    for (UWidget* Widget : Widgets)
        if (IsValid(Widget) && Widget!=HUD && !HiddenLegacyWidgets.Contains(Widget))
        {
            HiddenLegacyWidgets.Add(Widget);
            PreviousLegacyVisibility.Add(Widget->GetVisibility());
            Widget->SetVisibility(ESlateVisibility::Hidden);
        }
}

bool UReditusBattleHUDPresenterComponent::HandleCommand(AActor* Unit,EReditusHUDCommand Command)
{
    const ReditusHUDInputPolicy::CommandRoute Route=ReditusHUDInputPolicy::ChooseCommandRoute(bRouteLegacyCommands,OnCommandRequested.IsBound());
    if (Route==ReditusHUDInputPolicy::CommandRoute::Legacy) return ExecuteLegacyCommand(Unit,Command);
    if (Route==ReditusHUDInputPolicy::CommandRoute::Blueprint)
    {
        LastRouteStatus=TEXT("Forwarded to Blueprint OnCommandRequested override");
        if (bLogCommandRouting) UE_LOG(LogReditusBattleHUD,Log,TEXT("%s"),*LastRouteStatus);
        OnCommandRequested.Broadcast(Unit,Command);
        return true;
    }
    LastRouteStatus=TEXT("Route Legacy Commands is disabled and OnCommandRequested has no Blueprint handler");
    if (bLogCommandRouting) UE_LOG(LogReditusBattleHUD,Warning,TEXT("%s"),*LastRouteStatus);
    return false;
}

bool UReditusBattleHUDPresenterComponent::ExecuteLegacyCommand(AActor* Unit,EReditusHUDCommand Command)
{
    if (!IsValid(Unit) || bRouting) return false;
    // Never promote a future ready entry by writing currentAttackingUnit.
    // Legacy commands are permitted only for the unit the existing controller owns.
    AActor* CurrentActor=Cast<AActor>(ObjectValue(GetOwner(),TEXT("currentAttackingUnit")));
    if (CurrentActor!=Unit)
    {
        LastRouteStatus=FString::Printf(TEXT("Command owner mismatch: selected=%s current=%s"),*GetNameSafe(Unit),*GetNameSafe(CurrentActor));
        if (bLogCommandRouting) UE_LOG(LogReditusBattleHUD,Log,TEXT("%s"),*LastRouteStatus);
        return false;
    }
    FName Function;
    switch (Command)
    {
    case EReditusHUDCommand::Attack: Function=TEXT("SelectEnemy"); break;
    case EReditusHUDCommand::Tech: Function=TEXT("TechPressed"); break;
    case EReditusHUDCommand::Items: Function=TEXT("UseItem"); break;
    case EReditusHUDCommand::Defend: Function=TEXT("Defend"); break;
    case EReditusHUDCommand::Overdrive:
        if (!Boolean(Unit,TEXT("CanEnterOverdrive")) || !Boolean(Unit,TEXT("OverdriveEntered"))) return false;
        Function=TEXT("Overdrive"); break;
    case EReditusHUDCommand::Hack:
        if (NameValue(Unit,TEXT("CharacterID"))!=HackCharacterID) return false;
        Function=TEXT("HackPressed"); break;
    case EReditusHUDCommand::Run: Function=TEXT("Run"); break;
    default: return false;
    }
    BeginNativeMenu(Unit,Command);
    TGuardValue<bool> Guard(bRouting,true);
    const bool Success=CallNoArgumentEvent(Unit,Function);
    if (Success && IsValid(HUD)) RecoverMenuPreparation(Unit,Command);
    if (!Success)
    {
        bAwaitingNativeMenu=false;
        if (IsValid(HUD) && PendingMenuOwner==Unit)
            HUD->ShowListStatus(FText::FromString(Command==EReditusHUDCommand::Items ? TEXT("ITEMS") : Command==EReditusHUDCommand::Overdrive ? TEXT("OVERDRIVE") : TEXT("SKILLS")),
                NSLOCTEXT("ReditusHUD","ChoiceCommandUnavailable","Choices are unavailable. Press Back to return."));
    }
    LastRouteStatus=FString::Printf(TEXT("%s: %s on %s"),Success ? TEXT("Called") : TEXT("Missing event"),*Function.ToString(),*GetNameSafe(Unit));
    if (bLogCommandRouting) UE_LOG(LogReditusBattleHUD,Log,TEXT("%s"),*LastRouteStatus);
    if (!Success) UE_LOG(LogReditusBattleHUD,Warning,TEXT("HUD could not call %s on %s. Bind OnCommandRequested for this command."),*Function.ToString(),*GetNameSafe(Unit));
    RefreshBattleHUD();
    return Success;
}

bool UReditusBattleHUDPresenterComponent::ForwardGameplayInput(FName ActionName,FKey Key)
{
    if (!IsValid(HUD) || bRouting) return false;
    TGuardValue<bool> Guard(bRouting,true);
    bool Routed=false;
    AActor* InputOwner=nullptr;
    if (ActionName==HUD->InputActions.Block || ActionName==HUD->InputActions.Speed)
    {
        Routed=ExecuteInputAction(Player,ActionName,Key);
        if (Routed) InputOwner=Player;
    }
    else
    {
        AActor* Unit=HUD->GetSelectedUnit();
        AActor* Current=Cast<AActor>(ObjectValue(GetOwner(),TEXT("currentAttackingUnit")));
        if (Unit==Current) { Routed=ExecuteInputAction(Unit,ActionName,Key); if (Routed) InputOwner=Unit; }
        if (!Routed) { Routed=ExecuteInputAction(Player,ActionName,Key); if (Routed) InputOwner=Player; }
    }
    if (!Routed && OnUnroutedGameplayInput.IsBound())
    {
        OnUnroutedGameplayInput.Broadcast(ActionName,Key);
        Routed=true;
        InputOwner=GetOwner();
    }
    AActor* SelectedActor=IsValid(HUD) ? HUD->GetSelectedUnit() : nullptr;
    LastRouteStatus=FString::Printf(TEXT("%s input %s; receiver=%s; selected=%s; current=%s; command owner=%s; SelectType=%d; input blocked=%d; target set=%d"),Routed ? TEXT("Forwarded") : TEXT("Unrouted"),
        *ActionName.ToString(),*GetNameSafe(InputOwner),*GetNameSafe(SelectedActor),*GetNameSafe(ObjectValue(GetOwner(),TEXT("currentAttackingUnit"))),
        *GetNameSafe(ObjectValue(GetOwner(),TEXT("CurrentCommandUnit"))),static_cast<int32>(Number(SelectedActor,TEXT("SelectType"),-1.0)),
        Boolean(SelectedActor,TEXT("isInputBlocked")),Boolean(SelectedActor,TEXT("TargetSet")));
    if (bLogCommandRouting || (!Routed && ActionName==TEXT("MenuConfirm"))) UE_LOG(LogReditusBattleHUD,Log,TEXT("%s"),*LastRouteStatus);
    RefreshBattleHUD();
    return Routed;
}

void UReditusBattleHUDPresenterComponent::HandleSelection(AActor* Unit) { OnSelectedUnitChanged.Broadcast(Unit); }
void UReditusBattleHUDPresenterComponent::HandleDevice(EReditusHUDInputDevice Device)
{
    if (!bSyncLegacyDeviceFlag || !IsValid(Player)) return;
    if (FBoolProperty* Field=CastField<FBoolProperty>(Property(Player,TEXT("IsUsingKeyboard"))))
        Field->SetPropertyValue_InContainer(Player,Device==EReditusHUDInputDevice::KeyboardMouse);
}
