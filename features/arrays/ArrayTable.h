#pragma once
#include "workspace/EditorClient.h"
#include <set>
namespace vdev {
// Selection and focus belong to the window. Values and undo belong to the
// registered property/command providers, including built-in table actions.
struct ArrayTable {
    ObjectRef target{};
    uint64_t revision = 0;
    std::set<int> selected;
    int anchor = -1, column = -1, focusRow = -1, focusColumn = -1;
    int editRow = -1, editColumn = -1;
    EditorEdit cell = 0, dialog = 0;
    unsigned activeInput = 0;
    std::string popup;
    bool openPopup = false, openDelete = false;
    int deleteColumn = -1;
    EditorOperation selectionOperation = 0;
    std::set<int> selectionAfter;
    void Cancel(const EditorClient &);
    void Draw(const EditorClient &, ObjectRef);
};
bool RegisterArrayValueEditors(EditorApplication &, std::string &error);
} // namespace vdev
