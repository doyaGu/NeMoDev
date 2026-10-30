#pragma once
#include "application/EditorApplication.h"
namespace vdev {
enum class ParameterValueKind;
struct ParameterValue;
EditorValue BorrowParameterValue(const ParameterValue &, EditorString key);
bool ParameterValueFromEditor(ParameterValueKind, const EditorValue &, ParameterValue &);
const char *ParameterProperty(ParameterValueKind);
class ParameterCommands {
  public:
    explicit ParameterCommands(std::function<Document *()> current) : m_Current(std::move(current)) {}
    bool Register(EditorApplication &, std::string &error);
  private:
    struct Entry { ParameterCommands *owner; unsigned kind; };
    std::vector<Entry> m_Entries;
    std::function<Document *()> m_Current;
    static EditorStatus Read(void *, const EditorHost *, const EditorRequest *, EditorSink);
    static EditorStatus Choices(void *, const EditorHost *, const EditorRequest *, EditorSink);
    static EditorStatus Describe(void *, const EditorHost *, const EditorRequest *, EditorSink);
    static EditorStatus Check(void *, const EditorHost *, const EditorRequest *);
    static EditorStatus Write(void *, const EditorHost *, const EditorRequest *, EditorSink);
};
} // namespace vdev
