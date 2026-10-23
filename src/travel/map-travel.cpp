#include "travel/map-travel.hpp"
#include "travel/chat-travel.hpp"
#include "core/module.hpp"
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
using MouseDownFn=void(*)(Obj,Obj,Method);
MenuFn originalMenu{}; WalkFn originalWalk{}; AutoFn originalAuto{}; MouseDownFn originalMouseDown{};
Method inBag{}, currentMap{}, currentWorld{}, resolve{}, known{}, append{}, pixelToMap{}, timeNow{};
// The game's own double-click window in its map mouse-down handler (fhw.beud):
// a second left click within 0.4 s of the previous one.
constexpr float DoubleClickSeconds=0.4f;
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
// « Remplacer l’autopilote du jeu » (Settings::mapTravelPlanner, on by default),
// read on every action: switching it applies to the next click, no restart.
// Off, the three travel entries hooked below (WalkHook, AutoHook,
// MouseDownHook) stay entirely the game's: original handler and arguments, no
// planner. Checked first, so nothing else is read then. The Havre-Sac zaap rows
// of the right-click menu (MenuHook) do not depend on it.
bool Replacing() { return module && module->settings.mapTravelPlanner && Available(); }
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
    // Reuse TP planning; its onward call goes to faw.bcdq, not these UI hooks.
    Log(std::string("MAP_TRAVEL_ROUTE entry=")+entry+" x="+std::to_string(target.x)+
        " y="+std::to_string(target.y)+" world="+std::to_string(target.world));
    StartChatTravel(target);
}
// fhw.betn: the right-click entry « Voyager jusqu'à cette position (double
// clic) » (ui.mountTrip.travel) that fhw.betr adds, when the game allows its
// autopilot, to the menus of the full map (fhw.bedm) and of the mini-map
// (CartographyBannerView.bedm). That entry's callback (fhw.fhp) is its only
// caller. The same entry in a flag's menu (MapFlagMenuMaker.AutoTrip) and the
// /travel command call esg.zhy directly: they always stay the game's.
void WalkHook(Obj owner,Point coordinates,int world,bool immediate,Method method) {
    try {
        if (!Replacing()) { originalWalk(owner,coordinates,world,immediate,method); return; }
        RouteFromMap(owner,coordinates,world,"destination");
    } catch (const std::exception& e) { Log(std::string("MAP_TRAVEL_ROUTE_ERROR ")+e.what()); }
}
// fhw.beuf has no caller in this build: the same code as the console branch of
// the double-click, which fhw.beud inlines. Kept as before, the game's when off.
void AutoHook(Obj owner,Point coordinates,Method method) {
    try {
        if (!Replacing()) { originalAuto(owner,coordinates,method); return; }
        if (OwnerCurrent(owner)) RouteFromMap(owner,coordinates,World(owner),"double-click");
    } catch (const std::exception& e) { Log(std::string("MAP_TRAVEL_ROUTE_ERROR ")+e.what()); }
}
bool DoubleClick(Obj owner) {
    // The game's own test: the previous left click (fhw.dugt, its Time.time)
    // still lies within the double-click window.
    const float last=api.Value<float>(api.Field(owner,"dugt")), now=api.Value<float>(api.Invoke(timeNow,nullptr));
    return std::isfinite(last) && std::isfinite(now) && last+DoubleClickSeconds>now;
}
void MouseDownHook(Obj owner,Obj event,Method method) {
    // The double-click travel is inlined in the map view's mouse-down handler
    // (fhw.beuf is never called by the game), the same for the full map and the
    // mini-map, so the interception happens here: the game's own detection,
    // then the TP planner instead of its autopilot. Ctrl + double-click keeps
    // the game's direct autopilot; single clicks, drags and right clicks reach
    // the native handler untouched, and every click does with the option off.
    bool intercept=false; Point mouse{};
    try {
        intercept=Replacing() && event && OwnerCurrent(owner) && api.Value<int>(api.Call(event,"get_button"))==0 &&
            !api.Value<bool>(api.Call(event,"get_ctrlKey")) && DoubleClick(owner);
        if (intercept) mouse=api.Value<Point>(api.Call(event,"get_mousePosition"));
    } catch (const std::exception& e) { Log(std::string("MAP_TRAVEL_ROUTE_ERROR ")+e.what()); intercept=false; }
    if (!intercept) { originalMouseDown(owner,event,method); return; }
    try {
        Ref world(api.Invoke(currentWorld,owner)); Ref root(api.Field(owner,"dugq"));
        if (!world.get() || !root.get()) { originalMouseDown(owner,event,method); return; }
        // Same panel-to-map conversion as the game (fhl.bepv), then the planner.
        const auto point=api.Value<Point>(api.Invoke(pixelToMap,nullptr,{&mouse,world.get(),root.get()}));
        api.Call(event,"StopPropagation");
        RouteFromMap(owner,point,World(owner),"double-click");
    } catch (const std::exception& e) { Log(std::string("MAP_TRAVEL_ROUTE_ERROR ")+e.what()); }
}
void RequireField(Klass type,const char* name,size_t offset) {
    auto field=api.class_get_field_from_name(type,name);
    if (!field || api.field_get_offset(field)!=offset) throw std::runtime_error(std::string("Map travel field mismatch: ")+name);
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
    // Map double-click: the game's mouse-down handler, its clock and the
    // panel-to-map conversion it uses itself. The mouse getters (button,
    // position, Ctrl) belong to the generic MouseEventBase<T> and are resolved
    // by name on the event's own class. Optional: without it, the double-click
    // stays the game's and the rest of map travel keeps working.
    try {
        pixelToMap=verify("fhl","bepv",3,0x1C4E8D0);
        timeNow=api.FindVerified(api.Class("UnityEngine.CoreModule.dll","UnityEngine","Time"),"get_time",0,0x54414E0);
        api.FindVerified(api.Class("UnityEngine.UIElementsModule.dll","UnityEngine.UIElements","EventBase"),"StopPropagation",0,0x576D9C0);
        auto view=api.Class("Core.dll","","fhw"); RequireField(view,"dugt",0x80);
        AddHook(verify("fhw","beud",1,0x1BF88F0),0x1BF88F0,reinterpret_cast<void*>(&MouseDownHook),reinterpret_cast<void**>(&originalMouseDown));
    } catch (const std::exception& e) { Log(std::string("MAP_DOUBLE_CLICK_UNAVAILABLE ")+e.what()); }
}
