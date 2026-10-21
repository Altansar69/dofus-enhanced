#include "notebook-panel.hpp"
#include "notebook-data.hpp"
#include "notebook-native.hpp"
#include "native-click.hpp"
#include "soul-panel.hpp"
#include "tools-ui.hpp"
#include "module.hpp"
#include <algorithm>
#include <array>
namespace {
using namespace managed;
constexpr const char* Ui="UnityEngine.UIElementsModule.dll";
constexpr const char* UiSpace="UnityEngine.UIElements";
constexpr const char* Figma="Core.UILogic.Components.Figma";
constexpr size_t Rows=6;
// FigmaIcons used by the panel: pin (window and positions), text (notes),
// chat (messages), plus, floppy, trash, zaap, copy, playerStanding, lines, edit.
constexpr int IconPin=225,IconText=305,IconChat=78,IconPlus=230,IconSave=148,IconTrash=316,IconZaap=336,IconCopy=99,IconPlayer=229,IconList=185,IconEdit=124,IconPrevious=44,IconNext=45;
struct State {
    Ref root,window,header,dragger,dependencies,cached,compositor,closeButton;
    Ref title,body,x,y,world,coordinates,status,save,remove,add,travel,prepare,copy,current,previous,next,reload,count,useRow,useDivider;
    std::array<Ref,3> tabs;
    std::array<Ref,4> inputEvents;
    std::array<Ref,Rows> rows;
    std::vector<Ref> manipulators;
    notebook::Document document;
    notebook::Entry editing;
    notebook::Entry captured;
    std::vector<notebook::Entry> visible;
    notebook::Kind kind=notebook::Kind::Position;
    size_t offset=0;
    bool cleanup=false,confirmDelete=false,confirmDiscard=false,readable=true;
    std::filesystem::path path;
} state;
bool installed=false,wanted=false;
Method unsubscribe=nullptr;
bool Session() {
    return module && module->ready && module->servicesReady && !module->stopped && !module->stopRequested;
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
    Pixels(row.get(),"minHeight",36); Pixels(row.get(),"minWidth",0); Pixels(row.get(),"marginBottom",2);
    Enum(row.get(),"display",1);
    Add(parent,row.get()); return row.get();
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
void Status(const std::string& text) {Text(state.status.get(),text);}
int KindIcon(notebook::Kind kind) { return kind==notebook::Kind::Position?IconPin:kind==notebook::Kind::Note?IconText:IconChat; }
void ShowRows() {
    state.visible.clear();
    for(const auto& entry:state.document.entries) if(entry.kind==state.kind)state.visible.push_back(entry);
    if(state.offset>=state.visible.size())state.offset=state.visible.empty()?0:((state.visible.size()-1)/Rows)*Rows;
    const int icon=KindIcon(state.kind);
    for(size_t i=0;i<Rows;++i){
        const auto index=state.offset+i;const bool show=index<state.visible.size();
        Enum(state.rows[i].get(),"display",show?0:1);
        if(show){
            const auto& entry=state.visible[index];
            Text(state.rows[i].get(),(entry.kind==notebook::Kind::Position?Coordinates(entry)+"  ":"")+entry.title);
            api.Call(state.rows[i].get(),"set_icon",{const_cast<int*>(&icon)});
            bool activated=entry.id!=0&&entry.id==state.editing.id;api.Call(state.rows[i].get(),"set_isActivated",{&activated});
        }
    }
    const auto total=state.visible.size();
    Text(state.count.get(),total==0?std::string("Aucune entrée"):total==1?std::string("1 entrée"):std::to_string(total)+" entrées");
    Enabled(state.previous.get(),state.offset>0);Enabled(state.next.get(),state.offset+Rows<state.visible.size());
}
void Fill(notebook::Entry entry) {
    entry.kind=state.kind;state.editing=std::move(entry);state.captured=state.editing;state.confirmDelete=state.confirmDiscard=false;
    Text(state.remove.get(),"Supprimer");
    Text(state.title.get(),state.editing.title);Text(state.body.get(),state.editing.body);
    Text(state.x.get(),std::to_string(state.editing.x));Text(state.y.get(),std::to_string(state.editing.y));Text(state.world.get(),std::to_string(state.editing.world));
    const bool position=state.kind==notebook::Kind::Position;
    Enum(state.coordinates.get(),"display",position?0:1);
    Enum(state.travel.get(),"display",position?0:1);Enum(state.body.get(),"display",position?1:0);
    Enum(state.prepare.get(),"display",state.kind==notebook::Kind::Message?0:1);
    bool multiline=state.kind==notebook::Kind::Note;api.Call(state.body.get(),"set_isMultiline",{&multiline});
    int max=multiline?8000:300;api.Call(state.body.get(),"set_maxLength",{&max});Pixels(state.body.get(),"height",multiline?160.f:36.f);
    Text(state.body.get(),state.kind==notebook::Kind::Note?"Écris ta note ici…":"Message à préparer dans le chat","set_placeholderText");
    Enabled(state.remove.get(),state.editing.id!=0&&state.readable);Enabled(state.save.get(),state.readable);
    for(size_t i=0;i<3;++i){bool on=static_cast<size_t>(state.kind)==i;api.Call(state.tabs[i].get(),"SetValueWithoutNotify",{&on});}
}
bool Dirty() {
    return Read(state.title.get())!=state.editing.title || Read(state.body.get())!=state.editing.body ||
        (state.kind==notebook::Kind::Position&&(Read(state.x.get())!=std::to_string(state.editing.x)||Read(state.y.get())!=std::to_string(state.editing.y)||Read(state.world.get())!=std::to_string(state.editing.world)));
}
bool CanDiscard() {
    if(!Dirty()||state.confirmDiscard){state.confirmDiscard=false;return true;}
    state.confirmDiscard=true;Status("Modifications non enregistrées. Enregistre-les ou reclique pour les abandonner.");return false;
}
notebook::Entry Editor() {
    auto entry=state.editing;entry.title=Read(state.title.get());entry.body=Read(state.body.get());entry.kind=state.kind;
    if(state.kind==notebook::Kind::Position){
        if(!notebook::Coordinate(Read(state.x.get()),entry.x)||!notebook::Coordinate(Read(state.y.get()),entry.y)||
            !notebook::Coordinate(Read(state.world.get()),entry.world)||entry.world<0)throw std::runtime_error("Renseigne des coordonnées entières et un monde valide.");
        entry.map=entry.x==state.captured.x&&entry.y==state.captured.y&&entry.world==state.captured.world?state.captured.map:0;
    }
    return entry;
}
void Reload() {
    try {state.document=notebook::Read(state.path);state.readable=true;ShowRows();}
    catch(...){state.readable=false;Enabled(state.save.get(),false);Enabled(state.remove.get(),false);throw;}
}
void SwitchTab(size_t tab) {
    state.kind=static_cast<notebook::Kind>(tab);state.offset=0;Fill({});ShowRows();
    Status(state.visible.empty()?"Aucune entrée ici. Clique sur « Nouvelle entrée » pour commencer.":"Choisis une entrée dans la liste de gauche.");
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
bool Action(Obj owner) {
    int row=-1;
    for(size_t i=0;i<Rows;++i)if(owner&&owner==state.rows[i].get())row=static_cast<int>(i);
    const bool own=owner&&(row>=0||owner==state.save.get()||owner==state.remove.get()||owner==state.add.get()||owner==state.travel.get()||
        owner==state.prepare.get()||owner==state.copy.get()||owner==state.current.get()||owner==state.closeButton.get()||owner==state.previous.get()||owner==state.next.get()||owner==state.reload.get());
    if(!own)return false;
    try {
        if(!wanted||!Current()||!api.Value<bool>(api.Call(owner,"get_enabledInHierarchy")))return true;
        if(owner!=state.remove.get()){state.confirmDelete=false;Text(state.remove.get(),"Supprimer");}
        if(owner==state.closeButton.get()){if(CanDiscard())wanted=false;return true;}
        if(row>=0&&state.offset+row<state.visible.size()){if(!CanDiscard())return true;Fill(state.visible[state.offset+row]);ShowRows();Status("Entrée sélectionnée. Clique sur Enregistrer après modification.");}
        else if(owner==state.add.get()){if(!CanDiscard())return true;Fill({});ShowRows();Status("Nouvelle entrée. Renseigne un titre puis enregistre.");}
        else if(owner==state.save.get()){auto saved=notebook::Save(state.path,Editor());Reload();Fill(saved);ShowRows();Status("Enregistré dans le carnet.");}
        else if(owner==state.remove.get()&&state.editing.id){
            if(!state.confirmDelete){state.confirmDelete=true;Text(state.remove.get(),"Confirmer la suppression");Status("Reclique sur Supprimer pour confirmer.");}
            else {notebook::Delete(state.path,state.editing.id,state.editing.version);Reload();Fill({});ShowRows();Status("Entrée supprimée.");}
        }
        else if(owner==state.previous.get()&&state.offset>=Rows){state.offset-=Rows;ShowRows();}
        else if(owner==state.next.get()&&state.offset+Rows<state.visible.size()){state.offset+=Rows;ShowRows();}
        else if(owner==state.reload.get()){if(!CanDiscard())return true;Reload();Fill({});ShowRows();Status("Carnet rechargé.");}
        else if(owner==state.current.get()){
            const auto position=notebook::CurrentPosition();Text(state.x.get(),std::to_string(position.x));Text(state.y.get(),std::to_string(position.y));Text(state.world.get(),std::to_string(position.world));
            state.captured=position;Status("Position actuelle renseignée. Clique sur Enregistrer pour la conserver.");
        }
        else if(owner==state.travel.get()){const auto entry=Editor();notebook::Travel(entry);Status("Trajet demandé vers "+Coordinates(entry)+".");}
        else if(owner==state.prepare.get()){const auto entry=Editor();notebook::PrepareMessage(state.kind==notebook::Kind::Position?Coordinates(entry):entry.body);Status("Ajouté au brouillon du chat. À toi de l’envoyer.");}
        else if(owner==state.copy.get()){const auto entry=Editor();notebook::Copy(state.kind==notebook::Kind::Position?Coordinates(entry):entry.body);Status("Copié dans le presse-papiers.");}
    }catch(const std::exception& error){Status(error.what());Log("NOTEBOOK_ACTION_ERROR operation_failed");}
    return true;
}
void Retain(const char* field) {
    Ref list(api.Field(state.window.get(),field));
    const int count=api.Value<int>(api.Call(list.get(),"get_Count"));
    if(count<0 || count>32) throw std::runtime_error("Notebook native manipulator bounds");
    for(int i=0;i<count;++i) state.manipulators.emplace_back(api.Call(list.get(),"get_Item",{&i}));
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
    bool yes=true,no=false; int icon=IconPin,size=1;
    api.Call(state.window.get(),"set_headerIcon",{&icon}); api.Call(state.window.get(),"set_headerSize",{&size});
    // The window's own close button; the game invokes no close callback of
    // its own for a window it does not manage, the panel handles the click.
    api.Call(state.window.get(),"set_headerShowCloseButton",{&yes});
    for(const auto* setter:{"set_verticalResizable","set_horizontalResizable","set_cornerResizable"})
        api.Call(state.window.get(),setter,{&no});
    api.Call(state.window.get(),"set_hasBackground",{&yes});
    Enum(state.window.get(),"position",1); Float(state.window.get(),"flexGrow",0);
    Pixels(state.window.get(),"width",660); Pixels(state.window.get(),"left",80); Pixels(state.window.get(),"top",90);
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
    Ref content(toolsUi::ScrollBody(state.window.get(),root));
    Float(content.get(),"flexGrow",0);
    for(const auto* property:{"paddingLeft","paddingRight","paddingTop","paddingBottom"}) Pixels(content.get(),property,10);
    // Native tab bar.
    Ref tabs(Row(content.get())); Pixels(tabs.get(),"marginBottom",0);
    const char* names[]={"Positions","Notes","Messages"};
    for(size_t i=0;i<3;++i)state.tabs[i].reset(Tab(tabs.get(),names[i]));
    Divider(content.get());
    // Two columns: the entries, then the editor.
    Ref columns(api.New(api.Class(Ui,UiSpace,"VisualElement"))); Enum(columns.get(),"flexDirection",2); Add(content.get(),columns.get());
    Ref library(api.New(api.Class(Ui,UiSpace,"VisualElement"))); Pixels(library.get(),"width",250); Float(library.get(),"flexShrink",0); Pixels(library.get(),"marginRight",12); Add(columns.get(),library.get());
    Ref entries(Section(library.get(),"Mes entrées",IconList));
    state.add.reset(Button(entries.get(),"Nouvelle entrée",IconPlus,1,0,"Commencer une nouvelle entrée dans cet onglet."));
    Pixels(state.add.get(),"marginBottom",8);
    for(auto& row:state.rows)row.reset(ListRow(entries.get()));
    Ref paging(Row(entries.get()));Pixels(paging.get(),"marginTop",4);
    state.previous.reset(Button(paging.get(),"",IconPrevious,2,0,"Entrées précédentes"));
    state.count.reset(Label(paging.get(),"",true));Float(state.count.get(),"flexGrow",1);Enum(state.count.get(),"unityTextAlign",4);
    state.next.reset(Button(paging.get(),"",IconNext,2,0,"Entrées suivantes"));Pixels(state.next.get(),"marginRight",0);
    Ref editorColumn(api.New(api.Class(Ui,UiSpace,"VisualElement"))); Float(editorColumn.get(),"flexGrow",1); Pixels(editorColumn.get(),"minWidth",300); Add(columns.get(),editorColumn.get());
    Ref editor(Section(editorColumn.get(),"Détail",IconEdit));
    state.title.reset(Input(editor.get(),"Titre de l’entrée","",80));Pixels(state.title.get(),"marginBottom",8);
    state.coordinates.reset(Row(editor.get()));
    state.x.reset(Input(state.coordinates.get(),"X","0",5));Pixels(state.x.get(),"width",64);Float(state.x.get(),"flexGrow",0);Pixels(state.x.get(),"marginRight",6);
    state.y.reset(Input(state.coordinates.get(),"Y","0",5));Pixels(state.y.get(),"width",64);Float(state.y.get(),"flexGrow",0);Pixels(state.y.get(),"marginRight",6);
    state.world.reset(Input(state.coordinates.get(),"Monde (1 : Amakna)","1",4));Pixels(state.world.get(),"width",72);Float(state.world.get(),"flexGrow",0);Pixels(state.world.get(),"marginRight",8);
    state.current.reset(Button(state.coordinates.get(),"Ma position",IconPlayer,2,0,"Reprendre les coordonnées de la carte où tu te trouves."));Pixels(state.current.get(),"marginBottom",0);
    state.body.reset(Input(editor.get(),"","",8000));Pixels(state.body.get(),"marginBottom",8);
    Ref actions(Row(editor.get()));
    state.save.reset(Button(actions.get(),"Enregistrer",IconSave,0,0,"Conserver cette entrée dans le carnet."));
    state.remove.reset(Button(actions.get(),"Supprimer",IconTrash,2,1,"Supprimer cette entrée (confirmation au second clic)."));
    state.useDivider.reset(Divider(editor.get(),"Utiliser"));
    state.useRow.reset(Row(editor.get()));Pixels(state.useRow.get(),"marginBottom",0);
    state.travel.reset(Button(state.useRow.get(),"Voyager",IconZaap,1,0,"Lancer le trajet vers cette position : zaaps, bateaux puis /travel."));
    state.prepare.reset(Button(state.useRow.get(),"Préparer dans le chat",IconChat,1,0,"Placer ce message dans le brouillon du chat, sans l’envoyer."));
    state.copy.reset(Button(state.useRow.get(),"Copier",IconCopy,2,0,"Copier dans le presse-papiers."));Pixels(state.copy.get(),"marginRight",0);
    Ref footer(Row(content.get()));Pixels(footer.get(),"marginTop",6);Pixels(footer.get(),"marginBottom",0);
    state.status.reset(Label(footer.get(),"Le carnet est vide. Clique sur « Nouvelle entrée » pour commencer.",true));Float(state.status.get(),"flexGrow",1);Float(state.status.get(),"flexShrink",1);Pixels(state.status.get(),"marginRight",8);
    state.reload.reset(Button(footer.get(),"Actualiser",0,2,0,"Relire le fichier du carnet."));Pixels(state.reload.get(),"marginRight",0);Pixels(state.reload.get(),"marginBottom",0);
    state.path=notebook::DataPath(module->settingsPath);
    Fill({});
    try {Reload();}catch(...){Status("Carnet illisible. Le fichier est conservé ; utilise Actualiser après correction.");}
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
    api.FindVerified(api.Class("Core.dll","Core.UILogic.Components","ResizableWindow"),"set_headerShowCloseButton",1,0x15BFB50);
    notebook::InstallNative();soul::SetAuxiliaryPanelAction(Action);nativeClick::AddHandler(Action);
    toolsUi::Register("notebook","Carnet personnel",IconPin,[]{return wanted;},[]{if(wanted&&Current()&&!CanDiscard())return false;wanted=!wanted;return true;});installed=true;
}
void OpenNotebookPanel(){if(!installed||!Session())return;wanted=true;if(Current())api.Call(state.window.get(),"BringToFront");}
void RestoreNotebookPanel() {
    wanted=false;
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
void TickNotebookPanel(){
    if(!installed)return;
    if(!wanted||!Session()||state.cleanup||(state.window.get()&&!Current())){RestoreNotebookPanel();return;}
    if(!state.window.get()){
        Ref root(Root());if(!root.get()||!api.Call(root.get(),"get_panel"))return;
        try{Create(root.get());}catch(...){Log("NOTEBOOK_PANEL_CREATE_ERROR native_panel_unavailable");RestoreNotebookPanel();}
        return;
    }
    try{PollTabs();}catch(const std::exception& error){Log(std::string("NOTEBOOK_TAB_ERROR ")+error.what());}
}
