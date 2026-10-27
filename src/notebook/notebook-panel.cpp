#include "notebook/notebook-panel.hpp"
#include "notebook/notebook-data.hpp"
#include "notebook/notebook-native.hpp"
#include "ui/native-click.hpp"
#include "souls/soul-panel.hpp"
#include "ui/tools-ui.hpp"
#include "core/module.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <optional>
namespace {
using namespace managed;
constexpr const char* Ui="UnityEngine.UIElementsModule.dll";
constexpr const char* UiSpace="UnityEngine.UIElements";
constexpr const char* Figma="Core.UILogic.Components.Figma";
constexpr size_t Rows=6;
// FigmaIcons used by the panel: pin (window and positions), text (notes),
// chat (messages, and the prepare button of a message's line), plus, floppy,
// trash, zaap (the travel button of a position's line), copy (the copy button
// of every line), send (the send button of a message's line), playerStanding,
// lines, edit, flag (the map pin of a position).
constexpr int IconPin=225,IconText=305,IconChat=78,IconPlus=230,IconSave=148,IconTrash=316,IconZaap=336,IconCopy=99,IconPlayer=229,IconList=185,IconEdit=124,IconPrevious=44,IconNext=45,IconSend=262,IconChannel=193,IconFlag=145;
// While the panel is open, the notebook file and the map pins are checked
// every half second (TickNotebookPanel runs at the module's pump, about every
// 200 ms); CheckNotebookPanel checks at once.
constexpr uint64_t PollMs=500;
// The notebook file as last read. Every save replaces it with a new file, so
// its file index changes even when two saves of the same size fall within
// one tick of the clock (an editor writing in place keeps the index: then the
// write time or the size tells the change).
struct FileStamp {
    bool exists=false; uint64_t written=0,size=0,index=0; DWORD volume=0;
    bool operator==(const FileStamp&) const = default;
};
constexpr const char* PinShow="Afficher cette position sur la carte : un repère sur la grande carte et la mini-carte.";
constexpr const char* PinHide="Retirer le repère de cette position de la carte.";
// The copy button's tooltip in each tab, in notebook::Kind order.
constexpr const char* CopyTips[]={"Copier les coordonnées de cette position dans le presse-papiers.",
    "Copier le texte de cette note dans le presse-papiers.","Copier ce message dans le presse-papiers."};
constexpr const char* TravelTip="Lancer le trajet vers cette position : zaaps, bateaux puis /travel.";
constexpr const char* PrepareTip="Placer ce message dans le brouillon du chat, sans l’envoyer.";
struct State {
    Ref root,window,header,dragger,dependencies,cached,compositor,closeButton;
    Ref title,body,x,y,world,coordinates,status,footer,save,remove,add,current,previous,next,paging,empty,channelButton,channelRow;
    Ref library,editorColumn; // Layouts: the two columns.
    int channelChoice=0; // Messages: notebook::Channels value shown on the channel button.
    Ref worldDropdown; int worldChoice=1; std::vector<notebook::World> worlds; // Positions: native dropdown of the game's worlds.
    std::array<Ref,3> tabs;
    std::array<Ref,4> inputEvents;
    // Each entry row, its line and, at its right, its small buttons: the map pin
    // of a position, the send and prepare buttons of a message, the copy button
    // of every entry and the travel button of a position.
    std::array<Ref,Rows> rows,lines,pins,sends,prepares,copies,travels;
    std::array<int,Rows> pinStates{}; // Pin button drawn: 0 not yet, 1 hidden, 2 no flag, 3 flag on the map.
    std::vector<Ref> manipulators;
    notebook::Document document;
    notebook::Entry editing;
    notebook::Entry captured;
    std::vector<notebook::Entry> visible;
    notebook::Kind kind=notebook::Kind::Position;
    size_t offset=0;
    bool cleanup=false,confirmDelete=false,confirmDiscard=false,readable=true;
    bool compact=false; // Compact layout: the entries and their line buttons only, no editor (settings.notebookCompact, applied in place).
    bool pinButtons=false; // Pin buttons shown: option on, positions tab, game flags available.
    bool copyButtons=false,sendButtons=false,travelButtons=false; // Line buttons shown: their option on, their tab (copy: every tab).
    FileStamp loaded; uint64_t nextPoll=0;
    bool retry=false; // The last read failed: read again at the next check, changed or not.
    std::array<bool,Rows> rowMenus{}; // Rows that have the game's right-click menu.
    std::string pinNote; // What the last reload could not do with a pin, for the status line.
    uint64_t statusUntil=0; // When the status line's feedback goes; 0 while it is empty or warns of an unreadable notebook.
    std::filesystem::path path;
} state;
bool installed=false,wanted=false;
// The unsaved draft of a notebook the game closed by leaving the world
// (CloseNotebookPanelOffWorld): given back in the form, as it was typed, the
// next time the player opens the notebook in its full layout.
struct Draft {
    notebook::Kind kind=notebook::Kind::Position; notebook::Entry editing,captured;
    std::string title,body,x,y; int world=1,channel=0;
};
std::optional<Draft> offWorldDraft;
Method unsubscribe=nullptr;
bool Session() {
    return module && module->ready && module->servicesReady && !module->stopped && !module->stopRequested;
}
// Polled failures repeat each check: one line per message every ten seconds.
void LogLimited(const std::string& line) {
    static std::string previous; static uint64_t next=0;
    const auto now=GetTickCount64();
    if(line==previous&&now<next)return;
    previous=line;next=now+10000;Log(line);
}
Obj Style(Obj element, const char* property) {
    Ref style(api.Call(element,"get_style"));
    Obj box = api.Call(style.get(),(std::string("UnityEngine.UIElements.IStyle.get_")+property).c_str());
    if (!box || !api.class_is_valuetype(api.object_get_class(box))) throw std::runtime_error("Notebook style unavailable");
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
// The game's own button component (same as its window and toolbar buttons):
// native hover, pressed and disabled states, icon and caption.
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
// Native list rows (choice style) for the entries: hover and activated states.
Obj ListRow(Obj parent) {
    Ref row(api.New(api.Class("Core.dll",Figma,"ListElementBasic")));
    Ref label(api.Field(row.get(),"m_textLabel"));
    bool rich=false;api.Call(label.get(),"set_enableRichText",{&rich});
    int style=0; api.Call(row.get(),"set_listElementStyle",{&style});
    Pixels(row.get(),"minHeight",36); Pixels(row.get(),"minWidth",0); Float(row.get(),"flexGrow",1);
    Add(parent,row.get()); return row.get();
}
// A small flat icon button (tertiary style, like the paging arrows) at the right of an entry line.
void LineButton(Ref& button, Obj line, int icon, const char* tooltip) {
    button.reset(Button(line,"",icon,2,0,tooltip));
    Pixels(button.get(),"marginRight",0); Pixels(button.get(),"marginBottom",0); Pixels(button.get(),"marginLeft",4);
    Float(button.get(),"flexShrink",0);
}
// An entry line: the native list row, then its small buttons, each hidden
// until its tab and its option show it: the map pin of a position, the send
// and prepare buttons of a message, the copy button of every entry and the
// travel button of a position. The row also opens the game's right-click menu
// (notebook-native).
void EntryLine(Obj parent, size_t index) {
    Ref line(api.New(api.Class(Ui,UiSpace,"VisualElement")));
    Enum(line.get(),"flexDirection",2); Enum(line.get(),"alignItems",2); Pixels(line.get(),"marginBottom",2); Enum(line.get(),"display",1);
    Add(parent,line.get()); state.lines[index].reset(line.get());
    state.rows[index].reset(ListRow(line.get()));
    LineButton(state.pins[index],line.get(),IconFlag,PinShow);
    LineButton(state.sends[index],line.get(),IconSend,"Envoyer ce message dans le chat.");Enum(state.sends[index].get(),"display",1);
    LineButton(state.prepares[index],line.get(),IconChat,PrepareTip);Enum(state.prepares[index].get(),"display",1);
    LineButton(state.copies[index],line.get(),IconCopy,CopyTips[0]);Enum(state.copies[index].get(),"display",1);
    LineButton(state.travels[index],line.get(),IconZaap,TravelTip);Enum(state.travels[index].get(),"display",1);
    state.rowMenus[index]=notebook::AttachRowMenu(state.rows[index].get());
}
Obj Tab(Obj parent, const char* text) {
    Ref tab(api.New(api.Class("Core.dll",Figma,"DofusTab")));
    int size=1,caps=1; api.Call(tab.get(),"set_size",{&size}); api.Call(tab.get(),"set_fontCase",{&caps});
    Text(tab.get(),text); Float(tab.get(),"flexGrow",1); Pixels(tab.get(),"minWidth",0);
    Add(parent,tab.get()); return tab.get();
}
Obj Divider(Obj parent, const char* inlineText=nullptr) {
    Ref divider(api.New(api.Class("Core.dll",Figma,"Divider")));
    int simple=0,horizontal=0; api.Call(divider.get(),"set_componentStyle",{&simple}); api.Call(divider.get(),"set_orientation",{&horizontal});
    if(inlineText)Text(divider.get(),inlineText,"set_inlineText");
    Pixels(divider.get(),"marginTop",4); Pixels(divider.get(),"marginBottom",8);
    Add(parent,divider.get()); return divider.get();
}
// Native titled section (the same SectionHeader as the map's resource
// drawer), opened then locked; returns its content container. The native
// set_isOpen keeps the previous (closed) state while canBeOpened is off, which
// it is after the constructor: open it while openable, then lock it.
Obj Section(Obj parent, const char* title, int icon, Obj* header=nullptr) {
    bool subContent=true,folderButton=true,no=false,hasIcon=true,open=true,openable=true,locked=false;
    Ref section(api.New(api.Class("Core.dll",Figma,"SectionHeader"),{&subContent,&folderButton,&no,&hasIcon,&no}));
    Text(section.get(),title,"set_title"); api.Call(section.get(),"set_icon",{&icon});
    api.Call(section.get(),"set_canBeOpened",{&openable}); api.Call(section.get(),"set_isOpen",{&open}); api.Call(section.get(),"set_canBeOpened",{&locked});
    Ref content(api.New(api.Class(Ui,UiSpace,"VisualElement")));
    for(auto p:{"paddingLeft","paddingRight"})Pixels(content.get(),p,4); Pixels(content.get(),"paddingTop",8);
    api.Call(section.get(),"AddSubContent",{content.get()});
    Add(parent,section.get()); if(header)*header=section.get(); return content.get();
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
    if (count<0 || count>64) throw std::runtime_error("Notebook UI layer bounds");
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
std::string Coordinates(const notebook::Entry& entry) {return "["+std::to_string(entry.x)+","+std::to_string(entry.y)+"]";}
std::string SendTip(const notebook::Entry& entry) {return std::string("Envoyer ce message dans le chat (canal ")+notebook::ChannelName(entry.channel)+").";}
// The status line under the lists answers an action or a change of the file
// for a few seconds, longer for a longer text, then goes (IdleStatus) and
// leaves no room. A new line replaces « reclique pour les abandonner »:
// dropping a draft then asks again.
constexpr uint64_t StatusMs=4000,StatusMsPerByte=40;
void Status(const std::string& text) {
    state.confirmDiscard=false;
    Text(state.status.get(),text);Enum(state.footer.get(),"display",text.empty()?1:0);
    state.statusUntil=text.empty()?0:GetTickCount64()+StatusMs+StatusMsPerByte*text.size();
}
void ChannelLabel() {Text(state.channelButton.get(),std::string("Canal : ")+notebook::ChannelName(state.channelChoice));}
std::string WorldLabel(int id) {
    for(const auto& world:state.worlds)if(world.id==id)return std::to_string(id)+" : "+world.name;
    return std::to_string(id)+" : monde inconnu";
}
void AddWorldItem(int id) {
    Ref label(api.string_new(WorldLabel(id).c_str()));int choice=0,none=0,item=id;
    api.Call(state.worldDropdown.get(),"AddListElementBasic",{label.get(),nullptr,&choice,&none,&none,&item});
}
// Selects a world in the native dropdown; an id the game does not list (older
// entry) gets its own item so the entry stays editable.
void SelectWorld(int id) {
    bool known=false;for(const auto& world:state.worlds)known=known||world.id==id;
    if(!known){state.worlds.push_back({id,"monde inconnu"});AddWorldItem(id);}
    int item=id;api.Call(state.worldDropdown.get(),"SelectItemIdWithoutNotify",{&item});
    Text(state.worldDropdown.get(),WorldLabel(id),"set_choiceText");state.worldChoice=id;
}
// The native dropdown selects on its own clicks; the panel reads the choice each frame.
void PollWorld() {
    if(!state.worldDropdown.get())return;
    Ref selected(api.Call(state.worldDropdown.get(),"get_selectedElement"));
    if(!selected.get())return;
    const int id=*static_cast<int*>(api.FieldAddress(selected.get(),"id"));
    if(id!=state.worldChoice){state.worldChoice=id;Text(state.worldDropdown.get(),WorldLabel(id),"set_choiceText");}
}
int KindIcon(notebook::Kind kind) { return kind==notebook::Kind::Position?IconPin:kind==notebook::Kind::Note?IconText:IconChat; }
// « Bouton de repère à côté des positions » (Settings::notebookPingButton),
// read every frame: switching it shows or hides the buttons at once.
bool PinButtonsWanted() {
    return module && module->settings.notebookPingButton && state.kind==notebook::Kind::Position && notebook::PinsAvailable();
}
bool Pinned(const notebook::Entry& entry) {
    try {return notebook::PinShown(entry);}
    catch(const std::exception& error){LogLimited(std::string("NOTEBOOK_PIN_STATE_ERROR ")+error.what());return false;}
}
// A row's pin button: flat while the position has no flag on the map, filled
// (the game's primary style, as a pressed toggle) while its flag is shown.
void PinState(size_t row, int drawn) {
    const int before=state.pinStates[row];
    if(before==drawn)return;
    Obj pin=state.pins[row].get();
    if(drawn>1){int style=drawn==3?0:2;api.Call(pin,"set_mainStyle",{&style});Text(pin,drawn==3?PinHide:PinShow,"set_tooltip");}
    if(before==0||(before>1)!=(drawn>1))Enum(pin,"display",drawn>1?0:1);
    state.pinStates[row]=drawn;
}
void UpdatePins() {
    state.pinButtons=PinButtonsWanted();
    for(size_t i=0;i<Rows;++i){
        const auto index=state.offset+i;
        PinState(i,state.pinButtons&&index<state.visible.size()?(Pinned(state.visible[index])?3:2):1);
    }
}
// « Bouton de copie à côté des entrées » (every tab), « Boutons d’envoi à côté
// des messages » and « Bouton de voyage à côté des positions »
// (Settings::notebookCopyButton, notebookSendButtons, notebookTravelButton),
// read every frame like the pin option: switching one shows or hides its
// buttons at once.
bool CopyButtonsWanted() {return module&&module->settings.notebookCopyButton;}
bool SendButtonsWanted() {return module&&module->settings.notebookSendButtons&&state.kind==notebook::Kind::Message;}
bool TravelButtonsWanted() {return module&&module->settings.notebookTravelButton&&state.kind==notebook::Kind::Position;}
// The send, prepare, copy and travel buttons of every line, redrawn when their
// option or the tab changes (the hidden line of an empty row hides them with it).
void UpdateLineButtons() {
    const bool copy=CopyButtonsWanted(),send=SendButtonsWanted(),travel=TravelButtonsWanted();
    for(size_t i=0;i<Rows;++i){
        if(send!=state.sendButtons){Enum(state.sends[i].get(),"display",send?0:1);Enum(state.prepares[i].get(),"display",send?0:1);}
        if(copy!=state.copyButtons)Enum(state.copies[i].get(),"display",copy?0:1);
        if(travel!=state.travelButtons)Enum(state.travels[i].get(),"display",travel?0:1);
    }
    state.copyButtons=copy;state.sendButtons=send;state.travelButtons=travel;
}
// The row's right-click menu, told on hover where the row has it.
std::string RowTip(size_t row) {
    if(!state.rowMenus[row])return {};
    return notebook::PinsAvailable()?"Clic droit : Voyager, Afficher sur la carte ou Retirer de la carte.":"Clic droit : Voyager.";
}
// An empty tab says so inside its list, with how to create an entry in this
// layout; nothing while the file cannot be read (the status line warns).
void EmptyNotice() {
    const bool shown=state.readable&&state.visible.empty();
    Enum(state.empty.get(),"display",shown?0:1);
    if(shown)Text(state.empty.get(),state.compact?"Aucune entrée ici. Passe en Version complète (menu …) pour en créer.":"Aucune entrée ici. Clique sur « Nouvelle entrée » pour commencer.");
}
void ShowRows() {
    state.visible.clear();
    for(const auto& entry:state.document.entries) if(entry.kind==state.kind)state.visible.push_back(entry);
    if(state.offset>=state.visible.size())state.offset=state.visible.empty()?0:((state.visible.size()-1)/Rows)*Rows;
    const int icon=KindIcon(state.kind);
    const bool position=state.kind==notebook::Kind::Position;
    for(size_t i=0;i<Rows;++i){
        const auto index=state.offset+i;const bool show=index<state.visible.size();
        Enum(state.lines[i].get(),"display",show?0:1);
        if(show){
            const auto& entry=state.visible[index];
            Text(state.rows[i].get(),(position?Coordinates(entry)+"  ":"")+entry.title);
            Text(state.rows[i].get(),position?RowTip(i):"","set_tooltip");
            api.Call(state.rows[i].get(),"set_icon",{const_cast<int*>(&icon)});
            bool activated=entry.id!=0&&entry.id==state.editing.id;api.Call(state.rows[i].get(),"set_isActivated",{&activated});
            // The copy button names what it copies; a message's send button, its channel.
            Text(state.copies[i].get(),CopyTips[static_cast<size_t>(state.kind)],"set_tooltip");
            if(state.kind==notebook::Kind::Message)Text(state.sends[i].get(),SendTip(entry),"set_tooltip");
        }
    }
    // No entry counter: the paging arrows only when the tab has a second page.
    Enum(state.paging.get(),"display",state.visible.size()>Rows?0:1);
    Enabled(state.previous.get(),state.offset>0);Enabled(state.next.get(),state.offset+Rows<state.visible.size());
    EmptyNotice();UpdatePins();UpdateLineButtons();
}
// Save and delete follow the file: disabled while it cannot be read.
void Actions() {Enabled(state.remove.get(),state.editing.id!=0&&state.readable);Enabled(state.save.get(),state.readable);}
void Fill(notebook::Entry entry) {
    entry.kind=state.kind;state.editing=std::move(entry);state.captured=state.editing;state.confirmDelete=state.confirmDiscard=false;
    state.channelChoice=state.editing.channel;ChannelLabel();
    Text(state.remove.get(),"Supprimer");
    Text(state.title.get(),state.editing.title);Text(state.body.get(),state.editing.body);
    Text(state.x.get(),std::to_string(state.editing.x));Text(state.y.get(),std::to_string(state.editing.y));SelectWorld(state.editing.world);
    const bool position=state.kind==notebook::Kind::Position;
    Enum(state.coordinates.get(),"display",position?0:1);Enum(state.body.get(),"display",position?1:0);
    // Every entry is used from its own line (copy, send, prepare, travel): the form only edits it.
    Enum(state.channelRow.get(),"display",state.kind==notebook::Kind::Message?0:1);
    bool multiline=state.kind==notebook::Kind::Note;api.Call(state.body.get(),"set_isMultiline",{&multiline});
    int max=multiline?8000:300;api.Call(state.body.get(),"set_maxLength",{&max});Pixels(state.body.get(),"height",multiline?160.f:36.f);
    Text(state.body.get(),state.kind==notebook::Kind::Note?"Écris ta note ici…":"Message à préparer dans le chat","set_placeholderText");
    Actions();
    for(size_t i=0;i<3;++i){bool on=static_cast<size_t>(state.kind)==i;api.Call(state.tabs[i].get(),"SetValueWithoutNotify",{&on});}
}
bool Dirty() {
    if(state.compact)return false; // Nothing is editable in the compact layout.
    return Read(state.title.get())!=state.editing.title || Read(state.body.get())!=state.editing.body || state.channelChoice!=state.editing.channel ||
        (state.kind==notebook::Kind::Position&&(Read(state.x.get())!=std::to_string(state.editing.x)||Read(state.y.get())!=std::to_string(state.editing.y)||state.worldChoice!=state.editing.world));
}
bool CanDiscard() {
    if(!Dirty()||state.confirmDiscard){state.confirmDiscard=false;return true;}
    Status("Modifications non enregistrées. Enregistre-les ou reclique pour les abandonner.");state.confirmDiscard=true;return false;
}
notebook::Entry Editor() {
    auto entry=state.editing;entry.title=Read(state.title.get());entry.body=Read(state.body.get());entry.kind=state.kind;
    entry.channel=state.kind==notebook::Kind::Message?state.channelChoice:0;
    if(state.kind==notebook::Kind::Position){
        if(!notebook::Coordinate(Read(state.x.get()),entry.x)||!notebook::Coordinate(Read(state.y.get()),entry.y))throw std::runtime_error("Renseigne des coordonnées entières.");
        entry.world=state.worldChoice;if(entry.world<0)throw std::runtime_error("Choisis un monde dans la liste.");
        entry.map=entry.x==state.captured.x&&entry.y==state.captured.y&&entry.world==state.captured.world?state.captured.map:0;
    }
    return entry;
}
FileStamp Stamp(const std::filesystem::path& path) {
    // Attributes only: never in the way of a reader or of a client replacing the file.
    const HANDLE file=CreateFileW(path.c_str(),FILE_READ_ATTRIBUTES,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(file==INVALID_HANDLE_VALUE)return {};
    BY_HANDLE_FILE_INFORMATION info{};const bool known=GetFileInformationByHandle(file,&info)!=0;CloseHandle(file);
    if(!known)return {};
    const auto join=[](DWORD high,DWORD low){return (static_cast<uint64_t>(high)<<32)|low;};
    return {true,join(info.ftLastWriteTime.dwHighDateTime,info.ftLastWriteTime.dwLowDateTime),join(info.nFileSizeHigh,info.nFileSizeLow),
        join(info.nFileIndexHigh,info.nFileIndexLow),info.dwVolumeSerialNumber};
}
// The pins this module placed follow the notebook, whoever changed it: a
// position deleted (here or elsewhere) takes its pin off the map, an edited one
// (same id, newer version) moves it, and an id another position took over
// (a recreated file numbers from 1 again) loses its pin.
void ReconcilePins() {
    if(!notebook::PinsAvailable())return;
    std::vector<notebook::Entry> pins;
    try {pins=notebook::PlacedPins();}
    catch(const std::exception& error){LogLimited(std::string("NOTEBOOK_PIN_STATE_ERROR ")+error.what());return;}
    for(const auto& pin:pins){
        const auto found=std::find_if(state.document.entries.begin(),state.document.entries.end(),[&](const notebook::Entry& entry){return entry.id==pin.id&&entry.kind==notebook::Kind::Position;});
        const bool listed=found!=state.document.entries.end();
        if(listed&&found->x==pin.x&&found->y==pin.y&&found->world==pin.world&&found->title==pin.title)continue;
        try {if(listed&&found->version>pin.version)notebook::ShowPin(*found);else notebook::HidePin(pin.id);}
        catch(const std::exception& error){
            bool kept=false;try {kept=notebook::PinShown(pin);}catch(const std::exception&){}
            state.pinNote=std::string(kept?"Repère non déplacé : ":"Repère retiré de la carte : ")+error.what();
        }
    }
}
// A status line, followed by what the last reload could not do with a pin.
std::string Noted(const std::string& text) {
    auto line=state.pinNote.empty()?text:text.empty()?state.pinNote:text+" "+state.pinNote;
    state.pinNote.clear();return line;
}
void Reload() {
    // Stamped before the read: a save landing during it is seen at the next check.
    state.loaded=Stamp(state.path);
    try {state.document=notebook::Read(state.path);state.readable=true;state.retry=false;}
    catch(...){
        // Read again at the next check even if the file stays the same (a backup,
        // a sync tool or an editor may hold it a moment).
        state.retry=true;state.readable=false;Enabled(state.save.get(),false);Enabled(state.remove.get(),false);EmptyNotice();throw;
    }
    ReconcilePins();
    ShowRows();
}
// A read failure stays reported until the file can be read again (saving stays disabled meanwhile).
constexpr const char* Unreadable="Carnet illisible. Le fichier est conservé ; il sera relu dès qu’il aura été corrigé.";
// The status line at rest (at opening, on tab and layout changes, after a
// selection and when a feedback goes): the warning of an unreadable notebook,
// which stays; else what the last reload could not do with a pin; else
// nothing, and no room taken. No hint stays there: an empty tab says so in its
// list (EmptyNotice) and an entry is used from the buttons of its line.
void IdleStatus() {
    if(!state.readable){Status(Unreadable);state.statusUntil=0;return;}
    Status(Noted(""));
}
// The file changed on disk (another client, a position added from a map, a
// manual fix): the list is read again at once. An unsaved draft is never
// replaced; its save still meets the concurrent-modification check.
void Refresh() {
    const bool dirty=Dirty(),wasReadable=state.readable;
    try {Reload();}
    // Told at the first failure; while the file stays unreadable, a feedback still
    // has its time before the warning comes back (IdleStatus).
    catch(const std::exception& error){if(wasReadable)IdleStatus();LogLimited(std::string("NOTEBOOK_RELOAD_ERROR ")+error.what());return;}
    Log("NOTEBOOK_RELOAD file_changed=1 draft="+std::string(dirty?"1":"0"));
    const auto saved=std::find_if(state.document.entries.begin(),state.document.entries.end(),[](const notebook::Entry& entry){return entry.id==state.editing.id&&entry.kind==state.editing.kind;});
    const bool gone=state.editing.id&&saved==state.document.entries.end();
    if(dirty){
        // A new notice: the earlier « reclique pour les abandonner » no longer holds.
        state.confirmDiscard=false;
        if(gone){
            // Deleted elsewhere: the draft becomes a new entry (its id cannot be saved again).
            state.editing.id=state.editing.version=0;ShowRows();Actions();
            Status(Noted("Cette entrée a été supprimée ailleurs. Ton brouillon est conservé : Enregistrer en fera une nouvelle entrée."));return;
        }
        Actions();Status(Noted("Le carnet a changé ailleurs. Ton brouillon est conservé."));return;
    }
    // The selected entry follows its saved version; deleted, the form is emptied.
    if(gone){Fill({});ShowRows();Status(Noted("L’entrée affichée a été supprimée ailleurs."));return;}
    if(state.editing.id&&saved->version!=state.editing.version){Fill(*saved);ShowRows();}
    else Actions();
    if(!wasReadable)IdleStatus(); else Status(Noted("Carnet mis à jour : le fichier a changé."));
}
// True when the file was read again.
bool PollFile() {
    if(!state.retry&&Stamp(state.path)==state.loaded)return false;
    Refresh();return true;
}
// The file and the pins, now.
void Check() {
    state.nextPoll=GetTickCount64()+PollMs;
    if(!PollFile())UpdatePins();
}
void SwitchTab(size_t tab) {
    state.kind=static_cast<notebook::Kind>(tab);state.offset=0;Fill({});ShowRows();IdleStatus();
}
// The native tabs toggle themselves on click; the panel follows their state
// each frame and refuses a change while an edit is unsaved.
void PollTabs() {
    for(size_t i=0;i<3;++i){
        if(static_cast<size_t>(state.kind)==i||!api.Value<bool>(api.Call(state.tabs[i].get(),"get_isOn")))continue;
        if(!CanDiscard()){bool off=false;api.Call(state.tabs[i].get(),"SetValueWithoutNotify",{&off});return;}
        SwitchTab(i);return;
    }
}
// The pin button of a position: its flag on the map goes, or comes.
void TogglePin(const notebook::Entry& entry) {
    try {
        if(notebook::PinShown(entry)){notebook::HidePin(entry.id);Status("Repère retiré de la carte.");}
        else{notebook::ShowPin(entry);Status("Repère placé sur la carte en "+Coordinates(entry)+" : grande carte et mini-carte.");}
    } catch(...){UpdatePins();throw;}
    UpdatePins();
}
// The buttons of a line act on that line's entry as saved, never on the form:
// the selection, the editor and an unsaved draft stay. A position is copied as
// its coordinates, a note or a message as its text; a message is sent on its
// own saved channel, or placed in the chat's draft. Their status replaces
// « reclique pour les abandonner » (Status): dropping the draft asks again.
void CopyLine(notebook::Entry entry) {
    notebook::Copy(entry.kind==notebook::Kind::Position?Coordinates(entry):entry.body);Status("Copié dans le presse-papiers.");
}
void TravelLine(notebook::Entry entry) {
    notebook::Travel(entry);Status("Trajet demandé vers "+Coordinates(entry)+".");
}
void SendLine(notebook::Entry entry) {
    notebook::SendChatMessage(entry.body,entry.channel);Status(std::string("Message envoyé dans le chat (canal ")+notebook::ChannelName(entry.channel)+").");
}
void PrepareLine(notebook::Entry entry) {
    notebook::PrepareMessage(entry.body);Status("Ajouté au brouillon du chat. À toi de l’envoyer.");
}
// The line (0 to Rows-1) of a row or of one of its buttons; -1 for any other element.
int LineOf(const std::array<Ref,Rows>& elements, Obj owner) {
    for(size_t i=0;i<Rows;++i)if(owner&&owner==elements[i].get())return static_cast<int>(i);
    return -1;
}
bool Action(Obj owner) {
    const int row=LineOf(state.rows,owner),pin=LineOf(state.pins,owner),send=LineOf(state.sends,owner),prepare=LineOf(state.prepares,owner),
        copy=LineOf(state.copies,owner),travel=LineOf(state.travels,owner);
    const bool own=owner&&(row>=0||pin>=0||send>=0||prepare>=0||copy>=0||travel>=0||owner==state.save.get()||owner==state.remove.get()||owner==state.add.get()||
        owner==state.current.get()||owner==state.channelButton.get()||owner==state.closeButton.get()||owner==state.previous.get()||owner==state.next.get());
    if(!own)return false;
    try {
        if(!wanted||!Current()||!api.Value<bool>(api.Call(owner,"get_enabledInHierarchy")))return true;
        if(owner!=state.remove.get()){state.confirmDelete=false;Text(state.remove.get(),"Supprimer");}
        if(owner==state.closeButton.get()){if(CanDiscard())wanted=false;return true;}
        // A selection or a new entry shows in the list and the form: the status line goes back to rest.
        if(row>=0&&state.offset+row<state.visible.size()){if(!CanDiscard())return true;Fill(state.visible[state.offset+row]);ShowRows();IdleStatus();}
        else if(pin>=0){if(state.pinButtons&&state.offset+pin<state.visible.size())TogglePin(state.visible[state.offset+pin]);}
        else if(send>=0){if(state.sendButtons&&state.offset+send<state.visible.size())SendLine(state.visible[state.offset+send]);}
        else if(prepare>=0){if(state.sendButtons&&state.offset+prepare<state.visible.size())PrepareLine(state.visible[state.offset+prepare]);}
        else if(copy>=0){if(state.copyButtons&&state.offset+copy<state.visible.size())CopyLine(state.visible[state.offset+copy]);}
        else if(travel>=0){if(state.travelButtons&&state.offset+travel<state.visible.size())TravelLine(state.visible[state.offset+travel]);}
        else if(owner==state.add.get()){if(!CanDiscard())return true;Fill({});ShowRows();IdleStatus();}
        else if(owner==state.save.get()){auto saved=notebook::Save(state.path,Editor());Reload();Fill(saved);ShowRows();Status(Noted("Enregistré dans le carnet."));}
        else if(owner==state.remove.get()&&state.editing.id){
            if(!state.confirmDelete){state.confirmDelete=true;Text(state.remove.get(),"Confirmer la suppression");Status("Reclique sur Supprimer pour confirmer.");}
            else {notebook::Delete(state.path,state.editing.id,state.editing.version);Reload();Fill({});ShowRows();Status(Noted("Entrée supprimée."));}
        }
        else if(owner==state.previous.get()&&state.offset>=Rows){state.offset-=Rows;ShowRows();}
        else if(owner==state.next.get()&&state.offset+Rows<state.visible.size()){state.offset+=Rows;ShowRows();}
        else if(owner==state.current.get()){
            const auto position=notebook::CurrentPosition();Text(state.x.get(),std::to_string(position.x));Text(state.y.get(),std::to_string(position.y));SelectWorld(position.world);
            state.captured=position;Status("Position actuelle renseignée. Clique sur Enregistrer pour la conserver.");
        }
        else if(owner==state.channelButton.get()){state.channelChoice=notebook::NextChannel(state.channelChoice);ChannelLabel();Status(std::string("Canal choisi : ")+notebook::ChannelName(state.channelChoice)+". Enregistre l’entrée pour que le bouton d’envoi de sa ligne utilise ce canal.");}
    }catch(const std::exception& error){Status(error.what());Log("NOTEBOOK_ACTION_ERROR operation_failed");}
    return true;
}
// The right-click menu of a row (notebook-native): the position it shows.
bool RowEntry(Obj row, notebook::Entry& entry) {
    if(!wanted||!Current()||state.kind!=notebook::Kind::Position)return false;
    for(size_t i=0;i<Rows;++i)
        if(row&&row==state.rows[i].get()&&state.offset+i<state.visible.size()){entry=state.visible[state.offset+i];return true;}
    return false;
}
// A menu action done: its result on the status line, the pins redrawn.
void RowDone(const notebook::Entry&, const std::string& status) {
    if(!wanted||!Current())return;
    UpdatePins();Status(status);
}
// Window size of each layout (full, compact); the native resizers start from it.
// The full one has the 70 pixels its list column gained for the copy and
// travel buttons of the position lines (1.16.0): the editor keeps its width.
// Each minimum height lost the room of the hints and of the entry counter
// (1.17.1): the status line and the paging row take none while they have
// nothing to show, so the window can be made that much shorter.
struct LayoutSize { float width, minWidth, minHeight; };
constexpr LayoutSize Sizes[2]={{730,630,300},{360,300,180}};
void WindowSize(bool compact) {
    const auto& size=Sizes[compact];
    Pixels(state.window.get(),"width",size.width);
    Pixels(state.window.get(),"minWidth",size.minWidth); Pixels(state.window.get(),"minHeight",size.minHeight);
}
// Inline position of the window (the native dragger writes it), else its resolved one.
float WindowPosition(const char* property) {
    Ref box(Style(state.window.get(),property));
    if(api.Value<int>(api.Call(box.get(),"get_keyword"))==0) {
        struct LengthValue { float value; int unit; };
        const auto length=api.Value<LengthValue>(api.Call(box.get(),"get_value"));
        if(length.unit==0&&std::isfinite(length.value))return length.value;
    }
    Ref resolved(api.Call(state.window.get(),"get_resolvedStyle"));
    return api.Value<float>(api.Call(resolved.get(),(std::string("get_")+property).c_str()));
}
// The game keeps a window inside the screen only while it is dragged
// (ElementDraggerWithBounds): after a layout change the window is moved back
// inside its layer by the new layout's width and minimum height (its resolved
// size still holds the old layout until the next layout pass).
void KeepInRoot(bool compact) {
    Ref resolved(api.Call(state.root.get(),"get_resolvedStyle"));
    const float limits[]={api.Value<float>(api.Call(resolved.get(),"get_width")),api.Value<float>(api.Call(resolved.get(),"get_height"))};
    const float sizes[]={Sizes[compact].width,Sizes[compact].minHeight};
    const char* properties[]={"left","top"};
    for(size_t i=0;i<2;++i) {
        const float position=WindowPosition(properties[i]);
        if(!std::isfinite(limits[i])||limits[i]<=0||!std::isfinite(position))continue;
        const float kept=std::clamp(position,0.f,std::max(0.f,limits[i]-sizes[i]));
        if(kept!=position)Pixels(state.window.get(),properties[i],kept);
    }
}
// Full layout: the entries, then the editor. Compact: a narrow window with the
// entries alone, each used from the buttons of its line; the editor hidden but
// built. Applied in place: the window, its position, tab and entry stay on screen.
void ApplyLayout(bool compact) {
    state.compact=compact; WindowSize(compact);
    Float(state.library.get(),"flexGrow",compact?1.f:0.f); Float(state.library.get(),"flexShrink",compact?1.f:0.f);
    Pixels(state.library.get(),"marginRight",compact?0.f:12.f);
    Enum(state.add.get(),"display",compact?1:0); Enum(state.editorColumn.get(),"display",compact?1:0);
    EmptyNotice();
}
// The open window in another layout: in place, kept inside its layer, the status line at rest.
void Relayout(bool compact) { ApplyLayout(compact); KeepInRoot(compact); IdleStatus(); }
// Layout chosen in the panel's ⋯ menu (0 full, 1 compact): saved, and applied
// to the open window in place. Unsaved edits need the confirming second
// choice, as for a tab change, and are then dropped.
bool SelectLayout(int layout) {
    if(!Session()||layout<0||layout>1)return false;
    const bool compact=layout==1;
    if(compact==module->settings.notebookCompact)return true;
    const bool open=wanted&&Current();
    if(open) {
        // Like every other panel control (Action): a pending delete confirmation is dropped.
        state.confirmDelete=false;Text(state.remove.get(),"Supprimer");
        const bool dirty=Dirty();if(!CanDiscard())return false;if(dirty)Fill(state.editing);
    }
    Settings next=module->settings;next.notebookCompact=compact;
    // Module::Change assigns the setting before saving it: when the save throws,
    // the open window still follows the setting the ⋯ menu marks.
    try{module->Change(next);}
    catch(...){if(open&&state.compact!=module->settings.notebookCompact)Relayout(module->settings.notebookCompact);throw;}
    if(open)Relayout(compact);
    return true;
}
void Retain(const char* field) {
    Ref list(api.Field(state.window.get(),field));
    const int count=api.Value<int>(api.Call(list.get(),"get_Count"));
    if(count<0 || count>32) throw std::runtime_error("Notebook native manipulator bounds");
    for(int i=0;i<count;++i) state.manipulators.emplace_back(api.Call(list.get(),"get_Item",{&i}));
}
// The kept draft back in the form: its tab, its entry and the fields as they
// were typed. Its save meets the concurrent-modification check like any edit;
// an entry deleted meanwhile becomes a new one, as in Refresh.
void GiveBackDraft() {
    if(!offWorldDraft||state.compact)return; // The compact layout has no form: the draft waits for the full one.
    const Draft draft=std::move(*offWorldDraft);offWorldDraft.reset();
    state.kind=draft.kind;state.offset=0;Fill(draft.editing);
    const bool gone=state.readable&&draft.editing.id&&std::none_of(state.document.entries.begin(),state.document.entries.end(),
        [&](const notebook::Entry& entry){return entry.id==draft.editing.id&&entry.kind==draft.kind;});
    if(gone)state.editing.id=state.editing.version=0;
    Text(state.title.get(),draft.title);Text(state.body.get(),draft.body);Text(state.x.get(),draft.x);Text(state.y.get(),draft.y);
    SelectWorld(draft.world);state.channelChoice=draft.channel;ChannelLabel();state.captured=draft.captured;
    ShowRows();Actions();Status("Brouillon non enregistré retrouvé : enregistre-le pour le garder.");
}
void Create(Obj root) {
    state.root.reset(root); state.dependencies.reset(module->FindService("fpt"));
    if(!state.dependencies.get() || api.object_get_class(state.dependencies.get())!=api.Class("Core.dll","","goc"))
        throw std::runtime_error("Notebook native dependencies unavailable");
    state.cached.reset(api.Call(state.dependencies.get(),"bfri")); state.compositor.reset(module->compositor.get());
    Ref input(api.Call(state.dependencies.get(),"bfre"));
    if(state.cached.get()!=module->cached.get() || !input.get() || api.object_get_class(input.get())!=api.Class("Core.dll","","eqo"))
        throw std::runtime_error("Notebook native session mismatch");
    state.window.reset(api.New(api.Class("Core.dll","Core.UILogic.Components","MagneticDragWindow")));
    Text(state.window.get(),"","set_name"); Text(state.window.get(),"Carnet personnel","set_headerTitle");
    bool yes=true; int icon=IconPin,size=1;
    api.Call(state.window.get(),"set_headerIcon",{&icon}); api.Call(state.window.get(),"set_headerSize",{&size});
    // The window's own close button; the game invokes no close callback of
    // its own for a window it does not manage, the panel handles the click.
    api.Call(state.window.get(),"set_headerShowCloseButton",{&yes});
    // Resizable like the game's own windows (map, chat): the native resizers
    // of the window; the body below fills the size the user gives it.
    for(const auto* setter:{"set_verticalResizable","set_horizontalResizable","set_cornerResizable"})
        api.Call(state.window.get(),setter,{&yes});
    api.Call(state.window.get(),"set_hasBackground",{&yes});
    Enum(state.window.get(),"position",1); Float(state.window.get(),"flexGrow",0);
    WindowSize(module->settings.notebookCompact); Pixels(state.window.get(),"left",80); Pixels(state.window.get(),"top",90);
    state.header.reset(api.Call(state.window.get(),"get_headerWidget"));
    state.closeButton.reset(api.Field(state.header.get(),"closeButton"));
    if(!state.closeButton.get()) throw std::runtime_error("Notebook native close button unavailable");
    toolsUi::AttachHeader("notebook",state.window.get());
    Ref draggers(api.Field(state.window.get(),"m_registeredElementDraggers"));
    state.dragger.reset(api.Call(draggers.get(),"get_Item",{state.header.get()}));
    Retain("m_elementDraggers"); Retain("m_resizerList");
    const char* getters[]={"ydf","ydh","ydj","ydl"};
    for(size_t i=0;i<4;++i) state.inputEvents[i].reset(api.Call(input.get(),getters[i]));
    api.Call(state.window.get(),"Init",{state.dependencies.get()});
    Ref content(toolsUi::ScrollBody(state.window.get(),root,true));
    Float(content.get(),"flexGrow",1);
    for(const auto* property:{"paddingLeft","paddingRight","paddingTop","paddingBottom"}) Pixels(content.get(),property,10);
    // Native tab bar.
    Ref tabs(Row(content.get())); Pixels(tabs.get(),"marginBottom",0);
    const char* names[]={"Positions","Notes","Messages"};
    for(size_t i=0;i<3;++i)state.tabs[i].reset(Tab(tabs.get(),names[i]));
    Divider(content.get());
    // Two columns: the entries, then the editor. The list column holds an
    // entry's name and its small buttons (three at most in a tab).
    Ref columns(api.New(api.Class(Ui,UiSpace,"VisualElement"))); Enum(columns.get(),"flexDirection",2); Add(content.get(),columns.get());
    state.library.reset(api.New(api.Class(Ui,UiSpace,"VisualElement"))); Pixels(state.library.get(),"width",320); Add(columns.get(),state.library.get());
    // The entries list has no section header: the new-entry button, the notice
    // of an empty tab, the rows and the paging sit directly in the column.
    Ref entries(api.New(api.Class(Ui,UiSpace,"VisualElement"))); Add(state.library.get(),entries.get());
    state.add.reset(Button(entries.get(),"Nouvelle entrée",IconPlus,1,0,"Commencer une nouvelle entrée dans cet onglet."));
    Pixels(state.add.get(),"marginBottom",8);
    state.empty.reset(Label(entries.get(),"",true));Enum(state.empty.get(),"display",1);
    for(size_t i=0;i<Rows;++i)EntryLine(entries.get(),i);
    // The paging arrows at both ends of their row, which a single page hides.
    state.paging.reset(Row(entries.get()));Pixels(state.paging.get(),"marginTop",4);Enum(state.paging.get(),"display",1);
    state.previous.reset(Button(state.paging.get(),"",IconPrevious,2,0,"Entrées précédentes"));
    Ref gap(api.New(api.Class(Ui,UiSpace,"VisualElement")));Float(gap.get(),"flexGrow",1);Add(state.paging.get(),gap.get());
    state.next.reset(Button(state.paging.get(),"",IconNext,2,0,"Entrées suivantes"));Pixels(state.next.get(),"marginRight",0);
    state.editorColumn.reset(api.New(api.Class(Ui,UiSpace,"VisualElement"))); Float(state.editorColumn.get(),"flexGrow",1); Pixels(state.editorColumn.get(),"minWidth",300); Add(columns.get(),state.editorColumn.get());
    Ref detail(Section(state.editorColumn.get(),"Détail",IconEdit));
    Obj editor=detail.get();
    state.title.reset(Input(editor,"Titre de l’entrée","",80));Pixels(state.title.get(),"marginBottom",8);
    state.coordinates.reset(Row(editor));
    state.x.reset(Input(state.coordinates.get(),"X","0",5));Pixels(state.x.get(),"width",64);Float(state.x.get(),"flexGrow",0);Pixels(state.x.get(),"marginRight",6);
    state.y.reset(Input(state.coordinates.get(),"Y","0",5));Pixels(state.y.get(),"width",64);Float(state.y.get(),"flexGrow",0);Pixels(state.y.get(),"marginRight",6);
    // Native dropdown of the game's worlds (replaces the numeric world field).
    state.worlds=notebook::Worlds();
    state.worldDropdown.reset(api.New(api.Class("Core.dll",Figma,"Dropdown")));
    int textType=0,small=0;api.Call(state.worldDropdown.get(),"set_dropdownType",{&textType});api.Call(state.worldDropdown.get(),"set_dropdownSize",{&small});
    *static_cast<bool*>(api.FieldAddress(state.worldDropdown.get(),"closeOnSelection"))=true;
    Text(state.worldDropdown.get(),"Monde de la position","set_tooltip");
    Pixels(state.worldDropdown.get(),"width",170);Float(state.worldDropdown.get(),"flexGrow",0);Float(state.worldDropdown.get(),"flexShrink",0);Pixels(state.worldDropdown.get(),"marginRight",8);
    for(const auto& world:state.worlds)AddWorldItem(world.id);
    Add(state.coordinates.get(),state.worldDropdown.get());
    state.current.reset(Button(state.coordinates.get(),"Ma position",IconPlayer,2,0,"Reprendre les coordonnées de la carte où tu te trouves."));Pixels(state.current.get(),"marginBottom",0);
    state.body.reset(Input(editor,"","",8000));Pixels(state.body.get(),"marginBottom",8);
    // Messages: the chat channel the entry is sent to (kept with the entry: the
    // send button of its line uses the saved one), above the save actions.
    state.channelRow.reset(Row(editor));
    state.channelButton.reset(Button(state.channelRow.get(),"Canal : actuel",IconChannel,2,0,"Canal du chat où le bouton d’envoi de sa ligne envoie ce message, une fois l’entrée enregistrée. Clique pour passer au suivant ; « actuel » garde le canal en cours du chat."));
    // The save actions end the form: an entry is used from the buttons of its line.
    Ref actions(Row(editor));Pixels(actions.get(),"marginBottom",0);
    state.save.reset(Button(actions.get(),"Enregistrer",IconSave,0,0,"Conserver cette entrée dans le carnet."));
    state.remove.reset(Button(actions.get(),"Supprimer",IconTrash,2,1,"Supprimer cette entrée (confirmation au second clic)."));
    // The status line has the whole width and no room while it is empty: the
    // file is read again by itself when it changes.
    state.footer.reset(Row(content.get()));Pixels(state.footer.get(),"marginTop",6);Pixels(state.footer.get(),"marginBottom",0);Enum(state.footer.get(),"display",1);
    state.status.reset(Label(state.footer.get(),"",true));Float(state.status.get(),"flexGrow",1);Float(state.status.get(),"flexShrink",1);
    ApplyLayout(module->settings.notebookCompact);
    state.path=notebook::DataPath(module->settingsPath);
    Fill({});
    try {Reload();}catch(...){} // An unreadable file: the status line warns below.
    IdleStatus();GiveBackDraft();
    state.nextPoll=GetTickCount64()+PollMs;
    Add(root,state.window.get());Log("NOTEBOOK_PANEL_OPEN native=true");
}
}
void InstallNotebookPanel(){
    auto inputClass=api.Class("Core.dll",Figma,"TextInput");
    api.FindVerified(inputClass,"set_isMultiline",1,0x178E150);
    api.FindVerified(api.Class(Ui,UiSpace,"TextElement"),"set_enableRichText",1,0x564B1E0);
    unsubscribe=api.FindVerified(api.Class("Core.dll","Core.Engine.Inputs.Events","InputCustomEvent"),"UnSubscribe",1,0x20FF620);
    auto tab=api.Class("Core.dll",Figma,"DofusTab");
    api.FindVerified(tab,".ctor",0,0x1712DC0);api.FindVerified(tab,"set_text",1,0x17136F0);api.FindVerified(tab,"set_size",1,0x1713550);
    api.FindVerified(tab,"set_fontCase",1,0x1712FC0);api.FindVerified(tab,"get_isOn",0,0x1712F50);api.FindVerified(tab,"SetValueWithoutNotify",1,0x1711970);
    auto button=api.Class("Core.dll",Figma,"DofusButtonCustom");
    api.FindVerified(button,".ctor",0,0x170D6F0);api.FindVerified(button,"set_icon",1,0x170DFE0);api.FindVerified(button,"set_text",1,0x170E5F0);
    api.FindVerified(button,"set_mainStyle",1,0x170E210);api.FindVerified(button,"set_size",1,0x170E330);api.FindVerified(button,"set_status",1,0x170E400);
    auto section=api.Class("Core.dll",Figma,"SectionHeader");
    api.FindVerified(section,".ctor",5,0x1745F00);api.FindVerified(section,"set_title",1,0x17467B0);api.FindVerified(section,"set_icon",1,0x1746200);
    api.FindVerified(section,"set_isOpen",1,0x17462F0);api.FindVerified(section,"set_canBeOpened",1,0x1746090);api.FindVerified(section,"AddSubContent",1,0x1744360);
    auto divider=api.Class("Core.dll",Figma,"Divider");
    api.FindVerified(divider,".ctor",0,0x170BC10);api.FindVerified(divider,"set_componentStyle",1,0x170BDC0);
    api.FindVerified(divider,"set_orientation",1,0x170C160);api.FindVerified(divider,"set_inlineText",1,0x170C0C0);
    auto listElement=api.Class("Core.dll",Figma,"ListElementBase");
    api.FindVerified(listElement,"set_icon",1,0x1733BD0);api.FindVerified(listElement,"set_isActivated",1,0x1733BF0);
    auto dropdown=api.Class("Core.dll",Figma,"Dropdown");
    api.FindVerified(dropdown,".ctor",0,0x17282C0);api.FindVerified(dropdown,"set_dropdownType",1,0x1728730);api.FindVerified(dropdown,"set_dropdownSize",1,0x1728620);
    api.FindVerified(dropdown,"AddListElementBasic",6,0x1724840);api.FindVerified(dropdown,"SelectItemIdWithoutNotify",1,0x17265B0);
    api.FindVerified(dropdown,"get_selectedElement",0,0x155DB30);api.FindVerified(dropdown,"set_choiceText",1,0x1728550);
    api.FindVerified(api.Class("Core.dll","Core.UILogic.Components","ResizableWindow"),"set_headerShowCloseButton",1,0x15BFB50);
    notebook::InstallNative();notebook::SetRowMenu({RowEntry,RowDone});
    soul::SetAuxiliaryPanelAction(Action);nativeClick::AddHandler(Action);
    // The panel's ⋯ menu lists its two layouts as the game's radio rows
    // (compact: entries and use actions only, like the game's own compact
    // spell bar); the choice is saved and applied to the open window in place.
    // Its top-bar button, the notebook's only shortcut, follows notebookButton
    // (1.16.1): read at each refresh of the bar; an open notebook stays open.
    toolsUi::Register("notebook","Carnet personnel",IconPin,[]{return wanted;},[]{if(wanted&&Current()&&!CanDiscard())return false;wanted=!wanted;return true;},
        {{{"Version complète","Grande fenêtre : la liste des entrées et le formulaire pour les créer et les modifier."},
          {"Version compacte","Fenêtre étroite : la liste des entrées et les boutons pour les utiliser, sans formulaire."}},
         []{return module&&module->settings.notebookCompact?1:0;},SelectLayout},
        []{return module&&module->settings.notebookButton;});
    installed=true;
}
void OpenNotebookPanel(){if(!installed||!Session())return;wanted=true;if(Current())api.Call(state.window.get(),"BringToFront");}
bool NotebookPanelEditing(){return installed&&wanted&&state.window.get()&&Current()&&Dirty();}
void RestoreNotebookPanel() {
    wanted=false;
    try {notebook::ReleaseRowMenus();}catch(const std::exception& error){Log(std::string("NOTEBOOK_ROW_MENU_CLEANUP_ERROR ")+error.what());}
    if(!state.window.get()) { state=State{}; return; }
    state.cleanup=true;
    try {
        Enum(state.window.get(),"display",1);
        toolsUi::DetachHeader("notebook");
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
            if(count<0 || count>16384) throw std::runtime_error("Notebook input subscriber bounds");
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
    } catch(const std::exception& error) {Log(std::string("NOTEBOOK_PANEL_CLEANUP_ERROR ")+error.what());}
}
bool NotebookPanelCleanupPending() { return state.cleanup; }
// The game left the world (inactivity disconnection, logout, character
// selection): the notebook closes like the game's own windows and stays closed
// on reconnection. Its entries are in the file; an unsaved draft is kept for
// the next opening.
void CloseNotebookPanelOffWorld(){
    if(!installed||(!wanted&&!state.window.get()))return;
    const bool shown=state.window.get()!=nullptr;
    try{
        if(shown&&Current()&&Dirty())
            offWorldDraft=Draft{state.kind,state.editing,state.captured,Read(state.title.get()),Read(state.body.get()),Read(state.x.get()),Read(state.y.get()),state.worldChoice,state.channelChoice};
    }catch(const std::exception& error){Log(std::string("NOTEBOOK_DRAFT_KEEP_ERROR ")+error.what());}
    RestoreNotebookPanel();
    if(shown&&!state.window.get())Log(std::string("NOTEBOOK_PANEL_CLOSED reason=world_left draft=")+(offWorldDraft?"1":"0"));
}
void TickNotebookPanel(){
    if(!installed)return;
    if(!wanted||!Session()||state.cleanup||(state.window.get()&&!Current())){RestoreNotebookPanel();return;}
    if(!state.window.get()){
        Ref root(Root());if(!root.get()||!api.Call(root.get(),"get_panel"))return;
        try{Create(root.get());}catch(...){Log("NOTEBOOK_PANEL_CREATE_ERROR native_panel_unavailable");RestoreNotebookPanel();}
        return;
    }
    try{PollTabs();PollWorld();}catch(const std::exception& error){Log(std::string("NOTEBOOK_TAB_ERROR ")+error.what());}
    // The pin, copy, send and travel options apply at once; a feedback leaves
    // the status line when its time is up; the file and the flags are checked
    // every half second.
    try{
        if(PinButtonsWanted()!=state.pinButtons)UpdatePins();
        UpdateLineButtons();
        if(state.statusUntil&&GetTickCount64()>=state.statusUntil)IdleStatus();
        if(GetTickCount64()>=state.nextPoll)Check();
    }catch(const std::exception& error){LogLimited(std::string("NOTEBOOK_POLL_ERROR ")+error.what());}
}
void CheckNotebookPanel(){
    if(!installed||!wanted||!Current())return;
    try{Check();}catch(const std::exception& error){LogLimited(std::string("NOTEBOOK_POLL_ERROR ")+error.what());}
}
