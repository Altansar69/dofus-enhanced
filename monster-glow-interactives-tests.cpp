#include "monster-glow-interactives.hpp"
#include "module.hpp"
#include "features.hpp"
#include "options-ui.hpp"
#include <array>
#include <cstring>
#include <functional>
#include <iostream>
#include <map>
#include <memory>
#include <limits>

namespace fake {
using namespace managed;
void Check(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
struct Class { std::string name; };
struct MethodInfo { std::string name; int argc; };
struct Object {
    alignas(16) std::array<unsigned char,0x400> bytes{};
    Class* klass{};
    int64_t value=0;
    int count=-1, version=0, cursor=0;
    bool exists=true, enabled=true, active=true, ready=true;
    Obj gameObject{};
    std::vector<Obj> items;
    std::map<int64_t,Obj> byId;
};
std::map<std::string,std::unique_ptr<Class>> classes;
std::vector<std::unique_ptr<Object>> objects;
std::vector<std::unique_ptr<MethodInfo>> methods;
std::map<GCHandle,Obj> handles;
GCHandle next=0xE65400000000ULL;
Obj service{}, mapService{}, map{}, registry{};
std::function<void()> onItem;
int listReads=0, enumerators=0, disposed=0;
bool failList=false;
std::map<std::string,size_t> offsets{
    {"dhbg",0x40},{"dhbx",0xC8},{"dhat",0x10},{"dhau",0x18},{"dhav",0x20},
    {"dcan",0x10},{"dcap",0x18},{"dham",0x20},{"<dhap>k__BackingField",0x30},
    {"dhaa",0x10},{"<dhad>k__BackingField",0x20},{"dhcd",0x18},{"fhro",0xA8},
    {"fhtg",0x1E8},{"colorModifier",0x34},{"meshRenderer",0x20},{"id",0x18}};
Class* Type(const std::string& name) {
    auto& c=classes[name]; if (!c) c=std::make_unique<Class>(Class{name}); return c.get();
}
Object& Get(Obj o) { Check(o!=nullptr,"Unexpected null object"); return *static_cast<Object*>(o); }
Obj Make(const char* name) {
    auto o=std::make_unique<Object>(); o->klass=Type(name); auto ptr=o.get(); objects.push_back(std::move(o)); return ptr;
}
template<class T> void Put(Obj o,size_t offset,T v) { std::memcpy(Get(o).bytes.data()+offset,&v,sizeof(v)); }
template<class T> Obj Box(T v) { auto o=Make("Box"); std::memcpy(&Get(o).value,&v,sizeof(v)); return o; }
void Init() {
    api.object_get_class=[](Obj o)->Klass { return Get(o).klass; };
    api.object_unbox=[](Obj o)->void* { return &Get(o).value; };
    api.class_is_assignable_from=[](Klass a,Klass b) { return a==b; };
    api.gchandle_new=[](Obj o,bool)->GCHandle { handles[++next]=o; return next; };
    api.gchandle_get_target=[](GCHandle h)->Obj { return handles.at(h); };
    api.gchandle_free=[](GCHandle h) { Check(handles.erase(h)==1,"Invalid handle release"); };
    api.class_get_field_from_name=[](Klass,const char* n)->void* { return &offsets.at(n); };
    api.field_get_offset=[](void* f)->size_t { return *static_cast<size_t*>(f); };
}
}
namespace managed {
Api api{};
Klass Api::Class(const char*,const char*,const char* name) const { return fake::Type(name); }
void* Api::FieldAddress(Obj o,const char* name) const {
    fake::Check(std::string(name)=="_version","Unexpected dynamic field");
    return &fake::Get(o).version;
}
Method Api::FindVerified(Klass,const char* name,int argc,uintptr_t rva) const {
    const std::map<std::string,uintptr_t> expected{
        {"qyb",0x2629090},{"qyg",0x2629380},{"xkj",0x18389C0},{"xkd",0x11ECD00},
        {"dayr",0x782BE0},{"op_Implicit",0x5439190},{"get_enabled",0x53F2300},
        {"get_gameObject",0x5429D00},{"get_activeInHierarchy",0x5432080}};
    fake::Check(expected.at(name)==rva,"Unverified native method");
    auto m=std::make_unique<fake::MethodInfo>(fake::MethodInfo{name,argc}); auto ptr=m.get();
    fake::methods.push_back(std::move(m)); return ptr;
}
Obj Api::Call(Obj o,const char* name,std::initializer_list<void*> args) const {
    using namespace fake;
    auto& v=Get(o); std::string m=name;
    if (m=="get_Count") return Box(v.count>=0?v.count:static_cast<int>(v.items.size()));
    if (m=="get_Item") {
        auto result=v.items.at(*static_cast<int*>(*args.begin())); if (onItem) onItem(); return result;
    }
    if (m=="GetEnumerator") { auto it=Make("Iterator"); Get(it).items=v.items; ++enumerators; return it; }
    if (m=="MoveNext") return Box(v.cursor++<static_cast<int>(v.items.size()));
    if (m=="get_Current") return v.items.at(v.cursor-1);
    if (m=="Dispose") { ++disposed; return nullptr; }
    throw std::runtime_error("Unexpected managed call");
}
Obj Api::Invoke(Method method,Obj o,std::initializer_list<void*> args) const {
    using namespace fake;
    const auto& m=*static_cast<const MethodInfo*>(method); std::vector<void*> p(args);
    Check(m.argc==static_cast<int>(p.size()),"Method ABI arity");
    if (m.name=="op_Implicit") return Box(p[0] && Get(p[0]).exists);
    if (m.name=="xkj") return Box(Get(o).ready);
    if (m.name=="xkd") return map;
    if (m.name=="qyb") { ++listReads; if(failList) throw std::runtime_error("List failure"); return registry; }
    if (m.name=="qyg") {
        auto it=Get(registry).byId.find(*static_cast<int64_t*>(p[0]));
        *static_cast<Obj*>(p[1])=it==Get(registry).byId.end()?nullptr:it->second;
        return Box(*static_cast<Obj*>(p[1])!=nullptr);
    }
    if (m.name=="get_enabled") return Box(Get(o).enabled);
    if (m.name=="get_gameObject") return Get(o).gameObject;
    if (m.name=="get_activeInHierarchy") return Box(Get(o).active);
    if (m.name=="dayr") return o;
    throw std::runtime_error("Unexpected managed invocation");
}
}
Module* module=nullptr;
Module::Module(std::filesystem::path p):folder(std::move(p)) {}
bool Module::InCombat() const { return fight.get() && managed::Read<bool>(fight.get(),0xB0); }
managed::Obj Module::FindService(const char* name) const {
    return std::string(name)=="fro"?fake::service:std::string(name)=="fpm"?fake::mapService:nullptr;
}
void Log(const std::string&) {}
namespace {
using namespace fake;
using namespace monsterGlow;
struct Fixture {
    Module state{std::filesystem::path{}};
    Fixture() {
        Check(handles.empty(),"Prior snapshot leaked handles");
        module=&state; state.servicesReady=true; state.settings.glowInteractives=true;
        state.compositor.reset(Make("Compositor")); state.fight.reset(Make("Fight"));
        service=Make("wp"); mapService=Make("epf"); map=Make("ClientMapData"); registry=Make("Dictionary");
        Put(service,0x40,mapService); Put(service,0xC8,registry); Put(map,0x18,42);
        onItem={}; failList=false; listReads=enumerators=disposed=0;
    }
    ~Fixture() { module=nullptr; }
    Obj Renderer() { auto r=Make("MeshRenderer"); Get(r).gameObject=Make("GameObject"); return r; }
    Obj Animated() {
        auto a=Make("Animated"), animator=Make("Animator2D");
        Put(a,0xA8,animator); Put(animator,0x40,1.0f); Put(a,0x1E8,Renderer()); return a;
    }
    Obj Add(int id,bool moving=false) {
        auto wrapper=Make("wm"), data=Make("dq"), skills=Make("List"), focus=Make(moving?"wk":"wl");
        Put(wrapper,0x10,int64_t{id}); Put(wrapper,0x18,focus); Put(wrapper,0x20,data);
        Put(data,0x10,id); Put(data,0x18,skills); Get(skills).items.push_back(Make("Skill"));
        if (moving) {
            auto a=Make("wq"); Put(a,0x18,Animated()); Put(focus,0x10,a); Put(focus,0x20,static_cast<uint32_t>(id));
        } else {
            auto e=Make("MapElement"); Put(e,0x20,Renderer()); Put(focus,0x20,e); Put(focus,0x30,static_cast<uint32_t>(id));
        }
        Get(registry).items.push_back(wrapper); Get(registry).byId[id]=wrapper;
        return wrapper;
    }
    Obj Focus(Obj w) { return Read<Obj>(w,0x18); }
    Obj StaticRenderer(Obj w) { return Read<Obj>(Read<Obj>(Focus(w),0x20),0x20); }
    Obj Root(Obj w) { return Read<Obj>(Read<Obj>(Focus(w),0x10),0x18); }
};
void Selection() {
    Fixture f; auto a=f.Add(1), b=f.Add(2,true); auto child=f.Animated();
    Get(f.Root(b)).items={child,f.Root(b)};
    Get(registry).items.push_back(a);
    {auto r=ReadGlowInteractives(); Check(r.valid && r.targets.size()==2 && r.interactiveCount==2,"Static and animated objects"); Check(r.targets[0].interactive && r.targets[1].renderers.size()==2,"Composite object renderer identity");}
    Check(enumerators==disposed,"Composite traversal must dispose iterators");
    f.state.settings.glowInteractives=false; int before=listReads;
    Check(ReadGlowInteractives().targets.empty() && listReads==before,"Disabled option does no native registry scan");
    f.state.settings.glowInteractives=true; Put(f.state.fight.get(),0xB0,true);
    Check(ReadGlowInteractives().targets.empty() && listReads==before,"Combat excludes map interactions");
}
void Visibility() {
    Fixture f; f.Add(1);
    auto empty=f.Add(2); Get(Read<Obj>(Read<Obj>(empty,0x20),0x18)).items.clear();
    auto disabled=f.Add(3); Get(f.StaticRenderer(disabled)).enabled=false;
    auto hidden=f.Add(4); Get(Get(f.StaticRenderer(hidden)).gameObject).active=false;
    auto destroyed=f.Add(5); Get(f.StaticRenderer(destroyed)).exists=false;
    auto mismatch=f.Add(6); Put(f.Focus(mismatch),0x30,uint32_t{123});
    auto unknown=f.Add(7); Get(f.Focus(unknown)).klass=Type("UnsupportedDisplayable");
    auto faded=f.Add(8,true); Put(Read<Obj>(f.Root(faded),0xA8),0x40,0.0f);
    auto invalid=f.Add(9,true); Put(Read<Obj>(f.Root(invalid),0xA8),0x40,std::numeric_limits<float>::quiet_NaN());
    auto r=ReadGlowInteractives(); Check(r.valid && r.targets.size()==1 && r.targets[0].id==1,"Inactive, unknown, unavailable or invisible objects excluded");
}
void Ownership() {
    Fixture f; auto a=f.Add(1);
    Get(registry).byId[1]=Make("wm");
    Check(ReadGlowInteractives().targets.empty(),"Replaced object cannot keep old renderer");
    Get(registry).byId[1]=a;
    onItem=[] { ++Get(registry).version; };
    {auto r=ReadGlowInteractives(); Check(!r.valid && r.targets.empty(),"Registry mutation invalidates full snapshot");}
    onItem=[] { map=Make("ClientMapData"); Put(map,0x18,43); };
    {auto r=ReadGlowInteractives(); Check(!r.valid && r.targets.empty(),"Map replacement invalidates full snapshot");}
    onItem={}; Get(mapService).ready=false;
    Check(!ReadGlowInteractives().valid,"Loading maps are excluded");
    Get(mapService).ready=true; Get(registry).count=513; int before=listReads;
    Check(!ReadGlowInteractives().valid && listReads==before,"Bound registry before native list allocation");
    Get(registry).count=-1; failList=true; Check(!ReadGlowInteractives().valid,"Native failure cannot retain targets");
}
void Bounds() {
    Fixture f; auto a=f.Add(1,true);
    for(int i=0;i<32;++i) Get(f.Root(a)).items.push_back(f.Animated());
    Check(ReadGlowInteractives().targets.empty() && enumerators==disposed,"Bound composite children and dispose iterator");
    Get(registry).items.clear(); Get(registry).byId.clear();
    for(int i=1;i<=140;++i) f.Add(i);
    auto r=ReadGlowInteractives(); Check(r.valid && r.targets.size()==128,"Bound accepted target count");
}
}
int main() {
    try {
        fake::Init(); monsterGlow::InstallGlowInteractives();
        Selection(); Visibility(); Ownership(); Bounds();
        fake::Check(fake::handles.empty(),"Snapshots leaked roots");
        std::cout<<"PASS: native interactive selection, visibility, categories, current-map ownership and bounds\n"; return 0;
    } catch(const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; }
}
