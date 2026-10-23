#include "travel/roublard-passage.hpp"
#include "core/module.hpp"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <atomic>
#include <fstream>
#include <limits>
#include <memory>
#include <utility>

namespace roublard_passage {
namespace {
using namespace managed;
using Json = nlohmann::json;
constexpr size_t MaxBytes = 256 * 1024, MaxMaps = 64, MaxCharacters = 256, MaxReplies = 16, MaxPending = 256, MaxPauses = 64;
constexpr int64_t MapLimit = int64_t(1) << 40;
// A run: Bad Aboum is looked for until the map's entities are there, then his
// question, the teleport after the reply and the arrival's loading.
constexpr uint64_t SearchMs = 4000, DialogMs = 8000, TeleportMs = 12000, ArrivalMs = 15000;
// The player's own trip: a map change this soon after a reply is its teleport;
// a map is looked at twice for him, once its scene has settled.
constexpr uint64_t ManualTeleportMs = 20000, ScanDelayMs = 1200;
// A file in use or out of reach: tried again this much later.
constexpr uint64_t StoreRetryMs = 30000;
constexpr int SpeakAction = 3;  // NpcActionData realId 3: « Parler »
bool ValidSide(Side side) { return side == Side::Astrub || side == Side::Temple || side == Side::Amakna; }
int Index(Side side) { return static_cast<int>(side); }
bool ValidMap(int64_t map) { return map > 0 && map < MapLimit; }
bool Listed(const std::vector<int64_t>& maps, int64_t map) { return std::find(maps.begin(), maps.end(), map) != maps.end(); }
bool Unlist(std::vector<int64_t>& maps, int64_t map) {
    const auto found = std::find(maps.begin(), maps.end(), map);
    if (found == maps.end()) return false;
    maps.erase(found);
    return true;
}
void List(std::vector<int64_t>& maps, int64_t map) {
    if (maps.size() >= MaxMaps) maps.erase(maps.begin());
    maps.push_back(map);
}
int64_t Seed(Side side) { return side == Side::Astrub ? AstrubCrypt : side == Side::Amakna ? AmaknaWell : 0; }
const Knowledge::Character* Person(const Knowledge& known, int64_t id) {
    for (const auto& who : known.characters) if (who.id == id) return &who;
    return nullptr;
}
Knowledge::Character& Person(Knowledge& known, int64_t id) {
    for (auto& who : known.characters) if (who.id == id) return who;
    if (known.characters.size() >= MaxCharacters) known.characters.erase(known.characters.begin());
    known.characters.push_back({id, {}, 0});
    return known.characters.back();
}
std::string Trip(Side from, Side to) { return std::string(SideName(from)) + ">" + SideName(to); }
std::string Name(int link) {
    Side from = Side::None, to = Side::None;
    return LinkSides(link, from, to) ? Trip(from, to) : "link" + std::to_string(link);
}
std::string Ids(const std::vector<int>& ids) {
    std::string text;
    for (const int id : ids) text += (text.empty() ? "" : ",") + std::to_string(id);
    return text.empty() ? "none" : text;
}
bool AnyReply(const std::vector<int>& ids) { return std::any_of(ids.begin(), ids.end(), [](int id) { return id > 0; }); }
// « d’Astrub », « au temple Roublard »: the trip told in the chat.
const char* From(Side side) {
    switch (side) {
    case Side::Astrub: return "d’Astrub";
    case Side::Temple: return "du temple Roublard";
    case Side::Amakna: return "du village d’Amakna";
    default: return "?";
    }
}
const char* To(Side side) {
    switch (side) {
    case Side::Astrub: return "à Astrub";
    case Side::Temple: return "au temple Roublard";
    case Side::Amakna: return "au village d’Amakna";
    default: return "?";
    }
}
}

const char* SideName(Side side) {
    switch (side) {
    case Side::Astrub: return "astrub";
    case Side::Temple: return "temple";
    case Side::Amakna: return "amakna";
    default: return "none";
    }
}
Side SideAt(int x, int y) {
    if (x == 5 && y == -15) return Side::Astrub;
    if (x == 10 && (y == 16 || y == 15)) return Side::Temple;
    if (x == 3 && y == 3) return Side::Amakna;
    return Side::None;
}
Side SideOf(int64_t map, int x, int y) {
    // « Temple Roublard » maps (subarea 6, one of them at [0,0]) and the
    // « Repaire des roublards » ones (subarea 39), from the game's map data.
    constexpr int64_t temple[] = {67108864, 67109888, 67110147, 67110912, 67111936, 67112960, 67113984, 83100172, 83101196};
    if (std::find(std::begin(temple), std::end(temple), map) != std::end(temple)) return Side::Temple;
    return SideAt(x, y);
}
Side QuestionSide(int message) {
    return message == AstrubMessage ? Side::Astrub : message == TempleMessage ? Side::Temple : message == AmaknaMessage ? Side::Amakna : Side::None;
}
bool LeadsTo(int reply, Side destination) {
    switch (destination) {
    case Side::Temple: return reply == 8758 || reply == 8781;  // « Se rendre au temple Roublard. »
    case Side::Astrub: return reply == 8759 || reply == 8782;  // « Se rendre à Astrub. »
    case Side::Amakna: return reply == 8779 || reply == 8780;  // « Se rendre au village d'Amakna. »
    default: return false;
    }
}
// The chapel mercenary's « Proposer de payer 50 kamas pour jeter un coup d'œil à
// l'intérieur. »: the passage never needs it, and no other paid reply is chosen.
bool Paid(int reply) { return reply == 37073; }
bool Travel(int reply) { return LeadsTo(reply, Side::Astrub) || LeadsTo(reply, Side::Temple) || LeadsTo(reply, Side::Amakna); }
bool Question(int message) { return QuestionSide(message) != Side::None; }
int LinkId(Side from, Side to) { return 100 + 10 * Index(from) + Index(to); }
bool LinkSides(int id, Side& from, Side& to) {
    if (id < 100 || id >= 100 + 10 * SideCount) return false;
    const int origin = (id - 100) / 10, destination = (id - 100) % 10;
    if (origin >= SideCount || destination >= SideCount || origin == destination) return false;
    from = static_cast<Side>(origin); to = static_cast<Side>(destination);
    return true;
}
unsigned RefusedBit(Side from, Side to) {
    return ValidSide(from) && ValidSide(to) && from != to ? 1u << (SideCount * Index(from) + Index(to)) : 0u;
}

int64_t NpcMap(const Knowledge& known, Side side) {
    if (!ValidSide(side)) return 0;
    const auto learned = known.npc[Index(side)];
    if (ValidMap(learned) && !Listed(known.absent, learned)) return learned;
    const auto seed = Seed(side);
    return seed && !Listed(known.absent, seed) ? seed : 0;
}
int64_t ArrivalMap(const Knowledge& known, Side from, Side to) {
    if (!ValidSide(from) || !ValidSide(to) || from == to) return 0;
    const auto learned = known.arrival[Index(from)][Index(to)];
    if (ValidMap(learned)) return learned;
    if (const auto npc = NpcMap(known, to)) return npc;
    return to == Side::Temple ? TempleEntrance : Seed(to);
}
std::vector<chat_route::Shortcut> Links(const Knowledge& known, int64_t character) {
    std::vector<chat_route::Shortcut> links;
    const auto* who = character > 0 ? Person(known, character) : nullptr;
    for (int origin = 0; origin < SideCount; ++origin) {
        for (int destination = 0; destination < SideCount; ++destination) {
            if (origin == destination) continue;
            const auto from = static_cast<Side>(origin), to = static_cast<Side>(destination);
            if (who && (who->refused & RefusedBit(from, to))) continue;
            const auto entrance = NpcMap(known, from), arrival = ArrivalMap(known, from, to);
            if (!entrance || !arrival || entrance == arrival) continue;
            links.push_back({LinkId(from, to), entrance, arrival, PassageCost});
        }
    }
    return links;
}
bool Apply(Knowledge& known, const Change& change) {
    switch (change.kind) {
    case Change::Kind::Npc: {
        if (!ValidSide(change.from) || !ValidMap(change.map)) return false;
        bool changed = false;
        auto& slot = known.npc[Index(change.from)];
        if (slot != change.map) { slot = change.map; changed = true; }
        changed = Unlist(known.absent, change.map) || changed;
        changed = Unlist(known.doubtful, change.map) || changed;
        return changed;
    }
    case Change::Kind::Doubt: {
        if (!ValidMap(change.map) || Listed(known.absent, change.map) || Listed(known.doubtful, change.map)) return false;
        List(known.doubtful, change.map);
        return true;
    }
    case Change::Kind::Absent: {
        if (!ValidMap(change.map) || Listed(known.absent, change.map)) return false;
        Unlist(known.doubtful, change.map);
        List(known.absent, change.map);
        for (auto& map : known.npc) if (map == change.map) map = 0;
        return true;
    }
    case Change::Kind::Arrival: {
        if (!ValidSide(change.from) || !ValidSide(change.to) || change.from == change.to || !ValidMap(change.map)) return false;
        auto& slot = known.arrival[Index(change.from)][Index(change.to)];
        if (slot == change.map) return false;
        slot = change.map;
        return true;
    }
    case Change::Kind::Offered: {
        if (change.character <= 0 || std::none_of(change.replies.begin(), change.replies.end(), Travel)) return false;
        const bool fresh = !Person(std::as_const(known), change.character);
        auto& who = Person(known, change.character);
        bool changed = fresh;
        for (const int reply : change.replies) {
            if (!Travel(reply)) continue;
            if (std::find(who.offered.begin(), who.offered.end(), reply) == who.offered.end()) { who.offered.push_back(reply); changed = true; }
            for (int side = 0; side < SideCount; ++side) {
                const auto bit = RefusedBit(change.from, static_cast<Side>(side));
                if (bit && LeadsTo(reply, static_cast<Side>(side)) && (who.refused & bit)) { who.refused &= ~bit; changed = true; }
            }
        }
        std::sort(who.offered.begin(), who.offered.end());
        return changed;
    }
    case Change::Kind::Refused: {
        const auto bit = RefusedBit(change.from, change.to);
        if (change.character <= 0 || !bit) return false;
        auto& who = Person(known, change.character);
        if (who.refused & bit) return false;
        who.refused |= bit;
        return true;
    }
    }
    return false;
}

namespace {
// Like the notebook: no file behind a link (never the mod's own file).
void Plain(const std::filesystem::path& path) {
    const auto attributes = GetFileAttributesW(path.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES) {
        if (GetLastError() == ERROR_FILE_NOT_FOUND) return;
        throw std::runtime_error("passage file inaccessible");
    }
    if (attributes & FILE_ATTRIBUTE_REPARSE_POINT) throw InvalidFile("passage file is a link");
}
Side ParseSide(const std::string& text) {
    for (int side = 0; side < SideCount; ++side) if (text == SideName(static_cast<Side>(side))) return static_cast<Side>(side);
    throw InvalidFile("passage file: unknown side");
}
Side ParseSide(const Json& value) {
    if (!value.is_string()) throw InvalidFile("passage file: unknown side");
    return ParseSide(value.get<std::string>());
}
int64_t ParseMap(const Json& value, bool zero) {
    if (!value.is_number_integer()) throw InvalidFile("passage file: invalid map");
    if (value.is_number_unsigned() && value.get<uint64_t>() >= static_cast<uint64_t>(MapLimit)) throw InvalidFile("passage file: invalid map");
    const auto map = value.get<int64_t>();
    if ((zero && map == 0) || ValidMap(map)) return map;
    throw InvalidFile("passage file: invalid map");
}
std::vector<int64_t> ParseMaps(const Json& value) {
    if (!value.is_array() || value.size() > MaxMaps) throw InvalidFile("passage file: invalid map list");
    std::vector<int64_t> maps;
    for (const auto& map : value) {
        const auto parsed = ParseMap(map, false);
        if (!Listed(maps, parsed)) maps.push_back(parsed);
    }
    return maps;
}
Json ToJson(const Knowledge& known) {
    Json npc = Json::object(), arrival = Json::array(), characters = Json::array();
    for (int side = 0; side < SideCount; ++side) npc[SideName(static_cast<Side>(side))] = known.npc[side];
    for (int from = 0; from < SideCount; ++from)
        for (int to = 0; to < SideCount; ++to)
            if (known.arrival[from][to])
                arrival.push_back({{"from", SideName(static_cast<Side>(from))}, {"to", SideName(static_cast<Side>(to))}, {"map", known.arrival[from][to]}});
    for (const auto& who : known.characters) {
        Json refused = Json::array();
        for (int from = 0; from < SideCount; ++from)
            for (int to = 0; to < SideCount; ++to)
                if (who.refused & RefusedBit(static_cast<Side>(from), static_cast<Side>(to))) refused.push_back(Trip(static_cast<Side>(from), static_cast<Side>(to)));
        characters.push_back({{"id", who.id}, {"offered", who.offered}, {"refused", refused}});
    }
    return {{"schema", 1}, {"npc", npc}, {"doubtful", known.doubtful}, {"absent", known.absent}, {"arrival", arrival}, {"characters", characters}};
}
Knowledge FromJson(const Json& json) {
    if (!json.is_object() || !json.contains("schema") || json.at("schema") != 1) throw InvalidFile("passage file: unknown format");
    Knowledge known;
    const auto& npc = json.at("npc");
    for (int side = 0; side < SideCount; ++side) known.npc[side] = ParseMap(npc.at(SideName(static_cast<Side>(side))), true);
    known.doubtful = ParseMaps(json.at("doubtful"));
    known.absent = ParseMaps(json.at("absent"));
    const auto& arrival = json.at("arrival");
    if (!arrival.is_array() || arrival.size() > SideCount * SideCount) throw InvalidFile("passage file: invalid arrivals");
    for (const auto& trip : arrival) {
        const auto from = ParseSide(trip.at("from")), to = ParseSide(trip.at("to"));
        if (from == to) throw InvalidFile("passage file: invalid arrival");
        known.arrival[Index(from)][Index(to)] = ParseMap(trip.at("map"), false);
    }
    const auto& characters = json.at("characters");
    if (!characters.is_array() || characters.size() > MaxCharacters) throw InvalidFile("passage file: invalid characters");
    for (const auto& item : characters) {
        Knowledge::Character who;
        if (!item.at("id").is_number_integer() || item.at("id").get<int64_t>() <= 0) throw InvalidFile("passage file: invalid character");
        who.id = item.at("id").get<int64_t>();
        const auto& offered = item.at("offered");
        if (!offered.is_array() || offered.size() > MaxReplies) throw InvalidFile("passage file: invalid replies");
        for (const auto& reply : offered) {
            if (!reply.is_number_integer() || !Travel(reply.get<int>())) throw InvalidFile("passage file: invalid reply");
            if (std::find(who.offered.begin(), who.offered.end(), reply.get<int>()) == who.offered.end()) who.offered.push_back(reply.get<int>());
        }
        std::sort(who.offered.begin(), who.offered.end());
        const auto& refused = item.at("refused");
        if (!refused.is_array() || refused.size() > SideCount * SideCount) throw InvalidFile("passage file: invalid refusals");
        for (const auto& trip : refused) {
            if (!trip.is_string()) throw InvalidFile("passage file: invalid refusal");
            const auto text = trip.get<std::string>();
            const auto mark = text.find('>');
            if (mark == std::string::npos) throw InvalidFile("passage file: invalid refusal");
            const auto bit = RefusedBit(ParseSide(text.substr(0, mark)), ParseSide(text.substr(mark + 1)));
            if (!bit) throw InvalidFile("passage file: invalid refusal");
            who.refused |= bit;
        }
        if (Person(std::as_const(known), who.id)) throw InvalidFile("passage file: duplicate character");
        known.characters.push_back(std::move(who));
    }
    return known;
}
void Write(const std::filesystem::path& path, const Knowledge& known) {
    const auto bytes = ToJson(known).dump(2);
    if (bytes.size() > MaxBytes) throw std::runtime_error("passage file too large");
    static std::atomic_uint64_t counter{0};
    auto temporary = path;
    temporary += L".tmp-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(++counter);
    struct Cleanup {
        std::filesystem::path path;
        ~Cleanup() { std::error_code ignored; std::filesystem::remove(path, ignored); }
    } cleanup{temporary};
    {
        Handle file(CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr));
        DWORD written = 0;
        if (file.value == INVALID_HANDLE_VALUE ||
            !WriteFile(file.value, bytes.data(), static_cast<DWORD>(bytes.size()), &written, nullptr) ||
            written != bytes.size() || !FlushFileBuffers(file.value))
            throw std::runtime_error("passage file not written");
    }
    Plain(path);
    if (!MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        throw std::runtime_error("passage file not replaced");
}
}

std::filesystem::path DataPath(const std::filesystem::path& settingsPath) {
    return settingsPath.parent_path() / L"DofusMiniatures-roublard-passage.json";
}
Knowledge Read(const std::filesystem::path& path) {
    Plain(path);
    std::error_code error;
    if (!std::filesystem::exists(path, error)) {
        if (error) throw std::runtime_error("passage file inaccessible");
        return {};
    }
    if (!std::filesystem::is_regular_file(path, error) || error) throw InvalidFile("passage file is not a file");
    const auto size = std::filesystem::file_size(path, error);
    if (error) throw std::runtime_error("passage file inaccessible");
    if (size > MaxBytes) throw InvalidFile("passage file too large");
    std::ifstream file(path, std::ios::binary);
    if (!file) throw std::runtime_error("passage file unreadable");
    std::string bytes(MaxBytes + 1, '\0');
    file.read(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    bytes.resize(static_cast<size_t>(file.gcount()));
    if (file.bad()) throw std::runtime_error("passage file unreadable");
    if (bytes.size() > MaxBytes) throw InvalidFile("passage file too large");
    try { return FromJson(Json::parse(bytes)); }
    catch (const Json::exception&) { throw InvalidFile("passage file: invalid content"); }
}
Knowledge Update(const std::filesystem::path& path, const std::vector<Change>& changes) {
    auto lockPath = path;
    lockPath += L".lock";
    Plain(lockPath);
    Handle lock(CreateFileW(lockPath.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_ALWAYS,
        FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
    if (lock.value == INVALID_HANDLE_VALUE) throw std::runtime_error("passage file in use by another client");
    auto known = Read(path);
    bool changed = false;
    for (const auto& change : changes) changed = Apply(known, change) || changed;
    if (changed) Write(path, known);
    return known;
}

namespace {
void RequireField(Klass type, const char* name, size_t offset) {
    auto field = api.class_get_field_from_name(type, name);
    if (!field && api.class_get_parent) {
        // A generic base's field (goz<fky.fkw>.eabq of the dialog service).
        if (const auto parent = api.class_get_parent(type)) field = api.class_get_field_from_name(parent, name);
    }
    if (!field || api.field_get_offset(field) != offset) throw std::runtime_error(std::string("Passage field mismatch: ") + name);
}
// The game's own NPC menu action and NPC dialog, read and clicked as the
// player's clicks do; no network message is built here.
class Native final : public Game {
public:
    void Install() {
        constexpr const char* entities = "Core.Services.Entities.EntityInformationServices";
        constexpr const char* menus = "Core.UILogic.Components.ContextMenu.ContextMenuFactory";
        registry = api.Class("Core.dll", "", "evv");
        info = api.Class("Core.dll", entities, "EntityInfo");
        // The entity registry of the current map and its NPC id (integer property
        // 15, read by NpcMenuMaker), as the monster glow reads them.
        list = api.FindVerified(registry, "baml", 0, 0x19579E0);
        find = api.FindVerified(registry, "bamm", 2, 0x1957A70);
        id = api.FindVerified(info, "bajh", 0, 0x1951440);
        intTry = api.FindVerified(info, "baks", 2, 0x1952330);
        // The NPC context menu: OnNPCMenuClick(view, entity, action) posts the
        // game's own action message (dgr) on m_messageBus, sent by fcl.bcsn.
        maker = api.Class("Core.dll", menus, "NpcMenuMaker");
        makerCtor = api.FindVerified(maker, ".ctor", 6, 0x16E8A40);
        menuClick = api.FindVerified(maker, "OnNPCMenuClick", 3, 0x16E89A0);
        RequireField(maker, "m_entityId", 0x14);
        RequireField(maker, "m_messageBus", 0x18);
        // The dialog: the service fky (ghd) keeps its UI in goz.eabq under key 0;
        // SelectReply(button) is the reply button's click (it posts dgz with the
        // button's reply id and locks the dialog), OnClose the close button's.
        // Open (dialog creation) shows six empty buttons and keeps the previous
        // question's entity, message and lock; OnNpcDialog sets m_npcEntityId
        // when the question arrives.
        dialogs = api.Class("Core.dll", "", "ghd");
        RequireField(dialogs, "eabq", 0x48);
        dialog = api.Class("Core.dll", "Core.UILogic.NpcDialog", "NpcDialog");
        select = api.FindVerified(dialog, "SelectReply", 1, 0x2A64DB0);
        close = api.FindVerified(dialog, "OnClose", 0, 0x2A61860);
        RequireField(dialog, "m_npc", 0x1D0);
        RequireField(dialog, "m_currentMsg", 0x1E0);
        RequireField(dialog, "m_repliesIdFromBtn", 0x1E8);
        RequireField(dialog, "m_lockAndWaitAnswers", 0x1F0);
        RequireField(dialog, "m_canClose", 0x244);
        RequireField(dialog, "m_npcEntityId", 0x290);
        npcData = api.Class("Ankama.Dofus.Core.DataCenter.dll", "Core.DataCenter.Metadata.Npc", "NpcData");
        RequireField(npcData, "id", 0x10);
        character = api.Class("Core.dll", "", "esg");
        localId = api.FindVerified(character, "zcx", 0, 0x18BB140);
        mapInfo = api.FindVerified(api.Class("Ankama.Dofus.Core.DataCenter.dll", "Core.DataCenter.Metadata.World", "MapInformationData"),
            "GetMapInformationById", 1, 0x7C4200);
    }
    void Release() { marked.reset(); }
    int64_t Character() override {
        Ref who(module ? module->FindService("fng") : nullptr);
        // zcx dereferences esg.dpzj: no character data while it is null.
        if (!Is(who.get(), character) || !api.Field(who.get(), "dpzj")) return 0;
        return api.Value<int64_t>(api.Invoke(localId, who.get()));
    }
    bool Locate(int64_t map, int& x, int& y) override {
        Ref data(api.Invoke(mapInfo, nullptr, {&map}));
        if (!data.get()) return false;
        x = managed::Read<int16_t>(data.get(), 0x18); y = managed::Read<int16_t>(data.get(), 0x1A);
        return true;
    }
    int FindNpc(int npc, int64_t& entity) override {
        entity = 0;
        Ref entities(Registry());
        if (!entities.get()) return -1;
        Ref all(api.Invoke(list, entities.get()));
        const int count = Count(all.get(), 512);
        int found = 0;
        for (int i = 0; i < count; ++i) {
            Ref item(api.Call(all.get(), "get_Item", {&i}));
            if (!Is(item.get(), info)) continue;
            int key = 15, value = 0;
            if (!api.Value<bool>(api.Invoke(intTry, item.get(), {&key, &value})) || value != npc) continue;
            const auto current = api.Value<int64_t>(api.Invoke(id, item.get()));
            if (!current) continue;
            if (!found++) entity = current;
        }
        return found;
    }
    void Mark() override {
        Ref ui(Ui());
        marked.reset(ui.get() ? api.Field(ui.get(), "m_repliesIdFromBtn") : nullptr);
    }
    Dialog Read() override {
        Dialog result;
        Ref ui(Ui());
        if (!ui.get()) return result;
        Ref npc(api.Field(ui.get(), "m_npc")), replies(api.Field(ui.get(), "m_repliesIdFromBtn"));
        result.npc = Is(npc.get(), npcData) ? *static_cast<int*>(api.FieldAddress(npc.get(), "id")) : 0;
        result.entity = *static_cast<int64_t*>(api.FieldAddress(ui.get(), "m_npcEntityId"));
        result.message = static_cast<int>(*static_cast<uint32_t*>(api.FieldAddress(ui.get(), "m_currentMsg")));
        result.locked = *static_cast<bool*>(api.FieldAddress(ui.get(), "m_lockAndWaitAnswers"));
        for (int i = 0, n = Count(replies.get(), static_cast<int>(MaxReplies)); i < n; ++i)
            result.replies.push_back(api.Value<int>(api.Call(replies.get(), "get_Item", {&i})));
        // Between Open and its question: six empty buttons, the previous entity.
        result.shown = replies.get() && result.npc > 0 && result.entity != -1 &&
            std::any_of(result.replies.begin(), result.replies.end(), [](int reply) { return reply != 0; });
        result.fresh = replies.get() && replies.get() != marked.get();
        return result;
    }
    bool Talk(int64_t entity) override {
        if (!module || module->InCombat() || entity < std::numeric_limits<int>::min() || entity > std::numeric_limits<int>::max()) return false;
        Ref entities(Registry());
        Obj found = nullptr;
        if (!entities.get() || !api.Value<bool>(api.Invoke(find, entities.get(), {&entity, &found})) || !Is(found, info)) return false;
        Ref data(found), bus(module->FindService("fmz")), who(module->FindService("fng")), npcs(module->FindService("fsq"));
        if (!bus.get() || !who.get()) return false;
        Ref menu(api.object_new(maker));
        int target = static_cast<int>(entity), action = SpeakAction;
        bool rightClick = false;
        api.Invoke(makerCtor, menu.get(), {data.get(), &target, &rightClick, bus.get(), who.get(), npcs.get()});
        if (api.Field(menu.get(), "m_messageBus") != bus.get() || *static_cast<int*>(api.FieldAddress(menu.get(), "m_entityId")) != target) return false;
        // The menu row's click, without its view (OnNPCMenuClick does not read it).
        api.Invoke(menuClick, menu.get(), {nullptr, &target, &action});
        return true;
    }
    bool Reply(int button, int reply) override {
        Ref ui(Ui());
        if (!module || module->InCombat() || !ui.get() || *static_cast<bool*>(api.FieldAddress(ui.get(), "m_lockAndWaitAnswers"))) return false;
        Ref replies(api.Field(ui.get(), "m_repliesIdFromBtn"));
        if (button < 0 || button >= Count(replies.get(), static_cast<int>(MaxReplies))) return false;
        const int held = api.Value<int>(api.Call(replies.get(), "get_Item", {&button}));
        if (held != reply || held <= 0 || Paid(held)) return false;
        api.Invoke(select, ui.get(), {&button});
        return *static_cast<bool*>(api.FieldAddress(ui.get(), "m_lockAndWaitAnswers"));
    }
    bool Close(int64_t entity) override {
        Ref ui(Ui());
        if (!ui.get() || !*static_cast<bool*>(api.FieldAddress(ui.get(), "m_canClose")) ||
            *static_cast<int64_t*>(api.FieldAddress(ui.get(), "m_npcEntityId")) != entity) return false;
        Ref npc(api.Field(ui.get(), "m_npc"));
        if (!Is(npc.get(), npcData) || *static_cast<int*>(api.FieldAddress(npc.get(), "id")) != BadAboum) return false;
        api.Invoke(close, ui.get());
        return true;
    }
private:
    static bool Is(Obj object, Klass type) { return object && api.object_get_class(object) == type; }
    static int Count(Obj values, int limit) {
        if (!values) return 0;
        const int n = api.Value<int>(api.Call(values, "get_Count"));
        if (n < 0 || n > limit) throw std::runtime_error("Passage list bounds");
        return n;
    }
    Obj Registry() const {
        auto found = module ? module->FindService("fso") : nullptr;
        return Is(found, registry) ? found : nullptr;
    }
    Obj Ui() const {
        Ref service(module ? module->FindService("fky") : nullptr);
        if (!Is(service.get(), dialogs)) return nullptr;
        Ref uis(api.Field(service.get(), "eabq"));
        if (!uis.get()) return nullptr;
        int key = 0;  // fky.fkw.dutm, the NPC dialog
        Obj value = nullptr;
        if (!api.Value<bool>(api.Call(uis.get(), "TryGetValue", {&key, &value})) || !Is(value, dialog)) return nullptr;
        return value;
    }
    Klass registry{}, info{}, maker{}, dialogs{}, dialog{}, npcData{}, character{};
    Method list{}, find{}, id{}, intTry{}, makerCtor{}, menuClick{}, select{}, close{}, localId{}, mapInfo{};
    Ref marked;
};

struct Run {
    enum class Phase { Idle, Search, Dialog, Teleport, Arrival } phase = Phase::Idle;
    int link = -1;
    Side from = Side::None, to = Side::None;
    int64_t npcMap = 0, entity = 0, character = 0, arrival = 0;
    uint64_t started = 0, deadline = 0;
    int reply = 0;
    unsigned settled = 0;
    bool registry = false;  // the map's entity registry answered at least once
};
// The player's own passage, seen from the maps he stands on.
struct Watch {
    int64_t map = 0, entity = 0;
    uint64_t since = 0, repliedAt = 0;
    int scans = 0;
    bool present = false, question = false, replied = false;
    Side side = Side::None;    // by the map
    Side origin = Side::None;  // by his last question, else the map
    // A reply on a side, then a map change: the next settled map is the trip's
    // arrival when it is on another side (loading maps in between are skipped).
    Side pendingFrom = Side::None;
    uint64_t pendingAt = 0;
};
// A trip not planned again for a while: for one character, or every one (0).
struct Pause { int64_t character = 0; int link = 0; uint64_t until = 0; };
struct State {
    bool installed = false, storeBroken = false, loaded = false;
    std::filesystem::path path;
    Knowledge known;
    std::vector<Change> pending;
    uint64_t nextSync = 0;
    unsigned syncs = 0;  // file cycles (lock, read, write when changed)
    std::vector<Pause> pauses;
    Game* scripted = nullptr;
    std::unique_ptr<Native> native;
    Run run;
    Watch watch;
    std::string lastNote;
    uint64_t lastNoteAt = 0;
} passage;
Game* Current() { return passage.scripted ? passage.scripted : passage.native.get(); }
void Note(const std::string& text) {
    const auto now = GetTickCount64();
    if (text == passage.lastNote && now - passage.lastNoteAt < 3000) return;
    passage.lastNote = text; passage.lastNoteAt = now;
    Log("ROUBLARD_PASSAGE_" + text);
}
// The side of a map, by its id or its coordinates (x, y: [0,0] when unknown).
Side Where(Game* game, int64_t map, int& x, int& y) {
    x = y = 0;
    return game->Locate(map, x, y) ? SideOf(map, x, y) : SideOf(map, -1000, -1000);
}
bool His(const Dialog& dialog, int64_t entity) { return dialog.shown && dialog.npc == BadAboum && dialog.entity == entity; }
std::string Describe(const Change& change) {
    switch (change.kind) {
    case Change::Kind::Npc: return std::string("npc side=") + SideName(change.from) + " map=" + std::to_string(change.map);
    case Change::Kind::Doubt: return "doubtful map=" + std::to_string(change.map);
    case Change::Kind::Absent: return "absent map=" + std::to_string(change.map);
    case Change::Kind::Arrival: return "arrival trip=" + Trip(change.from, change.to) + " map=" + std::to_string(change.map);
    case Change::Kind::Offered: return std::string("offered side=") + SideName(change.from) + " replies=" + Ids(change.replies);
    case Change::Kind::Refused: return "refused trip=" + Trip(change.from, change.to);
    }
    return "?";
}
// In memory now; to the file at most once per step (Sync).
void Learn(Change change) {
    if (!Apply(passage.known, change)) return;
    Note("LEARN " + Describe(change));
    if (passage.path.empty() || passage.storeBroken) return;
    if (passage.pending.size() >= MaxPending) passage.pending.erase(passage.pending.begin());
    passage.pending.push_back(std::move(change));
}
Change Making(Change::Kind kind, Side from, Side to, int64_t map, int64_t character = 0, std::vector<int> replies = {}) {
    Change change;
    change.kind = kind; change.from = from; change.to = to; change.map = map; change.character = character; change.replies = std::move(replies);
    return change;
}
// One file cycle under its lock: read (the other clients' learning), apply what
// is pending, write when it changed. A file in use is tried again later; a
// file that is not the mod's is kept and this session learns in memory.
void Sync(uint64_t now, bool force) {
    if (passage.path.empty() || passage.storeBroken || (passage.loaded && passage.pending.empty())) return;
    if (!force && now < passage.nextSync) return;
    ++passage.syncs;
    try {
        passage.known = Update(passage.path, passage.pending);
        passage.pending.clear(); passage.loaded = true; passage.nextSync = 0;
    } catch (const InvalidFile& error) {
        passage.storeBroken = true; passage.pending.clear();
        Log(std::string("ROUBLARD_PASSAGE_STORE_ERROR ") + error.what() + " (kept as it is; this session learns in memory)");
    } catch (const std::exception& error) {
        passage.nextSync = now + StoreRetryMs;
        Note(std::string("STORE_RETRY ") + error.what());
    }
}
void PauseTrip(int64_t character, int link, uint64_t until) {
    for (auto& pause : passage.pauses)
        if (pause.character == character && pause.link == link) { pause.until = std::max(pause.until, until); return; }
    if (passage.pauses.size() >= MaxPauses) passage.pauses.erase(passage.pauses.begin());
    passage.pauses.push_back({character, link, until});
}
bool Paused(int64_t character, int link, uint64_t now) {
    return std::any_of(passage.pauses.begin(), passage.pauses.end(), [&](const Pause& pause) {
        return pause.link == link && (!pause.character || pause.character == character) && now < pause.until;
    });
}
const char* PhaseName(Run::Phase phase) {
    switch (phase) {
    case Run::Phase::Search: return "search";
    case Run::Phase::Dialog: return "dialog";
    case Run::Phase::Teleport: return "teleport";
    case Run::Phase::Arrival: return "arrival";
    default: return "idle";
    }
}
constexpr const char* Without = " ; le trajet continue sans le passage secret.";
Result Finish(Outcome outcome, int64_t map, const std::string& reason, const std::string& notice, uint64_t now) {
    const auto& run = passage.run;
    const char* word = outcome == Outcome::Arrived ? "ARRIVED" : outcome == Outcome::Failed ? "FAILED" : "ABORTED";
    Note(std::string(word) + " trip=" + Name(run.link) + " phase=" + PhaseName(run.phase) + " map=" + std::to_string(map) +
        " reason=" + reason + " ms=" + std::to_string(now - run.started));
    // A failure that taught nothing: this trip waits before the next try.
    if (outcome == Outcome::Failed) PauseTrip(run.character, run.link, now + PauseMs);
    passage.run = {};
    return {outcome, map, reason, notice};
}
Result Advance(int64_t map, bool ready, uint64_t now) {
    auto& run = passage.run;
    auto* game = Current();
    switch (run.phase) {
    case Run::Phase::Search: {
        if (map != run.npcMap) return Finish(Outcome::Aborted, map, "left his map", {}, now);
        if (!ready) {
            if (now >= run.deadline) return Finish(Outcome::Failed, map, "map not ready", std::string("la carte de Bad Aboum ne se charge pas") + Without, now);
            return {};
        }
        int64_t entity = 0;
        const int count = game->FindNpc(BadAboum, entity);
        if (count >= 0) run.registry = true;
        if (count > 1) return Finish(Outcome::Failed, map, "several npcs count=" + std::to_string(count), std::string("plusieurs Bad Aboum sur la carte") + Without, now);
        if (count == 1) {
            game->Mark();
            Note("TALK entity=" + std::to_string(entity) + " map=" + std::to_string(map));
            if (!game->Talk(entity)) return Finish(Outcome::Failed, map, "talk unavailable", std::string("impossible de parler à Bad Aboum") + Without, now);
            run.entity = entity; run.phase = Run::Phase::Dialog; run.deadline = now + DialogMs;
            return {};
        }
        if (now < run.deadline) return {};
        if (!run.registry) return Finish(Outcome::Failed, map, "entity registry unavailable", std::string("Bad Aboum est introuvable pour l’instant") + Without, now);
        // Not there: a first time is only a doubt (and a pause for everyone);
        // a second look in vain, later, removes his map.
        Learn(Making(Listed(passage.known.doubtful, map) ? Change::Kind::Absent : Change::Kind::Doubt, run.from, Side::None, map));
        for (int side = 0; side < SideCount; ++side)
            if (side != Index(run.from)) PauseTrip(0, LinkId(run.from, static_cast<Side>(side)), now + PauseMs);
        return Finish(Outcome::Failed, map, "npc absent", std::string("Bad Aboum est introuvable") + Without, now);
    }
    case Run::Phase::Dialog: {
        if (map != run.npcMap) return Finish(Outcome::Aborted, map, "left his map during the dialog", {}, now);
        const auto dialog = game->Read();
        if (dialog.fresh && dialog.shown) {
            // Another NPC's dialog is the player's own: never closed by the mod.
            if (dialog.npc != BadAboum || dialog.entity != run.entity) return Finish(Outcome::Aborted, map, "another dialog npc=" + std::to_string(dialog.npc), {}, now);
            Note("DIALOG message=" + std::to_string(dialog.message) + " replies=" + Ids(dialog.replies) + " locked=" + std::to_string(dialog.locked));
            if (dialog.locked) return Finish(Outcome::Aborted, map, "answered by the player", {}, now);
            Learn(Making(Change::Kind::Npc, run.from, Side::None, map));
            if (!Question(dialog.message)) {
                game->Close(run.entity);
                return Finish(Outcome::Failed, map, "unexpected question " + std::to_string(dialog.message), std::string("question inattendue de Bad Aboum") + Without, now);
            }
            std::vector<int> offered;
            for (const int reply : dialog.replies) if (Travel(reply)) offered.push_back(reply);
            Learn(Making(Change::Kind::Offered, run.from, Side::None, 0, run.character, offered));
            int button = -1, matches = 0;
            for (size_t i = 0; i < dialog.replies.size(); ++i)
                if (LeadsTo(dialog.replies[i], run.to) && !Paid(dialog.replies[i])) { button = static_cast<int>(i); ++matches; }
            if (!matches) {
                // Only a question that offers replies says what he refuses.
                if (AnyReply(dialog.replies)) Learn(Making(Change::Kind::Refused, run.from, run.to, 0, run.character));
                game->Close(run.entity);
                return Finish(Outcome::Failed, map, "reply absent", std::string("Bad Aboum ne propose pas ce trajet à ce personnage") + Without, now);
            }
            if (matches > 1) {
                game->Close(run.entity);
                return Finish(Outcome::Failed, map, "ambiguous replies", std::string("Bad Aboum propose plusieurs réponses vers cette destination") + Without, now);
            }
            run.reply = dialog.replies[static_cast<size_t>(button)];
            Note("REPLY reply=" + std::to_string(run.reply) + " button=" + std::to_string(button));
            if (!game->Reply(button, run.reply)) {
                game->Close(run.entity);
                return Finish(Outcome::Failed, map, "reply not sent", std::string("la réponse à Bad Aboum n’a pas pu être envoyée") + Without, now);
            }
            game->Mark();
            run.phase = Run::Phase::Teleport; run.deadline = now + TeleportMs;
            return {};
        }
        if (now >= run.deadline) return Finish(Outcome::Failed, map, "no dialog", std::string("Bad Aboum ne répond pas") + Without, now);
        return {};
    }
    case Run::Phase::Teleport: {
        if (map && map != run.npcMap) {
            run.phase = Run::Phase::Arrival; run.arrival = map; run.settled = 0; run.deadline = now + ArrivalMs;
            Note("TELEPORTED map=" + std::to_string(map) + " reply=" + std::to_string(run.reply));
            return {};
        }
        const auto dialog = game->Read();
        if (dialog.fresh && dialog.shown) {
            if (dialog.npc != BadAboum || dialog.entity != run.entity) return Finish(Outcome::Aborted, map, "another dialog npc=" + std::to_string(dialog.npc), {}, now);
            if (dialog.locked) return Finish(Outcome::Aborted, map, "answered by the player", {}, now);
            game->Close(run.entity);
            return Finish(Outcome::Failed, map, "another question " + std::to_string(dialog.message), std::string("Bad Aboum pose une autre question") + Without, now);
        }
        if (now >= run.deadline) {
            // Only his own answered question, still waiting, is closed.
            if (His(dialog, run.entity) && dialog.locked && !dialog.fresh) game->Close(run.entity);
            return Finish(Outcome::Failed, map, "no teleport", "le passage secret n’a pas eu lieu ; le trajet continue sans lui.", now);
        }
        return {};
    }
    case Run::Phase::Arrival: {
        if (map != run.arrival) { run.arrival = map; run.settled = 0; }
        if (!map || !ready) {
            run.settled = 0;
            if (now >= run.deadline) return Finish(Outcome::Aborted, map, "arrival not ready", "la carte d’arrivée ne se charge pas : trajet arrêté.", now);
            return {};
        }
        if (++run.settled < 2) return {};
        int x = 0, y = 0;
        const auto side = Where(game, map, x, y);
        if (side == run.to) {
            Learn(Making(Change::Kind::Arrival, run.from, run.to, map));
            // He usually waits at the arrival: his map on that side, for the
            // way back (the trip goes on at once, before any passive look).
            int64_t entity = 0;
            if (game->FindNpc(BadAboum, entity) == 1) {
                Note("SEEN map=" + std::to_string(map) + " side=" + SideName(run.to) + " entity=" + std::to_string(entity));
                Learn(Making(Change::Kind::Npc, run.to, Side::None, map));
            }
            return Finish(Outcome::Arrived, map, "arrived", {}, now);
        }
        // Not where his trip leads: the player's own move (or no teleport at all).
        if (side == run.from) return Finish(Outcome::Aborted, map, "back on the origin side", {}, now);
        return Finish(Outcome::Aborted, map, "unexpected arrival [" + std::to_string(x) + "," + std::to_string(y) + "]",
            "arrivée inattendue après le passage secret : trajet arrêté.", now);
    }
    default: break;
    }
    return Finish(Outcome::Aborted, map, "phase", {}, now);
}
void Look(int64_t map, bool ready, uint64_t now) {
    auto& watch = passage.watch;
    auto* game = Current();
    if (map != watch.map) {
        Watch next;
        next.map = map;
        if (watch.replied && watch.origin != Side::None) { next.pendingFrom = watch.origin; next.pendingAt = watch.repliedAt; }
        else { next.pendingFrom = watch.pendingFrom; next.pendingAt = watch.pendingAt; }
        watch = next;
    }
    if (watch.pendingFrom != Side::None && now - watch.pendingAt >= ManualTeleportMs) watch.pendingFrom = Side::None;
    if (!map || !ready) { watch.since = 0; return; }
    if (!watch.since) watch.since = now;
    if (watch.pendingFrom != Side::None) {
        int x = 0, y = 0;
        const auto side = Where(game, map, x, y);
        if (side != Side::None && side != watch.pendingFrom) {
            Note("OBSERVED_TRIP trip=" + Trip(watch.pendingFrom, side) + " map=" + std::to_string(map));
            Learn(Making(Change::Kind::Arrival, watch.pendingFrom, side, map));
        }
        watch.pendingFrom = Side::None;
    }
    if (!watch.present) {
        if (watch.scans >= 2 || now < watch.since + ScanDelayMs * static_cast<uint64_t>(watch.scans + 1)) return;
        ++watch.scans;
        int64_t entity = 0;
        if (game->FindNpc(BadAboum, entity) != 1) return;
        int x = 0, y = 0;
        watch.present = true; watch.entity = entity;
        watch.side = watch.origin = Where(game, map, x, y);
        game->Mark();
        Note("SEEN map=" + std::to_string(map) + " side=" + SideName(watch.side) + " entity=" + std::to_string(entity));
        if (watch.side != Side::None) Learn(Making(Change::Kind::Npc, watch.side, Side::None, map));
        return;
    }
    const auto dialog = game->Read();
    if (!His(dialog, watch.entity)) return;
    if (dialog.fresh) {
        game->Mark();
        watch.question = true; watch.replied = false;
        Note("OBSERVED_DIALOG message=" + std::to_string(dialog.message) + " replies=" + Ids(dialog.replies) + " locked=" + std::to_string(dialog.locked));
        const auto asked = QuestionSide(dialog.message);
        if (asked != Side::None) {
            // His question says where he was asked, whatever the map's coordinates.
            if (watch.side == Side::None) { watch.side = asked; Learn(Making(Change::Kind::Npc, asked, Side::None, map)); }
            else if (watch.side != asked) Note("OBSERVED_SIDE map=" + std::to_string(map) + " coordinates=" + SideName(watch.side) + " question=" + SideName(asked));
            watch.origin = asked;
            const auto character = game->Character();
            std::vector<int> offered;
            for (const int reply : dialog.replies) if (Travel(reply)) offered.push_back(reply);
            Learn(Making(Change::Kind::Offered, asked, Side::None, 0, character, offered));
            if (AnyReply(dialog.replies)) {
                for (int side = 0; side < SideCount; ++side) {
                    const auto to = static_cast<Side>(side);
                    if (to == asked || std::any_of(offered.begin(), offered.end(), [to](int reply) { return LeadsTo(reply, to); })) continue;
                    Learn(Making(Change::Kind::Refused, asked, to, 0, character));
                }
            }
        }
    }
    if (watch.question && dialog.locked && !watch.replied) {
        watch.replied = true; watch.repliedAt = now;
        Note("OBSERVED_REPLY side=" + std::string(SideName(watch.origin)) + " map=" + std::to_string(map));
    }
}
}

void Install(const std::filesystem::path& settingsPath) {
    Restore();
    passage.installed = passage.storeBroken = passage.loaded = false;
    passage.native.reset(); passage.pending.clear(); passage.known = {}; passage.pauses.clear(); passage.nextSync = 0;
    passage.path = settingsPath.empty() ? std::filesystem::path{} : DataPath(settingsPath);
    Sync(GetTickCount64(), true);
    try {
        auto native = std::make_unique<Native>();
        native->Install();
        passage.native = std::move(native);
        passage.installed = true;
        Log("ROUBLARD_PASSAGE_INSTALL npc=" + std::to_string(BadAboum) + " astrub=" + std::to_string(NpcMap(passage.known, Side::Astrub)) +
            " temple=" + std::to_string(NpcMap(passage.known, Side::Temple)) + " amakna=" + std::to_string(NpcMap(passage.known, Side::Amakna)) +
            " characters=" + std::to_string(passage.known.characters.size()) + " file=" + (passage.path.empty() ? "none" : passage.loaded ? "read" : "later"));
    } catch (const std::exception& error) {
        Log(std::string("ROUBLARD_PASSAGE_UNAVAILABLE ") + error.what());
    }
}
bool Available() { return passage.scripted || (passage.installed && passage.native); }
void Restore() {
    passage.run = {}; passage.watch = {};
    if (passage.native) passage.native->Release();
    if (!passage.pending.empty()) Sync(GetTickCount64(), true);
}
void UseGame(Game* game) { Restore(); passage.scripted = game; }
const Knowledge& Known() { return passage.known; }
void ResetKnowledge() { passage.known = {}; passage.pending.clear(); passage.pauses.clear(); passage.lastNote.clear(); }
std::vector<chat_route::Shortcut> Shortcuts(uint64_t now) {
    if (!Available()) return {};
    Sync(now, false);
    int64_t character = 0;
    try { character = Current()->Character(); }
    catch (const std::exception& error) { Note(std::string("CHARACTER_ERROR ") + error.what()); }
    auto links = Links(passage.known, character);
    const auto before = links.size();
    links.erase(std::remove_if(links.begin(), links.end(), [&](const chat_route::Shortcut& link) { return Paused(character, link.id, now); }), links.end());
    std::string text;
    for (const auto& link : links) text += " " + Name(link.id) + ":" + std::to_string(link.from) + ">" + std::to_string(link.to);
    Note("SHORTCUTS character=" + std::string(character ? "known" : "unknown") + " links=" + std::to_string(links.size()) +
        " paused=" + std::to_string(before - links.size()) + text);
    return links;
}
int64_t Entrance(int link) {
    Side from = Side::None, to = Side::None;
    return LinkSides(link, from, to) ? NpcMap(passage.known, from) : 0;
}
std::string Describe(int link) {
    Side from = Side::None, to = Side::None;
    return LinkSides(link, from, to) ? std::string(From(from)) + " " + To(to) : std::string("?");
}
bool Start(int link, int64_t map, uint64_t now) {
    Side from = Side::None, to = Side::None;
    if (!Available() || !LinkSides(link, from, to)) return false;
    const auto entrance = NpcMap(passage.known, from);
    if (!entrance || map != entrance) {
        Note("START_REFUSED trip=" + Name(link) + " map=" + std::to_string(map) + " entrance=" + std::to_string(entrance));
        return false;
    }
    passage.run = {}; passage.watch = {};
    auto& run = passage.run;
    run.phase = Run::Phase::Search; run.link = link; run.from = from; run.to = to; run.npcMap = entrance;
    run.started = now; run.deadline = now + SearchMs;
    try { run.character = Current()->Character(); }
    catch (const std::exception& error) { Note(std::string("CHARACTER_ERROR ") + error.what()); }
    Note("START trip=" + Name(link) + " map=" + std::to_string(map));
    return true;
}
bool Running() { return passage.run.phase != Run::Phase::Idle; }
void Stop() {
    if (Running()) Note(std::string("STOPPED trip=") + Name(passage.run.link) + " phase=" + PhaseName(passage.run.phase));
    passage.run = {};
}
Result Step(int64_t map, bool ready, uint64_t now) {
    if (!Running()) return {Outcome::Aborted, map, "idle", {}};
    Result result;
    if (!Available()) result = Finish(Outcome::Aborted, map, "unavailable", {}, now);
    else {
        try { result = Advance(map, ready, now); }
        catch (const std::exception& error) {
            result = Finish(Outcome::Failed, map, std::string("error ") + error.what(), "le passage secret a échoué ; le trajet continue sans lui.", now);
        }
    }
    Sync(now, false);
    return result;
}
void Observe(int64_t map, bool ready, uint64_t now) {
    if (!Available() || Running()) return;
    try { Look(map, ready, now); }
    catch (const std::exception& error) {
        Note(std::string("OBSERVE_ERROR ") + error.what());
        Watch next;
        next.map = map; next.scans = 2;  // no new look at this map
        passage.watch = next;
    }
    Sync(now, false);
}
}
