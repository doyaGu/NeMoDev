#include "features/objects/SelectionCommands.h"
#include "application/EditorApplication.h"
#include "application/ObjectSelection.h"
#include "core/document/Document.h"
#include <algorithm>
#include <stdexcept>
#include <unordered_set>

namespace vdev {
namespace {
const EditorValue &Field(const EditorRequest &request, const char *key) {
    for (uint32_t i = 0; i < request.count; ++i)
        if (std::string(request.values[i].key.data, request.values[i].key.size) == key)
            return request.values[i];
    throw std::invalid_argument("Missing selection argument");
}
} // namespace
EditorStatus SelectionCommands::Read(void *data, const EditorHost *, const EditorRequest *, EditorSink sink) {
    const auto &selection = static_cast<SelectionCommands *>(data)->m_Selection;
    const EditorValue fields[] = {
        {EDITOR_TEXT("objects"),
         EDITOR_OBJECT_LIST,
         {},
         {},
         0,
         {selection.References().data(), static_cast<uint32_t>(selection.References().size())}},
        {EDITOR_TEXT("locked"), EDITOR_INTEGER_VALUE, {}, {}, uint64_t(selection.Locked())},
        {EDITOR_TEXT("revision"), EDITOR_INTEGER_VALUE, {}, {}, selection.Revision()}};
    return sink.write ? sink.write(sink.data, fields, 3) : EDITOR_INVALID;
}
EditorStatus SelectionCommands::Unlocked(void *data, const EditorHost *, const EditorRequest *) {
    return static_cast<SelectionCommands *>(data)->m_Selection.Locked() ? EDITOR_REFUSED : EDITOR_OK;
}
EditorStatus SelectionCommands::Replace(void *data, const EditorHost *, const EditorRequest *request, EditorSink sink) {
    auto &self = *static_cast<SelectionCommands *>(data);
    const auto &objects = Field(*request, "objects").objects;
    const auto renderOnly = Field(*request, "render-only").integer;
    if (renderOnly > 1)
        return EDITOR_INVALID;
    if (!objects.count) {
        const auto status = Clear(data, nullptr, request, {});
        const EditorValue version{EDITOR_TEXT("revision"), EDITOR_INTEGER_VALUE, {}, {}, self.m_Selection.Revision()};
        if (sink.write) sink.write(sink.data, &version, 1);
        return status;
    }
    std::vector<CK_ID> ids;
    auto *document = self.m_Current();
    if (!document)
        return EDITOR_STALE;
    for (uint32_t i = 0; i < objects.count; ++i) {
        auto *object = document->Resolve(objects.data[i]);
        if (!object)
            return EDITOR_STALE;
        if (!renderOnly || CKRenderObject::Cast(object))
            ids.push_back(object->GetID());
    }
    const bool changed = ids != self.m_Selection.Objects();
    if (changed) self.m_Selection.Set(*document, ids);
    const EditorValue version{EDITOR_TEXT("revision"), EDITOR_INTEGER_VALUE, {}, {}, self.m_Selection.Revision()};
    if (sink.write) sink.write(sink.data, &version, 1);
    return changed ? EDITOR_OK : EDITOR_NO_CHANGE;
}
EditorStatus SelectionCommands::Invert(void *data, const EditorHost *, const EditorRequest *request, EditorSink) {
    auto &self = *static_cast<SelectionCommands *>(data);
    const auto &objects = Field(*request, "objects").objects;
    if (!objects.count)
        return Clear(data, nullptr, request, {});
    auto *document = self.m_Current();
    if (!document)
        return EDITOR_STALE;
    std::vector<CK_ID> ids;
    const std::unordered_set<CK_ID> selected(self.m_Selection.Objects().begin(), self.m_Selection.Objects().end());
    for (uint32_t i = 0; i < objects.count; ++i) {
        auto *object = document->Resolve(objects.data[i]);
        if (!object)
            return EDITOR_STALE;
        if (!selected.count(object->GetID()))
            ids.push_back(object->GetID());
    }
    if (ids == self.m_Selection.Objects())
        return EDITOR_NO_CHANGE;
    self.m_Selection.Set(*document, ids);
    return EDITOR_OK;
}
EditorStatus SelectionCommands::Restore(void *data, const EditorHost *, const EditorRequest *request, EditorSink) {
    auto &self = *static_cast<SelectionCommands *>(data);
    const auto locked = Field(*request, "locked").integer;
    if (locked > 1)
        return EDITOR_INVALID;
    const auto &objects = Field(*request, "objects").objects;
    if (!objects.count)
        return Clear(data, nullptr, request, {});
    auto *document = self.m_Current();
    if (!document)
        return EDITOR_STALE;
    std::vector<CK_ID> ids;
    for (uint32_t i = 0; i < objects.count; ++i) {
        const auto id = document->NativeId(objects.data[i]);
        if (!id)
            return EDITOR_STALE;
        ids.push_back(id);
    }
    self.m_Selection.Restore(*document, ids, locked != 0);
    return EDITOR_OK;
}
EditorStatus SelectionCommands::Lock(void *data, const EditorHost *, const EditorRequest *request, EditorSink) {
    auto &selection = static_cast<SelectionCommands *>(data)->m_Selection;
    const auto locked = Field(*request, "locked").integer;
    if (locked > 1)
        return EDITOR_INVALID;
    const bool next = locked && !selection.Objects().empty();
    if (selection.m_Locked == next)
        return EDITOR_NO_CHANGE;
    selection.m_Locked = next;
    return EDITOR_OK;
}
EditorStatus SelectionCommands::Clear(void *data, const EditorHost *, const EditorRequest *, EditorSink) {
    auto &selection = static_cast<SelectionCommands *>(data)->m_Selection;
    if (selection.Objects().empty())
        return EDITOR_NO_CHANGE;
    selection.Reset();
    return EDITOR_OK;
}
bool SelectionCommands::Register(EditorApplication &editor, std::string &error) {
    const EditorParameter targets[] = {{EDITOR_TEXT("objects"), EDITOR_OBJECT_LIST},
                                       {EDITOR_TEXT("render-only"), EDITOR_INTEGER_VALUE, 1}};
    const EditorParameter restore[] = {{EDITOR_TEXT("objects"), EDITOR_OBJECT_LIST},
                                       {EDITOR_TEXT("locked"), EDITOR_INTEGER_VALUE}};
    const EditorParameter locked{EDITOR_TEXT("locked"), EDITOR_INTEGER_VALUE};
    const EditorCommandDesc commands[] = {
        {sizeof(EditorCommandDesc), EDITOR_TEXT("selection.replace"), EDITOR_TEXT("builtin.selection"),
         EDITOR_TEXT("Select Objects"), targets, 2, Replace, this, EDITOR_DOCUMENT, EDITOR_SESSION_CHANGE, Unlocked},
        {sizeof(EditorCommandDesc), EDITOR_TEXT("selection.invert"), EDITOR_TEXT("builtin.selection"),
         EDITOR_TEXT("Invert Selection"), targets, 1, Invert, this, EDITOR_DOCUMENT, EDITOR_SESSION_CHANGE, Unlocked},
        {sizeof(EditorCommandDesc), EDITOR_TEXT("selection.restore"), EDITOR_TEXT("builtin.selection"),
         EDITOR_TEXT("Restore Selection"), restore, 2, Restore, this, EDITOR_DOCUMENT, EDITOR_SESSION_CHANGE, nullptr},
        {sizeof(EditorCommandDesc), EDITOR_TEXT("selection.lock"), EDITOR_TEXT("builtin.selection"),
         EDITOR_TEXT("Lock Selection"), &locked, 1, Lock, this, EDITOR_DOCUMENT, EDITOR_SESSION_CHANGE, nullptr},
        {sizeof(EditorCommandDesc), EDITOR_TEXT("selection.clear"), EDITOR_TEXT("builtin.selection"),
         EDITOR_TEXT("Clear Selection"), nullptr, 0, Clear, this, EDITOR_DOCUMENT, EDITOR_SESSION_CHANGE, Unlocked}};
    const EditorParameter fields[] = {{EDITOR_TEXT("objects"), EDITOR_OBJECT_LIST},
                                      {EDITOR_TEXT("locked"), EDITOR_INTEGER_VALUE},
                                      {EDITOR_TEXT("revision"), EDITOR_INTEGER_VALUE}};
    EditorQueryDesc query{sizeof(query),
                          EDITOR_TEXT("selection.objects"),
                          EDITOR_TEXT("builtin.selection"),
                          nullptr,
                          0,
                          Read,
                          this,
                          EDITOR_QUERY_EACH_READ,
                          EDITOR_APPLICATION,
                          fields,
                          3};
    EditorModuleDesc module{};
    module.size = sizeof(module);
    module.version = EDITOR_VERSION;
    module.id = EditorLiteral("objects.selection");
    module.commands = commands;
    module.commandCount = 5;
    module.queries = &query;
    module.queryCount = 1;
    return editor.Register(module, error);
}
} // namespace vdev
