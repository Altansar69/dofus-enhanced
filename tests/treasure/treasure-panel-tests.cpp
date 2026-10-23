#include "ui/tools-ui.hpp"
#include "ui/native-click.hpp"
#include "support/native-panel-test-fixture.hpp"
#include "treasure/treasure-launch.hpp"
#include "treasure/treasure-data.hpp"
#include "treasure/treasure-hunt.hpp"
#include "notebook/notebook-native.hpp"
#include "travel/chat-travel.hpp"
#include <atomic>
#include <thread>

std::atomic_int fetches{0},finished{0};
std::atomic_bool hold{false},failFetch{false};
namespace treasure {
std::vector<Clue> Fetch(const Query& query,const std::atomic_bool& cancelled) {
    ++fetches;
    while(hold && !cancelled) Sleep(1);
    ++finished;
    if(failFetch)throw std::runtime_error("Provider unavailable");
    return {{1,query.x+1,query.y,1,"Épée"}, {2,query.x+2,query.y,2,"Crâne décoré"}};
}
}
namespace {
bool positionAvailable=false; int travels=0; ChatTpTarget lastTravel; treasure::Step fakeStep;
}
namespace notebook {
Entry CurrentPosition() { if(!positionAvailable) throw std::runtime_error("Position actuelle indisponible."); Entry here; here.map=123; here.x=-25; here.y=-36; here.world=1; return here; }
}
void StartChatTravel(ChatTpTarget target) { ++travels; lastTravel=target; }
namespace treasure { Step CurrentStep() { return fakeStep; } }
namespace soul {
managed::Obj CreateSpellView(managed::Obj parent) { auto slot=fake::Element("SpellSlot");fake::Add(parent,slot);return slot; }
bool UpdateSpellView(managed::Obj,bool) {return true;}
void ReleaseSpellView() {}
}
using namespace fake;
Obj FindText(Obj root,const std::string& text) {
    if(Get(root).text==text)return root;
    for(auto child:Get(root).children) if(auto found=FindText(child,text))return found;
    return nullptr;
}
Obj FindType(Obj root,const char* type,const std::string& text){
    if(Get(root).type->name==type&&Get(root).text==text)return root;
    for(auto child:Get(root).children)if(auto found=FindType(child,type,text))return found;
    return nullptr;
}
int Count(Obj root,const char* type){int n=Get(root).type->name==type?1:0;for(auto child:Get(root).children)n+=Count(child,type);return n;}
std::vector<Obj> Inputs(Obj root) {
    std::vector<Obj> result;
    if(Get(root).type->name=="TextInput")result.push_back(root);
    for(auto child:Get(root).children) {auto nested=Inputs(child);result.insert(result.end(),nested.begin(),nested.end());}
    return result;
}
int clicks=0;
// Native buttons report through the shared button hook; list rows and other
// elements through the shared panel action hook.
void Click(Obj object) {
    ++clicks;
    if(!object)throw std::runtime_error("Missing native action at click #"+std::to_string(clicks));
    if(Get(object).type->name=="DofusButtonCustom"){Check(!!clickHook,"Native button hook installed");clickHook(Get(object).fields.at("m_clickable"),Make("PointerUpEvent"),Vec2{},0,nullptr);}
    else actionHook(object,nullptr);
}
Obj CloseButton(Obj window){return Get(Get(window).fields.at("header")).fields.at("closeButton");}
bool TabOn(Obj window,const char* text){return Get(FindType(window,"DofusTab",text)).enums["isOn"]==1;}
int ResultLogs(){int n=0;for(const auto& m:logs)if(m.rfind("TREASURE_PANEL_RESULTS",0)==0||m.rfind("TREASURE_PANEL_PROVIDER_ERROR",0)==0)++n;return n;}
void AwaitResult() {
    // The worker flags completion after the fake fetch returns; keep ticking
    // until the panel consumed the job (or, for a rejected job, a short grace).
    const int before=ResultLogs();
    for(int i=0;i<500;++i){TickTreasurePanel();if(finished==fetches&&fetches>0)break;Sleep(2);}
    if(!(finished==fetches&&fetches>0))throw std::runtime_error("Worker timeout");
    for(int i=0;i<200&&ResultLogs()==before;++i){Sleep(2);TickTreasurePanel();}
    TickTreasurePanel();
}
int main() {
    try {
        Initialize();treasureFixture=true;
        Module owner{std::filesystem::path{}};module=&owner;
        owner.ready=owner.servicesReady=true;owner.cached.reset(Make("Cache"));owner.compositor.reset(Make("Compositor"));
        contextService=Make("ContextService");dependencies=Make("goc");input=Make("eqo");Get(dependencies).fields["cached"]=owner.cached.get();
        for(const auto* key:{"ydf","ydh","ydj","ydl"}) {
            auto event=Make("InputCustomEvent"),list=Make("List"),foreign=Make("Action");
            Get(foreign).fields["target"]=Make("ForeignDragger");Get(list).children={foreign};
            Get(event).fields["m_input"]=list;Get(input).fields[key]=event;
        }
        nativeRoot=Element("Document");Get(nativeRoot).fields["panel"]=Make("Panel");
        auto layers=Element("gmy"),service=Make("gmo"),hud=Element("Root"),middle=Element("Root");
        Get(hud).name="RootLow";Get(middle).name="RootMedium";
        Add(nativeRoot,layers);Add(layers,hud);Add(layers,middle);
        Get(nativeRoot).fields["layersService"]=service;Get(service).fields["dzrv"]=layers;
        soul::Panel{}.Install(Equip,Save,HideModule);
        nativeClick::Install();toolsUi::Install();InstallTreasurePanel();TickTreasurePanel();
        Check(Get(middle).children.empty()&&fetches==0,"Opening settings must not start a panel or query");
        OpenTreasureOverlay({});TickTreasurePanel();
        auto window=latestWindow;Check(Get(window).fields["parent"]==middle,"Panel must use native normal-window layer");
        auto inputs=Inputs(window);Check(inputs.size()==3,"Manual X, Y and clue controls");
        Check(Get(window).enums["set_headerShowCloseButton"]==1&&Get(window).enums["set_verticalResizable"]==1&&Get(window).enums["set_horizontalResizable"]==1&&Get(window).enums["set_cornerResizable"]==1,"Native close button and resizers like the game's windows");
        Check(Count(window,"SectionHeader")==3&&Count(window,"DofusTab")==4&&Count(window,"DofusButtonCustom")==11&&Count(window,"ListElementBasic")==4,"Three native sections, four direction tabs, nine native buttons (plus the two header buttons) and four result rows");
        Check(!FindText(window,"Fermer")&&FindText(window,"Ma position")&&FindText(window,"Étape en cours")&&FindText(window,"Rechercher les indices"),"No custom close row; native action buttons");
        Check(TabOn(window,"Est")&&!TabOn(window,"Nord"),"The default direction tab is on");
        OpenTreasureOverlay({});TickTreasurePanel();Check(latestWindow==window,"Repeated open must reuse panel");
        Click(FindText(window,"Rechercher les indices"));Check(fetches==0,"Blank coordinates must not query");
        Get(FindType(window,"DofusTab","Ouest")).enums["isOn"]=1;TickTreasurePanel();
        Check(TabOn(window,"Ouest")&&!TabOn(window,"Est")&&FindText(window,"Direction modifiée. Clique sur Rechercher."),"A direction tab the player turns on becomes the direction and the others turn off");
        Get(inputs[0]).text="-25";Get(inputs[1]).text="-36";Get(inputs[2]).text="epee";
        Click(FindText(window,"Rechercher les indices"));AwaitResult();
        Check(fetches==1,"Exactly one explicit request");
        auto result=FindText(window,"Épée\n[-24,-36] · 1 carte(s)");Check(!!result&&Get(result).type->name=="ListElementBasic"&&Get(result).enums["set_icon"]==317,"Native result row must show next coordinates with the treasure icon");
        Check(FindText(window,"1 indice")!=nullptr,"Result counter");
        Click(result);Check(Get(inputs[0]).text=="-24"&&Get(inputs[1]).text=="-36","Selection prepares next start");
        Check(fetches==1,"Selecting result must not automate the next search");
        const auto before=nativeActions;Click(Element("ForeignAction"));Check(nativeActions==before+1,"Foreign native action forwarded");
        Click(CloseButton(window));TickTreasurePanel();
        Check(!Panel(window)&&!TreasurePanelCleanupPending(),"The native close button detaches the window");
        for(const auto* key:{"ydf","ydh","ydj","ydl"})
            Check(Get(Get(Get(input).fields[key]).fields["m_input"]).children.size()==1,"Only foreign input delegate remains");
        // Running hunt: the panel starts from the player's map and the game's own step.
        positionAvailable=true;fakeStep={true,3,1,4,2,0,0,"Crâne décoré"};
        OpenTreasureOverlay({});TickTreasurePanel();window=latestWindow;inputs=Inputs(window);
        Check(Get(inputs[0]).text=="-25"&&Get(inputs[1]).text=="-36"&&Get(inputs[2]).text.empty()&&fetches==1,"Opening fills the start from the current map without querying");
        Check(FindText(window,"Étape 3 · Ouest · Crâne décoré\nDépart : ta position actuelle. Clique sur Étape en cours pour rechercher."),"The running hunt step is described");
        Check(TabOn(window,"Ouest")&&!TabOn(window,"Est"),"The hunt step selects its direction tab");
        Get(inputs[0]).text="0";Get(inputs[1]).text="0";TickTreasurePanel();
        Click(FindText(window,"Ma position"));Check(Get(inputs[0]).text=="-25"&&Get(inputs[1]).text=="-36","Position button fills the start from the current map");
        Click(FindText(window,"Étape en cours"));AwaitResult();
        Check(fetches==2,"The hunt step launches exactly one search");
        Check(FindText(window,"Crâne décoré\n[-23,-36] · 2 carte(s)")&&!FindText(window,"Épée\n[-24,-36] · 1 carte(s)"),"Hunt results are filtered to the step's point of interest");
        Click(FindText(window,"Chemin"));
        Check(travels==1&&lastTravel==ChatTpTarget{-23,-36,1},"Path button starts the improved journey to the clue");
        Get(inputs[2]).text="epee";TickTreasurePanel();
        Check(FindText(window,"Épée\n[-24,-36] · 1 carte(s)"),"Typing a clue overrides the hunt filter");
        positionAvailable=false;Click(FindText(window,"Chemin"));Check(travels==1,"Unavailable position never starts a journey");
        fakeStep={};Click(CloseButton(window));TickTreasurePanel();
        OpenTreasureOverlay({});TickTreasurePanel();window=latestWindow;inputs=Inputs(window);
        Get(inputs[0]).text="2";Get(inputs[1]).text="3";hold=true;
        Click(FindText(window,"Rechercher les indices"));
        for(int i=0;i<100&&fetches<3;++i)Sleep(1);
        Get(inputs[0]).text="4";TickTreasurePanel();hold=false;AwaitResult();
        Check(!FindText(window,"Épée\n[3,3] · 1 carte(s)"),"Changed input rejects stale response");
        // Cleanup must release owned drag and retry transient native failures.
        auto header=Get(window).fields["header"];capture=header;globalDragging=true;
        failOwner=window;failMethod="RemoveFromHierarchy";
        RestoreTreasurePanel();Check(TreasurePanelCleanupPending(),"Transient detach must remain pending");
        Check(!capture&&!globalDragging,"Close releases only owned capture/drag");
        RestoreTreasurePanel();Check(!TreasurePanelCleanupPending()&&!Panel(window),"Cleanup retry completes");
        OpenTreasureOverlay({});TickTreasurePanel();window=latestWindow;
        owner.cached.reset(Make("OtherCache"));TickTreasurePanel();
        Check(!Panel(window),"Session replacement detaches old panel");
        owner.cached.reset(Get(dependencies).fields["cached"]);
        initFailures=1;OpenTreasureOverlay({});
        bool rejected=false;try{TickTreasurePanel();}catch(...){rejected=true;}
        Check(rejected&&!TreasurePanelCleanupPending(),"Partial native Init safely rolled back");
        toolsUi::Restore();owner.cached.reset();owner.compositor.reset();module=nullptr;
        Check(handles.empty(),"Panel leaked GC handles");
        std::cout<<"PASS: native treasure panel (sections, direction tabs, buttons, rows), manual actions, hunt step, journey button, result filtering, stale requests, session replacement and retryable cleanup\n";
        return 0;
    } catch(const std::exception& e) {
        std::cerr<<"FAIL: "<<e.what()<<'\n';for(const auto& message:logs)std::cerr<<message<<'\n';return 1;
    }
}
