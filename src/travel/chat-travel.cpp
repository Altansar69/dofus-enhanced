#include "travel/chat-travel.hpp"
#include "travel/chat-travel-policy.hpp"
#include "travel/chat-route.hpp"
#include "travel/roublard-passage.hpp"
#include "core/module.hpp"
#include "maps/map-transitions.hpp"
#include "stats/stats-travel.hpp"
#include <algorithm>

// Ends the journey but keeps the passive trip watch (defined below).
void EndJourney();
namespace {
void PostNotice(const std::string& message);
using namespace managed;
using chat_travel::Phase;
chat_travel::Journey journey;
Ref character, teleports, cached, compositor, freshList, cartography;
Ref routeService;
Klass routeClass{}, mapServiceClass{};
Method mapReady{}, mapData{}, routeActive{}, following{};
// Passive record of the native trip after the onward request: the route
// service keeps no destination after a cancel, so any restart comes from a
// new native request (the party follow) and shows up here in the log.
struct Watch {
    Ref route, character;
    uint64_t started=0, until=0;
    std::string last;
    int lines=0;
} watch;
constexpr uint64_t WatchMs=120000; constexpr int WatchLines=24;
int64_t quotedMap=0;
int quotedCost=-1;
bool quoteReceived=false;
int freshSource=-1;
bool installed=false, dispatching=false;
bool directPlan=false;
bool teleportOnly=false, havenbagDeparture=false;
uint64_t nextPlanningSlice=0;
// The game's own destination: the last map esg.zid gives for the coordinates
// and world, the one its autopilot travels to. The route search, already there,
// the arrival and the onward trip use this map only.
int64_t targetMap=0;
// The Roublards' secret passage the route comparison chose (roublard_passage):
// its shortcut id and Bad Aboum's map, the autopilot's target before it. After a
// passage, or when one fails, the rest of the trip is planned without shortcuts.
int passageLink=-1;
int64_t passageMap=0;
bool passageOff=false;
std::vector<int> plannedLinks;
uint64_t nextObservation=0;
Method resolve{}, currentMap{}, inBag{}, eligible{}, travel{}, interactives{}, interact{}, interactCtor{};
Method teleport{}, teleportCtor{}, funds{}, mapInfo{}, subarea{}, world{};
Method isKnown{}, quote{}, quoteCtor{};
Klass skillMakerClass{}, teleportClass{}, quoteClass{};
Method noticeCtor{},noticeConvert{},noticeDisplay{};
Klass noticeClass{},chatClass{};
// The player's own cell and zone, read like the game's world path service.
Method localId{},findEntity{},entityCell{},cellZone{};
Klass registryClass{},entityClass{},mapDataClass{},cellClass{};
std::string lastNotice;
uint64_t lastNoticeAt=0;
using ListFn=bool(*)(Obj,Obj,Method);
ListFn originalList=nullptr;
ListFn originalQuote=nullptr;
int Count(Obj list) {
    if (!list) return 0;
    const int n=api.Value<int>(api.Call(list,"get_Count"));
    if (n<0 || n>4096) throw std::runtime_error("Travel list bounds");
    return n;
}
bool Available() { return installed && module && module->ready && module->servicesReady && !module->stopped && !module->stopRequested && !module->InCombat(); }
bool SameSession() {
    return Available() && module->cached.get()==cached.get() && module->compositor.get()==compositor.get() &&
        module->FindService("fng")==character.get() && module->FindService("fpf")==teleports.get() &&
        (!cartography.get() || module->FindService("fkc")==cartography.get()) &&
        (!routeService.get() || module->FindService("fjs")==routeService.get());
}
int64_t Map() {
    Ref location(api.Invoke(currentMap,character.get()));
    return location.get()?*static_cast<int64_t*>(api.FieldAddress(location.get(),"m_mapId")):0;
}
void RequireField(Klass type,const char* name,size_t offset) {
    auto field=api.class_get_field_from_name(type,name);
    if (!field || api.field_get_offset(field)!=offset) throw std::runtime_error(std::string("Travel field mismatch: ")+name);
}
void Notice(const std::string& message) { PostChatNotice("TP : "+message); }
void PostNotice(const std::string& message) {
    // Feed the native local chat history. This is not a chat send request and
    // has no sender, recipient or network action.
    if(!module||!module->servicesReady||module->stopped||module->stopRequested)return;
    if(!noticeCtor||!noticeConvert||!noticeDisplay||!noticeClass||!chatClass)return;
    const auto now=GetTickCount64();
    if(message==lastNotice&&now-lastNoticeAt<2000)return;
    lastNotice=message;lastNoticeAt=now;
    try {
        Ref chat(module->FindService("ham"));
        if(!chat.get()||api.object_get_class(chat.get())!=chatClass)return;
        Ref text(api.string_new(message.c_str())),action(api.object_new(noticeClass));
        unsigned channel=0x200,flags=4;int zero=0;int64_t zero64=0;
        api.Invoke(noticeCtor,action.get(),{&channel,text.get(),&zero64,&zero64,&zero64,nullptr,nullptr,&zero,&zero64,&zero64,nullptr,&flags});
        Ref rendered(api.Invoke(noticeConvert,nullptr,{action.get()}));
        if(rendered.get())api.Invoke(noticeDisplay,chat.get(),{rendered.get()});
    } catch(const std::exception& e) {Log(std::string("CHAT_TRAVEL_NOTICE_ERROR ")+e.what());}
}
bool Following() {
    // The game's party follow (the character follows a member): its own trip
    // requests replace ours at each map load or member position update.
    return character.get() && following && api.Value<bool>(api.Invoke(following,character.get()));
}
void StartWatch() {
    watch=Watch{}; watch.route.reset(routeService.get()); watch.character.reset(character.get());
    watch.started=GetTickCount64(); watch.until=watch.started+WatchMs;
}
void TickWatch() {
    if (!watch.route.get()) return;
    const auto now=GetTickCount64();
    if (now>=watch.until || watch.lines>=WatchLines || !Available() ||
        module->FindService("fjs")!=watch.route.get() || module->FindService("fng")!=watch.character.get()) { watch=Watch{}; return; }
    const bool active=api.Value<bool>(api.Invoke(routeActive,watch.route.get()));
    const bool pending=api.Value<bool>(api.Field(watch.route.get(),"dsjd"));
    Ref destination(api.Field(watch.route.get(),"dsjs"));
    const auto target=destination.get()?api.Value<uint32_t>(api.Field(destination.get(),"id")):0;
    const bool follows=following && api.Value<bool>(api.Invoke(following,watch.character.get()));
    const auto state="active="+std::to_string(active)+" pending="+std::to_string(pending)+
        " target="+std::to_string(target)+" following="+std::to_string(follows);
    if (state==watch.last) return;
    watch.last=state; ++watch.lines;
    Log("AUTOTRIP_WATCH "+state+" ms="+std::to_string(now-watch.started));
}
void Cancel(const char* reason,const char* message="trajet interrompu. Consulte le journal du mod pour le détail.") {
    Log(std::string("CHAT_TRAVEL ")+reason);RestoreChatTravel();Notice(message);
}
bool ListHook(Obj service,Obj message,Method method) {
    const bool result=originalList(service,message,method);
    try {
        if (!message || journey.phase!=Phase::Destinations || service!=teleports.get() || !SameSession() ||
            Map()!=journey.sourceMap || journey.Expired(GetTickCount64())) return result;
        const int source=Read<int>(message,0x28);
        if (source==1 || source<0 || source>4) return result;
        freshList.reset(api.Field(service,"dspc")); freshSource=source;
    } catch (const std::exception& e) { Log(std::string("CHAT_TRAVEL_LIST_ERROR ")+e.what()); RestoreChatTravel(); }
    return result;
}
bool QuoteHook(Obj service,Obj message,Method method) {
    const bool result=originalQuote(service,message,method);
    try {
        if (!message || journey.phase!=Phase::DirectQuote || service!=cartography.get() || !SameSession() ||
            Map()!=journey.sourceMap || journey.Expired(GetTickCount64()) || quoteReceived ||
            Read<int64_t>(message,0x28)!=quotedMap) return result;
        // hgu is the native map tooltip reply. A positive price is required by fhw.betw.
        quotedCost=Read<int>(message,0x30); quoteReceived=true;
    } catch (const std::exception& e) { Log(std::string("CHAT_TRAVEL_QUOTE_ERROR ")+e.what()); RestoreChatTravel(); }
    return result; // Never teleport from a receive callback.
}
bool Locate(chat_travel::Destination& value) {
    Ref info(api.Invoke(mapInfo,nullptr,{&value.map})); if (!info.get()) return false;
    Ref area(api.Invoke(subarea,info.get())); if (!area.get()) return false;
    value.world=api.Value<int>(api.Invoke(world,area.get()));
    value.x=Read<int16_t>(info.get(),0x18); value.y=Read<int16_t>(info.get(),0x1A);
    return true;
}
void RequestTeleport(int source,int64_t map,int price) {
    Ref request(api.object_new(teleportClass));
    int destination=0; unsigned cost=static_cast<unsigned>(price);
    api.Invoke(teleportCtor,request.get(),{&source,&destination,&map,&cost});
    if (!SameSession() || Map()!=journey.sourceMap ||
        (havenbagDeparture && !api.Value<bool>(api.Invoke(inBag,character.get())))) {
        Cancel("session changed before teleport"); return;
    }
    journey.Teleport(map,GetTickCount64()); freshList.reset(); freshSource=-1;
    api.Invoke(teleport,teleports.get(),{request.get()});
    Log("CHAT_TRAVEL teleport requested; awaiting confirmed arrival");
    Notice(teleportOnly ? "téléportation demandée vers le zaap choisi." : "téléportation demandée ; le trajet continuera après l'arrivée.");
}
// The player's own zone on the current map, read the way the game's world path
// service starts its own search (eza.bbnj): the local entity's cell
// (EntityInfo.bajk) in the loaded map's cells, then that cell's roleplay zone
// (ClientCellData.GetLinkedRpZone), the zone of the graph's vertices. -1 when
// any step is not exactly known: every zone of the map then starts the search.
int HereZone() {
    const auto map=journey.sourceMap;
    const auto unknown=[map](const std::string& reason) {
        Log("CHAT_TRAVEL_HERE_ZONE map="+std::to_string(map)+" unknown="+reason); return -1;
    };
    if (!localId || !findEntity || !entityCell || !cellZone) return unknown("contracts");
    try {
        // zcx dereferences esg.dpzj: no character data while it is null.
        if (!api.Field(character.get(),"dpzj")) return unknown("character");
        auto id=api.Value<int64_t>(api.Invoke(localId,character.get()));
        Ref registry(module->FindService("fso"));
        if (id<=0 || !registry.get() || api.object_get_class(registry.get())!=registryClass) return unknown("registry");
        Obj found=nullptr;
        if (!api.Value<bool>(api.Invoke(findEntity,registry.get(),{&id,&found})) || !found) return unknown("entity");
        Ref entity(found);
        if (api.object_get_class(entity.get())!=entityClass) return unknown("entity");
        int cell=api.Value<int>(api.Invoke(entityCell,entity.get()));
        Ref scene(module->FindService("fpm"));
        if (!scene.get() || api.object_get_class(scene.get())!=mapServiceClass ||
            !api.Value<bool>(api.Invoke(mapReady,scene.get()))) return unknown("scene");
        Ref data(api.Invoke(mapData,scene.get()));
        if (!data.get() || api.object_get_class(data.get())!=mapDataClass || api.Value<int>(api.Field(data.get(),"id"))!=map) return unknown("map");
        Ref cells(api.Field(data.get(),"cellsData"));
        if (cell<0 || cell>=Count(cells.get())) return unknown("cell");
        Ref info(api.Call(cells.get(),"get_Item",{&cell}));
        if (!info.get() || api.object_get_class(info.get())!=cellClass ||
            api.Value<uint32_t>(api.Field(info.get(),"cellNumber"))!=static_cast<uint32_t>(cell)) return unknown("cell");
        const int zone=api.Value<int>(api.Invoke(cellZone,info.get()));
        Log("CHAT_TRAVEL_HERE_ZONE map="+std::to_string(map)+" cell="+std::to_string(cell)+" zone="+std::to_string(zone));
        return zone;
    } catch (const std::exception& e) { return unknown(e.what()); }
}
void CompletePlan();
void Plan(std::vector<chat_travel::Destination> choices,bool direct) {
    chat_travel::Destination here;here.map=journey.sourceMap;here.known=true;
    int zone=-1;
    if(!api.Value<bool>(api.Invoke(inBag,character.get()))&&Locate(here)) {
        choices.push_back(here); zone=HereZone();
    }
    directPlan=direct;
    // « Passage secret et temple des Roublards »: Bad Aboum's trips are offered to
    // the comparison as shortcuts, only while the option is on.
    std::vector<chat_route::Shortcut> shortcuts;
    if(module->settings.travelRoublardTemple&&!passageOff)shortcuts=roublard_passage::Shortcuts(GetTickCount64());
    plannedLinks.clear();
    for(const auto& link:shortcuts)plannedLinks.push_back(link.id);
    chat_route::SetShortcuts(std::move(shortcuts));
    chat_route::Begin(std::move(choices),targetMap,journey.sourceMap,zone,module->settings.travelRoublardTemple);
    journey.phase=Phase::Planning;
    Log("CHAT_TRAVEL comparing native traversable routes (map changes, teleport penalty 1)");
    Notice("recherche d'un trajet accessible...");
    CompletePlan();
}
void BeginDirect() {
    cartography.reset(module->FindService("fkc"));
    if (!cartography.get()) { Cancel("native direct zaap service unavailable"); return; }
    // Native zhr/zhq read/write esg.dpzi (+0x120). zch reads a different
    // List<long> at +0xF8 and can be empty even when zaaps are unlocked.
    Ref maps(api.Field(character.get(),"dpzi"));
    std::vector<chat_travel::Destination> choices;
    for (int i=0,n=Count(maps.get());i<n;++i) {
        Ref entry(api.Call(maps.get(),"get_Item",{&i})); if (!entry.get()) continue;
        chat_travel::Destination value; value.map=api.Value<int64_t>(entry.get()); value.known=true;
        // World-map IDs identify map layers, not disconnected travel networks.
        // Let the graph and native character criteria decide connectivity.
        if (value.map>0 && Locate(value)) choices.push_back(value);
    }
    Plan(std::move(choices),true);
}
void QuoteDirect(const chat_travel::Destination& selected) {
    quotedMap=selected.map;
    if (!SameSession() || Map()!=journey.sourceMap || !api.Value<bool>(api.Invoke(isKnown,character.get(),{&quotedMap}))) {
        Cancel("direct zaap context changed"); return;
    }
    if (quotedMap==journey.sourceMap) { journey.Teleport(quotedMap,GetTickCount64()); return; }
    Ref request(api.object_new(quoteClass));
    api.Invoke(quoteCtor,request.get(),{&quotedMap});
    quotedCost=-1; quoteReceived=false;
    journey.Quote(GetTickCount64()); // Set the pending map before any synchronous native callback.
    api.Invoke(quote,cartography.get(),{request.get()});
    Log("CHAT_TRAVEL direct zaap price requested; no Havenbag entry");
}
void CompleteDirect() {
    if (!quoteReceived) return;
    if (!api.Value<bool>(api.Invoke(isKnown,character.get(),{&quotedMap})) ||
        !api.Value<bool>(api.Invoke(eligible,character.get())) || api.Value<bool>(api.Invoke(inBag,character.get()))) {
        Cancel("direct zaap no longer available"); return;
    }
    Ref money(api.Invoke(funds,character.get()));
    if (quotedCost<=0 || !money.get() || *static_cast<int64_t*>(api.FieldAddress(money.get(),"dcjl"))<quotedCost) {
        Cancel("direct zaap price unavailable or insufficient funds"); return;
    }
    // Native cartography callback fhw.fht.beql uses source5, destination0 and this price.
    RequestTeleport(5,quotedMap,quotedCost);
}
bool UseZaap() {
    Ref source(module->FindService("fro")); if (!source.get()) return false;
    Ref list(api.Invoke(interactives,source.get()));
    Ref selected; unsigned instance=0;
    for (int i=0,n=Count(list.get());i<n;++i) {
        Ref wrapper(api.Call(list.get(),"get_Item",{&i}));
        Ref data(wrapper.get()?api.Field(wrapper.get(),"dhav"):nullptr);
        if (!data.get() || *static_cast<int*>(api.FieldAddress(data.get(),"dcao"))!=16) continue;
        Ref skills(api.Field(data.get(),"dcap"));
        for (int j=0,m=Count(skills.get());j<m;++j) {
            Ref skill(api.Call(skills.get(),"get_Item",{&j}));
            if (!skill.get() || *static_cast<int*>(api.FieldAddress(skill.get(),"dcak"))!=114) continue;
            if (selected.get()) return false; // Ambiguous enabled zaaps cannot authorize an interaction.
            selected.reset(wrapper.get()); instance=*static_cast<unsigned*>(api.FieldAddress(skill.get(),"dcal"));
        }
    }
    if (!selected.get()) return false;
    Ref dependencies[7]; const char* names[]={"frv","fsm","fmz","fnm","fnk","fnr","fjx"};
    for (int i=0;i<7;++i) { dependencies[i].reset(module->FindService(names[i])); if (!dependencies[i].get()) return false; }
    Ref maker(api.object_new(skillMakerClass));
    api.Invoke(interactCtor,maker.get(),{nullptr,selected.get(),nullptr,dependencies[0].get(),dependencies[1].get(),dependencies[2].get(),
        dependencies[3].get(),dependencies[4].get(),dependencies[5].get(),dependencies[6].get()});
    const auto map=Map(); if (!map || !SameSession()) return false;
    freshList.reset(); freshSource=-1;
    journey.Interact(map,GetTickCount64()); // Commit before synchronous native callbacks.
    int extra=-1;
    api.Invoke(interact,maker.get(),{nullptr,selected.get(),&instance,&extra});
    return true;
}
void Choose() {
    if (!freshList.get() || freshSource<0 || Map()!=journey.sourceMap) return;
    if (api.Field(teleports.get(),"dspc")!=freshList.get()) { Cancel("destination list replaced"); return; }
    Ref money(api.Invoke(funds,character.get()));
    if (!money.get()) { Cancel("funds unavailable"); return; }
    const auto kamas=*static_cast<int64_t*>(api.FieldAddress(money.get(),"dcjl"));
    std::vector<chat_travel::Destination> choices;
    for (int i=0,n=Count(freshList.get());i<n;++i) {
        Ref entry(api.Call(freshList.get(),"get_Item",{&i})); if (!entry.get()) continue;
        chat_travel::Destination value;
        value.map=*static_cast<int64_t*>(api.FieldAddress(entry.get(),"dsoi"));
        value.type=*static_cast<int*>(api.FieldAddress(entry.get(),"dsok"));
        value.cost=*static_cast<int*>(api.FieldAddress(entry.get(),"dsol"));
        value.known=*static_cast<bool*>(api.FieldAddress(entry.get(),"dsoo"));
        if (!value.known || value.type!=0 || value.cost<0 || value.map==journey.sourceMap) continue;
        if (value.cost<=kamas && Locate(value)) choices.push_back(value);
    }
    if (teleportOnly) {
        // Only the explicitly selected map may be used. Cached prices, a
        // nearby alternative, and the direct-cartography source cannot apply.
        const chat_travel::Destination* selected=nullptr;
        for (const auto& value:choices) if (value.map==targetMap) {
            if (selected) { Cancel("ambiguous selected Havenbag zaap"); return; }
            selected=&value;
        }
        if (!selected || !api.Value<bool>(api.Invoke(isKnown,character.get(),{&targetMap}))) {
            Cancel("selected Havenbag zaap unavailable", "ce zaap n'est pas disponible depuis le Havre-Sac, ou les kamas sont insuffisants."); return;
        }
        RequestTeleport(freshSource,selected->map,selected->cost);
        return;
    }
    Plan(std::move(choices),false);
}
void CompletePlan() {
    if(Map()!=journey.sourceMap) {Cancel("departure changed during route comparison");return;}
    if(!directPlan&&api.Field(teleports.get(),"dspc")!=freshList.get()) {Cancel("destination list changed during planning");return;}
    const auto result=chat_route::Tick();
    if(result.status==chat_route::Status::Pending)return;
    chat_route::Clear();
    if(result.status!=chat_route::Status::Found){
        Cancel("no verified native route; no coordinate-distance fallback",
            journey.target.world==14 ? "aucun trajet accessible vers Srambad depuis ta position ou tes zaaps connus. Un accès à la dimension est nécessaire." :
            "aucun trajet accessible depuis ta position ou tes zaaps connus. Vérifie l'accès à cette zone.");return;
    }
    Log("CHAT_TRAVEL selected departure map="+std::to_string(result.departure.map)+" routeScore="+std::to_string(result.steps));
    passageLink=-1;passageMap=0;
    if(result.shortcut>=0){
        // Only a shortcut this comparison was given: Bad Aboum's map becomes the
        // autopilot's target, the passage then starts there.
        const bool offered=std::find(plannedLinks.begin(),plannedLinks.end(),result.shortcut)!=plannedLinks.end();
        const auto entrance=offered?roublard_passage::Entrance(result.shortcut):0;
        if(entrance){
            passageLink=result.shortcut;passageMap=entrance;
            Log("ROUBLARD_PASSAGE_ROUTE link="+std::to_string(passageLink)+" entrance="+std::to_string(passageMap)+" departure="+std::to_string(result.departure.map)+" target="+std::to_string(targetMap));
            Notice("le trajet passe par le passage secret des Roublards, "+roublard_passage::Describe(passageLink)+".");
        } else Log("ROUBLARD_PASSAGE_ROUTE_IGNORED link="+std::to_string(result.shortcut));
    }
    if(result.departure.map==journey.sourceMap)journey.Teleport(journey.sourceMap,GetTickCount64());
    else if(directPlan)QuoteDirect(result.departure);
    else RequestTeleport(freshSource,result.departure.map,result.departure.cost);
}
bool ArrivalReady() {
    if (Map()!=journey.arrivalMap || MapTransitionLoading() || api.Value<bool>(api.Invoke(inBag,character.get()))) return false;
    Ref scene(module->FindService("fpm"));
    if (!scene.get() || api.object_get_class(scene.get())!=mapServiceClass ||
        !api.Value<bool>(api.Invoke(mapReady,scene.get()))) return false;
    Ref data(api.Invoke(mapData,scene.get()));
    return data.get() && api.Value<int>(api.Field(data.get(),"id"))==journey.arrivalMap &&
        api.Value<bool>(api.Invoke(eligible,character.get()));
}
// The autopilot's target: Bad Aboum's map before a passage, else the game's own
// destination map.
int64_t Goal() { return passageLink>=0?passageMap:targetMap; }
void RequestOnward() {
    auto map=Goal();
    journey.Walk(GetTickCount64()); // Commit before the native action can reenter.
    // esg.zhy resolves the coordinates again and sends their last map in the
    // game's autopilot request, which faw.bcdl hands to faw.bcdq. The same native
    // call, with the map resolved at the start: zid ranks the maps from the
    // current one, so after a teleport it could pick another layer.
    bool immediate=true;
    api.Invoke(travel,routeService.get(),{&map,&immediate});
    journey.onwardAccepted=api.Value<bool>(api.Invoke(routeActive,routeService.get())) ||
        api.Value<bool>(api.Field(routeService.get(),"dsjd"));
    Log("CHAT_TRAVEL_ONWARD_REQUEST attempt="+std::to_string(journey.onwardAttempts)+" target="+std::to_string(map));
}
void ContinueOnward() {
    const auto goal=Goal();
    const bool active=api.Value<bool>(api.Invoke(routeActive,routeService.get()));
    const bool pending=api.Value<bool>(api.Field(routeService.get(),"dsjd"));
    Ref destination(api.Field(routeService.get(),"dsjs"));
    const auto nativeTarget=destination.get()?api.Value<uint32_t>(api.Field(destination.get(),"id")):0;
    if ((active || pending) && nativeTarget!=goal) {
        Cancel("onward replaced by another native destination","un autre trajet est actif ; la demande précédente est abandonnée."); return;
    }
    if (passageLink>=0 && ((active && nativeTarget==goal) || Map()==goal)) {
        // The journey goes on: the passage starts once the autopilot reached him.
        Log("ROUBLARD_PASSAGE_WALK target="+std::to_string(goal)+" active="+std::to_string(active));
        journey.WalkToPassage(goal,GetTickCount64());
        return;
    }
    if (active && nativeTarget==goal) {
        const bool follows=Following();
        Log("CHAT_TRAVEL_ONWARD_ACTIVE target="+std::to_string(targetMap)+" following="+std::to_string(follows));
        StartWatch(); EndJourney(); StatsModTrip();
        Notice(follows?"autopilote lancé. Ton personnage suit un membre du groupe : le jeu relancera son propre trajet vers ce membre après une annulation ou un changement de carte. Arrête le suivi dans le groupe pour voyager seul.":"autopilote lancé.");
        return;
    }
    if (Map()==targetMap) { StatsModTrip(); RestoreChatTravel(); Notice("destination atteinte."); return; }
    if (Map()!=journey.arrivalMap) { Cancel("left arrival before onward confirmation"); return; }
    if (pending) { journey.onwardAccepted=true; return; }
    const auto elapsed=GetTickCount64()-journey.onwardAt;
    if (elapsed<1000) return;
    // One bounded retry. A route the game accepted and then dropped within a
    // few seconds of the arrival (the player still settling on the zaap map)
    // is retried once as well: in play, pressing TP again was what it took.
    // A route dropped later than that was interrupted on purpose: never restart it.
    if (journey.onwardAttempts>=2 || (journey.onwardAccepted && elapsed>3000)) {
        Cancel("onward not started","l'autopilote n'a pas démarré. Le jeu a refusé ou interrompu le trajet."); return;
    }
    if (journey.onwardAccepted && journey.onwardAttempts==1) Log("CHAT_TRAVEL onward dropped right after acceptance; one retry");
    if (journey.Settled(ArrivalReady())) RequestOnward();
}
// The current map's scene is loaded (as ArrivalReady reads it, for any map).
bool SceneReady(int64_t map) {
    if (!map || MapTransitionLoading()) return false;
    Ref scene(module->FindService("fpm"));
    if (!scene.get() || api.object_get_class(scene.get())!=mapServiceClass ||
        !api.Value<bool>(api.Invoke(mapReady,scene.get()))) return false;
    Ref data(api.Invoke(mapData,scene.get()));
    return data.get() && api.Value<int>(api.Field(data.get(),"id"))==map;
}
// A fresh trip toward the same destination from where the character is now,
// without shortcuts: after the passage, or instead of it when it failed.
void Replan(const std::string& reason) {
    Log("CHAT_TRAVEL_REPLAN "+reason);
    roublard_passage::Stop();
    passageLink=-1;passageMap=0;passageOff=true;
    const auto map=Map();
    if (!map) { Cancel("no current map after the passage"); return; }
    if (map==targetMap) { StatsModTrip(); RestoreChatTravel(); Notice("destination atteinte."); return; }
    const auto target=journey.target;
    journey.Clear(); journey.Begin(target,map,GetTickCount64());
    freshList.reset(); freshSource=-1; quotedMap=0; quotedCost=-1; quoteReceived=false; directPlan=false; havenbagDeparture=false;
    BeginDirect();
}
void BeginPassage() {
    const auto now=GetTickCount64();
    if (!roublard_passage::Start(passageLink,Map(),now)) {
        Notice("le passage secret des Roublards n’est pas disponible ; le trajet continue sans lui.");
        Replan("ROUBLARD_PASSAGE start refused"); return;
    }
    journey.Pass(now);
}
// The autopilot walks to Bad Aboum: its end on his map starts the passage, any
// other end is the player's (or the game's) interruption, as for any trip.
void ContinuePassageWalk() {
    // « Passage secret et temple des Roublards » unticked meanwhile: no passage.
    if (!module->settings.travelRoublardTemple) { Cancel("passage option unticked during the walk","option du passage secret des Roublards décochée : trajet arrêté."); return; }
    const bool active=api.Value<bool>(api.Invoke(routeActive,routeService.get()));
    const bool pending=api.Value<bool>(api.Field(routeService.get(),"dsjd"));
    Ref destination(api.Field(routeService.get(),"dsjs"));
    const auto nativeTarget=destination.get()?api.Value<uint32_t>(api.Field(destination.get(),"id")):0;
    if ((active || pending) && nativeTarget!=passageMap) {
        Cancel("passage walk replaced by another native destination","un autre trajet est actif ; la demande précédente est abandonnée."); return;
    }
    if (Map()==passageMap) {
        journey.idleSamples=0;
        if (!active && !pending && journey.Settled(ArrivalReady())) BeginPassage();
        return;
    }
    journey.readySamples=0;
    if (active || pending) { journey.idleSamples=0; return; }
    if (++journey.idleSamples>=3) Cancel("passage walk interrupted before Bad Aboum","trajet interrompu avant le passage secret des Roublards.");
}
void ContinuePassage() {
    constexpr const char* interrupted="trajet interrompu pendant le passage secret des Roublards.";
    if (!module->settings.travelRoublardTemple) { Cancel("passage option unticked during the passage","option du passage secret des Roublards décochée : trajet arrêté."); return; }
    // The player's own moves while Bad Aboum is talked to: the Havre-Sac, or a
    // trip of the game's autopilot (his own, or the party follow).
    if (api.Value<bool>(api.Invoke(inBag,character.get()))) { Cancel("passage aborted: Havre-Sac",interrupted); return; }
    if (api.Value<bool>(api.Invoke(routeActive,routeService.get())) || api.Value<bool>(api.Field(routeService.get(),"dsjd"))) {
        Cancel("passage aborted: the game's autopilot started",interrupted); return;
    }
    const auto map=Map();
    const auto ready=map && SceneReady(map);
    const auto result=roublard_passage::Step(map,ready,GetTickCount64());
    switch (result.outcome) {
    case roublard_passage::Outcome::Running: return;
    case roublard_passage::Outcome::Arrived:
        Notice("passage secret des Roublards emprunté ; suite du trajet.");
        Replan("ROUBLARD_PASSAGE arrived map="+std::to_string(result.map)); return;
    case roublard_passage::Outcome::Failed:
        Notice(result.notice);
        Replan("ROUBLARD_PASSAGE failed: "+result.reason); return;
    case roublard_passage::Outcome::Aborted:
        // The trip stops there: never a new autopilot after the player's own move.
        Cancel(("passage aborted: "+result.reason).c_str(),result.notice.empty()?interrupted:result.notice.c_str()); return;
    }
}
// Learning from the player's own passages, whatever the journey (read-only).
void ObservePassage() {
    if (!installed || !Available() || !module->settings.travelRoublardTemple ||
        !roublard_passage::Available() || roublard_passage::Running()) return;
    // The planning pump runs every frame: look at most every 200 ms.
    const auto now=GetTickCount64();
    if (now<nextObservation) return;
    nextObservation=now+200;
    Ref who(module->FindService("fng"));
    if (!who.get()) return;
    Ref location(api.Invoke(currentMap,who.get()));
    const auto map=location.get()?*static_cast<int64_t*>(api.FieldAddress(location.get(),"m_mapId")):0;
    const bool ready=map && !api.Value<bool>(api.Invoke(inBag,who.get())) && SceneReady(map);
    roublard_passage::Observe(map,ready,now);
}
}
void StartChatTravel(ChatTpTarget target) {
    if (dispatching || !Available()) return;
    if (journey.phase!=Phase::Idle) {
        if(!teleportOnly && journey.target==target){Notice("trajet déjà en cours de préparation.");return;}
        Log("CHAT_TRAVEL replaced by a different TP destination");RestoreChatTravel();
    }
    dispatching=true; struct Guard { ~Guard(){dispatching=false;} } guard;
    try {
        character.reset(module->FindService("fng")); teleports.reset(module->FindService("fpf"));
        routeService.reset(module->FindService("fjs"));
        cached.reset(module->cached.get()); compositor.reset(module->compositor.get());
        if (!character.get() || !teleports.get() || !routeService.get() || api.object_get_class(routeService.get())!=routeClass || !api.Value<bool>(api.Invoke(eligible,character.get()))) { Cancel("native automatic travel unavailable","voyage automatique indisponible dans l'état actuel du personnage."); return; }
        const auto map=Map(); if (!map || target.world<0) { Cancel("destination context unavailable"); return; }
        Ref candidates(api.Invoke(resolve,character.get(),{&target.x,&target.y,&target.world}));
        const int count=Count(candidates.get());
        if (!count) { Cancel("no native map at destination coordinates"); return; }
        // Every map at the coordinates is logged; the last one is the game's choice.
        std::string layers;
        for (int i=0;i<count;++i) {
            targetMap=api.Value<int64_t>(api.Call(candidates.get(),"get_Item",{&i}));
            layers+=(layers.empty()?"":",")+std::to_string(targetMap);
        }
        if(targetMap<=0){Cancel("invalid native destination map");return;}
        Log("CHAT_TRAVEL_RESOLVED source="+std::to_string(map)+" target="+std::to_string(targetMap)+" world="+std::to_string(target.world)+" maps="+layers);
        const bool bag=api.Value<bool>(api.Invoke(inBag,character.get()));
        // Already on the game's destination map. A Havre-Sac start always goes
        // through its zaap, even at the Havre-Sac's own coordinates.
        if (!bag && map==targetMap) {
            Log("CHAT_TRAVEL_ALREADY_THERE map="+std::to_string(map)+" x="+std::to_string(target.x)+" y="+std::to_string(target.y)+" world="+std::to_string(target.world));
            EndJourney(); Notice("tu es déjà sur ["+std::to_string(target.x)+","+std::to_string(target.y)+"]."); return;
        }
        journey.Begin(target,map,GetTickCount64());
        havenbagDeparture=bag;
        if (havenbagDeparture) { UseZaap(); return; }
        BeginDirect();
    } catch (const std::exception& e) { Log(std::string("CHAT_TRAVEL_START_ERROR ")+e.what()); RestoreChatTravel();Notice("échec du calcul du trajet. Le détail est enregistré dans module.log."); }
}
void StartHavenbagZaap(int64_t map) {
    if (dispatching || !Available() || map<=0) return;
    if (teleportOnly && journey.phase!=Phase::Idle && targetMap==map) {
        Notice("téléportation déjà en cours de préparation."); return;
    }
    RestoreChatTravel();
    dispatching=true; struct Guard { ~Guard(){dispatching=false;} } guard;
    try {
        character.reset(module->FindService("fng")); teleports.reset(module->FindService("fpf"));
        cached.reset(module->cached.get()); compositor.reset(module->compositor.get());
        if (!character.get() || !teleports.get() || !api.Value<bool>(api.Invoke(inBag,character.get())) ||
            !api.Value<bool>(api.Invoke(isKnown,character.get(),{&map}))) {
            Cancel("selected zaap requires current Havenbag and known destination"); return;
        }
        chat_travel::Destination selected; selected.map=map;
        const auto source=Map();
        if (!source || source==map || !Locate(selected)) { Cancel("selected zaap context unavailable"); return; }
        targetMap=map; teleportOnly=true; havenbagDeparture=true;
        journey.Begin({selected.x,selected.y,selected.world},source,GetTickCount64());
        Log("MAP_HAVENBAG_ZAAP requested map="+std::to_string(map));
        Notice("préparation du zaap du Havre-Sac...");
        UseZaap();
    } catch (const std::exception& e) {
        Log(std::string("MAP_HAVENBAG_ZAAP_ERROR ")+e.what()); RestoreChatTravel();
        Notice("la téléportation depuis le Havre-Sac n'a pas pu être préparée.");
    }
}
void TickChatTravel() {
    if (!dispatching) {
        try { TickWatch(); } catch (const std::exception& e) { Log(std::string("AUTOTRIP_WATCH_ERROR ")+e.what()); watch=Watch{}; }
        try { ObservePassage(); } catch (const std::exception& e) { Log(std::string("ROUBLARD_PASSAGE_OBSERVE_ERROR ")+e.what()); }
    }
    if (journey.phase==Phase::Idle || dispatching) return;
    if (journey.phase==Phase::Planning) {
        const auto now=GetTickCount64();
        if(now<nextPlanningSlice)return;
        nextPlanningSlice=now+10;
    }
    dispatching=true; struct Guard { ~Guard(){dispatching=false;} } guard;
    try {
        if (!SameSession()) { Cancel("cancelled: session or combat","trajet annulé : le contexte du personnage a changé.");return; }
        if (journey.Expired(GetTickCount64())) { Cancel("cancelled: timeout","délai dépassé pendant la préparation du trajet."); return; }
        if (havenbagDeparture && journey.phase!=Phase::Arrival && journey.phase!=Phase::Onward &&
            journey.phase!=Phase::PassageWalk && journey.phase!=Phase::Passage &&
            (!api.Value<bool>(api.Invoke(inBag,character.get())) || Map()!=journey.sourceMap)) {
            Cancel("left Havenbag during preparation"); return;
        }
        if (journey.phase==Phase::Havenbag) {
            if (api.Value<bool>(api.Invoke(inBag,character.get()))) UseZaap();
        } else if (journey.phase==Phase::Planning) {
            CompletePlan();
        } else if (journey.phase==Phase::DirectQuote) {
            if (Map()!=journey.sourceMap) { Cancel("left direct departure map"); return; }
            CompleteDirect();
        } else if (journey.phase==Phase::Destinations) {
            if (Map()!=journey.sourceMap) { Cancel("left interaction map"); return; }
            Choose();
        } else if (journey.phase==Phase::Arrival) {
            if (teleportOnly) {
                if (journey.CanWalk(Map()) && !api.Value<bool>(api.Invoke(inBag,character.get()))) {
                    Log("MAP_HAVENBAG_ZAAP arrived map="+std::to_string(targetMap));
                    StatsModTrip(); RestoreChatTravel(); Notice("arrivée au zaap choisi.");
                }
                return;
            }
            if (journey.CanWalk(Map()) && Map()==targetMap) { StatsModTrip(); RestoreChatTravel(); Notice("destination atteinte."); return; }
            // Departure on Bad Aboum's own map: no autopilot before the passage.
            if (passageLink>=0 && journey.CanWalk(Map()) && Map()==passageMap) { journey.WalkToPassage(passageMap,GetTickCount64()); return; }
            if (journey.Settled(journey.CanWalk(Map()) && ArrivalReady())) RequestOnward();
        } else if (journey.phase==Phase::Onward) {
            ContinueOnward();
        } else if (journey.phase==Phase::PassageWalk) {
            ContinuePassageWalk();
        } else if (journey.phase==Phase::Passage) {
            ContinuePassage();
        }
    } catch (const std::exception& e) { Log(std::string("CHAT_TRAVEL_ERROR ")+e.what()); RestoreChatTravel();Notice("échec du calcul du trajet. Le détail est enregistré dans module.log."); }
}
void TickChatTravelPlanning() {
    // The compositor runs each frame; the rest of the mod only pumps every
    // 200ms. Advance the bounded CPU search here, leaving network phases on
    // their existing cadence. The shared slice gate prevents double pumping.
    if(journey.phase==Phase::Planning)TickChatTravel();
}
void EndJourney() { chat_route::Clear(); journey.Clear(); freshList.reset(); freshSource=-1; character.reset(); teleports.reset(); cached.reset(); compositor.reset(); cartography.reset(); routeService.reset(); quotedMap=targetMap=0; quotedCost=-1; quoteReceived=false;directPlan=false;teleportOnly=havenbagDeparture=false;nextPlanningSlice=0;
    roublard_passage::Stop(); passageLink=-1; passageMap=0; passageOff=false; plannedLinks.clear(); }
void RestoreChatTravel() { EndJourney(); watch=Watch{}; roublard_passage::Restore(); }
void PostChatNotice(const std::string& text) { PostNotice(text); }
void InstallChatTravel() {
    chat_route::Install();
    const auto verify=[](const char* type,const char* name,int argc,uintptr_t rva) { return api.FindVerified(api.Class("Core.dll","",type),name,argc,rva); };
    noticeClass=api.Class("Core.dll","","cwo");chatClass=api.Class("Core.dll","","hal");
    noticeCtor=verify("cwo",".ctor",12,0x268B950);
    noticeConvert=verify("hay","bncy",1,0x2132A60);
    noticeDisplay=verify("hal","bmyc",1,0x2121CD0);
    resolve=verify("esg","zid",3,0x18BD8B0);
    routeClass=api.Class("Core.dll","","faw"); mapServiceClass=api.Class("Core.dll","","epf");
    mapReady=verify("epf","xkj",0,0x18389C0);
    mapData=verify("epf","xkd",0,0x11ECD00);
    routeActive=verify("faw","bcdi",0,0x7E1C10);
    currentMap=verify("esg","zeo",0,0x185C1E0); inBag=verify("esg","zfr",0,0x18BBA00);
    eligible=verify("esg","zhx",0,0x18BD400);
    // The game's autopilot request handler (faw.bcdl and faw.bcdm call it):
    // travel to an exact map. esg.zhy is zhx, then zid's last map to this call.
    travel=verify("faw","bcdq",2,0x1A1A2D0);
    // Party follow state of the character (esg <dpzc>, set by the game's
    // follow messages): the only native source of trips after a cancel.
    following=verify("esg","zcl",0,0x18BAE60);
    funds=verify("esg","zdc",0,0x11EEB80);
    isKnown=verify("esg","zhr",1,0x18BCB30);
    quoteClass=api.Class("Core.dll","","dhw"); quoteCtor=verify("dhw",".ctor",1,0x68A590);
    quote=verify("fhk","bepd",1,0x1C4A520);
    interactives=verify("wp","qyb",0,0x2629090);
    skillMakerClass=api.Class("Core.dll","Core.UILogic.Components.ContextMenu.ContextMenuFactory","SkillMenuMaker");
    interactCtor=api.FindVerified(skillMakerClass,".ctor",10,0x16F6E20);
    interact=api.FindVerified(skillMakerClass,"OnSkillClicked",4,0x16F6CE0);
    teleportClass=api.Class("Core.dll","","dhc"); teleportCtor=api.FindVerified(teleportClass,".ctor",4,0x268C3E0);
    teleport=verify("fbq","bcla",1,0x1A4A5E0);
    constexpr const char* image="Ankama.Dofus.Core.DataCenter.dll",*space="Core.DataCenter.Metadata.World";
    auto info=api.Class(image,space,"MapInformationData");
    mapInfo=api.FindVerified(info,"GetMapInformationById",1,0x7C4200);
    subarea=api.FindVerified(info,"get_subArea",0,0x7C44B0);
    world=api.FindVerified(api.Class(image,space,"SubAreaData"),"get_worldmapId",0,0x7D1040);
    auto receive=verify("fbq","bcld",1,0x1A4AA10);
    AddHook(receive,0x1A4AA10,reinterpret_cast<void*>(&ListHook),reinterpret_cast<void**>(&originalList));
    auto price=verify("fhk","bepe",1,0x1C4A5D0);
    AddHook(price,0x1C4A5D0,reinterpret_cast<void*>(&QuoteHook),reinterpret_cast<void**>(&originalQuote));
    // The player's own zone, as the game's world path service reads it (eza.bbnj).
    // Optional: without it, every zone of the current map starts the comparison.
    try {
        constexpr const char* entities="Core.Services.Entities.EntityInformationServices",*cells="AleCore.Data.UnityClient";
        registryClass=api.Class("Core.dll","","evv"); entityClass=api.Class("Core.dll",entities,"EntityInfo");
        mapDataClass=api.Class("AleCore.dll",cells,"ClientMapData"); cellClass=api.Class("AleCore.dll",cells,"ClientCellData");
        RequireField(mapDataClass,"cellsData",0x80); RequireField(cellClass,"cellNumber",0x10);
        localId=verify("esg","zcx",0,0x18BB140); findEntity=verify("evv","bamm",2,0x1957A70);
        entityCell=api.FindVerified(entityClass,"bajk",0,0x1951590);
        cellZone=api.FindVerified(cellClass,"GetLinkedRpZone",0,0x681F30);
    } catch (const std::exception& e) {
        localId=findEntity=entityCell=cellZone=nullptr;
        Log(std::string("CHAT_TRAVEL_HERE_ZONE_UNAVAILABLE ")+e.what());
    }
    // The Roublards' secret passage: optional, its failure disables only itself
    // (ROUBLARD_PASSAGE_UNAVAILABLE). Its learning lives beside the shared settings.
    roublard_passage::Install(module?module->settingsPath:std::filesystem::path{});
    installed=true;
}
