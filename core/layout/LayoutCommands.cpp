#include "core/layout/LayoutCommands.h"

#include "core/document/Document.h"
#include "core/document/Edit.h"

namespace vdev {
namespace {
using MemberList = std::set<CK_ID> LayoutLists::*;
MemberList Members(LayoutList list) {
    switch (list) {
    case LayoutList::Cones: return &LayoutLists::cones;
    case LayoutList::PlayerShown: return &LayoutLists::playerShown;
    case LayoutList::HiddenCurvePoints: return &LayoutLists::hiddenCurvePoints;
    }
    return nullptr;
}
} // namespace

class LayoutListAction : public Action {
public:
    LayoutListAction(LayoutList list, CK_ID object, bool listed) : m_List(list), m_Object(object), m_Listed(listed) {}
    bool Redo(Document &document) override { return Apply(document, m_Listed); }
    bool Undo(Document &document) override { return Apply(document, !m_Listed); }

private:
    bool Apply(Document &document, bool listed) {
        const auto member = Members(m_List);
        if (!member) return false;
        auto &members = document.m_Layout.*member;
        if (listed) members.insert(m_Object);
        else members.erase(m_Object);
        document.m_LayoutChanged = true;
        return true;
    }
    LayoutList m_List;
    CK_ID m_Object;
    bool m_Listed;
};

class FrameRepresentationAction : public Action {
public:
    FrameRepresentationAction(std::vector<std::pair<CK_ID, int>> before, int representation)
        : m_Before(std::move(before)), m_Representation(representation) {}
    bool Redo(Document &document) override { return Apply(document, true); }
    bool Undo(Document &document) override { return Apply(document, false); }

private:
    bool Apply(Document &document, bool after) {
        for (const auto &entry : m_Before) {
            const int value = after ? m_Representation : entry.second;
            if (value < 0) document.m_Layout.representations.erase(entry.first);
            else document.m_Layout.representations[entry.first] = value;
        }
        document.m_LayoutChanged = true;
        return true;
    }
    std::vector<std::pair<CK_ID, int>> m_Before;
    int m_Representation;
};

EditResult SetLayoutMembership(Document &document, LayoutList list, CK_ID object,
                               bool listed, const std::string &label) {
    if (!document.IsOpen() || !document.IsLive(object)) return EditResult::Refused("No live object");
    const auto member = Members(list);
    if (!member) return EditResult::Refused("Invalid Layout list");
    if (((document.Layout().*member).count(object) != 0) == listed) return EditResult::NoChange();
    Edit edit(document, label);
    if (!edit.Do(std::make_unique<LayoutListAction>(list, object, listed))) return edit.Fail(label + " failed");
    return edit.Commit();
}

EditResult SetFrameRepresentation(Document &document, const std::vector<CK_ID> &frames, int representation) {
    if (!document.IsOpen() || representation < 0) return EditResult::Refused("Invalid frame representation");
    std::map<CK_ID, int> before;
    for (CK_ID id : frames) {
        if (!document.IsLive(id)) continue;
        const auto found = document.Layout().representations.find(id);
        const int previous = found != document.Layout().representations.end() ? found->second : -1;
        if ((previous < 0 ? 1 : previous) != representation) before.emplace(id, previous);
    }
    if (before.empty()) return EditResult::NoChange();
    Edit edit(document, "Change Representation");
    if (!edit.Do(std::make_unique<FrameRepresentationAction>(
            std::vector<std::pair<CK_ID, int>>(before.begin(), before.end()), representation)))
        return edit.Fail("Change Representation failed");
    return edit.Commit();
}
} // namespace vdev
