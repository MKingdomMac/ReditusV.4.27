#include <cassert>
#include <memory>
#include <string>
#include <vector>
#include <algorithm>
using int32=int;
using FString=std::string;
#define TEXT(V) V
struct FText { std::string Value; static FText FromString(const FString& V){return {V};} FString ToString()const{return Value;} };
struct FLinearColor { FLinearColor(float,float,float,float = 1.f){} };
struct FMargin { FMargin(int,int){} FMargin(int,int,int,int){} };
template<class T>struct TArray {
    std::vector<T> V; int32 Num()const{return int32(V.size());} void Add(const T& X){V.push_back(X);}
    void RemoveAt(int32 I){V.erase(V.begin()+I);} void Empty(){V.clear();}
    T& operator[](int32 I){return V[unsigned(I)];} const T& operator[](int32 I)const{return V[unsigned(I)];}
};
struct Widget { virtual ~Widget()=default; };
struct UTextBlock:Widget { FText Text; int Writes=0; bool Wrap=true; void SetText(FText T){Text=T;++Writes;} void SetAutoWrapText(bool V){Wrap=V;} };
struct UBorder:Widget { Widget* Child=nullptr; void SetBrushColor(FLinearColor){} void SetPadding(int){} void SetPadding(FMargin){} void SetContent(Widget* V){Child=V;} };
struct Slot { void SetPadding(FMargin){} };
struct UVerticalBox:Widget {
    std::vector<Widget*> Children; Slot Layout; int Adds=0,Removes=0;
    Slot* AddChildToVerticalBox(Widget* W){Children.push_back(W);++Adds;return &Layout;}
    bool RemoveChild(Widget* W){auto I=std::find(Children.begin(),Children.end(),W);if(I==Children.end())return false;Children.erase(I);++Removes;return true;}
};
struct Tree { std::vector<std::unique_ptr<Widget>> All; template<class T>T* ConstructWidget(){auto* W=new T();All.emplace_back(W);return W;} };
static UTextBlock* Label(Tree* T,const FString& V,int32,FLinearColor,void*){auto* W=T->ConstructWidget<UTextBlock>();W->SetText(FText::FromString(V));return W;}
struct Timer { FText Label; FString DisplayTime; bool Paused=false; };
struct UReditusQuestSubsystem {
    static UReditusQuestSubsystem* Current; TArray<Timer> Timers;
    static UReditusQuestSubsystem* GetQuestSystem(void*){return Current;}
    TArray<Timer> GetQuestTimers(){return Timers;}
};
UReditusQuestSubsystem* UReditusQuestSubsystem::Current=nullptr;
struct UReditusQuestNotificationsWidget {
    Tree* WidgetTree=nullptr; UVerticalBox* TimerList=nullptr; void* QuestFont=nullptr;
    TArray<UBorder*> TimerFrames; TArray<UTextBlock*> TimerTitles,TimerValues;
    TArray<FString> LastTimerTitles,LastTimerValues;
    void RefreshTimers();
};
// ACTUAL_TIMER_REFRESH
int main()
{
    UReditusQuestSubsystem System; UReditusQuestSubsystem::Current=&System;
    Tree Widgets; UVerticalBox List; UReditusQuestNotificationsWidget View; View.WidgetTree=&Widgets;View.TimerList=&List;
    View.RefreshTimers(); assert(View.TimerFrames.Num()==0 && Widgets.All.empty());
    System.Timers.Add(Timer{FText{"Reach the gate"},"4:42",false}); View.RefreshTimers();
    assert(View.TimerFrames.Num()==1 && List.Children.size()==1);
    auto* Frame=View.TimerFrames[0];auto* Title=View.TimerTitles[0];auto* Clock=View.TimerValues[0];
    const auto Allocations=Widgets.All.size();const int TitleWrites=Title->Writes,ClockWrites=Clock->Writes;
    assert(Title->Text.Value=="Reach the gate" && Clock->Text.Value=="4:42" && !Clock->Wrap);
    for(int I=0;I<10;++I)View.RefreshTimers();
    assert(Widgets.All.size()==Allocations && Title->Writes==TitleWrites && Clock->Writes==ClockWrites);
    System.Timers[0].DisplayTime="4:41";View.RefreshTimers();
    assert(View.TimerFrames[0]==Frame && View.TimerTitles[0]==Title && View.TimerValues[0]==Clock);
    assert(Widgets.All.size()==Allocations && List.Adds==1 && List.Removes==0);
    assert(Title->Writes==TitleWrites && Clock->Writes==ClockWrites+1 && Clock->Text.Value=="4:41");
    System.Timers[0].Paused=true;View.RefreshTimers();assert(Clock->Text.Value=="4:41  (Paused)");
    System.Timers[0].Label=FText{"Another objective"};View.RefreshTimers();assert(Title->Text.Value=="Another objective");
    System.Timers.Add(Timer{FText{"Wait for signal"},"0:12",false});View.RefreshTimers();
    assert(View.TimerFrames.Num()==2 && View.TimerFrames[0]==Frame && List.Adds==2);
    System.Timers.RemoveAt(1);View.RefreshTimers();assert(View.TimerFrames.Num()==1 && View.TimerFrames[0]==Frame && List.Removes==1);
    System.Timers.Empty();View.RefreshTimers();assert(View.TimerFrames.Num()==0 && List.Children.empty());
    assert(View.TimerTitles.Num()==0 && View.TimerValues.Num()==0 && View.LastTimerTitles.Num()==0 && View.LastTimerValues.Num()==0);
    System.Timers.Add(Timer{FText{"New timer"},"1:00",false});View.RefreshTimers();assert(View.TimerFrames.Num()==1 && View.TimerValues[0]->Text.Value=="1:00");
}
