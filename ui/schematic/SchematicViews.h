#pragma once
#include "editor/Editor.h"
#include <memory>
#include <deque>
#include <string>

namespace vdev {
class EditorWorkspace;
struct SchematicStyle;
struct ViewOptions;
struct SchematicNavigation;
struct ElementRef;
struct ElementAddress;
class SchematicCanvas;
class EditorApplication;

// Workspace owns the canvases. This module routes Schematic navigation to a
// particular instance and applies document-wide notifications to all of them.
class SchematicViews {
public:
    SchematicViews(EditorWorkspace &, const SchematicStyle &);
    ~SchematicViews();
    bool Register(EditorApplication &, std::string &error);
    SchematicCanvas *Find(EditorView) const;
    SchematicCanvas &Current() const;
    EditorView CurrentId() const;
    ViewOptions &Defaults();
    void ApplyOptions(const ViewOptions &);
    void Validate();
    void CancelInteraction();
    void DocumentClosed();
    void Invalidate();
    void RefreshValues();
    void RefreshStacks();
    void ClearSelections();
    bool Busy() const;
    bool DropFile(const std::wstring &, const std::string &, float x, float y);
    EditorStatus Show(const ElementAddress &, ObjectRef focus = {}, bool select = true, EditorView view = 0);
    void RequestShow(const ElementAddress &, ObjectRef focus = {}, EditorView view = 0);
    void ShowRequested();
    void Focus(EditorView view = 0);
    std::string Window(EditorView view = 0) const;
private:
    bool RegisterControls(EditorApplication &, EditorModuleDesc, std::string &);
    struct ToolAction {
        SchematicViews *views;
        std::string id;
    };
    std::deque<ToolAction> m_ToolActions;
    struct Data;
    std::unique_ptr<Data> m_Data;
};
} // namespace vdev
