#include "treasure/treasure-launch.hpp"
#include "treasure/treasure-data.hpp"
#include "treasure/treasure-hunt.hpp"
#include "notebook/notebook-native.hpp"
#include "travel/chat-travel.hpp"
#include "ui/native-click.hpp"
#include "souls/soul-panel.hpp"
#include "ui/tools-ui.hpp"
#include "core/module.hpp"
#include <algorithm>
#include <array>
#include <thread>
namespace {
using namespace managed;
constexpr const char* Ui = "UnityEngine.UIElementsModule.dll";
constexpr const char* UiSpace = "UnityEngine.UIElements";
constexpr const char* Figma = "Core.UILogic.Components.Figma";
constexpr size_t Rows = 4;
// FigmaIcons used by the panel: treasureMap (window, hunt step, result rows),
// playerStanding and player (start), magnifier (search), lines (results),
// zaap (journey), arrowLeft/arrowRight (pages).
constexpr int IconTreasure=317,IconStart=229,IconPlayer=228,IconSearch=194,IconList=185,IconZaap=336,IconPrevious=44,IconNext=45;
constexpr int Directions[4]={6,4,2,0};
constexpr const char* DirectionNames[4]={"Nord","Ouest","Sud","Est"};
struct Job {
    treasure::Query query;
    std::atomic_bool cancelled{false}, done{false};
    std::vector<treasure::Clue> clues;
    std::string error;
};
struct State {
    Ref root, window, header, dragger, dependencies, cached, compositor, closeButton;
    Ref x, y, filter, status, search, previous, next, position, hunt, count;
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
void Color(Obj element,const char* property,float r,float g,float b) {
    float a=1;Ref color(api.New(api.Class("UnityEngine.CoreModule.dll","UnityEngine","Color"),{&r,&g,&b,&a}));
    Ref box(Style(element,property));api.Call(box.get(),"set_value",{api.object_unbox(color.get())});WriteStyle(element,property,box.get());
}
Obj Label(Obj parent, const std::string& text, bool muted=false) {
    Ref label(api.New(api.Class(Ui,UiSpace,"Label")));
    bool rich=false;api.Call(label.get(),"set_enableRichText",{&rich});
    Enum(label.get(),"whiteSpace",0); Pixels(label.get(),"fontSize",muted?12.f:13.f); Pixels(label.get(),"marginBottom",muted?0.f:4.f);
    for(auto p:{"paddingLeft","paddingRight"})Pixels(label.get(),p,0);
    if(muted)Color(label.get(),"color",.72f,.72f,.68f); else Color(label.get(),"color",.9f,.9f,.9f);
    Text(label.get(),text); Add(parent,label.get()); return label.get();
}
Obj Row(Obj parent) {
    Ref row(api.New(api.Class(Ui,UiSpace,"VisualElement")));
    Enum(row.get(),"flexDirection",2); Enum(row.get(),"alignItems",2); Pixels(row.get(),"marginBottom",6); Add(parent,row.get()); return row.get();
}
// The game's own button component: native hover, pressed and disabled states.
Obj Button(Obj parent, const std::string& text, int icon, int style, int status=0, const char* tooltip=nullptr) {
    Ref button(api.New(api.Class("Core.dll",Figma,"DofusButtonCustom")));
    int size=0; api.Call(button.get(),"set_size",{&size});
    api.Call(button.get(),"set_mainStyle",{&style});
    if(status)api.Call(button.get(),"set_status",{&status});
    if(icon)api.Call(button.get(),"set_icon",{&icon});
    if(!text.empty())Text(button.get(),text);
    if(tooltip)Text(button.get(),tooltip,"set_tooltip");
    Pixels(button.get(),"marginRight",6); Pixels(button.get(),"marginBottom",4);
    Add(parent,button.get()); return button.get();
}
// Native list rows (choice style) for the results: hover and activated states.
Obj ListRow(Obj parent) {
    Ref row(api.New(api.Class("Core.dll",Figma,"ListElementBasic")));
    Ref label(api.Field(row.get(),"m_textLabel"));
    bool rich=false,single=false;api.Call(label.get(),"set_enableRichText",{&rich});api.Call(label.get(),"set_isSingleLine",{&single});
    Enum(label.get(),"whiteSpace",0);
    int style=0,icon=IconTreasure; api.Call(row.get(),"set_listElementStyle",{&style}); api.Call(row.get(),"set_icon",{&icon});
    Pixels(row.get(),"minHeight",48); Pixels(row.get(),"minWidth",0); Float(row.get(),"flexGrow",1); Pixels(row.get(),"marginRight",6);
    Add(parent,row.get()); return row.get();
}
Obj Tab(Obj parent, const char* text) {
    Ref tab(api.New(api.Class("Core.dll",Figma,"DofusTab")));
    int size=1,caps=1; api.Call(tab.get(),"set_size",{&size}); api.Call(tab.get(),"set_fontCase",{&caps});
    Text(tab.get(),text); Float(tab.get(),"flexGrow",1); Pixels(tab.get(),"minWidth",0);
    Add(parent,tab.get()); return tab.get();
}
// Native titled section, opened while openable then locked (the native
// set_isOpen keeps the closed state while canBeOpened is off).
Obj Section(Obj parent, const char* title, int icon) {
    bool subContent=true,folderButton=true,no=false,hasIcon=true,open=true,openable=true,locked=false;
    Ref section(api.New(api.Class("Core.dll",Figma,"SectionHeader"),{&subContent,&folderButton,&no,&hasIcon,&no}));
    Text(section.get(),title,"set_title"); api.Call(section.get(),"set_icon",{&icon});
    api.Call(section.get(),"set_canBeOpened",{&openable}); api.Call(section.get(),"set_isOpen",{&open}); api.Call(section.get(),"set_canBeOpened",{&locked});
    Ref content(api.New(api.Class(Ui,UiSpace,"VisualElement")));
    for(auto p:{"paddingLeft","paddingRight"})Pixels(content.get(),p,4); Pixels(content.get(),"paddingTop",8);
    api.Call(section.get(),"AddSubContent",{content.get()});
    Add(parent,section.get()); return content.get();
}
Obj Input(Obj parent, const char* hint, const char* value, int maxLength) {
    Ref input(api.New(api.Class("Core.dll",Figma,"TextInput")));
    Text(input.get(),hint,"set_placeholderText"); Text(input.get(),hint,"set_tooltip");
    api.Call(input.get(),"set_maxLength",{&maxLength}); Text(input.get(),value);
    Pixels(input.get(),"minWidth",0); Pixels(input.get(),"minHeight",32); Float(input.get(),"flexGrow",1);
    Add(parent,input.get()); return input.get();
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
void Status(const std::string& text) { Text(state.status.get(),text); }
void Invalidate() {
    state.haveResults=false; state.clues.clear(); state.filtered.clear(); state.offset=0;
    if(state.job) state.job->cancelled.store(true);
    for(auto& line:state.lines) if(line.get()) Enum(line.get(),"display",1);
    if(state.previous.get()) Enabled(state.previous.get(),false);
    if(state.next.get()) Enabled(state.next.get(),false);
    if(state.count.get()) Text(state.count.get(),"");
}
void ShowRows() {
    for(size_t i=0;i<Rows;++i) {
        const size_t index=state.offset+i;
        const bool show=index<state.filtered.size();
        Enum(state.lines[i].get(),"display",show?0:1);
        if(show) {
            const auto& clue=state.filtered[index];
            const auto text=clue.name+" : ["+std::to_string(clue.x)+","+std::to_string(clue.y)+"] · "+std::to_string(clue.distance)+" carte(s)";
            Text(state.rows[i].get(),clue.name+"\n["+std::to_string(clue.x)+","+std::to_string(clue.y)+"] · "+std::to_string(clue.distance)+" carte(s)");
            Text(state.rows[i].get(),text+"\nCliquer pour utiliser ces coordonnées comme nouveau départ.","set_tooltip");
            Text(state.travel[i].get(),"Lancer le trajet vers ["+std::to_string(clue.x)+","+std::to_string(clue.y)+"] : zaaps, bateaux et /travel, comme le bouton TP du chat.","set_tooltip");
        }
    }
    Enabled(state.previous.get(),state.offset>0);
    Enabled(state.next.get(),state.offset+Rows<state.filtered.size());
    const auto total=state.filtered.size();
    Text(state.count.get(),total==0?std::string("Aucun indice"):total==1?std::string("1 indice"):std::to_string(total)+" indices");
    Status(state.filtered.empty() ?
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
    } catch(const std::exception& error) { Status(error.what()); return false; }
}
const char* DirectionName(int direction) {
    for(size_t i=0;i<4;++i)if(Directions[i]==direction)return DirectionNames[i];
    return "?";
}
// The four native direction tabs follow the current direction; a tab the
// player turns on becomes the direction (the native tabs toggle themselves).
void ShowDirection() {
    for(size_t i=0;i<4;++i){bool on=Directions[i]==state.directionValue;api.Call(state.direction[i].get(),"SetValueWithoutNotify",{&on});}
}
void PollDirections() {
    for(size_t i=0;i<4;++i) {
        if(Directions[i]==state.directionValue||!api.Value<bool>(api.Call(state.direction[i].get(),"get_isOn")))continue;
        state.directionValue=Directions[i]; ShowDirection();
        Invalidate(); Status("Direction modifiée. Clique sur Rechercher."); return;
    }
}
void Search();
bool ApplyHunt(bool search) {
    const auto step=treasure::CurrentStep();
    if(!step.active) { state.poiFilter=0; state.huntLabel.clear(); Status("Aucune chasse au trésor en cours. Renseigne le départ et la direction à la main."); return false; }
    if(step.direction!=0 && step.direction!=2 && step.direction!=4 && step.direction!=6) { Status("Étape de chasse sans direction exploitable."); return false; }
    Invalidate();
    state.directionValue=step.direction; ShowDirection(); state.poiFilter=step.poiId; Text(state.filter.get(),""); state.lastFilter.clear();
    const std::string target=step.poiId ? (step.hint.empty() ? "indice n°"+std::to_string(step.poiId) : step.hint) :
        step.type==5 ? "indice d’un PNJ (non recherché sur DofusDB)" : std::to_string(step.mapCount)+" carte(s) dans cette direction";
    state.huntLabel="Étape "+std::to_string(step.index)+" · "+DirectionName(step.direction)+" · "+target;
    if(!FillPosition()) return false;
    Status(state.huntLabel+(search?"":"\nDépart : ta position actuelle. Clique sur Étape en cours pour rechercher."));
    if(search && step.poiId) Search();
    return true;
}
void Search() {
    if(state.job || GetTickCount64()<state.nextRequest) return;
    if(activeJob && !activeJob->done) {
        Status("La recherche précédente se termine. Réessaie dans un instant."); return;
    }
    treasure::Query query;
    if(!ReadQuery(query)) { Status("Entre des coordonnées X et Y entières valides."); return; }
    Invalidate();
    auto job=std::make_shared<Job>(); job->query=query; state.job=activeJob=job;
    state.query=query; state.lastX=Read(state.x.get()); state.lastY=Read(state.y.get());
    state.nextRequest=GetTickCount64()+1500;
    Status("Recherche des indices DofusDB…"); Enabled(state.search.get(),false);
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
    int result=-1, travel=-1;
    for(size_t i=0;i<Rows;++i) { if(owner && owner==state.rows[i].get()) result=static_cast<int>(i); if(owner && owner==state.travel[i].get()) travel=static_cast<int>(i); }
    const bool own=owner && (result>=0 || travel>=0 || owner==state.search.get() || owner==state.closeButton.get() ||
        owner==state.previous.get() || owner==state.next.get() || owner==state.position.get() || owner==state.hunt.get());
    if(!own) return false;
    try {
        if(!wanted || !Current() || !api.Value<bool>(api.Call(owner,"get_enabledInHierarchy"))) return true;
        if(owner==state.closeButton.get()) { wanted=false; if(state.job)state.job->cancelled.store(true); return true; }
        if(owner==state.search.get()) Search();
        else if(owner==state.position.get()) { if(FillPosition()) { Invalidate(); Status("Position actuelle renseignée. Choisis la direction puis Rechercher."); } }
        else if(owner==state.hunt.get()) ApplyHunt(true);
        else if(travel>=0 && state.haveResults && state.offset+travel<state.filtered.size()) {
            const auto clue=state.filtered[state.offset+travel];
            try {
                // Same improved journey as the chat TP button: zaaps, boats, then /travel.
                const auto here=notebook::CurrentPosition();
                StartChatTravel({clue.x,clue.y,here.world});
                Status("Trajet lancé vers ["+std::to_string(clue.x)+","+std::to_string(clue.y)+"]. Clique le résultat pour en faire le prochain départ.");
            } catch(const std::exception& error) { Status(error.what()); }
        }
        else if(owner==state.previous.get() && state.offset>=Rows) {state.offset-=Rows;ShowRows();}
        else if(owner==state.next.get() && state.offset+Rows<state.filtered.size()) {state.offset+=Rows;ShowRows();}
        else if(result>=0 && state.haveResults && state.offset+result<state.filtered.size()) {
            treasure::Query current;
            if(!ReadQuery(current) || current!=state.query) { Invalidate(); return true; }
            const auto clue=state.filtered[state.offset+result];
            Text(state.x.get(),std::to_string(clue.x)); Text(state.y.get(),std::to_string(clue.y)); Text(state.filter.get(),"");
            Invalidate(); Status("Nouveau départ renseigné. Choisis la direction suivante.");
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
    bool yes=true; int icon=IconTreasure,size=1;
    api.Call(state.window.get(),"set_headerIcon",{&icon}); api.Call(state.window.get(),"set_headerSize",{&size});
    // The window's own close button and resizers, like the game's windows.
    api.Call(state.window.get(),"set_headerShowCloseButton",{&yes});
    for(const auto* setter:{"set_verticalResizable","set_horizontalResizable","set_cornerResizable"})
        api.Call(state.window.get(),setter,{&yes});
    api.Call(state.window.get(),"set_hasBackground",{&yes});
    Enum(state.window.get(),"position",1); Float(state.window.get(),"flexGrow",0);
    Pixels(state.window.get(),"width",520); Pixels(state.window.get(),"left",80); Pixels(state.window.get(),"top",90);
    Pixels(state.window.get(),"minWidth",440); Pixels(state.window.get(),"minHeight",320);
    state.header.reset(api.Call(state.window.get(),"get_headerWidget"));
    state.closeButton.reset(api.Field(state.header.get(),"closeButton"));
    if(!state.closeButton.get()) throw std::runtime_error("Treasure native close button unavailable");
    toolsUi::AttachHeader("treasure",state.window.get());
    Ref draggers(api.Field(state.window.get(),"m_registeredElementDraggers"));
    state.dragger.reset(api.Call(draggers.get(),"get_Item",{state.header.get()}));
    Retain("m_elementDraggers"); Retain("m_resizerList");
    const char* getters[]={"ydf","ydh","ydj","ydl"};
    for(size_t i=0;i<4;++i) state.inputEvents[i].reset(api.Call(input.get(),getters[i]));
    api.Call(state.window.get(),"Init",{state.dependencies.get()});
    Ref content(toolsUi::ScrollBody(state.window.get(),root,true));
    Float(content.get(),"flexGrow",1);
    for(const auto* property:{"paddingLeft","paddingRight","paddingTop","paddingBottom"}) Pixels(content.get(),property,10);
    // 1. Start: coordinates, current position, direction tabs.
    Ref departure(Section(content.get(),"Point de départ",IconStart));
    Ref coords(Row(departure.get()));
    state.x.reset(Input(coords.get(),"X","",5));Pixels(state.x.get(),"width",64);Float(state.x.get(),"flexGrow",0);Pixels(state.x.get(),"marginRight",6);
    state.y.reset(Input(coords.get(),"Y","",5));Pixels(state.y.get(),"width",64);Float(state.y.get(),"flexGrow",0);Pixels(state.y.get(),"marginRight",8);
    state.position.reset(Button(coords.get(),"Ma position",IconPlayer,2,0,"Reprendre les coordonnées de la carte où tu te trouves."));Pixels(state.position.get(),"marginBottom",0);
    Ref directions(Row(departure.get()));
    for(size_t i=0;i<4;++i) state.direction[i].reset(Tab(directions.get(),DirectionNames[i]));
    ShowDirection();
    // 2. Clue: filter text, hunt step, search.
    Ref clueSection(Section(content.get(),"Indice à trouver",IconSearch));
    state.filter.reset(Input(clueSection.get(),"Nom de l’indice (vide : tous les indices)","",100));
    Pixels(state.filter.get(),"marginBottom",8);
    Ref actions(Row(clueSection.get()));
    state.hunt.reset(Button(actions.get(),"Étape en cours",IconTreasure,1,0,"Reprend ta position, la direction et l’indice de la chasse au trésor en cours, puis lance la recherche."));
    state.search.reset(Button(actions.get(),"Rechercher les indices",IconSearch,0,0,"Chercher sur DofusDB les indices dans cette direction depuis le départ."));
    // 3. Results: native rows (click = next start) with a journey button each.
    Ref results(Section(content.get(),"Résultats",IconList));
    for(size_t i=0;i<Rows;++i) {
        state.lines[i].reset(Row(results.get())); Enum(state.lines[i].get(),"display",1);
        state.rows[i].reset(ListRow(state.lines[i].get()));
        state.travel[i].reset(Button(state.lines[i].get(),"Chemin",IconZaap,2,0,"Lancer le trajet vers cet indice."));Pixels(state.travel[i].get(),"marginRight",0);Pixels(state.travel[i].get(),"marginBottom",0);
    }
    Ref pages(Row(results.get()));Pixels(pages.get(),"marginTop",4);
    state.previous.reset(Button(pages.get(),"",IconPrevious,2,0,"Indices précédents"));
    state.count.reset(Label(pages.get(),"",true));Float(state.count.get(),"flexGrow",1);Enum(state.count.get(),"unityTextAlign",4);
    state.next.reset(Button(pages.get(),"",IconNext,2,0,"Indices suivants"));Pixels(state.next.get(),"marginRight",0);
    Enabled(state.previous.get(),false); Enabled(state.next.get(),false);
    Ref footer(Row(content.get()));Pixels(footer.get(),"marginTop",6);Pixels(footer.get(),"marginBottom",0);
    state.status.reset(Label(footer.get(),"Renseigne le départ et la direction, puis lance la recherche.",true));Float(state.status.get(),"flexGrow",1);Float(state.status.get(),"flexShrink",1);
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
    api.FindVerified(api.Class(Ui,UiSpace,"TextElement"),"set_enableRichText",1,0x564B1E0);
    unsubscribe=api.FindVerified(api.Class("Core.dll","Core.Engine.Inputs.Events","InputCustomEvent"),"UnSubscribe",1,0x20FF620);
    // The same native components as the notebook (verified once more here).
    auto tab=api.Class("Core.dll",Figma,"DofusTab");
    api.FindVerified(tab,".ctor",0,0x1712DC0);api.FindVerified(tab,"set_text",1,0x17136F0);api.FindVerified(tab,"set_size",1,0x1713550);
    api.FindVerified(tab,"set_fontCase",1,0x1712FC0);api.FindVerified(tab,"get_isOn",0,0x1712F50);api.FindVerified(tab,"SetValueWithoutNotify",1,0x1711970);
    auto button=api.Class("Core.dll",Figma,"DofusButtonCustom");
    api.FindVerified(button,".ctor",0,0x170D6F0);api.FindVerified(button,"set_icon",1,0x170DFE0);api.FindVerified(button,"set_text",1,0x170E5F0);
    api.FindVerified(button,"set_mainStyle",1,0x170E210);api.FindVerified(button,"set_size",1,0x170E330);api.FindVerified(button,"set_status",1,0x170E400);
    auto section=api.Class("Core.dll",Figma,"SectionHeader");
    api.FindVerified(section,".ctor",5,0x1745F00);api.FindVerified(section,"set_title",1,0x17467B0);api.FindVerified(section,"set_icon",1,0x1746200);
    api.FindVerified(section,"set_isOpen",1,0x17462F0);api.FindVerified(section,"set_canBeOpened",1,0x1746090);api.FindVerified(section,"AddSubContent",1,0x1744360);
    auto listElement=api.Class("Core.dll",Figma,"ListElementBase");
    api.FindVerified(listElement,"set_icon",1,0x1733BD0);
    api.FindVerified(api.Class("Core.dll",Figma,"ListElementBasic"),"set_listElementStyle",1,0x1734700);
    api.FindVerified(api.Class("Core.dll","Core.UILogic.Components","ResizableWindow"),"set_headerShowCloseButton",1,0x15BFB50);
    // Shared window/control contracts are verified by InstallSoulHelper first.
    soul::SetAuxiliaryPanelAction(Action); nativeClick::AddHandler(Action);
    toolsUi::Register("treasure","Chasse au trésor · DofusDB",IconTreasure,[]{return wanted;},[]{wanted=!wanted;if(!wanted&&state.job)state.job->cancelled.store(true);return true;});installed=true;
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
    PollDirections();
    const auto x=Read(state.x.get()),y=Read(state.y.get()),filter=Read(state.filter.get());
    if(x!=state.lastX || y!=state.lastY) {
        Invalidate(); state.lastX=x; state.lastY=y;
        Status("Départ modifié. Clique sur Rechercher.");
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
                Status(job->error);
                Log("TREASURE_PANEL_PROVIDER_ERROR "+job->error);
            }
        }
    }
    Enabled(state.search.get(),!state.job && GetTickCount64()>=state.nextRequest);
    if(filter!=state.lastFilter) {
        state.lastFilter=filter; state.offset=0;
        if(state.haveResults) {state.filtered=Filtered(filter);ShowRows();}
    }
}
