#include "resource-map-ui.hpp"
#include "resource-map-data.hpp"
#include "resource-map-policy.hpp"
#include "module.hpp"
#include <algorithm>
#include <cmath>
#include <memory>
#include <set>
#include <vector>

namespace resourceMap {
namespace {
using namespace managed;
constexpr const char* UiAssembly="UnityEngine.UIElementsModule.dll";
constexpr const char* UiNamespace="UnityEngine.UIElements";
constexpr const char* Figma="Core.UILogic.Components.Figma";
constexpr size_t MaxRows=128,MaxNativeChildren=64;
struct Rect {float x,y,width,height;};
using LifeFn=void(*)(Obj,Method);
LifeFn originalEnable{},originalDisable{};
Klass checkboxClass{},headerClass{},scrollClass{},labelClass{};
Method bindingFilters{},checkboxCtor{},checkboxValue{},checkboxSet{},checkboxText{},checkboxSize{},checkboxLevel{};
Method headerCtor{},headerTitle{},headerOpen{},headerCanOpen{},headerAdd{},scrollCtor{},scrollContent{};
Method scrollHorizontal{},scrollVertical{};
Method labelCtor{},labelText{},labelSingleLine{},tooltip{};
bool installed=false;
Ref pending;
struct Row {int id=0; Ref checkbox;};
struct State {
    Ref owner,root,panel,filters,scroll,content,resources,master,compositor,character,cartography;
    std::vector<Ref> nativeChildren;
    std::vector<Row> rows;
    std::vector<int> selection;
    bool enabled=true,attached=false;
    float height=-1;
};
std::unique_ptr<State> owned;
bool Session(){return installed&&module&&module->ready&&module->servicesReady&&!module->stopped&&!module->stopRequested&&module->compositor.get();}
bool CurrentOwner(Obj owner){
    return owner&&Session()&&api.Field(owner,"dtzy")==module->FindService("fng")&&
        api.Field(owner,"dtzz")==module->FindService("fkc")&&
        *static_cast<bool*>(api.FieldAddress(owner,"dtzx"));
}
void Text(Obj object,Method setter,const char* text){Ref value(api.string_new(text));api.Invoke(setter,object,{value.get()});}
Obj ReadStyle(Obj element,const char* property){
    Ref style(api.Call(element,"get_style"));
    auto value=api.Call(style.get(),(std::string("UnityEngine.UIElements.IStyle.get_")+property).c_str());
    if(!value||!api.class_is_valuetype(api.object_get_class(value)))throw std::runtime_error("Resource filters expected boxed style");
    return value;
}
void Pixels(Obj element,const char* property,float value){
    Ref box(ReadStyle(element,property));int unit=0;
    Ref length(api.New(api.Class(UiAssembly,UiNamespace,"Length"),{&value,&unit}));
    api.Call(box.get(),"set_value",{api.object_unbox(length.get())});
    Ref style(api.Call(element,"get_style"));
    api.Call(style.get(),(std::string("UnityEngine.UIElements.IStyle.set_")+property).c_str(),{api.object_unbox(box.get())});
}
std::vector<Obj> Children(Obj element){
    Ref hierarchy(api.Call(element,"get_hierarchy"));
    const auto count=api.Value<int>(api.Call(hierarchy.get(),"get_childCount"));
    if(count<0||count>static_cast<int>(MaxNativeChildren+2))throw std::runtime_error("Resource filters child bounds");
    std::vector<Obj> result;
    for(int i=0;i<count;++i)result.push_back(api.Call(hierarchy.get(),"get_Item",{&i}));
    return result;
}
Obj Parent(Obj element){return api.Call(element,"get_parent");}
void Insert(Obj parent,int index,Obj child){api.Call(parent,"Insert",{&index,child});}
bool Detach(){
    if(!owned)return true;
    auto& state=*owned;
    try {
        // Move only the original children still owned by our scroll content.
        // A native/third-party move to another owner takes precedence.
        int index=0;
        for(const auto& child:state.nativeChildren){
            const auto parent=Parent(child.get());
            if(!parent||parent==state.content.get()||parent==state.filters.get())Insert(state.filters.get(),index++,child.get());
        }
        if(state.content.get())for(auto child:Children(state.content.get())){
            if(child==state.resources.get())continue;
            // Preserve any foreign row added while the wrapper was active.
            api.Call(state.filters.get(),"Add",{child});
        }
        if(state.scroll.get()&&Parent(state.scroll.get()))api.Call(state.scroll.get(),"RemoveFromHierarchy");
        owned.reset();return true;
    }catch(const std::exception& error){Log(std::string("RESOURCE_MAP_UI_RESTORE_ERROR ")+error.what());return false;}
}
Obj Header(const char* title,bool open){
    Ref result(api.object_new(headerClass));
    bool subContent=true,folderButton=true,no=false;
    api.Invoke(headerCtor,result.get(),{&subContent,&folderButton,&no,&no,&no});
    Text(result.get(),headerTitle,title);
    bool yes=true;api.Invoke(headerCanOpen,result.get(),{&yes});api.Invoke(headerOpen,result.get(),{&open});
    return result.get();
}
Obj Checkbox(const char* text,bool selected){
    Ref result(api.object_new(checkboxClass));api.Invoke(checkboxCtor,result.get());
    int size=2;api.Invoke(checkboxSize,result.get(),{&size});
    Text(result.get(),checkboxText,text);api.Invoke(checkboxSet,result.get(),{&selected});
    return result.get();
}
void AddSub(Obj parent,Obj child){api.Invoke(headerAdd,parent,{child});}
bool Layout(State& state){
    const auto root=api.Value<Rect>(api.Call(state.root.get(),"get_worldBound"));
    const auto filters=api.Value<Rect>(api.Call(state.filters.get(),"get_worldBound"));
    const float height=root.y+root.height-filters.y-12.0f;
    if(!std::isfinite(height)||root.height<=0||height<96)return false;
    const float bounded=(std::min)(height,720.0f);
    if(state.height!=bounded){Pixels(state.scroll.get(),"height",bounded);state.height=bounded;}
    return true;
}
bool Fresh(const State& state){
    return CurrentOwner(state.owner.get())&&state.compositor.get()==module->compositor.get()&&
        state.character.get()==module->FindService("fng")&&state.cartography.get()==module->FindService("fkc")&&
        api.Field(state.owner.get(),"dzyj")==state.root.get()&&api.Call(state.root.get(),"get_panel")==state.panel.get()&&
        Parent(state.filters.get())&&(!state.attached||Parent(state.scroll.get())==state.filters.get());
}
void Attach(Obj owner){
    if(!CurrentOwner(owner))return;
    Ref root(api.Field(owner,"dzyj"));
    if(!root.get())return;
    Ref panel(api.Call(root.get(),"get_panel"));if(!panel.get())return;
    Ref filters(api.Invoke(bindingFilters,api.FieldAddress(owner,"dzyi")));
    if(!filters.get()||api.Call(filters.get(),"get_panel")!=panel.get()||!Parent(filters.get()))return;
    auto original=Children(filters.get());
    if(original.empty()||original.size()>MaxNativeChildren)throw std::runtime_error("Resource filters native rows unavailable");
    const auto catalog=Catalog();
    if(catalog.empty()||catalog.size()>MaxRows)throw std::runtime_error("Resource catalog bounds");
    std::set<int> ids;
    for(const auto& resource:catalog)if(resource.itemId<=0||!resource.name||!*resource.name||!ids.insert(resource.itemId).second)
        throw std::runtime_error("Resource catalog entry invalid");
    auto next=std::make_unique<State>();
    next->owner.reset(owner);next->root.reset(root.get());next->panel.reset(panel.get());next->filters.reset(filters.get());
    next->compositor.reset(module->compositor.get());next->character.reset(module->FindService("fng"));next->cartography.reset(module->FindService("fkc"));
    for(auto child:original)next->nativeChildren.emplace_back(child);
    next->selection=module->settings.resourceMapItems;next->enabled=module->settings.resourceMapEnabled;
    next->scroll.reset(api.object_new(scrollClass));int mode=0;api.Invoke(scrollCtor,next->scroll.get(),{&mode});
    next->content.reset(api.Invoke(scrollContent,next->scroll.get()));
    if(!next->content.get())throw std::runtime_error("Resource filter scroll content unavailable");
    int hidden=2,automatic=0;api.Invoke(scrollHorizontal,next->scroll.get(),{&hidden});api.Invoke(scrollVertical,next->scroll.get(),{&automatic});
    Pixels(next->scroll.get(),"width",280.0f);
    next->resources.reset(Header("Ressources",true));
    next->master.reset(Checkbox("Afficher les ressources",next->enabled));
    AddSub(next->resources.get(),next->master.get());
    Ref note(api.object_new(labelClass));api.Invoke(labelCtor,note.get());
    Text(note.get(),labelText,"Relevés statiques · couverture partielle\nDisponibilité en jeu inconnue");
    bool multiline=false;api.Invoke(labelSingleLine,note.get(),{&multiline});
    Text(note.get(),tooltip,"Source : Dofus Map et cartes locales dont la ressource est identifiée. Ces relevés ne donnent pas la disponibilité actuelle en jeu.");
    Pixels(note.get(),"fontSize",12.0f);Pixels(note.get(),"marginBottom",6.0f);
    AddSub(next->resources.get(),note.get());
    for(const auto& job:Jobs){
        Ref header(Header(job.name,false));bool hasRows=false;
        for(const auto& resource:catalog)if(resource.jobId==job.id){
            const bool selected=std::binary_search(next->selection.begin(),next->selection.end(),resource.itemId);
            const std::string caption=resource.mapLabel ? std::string(resource.mapLabel)+" · "+resource.name : resource.name;
            Row row;row.id=resource.itemId;row.checkbox.reset(Checkbox(caption.c_str(),selected));
            const auto level=std::string("Niv. ")+std::to_string(resource.level);
            Text(row.checkbox.get(),checkboxLevel,level.c_str());
            AddSub(header.get(),row.checkbox.get());next->rows.push_back(std::move(row));hasRows=true;
        }
        api.Invoke(headerCanOpen,header.get(),{&hasRows});AddSub(next->resources.get(),header.get());
    }
    if(next->rows.size()!=catalog.size())throw std::runtime_error("Resource catalog job unsupported");
    api.Call(next->content.get(),"Add",{next->resources.get()});
    if(!Layout(*next))return;
    // From the first reparent onward, retain all references for retryable cleanup.
    owned=std::move(next);
    for(const auto& child:owned->nativeChildren)api.Call(owned->content.get(),"Add",{child.get()});
    api.Call(filters.get(),"Add",{owned->scroll.get()});owned->attached=true;
    Log("RESOURCE_MAP_UI_READY rows="+std::to_string(owned->rows.size()));
}
void Sync(State& state){
    const auto& settings=module->settings;
    for(auto& row:state.rows){bool value=std::binary_search(settings.resourceMapItems.begin(),settings.resourceMapItems.end(),row.id);api.Invoke(checkboxSet,row.checkbox.get(),{&value});}
    bool enabled=settings.resourceMapEnabled;api.Invoke(checkboxSet,state.master.get(),{&enabled});
    state.selection=settings.resourceMapItems;state.enabled=enabled;
}
void Poll(State& state){
    // External reload/reset wins over stale visual values. Native CheckboxCustom
    // owns click/keyboard handling; no fabricated managed callback is installed.
    if(state.selection!=module->settings.resourceMapItems||state.enabled!=module->settings.resourceMapEnabled){Sync(state);return;}
    std::vector<int> selected;
    // Keep IDs unknown to this catalog when opening an older local revision.
    const auto catalog=Catalog();
    for(int id:state.selection)if(std::none_of(catalog.begin(),catalog.end(),[id](const Resource& item){return item.itemId==id;}))selected.push_back(id);
    for(const auto& row:state.rows)if(api.Value<bool>(api.Invoke(checkboxValue,row.checkbox.get())))selected.push_back(row.id);
    std::sort(selected.begin(),selected.end());
    const bool enabled=api.Value<bool>(api.Invoke(checkboxValue,state.master.get()));
    if(selected==state.selection&&enabled==state.enabled)return;
    if(!ValidSelection(selected))throw std::runtime_error("Resource selection invalid");
    auto next=module->settings;next.resourceMapEnabled=enabled;next.resourceMapItems=std::move(selected);
    try{module->Change(std::move(next));}catch(...){Sync(state);throw;}
    Sync(state);
}
void EnableHook(Obj owner,Method method){
    originalEnable(owner,method);
    try{if(CurrentOwner(owner)){if(owned&&owned->owner.get()==owner&&Fresh(*owned))return;if(!Detach())return;pending.reset(owner);}}
    catch(const std::exception& error){Log(std::string("RESOURCE_MAP_UI_ENABLE_ERROR ")+error.what());}
}
void DisableHook(Obj owner,Method method){
    try{if(pending.get()==owner)pending.reset();if(owned&&owned->owner.get()==owner)Detach();}
    catch(const std::exception& error){Log(std::string("RESOURCE_MAP_UI_DISABLE_ERROR ")+error.what());}
    originalDisable(owner,method);
}
}
void InstallUi(){
    if(installed)return;
    auto native=api.Class("Core.dll","","fhi");
    auto binding=api.Class("Core.dll","Core.UILogic.Bindings","CartographyMapUIBinding");
    checkboxClass=api.Class("Core.dll",Figma,"CheckboxCustom");headerClass=api.Class("Core.dll",Figma,"SectionHeader");
    scrollClass=api.Class(UiAssembly,UiNamespace,"ScrollView");
    labelClass=api.Class("Core.dll","Core.UILogic.Components","DofusLabel");
    bindingFilters=api.FindVerified(binding,"get_ctr_filters",0,0x2EA2480);
    checkboxCtor=api.FindVerified(checkboxClass,".ctor",0,0x1704CD0);
    checkboxValue=api.FindVerified(checkboxClass,"get_value",0,0x1705050);
    checkboxSet=api.FindVerified(checkboxClass,"SetValueWithoutNotify",1,0x17044F0);
    checkboxText=api.FindVerified(checkboxClass,"set_rightText",1,0x1705590);
    checkboxSize=api.FindVerified(checkboxClass,"set_size",1,0x17055E0);
    checkboxLevel=api.FindVerified(checkboxClass,"set_farRightText",1,0x1705060);
    headerCtor=api.FindVerified(headerClass,".ctor",5,0x1745F00);
    headerTitle=api.FindVerified(headerClass,"set_title",1,0x17467B0);
    headerOpen=api.FindVerified(headerClass,"set_isOpen",1,0x17462F0);
    headerCanOpen=api.FindVerified(headerClass,"set_canBeOpened",1,0x1746090);
    headerAdd=api.FindVerified(headerClass,"AddSubContent",1,0x1744360);
    scrollCtor=api.FindVerified(scrollClass,".ctor",1,0x5741C70);
    scrollContent=api.FindVerified(scrollClass,"get_contentContainer",0,0x5742BE0);
    scrollHorizontal=api.FindVerified(scrollClass,"set_horizontalScrollerVisibility",1,0x5743280);
    scrollVertical=api.FindVerified(scrollClass,"set_verticalScrollerVisibility",1,0x5743CB0);
    labelCtor=api.FindVerified(labelClass,".ctor",0,0x1593940);
    labelText=api.FindVerified(labelClass,"set_text",1,0x1594570);
    labelSingleLine=api.FindVerified(labelClass,"set_isSingleLine",1,0x1593FB0);
    tooltip=api.FindVerified(api.Class(UiAssembly,UiNamespace,"VisualElement"),"set_tooltip",1,0x55F9AB0);
    AddHook(api.Find(native,"OnEnable",0),0x1BAEA20,reinterpret_cast<void*>(EnableHook),reinterpret_cast<void**>(&originalEnable));
    AddHook(api.Find(native,"OnDisable",0),0x1BAE560,reinterpret_cast<void*>(DisableHook),reinterpret_cast<void**>(&originalDisable));
    installed=true;
}
void TickUi(){
    try{
        if(!Session()){pending.reset();Detach();return;}
        if(owned&&!Fresh(*owned)){if(!Detach())return;}
        if(pending.get()){
            if(!CurrentOwner(pending.get()))pending.reset();
            else if(!owned){Attach(pending.get());if(owned)pending.reset();}
        }
        if(owned){if(!Layout(*owned)){Detach();return;}Poll(*owned);}
    }catch(const std::exception& error){Log(std::string("RESOURCE_MAP_UI_ERROR ")+error.what());pending.reset();Detach();}
}
void RestoreUi(){pending.reset();Detach();}
bool UiCleanupPending(){return owned!=nullptr;}
}
