#include "chat-tp-links.hpp"
#include "chat-soul-status.hpp"
#include "chat-coordinate-policy.hpp"
#include "module.hpp"
#include <algorithm>
#include <cstddef>
#include <memory>
#include <set>
#include <string_view>
#include <vector>

namespace chat_tp_links {
using namespace managed;
constexpr const char* Space = "Core.UILogic.Components.Hyperlink";
constexpr size_t MaxLabels = 256, MaxTargets = 16;
constexpr std::string_view Separator = " · ";
struct Color { float r, g, b, a; };
// Pinned native constructor copies 20 bytes for Color?: presence byte, padding,
// then RGBA. Keep it unboxed: il2cpp_object_new(Nullable<T>) allocates T instead.
struct NullableColor {
    uint8_t hasValue = 0;
    uint8_t padding[3]{};
    Color value{};
};
static_assert(sizeof(NullableColor) == 20 && offsetof(NullableColor, value) == 4);
enum class LinkPart { Travel, Inventory, Ocre };
constexpr LinkPart LinkParts[] = {LinkPart::Travel, LinkPart::Inventory, LinkPart::Ocre};
struct Link { Ref manager; ChatTpTarget target; std::string sourceId; int monster = 0; std::string caption; LinkPart part; };
// A native position the parts follow: map link, shared monster group, or
// plain coordinates converted for TP.
struct Source { std::string id; ChatTpTarget target; int monster = 0; };
struct Label {
    // clean holds the native content: no part, plain coordinates still text.
    Ref element, manipulator, clean, enhanced, rendered, cached;
    std::vector<Link> links;
    std::vector<Source> sources;
    bool coordinates = false; // The clean text holds plain coordinates TP can convert.
    std::string signature; // Signature (below) when enhanced was built.
};
struct State {
    bool installed = false, restoring = false, failed = false;
    uint64_t serial = 0;
    std::vector<std::unique_ptr<Label>> labels;
    Klass mapClass = nullptr, groupClass = nullptr, resultClass = nullptr, tooltipClass = nullptr, questLinkClass = nullptr;
    Ref stringType;
    NullableColor linkColor, hoverColor;
    Method getBinding = nullptr, addText = nullptr, addLink = nullptr;
    Method createArray = nullptr, setArray = nullptr, pointerOut = nullptr, setTarget = nullptr;
    Method parseMapLinks = nullptr;
    Method tooltipPosition = nullptr, tooltipCtor = nullptr;
    Method questCtor = nullptr, questOpen = nullptr;
    void (*clicked)(ChatTpTarget) = nullptr;
} state;
void InitializeColors(Method ctor) {
    state.linkColor = {}; state.hoverColor = {};
    try {
        auto nullable = api.class_from_type(api.method_get_param(ctor,5));
        if (!nullable || !api.class_is_valuetype(nullable) ||
            api.class_from_type(api.method_get_param(ctor,6)) != nullable)
            throw std::runtime_error("Native hyperlink color type unavailable");
        // Inspect the declared nullable type, never an allocated object that
        // has already been unwrapped to Color. IL2CPP offsets include its header.
        auto present = api.class_get_field_from_name(nullable,"hasValue");
        auto value = api.class_get_field_from_name(nullable,"value");
        constexpr size_t header = 2 * sizeof(void*);
        if (!present || !value || api.field_get_offset(present) != header ||
            api.field_get_offset(value) != header + offsetof(NullableColor,value))
            throw std::runtime_error("Native nullable color layout unavailable");
        state.linkColor = {1,{}, {0.30f,0.85f,1.0f,1.0f}};
        state.hoverColor = {1,{}, {0.72f,0.95f,1.0f,1.0f}};
    } catch (const std::exception& error) {
        // The exact-client constructor ABI is pinned before this optional
        // styling step. Empty nullable values preserve native theme colors.
        Log(std::string("CHAT_TP_COLOR_FALLBACK ") + error.what());
    }
}
using BindFn = void(*)(Obj, Obj, int, Method);
using TextFn = void(*)(Obj, Obj, Method);
using DecodeFn = bool(*)(Obj, Obj, Obj*, Obj, Method);
struct Vec2 { float x, y; };
using ClickFn = void(*)(Obj, Vec2, Method);
using TooltipFn = Obj(*)(Obj, Vec2, Method);
BindFn originalBind = nullptr;
TextFn originalText = nullptr;
DecodeFn originalDecode = nullptr;
ClickFn originalClick = nullptr;
TooltipFn originalTooltip = nullptr;
thread_local Label* decoding = nullptr;
bool Active() {
    return state.installed && !state.failed && !state.restoring && module && module->ready.load() &&
        module->servicesReady && !module->stopped && !module->stopRequested.load();
}
// « Chat » options of the mod's tab, read live: a disabled part is never
// appended, and one already shown leaves on the label's next refresh.
bool Enabled(LinkPart part) {
    const auto& settings = module->settings;
    return part == LinkPart::Travel ? settings.chatTravelLinks :
        part == LinkPart::Inventory ? settings.chatSoulInventory : settings.chatOcreStatus;
}
bool Owned(Obj manager) {
    return manager && std::any_of(state.labels.begin(),state.labels.end(),[&](const auto& label) {
        return std::any_of(label->links.begin(),label->links.end(),[&](const Link& link) { return link.manager.get() == manager; });
    });
}
bool AddedTp(Obj manager) {
    if (!manager) return false;
    if (Owned(manager)) return true;
    if (api.object_get_class(manager)!=state.mapClass) return false;
    Ref data(api.Call(manager,"get_m_dataContext"));
    const auto marker=api.String(api.Call(data.get(),"get_elementName"));
    constexpr std::string_view prefix="codex-tp-";
    if (!marker.starts_with(prefix)) return false;
    const std::string_view suffix=std::string_view(marker).substr(prefix.size());
    const auto separator=suffix.find('-');
    if (separator==std::string_view::npos) return false;
    const auto digits=[](std::string_view part) {
        return !part.empty() && part.size()<=20 && std::all_of(part.begin(),part.end(),[](char c){return c>='0' && c<='9';});
    };
    // Parameter3 survives native reconstruction; the wrapper id hashes the
    // complete protocol and is not this marker. Never identify by visible "TP".
    return digits(suffix.substr(0,separator)) && digits(suffix.substr(separator+1));
}
Label* Find(Obj element) {
    for (const auto& label : state.labels) if (label->element.get() == element) return label.get();
    return nullptr;
}
void Register(Obj element) {
    if (!element || Find(element) || state.labels.size() >= MaxLabels) return;
    auto label = std::make_unique<Label>();
    label->element.reset(element); label->cached.reset(module->cached.get());
    state.labels.push_back(std::move(label));
}
void ClearOwnedHover(Label& label) {
    if (!label.manipulator.get()) return;
    Ref manager(api.Field(label.manipulator.get(),"m_hoveredHyperlinkManager"));
    if (!AddedTp(manager.get())) return;
    if (api.Field(label.manipulator.get(),"m_textElementTarget"))
        api.Invoke(state.pointerOut,label.manipulator.get(),{nullptr});
    else {
        api.Call(manager.get(),"OnRollOutCallback");
        *static_cast<bool*>(api.FieldAddress(label.manipulator.get(),"m_isHovered")) = false;
        api.gc_wbarrier_set_field(label.manipulator.get(),
            static_cast<Obj*>(api.FieldAddress(label.manipulator.get(),"m_hoveredHyperlinkManager")),nullptr);
    }
}
void Restore(Label& label) {
    if (!label.clean.get() || !label.manipulator.get()) return;
    ClearOwnedHover(label);
    if (api.Field(label.manipulator.get(),"m_textElementTarget") != label.element.get()) return;
    Ref text(api.Call(label.clean.get(),"BuildText",{nullptr}));
    // TryDecodeText can return without assigning when input already equals
    // BuildText. Restore its native result first, then the native text setter.
    api.gc_wbarrier_set_field(label.manipulator.get(),
        static_cast<Obj*>(api.FieldAddress(label.manipulator.get(),"m_hyperlinkResult")),label.clean.get());
    api.Invoke(state.setTarget,label.manipulator.get(),{text.get()});
    label.clean.reset(); label.enhanced.reset(); label.rendered.reset(); label.links.clear();
}
void BindHook(Obj view, Obj element, int index, Method method) {
    if (Active()) {
        try {
            Ref binding(api.Invoke(state.getBinding,view,{element}));
            Register(api.Call(binding.get(),"get_content"));
            Register(api.Call(binding.get(),"get_subContent"));
        } catch (const std::exception& e) { Log(std::string("CHAT_TP_BIND_ERROR ") + e.what()); }
    }
    originalBind(view,element,index,method);
}
void TextHook(Obj manipulator, Obj text, Method method) {
    Label* label = nullptr;
    if (Active()) {
        try {
            label = Find(api.Field(manipulator,"m_textElementTarget"));
            if (label && label->cached.get() == module->cached.get()) label->manipulator.reset(manipulator);
            else label = nullptr;
        } catch (const std::exception& e) { Log(std::string("CHAT_TP_SCOPE_ERROR ") + e.what()); }
    }
    const auto previous = decoding; decoding = label;
    struct Scope { Label* prior; ~Scope() { decoding = prior; } } scope{previous};
    originalText(manipulator,text,method);
}
struct Token { int type; int padding; Obj text; Obj manager; };
static_assert(sizeof(Token) == 24);
struct Piece { Ref text, manager; };
// Native content of a decode result: this module's links, current or left in
// serialized text by an earlier revision, are dropped with their separators.
bool ReadPieces(Obj result, std::vector<Piece>& pieces, std::set<std::string>& ids) {
    Ref sequence(api.Call(result,"get_m_sequence"));
    const int count = api.Value<int>(api.Call(sequence.get(),"get_Count"));
    if (count < 0 || count > 2048) return false;
    for (int i = 0; i < count; ++i) {
        Ref boxed(api.Call(sequence.get(),"get_Item",{&i}));
        const auto token = api.Value<Token>(boxed.get());
        if (token.type == 1 && AddedTp(token.manager)) {
            if (!pieces.empty() && pieces.back().text.get()) {
                auto text = api.String(pieces.back().text.get());
                if (text.ends_with(Separator)) {
                    text.resize(text.size() - Separator.size());
                } else if (!text.empty() && text.back() == ' ') {
                    // R63 and earlier added a plain space before their combined link.
                    text.pop_back();
                }
                if (text.empty()) pieces.pop_back();
                else pieces.back().text.reset(api.string_new(text.c_str()));
            }
            continue;
        }
        if (token.type == 0) pieces.push_back({Ref(token.text),{}});
        else if (token.type == 1 && token.manager) {
            pieces.push_back({{},Ref(token.manager)});
            ids.insert(api.String(api.Call(token.manager,"get_id")));
        } else return false;
        if (pieces.size() > 4096) return false;
    }
    return true;
}
// Plain coordinates become native map links, for TP only; without TP they stay
// text. Only plain text tokens go through the native coordinate parser;
// item/spell/URL and existing map hyperlink payloads are left intact.
bool Convert(std::vector<Piece>& clean, std::vector<Piece>& pieces, std::set<std::string>& ids,
    Obj service, Method decodeMethod, bool& coordinates) {
    const bool travel = Enabled(LinkPart::Travel);
    for (auto& piece : clean) {
        const auto text = piece.manager.get() || !piece.text.get() ? std::string{} : api.String(piece.text.get());
        const auto spans = ChatCoordinateSpans(text);
        coordinates = coordinates || !spans.empty();
        if (spans.empty() || !travel) { pieces.push_back(std::move(piece)); continue; }
        size_t offset = 0;
        auto literal = [&](size_t end) {
            if (end > offset) pieces.push_back({Ref(api.string_new(text.substr(offset,end-offset).c_str())),{}});
        };
        for (const auto span : spans) {
            literal(span.start);
            Ref candidate(api.string_new(text.substr(span.start,span.length).c_str())); Obj parsed = nullptr;
            // The native helper also strips malformed brackets; only
            // exact signed-integer coordinate pairs may enter it.
            if (!api.Value<bool>(api.Invoke(state.parseMapLinks,nullptr,{candidate.get(),&parsed})) || !parsed) return false;
            Ref parsedText(parsed); Obj decoded = nullptr;
            if (!originalDecode(service,parsedText.get(),&decoded,nullptr,decodeMethod) || !decoded) return false;
            Ref nested(decoded);
            if (!ReadPieces(nested.get(),pieces,ids)) return false;
            offset = span.start + span.length;
        }
        literal(text.size());
        if (pieces.size() > 4096) return false;
    }
    return true;
}
std::string Id(Obj manager) {
    Ref data(api.Call(manager,"get_m_dataContext"));
    return api.String(api.Call(data.get(),"get_id"));
}
ChatTpTarget Target(Obj manager) {
    Ref data(api.Call(manager,"get_m_dataContext"));
    const bool group = api.object_get_class(manager)==state.groupClass;
    return {api.Value<int>(api.Call(data.get(),group?"get_x":"get_posX")),api.Value<int>(api.Call(data.get(),group?"get_y":"get_posY")),
        api.Value<int>(api.Call(data.get(),"get_worldMapId"))};
}
ChatSoulPart SoulPart(LinkPart part) {
    return part == LinkPart::Inventory ? ChatSoulPart::Inventory : ChatSoulPart::Ocre;
}
std::string Caption(int monster, LinkPart part) {
    if (part == LinkPart::Travel) return "TP";
    const auto status = ChatSoulCaption(monster, SoulPart(part), true);
    // Scope status colors to their own native hover area; TP keeps its cyan.
    return status.empty() ? std::string{} : "<color=#D7CFB8>" + status + "</color>";
}
// What a label shows for the options and statuses of now: TP's conversion of
// plain coordinates, then each enabled part's caption for every source.
std::string Signature(const Label& label) {
    std::string signature(label.coordinates && Enabled(LinkPart::Travel) ? "P" : "-");
    for (const auto& source : label.sources) for (const auto part : LinkParts) {
        if (Enabled(part)) signature += Caption(source.monster, part);
        signature += '\x1F';
    }
    return signature;
}
// A status or a « Chat » option changed since the label was built.
bool Stale(const Label& label) { return Signature(label) != label.signature; }
// The label shows a text this module did not build: drop what belonged to its
// previous text, so neither a refresh nor a cleanup can bring that text back.
void Forget(Label& label) {
    try { ClearOwnedHover(label); } catch (const std::exception& e) { Log(std::string("CHAT_TP_HOVER_ERROR ") + e.what()); }
    label.clean.reset(); label.enhanced.reset(); label.rendered.reset();
    label.links.clear(); label.sources.clear(); label.coordinates = false; label.signature.clear();
}
Obj NewLink(Obj source, ChatTpTarget target, const std::string& caption) {
    int count = 6;
    Ref parameters(api.Invoke(state.createArray,nullptr,{state.stringType.get(),&count}));
    const std::string values[] = {std::to_string(target.x),std::to_string(target.y),std::to_string(target.world),
        "codex-tp-" + std::to_string(GetTickCount64()) + "-" + std::to_string(++state.serial),"True","False"};
    for (int i = 0; i < count; ++i) {
        Ref value(api.string_new(values[i].c_str()));
        api.Invoke(state.setArray,parameters.get(),{value.get(),&i});
    }
    Ref text(api.string_new(caption.c_str()));
    return api.New(state.mapClass,{text.get(),parameters.get(),api.Field(source,"m_bus"),
        api.Field(source,"m_tooltipService"),api.Field(source,"m_themeService"),
        &state.linkColor,&state.hoverColor});
}
bool DecodeHook(Obj service, Obj input, Obj* output, Obj previous, Method method) {
    try {
        if (output && decoding && Active() && decoding->rendered.get() && decoding->enhanced.get() &&
            service==api.Field(decoding->manipulator.get(),"m_hyperlinkService") &&
            api.String(input)==api.String(decoding->rendered.get()) && !Stale(*decoding)) {
            // BuildText emits hashed link IDs resolved through the previous result.
            // Keep the result and owned managers together on an unchanged refresh.
            *output=decoding->enhanced.get();
            api.gc_wbarrier_set_field(decoding->manipulator.get(),
                static_cast<Obj*>(api.FieldAddress(decoding->manipulator.get(),"m_hyperlinkResult")),*output);
            return true;
        }
    } catch (const std::exception& e) { Log(std::string("CHAT_TP_REUSE_ERROR ")+e.what()); }
    const bool success = originalDecode(service,input,output,previous,method);
    if (!decoding || !Active()) return success;
    auto& label = *decoding;
    bool own = false;
    // An unusable text: a new one drops the label's previous content; this
    // module's own text keeps its working links until the next change.
    const auto settle = [&] { if (own) label.signature = Signature(label); else Forget(label); };
    try {
        if (service != api.Field(label.manipulator.get(),"m_hyperlinkService")) return success;
        // A refresh decodes the text this module rendered: rebuild it from the
        // retained native content, where converted coordinates are still text.
        // Native decoding resolves those links to the same managers anyway.
        own = label.clean.get() && label.rendered.get() && api.String(input) == api.String(label.rendered.get());
        if (!success || !output || !*output) { settle(); return success; }
        Ref result(own ? label.clean.get() : *output);
        std::vector<Piece> original, pieces;
        std::set<std::string> ids;
        if (!ReadPieces(result.get(),original,ids)) { settle(); return success; }
        Ref clean(own ? label.clean.get() : api.New(state.resultClass));
        if (!own) for (const auto& piece : original)
            api.Invoke(piece.manager.get() ? state.addLink : state.addText,clean.get(),
                {piece.manager.get() ? piece.manager.get() : piece.text.get()});
        bool coordinates = false;
        if (!Convert(original,pieces,ids,service,method,coordinates)) { settle(); return success; }
        Ref enhanced(api.New(state.resultClass)), separator(api.string_new(Separator.data()));
        std::vector<Link> links;
        std::vector<Source> sources;
        for (const auto& piece : pieces) {
            const auto append = piece.manager.get() ? state.addLink : state.addText;
            api.Invoke(append,enhanced.get(),{piece.manager.get() ? piece.manager.get() : piece.text.get()});
            if (!piece.manager.get() || sources.size() >= MaxTargets) continue;
            const auto kind = api.object_get_class(piece.manager.get());
            // A shared monster group's displayed coordinates are a distinct
            // native hyperlink. Preserve that manager/tooltip and append TP.
            if (kind != state.mapClass && kind != state.groupClass) continue;
            if (!api.Value<bool>(api.Call(piece.manager.get(),"get_isValid")) ||
                !api.Value<bool>(api.Call(piece.manager.get(),"get_isEnabled"))) continue;
            const auto target = Target(piece.manager.get());
            if (target.world < 0) continue;
            const auto sourceId=Id(piece.manager.get());
            int monster = 0;
            if (kind == state.groupClass) {
                Ref data(api.Call(piece.manager.get(),"get_m_dataContext"));
                monster = api.Value<int>(api.Call(data.get(),"get_leaderId"));
            }
            sources.push_back({sourceId,target,monster});
            for (const auto part : LinkParts) {
                const auto caption = Enabled(part) ? Caption(monster, part) : std::string{};
                if (caption.empty()) continue;
                Ref manager;
                for (const auto& existing:label.links) {
                    if (existing.part==part && existing.sourceId==sourceId && existing.target==target && existing.monster==monster && existing.caption==caption && ids.insert(Id(existing.manager.get())).second) {
                        manager.reset(existing.manager.get()); break;
                    }
                }
                for (int attempt = 0; !manager.get() && attempt < 4; ++attempt) {
                    manager.reset(NewLink(piece.manager.get(),target,caption));
                    if (ids.insert(Id(manager.get())).second) break;
                    manager.reset();
                }
                if (!manager.get()) continue;
                api.Invoke(state.addText,enhanced.get(),{separator.get()});
                api.Invoke(state.addLink,enhanced.get(),{manager.get()});
                links.push_back({std::move(manager),target,sourceId,monster,caption,part});
            }
        }
        Ref rendered(api.Call(enhanced.get(),"BuildText",{nullptr}));
        ClearOwnedHover(label);
        label.clean = std::move(clean);
        label.enhanced = std::move(enhanced);
        label.rendered = std::move(rendered);
        label.links = std::move(links);
        label.sources = std::move(sources);
        label.coordinates = coordinates;
        label.signature = Signature(label);
        *output = label.enhanced.get();
        // Native TryDecodeText returns early when BuildText equals the input;
        // it would otherwise leave different managers bound to identical text.
        api.gc_wbarrier_set_field(label.manipulator.get(),
            static_cast<Obj*>(api.FieldAddress(label.manipulator.get(),"m_hyperlinkResult")),*output);
    } catch (const std::exception& e) {
        Log(std::string("CHAT_TP_DECODE_ERROR ") + e.what());
        try { settle(); } catch (const std::exception& retry) { Log(std::string("CHAT_TP_DECODE_ERROR ") + retry.what()); }
    }
    return success;
}
void OpenOcreQuest(Obj manager, Vec2 position) {
    // Use the game's ShowQuest link: it sends the native open-book event with
    // QuestData439 and selects it through the journal's normal lifecycle.
    Ref source(manager), bus(api.Field(source.get(),"m_bus"));
    Ref tooltipService(api.Field(source.get(),"m_tooltipService")), themeService(api.Field(source.get(),"m_themeService"));
    if (!bus.get() || !tooltipService.get() || !themeService.get()) throw std::runtime_error("Quest link services unavailable");
    int count = 1, index = 0;
    Ref parameters(api.Invoke(state.createArray,nullptr,{state.stringType.get(),&count}));
    Ref questId(api.string_new("439")), title(api.string_new("L’éternelle moisson"));
    api.Invoke(state.setArray,parameters.get(),{questId.get(),&index});
    NullableColor nativeColor{};
    Ref nativeLink(api.object_new(state.questLinkClass));
    if (!nativeLink.get()) throw std::runtime_error("Quest link allocation failed");
    api.Invoke(state.questCtor,nativeLink.get(),{title.get(),parameters.get(),bus.get(),tooltipService.get(),themeService.get(),&nativeColor,&nativeColor});
    api.Invoke(state.questOpen,nativeLink.get(),{&position});
    Log("CHAT_OCRE_OPEN requested quest=439");
}
void ClickHook(Obj manager, Vec2 position, Method method) {
    for (const auto& label : state.labels) for (const auto& link : label->links) {
        if (link.manager.get() != manager) continue;
        if (link.part == LinkPart::Inventory) return;
        try {
            // A part whose option was just unchecked stays inert until it leaves.
            if (Active() && Enabled(link.part) && label->cached.get() == module->cached.get() && label->element.get() &&
                api.Call(label->element.get(),"get_panel")) {
                if (link.part == LinkPart::Ocre) OpenOcreQuest(manager,position);
                else if (state.clicked) {
                    Log("CHAT_TP_CLICK x="+std::to_string(link.target.x)+" y="+std::to_string(link.target.y)+" world="+std::to_string(link.target.world));
                    state.clicked(link.target);
                }
            }
        } catch (const std::exception& e) { Log(std::string("CHAT_TP_CLICK_ERROR ") + e.what()); }
        return;
    }
    try { if (AddedTp(manager)) return; } // A removed/rebuilt mod link is not a native map action.
    catch (const std::exception& e) { Log(std::string("CHAT_TP_STALE_CLICK_ERROR ")+e.what()); }
    originalClick(manager,position,method);
}
Obj TooltipHook(Obj manager, Vec2 mousePosition, Method method) {
    try {
        for (const auto& label : state.labels) for (const auto& link : label->links) {
            if (link.manager.get() != manager) continue;
            if (!Active() || !Enabled(link.part) || label->cached.get() != module->cached.get() ||
                !api.Call(label->element.get(),"get_panel")) return nullptr;
            const auto target=link.target;
            auto text=link.part != LinkPart::Travel ? ChatSoulDetail(link.monster,SoulPart(link.part)) :
                module->InCombat() ? std::string("TP indisponible pendant un combat.") :
                "TP vers le zaap le plus proche, puis trajet vers ["+std::to_string(target.x)+","+std::to_string(target.y)+"].";
            if (link.part == LinkPart::Ocre) text += "\nClique pour ouvrir L’éternelle moisson dans le journal de quêtes.";
            Ref caption(api.string_new(text.c_str())), empty(api.string_new(""));
            Ref position(api.Invoke(state.tooltipPosition,manager,{&mousePosition}));
            Ref builder(api.object_new(state.tooltipClass));
            int style=0; bool front=false,rich=false,tracking=false;
            api.Invoke(state.tooltipCtor,builder.get(),{caption.get(),position.get(),empty.get(),&style,&front,&rich,&tracking});
            return builder.get();
        }
        if (AddedTp(manager)) return nullptr; // Retired mod action must not advertise native follow behavior.
    } catch (const std::exception& e) { Log(std::string("CHAT_TP_TOOLTIP_ERROR ")+e.what()); return nullptr; }
    return originalTooltip(manager,mousePosition,method);
}
}

void InstallChatTpLinks(void (*clicked)(ChatTpTarget)) {
    using namespace chat_tp_links;
    state.clicked = clicked;
    if (state.installed) return;
    auto chat = api.Class("Core.dll","Core.UILogic.Chat","ChatView");
    state.getBinding = api.FindVerified(chat,"GetBinding",1,0x2D91870);
    auto binding = api.Class("Core.dll","Core.UILogic.Bindings","ChatItemBinding");
    api.FindVerified(binding,"get_content",0,0x2EB8790);
    api.FindVerified(binding,"get_subContent",0,0x2EB89A0);
    state.mapClass = api.Class("Core.dll",Space,"HyperlinkMapInformation");
    state.groupClass = api.Class("Core.dll",Space,"HyperlinkLocalizeMonsterGroup");
    api.FindVerified(state.groupClass,"get_m_dataContext",0,0x68D2C0);
    api.FindVerified(state.mapClass,"get_m_dataContext",0,0x68D2C0);
    auto ctor = api.FindVerified(state.mapClass,".ctor",7,0x168FA80);
    state.questLinkClass = api.Class("Core.dll",Space,"HyperlinkShowQuest");
    state.questCtor = api.FindVerified(state.questLinkClass,".ctor",7,0x16A6E70);
    state.questOpen = api.FindVerified(state.questLinkClass,"OnHyperlinkLeftClick",1,0x16A6C60);
    state.parseMapLinks = api.FindVerified(state.mapClass,"TryParseMapLinks",2,0x168EBE0);
    InitializeColors(ctor);
    auto wrapper = api.NestedClass(state.mapClass,"MapHyperlinkTextWrapper");
    api.FindVerified(api.Class("Core.dll",Space,"BaseHyperlinkTextWrapper"),"get_id",0,0x68D2B0);
    api.FindVerified(wrapper,"get_posX",0,0x753E30);
    api.FindVerified(wrapper,"get_posY",0,0xAD8DF0);
    api.FindVerified(wrapper,"get_worldMapId",0,0x9CD220);
    api.FindVerified(wrapper,"get_elementName",0,0x748D50);
    auto groupWrapper=api.NestedClass(state.groupClass,"LocalizeMonsterGroupHyperlinkTextWrapper");
    api.FindVerified(groupWrapper,"get_x",0,0x753E30);
    api.FindVerified(groupWrapper,"get_y",0,0xAD8DF0);
    api.FindVerified(groupWrapper,"get_worldMapId",0,0x9CD220);
    api.FindVerified(groupWrapper,"get_leaderId",0,0x956730);
    state.tooltipPosition=api.FindVerified(state.mapClass,"CreateTooltipPosition",1,0x3CA37C0);
    state.tooltipClass=api.Class("Core.dll","Core.UILogic.Components.Tooltips.Builder","TextTooltipBuilder");
    state.tooltipCtor=api.FindVerified(state.tooltipClass,".ctor",7,0x164A2F0);
    state.resultClass = api.Class("Core.dll",Space,"HyperlinkDecodeResult");
    api.FindVerified(state.resultClass,".ctor",0,0x167FF30);
    api.FindVerified(state.resultClass,"get_m_sequence",0,0x68D2C0);
    api.FindVerified(state.resultClass,"BuildText",1,0x167ECE0);
    state.addText = api.FindVerified(state.resultClass,"AddToSequence",1,0x167EC50);
    state.addLink = api.FindVerified(state.resultClass,"AddToSequence",1,0x167EBA0);
    auto array = api.Class("mscorlib.dll","System","Array");
    state.createArray = api.FindVerified(array,"CreateInstance",2,0x43005E0);
    state.setArray = api.FindVerified(array,"SetValue",2,0x4302FD0);
    state.stringType.reset(api.type_get_object(api.class_get_type(api.Class("mscorlib.dll","System","String"))));
    auto manipulator = api.Class("Core.dll",Space,"HyperlinkManipulator");
    state.pointerOut = api.FindVerified(manipulator,"OnPointerOutLinkTagEvent",1,0x168C810);
    state.setTarget = api.FindVerified(manipulator,"set_m_targetText",1,0x168DF30);
    AddHook(api.Find(chat,"BindItem",2),0x2D8E4C0,reinterpret_cast<void*>(&BindHook),reinterpret_cast<void**>(&originalBind));
    AddHook(api.Find(manipulator,"TryDecodeText",1),0x168D990,reinterpret_cast<void*>(&TextHook),reinterpret_cast<void**>(&originalText));
    AddHook(api.Find(api.Class("Core.dll",Space,"HyperlinkService"),"TryDecode",3),0x169CE90,
        reinterpret_cast<void*>(&DecodeHook),reinterpret_cast<void**>(&originalDecode));
    AddHook(api.Find(state.mapClass,"OnHyperlinkLeftClick",1),0x168E880,reinterpret_cast<void*>(&ClickHook),reinterpret_cast<void**>(&originalClick));
    AddHook(api.Find(state.mapClass,"CreateTooltipBuilder",1),0x168E760,reinterpret_cast<void*>(&TooltipHook),reinterpret_cast<void**>(&originalTooltip));
    state.installed = true;
}
void TickChatTpLinks() {
    using namespace chat_tp_links;
    if (!Active()) { RestoreChatTpLinks(); return; }
    state.restoring = true;
    for (auto i = state.labels.begin(); i != state.labels.end();) {
        try {
            if ((*i)->cached.get() != module->cached.get() || !api.Call((*i)->element.get(),"get_panel")) {
                Restore(**i); i = state.labels.erase(i); continue;
            }
        } catch (const std::exception& e) { Log(std::string("CHAT_TP_CLEANUP_ERROR ") + e.what()); }
        ++i;
    }
    state.restoring = false;
    // Each status reader runs only while a label shows its enabled part: with
    // both unchecked, no inventory or quest data is read at all.
    const auto shown = [](LinkPart part) {
        return Enabled(part) && std::any_of(state.labels.begin(),state.labels.end(),[&](const auto& label) {
            return std::any_of(label->links.begin(),label->links.end(),[&](const Link& link) { return link.part == part; });
        });
    };
    TickChatSoulStatus(shown(LinkPart::Inventory),shown(LinkPart::Ocre));
    // Native text rebuild updates row wrapping as well as the clickable managers.
    // Bound changes per pump; unchanged captions retain their original identities.
    // A status or « Chat » option change adds or removes parts and converts or
    // restores plain coordinates; all three unchecked leave the native text.
    int refreshed = 0;
    for (size_t i = 0; i < state.labels.size() && refreshed < 4; ++i) {
        auto& label = *state.labels[i];
        if (!label.rendered.get() || !label.manipulator.get() || !Stale(label)) continue;
        try {
            // TextHook decodes for the manipulator's current element: never
            // rebuild this text into another row.
            if (api.Field(label.manipulator.get(),"m_textElementTarget") != label.element.get()) continue;
        } catch (const std::exception& e) { Log(std::string("CHAT_TP_REFRESH_ERROR ") + e.what()); continue; }
        Ref text(label.rendered.get()), manipulator(label.manipulator.get());
        TextHook(manipulator.get(),text.get(),nullptr); ++refreshed;
    }
}
void RestoreChatTpLinks() {
    using namespace chat_tp_links;
    state.restoring = true;
    for (auto i = state.labels.begin(); i != state.labels.end();) {
        try { Restore(**i); i = state.labels.erase(i); }
        catch (const std::exception& e) { Log(std::string("CHAT_TP_CLEANUP_ERROR ") + e.what()); ++i; }
    }
    if (state.labels.empty() && module && (module->stopped || module->stopRequested.load())) {
        state.stringType.reset(); state.linkColor = {}; state.hoverColor = {};
    }
    state.restoring = false;
    RestoreChatSoulStatus();
}
bool ChatTpLinksCleanupPending() { return !chat_tp_links::state.labels.empty(); }
