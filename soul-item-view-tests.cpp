#include "soul-item-view.hpp"
#include "module.hpp"
#include "features.hpp"
#include "options-ui.hpp"
#include <array>
#include <cstring>
#include <iostream>
#include <map>
#include <memory>
#include <unordered_map>
#include <vector>

// Offline managed-contract fixture: no game binaries or running process access.
namespace fake {
using namespace managed;
struct Class { std::string name; };
struct MethodInfo { Class* type; std::string name; int argc; };
struct Object { Class* type; std::map<std::string,Obj> fields; std::map<std::string,Obj> tips; std::string text; int scalar=0, slotSize=-1, slotType=-1, sourceOrigin=-1, tooltipOrigin=-1; bool attached=true; };
std::map<std::string,std::unique_ptr<Class>> classes;
std::vector<std::unique_ptr<Object>> objects;
std::vector<std::unique_ptr<MethodInfo>> methods;
std::unordered_map<GCHandle,Obj> handles;
GCHandle nextHandle=0xDCA000000000ULL;
std::map<std::string,Obj> services;
std::vector<std::string> logs;
std::string failing;
int failures=0, binds=0, shows=0, hides=0, updates=0, clicks=0, enters=0, leaves=0, detaches=0, refreshes=0;
Obj latestSlot=nullptr, latestBuilder=nullptr;
std::map<Obj,int64_t> cursorOwners;
using EventFn=void(*)(Obj,Obj,Method);using RefreshFn=void(*)(Obj,Method);
EventFn enterHook=nullptr,leaveHook=nullptr,detachHook=nullptr,clickHook=nullptr;
RefreshFn refreshHook=nullptr;
void Check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
Class* Type(const std::string& name){auto& t=classes[name];if(!t)t=std::make_unique<Class>(Class{name});return t.get();}
Object& Get(Obj o){Check(o!=nullptr,"Null fake object");return *static_cast<Object*>(o);}
Obj Make(const std::string& type){auto o=std::make_unique<Object>();o->type=Type(type);auto p=o.get();objects.push_back(std::move(o));return p;}
Obj String(const std::string& s){auto o=Make("String");Get(o).text=s;return o;}
void OriginalEnter(Obj,Obj,Method){++enters;}
void OriginalLeave(Obj,Obj,Method){++leaves;}
void OriginalDetach(Obj owner,Obj,Method){++detaches;Get(owner).attached=false;}
void OriginalClick(Obj,Obj,Method){++clicks;}
void OriginalRefresh(Obj,Method){++refreshes;}
void Initialize(){
 api.object_new=[](Klass type)->Obj{return Make(static_cast<Class*>(type)->name);};
 api.object_get_class=[](Obj o)->Klass{return Get(o).type;};
 api.class_is_assignable_from=[](Klass a,Klass b){return a==b;};
 api.object_unbox=[](Obj o)->void*{return &Get(o).scalar;};
 api.string_new=[](const char* s)->Obj{return String(s);};
 api.gchandle_new=[](Obj o,bool)->GCHandle{auto h=++nextHandle;handles[h]=o;return h;};
 api.gchandle_get_target=[](GCHandle h)->Obj{return handles.at(h);};
 api.gchandle_free=[](GCHandle h){Check(handles.erase(h)==1,"Invalid GC handle release");};
}
Obj Invoke(Method method,Obj self,std::initializer_list<void*> arguments){
 const auto& m=*static_cast<const MethodInfo*>(method);const std::vector<void*> args(arguments);
 Check(static_cast<int>(args.size())==m.argc,"Wrong managed argc");
 if(m.name==failing&&failures-->0)throw std::runtime_error("Injected "+m.name+" failure");
 if(!self){
  if(m.type->name=="DofusCursor") {
   Check(*static_cast<int*>(args[0])==3,"Owned hover must use native UI cursor layer");
   if(m.name=="SetCursor") {
    Check(args.size()==4&&*static_cast<int64_t*>(args[1])==2097152&&*static_cast<int*>(args[2])==0,"Hand cursor native enum widths and values");
    Check(Get(args[3]).slotType==4,"Only Capture spell owns a hand cursor");
    cursorOwners[args[3]]=*static_cast<int64_t*>(args[1]);return nullptr;
   }
   Check(m.name=="RemoveCursor"&&args.size()==2,"Native cursor cleanup contract");cursorOwners.erase(args[1]);return nullptr;
  }
  Check(m.type->name=="ItemTooltipBuilder"&&m.name=="CreateItemTooltip"&&args.size()==9,"Unexpected factory call");
  Check(args[0]==services["fqm"]&&args[1]==services["fkq"],"Stale tooltip dependencies");
  Check(Get(args[2]).type==Type("mc")&&Get(args[3]).type==Type("TooltipPositioning"),"Factory actual item and positioning ABI");
  for(auto i:{4,5,6,8})Check(!*static_cast<bool*>(args[i]),"Preview must be nonpinnable, noncontextual");
  Check(args[7]==nullptr,"No drop information expected");
  auto b=Make("ItemTooltipBuilder");Get(b).fields["item"]=args[2];latestBuilder=b;return b;
 }
 auto& o=Get(self);
 if(m.name=="set_sourcePlacementOrigin"){o.sourceOrigin=*static_cast<int*>(args[0]);return nullptr;}
 if(m.name=="set_tooltipPlacementOrigin"){o.tooltipOrigin=*static_cast<int*>(args[0]);return nullptr;}
 if(m.name==".ctor"&&o.type->name=="SpellTooltipInfo") {Check(args.size()==2&&Get(args[0]).type==Type("ht")&&args[1]==nullptr,"Real known spell tooltip info");o.fields["spell"]=args[0];return nullptr;}
 if(m.name==".ctor"&&o.type->name=="SpellTooltipBuilder") {Check(args.size()==4&&args[1]==services["frc"]&&args[2]==services["fkq"]&&Get(args[3]).type==Type("TooltipPositioning"),"Native spell tooltip dependencies/positioning overload");Check(Get(args[3]).sourceOrigin==1&&Get(args[3]).tooltipOrigin==7,"Capture tooltip must prefer above its spell slot");o.fields["spell"]=Get(args[0]).fields["spell"];latestBuilder=self;return nullptr;}
 if(m.name=="SetEnabled") {o.fields["disabled"]=*static_cast<bool*>(args[0])?nullptr:self;if(o.fields["disabled"])cursorOwners.erase(self);return nullptr;}
 if(m.name=="get_enabledInHierarchy") {auto box=Make("Box");Get(box).scalar=!o.fields["disabled"];return box;}
 if(m.name=="Add"){Get(args[0]).fields["parent"]=self;return nullptr;}
 if(m.name=="get_panel")return o.attached?self:nullptr;
 if(m.name=="get_resolvedStyle")return self;
 if(m.name=="get_display"){auto b=Make("Box");Get(b).scalar=o.scalar;return b;}
 if(m.name=="get_parent")return o.fields["parent"];
 if(m.name=="DisableDrag"){o.fields["dragDisabled"]=self;return nullptr;}
 if(m.name=="set_slotSize"){Check(o.type==Type("Slot"),"Size applies only to native Slot");o.slotSize=*static_cast<int*>(args[0]);return nullptr;}
 if(m.name=="set_slotType"){Check(o.type==Type("Slot"),"Type applies only to native Slot");o.slotType=*static_cast<int*>(args[0]);return nullptr;}
 if(m.name=="set_data"){Check(o.type==Type("Slot"),"Only slot data setter");++binds;o.fields["data"]=args[0];refreshHook(self,nullptr);if(failing=="partial_set_data"&&failures-->0)throw std::runtime_error("Injected partial bind");return nullptr;}
 if(m.name=="RemoveFromHierarchy"){detachHook(self,nullptr,nullptr);o.fields["parent"]=nullptr;return nullptr;}
 if(m.name=="set_nameId"){o.text=Get(args[0]).text;return nullptr;}
 if(m.name=="bfqc"){Check(o.type==Type("gbr"),"Tooltip query service ABI");return o.tips[Get(args[0]).text];}
 if(m.name=="bfpo"){++shows;auto& b=Get(args[0]);Check(b.text!="standard"&&!b.text.empty(),"Must own unique tooltip name");o.tips[b.text]=args[0];return String(b.text);}
 if(m.name=="bfps"){Check(args.size()==2&&!*static_cast<bool*>(args[1]),"Hide ABI");++hides;o.tips.erase(Get(args[0]).text);return nullptr;}
 if(m.name=="UpdateContent"){Check(args[0]==o.fields["item"],"In-place update must keep actual item");++updates;return nullptr;}
 throw std::runtime_error("Unexpected managed call "+o.type->name+"."+m.name);
}
}
namespace managed {
Api api{};uintptr_t gameBase=0;
Klass Api::Class(const char*,const char*,const char* name)const{return fake::Type(name);}
Method Api::Find(Klass k,const char* n,int c)const{auto m=std::make_unique<fake::MethodInfo>(fake::MethodInfo{static_cast<fake::Class*>(k),n,c});auto p=m.get();fake::methods.push_back(std::move(m));return p;}
Obj Api::Invoke(Method m,Obj self,std::initializer_list<void*> args)const{return fake::Invoke(m,self,args);}
Obj Api::Call(Obj self,const char* n,std::initializer_list<void*> args)const{return Invoke(Find(object_get_class(self),n,static_cast<int>(args.size())),self,args);}
Obj Api::New(Klass k,std::initializer_list<void*> arguments)const{
 const auto name=static_cast<fake::Class*>(k)->name;const std::vector<void*> args(arguments);auto o=fake::Make(name);
 if(name=="Slot"){fake::Check(args.empty(),"Slot ctor argc");fake::latestSlot=o;}
 else {fake::Check(name=="TooltipPositioning"&&args.size()==1&&args[0]==fake::latestSlot,"Positioning source ctor ABI");}
 return o;
}
std::string Api::String(Obj o)const{return fake::Get(o).text;}
void* Api::Native(Method m,uintptr_t rva)const{
 const auto& v=*static_cast<const fake::MethodInfo*>(m);
 const std::map<uintptr_t,std::pair<std::string,int>> expected={
 {0x280A070,{"SetCursor",4}},{0x2809EA0,{"RemoveCursor",2}},
 {0x7D2B60,{"set_sourcePlacementOrigin",1}},{0x758DE0,{"set_tooltipPlacementOrigin",1}},
 {0x165E8E0,{".ctor",0}},{0x165EC60,{"set_data",1}},{0x165F4F0,{"set_slotSize",1}},{0x165ADC0,{"DisableDrag",0}},
 {0x165F5F0,{"set_slotType",1}},
 {0x1EEE490,{"bfpo",1}},{0x1EEF050,{"bfps",2}},{0x1EEFF60,{"bfqc",1}},
 {0x1647FD0,{".ctor",2}},{0x1647AC0,{".ctor",4}},{0x1617AA0,{"UpdateContent",2}},{0x1666720,{".ctor",1}},{0x1610130,{"CreateItemTooltip",9}}};
 const auto& e=expected.at(rva);fake::Check(v.name==e.first&&v.argc==e.second,"Native guard contract mismatch");return reinterpret_cast<void*>(rva);
}
}
Module* module=nullptr;
Module::Module(std::filesystem::path p):folder(std::move(p)){}
managed::Obj Module::FindService(const char* name)const{return fake::services[name];}
void Log(const std::string& s){fake::logs.push_back(s);}
void AddHook(managed::Method m,uintptr_t rva,void* replace,void** original){
 const auto& v=*static_cast<const fake::MethodInfo*>(m);
 fake::Check(v.type->name=="Slot","Only Slot callbacks expected");
 if(rva==0x165CF10){fake::Check(v.name=="Refresh"&&v.argc==0,"Refresh ABI");fake::refreshHook=reinterpret_cast<fake::RefreshFn>(replace);*original=reinterpret_cast<void*>(&fake::OriginalRefresh);return;}
 fake::Check(v.argc==1,"Native event ABI");
 if(rva==0x165C170){fake::Check(v.name=="OnMouseEnter","Enter ABI");fake::enterHook=reinterpret_cast<fake::EventFn>(replace);*original=reinterpret_cast<void*>(&fake::OriginalEnter);}
 else if(rva==0x165C200){fake::Check(v.name=="OnMouseLeave","Leave ABI");fake::leaveHook=reinterpret_cast<fake::EventFn>(replace);*original=reinterpret_cast<void*>(&fake::OriginalLeave);}
 else if(rva==0x165BDF0){fake::Check(v.name=="OnDetachedFromPanel","Detach ABI");fake::detachHook=reinterpret_cast<fake::EventFn>(replace);*original=reinterpret_cast<void*>(&fake::OriginalDetach);}
 else {fake::Check(rva==0x165B840&&v.name=="OnClick","Click ABI");fake::clickHook=reinterpret_cast<fake::EventFn>(replace);*original=reinterpret_cast<void*>(&fake::OriginalClick);}
}
namespace fake {
struct Fixture {
 Module owner{std::filesystem::path{}};Obj parent=nullptr,item=nullptr;
 Fixture(){soul::ReleaseSpellView();soul::ReleaseItemView();Check(handles.empty(),"Previous view leaked refs");module=&owner;owner.servicesReady=true;owner.compositor.reset(Make("Compositor"));
  services={{"fou",Make("gbr")},{"fqm",Make("fqm")},{"fkq",Make("fkq")}};logs.clear();failing.clear();failures=0;
  binds=shows=hides=updates=clicks=enters=leaves=detaches=refreshes=0;
  soul::InstallItemView();parent=Make("Root");soul::CreateItemView(parent);item=Make("mc");Check(soul::UpdateItemView(item),"Initial bind failed");
 }
 ~Fixture(){soul::ReleaseSpellView();soul::ReleaseItemView();owner.compositor.reset();module=nullptr;}
 void Enter(){enterHook(latestSlot,nullptr,nullptr);}
};
void IconAndTooltip(){Fixture f;Check(Get(latestSlot).fields["dragDisabled"]==latestSlot,"Slot drag must be disabled");Check(Get(latestSlot).fields["data"]==f.item,"Actual equipped wrapper must be bound");
 Check(Get(latestSlot).slotSize==2,"Use the native 80px Slot and its fitted image, not only an enlarged outer frame");
 f.Enter();Check(shows==1&&Get(latestBuilder).fields["item"]==f.item,"Hover should show actual native item tooltip");
 for(int i=0;i<5;++i)soul::UpdateItemView(f.item);Check(shows==1&&binds==1&&updates==0,"Stable polls must not reset tooltip or native slot");
 refreshHook(latestSlot,nullptr);soul::UpdateItemView(f.item);Check(updates==1&&shows==1,"Native item refresh should update existing tooltip without flicker");
 auto other=Make("Slot");clickHook(latestSlot,nullptr,nullptr);clickHook(other,nullptr,nullptr);Check(clicks==1,"Only preview clicks must be suppressed");
 enterHook(other,nullptr,nullptr);leaveHook(other,nullptr,nullptr);Check(shows==1&&hides==0,"Foreign slot hover must remain original");
 auto service=services["fou"];Get(service).tips.clear();soul::UpdateItemView(f.item);Check(shows==2,"External native tooltip removal must recover on next visible poll");
 leaveHook(latestSlot,nullptr,nullptr);Check(hides==1,"Leave must remove owned tooltip");
}
void ReplacementAndDetach(){Fixture f;f.Enter();auto previous=latestBuilder;auto second=Make("mc");soul::UpdateItemView(second);Check(hides==1&&shows==2&&latestBuilder!=previous&&Get(latestBuilder).fields["item"]==second,"Replacement must rebuild for actual new item");
 soul::UpdateItemView(nullptr);Check(hides==2&&!Get(latestSlot).fields["data"],"Unequip must unbind and hide tooltip");
 soul::UpdateItemView(f.item);f.Enter();auto slot=latestSlot;detachHook(slot,nullptr,nullptr);Check(hides==3&&!Get(slot).attached,"Detach must hide own tooltip");
 soul::UpdateItemView(f.item);Check(shows==3,"Detached slot must never show tooltip");
}
void OwnershipAndServiceChange(){Fixture f;f.Enter();auto oldService=services["fou"];auto name=Get(latestBuilder).text;auto foreign=Make("ItemTooltipBuilder");Get(oldService).tips[name]=foreign;
 leaveHook(latestSlot,nullptr,nullptr);Check(hides==0&&Get(oldService).tips[name]==foreign,"Cleanup must preserve foreign replacement");
 f.Enter();Check(shows==1&&Get(oldService).tips[name]==foreign,"Tooltip name collision must not replace foreign builder");
 Get(oldService).tips.clear();f.Enter();Check(shows==2,"Name released: normal hover should recover");
 services["fou"]=Make("gbr");soul::UpdateItemView(f.item);Check(hides==1&&shows==3&&Get(oldService).tips.empty(),"Tooltip service replacement must remove old and use current");
 services["fqm"]=Make("fqm");soul::UpdateItemView(f.item);Check(hides==2&&shows==4,"Dependency replacement must rebuild tooltip");
 f.owner.compositor.reset(Make("NewCompositor"));soul::UpdateItemView(f.item);Check(hides==3&&shows==5,"Session change must rebuild tooltip");
}
void FailuresAndRetry(){Fixture f;auto second=Make("mc");failing="set_data";failures=1;Check(!soul::UpdateItemView(second),"Failed binding must hide stale icon");Check(Get(latestSlot).fields["data"]==f.item,"Injected setter failure fixture");
 failing="partial_set_data";failures=1;Check(!soul::UpdateItemView(second)&&Get(latestSlot).fields["data"]==second,"Partial native bind failure must still hide until confirmed");
 failing.clear();Check(soul::UpdateItemView(f.item)&&Get(latestSlot).fields["data"]==f.item,"Snapshot reverting after partial B bind must explicitly restore A");
 Check(soul::UpdateItemView(second)&&Get(latestSlot).fields["data"]==second,"Same requested item must retry after setter failure");
 services["fqm"]=nullptr;f.Enter();auto logCount=logs.size();soul::UpdateItemView(second);Check(shows==0&&logs.size()==logCount,"Missing tooltip service must degrade and log once");
 services["fqm"]=Make("fqm");soul::UpdateItemView(second);Check(shows==1,"Missing service recovery should restore tooltip");
 failing="bfps";failures=1;leaveHook(latestSlot,nullptr,nullptr);Check(hides==0,"Injected hide failure");
 failing.clear();soul::ReleaseItemView();Check(hides==1,"Cleanup must retry retained ownership after transient hide error");
}
int spellClicks=0; Obj clickedSpell=nullptr;
void SpellClicked(Obj spell) { ++spellClicks; clickedSpell=spell; }
void SpellSlotAndTooltip() {
 Fixture f;auto itemSlot=latestSlot;services["frc"]=Make("frc");soul::SetSpellClick(SpellClicked);
 auto slot=soul::CreateSpellView(f.parent),spell=Make("ht");spellClicks=0;clickedSpell=nullptr;
 Check(Get(slot).slotSize==2&&Get(slot).slotType==4&&Get(itemSlot).slotSize==2&&Get(itemSlot).slotType==-1,
       "Capture uses the medium80 native spell tile without changing the separate inventory tile");
 Check(soul::UpdateSpellView(spell,true),"Known spell binds to native slot");
 Check(cursorOwners.empty(),"Binding must not change cursor before hover");
 enterHook(slot,nullptr,nullptr);Check(shows==1&&Get(latestBuilder).type==Type("SpellTooltipBuilder")&&Get(latestBuilder).fields["spell"]==spell,"Spell hover uses native tooltip");
 Check(cursorOwners.contains(slot)&&!cursorOwners.contains(itemSlot),"Only hovered spell requests native hand");
 clickHook(itemSlot,nullptr,nullptr);Check(spellClicks==0&&clicks==0,"Item preview stays inert with spell slot present");
 clickHook(slot,nullptr,nullptr);Check(spellClicks==1&&clickedSpell==spell&&clicks==0,"Spell callback receives exact displayed wrapper");
 soul::UpdateSpellView(spell,false);clickHook(slot,nullptr,nullptr);Check(spellClicks==1,"Disabled spell never invokes callback");
 Check(cursorOwners.contains(slot),"Hover keeps hand when native casting is unavailable");
 auto foreign=Make("NativeButton");cursorOwners[foreign]=33554432;
 leaveHook(slot,nullptr,nullptr);
 Check(!cursorOwners.contains(slot)&&cursorOwners.contains(foreign),"Leaving restores other native cursors without clearing foreign owners");
 enterHook(slot,nullptr,nullptr);
 soul::UpdateSpellView(nullptr,false);Check(hides==2&&!Get(slot).fields["data"],"Phase change clears spell and tooltip");
 Check(!cursorOwners.contains(slot),"Unbinding clears hovered hand");
 soul::UpdateSpellView(spell,true);Get(f.parent).scalar=1;clickHook(slot,nullptr,nullptr);Check(spellClicks==1,"Hidden parent prevents cast");
 soul::UpdateSpellView(spell,true);Check(!cursorOwners.contains(slot),"Hidden parent clears hand");
 Get(f.parent).scalar=0;soul::UpdateSpellView(spell,true);Check(cursorOwners.contains(slot),"Visible hovered spell restores hand");
 failing="RemoveCursor";failures=1;bool cleanupFailed=false;
 try{soul::ReleaseSpellView();}catch(const std::exception&){cleanupFailed=true;}
 Check(cleanupFailed&&cursorOwners.contains(slot),"Failed cursor cleanup retains owned slot for retry");
 failing.clear();soul::ReleaseSpellView();Check(!cursorOwners.contains(slot)&&cursorOwners.contains(foreign),"Retry clears only owned cursor before destroying slot");
 cursorOwners.erase(foreign);
}

void HiddenAndStopped(){Fixture f;Get(f.parent).scalar=1;f.Enter();Check(shows==0,"Hidden ancestor must not show tooltip");Get(f.parent).scalar=0;f.Enter();Check(shows==1,"Visible hover should show tooltip");
 f.owner.stopped=true;soul::UpdateItemView(f.item);Check(hides==1,"Stopped module must hide tooltip");f.Enter();Check(shows==1,"Stopped module cannot show tooltip");
}
}
int main(){try{fake::Initialize();for(auto test:{fake::IconAndTooltip,fake::ReplacementAndDetach,fake::OwnershipAndServiceChange,fake::FailuresAndRetry,fake::HiddenAndStopped,fake::SpellSlotAndTooltip}){test();fake::Check(fake::handles.empty(),"Leaked native item-view roots");}
 std::cout<<"PASS: native item icon/tooltip, current item and service binding, owned cleanup, real callback isolation, stale-bind retry and graceful errors\n";return 0;
}catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}}

managed::Method managed::Api::FindVerified(managed::Klass klass, const char* name, int argc, uintptr_t rva) const { auto result=Find(klass,name,argc); Native(result,rva); return result; }
