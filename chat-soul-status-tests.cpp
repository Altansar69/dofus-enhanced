#include "module.hpp"
#include "features.hpp"
#include "options-ui.hpp"
#include <iostream>
#include <memory>
#include <map>
#include <vector>
#include "chat-soul-status.cpp"

namespace fake {
using namespace managed;
struct Type { std::string name; };
struct Node { Type* type; int64_t scalar=0; std::map<std::string,Obj> fields; std::vector<Obj> items; std::map<int,Obj> objects; std::map<int,bool> flags; int cursor=-1; };
std::map<std::string,Type> types;
std::vector<std::unique_ptr<Node>> heap;
std::map<GCHandle,Obj> handles;
GCHandle serial=0x100000000ull;
Obj service,inventory,content,quests,finished,active,progress,objectives;
int moves=0,disposed=0;
std::string fail;
std::vector<std::string> logs;
void Check(bool b,const char* message) { if(!b)throw std::runtime_error(message); }
Node& Get(Obj o) { Check(o!=nullptr,"Null fixture object");return *static_cast<Node*>(o); }
Type* TypeOf(std::string name) { auto& t=types[name];t.name=name;return &t; }
Obj Make(std::string name) { auto n=std::make_unique<Node>();n->type=TypeOf(name);auto result=n.get();heap.push_back(std::move(n));return result; }
Obj Box(int64_t n) { auto o=Make("Box");Get(o).scalar=n;return o; }
void Set(Obj o,const char* field,int64_t n) { Get(o).fields[field]=Box(n); }
Obj Item(int id,int type=327,int quantity=1,int position=63) {
    auto item=Make("mc"),mask=Make("ItemMasks");Get(mask).fields["item"]=item;
    Set(item,"myn",id);Set(item,"mzb",type);Set(item,"myo",quantity);Set(item,"nag",position);return mask;
}
Obj FullStone(int monster) {
    auto mask=Item(7010,85);auto item=Get(mask).fields["item"],effects=Make("List"),effect=Make("EffectInstanceDice");
    Set(effect,"actionId",623);Set(effect,"value",monster);Get(effects).items={effect};Get(item).fields["myt"]=effects;return mask;
}
// Eliocalypse items share type 220 with heroic stones and carry other effects.
Obj OtherEffectItem() {
    auto mask=Item(21983,220);auto item=Get(mask).fields["item"],effects=Make("List"),effect=Make("EffectInstanceDice");
    Set(effect,"actionId",1);Set(effect,"value",5);Get(effects).items={effect};Get(item).fields["myt"]=effects;return mask;
}
void Items(std::vector<Obj> items) { Get(content).items=std::move(items);Get(Get(content).fields["_version"]).scalar++; }
void PumpFresh() { chat_souls::state.nextScan=0;chat_souls::state.nextQuest=0;TickChatSoulStatus(true); }
void FinishScan() { for(int i=0;i<100 && chat_souls::state.iterator.get();++i)TickChatSoulStatus(true);Check(!chat_souls::state.iterator.get(),"Scan did not finish"); }
std::string CombinedCaption(int monster,bool colored=false) {
    const auto inventoryCaption=ChatSoulCaption(monster,ChatSoulPart::Inventory,colored);
    const auto ocreCaption=ChatSoulCaption(monster,ChatSoulPart::Ocre,colored);
    return inventoryCaption.empty()?std::string{}:" · "+inventoryCaption+" · "+ocreCaption;
}
bool Has(int monster,const char* text) { return CombinedCaption(monster).find(text)!=std::string::npos; }
void Setup() {
    api.gchandle_new=[](Obj o,bool)->GCHandle{handles[++serial]=o;return serial;};
    api.gchandle_get_target=[](GCHandle h)->Obj{return handles.at(h);};
    api.gchandle_free=[](GCHandle h){Check(handles.erase(h)==1,"GC ref imbalance");};
    api.object_get_class=[](Obj o)->Klass{return Get(o).type;};
    api.class_is_assignable_from=[](Klass a,Klass b){return a==b || (static_cast<Type*>(a)->name=="EffectInstanceInteger"&&static_cast<Type*>(b)->name=="EffectInstanceDice");};
    api.object_unbox=[](Obj o)->void*{return &Get(o).scalar;};
    service=Make("feh");inventory=Make("Inventory");content=Make("Content");Set(content,"_version",1);
    Get(service).fields["bdoq"]=inventory;Get(inventory).fields["get_content"]=content;
    quests=Make("fdi");finished=Make("Finished");active=Make("Active");progress=Make("fcz");objectives=Make("Objectives");
    Set(finished,"_version",0);
    Get(quests).fields={{"bczk",finished},{"bczg",active}};Get(active).objects[439]=progress;
    Set(progress,"dtbr",439);Set(progress,"dtbs",863);Get(progress).fields["dtbt"]=objectives;
    Get(objectives).flags={{2266,true},{2267,false}};
}
}
namespace managed {
Api api{};uintptr_t gameBase=0;
Klass Api::Class(const char*,const char*,const char* name)const{return fake::TypeOf(name);}
Method Api::FindVerified(Klass,const char*,int,uintptr_t rva)const{return reinterpret_cast<Method>(rva);}
Obj Api::Field(Obj o,const char* name)const{return fake::Get(o).fields.at(name);}
Obj Api::Call(Obj o,const char* name,std::initializer_list<void*> arguments)const {
    using namespace fake;std::string n=name;Check(n!=fail,"Injected read failure");std::vector<void*> args(arguments);auto& node=Get(o);
    if(node.fields.contains(n))return node.fields.at(n);
    if(n=="get_Count")return Box(node.items.size());
    if(n=="get_Values")return o;
    if(n=="GetEnumerator"){auto e=Make("Enumerator");Get(e).fields["owner"]=o;return e;}
    if(n=="MoveNext"){++moves;return Box(++node.cursor < static_cast<int>(Get(node.fields.at("owner")).items.size()));}
    if(n=="get_Current")return Get(node.fields.at("owner")).items.at(node.cursor);
    if(n=="Dispose"){++disposed;return nullptr;}
    if(n=="get_Item")return node.items.at(*static_cast<int*>(args.at(0)));
    if(n=="Contains"){const int key=*static_cast<short*>(args.at(0));return Box(node.flags.contains(key)&&node.flags.at(key));}
    if(n=="TryGetValue"){
        const int key=*static_cast<short*>(args.at(0));
        if(node.type->name=="Active") {const bool found=node.objects.contains(key);*static_cast<Obj*>(args.at(1))=found?node.objects.at(key):nullptr;return Box(found);}
        Check(node.type->name=="Objectives","Unexpected dictionary value type");
        const bool found=node.flags.contains(key);*static_cast<bool*>(args.at(1))=found&&node.flags.at(key);return Box(found);
    }
    throw std::runtime_error("Unexpected call (read-only fixture): "+n);
}
}
Module* module=nullptr;
Module::Module(std::filesystem::path path):folder(std::move(path)){}
managed::Obj Module::FindService(const char* name)const {return std::string(name)=="fmo"?fake::service:std::string(name)=="fnp"?fake::quests:nullptr;}
void Log(const std::string& value){fake::logs.push_back(value);}
int main() {
    using namespace fake;
    try {
        Setup();Module owner("offline-only");module=&owner;owner.ready=true;owner.servicesReady=true;
        owner.cached.reset(Make("Cached"));owner.compositor.reset(Make("Compositor"));const auto baseline=handles.size();
        Check(CombinedCaption(0).empty(),"Ordinary coordinate links need no soul label");
        Check(Has(147,"Sac : ?")&&Has(147,"Ocre : ?"),"Unread state must not imply missing possession or validation");
        Check(CombinedCaption(147,true)==" · Sac : ? · Ocre : ?","Unknown inventory must retain a neutral caption, not absence red");
        Items({Item(33684),Item(33914,328),Item(33682,327,0),Item(147,1)});
        PumpFresh();FinishScan();
        Check(Has(147,"Âme en sac")&&Has(147,"Ocre à faire"),"Owned modern boss soul must remain independent from unvalidated objective");
        Check(Has(928,"Pas en sac")&&Has(928,"Ocre validé"),"Validated objective must not imply still owning its soul");
        Check(CombinedCaption(147,true)==" · <color=#8DDB91>Âme en sac</color> · <color=#F18C8C>Ocre à faire</color>",
            "Owned soul must be green independently of red unvalidated Ocre objective");
        Check(CombinedCaption(928,true)==" · <color=#F18C8C>Pas en sac</color> · <color=#8DDB91>Ocre validé</color>",
            "Absent soul must be red independently of green validated Ocre objective");
        Check(Has(2477,"Âme en sac")&&Has(2477,"Ocre à faire"),"Future archmonster objective must use exact ID and separate possession");
        Check(Has(2468,"Pas en sac"),"One archmonster must not count as a different archmonster");
        for (const auto part : {ChatSoulPart::Inventory, ChatSoulPart::Ocre}) {
            Check(ChatSoulCaption(999999,part).empty() && ChatSoulCaption(999999,part,true).empty(),
                "Monsters outside Ocre must have neither bag nor quest badges");
            Check(ChatSoulDetail(999999,part).empty(),"Hidden outside-Ocre badges must have no tooltip");
        }
        Items({FullStone(999999)});PumpFresh();FinishScan();
        Check(chat_souls::state.owned.contains(999999) && CombinedCaption(999999,true).empty(),
            "Owning an unrelated monster soul must not restore either hidden badge");
        Items({Item(33684),Item(33914,328),Item(33682,327,0),Item(147,1)});PumpFresh();FinishScan();
        Check(ChatSoulDetail(147,ChatSoulPart::Inventory).find("banque")!=std::string::npos,"Tooltip must explain bag-only scope");
        Check(ChatSoulDetail(147,ChatSoulPart::Inventory).find("<color=")==std::string::npos,"Item status tooltip stays plain text");
        Check(ChatSoulDetail(147,ChatSoulPart::Inventory).find("Ocre")==std::string::npos&&
            ChatSoulDetail(147,ChatSoulPart::Ocre).find("banque")==std::string::npos&&
            ChatSoulDetail(147,ChatSoulPart::Ocre).find("reste à valider")!=std::string::npos,
            "Inventory and quest details must be distinct and limited to the hovered state");

        Items({OtherEffectItem(),FullStone(2468),Item(33684,327,1,1),Item(33682,327,1,31)});PumpFresh();FinishScan();
        Check(Has(2468,"Âme en sac")&&Has(147,"Pas en sac")&&Has(928,"Âme en sac"),"Legacy effects, quantity and supported inventory positions must be respected");
        Check(CombinedCaption(147,true).find("<color=#F18C8C>Pas en sac</color>")!=std::string::npos&&
            CombinedCaption(928,true).find("<color=#8DDB91>Âme en sac</color>")!=std::string::npos,
            "Possession color must follow removal and acquisition in fresh inventory snapshots");
        Set(progress,"dtbs",864);PumpFresh();FinishScan();
        Check(Has(147,"Ocre validé"),"Earlier steps must stay validated after leaving their objective list");
        Set(progress,"dtbs",863);Get(objectives).flags[2267]=true;PumpFresh();FinishScan();
        Check(Has(147,"Ocre validé"),"Current objective server confirmation must refresh");
        Check(ChatSoulDetail(147,ChatSoulPart::Ocre).find("déjà été validé")!=std::string::npos&&
            ChatSoulDetail(147,ChatSoulPart::Ocre).find("<color=")==std::string::npos,"Validated quest tooltip must describe only the current state in plain text");
        Check(CombinedCaption(147,true).ends_with(" · <color=#8DDB91>Ocre validé</color>"),"Objective validation must switch the quest label to green");
        Get(objectives).flags.erase(2267);PumpFresh();FinishScan();Check(Has(147,"Ocre : ?"),"Missing current objective is unknown, not false");
        Check(CombinedCaption(147,true).ends_with("</color> · Ocre : ?"),"Missing objective data must clear the previous quest color");
        Get(active).objects.clear();PumpFresh();FinishScan();Check(Has(147,"Ocre : ?"),"Unavailable quest must not claim unstarted or unvalidated");
        Set(finished,"_version",1);PumpFresh();FinishScan();
        Check(Has(147,"Ocre non commencée")&&Has(2477,"Ocre non commencée")&&Has(928,"Âme en sac"),
            "Received snapshot without Ocre must show not-started independently from soul possession");
        Check(CombinedCaption(147,true).ends_with(" · <color=#F18C8C>Ocre non commencée</color>"),"Confirmed not-started quest must be red");
        const auto logsBefore=logs.size();PumpFresh();FinishScan();Check(logs.size()==logsBefore,"Unchanged quest reason must not spam the log");
        Get(active).objects[439]=progress;Get(objectives).flags[2267]=false;PumpFresh();FinishScan();
        Check(Has(147,"Ocre à faire"),"Accepting the quest must replace not-started state using current native progress");
        Set(progress,"dtbs",0);PumpFresh();FinishScan();
        Check(Has(147,"Ocre : ?"),"Known active quest without its step data must never be labelled not-started");
        Get(active).objects.clear();Set(finished,"_version",0);PumpFresh();FinishScan();
        Check(Has(147,"Ocre : ?"),"New empty snapshot must clear a previously known not-started state");
        Get(finished).flags[439]=true;PumpFresh();FinishScan();Check(Has(2477,"Ocre validé"),"Completed quest validates all its monster objectives");
        Get(finished).flags.clear();Set(finished,"_version",1);PumpFresh();FinishScan();
        fail="TryGetValue";PumpFresh();fail.clear();Check(Has(147,"Ocre : ?"),"Failed quest read must clear not-started state");
        PumpFresh();FinishScan();Check(Has(147,"Ocre non commencée"),"Received snapshot must recover after read failure");
        owner.cached.reset(Make("OtherCharacter"));Check(Has(147,"Ocre : ?"),"Character change must invalidate not-started status immediately");
        Get(finished).flags.clear();quests=nullptr;TickChatSoulStatus(true);Check(Has(147,"Ocre : ?"),"Lost quest service must clear stale positive status");

        std::vector<Obj> many(400,Item(50000,1));many.back()=Item(33684);Items(std::move(many));
        RestoreChatSoulStatus();moves=0;PumpFresh();
        Check(moves<=128 && chat_souls::state.iterator.get() && Has(147,"Sac : ?"),"Large inventory scan must yield without publishing a false absence");
        Get(Get(content).fields["_version"]).scalar++;TickChatSoulStatus(true);
        Check(!chat_souls::state.iterator.get()&&Has(147,"Sac : ?"),"Mutation invalidates the partial snapshot and releases iterator");
        PumpFresh();FinishScan();Check(Has(147,"Âme en sac"),"Stable subsequent scan must recover");
        fail="get_Current";PumpFresh();fail.clear();Check(Has(147,"Âme en sac"),"A read failure keeps the last complete scan (never a false absence) and is retried");
        PumpFresh();FinishScan();Check(Has(147,"Âme en sac"),"Read failure must remain recoverable");
        // Marketplace: the game refreshes the inventory content faster than a
        // scan completes; every interrupted scan keeps the last complete result.
        for(int i=0;i<3;++i){PumpFresh();Get(Get(content).fields["_version"]).scalar++;TickChatSoulStatus(true);Check(!chat_souls::state.iterator.get()&&Has(147,"Âme en sac"),"Interrupted scans keep the last complete result instead of falling back to unknown");}
        PumpFresh();FinishScan();Check(Has(147,"Âme en sac"),"Scans resume after the interruptions");
        owner.cached.reset(Make("NextCharacter"));Check(Has(147,"Sac : ?"),"Character change invalidates cached possession immediately");
        Items({});PumpFresh();FinishScan();Check(Has(147,"Pas en sac"),"Next character must not inherit previous inventory");
        service=nullptr;TickChatSoulStatus(true);Check(Has(147,"Sac : ?"),"Unavailable inventory service is unknown");
        owner.stopped=true;TickChatSoulStatus(true);RestoreChatSoulStatus();
        Check(handles.size()==baseline && disposed>0,"Cleanup must release all reader roots and iterators");
        owner.cached.reset();owner.compositor.reset();Check(handles.empty(),"Fixture GC roots leaked");module=nullptr;
        std::cout<<"PASS: read-only soul inventory, exact identity, legacy effects, independent Ocre states, bounded scanning, mutation and session cleanup\n";
        return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
