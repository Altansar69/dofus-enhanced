#include "notebook/notebook-native.hpp"
#include "core/module.hpp"
#include "appearance/features.hpp"
#include "ui/options-ui.hpp"
#include "travel/chat-travel.hpp"
#include <array>
#include <cstring>
#include <iostream>
#include <map>

namespace fake {
using namespace managed;
struct Object {
    std::array<unsigned char,64> raw{};
    std::string type,text;
    std::map<std::string,Obj> fields;
    bool enabled=true;
};
struct MethodInfo {std::string name;uintptr_t rva;};
std::map<std::string,std::unique_ptr<std::string>> classes;
std::vector<std::unique_ptr<Object>> objects;
std::vector<std::unique_ptr<MethodInfo>> methods;
std::map<GCHandle,Obj> handles;
std::map<std::string,Obj> services;
GCHandle nextHandle=1;
bool allowLength=true,swapChat=false,changeMap=false,combat=false;
int appends=0,travels=0,mapReads=0,sends=0,sentChannel=-1,sendingChannel=1;
bool acceptSend=true;
std::string draft="Brouillon existant ";
std::wstring sent,chars;
std::wstring Wide(const std::string& text){std::wstring result(text.size(),L'\0');const int count=MultiByteToWideChar(CP_UTF8,0,text.data(),static_cast<int>(text.size()),result.data(),static_cast<int>(result.size()));result.resize(count>0?count:0);return result;}
Obj input{},view{},location{},info{},area{};
void Check(bool okay,const char* message){if(!okay)throw std::runtime_error(message);}
Object& Get(Obj object){Check(object!=nullptr,"Unexpected null native object");return *static_cast<Object*>(object);}
Obj Make(const std::string& type){auto object=std::make_unique<Object>();object->type=type;auto result=object.get();objects.push_back(std::move(object));return result;}
template<class T> Obj Box(T value){auto object=Make("Box");std::memcpy(Get(object).raw.data(),&value,sizeof(value));return object;}
template<class T> void Set(Obj object,size_t offset,T value){std::memcpy(Get(object).raw.data()+offset,&value,sizeof(value));}
void Initialize(){
    api.object_get_class=[](Obj object)->Klass{return api.Class("","",Get(object).type.c_str());};
    api.object_unbox=[](Obj object)->void*{return object;};
    api.gchandle_new=[](Obj object,bool)->GCHandle{handles[++nextHandle]=object;return nextHandle;};
    api.gchandle_get_target=[](GCHandle handle)->Obj{return handles.at(handle);};
    api.gchandle_free=[](GCHandle handle){Check(handles.erase(handle)==1,"Unknown GC handle");};
    api.string_new=[](const char* text)->Obj{auto object=Make("String");Get(object).text=text;return object;};
    api.string_length=[](Obj object)->int32_t{Check(Get(object).type=="String","Length of a managed string");return static_cast<int32_t>(Wide(Get(object).text).size());};
    api.string_chars=[](Obj object)->const wchar_t*{Check(Get(object).type=="String","Chars of a managed string");chars=Wide(Get(object).text);return chars.c_str();};
    api.class_get_field_from_name=[](Klass,const char* name)->void*{
        static std::map<std::string,size_t> fields={{"dyvx",0x258},{"posX",0x18},{"posY",0x1A},{"m_mapId",0x10},{"m_sendingChannel",0x1EC}};
        auto found=fields.find(name);return found==fields.end()?nullptr:&found->second;
    };
    api.field_get_offset=[](void* field){return *static_cast<size_t*>(field);};
}
Obj Invoke(Method method,Obj self,std::initializer_list<void*> arguments){
    const auto& name=static_cast<const MethodInfo*>(method)->name;const std::vector<void*> args(arguments);
    if(name=="zeo"){
        Check(Get(self).type=="esg","Read current local character only");
        if(changeMap&&++mapReads>1)Set<int64_t>(location,0x10,456);
        return location;
    }
    if(name=="GetMapInformationById"){Check(self==nullptr&&*static_cast<int64_t*>(args.at(0))==123,"Read actual map information");return info;}
    if(name=="get_subArea")return area;
    if(name=="get_worldmapId")return Box(2);
    if(name=="GetWorldMapById"){Check(self==nullptr,"Static world lookup");const int id=*static_cast<int*>(args.at(0));if(id<1||id>3)return nullptr;auto world=Make("WorldMapData");Get(world).text=id==1?"Monde des Douze":id==2?"Incarnam":"Srambad";return world;}
    if(name=="get_name"){Check(Get(self).type=="WorldMapData","Name of a world map");auto text=Make("String");Get(text).text=Get(self).text;return text;}
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
    throw std::runtime_error("Unexpected native action: "+name);
}
struct Fixture {
    Module owner{"offline"};
    Fixture(){
        module=&owner;owner.ready=owner.servicesReady=true;owner.compositor.reset(Make("Compositor"));
        services={{"fng",Make("esg")},{"fqw",Make("ghz")}};
        view=Make("ChatView");input=Make("TextInput");Get(input).fields["panel"]=Make("Panel");
        Get(view).fields["dzyi"]=Make("ChatviewBinding");Get(services["fqw"]).fields["dyvx"]=view;
        location=Make("tk");Set<int64_t>(location,0x10,123);info=Make("MapInformationData");area=Make("SubAreaData");
        Set<int16_t>(info,0x18,-8);Set<int16_t>(info,0x1A,42);
        appends=travels=mapReads=sends=0;sentChannel=-1;sendingChannel=1;acceptSend=allowLength=true;swapChat=changeMap=combat=false;draft="Brouillon existant ";sent.clear();
    }
    ~Fixture(){module=nullptr;}
};
template<class Fn>void Reject(Fn action,const char* message){bool failed=false;try{action();}catch(...){failed=true;}Check(failed,message);}
}
namespace managed {
Api api{};uintptr_t gameBase=0;
Klass Api::Class(const char*,const char*,const char* name)const{auto& type=fake::classes[name];if(!type)type=std::make_unique<std::string>(name);return type.get();}
Method Api::FindVerified(Klass,const char* name,int count,uintptr_t rva)const{
    static const std::map<std::string,std::pair<int,uintptr_t>> expected={
        {"zeo",{0,0x185C1E0}},{"GetMapInformationById",{1,0x7C4200}},{"get_subArea",{0,0x7C44B0}},
        {"get_worldmapId",{0,0x7D1040}},{"get_ti_chatInput",{0,0x2EB8FD0}},
        {"IsTextLengthValid",{1,0x2D92630}},{"AppendTextToInputAndFocus",{1,0x2D8E010}},{"TrySendMessage",{1,0x2D9D300}},
        {"GetWorldMapById",{1,0x7D3D50}},{"get_name",{0,0x7D3DE0}}};
    fake::Check(expected.at(name)==std::pair{count,rva},"Exact native notebook contract");
    auto method=std::make_unique<fake::MethodInfo>(fake::MethodInfo{name,rva});auto result=method.get();fake::methods.push_back(std::move(method));return result;
}
Obj Api::Invoke(Method method,Obj self,std::initializer_list<void*> arguments)const{return fake::Invoke(method,self,arguments);}
Obj Api::Call(Obj self,const char* name,std::initializer_list<void*> arguments)const{fake::MethodInfo method{name,0};return Invoke(&method,self,arguments);}
Obj Api::Field(Obj self,const char* name)const{return fake::Get(self).fields[name];}
std::string Api::String(Obj self)const{return self?fake::Get(self).text:std::string{};}
void* Api::FieldAddress(Obj self,const char* name)const{fake::Check(self==fake::view&&std::string(name)=="m_sendingChannel","Only the chat view's sending channel is addressed");return &fake::sendingChannel;}
}
Module* module=nullptr;
Module::Module(std::filesystem::path path):folder(path){}
managed::Obj Module::FindService(const char* name)const{return fake::services[name];}
bool Module::InCombat()const{return fake::combat;}
void StartChatTravel(ChatTpTarget target){fake::Check(target.x==-8&&target.y==42&&target.world==2,"Exact saved destination");++fake::travels;}
void Log(const std::string&){}
int main(){using namespace fake;try{
    Initialize();notebook::InstallNative();
    {Fixture fixture;auto point=notebook::CurrentPosition();Check(point.map==123&&point.x==-8&&point.y==42&&point.world==2,"Position from actual map with native world");notebook::Travel(point);Check(travels==1,"Explicit journey");
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
    Check(handles.empty(),"Native operations release all managed refs");std::cout<<"PASS: native map capture, explicit chat draft append and explicit send on a chosen channel, stale rejection, length checks\n";return 0;
}catch(const std::exception& error){std::cerr<<"FAIL: "<<error.what()<<'\n';return 1;}}
