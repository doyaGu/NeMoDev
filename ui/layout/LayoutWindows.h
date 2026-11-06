#pragma once
#include "ui/layout/LayoutWindow.h"
#include "workspace/EditorWorkspace.h"
#include <map>
#include <deque>

namespace vdev {
// The workspace owns each window's input state. This collection owns its
// renderer and resolves commands to the window that supplied their scope.
class LayoutWindows {
  public:
    LayoutWindows(CkRuntimeHost &, ParameterCopies &, EditorApplication &, EditorWorkspace &);
    bool Register(std::string &);
    LayoutWindow &Primary();
    const LayoutWindow &Primary() const;
    LayoutWindow &Active();
    const LayoutWindow &Active() const;
    LayoutWindow *Find(EditorView) const;
    LayoutTarget Target(EditorView);
    bool Start(SDL_Window *, std::string *);
    void Stop();
    void DocumentClosed();
    void CancelEdits();
    bool HandleEvent(const SDL_Event &);
    bool Busy() const;
    bool FullScreen() const;
    void UpdateInput();
    std::function<void(EditorView, LayoutObject)> createObject;
    template <class F> void ForEach(F &&call) {
        if (m_Instances.empty())
            call(LayoutTarget{0, m_Primary.m_View.get(), &m_Primary});
        else
            for (auto &entry : m_Instances)
                call(LayoutTarget{entry.first, entry.second.window->m_View.get(), entry.second.window});
    }

  private:
    friend class LayoutWindow;
    void PrepareFullScreen(LayoutWindow &);
    struct Entry {
        LayoutWindow *window = nullptr;
        std::unique_ptr<LayoutWindow> owned;
    };
    void *Create(EditorView);
    void Destroy(void *);
    bool IsPrimary(EditorView) const;
    bool RegisterControls(EditorModuleDesc, std::string &);
    struct Action {
        LayoutWindows *owner;
        int command;
        std::string title;
    };
    std::deque<Action> m_Actions;
    CkRuntimeHost &m_Runtime;
    ParameterCopies &m_Copies;
    EditorApplication &m_Editor;
    EditorWorkspace &m_Workspace;
    LayoutWindow m_Primary;
    std::map<EditorView, Entry> m_Instances;
    SDL_Window *m_MainWindow = nullptr;
    LayoutWindow *m_InputOwner = nullptr;
};
} // namespace vdev
