#include "map-travel.hpp"
#include "chat-travel.hpp"
#include "module.hpp"
#include <cmath>
#include <memory>
#include <set>
#include <vector>

namespace {
using namespace managed;
struct Point { float x, y; };
static_assert(sizeof(Point)==8); // Native Vector2 travels in one x64 register.
using MenuFn=void(*)(Obj,Obj,Point,Point,Method);
using WalkFn=void(*)(Obj,Point,int,bool,Method);
using AutoFn=void(*)(Obj,Point,Method);
MenuFn originalMenu{}; WalkFn originalWalk{}; AutoFn originalAuto{};
Method inBag{}, currentMap{}, currentWorld{}, resolve{}, known{}, append{};
bool installed=false;
struct Row { Ref item, callback; int64_t map=0; bool armed=false; };
struct Menu {
    Ref menu, owner, character, cached, compositor, world;
    std::vector<Row> rows;
    int64_t source=0;
    uint64_t created=0;
    bool attached=false;
};
std::unique_ptr<Menu> owned;
bool Available() {
    return installed && module && module->ready && module->servicesReady &&
        !module->stopped && !module->stopRequested && !module->InCombat();
}
bool InHavenbag() {
    if (!Available()) return false;
    const auto character=module->FindService("fng");
    return character && api.Value<bool>(api.Invoke(inBag,character));
}
bool OwnerCurrent(Obj owner) {
    if (!owner || !Available() || api.Field(owner,"dugy")!=module->FindService("fng") ||
        api.Field(owner,"dugv")!=module->FindService("fkc")) return false;
    Ref root(api.Field(owner,"dugq"));
    return root.get() && api.Call(root.get(),"get_panel");
}
int64_t Map(Obj character) {
    Ref location(api.Invoke(currentMap,character));
    return location.get()?*static_cast<int64_t*>(api.FieldAddress(location.get(),"m_mapId")):0;
}
int World(Obj owner) {
    Ref data(api.Invoke(currentWorld,owner));
    return data.get()?api.Value<int>(api.Field(data.get(),"id")):-1;
}
bool Target(Point point,int world,ChatTpTarget& target) {
    if (world<0 || !std::isfinite(point.x) || !std::isfinite(point.y) ||
        std::abs(point.x)>100000 || std::abs(point.y)>100000) return false;
    target={static_cast<int>(point.x),static_cast<int>(point.y),world};
    return true; // Match native truncation of already converted map coordinates.
}
int Count(Obj list) {
    if (!list) return 0;
    const auto n=api.Value<int>(api.Call(list,"get_Count"));
    if (n<0 || n>4096) throw std::runtime_error("Map travel list bounds");
    return n;
}
Obj Root(const Menu& menu) {
    const auto root=api.Field(menu.menu.get(),"dofusContextualMenuRoot");
    return root && api.Call(root,"get_menuData")==menu.menu.get()?root:nullptr;
}
int Index(const Menu& menu,Obj row) {
    Ref rows(api.Call(menu.menu.get(),"get_menuItems"));
    for (int i=0,n=Count(rows.get());i<n;++i) if (api.Call(rows.get(),"get_Item",{&i})==row) return i;
    return -1;
}
bool Fresh(const Menu& menu) {
    return InHavenbag() && module->cached.get()==menu.cached.get() && module->compositor.get()==menu.compositor.get() &&
        module->FindService("fng")==menu.character.get() && OwnerCurrent(menu.owner.get()) &&
        api.Invoke(currentWorld,menu.owner.get())==menu.world.get() && Map(menu.character.get())==menu.source;
}
void MenuHook(Obj owner,Obj menu,Point coordinates,Point mouse,Method method) {
    try {
        if (!InHavenbag()) { originalMenu(owner,menu,coordinates,mouse,method); return; }
        // Replace only the direct-zaap fragment in this context. The parent
        // still builds all its other native actions; no quote is requested here.
        RestoreMapTravel();
        if (!menu || !OwnerCurrent(owner)) return;
        ChatTpTarget target;
        if (!Target(coordinates,World(owner),target)) return;
        auto next=std::make_unique<Menu>();
        next->menu.reset(menu); next->owner.reset(owner); next->character.reset(module->FindService("fng"));
        next->cached.reset(module->cached.get()); next->compositor.reset(module->compositor.get());
        next->world.reset(api.Invoke(currentWorld,owner)); next->source=Map(next->character.get());
        next->created=GetTickCount64();
        if (!next->source) return;
        Ref maps(api.Invoke(resolve,next->character.get(),{&target.x,&target.y,&target.world}));
        std::set<int64_t> added;
        owned=std::move(next);
        for (int i=Count(maps.get())-1;i>=0 && owned->rows.size()<8;--i) {
            auto map=api.Value<int64_t>(api.Call(maps.get(),"get_Item",{&i}));
            if (map<=0 || map==owned->source || !added.insert(map).second ||
                !api.Value<bool>(api.Invoke(known,owned->character.get(),{&map}))) continue;
            Ref rows(api.Call(menu,"get_menuItems")); int index=Count(rows.get());
            Ref label(api.string_new("Se téléporter à ce zaap (Havre-Sac)")),empty(api.string_new(""));
            Ref asset(api.object_new(api.class_from_type(api.method_get_param(append,7))));
            Ref color(api.object_new(api.class_from_type(api.method_get_param(append,10))));
            bool disabled=false,close=true; unsigned delay=1000; int style=1,icon=304;
            // Same native default-item ABI as the other owned map-menu entries.
            try {
                api.Invoke(append,menu,{label.get(),nullptr,&disabled,&close,empty.get(),&delay,empty.get(),
                    api.object_unbox(asset.get()),&style,&icon,api.object_unbox(color.get()),empty.get(),empty.get()});
            } catch (...) {
                if (Count(rows.get())==index+1) api.Call(menu,"RemoveItemAt",{&index});
                throw;
            }
            owned->rows.emplace_back();
            auto& row=owned->rows.back(); row.item.reset(api.Call(rows.get(),"get_Item",{&index}));
            row.callback.reset(api.Call(row.item.get(),"get_m_callback")); row.map=map; row.armed=true;
            if (!row.callback.get()) throw std::runtime_error("Map zaap callback unavailable");
        }
        if (owned->rows.empty()) owned.reset();
    } catch (const std::exception& e) { Log(std::string("MAP_HAVENBAG_MENU_ERROR ")+e.what()); RestoreMapTravel(); }
}
void RouteFromMap(Obj owner,Point coordinates,int world,const char* entry) {
    ChatTpTarget target;
    if (!OwnerCurrent(owner) || world!=World(owner) || !Target(coordinates,world,target)) return;
    // Banner (mini-map) and full map share these explicit native actions.
    // Reuse TP planning; its onward call goes to esg.zhy, not these UI hooks.
    Log(std::string("MAP_TRAVEL_ROUTE entry=")+entry+" x="+std::to_string(target.x)+
        " y="+std::to_string(target.y)+" world="+std::to_string(target.world));
    StartChatTravel(target);
}
void WalkHook(Obj owner,Point coordinates,int world,bool immediate,Method method) {
    try {
        if (!Available()) { originalWalk(owner,coordinates,world,immediate,method); return; }
        RouteFromMap(owner,coordinates,world,"destination");
    } catch (const std::exception& e) { Log(std::string("MAP_TRAVEL_ROUTE_ERROR ")+e.what()); }
}
void AutoHook(Obj owner,Point coordinates,Method method) {
    try {
        if (!Available()) { originalAuto(owner,coordinates,method); return; }
        if (OwnerCurrent(owner)) RouteFromMap(owner,coordinates,World(owner),"double-click");
    } catch (const std::exception& e) { Log(std::string("MAP_TRAVEL_ROUTE_ERROR ")+e.what()); }
}
}
bool HandleMapTravelCallback(managed::Obj callback,managed::Obj view) {
    if (!owned) return false;
    for (auto& row:owned->rows) if (row.callback.get()==callback) {
        try {
            if (!row.armed || !view || !api.Call(view,"get_panel") ||
                api.Call(view,"get_itemData")!=row.item.get() || Index(*owned,row.item.get())<0 ||
                api.Value<bool>(api.Call(row.item.get(),"get_disabled")) || !Fresh(*owned) ||
                !api.Value<bool>(api.Invoke(known,owned->character.get(),{&row.map}))) return true;
            row.armed=false;
            StartHavenbagZaap(row.map);
        } catch (const std::exception& e) { Log(std::string("MAP_HAVENBAG_CLICK_ERROR ")+e.what()); }
        return true;
    }
    return false;
}
void RestoreMapTravel() {
    auto previous=std::move(owned); if (!previous) return;
    for (auto& row:previous->rows) {
        row.armed=false;
        try { int index=Index(*previous,row.item.get()); if (index>=0) api.Call(previous->menu.get(),"RemoveItemAt",{&index}); }
        catch (const std::exception& e) { Log(std::string("MAP_HAVENBAG_RESTORE_ERROR ")+e.what()); }
    }
}
void TickMapTravel() {
    if (!owned) return;
    try {
        if (!Fresh(*owned)) { RestoreMapTravel(); return; }
        Ref root(Root(*owned)); const bool attached=root.get() && api.Call(root.get(),"get_panel");
        if (attached) owned->attached=true;
        else if (owned->attached || GetTickCount64()-owned->created>5000) RestoreMapTravel();
    } catch (const std::exception& e) { Log(std::string("MAP_HAVENBAG_TICK_ERROR ")+e.what()); RestoreMapTravel(); }
}
void InstallMapTravel() {
    const auto verify=[](const char* type,const char* name,int argc,uintptr_t rva) {
        return api.FindVerified(api.Class("Core.dll","",type),name,argc,rva);
    };
    inBag=verify("esg","zfr",0,0x18BBA00); currentMap=verify("esg","zeo",0,0x185C1E0);
    currentWorld=verify("fhw","beqx",0,0x682030);
    resolve=verify("esg","zid",3,0x18BD8B0); known=verify("esg","zhr",1,0x18BCB30);
    append=api.FindVerified(api.Class("Core.dll","Core.UILogic.Components.ContextMenu","DofusContextualMenu"),"AppendDefaultItem",13,0x16D6C60);
    AddHook(verify("fhw","betw",3,0x1BF78A0),0x1BF78A0,reinterpret_cast<void*>(&MenuHook),reinterpret_cast<void**>(&originalMenu));
    AddHook(verify("fhw","betn",3,0x1BF5E50),0x1BF5E50,reinterpret_cast<void*>(&WalkHook),reinterpret_cast<void**>(&originalWalk));
    AddHook(verify("fhw","beuf",1,0x1BF8F40),0x1BF8F40,reinterpret_cast<void*>(&AutoHook),reinterpret_cast<void**>(&originalAuto));
    installed=true;
}
