#include "resource-map-render.hpp"
#include "resource-map-policy.hpp"
#include "resource-map-icons.hpp"
#include "module.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <tuple>

namespace resourceMap {
namespace {
using namespace managed;
constexpr const char* UiAssembly = "UnityEngine.UIElementsModule.dll";
constexpr const char* UiSpace = "UnityEngine.UIElements";
constexpr size_t CellBudget = 384, MarkerBudget = 384;
constexpr float FullMarkerSize=50, MiniMarkerSize=44;
struct Vec2 { float x, y; };
struct Rect { float x, y, width, height; };
struct Color { float r, g, b, a; };
// One native icon marker (fgm) per resource and cell. Markers of one cell share
// the game's own position group (fgk): count badge, hover spread and collapse
// are the native cartography behaviour, not a reimplementation.
struct Marker {
    Ref native, root, id, label;
    int x = 0, y = 0, item = 0;
    bool used = false;
    std::string content;
    Vec2 scale{};
};
struct View {
    Ref compositor, service, dictionary, owner, world, root, panel, manager, group, groupItems, layer;
    std::vector<Marker> markers;
    bool cleaning = false;
    int worldId=0;
    bool cached=false;
    Rect viewport{};
    Vec2 scale{},origin{};
    double zoom=0;
    std::vector<int> selection;
};
struct Frame {
    Ref service, dictionary, owner, world, root, panel, manager;
    Bounds bounds{};
    Rect viewport{};
    Vec2 origin{}, scale{};
    double zoom = 0;
    int worldId = 0, key = 0;
    bool valid = false;
};
struct Methods {
    Klass view{}, service{}, marker{}, world{}, element{}, label{};
    Method markerCtor{}, position{}, dispose{}, find{}, enableGroup{}, cluster{}, uncluster{};
    Method mapToPixel{}, pixelToMap{}, toWorld{}, toLocal{}, centerPlayer{};
} methods;
std::array<View, 2> views;
bool installed = false, reported = false;
uint64_t nextPump = 0, serial = 0;
bool Session() {
    return installed && module && module->ready && module->servicesReady && !module->stopped && !module->stopRequested &&
        module->compositor.get();
}
void Report(const std::exception& error) {
    if (!reported) Log(std::string("RESOURCE_MAP_RENDER_ERROR ") + error.what());
    reported = true;
}
bool Finite(Rect r) {
    return std::isfinite(r.x) && std::isfinite(r.y) && std::isfinite(r.width) && std::isfinite(r.height) &&
        r.width > 0 && r.height > 0 && std::isfinite(r.x+r.width) && std::isfinite(r.y+r.height);
}
Rect Intersect(Rect a, Rect b) {
    const double left = std::max(a.x,b.x), top = std::max(a.y,b.y);
    const double right = std::min(double(a.x)+a.width,double(b.x)+b.width);
    const double bottom = std::min(double(a.y)+a.height,double(b.y)+b.height);
    return {float(left),float(top),float(right-left),float(bottom-top)};
}
bool Overlaps(Rect a, Rect b) { return Finite(Intersect(a,b)); }
bool Same(Rect a,Rect b) {return a.x==b.x&&a.y==b.y&&a.width==b.width&&a.height==b.height;}
bool Same(Vec2 a,Vec2 b) {return a.x==b.x&&a.y==b.y;}
bool Is(Obj object, Klass type) { return object && api.object_get_class(object) == type; }
bool Visible(Obj element) {
    if (!element || !api.Call(element,"get_panel")) return false;
    Ref node(element);
    for (int depth=0; node.get(); ++depth) {
        if (depth >= 96 || !api.Value<bool>(api.Call(node.get(),"get_visible"))) return false;
        Ref style(api.Call(node.get(),"get_resolvedStyle"));
        if (api.Value<int>(api.Call(style.get(),"get_display")) != 0 ||
            api.Value<int>(api.Call(style.get(),"get_visibility")) != 0) return false;
        node.reset(api.Call(node.get(),"get_parent"));
    }
    return true;
}
Obj DictionaryItem(Obj dictionary, int key) {
    Obj result = nullptr;
    return api.Value<bool>(api.Call(dictionary,"TryGetValue",{&key,&result})) ? result : nullptr;
}
Obj StringItem(Obj dictionary, Obj key) {
    Obj result = nullptr;
    return dictionary && api.Value<bool>(api.Call(dictionary,"TryGetValue",{key,&result})) ? result : nullptr;
}
bool Current(const Frame& f) {
    return f.valid && Session() && module->FindService("fkc") == f.service.get() &&
        api.Field(f.service.get(),"dueo") == f.dictionary.get() && DictionaryItem(f.dictionary.get(),f.key) == f.owner.get() &&
        api.Field(f.owner.get(),"dugv") == f.service.get() && api.Field(f.owner.get(),"dugy") == module->FindService("fng") &&
        api.Field(f.owner.get(),"dugq") == f.root.get() && api.Field(f.owner.get(),"<dugi>k__BackingField") == f.world.get() &&
        api.Field(f.owner.get(),"<dugk>k__BackingField") == f.manager.get() &&
        api.Call(f.root.get(),"get_panel") == f.panel.get() && Visible(f.root.get());
}
Frame ReadFrame(int key) {
    Frame f; f.key = key;
    f.service.reset(module->FindService("fkc"));
    if (!Is(f.service.get(),methods.service)) return f;
    f.dictionary.reset(api.Field(f.service.get(),"dueo"));
    if (!f.dictionary.get()) return f;
    const int count = api.Value<int>(api.Call(f.dictionary.get(),"get_Count"));
    if (count < 1 || count > 2) return f;
    f.owner.reset(DictionaryItem(f.dictionary.get(),key));
    if (!f.owner.get() || !api.class_is_assignable_from(methods.view,api.object_get_class(f.owner.get()))) return f;
    f.root.reset(api.Field(f.owner.get(),"dugq"));
    f.world.reset(api.Field(f.owner.get(),"<dugi>k__BackingField"));
    f.manager.reset(api.Field(f.owner.get(),"<dugk>k__BackingField"));
    if (!f.root.get() || !Is(f.world.get(),methods.world) || !f.manager.get() || !Visible(f.root.get())) return f;
    f.panel.reset(api.Call(f.root.get(),"get_panel"));
    f.zoom = *static_cast<double*>(api.FieldAddress(f.owner.get(),"<dugf>k__BackingField"));
    f.worldId = *static_cast<int*>(api.FieldAddress(f.world.get(),"id"));
    if (!std::isfinite(f.zoom) || f.zoom <= 0 || f.zoom > 10000) return f;
    const Rect bound = api.Value<Rect>(api.Call(f.root.get(),"get_worldBound"));
    const Rect clip = api.Value<Rect>(api.Call(f.root.get(),"get_worldClip"));
    f.viewport = Intersect(bound,clip);
    if (!Finite(f.viewport) || f.viewport.width < 8 || f.viewport.height < 8) return f;
    Vec2 origin{}, unit{1,1};
    f.origin = api.Value<Vec2>(api.Invoke(methods.toWorld,nullptr,{f.root.get(),&origin}));
    auto endpoint = api.Value<Vec2>(api.Invoke(methods.toWorld,nullptr,{f.root.get(),&unit}));
    f.scale = {endpoint.x-f.origin.x,endpoint.y-f.origin.y};
    if (!std::isfinite(f.scale.x) || !std::isfinite(f.scale.y) || f.scale.x < .001f || f.scale.y < .001f ||
            f.scale.x > 1000 || f.scale.y > 1000) return f;
    Vec2 first{f.viewport.x,f.viewport.y}, last{f.viewport.x+f.viewport.width,f.viewport.y+f.viewport.height};
    const auto a = api.Value<Vec2>(api.Invoke(methods.pixelToMap,nullptr,{&first,f.world.get(),f.root.get()}));
    const auto b = api.Value<Vec2>(api.Invoke(methods.pixelToMap,nullptr,{&last,f.world.get(),f.root.get()}));
    for (float v : {a.x,a.y,b.x,b.y}) if (!std::isfinite(v) || v < -100000 || v > 100000) return f;
    f.bounds = {int(std::min(a.x,b.x)),int(std::min(a.y,b.y)),int(std::max(a.x,b.x)),int(std::max(a.y,b.y))};
    f.valid = true;
    if (!Current(f)) f.valid = false;
    return f;
}
Obj ReadStyle(Obj element,const char* name) {
    Ref style(api.Call(element,"get_style"));
    return api.Call(style.get(),(std::string("UnityEngine.UIElements.IStyle.get_")+name).c_str());
}
void WriteStyle(Obj element,const char* name,Obj value) {
    Ref style(api.Call(element,"get_style"));
    api.Call(style.get(),(std::string("UnityEngine.UIElements.IStyle.set_")+name).c_str(),{api.object_unbox(value)});
}
template<class T> void Style(Obj element,const char* name,T value) {
    Ref box(ReadStyle(element,name)); api.Call(box.get(),"set_value",{&value}); WriteStyle(element,name,box.get());
}
void Pixels(Obj element,const char* name,float value) {
    int unit=0; Ref length(api.New(api.Class(UiAssembly,UiSpace,"Length"),{&value,&unit}));
    Ref box(ReadStyle(element,name)); api.Call(box.get(),"set_value",{api.object_unbox(length.get())}); WriteStyle(element,name,box.get());
}
void Name(Obj element,const std::string& name) { Ref text(api.string_new(name.c_str())); api.Call(element,"set_name",{text.get()}); }
void Ignore(Obj element) { int ignore=1; bool no=false; api.Call(element,"set_pickingMode",{&ignore}); api.Call(element,"set_focusable",{&no}); }
Obj Element(bool label=false) { Ref e(api.New(label?methods.label:methods.element)); Ignore(e.get()); return e.get(); }
void SetText(Obj label,const std::string& text) { Ref s(api.string_new(text.c_str())); api.Call(label,"set_text",{s.get()}); }
void Place(Obj element,Rect rect) {
    Style(element,"position",1);
    Pixels(element,"left",rect.x); Pixels(element,"top",rect.y); Pixels(element,"width",rect.width); Pixels(element,"height",rect.height);
}
Obj MarkerRegistered(const View& v,const Marker& m) {
    return v.manager.get() && m.id.get() && v.group.get() ? api.Invoke(methods.find,v.manager.get(),{m.id.get(),v.group.get()}) : nullptr;
}
void RemoveMarker(View& v,Marker& m) {
    if (!m.native.get()) return;
    // A replaced dictionary entry belongs to its new owner. Detach only our
    // visual in that case; native begd would remove the replacement by key.
    if (MarkerRegistered(v,m) == m.native.get()) {
        // Leave the native position group first so an emptied group is dropped
        // from the manager and never retained for a later layer.
        api.Invoke(methods.uncluster,v.manager.get(),{m.native.get()});
        api.Invoke(methods.dispose,m.native.get());
    }
    else if (m.root.get()) api.Call(m.root.get(),"RemoveFromHierarchy");
    m.label.reset(); m.root.reset(); m.id.reset(); m.native.reset();
}
bool Cleanup(View& v) {
    v.cleaning=true;
    bool complete=true;
    for (auto& m:v.markers) {
        try { RemoveMarker(v,m); }
        catch(const std::exception& e) { complete=false; Report(e); }
    }
    if (!complete) return false;
    v.markers.clear();
    for (auto e:{&v.layer}) if (e->get()) {
        try { api.Call(e->get(),"RemoveFromHierarchy"); e->reset(); }
        catch(const std::exception& error) { Report(error); return false; }
    }
    if (v.group.get() && v.manager.get()) {
        try {
            Ref groups(api.Field(v.manager.get(),"dttw")), enabled(api.Field(v.manager.get(),"dttx"));
            Obj owned = StringItem(groups.get(),v.group.get());
            if (owned == v.groupItems.get() && owned && api.Value<int>(api.Call(owned,"get_Count")) == 0) {
                api.Call(enabled.get(),"Remove",{v.group.get()});
                api.Call(groups.get(),"Remove",{v.group.get()});
            }
        } catch(const std::exception& error) { Report(error); return false; }
    }
    v = View{}; return true;
}
bool SameView(const View& v,const Frame& f) {
    return !v.cleaning && v.compositor.get() == module->compositor.get() && v.service.get() == f.service.get() &&
        v.dictionary.get() == f.dictionary.get() && v.owner.get() == f.owner.get() && v.root.get() == f.root.get() &&
        v.world.get() == f.world.get() && v.worldId==f.worldId && v.manager.get() == f.manager.get() && v.panel.get() == f.panel.get() &&
        v.layer.get() && api.Call(v.layer.get(),"get_parent")==v.root.get();
}
void CreateView(View& v,const Frame& f) {
    v.compositor.reset(module->compositor.get()); v.service.reset(f.service.get()); v.dictionary.reset(f.dictionary.get());
    v.owner.reset(f.owner.get()); v.world.reset(f.world.get()); v.root.reset(f.root.get()); v.panel.reset(f.panel.get()); v.manager.reset(f.manager.get());
    v.worldId=f.worldId;
    const auto group = "dofus_miniatures_resources_"+std::to_string(++serial);
    v.group.reset(api.string_new(group.c_str()));
    Ref groups(api.Field(v.manager.get(),"dttw"));
    if (StringItem(groups.get(),v.group.get())) throw std::runtime_error("Resource map group collision");
    v.layer.reset(Element()); Name(v.layer.get(),group); Place(v.layer.get(),{0,0,0,0});
    api.Call(v.root.get(),"Add",{v.layer.get()});
}
Marker& NewMarker(View& v,const Frame& f,const Cell& cell,const Count& count) {
    const auto* resource=LookupResource(Catalog(),count.itemId);
    if(!resource)throw std::runtime_error("Unknown selected map resource");
    v.markers.emplace_back(); auto& m=v.markers.back(); m.x=cell.x; m.y=cell.y; m.item=count.itemId;
    m.id.reset(api.string_new(("cell_"+std::to_string(cell.x)+"_"+std::to_string(cell.y)+"_"+std::to_string(count.itemId)).c_str()));
    float x=float(cell.x),y=float(cell.y),zero=0; int layer=-1;
    auto point=api.Value<Vec2>(api.Invoke(methods.mapToPixel,nullptr,{&x,&y,f.world.get(),const_cast<double*>(&f.zoom)}));
    // Native icon marker: the item image comes from the game's item catalog.
    // Untinted, without the optional caption and reference id; the 15-parameter
    // constructor itself opts the marker into position groups.
    Ref address(ItemIconAddress(resource->iconId)),caption(api.string_new(""));
    Color tint{1,1,1,1}; uint32_t reference=0; bool shown=true;
    m.native.reset(api.object_new(methods.marker));
    api.Invoke(methods.markerCtor,m.native.get(),{m.id.get(),&point.x,&point.y,&zero,&zero,const_cast<double*>(&f.zoom),&layer,
        api.object_unbox(address.get()),v.group.get(),v.manager.get(),&tint,caption.get(),&reference,v.layer.get(),&shown});
    m.root.reset(api.Field(m.native.get(),"<dtwe>k__BackingField"));
    if (!m.root.get()) throw std::runtime_error("Resource map native marker root unavailable");
    Ignore(m.root.get());
    // Opt into the manager's position groups (the flag the game sets for its
    // own hints), keyed by map coordinates like native hints and flags.
    *static_cast<bool*>(api.FieldAddress(m.native.get(),"dtxh")) = true;
    Ref groups(api.Field(v.manager.get(),"dttw"));
    v.groupItems.reset(StringItem(groups.get(),v.group.get()));
    api.Invoke(methods.enableGroup,v.manager.get(),{v.group.get()});
    Vec2 key{x,y};
    api.Invoke(methods.cluster,v.manager.get(),{&key,m.native.get(),v.layer.get()});
    double iconScale=1;
    api.Invoke(methods.position,m.native.get(),{v.layer.get(),const_cast<double*>(&f.zoom),&iconScale});
    return m;
}
void UpdateLabel(Marker& marker,const Frame& f,const Count& count) {
    const auto quantity=std::to_string(count.count)+(count.interiorCount?"*":"");
    if(marker.content==quantity&&Same(marker.scale,f.scale))return;
    if(!marker.label.get()){marker.label.reset(Element(true));api.Call(marker.root.get(),"Add",{marker.label.get()});}
    // Bottom-right quantity badge inside the native marker, whatever its size.
    const float countWidth=(std::max)(18.0f,8.0f*float(quantity.size())+6.0f);
    Style(marker.label.get(),"position",1);
    Pixels(marker.label.get(),"right",0);Pixels(marker.label.get(),"bottom",0);
    Pixels(marker.label.get(),"width",countWidth/f.scale.x);Pixels(marker.label.get(),"height",18/f.scale.y);
    Pixels(marker.label.get(),"fontSize",14/f.scale.y);
    Style(marker.label.get(),"unityFontStyleAndWeight",1);
    Style(marker.label.get(),"color",Color{.09f,.10f,.08f,1});
    Style(marker.label.get(),"backgroundColor",Color{.98f,.87f,.55f,1});
    for(auto side:{"borderTopColor","borderBottomColor","borderLeftColor","borderRightColor"})
        Style(marker.label.get(),side,Color{.18f,.15f,.08f,1});
    for(auto side:{"borderTopWidth","borderBottomWidth","borderLeftWidth","borderRightWidth"})
        Style(marker.label.get(),side,1/f.scale.y);
    for(auto corner:{"borderTopLeftRadius","borderTopRightRadius","borderBottomLeftRadius","borderBottomRightRadius"})
        Pixels(marker.label.get(),corner,5/f.scale.y);
    for(auto edge:{"paddingLeft","paddingRight","paddingTop","paddingBottom"})Pixels(marker.label.get(),edge,0);
    Style(marker.label.get(),"unityTextAlign",4);
    SetText(marker.label.get(),quantity);
    marker.content=quantity;marker.scale=f.scale;
}
void Render(View& v,const Frame& f) {
    if(v.cached&&Same(v.viewport,f.viewport)&&Same(v.scale,f.scale)&&Same(v.origin,f.origin)&&v.zoom==f.zoom&&
            v.selection==module->settings.resourceMapItems) {
        Ref groups(api.Field(v.manager.get(),"dttw")),enabled(api.Field(v.manager.get(),"dttx"));
        auto existing=StringItem(groups.get(),v.group.get());
        if((v.markers.empty()&&!existing)||(existing==v.groupItems.get()&&existing&&
                api.Value<int>(api.Call(existing,"get_Count"))==int(v.markers.size())&&
                api.Value<bool>(api.Call(enabled.get(),"Contains",{v.group.get()}))))return;
    }
    v.cached=false;
    auto result=Query(Catalog(),Placements(),module->settings.resourceMapItems,f.worldId,f.bounds,CellBudget);
    const double cx=(double(f.bounds.minX)+f.bounds.maxX)/2,cy=(double(f.bounds.minY)+f.bounds.maxY)/2;
    std::stable_sort(result.cells.begin(),result.cells.end(),[&](const Cell& a,const Cell& b) {
        const auto distance=[&](const Cell& c){return (c.x-cx)*(c.x-cx)+(c.y-cy)*(c.y-cy);};
        return std::tuple(distance(a),a.x,a.y)<std::tuple(distance(b),b.x,b.y);
    });
    for(auto& m:v.markers)m.used=false;
    size_t markerCount=0;
    for(const auto& cell:result.cells) {
        if(cell.counts.empty())continue;
        if(markerCount+cell.counts.size()>MarkerBudget) {result.limited=true;continue;}
        float x=float(cell.x),y=float(cell.y);
        auto local=api.Value<Vec2>(api.Invoke(methods.mapToPixel,nullptr,{&x,&y,f.world.get(),const_cast<double*>(&f.zoom)}));
        auto screen=api.Value<Vec2>(api.Invoke(methods.toWorld,nullptr,{f.root.get(),&local}));
        const float size=f.key==0?MiniMarkerSize:FullMarkerSize;
        const Rect bounds{screen.x-size/2,screen.y-size/2,size,size};
        if(!Finite(bounds) || !Overlaps(bounds,f.viewport))continue;
        // Each cell keeps every selected resource. Overlap with another cell
        // must not delete minerals when the selection changes.
        if(markerCount%16==0&&!Current(f))throw std::runtime_error("Cartography changed during resource rendering");
        for(const auto& count:cell.counts) {
            auto found=std::find_if(v.markers.begin(),v.markers.end(),[&](const Marker& m){return m.x==cell.x && m.y==cell.y && m.item==count.itemId && m.native.get();});
            if(found!=v.markers.end() && MarkerRegistered(v,*found)!=found->native.get()) {RemoveMarker(v,*found);found=v.markers.end();}
            auto& marker=found==v.markers.end()?NewMarker(v,f,cell,count):*found;
            UpdateLabel(marker,f,count); marker.used=true;
            ++markerCount;
        }
    }
    for(auto& marker:v.markers)if(!marker.used)RemoveMarker(v,marker);
    std::erase_if(v.markers,[](const Marker& m){return !m.native.get();});
    if(!Current(f))throw std::runtime_error("Cartography changed before resource render commit");
    // Re-enable only our group if a native filter refresh cleared its active list.
    if(!v.markers.empty())api.Invoke(methods.enableGroup,v.manager.get(),{v.group.get()});
    v.cached=true;v.viewport=f.viewport;v.scale=f.scale;v.origin=f.origin;v.zoom=f.zoom;v.selection=module->settings.resourceMapItems;
}
void RequireField(Klass type,const char* name,size_t offset) {
    auto field=api.class_get_field_from_name(type,name);
    if(!field || api.field_get_offset(field)!=offset)throw std::runtime_error(std::string("Resource map field mismatch: ")+name);
}
}
void InstallRender() {
    if(installed)return;
    InstallIcons();
    methods.service=api.Class("Core.dll","","fhk"); methods.view=api.Class("Core.dll","","fhw");
    methods.marker=api.Class("Core.dll","","fgm"); auto manager=api.Class("Core.dll","","fga");
    methods.world=api.Class("Ankama.Dofus.Core.DataCenter.dll","Core.DataCenter.Metadata.World","WorldMapData");
    methods.element=api.Class(UiAssembly,UiSpace,"VisualElement"); methods.label=api.Class(UiAssembly,UiSpace,"Label");
    RequireField(methods.service,"dueo",0x98); RequireField(methods.view,"dugq",0x68);
    RequireField(methods.view,"dugv",0x90); RequireField(methods.view,"dugy",0xA8);
    RequireField(methods.view,"<dugi>k__BackingField",0x38); RequireField(methods.view,"<dugk>k__BackingField",0x48);
    RequireField(methods.view,"<dugf>k__BackingField",0x20); RequireField(methods.world,"id",0x10);
    auto baseMarker=api.Class("Core.dll","","fgh"); // fgm : fgg : fgh owns the root element field.
    RequireField(baseMarker,"<dtwe>k__BackingField",0x10); RequireField(methods.marker,"dtxh",0xC1);
    RequireField(manager,"dttw",0x10); RequireField(manager,"dttx",0x18); RequireField(manager,"dtty",0x20);
    methods.centerPlayer=api.FindVerified(methods.view,"beus",1,0x1BFA370);
    // Icon marker (fgm): native item image, group opt-in flag and overrides of
    // the base marker's positioning and disposal.
    methods.markerCtor=api.FindVerified(methods.marker,".ctor",15,0x1C40D70);
    methods.position=api.FindVerified(methods.marker,"beft",3,0x1C410D0);
    methods.dispose=api.FindVerified(methods.marker,"begd",0,0x1C411D0);
    methods.find=api.FindVerified(manager,"beec",2,0x1B9EAE0);
    methods.enableGroup=api.FindVerified(manager,"beem",1,0x1B9F530);
    // Position groups keyed by map coordinates: the same calls the game makes
    // for its hints; the view's own mouse-move handler spreads a hovered group.
    methods.cluster=api.FindVerified(manager,"bedx",3,0x1B9E370);
    methods.uncluster=api.FindVerified(manager,"bedy",1,0x1B9E5C0);
    auto conversion=api.Class("Core.dll","","fhl");
    methods.mapToPixel=api.FindVerified(conversion,"bepx",4,0x1C4EB30);
    methods.pixelToMap=api.FindVerified(conversion,"bepv",3,0x1C4E8D0);
    auto extensions=api.Class(UiAssembly,UiSpace,"VisualElementExtensions");
    methods.toWorld=api.FindVerified(extensions,"LocalToWorld",2,0x56766A0);
    methods.toLocal=api.FindVerified(extensions,"WorldToLocal",2,0x56776A0);
    api.FindVerified(methods.label,".ctor",0,0x5729760);
    struct Guard {const char* name;int argc;uintptr_t rva;};
    for(const auto& g:std::array<Guard,13>{{{".ctor",0,0x55F50B0},{"Add",1,0x55DFAD0},{"RemoveFromHierarchy",0,0x55E7BD0},
        {"get_contentContainer",0,0x3153690},{"get_panel",0,0x55F7170},{"get_parent",0,0x55F7180},{"get_worldBound",0,0x55F7CC0},{"get_worldClip",0,0x55F7EE0},
        {"get_visible",0,0x55F7BC0},{"get_resolvedStyle",0,0x55F72E0},{"set_name",1,0x55F9670},
        {"set_pickingMode",1,0x55F97B0},{"get_style",0,0x55F74A0}}}) api.FindVerified(methods.element,g.name,g.argc,g.rva);
    installed=true;
}
bool RenderCleanupPending() {
    for(const auto& v:views)if(v.owner.get() || v.layer.get() || !v.markers.empty())return true;
    return false;
}
void RestoreRender() { for(auto& v:views)Cleanup(v); nextPump=0; }
MiniMapSurface ReadMiniMapSurface() {
    MiniMapSurface surface;
    if(!Session())return surface;
    auto f=ReadFrame(0);
    if(!f.valid)return surface;
    // dugq is the zoomed/panned cartography content. Controls belong beside
    // it, outside its transform and mouse/wheel callback propagation path.
    Ref host(api.Call(f.root.get(),"get_parent"));
    if(!host.get()||!Visible(host.get())||api.Call(host.get(),"get_panel")!=f.panel.get()||
        api.Call(host.get(),"get_contentContainer")!=host.get())return surface;
    const auto viewport=Intersect(api.Value<Rect>(api.Call(host.get(),"get_worldBound")),
        api.Value<Rect>(api.Call(host.get(),"get_worldClip")));
    if(!Finite(viewport))return surface;
    Vec2 corner{viewport.x,viewport.y},end{viewport.x+viewport.width,viewport.y+viewport.height};
    const auto local=api.Value<Vec2>(api.Invoke(methods.toLocal,nullptr,{host.get(),&corner}));
    const auto last=api.Value<Vec2>(api.Invoke(methods.toLocal,nullptr,{host.get(),&end}));
    for(float value:{local.x,local.y,last.x,last.y})if(!std::isfinite(value))return surface;
    if(last.x<=local.x||last.y<=local.y||!Current(f)||api.Call(f.root.get(),"get_parent")!=host.get())return surface;
    surface.root.reset(host.get());surface.mapRoot.reset(f.root.get());
    surface.width=last.x-local.x;surface.height=last.y-local.y;
    surface.left=local.x;surface.top=local.y;
    return surface;
}
bool CenterMiniMap(const MiniMapSurface& expected) {
    if(!Session() || !expected.root.get() || !expected.mapRoot.get())return false;
    auto frame=ReadFrame(0);
    if(!frame.valid || frame.root.get()!=expected.mapRoot.get() ||
        api.Call(frame.root.get(),"get_parent")!=expected.root.get() || !Current(frame))return false;
    // DisplayWhenReady(moveToPlayer=true), also used by CartographyBannerUI
    // after a map change: the game's own instant centering, keeps zoom.
    bool moveToPlayer=true;
    api.Invoke(methods.centerPlayer,frame.owner.get(),{&moveToPlayer});
    views[0].cached=false;
    return true;
}
void PumpRender(uint64_t now) {
    if(!Session()||(!module->settings.resourceMapEnabled&&!module->settings.resourceMiniMapEnabled)||module->settings.resourceMapItems.empty()) {RestoreRender();return;}
    if(now<nextPump)return;
    nextPump=now+200;
    for(int key=0;key<2;++key) {
        auto& v=views[key];
        try {
            if(v.cleaning && !Cleanup(v))continue;
            const bool enabled=key==0?module->settings.resourceMiniMapEnabled:module->settings.resourceMapEnabled;
            if(!enabled){Cleanup(v);continue;}
            auto f=ReadFrame(key);
            if(!f.valid) {Cleanup(v);continue;}
            if(!SameView(v,f)) {if(!Cleanup(v))continue;CreateView(v,f);}
            Render(v,f);
        } catch(const std::exception& error) {Report(error);Cleanup(v);}
    }
}
}
