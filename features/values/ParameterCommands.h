#pragma once
#include "application/EditorApplication.h"
namespace vdev {
enum class ParameterValueKind;
struct ParameterScalar;
bool ParameterScalarFromEditor(ParameterValueKind, const EditorValue &, ParameterScalar &);
const char *ParameterProperty(ParameterValueKind);
class ParameterCommands {
  public:
    explicit ParameterCommands(std::function<Document *()> current) : m_Current(std::move(current)) {}
    bool Register(EditorApplication &, std::string &error);
  private:
    struct Entry { ParameterCommands *owner; unsigned kind; };
    Entry m_Entries[5]{};
    std::function<Document *()> m_Current;
    static EditorStatus Read(void *, const EditorHost *, const EditorRequest *, EditorSink);
    static EditorStatus Choices(void *, const EditorHost *, const EditorRequest *, EditorSink);
    static EditorStatus Describe(void *, const EditorHost *, const EditorRequest *, EditorSink);
    static EditorStatus Check(void *, const EditorHost *, const EditorRequest *);
    static EditorStatus Write(void *, const EditorHost *, const EditorRequest *, EditorSink);
};
} // namespace vdev
