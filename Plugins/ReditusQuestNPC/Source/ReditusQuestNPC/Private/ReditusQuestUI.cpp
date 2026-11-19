#include "ReditusQuestUI.h"
#include "ReditusQuestSubsystem.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/PanelWidget.h"
#include "Components/ProgressBar.h"
#include "Components/ScrollBox.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "GameFramework/PlayerController.h"
#include "Engine/Font.h"
#include "Kismet/GameplayStatics.h"
#include "Styling/CoreStyle.h"
#include "UObject/UnrealType.h"

namespace
{
    template<class T> T* Find(UUserWidget* View, const TCHAR* Name)
    { return View && View->WidgetTree ? Cast<T>(View->WidgetTree->FindWidget(FName(Name))) : nullptr; }
    void Text(UUserWidget* View, const TCHAR* Name, const FText& Value)
    { if (auto* Widget = Find<UTextBlock>(View, Name)) Widget->SetText(Value); }
    void Int(UObject* Object, const TCHAR* Name, int32 Value)
    { if (auto* P = FindFProperty<FIntProperty>(Object->GetClass(), FName(Name))) P->SetPropertyValue_InContainer(Object, Value); }
    int32 Int(UObject* Object, const TCHAR* Name)
    { auto* P = FindFProperty<FIntProperty>(Object->GetClass(), FName(Name)); return P ? P->GetPropertyValue_InContainer(Object) : 0; }
    void Flag(UObject* Object, const TCHAR* Name, bool Value)
    { if (auto* P = FindFProperty<FBoolProperty>(Object->GetClass(), FName(Name))) P->SetPropertyValue_InContainer(Object, Value); }
    void Highlight(UUserWidget* Row, bool Selected)
    {
        const auto Visibility = Selected ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Hidden;
        for (const TCHAR* Name : {TEXT("SelectedImage"), TEXT("Underline")})
            if (auto* Widget = Find<UWidget>(Row, Name)) Widget->SetVisibility(Visibility);
        if (auto* Name = Find<UTextBlock>(Row, TEXT("QuestName")))
            Name->SetColorAndOpacity(Selected ? FLinearColor(1.f, .85f, .4f) : FLinearColor::White);
    }
    UTextBlock* Label(UWidgetTree* Tree, const FString& Value, int32 Size, FLinearColor Color, UFont* Font)
    {
        auto* T = Tree->ConstructWidget<UTextBlock>(); T->SetText(FText::FromString(Value));
        T->SetFont(Font ? FSlateFontInfo(Font, Size) : FCoreStyle::GetDefaultFontStyle("Regular", Size));
        T->SetColorAndOpacity(Color); T->SetAutoWrapText(true); return T;
    }

    bool MatchesPage(EReditusQuestJournalPage Page, bool Main, bool Completed, bool Failed)
    {
        switch (Page)
        {
        case EReditusQuestJournalPage::MainQuests: return Main && !Completed && !Failed;
        case EReditusQuestJournalPage::SideQuests: return !Main && !Completed && !Failed;
        case EReditusQuestJournalPage::CompletedQuests: return Completed || Failed;
        case EReditusQuestJournalPage::FailedQuests: return Failed;
        }
        return false;
    }

    void AddBlueprintQuest(TArray<FReditusQuestJournalEntry>& Entries, EReditusQuestJournalPage Page,
        UClass* Quest, int32 Progress, bool Completed)
    {
        const auto* Defaults = IsValid(Quest) ? Quest->GetDefaultObject() : nullptr;
        if (!Defaults) return;
        const auto* Main = FindFProperty<FBoolProperty>(Quest, TEXT("IsMainQuest"));
        const auto* Name = FindFProperty<FTextProperty>(Quest, TEXT("name"));
        const auto* Description = FindFProperty<FTextProperty>(Quest, TEXT("description"));
        const auto* Steps = FindFProperty<FIntProperty>(Quest, TEXT("steps"));
        if (!Main || !Name || !Description || !Steps
            || !MatchesPage(Page, Main->GetPropertyValue_InContainer(Defaults), Completed, false)) return;

        FReditusQuestJournalEntry Entry;
        Entry.BlueprintQuest = Quest;
        Entry.Title = Name->GetPropertyValue_InContainer(Defaults);
        Entry.Description = Description->GetPropertyValue_InContainer(Defaults);
        Entry.Goal = FMath::Max(0, Steps->GetPropertyValue_InContainer(Defaults));
        Entry.Progress = Completed ? Entry.Goal : FMath::Clamp(Progress, 0, Entry.Goal);
        Entries.Add(MoveTemp(Entry));
    }

    void ReadBlueprintQuests(APlayerController* Player, EReditusQuestJournalPage Page,
        TArray<FReditusQuestJournalEntry>& Entries)
    {
        if (!IsValid(Player) || Page == EReditusQuestJournalPage::FailedQuests) return;
        // The original controller still owns these class-based quests and their saved progress.
        if (Page == EReditusQuestJournalPage::CompletedQuests)
        {
            const auto* Property = FindFProperty<FArrayProperty>(Player->GetClass(), TEXT("completedQuests"));
            const auto* ClassProperty = Property ? CastField<FClassProperty>(Property->Inner) : nullptr;
            if (!ClassProperty) return;
            const FScriptArrayHelper Completed(Property, Property->ContainerPtrToValuePtr<void>(Player));
            TSet<UClass*> Seen;
            for (int32 I = 0; I < Completed.Num(); ++I)
            {
                auto* Quest = Cast<UClass>(ClassProperty->GetObjectPropertyValue(Completed.GetRawPtr(I)));
                if (!Quest || Seen.Contains(Quest)) continue;
                Seen.Add(Quest);
                AddBlueprintQuest(Entries, Page, Quest, 0, true);
            }
            return;
        }
        const auto* Property = FindFProperty<FMapProperty>(Player->GetClass(), TEXT("activeQuests"));
        const auto* ClassProperty = Property ? CastField<FClassProperty>(Property->KeyProp) : nullptr;
        const auto* Progress = Property ? CastField<FIntProperty>(Property->ValueProp) : nullptr;
        if (!ClassProperty || !Progress) return;
        FScriptMapHelper Active(Property, Property->ContainerPtrToValuePtr<void>(Player));
        for (int32 I = 0; I < Active.GetMaxIndex(); ++I)
        {
            if (!Active.IsValidIndex(I)) continue;
            auto* Quest = Cast<UClass>(ClassProperty->GetObjectPropertyValue(Active.GetKeyPtr(I)));
            AddBlueprintQuest(Entries, Page, Quest, Progress->GetPropertyValue(Active.GetValuePtr(I)), false);
        }
    }

    UClass* QuestRowClass(const FObjectProperty& Element)
    {
        UClass* RowClass = Element.PropertyClass;
        if (RowClass && RowClass != UUserWidget::StaticClass()
            && RowClass->IsChildOf(UUserWidget::StaticClass()) && !RowClass->HasAnyClassFlags(CLASS_Abstract))
            return RowClass;

        const TCHAR* Path = TEXT("/Game/TurnBasedJRPGTemplate/Blueprints/UI/Dialogues/BP_QuestButton.BP_QuestButton_C");
        UClass* StyledClass = LoadClass<UUserWidget>(nullptr, Path);
        return StyledClass && RowClass && StyledClass->IsChildOf(RowClass)
            && !StyledClass->HasAnyClassFlags(CLASS_Abstract) ? StyledClass : nullptr;
    }
}

void UReditusQuestJournalSelection::Clicked() { if (Binding.IsValid()) Binding->Select(Index); }

UReditusQuestJournalBinding* UReditusQuestUILibrary::Binding(UUserWidget* Journal)
{
    auto* System = UReditusQuestSubsystem::GetQuestSystem(Journal);
    if (!Journal || !System) return nullptr;
    for (auto* Existing : System->JournalBindings) if (Existing && Existing->Journal.Get() == Journal) return Existing;
    if (!Find<UPanelWidget>(Journal, TEXT("QuestList")) || !Find<UTextBlock>(Journal, TEXT("QuestName"))
        || !Find<UTextBlock>(Journal, TEXT("QuestDescription")))
    { UE_LOG(LogTemp, Warning, TEXT("Quest journal needs the existing QuestList, QuestName and QuestDescription widgets.")); return nullptr; }
    auto* Result = NewObject<UReditusQuestJournalBinding>(System);
    Result->Journal = Journal; Result->System = System; System->JournalBindings.Add(Result);
    System->OnQuestChanged.AddUniqueDynamic(Result, &UReditusQuestJournalBinding::Changed);
    System->OnQuestStateRestored.AddUniqueDynamic(Result, &UReditusQuestJournalBinding::Restored);
    return Result;
}
void UReditusQuestUILibrary::ShowQuestJournalPage(UUserWidget* Journal, EReditusQuestJournalPage Page)
{ if (auto* B = Binding(Journal)) B->SetPage(Page); }
void UReditusQuestUILibrary::SelectQuestJournalEntry(UUserWidget* Journal)
{ if (auto* B = Binding(Journal)) B->Select(Int(Journal, TEXT("selectedQuestIndex"))); }
void UReditusQuestJournalBinding::Changed(FDataTableRowHandle) { Refresh(); }
void UReditusQuestJournalBinding::Restored() { Refresh(); }
void UReditusQuestJournalBinding::SetPage(EReditusQuestJournalPage NewPage)
{ Page = NewPage; if (Journal.IsValid()) Int(Journal.Get(), TEXT("selectedQuestIndex"), 0); Refresh(); }

void UReditusQuestJournalBinding::Refresh()
{
    auto* View = Journal.Get(); auto* Manager = System.Get(); if (!View || !Manager) return;
    auto* List = Find<UPanelWidget>(View, TEXT("QuestList")); if (!List) return;
    auto* Array = FindFProperty<FArrayProperty>(View->GetClass(), TEXT("questUIs"));
    auto* Element = Array ? CastField<FObjectProperty>(Array->Inner) : nullptr;
    auto* RowClass = Element ? QuestRowClass(*Element) : nullptr;
    auto* Player = View->GetOwningPlayer();
    if (!Player) Player = UGameplayStatics::GetPlayerController(View, 0);
    if (!RowClass || !Player)
    {
        UE_LOG(LogTemp, Warning, TEXT("Could not refresh %s. Check its questUIs row widget type and owning player."), *View->GetName());
        return;
    }

    TArray<FReditusQuestJournalEntry> Visible;
    ReadBlueprintQuests(Player, Page, Visible);
    for (const auto& Row : Manager->GetStartedQuests())
    {
        FReditusQuestDefinition Definition;
        FReditusQuestSnapshot Snapshot;
        TArray<FReditusQuestObjectiveStatus> Tasks;
        if (!Manager->GetQuestDetails(Row, Definition, Snapshot, Tasks)
            || !MatchesPage(Page, Definition.IsMainQuest, Snapshot.State == EReditusQuestState::Completed,
                Snapshot.State == EReditusQuestState::Failed)) continue;
        FReditusQuestJournalEntry Entry;
        Entry.QuestRow = Row;
        Entry.Title = Definition.Title;
        Visible.Add(MoveTemp(Entry));
    }

    const int32 PreviousIndex = Int(View, TEXT("selectedQuestIndex"));
    const auto Previous = Entries.IsValidIndex(PreviousIndex) ? Entries[PreviousIndex] : FReditusQuestJournalEntry();
    List->ClearChildren(); RowWidgets.Reset(); Entries.Reset(); Clicks.Reset();
    FScriptArrayHelper WidgetArray(Array, Array->ContainerPtrToValuePtr<void>(View));
    WidgetArray.EmptyValues();
    Flag(View, TEXT("MainQuestsVisible"), Page == EReditusQuestJournalPage::MainQuests);
    Flag(View, TEXT("CompletedQuestsVisible"), Page == EReditusQuestJournalPage::CompletedQuests);
    const TCHAR* Heading = Page == EReditusQuestJournalPage::MainQuests ? TEXT("Main Quests")
        : Page == EReditusQuestJournalPage::SideQuests ? TEXT("Side Quests")
        : Page == EReditusQuestJournalPage::FailedQuests ? TEXT("Failed Quests") : TEXT("Finished Quests");
    Text(View, TEXT("QuestsDialogueTitle"), FText::FromString(Heading));
    for (const auto& Entry : Visible)
    {
        auto* Item = CreateWidget<UUserWidget>(Player, RowClass);
        if (!Item || !List->AddChild(Item)) continue;
        Text(Item, TEXT("QuestName"), Entry.Title);
        RowWidgets.Add(Item);
        const int32 Index = Entries.Add(Entry);
        const int32 Stored = WidgetArray.AddValue();
        Element->SetObjectPropertyValue(WidgetArray.GetRawPtr(Stored), Item);
        if (auto* Button = Find<UButton>(Item, TEXT("QuestButton")))
        {
            auto* Click = NewObject<UReditusQuestJournalSelection>(this); Click->Binding = this; Click->Index = Index;
            Clicks.Add(Click); Button->OnClicked.AddUniqueDynamic(Click, &UReditusQuestJournalSelection::Clicked);
        }
    }
    if (auto* Empty = Find<UWidget>(View, TEXT("EmptyText")))
        Empty->SetVisibility(Entries.Num() ? ESlateVisibility::Collapsed : ESlateVisibility::HitTestInvisible);
    const int32 Selected = Entries.IndexOfByPredicate([&Previous](const FReditusQuestJournalEntry& Entry)
    { return Entry.BlueprintQuest == Previous.BlueprintQuest && Entry.QuestRow == Previous.QuestRow; });
    Select(Selected == INDEX_NONE ? PreviousIndex : Selected);
}

void UReditusQuestJournalBinding::Select(int32 Index)
{
    auto* View = Journal.Get(); auto* Manager = System.Get(); if (!View || !Manager) return;
    Index = FMath::Clamp(Index, 0, FMath::Max(0, Entries.Num() - 1));
    Int(View, TEXT("selectedQuestIndex"), Index);
    for (int32 I = 0; I < RowWidgets.Num(); ++I) Highlight(RowWidgets[I], I == Index);
    if (auto* P = FindFProperty<FObjectPropertyBase>(View->GetClass(), TEXT("highlightedQuest")))
        P->SetObjectPropertyValue_InContainer(View, RowWidgets.IsValidIndex(Index) ? RowWidgets[Index] : nullptr);
    if (auto* Bar = Find<UProgressBar>(View, TEXT("QuestBar"))) Bar->SetPercent(0);
    Text(View, TEXT("QuestName"), FText::GetEmpty()); Text(View, TEXT("QuestDescription"), FText::GetEmpty());
    Text(View, TEXT("ProgressDetails"), FText::GetEmpty());
    if (auto* Overlay = Find<UWidget>(View, TEXT("ProgressOverlay")))
        Overlay->SetVisibility(Entries.Num() ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
    if (!Entries.IsValidIndex(Index)) return;
    if (auto* Scroll = Find<UScrollBox>(View, TEXT("QuestList"))) Scroll->ScrollWidgetIntoView(RowWidgets[Index]);
    const auto& Entry = Entries[Index];
    if (Entry.BlueprintQuest)
    {
        Text(View, TEXT("QuestName"), Entry.Title);
        Text(View, TEXT("QuestDescription"), Entry.Description);
        if (auto* Bar = Find<UProgressBar>(View, TEXT("QuestBar")))
            Bar->SetPercent(Entry.Goal > 0 ? float(Entry.Progress) / float(Entry.Goal) : 0.f);
        Text(View, TEXT("ProgressDetails"), FText::FromString(FString::Printf(TEXT("%d/%d"), Entry.Progress, Entry.Goal)));
        return;
    }
    FReditusQuestDefinition Definition; FReditusQuestSnapshot Snapshot; TArray<FReditusQuestObjectiveStatus> Tasks;
    if (!Manager->GetQuestDetails(Entry.QuestRow, Definition, Snapshot, Tasks)) return;
    Text(View, TEXT("QuestName"), Definition.Title);
    FText Description = Definition.Description;
    const auto AppendLine = [](const FText& A, const FText& B)
    { return FText::Format(FText::FromString(TEXT("{0}\n{1}")), A, B); };
    double Count = 0, Goal = 0;
    for (const auto& Task : Tasks)
    {
        if (!Task.Objective.Optional) { Count += Task.Count; Goal += Task.Objective.RequiredCount; }
        if (!Task.CurrentStage) continue;
        FFormatNamedArguments Args;
        Args.Add(TEXT("Marker"), FText::FromString(Task.Complete ? TEXT("[+]") : TEXT("[ ]")));
        Args.Add(TEXT("Objective"), Task.Objective.Description);
        Args.Add(TEXT("Current"), FText::AsNumber(Task.Count)); Args.Add(TEXT("Goal"), FText::AsNumber(Task.Objective.RequiredCount));
        Description = AppendLine(Description, FText::Format(NSLOCTEXT("ReditusQuests", "ObjectiveProgress", "{Marker} {Objective}  {Current}/{Goal}"), Args));
    }
    for (const auto& Choice : Manager->GetQuestChoices(Entry.QuestRow))
        for (const auto& Stage : Definition.Stages) for (const auto& Authored : Stage.Choices)
            if (Authored.ChoiceID == Choice.ChoiceID) for (const auto& Option : Authored.Options)
                if (Option.OptionID == Choice.OptionID)
                    Description = AppendLine(Description, FText::Format(NSLOCTEXT("ReditusQuests", "RecordedQuestChoice", "{0}: {1}"), Authored.Description, Option.Label));
    if (Snapshot.State == EReditusQuestState::ReadyToDeliver) Description = AppendLine(Description, NSLOCTEXT("ReditusQuests", "ReadyToTurnIn", "Ready to turn in."));
    if (Snapshot.State == EReditusQuestState::Completed) Description = AppendLine(Description, NSLOCTEXT("ReditusQuests", "QuestCompletedDetail", "Completed."));
    if (Snapshot.State == EReditusQuestState::Failed) Description = AppendLine(Description, NSLOCTEXT("ReditusQuests", "QuestTimeExpired", "Time expired."));
    const FText Rewards = Manager->GetQuestRewardSummary(Entry.QuestRow);
    if (!Rewards.IsEmpty()) Description = FText::Format(FText::FromString(TEXT("{0}\n\n{1}")), Description, Rewards);
    Text(View, TEXT("QuestDescription"), Description);
    if (auto* Bar = Find<UProgressBar>(View, TEXT("QuestBar"))) Bar->SetPercent(Goal > 0 ? float(Count / Goal) : 0);
    Text(View, TEXT("ProgressDetails"), FText::FromString(FString::Printf(TEXT("%.0f/%.0f"), Count, Goal)));
}

TSharedRef<SWidget> UReditusQuestNotificationsWidget::RebuildWidget()
{
    WidgetTree = NewObject<UWidgetTree>(this, TEXT("QuestHUDTree"));
    // This is the font referenced by the supplied quest journal/button assets.
    QuestFont = LoadObject<UFont>(nullptr, TEXT("/Game/DialogueSystem/Font/final_fantasy_36_font_Font.final_fantasy_36_font_Font"));
    auto* Canvas = WidgetTree->ConstructWidget<UCanvasPanel>(); WidgetTree->RootWidget = Canvas;
    auto* Stack = WidgetTree->ConstructWidget<UVerticalBox>();
    // UE4.27: auto-wrap to the allotted width instead of SetWrapTextAt.
    // 440 outside - 4 border pixels - 36 inner padding = 400 for text.
    auto* WidthBox = WidgetTree->ConstructWidget<USizeBox>();
    WidthBox->SetWidthOverride(440.f); WidthBox->SetContent(Stack);
    auto* Position = Canvas->AddChildToCanvas(WidthBox); Position->SetAnchors(FAnchors(1.f, 0.f));
    Position->SetAlignment(FVector2D(1.f, 0.f)); Position->SetPosition(FVector2D(-38.f, 135.f));
    Position->SetAutoSize(true);
    TimerList = WidgetTree->ConstructWidget<UVerticalBox>(); Stack->AddChildToVerticalBox(TimerList);
    Card = WidgetTree->ConstructWidget<UBorder>(); Card->SetBrushColor(FLinearColor(.7f, .56f, .27f, .95f)); Card->SetPadding(2.f);
    Stack->AddChildToVerticalBox(Card)->SetPadding(FMargin(0, 10, 0, 0));
    auto* Inside = WidgetTree->ConstructWidget<UBorder>(); Inside->SetBrushColor(FLinearColor(.015f, .035f, .065f, .92f));
    Inside->SetPadding(FMargin(18, 12)); Card->SetContent(Inside);
    auto* Lines = WidgetTree->ConstructWidget<UVerticalBox>(); Inside->SetContent(Lines);
    Heading = Label(WidgetTree, TEXT(""), 15, FLinearColor(1, .82f, .4f), QuestFont);
    Title = Label(WidgetTree, TEXT(""), 21, FLinearColor::White, QuestFont);
    Detail = Label(WidgetTree, TEXT(""), 16, FLinearColor(.8f, .84f, .88f), QuestFont);
    Lines->AddChildToVerticalBox(Heading); Lines->AddChildToVerticalBox(Title); Lines->AddChildToVerticalBox(Detail);
    Card->SetVisibility(ESlateVisibility::Collapsed); SetVisibility(ESlateVisibility::HitTestInvisible);
    bShowing = false; RefreshIn = 0;
    TimerFrames.Empty(); TimerTitles.Empty(); TimerValues.Empty();
    LastTimerTitles.Empty(); LastTimerValues.Empty();
    return Super::RebuildWidget();
}
void UReditusQuestNotificationsWidget::Enqueue(const FReditusQuestNotification& Notice)
{
    const bool Terminal = Notice.Kind == EReditusQuestNoticeKind::Completed || Notice.Kind == EReditusQuestNoticeKind::Failed;
    if (Terminal)
    {
        // An immediate hand-in must not hide its reward behind stale progress.
        Queue.RemoveAll([&](const FReditusQuestNotification& Existing)
        { return Existing.Quest == Notice.Quest && (Existing.Kind == EReditusQuestNoticeKind::Added || Existing.Kind == EReditusQuestNoticeKind::Updated); });
        if (bShowing && CurrentNotice.Quest == Notice.Quest
            && (CurrentNotice.Kind == EReditusQuestNoticeKind::Added || CurrentNotice.Kind == EReditusQuestNoticeKind::Updated))
        {
            bShowing = false; Queue.Insert(Notice, 0);
            if (Card) ShowNext();
            return;
        }
    }
    // Coalesce bursts of progress for the same quest; retain other quests' notices.
    if (Notice.Kind == EReditusQuestNoticeKind::Updated)
        for (auto& Existing : Queue) if (Existing.Quest == Notice.Quest && Existing.Kind == Notice.Kind) { Existing = Notice; return; }
    Queue.Add(Notice);
    if (!bShowing && Card) ShowNext();
}
void UReditusQuestNotificationsWidget::ClearNotices()
{ Queue.Empty(); bShowing = false; if (Card) Card->SetVisibility(ESlateVisibility::Collapsed); }
void UReditusQuestNotificationsWidget::ShowNext()
{
    if (!Card || Queue.Num() == 0) return;
    const auto Notice = Queue[0]; Queue.RemoveAt(0); CurrentNotice = Notice; bShowing = true; Age = 0;
    const FText TextValue = Notice.Kind == EReditusQuestNoticeKind::Added ? NSLOCTEXT("ReditusQuests", "NoticeAdded", "QUEST ADDED")
        : Notice.Kind == EReditusQuestNoticeKind::Completed ? NSLOCTEXT("ReditusQuests", "NoticeCompleted", "QUEST COMPLETED")
        : Notice.Kind == EReditusQuestNoticeKind::Failed ? NSLOCTEXT("ReditusQuests", "NoticeFailed", "QUEST FAILED") : NSLOCTEXT("ReditusQuests", "NoticeUpdated", "QUEST UPDATED");
    Heading->SetText(TextValue); Title->SetText(Notice.Title); Detail->SetText(Notice.Detail);
    Card->SetRenderOpacity(0); Card->SetVisibility(ESlateVisibility::HitTestInvisible);
}
void UReditusQuestNotificationsWidget::RefreshTimers()
{
    auto* Manager = UReditusQuestSubsystem::GetQuestSystem(this); if (!Manager || !TimerList) return;
    const auto Timers = Manager->GetQuestTimers();
    // Keep rows alive as seconds change. Recreating auto-wrapped text every
    // second causes repeated initial layout and can visibly flash the clock.
    while (TimerFrames.Num() > Timers.Num())
    {
        const int32 I = TimerFrames.Num() - 1;
        TimerList->RemoveChild(TimerFrames[I]);
        TimerFrames.RemoveAt(I); TimerTitles.RemoveAt(I); TimerValues.RemoveAt(I);
        LastTimerTitles.RemoveAt(I); LastTimerValues.RemoveAt(I);
    }
    while (TimerFrames.Num() < Timers.Num())
    {
        auto* Frame = WidgetTree->ConstructWidget<UBorder>(); Frame->SetBrushColor(FLinearColor(.7f, .56f, .27f, .95f)); Frame->SetPadding(2);
        auto* Inner = WidgetTree->ConstructWidget<UBorder>(); Inner->SetBrushColor(FLinearColor(.015f, .035f, .065f, .92f)); Inner->SetPadding(FMargin(18, 9));
        auto* Contents = WidgetTree->ConstructWidget<UVerticalBox>();
        auto* TimerTitle = Label(WidgetTree, TEXT(""), 22, FLinearColor(1.f, .87f, .55f), QuestFont);
        auto* TimerValue = Label(WidgetTree, TEXT(""), 24, FLinearColor(1.f, .87f, .55f), QuestFont);
        TimerValue->SetAutoWrapText(false); // Clock width never triggers paragraph auto-wrap.
        Contents->AddChildToVerticalBox(TimerTitle); Contents->AddChildToVerticalBox(TimerValue);
        Inner->SetContent(Contents); Frame->SetContent(Inner);
        TimerList->AddChildToVerticalBox(Frame)->SetPadding(FMargin(0, 0, 0, 7));
        TimerFrames.Add(Frame); TimerTitles.Add(TimerTitle); TimerValues.Add(TimerValue);
        LastTimerTitles.Add(FString()); LastTimerValues.Add(FString());
    }
    for (int32 I = 0; I < Timers.Num(); ++I)
    {
        const FString TitleLine = Timers[I].Label.ToString();
        const FString TimeLine = Timers[I].DisplayTime + (Timers[I].Paused ? TEXT("  (Paused)") : TEXT(""));
        if (LastTimerTitles[I] != TitleLine)
        { TimerTitles[I]->SetText(FText::FromString(TitleLine)); LastTimerTitles[I] = TitleLine; }
        if (LastTimerValues[I] != TimeLine)
        { TimerValues[I]->SetText(FText::FromString(TimeLine)); LastTimerValues[I] = TimeLine; }
    }
}
void UReditusQuestNotificationsWidget::NativeTick(const FGeometry& Geometry, float DeltaTime)
{
    Super::NativeTick(Geometry, DeltaTime);
    RefreshIn -= DeltaTime; if (RefreshIn <= 0) { RefreshIn = .1f; RefreshTimers(); }
    if (!bShowing) { ShowNext(); return; }
    Age += DeltaTime; const float Alpha = FMath::Clamp(FMath::Min(Age / .18f, (3.5f - Age) / .35f), 0.f, 1.f);
    Card->SetRenderOpacity(Alpha);
    if (Age >= 3.5f) { bShowing = false; Card->SetVisibility(ESlateVisibility::Collapsed); ShowNext(); }
}
