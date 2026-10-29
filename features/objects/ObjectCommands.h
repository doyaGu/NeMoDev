#pragma once

#include "editor/Editor.h"
#include <functional>
#include <memory>
#include <string>

namespace vdev {
class Document;
class EditorApplication;

// Native object queries, creation and edits. Views receive owned values and
// invoke registered commands; they cannot borrow this document.
class ObjectCommands {
  public:
    explicit ObjectCommands(std::function<Document *()> current);
    ~ObjectCommands();
    bool Register(EditorApplication &editor, std::string &error);

  private:
    static EditorStatus Read(void *, const EditorHost *, const EditorRequest *, EditorSink);
    static EditorStatus Rename(void *, const EditorHost *, const EditorRequest *, EditorSink);
    static EditorStatus Execute(void *, const EditorHost *, const EditorRequest *, EditorSink);
    static EditorStatus DeleteOptions(void *, const EditorHost *, const EditorRequest *, EditorSink);
    struct State;
    std::unique_ptr<State> m_State;
    std::function<Document *()> m_Current;
};
} // namespace vdev
