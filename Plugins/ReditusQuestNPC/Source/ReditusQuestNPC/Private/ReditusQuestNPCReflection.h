#pragma once

#include "CoreMinimal.h"
#include "UObject/UnrealType.h"

namespace ReditusQuestNPCReflection
{
    inline int32 ParameterCount(UFunction* Function)
    {
        int32 Count = 0;
        if (Function) for (TFieldIterator<FProperty> It(Function); It; ++It)
            if (It->HasAnyPropertyFlags(CPF_Parm)) ++Count;
        return Count;
    }

    inline bool CallNoArguments(UObject* Object, FName Name)
    {
        UFunction* Function = IsValid(Object) ? Object->FindFunction(Name) : nullptr;
        if (!Function || ParameterCount(Function) != 0) return false;
        Object->ProcessEvent(Function, nullptr);
        return true;
    }

    inline bool DelegateSignature(UFunction* Function, bool OneBool)
    {
        if (!Function || ParameterCount(Function) != (OneBool ? 1 : 0)) return false;
        if (OneBool)
            for (TFieldIterator<FProperty> It(Function); It; ++It)
                if (It->HasAnyPropertyFlags(CPF_Parm))
                    return CastField<FBoolProperty>(*It) && !It->HasAnyPropertyFlags(CPF_OutParm | CPF_ReturnParm);
        return true;
    }

    inline bool Bind(UObject* Target, FName Name, UObject* Listener, FName Method, bool Add, bool OneBool = false)
    {
        auto* P = IsValid(Target) ? FindFProperty<FMulticastInlineDelegateProperty>(Target->GetClass(), Name) : nullptr;
        UFunction* Handler = IsValid(Listener) ? Listener->FindFunction(Method) : nullptr;
        if (!P || !DelegateSignature(P->SignatureFunction, OneBool) || !DelegateSignature(Handler, OneBool)) return false;
        auto* Delegate = P->ContainerPtrToValuePtr<FMulticastScriptDelegate>(Target);
        FScriptDelegate Callback;
        Callback.BindUFunction(Listener, Method);
        if (Add) Delegate->AddUnique(Callback);
        else Delegate->Remove(Callback);
        return true;
    }

    // Current DemoDialogueWidget.Removed carries one Blueprint recruitment enum.
    // Older widgets used no arguments. Match either known shape, without changing
    // the widget dispatcher or removing any other listener's binding.
    inline bool BindDialogueRemoved(UObject* Target, UObject* Listener, bool Add)
    {
        auto* P = IsValid(Target) ? FindFProperty<FMulticastInlineDelegateProperty>(Target->GetClass(), TEXT("Removed")) : nullptr;
        if (!P || !IsValid(Listener)) return false;
        if (DelegateSignature(P->SignatureFunction, false))
            return Bind(Target, TEXT("Removed"), Listener, TEXT("HandleDialogueRemoved"), Add);

        UFunction* Signature = P->SignatureFunction;
        UFunction* Handler = Listener->FindFunction(TEXT("HandleDialogueRemovedWithResult"));
        auto* Source = Signature ? FindFProperty<FByteProperty>(Signature, TEXT("RecruitmentEvents")) : nullptr;
        auto* Destination = Handler ? FindFProperty<FByteProperty>(Handler, TEXT("RecruitmentEvents")) : nullptr;
        if (ParameterCount(Signature) != 1 || ParameterCount(Handler) != 1 || !Source || !Destination
            || !Source->Enum || Source->ArrayDim != 1 || Destination->ArrayDim != 1
            || !Source->HasAnyPropertyFlags(CPF_Parm) || !Destination->HasAnyPropertyFlags(CPF_Parm)
            || Source->HasAnyPropertyFlags(CPF_OutParm | CPF_ReturnParm)
            || Destination->HasAnyPropertyFlags(CPF_OutParm | CPF_ReturnParm)
            || Signature->ParmsSize != Handler->ParmsSize
            || Source->GetOffset_ForInternal() != Destination->GetOffset_ForInternal()) return false;
        FScriptDelegate Callback;
        Callback.BindUFunction(Listener, TEXT("HandleDialogueRemovedWithResult"));
        auto* Delegate = P->ContainerPtrToValuePtr<FMulticastScriptDelegate>(Target);
        if (Add) Delegate->AddUnique(Callback);
        else Delegate->Remove(Callback);
        return true;
    }

    inline UObject* ReadObject(UObject* Object, FName Name)
    {
        auto* P = IsValid(Object) ? FindFProperty<FObjectPropertyBase>(Object->GetClass(), Name) : nullptr;
        return P ? P->GetObjectPropertyValue_InContainer(Object) : nullptr;
    }

    inline bool WriteObject(UObject* Object, FName Name, UObject* Value)
    {
        auto* P = IsValid(Object) ? FindFProperty<FObjectPropertyBase>(Object->GetClass(), Name) : nullptr;
        if (!P || (Value && !Value->IsA(P->PropertyClass))) return false;
        P->SetObjectPropertyValue_InContainer(Object, Value);
        return true;
    }
}
