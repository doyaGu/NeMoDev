#pragma once

#include <map>
#include <optional>
#include "workspace/EditorClient.h"
#include "features/preferences/PreferencesData.h"
#include <set>
#include <string>
#include <vector>

#include "ui/controls/ParameterBox.h"
#include "core/schematic/SchematicSearch.h"
#include "core/schematic/SchematicNavigation.h"
#include "ui/controls/ValueEditor.h"

namespace vdev {

class SchematicViews;
class EditorWorkspace;

class EditorPanels {
public:
    EditorPanels(ParameterCopies &copies, EditorWorkspace &workspace, SchematicViews &canvases)
        : m_Workspace(workspace), m_Schematics(canvases), m_ExplorerBox(copies), m_FilterValue(copies), m_ReplaceValue(copies) {}
    bool Register(EditorApplication &, std::string &error);

    void ExplorerPanel(bool *open);

    // The stack made again (the Scripts or the filters changed), for the
    // Script it was made for while it exists.
    void RefreshStack();
    void FocusExplorer() { m_FocusSearch = true; }
    // F3 / Shift+F3: show the next / previous Explorer result.
    void FindNext(int direction);
    // The Schematic Explorer's results of its last search.
    const std::vector<SearchResult> &SearchResults() const { return m_Doc.results; }
    // The composition is closed: the ids kept (rows, results, a dialog's
    // object) name objects of the next one after ClearAll.
    void DocumentClosed();
private:
    void RunSearch();
    void CloseExplorer(bool keep);
    PreferencesData m_Preferences;
    // The Schematic Explorer's parts: the rows chosen made the Schematic's
    // selection; the tree view; the parameters of the Parameter Filter.
    void ExplorerSelect();
    SchematicNavigation &ExplorerNavigation();
    void ExplorerTree(const ImVec2 &min, const ImVec2 &max);
    ParameterBox *ExplorerValue(bool replace);
    EditorClient Client() const;
    bool ReadSearch(bool tree, std::vector<SearchResult> &);
    void Command(const char *, const std::vector<EditorField> &);
    void ExplorerRenameDialog(const char *id);
    void Poll();
    std::vector<EditorField> m_ParameterTypes;
    uint64_t m_TypesRevision = 0;
    std::vector<std::pair<EditorOperation, bool>> m_Pending;

    std::string m_ExplorerRenameText;

    // What names the open composition's objects (CK gives their ids to the
    // next document's objects): made anew when it is closed; a field naming
    // an object goes here.
    struct DocumentState {
        std::optional<SearchResult> treeTarget;
        ObjectRef renameTarget; // the object F2 selected, independent of refreshed rows
        // The Schematic Explorer's results (made again when the query or
        // the composition changes), the rows chosen (Shift's anchor), the
        // Schematic's selection when it opened (Cancel gives it back), the
        // addresses copied from the search query.
        std::vector<SearchResult> results;
        int currentResult = -1;
        std::set<int> chosen;
        int anchor = -1;
        Selection selectionBefore;
        EditorView explorerView = 0;

        std::string resultsKey;
    };

    EditorWorkspace &m_Workspace;
    DocumentState m_Doc;
    SchematicViews &m_Schematics;
    // Schematic Explorer (Dev's Schematic Filter Box, dialog 732): Show in
    // List (Dev's default: Scripts), the CK ID and Text Filters, Parse All
    // Items, the tree view, the Parameter Filter (mode, type, Derived).
    std::string m_SearchText;
    std::string m_SearchIdText;
    unsigned m_SearchKinds = SearchScripts;
    bool m_SearchAllScripts = false;
    bool m_SearchTree = false;
    int m_SearchFilter = 0;
    uint64_t m_SearchType = 0;
    bool m_SearchDerived = false;
    bool m_FocusSearch = false;
    bool m_ExplorerShown = false;
    ParameterBox m_ExplorerBox, m_FilterValue, m_ReplaceValue;
    bool m_ExplorerEditReplace = false;
    bool m_OpenExplorerValue = false;
};

} // namespace vdev
