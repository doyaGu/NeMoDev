#pragma once

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>
#include "editor/Editor.h"
#include "application/KeyBinding.h"

namespace vdev {

class Document;

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
struct EditorConfiguration {
    std::vector<ProviderChoice> providers;
    std::vector<std::pair<std::string, std::string>> bindings;
    std::vector<MenuPlacement> menus;
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
    EditorResponse SaveViewState(const EditorViewDesc &, void *data, EditorScope);
    EditorStatus RestoreViewState(const EditorViewDesc &, void *data, EditorScope, const std::vector<EditorField> &);
    EditorResponse InspectView(const EditorViewDesc &, void *data, EditorScope);
    uint64_t InspectRevision(const EditorViewDesc &, void *data);
    EditorStatus QueryRevision(const std::string &id, uint64_t &version);
    // Collect, validate, activate. No contribution becomes callable earlier.
    bool Register(const EditorModuleDesc &module, std::string &error);
    bool Load(const std::string &path, std::string &error);
    bool Activate(EditorDrawingVersion imguiVersion, std::string &error, const EditorConfiguration &configuration = {});
    bool Configure(const EditorConfiguration &configuration, std::string &error);
    const EditorConfiguration &Configuration() const;
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
    EditorStatus UpdateEdit(EditorEdit edit, const std::string &text);
    EditorStatus UpdateEdit(EditorEdit edit, const EditorValue &value);
    EditorStatus FinishEdit(EditorEdit edit, bool commit);
    uint32_t DrawEdit(EditorEdit edit, EditorString label, uint32_t focus);
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
    struct State;
    std::unique_ptr<State> m_State;
};

inline EditorString EditorText(const std::string &text) { return {text.data(), static_cast<uint32_t>(text.size())}; }
inline EditorString EditorLiteral(const char *text) {
    return {text, static_cast<uint32_t>(std::char_traits<char>::length(text))};
}
} // namespace vdev
