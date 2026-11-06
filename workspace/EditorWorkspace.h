#pragma once

#include "application/EditorApplication.h"
#include "application/WorkspaceState.h"
#include "application/DocumentViews.h"
#include <map>
#include <string>
#include <vector>

namespace vdev {
// Windows keep only their input's ID. Drafts, chosen editors and native target
// validation belong to EditorApplication. Closing an owner cancels its drafts.
class EditorWorkspace : private EditorViewNavigation, public DocumentViews {
  public:
    explicit EditorWorkspace(EditorApplication &application);
    ~EditorWorkspace();
    bool Register(std::string &error);
    EditorApplication &Application() { return m_Application; }
    bool Begin(const char *property, ObjectRef object, EditorEdit &edit, EditorView &owner,
               const std::vector<EditorField> &arguments = {});
    bool DrawInput(EditorEdit &edit, const char *label, bool focus = false, bool *active = nullptr,
                   EditorValueStyle style = EDITOR_VALUE_INLINE, float width = 0, float height = 0);
    bool Finish(EditorEdit &edit, bool commit);
    void CloseOwner(EditorView &owner);
    bool Shortcut(const std::string &action, bool consume = false) const override;
    const char *ShortcutLabel(const char *property) const;
    bool EditMenu(const char *location, ObjectRef target, EditorEdit &edit, EditorView &owner);
    void CommandMenu(const char *location);
    void CommandShortcuts();
    EditorView ActionView(const std::string &, EditorView source) const override;
    EditorStatus Toolbar(const std::string &, EditorScope, const EditorToolbarFrame &) override;
    void Shortcuts(EditorScope) override;
    bool InputBinding(EditorScope, const std::string &, EditorInputPhase, bool) const override;
    bool Binding(const std::string &, EditorInputPhase, bool consume) const;
    void Draw(bool nativeAvailable = true);
    void Menu();
    EditorStatus Menu(const std::string &, EditorScope, ObjectRef, EditorEdit &) override;
    EditorView Open(const std::string &type, const std::string &role = {}, bool pinned = false);
    EditorView OpenRole(const std::string &role);
    bool Configure(const WorkspaceConfiguration &, std::string &error);
    void ConfigurationWriter(
        std::function<bool(const EditorConfiguration &, const WorkspaceState &, std::string &)> writer) {
        m_WriteConfiguration = std::move(writer);
    }
    WorkspaceConfiguration Configuration() const;
    bool Save(WorkspaceState &, std::string &error) const;
    bool Restore(const WorkspaceState &, std::string &error);
    bool LoadLayout(const std::string &, std::string &error);
    bool Unavailable(EditorView) const;
    bool SavingBlocked() const { return m_RestoreFailed || m_Detached; }
    struct LayoutUpdate {
        bool changed = false;
        EditorView focus = 0;
        std::vector<unsigned> closeLegacy;
    };
    LayoutUpdate UpdateLayout(unsigned dockspace, bool reset = false, bool closeSeparated = false);
    unsigned RegionDock(const std::string &region) const;
    void CloseFloatingViews();
    void DockViews();
    EditorView Role(const std::string &role) const;
    const EditorResponse &Inspect(EditorView view);
    std::vector<ObjectRef> Pick(float x, float y, bool *handled = nullptr, uint32_t objectClass = 0) const;
    bool Interacting() const;
    bool Focus(EditorView view);
    EditorView Focused() const;
    EditorView LastFocused(const std::string &type) const;
    std::string WindowName(EditorView view) const;
    bool Close(EditorView view);
    void CloseAll();
    void DocumentChanging();
    void DetachDocument(bool replaceContext) override;
    void RestoreDocument() override;
    void DocumentReplaced(DocumentToken, uint64_t revision, bool open) override;
    bool DocumentInteractionActive() const override { return Interacting(); }
    bool Configure(const EditorConfiguration &configuration, std::string &error);
    size_t ViewCount() const { return m_Views.size(); }

  private:
    struct View {
        EditorView id;
        EditorViewDesc desc;
        void *data;
        bool focus;
        struct Placement {
            float x = 0, y = 0, width = 0, height = 0;
            unsigned dock = 0;
            bool apply = false;
        } placement;
        bool pinned = false, active = false;
        std::string role;
        std::vector<EditorPickRegion> picks;
        int pickFrame = -1;
        int drawnFrame = -1;
        EditorResponse inspection;
        uint64_t inspectionRevision = 0;
        DocumentToken inspectionDocument{};
        bool inspected = false;
        uint64_t instance = 0;
        bool unavailable = false;
        std::vector<unsigned> windows; // top-level windows begun by this view
    };
    EditorStatus Navigate(const EditorViewDesc &, EditorView, const EditorRequest &, EditorSink) override;
    EditorStatus Inspect(EditorView, EditorSink) override;
    EditorStatus PickRegions(EditorView, const EditorPickRegion *, uint32_t) override;
    EditorView Create(const EditorViewDesc &desc, const std::string &role = {});
    bool PrepareClose(const View &, std::string &error);
    void Destroy(EditorView view);
    static EditorStatus OpenCommand(void *, const EditorHost *, const EditorRequest *, EditorSink);
    static EditorStatus FocusCommand(void *, const EditorHost *, const EditorRequest *, EditorSink);
    static EditorStatus CloseCommand(void *, const EditorHost *, const EditorRequest *, EditorSink);
    static EditorStatus ViewQuery(void *, const EditorHost *, const EditorRequest *, EditorSink);
    static EditorStatus ListQuery(void *, const EditorHost *, const EditorRequest *, EditorSink);
    static EditorStatus InstanceQuery(void *, const EditorHost *, const EditorRequest *, EditorSink);
    bool Configure(const EditorConfiguration &, const WorkspaceConfiguration *, std::string &error);
    bool Validate(const WorkspaceConfiguration &, bool restoring, std::string &) const;
    EditorView Reopen(uint64_t, std::string &);
    View UnavailableView(const WorkspaceViewState &, EditorView);
    bool ApplySettings(const WorkspaceViewState &, const EditorViewDesc &, void *, EditorView, std::string &);
    bool CaptureView(const View &, const WorkspaceConfiguration &, WorkspaceViewState &, std::string &) const;
    bool Capture(const std::map<EditorView, View> &, const WorkspaceConfiguration &, WorkspaceState &,
                 std::string &) const;
    void UpdateRegionDocks();
    unsigned ViewDock(const View &) const;
    const WorkspaceViewPlacement *Placement(const View &) const;
    std::vector<unsigned> BuildLayout(unsigned dockspace, bool closeSeparated);
    bool Checked(const MenuEntry &, EditorScope);
    void CommandResult(const EditorResponse &);
    EditorApplication &m_Application;
    bool m_Detached = false, m_ReplacingContext = false;
    DocumentToken m_DetachedDocument{};
    std::map<EditorView, View> m_Views;
    EditorView m_LastFocusRequest = 0;
    EditorView m_DragOwner = 0;
    unsigned m_DragId = 0;
    std::map<std::string, EditorView> m_LastFocused;
    EditorView m_LastWindowFocus = 0;
    WorkspaceConfiguration m_Configuration;
    std::map<std::string, unsigned> m_RegionDocks;
    uint64_t m_ViewsRevision = 1;
    unsigned m_Dockspace = 0;
    bool m_LayoutPrepared = false, m_LayoutChanged = false;
    void ToolMenus();
    std::map<std::string, std::string> m_BindingDrafts;
    std::string m_NewToolbarId, m_NewToolbarTitle, m_NewToolbarAction, m_NewToolbarLocation;
    float m_NewToolbarPosition[2] = {0, 0}, m_NewToolbarSize[2] = {70, 19};
    struct SavedView {
        uint32_t version;
        std::vector<EditorField> fields;
        View::Placement placement;
        std::string tool;
    };
    // Singletons use (type, ""); named instances use (type, role).
    std::map<std::pair<std::string, std::string>, SavedView> m_ClosedStates;
    std::map<uint64_t, WorkspaceViewState> m_Saved;
    uint64_t m_NextInstance = 1;
    bool m_RestoreFailed = false;
    EditorEdit m_MenuEdit = 0;
    EditorView m_MenuOwner = 0;
    bool m_OpenMenuEdit = false;
    std::string m_Error;
    std::function<bool(const EditorConfiguration &, const WorkspaceState &, std::string &)> m_WriteConfiguration;
    EditorResponse m_NoView{EDITOR_STALE};
};
} // namespace vdev
