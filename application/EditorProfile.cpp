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
    Require(result.kind == "command" || result.kind == "query" || result.kind == "editor" || result.kind == "view", "Unknown provider kind");
    if (row.contains("module")) Identifier(row.at("module"));
    return result;
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
    void Read() {
        modules.clear(); contributions = {}; settings = Preferences{};
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
    result.bindings.clear(); result.menus.clear();
    for (size_t i = 0; i < bindings.size(); ++i) if (!deferred(bindings[i])) result.bindings.push_back(m_Data->contributions.bindings[i]);
    for (size_t i = 0; i < menus.size(); ++i) if (!deferred(menus[i])) result.menus.push_back(m_Data->contributions.menus[i]);
    return result;
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
        Data next = *m_Data; next.raw = std::move(raw); next.Read(); next.settings = settings;
        *m_Data = std::move(next); error.clear(); return true;
    } catch (const std::exception &failure) { error = failure.what(); return false; }
}
} // namespace vdev
