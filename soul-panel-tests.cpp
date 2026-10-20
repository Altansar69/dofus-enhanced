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
    bool active = false, enabled = true, movable = false, modal = false, hasBeenDragged = false, snapGroup = false;
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
int snapCleanups = 0, removedInputs = 0, initFailures = 0;
std::string failMethod;
bool globalDragging = false;
Obj itemSlot = nullptr, itemBound = nullptr; bool bindSuccess = true;
Obj spellSlot = nullptr, spellBound = nullptr; bool spellEnabled = false;
int equips = 0, saves = 0, savedX = 0, savedY = 0, nativeActions = 0, nativeUps = 0, globalWrites = 0;
uint64_t savedGeneration = 0;
int layoutSaves = 0;
using ActionFn = void(*)(Obj, Method);
using DragFn = void(*)(Obj, Obj, Method);
using FlagFn = void(*)(bool, Method);
ActionFn actionHook = nullptr;
ActionFn notifyHook = nullptr;
DragFn downHook = nullptr, upHook = nullptr, populateHook = nullptr;
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
        Check(static_cast<const MethodInfo*>(m)->name=="AppendDefaultItem"&&(index==7||index==10),"Menu value argument metadata");
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
        if(m.name=="AppendTitle") {Check(args.size()==1&&Get(args[0]).text=="Pierre d'âme","Native module title before hide action");Get(row).text=Get(args[0]).text;}
        else Check(args.empty(),"Native separator has no arguments");
        Get(obj.fields.at("menuItems")).children.push_back(row);return nullptr;
    }
    if(m.name=="AppendDefaultItem") {
        Check(args.size()==13&&Get(args[0]).text=="Masquer le module"&&!args[1],"Hide menu must use null native fallback and localized title");
        Check(!*static_cast<bool*>(args[2])&&*static_cast<bool*>(args[3])&&*static_cast<int*>(args[9])==137,"Enabled native closing row with eye-slash icon");
        Check(Get(args[4]).text.find("barre des modules")!=std::string::npos,"Explain how to show the module again");
        auto item=Make("MenuItem"),callback=Make("MenuCallback");Get(item).fields["m_callback"]=callback;
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
        const bool enumStyle=property=="position"||property=="whiteSpace"||property=="display"||property=="flexDirection";
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
    if(m.name=="set_text") { obj.text=Get(args.at(0)).text;return nullptr; }
    if(m.name=="set_name") { obj.name=Get(args.at(0)).text;return nullptr; }
    if(m.name=="set_headerTitle") { obj.text=Get(args.at(0)).text;return nullptr; }
    if(m.name=="set_headerIcon") { Check(*static_cast<int*>(args.at(0))==128,"Use the native Capture toolbar icon in the window title");obj.enums[m.name]=128;return nullptr; }
    if(m.name=="set_headerShowCloseButton"||m.name=="set_verticalResizable"||m.name=="set_horizontalResizable"||m.name=="set_cornerResizable") {
        Check(!*static_cast<bool*>(args[0]),"Native compact widget must not expose resize/close controls");obj.enums[m.name]=0;return nullptr;
    }
    if(m.name=="set_hasBackground") { Check(*static_cast<bool*>(args[0]),"Use native themed background");return nullptr; }
    if(m.name=="set_headerSize") { Check(*static_cast<int*>(args[0])==1,"Native large header retains its readable 24px title");return nullptr; }
    if(m.name=="set_dragPosition") { Check(*static_cast<int*>(args[0])==0,"Cleanup disables native drag handlers");obj.movable=false;return nullptr; }
    if(m.name=="set_isUILocked") {obj.movable=!*static_cast<bool*>(args[0]);return nullptr;}
    if(m.name=="set_listElementStyle") { Check(*static_cast<int*>(args.at(0))==1,"Action style is 1");return nullptr; }
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
    if(name=="Label"||name=="ListElementBasic"||name=="VisualElement") {Check(args.empty(),"Native element ctor argc");auto obj=Element(name);if(name=="ListElementBasic")latestAction=obj;return obj;}
    if(name=="DofusContextualMenuManipulator") {
        Check(args.size()==4&&args[0]==contextService&&!args[1]&&*static_cast<int*>(args[2])==0&&!*static_cast<bool*>(args[3]),"Native left-click menu constructor with null fallback");
        return Make(name);
    }
    Check(name=="MagneticDragWindow"&&args.empty(),"Native magnetic widget ctor ABI");
    auto window=Element(name);latestWindow=window;Get(window).name="native-default";Get(window).movable=true;
    auto header=Element("HeaderWidget"),content=Element("Content"),dragger=Make("ElementDraggerWithBounds");
    auto rightButton=Element("DofusButtonCustom");Get(header).fields["rightButton"]=rightButton;Add(header,rightButton);
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
    throw std::runtime_error("Unexpected native field");
}
void* Api::NativeAt(uintptr_t rva) const {fake::Check(rva==0x166DF30,"Unexpected native helper");return reinterpret_cast<void*>(&fake::DragFlag);}
void* Api::Native(Method method,uintptr_t rva) const {
    const auto& m=*static_cast<const fake::MethodInfo*>(method);
    if(rva==0x11E7EC0&&m.name=="get_menu"&&m.count==0)return reinterpret_cast<void*>(rva);
    const std::map<uintptr_t,std::pair<std::string,int>> expected={
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
        {0x57A9FD0,{"HasMouseCapture",1}}, {0x57AA140,{"ReleaseMouse",1}}
    };
    const auto& contract=expected.at(rva);
    fake::Check(m.name==contract.first&&m.count==contract.second,"Native metadata contract mismatch");
    return reinterpret_cast<void*>(rva);
}
}
Module* module = nullptr;
Module::Module(std::filesystem::path): features(nullptr),ui(nullptr) {}
managed::Obj Module::FindService(const char* name) const {
    if(std::string(name)=="fpz")return fake::contextService;
    if(std::string(name)=="fqp")return fake::Get(fake::nativeRoot).fields.at("layersService");
    fake::Check(std::string(name)=="fpt","Panel only requests native widget dependencies");return fake::dependencies;
}
void Log(const std::string& message) {fake::logs.push_back(message);}
void AddHook(managed::Method m,uintptr_t rva,void* replacement,void** original) {
    const auto& method=*static_cast<const fake::MethodInfo*>(m);
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

namespace fake {
struct Fixture {
    Module owner{std::filesystem::path{}};
    soul::Panel panel;
    soul::PanelView view{"Capacite requise : 150","Stock : 2","Equipee : aucune","Choisir une pierre",false,true};
    Obj root=nullptr,nativeChild=nullptr;
    Fixture() {
        panel.Hide();Check(handles.empty(),"Previous fixture leaked refs");
        module=&owner;owner.cached.reset(Make("Cache"));
        owner.ready=owner.servicesReady=true;owner.settings.soulReminder=true;owner.compositor.reset(Make("Compositor"));
        contextService=Make("ContextService");hides=nativePopulates=0;lastMenuRoot=nullptr;
        auto options=Make("Options"),lockOption=Make("BoolOption");Get(lockOption).enabled=false;
        Get(options).fields["lockUI"]=lockOption;Get(owner.cached.get()).fields["options"]=options;
        dependencies=Make("goc");input=Make("eqo");Get(dependencies).fields["cached"]=owner.cached.get();
        for(const auto* key:{"ydf","ydh","ydj","ydl"}) {
            auto event=Make("InputCustomEvent"),list=Make("List"),foreign=Make("Action");
            Get(foreign).fields["target"]=Make("NativeDragger");Get(list).children.push_back(foreign);
            Get(event).fields["m_input"]=list;Get(input).fields[key]=event;
        }
        snapCleanups=removedInputs=initFailures=0;
        bindSuccess=true;equips=saves=globalWrites=layoutSaves=0;globalDragging=false;capture=nullptr;
        root=Element("Root");Get(root).dimensions["width"]=1200;Get(root).dimensions["height"]=800;Get(root).fields["panel"]=Make("Panel");
        nativeRoot=Element("Document");auto layers=Element("gmy"),layerService=Make("gmo");
        Get(nativeRoot).fields["panel"]=Get(root).fields["panel"];
        Get(nativeRoot).fields["layersService"]=layerService;Get(layerService).fields["dzrv"]=layers;
        Add(nativeRoot,layers);Add(layers,root);Get(root).name="RootLow";
        nativeChild=Element("NativeUi");Add(root,nativeChild);
        panel.Install(&Equip,&Save,&HideModule);panel.Render(root,view);
    }
    ~Fixture(){panel.Hide();module=nullptr;}
    Obj Window(){return latestWindow;}
    Obj Dragger(){return Get(Window()).fields.at("m_movableManipulator");}
    void BeginDrag(float x,float y) {downHook(Dragger(),Make("MouseDown"),nullptr);Get(Window()).dimensions["left"]=x;Get(Window()).dimensions["top"]=y;}
};
struct MenuFixture {
    Obj button, manipulator, menu, event, root, item, view, callback;
    explicit MenuFixture(Fixture& f) {
        button=Get(Get(f.Window()).fields.at("header")).fields.at("rightButton");
        manipulator=Get(button).fields.at("manipulator");
        menu=Make("Menu");event=Make("PopulateEvent");root=Element("MenuRoot");view=Element("MenuView");
        Get(menu).fields["menuItems"]=Make("List");Get(menu).fields["dofusContextualMenuRoot"]=root;
        Get(event).fields["menu"]=menu;Get(root).fields["menuData"]=menu;
        populateHook(manipulator,event,nullptr);
        auto& rows=Get(Get(menu).fields.at("menuItems")).children;
        Check(rows.size()==3&&Get(rows[0]).type->name=="MenuTitle"&&Get(rows[1]).type->name=="MenuSeparator","Title and separator precede the hide option");
        item=rows[2];callback=Get(item).fields.at("m_callback");Get(view).fields["itemData"]=item;
        Add(nativeRoot,root);Add(root,view);lastMenuRoot=root;
    }
};
void HeaderMenuSelectionAndReopen() {
    Fixture f;MenuFixture menu(f);
    Check(hides==0&&f.owner.settings.soulReminder,"Opening options must not hide the module");
    populateHook(menu.manipulator,menu.event,nullptr);
    Check(Get(Get(menu.menu).fields.at("menuItems")).children.size()==3,"Duplicate event must not duplicate row");
    populateHook(Make("ForeignManipulator"),menu.event,nullptr);
    Check(nativePopulates==1,"Foreign menus must retain native callback");
    Check(!soul::HandlePanelMenuCallback(Make("ForeignCallback"),menu.view),"Foreign selection must continue native routing");
    Check(soul::HandlePanelMenuCallback(menu.callback,menu.view)&&hides==1&&!f.owner.settings.soulReminder&&!Panel(f.Window()),"Hide closes menu and owned panel");
    soul::HandlePanelMenuCallback(menu.callback,menu.view);Check(hides==1,"Stale queued callback cannot hide twice");
    Check(!Get(menu.button).fields["manipulator"],"Hide unregisters the native menu manipulator");
    f.owner.settings.soulReminder=true;f.panel.Render(f.root,f.view);
    MenuFixture reopened(f);Check(hides==1&&Panel(f.Window()),"Reactivation restores the same header menu");
    f.panel.Hide();Check(!Panel(reopened.root),"Hiding externally also closes its menu");
}
void HeaderMenuRejectsStaleAndCleanupFailure() {
    Fixture f;MenuFixture menu(f);
    Get(menu.item).enabled=false;soul::HandlePanelMenuCallback(menu.callback,menu.view);
    Check(hides==0,"Disabled row cannot hide module");Get(menu.item).enabled=true;
    auto compositor=f.owner.compositor.get();f.owner.compositor.reset(Make("Replacement"));
    soul::HandlePanelMenuCallback(menu.callback,menu.view);Check(hides==0,"Old session cannot change new settings");
    f.owner.compositor.reset(compositor);
    auto rows=Get(menu.menu).fields.at("menuItems");Get(rows).children.clear();
    soul::HandlePanelMenuCallback(menu.callback,menu.view);Check(hides==0,"Removed row cannot act");Get(rows).children={menu.item};
    failOwner=menu.root;failMethod="RemoveMenu";f.panel.Hide();
    Check(soul::PanelCleanupPending()&&Panel(menu.root),"Failed native menu close retains references for retry");
    soul::HandlePanelMenuCallback(menu.callback,menu.view);Check(hides==0,"Cleanup failure disarms callbacks");
    f.panel.Hide();Check(!soul::PanelCleanupPending()&&!Panel(menu.root),"Retry closes only owned native menu");
    f.panel.Render(f.root,f.view);MenuFixture recreated(f);
    failOwner=recreated.button;failMethod="RemoveManipulator";f.panel.Hide();
    Check(soul::PanelCleanupPending()&&Get(recreated.button).fields["manipulator"],"Failed manipulator removal retained");
    f.panel.Hide();Check(!soul::PanelCleanupPending()&&!Get(recreated.button).fields["manipulator"],"Cleanup retries exact manipulator");
}
bool OnlyModuleRefs() {
    return module&&handles.size()==2&&std::all_of(handles.begin(),handles.end(),[](const auto& entry) {
        return entry.second==module->cached.get()||entry.second==module->compositor.get();
    });
}
void CreationAndClicks() {
    Fixture f;
    Check(soul::GetUiRoot()==f.root,"Root API mismatch");
    Check(Get(f.Window()).name.empty()&&!Get(f.Window()).modal&&Get(f.Window()).movable,"Overlay must be unnamed nonmodal movable native window");
    Check(Get(f.root).children.size()==2,"One overlay only");
    f.panel.Render(f.root,f.view);Check(Get(f.root).children.size()==2,"Render duplicated overlay");
    auto content=Get(f.Window()).fields.at("content");
    Check(Get(content).children.size()==5,"Expected four lines and equip action");
    auto equipped=Get(Get(content).children[2]).children[1];Check(Get(equipped).dimensions["green"]<0.4f,"Missing stone must be red");
    auto other=Element("ListElementBasic");int before=nativeActions;actionHook(other,nullptr);Check(nativeActions==before+1,"Other native action swallowed");
    actionHook(latestAction,nullptr);actionHook(latestAction,nullptr);Check(equips==1,"Repeated queued click should not equip twice");
    f.view.equippedCorrect=true;f.view.equipEnabled=false;f.panel.Render(f.root,f.view);actionHook(latestAction,nullptr);Check(equips==1,"Disabled equip invoked callback");
    Check(Get(equipped).dimensions["green"]>0.8f,"Correct equipped stone warning persisted");
    f.view.equipEnabled=true;f.panel.Render(f.root,f.view);Get(f.root).enums["display"]=1;actionHook(latestAction,nullptr);Check(equips==1,"Hidden ancestor action accepted");
    Get(f.root).enums["display"]=0;Get(f.root).enabled=false;actionHook(latestAction,nullptr);Check(equips==1,"Disabled ancestor action accepted");
    f.panel.Hide();Check(Get(f.root).children==std::vector<Obj>{f.nativeChild},"Hide changed native siblings");
}
void EquippedItemRow() {
    Fixture f;auto content=Get(f.Window()).fields.at("content");
    auto row=Get(content).children[2];auto label=Get(row).children[1];
    auto item=Make("mc");f.view.equippedItem=item;f.view.equipped="Pierre moyenne";
    f.panel.Render(f.root,f.view);
    Check(itemBound==item&&Get(itemSlot).enums["display"]==0,"Equipped native instance must bind and show icon");
    Check(Get(row).enums["flexDirection"]==2&&Get(label).text=="Pierre moyenne","Item icon/text row layout");
    f.view.equipmentChecked=false;f.panel.Render(f.root,f.view);
    Check(Get(label).dimensions["red"]==Get(label).dimensions["green"],"Combat must not reuse placement warning color");
    bindSuccess=false;f.view.equippedItem=Make("mc");f.panel.Render(f.root,f.view);
    Check(Get(itemSlot).enums["display"]==1,"Failed binding must hide stale previous icon");
    bindSuccess=true;f.panel.Render(f.root,f.view);Check(itemBound==f.view.equippedItem&&Get(itemSlot).enums["display"]==0,"Binding recovery must show new item");
    f.view.equippedItem=nullptr;f.panel.Render(f.root,f.view);
    Check(!itemBound&&Get(itemSlot).enums["display"]==1,"Unequip must clear icon");
}
void DragCompletionAndClamping() {
    Fixture f;f.BeginDrag(9999,-50);
    f.panel.Render(f.root,f.view);Check(saves==0&&Get(f.Window()).dimensions["left"]==9999,"Render overwrote active native drag");
    auto event=Make("MouseUp");upHook(f.Dragger(),event,nullptr);
    Check(!capture&&!globalDragging&&!Get(f.Dragger()).active,"Native drag was not completed");
    Check(saves==1&&savedX==956&&savedY==0,"Position must clamp and save after drag only");
    Check(layoutSaves==0,"Native shared layout save must not run for unnamed overlay");
    f.panel.Render(f.root,f.view,savedX,savedY);Check(saves==1,"Stable position caused repeated saves");
    f.BeginDrag(50,60);Get(event).enabled=false;upHook(f.Dragger(),event,nullptr);
    Check(!capture&&!globalDragging,"Wrong mouse-button up left owned global drag flag");
}
void LayoutApplicationAndDragRevision() {
    Fixture f;
    f.panel.Render(f.root,f.view,24,180,10);
    f.BeginDrag(300,240);
    // A new native preset with the same coordinates still cancels this gesture.
    f.panel.Render(f.root,f.view,24,180,11);
    Check(!capture&&!globalDragging&&!Get(f.Dragger()).active&&saves==0,
        "Layout application must cancel the old drag without persisting it");
    Check(Get(f.Window()).dimensions["left"]==24&&Get(f.Window()).dimensions["top"]==180,
        "Same-coordinate layout must restore its position after interrupted drag");
    upHook(f.Dragger(),Make("MouseUp"),nullptr);
    Check(saves==0,"Stale mouse-up must not save into the new layout");
    f.BeginDrag(400,250);
    upHook(f.Dragger(),Make("MouseUp"),nullptr);
    Check(saves==1&&savedGeneration==11&&savedX==400&&savedY==250,
        "A completed gesture must save with the layout revision captured at mouse-down");
    f.panel.Render(f.root,f.view,400,250,11);
    Get(f.root).dimensions["width"]=500;
    f.panel.Render(f.root,f.view,400,250,11);
    Check(saves==1&&Get(f.Window()).dimensions["left"]==256,
        "Viewport clamping must not overwrite a saved native preset");
    f.panel.Render(f.root,f.view,40,60,12);
    Check(saves==1&&Get(f.Window()).dimensions["left"]==40&&Get(f.Window()).dimensions["top"]==60,
        "Applying another layout must restore coordinates without a save callback");
}
void CaptureStatusAndActionVisibility() {
    Fixture f;
    auto content=Get(f.Window()).fields.at("content");
    auto status=Get(Get(content).children[3]).children[1];
    f.view.equippedItem=Make("mc"); f.panel.Render(f.root,f.view);
    Check(itemBound==f.view.equippedItem,"Placement must bind actual native item before combat transition");
    // A stale placement view must still clear the binding and hide every
    // equipment row solely from the capture-only flag.
    f.view.showEquipment=false;
    f.view.captureSpell=Make("ht"); f.view.castEnabled=false;
    f.view.status="Capture absente"; f.view.statusTone=soul::StatusTone::Warning;
    f.panel.Render(f.root,f.view);
    Check(spellBound==f.view.captureSpell && !spellEnabled && Get(spellSlot).enums["display"]==0,
        "Missing capture must show the learned spell even while casting is disabled");
    Check(Get(status).dimensions["green"]<0.4f && Get(status).dimensions["red"]>0.9f,"Missing capture must be red");
    Check(Get(latestAction).enums["display"]==1 && !Get(latestAction).enabled,"Active combat must hide and disable equip even with stale enabled view");
    actionHook(latestAction,nullptr); Check(equips==0,"Hidden equipment callback must be suppressed");
    Check(Get(Get(content).children[0]).enums["display"]==1 && Get(Get(content).children[1]).enums["display"]==1 &&
        Get(Get(content).children[2]).enums["display"]==1 && !itemBound && Get(itemSlot).enums["display"]==1,
        "Capture-only state must hide stale preparation text, item and native hover binding");
    f.view.castEnabled=true;f.panel.Render(f.root,f.view);
    Check(spellEnabled && spellBound==f.view.captureSpell,"Cast eligibility enables the existing visible icon");
    f.view.status="Capture active"; f.view.statusTone=soul::StatusTone::Good; f.view.castEnabled=false;f.panel.Render(f.root,f.view);
    Check(spellBound==f.view.captureSpell && !spellEnabled && Get(spellSlot).enums["display"]==0,
        "Casting the spell changes status/eligibility without removing its icon");
    Check(Get(status).dimensions["green"]>0.8f && Get(status).dimensions["red"]<0.5f,"Actual active capture must be green");
    f.view.status="Inconnu"; f.view.statusTone=soul::StatusTone::Neutral; f.panel.Render(f.root,f.view);
    Check(Get(status).dimensions["green"]==Get(status).dimensions["red"],"Unknown must clear red/green indication");
    f.view.showEquipment=true; f.view.stock="En sac : 2"; f.panel.Render(f.root,f.view);
    Check(!spellBound && !spellEnabled && Get(spellSlot).enums["display"]==1,"Placement clears the stale spell binding");
    Check(Get(latestAction).enums["display"]==0 && Get(latestAction).enabled && Get(Get(content).children[1]).enums["display"]==0 &&
        Get(Get(content).children[0]).enums["display"]==0 && Get(Get(content).children[2]).enums["display"]==0 && itemBound==f.view.equippedItem,
        "New placement must restore previous native row visibility");
}
void HideDuringDragAndForeignCapture() {
    Fixture f;f.BeginDrag(40,70);auto header=capture;f.panel.Hide();
    Check(!capture&&!globalDragging&&globalWrites==2,"Mid-drag hide failed to release native capture/flag");
    Check(!Get(header).fields["parent"]||!Panel(header),"Hidden header remained attached");
    Check(OnlyModuleRefs(),"Hide leaked refs beyond the module cache and compositor");f.panel.Hide();Check(globalWrites==2,"Repeated hide changed global state");
    f.panel.Render(f.root,f.view);f.BeginDrag(50,80);
    auto foreign=Make("ElementDragger");Get(foreign).fields["header"]=Element("OtherNativeHeader");
    downHook(foreign,Make("MouseDown"),nullptr);Obj other=capture;int before=globalWrites;
    f.panel.Hide();Check(capture==other&&globalDragging&&globalWrites==before,"Hide cleared another native window's capture/drag state");
    capture=nullptr;globalDragging=false;
}
void LostCaptureStillClearsOnlyOwnedFlag() {
    Fixture f;f.BeginDrag(50,90);capture=nullptr;
    f.panel.Render(f.root,f.view);
    Check(!globalDragging&&!Get(f.Dragger()).active&&saves==1,"Lost capture left our global drag flag or discarded final position");
    f.BeginDrag(70,100);capture=Element("NonDraggingControl");Obj foreign=capture;
    f.panel.Hide();Check(!globalDragging&&capture==foreign,"Owned flag cleanup must preserve foreign non-drag capture");
    f.panel.Render(f.root,f.view);f.BeginDrag(60,110);flagHook(true,nullptr);capture=nullptr;int writes=globalWrites;
    f.panel.Hide();Check(globalDragging&&globalWrites==writes,"Unrelated direct native setter write must relinquish ownership");
    flagHook(false,nullptr);capture=nullptr;
}
void DetachedAndFailureCleanup() {
    Fixture f;auto old=f.Window();Detach(old);actionHook(latestAction,nullptr);Check(equips==0,"Detached action accepted");
    f.panel.Render(f.root,f.view);Check(f.Window()!=old&&Get(f.root).children.size()==2,"Detached window not replaced cleanly");
    failOwner=f.Window();failMethod="set_dragPosition";f.panel.Hide();Check(handles.size()>2,"Cleanup failure must retain references for retry");f.panel.Hide();Check(OnlyModuleRefs(),"Cleanup retry must release owned references");
    Check(Get(f.root).children.size()==1,"Cleanup exception prevented detach");
    f.panel.Render(f.root,f.view);old=f.Window();failOwner=old;failMethod="RemoveFromHierarchy";f.panel.Hide();
    Check(soul::PanelCleanupPending()&&Get(old).enums["display"]==1,"Failed detach keeps an invisible pending widget");
    f.panel.Render(f.root,f.view);
    Check(f.Window()!=old&&!soul::PanelCleanupPending()&&Get(f.root).children.size()==2,"Render must finish pending cleanup before rebuilding an attached widget");
    f.panel.Render(f.root,f.view);Detach(f.root);Get(f.root).fields["panel"]=nullptr;f.panel.Render(f.root,f.view);Check(OnlyModuleRefs(),"Unattached root must remove panel");
}
void NativeSubscriptionsAndPartialInit() {
    Fixture f;
    auto count=[](const char* key){return Get(Get(Get(input).fields.at(key)).fields.at("m_input")).children.size();};
    for(const auto* key:{"ydf","ydh","ydj","ydl"})Check(count(key)==5,"Native Init must subscribe all owned draggers and disabled resizers");
    f.panel.Hide();
    Check(removedInputs==16&&!soul::PanelCleanupPending(),"Hide must remove all owned keyboard subscriptions");
    for(const auto* key:{"ydf","ydh","ydj","ydl"})Check(count(key)==1,"Foreign native input callback must survive Hide");
    initFailures=1;bool failed=false;
    try{f.panel.Render(f.root,f.view);}catch(const std::exception&){failed=true;}
    Check(failed&&OnlyModuleRefs()&&!soul::PanelCleanupPending(),"Partial Init failure must release widget and subscribed callbacks");
    for(const auto* key:{"ydf","ydh","ydj","ydl"})Check(count(key)==1,"Partial Init cannot leak even its first event subscription");
    f.panel.Render(f.root,f.view);
    failOwner=Get(input).fields.at("ydf");failMethod="UnSubscribe";
    f.panel.Hide();
    Check(soul::PanelCleanupPending()&&handles.size()>2,"Failed unsubscribe retains roots and exposes cleanup pending to stop pump");
    Check(Get(f.root).children.size()==1,"Detach still proceeds after an unsubscribe failure");
    f.panel.Hide();
    Check(!soul::PanelCleanupPending()&&OnlyModuleRefs(),"Retry drains exact input delegates and releases owned refs");
    for(const auto* key:{"ydf","ydh","ydj","ydl"})Check(count(key)==1,"Cleanup retries must preserve unrelated subscriptions");
}
void ImmediateSnapAndNativeGroupMove() {
    Fixture f;f.panel.Render(f.root,f.view,24,180,10);
    f.BeginDrag(300,220);
    Get(f.Window()).delayedResolved={{"left",24.0f},{"top",180.0f}};
    upHook(f.Dragger(),Make("MouseUp"),nullptr);
    Check(saves==1&&savedX==300&&savedY==220,"Final snap position comes from inline coordinates before resolved layout catches up");
    Get(f.Window()).delayedResolved.clear();
    f.panel.Render(f.root,f.view,300,220,10);
    auto foreign=Make("ElementDraggerWithBounds"),other=Element("NativeWidget"),header=Element("HeaderWidget"),group=Make("List");
    Get(foreign).fields["header"]=header;Get(foreign).fields["window"]=other;
    Get(foreign).fields["m_snappedWindows"]=group;Get(group).children={other,f.Window()};Get(foreign).snapGroup=true;
    downHook(foreign,Make("MouseDown"),nullptr);
    Get(f.Window()).dimensions["left"]=480;Get(f.Window()).dimensions["top"]=260;
    f.panel.Render(f.root,f.view,300,220,10);
    Check(Get(f.Window()).dimensions["left"]==480&&saves==1,"Render must preserve a native neighbouring window's group movement");
    upHook(foreign,Make("MouseUp"),nullptr);
    Check(saves==2&&savedX==480&&savedY==260&&savedGeneration==10,"Group completion persists this widget after native snap");
    f.panel.Render(f.root,f.view,480,260,10);
    downHook(foreign,Make("MouseDown"),nullptr);
    f.panel.Render(f.root,f.view,40,60,11);
    Get(f.Window()).dimensions["left"]=700;Get(f.Window()).dimensions["top"]=500;
    upHook(foreign,Make("MouseUp"),nullptr);
    Check(saves==2&&Get(f.Window()).dimensions["left"]==40&&Get(f.Window()).dimensions["top"]==60,
        "A group drag crossing a layout change must restore the new layout and never save stale coordinates");
}
void CompactNativeLockAndCaptureLayout() {
    Fixture f;
    Check(Get(f.Window()).type->name=="MagneticDragWindow"&&Get(f.Window()).dimensions["width"]==244,
        "Placement uses the compact native magnetic widget");
    Check(Get(itemSlot).dimensions["width"]==80&&Get(itemSlot).dimensions["height"]==80&&
        Get(itemSlot).dimensions["minWidth"]==80&&Get(itemSlot).dimensions["maxWidth"]==80&&
        Get(itemSlot).dimensions["flexShrink"]==0,"Readable native item size must not shrink under a long item name");
    Check(Get(spellSlot).dimensions["width"]==80&&Get(spellSlot).dimensions["height"]==80&&
        Get(spellSlot).dimensions["minWidth"]==80&&Get(spellSlot).dimensions["maxWidth"]==80&&
        Get(spellSlot).dimensions["flexShrink"]==0,"Native capture spell case must remain80px beside wrapping status text");
    auto panelContent=Get(f.Window()).fields.at("content");
    for(auto index:{0,1}) {
        const auto& label=Get(Get(panelContent).children[index]);
        Check(label.dimensions.at("red")>=0.9f&&label.dimensions.at("green")>=0.9f&&label.dimensions.at("fontSize")==14,
            "Requirement and stock must explicitly use readable light text on the native dark background");
    }
    const auto& name=Get(Get(Get(panelContent).children[2]).children[1]);
    Check(name.dimensions.at("minWidth")==0&&name.dimensions.at("flexGrow")==1&&name.enums.at("whiteSpace")==0,
        "Item name must wrap in the remaining compact width");
    auto options=Get(f.owner.cached.get()).fields.at("options"),lockOption=Get(options).fields.at("lockUI");
    f.BeginDrag(50,90);Get(lockOption).enabled=true;f.panel.Render(f.root,f.view);
    Check(!Get(f.Window()).movable&&!capture&&!globalDragging,"Global UI lock cancels only the owned gesture and locks its native header");
    Get(lockOption).enabled=false;f.panel.Render(f.root,f.view);Check(Get(f.Window()).movable,"Native UI unlock restores header movement");
    f.view.showEquipment=false;f.view.status="Capture active";f.panel.Render(f.root,f.view);
    Check(Get(f.Window()).dimensions["width"]==220,"Active combat narrows the compact capture-only panel");
    f.view.showEquipment=true;f.view.status.clear();f.panel.Render(f.root,f.view);
    auto content=Get(f.Window()).fields.at("content");
    Check(Get(Get(content).children[3]).enums["display"]==1,"No empty redundant status row in placement");
}
}
namespace fake {
void SpellMatchesActionBarAndHudLayer() {
    Fixture f;
    auto grid=Element("SlotGrid"),slot=Element("Slot");Add(f.root,grid);Add(grid,slot);
    Get(slot).enums["slotType"]=4;Get(slot).enums["slotSize"]=1;
    Get(slot).dimensions["width"]=64;Get(slot).dimensions["height"]=64;
    f.view.showEquipment=false;f.view.captureSpell=Make("ht");
    f.panel.Render(f.root,f.view);
    Check(Get(spellSlot).dimensions["width"]==64&&Get(spellSlot).dimensions["height"]==64&&Get(spellSlot).enums["slotSize"]==1,
        "Capture slot must match native action bar size and scale");
    Check(soul::GetUiRoot()==f.root,"Soul panel stays in HUD layer below tooltips/windows");
    Get(f.root).name="RootWindow";Check(!soul::GetUiRoot(),"Missing HUD layer must never fall back to document overlay");
}
}
int main() {
    try {
        fake::Initialize();
        soul::Panel preinstall;preinstall.Hide();
        fake::SpellMatchesActionBarAndHudLayer();
        for(auto test:{fake::HeaderMenuSelectionAndReopen,fake::HeaderMenuRejectsStaleAndCleanupFailure,fake::CreationAndClicks,fake::EquippedItemRow,fake::CaptureStatusAndActionVisibility,fake::DragCompletionAndClamping,fake::LayoutApplicationAndDragRevision,fake::HideDuringDragAndForeignCapture,fake::LostCaptureStillClearsOnlyOwnedFlag,fake::DetachedAndFailureCleanup,fake::NativeSubscriptionsAndPartialInit,fake::ImmediateSnapAndNativeGroupMove,fake::CompactNativeLockAndCaptureLayout}) {
            test();fake::Check(fake::handles.empty(),"Leaked 64-bit GC refs");
        }
        std::cout<<"PASS: soul panel native contracts, click isolation, clamped drag, mid-drag cleanup, foreign capture, detached UI and failures\n";
        return 0;
    } catch(const std::exception& e) {std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}
}

namespace soul {
managed::Obj CreateSpellView(managed::Obj parent) { fake::spellSlot=fake::Element("SpellSlot");fake::Add(parent,fake::spellSlot);return fake::spellSlot; }
bool UpdateSpellView(managed::Obj spell,bool enabled) { fake::spellBound=spell;fake::spellEnabled=enabled;return true; }
void ReleaseSpellView() { if(fake::spellSlot)fake::Detach(fake::spellSlot);fake::spellSlot=fake::spellBound=nullptr;fake::spellEnabled=false; }
}
