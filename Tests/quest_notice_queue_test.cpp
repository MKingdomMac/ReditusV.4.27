#include <algorithm>
#include <cassert>
#include <vector>
enum class EReditusQuestNoticeKind{Added,Updated,Completed,Failed};
struct FReditusQuestNotification{int Quest=0;EReditusQuestNoticeKind Kind=EReditusQuestNoticeKind::Added;int Revision=0;};
template<class T>struct TArray:std::vector<T>
{
    void Add(const T& V){this->push_back(V);}void Insert(const T& V,int I){this->insert(this->begin()+I,V);}
    template<class P>void RemoveAll(P Predicate){this->erase(std::remove_if(this->begin(),this->end(),Predicate),this->end());}
};
struct UReditusQuestNotificationsWidget
{
    TArray<FReditusQuestNotification> Queue;FReditusQuestNotification CurrentNotice;bool bShowing=false;bool Card=true;
    void Enqueue(const FReditusQuestNotification&);
    void ShowNext(){if(!Queue.empty()){CurrentNotice=Queue[0];Queue.erase(Queue.begin());bShowing=true;}}
};
// ACTUAL_ENQUEUE
int main()
{
    using K=EReditusQuestNoticeKind;UReditusQuestNotificationsWidget W;
    W.Enqueue({1,K::Added,0});assert(W.CurrentNotice.Quest==1&&W.CurrentNotice.Kind==K::Added);
    W.Enqueue({2,K::Added,0});W.Enqueue({1,K::Updated,1});W.Enqueue({1,K::Updated,2});assert(W.Queue.size()==2);
    W.Enqueue({1,K::Completed,3});assert(W.CurrentNotice.Kind==K::Completed&&W.CurrentNotice.Revision==3);
    assert(W.Queue.size()==1&&W.Queue[0].Quest==2); // Other quest's notice retained.
    W.Enqueue({3,K::Failed,0});assert(W.CurrentNotice.Kind==K::Completed&&W.Queue.size()==2);
    W.bShowing=false;W.ShowNext();assert(W.CurrentNotice.Quest==2);
    W.Enqueue({2,K::Failed,0});assert(W.CurrentNotice.Kind==K::Failed&&W.Queue[0].Quest==3);
    W.Card=false;W.bShowing=false;W.Enqueue({4,K::Completed,0});assert(!W.bShowing&&W.Queue.back().Quest==4);
}
