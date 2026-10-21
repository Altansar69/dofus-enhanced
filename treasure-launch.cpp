#include "treasure-launch.hpp"
#include "treasure-data.hpp"
#include "treasure-hunt.hpp"
#include "notebook-native.hpp"
#include "chat-travel.hpp"
#include "soul-panel.hpp"
#include "tools-ui.hpp"
#include "module.hpp"
#include <algorithm>
#include <array>
#include <thread>
namespace {
using namespace managed;
constexpr const char* Ui = "UnityEngine.UIElementsModule.dll";
constexpr const char* UiSpace = "UnityEngine.UIElements";
constexpr const char* Figma = "Core.UILogic.Components.Figma";
constexpr size_t Rows = 4;
struct Job {
    treasure::Query query;
    std::atomic_bool cancelled{false}, done{false};
    std::vector<treasure::Clue> clues;
    std::string error;
};
struct State {
    Ref root, window, header, dragger, dependencies, cached, compositor;
    Ref x, y, filter, status, search, close, previous, next, position, hunt;
    std::array<Ref, 4> direction, inputEvents;
    std::array<Ref, Rows> rows, lines, travel;
    unsigned poiFilter = 0;
    std::string huntLabel;
    std::vector<Ref> manipulators;
    std::vector<treasure::Clue> clues, filtered;
    std::shared_ptr<Job> job;
    treasure::Query query;
    std::string lastX, lastY, lastFilter;
    int directionValue = 0;
    size_t offset = 0;
    bool haveResults = false, cleanup = false;
    ULONGLONG nextRequest = 0;
} state;
bool installed = false, wanted = false;
Method unsubscribe = nullptr;
// Retain a cancelled job until it finishes, even if its panel was closed.
// Reopening cannot accumulate background HTTP workers.
std::shared_ptr<Job> activeJob;
bool Session() {
    return module && module->ready && module->servicesReady && !module->stopped && !module->stopRequested;
}
Obj Style(Obj element, const char* property) {
    Ref style(api.Call(element,"get_style"));
    Obj box = api.Call(style.get(),(std::string("UnityEngine.UIElements.IStyle.get_")+property).c_str());
    if (!box || !api.class_is_valuetype(api.object_get_class(box))) throw std::runtime_error("Treasure style unavailable");
    return box;
}
void WriteStyle(Obj element, const char* property, Obj box) {
    Ref style(api.Call(element,"get_style"));
    api.Call(style.get(),(std::string("UnityEngine.UIElements.IStyle.set_")+property).c_str(),{api.object_unbox(box)});
}
void Enum(Obj element, const char* property, int value) {
    Ref box(Style(element,property)); api.Call(box.get(),"set_value",{&value}); WriteStyle(element,property,box.get());
}
void Float(Obj element, const char* property, float value) {
    Ref box(Style(element,property)); api.Call(box.get(),"set_value",{&value}); WriteStyle(element,property,box.get());
}
void Pixels(Obj element, const char* property, float value) {
    int unit=0;
    Ref length(api.New(api.Class(Ui,UiSpace,"Length"),{&value,&unit}));
    Ref box(Style(element,property)); api.Call(box.get(),"set_value",{api.object_unbox(length.get())}); WriteStyle(element,property,box.get());
}
void Text(Obj element, const std::string& text, const char* setter = "set_text") {
    Ref value(api.string_new(text.c_str())); api.Call(element,setter,{value.get()});
}
void Enabled(Obj element, bool enabled) { api.Call(element,"SetEnabled",{&enabled}); }
void Add(Obj parent, Obj child) { api.Call(parent,"Add",{child}); }
Obj Label(Obj parent, const std::string& text) {
    Ref label(api.New(api.Class(Ui,UiSpace,"Label")));
    Enum(label.get(),"whiteSpace",0); Pixels(label.get(),"fontSize",14); Pixels(label.get(),"marginBottom",6);
    float r=.9f,g=.9f,b=.9f,a=1;
    Ref color(api.New(api.Class("UnityEngine.CoreModule.dll","UnityEngine","Color"),{&r,&g,&b,&a}));
    Ref box(Style(label.get(),"color")); api.Call(box.get(),"set_value",{api.object_unbox(color.get())}); WriteStyle(label.get(),"color",box.get());
    Text(label.get(),text); Add(parent,label.get()); return label.get();
}
Obj Row(Obj parent) {
    Ref row(api.New(api.Class(Ui,UiSpace,"VisualElement")));
    Enum(row.get(),"flexDirection",2); Pixels(row.get(),"marginBottom",6); Add(parent,row.get()); return row.get();
}
Obj Button(Obj parent, const std::string& text) {
    Ref button(api.New(api.Class("Core.dll",Figma,"ListElementBasic")));
    int style=1; api.Call(button.get(),"set_listElementStyle",{&style});
    Pixels(button.get(),"minHeight",34); Pixels(button.get(),"marginRight",4); Pixels(button.get(),"minWidth",0);
    Text(button.get(),text); Add(parent,button.get()); return button.get();
}
Obj Input(Obj parent, const char* hint, const char* value, int maxLength) {
    Ref input(api.New(api.Class("Core.dll",Figma,"TextInput")));
    Text(input.get(),hint,"set_placeholderText"); Text(input.get(),hint,"set_tooltip");
    api.Call(input.get(),"set_maxLength",{&maxLength}); Text(input.get(),value);
    Pixels(input.get(),"minWidth",0); Pixels(input.get(),"minHeight",32); Float(input.get(),"flexGrow",1);
    Add(parent,input.get()); return input.get();
}
void Color(Obj element,const char* property,float r,float g,float b) {
    float a=1;Ref color(api.New(api.Class("UnityEngine.CoreModule.dll","UnityEngine","Color"),{&r,&g,&b,&a}));
    Ref box(Style(element,property));api.Call(box.get(),"set_value",{api.object_unbox(color.get())});WriteStyle(element,property,box.get());
}
Obj Section(Obj parent,const char* title) {
    Ref card(api.New(api.Class(Ui,UiSpace,"VisualElement")));Add(parent,card.get());
    for(auto p:{"paddingLeft","paddingRight","paddingTop","paddingBottom"})Pixels(card.get(),p,12);
    Pixels(card.get(),"marginBottom",10);Color(card.get(),"backgroundColor",.13f,.14f,.12f);
    for(auto p:{"borderTopLeftRadius","borderTopRightRadius","borderBottomLeftRadius","borderBottomRightRadius"})Pixels(card.get(),p,6);
    Ref heading(Label(card.get(),title));Color(heading.get(),"color",.77f,.87f,.35f);Enum(heading.get(),"unityFontStyleAndWeight",1);Pixels(heading.get(),"marginBottom",10);
    return card.get();
}
void Select(Obj button,bool selected) {
    Color(button,"backgroundColor",selected?.26f:.16f,selected?.33f:.17f,selected?.13f:.15f);
    Ref label(api.Field(button,"m_textLabel"));Color(label.get(),"color",selected?.83f:.92f,selected?.95f:.92f,selected?.43f:.90f);
}
std::string Read(Obj input) { return api.String(api.Call(input,"get_text")); }
Obj Root() {
    // Normal windows share RootMedium. RootHigh/tooltips retain native priority.
    Ref hud(soul::GetUiRoot()); if (!hud.get()) return nullptr;
    Ref parent(api.Call(hud.get(),"get_parent"));
    Ref hierarchy(api.Call(parent.get(),"get_hierarchy"));
    const int count=api.Value<int>(api.Call(hierarchy.get(),"get_childCount"));
    if (count<0 || count>64) throw std::runtime_error("Treasure UI layer bounds");
    Obj result=nullptr;
    for(int i=0;i<count;++i) {
        Obj child=api.Call(hierarchy.get(),"get_Item",{&i});
        if(api.String(api.Call(child,"get_name"))=="RootMedium") {
            if(result) throw std::runtime_error("Ambiguous native window layer");
            result=child;
        }
    }
    return result;
}
bool Current() {
    return Session() && state.window.get() && !state.cleanup && state.cached.get()==module->cached.get() &&
        state.compositor.get()==module->compositor.get() && state.dependencies.get()==module->FindService("fpt") &&
        api.Call(state.window.get(),"get_parent")==state.root.get() && api.Call(state.window.get(),"get_panel");
}
void Invalidate() {
    state.haveResults=false; state.clues.clear(); state.filtered.clear(); state.offset=0;
    if(state.job) state.job->cancelled.store(true);
    for(auto& line:state.lines) if(line.get()) Enum(line.get(),"display",1);
    if(state.previous.get()) Enabled(state.previous.get(),false);
    if(state.next.get()) Enabled(state.next.get(),false);
}
void ShowRows() {
    for(size_t i=0;i<Rows;++i) {
        const size_t index=state.offset+i;
        const bool show=index<state.filtered.size();
        Enum(state.lines[i].get(),"display",show?0:1);
        if(show) {
            const auto& clue=state.filtered[index];
            const auto text=clue.name+" → ["+std::to_string(clue.x)+","+std::to_string(clue.y)+"] · "+std::to_string(clue.distance)+" carte(s)";
            Text(state.rows[i].get(),clue.name+"\n["+std::to_string(clue.x)+","+std::to_string(clue.y)+"] · "+std::to_string(clue.distance)+" carte(s)");
            Text(state.rows[i].get(),text+"\nCliquer pour utiliser ces coordonnées comme nouveau départ.","set_tooltip");
            Text(state.travel[i].get(),"Lancer le trajet vers ["+std::to_string(clue.x)+","+std::to_string(clue.y)+"] : zaaps, bateaux et /travel, comme le bouton TP du chat.","set_tooltip");
        }
    }
    Enabled(state.previous.get(),state.offset>0);
    Enabled(state.next.get(),state.offset+Rows<state.filtered.size());
    Text(state.status.get(),state.filtered.empty() ?
        "Aucun indice correspondant. Les Phorreurs se cherchent en jeu." :
        std::to_string(state.filtered.size())+" indice(s) · résultat le plus proche pour chacun.\nClique un résultat pour préparer l'étape suivante.");
}
bool ReadQuery(treasure::Query& query) {
    query.direction=state.directionValue;
    return treasure::Coordinate(Read(state.x.get()),query.x) && treasure::Coordinate(Read(state.y.get()),query.y);
}
std::vector<treasure::Clue> Filtered(const std::string& text) {
    // The game's current point of interest filters exactly until the player
    // types their own clue text.
    if(text.empty() && state.poiFilter) return treasure::FilterById(state.clues,int(state.poiFilter));
    return treasure::Filter(state.clues,text);
}
bool FillPosition() {
    try {
        const auto here=notebook::CurrentPosition();
        Text(state.x.get(),std::to_string(here.x)); Text(state.y.get(),std::to_string(here.y));
        state.lastX=Read(state.x.get()); state.lastY=Read(state.y.get());
        return true;
    } catch(const std::exception& error) { Text(state.status.get(),error.what()); return false; }
}
const char* DirectionName(int direction) {
    return direction==6?"Nord":direction==4?"Ouest":direction==2?"Sud":direction==0?"Est":"?";
}
void Search();
bool ApplyHunt(bool search) {
    const auto step=treasure::CurrentStep();
    if(!step.active) { state.poiFilter=0; state.huntLabel.clear(); Text(state.status.get(),"Aucune chasse au trésor en cours. Renseigne le départ et la direction à la main."); return false; }
    if(step.direction!=0 && step.direction!=2 && step.direction!=4 && step.direction!=6) { Text(state.status.get(),"Étape de chasse sans direction exploitable."); return false; }
    Invalidate();
    state.directionValue=step.direction; state.poiFilter=step.poiId; Text(state.filter.get(),""); state.lastFilter.clear();
    const std::string target=step.poiId ? (step.hint.empty() ? "indice n°"+std::to_string(step.poiId) : step.hint) :
        step.type==5 ? "indice d’un PNJ (non recherché sur DofusDB)" : std::to_string(step.mapCount)+" carte(s) dans cette direction";
    state.huntLabel="Étape "+std::to_string(step.index)+" · "+DirectionName(step.direction)+" · "+target;
    if(!FillPosition()) return false;
    Text(state.status.get(),state.huntLabel+(search?"":"\nDépart : ta position actuelle. Clique sur Étape en cours pour rechercher."));
    if(search && step.poiId) Search();
    return true;
}
void Search() {
    if(state.job || GetTickCount64()<state.nextRequest) return;
    if(activeJob && !activeJob->done) {
        Text(state.status.get(),"La recherche précédente se termine. Réessaie dans un instant."); return;
    }
    treasure::Query query;
    if(!ReadQuery(query)) { Text(state.status.get(),"Entre des coordonnées X et Y entières valides."); return; }
    Invalidate();
    auto job=std::make_shared<Job>(); job->query=query; state.job=activeJob=job;
    state.query=query; state.lastX=Read(state.x.get()); state.lastY=Read(state.y.get());
    state.nextRequest=GetTickCount64()+1500;
    Text(state.status.get(),"Recherche des indices DofusDB…"); Enabled(state.search.get(),false);
    // Only ordinary C++ data crosses this boundary. No IL2CPP calls on this thread.
    try {
        std::thread([job] {
            try { job->clues=treasure::Fetch(job->query,job->cancelled); }
            catch(const std::exception& error) { job->error=error.what(); }
            catch(...) { job->error="La recherche DofusDB a échoué."; }
            job->done.store(true);
        }).detach();
    } catch(...) { state.job.reset(); activeJob.reset(); throw; }
}
bool Action(Obj owner) {
    int direction=-1, result=-1, travel=-1;
    for(int i=0;i<4;++i) if(owner && owner==state.direction[i].get()) direction=i;
    for(size_t i=0;i<Rows;++i) { if(owner && owner==state.rows[i].get()) result=static_cast<int>(i); if(owner && owner==state.travel[i].get()) travel=static_cast<int>(i); }
    const bool own=owner && (direction>=0 || result>=0 || travel>=0 || owner==state.search.get() || owner==state.close.get() ||
        owner==state.previous.get() || owner==state.next.get() || owner==state.position.get() || owner==state.hunt.get());
    if(!own) return false;
    try {
        if(!wanted || !Current() || !api.Value<bool>(api.Call(owner,"get_enabledInHierarchy"))) return true;
        if(owner==state.close.get()) { wanted=false; if(state.job)state.job->cancelled.store(true); return true; }
        if(direction>=0) {
            const int values[]={6,4,2,0}; state.directionValue=values[direction];
            Invalidate(); Text(state.status.get(),"Direction modifiée. Clique sur Rechercher.");
        } else if(owner==state.search.get()) Search();
        else if(owner==state.position.get()) { if(FillPosition()) { Invalidate(); Text(state.status.get(),"Position actuelle renseignée. Choisis la direction puis Rechercher."); } }
        else if(owner==state.hunt.get()) ApplyHunt(true);
        else if(travel>=0 && state.haveResults && state.offset+travel<state.filtered.size()) {
            const auto clue=state.filtered[state.offset+travel];
            try {
                // Same improved journey as the chat TP button: zaaps, boats, then /travel.
                const auto here=notebook::CurrentPosition();
                StartChatTravel({clue.x,clue.y,here.world});
                Text(state.status.get(),"Trajet lancé vers ["+std::to_string(clue.x)+","+std::to_string(clue.y)+"]. Clique le résultat pour en faire le prochain départ.");
            } catch(const std::exception& error) { Text(state.status.get(),error.what()); }
        }
        else if(owner==state.previous.get() && state.offset>=Rows) {state.offset-=Rows;ShowRows();}
        else if(owner==state.next.get() && state.offset+Rows<state.filtered.size()) {state.offset+=Rows;ShowRows();}
        else if(result>=0 && state.haveResults && state.offset+result<state.filtered.size()) {
            treasure::Query current;
            if(!ReadQuery(current) || current!=state.query) { Invalidate(); return true; }
            const auto clue=state.filtered[state.offset+result];
            Text(state.x.get(),std::to_string(clue.x)); Text(state.y.get(),std::to_string(clue.y)); Text(state.filter.get(),"");
            Invalidate(); Text(state.status.get(),"Nouveau départ renseigné. Choisis la direction suivante.");
        }
    } catch(const std::exception& error) { Log(std::string("TREASURE_PANEL_ACTION_ERROR ")+error.what()); }
    return true;
}
void Retain(const char* field) {
    Ref list(api.Field(state.window.get(),field));
    const int count=api.Value<int>(api.Call(list.get(),"get_Count"));
    if(count<0 || count>32) throw std::runtime_error("Treasure native manipulator bounds");
    for(int i=0;i<count;++i) state.manipulators.emplace_back(api.Call(list.get(),"get_Item",{&i}));
}
void Create(Obj root) {
    state.root.reset(root); state.dependencies.reset(module->FindService("fpt"));
    if(!state.dependencies.get() || api.object_get_class(state.dependencies.get())!=api.Class("Core.dll","","goc"))
        throw std::runtime_error("Treasure native dependencies unavailable");
    state.cached.reset(api.Call(state.dependencies.get(),"bfri")); state.compositor.reset(module->compositor.get());
    Ref input(api.Call(state.dependencies.get(),"bfre"));
    if(state.cached.get()!=module->cached.get() || !input.get() || api.object_get_class(input.get())!=api.Class("Core.dll","","eqo"))
        throw std::runtime_error("Treasure native session mismatch");
    state.window.reset(api.New(api.Class("Core.dll","Core.UILogic.Components","MagneticDragWindow")));
    Text(state.window.get(),"","set_name"); Text(state.window.get(),"Chasse au trésor · DofusDB","set_headerTitle");
    bool yes=true,no=false; int icon=317,size=1;
    api.Call(state.window.get(),"set_headerIcon",{&icon}); api.Call(state.window.get(),"set_headerSize",{&size});
    api.Call(state.window.get(),"set_headerShowCloseButton",{&no});
    for(const auto* setter:{"set_verticalResizable","set_horizontalResizable","set_cornerResizable"})
        api.Call(state.window.get(),setter,{&no});
    api.Call(state.window.get(),"set_hasBackground",{&yes});
    Enum(state.window.get(),"position",1); Float(state.window.get(),"flexGrow",0);
    Pixels(state.window.get(),"width",450); Pixels(state.window.get(),"left",80); Pixels(state.window.get(),"top",90);
    state.header.reset(api.Call(state.window.get(),"get_headerWidget"));
    toolsUi::AttachHeader("treasure",state.window.get());
    Ref draggers(api.Field(state.window.get(),"m_registeredElementDraggers"));
    state.dragger.reset(api.Call(draggers.get(),"get_Item",{state.header.get()}));
    Retain("m_elementDraggers"); Retain("m_resizerList");
    const char* getters[]={"ydf","ydh","ydj","ydl"};
    for(size_t i=0;i<4;++i) state.inputEvents[i].reset(api.Call(input.get(),getters[i]));
    api.Call(state.window.get(),"Init",{state.dependencies.get()});
    Ref content(toolsUi::ScrollBody(state.window.get(),root));
    Float(content.get(),"flexGrow",0);
    for(const auto* property:{"paddingLeft","paddingRight","paddingTop","paddingBottom"}) Pixels(content.get(),property,8);
    Ref departure(Section(content.get(),"1 · Point de départ"));
    Ref coords(Row(departure.get()));
    Label(coords.get(),"X "); state.x.reset(Input(coords.get(),"X","",5));
    Label(coords.get(),"  Y "); state.y.reset(Input(coords.get(),"Y","",5));
    state.position.reset(Button(coords.get(),"Ma position"));
    Text(state.position.get(),"Reprendre les coordonnées de la carte où tu te trouves.","set_tooltip");
    Ref directions(Row(departure.get()));
    const char* names[]={"↑ Nord","← Ouest","↓ Sud","→ Est"};
    for(size_t i=0;i<4;++i) {state.direction[i].reset(Button(directions.get(),names[i])); Float(state.direction[i].get(),"flexGrow",1);}
    Ref clueSection(Section(content.get(),"2 · Indice à trouver"));
    state.filter.reset(Input(clueSection.get(),"Nom de l’indice (ou laisse vide pour les lister)","",100));
    Pixels(state.filter.get(),"marginBottom",8);
    state.hunt.reset(Button(clueSection.get(),"Étape en cours"));
    Text(state.hunt.get(),"Reprend ta position, la direction et l’indice de la chasse au trésor en cours, puis lance la recherche.","set_tooltip");
    Pixels(state.hunt.get(),"marginBottom",8);
    state.search.reset(Button(clueSection.get(),"Rechercher les indices"));Select(state.search.get(),true);
    state.status.reset(Label(content.get(),"Renseigne le départ et la direction, puis lance la recherche."));
    Ref results(Section(content.get(),"3 · Résultats"));
    for(size_t i=0;i<Rows;++i) {
        auto& row=state.rows[i];
        state.lines[i].reset(Row(results.get())); Enum(state.lines[i].get(),"display",1);
        row.reset(Button(state.lines[i].get(),"")); Float(row.get(),"flexGrow",1);
        Pixels(row.get(),"height",48);
        Ref label(api.Field(row.get(),"m_textLabel"));
        api.Call(label.get(),"set_isSingleLine",{&no});
        Enum(label.get(),"whiteSpace",0);
        Pixels(label.get(),"fontSize",14);
        state.travel[i].reset(Button(state.lines[i].get(),"Chemin")); Pixels(state.travel[i].get(),"height",48); Pixels(state.travel[i].get(),"marginRight",0);
    }
    Ref pages(Row(results.get()));
    state.previous.reset(Button(pages.get(),"Précédents")); state.next.reset(Button(pages.get(),"Suivants"));
    Float(state.previous.get(),"flexGrow",1); Float(state.next.get(),"flexGrow",1);
    Enabled(state.previous.get(),false); Enabled(state.next.get(),false);
    state.close.reset(Button(content.get(),"Fermer"));
    Add(root,state.window.get());
    Log("TREASURE_PANEL_OPEN native=true provider=DofusDB");
    // Start from where the player stands and from the running hunt, if any.
    if(!ApplyHunt(false)) FillPosition();
}
}
void InstallTreasurePanel() {
    auto input=api.Class("Core.dll",Figma,"TextInput");
    api.FindVerified(input,".ctor",0,0x178DC90);
    api.FindVerified(input,"get_text",0,0x178DDA0);
    api.FindVerified(input,"set_text",1,0x178EC50);
    api.FindVerified(input,"set_placeholderText",1,0x178EAA0);
    api.FindVerified(input,"set_maxLength",1,0x178E9B0);
    api.FindVerified(api.Class(Ui,UiSpace,"VisualElement"),"BringToFront",0,0x55E08E0);
    api.FindVerified(api.Class("Core.dll","Core.UILogic.Components","DofusLabel"),"set_isSingleLine",1,0x1593FB0);
    unsubscribe=api.FindVerified(api.Class("Core.dll","Core.Engine.Inputs.Events","InputCustomEvent"),"UnSubscribe",1,0x20FF620);
    // Shared window/control contracts are verified by InstallSoulHelper first.
    soul::SetAuxiliaryPanelAction(Action);
    toolsUi::Register("treasure","Chasse au trésor · DofusDB",317,[]{return wanted;},[]{wanted=!wanted;if(!wanted&&state.job)state.job->cancelled.store(true);return true;});installed=true;
}
void OpenTreasureOverlay(const std::filesystem::path&) {
    if(!installed || !Session()) return;
    wanted=true;
    if(Current()) api.Call(state.window.get(),"BringToFront");
}
void RestoreTreasurePanel() {
    wanted=false;
    if(state.job) state.job->cancelled.store(true);
    if(!state.window.get()) { state=State{}; return; }
    state.cleanup=true;
    try {
        Enum(state.window.get(),"display",1);
        toolsUi::DetachHeader("treasure");
        const auto mouse=api.Class(Ui,UiSpace,"MouseCaptureController");
        if(state.header.get() && api.Value<bool>(api.Invoke(api.Find(mouse,"HasMouseCapture",1),nullptr,{state.header.get()}))) {
            *static_cast<bool*>(api.FieldAddress(state.dragger.get(),"m_active"))=false;
            api.Invoke(api.Find(mouse,"ReleaseMouse",1),nullptr,{state.header.get()});
            // Only the window that still owns capture may clear the drag flag.
            reinterpret_cast<void(*)(bool,Method)>(api.NativeAt(0x166DF30))(false,nullptr);
        }
        if(state.dragger.get()) api.Call(state.dragger.get(),"RemoveSnapPreview");
        bool locked=true; int none=0;
        api.Call(state.window.get(),"set_isUILocked",{&locked});
        api.Call(state.window.get(),"set_dragPosition",{&none});
        for(const auto& event:state.inputEvents) {
            if(!event.get()) continue;
            Ref list(api.Field(event.get(),"m_input")); if(!list.get()) continue;
            int count=api.Value<int>(api.Call(list.get(),"get_Count"));
            if(count<0 || count>16384) throw std::runtime_error("Treasure input subscriber bounds");
            for(int i=count;i-->0;) {
                Ref callback(api.Call(list.get(),"get_Item",{&i}));
                if(!callback.get()) continue;
                Obj target=api.Call(callback.get(),"get_Target");
                if(std::any_of(state.manipulators.begin(),state.manipulators.end(),[&](const Ref& item){return item.get()==target;}))
                    api.Invoke(unsubscribe,event.get(),{callback.get()});
            }
        }
        api.Call(state.window.get(),"RemoveFromHierarchy");
        state=State{};
    } catch(const std::exception& error) {Log(std::string("TREASURE_PANEL_CLEANUP_ERROR ")+error.what());}
}
bool TreasurePanelCleanupPending() { return state.cleanup; }
void TickTreasurePanel() {
    if(!installed) return;
    if(activeJob && activeJob->done) activeJob.reset();
    if(!wanted || !Session() || state.cleanup || (state.window.get() && !Current())) {RestoreTreasurePanel();return;}
    if(!state.window.get()) {
        Ref root(Root());
        if(!root.get() || !api.Call(root.get(),"get_panel")) return;
        try {Create(root.get());} catch(...) {RestoreTreasurePanel();throw;}
    }
    const auto x=Read(state.x.get()),y=Read(state.y.get()),filter=Read(state.filter.get());
    if(x!=state.lastX || y!=state.lastY) {
        Invalidate(); state.lastX=x; state.lastY=y;
        Text(state.status.get(),"Départ modifié. Clique sur Rechercher.");
    }
    if(state.job && state.job->done.load()) {
        auto job=std::move(state.job);
        treasure::Query current;
        if(!job->cancelled && ReadQuery(current) && current==job->query) {
            if(job->error.empty()) {
                state.clues=std::move(job->clues); state.haveResults=true;
                state.filtered=Filtered(filter); state.offset=0; ShowRows();
                Log("TREASURE_PANEL_RESULTS count="+std::to_string(state.clues.size()));
            } else {
                Text(state.status.get(),job->error);
                Log("TREASURE_PANEL_PROVIDER_ERROR "+job->error);
            }
        }
    }
    Enabled(state.search.get(),!state.job && GetTickCount64()>=state.nextRequest);
    if(filter!=state.lastFilter) {
        state.lastFilter=filter; state.offset=0;
        if(state.haveResults) {state.filtered=Filtered(filter);ShowRows();}
    }
    const int values[]={6,4,2,0};
    const char* names[]={"↑ Nord","← Ouest","↓ Sud","→ Est"};
    for(size_t i=0;i<4;++i){Text(state.direction[i].get(),names[i]);Select(state.direction[i].get(),state.directionValue==values[i]);}
}
