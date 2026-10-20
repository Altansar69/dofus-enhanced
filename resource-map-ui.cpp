#include "resource-map-ui.hpp"
#include "resource-map-data.hpp"
#include "resource-map-policy.hpp"
#include "resource-map-icons.hpp"
#include "resource-map-render.hpp"
#include "module.hpp"
#include <algorithm>
#include <array>
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
constexpr size_t MaxRows=128;
struct Rect {float x,y,width,height;};
struct Color {float r,g,b,a;};
using LifeFn=void(*)(Obj,Method);
LifeFn originalEnable{},originalDisable{};
Klass checkboxClass{},headerClass{},scrollClass{},labelClass{};
Method checkboxCtor{},checkboxValue{},checkboxSet{},checkboxText{},checkboxSize{},checkboxLevel{},checkboxIcon{};
Method headerCtor{},headerTitle{},headerOpen{},headerCanOpen{},headerAdd{},scrollCtor{},scrollContent{};
Method scrollHorizontal{},scrollVertical{};
Method labelCtor{},labelText{},labelSingleLine{},tooltip{},bringToFront{};
bool installed=false;
Ref pending;
struct Row {int id=0; Ref checkbox;};
struct State {
    Ref owner,root,panel,scroll,content,resources,master,compositor,character,cartography;
    std::vector<Row> rows;
    std::vector<int> selection;
    bool enabled=true,attached=false;
    float height=-1,width=-1,left=-1,top=-1;
};
std::unique_ptr<State> owned,miniOwned;
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
template<class T> void Style(Obj element,const char* property,T value){
    Ref box(ReadStyle(element,property));api.Call(box.get(),"set_value",{&value});
    Ref style(api.Call(element,"get_style"));
    api.Call(style.get(),(std::string("UnityEngine.UIElements.IStyle.set_")+property).c_str(),{api.object_unbox(box.get())});
}
Obj Parent(Obj element){return api.Call(element,"get_parent");}
bool Detach(std::unique_ptr<State>& target){
    if(!target)return true;
    auto& state=*target;
    try {
        if(state.resources.get()&&Parent(state.resources.get()))api.Call(state.resources.get(),"RemoveFromHierarchy");
        target.reset();return true;
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
    Style(api.Field(result.get(),"m_rightLabel"),"color",Color{.94f,.94f,.90f,1});
    return result.get();
}
void AddSub(Obj parent,Obj child){api.Invoke(headerAdd,parent,{child});}
bool Layout(State& state,const MiniMapSurface* mini=nullptr){
    const auto root=api.Value<Rect>(api.Call(state.root.get(),"get_worldBound"));
    const float height=mini?mini->height-112.0f:root.height-192.0f;
    const float width=mini?(std::min)(260.0f,mini->width-16.0f):(std::min)(300.0f,root.width-112.0f);
    if(!std::isfinite(height)||!std::isfinite(width)||height<(mini?48:96)||width<(mini?128:200))return false;
    const float bounded=(std::min)(height,520.0f);
    if(state.height!=bounded){Pixels(state.scroll.get(),"height",bounded);state.height=bounded;}
    if(state.width!=width){Pixels(state.resources.get(),"width",width);state.width=width;}
    if(mini){
        const float left=mini->left+mini->width-width-8,top=mini->top+40;
        if(state.left!=left){Pixels(state.resources.get(),"left",left);state.left=left;}
        if(state.top!=top){Pixels(state.resources.get(),"top",top);state.top=top;}
    }
    return true;
}
bool Fresh(const State& state,const MiniMapSurface* mini=nullptr){
    const bool ownerCurrent=mini?mini->root.get()==state.root.get():
        CurrentOwner(state.owner.get())&&api.Field(state.owner.get(),"dzyj")==state.root.get();
    return ownerCurrent&&Session()&&state.compositor.get()==module->compositor.get()&&
        state.character.get()==module->FindService("fng")&&state.cartography.get()==module->FindService("fkc")&&
        api.Call(state.root.get(),"get_panel")==state.panel.get()&&
        (!state.attached||Parent(state.resources.get())==state.root.get());
}
void Attach(std::unique_ptr<State>& target,Obj owner,const MiniMapSurface* mini=nullptr){
    if(!mini&&!CurrentOwner(owner))return;
    Ref root(mini?mini->root.get():api.Field(owner,"dzyj"));
    if(!root.get())return;
    Ref panel(api.Call(root.get(),"get_panel"));if(!panel.get())return;
    const auto bounds=api.Value<Rect>(api.Call(root.get(),"get_worldBound"));
    if(!std::isfinite(bounds.width)||!std::isfinite(bounds.height))return;
    if(mini){if(mini->width<144||mini->height<160)return;}
    else if(bounds.width<312||bounds.height<288)return;
    const auto catalog=Catalog();
    if(catalog.empty()||catalog.size()>MaxRows)throw std::runtime_error("Resource catalog bounds");
    std::set<int> ids;
    for(const auto& resource:catalog)if(resource.itemId<=0||!resource.name||!*resource.name||!ids.insert(resource.itemId).second)
        throw std::runtime_error("Resource catalog entry invalid");
    auto next=std::make_unique<State>();
    next->owner.reset(owner);next->root.reset(root.get());next->panel.reset(panel.get());
    next->compositor.reset(module->compositor.get());next->character.reset(module->FindService("fng"));next->cartography.reset(module->FindService("fkc"));
    next->selection=module->settings.resourceMapItems;next->enabled=module->settings.resourceMapEnabled;
    next->scroll.reset(api.object_new(scrollClass));int mode=0;api.Invoke(scrollCtor,next->scroll.get(),{&mode});
    next->content.reset(api.Invoke(scrollContent,next->scroll.get()));
    if(!next->content.get())throw std::runtime_error("Resource filter scroll content unavailable");
    int hidden=2,automatic=0;api.Invoke(scrollHorizontal,next->scroll.get(),{&hidden});api.Invoke(scrollVertical,next->scroll.get(),{&automatic});
    next->resources.reset(Header("Ressources",false));
    Style(next->resources.get(),"position",1);
    if(!mini){Pixels(next->resources.get(),"right",64.0f);Pixels(next->resources.get(),"top",80.0f);}
    Text(next->resources.get(),tooltip,"Choisir les ressources affichées sur la carte et la mini-carte.");
    Style(next->resources.get(),"backgroundColor",Color{.105f,.115f,.12f,.97f});
    for(auto corner:{"borderTopLeftRadius","borderTopRightRadius","borderBottomLeftRadius","borderBottomRightRadius"})
        Pixels(next->resources.get(),corner,8.0f);
    AddSub(next->resources.get(),next->scroll.get());
    auto add=[&](Obj child){api.Call(next->content.get(),"Add",{child});};
    next->master.reset(Checkbox("Afficher les ressources",next->enabled));
    add(next->master.get());
    Ref note(api.object_new(labelClass));api.Invoke(labelCtor,note.get());
    Text(note.get(),labelText,"DofusDB · Points de récolte recensés");
    bool multiline=false;api.Invoke(labelSingleLine,note.get(),{&multiline});
    Text(note.get(),tooltip,"Source : DofusDB. Quantités recensées par carte, intérieurs inclus. La présence actuelle après récolte n’est pas connue.");
    Pixels(note.get(),"fontSize",12.0f);Pixels(note.get(),"marginBottom",6.0f);
    Style(note.get(),"color",Color{.75f,.79f,.81f,1});add(note.get());
    for(const auto& job:Jobs){
        Ref header(Header(job.name,false));bool hasRows=false;
        for(const auto& resource:catalog)if(resource.jobId==job.id){
            const bool selected=std::binary_search(next->selection.begin(),next->selection.end(),resource.itemId);
            Row row;row.id=resource.itemId;row.checkbox.reset(Checkbox(resource.name,selected));
            Ref address(ItemIconAddress(resource.iconId));api.Invoke(checkboxIcon,row.checkbox.get(),{api.object_unbox(address.get())});
            const auto level=std::string("Niv. ")+std::to_string(resource.level);
            Text(row.checkbox.get(),checkboxLevel,level.c_str());
            Style(api.Field(row.checkbox.get(),"m_farRightLabel"),"color",Color{.94f,.82f,.43f,1});
            Text(row.checkbox.get(),tooltip,(std::string(resource.name)+" · "+level).c_str());
            AddSub(header.get(),row.checkbox.get());next->rows.push_back(std::move(row));hasRows=true;
        }
        api.Invoke(headerCanOpen,header.get(),{&hasRows});add(header.get());
    }
    if(next->rows.size()!=catalog.size())throw std::runtime_error("Resource catalog job unsupported");
    if(!Layout(*next,mini))return;
    // The transport filters and their original parent/order are never modified.
    target=std::move(next);
    api.Call(root.get(),"Add",{target->resources.get()});target->attached=true;
    Log("RESOURCE_MAP_UI_READY view="+std::string(mini?"mini":"full")+" rows="+std::to_string(target->rows.size()));
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
    try{if(CurrentOwner(owner)){if(owned&&owned->owner.get()==owner&&Fresh(*owned))return;if(!Detach(owned))return;pending.reset(owner);}}
    catch(const std::exception& error){Log(std::string("RESOURCE_MAP_UI_ENABLE_ERROR ")+error.what());}
}
void DisableHook(Obj owner,Method method){
    try{if(pending.get()==owner)pending.reset();if(owned&&owned->owner.get()==owner)Detach(owned);}
    catch(const std::exception& error){Log(std::string("RESOURCE_MAP_UI_DISABLE_ERROR ")+error.what());}
    originalDisable(owner,method);
}
}
void InstallUi(){
    if(installed)return;
    auto native=api.Class("Core.dll","","fhi");
    checkboxClass=api.Class("Core.dll",Figma,"CheckboxCustom");headerClass=api.Class("Core.dll",Figma,"SectionHeader");
    scrollClass=api.Class(UiAssembly,UiNamespace,"ScrollView");
    labelClass=api.Class("Core.dll","Core.UILogic.Components","DofusLabel");
    InstallIcons();
    checkboxCtor=api.FindVerified(checkboxClass,".ctor",0,0x1704CD0);
    checkboxValue=api.FindVerified(checkboxClass,"get_value",0,0x1705050);
    checkboxSet=api.FindVerified(checkboxClass,"SetValueWithoutNotify",1,0x17044F0);
    checkboxText=api.FindVerified(checkboxClass,"set_rightText",1,0x1705590);
    checkboxSize=api.FindVerified(checkboxClass,"set_size",1,0x17055E0);
    checkboxLevel=api.FindVerified(checkboxClass,"set_farRightText",1,0x1705060);
    checkboxIcon=api.FindVerified(checkboxClass,"set_leftIconAddress",1,0x1705510);
    for(const auto& [name,offset]:std::array<std::pair<const char*,size_t>,2>{{{"m_rightLabel",0x318},{"m_farRightLabel",0x320}}}){
        auto field=api.class_get_field_from_name(checkboxClass,name);
        if(!field||api.field_get_offset(field)!=offset)throw std::runtime_error("Resource checkbox label layout mismatch");
    }
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
    bringToFront=api.FindVerified(api.Class(UiAssembly,UiNamespace,"VisualElement"),"BringToFront",0,0x55E08E0);
    AddHook(api.Find(native,"OnEnable",0),0x1BAEA20,reinterpret_cast<void*>(EnableHook),reinterpret_cast<void**>(&originalEnable));
    AddHook(api.Find(native,"OnDisable",0),0x1BAE560,reinterpret_cast<void*>(DisableHook),reinterpret_cast<void**>(&originalDisable));
    installed=true;
}
void TickUi(){
    try{
        if(!Session()){RestoreUi();return;}
        if(owned&&!Fresh(*owned)){if(!Detach(owned))return;}
        if(pending.get()){
            if(!CurrentOwner(pending.get()))pending.reset();
            else if(!owned){Attach(owned,pending.get());if(owned)pending.reset();}
        }
        if(owned){if(!Layout(*owned))Detach(owned);else Poll(*owned);}
    }catch(const std::exception& error){Log(std::string("RESOURCE_MAP_UI_ERROR ")+error.what());pending.reset();Detach(owned);}
    // Discover the mini-map even when the full map was never opened. Its
    // controls must remain reachable with rendering disabled or no selection.
    try{
        if(!Session()){Detach(miniOwned);return;}
        auto surface=ReadMiniMapSurface();
        if(miniOwned&&!Fresh(*miniOwned,&surface)){if(!Detach(miniOwned))return;}
        if(!miniOwned&&surface.root.get())Attach(miniOwned,nullptr,&surface);
        if(miniOwned){
            if(!Layout(*miniOwned,&surface)){Detach(miniOwned);return;}
            Poll(*miniOwned);
            // Resource marker layers can be recreated after native filter
            // refreshes. Keep the drawer above those non-interactive markers.
            api.Invoke(bringToFront,miniOwned->resources.get());
        }
    }catch(const std::exception& error){Log(std::string("RESOURCE_MINIMAP_UI_ERROR ")+error.what());Detach(miniOwned);}
}
void RestoreUi(){pending.reset();Detach(owned);Detach(miniOwned);}
bool UiCleanupPending(){return owned!=nullptr||miniOwned!=nullptr;}
}
