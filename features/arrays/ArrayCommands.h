#pragma once
#include "application/EditorApplication.h"
namespace vdev {
struct Clipboard;
class ArrayCommands {
  public:
    ArrayCommands(std::function<Document *()> current, Clipboard &clipboard)
        : m_Current(std::move(current)), m_Clipboard(clipboard) {}
    bool Register(EditorApplication &, std::string &error);

  private:
    struct Entry {
        ArrayCommands *owner;
        unsigned kind;
    };
    std::vector<Entry> m_Entries;
    std::function<Document *()> m_Current;
    Clipboard &m_Clipboard;
    static EditorStatus Read(void *, const EditorHost *, const EditorRequest *, EditorSink);
    static EditorStatus Check(void *, const EditorHost *, const EditorRequest *);
    static EditorStatus Write(void *, const EditorHost *, const EditorRequest *, EditorSink);
};
} // namespace vdev
