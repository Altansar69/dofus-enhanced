#include "monster-glow-entities.hpp"
#include "module.hpp"
#include "features.hpp"
#include "options-ui.hpp"
#include <array>
#include <cmath>
#include <cstring>
#include <functional>
#include <iostream>
#include <limits>
#include <map>
#include <memory>
#include <unordered_map>

// Production reader with synthetic objects only. No client DLL or process is loaded.
namespace fake {
using namespace managed;
void Check(bool value, const char* text) { if (!value) throw std::runtime_error(text); }
struct Class { std::string name; };
struct MethodInfo { std::string name; int argc; };
struct Object {
    alignas(16) std::array<unsigned char, 0x400> bytes{};
    Class* klass{};
    int64_t value=0;
    int flags=9, countOverride=-1, cursor=0, npcId=0;
    int8_t visibility=2;
    bool visibilityKnown=true, alive=true, aliveKnown=true, exists=true, enabled=true, active=true;
    bool throwCurrent=false, missingIterator=false;
    Obj gameObject{};
    std::vector<Obj> items;
    std::map<int64_t, Obj> byId;
};
std::map<std::string, std::unique_ptr<Class>> classes;
std::vector<std::unique_ptr<Object>> objects;
std::vector<std::unique_ptr<MethodInfo>> methods;
std::map<GCHandle, Obj> handles;
GCHandle nextHandle=0xF12340000000ULL;
Obj currentInfo{}, currentWorld{};
std::function<void()> onItem;
std::vector<std::string> logs;
int enumerators=0, disposed=0;
bool failList=false;
Class* Type(const std::string& name) {
    auto& value=classes[name]; if (!value) value=std::make_unique<Class>(Class{name}); return value.get();
}
Object& Get(Obj obj) { Check(obj!=nullptr,"Null fake glow entity"); return *static_cast<Object*>(obj); }
Obj Make(const char* name) {
    auto value=std::make_unique<Object>(); value->klass=Type(name);
    auto result=value.get(); objects.push_back(std::move(value)); return result;
}
template<class T> void Put(Obj obj, size_t offset, T value) {
    Check(offset+sizeof(T)<=Get(obj).bytes.size(),"Fake glow field overflow");
    std::memcpy(Get(obj).bytes.data()+offset,&value,sizeof(value));
}
template<class T> Obj Box(T value) {
    auto object=Make("Box"); std::memcpy(&Get(object).value,&value,sizeof(value)); return object;
}
void Initialize() {
    api.object_get_class=[](Obj obj)->Klass { return Get(obj).klass; };
    api.object_unbox=[](Obj obj)->void* { return &Get(obj).value; };
    api.gchandle_new=[](Obj obj,bool)->GCHandle { handles[++nextHandle]=obj; return nextHandle; };
    api.gchandle_get_target=[](GCHandle id)->Obj { Check(handles.contains(id),"Invalid glow handle"); return handles.at(id); };
    api.gchandle_free=[](GCHandle id) { Check(handles.erase(id)==1,"Glow handle double free"); };
}
}
namespace managed {
Api api{};
Klass Api::Class(const char*,const char*,const char* name) const { return fake::Type(name); }
Method Api::FindVerified(Klass,const char* name,int argc,uintptr_t rva) const {
    static const std::map<std::string,uintptr_t> expected={
        {"baml",0x19579E0},{"bamm",0x1957A70},{"bajh",0x1951440},{"bajs",0x19518F0},
        {"baks",0x1952330},{"bakd",0x1951D50},{"bajy",0x1951B60},{"vkq",0x27695E0},{"zzm",0x190FE40},
        {"qfv",0x260F8D0},{"rgq",0x26172B0},{"dayr",0x782BE0},{"op_Implicit",0x5439190},
        {"get_enabled",0x53F2300},{"get_gameObject",0x5429D00},{"get_activeInHierarchy",0x5432080}};
    fake::Check(expected.at(name)==rva,"Unexpected glow method RVA");
    auto value=std::make_unique<fake::MethodInfo>(fake::MethodInfo{name,argc});
    auto result=value.get(); fake::methods.push_back(std::move(value)); return result;
}
Obj Api::Call(Obj self,const char* name,std::initializer_list<void*> args) const {
    auto& object=fake::Get(self); std::string method=name;
    if (method=="get_Count") return fake::Box(object.countOverride<0?static_cast<int>(object.items.size()):object.countOverride);
    if (method=="get_Item") {
        auto result=object.items.at(*static_cast<int*>(*args.begin())); if(fake::onItem) fake::onItem(); return result;
    }
    if (method=="GetEnumerator") {
        if (object.missingIterator) return nullptr;
        auto result=fake::Make("Enumerator"); fake::Get(result).items=object.items;
        fake::Get(result).throwCurrent=object.throwCurrent; ++fake::enumerators; return result;
    }
    if (method=="MoveNext") return fake::Box(object.cursor++<static_cast<int>(object.items.size()));
    if (method=="get_Current") {
        if(object.throwCurrent) throw std::runtime_error("Invalid child iterator");
        return object.items.at(object.cursor-1);
    }
    if (method=="Dispose") { ++fake::disposed; return nullptr; }
    throw std::runtime_error("Unexpected glow managed call");
}
Obj Api::Invoke(Method method,Obj self,std::initializer_list<void*> args) const {
    const auto& m=*static_cast<const fake::MethodInfo*>(method); std::vector<void*> p(args);
    fake::Check(static_cast<int>(p.size())==m.argc,"Glow method arity mismatch");
    if(m.name=="op_Implicit") return fake::Box(p[0] && fake::Get(p[0]).exists);
    auto& object=fake::Get(self);
    if(m.name=="baml") { if(fake::failList) throw std::runtime_error("Roster failed"); return self; }
    if(m.name=="bajh") return fake::Box(object.value);
    if(m.name=="bajs") { int flag=*static_cast<int*>(p[0]); return fake::Box((object.flags&flag)==flag); }
    if(m.name=="bakd") {
        fake::Check(*static_cast<int*>(p[0])==3,"Unexpected alive key");
        *static_cast<bool*>(p[1])=object.alive; return fake::Box(object.aliveKnown);
    }
    if(m.name=="bajy") {
        fake::Check(*static_cast<int*>(p[0])==2,"Unexpected visibility key");
        *static_cast<int8_t*>(p[1])=object.visibility; return fake::Box(object.visibilityKnown);
    }
    if(m.name=="baks") {
        fake::Check(*static_cast<int*>(p[0])==15,"NPC identification must use native NPC data key");
        *static_cast<int*>(p[1])=object.npcId; return fake::Box(object.npcId!=0);
    }
    if(m.name=="bamm" || m.name=="zzm" || m.name=="vkq") {
        auto found=object.byId.find(*static_cast<int64_t*>(p[0]));
        Obj result=found==object.byId.end()?nullptr:found->second;
        if(m.name=="vkq") return result;
        *static_cast<Obj*>(p[1])=result; return fake::Box(result!=nullptr);
    }
    if(m.name=="qfv") return fake::Box(Read<bool>(self,0x382));
    if(m.name=="rgq") return fake::Box(Read<float>(self,0x384));
    if(m.name=="dayr") return self;
    if(m.name=="get_enabled") return fake::Box(object.enabled);
    if(m.name=="get_activeInHierarchy") return fake::Box(object.active);
    if(m.name=="get_gameObject") return object.gameObject;
    throw std::runtime_error("Unexpected glow managed invocation");
}
}
Module* module=nullptr;
Module::Module(std::filesystem::path path):folder(std::move(path)) {}
bool Module::InCombat() const { return servicesReady && fight.get() && managed::Read<bool>(fight.get(),0xB0); }
managed::Obj Module::FindService(const char* name) const {
    return std::string(name)=="fso"?fake::currentInfo:fake::currentWorld;
}
void Log(const std::string& text) { fake::logs.push_back(text); }

namespace {
using namespace fake;
struct Fixture {
    Module state{std::filesystem::path{}};
    Obj info{}, world{}, services{}, context{}, rendered{};
    explicit Fixture(bool combat=true) {
        module=&state; state.servicesReady=true; onItem={}; failList=false; logs.clear(); enumerators=disposed=0;
        state.compositor.reset(Make("Compositor")); state.creatures.reset(Make("exf")); state.fight.reset(Make("fxc"));
        services=Make("fsm"); context=Make("fyb"); rendered=Make("fxw"); info=Make("evv"); world=Make(combat?"ebm":"eun");
        Put(state.creatures.get(),0x10,services); Put(state.fight.get(),0xB0,combat); Put(state.fight.get(),0x18,context);
        Put(context,0x58,info); Put(context,0x68,rendered); Put(rendered,0x150,world); Put(rendered,0x20,services);
        Put(world,0x20,services); currentInfo=info; currentWorld=world;
    }
    ~Fixture() {
        state.compositor.reset(); state.creatures.reset(); state.fight.reset(); module=nullptr;
        Check(handles.empty(),"Glow entities leaked managed references");
        Check(enumerators==disposed,"Glow child enumerator leaked");
    }
    Obj Part(Obj root=nullptr) {
        auto entity=Make("AnimatedEntity"), animator=Make("Animator2D"), renderer=Make("MeshRenderer");
        Put(entity,0x1A8,root?root:entity); Put(entity,0x398,services); Put(entity,0xA8,animator); Put(entity,0x1E8,renderer);
        Put(entity,0x382,true); Put(entity,0x384,1.0f); Put(animator,0x40,1.0f);
        Get(renderer).gameObject=Make("GameObject"); return entity;
    }
    Obj Add(int64_t id) {
        auto data=Make("EntityInfo"); Get(data).value=id; Get(data).flags=state.InCombat()?9:10;
        Get(info).items.push_back(data); Get(info).byId[id]=data;
        auto entity=Part(); Put(entity,0x1C8,id); Get(world).byId[id]=entity; return entity;
    }
    Object& Data(int64_t id) { return Get(Get(info).byId.at(id)); }
};
void CombatAndRoleplay() {
    for(bool combat:{true,false}) {
        Fixture f(combat); auto entity=f.Add(-1234567890123LL);
        auto child=f.Part(entity); Get(entity).items.push_back(child); Get(child).items.push_back(entity);
        Get(f.info).items.push_back(Get(f.info).items.front());
        auto result=monsterGlow::ReadEntities();
        Check(result.valid && result.combat==combat && result.context.get()==f.state.compositor.get(),"Wrong glow context");
        Check(result.targets.size()==1 && result.targets[0].id==-1234567890123LL && result.targets[0].entity.get()==entity,"Monster ID/root mismatch");
        Check(result.targets[0].renderers.size()==2,"Composite renderers not collected or cycle duplicated");
    }
}
void PrivacyAndLife() {
    Fixture f;
    f.Add(1); f.Add(2); f.Data(2).visibility=0;
    f.Add(3); f.Data(3).visibility=1;
    f.Add(4); f.Data(4).visibilityKnown=false;
    f.Add(5); f.Data(5).alive=false;
    f.Add(6); f.Data(6).aliveKnown=false;
    for(int flag:{0x80,0x10,0x20,0x40}) { auto id=100+flag; f.Add(id); f.Data(id).flags|=flag; }
    f.Add(7); f.Data(7).flags=0x81;
    auto result=monsterGlow::ReadEntities();
    Check(result.valid && result.targets.size()==1 && result.targets.front().id==1,"Hidden/dead/nonmonster entity escaped gates");
}
void MonsterFlags() {
    for(bool combat:{true,false}) {
        Fixture f(combat); f.Add(1);
        f.Add(2); f.Data(2).flags=8; // Monster bit alone has no current scene membership.
        f.Add(3); f.Data(3).flags=combat?1:2; // Scene membership alone is not a monster.
        f.Add(4); f.Data(4).flags=combat?10:9; // A different scene's monster is not eligible.
        f.Add(5); f.Data(5).flags=0;
        if(!combat) { f.Add(6); f.Data(6).flags=11; }
        auto result=monsterGlow::ReadEntities();
        Check(result.valid && result.targets.size()==1 && result.targets[0].id==1,"Monster mask accepted partial/wrong scene flags");
    }
}
void NpcSelection() {
    Fixture f(false); f.Add(1); f.Add(2); f.Data(2).flags=2; f.Data(2).npcId=123;
    f.Add(3); f.Data(3).flags=2; // Other roleplay actor without NPC identity.
    f.Add(4); f.Data(4).flags=0x86; f.Data(4).npcId=123; // Player must stay excluded.
    auto hidden=f.Add(5); f.Data(5).flags=2; f.Data(5).npcId=456; Put(hidden,0x382,false);
    {auto r=monsterGlow::ReadEntities(); Check(r.targets.size()==1 && !r.npcCount,"NPC glow defaults off");}
    f.state.settings.glowNpcs=true;
    {auto r=monsterGlow::ReadEntities(); Check(r.targets.size()==2 && r.targets[1].id==2 && r.npcCount==2,"Only visible identified NPCs join monsters");}
    f.state.settings.glowNpcs=false;
    {auto r=monsterGlow::ReadEntities(); Check(r.targets.size()==1,"NPC toggle must immediately remove NPCs");}
}
void ActualRendering() {
    Fixture f; f.Add(1);
    auto hidden=f.Add(2); Put(hidden,0x382,false);
    auto faded=f.Add(3); Put(faded,0x384,0.5f);
    auto alpha=f.Add(4); Put(Read<Obj>(alpha,0xA8),0x40,0.0f);
    auto disabled=f.Add(5); Get(Read<Obj>(disabled,0x1E8)).enabled=false;
    auto inactive=f.Add(6); Get(Get(Read<Obj>(inactive,0x1E8)).gameObject).active=false;
    auto destroyed=f.Add(7); Get(Read<Obj>(destroyed,0xA8)).exists=false;
    auto foreign=f.Add(8); Put(foreign,0x398,Make("OtherServices"));
    auto mismatch=f.Add(9); Put(mismatch,0x1C8,int64_t{10});
    auto invalidAlpha=f.Add(10); Put(Read<Obj>(invalidAlpha,0xA8),0x40,std::numeric_limits<float>::quiet_NaN());
    auto result=monsterGlow::ReadEntities();
    Check(result.valid && result.targets.size()==2 && result.targets[0].id==1 && result.targets[1].id==3,
        "Visible translucent source must pass while non-rendered entities remain excluded");
    Check(result.rosterCount==10 && result.monsterCount==10 && result.matchedRoots==9 &&
          std::string(result.lastRejectedPart)=="animator-alpha","Diagnostics must expose where eligible sources were filtered");
}
void VisibleTransparency() {
    for (bool combat : {false,true}) for (float opacity : {0.7f,0.5f,0.03f}) {
        Fixture f(combat);
        auto entity=f.Add(1), child=f.Part(entity);
        Get(entity).items.push_back(child);
        for (auto part : {entity,child}) {
            Put(part,0x384,opacity);
            Put(Read<Obj>(part,0xA8),0x40,opacity);
        }
        {
            auto result=monsterGlow::ReadEntities();
            Check(result.valid && result.targets.size()==1 && result.targets[0].renderers.size()==2,
                "Visible native-translucent monsters must reach glow rendering");
        }
        Put(entity,0x384,0.0f); Put(Read<Obj>(entity,0xA8),0x40,0.0f);
        {
            auto result=monsterGlow::ReadEntities();
            Check(result.valid && result.targets.empty(),"A transparent root must suppress its entire contour");
        }
        Put(entity,0x384,opacity); Put(Read<Obj>(entity,0xA8),0x40,opacity);
        Put(entity,0x382,false);
        {
            auto result=monsterGlow::ReadEntities();
            Check(result.valid && result.targets.empty(),"Opacity must not override native hidden state");
        }
        Put(entity,0x382,true);
        if (combat) {
            f.Data(1).visibility=1;
            auto result=monsterGlow::ReadEntities();
            Check(result.valid && result.targets.empty(),"Translucent source must not reveal invisible combat entities");
        }
    }
}
void InvalidOrInvisibleOpacity() {
    for (bool animator : {false,true}) for (float opacity : {0.0f,-0.2f,0.01f,0.02f,1.5f,
            std::numeric_limits<float>::quiet_NaN(),std::numeric_limits<float>::infinity()}) {
        Fixture f(false); auto entity=f.Add(1);
        if (animator) Put(Read<Obj>(entity,0xA8),0x40,opacity);
        else Put(entity,0x384,opacity);
        auto result=monsterGlow::ReadEntities();
        Check(result.valid && result.targets.empty(),"Invisible or invalid opacity must not produce a contour");
        Check(std::string(result.lastRejectedPart)==(animator?"animator-alpha":"part-alpha") &&
              (std::isnan(opacity)?std::isnan(result.lastRejectedAlpha):result.lastRejectedAlpha==opacity),
              "Rejected alpha must be diagnosed with its source and actual value");
    }
}
void MountedParts() {
    Fixture f(false); auto entity=f.Add(1);
    auto visible=f.Part(entity), hidden=f.Part(entity), faded=f.Part(entity), tinted=f.Part(entity);
    auto hiddenDescendant=f.Part(entity), foreign=f.Part(entity), wrongRoot=f.Part();
    Put(hidden,0x382,false); Put(faded,0x384,0.4f); Put(Read<Obj>(tinted,0xA8),0x40,0.4f);
    Put(foreign,0x398,Make("OtherServices"));
    Get(hidden).items.push_back(hiddenDescendant);
    Get(entity).items={visible,hidden,faded,tinted,foreign,wrongRoot,Make("UnknownAnimatedSubclass"),nullptr};
    auto result=monsterGlow::ReadEntities();
    Check(result.valid && result.targets.size()==1 && result.targets[0].renderers.size()==4,
        "Visible translucent parts must pass; hidden mounted parts and descendants must stay excluded");
    Check(result.targets[0].renderers[0].get()==Read<Obj>(entity,0x1E8) &&
        result.targets[0].renderers[1].get()==Read<Obj>(visible,0x1E8) &&
        result.targets[0].renderers[2].get()==Read<Obj>(faded,0x1E8) &&
        result.targets[0].renderers[3].get()==Read<Obj>(tinted,0x1E8),"Wrong composite source renderer identity");
}
void MalformedChildren() {
    {
        Fixture f; auto entity=f.Add(1); auto child=f.Part(entity);
        Get(entity).items.assign(33,child);
        auto result=monsterGlow::ReadEntities();
        Check(result.valid && result.targets.empty() && enumerators==1 && disposed==1,"Unbounded duplicate child iterator or missing disposal");
    }
    {
        Fixture f; auto entity=f.Add(1);
        for(int i=0;i<32;++i) Get(entity).items.push_back(f.Part(entity));
        auto result=monsterGlow::ReadEntities();
        Check(result.valid && result.targets.empty() && enumerators==1 && disposed==1,"Too many unique composite parts accepted");
    }
    {
        Fixture f; auto entity=f.Add(1); Get(entity).items.push_back(f.Part(entity)); Get(entity).throwCurrent=true;
        auto result=monsterGlow::ReadEntities();
        Check(!result.valid && result.targets.empty() && enumerators==1 && disposed==1,"Malformed child iterator exposed partial target or leaked");
    }
    {
        Fixture f; auto entity=f.Add(1); Get(entity).missingIterator=true;
        auto result=monsterGlow::ReadEntities();
        Check(result.valid && result.targets.empty(),"Missing child iterator exposed partial target");
    }
}
void ChangesAndBounds() {
    Fixture f(false); f.Add(1); f.Add(2); Get(f.world).byId.erase(2);
    { auto result=monsterGlow::ReadEntities(); Check(result.valid && result.targets.size()==1,"Removed root remained visible"); }
    Get(f.info).items.clear();
    { auto result=monsterGlow::ReadEntities(); Check(result.valid && result.targets.empty(),"Empty roster was not valid empty"); }
    f.Add(3); onItem=[&] { Put(f.state.creatures.get(),0x10,Make("OtherServices")); };
    { auto result=monsterGlow::ReadEntities(); Check(!result.valid && result.targets.empty(),"Changed context escaped snapshot gate"); }
    onItem={}; Put(f.state.creatures.get(),0x10,f.services);
    Get(f.info).countOverride=513;
    { auto result=monsterGlow::ReadEntities(); Check(!result.valid && std::string(result.reason)=="roster-limit" && result.rosterCount==513,"Oversized roster must be diagnosed"); }
    Get(f.info).countOverride=-1; f.state.stopped=true;
    { auto result=monsterGlow::ReadEntities(); Check(!result.valid,"Stopped module exposed targets"); }
    f.state.stopped=false; failList=true;
    { auto result=monsterGlow::ReadEntities(); Check(!result.valid && result.targets.empty(),"Failed roster exposed stale targets"); }
}
}
int main() {
    try {
        fake::Initialize(); monsterGlow::InstallEntities(); CombatAndRoleplay(); PrivacyAndLife(); MonsterFlags(); NpcSelection();
        VisibleTransparency(); InvalidOrInvisibleOpacity(); ActualRendering(); MountedParts(); MalformedChildren(); ChangesAndBounds();
        std::cout<<"Monster glow entity offline fixtures passed\n"; return 0;
    } catch(const std::exception& error) { std::cerr<<error.what()<<'\n'; return 1; }
}
