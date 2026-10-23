#include "ui/tools-ui.hpp"
#include "ui/native-click.hpp"
#include "support/native-panel-test-fixture.hpp"
#include "notebook/notebook-panel.hpp"
#include "notebook/notebook-native.hpp"
#include <algorithm>
#include <fstream>

int travels=0,prepares=0,copies=0,sends=0,sentChannel=-1;
std::string prepared,sent;
notebook::Entry travelled;
namespace notebook {
void InstallNative() {}
Entry CurrentPosition(){Entry result;result.x=-25;result.y=36;result.world=1;result.map=987;return result;}
void Travel(const Entry& entry){++travels;travelled=entry;}
void PrepareMessage(const std::string& text){++prepares;prepared=text;}
void SendChatMessage(const std::string& text,int channel){++sends;sent=text;sentChannel=channel;}
std::vector<World> Worlds(){return {{1,"Monde des Douze"},{2,"Incarnam"},{3,"Srambad"}};}
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
// The window's ⋯ menu, opened through the native populate hook; a row is
// clicked through the shared native callback router, like the game does.
struct LayoutMenu {Obj menu=nullptr,root=nullptr;std::vector<Obj> rows;};
LayoutMenu OpenMenu(Obj window){
    auto button=Get(Get(window).fields.at("header")).fields.at("rightButton");
    LayoutMenu m;m.menu=Make("Menu");m.root=Element("MenuRoot");auto event=Make("PopulateEvent");
    Get(m.menu).fields["menuItems"]=Make("List");Get(m.menu).fields["dofusContextualMenuRoot"]=m.root;
    Get(m.root).fields["menuData"]=m.menu;Get(event).fields["menu"]=m.menu;Add(nativeRoot,m.root);
    populateHook(Get(button).fields.at("manipulator"),event,nullptr);
    m.rows=Get(Get(m.menu).fields.at("menuItems")).children;return m;
}
bool Pick(const LayoutMenu& m,size_t row){
    auto view=Element("MenuView");Get(view).fields["itemData"]=m.rows.at(row);Add(m.root,view);
    return soul::HandlePanelMenuCallback(Get(m.rows.at(row)).fields.at("m_callback"),view);
}
size_t IndexIn(Obj parent,Obj child){const auto& children=Get(parent).children;return static_cast<size_t>(std::find(children.begin(),children.end(),child)-children.begin());}
int Count(Obj root,const char* type){int n=Get(root).type->name==type?1:0;for(auto child:Get(root).children)n+=Count(child,type);return n;}
std::vector<Obj> Inputs(Obj root){
    std::vector<Obj> result;if(Get(root).type->name=="TextInput")result.push_back(root);
    for(auto child:Get(root).children){auto nested=Inputs(child);result.insert(result.end(),nested.begin(),nested.end());}return result;
}
// Native rows arrive through the ListElement action hook, native buttons
// through the ExtendedClickable up-event hook, tabs toggle themselves.
int clicks=0;
void Click(Obj object){
    ++clicks;
    if(!object)throw std::runtime_error("Missing notebook action at click #"+std::to_string(clicks));
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
        Check(inputs.size()==4&&Get(window).fields["parent"]==middle,"Native window with title, x, y and body (the world is a dropdown)");
        Check(Get(window).enums["set_headerShowCloseButton"]==1&&Get(window).enums["SetHeaderSize"]==0,"The window shows its native close button and keeps the full header while the setting is off");
        Check(Get(window).enums["set_verticalResizable"]==1&&Get(window).enums["set_horizontalResizable"]==1&&Get(window).enums["set_cornerResizable"]==1,"The notebook window resizes like the game's own windows");
        Check(Count(window,"DofusTab")==3&&TabOn(window,"Positions")&&!TabOn(window,"Notes")&&!TabOn(window,"Messages"),"Three native tabs with Positions selected");
        Check(Get(FindType(window,"DofusTab","Notes")).enums["set_fontCase"]==1&&Get(FindType(window,"DofusTab","Notes")).enums["set_size"]==1,"Tabs use the game's capitalised medium style");
        Check(Count(window,"DofusButtonCustom")==14&&Count(window,"SectionHeader")==1&&Count(window,"Divider")==2&&Count(window,"ListElementBasic")==9,"Native buttons, one titled section (the entries list has no header), dividers, six list rows and three world items");
        Check(!FindText(window,"Fermer")&&FindText(window,"Nouvelle entrée")&&FindText(window,"Ma position")&&FindText(window,"Actualiser"),"No custom close row; native action buttons");
        Check(Get(FindText(window,"Enregistrer")).enums["set_mainStyle"]==0&&Get(FindText(window,"Supprimer")).enums["set_status"]==1&&Get(FindText(window,"Enregistrer")).enums["set_icon"]==148,"Primary save, negative delete, floppy icon");
        Check(!FindText(window,"Mes entrées")&&FindText(window,"Détail")&&FindText(window,"Aucune entrée"),"No entries header, the detail section title and an empty-list counter");
        for(const auto* title:{"Détail"}){auto section=FindType(window,"SectionHeader",title);Check(section&&Get(section).enums["set_isOpen"]==1&&Get(section).enums["set_canBeOpened"]==0,"Sections opened while openable, then locked: their content (new-entry button, rows, form) is shown");}
        OpenNotebookPanel();TickNotebookPanel();Check(window==latestWindow,"Open must reuse window");
        Get(inputs[0]).text="Mon zaap <b>littéral</b>";Click(FindText(window,"Ma position"));
        Check(Get(inputs[1]).text=="-25"&&Get(inputs[2]).text=="36","Current position captured");
        auto worldList=FindType(window,"Dropdown","1 : Monde des Douze");
        Check(worldList&&Count(worldList,"ListElementBasic")==3&&Get(worldList).closeOnSelection,"The world is a native dropdown of the game's worlds, set from the current map");
        Click(FindText(window,"Enregistrer"));
        const auto path=notebook::DataPath(owner.settingsPath);
        auto data=notebook::Read(path);Check(data.entries.size()==1&&data.entries[0].map==987,"Position saved with map identity");
        Get(worldList).fields["selected"]=FindText(worldList,"2 : Incarnam");TickNotebookPanel();
        Check(Get(worldList).text=="2 : Incarnam","A dropdown choice is read back each frame");
        Click(FindText(window,"Enregistrer"));data=notebook::Read(path);
        Check(data.entries[0].world==2&&data.entries[0].map==0,"World change saved; the captured map identity no longer applies");
        Check(!travels&&!prepares,"Saving never travels or sends/prepares");
        auto row=FindText(window,"[-25,36]  Mon zaap <b>littéral</b>");
        Check(row&&Get(Get(row).fields["m_textLabel"]).enums["set_enableRichText"]==0,"User text displayed literally");
        Check(Get(row).enums["set_listElementStyle"]==0&&Get(row).enums["set_icon"]==225&&Get(row).enums["set_isActivated"]==1&&FindText(window,"1 entrée"),"Choice row with the position icon, activated for the saved entry");
        Click(row);Click(FindText(window,"Voyager"));Check(travels==1&&travelled.x==-25,"Explicit travel only");
        ClickTab(window,"Notes");Check(Get(inputs[3]).enums["set_isMultiline"]==1&&TabOn(window,"Notes")&&!TabOn(window,"Positions"),"Notes use native multiline input and the tab switches");
        Get(inputs[0]).text="Plan";Get(inputs[3]).text="Ligne un\nLigne deux";
        ClickTab(window,"Messages");Check(Get(inputs[0]).text=="Plan"&&!TabOn(window,"Messages")&&TabOn(window,"Notes"),"Unsaved tab change is refused and the tab snaps back without destroying text");
        Click(FindText(window,"Enregistrer"));Check(notebook::Read(path).entries.size()==2,"Multiline note saved");
        ClickTab(window,"Messages");Check(TabOn(window,"Messages"),"After saving the tab change goes through");
        Get(inputs[0]).text="Salut";Get(inputs[3]).text="Bonjour groupe";
        Click(FindText(window,"Enregistrer"));Click(FindText(window,"Préparer dans le chat"));
        Check(prepares==1&&prepared=="Bonjour groupe","Only explicit prepare action touches draft");
        auto channel=FindText(window,"Canal : actuel");
        Check(!sends&&channel&&Get(channel).type->name=="DofusButtonCustom","Nothing is sent without the explicit button; a message starts on the chat's current channel");
        Click(channel);Check(Get(channel).text=="Canal : Général","The channel button cycles to the first game channel");
        Click(channel);Click(channel);Check(Get(channel).text=="Canal : Guilde","The channel button cycles through the game's channels");
        Click(FindText(window,"Envoyer"));Check(sends==1&&sent=="Bonjour groupe"&&sentChannel==4,"Explicit send carries the chosen channel");
        Click(FindText(window,"Enregistrer"));Check(notebook::Read(path).entries.back().channel==4,"The channel is saved with the message");
        Check(FindText(window,"Enregistré dans le carnet.")!=nullptr,"Saving with a channel reports success and keeps the entry selected");
        Click(FindText(window,"Supprimer"));Check(notebook::Read(path).entries.size()==3,"First delete requires confirmation");
        Click(FindText(window,"Confirmer la suppression"));Check(notebook::Read(path).entries.size()==2,"Confirmed delete");
        Get(inputs[0]).text="Brouillon";Get(inputs[3]).text="À garder";
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
        // Layouts chosen in the panel's ⋯ menu (the game's radio rows): applied in
        // place to the open window, which is never rebuilt and stays on screen.
        Get(middle).dimensions["width"]=1280;Get(middle).dimensions["height"]=720;logs.clear();
        owner.settings.notebookCompact=false;OpenNotebookPanel();TickNotebookPanel();window=latestWindow;Check(Panel(window),"Full panel open");
        inputs=Inputs(window);auto savedRow=FindText(window,"[-25,36]  Mon zaap <b>littéral</b>");Check(!!savedRow,"The saved position is listed");Click(savedRow);
        Get(window).dimensions["left"]=230;Get(window).dimensions["top"]=140; // The native dragger writes the inline left/top.
        auto menu=OpenMenu(window);
        Check(menu.rows.size()==6&&Get(menu.rows[4]).text=="Version complète"&&Get(menu.rows[5]).text=="Version compacte"&&Get(menu.rows[4]).enums["isOn"]==1&&Get(menu.rows[5]).enums["isOn"]==0,
            "The notebook's ⋯ menu lists both layouts as radio rows, the full one marked");
        Get(inputs[0]).text="Titre non enregistré";
        Check(Pick(menu,5)&&!owner.settings.notebookCompact&&!Panel(menu.root)&&Get(window).dimensions["width"]==660&&
            FindText(window,"Modifications non enregistrées. Enregistre-les ou reclique pour les abandonner."),"A choice with unsaved edits closes the menu and waits for a confirming second choice");
        menu=OpenMenu(window);
        Check(Pick(menu,5)&&owner.settings.notebookCompact&&Get(inputs[0]).text=="Mon zaap <b>littéral</b>","The confirming choice drops the edits and applies the compact layout");
        TickNotebookPanel();TickNotebookPanel();
        Check(latestWindow==window&&Panel(window)&&Get(window).fields["parent"]==middle&&Get(window).dimensions["left"]==230&&Get(window).dimensions["top"]==140,
            "The same window stays where it was dragged: no rebuild, no position reset");
        auto detail=FindType(window,"SectionHeader","Détail"),add=FindText(window,"Nouvelle entrée"),entries=Get(add).fields.at("parent");
        Check(Get(window).dimensions["width"]==360&&Get(window).dimensions["minWidth"]==300&&Get(Get(detail).fields.at("parent")).enums["display"]==1&&Get(add).enums["display"]==1,
            "Compact: narrow window, hidden editor, no new-entry button");
        auto channelRow=Get(FindText(window,"Canal : actuel")).fields.at("parent"),useRow=Get(FindText(window,"Copier")).fields.at("parent");
        const auto& listed=Get(entries).children;
        Check(listed.size()>=3&&listed[listed.size()-3]==channelRow&&listed.back()==useRow&&!FindText(detail,"Copier")&&!FindText(detail,"Canal : actuel"),
            "Compact: the channel and use actions sit under the entries list");
        Check(FindText(window,"Choisis une entrée, puis Voyager, Envoyer ou Copier.")&&Get(savedRow).enums["set_isActivated"]==1,"Compact status explains the layout; the entry stays selected");
        const auto travelsBefore=travels;Click(FindText(window,"Voyager"));Check(travels==travelsBefore+1&&travelled.x==-25,"Compact travel uses the selected entry");
        menu=OpenMenu(window);Check(Get(menu.rows[4]).enums["isOn"]==0&&Get(menu.rows[5]).enums["isOn"]==1,"The mark follows the compact layout");
        Check(Pick(menu,5)&&owner.settings.notebookCompact&&latestWindow==window&&!Panel(menu.root),"The current layout's row only closes the menu");
        Check(toolsUi::ChooseLayout("notebook",1)&&owner.settings.notebookCompact&&latestWindow==window,"Choosing the current layout changes nothing");
        menu=OpenMenu(window);
        Check(Pick(menu,4)&&!owner.settings.notebookCompact&&latestWindow==window&&Panel(window),"« Version complète » from the menu restores the full layout in the same window");
        auto slot=Get(channelRow).fields.at("parent"),editor=Get(useRow).fields.at("parent"),actions=Get(FindText(window,"Enregistrer")).fields.at("parent");
        Check(Get(window).dimensions["width"]==660&&Get(window).dimensions["minWidth"]==560&&Get(Get(detail).fields.at("parent")).enums["display"]==0&&Get(add).enums["display"]==0,
            "Full layout restored: wide window, editor and new-entry button shown");
        Check(Get(slot).fields.at("parent")==editor&&Get(actions).fields.at("parent")==editor&&IndexIn(editor,slot)<IndexIn(editor,actions)&&Get(editor).children.back()==useRow&&FindText(detail,"Copier"),
            "Full: the channel row is back above the save actions and the use actions at the end of the editor");
        Check(FindText(window,"Choisis une entrée dans la liste de gauche.")!=nullptr,"Full status explains the layout");
        Check(Get(window).dimensions["left"]==230&&Get(window).dimensions["top"]==140,"A window inside the screen keeps its position");
        Check(std::none_of(logs.begin(),logs.end(),[](const std::string& line){return line.starts_with("TOOL_PANEL_");}),"The notebook menu is populated and dispatched without error");
        // A pending delete confirmation does not survive a layout change.
        const auto kept=notebook::Read(path).entries.size();
        Click(FindText(window,"Supprimer"));Check(FindText(window,"Confirmer la suppression")!=nullptr,"Delete armed");
        Check(toolsUi::ChooseLayout("notebook",1)&&toolsUi::ChooseLayout("notebook",0),"Layout round trip");
        Check(FindText(window,"Supprimer")&&!FindText(window,"Confirmer la suppression"),"A layout change drops the pending delete confirmation");
        Click(FindText(window,"Supprimer"));Check(notebook::Read(path).entries.size()==kept&&FindText(window,"Confirmer la suppression"),"One click after a layout change only re-arms the delete");
        // Another tab and its entry are kept across both layouts.
        ClickTab(window,"Notes");auto noteRow=FindType(window,"ListElementBasic","Plan");Check(!!noteRow,"The saved note is listed");Click(noteRow);
        Check(toolsUi::ChooseLayout("notebook",1),"Compact from the Notes tab");TickNotebookPanel();
        Check(TabOn(window,"Notes")&&!TabOn(window,"Positions")&&Get(noteRow).enums["set_isActivated"]==1&&Get(window).dimensions["left"]==230&&Get(window).dimensions["top"]==140,
            "Compact keeps the Notes tab, its entry and the dragged position");
        Check(toolsUi::ChooseLayout("notebook",0),"Full from the Notes tab");TickNotebookPanel();
        Check(TabOn(window,"Notes")&&Get(inputs[0]).text=="Plan"&&Get(noteRow).enums["set_isActivated"]==1,"Full keeps the Notes tab and its entry");
        // A compact window docked at the right and bottom edges stays inside the screen in the wider layout.
        Check(toolsUi::ChooseLayout("notebook",1),"Compact before docking");Get(window).dimensions["left"]=1280-360;Get(window).dimensions["top"]=720-240;
        Check(toolsUi::ChooseLayout("notebook",0)&&Get(window).dimensions["left"]==1280-660&&Get(window).dimensions["top"]==720-360,"The full layout is moved back inside the screen");
        // A window opened while the compact layout is saved is built compact.
        Check(toolsUi::ChooseLayout("notebook",1),"Compact again");Click(CloseButton(window));TickNotebookPanel();Check(!Panel(window),"Compact window closed");
        OpenNotebookPanel();TickNotebookPanel();window=latestWindow;
        Check(Panel(window)&&Get(window).dimensions["width"]==360&&Get(Get(FindText(window,"Copier")).fields.at("parent")).fields.at("parent")==Get(FindText(window,"Nouvelle entrée")).fields.at("parent"),
            "A window opened in the compact layout is built compact");
        // An unreadable notebook keeps its warning across layout and tab changes.
        RestoreNotebookPanel();owner.settings.notebookCompact=false;
        const auto backup=directory/"notebook-backup.json";std::filesystem::copy_file(path,backup,std::filesystem::copy_options::overwrite_existing);
        {std::ofstream corrupt(path,std::ios::binary|std::ios::trunc);corrupt<<"{";}
        OpenNotebookPanel();TickNotebookPanel();window=latestWindow;
        const std::string unreadable="Carnet illisible. Le fichier est conservé ; utilise Actualiser après correction.";
        Check(FindText(window,unreadable)&&!Get(FindText(window,"Enregistrer")).enabled,"An unreadable notebook is reported at opening, saving disabled");
        Check(toolsUi::ChooseLayout("notebook",1)&&latestWindow==window&&FindText(window,unreadable),"The warning survives the compact layout");
        Check(toolsUi::ChooseLayout("notebook",0)&&FindText(window,unreadable)&&!Get(FindText(window,"Enregistrer")).enabled,"The warning survives the return to the full layout");
        ClickTab(window,"Notes");Check(FindText(window,unreadable)!=nullptr,"The warning survives a tab change");
        RestoreNotebookPanel();std::filesystem::copy_file(backup,path,std::filesystem::copy_options::overwrite_existing);
        // A settings save that fails after the setting changed: the open window
        // follows the setting the menu marks, and one choice brings it back.
        OpenNotebookPanel();TickNotebookPanel();window=latestWindow;Check(Panel(window)&&Get(window).dimensions["width"]==660,"Full window before a failed save");
        changeFailures=1;bool threw=false;
        try{toolsUi::ChooseLayout("notebook",1);}catch(const std::runtime_error&){threw=true;}
        Check(threw&&owner.settings.notebookCompact&&Get(window).dimensions["width"]==360&&Get(Get(FindType(window,"SectionHeader","Détail")).fields.at("parent")).enums["display"]==1,
            "After a failed save the window still follows the setting the menu marks");
        Check(toolsUi::ChooseLayout("notebook",0)&&!owner.settings.notebookCompact&&Get(window).dimensions["width"]==660,"One choice brings the full layout back");
        RestoreNotebookPanel();Check(!NotebookPanelCleanupPending()&&!Panel(window),"Layout scenario releases its window");
        toolsUi::Restore();owner.cached.reset();owner.compositor.reset();module=nullptr;Check(handles.empty(),"No GC handles leaked");
        std::filesystem::remove_all(directory);
        std::cout<<"PASS: notebook native tabs/buttons/sections, draft protection, explicit travel/chat, literal text and cleanup\n";return 0;
    }catch(const std::exception& error){std::cerr<<"FAIL: "<<error.what()<<'\n';for(const auto& message:logs)std::cerr<<message<<'\n';return 1;}
}
