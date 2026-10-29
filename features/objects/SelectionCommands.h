#pragma once

#include "editor/Editor.h"
#include <functional>
#include <string>

namespace vdev {
class Document;
class EditorApplication;
class ObjectSelection;
class SelectionCommands {
  public:
    SelectionCommands(std::function<Document *()> current, ObjectSelection &selection)
        : m_Current(std::move(current)), m_Selection(selection) {}
    bool Register(EditorApplication &editor, std::string &error);

  private:
    static EditorStatus Read(void *, const EditorHost *, const EditorRequest *, EditorSink);
    static EditorStatus Replace(void *, const EditorHost *, const EditorRequest *, EditorSink);
    static EditorStatus Invert(void *, const EditorHost *, const EditorRequest *, EditorSink);
    static EditorStatus Restore(void *, const EditorHost *, const EditorRequest *, EditorSink);
    static EditorStatus Lock(void *, const EditorHost *, const EditorRequest *, EditorSink);
    static EditorStatus Clear(void *, const EditorHost *, const EditorRequest *, EditorSink);
    static EditorStatus Unlocked(void *, const EditorHost *, const EditorRequest *);
    std::function<Document *()> m_Current;
    ObjectSelection &m_Selection;
};
} // namespace vdev
