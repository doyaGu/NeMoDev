#pragma once

#include <map>
#include <string>
#include <vector>
#include "application/EditorApplication.h"
#include "workspace/WorkspaceConfiguration.h"

namespace vdev {
// Windows keep only their input's ID. Drafts, chosen editors and native target
// validation belong to EditorApplication. Closing an owner cancels its drafts.
class EditorWorkspace : private EditorViewNavigation {
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
    void Draw(bool nativeAvailable = true);
    void Menu();
    EditorStatus Menu(const std::string &, EditorScope, ObjectRef, EditorEdit &) override;
    EditorView Open(const std::string &type, const std::string &role = {}, bool pinned = false);
    EditorView OpenRole(const std::string &role);
    bool Configure(const WorkspaceConfiguration &, std::string &error);
    WorkspaceConfiguration Configuration() const;
    struct LayoutUpdate {
        bool changed = false;
        std::vector<unsigned> closeLegacy;
    };
    LayoutUpdate UpdateLayout(unsigned dockspace, bool reset = false, bool closeSeparated = false);
    unsigned RegionDock(const std::string &region) const;
    void CloseFloatingViews();
    void DockViews();
    EditorView Role(const std::string &role) const;
    const EditorResponse &Inspect(EditorView view);
    std::vector<ObjectRef> Pick(float x, float y, bool *handled = nullptr) const;
    bool Interacting() const;
    bool Focus(EditorView view);
    EditorView Focused() const;
    std::string WindowName(EditorView view) const;
    bool Close(EditorView view);
    void CloseAll();
    void DocumentChanging();
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
    };
    EditorStatus Navigate(const EditorViewDesc &, EditorView, const EditorRequest &, EditorSink) override;
    EditorStatus Inspect(EditorView, EditorSink) override;
    EditorStatus PickRegions(EditorView, const EditorPickRegion *, uint32_t) override;
    EditorView Create(const EditorViewDesc &desc);
    void Destroy(EditorView view);
    static EditorStatus OpenCommand(void *, const EditorHost *, const EditorRequest *, EditorSink);
    static EditorStatus FocusCommand(void *, const EditorHost *, const EditorRequest *, EditorSink);
    static EditorStatus CloseCommand(void *, const EditorHost *, const EditorRequest *, EditorSink);
    static EditorStatus ViewQuery(void *, const EditorHost *, const EditorRequest *, EditorSink);
    static EditorStatus ListQuery(void *, const EditorHost *, const EditorRequest *, EditorSink);
    static EditorStatus InstanceQuery(void *, const EditorHost *, const EditorRequest *, EditorSink);
    bool Configure(const EditorConfiguration &, const WorkspaceConfiguration *, std::string &error);
    void UpdateRegionDocks();
    unsigned ViewDock(const View &) const;
    const WorkspaceViewPlacement *Placement(const View &) const;
    std::vector<unsigned> BuildLayout(unsigned dockspace, bool closeSeparated);
    bool Checked(const MenuEntry &, EditorScope);
    EditorApplication &m_Application;
    std::map<EditorView, View> m_Views;
    EditorView m_LastFocusRequest = 0;
    WorkspaceConfiguration m_Configuration;
    std::map<std::string, unsigned> m_RegionDocks;
    uint64_t m_ViewsRevision = 1;
    unsigned m_Dockspace = 0;
    bool m_LayoutPrepared = false, m_LayoutChanged = false;
    struct SavedView {
        uint32_t version;
        std::vector<EditorField> fields;
        View::Placement placement;
    };
    std::map<std::string, SavedView> m_ClosedStates;
    EditorEdit m_MenuEdit = 0;
    EditorView m_MenuOwner = 0;
    bool m_OpenMenuEdit = false;
    std::string m_Error;
    EditorResponse m_NoView{EDITOR_STALE};
};
} // namespace vdev
