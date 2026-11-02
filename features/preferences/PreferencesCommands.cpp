#include "features/preferences/PreferencesCommands.h"
#include "core/document/Document.h"
#include "core/level/SetupCommands.h"
#include "core/runtime/CkRuntimeHost.h"
#include "core/runtime/SdkCompat.h"
#include "core/runtime/TextEncoding.h"
#include <algorithm>
#include <chrono>
#include <cctype>
#include <filesystem>
#include <set>
#include <stdexcept>

namespace vdev {
namespace {
const EditorValue *Argument(const EditorRequest &request, const char *key) {
    for (uint32_t i = 0; i < request.count; ++i)
        if (std::string(request.values[i].key.data, request.values[i].key.size) == key) return request.values + i;
    return nullptr;
}
EditorStatus Write(EditorSink sink, const std::vector<EditorField> &fields) {
    const auto values = BorrowEditorFields(fields);
    return sink.write ? sink.write(sink.data, values.data(), uint32_t(values.size())) : EDITOR_INVALID;
}
std::vector<EditorField> Projection(const Preferences &preferences, bool generalOnly = true) {
    auto fields = preferences.Values(generalOnly);
    for (auto &field : fields) if (PreferenceNativeText(field.key)) field.text = AnsiToUtf8(field.text);
    return fields;
}
std::string Lower(std::string text) {
    for (auto &c : text) c = char(std::tolower(static_cast<unsigned char>(c)));
    return text;
}
void SetNative(CKContext *context, const Preferences &preferences, const std::vector<char> &format) {
    const auto mode = context->GetFileWriteMode();
    context->SetFileWriteMode(static_cast<CK_FILE_WRITEMODE>(preferences.Bool("File Options\\Compressed") ?
        mode | CKFILE_WHOLECOMPRESSED : mode & ~CKFILE_WHOLECOMPRESSED));
    context->SetCompressionLevel(std::clamp(preferences.Int("File Options\\Compression Level"), 1, 9));
    context->SetGlobalImagesSaveOptions(static_cast<CK_TEXTURE_SAVEOPTIONS>(preferences.Int("File Options\\Textures")));
    context->SetGlobalSoundsSaveOptions(static_cast<CK_SOUND_SAVEOPTIONS>(preferences.Int("File Options\\Sounds")));
    if (!format.empty()) context->SetGlobalImagesSaveFormat(reinterpret_cast<CKBitmapProperties *>(const_cast<char *>(format.data())));
    SetQuantizationSamplingFactor(std::clamp(preferences.Int("CK2 Settings\\Quantization Sampling Factor"), 1, 100));
    // This value belongs to Behavior Link Properties and is retained by the
    // General Preferences command, including when those properties change.
    if (auto *manager = context->GetBehaviorManager()) manager->SetBehaviorMaxIteration(std::max(1, preferences.Int("BehaviorManager\\MaxIteration")));
}
struct RuntimeSettings {
    CKContext *context;
    CK_FILE_WRITEMODE mode;
    int compression, quantization, iterations;
    CK_TEXTURE_SAVEOPTIONS images;
    CK_SOUND_SAVEOPTIONS sounds;
    std::vector<char> format;
    explicit RuntimeSettings(CKContext *context) : context(context), mode(context->GetFileWriteMode()),
        compression(context->GetCompressionLevel()), quantization(GetQuantizationSamplingFactor()),
        iterations(context->GetBehaviorManager() ? context->GetBehaviorManager()->GetBehaviorMaxIteration() : 0),
        images(context->GetGlobalImagesSaveOptions()), sounds(context->GetGlobalSoundsSaveOptions()) {
        auto *properties = context->GetGlobalImagesSaveFormat();
        if (properties && properties->m_Size >= sizeof(CKBitmapProperties))
            format.assign(reinterpret_cast<const char *>(properties), reinterpret_cast<const char *>(properties) + properties->m_Size);
    }
    void Restore() const {
        context->SetFileWriteMode(mode); context->SetCompressionLevel(compression);
        context->SetGlobalImagesSaveOptions(images); context->SetGlobalSoundsSaveOptions(sounds);
        if (!format.empty()) context->SetGlobalImagesSaveFormat(reinterpret_cast<CKBitmapProperties *>(const_cast<char *>(format.data())));
        SetQuantizationSamplingFactor(quantization);
        if (auto *manager = context->GetBehaviorManager()) manager->SetBehaviorMaxIteration(iterations);
    }
};
} // namespace
struct PreferencesCommands::Data {
    EditorApplication &editor;
    CkRuntimeHost &runtime;
    Preferences &preferences;
    std::function<bool()> available;
    Save save;
    std::function<void()> changed;
    std::vector<EditorField> values, displayValues, catalog;
    std::set<EditorView> readers;
    uint64_t revision = 1, catalogRevision = 1, generation = UINT64_MAX;
    bool catalogDirty = true, stopped = false;
    std::string startedEngine;
    std::chrono::steady_clock::time_point sampled{};
    Data(EditorApplication &editor, CkRuntimeHost &runtime, Preferences &preferences, std::function<bool()> available,
         Save save, std::function<void()> changed) : editor(editor), runtime(runtime), preferences(preferences),
         available(std::move(available)), save(std::move(save)), changed(std::move(changed)),
         startedEngine(AnsiToUtf8(preferences.Text("CK2 Settings\\Render Engine"))) {}
    bool Ready() const { return !stopped && editor.NativeAvailable() && (!available || available()) && !editor.HasEdits() && runtime.Context(); }
    void Sync() {
        if (!Ready()) return;
        auto next = Projection(preferences);
        const auto current = runtime.Generation();
        if (current != generation) { generation = current; catalogDirty = true; ++catalogRevision; ++revision; }
        if (next != values) { values = std::move(next); ++revision; }
        auto display = Projection(preferences, false);
        if (display != displayValues) { displayValues = std::move(display); ++revision; }
    }
    void ReadCatalog() {
        if (!catalogDirty || !Ready()) return;
        EditorField drivers{"drivers", EDITOR_RECORD}, engines{"engines", EDITOR_RECORD}, formats{"formats", EDITOR_RECORD};
        if (auto *render = runtime.Context()->GetRenderManager())
            for (int i = 0; i < render->GetRenderDriverCount(); ++i) {
                auto *driver = render->GetRenderDriverDescription(i); if (!driver) continue;
                EditorField row{std::to_string(drivers.fields.size()), EDITOR_RECORD};
                const auto *name = DriverName(driver), *description = DriverDescription(driver);
                row.fields = {{"name", EDITOR_TEXT_VALUE, {}, AnsiToUtf8(name ? name : "")},
                    {"description", EDITOR_TEXT_VALUE, {}, AnsiToUtf8(description ? description : "")}, {"modes", EDITOR_TEXT_LIST}};
                for (int m = 0; m < DisplayModeCount(driver); ++m) {
                    const auto &mode = DisplayModeAt(driver, m);
                    row.fields[2].texts.push_back(std::to_string(mode.Width) + "x" + std::to_string(mode.Height) + "x" +
                        std::to_string(mode.Bpp) + "x" + std::to_string(mode.RefreshRate));
                }
                drivers.fields.push_back(std::move(row));
            }
        auto *plugins = CKGetPluginManager();
        for (int i = 0; plugins && i < plugins->GetPluginCount(CKPLUGIN_RENDERENGINE_DLL); ++i) {
            auto *engine = plugins->GetPluginInfo(CKPLUGIN_RENDERENGINE_DLL, i);
            auto *dll = engine ? plugins->GetPluginDllInfo(engine->m_PluginDllIndex) : nullptr;
            if (!dll || !dll->m_DllFileName.CStr()) continue;
            auto name = AnsiToUtf8(Lower(std::filesystem::path(dll->m_DllFileName.CStr()).stem().string()));
            auto label = name; for (auto &c : label) c = char(std::toupper(static_cast<unsigned char>(c)));
            const auto text = [](const char *value) { return AnsiToUtf8(value ? value : ""); };
            EditorField row{std::to_string(engines.fields.size()), EDITOR_RECORD};
            row.fields = {{"name", EDITOR_TEXT_VALUE, {}, name}, {"label", EDITOR_TEXT_VALUE, {}, label},
                {"version", EDITOR_INTEGER_VALUE, {}, {}, uint64_t(engine->m_PluginInfo.m_Version)},
                {"description", EDITOR_TEXT_VALUE, {}, text(engine->m_PluginInfo.m_Description.CStr())},
                {"author", EDITOR_TEXT_VALUE, {}, text(engine->m_PluginInfo.m_Author.CStr())},
                {"summary", EDITOR_TEXT_VALUE, {}, text(engine->m_PluginInfo.m_Summary.CStr())}};
            engines.fields.push_back(std::move(row));
        }
        for (int i = 0; plugins && i < plugins->GetPluginCount(CKPLUGIN_BITMAP_READER); ++i) {
            auto *reader = plugins->GetPluginInfo(CKPLUGIN_BITMAP_READER, i);
            if (!reader || !reader->m_PluginInfo.m_Extension.m_Data[0] ||
                (reader->m_ReadersInfo && !(reader->m_ReadersInfo->m_ReaderFlags & CK_DATAREADER_FILESAVE))) continue;
            const std::string extension = reader->m_PluginInfo.m_Extension.m_Data;
            const auto *summary = reader->m_PluginInfo.m_Summary.CStr();
            EditorField row{std::to_string(formats.fields.size()), EDITOR_RECORD};
            row.fields = {{"extension", EDITOR_TEXT_VALUE, {}, extension}, {"label", EDITOR_TEXT_VALUE, {}, AnsiToUtf8(summary ? summary : "") + " (" + extension + ")"}};
            formats.fields.push_back(std::move(row));
        }
        catalog = {drivers, engines, formats, {"engine", EDITOR_TEXT_VALUE, {}, startedEngine}, {"version", EDITOR_INTEGER_VALUE, {}, {}, CKGetVersion()}};
        catalogDirty = false; ++catalogRevision;
    }
    static EditorStatus State(void *data, const EditorHost *, const EditorRequest *) {
        return static_cast<Data *>(data)->Ready() ? EDITOR_OK : EDITOR_REFUSED;
    }
    static EditorStatus Explorer(void *data, const EditorHost *, const EditorRequest *request, EditorSink sink) {
        auto &self = *static_cast<Data *>(data);
        if (self.stopped) return EDITOR_REFUSED;
        std::vector<EditorField> record;
        if (CopyEditorFields(record, Argument(*request, "value"), 1) != EDITOR_OK || record.size() != 1) return EDITOR_INVALID;
        const auto &fields = record.front().fields;
        Preferences next = self.preferences;
        std::set<std::string> keys;
        for (const auto &field : fields) {
            const bool kinds = field.key == "Schematic Explorer\\Show In List";
            const bool filter = field.key == "Schematic Explorer\\Parameter Filter";
            const bool boolean = field.key == "Schematic Explorer\\Parse All Items" ||
                field.key == "Schematic Explorer\\Tree View" || field.key == "Schematic Explorer\\Derived";
            if ((!kinds && !filter && !boolean) || field.type != EDITOR_INTEGER_VALUE ||
                !keys.insert(field.key).second || (kinds && (field.integer & ~0x13FFu)) ||
                (filter && field.integer > 2) || (boolean && field.integer > 1)) return EDITOR_INVALID;
            next.SetInt(field.key, int(field.integer));
        }
        if (next.Values(false) == self.preferences.Values(false)) return EDITOR_NO_CHANGE;
        self.preferences = std::move(next);
        self.values = Projection(self.preferences);
        self.displayValues = Projection(self.preferences, false);
        ++self.revision;
        return EDITOR_OK;
    }
    static EditorStatus Apply(void *data, const EditorHost *, const EditorRequest *request, EditorSink sink) {
        auto &self = *static_cast<Data *>(data);
        if (!self.Ready() || !self.save) return EDITOR_REFUSED;
        self.Sync();
        if (Argument(*request, "revision")->integer != self.revision) return EDITOR_STALE;
        std::vector<EditorField> record;
        if (CopyEditorFields(record, Argument(*request, "value"), 1) != EDITOR_OK || record.size() != 1) return EDITOR_INVALID;
        auto fields = std::move(record.front().fields);
        for (auto &field : fields) if (PreferenceNativeText(field.key)) {
            bool lossy = false; field.text = Utf8ToAnsi(field.text, &lossy); if (lossy) return EDITOR_INVALID;
        }
        Preferences next = self.preferences;
        std::string error;
        if (!next.SetValues(fields, error)) { Write(sink, {{"message", EDITOR_TEXT_VALUE, {}, error}}); return EDITOR_INVALID; }
        if (next.Values() == self.preferences.Values()) return EDITOR_NO_CHANGE;
        const auto before = self.preferences;
        bool saved = false, applied = false;
        const bool completed = GuardedCall([&] {
            auto *context = self.runtime.Context();
            const auto format = next.Text("File Options\\Texture Format");
            auto bytes = format.empty() ? std::vector<char>{} : MakeSaveFormat(context, format);
            if (!format.empty() && bytes.empty() && (format != before.Text("File Options\\Texture Format") ||
                (next.Int("File Options\\Textures") == 2 && before.Int("File Options\\Textures") != 2)))
                throw std::runtime_error("The selected image format cannot be saved by the current runtime.");
            const RuntimeSettings runtimeBefore(context);
            if (!bytes.empty() && runtimeBefore.format.empty()) throw std::runtime_error("The previous image save format cannot be restored.");
            try {
                SetNative(context, next, bytes);
                self.preferences = next;
                if (self.changed) self.changed();
                saved = self.save(next, error);
                if (!saved) throw std::runtime_error(error.empty() ? "Preferences could not be saved." : error);
                applied = true;
            } catch (...) {
                self.preferences = before;
                runtimeBefore.Restore();
                if (self.changed) self.changed();
                throw;
            }
        }, &self.runtime, "Apply General Preferences");
        self.Sync();
        if (!completed || !saved || !applied) {
            Write(sink, {{"message", EDITOR_TEXT_VALUE, {}, error.empty() ? "Preferences could not be applied or saved." : error}});
            return EDITOR_ERROR;
        }
        return EDITOR_OK;
    }
};
PreferencesCommands::PreferencesCommands(EditorApplication &editor, CkRuntimeHost &runtime, Preferences &preferences,
    std::function<bool()> available, Save save, std::function<void()> changed)
    : m_Data(std::make_unique<Data>(editor, runtime, preferences, std::move(available), std::move(save), std::move(changed))) {}
PreferencesCommands::~PreferencesCommands() = default;
void PreferencesCommands::Poll() {
    auto &self = *m_Data;
    for (auto it = self.readers.begin(); it != self.readers.end();)
        if (self.editor.Scope(*it).view != *it) it = self.readers.erase(it); else ++it;
    if (self.stopped || self.readers.empty()) return;
    const auto now = std::chrono::steady_clock::now();
    if (now - self.sampled < std::chrono::milliseconds(250)) return;
    self.sampled = now; self.Sync(); self.ReadCatalog();
}
void PreferencesCommands::InvalidateCatalog() { m_Data->catalogDirty = true; ++m_Data->catalogRevision; ++m_Data->revision; }
void PreferencesCommands::Stop() { m_Data->stopped = true; m_Data->readers.clear(); }
bool PreferencesCommands::ApplyRuntimeSettings(std::string &error) {
    if (!m_Data->Ready()) { error = "The runtime is busy."; return false; }
    return GuardedCall([&] {
        const auto &p = m_Data->preferences;
        const auto format = p.Text("File Options\\Texture Format");
        SetNative(m_Data->runtime.Context(), p, format.empty() ? std::vector<char>{} : MakeSaveFormat(m_Data->runtime.Context(), format));
    }, &m_Data->runtime, "Apply runtime preferences");
}
SceneEnvironment PreferencesCommands::NewSceneEnvironment() const {
    const auto &p = m_Data->preferences;
    const std::string prefix = "3D Layout\\Environment\\";
    SceneEnvironment environment;
    environment.ambient = static_cast<CKDWORD>(std::strtoul(p.Text(prefix + "Ambient Light Color").c_str(), nullptr, 10));
    environment.background = static_cast<CKDWORD>(std::strtoul(p.Text(prefix + "Background Color").c_str(), nullptr, 10));
    environment.fogColor = static_cast<CKDWORD>(std::strtoul(p.Text(prefix + "Fog Color").c_str(), nullptr, 10));
    environment.fogMode = p.Int(prefix + "Fog Mode"); environment.fogStart = p.Float(prefix + "Fog Start");
    environment.fogEnd = p.Float(prefix + "Fog End"); environment.fogDensity = p.Float(prefix + "Fog Density");
    return environment;
}
const std::vector<ProviderChoice> &PreferencesCommands::DefaultProviders() {
    static const std::vector<ProviderChoice> providers{{"query", "preferences.values", "builtin.preferences"},
        {"query", "preferences.display", "builtin.preferences"},
        {"query", "preferences.catalog", "builtin.preferences"}, {"query", "preferences.state", "builtin.preferences"},
        {"command", "preferences.apply", "builtin.preferences"}, {"view", "preferences.general", "builtin.preference-views"},
        {"command", "preferences.set-explorer-options", "builtin.preferences"},
        {"query", "preferences.dynamic-objects", "builtin.preferences"},
        {"query", "preferences.recent-files", "builtin.preferences"},
        {"command", "preferences.toggle-dynamic-objects", "builtin.preferences"}};
    return providers;
}
bool PreferencesCommands::Register(std::string &error) {
    auto &self = *m_Data;
    self.startedEngine = AnsiToUtf8(self.preferences.Text("CK2 Settings\\Render Engine")); self.Sync();
    const EditorParameter args[] = {{EDITOR_TEXT("revision"), EDITOR_INTEGER_VALUE}, {EDITOR_TEXT("value"), EDITOR_RECORD}};
    const EditorParameter explorer{EDITOR_TEXT("value"), EDITOR_RECORD};
    const EditorCommandDesc commands[] = {
        {sizeof(EditorCommandDesc), EDITOR_TEXT("preferences.set-explorer-options"), EDITOR_TEXT("builtin.preferences"),
            EDITOR_TEXT("Schematic Explorer Options"), &explorer, 1, Data::Explorer, &self, EDITOR_WORKSPACE, EDITOR_SESSION_CHANGE},
        {sizeof(EditorCommandDesc), EDITOR_TEXT("preferences.apply"), EDITOR_TEXT("builtin.preferences"),
            EDITOR_TEXT("Apply General Preferences"), args, 2, Data::Apply, &self, EDITOR_APPLICATION, EDITOR_RUNTIME_CONTROL, Data::State},
        {sizeof(EditorCommandDesc), EDITOR_TEXT("preferences.toggle-dynamic-objects"), EDITOR_TEXT("builtin.preferences"),
            EDITOR_TEXT("Show/Hide Dynamic Objects"), nullptr, 0,
            [](void *data, const EditorHost *host, const EditorRequest *request, EditorSink sink) {
                auto &self = *static_cast<Data *>(data); self.Sync();
                const EditorValue value{EDITOR_TEXT("3D Layout\\Show Dynamic Objects"), EDITOR_INTEGER_VALUE, {}, {}, uint64_t(!self.preferences.Bool("3D Layout\\Show Dynamic Objects"))};
                const EditorValue args[] = {{EDITOR_TEXT("revision"), EDITOR_INTEGER_VALUE, {}, {}, self.revision},
                    {EDITOR_TEXT("value"), EDITOR_RECORD, {}, {}, 0, {}, {}, {}, 0, {}, {&value, 1}}};
                const EditorRequest next{request->scope, args, 2};
                return host->execute(host->data, EditorString EDITOR_TEXT("preferences.apply"), &next, sink);
            }, &self, EDITOR_APPLICATION, EDITOR_RUNTIME_CONTROL, Data::State}};
    const EditorParameter valueFields[] = {{EDITOR_TEXT("revision"), EDITOR_INTEGER_VALUE}, {EDITOR_TEXT("value"), EDITOR_RECORD}};
    const EditorParameter catalogFields[] = {{EDITOR_TEXT("revision"), EDITOR_INTEGER_VALUE}, {EDITOR_TEXT("drivers"), EDITOR_RECORD},
        {EDITOR_TEXT("engines"), EDITOR_RECORD}, {EDITOR_TEXT("formats"), EDITOR_RECORD}, {EDITOR_TEXT("engine"), EDITOR_TEXT_VALUE}, {EDITOR_TEXT("version"), EDITOR_INTEGER_VALUE}};
    const EditorParameter available{EDITOR_TEXT("available"), EDITOR_INTEGER_VALUE};
    const EditorParameter checked{EDITOR_TEXT("checked"), EDITOR_INTEGER_VALUE};
    const EditorParameter recent{EDITOR_TEXT("paths"), EDITOR_TEXT_LIST};
    const EditorQueryDesc queries[] = {
        {sizeof(EditorQueryDesc), EDITOR_TEXT("preferences.display"), EDITOR_TEXT("builtin.preferences"), nullptr, 0,
            [](void *data, const EditorHost *, const EditorRequest *request, EditorSink sink) {
                auto &self = *static_cast<Data *>(data);
                if (request->scope.view) self.readers.insert(request->scope.view);
                self.Sync();
                EditorField value{"value", EDITOR_RECORD}; value.fields = self.displayValues;
                return Write(sink, {{"revision", EDITOR_INTEGER_VALUE, {}, {}, self.revision}, value});
            }, &self, EDITOR_QUERY_VERSIONED, EDITOR_APPLICATION, valueFields, 2,
            [](void *data, const EditorHost *) { return static_cast<Data *>(data)->revision; }},
        {sizeof(EditorQueryDesc), EDITOR_TEXT("preferences.recent-files"), EDITOR_TEXT("builtin.preferences"), nullptr, 0,
            [](void *data, const EditorHost *, const EditorRequest *, EditorSink sink) {
                EditorField paths{"paths", EDITOR_TEXT_LIST};
                paths.texts = static_cast<Data *>(data)->preferences.recentFiles;
                return Write(sink, {paths});
            }, &self, EDITOR_QUERY_EACH_READ, EDITOR_APPLICATION, &recent, 1},
        {sizeof(EditorQueryDesc), EDITOR_TEXT("preferences.values"), EDITOR_TEXT("builtin.preferences"), nullptr, 0,
            [](void *data, const EditorHost *, const EditorRequest *request, EditorSink sink) {
                auto &self = *static_cast<Data *>(data);
                if (request->scope.view) self.readers.insert(request->scope.view);
                self.Sync();
                EditorField value{"value", EDITOR_RECORD}; value.fields = self.values;
                return Write(sink, {{"revision", EDITOR_INTEGER_VALUE, {}, {}, self.revision}, value});
            }, &self, EDITOR_QUERY_VERSIONED, EDITOR_APPLICATION, valueFields, 2,
            [](void *data, const EditorHost *) { return static_cast<Data *>(data)->revision; }},
        {sizeof(EditorQueryDesc), EDITOR_TEXT("preferences.catalog"), EDITOR_TEXT("builtin.preferences"), nullptr, 0,
            [](void *data, const EditorHost *, const EditorRequest *request, EditorSink sink) {
                auto &self = *static_cast<Data *>(data); if (request->scope.view) self.readers.insert(request->scope.view);
                self.ReadCatalog(); if (self.catalog.empty()) return EDITOR_REFUSED;
                auto fields = self.catalog; fields.insert(fields.begin(), {"revision", EDITOR_INTEGER_VALUE, {}, {}, self.catalogRevision});
                return Write(sink, fields);
            }, &self, EDITOR_QUERY_VERSIONED, EDITOR_APPLICATION, catalogFields, 6,
            [](void *data, const EditorHost *) { return static_cast<Data *>(data)->catalogRevision; }},
        {sizeof(EditorQueryDesc), EDITOR_TEXT("preferences.state"), EDITOR_TEXT("builtin.preferences"), nullptr, 0,
            [](void *data, const EditorHost *, const EditorRequest *, EditorSink sink) {
                return Write(sink, {{"available", EDITOR_INTEGER_VALUE, {}, {}, uint64_t(static_cast<Data *>(data)->Ready())}});
            }, &self, EDITOR_QUERY_EACH_READ, EDITOR_APPLICATION, &available, 1},
        {sizeof(EditorQueryDesc), EDITOR_TEXT("preferences.dynamic-objects"), EDITOR_TEXT("builtin.preferences"), nullptr, 0,
            [](void *data, const EditorHost *, const EditorRequest *, EditorSink sink) {
                return Write(sink, {{"checked", EDITOR_INTEGER_VALUE, {}, {}, uint64_t(static_cast<Data *>(data)->preferences.Bool("3D Layout\\Show Dynamic Objects"))}});
            }, &self, EDITOR_QUERY_EACH_READ, EDITOR_APPLICATION, &checked, 1}};
    EditorModuleDesc module{}; module.size = sizeof(module); module.version = EDITOR_VERSION;
    module.id = EDITOR_TEXT("preferences.settings"); module.commands = commands; module.commandCount = uint32_t(std::size(commands));
    module.queries = queries; module.queryCount = uint32_t(std::size(queries));
    return self.editor.Register(module, error);
}
} // namespace vdev
