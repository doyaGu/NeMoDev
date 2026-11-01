#pragma once

#include "application/EditorApplication.h"
#include "core/document/Document.h"

namespace vdev {

// Native descriptions contain copied values and complete object references.
struct SetupDescription {
    Document &document;
    std::vector<EditorField> fields;
    void Integer(const char *, int64_t);
    void Real(const char *, double);
    void Text(const char *, std::string);
    void Link(const char *, CKObject *);
    void Numbers(const char *, std::vector<double>);
    void Rows(const char *, std::vector<EditorField>);
    EditorField Row(CKObject *) const;
};

std::string SetupObjectName(CKObject *, const char *none = "-- None --");
CKBehavior *SetupScriptOf(CKBehavior *);
CKBehavior *SetupBehaviorOf(CKObject *);
std::string SetupTypeName(CKContext *, CKParameterType);
std::string SetupGuidText(CKGUID);

bool DescribeSetupObject(CKObject *, const std::string &, const std::vector<uint64_t> &, SetupDescription &);
bool DescribeSetupRender(CKObject *, const std::string &, const std::vector<uint64_t> &, SetupDescription &);
bool DescribeSetupScene(CKObject *, const std::string &, const std::vector<uint64_t> &, SetupDescription &);
void AppendSetupPageQueries(std::vector<EditorQueryDesc> &, std::function<Document *()> *);
void AppendSetupPageCommands(std::vector<EditorCommandDesc> &, std::function<Document *()> *);

} // namespace vdev
