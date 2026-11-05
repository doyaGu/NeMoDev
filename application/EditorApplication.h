#pragma once

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>
#include "editor/Editor.h"
#include "application/KeyBinding.h"

namespace vdev {

class Document;
class DocumentDataRegistry;
struct EditorModuleSource;
struct EditorModuleSetting;
struct EditorModuleInfo;

// Owned internal values; C consumers use the copying sink instead.
struct EditorField {
    std::string key;
    EditorValueType type = EDITOR_TEXT_VALUE;
    ObjectRef object{};
    std::string text;
    uint64_t integer = 0;
    std::vector<ObjectRef> objects;
    std::vector<uint64_t> integers;
    std::vector<std::string> texts;
    double real = 0;
    std::vector<double> reals;
    std::vector<EditorField> fields;
};
bool operator==(const EditorField &, const EditorField &);
inline bool operator!=(const EditorField &a, const EditorField &b) { return !(a == b); }
// Borrowed C views of owned fields; the fields must outlive this value.
struct EditorFieldValues {
    EditorFieldValues() = default;
    EditorFieldValues(const EditorFieldValues &) = delete;
    EditorFieldValues &operator=(const EditorFieldValues &) = delete;
    EditorFieldValues(EditorFieldValues &&) = default;
    EditorFieldValues &operator=(EditorFieldValues &&) = default;
    std::vector<EditorString> texts;
    std::vector<EditorValue> values;
    std::vector<EditorValue> members;
    const EditorValue *data() const { return values.data(); }
    size_t size() const { return values.size(); }
};
EditorFieldValues BorrowEditorFields(const std::vector<EditorField> &);
using EditorValueEditors = std::map<std::string, const EditorValueEditorDesc *>;
struct EditorResponse {
    EditorStatus status = EDITOR_ERROR;
    std::string message;
    std::vector<EditorField> fields;
    EditorOperation operation = 0;
    uint64_t documentRevision = 0;
};
// Shared bounded value copying for internal clients of the public C interface.
EditorStatus CopyEditorFields(std::vector<EditorField> &, const EditorValue *, uint32_t);
struct ProviderChoice {
    std::string kind, id, provider;
};
struct MenuPlacement {
    std::string id, location;
    int order = 0;
};
struct ToolbarPlacement {
    std::string id, location, action, title;
    float x = 0, y = 0, width = 0, height = 0;
    // Only specified fields override an existing item. New items need location,
    // action and geometry. Hidden items retain their saved settings.
    std::vector<EditorField> arguments;
    uint32_t fields = 0;
    bool visible = true;
};
enum ToolbarFields : uint32_t {
    ToolbarLocation = 1,
    ToolbarAction = 2,
    ToolbarTitle = 4,
    ToolbarPosition = 8,
    ToolbarSize = 16,
    ToolbarVisibility = 32,
    ToolbarArguments = 64
};
struct EditorConfiguration {
    std::vector<ProviderChoice> providers;
    std::vector<std::pair<std::string, std::string>> bindings;
    std::vector<MenuPlacement> menus;
    std::vector<ToolbarPlacement> toolbarItems;
};
struct ToolbarEntry {
    std::string id, provider, location, title, action, checked, tooltip;
    std::vector<EditorField> arguments;
    float x = 0, y = 0, width = 0, height = 0;
    uint32_t flags = 0;
    EditorToolbarDraw draw = nullptr;
    void *data = nullptr;
};
struct PropertyBinding {
    std::string id, label, query, field, command, argument, editor, choices;
    EditorValueType type = EDITOR_TEXT_VALUE;
    std::string commandProvider, queryProvider, choicesProvider;
};
struct MenuEntry {
    std::string id, action, location, label;
    int order = 0;
    std::string checked;
};

class EditorViewNavigation {
  public:
    virtual ~EditorViewNavigation() = default;
    virtual EditorStatus Navigate(const EditorViewDesc &, EditorView, const EditorRequest &, EditorSink) = 0;
    virtual EditorStatus Inspect(EditorView, EditorSink) = 0;
    virtual EditorStatus PickRegions(EditorView, const EditorPickRegion *, uint32_t) = 0;
    virtual bool Shortcut(const std::string &, bool consume = false) const = 0;
    virtual EditorStatus Menu(const std::string &, EditorScope, ObjectRef, EditorEdit &) = 0;
    virtual EditorView ActionView(const std::string &, EditorView source) const { return source; }
    virtual EditorStatus Toolbar(const std::string &, EditorScope, const EditorToolbarFrame &) {
        return EDITOR_REFUSED;
    }
    virtual void Shortcuts(EditorScope) {}
    virtual bool InputBinding(EditorScope, const std::string &, EditorInputPhase, bool) const { return false; }
};

class EditorApplication {
  public:
    explicit EditorApplication(std::function<Document *()> current, std::function<bool()> nativeAvailable = {});
    ~EditorApplication();
    EditorApplication(const EditorApplication &) = delete;
    EditorApplication &operator=(const EditorApplication &) = delete;
    const EditorHost *Host() const;
    void ViewNavigation(EditorViewNavigation *navigation);
    void *CreateView(const EditorViewDesc &, EditorView);
    bool DestroyView(const EditorViewDesc &, void *data);
    bool ClosingView(const EditorViewDesc &, void *, EditorView);
    EditorResponse SaveViewState(const EditorViewDesc &, void *data, EditorScope);
    EditorResponse SaveViewSettings(const EditorViewDesc &, void *data, EditorScope);
    EditorStatus RestoreViewSettings(const EditorViewDesc &, void *, EditorScope, const std::vector<EditorField> &);
    EditorStatus RestoreViewState(const EditorViewDesc &, void *data, EditorScope, const std::vector<EditorField> &);
    EditorResponse InspectView(const EditorViewDesc &, void *data, EditorScope);
    EditorResponse PickView(const EditorViewDesc &, void *data, EditorScope, float x, float y,
                            uint32_t objectClass = 0);
    uint64_t InspectRevision(const EditorViewDesc &, void *data);
    EditorStatus QueryRevision(const std::string &id, uint64_t &version);
    // Collect, validate, activate. No contribution becomes callable earlier.
    bool Register(const EditorModuleDesc &module, std::string &error);
    bool Load(const std::string &path, std::string &error);
    // Collect all sources and activate together. Failure stops and removes
    // only modules added by this call; existing registrations remain usable.
    bool Assemble(const std::vector<EditorModuleSource> &, EditorDrawingVersion, std::string &error,
                  const EditorConfiguration &configuration = {},
                  const std::vector<EditorModuleSetting> &modules = {},
                  const EditorConfiguration &overrides = {});
    std::vector<EditorModuleInfo> Modules() const;
    std::string ProviderModule(const ProviderChoice &) const;
    const EditorConfiguration &DefaultConfiguration() const;
    bool Activate(EditorDrawingVersion imguiVersion, std::string &error, const EditorConfiguration &configuration = {});
    bool Configure(const EditorConfiguration &configuration, std::string &error);
    const EditorConfiguration &Configuration() const;
    std::shared_ptr<const DocumentDataRegistry> DataTypes() const;
    void Attach(DocumentToken document, uint64_t revision, bool available, bool writable);
    void NativeAvailable(bool available);
    bool NativeAvailable() const;
    uint64_t Revision() const;
    bool HasEdits() const;
    ObjectRef EditTarget(EditorEdit edit) const;
    void SetSelection(ObjectRef object);
    ObjectRef Selection() const;
    EditorView OpenScope();
    void CloseScope(EditorView view);
    EditorScope Scope(EditorView view = 0) const;
    EditorResponse Query(const std::string &id, const EditorRequest &request);
    // Same query path, borrowing its owned projection until the next query or
    // mutation. Internal views need not copy unchanged results each frame.
    const EditorResponse &ReadQuery(const std::string &id, const EditorRequest &request);
    EditorStatus CommandState(const std::string &id, const EditorRequest &request);
    EditorStatus CanEdit(const std::string &property, EditorScope scope, ObjectRef target) const;
    EditorResponse Execute(const std::string &id, const EditorRequest &request);
    bool CompleteOperation(EditorOperation operation, EditorResponse result);
    EditorStatus BeginEdit(const std::string &property, EditorScope scope, ObjectRef target, EditorEdit &out);
    EditorStatus BeginEdit(const std::string &property, const EditorRequest &, EditorEdit &out);
    EditorStatus BeginCommand(const std::string &command, const EditorRequest &, EditorEdit &out);
    EditorStatus UpdateEdit(EditorEdit edit, const std::string &text);
    EditorStatus UpdateEdit(EditorEdit edit, const EditorValue &value);
    EditorStatus FinishEdit(EditorEdit edit, bool commit, EditorOperation *operation = nullptr);
    uint32_t DrawEdit(EditorEdit edit, EditorString label, uint32_t focus, EditorValueStyle style = EDITOR_VALUE_INLINE,
                      float width = 0, float height = 0);
    bool EditState(EditorEdit edit, std::string &value, EditorValueEditorDesc &editor) const;
    bool EditState(EditorEdit edit, EditorField &value, EditorValueEditorDesc &editor) const;
    uint32_t DrawValue(const std::string &editor, const EditorValueInput &);
    uint32_t DrawValue(const EditorValueEditorDesc &, const EditorValueInput &, const EditorValueEditors * = nullptr);
    EditorValueEditors CaptureValueEditors() const;
    bool ValueEditor(const std::string &, EditorValueEditorDesc &) const;
    // Native dialog copies join their already-open core edit. The selected
    // property/editor are captured when that dialog creates the copy.
    EditorResponse ApplyEdit(const PropertyBinding &, EditorScope, ObjectRef, const EditorValue &,
                             const std::vector<EditorField> &context);
    EditorResponse ApplyEdit(const std::string &command, const EditorRequest &);
    uint32_t DrawProperty(const std::string &, EditorScope, ObjectRef, EditorEdit &, EditorString label);
    uint32_t DrawProperty(const std::string &, const EditorRequest &, EditorEdit &, EditorString label);
    const PropertyBinding *Property(const std::string &id) const;
    const std::vector<MenuEntry> &Menus() const;
    const std::vector<ToolbarEntry> &ToolbarItems() const;
    std::vector<EditorToolbarItemDesc> ToolbarProviders() const;
    std::vector<EditorCommandDesc> Commands(bool allProviders = false) const;
    std::vector<EditorToolDesc> Tools(bool allProviders = false) const;
    const std::map<std::string, EditorToolbarLocation> &Toolbars() const;
    std::vector<EditorThemeDesc> Themes(bool allProviders = false) const;
    std::vector<EditorFontDesc> Fonts(bool allProviders = false) const;
    std::vector<EditorImageDesc> Images(bool allProviders = false) const;
    const std::map<std::string, EditorField> &ThemeValues(const std::string &id) const;
    uint64_t AppearanceRevision() const;
    std::string ActionView(const std::string &action) const;
    const std::map<std::string, std::string> &BindingViews() const;
    EditorInputPhase BindingPhase(const std::string &) const;
    EditorStatus SelectTool(EditorScope, const std::string &);
    std::string ToolChoice(EditorView) const;
    const EditorToolDesc *ActiveTool(EditorView) const;
    EditorStatus ToolInput(EditorScope, const EditorToolInput &);
    void CancelTool(EditorView);
    bool ToolCaptured(EditorView) const;
    void RestoreTool(EditorView, const std::string &choice);
    const std::map<std::string, std::string> &MenuLocations() const;
    const std::vector<std::pair<std::string, std::string>> &Bindings() const;
    const std::map<std::string, std::vector<EditorKey>> &Shortcuts() const;
    const char *ActionTitle(const std::string &action) const;
    bool IsCommand(const std::string &action) const;
    std::string CommandProvider(const std::string &) const;
    std::vector<EditorViewDesc> Views(bool allProviders = false) const;
    std::vector<EditorValueEditorDesc> ValueEditors() const;
    // Callback execution pins registered code. Mutating configuration is
    // refused while a draft/callback is active; callers keep the old profile.
    void EnterDraw();
    void LeaveDraw();
    void EndFrame();
    void Stop();
    std::function<void(const EditorResponse &)> report;

  private:
    struct CachedQuery;
    const EditorResponse &ReadQuery(std::string_view, const EditorRequest &,
                                    std::shared_ptr<const CachedQuery> *retained);
    EditorStatus DeliverQuery(std::string_view, const EditorRequest &, EditorSink);
    struct State;
    std::unique_ptr<State> m_State;
};

inline EditorString EditorText(const std::string &text) { return {text.data(), static_cast<uint32_t>(text.size())}; }
inline EditorString EditorLiteral(const char *text) {
    return {text, static_cast<uint32_t>(std::char_traits<char>::length(text))};
}
} // namespace vdev
