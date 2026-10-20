#include "breeding-crossing-panel.hpp"
#include "breeding-crossing-data.hpp"
#include "module.hpp"
#include "soul-panel.hpp"
#include <array>
#include <algorithm>

namespace {
using namespace managed;
constexpr const char* Ui="UnityEngine.UIElementsModule.dll";
constexpr const char* UiSpace="UnityEngine.UIElements";
constexpr const char* Figma="Core.UILogic.Components.Figma";
constexpr size_t PageSize=8;
using Life=void(*)(Obj,Method);
Life originalEnable=nullptr,originalDisable=nullptr;
Method rootGetter=nullptr;
Klass bindingClass=nullptr;
Method tabsGetter=nullptr,contentGetter=nullptr;
bool installed=false;
struct State {
    Ref owner,root,binding,tabs,content,session,cache,panel,entry,window,summary,status,previous,next,close;
    std::array<Ref,11> generations;
    std::array<Ref,PageSize> rows;
    breedingCrossing::Selection selection{};
    std::vector<size_t> filtered;
    int generation=0;
    size_t offset=0;
    bool active=false,open=false,cleanup=false,loaded=false;
    std::filesystem::path path;
} state;
bool Session(){return module&&module->ready&&module->servicesReady&&!module->stopped&&!module->stopRequested;}
void Text(Obj element,const std::string& value,const char* setter="set_text"){
    Ref text(api.string_new(value.c_str()));api.Call(element,setter,{text.get()});
}
Obj Style(Obj element,const char* name){
    Ref style(api.Call(element,"get_style"));
    auto box=api.Call(style.get(),(std::string("UnityEngine.UIElements.IStyle.get_")+name).c_str());
    if(!box||!api.class_is_valuetype(api.object_get_class(box)))throw std::runtime_error("Crossings style unavailable");
    return box;
}
void Write(Obj element,const char* name,Obj box){
    Ref style(api.Call(element,"get_style"));
    api.Call(style.get(),(std::string("UnityEngine.UIElements.IStyle.set_")+name).c_str(),{api.object_unbox(box)});
}
void Enum(Obj element,const char* name,int value){Ref box(Style(element,name));api.Call(box.get(),"set_value",{&value});Write(element,name,box.get());}
void Pixels(Obj element,const char* name,float value){
    int unit=0;Ref length(api.New(api.Class(Ui,UiSpace,"Length"),{&value,&unit}));
    Ref box(Style(element,name));api.Call(box.get(),"set_value",{api.object_unbox(length.get())});Write(element,name,box.get());
}
void Add(Obj parent,Obj child){api.Call(parent,"Add",{child});}
Obj Element(Obj parent,const char* type="VisualElement"){
    Ref value(api.New(api.Class(Ui,UiSpace,type)));if(parent)Add(parent,value.get());return value.get();
}
Obj Label(Obj parent,const std::string& text){
    Ref value(Element(parent,"Label"));Text(value.get(),text);
    Enum(value.get(),"whiteSpace",0);Pixels(value.get(),"fontSize",14);Pixels(value.get(),"marginBottom",6);
    float r=.93f,g=.91f,b=.84f,a=1;
    Ref color(api.New(api.Class("UnityEngine.CoreModule.dll","UnityEngine","Color"),{&r,&g,&b,&a}));
    Ref box(Style(value.get(),"color"));api.Call(box.get(),"set_value",{api.object_unbox(color.get())});Write(value.get(),"color",box.get());
    return value.get();
}
Obj Button(Obj parent,const std::string& text){
    Ref value(api.New(api.Class("Core.dll",Figma,"ListElementBasic")));
    int style=1;api.Call(value.get(),"set_listElementStyle",{&style});Text(value.get(),text);
    Pixels(value.get(),"minHeight",32);Pixels(value.get(),"minWidth",0);Add(parent,value.get());return value.get();
}
bool Current(){
    return Session()&&state.active&&!state.cleanup&&state.session.get()==module->compositor.get()&&
        state.cache.get()==module->cached.get()&&state.owner.get()&&
        api.Invoke(rootGetter,state.owner.get())==state.root.get()&&state.root.get()&&
        api.Call(state.root.get(),"get_panel")==state.panel.get()&&state.panel.get()&&
        (!state.tabs.get()||api.Call(state.tabs.get(),"get_panel")==state.panel.get())&&
        (!state.content.get()||api.Call(state.content.get(),"get_panel")==state.panel.get());
}
void Render(){
    state.filtered.clear();
    const auto& entries=breedingCrossing::Crossings();
    for(size_t i=0;i<entries.size();++i)if(!state.generation||entries[i].generation==state.generation)state.filtered.push_back(i);
    if(state.offset>=state.filtered.size())state.offset=0;
    const auto total=breedingCrossing::Completion(state.selection),group=breedingCrossing::Completion(state.selection,state.generation);
    Text(state.summary.get(),std::to_string(total.completed)+" / 66 croisements cochés · "+
        std::to_string(66-total.completed)+" restants\n"+
        (state.generation?"Génération "+std::to_string(state.generation)+" : ":"Toutes générations : ")+
        std::to_string(group.completed)+" / "+std::to_string(group.total));
    for(size_t i=0;i<PageSize;++i){
        bool show=state.offset+i<state.filtered.size();Enum(state.rows[i].get(),"display",show?0:1);
        if(show){
            auto index=state.filtered[state.offset+i];const auto& row=entries[index];
            Text(state.rows[i].get(),std::string(state.selection[index]?"☑ ":"☐ ")+row.name+" · G"+std::to_string(row.generation));
            Text(state.rows[i].get(),row.name+"\nGénération "+std::to_string(row.generation)+
                (row.gestationHours?" · Gestation : "+std::to_string(row.gestationHours)+" h":"")+
                "\nCliquer pour cocher ou décocher dans ton suivi manuel.","set_tooltip");
        }
    }
    for(size_t i=0;i<state.generations.size();++i)Text(state.generations[i].get(),std::string(state.generation==static_cast<int>(i)?"• ":"")+(i?std::to_string(i):"Tout"));
    bool prev=state.offset>0,next=state.offset+PageSize<state.filtered.size();
    api.Call(state.previous.get(),"SetEnabled",{&prev});api.Call(state.next.get(),"SetEnabled",{&next});
}
bool Detach(){
    state.active=false;state.cleanup=true;
    try{
        if(state.window.get())api.Call(state.window.get(),"RemoveFromHierarchy");
        if(state.entry.get())api.Call(state.entry.get(),"RemoveFromHierarchy");
        state=State{};return true;
    }catch(const std::exception& error){Log(std::string("CROSSINGS_CLEANUP_ERROR ")+error.what());return false;}
}
void Open(){
    if(!state.window.get()){
        // A native child of the breeding window: never an external app or an
        // independently draggable HUD module.
        state.window.reset(Element(state.content.get()));
        Text(state.window.get(),"mod_breeding_crossings","set_name");
        Enum(state.window.get(),"position",1);Pixels(state.window.get(),"right",0);Pixels(state.window.get(),"top",0);
        Pixels(state.window.get(),"width",450);
        for(const auto* name:{"paddingLeft","paddingRight","paddingTop","paddingBottom"})Pixels(state.window.get(),name,12);
        float r=.055f,g=.065f,b=.07f,a=1;
        Ref color(api.New(api.Class("UnityEngine.CoreModule.dll","UnityEngine","Color"),{&r,&g,&b,&a}));
        Ref box(Style(state.window.get(),"backgroundColor"));api.Call(box.get(),"set_value",{api.object_unbox(color.get())});Write(state.window.get(),"backgroundColor",box.get());
        Label(state.window.get(),"Croisements · Dragodindes");
        Label(state.window.get(),"Suivi manuel inspiré de DofusDB. Coche les croisements réalisés.");
        state.summary.reset(Label(state.window.get(),""));
        Ref filters(Element(state.window.get()));Enum(filters.get(),"flexDirection",2);Enum(filters.get(),"flexWrap",1);
        for(size_t i=0;i<state.generations.size();++i){state.generations[i].reset(Button(filters.get(),""));Pixels(state.generations[i].get(),"width",36);}
        for(auto& row:state.rows)row.reset(Button(state.window.get(),""));
        Ref pages(Element(state.window.get()));Enum(pages.get(),"flexDirection",2);
        state.previous.reset(Button(pages.get(),"Précédents"));state.next.reset(Button(pages.get(),"Suivants"));
        state.status.reset(Label(state.window.get(),"Gestation des couleurs pures au survol."));
        state.close.reset(Button(state.window.get(),"Fermer les croisements"));
    }
    state.open=true;Enum(state.window.get(),"display",0);
    state.path=breedingCrossing::StoragePath(module->settingsPath);
    try{state.selection=breedingCrossing::Read(state.path);state.loaded=true;Text(state.status.get(),"Gestation des couleurs pures au survol.");}
    catch(const std::exception& error){state.loaded=false;Text(state.status.get(),error.what());}
    Render();
}
bool Action(Obj action){
    int row=-1,generation=-1;
    for(size_t i=0;i<PageSize;++i)if(action&&action==state.rows[i].get())row=static_cast<int>(i);
    for(size_t i=0;i<state.generations.size();++i)if(action&&action==state.generations[i].get())generation=static_cast<int>(i);
    bool own=action&&(action==state.entry.get()||action==state.previous.get()||action==state.next.get()||
        action==state.close.get()||row>=0||generation>=0);
    if(!own)return false;
    try{
        if(!Current()||!api.Value<bool>(api.Call(action,"get_enabledInHierarchy")))return true;
        if(action==state.entry.get()){
            try{Open();}catch(...){Detach();throw;}
            return true;
        }
        if(!state.open)return true;
        if(action==state.close.get()){state.open=false;Enum(state.window.get(),"display",1);return true;}
        if(generation>=0){state.generation=generation;state.offset=0;}
        else if(action==state.previous.get()&&state.offset>=PageSize)state.offset-=PageSize;
        else if(action==state.next.get()&&state.offset+PageSize<state.filtered.size())state.offset+=PageSize;
        else if(row>=0&&state.loaded&&state.offset+row<state.filtered.size()){
            size_t index=state.filtered[state.offset+row];
            state.selection=breedingCrossing::Set(state.path,index,!state.selection[index]);
            Text(state.status.get(),"Croisement enregistré.");
        }
        Render();
    }catch(const std::exception& error){if(state.status.get())Text(state.status.get(),error.what());Log(std::string("CROSSINGS_ACTION_ERROR ")+error.what());}
    return true;
}
void Enable(Obj owner,Method method){
    originalEnable(owner,method);
    try{
        if(!Session())return;
        if(state.owner.get()==owner&&state.active&&!state.cleanup)return;
        if(!Detach())return;
        state.owner.reset(owner);state.root.reset(api.Invoke(rootGetter,owner));
        state.session.reset(module->compositor.get());state.cache.reset(module->cached.get());state.active=true;
    }catch(const std::exception& error){Log(std::string("CROSSINGS_ENABLE_ERROR ")+error.what());Detach();}
}
void Disable(Obj owner,Method method){
    if(state.owner.get()==owner)Detach();
    originalDisable(owner,method);
}
}
void InstallBreedingCrossing(){
    if(installed)return;
    const auto owner=api.Class("Core.dll","Core.UILogic.Breeding","BreedingUi");
    rootGetter=api.FindVerified(owner,"blqu",0,0x36BE9D0);
    bindingClass=api.Class("Core.dll","Core.UILogic.Bindings","BreedingUiBinding");
    api.FindVerified(bindingClass,".ctor",1,0x151B3E0);
    tabsGetter=api.FindVerified(bindingClass,"get_ctr_tabs",0,0x2E78AA0);
    contentGetter=api.FindVerified(bindingClass,"get_ctr_window",0,0x2E78C20);
    AddHook(api.Find(owner,"OnEnable",0),0x2DF50C0,reinterpret_cast<void*>(Enable),reinterpret_cast<void**>(&originalEnable));
    AddHook(api.Find(owner,"OnDisable",0),0x2DF4F70,reinterpret_cast<void*>(Disable),reinterpret_cast<void**>(&originalDisable));
    // Shared native ListElement, Label and style contracts installed by Soul.
    soul::SetAuxiliaryPanelAction(Action);installed=true;
}
void TickBreedingCrossing(){
    if(!installed)return;
    try{
        if(state.cleanup){Detach();return;}
        if(!state.owner.get())return;
        if(!Session()||state.session.get()!=module->compositor.get()||state.cache.get()!=module->cached.get()){Detach();return;}
        Ref panel(state.root.get()?api.Call(state.root.get(),"get_panel"):nullptr);
        if(!panel.get()){
            if(state.panel.get())Detach();
            return; // OnEnable may precede attachment to the panel.
        }
        if(state.panel.get()&&state.panel.get()!=panel.get()){Detach();return;}
        state.panel.reset(panel.get());
        if(!Current()){Detach();return;}
        if(!state.entry.get()){
            if(!state.binding.get())state.binding.reset(api.New(bindingClass,{state.root.get()}));
            state.tabs.reset(api.Invoke(tabsGetter,api.object_unbox(state.binding.get())));
            state.content.reset(api.Invoke(contentGetter,api.object_unbox(state.binding.get())));
            if(!state.tabs.get()||!state.content.get()||
                api.Call(state.tabs.get(),"get_panel")!=state.panel.get()||
                api.Call(state.content.get(),"get_panel")!=state.panel.get())return;
            state.entry.reset(Button(state.tabs.get(),"Croisements"));
            Text(state.entry.get(),"Suivi des croisements de Dragodindes","set_tooltip");
            Pixels(state.entry.get(),"width",140);Pixels(state.entry.get(),"marginLeft",8);
        }
    }catch(const std::exception& error){Log(std::string("CROSSINGS_UI_ERROR ")+error.what());Detach();}
}
void RestoreBreedingCrossing(){Detach();}
bool BreedingCrossingCleanupPending(){return state.cleanup;}
