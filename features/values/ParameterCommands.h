#pragma once
#include "application/EditorApplication.h"
namespace vdev {
class Clipboard;
enum class ParameterValueKind;
struct ParameterValue;
struct ParameterValueDescription;
EditorField ParameterValueField(const ParameterValue &, const std::string &key = "value");
std::vector<EditorField> ParameterFields(const ParameterValueDescription &);
bool ParameterValueFromEditor(const ParameterValueDescription &, const EditorValue &, ParameterValue &);
bool ParameterValueFromEditor(ParameterValueKind, const EditorValue &, ParameterValue &);
const char *ParameterProperty(ParameterValueKind);
class ParameterCommands {
  public:
    explicit ParameterCommands(std::function<Document *()> current, const Clipboard *clipboard = nullptr)
        : m_Current(std::move(current)), m_Clipboard(clipboard) {}
    bool Register(EditorApplication &, std::string &error);
  private:
    struct Entry { ParameterCommands *owner; unsigned kind; };
    std::vector<Entry> m_Entries;
    std::function<Document *()> m_Current;
    const Clipboard *m_Clipboard;
    static EditorStatus Watch(void *, const EditorHost *, const EditorRequest *, EditorSink);
    static EditorStatus ClipboardParameters(void *, const EditorHost *, const EditorRequest *, EditorSink);
    static EditorStatus ScriptLocals(void *, const EditorHost *, const EditorRequest *, EditorSink);
    static EditorStatus WriteText(void *, const EditorHost *, const EditorRequest *, EditorSink);
    static EditorStatus Read(void *, const EditorHost *, const EditorRequest *, EditorSink);
    static EditorStatus Selections(void *, const EditorHost *, const EditorRequest *, EditorSink);
    static EditorStatus Choices(void *, const EditorHost *, const EditorRequest *, EditorSink);
    static EditorStatus Describe(void *, const EditorHost *, const EditorRequest *, EditorSink);
    static EditorStatus Check(void *, const EditorHost *, const EditorRequest *);
    static EditorStatus Write(void *, const EditorHost *, const EditorRequest *, EditorSink);
    static EditorStatus Copy(void *, const EditorHost *, const EditorRequest *, EditorSink);
    static EditorStatus CurvePreview(void *, const EditorHost *, const EditorRequest *, EditorSink);
};
} // namespace vdev
