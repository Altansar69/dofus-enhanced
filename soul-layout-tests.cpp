#include "soul-layout.hpp"
#include "module.hpp"
#include "features.hpp"
#include "options-ui.hpp"
#include <array>
#include <cmath>
#include <functional>
#include <iostream>
#include <map>
#include <memory>
#include <unordered_map>

// Isolated managed fixture: no client, loader, process, settings or disk writes.
namespace fake {
using namespace managed;
struct Class { std::string name; };
struct MethodInfo { Class* klass; std::string name; int argc; };
struct Object {
    Class* klass = nullptr;
    std::map<std::string, Obj> fields;
    std::map<std::string, float> floats;
    std::map<std::string, bool> flags;
    std::string text;
    std::array<float, 4> value{};
    bool scalar = false;
    int integer = 0;
    std::map<std::string, int> integers;
    std::vector<Obj> items;
};
std::map<std::string, std::unique_ptr<Class>> classes;
std::vector<std::unique_ptr<MethodInfo>> methods;
std::vector<std::unique_ptr<Object>> objects;
std::unordered_map<GCHandle, Obj> handles;
GCHandle nextHandle = 0xF12345670000ULL;
Obj service = nullptr;
unsigned saves = 0, flushes = 0, resets = 0, originalClears = 0;
std::vector<std::string> logs;
std::string failure;
bool dropSave = false;
std::function<void()> onConstruct, onReset, onTupleRead;
unsigned sharedChanges=0;
using CloneFn = Obj(*)(Obj, Obj, Method);
CloneFn cloneHook=nullptr;
using ClearFn = void(*)(Obj, Method);
ClearFn clearHook = nullptr;
void Check(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
Object& Get(Obj value) { Check(value != nullptr, "Null fixture object"); return *static_cast<Object*>(value); }
Class* Type(const std::string& name) {
    auto& result = classes[name]; if (!result) result = std::make_unique<Class>(Class{name}); return result.get();
}
Obj Make(const std::string& name) {
    auto result = std::make_unique<Object>(); result->klass = Type(name); auto raw = result.get();
    objects.push_back(std::move(result)); return raw;
}
Obj Boolean(bool value) { auto box = Make("Bool"); Get(box).scalar = value; return box; }
Obj Integer(int value) { auto box=Make("Int"); Get(box).integer=value; return box; }
Obj List(std::initializer_list<Obj> rows={}) { auto list=Make("List");Get(list).items=rows;return list; }
Obj String(const char* value) { auto text = Make("String"); Get(text).text = value; return text; }
Obj Point(float x, float y) { auto point = Make("UICoordinates"); Get(point).floats = {{"x",x},{"y",y}}; return point; }
Obj Record(float x, float y) {
    auto record = Make("Tuple"); Get(record).fields = {{"Item1",String("DofusMiniatures.SoulStonePanel")},{"Item2",Point(x,y)}};
    Get(record).flags = {{"Item3",false},{"Item4",false}}; return record;
}
Obj Preset(Obj container) { return Get(container).fields["preset"]; }
Obj CurrentRecord(Obj container) { return Get(Preset(container)).fields["record"]; }
void OriginalClear(Obj container, Method) { ++originalClears; Get(Preset(container)).fields["record"] = nullptr; }
Obj OriginalClone(Obj source, Obj, Method) {
    auto clone=Make("PresetData"); Get(clone).integers["uid"]=-1;
    auto positions=List(); Get(clone).fields["uiPositionData"]=positions;
    for(auto row:Get(Get(source).fields.at("uiPositionData")).items) {
        Get(positions).items.push_back(row);
        if(Get(Get(row).fields["Item1"]).text=="DofusMiniatures.SoulStonePanel")Get(clone).fields["record"]=row;
    }
    return clone;
}
void Initialize() {
    cloneHook = OriginalClone; // Also lets this regression run against the previous bridge.
    api.object_get_class = [](Obj value) -> Klass { return Get(value).klass; };
    api.class_is_valuetype = [](Klass type) { return static_cast<Class*>(type)->name == "Tuple"; };
    api.object_unbox = [](Obj value) -> void* {
        if(Get(value).klass->name=="Bool")return &Get(value).scalar;
        if(Get(value).klass->name=="Int")return &Get(value).integer;
        if(Get(value).klass->name=="Tuple")return value;
        return Get(value).value.data();
    };
    api.string_new = String;
    api.gchandle_new = [](Obj value, bool) -> GCHandle { auto handle = ++nextHandle; handles[handle] = value; return handle; };
    api.gchandle_get_target = [](GCHandle handle) -> Obj { Check(handles.contains(handle), "Invalid GC handle"); return handles.at(handle); };
    api.gchandle_free = [](GCHandle handle) { Check(handles.erase(handle) == 1, "Double GC release"); };
}
}
namespace managed {
Api api;
Klass Api::Class(const char*, const char*, const char* name) const { return fake::Type(name); }
Method Api::Find(Klass klass, const char* name, int argc) const {
    auto result = std::make_unique<fake::MethodInfo>(fake::MethodInfo{static_cast<fake::Class*>(klass),name,argc});
    const auto raw = result.get(); fake::methods.push_back(std::move(result)); return raw;
}
Obj Api::Field(Obj owner, const char* name) const { return fake::Get(owner).fields[name]; }
void* Api::FieldAddress(Obj owner, const char* name) const {
    auto& value = fake::Get(owner);
    if (std::string(name) == "x" || std::string(name) == "y") return &value.floats[name];
    if (std::string(name) == "uid") return &value.integers[name];
    return &value.flags[name];
}
std::string Api::String(Obj value) const { return fake::Get(value).text; }
Obj Api::New(Klass klass, std::initializer_list<void*> values) const {
    fake::Check(static_cast<fake::Class*>(klass)->name == "UICoordinates" && values.size() == 2, "Coordinate ctor ABI");
    const std::vector<void*> args(values); auto result = fake::Point(*static_cast<float*>(args[0]), *static_cast<float*>(args[1]));
    if (fake::onConstruct) fake::onConstruct(); return result;
}
Obj Api::Call(Obj owner, const char* rawName, std::initializer_list<void*> values) const {
    const std::string name(rawName); const std::vector<void*> args(values); auto& object = fake::Get(owner);
    if (name == fake::failure) throw std::runtime_error("Injected native layout failure");
    if (name == "get_noOptionSave") return fake::Boolean(object.flags["noSave"]);
    if (name == "get_uiPresetOptionsContainer") return object.fields["container"];
    if (name == "get_property") return object.fields["property"];
    if (name == "get_Count") return fake::Integer(static_cast<int>(object.items.size()));
    if (name == "get_Item") return object.items.at(*static_cast<int*>(args[0]));
    if (name == "Add") { object.items.push_back(args[0]);return nullptr; }
    if (name == "RemoveAt") { object.items.erase(object.items.begin()+*static_cast<int*>(args[0]));return nullptr; }
    if (name == "set_Item") { object.items.at(*static_cast<int*>(args[0]))=args[1];if(object.flags["shared"])++fake::sharedChanges;return nullptr; }
    if (name == "IsLoaded") return fake::Boolean(object.flags["loaded"]);
    if (name == "get_currentContextualPreset") return object.fields["option"];
    if (name == "get_currentContextualPresetData") return object.fields["preset"];
    if (name == "get_panel") return object.flags["detached"] ? nullptr : owner;
    if (name == "get_worldBound") return owner;
    if (name == "LoadUiPositionById") {
        fake::Check(args.size() == 1 && fake::Get(args[0]).text == "DofusMiniatures.SoulStonePanel", "Native load key ABI");
        auto record = fake::CurrentRecord(owner);
        if (fake::onTupleRead) fake::onTupleRead();
        return record ? record : fake::Make("Tuple");
    }
    if (name == "SaveUiPositionById") {
        fake::Check(args.size() == 4 && fake::Get(args[0]).text == "DofusMiniatures.SoulStonePanel", "Native save key ABI");
        fake::Check(!*static_cast<bool*>(args[2]) && !*static_cast<bool*>(args[3]), "Custom layout uses left/top anchors");
        ++fake::saves;
        if (!fake::dropSave) fake::Get(fake::Preset(owner)).fields["record"] = fake::Record(fake::Get(args[1]).floats["x"],fake::Get(args[1]).floats["y"]);
        return nullptr;
    }
    if (name == "SetResetUiPosition") { ++fake::resets; if (fake::onReset) fake::onReset(); return nullptr; }
    if (name == "RequestSaveUi") { fake::Check(args.size() == 1 && !args[0], "Native save scheduling ignores null message"); ++fake::flushes; return nullptr; }
    throw std::runtime_error("Unexpected native layout method: " + name);
}
void* Api::Native(Method method, uintptr_t rva) const {
    const auto& value = *static_cast<const fake::MethodInfo*>(method);
    const std::map<uintptr_t,std::pair<std::string,int>> expected = {
        {0x7E0890,{"get_uiPresetOptionsContainer",0}},{0x752E10,{"get_noOptionSave",0}},{0x18B6B80,{"RequestSaveUi",1}},
        {0x20938A0,{"get_currentContextualPreset",0}},{0x20937C0,{"get_currentContextualPresetData",0}},
        {0x2091B60,{"LoadUiPositionById",1}},{0x2092DE0,{"SaveUiPositionById",4}},{0x2092E50,{"SetResetUiPosition",0}},
        {0x14F8840,{".ctor",2}}};
    const auto& target = expected.at(rva); fake::Check(value.name == target.first && value.argc == target.second, "Native guard ABI");
    return reinterpret_cast<void*>(rva);
}
}
Module* module = nullptr;
Module::Module(std::filesystem::path path) : folder(std::move(path)) {}
managed::Obj Module::FindService(const char* name) const { fake::Check(std::string(name) == "fjy", "Resolve exact cached data service"); return fake::service; }
void Log(const std::string& message) { fake::logs.push_back(message); }
void AddHook(managed::Method method, uintptr_t rva, void* replacement, void** original) {
    const auto& value = *static_cast<const fake::MethodInfo*>(method);
    if (rva==0x207F0C0) {
        fake::Check(value.name=="bmce" && value.argc==1,"Native disposition clone hook ABI");
        fake::cloneHook=reinterpret_cast<fake::CloneFn>(replacement);*original=reinterpret_cast<void*>(&fake::OriginalClone);
    } else {
        fake::Check(rva == 0x2091060 && value.name == "ClearAllSavedUi" && value.argc == 0, "Native reset hook ABI");
        fake::clearHook = reinterpret_cast<fake::ClearFn>(replacement); *original = reinterpret_cast<void*>(&fake::OriginalClear);
    }
}
namespace fake {
struct Fixture {
    Module owner{std::filesystem::path{}};
    Obj root, cached, container;
    Fixture() {
        soul::ResetLayout(); Check(handles.empty(), "Previous layout refs leaked");
        module = &owner; owner.ready = true; owner.servicesReady = true;
        owner.compositor.reset(Make("Compositor")); cached = service = Make("CachedDataService"); owner.cached.reset(cached);
        container = Make("UiPresetOptionsContainer"); Get(cached).fields["container"] = container;
        auto option = Make("AccountOption"); Get(option).flags["loaded"] = true;
        Get(container).fields = {{"currentPreset",option},{"option",option},{"preset",Make("PresetData")}};
        root = Make("Root"); Get(root).value = {0,0,1000,800};
        saves = flushes = resets = originalClears = 0; failure.clear(); logs.clear(); dropSave = false; onConstruct = {}; onReset = {}; onTupleRead = {}; sharedChanges=0;
        soul::InstallLayout();
    }
    ~Fixture() { soul::ResetLayout(); owner.cached.reset(); owner.compositor.reset(); module = nullptr; }
    soul::LayoutPosition Read() { return soul::ReadLayout(root); }
    void Apply(Obj preset) { Get(container).fields["preset"] = preset; }
};
struct FightFixture : Fixture {
    Obj assigned{}, other{}, presets{}, shared{}, id{}, fightOption{}, normal{};
    FightFixture() {
        normal=Preset(container);
        assigned=Make("PresetData");other=Make("PresetData");
        Get(assigned).integers["uid"]=3;Get(other).integers["uid"]=4;
        auto native=Record(88,77);Get(Get(native).fields["Item1"]).text="NativeWidget";
        Get(assigned).fields["uiPositionData"]=List({native});Get(assigned).integers["widgetSettings"]=19;
        Get(other).fields["uiPositionData"]=List();
        presets=List({assigned,other});Get(presets).flags["shared"]=true;
        shared=Make("SharedOption");Get(shared).flags["loaded"]=true;Get(shared).fields["property"]=presets;
        id=Make("AccountOption");Get(id).flags["loaded"]=true;Get(id).fields["property"]=Integer(3);
        Get(container).fields["savedPresets"]=shared;Get(container).fields["fightPresetId"]=id;
        fightOption=Make("AccountOption");Get(fightOption).flags["loaded"]=true;
        Get(container).fields["currentFightPreset"]=fightOption;
        Enter(assigned);
    }
    void Enter(Obj source) {
        auto clone=cloneHook(source,nullptr,nullptr);
        Get(container).fields["option"]=fightOption;Apply(clone);
    }
    Obj Rows() { return Get(assigned).fields["uiPositionData"]; }
};
void AssignedFightSurvivesNextCombat() {
    FightFixture f;auto initial=f.Read();auto native=Get(f.Rows()).items.front();
    Check(initial.x==24 && initial.y==180,"Assigned template initially lacks custom record");
    Get(Preset(f.container)).integers["widgetSettings"]=999;
    Check(soul::SaveLayoutPosition(f.root,320,240,initial.revision),"Drag saves current and assigned combat disposition");
    Check(Get(f.Rows()).items.size()==2 && Get(f.Rows()).items.front()==native && sharedChanges==1 && flushes==1,
        "Append only soul key and notify native shared store");
    Check(Get(f.assigned).integers["widgetSettings"]==19 && Get(Get(f.other).fields["uiPositionData"]).items.empty(),
        "Other widget settings and other named dispositions are preserved");
    auto stored=Get(f.Rows()).items.back();
    Check(std::abs(Get(Get(stored).fields["Item2"]).floats["x"]-32)<0.0001f &&
        std::abs(Get(Get(stored).fields["Item2"]).floats["y"]-30)<0.0001f,
        "Assigned disposition stores native percentages");
    Get(f.container).fields["option"]=Get(f.container).fields["currentPreset"];f.Apply(f.normal);f.Read();
    f.Enter(f.assigned);auto next=f.Read();
    Check(next.x==320 && next.y==240,"New combat cloning assigned disposition retains panel location");
    Check(soul::SaveLayoutPosition(f.root,420,280,next.revision) && Get(f.Rows()).items.size()==2 && sharedChanges==2,
        "Second drag replaces key without duplicate or whole-disposition save");
    soul::ResetLayout();f.Enter(f.assigned);Check(f.Read().x==420,"Module reload and subsequent combat restore saved key");
    clearHook(f.container,nullptr);Check(Get(f.Rows()).items.size()==1 && Get(f.Rows()).items.front()==native,
        "Reset removes only soul key from assigned template");
    f.Enter(f.assigned);Check(f.Read().x==24,"Reset cannot resurrect saved soul position next combat");
}
void AssignedFightOwnership() {
    { FightFixture f;soul::ResetLayout();f.Apply(nullptr);f.Enter(f.assigned);auto initial=f.Read();
      Check(soul::SaveLayoutPosition(f.root,300,200,initial.revision) && sharedChanges==1,
          "First combat tracks clone even when the contextual preset was previously absent"); }
    for(int reason=0;reason<7;++reason) {
        FightFixture f;
        if(reason==0)f.Enter(f.other); // Manual application in combat is not the assigned template.
        if(reason==1)Get(f.shared).flags["loaded"]=false;
        if(reason==2)Get(f.id).fields["property"]=Integer(4);
        if(reason==3)Get(f.presets).items.erase(Get(f.presets).items.begin());
        if(reason==4){soul::ResetLayout();} // No known clone origin: never guess by name or icon.
        if(reason==5){Get(f.container).fields["option"]=Get(f.container).fields["currentPreset"];f.Apply(f.normal);}
        if(reason==6)Get(f.id).flags["loaded"]=false;
        auto layout=f.Read();
        Check(soul::SaveLayoutPosition(f.root,300,200,layout.revision),"Ordinary current-context save remains available");
        Check(sharedChanges==0 && Get(f.Rows()).items.size()==1,"Unrelated/unready/deleted/unknown origin never writes assigned template");
    }
    { FightFixture f;auto initial=f.Read();onConstruct=[&]{f.Enter(f.other);};
      Check(!soul::SaveLayoutPosition(f.root,300,200,initial.revision) && !sharedChanges,"Reentrant preset switch rejects old drag before write"); }
    { FightFixture f;auto initial=f.Read();unsigned reads=0;
      onTupleRead=[&]{if(++reads==3)Get(f.presets).items.erase(Get(f.presets).items.begin());};
      Check(soul::SaveLayoutPosition(f.root,300,200,initial.revision) && !sharedChanges && Get(f.Rows()).items.size()==1,
          "Template removed between active save and mirror remains untouched"); }
}
void SaveApplyAndHidden() {
    Fixture f; const auto initial = f.Read(); Check(initial.writable && initial.x == 24 && initial.y == 180, "Missing record uses default");
    Check(saves == 0 && flushes == 0, "Read/clamp must not write native or local settings");
    Check(soul::SaveLayoutPosition(f.root,200,400,initial.revision), "Drag should save native layout");
    auto a = Preset(f.container), record = CurrentRecord(f.container), point = Get(record).fields["Item2"];
    Check(Get(point).floats["x"] == 20 && Get(point).floats["y"] == 50, "Native positions are percentages");
    Check(saves == 1 && flushes == 1 && resets == 1, "Successful native write schedules one native save");
    const auto saved = f.Read(); Check(saved.x == 200 && saved.y == 400, "Native position round trip");
    auto b = Make("PresetData"); f.Apply(b); const auto missing = f.Read();
    Check(missing.x == 24 && missing.y == 180 && missing.revision != saved.revision, "Applying missing profile must not inherit previous position");
    // Native SavePreset copies the collection even if our window is absent.
    auto copy = Make("PresetData"); Get(copy).fields["record"] = Get(a).fields["record"];
    f.Apply(copy); const auto restored = f.Read(); Check(restored.x == 200 && restored.y == 400, "Saved hidden record must restore");
    soul::ResetLayout(); Check(CurrentRecord(f.container) == record, "Module cleanup must not remove native saved record");
    Check(f.Read().x == 200 && saves == 1, "Reopen must restore without writing");
}
void StaleDragAndReset() {
    Fixture f; auto first = f.Read(); auto sameCoordinates = Make("PresetData"); f.Apply(sameCoordinates);
    Check(!soul::SaveLayoutPosition(f.root,250,250,first.revision) && saves == 0, "Apply identical coordinates cancels old drag via identity");
    auto empty = f.Read(); clearHook(f.container,nullptr);
    Check(!soul::SaveLayoutPosition(f.root,250,250,empty.revision) && saves == 0, "Reset empty collection must cancel first drag");
    auto before = f.Read(); Check(soul::SaveLayoutPosition(f.root,200,200,before.revision), "New drag should work after reset");
    const auto after = f.Read(); clearHook(f.container,nullptr); const auto cleared = f.Read();
    Check(cleared.x == 24 && cleared.y == 180 && cleared.revision != after.revision, "Native reset restores fixed default");
    auto foreign = Make("UiPresetOptionsContainer"); Get(foreign).fields["preset"] = Make("PresetData");
    clearHook(foreign,nullptr); Check(f.Read().revision == cleared.revision && originalClears == 3, "Foreign resets retain original and do not invalidate our drag");
}
void RootResizeAndReadiness() {
    Fixture f; auto initial = f.Read(); Get(f.root).value[2] = 500;
    Check(!soul::SaveLayoutPosition(f.root,200,200,initial.revision), "Resize cancels stale drag");
    auto current = f.Read(); auto otherRoot = Make("Root"); Get(otherRoot).value = {0,0,500,800};
    Check(!soul::SaveLayoutPosition(otherRoot,200,200,current.revision), "Root replacement cancels stale drag");
    const auto changedRoot = soul::ReadLayout(otherRoot); service = Make("NewCached");
    Check(!soul::SaveLayoutPosition(otherRoot,200,200,changedRoot.revision) && saves == 0, "Stale account/service must not write");
    service = f.cached; Get(f.cached).flags["noSave"] = true; Check(!f.Read().writable, "Native no-save state respected");
    Get(f.cached).flags["noSave"] = false; auto option = Get(f.container).fields["option"]; Get(option).flags["loaded"] = false;
    Check(!f.Read().writable, "Unloaded account options are not writable"); Get(option).flags["loaded"] = true;
    f.owner.stopRequested = true; Check(!f.Read().writable, "Stopping module is not writable");
}
void ContextAndInPlaceChanges() {
    Fixture f; const auto normal = f.Read();
    auto fight = Make("AccountOption"); Get(fight).flags["loaded"] = true;
    Get(f.container).fields["option"] = fight;
    const auto inFight = f.Read();
    Check(inFight.revision != normal.revision, "Contextual fight option changes invalidate drag even with same preset object");
    Check(!soul::SaveLayoutPosition(f.root,100,100,normal.revision) && saves == 0, "Normal context drag cannot write fight disposition");
    Get(Preset(f.container)).fields["record"] = Record(10,20);
    const auto external = f.Read(); Check(external.x == 100 && external.y == 160 && external.revision != inFight.revision, "In-place native key replacement reloads position");
    Get(Preset(f.container)).fields["record"] = nullptr;
    const auto cleared = f.Read(); Check(cleared.x == 24 && cleared.revision != external.revision, "Per-key native deletion restores default");
}
void FailureAndReentrancy() {
    Fixture f; auto initial = f.Read(); dropSave = true;
    Check(!soul::SaveLayoutPosition(f.root,300,400,initial.revision) && flushes == 0, "Silently rejected native write must not schedule save");
    dropSave = false; const auto retry = f.Read(); Check(soul::SaveLayoutPosition(f.root,300,400,retry.revision), "Transient native save failure recovers");
    const auto current = f.Read(); onConstruct = [&] { f.Apply(Make("PresetData")); };
    Check(!soul::SaveLayoutPosition(f.root,200,200,current.revision) && saves == 2, "Reentrant profile switch before commit rejects write");
    onConstruct = {}; auto changed = f.Read(); onReset = [&] { service = Make("OtherAccountCached"); };
    Check(!soul::SaveLayoutPosition(f.root,200,200,changed.revision) && flushes == 1, "Service replacement before schedule must not flush stale account");
    service = f.cached; onReset = {}; failure = "LoadUiPositionById"; Check(!f.Read().writable, "Native read failure degrades safely");
    const auto logCount = logs.size(); f.Read(); Check(logs.size() == logCount, "Repeated errors log once");
}
void MalformedAndScaled() {
    Fixture f; Get(Preset(f.container)).fields["record"] = Record(25,50);
    const auto first = f.Read(); Check(first.x == 250 && first.y == 400, "Native percentage conversion");
    Get(f.root).value = {0,0,2000,1600}; const auto larger = f.Read(); Check(larger.x == 500 && larger.y == 800, "Disposition adapts to native root dimensions");
    Get(CurrentRecord(f.container)).flags["Item3"] = true; Check(!f.Read().writable, "Unknown anchor must not be silently rewritten");
    Get(CurrentRecord(f.container)).flags["Item3"] = false; auto point = Get(CurrentRecord(f.container)).fields["Item2"];
    Get(point).floats["x"] = std::numeric_limits<float>::quiet_NaN(); Check(!f.Read().writable, "Nonfinite saved coordinate rejected");
}
}
int main() {
    try {
        fake::Initialize();
        for (auto test : {fake::SaveApplyAndHidden,fake::StaleDragAndReset,fake::RootResizeAndReadiness,fake::ContextAndInPlaceChanges,fake::FailureAndReentrancy,fake::MalformedAndScaled,fake::AssignedFightSurvivesNextCombat,fake::AssignedFightOwnership}) {
            test(); fake::Check(fake::handles.empty(), "Native layout GC references leaked");
        }
        std::cout << "PASS: native disposition persistence bridge, percentage coordinates, apply/reset identity, hidden entries, stale drag/account/root rejection, failure recovery\n";
        return 0;
    } catch (const std::exception& e) { std::cerr << "FAIL: " << e.what() << '\n'; return 1; }
}
