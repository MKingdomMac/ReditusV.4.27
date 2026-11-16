#pragma once
#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "ReditusQuestSystemTypes.h"
#include "ReditusQuestUI.generated.h"

class UButton;
class UFont;
class UBorder;
class UTextBlock;
class UVerticalBox;
class UReditusQuestSubsystem;
class UReditusQuestJournalBinding;

UENUM(BlueprintType)
enum class EReditusQuestJournalPage : uint8 { MainQuests, SideQuests, CompletedQuests, FailedQuests };

UCLASS()
class REDITUSQUESTNPC_API UReditusQuestJournalSelection : public UObject
{
    GENERATED_BODY()
public:
    TWeakObjectPtr<UReditusQuestJournalBinding> Binding;
    int32 Index = 0;
    UFUNCTION() void Clicked();
};

// Updates the existing BP_QuestDetails. Does not replace its Designer tree,
// parent class, Back button, menu focus or controller input handling.
UCLASS()
class REDITUSQUESTNPC_API UReditusQuestJournalBinding : public UObject
{
    GENERATED_BODY()
public:
    TWeakObjectPtr<UUserWidget> Journal;
    TWeakObjectPtr<UReditusQuestSubsystem> System;
    void Refresh();
    void SetPage(EReditusQuestJournalPage NewPage);
    void Select(int32 Index);
    UFUNCTION() void Changed(FDataTableRowHandle Quest);
    UFUNCTION() void Restored();
private:
    UPROPERTY(Transient) TArray<UUserWidget*> RowWidgets;
    UPROPERTY(Transient) TArray<FDataTableRowHandle> QuestRows;
    UPROPERTY(Transient) TArray<UReditusQuestJournalSelection*> Clicks;
    EReditusQuestJournalPage Page = EReditusQuestJournalPage::MainQuests;
};

// Shared, non-interactive notices and clocks. Never takes keyboard/gamepad focus.
UCLASS()
class REDITUSQUESTNPC_API UReditusQuestNotificationsWidget : public UUserWidget
{
    GENERATED_BODY()
public:
    void Enqueue(const FReditusQuestNotification& Notice);
    void ClearNotices();
protected:
    virtual TSharedRef<SWidget> RebuildWidget() override;
    virtual void NativeTick(const FGeometry& Geometry, float DeltaTime) override;
private:
    void ShowNext();
    void RefreshTimers();
    UPROPERTY(Transient) TArray<FReditusQuestNotification> Queue;
    UPROPERTY(Transient) FReditusQuestNotification CurrentNotice;
    UPROPERTY(Transient) UBorder* Card = nullptr;
    UPROPERTY(Transient) UTextBlock* Heading = nullptr;
    UPROPERTY(Transient) UTextBlock* Title = nullptr;
    UPROPERTY(Transient) UTextBlock* Detail = nullptr;
    UPROPERTY(Transient) UVerticalBox* TimerList = nullptr;
    UPROPERTY(Transient) UFont* QuestFont = nullptr;
    UPROPERTY(Transient) TArray<UBorder*> TimerFrames;
    UPROPERTY(Transient) TArray<UTextBlock*> TimerTitles;
    UPROPERTY(Transient) TArray<UTextBlock*> TimerValues;
    TArray<FString> LastTimerTitles;
    TArray<FString> LastTimerValues;
    bool bShowing = false;
    float Age = 0;
    float RefreshIn = 0;
};

UCLASS()
class REDITUSQUESTNPC_API UReditusQuestUILibrary : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()
public:
    UFUNCTION(BlueprintCallable, Category="Reditus|New Quests|UI")
    static void ShowQuestJournalPage(UUserWidget* Journal, EReditusQuestJournalPage Page);
    UFUNCTION(BlueprintCallable, Category="Reditus|New Quests|UI")
    static void SelectQuestJournalEntry(UUserWidget* Journal);
private:
    static UReditusQuestJournalBinding* Binding(UUserWidget* Journal);
};
