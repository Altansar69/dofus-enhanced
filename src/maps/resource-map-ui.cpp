#include "maps/resource-map-ui.hpp"
#include "maps/resource-map-data.hpp"
#include "maps/resource-map-policy.hpp"
#include "maps/resource-map-icons.hpp"
#include "maps/resource-map-render.hpp"
#include "ui/native-click.hpp"
#include "core/module.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace resourceMap {
namespace {
using namespace managed;
constexpr const char* UiAssembly="UnityEngine.UIElementsModule.dll";
constexpr const char* UiNamespace="UnityEngine.UIElements";
constexpr const char* Figma="Core.UILogic.Components.Figma";
constexpr const char* Tooltips="Core.UILogic.Components.Tooltips";
constexpr size_t MaxRows=128;
// Mini-map centering control: the native button component and icon of the
// full map's btn_player in a square host; its click is observed through the
// shared native button hook and consumed by the next UI tick.
constexpr float CenterSize=40;
constexpr int PlayerIcon=228; // FigmaIcons.player
// Big map « Ressources » mode, made as the game's own Anomalies and Conquête
// (CartographyMapUI, fhi):
// - its square is a mode button of ctr_worldmapButtons, right after
//   btn_anomaly: DofusButtonCustom, main-style secondary, an icon of the
//   game's set, no rich text, 4 px above the next square (btn_anomaly's
//   margin), and the game's text tooltip (TooltipManipulator, placed as
//   btn_anomaly's: its top left beside the square's top right, 12 px away,
//   hidden when the mouse goes out);
// - its content is a WindowFigma placed as the game's mode windows
//   (ctr_locTreeConquest: secondary header, 392 x 700, 16 px from the right
//   and 10 % from the top), right after ctr_locTreeAnomaly in their parent;
//   inside, the conquest window's padding, a list with the game's
//   scrollbarPadding class and the anomaly window's Footer. No colour or
//   rounded box of the mod's own.
// The game's squares keep one look (no selected state): the open window tells
// the mode, one at a time. Selecting ours leaves the game's anomaly or conquest
// mode with fhi.bejv(0), the game's own way back to its world map mode (its
// windows hidden, its world map layers back), where the resource markers
// belong; a game mode chosen closes ours. Nothing of the game's is restyled.
constexpr int ResourcesIcon=247; // FigmaIcons.resources
constexpr int SecondaryStyle=1;  // DofusButtonCustom and HeaderWindow ComponentStyleEnum.secondary
constexpr int WorldMapMode=0;    // fhi+fgq: 0 world map, 1 anomalies, 2 conquest
constexpr int TipTopLeft=0,TipTopRight=2,TipOut=1; // TooltipPositioning+Location; TooltipManipulatorBehavior.Out
constexpr float WindowWidth=392,WindowHeight=700;
constexpr const char* TabTitle="Ressources";
struct Rect {float x,y,width,height;};
struct Color {float r,g,b,a;};
using LifeFn=void(*)(Obj,Method);
LifeFn originalEnable{},originalDisable{};
Klass cursorClass{},checkboxClass{},headerClass{},scrollClass{},labelClass{},elementClass{},buttonClass{};
Klass bindingClass{},windowClass{},tooltipClass{};
Method buttonCtor{},buttonIcon{},buttonStyle{},buttonRichText{};
Method checkboxCtor{},checkboxValue{},checkboxSet{},checkboxText{},checkboxSize{},checkboxLevel{},checkboxIcon{};
Method headerCtor{},headerTitle{},headerOpen{},headerCanOpen{},headerAdd{},scrollCtor{},scrollContent{};
Method scrollHorizontal{},scrollVertical{};
Method labelCtor{},labelText{},labelSingleLine{},tooltip{},bringToFront{},enableCursor{},removeCursor{};
Method worldButtons{},anomalyButton{},conquestButton{},anomalyWindow{},leaveMode{};
Method windowCtor{},windowTitle{},windowHeaderStyle{},windowClose{};
Method tooltipCtor{},hideTooltip{},addManipulator{},removeManipulator{};
Method addChild{},insert{},indexOf{},addClass{},removeFromHierarchy{};
bool installed=false;
Ref pending;
struct Row {int id=0; Ref checkbox;};
struct State {
    Ref owner,root,mapRoot,panel,scroll,content,resources,master,compositor,character,cartography;
    std::vector<Row> rows;
    std::vector<int> selection;
    Ref toolbar,center,centerButton;
    std::vector<Ref> tabs,groups,cursorTargets;
    // Big map: the game's mode column and squares, our square and its game
    // tooltip, the parent of the game's mode windows, our window's close button.
    // resources is our window there.
    Ref buttons,anomaly,conquest,tab,tip,host,close;
    int activeJob=-1;
    bool enabled=true,attached=false,mini=false,cleaning=false,shown=true,centerShown=true,centerRequested=false;
    // Big map: the mode selected; its square shown; the tooltip added to it;
    // a selection to apply (leaving the game's mode) at the next tick.
    bool open=false,tabShown=true,tipAdded=false,switchRequested=false;
    float height=-1,width=-1,left=-1,top=-1,centerLeft=-1,centerTop=-1;
};
std::unique_ptr<State> owned,miniOwned;
bool Session(){return installed&&module&&module->ready&&module->servicesReady&&!module->stopped&&!module->stopRequested&&module->compositor.get();}
bool CurrentOwner(Obj owner){
    return owner&&Session()&&api.Field(owner,"dtzy")==module->FindService("fng")&&
        api.Field(owner,"dtzz")==module->FindService("fkc")&&
        *static_cast<bool*>(api.FieldAddress(owner,"dtzx"));
}
// The map window's mode (fhi.dubo, fhi+fgq).
int Mode(Obj owner){return *static_cast<int*>(api.FieldAddress(owner,"dubo"));}
void RequireField(Klass type,const char* name,size_t offset){
    auto field=api.class_get_field_from_name(type,name);
    if(!field||api.field_get_offset(field)!=offset)throw std::runtime_error(std::string("Resource map window field mismatch: ")+name);
}
void Text(Obj object,Method setter,const char* text){Ref value(api.string_new(text));api.Invoke(setter,object,{value.get()});}
Obj ReadStyle(Obj element,const char* property){
    Ref style(api.Call(element,"get_style"));
    auto value=api.Call(style.get(),(std::string("UnityEngine.UIElements.IStyle.get_")+property).c_str());
    if(!value||!api.class_is_valuetype(api.object_get_class(value)))throw std::runtime_error("Resource filters expected boxed style");
    return value;
}
void Length(Obj element,const char* property,float value,int unit){
    Ref box(ReadStyle(element,property));
    Ref length(api.New(api.Class(UiAssembly,UiNamespace,"Length"),{&value,&unit}));
    api.Call(box.get(),"set_value",{api.object_unbox(length.get())});
    Ref style(api.Call(element,"get_style"));
    api.Call(style.get(),(std::string("UnityEngine.UIElements.IStyle.set_")+property).c_str(),{api.object_unbox(box.get())});
}
void Pixels(Obj element,const char* property,float value){Length(element,property,value,0);}
void Percent(Obj element,const char* property,float value){Length(element,property,value,1);}
template<class T> void Style(Obj element,const char* property,T value){
    Ref box(ReadStyle(element,property));api.Call(box.get(),"set_value",{&value});
    Ref style(api.Call(element,"get_style"));
    api.Call(style.get(),(std::string("UnityEngine.UIElements.IStyle.set_")+property).c_str(),{api.object_unbox(box.get())});
}
Obj Parent(Obj element){return api.Call(element,"get_parent");}
Obj New(Klass type,Method ctor){
    Ref value(api.object_new(type));
    if(!value.get())throw std::runtime_error("Resource map allocation failed");
    api.Invoke(ctor,value.get());
    return value.get();
}
void Classes(Obj element,std::initializer_list<const char*> names){for(const auto* name:names)Text(element,addClass,name);}
// The position of a native child, -1 when it is not one.
int IndexOf(Obj parent,Obj child){return api.Value<int>(api.Invoke(indexOf,parent,{child}));}
void InsertAfter(Obj parent,Obj sibling,Obj child){
    const int index=IndexOf(parent,sibling);
    if(index<0)throw std::runtime_error("Resource map native sibling unavailable");
    int at=index+1;api.Invoke(insert,parent,{&at,child});
}
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
        // Big map: the game's tooltip closed and taken off our square, which
        // then leaves the mode column; the game's squares stay as they are.
        if(state.tipAdded){
            api.Invoke(hideTooltip,state.tip.get());
            api.Invoke(removeManipulator,nullptr,{state.tab.get(),state.tip.get()});
            state.tipAdded=false;
        }
        if(state.tab.get()&&Parent(state.tab.get()))api.Invoke(removeFromHierarchy,state.tab.get());
        if(state.center.get()&&Parent(state.center.get()))api.Call(state.center.get(),"RemoveFromHierarchy");
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
// Mini-map picker geometry. The big map's window takes the game's own sizes.
bool Layout(State& state,const MiniMapSurface& mini){
    const float width=(std::min)(320.0f,mini.width-16.0f);
    if(!std::isfinite(width)||width<128)return false;
    const int columns=(std::max)(1,int(width/52));
    const float toolbarHeight=52.0f*float((std::size(Jobs)+columns-1)/columns);
    const float height=mini.height-48.0f-toolbarHeight;
    if(!std::isfinite(height)||height<48)return false;
    const float bounded=(std::min)(height,520.0f);
    if(state.height!=bounded){Pixels(state.scroll.get(),"height",bounded);state.height=bounded;}
    if(state.width!=width){Pixels(state.resources.get(),"width",width);state.width=width;}
    const float left=mini.left+mini.width-width-8,top=mini.top+40;
    if(state.left!=left){Pixels(state.resources.get(),"left",left);state.left=left;}
    if(state.top!=top){Pixels(state.resources.get(),"top",top);state.top=top;}
    if(state.center.get()){
        // Bottom-left corner: clear of the native header, the drawer and the resize grip.
        const float centerLeft=mini.left+8,centerTop=mini.top+mini.height-CenterSize-8;
        if(state.centerLeft!=centerLeft){Pixels(state.center.get(),"left",centerLeft);state.centerLeft=centerLeft;}
        if(state.centerTop!=centerTop){Pixels(state.center.get(),"top",centerTop);state.centerTop=centerTop;}
    }
    return true;
}
bool Fresh(const State& state,const MiniMapSurface* mini=nullptr){
    const bool ownerCurrent=mini?(mini->root.get()==state.root.get()&&mini->mapRoot.get()==state.mapRoot.get()&&
        mini->mapRoot.get()&&Parent(mini->mapRoot.get())==state.root.get()):
        CurrentOwner(state.owner.get())&&api.Field(state.owner.get(),"dzyj")==state.root.get();
    if(state.cleaning||!ownerCurrent||!Session()||state.compositor.get()!=module->compositor.get()||
        state.character.get()!=module->FindService("fng")||state.cartography.get()!=module->FindService("fkc")||
        api.Call(state.root.get(),"get_panel")!=state.panel.get())return false;
    if(!state.attached)return true;
    if(state.mini)return Parent(state.resources.get())==state.root.get()&&(!state.center.get()||Parent(state.center.get())==state.root.get());
    // Big map: our square still in the game's mode column and our window beside
    // the game's mode windows, both in the map's panel.
    return Parent(state.tab.get())==state.buttons.get()&&Parent(state.resources.get())==state.host.get()&&
        api.Call(state.tab.get(),"get_panel")==state.panel.get()&&api.Call(state.resources.get(),"get_panel")==state.panel.get();
}
void Attach(std::unique_ptr<State>& target,Obj owner,const MiniMapSurface* mini=nullptr){
    if(!mini&&!CurrentOwner(owner))return;
    Ref root(mini?mini->root.get():api.Field(owner,"dzyj"));
    if(!root.get())return;
    Ref panel(api.Call(root.get(),"get_panel"));if(!panel.get())return;
    // Big map: the game's mode column, its squares and the anomaly window
    // (CartographyMapUIBinding, a value type read on a boxed copy).
    Ref buttons,anomaly,conquest,anomalyFrame,host;
    if(mini){
        const auto bounds=api.Value<Rect>(api.Call(root.get(),"get_worldBound"));
        if(!std::isfinite(bounds.width)||!std::isfinite(bounds.height))return;
        if(mini->width<144||mini->height<160)return;
    }else{
        Ref binding(api.Field(owner,"dzyi"));
        if(!binding.get()||api.object_get_class(binding.get())!=bindingClass)throw std::runtime_error("Cartography binding unavailable");
        auto* data=api.object_unbox(binding.get());
        buttons.reset(api.Invoke(worldButtons,data));anomaly.reset(api.Invoke(anomalyButton,data));
        conquest.reset(api.Invoke(conquestButton,data));anomalyFrame.reset(api.Invoke(anomalyWindow,data));
        if(!buttons.get()||!anomaly.get()||!conquest.get()||Parent(anomaly.get())!=buttons.get()||Parent(conquest.get())!=buttons.get())
            throw std::runtime_error("Cartography mode column unavailable");
        if(anomalyFrame.get())host.reset(Parent(anomalyFrame.get()));
        if(!host.get()||api.Call(host.get(),"get_panel")!=panel.get())throw std::runtime_error("Cartography mode windows unavailable");
    }
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
    // Where the master checkbox and the source note go: the mini-map's list,
    // the footer of the big map's window.
    Ref options;
    if(mini){
        next->resources.reset(Element());
        Style(next->resources.get(),"position",1);
        Text(next->resources.get(),tooltip,"Choisir les ressources affichées sur la carte et la mini-carte.");
        next->toolbar.reset(Element());
        Style(next->toolbar.get(),"flexDirection",2);Style(next->toolbar.get(),"flexWrap",1);
        api.Call(next->resources.get(),"Add",{next->toolbar.get()});
        api.Call(next->resources.get(),"Add",{next->scroll.get()});
        Style(next->scroll.get(),"backgroundColor",Color{.105f,.115f,.12f,1});
        Rounded(next->scroll.get(),8);
        next->center.reset(Element());
        Style(next->center.get(),"position",1);
        Pixels(next->center.get(),"width",CenterSize);Pixels(next->center.get(),"height",CenterSize);
        for(auto side:{"paddingLeft","paddingRight","paddingTop","paddingBottom"})Pixels(next->center.get(),side,0);
        Style(next->center.get(),"alignItems",2);Style(next->center.get(),"justifyContent",1);
        Text(next->center.get(),tooltip,"Recentrer la mini-carte sur mon personnage");
        // Same native component and icon as btn_player on the full map: native
        // hover state, cursor and tooltip. Its clickable stops pointer
        // propagation, so the click is reported by nativeClick (Clicked).
        Ref button(api.object_new(buttonClass));api.Invoke(buttonCtor,button.get());
        int player=PlayerIcon;api.Invoke(buttonIcon,button.get(),{&player});
        Text(button.get(),tooltip,"Recentrer la mini-carte sur mon personnage");
        api.Call(next->center.get(),"Add",{button.get()});next->centerButton.reset(button.get());
        options.reset(next->content.get());
    }else{
        next->buttons=std::move(buttons);next->anomaly=std::move(anomaly);next->conquest=std::move(conquest);next->host=std::move(host);
        // The window: the game's mode window component and layout, closed until
        // the square is selected.
        next->resources.reset(New(windowClass,windowCtor));
        const Obj window=next->resources.get();
        Text(window,windowTitle,TabTitle);
        int secondary=SecondaryStyle;api.Invoke(windowHeaderStyle,window,{&secondary});
        next->close.reset(api.Invoke(windowClose,window));
        if(!next->close.get())throw std::runtime_error("Resource window close button unavailable");
        Style(window,"position",1);Percent(window,"top",10);Pixels(window,"right",16);
        Pixels(window,"width",WindowWidth);Pixels(window,"height",WindowHeight);
        // As the Ocre panel: the window stays on small screens.
        Percent(window,"maxHeight",86);Style(window,"flexShrink",0.0f);
        Style(window,"display",1);next->shown=false;
        Ref body(Element());
        Pixels(body.get(),"paddingTop",16);Pixels(body.get(),"paddingLeft",24);Style(body.get(),"flexGrow",1.0f);
        api.Invoke(addChild,window,{body.get()});
        Classes(next->scroll.get(),{"scrollbarPadding"});Style(next->scroll.get(),"flexGrow",1.0f);
        api.Invoke(addChild,body.get(),{next->scroll.get()});
        options.reset(Element());
        Classes(options.get(),{"Footer"});
        Pixels(options.get(),"paddingTop",12);Pixels(options.get(),"paddingBottom",12);
        Pixels(options.get(),"paddingRight",16);Pixels(options.get(),"paddingLeft",16);
        Style(options.get(),"flexShrink",0.0f);
        api.Invoke(addChild,window,{options.get()});
        // The square: the game's own mode button, with the game's tooltip.
        next->tab.reset(New(buttonClass,buttonCtor));
        bool rich=false;api.Invoke(buttonRichText,next->tab.get(),{&rich});
        api.Invoke(buttonStyle,next->tab.get(),{&secondary});
        int icon=ResourcesIcon;api.Invoke(buttonIcon,next->tab.get(),{&icon});
        Pixels(next->tab.get(),"marginBottom",4);
        Ref service(module->FindService("fou"));
        if(!service.get())throw std::runtime_error("Tooltip service unavailable");
        Ref caption(api.string_new(TabTitle));
        next->tip.reset(api.object_new(tooltipClass));
        int origin=TipTopLeft,source=TipTopRight,behavior=TipOut;bool front=false;
        api.Invoke(tooltipCtor,next->tip.get(),{service.get(),caption.get(),&origin,&source,&behavior,&front});
    }
    auto add=[&](Obj child){api.Call(next->content.get(),"Add",{child});};
    next->master.reset(Checkbox(mini?"Afficher sur la mini-carte":"Afficher sur la grande carte",next->enabled));
    api.Call(options.get(),"Add",{next->master.get()});
    Ref note(api.object_new(labelClass));api.Invoke(labelCtor,note.get());
    Text(note.get(),labelText,"DofusDB · Points de récolte recensés");
    bool multiline=false;api.Invoke(labelSingleLine,note.get(),{&multiline});
    Text(note.get(),tooltip,"Source : DofusDB. Quantités recensées par carte, intérieurs inclus. La présence actuelle après récolte n’est pas connue.");
    if(mini){
        Pixels(note.get(),"fontSize",12.0f);Pixels(note.get(),"marginBottom",6.0f);
        Style(note.get(),"color",Color{.75f,.79f,.81f,1});
    }else{
        // The game's footer text, as the Ocre panel's legend.
        Classes(note.get(),{"textColor_white_white80"});Pixels(note.get(),"marginTop",6.0f);
    }
    api.Call(options.get(),"Add",{note.get()});
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
    if(mini){ShowJob(*next,-1);if(!Layout(*next,*mini))return;}
    // The transport filters and their original parent/order are never modified.
    target=std::move(next);
    if(!mini){
        // The game's squares, windows and their order stay; ours go right after
        // its Anomalies square and window. Registered only after retaining the
        // tree so a partial failure is retryable.
        InsertAfter(target->buttons.get(),target->anomaly.get(),target->tab.get());
        api.Invoke(addManipulator,nullptr,{target->tab.get(),target->tip.get()});target->tipAdded=true;
        InsertAfter(target->host.get(),anomalyFrame.get(),target->resources.get());
        target->attached=true;
        Log("RESOURCE_MAP_UI_READY view=full mode=tab rows="+std::to_string(target->rows.size()));
        return;
    }
    api.Call(root.get(),"Add",{target->resources.get()});target->attached=true;
    if(target->center.get())api.Call(root.get(),"Add",{target->center.get()});
    // Native handlers manage enter/leave. These owned category controls stay
    // enabled; avoid AddCursorHandler, which overwrites the native enabled-state callback.
    // Register only after retaining the tree so partial failure is retryable.
    for(const auto& tab:target->tabs){
        target->cursorTargets.emplace_back(tab.get());
        int64_t hand=2097152;int normal=0;
        api.Invoke(enableCursor,nullptr,{tab.get(),&hand,&normal});
    }
    // The centering button's native component carries its own hover cursor.
    Log("RESOURCE_MAP_UI_READY view=mini rows="+std::to_string(target->rows.size()));
}
void Sync(State& state){
    const auto& settings=module->settings;
    for(auto& row:state.rows){bool value=std::binary_search(settings.resourceMapItems.begin(),settings.resourceMapItems.end(),row.id);api.Invoke(checkboxSet,row.checkbox.get(),{&value});}
    bool enabled=state.mini?settings.resourceMiniMapEnabled:settings.resourceMapEnabled;
    api.Invoke(checkboxSet,state.master.get(),{&enabled});
    state.selection=settings.resourceMapItems;state.enabled=enabled;
}
// Big map: our window hidden at once (a game square's click); the next tick
// would hide it anyway.
void HideWindow(State& state){
    if(!state.shown)return;
    Style(state.resources.get(),"display",1);state.shown=false;
}
// Big map: the mode selected or left by its square, its window's close button
// and the game's modes, applied once per tick.
void Tabs(State& state){
    const bool switching=std::exchange(state.switchRequested,false);
    // Off (« Ressources sur la grande carte »): no square, so no mode.
    if(!state.enabled){state.open=false;return;}
    // Selected while the game shows its anomalies or conquest: the game's own
    // way back to its world map mode, as its windows' close buttons and a
    // second click on its square do (fhi.bejv toggles the asked mode off).
    if(switching&&state.open&&Mode(state.owner.get())!=WorldMapMode){
        int worldMap=WorldMapMode;api.Invoke(leaveMode,state.owner.get(),{&worldMap});
    }
    // A game mode chosen by other means than its square (its anomaly
    // notifications, a link that opens the map in a mode…): ours closes.
    if(state.open&&Mode(state.owner.get())!=WorldMapMode)state.open=false;
}
void ApplyVisibility(State& state){
    // A map whose resource display is off also hides its picker; the option
    // or the other map's picker re-enables it.
    if(state.center.get()){
        const bool center=module->settings.miniMapCenterButton;
        if(state.centerShown!=center){Style(state.center.get(),"display",center?0:1);state.centerShown=center;}
    }
    if(!state.mini){
        // The square follows « Ressources sur la grande carte »; the window, the
        // mode being selected.
        if(state.tabShown!=state.enabled){Style(state.tab.get(),"display",state.enabled?0:1);state.tabShown=state.enabled;}
        const bool shown=state.enabled&&state.open;
        if(state.shown==shown)return;
        Style(state.resources.get(),"display",shown?0:1);state.shown=shown;
        return;
    }
    // The mini-map's bar of the six jobs, with the list it opens, also follows
    // « Barre des ressources sur la mini-carte »: unchecked, it leaves while the
    // markers of the chosen resources stay (resource-map-render follows
    // resourceMiniMapEnabled only); the choice is then made on the big map.
    const bool shown=state.enabled&&module->settings.resourceMiniMapBar;
    if(state.shown==shown)return;
    Style(state.resources.get(),"display",shown?0:1);state.shown=shown;
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
// Native clicks (shared ExtendedClickable hook, after the game's own handling):
// the mini-map's centering button, our square and our window's close button,
// remembered for the next UI tick, which validates the surface first. The
// game's Anomalies and Conquête squares are never claimed: their ClickEvent
// switches the game's mode; ours closes at once, as one of the game's own
// windows does, and a selection of ours still waiting for its tick is dropped
// (the later click wins).
bool Clicked(Obj button){
    if(!button)return false;
    if(miniOwned&&button==miniOwned->centerButton.get()){miniOwned->centerRequested=true;return true;}
    if(!owned||owned->cleaning||!owned->attached)return false;
    auto& state=*owned;
    if(button==state.tab.get()){state.open=!state.open;state.switchRequested=state.open;return true;}
    if(button==state.close.get()){state.open=false;state.switchRequested=false;return true;}
    if(button==state.anomaly.get()||button==state.conquest.get()){
        state.open=false;state.switchRequested=false;
        try{HideWindow(state);}catch(const std::exception& error){Log(std::string("RESOURCE_MAP_UI_ERROR ")+error.what());}
    }
    return false;
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
    insert=api.FindVerified(elementClass,"Insert",2,0x55E5710);
    api.FindVerified(elementClass,"set_pickingMode",1,0x55F97B0);
    buttonClass=api.Class("Core.dll",Figma,"DofusButtonCustom");
    buttonCtor=api.FindVerified(buttonClass,".ctor",0,0x170D6F0);
    buttonIcon=api.FindVerified(buttonClass,"set_icon",1,0x170DFE0);
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
    // Big map « Ressources » mode: the map window's mode (fhi.dubo, fhi+fgq)
    // and the game's way back to its world map mode, the binding's mode column,
    // squares and anomaly window, and the game's components for ours.
    RequireField(native,"dtzx",0x408);
    RequireField(native,"dtzy",0x410);
    RequireField(native,"dtzz",0x418);
    RequireField(native,"dubo",0x52C);
    leaveMode=api.FindVerified(native,"bejv",1,0x1BB4090);
    bindingClass=api.Class("Core.dll","Core.UILogic.Bindings","CartographyMapUIBinding");
    worldButtons=api.FindVerified(bindingClass,"get_ctr_worldmapButtons",0,0x2EA2830);
    anomalyButton=api.FindVerified(bindingClass,"get_btn_anomaly",0,0x2EA0940);
    conquestButton=api.FindVerified(bindingClass,"get_btn_conquest",0,0x2EA0A00);
    anomalyWindow=api.FindVerified(bindingClass,"get_ctr_locTreeAnomaly",0,0x2EA2530);
    buttonStyle=api.FindVerified(buttonClass,"set_mainStyle",1,0x170E210);
    buttonRichText=api.FindVerified(buttonClass,"set_enableRichText",1,0x170DEE0);
    windowClass=api.Class("Core.dll",Figma,"WindowFigma");
    windowCtor=api.FindVerified(windowClass,".ctor",0,0x17987B0);
    windowTitle=api.FindVerified(windowClass,"set_title",1,0x1799210);
    windowHeaderStyle=api.FindVerified(windowClass,"set_headerStyle",1,0x1798F30);
    windowClose=api.FindVerified(windowClass,"get_closeButton",0,0x1798CF0);
    tooltipClass=api.Class("Core.dll",Tooltips,"TooltipManipulator");
    tooltipCtor=api.FindVerified(tooltipClass,".ctor",6,0x15DFAE0);
    hideTooltip=api.FindVerified(api.Class("Core.dll",Tooltips,"BaseTooltipManipulator"),"HideTooltip",0,0x15CD030);
    auto extensions=api.Class(UiAssembly,UiNamespace,"VisualElementExtensions");
    addManipulator=api.FindVerified(extensions,"AddManipulator",2,0x56755F0);
    removeManipulator=api.FindVerified(extensions,"RemoveManipulator",2,0x5676A90);
    addChild=api.FindVerified(elementClass,"Add",1,0x55DFAD0);
    indexOf=api.FindVerified(elementClass,"IndexOf",1,0x55E55B0);
    addClass=api.FindVerified(elementClass,"AddToClassList",1,0x55DF820);
    removeFromHierarchy=api.FindVerified(elementClass,"RemoveFromHierarchy",0,0x55E7BD0);
    AddHook(api.Find(native,"OnEnable",0),0x1BAEA20,reinterpret_cast<void*>(EnableHook),reinterpret_cast<void**>(&originalEnable));
    AddHook(api.Find(native,"OnDisable",0),0x1BAE560,reinterpret_cast<void*>(DisableHook),reinterpret_cast<void**>(&originalDisable));
    nativeClick::AddHandler(Clicked);
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
        if(owned){Poll(*owned);Tabs(*owned);ApplyVisibility(*owned);}
    }catch(const std::exception& error){Log(std::string("RESOURCE_MAP_UI_ERROR ")+error.what());pending.reset();Detach(owned);}
    // Discover the mini-map even when the full map was never opened. Its
    // controls must remain reachable with rendering disabled or no selection.
    try{
        if(!Session()){Detach(miniOwned);return;}
        auto surface=ReadMiniMapSurface();
        if(miniOwned&&!Fresh(*miniOwned,&surface)){if(!Detach(miniOwned))return;}
        if(!miniOwned&&surface.root.get())Attach(miniOwned,nullptr,&surface);
        if(miniOwned){
            if(!Layout(*miniOwned,surface)){Detach(miniOwned);return;}
            if(miniOwned->centerRequested){
                miniOwned->centerRequested=false;
                if(module->settings.miniMapCenterButton)CenterMiniMap(surface);
            }
            PollTabs(*miniOwned);Poll(*miniOwned);ApplyVisibility(*miniOwned);
            // Resource marker layers can be recreated after native filter
            // refreshes. Keep the drawer and the button above those markers.
            api.Invoke(bringToFront,miniOwned->resources.get());
            if(miniOwned->center.get())api.Invoke(bringToFront,miniOwned->center.get());
        }
    }catch(const std::exception& error){Log(std::string("RESOURCE_MINIMAP_UI_ERROR ")+error.what());Detach(miniOwned);}
}
void RestoreUi(){pending.reset();Detach(owned);Detach(miniOwned);}
Obj CartographyWindow(){return owned?owned->owner.get():pending.get();}
bool UiCleanupPending(){return owned!=nullptr||miniOwned!=nullptr;}
bool ResourceTabOpen(){return owned&&owned->open;}
void CloseResourceTab(){if(owned){owned->open=false;owned->switchRequested=false;}}
}
