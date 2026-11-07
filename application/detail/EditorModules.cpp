#include "application/detail/EditorModules.h"
#include "application/detail/EditorValidation.h"
#include "application/EditorModules.h"
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <set>
#include <type_traits>
#if defined(_WIN32)
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace vdev::detail {
namespace {
bool ModuleFile(const std::string &a, const std::string &b) {
    if (a == b) return true;
    if (a.empty() || b.empty()) return false;
    std::error_code error;
    return std::filesystem::equivalent(std::filesystem::u8path(a), std::filesystem::u8path(b), error) && !error;
}
void CloseLibrary(void *library) {
    if (!library) return;
#ifdef _WIN32
    FreeLibrary(static_cast<HMODULE>(library));
#else
    dlclose(library);
#endif
}
template <class T> bool Schema(const T &a, const T &b) {
    if (a.count != b.count) return false;
    for (uint32_t i = 0; i < a.count; ++i)
        if (a.parameters[i].type != b.parameters[i].type || a.parameters[i].optional != b.parameters[i].optional ||
            a.parameters[i].nullable != b.parameters[i].nullable ||
            String(a.parameters[i].key) != String(b.parameters[i].key))
            return false;
    return true;
}
} // namespace

EditorString Module::Text(EditorString text, bool identifier) {
    strings.push_back(identifier ? Id(text) : String(text));
    return EditorText(strings.back());
}

const EditorParameter *Module::Parameters(const EditorParameter *source, uint32_t count) {
    Array(source, count);
    if (count > 64) throw std::invalid_argument("Too many parameters or fields");
    parameters.emplace_back();
    std::set<std::string> keys;
    for (uint32_t i = 0; i < count; ++i) {
        auto p = source[i];
        const auto key = String(p.key);
        if (key.empty() || !keys.insert(key).second ||
            (!ValueType(p.type) || p.optional > 1 || p.nullable > 1 || (p.nullable && p.type != EDITOR_OBJECT)))
            throw std::invalid_argument("Invalid parameter schema");
        p.key = Text(p.key);
        parameters.back().push_back(p);
    }
    return parameters.back().data();
}

Module::Module(const EditorModuleDesc &source, const std::string &origin) {
    if (source.size != sizeof(source) || source.version != EDITOR_VERSION)
        throw std::invalid_argument("Module version/size mismatch");
    id = Text(source.id, true);
    data = source.data;
    stop = source.stop;
    destroy = source.destroy;
    start = source.start;
    update = source.update;
    document = source.document;
    Array(source.dependencies, source.dependencyCount);
    for (uint32_t i = 0; i < source.dependencyCount; ++i)
        dependencies.push_back(Text(source.dependencies[i], true));
    Array(source.optionalDependencies, source.optionalDependencyCount);
    for (uint32_t i = 0; i < source.optionalDependencyCount; ++i)
        optionalDependencies.push_back(Text(source.optionalDependencies[i], true));
    Array(source.commands, source.commandCount);
    Array(source.queries, source.queryCount);
    Array(source.properties, source.propertyCount);
    Array(source.editors, source.editorCount);
    Array(source.views, source.viewCount);
    Array(source.menus, source.menuCount);
    Array(source.bindings, source.bindingCount);
    Array(source.locations, source.locationCount);
    Array(source.tools, source.toolCount);
    Array(source.toolbarItems, source.toolbarItemCount);
    Array(source.toolbars, source.toolbarCount);
    Array(source.themes, source.themeCount);
    Array(source.fonts, source.fontCount);
    Array(source.images, source.imageCount);
    Array(source.documentData, source.documentDataCount);
    for (uint32_t i = 0; i < source.documentDataCount; ++i) {
        auto d = source.documentData[i];
        if (d.size != sizeof(d) || !d.version) throw std::invalid_argument("Invalid document data descriptor");
        d.id = Text(d.id, true);
        d.provider = Text(d.provider, true);
        if (IsNativeDocumentDataType(String(d.id)))
            throw std::invalid_argument("Reserved native document data type");
        documentData.push_back(d);
    }
    if (source.themeCount > 256 || source.fontCount > 1024 || source.imageCount > 4096)
        throw std::invalid_argument("Too many appearance resources");
    for (uint32_t i = 0; i < source.commandCount; ++i) {
        auto d = source.commands[i];
        if (d.size != sizeof(d) || !d.run || d.area < EDITOR_DOCUMENT || d.area > EDITOR_APPLICATION ||
            d.effect < EDITOR_DOCUMENT_EDIT || d.effect > EDITOR_RUNTIME_CONTROL ||
            (d.preview && d.effect != EDITOR_DOCUMENT_EDIT) ||
            (d.commitPreview && (!d.preview || d.effect != EDITOR_DOCUMENT_EDIT)) ||
            ((d.effect == EDITOR_DOCUMENT_EDIT || d.effect == EDITOR_DOCUMENT_CHANGE) && d.area != EDITOR_DOCUMENT))
            throw std::invalid_argument("Invalid command descriptor");
        d.id = Text(d.id, true);
        d.provider = Text(d.provider, true);
        d.title = Text(d.title);
        d.view = Text(d.view, d.view.size != 0);
        if (!d.title.size) throw std::invalid_argument("Missing command title");
        d.parameters = Parameters(d.parameters, d.count);
        commands.push_back(d);
    }
    for (uint32_t i = 0; i < source.queryCount; ++i) {
        auto d = source.queries[i];
        if (d.size != sizeof(d) || !d.read ||
            (d.refresh < EDITOR_QUERY_DOCUMENT || d.refresh > EDITOR_QUERY_VERSIONED) ||
            bool(d.revision) != (d.refresh == EDITOR_QUERY_VERSIONED) ||
            (d.flags & ~uint32_t(EDITOR_QUERY_RETAINED_REFERENCES)) ||
            (d.area < EDITOR_DOCUMENT || d.area > EDITOR_APPLICATION))
            throw std::invalid_argument("Invalid query descriptor");
        d.id = Text(d.id, true);
        d.provider = Text(d.provider, true);
        d.parameters = Parameters(d.parameters, d.count);
        d.fields = Parameters(d.fields, d.fieldCount);
        queries.push_back(d);
    }
    for (uint32_t i = 0; i < source.propertyCount; ++i) {
        auto d = source.properties[i];
        if (d.size != sizeof(d)) throw std::invalid_argument("Invalid property descriptor");
        d.id = Text(d.id, true);
        d.label = Text(d.label);
        d.query = Text(d.query, true);
        d.field = Text(d.field);
        d.command = Text(d.command, true);
        d.argument = Text(d.argument);
        d.editor = Text(d.editor, true);
        d.choices = Text(d.choices, d.choices.size != 0);
        properties.push_back(d);
    }
    for (uint32_t i = 0; i < source.editorCount; ++i) {
        auto d = source.editors[i];
        if (d.size != sizeof(d) || !d.draw || !ValueType(d.type))
            throw std::invalid_argument("Invalid editor descriptor");
        d.id = Text(d.id, true);
        d.provider = Text(d.provider, true);
        d.title = Text(d.title);
        editors.push_back(d);
    }
    for (uint32_t i = 0; i < source.viewCount; ++i) {
        if (source.views[i].size != sizeof(EditorViewDesc)) throw std::invalid_argument("Invalid view descriptor size");
        auto d = source.views[i];
        if (!d.create || !d.draw || !d.destroy || d.kind < EDITOR_WINDOW_VIEW || d.kind > EDITOR_OVERLAY_VIEW ||
            d.singleInstance > 1 || d.documentBound > 1 || (d.documentBound && d.area != EDITOR_DOCUMENT) ||
            d.area < EDITOR_DOCUMENT || d.area > EDITOR_APPLICATION || (d.count && !d.navigate) ||
            bool(d.saveState) != bool(d.restoreState) || bool(d.stateVersion) != bool(d.saveState) ||
            (d.pick && d.kind != EDITOR_WINDOW_VIEW))
            throw std::invalid_argument("Invalid view descriptor");
        d.id = Text(d.id, true);
        d.provider = Text(d.provider, true);
        d.title = Text(d.title);
        d.parameters = Parameters(d.parameters, d.count);
        if ((!d.inspect && (d.fieldCount || d.inspectRevision)) || (d.inspect && !d.fieldCount))
            throw std::invalid_argument("Invalid view inspection");
        d.fields = Parameters(d.fields, d.fieldCount);
        d.settings = Parameters(d.settings, d.settingCount);
        d.defaultTool = Text(d.defaultTool, d.defaultTool.size != 0);
        if (d.settingCount && (!d.saveState || d.documentBound || d.kind != EDITOR_WINDOW_VIEW))
            throw std::invalid_argument("Invalid persistent view settings");
        for (uint32_t j = 0; j < d.settingCount; ++j)
            if (d.settings[j].type == EDITOR_OBJECT || d.settings[j].type == EDITOR_OBJECT_LIST)
                throw std::invalid_argument("Object references cannot be persistent view settings");
        views.push_back(d);
    }
    for (uint32_t i = 0; i < source.menuCount; ++i) {
        auto d = source.menus[i];
        d.id = Text(d.id, true);
        d.action = Text(d.action, true);
        d.location = Text(d.location, true);
        d.checked = Text(d.checked, d.checked.size != 0);
        d.label = Text(d.label);
        menus.push_back(d);
    }
    for (uint32_t i = 0; i < source.bindingCount; ++i) {
        auto d = source.bindings[i];
        if (d.phase < EDITOR_KEY_PRESSED || d.phase > EDITOR_KEY_RELEASED)
            throw std::invalid_argument("Invalid input phase");
        d.action = Text(d.action, true);
        d.chord = Text(d.chord);
        d.view = Text(d.view, d.view.size != 0);
        bindings.push_back(d);
    }
    for (uint32_t i = 0; i < source.locationCount; ++i) {
        auto location = source.locations[i];
        location.id = Text(location.id, true);
        location.title = Text(location.title);
        locations.push_back(location);
    }
    for (uint32_t i = 0; i < source.toolCount; ++i) {
        auto d = source.tools[i];
        if (d.size != sizeof(d) || !d.input) throw std::invalid_argument("Invalid view tool");
        d.id = Text(d.id, true);
        d.provider = Text(d.provider, true);
        d.view = Text(d.view, true);
        d.title = Text(d.title);
        if (!d.title.size) throw std::invalid_argument("Missing tool title");
        tools.push_back(d);
    }
    for (auto &tool : tools) {
        EditorCommandDesc command{};
        command.size = sizeof(command);
        command.id = tool.id;
        command.provider = tool.provider;
        command.title = tool.title;
        command.data = &tool;
        command.area = EDITOR_WORKSPACE;
        command.effect = EDITOR_SESSION_CHANGE;
        command.view = tool.view;
        command.run = [](void *data, const EditorHost *host, const EditorRequest *r, EditorSink) {
            return host->selectTool(host->data, r->scope, static_cast<EditorToolDesc *>(data)->id);
        };
        command.state = [](void *data, const EditorHost *host, const EditorRequest *r) {
            const auto &d = *static_cast<EditorToolDesc *>(data);
            return d.state ? d.state(d.data, host, r) : EDITOR_OK;
        };
        commands.push_back(command);
    }
    for (uint32_t i = 0; i < source.toolbarCount; ++i) {
        auto d = source.toolbars[i];
        d.id = Text(d.id, true);
        d.title = Text(d.title);
        d.view = Text(d.view, d.view.size != 0);
        toolbars.push_back(d);
    }
    for (uint32_t i = 0; i < source.toolbarItemCount; ++i) {
        auto d = source.toolbarItems[i];
        if (d.size != sizeof(d) || (!d.draw && !d.action.size) || d.count > 64 || !std::isfinite(d.x) ||
            !std::isfinite(d.y) || !std::isfinite(d.width) || !std::isfinite(d.height) || d.width <= 0 ||
            d.height <= 0 || (d.flags & ~(EDITOR_TOOLBAR_RIGHT | EDITOR_TOOLBAR_STRETCH)))
            throw std::invalid_argument("Invalid toolbar item");
        d.id = Text(d.id, true);
        d.provider = Text(d.provider, true);
        d.location = Text(d.location, true);
        d.title = Text(d.title);
        d.action = Text(d.action, d.action.size != 0);
        d.checked = Text(d.checked, d.checked.size != 0);
        toolbarArguments.emplace_back();
        if (CopyEditorFields(toolbarArguments.back(), d.arguments, d.count) != EDITOR_OK ||
            !EveryField(toolbarArguments.back(),
                        [](const auto &v) { return v.type != EDITOR_OBJECT && v.type != EDITOR_OBJECT_LIST; }))
            throw std::invalid_argument("Invalid toolbar arguments");
        toolbarValues.push_back(BorrowEditorFields(toolbarArguments.back()));
        d.arguments = toolbarValues.back().data();
        toolbarItems.push_back(d);
    }
    for (uint32_t i = 0; i < source.themeCount; ++i) {
        auto d = source.themes[i];
        if (d.size != sizeof(d) || d.count > 4096) throw std::invalid_argument("Invalid theme descriptor");
        d.id = Text(d.id, true);
        d.provider = Text(d.provider, true);
        d.title = Text(d.title);
        d.parent = Text(d.parent, d.parent.size != 0);
        EditorValue record{};
        record.key = EDITOR_TEXT("theme");
        record.type = EDITOR_RECORD;
        record.fields = {d.values, d.count};
        std::vector<EditorField> copied;
        if (CopyEditorFields(copied, &record, 1) != EDITOR_OK) throw std::invalid_argument("Invalid theme values");
        themeFields.push_back(std::move(copied.front().fields));
        for (const auto &f : themeFields.back()) {
            Id(EditorText(f.key));
            const bool color =
                f.key.compare(0, 6, "color.") == 0 && f.type == EDITOR_INTEGER_VALUE && f.integer <= UINT32_MAX;
            const bool metric =
                f.key.compare(0, 7, "metric.") == 0 &&
                ((f.type == EDITOR_REAL_VALUE && f.real >= 0 && f.real <= 10000) ||
                 (f.type == EDITOR_REAL_LIST && f.reals.size() == 2 &&
                  std::all_of(f.reals.begin(), f.reals.end(), [](double v) { return v >= 0 && v <= 10000; })));
            const bool resource = (f.key.compare(0, 5, "font.") == 0 || f.key.compare(0, 6, "image.") == 0) &&
                                  f.type == EDITOR_TEXT_VALUE;
            if (!color && !metric && !resource) throw std::invalid_argument("Invalid theme role: " + f.key);
            if (resource) Id(EditorText(f.text));
        }
        themeValues.push_back(BorrowEditorFields(themeFields.back()));
        d.values = themeValues.back().data();
        themes.push_back(d);
    }
    for (uint32_t i = 0; i < source.fontCount; ++i) {
        auto d = source.fonts[i];
        if (d.size != sizeof(d) || !d.count || d.count > 16 || !std::isfinite(d.height) || d.height < 4 ||
            d.height > 128)
            throw std::invalid_argument("Invalid font descriptor");
        Array(d.files, d.count);
        d.id = Text(d.id, true);
        d.provider = Text(d.provider, true);
        d.title = Text(d.title);
        fontFiles.emplace_back();
        for (uint32_t j = 0; j < d.count; ++j) {
            auto name = String(d.files[j]);
            if (name.empty() && j != 0) throw std::invalid_argument("Only the first font source may be built-in");
            if (!name.empty()) {
                auto path = std::filesystem::u8path(name);
                if (path.is_relative()) {
                    if (origin.empty()) throw std::invalid_argument("Relative font path requires a module library");
                    path = std::filesystem::u8path(origin).parent_path() / path;
                }
                name = path.lexically_normal().u8string();
            }
            fontFiles.back().push_back(Text(EditorText(name)));
        }
        d.files = fontFiles.back().data();
        fonts.push_back(d);
    }
    uint64_t imageBytes = 0;
    for (uint32_t i = 0; i < source.imageCount; ++i) {
        auto d = source.images[i];
        const uint64_t bytes = uint64_t(d.width) * d.height * 4;
        if (d.size != sizeof(d) || !d.width || !d.height || d.width > 16384 || d.height > 16384 ||
            bytes > 64 * 1024 * 1024 || bytes != d.byteCount || !d.pixels || (imageBytes += bytes) > 128 * 1024 * 1024)
            throw std::invalid_argument("Invalid RGBA image descriptor");
        d.id = Text(d.id, true);
        d.provider = Text(d.provider, true);
        d.title = Text(d.title);
        imagePixels.emplace_back(d.pixels, d.pixels + d.byteCount);
        d.pixels = imagePixels.back().data();
        images.push_back(d);
    }
}

void Module::Destroy() {
    dataCallbacks->Close();
    const auto callback = destroy;
    destroy = nullptr;
    if (callback) {
        try {
            callback(data);
        } catch (...) {
        }
    }
}

Module::~Module() {
    dataCallbacks->Close();
    CloseLibrary(library);
}

void RegisteredModules::PrepareActivation(EditorDrawingVersion imguiVersion) {
    auto ordered = ModuleOrder();
    for (auto *m : ordered) {
        for (auto &d : m->editors)
            if (d.imguiVersion.version != imguiVersion.version || d.imguiVersion.ioSize != imguiVersion.ioSize ||
                d.imguiVersion.styleSize != imguiVersion.styleSize ||
                d.imguiVersion.vertexSize != imguiVersion.vertexSize ||
                d.imguiVersion.indexSize != imguiVersion.indexSize)
                throw std::invalid_argument("ImGui editor version mismatch");
        for (auto &d : m->views)
            if (d.imguiVersion.version != imguiVersion.version || d.imguiVersion.ioSize != imguiVersion.ioSize ||
                d.imguiVersion.styleSize != imguiVersion.styleSize ||
                d.imguiVersion.vertexSize != imguiVersion.vertexSize ||
                d.imguiVersion.indexSize != imguiVersion.indexSize)
                throw std::invalid_argument("ImGui view version mismatch");
        for (auto &d : m->toolbarItems)
            if (d.draw &&
                (d.imguiVersion.version != imguiVersion.version || d.imguiVersion.ioSize != imguiVersion.ioSize ||
                 d.imguiVersion.styleSize != imguiVersion.styleSize ||
                 d.imguiVersion.vertexSize != imguiVersion.vertexSize ||
                 d.imguiVersion.indexSize != imguiVersion.indexSize))
                throw std::invalid_argument("ImGui toolbar version mismatch");
    }
    activation = std::move(ordered);
}

bool RegisteredModules::Start(const EditorHost *host, std::string &error) {
    if (Busy() || stopped) return false;
    if (started) return true;
    Callback running(callback);
    for (auto *module : activation) {
        module->running = true;
        if (!module->start) continue;
        EditorStatus status = EDITOR_ERROR;
        std::string message;
        try {
            status = module->start(module->data, host);
        } catch (const std::exception &failure) {
            message = failure.what();
        } catch (...) {
            message = "Startup raised an exception";
        }
        if (status == EDITOR_OK || status == EDITOR_NO_CHANGE) continue;
        error = "Module start failed: " + String(module->id);
        if (!message.empty()) error += ": " + message;
        return false;
    }
    started = true;
    return true;
}

bool RegisteredModules::Update(const EditorHost *host, EditorModulePhase phase,
                               const std::function<void(const EditorResponse &)> &report) {
    if (!started || Busy() ||
        (phase != EDITOR_PREPARE_FRAME && phase != EDITOR_BEFORE_VIEWS && phase != EDITOR_AFTER_VIEWS))
        return false;
    Callback running(callback);
    bool updated = true;
    for (auto *module : activation) {
        if (!module->update) continue;
        try {
            module->update(module->data, host, phase);
        } catch (...) {
            updated = false;
            Report(report, "Module update failed: " + String(module->id));
        }
    }
    return updated;
}

void RegisteredModules::Stop(const EditorHost *host, const std::function<void(const EditorResponse &)> &report) {
    if (stopped || Busy()) return;
    Callback running(callback);
    stopped = true;
    started = false;
    detached.clear();
    for (auto it = activation.rbegin(); it != activation.rend(); ++it) {
        auto &module = **it;
        if (!module.running) continue;
        module.running = false;
        if (!module.stop) continue;
        try {
            module.stop(module.data, host);
        } catch (...) {
            Report(report, "Module stop failed: " + String(module.id));
        }
    }
}

bool RegisteredModules::Document(const EditorHost *host, const EditorDocumentChange &change, std::string &error) {
    // Unstarted modules have no presentation activity to update.
    if (change.phase == EDITOR_DOCUMENT_LOADED && !started && !stopped && !Busy()) return true;
    if (!started || stopped || Busy()) {
        error = "Modules cannot change documents during another lifecycle callback";
        return false;
    }
    Callback running(callback);
    const auto call = [&](Module &module) {
        EditorStatus status = EDITOR_ERROR;
        std::string reason;
        try { status = module.document(module.data, host, &change); }
        catch (const std::exception &failure) { reason = failure.what(); }
        catch (...) { reason = "Document callback raised an exception"; }
        if (status == EDITOR_OK || status == EDITOR_NO_CHANGE) return true;
        if (!error.empty()) error += "; ";
        error += "Module document change failed: " + String(module.id);
        if (!reason.empty()) error += ": " + reason;
        return false;
    };
    if (change.phase == EDITOR_DOCUMENT_DETACH) {
        if (!detached.empty()) { error = "The previous document has not been restored"; return false; }
        // Allocate before any module releases resources. Even a throwing module
        // belongs to recovery, since it may already have changed its own state.
        detached.reserve(activation.size());
        for (auto it = activation.rbegin(); it != activation.rend(); ++it) {
            if (!(*it)->document) continue;
            detached.push_back(*it);
            if (!call(**it)) return false;
        }
        return true;
    }
    bool result = true;
    for (auto *module : activation) {
        const auto previous = std::find(detached.begin(), detached.end(), module);
        if (!module->document || (change.phase == EDITOR_DOCUMENT_RESTORE && previous == detached.end())) continue;
        const bool ok = call(*module);
        result = ok && result;
        if (ok && previous != detached.end()) detached.erase(previous);
    }
    if (change.phase == EDITOR_DOCUMENT_REPLACED) detached.clear();
    return result;
}

void RegisteredModules::Report(const std::function<void(const EditorResponse &)> &report, std::string message) {
    if (report) {
        try {
            report({EDITOR_ERROR, std::move(message)});
        } catch (...) {
        }
    }
}

std::vector<Module *> RegisteredModules::ModuleOrder() const {
    std::map<std::string, Module *> named;
    for (const auto &module : modules)
        if (module->enabled) named.emplace(String(module->id), module.get());
    std::set<Module *> visiting, done;
    std::vector<Module *> order;
    const auto visit = [&](const auto &self, Module *module) -> void {
        if (done.count(module)) return;
        if (!visiting.insert(module).second)
            throw std::invalid_argument("Module dependency cycle: " + String(module->id));
        for (auto dependency : module->dependencies) {
            const auto found = named.find(String(dependency));
            if (found == named.end())
                throw std::invalid_argument("Missing module dependency: " + String(module->id) + " -> " +
                                            String(dependency));
            self(self, found->second);
        }
        for (auto dependency : module->optionalDependencies) {
            const auto found = named.find(String(dependency));
            if (found != named.end()) self(self, found->second);
        }
        visiting.erase(module);
        done.insert(module);
        order.push_back(module);
    };
    for (const auto &module : modules)
        if (module->enabled) visit(visit, module.get());
    return order;
}

void RegisteredModules::DiscardModules(size_t first) {
    discarding = true;
    auto order = activation;
    if (order.empty()) {
        try {
            order = ModuleOrder();
        } catch (...) {
            for (const auto &module : modules)
                order.push_back(module.get());
        }
    }
    for (const auto &module : modules)
        if (std::find(order.begin(), order.end(), module.get()) == order.end()) order.push_back(module.get());
    std::set<Module *> removed;
    for (size_t i = first; i < modules.size(); ++i)
        removed.insert(modules[i].get());
    for (auto it = order.rbegin(); it != order.rend(); ++it)
        if (removed.count(*it)) (*it)->Destroy();
    // All module data is released while every library is still loaded.
    modules.resize(first);
    discarding = false;
}

bool RegisteredModules::Register(const EditorModuleDesc &module, std::string &error) {
    if (discarding) {
        error = "Registration is closed";
        return false;
    }
    try {
        auto copy = std::make_unique<Module>(module, sourceFile);
        copy->file = sourceFile;
        for (const auto &setting : moduleSettings)
            if (setting.id == String(copy->id)) {
                copy->enabled = setting.enabled && (!setting.builtin || copy->file.empty()) &&
                                (setting.path.empty() || ModuleFile(setting.path, copy->file));
            }
        if (!copy->enabled) {
            modules.push_back(std::move(copy));
            return true;
        }
        for (auto &m : modules)
            if (m->enabled && String(m->id) == String(copy->id))
                throw std::invalid_argument("Duplicate module identifier");
        // Validate every candidate against both existing modules and itself.
        auto validate = [&](auto member) {
            using Table = std::remove_reference_t<decltype(copy.get()->*member)>;
            std::map<std::pair<std::string, std::string>, const typename Table::value_type *> seen;
            std::map<std::string, const typename Table::value_type *> schemas;
            auto add = [&](Module &m) {
                if (!m.enabled) return;
                for (auto &d : m.*member) {
                    const auto key = std::make_pair(String(d.id), String(d.provider));
                    if (!seen.emplace(key, &d).second) throw std::invalid_argument("Duplicate contribution provider");
                    auto inserted = schemas.emplace(key.first, &d);
                    if constexpr (std::is_same_v<Table, std::vector<EditorCommandDesc>> ||
                                  std::is_same_v<Table, std::vector<EditorQueryDesc>> ||
                                  std::is_same_v<Table, std::vector<EditorViewDesc>>)
                        if (!inserted.second && !Schema(*inserted.first->second, d))
                            throw std::invalid_argument("Incompatible contribution schema: " + key.first);
                    if constexpr (std::is_same_v<Table, std::vector<EditorToolDesc>>) {
                        if (!inserted.second && String(d.view) != String(inserted.first->second->view))
                            throw std::invalid_argument("Incompatible tool view");
                    }
                    if constexpr (std::is_same_v<Table, std::vector<EditorCommandDesc>>) {
                        if (!inserted.second && String(d.view) != String(inserted.first->second->view))
                            throw std::invalid_argument("Incompatible command view");
                        if (!inserted.second &&
                            (d.area != inserted.first->second->area || d.effect != inserted.first->second->effect))
                            throw std::invalid_argument("Incompatible command effect or scope");
                    }
                    if constexpr (std::is_same_v<Table, std::vector<EditorQueryDesc>>) {
                        if (!inserted.second) {
                            const auto &other = *inserted.first->second;
                            if (d.area != other.area || d.fieldCount != other.fieldCount || d.flags != other.flags)
                                throw std::invalid_argument("Incompatible query fields");
                            for (uint32_t i = 0; i < d.fieldCount; ++i)
                                if (d.fields[i].type != other.fields[i].type ||
                                    String(d.fields[i].key) != String(other.fields[i].key))
                                    throw std::invalid_argument("Incompatible query fields");
                        }
                    }
                    if constexpr (std::is_same_v<Table, std::vector<EditorViewDesc>>) {
                        if (!inserted.second &&
                            (d.kind != inserted.first->second->kind || d.area != inserted.first->second->area ||
                             d.singleInstance != inserted.first->second->singleInstance ||
                             bool(d.navigate) != bool(inserted.first->second->navigate) ||
                             bool(d.pick) != bool(inserted.first->second->pick)))
                            throw std::invalid_argument("Incompatible view presentation");
                        if (!inserted.second) {
                            const auto &other = *inserted.first->second;
                            if (bool(d.inspect) != bool(other.inspect) || d.fieldCount != other.fieldCount)
                                throw std::invalid_argument("Incompatible view inspection");
                            for (uint32_t i = 0; i < d.fieldCount; ++i)
                                if (d.fields[i].type != other.fields[i].type ||
                                    String(d.fields[i].key) != String(other.fields[i].key))
                                    throw std::invalid_argument("Incompatible view inspection");
                        }
                    }
                }
            };
            for (auto &m : modules)
                add(*m);
            add(*copy);
        };
        validate(&Module::commands);
        validate(&Module::queries);
        validate(&Module::editors);
        validate(&Module::views);
        validate(&Module::tools);
        validate(&Module::toolbarItems);
        validate(&Module::themes);
        validate(&Module::fonts);
        validate(&Module::images);
        validate(&Module::documentData);
        std::set<std::string> properties, menus, locations, toolbarLocations;
        auto unique = [&](Module &m) {
            if (!m.enabled) return;
            for (auto &d : m.locations)
                if (!locations.insert(String(d.id)).second) throw std::invalid_argument("Duplicate menu location");
            for (auto &d : m.toolbars)
                if (!toolbarLocations.insert(String(d.id)).second)
                    throw std::invalid_argument("Duplicate toolbar location");
            for (auto &d : m.properties)
                if (!properties.insert(String(d.id)).second) throw std::invalid_argument("Duplicate property");
            for (auto &d : m.menus)
                if (!menus.insert(String(d.id)).second) throw std::invalid_argument("Duplicate menu item");
        };
        for (auto &m : modules)
            unique(*m);
        unique(*copy);
        modules.push_back(std::move(copy));
        return true;
    } catch (const std::exception &e) {
        error = e.what();
        return false;
    }
}
bool RegisteredModules::Load(const EditorHost *host, const std::string &path, std::string &error) {
    if (loading || discarding) {
        error = "Registration is closed";
        return false;
    }
    struct Loading {
        bool &flag;
        ~Loading() { flag = false; }
    } loadingGuard{loading};
    loading = true;
    std::filesystem::path file;
    try {
        if (path.empty() || path.find('\0') != std::string::npos) throw std::invalid_argument("Invalid module path");
        file = std::filesystem::canonical(std::filesystem::u8path(path));
        if (!std::filesystem::is_regular_file(file)) throw std::invalid_argument("Expected a module file");
    } catch (const std::exception &failure) {
        error = "Cannot load module: " + path + ": " + failure.what();
        return false;
    }
#ifdef _WIN32
    void *library =
        LoadLibraryExW(file.c_str(), nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    auto entry =
        library ? reinterpret_cast<EditorModuleEntry>(GetProcAddress(static_cast<HMODULE>(library), "NeMoDevModule"))
                : nullptr;
#else
    void *library = dlopen(file.c_str(), RTLD_NOW | RTLD_LOCAL);
    auto entry = library ? reinterpret_cast<EditorModuleEntry>(dlsym(library, "NeMoDevModule")) : nullptr;
#endif
    if (!entry) {
        CloseLibrary(library);
        error = "Cannot load module entry: " + path;
        return false;
    }
    EditorModuleDesc desc{};
    desc.size = sizeof(desc);
    desc.version = EDITOR_VERSION;
    struct FileScope {
        std::string &value, previous;
        ~FileScope() { value = std::move(previous); }
    } origin{sourceFile, sourceFile};
    sourceFile = file.u8string();
    const auto discard = [&] {
        if (desc.destroy) {
            try {
                desc.destroy(desc.data);
            } catch (...) {
            }
        }
        CloseLibrary(library);
    };
    try {
        if (entry(host, &desc) != EDITOR_OK) {
            discard();
            error = "Module entry failed";
            return false;
        }
        for (const auto &setting : moduleSettings)
            if (setting.enabled && ModuleFile(setting.path, sourceFile) && setting.id != Id(desc.id)) {
                discard();
                error = "Configured module has a different identifier: " + setting.id;
                return false;
            }
        if (!Register(desc, error)) {
            discard();
            return false;
        }
        modules.back()->library = library;
        return true;
    } catch (...) {
        discard();
        error = "Module entry raised an exception";
        return false;
    }
}
bool RegisteredModules::Assemble(EditorApplication &editor, const std::vector<EditorModuleSource> &sources,
                                 EditorDrawingVersion drawing, std::string &error,
                                 const EditorConfiguration &configuration,
                                 const std::vector<EditorModuleSetting> &settings,
                                 const EditorConfiguration &overrides) {
    if (assembling || loading || discarding) {
        error = "Registration is closed";
        return false;
    }
    const auto first = modules.size();
    std::string source;
    assembling = true;
    try {
        std::set<std::string> ids;
        auto selected = sources;
        moduleSettings = settings;
        for (auto &setting : moduleSettings) {
            Id(EditorText(setting.id));
            if (!ids.insert(setting.id).second || (setting.builtin && !setting.path.empty()))
                throw std::invalid_argument("Invalid or duplicate module setting: " + setting.id);
            if (setting.id == "application.operations" &&
                (!setting.enabled || setting.builtin || !setting.path.empty()))
                throw std::invalid_argument("The application operation registry cannot be replaced or disabled");
            if (!setting.path.empty()) {
                setting.path =
                    std::filesystem::weakly_canonical(std::filesystem::absolute(std::filesystem::u8path(setting.path)))
                        .u8string();
                if (setting.enabled && std::none_of(selected.begin(), selected.end(), [&](const auto &entry) {
                        return ModuleFile(entry.file, setting.path);
                    })) {
                    const auto file = setting.path;
                    selected.push_back(
                        {file, [file](auto &editor, auto &error) { return editor.Load(file, error); }, file});
                }
            }
        }
        for (const auto &entry : selected) {
            source = entry.name;
            error.clear();
            if (source.empty() || !entry.collect) throw std::invalid_argument("Invalid module source");
            if (!entry.file.empty()) {
                const auto file = std::filesystem::weakly_canonical(std::filesystem::u8path(entry.file)).u8string();
                if (std::any_of(moduleSettings.begin(), moduleSettings.end(), [&](const auto &setting) {
                        return !setting.enabled && !setting.path.empty() && ModuleFile(setting.path, file);
                    }))
                    continue;
            }
            const auto count = modules.size();
            if (!entry.collect(editor, error))
                throw std::runtime_error(error.empty() ? "Module collection failed" : error);
            if (modules.size() == count) throw std::runtime_error("Module source registered no module");
        }
        for (const auto &setting : moduleSettings)
            if (setting.enabled && std::none_of(modules.begin(), modules.end(), [&](const auto &module) {
                    return module->enabled && String(module->id) == setting.id;
                }))
                throw std::runtime_error("Configured module is missing or has a different identifier: " + setting.id);
        std::map<std::string, EditorModuleInfo> catalog;
        for (const auto &module : modules) {
            const auto id = String(module->id);
            if (!catalog.count(id) || module->enabled) catalog[id] = {id, module->file, module->enabled};
        }
        for (const auto &setting : moduleSettings)
            if (!catalog.count(setting.id)) catalog[setting.id] = {setting.id, setting.path, false};
        const auto provides = [](const Module &module, const ProviderChoice &choice) {
            const auto has = [&](auto member) {
                for (const auto &desc : module.*member)
                    if (String(desc.id) == choice.id && String(desc.provider) == choice.provider) return true;
                return false;
            };
            return choice.kind == "command"         ? has(&Module::commands)
                   : choice.kind == "query"         ? has(&Module::queries)
                   : choice.kind == "editor"        ? has(&Module::editors)
                   : choice.kind == "view"          ? has(&Module::views)
                   : choice.kind == "theme"         ? has(&Module::themes)
                   : choice.kind == "font"          ? has(&Module::fonts)
                   : choice.kind == "document-data" ? has(&Module::documentData)
                   : choice.kind == "image"         ? has(&Module::images)
                   : choice.kind == "toolbar"       ? has(&Module::toolbarItems)
                                                    : false;
        };
        defaults = configuration;
        // Only choices removed by module selection may disappear from the
        // defaults. A misspelled or otherwise unavailable provider still fails.
        defaults.providers.erase(
            std::remove_if(defaults.providers.begin(), defaults.providers.end(),
                           [&](const auto &choice) {
                               const auto has = [&](bool enabled) {
                                   return std::any_of(modules.begin(), modules.end(), [&](const auto &module) {
                                       return module->enabled == enabled && provides(*module, choice);
                                   });
                               };
                               return !has(true) && has(false);
                           }),
            defaults.providers.end());
        std::vector<std::unique_ptr<Module>> disabled;
        discarding = true;
        for (auto &module : modules)
            if (!module->enabled) disabled.push_back(std::move(module));
        modules.erase(std::remove(modules.begin(), modules.end(), nullptr), modules.end());
        for (auto it = disabled.rbegin(); it != disabled.rend(); ++it)
            (*it)->Destroy();
        disabled.clear();
        discarding = false;
        auto effective = defaults;
        for (const auto &choice : overrides.providers) {
            effective.providers.erase(
                std::remove_if(effective.providers.begin(), effective.providers.end(),
                               [&](const auto &old) { return old.kind == choice.kind && old.id == choice.id; }),
                effective.providers.end());
            effective.providers.push_back(choice);
        }
        std::set<std::string> bound;
        for (const auto &binding : overrides.bindings) {
            if (bound.insert(binding.first).second)
                effective.bindings.erase(std::remove_if(effective.bindings.begin(), effective.bindings.end(),
                                                        [&](const auto &old) { return old.first == binding.first; }),
                                         effective.bindings.end());
            effective.bindings.push_back(binding);
        }
        for (const auto &menu : overrides.menus) {
            effective.menus.erase(std::remove_if(effective.menus.begin(), effective.menus.end(),
                                                 [&](const auto &old) { return old.id == menu.id; }),
                                  effective.menus.end());
            effective.menus.push_back(menu);
        }
        for (const auto &item : overrides.toolbarItems) {
            effective.toolbarItems.erase(std::remove_if(effective.toolbarItems.begin(), effective.toolbarItems.end(),
                                                        [&](const auto &old) { return old.id == item.id; }),
                                         effective.toolbarItems.end());
            effective.toolbarItems.push_back(item);
        }
        assembling = false;
        source = "activation";
        if (editor.Activate(drawing, error, effective)) {
            moduleInfo.clear();
            for (const auto &info : catalog)
                moduleInfo.push_back(info.second);
            moduleSettings.clear();
            error.clear();
            return true;
        }
    } catch (const std::exception &failure) {
        error = failure.what();
    } catch (...) {
        error = "Module collection raised an exception";
    }
    assembling = true;
    DiscardModules(std::min(first, modules.size()));
    moduleSettings.clear();
    defaults = {};
    assembling = false;
    error = "Editor modules (" + source + "): " + error;
    return false;
}

} // namespace vdev::detail
