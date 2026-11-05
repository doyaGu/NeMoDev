#include "application/EditorProfile.h"
#include "core/runtime/TextEncoding.h"
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <set>
#include <stdexcept>

namespace vdev {
namespace {
using Json = nlohmann::json;
constexpr size_t MaximumSize = 4 * 1024 * 1024;
void Require(bool value, const std::string &message) { if (!value) throw std::invalid_argument(message); }
std::string Text(const Json &value) {
    Require(value.is_string(), "Expected UTF-8 text");
    auto text = value.get<std::string>();
    Require(text.size() <= 1024 * 1024 && text.find('\0') == std::string::npos, "Invalid text value");
    return text;
}
std::string Identifier(const Json &value) {
    auto id = Text(value);
    Require(!id.empty() && id.size() <= 256 && id.find('.') != std::string::npos &&
        id.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789._-") == std::string::npos,
        "Expected a namespaced identifier: " + id);
    return id;
}
const Json &Array(const Json &object, const char *key) {
    static const Json empty = Json::array();
    if (!object.contains(key)) return empty;
    const auto &value = object.at(key);
    Require(value.is_array() && value.size() <= 4096, std::string("Invalid array: ") + key);
    return value;
}
std::vector<std::string> Strings(const Json &object, const char *key) {
    std::vector<std::string> result;
    for (const auto &value : Array(object, key)) result.push_back(Text(value));
    return result;
}
std::string Resolved(const std::string &path, const std::string &profile) {
    if (path.empty()) return {};
    auto file = std::filesystem::u8path(path);
    if (file.is_relative()) file = std::filesystem::u8path(profile).parent_path() / file;
    return std::filesystem::weakly_canonical(std::filesystem::absolute(file)).u8string();
}
EditorModuleSetting ModuleSetting(const Json &row) {
    Require(row.is_object() && row.contains("id") && row.contains("enabled") && row.at("enabled").is_boolean(), "Invalid module setting");
    EditorModuleSetting result{Identifier(row.at("id")), row.at("enabled").get<bool>()};
    if (row.contains("path")) result.path = Text(row.at("path"));
    if (row.contains("source")) { Require(Text(row.at("source")) == "builtin", "Unknown module source"); result.builtin = true; }
    Require(!result.builtin || result.path.empty(), "Builtin and external module sources conflict");
    Require(result.id != "application.operations" || (result.enabled && !result.builtin && result.path.empty()),
            "The application operation registry cannot be replaced or disabled");
    return result;
}
ProviderChoice Provider(const Json &row) {
    Require(row.is_object(), "Invalid provider choice");
    ProviderChoice result{Text(row.at("kind")), Identifier(row.at("id")), Identifier(row.at("provider"))};
    Require(result.kind == "command" || result.kind == "query" || result.kind == "editor" || result.kind == "view" ||
                result.kind == "document-data" || result.kind == "toolbar" || result.kind == "theme" || result.kind == "font" || result.kind == "image",
            "Unknown provider kind");
    if (row.contains("module")) Identifier(row.at("module"));
    return result;
}
uint64_t Number(const Json &value) {
    Require(value.is_number_unsigned() || (value.is_number_integer() && value.get<int64_t>() >= 0),
            "Expected an unsigned integer");
    return value.get<uint64_t>();
}
bool Boolean(const Json &row, const char *key, bool fallback = false) {
    if (!row.contains(key))
        return fallback;
    Require(row.at(key).is_boolean(), std::string("Expected a boolean: ") + key);
    return row.at(key).get<bool>();
}
std::vector<EditorField> StateFields(const Json &rows, unsigned depth = 0) {
    Require(rows.is_array() && rows.size() <= 4096 && depth < 32, "Invalid view settings");
    std::vector<EditorField> result;
    for (const auto &row : rows) {
        Require(row.is_object(), "Invalid view setting");
        EditorField field;
        field.key = Text(row.at("key"));
        const auto type = Text(row.at("type"));
        const auto &value = row.at("value");
        if (type == "text") {
            field.type = EDITOR_TEXT_VALUE;
            field.text = Text(value);
        } else if (type == "integer") {
            field.type = EDITOR_INTEGER_VALUE;
            field.integer = Number(value);
        } else if (type == "real") {
            Require(value.is_number(), "Invalid real setting");
            field.type = EDITOR_REAL_VALUE;
            field.real = value.get<double>();
        } else if (type == "record") {
            field.type = EDITOR_RECORD;
            field.fields = StateFields(value, depth + 1);
        } else {
            Require(value.is_array() && value.size() <= 4096, "Invalid setting list");
            if (type == "integers") {
                field.type = EDITOR_INTEGER_LIST;
                for (const auto &n : value)
                    field.integers.push_back(Number(n));
            } else if (type == "texts") {
                field.type = EDITOR_TEXT_LIST;
                for (const auto &s : value)
                    field.texts.push_back(Text(s));
            } else if (type == "reals") {
                field.type = EDITOR_REAL_LIST;
                for (const auto &n : value) {
                    Require(n.is_number(), "Invalid real setting");
                    field.reals.push_back(n.get<double>());
                }
            } else
                throw std::invalid_argument("Unknown view setting type");
        }
        result.push_back(std::move(field));
    }
    const auto borrowed = BorrowEditorFields(result);
    std::vector<EditorField> checked;
    Require(CopyEditorFields(checked, borrowed.data(), uint32_t(borrowed.size())) == EDITOR_OK,
            "Invalid view settings");
    return result;
}
Json StateFields(const std::vector<EditorField> &fields, const Json &previous) {
    Json result = Json::array();
    for (const auto &field : fields) {
        Json row = Json::object();
        if (previous.is_array())
            for (const auto &old : previous)
                if (old.at("key") == field.key) {
                    row = old;
                    break;
                }
        row["key"] = field.key;
        switch (field.type) {
        case EDITOR_TEXT_VALUE:
            row["type"] = "text";
            row["value"] = field.text;
            break;
        case EDITOR_INTEGER_VALUE:
            row["type"] = "integer";
            row["value"] = field.integer;
            break;
        case EDITOR_REAL_VALUE:
            Require(std::isfinite(field.real), "Non-finite view setting");
            row["type"] = "real";
            row["value"] = field.real;
            break;
        case EDITOR_TEXT_LIST:
            row["type"] = "texts";
            row["value"] = field.texts;
            break;
        case EDITOR_INTEGER_LIST:
            row["type"] = "integers";
            row["value"] = field.integers;
            break;
        case EDITOR_REAL_LIST:
            Require(std::all_of(field.reals.begin(), field.reals.end(), [](double n) { return std::isfinite(n); }),
                    "Non-finite view setting");
            row["type"] = "reals";
            row["value"] = field.reals;
            break;
        case EDITOR_RECORD:
            row["type"] = "record";
            row["value"] = StateFields(field.fields, row.value("value", Json::array()));
            break;
        default:
            throw std::invalid_argument("Object references cannot be saved in workspace settings");
        }
        result.push_back(std::move(row));
    }
    return result;
}
WorkspaceState ReadWorkspace(const Json &raw) {
    Require(raw.is_object() && Number(raw.at("version")) == 1, "Unsupported workspace version");
    WorkspaceState state;
    state.layout = Text(raw.value("layout", Json("")));
    state.focused = Number(raw.value("focused", Json(0)));
    const auto &config = raw.at("configuration");
    Require(config.is_object(), "Invalid workspace configuration");
    for (const auto &row : Array(config, "views")) {
        WorkspaceViewPlacement v;
        v.type = Identifier(row.at("type"));
        v.role = Text(row.value("role", Json("")));
        v.region = Text(row.value("region", Json("")));
        v.startup = Boolean(row, "startup");
        v.pinned = Boolean(row, "pinned");
        v.keepOnReset = Boolean(row, "keep-on-reset");
        const auto id = Number(row.value("window-id", Json(0)));
        Require(id <= uint64_t(WorkspaceWindowId::Named), "Invalid window identity");
        v.windowId = WorkspaceWindowId(id);
        v.windowName = Text(row.value("window-name", Json("")));
        state.configuration.views.push_back(std::move(v));
    }
    for (const auto &row : Array(config, "splits")) {
        WorkspaceSplit s;
        s.source = Text(row.at("source"));
        s.first = Text(row.at("first"));
        s.remainder = Text(row.at("remainder"));
        const auto direction = Number(row.at("direction"));
        Require(direction <= 3 && row.at("fraction").is_number(), "Invalid workspace split");
        s.direction = WorkspaceDirection(direction);
        s.fraction = row.at("fraction").get<float>();
        state.configuration.splits.push_back(std::move(s));
    }
    for (const auto &row : Array(config, "regions"))
        state.configuration.regions.push_back(
            {Text(row.at("id")), Text(row.value("anchor-role", Json(""))), Text(row.value("anchor-window", Json("")))});
    for (const auto &row : Array(config, "legacy"))
        state.configuration.legacyWindows.push_back(
            {Text(row.at("name")), Text(row.at("region")), Boolean(row, "contains"), Boolean(row, "keep-on-reset")});
    for (const auto &row : Array(raw, "views")) {
        WorkspaceViewState v;
        v.instance = Number(row.at("instance"));
        v.type = Identifier(row.at("type"));
        v.provider = Text(row.value("provider", Json("")));
        v.module = Text(row.value("module", Json("")));
        v.title = Text(row.value("title", Json("")));
        v.role = Text(row.value("role", Json("")));
        v.tool = Text(row.value("tool", Json("")));
        v.open = Boolean(row, "open", true);
        v.pinned = Boolean(row, "pinned");
        v.single = Boolean(row, "single");
        const auto version = Number(row.value("state-version", Json(0)));
        Require(version <= UINT32_MAX, "Invalid view state version");
        v.version = uint32_t(version);
        v.settings = StateFields(row.value("settings", Json::array()));
        if (row.contains("placement")) {
            const auto &p = row.at("placement");
            Require(p.is_object(), "Invalid window placement");
            v.placed = true;
            for (const auto *key : {"x", "y", "width", "height"})
                Require(p.at(key).is_number(), "Invalid window position");
            v.x = p.at("x").get<float>();
            v.y = p.at("y").get<float>();
            v.width = p.at("width").get<float>();
            v.height = p.at("height").get<float>();
            const auto dock = Number(p.value("dock", Json(0)));
            Require(dock <= UINT32_MAX, "Invalid dock ID");
            v.dock = unsigned(dock);
        }
        state.views.push_back(std::move(v));
    }
    std::string error;
    Require(ValidateWorkspaceState(state, error), error);
    return state;
}
Json WriteWorkspace(const WorkspaceState &state, Json raw) {
    if (!raw.is_object())
        raw = Json::object();
    raw["version"] = 1;
    raw["layout"] = state.layout;
    raw["focused"] = state.focused;
    auto config = raw.value("configuration", Json::object());
    const auto rows = [&](const char *key, const auto &source, auto make, auto same) {
        Json result = Json::array();
        for (const auto &entry : source) {
            Json row = Json::object();
            for (const auto &old : Array(config, key))
                if (same(old, entry)) {
                    row = old;
                    break;
                }
            make(row, entry);
            result.push_back(std::move(row));
        }
        return result;
    };
    config["views"] = rows(
        "views", state.configuration.views,
        [](auto &r, const auto &v) {
            r["type"] = v.type;
            r["role"] = v.role;
            r["region"] = v.region;
            r["startup"] = v.startup;
            r["pinned"] = v.pinned;
            r["keep-on-reset"] = v.keepOnReset;
            r["window-id"] = int(v.windowId);
            r["window-name"] = v.windowName;
        },
        [](const auto &r, const auto &v) {
            return r.at("type") == v.type && r.value("role", std::string{}) == v.role;
        });
    config["splits"] = rows(
        "splits", state.configuration.splits,
        [](auto &r, const auto &s) {
            r["source"] = s.source;
            r["first"] = s.first;
            r["remainder"] = s.remainder;
            r["direction"] = int(s.direction);
            r["fraction"] = s.fraction;
        },
        [](const auto &r, const auto &s) { return r.at("source") == s.source; });
    config["regions"] = rows(
        "regions", state.configuration.regions,
        [](auto &r, const auto &s) {
            r["id"] = s.id;
            r["anchor-role"] = s.anchorRole;
            r["anchor-window"] = s.anchorWindow;
        },
        [](const auto &r, const auto &s) { return r.at("id") == s.id; });
    config["legacy"] = rows(
        "legacy", state.configuration.legacyWindows,
        [](auto &r, const auto &s) {
            r["name"] = s.name;
            r["region"] = s.region;
            r["contains"] = s.contains;
            r["keep-on-reset"] = s.keepOnReset;
        },
        [](const auto &r, const auto &s) { return r.at("name") == s.name; });
    raw["configuration"] = std::move(config);
    Json views = Json::array();
    for (const auto &v : state.views) {
        Json row = Json::object();
        for (const auto &old : Array(raw, "views"))
            if (old.at("instance") == v.instance) {
                row = old;
                break;
            }
        row["instance"] = v.instance;
        row["type"] = v.type;
        row["provider"] = v.provider;
        row["module"] = v.module;
        row["title"] = v.title;
        row["role"] = v.role;
        row["tool"] = v.tool;
        row["open"] = v.open;
        row["pinned"] = v.pinned;
        row["single"] = v.single;
        row["state-version"] = v.version;
        row["settings"] = StateFields(v.settings, row.value("settings", Json::array()));
        if (v.placed) {
            auto p = row.value("placement", Json::object());
            p["x"] = v.x;
            p["y"] = v.y;
            p["width"] = v.width;
            p["height"] = v.height;
            p["dock"] = v.dock;
            row["placement"] = std::move(p);
        } else
            row.erase("placement");
        views.push_back(std::move(row));
    }
    raw["views"] = std::move(views);
    return raw;
}
Json Parse(const std::string &text) {
    Require(text.size() <= MaximumSize, "Profile exceeds the size limit");
    std::vector<std::set<std::string>> keys;
    size_t nodes = 0;
    auto value = Json::parse(text, [&](int depth, Json::parse_event_t event, Json &value) {
        Require(depth <= 64 && ++nodes <= 65536, "Profile nesting or value count exceeds the limit");
        if (event == Json::parse_event_t::object_start) keys.emplace_back();
        if (event == Json::parse_event_t::key)
            Require(!keys.empty() && keys.back().insert(value.get<std::string>()).second, "Duplicate JSON member");
        if (event == Json::parse_event_t::object_end) keys.pop_back();
        return true;
    });
    Require(value.is_object() && value.contains("version") && value.at("version").is_number_integer() &&
        value.at("version") == 1, "Unsupported profile version");
    return value;
}
}

struct EditorProfile::Data {
    Json raw = {{"version", 1}};
    std::string path;
    Preferences settings;
    std::vector<EditorModuleSetting> modules;
    EditorConfiguration contributions;
    std::unique_ptr<WorkspaceState> workspace;
    Data() = default;
    Data(const Data &other)
        : raw(other.raw), path(other.path), settings(other.settings), modules(other.modules),
          contributions(other.contributions),
          workspace(other.workspace ? std::make_unique<WorkspaceState>(*other.workspace) : nullptr) {}
    Data &operator=(Data &&) noexcept = default;
    void Read() {
        modules.clear(); contributions = {}; settings = Preferences{};
        workspace =
            raw.contains("workspace") ? std::make_unique<WorkspaceState>(ReadWorkspace(raw.at("workspace"))) : nullptr;
        std::set<std::string> ids;
        for (const auto &row : Array(raw, "modules")) {
            auto setting = ModuleSetting(row);
            Require(ids.insert(setting.id).second, "Duplicate module setting: " + setting.id);
            modules.push_back(std::move(setting));
        }
        std::set<std::pair<std::string, std::string>> providers;
        for (const auto &row : Array(raw, "providers")) {
            auto choice = Provider(row);
            Require(providers.insert({choice.kind, choice.id}).second, "Duplicate provider selection: " + choice.id);
            contributions.providers.push_back(std::move(choice));
        }
        std::set<std::pair<std::string, std::string>> bindings;
        for (const auto &row : Array(raw, "bindings")) {
            Require(row.is_object(), "Invalid key binding");
            auto id = Identifier(row.at("action")); auto chord = Text(row.at("chord"));
            if (row.contains("module")) Identifier(row.at("module"));
            Require(bindings.insert({id, chord}).second, "Duplicate key binding: " + id);
            contributions.bindings.emplace_back(std::move(id), std::move(chord));
        }
        ids.clear();
        for (const auto &row : Array(raw, "menus")) {
            Require(row.is_object() && row.at("order").is_number_integer(), "Invalid menu placement");
            auto id = Identifier(row.at("id")); auto location = Identifier(row.at("location"));
            if (row.contains("module")) Identifier(row.at("module"));
            Require(!row.at("order").is_number_unsigned() || row.at("order").get<uint64_t>() <= INT32_MAX, "Invalid menu order");
            const auto order = row.at("order").get<int64_t>();
            Require(order >= INT32_MIN && order <= INT32_MAX && ids.insert(id).second, "Invalid or duplicate menu order");
            contributions.menus.push_back({std::move(id), std::move(location), int(order)});
        }
        ids.clear();
        for (const auto &row : Array(raw, "toolbar-items")) {
            Require(row.is_object(), "Invalid toolbar item");
            ToolbarPlacement item;
            item.id = Identifier(row.at("id"));
            Require(ids.insert(item.id).second, "Duplicate toolbar item");
            if (row.contains("module")) Identifier(row.at("module"));
            if (row.contains("location")) {
                item.location = Identifier(row.at("location"));
                item.fields |= ToolbarLocation;
            }
            if (row.contains("action")) {
                item.action = Identifier(row.at("action"));
                item.fields |= ToolbarAction;
            }
            if (row.contains("title")) {
                item.title = Text(row.at("title"));
                item.fields |= ToolbarTitle;
            }
            if (row.contains("visible")) {
                item.visible = Boolean(row, "visible");
                item.fields |= ToolbarVisibility;
            }
            const auto pair = [&](const char *key, float &a, float &b, uint32_t flag) {
                if (!row.contains(key)) return;
                const auto &v = row.at(key);
                Require(v.is_array() && v.size() == 2 && v[0].is_number() && v[1].is_number(),
                        "Invalid toolbar geometry");
                a = v[0].get<float>();
                b = v[1].get<float>();
                Require(std::isfinite(a) && std::isfinite(b) && (flag != ToolbarSize || (a > 0 && b > 0)),
                        "Invalid toolbar geometry");
                item.fields |= flag;
            };
            pair("position", item.x, item.y, ToolbarPosition);
            pair("size", item.width, item.height, ToolbarSize);
            if (row.contains("arguments")) {
                item.arguments = StateFields(row.at("arguments"));
                item.fields |= ToolbarArguments;
            }
            contributions.toolbarItems.push_back(std::move(item));
        }
        Strings(raw, "module-files"); Strings(raw, "module-directories");
        if (raw.contains("preferences")) {
            const auto &preferences = raw.at("preferences"); Require(preferences.is_object(), "Invalid preferences record");
            if (preferences.contains("values")) {
                const auto &values = preferences.at("values"); Require(values.is_object(), "Invalid preference values");
                std::vector<EditorField> fields;
                for (auto field : settings.Values(false)) if (values.contains(field.key)) {
                    const auto &value = values.at(field.key);
                    if (field.type == EDITOR_TEXT_VALUE) {
                        field.text = Text(value);
                        if (PreferenceNativeText(field.key)) {
                            bool lossy = false; field.text = Utf8ToAnsi(field.text, &lossy);
                            Require(!lossy, "Preference cannot be represented by the runtime: " + field.key);
                        }
                    } else if (field.type == EDITOR_REAL_VALUE) {
                        Require(value.is_number(), "Expected a real preference: " + field.key);
                        field.real = value.get<double>(); Require(std::isfinite(field.real), "Non-finite preference");
                    } else {
                        Require(value.is_number_integer(), "Expected an integer preference: " + field.key);
                        field.integer = value.is_number_unsigned() ? value.get<uint64_t>() : uint64_t(value.get<int64_t>());
                    }
                    fields.push_back(std::move(field));
                }
                std::string error; Require(settings.RestoreValues(fields, error), error);
                settings.settingsSaved = true;
            }
            settings.recentFiles = Strings(preferences, "recent-files");
            settings.dataResources = Strings(preferences, "data-resources");
            if (preferences.contains("paths-saved")) {
                Require(preferences.at("paths-saved").is_boolean(), "Invalid paths-saved value");
                settings.pathsSaved = preferences.at("paths-saved").get<bool>();
            }
            std::set<std::string> categories;
            for (const auto &row : Array(preferences, "paths")) {
                Require(row.is_object(), "Invalid path category"); const auto category = Text(row.at("category"));
                Require(categories.insert(category).second, "Duplicate path category");
                settings.paths.push_back({category, Strings(row, "values")});
            }
        }
    }
};

EditorProfile::EditorProfile() : m_Data(std::make_unique<Data>()) {}
EditorProfile::EditorProfile(const EditorProfile &other) : m_Data(std::make_unique<Data>(*other.m_Data)) {}
EditorProfile &EditorProfile::operator=(const EditorProfile &other) { if (this != &other) m_Data = std::make_unique<Data>(*other.m_Data); return *this; }
EditorProfile::EditorProfile(EditorProfile &&) noexcept = default;
EditorProfile &EditorProfile::operator=(EditorProfile &&) noexcept = default;
EditorProfile::~EditorProfile() = default;
EditorProfile::ReadResult EditorProfile::Load(const std::string &path, std::string &error) {
    try {
        const auto file = std::filesystem::u8path(path);
        if (!std::filesystem::exists(file)) {
            auto next = std::make_unique<Data>(); next->path = std::filesystem::absolute(file).u8string();
            m_Data = std::move(next); error.clear(); return ReadResult::Missing;
        }
        Require(std::filesystem::is_regular_file(file) && std::filesystem::file_size(file) <= MaximumSize, "Invalid profile file or size");
        std::ifstream input(file, std::ios::binary); Require(bool(input), "Cannot read profile");
        const std::string text{std::istreambuf_iterator<char>(input), {}};
        Require(!input.bad(), "Profile read failed");
        return Decode(text, path, error) ? ReadResult::Loaded : ReadResult::Failed;
    } catch (const std::exception &failure) { error = failure.what(); return ReadResult::Failed; }
}
bool EditorProfile::Decode(const std::string &text, const std::string &path, std::string &error) {
    try {
        auto next = std::make_unique<Data>(); next->raw = Parse(text);
        next->path = std::filesystem::absolute(std::filesystem::u8path(path)).u8string(); next->Read();
        m_Data = std::move(next); error.clear(); return true;
    } catch (const std::exception &failure) { error = failure.what(); return false; }
}
bool EditorProfile::Encode(std::string &text, std::string &error) const {
    try { auto value = m_Data->raw.dump(2) + '\n'; Require(value.size() <= MaximumSize, "Profile exceeds the size limit"); text = std::move(value); error.clear(); return true; }
    catch (const std::exception &failure) { error = failure.what(); return false; }
}
const Preferences &EditorProfile::Settings() const { return m_Data->settings; }
const std::vector<EditorModuleSetting> &EditorProfile::Modules() const { return m_Data->modules; }
std::vector<EditorModuleSetting> EditorProfile::ResolvedModules() const {
    auto result = Modules(); for (auto &row : result) if (!row.path.empty()) row.path = Resolved(row.path, m_Data->path); return result;
}
std::vector<std::string> EditorProfile::Files() const {
    auto result = Strings(m_Data->raw, "module-files");
    for (auto &file : result) file = Resolved(file, m_Data->path);
    const auto modules = ResolvedModules();
    result.erase(std::remove_if(result.begin(), result.end(), [&](const auto &file) {
        return std::any_of(modules.begin(), modules.end(), [&](const auto &row) { return !row.enabled && row.path == file; });
    }), result.end());
    return result;
}
std::vector<std::string> EditorProfile::Directories() const {
    auto result = Strings(m_Data->raw, "module-directories"); for (auto &path : result) path = Resolved(path, m_Data->path); return result;
}
EditorConfiguration EditorProfile::Contributions() const {
    auto result = m_Data->contributions;
    const auto &rows = Array(m_Data->raw, "providers");
    result.providers.clear();
    for (const auto &row : rows) {
        const auto module = row.value("module", std::string{});
        if (std::any_of(Modules().begin(), Modules().end(), [&](const auto &entry) { return entry.id == module && !entry.enabled; })) continue;
        result.providers.push_back(Provider(row));
    }
    const auto deferred = [&](const Json &row) {
        const auto module = row.value("module", std::string{});
        return std::any_of(Modules().begin(), Modules().end(), [&](const auto &entry) { return entry.id == module && !entry.enabled; });
    };
    const auto bindings = Array(m_Data->raw, "bindings"), menus = Array(m_Data->raw, "menus");
    result.bindings.clear();
    result.menus.clear();
    result.toolbarItems.clear();
    const auto toolbars = Array(m_Data->raw, "toolbar-items");
    for (size_t i = 0; i < toolbars.size(); ++i)
        if (!deferred(toolbars[i])) result.toolbarItems.push_back(m_Data->contributions.toolbarItems[i]);
    for (size_t i = 0; i < bindings.size(); ++i) if (!deferred(bindings[i])) result.bindings.push_back(m_Data->contributions.bindings[i]);
    for (size_t i = 0; i < menus.size(); ++i) if (!deferred(menus[i])) result.menus.push_back(m_Data->contributions.menus[i]);
    return result;
}
const WorkspaceState *EditorProfile::Workspace() const {
    return m_Data->workspace.get();
}
bool EditorProfile::SetWorkspace(const WorkspaceState &state, std::string &error) {
    try {
        Require(ValidateWorkspaceState(state, error), error);
        Data next = *m_Data;
        next.raw["workspace"] = WriteWorkspace(state, next.raw.value("workspace", Json::object()));
        next.Read();
        *m_Data = std::move(next);
        error.clear();
        return true;
    } catch (const std::exception &failure) {
        error = failure.what();
        return false;
    }
}
bool EditorProfile::SetModule(const EditorModuleSetting &setting, std::string &error) {
    try {
        auto raw = m_Data->raw; auto &rows = raw["modules"]; if (rows.is_null()) rows = Json::array();
        auto found = std::find_if(rows.begin(), rows.end(), [&](const auto &row) { return row.at("id") == setting.id; });
        Json row = found == rows.end() ? Json::object() : *found;
        row["id"] = setting.id; row["enabled"] = setting.enabled;
        row.erase("path"); row.erase("source");
        if (!setting.path.empty()) row["path"] = setting.path;
        if (setting.builtin) row["source"] = "builtin";
        ModuleSetting(row);
        if (found == rows.end()) rows.push_back(std::move(row)); else *found = std::move(row);
        Data next = *m_Data; next.raw = std::move(raw); next.Read(); *m_Data = std::move(next); error.clear(); return true;
    } catch (const std::exception &failure) { error = failure.what(); return false; }
}
bool EditorProfile::AddSources(const std::vector<std::string> &files, const std::vector<std::string> &directories, std::string &error) {
    try {
        auto raw = m_Data->raw;
        for (const auto &pair : {std::make_pair("module-files", files), std::make_pair("module-directories", directories)}) {
            if (pair.second.empty()) continue;
            auto &rows = raw[pair.first]; if (rows.is_null()) rows = Json::array();
            for (const auto &path : pair.second) if (std::none_of(rows.begin(), rows.end(), [&](const auto &row) {
                return Resolved(Text(row), m_Data->path) == Resolved(path, m_Data->path);
            })) rows.push_back(path);
        }
        Data next = *m_Data; next.raw = std::move(raw); next.Read(); *m_Data = std::move(next); error.clear(); return true;
    } catch (const std::exception &failure) { error = failure.what(); return false; }
}
bool EditorProfile::Capture(const Preferences &settings, const EditorConfiguration &configuration,
                            const EditorConfiguration &defaults,
                            const std::function<std::string(const ProviderChoice &)> &owner, std::string &error) {
    try {
        auto raw = m_Data->raw;
        auto &preferences = raw["preferences"]; if (preferences.is_null()) preferences = Json::object();
        auto &values = preferences["values"]; if (values.is_null()) values = Json::object();
        for (const auto &field : settings.Values(false)) {
            if (field.type == EDITOR_TEXT_VALUE) values[field.key] = PreferenceNativeText(field.key) ? AnsiToUtf8(field.text) : field.text;
            else if (field.type == EDITOR_REAL_VALUE) { Require(std::isfinite(field.real), "Non-finite preference"); values[field.key] = field.real; }
            else values[field.key] = int64_t(field.integer);
        }
        preferences["recent-files"] = settings.recentFiles; preferences["data-resources"] = settings.dataResources;
        preferences["paths-saved"] = settings.pathsSaved;
        Json paths = Json::array();
        for (const auto &category : settings.paths) {
            Json row = {{"category", category.first}, {"values", category.second}};
            for (const auto &old : Array(preferences, "paths")) if (old.at("category") == category.first) { row = old; row["values"] = category.second; break; }
            paths.push_back(std::move(row));
        }
        preferences["paths"] = std::move(paths);
        Json providers = Json::array();
        for (const auto &old : Array(raw, "providers")) if (owner && owner(Provider(old)).empty() &&
            std::none_of(configuration.providers.begin(), configuration.providers.end(), [&](const auto &choice) {
                return old.at("kind") == choice.kind && old.at("id") == choice.id &&
                    std::none_of(defaults.providers.begin(), defaults.providers.end(), [&](const auto &base) {
                        return base.kind == choice.kind && base.id == choice.id && base.provider == choice.provider;
                    });
            })) providers.push_back(old);
        for (const auto &choice : configuration.providers) {
            if (std::any_of(defaults.providers.begin(), defaults.providers.end(), [&](const auto &base) {
                return base.kind == choice.kind && base.id == choice.id && base.provider == choice.provider;
            })) continue;
            Json row = Json::object();
            for (const auto &old : Array(raw, "providers")) if (old.at("kind") == choice.kind && old.at("id") == choice.id) { row = old; break; }
            row["kind"] = choice.kind; row["id"] = choice.id; row["provider"] = choice.provider;
            const auto module = owner ? owner(choice) : std::string{};
            if (!module.empty()) row["module"] = module;
            providers.push_back(std::move(row));
        }
        raw["providers"] = std::move(providers);
        Json bindings = Json::array();
        for (const auto &old : Array(raw, "bindings")) if (owner && owner({"action", Text(old.at("action")), {}}).empty() &&
            std::none_of(configuration.bindings.begin(), configuration.bindings.end(), [&](const auto &entry) {
                return old.at("action") == entry.first;
            })) bindings.push_back(old);
        for (const auto &binding : configuration.bindings) {
            Json row = Json::object();
            for (const auto &old : Array(raw, "bindings")) if (old.at("action") == binding.first &&
                (row.empty() || old.at("chord") == binding.second)) row = old;
            row["action"] = binding.first; row["chord"] = binding.second;
            const auto module = owner ? owner({"action", binding.first, {}}) : std::string{};
            if (!module.empty()) row["module"] = module;
            bindings.push_back(std::move(row));
        }
        raw["bindings"] = std::move(bindings);
        Json menus = Json::array();
        for (const auto &old : Array(raw, "menus")) if (owner && owner({"menu", Text(old.at("id")), {}}).empty() &&
            std::none_of(configuration.menus.begin(), configuration.menus.end(), [&](const auto &entry) {
                return old.at("id") == entry.id;
            })) menus.push_back(old);
        for (const auto &menu : configuration.menus) {
            Json row = Json::object(); for (const auto &old : Array(raw, "menus")) if (old.at("id") == menu.id) { row = old; break; }
            row["id"] = menu.id; row["location"] = menu.location; row["order"] = menu.order;
            const auto module = owner ? owner({"menu", menu.id, {}}) : std::string{};
            if (!module.empty()) row["module"] = module;
            menus.push_back(std::move(row));
        }
        raw["menus"] = std::move(menus);
        Json toolbars = Json::array();
        for (const auto &old : Array(raw, "toolbar-items"))
            if (owner && owner({"toolbar-item", Text(old.at("id")), {}}).empty() &&
                std::none_of(configuration.toolbarItems.begin(), configuration.toolbarItems.end(),
                             [&](const auto &item) { return old.at("id") == item.id; }))
                toolbars.push_back(old);
        for (const auto &item : configuration.toolbarItems) {
            Json row = Json::object();
            for (const auto &old : Array(raw, "toolbar-items"))
                if (old.at("id") == item.id) {
                    row = old;
                    break;
                }
            for (const auto *key : {"location", "action", "title", "position", "size", "visible", "arguments"})
                row.erase(key);
            row["id"] = item.id;
            if (item.fields & ToolbarLocation) row["location"] = item.location;
            if (item.fields & ToolbarAction) row["action"] = item.action;
            if (item.fields & ToolbarTitle) row["title"] = item.title;
            if (item.fields & ToolbarPosition) row["position"] = {item.x, item.y};
            if (item.fields & ToolbarSize) row["size"] = {item.width, item.height};
            if (item.fields & ToolbarVisibility) row["visible"] = item.visible;
            if (item.fields & ToolbarArguments) row["arguments"] = StateFields(item.arguments, Json::array());
            const auto module = owner ? owner({"toolbar-item", item.id, {}}) : std::string{};
            if (!module.empty()) row["module"] = module;
            toolbars.push_back(std::move(row));
        }
        raw["toolbar-items"] = std::move(toolbars);
        Data next = *m_Data; next.raw = std::move(raw); next.Read(); next.settings = settings;
        *m_Data = std::move(next); error.clear(); return true;
    } catch (const std::exception &failure) { error = failure.what(); return false; }
}
} // namespace vdev
