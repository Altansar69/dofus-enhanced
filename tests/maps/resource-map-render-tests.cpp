#include "maps/resource-map-render.hpp"
#include "maps/resource-map-policy.hpp"
#include "core/module.hpp"
#include "appearance/features.hpp"
#include "ui/options-ui.hpp"
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
    double zoom=1,duge=1; int world=1;
    bool owned=false,visible=true,clusterable=false,expanded=false;
    int picking=0;
};
std::map<std::string,std::unique_ptr<Class>> classes;
std::vector<std::unique_ptr<Object>> objects;
std::vector<std::unique_ptr<MethodInfo>> methods;
std::map<GCHandle,Obj> handles;
std::map<std::string,Obj> services;
GCHandle next=0xA987000000ULL;
bool nativeRemoveFails=false; int creates=0,disposes=0,nativeWrites=0,writes=0,groupJoins=0,groupLeaves=0,placements=0;
std::vector<Obj> centeredViews;
std::function<void()> onConvert;
std::map<std::string,size_t> offsets{{"dueo",0x98},{"dugq",0x68},{"dugv",0x90},{"dugy",0xA8},
    {"<dugi>k__BackingField",0x38},{"<dugk>k__BackingField",0x48},{"<dugf>k__BackingField",0x20},{"<duge>k__BackingField",0x18},
    {"id",0x10},{"<dtwe>k__BackingField",0x10},{"dttw",0x10},{"dttx",0x18},{"dtty",0x20},{"dtwz",0x2D8},{"<dtwx>k__BackingField",0x2D0},{"<dtww>k__BackingField",0x2C8}};
// Style properties written through Length boxes (value, unit) by the renderer.
bool LengthProperty(const std::string& p){
    for(const char* name:{"width","height","left","top","right","bottom","fontSize","borderTopLeftRadius","borderTopRightRadius","borderBottomLeftRadius","borderBottomRightRadius"})if(p==name)return true;
    return false;
}
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
std::map<std::string,Obj>& PositionGroups(Obj owner){auto manager=Get(owner).refs["<dugk>k__BackingField"];return Get(Get(manager).refs["dtty"]).strings;}
Obj GroupLayer(Obj owner){return Get(owner).refs["layer24"];}
std::vector<Obj> AllOwned(const std::string& type){std::vector<Obj> result;for(auto& p:objects)if(p->owned&&p->type->name==type&&Panel(p.get()))result.push_back(p.get());return result;}
std::string Key(const Vec2& v){return std::to_string(int(v.x))+","+std::to_string(int(v.y));}
int IndexIn(Obj parent,Obj child){const auto& c=Get(parent).children;for(size_t i=0;i<c.size();++i)if(c[i]==child)return int(i);return -1;}
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
    if(static_cast<fake::Class*>(type)->name=="Length"){std::memcpy(fake::Get(result).value.data(),*a.begin(),sizeof(float));std::memcpy(fake::Get(result).value.data()+4,*(a.begin()+1),sizeof(int));}
    return result;
}
Obj Api::Field(Obj o,const char* n)const{return fake::Get(o).refs[n];}
std::string Api::String(Obj o)const{return o?fake::Get(o).text:std::string{};}
void* Api::FieldAddress(Obj o,const char* n)const{auto& p=fake::Get(o);std::string name=n;if(name=="id")return &p.world;if(name=="<dugf>k__BackingField")return &p.zoom;if(name=="<duge>k__BackingField")return &p.duge;
    if(name=="<dtwx>k__BackingField"){fake::Check(p.type->name=="fgk","Only position groups expose the spread flag");return &p.expanded;}throw std::runtime_error("Unexpected raw field");}
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
    if(name=="get_contentContainer")return o.refs.contains("content")?o.refs["content"]:self;
    if(name=="get_visible")return Box(o.visible);if(name=="get_worldBound")return Box(o.bound);if(name=="get_worldClip")return Box(o.clip);
    if(name=="get_resolvedStyle"){auto p=Make("ResolvedStyle");Get(p).styleOwner=self;return p;}
    if(name=="get_display" || name=="get_visibility")return Box(int(Get(o.styleOwner).numbers[name]));
    if(name=="get_style"){auto p=Make("Style");Get(p).styleOwner=self;return p;}
    const std::string get="UnityEngine.UIElements.IStyle.get_",set="UnityEngine.UIElements.IStyle.set_";
    if(name.starts_with(get)){auto p=Make("StyleBox");Get(p).property=name.substr(get.size());Get(p).value.fill(0);auto& previous=Get(o.styleOwner).styles[Get(p).property];std::memcpy(Get(p).value.data(),previous.data(),16);return p;}
    if(name.starts_with(set)){Check(Get(o.styleOwner).owned,"Style mutation of native map");std::memcpy(Get(o.styleOwner).styles[name.substr(set.size())].data(),a[0],16);++writes;return nullptr;}
    if(name=="set_value"){std::memcpy(o.value.data(),a[0],o.property=="color"||o.property.ends_with("Color")?16:LengthProperty(o.property)?8:sizeof(float));return nullptr;}
    if(name=="set_pickingMode"){Check(o.owned,"Native picking mutation");o.picking=*static_cast<int*>(a[0]);return nullptr;}
    if(name=="set_focusable")return nullptr;
    if(name=="set_name"||name=="set_text"){Check(o.owned,"Native text mutation");o.text=Get(a[0]).text;++writes;return nullptr;}
    if(name=="get_text")return Str(o.text);
    if(name=="Add"){Check(Get(a[0]).owned,"Attach only owned UI");Attach(self,a[0]);return nullptr;}
    if(name=="IndexOf")return Box(IndexIn(self,a[0]));
    if(name=="Insert"){Check(Get(a[1]).owned,"Insert only owned UI");const int index=*static_cast<int*>(a[0]);Check(index>=0&&index<=int(o.children.size()),"Insert index in range");Detach(a[1]);o.children.insert(o.children.begin()+index,a[1]);Get(a[1]).parent=self;return nullptr;}
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
    if(m.name=="beus"){
        Check(m.rva==0x1BFA370&&a.size()==1&&*static_cast<bool*>(a[0]),"Native centering must request current player position");
        Check(o.type->name=="CartographyBannerView","Never center the full map from the mini-map control");
        centeredViews.push_back(self);return nullptr;
    }
    if(m.name=="bedd"){Check(m.rva==0x1B92EF0&&a.empty(),"Native hint size ABI");return Box(32.0f);} // fhw.bedd: the game's hint size, both views
    if(m.name=="bexb"){
        // fhw.bexb: the game's layer of a hint category, created on demand as a child of the map content.
        Check(m.rva==0x1C04C70&&*static_cast<int*>(a[0])==24,"Only the game's position group layer is requested");
        if(!o.refs["layer24"]){auto layer=Make("VisualElement");Attach(o.refs["dugq"],layer);o.refs["layer24"]=layer;Get(o.refs["<dugk>k__BackingField"]).refs["groupLayer"]=layer;}
        return o.refs["layer24"];
    }
    if(m.name==".ctor"&&m.type->name=="fgm"){
        Check(m.rva==0x1C40D70&&a.size()==15&&*static_cast<int*>(a[6])==-1,"Native icon marker ctor ABI");
        const float width=*static_cast<float*>(a[3]),height=*static_cast<float*>(a[4]);
        Check(width==32&&height==32,"The constructor receives the game's own hint size (fhw.bedd): the native placement sizes the root from it");
        o.numbers["size"]=width;
        const int icon=*static_cast<int*>(a[7]);Check(icon==101||icon==102,"Icon marker must receive the native item icon address");
        const float* tint=static_cast<const float*>(a[10]);Check(tint[0]==1&&tint[1]==1&&tint[2]==1&&tint[3]==1,"Item images stay untinted");
        Check(Get(a[11]).text.empty()&&*static_cast<uint32_t*>(a[12])==0&&*static_cast<bool*>(a[14]),"No caption or reference id; marker shown");
        Check(Get(a[13]).owned&&Get(a[13]).type->name=="VisualElement","The marker's layer is the mod's own hint layer");
        // The native constructor loads the item image onto the root itself.
        auto root=Make("VisualElement",true);Get(root).picking=1;Get(root).numbers["icon"]=icon;o.refs["<dtwe>k__BackingField"]=root;o.numbers["icon"]=icon;o.clusterable=true;
        o.refs["id"]=a[0];o.refs["group"]=a[8];o.refs["manager"]=a[9];o.refs["layer"]=a[13];
        o.origin={*static_cast<float*>(a[1]),*static_cast<float*>(a[2])};o.zoom=*static_cast<double*>(a[5]);
        Get(Group(a[9],Get(a[8]).text,true)).strings[Get(a[0]).text]=self;++creates;return nullptr;
    }
    if(m.name=="beem"){Check(m.rva==0x1B9F530,"Native group enable ABI");Get(o.refs["dttx"]).strings[Get(a[0]).text]=a[0];return nullptr;}
    if(m.name=="beec"){auto group=Group(self,Get(a[1]).text);return group?Get(group).strings[Get(a[0]).text]:nullptr;}
    if(m.name=="bedx"){
        Check(m.rva==0x1B9E370&&o.type->name=="fga","Native position group registration ABI");
        auto& marker=Get(a[1]);Check(marker.clusterable,"Position groups accept only opted-in markers");
        Check(Key(*static_cast<Vec2*>(a[0]))==Key(Vec2{float(std::round(marker.origin.x/100-.5)),float(std::round(marker.origin.y/100-.5))}),"Group key must be the marker's map coordinates");
        Check(a[2]==o.refs["groupLayer"]&&a[2]!=nullptr,"Position groups live in the game's own group layer");
        auto& groups=Get(o.refs["dtty"]).strings;auto& cluster=groups[Key(*static_cast<Vec2*>(a[0]))];
        if(!cluster){cluster=Make("fgk",true);Get(cluster).refs["dtwz"]=Make("Label",true);Get(Get(cluster).refs["dtwz"]).text="";Get(cluster).refs["<dtww>k__BackingField"]=cluster;Attach(a[2],cluster);}
        Get(Get(cluster).refs["dtwz"]).text=std::to_string(Get(cluster).strings.size()+1); // native: member count
        Check(!marker.refs["cluster"],"A marker joins one position group");
        Get(cluster).strings[Get(marker.refs["id"]).text]=a[1];marker.refs["cluster"]=cluster;++groupJoins;return nullptr;
    }
    if(m.name=="bedz"){
        Check(m.rva==0x1B9E780&&o.type->name=="fga","Native position group lookup ABI");
        auto& groups=Get(o.refs["dtty"]).strings;auto it=groups.find(Key(*static_cast<Vec2*>(a[0])));return it==groups.end()?nullptr:it->second;
    }
    if(m.name=="bedy"){
        Check(m.rva==0x1B9E5C0&&o.type->name=="fga","Native position group removal ABI");
        auto& marker=Get(a[0]);auto cluster=marker.refs["cluster"];if(!cluster)return nullptr; // native: no group contains it, nothing to do
        Get(cluster).strings.erase(Get(marker.refs["id"]).text);marker.refs["cluster"]=nullptr;++groupLeaves;
        if(Get(cluster).strings.empty()){Detach(cluster);std::erase_if(Get(o.refs["dtty"]).strings,[&](const auto& entry){return entry.second==cluster;});}
        return nullptr;
    }
    if(m.name=="beft"){
        Check(m.rva==0x1C410D0&&o.type->name=="fgm"&&a.size()==3,"Native icon marker placement ABI");
        Check(o.refs["cluster"]!=nullptr,"Position after joining the group so the group is placed too");
        Check(a[0]==o.refs["layer"],"The placement layer is the one given to the constructor (the native code ignores any other)");
        const double zoom=*static_cast<double*>(a[1]),scale=*static_cast<double*>(a[2]);
        Check(std::isfinite(zoom)&&zoom>0&&std::isfinite(scale)&&scale>0,"Finite positive zoom and scale");
        o.numbers["zoom"]=zoom;o.numbers["scale"]=scale;++placements;
        Attach(a[0],o.refs["<dtwe>k__BackingField"]);return nullptr;
    }
    if(m.name=="begd"){
        Check(m.rva==0x1C411D0&&o.type->name=="fgm","Native icon marker dispose ABI");
        if(nativeRemoveFails)throw std::runtime_error("Synthetic dispose failure");
        Check(!o.refs["cluster"],"Dispose must follow position group removal");
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
managed::Obj ItemIconAddress(int id){fake::Check(id==101||id==102,"Use native item icon ID, not item ID");return fake::Box(id);}
void LoadItemIcon(managed::Obj,int){throw std::runtime_error("The native marker loads its own image; the mod must not load a second one");}
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
        banner=View(0);full=View(1);nativeRemoveFails=false;creates=disposes=nativeWrites=groupJoins=groupLeaves=placements=0;onConvert={};centeredViews.clear();
        fakePlacements={{10,1,1,1,1,4,false},{10,1,1,1,2,2,false},{11,1,3,2,1,5,true},{20,2,1,1,1,99,false}};
    }
    Obj View(int key){auto owner=Make(key?"fhw":"CartographyBannerView");auto root=Make("NativeRoot"),host=Make("NativeHost");Get(host).panel=Make("Panel");Attach(host,root);
        auto world=Make("WorldMapData"),manager=Make("fga");Get(manager).refs["dttw"]=Make("StringDict");Get(manager).refs["dttx"]=Make("StringDict");Get(manager).refs["dtty"]=Make("StringDict");
        Get(owner).refs={{"dugq",root},{"<dugi>k__BackingField",world},{"<dugk>k__BackingField",manager},{"dugv",service},{"dugy",services["fng"]}};
        Get(dictionary).indexed[key]=owner;return owner;}
    Obj Root(Obj view){return Get(view).refs["dugq"];}
    void Tick(){now+=250;PumpRender(now);}
    ~Fixture(){nativeRemoveFails=false;onConvert={};RestoreRender();Check(!RenderCleanupPending(),"Renderer cleanup pending");state.compositor.reset();module=nullptr;Check(handles.empty(),"Renderer leaked GC roots");}
};
Obj MarkerRoot(Obj m){return Get(m).refs.at("<dtwe>k__BackingField");}
Obj MarkerLabel(Obj m){const auto& children=Get(MarkerRoot(m)).children;Check(children.size()==1,"One quantity label per native marker: the image is the root's own background");auto c=children.front();Check(Get(c).type->name=="Label","Quantity label missing");return c;}
Obj ModLayer(Obj owner){for(auto c:Get(Get(owner).refs["dugq"]).children)if(Get(c).owned&&Get(c).type->name=="VisualElement")return c;return nullptr;}
int Display(Obj element){int value=0;const auto& styles=Get(element).styles;if(!styles.contains("display"))return 0;std::memcpy(&value,styles.at("display").data(),4);return value;}
float StyleValue(Obj element,const char* name){float value=0;std::memcpy(&value,Get(element).styles.at(name).data(),4);return value;}
int StyleUnit(Obj element,const char* name){int unit=0;std::memcpy(&unit,Get(element).styles.at(name).data()+4,4);return unit;}
void BothMapsAndCounts(){Fixture f;f.Tick();Check(LiveMarkers(f.banner).size()==3&&LiveMarkers(f.full).size()==3,"Both maps must display one native marker per resource and cell");
    auto labels=AllOwned("Label");bool first=false,second=false,interior=false;
    for(auto l:labels){Check(Get(l).picking==1,"Labels must preserve travel picking");first|=Get(l).text=="4";second|=Get(l).text=="2";interior|=Get(l).text=="5*";Check(Get(l).text.find("99")==std::string::npos,"Other world leaked");}
    Check(first&&second&&interior,"Per-resource counts and interior marker missing");
    for(auto l:labels)if(Get(l).text=="4"||Get(l).text=="2"||Get(l).text=="5*"){
        const auto& style=Get(l).styles;
        float foreground[4]{},background[4]{};
        std::memcpy(foreground,style.at("color").data(),16);std::memcpy(background,style.at("backgroundColor").data(),16);
        const float font=StyleValue(l,"fontSize"),height=StyleValue(l,"height");
        // Marker of 32*zoom/0.6 local units at display scale 1: badge of 0.34 of it, font 0.26.
        Check(foreground[0]<.15f&&foreground[3]==1&&background[0]>.9f&&background[3]==1&&font>13&&font<15&&height>17&&height<19,"Quantity badges need large dark text on a fully opaque light background, proportional to the marker");
        int weight=0;std::memcpy(&weight,style.at("unityFontStyleAndWeight").data(),4);Check(weight==1,"Quantity text must be bold");
        Check(StyleValue(l,"right")==0&&StyleValue(l,"bottom")==0,"Quantity badge anchors to the native marker's bottom-right corner");
    }
    int icons=0;for(auto m:LiveMarkers(f.full))if(Get(m).numbers.contains("icon")){++icons;Check(Get(MarkerRoot(m)).picking==1,"Resource markers must not consume map clicks");
        Check(Get(MarkerRoot(m)).numbers.at("icon")==Get(m).numbers.at("icon"),"The native constructor loads the catalog image onto the marker root");}
    Check(icons==3&&creates==6,"Every marker must use its item image through the native icon marker");
    auto before=creates,previousWrites=writes;f.Tick();Check(creates==before&&writes==previousWrites,"Unchanged frames must reuse markers without UI writes");
}
void NativeLayersAndSizes(){Fixture f;f.Tick();
    for(auto owner:{f.banner,f.full}){
        auto root=f.Root(owner),layer=ModLayer(owner),groupLayer=GroupLayer(owner);
        Check(layer&&groupLayer&&Get(layer).parent==root&&Get(groupLayer).parent==root,"The mod's hint layer and the game's group layer are children of the map content");
        Check(IndexIn(root,layer)+1==IndexIn(root,groupLayer),"The mod's layer sits just below the game's group layer so native badges draw above the stacks");
        Check(StyleValue(layer,"width")==100&&StyleUnit(layer,"width")==1&&StyleValue(layer,"height")==100&&StyleUnit(layer,"height")==1,"The layer is 100% x 100% like the game's own hint layers: a zero layout would exclude its markers from the native spread");
        Check(Display(layer)==0&&Get(layer).picking==1,"The layer is displayed and unpickable");
        for(auto m:LiveMarkers(owner)){
            Check(Get(m).numbers.at("size")==32,"Markers use the game's hint size on both maps");
            Check(Get(m).numbers.at("scale")==0.6&&Get(m).numbers.at("zoom")==1,"Placement uses the scale the game's own refresh applies (0.6 at display scale 1)");
            Check(Get(MarkerRoot(m)).parent==layer,"Marker roots live in the mod's layer");
            Check(Get(Get(m).refs.at("cluster")).parent==groupLayer,"Cell groups live in the game's group layer");
        }
    }
    // A smaller display scale: the game's refresh formula follows it.
    Get(f.full).duge=0.35;Get(f.Root(f.full)).scale={.35f,.35f};const auto before=placements;f.Tick();
    for(auto m:LiveMarkers(f.full))Check(std::abs(Get(m).numbers.at("scale")-0.3)<1e-9,"Display scales under 0.7 shrink the marker scale like the game's own formula");
    Check(placements==before+3&&creates==6,"Existing markers are re-placed, not recreated");
    // No display scale yet (the map's first layout): nothing is created.
    Get(f.banner).duge=0;f.Tick();Check(LiveMarkers(f.banner).empty(),"A map without display scale is not rendered (the native placement would divide by zero)");
    Get(f.banner).duge=1;f.Tick();Check(LiveMarkers(f.banner).size()==3,"Rendering resumes with the display scale");
}
void NativePositionGroups(){Fixture f;f.Tick();
    auto& groups=PositionGroups(f.full);
    Check(groups.size()==2&&groups.contains("1,1")&&groups.contains("3,2"),"One native position group per cell with resources");
    const auto shared=groups.at("1,1"),single=groups.at("3,2");
    Check(Get(shared).strings.size()==2&&Get(single).strings.size()==1,"Same-cell markers share the game's position group");
    for(auto m:LiveMarkers(f.full)){Check(Get(m).clusterable,"Markers opt into position groups like native hints");
        Check(Get(m).refs.at("cluster")!=nullptr,"Every marker joined its cell group");}
    Check(groupJoins==6&&groupLeaves==0,"Each marker joins exactly one group");
    f.state.settings.resourceMapItems={1};f.Tick();
    Check(Get(shared).strings.size()==1&&groups.size()==2&&groupLeaves==2,"Deselecting a resource removes only its markers from the groups");
    f.state.settings.resourceMapItems={2};f.Tick();
    Check(groups.size()==1&&groups.contains("1,1")&&!Get(single).parent,"Emptied position groups leave the manager and the layer");
    f.state.settings.resourceMapEnabled=false;f.Tick();
    Check(groups.empty()&&PositionGroups(f.banner).size()==1,"Disabling one map releases its position groups only");
}
void GroupTotalsAndSpread(){Fixture f;f.Tick();
    auto& groups=PositionGroups(f.full);const auto shared=groups.at("1,1"),single=groups.at("3,2");
    Check(Get(Get(shared).refs["dtwz"]).text=="6","The collapsed group badge shows the cell's total quantity instead of the member count");
    std::vector<Obj> sharedMarkers,singleMarkers;
    for(auto m:LiveMarkers(f.full))(Get(m).refs.at("cluster")==shared?sharedMarkers:singleMarkers).push_back(m);
    Check(sharedMarkers.size()==2&&singleMarkers.size()==1,"Markers found per group");
    for(auto m:sharedMarkers)Check(Display(MarkerLabel(m))==1,"Per-resource badges stay hidden while the group is collapsed");
    Check(Display(MarkerLabel(singleMarkers.front()))==0&&Get(Get(single).refs["dtwz"]).text=="1","A lone resource keeps its own badge and the game's own group badge");
    Get(shared).expanded=true;const auto before=writes;f.Tick();
    for(auto m:sharedMarkers)Check(Display(MarkerLabel(m))==0,"Per-resource badges show while the game spreads the group");
    Check(writes==before+2,"Spreading rewrites only the two badge displays");
    Get(Get(shared).refs["dtwz"]).text="2";f.Tick();
    Check(Get(Get(shared).refs["dtwz"]).text=="6","The game's own member-count refresh is overridden again by the total");
    Get(shared).expanded=false;f.Tick();
    for(auto m:sharedMarkers)Check(Display(MarkerLabel(m))==1,"Collapsing hides the per-resource badges again");
    const auto settled=writes;f.Tick();Check(writes==settled,"A settled group causes no writes");
    // One of the game's own hints (a drill, an attitude) shares the cell: the
    // collapsed badge still shows the resource total, never the member count.
    Get(shared).strings["drill"]=Make("fgm",true);Get(Get(shared).refs["dtwz"]).text="3";f.Tick();
    Check(Get(Get(shared).refs["dtwz"]).text=="6","With a game hint in the group, the collapsed badge still shows the cell's resource total");
    for(auto m:sharedMarkers)Check(Display(MarkerLabel(m))==1,"Per-resource badges stay hidden in a collapsed mixed group");
    Get(shared).expanded=true;f.Tick();for(auto m:sharedMarkers)Check(Display(MarkerLabel(m))==0,"A spread mixed group shows the per-resource badges");
    Get(shared).expanded=false;Get(shared).strings.erase("drill");f.Tick();Check(Get(Get(shared).refs["dtwz"]).text=="6","The total stays once the game hint leaves");
    // A lone resource stacked with a game hint: its quantity on the group
    // badge, its own badge only once the stack is spread.
    const auto lone=Get(MarkerLabel(singleMarkers.front())).text;
    Get(single).strings["attitude"]=Make("fgm",true);Get(Get(single).refs["dtwz"]).text="2";f.Tick();
    Check(Get(Get(single).refs["dtwz"]).text==lone&&Display(MarkerLabel(singleMarkers.front()))==1,"A lone resource stacked with a game hint shows its quantity on the group badge");
    Get(single).expanded=true;f.Tick();Check(Display(MarkerLabel(singleMarkers.front()))==0,"Its own badge shows once the game spreads the stack");
    Get(single).expanded=false;Get(single).strings.erase("attitude");f.Tick();
    Check(Display(MarkerLabel(singleMarkers.front()))==0,"Alone again, the resource keeps its own badge");
    // The game's own map reset (Display) empties its groups and disposes the markers of the active groups.
    const auto created=creates;
    for(auto m:LiveMarkers(f.full)){auto group=Group(Get(m).refs["manager"],Get(Get(m).refs["group"]).text);Get(group).strings.erase(Get(Get(m).refs["id"]).text);Get(m).refs["cluster"]=nullptr;}
    groups.clear();f.Tick();
    Check(creates==created+3&&LiveMarkers(f.full).size()==3&&groups.size()==2&&Get(Get(groups.at("1,1")).refs["dtwz"]).text=="6","After a native reset the markers and their groups are recreated with the total");
}
void PanZoomAndWorld(){Fixture f;f.Tick();auto old=LiveMarkers(f.full).front();
    Get(f.Root(f.full)).origin={-5000,-5000};f.Tick();Check(LiveMarkers(f.full).empty(),"Viewport pan must cull offscreen cells");
    Get(f.Root(f.full)).origin={};Get(Get(f.full).refs["<dugi>k__BackingField"]).world=2;f.Tick();Check(LiveMarkers(f.full).size()==1,"Viewed world must select correct map data");
    Get(f.Root(f.banner)).scale={2,2};Get(f.Root(f.banner)).clip={0,0,300,300};f.Tick();Check(LiveMarkers(f.banner).size()==2,"Native panel scale/viewport query must cull correctly");
    Check(!Get(old).refs["<dtwe>k__BackingField"]||!Panel(Get(old).refs["<dtwe>k__BackingField"]),"Old marker remained attached");
    // A level of detail change (zoom) re-places the reused markers like the game's refresh.
    const auto before=placements,created=creates;Get(f.full).zoom=2;f.Tick();
    Check(creates==created&&placements>before,"A zoom change re-places the existing markers without recreating them");
}
void HiddenReplacedStopped(){Fixture f;f.Tick();auto oldRoot=f.Root(f.banner);Get(oldRoot).visible=false;f.Tick();Check(LiveMarkers(f.banner).empty(),"Hidden map kept markers");
    f.banner=f.View(0);f.Tick();Check(LiveMarkers(f.banner).size()==3,"Recreated map not discovered");
    f.state.settings.resourceMapEnabled=false;f.state.settings.resourceMiniMapEnabled=false;f.Tick();Check(!RenderCleanupPending(),"Disabled filters did not clean both views");
}
void FailClosedAndRetry(){Fixture f;f.Tick();nativeRemoveFails=true;f.state.settings.resourceMapEnabled=false;f.state.settings.resourceMiniMapEnabled=false;f.Tick();Check(RenderCleanupPending(),"Failed cleanup must retain ownership");auto before=creates;
    f.state.settings.resourceMapEnabled=true;f.state.settings.resourceMiniMapEnabled=true;f.Tick();Check(creates==before,"No replacements while cleanup is pending");
    nativeRemoveFails=false;f.Tick();Check(LiveMarkers(f.banner).size()==3,"Cleanup recovery did not restore rendering");
    onConvert=[&]{services["fkc"]=nullptr;};f.Tick();Check(!RenderCleanupPending(),"Stale service snapshot must clean rendered markers");
}
void NativeRefreshAndCollision(){Fixture f;f.Tick();auto m=LiveMarkers(f.banner).front();auto& marker=Get(m);auto group=Group(marker.refs["manager"],Get(marker.refs["group"]).text);
    auto replacement=Make("ForeignMarker");Get(group).strings[Get(marker.refs["id"]).text]=replacement;
    // Replacement key is foreign; renderer must never delete it by calling begd.
    f.state.settings.resourceMiniMapEnabled=false;f.Tick();Check(Get(group).strings[Get(marker.refs["id"]).text]==replacement,"Cleanup removed a foreign replacement");
}
void LimitsAndInvalidGeometry(){Fixture f;fakePlacements.clear();for(int x=0;x<60;++x)for(int y=0;y<60;++y)fakePlacements.push_back({x*100+y,1,x,y,1,1,false});
    Get(f.Root(f.full)).scale={.05f,.05f};Get(f.Root(f.full)).bound={0,0,300,300};Get(f.Root(f.full)).clip={0,0,300,300};f.Tick();
    // Creation is budgeted per pass and continued on the following frames.
    Check(LiveMarkers(f.full).size()==32,"A pass creates at most 32 native markers");
    f.now+=1;PumpRender(f.now);Check(LiveMarkers(f.full).size()==64,"A cut pass continues on the very next frame");
    for(int i=0;i<40;++i)f.Tick();
    const auto settled=LiveMarkers(f.full).size();Check(settled>64&&settled<=384,"Visible resource budget exceeded");
    f.Tick();Check(LiveMarkers(f.full).size()==settled,"A complete pass settles");
    for(auto l:AllOwned("Label"))Check(Get(l).text.find("zoomez")==std::string::npos,"Dense maps must not display the removed zoom warning");
    Get(f.Root(f.full)).scale.x=std::numeric_limits<float>::quiet_NaN();f.Tick();Check(LiveMarkers(f.full).empty(),"Invalid transform must fail closed");
}
void MiniSurfaceIndependentOfSelection(){Fixture f;
    f.state.settings.resourceMapEnabled=false;f.state.settings.resourceMapItems.clear();
    {
        auto surface=ReadMiniMapSurface();Check(surface.root.get()==Get(f.Root(f.banner)).parent,"Picker must access mini-map when markers disabled and selection empty");
    }
    auto host=Get(f.Root(f.banner)).parent;
    Get(host).scale={2,2};Get(host).origin={10,20};
    Get(host).bound={10,20,600,600};Get(host).clip={30,60,400,400};
    Get(f.Root(f.banner)).scale={.04f,.04f};Get(f.Root(f.banner)).origin={10,20};
    Get(f.Root(f.banner)).bound={10,20,600,600};Get(f.Root(f.banner)).clip={30,60,400,400};
    {
        auto surface=ReadMiniMapSurface();
        Check(surface.width==200&&surface.height==200&&surface.left==10&&surface.top==20,"Picker dimensions must account for panel scale and clipping");
    }
    for(float zoomScale:{.01f,.1f,1.0f,10.0f}){
        Get(f.Root(f.banner)).scale={zoomScale,zoomScale};
        Get(f.Root(f.banner)).bound={30,60,zoomScale*4000,zoomScale*4000};
        auto surface=ReadMiniMapSurface();
        Check(surface.mapRoot.get()==f.Root(f.banner)&&surface.root.get()==host,"Picker must be sibling of native map content");
        Check(surface.width==200&&surface.height==200&&surface.left==10&&surface.top==20,"Map zoom must not change picker geometry");
    }
    Get(host).refs["content"]=f.Root(f.banner);
    Check(!ReadMiniMapSurface().root.get(),"A parent that redirects Add into the map must never host controls");
    Get(host).refs.erase("content");
    Get(host).scale.x=std::numeric_limits<float>::quiet_NaN();
    Check(!ReadMiniMapSurface().root.get(),"Invalid host transform must fail closed");Get(host).scale.x=2;
    Get(f.Root(f.banner)).visible=false;Check(!ReadMiniMapSurface().root.get(),"Hidden mini-map must not expose picker surface");
    Get(f.Root(f.banner)).visible=true;Get(f.dictionary).indexed.erase(0);
    Check(!ReadMiniMapSurface().root.get(),"Full map is never mistaken for mini-map");
}
void IndependentMapVisibility(){Fixture f;f.Tick();
    const auto full=LiveMarkers(f.full);
    f.state.settings.resourceMiniMapEnabled=false;f.Tick();
    Check(LiveMarkers(f.banner).empty()&&LiveMarkers(f.full)==full,"Mini-map opt-out removes only mini-map markers");
    Check(ReadMiniMapSurface().root.get()==Get(f.Root(f.banner)).parent,"Disabled mini-map keeps its picker accessible");
    f.state.settings.resourceMapEnabled=false;f.state.settings.resourceMiniMapEnabled=true;f.Tick();
    Check(LiveMarkers(f.full).empty()&&LiveMarkers(f.banner).size()==3,"Mini-map markers can display independently of the full map");
    f.state.settings.resourceMapEnabled=true;f.Tick();
    Check(LiveMarkers(f.full).size()==3&&LiveMarkers(f.banner).size()==3,"Both switches can be restored without losing the selection");
}
void QuantityBadgeStaysCompact(){Fixture f;f.Tick();
    for(auto owner:{f.banner,f.full})for(auto m:LiveMarkers(owner)){
        const auto label=MarkerLabel(m);
        Check(StyleValue(label,"width")<=22&&StyleValue(label,"height")>17&&StyleValue(label,"height")<19,"Quantity badge must stay a small corner pastille over the native image");
        Check(Get(label).picking==1,"Badges never intercept map clicks");
    }
    // The panel scale alone (wheel zoom) never rewrites the badge: the game scales the marker.
    const auto before=writes;Get(f.Root(f.full)).scale={1.5f,1.5f};f.Tick();Check(writes==before,"A panel scale change rewrites no badge geometry");
}
void AddingMineralKeepsSameAndNeighbouringCells(){Fixture f;
    fakePlacements={{10,1,1,1,1,4,false},{10,1,1,1,2,2,false},{11,1,2,1,1,5,true}};
    for(auto owner:{f.banner,f.full})Get(f.Root(owner)).scale={.6f,.6f};
    f.state.settings.resourceMapItems={1};f.Tick();
    for(auto owner:{f.banner,f.full})Check(LiveMarkers(owner).size()==2,"Both initial mineral locations visible");
    for(auto selection:{std::vector<int>{1,2},std::vector<int>{2},std::vector<int>{1,2}}){
        f.state.settings.resourceMapItems=selection;f.Tick();
        for(auto owner:{f.banner,f.full}){
            const auto markers=LiveMarkers(owner);
            Check(markers.size()==(selection.size()==2?3:1),"Selecting another mineral must not cull a neighbouring selected location");
            auto& groups=PositionGroups(owner);
            Check(groups.contains("1,1")&&Get(groups.at("1,1")).strings.size()==selection.size(),"All selected minerals at the same coordinates share one native position group");
            Check(groups.contains("2,1")==(selection.size()==2),"Neighbouring single-resource cells keep their own group only while selected");
        }
    }
}
void CenterOnlyCurrentMiniMap(){Fixture f;
    f.state.settings.resourceMiniMapEnabled=false;f.state.settings.resourceMapEnabled=false;f.state.settings.resourceMapItems.clear();
    const auto preferences=f.state.settings;
    auto surface=ReadMiniMapSurface();
    Check(CenterMiniMap(surface)&&centeredViews==std::vector<Obj>{f.banner},"Center works with resource rendering disabled");
    Check(f.state.settings==preferences&&Get(f.banner).zoom==1,"Center does not change selection or zoom");
    MiniMapSurface foreign;foreign.root.reset(Get(f.Root(f.full)).parent);foreign.mapRoot.reset(f.Root(f.full));
    Check(!CenterMiniMap(foreign),"Reject the full map surface");
    f.banner=f.View(0);
    Check(!CenterMiniMap(surface)&&centeredViews.size()==1,"Reject a replaced mini-map");
    surface=ReadMiniMapSurface();Get(f.Root(f.banner)).visible=false;
    Check(!CenterMiniMap(surface),"Reject hidden mini-map");Get(f.Root(f.banner)).visible=true;
    f.state.stopRequested=true;Check(!CenterMiniMap(surface),"Never center after stop");f.state.stopRequested=false;
    surface=ReadMiniMapSurface();Check(CenterMiniMap(surface)&&centeredViews.back()==f.banner,"New mini-map gets a fresh center action");
    surface.root.reset();surface.mapRoot.reset();foreign.root.reset();foreign.mapRoot.reset();
}
}
int main(){try{fake::Initialize();resourceMap::InstallRender();for(auto test:{BothMapsAndCounts,NativeLayersAndSizes,NativePositionGroups,GroupTotalsAndSpread,PanZoomAndWorld,HiddenReplacedStopped,FailClosedAndRetry,NativeRefreshAndCollision,LimitsAndInvalidGeometry,MiniSurfaceIndependentOfSelection,IndependentMapVisibility,QuantityBadgeStaysCompact,AddingMineralKeepsSameAndNeighbouringCells,CenterOnlyCurrentMiniMap})test();std::cout<<"ResourceMapRenderTests: 14 suites passed\n";return 0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
