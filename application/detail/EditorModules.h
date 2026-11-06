#pragma once

#include "application/EditorApplication.h"
#include "application/EditorModules.h"
#include "application/DocumentDataCallbacks.h"
#include <deque>

namespace vdev::detail {
struct Module {
    EditorString id{};
    void *data = nullptr;
    void(EDITOR_CALL *destroy)(void *) = nullptr;
    void(EDITOR_CALL *stop)(void *, const EditorHost *) = nullptr;
    EditorStatus(EDITOR_CALL *start)(void *, const EditorHost *) = nullptr;
    void(EDITOR_CALL *update)(void *, const EditorHost *, EditorModulePhase) = nullptr;
    EditorStatus(EDITOR_CALL *document)(void *, const EditorHost *, const EditorDocumentChange *) = nullptr;
    bool running = false;
    std::deque<std::string> strings;
    std::deque<std::vector<EditorParameter>> parameters;
    std::vector<EditorString> dependencies;
    std::vector<EditorString> optionalDependencies;
    std::vector<EditorCommandDesc> commands;
    std::vector<EditorQueryDesc> queries;
    std::vector<EditorPropertyDesc> properties;
    std::vector<EditorValueEditorDesc> editors;
    std::vector<EditorViewDesc> views;
    std::vector<EditorMenuDesc> menus;
    std::vector<EditorBindingDesc> bindings;
    std::vector<EditorMenuLocation> locations;
    std::vector<EditorToolDesc> tools;
    std::vector<EditorToolbarItemDesc> toolbarItems;
    std::vector<EditorToolbarLocation> toolbars;
    std::deque<std::vector<EditorField>> toolbarArguments;
    std::deque<EditorFieldValues> toolbarValues;
    std::vector<EditorThemeDesc> themes;
    std::vector<EditorFontDesc> fonts;
    std::vector<EditorImageDesc> images;
    std::vector<EditorDocumentDataDesc> documentData;
    std::shared_ptr<DocumentDataCallbacks> dataCallbacks = std::make_shared<DocumentDataCallbacks>();
    std::deque<std::vector<EditorField>> themeFields;
    std::deque<EditorFieldValues> themeValues;
    std::deque<std::vector<EditorString>> fontFiles;
    std::deque<std::vector<unsigned char>> imagePixels;
    void *library = nullptr;
    std::string file;
    bool enabled = true;
    EditorString Text(EditorString text, bool identifier = false);
    const EditorParameter *Parameters(const EditorParameter *source, uint32_t count);
    explicit Module(const EditorModuleDesc &source, const std::string &origin);
    void Destroy();
    ~Module();
};
class RegisteredModules {
  public:
    bool Register(const EditorModuleDesc &, std::string &);
    bool Load(const EditorHost *, const std::string &, std::string &);
    bool Assemble(EditorApplication &, const std::vector<EditorModuleSource> &, EditorDrawingVersion, std::string &,
                  const EditorConfiguration &, const std::vector<EditorModuleSetting> &, const EditorConfiguration &);
    const auto &All() const { return modules; }
    const auto &Active() const { return activation; }
    const auto &Information() const { return moduleInfo; }
    const auto &Defaults() const { return defaults; }
    bool Busy() const { return assembling || loading || discarding || callback; }
    bool Started() const { return started; }
    bool Stopped() const { return stopped; }
    void ClearActivation() { activation.clear(); }
    void Clear() {
        DiscardModules(0);
        activation.clear();
    }
    void PrepareActivation(EditorDrawingVersion imguiVersion);
    bool Start(const EditorHost *host, std::string &error);
    bool Update(const EditorHost *host, EditorModulePhase phase,
                const std::function<void(const EditorResponse &)> &report);
    void Stop(const EditorHost *host, const std::function<void(const EditorResponse &)> &report);
    bool Document(const EditorHost *, const EditorDocumentChange &, std::string &error);

  private:
    struct Callback {
        bool &active;
        explicit Callback(bool &active) : active(active) { active = true; }
        ~Callback() { active = false; }
    };
    static void Report(const std::function<void(const EditorResponse &)> &report, std::string message);
    std::vector<std::unique_ptr<Module>> modules;
    std::vector<Module *> activation;
    std::vector<EditorModuleInfo> moduleInfo;
    std::vector<EditorModuleSetting> moduleSettings;
    std::string sourceFile;
    EditorConfiguration defaults;
    bool assembling = false, loading = false, discarding = false;
    bool started = false, stopped = false, callback = false;
    std::vector<Module *> detached;
    std::vector<Module *> ModuleOrder() const;
    void DiscardModules(size_t first);
};
} // namespace vdev::detail
