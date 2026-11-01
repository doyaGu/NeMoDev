#pragma once

#include <map>
#include <optional>
#include "workspace/EditorClient.h"
#include <set>
#include <string>
#include <vector>

#include "ui/app/EditorState.h"
#include "ui/controls/ParameterBox.h"
#include "core/schematic/SchematicSearch.h"
#include "ui/controls/ValueEditor.h"

namespace vdev {

class SchematicViews;

class EditorPanels {
public:
    EditorPanels(EditorState &state, SchematicViews &canvases) : m_State(state), m_Schematics(canvases) {}

    void ExplorerPanel(bool *open);

    // Shows the Scripts of the given Script's owner (Dev shows the Scripts of
    // the selected object) and centers the view on it.
    // Shows the Script's stack (all Scripts, or the owner's); focus scrolls
    // the Schematic to it (opening a file starts at the top of the stack).
    void ShowScript(CK_ID script, const ElementRef &ref = ElementRef(), CK_ID focusBehavior = 0, bool focus = true);
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
    // The Schematic Explorer's parts: the rows chosen made the Schematic's
    // selection; the tree view; the parameters of the Parameter Filter.
    void ExplorerSelect();
    SchematicNavigation &ExplorerNavigation();
    void ExplorerTree(const ImVec2 &min, const ImVec2 &max);
    ParameterBox *ExplorerValue(bool replace);
    EditorClient Client() const;
    bool ReadSearch(bool tree, std::vector<SearchResult> &);
    void Command(const char *, const std::vector<EditorField> &);
    void Rename(const SearchResult &);
    void Poll();
    std::vector<EditorField> m_ParameterTypes;
    uint64_t m_TypesRevision = 0;
    std::vector<std::pair<EditorOperation, bool>> m_Pending;

    int m_ExplorerRename = -1; // the row F2 renames (-2: the tree's node)
    std::string m_ExplorerRenameText;

    // What names the open composition's objects (CK gives their ids to the
    // next document's objects): made anew when it is closed; a field naming
    // an object goes here.
    struct DocumentState {
        std::optional<SearchResult> treeTarget;
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

    EditorState &m_State;
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
    ParameterBox m_ExplorerBox{m_State}, m_FilterValue{m_State}, m_ReplaceValue{m_State};
    bool m_ExplorerEditReplace = false;
    bool m_OpenExplorerValue = false;
};

} // namespace vdev
