#pragma once
#include "soul-panel.hpp"
#include "soul-item-view.hpp"
#include "module.hpp"
#include "features.hpp"
#include "options-ui.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <iostream>
#include <map>
#include <memory>
#include <unordered_map>
#include <vector>

// Offline native-contract fixture. No client binary is loaded or process opened.
namespace fake {
bool treasureFixture = false;
bool notebookFixture = false;
using namespace managed;
struct Class { std::string name; };
struct MethodInfo { Class* type; std::string name; int count; };
struct Object {
    Class* type = nullptr;
    std::map<std::string, Obj> fields;
    std::map<std::string, float> dimensions;
    std::map<std::string, int> enums;
    std::vector<Obj> children;
    std::array<std::byte, 32> value{};
    std::string text, name;
    bool active = false, enabled = true, movable = false, modal = false, hasBeenDragged = false, snapGroup = false, closeOnSelection = false;
    int id = 0; // ListElementBasic.id (dropdown items)
    std::map<std::string,float> delayedResolved;
};
std::map<std::string, std::unique_ptr<Class>> classes;
std::vector<std::unique_ptr<Object>> objects;
std::vector<std::unique_ptr<MethodInfo>> methods;
std::unordered_map<GCHandle, Obj> handles;
GCHandle nextHandle = 0xDCA000000000ULL;
Obj nativeRoot = nullptr, capture = nullptr, latestWindow = nullptr, latestAction = nullptr;
Obj failOwner = nullptr;
Obj dependencies = nullptr, input = nullptr, contextService = nullptr;
int hides = 0, nativePopulates = 0;
Obj lastMenuRoot = nullptr;
int changeFailures = 0; // Module::Change: settings assigned, then the save throws (module.cpp order).
int snapCleanups = 0, removedInputs = 0, initFailures = 0;
std::string failMethod;
bool globalDragging = false;
Obj itemSlot = nullptr, itemBound = nullptr; bool bindSuccess = true;
Obj spellSlot = nullptr, spellBound = nullptr; bool spellEnabled = false;
int equips = 0, saves = 0, savedX = 0, savedY = 0, nativeActions = 0, nativeUps = 0, globalWrites = 0;
int buttonUps = 0, nativeSubMenus = 0, nativeAllHeaders = 0;
Obj widgetService = nullptr;
uint64_t savedGeneration = 0;
int layoutSaves = 0;
using ActionFn = void(*)(Obj, Method);
using DragFn = void(*)(Obj, Obj, Method);
struct Vec2 { float x, y; };
using UpFn = void(*)(Obj, Obj, Vec2, int, Method);
using SubMenuFn = Obj(*)(Obj, Method);
using AllFn = void(*)(Obj, bool, Method);
UpFn clickHook = nullptr; SubMenuFn subMenuHook = nullptr; AllFn allHeadersHook = nullptr;
using FlagFn = void(*)(bool, Method);
ActionFn actionHook = nullptr;
ActionFn notifyHook = nullptr;
DragFn downHook = nullptr, upHook = nullptr, populateHook = nullptr;
DragFn modulesHook = nullptr;
FlagFn flagHook = nullptr;
std::vector<std::string> logs;
void Check(bool condition, const char* message) { if (!condition) throw std::runtime_error(message); }
Object& Get(Obj value) { Check(value != nullptr, "Null fake object"); return *static_cast<Object*>(value); }
Class* Type(const std::string& name) {
    auto& entry = classes[name];
    if (!entry) entry = std::make_unique<Class>(Class{name});
    return entry.get();
}
Obj Make(const std::string& type) {
    auto value = std::make_unique<Object>(); value->type = Type(type);
    auto result = value.get(); objects.push_back(std::move(value)); return result;
}
template<class T> Obj Box(const T& value) {
    Obj box = Make("Box"); std::memcpy(Get(box).value.data(), &value, sizeof(T)); return box;
}
Obj String(const std::string& value) { auto result = Make("String"); Get(result).text = value; return result; }
Obj Element(const std::string& type) {
    const auto result = Make(type);
    auto& object = Get(result);
    object.dimensions = {{"left",0.0f},{"top",0.0f},{"width",320.0f},{"height",210.0f}};
    object.fields["style"] = Make("InlineStyleAccess");
    Get(object.fields["style"]).fields["owner"] = result;
    return result;
}
void Add(Obj parent, Obj child) {
    if (Get(child).type->name == "MagneticDragWindow")
        Check(Get(child).enums["initialized"]==1,"Native magnetic widget must initialize before attachment");
    Check(!Get(child).fields["parent"], "Already attached");
    Get(parent).children.push_back(child); Get(child).fields["parent"] = parent;
}
void Detach(Obj child) {
    if (auto parent = Get(child).fields["parent"]) {
        auto& children = Get(parent).children;
        children.erase(std::remove(children.begin(), children.end(), child), children.end());
    }
    Get(child).fields["parent"] = nullptr;
}
Obj Panel(Obj element) {
    if (Get(element).fields["panel"]) return Get(element).fields["panel"];
    auto parent = Get(element).fields["parent"];
    return parent ? Panel(parent) : nullptr;
}
bool Enabled(Obj element) {
    if (!Get(element).enabled) return false;
    auto parent = Get(element).fields["parent"];
    return !parent || Enabled(parent);
}
void DragFlag(bool value, Method) { globalDragging = value; ++globalWrites; }
void OriginalAction(Obj, Method) { ++nativeActions; }
void OriginalButtonUp(Obj, Obj, Vec2, int, Method) { ++buttonUps; }
Obj OriginalSubMenu(Obj, Method) { ++nativeSubMenus; auto menu=Make("DofusContextualMenu"); Get(menu).fields["menuItems"]=Make("List"); return menu; }
void OriginalAllHeaders(Obj, bool, Method) { ++nativeAllHeaders; }
// The game's button component carries its own ExtendedClickable manipulator.
Obj NativeButton() { auto button=Element("DofusButtonCustom"); auto clickable=Make("ExtendedClickable"); Get(clickable).fields["target"]=button; Get(button).fields["m_clickable"]=clickable; return button; }
void OriginalNotify(Obj owner, Method) { Get(owner).hasBeenDragged=false; }
void OriginalDown(Obj owner, Obj, Method) {
    auto& dragger=Get(owner);
    if (dragger.active) return;
    flagHook(true,nullptr);dragger.active=true;dragger.hasBeenDragged=true;capture=dragger.fields.at("header");
}
void OriginalUp(Obj owner, Obj event, Method) {
    ++nativeUps;
    auto& dragger = Get(owner);
    if (dragger.active && capture == dragger.fields["header"] && Get(event).enabled) {
        flagHook(false, nullptr);
        capture = nullptr;
        if (!Get(dragger.fields["window"]).name.empty()) ++layoutSaves;
        notifyHook(owner,nullptr);
    }
    dragger.active = false;
}
void Equip() { ++equips; }
void OriginalPopulate(Obj, Obj, Method) { ++nativePopulates; }
void HideModule() {
    Check(lastMenuRoot && !Panel(lastMenuRoot), "Close the menu before saving/hiding the module");
    ++hides; module->settings.soulReminder=false; soul::Panel{}.Hide();
}
void Save(int x,int y,uint64_t generation) { ++saves; savedX=x; savedY=y; savedGeneration=generation; }
void Initialize() {
    api.object_get_class = [](Obj o)->Klass { return Get(o).type; };
    api.class_is_valuetype = [](Klass k) { const auto& n=static_cast<Class*>(k)->name; return n=="Box" || n=="Length" || n=="Color" || n.starts_with("Style"); };
    api.object_new = [](Klass k)->Obj { return Make(static_cast<Class*>(k)->name); };
    api.class_from_type = [](const void* t)->Klass { return const_cast<void*>(t); };
    api.method_get_param = [](Method m,uint32_t index)->const void* {
        const auto& name=static_cast<const MethodInfo*>(m)->name;
        Check((name=="AppendDefaultItem"&&(index==7||index==10))||(name=="AppendRadioButtonItem"&&index==4),"Menu value argument metadata");
        return Type("Box");
    };
    api.object_unbox = [](Obj o)->void* { return Get(o).value.data(); };
    api.string_new = [](const char* text)->Obj { return String(text); };
    api.gchandle_new = [](Obj value,bool)->GCHandle { auto h=++nextHandle; handles[h]=value; return h; };
    api.gchandle_get_target = [](GCHandle h)->Obj { return handles.at(h); };
    api.gchandle_free = [](GCHandle h) { Check(handles.erase(h)==1,"Invalid GC release"); };
}
Obj Invoke(Method method,Obj self,std::initializer_list<void*> arguments) {
    const auto& m=*static_cast<const MethodInfo*>(method);
    Check(static_cast<int>(arguments.size())==m.count,"Wrong managed argument count");
    const std::vector<void*> args(arguments);
    if (!self) {
        if(m.type->name=="VisualElementExtensions") {
            Check(args.size()==2,"Manipulator extension argc");
            if(args[0]==failOwner && m.name==failMethod) {failOwner=nullptr;throw std::runtime_error("Injected manipulator cleanup failure");}
            if(m.name=="AddManipulator") {
                Get(args[0]).fields["manipulator"]=args[1];Get(args[1]).fields["target"]=args[0];return nullptr;
            }
            Check(m.name=="RemoveManipulator"&&Get(args[1]).fields["target"]==args[0],"Remove exact owned manipulator");
            Get(args[0]).fields["manipulator"]=nullptr;Get(args[1]).fields["target"]=nullptr;return nullptr;
        }
        if(m.type->name=="pq" && m.name=="pbs") return nativeRoot;
        if(m.type->name=="MouseCaptureController" && m.name=="HasMouseCapture") return Box(capture==args.at(0));
        if(m.type->name=="MouseCaptureController" && m.name=="ReleaseMouse") {
            Check(capture==args.at(0),"Attempted to release foreign capture"); capture=nullptr; return nullptr;
        }
        throw std::runtime_error("Unexpected static call "+m.type->name+"."+m.name);
    }
    if(self==failOwner && m.name==failMethod) { failOwner=nullptr; throw std::runtime_error("Injected UI failure"); }
    auto& obj=Get(self);
    if(m.name=="get_menu"||m.name=="get_menuItems"||m.name=="get_m_callback"||m.name=="get_itemData"||m.name=="get_menuData") return obj.fields.at(m.name.substr(4));
    if(m.name=="get_disabled") return Box(!obj.enabled);
    if(m.name=="AppendTitle"||m.name=="AppendSeparator") {
        auto row=Make(m.name=="AppendTitle"?"MenuTitle":"MenuSeparator");
        if(m.name=="AppendTitle") {Check(args.size()==1&&!Get(args[0]).text.empty(),"Native module title before hide action");Get(row).text=Get(args[0]).text;}
        else Check(args.empty(),"Native separator has no arguments");
        Get(obj.fields.at("menuItems")).children.push_back(row);return nullptr;
    }
    if(m.name=="AppendDefaultItem") {
        Check(args.size()==13&&Get(args[0]).text=="Masquer le module"&&!args[1],"Hide menu must use null native fallback and localized title");
        Check(!*static_cast<bool*>(args[2])&&*static_cast<bool*>(args[3])&&*static_cast<int*>(args[9])==137,"Enabled native closing row with eye-slash icon");
        Check(!Get(args[4]).text.empty(),"Explain how to show the module again");
        auto item=Make("MenuItem"),callback=Make("MenuCallback");Get(item).fields["m_callback"]=callback;
        Get(obj.fields.at("menuItems")).children.push_back(item);return nullptr;
    }
    if(m.name=="AppendRadioButtonItem") {
        Check(args.size()==12&&!args[1]&&*static_cast<int*>(args[3])==0&&!*static_cast<bool*>(args[5])&&*static_cast<bool*>(args[7])&&!*static_cast<bool*>(args[11])&&!Get(args[8]).text.empty(),
            "Layout rows: one native radio group, null callback, no switch-off, closing on select, explained");
        auto item=Make("MenuItem"),callback=Make("MenuCallback");Get(item).text=Get(args[0]).text;
        Get(item).enums["isOn"]=*static_cast<bool*>(args[2])?1:0;Get(item).enabled=!*static_cast<bool*>(args[6]);Get(item).fields["m_callback"]=callback;
        Get(obj.fields.at("menuItems")).children.push_back(item);return nullptr;
    }
    if(m.name=="RemoveMenu") {Check(!*static_cast<bool*>(args[0]),"Close native menu immediately");Detach(self);return nullptr;}
    if(m.name=="set_headerIconRight") {Check(*static_cast<int*>(args[0])==202,"Native three dots");obj.enums[m.name]=202;return nullptr;}
    if(m.name=="set_showRightButton") {Check(*static_cast<bool*>(args[0]),"Keep three dots visible");obj.enums[m.name]=1;return nullptr;}
    if(m.name=="bfri") return obj.fields.at("cached");
    if(m.name=="get_dofusOptionsContainer") return obj.fields.at("options");
    if(m.name=="get_property") return Box(obj.enabled);
    if(m.name=="bfre") return input;
    if(m.name=="ydf"||m.name=="ydh"||m.name=="ydj"||m.name=="ydl") return obj.fields.at(m.name);
    if(m.name=="get_Count") return Box(static_cast<int>(obj.children.size()));
    if(m.name=="get_hierarchy") return self;
    if(m.name=="get_childCount") return Box(static_cast<int>(obj.children.size()));
    if(m.name=="get_name") return String(obj.name);
    if(m.name=="get_Item") {
        if(obj.type->name=="Dictionary") {
            Check(args[0]==obj.fields.at("header"),"Header dragger dictionary lookup must use native HeaderWidget");
            return obj.fields.at("dragger");
        }
        return obj.children.at(*static_cast<int*>(args[0]));
    }
    if(m.name=="get_Target") return obj.fields.at("target");
    if(m.name=="UnSubscribe") {
        auto& callbacks=Get(obj.fields.at("m_input")).children;
        auto found=std::find(callbacks.begin(),callbacks.end(),args[0]);
        Check(found!=callbacks.end(),"Unsubscribe must receive actual existing delegate");
        Check(Get(args[0]).enums["owned"]==1,"Never unsubscribe a foreign input callback");
        callbacks.erase(found); ++removedInputs;return nullptr;
    }
    if(m.name=="Init") {
        Check(args[0]==dependencies&&!Panel(self),"Init must use native dependencies before attachment");
        for(const auto* key:{"ydf","ydh","ydj","ydl"}) {
            auto event=Get(input).fields.at(key);
            for(const auto* field:{"m_elementDraggers","m_resizerList"})for(auto target:Get(obj.fields.at(field)).children) {
                auto callback=Make("Action");Get(callback).fields["target"]=target;Get(callback).enums["owned"]=1;
                Get(Get(event).fields.at("m_input")).children.push_back(callback);
            }
            if(initFailures>0){--initFailures;throw std::runtime_error("Partial native Init failure");}
        }
        obj.enums["initialized"]=1;return nullptr;
    }
    if(m.name=="RemoveSnapPreview") { ++snapCleanups;return nullptr; }
    if(m.name=="get_style") return obj.fields.at("style");
    if(m.name=="get_resolvedStyle") { auto r=Make("ResolvedStyleAccess");Get(r).fields["owner"]=self;return r; }
    if(obj.type->name=="ResolvedStyleAccess") {
        auto& owner=Get(obj.fields.at("owner"));
        Check(m.name.starts_with("get_"),"Resolved-style getter must use concrete public method");
        auto p=m.name.substr(4);
        if(p=="display") return Box(owner.enums[p]);
        return Box(owner.delayedResolved.contains(p)?owner.delayedResolved.at(p):owner.dimensions.at(p));
    }
    if(obj.type->name=="InlineStyleAccess") {
        auto& owner=Get(obj.fields.at("owner"));
        const std::string prefix="UnityEngine.UIElements.IStyle.";
        Check(m.name.starts_with(prefix),"Inline styles require explicit IStyle method");
        auto operation=m.name.substr(prefix.size()); auto property=operation.substr(4);
        const bool enumStyle=property=="position"||property=="whiteSpace"||property=="display"||property=="flexDirection"||property=="alignItems"||property=="justifyContent"||property=="unityTextAlign"||property=="flexWrap"||property=="unityFontStyleAndWeight";
        if(operation.starts_with("get_")) {
            auto box=Make(property=="color"?"StyleColor":enumStyle?"StyleEnum":property=="flexGrow"||property=="flexShrink"?"StyleFloat":"StyleLength");
            if(enumStyle) std::memcpy(Get(box).value.data(),&owner.enums[property],sizeof(int));
            else std::memcpy(Get(box).value.data(),&owner.dimensions[property],sizeof(float));
            return box;
        }
        Check(operation.starts_with("set_"),"Unexpected style method");
        if(property=="color") { float rgb[4];std::memcpy(rgb,args.at(0),sizeof(rgb));owner.dimensions["red"]=rgb[0];owner.dimensions["green"]=rgb[1]; }
        else if(enumStyle) owner.enums[property]=*static_cast<int*>(args.at(0));
        else owner.dimensions[property]=*static_cast<float*>(args.at(0));
        return nullptr;
    }
    if(obj.type->name.starts_with("Style") && m.name=="set_value") {
        const auto bytes=obj.type->name=="StyleColor"?16:obj.type->name=="StyleLength"?8:4;
        std::memcpy(obj.value.data(),args.at(0),bytes); return nullptr;
    }
    if(obj.type->name=="StyleLength"&&m.name=="get_keyword") return Box(0);
    if(obj.type->name=="StyleLength"&&m.name=="get_value") {
        auto length=Make("Length");std::memcpy(Get(length).value.data(),obj.value.data(),8);return length;
    }
    if(m.name=="set_isSingleLine"||m.name=="set_isMultiline"||m.name=="set_enableRichText") {obj.enums[m.name]=*static_cast<bool*>(args[0]);return nullptr;}
    if(m.name=="get_text") return String(obj.text);
    if(m.name=="set_tooltip"||m.name=="set_placeholderText") {obj.fields[m.name]=args[0];return nullptr;}
    if(m.name=="set_maxLength") {obj.enums[m.name]=*static_cast<int*>(args[0]);return nullptr;}
    if(m.name=="BringToFront") {auto parent=obj.fields["parent"];Detach(self);Add(parent,self);return nullptr;}
    if(m.name=="set_text") { obj.text=Get(args.at(0)).text;return nullptr; }
    if(m.name=="set_name") { obj.name=Get(args.at(0)).text;return nullptr; }
    if(m.name=="set_headerTitle") { obj.text=Get(args.at(0)).text;return nullptr; }
    if(m.name=="set_headerIcon") { const int icon=*static_cast<int*>(args.at(0));Check(icon==(notebookFixture?225:treasureFixture?317:128),"Use the native panel icon in the window title");obj.enums[m.name]=icon;return nullptr; }
    if(m.name=="set_headerShowCloseButton") { Check(*static_cast<bool*>(args[0])==(notebookFixture||treasureFixture),"Only the notebook and treasure windows show the native close button");obj.enums[m.name]=*static_cast<bool*>(args[0])?1:0;return nullptr; }
    if(m.name=="set_verticalResizable"||m.name=="set_horizontalResizable"||m.name=="set_cornerResizable") {
        // The soul-stone widget stays compact; the notebook and treasure windows resize like the game's own windows.
        Check(*static_cast<bool*>(args[0])==(notebookFixture||treasureFixture),"Only the notebook and treasure windows expose the native resizers");
        obj.enums[m.name]=*static_cast<bool*>(args[0])?1:0;return nullptr;
    }
    if(m.name=="set_hasBackground") { Check(*static_cast<bool*>(args[0]),"Use native themed background");return nullptr; }
    if(m.name=="set_headerSize") { Check(*static_cast<int*>(args[0])==1,"Native large header retains its readable 24px title");return nullptr; }
    if(m.name=="set_dragPosition") { Check(*static_cast<int*>(args[0])==0,"Cleanup disables native drag handlers");obj.movable=false;return nullptr; }
    if(m.name=="set_isUILocked") {obj.movable=!*static_cast<bool*>(args[0]);return nullptr;}
    if(m.name=="set_listElementStyle") { const int style=*static_cast<int*>(args.at(0));Check(style==0||style==1,"Choice or action list style");obj.enums[m.name]=style;return nullptr; }
    if(m.name=="get_headerWidget") return obj.fields.at("header");
    if(m.name=="get_contentContainer") return obj.fields.at("content");
    if(m.name=="get_panel") return Panel(self);
    if(m.name=="get_parent") return obj.fields["parent"];
    if(m.name=="get_slotType") return Box(obj.enums["slotType"]);
    if(m.name=="get_slotSize") return Box(obj.enums["slotSize"]);
    if(m.name=="set_slotSize") {obj.enums["slotSize"]=*static_cast<int*>(args[0]);return nullptr;}
    if(m.name=="get_worldBound") {struct Rect{float x,y,w,h;};return Box(Rect{0,0,obj.dimensions["width"],obj.dimensions["height"]});}
    if(m.name=="get_enabledInHierarchy") return Box(Enabled(self));
    if(m.name=="SetEnabled") { obj.enabled=*static_cast<bool*>(args.at(0));return nullptr; }
    if(m.name=="Add") { Add(self,args.at(0));return nullptr; }
    if(m.name=="RemoveFromHierarchy") { Detach(self);return nullptr; }
    if(m.name=="set_icon"||m.name=="set_mainStyle"||m.name=="set_size"||m.name=="set_status"||m.name=="set_fontCase"||m.name=="set_componentStyle"||m.name=="set_orientation") {obj.enums[m.name]=*static_cast<int*>(args.at(0));return nullptr;}
    if(m.name=="set_isActivated"||m.name=="set_canBeOpened"||m.name=="SetHeaderSize") {obj.enums[m.name]=*static_cast<bool*>(args.at(0))?1:0;return nullptr;}
    // Native SectionHeader.set_isOpen (0x17462F0) keeps the previous state unless canBeOpened is on; both are off after the constructor.
    if(m.name=="set_isOpen") {if(obj.enums["set_canBeOpened"]==1)obj.enums[m.name]=*static_cast<bool*>(args.at(0))?1:0;return nullptr;}
    if(m.name=="set_inlineText"||m.name=="set_title"||m.name=="set_choiceText") {obj.text=Get(args.at(0)).text;return nullptr;}
    // Native Dropdown: items with an id, selection without notification, read back by the owner.
    if(m.name=="set_dropdownType"||m.name=="set_dropdownSize") {obj.enums[m.name]=*static_cast<int*>(args.at(0));return nullptr;}
    if(m.name=="AddListElementBasic") {Check(args.size()==6&&!args.at(1),"Native dropdown item without callback");auto item=Element("ListElementBasic");Get(item).text=Get(args.at(0)).text;Get(item).id=*static_cast<int*>(args.at(5));Add(obj.fields.at("list"),item);return nullptr;}
    if(m.name=="SelectItemIdWithoutNotify") {const int id=*static_cast<int*>(args.at(0));for(auto item:Get(obj.fields.at("list")).children)if(Get(item).id==id){obj.fields["selected"]=item;return nullptr;}throw std::runtime_error("Dropdown item id unknown");}
    if(m.name=="get_selectedElement") {auto found=obj.fields.find("selected");return found==obj.fields.end()?nullptr:found->second;}
    if(m.name=="AddSubContent") {Add(obj.fields.at("content"),args.at(0));return nullptr;}
    if(m.name=="get_isOn") return Box(obj.enums["isOn"]!=0);
    if(m.name=="SetValueWithoutNotify") {obj.enums["isOn"]=*static_cast<bool*>(args.at(0))?1:0;return nullptr;}
    if(m.name=="get_active"||m.name=="ContainsPointer") return Box(true);
    if(m.name=="get_target") return obj.fields.at("target");
    throw std::runtime_error("Unexpected instance call "+obj.type->name+"."+m.name);
}
}

namespace managed {
Api api{};
uintptr_t gameBase=0;
Klass Api::Class(const char*,const char*,const char* name) const {return fake::Type(name);}
Method Api::Find(Klass klass,const char* name,int count) const {
    auto m=std::make_unique<fake::MethodInfo>(fake::MethodInfo{static_cast<fake::Class*>(klass),name,count});
    auto result=m.get();fake::methods.push_back(std::move(m));return result;
}
Method Api::FindVerified(Klass klass,const char* name,int count,uintptr_t rva) const {
    auto method=Find(klass,name,count);Native(method,rva);return method;
}
Obj Api::Invoke(Method m,Obj self,std::initializer_list<void*> args) const {return fake::Invoke(m,self,args);}
Obj Api::Call(Obj self,const char* name,std::initializer_list<void*> args) const {return Invoke(Find(object_get_class(self),name,static_cast<int>(args.size())),self,args);}
Obj Api::New(Klass klass,std::initializer_list<void*> arguments) const {
    using namespace fake;
    const auto& name=static_cast<fake::Class*>(klass)->name;
    const std::vector<void*> args(arguments);
    if(name=="Length") { Check(args.size()==2,"Length argc");auto box=Make(name);std::memcpy(Get(box).value.data(),args[0],4);std::memcpy(Get(box).value.data()+4,args[1],4);return box; }
    if(name=="Color") { Check(args.size()==4,"Color argc");auto box=Make(name);for(size_t i=0;i<4;++i)std::memcpy(Get(box).value.data()+i*4,args[i],4);return box; }
    if(name=="ScrollView") {auto scroll=Element(name),content=Element("ScrollContent");Add(scroll,content);Get(scroll).fields["content"]=content;return scroll;}
    if(name=="Label"||name=="ListElementBasic"||name=="VisualElement"||name=="TextInput"||name=="DofusTab"||name=="Divider"||name=="DofusButtonCustom") {Check(args.empty(),"Native element ctor argc");auto obj=name=="DofusButtonCustom"?NativeButton():Element(name);if(name=="ListElementBasic"){latestAction=obj;auto label=Element("DofusLabel");Get(obj).fields["m_textLabel"]=label;Add(obj,label);}return obj;}
    if(name=="SectionHeader") {Check(args.size()==5&&*static_cast<bool*>(args[0])&&*static_cast<bool*>(args[1])&&!*static_cast<bool*>(args[2])&&*static_cast<bool*>(args[3])&&!*static_cast<bool*>(args[4]),"Native section header ctor ABI");auto section=Element(name);auto content=Element("SectionContent");Add(section,content);Get(section).fields["content"]=content;return section;}
    if(name=="Dropdown") {Check(args.empty(),"Native dropdown ctor argc");auto dropdown=Element(name);auto list=Element("DropdownList");Add(dropdown,list);Get(dropdown).fields["list"]=list;return dropdown;}
    if(name=="DofusContextualMenuManipulator") {
        Check(args.size()==4&&args[0]==contextService&&!args[1]&&*static_cast<int*>(args[2])==0&&!*static_cast<bool*>(args[3]),"Native left-click menu constructor with null fallback");
        return Make(name);
    }
    Check(name=="MagneticDragWindow"&&args.empty(),"Native magnetic widget ctor ABI");
    auto window=Element(name);latestWindow=window;Get(window).name="native-default";Get(window).movable=true;
    auto header=Element("HeaderWidget"),content=Element("Content"),dragger=Make("ElementDraggerWithBounds");
    auto rightButton=NativeButton(),closeButton=NativeButton();Get(header).fields["rightButton"]=rightButton;Add(header,rightButton);Get(header).fields["closeButton"]=closeButton;Add(header,closeButton);
    Get(window).fields["header"]=header;Get(window).fields["content"]=content;Get(window).fields["m_movableManipulator"]=dragger;
    Get(dragger).fields["header"]=header;Get(dragger).fields["window"]=window;Add(window,header);Add(window,content);
    auto dict=Make("Dictionary");Get(dict).fields["header"]=header;Get(dict).fields["dragger"]=dragger;
    Get(window).fields["m_registeredElementDraggers"]=dict;
    auto draggers=Make("List"),resizers=Make("List");
    Get(draggers).children={dragger,Make("ElementDraggerWithBounds")};
    Get(resizers).children={Make("ElementResizer"),Make("ElementResizer")};
    Get(window).fields["m_elementDraggers"]=draggers;Get(window).fields["m_resizerList"]=resizers;
    return window;
}
Obj Api::Field(Obj self,const char* name) const {return fake::Get(self).fields.at(name);}
std::string Api::String(Obj self) const {return self?fake::Get(self).text:std::string{};}
void* Api::FieldAddress(Obj self,const char* name) const {
    if(std::string(name)=="m_active")return &fake::Get(self).active;
    if(std::string(name)=="m_hasBeenDragged")return &fake::Get(self).hasBeenDragged;
    if(std::string(name)=="m_isSnapGroup")return &fake::Get(self).snapGroup;
    if(std::string(name)=="closeOnSelection")return &fake::Get(self).closeOnSelection;
    if(std::string(name)=="id")return &fake::Get(self).id;
    throw std::runtime_error("Unexpected native field");
}
void* Api::NativeAt(uintptr_t rva) const {fake::Check(rva==0x166DF30,"Unexpected native helper");return reinterpret_cast<void*>(&fake::DragFlag);}
void* Api::Native(Method method,uintptr_t rva) const {
    const auto& m=*static_cast<const fake::MethodInfo*>(method);
    if(rva==0x11E7EC0&&m.name=="get_menu"&&m.count==0)return reinterpret_cast<void*>(rva);
    const std::map<uintptr_t,std::pair<std::string,int>> expected={
        {0x17D83F0,{"AddPresetContextualMenuItems",1}},{0x16D63D0,{"AppendCheckboxItem",9}},{0x16D72F0,{"AppendRadioButtonItem",12}},
        {0x5742BD0,{".ctor",0}},{0x5742BE0,{"get_contentContainer",0}},
        {0x55E08E0,{"BringToFront",0}},{0x1593FB0,{"set_isSingleLine",1}},
        {0x178E150,{"set_isMultiline",1}},{0x564B1E0,{"set_enableRichText",1}},
        {0x178DC90,{".ctor",0}},{0x178DDA0,{"get_text",0}},{0x178EC50,{"set_text",1}},{0x178EAA0,{"set_placeholderText",1}},{0x178E9B0,{"set_maxLength",1}},
        {0x15BFAD0,{"set_headerIconRight",1}},{0x172DB70,{"set_showRightButton",1}},
        {0x16D0BB0,{".ctor",4}},{0x16D04D0,{"OnContextualMenuEvent",1}},
        {0x16D7F70,{"AppendTitle",1}},{0x16D76E0,{"AppendSeparator",0}},{0x16D6C60,{"AppendDefaultItem",13}},{0x56755F0,{"AddManipulator",2}},{0x5676A90,{"RemoveManipulator",2}},
        {0x1597E90,{"get_slotType",0}},{0x165EC30,{"get_slotSize",0}},
        {0x2E93CC0,{"pbs",0}}, {0x15B2DE0,{".ctor",0}}, {0x15B2010,{"Init",1}},
        {0x15B3440,{"set_hasBackground",1}}, {0x15BFC00,{"set_headerTitle",1}}, {0x15BFB10,{"set_headerIcon",1}},
        {0x15BFB80,{"set_headerSize",1}}, {0x15BFB50,{"set_headerShowCloseButton",1}},
        {0x15BFCF0,{"set_verticalResizable",1}}, {0x15BFC40,{"set_horizontalResizable",1}},
        {0x15BF930,{"set_cornerResizable",1}}, {0x15BF9D0,{"set_dragPosition",1}}, {0x15BFC60,{"set_isUILocked",1}},
        {0x1585330,{"get_headerWidget",0}}, {0x1585350,{"get_contentContainer",0}},
        {0x68D2C0,{"bfre",0}}, {0x682030,{"bfri",0}}, {0x69A450,{"ydf",0}},
        {0x741A60,{"ydh",0}}, {0x11E7EC0,{"ydj",0}}, {0x11E7E80,{"ydl",0}},
        {0x68D2D0,{"get_Target",0}}, {0x20FF620,{"UnSubscribe",1}}, {0x16714A0,{"RemoveSnapPreview",0}},
        {0x1734630,{".ctor",0}}, {0x1734700,{"set_listElementStyle",1}},
        {0x57A9FD0,{"HasMouseCapture",1}}, {0x57AA140,{"ReleaseMouse",1}},
        {0x15B27B0,{"SetHeaderSize",1}},{0x17D9B00,{"CreateHeaderSubMenu",0}},{0x17DDDD0,{"SetAllHeaderState",1}},
        {0x172B6A0,{"ProcessUpEvent",3}},{0x752840,{"get_active",0}},{0x5590600,{"ContainsPointer",1}},{0x30D6960,{"get_target",0}},{0x55F5EF0,{"get_enabledInHierarchy",0}},
        {0x1712DC0,{".ctor",0}},{0x17136F0,{"set_text",1}},{0x1713550,{"set_size",1}},{0x1712FC0,{"set_fontCase",1}},{0x1712F50,{"get_isOn",0}},{0x1711970,{"SetValueWithoutNotify",1}},
        {0x170D6F0,{".ctor",0}},{0x170DFE0,{"set_icon",1}},{0x170E5F0,{"set_text",1}},{0x170E210,{"set_mainStyle",1}},{0x170E330,{"set_size",1}},{0x170E400,{"set_status",1}},
        {0x1745F00,{".ctor",5}},{0x17467B0,{"set_title",1}},{0x1746200,{"set_icon",1}},{0x17462F0,{"set_isOpen",1}},{0x1746090,{"set_canBeOpened",1}},{0x1744360,{"AddSubContent",1}},
        {0x170BC10,{".ctor",0}},{0x170BDC0,{"set_componentStyle",1}},{0x170C160,{"set_orientation",1}},{0x170C0C0,{"set_inlineText",1}},
        {0x1733BD0,{"set_icon",1}},{0x1733BF0,{"set_isActivated",1}},
        {0x17282C0,{".ctor",0}},{0x1728730,{"set_dropdownType",1}},{0x1728620,{"set_dropdownSize",1}},{0x1724840,{"AddListElementBasic",6}},
        {0x17265B0,{"SelectItemIdWithoutNotify",1}},{0x155DB30,{"get_selectedElement",0}},{0x1728550,{"set_choiceText",1}}
    };
    const auto& contract=expected.at(rva);
    fake::Check(m.name==contract.first&&m.count==contract.second,"Native metadata contract mismatch");
    return reinterpret_cast<void*>(rva);
}
}
Module* module = nullptr;
Module::Module(std::filesystem::path): features(nullptr),ui(nullptr) {}
void Module::Change(Settings value,bool) {
    settings=std::move(value);
    if(fake::changeFailures>0){--fake::changeFailures;throw std::runtime_error("Cannot replace settings");}
}
managed::Obj Module::FindService(const char* name) const {
    if(std::string(name)=="fpz")return fake::contextService;
    if(std::string(name)=="fqp")return fake::Get(fake::nativeRoot).fields.at("layersService");
    if(std::string(name)=="frm")return fake::widgetService;
    fake::Check(std::string(name)=="fpt","Panel only requests native widget dependencies");return fake::dependencies;
}
void Log(const std::string& message) {fake::logs.push_back(message);}
void AddHook(managed::Method m,uintptr_t rva,void* replacement,void** original) {
    const auto& method=*static_cast<const fake::MethodInfo*>(m);
    if(rva==0x17D83F0) {fake::modulesHook=reinterpret_cast<fake::DragFn>(replacement);*original=reinterpret_cast<void*>(&fake::OriginalPopulate);return;}
    if(rva==0x16D04D0) {
        fake::Check(method.name=="OnContextualMenuEvent"&&method.count==1,"Native menu population ABI");
        fake::populateHook=reinterpret_cast<fake::DragFn>(replacement);*original=reinterpret_cast<void*>(&fake::OriginalPopulate);return;
    }
    if(rva==0x16702F0) {
        fake::Check(method.name=="NotifyDragCompleted"&&method.count==0,"Native magnetic notification ABI");
        fake::notifyHook=reinterpret_cast<fake::ActionFn>(replacement);*original=reinterpret_cast<void*>(&fake::OriginalNotify);return;
    }
    if(rva==0x1733850) {
        fake::Check(method.name=="<AssignClickAction>b__27_0"&&method.count==0,"Action hook ABI");
        fake::actionHook=reinterpret_cast<fake::ActionFn>(replacement);*original=reinterpret_cast<void*>(&fake::OriginalAction);return;
    }
    if(rva==0x1670470) {
        fake::Check(method.name=="OnMouseDown"&&method.count==1,"Drag-down hook ABI");
        fake::downHook=reinterpret_cast<fake::DragFn>(replacement);*original=reinterpret_cast<void*>(&fake::OriginalDown);return;
    }
    if(rva==0x172B6A0) {fake::Check(method.name=="ProcessUpEvent"&&method.count==3,"Native button click hook ABI");fake::clickHook=reinterpret_cast<fake::UpFn>(replacement);*original=reinterpret_cast<void*>(&fake::OriginalButtonUp);return;}
    if(rva==0x17D9B00) {fake::Check(method.name=="CreateHeaderSubMenu"&&method.count==0,"Header sub-menu hook ABI");fake::subMenuHook=reinterpret_cast<fake::SubMenuFn>(replacement);*original=reinterpret_cast<void*>(&fake::OriginalSubMenu);return;}
    if(rva==0x17DDDD0) {fake::Check(method.name=="SetAllHeaderState"&&method.count==1,"All headers hook ABI");fake::allHeadersHook=reinterpret_cast<fake::AllFn>(replacement);*original=reinterpret_cast<void*>(&fake::OriginalAllHeaders);return;}
    fake::Check(rva==0x16709C0&&method.name=="OnMouseUp"&&method.count==1,"Drag hook ABI");
    fake::upHook=reinterpret_cast<fake::DragFn>(replacement);*original=reinterpret_cast<void*>(&fake::OriginalUp);
}
void AddNativeHook(uintptr_t rva,void* replacement,void** original) {
    fake::Check(rva==0x166DF30,"Unexpected raw hook");
    fake::flagHook=reinterpret_cast<fake::FlagFn>(replacement);*original=reinterpret_cast<void*>(&fake::DragFlag);
}

namespace soul {
void InstallItemView() {}
managed::Obj CreateItemView(managed::Obj parent) { fake::itemSlot=fake::Element("Slot");fake::Add(parent,fake::itemSlot);return fake::itemSlot; }
bool UpdateItemView(managed::Obj item) { if(fake::bindSuccess)fake::itemBound=item;return fake::bindSuccess; }
void ReleaseItemView() { if(fake::itemSlot)fake::Detach(fake::itemSlot);fake::itemSlot=fake::itemBound=nullptr; }
}

