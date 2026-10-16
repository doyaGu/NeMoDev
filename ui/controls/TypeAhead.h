#pragma once

#include <string>
#include <vector>

#include "imgui.h"

namespace vdev {

// A browser's type-ahead for a list of Selectables (a list box, a combo's
// list, a Setup's list): letters typed while the list has the focus move
// its highlight (the keyboard focus, as the arrow keys do), they choose
// nothing and leave a combo's list open. The first letter goes to the next
// item starting with it, the letters after it to the first item starting
// with all of them ("A", then "Ad"), from the highlighted one on (else the
// selected one), wrapping; the same letter again goes to the next item
// starting with it; a pause starts again. While there is a highlight it is
// shown selected (the selected item not); Enter chooses it as a click does
// (a combo's list closes), Escape leaves the list as it was. Made in the
// list's window before its items; Selectable for each item, in the order
// shown.
class TypeAhead {
public:
    enum Flags {
        None = 0,
        // The list shares its window (a table without scrolling in a page):
        // the typing is its while one of its items has the keyboard focus,
        // not while the window has the focus.
        Shared = 1 << 0,
    };

    explicit TypeAhead(int flags = None);
    ~TypeAhead();
    TypeAhead(const TypeAhead &) = delete;
    TypeAhead &operator=(const TypeAhead &) = delete;

    bool Selectable(const char *label, bool selected = false, ImGuiSelectableFlags flags = 0,
                    const ImVec2 &size = ImVec2(0, 0));

private:
    int m_Flags;
    ImGuiID m_Key;
    bool m_Focused = false;
    bool m_ShowHighlight = false; // the list had a highlight at the last frame
    bool m_Highlighted = false;   // and has one now
    int m_Selected = -1;
    int m_Current = -1; // the item with the keyboard focus
    std::vector<std::string> m_Labels;
};

// ImGui::Combo with the type-ahead.
bool TypeAheadCombo(const char *label, int *current, const char *const items[], int count);

} // namespace vdev
