#include "monster-glow-render.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <memory>
#include <sstream>

namespace monsterGlow {
namespace {
using namespace managed;
struct Vector4 { float x, y, z, w; };
struct Matrix { float v[16]{}; };
struct TargetIdentifier { alignas(8) unsigned char data[40]{}; };
static_assert(sizeof(Matrix) == 64 && sizeof(Vector4) == 16 && sizeof(TargetIdentifier) == 40);
constexpr size_t MaxSlots = 32, MaxRenderers = 64, MaxDraws = 256;
struct Methods {
    Klass materialClass{}, textureClass{}, bufferClass{}, listClass{}, blockClass{};
    Method shaderFind{}, materialCtor{}, materialCopyCtor{}, blockCtor{}, setFloat{}, setVector{}, setTexture{}, setMatrix{}, queue{};
    Method textureCtor{}, create{}, releaseTexture{}, active{}, setActive{}, filter{}, wrap{}, destroy{};
    Method bufferCtor{}, clear{}, target{}, clearTarget{}, matrices{}, viewport{}, drawRenderer{}, drawMesh{}, drawProcedural{}, releaseBuffer{};
    Method textureIdentifier{}, builtinIdentifier{}, execute{}, sharedMaterials{}, cameraView{}, cameraProjection{}, cameraRect{}, fullscreenMesh{};
    Method gpuProjection{}, getGlobalMatrix{}, setGlobalMatrix{}, whiteTexture{};
} methods;
struct Slot {
    Ref capture, mask, output, present, maskMaterial, horizontalMaterial, verticalMaterial, mixMaterial;
    Ref maskProperties, horizontalProperties, verticalProperties, mixProperties, presentProperties;
    Ref coverageProperties, whiteProperties, coverageMixProperties;
    int width = 0, height = 0;
};
struct Resources {
    Ref maskMaterial, blurMaterial, mixMaterial, buffer, materialList, fullscreen, white;
    std::array<Slot, MaxSlots> slots;
};
std::unique_ptr<Resources> resources;
bool installed = false;
// Three diagnostic samples per module lifetime, separated by at least 10s.
// These counters deliberately survive map changes and option toggles.
unsigned probeAttempts = 0;
uint64_t nextProbe = 0;
struct ProbeMethods { Klass texture{}; Method constructor{}, copy{}, read{}, pixels{}; } probeMethods;

Method Guard(const char* space, const char* type, const char* name, int argc, uintptr_t rva,
             const char* image = "UnityEngine.CoreModule.dll") {
    return api.FindVerified(api.Class(image, space, type), name, argc, rva);
}
bool Finite(const Matrix& m) {
    return std::all_of(std::begin(m.v), std::end(m.v), [](float f) { return std::isfinite(f); });
}
bool Valid(ScreenRect r) {
    return std::isfinite(r.x) && std::isfinite(r.y) && std::isfinite(r.width) && std::isfinite(r.height) &&
        r.width > 0 && r.height > 0 && r.width <= 32768 && r.height <= 32768;
}
Matrix Identity() { Matrix m; m.v[0] = m.v[5] = m.v[10] = m.v[15] = 1; return m; }
Matrix Multiply(const Matrix& a, const Matrix& b) {
    Matrix result;
    for (int column = 0; column < 4; ++column)
        for (int row = 0; row < 4; ++row)
            for (int k = 0; k < 4; ++k)
                result.v[column*4+row] += a.v[k*4+row] * b.v[column*4+k];
    return result;
}
// URP shaders read explicit globals; SetViewProjectionMatrices alone only
// supplies the legacy camera state (the UI blur also uses that legacy state).
// Record both paths and restore the exact globals, even if another UI renderer
// supplied them rather than the world camera.
struct ShaderMatrices {
    static constexpr std::array<const char*,3> Names{"unity_MatrixV", "glstate_matrix_projection", "unity_MatrixVP"};
    std::array<Ref,3> names;
    std::array<Matrix,3> saved;
    ShaderMatrices() {
        for (size_t i = 0; i < names.size(); ++i) {
            names[i].reset(api.string_new(Names[i]));
            saved[i] = api.Value<Matrix>(api.Invoke(methods.getGlobalMatrix, nullptr, {names[i].get()}));
        }
    }
    void Write(Obj buffer, const std::array<Matrix,3>& values) const {
        for (size_t i = 0; i < names.size(); ++i) {
            auto value = values[i];
            api.Invoke(methods.setGlobalMatrix, buffer, {names[i].get(), &value});
        }
    }
    void Set(Obj buffer, Matrix view, Matrix projection) const {
        bool intoTexture = true;
        const auto gpu = api.Value<Matrix>(api.Invoke(methods.gpuProjection, nullptr, {&projection,&intoTexture}));
        const auto vp = Multiply(gpu,view);
        if (!Finite(gpu) || !Finite(vp)) throw std::runtime_error("Nonfinite glow GPU projection");
        // Raw projection for the legacy command; GPU conversion only for the
        // explicit URP globals, never fed back through the legacy conversion.
        api.Invoke(methods.matrices, buffer, {&view,&projection});
        Write(buffer, {view,gpu,vp});
    }
    void Restore(Obj buffer, Matrix view, Matrix projection) const {
        api.Invoke(methods.matrices, buffer, {&view,&projection});
        Write(buffer,saved);
    }
};
Matrix Crop(Matrix projection, ScreenRect camera, ScreenRect rectangle) {
    const float sx = camera.width / rectangle.width, sy = camera.height / rectangle.height;
    const float tx = (2 * camera.x + camera.width - 2 * rectangle.x - rectangle.width) / rectangle.width;
    const float ty = (2 * camera.y + camera.height - 2 * rectangle.y - rectangle.height) / rectangle.height;
    // Unity Matrix4x4 is column-major. Left-multiply by the clip-space crop.
    for (int column = 0; column < 4; ++column) {
        const int offset = column * 4;
        projection.v[offset] = sx * projection.v[offset] + tx * projection.v[offset + 3];
        projection.v[offset + 1] = sy * projection.v[offset + 1] + ty * projection.v[offset + 3];
    }
    return projection;
}
void Destroy(Ref& object, bool texture = false) {
    if (!object.get()) return;
    if (texture) { try { api.Invoke(methods.releaseTexture, object.get()); } catch (...) {} }
    try { api.Invoke(methods.destroy, nullptr, {object.get()}); } catch (...) {}
    object.reset();
}
void ClearSlot(Slot& slot) {
    // PropertyBlock is a managed finalizable object, not UnityEngine.Object.
    // Release its GC roots; never send it through Object.Destroy.
    slot.maskProperties.reset(); slot.horizontalProperties.reset();
    slot.verticalProperties.reset(); slot.mixProperties.reset();
    slot.presentProperties.reset();
    slot.coverageProperties.reset(); slot.whiteProperties.reset(); slot.coverageMixProperties.reset();
    Destroy(slot.capture, true); Destroy(slot.mask, true); Destroy(slot.output, true);
    Destroy(slot.present, true);
    Destroy(slot.maskMaterial); Destroy(slot.horizontalMaterial); Destroy(slot.verticalMaterial); Destroy(slot.mixMaterial);
    slot.width = slot.height = 0;
}
Ref NewObject(Klass klass, Method constructor, std::initializer_list<void*> args = {}) {
    Ref result(api.object_new(klass));
    if (!result.get()) throw std::runtime_error("Glow object allocation failed");
    try { api.Invoke(constructor, result.get(), args); }
    catch (...) {
        if (klass == methods.bufferClass) { try { api.Invoke(methods.releaseBuffer, result.get()); } catch (...) {} }
        else if (klass != methods.blockClass) Destroy(result, klass == methods.textureClass);
        throw;
    }
    return result;
}
Ref Texture(int width, int height, int depth) {
    int format = 0, readWrite = 1; // ARGB32, Linear in this verified Unity metadata.
    Ref result = NewObject(methods.textureClass, methods.textureCtor, {&width, &height, &depth, &format, &readWrite});
    try {
        int bilinear = 1, clamp = 1;
        api.Invoke(methods.filter, result.get(), {&bilinear});
        api.Invoke(methods.wrap, result.get(), {&clamp});
        if (!api.Value<bool>(api.Invoke(methods.create, result.get()))) throw std::runtime_error("Glow RenderTexture creation failed");
        return result;
    } catch (...) { Destroy(result, true); throw; }
}
Ref Material(const char* shaderName) {
    Ref name(api.string_new(shaderName));
    Ref shader(api.Invoke(methods.shaderFind, nullptr, {name.get()}));
    if (!shader.get()) throw std::runtime_error(std::string("Glow shader unavailable: ") + shaderName);
    return NewObject(methods.materialClass, methods.materialCtor, {shader.get()});
}
void Float(Obj properties, const char* name, float value) {
    Ref key(api.string_new(name)); api.Invoke(methods.setFloat, properties, {key.get(), &value});
}
void Vector(Obj properties, const char* name, Vector4 value) {
    Ref key(api.string_new(name)); api.Invoke(methods.setVector, properties, {key.get(), &value});
}
void Sampler(Obj properties, const char* name, Obj texture) {
    Ref key(api.string_new(name)); api.Invoke(methods.setTexture, properties, {key.get(), texture});
}
void EnsureResources() {
    if (resources) return;
    auto staged = std::make_unique<Resources>();
    try {
        staged->maskMaterial = Material("Hidden/UIR/ColorEffect");
        staged->blurMaterial = Material("Hidden/UIR/GaussianBlur");
        staged->mixMaterial = Material("Custom/CommonGrayScale");
        staged->buffer = NewObject(methods.bufferClass, methods.bufferCtor);
        staged->materialList.reset(api.New(methods.listClass));
        staged->fullscreen.reset(api.Invoke(methods.fullscreenMesh, nullptr));
        staged->white.reset(api.Invoke(methods.whiteTexture, nullptr));
        if (!staged->materialList.get() || !staged->fullscreen.get() || !staged->white.get()) throw std::runtime_error("Glow capture support unavailable");
        resources = std::move(staged);
    } catch (...) {
        if (staged->buffer.get()) { try { api.Invoke(methods.releaseBuffer, staged->buffer.get()); } catch (...) {} }
        Destroy(staged->maskMaterial); Destroy(staged->blurMaterial); Destroy(staged->mixMaterial);
        throw;
    }
}
int Extent(float value) {
    int extent = 32;
    while (extent < 256 && extent < value) extent *= 2;
    return extent;
}
void Resize(Slot& slot, ScreenRect rectangle) {
    const int width = std::max(slot.width, Extent(rectangle.width));
    const int height = std::max(slot.height, Extent(rectangle.height));
    if (slot.capture.get() && slot.width == width && slot.height == height) return;
    Slot staged;
    try {
        staged.capture = Texture(width, height, 24);
        staged.mask = Texture(width, height, 0);
        staged.output = Texture(width, height, 0);
        staged.present = Texture(width, height, 0);
        staged.maskMaterial = NewObject(methods.materialClass, methods.materialCopyCtor, {resources->maskMaterial.get()});
        staged.horizontalMaterial = NewObject(methods.materialClass, methods.materialCopyCtor, {resources->blurMaterial.get()});
        staged.verticalMaterial = NewObject(methods.materialClass, methods.materialCopyCtor, {resources->blurMaterial.get()});
        staged.mixMaterial = NewObject(methods.materialClass, methods.materialCopyCtor, {resources->mixMaterial.get()});
        staged.maskProperties = NewObject(methods.blockClass, methods.blockCtor);
        staged.horizontalProperties = NewObject(methods.blockClass, methods.blockCtor);
        staged.verticalProperties = NewObject(methods.blockClass, methods.blockCtor);
        staged.mixProperties = NewObject(methods.blockClass, methods.blockCtor);
        staged.presentProperties = NewObject(methods.blockClass, methods.blockCtor);
        staged.coverageProperties = NewObject(methods.blockClass, methods.blockCtor);
        staged.whiteProperties = NewObject(methods.blockClass, methods.blockCtor);
        staged.coverageMixProperties = NewObject(methods.blockClass, methods.blockCtor);
        // Extract coverage directly from capture alpha. Both native ColorEffect
        // variants map transparent pixels to zero. Boost partially opaque bodies
        // to a solid mask so the knockout stays hollow; retain soft edge coverage.
        // No negative offset: the linear-output polynomial would turn negative
        // RGB into a positive background. No world _OutlineType mode is involved.
        Matrix alpha{}; alpha.v[12] = 4.0f; // column3, row0: red = 4 * source alpha.
        Ref colorMatrix(api.string_new("_ColorMatrix"));
        api.Invoke(methods.setMatrix, staged.maskProperties.get(), {colorMatrix.get(), &alpha});
        Float(staged.maskProperties.get(), "_ColorInvert", 0);
        Float(staged.maskProperties.get(), "_ColorOffset", 0);
        Vector(staged.maskProperties.get(), "_MainTex_ST", {1,1,0,0});
        for (auto properties : {staged.horizontalProperties.get(), staged.verticalProperties.get()}) {
            Vector(properties, "_MainTex_ST", {1,1,0,0});
            Float(properties, "_Sigma", 1.2f);
        }
        Vector(staged.mixProperties.get(), "_BlitScaleBias", {1,1,0,0});
        Float(staged.mixProperties.get(), "_IsInline", 0);
        // The old blurred exterior peaked at ~55% opacity with equally dim RGB.
        // UI alpha blending attenuated that a second time. Give the exterior a
        // solid bright core and a soft tail, preserving zero-alpha interiors.
        Matrix contrast{};
        for (int row = 0; row != 4; ++row) contrast.v[12+row] = 10.0f;
        api.Invoke(methods.setMatrix, staged.presentProperties.get(), {colorMatrix.get(), &contrast});
        Float(staged.presentProperties.get(), "_ColorInvert", 0);
        Float(staged.presentProperties.get(), "_ColorOffset", 0);
        Vector(staged.presentProperties.get(), "_MainTex_ST", {1,1,0,0});
        Matrix coverageAlpha{}; coverageAlpha.v[12] = 1;
        api.Invoke(methods.setMatrix, staged.coverageProperties.get(), {colorMatrix.get(), &coverageAlpha});
        Float(staged.coverageProperties.get(), "_ColorInvert", 1);
        Float(staged.coverageProperties.get(), "_ColorOffset", 0);
        Vector(staged.coverageProperties.get(), "_MainTex_ST", {1,1,0,0});
        auto white = Identity();
        api.Invoke(methods.setMatrix, staged.whiteProperties.get(), {colorMatrix.get(), &white});
        Float(staged.whiteProperties.get(), "_ColorInvert", 0);
        Float(staged.whiteProperties.get(), "_ColorOffset", 0);
        Vector(staged.whiteProperties.get(), "_MainTex_ST", {1,1,0,0});
        Float(staged.coverageMixProperties.get(), "_IsInline", 0);
        Vector(staged.coverageMixProperties.get(), "_BlitScaleBias", {1,1,0,0});
        staged.width = width; staged.height = height;
    } catch (...) { ClearSlot(staged); throw; }
    ClearSlot(slot);
    slot = std::move(staged);
}
TargetIdentifier Identifier(Obj texture) {
    if (texture) return api.Value<TargetIdentifier>(api.Invoke(methods.textureIdentifier, nullptr, {texture}));
    int cameraTarget = 2;
    return api.Value<TargetIdentifier>(api.Invoke(methods.builtinIdentifier, nullptr, {&cameraTarget}));
}
void Target(Obj buffer, Obj texture) {
    auto identifier = Identifier(texture);
    api.Invoke(methods.target, buffer, {&identifier});
}
void Clear(Obj buffer, bool depth = false) {
    bool color = true; Vector4 transparent{};
    api.Invoke(methods.clearTarget, buffer, {&depth, &color, &transparent});
}
struct Draw { Ref renderer, material; int submesh = 0, queue = 0; };
std::vector<Draw> Collect(const std::vector<Obj>& renderers) {
    std::vector<Draw> draws;
    for (auto renderer : renderers) {
        if (!renderer) throw std::runtime_error("Glow renderer unavailable");
        api.Invoke(methods.sharedMaterials, renderer, {resources->materialList.get()});
        const int count = api.Value<int>(api.Call(resources->materialList.get(), "get_Count"));
        // A renderer without shared materials (seen right after a zaap, while
        // the next map loads) or with an implausible count is left out of this
        // frame; reaching the draw budget ends the collection. Neither disables
        // the glow for the rest of the session.
        if (count <= 0 || count > 32) continue;
        if (draws.size() + static_cast<size_t>(count) > MaxDraws) break;
        for (int i = 0; i < count; ++i) {
            Ref material(api.Call(resources->materialList.get(), "get_Item", {&i}));
            if (!material.get()) throw std::runtime_error("Glow material unavailable");
            const int queue = api.Value<int>(api.Invoke(methods.queue, material.get()));
            draws.push_back({Ref(renderer), std::move(material), i, queue});
        }
    }
    std::stable_sort(draws.begin(), draws.end(), [](const Draw& a, const Draw& b) { return a.queue < b.queue; });
    return draws;
}
struct Probe {
    Ref snapshot, cpu;
    int width = 0, height = 0;
    ~Probe() { Destroy(snapshot, true); Destroy(cpu); }
};
std::unique_ptr<Probe> PrepareProbe(size_t slot, int width, int height, uint64_t now) {
    if (slot != 0 || probeAttempts >= 3 || now < nextProbe) return {};
    ++probeAttempts; nextProbe = now + 10000;
    try {
        if (!probeMethods.texture) {
            ProbeMethods staged;
            staged.texture = api.Class("UnityEngine.CoreModule.dll", "UnityEngine", "Texture2D");
            staged.constructor = Guard("UnityEngine", "Texture2D", ".ctor", 5, 0x5410E20);
            staged.copy = Guard("UnityEngine.Rendering", "CommandBuffer", "CopyTexture", 2, 0x5450CA0);
            staged.read = Guard("UnityEngine", "Texture2D", "ReadPixels", 4, 0x540EAF0);
            staged.pixels = Guard("UnityEngine", "Texture2D", "GetPixels32", 0, 0x540D5E0);
            probeMethods = staged;
        }
        auto result = std::make_unique<Probe>();
        result->width = width; result->height = height;
        result->snapshot = Texture(width, height, 24);
        int rgba32 = 4; bool mipmaps = false, linear = true;
        result->cpu = NewObject(probeMethods.texture, probeMethods.constructor, {&width,&height,&rgba32,&mipmaps,&linear});
        return result;
    } catch (const std::exception& error) {
        Log(std::string("[DEBUG:glow-pixels] unavailable: ") + error.what());
        return {};
    }
}
std::string PixelCounts(Probe& probe, Obj texture) {
    api.Invoke(methods.setActive, nullptr, {texture});
    ScreenRect rect{0,0,static_cast<float>(probe.width),static_cast<float>(probe.height)};
    int zero = 0; bool mipmaps = false;
    api.Invoke(probeMethods.read, probe.cpu.get(), {&rect,&zero,&zero,&mipmaps});
    Ref pixels(api.Invoke(probeMethods.pixels, probe.cpu.get()));
    const size_t expected = static_cast<size_t>(probe.width) * probe.height;
    // Pinned IL2CPP x64 array layout: bounds at16, length at24, Color32 data at32.
    if (!pixels.get() || expected > 65536 || Read<uintptr_t>(pixels.get(),24) != expected)
        throw std::runtime_error("Invalid glow diagnostic pixel count");
    size_t alpha = 0, red = 0; unsigned maxAlpha = 0, maxRed = 0;
    const auto bytes = static_cast<const uint8_t*>(pixels.get()) + 32;
    for (size_t i = 0; i < expected; ++i) {
        const unsigned r = bytes[4*i], a = bytes[4*i+3];
        red += r > 5; alpha += a > 5;
        maxRed = std::max(maxRed,r); maxAlpha = std::max(maxAlpha,a);
    }
    std::ostringstream text;
    text << "a:" << alpha << "/" << expected << ",maxA:" << maxAlpha << ",r:" << red << ",maxR:" << maxRed;
    return text.str();
}
void InspectPixels(Probe& probe, const Slot& slot, Obj previousTarget) {
    try {
        // Only private mod textures are read. No screen/game camera target is
        // read or saved. Counts, not pixels, are written to the module log.
        const auto source = PixelCounts(probe,probe.snapshot.get());
        const auto mask = PixelCounts(probe,slot.mask.get());
        const auto blur = PixelCounts(probe,slot.capture.get());
        const auto output = PixelCounts(probe,slot.output.get());
        const auto presented = PixelCounts(probe,slot.present.get());
        Log("[DEBUG:glow-pixels] sample=" + std::to_string(probeAttempts) +
            " source={" + source + "} mask={" + mask + "} blur={" + blur + "} output={" + output +
            "} presented={" + presented + "}");
    } catch (const std::exception& error) {
        Log(std::string("[DEBUG:glow-pixels] readback-error: ") + error.what());
    }
    // Readback failure is isolated from normal glow rendering.
    api.Invoke(methods.setActive, nullptr, {previousTarget});
}
}

void InstallGlowRender() {
    if (installed) return;
    methods.whiteTexture = Guard("UnityEngine", "Texture2D", "get_whiteTexture", 0, 0x5412100);
    methods.materialClass = api.Class("UnityEngine.CoreModule.dll", "UnityEngine", "Material");
    methods.textureClass = api.Class("UnityEngine.CoreModule.dll", "UnityEngine", "RenderTexture");
    methods.bufferClass = api.Class("UnityEngine.CoreModule.dll", "UnityEngine.Rendering", "CommandBuffer");
    methods.blockClass = api.Class("UnityEngine.CoreModule.dll", "UnityEngine", "MaterialPropertyBlock");
    methods.shaderFind = Guard("UnityEngine", "Shader", "Find", 1, 0x53F5EF0);
    methods.materialCtor = Guard("UnityEngine", "Material", ".ctor", 1, 0x53E49B0);
    methods.materialCopyCtor = Guard("UnityEngine", "Material", ".ctor", 1, 0x53E4B30);
    methods.blockCtor = Guard("UnityEngine", "MaterialPropertyBlock", ".ctor", 0, 0x53D9F90);
    methods.setFloat = Guard("UnityEngine", "MaterialPropertyBlock", "SetFloat", 2, 0x53D8E10);
    methods.setVector = Guard("UnityEngine", "MaterialPropertyBlock", "SetVector", 2, 0x53D9F00);
    methods.setTexture = Guard("UnityEngine", "MaterialPropertyBlock", "SetTexture", 2, 0x53D9830);
    methods.setMatrix = Guard("UnityEngine", "MaterialPropertyBlock", "SetMatrix", 2, 0x53D9510);
    methods.queue = Guard("UnityEngine", "Material", "get_renderQueue", 0, 0x53E55A0);
    methods.textureCtor = Guard("UnityEngine", "RenderTexture", ".ctor", 5, 0x54078E0);
    methods.create = Guard("UnityEngine", "RenderTexture", "Create", 0, 0x5404A20);
    methods.releaseTexture = Guard("UnityEngine", "RenderTexture", "Release", 0, 0x54063C0);
    methods.active = Guard("UnityEngine", "RenderTexture", "get_active", 0, 0x5404AD0);
    methods.setActive = Guard("UnityEngine", "RenderTexture", "set_active", 1, 0x5406480);
    methods.filter = Guard("UnityEngine", "Texture", "set_filterMode", 1, 0x5414BC0);
    methods.wrap = Guard("UnityEngine", "Texture", "set_wrapMode", 1, 0x5415140);
    methods.destroy = Guard("UnityEngine", "Object", "Destroy", 1, 0x5435EA0);
    methods.bufferCtor = Guard("UnityEngine.Rendering", "CommandBuffer", ".ctor", 0, 0x5468930);
    methods.clear = Guard("UnityEngine.Rendering", "CommandBuffer", "Clear", 0, 0x5450590);
    methods.target = Guard("UnityEngine.Rendering", "CommandBuffer", "SetRenderTarget", 1, 0x54669F0);
    methods.clearTarget = Guard("UnityEngine.Rendering", "CommandBuffer", "ClearRenderTarget", 3, 0x5450290);
    methods.matrices = Guard("UnityEngine.Rendering", "CommandBuffer", "SetViewProjectionMatrices", 2, 0x54681E0);
    methods.viewport = Guard("UnityEngine.Rendering", "CommandBuffer", "SetViewport", 1, 0x54682A0);
    methods.drawRenderer = Guard("UnityEngine.Rendering", "CommandBuffer", "DrawRenderer", 4, 0x5455230);
    methods.drawMesh = Guard("UnityEngine.Rendering", "CommandBuffer", "DrawMesh", 6, 0x54532D0);
    methods.drawProcedural = Guard("UnityEngine.Rendering", "CommandBuffer", "DrawProcedural", 7, 0x5454D30);
    methods.releaseBuffer = Guard("UnityEngine.Rendering", "CommandBuffer", "Release", 0, 0x5451E80);
    methods.textureIdentifier = Guard("UnityEngine.Rendering", "RenderTargetIdentifier", "op_Implicit", 1, 0x5474090);
    methods.builtinIdentifier = Guard("UnityEngine.Rendering", "RenderTargetIdentifier", "op_Implicit", 1, 0x5473FF0);
    methods.execute = Guard("UnityEngine", "Graphics", "ExecuteCommandBuffer", 1, 0x53D4200);
    methods.sharedMaterials = Guard("UnityEngine", "Renderer", "GetSharedMaterials", 1, 0x53F1AD0);
    methods.cameraView = Guard("UnityEngine", "Camera", "get_worldToCameraMatrix", 0, 0x53BA1E0);
    methods.cameraProjection = Guard("UnityEngine", "Camera", "get_projectionMatrix", 0, 0x53B8DF0);
    methods.cameraRect = Guard("UnityEngine", "Camera", "get_pixelRect", 0, 0x53B8AA0);
    methods.gpuProjection = Guard("UnityEngine", "GL", "GetGPUProjectionMatrix", 2, 0x53CFA60);
    methods.getGlobalMatrix = Guard("UnityEngine", "Shader", "GetGlobalMatrix", 1, 0x53F6BE0);
    methods.setGlobalMatrix = Guard("UnityEngine.Rendering", "CommandBuffer", "SetGlobalMatrix", 2, 0x5462620);
    methods.fullscreenMesh = Guard("UnityEngine.Rendering.Universal", "RenderingUtils", "get_fullscreenMesh", 0, 0x5210E80,
        "Unity.RenderPipelines.Universal.Runtime.dll");
    methods.listClass = api.class_from_type(api.method_get_param(methods.sharedMaterials, 0));
    if (!methods.listClass) throw std::runtime_error("Glow material list type unavailable");
    installed = true;
}

Obj RenderGlow(size_t slotIndex, Obj camera, const std::vector<Obj>& renderers, ScreenRect rectangle, uint64_t nowMs,
               const GlowCoverage* coverage) {
    if (!installed || slotIndex >= MaxSlots || !camera || renderers.empty() || renderers.size() > MaxRenderers || !Valid(rectangle)) return nullptr;
    if (coverage) {
        if (coverage->renderers.size() > MaxRenderers || coverage->rectangles.size() > 64 ||
            (coverage->renderers.empty() && coverage->rectangles.empty())) return nullptr;
        if (std::any_of(coverage->renderers.begin(),coverage->renderers.end(),[](Obj r) { return !r; })) return nullptr;
        for (auto r : coverage->rectangles)
            if (!Valid(r) || r.x < 0 || r.y < 0 || r.x+r.width > 1.0001f || r.y+r.height > 1.0001f) return nullptr;
    }
    auto cameraRect = api.Value<ScreenRect>(api.Invoke(methods.cameraRect, camera));
    auto view = api.Value<Matrix>(api.Invoke(methods.cameraView, camera));
    auto projection = api.Value<Matrix>(api.Invoke(methods.cameraProjection, camera));
    if (!Valid(cameraRect) || !Finite(view) || !Finite(projection)) return nullptr;
    EnsureResources();
    auto draws = Collect(renderers);
    if (draws.empty()) return nullptr; // Nothing drawable this frame: no texture, target skipped.
    auto decorDraws = coverage ? Collect(coverage->renderers) : std::vector<Draw>{};
    auto& slot = resources->slots[slotIndex];
    Resize(slot, rectangle);
    auto probe = PrepareProbe(slotIndex,slot.width,slot.height,nowMs);
    auto cropped = Crop(projection, cameraRect, rectangle);
    auto identity = Identity();
    if (!Finite(cropped)) return nullptr;
    Ref previousTarget(api.Invoke(methods.active, nullptr));
    ShaderMatrices shaderMatrices;
    auto buffer = resources->buffer.get();
    Sampler(slot.maskProperties.get(), "_MainTex", slot.capture.get());
    Sampler(slot.horizontalProperties.get(), "_MainTex", slot.mask.get());
    Sampler(slot.verticalProperties.get(), "_MainTex", slot.output.get());
    Vector4 texels{1.0f / slot.width, 1.0f / slot.height, static_cast<float>(slot.width), static_cast<float>(slot.height)};
    Vector(slot.horizontalProperties.get(), "_MainTex_TexelSize", texels);
    Vector(slot.verticalProperties.get(), "_MainTex_TexelSize", texels);
    // Keep the visible halo roughly the same width as capture resolution changes.
    Float(slot.horizontalProperties.get(), "_Sigma", std::clamp(3.6f * slot.width / rectangle.width, 0.25f, 6.0f));
    Float(slot.verticalProperties.get(), "_Sigma", std::clamp(3.6f * slot.height / rectangle.height, 0.25f, 6.0f));
    Sampler(slot.mixProperties.get(), "_CommonGrayScaleMixTex", slot.mask.get());
    Sampler(slot.mixProperties.get(), "_BlitTexture", slot.capture.get());
    Sampler(slot.presentProperties.get(), "_MainTex", coverage ? slot.capture.get() : slot.output.get());
    if (coverage) {
        Sampler(slot.whiteProperties.get(), "_MainTex", resources->white.get());
        Sampler(slot.coverageProperties.get(), "_MainTex", slot.capture.get());
        Sampler(slot.coverageMixProperties.get(), "_CommonGrayScaleMixTex", slot.mask.get());
        Sampler(slot.coverageMixProperties.get(), "_BlitTexture", slot.output.get());
    }
    api.Invoke(methods.clear, buffer);
    Target(buffer, slot.capture.get()); Clear(buffer, true);
    ScreenRect textureRect{0, 0, static_cast<float>(slot.width), static_cast<float>(slot.height)};
    api.Invoke(methods.viewport, buffer, {&textureRect});
    shaderMatrices.Set(buffer,view,cropped);
    int pass = 0, submesh = 0, triangles = 0, vertices = 3, instances = 1;
    // Pass0 is the game's ordinary unlit draw. -1 would render its outline and
    // other passes too. Original source materials and property blocks are read.
    for (auto& draw : draws)
        api.Invoke(methods.drawRenderer, buffer, {draw.renderer.get(), draw.material.get(), &draw.submesh, &pass});
    if (probe) {
        try {
            auto source = Identifier(slot.capture.get()), destination = Identifier(probe->snapshot.get());
            api.Invoke(probeMethods.copy, buffer, {&source,&destination});
        } catch (const std::exception& error) {
            // Keep the snapshot alive until execution, even on a recording error.
            Log(std::string("[DEBUG:glow-pixels] copy-error: ") + error.what());
            Destroy(probe->cpu);
        }
    }
    Target(buffer, slot.mask.get()); Clear(buffer);
    api.Invoke(methods.viewport, buffer, {&textureRect});
    shaderMatrices.Set(buffer,identity,identity);
    api.Invoke(methods.drawMesh, buffer, {resources->fullscreen.get(), &identity, slot.maskMaterial.get(), &submesh, &pass, slot.maskProperties.get()});
    // Native UI blur has private texel/sigma parameters, unlike the game's
    // world-outline shaders which depend on inherited camera globals.
    Target(buffer, slot.output.get()); Clear(buffer);
    api.Invoke(methods.viewport, buffer, {&textureRect});
    api.Invoke(methods.drawMesh, buffer, {resources->fullscreen.get(), &identity, slot.horizontalMaterial.get(), &submesh, &pass, slot.horizontalProperties.get()});
    Target(buffer, slot.capture.get()); Clear(buffer);
    api.Invoke(methods.viewport, buffer, {&textureRect});
    int verticalPass = 1;
    api.Invoke(methods.drawMesh, buffer, {resources->fullscreen.get(), &identity, slot.verticalMaterial.get(), &submesh, &verticalPass, slot.verticalProperties.get()});
    Target(buffer, slot.output.get()); Clear(buffer);
    api.Invoke(methods.viewport, buffer, {&textureRect});
    // Exact installed pixel shader: rgba = blurredMask.r * (1 - originalMask.r).
    // Interior is transparent. The UI tints this white outer glow with amber.
    api.Invoke(methods.drawProcedural, buffer, {&identity, slot.mixMaterial.get(), &pass, &triangles, &vertices, &instances, slot.mixProperties.get()});
    if (coverage) {
        // The blur intermediate is now free. Capture only verified foreground
        // decor, using its actual geometry/texture alpha, into a fresh depth buffer.
        Target(buffer, slot.capture.get()); Clear(buffer, true);
        api.Invoke(methods.viewport, buffer, {&textureRect});
        shaderMatrices.Set(buffer,view,cropped);
        for (auto& draw : decorDraws)
            api.Invoke(methods.drawRenderer, buffer, {draw.renderer.get(), draw.material.get(), &draw.submesh, &pass});
        // Opaque HUD rectangles form a union with decor coverage. Each rectangle
        // is a viewport, never a replacement for a wall's actual silhouette.
        shaderMatrices.Set(buffer,identity,identity);
        for (auto r : coverage->rectangles) {
            ScreenRect viewport{r.x*slot.width,r.y*slot.height,r.width*slot.width,r.height*slot.height};
            api.Invoke(methods.viewport, buffer, {&viewport});
            api.Invoke(methods.drawMesh, buffer, {resources->fullscreen.get(), &identity, slot.maskMaterial.get(), &submesh, &pass, slot.whiteProperties.get()});
        }
        Target(buffer, slot.mask.get()); Clear(buffer);
        api.Invoke(methods.viewport, buffer, {&textureRect});
        api.Invoke(methods.drawMesh, buffer, {resources->fullscreen.get(), &identity, slot.maskMaterial.get(), &submesh, &pass, slot.coverageProperties.get()});
        // Native mix is (1 - mask.r) * glow.r. Invert the coverage in the
        // previous pass to retain glow only where HUD/decor actually covers it.
        Target(buffer, slot.capture.get()); Clear(buffer);
        api.Invoke(methods.viewport, buffer, {&textureRect});
        api.Invoke(methods.drawProcedural, buffer, {&identity, slot.mixMaterial.get(), &pass, &triangles, &vertices, &instances, slot.coverageMixProperties.get()});
    }
    // Dedicated presentation texture is never a blur intermediate or mask.
    Target(buffer, slot.present.get()); Clear(buffer);
    api.Invoke(methods.viewport, buffer, {&textureRect});
    api.Invoke(methods.drawMesh, buffer, {resources->fullscreen.get(), &identity, slot.maskMaterial.get(), &submesh, &pass, slot.presentProperties.get()});
    // Append restoration before executing any GPU work. A recording failure has
    // no rendering side effects. The camera itself is never modified.
    Target(buffer, previousTarget.get());
    api.Invoke(methods.viewport, buffer, {&cameraRect});
    shaderMatrices.Restore(buffer,view,projection);
    try {
        api.Invoke(methods.execute, nullptr, {buffer});
        api.Invoke(methods.setActive, nullptr, {previousTarget.get()});
        if (probe && probe->cpu.get()) InspectPixels(*probe,slot,previousTarget.get());
    } catch (...) {
        try { api.Invoke(methods.setActive, nullptr, {previousTarget.get()}); } catch (...) {}
        // Best effort restore if the native execute failed after starting work.
        try {
            api.Invoke(methods.clear, buffer);
            Target(buffer, previousTarget.get());
            api.Invoke(methods.viewport, buffer, {&cameraRect});
            shaderMatrices.Restore(buffer,view,projection);
            api.Invoke(methods.execute, nullptr, {buffer});
        } catch (...) {}
        throw;
    }
    return slot.present.get();
}

void ReleaseGlowSlot(size_t slot) {
    if (resources && slot < MaxSlots) ClearSlot(resources->slots[slot]);
}
void ResetGlowRender() {
    if (!resources) return;
    for (auto& slot : resources->slots) ClearSlot(slot);
    if (resources->buffer.get()) { try { api.Invoke(methods.releaseBuffer, resources->buffer.get()); } catch (...) {} }
    Destroy(resources->maskMaterial); Destroy(resources->blurMaterial); Destroy(resources->mixMaterial);
    // fullscreen is owned by Unity's shared URP cache, and materialList is managed.
    resources.reset();
}
}
