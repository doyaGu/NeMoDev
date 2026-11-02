#pragma once
#include "application/EditorProfile.h"

namespace vdev {
class ProfileCommands {
  public:
    using Writer = std::function<bool(EditorProfile &, std::string &)>;
    ProfileCommands(EditorApplication &editor, EditorProfile &profile, Writer write)
        : m_Editor(editor), m_Profile(profile), m_Write(std::move(write)) {}
    bool Register(std::string &error);
  private:
    static EditorStatus Read(void *, const EditorHost *, const EditorRequest *, EditorSink);
    static EditorStatus Set(void *, const EditorHost *, const EditorRequest *, EditorSink);
    EditorApplication &m_Editor;
    EditorProfile &m_Profile;
    Writer m_Write;
    uint64_t m_Revision = 1;
};
} // namespace vdev
