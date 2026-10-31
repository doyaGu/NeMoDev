#pragma once

#include "application/EditorApplication.h"

namespace vdev {
class SchematicCommands {
public:
    explicit SchematicCommands(std::function<Document *()> current);
    bool Register(EditorApplication &, std::string &error);
    static const std::vector<ProviderChoice> &DefaultProviders();

private:
    struct Entry {
        SchematicCommands *owner;
        bool comment;
        bool reset = false;
    };
    std::function<Document *()> m_Current;
    Entry m_Entries[3];
    static EditorStatus Read(void *, const EditorHost *, const EditorRequest *, EditorSink);
    static EditorStatus Check(void *, const EditorHost *, const EditorRequest *);
    static EditorStatus Write(void *, const EditorHost *, const EditorRequest *, EditorSink);
    static EditorStatus Replace(void *, const EditorHost *, const EditorRequest *, EditorSink);
};
} // namespace vdev
