#include "chat-travel.hpp"
#include "chat-travel-policy.hpp"
#include "chat-route.hpp"
#include "module.hpp"
#include "map-transitions.hpp"

namespace {
using namespace managed;
using chat_travel::Phase;
chat_travel::Journey journey;
Ref character, teleports, cached, compositor, freshList, cartography;
Ref routeService;
Klass routeClass{}, mapServiceClass{};
Method mapReady{}, mapData{}, routeActive{};
int64_t quotedMap=0;
int quotedCost=-1;
bool quoteReceived=false;
int freshSource=-1;
bool installed=false, dispatching=false;
bool directPlan=false;
bool teleportOnly=false, havenbagDeparture=false;
uint64_t nextPlanningSlice=0;
int64_t targetMap=0;
Method resolve{}, currentMap{}, inBag{}, eligible{}, walk{}, interactives{}, interact{}, interactCtor{};
Method teleport{}, teleportCtor{}, funds{}, mapInfo{}, subarea{}, world{};
Method isKnown{}, quote{}, quoteCtor{};
Klass skillMakerClass{}, teleportClass{}, quoteClass{};
Method noticeCtor{},noticeConvert{},noticeDisplay{};
Klass noticeClass{},chatClass{};
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
void Notice(const std::string& message) {
    // Feed the native local chat history. This is not a chat send request and
    // has no sender, recipient or network action.
    if(!module||!module->servicesReady||module->stopped||module->stopRequested)return;
    const auto now=GetTickCount64();
    if(message==lastNotice&&now-lastNoticeAt<2000)return;
    lastNotice=message;lastNoticeAt=now;
    try {
        Ref chat(module->FindService("ham"));
        if(!chat.get()||api.object_get_class(chat.get())!=chatClass)return;
        Ref text(api.string_new(("TP : "+message).c_str())),action(api.object_new(noticeClass));
        unsigned channel=0x200,flags=4;int zero=0;int64_t zero64=0;
        api.Invoke(noticeCtor,action.get(),{&channel,text.get(),&zero64,&zero64,&zero64,nullptr,nullptr,&zero,&zero64,&zero64,nullptr,&flags});
        Ref rendered(api.Invoke(noticeConvert,nullptr,{action.get()}));
        if(rendered.get())api.Invoke(noticeDisplay,chat.get(),{rendered.get()});
    } catch(const std::exception& e) {Log(std::string("CHAT_TRAVEL_NOTICE_ERROR ")+e.what());}
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
void CompletePlan();
void Plan(std::vector<chat_travel::Destination> choices,bool direct) {
    chat_travel::Destination here;here.map=journey.sourceMap;here.known=true;
    if(!api.Value<bool>(api.Invoke(inBag,character.get()))&&Locate(here))
        choices.push_back(here);
    directPlan=direct;
    chat_route::Begin(std::move(choices),targetMap,journey.sourceMap);
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
void RequestOnward() {
    auto target=journey.target;
    journey.Walk(GetTickCount64()); // Commit before the native action can reenter.
    bool immediate=true;
    api.Invoke(walk,character.get(),{&target.x,&target.y,&target.world,&immediate});
    journey.onwardAccepted=api.Value<bool>(api.Invoke(routeActive,routeService.get())) ||
        api.Value<bool>(api.Field(routeService.get(),"dsjd"));
    Log("CHAT_TRAVEL_ONWARD_REQUEST attempt="+std::to_string(journey.onwardAttempts)+" target="+std::to_string(targetMap));
}
void ContinueOnward() {
    const bool active=api.Value<bool>(api.Invoke(routeActive,routeService.get()));
    const bool pending=api.Value<bool>(api.Field(routeService.get(),"dsjd"));
    Ref destination(api.Field(routeService.get(),"dsjs"));
    const auto nativeTarget=destination.get()?api.Value<uint32_t>(api.Field(destination.get(),"id")):0;
    if ((active || pending) && nativeTarget!=targetMap) {
        Cancel("onward replaced by another native destination","un autre trajet est actif ; la demande précédente est abandonnée."); return;
    }
    if (active && nativeTarget==targetMap) {
        Log("CHAT_TRAVEL_ONWARD_ACTIVE target="+std::to_string(targetMap));
        RestoreChatTravel(); Notice("autopilote lancé."); return;
    }
    if (Map()==targetMap) { RestoreChatTravel(); Notice("destination atteinte."); return; }
    if (Map()!=journey.arrivalMap) { Cancel("left arrival before onward confirmation"); return; }
    if (pending) { journey.onwardAccepted=true; return; }
    if (GetTickCount64()-journey.onwardAt<1000) return;
    // Never restart a route which the native game accepted then cancelled.
    // One bounded retry is allowed only when no acceptance was ever observed.
    if (journey.onwardAccepted || journey.onwardAttempts>=2) {
        Cancel("onward not started","l'autopilote n'a pas démarré. Le jeu a refusé ou interrompu le trajet."); return;
    }
    if (journey.Settled(ArrivalReady())) RequestOnward();
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
        int index=Count(candidates.get())-1;
        if (index<0) { Cancel("no native map at destination coordinates"); return; }
        targetMap=api.Value<int64_t>(api.Call(candidates.get(),"get_Item",{&index}));
        if(targetMap<=0){Cancel("invalid native destination map");return;}
        Log("CHAT_TRAVEL_RESOLVED source="+std::to_string(map)+" target="+std::to_string(targetMap)+" world="+std::to_string(target.world));
        journey.Begin(target,map,GetTickCount64());
        havenbagDeparture=api.Value<bool>(api.Invoke(inBag,character.get()));
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
                    RestoreChatTravel(); Notice("arrivée au zaap choisi.");
                }
                return;
            }
            if (journey.CanWalk(Map()) && Map()==targetMap) { RestoreChatTravel(); Notice("destination atteinte."); return; }
            if (journey.Settled(journey.CanWalk(Map()) && ArrivalReady())) RequestOnward();
        } else if (journey.phase==Phase::Onward) {
            ContinueOnward();
        }
    } catch (const std::exception& e) { Log(std::string("CHAT_TRAVEL_ERROR ")+e.what()); RestoreChatTravel();Notice("échec du calcul du trajet. Le détail est enregistré dans module.log."); }
}
void TickChatTravelPlanning() {
    // The compositor runs each frame; the rest of the mod only pumps every
    // 200ms. Advance the bounded CPU search here, leaving network phases on
    // their existing cadence. The shared slice gate prevents double pumping.
    if(journey.phase==Phase::Planning)TickChatTravel();
}
void RestoreChatTravel() { chat_route::Clear(); journey.Clear(); freshList.reset(); freshSource=-1; character.reset(); teleports.reset(); cached.reset(); compositor.reset(); cartography.reset(); routeService.reset(); quotedMap=targetMap=0; quotedCost=-1; quoteReceived=false;directPlan=false;teleportOnly=havenbagDeparture=false;nextPlanningSlice=0; }
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
    eligible=verify("esg","zhx",0,0x18BD400); walk=verify("esg","zhy",4,0x18BD470);
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
    installed=true;
}
