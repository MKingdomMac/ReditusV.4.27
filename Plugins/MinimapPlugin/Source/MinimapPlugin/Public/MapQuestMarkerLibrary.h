#pragma once
#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "MapQuestMarkerLibrary.generated.h"

class AActor;
class UImage;
class UTexture2D;
class UMapIconComponent;
class UWidgetComponent;

// One minimap-owned style. NPCs never choose main/side type or supply a texture.
// Optional global overrides live in DefaultGame.ini, not on individual actors.
UCLASS(Config=Game, DefaultConfig)
class MINIMAPPLUGIN_API UMapQuestMarkerStyle : public UObject
{
    GENERATED_BODY()
public:
    UPROPERTY(Config, EditAnywhere, Category="Quest Markers") FLinearColor MainQuestColor = FLinearColor(1.f, .72f, .2f);
    UPROPERTY(Config, EditAnywhere, Category="Quest Markers") FLinearColor SideQuestColor = FLinearColor(.35f, .85f, 1.f);
    UPROPERTY(Config, EditAnywhere, Category="Quest Markers") TSoftObjectPtr<UTexture2D> MainExclamationIcon;
    UPROPERTY(Config, EditAnywhere, Category="Quest Markers") TSoftObjectPtr<UTexture2D> SideQuestionIcon;
    UPROPERTY(Config, EditAnywhere, Category="Quest Markers") float MinimapSize = 26.f;
    UPROPERTY(Config, EditAnywhere, Category="Quest Markers") FVector2D OverheadSize = FVector2D(32.f, 40.f);
    UPROPERTY(Config, EditAnywhere, Category="Quest Markers") FVector OverheadOffset = FVector(0, 0, 24);
    UPROPERTY(Config, EditAnywhere, Category="Quest Markers") bool EdgeMarkers = true;
    UTexture2D* Icon(bool MainQuest);
private:
    UPROPERTY(Transient) UTexture2D* GeneratedMain = nullptr;
    UPROPERTY(Transient) UTexture2D* GeneratedSide = nullptr;
};

UCLASS()
class MINIMAPPLUGIN_API UMapQuestHeadWidget : public UUserWidget
{
    GENERATED_BODY()
public:
    void SetQuestType(bool MainQuest);
protected:
    virtual TSharedRef<SWidget> RebuildWidget() override;
private:
    UPROPERTY(Transient) UImage* Glyph = nullptr;
    bool bMainQuest = false;
};

UCLASS()
class MINIMAPPLUGIN_API UMapQuestMarkerLibrary : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()
public:
    // Quest system supplies visibility and DT_Quests.IsMainQuest; minimap owns appearance.
    UFUNCTION(BlueprintCallable, Category="Minimap|Quests")
    static void ApplyQuestMapIcon(UMapIconComponent* Icon, bool Visible, bool MainQuest, FText Tooltip);
    // Creates a real screen-space head widget, rather than enabling an editor billboard.
    static UWidgetComponent* UpdateQuestHead(AActor* Owner, UWidgetComponent* Existing,
        FVector HeadTop, bool Visible, bool MainQuest);
};
