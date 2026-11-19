#pragma once

#include "CoreMinimal.h"

class UReditusQuestAsset;
class UDataTable;

namespace ReditusQuestGraphCompiler
{
    bool Compile(UReditusQuestAsset* Asset, FString& Error, bool Register = true);
    UDataTable* DefaultCatalog(FString& Error);
}
