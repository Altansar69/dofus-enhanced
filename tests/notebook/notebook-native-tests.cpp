#include "notebook/notebook-native.hpp"
#include "core/module.hpp"
#include "core/signatures.hpp"
#include "appearance/features.hpp"
#include "ui/options-ui.hpp"
#include "travel/chat-travel.hpp"
#include "souls/soul-panel.hpp"
#include <algorithm>
#include <array>
#include <cstring>
#include <fstream>
#include <iostream>
#include <iterator>
#include <map>

namespace fake {
using namespace managed;
struct Object {
    std::array<unsigned char,64> raw{};
    std::string type,text;
    std::map<std::string,Obj> fields;
    std::vector<Obj> items;
    bool enabled=true;
    int icon=0;
};
struct MethodInfo {std::string type,name;uintptr_t rva;};
std::map<std::string,std::unique_ptr<std::string>> classes;
std::vector<std::unique_ptr<Object>> objects;
std::vector<std::unique_ptr<MethodInfo>> methods;
std::map<GCHandle,Obj> handles;
std::map<std::string,Obj> services;
std::vector<std::string> logs;
GCHandle nextHandle=1;
bool allowLength=true,swapChat=false,changeMap=false,combat=false;
int appends=0,travels=0,mapReads=0,sends=0,sentChannel=-1,sendingChannel=1;
ChatTpTarget travelled{};
bool acceptSend=true;
std::string draft="Brouillon existant ";
std::wstring sent,chars;
std::wstring Wide(const std::string& text){std::wstring result(text.size(),L'\0');const int count=MultiByteToWideChar(CP_UTF8,0,text.data(),static_cast<int>(text.size()),result.data(),static_cast<int>(result.size()));result.resize(count>0?count:0);return result;}
Obj input{},view{},location{},info{},area{};
std::map<int64_t,Obj> mapInfos;
// The game's map flags: each map view keeps them by world map id, then by flag
// id (fhw.duiw); the cartography controller (fhk) gives each message to both.
struct Flag { std::string id,legend; int world=0,x=0,y=0; std::array<float,4> color{}; bool toggle=false,open=false,removable=false,last=false; };
std::map<Obj,Flag> messages; // ccn built by the module.
std::array<std::map<int,std::map<std::string,Flag>>,2> flags; // Mini-map (0) and big map (1).
std::vector<Flag> added; // ccn received by fhk.beoo, in order.
std::vector<std::pair<std::string,int>> removed; // cco received by fhk.beon.
std::array<Obj,2> views{};
bool missingView=false;
// Row menus.
struct Manipulator {Obj service=nullptr;bool builder=true;int button=-1;bool other=true;Obj target=nullptr;};
std::map<Obj,Manipulator> manipulators;
int stoppedEvents=0,removedManipulators=0,closedMenus=0;
bool (*populate)(Obj,Obj)=nullptr;
bool (*menuCallback)(Obj,Obj)=nullptr;
std::string failVerified; // A contract reported missing, as by a different game build.
bool populateHooked=true; // The soul panel's hook on the menus' populate event is active.
notebook::Entry rowEntry;bool rowHasEntry=true;Obj rowAsked=nullptr;
std::vector<std::pair<uint64_t,std::string>> done;
void Check(bool okay,const std::string& message){if(!okay)throw std::runtime_error(message);}
Object& Get(Obj object){Check(object!=nullptr,"Unexpected null native object");return *static_cast<Object*>(object);}
Obj Make(const std::string& type){auto object=std::make_unique<Object>();object->type=type;auto result=object.get();objects.push_back(std::move(object));return result;}
template<class T> Obj Box(T value){auto object=Make("Box");std::memcpy(Get(object).raw.data(),&value,sizeof(value));return object;}
template<class T> void Set(Obj object,size_t offset,T value){std::memcpy(Get(object).raw.data()+offset,&value,sizeof(value));}
template<class T> T At(Obj object,size_t offset){T value{};std::memcpy(&value,Get(object).raw.data()+offset,sizeof(value));return value;}
Obj String(const std::string& text){auto object=Make("String");Get(object).text=text;return object;}
const std::string& Text(void* object){return Get(static_cast<Obj>(object)).text;}
void Initialize(){
    api.object_get_class=[](Obj object)->Klass{return api.Class("","",Get(object).type.c_str());};
    api.object_unbox=[](Obj object)->void*{return object;};
    api.object_new=[](Klass klass)->Obj{return Make(*static_cast<std::string*>(klass));};
    api.method_get_param=[](Method method,uint32_t index)->const void*{
        const auto& m=*static_cast<const MethodInfo*>(method);
        Check(m.name=="AppendDefaultItem"&&(index==7||index==10),"Menu value argument metadata");
        return api.Class("","",index==7?"AddressableEntry":"Color");};
    api.class_from_type=[](const void* type)->Klass{return const_cast<void*>(type);};
    api.gchandle_new=[](Obj object,bool)->GCHandle{handles[++nextHandle]=object;return nextHandle;};
    api.gchandle_get_target=[](GCHandle handle)->Obj{return handles.at(handle);};
    api.gchandle_free=[](GCHandle handle){Check(handles.erase(handle)==1,"Unknown GC handle");};
    api.string_new=[](const char* text)->Obj{return String(text);};
    api.string_length=[](Obj object)->int32_t{Check(Get(object).type=="String","Length of a managed string");return static_cast<int32_t>(Wide(Get(object).text).size());};
    api.string_chars=[](Obj object)->const wchar_t*{Check(Get(object).type=="String","Chars of a managed string");chars=Wide(Get(object).text);return chars.c_str();};
    api.class_get_field_from_name=[](Klass,const char* name)->void*{
        static std::map<std::string,size_t> fields={{"dyvx",0x258},{"posX",0x18},{"posY",0x1A},{"m_mapId",0x10},{"m_sendingChannel",0x1EC},{"duiw",0x1F8},{"duix",0x200}};
        auto found=fields.find(name);return found==fields.end()?nullptr:&found->second;
    };
    api.field_get_offset=[](void* field){return *static_cast<size_t*>(field);};
}
Obj Invoke(Method method,Obj self,std::initializer_list<void*> arguments){
    const auto& m=*static_cast<const MethodInfo*>(method);const auto& name=m.name;const std::vector<void*> args(arguments);
    if(name=="AddManipulator"||name=="RemoveManipulator"){
        Check(self==nullptr&&args.size()==2,"Static manipulator extension");
        auto row=static_cast<Obj>(args[0]),manipulator=static_cast<Obj>(args[1]);
        Check(manipulators.contains(manipulator),"Only the module's own manipulators");
        auto& owned=manipulators[manipulator];
        if(name=="AddManipulator"){Check(!owned.target&&!Get(row).fields["manipulator"],"One menu manipulator per row");owned.target=row;Get(row).fields["manipulator"]=manipulator;}
        else{Check(owned.target==row&&Get(row).fields["manipulator"]==manipulator,"Remove the row's exact manipulator");owned.target=nullptr;Get(row).fields["manipulator"]=nullptr;++removedManipulators;}
        return nullptr;
    }
    if(name=="zeo"){
        Check(Get(self).type=="esg","Read current local character only");
        if(changeMap&&++mapReads>1)Set<int64_t>(location,0x10,456);
        return location;
    }
    if(name=="GetMapInformationById"){Check(self==nullptr,"Static map information lookup");const auto map=*static_cast<int64_t*>(args.at(0));return mapInfos.contains(map)?mapInfos.at(map):nullptr;}
    if(name=="get_subArea")return Get(self).fields["area"];
    if(name=="get_worldmapId")return Box(2);
    if(name=="GetWorldMapById"){Check(self==nullptr,"Static world lookup");const int id=*static_cast<int*>(args.at(0));if(id<1||id>3)return nullptr;auto world=Make("WorldMapData");Get(world).text=id==1?"Monde des Douze":id==2?"Incarnam":"Srambad";return world;}
    if(name=="get_name"){Check(Get(self).type=="WorldMapData"||Get(self).type=="SubAreaData","Name of a world map or of a zone");return String(Get(self).text);}
    if(name=="get_ti_chatInput"){Check(Get(self).type=="ChatviewBinding","Unboxed native binding");return input;}
    if(name=="get_panel")return Get(self).fields["panel"];
    if(name=="get_enabledInHierarchy")return Box(Get(self).enabled);
    if(name=="IsTextLengthValid"){
        Check(self==view,"Validate current chat draft");
        if(swapChat)services["fqw"]=Make("ghz");
        return Box(allowLength);
    }
    if(name=="AppendTextToInputAndFocus"){Check(self==view,"Append current chat only");++appends;draft+=Get(args.at(0)).text+" ";return nullptr;}
    if(name=="TrySendMessage"){
        // ReadOnlySpan<char> by value: the game reads the chosen channel from m_sendingChannel.
        struct Span{const wchar_t* data;int32_t length;};const auto* span=static_cast<const Span*>(args.at(0));
        Check(self==view&&span&&span->data&&span->length>0,"Send through the current chat view with a real span");
        ++sends;sent.assign(span->data,static_cast<size_t>(span->length));sentChannel=sendingChannel;return Box(acceptSend);
    }
    // Map flags: the messages, then the cartography controller as the bus calls it.
    if(name==".ctor"&&m.type=="ccn"){
        Check(Get(self).type=="ccn"&&args.size()==10,"The game's add-flag message and its ten arguments");
        Flag flag;flag.id=Text(args[0]);flag.legend=Text(args[1]);
        flag.world=*static_cast<int*>(args[2]);flag.x=*static_cast<int*>(args[3]);flag.y=*static_cast<int*>(args[4]);
        std::memcpy(flag.color.data(),args[5],sizeof(float)*4);
        flag.toggle=*static_cast<bool*>(args[6]);flag.open=*static_cast<bool*>(args[7]);flag.removable=*static_cast<bool*>(args[8]);flag.last=*static_cast<bool*>(args[9]);
        messages[self]=flag;return nullptr;
    }
    if(name==".ctor"&&m.type=="cco"){Check(Get(self).type=="cco"&&args.size()==2,"The game's remove-flag message");Get(self).text=Text(args[0]);Set<int>(self,0,*static_cast<int*>(args[1]));return nullptr;}
    if(name=="beoo"){
        Check(self==services["fkc"]&&messages.contains(static_cast<Obj>(args.at(0))),"Flags reach the registered cartography controller");
        const auto flag=messages.at(static_cast<Obj>(args.at(0)));added.push_back(flag);
        // fhw.beuw: a world the view does not draw keeps nothing; an existing id
        // is removed by a toggling message, otherwise moved.
        for(auto& worlds:flags){auto known=worlds.find(flag.world);if(known==worlds.end())continue;
            if(known->second.contains(flag.id)&&flag.toggle)known->second.erase(flag.id);else known->second[flag.id]=flag;}
        return nullptr;
    }
    if(name=="beon"){
        auto message=static_cast<Obj>(args.at(0));Check(self==services["fkc"]&&Get(message).type=="cco","Removal reaches the registered cartography controller");
        const int world=At<int>(message,0);removed.push_back({Get(message).text,world});
        for(auto& worlds:flags)for(auto& [key,map]:worlds)if(world==-1||key==world)map.erase(Get(message).text);
        return nullptr;
    }
    if(name=="beop"){Check(self==services["fkc"],"Views of the registered cartography controller");const int kind=*static_cast<int*>(args.at(0));
        if(missingView||kind<0||kind>1)throw std::runtime_error("KeyNotFoundException");return views[kind];}
    if(Get(self).type=="WorldFlags"&&(name=="ContainsKey"||name=="get_Item")){
        const int kind=At<int>(self,0),world=*static_cast<int*>(args.at(0));
        if(name=="ContainsKey")return Box(flags[kind].contains(world));
        Check(flags[kind].contains(world),"A known world only");auto list=Make("Flags");Set<int>(list,0,kind);Set<int>(list,4,world);return list;
    }
    if(Get(self).type=="Flags"&&name=="ContainsKey")return Box(flags[At<int>(self,0)][At<int>(self,4)].contains(Text(args.at(0))));
    // Row menus.
    if(name==".ctor"&&m.type=="DofusContextualMenuManipulator"){
        Check(args.size()==4,"Native menu manipulator ctor");Manipulator owned;owned.service=static_cast<Obj>(args[0]);owned.builder=args[1]!=nullptr;
        owned.button=*static_cast<int*>(args[2]);owned.other=*static_cast<bool*>(args[3]);manipulators[self]=owned;return nullptr;
    }
    if(name=="get_menu")return Get(self).fields.at("menu");
    if(name=="StopPropagation"){++stoppedEvents;Get(self).enabled=false;return nullptr;}
    if(name=="AppendTitle"){auto item=Make("ContextMenuTitleItem");Get(item).text=Text(args.at(0));Get(Get(self).fields.at("menuItems")).items.push_back(item);return nullptr;}
    if(name=="AppendDefaultItem"){
        Check(args.size()==13&&!args[1]&&!*static_cast<bool*>(args[2])&&*static_cast<bool*>(args[3])&&*static_cast<int*>(args[8])==1,
            "Owned native row: no managed action, enabled, closing the menu, default list style");
        auto item=Make("ContextMenuItem");Get(item).text=Text(args[0]);Get(item).fields["tooltip"]=static_cast<Obj>(args[4]);Get(item).icon=*static_cast<int*>(args[9]);
        Get(item).fields["m_callback"]=Make("ClickCallbackHandler");Get(Get(self).fields.at("menuItems")).items.push_back(item);return nullptr;
    }
    if(name=="get_menuItems")return Get(self).fields.at("menuItems");
    if(name=="get_Count")return Box(static_cast<int>(Get(self).items.size()));
    if(name=="get_Item")return Get(self).items.at(*static_cast<int*>(args.at(0)));
    if(name=="get_m_callback")return Get(self).fields.at("m_callback");
    if(name=="get_itemData")return Get(self).fields["itemData"];
    if(name=="get_disabled")return Box(!Get(self).enabled);
    if(name=="get_menuData")return Get(self).fields["menuData"];
    if(name=="RemoveMenu"){Check(!*static_cast<bool*>(args.at(0)),"Close the menu at once");Get(self).fields["panel"]=nullptr;++closedMenus;return nullptr;}
    throw std::runtime_error("Unexpected native action: "+m.type+"."+name);
}
struct Fixture {
    Module owner{"offline"};
    std::filesystem::path directory;
    Fixture(){
        module=&owner;owner.ready=owner.servicesReady=true;owner.compositor.reset(Make("Compositor"));owner.cached.reset(Make("Cache"));
        directory=std::filesystem::temp_directory_path()/("DofusNotebookNativeTests-"+std::to_string(GetCurrentProcessId()));
        std::error_code ignored;std::filesystem::remove_all(directory,ignored);std::filesystem::create_directories(directory);owner.settingsPath=directory/"settings.ini";
        services={{"fng",Make("esg")},{"fqw",Make("ghz")},{"fkc",Make("fhk")},{"fpz",Make("DofusContextualMenuService")}};
        view=Make("ChatView");input=Make("TextInput");Get(input).fields["panel"]=Make("Panel");
        Get(view).fields["dzyi"]=Make("ChatviewBinding");Get(services["fqw"]).fields["dyvx"]=view;
        location=Make("tk");Set<int64_t>(location,0x10,123);info=Make("MapInformationData");area=Make("SubAreaData");Get(info).fields["area"]=area;Get(area).text="Plaines de Cania";
        Set<int16_t>(info,0x18,-8);Set<int16_t>(info,0x1A,42);mapInfos={{123,info}};
        // The game's two map views, worlds 1 to 3, and a flag of the player's own on world 2.
        for(int kind=0;kind<2;++kind){
            views[kind]=Make(kind?"CartographyMapView":"CartographyBannerView");auto worlds=Make("WorldFlags");Set<int>(worlds,0,kind);Get(views[kind]).fields["duiw"]=worlds;
            flags[kind]={{1,{}},{2,{}},{3,{}}};Flag own;own.id="flag_custom_3_4";own.world=2;own.x=3;own.y=4;own.removable=true;flags[kind][2][own.id]=own;
        }
        messages.clear();added.clear();removed.clear();missingView=false;manipulators.clear();stoppedEvents=removedManipulators=closedMenus=0;
        appends=travels=mapReads=sends=0;sentChannel=-1;sendingChannel=1;acceptSend=allowLength=true;swapChat=changeMap=combat=false;draft="Brouillon existant ";sent.clear();travelled={};
        rowEntry={};rowHasEntry=true;rowAsked=nullptr;done.clear();populateHooked=true;
    }
    ~Fixture(){notebook::ReleaseRowMenus();std::error_code ignored;std::filesystem::remove_all(directory,ignored);owner.compositor.reset();owner.cached.reset();module=nullptr;}
};
template<class Fn>void Reject(Fn action,const std::string& message){bool failed=false;try{action();}catch(...){failed=true;}Check(failed,message);}
notebook::Entry Position(uint64_t id,int x,int y,int world,const std::string& title){
    notebook::Entry entry;entry.id=id;entry.version=1;entry.kind=notebook::Kind::Position;entry.x=x;entry.y=y;entry.world=world;entry.title=title;return entry;
}
bool Logged(const std::string& text){return std::any_of(logs.begin(),logs.end(),[&](const std::string& line){return line.find(text)!=std::string::npos;});}
// The game's right-click menu as its service builds it: a menu, its root on screen, the populate event.
struct Menu {Obj menu=nullptr,root=nullptr,event=nullptr;};
Menu Event(){
    Menu result;result.menu=Make("DofusContextualMenu");result.root=Make("DofusContextualMenuRoot");result.event=Make("DofusContextualMenuPopulateEvent");
    Get(result.menu).fields["menuItems"]=Make("List");Get(result.menu).fields["dofusContextualMenuRoot"]=result.root;
    Get(result.root).fields["menuData"]=result.menu;Get(result.root).fields["panel"]=Make("Panel");Get(result.event).fields["menu"]=result.menu;
    return result;
}
std::vector<Obj>& Items(const Menu& menu){return Get(Get(menu.menu).fields.at("menuItems")).items;}
Obj ItemView(Obj item){auto row=Make("ContextMenuItemView");Get(row).fields["itemData"]=item;Get(row).fields["panel"]=Make("Panel");return row;}
bool Click(Obj item){return menuCallback(Get(item).fields.at("m_callback"),ItemView(item));}
bool RowEntry(Obj row,notebook::Entry& entry){rowAsked=row;if(!rowHasEntry)return false;entry=rowEntry;return true;}
void RowDone(const notebook::Entry& entry,const std::string& status){done.push_back({entry.id,status});}
}
namespace managed {
Api api{};uintptr_t gameBase=0;
Klass Api::Class(const char*,const char*,const char* name)const{auto& type=fake::classes[name];if(!type)type=std::make_unique<std::string>(name);return type.get();}
Method Api::FindVerified(Klass klass,const char* name,int count,uintptr_t rva)const{
    const auto key=*static_cast<std::string*>(klass)+"."+name;
    if(key==fake::failVerified)throw std::runtime_error("Verified method unavailable: "+key);
    static const std::map<std::string,std::pair<int,uintptr_t>> expected={
        {"esg.zeo",{0,0x185C1E0}},{"MapInformationData.GetMapInformationById",{1,0x7C4200}},{"MapInformationData.get_subArea",{0,0x7C44B0}},
        {"SubAreaData.get_worldmapId",{0,0x7D1040}},{"ChatviewBinding.get_ti_chatInput",{0,0x2EB8FD0}},
        {"ChatView.IsTextLengthValid",{1,0x2D92630}},{"ChatView.AppendTextToInputAndFocus",{1,0x2D8E010}},{"ChatView.TrySendMessage",{1,0x2D9D300}},
        {"WorldMapData.GetWorldMapById",{1,0x7D3D50}},{"WorldMapData.get_name",{0,0x7D3DE0}},{"SubAreaData.get_name",{0,0x7D0F60}},
        // Map pins.
        {"ccn..ctor",{10,0x267D110}},{"cco..ctor",{2,0x267D290}},{"fhk.beoo",{1,0x1C49250}},{"fhk.beon",{1,0x1C49100}},{"fhk.beop",{1,0x1C49400}},
        // Row menus.
        {"DofusContextualMenuManipulator..ctor",{4,0x16D0BB0}},{"VisualElementExtensions.AddManipulator",{2,0x56755F0}},
        {"VisualElementExtensions.RemoveManipulator",{2,0x5676A90}},{"DofusContextualMenuPopulateEvent.get_menu",{0,0x11E7EC0}},
        {"DofusContextualMenu.AppendTitle",{1,0x16D7F70}},{"DofusContextualMenu.AppendDefaultItem",{13,0x16D6C60}},
        {"ContextMenuItem.get_m_callback",{0,0x16CF040}},{"EventBase.StopPropagation",{0,0x576D9C0}}};
    fake::Check(expected.contains(key)&&expected.at(key)==std::pair{count,rva},"Exact native notebook contract: "+key);
    fake::Check(std::any_of(std::begin(Signatures),std::end(Signatures),[&](const NativeSignature& guard){return guard.rva==rva;}),"Every notebook contract has its 16-byte guard: "+key);
    auto method=std::make_unique<fake::MethodInfo>(fake::MethodInfo{*static_cast<std::string*>(klass),name,rva});auto result=method.get();fake::methods.push_back(std::move(method));return result;
}
Obj Api::Invoke(Method method,Obj self,std::initializer_list<void*> arguments)const{return fake::Invoke(method,self,arguments);}
Obj Api::Call(Obj self,const char* name,std::initializer_list<void*> arguments)const{fake::MethodInfo method{fake::Get(self).type,name,0};return Invoke(&method,self,arguments);}
Obj Api::Field(Obj self,const char* name)const{return fake::Get(self).fields[name];}
std::string Api::String(Obj self)const{return self?fake::Get(self).text:std::string{};}
void* Api::NativeAt(uintptr_t)const{fake::Check(false,"No raw native address is probed");return nullptr;}
void* Api::FieldAddress(Obj self,const char* name)const{fake::Check(self==fake::view&&std::string(name)=="m_sendingChannel","Only the chat view's sending channel is addressed");return &fake::sendingChannel;}
}
Module* module=nullptr;
Module::Module(std::filesystem::path path):folder(path){}
managed::Obj Module::FindService(const char* name)const{return fake::services[name];}
bool Module::InCombat()const{return fake::combat;}
void StartChatTravel(ChatTpTarget target){fake::travelled=target;++fake::travels;}
void Log(const std::string& line){fake::logs.push_back(line);}
namespace soul {
void SetAuxiliaryPanelMenus(bool (*populate)(managed::Obj,managed::Obj),bool (*callback)(managed::Obj,managed::Obj)){fake::populate=populate;fake::menuCallback=callback;}
bool PanelMenusReady(){return fake::populateHooked;}
}
// Map pins: the game's own flag, owned by the mod through its id.
void PinTests(){
    using namespace fake;
    const std::string id="flag_dofusenhanced_notebook_5";
    {Fixture fixture;const auto entry=Position(5,-8,42,2,"Mon zaap");
        Check(notebook::PinsAvailable()&&!notebook::PinShown(entry),"No pin before it is shown");
        notebook::ShowPin(entry);
        Check(removed.size()==1&&removed[0]==std::pair<std::string,int>{id,-1},"A previous flag of the position goes first, from every world");
        Check(added.size()==1,"One add-flag message");const auto flag=added[0];
        Check(flag.id==id&&flag.legend=="Carnet : Mon zaap (-8,42)"&&flag.world==2&&flag.x==-8&&flag.y==42,"The game's flag at the position, in its world, titled from the notebook");
        Check(!flag.toggle&&!flag.open&&flag.removable&&!flag.last,"Shown again it moves (no toggle), the map stays closed, the player can remove it from its menu");
        Check(flag.color==std::array<float,4>{0.25f,0.7f,1.f,1.f},"The notebook's own flag colour");
        Check(flags[0][2].contains(id)&&flags[1][2].contains(id)&&notebook::PinShown(entry),"Shown on the mini-map and on the big map");
        notebook::HidePin(5);
        Check(removed.size()==2&&removed[1]==std::pair<std::string,int>{id,-1}&&!notebook::PinShown(entry)&&!flags[0][2].contains(id)&&!flags[1][2].contains(id),"Retirer removes it from both maps");
        Check(flags[0][2].contains("flag_custom_3_4")&&flags[1][2].contains("flag_custom_3_4"),"The player's own flag is never touched");}
    {Fixture fixture;auto entry=Position(5,-8,42,2,"Mon zaap");notebook::ShowPin(entry);entry.world=3;entry.x=1;entry.title="Renommé";notebook::ShowPin(entry);
        Check(!flags[0][2].contains(id)&&flags[0][3].at(id).x==1&&flags[1][3].at(id).legend=="Carnet : Renommé (1,42)"&&notebook::PinShown(entry),"An edited position moves its one flag to its new world and title");}
    {Fixture fixture;notebook::ShowPin(Position(8,1,2,2,"Mon <b>zaap</b>"));
        Check(added.back().legend=="Carnet : Mon \xE2\x80\xB9" "b>zaap\xE2\x80\xB9" "/b> (1,2)","The title shows as written: no tag in the game's tooltip");}
    {Fixture fixture;const auto entry=Position(5,-8,42,2,"Mon zaap"),other=Position(6,1,2,3,"Autre");
        notebook::ShowPin(entry);notebook::ShowPin(other);
        auto placed=notebook::PlacedPins();
        Check(placed.size()==2&&placed[0]==entry&&placed[1]==other,"The pins placed, as placed");
        notebook::HidePin(6);placed=notebook::PlacedPins();Check(placed.size()==1&&placed[0]==entry,"A pin removed here is no longer placed");
        flags[0][2].erase(id);flags[1][2].erase(id);
        Check(notebook::PlacedPins().empty(),"A pin the player removed from the map is forgotten, never put back");
        Check(!notebook::PinShown(entry)&&added.size()==2,"Nothing is placed again");}
    {Fixture fixture;Get(views[1]).fields["duix"]=Make("fgb");const auto entry=Position(5,-8,42,2,"Mon zaap");
        try {notebook::ShowPin(entry);Check(false,"A ghost's map refuses the pin");}
        catch(const std::runtime_error& error){Check(std::string(error.what())=="Repère impossible tant que ton personnage est un fantôme (drapeau du Phénix sur la carte).","The ghost is told");}
        Check(added.empty()&&removed.empty()&&notebook::PlacedPins().empty(),"While the Phoenix flag is shown, nothing is sent to the map");
        Get(views[1]).fields["duix"]=nullptr;notebook::ShowPin(entry);Check(notebook::PinShown(entry),"Resurrected, the pin is placed");}
    {Fixture fixture;const auto entry=Position(0,1,1,2,"Brouillon");Reject([&]{notebook::ShowPin(entry);},"An unsaved position has no pin");
        notebook::HidePin(0);Check(added.empty()&&removed.empty()&&!notebook::PinShown(entry),"Nothing is sent for an unsaved position");}
    {Fixture fixture;const auto entry=Position(6,1,1,9,"Ailleurs");Reject([&]{notebook::ShowPin(entry);},"A world the game does not draw cannot hold the pin");
        Check(added.size()==1&&!notebook::PinShown(entry),"The game kept nothing for it");}
    {Fixture fixture;auto entry=Position(7,5000,1,2,"Hors carte");Reject([&]{notebook::ShowPin(entry);},"Out-of-range coordinates are refused");Check(added.empty()&&removed.empty(),"Nothing sent for them");}
    {Fixture fixture;services["fkc"]=Make("OtherCartography");const auto entry=Position(5,1,1,2,"A");
        Reject([&]{notebook::ShowPin(entry);},"Another cartography service is refused");Reject([&]{notebook::PinShown(entry);},"Its flags are not read");
        Reject([]{notebook::HidePin(5);},"Nor changed");Check(added.empty()&&removed.empty(),"Nothing sent to it");}
    {Fixture fixture;fixture.owner.stopRequested=true;const auto entry=Position(5,1,1,2,"A");
        Reject([&]{notebook::ShowPin(entry);},"A stopping module places no pin");Reject([]{notebook::HidePin(5);},"Nor removes one");Check(added.empty()&&removed.empty(),"Nothing sent while stopping");}
    {Fixture fixture;missingView=true;const auto entry=Position(5,1,1,2,"A");Reject([&]{notebook::PinShown(entry);},"A missing map view is reported");}
}
// « Ajouter aux positions » from a map: saved like the panel's entries.
void AddPositionTests(){
    using namespace fake;
    {Fixture fixture;auto zone=Make("MapInformationData"),zoneArea=Make("SubAreaData");Get(zone).fields["area"]=zoneArea;Get(zoneArea).text="Plaine des Scarafeuilles";mapInfos[777]=zone;
        const auto path=notebook::DataPath(fixture.owner.settingsPath);
        const auto first=notebook::AddMapPosition(4,-18,1,777);
        Check(first.created&&first.entry.id==1&&first.entry.version==1&&first.entry.title=="Plaine des Scarafeuilles"&&first.entry.map==777&&first.entry.x==4&&first.entry.y==-18&&first.entry.world==1&&first.entry.kind==notebook::Kind::Position,
            "A map cell is saved with its zone name, its map and its world");
        const auto saved=notebook::Read(path);Check(saved.entries.size()==1&&saved.entries[0]==first.entry,"Saved in the notebook file like the panel's entries");
        const auto again=notebook::AddMapPosition(4,-18,1,778);Check(!again.created&&again.entry==first.entry&&notebook::Read(path).entries.size()==1,"The same cell is not added twice");
        const auto other=notebook::AddMapPosition(4,-18,2,0);Check(other.created&&other.entry.title=="Position [4,-18]"&&other.entry.map==0&&other.entry.world==2,"Another world is another position; without a map its coordinates title it");
        Check(notebook::AddMapPosition(5,5,1,999).entry.title=="Position [5,5]","A map without information is titled by its coordinates");
        Get(zoneArea).text="Zone\ncachée";Check(notebook::AddMapPosition(6,6,1,777).entry.title=="Position [6,6]","A zone name the notebook cannot hold falls back to the coordinates");
        Get(zoneArea).text="";Check(notebook::AddMapPosition(7,7,1,777).entry.title=="Position [7,7]","So does an empty zone name");
        Reject([]{notebook::AddMapPosition(5000,1,1,0);},"Out-of-range coordinates are refused");Check(notebook::Read(path).entries.size()==5,"Nothing saved for them");
        {std::ofstream corrupt(path,std::ios::binary|std::ios::trunc);corrupt<<"{";}
        Reject([]{notebook::AddMapPosition(8,8,1,0);},"An unreadable notebook is reported");
        std::ifstream kept(path,std::ios::binary);const std::string text((std::istreambuf_iterator<char>(kept)),std::istreambuf_iterator<char>());
        Check(text=="{","The unreadable file is kept as it was");}
    {Fixture fixture;auto lockPath=notebook::DataPath(fixture.owner.settingsPath);lockPath+=L".lock";
        const auto lock=CreateFileW(lockPath.c_str(),GENERIC_READ|GENERIC_WRITE,0,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
        Check(lock!=INVALID_HANDLE_VALUE,"Fixture writers' lock");
        Reject([]{notebook::AddMapPosition(1,2,1,0);},"Adding from a map takes the writers' lock");CloseHandle(lock);
        Check(!std::filesystem::exists(notebook::DataPath(fixture.owner.settingsPath)),"Nothing added while another client writes");}
    {Fixture fixture;fixture.owner.stopRequested=true;Reject([]{notebook::AddMapPosition(1,1,1,0);},"A stopping module saves nothing");
        Check(!std::filesystem::exists(notebook::DataPath(fixture.owner.settingsPath)),"No file written");}
}
// The right-click menu of the panel's rows.
void RowMenuTests(){
    using namespace fake;
    {Fixture fixture;auto row=Make("ListElementBasic");
        Check(notebook::AttachRowMenu(row),"Rows get the game's contextual menu");
        const auto manipulator=Get(row).fields.at("manipulator");const auto owned=manipulators.at(manipulator);
        Check(owned.service==services["fpz"]&&!owned.builder&&owned.button==1&&!owned.other,"The game's menu manipulator: its menu service, no managed builder, right button only");
        rowEntry=Position(5,-8,42,2,"Mon zaap");
        auto menu=Event();Check(populate(manipulator,menu.event)&&rowAsked==row,"The row's own menu asks the panel for its position");
        auto items=Items(menu);
        Check(items.size()==3&&Get(items[0]).text=="Mon zaap [-8,42]"&&Get(items[1]).text=="Voyager"&&Get(items[2]).text=="Afficher sur la carte"&&Get(items[1]).icon==336&&Get(items[2]).icon==145&&!stoppedEvents,
            "Its title, then Voyager and Afficher sur la carte; the menu is shown");
        Check(Click(items[2])&&added.size()==1&&notebook::PinShown(rowEntry),"Afficher sur la carte places the game's flag");
        Check(done.size()==1&&done[0].first==5&&done[0].second=="Repère placé sur la carte en [-8,42] : grande carte et mini-carte."&&closedMenus==1,"The panel is told and the menu closes");
        Check(!Click(items[2])&&added.size()==1&&done.size()==1,"A closed menu's row is forgotten: its click is left to its null native action");
        menu=Event();populate(manipulator,menu.event);items=Items(menu);
        Check(items.size()==3&&Get(items[2]).text=="Retirer de la carte"&&Get(items[2]).icon==137,"With its flag shown, the menu offers to remove it");
        Check(Click(items[2])&&!notebook::PinShown(rowEntry)&&done.back().second=="Repère retiré de la carte."&&flags[0][2].contains("flag_custom_3_4"),"Retirer de la carte removes the mod's flag only");
        menu=Event();populate(manipulator,menu.event);items=Items(menu);
        Check(Click(items[1])&&travels==1&&travelled==ChatTpTarget{-8,42,2}&&done.back().second=="Trajet demandé vers [-8,42].","Voyager starts the mod's trip to the position");
        combat=true;menu=Event();populate(manipulator,menu.event);items=Items(menu);
        Check(Click(items[1])&&travels==1&&done.back().second=="Trajet indisponible pendant un combat.","A refused trip is told to the panel");combat=false;
        // Stale or foreign clicks are consumed without action.
        const auto count=done.size();
        menu=Event();populate(manipulator,menu.event);items=Items(menu);
        auto foreign=ItemView(items[1]);Get(foreign).fields["itemData"]=items[2];Check(menuCallback(Get(items[1]).fields.at("m_callback"),foreign),"A view of another row is consumed");
        auto closed=ItemView(items[1]);Get(closed).fields["panel"]=nullptr;Check(menuCallback(Get(items[1]).fields.at("m_callback"),closed),"A closed view is consumed");
        Get(items[1]).enabled=false;Check(Click(items[1]),"A disabled row is consumed");Get(items[1]).enabled=true;
        Get(menu.root).fields["panel"]=nullptr;Check(Click(items[1]),"A menu the game closed is consumed");Get(menu.root).fields["panel"]=Make("Panel");
        fixture.owner.cached.reset(Make("OtherCache"));Check(Click(items[1]),"Another session is consumed");
        Check(travels==1&&done.size()==count,"None of them acts");
        Check(!menuCallback(Make("ClickCallbackHandler"),ItemView(items[1])),"Foreign callbacks are left to the game");
        Check(!populate(Make("DofusContextualMenuManipulator"),Event().event),"Foreign menus are left to their owners");
        rowEntry.title="<i>Mon</i> zaap";menu=Event();populate(manipulator,menu.event);
        Check(Get(Items(menu)[0]).text=="\xE2\x80\xB9" "i>Mon\xE2\x80\xB9" "/i> zaap [-8,42]","The menu title shows the title as written");rowEntry.title="Mon zaap";
        // A row without a position: no menu at all.
        rowHasEntry=false;menu=Event();Check(populate(manipulator,menu.event)&&Items(menu).empty()&&stoppedEvents==1,"Nothing to offer: no items and the empty menu is not displayed");
        rowHasEntry=true;rowEntry.kind=notebook::Kind::Note;menu=Event();Check(populate(manipulator,menu.event)&&Items(menu).empty()&&stoppedEvents==2,"Notes and messages have no such menu");
        notebook::ReleaseRowMenus();Check(removedManipulators==1&&!Get(row).fields["manipulator"],"Release removes the row's manipulator");
        Check(!populate(manipulator,Event().event),"A released row's menu is left alone");}
    {Fixture fixture;fixture.owner.stopRequested=true;Check(!notebook::AttachRowMenu(Make("ListElementBasic"))&&manipulators.empty(),"A stopping module attaches nothing");}
    {Fixture fixture;populateHooked=false;logs.clear();
        Check(!notebook::AttachRowMenu(Make("ListElementBasic"))&&!notebook::AttachRowMenu(Make("ListElementBasic"))&&manipulators.empty(),
            "Without the populate hook (soul panel not installed) the rows get no menu: a right-click would bring an empty one");
        Check(std::count_if(logs.begin(),logs.end(),[](const std::string& line){return line.find("NOTEBOOK_ROW_MENU_UNAVAILABLE populate hook inactive")!=std::string::npos;})==1,"Told once in the log");
        populateHooked=true;auto row=Make("ListElementBasic");Check(notebook::AttachRowMenu(row)&&manipulators.size()==1,"With the hook back, rows get their menu");}
    {Fixture fixture;services["fpz"]=nullptr;Check(!notebook::AttachRowMenu(Make("ListElementBasic"))&&Logged("NOTEBOOK_ROW_MENU_ERROR"),"Without the menu service the row has no menu");}
}
// A contract that does not match disables only its own part.
void InstallTests(){
    using namespace fake;
    logs.clear();failVerified="fhk.beoo";notebook::InstallNative();failVerified.clear();
    Check(!notebook::PinsAvailable()&&Logged("NOTEBOOK_PIN_UNAVAILABLE"),"A pin contract that does not match disables the pins");
    {Fixture fixture;const auto entry=Position(5,1,1,2,"A");Reject([&]{notebook::ShowPin(entry);},"No pin without its contracts");
        auto row=Make("ListElementBasic");Check(notebook::AttachRowMenu(row),"The row menus stay");rowEntry=entry;auto menu=Event();populate(Get(row).fields.at("manipulator"),menu.event);
        Check(Items(menu).size()==2&&Get(Items(menu)[1]).text=="Voyager","Without pins the menu only travels");
        Check(notebook::AddMapPosition(1,2,1,123).entry.title=="Plaines de Cania","Positions from the maps stay");}
    logs.clear();failVerified="EventBase.StopPropagation";notebook::InstallNative();failVerified.clear();
    {Fixture fixture;Check(notebook::PinsAvailable()&&!notebook::AttachRowMenu(Make("ListElementBasic"))&&manipulators.empty()&&Logged("NOTEBOOK_ROW_MENU_UNAVAILABLE"),"A menu contract that does not match disables the row menus only");}
    logs.clear();failVerified="SubAreaData.get_name";notebook::InstallNative();failVerified.clear();
    {Fixture fixture;Check(Logged("NOTEBOOK_ZONE_NAME_UNAVAILABLE")&&notebook::AddMapPosition(1,2,1,123).entry.title=="Position [1,2]","Without the zone name a position is titled by its coordinates");}
    logs.clear();notebook::InstallNative();Check(notebook::PinsAvailable()&&logs.empty(),"Everything installs again");
}
int main(){using namespace fake;try{
    Initialize();notebook::InstallNative();notebook::SetRowMenu({RowEntry,RowDone});
    Check(populate&&menuCallback&&logs.empty(),"Row menus registered with the shared menu router, every contract installed");
    {Fixture fixture;auto point=notebook::CurrentPosition();Check(point.map==123&&point.x==-8&&point.y==42&&point.world==2,"Position from actual map with native world");notebook::Travel(point);Check(travels==1&&travelled==ChatTpTarget{-8,42,2},"Explicit journey to the exact saved destination");
        combat=true;Reject([&]{notebook::Travel(point);},"Combat travel forbidden");Check(travels==1,"Combat blocked before route");}
    {Fixture fixture;changeMap=true;Reject([]{notebook::CurrentPosition();},"Reject changing map snapshot");}
    {Fixture fixture;const auto worlds=notebook::Worlds();Check(worlds.size()==3&&worlds[0].id==1&&worlds[1].id==2&&worlds[1].name=="Incarnam"&&worlds[2].name=="Srambad","Worlds listed from the game's data with their names, unused ids skipped");}
    {Fixture fixture;notebook::PrepareMessage("Bonjour");Check(appends==1&&draft=="Brouillon existant Bonjour ","Append preserves existing draft");
        Reject([]{notebook::PrepareMessage("hello\n/send");},"Reject multiline command injection");Check(appends==1,"Invalid text never appended");}
    {Fixture fixture;allowLength=false;Reject([]{notebook::PrepareMessage("Bonjour");},"Native length check gates append");Check(!appends,"No append when draft full");}
    {Fixture fixture;swapChat=true;Reject([]{notebook::PrepareMessage("Bonjour");},"Revalidate chat owner after length check");Check(!appends,"No stale append");}
    {Fixture fixture;Get(input).fields["panel"]=nullptr;Reject([]{notebook::PrepareMessage("Bonjour");},"Detached chat rejected");Check(!appends,"No detached append");}
    {Fixture fixture;fixture.owner.stopRequested=true;Reject([]{notebook::PrepareMessage("Bonjour");},"Stopped module cannot alter draft");}
    {Fixture fixture;notebook::SendChatMessage("Bonjour guilde",4);Check(sends==1&&sent==L"Bonjour guilde"&&sentChannel==4&&sendingChannel==1,"Explicit send on the chosen channel through the game's entry point, previous channel restored");
        notebook::SendChatMessage("Salut",0);Check(sends==2&&sentChannel==1&&sendingChannel==1,"Channel 0 keeps the chat's current channel");Check(appends==0,"Sending never touches the draft");}
    {Fixture fixture;Reject([]{notebook::SendChatMessage("Bonjour",3);},"Unknown channel rejected");Reject([]{notebook::SendChatMessage("hello\n/send",4);},"Multiline send rejected");Check(!sends,"Invalid text never sent");}
    {Fixture fixture;acceptSend=false;Reject([]{notebook::SendChatMessage("Bonjour",4);},"A refusal by the chat is reported");Check(sends==1&&sendingChannel==1,"Channel restored after a refusal");}
    {Fixture fixture;allowLength=false;Reject([]{notebook::SendChatMessage("Bonjour",0);},"Native length check gates send");Check(!sends,"No send when too long");}
    {Fixture fixture;Get(input).fields["panel"]=nullptr;Reject([]{notebook::SendChatMessage("Bonjour",0);},"Closed chat rejected for send");Check(!sends,"No send without the chat");}
    {Fixture fixture;fixture.owner.stopRequested=true;Reject([]{notebook::SendChatMessage("Bonjour",0);},"Stopped module cannot send");}
    PinTests();AddPositionTests();RowMenuTests();InstallTests();
    Check(handles.empty(),"Native operations release all managed refs");
    std::cout<<"PASS: native map capture, explicit chat draft append and send, the game's map flags as position pins, positions added from the maps, the rows' right-click menu, optional contracts\n";return 0;
}catch(const std::exception& error){std::cerr<<"FAIL: "<<error.what()<<'\n';for(const auto& line:fake::logs)std::cerr<<line<<'\n';return 1;}}
