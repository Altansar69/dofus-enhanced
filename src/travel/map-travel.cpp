#include "travel/map-travel.hpp"
#include "travel/chat-travel.hpp"
#include "notebook/notebook-native.hpp"
#include "notebook/notebook-panel.hpp"
#include "core/module.hpp"
#include <cmath>
#include <memory>
#include <set>
#include <span>
#include <vector>

namespace {
using namespace managed;
struct Point { float x, y; };
static_assert(sizeof(Point)==8); // Native Vector2 travels in one x64 register.
using MenuFn=void(*)(Obj,Obj,Point,Point,Method);
using WalkFn=void(*)(Obj,Point,int,bool,Method);
using AutoFn=void(*)(Obj,Point,Method);
using MouseDownFn=void(*)(Obj,Obj,Method);
using BuildFn=Obj(*)(Obj,Point,Method);
MenuFn originalMenu{}; WalkFn originalWalk{}; AutoFn originalAuto{}; MouseDownFn originalMouseDown{};
BuildFn originalMiniMapMenu{}, originalBigMapMenu{};
Method inBag{}, currentMap{}, currentWorld{}, resolve{}, known{}, append{}, pixelToMap{}, timeNow{};
// The game's own double-click window in its map mouse-down handler (fhw.beud):
// a second left click within 0.4 s of the previous one.
constexpr float DoubleClickSeconds=0.4f;
// FigmaIcons of the owned rows: teleport (Havre-Sac zaap), pin (the notebook's positions).
constexpr int IconTeleport=304, IconPosition=225;
bool installed=false;
// « Réglages des cartes au clic droit » (Settings::mapMenuSettings, on by
// default, read at each right-click): below everything the game puts in the
// menu and a separator, the mod's settings of the right-clicked map, those of
// its rows in « Cartes »: native checkboxes that keep the menu open, as the
// game's own checkbox there, then the dimension choice as a sub-menu of three
// radio rows. The two maps are the two cartography views (fhw), told apart by
// the menu owner's exact class as map-info does: the mini-map is the banner's
// (CartographyBannerView, the service's view 0), the big map the window's
// (CartographyMapView, view 1).
enum class MapKind { None, Mini, Big };
constexpr const char* MenuSpace="Core.UILogic.Components.ContextMenu";
constexpr const char* ViewSpace="Core.Services.Roleplay.CartographyService";
constexpr int IconNone=0; // FigmaIcons.none: the game's checkbox and sub-menu in these menus have no icon either.
// A checkbox row: its setting, its label (no « sur la mini-carte »: the menu
// is the map's own) and its short help (the options rows say more).
struct Toggle { bool Settings::* field; const char* label; const char* help; };
constexpr Toggle MiniMapToggles[]={
    {&Settings::resourceMiniMapEnabled,"Ressources",
        "Les repères des ressources choisies et leur barre, sur la mini-carte seulement. Décochée, ils disparaissent et ta sélection est gardée."},
    {&Settings::resourceMiniMapBar,"Barre des ressources",
        "La barre des six métiers qui ouvre le choix des ressources. Décochée, elle disparaît mais les repères restent ; le choix se fait alors depuis la grande carte."},
    {&Settings::miniMapCenterButton,"Bouton de recentrage","Le bouton en bas à gauche de la mini-carte qui la ramène sur ton personnage."},
    {&Settings::miniMapZoneName,"Nom de la zone au survol","L’infobulle de la case survolée affiche le nom de sa zone au-dessus de ses coordonnées."},
    {&Settings::archiZonesMiniMap,"Archimonstres manquants",
        "Colore les zones des archimonstres et boss de donjon de L’éternelle moisson qu’il te manque, selon le choix Tous / Archimonstres / Boss de donjon du mode Ocre de la grande carte."}};
constexpr Toggle BigMapToggles[]={
    {&Settings::resourceMapEnabled,"Ressources",
        "Le carré « Ressources » sous « Anomalies » et les repères des ressources choisies, sur la grande carte seulement. Décochée, ils disparaissent et ta sélection est gardée."},
    {&Settings::archiZonesMap,"Archimonstres manquants",
        "Le carré « Ocre » sous « Conquête » : il colore les zones des archimonstres et boss de donjon de L’éternelle moisson qu’il te manque."}};
// The dimension's sub-menu: the options' three choices, each row's index its saved mode.
constexpr const char* DimensionTitle="Dimension avec les coordonnées";
constexpr const char* DimensionLabels[]={"Non","Oui, sauf dans le Monde des Douze","Oui"};
constexpr const char* DimensionHelps[]={
    "L’infobulle de la case survolée garde les coordonnées du jeu, [x,y].",
    "La dimension s’ajoute aux coordonnées de la case survolée, [x,y, dimension], sauf dans le Monde des Douze.",
    "La dimension s’ajoute toujours aux coordonnées de la case survolée : [x,y, dimension]."};
static_assert(std::size(DimensionLabels)==3 && std::size(DimensionHelps)==3);
using DimensionField=int Settings::*;
Klass miniMapView{}, bigMapView{}, menuClass{}, itemViewClass{}, callbackClass{};
Method appendSeparator{}, appendCheckbox{}, appendRadio{}, appendSubMenu{}, setItemOn{}, setViewOn{}, childViews{};
bool settingsInstalled=false;
// Owned rows of a map's right-click menu: Havre-Sac zaaps (map) and « Ajouter aux positions » (position).
struct Row { Ref item, callback; int64_t map=0; bool armed=false, position=false; };
// An owned settings row: a checkbox of the menu (field) or a radio row of the
// dimension's sub-menu (choice, its saved mode).
struct SettingRow { Ref item, callback; bool Settings::* field=nullptr; int choice=-1; };
struct Menu {
    Ref menu, owner, character, cached, compositor, world;
    std::vector<Row> rows;
    int64_t source=0;      // The character's map when the menu was built (zaap rows).
    ChatTpTarget cell{};   // The right-clicked cell, in the displayed world.
    uint64_t created=0;
    bool attached=false, havenbag=false;
    // The settings rows of this map (None: none), added once the game's menu
    // builder returns (pending until then), with their separator, the
    // dimension's sub-menu (the mod's own menu) and its entry.
    MapKind kind=MapKind::None;
    bool settingsPending=false;
    std::vector<SettingRow> settings;
    Ref separator, submenu, entry;
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
// « « Ajouter aux positions » au clic droit sur les cartes » (Settings::mapAddPosition,
// on by default), read at each right-click: off, the menu gets no such row.
bool AddingPositions() { return module && module->settings.mapAddPosition && Available(); }
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
bool Is(Obj object,Klass klass) { return object && api.class_is_assignable_from(klass,api.object_get_class(object)); }
// The root showing a native menu, while it shows that menu.
Obj RootOf(Obj menu) {
    const auto root=api.Field(menu,"dofusContextualMenuRoot");
    return root && api.Call(root,"get_menuData")==menu?root:nullptr;
}
Obj Root(const Menu& menu) { return RootOf(menu.menu.get()); }
int IndexIn(Obj menu,Obj row) {
    Ref rows(api.Call(menu,"get_menuItems"));
    for (int i=0,n=Count(rows.get());i<n;++i) if (api.Call(rows.get(),"get_Item",{&i})==row) return i;
    return -1;
}
int Index(const Menu& menu,Obj row) { return IndexIn(menu.menu.get(),row); }
// The menu still belongs to the map view, world and session it was built for.
bool Current(const Menu& menu) {
    return Available() && module->cached.get()==menu.cached.get() && module->compositor.get()==menu.compositor.get() &&
        module->FindService("fng")==menu.character.get() && OwnerCurrent(menu.owner.get()) &&
        api.Invoke(currentWorld,menu.owner.get())==menu.world.get();
}
// Zaap rows also need the Havre-Sac and the map their list was made on.
bool Fresh(const Menu& menu) {
    return menu.havenbag && InHavenbag() && Current(menu) && Map(menu.character.get())==menu.source;
}
// Appends one owned row (null managed action: only HandleMapTravelCallback acts
// on it), kept in the menu's rows before anything else can fail.
void AppendRow(Menu& owner,const char* text,const char* help,int icon,int64_t map,bool position) {
    const auto menu=owner.menu.get();
    Ref rows(api.Call(menu,"get_menuItems")); int index=Count(rows.get());
    Ref label(api.string_new(text)),tooltip(api.string_new(help)),empty(api.string_new(""));
    Ref asset(api.object_new(api.class_from_type(api.method_get_param(append,7))));
    Ref color(api.object_new(api.class_from_type(api.method_get_param(append,10))));
    bool disabled=false,close=true; unsigned delay=1000; int style=1,figma=icon;
    // Same native default-item ABI as the other owned map-menu entries.
    try {
        api.Invoke(append,menu,{label.get(),nullptr,&disabled,&close,tooltip.get(),&delay,empty.get(),
            api.object_unbox(asset.get()),&style,&figma,api.object_unbox(color.get()),empty.get(),empty.get()});
    } catch (...) {
        if (Count(rows.get())==index+1) api.Call(menu,"RemoveItemAt",{&index});
        throw;
    }
    owner.rows.emplace_back();
    auto& row=owner.rows.back(); row.item.reset(api.Call(rows.get(),"get_Item",{&index}));
    row.callback.reset(api.Call(row.item.get(),"get_m_callback")); row.map=map; row.position=position; row.armed=true;
    if (!row.callback.get()) throw std::runtime_error("Map menu callback unavailable");
}
// The Havre-Sac rows: one per known zaap among the maps of the cell.
void AppendZaaps(Menu& menu,ChatTpTarget target) {
    Ref maps(api.Invoke(resolve,menu.character.get(),{&target.x,&target.y,&target.world}));
    std::set<int64_t> added;
    for (int i=Count(maps.get())-1;i>=0 && menu.rows.size()<8;--i) {
        auto map=api.Value<int64_t>(api.Call(maps.get(),"get_Item",{&i}));
        if (map<=0 || map==menu.source || !added.insert(map).second ||
            !api.Value<bool>(api.Invoke(known,menu.character.get(),{&map}))) continue;
        AppendRow(menu,"Se téléporter à ce zaap (Havre-Sac)","",IconTeleport,map,false);
    }
}
// Removes the owned rows of one kind from the menu (zaap rows: position false).
void RemoveRows(Menu& menu,bool position) {
    for (auto& row:menu.rows) if (row.position==position) {
        row.armed=false;
        int index=Index(menu,row.item.get()); if (index>=0) api.Call(menu.menu.get(),"RemoveItemAt",{&index});
    }
    std::erase_if(menu.rows,[position](const Row& row){return row.position==position;});
}
// The map whose settings rows a right-click menu gets: none with the option
// off, without the rows' natives, or for any other owner.
MapKind SettingsKind(Obj owner) {
    if (!settingsInstalled || !module || !module->settings.mapMenuSettings || !owner) return MapKind::None;
    const auto type=api.object_get_class(owner);
    return type==miniMapView?MapKind::Mini:type==bigMapView?MapKind::Big:MapKind::None;
}
std::span<const Toggle> Toggles(MapKind kind) {
    if (kind==MapKind::Mini) return MiniMapToggles;
    return BigMapToggles;
}
DimensionField Dimension(MapKind kind) {
    return kind==MapKind::Mini?&Settings::coordsWorldMiniMapMode:&Settings::coordsWorldMapMode;
}
// What a settings row shows: its box ticked, or its choice the saved one.
bool Shown(const Menu& menu,const SettingRow& row) {
    return row.field?module->settings.*row.field:module->settings.*Dimension(menu.kind)==row.choice;
}
bool Empty(const Menu& menu) { return menu.rows.empty() && menu.settings.empty() && !menu.settingsPending; }
// Appends one item to a native menu and returns it; a partial native append is
// taken back before the error goes on.
template<class Append> Obj AppendItem(Obj menu,const Append& append) {
    Ref rows(api.Call(menu,"get_menuItems")); int index=Count(rows.get());
    try { append(); }
    catch (...) {
        if (Count(rows.get())==index+1) api.Call(menu,"RemoveItemAt",{&index});
        throw;
    }
    if (Count(rows.get())!=index+1) throw std::runtime_error("Map menu settings row not appended");
    return api.Call(rows.get(),"get_Item",{&index});
}
// Keeps a settings row as soon as it is in (null managed action: only
// HandleMapTravelCallback acts on it, recognising its handler by identity).
void KeepSetting(Menu& owner,Obj item,bool Settings::* field,int choice) {
    auto& row=owner.settings.emplace_back();
    row.item.reset(item); row.field=field; row.choice=choice;
    row.callback.reset(api.Call(item,"get_m_callback"));
    if (!Is(row.callback.get(),callbackClass)) throw std::runtime_error("Unexpected map menu settings callback");
}
// The settings rows, below everything the game put in the menu: the
// dimension's sub-menu first (the mod's own menu, the game's not touched yet),
// then a separator, the checkboxes and the sub-menu's entry. The checkboxes
// and radio rows keep the menu open, as the game's checkbox in these menus.
void AppendSettings(Menu& owner) {
    const auto menu=owner.menu.get();
    const int dimension=module->settings.*Dimension(owner.kind);
    if (!Settings::ValidCoordsWorldMode(dimension)) throw std::runtime_error("Invalid map dimension choice");
    Ref empty(api.string_new(""));
    bool disabled=false,close=false; unsigned delay=1000; int icon=IconNone;
    owner.submenu.reset(api.New(menuClass));
    // The radio method's own asset parameter type, as context-display builds its radio rows.
    const auto assetClass=api.class_from_type(api.method_get_param(appendRadio,4));
    if (!assetClass || !api.class_is_valuetype(assetClass)) throw std::runtime_error("Map menu radio asset is not a value type");
    Ref asset(api.object_new(assetClass));
    int group=0; bool allowOff=false,refreshParent=false;
    for (int choice=0;choice<static_cast<int>(std::size(DimensionLabels));++choice) {
        Ref label(api.string_new(DimensionLabels[choice])),help(api.string_new(DimensionHelps[choice]));
        bool on=choice==dimension;
        KeepSetting(owner,AppendItem(owner.submenu.get(),[&]{
            api.Invoke(appendRadio,owner.submenu.get(),{label.get(),nullptr,&on,&group,api.object_unbox(asset.get()),
                &allowOff,&disabled,&close,help.get(),empty.get(),&delay,&refreshParent});
        }),nullptr,choice);
    }
    owner.separator.reset(AppendItem(menu,[&]{ api.Invoke(appendSeparator,menu); }));
    for (const auto& toggle:Toggles(owner.kind)) {
        Ref label(api.string_new(toggle.label)),help(api.string_new(toggle.help));
        bool on=module->settings.*toggle.field;
        KeepSetting(owner,AppendItem(menu,[&]{
            api.Invoke(appendCheckbox,menu,{label.get(),nullptr,&on,&disabled,&close,help.get(),&delay,&icon,empty.get()});
        }),toggle.field,-1);
    }
    Ref title(api.string_new(DimensionTitle)); int style=1; // ListElementStyleEnum.action, as the game's sub-menu in these menus
    owner.entry.reset(AppendItem(menu,[&]{
        api.Invoke(appendSubMenu,menu,{title.get(),owner.submenu.get(),nullptr,&disabled,&style,&icon});
    }));
}
// Takes the settings rows, their separator and the sub-menu's entry out of the
// game's menu, each on its own; their handlers, no longer kept, stay inert.
void RemoveSettings(Menu& owner) {
    std::vector<Obj> items{owner.entry.get(),owner.separator.get()};
    for (const auto& row:owner.settings) if (row.field) items.push_back(row.item.get());
    for (auto item:items) if (item) {
        try { int index=Index(owner,item); if (index>=0) api.Call(owner.menu.get(),"RemoveItemAt",{&index}); }
        catch (const std::exception& e) { Log(std::string("MAP_MENU_SETTINGS_RESTORE_ERROR ")+e.what()); }
    }
    owner.settings.clear(); owner.entry.reset(); owner.separator.reset(); owner.submenu.reset();
    owner.settingsPending=false;
}
// The settings rows show the settings: each item (what its view shows when it
// is built, the sub-menu's at each opening) and each view already shown, which
// the game's own click handler toggles by itself too; once after a click and at
// every tick, so that a change made elsewhere (the options page) shows too.
void SyncSettings(Menu& owner) {
    for (const auto& row:owner.settings) { bool on=Shown(owner,row); api.Invoke(setItemOn,row.item.get(),{&on}); }
    for (const auto parent:{owner.menu.get(),owner.submenu.get()}) {
        Ref root(parent?RootOf(parent):nullptr);
        if (!root.get() || !api.Call(root.get(),"get_panel")) continue;
        Ref views(api.Invoke(childViews,root.get()));
        for (int i=0,n=Count(views.get());i<n;++i) {
            Ref view(api.Call(views.get(),"get_Item",{&i}));
            if (!Is(view.get(),itemViewClass) || !api.Call(view.get(),"get_panel")) continue;
            const auto item=api.Call(view.get(),"get_itemData");
            for (const auto& row:owner.settings) if (row.item.get()==item) {
                bool on=Shown(owner,row); api.Invoke(setViewOn,view.get(),{&on});
            }
        }
    }
}
// A click on a settings row of the menu still shown, in its own view: its box
// or its choice saved at once (Module::Change: the features read it on the fly
// and an open options page shows it), then the rows show it.
void ApplySetting(Menu& owner,const SettingRow& row,Obj view) {
    const auto parent=row.field?owner.menu.get():owner.submenu.get();
    if (!Is(view,itemViewClass) || !api.Call(view,"get_panel") || api.Call(view,"get_itemData")!=row.item.get() ||
        IndexIn(parent,row.item.get())<0 || api.Value<bool>(api.Call(row.item.get(),"get_disabled")) ||
        !module->settings.mapMenuSettings || !Current(owner)) return;
    auto next=module->settings;
    if (row.field) next.*row.field=!(next.*row.field);
    else next.*Dimension(owner.kind)=row.choice;
    const auto menu=owned.get();
    if (!(next==module->settings)) module->Change(next); // The choice already saved: nothing to save again.
    if (owned && owned.get()==menu) SyncSettings(*owned);
}
void MenuHook(Obj owner,Obj menu,Point coordinates,Point mouse,Method method) {
    bool bag=false;
    try { bag=InHavenbag(); }
    catch (const std::exception& e) { Log(std::string("MAP_HAVENBAG_MENU_ERROR ")+e.what()); }
    // Outside the Havre-Sac, the game's own direct-zaap fragment; inside, the
    // mod's zaap rows replace it. The parent still builds all its other native
    // actions; no quote is requested here.
    if (!bag) originalMenu(owner,menu,coordinates,mouse,method);
    try {
        RestoreMapTravel();
        const bool adding=AddingPositions();
        const auto kind=SettingsKind(owner);
        if ((!bag && !adding && kind==MapKind::None) || !menu || !OwnerCurrent(owner)) return;
        // The cell's rows need a valid cell; the settings rows do not.
        ChatTpTarget target{};
        const bool cell=Target(coordinates,World(owner),target);
        if (!cell && kind==MapKind::None) return;
        auto next=std::make_unique<Menu>();
        next->menu.reset(menu); next->owner.reset(owner); next->character.reset(module->FindService("fng"));
        next->cached.reset(module->cached.get()); next->compositor.reset(module->compositor.get());
        next->world.reset(api.Invoke(currentWorld,owner)); next->source=Map(next->character.get());
        next->cell=target; next->havenbag=bag && next->source; next->created=GetTickCount64();
        next->kind=kind; next->settingsPending=kind!=MapKind::None;
        owned=std::move(next);
        if (cell && owned->havenbag) AppendZaaps(*owned,target);
        // After the teleport rows, right before the game's flag rows of this cell.
        if (cell && adding) AppendRow(*owned,"Ajouter aux positions","Enregistrer cette case (coordonnées et monde) dans les positions du Carnet personnel.",
            IconPosition,0,true);
        if (Empty(*owned)) owned.reset();
    } catch (const std::exception& e) {
        Log(std::string(bag?"MAP_HAVENBAG_MENU_ERROR ":"MAP_ADD_POSITION_MENU_ERROR ")+e.what()); RestoreMapTravel();
    }
}
// The right-click menu builders: CartographyBannerView.bedm (mini-map) and
// fhw.bedm (big map: CartographyMapView keeps it). Each starts with fhw.betw
// (MenuHook), then adds the game's other entries; the mini-map's ends with the
// filters sub-menu (fhw.bets), which the big map's leaves out. Their menu is
// complete when they return, before fhw.betv shows it: the settings rows of
// the menu MenuHook started for this owner go last.
Obj FinishMenu(Obj owner,Obj menu) {
    if (!owned || !owned->settingsPending || !menu || owned->menu.get()!=menu || owned->owner.get()!=owner) return menu;
    owned->settingsPending=false;
    try {
        if (SettingsKind(owner)==owned->kind && Current(*owned)) AppendSettings(*owned);
    } catch (const std::exception& e) { Log(std::string("MAP_MENU_SETTINGS_ERROR ")+e.what()); RemoveSettings(*owned); }
    if (Empty(*owned)) owned.reset();
    return menu;
}
Obj MiniMapMenuHook(Obj owner,Point mouse,Method method) { return FinishMenu(owner,originalMiniMapMenu(owner,mouse,method)); }
Obj BigMapMenuHook(Obj owner,Point mouse,Method method) { return FinishMenu(owner,originalBigMapMenu(owner,mouse,method)); }
// « Ajouter aux positions »: the clicked cell into the notebook, with the game's
// own map for its coordinates (the last one esg.zid gives, the one its autopilot
// travels to), told in the local chat.
void AddPosition(const Menu& menu) {
    auto cell=menu.cell; int64_t map=0;
    try {
        Ref maps(api.Invoke(resolve,menu.character.get(),{&cell.x,&cell.y,&cell.world}));
        for (int i=Count(maps.get())-1;i>=0 && map<=0;--i) map=api.Value<int64_t>(api.Call(maps.get(),"get_Item",{&i}));
    } catch (const std::exception& e) { Log(std::string("MAP_ADD_POSITION_MAP_ERROR ")+e.what()); map=0; }
    const auto where="["+std::to_string(cell.x)+","+std::to_string(cell.y)+"]";
    try {
        const auto added=notebook::AddMapPosition(cell.x,cell.y,cell.world,map>0?map:0);
        Log("MAP_ADD_POSITION x="+std::to_string(cell.x)+" y="+std::to_string(cell.y)+" world="+std::to_string(cell.world)+
            " map="+std::to_string(added.entry.map)+" created="+(added.created?"1":"0"));
        PostChatNotice(added.created?"Carnet : "+where+" « "+added.entry.title+" » ajoutée aux positions.":
            "Carnet : "+where+" est déjà dans tes positions (« "+added.entry.title+" »).");
        // An open notebook lists it at once.
        if (added.created) CheckNotebookPanel();
    } catch (const std::exception& e) {
        Log(std::string("MAP_ADD_POSITION_ERROR ")+e.what());
        PostChatNotice(std::string("Carnet : ")+e.what());
    }
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
        const bool position=row.position;
        try {
            if (!row.armed || !view || !api.Call(view,"get_panel") ||
                api.Call(view,"get_itemData")!=row.item.get() || Index(*owned,row.item.get())<0 ||
                api.Value<bool>(api.Call(row.item.get(),"get_disabled"))) return true;
            if (position) {
                if (!Current(*owned)) return true;
                row.armed=false;
                AddPosition(*owned);
                return true;
            }
            if (!Fresh(*owned) || !api.Value<bool>(api.Invoke(known,owned->character.get(),{&row.map}))) return true;
            row.armed=false;
            StartHavenbagZaap(row.map);
        } catch (const std::exception& e) { Log(std::string(position?"MAP_ADD_POSITION_ERROR ":"MAP_HAVENBAG_CLICK_ERROR ")+e.what()); }
        return true;
    }
    // A settings row: recognised by its handler, consumed even when stale.
    for (const auto& row:owned->settings) if (row.callback.get()==callback) {
        try { ApplySetting(*owned,row,view); }
        catch (const std::exception& e) { Log(std::string("MAP_MENU_SETTINGS_ERROR ")+e.what()); }
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
    RemoveSettings(*previous);
}
void TickMapTravel() {
    if (!owned) return;
    try {
        if (!Current(*owned)) { RestoreMapTravel(); return; }
        // Leaving the Havre-Sac, or its map, retires the zaap rows only.
        if (owned->havenbag && !Fresh(*owned)) { RemoveRows(*owned,false); owned->havenbag=false; }
        // « Réglages des cartes au clic droit » unticked meanwhile: its rows go too.
        if (!owned->settings.empty() && !module->settings.mapMenuSettings) RemoveSettings(*owned);
        if (Empty(*owned)) { owned.reset(); return; }
        Ref root(Root(*owned)); const bool attached=root.get() && api.Call(root.get(),"get_panel");
        if (attached) {
            owned->attached=true;
            if (!owned->settings.empty()) {
                try { SyncSettings(*owned); }
                catch (const std::exception& e) { Log(std::string("MAP_MENU_SETTINGS_ERROR ")+e.what()); RemoveSettings(*owned); }
            }
        }
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
    // « Réglages des cartes au clic droit »: both maps' menu builders, the two
    // views (exact classes, as map-info checks them) and the native rows that
    // context-display builds too (its click hook routes their handlers here).
    // Optional: without it, the menus keep their other rows.
    try {
        const auto view=api.Class("Core.dll","","fhw");
        miniMapView=api.Class("Core.dll",ViewSpace,"CartographyBannerView");
        bigMapView=api.Class("Core.dll",ViewSpace,"CartographyMapView");
        if (!api.class_is_assignable_from(view,miniMapView) || !api.class_is_assignable_from(view,bigMapView))
            throw std::runtime_error("Map menu cartography views unavailable");
        menuClass=api.Class("Core.dll",MenuSpace,"DofusContextualMenu");
        const auto item=api.Class("Core.dll",MenuSpace,"ContextMenuItem");
        itemViewClass=api.NestedClass(item,"ContextMenuItemView");
        callbackClass=api.NestedClass(item,"ClickCallbackHandler");
        const auto root=api.NestedClass(menuClass,"DofusContextualMenuRoot");
        api.FindVerified(menuClass,".ctor",0,0x16D8CC0);
        appendSeparator=api.FindVerified(menuClass,"AppendSeparator",0,0x16D76E0);
        appendCheckbox=api.FindVerified(menuClass,"AppendCheckboxItem",9,0x16D63D0);
        appendRadio=api.FindVerified(menuClass,"AppendRadioButtonItem",12,0x16D72F0);
        appendSubMenu=api.FindVerified(menuClass,"AppendSubMenu",6,0x16D7CF0);
        setItemOn=api.FindVerified(item,"set_isOn",1,0x11ECD70);
        setViewOn=api.FindVerified(itemViewClass,"set_isOn",1,0x16CE970);
        childViews=api.FindVerified(root,"GetChildItemsInMenu",0,0x16D18C0);
        AddHook(api.FindVerified(miniMapView,"bedm",1,0x1B93460),0x1B93460,
            reinterpret_cast<void*>(&MiniMapMenuHook),reinterpret_cast<void**>(&originalMiniMapMenu));
        AddHook(verify("fhw","bedm",1,0x1BECC50),0x1BECC50,reinterpret_cast<void*>(&BigMapMenuHook),reinterpret_cast<void**>(&originalBigMapMenu));
        settingsInstalled=true;
    } catch (const std::exception& e) { Log(std::string("MAP_MENU_SETTINGS_UNAVAILABLE ")+e.what()); }
}
