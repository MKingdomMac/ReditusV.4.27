#pragma once

#include "CoreMinimal.h"
#include "Factories/Factory.h"
#include "ReditusQuestGraphFactory.generated.h"

UCLASS()
class UReditusQuestGraphFactory : public UFactory
{
    GENERATED_BODY()
public:
    UReditusQuestGraphFactory();
    virtual UObject* FactoryCreateNew(UClass* Class, UObject* Parent, FName Name, EObjectFlags Flags,
        UObject* Context, FFeedbackContext* Warn) override;
};

void RegisterReditusQuestGraphEditor();
void UnregisterReditusQuestGraphEditor();
