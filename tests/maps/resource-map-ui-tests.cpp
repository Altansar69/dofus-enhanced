#include "maps/resource-map-ui.hpp"
#include "maps/resource-map-icons.hpp"
#include "maps/resource-map-render.hpp"
#include "maps/resource-map-data.hpp"
#include "core/module.hpp"
#include "core/signatures.hpp"
#include "appearance/features.hpp"
#include "ui/options-ui.hpp"
#include <algorithm>
#include <array>
#include <cstring>
#include <iostream>
#include <map>
#include <set>
#include <memory>
#include <unordered_map>
#include <vector>

// Production UI orchestration against an offline managed tree. This fixture
// never opens a client, drives input, changes preferences or loads game code.
namespace fake {
using namespace managed;
struct Rect {float x,y,width,height;};
struct Type {std::string name;};
struct MethodData {Type* type;std::string name;int argc;};
struct Object {
    Type* type{};std::string text;
    std::map<std::string,Obj> fields;
    std::map<std::string,float> styles;
    std::map<std::string,int> units,ints;
    std::vector<std::string> classes;
    std::vector<Obj> children;
    std::array<unsigned char,64> data{};
    Rect bound{0,0,1000,800};
    bool owned=false,value=false,enabled=true,open=false;
    int size=0,notifications=0,mode=0;
};
std::map<std::string,std::unique_ptr<Type>> types;
std::vector<std::unique_ptr<Object>> objects;
std::vector<std::unique_ptr<MethodData>> methods;
std::unordered_map<GCHandle,Obj> handles;
std::map<std::string,uintptr_t> guards;
std::vector<std::string> logs;
GCHandle nextHandle=0xABCD00000000ULL;
Obj character{},cartography{},miniRoot{},miniMapRoot{},tooltipService{};
// The game's map window parts the big map tab sits among (CartographyMapUI).
Obj anomalyFrame{},conquestFrame{};
std::set<Obj> cursorTargets;int cursorRemoveFailures=0,cursorMapRemoveFailures=0;
int cursorEnableFailureCountdown=-1;
Obj cursorEnterMap{},cursorLeaveMap{};
std::map<Obj,std::set<Obj>> cursorCacheKeys;
int nativeEnable=0,nativeDisable=0,changes=0,nativeStyleWrites=0,moveFailureCountdown=-1,removeFailures=0;
int centerRequests=0,tooltipHides=0,manipulatorRemoveFailures=0;
std::vector<int> modeCalls; // fhi.bejv calls made by the mod
bool rejectChange=false;
using LifeFn=void(*)(Obj,Method);
LifeFn enableHook{},disableHook{};
void Check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
Type* Class(const std::string& name){auto& type=types[name];if(!type)type=std::make_unique<Type>(Type{name});return type.get();}
Object& Get(Obj value){Check(value!=nullptr,"Null fixture object");return *static_cast<Object*>(value);}
Obj Make(const std::string& name,bool owned=false){auto value=std::make_unique<Object>();value->type=Class(name);value->owned=owned;auto result=value.get();objects.push_back(std::move(value));return result;}
template<class T>Obj Box(T value,const char* name="Box"){auto result=Make(name);static_assert(sizeof(T)<=64);std::memcpy(Get(result).data.data(),&value,sizeof(value));return result;}
Obj String(const char* text){auto result=Make("String");Get(result).text=text;return result;}
void Detach(Obj child){auto parent=Get(child).fields["parent"];if(parent){auto& children=Get(parent).children;children.erase(std::remove(children.begin(),children.end(),child),children.end());}Get(child).fields["parent"]=nullptr;}
void Insert(Obj parent,int index,Obj child){
    if(Get(child).type->name=="NativeRow"&&Get(parent).owned){
        if(moveFailureCountdown==0){moveFailureCountdown=-1;throw std::runtime_error("injected move failure");}
        if(moveFailureCountdown>0)--moveFailureCountdown;
    }
    Detach(child);auto& children=Get(parent).children;Check(index>=0&&index<=static_cast<int>(children.size()),"Insert bounds");
    children.insert(children.begin()+index,child);Get(child).fields["parent"]=parent;
}
void Add(Obj parent,Obj child){Detach(child);Insert(parent,static_cast<int>(Get(parent).children.size()),child);}
Obj Panel(Obj value){while(value){if(Get(value).fields["panel"])return Get(value).fields["panel"];value=Get(value).fields["parent"];}return nullptr;}
void OriginalEnable(Obj owner,Method){++nativeEnable;Get(owner).enabled=true;}
// fhi.OnDisable: the game's mode back to its world map (bejb(0)).
void OriginalDisable(Obj owner,Method){++nativeDisable;Get(owner).enabled=false;Get(owner).mode=0;}
// The game's own mode switch (fhi.bejv): the asked mode, or back to the world
// map when it is already the current one; only its window shown.
// The game's mode windows shown for its mode (fhi.bejm, also on a world change).
void GameWindows(Obj owner){
    Get(anomalyFrame).styles["display"]=Get(owner).mode==1?0.0f:1.0f;Get(conquestFrame).styles["display"]=Get(owner).mode==2?0.0f:1.0f;
}
void GameMode(Obj owner,int mode){
    auto& window=Get(owner);window.mode=mode==window.mode?0:mode;GameWindows(owner);
}
}
// The shared native button hook is faked: the test routes clicks to the registered handler.
namespace nativeClick{bool(*handler)(managed::Obj)=nullptr;void AddHandler(bool(*h)(managed::Obj)){handler=h;}}
namespace fake{
using namespace managed;
void NativeToggle(Obj tab,bool value){
    Get(tab).value=value;Get(Get(tab).fields.at("m_checkbox")).styles["display"]=0;
}
void Init(){
    api.class_get_field_from_name=[](Klass,const char* name)->void*{static std::map<std::string,size_t> offsets{{"m_rightLabel",0x318},{"m_farRightLabel",0x320},{"deuc",0x60},{"devy",0x660},{"m_checkbox",0x360},{"iconContainer",0x310},{"s_mouseEnterCursorMap",0x40},{"s_mouseLeaveCursorMap",0x48},
        {"dtzx",0x408},{"dtzy",0x410},{"dtzz",0x418},{"dubo",0x52C}};return &offsets.at(name);};
    api.field_get_offset=[](void* field)->size_t{return *static_cast<size_t*>(field);};
    api.field_get_value_object=[](void* field,Obj self)->Obj{Check(!self,"Static catalog type");return Box(int(*static_cast<size_t*>(field)),"AddressableEntryType");};
    api.class_get_name=[](Klass type)->const char*{return static_cast<Type*>(type)->name.c_str();};
    cursorEnterMap=Make("Dictionary`2");cursorLeaveMap=Make("Dictionary`2");
    api.object_get_class=[](Obj value)->Klass{return Get(value).type;};
    api.class_is_valuetype=[](Klass type){const auto& name=static_cast<Type*>(type)->name;return name=="Box"||name=="Length"||name=="StyleValue"||name=="Hierarchy"||name=="CartographyMapUIBinding";};
    api.object_unbox=[](Obj value)->void*{return Get(value).data.data();};
    api.object_new=[](Klass type)->Obj{return Make(static_cast<Type*>(type)->name,true);};
    api.string_new=[](const char* text)->Obj{return String(text);};
    api.gchandle_new=[](Obj value,bool)->GCHandle{const auto handle=++nextHandle;handles.emplace(handle,value);return handle;};
    api.gchandle_get_target=[](GCHandle handle)->Obj{return handles.at(handle);};
    api.gchandle_free=[](GCHandle handle){Check(handles.erase(handle)==1,"GC reference double free");};
}
Obj Invoke(Method method,Obj self,std::initializer_list<void*> arguments){
    const auto& m=*static_cast<const MethodData*>(method);const std::vector<void*> args(arguments);
    Check(static_cast<int>(args.size())==m.argc,"Managed argument count mismatch");
    if(m.name=="qbk"){Check(!self,"Static catalog initializer");return Make("AddressableCatalogName");}
    if(m.type->name=="AddressableEntry"&&m.name==".ctor"){
        Check(args.size()==2&&*static_cast<int*>(args[1])>0,"Native entry receives icon ID");
        *static_cast<int*>(self)=*static_cast<int*>(args[1]);static_cast<int*>(self)[1]=*static_cast<int*>(args[0]);return nullptr;
    }
    if(m.name=="paw"){
        Check(!self&&args.size()==7&&args[0]&&*static_cast<int*>(args[1])>0,"Native item/job sprite loader ABI");
        Check(!args[2]&&!args[3]&&!args[4]&&!(*static_cast<bool*>(args[5]))&&*static_cast<bool*>(args[6]),"Same cache/lifetime flags as native CheckboxCustom");
        Get(args[0]).size=*static_cast<int*>(args[1]);Get(args[0]).fields["catalog"]=Box(static_cast<int*>(args[1])[1]);return nullptr;
    }
    Check(m.name!="AddCursorHandler","Do not overwrite CheckboxCustom's native enabled-state callback");
    if(m.name=="EnableCursorHandler"){
        Check(!self&&*static_cast<int64_t*>(args[1])==2097152&&*static_cast<int*>(args[2])==0,"Native hand cursor ABI");
        Check(Get(args[0]).owned&&Panel(args[0]),"Cursor belongs to attached owned control");
        Check(Get(args[0]).fields["enabledCallback"],"Keep the constructor's native enabled-state callback");
        Check(cursorTargets.insert(args[0]).second,"Duplicate cursor handler");
        cursorCacheKeys[cursorEnterMap].insert(args[0]);
        if(cursorEnableFailureCountdown==0){cursorEnableFailureCountdown=-1;throw std::runtime_error("injected partial cursor registration failure");}
        if(cursorEnableFailureCountdown>0)--cursorEnableFailureCountdown;
        cursorCacheKeys[cursorLeaveMap].insert(args[0]);return nullptr;
    }
    if(m.name=="RemoveCursorHandler"){
        Check(!self,"Static cursor cleanup");
        if(cursorRemoveFailures>0){--cursorRemoveFailures;throw std::runtime_error("injected cursor remove failure");}
        cursorTargets.erase(args[0]);return nullptr;
    }
    if(m.name=="Remove"&&m.type->name=="Dictionary`2"){
        if(self==cursorLeaveMap&&cursorMapRemoveFailures>0){--cursorMapRemoveFailures;throw std::runtime_error("injected cache remove failure");}
        Check(Get(args[0]).owned,"Never remove foreign cursor cache keys");
        return Box(cursorCacheKeys[self].erase(args[0])!=0);
    }
    // VisualElementExtensions: the game's tooltip manipulator on our square only.
    if(m.name=="AddManipulator"){
        Check(!self&&m.type->name=="VisualElementExtensions","Static manipulator extension");
        Check(Get(args[0]).owned&&Get(args[0]).type->name=="DofusButtonCustom"&&Get(args[1]).type->name=="TooltipManipulator","Our square takes our tooltip");
        Check(!Get(args[1]).fields["target"],"Manipulator added once");
        Get(args[0]).fields["manipulator"]=static_cast<Obj>(args[1]);Get(args[1]).fields["target"]=static_cast<Obj>(args[0]);return nullptr;
    }
    if(m.name=="RemoveManipulator"){
        Check(!self&&m.type->name=="VisualElementExtensions","Static manipulator extension");
        if(manipulatorRemoveFailures>0){--manipulatorRemoveFailures;throw std::runtime_error("injected manipulator remove failure");}
        Check(Get(args[1]).fields["target"]==args[0],"Remove exactly our manipulator");
        Get(args[0]).fields["manipulator"]=nullptr;Get(args[1]).fields["target"]=nullptr;return nullptr;
    }
    // CartographyMapUIBinding is a value type: its getters receive the unboxed struct.
    if(m.type->name=="CartographyMapUIBinding"){Obj data=*static_cast<Obj*>(self);return Get(data).fields.at(m.name);}
    auto& object=Get(self);
    if(m.type->name=="fhi"&&m.name=="bejv"){modeCalls.push_back(*static_cast<int*>(args[0]));GameMode(self,*static_cast<int*>(args[0]));return nullptr;}
    if(m.name=="get_ctr_filters")return object.fields["filters"];
    if(m.name==".ctor"){
        if(m.type->name=="CheckboxCustom"||m.type->name=="DofusLabel"){
            Check(args.empty(),"Checkbox/label constructor ABI");
            if(m.type->name=="CheckboxCustom"){
                auto square=Make("VisualElement",true);Add(self,square);object.fields["m_checkbox"]=square;
                object.fields["enabledCallback"]=Make("NativeEnabledCallback");
                // Native UpdateContent creates optional labels only for
                // nonempty text. Icon-only checkboxes have no right label.
            }
            return nullptr;
        }
        if(m.type->name=="DofusButtonCustom"){
            Check(args.empty(),"Native button default constructor");
            auto icon=Make("DofusIcon",true),frame=Make("VisualElement",true);Add(self,frame);Add(frame,icon);object.fields["m_iconLeft"]=icon;return nullptr;
        }
        if(m.type->name=="SectionHeader"){
            Check(args.size()==5&&*static_cast<bool*>(args[0])&&*static_cast<bool*>(args[1]),"Native header must have content and folder button");
            for(int i:{2,4})Check(!*static_cast<bool*>(args[i]),"No data/action buttons");
            auto row=Make("VisualElement",true),frame=Make("VisualElement",true),icon=Make("VisualElement",true),title=Make("DofusLabel",true);
            Add(self,row);Add(row,frame);Add(frame,icon);Add(row,title);
            object.fields["iconContainer"]=icon;object.fields["iconFrame"]=frame;object.fields["titleLabel"]=title;
            Get(frame).styles["display"]=Get(icon).styles["display"]=1;
            auto content=Make("VisualElement",true);Add(self,content);object.fields["content"]=content;return nullptr;
        }
        if(m.type->name=="ScrollView"){
            Check(args.size()==1&&*static_cast<int*>(args[0])==0,"Vertical ScrollView constructor");
            auto content=Make("VisualElement",true);Add(self,content);object.fields["content"]=content;return nullptr;
        }
        // The game's window: a header with its close button, then its content.
        if(m.type->name=="WindowFigma"){
            Check(args.empty(),"Native window default constructor (title bar and close button)");
            auto header=Make("HeaderWindow",true),close=Make("DofusButtonCustom",true),content=Make("VisualElement",true);
            Add(self,header);Add(header,close);Add(self,content);object.fields["closeButton"]=close;object.fields["content"]=content;return nullptr;
        }
        if(m.type->name=="TooltipManipulator"){
            Check(args.size()==6&&args[0]==tooltipService,"The game's tooltip service");
            object.text=Get(args[1]).text;
            object.ints["origin"]=*static_cast<int*>(args[2]);object.ints["source"]=*static_cast<int*>(args[3]);
            object.ints["behavior"]=*static_cast<int*>(args[4]);object.ints["front"]=*static_cast<bool*>(args[5]);return nullptr;
        }
        throw std::runtime_error("Unexpected native constructor");
    }
    if(m.name=="HideTooltip"){Check(object.type->name=="TooltipManipulator","Only our tooltip is hidden");++tooltipHides;return nullptr;}
    if(m.name=="set_pickingMode"){object.notifications=*static_cast<int*>(args[0]);return nullptr;}
    if(m.name=="set_icon"){Check(object.type->name=="DofusButtonCustom","Only the native button receives a Figma icon");object.size=*static_cast<int*>(args[0]);return nullptr;}
    if(m.name=="set_mainStyle"||m.name=="set_headerStyle"){object.ints[m.name.substr(4)]=*static_cast<int*>(args[0]);return nullptr;}
    if(m.name=="set_enableRichText"){object.ints["enableRichText"]=*static_cast<bool*>(args[0]);return nullptr;}
    if(m.name=="get_closeButton")return object.fields.at("closeButton");
    if(m.name=="AddToClassList"){object.classes.push_back(Get(args[0]).text);return nullptr;}
    if(m.name=="IndexOf"){
        const auto& c=object.children;const auto it=std::find(c.begin(),c.end(),static_cast<Obj>(args[0]));
        return Box(it==c.end()?-1:static_cast<int>(it-c.begin()));
    }
    if(m.name=="get_titleLabel")return object.fields.at("titleLabel");
    if(m.name=="get_contentContainer")return object.fields["content"];
    if(m.name=="get_panel")return Panel(self);
    if(m.name=="get_parent")return object.fields["parent"];
    if(m.name=="get_worldBound")return Box(object.bound);
    if(m.name=="get_value")return Box(object.value);
    if(m.name=="SetValueWithoutNotify"){NativeToggle(self,*static_cast<bool*>(args[0]));return nullptr;}
    if(m.name=="set_rightText"){
        object.text=Get(args[0]).text;
        auto& label=object.fields["m_rightLabel"];
        if(object.text.empty()){if(label)Detach(label);label=nullptr;}
        else {if(!label){label=Make("DofusLabel",true);Add(self,label);}Get(label).text=object.text;}
        return nullptr;
    }
    if(m.name=="set_title"||m.name=="set_text"){object.text=Get(args[0]).text;return nullptr;}
    if(m.name=="set_isSingleLine"){Check(!*static_cast<bool*>(args[0]),"Source note can wrap");return nullptr;}
    if(m.name=="set_tooltip"){object.fields["tooltip"]=args[0];return nullptr;}
    if(m.name=="set_farRightText"){
        object.fields["level"]=args[0];
        auto& label=object.fields["m_farRightLabel"];
        if(Get(args[0]).text.empty()){if(label)Detach(label);label=nullptr;}
        else {if(!label){label=Make("DofusLabel",true);Add(self,label);}Get(label).text=Get(args[0]).text;}
        return nullptr;
    }
    if(m.name=="set_leftIconAddress"){Check(*static_cast<int*>(args[0])>0,"Native item icon address");return nullptr;}
    if(m.name=="set_size"){object.size=*static_cast<int*>(args[0]);return nullptr;}
    if(m.name=="set_canBeOpened"){object.enabled=*static_cast<bool*>(args[0]);Get(object.fields.at("iconFrame")).styles["display"]=1;return nullptr;}
    if(m.name=="set_isOpen"){object.open=*static_cast<bool*>(args[0]);return nullptr;}
    if(m.name=="set_horizontalScrollerVisibility"){Check(*static_cast<int*>(args[0])==2,"Horizontal scroller hidden");return nullptr;}
    if(m.name=="set_verticalScrollerVisibility"){Check(*static_cast<int*>(args[0])==0,"Vertical scroller automatic");return nullptr;}
    if(m.name=="AddSubContent"){Add(object.fields["content"],args[0]);return nullptr;}
    // VisualElement.Add goes to the content container (a WindowFigma's content).
    if(m.name=="Add"){Add(object.type->name=="WindowFigma"?object.fields.at("content"):self,args[0]);return nullptr;}
    if(m.name=="BringToFront"){Check(object.owned,"Only owned drawer reordered");Add(object.fields["parent"],self);return nullptr;}
    if(m.name=="Insert"){Insert(self,*static_cast<int*>(args[0]),args[1]);return nullptr;}
    if(m.name=="RemoveFromHierarchy"){
        Check(object.owned,"Only wrapper removed");if(removeFailures>0){--removeFailures;throw std::runtime_error("injected remove failure");}Detach(self);return nullptr;
    }
    if(m.name=="get_hierarchy"){auto value=Make("Hierarchy");Get(value).fields["owner"]=self;return value;}
    if(m.name=="get_childCount")return Box(static_cast<int>(Get(object.fields["owner"]).children.size()));
    if(m.name=="get_Item")return Get(object.fields["owner"]).children.at(*static_cast<int*>(args[0]));
    if(m.name=="get_style")return self;
    if(m.name.starts_with("UnityEngine.UIElements.IStyle.get_"))return Make("StyleValue");
    if(m.name=="set_value"){std::memcpy(object.data.data(),args[0],8);return nullptr;}
    if(m.name.starts_with("UnityEngine.UIElements.IStyle.set_")){
        if(!object.owned)++nativeStyleWrites;
        const auto name=m.name.substr(std::string("UnityEngine.UIElements.IStyle.set_").size());
        // A length's value then its unit (0 pixels, 1 percent).
        object.styles[name]=*static_cast<float*>(args[0]);object.units[name]=static_cast<int*>(args[0])[1];return nullptr;
    }
    throw std::runtime_error("Unexpected managed operation "+m.type->name+"."+m.name);
}
}
namespace managed {
Api api{};uintptr_t gameBase=0;
Klass Api::Class(const char*,const char*,const char* name)const{return fake::Class(name);}
Klass Api::NestedClass(Klass,const char* name)const{return fake::Class(name);}
Method Api::Find(Klass type,const char* name,int argc)const{auto method=std::make_unique<fake::MethodData>(fake::MethodData{static_cast<fake::Type*>(type),name,argc});auto result=method.get();fake::methods.push_back(std::move(method));return result;}
Method Api::FindVerified(Klass type,const char* name,int argc,uintptr_t rva)const{fake::guards[static_cast<fake::Type*>(type)->name+"."+name]=rva;return Find(type,name,argc);}
Obj Api::Invoke(Method method,Obj self,std::initializer_list<void*> args)const{return fake::Invoke(method,self,args);}
Obj Api::Call(Obj self,const char* name,std::initializer_list<void*> args)const{return Invoke(Find(object_get_class(self),name,static_cast<int>(args.size())),self,args);}
Obj Api::StaticField(Klass,const char* name)const{
    if(std::string(name)=="s_mouseEnterCursorMap")return fake::cursorEnterMap;
    if(std::string(name)=="s_mouseLeaveCursorMap")return fake::cursorLeaveMap;
    throw std::runtime_error("Unexpected static field");
}
Obj Api::Field(Obj self,const char* name)const{return fake::Get(self).fields[name];}
void* Api::FieldAddress(Obj self,const char* name)const{
    if(std::string(name)=="dtzx")return &fake::Get(self).enabled;
    if(std::string(name)=="dubo")return &fake::Get(self).mode;
    throw std::runtime_error("Unexpected field address");
}
Obj Api::New(Klass type,std::initializer_list<void*> args)const{
    if(static_cast<fake::Type*>(type)->name=="VisualElement"){fake::Check(args.size()==0,"Native VisualElement ctor");return fake::Make("VisualElement",true);}
    fake::Check(static_cast<fake::Type*>(type)->name=="Length"&&args.size()==2,"Only native Length constructor");
    const std::vector<void*> values(args);struct Length{float value;int unit;};return fake::Box(Length{*static_cast<float*>(values[0]),*static_cast<int*>(values[1])},"Length");
}
}
namespace resourceMap {
constexpr Resource TestCatalog[]{{311,1,1,1,"Eau"},{303,2,1,2,"Frêne"},{312,24,1,3,"Fer"},
    {421,26,1,4,"Ortie"},{289,28,1,5,"Blé"},{1782,36,1,6,"Goujon"}};
std::span<const Resource> Catalog(){return TestCatalog;}
MiniMapSurface ReadMiniMapSurface(){
    MiniMapSurface result;
    if(fake::miniRoot&&fake::Panel(fake::miniRoot)){
        result.root.reset(fake::miniRoot);result.mapRoot.reset(fake::miniMapRoot);
        const auto& bounds=fake::Get(fake::miniRoot).bound;
        result.width=bounds.width;result.height=bounds.height;
    }
    return result;
}
bool CenterMiniMap(const MiniMapSurface& surface){
    fake::Check(surface.root.get()==fake::miniRoot&&surface.mapRoot.get()==fake::miniMapRoot,"Center must receive the current mini-map surface");
    ++fake::centerRequests;return true;
}
}
Module* module=nullptr;
Module::Module(std::filesystem::path):features(nullptr),ui(nullptr){}
managed::Obj Module::FindService(const char* name)const{
    if(std::string(name)=="fng")return fake::character;if(std::string(name)=="fkc")return fake::cartography;
    if(std::string(name)=="fou")return fake::tooltipService;
    throw std::runtime_error("Unexpected resource service");
}
void Module::Change(Settings value,bool){++fake::changes;if(fake::rejectChange)throw std::runtime_error("injected preference failure");settings=std::move(value);}
void Log(const std::string& text){fake::logs.push_back(text);}
void AddHook(managed::Method method,uintptr_t rva,void* replacement,void** original){
    const auto& info=*static_cast<const fake::MethodData*>(method);fake::guards["fhi."+info.name]=rva;
    if(info.name=="OnEnable"){fake::Check(rva==0x1BAEA20,"Enable RVA");fake::enableHook=reinterpret_cast<fake::LifeFn>(replacement);*original=reinterpret_cast<void*>(fake::OriginalEnable);}
    else if(info.name=="OnDisable"){fake::Check(rva==0x1BAE560,"Disable RVA");fake::disableHook=reinterpret_cast<fake::LifeFn>(replacement);*original=reinterpret_cast<void*>(fake::OriginalDisable);}
    else throw std::runtime_error("Unexpected hook");
}
namespace fake {
struct Fixture {
    Module state{std::filesystem::path{}};
    // The map window (fhi) and its CartographyMapUI tree: the transport
    // filters, the game's conquest and anomaly windows, then its mode menu
    // with the worlds square, a divider and the Anomalies and Conquête squares.
    Obj owner{},root{},panel{},filters{},menu{},buttons{},worldButton{},divider{},anomaly{},conquest{};
    std::vector<Obj> originals,rootChildren,squares;
    Fixture(){
        miniRoot=miniMapRoot=nullptr;module=&state;state.ready=true;state.servicesReady=true;state.compositor.reset(Make("Compositor"));
        character=Make("Character");cartography=Make("Cartography");tooltipService=Make("TooltipService");
        owner=Make("fhi");root=Make("VisualElement");panel=Make("Panel");filters=Make("VisualElement");
        Get(root).fields["panel"]=panel;Get(filters).bound={0,272,140,468};Add(root,filters);
        conquestFrame=Make("WindowFigma");anomalyFrame=Make("WindowFigma");Add(root,conquestFrame);Add(root,anomalyFrame);
        Get(conquestFrame).styles["display"]=Get(anomalyFrame).styles["display"]=1;
        menu=Make("VisualElement");buttons=Make("VisualElement");Add(root,menu);Add(menu,buttons);
        worldButton=Make("DofusButtonCustom");divider=Make("Divider");anomaly=Make("DofusButtonCustom");conquest=Make("DofusButtonCustom");
        for(auto square:{worldButton,divider,anomaly,conquest})Add(buttons,square);
        squares=Get(buttons).children;rootChildren=Get(root).children;
        Get(owner).fields["dtzy"]=character;Get(owner).fields["dtzz"]=cartography;Get(owner).fields["dzyj"]=root;
        const auto data=Make("BindingData");
        Get(data).fields={{"get_ctr_worldmapButtons",buttons},{"get_btn_anomaly",anomaly},{"get_btn_conquest",conquest},{"get_ctr_locTreeAnomaly",anomalyFrame}};
        const auto binding=Make("CartographyMapUIBinding");std::memcpy(Get(binding).data.data(),&data,sizeof(Obj));Get(owner).fields["dzyi"]=binding;
        for(int i=0;i<14;++i){auto child=Make("NativeRow");Get(child).notifications=i+10;originals.push_back(child);Add(filters,child);}
        nativeEnable=nativeDisable=changes=nativeStyleWrites=removeFailures=centerRequests=tooltipHides=manipulatorRemoveFailures=0;moveFailureCountdown=cursorEnableFailureCountdown=-1;rejectChange=false;logs.clear();modeCalls.clear();
        resourceMap::InstallUi();
    }
    ~Fixture(){moveFailureCountdown=cursorEnableFailureCountdown=-1;removeFailures=cursorRemoveFailures=cursorMapRemoveFailures=manipulatorRemoveFailures=0;resourceMap::RestoreUi();Check(cursorTargets.empty(),"Native cursor handlers leaked");
        for(auto& [_,keys]:cursorCacheKeys)for(auto key:keys)Check(!Get(key).owned,"Owned native cursor cache key leaked");miniRoot=miniMapRoot=nullptr;module=nullptr;}
    void Open(){enableHook(owner,nullptr);resourceMap::TickUi();}
    // Our window beside the game's mode windows, and our square in its mode column.
    Obj Resources(){for(auto child:Get(root).children)if(Get(child).owned&&Get(child).type->name=="WindowFigma")return child;return nullptr;}
    Obj Tab(){for(auto child:Get(buttons).children)if(Get(child).owned)return child;return nullptr;}
    Obj Body(){return Get(Get(Resources()).fields.at("content")).children.at(0);}
    Obj Footer(){return Get(Get(Resources()).fields.at("content")).children.at(1);}
    Obj Scroll(){return Resources()?Get(Body()).children.front():nullptr;}
    Obj Content(){return Get(Scroll()).fields.at("content");}
    Obj Master(){return Get(Footer()).children.at(0);}
    Obj Note(){return Get(Footer()).children.at(1);}
    Obj Close(){return Get(Resources()).fields.at("closeButton");}
    bool Shown(){return Get(Resources()).styles.at("display")==0;}
    Obj Row(const char* text){for(const auto& object:objects)if(object->owned&&object->type->name=="CheckboxCustom"&&object->text==text&&Panel(object.get())==panel)return object.get();throw std::runtime_error("Missing resource checkbox");}
    // A click on one of the game's own squares: its ExtendedClickable reports
    // it through the shared native button hook (never ours to claim), then its
    // ClickEvent switches the game's mode (fhi.bejv).
    void GameClick(Obj square,int mode){Check(!nativeClick::handler(square),"The game's squares are never claimed");GameMode(owner,mode);}
    void Click(Obj ours){Check(nativeClick::handler(ours),"Our square and close button are ours");}
    // The transport filters and every element of the game's are never restyled.
    void Untouched(){Check(Get(filters).children==originals,"Original native children and order must be restored");for(int i=0;i<14;++i)Check(Get(originals[i]).notifications==i+10,"Native handlers/state changed");Check(nativeStyleWrites==0,"Native filter styles were modified");}
    // Nothing of ours left: the game's squares, windows and their order as the game made them.
    void Restored(){Untouched();Check(Get(buttons).children==squares&&Get(root).children==rootChildren,"The game's squares, windows and their order are as the game made them");}
};
void NativeTreeAndDuplicateOpen(){
    Fixture f;f.Open();auto scroll=f.Scroll();Check(scroll,"Native scroll wrapper missing");
    auto window=f.Resources(),tab=f.Tab();
    Check(Get(window).fields["parent"]==f.root&&!f.Shown(),"Our window sits beside the game's mode windows, closed until its square is selected");
    Check(Get(window).type->name=="WindowFigma"&&tab,"The game's window and square, no floating section of ours");
    f.Untouched();
    const auto& categories=Get(f.Content()).children;Check(categories.size()==6,"Six harvest job categories in the list");
    Check(Get(f.Note()).text.find("DofusDB")!=std::string::npos,"Source credit visible in the window's footer");
    Check(Get(f.Master()).text=="Afficher sur la grande carte"&&Get(f.Master()).size==2,"The display checkbox in the footer, large as the game's footer checkboxes");
    Check(Get(categories[0]).text=="Base"&&Get(categories[1]).text=="Bûcheron"&&Get(categories[5]).text=="Pêcheur","Exact job labels");
    for(int i=0;i<6;++i)Check(!Get(categories[i]).open&&Get(categories[i]).enabled,"Native accordion initially collapsed");
    Check(Get(Get(f.Row("Fer")).fields["m_farRightLabel"]).styles["color"]>.9f,"Levels need explicit readable light color");
    Check(Get(Get(f.Row("Fer")).fields["m_rightLabel"]).styles["color"]>.9f,"Names need readable light color");
    for(int i=0;i<6;++i){
        auto title=Get(categories[i]).fields.at("titleLabel"),row=Get(title).fields.at("parent");
        auto icon=Get(row).children.front();
        Check(Get(icon).size>0&&Get(icon).notifications==1,"Category resource icon stays beside title, outside the native hidden icon frame");
        Check(Get(Get(categories[i]).fields.at("iconFrame")).styles["display"]!=0,"Native empty icon frame remains hidden after canBeOpened refresh");
        const int catalog=*reinterpret_cast<int*>(Get(Get(icon).fields["catalog"]).data.data());Check(catalog==0x60,"Category images must use actual resources from item catalog");
        Check(Get(icon).size==resourceMap::TestCatalog[i].iconId,"Category image identifies first resource for the profession");}
    Check(Get(f.Row("Eau")).size==2,"Rows match large native checkbox size");
    f.Open();Check(f.Scroll()==scroll&&f.Tab()==tab&&nativeEnable==2,"Duplicate native OnEnable must not duplicate controls");
    Check(Get(f.buttons).children.size()==5&&Get(f.root).children.size()==5,"One square and one window per map window");
    Check(resourceMap::CartographyWindow()==f.owner,"The open big map window is shared with the Ocre mode");
    Check(guards.at("SectionHeader..ctor")==0x1745F00&&guards.at("ScrollView..ctor")==0x5741C70&&guards.at("CheckboxCustom.SetValueWithoutNotify")==0x17044F0,"Verified exact constructor/value contracts");
    Check(guards.at("CartographyMapUIBinding.get_btn_anomaly")==0x2EA0940&&guards.at("CartographyMapUIBinding.get_ctr_locTreeAnomaly")==0x2EA2530&&
        guards.at("CartographyMapUIBinding.get_btn_conquest")==0x2EA0A00&&guards.at("CartographyMapUIBinding.get_ctr_worldmapButtons")==0x2EA2830&&
        guards.at("fhi.bejv")==0x1BB4090&&guards.at("WindowFigma..ctor")==0x17987B0&&guards.at("WindowFigma.get_closeButton")==0x1798CF0&&
        guards.at("TooltipManipulator..ctor")==0x15DFAE0&&guards.at("BaseTooltipManipulator.HideTooltip")==0x15CD030&&
        guards.at("VisualElementExtensions.AddManipulator")==0x56755F0&&guards.at("VisualElementExtensions.RemoveManipulator")==0x5676A90&&
        guards.at("DofusButtonCustom.set_mainStyle")==0x170E210&&guards.at("DofusButtonCustom.set_enableRichText")==0x170DEE0&&
        guards.at("VisualElement.IndexOf")==0x55E55B0&&guards.at("VisualElement.AddToClassList")==0x55DF820&&guards.at("VisualElement.RemoveFromHierarchy")==0x55E7BD0,
        "Verified exact mode tab contracts");
    for(const auto& [key,rva]:guards)
        Check(std::any_of(std::begin(Signatures),std::end(Signatures),[&](const NativeSignature& guard){return guard.rva==rva;}),
            ("Every resource map contract and hook has its 16-byte guard: "+key).c_str());
    disableHook(f.owner,nullptr);Check(nativeDisable==1,"Native OnDisable preserved");f.Restored();
    Check(!resourceMap::CartographyWindow(),"A closed big map is no longer shared");
    enableHook(f.owner,nullptr);Check(resourceMap::CartographyWindow()==f.owner,"A window enabled but not attached yet is shared too");
    disableHook(f.owner,nullptr);Check(!resourceMap::CartographyWindow(),"Closing it before attachment unshares it");
}
// The square and the window are the game's own components, made and placed as
// the UXML makes Anomalies and Conquête; nothing of the mod's look.
void TabLikeTheGame(){
    Fixture f;f.Open();
    auto tab=f.Tab(),window=f.Resources();
    const std::vector<Obj> order{f.worldButton,f.divider,f.anomaly,tab,f.conquest};
    Check(tab&&Get(f.buttons).children==order,"« Ressources » right after « Anomalie » in the game's mode column, the game's squares untouched and in order");
    Check(Get(tab).type->name=="DofusButtonCustom"&&Get(tab).size==247&&Get(tab).ints.at("mainStyle")==1&&Get(tab).ints.at("enableRichText")==0,
        "The game's mode square: secondary style, the game's « resources » icon, no rich text");
    Check(Get(tab).styles.at("marginBottom")==4&&Get(tab).units.at("marginBottom")==0&&Get(tab).text.empty()&&!Get(tab).fields["tooltip"],
        "4 px above the next square, as btn_anomaly; no caption and no unused VisualElement tooltip");
    auto tip=Get(tab).fields.at("manipulator");
    Check(tip&&Get(tip).text=="Ressources"&&Get(tip).ints.at("origin")==0&&Get(tip).ints.at("source")==2&&Get(tip).ints.at("behavior")==1&&Get(tip).ints.at("front")==0,
        "The game's tooltip « Ressources », placed and hidden as btn_anomaly's");
    const std::vector<Obj> windows{f.filters,conquestFrame,anomalyFrame,window,f.menu};
    Check(Get(f.root).children==windows,"Our window right after the game's anomaly window, under its mode menu");
    Check(Get(window).text=="Ressources"&&Get(window).ints.at("headerStyle")==1,"The game's window, titled, with the mode windows' secondary header");
    const auto& s=Get(window).styles;const auto& u=Get(window).units;
    Check(s.at("position")!=0&&s.at("top")==10&&u.at("top")==1&&s.at("right")==16&&u.at("right")==0&&s.at("width")==392&&s.at("height")==700&&
        s.at("maxHeight")==86&&u.at("maxHeight")==1&&s.at("flexShrink")==0,"Placed and sized as the game's conquest window, kept on small screens");
    for(auto element:{window,f.Body(),f.Footer(),f.Scroll()})
        for(const auto* mod:{"backgroundColor","borderTopLeftRadius","borderTopRightRadius","borderBottomLeftRadius","borderBottomRightRadius"})
            Check(!Get(element).styles.contains(mod),"No background colour or rounded box of the mod's own");
    Check(Get(f.Body()).styles.at("paddingTop")==16&&Get(f.Body()).styles.at("paddingLeft")==24&&Get(f.Body()).styles.at("flexGrow")==1,"The conquest window's padding");
    Check(Get(f.Scroll()).classes==std::vector<std::string>{"scrollbarPadding"}&&Get(f.Scroll()).styles.at("flexGrow")==1,"The game's list class");
    Check(Get(f.Footer()).classes==std::vector<std::string>{"Footer"}&&Get(f.Footer()).styles.at("paddingTop")==12&&Get(f.Footer()).styles.at("paddingLeft")==16&&
        Get(f.Footer()).styles.at("flexShrink")==0,"The anomaly window's footer");
    Check(Get(f.Note()).classes==std::vector<std::string>{"textColor_white_white80"}&&!Get(f.Note()).styles.contains("color"),"The game's footer text colour for the source note");
    Check(Get(f.Close()).fields["parent"]&&!f.Shown()&&f.state.settings.resourceMapEnabled&&Get(tab).styles["display"]==0,"Shown square, closed window with the game's close button");
    Check(modeCalls.empty()&&Get(f.owner).mode==0,"Opening the map leaves the game's mode as it is");
    Check(logs.size()==1&&logs.front()=="RESOURCE_MAP_UI_READY view=full mode=tab rows=6","One ready line");
    f.Untouched();resourceMap::RestoreUi();f.Restored();
}
// One selected tab at a time, as the game's own: ours or its anomalies or its conquest.
void TabExclusiveWithGameModes(){
    Fixture f;f.Open();auto tab=f.Tab();
    f.Click(tab);Check(!f.Shown(),"A selection applies at the next tick");
    resourceMap::TickUi();Check(f.Shown()&&modeCalls.empty()&&Get(f.owner).mode==0&&resourceMap::ResourceTabOpen(),"Selected from the world map: our window, the game's mode untouched");
    f.GameClick(f.anomaly,1);
    Check(!f.Shown()&&!resourceMap::ResourceTabOpen()&&Get(f.owner).mode==1&&Get(anomalyFrame).styles["display"]==0,"The game's Anomalies square: ours closes at once, its window opens");
    resourceMap::TickUi();Check(!f.Shown()&&Get(f.owner).mode==1&&modeCalls.empty(),"And stays closed");
    f.Click(tab);resourceMap::TickUi();
    Check(f.Shown()&&modeCalls==std::vector<int>{0}&&Get(f.owner).mode==0&&Get(anomalyFrame).styles["display"]!=0,"Ours from Anomalies: the game's own way back to its world map mode, then our window");
    f.GameClick(f.conquest,2);
    Check(!f.Shown()&&Get(f.owner).mode==2&&Get(conquestFrame).styles["display"]==0,"The game's Conquête square: ours closes at once");
    f.Click(tab);resourceMap::TickUi();
    Check(f.Shown()&&modeCalls==std::vector<int>({0,0})&&Get(f.owner).mode==0&&Get(conquestFrame).styles["display"]!=0,"Ours from Conquête");
    f.Click(tab);resourceMap::TickUi();Check(!f.Shown()&&modeCalls.size()==2&&Get(f.owner).mode==0,"A second click on our square closes it, as the game's squares");
    f.Click(tab);resourceMap::TickUi();f.Click(f.Close());resourceMap::TickUi();Check(!f.Shown()&&Get(f.owner).mode==0,"Our window's close button");
    f.Click(tab);resourceMap::TickUi();Check(f.Shown(),"Selected again");
    GameMode(f.owner,1);resourceMap::TickUi();Check(!f.Shown()&&!resourceMap::ResourceTabOpen()&&Get(f.owner).mode==1,"A game mode chosen without its square closes ours");
    GameMode(f.owner,1);resourceMap::TickUi();
    f.Click(tab);f.GameClick(f.anomaly,1);resourceMap::TickUi();
    Check(!f.Shown()&&Get(f.owner).mode==1&&modeCalls.size()==2,"Ours then Anomalies before a tick: the later click wins");
    f.Click(tab);resourceMap::TickUi();Check(f.Shown()&&Get(f.owner).mode==0,"Back to ours");
    f.Click(tab);f.GameClick(f.conquest,2);f.Click(tab);resourceMap::TickUi();
    Check(f.Shown()&&Get(f.owner).mode==0&&Get(conquestFrame).styles["display"]!=0,"Conquête then ours before a tick: ours wins");
    Check(!nativeClick::handler(f.worldButton)&&f.Shown(),"The worlds square is not a mode: ours stays");
    // The mod's other mode of the column (« Ocre ») closes ours through the API.
    const auto calls=modeCalls.size();
    resourceMap::CloseResourceTab();Check(!resourceMap::ResourceTabOpen(),"Closed for the other mod mode");
    resourceMap::TickUi();Check(!f.Shown()&&Get(f.owner).mode==0&&modeCalls.size()==calls,"Its window hides; the game's mode untouched");
    f.Click(tab);resourceMap::TickUi();Check(f.Shown()&&resourceMap::ResourceTabOpen(),"Selected again");
    Check(Get(f.buttons).children.size()==5&&Get(f.root).children.size()==5,"Still one square and one window");
    f.Untouched();resourceMap::RestoreUi();f.Restored();
    Check(Get(f.owner).mode==0&&Get(anomalyFrame).styles["display"]!=0&&Get(conquestFrame).styles["display"]!=0,"Restoring leaves the game in its own world map mode");
}
// The mod's other mode of the column (« Ocre », archi-zones, which ticks before
// this module): it sees our selection from the click on, and entering it
// closes ours, a selection still waiting for its tick included.
void TabWithTheOcreMode(){
    Fixture f;f.Open();auto tab=f.Tab();
    f.Click(tab);
    Check(resourceMap::ResourceTabOpen()&&!f.Shown(),"Selected from the click on, before the tick that shows our window: Ocre leaves in that same tick");
    resourceMap::TickUi();Check(f.Shown()&&resourceMap::ResourceTabOpen(),"Then our window");
    resourceMap::CloseResourceTab();Check(!resourceMap::ResourceTabOpen(),"Ocre entered: ours left");
    resourceMap::TickUi();Check(!f.Shown()&&modeCalls.empty()&&Get(f.owner).mode==0,"Our window hides in that module tick, the game's mode untouched");
    // From the game's Anomalies: our square, then Ocre's, before a tick. Ocre
    // closes ours, then leaves the game's mode itself (its own fhi.bejv(0)).
    f.GameClick(f.anomaly,1);f.Click(tab);Check(resourceMap::ResourceTabOpen(),"Our selection waits for its tick");
    resourceMap::CloseResourceTab();GameMode(f.owner,0);
    resourceMap::TickUi();
    Check(!f.Shown()&&!resourceMap::ResourceTabOpen()&&modeCalls.empty()&&Get(f.owner).mode==0&&Get(anomalyFrame).styles["display"]!=0,
        "A selection waiting for its tick is dropped: no window of ours, no game switch asked by ours");
    f.Click(tab);resourceMap::TickUi();Check(f.Shown()&&resourceMap::ResourceTabOpen(),"One click selects ours again");
    Check(Get(f.buttons).children.size()==5&&Get(f.root).children.size()==5,"Still one square and one window");
    f.Untouched();resourceMap::RestoreUi();f.Restored();
    resourceMap::CloseResourceTab();Check(!resourceMap::ResourceTabOpen(),"No map: no mode, and leaving it is harmless");
}
// The square and its window follow « Ressources sur la grande carte ».
void TabFollowsTheOption(){
    Fixture f;f.Open();auto tab=f.Tab();
    f.Click(tab);resourceMap::TickUi();Check(f.Shown(),"Selected");
    f.state.settings.resourceMapEnabled=false;resourceMap::TickUi();
    Check(Get(tab).styles["display"]!=0&&!f.Shown()&&!resourceMap::ResourceTabOpen()&&Get(tab).fields["parent"]==f.buttons,"Option off: square and window hidden, the mode left");
    f.Click(tab);resourceMap::TickUi();Check(!f.Shown()&&modeCalls.empty(),"No mode while the option is off");
    f.state.settings.resourceMapEnabled=true;resourceMap::TickUi();
    Check(Get(tab).styles["display"]==0&&!f.Shown(),"Option on: the square back, its window closed");
    Check(changes==0,"The option is never written by the tab");
    f.Untouched();resourceMap::RestoreUi();f.Restored();
}
// Closing the map, a world change, stopping the module: the game's squares,
// windows and modes as the game made them; nothing of ours left.
void TabRestore(){
    Fixture f;f.Open();auto tab=f.Tab(),window=f.Resources(),tip=Get(tab).fields.at("manipulator");
    f.Click(tab);resourceMap::TickUi();f.GameClick(f.anomaly,1);f.Click(tab);resourceMap::TickUi();Check(f.Shown()&&Get(f.owner).mode==0,"Open from Anomalies");
    disableHook(f.owner,nullptr);
    Check(!Get(tab).fields["parent"]&&!Get(window).fields["parent"]&&!Get(tab).fields["manipulator"]&&!Get(tip).fields["target"]&&tooltipHides==1&&!resourceMap::UiCleanupPending(),
        "Closed map: our square, its tooltip and our window leave");
    Check(Get(f.owner).mode==0&&nativeDisable==1,"The game puts its own mode back on closing");
    f.Restored();
    f.Open();Check(f.Tab()&&f.Tab()!=tab&&!f.Shown()&&!resourceMap::ResourceTabOpen()&&Get(f.owner).mode==0,"Reopened: the game's own mode, our square unselected");
    // A world change: the game shows its mode windows again for its mode
    // (fhi.bejm from SetUpWorldMap); ours stays as it is.
    f.Click(f.Tab());resourceMap::TickUi();GameWindows(f.owner);resourceMap::TickUi();
    Check(f.Shown()&&Get(f.owner).mode==0&&Get(f.buttons).children.size()==5&&Get(f.root).children.size()==5,"World change: still one square and one open window");
    // The game replacing its mode column while the map is open: nothing of
    // ours stays in the old one, the new one is left as the game made it.
    auto replacement=Make("VisualElement");auto old=f.buttons;
    for(auto square:f.squares)Add(replacement,square);Add(f.menu,replacement);Detach(old);
    auto data=*reinterpret_cast<Obj*>(Get(Get(f.owner).fields["dzyi"]).data.data());Get(data).fields["get_ctr_worldmapButtons"]=replacement;
    resourceMap::TickUi();f.buttons=replacement;
    Check(Get(old).children.empty()&&!f.Tab()&&!f.Resources()&&tooltipHides==2&&!resourceMap::UiCleanupPending(),"A stale column: our square, its tooltip and its window leave");
    f.Restored();
    f.Open();Check(f.Tab()&&Get(replacement).children.size()==5&&!f.Shown(),"The next opening puts ours in the game's current column");
    f.state.stopRequested=true;resourceMap::TickUi();
    Check(!resourceMap::UiCleanupPending()&&!f.Tab()&&!f.Resources()&&tooltipHides==3,"Stopping the module: no square, tooltip or window left");
    f.Restored();
}
void TabCleanupRetries(){
    Fixture f;f.Open();auto tab=f.Tab(),window=f.Resources();
    manipulatorRemoveFailures=1;resourceMap::RestoreUi();
    Check(resourceMap::UiCleanupPending()&&Get(tab).fields["parent"]&&Get(window).fields["parent"],"A failed tooltip release keeps every reference");
    removeFailures=1;resourceMap::RestoreUi();
    Check(resourceMap::UiCleanupPending()&&!Get(tab).fields["manipulator"],"A failed square removal keeps its window for the retry");
    resourceMap::RestoreUi();
    Check(!resourceMap::UiCleanupPending()&&!Get(tab).fields["parent"]&&!Get(window).fields["parent"],"The retry removes what is left");
    f.Restored();
}
void SelectionAndExternalChanges(){
    Fixture f;f.state.settings.resourceMapItems={303};f.Open();Check(Get(f.Row("Frêne")).value,"Persisted selection restored");
    f.Click(f.Tab());
    Get(f.Row("Eau")).value=true;Get(f.Row("Fer")).value=true;resourceMap::TickUi();
    Check(changes==1&&f.state.settings.resourceMapItems==std::vector<int>({303,311,312}),"Multiple checked resources saved once sorted");
    Check(f.Shown(),"Enabled full-map resources show the picker");
    Get(f.Master()).value=false;resourceMap::TickUi();Check(changes==2&&!f.state.settings.resourceMapEnabled&&f.state.settings.resourceMapItems.size()==3,"Disable preserves selection");
    Check(!f.Shown()&&Get(f.Tab()).styles["display"]!=0,"Disabling full-map resources hides its picker");
    f.state.settings.resourceMapItems={289};f.state.settings.resourceMapEnabled=true;resourceMap::TickUi();
    Check(changes==2&&Get(f.Row("Blé")).value&&!Get(f.Row("Eau")).value&&Get(f.Master()).value,"External reset wins without feedback save");
    Check(Get(f.Tab()).styles["display"]==0&&!f.Shown(),"An external enable reveals the full-map square");
    Get(f.Row("Fer")).value=true;rejectChange=true;resourceMap::TickUi();Check(f.state.settings.resourceMapItems==std::vector<int>{289},"Failed save must not mutate preferences");f.Restored();
}
void UnknownIdsArePreserved(){
    Fixture f;f.state.settings.resourceMapItems={999999};f.Open();Check(changes==0,"Opening must not remove unknown persisted resource IDs");
    Get(f.Row("Eau")).value=true;resourceMap::TickUi();Check(f.state.settings.resourceMapItems==std::vector<int>({311,999999}),"Selection retains IDs from another compatible catalog");
}
void OwnerAndPanelLifetime(){
    Fixture f;f.Open();Get(f.Row("Eau")).value=true;f.state.compositor.reset(Make("NewCompositor"));resourceMap::TickUi();
    Check(changes==0,"Stale session UI must never commit");f.Restored();
    f.Open();Get(f.root).fields["panel"]=Make("ReplacementPanel");resourceMap::TickUi();f.Restored();
    Get(f.root).fields["panel"]=f.panel;f.Open();f.state.stopRequested=true;resourceMap::TickUi();f.Restored();
    Check(changes==0,"Stop must not apply pending UI changes");
}
void PartialMoveAndRetryableCleanup(){
    Fixture f;
    // Reparenting native filters is forbidden, even during attachment.
    enableHook(f.owner,nullptr);moveFailureCountdown=0;resourceMap::TickUi();Check(f.Resources(),"Resources attach without moving a native child");moveFailureCountdown=-1;
    if(f.Scroll())resourceMap::RestoreUi();f.Restored();
    f.Open();removeFailures=1;disableHook(f.owner,nullptr);Check(nativeDisable==1,"Cleanup failure cannot swallow native lifecycle");
    Check(resourceMap::UiCleanupPending(),"Transient cleanup keeps references for stop drain");
    resourceMap::TickUi();Check(!resourceMap::UiCleanupPending(),"Successful retry releases pending cleanup");f.Restored();
}
void ForeignChildrenAndNativeReparent(){
    Fixture f;f.Open();auto outside=Make("VisualElement");Add(f.root,outside);Add(outside,f.originals[3]);
    auto foreign=Make("ForeignRow");Add(f.filters,foreign);auto sibling=Make("OtherExtension");Add(f.filters,sibling);
    resourceMap::RestoreUi();Check(Get(f.originals[3]).fields["parent"]==outside,"Restore must not steal independently reparented native row");
    Check(Get(foreign).fields["parent"]==f.filters&&Get(sibling).fields["parent"]==f.filters,"Foreign additions retained");
    Check(Get(f.filters).children.size()==15,"Only owned wrapper/resources removed");
}
// The window takes the game's own sizes: no wait for the map's layout and no
// size of ours rewritten when the map is resized.
void LayoutIsTheGames(){
    Fixture f;Get(f.root).bound.height=0;f.Open();Check(f.Scroll()&&f.Tab(),"The tab does not wait for the map's layout");
    const auto styles=Get(f.Resources()).styles;
    Get(f.root).bound.height=700;resourceMap::TickUi();Get(f.root).bound={0,0,500,400};resourceMap::TickUi();
    Check(Get(f.Resources()).styles==styles,"Resizing the map rewrites nothing of the window");
    Get(f.owner).fields["dtzz"]=Make("StaleService");resourceMap::TickUi();f.Restored();
}
Obj FindUnder(Obj root,const char* type,const char* text=nullptr){
    if(Get(root).type->name==type&&(!text||Get(root).text==text))return root;
    for(auto child:Get(root).children)if(auto found=FindUnder(child,type,text))return found;
    return nullptr;
}
Obj MiniDrawer(){for(auto child:Get(miniRoot).children)if(Get(child).owned&&Get(child).type->name=="VisualElement"&&Get(child).fields["tooltip"])return child;return nullptr;}
Obj MiniCenter(){for(auto child:Get(miniRoot).children)if(Get(child).owned&&Get(child).type->name=="VisualElement"&&FindUnder(child,"DofusButtonCustom"))return child;return nullptr;}
void ClickCenter(Obj center){auto button=FindUnder(center,"DofusButtonCustom");Check(button&&nativeClick::handler&&nativeClick::handler(button),"The native button click reaches the mini-map center handler");}
void CheckPickable(Obj element){Check(Get(element).notifications==0,"Native button subtree stays pickable for hover, cursor and tooltip");for(auto child:Get(element).children)CheckPickable(child);}
void MiniMapSelectionAndLifetime(){
    Fixture f;
    miniRoot=Make("MiniHost");Get(miniRoot).fields["panel"]=Make("MiniPanel");Get(miniRoot).bound={0,0,300,300};
    miniMapRoot=Make("MiniMapContent");Add(miniRoot,miniMapRoot);
    f.state.settings.resourceMapEnabled=false;f.state.settings.resourceMiniMapEnabled=false;f.state.settings.resourceMapItems.clear();
    resourceMap::TickUi();
    auto drawer=MiniDrawer();
    Check(drawer&&Get(drawer).type->name=="VisualElement"&&!f.Resources(),"Mini-map picker discovered without opening full map or selecting resources");
    auto scroll=FindUnder(drawer,"ScrollView");
    Check(Get(scroll).styles["display"]!=0,"Mini-map list starts hidden, leaving only profession icons");
    auto tabs=Get(Get(drawer).children.front()).children;
    Check(tabs.size()==6,"Six compact profession buttons");
    Check(Get(drawer).styles["display"]!=0,"Disabled mini-map resources hide the whole picker");
    auto center=MiniCenter();
    Check(center&&!cursorTargets.contains(center)&&Get(center).fields["tooltip"]&&!FindUnder(drawer,"CheckboxCustom","Centrer"),"Mini-map has a centering button beside the drawer; its native component owns the hover cursor");
    Check(Get(center).styles["left"]==8&&Get(center).styles["top"]==252&&Get(center).styles["width"]==40&&Get(center).styles["height"]==40,"Center button sits in the bottom-left corner, clear of the header and the drawer");
    Check(Get(center).text.empty()&&!Get(center).fields["m_rightLabel"],"Center button shows no caption");
    auto nativeButton=FindUnder(center,"DofusButtonCustom");
    Check(nativeButton&&Get(nativeButton).owned&&Get(nativeButton).size==228,"Center uses the native button component with the full map's player icon");
    CheckPickable(nativeButton);
    Check(Get(nativeButton).fields["tooltip"]&&Get(Get(nativeButton).fields["tooltip"]).text=="Recentrer la mini-carte sur mon personnage","Native button carries the hover tooltip");
    const auto unchanged=f.state.settings;
    ClickCenter(center);resourceMap::TickUi();
    Check(centerRequests==1&&!Get(center).value&&f.state.settings==unchanged&&changes==0,"Center is a one-shot action even with resources disabled and nothing selected");
    Check(!nativeClick::handler(Make("DofusButtonCustom",true)),"Foreign native buttons are not ours");
    resourceMap::TickUi();Check(centerRequests==1,"Idle ticks do not center repeatedly");
    ClickCenter(center);resourceMap::TickUi();Check(centerRequests==2,"Each new activation can center again");
    f.state.settings.miniMapCenterButton=false;resourceMap::TickUi();
    Check(Get(center).styles["display"]!=0&&Get(center).fields["parent"]==miniRoot,"The option hides the center button without rebuilding the picker");
    ClickCenter(center);resourceMap::TickUi();Check(centerRequests==2&&!Get(center).value,"A hidden center button never centers");
    f.state.settings.miniMapCenterButton=true;resourceMap::TickUi();Check(Get(center).styles["display"]==0,"Re-enabling the option shows the button again");
    Check(Get(center).type->name=="VisualElement"&&Get(nativeButton).fields["tooltip"],"Center is a plain host around the native button, which carries the tooltip");
    // Native map wheel/move callbacks are on MiniMapContent. A UI event on
    // our controls bubbles to MiniHost without entering that map subtree.
    for(auto control:{tabs[0],scroll,center}){
        bool reachesMap=false,reachesHost=false;
        for(auto node=control;node;node=Get(node).fields["parent"]){reachesMap|=node==miniMapRoot;reachesHost|=node==miniRoot;}
        Check(reachesHost&&!reachesMap,"Picker wheel and hover events must bypass native cartography handlers");
    }
    for(size_t i=0;i<tabs.size();++i){
        auto tab=tabs[i];
        Check(Get(tab).styles["width"]==48&&Get(tab).fields["tooltip"],"Round profession buttons with hover labels");
        Check(cursorTargets.contains(tab),"Category buttons must use the native hand cursor");
        Check(!Get(tab).fields["m_rightLabel"],"Native icon-only tabs do not instantiate a caption");
        auto icon=Get(tab).children.back();
        Check(Get(icon).size==resourceMap::TestCatalog[i].iconId,"Each mini-map shortcut must display its representative resource image");
        const int catalog=*reinterpret_cast<int*>(Get(Get(icon).fields["catalog"]).data.data());
        Check(catalog==0x60&&Get(icon).notifications==1,"Mini-map resource images use the native item catalog without intercepting clicks");
    }
    auto checkSquares=[&]{for(auto tab:tabs){
        auto square=Get(tab).fields.at("m_checkbox");
        Check(Get(square).styles["visibility"]!=0&&Get(square).styles["position"]!=0,
            "Native checkbox refresh cannot restore a visible square or reserve its layout space");
    }};
    checkSquares();
    Check(logs.size()==1&&logs.front().find("RESOURCE_MAP_UI_READY")!=std::string::npos,"Optional native labels must not abort mini-map construction");
    NativeToggle(tabs[2],true);checkSquares();resourceMap::TickUi();checkSquares();
    Check(Get(tabs[2]).styles["backgroundColor"]>Get(tabs[0]).styles["backgroundColor"],"Open profession bubble is highlighted");
    Check(changes==0&&Get(scroll).styles["display"]==0,"Opening Mineur only opens the list without altering preferences");
    Check(Get(FindUnder(drawer,"SectionHeader","Mineur")).styles["display"]==0&&Get(FindUnder(drawer,"SectionHeader","Paysan")).styles["display"]!=0,"Only selected profession resources appear");
    NativeToggle(tabs[4],true);resourceMap::TickUi();
    Check(!Get(tabs[2]).value&&Get(tabs[4]).value,"Profession tabs are mutually exclusive");
    NativeToggle(tabs[4],false);resourceMap::TickUi();
    Check(Get(scroll).styles["display"]!=0,"Second click closes the resource list");
    Check(Get(drawer).styles["width"]==284&&Get(drawer).styles["left"]==8&&Get(drawer).styles["top"]==40,"Mini picker leaves space for native controls");
    Check(Get(drawer).styles["display"]!=0,"Opening a profession list does not reveal a disabled picker");
    Check(Get(scroll).styles["height"]==148,"Mini picker stays within its viewport");
    auto master=FindUnder(drawer,"CheckboxCustom","Afficher sur la mini-carte");
    Get(master).value=true;Get(FindUnder(drawer,"CheckboxCustom","Fer")).value=true;
    resourceMap::TickUi();
    Check(changes==1&&!f.state.settings.resourceMapEnabled&&f.state.settings.resourceMiniMapEnabled&&f.state.settings.resourceMapItems==std::vector<int>{312},"Mini visibility is independent and resource selection shared");
    Check(Get(drawer).styles["display"]==0,"Enabling mini-map resources shows its picker");
    f.Open();Check(Get(f.Row("Fer")).value&&!Get(f.Master()).value,"Full map restores shared selection while retaining its own visibility");
    Check(Get(f.Tab()).styles["display"]!=0&&!f.Shown(),"Disabled full-map resources hide the full-map square and its window");
    Get(f.Row("Blé")).value=true;resourceMap::TickUi();
    Check(Get(FindUnder(drawer,"CheckboxCustom","Blé")).value,"Full map changes sync to mini-map");
    Get(FindUnder(drawer,"CheckboxCustom","Eau")).value=true;resourceMap::TickUi();resourceMap::TickUi();
    Check(Get(f.Row("Eau")).value,"Mini-map changes sync back to open full map");
    Get(f.Master()).value=true;resourceMap::TickUi();
    Check(Get(f.Tab()).styles["display"]==0,"Enabling full-map resources from its picker shows its square again");
    Get(master).value=false;resourceMap::TickUi();
    Check(f.state.settings.resourceMapEnabled&&!f.state.settings.resourceMiniMapEnabled&&Get(f.Master()).value,"Disabling mini-map retains full-map display");
    Check(Get(drawer).styles["display"]!=0&&Get(center).fields["parent"]==miniRoot,"Disabling mini-map resources hides its picker but keeps the center button");
    f.state.settings.resourceMiniMapEnabled=true;resourceMap::TickUi();
    Check(Get(master).value&&Get(drawer).styles["display"]==0,"External mini-map preference reload synchronizes and reveals the picker without saving stale UI");
    // The big map's tab and the mini-map's picker are independent: selecting
    // or leaving the mode never touches the mini-map.
    const auto miniChildren=Get(miniRoot).children;
    f.Click(f.Tab());resourceMap::TickUi();f.GameClick(f.anomaly,1);resourceMap::TickUi();
    Check(Get(miniRoot).children==miniChildren&&Get(drawer).styles["display"]==0&&Get(center).styles["display"]==0,"The big map's modes leave the mini-map as it is");
    disableHook(f.owner,nullptr);Check(Get(drawer).fields["parent"]==miniRoot,"Closing full map retains mini-map picker");
    auto foreign=Make("NativeCommand");Add(miniRoot,foreign);
    resourceMap::TickUi();{const auto& order=Get(miniRoot).children;Check(order.size()>=3&&order[order.size()-2]==drawer&&order.back()==center&&Get(foreign).fields["parent"]==miniRoot,"Drawer and center button stay above marker layers without modifying native children");}
    Get(miniRoot).bound={0,0,200,200};resourceMap::TickUi();
    Check(Get(drawer).styles["width"]==184&&Get(scroll).styles["height"]==48,"Small mini-map stays usable after resize");
    Check(Get(center).styles["top"]==152&&Get(center).styles["left"]==8,"Center button follows the mini-map height");
    const auto previous=miniRoot;
    ClickCenter(center);
    miniRoot=nullptr;resourceMap::TickUi();
    Check(centerRequests==2,"Closing the mini-map discards its pending center request");
    Check(!Get(drawer).fields["parent"]&&!Get(center).fields["parent"]&&Get(previous).children==std::vector<Obj>{miniMapRoot,foreign},"Hidden mini-map detaches only owned resources and its button");
    miniRoot=previous;resourceMap::TickUi();
    auto replacement=MiniDrawer();Check(replacement&&replacement!=drawer,"Mini-map reopening recreates picker");
    Get(FindUnder(replacement,"CheckboxCustom","Frêne")).value=true;
    const auto count=changes;f.state.stopRequested=true;resourceMap::TickUi();
    Check(changes==count&&!resourceMap::UiCleanupPending(),"Stopping discards pending mini-map changes and removes controls");
    f.Restored();
}
void MiniMapCleanupRetries(){
    Fixture f;miniRoot=Make("MiniHost");Get(miniRoot).fields["panel"]=Make("MiniPanel");Get(miniRoot).bound={0,0,300,300};
    miniMapRoot=Make("MiniMapContent");Add(miniRoot,miniMapRoot);
    resourceMap::TickUi();auto drawer=MiniDrawer();
    cursorRemoveFailures=1;resourceMap::RestoreUi();
    Check(resourceMap::UiCleanupPending()&&!cursorTargets.empty(),"Failed native cursor cleanup must retain ownership");
    auto foreign=Make("NativeCursorControl");cursorCacheKeys[cursorEnterMap].insert(foreign);cursorCacheKeys[cursorLeaveMap].insert(foreign);
    cursorMapRemoveFailures=1;resourceMap::RestoreUi();
    Check(resourceMap::UiCleanupPending(),"Partial cursor cache cleanup must retain retry state");
    removeFailures=1;resourceMap::RestoreUi();
    Check(resourceMap::UiCleanupPending(),"Failed mini-map cleanup retains references");
    resourceMap::RestoreUi();Check(!resourceMap::UiCleanupPending()&&!Get(drawer).fields["parent"],"Mini-map cleanup can retry");
    Check(cursorCacheKeys[cursorEnterMap].contains(foreign)&&cursorCacheKeys[cursorLeaveMap].contains(foreign),"Foreign native cursor handlers and cache keys remain intact");
}
void MiniMapPartialCursorRegistration(){
    Fixture f;miniRoot=Make("MiniHost");Get(miniRoot).fields["panel"]=Make("MiniPanel");Get(miniRoot).bound={0,0,300,300};
    miniMapRoot=Make("MiniMapContent");Add(miniRoot,miniMapRoot);
    cursorEnableFailureCountdown=2;cursorRemoveFailures=2;
    resourceMap::TickUi();auto incomplete=MiniDrawer();
    Check(incomplete&&resourceMap::UiCleanupPending(),"Partial cursor attachment must retain the tree until cleanup succeeds");
    auto oldTabs=Get(Get(incomplete).children.front()).children;
    Check(cursorTargets.size()==3&&cursorCacheKeys[cursorEnterMap].contains(oldTabs[2])&&
        !cursorCacheKeys[cursorLeaveMap].contains(oldTabs[2]),"Failure must occur after a real partial native registration");
    Get(FindUnder(incomplete,"CheckboxCustom","Fer")).value=true;
    resourceMap::TickUi();
    Check(changes==0&&MiniDrawer()==incomplete&&cursorRemoveFailures==0,
        "An incomplete attachment must retry cleanup before polling or saving stale controls");
    resourceMap::TickUi();auto replacement=MiniDrawer();
    Check(replacement&&replacement!=incomplete&&!Get(incomplete).fields["parent"],
        "Successful cleanup must replace the incomplete picker on the next tick");
    for(auto tab:oldTabs)Check(!cursorTargets.contains(tab)&&!cursorCacheKeys[cursorEnterMap].contains(tab)&&
        !cursorCacheKeys[cursorLeaveMap].contains(tab),"Partial attachment must release every old cursor handler and cache key");
    auto tabs=Get(Get(replacement).children.front()).children;
    for(auto tab:tabs)Check(cursorTargets.contains(tab)&&cursorCacheKeys[cursorEnterMap].contains(tab)&&
        cursorCacheKeys[cursorLeaveMap].contains(tab),"Replacement picker must register all six complete cursor handlers");
    Check(changes==0&&f.state.settings.resourceMapItems.empty(),"Retrying attachment must discard pending values from the failed picker");
}
void MiniMapIdentityChanges(){
    Fixture f;miniRoot=Make("MiniHost");Get(miniRoot).fields["panel"]=Make("MiniPanel");Get(miniRoot).bound={0,0,300,300};
    miniMapRoot=Make("MiniMapContent");Add(miniRoot,miniMapRoot);
    resourceMap::TickUi();auto drawer=MiniDrawer(),oldMap=miniMapRoot;
    auto oldTabs=Get(Get(drawer).children.front()).children;
    Get(FindUnder(drawer,"CheckboxCustom","Fer")).value=true;
    miniMapRoot=Make("ReplacementMapContent");Add(miniRoot,miniMapRoot);
    resourceMap::TickUi();auto replacement=MiniDrawer();
    Check(replacement&&replacement!=drawer&&!Get(drawer).fields["parent"]&&changes==0,
        "Replacing the map under the same host must retire stale controls before polling them");
    Check(Get(oldMap).fields["parent"]==miniRoot&&Get(miniMapRoot).fields["parent"]==miniRoot,
        "Map replacement cleanup must preserve native children of the host");
    for(auto tab:oldTabs)Check(!cursorTargets.contains(tab)&&!cursorCacheKeys[cursorEnterMap].contains(tab)&&
        !cursorCacheKeys[cursorLeaveMap].contains(tab),"Map identity replacement must release stale cursor callbacks and keys");
    const auto previousHost=miniRoot;
    auto newHost=Make("ReplacementHost");Get(newHost).fields["panel"]=Panel(previousHost);Get(newHost).bound={0,0,300,300};
    Add(newHost,miniMapRoot);miniRoot=newHost;
    Get(FindUnder(replacement,"CheckboxCustom","Eau")).value=true;
    resourceMap::TickUi();auto reparented=MiniDrawer();
    Check(reparented&&reparented!=replacement&&!Get(replacement).fields["parent"]&&changes==0,
        "Reparenting a map in the same panel must rebuild its sibling picker without saving stale values");
    Check(Get(previousHost).children==std::vector<Obj>{oldMap}&&Get(miniMapRoot).fields["parent"]==newHost,
        "Reparent cleanup must leave both native host hierarchies intact");
}
// « Barre des ressources sur la mini-carte »: unchecked, the mini-map's bar of
// the six jobs and the list it opens leave; the selection and the mini-map
// display option (which draws the markers) are untouched, and neither the
// centering button nor the full map's picker follows it.
void MiniMapBarOption(){
    Fixture f;miniRoot=Make("MiniHost");Get(miniRoot).fields["panel"]=Make("MiniPanel");Get(miniRoot).bound={0,0,300,300};
    miniMapRoot=Make("MiniMapContent");Add(miniRoot,miniMapRoot);
    f.state.settings.resourceMapEnabled=true;f.state.settings.resourceMiniMapEnabled=true;f.state.settings.resourceMapItems={312};
    Check(f.state.settings.resourceMiniMapBar,"The bar is shown by default");
    resourceMap::TickUi();auto drawer=MiniDrawer();auto center=MiniCenter();
    Check(drawer&&center&&Get(drawer).styles["display"]==0&&Get(center).styles["display"]==0,"Bar and centering button shown by default");
    auto tabs=Get(Get(drawer).children.front()).children;auto scroll=FindUnder(drawer,"ScrollView");
    NativeToggle(tabs[2],true);resourceMap::TickUi();
    Check(Get(scroll).styles["display"]==0,"A job's list is open");
    auto expected=f.state.settings;expected.resourceMiniMapBar=false;
    f.state.settings.resourceMiniMapBar=false;resourceMap::TickUi();
    Check(Get(drawer).styles["display"]!=0&&Get(drawer).fields["parent"]==miniRoot,"Unchecked: the bar and its open list leave the mini-map");
    Check(Get(center).styles["display"]==0&&Get(center).fields["parent"]==miniRoot,"The centering button keeps its own option");
    Check(changes==0&&f.state.settings==expected&&f.state.settings.resourceMiniMapEnabled&&f.state.settings.resourceMapItems==std::vector<int>{312},
        "The markers' option and the selection are untouched: the chosen resources stay on the mini-map");
    for(int i=0;i<5;++i)resourceMap::TickUi();
    Check(MiniDrawer()==drawer&&Get(drawer).styles["display"]!=0&&changes==0,"Stable while unchecked: no rebuild and no saved change");
    f.Open();
    Check(Get(f.Tab()).styles["display"]==0,"The full map's square does not follow the mini-map bar");
    f.Click(f.Tab());resourceMap::TickUi();Check(f.Shown(),"Nor does its window");
    f.state.settings.resourceMiniMapBar=true;resourceMap::TickUi();
    Check(Get(drawer).styles["display"]==0&&Get(scroll).styles["display"]==0&&changes==0,"Checked again: the bar returns as it was");
    f.state.settings.resourceMiniMapEnabled=false;resourceMap::TickUi();
    Check(Get(drawer).styles["display"]!=0,"Mini-map resources off still hide the bar too");
    f.state.settings.resourceMiniMapBar=false;resourceMap::TickUi();
    f.state.settings.resourceMiniMapEnabled=true;resourceMap::TickUi();
    Check(Get(drawer).styles["display"]!=0&&changes==0,"Resources back on the mini-map: the unchecked bar stays away");
    const auto host=miniRoot;miniRoot=nullptr;resourceMap::TickUi();miniRoot=host;resourceMap::TickUi();
    auto rebuilt=MiniDrawer();
    Check(rebuilt&&rebuilt!=drawer&&Get(rebuilt).styles["display"]!=0&&Get(Get(rebuilt).children.front()).children.size()==6,
        "A picker built while the bar is unchecked starts hidden");
    f.state.settings.resourceMiniMapBar=true;resourceMap::TickUi();
    Check(Get(rebuilt).styles["display"]==0&&changes==0,"And shows once checked");
}
}
int main(){
    try{
        fake::Init();
        resourceMap::InstallIcons();
        auto icon=fake::Make("VisualElement",true);resourceMap::LoadItemIcon(icon,39401);
        fake::Check(fake::Get(icon).size==39401,"New ores use the native item catalog");
        fake::Check(fake::guards.at("AddressableEntry..ctor")==0x68D5C0&&fake::guards.at("pq.paw")==0x2E91370,"Native image methods pinned");
        fake::NativeTreeAndDuplicateOpen();std::cout<<"native lifecycle passed\n";
        fake::TabLikeTheGame();std::cout<<"mode tab made as the game's passed\n";
        fake::TabExclusiveWithGameModes();std::cout<<"mode tab exclusive with the game's modes passed\n";
        fake::TabWithTheOcreMode();std::cout<<"mode tab with the Ocre mode passed\n";
        fake::TabFollowsTheOption();std::cout<<"mode tab option passed\n";
        fake::TabRestore();std::cout<<"mode tab restore passed\n";
        fake::TabCleanupRetries();std::cout<<"mode tab cleanup retry passed\n";
        fake::SelectionAndExternalChanges();std::cout<<"selection passed\n";
        fake::UnknownIdsArePreserved();std::cout<<"catalog migration passed\n";
        fake::OwnerAndPanelLifetime();std::cout<<"owner lifetime passed\n";
        fake::PartialMoveAndRetryableCleanup();std::cout<<"partial mutation recovery passed\n";
        fake::ForeignChildrenAndNativeReparent();std::cout<<"foreign ownership passed\n";
        fake::LayoutIsTheGames();std::cout<<"game layout passed\n";
        fake::MiniMapSelectionAndLifetime();std::cout<<"mini-map selection and lifetime passed\n";
        fake::MiniMapCleanupRetries();std::cout<<"mini-map cleanup retry passed\n";
        fake::MiniMapPartialCursorRegistration();std::cout<<"mini-map partial cursor attachment recovery passed\n";
        fake::MiniMapIdentityChanges();std::cout<<"mini-map identity replacement and reparenting passed\n";
        fake::MiniMapBarOption();std::cout<<"mini-map bar option passed\n";
        fake::Check(fake::handles.empty(),"Managed references leaked after fixture destruction");
        std::cout<<"Resource map UI production orchestration tests passed\n";return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';for(const auto& line:fake::logs)std::cerr<<line<<'\n';return 1;}
}
