#include "core/module.hpp"
#include "appearance/features.hpp"
#include "ui/options-ui.hpp"
#include <iostream>
#include <map>
#include <memory>
#include <cstring>
#include <algorithm>
#include "maps/map-preload.cpp"

namespace fake {
using namespace managed;
struct Type { std::string name; };
struct Node;
struct Payload { Node* owner; int64_t number=0; };
struct Node {
    Type* type; Payload payload{this};
    std::string text; std::map<std::string,Obj> fields; std::vector<Obj> items;
    bool valid=true,done=false; int state=1, references=1; Obj output=nullptr;
};
std::map<std::string,Type> types;
std::vector<std::unique_ptr<Node>> heap;
std::vector<Obj> operations;
std::vector<std::string> keys,logs;
std::map<uintptr_t,std::string> methods;
Obj input,container,cached,implementation,rm,textureSetting;
bool combat=false,unavailable=false,ambiguous=false,failRelease=false,wrongMap=false;
int liveRoots=0, released=0;
void Check(bool b,const char* s) { if(!b)throw std::runtime_error(s); }
Node& Get(Obj o) { Check(o!=nullptr,"Null fake object");return *static_cast<Node*>(o); }
Type* TypeOf(std::string name) { auto& t=types[name];t.name=name;return &t; }
Obj Make(std::string type) { auto n=std::make_unique<Node>();n->type=TypeOf(type);auto o=n.get();heap.push_back(std::move(n));return o; }
Obj Box(int64_t value) { auto o=Make("Box");Get(o).payload.number=value;return o; }
Obj List(std::vector<Obj> items={}) { auto o=Make("List");Get(o).items=std::move(items);return o; }
Obj Map(int id,int count=80) {
    auto m=Make("ClientMapData");Get(m).fields["id"]=Box(id);
    std::vector<Obj> elements;
    for(int i=0;i<count;++i) {auto e=Make("ClientMapElement");Get(e).fields["gfxId"]=Box(i/2);elements.push_back(e);}
    auto group=Make("ClientMergedMapElements");Get(group).fields["mapElements"]=List(elements);
    Get(m).fields["backgroundMapElements"]=List({group});
    Get(m).fields["middlegroundMapElements"]=List();Get(m).fields["foregroundMapElements"]=List();return m;
}
void Complete() { for(auto o:operations) Get(o).done=true; }
void Destination(int64_t id) {Get(input).fields["dsvp"]=Box(id);}
void Setup() {
    api.gchandle_new=[](Obj o,bool)->GCHandle{++liveRoots;return reinterpret_cast<GCHandle>(o);};
    api.gchandle_get_target=[](GCHandle h)->Obj{return reinterpret_cast<Obj>(h);};
    api.gchandle_free=[](GCHandle){--liveRoots;};
    api.object_get_class=[](Obj o)->Klass{return Get(o).type;};
    api.object_unbox=[](Obj o)->void*{return Get(o).type->name=="Box"?static_cast<void*>(&Get(o).payload.number):static_cast<void*>(&Get(o).payload);};
    api.class_get_type=[](Klass c)->const void*{return c;};
    api.type_get_object=[](const void* c)->Obj{return const_cast<void*>(c);};
    api.string_new=[](const char* s)->Obj{auto o=Make("String");Get(o).text=s;return o;};
    input=Make("fce");container=Make("Container");cached=Make("Cached");implementation=Make("AddressablesImpl");rm=Make("ResourceManager");
    textureSetting=Make("Setting");Get(textureSetting).fields["value"]=Box(2);Get(container).fields["currentTexturePack"]=textureSetting;
    Destination(-1);
}
}
namespace managed {
Api api{}; uintptr_t gameBase=0;
Klass Api::Class(const char*,const char*,const char* n)const{return fake::TypeOf(n);}
Method Api::FindVerified(Klass,const char* n,int,uintptr_t rva)const{fake::methods[rva]=n;return reinterpret_cast<Method>(rva);}
Obj Api::StaticField(Klass,const char* n)const {fake::Check(std::string(n)=="m_AddressablesInstance","Do not mutate native map caches");return fake::implementation;}
Obj Api::Field(Obj o,const char* n)const {return fake::Get(o).fields.at(n);}
Obj Api::Call(Obj o,const char* n,std::initializer_list<void*> args)const {
    using namespace fake;
    std::string name=n;
    if(name=="get_Count")return Box(Get(o).items.size());
    if(name=="get_Item")return Get(o).items.at(*static_cast<int*>(*args.begin()));
    if(name=="get_dofusOptionsContainer")return container;
    if(name=="get_property")return Get(o).fields.at("value");
    throw std::runtime_error("Unexpected call "+name);
}
Obj Api::Invoke(Method m,Obj self,std::initializer_list<void*> args)const {
    using namespace fake;auto n=methods.at(reinterpret_cast<uintptr_t>(m));auto a=args.begin();
    if(n=="GetResourceLocations") {
        Check(self==implementation&&args.size()==3,"Location ABI");
        auto key=Get(a[0]).text;auto location=Make("Location");Get(location).text=key;
        auto type=static_cast<Type*>(a[1]);
        Check((key.starts_with("map_")&&type->name=="ClientMapData")||(key.starts_with("2x-")&&type->name=="Sprite"),"Exact resource type and active texture pack");
        *static_cast<Obj*>(a[2])=List(unavailable?std::vector<Obj>{}:ambiguous?std::vector<Obj>{location,location}:std::vector<Obj>{location});return Box(!unavailable);
    }
    if(n=="get_ResourceManager")return rm;
    if(n=="ProvideResource") {
        Check(self==rm&&args.size()==3&&*static_cast<bool*>(a[2]),"ProvideResource ABI/release-on-failure");
        auto key=Get(a[0]).text;keys.push_back(key);auto h=Make("AsyncOperationHandle");
        Get(h).output=key.starts_with("map_")?Map(wrongMap?999:std::stoi(key.substr(4))):Make("Sprite");
        operations.push_back(h);return h;
    }
    if(n=="Release") {
        Check(self==rm&&args.size()==1,"Release only own handle via retained manager");
        if(failRelease)throw std::runtime_error("Transient release failure");
        auto& op=*static_cast<Payload*>(a[0])->owner;
        Check(op.type->name=="AsyncOperationHandle"&&op.references>0,"Release receives unboxed handle once");
        --op.references;op.valid=false;++released;return nullptr;
    }
    auto& op=*static_cast<Payload*>(self)->owner;
    Check(op.type->name=="AsyncOperationHandle","Handle methods must receive unboxed struct, never object header");
    if(n=="IsValid")return Box(op.valid);
    if(n=="get_IsDone")return Box(op.done);
    if(n=="get_Status")return Box(op.state);
    if(n=="get_Result")return op.output;
    throw std::runtime_error("Unexpected invoke "+n);
}
}
Module* module=nullptr;
Module::Module(std::filesystem::path) {}
bool Module::InCombat()const{return fake::combat;}
managed::Obj Module::FindService(const char* n)const {fake::Check(std::string(n)=="fnv","Observe destination only");return fake::input;}
void Log(const std::string& s){fake::logs.push_back(s);}
int main() {
 using namespace fake;
 try {
    Setup();InstallMapPreload();Module owner(std::filesystem::path{});module=&owner;
    owner.cached.reset(cached);owner.ready=true;owner.servicesReady=true;
    owner.settings.preloadNextMap=false;
    Destination(123);TickAt(100);Check(keys.empty(),"Explicit opt-out does no preload work");
    owner.settings.preloadNextMap=true;TickAt(200);
    Check(keys==std::vector<std::string>{"map_123.asset"}&&assets.size()==1,"Begin real async map load while destination pending");
    TickAt(300);Check(keys.size()==1,"Never block or duplicate pending map operation");
    Complete();TickAt(400);Check(assets.size()<=9&&assets.size()>1,"Bound sprite concurrency");
    for(ULONGLONG t=500;t<4000;t+=100) {Complete();TickAt(t);}
    Check(reportedReady&&keys.size()==41,"Load map plus deduplicated static sprites");
    for(auto o:operations)++Get(o).references; // Independent native loader owners.
    Destination(-1);TickAt(4000);Check(assets.size()==41,"Keep warm refs while actual arrival loads");
    Destination(124);TickAt(4100);Check(released==41&&keys.back()=="map_124.asset","New destination releases only prior owned handles");
    for(size_t i=0;i<41;++i)Check(Get(operations[i]).references==1,"Native loader refs survive speculative cache cleanup");
    Destination(-1);TickAt(34200);Check(!MapPreloadCleanupPending(),"Cancelled destination expires");
    auto before=keys.size();TickAt(35000);Check(keys.size()==before,"No repeated preload without fresh intent");
    Destination(125);unavailable=true;TickAt(35100);Check(!MapPreloadCleanupPending(),"Unloaded catalog falls back without fetching catalogs");unavailable=false;
    Destination(126);ambiguous=true;TickAt(35200);Check(!MapPreloadCleanupPending(),"Ambiguous key must not choose wrong resources");ambiguous=false;
    Destination(127);combat=true;TickAt(35300);Check(keys.size()==before,"Combat must not prefetch");combat=false;
    TickAt(35400);Check(MapPreloadCleanupPending(),"Restart after combat with real destination");
    failRelease=true;RestoreMapPreload();Check(MapPreloadCleanupPending(),"Failed cleanup retains ownership");failRelease=false;RestoreMapPreload();Check(!MapPreloadCleanupPending(),"Cleanup retries and drains");
    wrongMap=true;Destination(128);TickAt(35500);Complete();bool rejected=false;
    try{TickAt(35600);}catch(const std::exception&){rejected=true;}Check(rejected,"Wrong map result must not preload its graphics");wrongMap=false;RestoreMapPreload();
    Destination(129);TickAt(35700);Complete();Get(operations.back()).output=Map(129,1000);
    for(ULONGLONG t=35800;t<43000;t+=100){Complete();TickAt(t);}
    Check(reportedReady&&assets.size()==501&&std::string(scanStop)=="complete","Preload all500 distinct sprites, beyond old64 limit");
    Destination(131);TickAt(43000);Complete();Get(operations.back()).output=Map(131,1400);
    for(ULONGLONG t=43100;t<51000;t+=100){
        TickAt(t);size_t pendingCount=0;for(auto& asset:assets)if(!asset.done)++pendingCount;
        Check(pendingCount<=8,"Eight pending resources maximum with larger cache");Complete();
    }
    Check(reportedReady&&assets.size()==513&&std::string(scanStop)=="sprite-limit","Cap larger maps at512 retained sprites");
    Check(std::any_of(logs.begin(),logs.end(),[](const auto& log){return log.find("uniqueSprites=512")!=std::string::npos&&log.find("sprite-limit")!=std::string::npos;}),"Report capped coverage explicitly");
    Destination(130);TickAt(51100);auto cancelledCount=keys.size();Destination(-1);Complete();TickAt(51200);
    Check(keys.size()==cancelledCount&&!scanData.get()&&sprites.empty(),"Cancelled or sent native request must stop new sprite work");
    owner.settings.preloadNextMap=false;TickAt(51300);Check(!MapPreloadCleanupPending(),"Disabling frees own resources");
    owner.cached.reset();module=nullptr;Check(liveRoots==0,"No managed GC roots retained after stop");
    std::cout<<"PASS: async destination prefetch, correct map/pack, bounded work, deduplication, cache ownership, expiry and cleanup retry\n";
 }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
