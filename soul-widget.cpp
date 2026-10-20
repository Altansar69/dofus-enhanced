#include "soul-widget.hpp"
#include "module.hpp"
#include "spouse-party-widget.hpp"
#include <cmath>

namespace {
using namespace managed;
constexpr const char* Figma="Core.UILogic.Components.Figma";
Ref button, manager, service, compositor, parent, menu, tooltip, tooltipService;
Klass managerClass{}, serviceClass{}, toggleClass{}, bindingClass{}, tooltipClass{};
Method menuToggle{}, getSize{}, tooltipCtor{}, addManipulator{}, removeManipulator{}, hideTooltip{};
bool installed=false, reported=false, removing=false;
uint64_t nextRefresh=0;
std::string lastState;
void Report(const std::string& value) {
    if(value!=lastState) { Log("SOUL_WIDGET_STATE "+value); lastState=value; }
}
bool Is(Obj object,Klass klass) { return object&&api.class_is_assignable_from(klass,api.object_get_class(object)); }
void Missing(const char* reason,Obj object=nullptr) {
    Report(std::string(reason)+" type="+(object?api.class_get_name(api.object_get_class(object)):"null"));
}
void Pixels(Obj style,const char* property,float value) {
    const std::string name=std::string("UnityEngine.UIElements.IStyle.");
    Ref box(api.Call(style,(name+"get_"+property).c_str()));
    int unit=0;
    Ref length(api.New(api.Class("UnityEngine.UIElementsModule.dll","UnityEngine.UIElements","Length"),{&value,&unit}));
    api.Call(box.get(),"set_value",{api.object_unbox(length.get())});
    api.Call(style,(name+"set_"+property).c_str(),{api.object_unbox(box.get())});
}
using ToggleFn=void(*)(Obj,Method);
ToggleFn originalToggle=nullptr;
bool Available() { return installed&&module&&module->ready&&module->servicesReady&&!module->stopped&&!module->stopRequested; }
bool Current() {
    return Available()&&!removing&&button.get()&&tooltip.get()&&module->compositor.get()==compositor.get()&&
        module->FindService("frm")==service.get()&&api.Field(service.get(),"<dzqi>k__BackingField")==manager.get()&&
        module->FindService("fou")==tooltipService.get()&&
        api.Call(menu.get(),"get_parent")==parent.get()&&
        api.Call(button.get(),"get_parent")==parent.get()&&api.Call(button.get(),"get_panel");
}
void Sync() { bool on=module->settings.soulReminder;api.Call(button.get(),"SetValueWithoutNotify",{&on}); }
void SyncLayout() {
    int size=api.Value<int>(api.Invoke(getSize,menu.get()));
    if(size<0||size>5)throw std::runtime_error("Unsupported widget toggle size");
    if(api.Value<int>(api.Invoke(getSize,button.get()))!=size)api.Call(button.get(),"set_toggleSize",{&size});
    // The native widget UXML also prevents the buttons shrinking in a full bar.
    Ref style(api.Call(button.get(),"get_style"));
    Ref shrink(api.Call(style.get(),"UnityEngine.UIElements.IStyle.get_flexShrink"));
    float zero=0;api.Call(shrink.get(),"set_value",{&zero});
    api.Call(style.get(),"UnityEngine.UIElements.IStyle.set_flexShrink",{api.object_unbox(shrink.get())});
    // Native toolbar layout can override the enum size (GameMenuBtnSize=36).
    // Match the actual Menu face, including UI scaling; never unhide a collapsed bar.
    Ref resolved(api.Call(menu.get(),"get_resolvedStyle"));
    float width=api.Value<float>(api.Call(resolved.get(),"get_width"));
    float height=api.Value<float>(api.Call(resolved.get(),"get_height"));
    if(std::isfinite(width)&&std::isfinite(height)&&width>=16&&width<=160&&height>=16&&height<=160) {
        for(auto property:{"width","minWidth","maxWidth"})Pixels(style.get(),property,width);
        for(auto property:{"height","minHeight","maxHeight"})Pixels(style.get(),property,height);
    }
    int ownIndex=api.Value<int>(api.Call(parent.get(),"IndexOf",{button.get()}));
    int menuIndex=api.Value<int>(api.Call(parent.get(),"IndexOf",{menu.get()}));
    if(ownIndex<0||menuIndex<0)throw std::runtime_error("Widget menu anchor detached");
    if(ownIndex+1!=menuIndex)api.Call(button.get(),"PlaceBehind",{menu.get()});
}
void ToggleHook(Obj owner,Method method) {
    if (HandleSpousePartyWidgetToggle(owner)) return;
    if (owner!=button.get()) { originalToggle(owner,method);return; }
    try {
        if (!Current()||!api.Value<bool>(api.Call(owner,"get_enabledInHierarchy"))) return;
        auto settings=module->settings;settings.soulReminder=!settings.soulReminder;
        module->Change(settings);Sync();
    } catch(const std::exception& e) { Log(std::string("SOUL_WIDGET_CLICK_ERROR ")+e.what()); }
}
}
void InstallSoulWidget() {
    if(installed)return;
    serviceClass=api.Class("Core.dll","","gml");
    managerClass=api.Class("Core.dll","Core.UILogic.Common","WidgetManager");
    toggleClass=api.Class("Core.dll",Figma,"DofusToggleButtonCustom");
    bindingClass=api.Class("Core.dll","Core.UILogic.Bindings","WidgetManagerBinding");
    menuToggle=api.FindVerified(bindingClass,"get_tg_modules",0,0x154D420);
    getSize=api.FindVerified(toggleClass,"get_toggleSize",0,0x1702100);
    tooltipClass=api.Class("Core.dll","Core.UILogic.Components.Tooltips","TooltipManipulator");
    tooltipCtor=api.FindVerified(tooltipClass,".ctor",6,0x15DFAE0);
    hideTooltip=api.FindVerified(api.Class("Core.dll","Core.UILogic.Components.Tooltips","BaseTooltipManipulator"),"HideTooltip",0,0x15CD030);
    auto extensions=api.Class("UnityEngine.UIElementsModule.dll","UnityEngine.UIElements","VisualElementExtensions");
    addManipulator=api.FindVerified(extensions,"AddManipulator",2,0x56755F0);
    removeManipulator=api.FindVerified(extensions,"RemoveManipulator",2,0x5676A90);
    api.FindVerified(api.Class("UnityEngine.UIElementsModule.dll","UnityEngine.UIElements","VisualElement"),"PlaceBehind",1,0x55E69D0);
    const std::pair<const char*,uintptr_t> calls[]={
        {"set_iconValue",0x1716690},{"set_mainStyle",0x1716C40},{"set_mainColor",0x1716B70},
        {"set_toggleSize",0x1716DB0},{"SetValueWithoutNotify",0x1714F40}};
    api.FindVerified(toggleClass,".ctor",0,0x1715D00);
    for(auto [name,rva]:calls)api.FindVerified(toggleClass,name,1,rva);
    AddHook(api.Find(toggleClass,"Toggle",0),0x1715030,reinterpret_cast<void*>(&ToggleHook),reinterpret_cast<void**>(&originalToggle));
    installed=true;
}
void RestoreSoulWidget() {
    removing=true;
    if(tooltip.get())try {
        api.Invoke(hideTooltip,tooltip.get());
        api.Invoke(removeManipulator,nullptr,{button.get(),tooltip.get()});
        tooltip.reset();
    }catch(const std::exception& e){Log(std::string("SOUL_WIDGET_TOOLTIP_CLEANUP_ERROR ")+e.what());return;}
    if(button.get())try {api.Call(button.get(),"RemoveFromHierarchy");}
    catch(const std::exception& e){Log(std::string("SOUL_WIDGET_CLEANUP_ERROR ")+e.what());return;}
    button.reset();parent.reset();manager.reset();service.reset();compositor.reset();
    menu.reset();tooltipService.reset();nextRefresh=0;removing=false;
}
bool SoulWidgetCleanupPending(){return button.get()!=nullptr||tooltip.get()!=nullptr;}
void TickSoulWidget() {
    try {
        if(!Available()) {RestoreSoulWidget();return;}
        if(button.get()) {
            if(Current()) {
                Sync();
                if(GetTickCount64()>=nextRefresh){SyncLayout();nextRefresh=GetTickCount64()+600;}
                return;
            }
            RestoreSoulWidget();
            if(button.get())return;
        }
        if(GetTickCount64()<nextRefresh)return;
        nextRefresh=GetTickCount64()+600;
        Ref candidate(module->FindService("frm"));
        if(!Is(candidate.get(),serviceClass)){Missing("widget-service-unavailable",candidate.get());return;}
        Ref widget(api.Field(candidate.get(),"<dzqi>k__BackingField"));
        if(!Is(widget.get(),managerClass)){Missing("widget-manager-unavailable",widget.get());return;}
        Ref binding(api.Field(widget.get(),"dzyi"));
        if(!binding.get()||api.object_get_class(binding.get())!=bindingClass){Missing("widget-binding-unavailable",binding.get());return;}
        Ref anchor(api.Invoke(menuToggle,api.object_unbox(binding.get())));
        if(!Is(anchor.get(),toggleClass)){Missing("menu-anchor-unavailable",anchor.get());return;}
        if(!api.Call(anchor.get(),"get_panel")){Missing("menu-anchor-detached",anchor.get());return;}
        Ref target(api.Call(anchor.get(),"get_parent")),tips(module->FindService("fou"));
        if(!target.get()||!tips.get()){Missing("menu-parent-or-tooltip-unavailable");return;}
        int index=api.Value<int>(api.Call(target.get(),"IndexOf",{anchor.get()}));if(index<0){Missing("menu-anchor-not-in-parent");return;}
        button.reset(api.New(toggleClass));parent.reset(target.get());manager.reset(widget.get());service.reset(candidate.get());compositor.reset(module->compositor.get());
        menu.reset(anchor.get());tooltipService.reset(tips.get());
        Ref name(api.string_new("dofus_miniatures_soul_toggle")),tip(api.string_new("Capture d’âme\nAfficher ou masquer le panneau dans les combats capturables."));
        api.Call(button.get(),"set_name",{name.get()});
        // Same component, actual face dimensions and theme as the native Menu toggle.
        int icon=128,style=2,color=0;
        api.Call(button.get(),"set_iconValue",{&icon});api.Call(button.get(),"set_mainStyle",{&style});
        api.Call(button.get(),"set_mainColor",{&color});
        api.Call(parent.get(),"Insert",{&index,button.get()});SyncLayout();
        // Same native text-tooltip constructor, placement and hover lifecycle
        // as WidgetManager.InitTooltips; VisualElement.tooltip alone is unused by Dofus.
        tooltip.reset(api.object_new(tooltipClass));
        int origin=1,source=7,behavior=1;bool front=false;
        api.Invoke(tooltipCtor,tooltip.get(),{tips.get(),tip.get(),&origin,&source,&behavior,&front});
        api.Invoke(addManipulator,nullptr,{button.get(),tooltip.get()});
        Sync();reported=false;
        Report("attached before=Menu size="+std::to_string(api.Value<int>(api.Invoke(getSize,button.get()))));
    } catch(const std::exception& e) {
        if(!reported)Log(std::string("SOUL_WIDGET_ERROR ")+e.what());reported=true;
        try{RestoreSoulWidget();}catch(...){}
        nextRefresh=GetTickCount64()+2000;
    }
}
