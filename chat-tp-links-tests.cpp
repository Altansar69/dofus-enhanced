#include "chat-tp-links.hpp"
#include "chat-soul-status.hpp"
#include "chat-coordinate-policy.hpp"
#include "module.hpp"
#include "features.hpp"
#include "options-ui.hpp"
#include <array>
#include <cstring>
#include <iostream>
#include <map>
#include <memory>
#include <set>
#include <vector>

namespace fake {
using namespace managed;
struct Type { std::string name; };
struct Object {
    Type* type; std::string text;
    std::map<std::string,Obj> fields;
    std::map<std::string,int> ints;
    std::vector<Obj> items;
    alignas(16) std::array<char,64> bytes{};
};
struct MethodInfo { std::string name; uintptr_t rva = 0; };
std::map<std::string,std::unique_ptr<Type>> types;
std::vector<std::unique_ptr<Object>> objects;
std::vector<std::unique_ptr<MethodInfo>> methods;
std::map<GCHandle,Obj> handles;
GCHandle nextHandle = 0x100000001ull;
Obj panel = nullptr, service = nullptr;
int clicks = 0, originalClicks = 0, rollouts = 0, parses = 0;
bool decodeFails = false, cleanupFails = false, combat = false;
bool colorLayoutAvailable = true, expectExplicitColors = true;
int nullableAllocations = 0;
int questOpens = 0;
bool questFailure = false;
std::vector<std::string> logs;
std::map<std::pair<int,ChatSoulPart>,std::string> soulCaptions, richSoulCaptions;
bool soulReaderNeeded = false;
ChatTpTarget clicked;
using BindFn = void(*)(Obj,Obj,int,Method);
using TextFn = void(*)(Obj,Obj,Method);
using DecodeFn = bool(*)(Obj,Obj,Obj*,Obj,Method);
struct Vec2 { float x,y; };
using ClickFn = void(*)(Obj,Vec2,Method);
using TooltipFn = Obj(*)(Obj,Vec2,Method);
BindFn bindHook = nullptr; TextFn textHook = nullptr; DecodeFn decodeHook = nullptr; ClickFn clickHook = nullptr;
TooltipFn tooltipHook = nullptr;
void Check(bool value,const char* message) { if (!value) throw std::runtime_error(message); }
Object& Get(Obj p) { if (!p) throw std::runtime_error("Unexpected null fake object"); return *static_cast<Object*>(p); }
Type* Class(const std::string& name) {
    auto& type=types[name]; if(!type)type=std::make_unique<Type>(Type{name}); return type.get();
}
Obj Make(const std::string& name) {
    auto object=std::make_unique<Object>();object->type=Class(name);auto p=object.get();objects.push_back(std::move(object));return p;
}
Obj Str(const std::string& s) { auto p=Make("String");Get(p).text=s;return p; }
template<class T> Obj Box(T value) { auto p=Make("Box");std::memcpy(Get(p).bytes.data(),&value,sizeof(value));return p; }
Method MethodOf(const char* name,uintptr_t rva=0) {
    auto p=std::make_unique<MethodInfo>(MethodInfo{name,rva});auto raw=p.get();methods.push_back(std::move(p));return raw;
}
struct Token { int type=0,padding=0; Obj text=nullptr,manager=nullptr; };
Obj Result() { auto p=Make("HyperlinkDecodeResult");Get(p).fields["sequence"]=Make("List");return p; }
void Text(Obj result,Obj text) { Get(Get(result).fields["sequence"]).items.push_back(Box(Token{0,0,text,nullptr})); }
void Link(Obj result,Obj manager) { Get(Get(result).fields["sequence"]).items.push_back(Box(Token{1,0,nullptr,manager})); }
Obj Map(int x,int y,int world,const std::string& id,const std::string& caption="coords") {
    auto manager=Make("HyperlinkMapInformation"),data=Make("MapHyperlinkTextWrapper");
    Get(data).ints={{"posX",x},{"posY",y},{"worldMapId",world}};Get(data).text=id;Get(data).fields["elementName"]=Str("");
    Get(manager).fields={{"data",data},{"m_bus",service},{"m_tooltipService",service},{"m_themeService",service}};
    Get(manager).text=caption;return manager;
}
Obj MonsterGroup(int x,int y,int world,const std::string& id,int leader=147) {
    auto manager=Map(x,y,world,id,"[20,-34]");
    Get(manager).type=Class("HyperlinkLocalizeMonsterGroup");
    auto data=Get(manager).fields.at("data");
    Get(data).type=Class("LocalizeMonsterGroupHyperlinkTextWrapper");
    Get(data).ints={{"x",x},{"y",y},{"worldMapId",world},{"leaderId",leader}};
    return manager;
}
Obj Build(Obj result) {
    std::string text;
    for(auto value:Get(Get(result).fields["sequence"]).items) {
        auto token=*reinterpret_cast<Token*>(Get(value).bytes.data());
        text+=token.type==0?Get(token.text).text:"<link="+Get(Get(token.manager).fields["data"]).text+">"+Get(token.manager).text+"</link>";
    }
    auto p=Str(text);Get(p).fields["decoded"]=result;return p;
}
bool NativeDecode(Obj,Obj input,Obj* result,Obj,Method) {
    if(decodeFails)return false;
    if(input&&Get(input).fields.contains("decoded")) {
        // A serialized hyperlink can produce fresh managers/wrappers. Returning the
        // exact input objects (the old fixture) hid pointer-only TP ownership bugs.
        auto prior=Get(input).fields.at("decoded");*result=Result();
        for(auto value:Get(Get(prior).fields["sequence"]).items) {
            auto token=*reinterpret_cast<Token*>(Get(value).bytes.data());
            if(token.type==0)Text(*result,token.text);
            else {
                auto manager=Make(Get(token.manager).type->name);Get(manager)=Get(token.manager);
                if(Get(manager).fields.contains("data")) {
                    auto data=Get(manager).fields["data"],copy=Make(Get(data).type->name);
                    Get(copy)=Get(data);Get(manager).fields["data"]=copy;
                }
                Link(*result,manager);
            }
        }
    }
    else {*result=Result();if(input)Text(*result,input);}
    return true;
}
void NativeText(Obj manip,Obj input,Method) {
    Obj result=nullptr;
    if(!decodeHook(service,input,&result,Get(manip).fields["m_hyperlinkResult"],nullptr))return;
    auto built=Build(result);
    if(Get(built).text==Get(input).text)return;
    Get(manip).fields["m_hyperlinkResult"]=result;
    Get(Get(manip).fields["m_textElementTarget"]).fields["text"]=built;
}
void NativeBind(Obj view,Obj,int,Method) {
    for(auto name:{"content","subContent"}) {
        auto label=Get(Get(view).fields["binding"]).fields.at(name);
        textHook(Get(label).fields["manip"],Get(label).fields["input"],nullptr);
    }
}
void NativeClick(Obj,Vec2,Method) { ++originalClicks; }
Obj NativeTooltip(Obj,Vec2,Method) { return Str("Suivre la position"); }
void OnClick(ChatTpTarget target) { ++clicks;clicked=target; }
Obj Invoke(Method method,Obj self,std::initializer_list<void*> arguments) {
    const auto& m=*static_cast<const MethodInfo*>(method);std::vector<void*> args(arguments);const auto& n=m.name;
    if(n=="GetBinding")return Get(self).fields.at("binding");
    if(n=="get_content"||n=="get_subContent")return Get(self).fields.at(n.substr(4));
    if(n=="get_m_sequence")return Get(self).fields.at("sequence");
    if(n=="get_Count")return Box(static_cast<int>(Get(self).items.size()));
    if(n=="get_Item")return Get(self).items.at(*static_cast<int*>(args.at(0)));
    if(n=="get_m_dataContext")return Get(self).fields.at("data");
    if(n=="get_elementName")return Get(self).fields.at("elementName");
    if(n=="get_id")return Str(Get(self).fields.contains("data")?Get(Get(self).fields.at("data")).text:Get(self).text);
    if(n=="get_posX"||n=="get_posY"||n=="get_worldMapId"||n=="get_x"||n=="get_y"||n=="get_leaderId")return Box(Get(self).ints.at(n.substr(4)));
    if(n=="get_isValid"||n=="get_isEnabled")return Box(!Get(self).ints.contains("invalid"));
    if(n=="get_panel")return Get(self).fields["panel"];
    if(n=="CreateTooltipPosition")return Make("TooltipPositioning");
    if(n==".ctor" && Get(self).type->name=="TextTooltipBuilder") {
        Check(m.rva==0x164A2F0 && args.size()==7 && Get(args[1]).type->name=="TooltipPositioning",
            "TP tooltip must use the native positioned text-builder overload");
        Get(self).text=Get(args[0]).text;return nullptr;
    }
    if(n==".ctor" && Get(self).type->name=="HyperlinkShowQuest") {
        Check(m.rva==0x16A6E70&&args.size()==7,"Quest opening must use the verified seven-argument native constructor");
        const auto& parameters=Get(args[1]).items;
        Check(parameters.size()==1&&Get(parameters[0]).text=="439","Ocre action must always target quest439, not the monster ID");
        Check(args[2]==service&&args[3]==service&&args[4]==service,"Quest action must retain the clicked link native services");
        Check(!*static_cast<bool*>(args[5])&&!*static_cast<bool*>(args[6]),"Temporary native quest link uses unboxed empty colors");
        Get(self).ints["questId"]=439;return nullptr;
    }
    if(n=="OnHyperlinkLeftClick" && Get(self).type->name=="HyperlinkShowQuest") {
        Check(m.rva==0x16A6C60&&args.size()==1&&Get(self).ints.at("questId")==439,"Only the verified native show-quest action may run");
        if(questFailure)throw std::runtime_error("Injected quest opening failure");
        ++questOpens;return nullptr;
    }
    if(n=="CreateInstance") { auto p=Make("Array");Get(p).items.resize(*static_cast<int*>(args[1]));return p; }
    if(n=="SetValue") {Get(self).items.at(*static_cast<int*>(args[1]))=args[0];return nullptr;}
    if(n=="AddToSequence") {if(m.rva==0x167EBA0)Link(self,args[0]);else Text(self,args[0]);return nullptr;}
    if(n=="BuildText")return Build(self);
    if(n=="TryParseMapLinks") {
        ++parses;auto input=Get(args[0]).text;auto spans=ChatCoordinateSpans(input);
        Check(spans.size()==1&&spans[0].length==input.size(),"Native lossy parser must receive only a strict candidate");
        int x=0,y=0;Check(sscanf_s(input.c_str(),"[%d,%d]",&x,&y)==2,"Fake expected coordinate pair");
        auto result=Result();Link(result,Map(x,y,1,"plain-"+input));
        *static_cast<Obj*>(args[1])=Build(result);return Box(true);
    }
    if(n=="OnPointerOutLinkTagEvent"||n=="OnRollOutCallback") {
        ++rollouts;if(n=="OnPointerOutLinkTagEvent")Get(self).fields["m_hoveredHyperlinkManager"]=nullptr;return nullptr;
    }
    if(n=="set_m_targetText") {
        if(cleanupFails)throw std::runtime_error("Injected cleanup failure");
        auto label=Get(self).fields.at("m_textElementTarget");Get(label).fields["text"]=args[0];textHook(self,args[0],nullptr);return nullptr;
    }
    throw std::runtime_error("Unexpected fake method "+n);
}
Obj New(Klass type,std::initializer_list<void*> args) {
    auto name=static_cast<Type*>(type)->name;
    if(name=="HyperlinkDecodeResult")return Result();
    if(name=="HyperlinkMapInformation") {
        std::vector<void*> a(args);auto& p=Get(a[1]).items;
        const auto normal=static_cast<char*>(a[5]), hover=static_cast<char*>(a[6]);
        Check(*reinterpret_cast<bool*>(normal)==expectExplicitColors&&*reinterpret_cast<bool*>(hover)==expectExplicitColors,
            "TP colors must be explicit only when the nullable value layout is verified");
        const auto nc=reinterpret_cast<float*>(normal+4), hc=reinterpret_cast<float*>(hover+4);
        Check(!expectExplicitColors||(nc[0]==0.30f&&nc[1]==0.85f&&nc[2]==1.0f&&nc[3]==1.0f&&hc[0]>nc[0]&&hc[3]==1.0f),
            "TP normal cyan and lighter hover color must survive native construction");
        // Native parameter3 is elementName, not id; id hashes the full protocol.
        auto manager=Map(std::stoi(Get(p[0]).text),std::stoi(Get(p[1]).text),std::stoi(Get(p[2]).text),"native-hash-"+Get(p[3]).text,Get(a[0]).text);
        Get(Get(manager).fields["data"]).fields["elementName"]=p[3];return manager;
    }
    throw std::runtime_error("Unexpected fake constructor "+name);
}
Obj MakeLabel(Obj input) {
    auto label=Make("Label"),manip=Make("HyperlinkManipulator");
    Get(label).fields={{"panel",panel},{"manip",manip},{"input",input},{"text",input}};
    Get(manip).fields={{"m_textElementTarget",label},{"m_hyperlinkService",service},{"m_hoveredHyperlinkManager",nullptr},{"m_hyperlinkResult",nullptr}};
    return label;
}
std::vector<Obj> TpManagers(Obj label) {
    std::vector<Obj> result;auto decoded=Get(Get(label).fields["manip"]).fields["m_hyperlinkResult"];
    if(!decoded)return result;
    for(auto value:Get(Get(decoded).fields["sequence"]).items) {
        auto token=*reinterpret_cast<Token*>(Get(value).bytes.data());
        if(token.manager&&(Get(token.manager).text=="TP"||Get(token.manager).text.starts_with("TP · ")||
            Get(token.manager).text.starts_with("TP<color=#D7CFB8> · ")))result.push_back(token.manager);
    }
    return result;
}
void Initialize() {
    api.gchandle_new=[](Obj obj,bool){auto id=nextHandle++;handles[id]=obj;return id;};
    api.gchandle_free=[](GCHandle h){handles.erase(h);};
    api.gchandle_get_target=[](GCHandle h){return handles.at(h);};
    api.object_get_class=[](Obj o)->Klass{return Get(o).type;};
    api.object_unbox=[](Obj o)->void*{return Get(o).bytes.data();};
    // il2cpp_object_new unwraps Nullable<T> to T in this client (0x5DB310).
    // The r42 fixture incorrectly kept a boxed Nullable<Color> alive.
    api.object_new=[](Klass k)->Obj{
        auto name=static_cast<Type*>(k)->name;
        if(name=="NullableColor") { ++nullableAllocations; return Make("Color"); }
        return Make(name);
    };
    api.class_is_valuetype=[](Klass)->bool{return true;};
    api.class_from_type=[](const void* t)->Klass{return const_cast<void*>(t);};
    api.class_get_type=[](Klass k)->const void*{return k;};
    api.method_get_param=[](Method,uint32_t)->const void*{return Class("NullableColor");};
    api.class_get_field_from_name=[](Klass k,const char* name)->void* {
        if(!colorLayoutAvailable||static_cast<Type*>(k)->name!="NullableColor")return nullptr;
        if(std::string(name)=="hasValue")return reinterpret_cast<void*>(16);
        if(std::string(name)=="value")return reinterpret_cast<void*>(20);
        return nullptr;
    };
    api.field_get_offset=[](void* field)->size_t{return reinterpret_cast<size_t>(field);};
    api.type_get_object=[](const void*)->Obj{return Make("RuntimeType");};
    api.string_new=[](const char* s)->Obj{return Str(s);};
    api.gc_wbarrier_set_field=[](Obj,Obj* field,Obj value){*field=value;};
    panel=Make("Panel");service=Make("Service");
}
void Run() {
    const auto rolloutsBefore=rollouts;
    for(auto text:{"[]","[","[1,2","[[1,2]]","[1,2,3]","[+-,2]","[+-1,2]","[2147483648,0]","<link='[1,2]'>x</link>"})
        Check(ChatCoordinateSpans(text).empty(),"Malformed or markup coordinate must remain literal");
    Check(ChatCoordinateSpans("abc [-12, +3] puis [0,0]").size()==2,"Signed/space/multiple coordinates");
    auto original=Map(-4,8,2,"native-map"),source=Result();Link(source,original);Text(source,Str(" [] [bad [9,10] fin"));
    auto input=Build(source),view=Make("ChatView"),binding=Make("ChatItemBinding"),row=Make("Row");
    auto label=MakeLabel(input),sub=MakeLabel(Str("simple"));Get(binding).fields={{"content",label},{"subContent",sub}};Get(view).fields["binding"]=binding;
    bindHook(view,row,0,nullptr);
    auto tp=TpManagers(label);Check(tp.size()==2&&clicks==0,"Rendering adds map and plain-coordinate TP without travel");
    Check(Get(input).text.find("TP")==std::string::npos,"Original chat data must not change");
    Check(Get(Get(label).fields["text"]).text.find(" [] [bad ")!=std::string::npos,"Invalid brackets remain literal");
    clickHook(tp[0],{},nullptr);Check(clicks==1&&clicked==ChatTpTarget{-4,8,2},"Explicit world is preserved by exact owned TP click");
    clickHook(tp[1],{},nullptr);Check(clicks==2&&clicked==ChatTpTarget{9,10,1},"Plain coordinate uses native world1");
    clickHook(original,{},nullptr);Check(originalClicks==1&&clicks==2,"Ordinary map click stays native");
    Check(Get(tooltipHook(original,{},nullptr)).text=="Suivre la position","Original coordinate hover stays native");
    auto foreign=MakeLabel(input);textHook(Get(foreign).fields["manip"],input,nullptr);Check(TpManagers(foreign).empty(),"Outside chat label is never decorated");
    auto manip=Get(label).fields["manip"];Get(manip).fields["m_hoveredHyperlinkManager"]=tp[0];
    const auto stableText=Get(Get(label).fields["text"]).text;
    const auto rootedHandles=handles.size();
    for(int repeat=0;repeat<100;++repeat) {
        textHook(manip,Get(label).fields["text"],nullptr);
        Check(TpManagers(label)==tp&&Get(Get(label).fields["text"]).text==stableText,"100 unchanged redraws retain exact TP identities and text");
    }
    Check(handles.size()==rootedHandles&&rollouts==rolloutsBefore&&clicks==2,"Unchanged refresh does not grow managed roots, clear hover or travel");
    for(int repeat=0;repeat<100;++repeat) {
        auto rebuilt=Result();Text(rebuilt,Str("refresh "));
        for(auto token:Get(Get(Get(manip).fields["m_hyperlinkResult"]).fields["sequence"]).items)
            Get(Get(rebuilt).fields["sequence"]).items.push_back(token);
        textHook(manip,Build(rebuilt),nullptr);
        Check(TpManagers(label)==tp,"100 changed redraws with fresh managers keep only the original two TP actions");
    }
    Check(rollouts==rolloutsBefore+1&&handles.size()==rootedHandles&&clicks==2,"Rebuilt text closes owned hover without growing roots or travelling");
    auto inflated=Result();Link(inflated,original);
    for(int repeat=0;repeat<50;++repeat) {
        Text(inflated,Str(" "));auto old=Map(-4,8,2,"old-hash-"+std::to_string(repeat),"TP");
        Get(Get(old).fields["data"]).fields["elementName"]=Str("codex-tp-123-"+std::to_string(repeat+1));Link(inflated,old);
    }
    Text(inflated,Str(" texte TP conservé "));
    auto ordinary=Map(1,1,1,"ordinary-caption-tp","TP");Link(inflated,ordinary);
    textHook(manip,Build(inflated),nullptr);
    Check(TpManagers(label).size()==3,"Fifty serialized legacy mod links collapse to one per source; ordinary TP-caption map survives");
    Check(Get(Get(label).fields["text"]).text.find("texte TP conservé")!=std::string::npos&&
        Get(Get(label).fields["text"]).text.find("ordinary-caption-tp>TP</link>")!=std::string::npos,"Literal TP and unrelated native TP link are untouched");
    auto currentTp=TpManagers(label)[0];clickHook(currentTp,{},nullptr);
    Check(clicks==3&&clicked==ChatTpTarget{-4,8,2},"Recovered TP keeps exact target and explicit click behavior");
    // Rebind the same virtualized label to a new message and then to plain text.
    auto replacement=Result();Link(replacement,Map(12,-7,4,"new-message"));
    Get(label).fields["input"]=Build(replacement);bindHook(view,row,1,nullptr);
    auto replacementTp=TpManagers(label);Check(replacementTp.size()==1,"Recycling row replaces previous links");
    clickHook(currentTp,{},nullptr);Check(clicks==3&&originalClicks==1,"Stale mod action cannot fall through to native map click");
    clickHook(replacementTp[0],{},nullptr);Check(clicks==4&&clicked==ChatTpTarget{12,-7,4},"Recycled row uses its current destination");
    textHook(manip,Str("TP est un mot normal"),nullptr);
    Check(TpManagers(label).empty(),"Plain rebind clears old owned actions");
    bindHook(view,row,1,nullptr);Check(TpManagers(label).size()==1,"Coordinate rebind decorates again exactly once");
    Get(label).fields["panel"]=nullptr;clickHook(TpManagers(label)[0],{},nullptr);Check(clicks==4,"Detached link cannot travel");Get(label).fields["panel"]=panel;
    module->stopRequested=true;clickHook(TpManagers(label)[0],{},nullptr);Check(clicks==4,"Stopping link cannot travel");
    cleanupFails=true;RestoreChatTpLinks();Check(ChatTpLinksCleanupPending(),"Failed cleanup must retain owned references for retry");
    cleanupFails=false;RestoreChatTpLinks();Check(!ChatTpLinksCleanupPending(),"Cleanup retry restores rows");
    Check(Get(Get(label).fields["text"]).text.find(">TP</link>")==std::string::npos,"Stop removes added TP from visible text");
}
void RecruitmentLinks() {
    const auto before=clicks;
    auto source=Result();Text(source,Str("(Recrutement) Enucalot\xC2\xA0:\xC2\xA0Rooroku l'Imposant "));
    auto group=MonsterGroup(20,-34,2,"monster-group-position");Link(source,group);
    auto input=Build(source),label=MakeLabel(input),sub=MakeLabel(Str("")),view=Make("ChatView"),binding=Make("ChatItemBinding");
    Get(binding).fields={{"content",label},{"subContent",sub}};Get(view).fields["binding"]=binding;
    bindHook(view,Make("Row"),0,nullptr);
    auto tp=TpManagers(label);
    Check(tp.size()==1,"Recruitment monster-group position must receive one TP action");
    Check(Get(tooltipHook(tp[0],{},nullptr)).text=="TP vers le zaap le plus proche, puis trajet vers [20,-34].",
        "Added TP must describe teleport and onward travel rather than position tracking");
    combat=true;Check(Get(tooltipHook(tp[0],{},nullptr)).text=="TP indisponible pendant un combat.","Combat tooltip must state travel unavailability");combat=false;
    Check(clicks==before && Get(input).text.find(">TP</link>")==std::string::npos,"Rendering group position must not travel or change original message");
    clickHook(tp[0],{},nullptr);Check(clicks==before+1 && clicked==ChatTpTarget{20,-34,2},"Group position must preserve the embedded world and coordinates");
    auto manip=Get(label).fields.at("manip");
    for(int i=0;i<50;++i)textHook(manip,Get(label).fields.at("text"),nullptr);
    Check(TpManagers(label)==tp,"Group refresh must keep exactly one stable TP");
    auto old=tp[0];
    auto text=Str("(Recrutement) Scatach : Ouverture du krala 10/12 en  [-52,15] 10/12 zn  [-56,4] 12/12 en  [-49,9] et 7/12 en  [-55,11] Il manque peu de personnes, venez !");
    textHook(manip,text,nullptr);tp=TpManagers(label);
    Check(tp.size()==4,"Four recruitment positions must each get their own TP");
    const ChatTpTarget targets[]={{-52,15,1},{-56,4,1},{-49,9,1},{-55,11,1}};
    for(int i=0;i<4;++i){clickHook(tp[i],{},nullptr);Check(clicked==targets[i],"Each TP must keep its own destination");}
    const auto travelled=clicks;clickHook(old,{},nullptr);Check(clicks==travelled,"Old group TP must remain inert after row recycling");
    Check(tooltipHook(old,{},nullptr)==nullptr,"Retired TP must not show native follow tooltip");
    RestoreChatTpLinks();
}
std::vector<Obj> InfoManagers(Obj label) {
    std::vector<Obj> result;auto decoded=Get(Get(label).fields["manip"]).fields["m_hyperlinkResult"];
    if(!decoded)return result;
    for(auto value:Get(Get(decoded).fields["sequence"]).items) {
        auto token=*reinterpret_cast<Token*>(Get(value).bytes.data());
        if(token.manager&&Get(token.manager).text.starts_with("<color=#D7CFB8>"))result.push_back(token.manager);
    }
    return result;
}
void SetSoul(int monster,const std::string& inventory,const std::string& ocre,
    const std::string& richInventory,const std::string& richOcre) {
    soulCaptions[{monster,ChatSoulPart::Inventory}]=inventory;soulCaptions[{monster,ChatSoulPart::Ocre}]=ocre;
    richSoulCaptions[{monster,ChatSoulPart::Inventory}]=richInventory;richSoulCaptions[{monster,ChatSoulPart::Ocre}]=richOcre;
}
size_t SeparatorCount(const std::string& text) {
    size_t count=0,offset=0;
    while((offset=text.find(" · ",offset))!=std::string::npos){++count;offset+=4;}
    return count;
}
void SoulStatusLinks() {
    SetSoul(147,"Âme en sac","Ocre à faire","<color=#8DDB91>Âme en sac</color>","<color=#F18C8C>Ocre à faire</color>");
    SetSoul(2477,"Pas en sac","Ocre validé","<color=#F18C8C>Pas en sac</color>","<color=#8DDB91>Ocre validé</color>");
    auto source=Result();Link(source,MonsterGroup(7,8,1,"boss",147));Text(source,Str(" puis · "));
    Link(source,MonsterGroup(7,8,1,"arch",2477));Text(source,Str(" [1,2]"));
    auto input=Build(source),label=MakeLabel(input),sub=MakeLabel(Str("")),view=Make("ChatView"),binding=Make("ChatItemBinding");
    Get(binding).fields={{"content",label},{"subContent",sub}};Get(view).fields["binding"]=binding;
    const auto clicksBefore=clicks,nativeClicksBefore=originalClicks;bindHook(view,Make("Row"),0,nullptr);
    auto tp=TpManagers(label),info=InfoManagers(label);auto manip=Get(label).fields["manip"];
    Check(tp.size()==3&&info.size()==4,"Two monster links need independent TP, bag and Ocre zones; plain coordinates only TP");
    Check(Get(info[0]).text=="<color=#D7CFB8><color=#8DDB91>Âme en sac</color></color>"&&
        Get(info[1]).text=="<color=#D7CFB8><color=#F18C8C>Ocre à faire</color></color>","Each hover area must retain its own status color");
    Check(Get(input).text.find("Ocre")==std::string::npos&&clicks==clicksBefore,"Decoration must not change original message or initiate travel");
    const auto text=Get(Get(label).fields["text"]).text;
    Check(text.find("boss>[20,-34]</link> · <link=")!=std::string::npos&&
        text.find(">TP</link> · <link=")!=std::string::npos&&text.find("</color></link> · <link=")!=std::string::npos,
        "A noninteractive separator must appear between coordinates, TP, bag and Ocre");
    Check(Get(tooltipHook(tp[0],{},nullptr)).text=="TP vers le zaap le plus proche, puis trajet vers [7,8].","TP tooltip must contain only travel information");
    Check(Get(tooltipHook(info[0],{},nullptr)).text=="Inventory detail: Âme en sac"&&
        Get(tooltipHook(info[1],{},nullptr)).text=="Ocre detail: Ocre à faire\nClique pour ouvrir L’éternelle moisson dans le journal de quêtes.","Bag and Ocre must dispatch distinct exact-monster tooltips");
    const auto opensBefore=questOpens;
    for(auto manager:info)clickHook(manager,{},nullptr);
    Check(questOpens==opensBefore+2,"Only the two Ocre zones must open the journal");
    Check(clicks==clicksBefore&&originalClicks==nativeClicksBefore,"Information labels must never teleport or fall through to native follow action");
    combat=true;
    Check(Get(tooltipHook(tp[0],{},nullptr)).text=="TP indisponible pendant un combat."&&
        Get(tooltipHook(info[1],{},nullptr)).text=="Ocre detail: Ocre à faire\nClique pour ouvrir L’éternelle moisson dans le journal de quêtes.","Combat only changes the travel tooltip");combat=false;
    const auto rootedHandles=handles.size();
    for(int i=0;i<100;++i)TickChatTpLinks();
    Check(soulReaderNeeded&&TpManagers(label)==tp&&InfoManagers(label)==info&&handles.size()==rootedHandles,"Stable ticks must retain all three manager identities and root count");
    Get(manip).fields["m_hoveredHyperlinkManager"]=info[0];const auto rolloutsBefore=rollouts;
    SetSoul(147,"Pas en sac","Ocre validé","<color=#F18C8C>Pas en sac</color>","<color=#8DDB91>Ocre validé</color>");
    TickChatTpLinks();auto changed=InfoManagers(label);
    Check(TpManagers(label)==tp&&changed.size()==4&&changed[0]!=info[0]&&changed[1]!=info[1]&&changed[2]==info[2]&&changed[3]==info[3],
        "Status changes rebuild only changed info areas, preserving TP and unrelated monster links");
    Check(rollouts==rolloutsBefore+1,"Status rebuild must close the previous info tooltip");
    Check(Get(tooltipHook(changed[0],{},nullptr)).text=="Inventory detail: Pas en sac"&&
        Get(tooltipHook(changed[1],{},nullptr)).text=="Ocre detail: Ocre validé\nClique pour ouvrir L’éternelle moisson dans le journal de quêtes.","Tooltip content must follow fresh status");
    clickHook(info[0],{},nullptr);Check(tooltipHook(info[1],{},nullptr)==nullptr&&clicks==clicksBefore&&originalClicks==nativeClicksBefore,"Retired info managers must be inert");
    clickHook(tp[0],{},nullptr);Check(clicks==clicksBefore+1&&clicked==ChatTpTarget{7,8,1},"Only TP initiates the exact target journey");
    SetSoul(147,"Sac : ?","Ocre : ?","Sac : ?","Ocre : ?");TickChatTpLinks();
    Check(Get(InfoManagers(label)[0]).text=="<color=#D7CFB8>Sac : ?</color>"&&Get(InfoManagers(label)[1]).text=="<color=#D7CFB8>Ocre : ?</color>","Unknown states return to independent neutral areas");
    for(int i=0;i<50;++i) {
        auto rebuilt=Result();Text(rebuilt,Str("refresh "));
        for(auto token:Get(Get(Get(manip).fields["m_hyperlinkResult"]).fields["sequence"]).items)Get(Get(rebuilt).fields["sequence"]).items.push_back(token);
        textHook(manip,Build(rebuilt),nullptr);
        Check(TpManagers(label)==tp&&InfoManagers(label).size()==4,"Serialized redraws must not duplicate any zone or separator");
        Check(SeparatorCount(Get(Get(label).fields["text"]).text)==8,"Serialized redraws must retain seven generated separators and one original separator");
    }
    const auto beforeRecycle=InfoManagers(label);
    textHook(manip,Str("recycled [9,10]"),nullptr);TickChatTpLinks();
    Check(!soulReaderNeeded&&TpManagers(label).size()==1&&InfoManagers(label).empty(),"Recycled coordinate row discards status areas and suspends reads");
    clickHook(beforeRecycle[0],{},nullptr);Check(tooltipHook(beforeRecycle[1],{},nullptr)==nullptr,"Recycled info hover cannot advertise native tracking");
    textHook(manip,input,nullptr);RestoreChatTpLinks();
    const auto cleaned=Get(Get(label).fields["text"]).text;
    Check(cleaned.find("Ocre")==std::string::npos&&cleaned.find("TP")==std::string::npos&&cleaned.find("<color=")==std::string::npos&&
        cleaned.find("</link> · <link=")==std::string::npos&&cleaned.find(" puis · ")!=std::string::npos&&SeparatorCount(cleaned)==1,
        "Cleanup removes all owned zones and separators while preserving original punctuation");
    // Old combined captions are recognized by ownership marker, never visible text.
    auto legacy=Result();Link(legacy,MonsterGroup(7,8,1,"legacy-group",147));Text(legacy,Str(" "));
    auto old=Map(7,8,1,"old-combined","TP<color=#D7CFB8> · Pas en sac · Ocre à faire</color>");
    Get(Get(old).fields["data"]).fields["elementName"]=Str("codex-tp-123-999");Link(legacy,old);
    Get(label).fields["input"]=Build(legacy);bindHook(view,Make("Row"),0,nullptr);
    Check(TpManagers(label).size()==1&&InfoManagers(label).size()==2,"Legacy combined link migrates to exactly three zones");
    RestoreChatTpLinks();
    auto many=Result();for(int i=0;i<17;++i)Link(many,MonsterGroup(i,8,1,"bounded-"+std::to_string(i),147));
    Get(label).fields["input"]=Build(many);bindHook(view,Make("Row"),0,nullptr);
    Check(TpManagers(label).size()==16&&InfoManagers(label).size()==32,"Limit remains 16 targets with complete info pairs, not 16 managers");
    RestoreChatTpLinks();soulCaptions.clear();richSoulCaptions.clear();
}

void OcreJournalClick() {
    const auto travelBefore=clicks,nativeBefore=originalClicks;
    auto source=Result();Link(source,MonsterGroup(7,8,1,"journal",147));
    auto label=MakeLabel(Build(source)),sub=MakeLabel(Str("")),view=Make("ChatView"),binding=Make("ChatItemBinding");
    Get(binding).fields={{"content",label},{"subContent",sub}};Get(view).fields["binding"]=binding;
    SetSoul(147,"Pas en sac","Ocre à faire","Pas en sac","Ocre à faire");bindHook(view,Make("Row"),0,nullptr);
    for(const auto& status:{"Ocre à faire","Ocre validé","Ocre non commencée","Ocre : ?"}) {
        SetSoul(147,"Pas en sac",status,"Pas en sac",status);TickChatTpLinks();
        auto info=InfoManagers(label);const auto before=questOpens;const auto rooted=handles.size();
        Check(Get(tooltipHook(info[1],{},nullptr)).text.find("Clique pour ouvrir L’éternelle moisson")!=std::string::npos,"Ocre tooltip advertises its journal action");
        TickChatTpLinks();Check(questOpens==before,"Hover and refresh must not open the journal");
        clickHook(info[0],{},nullptr);Check(questOpens==before,"Bag status has no journal action");
        clickHook(info[1],{4,9},nullptr);
        Check(questOpens==before+1&&handles.size()==rooted,"Ocre click dispatches once and releases temporary native objects for every displayed state");
    }
    auto ocre=InfoManagers(label)[1];const auto before=questOpens;
    Get(label).fields["panel"]=nullptr;clickHook(ocre,{},nullptr);Get(label).fields["panel"]=panel;
    module->stopRequested=true;clickHook(ocre,{},nullptr);module->stopRequested=false;
    module->servicesReady=false;clickHook(ocre,{},nullptr);module->servicesReady=true;
    auto cached=module->cached.get();module->cached.reset(Make("OtherCharacter"));clickHook(ocre,{},nullptr);module->cached.reset(cached);
    Check(questOpens==before,"Detached, stopping, unavailable and previous-character Ocre links must be inert");
    Get(ocre).fields["m_bus"]=nullptr;clickHook(ocre,{},nullptr);Get(ocre).fields["m_bus"]=service;
    questFailure=true;clickHook(ocre,{},nullptr);questFailure=false;
    Check(questOpens==before&&clicks==travelBefore&&originalClicks==nativeBefore,"Missing services or native failure must never fall through to travel or native map follow");
    clickHook(ocre,{},nullptr);Check(questOpens==before+1,"An explicit retry must recover from native quest opening failure");
    auto manip=Get(label).fields["manip"];textHook(manip,Str("recycled"),nullptr);
    clickHook(ocre,{},nullptr);Check(questOpens==before+1,"Recycled Ocre link must not open an old journal action");
    RestoreChatTpLinks();soulCaptions.clear();richSoulCaptions.clear();
}

}
namespace managed {
Api api;uintptr_t gameBase=0;
Klass Api::Class(const char*,const char*,const char* name)const{return fake::Class(name);}
Klass Api::NestedClass(Klass,const char* name)const{return fake::Class(name);}
Method Api::Find(Klass,const char* name,int)const{return fake::MethodOf(name);}
Method Api::FindVerified(Klass,const char* name,int,uintptr_t rva)const{return fake::MethodOf(name,rva);}
Obj Api::Invoke(Method m,Obj self,std::initializer_list<void*> args)const{return fake::Invoke(m,self,args);}
Obj Api::Call(Obj self,const char* name,std::initializer_list<void*> args)const{return Invoke(fake::MethodOf(name),self,args);}
Obj Api::New(Klass type,std::initializer_list<void*> args)const{return fake::New(type,args);}
Obj Api::Field(Obj self,const char* name)const{return fake::Get(self).fields.at(name);}
void* Api::FieldAddress(Obj self,const char* name)const{
    if(fake::Get(self).type->name=="Color"&&(std::string(name)=="value"||std::string(name)=="hasValue"))
        throw std::runtime_error(std::string("Field unavailable: ")+name);
    if(std::string(name)=="m_isHovered")return &fake::Get(self).ints[name];
    return &fake::Get(self).fields[name];
}
std::string Api::String(Obj self)const{return self?fake::Get(self).text:std::string{};}
}
Module* module=nullptr;
Module::Module(std::filesystem::path path):folder(std::move(path)){}
bool Module::InCombat()const{return fake::combat;}
void Log(const std::string& message){fake::logs.push_back(message);}
std::string ChatSoulCaption(int monster,ChatSoulPart part,bool colorStatus){
    const auto& captions=colorStatus?fake::richSoulCaptions:fake::soulCaptions;
    auto it=captions.find({monster,part});return it==captions.end()?std::string{}:it->second;
}
std::string ChatSoulDetail(int monster,ChatSoulPart part){return std::string(part==ChatSoulPart::Inventory?"Inventory detail: ":"Ocre detail: ")+ChatSoulCaption(monster,part);}
void TickChatSoulStatus(bool needed){fake::soulReaderNeeded=needed;}
void RestoreChatSoulStatus(){fake::soulReaderNeeded=false;}
void AddHook(managed::Method,uintptr_t rva,void* hook,void** original){
    using namespace fake;
    if(rva==0x2D8E4C0){bindHook=reinterpret_cast<BindFn>(hook);*original=reinterpret_cast<void*>(&NativeBind);}
    else if(rva==0x168D990){textHook=reinterpret_cast<TextFn>(hook);*original=reinterpret_cast<void*>(&NativeText);}
    else if(rva==0x169CE90){decodeHook=reinterpret_cast<DecodeFn>(hook);*original=reinterpret_cast<void*>(&NativeDecode);}
    else if(rva==0x168E880){clickHook=reinterpret_cast<ClickFn>(hook);*original=reinterpret_cast<void*>(&NativeClick);}
    else if(rva==0x168E760){tooltipHook=reinterpret_cast<TooltipFn>(hook);*original=reinterpret_cast<void*>(&NativeTooltip);}
    else throw std::runtime_error("Unexpected chat hook");
}
int main(int argc,char** argv){
    try{fake::Initialize();Module owner("offline");module=&owner;owner.ready=true;owner.servicesReady=true;owner.cached.reset(fake::Make("Cached"));
        const bool fallback=argc==2&&std::string(argv[1])=="--missing-color-layout";
        fake::colorLayoutAvailable=!fallback;fake::expectExplicitColors=!fallback;
        InstallChatTpLinks(fake::OnClick);
        if(fallback)fake::Check(!fake::logs.empty()&&fake::logs.back().starts_with("CHAT_TP_COLOR_FALLBACK "),
            "Unavailable cosmetic layout must preserve chat TP with a diagnostic instead of aborting module startup");
        fake::RecruitmentLinks();fake::SoulStatusLinks();fake::OcreJournalClick();fake::clicks=0;fake::Run();
        fake::Check(fake::nullableAllocations==0,"Nullable<Color> must never be allocated or boxed");
        module=nullptr;
        std::cout<<"PASS: native chat scope, coordinate preservation, explicit click, deduplication, hover and cleanup retries\n";return 0;
    }catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}
}
