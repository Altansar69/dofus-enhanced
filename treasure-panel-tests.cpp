#include "tools-ui.hpp"
#include "native-panel-test-fixture.hpp"
#include "treasure-launch.hpp"
#include "treasure-data.hpp"
#include "treasure-hunt.hpp"
#include "notebook-native.hpp"
#include "chat-travel.hpp"
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
std::vector<Obj> Inputs(Obj root) {
    std::vector<Obj> result;
    if(Get(root).type->name=="TextInput")result.push_back(root);
    for(auto child:Get(root).children) {auto nested=Inputs(child);result.insert(result.end(),nested.begin(),nested.end());}
    return result;
}
void Click(Obj object) {Check(!!object,"Missing native action");actionHook(object,nullptr);}
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
        toolsUi::Install();InstallTreasurePanel();TickTreasurePanel();
        Check(Get(middle).children.empty()&&fetches==0,"Opening settings must not start a panel or query");
        OpenTreasureOverlay({});TickTreasurePanel();
        auto window=latestWindow;Check(Get(window).fields["parent"]==middle,"Panel must use native normal-window layer");
        auto inputs=Inputs(window);Check(inputs.size()==3,"Manual X, Y and clue controls");
        OpenTreasureOverlay({});TickTreasurePanel();Check(latestWindow==window,"Repeated open must reuse panel");
        Click(FindText(window,"Rechercher les indices"));Check(fetches==0,"Blank coordinates must not query");
        Get(inputs[0]).text="-25";Get(inputs[1]).text="-36";Get(inputs[2]).text="epee";
        Click(FindText(window,"Rechercher les indices"));AwaitResult();
        Check(fetches==1,"Exactly one explicit request");
        auto result=FindText(window,"Épée\n[-24,-36] · 1 carte(s)");Check(!!result,"Native result must show next coordinates");
        Click(result);Check(Get(inputs[0]).text=="-24"&&Get(inputs[1]).text=="-36","Selection prepares next start");
        Check(fetches==1,"Selecting result must not automate the next search");
        const auto before=nativeActions;Click(Element("ForeignAction"));Check(nativeActions==before+1,"Foreign native action forwarded");
        auto oldClose=FindText(window,"Fermer");Click(oldClose);TickTreasurePanel();
        Check(!Panel(window)&&!TreasurePanelCleanupPending(),"Close detaches native window");
        for(const auto* key:{"ydf","ydh","ydj","ydl"})
            Check(Get(Get(Get(input).fields[key]).fields["m_input"]).children.size()==1,"Only foreign input delegate remains");
        // Running hunt: the panel starts from the player's map and the game's own step.
        positionAvailable=true;fakeStep={true,3,1,4,2,0,0,"Crâne décoré"};
        OpenTreasureOverlay({});TickTreasurePanel();window=latestWindow;inputs=Inputs(window);
        Check(Get(inputs[0]).text=="-25"&&Get(inputs[1]).text=="-36"&&Get(inputs[2]).text.empty()&&fetches==1,"Opening fills the start from the current map without querying");
        Check(FindText(window,"Étape 3 · Ouest · Crâne décoré\nDépart : ta position actuelle. Clique sur Étape en cours pour rechercher."),"The running hunt step is described");
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
        fakeStep={};Click(FindText(window,"Fermer"));TickTreasurePanel();
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
        std::cout<<"PASS: native panel, manual actions, hunt step, journey button, result filtering, stale requests, session replacement and retryable cleanup\n";
        return 0;
    } catch(const std::exception& e) {
        std::cerr<<"FAIL: "<<e.what()<<'\n';for(const auto& message:logs)std::cerr<<message<<'\n';return 1;
    }
}
