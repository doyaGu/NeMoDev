#pragma once

#include "application/EditorApplication.h"
#include <algorithm>

namespace vdev {
class EditorApplication::ViewTools {
  public:
    // Choices belong to a view instance. Input and restoration ownership are
    // temporary and always unwind to the previous view, including nested calls.
    ViewTools() = default;
    ViewTools(const ViewTools &) = delete;
    ViewTools &operator=(const ViewTools &) = delete;
    struct Tool {
        std::string type, choice, fallback;
        const EditorToolDesc *handler = nullptr;
        bool entered = false, dispatching = false, captured = false, reset = false, focused = false;
    };
    void Open(EditorView view, std::string type, std::string fallback) {
        auto &tool = m_Tools[view];
        tool.type = std::move(type);
        tool.fallback = std::move(fallback);
    }
    Tool *Find(EditorView view) {
        const auto found = m_Tools.find(view);
        return found == m_Tools.end() ? nullptr : &found->second;
    }
    const Tool *Find(EditorView view) const {
        const auto found = m_Tools.find(view);
        return found == m_Tools.end() ? nullptr : &found->second;
    }
    const auto &Entries() const { return m_Tools; }
    void Close(EditorView view) { m_Tools.erase(view); }
    void Clear() {
        m_Tools.clear();
        m_Input = m_Restoring = 0;
    }
    bool Captured(EditorView view) const {
        const auto *tool = Find(view);
        return tool && tool->captured;
    }
    bool Captured() const {
        return std::any_of(m_Tools.begin(), m_Tools.end(), [](const auto &entry) { return entry.second.captured; });
    }
    bool Busy() const {
        return std::any_of(m_Tools.begin(), m_Tools.end(),
                           [](const auto &entry) { return entry.second.captured || entry.second.dispatching; });
    }
    EditorView InputView() const { return m_Input; }
    EditorView RestoringView() const { return m_Restoring; }
    class Restore {
      public:
        Restore(EditorView &current, EditorView view) : m_Current(current), m_Previous(current) { current = view; }
        ~Restore() { m_Current = m_Previous; }
        Restore(const Restore &) = delete;
        Restore &operator=(const Restore &) = delete;

      private:
        EditorView &m_Current;
        EditorView m_Previous;
    };
    Restore Restoring(EditorView view) { return Restore(m_Restoring, view); }
    class Dispatch {
      public:
        Dispatch(ViewTools &tools, EditorView view) : m_Tool(*tools.Find(view)), m_Input(tools.m_Input, view) {
            m_Tool.dispatching = true;
        }
        ~Dispatch() { m_Tool.dispatching = false; }
        Dispatch(const Dispatch &) = delete;
        Dispatch &operator=(const Dispatch &) = delete;

      private:
        Tool &m_Tool;
        Restore m_Input;
    };
    Dispatch DispatchTo(EditorView view) { return Dispatch(*this, view); }

  private:
    std::map<EditorView, Tool> m_Tools;
    EditorView m_Restoring = 0, m_Input = 0;
};
} // namespace vdev
