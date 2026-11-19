// Actual editor setup function against typed host stand-ins. No asset files are touched.
#include <cassert>
#include <map>
#include <string>
#include <vector>
#define TEXT(V) V
#define UE_LOG(...) do {} while(false)
using FString=std::string;
FString operator*(const FString& V){return V;}
const int LOAD_NoWarn=1,LOAD_Quiet=2;
struct FText{std::string Value;static FText FromString(const char* S){return {S};}};
template<class T>struct TArray:std::vector<T>{void Add(const T& V){this->push_back(V);}};
struct UDataTable;
struct Handle{UDataTable* DataTable=nullptr;std::string RowName;};
enum class EReditusQuestObjectiveKind{Talk,DefeatEnemy,CollectItem};
struct FReditusQuestTaskDefinition
{
 std::string ObjectiveID,TargetID;FText Description;Handle ItemRow;
 EReditusQuestObjectiveKind Kind=EReditusQuestObjectiveKind::Talk;
 int RequiredCount=1;bool Optional=false;
};
struct FReditusQuestStageDefinition{std::string StageID;FText Description;TArray<FReditusQuestTaskDefinition> Objectives;};
struct FReditusQuestDefinition
{
 FText Title,Description;TArray<FReditusQuestStageDefinition> Stages;
 static int* StaticStruct(){static int Value;return &Value;}
};
struct UDataTable
{
 std::map<std::string,FReditusQuestDefinition> Rows;int Writes=0;
 struct RowMap{const UDataTable* Owner;bool Contains(const char* Name)const{return Owner->Rows.count(Name)!=0;}};
 RowMap GetRowMap()const{return {this};}
 void AddRow(const char* Name,const FReditusQuestDefinition& Row){Rows[Name]=Row;++Writes;}
 void MarkPackageDirty(){}
};
struct Editor{bool PlayWorld=false;};Editor Engine;Editor* GEditor=&Engine;
const char* QuestTablePackage="/Game/Reditus/Story/DT_Quests";
UDataTable Quests,Keys;bool Existing=false,KeysAvailable=true,Available=true;int AssetSetupCalls=0;
FString ObjectPath(const char* Path){return Path;}
bool PathAvailable(const char*){return Available;}
UDataTable* Table(const char*,int*){return &Quests;}
void CreateAssets(){++AssetSetupCalls;}
template<class T>T* LoadObject(void*,const FString& Path,void* =nullptr,int =0)
{
 if(Path==QuestTablePackage)return Existing?&Quests:nullptr;
 return KeysAvailable?&Keys:nullptr;
}
// ACTUAL_EDITOR_SETUP
int main()
{
 Keys.Rows["Letter"]={};CreateQuestSystem();
 assert(Quests.Rows.size()==2&&Quests.Writes==2&&AssetSetupCalls==1);
 const auto& Hunt=Quests.Rows.at("Test_DefeatBoss");
 assert(Hunt.Stages.size()==1&&Hunt.Stages[0].Objectives.size()==1);
 const auto& Target=Hunt.Stages[0].Objectives[0];
 assert(Target.Kind==EReditusQuestObjectiveKind::DefeatEnemy&&Target.TargetID=="DeathBoar"&&Target.RequiredCount==1&&!Target.Optional);
 const auto& Item=Quests.Rows.at("Test_PossessLetter").Stages[0].Objectives[0];
 assert(Item.Kind==EReditusQuestObjectiveKind::CollectItem&&Item.ItemRow.DataTable==&Keys&&Item.ItemRow.RowName=="Letter");
 // Upgrading an existing table must preserve edited examples, authored content,
 // and existing complex rows instead of resetting/deleting or adding tutorials.
 Existing=true;Quests.Rows["Test_DefeatBoss"].Title.Value="Authored title";
 Quests.Rows["MyQuest"].Title.Value="My content";
 Quests.Rows["Example_ExtortionSteps"].Title.Value="Existing complex quest";
 const int Writes=Quests.Writes;CreateQuestSystem();
 assert(Quests.Writes==Writes&&Quests.Rows.size()==4&&AssetSetupCalls==2);
 assert(Quests.Rows.at("Test_DefeatBoss").Title.Value=="Authored title");
 assert(Quests.Rows.at("MyQuest").Title.Value=="My content");
 assert(Quests.Rows.at("Example_ExtortionSteps").Title.Value=="Existing complex quest");
 // Missing inventory catalog/Letter never creates a broken item objective.
 Existing=false;KeysAvailable=false;Quests={};CreateQuestSystem();assert(Quests.Rows.size()==1&&Quests.Rows.count("Test_DefeatBoss"));
 KeysAvailable=true;Keys.Rows.clear();Quests={};CreateQuestSystem();assert(Quests.Rows.size()==1);
 // Play mode and an occupied unrelated asset path block all changes.
 Quests={};AssetSetupCalls=0;Engine.PlayWorld=true;CreateQuestSystem();assert(Quests.Rows.empty()&&AssetSetupCalls==0);
 Engine.PlayWorld=false;Available=false;CreateQuestSystem();assert(Quests.Rows.empty()&&AssetSetupCalls==0);
}
