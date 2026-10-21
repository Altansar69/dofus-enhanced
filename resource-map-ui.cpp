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
Klass cursorClass{},checkboxClass{},headerClass{},scrollClass{},labelClass{},elementClass{};
Method checkboxCtor{},checkboxValue{},checkboxSet{},checkboxText{},checkboxSize{},checkboxLevel{},checkboxIcon{};
Method headerCtor{},headerTitle{},headerOpen{},headerCanOpen{},headerAdd{},scrollCtor{},scrollContent{};
Method scrollHorizontal{},scrollVertical{};
Method labelCtor{},labelText{},labelSingleLine{},tooltip{},bringToFront{},enableCursor{},removeCursor{};
bool installed=false;
Ref pending;
struct Row {int id=0; Ref checkbox;};
struct State {
    Ref owner,root,mapRoot,panel,scroll,content,resources,master,compositor,character,cartography;
    std::vector<Row> rows;
    std::vector<int> selection;
    Ref toolbar,center;
    std::vector<Ref> tabs,groups,cursorTargets;
    int activeJob=-1;
    bool enabled=true,attached=false,mini=false,cleaning=false;
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
    state.cleaning=true;
    try {
        while(!state.cursorTargets.empty()){
            const auto control=state.cursorTargets.back().get();
            api.Invoke(removeCursor,nullptr,{control});
            // Native RemoveCursorHandler unregisters events but retains strong
            // keys in these caches. Release only this owned control's entries.
            for(const auto* name:{"s_mouseEnterCursorMap","s_mouseLeaveCursorMap"}){
                Ref map(api.StaticField(cursorClass,name));
                if(!map.get())throw std::runtime_error("Resource cursor cache unavailable");
                auto type=api.object_get_class(map.get());
                if(std::string(api.class_get_name(type))!="Dictionary`2")throw std::runtime_error("Resource cursor cache type mismatch");
                auto remove=api.FindVerified(type,"Remove",1,0x36F0D60);
                api.Invoke(remove,map.get(),{control});
            }
            state.cursorTargets.pop_back();
        }
        if(state.resources.get()&&Parent(state.resources.get()))api.Call(state.resources.get(),"RemoveFromHierarchy");
        target.reset();return true;
    }catch(const std::exception& error){Log(std::string("RESOURCE_MAP_UI_RESTORE_ERROR ")+error.what());return false;}
}
Obj Header(const char* title,bool open,bool icon=false){
    Ref result(api.object_new(headerClass));
    bool subContent=true,folderButton=true,no=false;
    api.Invoke(headerCtor,result.get(),{&subContent,&folderButton,&no,&icon,&no});
    Text(result.get(),headerTitle,title);
    bool yes=true;api.Invoke(headerCanOpen,result.get(),{&yes});api.Invoke(headerOpen,result.get(),{&open});
    return result.get();
}
Obj Checkbox(const char* text,bool selected){
    Ref result(api.object_new(checkboxClass));api.Invoke(checkboxCtor,result.get());
    int size=2;api.Invoke(checkboxSize,result.get(),{&size});
    Text(result.get(),checkboxText,text);api.Invoke(checkboxSet,result.get(),{&selected});
    // Native UpdateContent omits the label for an empty caption (icon tabs).
    if(auto label=api.Field(result.get(),"m_rightLabel"))
        Style(label,"color",Color{.94f,.94f,.90f,1});
    return result.get();
}
void AddSub(Obj parent,Obj child){api.Invoke(headerAdd,parent,{child});}
Obj Element(){return api.New(elementClass);}
void Rounded(Obj element,float radius){
    for(auto corner:{"borderTopLeftRadius","borderTopRightRadius","borderBottomLeftRadius","borderBottomRightRadius"})Pixels(element,corner,radius);
}
void Ignore(Obj element){int ignore=1;api.Call(element,"set_pickingMode",{&ignore});}
void HideCheckboxSquare(Obj control){
    Ref square(api.Field(control,"m_checkbox"));
    // Native selection refreshes display; visibility keeps the square hidden.
    Style(square.get(),"visibility",1);Style(square.get(),"position",1);
    Style(square.get(),"display",1);Ignore(square.get());
}
void ShowJob(State& state,int selected){
    state.activeJob=selected;
    for(size_t i=0;i<state.tabs.size();++i){
        bool active=int(i)==selected;
        api.Invoke(checkboxSet,state.tabs[i].get(),{&active});
        Style(state.tabs[i].get(),"backgroundColor",active?Color{.38f,.32f,.12f,1}:Color{.10f,.12f,.13f,1});
        for(auto side:{"borderTopColor","borderBottomColor","borderLeftColor","borderRightColor"})
            Style(state.tabs[i].get(),side,active?Color{1,.82f,.32f,1}:Color{.49f,.51f,.48f,1});
        Style(state.groups[i].get(),"display",active?0:1);
    }
    Style(state.scroll.get(),"display",selected<0?1:0);
}
void PollTabs(State& state){
    if(state.tabs.empty())return;
    // Switching categories is UI state only; it never alters selected resources.
    for(size_t i=0;i<state.tabs.size();++i)
        if(int(i)!=state.activeJob&&api.Value<bool>(api.Invoke(checkboxValue,state.tabs[i].get()))){ShowJob(state,int(i));return;}
    if(state.activeJob>=0&&!api.Value<bool>(api.Invoke(checkboxValue,state.tabs[state.activeJob].get())))ShowJob(state,-1);
}
bool Layout(State& state,const MiniMapSurface* mini=nullptr){
    const auto root=api.Value<Rect>(api.Call(state.root.get(),"get_worldBound"));
    const float width=mini?(std::min)(320.0f,mini->width-16.0f):(std::min)(320.0f,root.width-112.0f);
    if(!std::isfinite(width)||width<(mini?128:200))return false;
    const int columns=(std::max)(1,int(width/52));
    const float toolbarHeight=52.0f*float((std::size(Jobs)+columns-1)/columns);
    const float height=mini?mini->height-48.0f-toolbarHeight:root.height-192.0f;
    if(!std::isfinite(height)||height<(mini?48:96))return false;
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
    const bool ownerCurrent=mini?(mini->root.get()==state.root.get()&&mini->mapRoot.get()==state.mapRoot.get()&&
        mini->mapRoot.get()&&Parent(mini->mapRoot.get())==state.root.get()):
        CurrentOwner(state.owner.get())&&api.Field(state.owner.get(),"dzyj")==state.root.get();
    return !state.cleaning&&ownerCurrent&&Session()&&state.compositor.get()==module->compositor.get()&&
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
    if(mini)next->mapRoot.reset(mini->mapRoot.get());
    next->compositor.reset(module->compositor.get());next->character.reset(module->FindService("fng"));next->cartography.reset(module->FindService("fkc"));
    next->mini=mini!=nullptr;
    next->selection=module->settings.resourceMapItems;
    next->enabled=next->mini?module->settings.resourceMiniMapEnabled:module->settings.resourceMapEnabled;
    next->scroll.reset(api.object_new(scrollClass));int mode=0;api.Invoke(scrollCtor,next->scroll.get(),{&mode});
    next->content.reset(api.Invoke(scrollContent,next->scroll.get()));
    if(!next->content.get())throw std::runtime_error("Resource filter scroll content unavailable");
    int hidden=2,automatic=0;api.Invoke(scrollHorizontal,next->scroll.get(),{&hidden});api.Invoke(scrollVertical,next->scroll.get(),{&automatic});
    next->resources.reset(mini?Element():Header("Ressources",false));
    Style(next->resources.get(),"position",1);
    if(!mini){Pixels(next->resources.get(),"right",64.0f);Pixels(next->resources.get(),"top",80.0f);}
    Text(next->resources.get(),tooltip,"Choisir les ressources affichées sur la carte et la mini-carte.");
    if(mini){
        next->toolbar.reset(Element());
        Style(next->toolbar.get(),"flexDirection",2);Style(next->toolbar.get(),"flexWrap",1);
        api.Call(next->resources.get(),"Add",{next->toolbar.get()});
        api.Call(next->resources.get(),"Add",{next->scroll.get()});
        Style(next->scroll.get(),"backgroundColor",Color{.105f,.115f,.12f,1});
        Rounded(next->scroll.get(),8);
        next->center.reset(Checkbox("Centrer",false));
        HideCheckboxSquare(next->center.get());
        Style(next->center.get(),"position",1);
        Pixels(next->center.get(),"left",0);Pixels(next->center.get(),"top",-34);
        Pixels(next->center.get(),"width",96);Pixels(next->center.get(),"height",30);
        Pixels(next->center.get(),"minHeight",30);Pixels(next->center.get(),"maxHeight",30);
        Style(next->center.get(),"backgroundColor",Color{.10f,.12f,.13f,.96f});
        Rounded(next->center.get(),8);
        Text(next->center.get(),tooltip,"Recentrer la mini-carte sur mon personnage");
        api.Call(next->resources.get(),"Add",{next->center.get()});
    }else{
        Style(next->resources.get(),"backgroundColor",Color{.105f,.115f,.12f,.97f});
        Rounded(next->resources.get(),8);
        AddSub(next->resources.get(),next->scroll.get());
    }
    auto add=[&](Obj child){api.Call(next->content.get(),"Add",{child});};
    next->master.reset(Checkbox(mini?"Afficher sur la mini-carte":"Afficher sur la grande carte",next->enabled));
    add(next->master.get());
    Ref note(api.object_new(labelClass));api.Invoke(labelCtor,note.get());
    Text(note.get(),labelText,"DofusDB · Points de récolte recensés");
    bool multiline=false;api.Invoke(labelSingleLine,note.get(),{&multiline});
    Text(note.get(),tooltip,"Source : DofusDB. Quantités recensées par carte, intérieurs inclus. La présence actuelle après récolte n’est pas connue.");
    Pixels(note.get(),"fontSize",12.0f);Pixels(note.get(),"marginBottom",6.0f);
    Style(note.get(),"color",Color{.75f,.79f,.81f,1});add(note.get());
    for(const auto& job:Jobs){
        Ref header(Header(job.name,mini!=nullptr));bool hasRows=false;
        Ref title(api.Call(header.get(),"get_titleLabel"));
        Ref titleRow(Parent(title.get()));
        if(!titleRow.get())throw std::runtime_error("Resource job title parent unavailable");
        // Native SectionHeader refreshes hide its empty icon frame. Keep the
        // resource image beside the title, outside that native-owned frame.
        Ref jobImage(Element());Ignore(jobImage.get());
        Pixels(jobImage.get(),"width",28);Pixels(jobImage.get(),"height",28);
        Pixels(jobImage.get(),"minWidth",28);Pixels(jobImage.get(),"marginRight",8);
        Style(jobImage.get(),"flexShrink",0.0f);Style(jobImage.get(),"alignSelf",2);
        LoadJobIcon(jobImage.get(),job.id);
        int first=0;api.Call(titleRow.get(),"Insert",{&first,jobImage.get()});
        if(mini){
            Ref tab(Checkbox("",false));
            // Preserve CheckboxCustom's own root clickable/keyboard behavior.
            // Only the checkbox square and empty caption of our button are hidden.
            HideCheckboxSquare(tab.get());
            if(auto label=api.Field(tab.get(),"m_rightLabel"))Style(label,"display",1);
            Pixels(tab.get(),"width",48);Pixels(tab.get(),"height",48);
            Pixels(tab.get(),"minWidth",48);Style(tab.get(),"flexShrink",0.0f);
            Pixels(tab.get(),"marginRight",4);Pixels(tab.get(),"marginBottom",4);
            for(auto side:{"paddingLeft","paddingRight","paddingTop","paddingBottom"})Pixels(tab.get(),side,0);
            for(auto side:{"borderTopWidth","borderBottomWidth","borderLeftWidth","borderRightWidth"})Style(tab.get(),side,1.0f);
            Rounded(tab.get(),24);
            Text(tab.get(),tooltip,(std::string(job.name)+" · choisir les ressources").c_str());
            Ref image(Element());Ignore(image.get());Style(image.get(),"position",1);
            Pixels(image.get(),"left",3);Pixels(image.get(),"top",3);Pixels(image.get(),"width",40);Pixels(image.get(),"height",40);
            LoadJobIcon(image.get(),job.id);api.Call(tab.get(),"Add",{image.get()});
            api.Call(next->toolbar.get(),"Add",{tab.get()});next->tabs.push_back(std::move(tab));
            next->groups.emplace_back(header.get());
        }
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
        bool canOpen=hasRows&&!mini;api.Invoke(headerCanOpen,header.get(),{&canOpen});add(header.get());
    }
    if(next->rows.size()!=catalog.size())throw std::runtime_error("Resource catalog job unsupported");
    if(mini)ShowJob(*next,-1);
    if(!Layout(*next,mini))return;
    // The transport filters and their original parent/order are never modified.
    target=std::move(next);
    api.Call(root.get(),"Add",{target->resources.get()});target->attached=true;
    // Native handlers manage enter/leave. These owned category controls stay
    // enabled; avoid AddCursorHandler, which overwrites the native enabled-state callback.
    // Register only after retaining the tree so partial failure is retryable.
    for(const auto& tab:target->tabs){
        target->cursorTargets.emplace_back(tab.get());
        int64_t hand=2097152;int normal=0;
        api.Invoke(enableCursor,nullptr,{tab.get(),&hand,&normal});
    }
    if(target->center.get()){
        target->cursorTargets.emplace_back(target->center.get());
        int64_t hand=2097152;int normal=0;
        api.Invoke(enableCursor,nullptr,{target->center.get(),&hand,&normal});
    }
    Log("RESOURCE_MAP_UI_READY view="+std::string(mini?"mini":"full")+" rows="+std::to_string(target->rows.size()));
}
void Sync(State& state){
    const auto& settings=module->settings;
    for(auto& row:state.rows){bool value=std::binary_search(settings.resourceMapItems.begin(),settings.resourceMapItems.end(),row.id);api.Invoke(checkboxSet,row.checkbox.get(),{&value});}
    bool enabled=state.mini?settings.resourceMiniMapEnabled:settings.resourceMapEnabled;
    api.Invoke(checkboxSet,state.master.get(),{&enabled});
    state.selection=settings.resourceMapItems;state.enabled=enabled;
}
void Poll(State& state){
    // External reload/reset wins over stale visual values. Native CheckboxCustom
    // owns click/keyboard handling; no fabricated managed callback is installed.
    const bool currentEnabled=state.mini?module->settings.resourceMiniMapEnabled:module->settings.resourceMapEnabled;
    if(state.selection!=module->settings.resourceMapItems||state.enabled!=currentEnabled){Sync(state);return;}
    std::vector<int> selected;
    // Keep IDs unknown to this catalog when opening an older local revision.
    const auto catalog=Catalog();
    for(int id:state.selection)if(std::none_of(catalog.begin(),catalog.end(),[id](const Resource& item){return item.itemId==id;}))selected.push_back(id);
    for(const auto& row:state.rows)if(api.Value<bool>(api.Invoke(checkboxValue,row.checkbox.get())))selected.push_back(row.id);
    std::sort(selected.begin(),selected.end());
    const bool enabled=api.Value<bool>(api.Invoke(checkboxValue,state.master.get()));
    if(selected==state.selection&&enabled==state.enabled)return;
    if(!ValidSelection(selected))throw std::runtime_error("Resource selection invalid");
    auto next=module->settings;
    if(state.mini)next.resourceMiniMapEnabled=enabled;else next.resourceMapEnabled=enabled;
    next.resourceMapItems=std::move(selected);
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
    elementClass=api.Class(UiAssembly,UiNamespace,"VisualElement");
    api.FindVerified(elementClass,".ctor",0,0x55F50B0);
    api.FindVerified(elementClass,"Insert",2,0x55E5710);
    api.FindVerified(elementClass,"set_pickingMode",1,0x55F97B0);
    InstallIcons();
    cursorClass=api.Class("Core.dll","Core.UILogic","DofusCursor");
    enableCursor=api.FindVerified(cursorClass,"EnableCursorHandler",3,0x2808CD0);
    removeCursor=api.FindVerified(cursorClass,"RemoveCursorHandler",1,0x2809CD0);
    for(const auto& [name,offset]:std::array<std::pair<const char*,size_t>,2>{{
        {"s_mouseEnterCursorMap",0x40},{"s_mouseLeaveCursorMap",0x48}}}){
        auto field=api.class_get_field_from_name(cursorClass,name);
        if(!field||api.field_get_offset(field)!=offset)throw std::runtime_error("Resource cursor cache layout mismatch");
    }
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
    auto square=api.class_get_field_from_name(checkboxClass,"m_checkbox");
    if(!square||api.field_get_offset(square)!=0x360)throw std::runtime_error("Resource checkbox square layout mismatch");
    api.FindVerified(headerClass,"get_titleLabel",0,0x15E8650);
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
            if(api.Value<bool>(api.Invoke(checkboxValue,miniOwned->center.get()))){
                bool off=false;api.Invoke(checkboxSet,miniOwned->center.get(),{&off});
                CenterMiniMap(surface);
            }
            PollTabs(*miniOwned);Poll(*miniOwned);
            // Resource marker layers can be recreated after native filter
            // refreshes. Keep the drawer above those non-interactive markers.
            api.Invoke(bringToFront,miniOwned->resources.get());
        }
    }catch(const std::exception& error){Log(std::string("RESOURCE_MINIMAP_UI_ERROR ")+error.what());Detach(miniOwned);}
}
void RestoreUi(){pending.reset();Detach(owned);Detach(miniOwned);}
bool UiCleanupPending(){return owned!=nullptr||miniOwned!=nullptr;}
}
