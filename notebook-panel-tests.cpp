#include "tools-ui.hpp"
#include "native-click.hpp"
#include "native-panel-test-fixture.hpp"
#include "notebook-panel.hpp"
#include "notebook-native.hpp"

int travels=0,prepares=0,copies=0;
std::string prepared;
notebook::Entry travelled;
namespace notebook {
void InstallNative() {}
Entry CurrentPosition(){Entry result;result.x=-25;result.y=36;result.world=1;result.map=987;return result;}
void Travel(const Entry& entry){++travels;travelled=entry;}
void PrepareMessage(const std::string& text){++prepares;prepared=text;}
void Copy(const std::string&){++copies;}
}
namespace soul {
managed::Obj CreateSpellView(managed::Obj parent){auto slot=fake::Element("SpellSlot");fake::Add(parent,slot);return slot;}
bool UpdateSpellView(managed::Obj,bool){return true;}
void ReleaseSpellView(){}
}
using namespace fake;
Obj FindText(Obj root,const std::string& text){
    if(Get(root).text==text)return root;
    for(auto child:Get(root).children)if(auto found=FindText(child,text))return found;
    return nullptr;
}
Obj FindType(Obj root,const char* type,const std::string& text){
    if(Get(root).type->name==type&&Get(root).text==text)return root;
    for(auto child:Get(root).children)if(auto found=FindType(child,type,text))return found;
    return nullptr;
}
int Count(Obj root,const char* type){int n=Get(root).type->name==type?1:0;for(auto child:Get(root).children)n+=Count(child,type);return n;}
std::vector<Obj> Inputs(Obj root){
    std::vector<Obj> result;if(Get(root).type->name=="TextInput")result.push_back(root);
    for(auto child:Get(root).children){auto nested=Inputs(child);result.insert(result.end(),nested.begin(),nested.end());}return result;
}
// Native rows arrive through the ListElement action hook, native buttons
// through the ExtendedClickable up-event hook, tabs toggle themselves.
void Click(Obj object){
    Check(!!object,"Missing notebook action");
    if(Get(object).type->name=="DofusButtonCustom"){Check(!!clickHook,"Native button hook installed");clickHook(Get(object).fields.at("m_clickable"),Make("PointerUpEvent"),Vec2{},0,nullptr);}
    else actionHook(object,nullptr);
}
void ClickTab(Obj window,const char* text){auto tab=FindType(window,"DofusTab",text);Check(!!tab,"Missing native tab");Get(tab).enums["isOn"]=1;TickNotebookPanel();}
bool TabOn(Obj window,const char* text){return Get(FindType(window,"DofusTab",text)).enums["isOn"]==1;}
Obj CloseButton(Obj window){return Get(Get(window).fields.at("header")).fields.at("closeButton");}
int main(){
    const auto directory=std::filesystem::temp_directory_path()/("DofusNotebookPanelTests-"+std::to_string(GetCurrentProcessId()));
    try {
        std::filesystem::create_directory(directory);Initialize();treasureFixture=true;notebookFixture=true;
        Module owner{directory};module=&owner;owner.settingsPath=directory/"settings.ini";
        owner.ready=owner.servicesReady=true;owner.cached.reset(Make("Cache"));owner.compositor.reset(Make("Compositor"));
        contextService=Make("ContextService");dependencies=Make("goc");input=Make("eqo");Get(dependencies).fields["cached"]=owner.cached.get();
        for(const auto* key:{"ydf","ydh","ydj","ydl"}){
            auto event=Make("InputCustomEvent"),list=Make("List"),foreign=Make("Action");
            Get(foreign).fields["target"]=Make("ForeignDragger");Get(list).children={foreign};
            Get(event).fields["m_input"]=list;Get(input).fields[key]=event;
        }
        nativeRoot=Element("Document");Get(nativeRoot).fields["panel"]=Make("Panel");
        auto layers=Element("gmy"),service=Make("gmo"),hud=Element("Root"),middle=Element("Root");
        Get(hud).name="RootLow";Get(middle).name="RootMedium";Add(nativeRoot,layers);Add(layers,hud);Add(layers,middle);
        Get(nativeRoot).fields["layersService"]=service;Get(service).fields["dzrv"]=layers;
        soul::Panel{}.Install(Equip,Save,HideModule);nativeClick::Install();toolsUi::Install();InstallNotebookPanel();TickNotebookPanel();
        Check(Get(middle).children.empty(),"No panel until explicit open");
        OpenNotebookPanel();TickNotebookPanel();auto window=latestWindow;auto inputs=Inputs(window);
        Check(inputs.size()==5&&Get(window).fields["parent"]==middle,"Native window with title, xyz and body");
        Check(Get(window).enums["set_headerShowCloseButton"]==1&&Get(window).enums["SetHeaderSize"]==0,"The window shows its native close button and keeps the full header while the setting is off");
        Check(Count(window,"DofusTab")==3&&TabOn(window,"Positions")&&!TabOn(window,"Notes")&&!TabOn(window,"Messages"),"Three native tabs with Positions selected");
        Check(Get(FindType(window,"DofusTab","Notes")).enums["set_fontCase"]==1&&Get(FindType(window,"DofusTab","Notes")).enums["set_size"]==1,"Tabs use the game's capitalised medium style");
        Check(Count(window,"DofusButtonCustom")==12&&Count(window,"SectionHeader")==2&&Count(window,"Divider")==2&&Count(window,"ListElementBasic")==6,"Native buttons, two titled sections, dividers and six list rows");
        Check(!FindText(window,"Fermer")&&FindText(window,"Nouvelle entrée")&&FindText(window,"Ma position")&&FindText(window,"Actualiser"),"No custom close row; native action buttons");
        Check(Get(FindText(window,"Enregistrer")).enums["set_mainStyle"]==0&&Get(FindText(window,"Supprimer")).enums["set_status"]==1&&Get(FindText(window,"Enregistrer")).enums["set_icon"]==148,"Primary save, negative delete, floppy icon");
        Check(FindText(window,"Mes entrées")&&FindText(window,"Détail")&&FindText(window,"Aucune entrée"),"Section titles and an empty-list counter");
        for(const auto* title:{"Mes entrées","Détail"}){auto section=FindType(window,"SectionHeader",title);Check(section&&Get(section).enums["set_isOpen"]==1&&Get(section).enums["set_canBeOpened"]==0,"Sections opened while openable, then locked: their content (new-entry button, rows, form) is shown");}
        OpenNotebookPanel();TickNotebookPanel();Check(window==latestWindow,"Open must reuse window");
        Get(inputs[0]).text="Mon zaap <b>littéral</b>";Click(FindText(window,"Ma position"));
        Check(Get(inputs[1]).text=="-25"&&Get(inputs[2]).text=="36","Current position captured");
        Click(FindText(window,"Enregistrer"));
        const auto path=notebook::DataPath(owner.settingsPath);
        auto data=notebook::Read(path);Check(data.entries.size()==1&&data.entries[0].map==987,"Position saved with map identity");
        Check(!travels&&!prepares,"Saving never travels or sends/prepares");
        auto row=FindText(window,"[-25,36]  Mon zaap <b>littéral</b>");
        Check(row&&Get(Get(row).fields["m_textLabel"]).enums["set_enableRichText"]==0,"User text displayed literally");
        Check(Get(row).enums["set_listElementStyle"]==0&&Get(row).enums["set_icon"]==225&&Get(row).enums["set_isActivated"]==1&&FindText(window,"1 entrée"),"Choice row with the position icon, activated for the saved entry");
        Click(row);Click(FindText(window,"Voyager"));Check(travels==1&&travelled.x==-25,"Explicit travel only");
        ClickTab(window,"Notes");Check(Get(inputs[4]).enums["set_isMultiline"]==1&&TabOn(window,"Notes")&&!TabOn(window,"Positions"),"Notes use native multiline input and the tab switches");
        Get(inputs[0]).text="Plan";Get(inputs[4]).text="Ligne un\nLigne deux";
        ClickTab(window,"Messages");Check(Get(inputs[0]).text=="Plan"&&!TabOn(window,"Messages")&&TabOn(window,"Notes"),"Unsaved tab change is refused and the tab snaps back without destroying text");
        Click(FindText(window,"Enregistrer"));Check(notebook::Read(path).entries.size()==2,"Multiline note saved");
        ClickTab(window,"Messages");Check(TabOn(window,"Messages"),"After saving the tab change goes through");
        Get(inputs[0]).text="Salut";Get(inputs[4]).text="Bonjour groupe";
        Click(FindText(window,"Enregistrer"));Click(FindText(window,"Préparer dans le chat"));
        Check(prepares==1&&prepared=="Bonjour groupe","Only explicit prepare action touches draft");
        Click(FindText(window,"Supprimer"));Check(notebook::Read(path).entries.size()==3,"First delete requires confirmation");
        Click(FindText(window,"Confirmer la suppression"));Check(notebook::Read(path).entries.size()==2,"Confirmed delete");
        Get(inputs[0]).text="Brouillon";Get(inputs[4]).text="À garder";
        const auto ups=buttonUps;
        Click(CloseButton(window));TickNotebookPanel();Check(Panel(window)&&buttonUps==ups+1,"Unsaved close through the native close button does not lose the draft; the game's own handling still ran");
        Click(CloseButton(window));TickNotebookPanel();Check(!Panel(window),"Second close explicitly discards");
        for(const auto* key:{"ydf","ydh","ydj","ydl"})
            Check(Get(Get(Get(input).fields[key]).fields["m_input"]).children.size()==1,"Owned input subscriptions removed");
        owner.settings.smallHeaderNotebook=true;
        OpenNotebookPanel();TickNotebookPanel();window=latestWindow;
        Check(Get(window).enums["SetHeaderSize"]==1,"A reopened window applies the saved reduced header");
        ClickTab(window,"Notes");Check(FindText(window,"Plan")!=nullptr,"Saved content survives reopen");
        const auto before=nativeActions;Click(Element("ForeignAction"));Check(nativeActions==before+1,"Unrelated native actions preserved");
        auto foreign=NativeButton();const auto foreignUps=buttonUps;clickHook(Get(foreign).fields.at("m_clickable"),Make("PointerUpEvent"),Vec2{},0,nullptr);
        Check(buttonUps==foreignUps+1&&Panel(window),"A foreign native button click is passed through untouched");
        auto header=Get(window).fields["header"];capture=header;globalDragging=true;failOwner=window;failMethod="RemoveFromHierarchy";
        RestoreNotebookPanel();Check(NotebookPanelCleanupPending()&&!capture&&!globalDragging,"Cleanup releases owned drag and retries detach");
        RestoreNotebookPanel();Check(!NotebookPanelCleanupPending(),"Cleanup retry completes");
        OpenNotebookPanel();TickNotebookPanel();window=latestWindow;owner.cached.reset(Make("OtherCache"));TickNotebookPanel();
        Check(!Panel(window),"Session replacement retires notebook");
        owner.cached.reset(Get(dependencies).fields["cached"]);initFailures=1;OpenNotebookPanel();TickNotebookPanel();
        Check(!NotebookPanelCleanupPending(),"Partial creation failure doesn't stop module or leak native state");
        toolsUi::Restore();owner.cached.reset();owner.compositor.reset();module=nullptr;Check(handles.empty(),"No GC handles leaked");
        std::filesystem::remove_all(directory);
        std::cout<<"PASS: notebook native tabs/buttons/sections, draft protection, explicit travel/chat, literal text and cleanup\n";return 0;
    }catch(const std::exception& error){std::cerr<<"FAIL: "<<error.what()<<'\n';for(const auto& message:logs)std::cerr<<message<<'\n';return 1;}
}
