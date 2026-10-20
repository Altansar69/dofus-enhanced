#include "notebook-panel.hpp"
#include "notebook-data.hpp"
#include "notebook-native.hpp"
#include "soul-panel.hpp"
#include "module.hpp"
#include <algorithm>
#include <array>

namespace {
using namespace managed;
constexpr const char* Ui="UnityEngine.UIElementsModule.dll";
constexpr const char* UiSpace="UnityEngine.UIElements";
constexpr const char* Figma="Core.UILogic.Components.Figma";
constexpr size_t Rows=5;
struct State {
    Ref root,window,header,dragger,dependencies,cached,compositor;
    Ref title,body,x,y,world,coordinates,status,save,remove,add,travel,prepare,copy,current,close,previous,next,reload;
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
Obj Label(Obj parent, const std::string& text) {
    Ref label(api.New(api.Class(Ui,UiSpace,"Label")));
    bool rich=false;api.Call(label.get(),"set_enableRichText",{&rich});
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
    Ref label(api.Field(button.get(),"m_textLabel"));
    bool rich=false;api.Call(label.get(),"set_enableRichText",{&rich});
    int style=1; api.Call(button.get(),"set_listElementStyle",{&style});
    Pixels(button.get(),"minHeight",30); Pixels(button.get(),"minWidth",0);
    Text(button.get(),text); Add(parent,button.get()); return button.get();
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
void ShowRows() {
    state.visible.clear();
    for(const auto& entry:state.document.entries) if(entry.kind==state.kind)state.visible.push_back(entry);
    if(state.offset>=state.visible.size())state.offset=state.visible.empty()?0:((state.visible.size()-1)/Rows)*Rows;
    for(size_t i=0;i<Rows;++i){
        const auto index=state.offset+i;const bool show=index<state.visible.size();
        Enum(state.rows[i].get(),"display",show?0:1);
        if(show){const auto& entry=state.visible[index];Text(state.rows[i].get(),(entry.kind==notebook::Kind::Position?Coordinates(entry)+"  ":"")+entry.title);}
    }
    Enabled(state.previous.get(),state.offset>0);Enabled(state.next.get(),state.offset+Rows<state.visible.size());
}
void Fill(notebook::Entry entry) {
    entry.kind=state.kind;state.editing=std::move(entry);state.captured=state.editing;state.confirmDelete=state.confirmDiscard=false;
    Text(state.remove.get(),"Supprimer");
    Text(state.title.get(),state.editing.title);Text(state.body.get(),state.editing.body);
    Text(state.x.get(),std::to_string(state.editing.x));Text(state.y.get(),std::to_string(state.editing.y));Text(state.world.get(),std::to_string(state.editing.world));
    const bool position=state.kind==notebook::Kind::Position;
    Enum(state.coordinates.get(),"display",position?0:1);Enum(state.current.get(),"display",position?0:1);
    Enum(state.travel.get(),"display",position?0:1);Enum(state.body.get(),"display",position?1:0);
    Enum(state.prepare.get(),"display",state.kind==notebook::Kind::Note?1:0);
    bool multiline=state.kind==notebook::Kind::Note;api.Call(state.body.get(),"set_isMultiline",{&multiline});
    int max=multiline?8000:300;api.Call(state.body.get(),"set_maxLength",{&max});Pixels(state.body.get(),"height",multiline?140.f:36.f);
    Enabled(state.remove.get(),state.editing.id!=0&&state.readable);Enabled(state.save.get(),state.readable);
    const char* names[]={"Positions","Notes","Messages"};
    for(size_t i=0;i<3;++i)Text(state.tabs[i].get(),std::string(static_cast<size_t>(state.kind)==i?"• ":"")+names[i]);
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
bool Action(Obj owner) {
    int tab=-1,row=-1;
    for(int i=0;i<3;++i)if(owner&&owner==state.tabs[i].get())tab=i;
    for(size_t i=0;i<Rows;++i)if(owner&&owner==state.rows[i].get())row=static_cast<int>(i);
    const bool own=owner&&(tab>=0||row>=0||owner==state.save.get()||owner==state.remove.get()||owner==state.add.get()||owner==state.travel.get()||
        owner==state.prepare.get()||owner==state.copy.get()||owner==state.current.get()||owner==state.close.get()||owner==state.previous.get()||owner==state.next.get()||owner==state.reload.get());
    if(!own)return false;
    try {
        if(!wanted||!Current()||!api.Value<bool>(api.Call(owner,"get_enabledInHierarchy")))return true;
        if(owner!=state.remove.get()){state.confirmDelete=false;Text(state.remove.get(),"Supprimer");}
        if(owner==state.close.get()){if(CanDiscard())wanted=false;return true;}
        if(tab>=0){if(!CanDiscard())return true;state.kind=static_cast<notebook::Kind>(tab);state.offset=0;Fill({});ShowRows();Status("Sélectionne une entrée ou crée-en une nouvelle.");}
        else if(row>=0&&state.offset+row<state.visible.size()){if(!CanDiscard())return true;Fill(state.visible[state.offset+row]);Status("Entrée sélectionnée. Clique sur Enregistrer après modification.");}
        else if(owner==state.add.get()){if(!CanDiscard())return true;Fill({});Status("Nouvelle entrée. Renseigne un titre puis enregistre.");}
        else if(owner==state.save.get()){auto saved=notebook::Save(state.path,Editor());Reload();Fill(saved);ShowRows();Status("Enregistré dans le carnet.");}
        else if(owner==state.remove.get()&&state.editing.id){
            if(!state.confirmDelete){state.confirmDelete=true;Text(state.remove.get(),"Confirmer la suppression");Status("Reclique sur Supprimer pour confirmer.");}
            else {notebook::Delete(state.path,state.editing.id,state.editing.version);Reload();Fill({});ShowRows();Status("Entrée supprimée.");}
        }
        else if(owner==state.previous.get()&&state.offset>=Rows){state.offset-=Rows;ShowRows();}
        else if(owner==state.next.get()&&state.offset+Rows<state.visible.size()){state.offset+=Rows;ShowRows();}
        else if(owner==state.reload.get()){if(!CanDiscard())return true;Reload();Fill({});Status("Carnet rechargé.");}
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
    bool yes=true,no=false; int icon=225,size=1;
    api.Call(state.window.get(),"set_headerIcon",{&icon}); api.Call(state.window.get(),"set_headerSize",{&size});
    api.Call(state.window.get(),"set_headerShowCloseButton",{&no});
    for(const auto* setter:{"set_verticalResizable","set_horizontalResizable","set_cornerResizable"})
        api.Call(state.window.get(),setter,{&no});
    api.Call(state.window.get(),"set_hasBackground",{&yes});
    Enum(state.window.get(),"position",1); Float(state.window.get(),"flexGrow",0);
    Pixels(state.window.get(),"width",500); Pixels(state.window.get(),"left",80); Pixels(state.window.get(),"top",90);
    state.header.reset(api.Call(state.window.get(),"get_headerWidget"));
    Ref draggers(api.Field(state.window.get(),"m_registeredElementDraggers"));
    state.dragger.reset(api.Call(draggers.get(),"get_Item",{state.header.get()}));
    Retain("m_elementDraggers"); Retain("m_resizerList");
    const char* getters[]={"ydf","ydh","ydj","ydl"};
    for(size_t i=0;i<4;++i) state.inputEvents[i].reset(api.Call(input.get(),getters[i]));
    api.Call(state.window.get(),"Init",{state.dependencies.get()});
    Ref content(api.Call(state.window.get(),"get_contentContainer"));
    Float(content.get(),"flexGrow",0);
    for(const auto* property:{"paddingLeft","paddingRight","paddingTop","paddingBottom"}) Pixels(content.get(),property,8);

    Ref tabs(Row(content.get()));
    const char* names[]={"Positions","Notes","Messages"};
    for(size_t i=0;i<3;++i){state.tabs[i].reset(Button(tabs.get(),names[i]));Float(state.tabs[i].get(),"flexGrow",1);}
    for(auto& row:state.rows){row.reset(Button(content.get(),""));Enum(row.get(),"display",1);}
    Ref paging(Row(content.get()));state.previous.reset(Button(paging.get(),"Précédentes"));state.next.reset(Button(paging.get(),"Suivantes"));state.add.reset(Button(paging.get(),"Nouveau"));state.reload.reset(Button(paging.get(),"Recharger"));
    Label(content.get(),"Titre");state.title.reset(Input(content.get(),"Nom de l’entrée","",80));
    state.coordinates.reset(Row(content.get()));
    Label(state.coordinates.get(),"X");state.x.reset(Input(state.coordinates.get(),"X","0",5));
    Label(state.coordinates.get(),"Y");state.y.reset(Input(state.coordinates.get(),"Y","0",5));
    Label(state.coordinates.get(),"Monde");state.world.reset(Input(state.coordinates.get(),"Monde (1 : Amakna)","1",4));
    state.current.reset(Button(content.get(),"Utiliser ma position actuelle"));
    state.body.reset(Input(content.get(),"Texte de la note ou du message","",8000));
    Ref edit(Row(content.get()));state.save.reset(Button(edit.get(),"Enregistrer"));state.remove.reset(Button(edit.get(),"Supprimer"));
    Ref actions(Row(content.get()));state.travel.reset(Button(actions.get(),"Voyager"));state.prepare.reset(Button(actions.get(),"Préparer dans le chat"));state.copy.reset(Button(actions.get(),"Copier"));
    state.status.reset(Label(content.get(),"Sélectionne une entrée ou crée-en une nouvelle."));
    state.close.reset(Button(content.get(),"Fermer"));
    state.path=notebook::DataPath(module->settingsPath);
    Fill({});
    try {Reload();}catch(...){Status("Carnet illisible. Le fichier est conservé ; utilise Recharger après correction.");}
    Add(root,state.window.get());Log("NOTEBOOK_PANEL_OPEN native=true");
}
}
void InstallNotebookPanel(){
    auto inputClass=api.Class("Core.dll",Figma,"TextInput");
    api.FindVerified(inputClass,"set_isMultiline",1,0x178E150);
    api.FindVerified(api.Class(Ui,UiSpace,"TextElement"),"set_enableRichText",1,0x564B1E0);
    unsubscribe=api.FindVerified(api.Class("Core.dll","Core.Engine.Inputs.Events","InputCustomEvent"),"UnSubscribe",1,0x20FF620);
    notebook::InstallNative();soul::SetAuxiliaryPanelAction(Action);installed=true;
}
void OpenNotebookPanel(){if(!installed||!Session())return;wanted=true;if(Current())api.Call(state.window.get(),"BringToFront");}
void RestoreNotebookPanel() {
    wanted=false;
    if(!state.window.get()) { state=State{}; return; }
    state.cleanup=true;
    try {
        Enum(state.window.get(),"display",1);
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
    }
}
