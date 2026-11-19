// Actual editor code compiled against the UE4.27 const-table and pin API contracts.
#include <cassert>
#include <algorithm>
#include <string>
#include <vector>

#define TEXT(V) V
using FString=std::string;
struct UDataTable
{
    const void* Type=nullptr;FString Path;int Writes=0;
    const void* GetRowStruct()const{return Type;}
    FString GetPathName()const{return Path;}
};
struct FDataTableRowHandle{const UDataTable* DataTable=nullptr;FString RowName;};
struct Asset{FDataTableRowHandle RegisteredQuest;FString QuestID;};
struct FReditusQuestCatalogRow{static const void* StaticStruct(){static int Tag;return &Tag;}};
static UDataTable Fallback;
namespace ReditusQuestGraphCompiler
{
    UDataTable* DefaultCatalog(FString&){return &Fallback;}
    bool Lookup(Asset* Asset,FString& Error,UDataTable*& Result)
    {
        // ACTUAL_MUTABLE_CATALOG_LOOKUP
        Result=Catalog;return true;
    }
}
struct UAssetEditorSubsystem
{
    FString OpenedPath;
    void OpenEditorForAsset(const FString& Path){OpenedPath=Path;}
};
struct Editor
{
    UAssetEditorSubsystem Subsystem;
    template<class T>T* GetEditorSubsystem(){return &Subsystem;}
};
static Editor* GEditor=nullptr;
static void OpenCatalog(Asset* Quest,FString& Status)
{
    // ACTUAL_OPEN_CATALOG
}

template<class T>struct TArray:std::vector<T>
{using std::vector<T>::vector;void Reset(){this->clear();}};
struct UEdGraphPin
{
    std::string PinName,PinFriendlyName;
    int Direction=0,PinId=0,Modified=0;
    TArray<UEdGraphPin*> LinkedTo;
    void Modify(){++Modified;}
    void BreakAllPinLinks()
    {
        for(auto* Other:LinkedTo)
        {
            auto& Links=Other->LinkedTo;
            Links.erase(std::remove(Links.begin(),Links.end(),this),Links.end());
        }
        LinkedTo.clear();
    }
    void MovePersistentDataFromOldPin(UEdGraphPin& Old)
    {
        PinId=Old.PinId;LinkedTo=Old.LinkedTo;Old.LinkedTo.clear();
        for(auto* Other:LinkedTo)for(auto*& Link:Other->LinkedTo)if(Link==&Old)Link=this;
        PinFriendlyName=Old.PinFriendlyName;
    }
};
struct UEdGraphNode
{
    static int Destroyed;
    static void DestroyPin(UEdGraphPin* Pin){Pin->BreakAllPinLinks();delete Pin;++Destroyed;}
};
int UEdGraphNode::Destroyed=0;
struct Graph{int Changes=0;void NotifyGraphChanged(){++Changes;}};
struct UReditusQuestGraphNode:UEdGraphNode
{
    TArray<UEdGraphPin*> Pins;std::vector<std::string> Desired{"In","Next"};
    Graph Owner;int Invalidations=0;static int NextID;
    void AllocateDefaultPins()
    {
        for(const auto& Name:Desired)
        {
            auto* Pin=new UEdGraphPin;Pin->PinName=Name;Pin->Direction=Name=="In"?0:1;
            Pin->PinId=++NextID;Pin->PinFriendlyName="new "+Name;Pins.push_back(Pin);
        }
    }
    UEdGraphPin* FindPin(const std::string& Name,int Direction)
    {for(auto* Pin:Pins)if(Pin->PinName==Name&&Pin->Direction==Direction)return Pin;return nullptr;}
    Graph* GetGraph(){return &Owner;}void Changed(){++Invalidations;}
    void RefreshPins();
    ~UReditusQuestGraphNode(){for(auto* Pin:Pins)DestroyPin(Pin);}
};
int UReditusQuestGraphNode::NextID=0;
// ACTUAL_REFRESH_PINS

int main()
{
    Fallback.Type=FReditusQuestCatalogRow::StaticStruct();Fallback.Path="/Game/DT_QuestCatalog.DT_QuestCatalog";
    Asset Quest;Quest.QuestID="Hunt";FString Error;UDataTable* Mutable=nullptr;
    assert(ReditusQuestGraphCompiler::Lookup(&Quest,Error,Mutable)&&Mutable==&Fallback);
    Quest.RegisteredQuest={&Fallback,"Hunt"};
    assert(ReditusQuestGraphCompiler::Lookup(&Quest,Error,Mutable));++Mutable->Writes;
    assert(Fallback.Writes==1); // The editor owns the explicit mutable boundary.
    Quest.RegisteredQuest.RowName="Other";assert(!ReditusQuestGraphCompiler::Lookup(&Quest,Error,Mutable));
    Quest.RegisteredQuest.RowName="Hunt";
    Editor RealEditor;GEditor=&RealEditor;FString Status;
    OpenCatalog(&Quest,Status);assert(RealEditor.Subsystem.OpenedPath==Fallback.Path);
    Quest.RegisteredQuest.DataTable=nullptr;RealEditor.Subsystem.OpenedPath.clear();
    OpenCatalog(&Quest,Status);assert(RealEditor.Subsystem.OpenedPath==Fallback.Path);

    // Refresh must preserve a live wire and stable pin ID, then disconnect a removed output.
    UEdGraphPin Other;Other.PinName="Other";
    {
        UReditusQuestGraphNode Node;Node.AllocateDefaultPins();auto* Before=Node.FindPin("Next",1);
        const int ID=Before->PinId;Before->LinkedTo.push_back(&Other);Other.LinkedTo.push_back(Before);
        Node.RefreshPins();auto* After=Node.FindPin("Next",1);
        assert(After&&After->PinId==ID&&After->LinkedTo.size()==1&&Other.LinkedTo[0]==After);
        assert(After->PinFriendlyName=="new Next"&&UEdGraphNode::Destroyed==2&&Other.Modified==1);
        Node.RefreshPins();assert(Node.FindPin("Next",1)->PinId==ID&&Other.LinkedTo.size()==1);
        Node.Desired={"In"};Node.RefreshPins();assert(Other.LinkedTo.empty());
        assert(Node.Invalidations==3&&Node.Owner.Changes==3);
    }
    assert(UEdGraphNode::Destroyed==7);
}
