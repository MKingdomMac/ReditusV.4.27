#include "ReditusTableBuffLibrary.h"
#include "ReditusTableBuffState.h"

#include "Components/SceneComponent.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Kismet/GameplayStatics.h"
#include "Modules/ModuleManager.h"
#include "NiagaraComponent.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraSystem.h"
#include "Particles/ParticleSystem.h"
#include "Particles/ParticleSystemComponent.h"
#include "Sound/SoundBase.h"
#include "UObject/EnumProperty.h"
#include "UObject/StructOnScope.h"
#include "UObject/UnrealType.h"

DEFINE_LOG_CATEGORY_STATIC(LogReditusTableBuffs, Log, All);

namespace
{
    const FName GuardUpTag(TEXT("Reditus.TableBuff.GuardUp"));
    const FName CycleEndName(TEXT("OnCycleEndRequested"));
    const FName DefenseDownSlot(TEXT("Reditus.TableBuff.DefenseDown"));
    const FName AggroFocusSlot(TEXT("Reditus.TableBuff.AggroFocus"));
    const FName AmpBlockSlot(TEXT("Reditus.TableBuff.BlockAmpGain"));
    uint64 NextAggroFocusOrder = 0;
    const FName GaugeSlot(TEXT("Reditus.TableBuff.GaugeRate"));
    const FName PreparationSlot(TEXT("Reditus.TableBuff.PreparationRate"));
    using EEffect = EReditusTableBuffEffect;

    EEffect EffectFor(const FReditusTableBuffRow& Row)
    {
        return Row.RuntimeEffect;
    }

    FName SlotFor(const FReditusTableBuffRow& Row, FName ID)
    {
        switch (EffectFor(Row))
        {
        case EEffect::PhysicalDefense: return Row.PhysicalDefenseMultiplier >= 1.f ? GuardUpTag : DefenseDownSlot;
        case EEffect::GaugeRate: return GaugeSlot;
        case EEffect::AggroFocus: return AggroFocusSlot;
        case EEffect::BlockAmpGain: return AmpBlockSlot;
        case EEffect::PreparationRate: return PreparationSlot;
        case EEffect::PeriodicDamage:
        case EEffect::PeriodicHealing:
        case EEffect::PeriodicMP:
            return FName(*FString::Printf(TEXT("Reditus.TableBuff.Periodic.%s"), *ID.ToString()));
        default: return NAME_None;
        }
    }

    bool ValidEffect(const FReditusTableBuffRow& Row)
    {
        if (Row.RuntimeEffect == EEffect::LegacyBlueprint)
            return !Row.LegacyEffectClass.IsNull() && (Row.LegacyDuration != EReditusLegacyBuffDuration::OwnerCycles ||
                (Row.OwnerCycles >= 1 && Row.OwnerCycles <= 99));
        if (Row.OwnerCycles < 1 || Row.OwnerCycles > 99 || !FMath::IsFinite(Row.ApplicationChancePercent) ||
            Row.ApplicationChancePercent < 0.f || Row.ApplicationChancePercent > 100.f) return false;
        switch (EffectFor(Row))
        {
        case EEffect::PhysicalDefense: return ReditusTableBuffRules::ValidEffectScale(Row.PhysicalDefenseMultiplier);
        case EEffect::AggroFocus: return !Row.AggroFocusBuffClass.IsNull();
        case EEffect::BlockAmpGain: return true;
        case EEffect::GaugeRate:
        case EEffect::PreparationRate: return ReditusTableBuffRules::ValidEffectScale(Row.RateMultiplier);
        case EEffect::PeriodicDamage: return ReditusTableBuffRules::PeriodicDamageFraction(Row.MaxHPDamagePercent) > 0.f;
        case EEffect::PeriodicHealing:
        case EEffect::PeriodicMP: return ReditusTableBuffRules::PeriodicDamageFraction(Row.RecoveryPercent) > 0.f;
        default: return false;
        }
    }

    UObject* ReadObject(const UObject* Owner, const TCHAR* Name)
    {
        if (!IsValid(Owner)) return nullptr;
        const auto* Prop = FindFProperty<FObjectPropertyBase>(Owner->GetClass(), FName(Name));
        return Prop ? Prop->GetObjectPropertyValue_InContainer(Owner) : nullptr;
    }

    bool ReadBool(const UObject* Owner, const TCHAR* Name)
    {
        if (!IsValid(Owner)) return false;
        const auto* Prop = FindFProperty<FBoolProperty>(Owner->GetClass(), FName(Name));
        return Prop && Prop->GetPropertyValue_InContainer(Owner);
    }

    int32 ReadInt(const UObject* Owner, const TCHAR* Name)
    {
        if (!IsValid(Owner)) return 0;
        const auto* Prop = FindFProperty<FIntProperty>(Owner->GetClass(), FName(Name));
        return Prop ? Prop->GetPropertyValue_InContainer(Owner) : 0;
    }

    bool AliveInBattle(const AActor* Unit, const AActor* Battle)
    {
        return IsValid(Unit) && IsValid(Battle) && !ReadBool(Battle, TEXT("isBattleOver")) &&
            ReadObject(Unit, TEXT("battleController")) == Battle && ReadInt(Unit, TEXT("currentHP")) > 0;
    }

    FMulticastScriptDelegate* CycleEndDelegate(UActorComponent* Clock)
    {
        if (!IsValid(Clock)) return nullptr;
        const auto* Prop = FindFProperty<FMulticastInlineDelegateProperty>(Clock->GetClass(), CycleEndName);
        const UFunction* Signature = Prop ? Prop->SignatureFunction : nullptr;
        if (!Signature || Signature->NumParms != 2) return nullptr;
        TFieldIterator<FProperty> Param(Signature);
        if (!Param) return nullptr;
        const auto* ActorParam = CastField<FObjectProperty>(*Param);
        if (!ActorParam || ActorParam->PropertyClass != AActor::StaticClass() ||
            !ActorParam->HasAnyPropertyFlags(CPF_Parm) || ActorParam->HasAnyPropertyFlags(CPF_OutParm)) return nullptr;
        ++Param;
        if (!Param) return nullptr;
        const auto* Token = CastField<FIntProperty>(*Param);
        if (!Token || !Token->HasAnyPropertyFlags(CPF_Parm) || Token->HasAnyPropertyFlags(CPF_OutParm)) return nullptr;
        return Prop->ContainerPtrToValuePtr<FMulticastScriptDelegate>(Clock);
    }

    bool ClockRunning(UActorComponent* Clock)
    {
        if (!IsValid(Clock)) return false;
        UFunction* Function = Clock->FindFunction(TEXT("IsATBRunning"));
        const auto* Return = Function ? CastField<FBoolProperty>(Function->GetReturnProperty()) : nullptr;
        if (!Return || Function->NumParms != 1 || Function->ParmsSize != sizeof(bool) ||
            Return->GetOffset_ForInternal() != 0 || !Return->IsNativeBool()) return false;
        bool bResult = false;
        Clock->ProcessEvent(Function, &bResult);
        return bResult;
    }

    UActorComponent* FindClock(AActor* Battle)
    {
        if (!IsValid(Battle)) return nullptr;
        TInlineComponentArray<UActorComponent*> Components;
        Battle->GetComponents(Components);
        for (UActorComponent* Candidate : Components)
            if (CycleEndDelegate(Candidate) && ClockRunning(Candidate)) return Candidate;
        return nullptr;
    }

    template<typename T> const T* InputProperty(const UStruct* Owner, const TCHAR* Name)
    {
        auto* Prop = FindFProperty<T>(Owner, FName(Name));
        return Prop && Prop->HasAnyPropertyFlags(CPF_Parm) && !Prop->HasAnyPropertyFlags(CPF_ReturnParm)
            ? Prop : nullptr;
    }

    bool UnitParameter(const FObjectPropertyBase* Prop, const AActor* Unit)
    {
        return IsValid(Unit) && Prop && Prop->PropertyClass == AActor::StaticClass();
    }

    // The plugin does not link against the game module. Check the complete
    // reflected signature before calling its ATB APIs.
    bool ReadOpenCycle(UActorComponent* Clock, AActor* Unit, int32& OpenToken)
    {
        UFunction* Function = Clock->FindFunction(TEXT("GetUnitState"));
        if (!Function || Function->NumParms != 2) return false;
        const auto* UnitParam = InputProperty<FObjectPropertyBase>(Function, TEXT("Unit"));
        const auto* Return = CastField<FStructProperty>(Function->GetReturnProperty());
        if (!UnitParameter(UnitParam, Unit) || !Return || !Return->Struct ||
            Return->Struct->GetPathName() != TEXT("/Script/Chapter1Reditus.ReditusATBUnitView")) return false;
        const auto* Phase = FindFProperty<FEnumProperty>(Return->Struct, TEXT("Phase"));
        const auto* Token = FindFProperty<FIntProperty>(Return->Struct, TEXT("CycleToken"));
        if (!Phase || !Phase->GetEnum() || !Token) return false;
        FStructOnScope Params(Function);
        void* Memory = Params.GetStructMemory();
        UnitParam->SetObjectPropertyValue_InContainer(Memory, Unit);
        Clock->ProcessEvent(Function, Memory);
        const void* State = Return->ContainerPtrToValuePtr<void>(Memory);
        const auto Value = Phase->GetUnderlyingProperty()->GetSignedIntPropertyValue(Phase->ContainerPtrToValuePtr<void>(State));
        const FString PhaseName = Phase->GetEnum()->GetNameStringByValue(Value);
        if (PhaseName.IsEmpty() || PhaseName == TEXT("Inactive")) return false;
        OpenToken = PhaseName == TEXT("Filling") ? 0 : FMath::Max(0, Token->GetPropertyValue_InContainer(State));
        return true;
    }

    bool ChangeTiming(UActorComponent* Clock, AActor* Unit, FName Slot, float Rate, bool bRemove)
    {
        if (!IsValid(Clock) || !IsValid(Unit)) return false;
        UFunction* Function = Clock->FindFunction(bRemove ? TEXT("RemoveTimingEffect") : TEXT("AddOrUpdateTimingEffect"));
        if (!Function || Function->NumParms != 3) return false;
        const auto* UnitParam = InputProperty<FObjectPropertyBase>(Function, TEXT("Unit"));
        const auto* Return = CastField<FBoolProperty>(Function->GetReturnProperty());
        if (!UnitParameter(UnitParam, Unit) || !Return) return false;
        FStructOnScope Params(Function);
        void* Memory = Params.GetStructMemory();
        UnitParam->SetObjectPropertyValue_InContainer(Memory, Unit);
        if (bRemove)
        {
            const auto* Source = InputProperty<FNameProperty>(Function, TEXT("SourceID"));
            if (!Source) return false;
            Source->SetPropertyValue_InContainer(Memory, Slot);
        }
        else
        {
            const auto* Effect = InputProperty<FStructProperty>(Function, TEXT("Effect"));
            if (!Effect || !Effect->Struct ||
                Effect->Struct->GetPathName() != TEXT("/Script/Chapter1Reditus.ReditusATBTimingEffect")) return false;
            const auto* Source = FindFProperty<FNameProperty>(Effect->Struct, TEXT("SourceID"));
            const auto* Gauge = FindFProperty<FFloatProperty>(Effect->Struct, TEXT("GaugeMultiplier"));
            const auto* Prep = FindFProperty<FFloatProperty>(Effect->Struct, TEXT("PreparationMultiplier"));
            if (!Source || !Gauge || !Prep) return false;
            void* Data = Effect->ContainerPtrToValuePtr<void>(Memory);
            Source->SetPropertyValue_InContainer(Data, Slot);
            Gauge->SetPropertyValue_InContainer(Data, Slot == GaugeSlot ? Rate : 1.f);
            Prep->SetPropertyValue_InContainer(Data, Slot == PreparationSlot ? Rate : 1.f);
        }
        Clock->ProcessEvent(Function, Memory);
        return Return->GetPropertyValue_InContainer(Memory);
    }

    const FFloatProperty* FindDamageOverTime(AActor* Unit, UFunction*& Function)
    {
        Function = IsValid(Unit) ? Unit->FindFunction(TEXT("TakePoisonDamage")) : nullptr;
        return Function && Function->NumParms == 1 ? InputProperty<FFloatProperty>(Function, TEXT("Damage")) : nullptr;
    }

    void DealPeriodicDamage(AActor* Unit, float Percent)
    {
        UFunction* Function = nullptr;
        const auto* Damage = FindDamageOverTime(Unit, Function);
        // BP_UnitBase multiplies this argument by MaxHP. Pass 0.04 for 4%.
        const float Fraction = ReditusTableBuffRules::PeriodicDamageFraction(Percent);
        if (!Damage || Fraction <= 0.f) return;
        FStructOnScope Params(Function);
        Damage->SetPropertyValue_InContainer(Params.GetStructMemory(), Fraction);
        Unit->ProcessEvent(Function, Params.GetStructMemory());
    }

    bool RestorePeriodicResource(AActor* Unit, EEffect Effect, float Percent, bool bValidateOnly)
    {
        const bool bHP = Effect == EEffect::PeriodicHealing;
        UFunction* Function = IsValid(Unit) ? Unit->FindFunction(bHP ? TEXT("Heal") : TEXT("SetMP")) : nullptr;
        if (!Function || Function->NumParms != (bHP ? 2 : 1)) return false;
        const auto* Amount = InputProperty<FIntProperty>(Function, bHP ? TEXT("healAmount") : TEXT("MP"));
        const auto* Multiplier = bHP ? InputProperty<FIntProperty>(Function, TEXT("HealMultiplier")) : nullptr;
        const int32 Maximum = ReadInt(Unit, bHP ? TEXT("maxHP") : TEXT("maxMP"));
        if (!Amount || (bHP && !Multiplier) || Maximum <= 0) return false;
        if (bValidateOnly) return true;

        const double Scaled = static_cast<double>(Maximum) * Percent / 100.0;
        const int32 Gain = Scaled >= Maximum ? Maximum : FMath::Max(1, static_cast<int32>(Scaled));
        const int32 MP = bHP ? 0 : FMath::Clamp(ReadInt(Unit, TEXT("currentMP")), 0, Maximum);
        if (!bHP && MP == Maximum) return true;
        FStructOnScope Params(Function);
        void* Memory = Params.GetStructMemory();
        Amount->SetPropertyValue_InContainer(Memory, bHP ? Gain : MP + FMath::Min(Gain, Maximum - MP));
        if (Multiplier) Multiplier->SetPropertyValue_InContainer(Memory, 1);
        Unit->ProcessEvent(Function, Memory);
        return true;
    }

    bool CanPlayBuffFeedback(const AActor* Target)
    {
        const UWorld* World = IsValid(Target) ? Target->GetWorld() : nullptr;
        return World && World->IsGameWorld() && !World->bIsTearingDown && World->GetNetMode() != NM_DedicatedServer;
    }

    USceneComponent* SpawnBuffEffect(AActor* Target, const FReditusBuffNiagaraEffect& Effect,
        UParticleSystem* Cascade, bool bOneShot)
    {
        const bool bNiagara = IsValid(Effect.Niagara);
        if ((!bNiagara && !IsValid(Cascade)) || !CanPlayBuffFeedback(Target)) return nullptr;
        auto* Anchor = Cast<USceneComponent>(ReadObject(Target, TEXT("Pixel2D")));
        if (!IsValid(Anchor)) Anchor = Target->GetRootComponent();
        if (!IsValid(Anchor)) return nullptr;
        if (!bNiagara)
            return UGameplayStatics::SpawnEmitterAttached(Cascade, Anchor, Effect.Socket, Effect.Offset,
                Effect.Rotation, Effect.Scale, EAttachLocation::KeepRelativeOffset, bOneShot);

        auto* Result = UNiagaraFunctionLibrary::SpawnSystemAttached(Effect.Niagara, Anchor, Effect.Socket,
            Effect.Offset, Effect.Rotation, EAttachLocation::KeepRelativeOffset,
            bOneShot, false, ENCPoolMethod::None, false);
        if (IsValid(Result))
        {
            Result->SetRelativeScale3D(Effect.Scale);
            Result->Activate(true);
        }
        return IsValid(Result) ? Result : nullptr;
    }

    void PlayBuffApplyFeedback(AActor* Target, const FReditusTableBuffRow& Row)
    {
        SpawnBuffEffect(Target, Row.Visuals.OnApply, Row.CascadeVisuals.OnApply, true);
        if (IsValid(Row.OnApplySound) && CanPlayBuffFeedback(Target))
            UGameplayStatics::PlaySound2D(Target, Row.OnApplySound);
    }

    const FMapProperty* FindLegacyBuffMap(AActor* Unit, const FClassProperty*& Key,
        const FObjectPropertyBase*& Value)
    {
        const auto* Property = IsValid(Unit) ? FindFProperty<FMapProperty>(Unit->GetClass(), TEXT("activeBuffs")) : nullptr;
        Key = Property ? CastField<FClassProperty>(Property->KeyProp) : nullptr;
        Value = Property ? CastField<FObjectPropertyBase>(Property->ValueProp) : nullptr;
        return Key && Value ? Property : nullptr;
    }

    int32 FindBuffIndex(FScriptMapHelper& Map, const FClassProperty* Key, UClass* EffectClass)
    {
        for (int32 I = 0; I < Map.GetMaxIndex(); ++I)
            if (Map.IsValidIndex(I) && Key->GetObjectPropertyValue(Map.GetKeyPtr(I)) == EffectClass) return I;
        return INDEX_NONE;
    }

    AActor* FindLegacyBuff(AActor* Unit, UClass* EffectClass)
    {
        const FClassProperty* Key;
        const FObjectPropertyBase* Value;
        const auto* Property = FindLegacyBuffMap(Unit, Key, Value);
        if (!Property) return nullptr;
        FScriptMapHelper Map(Property, Property->ContainerPtrToValuePtr<void>(Unit));
        const int32 Index = FindBuffIndex(Map, Key, EffectClass);
        return Index == INDEX_NONE ? nullptr : Cast<AActor>(Value->GetObjectPropertyValue(Map.GetValuePtr(Index)));
    }

    void RemoveLegacyEntry(AActor* Unit, UClass* EffectClass, AActor* Expected)
    {
        const FClassProperty* Key;
        const FObjectPropertyBase* Value;
        const auto* Property = FindLegacyBuffMap(Unit, Key, Value);
        if (!Property) return;
        FScriptMapHelper Map(Property, Property->ContainerPtrToValuePtr<void>(Unit));
        const int32 Index = FindBuffIndex(Map, Key, EffectClass);
        if (Index != INDEX_NONE && Value->GetObjectPropertyValue(Map.GetValuePtr(Index)) == Expected)
            Map.RemoveAt(Index);
    }

    void UnbindLegacyTimer(AActor* Battle, AActor* Effect, const TCHAR* EventName, const TCHAR* HandlerName)
    {
        if (!IsValid(Battle) || !IsValid(Effect)) return;
        const auto* Property = FindFProperty<FMulticastInlineDelegateProperty>(Battle->GetClass(), FName(EventName));
        if (!Property) return;
        FScriptDelegate Callback;
        Callback.BindUFunction(Effect, FName(HandlerName));
        Property->ContainerPtrToValuePtr<FMulticastScriptDelegate>(Battle)->Remove(Callback);
    }

    void UnbindLegacyTimers(AActor* Battle, AActor* Effect)
    {
        UnbindLegacyTimer(Battle, Effect, TEXT("OnTurnChanged"), TEXT("OnTurnChangedHandler"));
        UnbindLegacyTimer(Battle, Effect, TEXT("OnActionTimeAdded"), TEXT("OnActionTimeAddedHandler"));
    }

    bool IsImmuneToLegacyEffect(AActor* Unit, UClass* Effect, UClass* Family)
    {
        const auto* Array = FindFProperty<FArrayProperty>(Unit->GetClass(), TEXT("Immunities"));
        const auto* Class = Array ? CastField<FClassProperty>(Array->Inner) : nullptr;
        if (!Class) return false;
        FScriptArrayHelper Items(Array, Array->ContainerPtrToValuePtr<void>(Unit));
        for (int32 I = 0; I < Items.Num(); ++I)
            if (const auto* Immune = Cast<UClass>(Class->GetObjectPropertyValue(Items.GetRawPtr(I))))
                if (Effect->IsChildOf(Immune) || (Family && Family->IsChildOf(Immune))) return true;
        return false;
    }

    bool MatchesCatalogueSide(const AActor* Unit, FName Side)
    {
        if (Side.IsNone() || Side == TEXT("Both")) return true;
        const FName Expected = Side == TEXT("Enemy") ? FName(TEXT("BP_EnemyUnitBase_C"))
            : Side == TEXT("Player") ? FName(TEXT("BP_PlayerUnitBase_C")) : NAME_None;
        for (auto* Class = Unit->GetClass(); Class && !Expected.IsNone(); Class = Class->GetSuperClass())
            if (Class->GetFName() == Expected) return true;
        return false;
    }

    using EEquipmentResistance = ReditusTableBuffRules::EEquipmentResistance;

    const TCHAR* EquipmentAilmentEnum = TEXT("/Game/TurnBasedJRPGTemplate/Blueprints/Enums/E_StatusAilments.E_StatusAilments");
    const TCHAR* EquipmentResistanceEnum = TEXT("/Game/TurnBasedJRPGTemplate/Blueprints/Enums/E_StatusResistanceLevel.E_StatusResistanceLevel");

    // Match stable members of the actual Blueprint enum, not localized display text
    // or the current order of its values.
    const TCHAR* AilmentEnumerator(FName Status)
    {
        if (Status == TEXT("Poison")) return TEXT("NewEnumerator0");
        if (Status == TEXT("Blind")) return TEXT("NewEnumerator1");
        if (Status == TEXT("Berserk")) return TEXT("NewEnumerator2");
        if (Status == TEXT("Petrify")) return TEXT("NewEnumerator3");
        if (Status == TEXT("Zombie")) return TEXT("NewEnumerator4");
        if (Status == TEXT("Imp")) return TEXT("NewEnumerator5");
        if (Status == TEXT("Silence")) return TEXT("NewEnumerator6");
        return nullptr;
    }

    FName EquipmentStatusForLegacyClass(const UClass* Class)
    {
        static const TCHAR* BuffFolder = TEXT("/Game/TurnBasedJRPGTemplate/Blueprints/Skills/Buffs/");
        for (const UClass* Current = Class; Current; Current = Current->GetSuperClass())
        {
            if (!Current->GetPathName().StartsWith(BuffFolder)) continue;
            const FName Name = Current->GetFName();
            if (Name == TEXT("BP_Poison_C") || Name == TEXT("BP_DeathBoarPoison_C")) return FName(TEXT("Poison"));
            if (Name == TEXT("BP_Blind_C") || Name == TEXT("BP_BlackSprayBlind_C") ||
                Name == TEXT("BP_WindVortexBlind_C") || Name == TEXT("BP_AugustusOverdriveBlind_C")) return FName(TEXT("Blind"));
            if (Name == TEXT("BP_BerserkBuff_C") || Name == TEXT("BP_AmpBerserkBuff_C")) return FName(TEXT("Berserk"));
        }
        return NAME_None;
    }

    FName EquipmentStatusForTableRow(FName ID, const FReditusTableBuffRow& Row,
        const UClass* LegacyClass, const UClass* ImmunityFamily)
    {
        const FName Authored = Row.Catalogue.ResistanceStatusKey;
        if (AilmentEnumerator(Authored)) return Authored;
        const FName ClassStatus = EquipmentStatusForLegacyClass(LegacyClass);
        if (!ClassStatus.IsNone()) return ClassStatus;
        const FName FamilyStatus = EquipmentStatusForLegacyClass(ImmunityFamily);
        if (!FamilyStatus.IsNone()) return FamilyStatus;
        if (ID == TEXT("Venom")) return FName(TEXT("Poison"));
        if (ID == TEXT("BlindUntilCured")) return FName(TEXT("Blind"));
        return AilmentEnumerator(ID) ? ID : NAME_None;
    }

    struct FReflectedStatusEnum
    {
        const UEnum* Enum = nullptr;
        const FNumericProperty* Storage = nullptr;

        explicit FReflectedStatusEnum(const FProperty* Property)
        {
            if (const auto* Native = CastField<FEnumProperty>(Property))
            {
                Enum = Native->GetEnum();
                Storage = Native->GetUnderlyingProperty();
            }
            else if (const auto* Byte = CastField<FByteProperty>(Property))
            {
                Enum = Byte->Enum;
                Storage = Byte;
            }
        }

        bool IsType(const TCHAR* Path) const { return Enum && Storage && Enum->GetPathName() == Path; }
        int64 Read(const void* Value) const { return static_cast<int64>(Storage->GetUnsignedIntPropertyValue(Value)); }
    };

    EEquipmentResistance ReadEquipmentResistance(AActor* Target, const AActor* Battle, FName Status)
    {
        const TCHAR* StatusEnumerator = AilmentEnumerator(Status);
        if (!StatusEnumerator || !IsValid(Target) || !MatchesCatalogueSide(Target, TEXT("Player")))
            return EEquipmentResistance::Normal;

        const auto* CharacterField = FindFProperty<FNameProperty>(Target->GetClass(), TEXT("CharacterID"));
        const FName Character = CharacterField ? CharacterField->GetPropertyValue_InContainer(Target) : NAME_None;
        UObject* Player = ReadObject(Battle, TEXT("jRPGPlayerController"));
        const auto* Roster = IsValid(Player) && !Character.IsNone()
            ? FindFProperty<FMapProperty>(Player->GetClass(), TEXT("PlayerUnitDataID")) : nullptr;
        const auto* RosterKey = Roster ? CastField<FNameProperty>(Roster->KeyProp) : nullptr;
        const auto* UnitData = Roster ? CastField<FStructProperty>(Roster->ValueProp) : nullptr;
        if (!RosterKey || !UnitData || !UnitData->Struct) return EEquipmentResistance::Normal;

        const FMapProperty* Effective = nullptr;
        for (TFieldIterator<FProperty> Field(UnitData->Struct); Field; ++Field)
        {
            const FString Name = Field->GetName();
            if (Name != TEXT("EffectiveStatusResistance") && !Name.StartsWith(TEXT("EffectiveStatusResistance_"))) continue;
            if (Effective) return EEquipmentResistance::Normal;
            Effective = CastField<FMapProperty>(*Field);
            if (!Effective) return EEquipmentResistance::Normal;
        }
        if (!Effective) return EEquipmentResistance::Normal;

        const FReflectedStatusEnum Ailment(Effective->KeyProp);
        const FReflectedStatusEnum Resistance(Effective->ValueProp);
        if (!Ailment.IsType(EquipmentAilmentEnum) || !Resistance.IsType(EquipmentResistanceEnum))
            return EEquipmentResistance::Normal;
        const int64 Desired = Ailment.Enum->GetValueByNameString(StatusEnumerator);
        if (Desired == INDEX_NONE) return EEquipmentResistance::Normal;
        const int64 Resist = Resistance.Enum->GetValueByNameString(TEXT("NewEnumerator1"));
        const int64 Immune = Resistance.Enum->GetValueByNameString(TEXT("NewEnumerator2"));

        FScriptMapHelper RosterRows(Roster, Roster->ContainerPtrToValuePtr<void>(Player));
        for (int32 I = 0; I < RosterRows.GetMaxIndex(); ++I)
        {
            if (!RosterRows.IsValidIndex(I) || RosterKey->GetPropertyValue(RosterRows.GetKeyPtr(I)) != Character) continue;
            FScriptMapHelper Statuses(Effective, Effective->ContainerPtrToValuePtr<void>(RosterRows.GetValuePtr(I)));
            for (int32 J = 0; J < Statuses.GetMaxIndex(); ++J)
            {
                if (!Statuses.IsValidIndex(J) || Ailment.Read(Statuses.GetKeyPtr(J)) != Desired) continue;
                const int64 Level = Resistance.Read(Statuses.GetValuePtr(J));
                if (Immune != INDEX_NONE && Level == Immune) return EEquipmentResistance::Immune;
                if (Resist != INDEX_NONE && Level == Resist) return EEquipmentResistance::Resist;
                break;
            }
            break;
        }
        return EEquipmentResistance::Normal;
    }

    bool CallNoArgumentEvent(AActor* Actor, const TCHAR* Name)
    {
        UFunction* Function = IsValid(Actor) ? Actor->FindFunction(Name) : nullptr;
        if (!Function || Function->NumParms != 0 || Function->ParmsSize != 0) return false;
        Actor->ProcessEvent(Function, nullptr);
        return true;
    }

    bool ApplyLegacyEffect(AActor* Caster, AActor* Unit, AActor* Battle, UActorComponent* Clock,
        const FReditusTableBuffRow& Row, UClass* EffectClass, UClass* ImmunityFamily,
        EEquipmentResistance EquipmentResistance)
    {
        using EDuration = EReditusLegacyBuffDuration;
        if (!EffectClass || !MatchesCatalogueSide(Caster, Row.Catalogue.AllowedSources) ||
            !MatchesCatalogueSide(Unit, Row.Catalogue.AllowedTargets) ||
            IsImmuneToLegacyEffect(Unit, EffectClass, ImmunityFamily)) return false;
        const bool bTimed = Row.LegacyDuration == EDuration::OwnerCycles;
        const bool bPersistent = Row.LegacyDuration == EDuration::UntilCured;
        int32 OpenToken = 0;
        if (bTimed && (!IsValid(Clock) || !ReadOpenCycle(Clock, Unit, OpenToken))) return false;

        const FClassProperty* Key;
        const FObjectPropertyBase* Value;
        const auto* Property = FindLegacyBuffMap(Unit, Key, Value);
        if (!Property || !Key->MetaClass || !EffectClass->IsChildOf(Key->MetaClass) ||
            !EffectClass->IsChildOf(Value->PropertyClass) || EffectClass->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated))
            return false;

        const auto* ControllerProp = FindFProperty<FObjectPropertyBase>(EffectClass, TEXT("battleController"));
        const auto* UnitProp = FindFProperty<FObjectPropertyBase>(EffectClass, TEXT("targetUnit"));
        const auto* Permanent = FindFProperty<FBoolProperty>(EffectClass, TEXT("Permenant State"));
        const auto* TurnLeft = FindFProperty<FIntProperty>(EffectClass, TEXT("turnLeft"));
        const auto* Chance = FindFProperty<FFloatProperty>(EffectClass, TEXT("successChance"));
        const auto* Apply = EffectClass->FindFunctionByName(TEXT("ApplyBuff"));
        const auto* End = EffectClass->FindFunctionByName(TEXT("OnBuffEnded"));
        const auto* Default = EffectClass->GetDefaultObject();
        if (!ControllerProp || !Battle->IsA(ControllerProp->PropertyClass) || !UnitProp ||
            !Unit->IsA(UnitProp->PropertyClass) || !Permanent || !Default || !Chance ||
            !Apply || Apply->NumParms != 0 || (bTimed && (!TurnLeft || !End || End->NumParms != 0))) return false;

        const float BaseChance = Row.LegacyChancePercent >= 0.f ? Row.LegacyChancePercent / 100.f
            : Chance->GetPropertyValue_InContainer(Default);
        const float Probability = ReditusTableBuffRules::BuffApplicationChance(BaseChance, EquipmentResistance);
        if (Probability <= 0.f ||
            (Probability < 1.f && !ReditusTableBuffRules::PassesBuffApplicationRoll(Probability, FMath::FRand()))) return false;
        auto* Effect = FindLegacyBuff(Unit, EffectClass);
        const bool bExisting = IsValid(Effect);
        if (bExisting && bTimed && Permanent->GetPropertyValue_InContainer(Effect)) return false;
        // The save loader can restore Blind to a new actor without recreating
        // a buff actor. Do not let a temporary Blind clear that lasting flag.
        if (!bExisting && bTimed && EffectClass->GetFName() == TEXT("BP_Blind_C") && ReadBool(Unit, TEXT("Is Blind?")))
            return false;
        if (!bExisting)
        {
            UWorld* World = Battle->GetWorld();
            if (!World || World->bIsTearingDown) return false;
            const FTransform Transform(FVector(999999.f, 999999.f, 999999.f));
            Effect = World->SpawnActorDeferred<AActor>(EffectClass, Transform, nullptr, nullptr,
                ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
            if (!IsValid(Effect)) return false;
            ControllerProp->SetObjectPropertyValue_InContainer(Effect, Battle);
            UnitProp->SetObjectPropertyValue_InContainer(Effect, Unit);
            Permanent->SetPropertyValue_InContainer(Effect, bPersistent ||
                (!bTimed && Permanent->GetPropertyValue_InContainer(Default)));
            Effect->FinishSpawning(Transform);
            if (!IsValid(Effect) || !AliveInBattle(Unit, Battle))
            {
                if (IsValid(Effect)) Effect->Destroy();
                return false;
            }

            FScriptMapHelper Map(Property, Property->ContainerPtrToValuePtr<void>(Unit));
            const int32 Old = FindBuffIndex(Map, Key, EffectClass);
            if (Old != INDEX_NONE) Map.RemoveAt(Old);
            const int32 Index = Map.AddDefaultValue_Invalid_NeedsRehash();
            Key->SetObjectPropertyValue(Map.GetKeyPtr(Index), EffectClass);
            Value->SetObjectPropertyValue(Map.GetValuePtr(Index), Effect);
            Map.Rehash();
        }

        bool bApplyEvent = !bExisting;
        if (bPersistent)
        {
            bApplyEvent |= !Permanent->GetPropertyValue_InContainer(Effect);
            Permanent->SetPropertyValue_InContainer(Effect, true);
            if (auto* Timer = Effect->FindComponentByClass<UReditusLegacyTableBuffTimer>()) Timer->Disarm();
            UnbindLegacyTimers(Battle, Effect);
        }
        else if (bTimed)
        {
            auto* Timer = Effect->FindComponentByClass<UReditusLegacyTableBuffTimer>();
            if (!Timer)
            {
                Timer = NewObject<UReditusLegacyTableBuffTimer>(Effect, NAME_None, RF_Transient);
                Effect->AddInstanceComponent(Timer);
                Timer->RegisterComponent();
            }
            if (!Timer->Arm(Unit, Clock, Row.OwnerCycles, OpenToken))
            {
                if (!bExisting)
                {
                    RemoveLegacyEntry(Unit, EffectClass, Effect);
                    Effect->Destroy();
                }
                return false;
            }
            UnbindLegacyTimers(Battle, Effect);
        }
        else if (bExisting && !Permanent->GetPropertyValue_InContainer(Effect))
        {
            UFunction* Refresh = Effect->FindFunction(TEXT("AddDuration"));
            const auto* Amp = Refresh ? InputProperty<FIntProperty>(Refresh, TEXT("AmpPointsInPlay")) : nullptr;
            if (Refresh && Refresh->NumParms == 1 && Amp)
            {
                FStructOnScope Params(Refresh);
                Amp->SetPropertyValue_InContainer(Params.GetStructMemory(), ReadInt(Caster, TEXT("CurrentAmpPointsInPlay")));
                Effect->ProcessEvent(Refresh, Params.GetStructMemory());
            }
        }
        if (bApplyEvent)
        {
            CallNoArgumentEvent(Effect, TEXT("ApplyBuff"));
            if (bTimed || bPersistent) UnbindLegacyTimers(Battle, Effect);
        }
        PlayBuffApplyFeedback(Unit, Row);
        return true;
    }
}

bool UReditusLegacyTableBuffTimer::Arm(AActor* Unit, UActorComponent* InClock, int32 Cycles, int32 OpenToken)
{
    auto* Delegate = CycleEndDelegate(InClock);
    auto* Effect = GetOwner();
    if (!Delegate || !IsValid(Unit) || !IsValid(Effect)) return false;
    if (Clock.Get() != InClock || Recipient.Get() != Unit) Disarm();
    Clock = InClock;
    Recipient = Unit;
    Lifetime.StartOrRefreshForTarget(Cycles, OpenToken);
    FScriptDelegate Callback;
    Callback.BindUFunction(this, GET_FUNCTION_NAME_CHECKED(UReditusLegacyTableBuffTimer, OnRecipientCycleEnd));
    Delegate->AddUnique(Callback);
    if (const auto* Turns = FindFProperty<FIntProperty>(Effect->GetClass(), TEXT("turnLeft")))
        Turns->SetPropertyValue_InContainer(Effect, Cycles);
    return true;
}

void UReditusLegacyTableBuffTimer::Disarm()
{
    if (auto* Delegate = CycleEndDelegate(Clock.Get()))
    {
        FScriptDelegate Callback;
        Callback.BindUFunction(this, GET_FUNCTION_NAME_CHECKED(UReditusLegacyTableBuffTimer, OnRecipientCycleEnd));
        Delegate->Remove(Callback);
    }
    Clock.Reset();
    Recipient.Reset();
    Lifetime = {};
}

void UReditusLegacyTableBuffTimer::OnRecipientCycleEnd(AActor* Unit, int32 Token)
{
    if (Unit != Recipient.Get()) return;
    auto* Effect = GetOwner();
    if (!IsValid(Effect) || !IsValid(Unit)) { Disarm(); return; }
    UClass* EffectClass = Effect->GetClass();
    if (FindLegacyBuff(Unit, EffectClass) != Effect)
    {
        Disarm();
        Effect->Destroy();
        return;
    }

    using EEnd = ReditusTableBuffRules::EOwnerEnd;
    const EEnd Result = Lifetime.FinishOwnerCycle(Token);
    if (Result == EEnd::Advanced || Result == EEnd::Expired)
        if (const auto* Turns = FindFProperty<FIntProperty>(EffectClass, TEXT("turnLeft")))
            Turns->SetPropertyValue_InContainer(Effect, Lifetime.Remaining);
    if (Result != EEnd::Expired) return;

    Disarm();
    CallNoArgumentEvent(Effect, TEXT("OnBuffEnded"));
    RemoveLegacyEntry(Unit, EffectClass, Effect);
    if (IsValid(Effect)) Effect->Destroy();
}

void UReditusLegacyTableBuffTimer::EndPlay(const EEndPlayReason::Type Reason)
{
    Disarm();
    Super::EndPlay(Reason);
}

UReditusTableBuffState::UReditusTableBuffState()
{
    PrimaryComponentTick.bCanEverTick = true;
    PrimaryComponentTick.bStartWithTickEnabled = false;
    PrimaryComponentTick.TickInterval = 0.10f;
}

bool UReditusTableBuffState::Apply(AActor* InBattle, UActorComponent* Clock, UDataTable* Table, FName ID,
    const FReditusTableBuffRow& Row, FName Slot, int32 SkipEndToken, bool bLegacySelfCast)
{
    auto* Delegate = CycleEndDelegate(Clock);
    if (!Delegate || !AliveInBattle(GetOwner(), InBattle)) return false;
    const bool bTiming = Slot == GaugeSlot || Slot == PreparationSlot;
    if (bTiming && !ChangeTiming(Clock, GetOwner(), Slot, Row.RateMultiplier, false)) return false;
    const auto StartDuration = [&]()
    {
        if (bLegacySelfCast) Lifetime.StartOrRefresh(Row.OwnerCycles);
        else Lifetime.StartOrRefreshForTarget(Row.OwnerCycles, SkipEndToken);
        if (Slot == AmpBlockSlot)
        {
            AmpBlockLifetime.StartOrRefresh(Row.OwnerCycles);
            AmpBlockTurnsRemaining = AmpBlockLifetime.Remaining;
        }
        if (Slot == AggroFocusSlot) AggroFocusOrder = ++NextAggroFocusOrder;
        BuffTurnsRemaining = Lifetime.Remaining;
    };
    if (ActiveTable.Get() == Table && ActiveID == ID && Battle.Get() == InBattle &&
        BattleClock.Get() == Clock && Lifetime.Remaining > 0)
    {
        StartDuration();
        ActiveRow = Row;
        PlayBuffApplyFeedback(GetOwner(), Row);
        auto* ExistingLoop = Loop.Get();
        if (!ExistingLoop || !ExistingLoop->IsActive())
        {
            if (ExistingLoop) ExistingLoop->DestroyComponent();
            Loop = SpawnBuffEffect(GetOwner(), Row.Visuals.WhileActive, Row.CascadeVisuals.WhileActive, false);
        }
        UE_LOG(LogReditusTableBuffs, Display, TEXT("Refreshed %s on %s (%d full cycles)."),
            *ID.ToString(), *GetNameSafe(GetOwner()), Row.OwnerCycles);
        return true;
    }

    Clear(false, bTiming && EffectSlot == Slot && BattleClock.Get() == Clock);
    EffectSlot = Slot;
    Battle = InBattle;
    BattleClock = Clock;
    ActiveTable = Table;
    ActiveID = ID;
    ActiveRow = Row;
    StartDuration();
    FScriptDelegate Callback;
    Callback.BindUFunction(this, GET_FUNCTION_NAME_CHECKED(UReditusTableBuffState, OnOwnerCycleEnd));
    Delegate->AddUnique(Callback);
    if (Slot == GuardUpTag) ComponentTags.AddUnique(GuardUpTag);
    if (Slot == AmpBlockSlot) ComponentTags.AddUnique(AmpBlockSlot);
    SetComponentTickEnabled(true);
    PlayBuffApplyFeedback(GetOwner(), Row);
    Loop = SpawnBuffEffect(GetOwner(), Row.Visuals.WhileActive, Row.CascadeVisuals.WhileActive, false);
    if (EffectFor(Row) == EEffect::PhysicalDefense)
    {
        const int32 Base = ReadInt(GetOwner(), TEXT("defense"));
        UE_LOG(LogReditusTableBuffs, Display, TEXT("Applied %s on %s for %d full cycles. Defense %d -> %d."),
            *ID.ToString(), *GetNameSafe(GetOwner()), Row.OwnerCycles,
            UReditusTableBuffLibrary::GetBuffedDefense(GetOwner(), Base));
    }
    else
    {
        UE_LOG(LogReditusTableBuffs, Display, TEXT("Applied %s on %s for %d full cycles."),
            *ID.ToString(), *GetNameSafe(GetOwner()), Row.OwnerCycles);
    }
    return true;
}

bool UReditusTableBuffState::IsLive() const
{
    return !ActiveID.IsNone() && BattleClock.IsValid() && AliveInBattle(GetOwner(), Battle.Get()) &&
        (EffectSlot != GuardUpTag || !ReadBool(GetOwner(), TEXT("IsProtected")));
}

uint64 UReditusTableBuffState::GetAggroFocusOrder(const AActor* InBattle) const
{
    return EffectSlot == AggroFocusSlot && InBattle == Battle.Get() && Lifetime.Remaining > 0 &&
        IsLive() && ClockRunning(BattleClock.Get()) ? AggroFocusOrder : 0;
}

double UReditusTableBuffState::DefenseDelta() const
{
    return Lifetime.Remaining > 0 && IsLive() && EffectFor(ActiveRow) == EEffect::PhysicalDefense
        ? static_cast<double>(ActiveRow.PhysicalDefenseMultiplier) - 1.0 : 0.0;
}

bool UReditusTableBuffState::TryBlockAmpGain(UActorComponent* Clock, int32 CycleToken)
{
    return Clock == BattleClock.Get() && BlocksAmpGain() && AmpBlockLifetime.BlockAward(CycleToken);
}

bool UReditusTableBuffState::BlocksAmpGain() const
{
    return EffectSlot == AmpBlockSlot && AmpBlockLifetime.Remaining > 0 &&
        IsLive() && ClockRunning(BattleClock.Get());
}

void UReditusTableBuffState::OnOwnerCycleEnd(AActor* Unit, int32 CycleToken)
{
    if (Unit != GetOwner()) return;
    if (!IsLive()) { Clear(false); return; }
    using EEnd = ReditusTableBuffRules::EOwnerEnd;
    const EEnd Result = EffectSlot == AmpBlockSlot
        ? AmpBlockLifetime.FinishOwnerCycle(CycleToken) : Lifetime.FinishOwnerCycle(CycleToken);
    if (EffectSlot == AmpBlockSlot)
    {
        AmpBlockTurnsRemaining = AmpBlockLifetime.Remaining;
        // Keep the ordinary active-state test in sync with the AMP lifetime.
        Lifetime.Remaining = AmpBlockLifetime.Remaining;
    }
    BuffTurnsRemaining = Lifetime.Remaining;
    if (Result == EEnd::Advanced || Result == EEnd::Expired)
    {
        const EEffect Effect = EffectFor(ActiveRow);
        if (Effect == EEffect::PeriodicDamage) DealPeriodicDamage(Unit, ActiveRow.MaxHPDamagePercent);
        else if (Effect == EEffect::PeriodicHealing || Effect == EEffect::PeriodicMP)
            RestorePeriodicResource(Unit, Effect, ActiveRow.RecoveryPercent, false);
    }
    if (IsValid(this) && !ActiveID.IsNone() && Result == EEnd::Expired && Lifetime.Remaining == 0)
        Clear(IsLive());
}

void UReditusTableBuffState::TickComponent(float DeltaTime, ELevelTick TickType,
    FActorComponentTickFunction* TickFunction)
{
    Super::TickComponent(DeltaTime, TickType, TickFunction);
    if (!IsLive() || !ClockRunning(BattleClock.Get())) Clear(false);
}

void UReditusTableBuffState::Clear(bool bNormalExpiry, bool bKeepTiming)
{
    const FName Previous = ActiveID;
    ActiveID = NAME_None;
    AggroFocusOrder = 0;
    Lifetime = {};
    AmpBlockLifetime = {};
    BuffTurnsRemaining = 0;
    AmpBlockTurnsRemaining = 0;
    ComponentTags.Remove(GuardUpTag);
    ComponentTags.Remove(AmpBlockSlot);
    SetComponentTickEnabled(false);
    if (auto* Delegate = CycleEndDelegate(BattleClock.Get()))
    {
        FScriptDelegate Callback;
        Callback.BindUFunction(this, GET_FUNCTION_NAME_CHECKED(UReditusTableBuffState, OnOwnerCycleEnd));
        Delegate->Remove(Callback);
    }
    if (!Previous.IsNone() && !bKeepTiming && (EffectSlot == GaugeSlot || EffectSlot == PreparationSlot))
        ChangeTiming(BattleClock.Get(), GetOwner(), EffectSlot, 1.f, true);
    Battle.Reset();
    BattleClock.Reset();
    ActiveTable.Reset();
    if (auto* Effect = Loop.Get()) Effect->DestroyComponent();
    Loop.Reset();
    if (bNormalExpiry && !Previous.IsNone())
    {
        SpawnBuffEffect(GetOwner(), ActiveRow.Visuals.OnEnd, ActiveRow.CascadeVisuals.OnEnd, true);
        UE_LOG(LogReditusTableBuffs, Display, TEXT("Expired %s on %s."),
            *Previous.ToString(), *GetNameSafe(GetOwner()));
    }
    ActiveRow = {};
}

void UReditusTableBuffState::EndPlay(const EEndPlayReason::Type Reason)
{
    Clear(false);
    Super::EndPlay(Reason);
}

void UReditusTableBuffState::OnComponentDestroyed(bool bDestroyingHierarchy)
{
    Clear(false);
    Super::OnComponentDestroyed(bDestroyingHierarchy);
}

namespace
{
    int32 ReadCommittedBuffAmp(AActor* Caster, UActorComponent* Clock)
    {
        // Prefer the queued action snapshot: command selection can change the
        // unit's editable AMP value while another action is being resolved.
        UFunction* Function = IsValid(Clock) ? Clock->FindFunction(TEXT("GetPendingAction")) : nullptr;
        const auto* Unit = Function ? InputProperty<FObjectPropertyBase>(Function, TEXT("Unit")) : nullptr;
        const auto* Action = Function ? FindFProperty<FStructProperty>(Function, TEXT("Action")) : nullptr;
        const auto* Result = Function ? CastField<FBoolProperty>(Function->GetReturnProperty()) : nullptr;
        const auto* Amp = Action && Action->Struct ? FindFProperty<FIntProperty>(Action->Struct, TEXT("AmpCommitted")) : nullptr;
        const auto* Actor = Action && Action->Struct ? FindFProperty<FObjectPropertyBase>(Action->Struct, TEXT("Unit")) : nullptr;
        if (Function && Function->NumParms == 3 && Unit && Unit->PropertyClass == AActor::StaticClass() &&
            Action && Action->HasAnyPropertyFlags(CPF_OutParm) && Result && Amp && Actor)
        {
            FStructOnScope Params(Function);
            void* Memory = Params.GetStructMemory();
            Unit->SetObjectPropertyValue_InContainer(Memory, Caster);
            Clock->ProcessEvent(Function, Memory);
            const void* Data = Action->ContainerPtrToValuePtr<void>(Memory);
            if (Result->GetPropertyValue_InContainer(Memory) && Actor->GetObjectPropertyValue_InContainer(Data) == Caster)
                return FMath::Max(0, Amp->GetPropertyValue_InContainer(Data));
        }
        return FMath::Max(0, ReadInt(Caster, TEXT("CurrentAmpPointsInPlay")));
    }

    int32 ApplyToRecipients(AActor* Caster, const TArray<AActor*>& Targets, UDataTable* BuffTable, FName BuffID,
        bool bLegacySelfCast)
    {
        AActor* Battle = Cast<AActor>(ReadObject(Caster, TEXT("battleController")));
        if (!AliveInBattle(Caster, Battle)) return 0;

        const auto* Definition = IsValid(BuffTable) && BuffTable->GetRowStruct() == FReditusTableBuffRow::StaticStruct() &&
            !BuffID.IsNone() ? BuffTable->FindRow<FReditusTableBuffRow>(BuffID, TEXT("Apply Buff by ID"), false) : nullptr;
        if (!Definition) return 0;
        FReditusTableBuffRow Row = *Definition;
        // The original Guard Up test shipped before the effect selector.
        if (BuffID == TEXT("Protect") && Row.RuntimeEffect == EEffect::Unwired && !Row.Catalogue.bDesignOnly)
            Row.RuntimeEffect = EEffect::PhysicalDefense;
        if (!ValidEffect(Row))
        {
            UE_LOG(LogReditusTableBuffs, Warning, TEXT("Buff '%s' is missing, unwired, or has invalid effect settings."),
                *BuffID.ToString());
            return 0;
        }
        const FName Slot = SlotFor(Row, BuffID);
        UActorComponent* Clock = FindClock(Battle);
        const bool bLegacy = Row.RuntimeEffect == EEffect::LegacyBlueprint;
        if (!Clock && (!bLegacy || Row.LegacyDuration == EReditusLegacyBuffDuration::OwnerCycles))
        {
            UE_LOG(LogReditusTableBuffs, Warning, TEXT("No compatible ATB cycle-end event on %s."), *GetNameSafe(Battle));
            return 0;
        }
        UClass* LegacyClass = bLegacy ? Row.LegacyEffectClass.LoadSynchronous() : nullptr;
        UClass* ImmunityFamily = bLegacy ? Row.LegacyImmunityClass.LoadSynchronous() : nullptr;
        if (bLegacy && (!LegacyClass || (!Row.LegacyImmunityClass.IsNull() && !ImmunityFamily)))
        {
            UE_LOG(LogReditusTableBuffs, Warning, TEXT("Legacy class or immunity family is missing for '%s'."),
                *BuffID.ToString());
            return 0;
        }

        UClass* StateClass = UReditusTableBuffState::StaticClass();
        if (Slot == AggroFocusSlot)
        {
            StateClass = Row.AggroFocusBuffClass.LoadSynchronous();
            if (!StateClass || !StateClass->IsChildOf(UReditusTableBuffState::StaticClass()) ||
                StateClass->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated))
            {
                UE_LOG(LogReditusTableBuffs, Warning, TEXT("Invalid Aggro Focus buff Blueprint class in row '%s'."), *BuffID.ToString());
                return 0;
            }
            if (!MatchesCatalogueSide(Caster, TEXT("Player"))) return 0;
        }

        // Work on this local row copy. Authored defaults never grow on recast.
        // ClassDefault already adds AMP in BP_BuffBase.AddDuration; do not add
        // it again. UntilCured remains persistent and has no cycle countdown.
        if (!bLegacy || Row.LegacyDuration == EReditusLegacyBuffDuration::OwnerCycles)
            Row.OwnerCycles = ReditusTableBuffRules::CyclesWithAmp(Row.OwnerCycles, ReadCommittedBuffAmp(Caster, Clock));
        const FName EquipmentStatus = EquipmentStatusForTableRow(BuffID, Row, LegacyClass, ImmunityFamily);
        int32 Applied = 0;
        TSet<AActor*> Seen;
        for (AActor* Unit : Targets)
        {
            if (!AliveInBattle(Unit, Battle) || Seen.Contains(Unit)) continue;
            Seen.Add(Unit);
            if (Slot == AggroFocusSlot && (!MatchesCatalogueSide(Unit, TEXT("Player")) || ReadBool(Unit, TEXT("Swallowed"))))
                continue;
            const auto EquipmentResistance = ReadEquipmentResistance(Unit, Battle, EquipmentStatus);
            if (EquipmentResistance == EEquipmentResistance::Immune) continue;
            if (bLegacy)
            {
                if (ApplyLegacyEffect(Caster, Unit, Battle, Clock, Row, LegacyClass, ImmunityFamily, EquipmentResistance)) ++Applied;
                continue;
            }
            if (Slot == GuardUpTag && ReadBool(Unit, TEXT("IsProtected"))) continue;
            if (!MatchesCatalogueSide(Caster, Row.Catalogue.AllowedSources) ||
                !MatchesCatalogueSide(Unit, Row.Catalogue.AllowedTargets)) continue;
            const float Probability = ReditusTableBuffRules::BuffApplicationChance(
                Row.ApplicationChancePercent / 100.f, EquipmentResistance);
            if (Probability <= 0.f || (Probability < 1.f && FMath::FRand() >= Probability)) continue;
            int32 SkipEndToken = 0;
            if ((!bLegacySelfCast || Slot != GuardUpTag) && !ReadOpenCycle(Clock, Unit, SkipEndToken))
            {
                UE_LOG(LogReditusTableBuffs, Warning, TEXT("No compatible ATB state for buff recipient %s."), *GetNameSafe(Unit));
                continue;
            }
            const EEffect Effect = EffectFor(Row);
            if (Effect == EEffect::PeriodicDamage)
            {
                UFunction* DamageFunction = nullptr;
                if (!FindDamageOverTime(Unit, DamageFunction) || ReadInt(Unit, TEXT("maxHP")) <= 0)
                {
                    UE_LOG(LogReditusTableBuffs, Warning, TEXT("Missing damage-over-time hook on %s."), *GetNameSafe(Unit));
                    continue;
                }
            }
            if ((Effect == EEffect::PeriodicHealing || Effect == EEffect::PeriodicMP) &&
                !RestorePeriodicResource(Unit, Effect, Row.RecoveryPercent, true)) continue;

            TInlineComponentArray<UReditusTableBuffState*> States;
            Unit->GetComponents(States);
            UReditusTableBuffState* State = nullptr;
            for (auto* Candidate : States)
            {
                if (IsValid(Candidate) && Candidate->GetEffectSlot() == Slot)
                {
                    State = Candidate;
                    break;
                }
            }
            if (State && Slot == AggroFocusSlot && State->GetClass() != StateClass)
            {
                State->DestroyComponent();
                State = nullptr;
            }
            const bool bCreated = State == nullptr;
            if (bCreated)
            {
                State = NewObject<UReditusTableBuffState>(Unit, StateClass, NAME_None, RF_Transient);
                Unit->AddInstanceComponent(State);
                State->RegisterComponent();
            }
            if (State->Apply(Battle, Clock, BuffTable, BuffID, Row, Slot, SkipEndToken, bLegacySelfCast))
                ++Applied;
            else
            {
                if (bCreated && IsValid(State)) State->DestroyComponent();
                UE_LOG(LogReditusTableBuffs, Warning, TEXT("Couldn't apply '%s' on %s; check the live ATB component."),
                    *BuffID.ToString(), *GetNameSafe(Unit));
            }
        }
        return Applied;
    }
}

bool UReditusTableBuffLibrary::ApplyBuffByID(AActor* Caster, UDataTable* BuffTable, FName BuffID)
{
    return ApplyToRecipients(Caster, {Caster}, BuffTable, BuffID, true) != 0;
}

void UReditusTableBuffLibrary::SynchronizeLegacyBuffDuration(AActor* Buff)
{
    if (!IsValid(Buff)) return;
    // BP_Staggered has an existing frozen-gauge recovery clock. Do not tick
    // that family twice or convert it to a cycle it cannot reach while frozen.
    for (const UClass* Type=Buff->GetClass(); Type; Type=Type->GetSuperClass())
        if (Type->GetPathName()==TEXT("/Game/TurnBasedJRPGTemplate/Blueprints/Skills/Buffs/BP_Staggered.BP_Staggered_C")) return;
    auto* Battle=Cast<AActor>(ReadObject(Buff,TEXT("battleController")));
    if (ReadBool(Buff,TEXT("Permenant State")))
    {
        if (auto* Timer=Buff->FindComponentByClass<UReditusLegacyTableBuffTimer>()) Timer->Disarm();
        UnbindLegacyTimers(Battle,Buff);
        return;
    }
    auto* Unit=Cast<AActor>(ReadObject(Buff,TEXT("targetUnit")));
    UActorComponent* Clock=FindClock(Battle);
    const int32 Cycles=ReadInt(Buff,TEXT("turnLeft"));
    int32 OpenToken=0;
    if (!AliveInBattle(Unit,Battle) || !Clock || Cycles<=0 || !ReadOpenCycle(Clock,Unit,OpenToken)) return;
    auto* Timer=Buff->FindComponentByClass<UReditusLegacyTableBuffTimer>();
    const bool bCreated=!Timer;
    if (!Timer)
    {
        Timer=NewObject<UReditusLegacyTableBuffTimer>(Buff,NAME_None,RF_Transient);
        Buff->AddInstanceComponent(Timer);
        Timer->RegisterComponent();
    }
    // AddDuration already computed base durationTurn + AMP + legacy carryover.
    // Adopt that count; never add the AMP bonus a second time.
    if (Timer->Arm(Unit,Clock,Cycles,OpenToken)) UnbindLegacyTimers(Battle,Buff);
    else if (bCreated) Timer->DestroyComponent();
}

bool UReditusTableBuffLibrary::CanReceiveAmp(AActor* Unit)
{
    if (!IsValid(Unit)) return false;
    TInlineComponentArray<UReditusTableBuffState*> States;
    Unit->GetComponents(States);
    for (const auto* State : States)
        if (IsValid(State) && State->BlocksAmpGain()) return false;
    return true;
}

int32 UReditusTableBuffLibrary::ApplyBuffByIDToTargets(AActor* Caster, const TArray<AActor*>& EligibleTargets,
    UDataTable* BuffTable, FName BuffID)
{
    return ApplyToRecipients(Caster, EligibleTargets, BuffTable, BuffID, false);
}

bool UReditusTableBuffLibrary::PassesLegacyBuffResistanceRoll(
    AActor* Target, TSubclassOf<AActor> BuffClass, float BaseChance, float Roll)
{
    if (!IsValid(Target) || !BuffClass.Get()) return false;
    const AActor* Battle = Cast<AActor>(ReadObject(Target, TEXT("battleController")));
    const FName Status = EquipmentStatusForLegacyClass(BuffClass.Get());
    const auto Resistance = ReadEquipmentResistance(Target, Battle, Status);
    const float Chance = ReditusTableBuffRules::BuffApplicationChance(BaseChance, Resistance);
    return ReditusTableBuffRules::PassesBuffApplicationRoll(Chance, Roll);
}

void UReditusTableBuffLibrary::SetTableSkillID(AActor* Skill, FName SkillID)
{
    if (IsValid(Skill) && !SkillID.IsNone())
        if (const auto* Field = FindFProperty<FNameProperty>(Skill->GetClass(), TEXT("SkillID")))
            Field->SetPropertyValue_InContainer(Skill, SkillID);
}

const FProperty* UReditusTableBuffLibrary::FindSkillBuffRowsField(const UScriptStruct* Struct)
{
    if (!Struct) return nullptr;
    const FProperty* Result = nullptr;
    for (TFieldIterator<FProperty> It(Struct); It; ++It)
    {
        // User-defined structs can sanitize punctuation and append a field ID and GUID.
        FString Name;
        for (TCHAR Character : It->GetName())
            if (FChar::IsAlnum(Character)) Name.AppendChar(Character);
        const auto Matches = [&Name](const TCHAR* Stem)
        {
            const int32 Length = FCString::Strlen(Stem);
            return Name.StartsWith(Stem, ESearchCase::IgnoreCase) &&
                (Name.Len() == Length || FChar::IsDigit(Name[Length]));
        };
        if (!Matches(TEXT("BuffDebuff")) && !Matches(TEXT("BuffRows"))) continue;
        const auto* Array = CastField<FArrayProperty>(*It);
        const auto* Handle = CastField<FStructProperty>(Array ? Array->Inner : *It);
        if (!Handle || Handle->Struct != FDataTableRowHandle::StaticStruct() || Result) return nullptr;
        Result = *It;
    }
    return Result;
}

bool UReditusTableBuffLibrary::ApplySkillBuffRows(AActor* Skill, UDataTable* SkillTable, AActor* EligibleTarget)
{
    if (!IsValid(Skill) || !IsValid(SkillTable) || !SkillTable->GetRowStruct()) return false;
    const auto* IDField = FindFProperty<FNameProperty>(Skill->GetClass(), TEXT("SkillID"));
    if (!IDField) return false;
    const FName ID = IDField->GetPropertyValue_InContainer(Skill);
    if (ID.IsNone()) return false;
    const uint8* Row = SkillTable->GetRowMap().FindRef(ID);
    const auto* Field = FindSkillBuffRowsField(SkillTable->GetRowStruct());
    if (!Row || !Field) return false;

    const auto* Battle = Cast<AActor>(ReadObject(Skill, TEXT("battleController")));
    auto* Caster = Cast<AActor>(ReadObject(Battle, TEXT("currentAttackingUnit")));
    TArray<FDataTableRowHandle> Handles;
    if (const auto* Array = CastField<FArrayProperty>(Field))
    {
        FScriptArrayHelper Rows(Array, Field->ContainerPtrToValuePtr<void>(Row));
        for (int32 Index = 0; Index < Rows.Num(); ++Index)
            Handles.Add(*reinterpret_cast<const FDataTableRowHandle*>(Rows.GetRawPtr(Index)));
    }
    else
    {
        Handles.Add(*Field->ContainerPtrToValuePtr<FDataTableRowHandle>(Row));
    }
    bool bConfigured = false;
    TArray<FDataTableRowHandle> Seen;
    for (const auto& Handle : Handles)
    {
        if (!Handle.DataTable && Handle.RowName.IsNone()) continue;
        bConfigured = true;
        if (Seen.ContainsByPredicate([&](const FDataTableRowHandle& Other)
            { return Other.DataTable == Handle.DataTable && Other.RowName == Handle.RowName; })) continue;
        Seen.Add(Handle);
        if (!IsValid(Handle.DataTable) || Handle.DataTable->GetRowStruct() != FReditusTableBuffRow::StaticStruct() ||
            Handle.RowName.IsNone() || !Handle.DataTable->GetRowMap().Contains(Handle.RowName))
        {
            UE_LOG(LogReditusTableBuffs, Warning, TEXT("Skill '%s' has an invalid Buff/Debuff entry (%s)."),
                *ID.ToString(), *Handle.RowName.ToString());
            continue;
        }
        ApplyToRecipients(Caster, {EligibleTarget}, const_cast<UDataTable*>(Handle.DataTable), Handle.RowName, false);
    }
    return bConfigured;
}

namespace
{
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

    int32 FindAggroFocus(AActor* BattleController, AActor*& OutTarget)
    {
        // Use the same array and preserve its exact indices, including any empty slots.
        OutTarget = nullptr;
        UObject* Battle = ReadObject(BattleController, TEXT("currentBattle"));
        const auto* Property = IsValid(Battle) ? FindFProperty<FArrayProperty>(Battle->GetClass(), TEXT("playerUnits")) : nullptr;
        const auto* Item = Property ? CastField<FObjectPropertyBase>(Property->Inner) : nullptr;
        if (!Item) return INDEX_NONE;
        FScriptArrayHelper Players(Property, Property->ContainerPtrToValuePtr<void>(Battle));
        int32 BestIndex = INDEX_NONE;
        uint64 BestOrder = 0;
        for (int32 Index = 0; Index < Players.Num(); ++Index)
        {
            auto* Unit = Cast<AActor>(Item->GetObjectPropertyValue(Players.GetRawPtr(Index)));
            if (!AliveInBattle(Unit, BattleController) || ReadBool(Unit, TEXT("Swallowed"))) continue;
            TInlineComponentArray<UReditusTableBuffState*> States;
            Unit->GetComponents(States);
            for (const auto* State : States)
            {
                if (!IsValid(State)) continue;
                const uint64 Order = State->GetAggroFocusOrder(BattleController);
                if (Order > BestOrder)
                {
                    BestOrder = Order;
                    BestIndex = Index;
                    OutTarget = Unit;
                }
            }
        }
        return BestIndex;
    }
}

AActor* UReditusTableBuffLibrary::GetAggroFocusTarget(AActor* BattleController)
{
    AActor* Target = nullptr;
    FindAggroFocus(BattleController, Target);
    return Target;
}

int32 UReditusTableBuffLibrary::ResolveAggroFocusIndex(AActor* BattleController, AActor* Selector, int32 LegacyIndex)
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

int32 UReditusTableBuffLibrary::GetBuffedDefense(AActor* Target, int32 BaseDefense)
{
    if (!IsValid(Target)) return BaseDefense;
    TInlineComponentArray<UReditusTableBuffState*> States;
    Target->GetComponents(States);
    double Delta = 0.0;
    for (const auto* State : States)
        if (IsValid(State)) Delta += State->DefenseDelta();
    return ReditusTableBuffRules::ScaleDefense(BaseDefense, Delta);
}

IMPLEMENT_MODULE(FDefaultModuleImpl, ReditusTableBuffs)
