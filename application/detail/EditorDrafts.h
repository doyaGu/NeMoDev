#pragma once

#include "application/EditorApplication.h"
#include <algorithm>

namespace vdev {
class EditorApplication::EditDrafts {
  public:
    // Drafts live until confirmation/cancellation. A queued preview still
    // returns here for cleanup; the command queue must be emptied first.
    struct Draft {
        EditorScope scope;
        ObjectRef object;
        PropertyBinding property;
        const EditorCommandDesc *command = nullptr;
        const EditorValueEditorDesc *editor = nullptr;
        EditorField value;
        std::vector<EditorField> fields;
        EditorValueEditors editors;
        std::vector<EditorField> arguments;
        void *preview = nullptr;
        bool commandOnly = false;
        bool toolOwned = false;
        bool deferred = false;
        std::vector<EditorField> Fields(const EditorField &value) const;
    };
    explicit EditDrafts(std::function<bool(Draft &)> restore) : m_Restore(std::move(restore)) {}
    EditDrafts(const EditDrafts &) = delete;
    EditDrafts &operator=(const EditDrafts &) = delete;
    bool Empty() const { return m_Drafts.empty(); }
    bool Active(bool runningPending) const {
        return std::any_of(m_Drafts.begin(), m_Drafts.end(),
                           [runningPending](const auto &entry) { return !runningPending || !entry.second.deferred; });
    }
    Draft *Find(EditorEdit edit) {
        const auto found = m_Drafts.find(edit);
        return found == m_Drafts.end() ? nullptr : &found->second;
    }
    const Draft *Find(EditorEdit edit) const {
        const auto found = m_Drafts.find(edit);
        return found == m_Drafts.end() ? nullptr : &found->second;
    }
    void Insert(EditorEdit edit, Draft draft) { m_Drafts.emplace(edit, std::move(draft)); }
    // Individual completion has already restored or transferred its preview.
    void Erase(EditorEdit edit) { m_Drafts.erase(edit); }
    std::shared_ptr<Draft> Retain(Draft draft) {
        return std::shared_ptr<Draft>(new Draft(std::move(draft)), [this](Draft *value) {
            if (value->preview) m_Restore(*value);
            delete value;
        });
    }
    void Clear() {
        CancelWhere([](const Draft &) { return true; });
    }
    void CloseScope(EditorView view, bool toolOnly = false) {
        CancelWhere([view, toolOnly](const Draft &draft) {
            return draft.scope.view == view && (!toolOnly || draft.toolOwned);
        });
    }
    template <class Current> void RemoveInvalid(Current current) {
        CancelWhere([&](const Draft &draft) { return !current(draft.object); });
    }
    template <class Refresh, class Preview> void RefreshDeferred(Refresh refresh, Preview preview) {
        for (auto it = m_Drafts.begin(); it != m_Drafts.end();) {
            if (!it->second.deferred || (refresh(it->second) == EDITOR_OK && preview(it->second) == EDITOR_OK)) {
                ++it;
                continue;
            }
            auto draft = std::move(it->second);
            it = m_Drafts.erase(it);
            m_Restore(draft);
        }
    }

  private:
    template <class Matches> void CancelWhere(Matches matches) {
        for (;;) {
            const auto it = std::find_if(m_Drafts.begin(), m_Drafts.end(),
                                         [&](const auto &entry) { return matches(entry.second); });
            if (it == m_Drafts.end()) break;
            // Remove before invoking native cancellation so nested cleanup
            // cannot restore the same preview twice.
            auto draft = std::move(it->second);
            m_Drafts.erase(it);
            m_Restore(draft);
        }
    }
    std::map<EditorEdit, Draft> m_Drafts;
    std::function<bool(Draft &)> m_Restore;
};
} // namespace vdev
