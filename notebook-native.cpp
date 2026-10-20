#include "notebook-native.hpp"
#include "module.hpp"
#include "chat-travel.hpp"
#include <cstring>

namespace notebook {
namespace {
using namespace managed;
Method currentMap{}, mapInfo{}, subarea{}, world{}, inputGetter{}, lengthValid{}, append{};
Klass characterClass{}, windowsClass{}, chatClass{}, inputClass{}, bindingClass{};
void Ready() {
    if (!module || !module->ready || !module->servicesReady || module->stopped || module->stopRequested)
        throw std::runtime_error("Personnage indisponible.");
}
void Field(Klass type, const char* name, size_t offset) {
    const auto field = api.class_get_field_from_name(type, name);
    if (!field || api.field_get_offset(field) != offset) throw std::runtime_error("Notebook native field unavailable");
}
}
void InstallNative() {
    characterClass = api.Class("Core.dll", "", "esg");
    windowsClass = api.Class("Core.dll", "", "ghz");
    chatClass = api.Class("Core.dll", "Core.UILogic.Chat", "ChatView");
    inputClass = api.Class("Core.dll", "Core.UILogic.Components.Figma", "TextInput");
    bindingClass = api.Class("Core.dll", "Core.UILogic.Bindings", "ChatviewBinding");
    Field(windowsClass, "dyvx", 0x258);
    currentMap = api.FindVerified(characterClass, "zeo", 0, 0x185C1E0);
    constexpr const char* image = "Ankama.Dofus.Core.DataCenter.dll";
    constexpr const char* space = "Core.DataCenter.Metadata.World";
    auto info = api.Class(image, space, "MapInformationData");
    Field(info, "posX", 0x18); Field(info, "posY", 0x1A);
    Field(api.Class(image, space, "WorldPoint"), "m_mapId", 0x10);
    mapInfo = api.FindVerified(info, "GetMapInformationById", 1, 0x7C4200);
    subarea = api.FindVerified(info, "get_subArea", 0, 0x7C44B0);
    world = api.FindVerified(api.Class(image, space, "SubAreaData"), "get_worldmapId", 0, 0x7D1040);
    inputGetter = api.FindVerified(bindingClass, "get_ti_chatInput", 0, 0x2EB8FD0);
    lengthValid = api.FindVerified(chatClass, "IsTextLengthValid", 1, 0x2D92630);
    append = api.FindVerified(chatClass, "AppendTextToInputAndFocus", 1, 0x2D8E010);
}
Entry CurrentPosition() {
    Ready();
    Ref compositor(module->compositor.get()), character(module->FindService("fng"));
    if (!character.get() || api.object_get_class(character.get()) != characterClass)
        throw std::runtime_error("Position actuelle indisponible.");
    Ref location(api.Invoke(currentMap, character.get()));
    if (!location.get()) throw std::runtime_error("Position actuelle indisponible.");
    auto map = managed::Read<int64_t>(location.get(), 0x10);
    if (map <= 0) throw std::runtime_error("Carte actuelle indisponible.");
    Ref info(api.Invoke(mapInfo, nullptr, {&map}));
    if (!info.get()) throw std::runtime_error("Cette carte ne possède pas de coordonnées publiques.");
    Ref area(api.Invoke(subarea, info.get()));
    if (!area.get()) throw std::runtime_error("Monde de cette carte indisponible.");
    Entry result;
    result.map = map; result.x = managed::Read<int16_t>(info.get(), 0x18); result.y = managed::Read<int16_t>(info.get(), 0x1A);
    result.world = api.Value<int>(api.Invoke(world, area.get()));
    Ready();
    Ref after(api.Invoke(currentMap, character.get()));
    if (module->compositor.get() != compositor.get() || module->FindService("fng") != character.get() ||
        !after.get() || managed::Read<int64_t>(after.get(), 0x10) != map)
        throw std::runtime_error("La carte a changé. Réessaie.");
    return result;
}
void Travel(const Entry& entry) {
    Ready();
    if (module->InCombat()) throw std::runtime_error("Trajet indisponible pendant un combat.");
    Entry checked = entry; checked.kind = Kind::Position; checked.title = "Position"; checked.body.clear(); Validate(checked);
    StartChatTravel({entry.x, entry.y, entry.world});
}
void PrepareMessage(const std::string& text) {
    Ready();
    Entry checked; checked.kind = Kind::Message; checked.title = "Message"; checked.body = text; Validate(checked);
    Ref compositor(module->compositor.get()), service(module->FindService("fqw"));
    if (!service.get() || api.object_get_class(service.get()) != windowsClass) throw std::runtime_error("Chat indisponible.");
    Ref view(api.Field(service.get(), "dyvx"));
    if (!view.get() || api.object_get_class(view.get()) != chatClass) throw std::runtime_error("Chat indisponible.");
    Ref binding(api.Field(view.get(), "dzyi"));
    if (!binding.get() || api.object_get_class(binding.get()) != bindingClass) throw std::runtime_error("Chat indisponible.");
    Ref input(api.Invoke(inputGetter, api.object_unbox(binding.get())));
    if (!input.get() || api.object_get_class(input.get()) != inputClass || !api.Call(input.get(), "get_panel") ||
        !api.Value<bool>(api.Call(input.get(), "get_enabledInHierarchy")))
        throw std::runtime_error("Ouvre le chat pour préparer le message.");
    Ref message(api.string_new(text.c_str()));
    if (!api.Value<bool>(api.Invoke(lengthValid, view.get(), {message.get()})))
        throw std::runtime_error("Le brouillon du chat est trop long pour ajouter ce message.");
    Ready();
    if (module->compositor.get() != compositor.get() || module->FindService("fqw") != service.get() ||
        api.Field(service.get(), "dyvx") != view.get() || !api.Call(input.get(), "get_panel"))
        throw std::runtime_error("Le chat a changé. Réessaie.");
    // Native append/focus only. The user explicitly sends their draft themselves.
    api.Invoke(append, view.get(), {message.get()});
}
void Copy(const std::string& text) {
    if (text.empty()) throw std::runtime_error("Aucun texte à copier.");
    const int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), nullptr, 0);
    if (count <= 0) throw std::runtime_error("Texte invalide.");
    const auto memory = GlobalAlloc(GMEM_MOVEABLE, (static_cast<size_t>(count) + 1) * sizeof(wchar_t));
    if (!memory) throw std::runtime_error("Copie impossible.");
    auto* content = static_cast<wchar_t*>(GlobalLock(memory));
    if (!content) {GlobalFree(memory); throw std::runtime_error("Copie impossible.");}
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), content, count);
    content[count] = L'\0'; GlobalUnlock(memory);
    const auto window = GetActiveWindow();
    if (!window || !OpenClipboard(window)) {GlobalFree(memory); throw std::runtime_error("Presse-papiers occupé. Réessaie.");}
    const bool copied = EmptyClipboard() && SetClipboardData(CF_UNICODETEXT, memory);
    CloseClipboard();
    if (!copied) {GlobalFree(memory); throw std::runtime_error("Copie impossible.");}
}
}
