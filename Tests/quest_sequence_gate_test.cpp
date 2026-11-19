#include <cassert>
#include <string>
#include <cstdint>
#define TEXT(V) V
struct UEnum {std::string Path;std::string GetPathName()const{return Path;}};
struct FByteProperty {
    UEnum* Enum=nullptr;uint8_t State=0;
    uint8_t GetPropertyValue_InContainer(void*)const{return State;}
};
struct APlayerController {
    bool Valid=true;FByteProperty* Field=nullptr;
    APlayerController* GetClass(){return this;}
};
bool IsValid(APlayerController* P){return P && P->Valid;}
template<class T>T* FindFProperty(APlayerController* P,const char* Name){return std::string(Name)=="gameState" ? P->Field : nullptr;}
// ACTUAL_SEQUENCE_GATE
int main(){
    using namespace ReditusQuestSequenceGate;
    UEnum Enum{"/Game/TurnBasedJRPGTemplate/Blueprints/Enums/E_GameState.E_GameState"};
    FByteProperty Field;Field.Enum=&Enum;APlayerController P;P.Field=&Field;
    assert(IsExploring(&P));
    for(uint8_t State:{1,2,3,4}){Field.State=State;assert(!IsExploring(&P));}
    Field.State=0;assert(IsExploring(&P));
    assert(!IsExploring(nullptr));P.Valid=false;assert(!IsExploring(&P));P.Valid=true;
    P.Field=nullptr;assert(IsExploring(&P));
    P.Field=&Field;Field.Enum=nullptr;assert(IsExploring(&P));
    UEnum Other{"/Other/Enum"};Field.Enum=&Other;Field.State=2;assert(IsExploring(&P));
}
