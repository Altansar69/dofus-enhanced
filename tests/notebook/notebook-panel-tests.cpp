#include "ui/tools-ui.hpp"
#include "ui/native-click.hpp"
#include "support/native-panel-test-fixture.hpp"
#include "notebook/notebook-panel.hpp"
#include "notebook/notebook-native.hpp"
#include <algorithm>
#include <fstream>
#include <map>

int travels=0,prepares=0,copies=0,sends=0,sentChannel=-1;
std::string prepared,sent,copied;
notebook::Entry travelled;bool failTravel=false;
// Map pins and row menus (notebook-native): the game's flags by entry id, as placed.
std::map<uint64_t,notebook::Entry> pinned;int pinShows=0,pinHides=0;bool failPin=false;notebook::Entry pinnedEntry;
notebook::RowMenu rowMenu;std::vector<managed::Obj> menuRows;bool pinsAvailable=true,menusAvailable=true;
namespace notebook {
void InstallNative() {}
Entry CurrentPosition(){Entry result;result.x=-25;result.y=36;result.world=1;result.map=987;return result;}
void Travel(const Entry& entry){if(failTravel)throw std::runtime_error("Trajet indisponible pendant un combat.");++travels;travelled=entry;}
void PrepareMessage(const std::string& text){++prepares;prepared=text;}
void SendChatMessage(const std::string& text,int channel){++sends;sent=text;sentChannel=channel;}
std::vector<World> Worlds(){return {{1,"Monde des Douze"},{2,"Incarnam"},{3,"Srambad"}};}
void Copy(const std::string& text){++copies;copied=text;}
bool PinsAvailable(){return pinsAvailable;}
bool PinShown(const Entry& entry){return pinned.contains(entry.id);}
void ShowPin(const Entry& entry){if(failPin)throw std::runtime_error("La carte du jeu ne connaît pas ce monde : repère impossible.");++pinShows;pinnedEntry=entry;pinned[entry.id]=entry;}
void HidePin(uint64_t id){++pinHides;pinned.erase(id);}
std::vector<Entry> PlacedPins(){std::vector<Entry> result;for(const auto& pin:pinned)result.push_back(pin.second);return result;}
void SetRowMenu(RowMenu menu){rowMenu=menu;}
bool AttachRowMenu(managed::Obj row){if(!menusAvailable)return false;menuRows.push_back(row);return true;}
void ReleaseRowMenus(){menuRows.clear();}
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
// The buttons after an entry's row, in its line: map pin, copy, travel.
enum LineButtonIndex : size_t { PinButton=1, CopyButton, TravelButton };
Obj LineButton(Obj row,LineButtonIndex index){return Get(Get(row).fields.at("parent")).children.at(index);}
bool Shown(Obj element){return Get(element).enums["display"]==0;}
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
        Check(Count(window,"DofusButtonCustom")==30&&Count(window,"SectionHeader")==1&&Count(window,"Divider")==1&&Count(window,"ListElementBasic")==9,"Native buttons (eighteen of them the map pin, copy and travel buttons of the six lines), one titled section (the entries list has no header), the tab divider only, six list rows and three world items");
        Check(!FindText(window,"Fermer")&&FindText(window,"Nouvelle entrée")&&FindText(window,"Ma position")&&!FindText(window,"Actualiser")&&!FindType(window,"Divider","Utiliser"),"No custom close row, no Actualiser button, no Utiliser separator; native action buttons");
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
        Check(!FindText(window,"Voyager")&&!Shown(FindText(window,"Copier")),"Positions: no « Voyager » under the form, and « Copier » hidden there");
        Click(row);Click(LineButton(row,TravelButton));Check(travels==1&&travelled.x==-25,"Explicit travel only, from the position's own line");
        Click(LineButton(row,CopyButton));Check(copies==1&&copied=="[-25,36]","The line's copy button copies the coordinates, as « Copier » did");
        ClickTab(window,"Notes");Check(Get(inputs[3]).enums["set_isMultiline"]==1&&TabOn(window,"Notes")&&!TabOn(window,"Positions"),"Notes use native multiline input and the tab switches");
        Get(inputs[0]).text="Plan";Get(inputs[3]).text="Ligne un\nLigne deux";
        ClickTab(window,"Messages");Check(Get(inputs[0]).text=="Plan"&&!TabOn(window,"Messages")&&TabOn(window,"Notes"),"Unsaved tab change is refused and the tab snaps back without destroying text");
        Click(FindText(window,"Enregistrer"));Check(notebook::Read(path).entries.size()==2,"Multiline note saved");
        Check(Shown(FindText(window,"Copier"))&&!Shown(LineButton(row,CopyButton))&&!Shown(LineButton(row,TravelButton)),"Notes: « Copier » under the form, no copy or travel button on the lines");
        Click(FindText(window,"Copier"));Check(copies==2&&copied=="Ligne un\nLigne deux","« Copier » copies the note's text");
        ClickTab(window,"Messages");Check(TabOn(window,"Messages"),"After saving the tab change goes through");
        Get(inputs[0]).text="Salut";Get(inputs[3]).text="Bonjour groupe";
        Click(FindText(window,"Enregistrer"));Click(FindText(window,"Préparer dans le chat"));
        Check(prepares==1&&prepared=="Bonjour groupe","Only explicit prepare action touches draft");
        auto channel=FindText(window,"Canal : actuel");
        Check(!sends&&channel&&Get(channel).type->name=="DofusButtonCustom","Nothing is sent without the explicit button; a message starts on the chat's current channel");
        Click(channel);Check(Get(channel).text=="Canal : Général","The channel button cycles to the first game channel");
        Click(channel);Click(channel);Check(Get(channel).text=="Canal : Guilde","The channel button cycles through the game's channels");
        Click(FindText(window,"Envoyer"));Check(sends==1&&sent=="Bonjour groupe"&&sentChannel==4,"Explicit send carries the chosen channel");
        Click(FindText(window,"Copier"));Check(copies==3&&copied=="Bonjour groupe","Messages keep « Copier »: it copies the message");
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
        Check(Pick(menu,5)&&!owner.settings.notebookCompact&&!Panel(menu.root)&&Get(window).dimensions["width"]==730&&
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
        Check(listed.size()>=3&&listed[listed.size()-2]==channelRow&&listed.back()==useRow&&!FindText(detail,"Copier")&&!FindText(detail,"Canal : actuel"),
            "Compact: the channel and use actions sit under the entries list");
        Check(FindText(window,"Copie les coordonnées d’une position ou lance le trajet avec les boutons de sa ligne.")&&Get(savedRow).enums["set_isActivated"]==1,
            "Compact status names the buttons of the position lines; the entry stays selected");
        const auto travelsBefore=travels;Click(LineButton(savedRow,TravelButton));Check(travels==travelsBefore+1&&travelled.x==-25,"Compact travel from the position's own line");
        menu=OpenMenu(window);Check(Get(menu.rows[4]).enums["isOn"]==0&&Get(menu.rows[5]).enums["isOn"]==1,"The mark follows the compact layout");
        Check(Pick(menu,5)&&owner.settings.notebookCompact&&latestWindow==window&&!Panel(menu.root),"The current layout's row only closes the menu");
        Check(toolsUi::ChooseLayout("notebook",1)&&owner.settings.notebookCompact&&latestWindow==window,"Choosing the current layout changes nothing");
        menu=OpenMenu(window);
        Check(Pick(menu,4)&&!owner.settings.notebookCompact&&latestWindow==window&&Panel(window),"« Version complète » from the menu restores the full layout in the same window");
        auto slot=Get(channelRow).fields.at("parent"),editor=Get(useRow).fields.at("parent"),actions=Get(FindText(window,"Enregistrer")).fields.at("parent");
        Check(Get(window).dimensions["width"]==730&&Get(window).dimensions["minWidth"]==630&&Get(Get(detail).fields.at("parent")).enums["display"]==0&&Get(add).enums["display"]==0,
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
        Check(toolsUi::ChooseLayout("notebook",0)&&Get(window).dimensions["left"]==1280-730&&Get(window).dimensions["top"]==720-360,"The full layout is moved back inside the screen");
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
        const std::string unreadable="Carnet illisible. Le fichier est conservé ; il sera relu dès qu’il aura été corrigé.";
        Check(FindText(window,unreadable)&&!Get(FindText(window,"Enregistrer")).enabled,"An unreadable notebook is reported at opening, saving disabled");
        Check(toolsUi::ChooseLayout("notebook",1)&&latestWindow==window&&FindText(window,unreadable),"The warning survives the compact layout");
        Check(toolsUi::ChooseLayout("notebook",0)&&FindText(window,unreadable)&&!Get(FindText(window,"Enregistrer")).enabled,"The warning survives the return to the full layout");
        ClickTab(window,"Notes");Check(FindText(window,unreadable)!=nullptr,"The warning survives a tab change");
        RestoreNotebookPanel();std::filesystem::copy_file(backup,path,std::filesystem::copy_options::overwrite_existing);
        // A settings save that fails after the setting changed: the open window
        // follows the setting the menu marks, and one choice brings it back.
        OpenNotebookPanel();TickNotebookPanel();window=latestWindow;Check(Panel(window)&&Get(window).dimensions["width"]==730,"Full window before a failed save");
        changeFailures=1;bool threw=false;
        try{toolsUi::ChooseLayout("notebook",1);}catch(const std::runtime_error&){threw=true;}
        Check(threw&&owner.settings.notebookCompact&&Get(window).dimensions["width"]==360&&Get(Get(FindType(window,"SectionHeader","Détail")).fields.at("parent")).enums["display"]==1,
            "After a failed save the window still follows the setting the menu marks");
        Check(toolsUi::ChooseLayout("notebook",0)&&!owner.settings.notebookCompact&&Get(window).dimensions["width"]==730,"One choice brings the full layout back");
        RestoreNotebookPanel();Check(!NotebookPanelCleanupPending()&&!Panel(window),"Layout scenario releases its window");
        // r112: each position has its map pin button and the game's right-click
        // menu; the file is read again by itself when it changes on disk.
        owner.settings.notebookCompact=false;OpenNotebookPanel();TickNotebookPanel();window=latestWindow;inputs=Inputs(window);
        Check(Panel(window)&&menuRows.size()==6&&rowMenu.entry&&rowMenu.done,"Every entry row gets the game's right-click menu");
        auto zaap=FindText(window,"[-25,36]  Mon zaap <b>littéral</b>");Check(!!zaap,"The saved position is listed");
        Check(Get(Get(zaap).fields.at("set_tooltip")).text=="Clic droit : Voyager, Afficher sur la carte ou Retirer de la carte.","A position's row tells its right-click menu");
        auto line=Get(zaap).fields.at("parent");auto pin=LineButton(zaap,PinButton);
        Check(Get(pin).type->name=="DofusButtonCustom"&&Get(pin).enums["set_icon"]==145&&Get(pin).enums["display"]==0&&Get(pin).enums["set_mainStyle"]==2&&Get(line).children.front()==zaap&&
            Get(line).children.size()==4,"A flat flag button at the right of the position's name, before its copy and travel buttons");
        const auto zaapId=notebook::Read(path).entries.front().id;
        Click(pin);
        Check(pinShows==1&&pinned.contains(zaapId)&&Get(pin).enums["set_mainStyle"]==0&&FindText(window,"Repère placé sur la carte en [-25,36] : grande carte et mini-carte."),
            "Clicked, the position's flag is placed and its button looks pressed");
        Click(pin);Check(pinHides==1&&pinned.empty()&&Get(pin).enums["set_mainStyle"]==2&&FindText(window,"Repère retiré de la carte."),"Clicked again, the flag goes and the button is flat again");
        failPin=true;Click(pin);
        Check(pinned.empty()&&Get(pin).enums["set_mainStyle"]==2&&FindText(window,"La carte du jeu ne connaît pas ce monde : repère impossible."),"A refused pin is told and the button stays flat");failPin=false;
        owner.settings.notebookPingButton=false;TickNotebookPanel();Check(Get(pin).enums["display"]==1,"Option off: no pin button, at once");
        Click(pin);Check(pinShows==1&&pinned.empty(),"A hidden pin button does nothing");
        owner.settings.notebookPingButton=true;TickNotebookPanel();Check(Get(pin).enums["display"]==0,"Option on again: the button is back without reopening the window");
        ClickTab(window,"Notes");Check(Get(pin).enums["display"]==1,"Notes have no pin button");
        notebook::Entry asked;auto note=FindType(window,"ListElementBasic","Plan");
        Check(note&&!rowMenu.entry(note,asked)&&Get(Get(note).fields.at("set_tooltip")).text.empty(),"Notes have no right-click menu, nor its tooltip");
        ClickTab(window,"Positions");
        Check(rowMenu.entry(zaap,asked)&&asked.id==zaapId&&asked.x==-25&&asked.y==36&&asked.title=="Mon zaap <b>littéral</b>","A position's row gives its position to the right-click menu");
        Check(!rowMenu.entry(Element("ForeignRow"),asked),"Another element has no position");
        pinned[zaapId]=asked;rowMenu.done(asked,"Repère placé sur la carte en [-25,36] : grande carte et mini-carte.");
        Check(Get(pin).enums["set_mainStyle"]==0&&FindText(window,"Repère placé sur la carte en [-25,36] : grande carte et mini-carte."),"After a menu action the button follows the flag and the status tells");
        pinned.clear();CheckNotebookPanel();
        Check(Get(pin).enums["set_mainStyle"]==2,"A flag removed from the map's own menu is seen at the next check");
        // The file is checked every half second at the module's pump, not every frame.
        notebook::Entry other;other.kind=notebook::Kind::Position;other.title="Ajoutée ailleurs";other.x=3;other.y=4;other.world=1;
        CheckNotebookPanel();notebook::Save(path,other);TickNotebookPanel();
        Check(!FindText(window,"[3,4]  Ajoutée ailleurs"),"Within the half second the file is not read again");
        Sleep(550);TickNotebookPanel();
        Check(FindText(window,"[3,4]  Ajoutée ailleurs")&&FindText(window,"Carnet mis à jour : le fichier a changé.")&&FindText(window,"2 entrées"),"Half a second later the position saved elsewhere appears");
        // A pinned position moves its flag when saved here or elsewhere.
        Click(pin);Click(zaap);Get(inputs[0]).text="Mon zaap déplacé";Get(inputs[1]).text="-24";Click(FindText(window,"Enregistrer"));
        Check(pinShows==3&&pinnedEntry.id==zaapId&&pinnedEntry.title=="Mon zaap déplacé"&&pinnedEntry.x==-24&&pinned.contains(zaapId)&&FindText(window,"Enregistré dans le carnet."),
            "Saving a pinned position moves its flag");
        const auto entryNamed=[&](uint64_t id){auto entries=notebook::Read(path).entries;return *std::find_if(entries.begin(),entries.end(),[&](const notebook::Entry& entry){return entry.id==id;});};
        auto edited=entryNamed(zaapId);edited.title="Renommé ailleurs";notebook::Save(path,edited);CheckNotebookPanel();
        Check(Get(inputs[0]).text=="Renommé ailleurs"&&FindText(window,"[-24,36]  Renommé ailleurs"),"With no draft, the selected entry follows its saved version");
        Check(pinShows==4&&pinnedEntry.title=="Renommé ailleurs","A pinned position edited elsewhere moves its flag too");
        // Every save is a new file: one of the same size within the same clock tick is still seen.
        CheckNotebookPanel();edited=entryNamed(zaapId);edited.title="Renommé ailleurZ";notebook::Save(path,edited);CheckNotebookPanel();
        Check(Get(inputs[0]).text=="Renommé ailleurZ","A save of the same size right after the last check is seen");
        // An unsaved draft is never replaced; after the notice, dropping it asks again.
        auto otherRow=FindText(window,"[3,4]  Ajoutée ailleurs");
        Get(inputs[0]).text="Mon brouillon";Click(otherRow);
        Check(Get(inputs[0]).text=="Mon brouillon"&&FindText(window,"Modifications non enregistrées. Enregistre-les ou reclique pour les abandonner."),"A draft asks before being dropped");
        edited=entryNamed(zaapId);edited.title="Encore changé";notebook::Save(path,edited);CheckNotebookPanel();
        Check(Get(inputs[0]).text=="Mon brouillon"&&FindText(window,"Le carnet a changé ailleurs. Ton brouillon est conservé.")&&FindText(window,"[-24,36]  Encore changé"),
            "The list follows the file, an unsaved draft is never replaced");
        Click(otherRow);Check(Get(inputs[0]).text=="Mon brouillon"&&FindText(window,"Modifications non enregistrées. Enregistre-les ou reclique pour les abandonner."),
            "After the notice one click asks again: the draft is not dropped silently");
        Click(FindText(window,"Enregistrer"));
        Check(Get(inputs[0]).text=="Mon brouillon"&&FindText(window,"Cette entrée a changé ailleurs. La resélectionner dans la liste charge sa nouvelle version et abandonne ton brouillon."),
            "Saving the draft over the newer version is refused and the draft kept");
        Click(zaap);Click(zaap);Check(Get(inputs[0]).text=="Encore changé","Selecting the entry again (confirmed) loads its new version");
        // A draft of an entry deleted elsewhere becomes a new entry.
        Get(inputs[0]).text="Brouillon orphelin";const auto doomed=entryNamed(zaapId);notebook::Delete(path,doomed.id,doomed.version);CheckNotebookPanel();
        Check(Get(inputs[0]).text=="Brouillon orphelin"&&FindText(window,"Cette entrée a été supprimée ailleurs. Ton brouillon est conservé : Enregistrer en fera une nouvelle entrée.")&&
            !Get(FindText(window,"Supprimer")).enabled,"A draft of an entry deleted elsewhere is kept as a new entry, which cannot be deleted");
        Check(pinHides==2&&!pinned.contains(zaapId),"The deleted position's pin left the map");
        Click(FindText(window,"Enregistrer"));
        const auto revived=notebook::Read(path).entries.back();
        Check(revived.id!=zaapId&&revived.title=="Brouillon orphelin"&&revived.x==-24&&FindText(window,"Enregistré dans le carnet."),"Enregistrer saves it as a new entry");
        // A file held a moment by another program: unreadable, then read again unchanged.
        notebook::Entry third;third.kind=notebook::Kind::Position;third.title="Pendant un verrou";third.x=5;third.y=6;third.world=1;notebook::Save(path,third);
        const auto held=CreateFileW(path.c_str(),GENERIC_READ,0,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);Check(held!=INVALID_HANDLE_VALUE,"Fixture exclusive handle");
        CheckNotebookPanel();Check(FindText(window,unreadable)&&!Get(FindText(window,"Enregistrer")).enabled,"A file held by another program is told, saving disabled");
        CloseHandle(held);CheckNotebookPanel();
        Check(!FindText(window,unreadable)&&Get(FindText(window,"Enregistrer")).enabled&&FindText(window,"[5,6]  Pendant un verrou"),"Released, it is read again at the next check though unchanged");
        // A recreated file numbers from 1 again: another position taking a pinned id does not get its pin.
        const auto listedNow=notebook::Read(path).entries;
        const auto otherId=std::find_if(listedNow.begin(),listedNow.end(),[](const notebook::Entry& entry){return entry.title=="Ajoutée ailleurs";})->id;
        otherRow=FindText(window,"[3,4]  Ajoutée ailleurs");auto otherPin=LineButton(otherRow,PinButton);
        Click(otherPin);Check(pinned.contains(otherId),"Fixture pin on the position added elsewhere");
        {std::ofstream recreated(path,std::ios::binary|std::ios::trunc);
            recreated<<R"({"schema":1,"nextId":)"<<otherId+1<<R"(,"entries":[{"id":)"<<otherId<<R"(,"version":1,"kind":"position","title":"Autre position","body":"","x":9,"y":9,"world":1,"map":0,"channel":0}]})";}
        CheckNotebookPanel();
        Check(FindText(window,"[9,9]  Autre position")&&!pinned.contains(otherId)&&pinHides==3,"The pin of the old entry does not stay on the new one");
        auto lastRow=FindText(window,"[9,9]  Autre position");auto lastPin=LineButton(lastRow,PinButton);
        Click(lastPin);Click(lastRow);Click(FindText(window,"Supprimer"));Click(FindText(window,"Confirmer la suppression"));
        Check(pinHides==4&&pinned.empty()&&notebook::Read(path).entries.empty()&&FindText(window,"Entrée supprimée."),"Deleting a pinned position here takes its flag off the map");
        RestoreNotebookPanel();Check(!NotebookPanelCleanupPending()&&!Panel(window)&&menuRows.empty(),"The row menus are released with the window");
        // The row tells its right-click menu only where it has one, naming what it offers.
        notebook::Entry tip;tip.kind=notebook::Kind::Position;tip.title="Info-bulle";tip.x=7;tip.y=7;tip.world=1;notebook::Save(path,tip);
        menusAvailable=false;OpenNotebookPanel();TickNotebookPanel();window=latestWindow;auto tipRow=FindText(window,"[7,7]  Info-bulle");
        Check(tipRow&&menuRows.empty()&&Get(Get(tipRow).fields.at("set_tooltip")).text.empty(),"Without the game's menu a row tells none");
        RestoreNotebookPanel();menusAvailable=true;pinsAvailable=false;OpenNotebookPanel();TickNotebookPanel();window=latestWindow;tipRow=FindText(window,"[7,7]  Info-bulle");
        Check(tipRow&&Get(Get(tipRow).fields.at("set_tooltip")).text=="Clic droit : Voyager."&&Get(LineButton(tipRow,PinButton)).enums["display"]==1,
            "Without the game's flags the menu only travels and no pin button shows");
        RestoreNotebookPanel();pinsAvailable=true;
        // r116: each position's line has its copy and travel buttons after its
        // pin, in both layouts. They act on that line's position only, paging
        // included: the selection, the form and an unsaved draft stay. Their
        // options show or hide them at once; « Voyager » and « Copier » left the
        // form for positions, « Copier » stays for notes and messages.
        for(int i=1;i<=7;++i){notebook::Entry step;step.kind=notebook::Kind::Position;step.title="Étape "+std::to_string(i);step.x=10+i;step.y=-i;step.world=1;notebook::Save(path,step);}
        {notebook::Entry noteEntry;noteEntry.kind=notebook::Kind::Note;noteEntry.title="Pense-bête";noteEntry.body="Acheter du pain";notebook::Save(path,noteEntry);}
        {notebook::Entry messageEntry;messageEntry.kind=notebook::Kind::Message;messageEntry.title="Salut";messageEntry.body="Bonjour à tous";notebook::Save(path,messageEntry);}
        owner.settings.notebookCompact=false;OpenNotebookPanel();TickNotebookPanel();window=latestWindow;inputs=Inputs(window);
        auto firstRow=FindText(window,"[7,7]  Info-bulle"),secondRow=FindText(window,"[11,-1]  Étape 1"),thirdRow=FindText(window,"[12,-2]  Étape 2");
        Check(Panel(window)&&firstRow&&secondRow&&thirdRow&&FindText(window,"8 entrées")&&!FindText(window,"Voyager"),"Eight positions on two pages, and no « Voyager » button");
        auto positionList=Get(Get(firstRow).fields.at("parent")).fields.at("parent");
        Check(Get(Get(positionList).fields.at("parent")).dimensions["width"]==320&&Get(window).dimensions["width"]==730,
            "The list column has room for a name and its three buttons; the window grew as much, the editor keeps its width");
        // Every line (after the new-entry button) shows, or hides, this button.
        const auto allLines=[&](LineButtonIndex index,bool shown){
            for(size_t i=1;i<=6;++i)if(Shown(Get(Get(positionList).children.at(i)).children.at(index))!=shown)return false;
            return true;
        };
        for(auto entryRow:{firstRow,secondRow,thirdRow}){
            auto entryLine=Get(entryRow).fields.at("parent");
            Check(Get(entryLine).children.size()==4&&Get(entryLine).children.front()==entryRow,"A position's line: its row, then the pin, copy and travel buttons");
            for(auto button:{LineButton(entryRow,PinButton),LineButton(entryRow,CopyButton),LineButton(entryRow,TravelButton)})
                Check(Get(button).type->name=="DofusButtonCustom"&&Get(button).enums["set_size"]==0&&Get(button).enums["set_mainStyle"]==2&&Get(button).text.empty()&&
                    Get(button).dimensions["marginLeft"]==4&&Get(button).dimensions["marginRight"]==0&&Get(button).dimensions["marginBottom"]==0&&
                    Get(button).dimensions["flexShrink"]==0&&Shown(button),"The copy and travel buttons are small, flat and icon only, like the pin");
            Check(Get(LineButton(entryRow,CopyButton)).enums["set_icon"]==99&&Get(LineButton(entryRow,TravelButton)).enums["set_icon"]==336&&
                Get(Get(LineButton(entryRow,CopyButton)).fields.at("set_tooltip")).text=="Copier les coordonnées de cette position dans le presse-papiers."&&
                Get(Get(LineButton(entryRow,TravelButton)).fields.at("set_tooltip")).text=="Lancer le trajet vers cette position : zaaps, bateaux puis /travel.",
                "The copy icon, then the zaap icon, each with its tooltip");
        }
        Check(allLines(CopyButton,true)&&allLines(TravelButton,true)&&!Shown(FindText(window,"Copier")),"Positions: the buttons on every line, no « Copier » under the form");
        const auto formCopies=copies;Click(FindText(window,"Copier"));Check(copies==formCopies,"Positions: the form's hidden « Copier » copies nothing");
        // A click acts on its own line: the selection, the form and an unsaved draft stay.
        Click(firstRow);Get(inputs[0]).text="Brouillon r116";
        const auto copiesBefore=copies,travelsBefore2=travels;
        Click(LineButton(thirdRow,CopyButton));
        Check(copies==copiesBefore+1&&copied=="[12,-2]"&&FindText(window,"Copié dans le presse-papiers.")&&Get(inputs[0]).text=="Brouillon r116"&&
            Get(firstRow).enums["set_isActivated"]==1&&Get(thirdRow).enums["set_isActivated"]==0,"The copy button copies its own line's coordinates; the selection and the draft stay");
        Click(LineButton(secondRow,TravelButton));
        Check(travels==travelsBefore2+1&&travelled.title=="Étape 1"&&travelled.x==11&&travelled.y==-1&&FindText(window,"Trajet demandé vers [11,-1].")&&
            Get(inputs[0]).text=="Brouillon r116"&&Get(firstRow).enums["set_isActivated"]==1&&Get(secondRow).enums["set_isActivated"]==0,
            "The travel button travels to its own line's position; the selection and the draft stay");
        // After a line button the next row click asks again: a draft is never dropped silently.
        Click(secondRow);
        Check(Get(inputs[0]).text=="Brouillon r116"&&FindText(window,"Modifications non enregistrées. Enregistre-les ou reclique pour les abandonner."),"Another row asks before dropping the draft");
        Click(LineButton(secondRow,TravelButton));Click(secondRow);
        Check(Get(inputs[0]).text=="Brouillon r116"&&Get(firstRow).enums["set_isActivated"]==1&&FindText(window,"Modifications non enregistrées. Enregistre-les ou reclique pour les abandonner."),
            "After a line button, the row click asks again instead of dropping the draft");
        Click(LineButton(thirdRow,CopyButton));Click(secondRow);
        Check(Get(inputs[0]).text=="Brouillon r116"&&Get(firstRow).enums["set_isActivated"]==1&&FindText(window,"Modifications non enregistrées. Enregistre-les ou reclique pour les abandonner."),
            "After the copy button too, the row click asks again");
        // Page two: each button acts on the position its line shows.
        auto paging=Get(FindText(window,"8 entrées")).fields.at("parent");Click(Get(paging).children.back());
        Check(FindText(window,"[16,-6]  Étape 6")==firstRow&&FindText(window,"[17,-7]  Étape 7")==secondRow&&!Shown(Get(thirdRow).fields.at("parent")),"Page two: Étape 6 and 7 on the first two lines");
        Click(LineButton(secondRow,TravelButton));Click(LineButton(firstRow,CopyButton));
        Check(travelled.title=="Étape 7"&&travelled.x==17&&copied=="[16,-6]"&&FindText(window,"Copié dans le presse-papiers.")&&Get(inputs[0]).text=="Brouillon r116"&&
            Get(firstRow).enums["set_isActivated"]==0&&Get(secondRow).enums["set_isActivated"]==0,"Page two: each button acts on its own line; nothing gets selected");
        const auto emptyLine=travels;Click(LineButton(thirdRow,TravelButton));Click(LineButton(thirdRow,CopyButton));
        Check(travels==emptyLine&&copied=="[16,-6]","The buttons of an empty line do nothing");
        failTravel=true;Click(LineButton(secondRow,TravelButton));failTravel=false;
        Check(FindText(window,"Trajet indisponible pendant un combat.")&&Get(inputs[0]).text=="Brouillon r116","A refused travel is told; the draft stays");
        Click(Get(paging).children.front());
        Check(FindText(window,"[7,7]  Info-bulle")==firstRow&&Get(firstRow).enums["set_isActivated"]==1&&Get(inputs[0]).text=="Brouillon r116","Back on page one, the entry in the form is still the selected one");
        // The options show or hide the buttons at once, each its own.
        owner.settings.notebookCopyButton=false;TickNotebookPanel();
        Check(allLines(CopyButton,false)&&allLines(TravelButton,true)&&Shown(LineButton(firstRow,PinButton)),"Copy option off: no copy button, at once; travel and pin stay");
        Check(FindText(window,"Trajet indisponible pendant un combat.")!=nullptr,"Full layout: an option change leaves the status line as it was (it names no line button)");
        const auto hiddenCopies=copies;Click(LineButton(firstRow,CopyButton));Check(copies==hiddenCopies,"A hidden copy button does nothing");
        owner.settings.notebookTravelButton=false;TickNotebookPanel();
        Check(allLines(CopyButton,false)&&allLines(TravelButton,false)&&Shown(LineButton(firstRow,PinButton)),"Travel option off: no travel button, at once");
        const auto hiddenTravels=travels;Click(LineButton(firstRow,TravelButton));Check(travels==hiddenTravels,"A hidden travel button does nothing");
        owner.settings.notebookCopyButton=owner.settings.notebookTravelButton=true;TickNotebookPanel();
        Check(allLines(CopyButton,true)&&allLines(TravelButton,true),"Both options on again: the buttons are back without reopening the window");
        // A panel of a replaced game session (until the next tick retires it): its line buttons do nothing.
        {const auto staleCopies=copies,staleTravels=travels;owner.cached.reset(Make("OtherCache"));
            Click(LineButton(firstRow,CopyButton));Click(LineButton(firstRow,TravelButton));owner.cached.reset(Get(dependencies).fields["cached"]);
            Check(copies==staleCopies&&travels==staleTravels&&Panel(window),"A stale panel's line buttons do nothing");}
        // Notes and messages: no line buttons; « Copier » stays under their form.
        Get(inputs[0]).text="Info-bulle";ClickTab(window,"Notes");
        Check(TabOn(window,"Notes")&&allLines(CopyButton,false)&&allLines(TravelButton,false)&&Shown(FindText(window,"Copier")),"Notes: no copy or travel button on the lines; « Copier » under the form");
        Click(FindType(window,"ListElementBasic","Pense-bête"));Click(FindText(window,"Copier"));
        Check(copied=="Acheter du pain"&&FindText(window,"Copié dans le presse-papiers."),"« Copier » copies the note's text");
        ClickTab(window,"Messages");Click(FindType(window,"ListElementBasic","Salut"));Click(FindText(window,"Copier"));
        Check(allLines(CopyButton,false)&&allLines(TravelButton,false)&&copied=="Bonjour à tous"&&Shown(FindText(window,"Envoyer"))&&Shown(FindText(window,"Préparer dans le chat")),
            "Messages: no line buttons; Envoyer, Préparer dans le chat and Copier under the form");
        ClickTab(window,"Positions");
        Check(allLines(CopyButton,true)&&allLines(TravelButton,true)&&!Shown(FindText(window,"Copier")),"Back to Positions: the line buttons, and no « Copier » under the form");
        // The compact layout: the same buttons on the lines, named by its status.
        Check(toolsUi::ChooseLayout("notebook",1)&&Get(window).dimensions["width"]==360,"Compact layout");
        Check(allLines(CopyButton,true)&&allLines(TravelButton,true)&&!Shown(FindText(window,"Copier"))&&
            FindText(window,"Copie les coordonnées d’une position ou lance le trajet avec les boutons de sa ligne."),"Compact: the buttons on every line, named by the status");
        Click(LineButton(secondRow,CopyButton));Click(LineButton(thirdRow,TravelButton));
        Check(copied=="[11,-1]"&&travelled.title=="Étape 2"&&FindText(window,"Trajet demandé vers [12,-2]."),"Compact: each button acts on its own line");
        // The compact status follows the options at once, like the buttons it names; a tick without an option change keeps it.
        TickNotebookPanel();Check(FindText(window,"Trajet demandé vers [12,-2].")!=nullptr,"Compact: a tick without an option change keeps the status line");
        owner.settings.notebookCopyButton=owner.settings.notebookTravelButton=false;TickNotebookPanel();
        Check(allLines(CopyButton,false)&&allLines(TravelButton,false)&&FindText(window,"Clic droit sur une position pour lancer le trajet.")&&
            !FindText(window,"Copie les coordonnées d’une position ou lance le trajet avec les boutons de sa ligne."),"Compact: both options off, the status no longer names the line buttons, at once");
        owner.settings.notebookCopyButton=true;TickNotebookPanel();
        Check(allLines(CopyButton,true)&&FindText(window,"Copie les coordonnées d’une position avec le bouton de sa ligne.")!=nullptr,"Compact: the copy option back on, the status names its button at once");
        owner.settings.notebookTravelButton=true;TickNotebookPanel();
        Check(allLines(TravelButton,true)&&FindText(window,"Copie les coordonnées d’une position ou lance le trajet avec les boutons de sa ligne.")!=nullptr,"Compact: both options on again, the status names both buttons at once");
        owner.settings.notebookTravelButton=false;Check(toolsUi::ChooseLayout("notebook",0)&&toolsUi::ChooseLayout("notebook",1),"Layout round trip, travel button off");
        Check(FindText(window,"Copie les coordonnées d’une position avec le bouton de sa ligne.")!=nullptr,"Compact status with the copy button alone");
        owner.settings.notebookCopyButton=false;owner.settings.notebookTravelButton=true;
        Check(toolsUi::ChooseLayout("notebook",0)&&toolsUi::ChooseLayout("notebook",1),"Layout round trip, copy button off");
        Check(FindText(window,"Lance le trajet vers une position avec le bouton de sa ligne.")!=nullptr,"Compact status with the travel button alone");
        owner.settings.notebookTravelButton=false;Check(toolsUi::ChooseLayout("notebook",0)&&toolsUi::ChooseLayout("notebook",1),"Layout round trip, both off");
        Check(FindText(window,"Clic droit sur une position pour lancer le trajet.")!=nullptr,"Compact status without either button: the right-click menu");
        owner.settings.notebookCopyButton=owner.settings.notebookTravelButton=true;
        ClickTab(window,"Notes");Check(FindText(window,"Choisis une entrée, puis Copier.")!=nullptr,"Compact status of the notes");
        ClickTab(window,"Messages");Check(FindText(window,"Choisis une entrée, puis Envoyer, Préparer dans le chat ou Copier.")!=nullptr,"Compact status of the messages");
        RestoreNotebookPanel();
        // A window opened compact has them too; without them and without the game's menu, its status says where they are.
        OpenNotebookPanel();TickNotebookPanel();window=latestWindow;firstRow=FindText(window,"[7,7]  Info-bulle");
        Check(Panel(window)&&Get(window).dimensions["width"]==360&&firstRow&&Shown(LineButton(firstRow,CopyButton))&&Shown(LineButton(firstRow,TravelButton)),"A window opened compact has the line buttons");
        RestoreNotebookPanel();menusAvailable=false;owner.settings.notebookCopyButton=owner.settings.notebookTravelButton=false;
        OpenNotebookPanel();TickNotebookPanel();window=latestWindow;
        Check(FindText(window,"Les boutons de copie et de voyage des positions sont désactivés dans les options du mod.")!=nullptr,"Neither button nor the game's menu: the status says where the buttons are");
        RestoreNotebookPanel();menusAvailable=true;owner.settings.notebookCopyButton=owner.settings.notebookTravelButton=true;owner.settings.notebookCompact=false;
        toolsUi::Restore();owner.cached.reset();owner.compositor.reset();module=nullptr;Check(handles.empty(),"No GC handles leaked");
        std::filesystem::remove_all(directory);
        std::cout<<"PASS: notebook native tabs/buttons/sections, draft protection, explicit travel/chat, literal text, map pin buttons, copy and travel buttons on the position lines, row menus, automatic reload and cleanup\n";return 0;
    }catch(const std::exception& error){std::cerr<<"FAIL: "<<error.what()<<'\n';for(const auto& message:logs)std::cerr<<message<<'\n';return 1;}
}
