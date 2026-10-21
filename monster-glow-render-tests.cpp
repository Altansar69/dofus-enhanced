#include "monster-glow-render.hpp"
#include <array>
#include <cmath>
#include <cstring>
#include <iostream>
#include <map>
#include <memory>
#include <set>
#include <unordered_map>

// Offline managed-API fixture. GPU shader output is validated separately with
// the installed shader bytecode; these tests exercise production orchestration,
// projection, bounded allocations, source ownership and failure restoration.
namespace fake {
using namespace managed;
using monsterGlow::ScreenRect;
using monsterGlow::GlowCoverage;
struct Matrix { float v[16]{}; };
struct Vec { float x, y, z, w; };
struct Identifier { alignas(8) unsigned char data[40]{}; };
struct Class { std::string name; };
struct MethodInfo { Class* klass; std::string name; int argc; uintptr_t rva; };
struct Properties {
    std::map<std::string, float> floats;
    std::map<std::string, int> integers;
    std::map<std::string, Vec> vectors;
    std::map<std::string, Obj> textures;
    std::map<std::string, Matrix> transforms;
};
struct Command {
    Properties parameters;
    std::string name;
    std::string property;
    Obj first = nullptr, second = nullptr;
    int submesh = 0, pass = 0;
    bool clearDepth = false;
    Matrix view{}, projection{};
    ScreenRect rectangle{};
};
struct Object : Properties {
    Class* klass = nullptr;
    alignas(8) std::array<unsigned char, 128> box{};
    bool owned = false, destroyed = false, released = false;
    int width = 0, height = 0, depth = 0, queue = 3000;
    std::string text, shader;
    std::vector<Obj> items;
    std::vector<Command> commands;
    Matrix view{}, projection{};
    ScreenRect viewport{0, 0, 800, 600};
    std::vector<unsigned char> raw;
    uint8_t pixelRed = 0, pixelAlpha = 0;
};
std::map<std::string, std::unique_ptr<Class>> classes;
std::vector<std::unique_ptr<MethodInfo>> methods;
std::vector<std::unique_ptr<Object>> objects;
std::unordered_map<Obj, Object*> lookup;
std::unordered_map<GCHandle, Obj> handles;
std::set<Obj> nativeOwned;
std::vector<std::vector<Command>> executions;
std::vector<std::array<int, 3>> allocations;
Obj active = nullptr, fullscreen = nullptr, whiteTexture = nullptr;
std::map<std::string, Matrix> globals;
GCHandle handleCounter = 0xFF1234500000ULL;
std::string failName, missingShader;
unsigned failCountdown = 0, guards = 0, destroys = 0;
unsigned readbacks = 0;
bool malformedPixels = false;
bool failAfterShaderMatrices = false;
bool failAfterCoverageMask = false;
std::vector<std::string> logs;
void Check(bool condition, const char* message) { if (!condition) throw std::runtime_error(message); }
bool Near(float a, float b) { return std::fabs(a-b) < 0.0001f; }
Matrix Identity() { Matrix m; m.v[0] = m.v[5] = m.v[10] = m.v[15] = 1; return m; }
Class* Type(const std::string& name) {
    auto& value = classes[name];
    if (!value) value = std::make_unique<Class>(Class{name});
    return value.get();
}
Object& Get(Obj object) {
    const auto it = lookup.find(object);
    if (it == lookup.end()) throw std::runtime_error("Unknown fake glow object");
    Check(!it->second->destroyed, "Use of destroyed glow object");
    return *it->second;
}
Obj Make(const std::string& name, bool owned = false) {
    auto value = std::make_unique<Object>(); value->klass = Type(name); value->owned = owned;
    value->view = value->projection = Identity();
    Obj result = value.get(); lookup[result] = value.get(); objects.push_back(std::move(value));
    if (owned) nativeOwned.insert(result);
    return result;
}
template<class T> Obj Box(T value) {
    auto result = Make("Box"); static_assert(sizeof(T) <= 128);
    std::memcpy(Get(result).box.data(), &value, sizeof(value)); return result;
}
void Initialize() {
    api.object_new = [](Klass klass) -> Obj {
        const auto name = static_cast<Class*>(klass)->name;
        return Make(name, name == "Material" || name == "RenderTexture" || name == "CommandBuffer" || name == "Texture2D");
    };
    api.string_new = [](const char* value) -> Obj { auto object = Make("String"); Get(object).text = value; return object; };
    api.object_unbox = [](Obj object) -> void* { return Get(object).box.data(); };
    api.object_get_class = [](Obj object) -> Klass { return Get(object).klass; };
    api.class_get_name = [](Klass klass) -> const char* { return static_cast<Class*>(klass)->name.c_str(); };
    api.method_get_param = [](Method method, uint32_t index) -> const void* {
        const auto& m = *static_cast<const MethodInfo*>(method);
        Check(m.name == "GetSharedMaterials" && index == 0, "Wrong list parameter inference");
        return Type("List<Material>");
    };
    api.class_from_type = [](const void* type) -> Klass { return const_cast<void*>(type); };
    api.gchandle_new = [](Obj object, bool) -> GCHandle {
        auto result = ++handleCounter; handles[result] = object; return result;
    };
    api.gchandle_get_target = [](GCHandle handle) -> Obj {
        Check(handles.contains(handle), "Invalid or truncated glow handle"); return handles.at(handle);
    };
    api.gchandle_free = [](GCHandle handle) { Check(handles.erase(handle) == 1, "Double free glow handle"); };
    fullscreen = Make("Mesh");
    whiteTexture = Make("Texture2D");
}
struct Fixture {
    Obj camera, renderer, material, previous;
    Fixture() {
        failName.clear(); failCountdown = 0; monsterGlow::ResetGlowRender();
        Check(handles.empty() && nativeOwned.empty(), "Previous fixture retained glow resources");
        executions.clear(); allocations.clear(); missingShader.clear();
        failAfterCoverageMask = false;
        camera = Make("Camera"); renderer = Make("MeshRenderer"); material = Make("Material");
        Get(material).shader = "Custom/DofusMainShader"; Get(renderer).items = {material};
        previous = Make("RenderTexture"); active = previous;
        globals.clear();
        for (const char* name : {"unity_MatrixV", "glstate_matrix_projection", "unity_MatrixVP"}) {
            auto matrix = Identity(); matrix.v[12] = 17; matrix.v[13] = 29;
            globals[name] = matrix;
        }
    }
    ~Fixture() {
        failName.clear(); monsterGlow::ResetGlowRender();
        Check(nativeOwned.empty(), "Owned native glow resources leaked");
        Check(handles.empty(), "Glow managed references leaked");
    }
    Obj Render(size_t slot = 0, ScreenRect rectangle = {200, 150, 400, 300}, const GlowCoverage* coverage = nullptr) {
        return monsterGlow::RenderGlow(slot, camera, {renderer}, rectangle, GetTickCount64(), coverage);
    }
};
}

void Log(const std::string& text) { fake::logs.push_back(text); }

namespace managed {
Api api{};
Klass Api::Class(const char*, const char*, const char* name) const { return fake::Type(name); }
Method Api::FindVerified(Klass klass, const char* name, int argc, uintptr_t rva) const {
    auto method = std::make_unique<fake::MethodInfo>(fake::MethodInfo{static_cast<fake::Class*>(klass), name, argc, rva});
    auto result = method.get(); fake::methods.push_back(std::move(method)); ++fake::guards; return result;
}
Obj Api::New(Klass klass, std::initializer_list<void*> args) const {
    fake::Check(static_cast<fake::Class*>(klass)->name == "List<Material>" && args.size() == 0, "Unexpected generic allocation");
    return fake::Make("List<Material>");
}
Obj Api::Call(Obj self, const char* method, std::initializer_list<void*> args) const {
    auto& object = fake::Get(self);
    fake::Check(object.klass->name == "List<Material>", "Only owned material list may use dynamic calls");
    if (std::string(method) == "get_Count") return fake::Box(static_cast<int>(object.items.size()));
    if (std::string(method) == "get_Item") return object.items.at(*static_cast<int*>(*args.begin()));
    throw std::runtime_error("Unexpected glow dynamic call");
}
Obj Api::Invoke(Method method, Obj self, std::initializer_list<void*> arguments) const {
    const auto& m = *static_cast<const fake::MethodInfo*>(method);
    std::vector<void*> args(arguments);
    fake::Check(static_cast<int>(args.size()) == m.argc, "Wrong managed glow arity");
    if (m.name == fake::failName && fake::failCountdown && --fake::failCountdown == 0) {
        fake::failName.clear(); throw std::runtime_error("Injected offline rendering failure");
    }
    const auto& type = m.klass->name;
    if (type == "Shader") {
        if (m.name == "GetGlobalMatrix") {
            fake::Check(self == nullptr && m.rva == 0x53F6BE0, "Wrong global matrix getter");
            return fake::Box(fake::globals.at(fake::Get(args[0]).text));
        }
        fake::Check(self == nullptr && m.rva == 0x53F5EF0, "Wrong Shader.Find ABI");
        const auto name = fake::Get(args.at(0)).text;
        if (name == fake::missingShader) return nullptr;
        auto shader = fake::Make("Shader"); fake::Get(shader).text = name; return shader;
    }
    if (type == "GL") {
        fake::Check(self == nullptr && m.rva == 0x53CFA60 && *static_cast<bool*>(args[1]), "Expected GPU texture projection");
        auto matrix = *static_cast<fake::Matrix*>(args[0]);
        for (int c = 0; c < 4; ++c) {
            matrix.v[c*4+1] = -matrix.v[c*4+1];
            matrix.v[c*4+2] = .5f * (matrix.v[c*4+3] - matrix.v[c*4+2]);
        }
        return fake::Box(matrix);
    }
    if (type == "RenderingUtils") {
        fake::Check(self == nullptr && m.rva == 0x5210E80, "Wrong shared fullscreen method");
        return fake::fullscreen;
    }
    if (type == "RenderTargetIdentifier") {
        fake::Identifier value;
        if (m.rva == 0x5474090) std::memcpy(value.data, &args[0], sizeof(Obj));
        else { fake::Check(m.rva == 0x5473FF0 && *static_cast<int*>(args[0]) == 2, "Wrong built-in target"); }
        return fake::Box(value);
    }
    if (type == "Object") {
        fake::Check(m.rva == 0x5435EA0 && self == nullptr, "Wrong Unity destroy ABI");
        auto& target = fake::Get(args[0]);
        fake::Check(target.owned && fake::nativeOwned.erase(args[0]) == 1, "Destroyed shared/source Unity object");
        target.destroyed = true; ++fake::destroys; return nullptr;
    }
    if (type == "Graphics") {
        fake::Check(m.rva == 0x53D4200 && self == nullptr, "Wrong execute ABI");
        auto commands = fake::Get(args[0]).commands;
        fake::executions.push_back(commands);
        Obj target = fake::active;
        for (const auto& command : commands) {
            if (command.name == "global-matrix") fake::globals[command.property] = command.projection;
            if (fake::failAfterShaderMatrices && command.name == "source") {
                fake::failAfterShaderMatrices = false;
                throw std::runtime_error("Injected execution failure after globals changed");
            }
            if (command.name == "target") { target = command.first; fake::active = target; }
            if (command.name == "source") { fake::Get(target).pixelRed = 123; fake::Get(target).pixelAlpha = 250; }
            if (command.name == "copy") {
                auto& to = fake::Get(command.second); const auto& from = fake::Get(command.first);
                fake::Check(from.owned && to.owned && from.width == to.width && from.height == to.height && from.depth == to.depth,
                    "Diagnostic copy must use private matching targets");
                to.pixelRed = from.pixelRed; to.pixelAlpha = from.pixelAlpha;
            }
            if (command.name == "mesh" || command.name == "procedural") {
                if (fake::failAfterCoverageMask && command.name == "mesh" &&
                    command.parameters.floats.contains("_ColorInvert") && command.parameters.floats.at("_ColorInvert") == 1) {
                    fake::failAfterCoverageMask = false;
                    throw std::runtime_error("Injected execution failure during coverage mask");
                }
                const auto& material = fake::Get(command.second);
                for (const auto& entry : command.parameters.textures)
                    fake::Check(entry.second != target, "Shader reads its own render target");
                fake::Get(target).pixelRed = command.name == "procedural" ? 25 : material.shader == "Hidden/UIR/ColorEffect" ? 255 : 50;
                fake::Get(target).pixelAlpha = command.name == "procedural" ? 25 : 0;
                if(material.shader=="Hidden/UIR/ColorEffect" && command.parameters.transforms.at("_ColorMatrix").v[15]>0)
                    fake::Get(target).pixelRed=fake::Get(target).pixelAlpha=150;
            }
        }
        return nullptr;
    }
    if (type == "RenderTexture" && m.name == "get_active") return fake::active;
    if (type == "RenderTexture" && m.name == "set_active") { fake::active = args[0]; return nullptr; }
    if (type == "Texture2D" && m.name == "get_whiteTexture") {
        fake::Check(self == nullptr && m.argc == 0 && m.rva == 0x5412100,
            "HUD coverage must read the verified shared white texture getter");
        return fake::whiteTexture;
    }
    auto& object = fake::Get(self);
    if (type == "Texture2D") {
        fake::Check(object.owned,"Readback must use owned CPU texture");
        if (m.name == ".ctor") {
            fake::Check(m.rva == 0x5410E20 && *static_cast<int*>(args[2]) == 4 &&
                !*static_cast<bool*>(args[3]) && *static_cast<bool*>(args[4]),"Incorrect readback format/overload");
            object.width = *static_cast<int*>(args[0]); object.height = *static_cast<int*>(args[1]); return nullptr;
        }
        if (m.name == "ReadPixels") {
            fake::Check(m.rva == 0x540EAF0 && fake::active && fake::Get(fake::active).owned &&
                fake::Get(fake::active).klass->name == "RenderTexture","Must never read the screen/camera target");
            const auto rect = *static_cast<fake::ScreenRect*>(args[0]);
            fake::Check(rect.width == object.width && rect.height == object.height && !*static_cast<bool*>(args[3]),"Readback bounds/mipmaps");
            ++fake::readbacks; object.pixelRed = fake::Get(fake::active).pixelRed; object.pixelAlpha = fake::Get(fake::active).pixelAlpha;
            return nullptr;
        }
        if (m.name == "GetPixels32") {
            fake::Check(m.rva == 0x540D5E0,"GetPixels32 overload");
            const auto result = fake::Make("Color32[]"); auto& array = fake::Get(result);
            const size_t size = static_cast<size_t>(object.width)*object.height;
            array.raw.resize(32+4*size);
            const size_t length = fake::malformedPixels ? size+1 : size;
            std::memcpy(array.raw.data()+24,&length,8);
            array.raw[32] = object.pixelRed; array.raw[35] = object.pixelAlpha;
            fake::lookup[array.raw.data()] = &array; return array.raw.data();
        }
    }
    if (type == "Camera") {
        if (m.name == "get_pixelRect") return fake::Box(object.viewport);
        if (m.name == "get_worldToCameraMatrix") return fake::Box(object.view);
        if (m.name == "get_projectionMatrix") return fake::Box(object.projection);
    }
    if (type == "MaterialPropertyBlock") {
        if (m.name == ".ctor") { fake::Check(m.rva==0x53D9F90,"Wrong property-block constructor"); return nullptr; }
        const auto name=fake::Get(args[0]).text;
        if (m.name=="SetFloat") object.floats[name]=*static_cast<float*>(args[1]);
        else if (m.name=="SetVector") object.vectors[name]=*static_cast<fake::Vec*>(args[1]);
        else if (m.name=="SetTexture") object.textures[name]=args[1];
        else if (m.name=="SetMatrix") object.transforms[name]=*static_cast<fake::Matrix*>(args[1]);
        else throw std::runtime_error("Unexpected property-block mutation");
        return nullptr;
    }
    if (type == "Material") {
        if (m.name == "get_renderQueue") return fake::Box(object.queue);
        fake::Check(object.owned, "Source material was modified");
        if (m.name == ".ctor") {
            const auto& source = fake::Get(args[0]);
            if (m.rva == 0x53E49B0) { fake::Check(source.klass->name == "Shader", "Shader ctor overload wrong"); object.shader = source.text; }
            else {
                fake::Check(m.rva == 0x53E4B30 && source.klass->name == "Material", "Material copy overload wrong");
                object.shader = source.shader; object.floats = source.floats; object.integers = source.integers;
                object.vectors = source.vectors; object.textures = source.textures;
            }
            return nullptr;
        }
        const auto name = fake::Get(args[0]).text;
        if (m.name == "SetFloat") object.floats[name] = *static_cast<float*>(args[1]);
        else if (m.name == "SetInteger") object.integers[name] = *static_cast<int*>(args[1]);
        else if (m.name == "SetVector") object.vectors[name] = *static_cast<fake::Vec*>(args[1]);
        else if (m.name == "SetTexture") object.textures[name] = args[1];
        else throw std::runtime_error("Unexpected material mutation");
        return nullptr;
    }
    if (type == "Texture") {
        fake::Check(object.owned && *static_cast<int*>(args[0]) == 1, "Wrong owned texture sampler"); return nullptr;
    }
    if (type == "RenderTexture") {
        if (m.name == ".ctor") {
            fake::Check(m.rva == 0x54078E0 && *static_cast<int*>(args[3]) == 0 && *static_cast<int*>(args[4]) == 1,
                "Wrong render texture format/constructor");
            object.width = *static_cast<int*>(args[0]); object.height = *static_cast<int*>(args[1]); object.depth = *static_cast<int*>(args[2]);
            fake::allocations.push_back({object.width, object.height, object.depth}); return nullptr;
        }
        if (m.name == "Create") return fake::Box(true);
        if (m.name == "Release") { object.released = true; return nullptr; }
    }
    if (type == "Renderer") {
        fake::Check(m.name == "GetSharedMaterials" && m.rva == 0x53F1AD0, "Wrong shared materials accessor");
        fake::Get(args[0]).items = object.items; return nullptr;
    }
    if (type == "CommandBuffer") {
        if (m.name == ".ctor") return nullptr;
        if (m.name == "Release") {
            fake::Check(fake::nativeOwned.erase(self) == 1, "Duplicate command release"); object.released = true; return nullptr;
        }
        if (m.name == "Clear") { object.commands.clear(); return nullptr; }
        fake::Command command;
        if (m.name == "SetRenderTarget") {
            command.name = "target"; std::memcpy(&command.first, args[0], sizeof(Obj));
        } else if (m.name == "CopyTexture") {
            fake::Check(m.rva == 0x5450CA0,"Diagnostic CopyTexture overload");
            command.name = "copy";
            std::memcpy(&command.first,args[0],sizeof(Obj)); std::memcpy(&command.second,args[1],sizeof(Obj));
        } else if (m.name == "ClearRenderTarget") {
            command.name = "clear";
            command.clearDepth = *static_cast<bool*>(args[0]);
            const auto color = *static_cast<fake::Vec*>(args[2]);
            fake::Check(*static_cast<bool*>(args[1]) && color.x == 0 && color.y == 0 && color.z == 0 && color.w == 0,
                "Render target must clear transparent");
        } else if (m.name == "SetViewport") {
            command.name = "viewport"; command.rectangle = *static_cast<monsterGlow::ScreenRect*>(args[0]);
        } else if (m.name == "SetViewProjectionMatrices") {
            command.name = "matrices"; command.view = *static_cast<fake::Matrix*>(args[0]); command.projection = *static_cast<fake::Matrix*>(args[1]);
        } else if (m.name == "SetGlobalMatrix") {
            fake::Check(m.rva == 0x5462620, "Wrong command-buffer matrix overload");
            command.name = "global-matrix"; command.property = fake::Get(args[0]).text;
            command.projection = *static_cast<fake::Matrix*>(args[1]);
        } else if (m.name == "DrawRenderer") {
            command.name = "source"; command.first = args[0]; command.second = args[1];
            command.submesh = *static_cast<int*>(args[2]); command.pass = *static_cast<int*>(args[3]);
            fake::Check(!fake::Get(command.second).owned && command.pass == 0, "Source capture must use original material/pass0");
        } else if (m.name == "DrawMesh") {
            fake::Check(args.size()==6 && m.rva==0x54532D0 && args[5], "Fullscreen draw requires an explicit property block");
            command.parameters=static_cast<const fake::Properties&>(fake::Get(args[5]));
            command.name = "mesh"; command.first = args[0]; command.second = args[2];
            command.view = *static_cast<fake::Matrix*>(args[1]);
            command.submesh = *static_cast<int*>(args[3]); command.pass = *static_cast<int*>(args[4]);
            fake::Check(command.first == fake::fullscreen && command.submesh == 0, "Wrong mask/fullscreen geometry");
        } else if (m.name == "DrawProcedural") {
            fake::Check(args.size()==7 && m.rva==0x5454D30 && *static_cast<int*>(args[5])==1 && args[6], "Knockout draw requires an explicit property block");
            command.parameters=static_cast<const fake::Properties&>(fake::Get(args[6]));
            command.name = "procedural"; command.second = args[1]; command.pass = *static_cast<int*>(args[2]);
            command.view = *static_cast<fake::Matrix*>(args[0]);
            fake::Check(m.rva == 0x5454D30 && *static_cast<int*>(args[3]) == 0 && *static_cast<int*>(args[4]) == 3,
                "Knockout requires 3-vertex fullscreen triangle");
        } else throw std::runtime_error("Unexpected command buffer call");
        object.commands.push_back(command); return nullptr;
    }
    throw std::runtime_error("Unexpected glow call " + type + "." + m.name);
}
}

namespace {
using namespace fake;
void BoundedPrivatePixelDiagnostics() {
    Fixture f;
    auto render = [&](uint64_t time, size_t slot=0) {
        return monsterGlow::RenderGlow(slot,f.camera,{f.renderer},{0,0,32,32},time);
    };
    Check(render(1000) && readbacks == 5 && active == f.previous,"Private probe failed/restoration");
    Check(logs.back().find("source={a:1/1024,maxA:250,r:1,maxR:123}") != std::string::npos &&
        logs.back().find("mask={a:0/1024,maxA:0,r:1,maxR:255}") != std::string::npos &&
        logs.back().find("output={a:1/1024,maxA:25,r:1,maxR:25}") != std::string::npos,"Stage counters incorrect");
    Check(logs.back().find("presented={a:1/1024,maxA:150,r:1,maxR:150}") != std::string::npos,"Presentation diagnostics must measure the actual UI texture");
    Check(render(10999) && render(11000,1) && readbacks == 5,"Probe must throttle and only sample slot0");
    failName = "ReadPixels"; failCountdown = 1;
    Check(render(11000) && active == f.previous && logs.back().find("readback-error") != std::string::npos,
        "Readback exception must not disable glow or leak active target");
    malformedPixels = true;
    Check(render(21000) && active == f.previous && logs.back().find("Invalid glow diagnostic pixel count") != std::string::npos,
        "Invalid returned array must be bounded and isolated");
    malformedPixels = false;
    const auto reads = readbacks;
    const auto count = logs.size();
    monsterGlow::ResetGlowRender();
    Check(render(999999) && readbacks == reads && logs.size() == count,"Three-attempt cap must survive resource reset");
    for (auto object : nativeOwned) Check(Get(object).klass->name != "Texture2D","CPU readback texture retained after probe");
}
void PipelineAndCrop() {
    Fixture f;
    const auto output = f.Render(0, {100, 100, 200, 200});
    Check(output && Get(output).width == 256 && Get(output).height == 256, "Expected bounded texture dimensions");
    Check(executions.size() == 1 && active == f.previous, "Capture failed to restore previous active texture");
    const auto& commands = executions.back();
    std::vector<Command> draws, matrices;
    for (const auto& command : commands) {
        if (command.name == "source" || command.name == "mesh" || command.name == "procedural") draws.push_back(command);
        if (command.name == "matrices") matrices.push_back(command);
    }
    Check(draws.size() == 6 && draws[0].name == "source", "Expected source + alpha mask + two blurs + knockout + contrast");
    Check(Near(matrices[0].projection.v[0], 4) && Near(matrices[0].projection.v[5], 3) &&
        Near(matrices[0].projection.v[12], 2) && Near(matrices[0].projection.v[13], 1), "Screen crop matrix is wrong");
    Check(Near(matrices[1].projection.v[0], 1) && Near(matrices[1].projection.v[5], 1), "Mask should use raw identity projection");
    Check(Near(matrices.back().projection.v[0], 1) && Near(matrices.back().projection.v[5], 1), "Camera projection was not restored");
    const auto& mask = draws[1].parameters;
    const auto& horizontal = draws[2].parameters;
    const auto& vertical = draws[3].parameters;
    const auto& mix = draws[4].parameters;
    Check(Get(draws[1].second).shader == "Hidden/UIR/ColorEffect" && mask.transforms.contains("_ColorMatrix"),
        "Mask must extract captured alpha without the world-outline mode");
    const auto& alphaMatrix=mask.transforms.at("_ColorMatrix");
    for (int i=0;i<16;++i) Check(Near(alphaMatrix.v[i],i==12 ? 4.0f : 0.0f),"Alpha mask must ignore source RGB and write only red");
    Check(Near(mask.floats.at("_ColorInvert"),0) && Near(mask.floats.at("_ColorOffset"),0),"Transparent background must remain zero under either color-space variant");
    Check(Get(draws[2].second).shader == "Hidden/UIR/GaussianBlur" && draws[2].pass == 0 && draws[3].pass == 1,
        "Expected horizontal/vertical native blur passes");
    Check(draws[2].second != draws[3].second && Near(horizontal.floats.at("_Sigma"), 4.608f), "Blur draw state must be private per pass");
    Check(Near(horizontal.vectors.at("_MainTex_TexelSize").x, 1.0f/256), "Wrong private texel size");
    Check(Get(draws[4].second).shader == "Custom/CommonGrayScale" && mix.floats.contains("_IsInline") && mix.floats.at("_IsInline") == 0,
        "Exterior knockout must use explicit per-draw zero inline mode");
    Check(mix.textures.at("_CommonGrayScaleMixTex") == horizontal.textures.at("_MainTex"), "Knockout used the wrong original mask");
    const auto& contrast=draws[5].parameters;
    Check(contrast.textures.at("_MainTex")==vertical.textures.at("_MainTex") && output!=vertical.textures.at("_MainTex"),
        "Dedicated presented texture must read the completed knockout, never alias an intermediate");
    const auto& gain=contrast.transforms.at("_ColorMatrix");
    for(int i=0;i<16;++i) Check(Near(gain.v[i],i>=12 ? 10.0f : 0.0f),"Presentation must amplify only exterior coverage into RGBA");
    Check(contrast.floats.at("_ColorInvert")==0 && contrast.floats.at("_ColorOffset")==0,
        "Empty background/interior must stay transparent after contrast");
    Check(commands[commands.size()-6].name == "target" && commands[commands.size()-6].first == f.previous,
        "Restore target must be recorded before execution");
    Check(Get(f.material).textures.empty() && Get(f.material).vectors.empty(), "Original material changed");
}
void ExplicitShaderMatrices() {
    Fixture f;
    const auto saved = globals;
    Get(f.camera).view.v[12] = 3;
    Get(f.camera).view.v[13] = 5;
    Check(f.Render(0,{100,100,200,200}) != nullptr, "Matrix fixture render failed");
    auto current = saved;
    unsigned draws = 0;
    for (const auto& command : executions.back()) {
        if (command.name == "global-matrix") current[command.property] = command.projection;
        if (command.name == "source") {
            const auto& vp = current.at("unity_MatrixVP");
            Check(Near(vp.v[0],4) && Near(vp.v[5],-3) && Near(vp.v[12],14) && Near(vp.v[13],-16),
                "URP source still inherits unrelated shader view-projection");
            ++draws;
        }
        if (command.name == "mesh") {
            const auto& vp = current.at("unity_MatrixVP");
            Check(Near(vp.v[0],1) && Near(vp.v[5],-1) && Near(vp.v[12],0) && Near(vp.v[13],0),
                "Fullscreen mask/blur inherits world shader matrix");
            ++draws;
        }
    }
    Check(draws == 5, "Matrix fixture did not inspect source/mask/both blur/contrast draws");
    for (const auto& [name,matrix] : saved)
        Check(std::memcmp(matrix.v,globals.at(name).v,sizeof(matrix)) == 0,"Shader global was not restored exactly");
}
void CoverageUnionPipeline() {
    Fixture f;
    const auto saved = globals;
    Get(f.camera).view.v[12] = 3;
    Get(f.camera).view.v[13] = 5;
    const auto wall = Make("SpriteRenderer"), wallMaterial = Make("Material"), wallTexture = Make("Texture2D");
    Get(wallMaterial).shader = "Custom/DofusMapShader";
    Get(wallMaterial).floats["_OutlineType"] = 7;
    Get(wallMaterial).textures["_MainTex"] = wallTexture;
    Get(wall).items = {wallMaterial};
    GlowCoverage coverage{{wall}, {{.125f,.25f,.375f,.1875f}, {.5f,.0625f,.125f,.3125f}}};
    const auto presented = f.Render(0, {100,100,200,160}, &coverage);
    Check(presented && executions.size() == 1 && active == f.previous, "Coverage union must render and restore active target");
    struct Captured { Command draw; Obj target; ScreenRect viewport; Matrix vp; bool depthCleared; };
    std::vector<Captured> draws;
    auto current = saved;
    Obj target = f.previous;
    ScreenRect viewport{};
    std::map<Obj,bool> depthCleared;
    for (const auto& command : executions.back()) {
        if (command.name == "target") target = command.first;
        if (command.name == "viewport") viewport = command.rectangle;
        if (command.name == "global-matrix") current[command.property] = command.projection;
        if (command.name == "clear") depthCleared[target] = command.clearDepth;
        if (command.name == "source" || command.name == "mesh" || command.name == "procedural") {
            for (const auto& [name, texture] : command.parameters.textures) {
                (void)name;
                Check(texture != target, "Coverage recording samples its own render target");
            }
            draws.push_back({command, target, viewport, current.at("unity_MatrixVP"), depthCleared[target]});
        }
    }
    Check(draws.size() == 11, "Expected full glow, wall, two HUD fills, inverse coverage, clipping mix and presentation");
    const auto capture = draws[0].target, mask = draws[1].target, fullGlow = draws[4].target;
    Check(capture != mask && mask != fullGlow && capture != fullGlow &&
        presented != capture && presented != mask && presented != fullGlow,
        "Coverage pipeline must retain four distinct private render targets");
    Check(draws[0].draw.first == f.renderer && draws[5].draw.first == wall && draws[5].draw.second == wallMaterial &&
        draws[5].draw.pass == 0 && draws[5].draw.submesh == 0 && draws[5].target == capture && draws[5].depthCleared,
        "Wall coverage must redraw the original decor material/pass0 after fresh depth clear");
    Check(std::memcmp(draws[0].vp.v,draws[5].vp.v,sizeof(Matrix)) == 0 &&
        Near(draws[5].vp.v[0],4) && Near(draws[5].vp.v[5],-3.75f) &&
        Near(draws[5].vp.v[12],14) && Near(draws[5].vp.v[13],-20.25f),
        "Wall coverage must use the same exact cropped world projection as the monster");
    const ScreenRect expectedRects[] = {{32,64,96,48}, {128,16,32,80}};
    const auto identity = Identity();
    for (size_t i = 0; i < 2; ++i) {
        const auto& fill = draws[6+i];
        const auto& p = fill.draw.parameters;
        const auto expected = expectedRects[i];
        Check(fill.target == capture && fill.draw.name == "mesh" && fill.draw.pass == 0 &&
            Get(fill.draw.second).shader == "Hidden/UIR/ColorEffect", "HUD coverage must share the decor coverage target");
        Check(Near(fill.viewport.x, expected.x) && Near(fill.viewport.y,expected.y) &&
            Near(fill.viewport.width,expected.width) && Near(fill.viewport.height,expected.height),
            "HUD viewport must scale fractional bottom-left coordinates without Y inversion");
        Check(p.textures.at("_MainTex") == whiteTexture && !Get(whiteTexture).owned &&
            p.floats.at("_ColorInvert") == 0 && p.floats.at("_ColorOffset") == 0 &&
            std::memcmp(p.transforms.at("_ColorMatrix").v,identity.v,sizeof(Matrix)) == 0,
            "HUD fill must use the shared opaque white texture with an exact identity color transform");
        Check(std::memcmp(fill.draw.view.v,identity.v,sizeof(Matrix)) == 0 &&
            Near(fill.vp.v[0],1) && Near(fill.vp.v[5],-1) && Near(fill.vp.v[12],0) && Near(fill.vp.v[13],0),
            "HUD fill must use identity draw transform and fullscreen shader matrices");
    }
    const auto& inverse = draws[8];
    Check(inverse.target == mask && inverse.draw.parameters.textures.at("_MainTex") == capture &&
        inverse.draw.parameters.floats.at("_ColorInvert") == 1 && inverse.draw.parameters.floats.at("_ColorOffset") == 0,
        "Coverage alpha must be inverted into the separate mask target");
    for (int i = 0; i < 16; ++i)
        Check(Near(inverse.draw.parameters.transforms.at("_ColorMatrix").v[i],i == 12 ? 1.0f : 0.0f),
            "Coverage inversion must depend only on source alpha");
    const auto& clipped = draws[9];
    Check(clipped.draw.name == "procedural" && clipped.target == capture &&
        Get(clipped.draw.second).shader == "Custom/CommonGrayScale" && clipped.draw.pass == 0 &&
        clipped.draw.parameters.textures.at("_CommonGrayScaleMixTex") == mask &&
        clipped.draw.parameters.textures.at("_BlitTexture") == fullGlow &&
        clipped.draw.parameters.floats.at("_IsInline") == 0,
        "Native mix must multiply full glow by actual HUD/decor coverage into another target");
    const auto& final = draws[10];
    Check(final.target == presented && final.draw.parameters.textures.at("_MainTex") == capture &&
        final.draw.parameters.floats.at("_ColorInvert") == 0 && final.draw.parameters.floats.at("_ColorOffset") == 0,
        "Contrast presentation must sample the clipped glow and preserve empty background");
    for (int i = 0; i < 16; ++i)
        Check(Near(final.draw.parameters.transforms.at("_ColorMatrix").v[i],i >= 12 ? 10.0f : 0.0f),
            "Clipped glow must retain the existing bright exterior presentation");
    for (size_t i : {size_t{8},size_t{9},size_t{10}})
        Check(Near(draws[i].viewport.x,0) && Near(draws[i].viewport.y,0) &&
            Near(draws[i].viewport.width,256) && Near(draws[i].viewport.height,256),
            "All passes after HUD fills must reset the full capture viewport");
    Check(Get(wall).items == std::vector<Obj>{wallMaterial} && Get(wallMaterial).floats.size() == 1 &&
        Get(wallMaterial).floats.at("_OutlineType") == 7 && Get(wallMaterial).textures.size() == 1 &&
        Get(wallMaterial).textures.at("_MainTex") == wallTexture && Get(wallMaterial).vectors.empty(),
        "Coverage capture must leave shared wall material and renderer unchanged");
    for (const auto& [name,matrix] : saved)
        Check(std::memcmp(matrix.v,globals.at(name).v,sizeof(Matrix)) == 0,"Coverage path leaked shader global matrices");
    const auto retained = nativeOwned.size(), refs = handles.size(), allocated = allocations.size();
    Check(f.Render(0,{100,100,200,160},&coverage) == presented && nativeOwned.size() == retained &&
        handles.size() == refs && allocations.size() == allocated, "Repeated coverage capture must reuse private resources");
    Check(f.Render(0,{100,100,200,160}) == presented, "Switching to always-glow should retain the output slot");
    size_t unrestrictedDraws = 0;
    Obj presentationInput = nullptr;
    for (const auto& command : executions.back()) {
        if (command.name == "source" || command.name == "mesh" || command.name == "procedural") ++unrestrictedDraws;
        if (command.name == "mesh") presentationInput = command.parameters.textures.at("_MainTex");
    }
    Check(unrestrictedDraws == 6 && presentationInput == fullGlow, "Unmasked capture must not retain the previous coverage sampler");
    monsterGlow::ResetGlowRender();
    Check(nativeOwned.empty() && handles.empty() && !Get(whiteTexture).owned && !Get(wallMaterial).owned,
        "Coverage teardown must release only private resources, preserving the shared white texture and decor");
}

void CoverageBoundsAndBudgets() {
    Fixture f;
    const auto rejected = [&](const GlowCoverage& coverage) {
        Check(!f.Render(0,{0,0,64,64},&coverage) && nativeOwned.empty() && handles.empty() && allocations.empty() && executions.empty(),
            "Invalid/empty coverage must return before resource allocation or command execution");
    };
    rejected({});
    rejected({std::vector<Obj>(65,f.renderer), {}});
    rejected({{}, std::vector<ScreenRect>(65,{0,0,1,1})});
    rejected({{nullptr}, {}});
    for (auto rectangle : {ScreenRect{-0.1f,0,.2f,.2f}, ScreenRect{0,-.1f,.2f,.2f}, ScreenRect{0,0,0,.2f},
        ScreenRect{0,0,.2f,-.2f}, ScreenRect{.9f,0,.2f,.2f}, ScreenRect{0,.9f,.2f,.2f},
        ScreenRect{NAN,0,.2f,.2f}, ScreenRect{0,0,INFINITY,.2f}})
        rejected({{}, {rectangle}});
    GlowCoverage rectangles{{},std::vector<ScreenRect>(64,{.25f,.25f,.5f,.5f})};
    Check(f.Render(0,{0,0,64,64},&rectangles), "Maximum supported HUD rectangle count was rejected");
    size_t hudFills = 0;
    for (const auto& command : executions.back())
        if (command.parameters.textures.contains("_MainTex") && command.parameters.textures.at("_MainTex") == whiteTexture) ++hudFills;
    Check(hudFills == 64, "HUD rectangle count must remain bounded and complete");
    GlowCoverage walls{std::vector<Obj>(64,f.renderer),{}};
    Check(f.Render(0,{0,0,64,64},&walls), "Maximum supported wall renderer count was rejected");
    size_t sourceDraws = 0;
    for (const auto& command : executions.back()) if (command.name == "source") ++sourceDraws;
    Check(sourceDraws == 65, "Only the requested monster and 64 bounded wall renderers may be recorded");
    const auto badWall = Make("SpriteRenderer");
    Get(badWall).items = std::vector<Obj>(33,f.material);
    GlowCoverage badMaterials{{badWall},{}};
    executions.clear();
    const auto retained = nativeOwned.size(), allocated = allocations.size(), refs = handles.size();
    // An implausible wall material count no longer disables the glow: that
    // wall is left out of the frame and the monster is still drawn.
    Check(f.Render(0,{0,0,64,64},&badMaterials) && executions.size() == 1 && handles.size() == refs,
        "Wall material overflow leaves that wall out of the frame without failing");
    sourceDraws = 0;
    for (const auto& command : executions.back()) if (command.name == "source") ++sourceDraws;
    Check(sourceDraws == 1, "Only the monster is recorded when the wall's materials are out of bounds");
    (void)retained; (void)allocated;
    Get(badWall).items = std::vector<Obj>(32,f.material);
    badMaterials.renderers = std::vector<Obj>(9,badWall);
    executions.clear();
    Check(f.Render(0,{0,0,64,64},&badMaterials) && executions.size() == 1 && handles.size() == refs,
        "The wall draw budget ends the collection instead of failing the frame");
    sourceDraws = 0;
    for (const auto& command : executions.back()) if (command.name == "source") ++sourceDraws;
    Check(sourceDraws == 1 + 8 * 32, "Walls are recorded up to the bounded draw budget");
    const auto monsterOnly = Make("SpriteRenderer");
    Get(monsterOnly).items = {};
    bool skipped = false;
    try { skipped = monsterGlow::RenderGlow(0, f.camera, {monsterOnly}, {0,0,64,64}, GetTickCount64(), nullptr) == nullptr; } catch (...) { skipped = false; }
    Check(skipped, "A monster renderer without materials (map still loading) yields no texture for the frame and no exception");
}

void CoverageFailuresRestore() {
    Fixture f;
    GlowCoverage coverage{{f.renderer},{{.25f,.125f,.5f,.5f}}};
    const auto saved = globals;
    const auto restored = [&]() {
        Check(active == f.previous, "Coverage error leaked active render target");
        for (const auto& [name,matrix] : saved)
            Check(std::memcmp(matrix.v,globals.at(name).v,sizeof(Matrix)) == 0, "Coverage error leaked shader globals");
    };
    Check(f.Render(0,{0,0,64,64},&coverage), "Coverage error fixture failed initial render");
    const auto retained = nativeOwned.size(), refs = handles.size();
    for (unsigned draw : {4u,5u}) {
        executions.clear(); failName = "DrawMesh"; failCountdown = draw;
        bool threw = false; try { f.Render(0,{0,0,64,64},&coverage); } catch (...) { threw = true; }
        Check(threw && executions.empty() && nativeOwned.size() == retained && handles.size() == refs,
            "Recording HUD/inverse-mask failure must not execute partial commands or leak resources");
        restored();
        Check(f.Render(0,{0,0,64,64},&coverage), "Coverage renderer did not recover after mask recording failure");
    }
    executions.clear(); failAfterCoverageMask = true;
    bool threw = false; try { f.Render(0,{0,0,64,64},&coverage); } catch (...) { threw = true; }
    Check(threw && executions.size() == 2 && executions.back().size() == 6 &&
        executions.back()[0].name == "target" && executions.back()[0].first == f.previous,
        "Execution failure inside inverse coverage must submit only a restoration buffer");
    restored();
    Check(nativeOwned.size() == retained && handles.size() == refs, "Coverage execution failure retained transient roots/resources");
    Check(f.Render(0,{0,0,64,64},&coverage), "Coverage renderer did not recover after mask execution failure");
    restored();
}
void ShaderMatrixFailureRestoration() {
    Fixture f;
    auto saved = globals;
    const auto unchanged = [&]() {
        for (const auto& [name,matrix] : saved)
            Check(std::memcmp(matrix.v,globals.at(name).v,sizeof(matrix)) == 0,"Failure leaked shader globals");
        Check(active == f.previous,"Failure leaked active target");
    };
    failAfterShaderMatrices = true;
    bool threw = false; try { f.Render(); } catch (...) { threw = true; }
    Check(threw && executions.size() == 2,"Partial execution did not submit restoration-only buffer");
    unchanged();
    for (const char* method : {"GetGlobalMatrix", "GetGPUProjectionMatrix", "SetGlobalMatrix"}) {
        executions.clear(); failName = method; failCountdown = 1;
        threw = false; try { f.Render(); } catch (...) { threw = true; }
        Check(threw && executions.empty(),"Matrix preparation failure submitted a partial capture");
        unchanged();
    }
    Check(f.Render() != nullptr,"Failed matrix preparation did not recover");
    unchanged();
}
void BoundsReuseAndIndependentSlots() {
    Fixture f;
    const auto small = f.Render(0, {10, 10, 50, 70});
    const auto allocationCount = allocations.size();
    Check(Get(small).width == 64 && Get(small).height == 128, "Small capture sizing wrong");
    Check(f.Render(0, {11, 12, 40, 60}) == small && allocations.size() == allocationCount, "Movement/shrink reallocates resources");
    const auto large = f.Render(0, {0, 0, 1000, 900});
    Check(large != small && Get(large).width == 256 && Get(large).height == 256, "Growth/max256 sizing wrong");
    const auto other = f.Render(1, {0, 0, 64, 64});
    Check(other != large, "Slots unexpectedly share output");
    Obj firstMix = nullptr, secondMix = nullptr;
    for (const auto& command : executions[2]) if (command.name == "procedural") firstMix = command.second;
    for (const auto& command : executions[3]) if (command.name == "procedural") secondMix = command.second;
    Check(firstMix != secondMix, "Multiple monsters share mutable shader draw state");
    const auto& firstParameters=executions[2][executions[2].size()-7].parameters;
    const auto& secondParameters=executions[3][executions[3].size()-7].parameters;
    Check(firstParameters.textures.at("_MainTex") != secondParameters.textures.at("_MainTex"),
        "Recorded per-draw textures must remain independent across monsters");
    Check(f.Render(32) == nullptr, "Slot budget exceeded");
    for (const auto& dimensions : allocations) Check(dimensions[0] <= 256 && dimensions[1] <= 256, "Texture budget exceeded");
    monsterGlow::ReleaseGlowSlot(0); monsterGlow::ReleaseGlowSlot(0); monsterGlow::ReleaseGlowSlot(99);
    Check(!Get(other).destroyed, "Releasing one slot destroyed another");
}
void UnavailableAndInvalidRequests() {
    Fixture f;
    Check(f.Render(0, {0,0,0,20}) == nullptr, "Zero area accepted");
    Check(f.Render(0, {NAN,0,20,20}) == nullptr, "NaN rectangle accepted");
    Check(monsterGlow::RenderGlow(0, f.camera, {}, {0,0,20,20}) == nullptr, "Empty source accepted");
    std::vector<Obj> tooMany(65, f.renderer);
    Check(monsterGlow::RenderGlow(0, f.camera, tooMany, {0,0,20,20}) == nullptr, "Renderer cap ignored");
    Get(f.camera).projection.v[0] = INFINITY;
    Check(f.Render() == nullptr && nativeOwned.empty(), "Nonfinite camera allocated/drew resources");
    Get(f.camera).projection = Identity();
    missingShader = "Hidden/UIR/GaussianBlur";
    bool threw = false; try { f.Render(); } catch (...) { threw = true; }
    Check(threw && nativeOwned.empty() && handles.empty() && executions.empty(), "Missing shader did not fail cleanly");
}
void RecordingAndExecuteFailuresRestore() {
    Fixture f; f.Render(); executions.clear();
    failName = "DrawMesh"; failCountdown = 1;
    bool threw = false; try { f.Render(); } catch (...) { threw = true; }
    Check(threw && executions.empty() && active == f.previous, "Recording failure executed partial capture");
    failName = "ExecuteCommandBuffer"; failCountdown = 1; threw = false;
    try { f.Render(); } catch (...) { threw = true; }
    Check(threw && active == f.previous && executions.size() == 1, "Execute failure did not attempt restoration");
    Check(executions[0].size() == 6 && executions[0][0].name == "target", "Failure restore unexpectedly draws source");
    Check(f.Render() != nullptr, "Renderer cannot recover after an execution error");
}
void AllocationFailureAndBackbuffer() {
    Fixture f; active = nullptr;
    failName = "Create"; failCountdown = 2;
    bool threw = false; try { f.Render(); } catch (...) { threw = true; }
    Check(threw && executions.empty(), "Partial allocation unexpectedly rendered");
    monsterGlow::ResetGlowRender();
    Check(nativeOwned.empty() && handles.empty(), "Partial texture allocation leaked resources");
    Check(f.Render() != nullptr && active == nullptr, "Backbuffer target was not restored");
    const auto saved = nativeOwned.size();
    Get(f.renderer).items.clear(); threw = false;
    Obj missing = nullptr;
    try { missing = f.Render(); } catch (...) { threw = true; }
    Check(!threw && !missing && nativeOwned.size() == saved, "Missing materials yield no texture for the frame, no exception and no extra render targets");
}
void PropertyBlockFailureIsIsolated() {
    Fixture f;
    failName="SetMatrix"; failCountdown=1;
    bool threw=false; try { f.Render(); } catch (...) { threw=true; }
    Check(threw && executions.empty() && active==f.previous,"Property setup failure must never submit a partial capture");
    monsterGlow::ResetGlowRender();
    Check(handles.empty() && nativeOwned.empty(),"Failed property-block setup leaked owned roots or textures");
    Check(f.Render()!=nullptr,"Property-block setup must recover after a failure");
    executions.clear(); failName="SetTexture"; failCountdown=2; threw=false;
    try { f.Render(); } catch (...) { threw=true; }
    Check(threw && executions.empty() && active==f.previous,"Texture parameter failure submitted stale commands");
    Check(f.Render()!=nullptr,"Texture parameter binding must recover after a failure");
}
}

int main() {
    try {
        fake::Initialize(); monsterGlow::InstallGlowRender();
        const auto installedGuards = fake::guards; monsterGlow::InstallGlowRender();
        fake::Check(installedGuards == 39 && fake::guards == installedGuards, "Wrong/idempotency native guard count");
        BoundedPrivatePixelDiagnostics();
        ExplicitShaderMatrices();
        ShaderMatrixFailureRestoration();
        PipelineAndCrop(); BoundsReuseAndIndependentSlots(); UnavailableAndInvalidRequests();
        CoverageUnionPipeline(); CoverageBoundsAndBudgets(); CoverageFailuresRestore();
        RecordingAndExecuteFailuresRestore(); AllocationFailureAndBackbuffer();
        PropertyBlockFailureIsIsolated();
        monsterGlow::ResetGlowRender();
        std::cout << "MonsterGlowRenderTests: PASS\n"; return 0;
    } catch (const std::exception& error) {
        std::cerr << "MonsterGlowRenderTests: FAIL: " << error.what() << '\n'; return 1;
    }
}
