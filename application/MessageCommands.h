#pragma once

#include "application/EditorApplication.h"
#include <deque>

namespace vdev {

// Application messages survive document and view replacement. Producers and
// dialogs use the same commands; only the first message can be answered.
class MessageCommands {
  public:
    bool Register(EditorApplication &editor, std::string &error);
  private:
    struct Message {
        uint64_t id;
        std::string text, title, format;
        uint64_t icon;
        bool cancel;
    };
    static EditorStatus Show(void *, const EditorHost *, const EditorRequest *, EditorSink);
    static EditorStatus Respond(void *, const EditorHost *, const EditorRequest *, EditorSink);
    static EditorStatus Read(void *, const EditorHost *, const EditorRequest *, EditorSink);
    std::deque<Message> m_Messages;
    uint64_t m_Next = 1, m_Revision = 1;
};

} // namespace vdev
