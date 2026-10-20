#include "monster-glow-ui.hpp"
#include "module.hpp"
#include "features.hpp"
#include "options-ui.hpp"
#include <array>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <iostream>
#include <limits>
#include <map>
#include <memory>
#include <unordered_map>

// Entirely offline managed/UI fixture. No game binary, client process, renderer,
// input driver, actual Unity object or real render texture is used by this test.
namespace fake {
using namespace managed;
using monsterGlow::UiRect;
struct Vec2 { float x,y; };
struct Vec3 { float x,y,z; };
struct Bounds { Vec3 center,extents; };
struct Color { float r=1,g=1,b=1,a=0; };
struct Background { Obj texture=nullptr,sprite=nullptr,renderTexture=nullptr,vectorImage=nullptr; };
struct Type { std::string name; bool value = false; };
struct MethodData { Type* type; std::string name; int argc; uintptr_t rva = 0; };
struct Object {
    Type* type;
    std::string name;
    std::map<std::string,Obj> fields;
    std::vector<Obj> children;
    std::map<std::string,float> styles;
    std::array<unsigned char,128> data{};
    UiRect bound{0,0,1000,800},clip{0,0,1000,800},bounding{0,0,1000,800};
    Color background{},tint{1,1,1,1}; Background image{};
    float opacity=1;
    bool visible=true,owned=false,focusable=true;
    int display=0,picking=0;
};
std::map<std::string,std::unique_ptr<Type>> types;
std::vector<std::unique_ptr<Object>> objects;
std::vector<std::unique_ptr<MethodData>> methods;
std::unordered_map<GCHandle,Obj> handles;
std::map<std::string,uintptr_t> guards;
GCHandle nextHandle=0xDCB000000000ULL;
Obj document=nullptr,panel=nullptr,tree=nullptr,camera=nullptr,service=nullptr;
Obj layerService=nullptr,layers=nullptr,hud=nullptr,widget=nullptr,windows=nullptr,menus=nullptr;
int screenWidth=1000,screenHeight=800,scans=0,writesToNative=0,detachFailures=0,bindFailures=0,styleFailures=0;
float panelScale=1,localOffsetX=0,localOffsetY=0;
std::vector<std::string> logs;
void Check(bool condition,const char* message){if(!condition)throw std::runtime_error(message);}
Type* Class(const std::string& name){auto& value=types[name];if(!value)value=std::make_unique<Type>(Type{name,name=="Box"||name=="Length"||name=="Hierarchy"||name=="StyleValue"});return value.get();}
Object& Get(Obj object){Check(object!=nullptr,"Null fake object");return *static_cast<Object*>(object);}
Obj Make(const std::string& name,bool owned=false){auto object=std::make_unique<Object>();object->type=Class(name);object->owned=owned;auto result=object.get();objects.push_back(std::move(object));return result;}
template<class T> Obj Box(T value,const char* type="Box"){auto result=Make(type);static_assert(sizeof(T)<=128);std::memcpy(Get(result).data.data(),&value,sizeof(value));return result;}
template<class T> T Data(Obj object){T result{};std::memcpy(&result,Get(object).data.data(),sizeof(result));return result;}
void Attach(Obj parent,Obj child){Get(parent).children.push_back(child);Get(child).fields["parent"]=parent;Get(child).fields["panel"]=Get(parent).fields["panel"];}
void Remove(Obj object){auto parent=Get(object).fields["parent"];if(parent){auto& children=Get(parent).children;children.erase(std::remove(children.begin(),children.end(),object),children.end());}Get(object).fields["parent"]=nullptr;Get(object).fields["panel"]=nullptr;}
void SetRect(Obj object,UiRect rect){Get(object).bound=Get(object).bounding=rect;}
Obj Painted(UiRect rect,float alpha=1,Obj parent=nullptr){auto object=Make("VisualElement");SetRect(object,rect);Get(object).background.a=alpha;Attach(parent?parent:widget,object);return object;}
Obj Renderer(float x=500,float y=400,float width=100,float height=100,float z=10){auto object=Make("Renderer");Get(object).fields["bounds"]=Box(Bounds{{x,y,z},{width/2,height/2,0}});return object;}
Obj Overlay(){for(auto object:Get(hud).children)if(Get(object).owned)return object;return nullptr;}
std::vector<Obj> VisibleImages(){std::vector<Obj> result;const auto overlay=Overlay();if(!overlay||Get(overlay).display)return result;for(auto clip:Get(overlay).children){if(Get(clip).display)continue;for(auto image:Get(clip).children)if(Get(image).fields["image"])result.push_back(image);}return result;}
void Initialize(){
 api.object_get_class=[](Obj value)->Klass{return Get(value).type;};
 api.class_is_valuetype=[](Klass type){return static_cast<Type*>(type)->value;};
 api.class_is_assignable_from=[](Klass expected,Klass actual){return expected==actual||(static_cast<Type*>(expected)->name=="VisualElement"&&static_cast<Type*>(actual)->name=="Image")||(static_cast<Type*>(expected)->name=="MagneticDragWindow"&&static_cast<Type*>(actual)->name=="DerivedWidget");};
 api.object_unbox=[](Obj value)->void*{return Get(value).data.data();};
 api.gchandle_new=[](Obj value,bool)->GCHandle{const auto handle=++nextHandle;handles[handle]=value;return handle;};
 api.gchandle_get_target=[](GCHandle handle)->Obj{return handles.at(handle);};
 api.gchandle_free=[](GCHandle handle){Check(handles.erase(handle)==1,"Duplicate GC release");};
}
Obj Invoke(Method method,Obj self,std::initializer_list<void*> arguments){
 const auto& m=*static_cast<const MethodData*>(method);const std::vector<void*> args(arguments);
 Check(static_cast<int>(args.size())==m.argc,"Wrong managed argument count");
 if(!self){
  if(m.name=="pbs")return document;
  if(m.name=="get_width")return Box(screenWidth);
  if(m.name=="get_height")return Box(screenHeight);
  if(m.name=="pcf"){Check(args[1]==panel,"Must project using actual panel");const auto point=*static_cast<Vec2*>(args[0]);return Box(Vec2{point.x/panelScale,(screenHeight-point.y)/panelScale});}
  if(m.name=="WorldToLocal"){Check(args[0]==hud,"Only HUD overlay parent conversion");const auto point=*static_cast<Vec2*>(args[1]);return Box(Vec2{point.x-localOffsetX,point.y-localOffsetY});}
  throw std::runtime_error("Unexpected static call "+m.name);
 }
 auto& object=Get(self);
 if(m.name=="xhs")return object.fields["camera"];
 if(m.name=="get_visualTree")return object.fields["tree"];
 if(m.name=="get_targetTexture")return object.fields["targetTexture"];
 if(m.name=="get_pixelRect")return Box(object.bound);
 if(m.name=="WorldToScreenPoint"){Check(self==camera,"Projection used stale camera");return Box(*static_cast<Vec3*>(args[0]));}
 if(m.name=="get_bounds")return object.fields["bounds"];
 if(m.name=="get_panel")return object.fields["panel"];
 if(m.name=="get_parent")return object.fields["parent"];
 if(m.name=="get_name"){auto result=Make("String");Get(result).name=object.name;return result;}
 if(m.name=="get_resolvedStyle"){++scans;return self;}
 if(m.name=="get_worldBound")return Box(object.bound);
 if(m.name=="get_worldBoundingBox")return Box(object.bounding);
 if(m.name=="get_worldClip")return Box(object.clip);
 if(m.name=="get_display")return Box(object.display);
 if(m.name=="get_opacity")return Box(object.opacity);
 if(m.name=="get_visible")return Box(object.visible);
 if(m.name=="get_backgroundColor")return Box(object.background);
 if(m.name=="get_backgroundImage")return Box(object.image);
 if(m.name=="get_unityBackgroundImageTintColor"||m.name=="get_tintColor")return Box(object.tint);
 if(m.name=="get_image")return object.fields["image"];
 if(m.name=="get_sprite")return object.fields["sprite"];
 if(m.name=="get_vectorImage")return object.fields["vectorImage"];
 if(m.name.find("get_border")==0){if(m.name.ends_with("Width"))return Box(object.styles[m.name]);return Box(Color{1,1,1,object.styles[m.name]});}
 if(m.name=="get_hierarchy"){auto hierarchy=Make("Hierarchy");Get(hierarchy).fields["owner"]=self;return hierarchy;}
 if(m.name=="get_childCount")return Box(static_cast<int>(Get(object.fields["owner"]).children.size()));
 if(m.name=="get_Item")return Get(object.fields["owner"]).children.at(*static_cast<int*>(args[0]));
 if(m.name=="get_style")return self;
 if(m.name.find("UnityEngine.UIElements.IStyle.get_")==0){auto box=Make("StyleValue");Get(box).fields["owner"]=self;return box;}
 if(m.name=="set_value"){std::memcpy(object.data.data(),args[0],4);return nullptr;}
 if(m.name.find("UnityEngine.UIElements.IStyle.set_")==0){
  if(!object.owned)++writesToNative;
  if(styleFailures>0){--styleFailures;throw std::runtime_error("style write failed");}
  const auto property=m.name.substr(std::string("UnityEngine.UIElements.IStyle.set_").size());
  if(property=="display"||property=="overflow"||property=="position"){
   int value=0;std::memcpy(&value,args[0],4);object.styles[property]=static_cast<float>(value);if(property=="display")object.display=value;
  }else{float value=0;std::memcpy(&value,args[0],4);object.styles[property]=value;}
  return nullptr;
 }
 if(m.name=="set_pickingMode"){if(!object.owned)++writesToNative;object.picking=*static_cast<int*>(args[0]);return nullptr;}
 if(m.name=="set_focusable"){if(!object.owned)++writesToNative;object.focusable=*static_cast<bool*>(args[0]);return nullptr;}
 if(m.name=="set_scaleMode"){Check(object.owned&&*static_cast<int*>(args[0])==0,"Owned images use normal stretch without UV flip");return nullptr;}
 if(m.name=="set_tintColor"){Check(object.owned,"Only private images receive glow tint");object.tint=*static_cast<Color*>(args[0]);return nullptr;}
 if(m.name=="set_image"){
  Check(object.owned,"Only owned image binding may change");object.fields["image"]=args[0];
  if(args[0]&&bindFailures>0){--bindFailures;throw std::runtime_error("partially applied texture bind failed");}
  return nullptr;
 }
 if(m.name=="Add"){Check(Get(args[0]).owned,"Cannot attach foreign native UI");Attach(self,args[0]);return nullptr;}
 if(m.name=="BringToFront"){Check(object.owned,"Only owned UI z order may change");auto parent=object.fields["parent"];Remove(self);Attach(parent,self);return nullptr;}
 if(m.name=="RemoveFromHierarchy"){
  Check(object.owned,"Cannot remove foreign native UI");
  if(detachFailures>0){--detachFailures;throw std::runtime_error("injected native detach failure");}
  Remove(self);return nullptr;
 }
 throw std::runtime_error("Unexpected managed call "+object.type->name+"."+m.name);
}
}
namespace managed {
Api api{};uintptr_t gameBase=0;
Klass Api::Class(const char*,const char*,const char* name)const{return fake::Class(name);}
Klass Api::NestedClass(Klass,const char* name)const{return fake::Class(name);}
Method Api::Find(Klass type,const char* name,int argc)const{auto method=std::make_unique<fake::MethodData>(fake::MethodData{static_cast<fake::Type*>(type),name,argc});auto result=method.get();fake::methods.push_back(std::move(method));return result;}
Method Api::FindVerified(Klass type,const char* name,int argc,uintptr_t rva)const{auto method=Find(type,name,argc);fake::guards[static_cast<fake::Type*>(type)->name+"."+name]=rva;return method;}
Obj Api::Invoke(Method method,Obj self,std::initializer_list<void*> args)const{return fake::Invoke(method,self,args);}
Obj Api::Call(Obj self,const char* name,std::initializer_list<void*> args)const{return Invoke(Find(object_get_class(self),name,static_cast<int>(args.size())),self,args);}
Obj Api::Field(Obj self,const char* name)const{return fake::Get(self).fields[name];}
std::string Api::String(Obj self)const{return self?fake::Get(self).name:std::string{};}
Obj Api::New(Klass type,std::initializer_list<void*> args)const{
 const std::string name=static_cast<fake::Type*>(type)->name;
 if(name=="Length"){fake::Check(args.size()==2,"Length ABI");auto begin=args.begin();struct Length{float value;int unit;};return fake::Box(Length{*static_cast<float*>(*begin),*static_cast<int*>(*(begin+1))},"Length");}
 fake::Check(args.size()==0&&(name=="VisualElement"||name=="Image"),"Only no-argument UI constructors");return fake::Make(name,true);
}
}
Module* module=nullptr;
Module::Module(std::filesystem::path):features(nullptr),ui(nullptr){}
managed::Obj Module::FindService(const char* name)const{if(std::string(name)=="fqp")return fake::layerService;fake::Check(std::string(name)=="fpk","Unexpected service requested");return fake::service;}
void Log(const std::string& message){fake::logs.push_back(message);}
namespace fake {
struct Fixture {
 Module owner{std::filesystem::path{}};
 Fixture(){
  module=&owner;owner.servicesReady=true;owner.compositor.reset(Make("Compositor"));
  document=Make("VisualElement");tree=Make("VisualElement");panel=Make("Panel");camera=Make("Camera");service=Make("eow");
  Get(service).fields["camera"]=camera;Get(panel).fields["tree"]=tree;Get(tree).fields["panel"]=panel;Attach(tree,document);
  layerService=Make("gmo");layers=Make("gmy");Get(layerService).fields["dzrv"]=layers;Attach(document,layers);
  hud=Make("VisualElement");Get(hud).name="RootLow";Attach(layers,hud);
  windows=Make("VisualElement");Get(windows).name="RootMedium";Attach(layers,windows);
  menus=Make("VisualElement");Get(menus).name="RootMenu";Attach(layers,menus);
  widget=Make("MagneticDragWindow");Get(widget).name="CartographyBanner";Attach(hud,widget);
  screenWidth=1000;screenHeight=800;panelScale=1;localOffsetX=localOffsetY=0;
  scans=writesToNative=detachFailures=bindFailures=styleFailures=0;logs.clear();
  monsterGlow::InstallGlowUi();
 }
 ~Fixture(){detachFailures=styleFailures=bindFailures=0;monsterGlow::ResetGlowUi();module=nullptr;}
};
float Area(UiRect rect){return rect.width*rect.height;}
float Overlap(UiRect a,UiRect b){return std::max(0.0f,std::min(a.x+a.width,b.x+b.width)-std::max(a.x,b.x))*std::max(0.0f,std::min(a.y+a.height,b.y+b.height)-std::max(a.y,b.y));}
bool Near(float a,float b){return std::abs(a-b)<0.001f;}
void NativeContractsAndProjection(){
 Fixture f;Painted({450,350,100,100});auto renderer=Renderer();auto frame=monsterGlow::BeginGlowUi(1000);
 auto projected=monsterGlow::ProjectGlowRenderers(frame,{renderer});
 Check(frame.valid&&projected.valid,"Painted HUD must occlude projected monster");
 Check(frame.paintCount==1&&frame.visitedNodes==3&&!frame.scanLimited&&std::string(projected.reason)=="ready","UI diagnostics must describe actual paint and projection");
 Check(Near(projected.screenRect.x,436)&&Near(projected.screenRect.y,336)&&Near(projected.screenRect.width,128)&&Near(projected.screenRect.height,128),"Screen crop must include exact14px padding");
 Check(Near(projected.panelRect.x,436)&&Near(projected.panelRect.y,336)&&projected.clipRects.size()==1,"Panel Y flip/crop correspondence");
 auto texture=Make("RenderTexture");Check(monsterGlow::ShowGlowUi(frame,1,projected,texture),"Owned glow should display");monsterGlow::EndGlowUi();
 auto images=VisibleImages();Check(images.size()==1&&Get(images.front()).fields["image"]==texture,"Texture bound only inside clip");
 Check(Near(Get(images.front()).tint.g,0.72f)&&Near(Get(images.front()).tint.b,0.16f)&&Near(Get(images.front()).tint.a,0.92f),"White rendered glow receives amber UI tint");
 const auto clip=Get(images.front()).fields["parent"];
 Check(Get(clip).styles["overflow"]==1&&Get(clip).styles["left"]==450&&Get(images.front()).styles["left"]==-14,"Exact clipping wrapper geometry");
 Check(writesToNative==0,"No native input/layout property changes");
 for(const auto& object:objects)if(object->owned)Check(object->picking==1&&!object->focusable,"Every owned element ignores input/focus");
 Check(guards.at("VisualElementExtensions.WorldToLocal")==0x56776A0&&guards.at("pq.pcf")==0x2E94C60&&guards.at("Camera.WorldToScreenPoint")==0x53B65B0,"Exact projection overload guards");
}
void ResolvedOverlayDiagnostics(){
 Fixture f;Painted({450,350,100,100});auto frame=monsterGlow::BeginGlowUi(1000);
 auto projection=monsterGlow::ProjectGlowRenderers(frame,{Renderer()});
 Check(monsterGlow::DescribeGlowUi()=="overlay=absent","Missing overlay diagnostic");
 Check(monsterGlow::ShowGlowUi(frame,1,projection,Make("RenderTexture")),"Diagnostic overlay setup");
 monsterGlow::EndGlowUi();auto image=VisibleImages().front();auto overlay=Overlay();
 auto description=monsterGlow::DescribeGlowUi();
 Check(description.find("overlayAttached=1 overlayFront=1 overlayVisible=1")!=std::string::npos&&
       description.find("images=1 laidOut=1 unclipped=1")!=std::string::npos,"Resolved visible overlay counters");
 Get(image).clip={0,0,0,0};
 Check(monsterGlow::DescribeGlowUi().find("unclipped=0")!=std::string::npos,"Fully clipped image must be observable");
 SetRect(image,{0,0,0,0});
 Check(monsterGlow::DescribeGlowUi().find("laidOut=0")!=std::string::npos,"Missing layout must be observable");
 Get(overlay).opacity=0;auto front=Make("VisualElement");Attach(hud,front);
 description=monsterGlow::DescribeGlowUi();
 Check(description.find("overlayFront=0")!=std::string::npos&&description.find("overlayOpacity=0")!=std::string::npos,
       "Overlay order/opacity diagnostics");
 Check(writesToNative==0&&Get(hud).children.back()==front,"Diagnostics must not change input or reorder the UI");
}
void TransparentAndHiddenUi(){
 Fixture f;auto transparent=Painted({0,0,1000,800},0);auto renderer=Renderer();
 auto first=monsterGlow::BeginGlowUi(1000);Check(!monsterGlow::ProjectGlowRenderers(first,{renderer}).valid,"Fullscreen transparent roots/blockers must never occlude");monsterGlow::EndGlowUi();
 auto image=Make("Image");SetRect(image,{450,350,100,100});Get(image).fields["image"]=Make("Texture");Get(image).picking=1;Attach(transparent,image);
 auto next=monsterGlow::BeginGlowUi(1100);Check(monsterGlow::ProjectGlowRenderers(next,{renderer}).valid,"Decorative Ignore images still paint");monsterGlow::EndGlowUi();
 Get(transparent).opacity=0;next=monsterGlow::BeginGlowUi(1200);Check(!monsterGlow::ProjectGlowRenderers(next,{renderer}).valid,"Ancestor opacity0 excludes descendants");monsterGlow::EndGlowUi();
 Get(transparent).opacity=1;Get(transparent).display=1;next=monsterGlow::BeginGlowUi(1300);Check(!monsterGlow::ProjectGlowRenderers(next,{renderer}).valid,"DisplayNone subtree excluded");monsterGlow::EndGlowUi();
 Get(transparent).display=0;Get(image).visible=false;next=monsterGlow::BeginGlowUi(1400);Check(!monsterGlow::ProjectGlowRenderers(next,{renderer}).valid,"Hidden visible property excluded");
}
void BoundedDisjointClips(){
 Fixture f;Painted({440,340,55,120});Painted({485,340,45,70});Painted({540,340,20,120});
 auto frame=monsterGlow::BeginGlowUi(1000);auto result=monsterGlow::ProjectGlowRenderers(frame,{Renderer()});Check(result.valid&&result.clipRects.size()>=3,"Separated HUD areas preserved");
 for(size_t i=0;i<result.clipRects.size();++i){const auto r=result.clipRects[i];Check(!(r.x<535&&r.x+r.width>535),"No contour in unpainted gap");for(size_t j=0;j<i;++j)Check(Overlap(r,result.clipRects[j])==0,"No duplicate glow over overlapping panels");}
 float area=0;for(auto r:result.clipRects)area+=Area(r);Check(area<128*128,"Clips cannot become target bounding rectangle");
 for(int i=0;i<20;++i)Painted({444+static_cast<float>(i)*5,450,2,5});
 frame=monsterGlow::BeginGlowUi(1100);result=monsterGlow::ProjectGlowRenderers(frame,{Renderer()});Check(result.clipRects.size()<=8,"Fragment count bounded");
}
void BordersClippingAndUnbounded(){
 Fixture f;auto box=Painted({400,300,200,200},0);Get(box).styles["get_borderLeftWidth"]=4;Get(box).styles["get_borderLeftColor"]=1;
 auto frame=monsterGlow::BeginGlowUi(1000);Check(!monsterGlow::ProjectGlowRenderers(frame,{Renderer()}).valid,"Border-only UI has transparent interior");monsterGlow::EndGlowUi();
 Get(box).background.a=1;const float inf=std::numeric_limits<float>::infinity();Get(box).clip={-inf,-inf,inf,inf};
 frame=monsterGlow::BeginGlowUi(1100);Check(monsterGlow::ProjectGlowRenderers(frame,{Renderer()}).valid,"Native infinite clip intersects finite bound before validation");monsterGlow::EndGlowUi();
 Get(box).clip={0,0,450,800};frame=monsterGlow::BeginGlowUi(1200);const auto clipped=monsterGlow::ProjectGlowRenderers(frame,{Renderer()});
 Check(clipped.valid&&clipped.clipRects.size()==1&&Near(clipped.clipRects.front().width,14),"Ancestor worldClip trims actual painted bounds");
}
void ScaleOffsetResizeAndCache(){
 Fixture f;panelScale=2;SetRect(tree,{10,20,500,400});localOffsetX=5;localOffsetY=7;
 auto painted=Painted({200,150,100,100});auto frame=monsterGlow::BeginGlowUi(1000);auto result=monsterGlow::ProjectGlowRenderers(frame,{Renderer()});
 Check(result.valid&&Near(result.panelRect.x,218)&&Near(result.panelRect.y,168)&&Near(result.panelRect.width,64),"Native scale applied once");
 Check(monsterGlow::ShowGlowUi(frame,1,result,Make("RenderTexture")),"Offset overlay display");
 auto image=VisibleImages().front();auto clip=Get(image).fields["parent"];
 Check(Near(Get(clip).styles["left"],208)&&Near(Get(clip).styles["top"],148),"Clip subtracts overlay local origin");
 monsterGlow::EndGlowUi();const auto before=scans;Get(painted).display=1;
 auto cached=monsterGlow::BeginGlowUi(1099);Check(scans==before&&monsterGlow::ProjectGlowRenderers(cached,{Renderer()}).valid,"Full tree scan throttled100ms");monsterGlow::EndGlowUi();
 auto expired=monsterGlow::BeginGlowUi(1100);Check(scans>before&&!monsterGlow::ProjectGlowRenderers(expired,{Renderer()}).valid,"Cache refresh observes hidden HUD");
 SetRect(tree,{0,0,600,400});auto resized=monsterGlow::BeginGlowUi(1101);Check(resized.generation!=frame.generation&&!monsterGlow::ShowGlowUi(resized,1,result,Make("RenderTexture")),"Resize invalidates old projection and branch");
}
void LifecycleIdentityFailures(){
 Fixture f;Painted({0,0,1000,800});auto renderer=Renderer();auto frame=monsterGlow::BeginGlowUi(1000);auto result=monsterGlow::ProjectGlowRenderers(frame,{renderer});
 Check(monsterGlow::ShowGlowUi(frame,1,result,Make("RenderTexture")),"Initial overlay");monsterGlow::EndGlowUi();
 auto empty=monsterGlow::BeginGlowUi(1001);monsterGlow::EndGlowUi();Check(VisibleImages().empty(),"Unused target hidden each frame");
 Check(!monsterGlow::ShowGlowUi(frame,1,result,Make("RenderTexture")),"Closed/old frame rejected");
 frame=monsterGlow::BeginGlowUi(1002);result=monsterGlow::ProjectGlowRenderers(frame,{renderer});auto oldCamera=camera;camera=Make("Camera");Get(service).fields["camera"]=camera;
 Check(!monsterGlow::ShowGlowUi(frame,1,result,Make("RenderTexture")),"Camera changed during frame rejects stale UI");camera=oldCamera;Get(service).fields["camera"]=camera;
 frame=monsterGlow::BeginGlowUi(1003);result=monsterGlow::ProjectGlowRenderers(frame,{renderer});bindFailures=1;
 Check(!monsterGlow::ShowGlowUi(frame,1,result,Make("RenderTexture"))&&!Overlay(),"Partial native texture bind failure removes whole owned overlay");
 frame=monsterGlow::BeginGlowUi(1004);result=monsterGlow::ProjectGlowRenderers(frame,{renderer});Check(monsterGlow::ShowGlowUi(frame,1,result,Make("RenderTexture")),"Recover after transient setter error");
 detachFailures=3;monsterGlow::ResetGlowUi();Check(Overlay()&&Get(Overlay()).display==1&&monsterGlow::GlowUiCleanupPending(),"Failed detach keeps hidden owned branch and exposes pending texture lifetime");
 auto blocked=monsterGlow::BeginGlowUi(1005);Check(!blocked.valid,"New frame blocked while old overlay detach fails");
 blocked=monsterGlow::BeginGlowUi(1006);Check(!blocked.valid,"Repeated detach failure remains blocked");
 auto recovered=monsterGlow::BeginGlowUi(1007);Check(recovered.valid&&!Overlay()&&!monsterGlow::GlowUiCleanupPending(),"Successful detach retry allows clean reinitialization and texture release");
 auto next=monsterGlow::ProjectGlowRenderers(recovered,{renderer});Check(monsterGlow::ShowGlowUi(recovered,1,next,Make("RenderTexture")),"Post-retry overlay");
 styleFailures=1;monsterGlow::ResetGlowUi();Check(!Overlay(),"Detach still attempted when hide style write throws");
}
void InvalidGeometryAndBudgets(){
 Fixture f;Painted({0,0,1000,800});auto frame=monsterGlow::BeginGlowUi(1000);
 Check(!monsterGlow::ProjectGlowRenderers(frame,{Renderer(500,400,100,100,-1)}).valid,"Behind-camera bounds rejected");
 Check(!monsterGlow::ProjectGlowRenderers(frame,{Renderer(std::numeric_limits<float>::quiet_NaN())}).valid,"Nonfinite renderer rejected");
 auto edge=monsterGlow::ProjectGlowRenderers(frame,{Renderer(0,400,100,100)});Check(edge.valid&&edge.screenRect.x==0&&edge.screenRect.width==64,"Padded crop clamps to physical viewport");
 auto texture=Make("RenderTexture");auto target=monsterGlow::ProjectGlowRenderers(frame,{Renderer()});
 for(int id=0;id<8;++id)Check(monsterGlow::ShowGlowUi(frame,id,target,texture),"Eight bounded targets accepted");
 Check(!monsterGlow::ShowGlowUi(frame,9,target,texture)&&VisibleImages().size()==8,"Ninth target cannot allocate unbounded images");
 monsterGlow::EndGlowUi();Get(camera).fields["targetTexture"]=Make("RenderTexture");auto offscreen=monsterGlow::BeginGlowUi(1100);Check(!offscreen.valid&&!Overlay()&&std::string(offscreen.reason)=="camera-target-texture","Offscreen camera must report its actual rejection gate");
 Get(camera).fields["targetTexture"]=nullptr;f.owner.stopped=true;Check(!monsterGlow::BeginGlowUi(1101).valid,"Stopped context never creates UI");
}
void ScanBudgetAndOwnOverlayExclusion(){
 Fixture f;auto paint=Painted({450,350,100,100});auto renderer=Renderer();
 auto frame=monsterGlow::BeginGlowUi(1000);auto projected=monsterGlow::ProjectGlowRenderers(frame,{renderer});
 Check(monsterGlow::ShowGlowUi(frame,1,projected,Make("RenderTexture")),"Initial owned image");monsterGlow::EndGlowUi();
 Get(paint).background.a=0;frame=monsterGlow::BeginGlowUi(1100);
 Check(!monsterGlow::ProjectGlowRenderers(frame,{renderer}).valid,"Owned textured overlay must never occlude itself");monsterGlow::EndGlowUi();
 for(int i=0;i<5000;++i){auto empty=Make("VisualElement");Attach(widget,empty);}
 scans=0;frame=monsterGlow::BeginGlowUi(1200);
 Check(scans<=4096&&scans>4000,"Hierarchy traversal has fixed4096 node budget even for huge chat trees");
 Check(frame.scanLimited,"Truncated UI discovery must be observable in diagnostics");
 Check(!monsterGlow::ProjectGlowRenderers(frame,{renderer}).valid,"Empty bounded tree remains transparent");
}
}
namespace fake {
void FullPanelAfterSmallDecorations(){
 Fixture f;
 // Frontmost small children are visited before an opaque window background.
 Painted({430,330,150,150});
 for(int i=0;i<12;++i)Painted({450+static_cast<float>(i)*7,360,2,2});
 auto frame=monsterGlow::BeginGlowUi(1000);
 auto projection=monsterGlow::ProjectGlowRenderers(frame,{Renderer()});
 float area=0;for(auto clip:projection.clipRects)area+=Area(clip);
 Check(projection.valid&&Near(area,128*128),"Opaque window must retain full coverage despite many painted decorations");
}
void UnrepresentableClipDoesNotClearOtherGlows(){
 Fixture f;Painted({430,330,150,150});
 auto frame=monsterGlow::BeginGlowUi(1000);
 auto projection=monsterGlow::ProjectGlowRenderers(frame,{Renderer()});
 auto texture=Make("RenderTexture");
 Check(monsterGlow::ShowGlowUi(frame,1,projection,texture),"First monster remains available");
 // Subtraction can leave a positive sliver narrower than a coordinate ULP.
 projection.clipRects.push_back({500,360,0.000001f,2});
 Check(monsterGlow::ShowGlowUi(frame,2,projection,texture),"Degenerate local clip must not reset all glow targets");
 monsterGlow::EndGlowUi();
 Check(VisibleImages().size()==2,"Keep valid fragments and the previous target when one sliver collapses");
 Check(logs.empty(),"Subpixel geometry must not become a UI failure");
}
// Flatten the fixture hierarchy in draw order, as UI Toolkit does. These tests
// exercise production placement/order; they do not emulate the GPU renderer.
void DrawOrder(Obj element,std::vector<Obj>& result){result.push_back(element);for(auto child:Get(element).children)DrawOrder(child,result);}
bool DrawsAfter(Obj later,Obj earlier){std::vector<Obj> order;DrawOrder(tree,order);auto a=std::find(order.begin(),order.end(),later),b=std::find(order.begin(),order.end(),earlier);return a!=order.end()&&b!=order.end()&&a>b;}
void OnlyDisplayModulesOcclude(){
 Fixture f;
 // An arbitrary painted root, ordinary window, or even a movable window in a
 // higher layer must not trigger the glow by itself.
 Painted({0,0,1000,800},1,document);Painted({0,0,1000,800},1,hud);
 auto inventory=Make("WindowFigma");Attach(windows,inventory);Painted({430,330,150,150},1,inventory);
 auto notification=Make("MagneticDragWindow");Attach(menus,notification);Painted({430,330,150,150},1,notification);
 auto renderer=Renderer();auto frame=monsterGlow::BeginGlowUi(1000);
 Check(frame.valid&&frame.paintCount==0&&!monsterGlow::ProjectGlowRenderers(frame,{renderer}).valid,"Only native display widgets in RootLow may trigger glow");
 monsterGlow::EndGlowUi();
 for(const char* name:{"CartographyBanner","Chat","ActionBar1"}){
  Get(widget).name=name;auto paint=Painted({450,350,100,100});
  frame=monsterGlow::BeginGlowUi(1100);auto result=monsterGlow::ProjectGlowRenderers(frame,{renderer});
  Check(result.valid&&frame.paintCount==1,"Display module's painted contents trigger glow");
  monsterGlow::EndGlowUi();
  Get(widget).display=1;frame=monsterGlow::BeginGlowUi(1200);
  Check(!monsterGlow::ProjectGlowRenderers(frame,{renderer}).valid,"Disabled module cannot trigger glow");
  monsterGlow::EndGlowUi();Remove(paint);Get(widget).display=0;
 }
 Get(widget).type=Class("DerivedWidget");Painted({450,350,100,100});
 frame=monsterGlow::BeginGlowUi(1300);Check(monsterGlow::ProjectGlowRenderers(frame,{renderer}).valid,"Native derived display widgets remain supported");
}
void AlwaysScopeUsesViewport(){
 Fixture f;auto frame=monsterGlow::BeginGlowUi(1000);auto renderer=Renderer();
 Check(frame.valid&&frame.paintCount==0,"No HUD paint in always fixture");
 Check(!monsterGlow::ProjectGlowRenderers(frame,{renderer}).valid,"UI scope still requires painted HUD");
 auto projection=monsterGlow::ProjectGlowRenderers(frame,{renderer},true);
 Check(projection.valid&&projection.clipRects.size()==1,"Always scope projects without HUD overlap");
 const auto clip=projection.clipRects.front();
 Check(Near(clip.x,projection.panelRect.x)&&Near(clip.y,projection.panelRect.y)&&
       Near(clip.width,projection.panelRect.width)&&Near(clip.height,projection.panelRect.height),
       "Unoccluded on-screen contour uses its full projected bounds");
 Check(monsterGlow::ShowGlowUi(frame,1,projection,Make("RenderTexture")),"Always scope shares owned HUD overlay");
 auto image=VisibleImages().front();auto window=Painted({0,0,1000,800},1,windows);
 Check(DrawsAfter(window,image)&&Get(Overlay()).fields["parent"]==hud&&writesToNative==0,
       "Always contour still stays below native full windows without changing native styles");
 monsterGlow::EndGlowUi();monsterGlow::ResetGlowUi();
 Check(VisibleImages().empty(),"Always mode releases its owned images");
 Check(!monsterGlow::ProjectGlowRenderers(frame,{renderer},true).valid,"Always scope rejects stale frame");
}
void WindowsStayAboveGlow(){
 Fixture f;Painted({430,330,150,150});auto frame=monsterGlow::BeginGlowUi(1000);
 auto projection=monsterGlow::ProjectGlowRenderers(frame,{Renderer()});auto texture=Make("RenderTexture");
 const auto nativeLayerOrder=Get(layers).children;
 Check(monsterGlow::ShowGlowUi(frame,1,projection,texture),"HUD glow setup");
 auto image=VisibleImages().front();Check(DrawsAfter(image,widget),"Glow must be above the display module");
 for(const char* name:{"WorldMap","Inventory","Spells"}){
  auto window=Make("WindowFigma");Get(window).name=name;Attach(windows,window);
  auto surface=Painted({0,0,1000,800},1,window);
  Check(DrawsAfter(surface,image),"New full window must cover existing glow immediately, without a rescan");
  Check(monsterGlow::ShowGlowUi(frame,1,projection,texture)&&DrawsAfter(surface,image),"Refreshing glow cannot lift it above a window");
  Remove(window);
 }
 auto popup=Painted({430,330,150,150},1,menus);
 Check(DrawsAfter(popup,image),"Context menu and higher popup layers stay above glow");
 Check(Get(layers).children==nativeLayerOrder&&Get(tree).children.size()==1&&Get(document).children.size()==1,"Native layer order and document siblings must remain unchanged");
 Check(Get(Overlay()).fields["parent"]==hud&&Get(hud).children.back()==Overlay()&&writesToNative==0,"Only private HUD branch is reordered");
}
void MissingOrChangedHudFailsClosed(){
 Fixture f;Painted({430,330,150,150});auto frame=monsterGlow::BeginGlowUi(1000);
 auto projection=monsterGlow::ProjectGlowRenderers(frame,{Renderer()});
 Check(monsterGlow::ShowGlowUi(frame,1,projection,Make("RenderTexture")),"Initial glow");
 auto oldOverlay=Overlay();auto oldHud=hud;
 Remove(hud);hud=Make("VisualElement");Get(hud).name="RootLow";Attach(layers,hud);Remove(widget);Attach(hud,widget);
 Check(!monsterGlow::ShowGlowUi(frame,1,projection,Make("RenderTexture")),"Detached HUD rejects stale frame");
 frame=monsterGlow::BeginGlowUi(1001);Check(frame.valid&&frame.root.get()==hud&&!Get(oldOverlay).fields["parent"]&&Get(oldHud).children.empty(),"Replacement HUD releases old private overlay");
 projection=monsterGlow::ProjectGlowRenderers(frame,{Renderer()});Check(monsterGlow::ShowGlowUi(frame,1,projection,Make("RenderTexture")),"Replacement HUD can show glow");
 auto duplicate=Make("VisualElement");Get(duplicate).name="RootLow";Attach(layers,duplicate);
 frame=monsterGlow::BeginGlowUi(1002);Check(!frame.valid&&!Overlay()&&std::string(frame.reason)=="hud-layer-unavailable","Ambiguous HUD never falls back to global overlay");Remove(duplicate);
 Get(hud).name="Unknown";Check(!monsterGlow::BeginGlowUi(1003).valid,"Missing named native layer fails closed");Get(hud).name="RootLow";
 auto originalService=layerService;layerService=nullptr;Check(!monsterGlow::BeginGlowUi(1004).valid,"Unavailable layer service fails closed");
 layerService=Make("UnexpectedService");Check(!monsterGlow::BeginGlowUi(1005).valid,"Wrong layer service type fails closed");layerService=originalService;
 frame=monsterGlow::BeginGlowUi(1006);projection=monsterGlow::ProjectGlowRenderers(frame,{Renderer()});
 Get(layerService).fields["dzrv"]=nullptr;Check(!monsterGlow::ShowGlowUi(frame,1,projection,Make("RenderTexture"))&&!monsterGlow::BeginGlowUi(1007).valid,"Layer host replacement rejects stale frames");
 Get(layerService).fields["dzrv"]=layers;Remove(layers);Attach(tree,layers);
 Check(!monsterGlow::BeginGlowUi(1008).valid,"Host outside active document fails closed");
 Check(Get(tree).children.size()==2&&writesToNative==0,"Failure cannot attach global overlay or change native styles");
}
}
int main(){try{
 fake::Initialize();
 for(auto test:{fake::OnlyDisplayModulesOcclude,fake::AlwaysScopeUsesViewport,fake::WindowsStayAboveGlow,fake::MissingOrChangedHudFailsClosed,fake::ResolvedOverlayDiagnostics,fake::FullPanelAfterSmallDecorations,fake::UnrepresentableClipDoesNotClearOtherGlows,fake::NativeContractsAndProjection,fake::TransparentAndHiddenUi,fake::BoundedDisjointClips,fake::BordersClippingAndUnbounded,fake::ScaleOffsetResizeAndCache,fake::LifecycleIdentityFailures,fake::InvalidGeometryAndBudgets,fake::ScanBudgetAndOwnOverlayExclusion}){
  test();fake::Check(fake::handles.empty(),"Managed UI references leaked");
 }
 std::cout<<"PASS: glow projection, native scale, painted UI occlusion, clipping, empty roots, bounds, ownership, target budgets, identity and cleanup recovery\n";return 0;
}catch(const std::exception& error){std::cerr<<"FAIL: "<<error.what()<<'\n';return 1;}}
