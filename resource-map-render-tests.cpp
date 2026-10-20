#include "resource-map-render.hpp"
#include "resource-map-policy.hpp"
#include "module.hpp"
#include "features.hpp"
#include "options-ui.hpp"
#include <array>
#include <cmath>
#include <cstring>
#include <functional>
#include <iostream>
#include <map>
#include <memory>
#include <limits>

// The production renderer runs against this synthetic managed runtime. These
// tests never load a game assembly, enumerate processes, or create real UI.
namespace fake {
using namespace managed;
void Check(bool ok,const char* text){if(!ok)throw std::runtime_error(text);}
struct Vec2{float x,y;}; struct Rect{float x,y,w,h;};
struct Class{std::string name;};
struct MethodInfo{Class* type;std::string name;int argc;uintptr_t rva;};
struct Object {
    Class* type{}; alignas(8) std::array<unsigned char,64> value{};
    std::map<std::string,Obj> refs;
    std::map<std::string,double> numbers;
    std::map<int,Obj> indexed;
    std::map<std::string,Obj> strings;
    std::map<std::string,std::array<unsigned char,16>> styles;
    std::vector<Obj> children;
    std::string text,property;
    Obj parent{},panel{},styleOwner{};
    Rect bound{0,0,1000,1000},clip{0,0,600,600};
    Vec2 origin{},scale{1,1};
    double zoom=1; int world=1;
    bool owned=false,visible=true;
    int picking=0;
};
std::map<std::string,std::unique_ptr<Class>> classes;
std::vector<std::unique_ptr<Object>> objects;
std::vector<std::unique_ptr<MethodInfo>> methods;
std::map<GCHandle,Obj> handles;
std::map<std::string,Obj> services;
GCHandle next=0xA987000000ULL;
bool nativeRemoveFails=false; int creates=0,disposes=0,nativeWrites=0,writes=0;
std::function<void()> onConvert;
std::map<std::string,size_t> offsets{{"dueo",0x98},{"dugq",0x68},{"dugv",0x90},{"dugy",0xA8},
    {"<dugi>k__BackingField",0x38},{"<dugk>k__BackingField",0x48},{"<dugf>k__BackingField",0x20},
    {"id",0x10},{"<dtwe>k__BackingField",0x10},{"dttw",0x10},{"dttx",0x18}};
Class* Type(const std::string& n){auto& p=classes[n];if(!p)p=std::make_unique<Class>(Class{n});return p.get();}
Object& Get(Obj o){Check(o!=nullptr,"Null fake object");return *static_cast<Object*>(o);}
Obj Make(const std::string& n,bool owned=false){auto p=std::make_unique<Object>();p->type=Type(n);p->owned=owned;auto result=p.get();objects.push_back(std::move(p));return result;}
template<class T> Obj Box(T value){auto o=Make("Box");std::memcpy(Get(o).value.data(),&value,sizeof(value));return o;}
Obj Str(const std::string& text){auto o=Make("String");Get(o).text=text;return o;}
Obj Panel(Obj o){return Get(o).parent?Panel(Get(o).parent):Get(o).panel;}
void Detach(Obj o){auto& p=Get(o);if(p.parent){auto& siblings=Get(p.parent).children;std::erase(siblings,o);p.parent=nullptr;}}
void Attach(Obj parent,Obj child){Detach(child);Get(parent).children.push_back(child);Get(child).parent=parent;}
Obj Group(Obj manager,const std::string& name,bool create=false){auto groups=Get(manager).refs["dttw"];auto& list=Get(groups).strings;if(!list.contains(name)){if(!create)return nullptr;list[name]=Make("StringDict");}return list[name];}
std::vector<Obj> LiveMarkers(Obj owner){auto manager=Get(owner).refs["<dugk>k__BackingField"];std::vector<Obj> result;for(auto& [_,g]:Get(Get(manager).refs["dttw"]).strings)for(auto& [__,m]:Get(g).strings)if(Get(m).owned)result.push_back(m);return result;}
std::vector<Obj> AllOwned(const std::string& type){std::vector<Obj> result;for(auto& p:objects)if(p->owned&&p->type->name==type&&Panel(p.get()))result.push_back(p.get());return result;}
void Initialize(){
    api.object_get_class=[](Obj o)->Klass{return Get(o).type;};
    api.class_is_assignable_from=[](Klass a,Klass b){return a==b || (static_cast<Class*>(a)->name=="fhw"&&static_cast<Class*>(b)->name=="CartographyBannerView");};
    api.object_new=[](Klass c)->Obj{return Make(static_cast<Class*>(c)->name,true);};
    api.object_unbox=[](Obj o)->void*{return Get(o).value.data();};
    api.string_new=[](const char* s)->Obj{return Str(s);};
    api.gchandle_new=[](Obj o,bool)->GCHandle{handles[++next]=o;return next;};
    api.gchandle_get_target=[](GCHandle h)->Obj{Check(handles.contains(h),"Stale GC root");return handles.at(h);};
    api.gchandle_free=[](GCHandle h){Check(handles.erase(h)==1,"Double GC release");};
    api.class_get_field_from_name=[](Klass,const char* n)->void*{auto it=offsets.find(n);return it==offsets.end()?nullptr:&it->second;};
    api.field_get_offset=[](void* field)->size_t{return *static_cast<size_t*>(field);};
}
}
namespace managed {
Api api{};
Klass Api::Class(const char*,const char*,const char* n)const{return fake::Type(n);}
Method Api::FindVerified(Klass type,const char* n,int argc,uintptr_t rva)const{
    fake::Check(rva!=0,"Missing native guard");auto p=std::make_unique<fake::MethodInfo>(fake::MethodInfo{static_cast<fake::Class*>(type),n,argc,rva});auto result=p.get();fake::methods.push_back(std::move(p));return result;
}
Obj Api::New(Klass type,std::initializer_list<void*> a)const{
    auto result=fake::Make(static_cast<fake::Class*>(type)->name,true);
    if(static_cast<fake::Class*>(type)->name=="Length")std::memcpy(fake::Get(result).value.data(),*a.begin(),sizeof(float));return result;
}
Obj Api::Field(Obj o,const char* n)const{return fake::Get(o).refs[n];}
void* Api::FieldAddress(Obj o,const char* n)const{auto& p=fake::Get(o);std::string name=n;if(name=="id")return &p.world;if(name=="<dugf>k__BackingField")return &p.zoom;throw std::runtime_error("Unexpected raw field");}
Obj Api::Call(Obj self,const char* n,std::initializer_list<void*> args)const{
    using namespace fake;auto& o=Get(self);std::string name=n;std::vector<void*> a(args);
    if(name=="get_Count")return Box(int(o.type->name=="IntDict"?o.indexed.size():o.strings.size()));
    if(name=="TryGetValue"){
        Obj value=nullptr;if(o.type->name=="IntDict"){auto i=o.indexed.find(*static_cast<int*>(a[0]));if(i!=o.indexed.end())value=i->second;}
        else {auto i=o.strings.find(Get(a[0]).text);if(i!=o.strings.end())value=i->second;}
        *static_cast<Obj*>(a[1])=value;return Box(value!=nullptr);
    }
    if(name=="Remove"){return Box(o.strings.erase(Get(a[0]).text)>0);}
    if(name=="Contains")return Box(o.strings.contains(Get(a[0]).text));
    if(name=="get_panel")return Panel(self);if(name=="get_parent")return o.parent;
    if(name=="get_visible")return Box(o.visible);if(name=="get_worldBound")return Box(o.bound);if(name=="get_worldClip")return Box(o.clip);
    if(name=="get_resolvedStyle"){auto p=Make("ResolvedStyle");Get(p).styleOwner=self;return p;}
    if(name=="get_display" || name=="get_visibility")return Box(int(Get(o.styleOwner).numbers[name]));
    if(name=="get_style"){auto p=Make("Style");Get(p).styleOwner=self;return p;}
    const std::string get="UnityEngine.UIElements.IStyle.get_",set="UnityEngine.UIElements.IStyle.set_";
    if(name.starts_with(get)){auto p=Make("StyleBox");Get(p).property=name.substr(get.size());Get(p).value.fill(0);auto& previous=Get(o.styleOwner).styles[Get(p).property];std::memcpy(Get(p).value.data(),previous.data(),16);return p;}
    if(name.starts_with(set)){Check(Get(o.styleOwner).owned,"Style mutation of native map");std::memcpy(Get(o.styleOwner).styles[name.substr(set.size())].data(),a[0],16);++writes;return nullptr;}
    if(name=="set_value"){std::memcpy(o.value.data(),a[0],sizeof(float));return nullptr;}
    if(name=="set_pickingMode"){Check(o.owned,"Native picking mutation");o.picking=*static_cast<int*>(a[0]);return nullptr;}
    if(name=="set_focusable")return nullptr;
    if(name=="set_name"||name=="set_text"){Check(o.owned,"Native text mutation");o.text=Get(a[0]).text;++writes;return nullptr;}
    if(name=="Add"){Check(Get(a[0]).owned,"Attach only owned UI");Attach(self,a[0]);return nullptr;}
    if(name=="RemoveFromHierarchy"){Check(o.owned,"Detach only owned UI");if(nativeRemoveFails)throw std::runtime_error("Synthetic detach failure");Detach(self);return nullptr;}
    throw std::runtime_error("Unexpected fake call: "+name);
}
Obj Api::Invoke(Method method,Obj self,std::initializer_list<void*> args)const{
    using namespace fake;auto& m=*static_cast<const MethodInfo*>(method);std::vector<void*> a(args);Check(int(a.size())==m.argc,"Native arity mismatch");
    if(m.name=="LocalToWorld"||m.name=="WorldToLocal"){
        auto& root=Get(a[0]);auto p=*static_cast<Vec2*>(a[1]);
        return Box(m.name=="LocalToWorld"?Vec2{root.origin.x+p.x*root.scale.x,root.origin.y+p.y*root.scale.y}:Vec2{(p.x-root.origin.x)/root.scale.x,(p.y-root.origin.y)/root.scale.y});
    }
    if(m.name=="bepx"){
        Check(m.rva==0x1C4EB30,"Map to pixel RVA");const auto zoom=*static_cast<double*>(a[3]);
        return Box(Vec2{float((*static_cast<float*>(a[0])+.5)*100*zoom),float((*static_cast<float*>(a[1])+.5)*100*zoom)});
    }
    if(m.name=="bepv"){
        Check(m.rva==0x1C4E8D0,"Pixel to map RVA");auto p=*static_cast<Vec2*>(a[0]);auto& root=Get(a[2]);
        auto result=Box(Vec2{float(std::floor((p.x-root.origin.x)/root.scale.x/(100*root.zoom))),float(std::floor((p.y-root.origin.y)/root.scale.y/(100*root.zoom)))});
        if(onConvert)onConvert();return result;
    }
    auto& o=Get(self);
    if(m.name==".ctor"&&m.type->name=="fgh"){
        Check(m.rva==0x1C3C420&&*static_cast<int*>(a[6])==-1,"Native marker ctor ABI");
        auto root=Make("VisualElement",true);Get(root).picking=1;o.refs["<dtwe>k__BackingField"]=root;
        o.refs["id"]=a[0];o.refs["group"]=a[7];o.refs["manager"]=a[8];o.refs["layer"]=a[9];
        o.origin={*static_cast<float*>(a[1]),*static_cast<float*>(a[2])};o.zoom=*static_cast<double*>(a[5]);
        Get(Group(a[8],Get(a[7]).text,true)).strings[Get(a[0]).text]=self;++creates;return nullptr;
    }
    if(m.name=="beem"){Check(m.rva==0x1B9F530,"Native group enable ABI");Get(o.refs["dttx"]).strings[Get(a[0]).text]=a[0];return nullptr;}
    if(m.name=="beec"){auto group=Group(self,Get(a[1]).text);return group?Get(group).strings[Get(a[0]).text]:nullptr;}
    if(m.name=="beft"){Check(m.rva==0x1C3CBB0,"Native reposition ABI");Attach(a[0],o.refs["<dtwe>k__BackingField"]);return nullptr;}
    if(m.name=="begd"){
        if(nativeRemoveFails)throw std::runtime_error("Synthetic dispose failure");
        auto root=o.refs["<dtwe>k__BackingField"];Detach(root);
        auto group=Group(o.refs["manager"],Get(o.refs["group"]).text);if(group)Get(group).strings.erase(Get(o.refs["id"]).text);++disposes;return nullptr;
    }
    throw std::runtime_error("Unexpected fake invocation: "+m.name);
}
}
Module* module=nullptr;
Module::Module(std::filesystem::path path):folder(std::move(path)){}
managed::Obj Module::FindService(const char* name)const{return fake::services[name];}
void Log(const std::string&){}
namespace resourceMap {
std::vector<Placement> fakePlacements;
void InstallIcons(){}
void LoadItemIcon(managed::Obj element,int id){fake::Check(id==101||id==102,"Use native item icon ID, not item ID");fake::Get(element).numbers["icon"]=id;}
std::span<const Resource> Catalog(){static const Resource catalog[]{{1,2,1,101,"Frêne"},{2,24,1,102,"Fer"}};return catalog;}
std::span<const Placement> Placements(){return fakePlacements;}
}
namespace {
using namespace fake;using namespace resourceMap;
struct Fixture {
    Module state{std::filesystem::path{}};Obj service{},dictionary{},banner{},full{};uint64_t now=1000;
    Fixture(){module=&state;state.ready=true;state.servicesReady=true;state.settings.resourceMapEnabled=true;state.settings.resourceMapItems={1,2};
        state.compositor.reset(Make("Compositor"));services["fng"]=Make("Player");
        service=services["fkc"]=Make("fhk");dictionary=Make("IntDict");Get(service).refs["dueo"]=dictionary;
        banner=View(0);full=View(1);nativeRemoveFails=false;creates=disposes=nativeWrites=0;onConvert={};
        fakePlacements={{10,1,1,1,1,4,false},{10,1,1,1,2,2,false},{11,1,3,2,1,5,true},{20,2,1,1,1,99,false}};
    }
    Obj View(int key){auto owner=Make(key?"fhw":"CartographyBannerView");auto root=Make("NativeRoot");Get(root).panel=Make("Panel");
        auto world=Make("WorldMapData"),manager=Make("fga");Get(manager).refs["dttw"]=Make("StringDict");Get(manager).refs["dttx"]=Make("StringDict");
        Get(owner).refs={{"dugq",root},{"<dugi>k__BackingField",world},{"<dugk>k__BackingField",manager},{"dugv",service},{"dugy",services["fng"]}};
        Get(dictionary).indexed[key]=owner;return owner;}
    Obj Root(Obj view){return Get(view).refs["dugq"];}
    void Tick(){now+=250;PumpRender(now);}
    ~Fixture(){nativeRemoveFails=false;onConvert={};RestoreRender();Check(!RenderCleanupPending(),"Renderer cleanup pending");state.compositor.reset();module=nullptr;Check(handles.empty(),"Renderer leaked GC roots");}
};
void BothMapsAndCounts(){Fixture f;f.Tick();Check(LiveMarkers(f.banner).size()==2&&LiveMarkers(f.full).size()==2,"Both maps must display exact cells");
    auto labels=AllOwned("Label");bool first=false,second=false,interior=false;
    for(auto l:labels){Check(Get(l).picking==1,"Labels must preserve travel picking");first|=Get(l).text=="4";second|=Get(l).text=="2";interior|=Get(l).text=="5*";Check(Get(l).text.find("99")==std::string::npos,"Other world leaked");}
    Check(first&&second&&interior,"Per-resource counts and interior marker missing");
    int icons=0;for(auto e:AllOwned("VisualElement"))if(Get(e).numbers.contains("icon")){++icons;Check(Get(e).picking==1,"Resource images must not consume map clicks");}
    Check(icons==6,"Every visible resource must use its item image");
    auto before=creates,previousWrites=writes;f.Tick();Check(creates==before&&writes==previousWrites,"Unchanged frames must reuse markers without UI writes");
    for(auto m:LiveMarkers(f.full))Check(Get(Get(m).refs["<dtwe>k__BackingField"]).picking==1,"Native marker root must ignore picking");
}
void PanZoomAndWorld(){Fixture f;f.Tick();auto old=LiveMarkers(f.full).front();
    Get(f.Root(f.full)).origin={-5000,-5000};f.Tick();Check(LiveMarkers(f.full).empty(),"Viewport pan must cull offscreen cells");
    Get(f.Root(f.full)).origin={};Get(Get(f.full).refs["<dugi>k__BackingField"]).world=2;f.Tick();Check(LiveMarkers(f.full).size()==1,"Viewed world must select correct map data");
    Get(f.Root(f.banner)).scale={2,2};Get(f.Root(f.banner)).clip={0,0,300,300};f.Tick();Check(LiveMarkers(f.banner).size()==1,"Native panel scale/viewport query must cull correctly");
    Check(!Get(old).refs["<dtwe>k__BackingField"]||!Panel(Get(old).refs["<dtwe>k__BackingField"]),"Old marker remained attached");
}
void HiddenReplacedStopped(){Fixture f;f.Tick();auto oldRoot=f.Root(f.banner);Get(oldRoot).visible=false;f.Tick();Check(LiveMarkers(f.banner).empty(),"Hidden map kept markers");
    f.banner=f.View(0);f.Tick();Check(LiveMarkers(f.banner).size()==2,"Recreated map not discovered");
    f.state.settings.resourceMapEnabled=false;f.Tick();Check(!RenderCleanupPending(),"Disabled filter did not clean both views");
}
void FailClosedAndRetry(){Fixture f;f.Tick();nativeRemoveFails=true;f.state.settings.resourceMapEnabled=false;f.Tick();Check(RenderCleanupPending(),"Failed cleanup must retain ownership");auto before=creates;
    f.state.settings.resourceMapEnabled=true;f.Tick();Check(creates==before,"No replacements while cleanup is pending");
    nativeRemoveFails=false;f.Tick();Check(LiveMarkers(f.banner).size()==2,"Cleanup recovery did not restore rendering");
    onConvert=[&]{services["fkc"]=nullptr;};f.Tick();Check(!RenderCleanupPending(),"Stale service snapshot must clean rendered markers");
}
void NativeRefreshAndCollision(){Fixture f;f.Tick();auto m=LiveMarkers(f.banner).front();auto& marker=Get(m);auto group=Group(marker.refs["manager"],Get(marker.refs["group"]).text);
    auto replacement=Make("ForeignMarker");Get(group).strings[Get(marker.refs["id"]).text]=replacement;
    // Replacement key is foreign; renderer must never delete it by calling begd.
    f.state.settings.resourceMapEnabled=false;f.Tick();Check(Get(group).strings[Get(marker.refs["id"]).text]==replacement,"Cleanup removed a foreign replacement");
}
void LimitsAndInvalidGeometry(){Fixture f;fakePlacements.clear();for(int x=0;x<60;++x)for(int y=0;y<60;++y)fakePlacements.push_back({x*100+y,1,x,y,1,1,false});
    Get(f.Root(f.full)).scale={.05f,.05f};Get(f.Root(f.full)).bound={0,0,300,300};Get(f.Root(f.full)).clip={0,0,300,300};f.Tick();
    Check(LiveMarkers(f.full).size()<=384,"Visible resource budget exceeded");bool hint=false;for(auto l:AllOwned("Label"))hint|=Get(l).text.find("zoomez")!=std::string::npos;Check(hint,"Limited/colliding view needs zoom hint");
    Get(f.Root(f.full)).scale.x=std::numeric_limits<float>::quiet_NaN();f.Tick();Check(LiveMarkers(f.full).empty(),"Invalid transform must fail closed");
}
void MiniSurfaceIndependentOfSelection(){Fixture f;
    f.state.settings.resourceMapEnabled=false;f.state.settings.resourceMapItems.clear();
    {
        auto surface=ReadMiniMapSurface();Check(surface.root.get()==f.Root(f.banner),"Picker must access mini-map when markers disabled and selection empty");
    }
    Get(f.Root(f.banner)).scale={2,2};Get(f.Root(f.banner)).origin={10,20};
    Get(f.Root(f.banner)).bound={10,20,600,600};Get(f.Root(f.banner)).clip={30,60,400,400};
    {
        auto surface=ReadMiniMapSurface();
        Check(surface.width==200&&surface.height==200&&surface.left==10&&surface.top==20,"Picker dimensions must account for panel scale and clipping");
    }
    Get(f.Root(f.banner)).visible=false;Check(!ReadMiniMapSurface().root.get(),"Hidden mini-map must not expose picker surface");
    Get(f.Root(f.banner)).visible=true;Get(f.dictionary).indexed.erase(0);
    Check(!ReadMiniMapSurface().root.get(),"Full map is never mistaken for mini-map");
}
}
int main(){try{fake::Initialize();resourceMap::InstallRender();for(auto test:{BothMapsAndCounts,PanZoomAndWorld,HiddenReplacedStopped,FailClosedAndRetry,NativeRefreshAndCollision,LimitsAndInvalidGeometry,MiniSurfaceIndependentOfSelection})test();std::cout<<"ResourceMapRenderTests: 7 suites passed\n";return 0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
