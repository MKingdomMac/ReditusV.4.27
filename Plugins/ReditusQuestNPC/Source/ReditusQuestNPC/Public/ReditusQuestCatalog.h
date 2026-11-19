#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "ReditusQuestAsset.h"
#include "ReditusQuestCatalog.generated.h"

UENUM(BlueprintType)
enum class EReditusQuestCatalogKind : uint8 { Graph, SimpleRow };

// A catalogue entry references a definition; it never stores player progress.
USTRUCT(BlueprintType)
struct REDITUSQUESTNPC_API FReditusQuestCatalogRow : public FTableRowBase
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Quest") EReditusQuestCatalogKind Kind = EReditusQuestCatalogKind::Graph;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Quest", meta=(EditCondition="Kind == EReditusQuestCatalogKind::Graph", EditConditionHides))
    UReditusQuestAsset* GraphQuest = nullptr;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Quest", meta=(RowType="ReditusQuestDefinition", EditCondition="Kind == EReditusQuestCatalogKind::SimpleRow", EditConditionHides))
    FDataTableRowHandle SimpleQuest;
};

namespace ReditusQuestRuntime { struct Definition; }

namespace ReditusQuestCatalog
{
    REDITUSQUESTNPC_API bool BuildDefinition(const FReditusQuestDefinition& Source, const FString& Key,
        ReditusQuestRuntime::Definition& Definition, FString& Error);
    // Simple aliases keep the original table/row identity, including existing saves.
    REDITUSQUESTNPC_API FDataTableRowHandle Canonical(FDataTableRowHandle Reference);
    REDITUSQUESTNPC_API const FReditusQuestDefinition* Definition(FDataTableRowHandle Reference);
    REDITUSQUESTNPC_API UDialogue* Conversation(FDataTableRowHandle Reference, EReditusQuestState State, FName StageID);
}

UCLASS()
class REDITUSQUESTNPC_API UReditusQuestCatalogLibrary : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()
public:
    UFUNCTION(BlueprintPure, Category="Reditus|Quest Catalogue")
    static bool FindQuestInCatalog(UDataTable* Catalog, FName QuestID, FDataTableRowHandle& Quest);
};
